'use strict';
require('./helpers/write-guard.js');

// `C1 feed fixtures` (spec SC-005; FR-001 to FR-012, FR-055; plan §3.1, §4.7 PD-11) and
// `feed rejects shared install facts` (spec SC-026; FR-014, PD-13).
// Reads fixtures/feeds/cases.json (built by fixtures/make-fixtures.js): every row's document must give its expected
// result under validateFeed, and the row set must cover every rule id in C1_RULES (no rule without an error row, no
// row naming an id outside the list), so dropping a schema rule or a fixture row both turn this test red.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const { validateFeed, definedPlatformKeys, findInstallDuplicates } = require('../contract.js');

const FEEDS_DIR = path.join(__dirname, '..', 'fixtures', 'feeds');
const CONTRACT_MD = path.join(__dirname, '..', 'CONTRACT.md');

// plan §4.7 "C1 invalid rows": every id of the table's right column, prefixed with its FR.
const C1_ERROR_RULES = Object.freeze([
  'FR-001/malformed-json',
  'FR-001/schema_version-const',
  'FR-001/plugins-required',
  'FR-002/plugin-key-pattern',
  'FR-002/plugin-key-reserved',
  'FR-003/version-type',
  'FR-003/download_url-required',
  'FR-003/release_notes-const',
  'FR-003/tag-required',
  'FR-003/pub_date-required',
  'FR-003/channel-required',
  'FR-003/release_notes_url-required',
  'FR-003/channel-stable-const',
  'FR-003/notes_markdown-type',
  'FR-003/platforms-requires-install',
  'FR-003/install-requires-platforms',
  'FR-004/version-suffix',
  'FR-004/version-ten-digits',
  'FR-004/version-leading-zero',
  'FR-005/tag-pattern',
  'FR-005/tag-stable-suffix',
  'FR-005/pub_date-pattern',
  'FR-005/url-https',
  'FR-005/url-whitespace',
  'FR-005/url-latest',
  'FR-006/platform-key-pattern',
  'FR-006/platform-url-required',
  'FR-006/platform-size-zero',
  'FR-006/platform-size-max',
  'FR-006/platform-size-type',
  'FR-006/platform-sha256-length',
  'FR-006/platform-sha256-hex',
  'FR-006/format-windows',
  'FR-006/format-darwin',
  'FR-006/format-linux',
  'FR-006/min_os-darwin-required',
  'FR-006/min_os-linux-required',
  'FR-006/min_os-pattern',
  'FR-007/signature-legacy-Ed',
  'FR-007/signature-cr',
  'FR-007/signature-blob-length',
  'FR-007/signature-noncanonical-base64',
  'FR-007/signature-trusted-comment',
  'FR-007/signature-no-final-lf',
  'FR-008/install-dotdot',
  'FR-008/install-slash',
  'FR-008/install-backslash',
  'FR-008/vst3_bundle-required',
  'FR-008/uninstall-key-pattern',
  'FR-008/pkg-id-pattern',
  'FR-008/darwin-requires-au_component',
  'FR-008/darwin-requires-macos_pkg_id',
  'FR-008/windows-requires-uninstall-key',
  'FR-008/bundle-reserved-stem',
  'FR-008/component-reserved-stem',
  'FR-008/preset_subdir-reserved',
  'FR-008/preset_subdir-trailing-space',
  'FR-009/payload-tar-gz-required',
  'FR-009/payload-inno-exe-forbidden',
  'FR-009/payload-pkg-forbidden',
  'FR-009/payload-files-min',
  'FR-009/payload-sha256-pattern',
  'FR-010/sibling-version-forbidden',
  'FR-010/sibling-download_url-forbidden',
  'FR-010/sibling-release_notes-forbidden',
  'FR-010/sibling-channel-const',
  'FR-010/sibling-tag-required',
  'FR-010/sibling-tag-pattern',
  'FR-010/sibling-platforms-requires-install',
  'FR-011/manager-latest_json_url-required',
  'FR-011/manager-latest_json_url-suffix',
  'FR-011/manager-latest_json_url-https',
  'FR-012/v1-version-required',
  'FR-012/v1-version-type',
  'FR-014/dup-vst3_bundle',
  'FR-014/dup-au_component',
  'FR-014/dup-uninstall-key',
  'FR-014/dup-pkg-id',
  'FR-014/dup-vst3_bundle-casefold',
]);

// plan §4.7, the valid ids listed under the table (FR-055's valid minimum set plus FR-014's sibling row).
const C1_VALID_RULES = Object.freeze([
  'FR-055/v1-only',
  'FR-055/full-v2',
  'FR-055/mixed',
  'FR-055/prerelease-suffixed',
  'FR-055/prerelease-plain',
  'FR-055/notes-14000',
  'FR-055/tenth-plugin',
  'FR-055/unknown-platform-key',
  'FR-055/unknown-extra-keys',
  'FR-014/sibling-same-install',
]);

const C1_RULES = Object.freeze([...C1_ERROR_RULES, ...C1_VALID_RULES]);

// FR-055's minimum invalid set (the ★ rows of plan §4.7).
const FR055_STAR = Object.freeze([
  'FR-001/malformed-json',
  'FR-003/version-type',
  'FR-003/release_notes-const',
  'FR-003/notes_markdown-type',
  'FR-003/platforms-requires-install',
  'FR-004/version-suffix',
  'FR-004/version-ten-digits',
  'FR-004/version-leading-zero',
  'FR-005/url-https',
  'FR-005/url-latest',
  'FR-006/format-windows',
  'FR-006/min_os-darwin-required',
  'FR-007/signature-legacy-Ed',
  'FR-008/install-dotdot',
  'FR-008/install-slash',
  'FR-008/install-backslash',
  'FR-008/uninstall-key-pattern',
  'FR-008/pkg-id-pattern',
  'FR-009/payload-tar-gz-required',
  'FR-009/payload-inno-exe-forbidden',
  'FR-010/sibling-version-forbidden',
  'FR-014/dup-vst3_bundle',
  'FR-014/dup-au_component',
  'FR-014/dup-uninstall-key',
  'FR-014/dup-pkg-id',
]);

const cases = JSON.parse(fs.readFileSync(path.join(FEEDS_DIR, 'cases.json'), 'utf8'));

const isError = (expect) => expect !== null && typeof expect === 'object' && typeof expect.error === 'string';
const isAccept = (expect) => expect === 'accept' || expect === 'accept-without-platforms';

/** The row's document, or null when the file is not valid JSON (the loader's finding is then E_INPUT, plan §3.1). */
function loadDoc(file) {
  const text = fs.readFileSync(path.join(FEEDS_DIR, ...file.split('/')), 'utf8');
  try {
    return JSON.parse(text);
  } catch {
    return null;
  }
}

/** Every finding as {code, pair}: a code finding by its code, an Ajv finding also as "<instancePath> <keyword>". */
function findingsOf(row) {
  const doc = loadDoc(row.file);
  if (doc === null) {
    return { doc, valid: false, findings: [{ code: 'E_INPUT', pair: null, message: 'not valid JSON' }] };
  }
  const result = validateFeed(doc, { schema: row.schema ?? 'v2' });
  const findings = (result.errors ?? []).map((e) => ({
    code: e.code,
    pair: e.code === 'E_SCHEMA' ? `${e.instancePath} ${e.keyword}` : null,
    message: e.message,
  }));
  return { doc, valid: result.valid, findings };
}

const show = (v) => JSON.stringify(v);

test('C1 feed fixtures', async (t) => {
  await t.test('every cases.json row yields its expected result', () => {
    assert.ok(Array.isArray(cases) && cases.length > 0, 'cases.json holds no rows');
    const failures = [];
    for (const row of cases) {
      const label = `${row.rule} (${row.file})`;
      const { doc, valid, findings } = findingsOf(row);
      const listed = findings.map((f) => f.pair ?? f.code).join(', ');
      if (isAccept(row.expect)) {
        if (!valid) {
          failures.push(`${label}: expected ${show(row.expect)}, got ${listed}`);
          continue;
        }
        if ((row.schema ?? 'v2') === 'v2') {
          const v1 = validateFeed(doc, { schema: 'v1' });
          if (!v1.valid) failures.push(`${label}: valid under v2 but not under v1 (FR-012)`);
        }
        if (row.expect === 'accept-without-platforms') {
          const entry = typeof row.plugin === 'string' ? doc.plugins?.[row.plugin] : undefined;
          if (entry === undefined) {
            failures.push(`${label}: accept-without-platforms needs a plugin present in the document`);
          } else {
            const keys = [...definedPlatformKeys(entry), ...definedPlatformKeys(entry.prerelease)];
            if (keys.length !== 0) failures.push(`${label}: ${row.plugin} has defined platform keys ${show(keys)}`);
          }
        }
      } else if (isError(row.expect)) {
        if (valid) {
          failures.push(`${label}: expected ${row.expect.error}, but the document is valid`);
          continue;
        }
        const want = row.expect.error;
        if (!findings.some((f) => f.code === want || f.pair === want)) {
          failures.push(`${label}: expected ${show(want)} among the findings, got ${listed}`);
        }
      } else {
        failures.push(`${label}: unknown expect ${show(row.expect)}`);
      }
    }
    assert.deepEqual(failures, []);
  });

  await t.test('the rows cover exactly the C1 rule list', () => {
    const errorIds = new Set(cases.filter((r) => isError(r.expect)).map((r) => r.rule));
    const acceptIds = new Set(cases.filter((r) => isAccept(r.expect)).map((r) => r.rule));
    assert.deepEqual(
      C1_ERROR_RULES.filter((id) => !errorIds.has(id)),
      [],
      'C1 rule ids without an error row',
    );
    assert.deepEqual(
      C1_VALID_RULES.filter((id) => !acceptIds.has(id)),
      [],
      'FR-055 valid categories without an accept row',
    );
    assert.deepEqual(
      cases.map((r) => r.rule).filter((id) => !C1_RULES.includes(id)),
      [],
      'rows naming an id outside C1_RULES',
    );
    assert.deepEqual(
      cases.filter((r) => C1_VALID_RULES.includes(r.rule) !== isAccept(r.expect)).map((r) => r.rule),
      [],
      'a valid id with an error row, or an error id with an accept row',
    );
    assert.deepEqual(
      FR055_STAR.filter((id) => !errorIds.has(id)),
      [],
      'FR-055 invalid categories without an error row',
    );
    assert.ok(cases.length >= 29, `SC-005 floor: ${cases.length} rows < 29`);
  });

  await t.test('rows are well formed and every fixture file belongs to a row', () => {
    const files = new Set();
    for (const row of cases) {
      assert.equal(typeof row.rule, 'string', show(row));
      assert.equal(typeof row.file, 'string', show(row));
      assert.ok(row.schema === undefined || row.schema === 'v1' || row.schema === 'v2', show(row));
      assert.ok(fs.existsSync(path.join(FEEDS_DIR, ...row.file.split('/'))), `missing file ${row.file}`);
      assert.ok(row.expect !== 'accept-without-platforms' || typeof row.plugin === 'string', show(row));
      files.add(row.file);
    }
    for (const dir of ['valid', 'invalid']) {
      for (const name of fs.readdirSync(path.join(FEEDS_DIR, dir))) {
        assert.ok(files.has(`${dir}/${name}`), `${dir}/${name} is not referenced by any row`);
      }
    }
  });

  await t.test('the notes-14000 document carries a notes_markdown over 14,000 bytes (FR-044)', () => {
    const row = cases.find((r) => r.rule === 'FR-055/notes-14000');
    const doc = loadDoc(row.file);
    const longest = Math.max(
      ...Object.values(doc.plugins).map((e) =>
        typeof e.notes_markdown === 'string' ? Buffer.byteLength(e.notes_markdown, 'utf8') : 0,
      ),
    );
    assert.ok(longest > 14000, `longest notes_markdown is ${longest} bytes`);
  });
});

// SC-026: the four shared-field documents, plus PD-13's case-folded bundle name.
const DUPLICATES = Object.freeze([
  { rule: 'FR-014/dup-vst3_bundle', field: 'vst3_bundle', plugins: ['Alpha', 'Bravo'] },
  { rule: 'FR-014/dup-au_component', field: 'au_component', plugins: ['Alpha', 'Bravo'] },
  { rule: 'FR-014/dup-uninstall-key', field: 'windows_uninstall_key', plugins: ['Alpha', 'Bravo'] },
  { rule: 'FR-014/dup-pkg-id', field: 'macos_pkg_id', plugins: ['Alpha', 'Bravo'] },
  { rule: 'FR-014/dup-vst3_bundle-casefold', field: 'vst3_bundle', plugins: ['Alpha', 'ALPHA'] },
]);

const docOf = (rule) => {
  const row = cases.find((r) => r.rule === rule);
  assert.ok(row, `no cases.json row for ${rule}`);
  const doc = loadDoc(row.file);
  assert.ok(doc, `${row.file} is not valid JSON`);
  return doc;
};

const isV1Only = (entry) => entry.platforms === undefined && entry.install === undefined;

test('feed rejects shared install facts', async (t) => {
  await t.test('each shared field gives one E_INSTALL_DUPLICATE naming the field and both plugin keys', () => {
    for (const { rule, field, plugins } of DUPLICATES) {
      const result = validateFeed(docOf(rule), { schema: 'v2' });
      assert.equal(result.valid, false, `${rule}: accepted`);
      assert.equal(result.errors.length, 1, `${rule}: findings ${show(result.errors)}`);
      const [finding] = result.errors;
      assert.equal(finding.code, 'E_INSTALL_DUPLICATE', `${rule}: ${show(finding)}`);
      assert.ok(finding.message.includes(`install.${field}`), `${rule}: message lacks the field: ${finding.message}`);
      for (const p of plugins) {
        assert.ok(finding.message.includes(show(p)), `${rule}: message does not name ${p}: ${finding.message}`);
      }
    }
  });

  await t.test('the full-v2 and mixed documents pass, with no duplicate pair', () => {
    for (const rule of ['FR-055/full-v2', 'FR-055/mixed']) {
      const doc = docOf(rule);
      assert.deepEqual(validateFeed(doc, { schema: 'v2' }), { valid: true, errors: [] }, rule);
      assert.deepEqual(findInstallDuplicates(doc.plugins), [], rule);
    }
  });

  await t.test('an entry and its own prerelease sibling with identical install pass', () => {
    const doc = docOf('FR-014/sibling-same-install');
    const withSibling = Object.entries(doc.plugins).filter(([, e]) => e.prerelease && e.prerelease.install);
    assert.ok(withSibling.length > 0, 'the fixture has no sibling with an install block');
    for (const [name, entry] of withSibling) {
      assert.deepEqual(entry.prerelease.install, entry.install, `${name}: sibling install differs from the entry's`);
    }
    assert.deepEqual(validateFeed(doc, { schema: 'v2' }), { valid: true, errors: [] });
  });

  await t.test('v1-only entries never appear in a finding', () => {
    const mixed = docOf('FR-055/mixed');
    const v1Only = Object.entries(mixed.plugins).filter(([, e]) => isV1Only(e));
    assert.ok(v1Only.length > 0, 'the mixed fixture has no v1-only entry');
    for (const { rule } of DUPLICATES) {
      const doc = docOf(rule);
      const before = validateFeed(doc, { schema: 'v2' }).errors;
      for (const [name, entry] of v1Only) {
        if (doc.plugins[name] === undefined) doc.plugins[name] = entry;
      }
      const after = validateFeed(doc, { schema: 'v2' }).errors;
      assert.deepEqual(after, before, `${rule}: adding v1-only entries changed the findings`);
      for (const [name] of v1Only) {
        assert.ok(after.every((f) => !f.message.includes(show(name))), `${rule}: a finding names v1-only ${name}`);
      }
    }
  });

  await t.test('CONTRACT.md names E_INSTALL_DUPLICATE', () => {
    assert.match(fs.readFileSync(CONTRACT_MD, 'utf8'), /E_INSTALL_DUPLICATE/);
  });
});
