#!/usr/bin/env node
// L-WSL for Krate Manager Phase 5 (tasks.md T001, TA-18; plan.md §7.4 "L-WSL", §8; spec R-15). Run by the MAIN LOOP at
// every group gate and at close (close-lane.js passes --out evidence/wsl-core.txt), never by an implementer.
//
// WHY: under R-15 the in-process PAC engine (`rquickjs`) and the manager's PAC fetch are `cfg(not(windows))`, so the
// Windows box never compiles or runs them; WSL Ubuntu 24.04 (the CI Linux leg's image version) is the only local place
// they build and run. The lane is an early signal: a red is a red to fix before pushing, a green closes no PENDING-CI
// row (plan §7.3, §8).
//
// WHY these rules: only `-p krate-manager-core` (WSL has no WebKitGTK, so src-tauri does not build there); the target
// dir is $HOME/km-target on WSL's own file system, never the Windows target/ and never inside the repo; `--locked` keeps
// Cargo.lock unchanged; never `--ignored`, never `--features live-network` (FR-072: the live-feed test never runs in a
// gate). cargo test runs each binary's tests in parallel threads (no --test-threads). The first run is a cold build of
// ring + rquickjs-sys, hence the 40-minute ceiling; on expiry the Linux side is ended by `timeout` inside WSL and our own
// wsl.exe tree by taskkill, and the verdict is RED "ceiling".
//
// Usage (repo root, Windows dev box; Node only, no dependencies):
//   node specs/krate-manager-phase5/evidence/wsl-lane.js                 # log under F:/tmp/gate/km5-wsl/
//   node specs/krate-manager-phase5/evidence/wsl-lane.js --out <file>    # log to <file> (the close lane's evidence)
//   node specs/krate-manager-phase5/evidence/wsl-lane.js --self-test     # pure checks of toWslPath and verdict
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
const DEFAULT_LOG = `F:/tmp/gate/km5-wsl/wsl-core-${STAMP}.txt`;

// plan §4.13: the thirteen `cfg(not(windows))` tests, absent from the Windows test binary (information line only).
const NON_WINDOWS_TESTS = [
  'pac::tests::url_argument_cases', 'pac::tests::script_world_cases', 'pac::tests::script_failure_cases',
  'pac::tests::dns_bound_cases', 'http::tests::pac_path_proxy', 'http::tests::pac_path_direct',
  'http::tests::pac_list_walk', 'http::tests::pac_per_hop', 'http::tests::pac_fetched_once_never_via_proxy',
  'http::tests::wpad_discovery_walk', 'http::tests::pac_fallback_to_static_proxy', 'http::tests::pac_body_cap',
  'feed::tests::check_survives_pac_failure',
];

/** `F:\projects\iterum` -> `/mnt/f/projects/iterum` (drive letter lower-cased, `\` -> `/`). */
function toWslPath(winPath) {
  const m = /^([A-Za-z]):[\\/]*(.*)$/.exec(winPath);
  if (!m) throw new Error(`not a drive path: ${winPath}`);
  const rest = m[2].replace(/\\/g, '/').replace(/\/+$/, '');
  return `/mnt/${m[1].toLowerCase()}${rest ? `/${rest}` : ''}`;
}

/** The bash command run inside WSL (plan §7.4), under a Linux-side `timeout` so the cargo tree ends at the ceiling. */
function laneCommand(wslRoot) {
  const inner = `cd ${wslRoot}/apps/krate-manager && export CARGO_TARGET_DIR=$HOME/km-target` +
    ' && cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings' +
    ' && cargo test -p krate-manager-core --locked';
  return `timeout --kill-after=30 ${CEILING_S} bash -c '${inner}'`;
}

/**
 * Pure verdict over the lane's log and exit code. GREEN only when the exit code is 0, the log holds at least one
 * `test result:` line, every such line says `0 failed`, and no line starts with `warning:`; otherwise RED with the
 * first reason. Also returns which of NON_WINDOWS_TESTS appeared as `test <name> ... ok`.
 */
function verdict(log, exitCode) {
  const lines = log.split(/\r?\n/);
  const results = lines.filter((l) => l.startsWith('test result:'));
  const passed = results.reduce((n, l) => n + Number((/(\d+) passed/.exec(l) || [0, 0])[1]), 0);
  const seen = NON_WINDOWS_TESTS.filter((name) => lines.some((l) => l.trim() === `test ${name} ... ok`));
  const red = (reason) => ({ green: false, reason, passed, seen });
  if (exitCode !== 0) return red(`exit=${exitCode}`);
  if (results.length === 0) return red('no "test result:" line in the log');
  const failedLine = results.find((l) => !/\b0 failed\b/.test(l));
  if (failedLine) return red(`a test binary failed: "${failedLine.trim()}"`);
  const warning = lines.find((l) => l.startsWith('warning:'));
  if (warning) return red(`warning line: "${warning.trim()}"`);
  return { green: true, reason: '', passed, seen };
}

function selfTest() {
  const failures = [];
  const expect = (name, got, want) => {
    if (got !== want) failures.push(`${name}: got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`);
  };
  expect('toWslPath repo', toWslPath('F:\\projects\\iterum'), '/mnt/f/projects/iterum');
  expect('toWslPath C:\\x\\y', toWslPath('C:\\x\\y'), '/mnt/c/x/y');

  const greenLog = [
    '    Checking krate-manager-core v0.1.0',
    '    Finished `dev` profile [unoptimized + debuginfo] target(s) in 1.00s',
    'test pac::tests::script_world_cases ... ok',
    'test http::tests::pac_path_proxy ... ok',
    'test result: ok. 12 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.50s',
    'test result: ok. 3 passed; 0 failed; 1 ignored; 0 measured; 0 filtered out; finished in 0.10s',
  ].join('\n');
  const g = verdict(greenLog, 0);
  expect('green log -> GREEN', g.green, true);
  expect('green log passed count', g.passed, 15);
  expect('green log seen names', g.seen.join(','), 'pac::tests::script_world_cases,http::tests::pac_path_proxy');

  const failedLog = `${greenLog}\ntest result: FAILED. 1 passed; 1 failed; 0 ignored; 0 measured; 0 filtered out`;
  expect('FAILED line -> RED', verdict(failedLog, 0).green, false);
  expect('warning line -> RED', verdict(`warning: unused import: \`std::io\`\n${greenLog}`, 0).green, false);
  expect('exit 101 -> RED', verdict(greenLog, 101).green, false);
  expect('no test result -> RED', verdict('    Finished `dev` profile target(s) in 1.00s', 0).green, false);

  if (failures.length) {
    for (const f of failures) console.log(`self-test FAIL ${f}`);
    console.log(`WSL LANE SELF-TEST RED (${failures.length} failed)`);
    return 1;
  }
  console.log('WSL LANE SELF-TEST GREEN (2 path cases, 5 verdict logs)');
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
      console.log(`cfg(not(windows)) tests seen ok: ${v.seen.length}/${NON_WINDOWS_TESTS.length}` +
        (v.seen.length < NON_WINDOWS_TESTS.length
          ? ` (missing: ${NON_WINDOWS_TESTS.filter((n) => !v.seen.includes(n)).join(', ')})` : ''));
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

module.exports = { toWslPath, verdict, laneCommand };
if (require.main === module) main();
