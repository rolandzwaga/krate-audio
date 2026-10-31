#!/usr/bin/env node
// Scope check for Krate Manager Phase 5 (spec FR-090 as amended by plan SI-41, FR-091, SC-044 diff half; plan.md §4.14,
// §7.2 FR-090, §8; tasks.md T015, rule 11).
//
// WHY: outside apps/krate-manager/ Phase 5 may change only the FR-090 paths (rule 11): the manager workflow and its
// shape test, feed-tools.yml, specs/krate-manager-phase5/**, the regenerated specs/INDEX.md, one added row in
// specs/krate-manager/probes/results.md (SI-41) and four places in specs/Krate-manager-roadmap.md. Nothing under
// plugins/, dsp/, cmake/, extern/, no tools/feed-contract/ file but the shape test, and no other workflow. The name-only
// diff cannot see a line-level breach, so the roadmap's -U0 hunk headers are checked against the pre-image windows
// (an insertion after old line 528 or 529: the Phase 5 status line, main loop at close; old lines 534-535: the "check
// now" sentence; an insertion after old line 571: the Phase 6 bullet; an insertion of exactly one line after old line
// 897: the RustSec bullet, TA-19), and results.md's numstat must be exactly `1 0` with the added line being the R-15
// row. A never-committed file is invisible to `git diff --name-only`, so the untracked listing is added. The scope base
// is a145271b (tasks.md T001: `git rev-parse --short HEAD` before the build). It spawns git, so it is evidence, not a
// test; it lives under specs/, outside the hygiene scan.
//
// Usage (repo root or anywhere inside the repo):
//   node specs/krate-manager-phase5/evidence/scope-check.js              # exit 0 clean, 1 breaches, 2 git error
//   node specs/krate-manager-phase5/evidence/scope-check.js --self-test  # the checkers against synthetic inputs
'use strict';
const childProcess = require('child_process');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const BASE = 'a145271b';
const APP_PREFIX = 'apps/krate-manager/';
const ROADMAP = 'specs/Krate-manager-roadmap.md';
const RESULTS = 'specs/krate-manager/probes/results.md';
const SHAPE_TEST = 'tools/feed-contract/tests/manager-ci-shape.test.js';
// plan §4.14 / tasks.md T001: the one row results.md gains (SI-41).
const RESULTS_ROW_PREFIX = '| Windows: is "Automatically detect settings" (WPAD) on for a user profile? |';

// tasks.md rule 11 (spec FR-090 as amended by SI-41).
const EXACT = new Set([
  '.github/workflows/manager-ci.yml',
  '.github/workflows/feed-tools.yml',
  SHAPE_TEST,
  'specs/INDEX.md',
  RESULTS, // numstat and row checked below
  ROADMAP, // line-level windows below
]);
const PREFIXES = ['specs/krate-manager-phase5/'];
// Named so the breach says why; any other path outside the allow-list is a breach too.
const FORBIDDEN_PREFIXES = ['plugins/', 'dsp/', 'cmake/', 'extern/', 'tools/feed-contract/'];
const WORKFLOWS = '.github/workflows/';

// Pre-image (a145271b) windows of the roadmap. An insertion-only hunk (`@@ -a,0 +c[,d] @@`) adds d lines after old
// line a; `lines` is the exact number of added lines the window admits (undefined = any).
const INSERT_WINDOWS = [
  { from: 528, to: 529, what: 'the Phase 5 status line' },
  { from: 534, to: 534, what: 'the "check now" sentence' },
  { from: 571, to: 571, what: 'the Phase 6 bullet' },
  { from: 897, to: 897, lines: 1, what: 'the RustSec bullet (one physical line, TA-19)' },
];
const CHANGE_WINDOWS = [{ from: 534, to: 535, what: 'the "check now" sentence' }];

/** git quotes a path with unusual characters ("a b.txt"); core.quotepath=off leaves non-ASCII bytes alone. */
function unquote(p) {
  if (!(p.length >= 2 && p.startsWith('"') && p.endsWith('"'))) return p;
  try {
    return JSON.parse(p);
  } catch {
    return p.slice(1, -1);
  }
}

/** One path per non-empty line (`git diff --name-only`, `git ls-files`), deduplicated, apps/krate-manager/ dropped. */
function collectPaths(...listings) {
  const paths = [];
  for (const text of listings) {
    for (const line of text.split(/\r?\n/)) {
      if (!line.trim()) continue;
      const p = unquote(line.trim());
      if (!p.startsWith(APP_PREFIX) && !paths.includes(p)) paths.push(p);
    }
  }
  return paths;
}

/** Checker 1: every path outside apps/krate-manager/ is a rule-11 path (one breach per path). */
function pathBreaches(paths) {
  const breaches = [];
  for (const p of paths) {
    if (EXACT.has(p) || PREFIXES.some((pre) => p.startsWith(pre))) continue;
    const forbidden = FORBIDDEN_PREFIXES.find((pre) => p.startsWith(pre));
    let why = 'is not an FR-090 path';
    if (forbidden) why = `is under ${forbidden}, which FR-090 forbids`;
    else if (p.startsWith(WORKFLOWS)) why = 'is a workflow other than manager-ci.yml / feed-tools.yml';
    breaches.push(`scope-check: ${p} ${why} (tasks.md rule 11)`);
  }
  return breaches;
}

/** Hunk headers `@@ -a[,b] +c[,d] @@` of a -U0 diff (a content line starts with -, + or \, never with @@). */
function parseHunks(diff) {
  const hunks = [];
  for (const raw of diff.split('\n')) {
    const line = raw.endsWith('\r') ? raw.slice(0, -1) : raw;
    const m = /^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@/.exec(line);
    if (m) {
      hunks.push({
        header: m[0],
        a: Number(m[1]),
        b: m[2] === undefined ? 1 : Number(m[2]),
        d: m[4] === undefined ? 1 : Number(m[4]),
      });
    }
  }
  return hunks;
}

/** Checker 2: every roadmap hunk's old-side range lies in an allowed window (one breach per hunk). */
function hunkBreaches(hunks) {
  const breaches = [];
  for (const h of hunks) {
    let ok;
    if (h.b === 0) {
      ok = INSERT_WINDOWS.some((w) => h.a >= w.from && h.a <= w.to && (w.lines === undefined || h.d === w.lines));
    } else {
      const hi = h.a + h.b - 1;
      ok = CHANGE_WINDOWS.some((w) => h.a >= w.from && hi <= w.to);
    }
    if (!ok) {
      const where = h.b === 0 ? `insertion of ${h.d} line(s) after old line ${h.a}` : `old lines ${h.a}-${h.a + h.b - 1}`;
      breaches.push(`scope-check: ${ROADMAP} hunk ${h.header} (${where}) is outside the FR-090 windows (plan §4.14)`);
    }
  }
  return breaches;
}

/**
 * Checker 3 (SI-41, SC-044): results.md's `git diff --numstat` is exactly `1 0`, and the -U0 diff's added lines are
 * exactly one, the R-15 row. At most two breaches: the numstat, then the added lines.
 */
function resultsBreaches(numstat, diff) {
  const breaches = [];
  const rows = numstat.split(/\r?\n/).filter((l) => l.trim());
  const counts = rows.map((l) => l.split('\t').slice(0, 2).join(' '));
  if (!(counts.length === 1 && counts[0] === '1 0')) {
    const got = counts.length ? counts.join(', ') : 'no change';
    breaches.push(`scope-check: ${RESULTS} numstat is ${got}, want 1 0 (one added row, nothing removed; plan SI-41)`);
  }
  const added = diff
    .split('\n')
    .map((l) => (l.endsWith('\r') ? l.slice(0, -1) : l))
    .filter((l) => l.startsWith('+') && !l.startsWith('+++'))
    .map((l) => l.slice(1));
  if (!(added.length === 1 && added[0].startsWith(RESULTS_ROW_PREFIX))) {
    const first = added.length ? ` (first: ${JSON.stringify(added[0].slice(0, 80))})` : '';
    breaches.push(
      `scope-check: ${RESULTS} adds ${added.length} line(s)${first}, want exactly the R-15 row starting ${RESULTS_ROW_PREFIX}`,
    );
  }
  return breaches;
}

function selfTest() {
  const allowedPaths = [
    '.github/workflows/manager-ci.yml',
    '.github/workflows/feed-tools.yml',
    SHAPE_TEST,
    'specs/krate-manager-phase5/evidence/scope-check.js',
    'specs/krate-manager-phase5/spec.md',
    'specs/INDEX.md',
    RESULTS,
    ROADMAP,
  ].join('\n');
  // The three hunks T001 left (git diff -U0 a145271b, this session) plus the status line at close.
  const allowedHunks = [
    '@@ -528,0 +529,2 @@ Part B',
    '@@ -534,2 +536,3 @@ Components:',
    '@@ -571,0 +575,3 @@ Components:',
    '@@ -897,0 +904 @@ Components:',
  ].join('\n');
  const okNumstat = `1\t0\t${RESULTS}\n`;
  const okRow = `${RESULTS_ROW_PREFIX} **On, on the user's own (not fresh) Windows 11 profile**: ... |`;
  const resultsDiff = (...lines) =>
    [`diff --git a/${RESULTS} b/${RESULTS}`, `--- a/${RESULTS}`, `+++ b/${RESULTS}`, '@@ -20,0 +21 @@ x', ...lines].join('\n');
  const withPath = (p) => () => pathBreaches(collectPaths(allowedPaths, `${p}\n`));
  const withHunk = (h) => () => hunkBreaches(parseHunks(`${allowedHunks}\n${h}`));
  const cases = [
    { name: 'allowed paths (apps/krate-manager/ dropped)', run: () => pathBreaches(collectPaths(allowedPaths, `${APP_PREFIX}Cargo.toml\n`)), want: 0 },
    { name: 'allowed roadmap hunks', run: () => hunkBreaches(parseHunks(allowedHunks)), want: 0 },
    { name: 'status line inserted after old line 529', run: () => hunkBreaches(parseHunks('@@ -529,0 +530 @@')), want: 0 },
    { name: 'T001 hunks as git printed them', run: () => hunkBreaches(parseHunks('@@ -534,2 +534,3 @@\n@@ -571,0 +573,3 @@\n@@ -897,0 +902 @@')), want: 0 },
    { name: 'allowed results.md row (CRLF diff)', run: () => resultsBreaches(okNumstat, `${resultsDiff(`+${okRow}`)}\r\n`), want: 0 },
    { name: 'plugins/x.cpp', run: withPath('plugins/x.cpp'), want: 1 },
    { name: 'dsp/include/x.h', run: withPath('dsp/include/x.h'), want: 1 },
    { name: 'tools/feed-contract/versions.v2.schema.json', run: withPath('tools/feed-contract/versions.v2.schema.json'), want: 1 },
    { name: 'tools/feed-contract/tests/hygiene.test.js', run: withPath('tools/feed-contract/tests/hygiene.test.js'), want: 1 },
    { name: '.github/workflows/ci.yml', run: () => pathBreaches(collectPaths('.github/workflows/ci.yml\n', allowedPaths)), want: 1 },
    { name: 'specs/krate-manager-phase4/spec.md', run: withPath('specs/krate-manager-phase4/spec.md'), want: 1 },
    { name: 'roadmap hunk at old line 540', run: withHunk('@@ -540 +545 @@'), want: 1 },
    { name: 'roadmap insertion at old line 572', run: withHunk('@@ -572,0 +578 @@'), want: 1 },
    { name: 'roadmap change at old line 572', run: withHunk('@@ -572 +578 @@'), want: 1 },
    { name: 'roadmap hunk spanning old lines 535-536', run: withHunk('@@ -535,2 +540,2 @@'), want: 1 },
    { name: 'roadmap modified line at 897', run: () => hunkBreaches(parseHunks('@@ -897 +903 @@')), want: 1 },
    { name: 'roadmap two-line insertion at 897', run: () => hunkBreaches(parseHunks('@@ -897,0 +903,2 @@')), want: 1 },
    { name: 'results.md deleted line', run: () => resultsBreaches(`1\t1\t${RESULTS}\n`, resultsDiff('-| old row |', `+${okRow}`)), want: 1 },
    { name: 'results.md two added lines', run: () => resultsBreaches(`2\t0\t${RESULTS}\n`, resultsDiff(`+${okRow}`, '+| another row |')), want: 2 },
    { name: 'results.md one added line with other text', run: () => resultsBreaches(okNumstat, resultsDiff('+| Some other question? | x | y |')), want: 1 },
    { name: 'results.md unchanged', run: () => resultsBreaches('', ''), want: 2 },
  ];
  let failed = 0;
  for (const c of cases) {
    const got = c.run();
    const ok = got.length === c.want;
    if (!ok) failed++;
    console.log(`${ok ? 'ok    ' : 'FAILED'} ${c.name}: ${got.length} breach(es), want ${c.want}`);
    for (const b of got) console.log(`         ${b}`);
  }
  console.log(failed ? `SELF-TEST RED (${failed} of ${cases.length})` : `SELF-TEST GREEN (${cases.length} cases)`);
  return failed ? 1 : 0;
}

function git(args) {
  const r = childProcess.spawnSync('git', args, { cwd: ROOT, encoding: 'utf8', windowsHide: true, maxBuffer: 1 << 28 });
  if (r.error || r.status !== 0) {
    console.log(`scope-check: git ${args.join(' ')} failed: ${r.error ? r.error.message : r.stderr.trim()}`);
    return null;
  }
  return r.stdout;
}

function main(argv) {
  if (argv.includes('--self-test')) return selfTest();
  const q = ['-c', 'core.quotepath=off'];
  const diffOpts = ['--no-color', '--no-ext-diff'];
  // --no-renames: a rename lists both its old and its new path, so a file moved out of plugins/ is still seen.
  const nameOnly = git([...q, 'diff', ...diffOpts, '--no-renames', '--name-only', BASE]);
  const untracked = git([...q, 'ls-files', '--others', '--exclude-standard']);
  const roadmapDiff = git([...q, 'diff', ...diffOpts, '-U0', BASE, '--', ROADMAP]);
  const resultsNumstat = git([...q, 'diff', ...diffOpts, '--numstat', BASE, '--', RESULTS]);
  const resultsDiff = git([...q, 'diff', ...diffOpts, '-U0', BASE, '--', RESULTS]);
  if ([nameOnly, untracked, roadmapDiff, resultsNumstat, resultsDiff].includes(null)) return 2;
  const paths = collectPaths(nameOnly, untracked);
  const hunks = parseHunks(roadmapDiff);
  const breaches = [...pathBreaches(paths), ...hunkBreaches(hunks), ...resultsBreaches(resultsNumstat, resultsDiff)];
  if (breaches.length) {
    for (const b of breaches) console.log(b);
    return 1;
  }
  console.log(`scope-check: ${paths.length} paths, ${hunks.length} roadmap hunks, ${RESULTS} 1 0: OK (base ${BASE})`);
  return 0;
}

process.exit(main(process.argv.slice(2)));
