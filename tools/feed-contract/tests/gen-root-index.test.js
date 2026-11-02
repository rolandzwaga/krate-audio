'use strict';
require('./helpers/write-guard.js');

// tools/gen-root-index.js (Krate Manager Phase 12: spec FR-081, D13; SC-029; plan §4.8, PD-16; SI-14; tasks TA-10 to
// TA-12). The landing page's root index, ported from the inline Python of release.yml's "Generate Root Index" step.
// Every case runs main(argv, io, deps) in process over injected deps: gitTags returns the fixture's tag lists in
// `git tag -l <pattern> --sort=-v:refname` order, repo() the REPO value, and the file functions read the fixture tree
// tools/feed-contract/fixtures/landing/ as the repository root (its index-template.html and manager-card.fixture.html
// stand in for docs/index-template.html and docs/manager-card-template.html, TA-12). writeFile only records.
// root-index.expected.html is the unchanged Python's output over the same tree and tags
// (specs/krate-manager-phase12/evidence/root-index-python.txt).
// Nothing here writes a file, spawns a process or opens a network connection.
// One top-level test (the feed-tools TAP plan counts top-level tests only); every other check is a subtest.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const REPO_ROOT = path.resolve(__dirname, '..', '..', '..');
const TOOL = path.join(REPO_ROOT, 'tools', 'gen-root-index.js');
const LANDING = path.resolve(__dirname, '..', 'fixtures', 'landing');
const EXPECTED = fs.readFileSync(path.join(LANDING, 'root-index.expected.html'), 'utf8');
const CARD_TEMPLATE = fs.readFileSync(path.join(LANDING, 'manager-card.fixture.html'), 'utf8');
const USAGE = 'usage: node tools/gen-root-index.js --out <file> [--manager-version <x.y.z>]';

// The fixture tags (evidence/root-index-python.txt), per pattern, in --sort=-v:refname order.
const PLUGIN_TAGS = Object.freeze({
  'alpha/v*': ['alpha/v1.10.0', 'alpha/v1.2.0'],
  'beta/v*': ['beta/v0.3.0'],
  'gamma/v*': [],
  'delta/v*': ['delta/v2.0.0'],
  'delta-x/v*': ['delta-x/v0.1.0'],
});
const MANAGER_TAGS = Object.freeze(['manager/v0.10.0', 'manager/v0.10.0-rc.1', 'manager/v0.9.0']);

// Repository-relative paths the tool reads -> the fixture file standing in for them.
const STAND_INS = Object.freeze({
  'docs/index-template.html': 'index-template.html',
  'docs/manager-card-template.html': 'manager-card.fixture.html',
});

function loadTool() {
  return require(TOOL);
}

function relOf(p) {
  const rel = path.relative(LANDING, p).split(path.sep).join('/');
  assert.ok(!rel.startsWith('..') && !path.isAbsolute(rel), `the tool reached outside its root: ${p}`);
  return rel;
}

function enoent(p) {
  return Object.assign(new Error(`ENOENT: no such file or directory, open '${p}'`), { code: 'ENOENT' });
}

/** Fake deps over the fixture tree. `files` overrides a repository-relative file: a string, or null for missing. */
function fixtureDeps(opts = {}) {
  const { managerTags = [], files = {} } = opts;
  // Not a destructuring default: `repo: undefined` must reach the tool as undefined.
  const repo = Object.hasOwn(opts, 'repo') ? opts.repo : 'o/r';
  const calls = [];
  const writes = [];
  const deps = {
    root: LANDING,
    gitTags(pattern) {
      calls.push(`gitTags ${pattern}`);
      if (pattern === 'manager/v*') return [...managerTags];
      if (!Object.hasOwn(PLUGIN_TAGS, pattern)) throw new Error(`unexpected tag pattern ${pattern}`);
      return [...PLUGIN_TAGS[pattern]];
    },
    repo() {
      calls.push('repo');
      return repo;
    },
    readFile(p) {
      const rel = relOf(p);
      calls.push(`readFile ${rel}`);
      if (Object.hasOwn(files, rel)) {
        if (files[rel] === null) throw enoent(p);
        return files[rel];
      }
      const file = Object.hasOwn(STAND_INS, rel) ? STAND_INS[rel] : rel;
      return fs.readFileSync(path.join(LANDING, ...file.split('/')), 'utf8');
    },
    readDir(p) {
      calls.push(`readDir ${relOf(p)}`);
      return fs.readdirSync(p);
    },
    isFile(p) {
      const rel = relOf(p);
      calls.push(`isFile ${rel}`);
      if (Object.hasOwn(files, rel)) return files[rel] !== null;
      const st = fs.statSync(p, { throwIfNoEntry: false });
      return st !== undefined && st.isFile();
    },
    writeFile(p, text) {
      calls.push(`writeFile ${p}`);
      writes.push({ path: p, text });
    },
  };
  return { deps, calls, writes };
}

function capture() {
  let stdout = '';
  let stderr = '';
  const io = {
    stdout: { write: (s) => ((stdout += s), true) },
    stderr: { write: (s) => ((stderr += s), true) },
  };
  return { io, out: () => stdout, err: () => stderr };
}

/** Runs main with the given argv over fixtureDeps(opts); returns the exit code, the captured streams and the deps log. */
function run(argv, opts) {
  const { main } = loadTool();
  const { deps, calls, writes } = fixtureDeps(opts);
  const { io, out, err } = capture();
  const code = main(argv, io, deps);
  return { code, stdout: out(), stderr: err(), calls, writes };
}

function renderedCard(version) {
  return loadTool().renderManagerCard({ version, template: CARD_TEMPLATE, repo: 'o/r' });
}

function installerUrls(version) {
  return [
    `Krate-Manager-${version}-Windows-x64-setup.exe`,
    `Krate-Manager-${version}-macOS.pkg`,
    `Krate-Manager-${version}-Linux-x86_64.AppImage`,
    `Krate-Manager-${version}-Linux-amd64.deb`,
  ].map((asset) => `https://github.com/o/r/releases/download/manager/v${version}/${asset}`);
}

function assertRefused(r, code) {
  assert.equal(r.code, 1, `exit code; stderr ${JSON.stringify(r.stderr)}`);
  assert.match(r.stderr, new RegExp(`^gen-root-index: ${code}: `, 'mu'), `stderr ${JSON.stringify(r.stderr)}`);
  assert.deepEqual(r.writes, [], 'a refused run wrote a file');
}

test('root index renders the plugin cards and the manager card', async (t) => {
  await t.test('no manager/v* tag: the bytes equal the Python output', () => {
    const r = run(['--out', 'site/index.html']);
    assert.equal(r.code, 0, `exit code; stderr ${JSON.stringify(r.stderr)}`);
    assert.equal(r.writes.length, 1);
    assert.equal(r.writes[0].path, path.resolve(LANDING, 'site/index.html'));
    assert.equal(r.writes[0].text, EXPECTED);
    assert.equal(r.writes[0].text.includes('manager-card'), false);
    assert.ok(r.calls.includes('gitTags manager/v*'), 'the CLI reads the manager tags');
    assert.equal(r.calls.includes('readFile docs/manager-card-template.html'), false, 'no version: no card template');
  });

  await t.test('plugin order is the Python path sort (TA-11) and untagged plugins are not read', () => {
    const { deps, calls } = fixtureDeps();
    const plugins = loadTool().collectPlugins(deps, LANDING);
    assert.deepEqual(
      plugins.map((p) => [p.dir, p.version, p.hasPage]),
      [
        ['alpha', '1.10.0', true],
        ['beta', '0.3.0', false],
        ['delta-x', '0.1.0', false],
        ['delta', '2.0.0', false],
      ],
    );
    assert.equal(calls.includes('readFile plugins/gamma/version.json'), false, 'an untagged version.json was read');
    assert.ok(EXPECTED.indexOf('<h2>Delta X</h2>') < EXPECTED.indexOf('<h2>Delta</h2>'), 'the fixture pins the order');
  });

  await t.test('the template is read with universal newlines', () => {
    const crlf = fs.readFileSync(path.join(LANDING, 'index-template.html'), 'utf8').replace(/\n/gu, '\r\n');
    const r = run(['--out', 'index.html'], { files: { 'docs/index-template.html': crlf } });
    assert.equal(r.code, 0, r.stderr);
    assert.equal(r.writes[0].text, EXPECTED);
  });

  await t.test('newest manager tag by version: 0.10.0, one card, and the card plus its LF removed is the plain page', () => {
    const { newestManagerVersion } = loadTool();
    assert.equal(newestManagerVersion([...MANAGER_TAGS]), '0.10.0');
    assert.equal(newestManagerVersion(['manager/v0.9.0', 'manager/v0.10.0-rc.1', 'manager/v0.10.0']), '0.10.0');
    assert.equal(newestManagerVersion(['manager/v0.10.0-rc.1', 'manager/v1.2', 'alpha/v1.0.0']), null);
    assert.equal(newestManagerVersion([]), null);
    const r = run(['--out', 'index.html'], { managerTags: MANAGER_TAGS });
    assert.equal(r.code, 0, r.stderr);
    const html = r.writes[0].text;
    assert.equal(html.split('manager-card').length - 1, 1, 'exactly one manager-card');
    const card = renderedCard('0.10.0');
    assert.ok(card.length > 0 && !card.endsWith('\n'), 'the rendered card has no trailing newline');
    assert.ok(html.includes(`${card}\n`), 'the card is in the page');
    assert.equal(html.replace(`${card}\n`, ''), EXPECTED);
    assert.ok(html.indexOf(card) < html.indexOf('<h2>Alpha</h2>'), 'the card is prepended to the plugin cards');
  });

  await t.test('--manager-version 0.2.0 overrides the tags', () => {
    const r = run(['--out', 'index.html', '--manager-version', '0.2.0'], { managerTags: MANAGER_TAGS });
    assert.equal(r.code, 0, r.stderr);
    assert.equal(r.calls.includes('gitTags manager/v*'), false, 'the tags were read despite --manager-version');
    const html = r.writes[0].text;
    assert.equal(html.replace(`${renderedCard('0.2.0')}\n`, ''), EXPECTED);
    assert.equal(html.includes('0.10.0'), false);
  });

  await t.test("the card links the four per-tag installers and the README, {{REPO}} filled", () => {
    const card = renderedCard('0.2.0');
    const hrefs = [...card.matchAll(/href="([^"]*)"/gu)].map((m) => m[1]);
    assert.deepEqual(
      hrefs.filter((h) => h.includes('/releases/download/')),
      installerUrls('0.2.0'),
    );
    assert.ok(hrefs.includes('https://github.com/o/r/blob/main/apps/krate-manager/README.md#first-launch'));
    for (const bad of ['{{REPO}}', '{{MANAGER_VERSION}}', '.app.tar.gz', '/releases/latest/']) {
      assert.equal(card.includes(bad), false, `the card holds ${bad}`);
    }
    assert.ok(card.includes('<span class="version">v0.2.0</span>'));
    assert.ok(card.includes('($&amp; stays)'), 'no $ pattern expansion');
    const crlf = loadTool().renderManagerCard({
      version: '0.2.0',
      template: CARD_TEMPLATE.replace(/\n/gu, '\r\n'),
      repo: 'o/r',
    });
    assert.equal(crlf, card, 'a CRLF card template renders the same bytes');
  });

  await t.test('renderManagerCard with a null version is empty, and renderRootIndex then equals the plain page', () => {
    const { renderManagerCard, renderRootIndex, renderPluginCards, collectPlugins } = loadTool();
    assert.equal(renderManagerCard({ version: null, template: CARD_TEMPLATE, repo: 'o/r' }), '');
    const { deps } = fixtureDeps();
    const html = renderRootIndex({
      pluginCards: renderPluginCards(collectPlugins(deps, LANDING)),
      managerCard: '',
      template: fs.readFileSync(path.join(LANDING, 'index-template.html'), 'utf8'),
      repo: 'o/r',
    });
    assert.equal(html, EXPECTED);
  });

  await t.test('a missing or empty REPO is E_ROOT_INDEX_REPO before anything is read', () => {
    for (const repo of [undefined, '']) {
      const r = run(['--out', 'index.html'], { repo, managerTags: MANAGER_TAGS });
      assertRefused(r, 'E_ROOT_INDEX_REPO');
      assert.deepEqual(r.calls, ['repo'], `repo ${JSON.stringify(repo)}: deps used`);
    }
  });

  await t.test('a version.json without string name or description is E_ROOT_INDEX_VERSION_JSON', () => {
    for (const text of ['{"description":"x"}\n', '{"name":3,"description":"x"}\n', '{"name":"Alpha"}\n', '{"name":', '[]']) {
      const r = run(['--out', 'index.html'], { files: { 'plugins/alpha/version.json': text } });
      assertRefused(r, 'E_ROOT_INDEX_VERSION_JSON');
      assert.match(r.stderr, /plugins\/alpha\/version\.json/u);
    }
  });

  await t.test('--manager-version not x.y.z is E_ROOT_INDEX_VERSION', () => {
    for (const v of ['1.2', 'v1.2.3', '1.2.3-rc.1', '01.2.3', '']) {
      const r = run(['--out', 'index.html', '--manager-version', v]);
      assertRefused(r, 'E_ROOT_INDEX_VERSION');
    }
  });

  await t.test('a missing template or card template is E_ROOT_INDEX_TEMPLATE', () => {
    assertRefused(run(['--out', 'index.html'], { files: { 'docs/index-template.html': null } }), 'E_ROOT_INDEX_TEMPLATE');
    assertRefused(
      run(['--out', 'index.html', '--manager-version', '0.2.0'], { files: { 'docs/manager-card-template.html': null } }),
      'E_ROOT_INDEX_TEMPLATE',
    );
  });

  await t.test('an unknown flag or a malformed command line is exit 2 with the usage line', () => {
    const argvs = [
      ['--bogus'],
      [],
      ['--out'],
      ['--out', 'a.html', '--out', 'b.html'],
      ['--manager-version', '0.2.0'],
      ['--out', 'a.html', 'extra'],
    ];
    for (const argv of argvs) {
      const r = run(argv);
      assert.equal(r.code, 2, `${JSON.stringify(argv)}: exit code`);
      assert.ok(r.stderr.split('\n').includes(USAGE), `${JSON.stringify(argv)}: stderr ${JSON.stringify(r.stderr)}`);
      assert.deepEqual(r.calls, [], `${JSON.stringify(argv)}: deps used`);
    }
    assert.throws(() => loadTool().main(['--out', 'a.html'], capture().io), TypeError, 'deps is required');
  });

  await t.test('the live docs/index-template.html equals the fixture copy (TA-12)', () => {
    const live = fs.readFileSync(path.join(REPO_ROOT, 'docs', 'index-template.html'), 'utf8').replace(/\r\n/gu, '\n');
    assert.equal(live, fs.readFileSync(path.join(LANDING, 'index-template.html'), 'utf8'));
  });

  await t.test('the tool is an LF shebang file exporting the plan §4.8 surface', () => {
    const bytes = fs.readFileSync(TOOL);
    assert.equal(bytes.includes(0x0d), false, 'the tool holds a CR byte');
    assert.equal(bytes.toString('utf8').split('\n')[0], '#!/usr/bin/env node');
    const mod = loadTool();
    for (const name of [
      'newestManagerVersion',
      'renderManagerCard',
      'renderPluginCards',
      'renderRootIndex',
      'collectPlugins',
      'main',
      'productionDeps',
    ]) {
      assert.equal(typeof mod[name], 'function', `exports ${name}`);
    }
  });
});
