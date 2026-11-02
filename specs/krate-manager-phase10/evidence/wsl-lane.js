#!/usr/bin/env node
// L-WSL for Krate Manager Phase 10 (tasks.md T017, TA-11, Rule 15; plan.md §7.4 step 4, SI-22). Run by the MAIN LOOP at
// the G2 gate and at close (close-lane.js passes --out evidence/wsl-core.txt), never by an implementer.
//
// WHY: the `cfg(unix)` arms never compile on the Windows box: Phase 9's core::install unix bodies and tests, and now the
// helper crate's `cfg(unix)` seams (PD-7: os.rs's O_NONBLOCK / O_NOFOLLOW opens, the 0700 run dir, the wait timeout) and
// its `_unix` tests. WSL Ubuntu 24.04 (the CI Linux leg's image version) is the only local place they build and run.
// The lane is an early signal: a red is a red to fix before pushing, a green closes no PENDING-CI row (tasks.md:214).
//
// WHY these rules: `-p krate-manager-core -p krate-manager-helper` only (WSL has no WebKitGTK, so src-tauri does not
// build there); the helper's `--features ci-temp-roots` clippy too (FR-103a's feature build has its own `cfg` arms);
// the full `cargo test` of both crates (TA-11: the guard integration tests lex sources and walk Cargo.lock, which can
// differ on Linux), not a filter; the target dir is $HOME/km-target on WSL's own file system, never the Windows target/,
// never /mnt/f (which drops modes and links) and never inside the repo; `--locked` keeps Cargo.lock unchanged; never
// `--ignored`, never `--features live-network`. cargo test runs each binary's tests in parallel threads (no thread-count
// flag, Rule 6). This phase's first run compiles the helper crate and every new `cfg(unix)` test cold, hence the
// 40-minute ceiling; on expiry the Linux side is ended by `timeout` inside WSL and our own wsl.exe tree by taskkill, and
// the verdict is RED "ceiling". Adapted from specs/krate-manager-phase9/evidence/wsl-lane.js (Phase 9 measured
// `WSL LANE GREEN (338 passed, 64 s)`, specs/krate-manager-phase9/evidence/timings.md row A10).
//
// Usage (repo root, Windows dev box; Node only, no dependencies):
//   node specs/krate-manager-phase10/evidence/wsl-lane.js                 # log under F:/tmp/gate/km10-wsl/
//   node specs/krate-manager-phase10/evidence/wsl-lane.js --out <file>    # log to <file> (the close lane's evidence)
//   node specs/krate-manager-phase10/evidence/wsl-lane.js --self-test     # pure checks of toWslPath and verdict
// Information line: `Linux-run Phase 9+10 tests seen ok: <k>/13` (the 13 names as `test <name> ... ok`, TA-11).
// Last line: `WSL LANE GREEN (<n> passed, <s> s)` exit 0 | `WSL LANE RED: <reason>` exit 1 |
// `WSL LANE SKIPPED: <reason>` exit 2 (no cargo or cc in WSL; recorded, never green).
// It writes nothing but its log and stdout.
'use strict';
const childProcess = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const CEILING_S = 40 * 60;
const STAMP = new Date().toISOString().replace(/[-:]/g, '').replace(/\..*/, '').replace('T', '-');
const DEFAULT_LOG = `F:/tmp/gate/km10-wsl/wsl-core-${STAMP}.txt`;

// TA-11 (tasks.md:256): Phase 9's nine Linux-run names (specs/krate-manager-phase9/tasks.md:894-902; the
// `install::tests::*` `_unix` names plus `native_wiring_installs_linux`) and the helper's four (`cargo.linux`,
// tests/required-tests.json). Information line only: a missing name does not turn the verdict red.
const LINUX_RUN_TESTS = [
  ...['install_update_remove_cycle_unix', 'crash_after_each_step_recovers_unix', 'manifest_outside_roots_refused_unix',
    'io_failures_roll_back_unix', 'collisions_refused_before_write_unix', 'non_utf8_root_refused_unix',
    'symlinked_plugin_root_unix', 'remove_bundle_link_unix', 'native_wiring_installs_linux']
    .map((name) => `install::tests::${name}`),
  'validate::tests::symlinked_parent_root_unix',
  'os::tests::open_source_refuses_links_and_fifos_unix',
  'os::tests::run_dir_mode_unix',
  'os::tests::wait_timeout_leaves_child_unix',
];
const INFO_PREFIX = 'Linux-run Phase 9+10 tests seen ok:';

/** `F:\projects\iterum` -> `/mnt/f/projects/iterum` (drive letter lower-cased, `\` -> `/`). */
function toWslPath(winPath) {
  const m = /^([A-Za-z]):[\\/]*(.*)$/.exec(winPath);
  if (!m) throw new Error(`not a drive path: ${winPath}`);
  const rest = m[2].replace(/\\/g, '/').replace(/\/+$/, '');
  return `/mnt/${m[1].toLowerCase()}${rest ? `/${rest}` : ''}`;
}

/** The bash command run inside WSL (tasks.md L-WSL), under a Linux-side `timeout` so the cargo tree ends at the ceiling. */
function laneCommand(wslRoot) {
  const inner = `cd ${wslRoot}/apps/krate-manager && export CARGO_TARGET_DIR=$HOME/km-target` +
    ' && cargo clippy -p krate-manager-core -p krate-manager-helper --all-targets --locked -- -D warnings' +
    ' && cargo clippy -p krate-manager-helper --all-targets --features ci-temp-roots --locked -- -D warnings' +
    ' && cargo test -p krate-manager-core -p krate-manager-helper --locked';
  return `timeout --kill-after=30 ${CEILING_S} bash -c '${inner}'`;
}

/**
 * Pure verdict over the lane's log and exit code. GREEN only when the exit code is 0, the log holds at least one
 * `test result:` line, every such line says `0 failed`, and no line starts with `warning:`; otherwise RED with the
 * first reason. Also returns which of LINUX_RUN_TESTS appeared as `test <name> ... ok` (information only).
 */
function verdict(log, exitCode) {
  const lines = log.split(/\r?\n/);
  // A summary is "test result: ok." / "test result: FAILED."; the helper's `result` module prints test lines that
  // start "test result::tests::", which a bare 'test result:' prefix also matched (the first Phase 10 close lane).
  const results = lines.filter((l) => /^test result: (ok|FAILED)\./.test(l));
  const passed = results.reduce((n, l) => n + Number((/(\d+) passed/.exec(l) || [0, 0])[1]), 0);
  const seen = LINUX_RUN_TESTS.filter((name) => lines.some((l) => l.trim() === `test ${name} ... ok`));
  const red = (reason) => ({ green: false, reason, passed, seen });
  if (exitCode !== 0) return red(`exit=${exitCode}`);
  if (results.length === 0) return red('no "test result:" line in the log');
  const failedLine = results.find((l) => !/\b0 failed\b/.test(l));
  if (failedLine) return red(`a test binary failed: "${failedLine.trim()}"`);
  const warning = lines.find((l) => l.startsWith('warning:'));
  if (warning) return red(`warning line: "${warning.trim()}"`);
  return { green: true, reason: '', passed, seen };
}

/** The information line over a verdict's `seen` list. */
function infoLine(seen) {
  const missing = LINUX_RUN_TESTS.filter((n) => !seen.includes(n));
  return `${INFO_PREFIX} ${seen.length}/${LINUX_RUN_TESTS.length}` + (missing.length ? ` (missing: ${missing.join(', ')})` : '');
}

function selfTest() {
  const failures = [];
  let cases = 0;
  const expect = (name, got, want) => {
    cases++;
    if (got !== want) failures.push(`${name}: got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`);
  };
  expect('toWslPath repo', toWslPath('F:\\projects\\iterum'), '/mnt/f/projects/iterum');
  expect('toWslPath C:\\x\\y', toWslPath('C:\\x\\y'), '/mnt/c/x/y');
  expect('toWslPath trailing slash', toWslPath('F:/projects/iterum/'), '/mnt/f/projects/iterum');
  expect('toWslPath drive root', toWslPath('D:\\'), '/mnt/d');
  let threw = false;
  try { toWslPath('/already/unix'); } catch { threw = true; }
  expect('toWslPath refuses a non-drive path', threw, true);

  // TA-11's 13 names, spelled here (not read from LINUX_RUN_TESTS) so a wrong table reads red. The literal prefix is
  // spelled too, so a stale INFO_PREFIX reads red.
  const thirteen = ['install_update_remove_cycle_unix', 'crash_after_each_step_recovers_unix',
    'manifest_outside_roots_refused_unix', 'io_failures_roll_back_unix', 'collisions_refused_before_write_unix',
    'non_utf8_root_refused_unix', 'symlinked_plugin_root_unix', 'remove_bundle_link_unix', 'native_wiring_installs_linux']
    .map((n) => `install::tests::${n}`)
    .concat(['validate::tests::symlinked_parent_root_unix', 'os::tests::open_source_refuses_links_and_fifos_unix',
      'os::tests::run_dir_mode_unix', 'os::tests::wait_timeout_leaves_child_unix']);
  expect('LINUX_RUN_TESTS is the 13 names of TA-11', LINUX_RUN_TESTS.join(','), thirteen.join(','));
  const info = 'Linux-run Phase 9+10 tests seen ok:';
  const okLines = thirteen.map((n) => `test ${n} ... ok`);
  const tail = [
    'test result: ok. 300 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 9.50s',
    'test result: ok. 60 passed; 0 failed; 1 ignored; 0 measured; 0 filtered out; finished in 0.10s',
  ];
  const head = [
    '    Checking krate-manager-helper v0.1.0',
    '    Finished `dev` profile [unoptimized + debuginfo] target(s) in 1.00s',
  ];
  const greenLog = [...head, 'test install::tests::install_update_remove_cycle ... ok',
    'test result::tests::log_tail_bounds ... ok', ...okLines, ...tail].join('\r\n');
  const g = verdict(greenLog, 0);
  expect('13/13 names ok -> GREEN', g.green, true);
  expect('green log passed count', g.passed, 360);
  expect('13/13 names ok -> info 13/13', infoLine(g.seen), `${info} 13/13`);
  expect('laneCommand: $HOME/km-target, core + helper, the feature clippy, both crates tested',
    laneCommand('/mnt/f/projects/iterum'),
    `timeout --kill-after=30 ${40 * 60} bash -c 'cd /mnt/f/projects/iterum/apps/krate-manager` +
    ' && export CARGO_TARGET_DIR=$HOME/km-target' +
    ' && cargo clippy -p krate-manager-core -p krate-manager-helper --all-targets --locked -- -D warnings' +
    ' && cargo clippy -p krate-manager-helper --all-targets --features ci-temp-roots --locked -- -D warnings' +
    " && cargo test -p krate-manager-core -p krate-manager-helper --locked'");
  expect('laneCommand holds no thread-count flag (Rule 6)', laneCommand('/mnt/f/x').includes(['--test', 'threads'].join('-')), false);
  expect('ceiling 40 min', CEILING_S, 2400);
  expect('default log under km10-wsl', DEFAULT_LOG.startsWith('F:/tmp/gate/km10-wsl/wsl-core-'), true);

  const failedLog = `${greenLog}\ntest result: FAILED. 1 passed; 1 failed; 0 ignored; 0 measured; 0 filtered out`;
  const f = verdict(failedLog, 0);
  expect('test result: FAILED -> RED', f.green, false);
  expect('test result: FAILED -> reason names the line', f.reason.startsWith('a test binary failed: "test result: FAILED.'), true);
  expect('warning line -> RED', verdict(`warning: unused import: \`std::os::unix\`\n${greenLog}`, 0).green, false);
  expect('exit 101 -> RED', verdict(greenLog, 101).green, false);
  expect('exit 101 -> reason', verdict(greenLog, 101).reason, 'exit=101');
  expect('no test result -> RED', verdict('    Finished `dev` profile target(s) in 1.00s', 0).green, false);

  const missingName = 'os::tests::wait_timeout_leaves_child_unix';
  const twelveLog = [...head, ...okLines.filter((l) => !l.includes(missingName)), ...tail].join('\n');
  const twelve = verdict(twelveLog, 0);
  expect('one name missing -> still GREEN', twelve.green, true);
  expect('one name missing -> info 12/13 names it', infoLine(twelve.seen), `${info} 12/13 (missing: ${missingName})`);
  const failedName = verdict(greenLog.replace('run_dir_mode_unix ... ok', 'run_dir_mode_unix ... FAILED'), 0);
  expect('a name reported FAILED is not counted as seen', infoLine(failedName.seen).startsWith(`${info} 12/13`), true);
  // The non-suffixed SC-001 test must not count as its `_unix` twin (exact-line match).
  const twinOnly = verdict([...head, 'test install::tests::install_update_remove_cycle ... ok', ...tail].join('\n'), 0);
  expect('the Windows twin is not the _unix name', twinOnly.seen.length, 0);

  if (failures.length) {
    for (const x of failures) console.log(`self-test FAIL ${x}`);
    console.log(`WSL LANE SELF-TEST RED (${failures.length} of ${cases} failed)`);
    return 1;
  }
  console.log(`WSL LANE SELF-TEST GREEN (${cases} cases)`);
  return 0;
}

/** Ends wsl.exe and its descendants on this side (the Linux side is ended by `timeout`). */
function killTree(child) {
  if (process.platform === 'win32') {
    childProcess.spawnSync('taskkill', ['/PID', String(child.pid), '/T', '/F'], { windowsHide: true });
  } else {
    child.kill('SIGKILL');
  }
}

function argValue(flag) {
  const i = process.argv.indexOf(flag);
  return i >= 0 ? process.argv[i + 1] : undefined;
}

function main() {
  if (process.argv.includes('--self-test')) {
    process.exitCode = selfTest();
    return;
  }
  const out = argValue('--out') || DEFAULT_LOG;
  const logPath = path.resolve(ROOT, out);

  const pre = childProcess.spawnSync('wsl', ['-e', 'bash', '-lc', 'command -v cargo && command -v cc'],
    { encoding: 'utf8', windowsHide: true, timeout: 120 * 1000 });
  if (pre.error || pre.status !== 0) {
    const why = pre.error ? pre.error.message : `exit=${pre.status} ${(pre.stdout + pre.stderr).trim()}`;
    console.log(`WSL LANE SKIPPED: preflight "command -v cargo && command -v cc" failed (${why})`);
    process.exitCode = 2;
    return;
  }

  fs.mkdirSync(path.dirname(logPath), { recursive: true });
  const sink = fs.createWriteStream(logPath);
  const cmd = laneCommand(toWslPath(ROOT));
  sink.write(`$ wsl -e bash -lc ${JSON.stringify(cmd)}\n`);
  const chunks = [];
  const t0 = Date.now();
  let timedOut = false;
  const child = childProcess.spawn('wsl', ['-e', 'bash', '-lc', cmd], { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  // One minute beyond the Linux-side `timeout`, so that one normally ends the run first.
  const timer = setTimeout(() => { timedOut = true; killTree(child); }, (CEILING_S + 60) * 1000);
  const take = (d) => { chunks.push(d.toString()); sink.write(d); };
  child.stdout.on('data', take);
  child.stderr.on('data', take);
  child.on('error', (e) => take(`spawn error: ${e.message}\n`));
  child.on('close', (code) => {
    clearTimeout(timer);
    const wallS = ((Date.now() - t0) / 1000).toFixed(0);
    sink.end(() => {
      const log = chunks.join('');
      // `timeout` exits 124 (or 137 after --kill-after) when the ceiling is reached.
      const ceiling = timedOut || code === 124 || code === 137;
      const v = verdict(log, code === null ? -1 : code);
      console.log(`log: ${logPath}`);
      console.log(infoLine(v.seen));
      if (ceiling) {
        console.log(`WSL LANE RED: ceiling (${CEILING_S} s reached, exit=${code})`);
        process.exitCode = 1;
      } else if (v.green) {
        console.log(`WSL LANE GREEN (${v.passed} passed, ${wallS} s)`);
        process.exitCode = 0;
      } else {
        console.log(`WSL LANE RED: ${v.reason}`);
        process.exitCode = 1;
      }
    });
  });
}

module.exports = { toWslPath, verdict, infoLine, laneCommand, LINUX_RUN_TESTS, INFO_PREFIX };
if (require.main === module) main();
