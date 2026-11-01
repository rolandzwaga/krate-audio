#!/usr/bin/env node
// Scope check for Krate Manager Phase 10 (L-SCOPE; spec FR-121, FR-122, FR-123; plan.md §2; tasks.md T017, T019 step 5,
// rule 11).
//
// WHY: outside apps/krate-manager/ Phase 10 may change only the rule 11 paths (tasks.md:149-156):
// specs/krate-manager-phase10/**, the regenerated specs/INDEX.md, tools/feed-contract/tests/manager-ci-shape.test.js
// (EXPECTED_REQUIRED by T001, the shape checks by T018, so the whole file), .github/workflows/manager-ci.yml (T018, the
// whole file), and in specs/Krate-manager-roadmap.md only FR-123 (a), (d), (e), (f), (g) (T002) and the Phase 10 status
// line (T019, TA-15). One more path is named: .claude/workflows/krate-manager-phase.js, the main loop's SI-22 edit
// (tasks.md:232-235: the WSL step gains -p krate-manager-helper), which is no task's file. Nothing under plugins/, dsp/,
// cmake/, extern/, no other tools/feed-contract/ file, no other workflow (release.yml, feed-tools.yml, ci.yml, feed.yml).
// Inside apps/krate-manager/ rule 11 is an allow-list: plan §2's files (plan.md:176-204), T001's call-site files
// (tasks.md:318-329: status_tests.rs, test_support_tests.rs) plus crates/core/src/detect_tests.rs (the `remove_hint: None`
// call sites T001's PluginView field forces, tasks.md:42-43), and the TA additions (seams_tests.rs TA-13,
// install_loopback_tests.rs TA-13/TA-14). Rule 11's unchanged modules (extract.rs, download.rs, verify.rs, feed.rs,
// detect.rs, context.rs) and the other fixed app files are refused with their reason named; any other app path is
// refused as "not a plan §2 file" (an M-1 fixture under tests/fixtures/helper/inno-locked/ included: only .gitkeep exists
// before the resume, SC-032). The name-only diff cannot see a line-level breach, so the roadmap's -U0 diff is checked
// against pre-image windows whose anchor lines are read from the base's own blob (`git show <base>:<path>`), so a stale
// line number reads as a breach instead of a vacuous pass. A never-committed file is invisible to `git diff
// --name-only`, so the untracked listing is added. The scope base is 4a8b3e2a (tasks.md:11-12). It spawns git, so it is
// evidence, not a test; it lives under specs/, outside the hygiene scan. Adapted from
// specs/krate-manager-phase9/evidence/scope-check.js (the allow-list became Phase 10's plan §2 files, the shape test and
// manager-ci.yml are whole-file allowed, the roadmap windows moved to FR-123's lines).
//
// Usage (repo root or anywhere inside the repo):
//   node specs/krate-manager-phase10/evidence/scope-check.js              # exit 0 clean, 1 breaches, 2 git error
//   node specs/krate-manager-phase10/evidence/scope-check.js --self-test  # the checkers against synthetic inputs
// It writes nothing; stdout only.
'use strict';
const childProcess = require('child_process');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const BASE = '4a8b3e2a';
const APP_PREFIX = 'apps/krate-manager/';
const ROADMAP = 'specs/Krate-manager-roadmap.md';
const SHAPE_TEST = 'tools/feed-contract/tests/manager-ci-shape.test.js';
const MANAGER_CI = '.github/workflows/manager-ci.yml';
const PHASE_WORKFLOW = '.claude/workflows/krate-manager-phase.js';

// tasks.md rule 11 (spec FR-121), outside apps/krate-manager/.
const EXACT = new Set([
  SHAPE_TEST, // T001 EXPECTED_REQUIRED + T018 shape checks: the whole file
  MANAGER_CI, // T018
  'specs/INDEX.md', // regenerated only (T019)
  ROADMAP, // line-level windows below
  PHASE_WORKFLOW, // the main loop's SI-22 edit (tasks.md:232-235)
]);
const PREFIXES = ['specs/krate-manager-phase10/'];
// The 13 status goldens of 4a8b3e2a (`git ls-tree 4a8b3e2a apps/krate-manager/tests/fixtures/status/`); a new one is a
// breach.
const GOLDENS = ['cached-stale', 'fresh-all-states', 'future-schema', 'installed-elsewhere', 'macos-au-copies',
  'macos-intel-not-for-this-mac', 'needs-newer-os-linux', 'needs-newer-os-macos', 'needs-newer-os-windows', 'no-feed',
  'not-for-this-computer-linux', 'notes-rendering', 'record-notices'];
// plan §2 (plan.md:176-204) + T001's call-site files + TA-13 / TA-14, paths relative to apps/krate-manager/.
const APP_ALLOWED = new Set([
  'Cargo.toml', 'Cargo.lock', '.gitignore', 'package.json', 'tsconfig.node.json',
  'eslint.config.mjs', // tasks.md D-10: the project service cannot reach scripts/*.js without a scripts/ block
  'crates/helper/Cargo.toml',
  ...['lib', 'cli', 'error', 'job', 'result', 'roots', 'validate', 'digest', 'ops', 'classify', 'seams', 'os', 'main']
    .map((stem) => `crates/helper/src/${stem}.rs`),
  ...['cli', 'job', 'result', 'roots', 'validate', 'ops', 'classify', 'os', 'seams']
    .map((stem) => `crates/helper/src/${stem}_tests.rs`),
  ...['usage', 'core_jobs', 'stub'].map((stem) => `crates/helper/tests/${stem}.rs`), // stub.rs is deleted (T004)
  'crates/core/Cargo.toml',
  ...['install', 'install_tests', 'install_loopback_tests', 'status', 'status_tests', 'roots', 'test_support',
    'test_support_tests', 'detect_tests'].map((stem) => `crates/core/src/${stem}.rs`),
  'crates/core/src/platform/mod.rs',
  ...['windows_quoting', 'injection', 'config_guard', 'roster'].map((stem) => `crates/core/tests/${stem}.rs`),
  'crates/core/tests/bin/argv_echo.rs',
  'src-tauri/Cargo.toml',
  ...['lib', 'commands', 'os_elevation'].map((stem) => `src-tauri/src/${stem}.rs`),
  'src-tauri/examples/manual_elevation.rs',
  'src-tauri/tauri.helper.conf.json',
  ...['bundle', 'bundle.test', 'write-test-job', 'write-test-job.test', 'inno-fixture.test']
    .map((stem) => `scripts/${stem}.js`),
  ...['status', 'view', 'status.test'].map((stem) => `src/${stem}.ts`),
  ...GOLDENS.map((stem) => `tests/fixtures/status/${stem}.json`),
  'tests/fixtures/inno/fixture.iss',
  ...['Cargo.toml', 'Cargo.lock', 'src/lib.rs'].map((p) => `tests/fixtures/locked-dll/${p}`),
  ...['Cargo.toml', 'Cargo.lock', 'src/main.rs'].map((p) => `tests/fixtures/argv-recorder/${p}`),
  'tests/fixtures/helper/.gitkeep',
  'tests/fixtures/clippy-guard/Cargo.lock',
  'tests/required-tests.json',
]);
// Named so an app-side breach says why; any other app path outside APP_ALLOWED is a breach too ("not a plan §2 file").
const APP_NAMED = [
  [/^crates\/core\/src\/(?:extract|download|verify|feed|detect|context)\.rs$/, 'a module rule 11 / FR-122 keeps unchanged'],
  [/^tests\/fixtures\/helper\/inno-locked\//, 'an M-1 fixture, written only at the resume R1 (SC-032)'],
  [/(?:^|\/)Cargo\.(?:toml|lock)$/, 'a Cargo manifest or lock plan §2 does not list (rule 8)'],
  [/^package-lock\.json$/, 'package-lock.json (rule 8)'],
  [/^src-tauri\/capabilities\//, 'the capability file (spec.md:663: unchanged)'],
  [/(?:^|\/)tauri[^/]*\.conf\.json$/, 'a tauri config other than tauri.helper.conf.json'],
  [/^tsconfig[^/]*\.json$/, 'a tsconfig other than tsconfig.node.json'],
  [/^index\.html$/, 'index.html'],
  [/^clippy\.toml$/, 'clippy.toml'],
  [/^vite\.config\.ts$/, 'vite.config.ts'],
  [/^CHANGELOG\.md$/, 'CHANGELOG.md'],
  [/^version\.json$/, 'version.json'],
  [/\.css$/, 'a CSS file'],
];
// Named so the breach says why; any other path outside the allow-list is a breach too. The shape test and manager-ci.yml
// are in EXACT, so they are accepted before these are read.
const FORBIDDEN_PREFIXES = ['plugins/', 'dsp/', 'cmake/', 'extern/', 'tools/feed-contract/'];
const WORKFLOWS = '.github/workflows/';

// Pre-image (base) windows of the roadmap. An insertion-only hunk (`@@ -a,0 +c[,d] @@`) adds d lines after old line a;
// `lines` is the exact number of added lines the window admits (undefined = any). A change hunk's old range must lie in a
// change window. tasks.md T002 step 2 and TA-15 give the lines.
const ROADMAP_WINDOWS = {
  insert: [
    { from: 477, to: 479, what: 'FR-123 (a): the Phase 4 Roots bullet, lines 477-479' },
    { from: 709, to: 712, lines: 2, what: 'FR-123 (c) + (h): the Phase 10 status line and its blank line (TA-15)' },
    { from: 713, to: 766, what: 'FR-123 (e) / (f): inside Phase 10 Components' },
    { from: 827, to: 838, what: 'FR-123 (d): the Phase 11 universal macOS bullet' },
    { from: 864, to: 866, what: 'FR-123 (d): the Phase 11 lipo outcome' },
    { from: 906, to: 912, what: 'FR-123 (g): the Phase 12 first-launch documentation bullet' },
  ],
  change: [
    { from: 477, to: 479, what: 'FR-123 (a): the Phase 4 Roots bullet, lines 477-479' },
    { from: 732, to: 737, what: 'FR-123 (e): the uninstall_pkg bullet, line 735' },
    { from: 827, to: 838, what: 'FR-123 (d): the Phase 11 universal macOS bullet' },
    { from: 864, to: 866, what: 'FR-123 (d): the Phase 11 lipo outcome' },
    { from: 906, to: 912, what: 'FR-123 (g): the Phase 12 first-launch documentation bullet' },
  ],
};
// The base lines the windows lean on (1-based line -> exact text, or { prefix } for a line too long to spell).
const ANCHORS = {
  [ROADMAP]: {
    477: { prefix: '  OS, tests build it from temp dirs.' },
    479: '  the same setup (Phase 4 spec D17).',
    480: { prefix: '  The guard is mechanical, not a convention:' },
    709: '### Phase 10: The Elevated Helper and the Windows / macOS Install Drivers',
    711: '**Spec:** `krate-manager-phase10`',
    713: 'Components:',
    732: { prefix: '  - `uninstall_pkg { pkg_id }` (macOS):' },
    735: { prefix: '    location, canonicalized) against `AllowedRoots`' },
    737: '    --forget <pkg_id>`.',
    738: '  Linux has no elevated operation. There is no generic copy / rename / remove op.',
    766: '  retry" outcome follows the measurement, not an assumption.',
    768: 'Measurable outcomes:',
    827: { prefix: '- **One universal macOS manager build, Intel Macs included' },
    838: '  should build universal is a plugin-build question outside this roadmap.',
    839: { prefix: "- 🆕 **The manager's first-install packaging" },
    864: { prefix: '- PENDING-CI, `release-manager.yml` macOS leg: `lipo -archs`' },
    866: '  per-tag URL (no `/releases/latest/`).',
    867: { prefix: '- PENDING-CI, first-install artifacts' },
    906: { prefix: '- 🆕 First-launch documentation as a deliverable' },
    912: '  which no `tauri.conf.json` key can carry (Phase 4 spec Non-goals).',
    913: { prefix: '- 🆕 Release readiness for manager 0.1.0' },
  },
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

/** Checker 1: every changed path is a rule 11 path (one breach per path). */
function pathBreaches(paths) {
  const breaches = [];
  for (const p of paths) {
    if (p.startsWith(APP_PREFIX)) {
      const relApp = p.slice(APP_PREFIX.length);
      if (APP_ALLOWED.has(relApp)) continue;
      const named = APP_NAMED.find(([re]) => re.test(relApp));
      const why = named ? `is ${named[1]}` : 'is not a plan §2 file';
      breaches.push(`scope-check: ${p} ${why} inside ${APP_PREFIX} (tasks.md rule 11)`);
      continue;
    }
    if (EXACT.has(p) || PREFIXES.some((pre) => p.startsWith(pre))) continue;
    const forbidden = FORBIDDEN_PREFIXES.find((pre) => p.startsWith(pre));
    let why = 'is not a rule 11 path';
    if (forbidden) why = `is under ${forbidden}, which FR-121 / FR-122 forbid`;
    else if (p.startsWith(WORKFLOWS)) why = `is a workflow other than ${MANAGER_CI}, which rule 11 keeps unchanged`;
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
      breaches.push(`scope-check: ${file} hunk ${h.header} (${where}) is outside the FR-123 windows (tasks.md rule 11)`);
    }
  }
  return breaches;
}

const roadmapBreaches = (hunks) => windowBreaches(ROADMAP, hunks, ROADMAP_WINDOWS);

/**
 * Checker 3: the base blob of `file` holds each anchor line where the windows expect it (one breach per anchor). An
 * anchor is the exact line text, or `{ prefix }` for a base line too long to spell (the line must start with it).
 */
function anchorBreaches(file, baseText, anchors) {
  const all = baseText.split(/\r?\n/);
  const holds = (got, want) => (typeof want === 'string' ? got === want : typeof got === 'string' && got.startsWith(want.prefix));
  return Object.entries(anchors).filter(([n, want]) => !holds(all[Number(n) - 1], want))
    .map(([n, want]) => `scope-check: ${file} line ${n} at ${BASE} is ${JSON.stringify(all[Number(n) - 1])}, the windows` +
      ` expect ${typeof want === 'string' ? JSON.stringify(want) : `a line starting ${JSON.stringify(want.prefix)}`}` +
      ' (update the window constants)');
}

function selfTest() {
  // Rule 11's app files, spelled here (not read from APP_ALLOWED) so a wrong table reads red.
  const appFiles = [
    'Cargo.toml', 'Cargo.lock', '.gitignore', 'package.json', 'tsconfig.node.json', 'eslint.config.mjs',
    'crates/helper/Cargo.toml', 'crates/helper/src/lib.rs', 'crates/helper/src/cli.rs', 'crates/helper/src/error.rs', 'crates/helper/src/job.rs',
    'crates/helper/src/result.rs', 'crates/helper/src/roots.rs', 'crates/helper/src/validate.rs',
    'crates/helper/src/digest.rs', 'crates/helper/src/ops.rs', 'crates/helper/src/classify.rs',
    'crates/helper/src/seams.rs', 'crates/helper/src/os.rs', 'crates/helper/src/main.rs',
    'crates/helper/src/cli_tests.rs', 'crates/helper/src/job_tests.rs', 'crates/helper/src/result_tests.rs',
    'crates/helper/src/roots_tests.rs', 'crates/helper/src/validate_tests.rs', 'crates/helper/src/ops_tests.rs',
    'crates/helper/src/classify_tests.rs', 'crates/helper/src/os_tests.rs', 'crates/helper/src/seams_tests.rs',
    'crates/helper/tests/usage.rs', 'crates/helper/tests/core_jobs.rs', 'crates/helper/tests/stub.rs',
    'crates/core/Cargo.toml', 'crates/core/src/install.rs', 'crates/core/src/install_tests.rs',
    'crates/core/src/install_loopback_tests.rs', 'crates/core/src/status.rs', 'crates/core/src/status_tests.rs',
    'crates/core/src/roots.rs', 'crates/core/src/test_support.rs', 'crates/core/src/test_support_tests.rs',
    'crates/core/src/detect_tests.rs', 'crates/core/src/platform/mod.rs', 'crates/core/tests/windows_quoting.rs',
    'crates/core/tests/injection.rs', 'crates/core/tests/config_guard.rs', 'crates/core/tests/roster.rs',
    'crates/core/tests/bin/argv_echo.rs', 'src-tauri/Cargo.toml', 'src-tauri/src/lib.rs', 'src-tauri/src/commands.rs',
    'src-tauri/src/os_elevation.rs', 'src-tauri/examples/manual_elevation.rs', 'src-tauri/tauri.helper.conf.json',
    'scripts/bundle.js', 'scripts/bundle.test.js', 'scripts/write-test-job.js', 'scripts/write-test-job.test.js',
    'scripts/inno-fixture.test.js', 'src/status.ts', 'src/view.ts', 'src/status.test.ts',
    'tests/fixtures/status/cached-stale.json', 'tests/fixtures/status/fresh-all-states.json',
    'tests/fixtures/status/future-schema.json', 'tests/fixtures/status/installed-elsewhere.json',
    'tests/fixtures/status/macos-au-copies.json', 'tests/fixtures/status/macos-intel-not-for-this-mac.json',
    'tests/fixtures/status/needs-newer-os-linux.json', 'tests/fixtures/status/needs-newer-os-macos.json',
    'tests/fixtures/status/needs-newer-os-windows.json', 'tests/fixtures/status/no-feed.json',
    'tests/fixtures/status/not-for-this-computer-linux.json', 'tests/fixtures/status/notes-rendering.json',
    'tests/fixtures/status/record-notices.json', 'tests/fixtures/inno/fixture.iss',
    'tests/fixtures/locked-dll/Cargo.toml', 'tests/fixtures/locked-dll/Cargo.lock', 'tests/fixtures/locked-dll/src/lib.rs',
    'tests/fixtures/argv-recorder/Cargo.toml', 'tests/fixtures/argv-recorder/Cargo.lock',
    'tests/fixtures/argv-recorder/src/main.rs', 'tests/fixtures/helper/.gitkeep',
    'tests/fixtures/clippy-guard/Cargo.lock', 'tests/required-tests.json',
  ].map((p) => `${APP_PREFIX}${p}`);
  const allowedPaths = [
    SHAPE_TEST,
    '.github/workflows/manager-ci.yml',
    '.claude/workflows/krate-manager-phase.js',
    'specs/krate-manager-phase10/evidence/scope-check.js',
    'specs/krate-manager-phase10/evidence/timings.md',
    'specs/krate-manager-phase10/spec.md',
    'specs/INDEX.md',
    ROADMAP,
    ...appFiles,
  ].join('\n');
  // T002's FR-123 edits as git -U0 may print them: (a) a reflow of 477-479 gaining a line, (d) the universal bullet's
  // last line changed plus two added lines and the outcome's first line changed, (e) line 735 changed, (f) one bullet
  // inserted after old line 738, (g) the bullet's last line changed and a line inserted after 912; then T019's status
  // line after old line 711 (TA-15, CRLF diff).
  const amendHunks = ['@@ -477,3 +477,4 @@ x', '@@ -735 +736,2 @@ x', '@@ -738,0 +741,3 @@ x', '@@ -838 +843,3 @@ x',
    '@@ -864 +871,2 @@ x', '@@ -912 +921 @@ x', '@@ -912,0 +922,2 @@ x', '@@ -711,0 +712,2 @@ Part B\r'].join('\n');
  const tracked = (p) => () => pathBreaches(collectPaths(allowedPaths, `${p}\n`, ''));
  const untracked = (p) => () => pathBreaches(collectPaths(allowedPaths, '', `${p}\r\n`));
  const roadmap = (h) => () => roadmapBreaches(parseHunks(`${amendHunks}\n${h}`));
  const roadmapBase = Array.from({ length: 1064 }, (_, i) => `line ${i + 1}`);
  const put = (n, text) => { roadmapBase[n - 1] = text; };
  put(477, '  OS, tests build it from temp dirs. The feed URL is injected the same way, and more');
  put(479, '  the same setup (Phase 4 spec D17).');
  put(480, '  The guard is mechanical, not a convention: (1) and more');
  put(709, '### Phase 10: The Elevated Helper and the Windows / macOS Install Drivers');
  put(711, '**Spec:** `krate-manager-phase10`');
  put(713, 'Components:');
  put(732, '  - `uninstall_pkg { pkg_id }` (macOS): `pkg_id` must match, and more');
  put(735, '    location, canonicalized) against `AllowedRoots` before removing anything, and more');
  put(737, '    --forget <pkg_id>`.');
  put(738, '  Linux has no elevated operation. There is no generic copy / rename / remove op.');
  put(766, '  retry" outcome follows the measurement, not an assumption.');
  put(768, 'Measurable outcomes:');
  put(827, '- **One universal macOS manager build, Intel Macs included (Q6 ruled, and more');
  put(838, '  should build universal is a plugin-build question outside this roadmap.');
  put(839, "- 🆕 **The manager's first-install packaging (Q5 ruled), and more");
  put(864, '- PENDING-CI, `release-manager.yml` macOS leg: `lipo -archs` on the built binary, and more');
  put(866, '  per-tag URL (no `/releases/latest/`).');
  put(867, '- PENDING-CI, first-install artifacts on fresh hosted runners, and more');
  put(906, '- 🆕 First-launch documentation as a deliverable: Windows SmartScreen, and more');
  put(912, '  which no `tauri.conf.json` key can carry (Phase 4 spec Non-goals).');
  put(913, '- 🆕 Release readiness for manager 0.1.0 (from version.json), and more');
  const anchors = (lines) => () => anchorBreaches(ROADMAP, lines.join('\r\n'), ANCHORS[ROADMAP]);
  const refusedApp = [
    ...['context', 'extract', 'download', 'verify', 'feed', 'detect', 'lib', 'contract', 'verify_tests']
      .map((s) => `crates/core/src/${s}.rs`),
    'crates/core/tests/other.rs', 'crates/helper/src/extra.rs', 'src-tauri/build.rs', 'src-tauri/capabilities/main.json',
    'src-tauri/tauri.conf.json', 'src-tauri/tauri.macos.conf.json', 'src-tauri/src/dev_feed.rs', 'index.html',
    'package-lock.json', 'tsconfig.json', 'vite.config.ts', 'clippy.toml', 'CHANGELOG.md',
    'version.json', 'src/styles.css', 'src/main.ts', 'scripts/other.js', 'tests/fixtures/clippy-guard/Cargo.toml',
    'tests/fixtures/status/new.json', 'tests/fixtures/helper/inno-locked/exit_code.txt',
    'tests/fixtures/locked-dll/target/debug/x.dll', 'src-tauri/binaries/krate-manager-helper-x86_64-pc-windows-msvc.exe',
  ];
  const cases = [
    { name: 'allowed paths (tracked)', run: () => pathBreaches(collectPaths(allowedPaths, '')), want: 0 },
    { name: 'allowed paths (untracked, CRLF listing)',
      run: () => pathBreaches(collectPaths('', allowedPaths.replace(/\n/g, '\r\n'))), want: 0 },
    { name: 'allowed paths count every listed line', run: () => collectPaths(allowedPaths).map(() => 'x'),
      want: appFiles.length + 8 },
    { name: 'a quoted path is unquoted', run: () => collectPaths('"specs/krate-manager-phase10/a b.md"\n')
      .filter((p) => p !== 'specs/krate-manager-phase10/a b.md'), want: 0 },
    { name: 'FR-123 (a), (d), (e), (f), (g) and the status line inside their windows',
      run: () => roadmapBreaches(parseHunks(amendHunks)), want: 0 },
    { name: 'the status line directly under the heading (after 709 / 710)',
      run: () => roadmapBreaches(parseHunks('@@ -709,0 +710,2 @@\n@@ -710,0 +711,2 @@')), want: 0 },
    { name: 'FR-123 (d) change of the whole bullet 827-838', run: () => roadmapBreaches(parseHunks('@@ -827,12 +827,15 @@')),
      want: 0 },
    { name: 'FR-123 (g) change of 906-912', run: () => roadmapBreaches(parseHunks('@@ -906,7 +906,9 @@')), want: 0 },
    { name: 'roadmap base anchors (CRLF)', run: anchors(roadmapBase), want: 0 },
    { name: 'roadmap anchors shifted by one line', run: anchors(['x', ...roadmapBase]), want: Object.keys(ANCHORS[ROADMAP]).length },
    { name: 'roadmap anchors with no text', run: anchors(['x']), want: Object.keys(ANCHORS[ROADMAP]).length },
    ...refusedApp.map((p) => ({ name: `${APP_PREFIX}${p} refused`, run: tracked(`${APP_PREFIX}${p}`), want: 1 })),
    { name: `untracked ${APP_PREFIX}crates/helper/src/second.rs`, run: untracked(`${APP_PREFIX}crates/helper/src/second.rs`),
      want: 1 },
    { name: 'plugins/x', run: tracked('plugins/x'), want: 1 },
    { name: 'plugins/iterum/installers/windows/setup.iss', run: tracked('plugins/iterum/installers/windows/setup.iss'), want: 1 },
    { name: 'dsp/x', run: tracked('dsp/x'), want: 1 },
    { name: 'untracked dsp/x', run: untracked('dsp/x'), want: 1 },
    { name: 'cmake/KratePlugin.cmake', run: tracked('cmake/KratePlugin.cmake'), want: 1 },
    { name: 'extern/x', run: tracked('extern/x'), want: 1 },
    { name: '.github/workflows/release.yml', run: tracked('.github/workflows/release.yml'), want: 1 },
    { name: '.github/workflows/feed-tools.yml', run: tracked('.github/workflows/feed-tools.yml'), want: 1 },
    { name: '.github/workflows/ci.yml', run: tracked('.github/workflows/ci.yml'), want: 1 },
    { name: '.github/workflows/feed.yml', run: tracked('.github/workflows/feed.yml'), want: 1 },
    { name: 'tools/feed-contract/schemas/x', run: tracked('tools/feed-contract/schemas/x'), want: 1 },
    { name: 'tools/feed-contract/tests/other.test.js', run: tracked('tools/feed-contract/tests/other.test.js'), want: 1 },
    { name: 'tools/feed-contract/listing.js', run: tracked('tools/feed-contract/listing.js'), want: 1 },
    { name: 'tools/gen-specs-index.js (not a rule 11 path)', run: tracked('tools/gen-specs-index.js'), want: 1 },
    { name: '.claude/workflows/other-phase.js', run: tracked('.claude/workflows/other-phase.js'), want: 1 },
    { name: 'specs/krate-manager-phase9/spec.md', run: tracked('specs/krate-manager-phase9/spec.md'), want: 1 },
    { name: 'specs/krate-manager-phase10x/spec.md', run: tracked('specs/krate-manager-phase10x/spec.md'), want: 1 },
    { name: 'roadmap hunk at old line 300', run: roadmap('@@ -300 +302 @@'), want: 1 },
    { name: 'roadmap change at old line 476 (before (a))', run: roadmap('@@ -476 +476 @@'), want: 1 },
    { name: 'roadmap change spanning 479-480', run: roadmap('@@ -479,2 +479,2 @@'), want: 1 },
    { name: 'roadmap one-line status insertion after 711', run: roadmap('@@ -711,0 +712 @@'), want: 1 },
    { name: 'roadmap status insertion after 708', run: roadmap('@@ -708,0 +709,2 @@'), want: 1 },
    { name: 'roadmap change of the Spec line 711', run: roadmap('@@ -711 +711 @@'), want: 1 },
    { name: 'roadmap change of the heading 709', run: roadmap('@@ -709 +709 @@'), want: 1 },
    { name: 'roadmap change at old line 731 (before (e))', run: roadmap('@@ -731 +731 @@'), want: 1 },
    { name: 'roadmap change spanning 737-738', run: roadmap('@@ -737,2 +737,2 @@'), want: 1 },
    { name: 'roadmap insertion after 767 (outside Components)', run: roadmap('@@ -767,0 +768 @@'), want: 1 },
    { name: 'roadmap change of a Measurable outcomes line 770', run: roadmap('@@ -770 +770 @@'), want: 1 },
    { name: 'roadmap change at old line 826 (before (d))', run: roadmap('@@ -826 +826 @@'), want: 1 },
    { name: 'roadmap change spanning 838-839', run: roadmap('@@ -838,2 +838,3 @@'), want: 1 },
    { name: 'roadmap insertion after 839', run: roadmap('@@ -839,0 +840 @@'), want: 1 },
    { name: 'roadmap change at old line 863', run: roadmap('@@ -863 +863 @@'), want: 1 },
    { name: 'roadmap change spanning 866-867', run: roadmap('@@ -866,2 +866,2 @@'), want: 1 },
    { name: 'roadmap change at old line 905 (before (g))', run: roadmap('@@ -905 +905 @@'), want: 1 },
    { name: 'roadmap insertion after 913', run: roadmap('@@ -913,0 +914 @@'), want: 1 },
    { name: 'roadmap change of the Phase 9 status line (old 665)', run: roadmap('@@ -665 +665 @@'), want: 1 },
    { name: 'base is 4a8b3e2a', run: () => (BASE === '4a8b3e2a' ? [] : [`BASE ${BASE}`]), want: 0 },
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
  const roadmapBase = git(['show', `${BASE}:${ROADMAP}`]);
  if ([nameOnly, untracked, roadmapDiff, roadmapBase].includes(null)) return 2;
  const paths = collectPaths(nameOnly, untracked);
  const roadmapHunks = parseHunks(roadmapDiff);
  const breaches = [
    ...anchorBreaches(ROADMAP, roadmapBase, ANCHORS[ROADMAP]),
    ...pathBreaches(paths),
    ...roadmapBreaches(roadmapHunks),
  ];
  if (breaches.length) {
    for (const b of breaches) console.log(b);
    console.log(`scope-check: ${breaches.length} breach(es) (base ${BASE})`);
    return 1;
  }
  const app = paths.filter((p) => p.startsWith(APP_PREFIX)).length;
  console.log(`scope-check: ${paths.length} paths (${app} under ${APP_PREFIX}, ${paths.length - app} outside),` +
    ` ${roadmapHunks.length} roadmap hunks: OK (base ${BASE})`);
  return 0;
}

process.exit(main(process.argv.slice(2)));
