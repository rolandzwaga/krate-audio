'use strict';
require('./helpers/write-guard.js');

// tools/manager-readiness-counts.js (spec FR-100, SC-020, SC-028; plan PD-26; tasks T012, TA-16). The counts run over
// the fixture tree tools/feed-contract/fixtures/landing/readiness/ (eleven small compliance files and a roadmap); the
// ledger check runs over readiness-ok.md, shaped like the real ledger (Phase 11 SC-014 closed by a run, Phase 11 SC-020
// deferred, the seven D17 ids in Post-release). Every mutation is made in memory and written to a temp copy; main(argv,
// io) runs in-process with a capturing io. The fixture tree is only read.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const tool = require('../../manager-readiness-counts.js');

const PREFIX = 'manager-readiness-counts: ';
const USAGE_START = 'usage: node tools/manager-readiness-counts.js';
const FIX = path.join(__dirname, '..', 'fixtures', 'landing', 'readiness');
const SPECS = path.join(FIX, 'specs');
const ROADMAP = path.join(FIX, 'roadmap.md');
const LEDGER_OK = path.join(FIX, 'readiness-ok.md');
const LEDGER_TEXT = fs.readFileSync(LEDGER_OK, 'utf8');
const PENDING = 'PENDING-CI rows';
const POST = 'Post-release rows';

const COUNT_LINES = [
  'phase1 file=2 status=2',
  'phase2 file=1 status=1',
  'phase3 file=0 status=0',
  'phase4 file=1 status=1',
  'phase5 file=1 status=2',
  'phase6 file=1 status=1',
  'phase7 file=1 status=1',
  'phase8 file=1 status=1',
  'phase9 file=1 status=1',
  'phase10 file=1 status=1',
  'phase11 file=3 status=3',
];
const TOTAL_LINE = 'total file=13 status=14';
const EXPECTED_COUNTS = [...COUNT_LINES, TOTAL_LINE, ''].join('\n');

const tmpDirs = [];
test.after(() => {
  for (const d of tmpDirs) fs.rmSync(d, { recursive: true, force: true });
});

function tmpDir() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'km12-readiness-'));
  tmpDirs.push(dir);
  return dir;
}

function writeTmp(name, text) {
  const file = path.join(tmpDir(), name);
  fs.writeFileSync(file, text);
  return file;
}

// A temp copy of the fixture specs dir; `mutate(dir)` runs on the copy.
function specsCopy(mutate) {
  const dir = path.join(tmpDir(), 'specs');
  fs.cpSync(SPECS, dir, { recursive: true });
  mutate(dir);
  return dir;
}

function run(argv) {
  let stdout = '';
  let stderr = '';
  const io = {
    stdout: { write: (s) => (stdout += s) },
    stderr: { write: (s) => (stderr += s) },
  };
  const code = tool.main(argv, io);
  return { code, stdout, stderr };
}

const row = (...cells) => `| ${cells.join(' | ')} |`;

// The line index range [from, to) of the `## <heading>` section's body.
function sectionRange(lines, heading) {
  const from = lines.indexOf(`## ${heading}`);
  assert.ok(from >= 0, `fixture has no "## ${heading}"`);
  let to = lines.findIndex((l, i) => i > from && l.startsWith('## '));
  if (to < 0) to = lines.length;
  return [from + 1, to];
}

// Replaces (or with null deletes) the row of `id` in the section `heading`; returns the new ledger text.
function setRow(text, heading, id, replacement) {
  const lines = text.split('\n');
  const [from, to] = sectionRange(lines, heading);
  const at = lines.findIndex((l, i) => i >= from && i < to && l.startsWith(`| ${id} |`));
  assert.ok(at >= 0, `no row ${id} under ${heading}`);
  if (replacement === null) lines.splice(at, 1);
  else lines[at] = replacement;
  return lines.join('\n');
}

// Appends a row after the last table line of the section `heading`.
function addRow(text, heading, line) {
  const lines = text.split('\n');
  const [from, to] = sectionRange(lines, heading);
  let last = -1;
  for (let i = from; i < to; i++) if (lines[i].startsWith('|')) last = i;
  assert.ok(last >= 0, `no table under ${heading}`);
  lines.splice(last + 1, 0, line);
  return lines.join('\n');
}

const RUN = 'https://github.com/o/r/actions/runs/104';
const closedRow = (id, { url = RUN, commit = '89abcdef', verdict = 'green' } = {}) =>
  row(id, '`check (ubuntu-24.04)`', 'manager-ci.yml', url, commit, verdict);
const deferredRow = (id) => row(id, 'deferred', 'release-manager.yml', '', '', 'post-release');

function checkLedgerText(text) {
  return run(['--specs', SPECS, '--roadmap', ROADMAP, '--check-ledger', writeTmp('readiness.md', text)]);
}

function assertLedgerRefused(r, code) {
  assert.equal(r.code, 1, `exit code; stdout: ${r.stdout} stderr: ${r.stderr}`);
  const codes = r.stderr
    .split('\n')
    .filter((l) => l.startsWith(PREFIX))
    .map((l) => l.slice(PREFIX.length).split(':')[0]);
  assert.ok(codes.includes(code), `${code} not among ${JSON.stringify(codes)}; stderr: ${r.stderr}`);
  assert.ok(!r.stdout.includes('ledger ok'), 'a refused ledger printed "ledger ok"');
}

test('readiness counts and the closed post-release list', async (t) => {
  await t.test('prints one line per phase and the Phases 1-11 total', () => {
    const r = run(['--specs', SPECS, '--roadmap', ROADMAP]);
    assert.equal(r.stderr, '');
    assert.equal(r.code, 0);
    assert.equal(r.stdout, EXPECTED_COUNTS);
  });

  await t.test('a PENDING-CI verdict in capitals counts; a pending-ci word outside a verdict cell does not', () => {
    const phase1 = fs.readFileSync(path.join(SPECS, 'krate-manager-phase1', 'compliance.md'), 'utf8');
    assert.equal(tool.countCompliance(phase1), 2);
    assert.equal(tool.countCompliance('| FR-1 | PENDING-CI | Needs CI. |\n'), 1);
    assert.equal(tool.countCompliance('| FR-1 |  Pending-CI  | Needs CI. |\r\n| FR-2 | pending-ci | x |\r\n'), 2);
    assert.equal(tool.countCompliance('Every pending-ci row closes on CI.\n'), 0);
    assert.equal(tool.countCompliance('| FR-1 | pass | Was pending-ci until run 101. |\n'), 0);
    assert.equal(tool.countCompliance('Overall status: COMPLETE (PENDING-CI: 3 item(s))\n'), 0);
  });

  await t.test('a status line without PENDING-CI gives status=null', () => {
    const counts = tool.countStatusLines(fs.readFileSync(ROADMAP, 'utf8'));
    assert.equal(counts.get(6), 1, 'the prose PENDING-CI before the Phase 6 status line is not the status');
    assert.equal(counts.get(12), null, 'Phase 12 has no status line; the next section is not read');
    const phase4 = '** — see specs/krate-manager-phase4/';
    const withCount = `**Status: ✅ COMPLETE (PENDING-CI: 1 item(s)) (2026-10-30)${phase4}`;
    const text = fs.readFileSync(ROADMAP, 'utf8').replace(withCount, `**Status: ✅ COMPLETE (2026-10-30)${phase4}`);
    assert.notEqual(text, fs.readFileSync(ROADMAP, 'utf8'), 'the Phase 4 status line was not found');
    const r = run(['--specs', SPECS, '--roadmap', writeTmp('roadmap.md', text)]);
    assert.equal(r.code, 0, r.stderr);
    const lines = r.stdout.split('\n');
    assert.equal(lines[3], 'phase4 file=1 status=null');
    assert.equal(lines[11], 'total file=13 status=null');
  });

  await t.test('a Phase 12 file prints its own line and stays out of the total', () => {
    const specs = specsCopy((dir) => {
      fs.mkdirSync(path.join(dir, 'krate-manager-phase12'));
      fs.writeFileSync(
        path.join(dir, 'krate-manager-phase12', 'compliance.md'),
        '| ID | Verdict | Evidence |\n|---|---|---|\n| SC-001 | pending-ci | Needs CI. |\n',
      );
      // A phase dir without a compliance file (as Phase 12 is before its close) prints nothing.
      fs.mkdirSync(path.join(dir, 'krate-manager-phase13'));
    });
    const r = run(['--specs', specs, '--roadmap', ROADMAP]);
    assert.equal(r.code, 0, r.stderr);
    assert.equal(r.stdout, [...COUNT_LINES, 'phase12 file=1 status=null', TOTAL_LINE, ''].join('\n'));
  });

  await t.test('a missing Phase 1-11 compliance file is refused', () => {
    const specs = specsCopy((dir) => fs.rmSync(path.join(dir, 'krate-manager-phase3'), { recursive: true }));
    const r = run(['--specs', specs, '--roadmap', ROADMAP]);
    assert.equal(r.code, 1);
    assert.ok(r.stderr.startsWith(`${PREFIX}E_READINESS_SPECS: `), r.stderr);
    assert.equal(r.stdout, '');
  });

  await t.test('--check-ledger accepts the fixture ledger', () => {
    const r = run(['--specs', SPECS, '--roadmap', ROADMAP, '--check-ledger', LEDGER_OK]);
    assert.equal(r.stderr, '');
    assert.equal(r.code, 0);
    assert.ok(r.stdout.startsWith(EXPECTED_COUNTS), r.stdout);
    assert.equal(
      r.stdout.slice(EXPECTED_COUNTS.length),
      'ledger ok: 13 PENDING-CI rows (12 closed by a run, 1 deferred), 7 post-release rows\n',
    );
  });

  await t.test('phase11:SC-020 dropped from PENDING-CI: the count is short', () => {
    assertLedgerRefused(checkLedgerText(setRow(LEDGER_TEXT, PENDING, 'phase11:SC-020', null)), 'E_LEDGER_COUNT');
  });

  await t.test('a deferred PENDING-CI row that is not in Post-release is refused', () => {
    assertLedgerRefused(checkLedgerText(setRow(LEDGER_TEXT, POST, 'phase11:SC-020', null)), 'E_LEDGER_ROW');
  });

  await t.test('a repeated id is refused', () => {
    // phase2:SC-001 becomes a second phase1:FR-001: the row count stays 13.
    const text = setRow(LEDGER_TEXT, PENDING, 'phase2:SC-001', closedRow('phase1:FR-001'));
    const r = checkLedgerText(text);
    assertLedgerRefused(r, 'E_LEDGER_REPEAT');
    assert.ok(!r.stderr.includes('E_LEDGER_COUNT'), r.stderr);
  });

  await t.test('a run-closed row whose id is also a Post-release id is refused', () => {
    assertLedgerRefused(
      checkLedgerText(setRow(LEDGER_TEXT, PENDING, 'phase11:SC-020', closedRow('phase11:SC-020'))),
      'E_LEDGER_BOTH',
    );
  });

  await t.test('post_release_rows_are_closed', () => {
    assert.deepEqual(tool.D17_IDS, [
      'phase11:SC-014:publish',
      'phase11:SC-020',
      'phase12:R-POST-1',
      'phase12:R-POST-2',
      'phase12:R-POST-3',
      'phase12:R-POST-4',
      'phase12:R-POST-5',
    ]);
    // The seven pass.
    assert.equal(checkLedgerText(LEDGER_TEXT).code, 0);
    // An eighth exemption planted in PENDING-CI only.
    const planted = setRow(LEDGER_TEXT, PENDING, 'phase5:FR-010', deferredRow('phase5:FR-010'));
    assertLedgerRefused(checkLedgerText(planted), 'E_LEDGER_ROW');
    // ... and in Post-release too: still not a D17 id, and the Post-release set is no longer the seven.
    const both = addRow(planted, POST, deferredRow('phase5:FR-010'));
    assertLedgerRefused(checkLedgerText(both), 'E_LEDGER_ROW');
    assertLedgerRefused(checkLedgerText(both), 'E_LEDGER_POST_RELEASE');
    // One of the seven missing from Post-release.
    assertLedgerRefused(checkLedgerText(setRow(LEDGER_TEXT, POST, 'phase12:R-POST-3', null)), 'E_LEDGER_POST_RELEASE');
    // A Post-release id repeated.
    const repeated = addRow(LEDGER_TEXT, POST, deferredRow('phase12:R-POST-3'));
    assertLedgerRefused(checkLedgerText(repeated), 'E_LEDGER_POST_RELEASE');
    // A deferred row that carries a run URL is neither closed (verdict) nor deferred (URL).
    const withUrl = row('phase11:SC-020', 'x', 'release-manager.yml', RUN, '', 'post-release');
    assertLedgerRefused(checkLedgerText(setRow(LEDGER_TEXT, PENDING, 'phase11:SC-020', withUrl)), 'E_LEDGER_ROW');
  });

  await t.test('a Post-release row with verdict green is refused', () => {
    const green = row('phase12:R-POST-1', 'card', 'release-manager.yml', RUN, '89abcdef', 'green');
    assertLedgerRefused(checkLedgerText(setRow(LEDGER_TEXT, POST, 'phase12:R-POST-1', green)), 'E_LEDGER_POST_RELEASE');
  });

  await t.test('a run URL not of the form https://github.com/<owner>/<repo>/actions/runs/<n> is refused', () => {
    for (const url of [
      'https://github.com/o/r/actions/runs/abc',
      'http://github.com/o/r/actions/runs/104',
      'https://github.com/o/actions/runs/104',
      'https://github.com/o/r/actions/runs/104/job/7',
      'https://example.com/o/r/actions/runs/104',
      'https://github.com/o/r/runs/104',
    ]) {
      const text = setRow(LEDGER_TEXT, PENDING, 'phase4:FR-010', closedRow('phase4:FR-010', { url }));
      assertLedgerRefused(checkLedgerText(text), 'E_LEDGER_ROW');
    }
  });

  await t.test('a commit that is not 7-40 hex is refused', () => {
    for (const commit of ['0123ab', '0123abg', '', '0'.repeat(41)]) {
      const text = setRow(LEDGER_TEXT, PENDING, 'phase4:FR-010', closedRow('phase4:FR-010', { commit }));
      assertLedgerRefused(checkLedgerText(text), 'E_LEDGER_ROW');
    }
  });

  await t.test('an open row (empty verdict) is refused', () => {
    const text = setRow(LEDGER_TEXT, PENDING, 'phase4:FR-010', closedRow('phase4:FR-010', { verdict: '' }));
    assertLedgerRefused(checkLedgerText(text), 'E_LEDGER_ROW');
  });

  await t.test('a cell holding an escaped \\| parses as one cell', () => {
    const result = tool.checkLedger(LEDGER_TEXT, 13);
    assert.deepEqual(result.errors, []);
    const r = result.pending.find((x) => x.id === 'phase1:FR-003');
    assert.ok(r, 'phase1:FR-003 not parsed');
    assert.equal(r.job, '`node-test (macos-latest)` | `node-test (ubuntu-latest)`');
    assert.equal(r.url, 'https://github.com/o/r/actions/runs/101');
    assert.equal(r.verdict, 'green');
    const deferred = result.pending.find((x) => x.id === 'phase11:SC-020');
    assert.equal(deferred.url, '', 'an empty cell is empty text');
    // Unescaped, the same pipe opens a seventh cell.
    const unescaped = LEDGER_TEXT.replace('` \\| `', '` | `');
    assert.notEqual(unescaped, LEDGER_TEXT);
    assertLedgerRefused(checkLedgerText(unescaped), 'E_LEDGER_TABLE');
  });

  await t.test('a missing table, a wrong header or a malformed id is refused', () => {
    assertLedgerRefused(checkLedgerText(LEDGER_TEXT.replace(`## ${POST}`, '## Post release')), 'E_LEDGER_TABLE');
    const header = '| id | job and assertion | closing workflow | run URL | commit | verdict |';
    assert.ok(LEDGER_TEXT.includes(header));
    assertLedgerRefused(
      checkLedgerText(LEDGER_TEXT.replace(header, '| id | job | closing workflow | run URL | commit | verdict |')),
      'E_LEDGER_TABLE',
    );
    const badId = setRow(LEDGER_TEXT, PENDING, 'phase4:FR-010', closedRow('phase-4/FR-010'));
    assertLedgerRefused(checkLedgerText(badId), 'E_LEDGER_TABLE');
  });

  await t.test('usage errors exit 2 with the usage line', () => {
    for (const argv of [
      ['--specs', SPECS, '--roadmap', ROADMAP, '--bogus', 'x'],
      ['--roadmap', ROADMAP],
      ['--specs', SPECS],
      ['--specs', SPECS, '--roadmap'],
      ['--specs', SPECS, '--specs', SPECS, '--roadmap', ROADMAP],
      ['--specs', SPECS, '--roadmap', path.join(FIX, 'no-such-roadmap.md')],
    ]) {
      const r = run(argv);
      assert.equal(r.code, 2, `${argv.join(' ')}: ${r.stderr}`);
      assert.ok(r.stderr.startsWith(PREFIX), r.stderr);
      assert.ok(r.stderr.includes(`\n${USAGE_START}`), r.stderr);
      assert.equal(r.stdout, '');
    }
  });

  await t.test('opens no path but its arguments', () => {
    const opened = [];
    const names = [
      'readFileSync',
      'readdirSync',
      'openSync',
      'statSync',
      'lstatSync',
      'existsSync',
      'opendirSync',
      'accessSync',
      'realpathSync',
    ];
    const saved = names.map((n) => [n, fs[n]]);
    for (const [n, orig] of saved) {
      fs[n] = function recorded(p, ...rest) {
        opened.push(`${n} ${typeof p === 'string' ? path.resolve(p) : String(p)}`);
        return orig.call(this, p, ...rest);
      };
    }
    let r;
    try {
      r = run(['--specs', SPECS, '--roadmap', ROADMAP, '--check-ledger', LEDGER_OK]);
    } finally {
      for (const [n, orig] of saved) fs[n] = orig;
    }
    assert.equal(r.code, 0, r.stderr);
    const paths = opened.map((o) => o.slice(o.indexOf(' ') + 1));
    const inArgs = (p) => p === ROADMAP || p === LEDGER_OK || p === SPECS || p.startsWith(SPECS + path.sep);
    const outside = paths.filter((p) => !inArgs(p));
    assert.deepEqual(outside, [], `opened: ${opened.join('\n')}`);
    assert.ok(paths.includes(ROADMAP), 'the roadmap was not read');
    assert.ok(paths.includes(LEDGER_OK), 'the ledger was not read');
    assert.ok(paths.includes(SPECS), 'the specs dir was not listed');
  });
});
