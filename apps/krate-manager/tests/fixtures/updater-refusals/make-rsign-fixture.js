#!/usr/bin/env node
// Makes the rsign-format fixture of the Phase 11 refusal harness (plan §4.7 `rsign-format`, PD-35 (b), tasks TA-25,
// TA-28): fixtures/rsign/package.bin (fixed bytes), its rsign2 signature package.bin.minisig and the throwaway public
// key rsign.pub.
//
// WHY: the release workflow's `sign` job signs updater artifacts with the rsign2 CLI, so the harness proves that an
// rsign-made signature passes the updater's own verification. The key pair is throwaway: generated in a fresh
// fs.mkdtempSync directory under os.tmpdir() (outside the repo) and deleted in a `finally`; only the public key and the
// signature are copied out. Run ONCE: it refuses to run when any output exists.
//
// Usage (from this directory):  node make-rsign-fixture.js [--self-test]
// Prints every command, its exit code and output, the CR / LF counts, the minisig's first two lines and the SHA-256 of
// the three files.

import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const FIXTURE_REL = 'fixtures/rsign';
const FIXTURE_DIR = path.join(HERE, 'fixtures', 'rsign');
const PACKAGE = path.join(FIXTURE_DIR, 'package.bin');
const MINISIG = path.join(FIXTURE_DIR, 'package.bin.minisig');
const PUBKEY = path.join(FIXTURE_DIR, 'rsign.pub');
const OUTPUTS = [PACKAGE, MINISIG, PUBKEY];
const EXPECTED_RSIGN = 'rsign2 0.6.7';
/** The trusted comment, real tab characters (the Tauri CLI's `key:value` fields; `version:` binds 0.2.0). */
const TRUSTED_COMMENT = ['timestamp:0', 'file:package.bin', 'version:0.2.0'].join('\t');
/** TA-25: never rsign's default comment, whose `secret key` words the FR-083 tree scan reports. */
const UNTRUSTED_COMMENT = 'rsign-format fixture (Krate Manager Phase 11)';
/** Fixed bytes; no plugin name (tasks Rule 4 (f)). */
const PACKAGE_BYTES = Buffer.from('krate-manager rsign-format fixture\n', 'utf8');

/** `rsign generate` into the temp dir, passwordless. */
function generateArgv(dir) {
  return ['generate', '-W', '-p', path.join(dir, 'rsign.pub'), '-s', path.join(dir, 'k.key')];
}

/** `rsign sign` of the fixture package, the signature written into the temp dir. */
function signArgv(dir) {
  return [
    'sign', '-W', '-s', path.join(dir, 'k.key'),
    '-c', UNTRUSTED_COMMENT,
    '-t', TRUSTED_COMMENT,
    '-x', path.join(dir, 'package.bin.minisig'),
    `${FIXTURE_REL}/package.bin`,
  ];
}

/** The outputs that already exist. */
function existingOutputs(paths, exists) {
  return paths.filter((p) => exists(p));
}

function countEndings(text) {
  return { cr: (text.match(/\r/gu) || []).length, lf: (text.match(/\n/gu) || []).length };
}

/** CRLF -> LF, with the CR / LF counts before and after. */
function normaliseLf(text) {
  const out = text.replace(/\r\n/gu, '\n');
  return { text: out, before: countEndings(text), after: countEndings(out) };
}

function fail(msg) {
  console.log(`make-rsign-fixture: ${msg}`);
  process.exit(1);
}

/** Runs one command (argv array, stdin closed), prints it, its exit code and output; fails on a non-zero exit. */
function run(args) {
  console.log(`$ rsign ${args.map((a) => JSON.stringify(a)).join(' ')}`);
  const r = spawnSync('rsign', args, { cwd: HERE, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
  if (r.error) fail(`rsign: ${r.error.message}`);
  const out = `${r.stdout || ''}${r.stderr || ''}`;
  if (out) process.stdout.write(out.replace(/^/gmu, '  > '));
  console.log(`  exit ${r.status}`);
  if (r.status !== 0) fail(`rsign exited ${r.status}`);
  return r.stdout || '';
}

/** Copies a text file rsign wrote, CRLF -> LF, and prints both counts. */
function copyLf(from, to, name) {
  const n = normaliseLf(fs.readFileSync(from, 'utf8'));
  fs.writeFileSync(to, n.text);
  console.log(`${name}: before CR=${n.before.cr} LF=${n.before.lf}; after CR=${n.after.cr} LF=${n.after.lf}`);
}

function sha256(file) {
  return createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}

function make() {
  const present = existingOutputs(OUTPUTS, (p) => fs.existsSync(p));
  if (present.length > 0) {
    fail(`refusing to overwrite ${path.relative(HERE, present[0]).split(path.sep).join('/')}`);
  }
  const version = run(['--version']).trim();
  if (version !== EXPECTED_RSIGN) fail(`rsign --version printed ${JSON.stringify(version)}, not ${EXPECTED_RSIGN}`);
  fs.mkdirSync(FIXTURE_DIR, { recursive: true });
  fs.writeFileSync(PACKAGE, PACKAGE_BYTES);
  console.log(`wrote ${FIXTURE_REL}/package.bin (${PACKAGE_BYTES.length} bytes)`);
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'km11-rsign-'));
  console.log(`key dir: ${dir}`);
  try {
    run(generateArgv(dir));
    run(signArgv(dir));
    copyLf(path.join(dir, 'rsign.pub'), PUBKEY, 'rsign.pub');
    copyLf(path.join(dir, 'package.bin.minisig'), MINISIG, 'package.bin.minisig');
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
  if (fs.existsSync(dir)) fail(`key dir ${dir} still exists`);
  console.log(`key dir ${dir} deleted (existsSync false)`);
  const lines = fs.readFileSync(MINISIG, 'utf8').split('\n');
  console.log(`minisig line 1: ${lines[0]}`);
  console.log(`minisig line 2: ${lines[1]}`);
  for (const f of OUTPUTS) {
    console.log(`sha256 ${sha256(f)}  ${path.relative(HERE, f).split(path.sep).join('/')}`);
  }
  console.log('MAKE-RSIGN-FIXTURE DONE');
}

function selfTest() {
  const failures = [];
  const expect = (what, got, want) => {
    if (JSON.stringify(got) !== JSON.stringify(want)) {
      failures.push(`${what}: got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`);
    }
  };
  const dir = path.join('T', 'km11-rsign-x');
  expect('generate argv', generateArgv(dir), [
    'generate', '-W', '-p', path.join(dir, 'rsign.pub'), '-s', path.join(dir, 'k.key'),
  ]);
  expect('sign argv', signArgv(dir), [
    'sign', '-W', '-s', path.join(dir, 'k.key'),
    '-c', 'rsign-format fixture (Krate Manager Phase 11)',
    '-t', 'timestamp:0\tfile:package.bin\tversion:0.2.0',
    '-x', path.join(dir, 'package.bin.minisig'),
    'fixtures/rsign/package.bin',
  ]);
  expect('trusted comment tabs', [...TRUSTED_COMMENT].filter((c) => c.charCodeAt(0) === 9).length, 2);
  expect('trusted comment has no backslash', TRUSTED_COMMENT.includes('\\'), false);
  expect('untrusted comment', UNTRUSTED_COMMENT, 'rsign-format fixture (Krate Manager Phase 11)');
  expect('package bytes', PACKAGE_BYTES.toString('utf8'), 'krate-manager rsign-format fixture\n');
  const outputs = ['a', 'b', 'c'];
  expect('refusal when one output exists', existingOutputs(outputs, (p) => p === 'b'), ['b']);
  expect('no refusal when none exists', existingOutputs(outputs, () => false), []);
  const canned = 'untrusted comment: x\r\nRWQ\r\ntrusted comment: y\r\nZZZ\r\n';
  const n = normaliseLf(canned);
  expect('normalised text', n.text, 'untrusted comment: x\nRWQ\ntrusted comment: y\nZZZ\n');
  expect('counts before', n.before, { cr: 4, lf: 4 });
  expect('counts after', n.after, { cr: 0, lf: 4 });
  if (failures.length > 0) {
    for (const f of failures) console.error(`self-test: ${f}`);
    process.exit(1);
  }
  console.log('make-rsign-fixture self-test: ok');
}

if (process.argv.includes('--self-test')) {
  selfTest();
} else {
  make();
}
