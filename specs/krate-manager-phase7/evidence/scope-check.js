#!/usr/bin/env node
// Scope check for Krate Manager Phase 7 (L-SCOPE; spec FR-063, FR-090, FR-091, SC-025; plan.md §7.1 SC-025, §7.4 step
// 5, §8; tasks.md T009, rule 11).
//
// WHY: outside apps/krate-manager/ Phase 7 may change only the FR-091 paths (rule 11): specs/krate-manager-phase7/**,
// the regenerated specs/INDEX.md, tools/feed-contract/tests/manager-ci-shape.test.js (EXPECTED_REQUIRED only, T010),
// and in specs/Krate-manager-roadmap.md only the Phase 7 status line (main loop at close). Nothing under plugins/, dsp/,
// cmake/, extern/, no other tools/feed-contract/ file, and no workflow at all (FR-090: manager-ci.yml is unchanged this
// phase). Inside apps/krate-manager/ the rule 11 exclusions (src/** for FR-063, index.html, tauri*.conf.json,
// package.json, package-lock.json, crates/helper/**, eslint.config.mjs, vite.config.ts, tsconfig*.json, clippy.toml,
// CHANGELOG.md, version.json, src-tauri/Cargo.toml) must not change either; each one changed is reported. The name-only
// diff cannot see a line-level breach, so two -U0 diffs are checked against pre-image windows: the roadmap admits only
// an insertion of two lines after old line 599 or 600 (the Phase 7 status line and its blank line, between `### Phase 7:
// Download and Verify` at 599 and `**Spec:** \`krate-manager-phase7\`` at 601, Phase 6's form at line 559); the shape
// test admits only hunks inside `const EXPECTED_REQUIRED = {` (old line 337) and its closing `};` (old line 597): an
// insertion after old lines 337-596, a change within old lines 338-596. The anchors of both windows are read from the
// base's own blobs (`git show <base>:<path>`), so a stale line number reads as a breach instead of a vacuous pass. A
// never-committed file is invisible to `git diff --name-only`, so the untracked listing is added (FR-063: a new file
// under apps/krate-manager/src/ is refused like a tracked change). The scope base is 2f3c93d7 (tasks.md header and T001:
// `git rev-parse --short HEAD` before the build). It spawns git, so it is evidence, not a test; it lives under specs/,
// outside the hygiene scan. Adapted from specs/krate-manager-phase6/evidence/scope-check.js (manager-ci.yml and the
// Phase 10 FR-092 window are no longer allowed; the shape-test window and the anchor check are new).
//
// Usage (repo root or anywhere inside the repo):
//   node specs/krate-manager-phase7/evidence/scope-check.js              # exit 0 clean, 1 breaches, 2 git error
//   node specs/krate-manager-phase7/evidence/scope-check.js --self-test  # the checkers against synthetic inputs
'use strict';
const childProcess = require('child_process');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const BASE = '2f3c93d7';
const APP_PREFIX = 'apps/krate-manager/';
const ROADMAP = 'specs/Krate-manager-roadmap.md';
const SHAPE_TEST = 'tools/feed-contract/tests/manager-ci-shape.test.js';

// tasks.md rule 11 (spec FR-091).
const EXACT = new Set([
  SHAPE_TEST, // line-level window below
  'specs/INDEX.md',
  ROADMAP, // line-level window below
]);
const PREFIXES = [APP_PREFIX, 'specs/krate-manager-phase7/']; // apps/krate-manager/ minus APP_EXCLUDED
// Rule 11's exclusions inside apps/krate-manager/, matched against the path relative to it. The two globs
// (tauri*.conf.json, tsconfig*.json) match the file name in any directory; the rest are app-root paths.
const APP_EXCLUDED = [
  [/^src\//, 'src/** (FR-063)'],
  [/^src-tauri\/Cargo\.toml$/, 'src-tauri/Cargo.toml'],
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

// Pre-image (base) windows. An insertion-only hunk (`@@ -a,0 +c[,d] @@`) adds d lines after old line a; `lines` is the
// exact number of added lines the window admits (undefined = any). A change hunk's old range must lie in a change window.
const ROADMAP_WINDOWS = {
  insert: [{ from: 599, to: 600, lines: 2, what: 'the Phase 7 status line and its blank line' }],
  change: [],
};
const SHAPE_WINDOWS = {
  insert: [{ from: 337, to: 596, what: 'inside EXPECTED_REQUIRED' }],
  change: [{ from: 338, to: 596, what: 'inside EXPECTED_REQUIRED' }],
};
// The base lines the windows lean on (1-based line -> exact text).
const ANCHORS = {
  [ROADMAP]: { 599: '### Phase 7: Download and Verify', 601: '**Spec:** `krate-manager-phase7`' },
  [SHAPE_TEST]: { 337: 'const EXPECTED_REQUIRED = {', 597: '};' },
};

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
    let why = 'is not an FR-091 path';
    if (forbidden) why = `is under ${forbidden}, which FR-090 forbids`;
    else if (p.startsWith(WORKFLOWS)) why = 'is a workflow, which FR-090 forbids';
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

/** Checker 2: every hunk of `file` lies in one of its windows (one breach per hunk). */
function windowBreaches(file, hunks, windows) {
  const breaches = [];
  for (const h of hunks) {
    let ok;
    if (h.b === 0) {
      ok = windows.insert.some((w) => h.a >= w.from && h.a <= w.to && (w.lines === undefined || h.d === w.lines));
    } else {
      const hi = h.a + h.b - 1;
      ok = windows.change.some((w) => h.a >= w.from && hi <= w.to);
    }
    if (!ok) {
      const where = h.b === 0 ? `insertion of ${h.d} line(s) after old line ${h.a}` : `old lines ${h.a}-${h.a + h.b - 1}`;
      breaches.push(`scope-check: ${file} hunk ${h.header} (${where}) is outside the FR-091 windows (tasks.md rule 11)`);
    }
  }
  return breaches;
}

const roadmapBreaches = (hunks) => windowBreaches(ROADMAP, hunks, ROADMAP_WINDOWS);
const shapeBreaches = (hunks) => windowBreaches(SHAPE_TEST, hunks, SHAPE_WINDOWS);

/** Checker 3: the base blob of `file` holds each anchor line where the windows expect it (one breach per anchor). */
function anchorBreaches(file, baseText, anchors) {
  const all = baseText.split(/\r?\n/);
  return Object.entries(anchors).filter(([n, want]) => all[Number(n) - 1] !== want)
    .map(([n, want]) => `scope-check: ${file} line ${n} at ${BASE} is ${JSON.stringify(all[Number(n) - 1])}, the windows` +
      ` expect ${JSON.stringify(want)} (update the window constants)`);
}

function selfTest() {
  const allowedPaths = [
    SHAPE_TEST,
    'specs/krate-manager-phase7/evidence/scope-check.js',
    'specs/krate-manager-phase7/spec.md',
    'specs/INDEX.md',
    ROADMAP,
    `${APP_PREFIX}Cargo.toml`,
    `${APP_PREFIX}Cargo.lock`,
    `${APP_PREFIX}crates/core/Cargo.toml`,
    `${APP_PREFIX}crates/core/src/download.rs`,
    `${APP_PREFIX}crates/core/src/verify_tests.rs`,
    `${APP_PREFIX}crates/core/tests/injection.rs`,
    `${APP_PREFIX}src-tauri/src/commands.rs`,
    `${APP_PREFIX}src-tauri/build.rs`,
    `${APP_PREFIX}src-tauri/capabilities/main.json`,
    `${APP_PREFIX}tests/required-tests.json`,
    `${APP_PREFIX}tests/fixtures/clippy-guard/Cargo.lock`,
  ].join('\n');
  // The status line at close (either alignment git may print).
  const statusHunk = '@@ -599,0 +600,2 @@ Part B';
  // T010's appends: after the opening line, inside cargo.all, before the closing `};` (CRLF diff), a changed last item.
  const shapeHunks = ['@@ -337,0 +338 @@ x', '@@ -450,0 +451,58 @@ x', '@@ -596,0 +597,3 @@ x\r', '@@ -500 +500,2 @@ x']
    .join('\n');
  const tracked = (p) => () => pathBreaches(collectPaths(allowedPaths, `${p}\n`, ''));
  const untracked = (p) => () => pathBreaches(collectPaths(allowedPaths, '', `${p}\r\n`));
  const roadmap = (h) => () => roadmapBreaches(parseHunks(`${statusHunk}\n${h}`));
  const shape = (h) => () => shapeBreaches(parseHunks(`${shapeHunks}\n${h}`));
  const roadmapBase = Array.from({ length: 610 }, (_, i) => `line ${i + 1}`);
  roadmapBase[598] = '### Phase 7: Download and Verify';
  roadmapBase[600] = '**Spec:** `krate-manager-phase7`';
  const shapeBase = Array.from({ length: 941 }, (_, i) => `line ${i + 1}`);
  shapeBase[336] = 'const EXPECTED_REQUIRED = {';
  shapeBase[596] = '};';
  const anchors = (file, lines) => () => anchorBreaches(file, lines.join('\r\n'), ANCHORS[file] || {});
  const cases = [
    { name: 'allowed paths', run: () => pathBreaches(collectPaths(allowedPaths, '')), want: 0 },
    { name: 'Phase 7 status line inserted after old line 599', run: () => roadmapBreaches(parseHunks(statusHunk)), want: 0 },
    { name: 'Phase 7 status line inserted after old line 600', run: () => roadmapBreaches(parseHunks('@@ -600,0 +601,2 @@')), want: 0 },
    { name: 'shape-test hunks inside EXPECTED_REQUIRED', run: () => shapeBreaches(parseHunks(shapeHunks)), want: 0 },
    { name: 'roadmap base anchors at 599 / 601 (CRLF)', run: anchors(ROADMAP, roadmapBase), want: 0 },
    { name: 'shape-test base anchors at 337 / 597 (CRLF)', run: anchors(SHAPE_TEST, shapeBase), want: 0 },
    { name: 'roadmap anchors shifted by one line', run: anchors(ROADMAP, ['x', ...roadmapBase]), want: 2 },
    { name: 'shape-test anchors with no table', run: anchors(SHAPE_TEST, ['x']), want: 2 },
    { name: `${APP_PREFIX}src/main.ts (tracked, FR-063)`, run: tracked(`${APP_PREFIX}src/main.ts`), want: 1 },
    { name: `untracked ${APP_PREFIX}src/main.ts (FR-063)`, run: untracked(`${APP_PREFIX}src/main.ts`), want: 1 },
    { name: `untracked ${APP_PREFIX}src/new-caller.ts (FR-063)`, run: untracked(`${APP_PREFIX}src/new-caller.ts`), want: 1 },
    { name: `${APP_PREFIX}package.json`, run: tracked(`${APP_PREFIX}package.json`), want: 1 },
    { name: `${APP_PREFIX}src-tauri/Cargo.toml`, run: tracked(`${APP_PREFIX}src-tauri/Cargo.toml`), want: 1 },
    { name: `${APP_PREFIX}src-tauri/tauri.conf.json`, run: tracked(`${APP_PREFIX}src-tauri/tauri.conf.json`), want: 1 },
    { name: `${APP_PREFIX}src-tauri/tauri.windows.conf.json`, run: tracked(`${APP_PREFIX}src-tauri/tauri.windows.conf.json`), want: 1 },
    { name: `${APP_PREFIX}clippy.toml`, run: tracked(`${APP_PREFIX}clippy.toml`), want: 1 },
    { name: `${APP_PREFIX}crates/helper/src/main.rs`, run: tracked(`${APP_PREFIX}crates/helper/src/main.rs`), want: 1 },
    { name: `untracked ${APP_PREFIX}tsconfig.node.json`, run: untracked(`${APP_PREFIX}tsconfig.node.json`), want: 1 },
    { name: `${APP_PREFIX}CHANGELOG.md`, run: tracked(`${APP_PREFIX}CHANGELOG.md`), want: 1 },
    { name: `${APP_PREFIX}version.json`, run: tracked(`${APP_PREFIX}version.json`), want: 1 },
    { name: '.github/workflows/manager-ci.yml', run: tracked('.github/workflows/manager-ci.yml'), want: 1 },
    { name: '.github/workflows/release.yml', run: tracked('.github/workflows/release.yml'), want: 1 },
    { name: 'plugins/x', run: tracked('plugins/x'), want: 1 },
    { name: 'untracked dsp/x', run: untracked('dsp/x'), want: 1 },
    { name: 'tools/feed-contract/versions.v2.schema.json', run: tracked('tools/feed-contract/versions.v2.schema.json'), want: 1 },
    { name: 'tools/feed-contract/tests/other.test.js', run: tracked('tools/feed-contract/tests/other.test.js'), want: 1 },
    { name: 'specs/krate-manager-phase6/spec.md', run: tracked('specs/krate-manager-phase6/spec.md'), want: 1 },
    { name: 'specs/krate-manager/probes/results.md', run: tracked('specs/krate-manager/probes/results.md'), want: 1 },
    { name: 'roadmap hunk at old line 300', run: roadmap('@@ -300 +302 @@'), want: 1 },
    { name: 'roadmap one-line insertion after 599', run: roadmap('@@ -599,0 +600 @@'), want: 1 },
    { name: 'roadmap insertion after 598', run: roadmap('@@ -598,0 +599,2 @@'), want: 1 },
    { name: 'roadmap insertion after 601', run: roadmap('@@ -601,0 +603,2 @@'), want: 1 },
    { name: 'roadmap change at old line 601', run: roadmap('@@ -601 +603 @@'), want: 1 },
    { name: 'roadmap Phase 6 status window (old 557)', run: roadmap('@@ -557,0 +558,2 @@'), want: 1 },
    { name: 'shape-test change at old line 739 (checkRequired)', run: shape('@@ -739,2 +739,3 @@'), want: 1 },
    { name: 'shape-test insertion after old line 597 (the closing brace)', run: shape('@@ -597,0 +598,2 @@'), want: 1 },
    { name: 'shape-test insertion after old line 336 (before the table)', run: shape('@@ -336,0 +337 @@'), want: 1 },
    { name: 'shape-test change of old line 337 (the declaration)', run: shape('@@ -337 +337 @@'), want: 1 },
    { name: 'shape-test change spanning 596-597', run: shape('@@ -596,2 +596,3 @@'), want: 1 },
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
  const shapeDiff = git([...q, 'diff', ...diffOpts, '-U0', BASE, '--', SHAPE_TEST]);
  const roadmapBase = git(['show', `${BASE}:${ROADMAP}`]);
  const shapeBase = git(['show', `${BASE}:${SHAPE_TEST}`]);
  if ([nameOnly, untracked, roadmapDiff, shapeDiff, roadmapBase, shapeBase].includes(null)) return 2;
  const paths = collectPaths(nameOnly, untracked);
  const roadmapHunks = parseHunks(roadmapDiff);
  const shapeHunks = parseHunks(shapeDiff);
  const breaches = [
    ...anchorBreaches(ROADMAP, roadmapBase, ANCHORS[ROADMAP]),
    ...anchorBreaches(SHAPE_TEST, shapeBase, ANCHORS[SHAPE_TEST]),
    ...pathBreaches(paths),
    ...roadmapBreaches(roadmapHunks),
    ...shapeBreaches(shapeHunks),
  ];
  if (breaches.length) {
    for (const b of breaches) console.log(b);
    return 1;
  }
  const app = paths.filter((p) => p.startsWith(APP_PREFIX)).length;
  console.log(`scope-check: ${paths.length} paths (${app} under ${APP_PREFIX}, ${paths.length - app} outside),` +
    ` ${roadmapHunks.length} roadmap hunks, ${shapeHunks.length} shape-test hunks: OK (base ${BASE})`);
  return 0;
}

process.exit(main(process.argv.slice(2)));
