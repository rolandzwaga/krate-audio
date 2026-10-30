#!/usr/bin/env node
// Scope check for Krate Manager Phase 1 (SC-020, FR-070; plan.md §8 step 8; tasks.md T020).
//
// WHY: Phase 1 may change only the FR-070 paths. A stray helper, a touched workflow or a plugin source edit must show
// up at close as a named path, not be found later in review. This runs the two git listings the plan names, drops every
// FR-070 path and the one start-of-phase baseline line, prints the raw and the filtered output, and exits 1 if anything
// is left.
//
// Usage (repo root or anywhere inside the repo):
//   node specs/krate-manager-phase1/evidence/scope-check.js              # exit 0 clean, 1 out-of-scope paths, 2 git error
//   node specs/krate-manager-phase1/evidence/scope-check.js --self-test  # the filter against three synthetic listings
'use strict';
const childProcess = require('child_process');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
// tasks.md header: the phase was written against feat/krate-manager at 84c6aab1. The base is 966ef217, the workflow-harness
// fix committed on its own on top of 84c6aab1 (ruled out of Phase 1's scope, T020), so its file is not listed here.
const BASE = '966ef217';

// FR-070 (spec.md "I. Scope fences"; tasks.md rule 8). specs/INDEX.md is in scope by the Q-B ruling (spec.md
// Clarifications, Session 2026-10-30, [PD-9]: "Yes. Regenerate it in this change"). .gitignore replaces the root
// package.json / package-lock.json by the FR-062 ruling (Session 2026-10-30, Q-D): ajv lives in tools/feed-contract/.
const EXACT = new Set([
  'tools/gen-feed.js',
  'plugins/shared/tests/test_update_checker.cpp',
  '.github/workflows/feed-tools.yml',
  '.gitignore',
  '.gitattributes',
  'specs/INDEX.md',
]);
const PREFIXES = ['tools/feed-contract/', 'specs/krate-manager-phase1/'];
// Untracked before this phase began (tasks.md header) and not changed by it: dropped as the whole status line only.
const BASELINE_LINES = new Set(['?? specs/Krate-manager-roadmap.md']);
// SC-020 names these two explicitly; they are out of scope like any other path and are labelled when they appear.
const FORBIDDEN = [/^\.github\/workflows\/release\.yml$/, /^plugins\/[^/]+\/src\//];

function inScope(p) {
  return EXACT.has(p) || PREFIXES.some((pre) => p.startsWith(pre));
}

/** The out-of-scope paths, in input order, without duplicates. */
function outOfScope(paths) {
  const left = [];
  for (const p of paths) if (!inScope(p) && !left.includes(p)) left.push(p);
  return left;
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
    if (!line.trim() || BASELINE_LINES.has(line)) continue;
    const xy = line.slice(0, 2);
    const rest = line.slice(3);
    const sides = /[RC]/.test(xy) && rest.includes(' -> ') ? rest.split(' -> ') : [rest];
    for (const side of sides) paths.push(unquote(side.trim()));
  }
  return paths;
}

/** Paths of `git diff --name-only` lines: one path per line. */
function diffPaths(text) {
  return text.split(/\r?\n/).filter((l) => l.trim()).map((l) => unquote(l.trim()));
}

function label(p) {
  return FORBIDDEN.some((re) => re.test(p)) ? `${p}   (SC-020: must be unchanged)` : p;
}

function selfTest() {
  const cases = [
    {
      name: 'clean',
      text: [
        ' M .gitattributes',
        ' M plugins/shared/tests/test_update_checker.cpp',
        '?? .github/workflows/feed-tools.yml',
        '?? specs/Krate-manager-roadmap.md',
        '?? specs/krate-manager-phase1/spec.md',
        '?? tools/feed-contract/contract.js',
        '?? tools/gen-feed.js',
        ' M specs/INDEX.md',
        ' M .gitignore',
        '?? tools/feed-contract/package.json',
        '',
      ].join('\n'),
      expect: [],
    },
    {
      name: 'stray',
      text: ' M tools/gen-feed.js\n?? tools/stray.js\n',
      expect: ['tools/stray.js'],
    },
    {
      name: 'rename',
      text: 'R  a -> plugins/iterum/src/x.cpp\n',
      expect: ['a', 'plugins/iterum/src/x.cpp'],
    },
  ];
  let failed = 0;
  for (const c of cases) {
    const left = outOfScope(statusPaths(c.text));
    const exit = left.length ? 1 : 0;
    const wantExit = c.expect.length ? 1 : 0;
    const ok = exit === wantExit && JSON.stringify(left) === JSON.stringify(c.expect);
    if (!ok) failed++;
    console.log(`${ok ? 'ok    ' : 'FAILED'} ${c.name}: left=${JSON.stringify(left)} exit=${exit}` +
      ` (want ${JSON.stringify(c.expect)} exit=${wantExit})`);
    for (const p of left) console.log(`         ${label(p)}`);
  }
  console.log(failed ? `SELF-TEST RED (${failed} of ${cases.length})` : `SELF-TEST GREEN (${cases.length} cases)`);
  return failed ? 1 : 0;
}

function git(args) {
  const r = childProcess.spawnSync('git', args, { cwd: ROOT, encoding: 'utf8', windowsHide: true });
  if (r.error || r.status !== 0) {
    console.log(`scope-check: git ${args.join(' ')} failed: ${r.error ? r.error.message : r.stderr.trim()}`);
    process.exit(2);
  }
  return r.stdout;
}

function main() {
  if (process.argv.includes('--self-test')) process.exit(selfTest());
  const listings = [
    { cmd: ['-c', 'core.quotepath=off', 'status', '--porcelain', '--untracked-files=all'], parse: statusPaths },
    { cmd: ['diff', '--name-only', `${BASE}..`], parse: diffPaths },
  ];
  let total = 0;
  for (const l of listings) {
    const raw = git(l.cmd);
    const left = outOfScope(l.parse(raw));
    total += left.length;
    console.log(`$ git ${l.cmd.join(' ')}`);
    console.log(`--- raw (${raw.split(/\r?\n/).filter(Boolean).length} lines)`);
    if (raw.trim()) process.stdout.write(raw.endsWith('\n') ? raw : `${raw}\n`);
    console.log(`--- filtered (${left.length} out of scope)`);
    for (const p of left) console.log(label(p));
  }
  console.log(total ? `SCOPE RED: ${total} path(s) outside FR-070` : 'SCOPE GREEN');
  process.exit(total ? 1 : 0);
}

main();
