#!/usr/bin/env node
// Scope check for Krate Manager Phase 11 (L-SCOPE; spec FR-092, FR-097; plan.md §2; tasks.md T016, T018 step 3, Rule 11).
//
// WHY: Phase 11 may change, outside apps/krate-manager/, only the Rule 11 paths (tasks.md Rule 11). This script lists
// `git diff --name-only 30878b36` plus every untracked file (`git status --porcelain --untracked-files=all`, a
// never-committed file is invisible to the diff) and checks each against that allow list.
//
// WHY 30878b36: it is the phase's starting HEAD (tasks.md:17-23, D-23). 4f04dc03 is wrong: its diff holds Phase 10's
// committed compliance.md and roadmap status line.
//
// The fence it names (spec FR-092; tasks.md T016): feed.yml, release.yml, ci.yml, docs.yml, backfill-sign.yml,
// tools/gen-feed.js, tools/feed-contract/CONTRACT.md, tools/feed-contract/fixtures/**, apps/krate-manager/crates/helper/**,
// plugins/, dsp/, cmake/, extern/. Any other path outside the allow list is refused as "not a Rule 11 path". Inside
// apps/krate-manager/ every path is allowed except the helper crate. No path anywhere may be a file named *.key (spec
// FR-083; tasks.md Rule 1: no private key is ever written to a file).
//
// WHY the .gitattributes hunk rule: Rule 11 allows "one added line only" plus its comment (TA-2, T001), so the name-only
// listing is not enough. `git diff 30878b36 -- .gitattributes` must add exactly TA-2's two lines and remove none (the
// pattern of Phase 10's AFTER_M1_EXACT, specs/krate-manager-phase10/evidence/scope-check.js:48).
//
// Usage (repo root or anywhere inside the repo):
//   node specs/krate-manager-phase11/evidence/scope-check.js              # exit 0 clean, 1 breaches, 2 git error
//   node specs/krate-manager-phase11/evidence/scope-check.js --self-test  # the checkers over fixed inputs, no git
// It writes nothing; stdout only. close-lane.js requires it for gitattributesBreaches (G6).
'use strict';
const childProcess = require('child_process');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const BASE = '30878b36';
const APP_PREFIX = 'apps/krate-manager/';
const GITATTRIBUTES = '.gitattributes';
// TA-2 (tasks.md:268): the comment and the path line T001 adds after .gitattributes:31.
const TA2_LINES = [
  '# The rsign-format fixture of the Krate Manager Phase 11 refusal harness: its signature covers the raw bytes.',
  'apps/krate-manager/tests/fixtures/updater-refusals/fixtures/** -text',
];

// tasks.md Rule 11, outside apps/krate-manager/.
const EXACT = new Set([
  'specs/Krate-manager-roadmap.md', // FR-093 (a), (b), (c), (e), T002
  'specs/INDEX.md', // regenerated only, T018
  'tools/gen-manager-latest.js', // T010 (new)
  'tools/manager-release-preflight.js', // T011 (new)
  'tools/feed-publish.js', // T012, FR-043 only
  'tools/feed-contract/tests/feed-pipeline.test.js', // T012: fixture URL to GitHub's raw-slash tag form (main-loop ruling)
  ...['gen-manager-latest', 'manager-release-preflight', 'release-manager-shape', 'feed-publish-collect',
    'manager-ci-shape', 'hygiene'].map((s) => `tools/feed-contract/tests/${s}.test.js`),
  'tools/feed-contract/tests/release-body.expected.md', // T011 (new)
  'tools/feed-contract/keys/README.md', // T002, the §2 Phase 11 sentence
  '.github/workflows/release-manager.yml', // T015 (new)
  '.github/workflows/manager-ci.yml', // T017
  '.github/workflows/feed-tools.yml', // T017
  GITATTRIBUTES, // the hunk rule below
]);
const PREFIXES = ['specs/krate-manager-phase11/', APP_PREFIX];
// Named so the breach says why (spec FR-092; tasks.md T016). Checked before the allow list, so the helper crate is
// refused although it lies under APP_PREFIX.
const FENCE = [
  [/^apps\/krate-manager\/crates\/helper\//, 'the helper crate (FR-092)'],
  [/^(?:plugins|dsp|cmake|extern)\//, 'under plugins/, dsp/, cmake/ or extern/ (FR-092)'],
  [/^\.github\/workflows\/(?:feed|release|ci|docs|backfill-sign)\.yml$/, 'a workflow FR-092 keeps unchanged'],
  [/^tools\/gen-feed\.js$/, 'tools/gen-feed.js (FR-092)'],
  [/^tools\/feed-contract\/CONTRACT\.md$/, 'CONTRACT.md (FR-092)'],
  [/^tools\/feed-contract\/fixtures\//, 'a feed-contract fixture (FR-092)'],
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

/** Checker 1: one breach per path outside Rule 11 (the fence names its reason) and per path named *.key. */
function pathBreaches(paths) {
  const breaches = [];
  for (const p of paths) {
    if (/\.key$/i.test(path.posix.basename(p))) {
      breaches.push(`scope-check: ${p} is a file named *.key (no private key is ever written to a file, tasks.md Rule 1)`);
      continue;
    }
    const fenced = FENCE.find(([re]) => re.test(p));
    if (fenced) {
      breaches.push(`scope-check: ${p} is ${fenced[1]} (tasks.md Rule 11)`);
      continue;
    }
    if (EXACT.has(p) || PREFIXES.some((pre) => p.startsWith(pre))) continue;
    breaches.push(`scope-check: ${p} is not a Rule 11 path (tasks.md Rule 11)`);
  }
  return breaches;
}

/** Checker 2: `git diff <base> -- .gitattributes` adds exactly TA2_LINES (in order) and removes no line. */
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
  const breaches = removed.map((l) => `scope-check: ${GITATTRIBUTES} removes ${JSON.stringify(l)} (TA-2: none removed)`);
  if (JSON.stringify(added) !== JSON.stringify(TA2_LINES)) {
    breaches.push(`scope-check: ${GITATTRIBUTES} adds ${JSON.stringify(added)}, TA-2 allows exactly` +
      ` ${JSON.stringify(TA2_LINES)} (tasks.md Rule 11)`);
  }
  return breaches;
}

function selfTest() {
  // Rule 11's paths, spelled here (not read from EXACT) so a wrong table reads red.
  const inPaths = [
    'specs/krate-manager-phase11/spec.md', 'specs/krate-manager-phase11/evidence/d9-windows.txt',
    'specs/Krate-manager-roadmap.md', 'specs/INDEX.md', 'tools/gen-manager-latest.js',
    'tools/manager-release-preflight.js', 'tools/feed-publish.js',
    'tools/feed-contract/tests/gen-manager-latest.test.js', 'tools/feed-contract/tests/manager-release-preflight.test.js',
    'tools/feed-contract/tests/release-manager-shape.test.js', 'tools/feed-contract/tests/release-body.expected.md',
    'tools/feed-contract/tests/feed-publish-collect.test.js', 'tools/feed-contract/tests/manager-ci-shape.test.js',
    'tools/feed-contract/tests/hygiene.test.js', 'tools/feed-contract/keys/README.md',
    '.github/workflows/release-manager.yml', '.github/workflows/manager-ci.yml', '.github/workflows/feed-tools.yml',
    '.gitattributes', 'apps/krate-manager/Cargo.toml', 'apps/krate-manager/crates/core/src/self_update.rs',
    'apps/krate-manager/src-tauri/src/self_update.rs', 'apps/krate-manager/tests/fixtures/updater-refusals/src/main.rs',
    'apps/krate-manager/tests/fixtures/updater-refusals/fixtures/rsign/rsign.pub',
    'apps/krate-manager/tests/fixtures/updater-refusals/fixtures/rsign/package.bin.minisig',
    'apps/krate-manager/src/manager_update.ts', 'apps/krate-manager/scripts/e2e-update-server.js',
  ];
  const outPaths = [
    '.github/workflows/feed.yml', '.github/workflows/release.yml', '.github/workflows/ci.yml',
    '.github/workflows/docs.yml', '.github/workflows/backfill-sign.yml', '.github/workflows/long-tests-nightly.yml',
    'tools/gen-feed.js', 'tools/feed-contract/CONTRACT.md', 'tools/feed-contract/fixtures/kat/README.md',
    'tools/feed-contract/versions.v2.schema.json', 'tools/feed-contract/tests/other.test.js',
    'tools/feed-contract/keys/krate-signing.pub', 'tools/gen-specs-index.js',
    'apps/krate-manager/crates/helper/src/ops.rs', 'apps/krate-manager/crates/helper/Cargo.toml',
    'plugins/iterum/version.json', 'dsp/include/krate/dsp/core/db_utils.h', 'cmake/KratePlugin.cmake', 'extern/x',
    'specs/krate-manager-phase10/compliance.md', 'specs/krate-manager-phase11x/spec.md', '.claude/workflows/x.js',
    'apps/krate-manager/tests/fixtures/updater-refusals/k.key', 'specs/krate-manager-phase11/evidence/minisign.key',
  ];
  const ta2Added = TA2_LINES.map((l) => `+${l}`);
  const hunk = (added, removed = []) => ['diff --git a/.gitattributes b/.gitattributes', 'index 1c55d739..b8746610 100644',
    '--- a/.gitattributes', '+++ b/.gitattributes', '@@ -29,3 +29,5 @@ tools/feed-contract/fixtures/** -text',
    ' tools/feed-contract/keys/** -text', ...removed, ...added, ''].join('\n');
  const goodDiff = hunk(ta2Added);
  const cases = [
    { name: 'every Rule 11 path, tracked', run: () => pathBreaches(collectPaths(inPaths.join('\n'), '')), want: 0 },
    { name: 'every Rule 11 path, untracked (CRLF porcelain)',
      run: () => pathBreaches(collectPaths('', inPaths.map((p) => `?? ${p}`).join('\r\n'))), want: 0 },
    { name: 'tracked + untracked are deduplicated',
      run: () => collectPaths(inPaths.join('\n'), inPaths.map((p) => `?? ${p}`).join('\n')).map(() => 'x'),
      want: inPaths.length },
    { name: 'porcelain: modified entries are not taken from status (the diff lists them)',
      run: () => collectPaths('', ' M plugins/x\n?? specs/krate-manager-phase11/a.md\n'), want: 1 },
    { name: 'a quoted untracked path is unquoted',
      run: () => collectPaths('', '?? "specs/krate-manager-phase11/a b.md"\n')
        .filter((p) => p !== 'specs/krate-manager-phase11/a b.md'), want: 0 },
    ...outPaths.map((p) => ({ name: `${p} refused`, run: () => pathBreaches([p]), want: 1 })),
    { name: 'every out path refused once', run: () => pathBreaches(outPaths), want: outPaths.length },
    { name: 'the helper crate breach names FR-092',
      run: () => pathBreaches(['apps/krate-manager/crates/helper/src/ops.rs']).filter((b) => !b.includes('helper crate')),
      want: 0 },
    { name: 'a *.key breach names the key rule',
      run: () => pathBreaches(['apps/krate-manager/x.key']).filter((b) => !b.includes('*.key')), want: 0 },
    { name: '.gitattributes: TA-2\'s two lines added, none removed', run: () => gitattributesBreaches(goodDiff), want: 0 },
    { name: '.gitattributes: CRLF diff text', run: () => gitattributesBreaches(goodDiff.replace(/\n/g, '\r\n')), want: 0 },
    { name: '.gitattributes: one TA-2 line missing', run: () => gitattributesBreaches(hunk([ta2Added[1]])), want: 1 },
    { name: '.gitattributes: an extra added line',
      run: () => gitattributesBreaches(hunk([...ta2Added, '+*.png binary'])), want: 1 },
    { name: '.gitattributes: a removed line', run: () => gitattributesBreaches(hunk(ta2Added, ['-tools/feed-contract/keys/** -text'])),
      want: 1 },
    { name: '.gitattributes: the two lines swapped', run: () => gitattributesBreaches(hunk([ta2Added[1], ta2Added[0]])), want: 1 },
    { name: '.gitattributes: the path line without -text',
      run: () => gitattributesBreaches(hunk([ta2Added[0], '+apps/krate-manager/tests/fixtures/updater-refusals/fixtures/**'])),
      want: 1 },
    { name: '.gitattributes: an empty diff (TA-2 not written)', run: () => gitattributesBreaches(''), want: 1 },
    { name: 'base is 30878b36', run: () => (BASE === '30878b36' ? [] : [`BASE ${BASE}`]), want: 0 },
  ];
  let failed = 0;
  for (const c of cases) {
    let got;
    try {
      got = c.run();
    } catch (e) {
      got = [`threw: ${e.message}`];
      failed++;
      console.log(`FAILED ${c.name}: ${got[0]}`);
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

module.exports = { BASE, TA2_LINES, collectPaths, pathBreaches, gitattributesBreaches };
if (require.main === module) process.exit(main(process.argv.slice(2)));
