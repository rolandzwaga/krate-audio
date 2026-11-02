#!/usr/bin/env node
// L-HARNESS for Krate Manager Phase 11 (tasks.md T016, lane L-HARNESS, T018 step 2; spec D9, FR-080 to FR-084, SC-007
// local row; plan.md §4.7, §7.4 step 3, §8.4; PD-33, PD-38; TA-22). Run at the G2 / G3 gates and by close-lane.js.
//
// WHY: SC-007's local row needs one green run of the refusal harness (tests/fixtures/updater-refusals/), with its five
// commands (tasks.md L-HARNESS): cargo fmt --check, cargo clippy -D warnings, cargo run (the 27 scenario lines), and the
// two plant runs (--plant committed-key, --plant bind-any). The pass rules are L-HARNESS's: fmt / clippy exit 0 with 0
// `warning:` lines; the run exits 0 with each of the 27 names `ok` exactly once, the summary `harness: 27 ok, 0 not ok`
// and `harness: wall <ms> ms`; each plant exits 1 with its code as its first and only stdout line and no `ok ` line
// (PD-38: a plant run spawns only the `good` child).
//
// WHY d9-windows.txt decides where the run happens (TA-22, PD-33, ruling P-5): T014 measured whether the harness loads
// on Windows with the manifest embedded and wrote one line. `D9 WINDOWS LOADS`: all five commands on Windows, in order,
// in the shared target dir (CARGO_TARGET_DIR="$PWD/target", as CI). `D9 WINDOWS LOAD FAILURE ...`: fmt and clippy still on
// Windows; the run and both plants in WSL Ubuntu 24.04 (plan §7.4 step 3) from the /mnt/<drive>/ path of the checkout
// (toWslPath, the copy of Phase 10's in wsl-lane.js), with CARGO_TARGET_DIR=$HOME/km-target-harness (WSL's own file
// system, never /mnt/f, never the Windows target/, never L-WSL's $HOME/km-target) under `timeout --kill-after=30` with a
// 40-minute ceiling (specs/krate-manager-phase10/evidence/wsl-lane.js:35, :68); the Windows chain and the WSL chain run
// concurrently (separate target dirs, no build lock between them; CLAUDE.md "Optimise every build and test run"). A WSL
// that cannot build (no webkit2gtk-4.1: R-17's packages, plan R-18) is `RED: WSL <reason>`, never green, never skipped. A
// missing d9 file: the Windows run is made, and a loader failure there (0xc0000139 or any loader status before the first
// output line) is `RED: d9 not recorded`, because T014 owns the measurement. A malformed d9 file is red at once.
//
// Usage (repo root, Windows dev box; Node only, no dependencies):
//   node specs/krate-manager-phase11/evidence/harness-lane.js                    # logs under F:/tmp/gate/km11-harness/
//   node specs/krate-manager-phase11/evidence/harness-lane.js --log-dir <dir>    # logs under <dir> (close-lane.js)
//   node specs/krate-manager-phase11/evidence/harness-lane.js --self-test        # pure checks: no cargo, no WSL
// Last line: `HARNESS LANE GREEN (Windows, <n> ok, <ms> ms)` | `HARNESS LANE GREEN (WSL, <n> ok, <ms> ms; Windows gap:
// <d9 line>)` exit 0 | `HARNESS LANE RED: <rows>` exit 1. It writes only its logs and stdout.
'use strict';
const childProcess = require('child_process');
const fs = require('fs');
const path = require('path');
const { toWslPath, killTree } = require('./wsl-lane.js');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const APP = path.join(ROOT, 'apps', 'krate-manager');
const MANIFEST = 'tests/fixtures/updater-refusals/Cargo.toml';
const D9_FILE = path.join(__dirname, 'd9-windows.txt');
const STAMP = new Date().toISOString().replace(/[-:]/g, '').replace(/\..*/, '').replace('T', '-');
const DEFAULT_LOG_DIR = `F:/tmp/gate/km11-harness/${STAMP}`;
const MIN = 60 * 1000;
const WSL_CEILING_S = 40 * 60;
const WSL_TARGET = '$HOME/km-target-harness';
// tasks.md T014 (the 27 line names, sorted): 26 scenarios plus `confinement`.
const HARNESS_NAMES = ['api-host', 'busy', 'busy-after-download', 'confinement', 'corrupt-manifest', 'deb-install',
  'downgrade', 'endpoint-latest-path', 'failed-install-clears', 'good', 'latest-path', 'map-errors', 'no-platform',
  'offline', 'other-key', 'production-inputs', 'recheck-replaces', 'redirect-loop', 'redirect-to-api-host',
  'redirect-to-http', 'replayed-old', 'rsign-format', 'same-version', 'socks-proxy', 'stall', 'tampered', 'unversioned'];
// plan.md §4.7 (PD-38): each plant's code, the first and only stdout line's first token.
const PLANTS = [['committed-key', 'E_HARNESS_KEY'], ['bind-any', 'E_HARNESS_BIND']];
// Windows loader statuses (STATUS_DLL_NOT_FOUND, STATUS_ENTRYPOINT_NOT_FOUND, STATUS_DLL_INIT_FAILED,
// STATUS_INVALID_IMAGE_FORMAT): a child that ends with one before its first stdout line never ran its main.
const LOADER_STATUSES = [0xC0000135, 0xC0000139, 0xC0000142, 0xC000007B];

// ---- pure helpers (covered by --self-test) ----------------------------------------------------------------------
const textLines = (text) => text.split(/\r?\n/).map((l) => l.trim()).filter(Boolean);

/**
 * d9-windows.txt (TA-22): exactly one line, `D9 WINDOWS LOADS`, or `D9 WINDOWS LOAD FAILURE exit=<code> ...` holding the
 * code as 0x hex. `text === null` is a missing file. Returns { kind: loads | failure | missing | malformed, line }.
 */
function parseD9(text) {
  if (text === null) return { kind: 'missing', line: '' };
  const all = text.split(/\r?\n/);
  if (all.length && all[all.length - 1] === '') all.pop();
  const line = all.length === 1 ? all[0] : '';
  if (line === 'D9 WINDOWS LOADS') return { kind: 'loads', line };
  if (/^D9 WINDOWS LOAD FAILURE exit=\S+/.test(line) && /\b0x[0-9a-fA-F]{1,8}\b/.test(line)) return { kind: 'failure', line };
  return { kind: 'malformed', line: all.join(' / ') };
}

/** True when a run ended with a Windows loader status before printing any stdout line (D9's failure mode). */
function loaderFailure(code, stdout, stderr) {
  if (code === 0 || textLines(stdout).length > 0) return false;
  if (typeof code === 'number' && LOADER_STATUSES.includes(code >>> 0)) return true;
  const hex = LOADER_STATUSES.map((s) => `0x${s.toString(16)}`).join('|');
  return new RegExp(`\\bexit code: (?:${hex})\\b`, 'i').test(stderr);
}

/**
 * The harness run's stdout (L-HARNESS): each of HARNESS_NAMES as `ok <name>` exactly once, no `not ok` line, no other
 * `ok` name, one summary `harness: 27 ok, 0 not ok`, one `harness: wall <ms> ms`, exit 0. Returns
 * { ok, okCount, wallMs, reasons }.
 */
function parseRun(stdout, code) {
  const all = textLines(stdout);
  const reasons = [];
  const counts = new Map(HARNESS_NAMES.map((n) => [n, 0]));
  for (const l of all) {
    const m = /^ok (\S+)$/.exec(l);
    if (m) {
      if (counts.has(m[1])) counts.set(m[1], counts.get(m[1]) + 1);
      else reasons.push(`unknown line "${l}"`);
    } else if (l.startsWith('not ok ')) {
      reasons.push(`"${l}"`);
    }
  }
  for (const [n, c] of counts) if (c !== 1) reasons.push(`ok ${n} x${c} (want 1)`);
  const okCount = [...counts.values()].filter((c) => c === 1).length;
  const summaries = all.filter((l) => /^harness: \d+ ok, \d+ not ok$/.test(l));
  const wantSummary = `harness: ${HARNESS_NAMES.length} ok, 0 not ok`;
  if (summaries.length !== 1 || summaries[0] !== wantSummary) {
    reasons.push(`summary ${JSON.stringify(summaries)} (want one "${wantSummary}")`);
  }
  const walls = all.map((l) => /^harness: wall (\d+) ms$/.exec(l)).filter(Boolean);
  if (walls.length !== 1) reasons.push(`${walls.length} "harness: wall <ms> ms" line(s) (want 1)`);
  const wallMs = walls.length === 1 ? Number(walls[0][1]) : NaN;
  if (code !== 0) reasons.push(`exit=${code}`);
  return { ok: reasons.length === 0, okCount, wallMs, reasons };
}

/** One plant run: exit 1, exactly one stdout line, starting with the plant's code, no `ok ` line (PD-38). */
function parsePlant(plant, stdout, code) {
  const entry = PLANTS.find(([name]) => name === plant);
  if (!entry) return { ok: false, reason: `unknown plant ${plant}` };
  const all = textLines(stdout);
  const reasons = [];
  if (code !== 1) reasons.push(`exit=${code} (want 1)`);
  if (all.length !== 1) reasons.push(`${all.length} stdout line(s) (want 1)`);
  if (!all.length || !new RegExp(`^${entry[1]}(?::|\\s|$)`).test(all[0])) reasons.push(`first line is not ${entry[1]}`);
  if (all.some((l) => l.startsWith('ok '))) reasons.push('an "ok " line (PD-38: only the good child runs)');
  return { ok: reasons.length === 0, reason: reasons.join(', ') };
}

/** The command run inside WSL for the run (`extra` empty) or a plant (`extra` = ['--plant', name]). */
function wslCommand(wslApp, extra) {
  const tail = extra.length ? ` -- ${extra.join(' ')}` : '';
  return `cd ${wslApp} && export CARGO_TARGET_DIR=${WSL_TARGET}` +
    ` && timeout --kill-after=30 ${WSL_CEILING_S} cargo run --manifest-path ${MANIFEST} --locked${tail}`;
}

/**
 * The lane's last line from the d9 reading, the rows ({ id, ok, text }), where the run happened ('Windows' | 'WSL') and
 * the run's parse (okCount, wallMs).
 */
function finalVerdict({ d9, rows, place, run }) {
  if (d9.kind === 'malformed') return `HARNESS LANE RED: d9 malformed: ${JSON.stringify(d9.line)}`;
  if (rows.length === 0) return 'HARNESS LANE RED: no rows';
  const red = rows.filter((r) => !r.ok);
  if (red.length) return `HARNESS LANE RED: ${red.map((r) => `${r.id} ${r.text}`).join('; ')}`;
  // PD-33: under LOAD FAILURE a Windows run is never the pass; WSL is the only green place.
  if (d9.kind === 'failure' && place !== 'WSL') return `HARNESS LANE RED: d9 reads "${d9.line}" but the run was on ${place}`;
  if (!run) return 'HARNESS LANE RED: no run parsed';
  if (place === 'WSL') {
    return `HARNESS LANE GREEN (WSL, ${run.okCount} ok, ${run.wallMs} ms; Windows gap: ${d9.line})`;
  }
  return `HARNESS LANE GREEN (Windows, ${run.okCount} ok, ${run.wallMs} ms)`;
}

function selfTest() {
  const failures = [];
  let cases = 0;
  const check = (name, fn) => {
    cases++;
    try { fn(); } catch (e) { failures.push(`${name}: ${e.message}`); }
  };
  const eq = (got, want) => { if (got !== want) throw new Error(`got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`); };

  // T014's 27 names, spelled here (not read from HARNESS_NAMES) so a wrong table reads red.
  const names = 'api-host busy busy-after-download confinement corrupt-manifest deb-install downgrade endpoint-latest-path' +
    ' failed-install-clears good latest-path map-errors no-platform offline other-key production-inputs' +
    ' recheck-replaces redirect-loop redirect-to-api-host redirect-to-http replayed-old rsign-format same-version' +
    ' socks-proxy stall tampered unversioned';
  check('HARNESS_NAMES is T014\'s 27, sorted', () => {
    eq(HARNESS_NAMES.join(' '), names);
    eq(HARNESS_NAMES.length, 27);
    eq([...HARNESS_NAMES].sort().join(' '), names);
  });
  check('plant codes (plan §4.7)', () => eq(JSON.stringify(PLANTS),
    '[["committed-key","E_HARNESS_KEY"],["bind-any","E_HARNESS_BIND"]]'));

  // d9-windows.txt, both forms and malformed ones.
  const loadsLine = 'D9 WINDOWS LOADS';
  const failLine = 'D9 WINDOWS LOAD FAILURE exit=-1073741511 (0xc0000139) no output';
  check('d9 LOADS', () => eq(parseD9(`${loadsLine}\n`).kind, 'loads'));
  check('d9 LOADS without newline', () => eq(parseD9(loadsLine).kind, 'loads'));
  check('d9 LOAD FAILURE', () => {
    const d = parseD9(`${failLine}\n`);
    eq(d.kind, 'failure');
    eq(d.line, failLine);
  });
  check('d9 LOAD FAILURE, unsigned code and a loader message', () => eq(parseD9(
    'D9 WINDOWS LOAD FAILURE exit=3221225785 0xC0000139 The procedure entry point could not be located\n').kind, 'failure'));
  check('d9 missing', () => eq(parseD9(null).kind, 'missing'));
  for (const [what, text] of [['empty', ''], ['two lines', `${loadsLine}\n${loadsLine}\n`], ['a typo', 'D9 WINDOWS LOADED\n'],
    ['failure without hex', 'D9 WINDOWS LOAD FAILURE exit=1 no output\n'], ['failure without exit=', 'D9 WINDOWS LOAD FAILURE 0xc0000139\n'],
    ['lower case', 'd9 windows loads\n']]) {
    check(`d9 malformed: ${what}`, () => eq(parseD9(text).kind, 'malformed'));
  }

  // The loader classification.
  check('loader: 0xc0000139 unsigned, no stdout', () => eq(loaderFailure(3221225785, '', ''), true));
  check('loader: 0xc0000139 signed, no stdout', () => eq(loaderFailure(-1073741511, '\r\n', ''), true));
  check('loader: cargo exit 101 naming 0xc0000139', () => eq(loaderFailure(101, '',
    "error: process didn't exit successfully: `target\\debug\\updater-refusals.exe` (exit code: 0xc0000139," +
    ' STATUS_ENTRYPOINT_NOT_FOUND)'), true));
  check('loader: a panic before output is not a loader failure', () => eq(loaderFailure(101, '',
    "thread 'main' panicked at src/main.rs:4:5:\nnot yet implemented: T014"), false));
  check('loader: a loader status after output is not D9', () => eq(loaderFailure(3221225785, 'not ok good x\n', ''), false));
  check('loader: exit 0 is not a loader failure', () => eq(loaderFailure(0, '', ''), false));

  // The run parser.
  const shuffled = [...HARNESS_NAMES].reverse();
  const green = [...shuffled.map((n) => `ok ${n}`), 'harness: 27 ok, 0 not ok', 'harness: wall 2345 ms', ''].join('\r\n');
  check('run green', () => {
    const r = parseRun(green, 0);
    eq(r.ok, true);
    eq(r.okCount, 27);
    eq(r.wallMs, 2345);
    eq(r.reasons.length, 0);
  });
  const redRun = green.replace('ok stall\r\n', 'not ok stall no stall error within 5 s\r\n')
    .replace('harness: 27 ok, 0 not ok', 'harness: 26 ok, 1 not ok');
  check('run red: a not ok line, summary 26/1, exit 1', () => {
    const r = parseRun(redRun, 1);
    eq(r.ok, false);
    eq(r.okCount, 26);
    eq(r.reasons.some((x) => x.includes('not ok stall no stall error')), true);
    eq(r.reasons.some((x) => x.includes('exit=1')), true);
  });
  check('run red: a duplicated ok line', () => eq(parseRun(`ok good\n${green}`, 0).ok, false));
  check('run red: a missing name', () => eq(parseRun(green.replace('ok tampered\r\n', ''), 0).ok, false));
  check('run red: an unknown name', () => eq(parseRun(`ok extra-scenario\n${green}`, 0).ok, false));
  check('run red: no wall line', () => eq(parseRun(green.replace('harness: wall 2345 ms', ''), 0).ok, false));
  check('run red: summary 27 but exit 1', () => eq(parseRun(green, 1).ok, false));
  check('run red: a name prefix is not the name (ok busy-after-download is not ok busy)', () => {
    const r = parseRun(green.replace('ok busy\r\n', ''), 0);
    eq(r.ok, false);
    eq(r.okCount, 26);
  });
  check('run red: the placeholder panic (no stdout)', () => eq(parseRun('', 101).ok, false));

  // The plant parser (PD-38).
  const keyLine = 'E_HARNESS_KEY: the harness key is the committed release key';
  check('plant committed-key green', () => eq(parsePlant('committed-key', `${keyLine}\n`, 1).ok, true));
  check('plant bind-any green', () => eq(parsePlant('bind-any', 'E_HARNESS_BIND: 0.0.0.0:0 is not loopback\r\n', 1).ok, true));
  check('plant bind-any: the code alone', () => eq(parsePlant('bind-any', 'E_HARNESS_BIND\n', 1).ok, true));
  check('plant red: exit 0', () => eq(parsePlant('committed-key', `${keyLine}\n`, 0).ok, false));
  check('plant red: an ok line beside it', () => eq(parsePlant('committed-key', `${keyLine}\nok good\n`, 1).ok, false));
  check('plant red: the other plant\'s code', () => eq(parsePlant('bind-any', `${keyLine}\n`, 1).ok, false));
  check('plant red: a longer code is not the code', () => eq(parsePlant('committed-key', 'E_HARNESS_KEYS x\n', 1).ok, false));
  check('plant red: the parent\'s not ok line', () => eq(parsePlant('committed-key',
    'not ok plant committed-key: ok good\n', 1).ok, false));
  check('plant red: no output', () => eq(parsePlant('bind-any', '', 1).ok, false));
  check('plant red: unknown plant name', () => eq(parsePlant('other', 'E_HARNESS_KEY\n', 1).ok, false));

  // The WSL command (plan §7.4 step 3).
  check('wslCommand run', () => eq(wslCommand('/mnt/f/projects/iterum/apps/krate-manager', []),
    'cd /mnt/f/projects/iterum/apps/krate-manager && export CARGO_TARGET_DIR=$HOME/km-target-harness' +
    ` && timeout --kill-after=30 ${40 * 60} cargo run --manifest-path tests/fixtures/updater-refusals/Cargo.toml --locked`));
  check('wslCommand plant', () => eq(wslCommand('/mnt/f/x', ['--plant', 'bind-any']).endsWith(
    'cargo run --manifest-path tests/fixtures/updater-refusals/Cargo.toml --locked -- --plant bind-any'), true));
  check('wslCommand: its own target dir, not L-WSL\'s, not /mnt', () => {
    const c = wslCommand('/mnt/f/x', []);
    eq(c.includes('CARGO_TARGET_DIR=$HOME/km-target-harness '), true);
    eq(/CARGO_TARGET_DIR=\/mnt/.test(c), false);
  });
  check('wslCommand holds no thread-count flag', () => eq(wslCommand('/mnt/f/x', []).includes(['--test', 'threads'].join('-')),
    false));
  check('ceiling 40 min', () => eq(WSL_CEILING_S, 2400));
  check('default log dir under km11-harness', () => eq(DEFAULT_LOG_DIR.startsWith('F:/tmp/gate/km11-harness/'), true));

  // The verdict for each combination.
  const row = (id, ok, text) => ({ id, ok, text });
  const greenRows = [row('H1', true, 'fmt'), row('H2', true, 'clippy'), row('H3', true, 'run'), row('H4', true, 'plant'),
    row('H5', true, 'plant')];
  const run = { okCount: 27, wallMs: 2345 };
  const loads = parseD9(loadsLine);
  const failure = parseD9(failLine);
  const missing = parseD9(null);
  check('LOADS + all green -> GREEN Windows', () => eq(finalVerdict({ d9: loads, rows: greenRows, place: 'Windows', run }),
    'HARNESS LANE GREEN (Windows, 27 ok, 2345 ms)'));
  check('missing d9 + Windows green -> GREEN Windows', () => eq(finalVerdict({ d9: missing, rows: greenRows,
    place: 'Windows', run }), 'HARNESS LANE GREEN (Windows, 27 ok, 2345 ms)'));
  check('LOAD FAILURE + WSL green -> GREEN WSL with the Windows gap', () => eq(finalVerdict({ d9: failure, rows: greenRows,
    place: 'WSL', run }), `HARNESS LANE GREEN (WSL, 27 ok, 2345 ms; Windows gap: ${failLine})`));
  check('LOADS + a red run -> RED naming it', () => eq(finalVerdict({ d9: loads, place: 'Windows', run,
    rows: [...greenRows.slice(0, 2), row('H3', false, 'run: not ok stall'), ...greenRows.slice(3)] }),
  'HARNESS LANE RED: H3 run: not ok stall'));
  check('LOAD FAILURE + no webkit in WSL -> RED WSL', () => eq(finalVerdict({ d9: failure, place: 'WSL', run: null,
    rows: [...greenRows.slice(0, 2), row('H0', false, 'WSL no webkit2gtk-4.1 (R-17\'s packages)')] }),
  'HARNESS LANE RED: H0 WSL no webkit2gtk-4.1 (R-17\'s packages)'));
  check('missing d9 + Windows loader failure -> RED d9 not recorded', () => eq(finalVerdict({ d9: missing,
    place: 'Windows', run: null, rows: [...greenRows.slice(0, 2), row('H3', false, 'd9 not recorded (exit=3221225785)')] }),
  'HARNESS LANE RED: H3 d9 not recorded (exit=3221225785)'));
  check('malformed d9 -> RED even with green rows', () => eq(finalVerdict({ d9: parseD9('x\n'), place: 'Windows', run,
    rows: greenRows }), 'HARNESS LANE RED: d9 malformed: "x"'));
  check('two red rows -> both named', () => eq(finalVerdict({ d9: loads, place: 'Windows', run,
    rows: [row('H1', false, 'fmt exit=1'), row('H2', false, 'clippy warning-lines=2'), ...greenRows.slice(2)] }),
  'HARNESS LANE RED: H1 fmt exit=1; H2 clippy warning-lines=2'));
  check('LOAD FAILURE but the run ran on Windows -> RED (never a Windows pass)', () => eq(finalVerdict({ d9: failure,
    rows: greenRows, place: 'Windows', run }).startsWith('HARNESS LANE RED: '), true));
  check('no rows -> RED', () => eq(finalVerdict({ d9: loads, rows: [], place: 'Windows', run }), 'HARNESS LANE RED: no rows'));

  for (const f of failures) console.log(`self-test FAIL ${f}`);
  console.log(failures.length ? `HARNESS LANE SELF-TEST RED (${failures.length} of ${cases} failed)`
    : `HARNESS LANE SELF-TEST GREEN (${cases} cases)`);
  return failures.length ? 1 : 0;
}

// ---- processes --------------------------------------------------------------------------------------------------
let LOG_DIR = DEFAULT_LOG_DIR;

/**
 * Runs one command under a wall-clock ceiling, streams stdout+stderr to <LOG_DIR>/<log>, resolves
 * { code, timedOut, wallS, stdout, stderr, log }.
 */
function run(cmd, args, { log, ceilingMs, env, cwd = APP }) {
  return new Promise((resolve) => {
    const logPath = `${LOG_DIR}/${log}`;
    const sink = fs.createWriteStream(logPath);
    const t0 = Date.now();
    const out = [];
    const err = [];
    let timedOut = false;
    let child;
    let timer;
    const finish = (code) => {
      clearTimeout(timer);
      sink.end(() => resolve({ code, timedOut, wallS: ((Date.now() - t0) / 1000).toFixed(1), stdout: out.join(''),
        stderr: err.join(''), log: logPath }));
    };
    sink.write(`$ (cwd ${path.relative(ROOT, cwd) || '.'}) ${cmd} ${args.join(' ')}\n`);
    try {
      child = childProcess.spawn(cmd, args, { cwd, env: env || process.env, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    } catch (e) {
      err.push(String(e.message));
      return finish(-1);
    }
    timer = setTimeout(() => { timedOut = true; killTree(child); }, ceilingMs);
    child.stdout.on('data', (d) => { out.push(d.toString()); sink.write(d); });
    child.stderr.on('data', (d) => { err.push(d.toString()); sink.write(d); });
    child.on('error', (e) => { err.push(`spawn error: ${e.message}\n`); });
    child.on('close', (code) => finish(code === null ? -1 : code));
    return undefined;
  });
}

const why = (r) => (r.timedOut ? `TIMEOUT (killed after ${r.wallS}s)` : `exit=${r.code}`);
const warningCount = (text) => text.split(/\r?\n/).filter((l) => /^warning:/.test(l.replace(/\x1b\[[0-9;]*m/g, ''))).length;

async function windowsLint(rows, env) {
  const fmt = await run('cargo', ['fmt', '--check', '--manifest-path', MANIFEST], { log: 'H1-fmt.log', ceilingMs: 5 * MIN, env });
  rows.push({ id: 'H1', ok: !fmt.timedOut && fmt.code === 0,
    text: `fmt (Windows) cargo fmt --check --manifest-path ${MANIFEST} ${why(fmt)} wall=${fmt.wallS}s` });
  const clippy = await run('cargo', ['clippy', '--manifest-path', MANIFEST, '--all-targets', '--locked', '--', '-D', 'warnings'],
    { log: 'H2-clippy.log', ceilingMs: 40 * MIN, env });
  const w = warningCount(clippy.stderr + clippy.stdout);
  rows.push({ id: 'H2', ok: !clippy.timedOut && clippy.code === 0 && w === 0,
    text: `clippy (Windows) --all-targets --locked -D warnings warning-lines=${w} ${why(clippy)} wall=${clippy.wallS}s` });
}

/** The run row and the plant rows from their results, printing the run's lines for the notes. */
function runRows(rows, place, d9, runResult, plantResults) {
  const r = runResult;
  if (place === 'Windows' && loaderFailure(r.code, r.stdout, r.stderr)) {
    const text = d9.kind === 'missing'
      ? `d9 not recorded: the Windows run is a loader failure (${why(r)}) and evidence/d9-windows.txt is absent (T014)`
      : `run (Windows) is a loader failure (${why(r)}) although d9 reads "${d9.line}"`;
    rows.push({ id: 'H3', ok: false, text });
    return null;
  }
  const p = parseRun(r.stdout, r.timedOut ? -1 : r.code);
  console.log(`run (${place}) stdout:`);
  for (const l of textLines(r.stdout)) console.log(`  ${l}`);
  rows.push({ id: 'H3', ok: !r.timedOut && p.ok, text: `run (${place}) ok=${p.okCount}/27 wall=${p.wallMs} ms` +
    `${p.reasons.length ? ` ${p.reasons.slice(0, 5).join('; ')}` : ''} ${why(r)} wall=${r.wallS}s` });
  PLANTS.forEach(([name], i) => {
    const pr = plantResults[i];
    if (!pr) {
      rows.push({ id: `H${4 + i}`, ok: false, text: `plant ${name} (${place}) not run` });
      return;
    }
    const pp = parsePlant(name, pr.stdout, pr.timedOut ? -1 : pr.code);
    rows.push({ id: `H${4 + i}`, ok: !pr.timedOut && pp.ok, text: `plant ${name} (${place}) stdout=` +
      `${JSON.stringify(textLines(pr.stdout).join(' | '))}${pp.ok ? '' : ` ${pp.reason}`} ${why(pr)} wall=${pr.wallS}s` });
  });
  return p;
}

async function windowsRun(rows, d9, env) {
  const r = await run('cargo', ['run', '--manifest-path', MANIFEST, '--locked'], { log: 'H3-run.log', ceilingMs: 40 * MIN, env });
  const plants = [];
  if (!loaderFailure(r.code, r.stdout, r.stderr)) {
    for (const [name] of PLANTS) {
      plants.push(await run('cargo', ['run', '--manifest-path', MANIFEST, '--locked', '--', '--plant', name],
        { log: `H-plant-${name}.log`, ceilingMs: 10 * MIN, env }));
    }
  }
  return runRows(rows, 'Windows', d9, r, plants);
}

/** WSL's missing prerequisites, one phrase each ('' when it can build: cargo, cc, pkg-config, webkit2gtk-4.1). */
async function wslPreflight() {
  // No double quote in a wsl.exe argument (Node would escape it as \" on the Windows command line).
  const script = 'for c in cargo cc pkg-config; do command -v $c >/dev/null 2>&1 || echo no $c; done;' +
    ' pkg-config --exists webkit2gtk-4.1 2>/dev/null || echo no webkit2gtk-4.1';
  const r = await run('wsl', ['-e', 'bash', '-lc', script], { log: 'H0-wsl-preflight.log', ceilingMs: 3 * MIN, cwd: ROOT });
  if (r.timedOut || r.code !== 0) return `unavailable (${why(r)} ${textLines(r.stderr).slice(-1)[0] || ''})`.trim();
  const missing = textLines(r.stdout);
  // R-17 (plan §10): the CI Linux leg's package line, manager-ci.yml:53-54 (libwebkit2gtk-4.1-dev and the rest).
  return missing.length ? `${missing.join(', ')} (R-17: the packages of manager-ci.yml:53-54)` : '';
}

async function wslRun(rows, d9) {
  const missing = await wslPreflight();
  if (missing) {
    rows.push({ id: 'H0', ok: false, text: `WSL ${missing}` });
    return null;
  }
  const wslApp = `${toWslPath(ROOT)}/apps/krate-manager`;
  const wsl = (extra, log, ceilingMs) => run('wsl', ['-e', 'bash', '-lc', wslCommand(wslApp, extra)],
    { log, ceilingMs, cwd: ROOT });
  const hostCeiling = (WSL_CEILING_S + 60) * 1000;
  const r = await wsl([], 'H3-run-wsl.log', hostCeiling);
  if (r.timedOut || r.code === 124 || r.code === 137) {
    rows.push({ id: 'H3', ok: false, text: `WSL ceiling (${WSL_CEILING_S} s reached, ${why(r)})` });
    return null;
  }
  if (/webkit2gtk-4\.1/.test(r.stderr) && r.code !== 0) {
    rows.push({ id: 'H3', ok: false, text: `WSL cannot build: webkit2gtk-4.1 not found (${why(r)}; R-17)` });
    return null;
  }
  const plants = [];
  for (const [name] of PLANTS) plants.push(await wsl(['--plant', name], `H-plant-${name}-wsl.log`, hostCeiling));
  return runRows(rows, 'WSL', d9, r, plants);
}

function argValue(flag) {
  const i = process.argv.indexOf(flag);
  return i >= 0 ? process.argv[i + 1] : undefined;
}

async function main() {
  LOG_DIR = argValue('--log-dir') || DEFAULT_LOG_DIR;
  fs.mkdirSync(LOG_DIR, { recursive: true });
  const t0 = Date.now();
  console.log(`harness lane: logs ${LOG_DIR}`);
  const d9 = parseD9(fs.existsSync(D9_FILE) ? fs.readFileSync(D9_FILE, 'utf8') : null);
  console.log(`d9: ${d9.kind}${d9.line ? ` "${d9.line}"` : ''} (${path.relative(ROOT, D9_FILE).replace(/\\/g, '/')})`);
  const rows = [];
  let place = 'Windows';
  let parsed = null;
  // CARGO_TARGET_DIR="$PWD/target" from apps/krate-manager/, as CI (plan §4.11).
  const env = { ...process.env, CARGO_TARGET_DIR: path.join(APP, 'target') };
  if (d9.kind === 'malformed') {
    // finalVerdict names it; nothing runs on a measurement nobody can read.
  } else if (d9.kind === 'failure') {
    place = 'WSL';
    const lint = windowsLint(rows, env);
    const wsl = wslRun(rows, d9).then((p) => { parsed = p; });
    await Promise.all([lint, wsl]);
  } else {
    await windowsLint(rows, env);
    parsed = await windowsRun(rows, d9, env);
  }
  rows.sort((a, b) => a.id.localeCompare(b.id));
  for (const r of rows) console.log(`${r.id.padEnd(3)} ${r.ok ? 'GREEN' : 'RED  '} ${r.text}`);
  console.log(`harness lane wall=${((Date.now() - t0) / 1000).toFixed(1)}s`);
  const line = finalVerdict({ d9, rows, place, run: parsed });
  console.log(line);
  process.exitCode = line.startsWith('HARNESS LANE GREEN') ? 0 : 1;
}

module.exports = { HARNESS_NAMES, PLANTS, parseD9, parseRun, parsePlant, loaderFailure, wslCommand, finalVerdict };
if (require.main === module) {
  if (process.argv.includes('--self-test')) process.exit(selfTest());
  main();
}
