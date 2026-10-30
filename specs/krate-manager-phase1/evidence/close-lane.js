#!/usr/bin/env node
// Close lane for Krate Manager Phase 1 (L-CLOSE; tasks.md T020; plan.md §8). Run by the MAIN LOOP at close, never by an
// implementer or a gate: it holds the long C++ build, the [update] gate, clang-tidy on the one TU and the measured SC
// rows (SC-015d, SC-016, SC-018 local count, SC-019, SC-020, SC-021, PD-9).
//
// WHY one script: the close rows must come from one run whose steps share a clock and a log directory, with every
// independent step concurrent (CLAUDE.md "Optimise Every Build and Test Run") and every child under a wall-clock
// ceiling, so a hung step reads RED instead of stalling the close.
//
// Usage (repo root, Windows dev box; Node only, no dependencies):
//   node specs/krate-manager-phase1/evidence/close-lane.js
// Order:
//   A (concurrent): A1 shared_tests build (30 min) · A2 node --test with the SC-015(d) git status before/after ·
//      A3 L-YAML · A4 header-consumers --tag '[update]' · A5 L-LINT · A6 check-portability on the TU ·
//      A7 gen-specs-index --check (Q-B ruled IN, spec.md Clarifications Session 2026-10-30 [PD-9])
//   B (after A1 succeeds): SC-021 stat · [update] gate (15 min) · the v2 case by name, junit · SC-021 stat again;
//      beside it, clang-tidy on plugins/shared/tests/test_update_checker.cpp (15 min)
//   C (last): scope-check.js (SC-020)
// Raw logs: F:/tmp/gate/krate-manager-phase1-close/<timestamp>/. Summary: specs/krate-manager-phase1/evidence/close-lane.log
// (written and printed). Last line: CLOSE LANE GREEN | CLOSE LANE RED. Exit 0 green, 1 red.
'use strict';
const childProcess = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const STAMP = new Date().toISOString().replace(/[-:]/g, '').replace(/\..*/, '').replace('T', '-');
const LOG_DIR = path.join('F:/tmp/gate/krate-manager-phase1-close', STAMP);
const SUMMARY = path.join(__dirname, 'close-lane.log');
const MIN = 60 * 1000;

const CMAKE = 'C:/Program Files/CMake/bin/cmake.exe';
const CLANG_TIDY = 'C:/Program Files/LLVM/bin/clang-tidy.exe'; // tools/run-clang-tidy.ps1:92-94
const TU = 'plugins/shared/tests/test_update_checker.cpp';
const GATE_FILTER = '[update]~[performance]~[perf]~[benchmark]~[!benchmark]~[.]'; // SC-016; run-gate.js:52-54
const V2_CASE = 'UpdateChecker - feed schema v2 is read as v1';
const V2_SECTIONS = ['v2 document', 'schema_version 99'];
const SHARED_TESTS_EXE = path.join(ROOT, 'build/windows-x64-release/bin/Release/shared_tests.exe');
const STATE_FILES = ['Iterum', 'Ruinae'].map((p) =>
  path.join(process.env.APPDATA || '', 'Krate Audio', p, 'update_state.json')); // SC-021

const results = []; // { id, ok, text }
function record(id, ok, text) {
  results.push({ id, ok, text });
  console.log(`${id.padEnd(4)} ${ok ? 'GREEN' : 'RED  '} ${text}`);
}

/** Kills a child and its descendants (cmake -> MSBuild -> cl, npx -> cmd -> node). */
function killTree(child) {
  if (process.platform === 'win32') {
    childProcess.spawnSync('taskkill', ['/PID', String(child.pid), '/T', '/F'], { windowsHide: true });
  } else {
    child.kill('SIGKILL');
  }
}

/**
 * Runs one command under a wall-clock ceiling, streams stdout+stderr to <LOG_DIR>/<log>, and resolves
 * { code, timedOut, wallS, out, log }. `stdinFile` feeds a file to stdin; `shell` is only for npx (a .cmd on Windows).
 */
function run(cmd, args, { log, ceilingMs, stdinFile, shell = false }) {
  return new Promise((resolve) => {
    const logPath = path.join(LOG_DIR, log);
    const sink = fs.createWriteStream(logPath);
    const stdin = stdinFile ? fs.openSync(path.join(ROOT, stdinFile), 'r') : 'ignore';
    const t0 = Date.now();
    const chunks = [];
    let timedOut = false;
    let child;
    let timer;
    const finish = (code) => {
      clearTimeout(timer);
      if (typeof stdin === 'number') fs.closeSync(stdin);
      sink.end(() => resolve({
        code, timedOut, wallS: ((Date.now() - t0) / 1000).toFixed(1), out: chunks.join(''), log: logPath,
      }));
    };
    sink.write(`$ ${cmd} ${args.join(' ')}${stdinFile ? ` < ${stdinFile}` : ''}\n`);
    try {
      const spawnOpts = { cwd: ROOT, shell, windowsHide: true, stdio: [stdin, 'pipe', 'pipe'] };
      // A shell command is one fixed string with no args array (Node DEP0190 warns on args with shell: true).
      child = shell ? childProcess.spawn(cmd, spawnOpts) : childProcess.spawn(cmd, args, spawnOpts);
    } catch (e) {
      chunks.push(String(e.message));
      return finish(-1);
    }
    timer = setTimeout(() => { timedOut = true; killTree(child); }, ceilingMs);
    for (const s of [child.stdout, child.stderr]) {
      s.on('data', (d) => { chunks.push(d.toString()); sink.write(d); });
    }
    child.on('error', (e) => { chunks.push(`spawn error: ${e.message}\n`); });
    child.on('close', (code) => finish(code === null ? -1 : code));
  });
}

const node = (script, args, opts) => run(process.execPath, [script, ...args], opts);
const lastLine = (text) => text.split(/\r?\n/).map((l) => l.trim()).filter(Boolean).pop() || '';
const fail = (r) => (r.timedOut ? `TIMEOUT (killed after ${r.wallS}s)` : `exit=${r.code}`);

function gitStatus() {
  const r = childProcess.spawnSync('git', ['-c', 'core.quotepath=off', 'status', '--porcelain', '--untracked-files=all'],
    { cwd: ROOT, encoding: 'utf8', windowsHide: true });
  return r.status === 0 ? r.stdout : `git status failed: ${r.stderr}`;
}

/** SC-021 reading: last-write time and size, or "missing". Read-only. */
function statState() {
  return STATE_FILES.map((f) => {
    try {
      const s = fs.statSync(f);
      return `${f} mtime=${s.mtime.toISOString()} size=${s.size}`;
    } catch {
      return `${f} missing`;
    }
  });
}

// ---- A1: L-CPP build -------------------------------------------------------------------------------------------
async function a1Build() {
  const r = await run(CMAKE, ['--build', 'build/windows-x64-release', '--config', 'Release', '--target', 'shared_tests',
    '--', '/nr:false'], { log: 'A1-cpp-build.log', ceilingMs: 30 * MIN });
  // Case-sensitive: MSBuild's summary "Warning(s)" has a capital W and does not match (tasks.md L-CPP).
  const warnings = r.out.split(/\r?\n/).filter((l) => /\bwarning\b/.test(l)).length;
  const ok = !r.timedOut && r.code === 0 && warnings === 0;
  record('A1', ok, `shared_tests build ${fail(r)} warnings=${warnings} wall=${r.wallS}s log=${r.log}`);
  return ok;
}

// ---- A2: L-NODE whole set + SC-015(d) --------------------------------------------------------------------------
async function a2NodeTests() {
  const before = gitStatus();
  // Quoted glob, expanded by Node (a directory argument runs nothing on Node 24); default concurrency and isolation.
  const r = await run(process.execPath, ['--test', '--test-reporter=tap', 'tools/feed-contract/tests/*.test.js'],
    { log: 'A2-node-test.log', ceilingMs: 10 * MIN });
  const after = gitStatus();
  const num = (k) => { const m = r.out.match(new RegExp(`^# ${k} (\\d+)`, 'm')); return m ? Number(m[1]) : NaN; };
  const tests = num('tests');
  const pass = num('pass');
  const failN = num('fail');
  const nodeWarnings = r.out.split(/\r?\n/).filter((l) => /\(node:\d+\) .*Warning/.test(l)).length;
  const ok = !r.timedOut && r.code === 0 && failN === 0 && tests >= 21 && nodeWarnings === 0;
  record('A2', ok, `node --test ${fail(r)} TAP tests=${tests} pass=${pass} fail=${failN} nodeWarnings=${nodeWarnings}` +
    ` wall=${r.wallS}s (SC-019 measurement; SC-018 local count) log=${r.log}`);
  fs.writeFileSync(path.join(LOG_DIR, 'A2d-git-status-before.txt'), before);
  fs.writeFileSync(path.join(LOG_DIR, 'A2d-git-status-after.txt'), after);
  const same = before === after;
  record('A2d', same, `git status --porcelain across the node run ${same ? 'unchanged' : 'CHANGED'}` +
    ` (${before.split('\n').filter(Boolean).length} lines before, ${after.split('\n').filter(Boolean).length} after;` +
    ` SC-015d) files=${LOG_DIR}/A2d-git-status-{before,after}.txt`);
}

// ---- A3..A7: short lanes ---------------------------------------------------------------------------------------
async function a3Yaml() {
  const r = await run('npx --yes yaml@2.9.1 valid', [], {
    log: 'A3-yaml.log', ceilingMs: 5 * MIN, stdinFile: '.github/workflows/feed-tools.yml', shell: true,
  });
  record('A3', !r.timedOut && r.code === 0, `L-YAML feed-tools.yml ${fail(r)} wall=${r.wallS}s log=${r.log}`);
}

async function a4Consumers() {
  const r = await node('tools/header-consumers.js', ['--tag', '[update]'], { log: 'A4-consumers.log', ceilingMs: 5 * MIN });
  const last = lastLine(r.out);
  const ok = !r.timedOut && r.code === 0 && last === 'CONSUMERS OUTSIDE [update]: none';
  record('A4', ok, `header-consumers ${fail(r)} last="${last}" log=${r.log}`);
}

async function a5Lint() {
  const r = await node('tools/lint-serial-test-runs.js', [], { log: 'A5-lint-serial.log', ceilingMs: 5 * MIN });
  record('A5', !r.timedOut && r.code === 0, `L-LINT lint-serial-test-runs ${fail(r)} last="${lastLine(r.out)}" log=${r.log}`);
}

async function a6Portability() {
  const r = await node('tools/check-portability.js', [TU], { log: 'A6-portability.log', ceilingMs: 10 * MIN });
  record('A6', !r.timedOut && r.code === 0, `check-portability ${TU} ${fail(r)} wall=${r.wallS}s log=${r.log}`);
}

async function a7SpecsIndex() {
  const r = await node('tools/gen-specs-index.js', ['--check'], { log: 'A7-specs-index.log', ceilingMs: 5 * MIN });
  record('A7', !r.timedOut && r.code === 0, `gen-specs-index --check ${fail(r)} (PD-9) log=${r.log}`);
}

// ---- B: [update] gate bracketed by SC-021 stat, beside clang-tidy on the TU ------------------------------------
async function bGate() {
  const before = statState();
  const gateOut = path.join(LOG_DIR, 'B1-run-gate');
  const g = await node('tools/run-gate.js', ['--filter', GATE_FILTER, '--out', gateOut, 'shared_tests'],
    { log: 'B1-run-gate.log', ceilingMs: 15 * MIN });
  const gLast = lastLine(g.out);
  record('B1', !g.timedOut && g.code === 0 && gLast.startsWith('GATE GREEN'), `[update] gate ${fail(g)} last="${gLast}" log=${g.log}`);

  // The shard logs use Catch2's default reporter, which names failed cases only, so a passing case is absent from
  // them: count the mentions (expected 0 when green) and prove the case and both sections by one run by name.
  let mentions = 0;
  try {
    for (const f of fs.readdirSync(gateOut)) {
      mentions += fs.readFileSync(path.join(gateOut, f), 'utf8').split(V2_CASE).length - 1;
    }
  } catch { /* no shard logs: B1 is already RED */ }
  const xml = path.join(LOG_DIR, 'B2-v2-case.junit.xml');
  const c = await run(SHARED_TESTS_EXE, [V2_CASE, '--reporter', 'junit', '--out', xml],
    { log: 'B2-v2-case.log', ceilingMs: 5 * MIN });
  let junit = '';
  try { junit = fs.readFileSync(xml, 'utf8'); } catch { /* missing report: RED below */ }
  const sectionOk = V2_SECTIONS.map((s) => {
    const esc = (t) => t.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const m = junit.match(new RegExp(`<testcase [^>]*name="${esc(`${V2_CASE}/${s}`)}"[^>]*?(/?)>`));
    return { s, ok: Boolean(m) && m[1] === '/' }; // self-closing = no <failure> child
  });
  const suiteOk = /<testsuite [^>]*errors="0" failures="0"/.test(junit);
  const cOk = !c.timedOut && c.code === 0 && suiteOk && sectionOk.every((x) => x.ok);
  record('B2', cOk, `"${V2_CASE}" by name ${fail(c)} sections: ${sectionOk.map((x) => `${x.s}=${x.ok ? 'passed' : 'MISSING/FAILED'}`).join(', ')}` +
    `; shard-log mentions=${mentions} report=${xml}`);

  const after = statState();
  const same = JSON.stringify(before) === JSON.stringify(after);
  record('B3', same, `SC-021 stat ${same ? 'unchanged' : 'CHANGED'} | before: ${before.join(' ; ')} | after: ${after.join(' ; ')}`);
}

async function bTidy() {
  // Reconfigure build/windows-ninja only if a test CMake file changed (tasks.md T020); none should.
  const cmakeChanged = gitStatus().split(/\r?\n/).filter((l) => /(CMakeLists\.txt|\.cmake)"?$/.test(l));
  if (cmakeChanged.length) {
    record('B4', false, `clang-tidy not run: a CMake file changed, reconfigure build/windows-ninja first: ${cmakeChanged.join(' ; ')}`);
    return;
  }
  const r = await run(CLANG_TIDY, ['-p', 'build/windows-ninja', TU], { log: 'B4-clang-tidy.log', ceilingMs: 15 * MIN });
  // The repo runner drops these two Catch2 macro-expansion checks in every tests/ file (tools/run-clang-tidy.ps1:382-386,
  // the same set dsp/tests/.clang-tidy disables); counted separately so the drop stays visible.
  const catch2 = /\[(bugprone-chained-comparison|cppcoreguidelines-avoid-do-while)\]$/;
  const raw = r.out.split(/\r?\n/);
  const lines = raw.filter((l) => !catch2.test(l));
  const catch2Dropped = raw.length - lines.length;
  const tuWarnings = lines.filter((l) => /test_update_checker\.cpp:\d+:\d+: warning:/.test(l)).length;
  const allWarnings = lines.filter((l) => /\bwarning:/.test(l)).length;
  const errors = lines.filter((l) => /\berror:/.test(l)).length;
  const ok = !r.timedOut && r.code === 0 && tuWarnings === 0 && errors === 0;
  record('B4', ok, `clang-tidy ${TU} ${fail(r)} TU warnings=${tuWarnings} (all warning: lines=${allWarnings}) errors=${errors}` +
    ` catch2-filtered=${catch2Dropped}` +
    ` wall=${r.wallS}s log=${r.log}`);
}

// ---- C: scope check ----------------------------------------------------------------------------------------------
async function cScope() {
  const r = await node(path.join(__dirname, 'scope-check.js'), [], { log: 'C-scope-check.log', ceilingMs: 2 * MIN });
  record('C', !r.timedOut && r.code === 0, `scope-check ${fail(r)} last="${lastLine(r.out)}" log=${r.log}`);
}

async function main() {
  fs.mkdirSync(LOG_DIR, { recursive: true });
  const t0 = Date.now();
  console.log(`close lane: logs ${LOG_DIR}`);
  const b = a1Build().then((ok) => {
    if (ok) return Promise.all([bGate(), bTidy()]);
    record('B', false, 'skipped: the shared_tests build (A1) is not green');
    return undefined;
  });
  await Promise.all([b, a2NodeTests(), a3Yaml(), a4Consumers(), a5Lint(), a6Portability(), a7SpecsIndex()]);
  await cScope();

  const green = results.every((r) => r.ok);
  const order = ['A1', 'A2', 'A2d', 'A3', 'A4', 'A5', 'A6', 'A7', 'B', 'B1', 'B2', 'B3', 'B4', 'C'];
  results.sort((x, y) => order.indexOf(x.id) - order.indexOf(y.id));
  const summary = [
    `Krate Manager Phase 1 close lane, ${new Date().toISOString()}, wall=${((Date.now() - t0) / 1000).toFixed(1)}s, logs ${LOG_DIR}`,
    ...results.map((r) => `${r.id.padEnd(4)} ${r.ok ? 'GREEN' : 'RED  '} ${r.text}`),
    green ? 'CLOSE LANE GREEN' : 'CLOSE LANE RED',
  ].join('\n');
  fs.writeFileSync(SUMMARY, `${summary}\n`);
  console.log(`\n${summary}`);
  process.exit(green ? 0 : 1);
}

main();
