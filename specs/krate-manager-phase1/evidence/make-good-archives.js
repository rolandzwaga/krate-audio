#!/usr/bin/env node
// Builds the well-formed C4 listing archives with GNU tar in WSL, then their .listing / .sha256 sidecars with
// listing.js (spec FR-058; plan §4.7, PD-18; tasks T006). Run once from the repo root on the Windows dev box:
//
//   node specs/krate-manager-phase1/evidence/make-good-archives.js
//
// WHY WSL: NTFS (and this checkout's core.symlinks=false) cannot hold POSIX modes or symlinks, so every tree is built
// in a `mktemp -d` directory under WSL /tmp (never under /mnt/f, which would strip modes and links), archived with
// GNU tar 1.35 there, gzipped with `gzip -n -9` (no name, no timestamp in the gzip header) straight into the repo's
// fixture directory through /mnt/f, and removed.
//
// Each bash script is fed to `wsl -e bash -s` on stdin rather than `wsl -e bash -c '<script>'`: the scripts hold
// quotes and spaces that would otherwise have to survive Windows command-line quoting; bash runs the same text.
//
// Outputs: tools/feed-contract/fixtures/listings/good/<name>.{tar.gz,listing,sha256} for the nine good archives,
// tools/feed-contract/fixtures/listings/bad/<name>.{tar.gz,expected-error} for the two layout archives (E_LAYOUT).
// It prints every script, the tar version, and `tar -tvzf` of every archive for the by-eye cross-check recorded in
// tools/feed-contract/fixtures/listings/README.md.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const childProcess = require('node:child_process');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const LISTINGS = path.join(ROOT, 'tools', 'feed-contract', 'fixtures', 'listings');
const listing = require(path.join(ROOT, 'tools', 'feed-contract', 'listing.js'));

const toWsl = (p) => {
  const m = /^([A-Za-z]):[\\/](.*)$/.exec(p);
  if (m === null) throw new Error(`not a drive path: ${p}`);
  return `/mnt/${m[1].toLowerCase()}/${m[2].split(path.sep).join('/')}`;
};

const B = 'Alpha.vst3';
const RES = `${B}/Contents/Resources`;
const LIB = `${B}/Contents/x86_64-linux`;
const ITEMS = `${B} CHANGELOG.txt README.txt presets`;
const padTo = (s, n) => s + 'x'.repeat(n - s.length);
const q = (s) => {
  if (s.includes("'")) throw new Error(`no single quotes in fixture names: ${s}`);
  return `'${s}'`;
};

// --- names that must cross the 100 / 255 byte limits (asserted, so a wrong count fails here, not in a test)

const longPreset = 'presets/Long Names/A preset whose file name is deliberately long enough to need a GNU long-name record.vstpreset';

const longLinkDir = `${RES}/a-deliberately-long-directory-name-for-the-gnu-long-link-fixture`;
const longLinkFile = 'a-deliberately-long-file-name-for-the-long-link-target.json';
const longLinkTarget = `../Resources/${path.posix.basename(longLinkDir)}/${longLinkFile}`;

const paxDeep = [`${RES}/pax`, ...[1, 2, 3, 4].map((i) => padTo(`segment-${i}-`, 60))].join('/');
const paxTargetDir = padTo('target-directory-', 60);
const paxTargetFile = `${padTo('target-file-', 55)}.json`;
const paxLink = `${paxDeep}/link.json`;
const paxTarget = `${paxTargetDir}/${paxTargetFile}`;

const ustarDir = `${RES}/${padTo('ustar-prefix-directory-', 90)}`;
const ustarFile = `${ustarDir}/split-by-the-ustar-prefix-field.json`;

function assertLength(label, s, ok) {
  const n = Buffer.byteLength(s);
  if (!ok(n)) throw new Error(`${label}: ${n} bytes is outside its range: ${s}`);
}
assertLength('long-name preset path', longPreset, (n) => n > 100);
assertLength('long-link target', longLinkTarget, (n) => n > 100);
assertLength('long-link link path', `${LIB}/long-link.json`, (n) => n <= 100);
assertLength('pax-path link path', paxLink, (n) => n > 255);
assertLength('pax-path link target', paxTarget, (n) => n > 100);
assertLength('ustar-prefix file path', ustarFile, (n) => n > 100 && n <= 255);
assertLength('ustar-prefix prefix part', ustarDir, (n) => n <= 155);
assertLength('ustar-prefix name part', path.posix.basename(ustarFile), (n) => n <= 100);

// --- the archives

const ARCHIVES = [
  {
    name: 'real-shaped',
    setup: [
      `printf '<vstgui-ui-description/>\\n' > ${RES}/editor.uidesc`,
      `mkdir -p 'presets/Warm Pads'`,
      `printf 'Slow Swell\\n' > 'presets/Warm Pads/Slow Swell.vstpreset'`,
      `printf 'Night Air\\n' > 'presets/Warm Pads/Night Air.vstpreset'`,
    ],
  },
  {
    name: 'exec-and-symlink',
    setup: [`ln -s Alpha.so ${LIB}/libalpha.so`],
    post: [`chmod 0755 ${LIB}/Alpha.so`],
  },
  {
    name: 'long-name',
    setup: [`mkdir -p 'presets/Long Names'`, `printf 'long\\n' > ${q(longPreset)}`],
  },
  {
    name: 'long-link',
    setup: [
      `mkdir -p ${q(longLinkDir)}`,
      `printf '{}\\n' > ${q(`${longLinkDir}/${longLinkFile}`)}`,
      `ln -s ${q(longLinkTarget)} ${LIB}/long-link.json`,
    ],
  },
  {
    name: 'pax-path',
    format: 'pax',
    setup: [
      `mkdir -p ${q(`${paxDeep}/${paxTargetDir}`)}`,
      `printf '{}\\n' > ${q(`${paxDeep}/${paxTarget}`)}`,
      `ln -s ${q(paxTarget)} ${q(paxLink)}`,
    ],
  },
  { name: 'pax-global', format: 'pax', extra: ['--pax-option=comment=krate-fixture'] },
  {
    name: 'implicit-dirs',
    setup: [`find . -type f -printf '%P\\n' | sort > "$T.list"`],
    extra: ['--no-recursion'],
    items: '-T "$T.list"',
  },
  { name: 'dot-slash', items: '.' },
  {
    name: 'ustar-prefix',
    format: 'ustar',
    setup: [`mkdir -p ${q(ustarDir)}`, `printf '{}\\n' > ${q(ustarFile)}`],
  },
  {
    name: 'layout-extra-root-file',
    bad: true,
    setup: [`printf 'notes\\n' > NOTES.txt`],
    items: `${ITEMS} NOTES.txt`,
  },
  {
    name: 'layout-missing-presets',
    bad: true,
    setup: ['rm -rf presets'],
    items: `${B} CHANGELOG.txt README.txt`,
  },
];

const BASE = [
  `mkdir -p ${RES} ${LIB} presets`,
  `printf '%s\\n' '{"Name": "Alpha", "Version": "1.2.0", "Classes": [{"Name": "Alpha", "Version": "1.2.0"}]}' > ${RES}/moduleinfo.json`,
  `printf 'fixture: not a real shared object\\n' > ${LIB}/Alpha.so`,
  `printf 'Init\\n' > presets/Init.vstpreset`,
  `printf 'Alpha README\\n' > README.txt`,
  `printf 'Alpha CHANGELOG\\n' > CHANGELOG.txt`,
];

function script(a, dest) {
  const tar = [
    'tar --owner=0 --group=0 --numeric-owner',
    "--mtime='2026-10-29 00:00:00Z' --sort=name",
    `--format=${a.format || 'gnu'}`,
    ...(a.extra || []),
    '-cf - -C "$T"',
    a.items || ITEMS,
  ].join(' ');
  return [
    'set -euo pipefail',
    'umask 022',
    'export LC_ALL=C',
    'T=$(mktemp -d /tmp/krate-listing-XXXXXX)',
    `trap 'rm -rf "$T" "$T.list"' EXIT`,
    'cd "$T"',
    ...BASE,
    ...(a.setup || []),
    'find . -mindepth 1 -type d -exec chmod 0755 {} +',
    'find . -type f -exec chmod 0644 {} +',
    ...(a.post || []),
    `${tar} | gzip -n -9 > ${q(dest)}`,
    '',
  ].join('\n');
}

function wsl(args, input) {
  const r = childProcess.spawnSync('wsl', args, { input, encoding: 'utf8' });
  if (r.status !== 0) throw new Error(`wsl ${args.join(' ')} exited ${r.status}: ${r.error || r.stderr}`);
  return r.stdout;
}

function main() {
  process.stdout.write(`$ wsl -e tar --version\n${wsl(['-e', 'tar', '--version']).split('\n')[0]}\n\n`);
  for (const a of ARCHIVES) {
    const dir = path.join(LISTINGS, a.bad ? 'bad' : 'good');
    fs.mkdirSync(dir, { recursive: true });
    const file = path.join(dir, `${a.name}.tar.gz`);
    const text = script(a, toWsl(file));
    process.stdout.write(`=== ${a.name} (${a.bad ? 'bad' : 'good'})\n$ wsl -e bash -s <<'SCRIPT'\n${text}SCRIPT\n`);
    wsl(['-e', 'bash', '-s'], text);

    const buf = fs.readFileSync(file);
    const lines = listing.listingFromTarGz(buf);
    if (a.bad) {
      fs.writeFileSync(path.join(dir, `${a.name}.expected-error`), 'E_LAYOUT\n');
    } else {
      fs.writeFileSync(path.join(dir, `${a.name}.listing`), listing.toText(lines));
      fs.writeFileSync(path.join(dir, `${a.name}.sha256`), `${listing.digest(lines)}\n`);
    }
    process.stdout.write(`--- listing.js (${lines.length} lines, sha256 ${listing.digest(lines)})\n${listing.toText(lines)}`);
    process.stdout.write(`--- $ wsl -e tar -tvzf ${toWsl(file)}\n${wsl(['-e', 'tar', '-tvzf', toWsl(file)])}\n`);
  }
}

main();
