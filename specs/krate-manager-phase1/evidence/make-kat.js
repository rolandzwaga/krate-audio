#!/usr/bin/env node
// Makes the C3 known-answer fixture (FR-023, SC-013 creation half): KatPlugin-1.2.3-Linux-x64.tar.gz, its rsign2
// .minisig and the public key kat.pub, under tools/feed-contract/fixtures/kat/.
//
// WHY: the trusted-comment grammar (C3) needs one real minisign signature made by the tool the probes proved against a
// tauri signer key, so the parser and the CI "rsign verify (KAT)" step have a known answer. The private key is
// throwaway: generated in a fresh fs.mkdtemp directory under os.tmpdir() (outside the repo) and deleted right after
// signing. Run ONCE; it refuses to run when the archive or the .minisig already exists, so a rerun can never replace
// the committed KAT.
//
// Usage (repo root, Windows with WSL and rsign2 0.6.7 on PATH):
//   node specs/krate-manager-phase1/evidence/make-kat.js
// Steps (plan.md §4.7 "KAT"; tasks.md T007):
//   1. build the archive in WSL from a WSL /tmp tree with the T006 good-set flags (--format=gnu, gzip -n -9);
//   2. key dir = fs.mkdtempSync(os.tmpdir()/kat-);  3. rsign generate;  4. rsign sign -t <C3 comment>;
//   5. copy kat.pub;  6. rewrite the .minisig with toLfMinisig (D7);  7. rsign verify;  8. delete the key dir.
// Prints every command and its output.
'use strict';
const childProcess = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const contract = require('../../../tools/feed-contract/contract.js');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const KAT_REL = 'tools/feed-contract/fixtures/kat';
const KAT_DIR = path.join(ROOT, ...KAT_REL.split('/'));
const ASSET = 'KatPlugin-1.2.3-Linux-x64.tar.gz';
const ARCHIVE = path.join(KAT_DIR, ASSET);
const MINISIG = `${ARCHIVE}.minisig`;
const TRUSTED_COMMENT = `krate:1:KatPlugin:1.2.3:linux-x86_64:${ASSET}`;
const EXPECTED_RSIGN = 'rsign2 0.6.7';

function fail(msg) {
  console.error(`make-kat: ${msg}`);
  process.exit(1);
}

/** Runs one command, prints it and its output, and fails on a non-zero exit. */
function run(cmd, args, opts = {}) {
  console.log(`$ ${[cmd, ...args].join(' ')}`);
  if (opts.input) console.log(`  (stdin script)\n${opts.input.replace(/^/gmu, '  | ')}`);
  const r = childProcess.spawnSync(cmd, args, { cwd: ROOT, encoding: 'utf8', ...opts });
  if (r.error) fail(`${cmd}: ${r.error.message}`);
  const out = `${r.stdout || ''}${r.stderr || ''}`;
  if (out) process.stdout.write(out.replace(/^/gmu, '  > '));
  console.log(`  exit ${r.status}`);
  if (r.status !== 0) fail(`${cmd} exited ${r.status}`);
  return out;
}

/** F:\projects\iterum\x -> /mnt/f/projects/iterum/x (WSL drvfs mount). */
function toWslPath(p) {
  const m = /^([A-Za-z]):[\\/](.*)$/u.exec(p);
  if (!m) fail(`not a drive path: ${p}`);
  return `/mnt/${m[1].toLowerCase()}/${m[2].replace(/\\/gu, '/')}`;
}

if (fs.existsSync(ARCHIVE) || fs.existsSync(MINISIG)) {
  fail(`${KAT_REL}/${ASSET} or its .minisig already exists; the committed KAT is never replaced`);
}
const version = run('rsign', ['--version']).trim();
if (version !== EXPECTED_RSIGN) fail(`rsign --version printed ${JSON.stringify(version)}, not ${EXPECTED_RSIGN}`);
fs.mkdirSync(KAT_DIR, { recursive: true });

// (1) Archive. The tree lives in WSL /tmp (never /mnt/f, which strips modes); files 0644, dirs 0755, as the real
// tarball (spec "Real data"). Every line is LF because bash reads the script from stdin.
const moduleinfo = '{"Name": "KatPlugin", "Version": "1.2.3", "Classes": [{"Name": "KatPlugin", "Version": "1.2.3"}]}';
const script = [
  'set -euo pipefail',
  'umask 022',
  'T="$(mktemp -d)"',
  'trap \'rm -rf "$T"\' EXIT',
  'cd "$T"',
  'mkdir -p KatPlugin.vst3/Contents/Resources KatPlugin.vst3/Contents/x86_64-linux presets',
  `printf '%s\\n' '${moduleinfo}' > KatPlugin.vst3/Contents/Resources/moduleinfo.json`,
  "printf '%s\\n' 'KatPlugin known-answer fixture, not a plug-in.' > KatPlugin.vst3/Contents/x86_64-linux/KatPlugin.so",
  "printf '%s\\n' 'KatPlugin default preset (fixture).' > presets/Default.preset",
  "printf '%s\\n' 'KatPlugin 1.2.3: C3 known-answer fixture.' > README.txt",
  "printf '%s\\n' '## [1.2.3] - 2026-10-30' '' '- Known-answer fixture.' > CHANGELOG.txt",
  'find . -type d -exec chmod 0755 {} +',
  'find . -type f -exec chmod 0644 {} +',
  "tar --owner=0 --group=0 --numeric-owner --mtime='2026-10-29 00:00:00Z' --sort=name --format=gnu -cf - " +
    `-C "$T" CHANGELOG.txt KatPlugin.vst3 README.txt presets | gzip -n -9 > '${toWslPath(ARCHIVE)}'`,
  'tar --version | head -1',
  `tar -tvzf '${toWslPath(ARCHIVE)}'`,
  '',
].join('\n');
run('wsl', ['-e', 'bash', '-s'], { input: script });

// (2) Throwaway key directory outside the repo.
const key = fs.mkdtempSync(path.join(os.tmpdir(), 'kat-'));
const keyRel = path.relative(ROOT, key); // absolute when on another drive
if (!keyRel.startsWith('..') && !path.isAbsolute(keyRel)) fail(`key dir ${key} is inside the repo`);
console.log(`key dir: ${key}`);
const pub = path.join(key, 'kat.pub');
const sec = path.join(key, 'kat.key');
try {
  // (3) Passwordless key pair.
  run('rsign', ['generate', '-p', pub, '-s', sec, '-W']);
  // (4) Sign; rsign writes <file>.minisig (CRLF on Windows, spec External facts).
  run('rsign', ['sign', '-s', sec, '-W', '-t', TRUSTED_COMMENT, ARCHIVE]);
  // (5) Public key into the fixture dir.
  fs.copyFileSync(pub, path.join(KAT_DIR, 'kat.pub'));
} finally {
  // (8) Delete the secret key whatever happened above.
  fs.rmSync(key, { recursive: true, force: true });
}
if (fs.existsSync(key)) fail(`key dir ${key} still exists`);
console.log(`key dir ${key} deleted (existsSync false)`);

// (6) D7: store the .minisig with LF; no signed byte changes.
const raw = fs.readFileSync(MINISIG, 'utf8');
console.log(`.minisig as written by rsign: ${raw.includes('\r\n') ? 'CRLF' : 'LF'}`);
fs.writeFileSync(MINISIG, contract.toLfMinisig(raw));
const parsed = contract.parseMinisigText(fs.readFileSync(MINISIG, 'utf8'));
console.log(`parsed: algorithm ${parsed.algorithm}, keyId ${parsed.keyId.toString('hex')}, trusted comment ${parsed.trustedComment}`);
if (parsed.trustedComment !== TRUSTED_COMMENT) fail('trusted comment differs from the one signed');

// (7) Verify the normalised file.
run('rsign', ['verify', '-p', `${KAT_REL}/kat.pub`, `${KAT_REL}/${ASSET}`]);
console.log('MAKE-KAT DONE');
