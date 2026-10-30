'use strict';
require('./helpers/write-guard.js');

// gen-feed rejections and degrades (spec SC-010, SC-022 to SC-025, SC-027; FR-043, FR-044, FR-046, FR-049; plan §4.5.4,
// §4.7, PD-8, PD-10, PD-13, PD-14, PD-16). Every case directory under fixtures/gen-feed/ (inputs built by
// fixtures/make-fixtures.js, goldens written once by gen-feed.js and reviewed by hand, fixtures/README.md) is read in
// place; every run goes through the exported main() in-process (helpers/fixtures.js runMain), outputs in fs.mkdtemp dirs.
//
// - `gen-feed rejects` (SC-010): abort class, exit 1, nothing written; usage, exit 2; the PD-16 seams.
// - `channel comes from the release list only` (SC-022 b-d).
// - `gen-feed degrades a bad manifest` (SC-023): degrade class, exit 3, the feed still written, the plugin v1-only.
// - `install cross-check is gated on version` (SC-024 a-d).
// - `windows_uninstall_key equals the setup.iss AppId` (SC-025).
// - `notes_markdown is one uncapped section` (SC-027).

const test = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { FIXTURES, readJson, loadCase, runMain, v2Argv } = require('./helpers/fixtures.js');
const { buildFeed, extractAppId, readPluginsDir } = require('../../gen-feed.js');
const { checkAssembledEntry, validateFeed } = require('../contract.js');

const GEN_FEED_DIR = path.join(FIXTURES, 'gen-feed');
const bytewise = (a, b) => Buffer.compare(Buffer.from(a, 'utf8'), Buffer.from(b, 'utf8'));

// spec SC-010 / FR-059 "rejections": case -> the abort code its first stderr line carries (tasks T016 "Cases").
const REJECTS = Object.freeze({
  'reject-v1-type': 'E_V1_TYPE',
  'reject-url-latest': 'E_URL_LATEST',
  'reject-url-http': 'E_URL_NOT_HTTPS',
  'reject-tag-suffix': 'E_TAG_FORMAT',
  'reject-tag-ten-digits': 'E_TAG_FORMAT',
  'reject-plugin-name': 'E_PLUGIN_NAME',
  'reject-reserved-name': 'E_PLUGIN_NAME',
  'reject-duplicate-plugin': 'E_DUPLICATE_PLUGIN',
  'duplicate-plugin-case': 'E_DUPLICATE_PLUGIN',
  'reject-input-ls-remote': 'E_INPUT',
  'reject-input-releases': 'E_INPUT',
  'reject-abort-plus-degrade': 'E_V1_TYPE',
});

// spec SC-023 / FR-059 "degrades" (Clarifications 2026-10-30 Q-A DEGRADE, Q-C): case -> its WARN rows, in stderr order.
// A row's `file`, when given, must also be named by its line; `plugin: null` is an unattributed WARN (PD-8).
const ISS = 'alpha/installers/windows/setup.iss';
const w = (code, plugin = 'Alpha', file) => (file === undefined ? { code, plugin } : { code, plugin, file });
const DEGRADES = Object.freeze({
  'degrade-preset-subdir': [w('E_MANIFEST_MISMATCH')],
  'degrade-pkg-id': [w('E_MANIFEST_MISMATCH')],
  'degrade-uninstall-key': [w('E_MANIFEST_MISMATCH', 'Alpha', ISS)],
  'degrade-no-appid': [w('E_MANIFEST_MISMATCH', 'Alpha', ISS)],
  'degrade-no-setup-iss': [w('E_MANIFEST_MISMATCH', 'Alpha', ISS)],
  'degrade-install-slash': [w('E_INSTALL_VALUE')],
  'degrade-install-backslash': [w('E_INSTALL_VALUE')],
  'degrade-install-dotdot': [w('E_INSTALL_VALUE')],
  'degrade-signature-text': [w('E_SIGNATURE_TEXT')],
  'degrade-tc-fields': [w('E_TC_FIELDS')],
  'degrade-tc-name': [w('E_TC_NAME')],
  'degrade-tc-version': [w('E_TC_VERSION')],
  'degrade-tc-platform': [w('E_TC_PLATFORM')],
  'degrade-tc-asset': [w('E_TC_ASSET')],
  'degrade-tc-binding': [w('E_TC_BINDING')],
  'degrade-listing-dotdot': [w('E_LISTING_DOTDOT')],
  'degrade-listing-not-canonical': [w('E_LISTING_NOT_CANONICAL')],
  'degrade-listing-count': [w('E_LISTING_COUNT')],
  'degrade-listing-digest': [w('E_LISTING_DIGEST')],
  'degrade-listing-path': [w('E_LISTING_PATH')],
  'degrade-listing-absolute': [w('E_LISTING_ABSOLUTE')],
  'degrade-listing-drive': [w('E_LISTING_DRIVE')],
  'degrade-listing-backslash': [w('E_LISTING_BACKSLASH')],
  'degrade-listing-duplicate': [w('E_LISTING_DUPLICATE')],
  'degrade-listing-root-symlink': [w('E_LISTING_ROOT_SYMLINK')],
  'degrade-listing-symlink-form': [w('E_LISTING_SYMLINK_FORM')],
  'degrade-listing-symlink-escape': [w('E_LISTING_SYMLINK_ESCAPE')],
  'degrade-listing-symlink-chain': [w('E_LISTING_SYMLINK_CHAIN')],
  'degrade-listing-through-symlink': [w('E_LISTING_THROUGH_SYMLINK')],
  'degrade-layout': [w('E_LAYOUT')],
  'degrade-url-http': [w('E_URL_NOT_HTTPS')],
  'degrade-url-latest': [w('E_URL_LATEST')],
  'degrade-url-not-per-tag': [w('E_URL_NOT_PER_TAG')],
  'degrade-sibling-manifest': [w('E_TC_BINDING')],
  'degrade-c2-missing-pkg-id': [w('E_MANIFEST_SCHEMA')],
  'degrade-c2-wrong-format': [w('E_MANIFEST_SCHEMA')],
  'degrade-dup-uninstall-key': [w('E_INSTALL_DUPLICATE', 'Alpha'), w('E_INSTALL_DUPLICATE', 'Bravo')],
  'degrade-dup-pkg-id': [w('E_INSTALL_DUPLICATE', 'Alpha'), w('E_INSTALL_DUPLICATE', 'Bravo')],
  'degrade-copied-bundle-name': [w('E_MANIFEST_SCHEMA', 'Bravo')],
  'degrade-manifest-unparseable': [w('E_MANIFEST_SCHEMA', null, 'manifests/broken.json')],
  'degrade-manifest-duplicate-tag': [w('E_MANIFEST_DUPLICATE')],
});

/** Every case directory (one holding case.json), bytewise by name. */
function allCases() {
  return fs
    .readdirSync(GEN_FEED_DIR, { withFileTypes: true })
    .filter((d) => d.isDirectory() && fs.existsSync(path.join(GEN_FEED_DIR, d.name, 'case.json')))
    .map((d) => d.name)
    .sort(bytewise);
}
const groupOf = (group) => allCases().filter((name) => loadCase(name).caseJson.group === group);

/** A temp dir for the duration of fn; removed afterwards (also when fn throws). */
function withTmp(prefix, fn) {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  try {
    return fn(tmp);
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
}

const sha256 = (buf) => crypto.createHash('sha256').update(buf).digest('hex');
const tmpLeftovers = (dir) => fs.readdirSync(dir).filter((n) => n.endsWith('.tmp'));
const firstLine = (s) => s.split('\n')[0];
const stderrLines = (s) => s.split('\n').filter((l) => l !== '');

/**
 * Runs main on a case into <tmp>/out.json: v2 mode, or legacy for a case whose modes are ["legacy"]. With `preexisting`,
 * the out file is written first. Returns {code, stdout, stderr, out, bytes} (bytes null when no out file exists).
 */
function runCase(info, tmp, { mode, preexisting, extra = [], hooks } = {}) {
  const out = path.join(tmp, 'out.json');
  if (preexisting !== undefined) fs.writeFileSync(out, preexisting);
  const runMode = mode ?? (info.caseJson.modes.includes('v2') ? 'v2' : 'legacy');
  let argv;
  if (runMode === 'v2') {
    const empty = path.join(tmp, 'empty-manifests');
    fs.mkdirSync(empty, { recursive: true });
    argv = [...v2Argv(info, out, empty), ...extra];
  } else {
    argv = ['--legacy', '--plugins-dir', info.pluginsDir, '--tags', info.tagsPath, '--out', out, ...extra];
  }
  const r = runMain(argv, hooks);
  return { ...r, out, bytes: fs.existsSync(out) ? fs.readFileSync(out) : null };
}

/** The manifests/*.json of a case as main reads them: bytewise, labelled manifests/<f>. */
function manifestFiles(info) {
  if (info.manifestsDir === null) return [];
  return fs
    .readdirSync(info.manifestsDir)
    .filter((n) => n.endsWith('.json'))
    .sort(bytewise)
    .map((n) => ({ file: `manifests/${n}`, text: fs.readFileSync(path.join(info.manifestsDir, n), 'utf8') }));
}

/** The tag of a manifest text, or undefined when it does not parse to an object with a string tag. */
function tagOf(text) {
  try {
    const doc = JSON.parse(text);
    return doc !== null && typeof doc === 'object' && typeof doc.tag === 'string' ? doc.tag : undefined;
  } catch {
    return undefined;
  }
}

/** The pure-core inputs of a case; `withoutPlugin` drops every manifest whose tag is that plugin's (`<name lc>/v`). */
function caseInputs(info, { withoutPlugin } = {}) {
  let manifests = manifestFiles(info);
  if (withoutPlugin !== undefined) {
    const prefix = `${withoutPlugin.toLowerCase()}/v`;
    manifests = manifests.filter((m) => !(tagOf(m.text) ?? '').startsWith(prefix));
  }
  const inputs = {
    plugins: readPluginsDir(info.pluginsDir, { withSetupIss: true }),
    releases: JSON.parse(fs.readFileSync(info.releasesPath, 'utf8')),
    manifests,
    repo: info.caseJson.repo,
  };
  if (info.caseJson.managerLatestUrl !== undefined) inputs.managerLatestUrl = info.caseJson.managerLatestUrl;
  return inputs;
}

/** A case copied into tmp, with its paths re-pointed (for in-test edits that must not touch the fixture). */
function copyCase(info, tmp) {
  const dir = path.join(tmp, 'case');
  fs.cpSync(info.dir, dir, { recursive: true });
  return {
    ...info,
    dir,
    pluginsDir: path.join(dir, 'plugins'),
    releasesPath: path.join(dir, 'releases.json'),
    tagsPath: path.join(dir, 'tags.txt'),
    manifestsDir: info.manifestsDir === null ? null : path.join(dir, 'manifests'),
  };
}

/** Asserts stderr is exactly the WARN rows, in order, each line naming the plugin (or, for plugin null, the file). */
function assertWarnLines(name, stderr, rows) {
  const lines = stderrLines(stderr);
  assert.equal(lines.length, rows.length, `${name}: stderr lines ${JSON.stringify(lines)}`);
  rows.forEach((row, i) => {
    const line = lines[i];
    assert.ok(line.startsWith(`gen-feed: WARN ${row.code}: `), `${name}: line ${i + 1} ${JSON.stringify(line)} is not WARN ${row.code}`);
    if (row.plugin !== null) assert.ok(line.includes(`plugin ${row.plugin}`), `${name}: line ${i + 1} does not name ${row.plugin}`);
    if (row.file !== undefined) assert.ok(line.includes(row.file), `${name}: line ${i + 1} does not name ${row.file}`);
  });
}

const PREEXISTING = 'pre-existing --out content, must survive an aborted run\n';

test('gen-feed rejects', async (t) => {
  const rejects = groupOf('reject');
  await t.test('the reject group is exactly the SC-010 cases', () => {
    assert.deepEqual([...rejects].sort(bytewise), Object.keys(REJECTS).sort(bytewise));
  });
  for (const name of Object.keys(REJECTS)) {
    await t.test(name, () =>
      withTmp('km1-rej-', (tmp) => {
        const info = loadCase(name);
        const code = REJECTS[name];
        assert.equal(info.caseJson.exit, 1, `${name}: case.json exit`);
        assert.equal(info.caseJson.error, code, `${name}: case.json error`);
        const r = runCase(info, tmp, { preexisting: PREEXISTING });
        assert.equal(r.code, 1, `${name}: exit (stderr ${JSON.stringify(r.stderr)})`);
        assert.equal(r.stdout, '', `${name}: stdout`);
        const expected = fs.readFileSync(path.join(info.dir, 'expected.error'), 'utf8');
        assert.equal(firstLine(r.stderr) + '\n', expected, `${name}: first stderr line`);
        assert.ok(expected.startsWith(`gen-feed: ${code}: `), `${name}: expected.error does not start with ${code}`);
        assert.ok(!r.stderr.includes('WARN'), `${name}: an aborted run prints no WARN line`);
        assert.equal(r.bytes.toString('utf8'), PREEXISTING, `${name}: the pre-existing --out file changed`);
        assert.deepEqual(tmpLeftovers(tmp), [], `${name}: a temp file is left`);
      }),
    );
  }
  await t.test('a usage error exits 2, prints the usage line and writes nothing', () =>
    withTmp('km1-rej-usage-', (tmp) => {
      const out = path.join(tmp, 'out.json');
      const r = runMain(['--no-such-flag', '--out', out]);
      assert.equal(r.code, 2);
      assert.ok(firstLine(r.stderr).startsWith('gen-feed: E_USAGE: '), r.stderr);
      assert.ok(r.stderr.includes('usage: node tools/gen-feed.js --plugins-dir <dir>'), 'no usage line');
      assert.equal(r.stdout, '');
      assert.deepEqual(fs.readdirSync(tmp), [], 'something was written');
    }),
  );
  for (const [label, url] of [
    ['http', 'http://rolandzwaga.github.io/krate-audio/manager/latest.json'],
    ['releases-latest', 'https://github.com/rolandzwaga/krate-audio/releases/latest/download/manager/latest.json'],
    ['wrong-suffix', 'https://rolandzwaga.github.io/krate-audio/manager/feed.json'],
  ]) {
    await t.test(`--manager-latest-url ${label} is E_INPUT (PD-16)`, () =>
      withTmp('km1-rej-mgr-', (tmp) => {
        const r = runCase(loadCase('clean-basic'), tmp, { extra: ['--manager-latest-url', url] });
        assert.equal(r.code, 1, r.stderr);
        assert.ok(firstLine(r.stderr).startsWith('gen-feed: E_INPUT: '), r.stderr);
        assert.equal(r.bytes, null, 'nothing may be written');
        assert.deepEqual(tmpLeftovers(tmp), []);
      }),
    );
  }
  await t.test('E_OUTPUT_SCHEMA: the self-check sees the document that would be written (PD-16)', () =>
    withTmp('km1-rej-self-', (tmp) => {
      let seen = 0;
      const hooks = {
        beforeSelfCheck: (doc) => {
          seen += 1;
          doc.plugins.Alpha.release_notes = 'x';
        },
      };
      const r = runCase(loadCase('clean-basic'), tmp, { preexisting: PREEXISTING, hooks });
      assert.equal(seen, 1, 'the hook ran once');
      assert.equal(r.code, 1, r.stderr);
      assert.ok(firstLine(r.stderr).startsWith('gen-feed: E_OUTPUT_SCHEMA: '), r.stderr);
      assert.equal(r.bytes.toString('utf8'), PREEXISTING, 'the pre-existing --out file changed');
      assert.deepEqual(tmpLeftovers(tmp), []);
    }),
  );
  await t.test('--project-v1 of a v2 feed is its legacy feed', () => {
    const info = loadCase('clean-basic');
    const r = runMain(['--project-v1', path.join(info.dir, 'expected.v2.json')]);
    assert.equal(r.code, 0, r.stderr);
    assert.equal(r.stderr, '');
    assert.equal(r.stdout, fs.readFileSync(path.join(info.dir, 'expected.legacy.json'), 'utf8'));
  });
  for (const [label, text] of [
    ['malformed JSON', '{"schema_version": 2, "plugins": {\n'],
    ['no plugins object', '{"schema_version": 2}\n'],
  ]) {
    await t.test(`--project-v1 on ${label} is E_INPUT`, () =>
      withTmp('km1-rej-proj-', (tmp) => {
        const feed = path.join(tmp, 'feed.json');
        fs.writeFileSync(feed, text);
        const r = runMain(['--project-v1', feed]);
        assert.equal(r.code, 1, r.stderr);
        assert.ok(firstLine(r.stderr).startsWith('gen-feed: E_INPUT: '), r.stderr);
        assert.equal(r.stdout, '');
      }),
    );
  }
  await t.test('assembled entry failing C1 degrades', () => {
    // The PD-10 backstop: a darwin-universal platform without au_component (FR-008), built by hand from the full-v2
    // fixture's Charlie, which no C2-valid manifest can produce.
    const entry = structuredClone(readJson('feeds/valid/full-v2.json').plugins.Charlie);
    assert.ok(entry.platforms['darwin-universal'], 'the base entry has a darwin-universal platform');
    delete entry.install.au_component;
    assert.throws(
      () => checkAssembledEntry('Charlie', entry),
      (err) => err.code === 'E_MANIFEST_SCHEMA' && err.plugin === 'Charlie' && err.instancePath === '/plugins/Charlie/install',
    );
    entry.install.au_component = 'Charlie.component';
    checkAssembledEntry('Charlie', entry);
  });
});

test('channel comes from the release list only', async (t) => {
  await t.test('(b) a plain-version prerelease is a beta sibling', () =>
    withTmp('km1-chan-b-', (tmp) => {
      const r = runCase(loadCase('pre-plain'), tmp);
      assert.equal(r.code, 0, r.stderr);
      assert.equal(r.stderr, '');
      const entry = JSON.parse(r.bytes.toString('utf8')).plugins.Alpha;
      assert.equal(entry.version, '1.1.0');
      assert.equal(entry.channel, 'stable');
      assert.equal(entry.prerelease.tag, 'alpha/v1.2.0');
      assert.equal(entry.prerelease.channel, 'beta');
      assert.ok(entry.prerelease.platforms, 'the sibling carries the manifest platforms');
    }),
  );
  await t.test('(c) promotion: the same manifest bytes become the stable entry', () =>
    withTmp('km1-chan-c-', (tmp) => {
      const info = loadCase('pre-plain-promoted');
      const manifestPath = path.join(info.manifestsDir, 'alpha-1.2.0.json');
      const before = sha256(fs.readFileSync(manifestPath));
      assert.equal(before, sha256(fs.readFileSync(path.join(loadCase('pre-plain').manifestsDir, 'alpha-1.2.0.json'))), 'pre-plain and pre-plain-promoted manifests differ');
      const r = runCase(info, tmp);
      assert.equal(r.code, 0, r.stderr);
      assert.ok(!r.stderr.includes('WARN'), r.stderr);
      assert.equal(sha256(fs.readFileSync(manifestPath)), before, 'the run changed the manifest file');
      const entry = JSON.parse(r.bytes.toString('utf8')).plugins.Alpha;
      assert.equal(entry.version, '1.2.0');
      assert.equal(entry.channel, 'stable');
      assert.ok(!Object.hasOwn(entry, 'prerelease'), 'no sibling after promotion');
      const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
      assert.deepEqual(Object.keys(entry.platforms), Object.keys(manifest.assets).sort(bytewise));
      for (const [key, asset] of Object.entries(manifest.assets)) {
        const p = entry.platforms[key];
        for (const field of ['url', 'size', 'sha256', 'signature', 'format']) assert.equal(p[field], asset[field], `${key}.${field}`);
        assert.ok(!Object.hasOwn(p, 'name'), `${key}: platforms carry no asset name`);
        if (asset.payload) assert.deepEqual(p.payload, { files: asset.payload.files, sha256: asset.payload.sha256 });
      }
      assert.deepEqual(entry.install, manifest.install);
    }),
  );
  await t.test('(d) a suffixed non-prerelease is E_TAG_FORMAT', () =>
    withTmp('km1-chan-d-', (tmp) => {
      const r = runCase(loadCase('reject-tag-suffix'), tmp);
      assert.equal(r.code, 1, r.stderr);
      assert.ok(firstLine(r.stderr).startsWith('gen-feed: E_TAG_FORMAT: '), r.stderr);
      assert.ok(firstLine(r.stderr).includes('alpha/v1.2.0-rc1'), 'the line names the tag');
      assert.equal(r.bytes, null);
    }),
  );
});

test('gen-feed degrades a bad manifest', async (t) => {
  const degrades = groupOf('degrade');
  await t.test('the degrade group is exactly the SC-023 cases', () => {
    assert.deepEqual([...degrades].sort(bytewise), Object.keys(DEGRADES).sort(bytewise));
  });
  for (const name of Object.keys(DEGRADES)) {
    await t.test(name, () =>
      withTmp('km1-deg-', (tmp) => {
        const info = loadCase(name);
        const rows = DEGRADES[name];
        assert.equal(info.caseJson.exit, 3, `${name}: case.json exit`);
        assert.deepEqual(info.caseJson.warn, rows, `${name}: case.json warn`);
        const r = runCase(info, tmp);
        assert.equal(r.code, 3, `${name}: exit (stderr ${JSON.stringify(r.stderr)})`);
        assertWarnLines(name, r.stderr, rows);
        assert.deepEqual(tmpLeftovers(tmp), [], `${name}: a temp file is left`);
        const doc = JSON.parse(r.bytes.toString('utf8'));
        for (const schema of ['v2', 'v1']) {
          const v = validateFeed(doc, { schema });
          assert.ok(v.valid, `${name}: --out fails ${schema}: ${JSON.stringify(v.errors)}`);
        }
        const golden = fs.readFileSync(path.join(info.dir, 'expected.v2.json'));
        assert.ok(r.bytes.equals(golden), `${name}: --out differs from expected.v2.json`);
        const goldenDoc = JSON.parse(golden.toString('utf8'));
        const degraded = new Set(rows.map((row) => row.plugin).filter((p) => p !== null));
        for (const plugin of degraded) {
          const entry = doc.plugins[plugin];
          assert.ok(entry, `${name}: ${plugin} is missing from the feed`);
          for (const part of [entry, entry.prerelease].filter(Boolean)) {
            assert.ok(!Object.hasOwn(part, 'platforms'), `${name}: ${plugin} keeps platforms`);
            assert.ok(!Object.hasOwn(part, 'install'), `${name}: ${plugin} keeps install`);
          }
          const { doc: without, warnings } = buildFeed(caseInputs(info, { withoutPlugin: plugin }));
          assert.ok(!warnings.some((x) => x.plugin === plugin), `${name}: ${plugin} still warns without its manifests`);
          assert.equal(JSON.stringify(entry), JSON.stringify(without.plugins[plugin]), `${name}: ${plugin} differs from its no-manifest entry`);
        }
        const manifests = manifestFiles(info);
        const healthy = Object.keys(doc.plugins).filter((p) => !degraded.has(p));
        assert.ok(healthy.length > 0, `${name}: no healthy plugin beside the degraded one`);
        for (const plugin of healthy) {
          assert.equal(JSON.stringify(doc.plugins[plugin]), JSON.stringify(goldenDoc.plugins[plugin]), `${name}: healthy ${plugin}`);
          const owns = manifests.some((m) => (tagOf(m.text) ?? '').startsWith(`${plugin.toLowerCase()}/v`));
          if (owns) assert.ok(doc.plugins[plugin].platforms, `${name}: healthy ${plugin} lost its platforms`);
        }
        if (name === 'degrade-manifest-duplicate-tag') {
          const files = manifests.filter((m) => tagOf(m.text) === 'alpha/v1.2.0').map((m) => m.file);
          assert.equal(files.length, 2, `${name}: two files claim alpha/v1.2.0`);
          for (const file of files) assert.ok(r.stderr.includes(file), `${name}: the WARN line does not name ${file}`);
        }
        const legacy = runCase(info, tmp, { mode: 'legacy' });
        assert.equal(legacy.code, 0, `${name}: --legacy (${legacy.stderr})`);
        const projection = runMain(['--project-v1', r.out]);
        assert.equal(projection.code, 0, `${name}: --project-v1 (${projection.stderr})`);
      }),
    );
  }
  await t.test('a clean run returns 0 with no WARN line', () =>
    withTmp('km1-deg-clean-', (tmp) => {
      const r = runCase(loadCase('clean-basic'), tmp);
      assert.equal(r.code, 0, r.stderr);
      assert.equal(r.stderr, '');
    }),
  );
  await t.test('abort beats degrade: nothing written (reject-abort-plus-degrade)', () =>
    withTmp('km1-deg-abort-', (tmp) => {
      const info = loadCase('reject-abort-plus-degrade');
      assert.ok(info.manifestsDir !== null, 'the case holds a manifest that would degrade');
      const r = runCase(info, tmp, { preexisting: PREEXISTING });
      assert.equal(r.code, 1, r.stderr);
      assert.ok(!r.stderr.includes('WARN'), r.stderr);
      assert.equal(r.bytes.toString('utf8'), PREEXISTING);
      assert.deepEqual(tmpLeftovers(tmp), []);
    }),
  );
});

test('install cross-check is gated on version', async (t) => {
  for (const [name, field] of [
    ['degrade-preset-subdir', 'install.preset_subdir'],
    ['degrade-pkg-id', 'install.macos_pkg_id'],
  ]) {
    await t.test(`(a) ${name}: equal versions, ${field} differs`, () =>
      withTmp('km1-gate-a-', (tmp) => {
        const info = loadCase(name);
        const versionJson = JSON.parse(fs.readFileSync(path.join(info.pluginsDir, 'alpha', 'version.json'), 'utf8'));
        const manifest = JSON.parse(fs.readFileSync(path.join(info.manifestsDir, 'alpha-1.2.0.json'), 'utf8'));
        assert.equal(versionJson.version, manifest.version, `${name}: the versions must be equal`);
        const r = runCase(info, tmp);
        assert.equal(r.code, 3, r.stderr);
        assert.equal(stderrLines(r.stderr).length, 1, r.stderr);
        assert.ok(r.stderr.startsWith('gen-feed: WARN E_MANIFEST_MISMATCH: '), r.stderr);
        assert.ok(r.stderr.includes(field), `${name}: the WARN line does not name ${field}`);
      }),
    );
  }
  await t.test('(b) clean-version-ahead: version.json ahead, check 1 skipped', () =>
    withTmp('km1-gate-b-', (tmp) => {
      const info = loadCase('clean-version-ahead');
      const versionJson = JSON.parse(fs.readFileSync(path.join(info.pluginsDir, 'alpha', 'version.json'), 'utf8'));
      const manifest = JSON.parse(fs.readFileSync(path.join(info.manifestsDir, 'alpha-1.2.0.json'), 'utf8'));
      assert.notEqual(versionJson.version, manifest.version);
      assert.notEqual(versionJson.preset_subdir, manifest.install.preset_subdir, 'preset_subdir must differ');
      assert.notEqual(versionJson.identifier, manifest.install.macos_pkg_id, 'identifier must differ');
      const r = runCase(info, tmp);
      assert.equal(r.code, 0, r.stderr);
      assert.equal(r.stderr, '');
      assert.deepEqual(JSON.parse(r.bytes.toString('utf8')).plugins.Alpha.install, manifest.install);
    }),
  );
  await t.test('(c) pre-newer: a prerelease-suffixed sibling manifest is skipped the same way', () =>
    withTmp('km1-gate-c-', (tmp) => {
      const info = loadCase('pre-newer');
      const versionJson = JSON.parse(fs.readFileSync(path.join(info.pluginsDir, 'alpha', 'version.json'), 'utf8'));
      const manifest = JSON.parse(fs.readFileSync(path.join(info.manifestsDir, 'alpha-1.2.0-beta.1.json'), 'utf8'));
      assert.notEqual(versionJson.preset_subdir, manifest.install.preset_subdir, 'preset_subdir must differ');
      const r = runCase(info, tmp);
      assert.equal(r.code, 0, r.stderr);
      assert.equal(r.stderr, '');
      assert.deepEqual(JSON.parse(r.bytes.toString('utf8')).plugins.Alpha.prerelease.install, manifest.install);
    }),
  );
  await t.test('(d) clean-version-ahead with a wrong windows_uninstall_key still fails check 2', () =>
    withTmp('km1-gate-d-', (tmp) => {
      const info = copyCase(loadCase('clean-version-ahead'), tmp);
      const manifestPath = path.join(info.manifestsDir, 'alpha-1.2.0.json');
      const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
      manifest.install.windows_uninstall_key = '{0A1F0000-0000-4000-8000-000000000099}_is1';
      fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2) + '\n');
      const r = runCase(info, tmp);
      assert.equal(r.code, 3, r.stderr);
      assert.ok(r.stderr.startsWith('gen-feed: WARN E_MANIFEST_MISMATCH: '), r.stderr);
      assert.ok(r.stderr.includes('install.windows_uninstall_key'), r.stderr);
      assert.ok(r.stderr.includes(ISS), r.stderr);
    }),
  );
});

test('windows_uninstall_key equals the setup.iss AppId', async (t) => {
  await t.test('(a) the nine snapshot setup.iss copies give nine distinct GUIDs', () => {
    const plugins = path.join(GEN_FEED_DIR, 'snapshot-2026-10-29', 'plugins');
    const ids = fs
      .readdirSync(plugins)
      .sort(bytewise)
      .map((dir) => extractAppId(fs.readFileSync(path.join(plugins, dir, 'installers', 'windows', 'setup.iss'), 'utf8')));
    assert.equal(ids.length, 9);
    for (const id of ids) {
      assert.match(String(id), /^\{[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}\}$/u);
    }
    assert.equal(new Set(ids).size, 9, `not distinct: ${ids.join(', ')}`);
  });
  await t.test('(b) a manifest key equal to its own setup.iss AppId passes (clean-basic)', () =>
    withTmp('km1-appid-b-', (tmp) => {
      const info = loadCase('clean-basic');
      const appId = extractAppId(fs.readFileSync(path.join(info.pluginsDir, 'alpha', 'installers', 'windows', 'setup.iss'), 'utf8'));
      const r = runCase(info, tmp);
      assert.equal(r.code, 0, r.stderr);
      assert.equal(JSON.parse(r.bytes.toString('utf8')).plugins.Alpha.install.windows_uninstall_key, `${appId}_is1`);
    }),
  );
  for (const name of ['degrade-uninstall-key', 'degrade-no-appid', 'degrade-no-setup-iss']) {
    await t.test(`(c) ${name}`, () =>
      withTmp('km1-appid-c-', (tmp) => {
        const r = runCase(loadCase(name), tmp);
        assert.equal(r.code, 3, r.stderr);
        assertWarnLines(name, r.stderr, [w('E_MANIFEST_MISMATCH', 'Alpha', ISS)]);
      }),
    );
  }
  await t.test("(d) Inno's doubled brace is read as one; a single brace is no AppId line", () => {
    assert.equal(extractAppId('AppId={{0A1B2C3D-0000-4000-8000-00000000000A}\r\n'), '{0A1B2C3D-0000-4000-8000-00000000000A}');
    assert.equal(extractAppId('[Setup]\nAppId={0A1B2C3D-0000-4000-8000-00000000000A}\n'), undefined);
  });
});

/** Every subschema reachable from `schema` (local $refs followed once each). */
function subschemas(schema, root, seen = new Set()) {
  const out = [];
  const visit = (node) => {
    if (node === null || typeof node !== 'object' || seen.has(node)) return;
    seen.add(node);
    out.push(node);
    if (typeof node.$ref === 'string' && node.$ref.startsWith('#/')) {
      visit(node.$ref.slice(2).split('/').reduce((o, k) => o?.[k], root));
    }
    for (const value of Object.values(node)) visit(value);
  };
  visit(schema);
  return out;
}

test('notes_markdown is one uncapped section', async (t) => {
  /** The `## [<version>]` section of a CHANGELOG text: from after its header line to the next `\n## [`, trimmed. */
  const sectionOf = (text, version) => {
    const start = text.indexOf(`\n## [${version}]`);
    assert.ok(start >= 0, `no ${version} section`);
    const bodyStart = text.indexOf('\n', start + 1) + 1;
    const end = text.indexOf('\n## [', bodyStart);
    return text.slice(bodyStart, end < 0 ? text.length : end).trim();
  };
  await t.test('clean-long-section: one section, at least 14,000 bytes, nothing else', () =>
    withTmp('km1-notes-long-', (tmp) => {
      const info = loadCase('clean-long-section');
      const changelog = fs.readFileSync(path.join(info.pluginsDir, 'alpha', 'CHANGELOG.md'), 'utf8');
      const r = runCase(info, tmp);
      assert.equal(r.code, 0, r.stderr);
      const notes = JSON.parse(r.bytes.toString('utf8')).plugins.Alpha.notes_markdown;
      assert.equal(typeof notes, 'string');
      assert.equal(notes, sectionOf(changelog, '1.2.0'));
      assert.ok(Buffer.byteLength(notes, 'utf8') >= 14000, `${Buffer.byteLength(notes, 'utf8')} bytes`);
      assert.ok(!notes.includes('\r'), 'notes hold a CR');
      for (const other of ['1.3.0', '1.1.0']) {
        assert.ok(!notes.includes(`## [${other}]`), `notes hold the ${other} header`);
        assert.ok(!notes.includes(sectionOf(changelog, other)), `notes hold the ${other} section`);
      }
    }),
  );
  await t.test('pre-newer: the sibling carries its own version section', () =>
    withTmp('km1-notes-sib-', (tmp) => {
      const info = loadCase('pre-newer');
      const changelog = fs.readFileSync(path.join(info.pluginsDir, 'alpha', 'CHANGELOG.md'), 'utf8');
      const r = runCase(info, tmp);
      assert.equal(r.code, 0, r.stderr);
      const entry = JSON.parse(r.bytes.toString('utf8')).plugins.Alpha;
      assert.equal(entry.notes_markdown, sectionOf(changelog, '1.1.0'));
      assert.equal(entry.prerelease.notes_markdown, sectionOf(changelog, '1.2.0-beta.1'));
      assert.notEqual(entry.notes_markdown, entry.prerelease.notes_markdown);
    }),
  );
  await t.test('an array notes_markdown fails C1 (feeds/invalid/notes_markdown-type.json)', () => {
    const v = validateFeed(readJson('feeds/invalid/notes_markdown-type.json'), { schema: 'v2' });
    assert.ok(!v.valid);
    assert.ok(
      v.errors.some((e) => e.instancePath === '/plugins/Alpha/notes_markdown' && e.keyword === 'type'),
      JSON.stringify(v.errors),
    );
  });
  await t.test('no schema caps notes_markdown or allows an array', () => {
    let found = 0;
    for (const file of ['versions.v2.schema.json', 'release.schema.json']) {
      const root = JSON.parse(fs.readFileSync(path.join(__dirname, '..', file), 'utf8'));
      for (const node of subschemas(root, root)) {
        const props = node.properties;
        if (props === null || typeof props !== 'object' || !Object.hasOwn(props, 'notes_markdown')) continue;
        found += 1;
        for (const sub of subschemas(props.notes_markdown, root)) {
          assert.ok(!Object.hasOwn(sub, 'maxLength'), `${file}: notes_markdown has a maxLength`);
          assert.ok(!Object.hasOwn(sub, 'items') && !Object.hasOwn(sub, 'prefixItems'), `${file}: notes_markdown has items`);
          const types = Array.isArray(sub.type) ? sub.type : [sub.type];
          assert.ok(!types.includes('array'), `${file}: notes_markdown allows an array`);
        }
      }
    }
    assert.ok(found >= 1, 'versions.v2.schema.json defines notes_markdown');
  });
});
