#!/usr/bin/env node
// Close lane for Krate Manager Phase 11 (L-CLOSE; tasks.md T016, T018 steps 1-5; spec FR-092, FR-095, FR-097; SC-007 local
// row, SC-012; plan.md §7.4, §8; TA-1, TA-2, TA-22). Run by the MAIN LOOP at close, never by an implementer or a gate:
// it holds L-WSL and L-HARNESS (whose WSL half, under D9's fallback, is a cold tauri + WebKitGTK build).
//
// WHY one script: the close rows must come from one run whose steps share a clock and a log directory, with every
// independent chain concurrent (CLAUDE.md "Optimise Every Build and Test Run") and every child under a wall-clock
// ceiling, so a hung step reads RED instead of stalling the close. Concurrency is plan §7.4 step 7's: the cargo lints are
// one chain (they serialise on the build lock anyway); after them `cargo test` and L-HARNESS run side by side (one
// target dir: cargo's lock serialises only their compile phases); the npm / node chains run beside the cargo chain; L-WSL
// beside everything (WSL's own $HOME/km-target), and so does L-HARNESS's WSL half under D9's fallback
// ($HOME/km-target-harness, harness-lane.js); L-REQ starts when both chains have written their logs. Adapted from
// specs/krate-manager-phase10/evidence/close-lane.js (header :1-60; Phase 10 took 225.9 s,
// specs/krate-manager-phase10/evidence/timings.md:3): the rows became Phase 11's lanes (tasks.md "Lanes"), L-BUNDLE and
// the helper feature test left (no Phase 11 lane), L-E2E, L-HARNESS and the rsign fixture's self-test came in, and the
// greps became T018 step 5's.
//
// WHY the workflow's own step text for two rows (L-GUARD, L-REQ): the script extracts that step's `run: |` block from
// .github/workflows/manager-ci.yml, removes its indentation and runs it with Git Bash `bash.exe -c`, cwd
// apps/krate-manager/, env RUNNER_OS=Windows and RUNNER_TEMP=<log dir>, so local and CI logic cannot drift. Every other
// row runs the lane's command itself and writes the files the CI steps write into RUNNER_TEMP (cargo-test.txt,
// vitest.json, node-tap.txt), which L-REQ reads.
//
// Usage (repo root, Windows dev box; Node only, no dependencies):
//   node specs/krate-manager-phase11/evidence/close-lane.js              # the close (last line GREEN | RED: <rows>)
//   node specs/krate-manager-phase11/evidence/close-lane.js --self-test  # pure checks; no cargo, no npm, no git
// Rows:
//   A0   rustc --version, node --version, df -h of the repo root only (records); D9 the d9 line (TA-22), well-formed
//   A0b  git status --porcelain --untracked-files=all snapshot, kept for C1
//   A1 (one chain, cwd apps/krate-manager/; plan §7.4 step 1): A1a L-RS cargo fmt --all --check; A1b L-RS clippy
//        --workspace; A1c L-APP; A1d L-DEVFEED; A1e L-MANUAL; A1f L-E2E (the three dummy KRATE_E2E_* values of PD-32);
//        A1g L-GUARD step `clippy-guard` (guard.txt -> evidence/clippy-guard.txt); then side by side: A1h L-RS cargo test
//        --workspace --locked -> cargo-test.txt (evidence copy), and H L-HARNESS (harness-lane.js; its last line)
//   A2 (one chain, cwd apps/krate-manager/; step 4): A2a npm ci only if node_modules is absent; A2b npm run typecheck;
//        A2c npm run lint; A2d L-TS npm test with the JSON reporter -> vitest.json; A2e L-NAPP node --test
//        --test-reporter=tap "scripts/*.test.js" -> node-tap.txt (# fail 0, TA-1's six node names ok once, no Warning)
//   A3 (repo root): A3a node --check on every new / changed .js of the phase; A3b --self-test of the four evidence
//        scripts; A3c L-NODE (# fail 0, plan 1..64, no Node warning line); A3d L-TAP ("TAP plan=1..64 top-level=64");
//        A3e L-VLINT; A3f make-rsign-fixture.js --self-test (never the real run: the fixture is committed)
//   A4 L-YAML x3 (release-manager.yml, manager-ci.yml, feed-tools.yml); A5 L-LINT; A6 L-INDEX (--check only: T018
//        regenerates); A7 L-SCOPE
//   G (T018 step 5, over `git diff --name-only 30878b36` plus untracked files, D-23): G1 no todo!( / unimplemented!(
//        outside *_tests.rs and tests/ (the harness's own tree counts as code); G2 no added lint attribute but the
//        harness build.rs #[expect(clippy::disallowed_methods ...)], no @ts-ignore / @ts-nocheck / eslint-disable; G3 no
//        thread-count / file-parallelism flag (the ban lists' homes exempt); G4 no file named *.key and no secret-key
//        comment under apps/krate-manager/; G5 git check-attr -a: `text: unset` for krate-signing.pub (FR-097) and the three
//        rsign fixture files (TA-2); G6 `git diff 30878b36 -- .gitattributes` adds exactly TA-2's two lines; G7 head -1
//        of the fixture .minisig holds no `secret key` (TA-25); G8 F:/tmp/km11-target is gone (Rule 10)
//   A10 L-WSL: wsl-lane.js --out evidence/wsl-core.txt; last line WSL LANE GREEN and 13/13 Linux-run names
//   B2 L-REQ (after A1h and A2): step `required tests ran`, want
//        "::notice title=required tests ran (Windows)::372 cargo + 70 vitest + 6 node" (355 `all` + 17 `windows`, TA-1)
//   C (last): C1 git status again, minus lines under specs/krate-manager-phase11/evidence/, equal to A0b; C2 L-SCOPE
//        again; C3 this run's raw log directory removed (its own output only)
// Raw logs: F:/tmp/gate/km11-close/<timestamp>/ (removed by C3). Summary: evidence/close-lane.log; wall clocks and the d9
// line: evidence/timings.md. Last line `CLOSE LANE GREEN` exit 0 | `CLOSE LANE RED: <rows>` exit 1. It writes only under
// specs/krate-manager-phase11/evidence/ and F:/tmp/gate/ (wsl-lane.js and harness-lane.js write their logs there too).
'use strict';
const childProcess = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const APP = path.join(ROOT, 'apps', 'krate-manager');
const STAMP = new Date().toISOString().replace(/[-:]/g, '').replace(/\..*/, '').replace('T', '-');
const EVID = __dirname;
const evid = (name) => path.join(EVID, name);
const SUMMARY = evid('close-lane.log');
const TIMINGS = evid('timings.md');
const D9_FILE = evid('d9-windows.txt');
const WORKFLOW = path.join(ROOT, '.github', 'workflows', 'manager-ci.yml');
const BASH = 'C:/Program Files/Git/bin/bash.exe';
const MIN = 60 * 1000;
const PHASE = 11;
// The scope base (scope-check.js BASE; tasks.md:17-23, D-23).
const BASE = '30878b36';
// Forward slashes: the directory is also RUNNER_TEMP inside Git Bash.
const LOG_DIR = `F:/tmp/gate/km11-close/${STAMP}`;
const EVID_REL = 'specs/krate-manager-phase11/evidence/';
const APP_REL = 'apps/krate-manager/';
const HARNESS_REL = `${APP_REL}tests/fixtures/updater-refusals/`;
const RSIGN_REL = `${HARNESS_REL}fixtures/rsign/`;
const RSIGN_FILES = ['package.bin', 'package.bin.minisig', 'rsign.pub'].map((f) => `${RSIGN_REL}${f}`);
const SIGNING_PUB = 'tools/feed-contract/keys/krate-signing.pub';
const HARNESS_BUILD_RS = `${HARNESS_REL}build.rs`;
// plan §4.7 / tasks.md Rule 2: the one new #[expect(...)] and its reason.
const HARNESS_EXPECT_REASON = 'reason = "a build script reads cargo\'s CARGO_CFG_TARGET_OS"';
const KM11_TARGET = 'F:/tmp/km11-target';
const EXPECTED_PLAN = 64; // tasks.md L-TAP; feed-tools.yml PLAN = "1..64" (T017)
// tasks.md L-REQ (TA-1): cargo.all 355 + cargo.windows 17; vitest 70; node 6.
const REQUIRED_NOTICE = '::notice title=required tests ran (Windows)::372 cargo + 70 vitest + 6 node';
// TA-1 / plan §4.13: the six `node` names of tests/required-tests.json.
const NODE_NAMES = ['bundle sidecar name per host tuple', 'fixture inno script keeps the plugin directives',
  'write test job prints verbatim windows paths', 'bundle release mode argv', 'bundle release asset table',
  'e2e update server serves only its directory'];
// PD-32 / TA-14: the dummy values of manager-ci.yml's `clippy updater-e2e` step (compiled and linted, never run).
const E2E_ENV = { KRATE_E2E_FEED_URL: 'http://127.0.0.1:9/versions.json', KRATE_E2E_MARKER_DIR: 'never-run',
  KRATE_E2E_OLD_VERSION: '0.0.0' };
// node --check set: the four evidence scripts, the app scripts (T008, T009 and the unchanged three, L-NAPP), the rsign
// fixture generator (T014) and the phase's tools and their tests (T010, T011, T012, T015, T017).
const NODE_CHECK = [
  ...['close-lane', 'scope-check', 'wsl-lane', 'harness-lane'].map((s) => `${EVID_REL}${s}.js`),
  ...['bundle', 'bundle.test', 'e2e-update-server', 'e2e-update-server.test', 'write-test-job', 'write-test-job.test',
    'inno-fixture.test'].map((s) => `${APP_REL}scripts/${s}.js`),
  `${HARNESS_REL}make-rsign-fixture.js`,
  ...['gen-manager-latest', 'manager-release-preflight', 'feed-publish'].map((s) => `tools/${s}.js`),
  ...['gen-manager-latest', 'manager-release-preflight', 'release-manager-shape', 'feed-publish-collect', 'manager-ci-shape',
    'hygiene'].map((s) => `tools/feed-contract/tests/${s}.test.js`),
];
const SELF_TESTS = ['scope-check', 'wsl-lane', 'harness-lane', 'close-lane'];
const YAMLS = ['release-manager', 'manager-ci', 'feed-tools'].map((s) => `.github/workflows/${s}.yml`);
// wsl-lane.js INFO_PREFIX and LINUX_RUN_TESTS (Phase 10's 13 names; none added in Phase 11).
const WSL_INFO_ALL = 'Linux-run Phase 9+10 tests seen ok: 13/13';
// The manager-ci.yml steps this script runs (step() refuses any other name; the self-test extracts each one).
const RUN_STEPS = ['clippy-guard', 'required tests ran'];
// G3: assembled so this file never holds them (G3 scans this file too).
const SERIAL_FLAGS = [['--test', 'threads'].join('-'), ['--no-file', 'parallelism'].join('-')];
// G3: the ban lists' own homes (their patterns and planted mutations hold the flags as data).
const SERIAL_FLAG_EXEMPT = [`${APP_REL}crates/core/tests/injection.rs`, 'tools/feed-contract/tests/manager-ci-shape.test.js',
  'tools/feed-contract/tests/release-manager-shape.test.js'];
const SUPPRESSION_WORDS = ['@ts-ignore', '@ts-nocheck', 'eslint-disable'];

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
 * The `run: |` block of manager-ci.yml step `name`, with its indentation removed. Refuses a step with its own `if:` or
 * `working-directory:`, whose local run would differ from the Windows leg's.
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

/** Rust sources and the JS / TS the app ships or tests (G1, G2); specs/ (docs, these scripts) is not product code. */
function isCodeFile(p) {
  return !p.startsWith('specs/') && /\.(?:rs|ts|tsx|mts|cts|js|mjs|cjs)$/.test(p);
}

/** G3's file set: code plus workflows and manifests, this phase's evidence scripts included. */
function isFlagScanFile(p) {
  return isCodeFile(p) || (p.startsWith(EVID_REL) && p.endsWith('.js')) || /\.(?:ya?ml|json|toml)$/.test(p);
}

/**
 * What a suppression an added line of `file` holds, or ''. Rust: a line starting `#[` / `#![` that names `allow(` or
 * `expect(` (a plant inside a string literal starts with its quote, or ends with a `\` continuation). JS / TS:
 * `@ts-ignore`, `@ts-nocheck`, `eslint-disable` anywhere on the line.
 */
function suppressionKind(file, content) {
  const t = content.trim();
  if (file.endsWith('.rs')) {
    return /^#!?\[/.test(t) && !t.endsWith('\\') && /\b(?:allow|expect)\s*\(/.test(t) ? 'lint attribute' : '';
  }
  return SUPPRESSION_WORDS.find((w) => t.includes(w)) || '';
}

/** G2: the added lines of a -U0 diff (code files only) that add a suppression, as { file, kind, text }. */
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
    if (!l.startsWith('+') || !isCodeFile(file)) continue;
    const kind = suppressionKind(file, l.slice(1));
    if (kind) hits.push({ file, kind, text: l.slice(1).trim() });
  }
  return hits;
}

/** G2: splits hits into the one allowed (the harness build.rs `#[expect(`) and the rest. */
function suppressionVerdict(hits) {
  const isAllowed = (h) => h.file === HARNESS_BUILD_RS && h.kind === 'lint attribute' && /^#\[expect\(/.test(h.text) &&
    !/\ballow\s*\(/.test(h.text);
  const allowed = hits.filter(isAllowed);
  // Only the first counts as the allowed one; a second #[expect( in build.rs is a new suppression.
  return { allowed: allowed.slice(0, 1), unexpected: [...hits.filter((h) => !isAllowed(h)), ...allowed.slice(1)] };
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

/** G1 (Rule 9): `<path>:<line>` of every todo!( / unimplemented!( in a non-test Rust source ([[path, text], ...]). */
function todoHits(entries) {
  // T018 step 5: the harness lives under tests/fixtures/ but is code (its src/ and build.rs).
  const isTest = (p) => p.endsWith('_tests.rs') || (/(?:^|\/)tests\//.test(p) && !p.startsWith(HARNESS_REL));
  const hits = [];
  for (const [p, text] of entries) {
    if (!p.endsWith('.rs') || isTest(p)) continue;
    lines(text).forEach((l, i) => { if (/\b(?:todo|unimplemented)!\s*\(/.test(l)) hits.push(`${p}:${i + 1}`); });
  }
  return hits;
}

/** G3: `<path>:<line>` of every line holding a serial-run flag, the exempt paths skipped. */
function serialFlagHits(entries, exempt = SERIAL_FLAG_EXEMPT) {
  const hits = [];
  for (const [p, text] of entries) {
    if (exempt.includes(p)) continue;
    lines(text).forEach((l, i) => { if (SERIAL_FLAGS.some((f) => l.includes(f))) hits.push(`${p}:${i + 1}`); });
  }
  return hits;
}

/** G4: paths named *.key, and `<path>:<line>` of every `untrusted comment:` line naming a secret key, under the app. */
function keyHits(paths, entries) {
  const app = (p) => p.startsWith(APP_REL);
  const hits = paths.filter((p) => app(p) && /\.key$/i.test(p));
  for (const [p, text] of entries) {
    if (!app(p)) continue;
    lines(text).forEach((l, i) => {
      const t = l.trim();
      if (t.startsWith('untrusted comment:') && /secret key/i.test(t)) hits.push(`${p}:${i + 1}`);
    });
  }
  return hits;
}

/** G5: the paths whose `git check-attr -a` output lacks `<path>: text: unset`. */
function checkAttrMissing(output, paths) {
  const got = new Set(lines(output).map((l) => l.trim()));
  return paths.filter((p) => !got.has(`${p}: text: unset`));
}

/**
 * L-NAPP: how many TAP lines `^ *ok \d+ - (.*?)(?: # .*)?$` name each of `names`, at any depth, and the TAP `# fail`
 * count (NaN when absent).
 */
function tapNames(tap, names) {
  const okNames = lines(tap).map((l) => /^ *ok \d+ - (.*?)(?: # .*)?$/.exec(l)).filter(Boolean).map((m) => m[1]);
  const failLine = /^# fail (\d+)\s*$/m.exec(tap.replace(/\r/g, ''));
  return { counts: names.map((n) => [n, okNames.filter((x) => x === n).length]), fail: failLine ? Number(failLine[1]) : NaN };
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
 * The timings table (evidence/timings.md): the d9 line in the preamble (T016: "the d9 line copied into timings.md"),
 * then one row per result, `| id | GREEN/RED | wall | command |`, the command cut at its ` wall=` suffix, pipes escaped.
 */
function timingsMarkdown({ rows, totalS, verdict, logDir, when, d9Line }) {
  const cell = (s) => s.replace(/\r?\n/g, ' ').replace(/\|/g, '\\|');
  const body = rows.map((r) => `| ${r.id} | ${r.ok ? 'GREEN' : 'RED'} | ${r.wallS || '-'} | ${cell(r.text.split(' wall=')[0])} |`);
  return [
    `# Krate Manager Phase ${PHASE}: close-lane wall clocks`,
    '',
    `Written by \`evidence/close-lane.js\`, ${when}. Total wall ${totalS}s; ${verdict}. Raw logs (removed by C3): \`${logDir}\`.`,
    `D9 (\`${EVID_REL}d9-windows.txt\`): ${d9Line ? `\`${d9Line}\`` : 'MISSING or malformed'}`,
    'G runs first (git listings and file scans). A1 (the cargo lints, then cargo test beside H, L-HARNESS), A2 (npm /' +
      ' node), A3-A7 and A10 (L-WSL, WSL\'s own target dir) run as concurrent chains; B2 (L-REQ) after A1 and A2; C last.',
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
  check('fail names the exit code', () => eq(fail({ code: 101, timedOut: false, wallS: '3.0' }), 'exit=101'));
  check('fail names the timeout', () => eq(fail({ code: -1, timedOut: true, wallS: '1200.0' }), 'TIMEOUT (killed after 1200.0s)'));
  check('verdict line green', () => eq(verdictLine([{ id: 'A0', ok: true }, { id: 'H', ok: true }]), 'CLOSE LANE GREEN'));
  check('verdict line names the red rows', () =>
    eq(verdictLine([{ id: 'A0', ok: true }, { id: 'A1h', ok: false }, { id: 'B2', ok: false }]), 'CLOSE LANE RED: A1h, B2'));
  check('verdict line with no rows is red', () => eq(verdictLine([]), 'CLOSE LANE RED: no rows'));

  // The timings table writer, with the d9 line.
  const d9Line = 'D9 WINDOWS LOAD FAILURE exit=-1073741511 (0xc0000139) no output';
  const table = timingsMarkdown({
    rows: [{ id: 'A1h', ok: true, wallS: '41.2', text: 'cargo test --workspace --locked passed=400 wall=41.2s log=x' },
      { id: 'B2', ok: false, wallS: '', text: 'step required tests ran notices="a | b"' }],
    totalS: '300.5', verdict: 'CLOSE LANE RED: B2', logDir: 'F:/tmp/gate/km11-close/x', when: '2026-11-01T12:00:00.000Z', d9Line,
  });
  const tl = lines(table);
  check('timings: phase 11 title', () => eq(tl[0], '# Krate Manager Phase 11: close-lane wall clocks'));
  check('timings: the d9 line verbatim', () => has(table, `\nD9 (\`${EVID_REL}d9-windows.txt\`): \`${d9Line}\`\n`));
  check('timings: a missing d9 file says so', () => has(timingsMarkdown({ rows: [], totalS: '1', verdict: 'x', logDir: 'y',
    when: 'z', d9Line: '' }), 'D9 (`specs/krate-manager-phase11/evidence/d9-windows.txt`): MISSING'));
  check('timings: header row and rule', () => has(table, '\n| Row | Result | Wall (s) | Command |\n|---|---|---|---|\n'));
  check('timings: a green row, command cut at wall=', () =>
    has(table, '\n| A1h | GREEN | 41.2 | cargo test --workspace --locked passed=400 |\n'));
  check('timings: a red row with no wall clock, pipes escaped', () =>
    has(table, '\n| B2 | RED | - | step required tests ran notices="a \\| b" |\n'));
  check('timings: total, verdict and log dir in the preamble', () => {
    has(table, 'Total wall 300.5s; CLOSE LANE RED: B2.');
    has(table, '`F:/tmp/gate/km11-close/x`');
  });
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
    has(s, '"$RUNNER_TEMP/node-tap.txt"');
    has(s, '::notice title=required tests ran (${runnerOs})::');
    eq(s.endsWith('\nREQUIRED\n'), true);
  });
  check('real step clippy-guard prints the notice A1g reads', () =>
    has(extractStep(real, 'clippy-guard'), 'echo "::notice title=clippy-guard ($RUNNER_OS)::$msg"'));

  // The constants this phase changes, spelled here so a stale Phase 10 value reads red.
  check('phase is 11', () => eq(PHASE, 11));
  check('base is 30878b36 (D-23)', () => eq(BASE, '30878b36'));
  check('required notice 372 cargo (355 all + 17 windows) + 70 vitest + 6 node (TA-1)', () =>
    eq(REQUIRED_NOTICE, '::notice title=required tests ran (Windows)::372 cargo + 70 vitest + 6 node'));
  check('TAP plan 1..64', () => eq(EXPECTED_PLAN, 64));
  check('the WSL all-seen line is 13/13', () => eq(WSL_INFO_ALL, 'Linux-run Phase 9+10 tests seen ok: 13/13'));
  check('raw log root', () => eq(LOG_DIR.startsWith('F:/tmp/gate/km11-close/'), true));
  check('evidence prefix is phase 11', () => eq(EVID_REL, 'specs/krate-manager-phase11/evidence/'));
  check('the e2e dummy env is PD-32\'s', () => eq(JSON.stringify(E2E_ENV), '{"KRATE_E2E_FEED_URL":"http://127.0.0.1:9/versions.json",' +
    '"KRATE_E2E_MARKER_DIR":"never-run","KRATE_E2E_OLD_VERSION":"0.0.0"}'));
  check('the three workflows L-YAML parses', () => eq(YAMLS.join(','), '.github/workflows/release-manager.yml,' +
    '.github/workflows/manager-ci.yml,.github/workflows/feed-tools.yml'));
  check('the serial-run flags are assembled, never spelled', () => {
    eq(SERIAL_FLAGS.join(' '), `--test${'-'}threads --no-file${'-'}parallelism`);
    const self = fs.readFileSync(__filename, 'utf8');
    for (const f of SERIAL_FLAGS) eq(self.includes(f), false);
  });
  check('the six node names (TA-1) equal tests/required-tests.json', () => {
    const req = JSON.parse(fs.readFileSync(path.join(APP, 'tests', 'required-tests.json'), 'utf8'));
    eq(JSON.stringify(req.node), JSON.stringify(NODE_NAMES));
  });
  check('the rsign fixture files (TA-28)', () => eq(RSIGN_FILES.join(','),
    'apps/krate-manager/tests/fixtures/updater-refusals/fixtures/rsign/package.bin,' +
    'apps/krate-manager/tests/fixtures/updater-refusals/fixtures/rsign/package.bin.minisig,' +
    'apps/krate-manager/tests/fixtures/updater-refusals/fixtures/rsign/rsign.pub'));

  // File sets.
  check('code files: Rust, TS, JS; not specs/', () => {
    eq(isCodeFile('apps/krate-manager/src/manager_update.ts'), true);
    eq(isCodeFile('apps/krate-manager/crates/core/src/self_update.rs'), true);
    eq(isCodeFile('tools/gen-manager-latest.js'), true);
    eq(isCodeFile('apps/krate-manager/eslint.config.mjs'), true);
    eq(isCodeFile('specs/krate-manager-phase11/tasks.md'), false);
    eq(isCodeFile('specs/krate-manager-phase11/evidence/close-lane.js'), false);
    eq(isCodeFile('.github/workflows/manager-ci.yml'), false);
  });
  check('flag-scan files: code, workflows, manifests, this phase\'s evidence scripts', () => {
    eq(isFlagScanFile('.github/workflows/manager-ci.yml'), true);
    eq(isFlagScanFile('apps/krate-manager/package.json'), true);
    eq(isFlagScanFile('apps/krate-manager/Cargo.toml'), true);
    eq(isFlagScanFile('specs/krate-manager-phase11/evidence/harness-lane.js'), true);
    eq(isFlagScanFile('specs/krate-manager-phase11/plan.md'), false);
  });

  // G2: the suppression scan.
  const diff = (file, ...added) => [
    `diff --git a/${file} b/${file}`, 'index 1111111..2222222 100644', `--- a/${file}`, `+++ b/${file}`,
    '@@ -10 +10,0 @@ fn x', '-#[allow(dead_code)]', '@@ -20,0 +20,2 @@ fn y', ...added.map((a) => `+${a}`),
  ].join('\n');
  const rsFile = 'apps/krate-manager/crates/core/src/self_update.rs';
  const tsFile = 'apps/krate-manager/src/manager_update.ts';
  check('added #[expect(clippy::x, reason = "y")] -> hit', () =>
    eq(suppressionHits(diff(rsFile, '#[expect(clippy::x, reason = "y")]', 'fn planted() {}')).length, 1));
  check('added #[cfg_attr(test, allow(x))] -> hit', () => eq(suppressionHits(diff(rsFile, '    #[cfg_attr(test, allow(x))]')).length, 1));
  check('added "// see allow(" comment -> none', () => eq(suppressionHits(diff(rsFile, '    // see allow( in the docs')).length, 0));
  check('a guard plant in a Rust string literal -> none', () =>
    eq(suppressionHits(diff(rsFile, '            "#[expect(clippy::all, reason = \\"x\\")]\\nfn planted() {}\\n",')).length, 0));
  check('a Rust guard constant naming @ts-ignore -> none (TS words count in JS / TS only)', () =>
    eq(suppressionHits(diff(rsFile, '    "// @ts-ignore",')).length, 0));
  check('added // @ts-ignore in TS -> hit', () => eq(suppressionHits(diff(tsFile, '// @ts-ignore')).length, 1));
  check('added // @ts-nocheck in TS -> hit', () => eq(suppressionHits(diff(tsFile, '// @ts-nocheck')).length, 1));
  check('added eslint-disable-next-line in JS -> hit', () =>
    eq(suppressionHits(diff('tools/gen-manager-latest.js', '// eslint-disable-next-line x')).length, 1));
  check('a markdown line naming @ts-ignore -> none', () =>
    eq(suppressionHits(diff('specs/krate-manager-phase11/tasks.md', 'No `// @ts-ignore`.')).length, 0));
  check('removed #[allow(dead_code)] only -> none', () => eq(suppressionHits(diff(rsFile)).length, 0));
  const buildRs = untrackedDiff([[HARNESS_BUILD_RS, 'fn main() {\n    #[expect(\n        clippy::disallowed_methods,\n' +
    `        ${HARNESS_EXPECT_REASON}\n    )]\n    let os = std::env::var("CARGO_CFG_TARGET_OS");\n}\n`]]);
  check('the harness build.rs #[expect( is the one allowed', () => {
    const v = suppressionVerdict(suppressionHits(buildRs));
    eq(v.allowed.length, 1);
    eq(v.unexpected.length, 0);
  });
  check('a second #[expect( in the harness build.rs is unexpected', () => {
    const v = suppressionVerdict(suppressionHits(untrackedDiff([[HARNESS_BUILD_RS,
      '#[expect(clippy::disallowed_methods, reason = "x")]\n#[expect(clippy::other, reason = "y")]\n']])));
    eq(v.allowed.length, 1);
    eq(v.unexpected.length, 1);
  });
  check('an #[allow( in the harness build.rs is unexpected', () => {
    const v = suppressionVerdict(suppressionHits(untrackedDiff([[HARNESS_BUILD_RS, '#[allow(clippy::disallowed_methods)]\n']])));
    eq(v.allowed.length, 0);
    eq(v.unexpected.length, 1);
  });
  check('an #[expect( in the harness main.rs is unexpected', () => eq(suppressionVerdict(suppressionHits(untrackedDiff(
    [[`${HARNESS_REL}src/main.rs`, '#[expect(dead_code, reason = "x")]\n']]))).unexpected.length, 1));
  check('untracked file read whole -> its #[allow( reported (CRLF)', () => {
    const hits = suppressionHits(untrackedDiff([[rsFile, 'fn a() {}\r\n#[allow(dead_code)]\r\nfn b() {}\r\n'],
      ['apps/krate-manager/crates/core/src/http.rs', '// allow( here\n']]));
    eq(hits.length, 1);
    eq(hits[0].file, rsFile);
  });

  // G1 (Rule 9): the harness tree counts as code.
  const rs = (p, text) => [p, text];
  const todoSet = [
    rs('apps/krate-manager/crates/core/src/self_update.rs', 'fn a() {\n    todo!("T003")\n}\n'),
    rs('apps/krate-manager/crates/core/src/self_update_tests.rs', 'fn t() { todo!() }\n'),
    rs('apps/krate-manager/crates/core/tests/injection.rs', 'fn t() { unimplemented!() }\n'),
    rs(`${HARNESS_REL}src/main.rs`, '//! x\n\nfn main() {\n    todo!("T014")\n}\n'),
    rs(`${HARNESS_REL}build.rs`, 'fn main() {\r\n    unimplemented!("x")\r\n}\r\n'),
    rs('apps/krate-manager/src-tauri/src/lib.rs', '// a todo list, not a todo macro\nfn f() {}\n'),
    rs('apps/krate-manager/scripts/bundle.js', 'todo!(\n'),
  ];
  check('G1 finds the non-test todo!( and unimplemented!(, the harness included', () =>
    eq(todoHits(todoSet).join(' ; '), 'apps/krate-manager/crates/core/src/self_update.rs:2 ;' +
      ` ${HARNESS_REL}src/main.rs:4 ; ${HARNESS_REL}build.rs:2`));
  check('G1 over clean sources -> none', () => eq(todoHits([todoSet[5]]).length, 0));

  // G3: the serial-run flags.
  const flagSet = [
    rs('apps/krate-manager/crates/core/tests/injection.rs', `    "${SERIAL_FLAGS[0]}",\n`),
    rs('tools/feed-contract/tests/manager-ci-shape.test.js', `  '${SERIAL_FLAGS[1]}',\n`),
    rs('apps/krate-manager/package.json', '{ "test": "vitest run" }\n'),
    rs('.github/workflows/manager-ci.yml', `      cargo test -- ${SERIAL_FLAGS[0]}=1\n`),
    rs('apps/krate-manager/package.json', `{ "test": "vitest run ${SERIAL_FLAGS[1]}" }\n`),
    rs('specs/krate-manager-phase11/evidence/x.js', `\n// ${SERIAL_FLAGS[0]}\n`),
  ];
  check('G3 skips the ban lists\' homes, reports the workflow, the package and a script', () =>
    eq(serialFlagHits(flagSet).join(' ; '), '.github/workflows/manager-ci.yml:1 ; apps/krate-manager/package.json:1 ;' +
      ' specs/krate-manager-phase11/evidence/x.js:2'));
  check('G3 with no exemption reports the homes too', () => eq(serialFlagHits(flagSet, []).length, 5));

  // G4: key files and secret-key comments under the app.
  check('G4: a *.key under the app', () => eq(keyHits([`${HARNESS_REL}fixtures/rsign/k.key`, 'apps/krate-manager/x.pub'], [])
    .join(), `${HARNESS_REL}fixtures/rsign/k.key`));
  check('G4: a *.key outside the app is not this grep\'s (the KAT fixture)', () =>
    eq(keyHits(['tools/feed-contract/fixtures/phase2/kat-secret/kat.key'], []).length, 0));
  check('G4: rsign\'s default secret-key comment under the app', () => eq(keyHits([], [rs(`${RSIGN_REL}package.bin.minisig`,
    'untrusted comment: signature from rsign secret key\nRUQ=\n')]).join(), `${RSIGN_REL}package.bin.minisig:1`));
  check('G4: TA-25\'s comment is clean', () => eq(keyHits([], [rs(`${RSIGN_REL}package.bin.minisig`,
    'untrusted comment: rsign-format fixture (Krate Manager Phase 11)\r\nRUQ=\r\n')]).length, 0));
  check('G4: a guard constant naming the words is not a comment line', () => eq(keyHits([], [rs(
    'apps/krate-manager/crates/core/tests/injection.rs', '        "{UNTRUSTED_COMMENT} rsign encrypted {SECRET_KEY}\\n{}\\n",\n')])
    .length, 0));

  // G5: git check-attr.
  const attrOut = [`${SIGNING_PUB}: text: unset`, ...RSIGN_FILES.map((f) => `${f}: text: unset`), ''].join('\n');
  check('G5: all four text: unset', () => eq(checkAttrMissing(attrOut, [SIGNING_PUB, ...RSIGN_FILES]).length, 0));
  check('G5: one file without the attribute', () => eq(checkAttrMissing(attrOut.replace(`${RSIGN_FILES[2]}: text: unset\n`, ''),
    [SIGNING_PUB, ...RSIGN_FILES]).join(), RSIGN_FILES[2]));
  check('G5: text: set is not unset', () => eq(checkAttrMissing(`${SIGNING_PUB}: text: set\r\n`, [SIGNING_PUB]).length, 1));

  // L-NAPP (the lenient regex of the `required tests ran` step).
  const tap = ['TAP version 13', `# Subtest: ${NODE_NAMES[0]}`, `ok 1 - ${NODE_NAMES[0]}`, '  ---', '  ...',
    `    ok 1 - ${NODE_NAMES[1]} # TODO later`, `ok 2 - ${NODE_NAMES[1]}`, `not ok 3 - ${NODE_NAMES[2]}`, '1..3',
    '# tests 3', '# pass 2', '# fail 1'].join('\n');
  const t = tapNames(tap, NODE_NAMES);
  check('tapNames: a top-level ok counts', () => eq(t.counts[0][1], 1));
  check('tapNames: an indented ok with a directive counts at any depth', () => eq(t.counts[1][1], 2));
  check('tapNames: not ok does not count', () => eq(t.counts[2][1], 0));
  check('tapNames: the # fail count', () => eq(t.fail, 1));
  check('tapNames: no # fail line -> NaN', () => eq(Number.isNaN(tapNames('ok 1 - x', ['x']).fail), true));

  // A10 and H: the sibling scripts' last lines this lane accepts.
  check('A10 wants the line wsl-lane.js prints for 13/13', () => {
    const wsl = require(path.join(EVID, 'wsl-lane.js'));
    const log = [...wsl.LINUX_RUN_TESTS.map((n) => `test ${n} ... ok`), 'test result: ok. 13 passed; 0 failed'].join('\n');
    eq(wsl.infoLine(wsl.verdict(log, 0).seen), WSL_INFO_ALL);
    eq(WSL_INFO_ALL.startsWith(wsl.INFO_PREFIX), true);
  });
  check('H accepts both green forms of harness-lane.js', () => {
    const h = require(path.join(EVID, 'harness-lane.js'));
    const run = { okCount: 27, wallMs: 10 };
    const rows = [{ id: 'H1', ok: true, text: 'x' }];
    eq(h.finalVerdict({ d9: h.parseD9('D9 WINDOWS LOADS\n'), rows, place: 'Windows', run }).startsWith('HARNESS LANE GREEN'), true);
    eq(h.finalVerdict({ d9: h.parseD9(`${d9Line}\n`), rows, place: 'WSL', run }).startsWith('HARNESS LANE GREEN (WSL'), true);
  });
  check('G6 uses scope-check\'s TA-2 rule', () => {
    const s = require(path.join(EVID, 'scope-check.js'));
    eq(s.BASE, BASE);
    eq(s.gitattributesBreaches('').length, 1);
  });

  check('status filter drops evidence only', () => eq(withoutEvidence(
    ` M specs/INDEX.md\n?? ${EVID_REL}timings.md\n?? specs/krate-manager-phase11/plan.md\n`),
  ' M specs/INDEX.md\n?? specs/krate-manager-phase11/plan.md'));
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

/** Reads the existing text files of `paths` as [[path, text], ...] (binary and deleted files skipped). */
function readTexts(paths) {
  const entries = [];
  for (const p of paths) {
    const abs = path.join(ROOT, p);
    if (!fs.existsSync(abs) || !fs.statSync(abs).isFile()) continue;
    const text = fs.readFileSync(abs, 'utf8');
    if (!text.includes('\0')) entries.push([p, text]);
  }
  return entries;
}

const today = () => new Date().toISOString().slice(0, 10);

// ---- A0: records, D9 ---------------------------------------------------------------------------------------------
async function a0Records() {
  const rc = await run('rustc', ['--version'], { log: 'A0-rustc.log', ceilingMs: 2 * MIN });
  const nv = await run(process.execPath, ['--version'], { log: 'A0-node.log', ceilingMs: 2 * MIN });
  const df = await run(BASH, ['-c', 'df -h .'], { log: 'A0-df.log', ceilingMs: 2 * MIN });
  const dfRow = lastLine(df.out).replace(/\s+/g, ' ');
  record('A0', green(rc) && green(nv) && green(df),
    `rustc="${lastLine(rc.out)}" node="${lastLine(nv.out)}" df(repo root)="${dfRow}"`, rc.wallS);
}

/** The d9 line (TA-22), read once: recorded as a row and copied into timings.md. */
function d9Row() {
  const { parseD9 } = require(path.join(EVID, 'harness-lane.js'));
  const d9 = parseD9(fs.existsSync(D9_FILE) ? fs.readFileSync(D9_FILE, 'utf8') : null);
  record('D9', d9.kind === 'loads' || d9.kind === 'failure', `${EVID_REL}d9-windows.txt: ${d9.kind}` +
    `${d9.line ? ` "${d9.line}"` : ''} (TA-22, T014; a LOAD FAILURE is the recorded Windows gap of SC-007, PD-33)`);
  return d9.kind === 'loads' || d9.kind === 'failure' ? d9.line : '';
}

// ---- A1: the cargo chain, then cargo test beside L-HARNESS -------------------------------------------------------
async function a1Cargo() {
  let r = await cargo(['fmt', '--all', '--check'], { log: 'A1a-cargo-fmt.log', ceilingMs: 5 * MIN });
  record('A1a', green(r), `L-RS cargo fmt --all --check last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);

  const clippies = [
    ['A1b', 'L-RS', ['clippy', '--workspace', '--all-targets', '--locked', '--', '-D', 'warnings']],
    ['A1c', 'L-APP', ['clippy', '-p', 'krate-manager', '--all-targets', '--locked', '--', '-D', 'warnings']],
    ['A1d', 'L-DEVFEED', ['clippy', '-p', 'krate-manager', '--all-targets', '--features', 'dev-feed', '--locked', '--', '-D',
      'warnings']],
    ['A1e', 'L-MANUAL', ['clippy', '-p', 'krate-manager', '--example', 'manual_elevation', '--features', 'manual-elevation',
      '--locked', '--', '-D', 'warnings']],
    ['A1f', 'L-E2E', ['clippy', '-p', 'krate-manager', '--all-targets', '--features', 'updater-e2e', '--locked', '--', '-D',
      'warnings'], E2E_ENV],
  ];
  for (const [id, lane, args, extraEnv] of clippies) {
    const env = extraEnv ? { ...process.env, ...extraEnv } : undefined;
    r = await cargo(args, { log: `${id}-clippy.log`, ceilingMs: 20 * MIN, env });
    const w = warningLines(r.out);
    const envText = extraEnv ? `${Object.entries(extraEnv).map(([k, v]) => `${k}=${v}`).join(' ')} ` : '';
    record(id, green(r) && w === 0, `${lane} ${envText}cargo ${args.join(' ')} warning-lines=${w} ${stepLabel(r)}`, r.wallS);
  }

  r = await step('clippy-guard', 'A1g-clippy-guard.log', 15 * MIN);
  const guardNotice = lines(r.out).find((l) => l.startsWith('::notice title=clippy-guard (Windows)::')) || '';
  const guardTxt = `${LOG_DIR}/guard.txt`;
  let copied = 'guard.txt missing';
  if (fs.existsSync(guardTxt)) {
    const header = `# ${today()} (Windows, Phase ${PHASE} close lane A1g, from apps/krate-manager/, RUNNER_TEMP=${LOG_DIR}):` +
      ` manager-ci.yml step clippy-guard, its own run: text (${guardNotice || 'no notice'})\n`;
    fs.writeFileSync(evid('clippy-guard.txt'), header + fs.readFileSync(guardTxt, 'utf8'));
    copied = `copied to ${EVID_REL}clippy-guard.txt`;
  }
  record('A1g', green(r) && guardNotice !== '', `L-GUARD step clippy-guard notice="${guardNotice || notices(r.out)}"` +
    ` ${copied} ${stepLabel(r)}`, r.wallS);

  await Promise.all([a1hCargoTest(), harnessLane()]);
}

async function a1hCargoTest() {
  const r = await cargo(['test', '--workspace', '--locked'], { log: 'A1h-cargo-test.log', ceilingMs: 25 * MIN });
  fs.writeFileSync(`${LOG_DIR}/cargo-test.txt`, r.out);
  fs.writeFileSync(evid('cargo-test.txt'), r.out);
  const summaries = lines(r.out).filter((l) => /^test result: (ok|FAILED)\./.test(l));
  const failedBins = summaries.filter((l) => !/ 0 failed;/.test(l)).length;
  const passed = summaries.reduce((n, l) => n + Number((/ (\d+) passed;/.exec(l) || [0, 0])[1]), 0);
  record('A1h', green(r) && summaries.length > 0 && failedBins === 0, `L-RS cargo test --workspace --locked` +
    ` binaries=${summaries.length} passed=${passed} binaries-with-failures=${failedBins} -> ${EVID_REL}cargo-test.txt` +
    ` ${stepLabel(r)}`, r.wallS);
}

async function harnessLane() {
  const dir = `${LOG_DIR}/harness`;
  fs.mkdirSync(dir, { recursive: true });
  // harness-lane.js's own ceilings sum to under 2 h even on the WSL path (a cold build, plan §8.4).
  const r = await node(path.join(EVID, 'harness-lane.js'), ['--log-dir', dir], { log: 'H-harness-lane.log', ceilingMs: 120 * MIN });
  const last = lastLine(r.out);
  record('H', green(r) && last.startsWith('HARNESS LANE GREEN'), `L-HARNESS last="${last}" ${stepLabel(r)}`, r.wallS);
}

// ---- A2: the npm / node chain -----------------------------------------------------------------------------------
async function a2Npm() {
  if (fs.existsSync(path.join(APP, 'node_modules'))) {
    record('A2a', true, 'node_modules present: npm ci not run');
  } else {
    const r = await npm('npm ci', { log: 'A2a-npm-ci.log', ceilingMs: 10 * MIN });
    record('A2a', green(r), `npm ci last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
  }
  for (const [id, command, ceiling] of [['A2b', 'npm run typecheck', 5], ['A2c', 'npm run lint', 5]]) {
    const r = await npm(command, { log: `${id}.log`, ceilingMs: ceiling * MIN });
    record(id, green(r), `L-TS ${command} last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
  }
  const vitestJson = `${LOG_DIR}/vitest.json`;
  const command = `npm test -- --reporter=default --reporter=json --outputFile.json=${vitestJson}`;
  let r = await npm(command, { log: 'A2d.log', ceilingMs: 10 * MIN });
  let report = 'vitest.json unreadable';
  let reportOk = false;
  try {
    const j = JSON.parse(fs.readFileSync(vitestJson, 'utf8'));
    reportOk = j.success === true && j.numFailedTests === 0;
    report = `vitest.json success=${j.success} passed=${j.numPassedTests} failed=${j.numFailedTests} total=${j.numTotalTests}`;
  } catch (e) {
    report = `vitest.json unreadable (${e.message})`;
  }
  record('A2d', green(r) && reportOk, `L-TS ${command} ${report} ${stepLabel(r)}`, r.wallS);
  // L-NAPP: quoted glob, expanded by Node; files in parallel child processes (node --test's default).
  r = await run(process.execPath, ['--test', '--test-reporter=tap', 'scripts/*.test.js'],
    { log: 'A2e-node-test-scripts.log', ceilingMs: 5 * MIN, cwd: APP });
  fs.writeFileSync(`${LOG_DIR}/node-tap.txt`, r.stdout);
  const t = tapNames(r.stdout, NODE_NAMES);
  const bad = t.counts.filter(([, c]) => c !== 1);
  const warn = lines(r.out).filter((l) => /Warning:/.test(l)).length;
  record('A2e', green(r) && t.fail === 0 && bad.length === 0 && warn === 0, `L-NAPP node --test --test-reporter=tap` +
    ` "scripts/*.test.js" fail=${t.fail} ${t.counts.map(([n, c]) => `"${n}" ok x${c}`).join(', ')} (want 1 each)` +
    ` warning-lines=${warn} tap=${LOG_DIR}/node-tap.txt ${stepLabel(r)}`, r.wallS);
}

// ---- A3: node --check, self-tests, L-NODE, L-TAP, L-VLINT, the rsign fixture's self-test -----------------------
async function a3Node() {
  const bad = [];
  for (const f of NODE_CHECK) {
    const c = await node('--check', [f], { log: `A3a-check-${path.basename(f, '.js')}.log`, ceilingMs: 2 * MIN });
    if (!green(c)) bad.push(`${f} ${fail(c)} "${lastLine(c.out)}"`);
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

  // Never the real run: the fixture is committed, and a real run would spawn rsign (tasks.md Rule 1).
  const m = await node(`${HARNESS_REL}make-rsign-fixture.js`, ['--self-test'], { log: 'A3f-make-rsign-self-test.log', ceilingMs: 2 * MIN });
  record('A3f', green(m), `node ${HARNESS_REL}make-rsign-fixture.js --self-test last="${lastLine(m.out)}" ${stepLabel(m)}`, m.wallS);
}

// ---- A4..A7, G, A10 ---------------------------------------------------------------------------------------------
async function a4Yaml() {
  // One after another: the first npx call fills the npx cache the next two read.
  for (const [i, file] of YAMLS.entries()) {
    const id = `A4${'abc'[i]}`;
    const r = await run('npx --yes yaml@2.6.1 valid', [], { log: `${id}-yaml.log`, ceilingMs: 5 * MIN, stdinFile: file, shell: true });
    record(id, green(r), `L-YAML ${file} ${stepLabel(r)}`, r.wallS);
  }
}

async function a5Lint() {
  const r = await node('tools/lint-serial-test-runs.js', [], { log: 'A5-lint-serial.log', ceilingMs: 5 * MIN });
  record('A5', green(r), `L-LINT lint-serial-test-runs last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
}

async function a6SpecsIndex() {
  // --check only: regenerating writes specs/INDEX.md, which is T018's (tasks.md T018 step 3), not this lane's.
  const r = await node('tools/gen-specs-index.js', ['--check'], { log: 'A6-specs-index.log', ceilingMs: 5 * MIN });
  record('A6', green(r), `L-INDEX gen-specs-index --check last="${lastLine(r.out)}"` +
    `${green(r) ? '' : ' (stale: T018 regenerates it with node tools/gen-specs-index.js)'} ${stepLabel(r)}`, r.wallS);
}

async function scope(id, log) {
  const r = await node(path.join(EVID, 'scope-check.js'), [], { log, ceilingMs: 2 * MIN });
  record(id, green(r), `L-SCOPE scope-check last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
}

/** G1-G8 (T018 step 5): git listings and file scans in this process, seconds. */
function greps() {
  const scopeCheck = require(path.join(EVID, 'scope-check.js'));
  const nameOnly = gitSync(['diff', '--no-color', '--no-ext-diff', '--no-renames', '--name-only', BASE]);
  const porcelain = gitSync(['status', '--porcelain', '--untracked-files=all']);
  const untrackedList = gitSync(['ls-files', '--others', '--exclude-standard']);
  const diffU0 = gitSync(['diff', '--no-color', '--no-ext-diff', '-U0', BASE]);
  const attrDiff = gitSync(['diff', '--no-color', '--no-ext-diff', BASE, '--', '.gitattributes']);
  const appKeys = gitSync(['ls-files', '-co', '--', `${APP_REL}*.key`]); // tracked, untracked and ignored
  const attrs = gitSync(['check-attr', '-a', '--', SIGNING_PUB, ...RSIGN_FILES]);
  const gitError = [nameOnly, porcelain, untrackedList, diffU0, attrDiff, appKeys, attrs].some((x) => x === null);
  if (gitError) {
    record('G0', false, 'a git listing for the greps failed (diff, status, ls-files, check-attr)');
    return;
  }
  const paths = scopeCheck.collectPaths(nameOnly, porcelain);
  const entries = readTexts(paths);
  fs.writeFileSync(`${LOG_DIR}/G-diff-u0.txt`, diffU0);
  fs.writeFileSync(`${LOG_DIR}/G-change-set.txt`, `${paths.join('\n')}\n`);

  const g1 = todoHits(entries);
  record('G1', g1.length === 0, `Rule 9: todo!( / unimplemented!( outside *_tests.rs and tests/ (the harness tree counts)` +
    ` in ${entries.filter(([p]) => p.endsWith('.rs')).length} changed .rs files: ${g1.length ? `HIT ${g1.join(' ; ')}` : 'none'}`);

  const untracked = lines(untrackedList).map((l) => l.trim()).filter(Boolean);
  const hits = [...suppressionHits(diffU0), ...suppressionHits(untrackedDiff(readTexts(untracked)))];
  const sv = suppressionVerdict(hits);
  const buildRs = entries.find(([p]) => p === HARNESS_BUILD_RS);
  const reasonOk = Boolean(buildRs) && buildRs[1].includes('clippy::disallowed_methods') && buildRs[1].includes(HARNESS_EXPECT_REASON);
  record('G2', sv.unexpected.length === 0 && sv.allowed.length === 1 && reasonOk, `Rule 2 / SC-012: added suppressions` +
    ` over the diff from ${BASE} and ${untracked.length} untracked file(s): ` +
    `${sv.unexpected.length ? `${sv.unexpected.length} UNEXPECTED: ${sv.unexpected.slice(0, 10).map((h) => `${h.file}: ${h.text}`).join(' ; ')}`
      : 'none unexpected'}; the harness build.rs #[expect( x${sv.allowed.length} (want 1),` +
    ` clippy::disallowed_methods + ${HARNESS_EXPECT_REASON} ${reasonOk ? 'present' : 'MISSING'}`);

  const g3 = serialFlagHits(entries.filter(([p]) => isFlagScanFile(p)));
  record('G3', g3.length === 0, `Rule 6: no ${SERIAL_FLAGS.join(' / ')} in the change set (exempt the ban lists'` +
    ` homes ${SERIAL_FLAG_EXEMPT.join(', ')}): ${g3.length ? `HIT ${g3.join(' ; ')}` : 'none'}`);

  const keyPaths = lines(appKeys).map((l) => l.trim()).filter(Boolean);
  const g4 = keyHits([...new Set([...keyPaths, ...paths])], entries);
  record('G4', g4.length === 0, `FR-083 / Rule 1: no *.key under ${APP_REL} (tracked, untracked or ignored) and no` +
    ` secret-key comment in its changed files: ${g4.length ? `HIT ${g4.join(' ; ')}` : 'none'}`);

  const missing = checkAttrMissing(attrs, [SIGNING_PUB, ...RSIGN_FILES]);
  record('G5', missing.length === 0, `FR-097 / TA-2 git check-attr -a: text: unset for ${SIGNING_PUB} and the three` +
    ` rsign fixture files: ${missing.length ? `MISSING for ${missing.join(', ')}` : 'all four'}` +
    ` (output: ${lines(attrs).filter(Boolean).join(' | ')})`);

  const g6 = scopeCheck.gitattributesBreaches(attrDiff);
  record('G6', g6.length === 0, `TA-2: git diff ${BASE} -- .gitattributes adds exactly the two lines:` +
    ` ${g6.length ? g6.join(' ; ') : 'yes'}`);

  const minisig = path.join(ROOT, RSIGN_FILES[1]);
  const first = fs.existsSync(minisig) ? lines(fs.readFileSync(minisig, 'utf8'))[0] : null;
  record('G7', first !== null && first.startsWith('untrusted comment:') && !/secret key/i.test(first),
    `TA-25: head -1 ${RSIGN_FILES[1]} = ${first === null ? 'MISSING FILE' : JSON.stringify(first)}`);

  const slot = fs.existsSync(KM11_TARGET);
  record('G8', !slot, `Rule 10: ${KM11_TARGET} ${slot ? 'STILL EXISTS (the last of T003 / T004 / T005 / T013 deletes it)' : 'gone'}`);
}

async function a10Wsl() {
  const out = `${EVID_REL}wsl-core.txt`;
  const r = await node(path.join(EVID, 'wsl-lane.js'), ['--out', out], { log: 'A10-wsl-lane.log', ceilingMs: 45 * MIN });
  const last = lastLine(r.out);
  const prefix = WSL_INFO_ALL.replace(/ \d+\/\d+$/, '');
  const info = lines(r.out).find((l) => l.startsWith(prefix)) || 'no information line';
  record('A10', green(r) && last.startsWith('WSL LANE GREEN') && info.startsWith(WSL_INFO_ALL), `L-WSL last="${last}"` +
    ` info="${info}" -> ${out} (early signal; closes no PENDING-CI row) ${stepLabel(r)}`, r.wallS);
}

async function b2Required() {
  const missing = ['cargo-test.txt', 'vitest.json', 'node-tap.txt'].filter((f) => !fs.existsSync(`${LOG_DIR}/${f}`));
  const r = await step('required tests ran', 'B2-required-tests-ran.log', 5 * MIN);
  record('B2', green(r) && r.out.includes(REQUIRED_NOTICE), `L-REQ step required tests ran notices="${notices(r.out)}"` +
    ` want "${REQUIRED_NOTICE}"${missing.length ? ` (inputs MISSING: ${missing.join(', ')})` : ''} ${stepLabel(r)}`, r.wallS);
}

/** C3: this run's own raw log directory (it holds nothing the evidence files do not summarise). */
function c3Clean() {
  const t0 = Date.now();
  let ok = true;
  let why = 'removed';
  try {
    fs.rmSync(LOG_DIR, { recursive: true, force: true });
    if (fs.existsSync(LOG_DIR)) { ok = false; why = 'STILL PRESENT'; }
  } catch (e) {
    ok = false;
    why = e.message;
  }
  const wallS = ((Date.now() - t0) / 1000).toFixed(1);
  record('C3', ok, `raw logs ${LOG_DIR} ${why} wall=${wallS}s`, wallS);
}

async function main() {
  fs.mkdirSync(LOG_DIR, { recursive: true });
  const t0 = Date.now();
  console.log(`close lane: logs ${LOG_DIR}`);
  yamlText = fs.readFileSync(WORKFLOW, 'utf8');
  const d9Line = d9Row();
  const before = gitStatus();
  fs.writeFileSync(`${LOG_DIR}/A0b-git-status.txt`, before);
  record('A0b', before !== 'git status failed', `git status --porcelain: ${lines(before).filter(Boolean).length} lines` +
    ` (without ${EVID_REL}) file=${LOG_DIR}/A0b-git-status.txt`);
  greps();

  const wsl = a10Wsl();
  const a1 = a1Cargo();
  const a2 = a2Npm();
  const required = Promise.all([a1, a2]).then(b2Required);
  await Promise.all([a0Records(), wsl, a1, a2, required, a3Node(), a4Yaml(), a5Lint(), a6SpecsIndex(),
    scope('A7', 'A7-scope-check.log')]);

  const after = gitStatus();
  fs.writeFileSync(`${LOG_DIR}/C1-git-status.txt`, after);
  const same = after === before;
  record('C1', same, `git status --porcelain (without ${EVID_REL}) ${same ? 'equal to A0b' : 'CHANGED from A0b'}` +
    ` (${lines(after).filter(Boolean).length} lines; changed: ${changedLines(before, after).filter(Boolean).slice(0, 10).join(' ; ') || 'none'})`);
  await scope('C2', 'C2-scope-check.log');
  const totalS = ((Date.now() - t0) / 1000).toFixed(1);
  c3Clean();

  const order = ['A0', 'D9', 'A0b', 'G0', 'G1', 'G2', 'G3', 'G4', 'G5', 'G6', 'G7', 'G8', 'A1a', 'A1b', 'A1c', 'A1d', 'A1e',
    'A1f', 'A1g', 'A1h', 'H', 'A2a', 'A2b', 'A2c', 'A2d', 'A2e', 'A3a', 'A3b', 'A3c', 'A3d', 'A3e', 'A3f', 'A4a', 'A4b', 'A4c',
    'A5', 'A6', 'A7', 'A10', 'B2', 'C1', 'C2', 'C3'];
  results.sort((x, y) => order.indexOf(x.id) - order.indexOf(y.id));
  const verdict = verdictLine(results);
  const summary = [
    `Krate Manager Phase ${PHASE} close lane, ${new Date().toISOString()}, wall=${totalS}s, logs ${LOG_DIR} (removed by C3)`,
    `D9: ${d9Line || 'MISSING or malformed'}`,
    ...results.map((r) => `${r.id.padEnd(5)} ${r.ok ? 'GREEN' : 'RED  '} ${r.text}`),
    verdict,
  ].join('\n');
  fs.writeFileSync(SUMMARY, `${summary}\n`);
  fs.writeFileSync(TIMINGS, timingsMarkdown({ rows: results, totalS, verdict, logDir: LOG_DIR, when: new Date().toISOString(),
    d9Line }));
  console.log(`\n${summary}`);
  process.exit(verdict === 'CLOSE LANE GREEN' ? 0 : 1);
}

module.exports = { suppressionHits, suppressionVerdict, untrackedDiff, todoHits, serialFlagHits, keyHits, isFlagScanFile };
if (require.main === module) {
  if (process.argv.includes('--self-test')) process.exit(selfTest());
  main();
}
