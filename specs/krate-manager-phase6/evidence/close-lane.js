#!/usr/bin/env node
// Close lane for Krate Manager Phase 6 (L-CLOSE; tasks.md T010, T013; spec SC-001, SC-022 tree record, SC-023 local,
// SC-028 record, SC-033 records; plan.md §4.13, §4.14, §7.4, §8; tasks TA-8, TA-13). Run by the MAIN LOOP at close,
// never by an implementer or a gate: it holds the release `npm run tauri build` and L-WSL, each well past five minutes.
//
// WHY one script: the close rows must come from one run whose steps share a clock and a log directory, with every
// independent step concurrent (CLAUDE.md "Optimise Every Build and Test Run") and every child under a wall-clock
// ceiling, so a hung step reads RED instead of stalling the close. Same form as
// specs/krate-manager-phase5/evidence/close-lane.js; its rows A1d, A1g, A1h, A3e, A8 and A12 are dropped (no
// live-network change, no model-range-node.js, no cross-target check, no RustSec row this phase).
//
// WHY the workflow's own step text: wherever a row says "step <name>", the script extracts that step's `run: |` block
// from .github/workflows/manager-ci.yml, removes its indentation and runs it with Git Bash `bash.exe -c`, cwd
// apps/krate-manager/ (the job's defaults.run.working-directory), env RUNNER_OS=Windows and RUNNER_TEMP=<log dir>. Local
// and CI logic cannot drift. The step text, as run, is saved beside its log (<log>.sh).
//
// Usage (repo root, Windows dev box; Node only, no dependencies):
//   node specs/krate-manager-phase6/evidence/close-lane.js                       # the close (last line GREEN | RED)
//   node specs/krate-manager-phase6/evidence/close-lane.js --skip-tauri-build    # everything but B2-B7 (last line
//                                                                                 # CLOSE LANE PARTIAL ...)
//   node specs/krate-manager-phase6/evidence/close-lane.js --self-test           # pure checks (extractStep over the
//                                                                                 # real workflow, the opener tree
//                                                                                 # verdicts, the SC-028 counter)
// Rows:
//   A0   rustc --version, node --version, df -h of the repo root only (records only)
//   A0b  git status --porcelain --untracked-files=all snapshot, kept for C1
//   A0c  the base binary size from evidence/base-binary-size.txt (T001, TA-13)
//   A (concurrent):
//     A1 one chain (the cargo commands share one target-dir lock): A1a step `cargo fmt`; A1b step `clippy`; A1c step
//        `clippy dev-feed`; A1d step `clippy-guard` (its ::notice; $RUNNER_TEMP/guard.txt copied to
//        evidence/clippy-guard.txt); A1e step `cargo test` ($RUNNER_TEMP/cargo-test.txt copied to
//        evidence/cargo-test.txt); A1f step `core dependency tree` (T012; ::notice ...::<n> crates, none excluded;
//        $RUNNER_TEMP/core-tree.txt copied to evidence/core-tree.txt, SC-022); A1g cargo tree -p krate-manager --locked
//        -e normal --prefix none > evidence/tree-app.txt (exactly one line `tauri-plugin-opener v2.7.2`, SC-033); A1h
//        the same for -p krate-manager-core > evidence/tree-core.txt (no line naming tauri-plugin-opener, SC-033)
//     A2 one chain: A2a step `npm ci` only if node_modules is absent; A2b step `typecheck`; A2c step `lint`; A2d step
//        `vitest`; A2e npm ls @tauri-apps/plugin-opener > evidence/npm-ls-opener.txt (no such package, SC-033)
//     A3 (repo root): A3a node --check on this phase's three scripts; A3b node --test --test-reporter=tap
//        "tools/feed-contract/tests/*.test.js" to a TAP file (# fail 0, plan 1..59, no Node warning line); A3c L-TAP on
//        it ("TAP plan=1..59 top-level=59"); A3d L-VLINT
//     A4 L-YAML (yaml@2.6.1) on manager-ci.yml
//     A5 L-LINT node tools/lint-serial-test-runs.js
//     A6 L-INDEX node tools/gen-specs-index.js --check; if red only because this phase's spec files changed the index
//        (every changed line names krate-manager-phase6/), regenerate once and re-check; otherwise restore and RED
//     A7 A7a scope-check.js --self-test, then A7b L-SCOPE
//     A9 the TA-4 grep (rule 9): no todo!( / unimplemented!( in crates/ or src-tauri/src outside *_tests.rs and tests/
//     A10 L-WSL (TA-8): wsl-lane.js --out evidence/wsl-core.txt; last line WSL LANE GREEN and 6/6 Linux-run names seen
//         ok (an early signal recorded beside the PENDING-CI rows, never instead of them)
//     A11 SC-028: the roadmap with every whitespace run collapsed to one space, each needle of
//         evidence/sc028-needles.json (T013 writes it for the ruled Q-C answer: {"needles": [[<text>, <count>], ...]})
//         counted -> evidence/sc028.txt
//   B (after A1 and A2, only when every A1 / A2 row is green):
//     B1 step `required tests ran` (::notice ...::172 cargo + 29 vitest, plan §4.14)
//     B2 step `tauri build` (40 min ceiling; 318 s in Phase 5); B3 step `build warnings`; B4 step `dev-feed absent`; B5
//     step `roster absent` (::notice ...::<n >= 9> names, 0 hits in <m> files; output to evidence/roster-absent.txt); B6
//     step `artifact version` (version.json's version and the NSIS file); B7 ls -l target/release/krate-manager.exe >
//     evidence/binary-size.txt beside A0c's base value (TA-13)
//   C (last): C1 git status again, minus lines under specs/krate-manager-phase6/evidence/, equal to A0b (SC-001); C2
//     L-SCOPE again; C3 node tools/clean-test-output.js --apply (repo rule; it removes every F:\tmp entry, this run's raw
//     log directory included, so it runs last and its summary line goes into the evidence summary)
// Step `Checkout unchanged` is not run locally: the phase's own uncommitted files fail it until the user commits.
// Raw logs: F:/tmp/gate/krate-manager-phase6-close/<timestamp>/ (removed by C3). Summary: evidence/close-lane.log
// (written and printed); wall clocks: evidence/timings.md (plan §7.4, for the next run's planning). Exit 0 green (or
// partial with every run row green), 1 red.
'use strict';
const childProcess = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const APP = path.join(ROOT, 'apps', 'krate-manager');
const STAMP = new Date().toISOString().replace(/[-:]/g, '').replace(/\..*/, '').replace('T', '-');
// Forward slashes: the directory is also RUNNER_TEMP inside Git Bash.
const LOG_DIR = `F:/tmp/gate/krate-manager-phase6-close/${STAMP}`;
const EVID = __dirname;
const EVID_REL = 'specs/krate-manager-phase6/evidence/';
const evid = (name) => path.join(EVID, name);
const rel = (f) => path.relative(ROOT, f).replace(/\\/g, '/');
const SUMMARY = evid('close-lane.log');
const TIMINGS = evid('timings.md');
const WORKFLOW = path.join(ROOT, '.github', 'workflows', 'manager-ci.yml');
const ROADMAP = path.join(ROOT, 'specs', 'Krate-manager-roadmap.md');
const BASH = 'C:/Program Files/Git/bin/bash.exe';
const MIN = 60 * 1000;
const SKIP_BUILD = process.argv.includes('--skip-tauri-build');

// The scope base (scope-check.js BASE; tasks.md header: written against feat/krate-manager at 3efe70de).
const BASE = '3efe70de';
const EXPECTED_PLAN = 59; // tasks.md L-TAP
const REQUIRED_NOTICE = '::notice title=required tests ran (Windows)::172 cargo + 29 vitest'; // plan §4.14 totals
const CORE_TREE_NOTICE = /^::notice title=core dependency tree \(Windows\)::(\d+) crates, none excluded$/m; // plan §4.13
const OPENER = 'tauri-plugin-opener';
const OPENER_LINE = `${OPENER} v2.7.2`; // spec SC-033, tasks.md rule 14
const TAP_FILE = `${LOG_DIR}/A3b-node-test.tap`;
// tasks.md T010 A9 (rule 9, TA-4): *_tests.rs and */tests/* excluded.
const TA4_GREP = "grep -rnE --include='*.rs' --exclude='*_tests.rs' --exclude-dir=tests" +
  " -- 'todo!\\(|unimplemented!\\(' apps/krate-manager/crates apps/krate-manager/src-tauri/src";
// L-NODE's node --check set: this phase's three scripts (tasks.md T010 A3).
const NODE_CHECK = [
  'specs/krate-manager-phase6/evidence/close-lane.js',
  'specs/krate-manager-phase6/evidence/scope-check.js',
  'specs/krate-manager-phase6/evidence/wsl-lane.js',
];
// wsl-lane.js INFO_PREFIX and LINUX_RUN_TESTS (TA-8's six names).
const WSL_INFO_ALL = 'Linux-run Phase 6 tests seen ok: 6/6';

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

/**
 * tasks.md T010 A1g / A1h (SC-033): the app tree holds exactly one line naming tauri-plugin-opener and it reads
 * `tauri-plugin-opener v2.7.2`; the core tree holds none. Returns { ok, detail }.
 */
function openerTreeVerdict(kind, text) {
  const hits = lines(text).map((l) => l.trim()).filter((l) => l.includes(OPENER));
  if (kind === 'app') {
    const ok = hits.length === 1 && hits[0] === OPENER_LINE;
    return { ok, detail: ok ? `exactly one line "${OPENER_LINE}"` : `want exactly one line "${OPENER_LINE}", got ${hits.length}` +
      ` line(s) naming ${OPENER}${hits.length ? ` (${hits.join('; ')})` : ''}` };
  }
  if (kind === 'core') {
    const ok = hits.length === 0;
    return { ok, detail: ok ? `no line naming ${OPENER}` : `${hits.length} line(s) naming ${OPENER} (${hits.join('; ')})` };
  }
  return { ok: false, detail: `unknown tree kind ${kind}` };
}

/** tasks.md T010 A2e (SC-033): `npm ls @tauri-apps/plugin-opener` finds no such package. Returns { ok, detail }. */
function npmLsVerdict(text) {
  const found = lines(text).filter((l) => l.includes('@tauri-apps/plugin-opener@'));
  const empty = lines(text).some((l) => /\(empty\)\s*$/.test(l));
  const ok = found.length === 0 && empty;
  return { ok, detail: ok ? 'no such package ("(empty)")' : found.length ? `FOUND ${found.map((l) => l.trim()).join('; ')}`
    : 'no "(empty)" line' };
}

/** Occurrences of `needle` in `text`, every whitespace run in both collapsed to one space (spec SC-028). */
function countCollapsed(text, needle) {
  const collapse = (s) => s.replace(/\s+/g, ' ');
  const t = collapse(text);
  const n = collapse(needle).trim();
  if (!n) return 0;
  let count = 0;
  for (let i = t.indexOf(n); i >= 0; i = t.indexOf(n, i + n.length)) count++;
  return count;
}

/**
 * tasks.md T010 A11: each [text, count] of the needles file counted over the roadmap; ok when every count matches and
 * there is at least one needle. Returns { ok, rows } (rows are the evidence lines).
 */
function sc028Verdict(text, needles) {
  const valid = Array.isArray(needles) && needles.length > 0 &&
    needles.every((n) => Array.isArray(n) && n.length === 2 && typeof n[0] === 'string' && Number.isInteger(n[1]));
  if (!valid) return { ok: false, rows: [`needles not a non-empty [[<text>, <count>], ...] list: ${JSON.stringify(needles)}`] };
  const rows = needles.map(([needle, want]) => {
    const got = countCollapsed(text, needle);
    return { ok: got === want, line: `${got === want ? 'ok ' : 'BAD'} got=${got} want=${want} ${JSON.stringify(needle)}` };
  });
  return { ok: rows.every((r) => r.ok), rows: rows.map((r) => r.line) };
}

/** git status --porcelain lines, minus this phase's evidence outputs (tasks.md T010 C1). */
function withoutEvidence(statusText) {
  return lines(statusText).filter((l) => l && !l.slice(3).replace(/^"/, '').startsWith(EVID_REL)).join('\n');
}

/** The byte size from an `ls -l ... [apps/krate-manager/]target/release/krate-manager.exe` line, or NaN. */
function parseLsSize(text) {
  // The size is the field before the date: long-iso `2026-10-30 22:21` or the default `Oct 31 12:01` / `Oct 31 2025`.
  // T001's base record lists the binary from the repo root (apps/krate-manager/target/...), B7 from the app dir.
  const date = String.raw`(?:\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}|[A-Z][a-z]{2}\s+\d{1,2}\s+(?:\d{2}:\d{2}|\d{4}))`;
  const m = new RegExp(String.raw`\s(\d+)\s+${date}\s+(?:\S*\/)?target\/release\/krate-manager\.exe\s*$`, 'm').exec(text);
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

  // The real workflow (CRLF in this checkout): the two steps the close lane's verdicts lean on.
  const real = fs.readFileSync(WORKFLOW, 'utf8');
  check('real step cargo test', () => {
    const s = extractStep(real, 'cargo test');
    eq(s.startsWith('set -euo pipefail\n'), true);
    has(s, 'cargo test --workspace --locked 2>&1 | tee "$RUNNER_TEMP/cargo-test.txt"\n');
    eq(s.includes('\r'), false);
  });
  check('real step required tests ran', () => {
    const s = extractStep(real, 'required tests ran');
    has(s, `node - "$RUNNER_TEMP/cargo-test.txt" "$RUNNER_TEMP/vitest.json" "$RUNNER_OS" <<'REQUIRED'\n`);
    has(s, '::notice title=required tests ran (${runnerOs})::${names.length} cargo + ${req.vitest.length} vitest');
    eq(s.endsWith('\nREQUIRED\n'), true); // the heredoc terminator at column 0
  });

  const appTree = 'krate-manager v0.1.0 (F:\\x\\src-tauri)\nkrate-manager-core v0.1.0 (F:\\x\\crates\\core)\n' +
    'tauri v2.12.5\ntauri-plugin-opener v2.7.2\nurl v2.5.8\n';
  const coreTree = 'krate-manager-core v0.1.0 (F:\\x\\crates\\core)\npulldown-cmark v0.13.6\nureq v3.5.0\n';
  check('core tree without the opener -> GREEN', () => eq(openerTreeVerdict('core', coreTree).ok, true));
  check('core tree with an opener line -> RED', () => eq(openerTreeVerdict('core', `${coreTree}${OPENER_LINE}\n`).ok, false));
  check('app tree with exactly one v2.7.2 (CRLF) -> GREEN', () => eq(openerTreeVerdict('app', appTree.replace(/\n/g, '\r\n')).ok, true));
  check('app tree without the opener -> RED', () => eq(openerTreeVerdict('app', appTree.replace(`${OPENER_LINE}\n`, '')).ok, false));
  check('app tree with another opener version -> RED', () =>
    eq(openerTreeVerdict('app', appTree.replace(OPENER_LINE, `${OPENER} v2.7.3`)).ok, false));
  check('app tree with two opener lines -> RED', () => eq(openerTreeVerdict('app', `${appTree}${OPENER_LINE} (*)\n`).ok, false));

  // npm 10 output, measured this session from apps/krate-manager/ (exit 1 when the package is absent).
  check('npm ls absent -> GREEN', () =>
    eq(npmLsVerdict('krate-manager@ F:\\projects\\iterum\\apps\\krate-manager\r\n`-- (empty)\r\n').ok, true));
  check('npm ls present -> RED', () =>
    eq(npmLsVerdict('krate-manager@ F:\\x\n`-- @tauri-apps/plugin-opener@2.5.0\n').ok, false));

  // The SC-028 counter over two in-memory texts: the (a) sentence wrapped and indented like the roadmap's bullet
  // (CRLF), and the same bullet before T013's edit.
  const before = [
    '  - `run_inno_uninstaller { app_id }` (Windows): ... requires the executable to be an `unins*.exe` in the plugin\'s',
    '    install dir under the compiled-in Windows plug-in root, and runs it with the fixed arguments ...',
    '    with a bounded timeout (the uninstaller returns before its files are removed, per_os_install_mechanics.md KQ2).',
  ].join('\r\n');
  const after = `${before}\r\n    A managed copy whose recorded \`InstallLocation\` lies outside the compiled-in Windows plug-in root is never\r\n` +
    '    uninstalled by the helper: the manager shows it as managed and tells the user to uninstall it from Windows Settings.\r\n';
  const needles = [["in the plugin's install dir under the compiled-in Windows plug-in root", 1],
    ['is never uninstalled by the helper', 1]];
  check('SC-028 counter after the edit -> GREEN', () => eq(sc028Verdict(after, needles).ok, true));
  check('SC-028 counter before the edit -> RED', () => eq(sc028Verdict(before, needles).ok, false));
  check('SC-028 count of a wrapped needle', () => eq(countCollapsed(after, 'is never uninstalled by the helper'), 1));
  check('SC-028 count of a repeated needle', () => eq(countCollapsed(`${after}${after}`, 'is never uninstalled by the helper'), 2));
  check('SC-028 empty needles -> RED', () => eq(sc028Verdict(after, []).ok, false));

  check('status filter drops evidence only', () => eq(withoutEvidence(
    ` M specs/INDEX.md\n?? ${EVID_REL}tree-core.txt\n?? specs/krate-manager-phase6/plan.md\n`),
  ' M specs/INDEX.md\n?? specs/krate-manager-phase6/plan.md'));
  check('ls size parsed (T001 base record, repo-root path)', () => eq(parseLsSize(
    '-rwxr-xr-x 2 scab 197609 4314624 2026-10-31 15:22 apps/krate-manager/target/release/krate-manager.exe'), 4314624));
  check('ls size parsed (default time style)', () => eq(parseLsSize(
    '-rwxr-xr-x 1 scab 197609 5300000 Oct 31 12:01 target/release/krate-manager.exe'), 5300000));
  check('changed lines', () => eq(changedLines('a\r\nb\r\nc', 'a\nc\nd').sort().join(','), 'b,d'));

  for (const f of failures) console.log(`self-test FAIL ${f}`);
  console.log(failures.length ? `CLOSE LANE SELF-TEST RED (${failures.length} failed)`
    : 'CLOSE LANE SELF-TEST GREEN (8 extractStep, 6 tree, 2 npm ls, 5 SC-028, 4 helper cases)');
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
  record('A0c', Number.isFinite(baseSize), `base binary size (TA-13) ${Number.isFinite(baseSize) ? `${baseSize} bytes` : 'NOT READ'}` +
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

  r = await step('clippy-guard', 'A1d-clippy-guard.log', 15 * MIN);
  const guardNotice = lines(r.out).find((l) => l.startsWith('::notice title=clippy-guard (Windows)::')) || '';
  const guardTxt = `${LOG_DIR}/guard.txt`;
  let copied = 'guard.txt missing';
  if (fs.existsSync(guardTxt)) {
    const header = `# ${today()} (Windows, T010 close lane A1d, from apps/krate-manager/, RUNNER_TEMP=${LOG_DIR}):` +
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

  // SC-022: the workflow's own core dependency tree step (T012, plan §4.13).
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

  // SC-033: the opener is in the app graph exactly once and absent from core.
  for (const [id, file, pkg, kind] of [['A1g', 'tree-app.txt', 'krate-manager', 'app'],
    ['A1h', 'tree-core.txt', 'krate-manager-core', 'core']]) {
    const args = ['tree', '-p', pkg, '--locked', '-e', 'normal', '--prefix', 'none'];
    r = await cargo(args, { log: `${id}-${file.replace(/\.txt$/, '')}.log`, ceilingMs: 5 * MIN });
    const text = r.stdout.replace(/\r\n/g, '\n');
    fs.writeFileSync(evid(file), text);
    const v = openerTreeVerdict(kind, text);
    record(id, green(r) && text.trim() !== '' && v.ok, `cargo ${args.join(' ')} -> ${EVID_REL}${file}` +
      ` lines=${lines(text.trim()).length} ${v.detail} ${stepLabel(r)}`, r.wallS);
  }
}

// ---- A2: the npm chain ------------------------------------------------------------------------------------------
async function a2Npm() {
  if (fs.existsSync(path.join(APP, 'node_modules'))) {
    record('A2a', true, 'node_modules present: step npm ci not run (tasks.md T010 A2)');
  } else {
    const r = await step('npm ci', 'A2a-npm-ci.log', 10 * MIN);
    record('A2a', green(r), `step npm ci last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
  }
  for (const [id, name, ceiling] of [['A2b', 'typecheck', 5], ['A2c', 'lint', 5], ['A2d', 'vitest', 10]]) {
    const r = await step(name, `${id}-${name}.log`, ceiling * MIN);
    record(id, green(r), `step ${name} last="${lastLine(r.out)}" ${stepLabel(r)}`, r.wallS);
  }
  // npm ls exits 1 when the package is absent (measured, npm 10); the verdict reads the listing.
  const r = await run('npm ls @tauri-apps/plugin-opener', [], { log: 'A2e-npm-ls-opener.log', ceilingMs: 2 * MIN, shell: true, cwd: APP });
  const text = r.out.replace(/\r\n/g, '\n');
  fs.writeFileSync(evid('npm-ls-opener.txt'), text);
  const v = npmLsVerdict(text);
  record('A2e', !r.timedOut && v.ok, `npm ls @tauri-apps/plugin-opener exit=${r.code} ${v.detail}` +
    ` -> ${EVID_REL}npm-ls-opener.txt wall=${r.wallS}s log=${r.log}`, r.wallS);
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

// ---- A4..A11: short lanes and L-WSL -----------------------------------------------------------------------------
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
  // tasks.md T010 A6: red only because this phase's spec files changed the index -> regenerate once and re-check.
  const indexPath = path.join(ROOT, 'specs', 'INDEX.md');
  const before = fs.readFileSync(indexPath, 'utf8');
  const g = await node('tools/gen-specs-index.js', [], { log: 'A6-specs-index-regen.log', ceilingMs: 5 * MIN });
  const changed = changedLines(before, fs.readFileSync(indexPath, 'utf8'));
  const foreign = changed.filter((l) => !l.includes('krate-manager-phase6/'));
  if (!green(g) || foreign.length) {
    fs.writeFileSync(indexPath, before);
    record('A6', false, `L-INDEX --check red and the regeneration ${green(g) ? `changed ${foreign.length} line(s) not naming` +
      ` krate-manager-phase6/ (first: "${foreign[0]}")` : `failed (${fail(g)})`}; specs/INDEX.md restored ${stepLabel(r)}`, r.wallS);
    return;
  }
  r = await node('tools/gen-specs-index.js', ['--check'], { log: 'A6-specs-index-recheck.log', ceilingMs: 5 * MIN });
  record('A6', green(r), `L-INDEX regenerated once (${changed.length} changed line(s), all naming krate-manager-phase6/),` +
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
  const info = lines(r.out).find((l) => l.startsWith('Linux-run Phase 6 tests seen ok:')) || 'no information line';
  const all = info.startsWith(WSL_INFO_ALL);
  record('A10', green(r) && last.startsWith('WSL LANE GREEN') && all, `L-WSL last="${last}" info="${info}"` +
    ` -> ${out} (early signal; closes no PENDING-CI row) ${stepLabel(r)}`, r.wallS);
}

async function a11Sc028() {
  const t0 = Date.now();
  const file = evid('sc028-needles.json');
  let needles;
  let why = '';
  try {
    needles = JSON.parse(fs.readFileSync(file, 'utf8')).needles;
  } catch (e) {
    why = `${rel(file)} not read (T013 writes it for the ruled Q-C answer): ${e.message}`;
  }
  const v = why ? { ok: false, rows: [why] } : sc028Verdict(fs.readFileSync(ROADMAP, 'utf8'), needles);
  fs.writeFileSync(evid('sc028.txt'), [
    `# ${today()} close lane A11 (SC-028): specs/Krate-manager-roadmap.md, every whitespace run collapsed to one space;`,
    `# needles and counts from ${rel(file)}; the roadmap's hunks are checked by L-SCOPE (A7b, C2)`,
    ...v.rows,
    '',
  ].join('\n'));
  const wallS = ((Date.now() - t0) / 1000).toFixed(1);
  record('A11', v.ok, `SC-028 ${v.rows.join(' | ')} -> ${EVID_REL}sc028.txt`, wallS);
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
  fs.writeFileSync(evid('roster-absent.txt'), `# ${today()} (Windows, T010 close lane B5, from apps/krate-manager/):` +
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
    `# ${today()} close lane B7 (Windows, from apps/krate-manager/): the release binary after step tauri build (TA-13;`,
    '# the growth is pulldown-cmark, tauri-plugin-opener, the Windows registry / version readers and the phase\'s code)',
    '$ ls -l target/release/krate-manager.exe',
    r.stdout.replace(/\r/g, '').trim(),
    `# base (A0c, ${EVID_REL}base-binary-size.txt, TA-13): ${baseSize} bytes; now ${size} bytes; delta ${delta}`,
    '',
  ].join('\n'));
  record('B7', green(r) && Number.isFinite(size), `release binary ${size} bytes, base ${baseSize}, delta ${delta}` +
    ` -> ${EVID_REL}binary-size.txt ${stepLabel(r)}`, r.wallS);
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
    '# Krate Manager Phase 6: close-lane wall clocks',
    '',
    `Written by \`evidence/close-lane.js\`${SKIP_BUILD ? ' `--skip-tauri-build` (PARTIAL: B2-B7 deferred)' : ''},` +
      ` ${new Date().toISOString()}. Total wall ${totalS}s; ${verdict}. Raw logs (removed by C3): \`${LOG_DIR}\`.`,
    'A1 (cargo) and A2 (npm) run as two chains beside A3-A7, A9-A11 (A10 is L-WSL, on WSL\'s own cores); B starts after' +
      ' A1 and A2; C runs last.',
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
  const b = Promise.all([a1, a2]).then(() => {
    const chain = results.filter((r) => /^A[12][a-z]$/.test(r.id));
    if (chain.every((r) => r.ok)) return bBuild();
    record('B', false, `skipped: not every A1 / A2 row is green (${chain.filter((r) => !r.ok).map((r) => r.id).join(', ')})`);
    return undefined;
  });
  await Promise.all([a1, a2, b, a3Node(), a4Yaml(), a5Lint(), a6SpecsIndex(), a7Scope(),
    emptyGrep('A9', 'TA-4 grep (todo!( / unimplemented!( outside tests)', TA4_GREP, 'A9-ta4-grep.log'),
    a10Wsl(), a11Sc028()]);

  const after = gitStatus();
  fs.writeFileSync(`${LOG_DIR}/C1-git-status.txt`, after);
  const same = after === before;
  record('C1', same, `git status --porcelain (without ${EVID_REL}) ${same ? 'equal to A0b' : 'CHANGED from A0b'}` +
    ` (${lines(after).filter(Boolean).length} lines) file=${LOG_DIR}/C1-git-status.txt`);
  await scope('C2', 'C2-scope-check.log');
  c3Clean();

  const order = ['A0', 'A0b', 'A0c', 'A1a', 'A1b', 'A1c', 'A1d', 'A1e', 'A1f', 'A1g', 'A1h', 'A2a', 'A2b', 'A2c', 'A2d',
    'A2e', 'A3a', 'A3b', 'A3c', 'A3d', 'A4', 'A5', 'A6', 'A7a', 'A7b', 'A9', 'A10', 'A11', 'B', 'B1', 'B2', 'B3', 'B4',
    'B5', 'B6', 'B7', 'C1', 'C2', 'C3'];
  results.sort((x, y) => order.indexOf(x.id) - order.indexOf(y.id));
  const allGreen = results.every((r) => r.ok);
  const verdict = !allGreen ? 'CLOSE LANE RED'
    : SKIP_BUILD ? 'CLOSE LANE PARTIAL (B2-B7 deferred; not a close)' : 'CLOSE LANE GREEN';
  const totalS = ((Date.now() - t0) / 1000).toFixed(1);
  const summary = [
    `Krate Manager Phase 6 close lane${SKIP_BUILD ? ' --skip-tauri-build' : ''}, ${new Date().toISOString()},` +
      ` wall=${totalS}s, logs ${LOG_DIR} (removed by C3)`,
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
