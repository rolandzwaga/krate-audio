'use strict';
require('./helpers/write-guard.js');

// `C2 release.json fixtures` (spec SC-006, SC-022a; FR-015 to FR-018, FR-056; plan §4.1, §4.2, §4.7 PD-10, PD-11).
// Reads fixtures/release/cases.json (built by fixtures/make-fixtures.js): every row's manifest goes through
// validateRelease (the C2 schema alone) and then checkReleaseManifest (schema plus the FR-017 cross-field rules, in
// the plan §4.2 order); the row set must cover every rule id in C2_RULES (no rule without an error row, no row naming
// an id outside the list), so dropping a schema rule, a code rule or a fixture row each turn this test red.
// Part `projection` (C2 ⊇ C1): the entry gen-feed's assembleEntry builds from each valid manifest passes
// checkAssembledEntry.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const { validateRelease, checkReleaseManifest, checkAssembledEntry, isStableCore, ContractError } = require('../contract.js');

const RELEASE_DIR = path.join(__dirname, '..', 'fixtures', 'release');
const REPO = 'rolandzwaga/krate-audio';

// plan §4.7 "C2 invalid rows": exactly the ids of the table, prefixed with their FR.
const C2_RULES = Object.freeze([
  'FR-015/tag-required',
  'FR-015/name-required',
  'FR-015/version-required',
  'FR-015/pub_date-required',
  'FR-015/assets-required',
  'FR-015/install-required',
  'FR-015/channel-stable',
  'FR-015/channel-beta',
  'FR-015/unknown-key',
  'FR-015/name-reserved',
  'FR-016/missing-asset',
  'FR-016/fourth-asset',
  'FR-016/two-darwin-keys',
  'FR-016/format-windows-pkg',
  'FR-016/format-darwin-inno-exe',
  'FR-016/format-linux-pkg',
  'FR-016/min_os-linux-required',
  'FR-016/min_os-darwin-required',
  'FR-016/payload-tar-gz-required',
  'FR-016/payload-pkg-forbidden',
  'FR-016/list-empty',
  'FR-016/install-missing-au_component',
  'FR-016/install-missing-macos_pkg_id',
  'FR-016/install-missing-uninstall-key',
  'FR-016/install-missing-vst3_bundle',
  'FR-016/install-dotdot',
  'FR-017/tag-mismatch',
  'FR-017/asset-name',
  'FR-017/vst3_bundle-name',
  'FR-017/au_component-name',
  'FR-017/url-other-tag',
  'FR-017/url-latest',
  'FR-017/url-http',
  'FR-017/signature-text',
  'FR-017/binding',
  'FR-017/list-not-canonical',
  'FR-017/list-bad-path',
  'FR-017/files-count',
  'FR-017/digest',
  'FR-017/layout',
]);

// The code the plan §4.7 table gives each id (in brackets there); every id not named here is E_MANIFEST_SCHEMA.
const PLAN_CODE = Object.freeze({
  'FR-016/install-dotdot': 'E_INSTALL_VALUE',
  'FR-017/url-other-tag': 'E_URL_NOT_PER_TAG',
  'FR-017/url-latest': 'E_URL_LATEST',
  'FR-017/url-http': 'E_URL_NOT_HTTPS',
  'FR-017/signature-text': 'E_SIGNATURE_TEXT',
  'FR-017/binding': 'E_TC_BINDING',
  'FR-017/list-not-canonical': 'E_LISTING_NOT_CANONICAL',
  'FR-017/list-bad-path': 'E_LISTING_DOTDOT',
  'FR-017/files-count': 'E_LISTING_COUNT',
  'FR-017/digest': 'E_LISTING_DIGEST',
  'FR-017/layout': 'E_LAYOUT',
});
const planCode = (rule) => PLAN_CODE[rule] ?? 'E_MANIFEST_SCHEMA';

// FR-056's valid manifests (plan §4.7 "Valid C2 rows").
const C2_VALID_RULES = Object.freeze([
  'FR-056/darwin-aarch64',
  'FR-056/darwin-x86_64',
  'FR-056/darwin-universal',
  'FR-056/windows-without-min_os',
  'FR-056/prerelease-suffixed',
  'FR-056/plain-version',
]);

const DARWIN_KEYS = Object.freeze(['darwin-aarch64', 'darwin-x86_64', 'darwin-universal']);

const cases = JSON.parse(fs.readFileSync(path.join(RELEASE_DIR, 'cases.json'), 'utf8'));

const isError = (expect) => expect !== null && typeof expect === 'object' && typeof expect.error === 'string';
const show = (v) => JSON.stringify(v);
const loadDoc = (file) => JSON.parse(fs.readFileSync(path.join(RELEASE_DIR, ...file.split('/')), 'utf8'));
const pairsOf = (result) => (result.errors ?? []).map((e) => `${e.instancePath} ${e.keyword}`);

/** The ContractError checkReleaseManifest throws for doc, or null when it accepts. */
function manifestError(doc, file) {
  try {
    checkReleaseManifest(doc, { repo: REPO, file });
    return null;
  } catch (err) {
    if (!(err instanceof ContractError)) throw err;
    return err;
  }
}

test('C2 release.json fixtures', async (t) => {
  await t.test('every cases.json row yields its expected result', () => {
    assert.ok(Array.isArray(cases) && cases.length > 0, 'cases.json holds no rows');
    const failures = [];
    for (const row of cases) {
      const label = `${row.rule} (${row.file})`;
      const doc = loadDoc(row.file);
      const schema = validateRelease(doc);
      const err = manifestError(doc, row.file);
      if (row.expect === 'accept') {
        if (!schema.valid) failures.push(`${label}: validateRelease rejects it: ${pairsOf(schema).join(', ')}`);
        if (err) failures.push(`${label}: checkReleaseManifest throws ${err.code}: ${err.message}`);
      } else if (isError(row.expect)) {
        const { error, ajv } = row.expect;
        if (!err) failures.push(`${label}: expected ${error}, but checkReleaseManifest accepts it`);
        else if (err.code !== error) failures.push(`${label}: expected ${error}, got ${err.code}: ${err.message}`);
        // ajv: the schema finding the row exists for, or null when only a code rule (FR-017) can catch it.
        if (ajv === null) {
          if (!schema.valid) failures.push(`${label}: expected schema-valid, got ${pairsOf(schema).join(', ')}`);
        } else if (!pairsOf(schema).includes(ajv)) {
          failures.push(`${label}: expected ${show(ajv)} among the schema findings, got ${pairsOf(schema).join(', ')}`);
        }
      } else {
        failures.push(`${label}: unknown expect ${show(row.expect)}`);
      }
    }
    assert.deepEqual(failures, []);
  });

  await t.test('the rows cover exactly the C2 rule list, each with its plan code', () => {
    const errorRows = cases.filter((r) => isError(r.expect));
    const errorIds = new Set(errorRows.map((r) => r.rule));
    const acceptIds = new Set(cases.filter((r) => r.expect === 'accept').map((r) => r.rule));
    assert.deepEqual(
      C2_RULES.filter((id) => !errorIds.has(id)),
      [],
      'C2 rule ids without an error row',
    );
    assert.deepEqual(
      C2_VALID_RULES.filter((id) => !acceptIds.has(id)),
      [],
      'FR-056 valid manifests without an accept row',
    );
    assert.deepEqual(
      cases.map((r) => r.rule).filter((id) => !C2_RULES.includes(id) && !C2_VALID_RULES.includes(id)),
      [],
      'rows naming an id outside C2_RULES and the FR-056 valid ids',
    );
    assert.deepEqual(
      cases.filter((r) => C2_RULES.includes(r.rule) !== isError(r.expect)).map((r) => r.rule),
      [],
      'an error id with an accept row, or a valid id with an error row',
    );
    assert.deepEqual(
      errorRows.filter((r) => r.expect.error !== planCode(r.rule)).map((r) => `${r.rule}: ${r.expect.error}`),
      [],
      'error rows whose code differs from the plan §4.7 table',
    );
  });

  await t.test('rows are well formed and every fixture file belongs to a row', () => {
    const files = new Set();
    for (const row of cases) {
      assert.equal(typeof row.rule, 'string', show(row));
      assert.equal(typeof row.file, 'string', show(row));
      assert.ok(row.expect === 'accept' || isError(row.expect), show(row));
      if (isError(row.expect)) assert.ok(row.expect.ajv === null || typeof row.expect.ajv === 'string', show(row));
      assert.ok(fs.existsSync(path.join(RELEASE_DIR, ...row.file.split('/'))), `missing file ${row.file}`);
      assert.ok(!files.has(row.file), `two rows share ${row.file}`);
      files.add(row.file);
    }
    for (const dir of ['valid', 'invalid']) {
      for (const name of fs.readdirSync(path.join(RELEASE_DIR, dir))) {
        assert.ok(files.has(`${dir}/${name}`), `${dir}/${name} is not referenced by any row`);
      }
    }
  });

  await t.test('a thrown error names the plugin, the file and the field', () => {
    for (const row of cases.filter((r) => isError(r.expect))) {
      const doc = loadDoc(row.file);
      const err = manifestError(doc, row.file);
      assert.ok(err, `${row.rule}: accepted`);
      assert.equal(err.file, row.file, `${row.rule}: file`);
      assert.equal(typeof err.field, 'string', `${row.rule}: field ${show(err.field)}`);
      if (typeof doc.name === 'string') assert.equal(err.plugin, doc.name, `${row.rule}: plugin`);
    }
  });

  await t.test('the valid manifests cover FR-056: each macOS key, Windows without min_os, both version forms', () => {
    const valid = cases.filter((r) => r.expect === 'accept').map((r) => ({ row: r, doc: loadDoc(r.file) }));
    for (const key of DARWIN_KEYS) {
      assert.ok(
        valid.some(({ doc }) => Object.hasOwn(doc.assets, key)),
        `no valid manifest with a ${key} asset`,
      );
    }
    assert.ok(
      valid.some(({ doc }) => !Object.hasOwn(doc.assets['windows-x86_64'], 'min_os')),
      'no valid manifest whose windows-x86_64 asset has no min_os',
    );
    assert.ok(
      valid.some(({ doc }) => !isStableCore(doc.version) && doc.version.includes('-')),
      'no valid manifest with a prerelease-suffixed version',
    );
    assert.ok(
      valid.some(({ doc }) => isStableCore(doc.version)),
      'no valid manifest with a plain three-part version',
    );
  });

  await t.test('no valid manifest carries a channel, and a channel key is E_MANIFEST_SCHEMA (SC-022a)', () => {
    for (const row of cases.filter((r) => r.expect === 'accept')) {
      assert.ok(!Object.hasOwn(loadDoc(row.file), 'channel'), `${row.rule}: valid manifest has a channel key`);
    }
    for (const [rule, channel] of [
      ['FR-015/channel-stable', 'stable'],
      ['FR-015/channel-beta', 'beta'],
    ]) {
      const row = cases.find((r) => r.rule === rule);
      assert.ok(row, `no cases.json row for ${rule}`);
      const doc = loadDoc(row.file);
      assert.equal(doc.channel, channel, `${rule}: channel value`);
      assert.equal(validateRelease(doc).valid, false, `${rule}: the schema accepts a channel key`);
      assert.equal(manifestError(doc, row.file)?.code, 'E_MANIFEST_SCHEMA', `${rule}: code`);
    }
  });

  // C2 covers C1 for copied data (plan §4.1, PD-10): the entry gen-feed assembles from each valid manifest, for a plugin
  // whose version.json and setup.iss agree with it, passes C1 on its own. A plain version is the stable entry; a
  // suffixed one is the prerelease sibling of a manifest-less stable entry.
  await t.test('projection', () => {
    const { assembleEntry } = require('../../gen-feed.js');
    const valid = cases.filter((r) => r.expect === 'accept');
    assert.ok(valid.length > 0);
    for (const row of valid) {
      const doc = loadDoc(row.file);
      const lower = doc.name.toLowerCase();
      const versionJson = { version: doc.version, name: doc.name, url: `https://krateaudio.com/${lower}/` };
      if (doc.install.preset_subdir !== undefined) versionJson.preset_subdir = doc.install.preset_subdir;
      if (doc.install.macos_pkg_id !== `com.krateaudio.${lower}`) versionJson.identifier = doc.install.macos_pkg_id;
      // Inno's doubled opening brace: AppId={{GUID} is the AppId {GUID}.
      const setupIss = `[Setup]\nAppId={${doc.install.windows_uninstall_key.slice(0, -'_is1'.length)}\n`;
      const plugin = { dir: lower, versionJson, setupIss };
      const own = { tagName: doc.tag, publishedAt: doc.pub_date, version: doc.version };
      const manifest = { file: row.file, doc };
      const stableRow = isStableCore(doc.version);
      const args = stableRow
        ? { plugin, stable: own, stableManifest: manifest, repo: REPO }
        : {
            plugin,
            stable: { tagName: `${lower}/v0.1.0`, publishedAt: doc.pub_date, version: '0.1.0' },
            sibling: own,
            siblingManifest: manifest,
            repo: REPO,
          };
      const { entry, warning } = assembleEntry(args);
      assert.equal(warning, undefined, `${row.rule}: assembleEntry degrades it: ${warning?.code} ${warning?.message}`);
      const part = stableRow ? entry : entry.prerelease;
      assert.ok(part, `${row.rule}: no sibling assembled`);
      assert.deepEqual(Object.keys(part.platforms), Object.keys(doc.assets).sort(), `${row.rule}: platform keys`);
      assert.deepEqual(part.install, doc.install, `${row.rule}: install is the manifest's verbatim`);
      for (const [key, platform] of Object.entries(part.platforms)) {
        assert.ok(!Object.hasOwn(platform, 'name'), `${row.rule}: ${key} keeps the asset name`);
        if (platform.payload) assert.ok(!Object.hasOwn(platform.payload, 'list'), `${row.rule}: ${key} keeps payload.list`);
      }
      assert.doesNotThrow(() => checkAssembledEntry(doc.name, entry), `${row.rule}: the assembled entry fails C1`);
    }
  });
});
