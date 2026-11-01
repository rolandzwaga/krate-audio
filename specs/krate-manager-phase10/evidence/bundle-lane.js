#!/usr/bin/env node
// L-BUNDLE for Krate Manager Phase 10 (tasks.md T017, TA-16; plan.md §7.4 step 5, PD-20, §8.2 `helper bundled` /
// `ci-temp-roots absent`). Run by the MAIN LOOP at close (close-lane.js runs it after its cargo chain, because it holds
// the cargo build lock), never by an implementer or a gate.
//
// WHY: `npm run bundle` (scripts/bundle.js, PD-20: the helper's release build, the copy to
// src-tauri/binaries/krate-manager-helper-<host tuple>.exe, then `tauri build --config src-tauri/tauri.helper.conf.json`)
// takes minutes and writes the git-ignored src-tauri/binaries/, so it is a main-loop lane, never a gate (plan.md:1095-1096).
// After it, TA-16's local checks read what this box can read (tasks.md:261; `7z` is absent here, so the `7z l` / `7z e`
// half of `helper bundled` is CI-only): (1) the run exited 0; (2) the `build warnings` rule of
// .github/workflows/manager-ci.yml:185-198 over the run's log (ANSI stripped, at least one "Finished `release` profile"
// line, 0 lines starting `warning:`; the log holds the helper's release build too, PD-20); (3) the staged sidecar
// src-tauri/binaries/<sidecar name> exists and holds `--ci-roots` 0 times (FR-103: the feature is never in a release
// build); (4) so does the release helper target/release/krate-manager-helper.exe; (5) an NSIS setup matching the
// `target/release/bundle/nsis/*-setup.exe` glob (plan.md:1119) exists. It never runs the produced setup or the helper
// (tasks.md rule 1), and it reads the binaries as bytes only.
//
// Usage (repo root, Windows dev box; Node only, no dependencies):
//   node specs/krate-manager-phase10/evidence/bundle-lane.js               # log under F:/tmp/gate/km10-bundle/
//   node specs/krate-manager-phase10/evidence/bundle-lane.js --out <file>  # the `npm run bundle` log to <file>
//   node specs/krate-manager-phase10/evidence/bundle-lane.js --self-test   # pure checks (the --ci-roots counter over a
//                                                                          # byte buffer, the sidecar name, the NSIS
//                                                                          # setup glob, the build-warnings rule)
// Rows print as `<id> GREEN|RED <text>`; last line `BUNDLE LANE GREEN` exit 0 | `BUNDLE LANE RED: <rows>` exit 1.
// It writes nothing but its log and stdout.
'use strict';
const childProcess = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const APP = path.join(ROOT, 'apps', 'krate-manager');
const CEILING_MS = 40 * 60 * 1000;
const STAMP = new Date().toISOString().replace(/[-:]/g, '').replace(/\..*/, '').replace('T', '-');
const DEFAULT_LOG = `F:/tmp/gate/km10-bundle/bundle-${STAMP}.txt`;
const NEEDLE = '--ci-roots'; // FR-103: absent from every release helper
const HELPER = 'krate-manager-helper';
const NSIS_DIR = 'target/release/bundle/nsis';

/** plan §4.6 `sidecarName(hostTuple)`: `krate-manager-helper-<tuple>`, plus `.exe` iff the tuple contains `-windows-`. */
function sidecarName(hostTuple) {
  const tuple = hostTuple.trim();
  return `${HELPER}-${tuple}${tuple.includes('-windows-') ? '.exe' : ''}`;
}

/** How many times `needle` occurs in the byte buffer `buf` (non-overlapping; `grep -acF` reads 0 exactly when this is 0). */
function countNeedle(buf, needle = NEEDLE) {
  const n = Buffer.from(needle);
  let count = 0;
  for (let at = buf.indexOf(n); at >= 0; at = buf.indexOf(n, at + n.length)) count++;
  return count;
}

/** The directory entries matching the `*-setup.exe` glob (plan.md:1119), in input order. */
function nsisSetups(names) {
  return names.filter((name) => /^[^/\\]*-setup\.exe$/.test(name));
}

/**
 * manager-ci.yml:185-198 `build warnings`: ANSI colour codes stripped, then GREEN when at least one line matches
 * `^ *Finished \`release\` profile` and no line starts with `warning:`.
 */
function buildWarnings(text) {
  const lines = text.replace(/\x1b\[[0-9;]*m/g, '').split(/\r?\n/);
  const finished = lines.filter((l) => /^ *Finished `release` profile/.test(l)).length;
  const warnings = lines.filter((l) => l.startsWith('warning:')).length;
  return { ok: finished >= 1 && warnings === 0, finished, warnings };
}

/** The last line of the run: GREEN, or RED naming every red row id. */
function verdictLine(rows) {
  if (rows.length === 0) return 'BUNDLE LANE RED: no rows';
  const red = rows.filter((r) => !r.ok).map((r) => r.id);
  return red.length ? `BUNDLE LANE RED: ${red.join(', ')}` : 'BUNDLE LANE GREEN';
}

function selfTest() {
  const failures = [];
  let cases = 0;
  const expect = (name, got, want) => {
    cases++;
    if (got !== want) failures.push(`${name}: got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`);
  };
  // TA-16's staged name on this box, and the other host tuples bundle.js serves.
  expect('sidecar name, windows msvc', sidecarName('x86_64-pc-windows-msvc'), 'krate-manager-helper-x86_64-pc-windows-msvc.exe');
  expect('sidecar name, windows arm64', sidecarName('aarch64-pc-windows-msvc'), 'krate-manager-helper-aarch64-pc-windows-msvc.exe');
  expect('sidecar name, apple arm64', sidecarName('aarch64-apple-darwin'), 'krate-manager-helper-aarch64-apple-darwin');
  expect('sidecar name, linux', sidecarName('x86_64-unknown-linux-gnu'), 'krate-manager-helper-x86_64-unknown-linux-gnu');
  expect('sidecar name trims the rustc newline', sidecarName('x86_64-pc-windows-msvc\r\n'),
    'krate-manager-helper-x86_64-pc-windows-msvc.exe');

  // The counter over raw bytes: a PE image is not UTF-8, so the needle is searched as bytes.
  const pe = Buffer.concat([Buffer.from([0x4d, 0x5a, 0x90, 0x00, 0xff, 0xfe]), Buffer.from('usage: --job <p>\0'),
    Buffer.from([0x00, 0xc3, 0x28])]);
  expect('needle absent in a binary buffer -> 0', countNeedle(pe), 0);
  const planted = Buffer.concat([pe, Buffer.from('--ci-roots'), Buffer.from([0xff, 0x00]), Buffer.from('x--ci-roots\0')]);
  expect('needle planted twice among non-UTF-8 bytes -> 2', countNeedle(planted), 2);
  expect('needle at the very start and end', countNeedle(Buffer.from('--ci-roots--ci-roots')), 2);
  expect('a near miss is not counted', countNeedle(Buffer.from('--ci-root --ci_roots -ci-roots')), 0);
  expect('empty buffer -> 0', countNeedle(Buffer.alloc(0)), 0);
  expect('the default needle is --ci-roots', NEEDLE, '--ci-roots');

  // The NSIS setup glob `*-setup.exe`.
  const entries = ['Krate Manager_0.1.0_x64-setup.exe', 'Krate Manager_0.1.0_x64-setup.exe.sig', 'setup.exe',
    'krate-manager.exe', 'x-setup.exe.bak', 'Krate Manager_0.1.0_x64-SETUP.exe', 'a-setup.exe'];
  expect('nsis glob picks the setups only', nsisSetups(entries).join('|'), 'Krate Manager_0.1.0_x64-setup.exe|a-setup.exe');
  expect('nsis glob over an empty dir', nsisSetups([]).length, 0);

  // The build-warnings rule (manager-ci.yml:185-198).
  const esc = '\x1b';
  const finished = '    Finished `release` profile [optimized] target(s) in 41.20s';
  const clean = ['   Compiling krate-manager-helper v0.1.0', finished, '   Compiling krate-manager v0.1.0', finished,
    '        Info Target: x64', '    Finished 1 bundle at:'].join('\r\n');
  const bw = buildWarnings(clean);
  expect('two Finished lines, no warning -> GREEN', bw.ok, true);
  expect('two Finished lines counted', bw.finished, 2);
  expect('a warning line -> RED', buildWarnings(`${clean}\nwarning: unused variable: \`x\``).ok, false);
  expect('an ANSI-coloured warning line is still a warning', buildWarnings(`${clean}\n${esc}[1m${esc}[33mwarning${esc}[0m: x`).warnings, 1);
  expect('an ANSI-coloured Finished line still counts', buildWarnings(`${esc}[1m${esc}[32m    Finished${esc}[0m \`release\` profile`).finished, 1);
  expect('no Finished line -> RED', buildWarnings('   Compiling krate-manager v0.1.0\n').ok, false);
  expect('a dev-profile Finished line does not count', buildWarnings('    Finished `dev` profile [unoptimized]').ok, false);
  expect('an indented "warning:" is not a warning line', buildWarnings(`${clean}\n  = note: warning: inside a note`).ok, true);

  expect('verdict line green', verdictLine([{ id: 'L1', ok: true }, { id: 'L5', ok: true }]), 'BUNDLE LANE GREEN');
  expect('verdict line names the red rows', verdictLine([{ id: 'L1', ok: true }, { id: 'L3', ok: false }, { id: 'L5', ok: false }]),
    'BUNDLE LANE RED: L3, L5');
  expect('verdict line with no rows is red', verdictLine([]), 'BUNDLE LANE RED: no rows');
  expect('default log under km10-bundle', DEFAULT_LOG.startsWith('F:/tmp/gate/km10-bundle/bundle-'), true);

  if (failures.length) {
    for (const x of failures) console.log(`self-test FAIL ${x}`);
    console.log(`BUNDLE LANE SELF-TEST RED (${failures.length} of ${cases} failed)`);
    return 1;
  }
  console.log(`BUNDLE LANE SELF-TEST GREEN (${cases} cases)`);
  return 0;
}

/** Kills a child and its descendants (cmd -> npm -> node -> cargo / tauri). */
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

/** `npm run bundle` from apps/krate-manager/ under the ceiling, stdout + stderr teed to `logPath`. */
function runBundle(logPath) {
  return new Promise((resolve) => {
    fs.mkdirSync(path.dirname(logPath), { recursive: true });
    const sink = fs.createWriteStream(logPath);
    sink.write('$ (cwd apps/krate-manager) npm run bundle\n');
    const chunks = [];
    const t0 = Date.now();
    let timedOut = false;
    // shell: npm is a .cmd on Windows; one fixed string, no args array (Node DEP0190).
    const child = childProcess.spawn('npm run bundle', { cwd: APP, shell: true, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    const timer = setTimeout(() => { timedOut = true; killTree(child); }, CEILING_MS);
    const take = (d) => { chunks.push(d.toString()); sink.write(d); };
    child.stdout.on('data', take);
    child.stderr.on('data', take);
    child.on('error', (e) => take(`spawn error: ${e.message}\n`));
    child.on('close', (code) => {
      clearTimeout(timer);
      sink.end(() => resolve({ code: code === null ? -1 : code, timedOut, wallS: ((Date.now() - t0) / 1000).toFixed(1),
        out: chunks.join('') }));
    });
  });
}

/** [exists, size, --ci-roots count] of a binary read as bytes (never executed). */
function scanBinary(abs) {
  if (!fs.existsSync(abs)) return { exists: false, size: 0, hits: NaN };
  const buf = fs.readFileSync(abs);
  return { exists: true, size: buf.length, hits: countNeedle(buf) };
}

async function main() {
  if (process.argv.includes('--self-test')) {
    process.exitCode = selfTest();
    return;
  }
  const logPath = path.resolve(ROOT, argValue('--out') || DEFAULT_LOG);
  const rows = [];
  const record = (id, ok, text) => {
    rows.push({ id, ok, text });
    console.log(`${id} ${ok ? 'GREEN' : 'RED  '} ${text}`);
  };

  const host = childProcess.spawnSync('rustc', ['--print', 'host-tuple'], { encoding: 'utf8', windowsHide: true, timeout: 60 * 1000 });
  const tuple = host.status === 0 ? host.stdout.trim() : '';
  record('L0', tuple !== '', `rustc --print host-tuple="${tuple || (host.error ? host.error.message : host.stderr.trim())}"` +
    ` (TA-16 expects x86_64-pc-windows-msvc)`);

  const r = await runBundle(logPath);
  record('L1', !r.timedOut && r.code === 0, `npm run bundle ${r.timedOut ? `TIMEOUT (killed after ${r.wallS}s)` : `exit=${r.code}`}` +
    ` wall=${r.wallS}s log=${logPath}`);

  const bw = buildWarnings(r.out);
  record('L2', bw.ok, `build warnings rule (manager-ci.yml:185-198): finished-lines=${bw.finished} warnings=${bw.warnings}`);

  const exe = process.platform === 'win32' ? '.exe' : '';
  const sidecarRel = `src-tauri/binaries/${tuple ? sidecarName(tuple) : `${HELPER}-<unknown tuple>`}`;
  const sidecar = scanBinary(path.join(APP, sidecarRel));
  record('L3', tuple !== '' && sidecar.exists && sidecar.hits === 0, `staged sidecar ${sidecarRel}` +
    ` ${sidecar.exists ? `${sidecar.size} bytes, "${NEEDLE}" x${sidecar.hits}` : 'MISSING'} (want present, 0)`);

  const helperRel = `target/release/${HELPER}${exe}`;
  const helper = scanBinary(path.join(APP, helperRel));
  record('L4', helper.exists && helper.hits === 0, `release helper ${helperRel}` +
    ` ${helper.exists ? `${helper.size} bytes, "${NEEDLE}" x${helper.hits}` : 'MISSING'} (want present, 0)`);

  const nsisAbs = path.join(APP, NSIS_DIR);
  const setups = fs.existsSync(nsisAbs) ? nsisSetups(fs.readdirSync(nsisAbs)) : [];
  record('L5', setups.length >= 1, `NSIS setup ${NSIS_DIR}/*-setup.exe: ${setups.length ? setups.join(', ') : 'NONE'}` +
    ' (never run, tasks.md rule 1)');

  const line = verdictLine(rows);
  console.log(line);
  process.exitCode = line === 'BUNDLE LANE GREEN' ? 0 : 1;
}

module.exports = { sidecarName, countNeedle, nsisSetups, buildWarnings, verdictLine, NEEDLE };
if (require.main === module) main();
