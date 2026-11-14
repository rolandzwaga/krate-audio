// test-crash-dumps.js: minidump capture for test processes that crash (Windows dev box only).
//
// WHY: a rare SIGSEGV (0xC0000005) in a disrumpo_tests gate shard (2026-11-11) left nothing to read but the exit
// code and was never reproduced in 192k tries. Every test process the gate runners start now runs under
// tools/test-crash-dumps.cs, a minimal debugger: exceptions go to the test's own handlers untouched, and only an
// exception nothing handled writes <exe>.<pid>.dmp (stacks, modules, the exception context) under DUMP_DIR.
// The shard's log then holds "KRATE CRASH DUMP <path> code=0x...", which the runners print with the red line.
// Read a dump: node tools/analyze-crashdump.js <dmp> (faulting module + offset, stack scan), then
// node tools/resolve_pdb_addrs.js <pdb> <exe> <offset> for file:line (a Release test exe has no PDB: relink one
// with /DEBUG, or reproduce in the ASan Debug build).
//
// No admin rights and no system or registry change: WER LocalDumps reads per-application keys from HKLM only (an
// HKCU key was measured to be ignored). Off on Linux / macOS, in CI (CI runs tools/ci-test-shard.js anyway) and
// when KRATE_CRASH_DUMPS=0. If the wrapper cannot be compiled the runners run the test exe directly, as before.
// Dumps live under f:\tmp, which tools/clean-test-output.js clears with the rest of the test output.
'use strict';
const { spawnSync } = require('child_process');
const crypto = require('crypto');
const fs = require('fs');
const os = require('os');
const path = require('path');

const DUMP_DIR = 'f:/tmp/crashdumps';
const SOURCE = path.join(__dirname, 'test-crash-dumps.cs');
const DUMP_LINE = /^KRATE CRASH DUMP (.+?) code=(0x[0-9A-Fa-f]{8})\s*$/gm;

let wrapperExe;  // undefined: not tried yet; null: unavailable

function enabled() {
    return process.platform === 'win32' && !process.env.CI && process.env.KRATE_CRASH_DUMPS !== '0';
}

// The compiled wrapper, built once per source version and cached under the OS temp dir.
function wrapper() {
    if (wrapperExe !== undefined) return wrapperExe;
    wrapperExe = null;
    if (!enabled()) return null;
    const source = fs.readFileSync(SOURCE);
    const hash = crypto.createHash('sha256').update(source).digest('hex').slice(0, 12);
    const dir = path.join(os.tmpdir(), 'krate-test-crash-dumps');
    const exe = path.join(dir, `test-crash-dumps-${hash}.exe`);
    if (fs.existsSync(exe)) return (wrapperExe = exe);
    const csc = path.join(process.env.WINDIR || 'C:\\Windows', 'Microsoft.NET', 'Framework64', 'v4.0.30319', 'csc.exe');
    if (!fs.existsSync(csc)) {
        console.log(`crash dumps OFF: ${csc} not found`);
        return null;
    }
    fs.mkdirSync(dir, { recursive: true });
    const tmp = `${exe}.${process.pid}.tmp.exe`;  // concurrent gates: compile apart, then rename
    const r = spawnSync(csc, ['/nologo', '/optimize+', '/platform:x64', '/target:exe', `/out:${tmp}`, SOURCE], { encoding: 'utf8' });
    if (r.status !== 0 || !fs.existsSync(tmp)) {
        console.log(`crash dumps OFF: compiling ${SOURCE} failed:\n${r.stdout || ''}${r.stderr || ''}`);
        return null;
    }
    try { fs.renameSync(tmp, exe); } catch { fs.rmSync(tmp, { force: true }); }
    return fs.existsSync(exe) ? (wrapperExe = exe) : null;
}

/** spawn() arguments that run `exe argv` with crash-dump capture when it is available, else as given. */
function wrap(exe, argv, env = process.env) {
    const w = wrapper();
    if (!w) return { cmd: exe, args: argv, env };
    // A process created under a debugger gets the debug heap (much slower) unless this is set.
    return { cmd: w, args: [DUMP_DIR, exe, ...argv], env: { ...env, _NO_DEBUG_HEAP: '1' } };
}

/** The dumps a test process's output reports: [{ path, code }]. */
function dumpsIn(text) {
    return [...String(text).matchAll(DUMP_LINE)].map((m) => ({ path: m[1], code: m[2] }));
}

module.exports = { DUMP_DIR, enabled, wrapper, wrap, dumpsIn };
