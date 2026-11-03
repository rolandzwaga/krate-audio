'use strict';
require('./helpers/write-guard.js');

// tools/bump-manager-version.js (Krate Manager Phase 12 spec FR-089, SC-036).
// One top-level test (the feed-tools TAP plan counts top-level tests only); every check is a subtest. Each case builds
// a fresh temp tree that lint-manager-version passes at 1.0.0, runs main(['<version>', '--root', dir], io) with a
// capturing io, and asserts the exit code, the output line and every file's bytes. Nothing here reads or writes the
// real apps/krate-manager tree.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const bumpManagerVersion = require('../../bump-manager-version.js');

const TOOL = path.join(__dirname, '..', '..', 'bump-manager-version.js');
const PREFIX = 'bump-manager-version: ';
const USAGE = 'usage: node tools/bump-manager-version.js <x.y.z> [--root <dir>]';
const crlf = (lines) => lines.join('\r\n');
const lf = (lines) => lines.join('\n');

// The 1.0.0 tree: CRLF and LF files mixed, a third-party crate at 1.0.0 in every lock, a fixture lock holding two of
// the three workspace crates and one holding none.
const BASE = {
  'version.json': crlf(['{', '  "version": "1.0.0",', '  "name": "Krate Manager"', '}', '']),
  'package.json': lf(['{', '  "name": "krate-manager",', '  "private": true', '}', '']),
  'Cargo.toml': crlf([
    '[workspace]',
    'members = ["crates/core", "crates/helper", "src-tauri"]',
    'resolver = "3"',
    '',
    '[workspace.package]',
    'version = "1.0.0"',
    'edition = "2024"',
    '',
    '[workspace.dependencies]',
    'serde = { version = "1.0.0" }',
    '',
  ]),
  'crates/core/Cargo.toml': lf(['[package]', 'name = "krate-manager-core"', 'version.workspace = true', '']),
  'crates/helper/Cargo.toml': lf(['[package]', 'name = "krate-manager-helper"', 'version.workspace = true', '']),
  'src-tauri/Cargo.toml': lf(['[package]', 'name = "krate-manager"', 'version.workspace = true', '']),
  'src-tauri/tauri.conf.json': crlf(['{', '  "productName": "Krate Manager",', '  "version": "1.0.0"', '}', '']),
  'Cargo.lock': crlf([
    'version = 4',
    '',
    '[[package]]',
    'name = "krate-manager"',
    'version = "1.0.0"',
    '',
    '[[package]]',
    'name = "krate-manager-core"',
    'version = "1.0.0"',
    '',
    '[[package]]',
    'name = "krate-manager-helper"',
    'version = "1.0.0"',
    '',
    '[[package]]',
    'name = "vswhom"',
    'version = "1.0.0"',
    '',
  ]),
  'tests/fixtures/harness/Cargo.lock': lf([
    '[[package]]',
    'name = "krate-manager"',
    'version = "1.0.0"',
    '',
    '[[package]]',
    'name = "krate-manager-core"',
    'version = "1.0.0"',
    '',
    '[[package]]',
    'name = "vswhom"',
    'version = "1.0.0"',
    '',
  ]),
  'tests/fixtures/other/Cargo.lock': lf(['[[package]]', 'name = "vswhom"', 'version = "1.0.0"', '']),
  'CHANGELOG.md': crlf(['# Changelog', '', '## [1.0.0] - Unreleased', '', '### Added', '', '- A thing.', '']),
};

const trees = [];

// overrides: { '<root-relative path>': '<content>' | null }; null deletes the file from the base tree.
function makeTree(overrides = {}) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'km12-bump-'));
  trees.push(dir);
  for (const [rel, content] of Object.entries({ ...BASE, ...overrides })) {
    if (content === null) continue;
    const abs = path.join(dir, ...rel.split('/'));
    fs.mkdirSync(path.dirname(abs), { recursive: true });
    fs.writeFileSync(abs, content);
  }
  return dir;
}

function snapshot(dir) {
  const out = {};
  const walk = (sub) => {
    for (const d of fs.readdirSync(path.join(dir, sub), { withFileTypes: true })) {
      const rel = sub ? `${sub}/${d.name}` : d.name;
      if (d.isDirectory()) walk(rel);
      else out[rel] = fs.readFileSync(path.join(dir, rel), 'utf8');
    }
  };
  walk('');
  return out;
}

function run(argv) {
  let stdout = '';
  let stderr = '';
  const io = {
    stdout: { write: (s) => ((stdout += s), true) },
    stderr: { write: (s) => ((stderr += s), true) },
  };
  const code = bumpManagerVersion.main(argv, io);
  return { code, stdout, stderr, lines: stderr.split('\n').filter((l) => l !== '') };
}

const swap = (rel, from, to) => {
  const out = BASE[rel].replace(from, to);
  assert.notEqual(out, BASE[rel], `fixture edit of ${rel} did not apply`);
  return { [rel]: out };
};

test('bump-manager-version writes every copy or nothing', async (t) => {
  t.after(() => {
    for (const dir of trees) fs.rmSync(dir, { recursive: true, force: true });
  });

  await t.test('a bump writes every copy, keeps line endings and leaves third-party crates alone', () => {
    const dir = makeTree();
    const r = run(['1.1.0', '--root', dir]);
    assert.equal(r.code, 0, r.stderr);
    assert.equal(r.stderr, '');
    assert.equal(
      r.stdout,
      `${PREFIX}1.0.0 -> 1.1.0: version.json, Cargo.toml, src-tauri/tauri.conf.json, Cargo.lock, ` +
        'tests/fixtures/harness/Cargo.lock, CHANGELOG.md\n',
    );
    const want = {
      ...BASE,
      'version.json': BASE['version.json'].replace('"version": "1.0.0"', '"version": "1.1.0"'),
      'Cargo.toml': BASE['Cargo.toml'].replace('\r\nversion = "1.0.0"\r\n', '\r\nversion = "1.1.0"\r\n'),
      'src-tauri/tauri.conf.json': BASE['src-tauri/tauri.conf.json'].replace('"1.0.0"', '"1.1.0"'),
      'Cargo.lock': BASE['Cargo.lock']
        .replace('"krate-manager"\r\nversion = "1.0.0"', '"krate-manager"\r\nversion = "1.1.0"')
        .replace('"krate-manager-core"\r\nversion = "1.0.0"', '"krate-manager-core"\r\nversion = "1.1.0"')
        .replace('"krate-manager-helper"\r\nversion = "1.0.0"', '"krate-manager-helper"\r\nversion = "1.1.0"'),
      'tests/fixtures/harness/Cargo.lock': BASE['tests/fixtures/harness/Cargo.lock']
        .replace('"krate-manager"\nversion = "1.0.0"', '"krate-manager"\nversion = "1.1.0"')
        .replace('"krate-manager-core"\nversion = "1.0.0"', '"krate-manager-core"\nversion = "1.1.0"'),
      'CHANGELOG.md': BASE['CHANGELOG.md'].replace('## [1.0.0] - Unreleased', '## [1.1.0] - Unreleased'),
    };
    assert.deepEqual(snapshot(dir), want);
    // The serde 1.0.0 dependency line and the vswhom 1.0.0 entries are untouched: the expected texts keep them.
    assert.ok(want['Cargo.toml'].includes('serde = { version = "1.0.0" }'));
    assert.ok(want['Cargo.lock'].includes('"vswhom"\r\nversion = "1.0.0"'));
  });

  await t.test('a released newest entry gets a new unreleased heading above it', () => {
    const dir = makeTree(swap('CHANGELOG.md', '## [1.0.0] - Unreleased', '## [1.0.0] - 2026-11-04'));
    const r = run(['1.0.1', '--root', dir]);
    assert.equal(r.code, 0, r.stderr);
    assert.equal(
      fs.readFileSync(path.join(dir, 'CHANGELOG.md'), 'utf8'),
      crlf(['# Changelog', '', '## [1.0.1] - Unreleased', '', '## [1.0.0] - 2026-11-04', '', '### Added', '', '- A thing.', '']),
    );
  });

  await t.test('every refusal names its code and writes nothing', () => {
    const cases = [
      ['same version', '1.0.0', {}, 1, 'E_BUMP_VERSION: 1.0.0 is not newer than the current 1.0.0'],
      ['an older version', '0.9.9', {}, 1, 'E_BUMP_VERSION: 0.9.9 is not newer than the current 1.0.0'],
      ['not X.Y.Z', '1.1', {}, 1, 'E_BUMP_VERSION: "1.1" is not X.Y.Z'],
      ['a leading zero', '1.01.0', {}, 1, 'E_BUMP_VERSION: "1.01.0" is not X.Y.Z'],
      [
        'a drifted copy',
        '1.1.0',
        swap('src-tauri/tauri.conf.json', '"1.0.0"', '"0.9.0"'),
        1,
        'E_BUMP_LINT: lint-manager-version fails before the bump: E_VERSION_MISMATCH: src-tauri/tauri.conf.json: version 0.9.0 != version.json 1.0.0',
      ],
      [
        'a member with its own version',
        '1.1.0',
        swap('crates/helper/Cargo.toml', 'version.workspace = true', 'version = "1.0.0"'),
        1,
        'E_BUMP_ANCHOR: crates/helper/Cargo.toml: [package] version is not version.workspace = true',
      ],
      [
        'the root lock missing a crate',
        '1.1.0',
        swap('Cargo.lock', 'name = "krate-manager-helper"', 'name = "krate-manager-other"'),
        1,
        'E_BUMP_ANCHOR: Cargo.lock: no krate-manager-helper 1.0.0 entry',
      ],
      [
        'a fixture lock at another version',
        '1.1.0',
        swap('tests/fixtures/harness/Cargo.lock', '"krate-manager-core"\nversion = "1.0.0"', '"krate-manager-core"\nversion = "0.9.0"'),
        1,
        'E_BUMP_ANCHOR: tests/fixtures/harness/Cargo.lock: krate-manager-core is locked 1 time(s), 0 of them at 1.0.0',
      ],
      [
        'the new entry already in the CHANGELOG',
        '1.1.0',
        swap('CHANGELOG.md', '# Changelog\r\n', '# Changelog\r\n\r\n## [1.1.0] - 2026-01-01\r\n'),
        1,
        'E_BUMP_ANCHOR: CHANGELOG.md: already has a ## [1.1.0] entry',
      ],
      [
        'no CHANGELOG heading',
        '1.1.0',
        swap('CHANGELOG.md', '## [1.0.0] - Unreleased', 'Nothing yet.'),
        1,
        'E_BUMP_ANCHOR: CHANGELOG.md: no "## [" version heading',
      ],
    ];
    for (const [name, version, overrides, code, line] of cases) {
      const dir = makeTree(overrides);
      const before = snapshot(dir);
      const r = run([version, '--root', dir]);
      assert.equal(r.code, code, `${name}: exit code; stderr ${JSON.stringify(r.stderr)}`);
      assert.deepEqual(r.lines, [PREFIX + line], name);
      assert.equal(r.stdout, '', name);
      assert.deepEqual(snapshot(dir), before, `${name}: a file changed`);
    }
  });

  await t.test('usage errors exit 2 with the usage line', () => {
    const dir = makeTree();
    for (const [argv, why] of [
      [[], 'missing the new version'],
      [['--root', dir], 'missing the new version'],
      [['1.1.0', '--root'], 'missing value for --root'],
      [['1.1.0', '1.2.0'], 'unknown argument: 1.2.0'],
      [['--dry-run'], 'unknown argument: --dry-run'],
    ]) {
      const r = run(argv);
      assert.equal(r.code, 2, JSON.stringify(argv));
      assert.deepEqual(r.lines, [PREFIX + why, USAGE], JSON.stringify(argv));
    }
    assert.deepEqual(snapshot(dir), BASE);
  });

  await t.test('the tool is an LF shebang file exporting bump and main', () => {
    const src = fs.readFileSync(TOOL, 'utf8');
    assert.ok(src.startsWith('#!/usr/bin/env node\n'), 'shebang line');
    assert.equal(src.includes('\r'), false, 'LF only');
    assert.deepEqual(Object.keys(bumpManagerVersion).sort(), ['bump', 'main']);
  });
});
