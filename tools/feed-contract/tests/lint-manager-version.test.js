'use strict';
require('./helpers/write-guard.js');

// tools/lint-manager-version.js (spec FR-060, FR-063, D18, SC-012; plan §4.15, §4.17, PD-13).
// Every case builds a fresh temp tree (the all-equal tree plus one override), runs main(['--root', dir], io) with a
// capturing io, and asserts the exit code and the exact first stderr line. Nothing here reads or writes the real
// apps/krate-manager tree: L-VLINT runs the lint on it.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const lintManagerVersion = require('../../lint-manager-version.js');

const PREFIX = 'lint-manager-version: ';
const USAGE = 'usage: node tools/lint-manager-version.js [--root <dir>]';

// The all-equal tree: a literal member version (crates/core), two version.workspace = true members, publish = false
// in [workspace.package] (PD-13), a [[bin]] header in src-tauri, inline tables only in sections the lint does not read.
const BASE = {
  'Cargo.toml': [
    '# Workspace manifest of the fixture tree.',
    '[workspace]',
    'members = [',
    '  "crates/core", # the literal-version member',
    '  "crates/helper",',
    '  "src-tauri",',
    ']',
    'resolver = "3"',
    '',
    '[workspace.package]',
    'version = "0.1.0"',
    'edition = "2024"',
    'license = "MIT"',
    'publish = false',
    '',
    '[workspace.dependencies]',
    'serde = { version = "1.0.229", features = ["derive"] }',
    '',
  ].join('\n'),
  'crates/core/Cargo.toml': [
    '[package]',
    'name = "krate-manager-core"',
    'version = "0.1.0"',
    'edition.workspace = true',
    'publish.workspace = true',
    '',
    '[dependencies]',
    'serde = { workspace = true }',
    '',
  ].join('\n'),
  'crates/helper/Cargo.toml': [
    '[package]',
    'name = "krate-manager-helper"',
    'version.workspace = true',
    'edition.workspace = true',
    '',
    '[lints]',
    'workspace = true',
    '',
  ].join('\n'),
  'src-tauri/Cargo.toml': [
    '[package]',
    'name = "krate-manager"',
    'version.workspace = true',
    'edition.workspace = true',
    '',
    '[lib]',
    'name = "krate_manager_lib"',
    'test = false',
    '',
    '[[bin]]',
    'name = "krate-manager"',
    'path = "src/main.rs"',
    '',
    '[features]',
    'dev-feed = []',
    '',
  ].join('\n'),
  'src-tauri/tauri.conf.json':
    JSON.stringify({ productName: 'Krate Manager', version: '0.1.0', identifier: 'com.example.km' }, null, 2) + '\n',
  'src-tauri/tauri.windows.conf.json': JSON.stringify({ bundle: { targets: ['nsis'] } }, null, 2) + '\n',
  'src-tauri/tauri.macos.conf.json': JSON.stringify({ bundle: { targets: ['app'] } }, null, 2) + '\n',
  'src-tauri/tauri.linux.conf.json': JSON.stringify({ bundle: { targets: ['appimage'] } }, null, 2) + '\n',
  'package.json': JSON.stringify({ name: 'krate-manager', private: true, type: 'module' }, null, 2) + '\n',
  'version.json': JSON.stringify({ version: '0.1.0', name: 'Krate Manager' }, null, 2) + '\n',
};

const trees = [];

// overrides: { '<root-relative path>': '<content>' | null }; null deletes the file from the all-equal tree.
function makeTree(overrides) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'km4-vlint-'));
  trees.push(dir);
  const files = { ...BASE, ...overrides };
  for (const [rel, content] of Object.entries(files)) {
    if (content === null) continue;
    const abs = path.join(dir, ...rel.split('/'));
    fs.mkdirSync(path.dirname(abs), { recursive: true });
    fs.writeFileSync(abs, content);
  }
  return dir;
}

function run(argv) {
  let stdout = '';
  let stderr = '';
  const io = {
    stdout: { write: (s) => ((stdout += s), true) },
    stderr: { write: (s) => ((stderr += s), true) },
  };
  const code = lintManagerVersion.main(argv, io);
  const lines = stderr.split('\n').filter((l) => l !== '');
  return { code, stdout, stderr, lines };
}

function lintTree(overrides) {
  return run(['--root', makeTree(overrides)]);
}

function expectFirst(result, exitCode, line) {
  assert.equal(result.code, exitCode, `exit code; stderr ${JSON.stringify(result.stderr)}`);
  assert.equal(result.lines[0], line, `first stderr line; stderr ${JSON.stringify(result.stderr)}`);
}

const edit = (rel, from, to) => {
  const out = BASE[rel].replace(from, to);
  assert.notEqual(out, BASE[rel], `fixture edit of ${rel} did not apply`);
  return out;
};
const json = (obj) => JSON.stringify(obj, null, 2) + '\n';
const versionJson = (version) => JSON.stringify({ version, name: 'Krate Manager' }, null, 2) + '\n';

test('lint-manager-version accepts equal versions and names every drift', async (t) => {
  t.after(() => {
    for (const dir of trees) fs.rmSync(dir, { recursive: true, force: true });
  });

  await t.test('the all-equal tree passes', () => {
    const r = lintTree({});
    assert.equal(r.code, 0, `exit code; stderr ${JSON.stringify(r.stderr)}`);
    assert.equal(r.stderr, '');
  });

  await t.test('the all-equal tree with CRLF line endings passes', () => {
    const crlf = Object.fromEntries(Object.entries(BASE).map(([k, v]) => [k, v.replace(/\n/g, '\r\n')]));
    const r = lintTree(crlf);
    assert.equal(r.code, 0, `exit code; stderr ${JSON.stringify(r.stderr)}`);
    assert.equal(r.stderr, '');
  });

  await t.test('tauri.conf.json version differing', () => {
    const r = lintTree({
      'src-tauri/tauri.conf.json': json({ productName: 'Krate Manager', version: '0.1.1' }),
    });
    expectFirst(r, 1, `${PREFIX}E_VERSION_MISMATCH: src-tauri/tauri.conf.json: version 0.1.1 != version.json 0.1.0`);
  });

  await t.test('root [workspace.package] version differing', () => {
    const r = lintTree({ 'Cargo.toml': edit('Cargo.toml', 'version = "0.1.0"', 'version = "0.1.1"') });
    expectFirst(r, 1, `${PREFIX}E_VERSION_MISMATCH: Cargo.toml: [workspace.package] version 0.1.1 != version.json 0.1.0`);
  });

  await t.test('member crates/core/Cargo.toml with a differing literal version', () => {
    const rel = 'crates/core/Cargo.toml';
    const r = lintTree({ [rel]: edit(rel, 'version = "0.1.0"', 'version = "0.2.0"') });
    expectFirst(r, 1, `${PREFIX}E_VERSION_MISMATCH: ${rel}: [package] version 0.2.0 != version.json 0.1.0`);
  });

  await t.test('member crates/helper/Cargo.toml with a differing literal version', () => {
    const rel = 'crates/helper/Cargo.toml';
    const r = lintTree({ [rel]: edit(rel, 'version.workspace = true', 'version = "0.2.0"') });
    expectFirst(r, 1, `${PREFIX}E_VERSION_MISMATCH: ${rel}: [package] version 0.2.0 != version.json 0.1.0`);
  });

  await t.test('member src-tauri/Cargo.toml with a differing literal version', () => {
    const rel = 'src-tauri/Cargo.toml';
    const r = lintTree({ [rel]: edit(rel, 'version.workspace = true', 'version = "0.2.0"') });
    expectFirst(r, 1, `${PREFIX}E_VERSION_MISMATCH: ${rel}: [package] version 0.2.0 != version.json 0.1.0`);
  });

  await t.test('version.json missing', () => {
    const r = lintTree({ 'version.json': null });
    expectFirst(r, 1, `${PREFIX}E_VERSION_FILE: version.json: missing`);
  });

  for (const bad of ['0.1', 'v0.1.0', '0.1.0-beta.1', '01.0.0']) {
    await t.test(`version.json version ${JSON.stringify(bad)}`, () => {
      const r = lintTree({ 'version.json': versionJson(bad) });
      expectFirst(r, 1, `${PREFIX}E_VERSION_FORMAT: version.json: version ${JSON.stringify(bad)} is not X.Y.Z`);
    });
  }

  await t.test('a version key in tauri.macos.conf.json', () => {
    const r = lintTree({
      'src-tauri/tauri.macos.conf.json': json({ version: '0.1.0', bundle: { targets: ['app'] } }),
    });
    expectFirst(
      r,
      1,
      `${PREFIX}E_VERSION_SOURCE: src-tauri/tauri.macos.conf.json: carries a version key; version.json is the only version source`,
    );
  });

  await t.test('a version key in package.json', () => {
    const r = lintTree({ 'package.json': json({ name: 'krate-manager', private: true, version: '0.1.0' }) });
    expectFirst(
      r,
      1,
      `${PREFIX}E_VERSION_SOURCE: package.json: carries a version key; version.json is the only version source`,
    );
  });

  await t.test('a glob in [workspace] members', () => {
    const r = lintTree({
      'Cargo.toml': edit('Cargo.toml', '  "crates/core", # the literal-version member\n  "crates/helper",', '  "crates/*",'),
    });
    expectFirst(r, 1, `${PREFIX}E_CARGO_TOML: Cargo.toml: [workspace] members entry "crates/*" is a glob; name every member`);
  });

  await t.test('a member with no version line', () => {
    const rel = 'crates/helper/Cargo.toml';
    const r = lintTree({ [rel]: edit(rel, 'version.workspace = true\n', '') });
    expectFirst(
      r,
      1,
      `${PREFIX}E_CARGO_TOML: ${rel}: [package] has no version (need version.workspace = true or version = "X.Y.Z")`,
    );
  });

  await t.test('an inline-table version = { workspace = true }', () => {
    const rel = 'crates/helper/Cargo.toml';
    const r = lintTree({ [rel]: edit(rel, 'version.workspace = true', 'version = { workspace = true }') });
    expectFirst(
      r,
      1,
      `${PREFIX}E_TOML_UNSUPPORTED: ${rel}: line 3: outside the lint's TOML subset in [package]: version = { workspace = true }`,
    );
  });

  await t.test('two independent drifts are both reported', () => {
    const rel = 'crates/core/Cargo.toml';
    const r = lintTree({
      'src-tauri/tauri.conf.json': json({ productName: 'Krate Manager', version: '0.1.1' }),
      [rel]: edit(rel, 'version = "0.1.0"', 'version = "0.2.0"'),
    });
    assert.equal(r.code, 1, `exit code; stderr ${JSON.stringify(r.stderr)}`);
    assert.deepEqual(r.lines, [
      `${PREFIX}E_VERSION_MISMATCH: src-tauri/tauri.conf.json: version 0.1.1 != version.json 0.1.0`,
      `${PREFIX}E_VERSION_MISMATCH: ${rel}: [package] version 0.2.0 != version.json 0.1.0`,
    ]);
  });

  await t.test('an unknown argument is a usage error', () => {
    const r = run(['--bogus']);
    expectFirst(r, 2, `${PREFIX}unknown argument: --bogus`);
    assert.equal(r.lines[1], USAGE);
  });

  await t.test('tauri.conf.json deleted', () => {
    const r = lintTree({ 'src-tauri/tauri.conf.json': null });
    expectFirst(r, 1, `${PREFIX}E_TAURI_CONF: src-tauri/tauri.conf.json: missing`);
  });

  await t.test('tauri.conf.json not JSON', () => {
    const r = lintTree({ 'src-tauri/tauri.conf.json': '{' });
    expectFirst(r, 1, `${PREFIX}E_TAURI_CONF: src-tauri/tauri.conf.json: not JSON`);
  });

  await t.test('tauri.conf.json without version', () => {
    const r = lintTree({ 'src-tauri/tauri.conf.json': json({ productName: 'Krate Manager' }) });
    expectFirst(r, 1, `${PREFIX}E_TAURI_CONF: src-tauri/tauri.conf.json: version missing or not a string`);
  });

  await t.test('crates/helper/Cargo.toml deleted while members names it', () => {
    const r = lintTree({ 'crates/helper/Cargo.toml': null });
    expectFirst(
      r,
      1,
      `${PREFIX}E_CARGO_TOML: crates/helper/Cargo.toml: missing (named in Cargo.toml [workspace] members)`,
    );
  });
});
