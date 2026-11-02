#!/usr/bin/env node
// Writes the malformed C4 listing archives from hand-built 512-byte ustar headers (spec FR-058; plan §4.7, PD-12, PD-18).
//
// WHY: GNU tar cannot write these from a POSIX tree (it strips a leading '/' without -P, needs root mknod for a
// device, never writes a drive or backslash path, a bad checksum or a cut stream), so each one is built byte by byte
// here: every header carries its own computed checksum (unless the defect is the checksum), and the stream is gzipped
// with node:zlib. Each archive is a small valid C4 tree plus exactly one defect, so the first FR-031 check that fails
// is the one the archive exists for; its code goes in <name>.expected-error.
//
// Usage:
//   node tools/feed-contract/fixtures/listings/make-bad.js --out tools/feed-contract/fixtures/listings/bad
// Writes only <out>/<name>.tar.gz and <out>/<name>.expected-error. No dependencies outside node:.
// The two bad LAYOUT archives (layout-extra-root-file, layout-missing-presets) are well-formed and are made by GNU tar
// in WSL (specs/krate-manager-phase1/evidence/make-good-archives.js), not here.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');

const BLOCK = 512;
const MTIME = Date.UTC(2026, 9, 29) / 1000; // 2026-10-29 00:00:00Z, the good set's --mtime
const B = 'Alpha.vst3';

function put(h, offset, length, text) {
  const b = Buffer.from(text, 'utf8');
  if (b.length > length) throw new Error(`field at ${offset} overflows: ${text}`);
  b.copy(h, offset);
}

const oct = (n, width) => n.toString(8).padStart(width - 1, '0') + '\0';

function checksum(h) {
  let sum = 0;
  for (let i = 0; i < BLOCK; i++) sum += i >= 148 && i < 156 ? 0x20 : h[i];
  return sum;
}

/** One POSIX ustar header. `size` is the data length; `rawSize` (12 bytes) replaces the octal size field;
 *  `badChecksum` stores the computed checksum plus one. */
function header({ name, typeflag = '0', mode = 0o644, size = 0, linkname = '', badChecksum = false, rawSize = null }) {
  const h = Buffer.alloc(BLOCK);
  put(h, 0, 100, name);
  put(h, 100, 8, oct(mode, 8));
  put(h, 108, 8, oct(0, 8));
  put(h, 116, 8, oct(0, 8));
  if (rawSize === null) put(h, 124, 12, oct(size, 12));
  else rawSize.copy(h, 124);
  put(h, 136, 12, oct(MTIME, 12));
  h[156] = typeflag.charCodeAt(0);
  put(h, 157, 100, linkname);
  put(h, 257, 6, 'ustar\0');
  put(h, 263, 2, '00');
  put(h, 265, 32, 'root');
  put(h, 297, 32, 'root');
  put(h, 329, 8, oct(0, 8));
  put(h, 337, 8, oct(0, 8));
  const sum = checksum(h) + (badChecksum ? 1 : 0);
  put(h, 148, 8, sum.toString(8).padStart(6, '0') + '\0 ');
  return h;
}

const pad = (data) => Buffer.concat([data, Buffer.alloc((BLOCK - (data.length % BLOCK)) % BLOCK)]);

// Member builders: each returns the header plus its padded data.
const file = (name, text = `${name}\n`, extra = {}) => {
  const data = Buffer.from(text, 'utf8');
  return Buffer.concat([header({ name, size: data.length, ...extra }), pad(data)]);
};
const dir = (name) => header({ name, typeflag: '5', mode: 0o755 });
const link = (name, target) => header({ name, typeflag: '2', mode: 0o777, linkname: target });
const pax = (records) => {
  const data = Buffer.from(records, 'utf8');
  return Buffer.concat([header({ name: 'PaxHeaders/x', typeflag: 'x', size: data.length }), pad(data)]);
};
const END = Buffer.alloc(2 * BLOCK);

// A valid C4 root (FR-033) for the archive Alpha.vst3.
const BASE = () => [
  dir(`${B}/`),
  dir(`${B}/Contents/`),
  dir(`${B}/Contents/Resources/`),
  file(`${B}/Contents/Resources/moduleinfo.json`, '{"Name": "Alpha", "Version": "1.2.0"}\n'),
  dir(`${B}/Contents/x86_64-linux/`),
  file(`${B}/Contents/x86_64-linux/Alpha.so`, 'fixture: not a real shared object\n'),
  dir('presets/'),
  file('presets/Init.vstpreset'),
  file('README.txt'),
  file('CHANGELOG.txt'),
];

const withEnd = (members) => Buffer.concat([...members, END]);

// name -> [expected code, () => uncompressed tar stream]
const ARCHIVES = {
  'absolute-path': ['E_LISTING_ABSOLUTE', () => withEnd([...BASE(), file('/etc/evil')])],
  'dotdot-path': ['E_LISTING_DOTDOT', () => withEnd([...BASE(), file('../evil')])],
  'drive-path': ['E_LISTING_DRIVE', () => withEnd([...BASE(), file('C:/x')])],
  'backslash-path': ['E_LISTING_BACKSLASH', () => withEnd([...BASE(), file(`${B}\\evil`)])],
  'symlink-escape-root': ['E_LISTING_SYMLINK_ESCAPE', () => withEnd([...BASE(), link(`${B}/Contents/x`, '../../../etc')])],
  'symlink-escape-bundle': [
    'E_LISTING_SYMLINK_ESCAPE',
    () => withEnd([...BASE(), link(`${B}/Contents/x`, '../../README.txt')]),
  ],
  'root-symlink': ['E_LISTING_ROOT_SYMLINK', () => withEnd([...BASE(), link('readme-link', 'README.txt')])],
  'hard-link': ['E_LISTING_TYPE', () => withEnd([...BASE(), header({ name: `${B}/Contents/hard`, typeflag: '1', linkname: 'README.txt' })])],
  'char-device': ['E_LISTING_TYPE', () => withEnd([...BASE(), header({ name: `${B}/Contents/dev`, typeflag: '3', mode: 0o600 })])],
  'duplicate-entry': ['E_LISTING_DUPLICATE', () => withEnd([...BASE(), file('README.txt')])],
  'file-dir-clash': ['E_LISTING_DUPLICATE', () => withEnd([...BASE(), file('presets/Init.vstpreset/inner.vstpreset')])],
  truncated: [
    'E_LISTING_TRUNCATED',
    () => {
      // Cut 100 bytes into the header of the fourth member: a tar stream that ends mid-header, gzipped whole.
      const members = BASE();
      const before = Buffer.concat(members.slice(0, 3));
      return Buffer.concat([before, members[3].subarray(0, 100)]);
    },
  ],
  'no-end-block': ['E_LISTING_TRUNCATED', () => Buffer.concat(BASE())],
  'bad-checksum': [
    'E_LISTING_CHECKSUM',
    () => withEnd([...BASE().slice(0, -2), file('README.txt', 'README.txt\n', { badChecksum: true }), file('CHANGELOG.txt')]),
  ],
  'pax-malformed': ['E_LISTING_CORRUPT', () => withEnd([...BASE(), pax('1x path=a\n'), file(`${B}/Contents/after-pax`)])],
  'base256-size': [
    'E_LISTING_CORRUPT',
    () => {
      // GNU base-256: the high bit of the first byte set, the value big-endian in the rest (5 bytes of data).
      const raw = Buffer.alloc(12);
      raw[0] = 0x80;
      raw[11] = 5;
      const data = Buffer.from('12345');
      return withEnd([...BASE(), header({ name: `${B}/Contents/b256`, rawSize: raw }), pad(data)]);
    },
  ],
  'symlink-dotdot-chain': [
    'E_LISTING_SYMLINK_FORM',
    () =>
      withEnd([
        ...BASE(),
        dir(`${B}/b/`),
        dir(`${B}/b/c/`),
        link(`${B}/b/c/s`, '../../d'),
        link(`${B}/b/c/t`, 's/../../../x'),
      ]),
  ],
  'symlink-chain': [
    'E_LISTING_SYMLINK_CHAIN',
    () => withEnd([...BASE(), link(`${B}/Contents/s`, 'Resources'), link(`${B}/Contents/t`, 's/moduleinfo.json')]),
  ],
  'symlink-self': ['E_LISTING_SYMLINK_CHAIN', () => withEnd([...BASE(), link(`${B}/Contents/l`, 'l')])],
  'through-symlink': [
    'E_LISTING_THROUGH_SYMLINK',
    () => withEnd([...BASE(), link(`${B}/Contents/t`, 'Resources'), file(`${B}/Contents/t/evil`)]),
  ],
};

function gzip(tar) {
  const gz = zlib.gzipSync(tar, { level: 9 });
  gz[9] = 0xff; // OS byte "unknown": zlib writes a platform-specific value here; the CRC does not cover it.
  return gz;
}

function main(argv) {
  const i = argv.indexOf('--out');
  if (i < 0 || argv[i + 1] === undefined || argv.length !== 2) {
    process.stderr.write('usage: node make-bad.js --out <dir>\n');
    return 2;
  }
  const out = path.resolve(argv[i + 1]);
  fs.mkdirSync(out, { recursive: true });
  for (const [name, [code, build]] of Object.entries(ARCHIVES)) {
    fs.writeFileSync(path.join(out, `${name}.tar.gz`), gzip(build()));
    fs.writeFileSync(path.join(out, `${name}.expected-error`), `${code}\n`);
    process.stdout.write(`${name}.tar.gz  ${code}\n`);
  }
  process.stdout.write(`wrote ${Object.keys(ARCHIVES).length} archives to ${out}\n`);
  return 0;
}

if (require.main === module) process.exitCode = main(process.argv.slice(2));

module.exports = { ARCHIVES };
