'use strict';
require('./helpers/write-guard.js');

// gen-feed v2 mode, clean and prerelease cases (spec SC-002, SC-003, SC-011, SC-012; FR-041 to FR-045, FR-047; plan
// §4.5.2, §4.5.4, §4.7). Every case directory under fixtures/gen-feed/ (inputs built by fixtures/make-fixtures.js, goldens
// written once by gen-feed.js and reviewed by hand, fixtures/README.md) is read in place; outputs go to fs.mkdtemp dirs.
//
// - `v1 projection of v2 equals legacy` (SC-002): where no prerelease and no draft exists, what a v1 reader sees of the v2
//   feed is today's feed, byte for byte.
// - `Q9 prerelease never moves the v1 version` (SC-003): the SC-003 table, row by row, plus SemVer 2.0.0 precedence.
// - `release_notes is always empty` (SC-011).
// - `gen-feed is deterministic` (SC-012): repeat runs, CRLF inputs, and the committed goldens.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { FIXTURES, loadCase, runMain, v2Argv } = require('./helpers/fixtures.js');
const { buildFeed, buildLegacyFeed, projectV1, readPluginsDir } = require('../../gen-feed.js');
const { compareSemVer } = require('../contract.js');

const GEN_FEED_DIR = path.join(FIXTURES, 'gen-feed');

/** Every case directory (one holding case.json), bytewise by name. */
function allCases() {
  return fs
    .readdirSync(GEN_FEED_DIR, { withFileTypes: true })
    .filter((d) => d.isDirectory() && fs.existsSync(path.join(GEN_FEED_DIR, d.name, 'case.json')))
    .map((d) => d.name)
    .sort((a, b) => Buffer.compare(Buffer.from(a, 'utf8'), Buffer.from(b, 'utf8')))
    .map((name) => ({ name, info: loadCase(name) }));
}

const CASES = allCases();
const casesOf = (group) => CASES.filter((c) => c.info.caseJson.group === group);

/** The pure-core inputs of a case, read the way main reads them (manifests/*.json bytewise, labelled manifests/<f>). */
function caseInputs(info) {
  const manifests =
    info.manifestsDir === null
      ? []
      : fs
          .readdirSync(info.manifestsDir)
          .filter((n) => n.endsWith('.json'))
          .sort((a, b) => Buffer.compare(Buffer.from(a, 'utf8'), Buffer.from(b, 'utf8')))
          .map((n) => ({ file: `manifests/${n}`, text: fs.readFileSync(path.join(info.manifestsDir, n), 'utf8') }));
  const inputs = {
    plugins: readPluginsDir(info.pluginsDir, { withSetupIss: true }),
    releases: JSON.parse(fs.readFileSync(info.releasesPath, 'utf8')),
    manifests,
    repo: info.caseJson.repo,
  };
  if (info.caseJson.managerLatestUrl !== undefined) inputs.managerLatestUrl = info.caseJson.managerLatestUrl;
  return inputs;
}

/** Legacy output for the case's release tag names (drafts included), the SC-002 / SC-003 comparison input. */
function legacyFor(inputs) {
  return buildLegacyFeed({ plugins: inputs.plugins, tagsText: inputs.releases.map((r) => r.tagName).join('\n') + '\n' });
}

/** A temp dir for the duration of fn; removed afterwards. */
function withTmp(prefix, fn) {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  try {
    return fn(tmp);
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
}

/** Runs main in v2 mode on a case into <tmp>/<outName>; returns {code, stdout, stderr, bytes}. */
function runV2(info, tmp, outName) {
  const empty = path.join(tmp, 'empty-manifests');
  fs.mkdirSync(empty, { recursive: true });
  const out = path.join(tmp, outName);
  const r = runMain(v2Argv(info, out, empty));
  return { ...r, bytes: fs.existsSync(out) ? fs.readFileSync(out) : null };
}

function runLegacy(info, tmp, outName) {
  const out = path.join(tmp, outName);
  const r = runMain(['--legacy', '--plugins-dir', info.pluginsDir, '--tags', info.tagsPath, '--out', out]);
  return { ...r, bytes: fs.existsSync(out) ? fs.readFileSync(out) : null };
}

function firstDifference(actual, expected) {
  const a = String(actual).split('\n');
  const e = String(expected).split('\n');
  for (let i = 0; i < Math.max(a.length, e.length); ++i) {
    if (a[i] !== e[i]) return `line ${i + 1}: got ${JSON.stringify(a[i])}, expected ${JSON.stringify(e[i])}`;
  }
  return 'same lines, different bytes';
}

test('v1 projection of v2 equals legacy', async (t) => {
  const clean = casesOf('clean');
  await t.test('the clean group has at least 8 cases, the snapshot among them', () => {
    assert.ok(clean.length >= 8, `only ${clean.length} clean cases`);
    assert.ok(clean.some((c) => c.name === 'snapshot-2026-10-29'), 'the snapshot is not a clean case');
  });
  for (const { name, info } of clean) {
    await t.test(name, () => {
      const inputs = caseInputs(info);
      const { doc, warnings } = buildFeed(inputs);
      assert.deepEqual(warnings, [], `${name}: a clean case degrades nothing`);
      const projection = projectV1(doc);
      const legacy = legacyFor(inputs);
      assert.equal(projection, legacy, `${name}: ${firstDifference(projection, legacy)}`);
      assert.ok(Object.keys(doc.plugins).length > 0, `${name}: no plugin in the feed`);
    });
  }
});

// spec SC-003, as tabled: v2 version (null: plugin absent), the sibling tag (null: no sibling), the legacy pick, and
// whether the v1 projection equals legacy. The subject plugin of every case is Alpha.
const SC003 = Object.freeze({
  'pre-newer': { version: '1.1.0', sibling: 'alpha/v1.2.0-beta.1', legacy: '1.2.0-beta.1', projection: 'differ' },
  'pre-same-core': { version: '1.1.0', sibling: null, legacy: '1.1.0-beta.1', projection: 'differ' },
  'pre-lower-core': { version: '1.1.0', sibling: null, legacy: '1.1.0', projection: 'equal' },
  'pre-only': { version: null, sibling: null, legacy: '1.0.0-beta.2', projection: 'differ' },
  'draft-above': { version: '1.1.0', sibling: null, legacy: '1.2.0', projection: 'differ' },
  'pre-plain': { version: '1.1.0', sibling: 'alpha/v1.2.0', legacy: '1.2.0', projection: 'differ' },
});

test('Q9 prerelease never moves the v1 version', async (t) => {
  const pre = casesOf('prerelease');
  await t.test('the prerelease group is exactly the SC-003 rows', () => {
    assert.deepEqual(pre.map((c) => c.name).sort(), Object.keys(SC003).sort());
  });
  for (const { name, info } of pre) {
    await t.test(name, () => {
      const row = SC003[name];
      assert.ok(row, `${name} is not an SC-003 row`);
      assert.deepEqual(info.caseJson.legacyExpect, { plugin: 'Alpha', version: row.legacy }, `${name}: case.json legacyExpect`);
      const inputs = caseInputs(info);
      const { doc, warnings } = buildFeed(inputs);
      assert.deepEqual(warnings, [], `${name}: nothing degrades`);
      if (row.version === null) {
        assert.ok(!Object.hasOwn(doc.plugins, 'Alpha'), `${name}: a plugin with no stable release is omitted`);
      } else {
        const entry = doc.plugins.Alpha;
        assert.ok(entry, `${name}: Alpha missing`);
        assert.equal(entry.version, row.version, `${name}: v2 version`);
        assert.equal(entry.tag, `alpha/v${row.version}`, `${name}: entry tag`);
        assert.equal(entry.channel, 'stable', `${name}: entry channel`);
        if (row.sibling === null) {
          assert.ok(!Object.hasOwn(entry, 'prerelease'), `${name}: no sibling expected`);
        } else {
          assert.ok(entry.prerelease, `${name}: sibling missing`);
          assert.equal(entry.prerelease.tag, row.sibling, `${name}: sibling tag`);
          assert.equal(entry.prerelease.channel, 'beta', `${name}: sibling channel`);
          assert.ok(!Object.hasOwn(entry.prerelease, 'version'), `${name}: the sibling carries no v1 key`);
        }
      }
      const legacy = legacyFor(inputs);
      assert.equal(JSON.parse(legacy).plugins.Alpha?.version, row.legacy, `${name}: legacy pick`);
      const projection = projectV1(doc);
      if (row.projection === 'equal') assert.equal(projection, legacy, `${name}: ${firstDifference(projection, legacy)}`);
      else assert.notEqual(projection, legacy, `${name}: the projection should differ from legacy`);
    });
  }
  await t.test('SemVer 2.0.0 precedence', () => {
    // https://semver.org/spec/v2.0.0.html §11, the example chain; then multi-digit cores.
    for (const chain of [
      ['1.0.0-alpha', '1.0.0-alpha.1', '1.0.0-alpha.beta', '1.0.0-beta', '1.0.0-beta.2', '1.0.0-beta.11', '1.0.0-rc.1', '1.0.0'],
      ['1.9.0', '1.10.0', '2.0.0'],
    ]) {
      for (let i = 0; i + 1 < chain.length; ++i) {
        assert.equal(compareSemVer(chain[i], chain[i + 1]), -1, `${chain[i]} < ${chain[i + 1]}`);
        assert.equal(compareSemVer(chain[i + 1], chain[i]), 1, `${chain[i + 1]} > ${chain[i]}`);
      }
      for (const v of chain) assert.equal(compareSemVer(v, v), 0, `${v} = ${v}`);
    }
  });
});

test('release_notes is always empty', async (t) => {
  const cases = CASES.filter((c) => c.info.caseJson.group !== 'reject');
  assert.ok(cases.length > 0);
  for (const { name, info } of cases) {
    await t.test(name, () => {
      const docs = [];
      const modes = info.caseJson.modes;
      if (modes.includes('v2')) {
        const { doc, text } = buildFeed(caseInputs(info));
        docs.push(['v2', doc], ['v2 text', JSON.parse(text)], ['projection', JSON.parse(projectV1(doc))]);
      }
      if (modes.includes('legacy')) {
        const legacy = buildLegacyFeed({
          plugins: readPluginsDir(info.pluginsDir, { withSetupIss: false }),
          tagsText: fs.readFileSync(info.tagsPath, 'utf8'),
        });
        docs.push(['legacy', JSON.parse(legacy)]);
      }
      assert.ok(docs.length > 0, `${name}: no mode ran`);
      for (const [label, doc] of docs) {
        for (const [plugin, entry] of Object.entries(doc.plugins)) {
          assert.equal(entry.release_notes, '', `${name} ${label}: plugins.${plugin}.release_notes`);
        }
      }
    });
  }
});

test('gen-feed is deterministic', async (t) => {
  const cases = [...casesOf('clean'), ...casesOf('prerelease')];
  for (const { name, info } of cases) {
    await t.test(name, () =>
      withTmp('km1-v2-det-', (tmp) => {
        const a = runV2(info, tmp, 'a.json');
        const b = runV2(info, tmp, 'b.json');
        assert.equal(a.code, info.caseJson.exit, `${name}: exit (${a.stderr})`);
        assert.equal(a.stderr, '', `${name}: stderr`);
        assert.ok(a.bytes && b.bytes && a.bytes.equals(b.bytes), `${name}: two runs differ`);
        const goldenV2 = fs.readFileSync(path.join(info.dir, 'expected.v2.json'));
        assert.ok(a.bytes.equals(goldenV2), `${name} expected.v2.json: ${firstDifference(a.bytes, goldenV2)}`);
        const legacy = runLegacy(info, tmp, 'legacy.json');
        assert.equal(legacy.code, 0, `${name}: legacy exit (${legacy.stderr})`);
        const goldenLegacy = fs.readFileSync(path.join(info.dir, 'expected.legacy.json'));
        assert.ok(legacy.bytes.equals(goldenLegacy), `${name} expected.legacy.json: ${firstDifference(legacy.bytes, goldenLegacy)}`);
        assert.deepEqual(fs.readdirSync(tmp).sort(), ['a.json', 'b.json', 'empty-manifests', 'legacy.json'], `${name}: a temp file is left`);
      }),
    );
  }
  await t.test('CRLF CHANGELOG.md and version.json give the same bytes (clean-changelog)', () =>
    withTmp('km1-v2-crlf-', (tmp) => {
      const info = loadCase('clean-changelog');
      const copy = path.join(tmp, 'case');
      fs.cpSync(info.dir, copy, { recursive: true });
      let converted = 0;
      for (const dir of fs.readdirSync(path.join(copy, 'plugins'))) {
        for (const file of ['CHANGELOG.md', 'version.json']) {
          const p = path.join(copy, 'plugins', dir, file);
          if (!fs.existsSync(p)) continue;
          const lf = fs.readFileSync(p, 'utf8');
          assert.ok(!lf.includes('\r'), `${dir}/${file} is not LF in the fixture`);
          fs.writeFileSync(p, lf.replace(/\n/g, '\r\n'));
          converted += 1;
        }
      }
      assert.ok(converted >= 3, `only ${converted} files converted`);
      const crlfInfo = {
        ...info,
        dir: copy,
        pluginsDir: path.join(copy, 'plugins'),
        releasesPath: path.join(copy, 'releases.json'),
        tagsPath: path.join(copy, 'tags.txt'),
        manifestsDir: info.manifestsDir === null ? null : path.join(copy, 'manifests'),
      };
      const lf = runV2(info, tmp, 'lf.json');
      const crlf = runV2(crlfInfo, tmp, 'crlf.json');
      assert.equal(lf.code, 0, lf.stderr);
      assert.equal(crlf.code, 0, crlf.stderr);
      assert.ok(lf.bytes.equals(crlf.bytes), firstDifference(crlf.bytes, lf.bytes));
      assert.ok(JSON.parse(lf.bytes.toString('utf8')).plugins.Alpha.notes_markdown, 'clean-changelog: Alpha has no notes');
    }),
  );
});
