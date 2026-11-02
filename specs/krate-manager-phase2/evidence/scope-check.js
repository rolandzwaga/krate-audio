#!/usr/bin/env node
// Scope check for Krate Manager Phase 2 (SC-020, SC-016 diff clause, FR-080, FR-081; plan.md §4.13; tasks.md T020).
//
// WHY: Phase 2 may change only the FR-080 paths, may only ADD lines to release.yml outside its protected ranges
// (FR-081), may add exactly one export line to gen-feed.js (FR-075) and may delete from the three Phase 1 test files
// only the lines the FR-071 allowlist and the FR-060 scan exemption replace (SC-016). A stray helper, a moved installer
// step or a loosened Phase 1 assertion must show up at close as a named offence, not be found later in review. This runs
// the git listings, prints the raw counts and every offence, and exits 1 if there is any.
//
// Usage (repo root or anywhere inside the repo):
//   node specs/krate-manager-phase2/evidence/scope-check.js              # exit 0 clean, 1 offences, 2 git error
//   node specs/krate-manager-phase2/evidence/scope-check.js --self-test  # the checks against synthetic listings
'use strict';
const childProcess = require('child_process');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
// tasks.md header: the phase was written against feat/krate-manager at 1dec384c.
const BASE = '1dec384c';

// FR-080 (spec.md "H. Scope fences"; tasks.md rule 12). keys/krate-signing.pub (user-committed, SC-020) is in scope
// through the tools/feed-contract/ prefix.
const EXACT = new Set([
  'tools/minisign.js',
  'tools/release-facts.js',
  'tools/gen-release-manifest.js',
  'tools/backfill-sign.js',
  'tools/gen-feed.js',
  '.github/workflows/release.yml',
  '.github/workflows/backfill-sign.yml',
  '.github/workflows/feed-tools.yml',
  'specs/INDEX.md',
  'specs/Krate-manager-roadmap.md', // FR-061: the main loop names keys/krate-signing.pub in Layout, Phase 7, Phase 11
]);
const PREFIXES = ['tools/feed-contract/', 'specs/krate-manager-phase2/'];
// FR-080: "or the Phase 1 fixtures" -- every fixture outside fixtures/phase2/ is Phase 1's.
const FIXTURES = 'tools/feed-contract/fixtures/';
const PHASE2_FIXTURES = 'tools/feed-contract/fixtures/phase2/';

const RELEASE_YML = '.github/workflows/release.yml';
// FR-081: windows-installer job, the pkg build steps, the tarball build steps (pre-image line numbers at BASE).
const PROTECTED = [
  [189, 250],
  [285, 337],
  [379, 403],
];
const GEN_FEED = 'tools/gen-feed.js';
// FR-075: the one export line, compared trimmed.
const GEN_FEED_ADDED = ['selectReleases,'];
// SC-016: the only deletions allowed in the three Phase 1 test files, by pre-image line and exact text at BASE.
// write-guard.js :14 is the header line naming the allowlist ('git' and 'tar'), :235 and :243 are the two checkSpawn
// lines the rsign branch replaces; c3-trusted-comment.test.js :275 is the empty-list secret-key assertion that the
// one-path FR-060 exemption replaces. hygiene.test.js only gains lines.
const GUARDED = {
  'tools/feed-contract/tests/helpers/write-guard.js': [
    {
      line: 14,
      text: "//   - turns child_process into an allowlist: only 'git' and 'tar' in fixed argument shapes, with a cwd under tmp;",
    },
    {
      line: 235,
      text: "    if (command !== 'git' && command !== 'tar') throw guardError(op, String(command), 'command not allowed');",
    },
    { line: 243, text: '    else checkTar(op, args, cwd);' },
  ],
  'tools/feed-contract/tests/hygiene.test.js': [],
  'tools/feed-contract/tests/c3-trusted-comment.test.js': [
    { line: 275, text: '  assert.deepEqual(findSecretKeyFiles(CONTRACT_DIR), []);' },
  ],
};
const DIFF_PATHS = [RELEASE_YML, GEN_FEED, ...Object.keys(GUARDED)];

function inScope(p) {
  return EXACT.has(p) || PREFIXES.some((pre) => p.startsWith(pre));
}

/** git quotes a path with unusual characters ("a b.txt"); core.quotepath=off leaves non-ASCII bytes alone. */
function unquote(p) {
  if (!(p.length >= 2 && p.startsWith('"') && p.endsWith('"'))) return p;
  try {
    return JSON.parse(p);
  } catch {
    return p.slice(1, -1);
  }
}

/** Paths of `git status --porcelain` lines: the part after the two status columns, both sides of a `->`. */
function statusPaths(text) {
  const paths = [];
  for (const line of text.split(/\r?\n/)) {
    if (!line.trim()) continue;
    const xy = line.slice(0, 2);
    const rest = line.slice(3);
    const sides = /[RC]/.test(xy) && rest.includes(' -> ') ? rest.split(' -> ') : [rest];
    for (const side of sides) paths.push(unquote(side.trim()));
  }
  return paths;
}

/** Paths of `git diff --name-only` lines: one path per line. */
function diffPaths(text) {
  return text
    .split(/\r?\n/)
    .filter((l) => l.trim())
    .map((l) => unquote(l.trim()));
}

/**
 * Hunks of a `git diff -U0` listing: { file, oldStart, oldCount, minus: [{ line, text }], plus: [text] }.
 * `minus[].line` is the pre-image line number of each deleted line. A hunk ends when its header's line counts are used
 * up, so a deleted line that reads `-- a/x` is not taken for the next file header.
 */
function parseDiff(text) {
  const hunks = [];
  let oldFile = null;
  let file = null;
  let hunk = null;
  let oldLine = 0;
  let oldLeft = 0;
  let newLeft = 0;
  for (const raw of text.split('\n')) {
    const line = raw.endsWith('\r') ? raw.slice(0, -1) : raw;
    if (hunk && oldLeft === 0 && newLeft === 0) hunk = null;
    if (hunk) {
      if (line.startsWith('-')) {
        hunk.minus.push({ line: oldLine++, text: line.slice(1) });
        oldLeft--;
      } else if (line.startsWith('+')) {
        hunk.plus.push(line.slice(1));
        newLeft--;
      }
      continue;
    }
    if (line.startsWith('diff --git ')) continue;
    if (line.startsWith('--- ')) {
      const p = line.slice(4);
      oldFile = p === '/dev/null' ? null : unquote(p).replace(/^a\//, '');
      continue;
    }
    if (line.startsWith('+++ ')) {
      const p = line.slice(4);
      file = p === '/dev/null' ? oldFile : unquote(p).replace(/^b\//, '');
      continue;
    }
    const m = /^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@/.exec(line);
    if (m) {
      hunk = { file, oldStart: Number(m[1]), oldCount: m[2] === undefined ? 1 : Number(m[2]), minus: [], plus: [] };
      oldLine = hunk.oldStart;
      oldLeft = hunk.oldCount;
      newLeft = m[4] === undefined ? 1 : Number(m[4]);
      hunks.push(hunk);
    }
  }
  return hunks;
}

/** Every offence, in a fixed order: out-of-scope paths, Phase 1 fixtures, release.yml, gen-feed.js, guarded files. */
function evaluate({ status, nameOnly, diff }) {
  const offences = [];
  const paths = [];
  for (const p of [...statusPaths(status), ...diffPaths(nameOnly)]) if (!paths.includes(p)) paths.push(p);
  for (const p of paths) if (!inScope(p)) offences.push(`scope: ${p} is not an FR-080 path`);
  for (const p of paths) {
    if (p.startsWith(FIXTURES) && !p.startsWith(PHASE2_FIXTURES)) {
      offences.push(`phase1-fixture: ${p} changed (FR-080)`);
    }
  }

  const hunks = parseDiff(diff);
  for (const h of hunks.filter((x) => x.file === RELEASE_YML)) {
    for (const d of h.minus) offences.push(`release.yml: line ${d.line} deleted or changed (FR-081)`);
    const range = PROTECTED.find(([lo, hi]) => h.oldStart >= lo && h.oldStart <= hi);
    if (range) offences.push(`release.yml: hunk at pre-image ${h.oldStart} inside :${range[0]}-${range[1]} (FR-081)`);
  }

  const genFeed = hunks.filter((x) => x.file === GEN_FEED);
  for (const h of genFeed) for (const d of h.minus) offences.push(`gen-feed.js: line ${d.line} deleted (FR-075)`);
  const added = genFeed.flatMap((h) => h.plus.map((s) => s.trim()));
  if (JSON.stringify(added) !== JSON.stringify(GEN_FEED_ADDED)) {
    offences.push(`gen-feed.js: added ${JSON.stringify(added)}, want exactly ${JSON.stringify(GEN_FEED_ADDED)} (FR-075)`);
  }

  for (const [file, allowed] of Object.entries(GUARDED)) {
    for (const h of hunks.filter((x) => x.file === file)) {
      for (const d of h.minus) {
        if (!allowed.some((a) => a.line === d.line && a.text === d.text)) {
          offences.push(`phase1-test: ${file}:${d.line} deleted (SC-016): ${JSON.stringify(d.text)}`);
        }
      }
    }
  }
  return offences;
}

function selfTest() {
  const cleanStatus = [
    ' M .github/workflows/release.yml',
    ' M tools/gen-feed.js',
    ' M tools/feed-contract/tests/helpers/write-guard.js',
    ' M specs/INDEX.md',
    '?? .github/workflows/backfill-sign.yml',
    '?? specs/krate-manager-phase2/evidence/scope-check.js',
    '?? tools/feed-contract/fixtures/phase2/kat-secret/kat.pub',
    '?? tools/feed-contract/keys/krate-signing.pub',
    '?? tools/minisign.js',
    '',
  ].join('\n');
  const cleanNames = '.github/workflows/release.yml\ntools/gen-feed.js\ntools/feed-contract/contract.js\n';
  const releaseAdd = [
    'diff --git a/.github/workflows/release.yml b/.github/workflows/release.yml',
    '--- a/.github/workflows/release.yml',
    '+++ b/.github/workflows/release.yml',
    '@@ -338,0 +343,2 @@ jobs:',
    '+      - name: Record macOS facts',
    '+        run: node tools/release-facts.js',
  ];
  const genFeedOne = [
    'diff --git a/tools/gen-feed.js b/tools/gen-feed.js',
    '--- a/tools/gen-feed.js',
    '+++ b/tools/gen-feed.js',
    '@@ -771,0 +772 @@ module.exports = {',
    '+  selectReleases,',
  ];
  const guardedOk = [
    'diff --git a/tools/feed-contract/tests/helpers/write-guard.js b/tools/feed-contract/tests/helpers/write-guard.js',
    '--- a/tools/feed-contract/tests/helpers/write-guard.js',
    '+++ b/tools/feed-contract/tests/helpers/write-guard.js',
    '@@ -243 +268,2 @@ function install() {',
    '-    else checkTar(op, args, cwd);',
    "+    else if (command === 'tar') checkTar(op, args, cwd);",
    '+    else checkRsign(op, args, cwd);',
    'diff --git a/tools/feed-contract/tests/c3-trusted-comment.test.js b/tools/feed-contract/tests/c3-trusted-comment.test.js',
    '--- a/tools/feed-contract/tests/c3-trusted-comment.test.js',
    '+++ b/tools/feed-contract/tests/c3-trusted-comment.test.js',
    '@@ -275 +275,2 @@ test(...)',
    '-  assert.deepEqual(findSecretKeyFiles(CONTRACT_DIR), []);',
    '+  assert.deepEqual(findSecretKeyFiles(CONTRACT_DIR), [KAT_SECRET_KEY]);',
  ];
  const join = (...parts) => `${parts.flat().join('\n')}\n`;
  const cases = [
    {
      name: 'clean',
      input: { status: cleanStatus, nameOnly: cleanNames, diff: join(releaseAdd, genFeedOne, guardedOk) },
      expect: [],
    },
    {
      name: 'stray',
      input: { status: `${cleanStatus}?? tools/stray.js\n`, nameOnly: cleanNames, diff: join(genFeedOne) },
      expect: ['scope: tools/stray.js is not an FR-080 path'],
    },
    {
      name: 'release.yml deletes pre-image line 300',
      input: {
        status: cleanStatus,
        nameOnly: cleanNames,
        diff: join(
          '--- a/.github/workflows/release.yml',
          '+++ b/.github/workflows/release.yml',
          '@@ -300 +299,0 @@ jobs:',
          '-        run: pkgbuild --root stage',
          genFeedOne,
        ),
      },
      expect: [
        'release.yml: line 300 deleted or changed (FR-081)',
        'release.yml: hunk at pre-image 300 inside :285-337 (FR-081)',
      ],
    },
    {
      name: 'gen-feed.js adds more than selectReleases,',
      input: {
        status: cleanStatus,
        nameOnly: cleanNames,
        diff: join(genFeedOne.slice(0, 3), '@@ -771,0 +772,2 @@', '+  selectReleases,', '+  parseTag,'),
      },
      expect: ['gen-feed.js: added ["selectReleases,","parseTag,"], want exactly ["selectReleases,"] (FR-075)'],
    },
    {
      name: 'release.yml insertion inside a protected range',
      input: {
        status: cleanStatus,
        nameOnly: cleanNames,
        diff: join('--- a/.github/workflows/release.yml', '+++ b/.github/workflows/release.yml', '@@ -390,0 +391 @@', '+x', genFeedOne),
      },
      expect: ['release.yml: hunk at pre-image 390 inside :379-403 (FR-081)'],
    },
    {
      name: 'Phase 1 fixture and a loosened Phase 1 assertion',
      input: {
        status: `${cleanStatus} M tools/feed-contract/fixtures/feed-v1/versions.json\n`,
        nameOnly: cleanNames,
        diff: join(
          genFeedOne,
          '--- a/tools/feed-contract/tests/hygiene.test.js',
          '+++ b/tools/feed-contract/tests/hygiene.test.js',
          '@@ -140 +140 @@',
          '-  assert.equal(a, b);',
          '+  assert.ok(true);',
          '--- a/tools/feed-contract/tests/helpers/write-guard.js',
          '+++ b/tools/feed-contract/tests/helpers/write-guard.js',
          '@@ -235 +235 @@',
          '-    if (command !== 1) throw x;',
          '+    if (true) {}',
        ),
      },
      expect: [
        'phase1-fixture: tools/feed-contract/fixtures/feed-v1/versions.json changed (FR-080)',
        'phase1-test: tools/feed-contract/tests/helpers/write-guard.js:235 deleted (SC-016): "    if (command !== 1) throw x;"',
        'phase1-test: tools/feed-contract/tests/hygiene.test.js:140 deleted (SC-016): "  assert.equal(a, b);"',
      ],
    },
  ];
  let failed = 0;
  for (const c of cases) {
    const got = evaluate(c.input);
    const exit = got.length ? 1 : 0;
    const wantExit = c.expect.length ? 1 : 0;
    const ok = exit === wantExit && JSON.stringify(got) === JSON.stringify(c.expect);
    if (!ok) failed++;
    console.log(`${ok ? 'ok    ' : 'FAILED'} ${c.name}: ${got.length} offence(s), exit=${exit} (want ${c.expect.length}, exit=${wantExit})`);
    for (const o of got) console.log(`         ${o}`);
    if (!ok) for (const o of c.expect) console.log(`  want   ${o}`);
  }
  console.log(failed ? `SELF-TEST RED (${failed} of ${cases.length})` : `SELF-TEST GREEN (${cases.length} cases)`);
  return failed ? 1 : 0;
}

function git(args) {
  const r = childProcess.spawnSync('git', args, { cwd: ROOT, encoding: 'utf8', windowsHide: true, maxBuffer: 1 << 28 });
  if (r.error || r.status !== 0) {
    console.log(`scope-check: git ${args.join(' ')} failed: ${r.error ? r.error.message : r.stderr.trim()}`);
    process.exit(2);
  }
  return r.stdout;
}

function countLines(text) {
  return text.split(/\r?\n/).filter(Boolean).length;
}

function main() {
  if (process.argv.includes('--self-test')) process.exit(selfTest());
  const statusCmd = ['-c', 'core.quotepath=off', 'status', '--porcelain', '--untracked-files=all'];
  const namesCmd = ['-c', 'core.quotepath=off', 'diff', '--name-only', BASE];
  const diffCmd = ['-c', 'core.quotepath=off', 'diff', '-U0', BASE, '--', ...DIFF_PATHS];
  const input = { status: git(statusCmd), nameOnly: git(namesCmd), diff: git(diffCmd) };
  console.log(`$ git ${statusCmd.join(' ')}   (${countLines(input.status)} lines)`);
  console.log(`$ git ${namesCmd.join(' ')}   (${countLines(input.nameOnly)} lines)`);
  const hunks = parseDiff(input.diff);
  for (const f of DIFF_PATHS) {
    const hs = hunks.filter((h) => h.file === f);
    const minus = hs.reduce((n, h) => n + h.minus.length, 0);
    const plus = hs.reduce((n, h) => n + h.plus.length, 0);
    console.log(`$ git diff -U0 ${BASE} -- ${f}   (${hs.length} hunks, -${minus} +${plus})`);
  }
  const offences = evaluate(input);
  const count = (prefix) => offences.filter((o) => o.startsWith(prefix)).length;
  console.log(
    `out-of-scope paths: ${count('scope:')}; Phase 1 fixtures: ${count('phase1-fixture:')}; ` +
      `release.yml protected: ${count('release.yml:')}; gen-feed.js: ${count('gen-feed.js:')}; ` +
      `Phase 1 test deletions: ${count('phase1-test:')}`,
  );
  for (const o of offences) console.log(o);
  console.log(offences.length ? `SCOPE RED: ${offences.length} offence(s)` : 'SCOPE GREEN');
  process.exit(offences.length ? 1 : 0);
}

main();
