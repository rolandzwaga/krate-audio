#!/usr/bin/env node
// Scope check for Krate Manager Phase 3 (spec SC-012, FR-060 as amended, FR-032, FR-033, FR-062; plan.md §4.10;
// tasks.md T006).
//
// WHY: Phase 3 may change only the FR-060 paths. ci.yml stays byte-identical (Q6 re-ruled, FR-062); tools/gen-feed.js,
// docs.yml, backfill-sign.yml, every setup.iss and every version.json stay byte-identical; under
// tools/feed-contract/fixtures/ only the phase3 set, the one new mixed feed, feeds/cases.json and make-fixtures.js may
// change; release.yml may lose exactly the "Generate Update Checker Endpoint" step (4f4c4175 :859-900) and gain exactly
// the plan §4.4 `feed` job at its end. A stray helper, a dropped `keep_files: true` or an edited fixture must show up at
// close as a named offence, not be found later in review. This runs the git listings, prints the raw counts and every
// offence, and exits 1 if there is any. It spawns git, so it is evidence, not a test (the write guard refuses it).
//
// Usage (repo root or anywhere inside the repo):
//   node specs/krate-manager-phase3/evidence/scope-check.js              # exit 0 clean, 1 offences, 2 git error
//   node specs/krate-manager-phase3/evidence/scope-check.js --self-test  # the checks against synthetic inputs
'use strict';
const childProcess = require('child_process');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
// tasks.md header: the phase was written against feat/krate-manager at 4f4c4175 (spec SC-012).
const BASE = '4f4c4175';

// FR-060 as amended by T001 (tasks.md rule 10, T006).
const EXACT = new Set([
  '.github/workflows/feed.yml',
  '.github/workflows/release.yml',
  '.github/workflows/feed-tools.yml',
  'tools/feed-publish.js',
  'plugins/shared/tests/CMakeLists.txt',
  'plugins/shared/tests/test_update_checker.cpp',
  'specs/INDEX.md',
  'specs/Krate-manager-roadmap.md', // the Phase 3 status line (main loop at close)
]);
const PREFIXES = ['tools/feed-contract/', 'specs/krate-manager-phase3/'];
// Under fixtures/ only these may change; every other fixture (Phase 1, Phase 2, the gen-feed snapshot) is frozen.
const FIXTURES = 'tools/feed-contract/fixtures/';
const FIXTURE_PREFIXES = [`${FIXTURES}phase3/`];
const FIXTURE_EXACT = new Set([
  `${FIXTURES}feeds/valid/one-v2-eight-v1.json`,
  `${FIXTURES}feeds/cases.json`,
  `${FIXTURES}make-fixtures.js`,
]);
// `git diff BASE -- <these>` must be empty.
const FROZEN = ['tools/gen-feed.js', '.github/workflows/docs.yml', '.github/workflows/backfill-sign.yml'];
const FROZEN_NAMES = ['setup.iss', 'version.json'];

const CI_YML = '.github/workflows/ci.yml';
const RELEASE_YML = '.github/workflows/release.yml';
// FR-032: the deleted step, pre-image lines at BASE ("Generate Update Checker Endpoint" through the blank line before
// "Deploy to gh-pages"), and the anchors that prove the base text is the one the plan read.
const DELETE_FROM = 859;
const DELETE_TO = 900;
const BASE_LINE_COUNT = 906;
const ANCHORS = [
  [DELETE_FROM, '      - name: Generate Update Checker Endpoint'],
  [DELETE_TO, ''],
  [DELETE_TO + 1, '      - name: Deploy to gh-pages'],
  [BASE_LINE_COUNT, '          keep_files: true'],
];
// FR-030: plan §4.4's block, appended after the last line, byte for byte (line endings aside).
const FEED_BLOCK = [
  '',
  '  # versions.json (the in-plugin update checker and the manager read it) is built, checked and deployed by the',
  '  # reusable feed workflow (specs/krate-manager-phase3/spec.md FR-030; plan §4.4). On a dry run feed.yml checks only.',
  '  feed:',
  '    needs: [release, deploy-pages]',
  "    if: ${{ !cancelled() && needs.release.result == 'success' }}",
  '    permissions:',
  '      contents: write',
  '    uses: ./.github/workflows/feed.yml',
  '    with:',
  '      dry_run: ${{ inputs.dry_run }}',
];

function inScope(p) {
  return EXACT.has(p) || PREFIXES.some((pre) => p.startsWith(pre));
}

function fixtureAllowed(p) {
  return FIXTURE_EXACT.has(p) || FIXTURE_PREFIXES.some((pre) => p.startsWith(pre));
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

/** Lines of a text file, without line endings and without the empty element after the final newline. */
function textLines(text) {
  const lines = text.split('\n').map((l) => (l.endsWith('\r') ? l.slice(0, -1) : l));
  if (lines.length && lines[lines.length - 1] === '') lines.pop();
  return lines;
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

/**
 * A pure deletion whose first deleted line equals the line after it (or whose last equals the line before it) can be
 * reported one line earlier or later with the same post-image; the diff heuristic picks one. Slide it toward
 * DELETE_FROM so the range check does not depend on that choice. Only a hunk whose deleted text matches the base is
 * slid; the post-image is unchanged by construction.
 */
function slideDeletion(h, base) {
  if (h.plus.length || !h.oldCount) return h;
  const at = (n) => base[n - 1];
  if (!h.minus.every((d) => at(d.line) === d.text)) return h;
  let start = h.oldStart;
  const count = h.oldCount;
  while (start < DELETE_FROM && start + count <= base.length && at(start) === at(start + count)) start++;
  while (start > DELETE_FROM && start > 1 && at(start - 1) === at(start + count - 1)) start--;
  if (start === h.oldStart) return h;
  const minus = [];
  for (let n = start; n < start + count; n++) minus.push({ line: n, text: at(n) });
  return { ...h, oldStart: start, minus };
}

/** "859-861, 870" for a sorted list of line numbers. */
function ranges(nums) {
  const out = [];
  for (let i = 0; i < nums.length; i++) {
    let j = i;
    while (j + 1 < nums.length && nums[j + 1] === nums[j] + 1) j++;
    out.push(i === j ? `${nums[i]}` : `${nums[i]}-${nums[j]}`);
    i = j;
  }
  return out.join(', ');
}

function releaseOffences(diff, baseText) {
  const offences = [];
  const base = textLines(baseText);
  if (base.length !== BASE_LINE_COUNT) {
    offences.push(`release.yml: base ${BASE} has ${base.length} lines, want ${BASE_LINE_COUNT} (plan §4.4)`);
  }
  for (const [n, text] of ANCHORS) {
    if (base[n - 1] !== text) {
      offences.push(`release.yml: base ${BASE}:${n} is ${JSON.stringify(base[n - 1])}, want ${JSON.stringify(text)}`);
    }
  }
  const hunks = parseDiff(diff)
    .filter((h) => h.file === RELEASE_YML)
    .map((h) => slideDeletion(h, base));

  const deleted = new Set();
  for (const h of hunks) {
    for (const d of h.minus) {
      if (d.line < DELETE_FROM || d.line > DELETE_TO) {
        offences.push(`release.yml: line ${d.line} deleted outside :${DELETE_FROM}-${DELETE_TO} (FR-033): ${JSON.stringify(d.text)}`);
      } else if (base[d.line - 1] !== d.text) {
        offences.push(`release.yml: line ${d.line} deleted as ${JSON.stringify(d.text)}, base has ${JSON.stringify(base[d.line - 1])}`);
      } else {
        deleted.add(d.line);
      }
    }
  }
  const kept = [];
  for (let n = DELETE_FROM; n <= DELETE_TO; n++) if (!deleted.has(n)) kept.push(n);
  if (kept.length) offences.push(`release.yml: pre-image :${ranges(kept)} not deleted (FR-032)`);

  // The only lines added form one hunk at the end of the file: after the last base line, or replacing a tail of it
  // (a deleted tail line is already an offence above).
  let appended = null;
  for (const h of hunks.filter((x) => x.plus.length)) {
    const last = h.oldCount === 0 ? h.oldStart : h.oldStart + h.oldCount - 1;
    if (last === base.length && appended === null) {
      appended = h.plus;
    } else {
      offences.push(`release.yml: ${h.plus.length} line(s) added at pre-image ${h.oldStart}, not at the end (FR-033)`);
    }
  }
  if (appended === null) {
    offences.push('release.yml: the plan §4.4 feed block is not appended (FR-030)');
  } else {
    if (appended.length !== FEED_BLOCK.length) {
      offences.push(`release.yml: ${appended.length} line(s) appended, plan §4.4 has ${FEED_BLOCK.length} (FR-030)`);
    }
    const n = Math.min(appended.length, FEED_BLOCK.length);
    for (let i = 0; i < n; i++) {
      if (appended[i] !== FEED_BLOCK[i]) {
        offences.push(
          `release.yml: appended line ${i + 1} differs from plan §4.4 (FR-030): ${JSON.stringify(appended[i])}, ` +
            `want ${JSON.stringify(FEED_BLOCK[i])}`,
        );
      }
    }
  }
  return offences;
}

/**
 * Every offence, in a fixed order: out-of-scope paths, frozen fixtures, setup.iss / version.json paths, the frozen
 * files' diff, ci.yml, release.yml.
 */
function evaluate({ status, nameOnly, frozenDiff, ciDiff, releaseDiff, releaseBase }) {
  const offences = [];
  const paths = [];
  for (const p of [...statusPaths(status), ...diffPaths(nameOnly)]) if (!paths.includes(p)) paths.push(p);
  for (const p of paths) if (!inScope(p)) offences.push(`scope: ${p} is not an FR-060 path`);
  for (const p of paths) {
    if (p.startsWith(FIXTURES) && !fixtureAllowed(p)) offences.push(`fixture: ${p} changed (FR-060)`);
  }
  for (const p of paths) {
    if (FROZEN_NAMES.includes(path.posix.basename(p))) offences.push(`frozen: ${p} changed (FR-060)`);
  }

  if (frozenDiff.trim()) {
    const files = [];
    for (const line of textLines(frozenDiff)) {
      const m = /^diff --git a\/(.+) b\/(.+)$/.exec(line);
      if (m && !files.includes(m[2])) files.push(m[2]);
    }
    if (files.length) for (const f of files) offences.push(`frozen: ${f} differs from ${BASE} (FR-060)`);
    else offences.push(`frozen: git diff ${BASE} -- ${FROZEN.join(' ')} is not empty (FR-060)`);
  }

  for (const h of parseDiff(ciDiff).filter((x) => x.file === CI_YML)) {
    for (const d of h.minus) offences.push(`ci.yml: line ${d.line} deleted (FR-062): ${JSON.stringify(d.text)}`);
    for (const t of h.plus) offences.push(`ci.yml: line added at pre-image ${h.oldStart} (FR-062): ${JSON.stringify(t)}`);
  }

  offences.push(...releaseOffences(releaseDiff, releaseBase));
  return offences;
}

function selfTest() {
  // A stand-in for the 4f4c4175 release.yml: 906 lines whose anchors are the real ones; :858 and :900 are blank, as in
  // the real file, so a deletion reported one line early has the same post-image.
  const baseLines = [];
  for (let n = 1; n <= BASE_LINE_COUNT; n++) baseLines.push(`# base line ${n}`);
  for (const [n, text] of ANCHORS) baseLines[n - 1] = text;
  baseLines[858 - 1] = '';
  baseLines[860 - 1] = '        run: |';
  baseLines[899 - 1] = "          '";
  const releaseBase = `${baseLines.join('\n')}\n`;
  const baseAt = (from, to) => baseLines.slice(from - 1, to).map((t) => `-${t}`);
  const head = ['diff --git a/.github/workflows/release.yml b/.github/workflows/release.yml', '--- a/.github/workflows/release.yml', '+++ b/.github/workflows/release.yml'];
  const delHunk = ['@@ -859,42 +858,0 @@ jobs:', ...baseAt(859, 900)];
  const addHunk = (block) => [`@@ -906,0 +865,${block.length} @@ jobs:`, ...block.map((t) => `+${t}`)];
  const join = (...parts) => `${parts.flat().join('\n')}\n`;
  const releaseClean = join(head, delHunk, addHunk(FEED_BLOCK));

  const cleanStatus = [
    ' M .github/workflows/release.yml',
    ' M .github/workflows/feed-tools.yml',
    ' M plugins/shared/tests/CMakeLists.txt',
    ' M plugins/shared/tests/test_update_checker.cpp',
    ' M specs/INDEX.md',
    ' M tools/feed-contract/contract.js',
    ' M tools/feed-contract/fixtures/feeds/cases.json',
    ' M tools/feed-contract/fixtures/make-fixtures.js',
    '?? .github/workflows/feed.yml',
    '?? specs/krate-manager-phase3/evidence/scope-check.js',
    '?? tools/feed-contract/fixtures/feeds/valid/one-v2-eight-v1.json',
    '?? tools/feed-contract/fixtures/phase3/make-phase3.js',
    '?? tools/feed-contract/fixtures/phase3/compare/cases.json',
    '?? tools/feed-publish.js',
    '',
  ].join('\n');
  const cleanNames = '.github/workflows/release.yml\ntools/feed-contract/contract.js\nspecs/Krate-manager-roadmap.md\n';
  const clean = { status: cleanStatus, nameOnly: cleanNames, frozenDiff: '', ciDiff: '', releaseDiff: releaseClean, releaseBase };
  const ciHead = ['diff --git a/.github/workflows/ci.yml b/.github/workflows/ci.yml', '--- a/.github/workflows/ci.yml', '+++ b/.github/workflows/ci.yml'];
  const oneOff = FEED_BLOCK.map((t, i) => (i === FEED_BLOCK.length - 1 ? t.replace('dry_run }}', 'dry-run }}') : t));

  const cases = [
    { name: '(1) clean listing', input: clean, expect: [] },
    {
      name: '(1b) clean listing, deletion reported one line early (:858-899)',
      input: { ...clean, releaseDiff: join(head, ['@@ -858,42 +857,0 @@ jobs:', ...baseAt(858, 899)], addHunk(FEED_BLOCK)) },
      expect: [],
    },
    {
      name: '(2) untracked tools/stray.js',
      input: { ...clean, status: `${cleanStatus}?? tools/stray.js\n` },
      expect: ['scope: tools/stray.js is not an FR-060 path'],
    },
    {
      name: '(3) ci.yml diff with a - line',
      input: { ...clean, ciDiff: join(ciHead, '@@ -86 +85,0 @@ jobs:', "-              - 'plugins/shared/**'") },
      expect: [`ci.yml: line 86 deleted (FR-062): ${JSON.stringify("              - 'plugins/shared/**'")}`],
    },
    {
      name: '(4) ci.yml diff with a + line',
      input: { ...clean, ciDiff: join(ciHead, '@@ -86,0 +87 @@ jobs:', "+              - 'tools/feed-contract/fixtures/feeds/**'") },
      expect: [`ci.yml: line added at pre-image 86 (FR-062): ${JSON.stringify("              - 'tools/feed-contract/fixtures/feeds/**'")}`],
    },
    {
      name: '(5) release.yml deletes keep_files: true (outside :859-900)',
      input: {
        ...clean,
        releaseDiff: join(head, delHunk, `@@ -906 +864,${FEED_BLOCK.length} @@ jobs:`, '-          keep_files: true', FEED_BLOCK.map((t) => `+${t}`)),
      },
      expect: [`release.yml: line 906 deleted outside :859-900 (FR-033): ${JSON.stringify('          keep_files: true')}`],
    },
    {
      name: '(6) release.yml + block one character off plan §4.4',
      input: { ...clean, releaseDiff: join(head, delHunk, addHunk(oneOff)) },
      expect: [
        `release.yml: appended line 11 differs from plan §4.4 (FR-030): ${JSON.stringify(oneOff[10])}, want ${JSON.stringify(FEED_BLOCK[10])}`,
      ],
    },
    {
      name: '(7) gen-feed snapshot README changed',
      input: { ...clean, status: `${cleanStatus} M tools/feed-contract/fixtures/gen-feed/snapshot-2026-10-29/README.md\n` },
      expect: ['fixture: tools/feed-contract/fixtures/gen-feed/snapshot-2026-10-29/README.md changed (FR-060)'],
    },
    {
      name: '(8) Phase 2 backfill state.json changed',
      input: { ...clean, nameOnly: `${cleanNames}tools/feed-contract/fixtures/phase2/backfill/state.json\n` },
      expect: ['fixture: tools/feed-contract/fixtures/phase2/backfill/state.json changed (FR-060)'],
    },
    {
      name: '(9) gen-feed.js diff and a plugin version.json',
      input: {
        ...clean,
        nameOnly: `${cleanNames}tools/gen-feed.js\nplugins/alpha/version.json\n`,
        frozenDiff: join('diff --git a/tools/gen-feed.js b/tools/gen-feed.js', '--- a/tools/gen-feed.js', '+++ b/tools/gen-feed.js', '@@ -1 +1 @@', '-a', '+b'),
      },
      expect: [
        'scope: tools/gen-feed.js is not an FR-060 path',
        'scope: plugins/alpha/version.json is not an FR-060 path',
        'frozen: plugins/alpha/version.json changed (FR-060)',
        'frozen: tools/gen-feed.js differs from 4f4c4175 (FR-060)',
      ],
    },
    {
      name: '(10) release.yml step not removed and nothing appended',
      input: { ...clean, releaseDiff: '' },
      expect: ['release.yml: pre-image :859-900 not deleted (FR-032)', 'release.yml: the plan §4.4 feed block is not appended (FR-030)'],
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
  const q = ['-c', 'core.quotepath=off'];
  const diffOpts = ['--no-color', '--no-ext-diff'];
  const cmds = {
    status: [...q, 'status', '--porcelain', '--untracked-files=all'],
    nameOnly: [...q, 'diff', ...diffOpts, '--name-only', BASE],
    frozenDiff: [...q, 'diff', ...diffOpts, BASE, '--', ...FROZEN],
    ciDiff: [...q, 'diff', ...diffOpts, '--unified=0', BASE, '--', CI_YML],
    releaseDiff: [...q, 'diff', ...diffOpts, '--unified=0', BASE, '--', RELEASE_YML],
    releaseBase: ['show', `${BASE}:${RELEASE_YML}`],
  };
  const input = {};
  for (const [key, args] of Object.entries(cmds)) input[key] = git(args);
  for (const key of ['status', 'nameOnly', 'frozenDiff']) {
    console.log(`$ git ${cmds[key].join(' ')}   (${countLines(input[key])} lines)`);
  }
  for (const [key, file] of [['ciDiff', CI_YML], ['releaseDiff', RELEASE_YML]]) {
    const hs = parseDiff(input[key]).filter((h) => h.file === file);
    const minus = hs.reduce((n, h) => n + h.minus.length, 0);
    const plus = hs.reduce((n, h) => n + h.plus.length, 0);
    console.log(`$ git ${cmds[key].join(' ')}   (${hs.length} hunks, -${minus} +${plus})`);
  }
  console.log(`$ git ${cmds.releaseBase.join(' ')}   (${textLines(input.releaseBase).length} lines)`);
  const offences = evaluate(input);
  const count = (prefix) => offences.filter((o) => o.startsWith(prefix)).length;
  console.log(
    `out-of-scope paths: ${count('scope:')}; frozen fixtures: ${count('fixture:')}; frozen files: ${count('frozen:')}; ` +
      `ci.yml: ${count('ci.yml:')}; release.yml: ${count('release.yml:')}`,
  );
  for (const o of offences) console.log(o);
  console.log(offences.length ? `SCOPE RED: ${offences.length} offence(s)` : 'SCOPE GREEN');
  process.exit(offences.length ? 1 : 0);
}

main();
