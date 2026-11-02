// Krate feed contract C4: the canonical listing of a Linux archive, from in-memory entries, a .tar.gz or an
// extracted directory tree, plus its digest and the FR-033 root layout check (spec FR-030 to FR-033; plan §4.3).
//
// WHY: the manager installs a Linux tarball by extracting it, so the feed carries the archive's listing and its
// digest (payload.list / payload.sha256). Phase 2 computes them from the tarball itself, Phase 5 / 8 re-check them
// before and after extraction. One reader with the FR-031 rules in one place keeps the three sources in agreement.
//
// Pure: no writes, no network. The tar reader only reads headers and skips data. listingFromDirectory uses lstat and
// readlink, never follows a link and never resolves a real path (macOS /var -> /private/var must not leak).
// Contains no shipped plugin name; every rejection is a ContractError with an E_LISTING_* or E_LAYOUT code.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');
const crypto = require('node:crypto');

const { ContractError } = require('./contract.js');

const UTF8 = new TextDecoder('utf-8', { fatal: true, ignoreBOM: true });
const BLOCK = 512;
const ARROW = ' -> ';

const fail = (code, message) => new ContractError(code, message);
const show = (s) => JSON.stringify(s);

/** @typedef {{type: 'f'|'d'|'l'|'other', mode: number, path: string|Buffer, target?: string|Buffer, typeflag?: string}} Entry */

// --- FR-031 rules -------------------------------------------------------------------------------------------------

// string | Buffer -> string; invalid UTF-8 (a lone surrogate in a string included) is E_LISTING_PATH.
function decode(value, what) {
  if (typeof value === 'string') {
    if (Buffer.from(value, 'utf8').toString('utf8') !== value) throw fail('E_LISTING_PATH', `${what}: invalid UTF-8`);
    return value;
  }
  if (value instanceof Uint8Array) {
    try {
      return UTF8.decode(value);
    } catch {
      throw fail('E_LISTING_PATH', `${what}: invalid UTF-8: ${Buffer.from(value).toString('hex')}`);
    }
  }
  throw fail('E_LISTING_PATH', `${what}: missing`);
}

// The character rules shared by entry paths and link targets.
function checkCharacters(s, what) {
  if (s === '') throw fail('E_LISTING_PATH', `${what}: empty`);
  if (/[\0\r\n]/.test(s)) throw fail('E_LISTING_PATH', `${what}: holds a NUL, CR or LF: ${show(s)}`);
  if (s.startsWith('/')) throw fail('E_LISTING_ABSOLUTE', `${what}: absolute: ${show(s)}`);
  if (/^[A-Za-z]:/.test(s)) throw fail('E_LISTING_DRIVE', `${what}: drive prefix: ${show(s)}`);
  if (s.includes('\\')) throw fail('E_LISTING_BACKSLASH', `${what}: holds a backslash: ${show(s)}`);
}

// Rule 1: decode and check an entry path; returns the canonical path, or null for the archive root ('.' / './').
function canonicalPath(entry) {
  const raw = decode(entry.path, 'entry path');
  if (raw.includes(ARROW)) throw fail('E_LISTING_PATH', `entry path holds ${show(ARROW)}: ${show(raw)}`);
  checkCharacters(raw, 'entry path');
  let p = raw.startsWith('./') ? raw.slice(2) : raw;
  if (entry.type === 'd') {
    if (p === '' || p === '.') return null;
    if (p.endsWith('/')) p = p.slice(0, -1);
  }
  if (p.split('/').some((s) => s === '' || s === '.' || s === '..')) {
    throw fail('E_LISTING_DOTDOT', `entry path has an empty, . or .. segment: ${show(raw)}`);
  }
  if (!['f', 'd', 'l'].includes(entry.type)) {
    throw fail('E_LISTING_TYPE', `entry ${show(p)}: type not allowed (typeflag ${show(entry.typeflag)})`);
  }
  return p;
}

// Rule 2: a link's target; returns {target, resolved}.
function checkLink(linkPath, rawTarget) {
  const segs = linkPath.split('/');
  if (segs.length === 1) throw fail('E_LISTING_ROOT_SYMLINK', `symlink at the archive root: ${show(linkPath)}`);
  const target = decode(rawTarget, `link target of ${linkPath}`);
  checkCharacters(target, `link target of ${linkPath}`);
  const t = target.split('/');
  let i = 0;
  while (i < t.length && t[i] === '..') i++;
  if (i === t.length || t.slice(i).some((s) => s === '' || s === '.' || s === '..')) {
    throw fail('E_LISTING_SYMLINK_FORM', `link ${show(linkPath)}: target is not leading .. then plain segments: ${show(target)}`);
  }
  const resolved = path.posix.normalize(path.posix.join(path.posix.dirname(linkPath), target));
  const r = resolved.split('/');
  if (resolved === '.' || resolved === '..' || resolved.startsWith('../') || r[0] !== segs[0] || r.length < 2) {
    throw fail(
      'E_LISTING_SYMLINK_ESCAPE',
      `link ${show(linkPath)} -> ${show(target)} resolves to ${show(resolved)}, not strictly inside ${show(segs[0])}`,
    );
  }
  return { target, resolved };
}

const properPrefixes = (p) => {
  const s = p.split('/');
  const out = [];
  for (let k = 1; k < s.length; k++) out.push(s.slice(0, k).join('/'));
  return out;
};

function modeOf(type, mode) {
  if (type === 'd') return '0755';
  if (type === 'l') return '0777';
  return mode & 0o111 ? '0755' : '0644';
}

/** Entries -> canonical listing lines (no terminators), FR-030 / FR-031, checks in plan §4.3 order.
 *  @param {Entry[]} entries @returns {string[]} */
function listingFromEntries(entries) {
  const items = [];
  for (const e of entries) {
    const p = canonicalPath(e);
    if (p !== null) items.push({ type: e.type, mode: e.mode, path: p, rawTarget: e.target });
  }
  for (const it of items) {
    if (it.type === 'l') Object.assign(it, checkLink(it.path, it.rawTarget));
  }
  const links = new Set(items.filter((it) => it.type === 'l').map((it) => it.path));
  // (a) nothing may sit under a link: it would be written through it.
  for (const it of items) {
    const via = properPrefixes(it.path).find((q) => links.has(q));
    if (via !== undefined) {
      throw fail('E_LISTING_THROUGH_SYMLINK', `entry ${show(it.path)} lies under the symlink ${show(via)}`);
    }
  }
  // (b) duplicates and file / directory clashes, then implied ancestors.
  const byPath = new Map();
  for (const it of items) {
    if (byPath.has(it.path)) throw fail('E_LISTING_DUPLICATE', `path listed twice: ${show(it.path)}`);
    byPath.set(it.path, it);
  }
  for (const it of items) {
    for (const q of properPrefixes(it.path)) {
      const have = byPath.get(q);
      if (have === undefined) byPath.set(q, { type: 'd', mode: 0o755, path: q });
      else if (have.type !== 'd') {
        throw fail('E_LISTING_DUPLICATE', `${show(q)} is a file and also the directory of ${show(it.path)}`);
      }
    }
  }
  // (c) no link may resolve to, or through, a link (itself included).
  for (const it of items) {
    if (it.type !== 'l') continue;
    const hit = [...properPrefixes(it.resolved), it.resolved].find((q) => links.has(q));
    if (hit !== undefined) {
      throw fail('E_LISTING_SYMLINK_CHAIN', `link ${show(it.path)} -> ${show(it.target)} resolves to or through the link ${show(hit)}`);
    }
  }
  return [...byPath.values()]
    .map((it) => ({ key: Buffer.from(it.path, 'utf8'), it }))
    .sort((a, b) => Buffer.compare(a.key, b.key))
    .map(({ it }) => {
      const head = `${it.type} ${modeOf(it.type, it.mode)} ${it.path}`;
      return it.type === 'l' ? `${head}${ARROW}${it.target}` : head;
    });
}

// --- tar reader ---------------------------------------------------------------------------------------------------

const cstrBytes = (buf) => {
  const nul = buf.indexOf(0);
  return nul < 0 ? buf : buf.subarray(0, nul);
};

// An octal field: optional leading spaces, octal digits, then only NUL / space. Base-256 and anything else: CORRUPT.
function octal(field, what, offset) {
  if (field[0] & 0x80) throw fail('E_LISTING_CORRUPT', `header at ${offset}: ${what} is a base-256 number`);
  const s = field.toString('latin1');
  const m = /^ *([0-7]+)[\0 ]*$/.exec(s);
  if (m === null) throw fail('E_LISTING_CORRUPT', `header at ${offset}: ${what} is not octal: ${show(s)}`);
  return parseInt(m[1], 8);
}

function checkChecksum(h, offset) {
  const s = h.subarray(148, 156).toString('latin1');
  const m = /^ *([0-7]+)[\0 ]*$/.exec(s);
  let unsigned = 0;
  let signed = 0;
  for (let i = 0; i < BLOCK; i++) {
    const b = i >= 148 && i < 156 ? 0x20 : h[i];
    unsigned += b;
    signed += b > 127 ? b - 256 : b;
  }
  const stored = m === null ? NaN : parseInt(m[1], 8);
  if (stored !== unsigned && stored !== signed) {
    throw fail('E_LISTING_CHECKSUM', `header at ${offset}: checksum ${show(s)} != ${unsigned.toString(8)}`);
  }
}

// pax records "<len> <key>=<value>\n" (len counts the whole record) -> Map key -> Buffer value.
function paxRecords(data, offset) {
  const out = new Map();
  let pos = 0;
  while (pos < data.length) {
    const sp = data.indexOf(0x20, pos);
    const lenText = sp < 0 ? '' : data.subarray(pos, sp).toString('latin1');
    if (!/^[0-9]+$/.test(lenText)) throw fail('E_LISTING_CORRUPT', `pax header at ${offset}: bad record length`);
    const len = parseInt(lenText, 10);
    const end = pos + len;
    if (end > data.length || end <= sp + 1 || data[end - 1] !== 0x0a) {
      throw fail('E_LISTING_CORRUPT', `pax header at ${offset}: record length ${len} does not end at an LF`);
    }
    const kv = data.subarray(sp + 1, end - 1);
    const eq = kv.indexOf(0x3d);
    if (eq < 0) throw fail('E_LISTING_CORRUPT', `pax header at ${offset}: record without '='`);
    out.set(kv.subarray(0, eq).toString('utf8'), kv.subarray(eq + 1));
    pos = end;
  }
  return out;
}

const TYPES = { '0': 'f', '\0': 'f', '5': 'd', '2': 'l' };

/** Reads the headers of an uncompressed tar stream. Returns Entry[] (paths and targets as Buffers), or with
 *  {raw: true} {entries, records}: one record per header, {typeflag, name, magic, version, prefix, paxKeys?}.
 *  @param {Buffer} tar @param {{raw?: boolean}} [options] */
function readTarEntries(tar, { raw = false } = {}) {
  const entries = [];
  const records = [];
  let pending = {};
  let off = 0;
  let ended = false;
  while (off < tar.length) {
    const h = tar.subarray(off, off + BLOCK);
    if (h.every((b) => b === 0)) {
      if (h.length < BLOCK) break;
      ended = true;
      break;
    }
    if (h.length < BLOCK) throw fail('E_LISTING_TRUNCATED', `stream ends inside the header at ${off}`);
    checkChecksum(h, off);
    const mode = octal(h.subarray(100, 108), 'mode', off);
    const size = octal(h.subarray(124, 136), 'size', off);
    const typeflag = String.fromCharCode(h[156]);
    const dataStart = off + BLOCK;
    if (dataStart + size > tar.length) throw fail('E_LISTING_TRUNCATED', `data of the header at ${off} runs past the end`);
    const data = tar.subarray(dataStart, dataStart + size);
    const magic = h.subarray(257, 263);
    const version = h.subarray(263, 265);
    const prefix = h.subarray(345, 500);
    const record = { typeflag, name: h.subarray(0, 100), magic, version, prefix };
    records.push(record);
    off = dataStart + Math.ceil(size / BLOCK) * BLOCK;

    if (typeflag === 'L') pending.longName = cstrBytes(data);
    else if (typeflag === 'K') pending.longLink = cstrBytes(data);
    else if (typeflag === 'x') {
      const pax = paxRecords(data, dataStart - BLOCK);
      record.paxKeys = [...pax.keys()];
      if (pax.has('path')) pending.paxPath = pax.get('path');
      if (pax.has('linkpath')) pending.paxLinkpath = pax.get('linkpath');
    } else if (typeflag === 'g') {
      // A pax global header: ignored (FR-032).
    } else {
      let name = cstrBytes(h.subarray(0, 100));
      // The prefix field exists only in a POSIX ustar header; the old-GNU magic 'ustar  \0' puts atime there.
      const posix = magic.equals(Buffer.from('ustar\0', 'latin1')) && version.equals(Buffer.from('00', 'latin1'));
      const pre = cstrBytes(prefix);
      if (posix && pre.length > 0) name = Buffer.concat([pre, Buffer.from('/'), name]);
      const type = TYPES[typeflag] || 'other';
      const entry = { type, mode, path: pending.paxPath || pending.longName || name };
      if (type === 'l') entry.target = pending.paxLinkpath || pending.longLink || cstrBytes(h.subarray(157, 257));
      if (type === 'other') entry.typeflag = typeflag;
      entries.push(entry);
      pending = {};
    }
  }
  if (!ended) throw fail('E_LISTING_TRUNCATED', 'stream ends before the end-of-archive block');
  if (Object.keys(pending).length > 0) throw fail('E_LISTING_TRUNCATED', 'an L / K / x record has no following header');
  return raw ? { entries, records } : entries;
}

/** @param {Buffer} buffer a .tar.gz @returns {string[]} */
function listingFromTarGz(buffer) {
  let tar;
  try {
    tar = zlib.gunzipSync(buffer);
  } catch (err) {
    throw fail('E_LISTING_CORRUPT', `gzip: ${err.message}`);
  }
  return listingFromEntries(readTarEntries(tar));
}

// --- directory tree -----------------------------------------------------------------------------------------------

/** @param {string} root @returns {string[]} */
function listingFromDirectory(root) {
  if (process.platform === 'win32') {
    throw fail('E_LISTING_NO_POSIX_MODES', 'listingFromDirectory: win32 file systems carry no POSIX modes (D4)');
  }
  const slash = Buffer.from('/');
  const entries = [];
  // Names stay Buffers end to end, so a non-UTF-8 name reaches the E_LISTING_PATH rule instead of being replaced.
  const walk = (abs, rel) => {
    for (const dirent of fs.readdirSync(abs, { withFileTypes: true, encoding: 'buffer' })) {
      const name = Buffer.from(dirent.name);
      const relPath = rel === null ? name : Buffer.concat([rel, slash, name]);
      const absPath = Buffer.concat([abs, slash, name]);
      const st = fs.lstatSync(absPath);
      const mode = st.mode & 0o7777;
      if (st.isSymbolicLink()) {
        entries.push({ type: 'l', mode, path: relPath, target: fs.readlinkSync(absPath, { encoding: 'buffer' }) });
      } else if (st.isDirectory()) {
        entries.push({ type: 'd', mode, path: relPath });
        walk(absPath, relPath);
      } else if (st.isFile()) {
        entries.push({ type: 'f', mode, path: relPath });
      } else {
        entries.push({ type: 'other', mode, path: relPath, typeflag: 'lstat: not a file, directory or symlink' });
      }
    }
  };
  walk(Buffer.from(root), null);
  return listingFromEntries(entries);
}

// --- text form, digest, re-canonicalisation -----------------------------------------------------------------------

/** The committed .listing form: LF after every line, the last one included. */
function toText(lines) {
  return lines.map((x) => `${x}\n`).join('');
}

/** Lowercase hex SHA-256 of toText(lines) as UTF-8 (payload.sha256). */
function digest(lines) {
  return crypto.createHash('sha256').update(toText(lines), 'utf8').digest('hex');
}

/** '<t> <mmmm> <path>[ -> <target>]' -> Entry; the path is everything after the second space. */
function parseListingLine(line) {
  const m = typeof line === 'string' ? /^([fdl]) ([0-7]{4}) (.*)$/s.exec(line) : null;
  if (m === null) throw fail('E_LISTING_NOT_CANONICAL', `not a listing line: ${show(line)}`);
  const entry = { type: m[1], mode: parseInt(m[2], 8), path: m[3] };
  if (m[1] === 'l') {
    const at = m[3].indexOf(ARROW);
    if (at < 0) throw fail('E_LISTING_NOT_CANONICAL', `symlink line without ${show(ARROW)}: ${show(line)}`);
    entry.path = m[3].slice(0, at);
    entry.target = m[3].slice(at + ARROW.length);
  }
  return entry;
}

/** A listing must equal its own re-canonicalisation (FR-031 errors pass through with their own codes). */
function checkCanonicalListing(lines) {
  if (!Array.isArray(lines)) throw fail('E_LISTING_NOT_CANONICAL', 'listing is not an array of lines');
  const again = listingFromEntries(lines.map(parseListingLine));
  const i = again.findIndex((x, k) => x !== lines[k]);
  if (i >= 0 || again.length !== lines.length) {
    const k = i >= 0 ? i : Math.min(again.length, lines.length);
    throw fail('E_LISTING_NOT_CANONICAL', `line ${k + 1} is ${show(lines[k])}, canonical form has ${show(again[k])}`);
  }
}

/** FR-033: the root holds exactly <vst3Bundle>, presets, README.txt, CHANGELOG.txt; the bundle holds
 *  Contents/Resources/moduleinfo.json and Contents/x86_64-linux/<Name>.so. E_LAYOUT names the item. */
function checkLinuxLayout(lines, { vst3Bundle }) {
  if (typeof vst3Bundle !== 'string' || !vst3Bundle.endsWith('.vst3') || vst3Bundle.length <= 5) {
    throw fail('E_LAYOUT', `vst3Bundle is not a <Name>.vst3 name: ${show(vst3Bundle)}`);
  }
  const byPath = new Map(lines.map((x) => parseListingLine(x)).map((e) => [e.path, e.type]));
  const stem = vst3Bundle.slice(0, -'.vst3'.length);
  const required = [
    [vst3Bundle, 'd'],
    ['presets', 'd'],
    ['README.txt', 'f'],
    ['CHANGELOG.txt', 'f'],
    [`${vst3Bundle}/Contents/Resources/moduleinfo.json`, 'f'],
    [`${vst3Bundle}/Contents/x86_64-linux/${stem}.so`, 'f'],
  ];
  for (const [item, type] of required) {
    const have = byPath.get(item);
    if (have === undefined) throw fail('E_LAYOUT', `layout: missing ${type === 'd' ? 'directory' : 'file'} ${item}`);
    if (have !== type) throw fail('E_LAYOUT', `layout: ${item} is type ${have}, expected ${type}`);
  }
  const rootItems = new Set(required.slice(0, 4).map(([item]) => item));
  for (const p of byPath.keys()) {
    if (!p.includes('/') && !rootItems.has(p)) throw fail('E_LAYOUT', `layout: unexpected root item ${p}`);
  }
}

module.exports = {
  listingFromEntries,
  listingFromTarGz,
  listingFromDirectory,
  digest,
  toText,
  parseListingLine,
  checkCanonicalListing,
  checkLinuxLayout,
  readTarEntries,
};
