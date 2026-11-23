#!/usr/bin/env node
// ==============================================================================
// guard-wsl-shutdown.js  —  PreToolUse hook (Bash, PowerShell)
// ==============================================================================
// Blocks `wsl --shutdown` and `wsl --terminate` / `wsl -t`. The user runs WSL in
// other sessions at the same time; shutting the VM down kills their work
// (user, 2026-11-18). Using WSL (wsl -e ..., macos-fastmath-repro.sh,
// check-portability.js) is fine; stopping or restarting it is not.
//
// Reads the PreToolUse JSON envelope on stdin; exit 2 blocks. Fail-open on any
// parse problem so the hook can never wedge the session.
//   node tools/hooks/guard-wsl-shutdown.js --self-test
// ==============================================================================
'use strict';

const PATTERN = /\bwsl(?:\.exe)?["']?\s+(?:[^|;&\n]*\s)?(?:--shutdown|--terminate|-t)(?:\s|$|["'])/i;

function isBlocked(cmd) {
    return typeof cmd === 'string' && PATTERN.test(cmd);
}

if (process.argv.includes('--self-test')) {
    const cases = [
        ['wsl --shutdown', true],
        ['wsl.exe --shutdown', true],
        ['"C:/Windows/System32/wsl.exe" --shutdown', true],
        ['wsl --terminate Ubuntu', true],
        ['wsl -t Ubuntu', true],
        ['foo && wsl --shutdown', true],
        ['wsl -d Ubuntu --shutdown', true],
        ['wsl -e bash -lc "make -j4"', false],
        ['wsl bash tools/macos-fastmath-repro.sh --ieee x.cpp', false],
        ['node tools/check-portability.js', false],
        ['grep -n "wsl" file.txt', false],
    ];
    let bad = 0;
    for (const [cmd, want] of cases) {
        if (isBlocked(cmd) !== want) { console.error(`FAIL: ${cmd} -> ${!want}`); ++bad; }
    }
    console.log(bad ? `${bad} failure(s)` : `all ${cases.length} cases pass`);
    process.exit(bad ? 1 : 0);
}

let input = '';
process.stdin.on('data', (c) => { input += c; });
process.stdin.on('end', () => {
    try {
        const cmd = JSON.parse(input)?.tool_input?.command;
        if (isBlocked(cmd)) {
            process.stderr.write(
                'BLOCKED: never shut down or restart WSL. The user runs WSL in other sessions at the same time ' +
                '(user, 2026-11-18). Leave WSL running; using it (wsl -e ..., repro scripts) is fine.\n');
            process.exit(2);
        }
    } catch { /* fail open */ }
    process.exit(0);
});
