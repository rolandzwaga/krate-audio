#!/usr/bin/env node
// Scope check for Krate Manager Phase 12 (L-SCOPE; spec FR-081, FR-111, SC-023; tasks.md T015, lane L-SCOPE). A copy of
// specs/krate-manager-phase11/evidence/scope-check.js with Phase 12's set (tasks.md T015 first bullet).
//
// WHY: Phase 12 may change only paths under apps/krate-manager/, specs/krate-manager-phase12/ and tools/feed-contract/,
// plus a fixed list of single files (tasks.md T015). This script lists `git diff --name-only 7c4b6837` plus every
// untracked file (`git status --porcelain --untracked-files=all`: a never-committed file is invisible to the diff) and
// checks each against that allow list.
//
// WHY 7c4b6837: it is the commit the task list was written against and the phase's starting HEAD (tasks.md:8-10).
//
// The fence (checked before the allow list, so a fenced path under an allowed prefix is still refused):
//   - plugins/, dsp/, cmake/, extern/ (spec FR-111: no C++ lane, tasks.md:15-18);
//   - docs/index-template.html (spec FR-081: the template does not change, TA-12);
//   - every Cargo.toml / Cargo.lock under apps/krate-manager/ and its package.json / package-lock.json (spec SC-023; no
//     dependency is added, so no task edits a manifest or a lockfile and tests/fixtures/clippy-guard/Cargo.lock is never
//     re-locked, tasks.md:36-39). T015 names the four top-level ones; the nested Cargo files are fenced by the same rule.
// No path anywhere may be a file named *.key (tasks.md Rule 1: no key file is written).
//
// WHY the .gitattributes hunk rule: T001 step 14 adds exactly two lines and "No other line changes" (tasks.md:343-345), so
// the name-only listing is not enough. `git diff 7c4b6837 -- .gitattributes` must add exactly those two lines, in order,
// and remove none (Phase 11's rule, specs/krate-manager-phase11/evidence/scope-check.js:111-130).
//
// Usage (repo root or anywhere inside the repo):
//   node specs/krate-manager-phase12/evidence/scope-check.js              # exit 0 clean, 1 breaches, 2 git error
//   node specs/krate-manager-phase12/evidence/scope-check.js --self-test  # the checkers over fixed inputs, no git
// It writes nothing; stdout only. close-lane.js requires it for collectPaths.
'use strict';
const childProcess = require('child_process');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const BASE = '7c4b6837';
const APP_PREFIX = 'apps/krate-manager/';
const GITATTRIBUTES = '.gitattributes';
// T001 step 14 (tasks.md:343-345): the comment and the path line added after .gitattributes:25.
const GITATTRIBUTES_LINES = [
  '# A shebang tool whose test reads it as LF, like gen-manager-latest.js above (Krate Manager Phase 12, PD-16).',
  'tools/gen-root-index.js         text eol=lf',
];

// tasks.md T015: the single files allowed outside the three prefixes.
const EXACT = new Set([
  'tools/gen-root-index.js', // T011 (new)
  'tools/manager-readiness-counts.js', // T012 (new)
  'tools/manager-release-preflight.js', // T014
  'docs/manager-card-template.html', // T010 (new)
  ...['release', 'docs', 'release-manager', 'feed-tools'].map((s) => `.github/workflows/${s}.yml`), // T016
  GITATTRIBUTES, // T001 step 14; the hunk rule below
  'specs/Krate-manager-roadmap.md', // T002
  'specs/INDEX.md', // T017, regenerated only
]);
const PREFIXES = [APP_PREFIX, 'specs/krate-manager-phase12/', 'tools/feed-contract/'];
const FENCE = [
  [/^(?:plugins|dsp|cmake|extern)\//, 'under plugins/, dsp/, cmake/ or extern/ (FR-111)'],
  [/^docs\/index-template\.html$/, 'docs/index-template.html, which stays unchanged (FR-081)'],
  [/^apps\/krate-manager\/(?:.*\/)?Cargo\.(?:toml|lock)$/, 'a Cargo manifest or lockfile, which stays unchanged (SC-023)'],
  [/^apps\/krate-manager\/package(?:-lock)?\.json$/, 'package.json / package-lock.json, which stay unchanged (SC-023)'],
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

/**
 * The changed paths: one path per non-empty line of `git diff --name-only`, plus the `?? <path>` entries of
 * `git status --porcelain --untracked-files=all`, deduplicated, in first-seen order.
 */
function collectPaths(nameOnly, porcelain) {
  const paths = [];
  const add = (p) => { if (p && !paths.includes(p)) paths.push(p); };
  for (const line of nameOnly.split(/\r?\n/)) add(unquote(line.trim()));
  for (const line of porcelain.split(/\r?\n/)) {
    if (line.startsWith('?? ')) add(unquote(line.slice(3).trim()));
  }
  return paths;
}

/** Checker 1: one breach per path outside the allow list (the fence names its reason) and per path named *.key. */
function pathBreaches(paths) {
  const breaches = [];
  for (const p of paths) {
    if (/\.key$/i.test(path.posix.basename(p))) {
      breaches.push(`scope-check: ${p} is a file named *.key (no key file is written, tasks.md Rule 1)`);
      continue;
    }
    const fenced = FENCE.find(([re]) => re.test(p));
    if (fenced) {
      breaches.push(`scope-check: ${p} is ${fenced[1]} (tasks.md T015)`);
      continue;
    }
    if (EXACT.has(p) || PREFIXES.some((pre) => p.startsWith(pre))) continue;
    breaches.push(`scope-check: ${p} is not a Phase 12 path (tasks.md T015)`);
  }
  return breaches;
}

/** Checker 2: `git diff <base> -- .gitattributes` adds exactly GITATTRIBUTES_LINES (in order) and removes no line. */
function gitattributesBreaches(diffText) {
  const added = [];
  const removed = [];
  let inHunk = false;
  for (const raw of diffText.split('\n')) {
    const l = raw.endsWith('\r') ? raw.slice(0, -1) : raw;
    if (l.startsWith('diff --git ')) { inHunk = false; continue; }
    if (l.startsWith('@@')) { inHunk = true; continue; }
    if (!inHunk) continue;
    if (l.startsWith('+')) added.push(l.slice(1));
    else if (l.startsWith('-')) removed.push(l.slice(1));
  }
  const breaches = removed.map((l) => `scope-check: ${GITATTRIBUTES} removes ${JSON.stringify(l)} (T001 step 14: none removed)`);
  if (JSON.stringify(added) !== JSON.stringify(GITATTRIBUTES_LINES)) {
    breaches.push(`scope-check: ${GITATTRIBUTES} adds ${JSON.stringify(added)}, T001 step 14 allows exactly` +
      ` ${JSON.stringify(GITATTRIBUTES_LINES)} (tasks.md:343-345)`);
  }
  return breaches;
}

function selfTest() {
  // The allowed paths, spelled here (not read from EXACT) so a wrong table reads red.
  const inPaths = [
    'apps/krate-manager/crates/core/src/install.rs', 'apps/krate-manager/crates/core/src/install_batch_tests.rs',
    'apps/krate-manager/crates/helper/src/job_tests.rs', 'apps/krate-manager/src-tauri/src/commands.rs',
    'apps/krate-manager/src/settings.ts', 'apps/krate-manager/README.md', 'apps/krate-manager/RELEASING.md',
    'apps/krate-manager/CHANGELOG.md', 'apps/krate-manager/tests/required-tests.json',
    'apps/krate-manager/tests/fixtures/status/no-feed.json', 'apps/krate-manager/src-tauri/capabilities/main.json',
    'specs/krate-manager-phase12/spec.md', 'specs/krate-manager-phase12/evidence/close-lane.log',
    'specs/krate-manager-phase12/user-checks.md', 'tools/feed-contract/first-launch-statements.json',
    'tools/feed-contract/tests/gen-root-index.test.js', 'tools/feed-contract/fixtures/landing/root-index.expected.html',
    'tools/feed-contract/fixtures/landing/plugins/delta-x/version.json',
    'tools/gen-root-index.js', 'tools/manager-readiness-counts.js', 'tools/manager-release-preflight.js',
    'docs/manager-card-template.html', '.github/workflows/release.yml', '.github/workflows/docs.yml',
    '.github/workflows/release-manager.yml', '.github/workflows/feed-tools.yml', '.gitattributes',
    'specs/Krate-manager-roadmap.md', 'specs/INDEX.md',
  ];
  const outPaths = [
    'plugins/iterum/version.json', 'dsp/include/krate/dsp/core/db_utils.h', 'cmake/KratePlugin.cmake', 'extern/x',
    'docs/index-template.html', 'docs/index.html', 'docs/other-template.html',
    'apps/krate-manager/Cargo.toml', 'apps/krate-manager/Cargo.lock', 'apps/krate-manager/package.json',
    'apps/krate-manager/package-lock.json', 'apps/krate-manager/crates/core/Cargo.toml',
    'apps/krate-manager/src-tauri/Cargo.toml', 'apps/krate-manager/tests/fixtures/clippy-guard/Cargo.lock',
    '.github/workflows/manager-ci.yml', '.github/workflows/ci.yml', '.github/workflows/backfill-sign.yml',
    'tools/gen-feed.js', 'tools/gen-manager-latest.js', 'tools/feed-publish.js', 'tools/lint-serial-test-runs.js',
    'specs/krate-manager-phase11/compliance.md', 'specs/krate-manager-phase12x/spec.md', '.claude/workflows/x.js',
    'CLAUDE.md', 'apps/krate-manager/tests/k.key', 'specs/krate-manager-phase12/evidence/minisign.key',
    'tools/feed-contract/fixtures/x/secret.KEY',
  ];
  const added = GITATTRIBUTES_LINES.map((l) => `+${l}`);
  const hunk = (plus, minus = []) => ['diff --git a/.gitattributes b/.gitattributes', 'index 6ec762eb..94aa69c4 100644',
    '--- a/.gitattributes', '+++ b/.gitattributes', '@@ -23,6 +23,8 @@ tools/macos-fastmath-repro.sh       text eol=lf',
    ' tools/gen-manager-latest.js         text eol=lf', ...minus, ...plus, ''].join('\n');
  const goodDiff = hunk(added);
  const cases = [
    { name: 'every Phase 12 path, tracked', run: () => pathBreaches(collectPaths(inPaths.join('\n'), '')), want: 0 },
    { name: 'every Phase 12 path, untracked (CRLF porcelain)',
      run: () => pathBreaches(collectPaths('', inPaths.map((p) => `?? ${p}`).join('\r\n'))), want: 0 },
    { name: 'tracked + untracked are deduplicated',
      run: () => collectPaths(inPaths.join('\n'), inPaths.map((p) => `?? ${p}`).join('\n')).map(() => 'x'),
      want: inPaths.length },
    { name: 'porcelain: modified entries are not taken from status (the diff lists them)',
      run: () => collectPaths('', ' M plugins/x\n?? specs/krate-manager-phase12/a.md\n'), want: 1 },
    { name: 'a quoted untracked path is unquoted',
      run: () => collectPaths('', '?? "specs/krate-manager-phase12/a b.md"\n')
        .filter((p) => p !== 'specs/krate-manager-phase12/a b.md'), want: 0 },
    ...outPaths.map((p) => ({ name: `${p} refused`, run: () => pathBreaches([p]), want: 1 })),
    { name: 'every out path refused once', run: () => pathBreaches(outPaths), want: outPaths.length },
    { name: 'the manifest breach names SC-023',
      run: () => pathBreaches(['apps/krate-manager/Cargo.lock']).filter((b) => !b.includes('SC-023')), want: 0 },
    { name: 'the template breach names FR-081',
      run: () => pathBreaches(['docs/index-template.html']).filter((b) => !b.includes('FR-081')), want: 0 },
    { name: 'the plugins/ breach names FR-111',
      run: () => pathBreaches(['plugins/x/y.cpp']).filter((b) => !b.includes('FR-111')), want: 0 },
    { name: 'a *.key breach names the key rule',
      run: () => pathBreaches(['apps/krate-manager/x.key']).filter((b) => !b.includes('*.key')), want: 0 },
    { name: '.gitattributes: the two lines added, none removed', run: () => gitattributesBreaches(goodDiff), want: 0 },
    { name: '.gitattributes: CRLF diff text', run: () => gitattributesBreaches(goodDiff.replace(/\n/g, '\r\n')), want: 0 },
    { name: '.gitattributes: one line missing', run: () => gitattributesBreaches(hunk([added[1]])), want: 1 },
    { name: '.gitattributes: an extra added line', run: () => gitattributesBreaches(hunk([...added, '+*.png binary'])), want: 1 },
    { name: '.gitattributes: a removed line',
      run: () => gitattributesBreaches(hunk(added, ['-tools/gen-manager-latest.js         text eol=lf'])), want: 1 },
    { name: '.gitattributes: the two lines swapped', run: () => gitattributesBreaches(hunk([added[1], added[0]])), want: 1 },
    { name: '.gitattributes: the path line with other spacing',
      run: () => gitattributesBreaches(hunk([added[0], '+tools/gen-root-index.js text eol=lf'])), want: 1 },
    { name: '.gitattributes: an empty diff (step 14 not written)', run: () => gitattributesBreaches(''), want: 1 },
    { name: 'base is 7c4b6837', run: () => (BASE === '7c4b6837' ? [] : [`BASE ${BASE}`]), want: 0 },
  ];
  let failed = 0;
  for (const c of cases) {
    let got;
    try {
      got = c.run();
    } catch (e) {
      failed++;
      console.log(`FAILED ${c.name}: threw: ${e.message}`);
      continue;
    }
    const ok = got.length === c.want;
    if (!ok) failed++;
    console.log(`${ok ? 'ok    ' : 'FAILED'} ${c.name}: ${got.length} breach(es), want ${c.want}`);
    if (!ok) for (const b of got) console.log(`         ${b}`);
  }
  console.log(failed ? `SELF-TEST RED (${failed} of ${cases.length})` : `SELF-TEST GREEN (${cases.length} cases)`);
  return failed ? 1 : 0;
}

function git(args) {
  const r = childProcess.spawnSync('git', ['-c', 'core.quotepath=off', ...args],
    { cwd: ROOT, encoding: 'utf8', windowsHide: true, maxBuffer: 1 << 28 });
  if (r.error || r.status !== 0) {
    console.log(`scope-check: git ${args.join(' ')} failed: ${r.error ? r.error.message : r.stderr.trim()}`);
    return null;
  }
  return r.stdout;
}

function main(argv) {
  if (argv.includes('--self-test')) return selfTest();
  const diffOpts = ['--no-color', '--no-ext-diff'];
  // --no-renames: a rename lists both its old and its new path, so a file moved out of plugins/ is still seen.
  const nameOnly = git(['diff', ...diffOpts, '--no-renames', '--name-only', BASE]);
  const porcelain = git(['status', '--porcelain', '--untracked-files=all']);
  if (nameOnly === null || porcelain === null) return 2;
  const paths = collectPaths(nameOnly, porcelain);
  const breaches = pathBreaches(paths);
  if (paths.includes(GITATTRIBUTES)) {
    const attrDiff = git(['diff', ...diffOpts, BASE, '--', GITATTRIBUTES]);
    if (attrDiff === null) return 2;
    breaches.push(...gitattributesBreaches(attrDiff));
  }
  if (breaches.length) {
    for (const b of breaches) console.log(b);
    console.log(`scope-check: ${breaches.length} breach(es) (base ${BASE})`);
    return 1;
  }
  const app = paths.filter((p) => p.startsWith(APP_PREFIX)).length;
  console.log(`scope-check: ${paths.length} paths (${app} under ${APP_PREFIX}, ${paths.length - app} outside): OK` +
    ` (base ${BASE})`);
  return 0;
}

module.exports = { BASE, GITATTRIBUTES_LINES, collectPaths, pathBreaches, gitattributesBreaches };
if (require.main === module) process.exit(main(process.argv.slice(2)));
