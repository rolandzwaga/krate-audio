#!/usr/bin/env node
// ==============================================================================
// guard-portability.js  —  PreToolUse hook (Bash)
// ==============================================================================
// Blocks `git commit` when a staged C++ translation unit fails to compile under
// GCC. A local Windows build cannot find this class of error at all: MSVC
// accepts constructs that GCC and Clang correctly reject, so a fully green
// Windows build still breaks the Linux and macOS CI legs ~7 minutes in.
//
// The instance that prompted this:
//     constexpr Steinberg::FIDString kPlatformType = Steinberg::kPlatformTypeHWND;
// The SDK declares those as plain `const FIDString`, not usable in a constant
// expression. MSVC compiled it; GCC and Clang both refused.
//
// Delegates to tools/check-portability.js --staged, which syntax-checks only the
// staged files (typically a handful, a second or two each). Skips silently when
// WSL/g++ is unavailable, and fails open on any spawn problem, so it can never
// wedge a session or block a machine that cannot run the check.
//
// Reads the PreToolUse JSON envelope on stdin; exit 2 blocks.
// ==============================================================================
'use strict';

const { execFileSync, spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

function readStdin() {
    return new Promise((resolve) => {
        let data = '';
        process.stdin.setEncoding('utf8');
        process.stdin.on('data', (c) => (data += c));
        process.stdin.on('end', () => resolve(data));
        if (process.stdin.isTTY) resolve('');
    });
}

function repoRoot() {
    return path.resolve(__dirname, '..', '..');
}

function isGitCommit(cmd) {
    if (!/\bgit\b[^\n]*\bcommit\b/.test(cmd)) return false;
    if (/\bgit\s+(log|show|diff|rev-parse|rev-list|status|cherry)\b/.test(cmd)) return false;
    return true;
}

(async () => {
    let payload;
    try {
        payload = JSON.parse(await readStdin());
    } catch {
        process.exit(0);
    }

    const cmd = (payload && payload.tool_input && payload.tool_input.command) || '';
    if (!cmd || !isGitCommit(cmd)) process.exit(0);

    const root = repoRoot();
    const checker = path.join(root, 'tools', 'check-portability.js');
    if (!fs.existsSync(checker)) process.exit(0);

    // Nothing to do when no C++ is staged -- avoids paying for doc/config commits.
    let staged = '';
    try {
        staged = execFileSync('git', ['diff', '--cached', '--name-only', '--diff-filter=ACMR'], {
            cwd: root,
            encoding: 'utf8',
        });
    } catch {
        process.exit(0);
    }
    const hasCpp = staged.split('\n').some((f) => /\.(cpp|cc)$/.test(f) && !f.startsWith('extern/'));
    if (!hasCpp) process.exit(0);

    const r = spawnSync(process.execPath, [checker, '--staged'], {
        cwd: root,
        encoding: 'utf8',
        timeout: 600000,
        maxBuffer: 32 * 1024 * 1024,
    });

    // Spawn failure / timeout is a hook problem, not a code problem: fail open.
    if (r.error || typeof r.status !== 'number') process.exit(0);
    if (r.status === 0) process.exit(0);

    process.stderr.write(
        'BLOCKED: staged C++ does not compile under GCC. MSVC accepts things GCC and\n' +
        'Clang reject, so a green Windows build does not mean the Linux and macOS CI\n' +
        'legs will pass.\n\n' +
        (r.stdout || '') + (r.stderr || '') + '\n' +
        'Run: node tools/check-portability.js --staged\n'
    );
    process.exit(2);
})();
