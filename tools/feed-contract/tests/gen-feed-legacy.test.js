'use strict';
require('./helpers/write-guard.js');

// `gen-feed legacy reproduces the 2026-10-29 live feed` (spec SC-001; FR-040, FR-048, FR-071; plan §4.5.3).
// The snapshot holds the real inputs of 2026-10-29 (nine version.json copies and the bare tag list); legacy mode must
// turn them into the live versions.json of that day, byte for byte, both through the pure core and through the CLI.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { loadCase, runMain } = require('./helpers/fixtures.js');
const { buildLegacyFeed, readPluginsDir } = require('../../gen-feed.js');

const SNAPSHOT = 'snapshot-2026-10-29';

// Names the first line where two byte buffers differ, for the failure message.
function firstDifference(actual, expected) {
  const a = actual.toString('utf8').split('\n');
  const e = expected.toString('utf8').split('\n');
  for (let i = 0; i < Math.max(a.length, e.length); ++i) {
    if (a[i] !== e[i]) {
      return `first difference at line ${i + 1}: got ${JSON.stringify(a[i])}, expected ${JSON.stringify(e[i])}`;
    }
  }
  return `same lines, different bytes (${actual.length} vs ${expected.length} bytes)`;
}

test('gen-feed legacy reproduces the 2026-10-29 live feed', async (t) => {
  const snap = loadCase(SNAPSHOT);
  const live = fs.readFileSync(path.join(snap.dir, 'live-versions.json'));

  await t.test('buildLegacyFeed on the snapshot equals live-versions.json', () => {
    const text = buildLegacyFeed({
      plugins: readPluginsDir(snap.pluginsDir, { withSetupIss: false }),
      tagsText: fs.readFileSync(snap.tagsPath, 'utf8'),
    });
    const got = Buffer.from(text, 'utf8');
    assert.ok(got.equals(live), firstDifference(got, live));
  });

  await t.test('main --legacy exits 0 and writes the same bytes', () => {
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'km1-legacy-'));
    try {
      const out = path.join(tmp, 'v.json');
      const r = runMain(['--legacy', '--plugins-dir', snap.pluginsDir, '--tags', snap.tagsPath, '--out', out]);
      assert.equal(r.code, 0, r.stderr);
      assert.equal(r.stderr, '');
      const written = fs.readFileSync(out);
      assert.ok(written.equals(live), firstDifference(written, live));
      // writeAtomic renamed its temp file onto --out: nothing else is left in the directory.
      assert.deepEqual(fs.readdirSync(tmp), ['v.json']);
      // The committed golden is the same bytes (it is what T015's determinism test compares against).
      const golden = fs.readFileSync(path.join(snap.dir, 'expected.legacy.json'));
      assert.ok(golden.equals(live), firstDifference(golden, live));
    } finally {
      fs.rmSync(tmp, { recursive: true, force: true });
    }
  });
});
