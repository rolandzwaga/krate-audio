'use strict';
require('./helpers/write-guard.js');

// tools/feed-publish.js compare: every v1 difference between the live feed and the new one is explained or fails the
// run (spec specs/krate-manager-phase3/spec.md FR-027, FR-040, FR-041, SC-007; plan §4.2, PD-10, PD-11, PD-13, PD-18,
// §7.1 T7). The expected lines come from fixtures/phase3/compare/cases.json, written by the fixture builder before the
// tool existed. Outputs land in an fs.mkdtemp dir; nothing spawns: the deps handed to main throw when called.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const feedPublish = require('../../feed-publish.js');

const COMPARE_DIR = path.resolve(__dirname, '..', 'fixtures', 'phase3', 'compare');
const CASES = JSON.parse(fs.readFileSync(path.join(COMPARE_DIR, 'cases.json'), 'utf8'));
// The fixed annotation titles of plan §4.1.6: the only lines the tool may start with "::".
const ANNOTATION_RE =
  /^::(notice|warning|error) title=(feed-collect|feed-generate|feed-degrade|feed-validate|feed-compare|feed-drift|feed-verdict)::/u;
const PRIOR_SUMMARY = '## earlier step\n\nsome text\n';

function withTemp(t, name) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `km3-${name}-`));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  return dir;
}

/** Stub effects: compare uses neither, so any call is a test failure. */
const stubDeps = Object.freeze({
  gh: () => {
    throw new Error('stub gh called');
  },
  genFeedMain: () => {
    throw new Error('stub genFeedMain called');
  },
});

const linesOf = (text) => (text === '' ? [] : text.replace(/\n$/u, '').split('\n'));

/** Runs main with captured output and checks the PD-18 (c) / FR-029e output rule on every call. */
function run(argv) {
  let stdout = '';
  let stderr = '';
  const io = {
    stdout: { write: (s) => ((stdout += s), true) },
    stderr: { write: (s) => ((stderr += s), true) },
  };
  const code = feedPublish.main(argv, io, stubDeps);
  const out = linesOf(stdout);
  const err = linesOf(stderr);
  for (const l of out) {
    assert.ok(!l.startsWith('::') || ANNOTATION_RE.test(l), `stdout line starts "::" without a fixed title: ${l}`);
    assert.ok(!l.includes('\r'), `stdout line holds a CR: ${JSON.stringify(l)}`);
  }
  for (const l of err) {
    assert.ok(!l.startsWith('::'), `stderr line starts "::": ${l}`);
    assert.ok(!l.includes('\r'), `stderr line holds a CR: ${JSON.stringify(l)}`);
  }
  return { code, stdout, stderr, out, err };
}

/** The expected --summary block, written from plan §4.2 / PD-18 (d), not from the tool. */
function summaryBlock(lines) {
  const runs = lines.join('\n').match(/`+/gu) || [];
  const longest = runs.reduce((m, r) => Math.max(m, r.length), 0);
  const fence = '`'.repeat(Math.max(3, longest + 1));
  return `${fence}text\n${lines.map((l) => `${l}\n`).join('')}${fence}\n`;
}

function caseArgv(row, dir, extra = []) {
  const base = path.join(COMPARE_DIR, row.case);
  return [
    'compare',
    '--live',
    path.join(base, 'live.json'),
    '--feed',
    path.join(base, 'feed.json'),
    '--releases',
    path.join(base, 'releases.json'),
    '--dry-run',
    String(row.dryRun),
    '--report',
    path.join(dir, 'report.txt'),
    '--summary',
    path.join(dir, 'summary.md'),
    ...extra,
  ];
}

const readFixture = (name, file) => JSON.parse(fs.readFileSync(path.join(COMPARE_DIR, name, file), 'utf8'));

test('compare explains every v1 difference or fails', async (t) => {
  assert.ok(CASES.length >= 30, `cases.json holds ${CASES.length} rows`);

  for (const row of CASES) {
    await t.test(`${row.case} (dry run ${row.dryRun})`, async (s) => {
      for (const annotateRun of [false, true]) {
        const label = `${row.case}${annotateRun ? ' --annotate' : ''}`;
        const dir = withTemp(s, `compare-${annotateRun ? 'a' : 'p'}`);
        fs.writeFileSync(path.join(dir, 'summary.md'), PRIOR_SUMMARY);
        const r = run(caseArgv(row, dir, annotateRun ? ['--annotate'] : []));

        assert.equal(r.code, row.exit, `${label}: exit (stderr: ${r.stderr})`);
        assert.deepEqual(r.out, annotateRun ? [...row.stdout, ...row.annotations] : row.stdout, `${label}: stdout`);
        assert.deepEqual(r.err, row.stderr, `${label}: stderr`);
        if (!annotateRun) assert.equal(r.stdout, row.stdout.map((l) => `${l}\n`).join(''), `${label}: stdout bytes`);

        const report = fs.readFileSync(path.join(dir, 'report.txt'), 'utf8');
        const expectedReport = row.stdout.length === 0 ? '' : row.stdout.join('\n') + '\n';
        assert.equal(report, expectedReport, `${label}: --report bytes`);
        const summary = fs.readFileSync(path.join(dir, 'summary.md'), 'utf8');
        assert.equal(summary, PRIOR_SUMMARY + summaryBlock(row.stdout), `${label}: --summary appended`);
        assert.deepEqual(
          fs.readdirSync(dir).sort(),
          ['report.txt', 'summary.md'],
          `${label}: no temp file left beside the report`,
        );
      }
    });
  }

  await t.test('the backtick key gets a four-backtick summary fence', (s) => {
    const row = CASES.find((c) => c.case === 'live-backtick');
    assert.ok(row, 'cases.json has the live-backtick row');
    const dir = withTemp(s, 'compare-fence');
    fs.writeFileSync(path.join(dir, 'summary.md'), PRIOR_SUMMARY);
    run(caseArgv(row, dir));
    const block = fs.readFileSync(path.join(dir, 'summary.md'), 'utf8').slice(PRIOR_SUMMARY.length);
    assert.ok(block.startsWith('````text\n'), `fence: ${JSON.stringify(block.slice(0, 12))}`);
    assert.ok(block.endsWith('\n````\n'));
  });

  await t.test('--dry-run missing or not a boolean is usage (2)', (s) => {
    const row = CASES.find((c) => c.case === 'same');
    for (const dryRun of [null, 'maybe']) {
      const dir = withTemp(s, 'compare-usage');
      const argv = caseArgv(row, dir);
      const at = argv.indexOf('--dry-run');
      if (dryRun === null) argv.splice(at, 2);
      else argv[at + 1] = dryRun;
      const r = run(argv);
      assert.equal(r.code, 2, `--dry-run ${dryRun}`);
      assert.match(r.err[0], /^feed-publish: E_USAGE: .*--dry-run/u);
      assert.ok(
        r.err.some((l) => l.startsWith('usage: node tools/feed-publish.js')),
        'usage block printed',
      );
      assert.equal(r.stdout, '');
      assert.equal(fs.existsSync(path.join(dir, 'report.txt')), false, 'no report on a usage error');
      assert.equal(fs.existsSync(path.join(dir, 'summary.md')), false, 'no summary on a usage error');
    }
  });

  await t.test('compareFeeds counts plugins and names every drift kind', () => {
    const triple = (name) => ({
      liveDoc: readFixture(name, 'live.json'),
      feedDoc: readFixture(name, 'feed.json'),
      rows: readFixture(name, 'releases.json'),
    });

    const same = feedPublish.compareFeeds(triple('same'));
    assert.deepEqual(same.counts, { same: 2, newer: 0, added: 0, q9: 0, drift: 0, block: 0, warn: 0 });
    assert.deepEqual(
      same.plugins.map((p) => [p.name, p.kind, p.drift]),
      [
        ['Alpha', 'same', undefined],
        ['Bravo', 'same', undefined],
      ],
    );
    assert.deepEqual(same.lines, ['same Alpha 1.1.0', 'same Bravo 2.0.0']);
    assert.equal(same.summaryLine, 'compare: 2 same, 0 newer, 0 added, 0 q9-corrected, 0 drift (0 block, 0 warn)');

    const mixed = feedPublish.compareFeeds(triple('mixed-kinds-one-plugin'));
    assert.deepEqual(mixed.counts, { same: 0, newer: 0, added: 0, q9: 0, drift: 1, block: 1, warn: 0 });
    assert.equal(mixed.plugins.length, 1);
    assert.equal(mixed.plugins[0].kind, 'drift');
    assert.deepEqual(mixed.plugins[0].drift, {
      kinds: ['version lowered', 'download_url changed without a newer release'],
      severity: 'block',
    });
    assert.equal(
      mixed.plugins[0].line,
      'drift Alpha version lowered; download_url changed without a newer release block',
    );
  });
});
