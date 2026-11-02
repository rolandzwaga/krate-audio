#!/usr/bin/env node
// Close lane for Krate Manager Phase 5 (L-CLOSE; tasks.md T016; spec SC-001, SC-016 local, SC-017, SC-019, SC-023,
// SC-037 Node record, SC-038 records, SC-044 record; plan.md §4.1, §7.4, §8; tasks TA-4, TA-7, TA-18, TA-24). Run by
// the MAIN LOOP at close, never by an implementer or a gate: it holds the release `npm run tauri build` and L-WSL, each
// well past five minutes.
//
// WHY one script: the close rows must come from one run whose steps share a clock and a log directory, with every
// independent step concurrent (CLAUDE.md "Optimise Every Build and Test Run") and every child under a wall-clock
// ceiling, so a hung step reads RED instead of stalling the close. Same form as
// specs/krate-manager-phase4/evidence/close-lane.js.
//
// WHY the workflow's own step text: wherever a row says "step <name>", the script extracts that step's `run: |` block
// from .github/workflows/manager-ci.yml, removes its indentation and runs it with Git Bash `bash.exe -c`, cwd
// apps/krate-manager/ (the job's defaults.run.working-directory), env RUNNER_OS=Windows and RUNNER_TEMP=<log dir>. Local
// and CI logic cannot drift. The step text, as run, is saved beside its log (<log>.sh).
//
// Usage (repo root, Windows dev box; Node only, no dependencies):
//   node specs/krate-manager-phase5/evidence/close-lane.js                       # the close (last line GREEN | RED)
//   node specs/krate-manager-phase5/evidence/close-lane.js --skip-tauri-build    # everything but B2-B7 (last line
//                                                                                 # CLOSE LANE PARTIAL ...)
//   node specs/krate-manager-phase5/evidence/close-lane.js --self-test           # pure checks (extractStep, the
//                                                                                 # cargo tree / L-XCHECK verdicts)
// Rows:
//   A0   rustc --version, node --version, df -h of the repo root only (records only)
//   A0b  git status --porcelain --untracked-files=all snapshot, kept for C1
//   A0c  the base binary size from evidence/base-binary-size.txt (T002, TA-24)
//   A (concurrent):
//     A1 one chain (the cargo commands share one target-dir lock):
//       A1a step `cargo fmt`; A1b step `clippy`; A1c step `clippy dev-feed`; A1d L-LIVE (clippy -p krate-manager-core
//       --features live-network; compiles tests/live_feed.rs, never runs it); A1e step `clippy-guard` (its ::notice;
//       $RUNNER_TEMP/guard.txt copied to evidence/clippy-guard.txt); A1f step `cargo test` ($RUNNER_TEMP/cargo-test.txt
//       copied to evidence/cargo-test.txt); A1g cargo test -p krate-manager-core --locked -- --list >
//       evidence/list-default.txt (0 lines naming live_feed_parses); A1h cargo test -p krate-manager-core --features
//       live-network --test live_feed --locked -- --list --ignored > evidence/list-live.txt (exactly 1); A1i the nine
//       cargo tree records of plan §7.4 (TREES below, each with its check)
//     A2 one chain: A2a step `npm ci` only if node_modules is absent; A2b step `typecheck`; A2c step `lint`; A2d step
//        `vitest`
//     A3 (repo root): A3a node --check on the phase's .js files; A3b node --test --test-reporter=tap
//        "tools/feed-contract/tests/*.test.js" to a TAP file (# fail 0, plan 1..59, no Node warning line); A3c L-TAP on
//        it ("TAP plan=1..59 top-level=59"); A3d L-VLINT; A3e model-range-node.js > evidence/model-range-node.txt
//        (exit 0, six rows findings=0)
//     A4 L-YAML (yaml@2.6.1) on manager-ci.yml and feed-tools.yml
//     A5 L-LINT node tools/lint-serial-test-runs.js
//     A6 L-INDEX node tools/gen-specs-index.js --check; if red only because this phase's spec files changed the index
//        (every changed line names krate-manager-phase5/), regenerate once and re-check; otherwise restore and RED
//     A7 A7a scope-check.js --self-test, then A7b L-SCOPE
//     A8 L-XCHECK (clippy of krate-manager-core for x86_64-unknown-linux-gnu and aarch64-apple-darwin; TA-7): the
//        expected build-script line (`failed to find tool "x86_64-linux-gnu-gcc"` / `failed to find tool "cc"`) is
//        recorded verbatim as the skip; anything else is RED. It starts after A1: cross clippy still builds host build
//        scripts under target/debug, whose lock A1 holds. It overlaps B2, which locks target/release.
//     A9 the SC-021 grep over apps/krate-manager (prints nothing)
//     A10 the TA-4 grep: no todo!( / unimplemented!( in crates/ or src-tauri/src outside *_tests.rs and tests/
//     A11 L-WSL (TA-18): wsl-lane.js --out evidence/wsl-core.txt; last line WSL LANE GREEN and 13/13 cfg(not(windows))
//         tests seen ok (an early signal recorded beside the PENDING-CI rows, never instead of them)
//     A12 (SC-044) A12a grep -n 'rustsec.org' specs/Krate-manager-roadmap.md > evidence/rustsec-line.txt: exactly one
//         line holding rquickjs, rquickjs-core, rquickjs-sys and "pin bump"; A12b git diff --numstat <base> --
//         specs/krate-manager/probes/results.md is "1 0"
//   B (after A1 and A2, only when every A1 / A2 row is green):
//     B1 step `required tests ran` (::notice ...::132 cargo + 3 vitest)
//     B2 step `tauri build` (40 min ceiling); B3 step `build warnings`; B4 step `dev-feed absent`; B5 step `roster
//     absent` (::notice ...::<n >= 9> names, 0 hits in <m> files; output to evidence/roster-absent.txt); B6 step
//     `artifact version` (version.json's version and the NSIS file); B7 ls -l target/release/krate-manager.exe >
//     evidence/binary-size.txt beside A0c's base value (plan §4.1)
//   C (last): C1 git status again, minus lines under specs/krate-manager-phase5/evidence/, equal to A0b (SC-001); C2
//     L-SCOPE again
// Step `Checkout unchanged` is not run locally: the phase's own uncommitted files fail it until the user commits.
// Raw logs: F:/tmp/gate/krate-manager-phase5-close/<timestamp>/. Summary: evidence/close-lane.log (written and printed);
// wall clocks: evidence/timings.md (plan §7.4, for the next run's planning). Exit 0 green (or partial with every run row
// green), 1 red.
'use strict';
const childProcess = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const APP = path.join(ROOT, 'apps', 'krate-manager');
const STAMP = new Date().toISOString().replace(/[-:]/g, '').replace(/\..*/, '').replace('T', '-');
// Forward slashes: the directory is also RUNNER_TEMP inside Git Bash.
const LOG_DIR = `F:/tmp/gate/krate-manager-phase5-close/${STAMP}`;
const EVID = __dirname;
const EVID_REL = 'specs/krate-manager-phase5/evidence/';
const evid = (name) => path.join(EVID, name);
const rel = (f) => path.relative(ROOT, f).replace(/\\/g, '/');
const SUMMARY = evid('close-lane.log');
const TIMINGS = evid('timings.md');
const WORKFLOW = path.join(ROOT, '.github', 'workflows', 'manager-ci.yml');
const BASH = 'C:/Program Files/Git/bin/bash.exe';
const MIN = 60 * 1000;
const SKIP_BUILD = process.argv.includes('--skip-tauri-build');

// The scope base (scope-check.js BASE; tasks.md header: written against feat/krate-manager at a145271b).
const BASE = 'a145271b';
const EXPECTED_PLAN = 59; // tasks.md L-TAP
const REQUIRED_NOTICE = '::notice title=required tests ran (Windows)::132 cargo + 3 vitest'; // tasks.md T016 B1, T017
const TAP_FILE = `${LOG_DIR}/A3b-node-test.tap`;
const XTARGETS = ['x86_64-unknown-linux-gnu', 'aarch64-apple-darwin'];
// plan §1.2 R-8 row (measured) and tasks.md L-XCHECK / TA-7: the C tool each cross target's build script cannot find.
const XTOOL = { 'x86_64-unknown-linux-gnu': 'x86_64-linux-gnu-gcc', 'aarch64-apple-darwin': 'cc' };
// The crates whose build scripts compile C for the target (TA-7: `ring`, and for these targets `rquickjs-sys`).
const XBUILD_SCRIPT_CRATES = ['ring', 'rquickjs-sys'];
// tasks.md T016 A9 (spec SC-021), with the two --exclude-dir options moved before `--`: in the task's order they follow
// the end-of-options marker, so grep reads them as file operands and walks node_modules and target (measured by the
// Phase 4 close lane, 2026-10-30: "grep: --exclude-dir=node_modules: No such file or directory", exit 2).
const SC021_GREP = 'grep -rnE --exclude-dir=node_modules --exclude-dir=target' +
  " -- '--test-threads|--no-file-parallelism|fileParallelism|maxWorkers|pool:' apps/krate-manager";
// tasks.md T016 A10 (TA-4): *_tests.rs and */tests/* excluded.
const TA4_GREP = "grep -rnE --include='*.rs' --exclude='*_tests.rs' --exclude-dir=tests" +
  " -- 'todo!\\(|unimplemented!\\(' apps/krate-manager/crates apps/krate-manager/src-tauri/src";
// L-NODE's node --check set: this phase's new or changed .js files.
const NODE_CHECK = [
  'specs/krate-manager-phase5/evidence/close-lane.js',
  'specs/krate-manager-phase5/evidence/model-range-node.js',
  'specs/krate-manager-phase5/evidence/scope-check.js',
  'specs/krate-manager-phase5/evidence/wsl-lane.js',
  'tools/feed-contract/tests/manager-ci-shape.test.js',
];
// plan §4.13 / wsl-lane.js NON_WINDOWS_TESTS: the thirteen cfg(not(windows)) tests.
const NON_WINDOWS_COUNT = 13;

// plan §7.4: the nine cargo tree records (all --locked, tasks.md rule 8), each with its tasks.md T016 A1 check.
const TREES = [
  ['tree-core.txt', 'core', ['-p', 'krate-manager-core', '-e', 'normal']],
  ['tree-core-windows.txt', 'core-windows', ['-p', 'krate-manager-core', '-e', 'normal', '--target', 'x86_64-pc-windows-msvc']],
  ['tree-core-linux.txt', 'core-linux', ['-p', 'krate-manager-core', '-e', 'normal', '--target', 'x86_64-unknown-linux-gnu']],
  ['tree-app.txt', 'app', ['-p', 'krate-manager', '-e', 'normal', '--target', 'all']],
  ['tree-rquickjs-features-linux.txt', 'features',
    ['-p', 'krate-manager-core', '-e', 'features', '-i', 'rquickjs', '--target', 'x86_64-unknown-linux-gnu']],
  ['tree-rquickjs-features-macos.txt', 'features',
    ['-p', 'krate-manager-core', '-e', 'features', '-i', 'rquickjs', '--target', 'aarch64-apple-darwin']],
  ['tree-webpki.txt', 'webpki', ['-p', 'krate-manager-core', '-e', 'features', '-i', 'webpki-roots']],
  ['tree-dups.txt', 'dups', ['--workspace', '-d', '--target', 'all']],
  ['tree-core-foundation.txt', 'core-foundation', ['--workspace', '-i', 'core-foundation', '--target', 'all']],
];
const CORE_BANNED = /(?:^|[\s─])(tauri[\w-]*|tokio[\w-]*|reqwest[\w-]*|hyper[\w-]*|dirs[\w-]*|directories[\w-]*|home) v\d/;

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

/** The distinct versions of crate `crate` (exact name: `objc2-core-foundation` is not `core-foundation`). */
function versionsOf(text, crate) {
  const re = new RegExp(`(?:^|[\\s─])${crate.replace(/[-]/g, '\\-')} v(\\d[^\\s]*)`, 'gm');
  const set = new Set();
  for (let m = re.exec(text); m; m = re.exec(text)) set.add(m[1]);
  return [...set];
}

/** tasks.md T016 A1: the check of one cargo tree record, over its stdout. Returns { ok, detail }. */
function treeVerdict(kind, text) {
  const v = (crate) => versionsOf(text, crate);
  const bad = [];
  const want = (cond, what) => { if (!cond) bad.push(what); };
  switch (kind) {
    case 'core': {
      want(v('ureq').includes('3.5.0'), 'ureq v3.5.0');
      want(v('rustls').length === 1, `exactly one rustls version (got ${v('rustls').join(',') || 'none'})`);
      want(v('rustls-platform-verifier').includes('0.7.1'), 'rustls-platform-verifier v0.7.1');
      want(v('ring').length > 0, 'ring');
      want(v('webpki-roots').length > 0, 'webpki-roots');
      want(!/rquickjs/.test(text), 'no rquickjs');
      const banned = lines(text).filter((l) => CORE_BANNED.test(l)).map((l) => l.replace(/^[\s│├└─]*/, ''));
      want(banned.length === 0, `none of tauri, tokio, reqwest, hyper, dirs, directories, home (found ${banned.join('; ')})`);
      break;
    }
    case 'core-windows':
      want(text.startsWith('krate-manager-core v'), 'root krate-manager-core');
      want(!/rquickjs/.test(text), 'no line containing rquickjs');
      break;
    case 'core-linux':
      for (const c of ['rquickjs', 'rquickjs-core', 'rquickjs-sys']) want(v(c).join(',') === '0.14.0', `${c} v0.14.0`);
      break;
    case 'app':
      want(v('windows-sys').length > 0, 'windows-sys');
      want(v('system-configuration').join(',') === '0.8.0', 'system-configuration v0.8.0');
      want(v('core-foundation').length === 1, `exactly one core-foundation version (got ${v('core-foundation').join(',') || 'none'})`);
      break;
    case 'features': {
      const feats = [...new Set([...text.matchAll(/rquickjs feature "([^"]+)"/g)].map((m) => m[1]))];
      want(text.startsWith('rquickjs v0.14.0'), 'root rquickjs v0.14.0');
      want(feats.includes('std') && feats.every((f) => f === 'default' || f === 'std'),
        `feature std only (default enables it; got ${feats.join(',') || 'none'})`);
      break;
    }
    case 'webpki':
      want(text.startsWith('webpki-roots v'), 'root webpki-roots (compiled in)');
      break;
    case 'dups': {
      const top = lines(text).filter((l) => /^(rustls|core-foundation) v/.test(l));
      want(top.length === 0, `no second rustls or core-foundation (found ${top.join('; ')})`);
      break;
    }
    case 'core-foundation':
      want(v('core-foundation').length === 1, `one core-foundation version (got ${v('core-foundation').join(',') || 'none'})`);
      break;
    default:
      bad.push(`unknown tree kind ${kind}`);
  }
  return { ok: bad.length === 0, detail: bad.length ? `MISSING: ${bad.join(' | ')}` : 'checks ok' };
}

/** tasks.md L-XCHECK / TA-7: the build-script skip, recorded verbatim, or RED. Returns { ok, detail }. */
function xcheckVerdict(target, code, timedOut, text) {
  if (timedOut) return { ok: false, detail: 'TIMEOUT' };
  const ls = lines(plain(text));
  const want = `failed to find tool "${XTOOL[target]}"`;
  const toolLine = ls.find((l) => l.includes(want));
  const scripts = ls.filter((l) => /failed to run custom build command for `/.test(l));
  const foreign = scripts.filter((l) => {
    const m = /for `([\w-]+) v/.exec(l);
    return !m || !XBUILD_SCRIPT_CRATES.includes(m[1]);
  });
  const compileErrors = ls.filter((l) => /^error\[E\d+\]|^error: could not compile/.test(l));
  if (code !== 0 && toolLine && scripts.length && !foreign.length && !compileErrors.length) {
    return { ok: true, detail: `SKIPPED (TA-7 build-script skip, verbatim: "${toolLine.trim()}"; ${scripts.map((s) => s.trim()).join('; ')})` };
  }
  const why = code === 0 ? 'clippy exited 0: the TA-7 build-script failure was expected, so the box changed; re-measure'
    : !toolLine ? `no "${want}" line`
      : foreign.length ? `other build script failed: ${foreign.join('; ')}`
        : compileErrors.length ? `compile error: ${compileErrors.slice(0, 3).join('; ')}` : 'no build-script failure line';
  return { ok: false, detail: `RED (${why})` };
}

/** git status --porcelain lines, minus this phase's evidence outputs (tasks.md T016 C1). */
function withoutEvidence(statusText) {
  return lines(statusText).filter((l) => l && !l.slice(3).replace(/^"/, '').startsWith(EVID_REL)).join('\n');
}

/** The byte size from an `ls -l ... target/release/krate-manager.exe` line, or NaN. */
function parseLsSize(text) {
  // The size is the field before the date: long-iso `2026-10-30 22:21` or the default `Oct 31 12:01` / `Oct 31 2025`.
  const date = String.raw`(?:\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}|[A-Z][a-z]{2}\s+\d{1,2}\s+(?:\d{2}:\d{2}|\d{4}))`;
  const m = new RegExp(String.raw`\s(\d+)\s+${date}\s+target\/release\/krate-manager\.exe\s*$`, 'm').exec(text);
  return m ? Number(m[1]) : NaN;
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
  const check = (name, fn) => { try { fn(); } catch (e) { failures.push(`${name}: ${e.message}`); } };
  const eq = (got, want) => { if (got !== want) throw new Error(`got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`); };
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

  const coreTree = [
    'krate-manager-core v0.1.0 (F:\\x)',
    '├── rustls v0.23.46',
    '├── ureq v3.5.0',
    '│   ├── ring v0.17.14',
    '│   ├── rustls v0.23.46 (*)',
    '│   ├── rustls-platform-verifier v0.7.1',
    '│   ├── rustls-webpki v0.103.16',
    '│   └── webpki-roots v1.0.11',
    '└── objc2-core-foundation v0.3.2',
  ].join('\n');
  check('versionsOf exact crate', () => eq(versionsOf(coreTree, 'rustls').join(','), '0.23.46'));
  check('versionsOf ignores a longer name', () => eq(versionsOf(coreTree, 'core-foundation').length, 0));
  check('core tree passes', () => eq(treeVerdict('core', coreTree).ok, true));
  check('core tree with tokio fails', () => eq(treeVerdict('core', `${coreTree}\n    └── tokio v1.0.0`).ok, false));
  check('core tree with two rustls fails', () => eq(treeVerdict('core', `${coreTree}\n└── rustls v0.22.0`).ok, false));
  check('windows tree with rquickjs fails', () =>
    eq(treeVerdict('core-windows', 'krate-manager-core v0.1.0\n└── rquickjs v0.14.0').ok, false));
  const feats = 'rquickjs v0.14.0\n├── rquickjs feature "default"\n└── rquickjs feature "std"\n';
  check('features std only passes', () => eq(treeVerdict('features', feats).ok, true));
  check('extra feature fails', () => eq(treeVerdict('features', `${feats}└── rquickjs feature "futures"\n`).ok, false));
  check('dups with a second rustls fails', () => eq(treeVerdict('dups', 'base64 v0.21.7\nrustls v0.22.0\n').ok, false));
  check('dups ignores objc2-core-foundation', () => eq(treeVerdict('dups', 'objc2-core-foundation v0.3.2\n').ok, true));

  const skipLog = 'error: failed to run custom build command for `ring v0.17.14`\n' +
    '  error occurred in cc-rs: failed to find tool "x86_64-linux-gnu-gcc": program not found\n';
  check('xcheck build-script skip', () => eq(xcheckVerdict('x86_64-unknown-linux-gnu', 101, false, skipLog).ok, true));
  check('xcheck wrong tool is red', () => eq(xcheckVerdict('aarch64-apple-darwin', 101, false, skipLog).ok, false));
  check('xcheck compile error is red', () =>
    eq(xcheckVerdict('x86_64-unknown-linux-gnu', 101, false, `${skipLog}error[E0425]: cannot find value\n`).ok, false));
  check('xcheck other build script is red', () => eq(xcheckVerdict('x86_64-unknown-linux-gnu', 101, false,
    `${skipLog}error: failed to run custom build command for \`libz-sys v1.0.0\`\n`).ok, false));
  check('xcheck exit 0 is red', () => eq(xcheckVerdict('x86_64-unknown-linux-gnu', 0, false, '').ok, false));

  check('status filter drops evidence only', () => eq(withoutEvidence(
    ` M specs/INDEX.md\n?? ${EVID_REL}tree-core.txt\n?? specs/krate-manager-phase5/plan.md\n`),
  ' M specs/INDEX.md\n?? specs/krate-manager-phase5/plan.md'));
  check('ls size parsed', () => eq(parseLsSize(
    '-rwxr-xr-x 2 scab 197609 4269056 2026-10-30 22:21 target/release/krate-manager.exe'), 4269056));
  check('ls size parsed (default time style)', () => eq(parseLsSize(
    '-rwxr-xr-x 1 scab 197609 5300000 Oct 31 12:01 target/release/krate-manager.exe'), 5300000));
  check('changed lines', () => eq(changedLines('a\r\nb\r\nc', 'a\nc\nd').sort().join(','), 'b,d'));

  for (const f of failures) console.log(`self-test FAIL ${f}`);
  console.log(failures.length ? `CLOSE LANE SELF-TEST RED (${failures.length} failed)`
    : 'CLOSE LANE SELF-TEST GREEN (6 extractStep, 10 tree, 5 xcheck, 4 helper cases)');
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
 * { code, timedOut, wallS, out, stdout, err, log }. `stdinFile` feeds a file to stdin; `shell` is only for npx (a .cmd
 * on Windows); `env` replaces the environment; `cwd` defaults to the repo root.
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
const fail = (r) => (r.timedOut ? `TIMEOUT (killed after ${r.wallS}s)` : `exit=${r.code}`);
const green = (r) => !r.timedOut && r.code === 0;
const stepLabel = (r) => `${fail(r)} wall=${r.wallS}s log=${r.log}`;

let yamlText = '';
/** Runs manager-ci.yml step `name` with Git Bash from apps/krate-manager/. */
function step(name, log, ceilingMs) {
  let script;
  try {
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
let baseSize = NaN;

// ---- A0: records ------------------------------------------------------------------------------------------------
async function a0Records() {
  const rc = await run('rustc', ['--version'], { log: 'A0-rustc.log', ceilingMs: 2 * MIN });
  const nv = await run(process.execPath, ['--version'], { log: 'A0-node.log', ceilingMs: 2 * MIN });
  const df = await run(BASH, ['-c', 'df -h .'], { log: 'A0-df.log', ceilingMs: 2 * MIN });
  const dfRow = lastLine(df.out).replace(/\s+/g, ' ');
  record('A0', green(rc) && green(nv) && green(df),
    `rustc="${lastLine(rc.out)}" node="${lastLine(nv.out)}" df(repo root)="${dfRow}"`, rc.wallS);
}

function a0cBaseSize() {
  const file = evid('base-binary-size.txt');
  const text = fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : '';
  baseSize = parseLsSize(text);
  record('A0c', Number.isFinite(baseSize), `base binary size (TA-24) ${Number.isFinite(baseSize) ? `${baseSize} bytes` : 'NOT READ'}` +
    ` from ${rel(file)}`);
}

// ---- A1: the cargo chain ----------------------------------------------------------------------------------------
async function a1Cargo() {
  let r = await step('cargo fmt', 'A1a-cargo-fmt.log', 5 * MIN);
  record('A1a', green(r), `step cargo fmt last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);

  for (const [id, name, log] of [['A1b', 'clippy', 'A1b-clippy.log'], ['A1c', 'clippy dev-feed', 'A1c-clippy-dev-feed.log']]) {
    r = await step(name, log, 20 * MIN);
    const w = warningLines(r.out);
    record(id, green(r) && w === 0, `step ${name} warning-lines=${w} notices="${notices(r.out)}" ${stepLabel(r)}`, r.wallS);
  }

  r = await cargo(['clippy', '-p', 'krate-manager-core', '--all-targets', '--features', 'live-network', '--locked', '--',
    '-D', 'warnings'], { log: 'A1d-clippy-live-network.log', ceilingMs: 20 * MIN });
  const lw = warningLines(r.out);
  record('A1d', green(r) && lw === 0, `L-LIVE clippy --features live-network warning-lines=${lw} ${stepLabel(r)}`, r.wallS);

  r = await step('clippy-guard', 'A1e-clippy-guard.log', 15 * MIN);
  const guardNotice = lines(r.out).find((l) => l.startsWith('::notice title=clippy-guard (Windows)::')) || '';
  const guardTxt = `${LOG_DIR}/guard.txt`;
  let copied = 'guard.txt missing';
  if (fs.existsSync(guardTxt)) {
    const header = `# ${today()} (Windows, T016 close lane A1e, from apps/krate-manager/, RUNNER_TEMP=${LOG_DIR}):` +
      ` manager-ci.yml step clippy-guard, its own run: text (${guardNotice || 'no notice'})\n`;
    fs.writeFileSync(evid('clippy-guard.txt'), header + fs.readFileSync(guardTxt, 'utf8'));
    copied = `copied to ${EVID_REL}clippy-guard.txt`;
  }
  record('A1e', green(r) && guardNotice !== '', `step clippy-guard notice="${guardNotice || notices(r.out)}" ${copied}` +
    ` ${stepLabel(r)}`, r.wallS);

  r = await step('cargo test', 'A1f-cargo-test.log', 20 * MIN);
  const summaries = lines(r.out).filter((l) => /^test result: /.test(l));
  const failedBins = summaries.filter((l) => !/ 0 failed;/.test(l)).length;
  const passed = summaries.reduce((n, l) => n + Number((/ (\d+) passed;/.exec(l) || [0, 0])[1]), 0);
  const testLog = `${LOG_DIR}/cargo-test.txt`;
  if (fs.existsSync(testLog)) fs.copyFileSync(testLog, evid('cargo-test.txt'));
  record('A1f', green(r) && summaries.length > 0 && failedBins === 0, `step cargo test binaries=${summaries.length}` +
    ` passed=${passed} binaries-with-failures=${failedBins} -> ${EVID_REL}cargo-test.txt ${stepLabel(r)}`, r.wallS);

  // SC-019: the live test is absent from the default build and present (ignored) only under its feature. --list
  // never runs a test.
  r = await cargo(['test', '-p', 'krate-manager-core', '--locked', '--', '--list'],
    { log: 'A1g-list-default.log', ceilingMs: 10 * MIN });
  fs.writeFileSync(evid('list-default.txt'), r.stdout.replace(/\r\n/g, '\n'));
  let named = lines(r.stdout).filter((l) => l.includes('live_feed_parses')).length;
  const listed = lines(r.stdout).filter((l) => /: test$/.test(l)).length;
  record('A1g', green(r) && listed > 0 && named === 0, `cargo test --list (default) tests=${listed}` +
    ` lines naming live_feed_parses=${named} (want 0) -> ${EVID_REL}list-default.txt ${stepLabel(r)}`, r.wallS);

  r = await cargo(['test', '-p', 'krate-manager-core', '--features', 'live-network', '--test', 'live_feed', '--locked', '--',
    '--list', '--ignored'], { log: 'A1h-list-live.log', ceilingMs: 10 * MIN });
  fs.writeFileSync(evid('list-live.txt'), r.stdout.replace(/\r\n/g, '\n'));
  named = lines(r.stdout).filter((l) => l.includes('live_feed_parses')).length;
  record('A1h', green(r) && named === 1, `cargo test --features live-network --test live_feed --list --ignored` +
    ` lines naming live_feed_parses=${named} (want 1) -> ${EVID_REL}list-live.txt ${stepLabel(r)}`, r.wallS);

  for (const [file, kind, args] of TREES) {
    r = await cargo(['tree', ...args, '--locked'], { log: `A1i-${file.replace(/\.txt$/, '')}.log`, ceilingMs: 5 * MIN });
    const text = r.stdout.replace(/\r\n/g, '\n');
    fs.writeFileSync(evid(file), text);
    const v = treeVerdict(kind, text);
    record('A1i', green(r) && text.trim() !== '' && v.ok, `cargo tree ${args.join(' ')} -> ${EVID_REL}${file}` +
      ` lines=${lines(text.trim()).length} ${v.detail} ${stepLabel(r)}`, r.wallS);
  }
}

// ---- A2: the npm chain ------------------------------------------------------------------------------------------
async function a2Npm() {
  if (fs.existsSync(path.join(APP, 'node_modules'))) {
    record('A2a', true, 'node_modules present: step npm ci not run (tasks.md T016 A2)');
  } else {
    const r = await step('npm ci', 'A2a-npm-ci.log', 10 * MIN);
    record('A2a', green(r), `step npm ci last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
  }
  for (const [id, name, ceiling] of [['A2b', 'typecheck', 5], ['A2c', 'lint', 5], ['A2d', 'vitest', 10]]) {
    const r = await step(name, `${id}-${name}.log`, ceiling * MIN);
    record(id, green(r), `step ${name} last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
  }
}

// ---- A3: L-NODE, L-TAP, L-VLINT, the SC-037 Node record ---------------------------------------------------------
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

  const m = await node(path.join(EVID, 'model-range-node.js'), [], { log: 'A3e-model-range-node.log', ceilingMs: 2 * MIN });
  fs.writeFileSync(evid('model-range-node.txt'), m.stdout.replace(/\r\n/g, '\n'));
  const rows = lines(m.stdout).filter((l) => /^row [1-6] v[12] findings=\d+$/.test(l));
  const zero = rows.filter((l) => l.endsWith(' findings=0')).length;
  record('A3e', green(m) && rows.length === 6 && zero === 6, `model-range-node (SC-037 Node parity) rows=${rows.length}` +
    ` findings=0 on ${zero}/6 -> ${EVID_REL}model-range-node.txt ${stepLabel(m)}`, m.wallS);
}

// ---- A4..A12: short lanes and L-WSL -----------------------------------------------------------------------------
async function a4Yaml() {
  // One file after another: concurrent `npx --yes` runs race on the npx cache.
  for (const [i, wf] of ['manager-ci.yml', 'feed-tools.yml'].entries()) {
    const r = await run('npx --yes yaml@2.6.1 valid', [], {
      log: `A4-yaml-${i + 1}-${path.basename(wf, '.yml')}.log`, ceilingMs: 5 * MIN, stdinFile: `.github/workflows/${wf}`, shell: true,
    });
    record('A4', green(r), `L-YAML .github/workflows/${wf} ${stepLabel(r)}`, r.wallS);
  }
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
  // tasks.md T016 A6: red only because this phase's spec files changed the index -> regenerate once and re-check.
  const indexPath = path.join(ROOT, 'specs', 'INDEX.md');
  const before = fs.readFileSync(indexPath, 'utf8');
  const g = await node('tools/gen-specs-index.js', [], { log: 'A6-specs-index-regen.log', ceilingMs: 5 * MIN });
  const changed = changedLines(before, fs.readFileSync(indexPath, 'utf8'));
  const foreign = changed.filter((l) => !l.includes('krate-manager-phase5/'));
  if (!green(g) || foreign.length) {
    fs.writeFileSync(indexPath, before);
    record('A6', false, `L-INDEX --check red and the regeneration ${green(g) ? `changed ${foreign.length} line(s) not naming` +
      ` krate-manager-phase5/ (first: "${foreign[0]}")` : `failed (${fail(g)})`}; specs/INDEX.md restored ${stepLabel(r)}`, r.wallS);
    return;
  }
  r = await node('tools/gen-specs-index.js', ['--check'], { log: 'A6-specs-index-recheck.log', ceilingMs: 5 * MIN });
  record('A6', green(r), `L-INDEX regenerated once (${changed.length} changed line(s), all naming krate-manager-phase5/),` +
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

async function a8XCheck() {
  for (const t of XTARGETS) {
    const r = await cargo(['clippy', '-p', 'krate-manager-core', '--all-targets', '--locked', '--target', t, '--',
      '-D', 'warnings'], { log: `A8-xcheck-${t}.log`, ceilingMs: 20 * MIN });
    const v = xcheckVerdict(t, r.code, r.timedOut, r.out);
    record('A8', v.ok, `L-XCHECK ${t} ${v.detail} ${stepLabel(r)}`, r.wallS);
  }
}

/** A grep that passes only when it matches nothing (exit 1, empty output). */
async function emptyGrep(id, label, cmd, log) {
  const r = await run(BASH, ['-c', cmd], { log, ceilingMs: 2 * MIN });
  const hits = lines(r.out.trim()).filter(Boolean);
  const ok = !r.timedOut && r.code === 1 && hits.length === 0;
  record(id, ok, `${label} exit=${r.code} ${hits.length ? `MATCHED (first 10 of ${hits.length}): ${hits.slice(0, 10).join(' ; ')}`
    : 'printed nothing'} wall=${r.wallS}s log=${r.log}`, r.wallS);
}

async function a11Wsl() {
  const out = `${EVID_REL}wsl-core.txt`;
  const r = await node(path.join(EVID, 'wsl-lane.js'), ['--out', out], { log: 'A11-wsl-lane.log', ceilingMs: 45 * MIN });
  const last = lastLine(r.out);
  const seen = lines(r.out).find((l) => l.startsWith('cfg(not(windows)) tests seen ok:')) || 'no information line';
  const all = seen.startsWith(`cfg(not(windows)) tests seen ok: ${NON_WINDOWS_COUNT}/${NON_WINDOWS_COUNT}`);
  record('A11', green(r) && last.startsWith('WSL LANE GREEN') && all, `L-WSL last="${last}" info="${seen}"` +
    ` -> ${out} (early signal; closes no PENDING-CI row) ${stepLabel(r)}`, r.wallS);
}

async function a12RustSec() {
  const r = await run(BASH, ['-c', "grep -n 'rustsec.org' specs/Krate-manager-roadmap.md"], { log: 'A12a-rustsec.log', ceilingMs: 2 * MIN });
  const text = r.stdout.replace(/\r/g, '');
  fs.writeFileSync(evid('rustsec-line.txt'), text);
  const hit = lines(text).filter(Boolean);
  const words = ['rquickjs', 'rquickjs-core', 'rquickjs-sys', 'pin bump'];
  const missing = hit.length === 1 ? words.filter((w) => !hit[0].includes(w)) : words;
  record('A12a', green(r) && hit.length === 1 && missing.length === 0, `SC-044 rustsec.org lines=${hit.length} (want 1)` +
    ` missing=${missing.length ? missing.join(',') : 'none'} -> ${EVID_REL}rustsec-line.txt ${stepLabel(r)}`, r.wallS);

  const d = await run('git', ['diff', '--numstat', BASE, '--', 'specs/krate-manager/probes/results.md'],
    { log: 'A12b-results-numstat.log', ceilingMs: 2 * MIN });
  const [added, removed] = d.stdout.trim().split(/\s+/);
  record('A12b', green(d) && added === '1' && removed === '0', `git diff --numstat ${BASE} -- specs/krate-manager/probes/results.md` +
    ` = "${added || ''} ${removed || ''}" (want "1 0") ${stepLabel(d)}`, d.wallS);
}

// ---- B: required tests, the release build and its checks --------------------------------------------------------
async function bBuild() {
  let r = await step('required tests ran', 'B1-required-tests-ran.log', 5 * MIN);
  record('B1', green(r) && r.out.includes(REQUIRED_NOTICE), `step required tests ran notices="${notices(r.out)}"` +
    ` want "${REQUIRED_NOTICE}" ${stepLabel(r)}`, r.wallS);
  if (SKIP_BUILD) {
    record('B2', true, 'DEFERRED (--skip-tauri-build): step tauri build and B3-B7 run only in the main loop close');
    return;
  }
  r = await step('tauri build', 'B2-tauri-build.log', 40 * MIN);
  record('B2', green(r), `step tauri build last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);

  const version = JSON.parse(fs.readFileSync(path.join(APP, 'version.json'), 'utf8')).version;
  for (const [id, name, notice] of [
    ['B3', 'build warnings', '::notice title=build warnings (Windows)::0 warning lines in the release build'],
    ['B4', 'dev-feed absent', '::notice title=dev-feed absent (Windows)::'],
  ]) {
    r = await step(name, `${id}-${name.replace(/\s+/g, '-')}.log`, 5 * MIN);
    record(id, green(r) && r.out.includes(notice), `step ${name} notices="${notices(r.out)}" want "${notice}" ${stepLabel(r)}`, r.wallS);
  }

  r = await step('roster absent', 'B5-roster-absent.log', 5 * MIN);
  fs.writeFileSync(evid('roster-absent.txt'), `# ${today()} (Windows, T016 close lane B5, from apps/krate-manager/):` +
    ` manager-ci.yml step roster absent, its own run: text, ${fail(r)}\n${r.out.replace(/\r\n/g, '\n')}`);
  const m = /^::notice title=roster absent \(Windows\)::(\d+) names, 0 hits in (\d+) files$/m.exec(r.out);
  const rosterOk = green(r) && m && Number(m[1]) >= 9 && Number(m[2]) >= 1;
  record('B5', Boolean(rosterOk), `step roster absent notices="${notices(r.out)}" (want n >= 9 names, 0 hits; a notice` +
    ` means the positive control was found) -> ${EVID_REL}roster-absent.txt ${stepLabel(r)}`, r.wallS);

  const want = `::notice title=artifact version (Windows)::${version} (nsis=present)`;
  r = await step('artifact version', 'B6-artifact-version.log', 5 * MIN);
  record('B6', green(r) && r.out.includes(want), `step artifact version notices="${notices(r.out)}" want "${want}" ${stepLabel(r)}`,
    r.wallS);

  r = await run(BASH, ['-c', 'ls -l target/release/krate-manager.exe'], { log: 'B7-binary-size.log', ceilingMs: 2 * MIN, cwd: APP });
  const size = parseLsSize(r.stdout);
  const delta = Number.isFinite(size) && Number.isFinite(baseSize)
    ? `${size - baseSize >= 0 ? '+' : ''}${size - baseSize} bytes (${(((size - baseSize) / baseSize) * 100).toFixed(1)} %)` : 'n/a';
  fs.writeFileSync(evid('binary-size.txt'), [
    `# ${today()} close lane B7 (Windows, from apps/krate-manager/): the release binary after step tauri build (plan §4.1;`,
    '# with R-15 the Windows binary carries no QuickJS: the growth is ureq / rustls / ring and the phase\'s code)',
    '$ ls -l target/release/krate-manager.exe',
    r.stdout.replace(/\r/g, '').trim(),
    `# base (A0c, ${EVID_REL}base-binary-size.txt, TA-24): ${baseSize} bytes; now ${size} bytes; delta ${delta}`,
    '',
  ].join('\n'));
  record('B7', green(r) && Number.isFinite(size), `release binary ${size} bytes, base ${baseSize}, delta ${delta}` +
    ` -> ${EVID_REL}binary-size.txt ${stepLabel(r)}`, r.wallS);
}

function writeTimings(totalS, verdict) {
  const rows = results.map((r) => `| ${r.id} | ${r.ok ? 'GREEN' : 'RED'} | ${r.wallS || '-'} | ${r.text.split(' wall=')[0].replace(/\|/g, '\\|')} |`);
  const text = [
    '# Krate Manager Phase 5: close-lane wall clocks',
    '',
    `Written by \`evidence/close-lane.js\`${SKIP_BUILD ? ' `--skip-tauri-build` (PARTIAL: B2-B7 deferred)' : ''},` +
      ` ${new Date().toISOString()}. Total wall ${totalS}s; ${verdict}. Raw logs (cleaned at close): \`${LOG_DIR}\`.`,
    'A1 (cargo) and A2 (npm) run as two chains beside A3-A7, A9-A12 (A11 is L-WSL, on WSL\'s own 4 cores); A8 starts' +
      ' after A1; B starts after A1 and A2.',
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
  console.log(`close lane${SKIP_BUILD ? ' (--skip-tauri-build)' : ''}: logs ${LOG_DIR}`);
  yamlText = fs.readFileSync(WORKFLOW, 'utf8');
  await a0Records();
  const before = gitStatus();
  fs.writeFileSync(`${LOG_DIR}/A0b-git-status.txt`, before);
  record('A0b', !before.startsWith('git status failed'), `git status --porcelain: ${lines(before).filter(Boolean).length} lines` +
    ` (without ${EVID_REL}) file=${LOG_DIR}/A0b-git-status.txt`);
  a0cBaseSize();

  const a1 = a1Cargo();
  const a2 = a2Npm();
  const a8 = a1.then(a8XCheck);
  const b = Promise.all([a1, a2]).then(() => {
    const chain = results.filter((r) => /^A[12][a-z]$/.test(r.id));
    if (chain.every((r) => r.ok)) return bBuild();
    record('B', false, `skipped: not every A1 / A2 row is green (${chain.filter((r) => !r.ok).map((r) => r.id).join(', ')})`);
    return undefined;
  });
  await Promise.all([a1, a2, a8, b, a3Node(), a4Yaml(), a5Lint(), a6SpecsIndex(), a7Scope(),
    emptyGrep('A9', 'SC-021 grep', SC021_GREP, 'A9-sc021-grep.log'),
    emptyGrep('A10', 'TA-4 grep (todo!( / unimplemented!( outside tests)', TA4_GREP, 'A10-ta4-grep.log'),
    a11Wsl(), a12RustSec()]);

  const after = gitStatus();
  fs.writeFileSync(`${LOG_DIR}/C1-git-status.txt`, after);
  const same = after === before;
  record('C1', same, `git status --porcelain (without ${EVID_REL}) ${same ? 'equal to A0b' : 'CHANGED from A0b'}` +
    ` (${lines(after).filter(Boolean).length} lines) file=${LOG_DIR}/C1-git-status.txt`);
  await scope('C2', 'C2-scope-check.log');

  const order = ['A0', 'A0b', 'A0c', 'A1a', 'A1b', 'A1c', 'A1d', 'A1e', 'A1f', 'A1g', 'A1h', 'A1i', 'A2a', 'A2b', 'A2c',
    'A2d', 'A3a', 'A3b', 'A3c', 'A3d', 'A3e', 'A4', 'A5', 'A6', 'A7a', 'A7b', 'A8', 'A9', 'A10', 'A11', 'A12a', 'A12b', 'B',
    'B1', 'B2', 'B3', 'B4', 'B5', 'B6', 'B7', 'C1', 'C2'];
  // Stable sort: the repeated A1i, A4 and A8 rows keep their order.
  results.sort((x, y) => order.indexOf(x.id) - order.indexOf(y.id));
  const allGreen = results.every((r) => r.ok);
  const verdict = !allGreen ? 'CLOSE LANE RED'
    : SKIP_BUILD ? 'CLOSE LANE PARTIAL (B2-B7 deferred; not a close)' : 'CLOSE LANE GREEN';
  const totalS = ((Date.now() - t0) / 1000).toFixed(1);
  const summary = [
    `Krate Manager Phase 5 close lane${SKIP_BUILD ? ' --skip-tauri-build' : ''}, ${new Date().toISOString()},` +
      ` wall=${totalS}s, logs ${LOG_DIR}`,
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
