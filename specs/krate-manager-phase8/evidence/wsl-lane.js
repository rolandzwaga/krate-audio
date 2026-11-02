#!/usr/bin/env node
// L-WSL for Krate Manager Phase 8 (tasks.md T006, TA-10; plan.md §7.3, §7.4). Run by the MAIN LOOP at the G2 gate and
// at close (close-lane.js passes --out evidence/wsl-core.txt), never by an implementer.
//
// WHY: the `cfg(unix)` / `cfg(not(windows))` arms Phase 8 adds to krate-manager-core never compile on the Windows box:
// core::extract's FsExtractIo unix bodies (modes, symlinks), the five `_unix` tests of FR-086 (spec.md:527-530:
// `prune_removes_real_link_unix`, `posix_modes_and_links_unix`, `special_bits_and_mtime_not_applied_unix`,
// `escape_cases_unix`, `symlinked_staging_root_unix`), the `cfg(unix)` blocks of `kat_archive_stages` and
// `phase8_doubles_cases`, `entry_path_rejects_every_bad_form`'s `cfg(not(windows))` rows and test_support's
// `temp_roots_via_link` (tasks.md rule 15). WSL Ubuntu 24.04 (the CI Linux leg's image version) is the only local place
// they build and run. The lane is an early signal: a red is a red to fix before pushing, a green closes no PENDING-CI
// row (plan §7.4 `plan.md:871-873`: "recorded as \"WSL, informative\"; the rows stay PENDING-CI").
//
// WHY these rules: only `-p krate-manager-core` (WSL has no WebKitGTK, so src-tauri does not build there); the full
// `cargo test -p krate-manager-core` run (TA-10: the guard integration tests `injection`, `config_guard`, `roster` lex
// sources and walk Cargo.lock, which can differ on Linux), not plan §7.4's `extract::` filter; the target dir is
// $HOME/km-target on WSL's own file system (Phases 5-7's), never the Windows target/, never /mnt/f (which drops modes
// and links, plan.md:872-873) and never inside the repo; `--locked` keeps Cargo.lock unchanged; never `--ignored`,
// never `--features live-network` (the live-feed test never runs in a gate). cargo test runs each binary's tests in
// parallel threads (no --test-threads). This phase's first run compiles the new module and every `cfg(unix)` test
// cold (flate2 moves to core's normal dependencies), hence the 40-minute ceiling; on expiry the Linux side is ended by
// `timeout` inside WSL and our own wsl.exe tree by taskkill, and the verdict is RED "ceiling". Adapted from
// specs/krate-manager-phase7/evidence/wsl-lane.js (Phase 7 measured `WSL LANE GREEN (260 passed, 40 s)`).
//
// Usage (repo root, Windows dev box; Node only, no dependencies):
//   node specs/krate-manager-phase8/evidence/wsl-lane.js                 # log under F:/tmp/gate/km8-wsl/
//   node specs/krate-manager-phase8/evidence/wsl-lane.js --out <file>    # log to <file> (the close lane's evidence)
//   node specs/krate-manager-phase8/evidence/wsl-lane.js --self-test     # pure checks of toWslPath and verdict
// Information line: `Linux-run Phase 8 tests seen ok: <k>/5` (FR-086's five `_unix` names as `test <name> ... ok`).
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
const DEFAULT_LOG = `F:/tmp/gate/km8-wsl/wsl-core-${STAMP}.txt`;

// spec FR-086 (spec.md:527-530): the five Linux-run Phase 8 tests (information line only; close-lane.js A10 wants 5/5).
const LINUX_RUN_TESTS = [
  'extract::tests::prune_removes_real_link_unix', 'extract::tests::posix_modes_and_links_unix',
  'extract::tests::special_bits_and_mtime_not_applied_unix', 'extract::tests::escape_cases_unix',
  'extract::tests::symlinked_staging_root_unix',
];
const INFO_PREFIX = 'Linux-run Phase 8 tests seen ok:';

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
    ' && cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings' +
    ' && cargo test -p krate-manager-core --locked';
  return `timeout --kill-after=30 ${CEILING_S} bash -c '${inner}'`;
}

/**
 * Pure verdict over the lane's log and exit code. GREEN only when the exit code is 0, the log holds at least one
 * `test result:` line, every such line says `0 failed`, and no line starts with `warning:`; otherwise RED with the
 * first reason. Also returns which of LINUX_RUN_TESTS appeared as `test <name> ... ok` (information only: a missing
 * name does not turn the verdict red; the close lane's A10 row wants 5/5).
 */
function verdict(log, exitCode) {
  const lines = log.split(/\r?\n/);
  const results = lines.filter((l) => l.startsWith('test result:'));
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

  // FR-086's five `_unix` names (spec.md:527-530), spelled here (not read from LINUX_RUN_TESTS) so a wrong table reads
  // red. The literal prefix is spelled too, so a stale INFO_PREFIX reads red.
  const fr086 = ['extract::tests::prune_removes_real_link_unix', 'extract::tests::posix_modes_and_links_unix',
    'extract::tests::special_bits_and_mtime_not_applied_unix', 'extract::tests::escape_cases_unix',
    'extract::tests::symlinked_staging_root_unix'];
  const info = 'Linux-run Phase 8 tests seen ok:';
  const okLines = fr086.map((n) => `test ${n} ... ok`);
  const tail = [
    'test result: ok. 262 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 9.50s',
    'test result: ok. 3 passed; 0 failed; 1 ignored; 0 measured; 0 filtered out; finished in 0.10s',
  ];
  const head = [
    '    Checking krate-manager-core v0.1.0',
    '    Finished `dev` profile [unoptimized + debuginfo] target(s) in 1.00s',
  ];
  const greenLog = [...head, 'test extract::tests::kat_archive_stages ... ok', ...okLines, ...tail].join('\r\n');
  const g = verdict(greenLog, 0);
  expect('5/5 names ok -> GREEN', g.green, true);
  expect('green log passed count', g.passed, 265);
  expect('5/5 names ok -> info 5/5', infoLine(g.seen), `${info} 5/5`);
  expect('laneCommand keeps $HOME/km-target and core only', laneCommand('/mnt/f/projects/iterum'),
    `timeout --kill-after=30 ${40 * 60} bash -c 'cd /mnt/f/projects/iterum/apps/krate-manager` +
    ' && export CARGO_TARGET_DIR=$HOME/km-target' +
    ' && cargo clippy -p krate-manager-core --all-targets --locked -- -D warnings' +
    " && cargo test -p krate-manager-core --locked'");
  expect('default log under km8-wsl', DEFAULT_LOG.startsWith('F:/tmp/gate/km8-wsl/wsl-core-'), true);

  const failedLog = `${greenLog}\ntest result: FAILED. 1 passed; 1 failed; 0 ignored; 0 measured; 0 filtered out`;
  expect('FAILED line -> RED', verdict(failedLog, 0).green, false);
  expect('warning line -> RED', verdict(`warning: unused import: \`std::os::unix\`\n${greenLog}`, 0).green, false);
  expect('exit 101 -> RED', verdict(greenLog, 101).green, false);
  expect('no test result -> RED', verdict('    Finished `dev` profile target(s) in 1.00s', 0).green, false);

  const fourLog = [...head, ...okLines.filter((l) => !l.includes('escape_cases_unix')), ...tail].join('\n');
  const four = verdict(fourLog, 0);
  expect('one name missing -> still GREEN', four.green, true);
  expect('one name missing -> info 4/5', infoLine(four.seen),
    `${info} 4/5 (missing: extract::tests::escape_cases_unix)`);
  const failedName = verdict(greenLog.replace('posix_modes_and_links_unix ... ok', 'posix_modes_and_links_unix ... FAILED'), 0);
  expect('a name reported FAILED is not counted as seen', infoLine(failedName.seen).startsWith(`${info} 4/5`), true);

  if (failures.length) {
    for (const f of failures) console.log(`self-test FAIL ${f}`);
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

module.exports = { toWslPath, verdict, infoLine, laneCommand };
if (require.main === module) main();
