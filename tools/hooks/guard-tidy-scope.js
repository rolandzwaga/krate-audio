#!/usr/bin/env node
// ==============================================================================
// guard-tidy-scope.js  —  PreToolUse hook (Bash, PowerShell)
// ==============================================================================
// Blocks a whole-target clang-tidy sweep (run-clang-tidy.ps1 -Full / run-clang-tidy.sh --full) unless the
// command says why, with a `# tidy-full-ok: <reason>` comment (valid in bash and PowerShell).
//
// The runners analyze only what changed by default (tools/tidy-changed-files.js). A full sweep is right for an
// audit or after a clang-tidy upgrade, and wrong for a task gate: a task that changed one DSP header and its
// test swept all 482 dsp TUs, ~40 min, to re-check 2 files (Ruinae audit T6.10, 2026-11-23). Workflow text is
// linted for the same thing in CI (tools/lint-tidy-scope.js); this hook catches an agent typing it.
//
// Reads the PreToolUse JSON envelope on stdin; exit 2 blocks. Fail-open on any parse problem.
//   node tools/hooks/guard-tidy-scope.js --self-test
// ==============================================================================
'use strict';

const TIDY = /run-clang-tidy\.(ps1|sh)\b/;
const FULL = /(^|\s)(-Full|--full)(?=\s|$|[;)`'"|&>])/i;
const REASON = /tidy-full-ok:\s*\S/;

function blocks(cmd) {
    if (typeof cmd !== 'string') return false;
    return cmd.split(/\r?\n/).some((line) => TIDY.test(line) && FULL.test(line) && !REASON.test(line));
}

const MESSAGE = `BLOCKED by tools/hooks/guard-tidy-scope.js: a whole-target clang-tidy sweep (-Full / --full) without a reason.
The runners analyze only what changed by default (the changed .cpp files plus one includer per changed header,
tools/tidy-changed-files.js), which is what a task, gate or commit needs. Drop -Full / --full.
Use -Base main / --base main to cover everything the branch changed.
A full sweep (an audit, a clang-tidy upgrade) needs the reason on the same line:
  ./tools/run-clang-tidy.ps1 -Target dsp -Full -Jobs 4   # tidy-full-ok: ruinae audit measurement lane`;

function selfTest() {
    const block = [
        './tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja -Jobs 4 -Full',
        './tools/run-clang-tidy.ps1 -Full -Target ruinae *> f:/tmp/t.log',
        './tools/run-clang-tidy.sh --target dsp --full',
        'cd /f/projects/iterum\n./tools/run-clang-tidy.sh --full --target all > log 2>&1',
        './tools/run-clang-tidy.ps1 -full -Target ruinae',
        '`./tools/run-clang-tidy.sh --target dsp --full`',
        './tools/run-clang-tidy.sh --full>log',
    ];
    const allow = [
        './tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja -Jobs 4',
        './tools/run-clang-tidy.ps1 -Target dsp -Base main -Jobs 4',
        './tools/run-clang-tidy.sh --target dsp --base main',
        './tools/run-clang-tidy.ps1 -Target dsp -Full -Jobs 4   # tidy-full-ok: audit measurement lane',
        './tools/run-clang-tidy.sh --target all --full # tidy-full-ok: LLVM 21 upgrade check',
        'grep -n "Full" tools/run-clang-tidy.ps1',
        'node tools/lint-tidy-scope.js --full-report',
        'cmake --build build --target Full',
    ];
    let failed = 0;
    for (const c of block) if (!blocks(c)) { failed++; console.log(`FAIL (should block): ${JSON.stringify(c)}`); }
    for (const c of allow) if (blocks(c)) { failed++; console.log(`FAIL (should allow): ${JSON.stringify(c)}`); }
    console.log(failed ? `guard-tidy-scope self-test: ${failed} FAILED` : `guard-tidy-scope self-test: ${block.length + allow.length} cases OK`);
    process.exit(failed ? 1 : 0);
}

if (require.main === module) {
    if (process.argv.includes('--self-test')) selfTest();
    let data = '';
    process.stdin.setEncoding('utf8');
    process.stdin.on('data', (c) => (data += c));
    process.stdin.on('end', () => {
        try {
            const env = JSON.parse(data || '{}');
            if (blocks(env.tool_input && env.tool_input.command)) {
                process.stderr.write(MESSAGE + '\n');
                process.exit(2);
            }
        } catch {
            // fail open
        }
        process.exit(0);
    });
    if (process.stdin.isTTY) process.exit(0);
}

module.exports = { blocks };
