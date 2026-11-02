'use strict';
require('./helpers/write-guard.js');

// tools/feed-publish.js collect: what it downloads, from which world, how it aborts and what it does with the manager
// release (spec specs/krate-manager-phase3/spec.md FR-021 to FR-026, FR-023a, FR-050, SC-003, SC-004, SC-006;
// plan §4.1.2, §4.1.3, §4.8, §7.1 T3, T4, T6; Clarifications 2026-10-30 (plan) P-2: a null-digest release.json is
// refused).
// The fake gh of helpers/fake-gh.js serves every release; its Phase 3 shapes (the `tagName,assets` view, the
// `--output` download, the snapshot world, addPhase2Files and addManagerRelease) are checked first, because every
// collect case stands on them. Every expected gh argv list is built before the run it is compared with. Failure
// injection is the test-local `flaky` wrapper composed with the exported withGhRetry (plan §4.8 last bullet); the tests
// never build production effects. Repo fixtures are only read; every output lands in an fs.mkdtemp dir.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');

const contract = require('../contract.js');
const minisign = require('../../minisign.js');
const feedPublish = require('../../feed-publish.js');
const genFeed = require('../../gen-feed.js');
const fake = require('./helpers/fake-gh.js');

const SNAPSHOT = path.join(__dirname, '..', 'fixtures', 'gen-feed', 'snapshot-2026-10-29');
const LIST_JSON = 'tagName,isPrerelease,isDraft,publishedAt';
const STABLE_TAG = 'alphaplug/v1.2.0';
const INSTALLERS = ['Alphaplug-1.2.0-Windows-x64.exe', 'Alphaplug-1.2.0-macOS.pkg', 'Alphaplug-1.2.0-Linux-x64.tar.gz'];
const MANAGER_URL = 'https://krateaudio.com/manager/latest.json';
const OWN = ['releases.json', 'manifests', 'manager', 'collect-report.json'];
const PUBLISHED = '2026-03-01T12:00:00Z';
// The Phase 2 world's repo (fixtures/phase2/backfill/state.json), for latest.json URLs built outside a world.
const REPO = 'krate-fixture/backfill';
// The fixed annotation titles of plan §4.1.6: the only lines the tool may start with "::".
const ANNOTATION_RE =
  /^::(notice|warning|error) title=(feed-collect|feed-generate|feed-degrade|feed-validate|feed-compare|feed-drift|feed-verdict)::/u;

const sha256 = (buf) => crypto.createHash('sha256').update(buf).digest('hex');
const sorted = (xs) => [...xs].sort();
const bytewise = (a, b) => Buffer.compare(Buffer.from(a, 'utf8'), Buffer.from(b, 'utf8'));

function withTemp(t, name) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `km3-${name}-`));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  return dir;
}

const releaseOf = (state, tag) => state.releases.find((r) => r.tagName === tag);

// --- running collect

const linesOf = (text) => (text === '' ? [] : text.replace(/\n$/u, '').split('\n'));

function capture() {
  let stdout = '';
  let stderr = '';
  return {
    io: {
      stdout: { write: (s) => ((stdout += s), true) },
      stderr: { write: (s) => ((stderr += s), true) },
    },
    out: () => linesOf(stdout),
    err: () => linesOf(stderr),
  };
}

/** main with captured output; every line obeys the output rule (PD-18 (c), FR-029e). */
function run(argv, deps, cap = capture()) {
  const code = feedPublish.main(argv, cap.io, deps);
  const out = cap.out();
  const err = cap.err();
  for (const l of out) {
    assert.ok(!l.startsWith('::') || ANNOTATION_RE.test(l), `stdout line starts "::" without a fixed title: ${l}`);
    assert.ok(!l.includes('\r'), `stdout line holds a CR: ${JSON.stringify(l)}`);
  }
  for (const l of err) assert.ok(!l.startsWith('::'), `stderr line starts "::": ${l}`);
  return { code, out, err };
}

/** Every entry under dir, relative, "/"-separated, sorted. */
function listing(dir) {
  return fs
    .readdirSync(dir, { recursive: true })
    .map((p) => String(p).split(path.sep).join('/'))
    .sort();
}

const listArgv = (repo) => ['release', 'list', '--repo', repo, '--limit', '1000', '--json', LIST_JSON];
const viewArgv = (tag, repo) => ['release', 'view', tag, '--repo', repo, '--json', 'tagName,assets'];
const downloadArgv = (tag, repo, pattern, file) => [
  'release',
  'download',
  tag,
  '--repo',
  repo,
  '--pattern',
  pattern,
  '--output',
  file,
];
const manifestPath = (outDir, tag) => path.join(outDir, 'manifests', tag.replace('/', '-') + '.json');
const managerPath = (outDir) => path.join(outDir, 'manager', 'latest.json');
const ghArgvs = (world) => world.calls.filter((c) => c.cmd === 'gh').map((c) => c.argv);
const listRows = (state) =>
  state.releases.map(({ tagName, isPrerelease, isDraft, publishedAt }) => ({ tagName, isPrerelease, isDraft, publishedAt }));
const hasAsset = (state, tag, name) => releaseOf(state, tag).assets.some((a) => a.name === name);
const assetOf = (state, tag, name) => releaseOf(state, tag).assets.find((a) => a.name === name);

/** The plugin names of a plugins dir, bytewise, read as collect reads them (never spelled here). */
const namesOf = (pluginsDir) =>
  genFeed
    .readPluginsDir(pluginsDir, { withSetupIss: false })
    .map((p) => p.versionJson.name)
    .sort(bytewise);

/** A valid Tauri static latest.json for the manager `version`; `edit(doc)` may change it (FR-025 cases). */
function latestJson(version, repo, edit = () => {}) {
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
  edit(doc);
  return JSON.stringify(doc, null, 2) + '\n';
}

// The T3 selection: Phase 2 files on one plugin's stable, sibling, an older release and a draft, a second plugin's
// stable and a third plugin's sibling. Only the selected four may be downloaded.
const PHASE2_TAGS = [
  'alphaplug/v1.2.0',
  'alphaplug/v1.3.0-beta.1',
  'alphaplug/v1.1.0',
  'alphaplug/v1.3.0',
  'betaplug/v2.0.1',
  'gammaplug/v0.13.0-alpha.2',
];
const DOWNLOADED = ['alphaplug/v1.2.0', 'alphaplug/v1.3.0-beta.1', 'betaplug/v2.0.1', 'gammaplug/v0.13.0-alpha.2'];
const MANAGER_TAG = 'manager/v0.2.0';

/**
 * A Phase 2 world for collect: Phase 2 files on `phase2` tags, `managers` rows added with addManagerRelease (default:
 * one valid MANAGER_TAG release), then `edit(state, ctx)` inside mutate. The out-dir exists and holds one unrelated
 * file, so "the listing is unchanged" is not vacuous. `pluginsDir` defaults to the Phase 2 plugins.
 */
function collectWorld(t, name, opts = {}) {
  const { phase2 = PHASE2_TAGS, managers, edit = () => {}, hooks = {}, pluginsDir = fake.PLUGINS_DIR } = opts;
  const tmp = withTemp(t, name);
  const assetsTmp = path.join(tmp, 'assets');
  const signer = phase2.length > 0 ? fake.throwawaySigner() : null;
  const world = fake.makeWorld({
    tmp,
    hooks,
    mutate: (state, ctx) => {
      for (const tag of phase2) fake.addPhase2Files(state, ctx, tag, { tmp: assetsTmp, signer, pluginsDir: fake.PLUGINS_DIR });
      const rows = managers || [{ tag: MANAGER_TAG, latestJson: latestJson('0.2.0', state.repo) }];
      for (const m of rows) fake.addManagerRelease(state, ctx, { publishedAt: PUBLISHED, ...m });
      edit(state, ctx);
    },
  });
  const outDir = path.join(tmp, 'out');
  fs.mkdirSync(outDir);
  fs.writeFileSync(path.join(outDir, 'keep.txt'), 'not collect output\n');
  return { tmp, world, outDir, repo: world.state.repo, pluginsDir };
}

const collectArgv = (w, annotate = false) => [
  'collect',
  '--plugins-dir',
  w.pluginsDir,
  '--repo',
  w.repo,
  '--out-dir',
  w.outDir,
  ...(annotate ? ['--annotate'] : []),
];

const plainDeps = (w) => ({ gh: w.world.deps.gh, genFeedMain: genFeed.main });

/**
 * The exact gh argv list of a complete collect run (plan §4.1.2, SC-003), built from the world before the run: the
 * list, then per plugin bytewise one view per selected release, each followed by its release.json download when the
 * release lists one, then the manager view and its latest.json download. `rows` are the rows the list serves.
 */
function expectedCalls(w, { rows = listRows(w.world.state), manager = MANAGER_TAG } = {}) {
  const { state } = w.world;
  const calls = [listArgv(w.repo)];
  for (const name of namesOf(w.pluginsDir)) {
    const sel = genFeed.selectReleases(name, rows);
    if (sel === null) continue;
    for (const rel of [sel.stable, sel.sibling]) {
      if (rel === null) continue;
      calls.push(viewArgv(rel.tagName, w.repo));
      if (hasAsset(state, rel.tagName, 'release.json')) {
        calls.push(downloadArgv(rel.tagName, w.repo, 'release.json', manifestPath(w.outDir, rel.tagName)));
      }
    }
  }
  if (manager !== null) {
    calls.push(viewArgv(manager, w.repo));
    if (hasAsset(state, manager, 'latest.json')) {
      calls.push(downloadArgv(manager, w.repo, 'latest.json', managerPath(w.outDir)));
    }
  }
  return calls;
}

/** The collect-report.json of plan §4.1.3 for a complete run. */
function expectedReport(w, { rows = listRows(w.world.state), manager, ghCalls }) {
  const { state } = w.world;
  const plugins = namesOf(w.pluginsDir).map((name) => {
    const sel = genFeed.selectReleases(name, rows);
    if (sel === null) return { name, status: 'no-stable' };
    const manifests = [sel.stable, sel.sibling]
      .filter((r) => r !== null)
      .map((r) => ({ tag: r.tagName, status: hasAsset(state, r.tagName, 'release.json') ? 'downloaded' : 'no-manifest' }));
    return { name, stable: sel.stable.tagName, sibling: sel.sibling === null ? null : sel.sibling.tagName, manifests };
  });
  return { plugins, manager, gh_calls: ghCalls };
}

const readText = (file) => fs.readFileSync(file, 'utf8');

/**
 * Failure injection (plan §4.8 last bullet): every call goes to the fake (so `calls` records each attempt, and a
 * failing download still writes its partial file); the first `failures` calls that `match` report status 1. `seen`
 * records, per matching call, whether its --output file existed before and after the fake ran.
 */
function flaky(gh, { match, failures }) {
  let left = failures;
  const seen = [];
  const wrapped = (argv) => {
    const i = argv.indexOf('--output');
    const out = i >= 0 ? argv[i + 1] : undefined;
    const before = out !== undefined && fs.existsSync(out);
    const r = gh(argv);
    if (!match(argv)) return r;
    seen.push({ before, after: out !== undefined && fs.existsSync(out) });
    if (left > 0) {
      left -= 1;
      return { status: 1, stdout: '', stderr: 'flaky: injected failure' };
    }
    return r;
  };
  return { gh: wrapped, seen };
}

const sameArgv = (a) => (b) => JSON.stringify(a) === JSON.stringify(b);

/** deps whose gh is withGhRetry over `gh`, with a recording sleep and the log on the captured stderr (or `logs`). */
function retryDeps(gh, cap, logs) {
  const sleeps = [];
  const log = logs ? (l) => logs.push(l) : (l) => cap.io.stderr.write(`${l}\n`);
  const deps = { gh: feedPublish.withGhRetry(gh, { sleep: (ms) => sleeps.push(ms), log }), genFeedMain: genFeed.main };
  return { deps, sleeps };
}

/** A copy of the Phase 2 plugins with the https `url` gen-feed's v2 mode needs (generate runs on collect's output). */
function pluginsWithUrl(tmp) {
  const dir = path.join(tmp, 'plugins');
  fs.cpSync(fake.PLUGINS_DIR, dir, { recursive: true });
  for (const d of fs.readdirSync(dir)) {
    const file = path.join(dir, d, 'version.json');
    const doc = JSON.parse(readText(file));
    doc.url = `https://krateaudio.com/${doc.name.toLowerCase()}/`;
    fs.writeFileSync(file, JSON.stringify(doc, null, 2) + '\n');
  }
  return dir;
}

/** generate (T009) on collect's out-dir, with a genFeedMain that records the argv of every gen-feed run. */
function generateAfter(w) {
  const argvs = [];
  const deps = {
    gh: () => {
      throw new Error('generate called gh');
    },
    genFeedMain: (argv, io) => {
      argvs.push([...argv]);
      return genFeed.main(argv, io);
    },
  };
  const siteDir = path.join(w.tmp, 'site');
  const argv = ['generate', '--in-dir', w.outDir, '--plugins-dir', w.pluginsDir, '--repo', w.repo, '--site-dir', siteDir];
  const r = run(argv, deps);
  return { ...r, argvs, siteDir };
}

test('collect downloads only the selected manifests', async (t) => {
  await t.test('the fake gh serves the Phase 3 shapes', async (st) => {
    await st.test('(1) the default world is the Phase 2 world', (s) => {
      const world = fake.makeWorld({ tmp: withTemp(s, 'world') });
      assert.equal(world.state.repo, 'krate-fixture/backfill');
      assert.equal(world.state.releases.length, 38);
    });

    await st.test('(2) release view serves the tagName,assets field set', (s) => {
      const world = fake.makeWorld({ tmp: withTemp(s, 'view') });
      const repo = world.state.repo;
      const res = world.deps.gh(['release', 'view', STABLE_TAG, '--repo', repo, '--json', 'tagName,assets']);
      assert.equal(res.status, 0, res.stderr);
      const doc = JSON.parse(res.stdout);
      assert.deepEqual(Object.keys(doc), ['tagName', 'assets']);
      assert.equal(doc.tagName, STABLE_TAG);
      assert.deepEqual(sorted(doc.assets.map((a) => a.name)), sorted(INSTALLERS));
    });

    await st.test('(3) release download --output writes exactly the served bytes', (s) => {
      const tmp = withTemp(s, 'dl');
      const world = fake.makeWorld({ tmp });
      const repo = world.state.repo;
      const out = path.join(tmp, 'out');
      fs.mkdirSync(out);
      const asset = INSTALLERS[2];
      const file = path.join(out, 'asset.bin');
      const download = (name, target) =>
        world.deps.gh(['release', 'download', STABLE_TAG, '--repo', repo, '--pattern', name, '--output', target]);

      const before = fs.readdirSync(out);
      const res = download(asset, file);
      assert.equal(res.status, 0, res.stderr);
      assert.deepEqual(sorted(fs.readdirSync(out)), sorted([...before, 'asset.bin']));
      assert.ok(fs.readFileSync(file).equals(world.served.bytes(STABLE_TAG, asset)), 'the served bytes');

      const missing = download('Alphaplug-1.2.0-nothing.zip', path.join(out, 'missing.bin'));
      assert.equal(missing.status, 1);
      assert.deepEqual(sorted(fs.readdirSync(out)), sorted([...before, 'asset.bin']), 'a missing asset writes nothing');

      const noDir = path.join(tmp, 'no-such-dir');
      const orphan = download(asset, path.join(noDir, 'asset.bin'));
      assert.equal(orphan.status, 1);
      assert.ok(orphan.stderr.includes(noDir), `stderr names the directory: ${orphan.stderr}`);
      assert.equal(fs.existsSync(noDir), false, 'a missing directory is not created');

      // The Phase 2 --dir shape is unchanged.
      const dir = path.join(tmp, 'dir-shape');
      const viaDir = world.deps.gh(['release', 'download', STABLE_TAG, '--repo', repo, '--pattern', asset, '--dir', dir]);
      assert.equal(viaDir.status, 0, viaDir.stderr);
      assert.ok(fs.readFileSync(path.join(dir, asset)).equals(world.served.bytes(STABLE_TAG, asset)));
    });

    await st.test('(4) world snapshot serves the 2026-10-29 releases', (s) => {
      const world = fake.makeWorld({ tmp: withTemp(s, 'snap'), world: 'snapshot' });
      const repo = world.state.repo;
      assert.equal(repo, 'rolandzwaga/krate-audio');
      const snapshotRows = JSON.parse(fs.readFileSync(path.join(SNAPSHOT, 'releases.json'), 'utf8'));
      assert.equal(snapshotRows.length, 88);
      const res = world.deps.gh(['release', 'list', '--repo', repo, '--limit', '1000', '--json', LIST_JSON]);
      assert.equal(res.status, 0, res.stderr);
      assert.deepEqual(JSON.parse(res.stdout), snapshotRows);

      // The names come from the snapshot's version.json copies, so this file never spells one.
      const pluginsDir = path.join(SNAPSHOT, 'plugins');
      const names = new Map();
      for (const d of fs.readdirSync(pluginsDir)) {
        const name = JSON.parse(fs.readFileSync(path.join(pluginsDir, d, 'version.json'), 'utf8')).name;
        names.set(name.toLowerCase(), name);
      }
      assert.equal(names.size, 9);
      const seen = new Set();
      for (const r of world.state.releases) {
        const m = /^([^/]+)\/v(.+)$/.exec(r.tagName);
        if (m === null) {
          // A pre-monorepo tag (no "<plugin>/" prefix): no version.json names it, so the fake serves no asset.
          assert.deepEqual(r.assets, [], `${r.tagName} has no assets`);
          continue;
        }
        const name = names.get(m[1]);
        assert.ok(name !== undefined, `${r.tagName}: its prefix names a snapshot plugin`);
        seen.add(name);
        const ver = m[2];
        assert.deepEqual(
          r.assets.map((a) => a.name),
          [`${name}-${ver}-Windows-x64.exe`, `${name}-${ver}-macOS.pkg`, `${name}-${ver}-Linux-x64.tar.gz`],
        );
        for (const a of r.assets) {
          assert.ok(Number.isInteger(a.size) && a.size > 0, `${a.name} size ${a.size}`);
          assert.match(a.digest, /^sha256:[0-9a-f]{64}$/);
          assert.equal(a.digest, `sha256:${sha256(world.served.bytes(r.tagName, a.name))}`);
        }
      }
      assert.equal(seen.size, 9, 'every snapshot plugin has releases');
    });

    await st.test('(5) addPhase2Files adds a verifiable Phase 2 file set', (s) => {
      const tmp = withTemp(s, 'p2');
      const signer = fake.throwawaySigner();
      const world = fake.makeWorld({
        tmp,
        mutate: (state, ctx) =>
          fake.addPhase2Files(state, ctx, STABLE_TAG, { tmp, signer, pluginsDir: fake.PLUGINS_DIR }),
      });
      const r = releaseOf(world.state, STABLE_TAG);
      const extra = ['SHA256SUMS', ...INSTALLERS.map((a) => `${a}.minisig`), 'release.json'];
      assert.deepEqual(sorted(r.assets.map((a) => a.name)), sorted([...INSTALLERS, ...extra]));
      for (const a of r.assets) {
        assert.equal(a.size, world.served.bytes(STABLE_TAG, a.name).length, `${a.name} size`);
        assert.equal(a.digest, `sha256:${sha256(world.served.bytes(STABLE_TAG, a.name))}`, `${a.name} digest`);
      }

      const doc = JSON.parse(world.served.bytes(STABLE_TAG, 'release.json').toString('utf8'));
      assert.doesNotThrow(() => contract.checkReleaseManifest(doc, { repo: world.state.repo }));
      assert.equal(doc.tag, STABLE_TAG);
      assert.equal(doc.version, '1.2.0');
      assert.equal(doc.pub_date, r.publishedAt);

      const verifyDir = path.join(tmp, 'verify');
      fs.mkdirSync(verifyDir);
      for (const asset of INSTALLERS) {
        const file = path.join(verifyDir, asset);
        fs.writeFileSync(file, world.served.bytes(STABLE_TAG, asset));
        const sig = world.served.bytes(STABLE_TAG, `${asset}.minisig`).toString('utf8');
        assert.doesNotThrow(() => minisign.verifyFile(file, sig, signer.publicKeyText), asset);
      }

      const sums = world.served.bytes(STABLE_TAG, 'SHA256SUMS').toString('utf8');
      const lines = sums.split('\n').filter((l) => l !== '');
      assert.equal(lines.length, 3);
      const listed = [];
      for (const line of lines) {
        const m = /^([0-9a-f]{64}) {2}(.+)$/.exec(line);
        assert.ok(m !== null, `SHA256SUMS line ${JSON.stringify(line)}`);
        assert.equal(m[1], sha256(world.served.bytes(STABLE_TAG, m[2])), m[2]);
        listed.push(m[2]);
      }
      assert.deepEqual(sorted(listed), sorted(INSTALLERS));
    });

    await st.test('(6) addManagerRelease adds a manager release row', (s) => {
      const tmp = withTemp(s, 'mgr');
      const publishedAt = '2026-03-01T12:00:00Z';
      const world = fake.makeWorld({
        tmp,
        mutate: (state, ctx) => {
          fake.addManagerRelease(state, ctx, { tag: 'manager/v0.1.0', publishedAt, latestJson: '{"x":1}\n' });
          fake.addManagerRelease(state, ctx, { tag: 'manager/v0.2.0', publishedAt, latestJson: null });
          fake.addManagerRelease(state, ctx, { tag: 'manager/v0.3.0', isDraft: true, publishedAt, latestJson: null });
          fake.addManagerRelease(state, ctx, {
            tag: 'manager/v0.4.0-rc.1',
            isPrerelease: true,
            publishedAt,
            latestJson: null,
          });
        },
      });
      assert.equal(world.state.releases.length, 38 + 4);

      const one = releaseOf(world.state, 'manager/v0.1.0');
      assert.equal(one.isDraft, false);
      assert.equal(one.isPrerelease, false);
      assert.equal(one.publishedAt, publishedAt);
      assert.deepEqual(
        one.assets.map((a) => a.name),
        ['latest.json'],
      );
      const bytes = Buffer.from('{"x":1}\n', 'utf8');
      assert.ok(world.served.bytes('manager/v0.1.0', 'latest.json').equals(bytes));
      assert.equal(one.assets[0].size, bytes.length);
      assert.equal(one.assets[0].digest, `sha256:${sha256(bytes)}`);
      const file = path.join(tmp, 'latest.json');
      const res = world.deps.gh([
        'release',
        'download',
        'manager/v0.1.0',
        '--repo',
        world.state.repo,
        '--pattern',
        'latest.json',
        '--output',
        file,
      ]);
      assert.equal(res.status, 0, res.stderr);
      assert.ok(fs.readFileSync(file).equals(bytes));

      const none = releaseOf(world.state, 'manager/v0.2.0');
      assert.deepEqual(none.assets, []);
      assert.equal(none.isDraft, false);
      assert.equal(none.isPrerelease, false);
      assert.equal(releaseOf(world.state, 'manager/v0.3.0').isDraft, true);
      assert.equal(releaseOf(world.state, 'manager/v0.3.0').isPrerelease, false);
      assert.equal(releaseOf(world.state, 'manager/v0.4.0-rc.1').isPrerelease, true);
      assert.equal(releaseOf(world.state, 'manager/v0.4.0-rc.1').isDraft, false);
    });
  });

  await t.test('(7) collect views every selected release and downloads exactly the listed release.json files', (s) => {
    const w = collectWorld(s, 'sel');
    const { state } = w.world;
    const expected = expectedCalls(w); // written before the run
    const report = expectedReport(w, { manager: { status: 'copied', tag: MANAGER_TAG }, ghCalls: expected.length });

    const r = run(collectArgv(w), plainDeps(w));
    assert.equal(r.code, 0, r.err.join('\n'));
    assert.deepEqual(r.err, []);
    const calls = ghArgvs(w.world);
    assert.deepStrictEqual(calls, expected);

    // The selection, spelled: the stable and sibling tags that list release.json, never the older one or the draft.
    const manifestDownloads = calls.filter((a) => a[1] === 'download' && a[6] === 'release.json').map((a) => a[2]);
    assert.deepEqual(manifestDownloads, DOWNLOADED);
    assert.deepEqual(listing(path.join(w.outDir, 'manifests')), sorted(DOWNLOADED.map((tag) => tag.replace('/', '-') + '.json')));
    for (const tag of DOWNLOADED) {
      assert.ok(fs.readFileSync(manifestPath(w.outDir, tag)).equals(w.world.served.bytes(tag, 'release.json')), tag);
    }
    assert.ok(fs.readFileSync(managerPath(w.outDir)).equals(w.world.served.bytes(MANAGER_TAG, 'latest.json')));
    for (const argv of calls) {
      assert.ok(!argv.includes('--clobber'), `no --clobber: ${argv.join(' ')}`);
      assert.ok(!argv.includes('api'), `no gh api: ${argv.join(' ')}`);
      for (const a of argv) assert.ok(!/:\/\/|github\.com|krateaudio/u.test(a), `no host in ${JSON.stringify(a)}`);
      if (argv[1] === 'download') assert.ok(['release.json', 'latest.json'].includes(argv[6]), argv.join(' '));
    }

    assert.equal(readText(path.join(w.outDir, 'releases.json')), JSON.stringify(listRows(state), null, 2) + '\n');
    assert.equal(readText(path.join(w.outDir, 'collect-report.json')), JSON.stringify(report, null, 2) + '\n');
    assert.deepEqual(sorted(fs.readdirSync(w.outDir)), sorted(['keep.txt', ...OWN]));

    assert.equal(r.out.length, 9 + 1, r.out.join('\n'));
    assert.equal(
      r.out[0],
      'collect: Alphaplug stable alphaplug/v1.2.0 (downloaded), sibling alphaplug/v1.3.0-beta.1 (downloaded)',
    );
    assert.equal(r.out[1], 'collect: Betaplug stable betaplug/v2.0.1 (downloaded), sibling betaplug/v2.1.0-rc.1 (no-manifest)');
    assert.equal(
      r.out[2],
      'collect: Deltaplug stable deltaplug/v1.10.0 (no-manifest), sibling deltaplug/v1.11.0-beta.1 (no-manifest)',
    );
    assert.equal(r.out[r.out.length - 1], `manager: copied ${MANAGER_TAG}`);
    assert.ok(!r.out.some((l) => l.startsWith('collect: digest checked')), 'P-2 (b) superseded the digest count line');
  });

  await t.test('(8) a world without a manager release records no manager call', (s) => {
    const w = collectWorld(s, 'nomgr', { managers: [] });
    const expected = expectedCalls(w, { manager: null });
    const r = run(collectArgv(w), plainDeps(w));
    assert.equal(r.code, 0, r.err.join('\n'));
    assert.deepStrictEqual(ghArgvs(w.world), expected);
    assert.ok(!ghArgvs(w.world).some((a) => a[2].startsWith('manager/')), 'no manager call');
    assert.equal(fs.existsSync(path.join(w.outDir, 'manager')), false);
    const report = JSON.parse(readText(path.join(w.outDir, 'collect-report.json')));
    assert.deepEqual(report.manager, { status: 'none' });
    assert.equal(report.gh_calls, expected.length);
    assert.equal(r.out[r.out.length - 1], 'manager: none');
  });

  await t.test('(9) a plugin without a stable release is reported no-stable and never viewed', (s) => {
    const hooks = { releaseList: (rows) => rows.filter((row) => !row.tagName.startsWith('etaplug/')) };
    const w = collectWorld(s, 'nostable', { hooks });
    const rows = listRows(w.world.state).filter((row) => !row.tagName.startsWith('etaplug/'));
    const expected = expectedCalls(w, { rows });
    const report = expectedReport(w, { rows, manager: { status: 'copied', tag: MANAGER_TAG }, ghCalls: expected.length });
    const r = run(collectArgv(w), plainDeps(w));
    assert.equal(r.code, 0, r.err.join('\n'));
    assert.deepStrictEqual(ghArgvs(w.world), expected);
    assert.ok(!ghArgvs(w.world).some((a) => a[2].startsWith('etaplug/')));
    assert.ok(r.out.includes('collect: Etaplug no-stable'), r.out.join('\n'));
    assert.deepEqual(report.plugins.find((p) => p.name === 'Etaplug'), { name: 'Etaplug', status: 'no-stable' });
    assert.equal(readText(path.join(w.outDir, 'collect-report.json')), JSON.stringify(report, null, 2) + '\n');
  });

  await t.test('(10) a release.json asset with digest null is refused before any download (P-2)', (s) => {
    const tag = 'betaplug/v2.0.1';
    const w = collectWorld(s, 'nulldigest', {
      edit: (state) => {
        assetOf(state, tag, 'release.json').digest = null;
      },
    });
    const full = expectedCalls(w);
    const view = full.findIndex(sameArgv(viewArgv(tag, w.repo)));
    const before = listing(w.outDir);
    const r = run(collectArgv(w), plainDeps(w));
    assert.equal(r.code, 1);
    assert.match(r.err[0], /^feed-publish: E_FEED_DOWNLOAD: /u);
    assert.ok(r.err[0].includes(tag) && r.err[0].includes('has no digest'), r.err[0]);
    assert.deepStrictEqual(ghArgvs(w.world), full.slice(0, view + 1), 'the view is the last call: nothing downloaded');
    assert.deepEqual(listing(w.outDir), before);
  });
});

test('collect aborts cleanly', async (t) => {
  /** Runs collect on `w` and checks the abort contract: exit 1, the first stderr line names `code`, nothing left. */
  function expectAbort(w, code, deps = plainDeps(w), cap = capture()) {
    const before = listing(w.outDir);
    const r = run(collectArgv(w), deps, cap);
    assert.equal(r.code, 1, r.err.join('\n'));
    assert.ok(r.err.length > 0 && r.err[0].startsWith(`feed-publish: ${code}: `), r.err.join('\n'));
    assert.deepEqual(listing(w.outDir), before, 'the out-dir listing is unchanged');
    for (const name of OWN) assert.equal(fs.existsSync(path.join(w.outDir, name)), false, name);
    assert.deepEqual(r.out, []);
    return r;
  }

  const thousand = (rows) =>
    Array.from({ length: 1000 }, (_, i) => rows[i] || { ...rows[0], tagName: `filler/v0.0.${i}` });

  await t.test('(1) every FR-023 abort exits 1 and leaves the out-dir as it was', async (st) => {
    await st.test('list exit 1 after three attempts (E_INPUT)', (s) => {
      const w = collectWorld(s, 'ab-list');
      const fl = flaky(w.world.deps.gh, { match: (a) => a[1] === 'list', failures: 3 });
      const logs = [];
      const { deps } = retryDeps(fl.gh, null, logs);
      expectAbort(w, 'E_INPUT', deps);
      assert.equal(logs.length, 2);
    });
    await st.test('a non-array list (E_INPUT)', (s) => {
      expectAbort(collectWorld(s, 'ab-nonarray', { hooks: { releaseList: () => ({ rows: [] }) } }), 'E_INPUT');
    });
    await st.test('a list row of the wrong shape (E_INPUT)', (s) => {
      const hooks = { releaseList: (rows) => [...rows, { tagName: 'alphaplug/v9.0.0', isPrerelease: 'no', isDraft: false }] };
      expectAbort(collectWorld(s, 'ab-row', { hooks }), 'E_INPUT');
    });
    await st.test('1000 rows (E_FEED_LIST_TRUNCATED)', (s) => {
      const w = collectWorld(s, 'ab-1000', { hooks: { releaseList: thousand } });
      expectAbort(w, 'E_FEED_LIST_TRUNCATED');
      assert.equal(ghArgvs(w.world).length, 1, 'no view after the list');
    });
    await st.test('a view for another tag (E_INPUT)', (s) => {
      const hooks = { view: (tag, doc) => (tag === 'betaplug/v2.0.1' ? { ...doc, tagName: 'betaplug/v2.0.0' } : doc) };
      expectAbort(collectWorld(s, 'ab-othertag', { hooks }), 'E_INPUT');
    });
    await st.test('a view asset of the wrong shape (E_INPUT)', (s) => {
      const hooks = {
        view: (tag, doc) =>
          tag === 'betaplug/v2.0.1' ? { ...doc, assets: doc.assets.map((a) => ({ ...a, size: -1 })) } : doc,
      };
      expectAbort(collectWorld(s, 'ab-assetshape', { hooks }), 'E_INPUT');
    });
    await st.test('a truncated release.json download (size, E_FEED_DOWNLOAD)', (s) => {
      const tag = 'gammaplug/v0.13.0-alpha.2';
      const edit = (state, ctx) => {
        const bytes = ctx.bytes(tag, 'release.json');
        const asset = assetOf(state, tag, 'release.json');
        asset.size = bytes.length;
        asset.digest = `sha256:${sha256(bytes)}`;
        ctx.setServed(tag, 'release.json', bytes.subarray(0, bytes.length - 10));
      };
      const r = expectAbort(collectWorld(s, 'ab-size', { edit }), 'E_FEED_DOWNLOAD');
      assert.ok(r.err[0].includes(tag), r.err[0]);
    });
    await st.test('a release.json digest mismatch (E_FEED_DOWNLOAD)', (s) => {
      const tag = 'gammaplug/v0.13.0-alpha.2';
      const edit = (state) => {
        assetOf(state, tag, 'release.json').digest = `sha256:${'0'.repeat(64)}`;
      };
      expectAbort(collectWorld(s, 'ab-digest', { edit }), 'E_FEED_DOWNLOAD');
    });
    await st.test('a truncated latest.json download (size, E_FEED_DOWNLOAD)', (s) => {
      const edit = (state, ctx) => {
        const bytes = ctx.bytes(MANAGER_TAG, 'latest.json');
        const asset = assetOf(state, MANAGER_TAG, 'latest.json');
        asset.size = bytes.length;
        asset.digest = `sha256:${sha256(bytes)}`;
        ctx.setServed(MANAGER_TAG, 'latest.json', bytes.subarray(0, 5));
      };
      expectAbort(collectWorld(s, 'ab-latestsize', { edit }), 'E_FEED_DOWNLOAD');
    });
    await st.test('an E_TAG_FORMAT tag aborts before any view', (s) => {
      const hooks = {
        releaseList: (rows) => [...rows, { tagName: 'zetaplug/v1.2', isPrerelease: false, isDraft: false, publishedAt: PUBLISHED }],
      };
      const w = collectWorld(s, 'ab-tag', { hooks });
      expectAbort(w, 'E_TAG_FORMAT');
      assert.deepStrictEqual(ghArgvs(w.world), [listArgv(w.repo)]);
    });

    /** A copy of the Phase 2 plugins plus `extra` dirs ({dir: name}), or with one name replaced. */
    function namedPlugins(s, label, { extra = {}, rename = {} }) {
      const tmp = withTemp(s, label);
      const dir = path.join(tmp, 'plugins');
      fs.cpSync(fake.PLUGINS_DIR, dir, { recursive: true });
      for (const [d, name] of Object.entries(rename)) {
        fs.writeFileSync(path.join(dir, d, 'version.json'), JSON.stringify({ name, version: '1.0.0' }, null, 2) + '\n');
      }
      for (const [d, name] of Object.entries(extra)) {
        fs.mkdirSync(path.join(dir, d));
        fs.writeFileSync(path.join(dir, d, 'version.json'), JSON.stringify({ name, version: '1.0.0' }, null, 2) + '\n');
      }
      return dir;
    }
    for (const [label, opts, code] of [
      ['an invalid name', { rename: { Betaplug: 'beta plug' } }, 'E_PLUGIN_NAME'],
      ['a name that is not a string', { rename: { Betaplug: 7 } }, 'E_PLUGIN_NAME'],
      ['two names equal after lower-casing', { extra: { Zzplug: 'ALPHAPLUG' } }, 'E_DUPLICATE_PLUGIN'],
      ['a name lower-casing to manager', { extra: { Mgr: 'Manager' } }, 'E_PLUGIN_NAME'],
    ]) {
      await st.test(`${label} (${code}) before any gh call`, (s) => {
        const w = collectWorld(s, 'ab-name', { pluginsDir: namedPlugins(s, 'ab-names', opts) });
        expectAbort(w, code);
        assert.equal(w.world.calls.length, 0, 'no gh call');
      });
    }
  });

  await t.test('(2) a usage error exits 2 with the usage text', (s) => {
    const w = collectWorld(s, 'usage', { phase2: [] });
    for (const argv of [
      ['collect', '--plugins-dir', w.pluginsDir, '--repo', w.repo],
      ['collect', '--plugins-dir', w.pluginsDir, '--repo', 'not a repo', '--out-dir', w.outDir],
      [...collectArgv(w), '--clobber'],
    ]) {
      const r = run(argv, plainDeps(w));
      assert.equal(r.code, 2, argv.join(' '));
      assert.match(r.err[0], /^feed-publish: E_USAGE: /u);
      assert.ok(r.err.some((l) => l.startsWith('usage: node tools/feed-publish.js')), r.err.join('\n'));
    }
    assert.equal(w.world.calls.length, 0);
  });

  await t.test('(3) a pre-filled out-dir is refused untouched, before any gh call (PD-6)', async (st) => {
    for (const name of OWN) {
      await st.test(name, (s) => {
        const w = collectWorld(s, 'prefilled', { phase2: [] });
        const entry = path.join(w.outDir, name);
        const sentinel = path.extname(name) === '.json' ? entry : path.join(entry, 'sentinel');
        if (sentinel !== entry) fs.mkdirSync(entry);
        const bytes = Buffer.from(`sentinel for ${name}\r\n`, 'utf8');
        fs.writeFileSync(sentinel, bytes);
        const before = listing(w.outDir);
        const r = run(collectArgv(w), plainDeps(w));
        assert.equal(r.code, 1);
        assert.match(r.err[0], /^feed-publish: E_INPUT: /u);
        assert.ok(r.err[0].includes(name), r.err[0]);
        assert.equal(w.world.calls.length, 0, 'zero gh calls');
        assert.ok(fs.readFileSync(sentinel).equals(bytes), 'the sentinel is byte-identical');
        assert.deepEqual(listing(w.outDir), before);
      });
    }
  });

  // The three retried argv: the list, one view and one release.json download of the T3 world.
  const RETRIED = [
    ['list', (w) => listArgv(w.repo), 'list'],
    ['view', (w) => viewArgv('betaplug/v2.0.1', w.repo), 'view betaplug/v2.0.1'],
    [
      'download',
      (w) => downloadArgv('betaplug/v2.0.1', w.repo, 'release.json', manifestPath(w.outDir, 'betaplug/v2.0.1')),
      'download betaplug/v2.0.1',
    ],
  ];

  for (const [failures, sleepsWanted] of [
    [1, [5000]],
    [2, [5000, 15000]],
  ]) {
    await t.test(`(4${failures === 1 ? 'a' : 'b'}) a gh call failing ${failures}x is retried, then collect completes`, async (st) => {
      for (const [kind, argvOf, label] of RETRIED) {
        await st.test(kind, (s) => {
          const w = collectWorld(s, `retry-${kind}`);
          const failing = argvOf(w);
          const base = expectedCalls(w); // written before the run
          const at = base.findIndex(sameArgv(failing));
          assert.ok(at >= 0, 'the failing argv is part of the T3 list');
          const expected = [...base.slice(0, at), ...Array(failures).fill(failing), ...base.slice(at)];
          const fl = flaky(w.world.deps.gh, { match: sameArgv(failing), failures });
          const cap = capture();
          const { deps, sleeps } = retryDeps(fl.gh, cap);
          const r = run(collectArgv(w), deps, cap);
          assert.equal(r.code, 0, r.err.join('\n'));
          assert.deepStrictEqual(ghArgvs(w.world), expected);
          assert.deepEqual(sleeps, sleepsWanted);
          const warns = r.err.filter((l) => l.startsWith('feed-publish: WARN gh attempt '));
          assert.deepEqual(
            warns,
            sleepsWanted.map(
              (ms, i) => `feed-publish: WARN gh attempt ${i + 1}/3 failed: ${label} (exit 1); retrying in ${ms / 1000} s`,
            ),
          );
          assert.deepEqual(r.err, warns, 'no other stderr line');
          const report = JSON.parse(readText(path.join(w.outDir, 'collect-report.json')));
          assert.equal(report.gh_calls, expected.length, 'gh_calls counts every attempt');
          if (kind === 'download') {
            // The fake wrote a partial file on each failed attempt; the retry found it removed.
            assert.deepEqual(
              fl.seen.map((x) => x.before),
              Array(failures + 1).fill(false),
            );
            assert.ok(fl.seen.slice(0, failures).every((x) => x.after), 'each failed attempt left a file');
            assert.ok(fs.readFileSync(failing[8]).equals(w.world.served.bytes('betaplug/v2.0.1', 'release.json')));
          }
        });
      }
    });
  }

  await t.test('(4c) a gh call failing three times aborts with E_INPUT naming it and 3 attempts', async (st) => {
    for (const [kind, argvOf, label] of RETRIED) {
      await st.test(kind, (s) => {
        const w = collectWorld(s, `retry3-${kind}`);
        const failing = argvOf(w);
        const fl = flaky(w.world.deps.gh, { match: sameArgv(failing), failures: 3 });
        const cap = capture();
        const { deps, sleeps } = retryDeps(fl.gh, cap);
        const before = listing(w.outDir);
        const r = run(collectArgv(w), deps, cap);
        assert.equal(r.code, 1);
        assert.deepEqual(sleeps, [5000, 15000]);
        assert.equal(ghArgvs(w.world).filter(sameArgv(failing)).length, 3, 'exactly three attempts');
        const last = r.err[r.err.length - 1];
        assert.match(last, /^feed-publish: E_INPUT: /u);
        assert.ok(last.includes(`gh release ${label}`) && last.includes('3 attempts'), last);
        assert.equal(r.err.length, 3, 'two WARN lines and the abort');
        assert.deepEqual(listing(w.outDir), before, 'the out-dir is clean');
      });
    }
  });

  await t.test('(4d) results decided on a successful call are attempted exactly once, never retried', async (st) => {
    const cases = [
      [
        'a view for another tag',
        { hooks: { view: (tag, doc) => (tag === 'betaplug/v2.0.1' ? { ...doc, tagName: 'betaplug/v9.9.9' } : doc) } },
        (w) => viewArgv('betaplug/v2.0.1', w.repo),
        'E_INPUT',
      ],
      [
        'a size mismatch',
        {
          edit: (state) => {
            assetOf(state, 'betaplug/v2.0.1', 'release.json').size = 3;
          },
        },
        (w) => downloadArgv('betaplug/v2.0.1', w.repo, 'release.json', manifestPath(w.outDir, 'betaplug/v2.0.1')),
        'E_FEED_DOWNLOAD',
      ],
      [
        'a digest mismatch',
        {
          edit: (state) => {
            assetOf(state, 'betaplug/v2.0.1', 'release.json').digest = `sha256:${'a'.repeat(64)}`;
          },
        },
        (w) => downloadArgv('betaplug/v2.0.1', w.repo, 'release.json', manifestPath(w.outDir, 'betaplug/v2.0.1')),
        'E_FEED_DOWNLOAD',
      ],
      ['a non-array list', { hooks: { releaseList: () => 'rows' } }, (w) => listArgv(w.repo), 'E_INPUT'],
      ['1000 rows', { hooks: { releaseList: thousand } }, (w) => listArgv(w.repo), 'E_FEED_LIST_TRUNCATED'],
    ];
    for (const [label, opts, argvOf, code] of cases) {
      await st.test(label, (s) => {
        const w = collectWorld(s, 'once', opts);
        const cap = capture();
        const { deps, sleeps } = retryDeps(w.world.deps.gh, cap);
        expectAbort(w, code, deps, cap);
        assert.equal(ghArgvs(w.world).filter(sameArgv(argvOf(w))).length, 1, 'attempted once');
        assert.deepEqual(sleeps, [], 'sleep never called');
      });
    }
  });
});

test('collect copies only a valid manager latest.json', async (t) => {
  /** A world with only manager rows (no Phase 2 files) and plugins gen-feed's v2 mode accepts. */
  function managerWorld(s, name, managers) {
    const w = collectWorld(s, name, { phase2: [], managers });
    return { ...w, pluginsDir: pluginsWithUrl(w.tmp) };
  }
  const managerCalls = (w) => ghArgvs(w.world).filter((a) => a[2].startsWith('manager'));
  const report = (w) => JSON.parse(readText(path.join(w.outDir, 'collect-report.json')));

  /** collect then generate: the feed has no manager block and gen-feed never saw --manager-latest-url. */
  function expectNoManager(w, r) {
    assert.equal(r.code, 0, r.err.join('\n'));
    assert.deepEqual(report(w).manager, { status: 'none' });
    assert.equal(fs.existsSync(path.join(w.outDir, 'manager')), false);
    const g = generateAfter(w);
    assert.equal(g.code, 0, g.err.join('\n'));
    assert.equal(g.argvs.length, 2);
    for (const argv of g.argvs) assert.ok(!argv.includes('--manager-latest-url'), argv.join(' '));
    assert.equal(fs.existsSync(path.join(g.siteDir, 'manager')), false);
    const feed = JSON.parse(readText(path.join(g.siteDir, 'versions.json')));
    assert.equal(Object.hasOwn(feed, 'manager'), false);
  }

  await t.test('(1) no manager release, a prerelease-only one and a draft-only one are manager none', async (st) => {
    for (const [label, managers] of [
      ['none', []],
      ['prerelease-only', [{ tag: 'manager/v0.3.0', isPrerelease: true }]],
      ['draft-only', [{ tag: 'manager/v0.3.0', isDraft: true }]],
    ]) {
      await st.test(label, (s) => {
        const w = managerWorld(s, `mgr-${label}`, managers.map((m) => ({ ...m, latestJson: latestJson('0.3.0', 'x/y') })));
        const r = run(collectArgv(w), plainDeps(w));
        assert.deepEqual(managerCalls(w), [], 'no manager view');
        assert.equal(r.out[r.out.length - 1], 'manager: none');
        expectNoManager(w, r);
      });
    }
  });

  await t.test('(2) tags outside manager/v<core> are ignored', async (st) => {
    for (const [tag, version] of [
      ['manager/v1.2.0-rc.1', '1.2.0-rc.1'],
      ['manager/v01.0.0', '01.0.0'],
      ['managerx/v9.0.0', '9.0.0'],
    ]) {
      await st.test(tag, (s) => {
        const w = managerWorld(s, 'mgr-pattern', [{ tag, latestJson: latestJson(version, 'x/y') }]);
        const r = run(collectArgv(w), plainDeps(w));
        assert.equal(r.code, 0, r.err.join('\n'));
        assert.deepEqual(managerCalls(w), [], 'no manager view');
        assert.deepEqual(report(w).manager, { status: 'none' });
        assert.equal(fs.existsSync(path.join(w.outDir, 'manager')), false);
      });
    }
  });

  await t.test('(3) the highest manager release by SemVer is copied, and generate stages it', async (st) => {
    for (const [label, order] of [
      ['listed low then high', ['manager/v0.9.0', 'manager/v0.10.0']],
      ['listed high then low', ['manager/v0.10.0', 'manager/v0.9.0']],
    ]) {
      await st.test(label, (s) => {
        const managers = order.map((tag) => ({ tag, latestJson: latestJson(tag.slice('manager/v'.length), REPO) }));
        const w = managerWorld(s, 'mgr-two', managers);
        const expected = expectedCalls(w, { manager: 'manager/v0.10.0' });
        const r = run(collectArgv(w), plainDeps(w));
        assert.equal(r.code, 0, r.err.join('\n'));
        assert.deepStrictEqual(ghArgvs(w.world), expected);
        assert.deepEqual(managerCalls(w), [
          viewArgv('manager/v0.10.0', w.repo),
          downloadArgv('manager/v0.10.0', w.repo, 'latest.json', managerPath(w.outDir)),
        ]);
        const served = w.world.served.bytes('manager/v0.10.0', 'latest.json');
        assert.ok(fs.readFileSync(managerPath(w.outDir)).equals(served), 'byte-equal to the served asset');
        assert.deepEqual(report(w).manager, { status: 'copied', tag: 'manager/v0.10.0' });
        assert.equal(r.out[r.out.length - 1], 'manager: copied manager/v0.10.0');

        const g = generateAfter(w);
        assert.equal(g.code, 0, g.err.join('\n'));
        assert.equal(g.argvs.length, 2);
        for (const argv of g.argvs) {
          const i = argv.indexOf('--manager-latest-url');
          assert.ok(i >= 0 && argv[i + 1] === MANAGER_URL, argv.join(' '));
        }
        assert.ok(fs.readFileSync(path.join(g.siteDir, 'manager', 'latest.json')).equals(served));
        const feed = JSON.parse(readText(path.join(g.siteDir, 'versions.json')));
        assert.equal(feed.manager.latest_json_url, MANAGER_URL);
      });
    }
  });

  await t.test('(4) a latest.json version with one leading v is accepted', (s) => {
    const text = latestJson('0.2.0', REPO, (doc) => {
      doc.version = 'v0.2.0';
    });
    const w = managerWorld(s, 'mgr-v', [{ tag: MANAGER_TAG, latestJson: text }]);
    const r = run(collectArgv(w), plainDeps(w));
    assert.equal(r.code, 0, r.err.join('\n'));
    assert.deepEqual(report(w).manager, { status: 'copied', tag: MANAGER_TAG });
  });

  await t.test('(5) every FR-025 failure degrades: exit 3, WARN E_MANAGER_LATEST, nothing of manager/ left', async (st) => {
    const repo = REPO;
    const bad = [
      ['not JSON', '{"version": "0.2.0",\n'],
      ['not an object', '[]\n'],
      [
        'version not the tag',
        latestJson('0.2.0', repo, (d) => {
          d.version = '0.3.0';
        }),
      ],
      [
        'version not a string',
        latestJson('0.2.0', repo, (d) => {
          d.version = 2;
        }),
      ],
      [
        'no platforms',
        latestJson('0.2.0', repo, (d) => {
          delete d.platforms;
        }),
      ],
      [
        'empty platforms',
        latestJson('0.2.0', repo, (d) => {
          d.platforms = {};
        }),
      ],
      [
        'a platform without signature',
        latestJson('0.2.0', repo, (d) => {
          delete d.platforms['windows-x86_64'].signature;
        }),
      ],
      [
        'an http:// URL',
        latestJson('0.2.0', repo, (d) => {
          d.platforms['windows-x86_64'].url = 'http://example.invalid/m.exe';
        }),
      ],
      [
        'a /releases/latest/ URL',
        latestJson('0.2.0', repo, (d) => {
          d.platforms['windows-x86_64'].url = `https://github.com/${repo}/releases/latest/download/m.exe`;
        }),
      ],
    ];
    for (const [label, text] of bad) {
      await st.test(label, (s) => {
        const w = managerWorld(s, 'mgr-bad', [{ tag: MANAGER_TAG, latestJson: text }]);
        const r = run(collectArgv(w), plainDeps(w));
        assert.equal(r.code, 3, r.err.join('\n'));
        const warns = r.err.filter((l) => l.startsWith('feed-publish: WARN E_MANAGER_LATEST: '));
        assert.equal(warns.length, 1, r.err.join('\n'));
        assert.equal(fs.existsSync(path.join(w.outDir, 'manager')), false, 'no manager/ left');
        const m = report(w).manager;
        assert.equal(m.status, 'degraded');
        assert.equal(m.tag, MANAGER_TAG);
        assert.equal(typeof m.message, 'string');
        assert.ok(warns[0].endsWith(m.message), `${warns[0]} / ${m.message}`);
        assert.equal(r.out[r.out.length - 1], `manager: degraded ${MANAGER_TAG}: ${m.message}`);
        // Every other output is written.
        for (const name of ['releases.json', 'manifests', 'collect-report.json']) {
          assert.ok(fs.existsSync(path.join(w.outDir, name)), name);
        }
        const g = generateAfter(w);
        assert.equal(g.code, 0, g.err.join('\n'));
        for (const argv of g.argvs) assert.ok(!argv.includes('--manager-latest-url'), argv.join(' '));
        assert.equal(fs.existsSync(path.join(g.siteDir, 'manager')), false);
      });
    }
  });

  await t.test('(6) --annotate prints one single-line feed-collect notice', (s) => {
    const sel = collectWorld(s, 'ann-sel');
    let r = run(collectArgv(sel, true), plainDeps(sel));
    assert.equal(r.code, 0, r.err.join('\n'));
    assert.equal(r.out[r.out.length - 1], '::notice title=feed-collect::4 downloaded, 14 no-manifest, 0 no-stable, manager copied');
    assert.equal(r.out.filter((l) => l.startsWith('::')).length, 1);

    const repo = sel.repo;
    const text = latestJson('0.2.0', repo, (d) => {
      d.version = 'line one\nline two';
    });
    const w = managerWorld(s, 'ann-bad', [{ tag: MANAGER_TAG, latestJson: text }]);
    r = run(collectArgv(w, true), plainDeps(w));
    assert.equal(r.code, 3);
    assert.equal(
      r.out[r.out.length - 1],
      '::notice title=feed-collect::0 downloaded, 18 no-manifest, 0 no-stable, manager degraded',
    );
    assert.ok(![...r.out, ...r.err].some((l) => l.startsWith('line two')), 'the value is quoted, never raw');
  });

  // Phase 11 FR-043 (plan §4.9, PD-17): every platforms.*.url is a per-tag release asset URL of collect's --repo.
  await t.test('(7) only a per-tag release asset URL of the repo is copied', async (st) => {
    const repo = REPO;
    const withUrl = (url) =>
      latestJson('0.2.0', repo, (d) => {
        d.platforms['windows-x86_64'].url = url;
      });
    const bad = [
      ['an api.github.com asset URL', 'https://api.github.com/repos/o/r/releases/assets/1'],
      ['a foreign host', `https://example.invalid/${repo}/releases/download/${MANAGER_TAG}/m.exe`],
      ['another tag', `https://github.com/${repo}/releases/download/manager/v0.1.0/m.exe`],
    ];
    for (const [label, url] of bad) {
      await st.test(label, (s) => {
        const w = managerWorld(s, 'mgr-pertag-bad', [{ tag: MANAGER_TAG, latestJson: withUrl(url) }]);
        const r = run(collectArgv(w), plainDeps(w));
        assert.equal(r.code, 3, r.err.join('\n'));
        assert.equal(fs.existsSync(path.join(w.outDir, 'manager')), false, 'nothing copied');
        const m = report(w).manager;
        assert.equal(m.status, 'degraded');
        assert.equal(m.tag, MANAGER_TAG);
        assert.ok(m.message.includes(`is not a per-tag release asset URL of ${repo}`), m.message);
        const warns = r.err.filter((l) => l.startsWith('feed-publish: WARN E_MANAGER_LATEST: '));
        assert.equal(warns.length, 1, r.err.join('\n'));
      });
    }

    await st.test('a per-tag github.com URL is copied', (s) => {
      const text = withUrl(`https://github.com/${repo}/releases/download/${MANAGER_TAG}/x`);
      const w = managerWorld(s, 'mgr-pertag-ok', [{ tag: MANAGER_TAG, latestJson: text }]);
      const r = run(collectArgv(w), plainDeps(w));
      assert.equal(r.code, 0, r.err.join('\n'));
      assert.deepEqual(report(w).manager, { status: 'copied', tag: MANAGER_TAG });
      assert.ok(fs.readFileSync(managerPath(w.outDir)).equals(w.world.served.bytes(MANAGER_TAG, 'latest.json')));
    });

    await st.test('checkLatestJson takes exactly one asset segment after the prefix', () => {
      const { checkLatestJson } = feedPublish;
      assert.equal(typeof checkLatestJson, 'function', 'checkLatestJson is exported');
      const prefix = `https://github.com/${repo}/releases/download/${MANAGER_TAG}/`;
      const bytesOf = (url) => Buffer.from(withUrl(url), 'utf8');
      assert.doesNotThrow(() => checkLatestJson(bytesOf(`${prefix}m.exe`), MANAGER_TAG, '0.2.0', repo));
      for (const name of ['', 'a/m.exe', 'm.exe?x=1', 'm.exe#frag']) {
        assert.throws(
          () => checkLatestJson(bytesOf(`${prefix}${name}`), MANAGER_TAG, '0.2.0', repo),
          (err) =>
            err instanceof contract.ContractError &&
            err.code === 'E_MANAGER_LATEST' &&
            err.message.includes(`is not a per-tag release asset URL of ${repo}`),
          JSON.stringify(name),
        );
      }
      // Another repo's per-tag URL is not this repo's.
      const foreign = bytesOf(`https://github.com/o/r/releases/download/${MANAGER_TAG}/m.exe`);
      assert.throws(
        () => checkLatestJson(foreign, MANAGER_TAG, '0.2.0', repo),
        (err) => err instanceof contract.ContractError && err.code === 'E_MANAGER_LATEST',
      );
    });
  });
});
