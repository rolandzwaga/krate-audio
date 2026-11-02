#!/usr/bin/env node
// L-WSL for Krate Manager Phase 12 (tasks.md T015, lane L-WSL; plan.md §7.4, §8). Run by the MAIN LOOP at close
// (close-lane.js passes --out evidence/wsl-core.txt), never by an implementer or a gate. A copy of
// specs/krate-manager-phase11/evidence/wsl-lane.js with Phase 12 paths, log names and the Phase 12 information line
// (tasks.md T015 second bullet).
//
// WHY: the `cfg(unix)` arms never compile on the Windows box: Phase 9's core::install unix bodies and tests, Phase 10's
// helper `cfg(unix)` seams and `_unix` tests, and every non-Windows arm this phase's code reaches. WSL Ubuntu 24.04 (the
// CI Linux leg's image version) is the only local place they build and run. The lane is an early signal: a red is a red
// to fix before pushing, a green closes no PENDING-CI row (tasks.md L-WSL).
//
// WHY these rules (unchanged from Phase 11): `-p krate-manager-core -p krate-manager-helper` only (src-tauri needs
// WebKitGTK); the helper's `--features ci-temp-roots` clippy too; the full `cargo test` of both crates, not a filter; the
// target dir is $HOME/km-target on WSL's own file system, never the Windows target/, never /mnt/f and never inside the
// repo; `--locked` keeps Cargo.lock unchanged; never `--ignored`, never `--features live-network`. cargo test runs each
// binary's tests in parallel threads (no thread-count flag, Rule 6). Ceiling 40 minutes; on expiry the Linux side is ended
// by `timeout` inside WSL and our own wsl.exe tree by taskkill, and the verdict is RED "ceiling".
//
// Information lines (never turn the verdict red):
//   `Linux-run Phase 9+10 tests seen ok: <k>/13`: Phase 10's 13 `_unix` / Linux names, unchanged since Phase 11.
//   `Phase 12 required cargo tests seen ok: <k>/34`: TA-1's 34 names appended to tests/required-tests.json cargo.all by
//   T001 step 13 (tasks.md:325-335). Every one runs on all three legs (SI-18; plan §7.2), and the four injection.rs cases
//   are core integration tests, so all 34 run in this lane's core + helper `cargo test`.
//
// Usage (repo root, Windows dev box; Node only, no dependencies):
//   node specs/krate-manager-phase12/evidence/wsl-lane.js                 # log under F:/tmp/gate/km12-wsl/
//   node specs/krate-manager-phase12/evidence/wsl-lane.js --out <file>    # log to <file> (the close lane's evidence)
//   node specs/krate-manager-phase12/evidence/wsl-lane.js --self-test     # pure checks; no WSL, no cargo
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
const DEFAULT_LOG = `F:/tmp/gate/km12-wsl/wsl-core-${STAMP}.txt`;

// Phase 10's TA-11 list (specs/krate-manager-phase11/evidence/wsl-lane.js:41-50).
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

// TA-1 / T001 step 13 (tasks.md:325-335): the 34 cargo names, in the file's order, as cargo prints them (core lib and
// helper lib tests by module path, injection.rs cases bare).
const PHASE12_TESTS = [
  ...['one_launch_windows', 'one_launch_macos', 'no_launch_linux', 'download_bound', 'production_bound_is_three',
    'installs_after_downloads', 'locked_item_continues', 'notice_cases', 'download_failure_excluded',
    'staging_full_excluded', 'state_changed_items', 'busy_items', 'input_checks', 'refused_prompt', 'elevation_busy',
    'registrar_kill_once', 'progress_mapping', 'log_lines', 'batch_level_errors'].map((n) => `install::batch::${n}`),
  'install::tests::orphan_remove_cases',
  'install::tests::not_manageable_is_not_orphan',
  'install::tests::manifest_heads_cases',
  'fixture_server::tests::held_reply_releases',
  'settings::tests::save_for_command_cases',
  'settings::tests::view_cases',
  'settings::tests::root_gate_cases',
  'platform::tests::graphics_env_cases',
  'platform::tests::apply_graphics_env_cases',
  'status::tests::orphans_cases',
  'no_process_enumeration',
  'one_set_var_site_before_threads',
  'no_cancel_setter',
  'plugin_commands_take_the_root_ticket',
  'job::tests::core_and_helper_op_caps_agree',
];
const PHASE12_PREFIX = 'Phase 12 required cargo tests seen ok:';

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

/** Which of `names` appear in the log as `test <name> ... ok`. */
function seenOk(lines, names) {
  return names.filter((name) => lines.some((l) => l.trim() === `test ${name} ... ok`));
}

/**
 * Pure verdict over the lane's log and exit code. GREEN only when the exit code is 0, the log holds at least one
 * `test result:` line, every such line says `0 failed`, and no line starts with `warning:`; otherwise RED with the
 * first reason. Also returns which of LINUX_RUN_TESTS (`seen`) and PHASE12_TESTS (`seen12`) appeared as
 * `test <name> ... ok` (information only).
 */
function verdict(log, exitCode) {
  const lines = log.split(/\r?\n/);
  // A summary is "test result: ok." / "test result: FAILED."; the helper's `result` module prints test lines that
  // start "test result::tests::", which a bare 'test result:' prefix would also match.
  const results = lines.filter((l) => /^test result: (ok|FAILED)\./.test(l));
  const passed = results.reduce((n, l) => n + Number((/(\d+) passed/.exec(l) || [0, 0])[1]), 0);
  const seen = seenOk(lines, LINUX_RUN_TESTS);
  const seen12 = seenOk(lines, PHASE12_TESTS);
  const red = (reason) => ({ green: false, reason, passed, seen, seen12 });
  if (exitCode !== 0) return red(`exit=${exitCode}`);
  if (results.length === 0) return red('no "test result:" line in the log');
  const failedLine = results.find((l) => !/\b0 failed\b/.test(l));
  if (failedLine) return red(`a test binary failed: "${failedLine.trim()}"`);
  const warning = lines.find((l) => l.startsWith('warning:'));
  if (warning) return red(`warning line: "${warning.trim()}"`);
  return { green: true, reason: '', passed, seen, seen12 };
}

/** An information line: `<prefix> <k>/<n>`, plus the missing names. */
function countLine(prefix, all, seen) {
  const missing = all.filter((n) => !seen.includes(n));
  return `${prefix} ${seen.length}/${all.length}` + (missing.length ? ` (missing: ${missing.join(', ')})` : '');
}
const infoLine = (seen) => countLine(INFO_PREFIX, LINUX_RUN_TESTS, seen);
const phase12Line = (seen12) => countLine(PHASE12_PREFIX, PHASE12_TESTS, seen12);

function selfTest() {
  const failures = [];
  let cases = 0;
  const expect = (name, fn, want) => {
    cases++;
    let got;
    try { got = fn(); } catch (e) { got = `threw: ${e.message}`; }
    if (got !== want) failures.push(`${name}: got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`);
  };
  expect('toWslPath repo', () => toWslPath('F:\\projects\\iterum'), '/mnt/f/projects/iterum');
  expect('toWslPath C:\\x\\y', () => toWslPath('C:\\x\\y'), '/mnt/c/x/y');
  expect('toWslPath trailing slash', () => toWslPath('F:/projects/iterum/'), '/mnt/f/projects/iterum');
  expect('toWslPath drive root', () => toWslPath('D:\\'), '/mnt/d');
  expect('toWslPath refuses a non-drive path', () => {
    try { toWslPath('/already/unix'); } catch (e) { return /not a drive path/.test(e.message); }
    return false;
  }, true);

  // The 13 names, spelled here (not read from LINUX_RUN_TESTS) so a wrong table reads red.
  const thirteen = ['install_update_remove_cycle_unix', 'crash_after_each_step_recovers_unix',
    'manifest_outside_roots_refused_unix', 'io_failures_roll_back_unix', 'collisions_refused_before_write_unix',
    'non_utf8_root_refused_unix', 'symlinked_plugin_root_unix', 'remove_bundle_link_unix', 'native_wiring_installs_linux']
    .map((n) => `install::tests::${n}`)
    .concat(['validate::tests::symlinked_parent_root_unix', 'os::tests::open_source_refuses_links_and_fifos_unix',
      'os::tests::run_dir_mode_unix', 'os::tests::wait_timeout_leaves_child_unix']);
  expect('LINUX_RUN_TESTS is Phase 10\'s 13 names', () => LINUX_RUN_TESTS.join(','), thirteen.join(','));
  expect('PHASE12_TESTS holds 34 distinct names', () => new Set(PHASE12_TESTS).size, 34);
  expect('PHASE12_TESTS: 19 install::batch names', () => PHASE12_TESTS.filter((n) => n.startsWith('install::batch::')).length, 19);
  // T001 step 13 appends the 34 to cargo.all in this order, so they are its last 34 entries (TA-1).
  expect('PHASE12_TESTS are the last 34 of tests/required-tests.json cargo.all, in order', () => {
    const req = JSON.parse(fs.readFileSync(path.join(ROOT, 'apps', 'krate-manager', 'tests', 'required-tests.json'), 'utf8'));
    return req.cargo.all.slice(-34).join(',');
  }, PHASE12_TESTS.join(','));

  const info = 'Linux-run Phase 9+10 tests seen ok:';
  const info12 = 'Phase 12 required cargo tests seen ok:';
  const okLines = [...thirteen, ...PHASE12_TESTS].map((n) => `test ${n} ... ok`);
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
  expect('all names ok -> GREEN', () => verdict(greenLog, 0).green, true);
  expect('green log passed count', () => verdict(greenLog, 0).passed, 360);
  expect('13/13 names ok -> info 13/13', () => infoLine(verdict(greenLog, 0).seen), `${info} 13/13`);
  expect('34/34 names ok -> info 34/34', () => phase12Line(verdict(greenLog, 0).seen12), `${info12} 34/34`);
  expect('laneCommand: $HOME/km-target, core + helper, the feature clippy, both crates tested',
    () => laneCommand('/mnt/f/projects/iterum'),
    `timeout --kill-after=30 ${40 * 60} bash -c 'cd /mnt/f/projects/iterum/apps/krate-manager` +
    ' && export CARGO_TARGET_DIR=$HOME/km-target' +
    ' && cargo clippy -p krate-manager-core -p krate-manager-helper --all-targets --locked -- -D warnings' +
    ' && cargo clippy -p krate-manager-helper --all-targets --features ci-temp-roots --locked -- -D warnings' +
    " && cargo test -p krate-manager-core -p krate-manager-helper --locked'");
  expect('laneCommand holds no thread-count flag (Rule 6)',
    () => laneCommand('/mnt/f/x').includes(['--test', 'threads'].join('-')), false);
  expect('laneCommand never runs ignored tests or live-network', () => /--ignored|live-network/.test(laneCommand('/mnt/f/x')), false);
  expect('ceiling 40 min', () => CEILING_S, 2400);
  expect('default log under km12-wsl', () => DEFAULT_LOG.startsWith('F:/tmp/gate/km12-wsl/wsl-core-'), true);

  const failedLog = `${greenLog}\ntest result: FAILED. 1 passed; 1 failed; 0 ignored; 0 measured; 0 filtered out`;
  expect('test result: FAILED -> RED', () => verdict(failedLog, 0).green, false);
  expect('test result: FAILED -> reason names the line',
    () => verdict(failedLog, 0).reason.startsWith('a test binary failed: "test result: FAILED.'), true);
  expect('warning line -> RED', () => verdict(`warning: unused import: \`std::os::unix\`\n${greenLog}`, 0).green, false);
  expect('exit 101 -> RED', () => verdict(greenLog, 101).green, false);
  expect('exit 101 -> reason', () => verdict(greenLog, 101).reason, 'exit=101');
  expect('no test result -> RED', () => verdict('    Finished `dev` profile target(s) in 1.00s', 0).green, false);

  const missingName = 'os::tests::wait_timeout_leaves_child_unix';
  const fewerLog = [...head, ...okLines.filter((l) => !l.includes(missingName)), ...tail].join('\n');
  expect('one Phase 10 name missing -> still GREEN', () => verdict(fewerLog, 0).green, true);
  expect('one Phase 10 name missing -> info 12/13 names it', () => infoLine(verdict(fewerLog, 0).seen),
    `${info} 12/13 (missing: ${missingName})`);
  const missing12 = 'install::batch::busy_items';
  const fewer12 = [...head, ...okLines.filter((l) => l !== `test ${missing12} ... ok`), ...tail].join('\n');
  expect('one Phase 12 name missing -> still GREEN', () => verdict(fewer12, 0).green, true);
  expect('one Phase 12 name missing -> info 33/34 names it', () => phase12Line(verdict(fewer12, 0).seen12),
    `${info12} 33/34 (missing: ${missing12})`);
  expect('a name reported FAILED is not counted as seen', () => infoLine(verdict(
    greenLog.replace('run_dir_mode_unix ... ok', 'run_dir_mode_unix ... FAILED'), 0).seen).startsWith(`${info} 12/13`), true);
  expect('a bare injection.rs name matches only the bare line', () => verdict(
    [...head, 'test x::no_cancel_setter ... ok', ...tail].join('\n'), 0).seen12.length, 0);
  expect('the Windows twin is not the _unix name', () => verdict(
    [...head, 'test install::tests::install_update_remove_cycle ... ok', ...tail].join('\n'), 0).seen.length, 0);

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
      console.log(phase12Line(v.seen12));
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

module.exports = { toWslPath, verdict, infoLine, phase12Line, laneCommand, killTree, LINUX_RUN_TESTS, PHASE12_TESTS,
  INFO_PREFIX, PHASE12_PREFIX, CEILING_S };
if (require.main === module) main();
