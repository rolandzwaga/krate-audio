'use strict';
require('./helpers/write-guard.js');

// The Phase 3 fixtures: the one-v2 / eight-v1 feed, the C2 manifest behind it and the compare case triples (spec
// specs/krate-manager-phase3/spec.md FR-044, FR-045, SC-007 fixtures, SC-008; plan §4.7, §7.1 T8, PD-14, PD-15).
// fixtures/phase3/make-phase3.js builds them in memory; make-fixtures.js (set `phase3`) writes and checks them. The
// plugin behind the manifest is found the way the builder finds it (the one snapshot version.json with preset_subdir),
// so this file names no plugin. Repo fixtures are only read; every output lands in an fs.mkdtemp dir.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const contract = require('../contract.js');
const { FIXTURES, readJson, runMain } = require('./helpers/fixtures.js');

const REPO = 'rolandzwaga/krate-audio';
const SNAPSHOT = path.join(FIXTURES, 'gen-feed', 'snapshot-2026-10-29');
const SNAPSHOT_PLUGINS = path.join(SNAPSHOT, 'plugins');
const MIXED_REL = 'feeds/valid/one-v2-eight-v1.json';
const CASES_REL = 'phase3/compare/cases.json';
const ROW_FIELDS = ['case', 'dryRun', 'exit', 'stdout', 'stderr', 'annotations'];

const fixturePath = (rel) => path.join(FIXTURES, ...rel.split('/'));

function withTemp(t, name) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `km3-${name}-`));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  return dir;
}

/** The snapshot version.json copies, read from the files (never spelled here). */
function snapshotVersionJsons() {
  return fs
    .readdirSync(SNAPSHOT_PLUGINS)
    .filter((d) => fs.existsSync(path.join(SNAPSHOT_PLUGINS, d, 'version.json')))
    .map((d) => JSON.parse(fs.readFileSync(path.join(SNAPSHOT_PLUGINS, d, 'version.json'), 'utf8')));
}

/** The one snapshot plugin whose version.json carries preset_subdir, and its manifest's path under fixtures/. */
function presetPlugin() {
  const withPresets = snapshotVersionJsons().filter((v) => Object.hasOwn(v, 'preset_subdir'));
  assert.equal(withPresets.length, 1, 'exactly one snapshot version.json carries preset_subdir');
  const [v] = withPresets;
  return { name: v.name, version: v.version, manifestRel: `phase3/${v.name.toLowerCase()}-v${v.version}.json` };
}

let built = null;
/** make-phase3.js files(), built once per process with make-fixtures.js's own fixtureSignature. */
function builtFiles() {
  if (built === null) {
    const { fixtureSignature } = require('../fixtures/make-fixtures.js');
    built = require('../fixtures/phase3/make-phase3.js').files({ fixtureSignature });
  }
  return built;
}

test('Phase 3 mixed feed fixture is real-shaped', async (t) => {
  await t.test('(1) files() returns exactly the manifest, the mixed feed, cases.json and every case triple', () => {
    const files = builtFiles();
    const rows = JSON.parse(files[CASES_REL]);
    assert.ok(Array.isArray(rows) && rows.length > 0, 'cases.json is a non-empty array');
    const expected = [presetPlugin().manifestRel, MIXED_REL, CASES_REL];
    for (const row of rows) {
      for (const f of ['live', 'feed', 'releases']) expected.push(`phase3/compare/${row.case}/${f}.json`);
    }
    assert.deepEqual(Object.keys(files).sort(), expected.sort());
    for (const [rel, content] of Object.entries(files)) {
      assert.equal(typeof content, 'string', `${rel} is text`);
      assert.ok(!content.includes('\r'), `${rel} has LF line ends only`);
    }
  });

  await t.test('(2) the committed manifest equals the built text and passes checkReleaseManifest', () => {
    const { manifestRel } = presetPlugin();
    const committed = fs.readFileSync(fixturePath(manifestRel));
    assert.ok(committed.equals(Buffer.from(builtFiles()[manifestRel], 'utf8')), `${manifestRel} differs from the builder`);
    assert.doesNotThrow(() => contract.checkReleaseManifest(JSON.parse(committed.toString('utf8')), { repo: REPO }));
  });

  await t.test('(3) gen-feed on the snapshot and the manifest reproduces the mixed feed byte for byte', (s) => {
    const { manifestRel } = presetPlugin();
    const tmp = withTemp(s, 'phase3-genfeed');
    const manifests = path.join(tmp, 'manifests');
    fs.mkdirSync(manifests);
    fs.copyFileSync(fixturePath(manifestRel), path.join(manifests, path.basename(manifestRel)));
    const out = path.join(tmp, 'versions.json');
    const run = runMain([
      '--plugins-dir',
      SNAPSHOT_PLUGINS,
      '--releases',
      path.join(SNAPSHOT, 'releases.json'),
      '--manifests',
      manifests,
      '--repo',
      REPO,
      '--out',
      out,
    ]);
    assert.equal(run.code, 0, run.stderr);
    assert.equal(run.stderr, '');
    assert.ok(fs.readFileSync(out).equals(fs.readFileSync(fixturePath(MIXED_REL))), `${MIXED_REL} differs from gen-feed`);
  });

  await t.test('(4) the mixed feed is v2 and v1 valid; only the preset_subdir plugin has platforms and install', () => {
    const doc = JSON.parse(builtFiles()[MIXED_REL]);
    for (const schema of ['v2', 'v1']) {
      const result = contract.validateFeed(doc, { schema });
      assert.equal(result.valid, true, `${schema}: ${JSON.stringify(result.errors)}`);
    }
    const names = Object.keys(doc.plugins);
    assert.equal(names.length, 9, 'nine plugins');
    assert.deepEqual([...names].sort(), snapshotVersionJsons().map((v) => v.name).sort());
    const withPlatforms = names.filter((n) => Object.hasOwn(doc.plugins[n], 'platforms'));
    const withInstall = names.filter((n) => Object.hasOwn(doc.plugins[n], 'install'));
    assert.deepEqual(withPlatforms, [presetPlugin().name]);
    assert.deepEqual(withInstall, [presetPlugin().name]);
  });

  await t.test('(5) the v1 projection of the mixed feed is the snapshot live-versions.json byte for byte', () => {
    const run = runMain(['--project-v1', fixturePath(MIXED_REL)]);
    assert.equal(run.code, 0, run.stderr);
    assert.ok(Buffer.from(run.stdout, 'utf8').equals(fs.readFileSync(path.join(SNAPSHOT, 'live-versions.json'))));
  });

  await t.test('(6) feeds/cases.json holds the Phase3/one-v2-eight-v1 row', () => {
    const rows = readJson('feeds/cases.json').filter((r) => r.rule === 'Phase3/one-v2-eight-v1');
    assert.deepEqual(rows, [{ rule: 'Phase3/one-v2-eight-v1', file: 'valid/one-v2-eight-v1.json', schema: 'v2', expect: 'accept' }]);
  });

  await t.test('(7) make-fixtures.js check() passes', () => {
    const { ok, diff } = require('../fixtures/make-fixtures.js').check();
    assert.equal(ok, true, diff);
  });

  await t.test('(8) every compare case row is complete and its feed passes v2 (except feed-fails-v2)', () => {
    const files = builtFiles();
    const rows = JSON.parse(files[CASES_REL]);
    const seen = new Set();
    for (const row of rows) {
      assert.deepEqual(Object.keys(row), ROW_FIELDS, `row ${JSON.stringify(row.case)} fields`);
      assert.ok(/^[a-z0-9-]+$/u.test(row.case), `case name ${JSON.stringify(row.case)}`);
      assert.ok(!seen.has(row.case), `case ${row.case} appears twice`);
      seen.add(row.case);
      assert.equal(typeof row.dryRun, 'boolean', `${row.case} dryRun`);
      assert.ok([0, 1, 3].includes(row.exit), `${row.case} exit ${row.exit}`);
      for (const f of ['stdout', 'stderr', 'annotations']) {
        assert.ok(Array.isArray(row[f]) && row[f].every((l) => typeof l === 'string'), `${row.case} ${f}`);
      }
      const feed = JSON.parse(files[`phase3/compare/${row.case}/feed.json`]);
      const v2 = contract.validateFeed(feed, { schema: 'v2' });
      if (row.case === 'feed-fails-v2') assert.equal(v2.valid, false, 'feed-fails-v2 must fail v2');
      else assert.equal(v2.valid, true, `${row.case} feed: ${JSON.stringify(v2.errors)}`);
    }
    assert.ok(seen.has('feed-fails-v2'), 'the feed-fails-v2 case exists');
  });
});
