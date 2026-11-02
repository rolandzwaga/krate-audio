'use strict';
require('./helpers/write-guard.js');

// The whole feed pipeline of tools/feed-publish.js, collect then generate (then compare), end to end through the fake gh
// (spec specs/krate-manager-phase3/spec.md SC-001, SC-002, SC-005, FR-008, FR-009; plan §7.1 T1, T2, T5).
// T1 runs it on the 2026-10-29 snapshot world and proves the first v2 deploy is, to a v1 reader, byte for byte today's
// live feed. T2 runs it twice on one Phase 2 world and proves the outputs byte-identical, then makes gen-feed's second
// run differ and proves generate refuses it. T5 proves a selected release without release.json keeps exactly gen-feed's
// v1-only entry, with no warning. Plugin names come from version.json files, never from this file. Every expected gh argv
// list is built before the run it is compared with. Repo fixtures are only read; every output lands in an fs.mkdtemp dir.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const feedPublish = require('../../feed-publish.js');
const genFeed = require('../../gen-feed.js');
const fake = require('./helpers/fake-gh.js');

const SNAPSHOT = path.join(__dirname, '..', 'fixtures', 'gen-feed', 'snapshot-2026-10-29');
const SNAPSHOT_PLUGINS = path.join(SNAPSHOT, 'plugins');
const SNAPSHOT_REPO = 'rolandzwaga/krate-audio';
const LIVE = path.join(SNAPSHOT, 'live-versions.json');
const LIST_JSON = 'tagName,isPrerelease,isDraft,publishedAt';
const MANAGER_TAG = 'manager/v0.2.0';
const PUBLISHED = '2026-03-01T12:00:00Z';
// The fixed annotation titles of plan §4.1.6: the only lines the tool may start with "::".
const ANNOTATION_RE =
  /^::(notice|warning|error) title=(feed-collect|feed-generate|feed-degrade|feed-validate|feed-compare|feed-drift|feed-verdict)::/u;

const bytewise = (a, b) => Buffer.compare(Buffer.from(a, 'utf8'), Buffer.from(b, 'utf8'));
const readText = (file) => fs.readFileSync(file, 'utf8');
const readJson = (file) => JSON.parse(readText(file));

function withTemp(t, name) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `km3-${name}-`));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  return dir;
}

// --- running the tool

const linesOf = (text) => (text === '' ? [] : text.replace(/\n$/u, '').split('\n'));

/** main with captured output; every line obeys the output rule (PD-18 (c), FR-029e). */
function run(argv, deps) {
  let stdout = '';
  let stderr = '';
  const io = {
    stdout: { write: (s) => ((stdout += s), true) },
    stderr: { write: (s) => ((stderr += s), true) },
  };
  const code = feedPublish.main(argv, io, deps);
  const out = linesOf(stdout);
  const err = linesOf(stderr);
  for (const l of out) assert.ok(!l.startsWith('::') || ANNOTATION_RE.test(l), `stdout line starts "::": ${l}`);
  for (const l of err) assert.ok(!l.startsWith('::'), `stderr line starts "::": ${l}`);
  return { code, out, err };
}

const collectArgv = ({ pluginsDir, repo, inDir }) => [
  'collect',
  '--plugins-dir',
  pluginsDir,
  '--repo',
  repo,
  '--out-dir',
  inDir,
];
const generateArgv = ({ pluginsDir, repo, inDir, siteDir }) => [
  'generate',
  '--in-dir',
  inDir,
  '--plugins-dir',
  pluginsDir,
  '--repo',
  repo,
  '--site-dir',
  siteDir,
];

const listArgv = (repo) => ['release', 'list', '--repo', repo, '--limit', '1000', '--json', LIST_JSON];
const viewArgv = (tag, repo) => ['release', 'view', tag, '--repo', repo, '--json', 'tagName,assets'];
const ghArgvs = (world) => world.calls.filter((c) => c.cmd === 'gh').map((c) => c.argv);
const listRows = (state) =>
  state.releases.map(({ tagName, isPrerelease, isDraft, publishedAt }) => ({ tagName, isPrerelease, isDraft, publishedAt }));

/** The plugin names of a plugins dir, bytewise, read as collect reads them (never spelled here). */
const namesOf = (pluginsDir) =>
  genFeed
    .readPluginsDir(pluginsDir, { withSetupIss: false })
    .map((p) => p.versionJson.name)
    .sort(bytewise);

/** The version.json of plugin `name` under `pluginsDir`. */
const versionJsonOf = (pluginsDir, name) =>
  genFeed.readPluginsDir(pluginsDir, { withSetupIss: false }).find((p) => p.versionJson.name === name).versionJson;

/** A copy of the Phase 2 plugins with the https `url` gen-feed's v2 mode needs (it is the v1 download_url). */
function pluginsWithUrl(tmp) {
  const dir = path.join(tmp, 'plugins');
  fs.cpSync(fake.PLUGINS_DIR, dir, { recursive: true });
  for (const d of fs.readdirSync(dir)) {
    const file = path.join(dir, d, 'version.json');
    const doc = readJson(file);
    doc.url = `https://krateaudio.com/${doc.name.toLowerCase()}/`;
    fs.writeFileSync(file, JSON.stringify(doc, null, 2) + '\n');
  }
  return dir;
}

/** A valid Tauri static latest.json for the manager `version` (FR-025). */
function latestJson(version, repo) {
  const doc = {
    version,
    notes: '',
    pub_date: PUBLISHED,
    platforms: {
      'windows-x86_64': {
        signature: 'dW50cnVzdGVkIGNvbW1lbnQ6IGZpeHR1cmUK',
        url: `https://github.com/${repo}/releases/download/manager/v${version}/manager_${version}_x64-setup.exe`,
      },
    },
  };
  return JSON.stringify(doc, null, 2) + '\n';
}

/** The entries of a flat directory (collect's manifests/), bytewise, with their bytes. */
function filesUnder(dir) {
  return fs
    .readdirSync(dir)
    .sort(bytewise)
    .map((rel) => ({ rel, bytes: fs.readFileSync(path.join(dir, rel)) }));
}

// --- T1 (SC-001)

test('feed pipeline reproduces the live v1 feed from the 2026-10-29 snapshot', (t) => {
  const tmp = withTemp(t, 'snapshot');
  const world = fake.makeWorld({ tmp, world: 'snapshot' });
  const deps = { gh: world.deps.gh, genFeedMain: genFeed.main };
  const snapshotRows = readJson(path.join(SNAPSHOT, 'releases.json'));
  const names = namesOf(SNAPSHOT_PLUGINS);
  assert.equal(names.length, 9, 'the snapshot holds nine plugins');

  // The expected selection and the exact argv list, from the snapshot rows, before the run.
  const stables = names.map((name) => {
    const sel = genFeed.selectReleases(name, snapshotRows);
    assert.notEqual(sel, null, `${name} has a stable release in the snapshot`);
    assert.equal(sel.sibling, null, `${name} has no prerelease sibling in the snapshot`);
    return sel.stable;
  });
  const expectedCalls = [listArgv(SNAPSHOT_REPO), ...stables.map((s) => viewArgv(s.tagName, SNAPSHOT_REPO))];

  // collect
  const inDir = path.join(tmp, 'in');
  const w = { pluginsDir: SNAPSHOT_PLUGINS, repo: SNAPSHOT_REPO, inDir };
  const c = run(collectArgv(w), deps);
  assert.equal(c.code, 0, c.err.join('\n'));
  assert.deepEqual(readJson(path.join(inDir, 'releases.json')), snapshotRows, 'releases.json = the snapshot rows');
  assert.ok(fs.statSync(path.join(inDir, 'manifests')).isDirectory(), 'manifests/ exists');
  assert.deepEqual(fs.readdirSync(path.join(inDir, 'manifests')), [], 'manifests/ is empty');
  assert.equal(fs.existsSync(path.join(inDir, 'manager')), false, 'no manager/');
  const report = readJson(path.join(inDir, 'collect-report.json'));
  assert.deepEqual(
    report.plugins,
    names.map((name, i) => ({
      name,
      stable: stables[i].tagName,
      sibling: null,
      manifests: [{ tag: stables[i].tagName, status: 'no-manifest' }],
    })),
    'nine no-manifest plugins',
  );
  assert.deepEqual(report.manager, { status: 'none' });
  assert.deepStrictEqual(ghArgvs(world), expectedCalls, '1 list + 9 views, bytewise by plugin name');
  assert.equal(ghArgvs(world).filter((a) => a[1] === 'download').length, 0, 'no download');
  assert.equal(report.gh_calls, 10);

  // generate into a site dir that does not exist yet
  const siteDir = path.join(tmp, 'site');
  assert.equal(fs.existsSync(siteDir), false);
  const g = run(generateArgv({ ...w, siteDir }), deps);
  assert.equal(g.code, 0, g.err.join('\n'));
  assert.ok(fs.statSync(siteDir).isDirectory(), 'generate created the site dir');
  const feedFile = path.join(siteDir, 'versions.json');
  const projected = Buffer.from(genFeed.projectV1(readJson(feedFile)), 'utf8');
  assert.ok(projected.equals(fs.readFileSync(LIVE)), 'projectV1(versions.json) is byte-identical to live-versions.json');

  // compare against the live feed, as a dry run
  const releasesFile = path.join(inDir, 'releases.json');
  const cmp = run(['compare', '--live', LIVE, '--feed', feedFile, '--releases', releasesFile, '--dry-run', 'true'], deps);
  assert.equal(cmp.code, 0, cmp.err.join('\n'));
  assert.deepEqual(cmp.out, [
    ...names.map((name, i) => `same ${name} ${stables[i].version}`),
    'compare: 9 same, 0 newer, 0 added, 0 q9-corrected, 0 drift (0 block, 0 warn)',
  ]);
  assert.deepEqual(cmp.err, []);
});

// --- T2 (SC-002, FR-009)

// Phase 2 files on selected stables and siblings of three plugins and on one release no selection picks.
const DETERMINISM_TAGS = [
  'alphaplug/v1.2.0',
  'alphaplug/v1.3.0-beta.1',
  'betaplug/v2.0.1',
  'gammaplug/v0.12.0',
  'gammaplug/v0.13.0-alpha.2',
  'deltaplug/v1.9.3',
];

test('feed pipeline is deterministic', async (t) => {
  const tmp = withTemp(t, 'determinism');
  const pluginsDir = pluginsWithUrl(tmp);
  const signer = fake.throwawaySigner();
  const world = fake.makeWorld({
    tmp,
    mutate: (state, ctx) => {
      const assetsTmp = path.join(tmp, 'assets');
      for (const tag of DETERMINISM_TAGS) fake.addPhase2Files(state, ctx, tag, { tmp: assetsTmp, signer, pluginsDir });
      fake.addManagerRelease(state, ctx, {
        tag: MANAGER_TAG,
        publishedAt: PUBLISHED,
        latestJson: latestJson('0.2.0', state.repo),
      });
    },
  });
  const repo = world.state.repo;

  /** One full collect + generate run into <tmp>/<label>/{in,site}; `genFeedMain` defaults to gen-feed's main. */
  function pipeline(label, genFeedMain = genFeed.main) {
    const base = path.join(tmp, label);
    const w = { pluginsDir, repo, inDir: path.join(base, 'in'), siteDir: path.join(base, 'site') };
    const c = run(collectArgv(w), { gh: world.deps.gh, genFeedMain: genFeed.main });
    assert.equal(c.code, 0, c.err.join('\n'));
    const g = run(generateArgv(w), { gh: world.deps.gh, genFeedMain });
    return { ...w, g };
  }

  await t.test('two runs give byte-identical outputs', () => {
    const a = pipeline('run-a');
    const b = pipeline('run-b');
    for (const r of [a, b]) {
      assert.equal(r.g.code, 0, r.g.err.join('\n'));
      assert.deepEqual(readJson(path.join(r.inDir, 'collect-report.json')).manager, { status: 'copied', tag: MANAGER_TAG });
      assert.ok(fs.existsSync(path.join(r.siteDir, 'manager', 'latest.json')), 'the run staged manager/latest.json');
    }
    const bytes = (dir, ...rel) => fs.readFileSync(path.join(dir, ...rel));
    assert.ok(bytes(a.inDir, 'releases.json').equals(bytes(b.inDir, 'releases.json')), 'releases.json');
    const ma = filesUnder(path.join(a.inDir, 'manifests'));
    const mb = filesUnder(path.join(b.inDir, 'manifests'));
    assert.ok(ma.length >= 5, `the selected releases' manifests were downloaded: ${ma.map((m) => m.rel).join(', ')}`);
    assert.deepEqual(
      mb.map((m) => m.rel),
      ma.map((m) => m.rel),
      'the same manifest names',
    );
    ma.forEach((m, i) => assert.ok(m.bytes.equals(mb[i].bytes), `manifests/${m.rel} bytes`));
    assert.ok(bytes(a.siteDir, 'versions.json').equals(bytes(b.siteDir, 'versions.json')), 'versions.json');
    const latestA = bytes(a.siteDir, 'manager', 'latest.json');
    assert.ok(latestA.equals(bytes(b.siteDir, 'manager', 'latest.json')), 'manager/latest.json');
    assert.ok(latestA.equals(world.served.bytes(MANAGER_TAG, 'latest.json')), 'manager/latest.json = the served asset');
  });

  await t.test('a second gen-feed run that differs is E_FEED_NONDETERMINISTIC', () => {
    // Between gen-feed's two calls the wrapper moves the publishedAt of a selected stable release without a manifest,
    // so the second run's pub_date, and only that, differs.
    const rows = listRows(world.state);
    const target = namesOf(pluginsDir)
      .map((name) => genFeed.selectReleases(name, rows))
      .find((sel) => sel !== null && !DETERMINISM_TAGS.includes(sel.stable.tagName)).stable.tagName;
    let calls = 0;
    let inDir;
    const rewriting = (argv, io) => {
      calls += 1;
      if (calls === 2) {
        const file = path.join(inDir, 'releases.json');
        const doc = readJson(file);
        const row = doc.find((r) => r.tagName === target);
        assert.notEqual(row.publishedAt, '2026-06-01T00:00:00Z');
        row.publishedAt = '2026-06-01T00:00:00Z';
        fs.writeFileSync(file, JSON.stringify(doc, null, 2) + '\n');
      }
      return genFeed.main(argv, io);
    };
    const base = path.join(tmp, 'run-c');
    inDir = path.join(base, 'in');
    const w = { pluginsDir, repo, inDir, siteDir: path.join(base, 'site') };
    const c = run(collectArgv(w), { gh: world.deps.gh, genFeedMain: genFeed.main });
    assert.equal(c.code, 0, c.err.join('\n'));
    assert.deepEqual(readJson(path.join(inDir, 'collect-report.json')).manager, { status: 'copied', tag: MANAGER_TAG });
    const g = run(generateArgv(w), { gh: world.deps.gh, genFeedMain: rewriting });
    assert.equal(g.code, 1, g.err.join('\n'));
    assert.equal(calls, 2, 'gen-feed ran twice');
    assert.match(g.err[0] ?? '', /^feed-publish: E_FEED_NONDETERMINISTIC: /u);
    assert.equal(fs.existsSync(path.join(w.siteDir, 'manager')), false, 'no <site-dir>/manager');
  });
});

// --- T5 (SC-005, FR-008)

test('a release without release.json stays v1-only', (t) => {
  const tmp = withTemp(t, 'v1-only');
  const pluginsDir = pluginsWithUrl(tmp);
  const names = namesOf(pluginsDir);
  const bare = names[0]; // the plugin whose stable release gets no Phase 2 files
  const signer = fake.throwawaySigner();
  const world = fake.makeWorld({
    tmp,
    mutate: (state, ctx) => {
      const rows = listRows(state);
      const assetsTmp = path.join(tmp, 'assets');
      for (const name of names) {
        const sel = genFeed.selectReleases(name, rows);
        if (sel === null) continue;
        for (const rel of [sel.stable, sel.sibling]) {
          if (rel === null || (name === bare && rel === sel.stable)) continue;
          fake.addPhase2Files(state, ctx, rel.tagName, { tmp: assetsTmp, signer, pluginsDir });
        }
      }
    },
  });
  const repo = world.state.repo;
  const rows = listRows(world.state);
  const deps = { gh: world.deps.gh, genFeedMain: genFeed.main };
  const w = { pluginsDir, repo, inDir: path.join(tmp, 'in'), siteDir: path.join(tmp, 'site') };

  const c = run(collectArgv(w), deps);
  assert.equal(c.code, 0, c.err.join('\n'));
  const report = readJson(path.join(w.inDir, 'collect-report.json'));
  assert.deepEqual(
    report.plugins.map((p) => p.name),
    names,
  );
  for (const p of report.plugins) {
    const sel = genFeed.selectReleases(p.name, rows);
    assert.equal(p.stable, sel.stable.tagName);
    const want = [sel.stable, sel.sibling]
      .filter((r) => r !== null)
      .map((r) => ({ tag: r.tagName, status: p.name === bare && r === sel.stable ? 'no-manifest' : 'downloaded' }));
    assert.deepEqual(p.manifests, want, `${p.name} manifests`);
  }

  const g = run(generateArgv(w), deps);
  assert.equal(g.code, 0, g.err.join('\n'));
  const log = readText(path.join(w.inDir, 'gen-feed.log'));
  assert.deepEqual(
    linesOf(log).filter((l) => l.includes('WARN')),
    [],
    'gen-feed.log holds no WARN line',
  );

  const feed = readJson(path.join(w.siteDir, 'versions.json'));
  assert.deepEqual(Object.keys(feed.plugins).sort(bytewise), names);
  const entry = feed.plugins[bare];
  assert.equal(Object.hasOwn(entry, 'platforms'), false, `${bare} has no platforms`);
  assert.equal(Object.hasOwn(entry, 'install'), false, `${bare} has no install`);
  const { version, download_url: downloadUrl, release_notes: releaseNotes } = entry;
  assert.deepEqual(
    { version, download_url: downloadUrl, release_notes: releaseNotes },
    {
      version: genFeed.selectReleases(bare, rows).stable.version,
      download_url: versionJsonOf(pluginsDir, bare).url,
      release_notes: '',
    },
  );
  for (const name of names.filter((n) => n !== bare)) {
    assert.ok(Object.hasOwn(feed.plugins[name], 'platforms'), `${name} has platforms`);
    assert.ok(Object.hasOwn(feed.plugins[name], 'install'), `${name} has install`);
  }
});
