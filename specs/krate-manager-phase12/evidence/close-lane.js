#!/usr/bin/env node
// Close lane for Krate Manager Phase 12 (L-CLOSE; tasks.md T015 third bullet, lanes table; spec FR-111, SC-023, SC-024
// local half; plan.md §7.4, §8; TA-1, TA-15, TA-17). Run by the MAIN LOOP at close, never by an implementer or a gate:
// it holds L-WSL and L-HARNESS.
//
// WHY one script: the close rows must come from one run whose steps share a clock and a log directory, with every
// independent chain concurrent (CLAUDE.md "Optimise Every Build and Test Run") and every child under a wall-clock
// ceiling, so a hung step reads RED instead of stalling the close. Concurrency is plan §7.4's: the cargo lints are one
// chain (they serialise on the build lock anyway); after them `cargo test` and L-HARNESS run side by side; the npm / node
// chains run beside the cargo chain (different toolchains); L-WSL beside everything (WSL's own $HOME/km-target); L-REQ
// starts when both chains have written their logs. Adapted from specs/krate-manager-phase11/evidence/close-lane.js (Phase
// 11's close took the wall clocks of specs/krate-manager-phase11/evidence/timings.md): the rows became Phase 12's lanes
// (tasks.md "Lanes", T015), the D9 record and the rsign fixture rows left (no Phase 12 lane), A8 (header-consumers,
// TA-17) came in, and the greps became T015's.
//
// WHY the workflow's own step text for two rows (L-GUARD, L-REQ): the script extracts that step's `run: |` block from
// .github/workflows/manager-ci.yml, removes its indentation and runs it with Git Bash `bash.exe -c`, cwd
// apps/krate-manager/, env RUNNER_OS=Windows and RUNNER_TEMP=<log dir>, so local and CI logic cannot drift. Every other
// row runs the lane's command itself and writes the files the CI steps write into RUNNER_TEMP (cargo-test.txt,
// vitest.json, node-tap.txt), which L-REQ reads.
//
// WHY the TAP plan is read from feed-tools.yml: T016 sets `const PLAN = "1..<P>"` from a measured local run, and "the
// measured count wins" over the expected 69 (TA-15). L-NODE and L-TAP want that pinned <P>; the row notes whether it is 69.
//
// Usage (repo root, Windows dev box; Node only, no dependencies):
//   node specs/krate-manager-phase12/evidence/close-lane.js              # the close (last line GREEN | RED: <rows>)
//   node specs/krate-manager-phase12/evidence/close-lane.js --self-test  # pure checks; no cargo, no npm, no git
// Rows:
//   A0   rustc --version, node --version, df -h of the repo root only (records)
//   A0b  git status --porcelain --untracked-files=all snapshot, kept for C1
//   G    (over `git diff --name-only 7c4b6837` plus untracked files): G1 no todo!( / unimplemented!( outside *_tests.rs
//        and tests/; G2 no added #[allow( / #[expect( (tasks.md Rule 2), @ts-ignore, @ts-nocheck, eslint-disable; G3 no
//        thread-count or file-parallelism flag (the ban lists' homes exempt; Rule 6); G4 `git check-attr eol
//        tools/gen-root-index.js` -> lf (T001 step 14); G5 no file named *.key under apps/krate-manager/ (Rule 1)
//   A1   (one chain, cwd apps/krate-manager/): A1a L-RS cargo fmt --all --check; A1b L-RS clippy --workspace; A1c L-APP;
//        A1d L-DEVFEED; A1e L-MANUAL; A1f L-E2E (the three dummy KRATE_E2E_* values); A1g L-GUARD step `clippy-guard`
//        (guard.txt -> evidence/clippy-guard.txt); then side by side: A1h L-RS cargo test --workspace --locked ->
//        evidence/cargo-test.txt, and H L-HARNESS (specs/krate-manager-phase11/evidence/harness-lane.js; its last line)
//   A2   (one chain, cwd apps/krate-manager/): A2a npm ci only if node_modules is absent; A2b npm run typecheck; A2c npm
//        run lint; A2d L-TS npm test with the JSON reporter -> vitest.json; A2e L-NAPP node --test --test-reporter=tap
//        "scripts/*.test.js" -> node-tap.txt (# fail 0, TA-1's six node names ok once, no Warning)
//   A3   (repo root): A3a node --check on every new / changed .js of the change set (and the four evidence scripts);
//        A3b --self-test of the four evidence scripts; A3c L-NODE (# fail 0, plan = feed-tools.yml PLAN, no Node warning
//        line); A3d L-TAP ("TAP plan=1..<P> top-level=<P>"); A3e L-VLINT
//   A4   L-YAML x4 (release.yml, docs.yml, release-manager.yml, feed-tools.yml); A5 L-LINT; A6 L-INDEX (--check only:
//        T017 regenerates); A7 L-SCOPE; A8 node tools/header-consumers.js --tag '[krate-manager]' -> last line
//        "CONSUMERS OUTSIDE [krate-manager]: none" (TA-17)
//   A10  L-WSL: wsl-lane.js --out evidence/wsl-core.txt; last line WSL LANE GREEN, 13/13 Phase 9+10 and 34/34 Phase 12
//   B2   L-REQ (after A1h and A2): step `required tests ran`, want
//        "::notice title=required tests ran (Windows)::406 cargo + 86 vitest + 6 node" (389 `all` + 17 `windows`, TA-1)
//   C    (last): C1 git status again, minus lines under specs/krate-manager-phase12/evidence/, equal to A0b; C3 this
//        run's raw log directory removed (its own output only)
// Raw logs: F:/tmp/gate/km12-close/<timestamp>/ (removed by C3). Summary: evidence/close-lane.log; wall clocks:
// evidence/timings.md; the lane copies evidence/cargo-test.txt, evidence/clippy-guard.txt, evidence/wsl-core.txt. Last
// line `CLOSE LANE GREEN` exit 0 | `CLOSE LANE RED: <rows>` exit 1. It writes only under
// specs/krate-manager-phase12/evidence/ and F:/tmp/gate/ (wsl-lane.js and harness-lane.js write their logs there too).
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
const WORKFLOW = path.join(ROOT, '.github', 'workflows', 'manager-ci.yml');
const FEED_TOOLS = path.join(ROOT, '.github', 'workflows', 'feed-tools.yml');
const HARNESS_LANE = path.join(ROOT, 'specs', 'krate-manager-phase11', 'evidence', 'harness-lane.js');
const BASH = 'C:/Program Files/Git/bin/bash.exe';
const MIN = 60 * 1000;
const PHASE = 12;
// The scope base (scope-check.js BASE; tasks.md:8-10).
const BASE = '7c4b6837';
// Forward slashes: the directory is also RUNNER_TEMP inside Git Bash.
const LOG_DIR = `F:/tmp/gate/km12-close/${STAMP}`;
const EVID_REL = 'specs/krate-manager-phase12/evidence/';
const APP_REL = 'apps/krate-manager/';
// TA-15: the expected top-level count; the pinned feed-tools.yml PLAN is what L-NODE and L-TAP compare against.
const EXPECTED_PLAN_TA15 = 69;
// tasks.md L-REQ (TA-1): cargo.all 389 + cargo.windows 17; vitest 86; node 6.
const REQUIRED_NOTICE = '::notice title=required tests ran (Windows)::406 cargo + 86 vitest + 6 node';
// TA-1: the six `node` names of tests/required-tests.json (unchanged this phase).
const NODE_NAMES = ['bundle sidecar name per host tuple', 'fixture inno script keeps the plugin directives',
  'write test job prints verbatim windows paths', 'bundle release mode argv', 'bundle release asset table',
  'e2e update server serves only its directory'];
// tasks.md L-E2E: the dummy values of manager-ci.yml's `clippy updater-e2e` step (compiled and linted, never run).
const E2E_ENV = { KRATE_E2E_FEED_URL: 'http://127.0.0.1:9/versions.json', KRATE_E2E_MARKER_DIR: 'never-run',
  KRATE_E2E_OLD_VERSION: '0.0.0' };
const EVIDENCE_SCRIPTS = ['scope-check', 'wsl-lane', 'close-lane', 'rustsec-check'];
const YAMLS = ['release', 'docs', 'release-manager', 'feed-tools'].map((s) => `.github/workflows/${s}.yml`);
// wsl-lane.js INFO_PREFIX / PHASE12_PREFIX with every name seen.
const WSL_INFO_ALL = ['Linux-run Phase 9+10 tests seen ok: 13/13', 'Phase 12 required cargo tests seen ok: 34/34'];
const CONSUMERS_NONE = 'CONSUMERS OUTSIDE [krate-manager]: none';
const EOL_FILE = 'tools/gen-root-index.js';
// The manager-ci.yml steps this script runs (step() refuses any other name; the self-test extracts each one).
const RUN_STEPS = ['clippy-guard', 'required tests ran'];
// G3: assembled so this file never holds them (G3 scans this file too). Thread count (cargo) and file parallelism
// (vitest, node --test), tasks.md Rule 6.
const SERIAL_FLAGS = [['--test', 'threads'].join('-'), ['--no-file', 'parallelism'].join('-'),
  ['--test', 'concurrency=1'].join('-'), ['--test', 'concurrency 1'].join('-')];
// G3: the ban lists' own homes (their patterns and planted mutations hold the flags as data).
const SERIAL_FLAG_EXEMPT = [`${APP_REL}crates/core/tests/injection.rs`, 'tools/feed-contract/tests/manager-ci-shape.test.js',
  'tools/feed-contract/tests/release-manager-shape.test.js', 'tools/feed-contract/tests/hygiene.test.js'];
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

/** The top-level count of feed-tools.yml's TAP check, `const PLAN = "1..<P>";` (T016 pins it), or NaN. */
function feedToolsPlan(yamlText) {
  const m = /^\s*const PLAN = "1\.\.(\d+)";\s*$/m.exec(yamlText.replace(/\r/g, ''));
  return m ? Number(m[1]) : NaN;
}

/** A3a: the .js / .mjs / .cjs paths of the change set, plus the four evidence scripts, deduplicated and sorted. */
function nodeCheckFiles(paths) {
  const own = EVIDENCE_SCRIPTS.map((s) => `${EVID_REL}${s}.js`);
  return [...new Set([...paths.filter((p) => /\.(?:js|mjs|cjs)$/.test(p)), ...own])].sort();
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

/** Untracked files as one new-file -U0 diff ([[path, text], ...]), so suppressionHits reads them like the tracked diff. */
function untrackedDiff(entries) {
  return entries.map(([p, text]) => {
    const body = lines(text);
    if (body.length && body[body.length - 1] === '') body.pop();
    return [`diff --git a/${p} b/${p}`, 'new file (untracked)', '--- /dev/null', `+++ b/${p}`,
      `@@ -0,0 +1,${body.length} @@`, ...body.map((l) => `+${l}`)].join('\n');
  }).join('\n');
}

/** G1: `<path>:<line>` of every todo!( / unimplemented!( in a non-test Rust source ([[path, text], ...]). */
function todoHits(entries) {
  const isTest = (p) => p.endsWith('_tests.rs') || /(?:^|\/)tests\//.test(p);
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

/** G4: whether `git check-attr eol <file>` printed `<file>: eol: lf`. */
function eolIsLf(output, file) {
  return lines(output).map((l) => l.trim()).includes(`${file}: eol: lf`);
}

/** G5: the paths named *.key under the app. */
function keyHits(paths) {
  return paths.filter((p) => p.startsWith(APP_REL) && /\.key$/i.test(p));
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

/** L-GUARD's pass (tasks.md): the Windows notice with `other-errors=0`, and no `::error` line. */
function guardVerdict(out) {
  const notice = lines(out).find((l) => l.startsWith('::notice title=clippy-guard (Windows)::')) || '';
  const errors = lines(out).filter((l) => l.startsWith('::error')).length;
  return { notice, errors, ok: notice !== '' && /\bother-errors=0\b/.test(notice) && errors === 0 };
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

/** The timings table (evidence/timings.md): one row per result, `| id | GREEN/RED | wall | command |`, pipes escaped. */
function timingsMarkdown({ rows, totalS, verdict, logDir, when }) {
  const cell = (s) => s.replace(/\r?\n/g, ' ').replace(/\|/g, '\\|');
  const body = rows.map((r) => `| ${r.id} | ${r.ok ? 'GREEN' : 'RED'} | ${r.wallS || '-'} | ${cell(r.text.split(' wall=')[0])} |`);
  return [
    `# Krate Manager Phase ${PHASE}: close-lane wall clocks`,
    '',
    `Written by \`evidence/close-lane.js\`, ${when}. Total wall ${totalS}s; ${verdict}. Raw logs (removed by C3): \`${logDir}\`.`,
    'G runs first (git listings and file scans). A1 (the cargo lints, then cargo test beside H, L-HARNESS), A2 (npm /' +
      ' node), A3-A8 and A10 (L-WSL, WSL\'s own target dir) run as concurrent chains; B2 (L-REQ) after A1 and A2; C last.',
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

  // The timings table writer.
  const table = timingsMarkdown({
    rows: [{ id: 'A1h', ok: true, wallS: '41.2', text: 'cargo test --workspace --locked passed=400 wall=41.2s log=x' },
      { id: 'B2', ok: false, wallS: '', text: 'step required tests ran notices="a | b"' }],
    totalS: '300.5', verdict: 'CLOSE LANE RED: B2', logDir: 'F:/tmp/gate/km12-close/x', when: '2026-11-02T12:00:00.000Z',
  });
  check('timings: phase 12 title', () => eq(lines(table)[0], '# Krate Manager Phase 12: close-lane wall clocks'));
  check('timings: header row and rule', () => has(table, '\n| Row | Result | Wall (s) | Command |\n|---|---|---|---|\n'));
  check('timings: a green row, command cut at wall=', () =>
    has(table, '\n| A1h | GREEN | 41.2 | cargo test --workspace --locked passed=400 |\n'));
  check('timings: a red row with no wall clock, pipes escaped', () =>
    has(table, '\n| B2 | RED | - | step required tests ran notices="a \\| b" |\n'));
  check('timings: total, verdict and log dir in the preamble', () => {
    has(table, 'Total wall 300.5s; CLOSE LANE RED: B2.');
    has(table, '`F:/tmp/gate/km12-close/x`');
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

  // L-GUARD's pass rule.
  const guardOk = '::notice title=clippy-guard (Windows)::rc=101 disallowed=3 from_os=1 Roots::new=1 std::env::var=1' +
    ' lint-named=5 other-errors=0';
  check('guard: the Phase 11 notice passes', () => eq(guardVerdict(`x\n${guardOk}\n`).ok, true));
  check('guard: other-errors=2 is red', () => eq(guardVerdict(guardOk.replace('other-errors=0', 'other-errors=2')).ok, false));
  check('guard: an ::error line is red', () => eq(guardVerdict(`${guardOk}\n::error title=clippy-guard::x`).ok, false));
  check('guard: no notice is red', () => eq(guardVerdict('rc=101').ok, false));
  check('guard: the macOS notice is not the Windows one', () =>
    eq(guardVerdict(guardOk.replace('(Windows)', '(macOS)')).ok, false));

  // The TAP plan of feed-tools.yml.
  check('plan: parsed from the heredoc line', () =>
    eq(feedToolsPlan('          const lines = 1;\r\n          const PLAN = "1..69";\r\n'), 69));
  check('plan: absent -> NaN', () => eq(Number.isNaN(feedToolsPlan('const PLAN = 69;')), true));
  check('plan: the real feed-tools.yml has one', () =>
    eq(Number.isInteger(feedToolsPlan(fs.readFileSync(FEED_TOOLS, 'utf8'))), true));
  check('plan: TA-15 expects 69', () => eq(EXPECTED_PLAN_TA15, 69));

  // The constants this phase changes, spelled here so a stale Phase 11 value reads red.
  check('phase is 12', () => eq(PHASE, 12));
  check('base is 7c4b6837', () => eq(BASE, '7c4b6837'));
  check('required notice 406 cargo (389 all + 17 windows) + 86 vitest + 6 node (TA-1)', () =>
    eq(REQUIRED_NOTICE, '::notice title=required tests ran (Windows)::406 cargo + 86 vitest + 6 node'));
  check('the required notice matches tests/required-tests.json', () => {
    const req = JSON.parse(fs.readFileSync(path.join(APP, 'tests', 'required-tests.json'), 'utf8'));
    eq(REQUIRED_NOTICE, `::notice title=required tests ran (Windows)::${req.cargo.all.length + req.cargo.windows.length}` +
      ` cargo + ${req.vitest.length} vitest + ${req.node.length} node`);
  });
  check('the six node names (TA-1) equal tests/required-tests.json', () => {
    const req = JSON.parse(fs.readFileSync(path.join(APP, 'tests', 'required-tests.json'), 'utf8'));
    eq(JSON.stringify(req.node), JSON.stringify(NODE_NAMES));
  });
  check('raw log root', () => eq(LOG_DIR.startsWith('F:/tmp/gate/km12-close/'), true));
  check('evidence prefix is phase 12', () => eq(EVID_REL, 'specs/krate-manager-phase12/evidence/'));
  check('the e2e dummy env is manager-ci.yml\'s', () => eq(JSON.stringify(E2E_ENV),
    '{"KRATE_E2E_FEED_URL":"http://127.0.0.1:9/versions.json",' +
    '"KRATE_E2E_MARKER_DIR":"never-run","KRATE_E2E_OLD_VERSION":"0.0.0"}'));
  check('the four workflows L-YAML parses', () => eq(YAMLS.join(','), '.github/workflows/release.yml,' +
    '.github/workflows/docs.yml,.github/workflows/release-manager.yml,.github/workflows/feed-tools.yml'));
  check('A8 wants header-consumers.js\'s none line for the tag (TA-17)', () =>
    eq(CONSUMERS_NONE, 'CONSUMERS OUTSIDE [krate-manager]: none'));
  check('the serial-run flags are assembled, never spelled', () => {
    eq(SERIAL_FLAGS.join(' / '),
      `--test${'-'}threads / --no-file${'-'}parallelism / --test${'-'}concurrency=1 / --test${'-'}concurrency 1`);
    const self = fs.readFileSync(__filename, 'utf8');
    for (const f of SERIAL_FLAGS) eq(self.includes(f), false);
  });
  check('the harness lane script exists (L-HARNESS)', () => eq(fs.existsSync(HARNESS_LANE), true));

  // File sets.
  check('code files: Rust, TS, JS; not specs/', () => {
    eq(isCodeFile('apps/krate-manager/src/settings.ts'), true);
    eq(isCodeFile('apps/krate-manager/crates/core/src/install.rs'), true);
    eq(isCodeFile('tools/gen-root-index.js'), true);
    eq(isCodeFile('apps/krate-manager/eslint.config.mjs'), true);
    eq(isCodeFile('specs/krate-manager-phase12/tasks.md'), false);
    eq(isCodeFile('specs/krate-manager-phase12/evidence/close-lane.js'), false);
    eq(isCodeFile('.github/workflows/release.yml'), false);
  });
  check('flag-scan files: code, workflows, manifests, this phase\'s evidence scripts', () => {
    eq(isFlagScanFile('.github/workflows/release-manager.yml'), true);
    eq(isFlagScanFile('apps/krate-manager/tests/required-tests.json'), true);
    eq(isFlagScanFile('specs/krate-manager-phase12/evidence/wsl-lane.js'), true);
    eq(isFlagScanFile('specs/krate-manager-phase12/plan.md'), false);
  });
  check('node --check set: the change set\'s .js plus the four evidence scripts, once each', () => eq(nodeCheckFiles([
    'tools/gen-root-index.js', 'tools/feed-contract/tests/gen-root-index.test.js', 'apps/krate-manager/src/main.ts',
    'apps/krate-manager/eslint.config.mjs', `${EVID_REL}close-lane.js`, `${EVID_REL}close-lane.log`, 'docs/x.html',
  ]).join(','), ['apps/krate-manager/eslint.config.mjs', `${EVID_REL}close-lane.js`, `${EVID_REL}rustsec-check.js`,
    `${EVID_REL}scope-check.js`, `${EVID_REL}wsl-lane.js`, 'tools/feed-contract/tests/gen-root-index.test.js',
    'tools/gen-root-index.js'].join(',')));

  // G2: the suppression scan (no added suppression is allowed in this phase, tasks.md Rule 2).
  const diff = (file, ...added) => [
    `diff --git a/${file} b/${file}`, 'index 1111111..2222222 100644', `--- a/${file}`, `+++ b/${file}`,
    '@@ -10 +10,0 @@ fn x', '-#[allow(dead_code)]', '@@ -20,0 +20,2 @@ fn y', ...added.map((a) => `+${a}`),
  ].join('\n');
  const rsFile = 'apps/krate-manager/crates/core/src/install.rs';
  const tsFile = 'apps/krate-manager/src/settings.ts';
  check('added #[expect(clippy::x, reason = "y")] -> hit', () =>
    eq(suppressionHits(diff(rsFile, '#[expect(clippy::x, reason = "y")]', 'fn planted() {}')).length, 1));
  check('added #[allow(dead_code)] -> hit', () => eq(suppressionHits(diff(rsFile, '#[allow(dead_code)]')).length, 1));
  check('added #![allow(x)] -> hit', () => eq(suppressionHits(diff(rsFile, '#![allow(x)]')).length, 1));
  check('added #[cfg_attr(test, allow(x))] -> hit', () => eq(suppressionHits(diff(rsFile, '    #[cfg_attr(test, allow(x))]')).length, 1));
  check('added "// see allow(" comment -> none', () => eq(suppressionHits(diff(rsFile, '    // see allow( in the docs')).length, 0));
  check('a guard plant in a Rust string literal -> none', () =>
    eq(suppressionHits(diff(rsFile, '            "#[expect(clippy::all, reason = \\"x\\")]\\nfn planted() {}\\n",')).length, 0));
  check('a Rust guard constant naming @ts-ignore -> none (TS words count in JS / TS only)', () =>
    eq(suppressionHits(diff(rsFile, '    "// @ts-ignore",')).length, 0));
  check('added // @ts-ignore in TS -> hit', () => eq(suppressionHits(diff(tsFile, '// @ts-ignore')).length, 1));
  check('added // @ts-nocheck in TS -> hit', () => eq(suppressionHits(diff(tsFile, '// @ts-nocheck')).length, 1));
  check('added eslint-disable-next-line in JS -> hit', () =>
    eq(suppressionHits(diff('tools/gen-root-index.js', '// eslint-disable-next-line x')).length, 1));
  check('a markdown line naming @ts-ignore -> none', () =>
    eq(suppressionHits(diff('specs/krate-manager-phase12/tasks.md', 'No `// @ts-ignore`.')).length, 0));
  check('removed #[allow(dead_code)] only -> none', () => eq(suppressionHits(diff(rsFile)).length, 0));
  check('untracked file read whole -> its #[allow( reported (CRLF)', () => {
    const hits = suppressionHits(untrackedDiff([['apps/krate-manager/crates/core/src/install_batch_tests.rs',
      'fn a() {}\r\n#[allow(dead_code)]\r\nfn b() {}\r\n'], ['apps/krate-manager/crates/core/src/http.rs', '// allow( here\n']]));
    eq(hits.length, 1);
    eq(hits[0].file, 'apps/krate-manager/crates/core/src/install_batch_tests.rs');
  });

  // G1.
  const rs = (p, text) => [p, text];
  const todoSet = [
    rs('apps/krate-manager/crates/core/src/install.rs', 'fn a() {\n    todo!("T003")\n}\n'),
    rs('apps/krate-manager/crates/core/src/install_batch_tests.rs', 'fn t() { todo!() }\n'),
    rs('apps/krate-manager/crates/core/tests/injection.rs', 'fn t() { unimplemented!() }\n'),
    rs('apps/krate-manager/crates/core/src/platform/mod.rs', 'fn g() {\r\n    unimplemented!("x")\r\n}\r\n'),
    rs('apps/krate-manager/src-tauri/src/lib.rs', '// a todo list, not a todo macro\nfn f() {}\n'),
    rs('apps/krate-manager/scripts/bundle.js', 'todo!(\n'),
  ];
  check('G1 finds the non-test todo!( and unimplemented!(', () =>
    eq(todoHits(todoSet).join(' ; '), 'apps/krate-manager/crates/core/src/install.rs:2 ;' +
      ' apps/krate-manager/crates/core/src/platform/mod.rs:2'));
  check('G1 over clean sources -> none', () => eq(todoHits([todoSet[4]]).length, 0));

  // G3: the serial-run flags.
  const flagSet = [
    rs('apps/krate-manager/crates/core/tests/injection.rs', `    "${SERIAL_FLAGS[0]}",\n`),
    rs('tools/feed-contract/tests/manager-ci-shape.test.js', `  '${SERIAL_FLAGS[1]}',\n`),
    rs('tools/feed-contract/tests/hygiene.test.js', `  const SERIAL_FLAGS = ['${SERIAL_FLAGS[2]}'];\n`),
    rs('apps/krate-manager/package.json', '{ "test": "vitest run" }\n'),
    rs('.github/workflows/release-manager.yml', `      cargo test -- ${SERIAL_FLAGS[0]}=1\n`),
    rs('apps/krate-manager/package.json', `{ "test": "vitest run ${SERIAL_FLAGS[1]}" }\n`),
    rs('.github/workflows/feed-tools.yml', `\n\n        node --test ${SERIAL_FLAGS[2]} x\n`),
    rs('specs/krate-manager-phase12/evidence/x.js', `\n// ${SERIAL_FLAGS[3]}\n`),
  ];
  check('G3 skips the ban lists\' homes, reports the workflows, the package and a script', () =>
    eq(serialFlagHits(flagSet).join(' ; '), '.github/workflows/release-manager.yml:1 ; apps/krate-manager/package.json:1 ;' +
      ' .github/workflows/feed-tools.yml:3 ; specs/krate-manager-phase12/evidence/x.js:2'));
  check('G3 with no exemption reports the homes too', () => eq(serialFlagHits(flagSet, []).length, 7));
  check('G3: --test-concurrency=4 is not a serial flag', () =>
    eq(serialFlagHits([rs('x.yml', `node --test ${['--test', 'concurrency=4'].join('-')}\n`)]).length, 0));

  // G4, G5.
  check('G4: eol lf', () => eq(eolIsLf('tools/gen-root-index.js: eol: lf\n', EOL_FILE), true));
  check('G4: eol lf, CRLF output', () => eq(eolIsLf('tools/gen-root-index.js: eol: lf\r\n', EOL_FILE), true));
  check('G4: eol unspecified is red', () => eq(eolIsLf('tools/gen-root-index.js: eol: unspecified\n', EOL_FILE), false));
  check('G4: eol crlf is red', () => eq(eolIsLf('tools/gen-root-index.js: eol: crlf\n', EOL_FILE), false));
  check('G5: a *.key under the app', () => eq(keyHits(['apps/krate-manager/tests/k.key', 'apps/krate-manager/x.pub',
    'apps/krate-manager/target/y.KEY']).join(), 'apps/krate-manager/tests/k.key,apps/krate-manager/target/y.KEY'));
  check('G5: a *.key outside the app is not this grep\'s (the KAT fixture)', () =>
    eq(keyHits(['tools/feed-contract/fixtures/phase2/kat-secret/kat.key']).length, 0));

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

  // A10, A7: the sibling scripts this lane reads.
  check('A10 wants the lines wsl-lane.js prints when every name is seen', () => {
    const wsl = require(path.join(EVID, 'wsl-lane.js'));
    const log = [...wsl.LINUX_RUN_TESTS, ...wsl.PHASE12_TESTS].map((n) => `test ${n} ... ok`)
      .concat('test result: ok. 47 passed; 0 failed').join('\n');
    const v = wsl.verdict(log, 0);
    eq(wsl.infoLine(v.seen), WSL_INFO_ALL[0]);
    eq(wsl.phase12Line(v.seen12), WSL_INFO_ALL[1]);
    eq(WSL_INFO_ALL[0].startsWith(wsl.INFO_PREFIX) && WSL_INFO_ALL[1].startsWith(wsl.PHASE12_PREFIX), true);
  });
  check('the change set comes from scope-check.js with the same base', () => {
    const s = require(path.join(EVID, 'scope-check.js'));
    eq(s.BASE, BASE);
    eq(s.collectPaths('a\n', '?? b\n').join(), 'a,b');
  });

  check('status filter drops evidence only', () => eq(withoutEvidence(
    ` M specs/INDEX.md\n?? ${EVID_REL}timings.md\n?? specs/krate-manager-phase12/plan.md\n`),
  ' M specs/INDEX.md\n?? specs/krate-manager-phase12/plan.md'));
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

// ---- A0: records -------------------------------------------------------------------------------------------------
async function a0Records() {
  const rc = await run('rustc', ['--version'], { log: 'A0-rustc.log', ceilingMs: 2 * MIN });
  const nv = await run(process.execPath, ['--version'], { log: 'A0-node.log', ceilingMs: 2 * MIN });
  const df = await run(BASH, ['-c', 'df -h .'], { log: 'A0-df.log', ceilingMs: 2 * MIN });
  const dfRow = lastLine(df.out).replace(/\s+/g, ' ');
  record('A0', green(rc) && green(nv) && green(df),
    `rustc="${lastLine(rc.out)}" node="${lastLine(nv.out)}" df(repo root)="${dfRow}"`, rc.wallS);
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
  const gv = guardVerdict(r.out);
  const guardTxt = `${LOG_DIR}/guard.txt`;
  let copied = 'guard.txt missing';
  if (fs.existsSync(guardTxt)) {
    const header = `# ${today()} (Windows, Phase ${PHASE} close lane A1g, from apps/krate-manager/, RUNNER_TEMP=${LOG_DIR}):` +
      ` manager-ci.yml step clippy-guard, its own run: text (${gv.notice || 'no notice'})\n`;
    fs.writeFileSync(evid('clippy-guard.txt'), header + fs.readFileSync(guardTxt, 'utf8'));
    copied = `copied to ${EVID_REL}clippy-guard.txt`;
  }
  record('A1g', green(r) && gv.ok, `L-GUARD step clippy-guard notice="${gv.notice || notices(r.out)}" error-lines=${gv.errors}` +
    ` (want other-errors=0, no ::error) ${copied} ${stepLabel(r)}`, r.wallS);

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
  // harness-lane.js's own ceilings sum to under 2 h even on its WSL path (specs/krate-manager-phase11/evidence/harness-lane.js).
  const r = await node(HARNESS_LANE, ['--log-dir', dir], { log: 'H-harness-lane.log', ceilingMs: 120 * MIN });
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

// ---- A3: node --check, self-tests, L-NODE, L-TAP, L-VLINT -------------------------------------------------------
async function a3Node(checkFiles) {
  // node --check and the self-tests are independent: all at once.
  const checks = await Promise.all(checkFiles.map((f, i) =>
    node('--check', [f], { log: `A3a-check-${i}-${path.basename(f)}.log`, ceilingMs: 2 * MIN }).then((c) => [f, c])));
  const bad = checks.filter(([, c]) => !green(c)).map(([f, c]) => `${f} ${fail(c)} "${lastLine(c.out)}"`);
  record('A3a', checkFiles.length > 0 && bad.length === 0,
    `node --check on ${checkFiles.length} files (the change set's .js and the four evidence scripts): ` +
    `${bad.length ? bad.join('; ') : 'all ok'}`);

  const selfs = await Promise.all(EVIDENCE_SCRIPTS.map((s) =>
    node(path.join(EVID, `${s}.js`), ['--self-test'], { log: `A3b-self-test-${s}.log`, ceilingMs: 2 * MIN }).then((c) => [s, c])));
  const red = selfs.filter(([, c]) => !green(c) || !lastLine(c.out).includes('GREEN'))
    .map(([s, c]) => `${s} ${fail(c)} last="${lastLine(c.out)}"`);
  record('A3b', red.length === 0, `--self-test of ${EVIDENCE_SCRIPTS.join(', ')}: ${red.length ? red.join('; ') : 'all GREEN'}`);

  const pinned = feedToolsPlan(fs.readFileSync(FEED_TOOLS, 'utf8'));
  const pinNote = `feed-tools.yml PLAN=1..${pinned}${pinned === EXPECTED_PLAN_TA15 ? '' : ` (TA-15 expected ${EXPECTED_PLAN_TA15};` +
    ' the measured count wins)'}`;
  const tapFile = `${LOG_DIR}/feed-tap.txt`;
  const r = await run(process.execPath, ['--test', '--test-reporter=tap', 'tools/feed-contract/tests/*.test.js'],
    { log: 'A3c-node-test.log', ceilingMs: 10 * MIN });
  fs.writeFileSync(tapFile, r.stdout);
  const num = (k) => { const m = r.stdout.match(new RegExp(`^# ${k} (\\d+)`, 'm')); return m ? Number(m[1]) : NaN; };
  const plan = Number((r.stdout.match(/^1\.\.(\d+)$/m) || [])[1]);
  const nodeWarnings = lines(r.out).filter((l) => /\(node:\d+\) .*Warning|DeprecationWarning|ExperimentalWarning/.test(l)).length;
  record('A3c', green(r) && num('fail') === 0 && Number.isInteger(pinned) && plan === pinned && nodeWarnings === 0,
    `L-NODE node --test plan=1..${plan} tests=${num('tests')} pass=${num('pass')} fail=${num('fail')}` +
    ` nodeWarnings=${nodeWarnings} want plan = ${pinNote} tap=${tapFile} ${stepLabel(r)}`, r.wallS);

  const t = await node('specs/krate-manager-phase2/evidence/tap-check.js', [tapFile], { log: 'A3d-tap-check.log', ceilingMs: 2 * MIN });
  const want = `TAP plan=1..${pinned} top-level=${pinned}`;
  record('A3d', green(t) && t.out.includes(want), `L-TAP want "${want}" ${t.out.includes(want) ? 'printed' : 'NOT printed'}` +
    ` last="${lastLine(t.out)}" ${stepLabel(t)}`, t.wallS);

  const v = await node('tools/lint-manager-version.js', [], { log: 'A3e-version-lint.log', ceilingMs: 2 * MIN });
  record('A3e', green(v) && v.err.trim() === '', `L-VLINT stderr=${v.err.trim() ? `"${lastLine(v.err)}"` : 'none'} ${stepLabel(v)}`,
    v.wallS);
}

// ---- A4..A8, G, A10, B2 -----------------------------------------------------------------------------------------
async function a4Yaml() {
  // One after another: the first npx call fills the npx cache the next three read.
  for (const [i, file] of YAMLS.entries()) {
    const id = `A4${'abcd'[i]}`;
    const r = await run('npx --yes yaml@2.6.1 valid', [], { log: `${id}-yaml.log`, ceilingMs: 5 * MIN, stdinFile: file, shell: true });
    record(id, green(r), `L-YAML ${file} ${stepLabel(r)}`, r.wallS);
  }
}

async function a5Lint() {
  const r = await node('tools/lint-serial-test-runs.js', [], { log: 'A5-lint-serial.log', ceilingMs: 5 * MIN });
  record('A5', green(r), `L-LINT lint-serial-test-runs last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
}

async function a6SpecsIndex() {
  // --check only: regenerating writes specs/INDEX.md, which is T017's (tasks.md T017), not this lane's.
  const r = await node('tools/gen-specs-index.js', ['--check'], { log: 'A6-specs-index.log', ceilingMs: 5 * MIN });
  record('A6', green(r), `L-INDEX gen-specs-index --check last="${lastLine(r.out)}"` +
    `${green(r) ? '' : ' (stale: T017 regenerates it with node tools/gen-specs-index.js)'} ${stepLabel(r)}`, r.wallS);
}

async function a7Scope() {
  const r = await node(path.join(EVID, 'scope-check.js'), [], { log: 'A7-scope-check.log', ceilingMs: 2 * MIN });
  record('A7', green(r), `L-SCOPE scope-check last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
}

async function a8Consumers() {
  // TA-17: --tag is required (tools/header-consumers.js:22-25); an array argument, so no shell quoting.
  const r = await node('tools/header-consumers.js', ['--tag', '[krate-manager]'], { log: 'A8-header-consumers.log', ceilingMs: 5 * MIN });
  const last = lastLine(r.out);
  record('A8', green(r) && last === CONSUMERS_NONE, `FR-111 / TA-17 node tools/header-consumers.js --tag '[krate-manager]'` +
    ` last="${last}" (want "${CONSUMERS_NONE}") ${stepLabel(r)}`, r.wallS);
}

/** G1-G5 (tasks.md T015): git listings and file scans in this process, seconds. Returns the change set's paths. */
function greps() {
  const scopeCheck = require(path.join(EVID, 'scope-check.js'));
  const nameOnly = gitSync(['diff', '--no-color', '--no-ext-diff', '--no-renames', '--name-only', BASE]);
  const porcelain = gitSync(['status', '--porcelain', '--untracked-files=all']);
  const untrackedList = gitSync(['ls-files', '--others', '--exclude-standard']);
  const diffU0 = gitSync(['diff', '--no-color', '--no-ext-diff', '-U0', BASE]);
  const appKeys = gitSync(['ls-files', '-co', '--', `${APP_REL}*.key`]); // tracked, untracked and ignored
  const eol = gitSync(['check-attr', 'eol', '--', EOL_FILE]);
  if ([nameOnly, porcelain, untrackedList, diffU0, appKeys, eol].some((x) => x === null)) {
    record('G0', false, 'a git listing for the greps failed (diff, status, ls-files, check-attr)');
    return [];
  }
  const paths = scopeCheck.collectPaths(nameOnly, porcelain);
  const entries = readTexts(paths);
  fs.writeFileSync(`${LOG_DIR}/G-diff-u0.txt`, diffU0);
  fs.writeFileSync(`${LOG_DIR}/G-change-set.txt`, `${paths.join('\n')}\n`);

  const g1 = todoHits(entries);
  record('G1', g1.length === 0, `todo!( / unimplemented!( outside *_tests.rs and tests/ in` +
    ` ${entries.filter(([p]) => p.endsWith('.rs')).length} changed .rs files: ${g1.length ? `HIT ${g1.join(' ; ')}` : 'none'}`);

  const untracked = lines(untrackedList).map((l) => l.trim()).filter(Boolean);
  const hits = [...suppressionHits(diffU0), ...suppressionHits(untrackedDiff(readTexts(untracked)))];
  record('G2', hits.length === 0, `Rule 2: added #[allow( / #[expect( / @ts-ignore / @ts-nocheck / eslint-disable over the` +
    ` diff from ${BASE} and ${untracked.length} untracked file(s): ` +
    `${hits.length ? `${hits.length} HIT: ${hits.slice(0, 10).map((h) => `${h.file}: ${h.text}`).join(' ; ')}` : 'none'}`);

  const g3 = serialFlagHits(entries.filter(([p]) => isFlagScanFile(p)));
  record('G3', g3.length === 0, `Rule 6: no ${SERIAL_FLAGS.join(' / ')} in the change set (exempt the ban lists'` +
    ` homes ${SERIAL_FLAG_EXEMPT.join(', ')}): ${g3.length ? `HIT ${g3.join(' ; ')}` : 'none'}`);

  record('G4', eolIsLf(eol, EOL_FILE), `T001 step 14: git check-attr eol ${EOL_FILE} -> "${lastLine(eol)}" (want "${EOL_FILE}: eol: lf")`);

  const keyPaths = lines(appKeys).map((l) => l.trim()).filter(Boolean);
  const g5 = keyHits([...new Set([...keyPaths, ...paths])]);
  record('G5', g5.length === 0, `Rule 1: no file named *.key under ${APP_REL} (tracked, untracked or ignored):` +
    ` ${g5.length ? `HIT ${g5.join(' ; ')}` : 'none'}`);
  return paths;
}

async function a10Wsl() {
  const out = `${EVID_REL}wsl-core.txt`;
  const r = await node(path.join(EVID, 'wsl-lane.js'), ['--out', out], { log: 'A10-wsl-lane.log', ceilingMs: 45 * MIN });
  const last = lastLine(r.out);
  const infos = WSL_INFO_ALL.map((want) => {
    const prefix = want.replace(/ \d+\/\d+$/, '');
    return lines(r.out).find((l) => l.startsWith(prefix)) || `no "${prefix}" line`;
  });
  const allSeen = WSL_INFO_ALL.every((want, i) => infos[i] === want);
  record('A10', green(r) && last.startsWith('WSL LANE GREEN') && allSeen, `L-WSL last="${last}"` +
    ` info="${infos.join(' | ')}" -> ${out} (early signal; closes no PENDING-CI row) ${stepLabel(r)}`, r.wallS);
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
  const before = gitStatus();
  fs.writeFileSync(`${LOG_DIR}/A0b-git-status.txt`, before);
  record('A0b', before !== 'git status failed', `git status --porcelain: ${lines(before).filter(Boolean).length} lines` +
    ` (without ${EVID_REL}) file=${LOG_DIR}/A0b-git-status.txt`);
  const changeSet = greps();

  const wsl = a10Wsl();
  const a1 = a1Cargo();
  const a2 = a2Npm();
  const required = Promise.all([a1, a2]).then(b2Required);
  await Promise.all([a0Records(), wsl, a1, a2, required, a3Node(nodeCheckFiles(changeSet)), a4Yaml(), a5Lint(),
    a6SpecsIndex(), a7Scope(), a8Consumers()]);

  const after = gitStatus();
  fs.writeFileSync(`${LOG_DIR}/C1-git-status.txt`, after);
  const same = after === before;
  record('C1', same, `git status --porcelain (without ${EVID_REL}) ${same ? 'equal to A0b' : 'CHANGED from A0b'}` +
    ` (${lines(after).filter(Boolean).length} lines; changed: ${changedLines(before, after).filter(Boolean).slice(0, 10).join(' ; ') || 'none'})`);
  const totalS = ((Date.now() - t0) / 1000).toFixed(1);
  c3Clean();

  const order = ['A0', 'A0b', 'G0', 'G1', 'G2', 'G3', 'G4', 'G5', 'A1a', 'A1b', 'A1c', 'A1d', 'A1e', 'A1f', 'A1g', 'A1h', 'H',
    'A2a', 'A2b', 'A2c', 'A2d', 'A2e', 'A3a', 'A3b', 'A3c', 'A3d', 'A3e', 'A4a', 'A4b', 'A4c', 'A4d', 'A5', 'A6', 'A7', 'A8',
    'A10', 'B2', 'C1', 'C3'];
  results.sort((x, y) => order.indexOf(x.id) - order.indexOf(y.id));
  const verdict = verdictLine(results);
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

module.exports = { suppressionHits, untrackedDiff, todoHits, serialFlagHits, keyHits, isFlagScanFile, nodeCheckFiles,
  feedToolsPlan, guardVerdict, eolIsLf };
if (require.main === module) {
  if (process.argv.includes('--self-test')) process.exit(selfTest());
  main();
}
