'use strict';
require('./helpers/write-guard.js');

// gen-feed's plugin description (Krate Manager Phase 12 spec FR-112, SC-037).
// One top-level test (the feed-tools TAP plan counts top-level tests only); every check is a subtest. buildFeed runs on
// in-memory plugins and release rows (no manifests, so every entry is v1-only), and the last subtest reads the real
// plugins/*/version.json files. Nothing is written.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const { buildFeed } = require('../../gen-feed.js');
const { ContractError, validateFeed } = require('../contract.js');
const schema = require('../versions.v2.schema.json');

const REPO = 'rolandzwaga/krate-audio';
const PLUGINS_DIR = path.join(__dirname, '..', '..', '..', 'plugins');
const DESCRIPTION_RE = new RegExp(schema.$defs.entry.properties.description.pattern, 'u');

const plugin = (name, extra = {}) => ({
  dir: name.toLowerCase(),
  versionJson: { name, url: `https://krateaudio.com/${name.toLowerCase()}/`, ...extra },
});
const row = (tagName, isPrerelease = false) => ({ tagName, isPrerelease, isDraft: false, publishedAt: '2026-11-03T12:00:00Z' });
const build = (plugins, releases) =>
  buildFeed({ plugins, releases: releases ?? plugins.map((p) => row(`${p.dir}/v1.0.0`)), repo: REPO });

test('gen-feed writes each plugin description from version.json', async (t) => {
  await t.test('it follows release_notes_url on the entry, never on the sibling, and the feed passes C1', () => {
    const text = 'Multi-Mode Delay & <Echo>';
    const { doc, warnings } = build([plugin('Alpha', { description: text })], [row('alpha/v1.0.0'), row('alpha/v1.1.0-beta.1', true)]);
    assert.deepEqual(warnings, []);
    const entry = doc.plugins.Alpha;
    const keys = Object.keys(entry);
    assert.equal(entry.description, text);
    assert.equal(keys.indexOf('description'), keys.indexOf('release_notes_url') + 1, keys.join(', '));
    assert.ok(entry.prerelease, 'the beta row gives a sibling');
    assert.equal(Object.hasOwn(entry.prerelease, 'description'), false);
    assert.equal(validateFeed(doc, { schema: 'v2' }).valid, true);
  });

  await t.test('a version.json without one gives an entry without one', () => {
    const { doc } = build([plugin('Alpha', { description: 'Step Arpeggiator' }), plugin('Bravo')]);
    assert.equal(doc.plugins.Alpha.description, 'Step Arpeggiator');
    assert.equal(Object.hasOwn(doc.plugins.Bravo, 'description'), false);
  });

  await t.test('120 characters pass; anything the C1 rule rejects is E_INPUT naming the file and field', () => {
    assert.equal(build([plugin('Alpha', { description: 'ü'.repeat(120) })]).doc.plugins.Alpha.description, 'ü'.repeat(120));
    for (const bad of [7, null, '', 'Multi-Mode\nDelay', 'Tab\tStop', 'Next\u0085Line', 'x'.repeat(121)]) {
      assert.throws(
        () => build([plugin('Alpha', { description: bad })]),
        (err) =>
          err instanceof ContractError &&
          err.code === 'E_INPUT' &&
          err.field === 'description' &&
          err.file === 'alpha/version.json' &&
          err.message.includes('"description"'),
        JSON.stringify(bad),
      );
    }
  });

  await t.test('every plugins/*/version.json description passes the C1 rule', () => {
    const dirs = fs.readdirSync(PLUGINS_DIR).filter((d) => fs.existsSync(path.join(PLUGINS_DIR, d, 'version.json')));
    assert.ok(dirs.length > 0, 'no plugin version.json found');
    for (const dir of dirs) {
      const { name, description } = JSON.parse(fs.readFileSync(path.join(PLUGINS_DIR, dir, 'version.json'), 'utf8'));
      assert.equal(typeof description, 'string', `${dir}: description`);
      assert.match(description, DESCRIPTION_RE, `${dir} (${name})`);
    }
  });
});
