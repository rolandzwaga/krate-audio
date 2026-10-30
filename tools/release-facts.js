#!/usr/bin/env node
// Records the platform facts of a finished release asset: the macOS pkg's architecture set and minimum macOS, the
// Linux tarball's glibc floor and payload listing (spec specs/krate-manager-phase2/spec.md FR-010 to FR-016, D1-D3;
// plan §4.2, PD-4, PD-26).
//
// WHY: release.json advertises a platform key and a min_os per asset, and the manager decides from them whether an
// asset can load on the user's machine. Both the release jobs and the backfill measure the facts from the asset itself
// by this one code path, never from the build tree. The workflow runs pkgutil, tar, lipo and objdump; this tool only
// reads their results from disk. It runs no child process, uses no network, and names no plugin.
//
// Usage:
//   node tools/release-facts.js darwin --pkg <file> --expanded <dir> --out <json>
//   node tools/release-facts.js linux --tarball <file> --extracted <dir> --objdump <txt> --out <json>
//   node tools/release-facts.js linux-precheck --tarball <file>
//   darwin          walks the pkgutil --expand-full tree (links skipped), reads every Mach-O's load commands, writes
//                   {asset, size, sha256, platform_key, min_os, archs, macho} and prints `<archs>\t<path>` per Mach-O
//                   for the workflow's lipo -archs cross-check
//   linux           checks the tarball's listing and layout, requires the extracted tree to list identically, takes
//                   the glibc floor from objdump -T output, writes {asset, size, sha256, min_os, payload}
//   linux-precheck  the tarball's listing, single-bundle rule and layout, before anything extracts it; prints nothing
// Exit codes: 0 done, 1 a fact cannot be recorded (--out untouched), 2 usage.
// Errors go to stderr as `release-facts: <CODE>: <message>`; the codes are in tools/feed-contract/CONTRACT.md §7.
// Exports main(argv, io), machoFacts, darwinFactsFromTree, platformKeyForArchs, glibcFloor, linuxPrecheck, linuxFacts.
'use strict';
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const { ContractError, CODES } = require('./feed-contract/contract.js');
const listing = require('./feed-contract/listing.js');

const USAGE = [
  'usage: node tools/release-facts.js darwin --pkg <file> --expanded <dir> --out <json>',
  '       node tools/release-facts.js linux --tarball <file> --extracted <dir> --objdump <txt> --out <json>',
  '       node tools/release-facts.js linux-precheck --tarball <file>',
  '',
].join('\n');

const json = (value) => JSON.stringify(value, null, 2) + '\n';
const show = (value) => JSON.stringify(value);
const bytewise = (a, b) => Buffer.compare(Buffer.from(a, 'utf8'), Buffer.from(b, 'utf8'));
const fail = (code, message, where) => new ContractError(code, message, where);

// --- Mach-O (cctools loader.h / fat.h, xnu machine.h; spec External facts)

const MH_MAGIC = 0xfeedface;
const MH_CIGAM = 0xcefaedfe;
const MH_MAGIC_64 = 0xfeedfacf;
const MH_CIGAM_64 = 0xcffaedfe;
const FAT_MAGIC = 0xcafebabe;
const FAT_MAGIC_64 = 0xcafebabf;
const LC_VERSION_MIN_MACOSX = 0x24;
const LC_BUILD_VERSION = 0x32;
const PLATFORM_MACOS = 1;
const CPU_TYPE_X86_64 = 0x01000007;
const CPU_TYPE_ARM64 = 0x0100000c;
const CPU_SUBTYPE_MASK = 0xff000000;
const THIN_HEADER_64 = 32;

const hex = (v) => `0x${(v >>> 0).toString(16).padStart(8, '0')}`;

/** cputype / cpusubtype -> 'x86_64' | 'arm64'; anything else E_FACTS_ARCH (FR-011). */
function archOf(cputype, cpusubtype, where) {
  if (cputype === CPU_TYPE_X86_64) return 'x86_64';
  const sub = (cpusubtype & ~CPU_SUBTYPE_MASK) >>> 0;
  if (cputype === CPU_TYPE_ARM64 && (sub === 0 || sub === 1)) return 'arm64';
  throw fail('E_FACTS_ARCH', `${where}: cputype ${hex(cputype)} cpusubtype ${hex(cpusubtype)} is not x86_64 or arm64`);
}

/** The xxxx.yy.zz nibble encoding as text: X.Y, or X.Y.Z when Z is not 0 (D3). */
function versionText(v) {
  const x = v >>> 16;
  const y = (v >>> 8) & 0xff;
  const z = v & 0xff;
  return z === 0 ? `${x}.${y}` : `${x}.${y}.${z}`;
}

/** Numeric, part by part; a missing part counts as 0. */
function compareVersions(a, b) {
  const pa = a.split('.').map(Number);
  const pb = b.split('.').map(Number);
  for (let i = 0; i < Math.max(pa.length, pb.length); ++i) {
    const d = (pa[i] || 0) - (pb[i] || 0);
    if (d !== 0) return d;
  }
  return 0;
}

const highest = (versions) => versions.reduce((best, v) => (compareVersions(v, best) > 0 ? v : best));

/** One thin 64-bit little-endian Mach-O: its arch and its minimum macOS from the load commands. */
function thinFacts(buf, where) {
  if (buf.length < 4) throw fail('E_FACTS_MACHO_CORRUPT', `${where}: shorter than a Mach-O magic`);
  const magic = buf.readUInt32LE(0);
  if (magic === MH_MAGIC || magic === MH_CIGAM) {
    throw fail('E_FACTS_ARCH', `${where}: a 32-bit Mach-O (${hex(magic)})`);
  }
  if (magic === MH_CIGAM_64) throw fail('E_FACTS_ARCH', `${where}: a big-endian 64-bit Mach-O (${hex(magic)})`);
  if (magic !== MH_MAGIC_64) throw fail('E_FACTS_MACHO_CORRUPT', `${where}: magic ${hex(magic)} is not a Mach-O`);
  if (buf.length < THIN_HEADER_64) throw fail('E_FACTS_MACHO_CORRUPT', `${where}: the header runs past the end`);
  const arch = archOf(buf.readUInt32LE(4), buf.readUInt32LE(8), where);
  const ncmds = buf.readUInt32LE(16);
  const end = THIN_HEADER_64 + buf.readUInt32LE(20);
  if (end > buf.length) throw fail('E_FACTS_MACHO_CORRUPT', `${where}: sizeofcmds runs past the end`);
  let buildMin = null;
  let versionMin = null;
  let off = THIN_HEADER_64;
  for (let i = 0; i < ncmds; ++i) {
    if (off + 8 > end) throw fail('E_FACTS_MACHO_CORRUPT', `${where}: load command ${i} runs past the end`);
    const cmd = buf.readUInt32LE(off);
    const cmdsize = buf.readUInt32LE(off + 4);
    if (cmdsize < 8) throw fail('E_FACTS_MACHO_CORRUPT', `${where}: load command ${i} has cmdsize ${cmdsize}`);
    if (off + cmdsize > end) throw fail('E_FACTS_MACHO_CORRUPT', `${where}: load command ${i} runs past the end`);
    if (cmd === LC_BUILD_VERSION) {
      if (cmdsize < 16) throw fail('E_FACTS_MACHO_CORRUPT', `${where}: LC_BUILD_VERSION cmdsize ${cmdsize}`);
      if (buildMin === null && buf.readUInt32LE(off + 8) === PLATFORM_MACOS) buildMin = buf.readUInt32LE(off + 12);
    } else if (cmd === LC_VERSION_MIN_MACOSX) {
      if (cmdsize < 12) throw fail('E_FACTS_MACHO_CORRUPT', `${where}: LC_VERSION_MIN_MACOSX cmdsize ${cmdsize}`);
      if (versionMin === null) versionMin = buf.readUInt32LE(off + 8);
    }
    off += cmdsize;
  }
  const min = buildMin !== null ? buildMin : versionMin;
  if (min === null) {
    throw fail('E_FACTS_NO_MINOS', `${where}: no LC_BUILD_VERSION for macOS and no LC_VERSION_MIN_MACOSX (${arch})`);
  }
  return { arch, minOs: versionText(min) };
}

/**
 * FR-011: a fat file (each slice it lists) or a thin 64-bit little-endian file.
 * @param {Buffer} buffer @returns {{archs: string[], minOs: string, slices: {arch: string, minOs: string}[]}}
 */
function machoFacts(buffer) {
  if (buffer.length < 4) throw fail('E_FACTS_MACHO_CORRUPT', 'shorter than a Mach-O magic');
  const fatMagic = buffer.readUInt32BE(0);
  let slices;
  if (fatMagic === FAT_MAGIC || fatMagic === FAT_MAGIC_64) {
    const wide = fatMagic === FAT_MAGIC_64;
    const entrySize = wide ? 32 : 20;
    if (buffer.length < 8) throw fail('E_FACTS_MACHO_CORRUPT', 'the fat header runs past the end');
    const n = buffer.readUInt32BE(4);
    if (n === 0) throw fail('E_FACTS_MACHO_CORRUPT', 'nfat_arch is 0');
    if (8 + n * entrySize > buffer.length) throw fail('E_FACTS_MACHO_CORRUPT', 'the fat_arch table runs past the end');
    slices = [];
    for (let i = 0; i < n; ++i) {
      const at = 8 + i * entrySize;
      const where = `fat slice ${i}`;
      const arch = archOf(buffer.readUInt32BE(at), buffer.readUInt32BE(at + 4), where);
      const offset = wide ? Number(buffer.readBigUInt64BE(at + 8)) : buffer.readUInt32BE(at + 8);
      const size = wide ? Number(buffer.readBigUInt64BE(at + 16)) : buffer.readUInt32BE(at + 12);
      if (offset + size > buffer.length) throw fail('E_FACTS_MACHO_CORRUPT', `${where} runs past the end`);
      const slice = thinFacts(buffer.subarray(offset, offset + size), where);
      if (slice.arch !== arch) {
        throw fail('E_FACTS_MACHO_CORRUPT', `${where}: the fat entry says ${arch}, the slice header ${slice.arch}`);
      }
      slices.push(slice);
    }
  } else {
    slices = [thinFacts(buffer, 'thin Mach-O')];
  }
  return {
    archs: [...new Set(slices.map((s) => s.arch))].sort(bytewise),
    minOs: highest(slices.map((s) => s.minOs)),
    slices,
  };
}

/** D2: {arm64, x86_64} / {arm64} / {x86_64} -> the darwin platform key; any other set E_FACTS_ARCH. */
function platformKeyForArchs(archs) {
  const key = [...new Set(archs)].sort(bytewise).join(' ');
  if (key === 'arm64 x86_64') return 'darwin-universal';
  if (key === 'arm64') return 'darwin-aarch64';
  if (key === 'x86_64') return 'darwin-x86_64';
  throw fail('E_FACTS_ARCH', `architecture set ${show(archs)} has no darwin platform key`);
}

// The first four bytes of a file, or null when it is shorter.
function readMagic(file) {
  const head = Buffer.alloc(4);
  const fd = fs.openSync(file, 'r');
  try {
    return fs.readSync(fd, head, 0, 4, 0) === 4 ? head : null;
  } finally {
    fs.closeSync(fd);
  }
}

const isMachoMagic = (head) =>
  [MH_MAGIC_64, MH_MAGIC, MH_CIGAM, MH_CIGAM_64].includes(head.readUInt32LE(0)) ||
  [FAT_MAGIC, FAT_MAGIC_64].includes(head.readUInt32BE(0));

/**
 * FR-012: every regular file under root whose magic is a Mach-O of either width or byte order, or a fat file; links
 * are skipped, never followed. D2 (one arch set) and D3 (the highest minos).
 * @param {string} root @returns {{platform_key: string, min_os: string, archs: string[], macho: object[]}}
 */
function darwinFactsFromTree(root) {
  const found = [];
  const walk = (abs, rel) => {
    let entries;
    try {
      entries = fs.readdirSync(abs, { withFileTypes: true });
    } catch (err) {
      throw fail('E_INPUT', `${show(rel || root)}: cannot read (${err.code || err.message})`, { file: abs });
    }
    for (const dirent of entries) {
      const absPath = path.join(abs, dirent.name);
      const relPath = rel === '' ? dirent.name : `${rel}/${dirent.name}`;
      let file;
      try {
        const st = fs.lstatSync(absPath);
        if (st.isSymbolicLink()) continue;
        if (st.isDirectory()) {
          walk(absPath, relPath);
          continue;
        }
        if (!st.isFile()) continue;
        const head = readMagic(absPath);
        if (head === null || !isMachoMagic(head)) continue;
        file = fs.readFileSync(absPath);
      } catch (err) {
        if (err instanceof ContractError) throw err;
        throw fail('E_INPUT', `${show(relPath)}: cannot read (${err.code || err.message})`, { file: relPath });
      }
      let m;
      try {
        m = machoFacts(file);
      } catch (err) {
        if (!(err instanceof ContractError)) throw err;
        throw fail(err.code, `${relPath}: ${err.message}`, { file: relPath });
      }
      found.push({ path: relPath, archs: m.archs, min_os: m.minOs });
    }
  };
  walk(root, '');
  if (found.length === 0) throw fail('E_FACTS_NO_MACHO', `no Mach-O file under ${show(root)}`, { file: root });
  found.sort((a, b) => bytewise(a.path, b.path));
  const [first] = found;
  for (const m of found) {
    if (m.archs.join(' ') !== first.archs.join(' ')) {
      throw fail(
        'E_FACTS_ARCH_MISMATCH',
        `${first.path} is [${first.archs.join(' ')}], ${m.path} is [${m.archs.join(' ')}]`,
        { file: m.path },
      );
    }
  }
  return {
    platform_key: platformKeyForArchs(first.archs),
    min_os: highest(found.map((m) => m.min_os)),
    archs: first.archs,
    macho: found,
  };
}

// --- Linux

/** FR-014: the highest GLIBC_<n>.<n>[.<n>] named in objdump -T output, as named. */
function glibcFloor(objdumpText) {
  const versions = [...String(objdumpText).matchAll(/\bGLIBC_([0-9]+(?:\.[0-9]+){1,2})\b/g)].map((m) => m[1]);
  if (versions.length === 0) throw fail('E_FACTS_NO_GLIBC', 'the objdump -T output names no GLIBC_<n>.<n> version');
  return highest(versions);
}

/**
 * PD-26: the tarball's listing (FR-031 rules: no absolute path, no '..', no escaping link), the single root-level
 * <Name>.vst3 directory and the C4 layout, before anything extracts the archive. No fs, no exec.
 * @param {Buffer} buf @returns {{lines: string[], vst3Bundle: string, soPath: string | null}}
 */
function linuxPrecheck(buf) {
  const lines = listing.listingFromTarGz(buf);
  const entries = lines.map(listing.parseListingLine);
  const bundles = entries.filter((e) => e.type === 'd' && !e.path.includes('/') && e.path.endsWith('.vst3'));
  if (bundles.length !== 1) {
    throw fail('E_LAYOUT', `layout: expected one root <Name>.vst3 directory, found ${show(bundles.map((e) => e.path))}`);
  }
  const vst3Bundle = bundles[0].path;
  listing.checkLinuxLayout(lines, { vst3Bundle });
  const stem = vst3Bundle.slice(0, -'.vst3'.length);
  const so = `${vst3Bundle}/Contents/x86_64-linux/${stem}.so`;
  const soPath = entries.some((e) => e.type === 'f' && e.path === so) ? so : null;
  return { lines, vst3Bundle, soPath };
}

function readBuffer(file, label) {
  try {
    return fs.readFileSync(file);
  } catch (err) {
    throw fail('E_INPUT', `${label} ${file}: cannot read (${err.code || err.message})`, { file });
  }
}

const sha256 = (buf) => crypto.createHash('sha256').update(buf).digest('hex');

/**
 * FR-015, in this order: the tarball's listing and layout (linuxPrecheck), then the extracted tree's listing line for
 * line, then the glibc floor. listDirectory is injectable so a win32 test can stand in for listingFromDirectory (PD-4).
 */
function linuxFacts({ tarball, extracted, objdumpText }, { listDirectory = listing.listingFromDirectory } = {}) {
  const buf = readBuffer(tarball, '--tarball');
  const { lines } = linuxPrecheck(buf);
  let ext;
  try {
    ext = listDirectory(extracted);
  } catch (err) {
    if (err instanceof ContractError || typeof err.syscall !== 'string') throw err;
    throw fail('E_INPUT', `--extracted ${extracted}: cannot read (${err.code || err.message})`, { file: extracted });
  }
  const n = Math.max(lines.length, ext.length);
  for (let i = 0; i < n; ++i) {
    if (lines[i] !== ext[i]) {
      const side = (x) => (x === undefined ? '(no line)' : show(x));
      throw fail(
        'E_FACTS_LISTING_MISMATCH',
        `line ${i + 1}: the tarball lists ${side(lines[i])}, the extracted tree ${side(ext[i])}`,
        { file: extracted },
      );
    }
  }
  return {
    asset: path.basename(tarball),
    size: buf.length,
    sha256: sha256(buf),
    min_os: glibcFloor(objdumpText),
    payload: { files: lines.length, sha256: listing.digest(lines) },
  };
}

// --- file IO

/** Size and SHA-256 of a file, hashed in 1 MiB chunks (a pkg can be large). */
function hashFile(file, label) {
  const hash = crypto.createHash('sha256');
  const chunk = Buffer.alloc(1 << 20);
  let size = 0;
  let fd = null;
  try {
    fd = fs.openSync(file, 'r');
    for (let n = fs.readSync(fd, chunk, 0, chunk.length, null); n > 0; n = fs.readSync(fd, chunk, 0, chunk.length, null)) {
      hash.update(chunk.subarray(0, n));
      size += n;
    }
  } catch (err) {
    throw fail('E_INPUT', `${label} ${file}: cannot read (${err.code || err.message})`, { file });
  } finally {
    if (fd !== null) fs.closeSync(fd);
  }
  return { size, sha256: hash.digest('hex') };
}

/** As tools/gen-feed.js writeAtomic: a temp file in the same directory, then a rename; E_INPUT on any failure. */
function writeAtomic(outPath, text) {
  const tmp = path.join(
    path.dirname(outPath),
    `.${path.basename(outPath)}.${process.pid}.${crypto.randomBytes(4).toString('hex')}.tmp`,
  );
  try {
    fs.writeFileSync(tmp, text, { flag: 'wx' });
    fs.renameSync(tmp, outPath);
  } catch (err) {
    try {
      fs.rmSync(tmp, { force: true });
    } catch {
      // The temp file was never created (or cannot be touched); the E_INPUT below is the report.
    }
    throw fail('E_INPUT', `--out ${outPath}: cannot write (${err.code || err.message})`, { file: outPath });
  }
}

// --- CLI

const MODES = {
  darwin: ['--pkg', '--expanded', '--out'],
  linux: ['--tarball', '--extracted', '--objdump', '--out'],
  'linux-precheck': ['--tarball'],
};

const usageError = (message) => fail('E_USAGE', message);

/** @returns {{mode: string, opts: Map<string, string>}} every flag of the mode required, none repeated */
function parseArgs(argv) {
  const [mode, ...rest] = argv;
  if (mode === undefined) throw usageError('a mode is required');
  if (!Object.prototype.hasOwnProperty.call(MODES, mode)) throw usageError(`unknown mode ${show(mode)}`);
  const flags = MODES[mode];
  const opts = new Map();
  for (let i = 0; i < rest.length; i += 2) {
    const a = rest[i];
    if (!flags.includes(a)) throw usageError(`${show(a)} is not a flag of the ${mode} mode`);
    if (opts.has(a)) throw usageError(`${a} given twice`);
    const value = rest[i + 1];
    if (value === undefined || value === '' || value.startsWith('--')) throw usageError(`${a} needs a value`);
    opts.set(a, value);
  }
  for (const flag of flags) {
    if (!opts.has(flag)) throw usageError(`the ${mode} mode needs ${flag}`);
  }
  return { mode, opts };
}

/**
 * @param {string[]} argv
 * @param {{stdout: {write(s: string): void}, stderr: {write(s: string): void}}} io
 * @returns {0|1|2}
 */
function main(argv, io) {
  try {
    const { mode, opts } = parseArgs(argv);
    if (mode === 'darwin') {
      const pkg = opts.get('--pkg');
      const tree = darwinFactsFromTree(opts.get('--expanded'));
      const { size, sha256: digest } = hashFile(pkg, '--pkg');
      const doc = {
        asset: path.basename(pkg),
        size,
        sha256: digest,
        platform_key: tree.platform_key,
        min_os: tree.min_os,
        archs: tree.archs,
        macho: tree.macho,
      };
      writeAtomic(opts.get('--out'), json(doc));
      for (const m of tree.macho) io.stdout.write(`${m.archs.join(' ')}\t${m.path}\n`);
      return 0;
    }
    if (mode === 'linux') {
      const objdump = opts.get('--objdump');
      const objdumpText = readBuffer(objdump, '--objdump').toString('utf8');
      const doc = linuxFacts({ tarball: opts.get('--tarball'), extracted: opts.get('--extracted'), objdumpText });
      writeAtomic(opts.get('--out'), json(doc));
      return 0;
    }
    const { vst3Bundle, soPath } = linuxPrecheck(readBuffer(opts.get('--tarball'), '--tarball'));
    if (soPath === null) {
      throw fail('E_LAYOUT', `layout: ${vst3Bundle} holds no Contents/x86_64-linux/<Name>.so regular file`);
    }
    return 0;
  } catch (err) {
    if (!(err instanceof ContractError)) throw err;
    io.stderr.write(`release-facts: ${err.code}: ${err.message}\n`);
    if (CODES[err.code] === 'usage') {
      io.stderr.write(USAGE);
      return 2;
    }
    return 1;
  }
}

if (require.main === module) process.exitCode = main(process.argv.slice(2), process);

module.exports = {
  main,
  machoFacts,
  darwinFactsFromTree,
  platformKeyForArchs,
  glibcFloor,
  linuxPrecheck,
  linuxFacts,
};
