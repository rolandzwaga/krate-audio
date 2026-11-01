#!/usr/bin/env node
// Close lane for Krate Manager Phase 8 (L-CLOSE; tasks.md T006, T008; spec SC-001, SC-018 record, FR-081, FR-082,
// FR-090, FR-091; plan.md §0.2 "Not an issue", §7.4, §8; tasks TA-1, TA-10, TA-11). Run by the MAIN LOOP at close, never
// by an implementer or a gate: it holds L-WSL, whose first Phase 8 run compiles the module and the `cfg(unix)` code
// cold, well past five minutes.
//
// WHY one script: the close rows must come from one run whose steps share a clock and a log directory, with every
// independent step concurrent (CLAUDE.md "Optimise Every Build and Test Run") and every child under a wall-clock
// ceiling, so a hung step reads RED instead of stalling the close. Same form as
// specs/krate-manager-phase7/evidence/close-lane.js minus its rows A0c, A1i and B2-B7 (the base binary size, Phase 7's
// SC-024 timing delta and the `tauri build` rows: TA-11, Phase 8 changes no src-tauri, webview or capability file, so
// the release build cannot see this phase's code and its evidence is the unchanged CI leg); A1h is now Phase 8's
// `cargo tree` record (FR-081, FR-082), A1k covers FR-090's three paths, and A1l (the flate2 backend and `tar`
// absence over every target, plan.md:69-79) and A1m (the two lockfiles, rule 8 and TA-1) are new.
//
// WHY the workflow's own step text: wherever a row says "step <name>", the script extracts that step's `run: |` block
// from .github/workflows/manager-ci.yml, removes its indentation and runs it with Git Bash `bash.exe -c`, cwd
// apps/krate-manager/ (the job's defaults.run.working-directory), env RUNNER_OS=Windows and RUNNER_TEMP=<log dir>. Local
// and CI logic cannot drift. The step text, as run, is saved beside its log (<log>.sh). The steps run are RUN_STEPS.
//
// Usage (repo root, Windows dev box; Node only, no dependencies):
//   node specs/krate-manager-phase8/evidence/close-lane.js              # the close (last line GREEN | RED)
//   node specs/krate-manager-phase8/evidence/close-lane.js --self-test  # pure checks (extractStep over the real
//                                                                        # workflow for every step run, the tree
//                                                                        # verdicts, the lock verdict, the
//                                                                        # suppression scan)
// Rows:
//   A0   rustc --version, node --version, df -h of the repo root only (records only)
//   A0b  git status --porcelain --untracked-files=all snapshot, kept for C1
//   A (concurrent):
//     A1 one chain (the cargo commands share one target-dir lock): A1j SC-001: git diff -U0 2499fb13 --
//        apps/krate-manager > evidence/diff-u0.txt, plus every untracked file under apps/krate-manager/ read whole (a
//        never-committed file such as extract.rs is not in the diff; its lines are all added), scanned for added lines
//        holding allow( / expect( in an attribute, @ts-ignore or eslint-disable (expected none); A1k FR-090: git diff
//        --name-only 2499fb13 -- apps/krate-manager/src apps/krate-manager/src-tauri apps/krate-manager/index.html and
//        the untracked files under those paths both empty; A1m rule 8 / TA-1: git diff --numstat 2499fb13 --
//        apps/krate-manager/Cargo.lock empty and the same for tests/fixtures/clippy-guard/Cargo.lock exactly `1 0`;
//        A1a step `cargo fmt`; A1b step `clippy`; A1c step `clippy dev-feed`; A1d step `clippy-guard` (its ::notice;
//        $RUNNER_TEMP/guard.txt copied to evidence/clippy-guard.txt); A1e step `cargo test`
//        ($RUNNER_TEMP/cargo-test.txt copied to evidence/cargo-test.txt); A1f step `core dependency tree` (::notice
//        ...::<n> crates, none excluded; $RUNNER_TEMP/core-tree.txt copied to evidence/core-tree.txt); A1h cargo tree
//        -p krate-manager-core -e normal --locked --prefix none --format "{p}" > evidence/core-tree-phase8.txt (FR-081,
//        FR-082, plan §7.4 step 6: `flate2 v1.1.10` present, no line starting `tar `, `tauri `, `tokio `, `reqwest `,
//        `hyper `, `dirs ` or `home `); A1l cargo tree --workspace --target all -e features -i zlib-rs --locked prints
//        "nothing to print" and cargo tree --workspace --target all -i tar --locked says `tar` matched no package
//        (plan.md:69-79: miniz_oxide on every leg, no tar crate)
//     A2 one chain: A2a step `npm ci` only if node_modules is absent; A2b step `typecheck`; A2c step `lint`; A2d step
//        `vitest`
//     A3 (repo root): A3a node --check on this phase's three scripts; A3b node --test --test-reporter=tap
//        "tools/feed-contract/tests/*.test.js" to a TAP file (# fail 0, plan 1..59, no Node warning line); A3c L-TAP on
//        it ("TAP plan=1..59 top-level=59"); A3d L-VLINT
//     A4 L-YAML (yaml@2.6.1) on manager-ci.yml
//     A5 L-LINT node tools/lint-serial-test-runs.js
//     A6 L-INDEX node tools/gen-specs-index.js --check; if red only because this phase's spec files changed the index
//        (every changed line names krate-manager-phase8/), regenerate once and re-check; otherwise restore and RED
//     A7 A7a scope-check.js --self-test, then A7b L-SCOPE
//     A9 the TA-1 / rule 9 grep: no todo!( / unimplemented!( in crates/ or src-tauri/src outside *_tests.rs and tests/
//     A10 L-WSL (TA-10): wsl-lane.js --out evidence/wsl-core.txt; last line WSL LANE GREEN and 5/5 FR-086 `_unix`
//         names seen ok (an early signal recorded beside the PENDING-CI rows, never instead of them)
//   B (after A1 and A2, only when every A1 / A2 row is green):
//     B1 step `required tests ran` (::notice ...::259 cargo + 29 vitest: 248 `all` + 11 `windows`, plan §4.7)
//   C (last): C1 git status again, minus lines under specs/krate-manager-phase8/evidence/, equal to A0b (SC-001); C2
//     L-SCOPE again; C3 node tools/clean-test-output.js --apply (repo rule; it removes every F:\tmp entry, this run's raw
//     log directory included, so it runs last and its summary line goes into the evidence summary)
// Step `Checkout unchanged` is not run locally: the phase's own uncommitted files fail it until the user commits. Steps
// `tauri build`, `build warnings`, `dev-feed absent`, `roster absent` and `artifact version` are not run (TA-11).
// Raw logs: F:/tmp/gate/krate-manager-phase8-close/<timestamp>/ (removed by C3). Summary: evidence/close-lane.log
// (written and printed); wall clocks: evidence/timings.md (for the next run's planning). Exit 0 green, 1 red.
'use strict';
const childProcess = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const APP = path.join(ROOT, 'apps', 'krate-manager');
const STAMP = new Date().toISOString().replace(/[-:]/g, '').replace(/\..*/, '').replace('T', '-');
// Forward slashes: the directory is also RUNNER_TEMP inside Git Bash.
const LOG_DIR = `F:/tmp/gate/krate-manager-phase8-close/${STAMP}`;
const EVID = __dirname;
const EVID_REL = 'specs/krate-manager-phase8/evidence/';
const evid = (name) => path.join(EVID, name);
const SUMMARY = evid('close-lane.log');
const TIMINGS = evid('timings.md');
const WORKFLOW = path.join(ROOT, '.github', 'workflows', 'manager-ci.yml');
const BASH = 'C:/Program Files/Git/bin/bash.exe';
const MIN = 60 * 1000;

// The scope base (scope-check.js BASE; tasks.md header: written against feat/krate-manager at 2499fb13).
const BASE = '2499fb13';
const EXPECTED_PLAN = 59; // tasks.md L-TAP
// plan §4.7 totals: cargo.all 248 + cargo.windows 11 (tasks.md T006, T007 step 2).
const REQUIRED_NOTICE = '::notice title=required tests ran (Windows)::259 cargo + 29 vitest';
const CORE_TREE_NOTICE = /^::notice title=core dependency tree \(Windows\)::(\d+) crates, none excluded$/m;
// tasks.md T006 A1h (FR-081, FR-082; plan §7.4 step 6).
const TREE_PRESENT = ['flate2 v1.1.10'];
const TREE_ABSENT = ['tar ', 'tauri ', 'tokio ', 'reqwest ', 'hyper ', 'dirs ', 'home '];
// tasks.md T006 A1k (FR-090): no src-tauri, webview or capability change.
const FR090_PATHS = ['apps/krate-manager/src', 'apps/krate-manager/src-tauri', 'apps/krate-manager/index.html'];
// tasks.md T006 A1m (rule 8, TA-1).
const WORKSPACE_LOCK = 'apps/krate-manager/Cargo.lock';
const GUARD_LOCK = 'apps/krate-manager/tests/fixtures/clippy-guard/Cargo.lock';
const TAP_FILE = `${LOG_DIR}/A3b-node-test.tap`;
// tasks.md T006 A9 (rule 9, TA-1): *_tests.rs and */tests/* excluded.
const TA1_GREP = "grep -rnE --include='*.rs' --exclude='*_tests.rs' --exclude-dir=tests" +
  " -- 'todo!\\(|unimplemented!\\(' apps/krate-manager/crates apps/krate-manager/src-tauri/src";
// L-NODE's node --check set: this phase's three scripts (tasks.md T006).
const NODE_CHECK = [
  'specs/krate-manager-phase8/evidence/close-lane.js',
  'specs/krate-manager-phase8/evidence/scope-check.js',
  'specs/krate-manager-phase8/evidence/wsl-lane.js',
];
// wsl-lane.js INFO_PREFIX and LINUX_RUN_TESTS (FR-086's five `_unix` names, spec.md:527-530).
const WSL_INFO_ALL = 'Linux-run Phase 8 tests seen ok: 5/5';
// Every manager-ci.yml step this script runs (step() refuses any other name; the self-test extracts each one).
const RUN_STEPS = ['cargo fmt', 'clippy', 'clippy dev-feed', 'clippy-guard', 'cargo test', 'core dependency tree',
  'npm ci', 'typecheck', 'lint', 'vitest', 'required tests ran'];

// ---- pure helpers (covered by --self-test) ----------------------------------------------------------------------
const lines = (text) => text.split(/\r?\n/);
const lastLine = (text) => lines(text).map((l) => l.trim()).filter(Boolean).pop() || '';
// cargo colours nothing into a pipe, but a step's tee keeps whatever arrives; strip ANSI before matching.
const plain = (text) => text.replace(/\x1b\[[0-9;]*m/g, '');
const warningLines = (text) => lines(plain(text)).filter((l) => /^warning:/.test(l)).length;
const notices = (text) => lines(text).filter((l) => /^::(notice|error) /.test(l)).join(' | ');

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

/** tasks.md T006 A1h (FR-081, FR-082): TREE_PRESENT lines present, no line starting with a TREE_ABSENT prefix. */
function coreTreeVerdict(text) {
  const all = lines(text).map((l) => l.trim()).filter(Boolean);
  const missing = TREE_PRESENT.filter((want) => !all.some((l) => l === want || l.startsWith(`${want} `)));
  const banned = all.filter((l) => TREE_ABSENT.some((prefix) => l.startsWith(prefix)));
  const ok = all.length > 0 && missing.length === 0 && banned.length === 0;
  const parts = [];
  if (all.length === 0) parts.push('no crate listed');
  if (missing.length) parts.push(`MISSING ${missing.join(', ')}`);
  if (banned.length) parts.push(`BANNED CRATE ${banned.join('; ')}`);
  return { ok, detail: ok ? `${TREE_PRESENT.join(' and ')} present, no ${TREE_ABSENT.map((p) => p.trim()).join(' / ')}`
    : parts.join('; ') };
}

/**
 * tasks.md T006 A1l, first half (plan.md:69-76): `cargo tree --workspace --target all -e features -i zlib-rs` exits 0
 * with cargo's "nothing to print" and lists no `zlib-rs v` node, so no target activates flate2's zlib-rs backend.
 */
function zlibVerdict({ code, out }) {
  const text = plain(out);
  const treeNode = lines(text).find((l) => /^zlib-rs v/.test(l.trim()));
  const ok = code === 0 && /nothing to print/.test(text) && !treeNode;
  return { ok, detail: ok ? '"nothing to print"' : `exit=${code} ${treeNode ? `TREE "${treeNode.trim()}"` : `last="${lastLine(text)}"`}` };
}

/**
 * tasks.md T006 A1l, second half (plan.md:77-79): `cargo tree --workspace --target all -i tar` fails with cargo's
 * "package ID specification `tar` did not match any packages", so no `tar` crate is in the graph of any target.
 */
function tarVerdict({ code, out }) {
  const text = plain(out);
  const hit = lines(text).find((l) => /package ID specification `tar` did not match any packages/.test(l));
  const ok = code !== 0 && Boolean(hit);
  return { ok, detail: ok ? `"${hit.trim()}"` : `exit=${code} last="${lastLine(text)}"` };
}

/**
 * tasks.md T006 A1m (rule 8, TA-1): `git diff --numstat` of the workspace lock prints nothing, and the clippy-guard
 * lock's is exactly one `1<TAB>0<TAB><path>` line (one added `"flate2",`, nothing removed).
 */
function numstatVerdict(workspaceOut, guardOut) {
  const ws = lines(workspaceOut).filter((l) => l.trim());
  const guard = lines(guardOut).filter((l) => l.trim());
  const guardOk = guard.length === 1 && /^1\t0\t\S/.test(guard[0]);
  const ok = ws.length === 0 && guardOk;
  const show = (l) => (l.length ? l.map((x) => x.replace(/\t/g, ' ')).join(' ; ') : 'empty');
  return { ok, detail: `${WORKSPACE_LOCK}: ${show(ws)} (want empty); ${GUARD_LOCK}: ${show(guard)} (want "1 0")` };
}

/**
 * What a suppression an added line holds, or ''. A lint attribute is a line that starts `#[` / `#![` (rustfmt puts
 * every attribute at the start of its line, the first line of a multi-line one included) and names `allow(` or
 * `expect(`; a guard's plant inside a string literal starts with its quote, or ends with a `\` continuation, and is
 * no attribute. `@ts-ignore` and `eslint-disable` count anywhere on the line.
 */
function suppressionKind(content) {
  const t = content.trim();
  if (/^#!?\[/.test(t) && !t.endsWith('\\') && /\b(?:allow|expect)\s*\(/.test(t)) return 'lint attribute';
  if (t.includes('@ts-ignore')) return '@ts-ignore';
  if (t.includes('eslint-disable')) return 'eslint-disable';
  return '';
}

/** tasks.md T006 A1j (SC-001): the added lines of a -U0 diff that add a suppression, as `<file>: <kind>: <line>`. */
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
  const yaml = [
    'jobs:',
    '  check:',
    '    steps:',
    '      - name: first',
    '        run: |',
    '          echo one',
    "      - name: 'second'",
    '        run: |',
    '          set -euo pipefail',
    "          node - <<'X'",
    '          const a = 1;',
    '            nested',
    '          X',
    '',
    '      - name: third',
    '        run: echo three',
    '      - name: fourth',
    "        if: runner.os == 'macOS'",
    '        run: |',
    '          echo four',
    '',
  ].join('\n');
  const second = "set -euo pipefail\nnode - <<'X'\nconst a = 1;\n  nested\nX\n";
  check('second step de-indented', () => eq(extractStep(yaml, 'second'), second));
  check('missing step throws', () => throws(() => extractStep(yaml, 'missing'), /no step "missing"/));
  check('CRLF yaml gives the same block', () => eq(extractStep(yaml.replace(/\n/g, '\r\n'), 'second'), second));
  check('first step stops at the next step', () => eq(extractStep(yaml, 'first'), 'echo one\n'));
  check('one-line run refused', () => throws(() => extractStep(yaml, 'third'), /has no "run: \|" block/));
  check('step with if: refused', () => throws(() => extractStep(yaml, 'fourth'), /sets if:/));

  // The constants this phase changes, spelled here so a stale Phase 7 value reads red.
  check('base is 2499fb13', () => eq(BASE, '2499fb13'));
  check('required notice 259 cargo (248 all + 11 windows) + 29 vitest', () =>
    eq(REQUIRED_NOTICE, '::notice title=required tests ran (Windows)::259 cargo + 29 vitest'));
  check('no tauri build step (TA-11)', () => eq(RUN_STEPS.join(','),
    'cargo fmt,clippy,clippy dev-feed,clippy-guard,cargo test,core dependency tree,npm ci,typecheck,lint,vitest,required tests ran'));
  check('node --check set is this phase\'s three scripts', () => eq(NODE_CHECK.join(','),
    ['close-lane', 'scope-check', 'wsl-lane'].map((s) => `specs/krate-manager-phase8/evidence/${s}.js`).join(',')));
  check('FR-090 paths', () => eq(FR090_PATHS.join(','),
    'apps/krate-manager/src,apps/krate-manager/src-tauri,apps/krate-manager/index.html'));
  check('raw log root', () => eq(LOG_DIR.startsWith('F:/tmp/gate/krate-manager-phase8-close/'), true));

  // The real workflow: every step this lane runs extracts (no if:, no working-directory:, a run: | block, no CR), and
  // the two steps the close lane's verdicts lean on hold the text they are read for.
  const real = fs.readFileSync(WORKFLOW, 'utf8');
  for (const name of RUN_STEPS) {
    check(`real step ${name} extracts`, () => {
      const s = extractStep(real, name);
      eq(s.trim() !== '' && !s.includes('\r') && !/^\s/.test(s), true);
    });
  }
  check('real step cargo test', () => {
    const s = extractStep(real, 'cargo test');
    eq(s.startsWith('set -euo pipefail\n'), true);
    has(s, 'cargo test --workspace --locked 2>&1 | tee "$RUNNER_TEMP/cargo-test.txt"\n');
  });
  check('real step required tests ran', () => {
    const s = extractStep(real, 'required tests ran');
    has(s, `node - "$RUNNER_TEMP/cargo-test.txt" "$RUNNER_TEMP/vitest.json" "$RUNNER_OS" <<'REQUIRED'\n`);
    has(s, '::notice title=required tests ran (${runnerOs})::${names.length} cargo + ${req.vitest.length} vitest');
    eq(s.endsWith('\nREQUIRED\n'), true); // the heredoc terminator at column 0
  });
  check('real step core dependency tree', () =>
    has(extractStep(real, 'core dependency tree'), 'cargo tree -p krate-manager-core -e normal --locked --prefix none --format "{p}"'));

  // A1h: `cargo tree --prefix none --format "{p}"` lines (a repeated crate carries ` (*)`).
  const tree = ['krate-manager-core v0.1.0 (F:\\x\\crates\\core)', 'base64 v0.23.1', 'flate2 v1.1.10',
    'minisign-verify v0.3.0', 'sha2 v0.11.1', 'ureq v3.5.0', 'digest v0.11.0 (*)', 'tarpc-like v0.1.0', 'homedir-like v1.0.0',
    ''].join('\n');
  check('tree with flate2 v1.1.10, no banned crate (CRLF) -> GREEN', () => eq(coreTreeVerdict(tree.replace(/\n/g, '\r\n')).ok, true));
  check('tree with a repeated flate2 (*) -> GREEN', () =>
    eq(coreTreeVerdict(tree.replace('flate2 v1.1.10', 'flate2 v1.1.10 (*)')).ok, true));
  check('tree without flate2 -> RED', () => eq(coreTreeVerdict(tree.replace('flate2 v1.1.10\n', '')).ok, false));
  check('tree with another flate2 version -> RED', () =>
    eq(coreTreeVerdict(tree.replace('flate2 v1.1.10', 'flate2 v1.1.9')).ok, false));
  for (const banned of ['tar v0.4.44', 'tauri v2.9.0', 'tokio v1.48.0', 'reqwest v0.12.0', 'hyper v1.7.0', 'dirs v6.0.0',
    'home v0.5.11']) {
    check(`tree with ${banned} -> RED`, () => eq(coreTreeVerdict(`${tree}${banned}\n`).ok, false));
  }
  check('empty tree -> RED', () => eq(coreTreeVerdict('').ok, false));

  // A1l: cargo's answers (plan.md:71, :78) and the trees that would contradict them.
  const nothing = 'warning: nothing to print.\n\nTo find dependencies that require specific target platforms, try to use' +
    ' option `--target all` first, and then narrow your search scope accordingly.\n';
  check('zlib-rs: nothing to print, exit 0 -> GREEN', () => eq(zlibVerdict({ code: 0, out: nothing }).ok, true));
  check('zlib-rs: an inverted tree -> RED', () =>
    eq(zlibVerdict({ code: 0, out: 'zlib-rs v0.6.8\n└── flate2 v1.1.10\n    └── ureq v3.5.0\n' }).ok, false));
  check('zlib-rs: exit 101 -> RED', () => eq(zlibVerdict({ code: 101, out: 'error: failed to load manifest\n' }).ok, false));
  const noTar = 'error: package ID specification `tar` did not match any packages\n';
  check('tar: did not match any packages, exit 101 -> GREEN', () => eq(tarVerdict({ code: 101, out: noTar }).ok, true));
  check('tar: did not match (CRLF) -> GREEN', () => eq(tarVerdict({ code: 101, out: noTar.replace(/\n/g, '\r\n') }).ok, true));
  check('tar: an inverted tree, exit 0 -> RED', () =>
    eq(tarVerdict({ code: 0, out: 'tar v0.4.44\n└── krate-manager-core v0.1.0\n' }).ok, false));
  check('tar: another error -> RED', () => eq(tarVerdict({ code: 101, out: 'error: failed to load manifest\n' }).ok, false));

  // A1m: `git diff --numstat <base> -- <lock>` (TA-1: the clippy-guard lock gains exactly one line).
  const guardLine = '1\t0\tapps/krate-manager/tests/fixtures/clippy-guard/Cargo.lock\n';
  check('locks: workspace lock unchanged, guard lock 1 0 -> GREEN', () => eq(numstatVerdict('', guardLine).ok, true));
  check('locks: CRLF output -> GREEN', () => eq(numstatVerdict('\r\n', guardLine.replace('\n', '\r\n')).ok, true));
  check('locks: workspace lock changed -> RED', () =>
    eq(numstatVerdict('3\t1\tapps/krate-manager/Cargo.lock\n', guardLine).ok, false));
  check('locks: guard lock 2 0 -> RED', () => eq(numstatVerdict('', guardLine.replace('1\t0', '2\t0')).ok, false));
  check('locks: guard lock 1 1 -> RED', () => eq(numstatVerdict('', guardLine.replace('1\t0', '1\t1')).ok, false));
  check('locks: guard lock not re-locked -> RED', () => eq(numstatVerdict('', '').ok, false));

  // A1j: a sample `git diff -U0`.
  const diff = (...added) => [
    'diff --git a/apps/krate-manager/crates/core/src/extract.rs b/apps/krate-manager/crates/core/src/extract.rs',
    'index 1111111..2222222 100644',
    '--- a/apps/krate-manager/crates/core/src/extract.rs',
    '+++ b/apps/krate-manager/crates/core/src/extract.rs',
    '@@ -10 +10,0 @@ fn x',
    '-#[allow(dead_code)]',
    '@@ -20,0 +20,2 @@ fn y',
    ...added.map((a) => `+${a}`),
  ].join('\n');
  check('added #[expect(clippy::x, reason = "y")] -> RED', () =>
    eq(suppressionHits(diff('#[expect(clippy::x, reason = "y")]', 'fn planted() {}')).length, 1));
  check('added "// see allow(" comment -> GREEN', () => eq(suppressionHits(diff('    // see allow( in the docs', 'fn f() {}')).length, 0));
  check('added // @ts-ignore -> RED', () => eq(suppressionHits(diff('// @ts-ignore', 'const x = 1;')).length, 1));
  check('added eslint-disable-next-line -> RED', () => eq(suppressionHits(diff('// eslint-disable-next-line no-console')).length, 1));
  check('added indented multi-line #[expect( -> RED', () =>
    eq(suppressionHits(diff('    #[expect(', '        clippy::too_many_arguments,')).length, 1));
  check('added #![allow(...)] -> RED', () => eq(suppressionHits(diff('#![allow(dead_code)]')).length, 1));
  check('added #[cfg_attr(test, allow(...))] -> RED', () => eq(suppressionHits(diff('    #[cfg_attr(test, allow(dead_code))]')).length, 1));
  check('added guard plant in a string literal -> GREEN', () =>
    eq(suppressionHits(diff('            "#[expect(clippy::too_many_arguments, reason = \\"x\\")]\\nfn planted() {}\\n",')).length, 0));
  check('added plant on a string continuation line -> GREEN', () =>
    eq(suppressionHits(diff('        #[expect(clippy::all)]\\n\\')).length, 0));
  check('added #[should_panic(expected = "x")] and #[test] -> GREEN', () =>
    eq(suppressionHits(diff('#[should_panic(expected = "x")]', '#[test]')).length, 0));
  check('removed #[allow(dead_code)] only -> GREEN', () => eq(suppressionHits(diff()).length, 0));
  check('a hit names its file', () =>
    has(suppressionHits(diff('#[allow(x)]'))[0] || '', 'apps/krate-manager/crates/core/src/extract.rs: '));
  check('untracked file read whole -> its #[allow( reported', () => {
    const hits = suppressionHits(untrackedDiff([['apps/krate-manager/crates/core/src/new.rs', 'fn a() {}\r\n#[allow(dead_code)]\r\nfn b() {}\r\n'],
      ['apps/krate-manager/crates/core/src/clean.rs', '// see allow( here\n']]));
    eq(hits.length, 1);
    has(hits[0], 'apps/krate-manager/crates/core/src/new.rs: ');
  });

  // A10: this phase's wsl-lane.js, whose all-seen information line A10 wants (FR-086's five names, spec.md:527-530).
  check('A10 wants the line wsl-lane.js prints for 5/5', () => {
    const wsl = require(path.join(EVID, 'wsl-lane.js'));
    const five = ['prune_removes_real_link_unix', 'posix_modes_and_links_unix', 'special_bits_and_mtime_not_applied_unix',
      'escape_cases_unix', 'symlinked_staging_root_unix'].map((n) => `test extract::tests::${n} ... ok`);
    const v = wsl.verdict([...five, 'test result: ok. 5 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out'].join('\n'), 0);
    eq(WSL_INFO_ALL, 'Linux-run Phase 8 tests seen ok: 5/5');
    eq(wsl.infoLine(v.seen), WSL_INFO_ALL);
  });

  check('status filter drops evidence only', () => eq(withoutEvidence(
    ` M specs/INDEX.md\n?? ${EVID_REL}core-tree-phase8.txt\n?? specs/krate-manager-phase8/plan.md\n`),
  ' M specs/INDEX.md\n?? specs/krate-manager-phase8/plan.md'));
  check('evidence prefix is phase 8', () => eq(EVID_REL, 'specs/krate-manager-phase8/evidence/'));
  check('changed lines', () => eq(changedLines('a\r\nb\r\nc', 'a\nc\nd').sort().join(','), 'b,d'));

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

/** Kills a child and its descendants (bash -> cargo -> rustc, npx -> cmd -> node, node -> wsl). */
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
 * .cmd on Windows); `env` replaces the environment; `cwd` defaults to the repo root.
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
const git = (args, opts) => run('git', ['-c', 'core.quotepath=off', ...args], opts);
const fail = (r) => (r.timedOut ? `TIMEOUT (killed after ${r.wallS}s)` : `exit=${r.code}`);
const green = (r) => !r.timedOut && r.code === 0;
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

function gitStatus() {
  const r = childProcess.spawnSync('git', ['-c', 'core.quotepath=off', 'status', '--porcelain', '--untracked-files=all'],
    { cwd: ROOT, encoding: 'utf8', windowsHide: true });
  if (r.status !== 0) return `git status failed: ${r.stderr}`;
  return withoutEvidence(r.stdout);
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

// ---- A1: the cargo chain (A1j / A1k / A1m first: git only, seconds) ---------------------------------------------
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
  record('A1j', green(d) && green(u) && !readError && hits.length === 0, `SC-001 git diff -U0 ${BASE} --` +
    ` apps/krate-manager -> ${EVID_REL}diff-u0.txt (${lines(d.stdout).length} lines) plus ${entries.length} untracked` +
    ` file(s) read whole${unread.length ? ` (${unread.length} binary skipped: ${unread.join(', ')})` : ''}` +
    `${readError ? ` (UNREADABLE ${readError})` : ''}: ` +
    `${hits.length ? `${hits.length} suppression(s) ADDED: ${hits.slice(0, 10).join(' ; ')}` : 'no added allow( / expect( attribute,' +
      ' @ts-ignore or eslint-disable'} ${fail(d)}/${fail(u)} wall=${wallS}s`, wallS);
}

async function a1kNoAppChange() {
  const t0 = Date.now();
  const d = await git(['diff', '--no-color', '--name-only', BASE, '--', ...FR090_PATHS], { log: 'A1k-fr090-diff.log', ceilingMs: 2 * MIN });
  const u = await git(['ls-files', '--others', '--exclude-standard', '--', ...FR090_PATHS],
    { log: 'A1k-fr090-untracked.log', ceilingMs: 2 * MIN });
  const changed = lines(d.stdout.trim()).filter(Boolean);
  const added = lines(u.stdout.trim()).filter(Boolean);
  const wallS = ((Date.now() - t0) / 1000).toFixed(1);
  record('A1k', green(d) && green(u) && changed.length === 0 && added.length === 0, `FR-090 git diff --name-only ${BASE} --` +
    ` ${FR090_PATHS.join(' ')}: ${changed.length ? changed.join(', ') : 'empty'}; untracked under them:` +
    ` ${added.length ? added.join(', ') : 'empty'} ${fail(d)}/${fail(u)} wall=${wallS}s`, wallS);
}

async function a1mLocks() {
  const t0 = Date.now();
  const w = await git(['diff', '--no-color', '--numstat', BASE, '--', WORKSPACE_LOCK], { log: 'A1m-workspace-lock.log', ceilingMs: 2 * MIN });
  const g = await git(['diff', '--no-color', '--numstat', BASE, '--', GUARD_LOCK], { log: 'A1m-guard-lock.log', ceilingMs: 2 * MIN });
  // stdout only: git's autocrlf notice ("LF will be replaced by CRLF") goes to stderr.
  const v = numstatVerdict(w.stdout, g.stdout);
  const wallS = ((Date.now() - t0) / 1000).toFixed(1);
  record('A1m', green(w) && green(g) && v.ok, `rule 8 / TA-1 git diff --numstat ${BASE}: ${v.detail} ${fail(w)}/${fail(g)}` +
    ` wall=${wallS}s`, wallS);
}

async function a1Cargo() {
  await a1jSuppressions();
  await a1kNoAppChange();
  await a1mLocks();

  let r = await step('cargo fmt', 'A1a-cargo-fmt.log', 5 * MIN);
  record('A1a', green(r), `step cargo fmt last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);

  for (const [id, name, log] of [['A1b', 'clippy', 'A1b-clippy.log'], ['A1c', 'clippy dev-feed', 'A1c-clippy-dev-feed.log']]) {
    r = await step(name, log, 20 * MIN);
    const w = warningLines(r.out);
    record(id, green(r) && w === 0, `step ${name} warning-lines=${w} notices="${notices(r.out)}" ${stepLabel(r)}`, r.wallS);
  }

  r = await step('clippy-guard', 'A1d-clippy-guard.log', 15 * MIN);
  const guardNotice = lines(r.out).find((l) => l.startsWith('::notice title=clippy-guard (Windows)::')) || '';
  const guardTxt = `${LOG_DIR}/guard.txt`;
  let copied = 'guard.txt missing';
  if (fs.existsSync(guardTxt)) {
    const header = `# ${today()} (Windows, Phase 8 close lane A1d, from apps/krate-manager/, RUNNER_TEMP=${LOG_DIR}):` +
      ` manager-ci.yml step clippy-guard, its own run: text (${guardNotice || 'no notice'})\n`;
    fs.writeFileSync(evid('clippy-guard.txt'), header + fs.readFileSync(guardTxt, 'utf8'));
    copied = `copied to ${EVID_REL}clippy-guard.txt`;
  }
  record('A1d', green(r) && guardNotice !== '', `step clippy-guard notice="${guardNotice || notices(r.out)}" ${copied}` +
    ` ${stepLabel(r)}`, r.wallS);

  r = await step('cargo test', 'A1e-cargo-test.log', 20 * MIN);
  const summaries = lines(r.out).filter((l) => /^test result: /.test(l));
  const failedBins = summaries.filter((l) => !/ 0 failed;/.test(l)).length;
  const passed = summaries.reduce((n, l) => n + Number((/ (\d+) passed;/.exec(l) || [0, 0])[1]), 0);
  const testLog = `${LOG_DIR}/cargo-test.txt`;
  if (fs.existsSync(testLog)) fs.copyFileSync(testLog, evid('cargo-test.txt'));
  record('A1e', green(r) && summaries.length > 0 && failedBins === 0, `step cargo test binaries=${summaries.length}` +
    ` passed=${passed} binaries-with-failures=${failedBins} -> ${EVID_REL}cargo-test.txt ${stepLabel(r)}`, r.wallS);

  // SC-018 / FR-081: the workflow's own core dependency tree step.
  r = await step('core dependency tree', 'A1f-core-dependency-tree.log', 5 * MIN);
  const treeNotice = CORE_TREE_NOTICE.exec(r.out.replace(/\r/g, ''));
  const coreTree = `${LOG_DIR}/core-tree.txt`;
  let treeCopied = 'core-tree.txt missing';
  if (fs.existsSync(coreTree)) {
    fs.copyFileSync(coreTree, evid('core-tree.txt'));
    treeCopied = `copied to ${EVID_REL}core-tree.txt`;
  }
  record('A1f', green(r) && Boolean(treeNotice), `step core dependency tree notices="${notices(r.out) || lastLine(r.out)}"` +
    ` ${treeCopied} ${stepLabel(r)}`, r.wallS);

  // FR-081, FR-082 record (plan §7.4 step 6): flate2 in core's normal graph, no tar / tauri / tokio / ... crate.
  const args = ['tree', '-p', 'krate-manager-core', '-e', 'normal', '--locked', '--prefix', 'none', '--format', '{p}'];
  r = await cargo(args, { log: 'A1h-core-tree-phase8.log', ceilingMs: 5 * MIN });
  const text = r.stdout.replace(/\r\n/g, '\n');
  fs.writeFileSync(evid('core-tree-phase8.txt'), text);
  const v = coreTreeVerdict(text);
  record('A1h', green(r) && v.ok, `cargo ${args.join(' ')} -> ${EVID_REL}core-tree-phase8.txt` +
    ` lines=${lines(text.trim()).filter(Boolean).length} ${v.detail} ${stepLabel(r)}`, r.wallS);

  // plan.md:69-79: no target enables flate2's zlib-rs backend, and no target's graph holds a `tar` crate.
  const zArgs = ['tree', '--workspace', '--target', 'all', '-e', 'features', '-i', 'zlib-rs', '--locked'];
  const tArgs = ['tree', '--workspace', '--target', 'all', '-i', 'tar', '--locked'];
  const z = await cargo(zArgs, { log: 'A1l-zlib-rs.log', ceilingMs: 5 * MIN });
  const t = await cargo(tArgs, { log: 'A1l-tar.log', ceilingMs: 5 * MIN });
  const zv = zlibVerdict(z);
  const tv = tarVerdict(t);
  const wallS = (Number(z.wallS) + Number(t.wallS)).toFixed(1);
  record('A1l', !z.timedOut && !t.timedOut && zv.ok && tv.ok, `cargo ${zArgs.join(' ')}: ${zv.detail}; cargo ${tArgs.join(' ')}:` +
    ` ${tv.detail} ${fail(z)}/${fail(t)} wall=${wallS}s logs=${z.log} ${t.log}`, wallS);
}

// ---- A2: the npm chain ------------------------------------------------------------------------------------------
async function a2Npm() {
  if (fs.existsSync(path.join(APP, 'node_modules'))) {
    record('A2a', true, 'node_modules present: step npm ci not run (tasks.md T006 A2)');
  } else {
    const r = await step('npm ci', 'A2a-npm-ci.log', 10 * MIN);
    record('A2a', green(r), `step npm ci last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
  }
  for (const [id, name, ceiling] of [['A2b', 'typecheck', 5], ['A2c', 'lint', 5], ['A2d', 'vitest', 10]]) {
    const r = await step(name, `${id}-${name}.log`, ceiling * MIN);
    record(id, green(r), `step ${name} last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
  }
}

// ---- A3: L-NODE, L-TAP, L-VLINT ---------------------------------------------------------------------------------
async function a3Node() {
  const bad = [];
  for (const f of NODE_CHECK) {
    const c = await node('--check', [f], { log: `A3a-check-${path.basename(f, '.js')}.log`, ceilingMs: 2 * MIN });
    if (!green(c)) bad.push(`${f} ${fail(c)}`);
  }
  record('A3a', bad.length === 0, `node --check on ${NODE_CHECK.length} files: ${bad.length ? bad.join('; ') : 'all ok'}`);

  // Quoted glob, expanded by Node; default concurrency and isolation (files in parallel child processes).
  const r = await run(process.execPath, ['--test', '--test-reporter=tap', 'tools/feed-contract/tests/*.test.js'],
    { log: 'A3b-node-test.log', ceilingMs: 10 * MIN });
  fs.writeFileSync(TAP_FILE, r.out);
  const num = (k) => { const m = r.out.match(new RegExp(`^# ${k} (\\d+)`, 'm')); return m ? Number(m[1]) : NaN; };
  const plan = (r.out.match(/^1\.\.(\d+)$/m) || [])[1];
  const nodeWarnings = lines(r.out).filter((l) => /\(node:\d+\) .*Warning|DeprecationWarning|ExperimentalWarning/.test(l)).length;
  record('A3b', green(r) && num('fail') === 0 && Number(plan) === EXPECTED_PLAN && nodeWarnings === 0,
    `node --test plan=1..${plan} tests=${num('tests')} pass=${num('pass')} fail=${num('fail')} skipped=${num('skipped')}` +
    ` nodeWarnings=${nodeWarnings} tap=${TAP_FILE} ${stepLabel(r)}`, r.wallS);

  const t = await node('specs/krate-manager-phase2/evidence/tap-check.js', [TAP_FILE], { log: 'A3c-tap-check.log', ceilingMs: 2 * MIN });
  const want = `TAP plan=1..${EXPECTED_PLAN} top-level=${EXPECTED_PLAN}`;
  record('A3c', green(t) && t.out.includes(want), `L-TAP want "${want}" ${t.out.includes(want) ? 'printed' : 'NOT printed'}` +
    ` last="${lastLine(t.out)}" ${stepLabel(t)}`, t.wallS);

  const v = await node('tools/lint-manager-version.js', [], { log: 'A3d-version-lint.log', ceilingMs: 2 * MIN });
  record('A3d', green(v) && v.err.trim() === '', `L-VLINT stderr=${v.err.trim() ? `"${lastLine(v.err)}"` : 'none'} ${stepLabel(v)}`,
    v.wallS);
}

// ---- A4..A10: short lanes and L-WSL -----------------------------------------------------------------------------
async function a4Yaml() {
  const wf = 'manager-ci.yml';
  const r = await run('npx --yes yaml@2.6.1 valid', [], {
    log: `A4-yaml-${path.basename(wf, '.yml')}.log`, ceilingMs: 5 * MIN, stdinFile: `.github/workflows/${wf}`, shell: true,
  });
  record('A4', green(r), `L-YAML .github/workflows/${wf} ${stepLabel(r)}`, r.wallS);
}

async function a5Lint() {
  const r = await node('tools/lint-serial-test-runs.js', [], { log: 'A5-lint-serial.log', ceilingMs: 5 * MIN });
  record('A5', green(r), `L-LINT lint-serial-test-runs last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
}

async function a6SpecsIndex() {
  let r = await node('tools/gen-specs-index.js', ['--check'], { log: 'A6-specs-index.log', ceilingMs: 5 * MIN });
  if (green(r)) {
    record('A6', true, `L-INDEX gen-specs-index --check last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
    return;
  }
  // tasks.md A6: red only because this phase's spec files changed the index -> regenerate once and re-check.
  const indexPath = path.join(ROOT, 'specs', 'INDEX.md');
  const before = fs.readFileSync(indexPath, 'utf8');
  const g = await node('tools/gen-specs-index.js', [], { log: 'A6-specs-index-regen.log', ceilingMs: 5 * MIN });
  const changed = changedLines(before, fs.readFileSync(indexPath, 'utf8'));
  const foreign = changed.filter((l) => !l.includes('krate-manager-phase8/'));
  if (!green(g) || foreign.length) {
    fs.writeFileSync(indexPath, before);
    record('A6', false, `L-INDEX --check red and the regeneration ${green(g) ? `changed ${foreign.length} line(s) not naming` +
      ` krate-manager-phase8/ (first: "${foreign[0]}")` : `failed (${fail(g)})`}; specs/INDEX.md restored ${stepLabel(r)}`, r.wallS);
    return;
  }
  r = await node('tools/gen-specs-index.js', ['--check'], { log: 'A6-specs-index-recheck.log', ceilingMs: 5 * MIN });
  record('A6', green(r), `L-INDEX regenerated once (${changed.length} changed line(s), all naming krate-manager-phase8/),` +
    ` then --check last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
}

async function scope(id, log) {
  const r = await node(path.join(EVID, 'scope-check.js'), [], { log, ceilingMs: 2 * MIN });
  record(id, green(r), `L-SCOPE scope-check last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
}

async function a7Scope() {
  const s = await node(path.join(EVID, 'scope-check.js'), ['--self-test'], { log: 'A7a-scope-self-test.log', ceilingMs: 2 * MIN });
  record('A7a', green(s), `scope-check --self-test last="${lastLine(s.out)}" ${stepLabel(s)}`, s.wallS);
  await scope('A7b', 'A7b-scope-check.log');
}

/** A grep that passes only when it matches nothing (exit 1, empty output). */
async function emptyGrep(id, label, cmd, log) {
  const r = await run(BASH, ['-c', cmd], { log, ceilingMs: 2 * MIN });
  const hits = lines(r.out.trim()).filter(Boolean);
  const ok = !r.timedOut && r.code === 1 && hits.length === 0;
  record(id, ok, `${label} exit=${r.code} ${hits.length ? `MATCHED (first 10 of ${hits.length}): ${hits.slice(0, 10).join(' ; ')}`
    : 'printed nothing'} wall=${r.wallS}s log=${r.log}`, r.wallS);
}

async function a10Wsl() {
  const out = `${EVID_REL}wsl-core.txt`;
  const r = await node(path.join(EVID, 'wsl-lane.js'), ['--out', out], { log: 'A10-wsl-lane.log', ceilingMs: 45 * MIN });
  const last = lastLine(r.out);
  const info = lines(r.out).find((l) => l.startsWith('Linux-run Phase 8 tests seen ok:')) || 'no information line';
  const all = info.startsWith(WSL_INFO_ALL);
  record('A10', green(r) && last.startsWith('WSL LANE GREEN') && all, `L-WSL last="${last}" info="${info}"` +
    ` -> ${out} (early signal; closes no PENDING-CI row) ${stepLabel(r)}`, r.wallS);
}

// ---- B: required tests ------------------------------------------------------------------------------------------
async function bRequired() {
  const r = await step('required tests ran', 'B1-required-tests-ran.log', 5 * MIN);
  record('B1', green(r) && r.out.includes(REQUIRED_NOTICE), `step required tests ran notices="${notices(r.out)}"` +
    ` want "${REQUIRED_NOTICE}" ${stepLabel(r)}`, r.wallS);
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

function writeTimings(totalS, verdict) {
  const rows = results.map((r) => `| ${r.id} | ${r.ok ? 'GREEN' : 'RED'} | ${r.wallS || '-'} | ${r.text.split(' wall=')[0].replace(/\|/g, '\\|')} |`);
  const text = [
    '# Krate Manager Phase 8: close-lane wall clocks',
    '',
    `Written by \`evidence/close-lane.js\`, ${new Date().toISOString()}. Total wall ${totalS}s; ${verdict}.` +
      ` Raw logs (removed by C3): \`${LOG_DIR}\`.`,
    'A1 (git scans, then cargo) and A2 (npm) run as two chains beside A3-A7, A9, A10 (A10 is L-WSL, on WSL\'s own' +
      ' cores); B starts after A1 and A2; C runs last. No `tauri build` row (TA-11).',
    '',
    '| Row | Result | Wall (s) | Command |',
    '|---|---|---|---|',
    ...rows,
    '',
  ].join('\n');
  fs.writeFileSync(TIMINGS, text);
}

async function main() {
  fs.mkdirSync(LOG_DIR, { recursive: true });
  const t0 = Date.now();
  console.log(`close lane: logs ${LOG_DIR}`);
  yamlText = fs.readFileSync(WORKFLOW, 'utf8');
  await a0Records();
  const before = gitStatus();
  fs.writeFileSync(`${LOG_DIR}/A0b-git-status.txt`, before);
  record('A0b', !before.startsWith('git status failed'), `git status --porcelain: ${lines(before).filter(Boolean).length} lines` +
    ` (without ${EVID_REL}) file=${LOG_DIR}/A0b-git-status.txt`);

  const a1 = a1Cargo();
  const a2 = a2Npm();
  const b = Promise.all([a1, a2]).then(() => {
    const chain = results.filter((r) => /^A[12][a-z]$/.test(r.id));
    if (chain.every((r) => r.ok)) return bRequired();
    record('B', false, `skipped: not every A1 / A2 row is green (${chain.filter((r) => !r.ok).map((r) => r.id).join(', ')})`);
    return undefined;
  });
  await Promise.all([a1, a2, b, a3Node(), a4Yaml(), a5Lint(), a6SpecsIndex(), a7Scope(),
    emptyGrep('A9', 'TA-1 / rule 9 grep (todo!( / unimplemented!( outside tests)', TA1_GREP, 'A9-ta1-grep.log'),
    a10Wsl()]);

  const after = gitStatus();
  fs.writeFileSync(`${LOG_DIR}/C1-git-status.txt`, after);
  const same = after === before;
  record('C1', same, `git status --porcelain (without ${EVID_REL}) ${same ? 'equal to A0b' : 'CHANGED from A0b'}` +
    ` (${lines(after).filter(Boolean).length} lines) file=${LOG_DIR}/C1-git-status.txt`);
  await scope('C2', 'C2-scope-check.log');
  c3Clean();

  const order = ['A0', 'A0b', 'A1a', 'A1b', 'A1c', 'A1d', 'A1e', 'A1f', 'A1h', 'A1l', 'A1j', 'A1k', 'A1m', 'A2a', 'A2b',
    'A2c', 'A2d', 'A3a', 'A3b', 'A3c', 'A3d', 'A4', 'A5', 'A6', 'A7a', 'A7b', 'A9', 'A10', 'B', 'B1', 'C1', 'C2', 'C3'];
  results.sort((x, y) => order.indexOf(x.id) - order.indexOf(y.id));
  const allGreen = results.every((r) => r.ok);
  const verdict = allGreen ? 'CLOSE LANE GREEN' : 'CLOSE LANE RED';
  const totalS = ((Date.now() - t0) / 1000).toFixed(1);
  const summary = [
    `Krate Manager Phase 8 close lane, ${new Date().toISOString()}, wall=${totalS}s, logs ${LOG_DIR} (removed by C3)`,
    ...results.map((r) => `${r.id.padEnd(5)} ${r.ok ? 'GREEN' : 'RED  '} ${r.text}`),
    verdict,
  ].join('\n');
  fs.writeFileSync(SUMMARY, `${summary}\n`);
  writeTimings(totalS, verdict);
  console.log(`\n${summary}`);
  process.exit(allGreen ? 0 : 1);
}

if (process.argv.includes('--self-test')) process.exit(selfTest());
main();
