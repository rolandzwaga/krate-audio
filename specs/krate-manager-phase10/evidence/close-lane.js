#!/usr/bin/env node
// Close lane for Krate Manager Phase 10 (L-CLOSE; tasks.md T017, T019 steps 1-6 and 9; spec FR-121, FR-122, SC-026,
// SC-032; plan.md §7.4 steps 1-6, §8; tasks TA-1, TA-11, TA-16). Run by the MAIN LOOP at close, never by an implementer
// or a gate: it holds L-WSL (whose first Phase 10 run compiles the helper crate and every new `cfg(unix)` test cold) and
// L-BUNDLE (the helper's release build plus `tauri build`).
//
// WHY one script: the close rows must come from one run whose steps share a clock and a log directory, with every
// independent step concurrent (CLAUDE.md "Optimise Every Build and Test Run") and every child under a wall-clock
// ceiling, so a hung step reads RED instead of stalling the close. Concurrency is plan §7.4's (plan.md:1098-1100): the
// cargo commands are one chain (they serialise on the build lock anyway), the npm / node commands run beside it, L-WSL
// beside both (WSL's own target dir); L-BUNDLE starts after the cargo chain, because it holds the same build lock; L-REQ
// starts when both chains have written their logs. Adapted from specs/krate-manager-phase9/evidence/close-lane.js
// (Phase 9 took 169.1 s, its timings.md): the rows became plan §7.4's commands, the `tauri build` rows moved into
// bundle-lane.js (TA-16), and T019 step 6's greps were added (rule 9 todo!, SC-026 suppressions, the thread-count flag,
// the M-1 fence, `classify::tests::provisional_rule`, the five FR-123 needles).
//
// WHY the workflow's own step text for two rows (L-GUARD, L-REQ, tasks.md:202, :213): the script extracts that step's
// `run: |` block from .github/workflows/manager-ci.yml, removes its indentation and runs it with Git Bash `bash.exe -c`,
// cwd apps/krate-manager/, env RUNNER_OS=Windows and RUNNER_TEMP=<log dir>, so local and CI logic cannot drift. Every
// other row runs plan §7.4's command itself and writes the files the CI steps write into RUNNER_TEMP (cargo-test.txt,
// cargo-test-ci-roots.txt, vitest.json, node-tap.txt; manager-ci.yml:120, :151, plan.md:1116-1118), which L-REQ reads.
//
// Usage (repo root, Windows dev box; Node only, no dependencies):
//   node specs/krate-manager-phase10/evidence/close-lane.js              # the close (last line GREEN | RED: <rows>)
//   node specs/krate-manager-phase10/evidence/close-lane.js --self-test  # pure checks (the row verdict, the timings
//                                                                         # table writer, the step extractor over the
//                                                                         # real workflow, the scans)
// Rows:
//   A0   rustc --version, node --version, df -h of the repo root only (records)
//   A0b  git status --porcelain --untracked-files=all snapshot, kept for C1
//   A1 (one chain, cwd apps/krate-manager/; plan §7.4 steps 1-2): A1j SC-026 git diff -U0 4a8b3e2a -- apps/krate-manager
//        > evidence/diff-u0.txt plus every untracked app file read whole, scanned for an added lint attribute naming
//        allow( / expect(, @ts-ignore or eslint-disable (expected none); A1k FR-122 no diff and no untracked file under
//        plugins/ dsp/ cmake/ extern/; A1a cargo fmt --all --check; A1b clippy --workspace; A1c clippy dev-feed; A1d
//        clippy helper ci-temp-roots; A1e clippy manual-elevation example (each 0 `warning:` lines); A1f step
//        `clippy-guard` (its ::notice, guard.txt -> evidence/clippy-guard.txt); A1g cargo test --workspace --locked ->
//        cargo-test.txt (evidence copy); A1h cargo test -p krate-manager-helper --features ci-temp-roots --locked ->
//        cargo-test-ci-roots.txt (evidence copy), `test cli::tests::ci_roots_file_is_read ... ok` once (L-HFEAT)
//   A2 (one chain, cwd apps/krate-manager/; step 3): A2a npm ci only if node_modules is absent; A2b npm run typecheck;
//        A2c npm run lint; A2d npm test with the JSON reporter -> vitest.json; A2e L-NAPP node --test
//        --test-reporter=tap "scripts/*.test.js" -> node-tap.txt (# fail 0, TA-1's three node names ok once, no
//        Warning line)
//   A3 (repo root): A3a node --check on the four evidence scripts and the five app scripts; A3b --self-test of
//        scope-check, wsl-lane and bundle-lane; A3c L-NODE node --test --test-reporter=tap
//        "tools/feed-contract/tests/*.test.js" (# fail 0, plan 1..59, no Node warning line); A3d L-TAP on it
//        ("TAP plan=1..59 top-level=59"); A3e L-VLINT
//   A4 L-YAML; A5 L-LINT; A6 L-INDEX (red only because this phase's spec files changed the index -> regenerate once and
//        re-check); A7 L-SCOPE
//   G (T019 step 6): G1 rule 9, no todo!( / unimplemented!( in apps/krate-manager/crates or src-tauri outside *_tests.rs
//        and tests/; G3 no thread-count flag in the app, manager-ci.yml or this phase's evidence scripts (the ban list's
//        own home crates/core/tests/injection.rs exempt: FR088_PATTERNS and its plants, injection.rs:217, :286); G4 the
//        M-1 fence (no tests/fixtures/helper/inno-locked/ entry, no LOCKED_EXIT / LOCKED_LINE word in the app); G5
//        `test classify::tests::provisional_rule ... ok` once in A1g's log; G6 the five FR-123 needles of T002, one
//        line each in the roadmap (A1j is G2)
//   A10 L-WSL: wsl-lane.js --out evidence/wsl-core.txt; last line WSL LANE GREEN and 13/13 Linux-run names (TA-11; an
//        early signal recorded beside the PENDING-CI rows, never instead of them)
//   B1 L-BUNDLE (after A1, only when every A1 row is green): bundle-lane.js; last line BUNDLE LANE GREEN
//   B2 L-REQ (after A1 and A2): step `required tests ran` over A1g / A2d / A2e's files, want
//        "::notice title=required tests ran (Windows)::345 cargo + 57 vitest + 3 node" (328 `all` + 17 `windows`, TA-1)
//   C (last; step 6): C1 git status again, minus lines under specs/krate-manager-phase10/evidence/, equal to A0b; C2
//        L-SCOPE again; C3 node tools/clean-test-output.js --apply (it removes every F:\tmp entry, this run's raw log
//        directory included, so it runs last)
// Raw logs: F:/tmp/gate/km10-close/<timestamp>/ (removed by C3). Summary: evidence/close-lane.log; wall clocks:
// evidence/timings.md. Last line `CLOSE LANE GREEN` exit 0 | `CLOSE LANE RED: <rows>` exit 1. It writes only its logs,
// those evidence files and stdout (and specs/INDEX.md through gen-specs-index.js when A6 regenerates it).
'use strict';
const childProcess = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const APP = path.join(ROOT, 'apps', 'krate-manager');
const STAMP = new Date().toISOString().replace(/[-:]/g, '').replace(/\..*/, '').replace('T', '-');
const EVID = __dirname;
const evid = (name) => path.join(EVID, name);
const rel = (f) => path.relative(ROOT, f).replace(/\\/g, '/');
const SUMMARY = evid('close-lane.log');
const TIMINGS = evid('timings.md');
const WORKFLOW = path.join(ROOT, '.github', 'workflows', 'manager-ci.yml');
const ROADMAP = path.join(ROOT, 'specs', 'Krate-manager-roadmap.md');
const BASH = 'C:/Program Files/Git/bin/bash.exe';
const MIN = 60 * 1000;
const PHASE = 10;
// The scope base (scope-check.js BASE; tasks.md:11-12: written against feat/krate-manager at 4a8b3e2a).
const BASE = '4a8b3e2a';
// Forward slashes: the directory is also RUNNER_TEMP inside Git Bash.
const LOG_DIR = `F:/tmp/gate/km10-close/${STAMP}`;
const EVID_REL = 'specs/krate-manager-phase10/evidence/';
const APP_REL = 'apps/krate-manager/';
const EXPECTED_PLAN = 59; // tasks.md L-TAP; .github/workflows/feed-tools.yml PLAN = "1..59" (plan.md:898-905)
// tasks.md L-REQ (TA-1): cargo.all 328 + cargo.windows 17; vitest 57; node 3.
const REQUIRED_NOTICE = '::notice title=required tests ran (Windows)::345 cargo + 57 vitest + 3 node';
// tasks.md L-HFEAT (FR-103a): the feature-run test, outside required-tests.json (plan.md:925-926).
const CI_ROOTS_TEST = 'cli::tests::ci_roots_file_is_read';
// T019 step 6 (SC-016 provisional rule until the resume R1).
const PROVISIONAL_TEST = 'classify::tests::provisional_rule';
// TA-1 / plan §4.9: the three `node` names of tests/required-tests.json.
const NODE_NAMES = ['bundle sidecar name per host tuple', 'fixture inno script keeps the plugin directives',
  'write test job prints verbatim windows paths'];
// tasks.md A1k (spec FR-122): no file under these changes.
const FR122_PATHS = ['plugins', 'dsp', 'cmake', 'extern'];
// node --check set: this phase's four evidence scripts (T017) and the five app scripts (T015, L-NAPP).
const NODE_CHECK = [
  ...['close-lane', 'scope-check', 'wsl-lane', 'bundle-lane'].map((s) => `${EVID_REL}${s}.js`),
  ...['bundle', 'bundle.test', 'write-test-job', 'write-test-job.test', 'inno-fixture.test'].map((s) => `${APP_REL}scripts/${s}.js`),
];
// The sibling evidence scripts whose --self-test A3b runs (close-lane's own runs before the lane, by hand or at the gate).
const SELF_TESTS = ['scope-check', 'wsl-lane', 'bundle-lane'];
// wsl-lane.js INFO_PREFIX and LINUX_RUN_TESTS (TA-11's 13 names).
const WSL_INFO_ALL = 'Linux-run Phase 9+10 tests seen ok: 13/13';
// The manager-ci.yml steps this script runs (step() refuses any other name; the self-test extracts each one).
const RUN_STEPS = ['clippy-guard', 'required tests ran'];
// T019 step 6: the five FR-123 needles T002 introduces, one token each so a line wrap cannot split one: (a) `CommandEnv`
// (Phase 4 line 477-479), (d) `lipo -create` (the Phase 11 universal-helper sentence, spec FR-123 (d)), (e) `structural`
// (D16's parenthesis after roadmap line 735), (f) `D19` (the residual-risk bullet), (g) `E_HELPER_JOB_LOCATION` (the Phase
// 12 `%LOCALAPPDATA%` line). Each has 0 hits at 4a8b3e2a (counted this session).
const ROADMAP_NEEDLES = ['CommandEnv', 'lipo -create', 'structural', 'D19', 'E_HELPER_JOB_LOCATION'];
// G3: the flag is assembled so this file never holds it (G3 scans this file too).
const THREAD_FLAG = ['--test', 'threads'].join('-');
// G3: the ban list's own home (FR088_PATTERNS and HOST_LITERALS hold the flag as data; its plants hold it in strings).
const THREAD_FLAG_EXEMPT = [`${APP_REL}crates/core/tests/injection.rs`];
// G4: the M-1 fence (SC-032, tasks.md T019 step 6).
const FENCE_DIR = `${APP_REL}tests/fixtures/helper/inno-locked/`;
const FENCE_WORD = /\bLOCKED_(?:EXIT|LINE)\b/;

// ---- pure helpers (covered by --self-test) ----------------------------------------------------------------------
const lines = (text) => text.split(/\r?\n/);
const lastLine = (text) => lines(text).map((l) => l.trim()).filter(Boolean).pop() || '';
// cargo colours nothing into a pipe, but strip ANSI before matching anyway.
const plain = (text) => text.replace(/\x1b\[[0-9;]*m/g, '');
const warningLines = (text) => lines(plain(text)).filter((l) => /^warning:/.test(l)).length;
const notices = (text) => lines(text).filter((l) => /^::(notice|error) /.test(l)).join(' | ');

/** A child result is green when it was not killed and exited 0. */
function green(r) {
  return !r.timedOut && r.code === 0;
}
/** Why a child result is red: TIMEOUT or its exit code. */
function fail(r) {
  return r.timedOut ? `TIMEOUT (killed after ${r.wallS}s)` : `exit=${r.code}`;
}

/**
 * The `run: |` block of manager-ci.yml step `name`, with its indentation removed (a heredoc terminator inside it then
 * sits at column 0, as in the runner's script file). Refuses a step with its own `if:` or `working-directory:`, whose
 * local run would differ from the Windows leg's.
 */
function extractStep(yamlText, name) {
  const all = lines(yamlText);
  const nameRe = /^(\s*)- name: (.*)$/;
  const unquote = (s) => s.trim().replace(/^'(.*)'$/, '$1').replace(/^"(.*)"$/, '$1');
  const at = all.findIndex((l) => { const m = nameRe.exec(l); return m && unquote(m[2]) === name; });
  if (at < 0) throw new Error(`manager-ci.yml has no step "${name}"`);
  const stepIndent = nameRe.exec(all[at])[1].length;
  for (let i = at + 1; i < all.length; i++) {
    const l = all[i];
    if (!l.trim()) continue;
    const ind = l.length - l.trimStart().length;
    if (ind <= stepIndent) break;
    if (/^\s*(if|working-directory):/.test(l)) throw new Error(`step "${name}" sets ${l.trim()}`);
    const m = /^(\s*)run: \|\s*$/.exec(l);
    if (!m) continue;
    const body = [];
    for (let j = i + 1; j < all.length; j++) {
      const b = all[j];
      if (b.trim() && b.length - b.trimStart().length <= m[1].length) break;
      body.push(b);
    }
    while (body.length && !body[body.length - 1].trim()) body.pop();
    const cut = Math.min(...body.filter((b) => b.trim()).map((b) => b.length - b.trimStart().length));
    return `${body.map((b) => b.slice(Math.min(cut, b.length))).join('\n')}\n`;
  }
  throw new Error(`step "${name}" has no "run: |" block`);
}

/**
 * What a suppression an added line holds, or ''. A lint attribute is a line that starts `#[` / `#![` (rustfmt puts
 * every attribute at the start of its line) and names `allow(` or `expect(`; a guard's plant inside a string literal
 * starts with its quote, or ends with a `\` continuation, and is no attribute. `@ts-ignore` and `eslint-disable` count
 * anywhere on the line.
 */
function suppressionKind(content) {
  const t = content.trim();
  if (/^#!?\[/.test(t) && !t.endsWith('\\') && /\b(?:allow|expect)\s*\(/.test(t)) return 'lint attribute';
  if (t.includes('@ts-ignore')) return '@ts-ignore';
  if (t.includes('eslint-disable')) return 'eslint-disable';
  return '';
}

/** SC-026: the added lines of a -U0 diff that add a suppression, as `<file>: <kind>: <line>`. */
function suppressionHits(diffText) {
  const hits = [];
  let file = '?';
  let inHunk = false;
  for (const l of lines(diffText)) {
    if (l.startsWith('diff --git ')) {
      inHunk = false;
      continue;
    }
    if (!inHunk) {
      const m = /^\+\+\+ (?:b\/)?(.*)$/.exec(l);
      if (m) file = m[1];
      if (l.startsWith('@@')) inHunk = true;
      continue;
    }
    if (!l.startsWith('+')) continue;
    const kind = suppressionKind(l.slice(1));
    if (kind) hits.push(`${file}: ${kind}: ${l.slice(1).trim()}`);
  }
  return hits;
}

/** Untracked files as one new-file -U0 diff ([[path, text], ...]), so suppressionHits reads them like the tracked diff. */
function untrackedDiff(entries) {
  return entries.map(([p, text]) => {
    const body = lines(text);
    if (body.length && body[body.length - 1] === '') body.pop();
    return [`diff --git a/${p} b/${p}`, 'new file (untracked)', '--- /dev/null', `+++ b/${p}`,
      `@@ -0,0 +1,${body.length} @@`, ...body.map((l) => `+${l}`)].join('\n');
  }).join('\n');
}

/** G1 (rule 9): `<path>:<line>` of every todo!( / unimplemented!( in a non-test Rust source ([[path, text], ...]). */
function todoHits(entries) {
  const isTest = (p) => p.endsWith('_tests.rs') || /(?:^|\/)tests\//.test(p);
  const hits = [];
  for (const [p, text] of entries) {
    if (!p.endsWith('.rs') || isTest(p)) continue;
    lines(text).forEach((l, i) => { if (/\b(?:todo|unimplemented)!\s*\(/.test(l)) hits.push(`${p}:${i + 1}`); });
  }
  return hits;
}

/** `<path>:<line>` of every line of `entries` (not under an exempt path) that `test(line)` accepts. */
function lineHits(entries, test, exempt = []) {
  const hits = [];
  for (const [p, text] of entries) {
    if (exempt.includes(p)) continue;
    lines(text).forEach((l, i) => { if (test(l)) hits.push(`${p}:${i + 1}`); });
  }
  return hits;
}

/** G3: `<path>:<line>` of every line holding the thread-count flag, the exempt paths skipped ([[path, text], ...]). */
function threadFlagHits(entries, exempt = THREAD_FLAG_EXEMPT) {
  return lineHits(entries, (l) => l.includes(THREAD_FLAG), exempt);
}

/** G4 (SC-032): the M-1 fence. Paths under FENCE_DIR, and `<path>:<line>` of every LOCKED_EXIT / LOCKED_LINE word. */
function fenceHits(paths, entries) {
  return [...paths.filter((p) => p.startsWith(FENCE_DIR)), ...lineHits(entries, (l) => FENCE_WORD.test(l))];
}

/** How many lines read exactly `test <name> ... ok` (cargo's libtest line; G5, L-HFEAT). */
function okCount(log, name) {
  return lines(log).filter((l) => l.trim() === `test ${name} ... ok`).length;
}

/**
 * L-NAPP (PD-27, plan.md:66): how many TAP lines `^ *ok \d+ - (.*?)(?: # .*)?$` name each of `names`, at any depth, and
 * the TAP `# fail` count (NaN when absent).
 */
function tapNames(tap, names) {
  const okNames = lines(tap).map((l) => /^ *ok \d+ - (.*?)(?: # .*)?$/.exec(l)).filter(Boolean).map((m) => m[1]);
  const failLine = /^# fail (\d+)\s*$/m.exec(tap.replace(/\r/g, ''));
  return { counts: names.map((n) => [n, okNames.filter((x) => x === n).length]), fail: failLine ? Number(failLine[1]) : NaN };
}

/** G6: how many lines of the roadmap hold each needle (line-based, as `rg -n` counts); GREEN when each is on one line. */
function needleVerdict(text, needles = ROADMAP_NEEDLES) {
  const all = lines(text);
  const counts = needles.map((n) => [n, all.filter((l) => l.includes(n)).length]);
  const ok = counts.length > 0 && counts.every(([, c]) => c === 1);
  return { ok, detail: counts.map(([n, c]) => `"${n}" ${c} hit(s)`).join('; ') || 'no needles' };
}

/** git status --porcelain lines, minus this phase's evidence outputs (C1). */
function withoutEvidence(statusText) {
  return lines(statusText).filter((l) => l && !l.slice(3).replace(/^"/, '').startsWith(EVID_REL)).join('\n');
}

/** Lines present in one text and not the other (multiset, line endings ignored). */
function changedLines(before, after) {
  const count = new Map();
  for (const l of lines(before)) count.set(l, (count.get(l) || 0) + 1);
  for (const l of lines(after)) count.set(l, (count.get(l) || 0) - 1);
  return [...count].filter(([, n]) => n !== 0).map(([l]) => l);
}

/** The last line of the run: GREEN, or RED naming every red row id. */
function verdictLine(rows) {
  if (rows.length === 0) return 'CLOSE LANE RED: no rows';
  const red = rows.filter((r) => !r.ok).map((r) => r.id);
  return red.length ? `CLOSE LANE RED: ${red.join(', ')}` : 'CLOSE LANE GREEN';
}

/**
 * The timings table (evidence/timings.md): one row per result, `| id | GREEN/RED | wall | command |`, the command text
 * cut at its ` wall=` suffix and its pipes escaped, so the table stays one row per result.
 */
function timingsMarkdown({ rows, totalS, verdict, logDir, when }) {
  const cell = (s) => s.replace(/\r?\n/g, ' ').replace(/\|/g, '\\|');
  const body = rows.map((r) => `| ${r.id} | ${r.ok ? 'GREEN' : 'RED'} | ${r.wallS || '-'} | ${cell(r.text.split(' wall=')[0])} |`);
  return [
    `# Krate Manager Phase ${PHASE}: close-lane wall clocks`,
    '',
    `Written by \`evidence/close-lane.js\`, ${when}. Total wall ${totalS}s; ${verdict}. Raw logs (removed by C3): \`${logDir}\`.`,
    'A1 (git scans, then cargo) and A2 (npm / node) run as two chains beside A3-A7 and A10 (L-WSL, on WSL\'s own target' +
      ' dir); G runs first (file scans) except G5, which reads A1g\'s log; B1 (L-BUNDLE) starts after A1 (the cargo' +
      ' build lock), B2 (L-REQ) after A1 and A2; C runs last.',
    '',
    '| Row | Result | Wall (s) | Command |',
    '|---|---|---|---|',
    ...body,
    '',
  ].join('\n');
}

function selfTest() {
  const failures = [];
  let cases = 0;
  const check = (name, fn) => { cases++; try { fn(); } catch (e) { failures.push(`${name}: ${e.message}`); } };
  const eq = (got, want) => { if (got !== want) throw new Error(`got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`); };
  const has = (text, part) => { if (!text.includes(part)) throw new Error(`${JSON.stringify(part)} not in ${JSON.stringify(text)}`); };
  const throws = (fn, re) => {
    let msg = null;
    try { fn(); } catch (e) { msg = e.message; }
    if (msg === null || !re.test(msg)) throw new Error(`did not throw ${re} (got ${JSON.stringify(msg)})`);
  };

  // The row runner's verdict.
  check('green: exit 0, not killed', () => eq(green({ code: 0, timedOut: false }), true));
  check('green: exit 1 is red', () => eq(green({ code: 1, timedOut: false }), false));
  check('green: killed at the ceiling is red even with exit 0', () => eq(green({ code: 0, timedOut: true }), false));
  check('green: spawn failure (-1) is red', () => eq(green({ code: -1, timedOut: false }), false));
  check('fail names the exit code', () => eq(fail({ code: 101, timedOut: false, wallS: '3.0' }), 'exit=101'));
  check('fail names the timeout', () => eq(fail({ code: -1, timedOut: true, wallS: '1200.0' }), 'TIMEOUT (killed after 1200.0s)'));
  check('verdict line green', () => eq(verdictLine([{ id: 'A0', ok: true }, { id: 'B1', ok: true }]), 'CLOSE LANE GREEN'));
  check('verdict line names the red rows', () =>
    eq(verdictLine([{ id: 'A0', ok: true }, { id: 'A1g', ok: false }, { id: 'B2', ok: false }]), 'CLOSE LANE RED: A1g, B2'));
  check('verdict line with no rows is red', () => eq(verdictLine([]), 'CLOSE LANE RED: no rows'));

  // The timings table writer.
  const table = timingsMarkdown({
    rows: [{ id: 'A1g', ok: true, wallS: '41.2', text: 'cargo test --workspace --locked passed=345 wall=41.2s log=x' },
      { id: 'B2', ok: false, wallS: '', text: 'step required tests ran notices="a | b"' }],
    totalS: '300.5', verdict: 'CLOSE LANE RED: B2', logDir: 'F:/tmp/gate/km10-close/x', when: '2026-11-01T12:00:00.000Z',
  });
  const tl = lines(table);
  check('timings: phase 10 title', () => eq(tl[0], '# Krate Manager Phase 10: close-lane wall clocks'));
  check('timings: header row and rule', () => {
    has(table, '\n| Row | Result | Wall (s) | Command |\n|---|---|---|---|\n');
  });
  check('timings: a green row, command cut at wall=', () =>
    has(table, '\n| A1g | GREEN | 41.2 | cargo test --workspace --locked passed=345 |\n'));
  check('timings: a red row with no wall clock, pipes escaped', () =>
    has(table, '\n| B2 | RED | - | step required tests ran notices="a \\| b" |\n'));
  check('timings: total, verdict and log dir in the preamble', () => {
    has(table, 'Total wall 300.5s; CLOSE LANE RED: B2.');
    has(table, '`F:/tmp/gate/km10-close/x`');
    has(table, '2026-11-01T12:00:00.000Z');
  });
  check('timings: one table row per result', () => eq(tl.filter((l) => /^\| (A1g|B2) \|/.test(l)).length, 2));
  check('timings: ends with a newline', () => eq(table.endsWith('|\n'), true));

  // Step extraction (synthetic, then the real workflow for every step run).
  const yaml = ['jobs:', '  check:', '    steps:', '      - name: first', '        run: |', '          echo one',
    "      - name: 'second'", '        run: |', '          set -euo pipefail', "          node - <<'X'",
    '          const a = 1;', '            nested', '          X', '', '      - name: third', '        run: echo three',
    '      - name: fourth', "        if: runner.os == 'macOS'", '        run: |', '          echo four', ''].join('\n');
  const second = "set -euo pipefail\nnode - <<'X'\nconst a = 1;\n  nested\nX\n";
  check('second step de-indented', () => eq(extractStep(yaml, 'second'), second));
  check('missing step throws', () => throws(() => extractStep(yaml, 'missing'), /no step "missing"/));
  check('CRLF yaml gives the same block', () => eq(extractStep(yaml.replace(/\n/g, '\r\n'), 'second'), second));
  check('first step stops at the next step', () => eq(extractStep(yaml, 'first'), 'echo one\n'));
  check('one-line run refused', () => throws(() => extractStep(yaml, 'third'), /has no "run: \|" block/));
  check('step with if: refused', () => throws(() => extractStep(yaml, 'fourth'), /sets if:/));
  const real = fs.readFileSync(WORKFLOW, 'utf8');
  for (const name of RUN_STEPS) {
    check(`real step ${name} extracts`, () => {
      const s = extractStep(real, name);
      eq(s.trim() !== '' && !s.includes('\r') && !/^\s/.test(s), true);
    });
  }
  check('real step required tests ran reads the files this lane writes', () => {
    const s = extractStep(real, 'required tests ran');
    has(s, '"$RUNNER_TEMP/cargo-test.txt"');
    has(s, '"$RUNNER_TEMP/vitest.json"');
    has(s, '::notice title=required tests ran (${runnerOs})::');
    eq(s.endsWith('\nREQUIRED\n'), true); // the heredoc terminator at column 0
  });
  check('real step clippy-guard prints the notice A1f reads', () =>
    has(extractStep(real, 'clippy-guard'), 'echo "::notice title=clippy-guard ($RUNNER_OS)::$msg"'));

  // The constants this phase changes, spelled here so a stale Phase 9 value reads red.
  check('phase is 10', () => eq(PHASE, 10));
  check('base is 4a8b3e2a', () => eq(BASE, '4a8b3e2a'));
  check('required notice 345 cargo (328 all + 17 windows) + 57 vitest + 3 node (TA-1)', () =>
    eq(REQUIRED_NOTICE, '::notice title=required tests ran (Windows)::345 cargo + 57 vitest + 3 node'));
  check('the WSL all-seen line is TA-11\'s 13/13', () => eq(WSL_INFO_ALL, 'Linux-run Phase 9+10 tests seen ok: 13/13'));
  check('raw log root', () => eq(LOG_DIR.startsWith('F:/tmp/gate/km10-close/'), true));
  check('evidence prefix is phase 10', () => eq(EVID_REL, 'specs/krate-manager-phase10/evidence/'));
  check('FR-122 paths', () => eq(FR122_PATHS.join(','), 'plugins,dsp,cmake,extern'));
  check('node --check set: four evidence + five app scripts', () => eq(NODE_CHECK.length, 9));
  check('the thread-count flag is assembled, never spelled', () => {
    eq(THREAD_FLAG, `--test${'-'}threads`);
    eq(fs.readFileSync(__filename, 'utf8').includes(THREAD_FLAG), false);
  });
  check('the three node names (TA-1) equal tests/required-tests.json', () => {
    const req = JSON.parse(fs.readFileSync(path.join(APP, 'tests', 'required-tests.json'), 'utf8'));
    eq(JSON.stringify(req.node), JSON.stringify(NODE_NAMES));
  });

  // SC-026: the suppression scan.
  const diff = (...added) => [
    'diff --git a/apps/krate-manager/crates/helper/src/ops.rs b/apps/krate-manager/crates/helper/src/ops.rs',
    'index 1111111..2222222 100644', '--- a/apps/krate-manager/crates/helper/src/ops.rs',
    '+++ b/apps/krate-manager/crates/helper/src/ops.rs', '@@ -10 +10,0 @@ fn x', '-#[allow(dead_code)]', '@@ -20,0 +20,2 @@ fn y',
    ...added.map((a) => `+${a}`),
  ].join('\n');
  check('added #[expect(clippy::x, reason = "y")] -> hit', () =>
    eq(suppressionHits(diff('#[expect(clippy::x, reason = "y")]', 'fn planted() {}')).length, 1));
  check('added "// see allow(" comment -> none', () => eq(suppressionHits(diff('    // see allow( in the docs')).length, 0));
  check('added // @ts-ignore -> hit', () => eq(suppressionHits(diff('// @ts-ignore')).length, 1));
  check('added eslint-disable-next-line -> hit', () => eq(suppressionHits(diff('// eslint-disable-next-line x')).length, 1));
  check('added #[cfg_attr(test, allow(...))] -> hit', () => eq(suppressionHits(diff('    #[cfg_attr(test, allow(x))]')).length, 1));
  check('added guard plant in a string literal -> none', () =>
    eq(suppressionHits(diff('            "#[expect(clippy::all, reason = \\"x\\")]\\nfn planted() {}\\n",')).length, 0));
  check('removed #[allow(dead_code)] only -> none', () => eq(suppressionHits(diff()).length, 0));
  check('untracked file read whole -> its #[allow( reported', () => {
    const hits = suppressionHits(untrackedDiff([['apps/krate-manager/crates/helper/src/os.rs',
      'fn a() {}\r\n#[allow(dead_code)]\r\nfn b() {}\r\n'], ['apps/krate-manager/crates/helper/src/job.rs', '// allow( here\n']]));
    eq(hits.length, 1);
    has(hits[0], 'apps/krate-manager/crates/helper/src/os.rs: ');
  });

  // G1 (rule 9).
  const rs = (p, text) => [p, text];
  const todoSet = [
    rs('apps/krate-manager/crates/helper/src/ops.rs', 'fn a() {\n    todo!("T006")\n}\n'),
    rs('apps/krate-manager/crates/helper/src/ops_tests.rs', 'fn t() { todo!() }\n'),
    rs('apps/krate-manager/crates/helper/tests/usage.rs', 'fn t() { unimplemented!() }\n'),
    rs('apps/krate-manager/crates/core/tests/bin/argv_echo.rs', 'fn main() { todo!() }\n'),
    rs('apps/krate-manager/src-tauri/examples/manual_elevation.rs', 'fn main() {\r\n    unimplemented!("x")\r\n}\r\n'),
    rs('apps/krate-manager/src-tauri/src/lib.rs', '// a todo list, not a todo macro\nfn f() {}\n'),
    rs('apps/krate-manager/scripts/bundle.js', 'todo!(\n'),
  ];
  check('G1 finds the non-test todo!( and unimplemented!( only', () =>
    eq(todoHits(todoSet).join(' ; '), 'apps/krate-manager/crates/helper/src/ops.rs:2 ;' +
      ' apps/krate-manager/src-tauri/examples/manual_elevation.rs:2'));
  check('G1 over clean sources -> none', () => eq(todoHits([todoSet[5]]).length, 0));

  // G3: the thread-count flag.
  const flagSet = [
    rs('apps/krate-manager/crates/core/tests/injection.rs', `    "${THREAD_FLAG}",\n`),
    rs('apps/krate-manager/package.json', `{ "test": "vitest run" }\n`),
    rs('.github/workflows/manager-ci.yml', `      cargo test -- ${THREAD_FLAG}=1\n`),
    rs('specs/krate-manager-phase10/evidence/x.js', `\n// ${THREAD_FLAG}\n`),
  ];
  check('G3 skips the ban list home, reports the workflow and a script', () =>
    eq(threadFlagHits(flagSet).join(' ; '), '.github/workflows/manager-ci.yml:1 ; specs/krate-manager-phase10/evidence/x.js:2'));
  check('G3 with no exemption reports the home too', () => eq(threadFlagHits(flagSet, []).length, 3));

  // G4: the M-1 fence.
  const fenceText = [rs('apps/krate-manager/crates/core/src/context.rs', 'pub const LOCKED_EXIT_DELAY: Duration = x;\n'),
    rs('apps/krate-manager/crates/helper/src/classify.rs', 'const PROVISIONAL: i32 = 1;\n')];
  check('G4: LOCKED_EXIT_DELAY is not the fenced constant', () =>
    eq(fenceHits(['apps/krate-manager/tests/fixtures/helper/.gitkeep'], fenceText).length, 0));
  check('G4: a LOCKED_LINE constant is fenced', () => eq(fenceHits([], [rs('apps/krate-manager/crates/helper/src/classify.rs',
    'x\npub const LOCKED_LINE: &str = "y";\n')]).join(), 'apps/krate-manager/crates/helper/src/classify.rs:2'));
  check('G4: a LOCKED_EXIT constant is fenced', () => eq(fenceHits([], [rs('a.rs', 'const LOCKED_EXIT: i32 = 5;')]).length, 1));
  check('G4: an inno-locked/ fixture is fenced', () => eq(fenceHits(
    ['apps/krate-manager/tests/fixtures/helper/inno-locked/exit_code.txt'], []).join(),
  'apps/krate-manager/tests/fixtures/helper/inno-locked/exit_code.txt'));

  // G5 / L-HFEAT: the libtest ok line.
  const cargoLog = ['running 3 tests', `test ${PROVISIONAL_TEST} ... ok`, `test ${PROVISIONAL_TEST}_twin ... ok`,
    `test ${CI_ROOTS_TEST} ... FAILED`, 'test result: FAILED. 2 passed; 1 failed'].join('\r\n');
  check('okCount: the exact name once', () => eq(okCount(cargoLog, PROVISIONAL_TEST), 1));
  check('okCount: FAILED is not ok', () => eq(okCount(cargoLog, CI_ROOTS_TEST), 0));
  check('okCount: twice in two binaries -> 2', () => eq(okCount(`${cargoLog}\ntest ${PROVISIONAL_TEST} ... ok\n`, PROVISIONAL_TEST), 2));

  // L-NAPP (PD-27's lenient regex).
  const tap = ['TAP version 13', `# Subtest: ${NODE_NAMES[0]}`, `ok 1 - ${NODE_NAMES[0]}`, '  ---', '  ...',
    `    ok 1 - ${NODE_NAMES[1]} # TODO later`, `ok 2 - ${NODE_NAMES[1]}`, `not ok 3 - ${NODE_NAMES[2]}`, '1..3',
    '# tests 3', '# pass 2', '# fail 1'].join('\n');
  const t = tapNames(tap, NODE_NAMES);
  check('tapNames: a top-level ok counts', () => eq(t.counts[0][1], 1));
  check('tapNames: an indented ok with a directive counts at any depth', () => eq(t.counts[1][1], 2));
  check('tapNames: not ok does not count', () => eq(t.counts[2][1], 0));
  check('tapNames: the # fail count', () => eq(t.fail, 1));
  check('tapNames: no # fail line -> NaN', () => eq(Number.isNaN(tapNames('ok 1 - x', ['x']).fail), true));

  // G6: the five FR-123 needles, one line each (rg -n semantics).
  const roadmapText = ['# Roadmap', 'x joins `CommandEnv` beside y', 'stages (`lipo -create` of both)', 'is structural:',
    'accepted (Phase 10 D19)', 'refused `E_HELPER_JOB_LOCATION`, D17', ''].join('\r\n');
  check('needles: one hit each (CRLF) -> GREEN', () => eq(needleVerdict(roadmapText).ok, true));
  check('needles: one missing -> RED', () => eq(needleVerdict(roadmapText.replace('D19', 'D18')).ok, false));
  check('needles: one on two lines -> RED', () => eq(needleVerdict(`${roadmapText}\r\nagain CommandEnv\r\n`).ok, false));
  check('needles: the detail names every needle', () => eq(needleVerdict(roadmapText).detail.split('; ').length, 5));
  check('needles: none at the base roadmap', () => {
    const r = childProcess.spawnSync('git', ['show', `${BASE}:specs/Krate-manager-roadmap.md`],
      { cwd: ROOT, encoding: 'utf8', windowsHide: true, maxBuffer: 1 << 26 });
    eq(r.status, 0);
    eq(needleVerdict(r.stdout).detail, ROADMAP_NEEDLES.map((n) => `"${n}" 0 hit(s)`).join('; '));
  });

  // A10: this phase's wsl-lane.js, whose all-seen information line A10 wants (TA-11's 13 names).
  check('A10 wants the line wsl-lane.js prints for 13/13', () => {
    const wsl = require(path.join(EVID, 'wsl-lane.js'));
    const log = [...wsl.LINUX_RUN_TESTS.map((n) => `test ${n} ... ok`), 'test result: ok. 13 passed; 0 failed'].join('\n');
    eq(wsl.infoLine(wsl.verdict(log, 0).seen), WSL_INFO_ALL);
    eq(WSL_INFO_ALL.startsWith(wsl.INFO_PREFIX), true);
  });

  check('status filter drops evidence only', () => eq(withoutEvidence(
    ` M specs/INDEX.md\n?? ${EVID_REL}timings.md\n?? specs/krate-manager-phase10/plan.md\n`),
  ' M specs/INDEX.md\n?? specs/krate-manager-phase10/plan.md'));
  check('changed lines', () => eq(changedLines('a\r\nb\r\nc', 'a\nc\nd').sort().join(','), 'b,d'));
  check('warning lines counted after ANSI strip', () => eq(warningLines('\x1b[33mwarning\x1b[0m: x\nnote: warning: y\n'), 1));

  for (const f of failures) console.log(`self-test FAIL ${f}`);
  console.log(failures.length ? `CLOSE LANE SELF-TEST RED (${failures.length} of ${cases} failed)`
    : `CLOSE LANE SELF-TEST GREEN (${cases} cases)`);
  return failures.length ? 1 : 0;
}

// ---- processes --------------------------------------------------------------------------------------------------
const results = []; // { id, ok, text, wallS }
function record(id, ok, text, wallS = '') {
  results.push({ id, ok, text, wallS });
  console.log(`${id.padEnd(5)} ${ok ? 'GREEN' : 'RED  '} ${text}`);
}

/** Kills a child and its descendants (bash -> cargo -> rustc, cmd -> npm -> node, node -> wsl). */
function killTree(child) {
  if (process.platform === 'win32') {
    childProcess.spawnSync('taskkill', ['/PID', String(child.pid), '/T', '/F'], { windowsHide: true });
  } else {
    child.kill('SIGKILL');
  }
}

/**
 * Runs one command under a wall-clock ceiling, streams stdout+stderr to <LOG_DIR>/<log>, and resolves
 * { code, timedOut, wallS, out, stdout, err, log }. `stdinFile` feeds a file to stdin; `shell` is only for npx / npm (a
 * .cmd on Windows: then `cmd` is one fixed string and `args` must be empty); `env` replaces the environment; `cwd`
 * defaults to the repo root.
 */
function run(cmd, args, { log, ceilingMs, stdinFile, shell = false, env, cwd = ROOT }) {
  return new Promise((resolve) => {
    const logPath = `${LOG_DIR}/${log}`;
    const sink = fs.createWriteStream(logPath);
    const stdin = stdinFile ? fs.openSync(path.join(ROOT, stdinFile), 'r') : 'ignore';
    const t0 = Date.now();
    const chunks = [];
    const outChunks = [];
    const errChunks = [];
    let timedOut = false;
    let child;
    let timer;
    const finish = (code) => {
      clearTimeout(timer);
      if (typeof stdin === 'number') fs.closeSync(stdin);
      sink.end(() => resolve({
        code, timedOut, wallS: ((Date.now() - t0) / 1000).toFixed(1), out: chunks.join(''), stdout: outChunks.join(''),
        err: errChunks.join(''), log: logPath,
      }));
    };
    sink.write(`$ (cwd ${path.relative(ROOT, cwd) || '.'}) ${cmd} ${args.join(' ')}${stdinFile ? ` < ${stdinFile}` : ''}\n`);
    try {
      const spawnOpts = { cwd, shell, windowsHide: true, stdio: [stdin, 'pipe', 'pipe'] };
      if (env) spawnOpts.env = env;
      // A shell command is one fixed string with no args array (Node DEP0190 warns on args with shell: true).
      child = shell ? childProcess.spawn(cmd, spawnOpts) : childProcess.spawn(cmd, args, spawnOpts);
    } catch (e) {
      chunks.push(String(e.message));
      return finish(-1);
    }
    timer = setTimeout(() => { timedOut = true; killTree(child); }, ceilingMs);
    child.stdout.on('data', (d) => { chunks.push(d.toString()); outChunks.push(d.toString()); sink.write(d); });
    child.stderr.on('data', (d) => { chunks.push(d.toString()); errChunks.push(d.toString()); sink.write(d); });
    child.on('error', (e) => { chunks.push(`spawn error: ${e.message}\n`); });
    child.on('close', (code) => finish(code === null ? -1 : code));
    return undefined;
  });
}

const node = (script, args, opts) => run(process.execPath, [script, ...args], opts);
const cargo = (args, opts) => run('cargo', args, { cwd: APP, ...opts });
const npm = (command, opts) => run(command, [], { cwd: APP, shell: true, ...opts });
const git = (args, opts) => run('git', ['-c', 'core.quotepath=off', ...args], opts);
const stepLabel = (r) => `${fail(r)} wall=${r.wallS}s log=${r.log}`;

let yamlText = '';
/** Runs manager-ci.yml step `name` (one of RUN_STEPS) with Git Bash from apps/krate-manager/. */
function step(name, log, ceilingMs) {
  let script;
  try {
    if (!RUN_STEPS.includes(name)) throw new Error(`step "${name}" is not in RUN_STEPS`);
    script = extractStep(yamlText, name);
  } catch (e) {
    return Promise.resolve({ code: -1, timedOut: false, wallS: '0.0', out: e.message, stdout: '', err: e.message, log: '-' });
  }
  fs.writeFileSync(`${LOG_DIR}/${log}.sh`, script);
  const env = { ...process.env, RUNNER_OS: 'Windows', RUNNER_TEMP: LOG_DIR };
  return run(BASH, ['-c', script], { log, ceilingMs, env, cwd: APP });
}

function gitSync(args) {
  const r = childProcess.spawnSync('git', ['-c', 'core.quotepath=off', ...args],
    { cwd: ROOT, encoding: 'utf8', windowsHide: true, maxBuffer: 1 << 28 });
  return r.status === 0 ? r.stdout : null;
}

function gitStatus() {
  const out = gitSync(['status', '--porcelain', '--untracked-files=all']);
  return out === null ? 'git status failed' : withoutEvidence(out);
}

/** Text files (tracked and untracked, not ignored) under `pathspecs`, as [[repo-relative path, text], ...]. */
function readTextFiles(pathspecs) {
  const listing = gitSync(['ls-files', '-co', '--exclude-standard', '--', ...pathspecs]);
  if (listing === null) return { entries: [], paths: [], error: `git ls-files ${pathspecs.join(' ')} failed` };
  const paths = [...new Set(lines(listing).map((l) => l.trim()).filter(Boolean))];
  const entries = [];
  for (const p of paths) {
    const abs = path.join(ROOT, p);
    if (!fs.existsSync(abs)) continue; // deleted in the work tree (crates/helper/tests/stub.rs)
    const text = fs.readFileSync(abs, 'utf8');
    if (!text.includes('\0')) entries.push([p, text]);
  }
  return { entries, paths, error: '' };
}

const today = () => new Date().toISOString().slice(0, 10);

// ---- A0: records ------------------------------------------------------------------------------------------------
async function a0Records() {
  const rc = await run('rustc', ['--version'], { log: 'A0-rustc.log', ceilingMs: 2 * MIN });
  const nv = await run(process.execPath, ['--version'], { log: 'A0-node.log', ceilingMs: 2 * MIN });
  const df = await run(BASH, ['-c', 'df -h .'], { log: 'A0-df.log', ceilingMs: 2 * MIN });
  const dfRow = lastLine(df.out).replace(/\s+/g, ' ');
  record('A0', green(rc) && green(nv) && green(df),
    `rustc="${lastLine(rc.out)}" node="${lastLine(nv.out)}" df(repo root)="${dfRow}"`, rc.wallS);
}

// ---- A1: the cargo chain (A1j / A1k first: git only, seconds) ---------------------------------------------------
async function a1jSuppressions() {
  const t0 = Date.now();
  const d = await git(['diff', '--no-color', '--no-ext-diff', '-U0', BASE, '--', 'apps/krate-manager'],
    { log: 'A1j-diff-u0.log', ceilingMs: 2 * MIN });
  fs.writeFileSync(evid('diff-u0.txt'), d.stdout);
  const u = await git(['ls-files', '--others', '--exclude-standard', '--', 'apps/krate-manager'],
    { log: 'A1j-untracked.log', ceilingMs: 2 * MIN });
  const untracked = lines(u.stdout.trim()).filter(Boolean);
  const unread = [];
  const entries = [];
  let readError = '';
  for (const p of untracked) {
    let text;
    try {
      text = fs.readFileSync(path.join(ROOT, p), 'utf8');
    } catch (e) {
      readError = readError || `${p}: ${e.message}`;
      continue;
    }
    if (text.includes('\0')) unread.push(p); // binary: no suppression can live in it
    else entries.push([p, text]);
  }
  const synthetic = untrackedDiff(entries);
  fs.writeFileSync(`${LOG_DIR}/A1j-untracked-as-diff.txt`, synthetic);
  const hits = [...suppressionHits(d.stdout), ...suppressionHits(synthetic)];
  const wallS = ((Date.now() - t0) / 1000).toFixed(1);
  record('A1j', green(d) && green(u) && !readError && hits.length === 0, `G2 / SC-026 git diff -U0 ${BASE} --` +
    ` apps/krate-manager -> ${EVID_REL}diff-u0.txt (${lines(d.stdout).length} lines) plus ${entries.length} untracked` +
    ` file(s) read whole${unread.length ? ` (${unread.length} binary skipped: ${unread.join(', ')})` : ''}` +
    `${readError ? ` (UNREADABLE ${readError})` : ''}: ` +
    `${hits.length ? `${hits.length} suppression(s) ADDED: ${hits.slice(0, 10).join(' ; ')}` : 'no added allow( / expect( attribute,' +
      ' @ts-ignore or eslint-disable'} ${fail(d)}/${fail(u)} wall=${wallS}s`, wallS);
}

async function a1kNoRepoChange() {
  const t0 = Date.now();
  const d = await git(['diff', '--no-color', '--name-only', BASE, '--', ...FR122_PATHS], { log: 'A1k-fr122-diff.log', ceilingMs: 2 * MIN });
  const u = await git(['ls-files', '--others', '--exclude-standard', '--', ...FR122_PATHS],
    { log: 'A1k-fr122-untracked.log', ceilingMs: 2 * MIN });
  const changed = lines(d.stdout.trim()).filter(Boolean);
  const added = lines(u.stdout.trim()).filter(Boolean);
  const wallS = ((Date.now() - t0) / 1000).toFixed(1);
  record('A1k', green(d) && green(u) && changed.length === 0 && added.length === 0, `FR-122 git diff --name-only ${BASE} --` +
    ` ${FR122_PATHS.join(' ')}: ${changed.length ? changed.join(', ') : 'empty'}; untracked under them:` +
    ` ${added.length ? added.join(', ') : 'empty'} ${fail(d)}/${fail(u)} wall=${wallS}s`, wallS);
}

/** A cargo test run whose combined output is also the RUNNER_TEMP file a CI step would tee (and its evidence copy). */
async function cargoTest(id, args, file) {
  const r = await cargo(args, { log: `${id}-cargo-test.log`, ceilingMs: 25 * MIN });
  fs.writeFileSync(`${LOG_DIR}/${file}`, r.out);
  fs.writeFileSync(evid(file), r.out);
  const summaries = lines(r.out).filter((l) => /^test result: /.test(l));
  const failedBins = summaries.filter((l) => !/ 0 failed;/.test(l)).length;
  const passed = summaries.reduce((n, l) => n + Number((/ (\d+) passed;/.exec(l) || [0, 0])[1]), 0);
  return { r, summaries, failedBins, passed, ok: green(r) && summaries.length > 0 && failedBins === 0 };
}

async function a1Cargo() {
  await a1jSuppressions();
  await a1kNoRepoChange();

  let r = await cargo(['fmt', '--all', '--check'], { log: 'A1a-cargo-fmt.log', ceilingMs: 5 * MIN });
  record('A1a', green(r), `cargo fmt --all --check last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);

  const clippies = [
    ['A1b', ['clippy', '--workspace', '--all-targets', '--locked', '--', '-D', 'warnings']],
    ['A1c', ['clippy', '-p', 'krate-manager', '--all-targets', '--features', 'dev-feed', '--locked', '--', '-D', 'warnings']],
    ['A1d', ['clippy', '-p', 'krate-manager-helper', '--all-targets', '--features', 'ci-temp-roots', '--locked', '--', '-D', 'warnings']],
    ['A1e', ['clippy', '-p', 'krate-manager', '--example', 'manual_elevation', '--features', 'manual-elevation', '--locked', '--',
      '-D', 'warnings']],
  ];
  for (const [id, args] of clippies) {
    r = await cargo(args, { log: `${id}-clippy.log`, ceilingMs: 20 * MIN });
    const w = warningLines(r.out);
    record(id, green(r) && w === 0, `cargo ${args.join(' ')} warning-lines=${w} ${stepLabel(r)}`, r.wallS);
  }

  r = await step('clippy-guard', 'A1f-clippy-guard.log', 15 * MIN);
  const guardNotice = lines(r.out).find((l) => l.startsWith('::notice title=clippy-guard (Windows)::')) || '';
  const guardTxt = `${LOG_DIR}/guard.txt`;
  let copied = 'guard.txt missing';
  if (fs.existsSync(guardTxt)) {
    const header = `# ${today()} (Windows, Phase ${PHASE} close lane A1f, from apps/krate-manager/, RUNNER_TEMP=${LOG_DIR}):` +
      ` manager-ci.yml step clippy-guard, its own run: text (${guardNotice || 'no notice'})\n`;
    fs.writeFileSync(evid('clippy-guard.txt'), header + fs.readFileSync(guardTxt, 'utf8'));
    copied = `copied to ${EVID_REL}clippy-guard.txt`;
  }
  record('A1f', green(r) && guardNotice !== '', `L-GUARD step clippy-guard notice="${guardNotice || notices(r.out)}"` +
    ` ${copied} ${stepLabel(r)}`, r.wallS);

  const t = await cargoTest('A1g', ['test', '--workspace', '--locked'], 'cargo-test.txt');
  record('A1g', t.ok, `cargo test --workspace --locked binaries=${t.summaries.length} passed=${t.passed}` +
    ` binaries-with-failures=${t.failedBins} -> ${EVID_REL}cargo-test.txt ${stepLabel(t.r)}`, t.r.wallS);
  const provisional = okCount(t.r.out, PROVISIONAL_TEST);
  record('G5', provisional === 1, `T019 step 6: "test ${PROVISIONAL_TEST} ... ok" ${provisional} time(s) in A1g's log (want 1)`);

  const h = await cargoTest('A1h', ['test', '-p', 'krate-manager-helper', '--features', 'ci-temp-roots', '--locked'],
    'cargo-test-ci-roots.txt');
  const ciRoots = okCount(h.r.out, CI_ROOTS_TEST);
  record('A1h', h.ok && ciRoots === 1, `L-HFEAT cargo test -p krate-manager-helper --features ci-temp-roots --locked` +
    ` binaries=${h.summaries.length} passed=${h.passed} binaries-with-failures=${h.failedBins}` +
    ` "test ${CI_ROOTS_TEST} ... ok" x${ciRoots} (want 1) -> ${EVID_REL}cargo-test-ci-roots.txt ${stepLabel(h.r)}`, h.r.wallS);
}

// ---- A2: the npm / node chain -----------------------------------------------------------------------------------
async function a2Npm() {
  if (fs.existsSync(path.join(APP, 'node_modules'))) {
    record('A2a', true, 'node_modules present: npm ci not run');
  } else {
    const r = await npm('npm ci', { log: 'A2a-npm-ci.log', ceilingMs: 10 * MIN });
    record('A2a', green(r), `npm ci last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
  }
  for (const [id, command, ceiling] of [['A2b', 'npm run typecheck', 5], ['A2c', 'npm run lint', 5],
    ['A2d', `npm test -- --reporter=default --reporter=json --outputFile.json=${LOG_DIR}/vitest.json`, 10]]) {
    const r = await npm(command, { log: `${id}.log`, ceilingMs: ceiling * MIN });
    record(id, green(r), `${command} last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
  }
  // L-NAPP: quoted glob, expanded by Node; files in parallel child processes (node --test's default).
  const r = await run(process.execPath, ['--test', '--test-reporter=tap', 'scripts/*.test.js'],
    { log: 'A2e-node-test-scripts.log', ceilingMs: 5 * MIN, cwd: APP });
  fs.writeFileSync(`${LOG_DIR}/node-tap.txt`, r.stdout);
  const t = tapNames(r.stdout, NODE_NAMES);
  const bad = t.counts.filter(([, c]) => c !== 1);
  const warn = lines(r.out).filter((l) => /Warning:/.test(l)).length;
  record('A2e', green(r) && t.fail === 0 && bad.length === 0 && warn === 0, `L-NAPP node --test --test-reporter=tap` +
    ` "scripts/*.test.js" fail=${t.fail} ${t.counts.map(([n, c]) => `"${n}" ok x${c}`).join(', ')} (want 1 each)` +
    ` warning-lines=${warn} tap=${LOG_DIR}/node-tap.txt ${stepLabel(r)}`, r.wallS);
}

// ---- A3: node --check, self-tests, L-NODE, L-TAP, L-VLINT -------------------------------------------------------
async function a3Node() {
  const bad = [];
  for (const f of NODE_CHECK) {
    const c = await node('--check', [f], { log: `A3a-check-${path.basename(f, '.js')}.log`, ceilingMs: 2 * MIN });
    if (!green(c)) bad.push(`${f} ${fail(c)}`);
  }
  record('A3a', bad.length === 0, `node --check on ${NODE_CHECK.length} files: ${bad.length ? bad.join('; ') : 'all ok'}`);

  const red = [];
  for (const s of SELF_TESTS) {
    const c = await node(path.join(EVID, `${s}.js`), ['--self-test'], { log: `A3b-self-test-${s}.log`, ceilingMs: 2 * MIN });
    if (!green(c) || !lastLine(c.out).includes('GREEN')) red.push(`${s} ${fail(c)} last="${lastLine(c.out)}"`);
  }
  record('A3b', red.length === 0, `--self-test of ${SELF_TESTS.join(', ')}: ${red.length ? red.join('; ') : 'all GREEN'}`);

  const tapFile = `${LOG_DIR}/feed-tap.txt`;
  const r = await run(process.execPath, ['--test', '--test-reporter=tap', 'tools/feed-contract/tests/*.test.js'],
    { log: 'A3c-node-test.log', ceilingMs: 10 * MIN });
  fs.writeFileSync(tapFile, r.out);
  const num = (k) => { const m = r.out.match(new RegExp(`^# ${k} (\\d+)`, 'm')); return m ? Number(m[1]) : NaN; };
  const plan = (r.out.match(/^1\.\.(\d+)$/m) || [])[1];
  const nodeWarnings = lines(r.out).filter((l) => /\(node:\d+\) .*Warning|DeprecationWarning|ExperimentalWarning/.test(l)).length;
  record('A3c', green(r) && num('fail') === 0 && Number(plan) === EXPECTED_PLAN && nodeWarnings === 0,
    `L-NODE node --test plan=1..${plan} tests=${num('tests')} pass=${num('pass')} fail=${num('fail')}` +
    ` nodeWarnings=${nodeWarnings} tap=${tapFile} ${stepLabel(r)}`, r.wallS);

  const t = await node('specs/krate-manager-phase2/evidence/tap-check.js', [tapFile], { log: 'A3d-tap-check.log', ceilingMs: 2 * MIN });
  const want = `TAP plan=1..${EXPECTED_PLAN} top-level=${EXPECTED_PLAN}`;
  record('A3d', green(t) && t.out.includes(want), `L-TAP want "${want}" ${t.out.includes(want) ? 'printed' : 'NOT printed'}` +
    ` last="${lastLine(t.out)}" ${stepLabel(t)}`, t.wallS);

  const v = await node('tools/lint-manager-version.js', [], { log: 'A3e-version-lint.log', ceilingMs: 2 * MIN });
  record('A3e', green(v) && v.err.trim() === '', `L-VLINT stderr=${v.err.trim() ? `"${lastLine(v.err)}"` : 'none'} ${stepLabel(v)}`,
    v.wallS);
}

// ---- A4..A7, G, A10 ---------------------------------------------------------------------------------------------
async function a4Yaml() {
  const r = await run('npx --yes yaml@2.6.1 valid', [], {
    log: 'A4-yaml-manager-ci.log', ceilingMs: 5 * MIN, stdinFile: '.github/workflows/manager-ci.yml', shell: true,
  });
  record('A4', green(r), `L-YAML .github/workflows/manager-ci.yml ${stepLabel(r)}`, r.wallS);
}

async function a5Lint() {
  const r = await node('tools/lint-serial-test-runs.js', [], { log: 'A5-lint-serial.log', ceilingMs: 5 * MIN });
  record('A5', green(r), `L-LINT lint-serial-test-runs last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
}

async function a6SpecsIndex() {
  const phaseDir = `krate-manager-phase${PHASE}/`;
  let r = await node('tools/gen-specs-index.js', ['--check'], { log: 'A6-specs-index.log', ceilingMs: 5 * MIN });
  if (green(r)) {
    record('A6', true, `L-INDEX gen-specs-index --check last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
    return;
  }
  // T019 step 4: red only because this phase's spec files changed the index -> regenerate once and re-check.
  const indexPath = path.join(ROOT, 'specs', 'INDEX.md');
  const before = fs.readFileSync(indexPath, 'utf8');
  const g = await node('tools/gen-specs-index.js', [], { log: 'A6-specs-index-regen.log', ceilingMs: 5 * MIN });
  const changed = changedLines(before, fs.readFileSync(indexPath, 'utf8'));
  const foreign = changed.filter((l) => !l.includes(phaseDir));
  if (!green(g) || foreign.length) {
    fs.writeFileSync(indexPath, before);
    record('A6', false, `L-INDEX --check red and the regeneration ${green(g) ? `changed ${foreign.length} line(s) not naming` +
      ` ${phaseDir} (first: "${foreign[0]}")` : `failed (${fail(g)})`}; specs/INDEX.md restored ${stepLabel(r)}`, r.wallS);
    return;
  }
  r = await node('tools/gen-specs-index.js', ['--check'], { log: 'A6-specs-index-recheck.log', ceilingMs: 5 * MIN });
  record('A6', green(r), `L-INDEX regenerated once (${changed.length} changed line(s), all naming ${phaseDir}),` +
    ` then --check last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
}

async function scope(id, log) {
  const r = await node(path.join(EVID, 'scope-check.js'), [], { log, ceilingMs: 2 * MIN });
  record(id, green(r), `L-SCOPE scope-check last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
}

/** G1, G3, G4, G6 (T019 step 6): file scans in this process, seconds. */
function greps() {
  const rust = readTextFiles([`${APP_REL}crates`, `${APP_REL}src-tauri`]);
  const g1 = todoHits(rust.entries);
  record('G1', !rust.error && g1.length === 0, `rule 9: todo!( / unimplemented!( outside *_tests.rs and tests/ in` +
    ` ${rust.entries.filter(([p]) => p.endsWith('.rs')).length} .rs files: ${rust.error || (g1.length ? `HIT ${g1.join(' ; ')}` : 'none')}`);

  const scanned = readTextFiles([APP_REL, '.github/workflows/manager-ci.yml', `${EVID_REL}*.js`]);
  const g3 = threadFlagHits(scanned.entries);
  record('G3', !scanned.error && g3.length === 0, `no thread-count flag in ${scanned.entries.length} files (the app,` +
    ` manager-ci.yml, ${EVID_REL}*.js; exempt ${THREAD_FLAG_EXEMPT.join(', ')}): ` +
    `${scanned.error || (g3.length ? `HIT ${g3.join(' ; ')}` : 'none')}`);

  const fence = fenceHits(scanned.paths.filter((p) => p.startsWith(APP_REL)), scanned.entries.filter(([p]) => p.startsWith(APP_REL)));
  const fenceDirOnDisk = fs.existsSync(path.join(ROOT, FENCE_DIR));
  record('G4', !scanned.error && fence.length === 0 && !fenceDirOnDisk, `M-1 fence (SC-032): ${FENCE_DIR}` +
    ` ${fenceDirOnDisk ? 'EXISTS' : 'absent'}; LOCKED_EXIT / LOCKED_LINE words: ${fence.length ? `HIT ${fence.join(' ; ')}` : 'none'}`);

  const v = needleVerdict(fs.readFileSync(ROADMAP, 'utf8'));
  record('G6', v.ok, `FR-123 needles of T002, one line each in ${rel(ROADMAP)}: ${v.detail}`);
}

async function a10Wsl() {
  const out = `${EVID_REL}wsl-core.txt`;
  const r = await node(path.join(EVID, 'wsl-lane.js'), ['--out', out], { log: 'A10-wsl-lane.log', ceilingMs: 45 * MIN });
  const last = lastLine(r.out);
  const prefix = WSL_INFO_ALL.replace(/ \d+\/\d+$/, '');
  const info = lines(r.out).find((l) => l.startsWith(prefix)) || 'no information line';
  const all = info.startsWith(WSL_INFO_ALL);
  record('A10', green(r) && last.startsWith('WSL LANE GREEN') && all, `L-WSL last="${last}" info="${info}"` +
    ` -> ${out} (early signal; closes no PENDING-CI row) ${stepLabel(r)}`, r.wallS);
}

// ---- B: L-BUNDLE after the cargo chain, L-REQ after both chains -------------------------------------------------
async function b1Bundle() {
  const r = await node(path.join(EVID, 'bundle-lane.js'), ['--out', `${LOG_DIR}/B1-npm-run-bundle.txt`],
    { log: 'B1-bundle-lane.log', ceilingMs: 45 * MIN });
  const rows = lines(r.out).filter((l) => /^L\d /.test(l)).map((l) => l.replace(/\s+/g, ' ')).join(' | ');
  record('B1', green(r) && lastLine(r.out) === 'BUNDLE LANE GREEN', `L-BUNDLE last="${lastLine(r.out)}" rows: ${rows}` +
    ` ${stepLabel(r)}`, r.wallS);
}

async function b2Required() {
  const missing = ['cargo-test.txt', 'vitest.json', 'node-tap.txt'].filter((f) => !fs.existsSync(`${LOG_DIR}/${f}`));
  const r = await step('required tests ran', 'B2-required-tests-ran.log', 5 * MIN);
  record('B2', green(r) && r.out.includes(REQUIRED_NOTICE), `L-REQ step required tests ran notices="${notices(r.out)}"` +
    ` want "${REQUIRED_NOTICE}"${missing.length ? ` (inputs MISSING: ${missing.join(', ')})` : ''} ${stepLabel(r)}`, r.wallS);
}

/** C3: the repo's cleanup rule. Synchronous and logged to no file: it removes LOG_DIR itself. */
function c3Clean() {
  const t0 = Date.now();
  const r = childProcess.spawnSync(process.execPath, ['tools/clean-test-output.js', '--apply'],
    { cwd: ROOT, encoding: 'utf8', windowsHide: true, timeout: 10 * MIN, maxBuffer: 1 << 26 });
  const wallS = ((Date.now() - t0) / 1000).toFixed(1);
  const timedOut = Boolean(r.error && r.error.code === 'ETIMEDOUT');
  const ok = !r.error && r.status === 0;
  const why = r.error ? (timedOut ? 'TIMEOUT' : r.error.message) : `exit=${r.status}`;
  record('C3', ok, `node tools/clean-test-output.js --apply summary="${lastLine(r.stdout || '')}" ${why} wall=${wallS}s` +
    ` (raw logs under ${LOG_DIR} removed)`, wallS);
}

async function main() {
  fs.mkdirSync(LOG_DIR, { recursive: true });
  const t0 = Date.now();
  console.log(`close lane: logs ${LOG_DIR}`);
  yamlText = fs.readFileSync(WORKFLOW, 'utf8');
  await a0Records();
  const before = gitStatus();
  fs.writeFileSync(`${LOG_DIR}/A0b-git-status.txt`, before);
  record('A0b', before !== 'git status failed', `git status --porcelain: ${lines(before).filter(Boolean).length} lines` +
    ` (without ${EVID_REL}) file=${LOG_DIR}/A0b-git-status.txt`);

  greps();
  const a1 = a1Cargo();
  const a2 = a2Npm();
  const bundle = a1.then(() => {
    const chain = results.filter((r) => /^A1[a-z]$/.test(r.id));
    if (chain.every((r) => r.ok)) return b1Bundle();
    record('B1', false, `L-BUNDLE skipped: not every A1 row is green (${chain.filter((r) => !r.ok).map((r) => r.id).join(', ')})`);
    return undefined;
  });
  const required = Promise.all([a1, a2]).then(b2Required);
  await Promise.all([a1, a2, bundle, required, a3Node(), a4Yaml(), a5Lint(), a6SpecsIndex(), scope('A7', 'A7-scope-check.log'),
    a10Wsl()]);

  const after = gitStatus();
  fs.writeFileSync(`${LOG_DIR}/C1-git-status.txt`, after);
  const same = after === before;
  record('C1', same, `git status --porcelain (without ${EVID_REL}) ${same ? 'equal to A0b' : 'CHANGED from A0b'}` +
    ` (${lines(after).filter(Boolean).length} lines; changed: ${changedLines(before, after).filter(Boolean).slice(0, 10).join(' ; ') || 'none'})` +
    ` file=${LOG_DIR}/C1-git-status.txt`);
  await scope('C2', 'C2-scope-check.log');
  c3Clean();

  const order = ['A0', 'A0b', 'A1j', 'A1k', 'A1a', 'A1b', 'A1c', 'A1d', 'A1e', 'A1f', 'A1g', 'A1h', 'A2a', 'A2b', 'A2c',
    'A2d', 'A2e', 'A3a', 'A3b', 'A3c', 'A3d', 'A3e', 'A4', 'A5', 'A6', 'A7', 'G1', 'G3', 'G4', 'G5', 'G6', 'A10', 'B1', 'B2',
    'C1', 'C2', 'C3'];
  results.sort((x, y) => order.indexOf(x.id) - order.indexOf(y.id));
  const verdict = verdictLine(results);
  const totalS = ((Date.now() - t0) / 1000).toFixed(1);
  const summary = [
    `Krate Manager Phase ${PHASE} close lane, ${new Date().toISOString()}, wall=${totalS}s, logs ${LOG_DIR} (removed by C3)`,
    ...results.map((r) => `${r.id.padEnd(5)} ${r.ok ? 'GREEN' : 'RED  '} ${r.text}`),
    verdict,
  ].join('\n');
  fs.writeFileSync(SUMMARY, `${summary}\n`);
  fs.writeFileSync(TIMINGS, timingsMarkdown({ rows: results, totalS, verdict, logDir: LOG_DIR, when: new Date().toISOString() }));
  console.log(`\n${summary}`);
  process.exit(verdict === 'CLOSE LANE GREEN' ? 0 : 1);
}

if (process.argv.includes('--self-test')) process.exit(selfTest());
main();
