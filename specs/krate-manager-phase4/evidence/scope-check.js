#!/usr/bin/env node
// Scope check for Krate Manager Phase 4 (spec FR-090, FR-091, SC-023; plan.md §4.19; tasks.md T018, TA-5, rule 11).
//
// WHY: outside apps/krate-manager/ Phase 4 may change only the FR-090 paths, and in specs/Krate-manager-roadmap.md only
// four places: the Phase 4 status line (inserted after the heading at :463 / :464, main loop at close), :473 (the D17
// deferral sentence), :497 (the D16 apt parenthetical; :496 stays byte-identical) and the Phase 12 first-launch bullet
// :882-887 (the Windows-floor sentence). The name-only diff cannot see a line-level breach, so the roadmap's -U0 hunk
// headers are checked too. A never-committed file is invisible to `git diff --name-only`, so the untracked listing is
// added (TA-5). The scope base is c66c8557 (tasks.md T018 main-loop note: ruling P-4's one-line workflow fix was
// committed on its own before the build stage). It spawns git, so it is evidence, not a test; it lives under specs/,
// outside the hygiene scan.
//
// Usage (repo root or anywhere inside the repo):
//   node specs/krate-manager-phase4/evidence/scope-check.js              # exit 0 clean, 1 breaches, 2 git error
//   node specs/krate-manager-phase4/evidence/scope-check.js --self-test  # the two checkers against synthetic inputs
'use strict';
const childProcess = require('child_process');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const BASE = 'c66c8557';
const APP_PREFIX = 'apps/krate-manager/';
const ROADMAP = 'specs/Krate-manager-roadmap.md';

// tasks.md rule 11 (spec FR-090).
const EXACT = new Set([
  '.github/workflows/manager-ci.yml',
  '.github/workflows/feed-tools.yml',
  'tools/lint-manager-version.js',
  'tools/feed-contract/tests/lint-manager-version.test.js',
  'tools/feed-contract/tests/manager-ci-shape.test.js',
  'tools/feed-contract/tests/hygiene.test.js',
  'specs/INDEX.md',
  ROADMAP, // line-level windows below
]);
const PREFIXES = ['specs/krate-manager-phase4/'];

// Pre-image (c66c8557) windows of the roadmap. An insertion-only hunk (`@@ -a,0 ...`) adds lines after old line a.
const INSERT_WINDOWS = [
  [463, 464], // the Phase 4 status line after the heading (main loop at close)
  [882, 887], // the Phase 12 first-launch bullet's appended sentence
];
const CHANGE_WINDOWS = [
  [473, 473], // the D17 sentence
  [497, 497], // the D16 apt parenthetical
  [882, 887], // the Phase 12 first-launch bullet
];

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

/** Checker 1: every path outside apps/krate-manager/ is a rule-11 path. */
function pathBreaches(paths) {
  return paths
    .filter((p) => !(EXACT.has(p) || PREFIXES.some((pre) => p.startsWith(pre))))
    .map((p) => `scope-check: ${p} is not an FR-090 path (tasks.md rule 11)`);
}

/** Hunk headers `@@ -a[,b] +c[,d] @@` of a -U0 diff (a content line starts with -, + or \, never with @@). */
function parseHunks(diff) {
  const hunks = [];
  for (const raw of diff.split('\n')) {
    const line = raw.endsWith('\r') ? raw.slice(0, -1) : raw;
    const m = /^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@/.exec(line);
    if (m) hunks.push({ header: m[0], a: Number(m[1]), b: m[2] === undefined ? 1 : Number(m[2]) });
  }
  return hunks;
}

/** Checker 2: every roadmap hunk's old-side range lies in an allowed window. */
function hunkBreaches(hunks) {
  const inside = (lo, hi, windows) => windows.some(([from, to]) => lo >= from && hi <= to);
  const breaches = [];
  for (const h of hunks) {
    const ok = h.b === 0 ? inside(h.a, h.a, INSERT_WINDOWS) : inside(h.a, h.a + h.b - 1, CHANGE_WINDOWS);
    if (!ok) {
      const where = h.b === 0 ? `insertion after old line ${h.a}` : `old lines ${h.a}-${h.a + h.b - 1}`;
      breaches.push(`scope-check: ${ROADMAP} hunk ${h.header} (${where}) is outside the FR-090 windows (plan §4.19)`);
    }
  }
  return breaches;
}

function selfTest() {
  const allowedPaths = [
    '.github/workflows/manager-ci.yml',
    '.github/workflows/feed-tools.yml',
    'tools/lint-manager-version.js',
    'tools/feed-contract/tests/lint-manager-version.test.js',
    'tools/feed-contract/tests/manager-ci-shape.test.js',
    'tools/feed-contract/tests/hygiene.test.js',
    'specs/krate-manager-phase4/evidence/scope-check.js',
    'specs/INDEX.md',
    ROADMAP,
  ].join('\n');
  const allowedHunks = [
    '@@ -463,0 +464,2 @@ Part B',
    '@@ -473 +475,3 @@ Components:',
    '@@ -497 +501 @@ Components:',
    '@@ -887 +891,3 @@ Components:',
  ].join('\n');
  const cases = [
    { name: 'allowed paths (apps/krate-manager/ dropped)', run: () => pathBreaches(collectPaths(allowedPaths, `${APP_PREFIX}Cargo.toml\n`)), want: 0 },
    { name: 'allowed roadmap hunks', run: () => hunkBreaches(parseHunks(allowedHunks)), want: 0 },
    { name: 'insertion inside the Phase 12 bullet', run: () => hunkBreaches(parseHunks('@@ -885,0 +889 @@')), want: 0 },
    { name: 'plugins/x.cpp', run: () => pathBreaches(collectPaths(allowedPaths, 'plugins/x.cpp\n')), want: 1 },
    { name: '.github/workflows/ci.yml', run: () => pathBreaches(collectPaths('.github/workflows/ci.yml\n', allowedPaths)), want: 1 },
    { name: 'hunk at old line 496', run: () => hunkBreaches(parseHunks(`${allowedHunks}\n@@ -496 +500 @@`)), want: 1 },
    { name: 'hunk at old line 500', run: () => hunkBreaches(parseHunks(`${allowedHunks}\n@@ -500 +504 @@`)), want: 1 },
    { name: 'hunk spanning old lines 496-497', run: () => hunkBreaches(parseHunks('@@ -496,2 +500,2 @@')), want: 1 },
    { name: 'insertion after old line 497', run: () => hunkBreaches(parseHunks('@@ -497,0 +498 @@')), want: 1 },
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
  const nameOnly = git([...q, 'diff', ...diffOpts, '--name-only', BASE]);
  const untracked = git([...q, 'ls-files', '--others', '--exclude-standard']);
  const roadmapDiff = git([...q, 'diff', ...diffOpts, '-U0', BASE, '--', ROADMAP]);
  if (nameOnly === null || untracked === null || roadmapDiff === null) return 2;
  const paths = collectPaths(nameOnly, untracked);
  const hunks = parseHunks(roadmapDiff);
  const breaches = [...pathBreaches(paths), ...hunkBreaches(hunks)];
  if (breaches.length) {
    for (const b of breaches) console.log(b);
    return 1;
  }
  console.log(`scope-check: ${paths.length} paths, ${hunks.length} roadmap hunks: OK`);
  return 0;
}

process.exit(main(process.argv.slice(2)));
