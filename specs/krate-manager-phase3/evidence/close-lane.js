#!/usr/bin/env node
// Close lane for Krate Manager Phase 3 (L-CLOSE; tasks.md T019; plan.md §7.4, §8). Run by the MAIN LOOP at close, never
// by an implementer or a gate: it holds the shared_tests build, the [update] gate, the single SC-009 case by name, the
// Ninja reconfigure and clang-tidy on the one TU, and the measured safety rows (the real update_state.json stat, the
// git status around the node run).
//
// WHY one script: the close rows must come from one run whose steps share a clock and a log directory, with every
// independent step concurrent (CLAUDE.md "Optimise Every Build and Test Run") and every child under a wall-clock
// ceiling, so a hung step reads RED instead of stalling the close. Same form as
// specs/krate-manager-phase1/evidence/close-lane.js.
//
// Usage (repo root, Windows dev box; Node only, no dependencies):
//   node specs/krate-manager-phase3/evidence/close-lane.js
// Rows:
//   A (concurrent):
//      A1  L-CXX build of shared_tests (-- /nr:false, 30 min ceiling, 0 warning lines)
//      A2  node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" to a TAP file (# fail 0, no Node
//          warning line); A2d git status --porcelain before and after, required equal (the tests write nothing in the
//          repo); A2t L-TAP on that file (tap-check.js file mode: "TAP plan=1..57 top-level=57")
//      A3  L-YAML (yaml@2.6.1) on feed.yml, release.yml, feed-tools.yml, ci.yml
//      A4  node tools/header-consumers.js --tag '[update]' (last line ends "none")
//      A5  L-LINT node tools/lint-serial-test-runs.js
//      A6  node tools/check-portability.js plugins/shared/tests/test_update_checker.cpp (explicit file: the default
//          diffs against origin/main and would sweep in Phases 1-2)
//      A7  node tools/gen-specs-index.js --check
//      A8  L-FIX node tools/feed-contract/fixtures/make-fixtures.js --check
//   B (after A1 succeeds):
//      B0  stat (mtime, size, or missing) of the nine real %APPDATA%\Krate Audio\<Name>\update_state.json files, names
//          from tools/feed-contract/fixtures/gen-feed/snapshot-2026-10-29/live-versions.json (read-only)
//      B1  L-CXX gate: run-gate.js --filter '[update]~[performance]~[perf]~[benchmark]~[!benchmark]~[.]' shared_tests
//      B2  the single case by name with -s: "9 == 9", nine of each per-name assertion, 1 test case passed, 0 failed
//      B3  the B0 stat again, required equal to B0
//      beside B1-B3:
//      B4r reconfigure build/windows-ninja (cmake --preset windows-ninja inside the VS developer environment: vswhere
//          -latest -property installationPath, VC\Auxiliary\Build\vcvars64.bat; clang-tidy-setup skill step 3)
//      B4c the compile_commands.json entry of the TU contains SHARED_FIXTURES_DIR (else RED "stale compile
//          database"; the define is never passed with --extra-arg)
//      B4  clang-tidy -p build/windows-ninja on the TU (15 min ceiling): 0 warning: / error: lines for it
//   C (last): C1 scope-check.js --self-test, then C2 scope-check.js (L-SCOPE, SC-012)
// Raw logs: F:/tmp/gate/krate-manager-phase3-close/<timestamp>/. Summary: specs/krate-manager-phase3/evidence/close-lane.log
// (written and printed). Last line: CLOSE LANE GREEN | CLOSE LANE RED. Exit 0 green, 1 red.
'use strict';
const childProcess = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const STAMP = new Date().toISOString().replace(/[-:]/g, '').replace(/\..*/, '').replace('T', '-');
const LOG_DIR = path.join('F:/tmp/gate/krate-manager-phase3-close', STAMP);
const SUMMARY = path.join(__dirname, 'close-lane.log');
const MIN = 60 * 1000;

const CMAKE = 'C:/Program Files/CMake/bin/cmake.exe';
const CLANG_TIDY = 'C:/Program Files/LLVM/bin/clang-tidy.exe'; // tools/run-clang-tidy.ps1:92-94
const VSWHERE = 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe';
const NINJA_DIR = path.join(ROOT, 'build/windows-ninja'); // CMakePresets.json "base" binaryDir build/${presetName}
const TU = 'plugins/shared/tests/test_update_checker.cpp';
const GATE_FILTER = '[update]~[performance]~[perf]~[benchmark]~[!benchmark]~[.]'; // tasks.md L-CXX; run-gate.js:51-53
const SC009_CASE = 'UpdateChecker - one v2 plugin among eight v1 plugins is read as v1';
// The per-name assertions of the case (test_update_checker.cpp:495-525), each expected once per live name.
const PER_NAME = [
  'REQUIRE( isUnder(settingsDir, settings.root()) )',
  'REQUIRE( isUnder(afterLive, settings.root()) )',
  'REQUIRE( std::filesystem::exists(afterLive) )',
  'REQUIRE( isUnder(afterMixed, settings.root()) )',
  'REQUIRE( std::filesystem::exists(afterMixed) )',
  'CHECK( onMixed.updateAvailable )',
  'CHECK( onMixed.updateAvailable == onLive.updateAvailable )',
  'CHECK( onMixed.latestVersion == onLive.latestVersion )',
  'CHECK( onMixed.downloadUrl == onLive.downloadUrl )',
  'CHECK( onMixed.releaseNotes.empty() )',
  'CHECK( onMixed.releaseNotes == onLive.releaseNotes )',
];
const SHARED_TESTS_EXE = path.join(ROOT, 'build/windows-x64-release/bin/Release/shared_tests.exe');
const LIVE = path.join(ROOT, 'tools/feed-contract/fixtures/gen-feed/snapshot-2026-10-29/live-versions.json');
const WORKFLOWS = ['feed.yml', 'release.yml', 'feed-tools.yml', 'ci.yml'].map((f) => `.github/workflows/${f}`);
const TAP_FILE = path.join(LOG_DIR, 'A2-node-test.tap');
const EXPECTED_PLAN = 57; // tasks.md rule 9: 46 + 11

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
 * { code, timedOut, wallS, out, log }. `stdinFile` feeds a file to stdin; `shell` is only for npx (a .cmd on Windows);
 * `env` replaces the environment; `verbatim` passes `args` to Windows unquoted (cmd.exe /s /c "<line>").
 */
function run(cmd, args, { log, ceilingMs, stdinFile, shell = false, env, verbatim = false }) {
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
      const spawnOpts = {
        cwd: ROOT, shell, windowsHide: true, stdio: [stdin, 'pipe', 'pipe'], windowsVerbatimArguments: verbatim,
      };
      if (env) spawnOpts.env = env;
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
const countOf = (text, needle) => text.split(needle).length - 1;

function gitStatus() {
  const r = childProcess.spawnSync('git', ['-c', 'core.quotepath=off', 'status', '--porcelain', '--untracked-files=all'],
    { cwd: ROOT, encoding: 'utf8', windowsHide: true });
  return r.status === 0 ? r.stdout : `git status failed: ${r.stderr}`;
}

/** B0 / B3 reading: last-write time and size, or "missing", of each real update_state.json. Read-only. */
function statState() {
  let names;
  try {
    names = Object.keys(JSON.parse(fs.readFileSync(LIVE, 'utf8')).plugins);
  } catch (e) {
    return [`cannot read the names from ${LIVE}: ${e.message}`];
  }
  return names.map((n) => {
    const f = path.join(process.env.APPDATA || '', 'Krate Audio', n, 'update_state.json');
    try {
      const s = fs.statSync(f);
      return `${f} mtime=${s.mtime.toISOString()} size=${s.size}`;
    } catch {
      return `${f} missing`;
    }
  });
}

// ---- A1: L-CXX build -------------------------------------------------------------------------------------------
async function a1Build() {
  const r = await run(CMAKE, ['--build', 'build/windows-x64-release', '--config', 'Release', '--target', 'shared_tests',
    '--', '/nr:false'], { log: 'A1-cxx-build.log', ceilingMs: 30 * MIN });
  // Case-sensitive: MSBuild's summary "Warning(s)" has a capital W and does not match.
  const warnings = r.out.split(/\r?\n/).filter((l) => /\bwarning\b/.test(l)).length;
  const ok = !r.timedOut && r.code === 0 && warnings === 0;
  record('A1', ok, `shared_tests build ${fail(r)} warnings=${warnings} wall=${r.wallS}s log=${r.log}`);
  return ok;
}

// ---- A2: L-NODE whole set, the git status around it, then L-TAP on its TAP file ---------------------------------
async function a2NodeTests() {
  const before = gitStatus();
  // Quoted glob, expanded by Node; default concurrency and isolation (files in parallel child processes).
  const r = await run(process.execPath, ['--test', '--test-reporter=tap', 'tools/feed-contract/tests/*.test.js'],
    { log: 'A2-node-test.log', ceilingMs: 10 * MIN });
  const after = gitStatus();
  // The log starts with the "$ cmd" line; the TAP file holds only the reporter's output.
  fs.writeFileSync(TAP_FILE, r.out);
  const num = (k) => { const m = r.out.match(new RegExp(`^# ${k} (\\d+)`, 'm')); return m ? Number(m[1]) : NaN; };
  const tests = num('tests');
  const pass = num('pass');
  const failN = num('fail');
  const skipped = num('skipped');
  const plan = (r.out.match(/^1\.\.(\d+)$/m) || [])[1];
  const nodeWarnings = r.out.split(/\r?\n/)
    .filter((l) => /\(node:\d+\) .*Warning|DeprecationWarning|ExperimentalWarning/.test(l)).length;
  const ok = !r.timedOut && r.code === 0 && failN === 0 && Number(plan) === EXPECTED_PLAN && nodeWarnings === 0;
  record('A2', ok, `node --test ${fail(r)} plan=1..${plan} tests=${tests} pass=${pass} fail=${failN} skipped=${skipped}` +
    ` nodeWarnings=${nodeWarnings} wall=${r.wallS}s tap=${TAP_FILE} log=${r.log}`);
  fs.writeFileSync(path.join(LOG_DIR, 'A2d-git-status-before.txt'), before);
  fs.writeFileSync(path.join(LOG_DIR, 'A2d-git-status-after.txt'), after);
  const same = before === after;
  record('A2d', same, `git status --porcelain across the node run ${same ? 'unchanged' : 'CHANGED'}` +
    ` (${before.split('\n').filter(Boolean).length} lines before, ${after.split('\n').filter(Boolean).length} after)` +
    ` files=${LOG_DIR}/A2d-git-status-{before,after}.txt`);

  const t = await node('specs/krate-manager-phase2/evidence/tap-check.js', [TAP_FILE],
    { log: 'A2t-tap-check.log', ceilingMs: 2 * MIN });
  const want = `TAP plan=1..${EXPECTED_PLAN} top-level=${EXPECTED_PLAN}`;
  const tOk = !t.timedOut && t.code === 0 && t.out.includes(want);
  record('A2t', tOk, `L-TAP ${fail(t)} want "${want}" ${t.out.includes(want) ? 'printed' : 'NOT printed'}` +
    ` last="${lastLine(t.out)}" log=${t.log}`);
}

// ---- A3..A8: short lanes ---------------------------------------------------------------------------------------
async function a3Yaml() {
  // One file after another: concurrent `npx --yes` runs race on the npx cache.
  for (const [i, wf] of WORKFLOWS.entries()) {
    const r = await run('npx --yes yaml@2.6.1 valid', [], {
      log: `A3-yaml-${i + 1}-${path.basename(wf, '.yml')}.log`, ceilingMs: 5 * MIN, stdinFile: wf, shell: true,
    });
    record('A3', !r.timedOut && r.code === 0, `L-YAML ${wf} ${fail(r)} wall=${r.wallS}s log=${r.log}`);
  }
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
  record('A6', !r.timedOut && r.code === 0, `check-portability ${TU} ${fail(r)} last="${lastLine(r.out)}" wall=${r.wallS}s log=${r.log}`);
}

async function a7SpecsIndex() {
  const r = await node('tools/gen-specs-index.js', ['--check'], { log: 'A7-specs-index.log', ceilingMs: 5 * MIN });
  record('A7', !r.timedOut && r.code === 0, `gen-specs-index --check ${fail(r)} last="${lastLine(r.out)}" log=${r.log}`);
}

async function a8Fixtures() {
  const r = await node('tools/feed-contract/fixtures/make-fixtures.js', ['--check'], { log: 'A8-fixtures.log', ceilingMs: 5 * MIN });
  const last = lastLine(r.out);
  const ok = !r.timedOut && r.code === 0 && last === 'make-fixtures: committed fixtures match the builder';
  record('A8', ok, `L-FIX make-fixtures --check ${fail(r)} last="${last}" log=${r.log}`);
}

// ---- B1..B3: [update] gate and the SC-009 case by name, bracketed by the real-state stat --------------------------
async function bGate() {
  const before = statState();
  record('B0', !before[0].startsWith('cannot read'), `real update_state.json stat (${before.length} files): ${before.join(' ; ')}`);
  const gateOut = path.join(LOG_DIR, 'B1-run-gate');
  const g = await node('tools/run-gate.js', ['--filter', GATE_FILTER, '--out', gateOut, 'shared_tests'],
    { log: 'B1-run-gate.log', ceilingMs: 15 * MIN });
  const gLast = lastLine(g.out);
  record('B1', !g.timedOut && g.code === 0 && gLast.startsWith('GATE GREEN'), `[update] gate ${fail(g)} last="${gLast}" log=${g.log}`);

  // The one direct single-case run CLAUDE.md permits; -s prints every passed assertion with its expansion.
  const c = await run(SHARED_TESTS_EXE, [SC009_CASE, '-s'], { log: 'B2-sc009-case.log', ceilingMs: 5 * MIN });
  const nine = /REQUIRE\( names\.size\(\) == 9 \)\r?\nwith expansion:\r?\n\s*9 == 9\r?\n/.test(c.out);
  const counts = PER_NAME.map((p) => ({ p, n: countOf(c.out, p) }));
  const failedLines = c.out.split(/\r?\n/).filter((l) => /FAILED:/.test(l)).length;
  const totals = (c.out.match(/^All tests passed \(\d+ assertions? in 1 test case\)$/m)
    || c.out.match(/^test cases: 1 \| 1 passed.*$/m) || [''])[0];
  const cOk = !c.timedOut && c.code === 0 && nine && counts.every((x) => x.n === 9) && failedLines === 0 && totals !== '';
  record('B2', cOk, `"${SC009_CASE}" by name -s ${fail(c)} "9 == 9" ${nine ? 'found' : 'MISSING'};` +
    ` per-name counts ${counts.map((x) => `${x.n}x ${x.p}`).join(', ')}; FAILED lines=${failedLines};` +
    ` totals="${totals || 'MISSING'}" log=${c.log}`);

  const after = statState();
  const same = JSON.stringify(before) === JSON.stringify(after);
  record('B3', same, `real update_state.json stat ${same ? 'unchanged' : 'CHANGED'} | after: ${after.join(' ; ')}`);
}

// ---- B4: Ninja reconfigure in the VS environment, compile-database check, clang-tidy on the TU --------------------
async function bTidy() {
  const vs = await run(VSWHERE, ['-latest', '-property', 'installationPath'], { log: 'B4r-vswhere.log', ceilingMs: 2 * MIN });
  const vsPath = lastLine(vs.out);
  const vcvars = path.join(vsPath, 'VC', 'Auxiliary', 'Build', 'vcvars64.bat');
  if (vs.code !== 0 || !vsPath || !fs.existsSync(vcvars)) {
    record('B4r', false, `VS developer environment not found headless (vswhere ${fail(vs)} path="${vsPath}"): run the` +
      ` clang-tidy-setup skill's "cmake --preset windows-ninja" from a Developer PowerShell, then clang-tidy on ${TU}`);
    return;
  }
  // cmd /s strips the outer quotes of the line; `set` prints the environment vcvars64.bat leaves behind.
  const e = await run('cmd.exe', ['/d', '/s', '/c', `""${vcvars}" >nul && set"`],
    { log: 'B4r-vcvars-env.log', ceilingMs: 3 * MIN, verbatim: true });
  const env = {};
  for (const l of e.out.split(/\r?\n/)) {
    const k = l.indexOf('=');
    if (k > 0) env[l.slice(0, k)] = l.slice(k + 1);
  }
  if (e.timedOut || e.code !== 0 || !env.VCToolsInstallDir) {
    record('B4r', false, `vcvars64.bat could not be entered headless (${fail(e)}, VCToolsInstallDir ${env.VCToolsInstallDir ? 'set' : 'unset'}):` +
      ` run the clang-tidy-setup skill's "cmake --preset windows-ninja" from a Developer PowerShell log=${e.log}`);
    return;
  }
  const cfg = await run(CMAKE, ['--preset', 'windows-ninja'], { log: 'B4r-ninja-configure.log', ceilingMs: 15 * MIN, env });
  const cfgOk = !cfg.timedOut && cfg.code === 0;
  record('B4r', cfgOk, `cmake --preset windows-ninja in ${vcvars} env ${fail(cfg)} last="${lastLine(cfg.out)}" wall=${cfg.wallS}s log=${cfg.log}`);
  if (!cfgOk) return;

  let entry;
  try {
    const db = JSON.parse(fs.readFileSync(path.join(NINJA_DIR, 'compile_commands.json'), 'utf8'));
    entry = db.find((x) => path.resolve(x.directory || '', x.file).replace(/\\/g, '/').toLowerCase()
      .endsWith(TU.toLowerCase()));
  } catch (err) {
    record('B4c', false, `cannot read ${NINJA_DIR}/compile_commands.json: ${err.message}`);
    return;
  }
  const line = entry ? (entry.command || (entry.arguments || []).join(' ')) : '';
  const dbOk = line.includes('SHARED_FIXTURES_DIR');
  record('B4c', dbOk, dbOk ? `compile_commands.json entry of ${TU} defines SHARED_FIXTURES_DIR`
    : `stale compile database: ${entry ? `the entry of ${TU} lacks SHARED_FIXTURES_DIR` : `no entry for ${TU}`}`);
  if (!dbOk) return;

  const r = await run(CLANG_TIDY, ['-p', 'build/windows-ninja', TU], { log: 'B4-clang-tidy.log', ceilingMs: 15 * MIN });
  // The repo runner drops these two Catch2 macro-expansion checks in every tests/ file (tools/run-clang-tidy.ps1:382-386);
  // counted separately so the drop stays visible.
  const catch2 = /\[(bugprone-chained-comparison|cppcoreguidelines-avoid-do-while)\]$/;
  const raw = r.out.split(/\r?\n/);
  const lines = raw.filter((l) => !catch2.test(l));
  const catch2Dropped = raw.length - lines.length;
  const tuWarnings = lines.filter((l) => /test_update_checker\.cpp:\d+:\d+: warning:/.test(l)).length;
  const tuErrors = lines.filter((l) => /test_update_checker\.cpp:\d+:\d+: error:/.test(l)).length;
  const allWarnings = lines.filter((l) => /\bwarning:/.test(l)).length;
  const errors = lines.filter((l) => /\berror:/.test(l)).length;
  const ok = !r.timedOut && r.code === 0 && tuWarnings === 0 && tuErrors === 0 && errors === 0;
  record('B4', ok, `clang-tidy ${TU} ${fail(r)} TU warnings=${tuWarnings} TU errors=${tuErrors}` +
    ` (all warning: lines=${allWarnings}, all error: lines=${errors}) catch2-filtered=${catch2Dropped}` +
    ` wall=${r.wallS}s log=${r.log}`);
}

// ---- C: scope check ----------------------------------------------------------------------------------------------
async function cScope() {
  const script = path.join(__dirname, 'scope-check.js');
  const s = await node(script, ['--self-test'], { log: 'C1-scope-self-test.log', ceilingMs: 2 * MIN });
  record('C1', !s.timedOut && s.code === 0, `scope-check --self-test ${fail(s)} last="${lastLine(s.out)}" log=${s.log}`);
  const r = await node(script, [], { log: 'C2-scope-check.log', ceilingMs: 2 * MIN });
  record('C2', !r.timedOut && r.code === 0, `scope-check ${fail(r)} last="${lastLine(r.out)}" log=${r.log}`);
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
  await Promise.all([b, a2NodeTests(), a3Yaml(), a4Consumers(), a5Lint(), a6Portability(), a7SpecsIndex(), a8Fixtures()]);
  await cScope();

  const green = results.every((r) => r.ok);
  const order = ['A1', 'A2', 'A2d', 'A2t', 'A3', 'A4', 'A5', 'A6', 'A7', 'A8', 'B', 'B0', 'B1', 'B2', 'B3', 'B4r', 'B4c',
    'B4', 'C1', 'C2'];
  // Stable sort: the four A3 rows keep their file order.
  results.sort((x, y) => order.indexOf(x.id) - order.indexOf(y.id));
  const summary = [
    `Krate Manager Phase 3 close lane, ${new Date().toISOString()}, wall=${((Date.now() - t0) / 1000).toFixed(1)}s, logs ${LOG_DIR}`,
    ...results.map((r) => `${r.id.padEnd(4)} ${r.ok ? 'GREEN' : 'RED  '} ${r.text}`),
    green ? 'CLOSE LANE GREEN' : 'CLOSE LANE RED',
  ].join('\n');
  fs.writeFileSync(SUMMARY, `${summary}\n`);
  console.log(`\n${summary}`);
  process.exit(green ? 0 : 1);
}

main();
