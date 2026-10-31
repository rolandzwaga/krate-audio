#!/usr/bin/env node
// Scope check for Krate Manager Phase 6 (L-SCOPE; spec FR-090, FR-091; plan.md §4.15, §8; tasks.md T010, rule 11).
//
// WHY: outside apps/krate-manager/ Phase 6 may change only the FR-090 paths (rule 11): manager-ci.yml (one step, T012),
// its shape test, specs/krate-manager-phase6/**, the regenerated specs/INDEX.md, and two places in
// specs/Krate-manager-roadmap.md. Nothing under plugins/, dsp/, cmake/, extern/, no tools/feed-contract/ file but the
// shape test, and no other workflow. Inside apps/krate-manager/ the rule 11 exclusions (index.html, tauri*.conf.json,
// package.json, package-lock.json, crates/helper/**, eslint.config.mjs, vite.config.ts, tsconfig*.json, clippy.toml,
// CHANGELOG.md, version.json) must not change either; each one changed is reported. The name-only diff cannot see a
// line-level breach, so the roadmap's -U0 hunk headers are checked against the pre-image windows: an insertion of two
// lines after old line 557 or 558 (the Phase 6 status line and its blank line, between `### Phase 6: Detection and the
// Status Screen` at 557 and `**Spec:** \`krate-manager-phase6\`` at 559, the Phase 5 form of lines 527-531; main loop at
// close), and an insertion after, or a change within, old lines 714-719 (the Phase 10 `run_inno_uninstaller` bullet,
// FR-092, T013). A never-committed file is invisible to `git diff --name-only`, so the untracked listing is added. The
// scope base is 3efe70de (tasks.md header and T001: `git rev-parse --short HEAD` before the build). It spawns git, so it
// is evidence, not a test; it lives under specs/, outside the hygiene scan. Adapted from
// specs/krate-manager-phase5/evidence/scope-check.js (results.md and feed-tools.yml are no longer allowed).
//
// Usage (repo root or anywhere inside the repo):
//   node specs/krate-manager-phase6/evidence/scope-check.js              # exit 0 clean, 1 breaches, 2 git error
//   node specs/krate-manager-phase6/evidence/scope-check.js --self-test  # the checkers against synthetic inputs
'use strict';
const childProcess = require('child_process');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const BASE = '3efe70de';
const APP_PREFIX = 'apps/krate-manager/';
const ROADMAP = 'specs/Krate-manager-roadmap.md';
const SHAPE_TEST = 'tools/feed-contract/tests/manager-ci-shape.test.js';

// tasks.md rule 11 (spec FR-090).
const EXACT = new Set([
  '.github/workflows/manager-ci.yml',
  SHAPE_TEST,
  'specs/INDEX.md',
  ROADMAP, // line-level windows below
]);
const PREFIXES = [APP_PREFIX, 'specs/krate-manager-phase6/']; // apps/krate-manager/ minus APP_EXCLUDED
// Rule 11's exclusions inside apps/krate-manager/, matched against the path relative to it. The two globs
// (tauri*.conf.json, tsconfig*.json) match the file name in any directory; the rest are the app-root files.
const APP_EXCLUDED = [
  [/^index\.html$/, 'index.html'],
  [/(?:^|\/)tauri[^/]*\.conf\.json$/, 'tauri*.conf.json'],
  [/^package\.json$/, 'package.json'],
  [/^package-lock\.json$/, 'package-lock.json'],
  [/^crates\/helper\//, 'crates/helper/**'],
  [/^eslint\.config\.mjs$/, 'eslint.config.mjs'],
  [/^vite\.config\.ts$/, 'vite.config.ts'],
  [/(?:^|\/)tsconfig[^/]*\.json$/, 'tsconfig*.json'],
  [/^clippy\.toml$/, 'clippy.toml'],
  [/^CHANGELOG\.md$/, 'CHANGELOG.md (Q-B)'],
  [/^version\.json$/, 'version.json'],
];
// Named so the breach says why; any other path outside the allow-list is a breach too.
const FORBIDDEN_PREFIXES = ['plugins/', 'dsp/', 'cmake/', 'extern/', 'tools/feed-contract/'];
const WORKFLOWS = '.github/workflows/';

// Pre-image (3efe70de) windows of the roadmap. An insertion-only hunk (`@@ -a,0 +c[,d] @@`) adds d lines after old
// line a; `lines` is the exact number of added lines the window admits (undefined = any).
const INSERT_WINDOWS = [
  { from: 557, to: 558, lines: 2, what: 'the Phase 6 status line and its blank line' },
  { from: 714, to: 719, what: 'the Phase 10 run_inno_uninstaller bullet (FR-092)' },
];
const CHANGE_WINDOWS = [{ from: 714, to: 719, what: 'the Phase 10 run_inno_uninstaller bullet (FR-092)' }];

/** git quotes a path with unusual characters ("a b.txt"); core.quotepath=off leaves non-ASCII bytes alone. */
function unquote(p) {
  if (!(p.length >= 2 && p.startsWith('"') && p.endsWith('"'))) return p;
  try {
    return JSON.parse(p);
  } catch {
    return p.slice(1, -1);
  }
}

/** One path per non-empty line (`git diff --name-only`, `git ls-files`), deduplicated. */
function collectPaths(...listings) {
  const paths = [];
  for (const text of listings) {
    for (const line of text.split(/\r?\n/)) {
      if (!line.trim()) continue;
      const p = unquote(line.trim());
      if (!paths.includes(p)) paths.push(p);
    }
  }
  return paths;
}

/** Checker 1: every changed path is a rule-11 path and no rule-11 exclusion changed (one breach per path). */
function pathBreaches(paths) {
  const breaches = [];
  for (const p of paths) {
    if (p.startsWith(APP_PREFIX)) {
      const excluded = APP_EXCLUDED.find(([re]) => re.test(p.slice(APP_PREFIX.length)));
      if (excluded) breaches.push(`scope-check: ${p} is a rule 11 exclusion (${excluded[1]}) inside ${APP_PREFIX} (tasks.md rule 11)`);
      continue;
    }
    if (EXACT.has(p) || PREFIXES.some((pre) => p.startsWith(pre))) continue;
    const forbidden = FORBIDDEN_PREFIXES.find((pre) => p.startsWith(pre));
    let why = 'is not an FR-090 path';
    if (forbidden) why = `is under ${forbidden}, which FR-090 forbids`;
    else if (p.startsWith(WORKFLOWS)) why = 'is a workflow other than manager-ci.yml';
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
      breaches.push(`scope-check: ${ROADMAP} hunk ${h.header} (${where}) is outside the FR-090 windows (plan §4.15)`);
    }
  }
  return breaches;
}

function selfTest() {
  const allowedPaths = [
    '.github/workflows/manager-ci.yml',
    SHAPE_TEST,
    'specs/krate-manager-phase6/evidence/scope-check.js',
    'specs/krate-manager-phase6/spec.md',
    'specs/INDEX.md',
    ROADMAP,
    `${APP_PREFIX}Cargo.toml`,
    `${APP_PREFIX}Cargo.lock`,
    `${APP_PREFIX}crates/core/src/detect.rs`,
    `${APP_PREFIX}src-tauri/src/os_facts.rs`,
    `${APP_PREFIX}src-tauri/capabilities/main.json`,
    `${APP_PREFIX}tests/required-tests.json`,
    `${APP_PREFIX}tests/fixtures/clippy-guard/Cargo.lock`,
    `${APP_PREFIX}src/status.ts`,
  ].join('\n');
  // The status line at close (either alignment git may print) and T013's FR-092 sentence after line 719.
  const allowedHunks = ['@@ -557,0 +558,2 @@ Part B', '@@ -719,0 +722,3 @@ Components:'].join('\n');
  const tracked = (p) => () => pathBreaches(collectPaths(allowedPaths, `${p}\n`, ''));
  const untracked = (p) => () => pathBreaches(collectPaths(allowedPaths, '', `${p}\r\n`));
  const withHunk = (h) => () => hunkBreaches(parseHunks(`${allowedHunks}\n${h}`));
  const cases = [
    { name: 'allowed paths', run: () => pathBreaches(collectPaths(allowedPaths, '')), want: 0 },
    { name: 'allowed roadmap hunks', run: () => hunkBreaches(parseHunks(allowedHunks)), want: 0 },
    { name: 'status line inserted after old line 558', run: () => hunkBreaches(parseHunks('@@ -558,0 +559,2 @@')), want: 0 },
    { name: 'FR-092 change within 716-719 (CRLF diff)', run: () => hunkBreaches(parseHunks('@@ -716,4 +716,6 @@ x\r\n')), want: 0 },
    { name: 'plugins/x', run: tracked('plugins/x'), want: 1 },
    { name: 'tools/feed-contract/schemas/x', run: tracked('tools/feed-contract/schemas/x'), want: 1 },
    { name: `${APP_PREFIX}package.json`, run: tracked(`${APP_PREFIX}package.json`), want: 1 },
    { name: 'roadmap hunk at old line 300', run: withHunk('@@ -300 +302 @@'), want: 1 },
    { name: 'untracked dsp/x', run: untracked('dsp/x'), want: 1 },
    { name: `${APP_PREFIX}crates/helper/src/main.rs`, run: tracked(`${APP_PREFIX}crates/helper/src/main.rs`), want: 1 },
    { name: `${APP_PREFIX}src-tauri/tauri.windows.conf.json`, run: tracked(`${APP_PREFIX}src-tauri/tauri.windows.conf.json`), want: 1 },
    { name: `untracked ${APP_PREFIX}tsconfig.node.json`, run: untracked(`${APP_PREFIX}tsconfig.node.json`), want: 1 },
    { name: `${APP_PREFIX}CHANGELOG.md`, run: tracked(`${APP_PREFIX}CHANGELOG.md`), want: 1 },
    { name: `${APP_PREFIX}version.json`, run: tracked(`${APP_PREFIX}version.json`), want: 1 },
    { name: '.github/workflows/feed-tools.yml', run: tracked('.github/workflows/feed-tools.yml'), want: 1 },
    { name: 'specs/krate-manager/probes/results.md', run: tracked('specs/krate-manager/probes/results.md'), want: 1 },
    { name: 'specs/krate-manager-phase5/spec.md', run: tracked('specs/krate-manager-phase5/spec.md'), want: 1 },
    { name: 'roadmap one-line insertion after 557', run: withHunk('@@ -557,0 +558 @@'), want: 1 },
    { name: 'roadmap insertion after 556', run: withHunk('@@ -556,0 +557,2 @@'), want: 1 },
    { name: 'roadmap change at old line 559', run: withHunk('@@ -559 +561 @@'), want: 1 },
    { name: 'roadmap change spanning 719-720', run: withHunk('@@ -719,2 +721,3 @@'), want: 1 },
    { name: 'roadmap insertion after 720', run: withHunk('@@ -720,0 +723 @@'), want: 1 },
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
  if ([nameOnly, untracked, roadmapDiff].includes(null)) return 2;
  const paths = collectPaths(nameOnly, untracked);
  const hunks = parseHunks(roadmapDiff);
  const breaches = [...pathBreaches(paths), ...hunkBreaches(hunks)];
  if (breaches.length) {
    for (const b of breaches) console.log(b);
    return 1;
  }
  const app = paths.filter((p) => p.startsWith(APP_PREFIX)).length;
  console.log(`scope-check: ${paths.length} paths (${app} under ${APP_PREFIX}, ${paths.length - app} outside),` +
    ` ${hunks.length} roadmap hunks: OK (base ${BASE})`);
  return 0;
}

process.exit(main(process.argv.slice(2)));
