'use strict';
require('./helpers/write-guard.js');

// tools/manager-release-preflight.js (spec FR-044, FR-051, FR-063, D15; SC-011; plan §4.9; PD-27; SI-40; tasks T011,
// TA-12). Every case builds a fixture app dir in a fresh temp dir from copies of the real apps/krate-manager files the
// version lint reads (version.json, src-tauri/tauri.conf.json, every Cargo.toml), a test-written CHANGELOG.md and
// crates/core/src/self_update.rs, mutates one thing, and runs main(argv, io) in-process with a capturing io. The real
// app tree and the committed keys are only read.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const preflightTool = require('../../manager-release-preflight.js');
const { parseTomlSubset } = require('../../lint-manager-version.js');

const PREFIX = 'manager-release-preflight: ';
const USAGE_START = 'usage: node tools/manager-release-preflight.js';
const REPO_ROOT = path.resolve(__dirname, '..', '..', '..');
const REAL_APP = path.join(REPO_ROOT, 'apps', 'krate-manager');
const KAT_PUB = path.join(REPO_ROOT, 'tools', 'feed-contract', 'fixtures', 'kat', 'kat.pub');
const EXPECTED_BODY = path.join(__dirname, 'release-body.expected.md');

const V = JSON.parse(fs.readFileSync(path.join(REAL_APP, 'version.json'), 'utf8')).version;
const TAG = `manager/v${V}`;
const REPO_LINE = (repo) => `pub const MANAGER_REPO: &str = "${repo}";`;

// The files lint-manager-version.js reads (tools/lint-manager-version.js:223-338): version.json, tauri.conf.json, the
// root Cargo.toml and each [workspace] member's Cargo.toml, in members order. package.json is optional to the lint.
function lintedFiles() {
  const rootToml = fs.readFileSync(path.join(REAL_APP, 'Cargo.toml'), 'utf8');
  const members = parseTomlSubset(rootToml, 'Cargo.toml', ['workspace']).sections.get('workspace').get('members');
  return ['version.json', 'src-tauri/tauri.conf.json', 'Cargo.toml', ...members.map((m) => `${m}/Cargo.toml`)];
}

function changelog(headings) {
  const sections = headings.map((h, i) => `${h}\n\n### Added\n\n- Entry ${i + 1}.\n`);
  const intro = ['# Changelog', '', 'All notable changes to Krate Manager will be documented in this file.', ''];
  return [...intro, ...sections].join('\n');
}

const DEFAULT_TAGS = ['refs/tags/membrum/v0.12.0', 'refs/tags/manager/v0.0.1'];
function tagsText({ tags = DEFAULT_TAGS, releases = ['membrum/v0.12.0'] } = {}) {
  return ['# tags', ...tags, '# releases', ...releases, ''].join('\n');
}

const tmpDirs = [];
test.after(() => {
  for (const d of tmpDirs) fs.rmSync(d, { recursive: true, force: true });
});

// Builds a fixture: { dir, app, tags, out(name) }. `mutate(app)` runs after the copies are written.
function fixture({
  changelogText = changelog([`## [${V}] - 2026-11-01`]),
  selfUpdate = `//! fixture\n${REPO_LINE('o/r')}\n`,
  tags = tagsText(),
  mutate,
} = {}) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'km11-preflight-'));
  tmpDirs.push(dir);
  const app = path.join(dir, 'app');
  for (const rel of lintedFiles()) {
    const dst = path.join(app, ...rel.split('/'));
    fs.mkdirSync(path.dirname(dst), { recursive: true });
    fs.writeFileSync(dst, fs.readFileSync(path.join(REAL_APP, ...rel.split('/'))));
  }
  fs.writeFileSync(path.join(app, 'CHANGELOG.md'), changelogText);
  if (selfUpdate !== null) {
    const su = path.join(app, 'crates', 'core', 'src', 'self_update.rs');
    fs.mkdirSync(path.dirname(su), { recursive: true });
    fs.writeFileSync(su, selfUpdate);
  }
  const tagsFile = path.join(dir, 'tags.txt');
  fs.writeFileSync(tagsFile, tags);
  if (mutate) mutate(app);
  return { dir, app, tags: tagsFile, out: (name) => path.join(dir, name) };
}

function editJson(file, edit) {
  const json = JSON.parse(fs.readFileSync(file, 'utf8'));
  edit(json);
  fs.writeFileSync(file, JSON.stringify(json, null, 2) + '\n');
}

const tauriConf = (app) => path.join(app, 'src-tauri', 'tauri.conf.json');

function run(argv) {
  let stdout = '';
  let stderr = '';
  const io = {
    stdout: { write: (s) => (stdout += s) },
    stderr: { write: (s) => (stderr += s) },
  };
  const code = preflightTool.main(argv, io);
  return { code, stdout, stderr };
}

// The full argv of a run over fixture f, with every output requested.
function argvOf(f, { dryRun = 'false', repo = 'o/r', extra = [] } = {}) {
  return [
    '--app-dir', f.app,
    '--dry-run', dryRun,
    '--tags', f.tags,
    '--repo', repo,
    '--notes', f.out('notes.md'),
    '--body', f.out('body.md'),
    '--github-output', f.out('gh-output.txt'),
    ...extra,
  ];
}

function assertRefused(f, r, code) {
  assert.equal(r.code, 1, `exit code; stderr: ${r.stderr}`);
  const first = r.stderr.split('\n')[0];
  assert.ok(first.startsWith(`${PREFIX}${code}: `), `first stderr line: ${first}`);
  for (const name of ['notes.md', 'body.md', 'gh-output.txt']) {
    assert.equal(fs.existsSync(f.out(name)), false, `${name} written on a refusal`);
  }
}

// Runs fixture f once per kind of run and expects `code` from both.
function refusedOnBoth(f, code, opts = {}) {
  for (const dryRun of ['true', 'false']) {
    assertRefused(f, run(argvOf(f, { ...opts, dryRun })), code);
  }
}

test('manager release preflight refuses by name', async (t) => {
  await t.test('version drift in tauri.conf.json -> E_PREFLIGHT_VERSION', () => {
    const f = fixture({ mutate: (app) => editJson(tauriConf(app), (j) => (j.version = '9.9.9')) });
    assertRefused(f, run(argvOf(f)), 'E_PREFLIGHT_VERSION');
  });

  await t.test('plugins.updater drift, each of the five keys -> E_PREFLIGHT_UPDATER_CONFIG', () => {
    const drifts = {
      pubkey: (u) => (u.pubkey = Buffer.from('untrusted comment: x\nRWQ=\n').toString('base64')),
      endpoints: (u) => (u.endpoints = ['https://example.com/latest.json']),
      requireSignedVersion: (u) => (u.requireSignedVersion = false),
      allowDowngrades: (u) => (u.allowDowngrades = true),
      windows: (u) => (u.windows = { installMode: 'basicUi' }),
    };
    for (const [key, drift] of Object.entries(drifts)) {
      const f = fixture({ mutate: (app) => editJson(tauriConf(app), (j) => drift(j.plugins.updater)) });
      const r = run(argvOf(f));
      assertRefused(f, r, 'E_PREFLIGHT_UPDATER_CONFIG');
      assert.ok(r.stderr.includes(`plugins.updater.${key}`), `${key}: ${r.stderr}`);
    }
    const extra = fixture({
      mutate: (app) => editJson(tauriConf(app), (j) => (j.plugins.updater.dangerousInsecureTransportProtocol = true)),
    });
    assertRefused(extra, run(argvOf(extra)), 'E_PREFLIGHT_UPDATER_CONFIG');
    const none = fixture({ mutate: (app) => editJson(tauriConf(app), (j) => delete j.plugins) });
    assertRefused(none, run(argvOf(none)), 'E_PREFLIGHT_UPDATER_CONFIG');
  });

  await t.test('no CHANGELOG heading, or two -> E_PREFLIGHT_CHANGELOG', () => {
    const missing = fixture({ changelogText: changelog(['## [0.0.1] - 2026-10-01']) });
    assertRefused(missing, run(argvOf(missing)), 'E_PREFLIGHT_CHANGELOG');
    const twice = fixture({ changelogText: changelog([`## [${V}] - 2026-11-01`, `## [${V}] - 2026-10-01`]) });
    assertRefused(twice, run(argvOf(twice)), 'E_PREFLIGHT_CHANGELOG');
    const neither = fixture({ changelogText: changelog([`## [${V}] - soon`]) });
    assertRefused(neither, run(argvOf(neither)), 'E_PREFLIGHT_CHANGELOG');
  });

  await t.test('an Unreleased heading: a real run refuses, a dry run warns', () => {
    const f = fixture({ changelogText: changelog([`## [${V}] - Unreleased`]) });
    assertRefused(f, run(argvOf(f, { dryRun: 'false' })), 'E_PREFLIGHT_CHANGELOG_UNDATED');
    const r = run(argvOf(f, { dryRun: 'true' }));
    assert.equal(r.code, 0, r.stderr);
    assert.ok(r.stdout.split('\n').some((l) => l.startsWith('::warning ')), r.stdout);
  });

  await t.test('an existing tag or release -> E_PREFLIGHT_TAG_EXISTS on both kinds of run', () => {
    const tagged = fixture({ tags: tagsText({ tags: [`refs/tags/${TAG}`] }) });
    refusedOnBoth(tagged, 'E_PREFLIGHT_TAG_EXISTS');
    const released = fixture({ tags: tagsText({ releases: [TAG] }) });
    refusedOnBoth(released, 'E_PREFLIGHT_TAG_EXISTS');
  });

  await t.test('a release list of 1000 lines -> E_PREFLIGHT_TAG_LIST on both kinds of run', () => {
    const names = (n) => Array.from({ length: n }, (_, i) => `x/v0.0.${i}`);
    const full = fixture({ tags: tagsText({ releases: names(1000) }) });
    refusedOnBoth(full, 'E_PREFLIGHT_TAG_LIST');
    const below = fixture({ tags: tagsText({ releases: names(999) }) });
    assert.equal(run(argvOf(below)).code, 0);
    const malformed = fixture({ tags: 'refs/tags/a\n' });
    refusedOnBoth(malformed, 'E_PREFLIGHT_TAG_LIST');
  });

  await t.test('--repo against MANAGER_REPO -> E_PREFLIGHT_REPO on both kinds of run', () => {
    refusedOnBoth(fixture(), 'E_PREFLIGHT_REPO', { repo: 'attacker/x' });
    refusedOnBoth(fixture({ selfUpdate: '//! no repo line\n' }), 'E_PREFLIGHT_REPO');
    refusedOnBoth(fixture({ selfUpdate: `${REPO_LINE('o/r')}\n${REPO_LINE('o/r')}\n` }), 'E_PREFLIGHT_REPO');
    refusedOnBoth(fixture({ selfUpdate: null }), 'E_PREFLIGHT_REPO');
    // Checked before E_PREFLIGHT_TAG_EXISTS.
    const tagged = fixture({ tags: tagsText({ tags: [`refs/tags/${TAG}`] }) });
    refusedOnBoth(tagged, 'E_PREFLIGHT_REPO', { repo: 'attacker/x' });
  });

  await t.test('managerRepo reads the one MANAGER_REPO line', () => {
    const real = fs.readFileSync(path.join(REAL_APP, 'crates', 'core', 'src', 'self_update.rs'), 'utf8');
    assert.equal(preflightTool.managerRepo(real), 'rolandzwaga/krate-audio');
    assert.equal(preflightTool.managerRepo(`a\r\n${REPO_LINE('o/r')}\r\nb\r\n`), 'o/r');
    assert.throws(() => preflightTool.managerRepo('// nothing\n'));
    assert.throws(() => preflightTool.managerRepo(`${REPO_LINE('a/b')}\n${REPO_LINE('c/d')}\n`));
  });

  await t.test('a test key as --pubkey on a real run -> E_SIGNING_CONFIG (TA-12)', () => {
    const katText = fs.readFileSync(KAT_PUB, 'utf8');
    const kat = fixture({
      mutate: (app) =>
        editJson(tauriConf(app), (j) => (j.plugins.updater.pubkey = Buffer.from(katText, 'utf8').toString('base64'))),
    });
    const r = run(argvOf(kat, { dryRun: 'false', extra: ['--pubkey', KAT_PUB] }));
    assertRefused(kat, r, 'E_SIGNING_CONFIG');
    assert.ok(r.stderr.includes('public key is a test key (tools/feed-contract/fixtures/kat/kat.pub)'), r.stderr);
    // The rule is real-run only: the same fixture passes a dry run.
    assert.equal(run(argvOf(kat, { dryRun: 'true', extra: ['--pubkey', KAT_PUB] })).code, 0);
  });

  await t.test('the valid fixture passes both kinds of run and writes notes, body and outputs', () => {
    const older = [`## [${V}] - 2026-11-01`, '## [0.0.2] - 2026-10-15', '## [0.0.1] - 2026-10-01'];
    for (const dryRun of ['true', 'false']) {
      const f = fixture({ changelogText: changelog(older) });
      const r = run(argvOf(f, { dryRun }));
      assert.equal(r.code, 0, r.stderr);
      assert.equal(r.stderr, '');
      assert.ok(!r.stdout.includes('::warning'), r.stdout);
      // FR-044: only the newest section.
      const notes = fs.readFileSync(f.out('notes.md'), 'utf8');
      assert.equal(notes, '### Added\n\n- Entry 1.');
      const body = fs.readFileSync(f.out('body.md'), 'utf8');
      assert.equal(body, preflightTool.releaseBody({ version: V, repo: 'o/r', notes }));
      assert.equal(fs.readFileSync(f.out('gh-output.txt'), 'utf8'), `version=${V}\ntag=${TAG}\n`);
    }
  });

  await t.test('changelogSection stops at the next heading and trims blank lines', () => {
    const text = `# C\r\n\r\n## [1.0.0] - 2026-11-01\r\n\r\n\r\nA\r\n\r\nB\r\n\r\n## [0.9.0] - 2026-10-01\r\nold\r\n`;
    assert.equal(preflightTool.changelogSection(text, '1.0.0'), 'A\n\nB');
    assert.equal(preflightTool.changelogSection(text, '0.9.0'), 'old');
  });

  await t.test('usage errors exit 2 with the usage line', () => {
    const f = fixture();
    const cases = [
      argvOf(f).filter((a, i, all) => a !== '--repo' && all[i - 1] !== '--repo'),
      argvOf(f, { dryRun: 'yes' }),
      argvOf(f, { repo: 'not a repo' }),
      [...argvOf(f), '--unknown'],
      [...argvOf(f), '--repo', 'o/r'],
      ['--app-dir'],
    ];
    for (const argv of cases) {
      const r = run(argv);
      assert.equal(r.code, 2, `${argv.join(' ')}: ${r.stderr}`);
      assert.ok(r.stderr.includes(USAGE_START), r.stderr);
    }
  });
});

test('manager release body is exact', () => {
  const notes = '### Added\n\n- Krate Manager updates itself.';
  const expected = fs.readFileSync(EXPECTED_BODY, 'utf8').replace(/\r\n/g, '\n');
  const body = preflightTool.releaseBody({ version: '0.1.0', repo: 'o/r', notes });
  assert.equal(body, expected);
  assert.ok(
    body.includes(
      'curl -fLO https://github.com/o/r/releases/download/manager/v0.1.0/Krate-Manager-0.1.0-macOS.pkg && ' +
        'sudo installer -pkg Krate-Manager-0.1.0-macOS.pkg -target /',
    ),
  );
  assert.ok(body.includes('(provisional: see the Krate Manager first-launch notes when they are published)'));
});
