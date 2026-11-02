// The fake world of the backfill tests: gh, git, pkgutil, lipo, tar, objdump and the signer over
// fixtures/phase2/backfill/state.json (spec FR-051, SC-010 to SC-013; plan §4.4, §4.12.3, PD-4).
//
// WHY: tools/backfill-sign.js changes published GitHub releases. Its tests must prove every path (plan, download, facts,
// apply, audit) without a network, a GitHub token or an Apple toolchain, so every effect it has goes through the `deps`
// object this helper builds. The fake is strict: an argv shape the tool is not meant to send throws, so a changed call
// is a red test, not a silent pass. It writes only under the temp dir it is given; it spawns nothing itself (the POSIX
// tar leg goes through helpers/external-tools.js, the one test-side spawner).
//
// makeWorld({tmp, mutate, hooks}) -> {deps, calls, state, logs, served, setSigner, setKeyFile}
//   world    (an optional fourth option) 'backfill' (default): the Phase 2 world of state.json; 'snapshot' (Phase 3, plan §4.8): the 2026-10-29
//            snapshot releases.json as repo rolandzwaga/krate-audio, each <plugin>/v<ver> row with its three
//            installers (<Name> from the snapshot version.json whose lower-cased name is the tag prefix); a tag with
//            no "<plugin>/" prefix gets no asset, as no version.json names it
//   state    a deep copy of state.json; release uploads append to it, so a second run sees them
//   calls    every call in order: {cmd: 'gh' | 'git' | 'pkgutil' | 'lipo' | 'tar' | 'objdump' | 'sign', argv}
//   mutate   (state, ctx) => void, before "auto" sizes and digests are filled; ctx.bytes(tag, asset) and
//            ctx.setServed(tag, asset, buffer) read and replace the bytes the fake serves
//   hooks    releaseList(rows) -> rows; view(tag, doc) -> doc; gitShow(tag, relPath, text) -> text | null (null: the
//            path does not exist at the tag); machos(assetName, defaults) -> [{path, buf, lipo}] (the files the fake
//            pkgutil writes, `lipo` the words its lipo -archs prints); lipo(file, words) -> words;
//            upload(tag, basenames) -> {status} | undefined (undefined: the upload succeeds)
//   gh       release list / view (field sets of VIEW_JSON) / upload, and release download in two shapes:
//            `--dir <dir>` (made if missing) and `--output <file>` (its directory must exist, as gh -O needs)
//
// Phase 3 helpers, for use inside mutate (spec specs/krate-manager-phase3/spec.md FR-050; plan §4.8):
//   addPhase2Files(state, ctx, tag, {tmp, signer, pluginsDir})  signs the release's three served installers with
//            `signer` (throwawaySigner()), builds release.json + SHA256SUMS in-process with
//            gen-release-manifest.buildManifest and adds the five assets (SHA256SUMS, three .minisig, release.json)
//   addManagerRelease(state, ctx, {tag, isDraft, isPrerelease, publishedAt, latestJson})  one release row whose only
//            asset is latest.json (`latestJson` its text), or no asset when `latestJson` is null
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');
const crypto = require('node:crypto');

const macho = require('./macho.js');
const externalTools = require('./external-tools.js');
const listing = require('../../listing.js');
const minisign = require('../../../minisign.js');
const releaseFacts = require('../../../release-facts.js');
const genReleaseManifest = require('../../../gen-release-manifest.js');

const BACKFILL = path.join(__dirname, '..', '..', 'fixtures', 'phase2', 'backfill');
const SNAPSHOT = path.join(__dirname, '..', '..', 'fixtures', 'gen-feed', 'snapshot-2026-10-29');
const SNAPSHOT_REPO = 'rolandzwaga/krate-audio';
const PLUGINS_DIR = path.join(BACKFILL, 'plugins');
const PLUGINS_EXTRA_DIR = path.join(BACKFILL, 'plugins-extra');
const STATE_FILE = path.join(BACKFILL, 'state.json');
const OBJDUMP_SAMPLE = path.join(__dirname, '..', '..', 'fixtures', 'phase2', 'objdump', 'glibc-mixed.txt');
const IS_WIN = process.platform === 'win32';

const SUFFIX = Object.freeze({ exe: '-Windows-x64.exe', pkg: '-macOS.pkg', tar: '-Linux-x64.tar.gz' });
const LIST_JSON = 'tagName,isPrerelease,isDraft,publishedAt';
const VIEW_JSON = new Set(['tagName,publishedAt,isImmutable,assets', 'assets', 'tagName,assets']);

const sha256 = (buf) => crypto.createHash('sha256').update(buf).digest('hex');
const key = (tag, asset) => `${tag}\n${asset}`;
const ok = (stdout = '') => ({ status: 0, stdout, stderr: '' });
const failed = (stderr) => ({ status: 1, stdout: '', stderr });

// --- C4 tarballs as old-GNU tar headers (the make-bad.js technique, fixtures/listings/make-bad.js)

const BLOCK = 512;
const MTIME = Date.UTC(2026, 9, 29) / 1000;
const oct = (n, width) => n.toString(8).padStart(width - 1, '0') + '\0';

function header({ name, typeflag, mode, size = 0, linkname = '' }) {
  const h = Buffer.alloc(BLOCK);
  const put = (offset, length, text) => {
    const b = Buffer.from(text, 'utf8');
    if (b.length > length) throw new Error(`fake-gh: tar field overflows: ${text}`);
    b.copy(h, offset);
  };
  put(0, 100, name);
  put(100, 8, oct(mode, 8));
  put(108, 8, oct(0, 8));
  put(116, 8, oct(0, 8));
  put(124, 12, oct(size, 12));
  put(136, 12, oct(MTIME, 12));
  h[156] = typeflag.charCodeAt(0);
  put(157, 100, linkname);
  put(257, 8, 'ustar  \0'); // old-GNU magic and version, as GNU tar's default format writes them
  put(265, 32, 'root');
  put(297, 32, 'root');
  let sum = 0;
  for (let i = 0; i < BLOCK; i++) sum += i >= 148 && i < 156 ? 0x20 : h[i];
  put(148, 8, sum.toString(8).padStart(6, '0') + '\0 ');
  return h;
}

const pad = (data) => Buffer.concat([data, Buffer.alloc((BLOCK - (data.length % BLOCK)) % BLOCK)]);
const tarFile = (name, text, mode = 0o644) => {
  const data = Buffer.from(text, 'utf8');
  return Buffer.concat([header({ name, typeflag: '0', mode, size: data.length }), pad(data)]);
};
const tarDir = (name) => header({ name, typeflag: '5', mode: 0o755 });

/**
 * A C4 tarball for <name> <version>: <Name>.vst3/Contents/{Resources/moduleinfo.json, x86_64-linux/<Name>.so},
 * presets/, README.txt, CHANGELOG.txt; modes 0644 / 0755 only. `omit` drops root items (e.g. ['README.txt']).
 */
function c4Tarball(name, version, { omit = [] } = {}) {
  const b = `${name}.vst3`;
  const members = [
    tarDir(`${b}/`),
    tarDir(`${b}/Contents/`),
    tarDir(`${b}/Contents/Resources/`),
    tarFile(`${b}/Contents/Resources/moduleinfo.json`, `{"Name": "${name}", "Version": "${version}"}\n`),
    tarDir(`${b}/Contents/x86_64-linux/`),
    tarFile(`${b}/Contents/x86_64-linux/${name}.so`, `fixture: not a real shared object (${name})\n`, 0o755),
    tarDir('presets/'),
    tarFile('presets/Init.vstpreset', `${name} preset\n`),
    omit.includes('README.txt') ? null : tarFile('README.txt', `${name} ${version}\n`),
    omit.includes('CHANGELOG.txt') ? null : tarFile('CHANGELOG.txt', `## [${version}]\n`),
  ].filter((m) => m !== null);
  return zlib.gzipSync(Buffer.concat([...members, Buffer.alloc(2 * BLOCK)]));
}

// --- Mach-Os for the fake pkgutil

const x86 = () =>
  macho.thin({
    cputype: macho.CPU_TYPE_X86_64,
    cpusubtype: macho.CPU_SUBTYPE_X86_64_ALL,
    commands: [macho.versionMin(0x000a0d00)],
  });
const arm64 = () => macho.thin({ cputype: macho.CPU_TYPE_ARM64, commands: [macho.buildVersion(0x000b0000)] });
/** fat x86_64 (macOS 10.13) + arm64 (macOS 11.0); lipo -archs prints the slices in file order. */
const universal = () => ({ buf: macho.fat([x86(), arm64()]), lipo: 'x86_64 arm64' });
const arm64Only = () => ({ buf: arm64(), lipo: 'arm64' });

/** The default expansion of <Name>-<v>-macOS.pkg: the VST3 and the AU binary, both universal. */
function defaultMachos(assetName) {
  const name = assetName.slice(0, assetName.indexOf('-'));
  return [
    { path: `${name}_vst3.pkg/Payload/${name}.vst3/Contents/MacOS/${name}`, ...universal() },
    { path: `${name}_au.pkg/Payload/${name}.component/Contents/MacOS/${name}`, ...universal() },
  ];
}

// --- asset bytes

function versionOfTag(tag) {
  return tag.slice(tag.indexOf('/v') + 2);
}

/** Stub bytes for an .exe / .pkg, a C4 tarball for a .tar.gz; the plugin name is the asset name's prefix. */
function assetBytes(tag, assetName) {
  const version = versionOfTag(tag);
  for (const [kind, suffix] of Object.entries(SUFFIX)) {
    const stem = `-${version}${suffix}`;
    if (!assetName.endsWith(stem)) continue;
    const name = assetName.slice(0, -stem.length);
    if (kind === 'tar') return c4Tarball(name, version);
    return Buffer.from(`fixture ${kind} stub for ${name} ${version}\n`.repeat(kind === 'exe' ? 20 : 30), 'utf8');
  }
  return Buffer.from(`fixture asset ${assetName}\n`, 'utf8');
}

/** A throwaway minisign.js signer for deps.sign (keys in memory only, FR-060). */
function throwawaySigner() {
  const keyPair = minisign.generateKeyPair();
  return {
    keyPair,
    publicKeyText: minisign.formatPublicKey(keyPair),
    sign(file, trustedComment) {
      const text = minisign.signFile(file, keyPair, { trustedComment });
      fs.writeFileSync(`${file}.minisig`, text);
      return text;
    },
  };
}

// --- Phase 3 (plan §4.8)

const TAG_RE = /^([^/]+)\/v(.+)$/;
const readJsonFile = (file) => JSON.parse(fs.readFileSync(file, 'utf8'));

/** The plugin directory under `pluginsDir` whose version.json name lower-cases to `prefix`, else throws. */
function pluginDirFor(pluginsDir, prefix) {
  for (const d of fs.readdirSync(pluginsDir)) {
    const vj = path.join(pluginsDir, d, 'version.json');
    if (fs.existsSync(vj) && String(readJsonFile(vj).name).toLowerCase() === prefix) return path.join(pluginsDir, d);
  }
  throw new Error(`fake-gh: no version.json under ${pluginsDir} names the tag prefix ${prefix}`);
}

/** The snapshot world: releases.json rows, each <plugin>/v<ver> row with its three installers ("auto" sized). */
function snapshotState() {
  const pluginsDir = path.join(SNAPSHOT, 'plugins');
  const names = new Map();
  for (const d of fs.readdirSync(pluginsDir)) {
    const name = readJsonFile(path.join(pluginsDir, d, 'version.json')).name;
    names.set(name.toLowerCase(), name);
  }
  const releases = readJsonFile(path.join(SNAPSHOT, 'releases.json')).map((row) => {
    const m = TAG_RE.exec(row.tagName);
    const name = m === null ? undefined : names.get(m[1]);
    if (m !== null && name === undefined) throw new Error(`fake-gh: snapshot tag ${row.tagName} names no plugin`);
    const assets =
      name === undefined
        ? []
        : Object.values(SUFFIX).map((suffix) => ({ name: `${name}-${m[2]}${suffix}`, size: 'auto', digest: 'auto' }));
    return {
      tagName: row.tagName,
      isPrerelease: row.isPrerelease,
      isDraft: row.isDraft,
      isImmutable: false,
      publishedAt: row.publishedAt,
      assets,
    };
  });
  return { repo: SNAPSHOT_REPO, releases };
}

/**
 * The Phase 2 file set of release `tag`, for use inside mutate: the three served installers are written to
 * <tmp>/<tag with "/" as "-">/ and signed with `signer`, facts are recorded in-process (as
 * tests/gen-release-manifest.test.js does), and buildManifest makes release.json and SHA256SUMS from them. The five
 * new assets are appended ("auto" sized) and served. `pluginsDir` holds the plugin's version.json (its version is set
 * to the tag's) and installers/windows/setup.iss. Spawns nothing (spec FR-051).
 */
function addPhase2Files(state, ctx, tag, { tmp, signer, pluginsDir }) {
  const r = state.releases.find((x) => x.tagName === tag);
  const m = TAG_RE.exec(tag);
  if (!r || m === null) throw new Error(`fake-gh: addPhase2Files: no release ${tag}`);
  const [, prefix, version] = m;
  const dir = pluginDirFor(pluginsDir, prefix);
  const versionDoc = readJsonFile(path.join(dir, 'version.json'));
  versionDoc.version = version;
  const versionJsonText = JSON.stringify(versionDoc, null, 2) + '\n';
  const setupIssText = fs.readFileSync(path.join(dir, 'installers', 'windows', 'setup.iss'), 'utf8');

  const assetsDir = path.join(tmp, tag.split('/').join('-'));
  fs.mkdirSync(assetsDir, { recursive: true });
  const installers = {};
  for (const [kind, suffix] of Object.entries(SUFFIX)) {
    const asset = `${versionDoc.name}-${version}${suffix}`;
    const bytes = ctx.bytes(tag, asset);
    if (bytes === undefined) throw new Error(`fake-gh: addPhase2Files: ${tag} serves no ${asset}`);
    fs.writeFileSync(path.join(assetsDir, asset), bytes);
    installers[kind] = { asset, bytes };
  }

  const pkg = installers.pkg;
  const darwinFacts = {
    asset: pkg.asset,
    size: pkg.bytes.length,
    sha256: sha256(pkg.bytes),
    platform_key: 'darwin-universal',
    min_os: '11.0',
    archs: ['arm64', 'x86_64'],
    macho: [],
  };
  const tarBytes = installers.tar.bytes;
  const linuxFacts = releaseFacts.linuxFacts(
    {
      tarball: path.join(assetsDir, installers.tar.asset),
      extracted: assetsDir,
      objdumpText: fs.readFileSync(OBJDUMP_SAMPLE, 'utf8'),
    },
    { listDirectory: () => listing.listingFromTarGz(tarBytes) },
  );
  const comments = genReleaseManifest.trustedComments({ versionJsonText, assetsDir, darwinFacts });
  for (const c of comments) signer.sign(path.join(assetsDir, c.asset), c.comment);

  const { releaseJsonText, sumsText } = genReleaseManifest.buildManifest({
    versionJsonText,
    setupIssText,
    assetsDir,
    darwinFacts,
    linuxFacts,
    repo: state.repo,
    pubDate: r.publishedAt,
    publicKeyText: signer.publicKeyText,
  });
  const added = [
    ['SHA256SUMS', Buffer.from(sumsText, 'utf8')],
    ...comments.map((c) => [`${c.asset}.minisig`, fs.readFileSync(path.join(assetsDir, `${c.asset}.minisig`))]),
    ['release.json', Buffer.from(releaseJsonText, 'utf8')],
  ];
  for (const [name, bytes] of added) {
    r.assets.push({ name, size: 'auto', digest: 'auto' });
    ctx.setServed(tag, name, bytes);
  }
}

/** One manager release row, for use inside mutate: a latest.json asset with `latestJson`'s bytes, none when null. */
function addManagerRelease(state, ctx, { tag, isDraft = false, isPrerelease = false, publishedAt, latestJson }) {
  if (latestJson !== null && typeof latestJson !== 'string') {
    throw new Error('fake-gh: addManagerRelease: latestJson is a string or null');
  }
  const assets = latestJson === null ? [] : [{ name: 'latest.json', size: 'auto', digest: 'auto' }];
  state.releases.push({ tagName: tag, isPrerelease, isDraft, isImmutable: false, publishedAt, assets });
  if (latestJson !== null) ctx.setServed(tag, 'latest.json', Buffer.from(latestJson, 'utf8'));
}

// --- the world

function makeWorld({ tmp, mutate = () => {}, hooks = {}, world = 'backfill' }) {
  let state;
  if (world === 'backfill') state = JSON.parse(fs.readFileSync(STATE_FILE, 'utf8'));
  else if (world === 'snapshot') state = snapshotState();
  else throw new Error(`fake-gh: unknown world ${JSON.stringify(world)}`);
  const served = new Map();
  for (const r of state.releases) for (const a of r.assets) served.set(key(r.tagName, a.name), assetBytes(r.tagName, a.name));
  const ctx = {
    bytes: (tag, asset) => served.get(key(tag, asset)),
    setServed: (tag, asset, buf) => served.set(key(tag, asset), buf),
  };
  mutate(state, ctx);
  for (const r of state.releases) {
    for (const a of r.assets) {
      const bytes = served.get(key(r.tagName, a.name)) || Buffer.from(`fixture asset ${a.name}\n`, 'utf8');
      served.set(key(r.tagName, a.name), bytes);
      if (a.size === 'auto') a.size = bytes.length;
      if (a.digest === 'auto') a.digest = `sha256:${sha256(bytes)}`;
    }
  }

  const calls = [];
  const logs = [];
  const lipoWords = new Map(); // absolute Mach-O path -> the words the fake lipo prints
  const tarSource = new Map(); // extraction dir -> the tarball tar was asked to extract
  let keyFile = null;

  const release = (tag) => state.releases.find((r) => r.tagName === tag);
  const strings = (argv) => {
    if (!Array.isArray(argv) || !argv.every((x) => typeof x === 'string')) {
      throw new Error(`fake-gh: argv is not an array of strings: ${JSON.stringify(argv)}`);
    }
  };
  const unexpected = (cmd, argv) => new Error(`fake-gh: unexpected ${cmd} call ${JSON.stringify(argv)}`);
  const assertKeyGone = (what) => {
    if (keyFile !== null && fs.existsSync(keyFile)) throw new Error(`fake-gh: ${what} while the --key file exists`);
  };

  function gh(argv) {
    strings(argv);
    calls.push({ cmd: 'gh', argv: [...argv] });
    const [group, sub, ...rest] = argv;
    if (group !== 'release') throw unexpected('gh', argv);
    if (sub === 'list') {
      const want = ['--repo', state.repo, '--limit', '1000', '--json', LIST_JSON];
      if (JSON.stringify(rest) !== JSON.stringify(want)) throw unexpected('gh', argv);
      let rows = state.releases.map((r) => ({
        tagName: r.tagName,
        isPrerelease: r.isPrerelease,
        isDraft: r.isDraft,
        publishedAt: r.publishedAt,
      }));
      if (hooks.releaseList) rows = hooks.releaseList(rows);
      return ok(JSON.stringify(rows));
    }
    if (sub === 'view') {
      const [tag, ...flags] = rest;
      if (flags.length !== 4 || flags[0] !== '--repo' || flags[1] !== state.repo || flags[2] !== '--json') {
        throw unexpected('gh', argv);
      }
      if (!VIEW_JSON.has(flags[3])) throw unexpected('gh', argv);
      assertKeyGone(`gh release view ${tag}`);
      const r = release(tag);
      if (!r) return failed(`release not found: ${tag}`);
      const full = {
        tagName: r.tagName,
        publishedAt: r.publishedAt,
        isImmutable: r.isImmutable,
        assets: r.assets.map((a) => ({ name: a.name, size: a.size, digest: a.digest })),
      };
      const doc = {};
      for (const field of flags[3].split(',')) doc[field] = full[field];
      return ok(JSON.stringify(hooks.view ? hooks.view(tag, doc) : doc));
    }
    if (sub === 'download') {
      const [tag, ...flags] = rest;
      const shape =
        flags.length === 6 &&
        flags[0] === '--repo' &&
        flags[2] === '--pattern' &&
        (flags[4] === '--dir' || flags[4] === '--output');
      if (!shape || flags[1] !== state.repo) throw unexpected('gh', argv);
      const r = release(tag);
      const asset = r && r.assets.find((a) => a.name === flags[3]);
      if (!asset) return failed(`no asset ${flags[3]} on ${tag}`);
      if (flags[4] === '--output') {
        const parent = path.dirname(flags[5]);
        if (!fs.existsSync(parent)) return failed(`open ${flags[5]}: the directory ${parent} does not exist`);
        fs.writeFileSync(flags[5], served.get(key(tag, asset.name)));
        return ok();
      }
      fs.mkdirSync(flags[5], { recursive: true });
      fs.writeFileSync(path.join(flags[5], asset.name), served.get(key(tag, asset.name)));
      return ok();
    }
    if (sub === 'upload') {
      const [tag, repoFlag, repo, ...files] = rest;
      if (repoFlag !== '--repo' || repo !== state.repo || files.length === 0) throw unexpected('gh', argv);
      assertKeyGone(`gh release upload ${tag}`);
      const r = release(tag);
      if (!r) return failed(`release not found: ${tag}`);
      const names = files.map((f) => path.basename(f));
      const verdict = hooks.upload ? hooks.upload(tag, names) : undefined;
      if (verdict && verdict.status !== 0) return { status: verdict.status, stdout: '', stderr: 'upload failed' };
      for (const f of files) {
        const bytes = fs.readFileSync(f);
        r.assets.push({ name: path.basename(f), size: bytes.length, digest: `sha256:${sha256(bytes)}` });
      }
      return ok();
    }
    throw unexpected('gh', argv);
  }

  function git(argv) {
    strings(argv);
    calls.push({ cmd: 'git', argv: [...argv] });
    const m = argv.length === 2 && argv[0] === 'show' ? /^([^:]+):plugins\/([^/]+)\/(.+)$/.exec(argv[1]) : null;
    if (!m) throw unexpected('git', argv);
    const [, tag, dir, rel] = m;
    let text = null;
    for (const root of [PLUGINS_DIR, PLUGINS_EXTRA_DIR]) {
      const f = path.join(root, dir, ...rel.split('/'));
      if (fs.existsSync(f)) text = fs.readFileSync(f, 'utf8');
    }
    if (hooks.gitShow) text = hooks.gitShow(tag, `plugins/${dir}/${rel}`, text);
    if (text === null) return { status: 128, stdout: '' };
    return { status: 0, stdout: text };
  }

  function exec(command, argv) {
    strings(argv);
    calls.push({ cmd: command, argv: [...argv] });
    if (command === 'pkgutil') {
      if (argv.length !== 3 || argv[0] !== '--expand-full') throw unexpected(command, argv);
      const [, pkg, dest] = argv;
      if (!fs.existsSync(pkg)) return failed(`no such pkg ${pkg}`);
      if (fs.existsSync(dest)) return failed(`${dest} already exists`);
      const assetName = path.basename(pkg);
      const files = hooks.machos ? hooks.machos(assetName, defaultMachos(assetName)) : defaultMachos(assetName);
      fs.mkdirSync(dest, { recursive: true });
      fs.writeFileSync(path.join(dest, 'Distribution'), '<installer-gui-script/>\n');
      for (const f of files) {
        const abs = path.join(dest, ...f.path.split('/'));
        fs.mkdirSync(path.dirname(abs), { recursive: true });
        fs.writeFileSync(abs, f.buf);
        lipoWords.set(path.resolve(abs), f.lipo);
      }
      return ok();
    }
    if (command === 'lipo') {
      if (argv.length !== 2 || argv[0] !== '-archs') throw unexpected(command, argv);
      const words = lipoWords.get(path.resolve(argv[1]));
      if (words === undefined) return failed(`lipo: not a recorded Mach-O: ${argv[1]}`);
      return ok(`${hooks.lipo ? hooks.lipo(argv[1], words) : words}\n`);
    }
    if (command === 'tar') {
      if (argv.length !== 4 || argv[0] !== '-xzf' || argv[2] !== '-C') throw unexpected(command, argv);
      const [, src, , dest] = argv;
      tarSource.set(path.resolve(dest), src);
      if (IS_WIN) fs.mkdirSync(dest, { recursive: true });
      else externalTools.extractTarGz(src, dest);
      return ok();
    }
    if (command === 'objdump') {
      if (argv.length !== 2 || argv[0] !== '-T') throw unexpected(command, argv);
      return ok(fs.readFileSync(OBJDUMP_SAMPLE, 'utf8'));
    }
    throw unexpected(command, argv);
  }

  const deps = {
    gh,
    git,
    exec,
    sign() {
      throw new Error('fake-gh: deps.sign is set by the test');
    },
    log: (line) => logs.push(line),
  };
  // PD-4: win32 has no POSIX modes, so the fake tar only made the directory and the lister returns the tarball's
  // own listing; the POSIX legs extract for real and leave listDirectory unset (the tool's default lister runs).
  if (IS_WIN) {
    deps.listDirectory = (root) => {
      const src = tarSource.get(path.resolve(root));
      if (src === undefined) throw new Error(`fake-gh: listDirectory of a directory tar never made: ${root}`);
      return listing.listingFromTarGz(fs.readFileSync(src));
    };
  }

  // Wraps every deps call so `calls` sees sign too.
  const recordSign = (fn) => (file, comment, keyFileArg) => {
    calls.push({ cmd: 'sign', argv: [file, comment, keyFileArg] });
    return fn(file, comment, keyFileArg);
  };

  return {
    deps,
    calls,
    state,
    logs,
    served: ctx,
    tmp,
    /** Sets deps.sign to fn, recorded in calls. */
    setSigner: (fn) => {
      deps.sign = recordSign(fn);
    },
    /** From now on every gh release upload / view asserts that this file no longer exists (PD-20). */
    setKeyFile: (file) => {
      keyFile = file;
    },
  };
}

module.exports = {
  BACKFILL,
  PLUGINS_DIR,
  PLUGINS_EXTRA_DIR,
  STATE_FILE,
  SUFFIX,
  makeWorld,
  addPhase2Files,
  addManagerRelease,
  c4Tarball,
  defaultMachos,
  universal,
  arm64Only,
  throwawaySigner,
};
