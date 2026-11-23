#!/usr/bin/env node
// ==============================================================================
// guard-test-scope.js  —  PreToolUse hook (Bash, PowerShell)
// ==============================================================================
// Test runs cover only what the change can reach. tools/run-gate.js and tools/run-cpu-tests.js do that by default
// (--scope affected, tools/affected-tests.js). This hook blocks the commands that run whole suites unless the line
// says why with a "# gate-all-ok: <reason>" comment (valid in bash and PowerShell):
//   - run-gate.js / run-cpu-tests.js with --scope all
//   - node tools/run-close-lanes.js (it always runs whole suites; --dry-run passes)
//   - ctest without a selection (-R / --tests-regex / -L / --label-regex / -N / --show-only)
// The CI twin for workflow text is tools/lint-test-scope.js (same matcher).
//
// WHY: a close-out gate over nine suites ran 6 h on code the change never touched (2026-11-23); the rule was in
// CLAUDE.md and memory and still was not followed. The user asked for a mechanical check.
//
// Reads the PreToolUse JSON envelope on stdin; exit 2 blocks. Fail-open on any parse problem.
//   node tools/hooks/guard-test-scope.js --self-test
// ==============================================================================
'use strict';

const REASON = /gate-all-ok:\s*\S/;
const RULES = [
    { re: /run-gate\.js\b.*--scope[\s=]+all\b/, what: 'run-gate.js --scope all' },
    { re: /run-cpu-tests\.js\b.*--scope[\s=]+all\b/, what: 'run-cpu-tests.js --scope all' },
    { re: /\bnode\s+\S*run-close-lanes\.js\b(?!.*--dry-run)/, what: 'run-close-lanes.js (whole suites)' },
    {
        re: /(^|[\s"'`/\\])ctest(?:\.exe)?["']?\s+-/,
        unless: /\s(-R|--tests-regex|-L|--label-regex|-N|--show-only)\b/,
        what: 'ctest without a test selection',
    },
];

// The first rule a line breaks, or null.
function violation(line) {
    if (typeof line !== 'string' || REASON.test(line)) return null;
    for (const r of RULES) if (r.re.test(line) && !(r.unless && r.unless.test(line))) return r.what;
    return null;
}
function blocks(cmd) {
    if (typeof cmd !== 'string') return null;
    for (const line of cmd.split(/\r?\n/)) { const v = violation(line); if (v) return v; }
    return null;
}

const MESSAGE = (what) => `BLOCKED by tools/hooks/guard-test-scope.js: ${what} runs whole test suites, and the line gives no reason.
Test runs cover only the code the change reaches: tools/run-gate.js and tools/run-cpu-tests.js do that by default
(--scope affected; tools/affected-tests.js shows the reach). Drop --scope all / use run-gate.js instead.
Measure a committed change with --base <ref> (main, the last release tag), not with a whole-suite run.
A whole-suite run that is really needed (an audit's measurement lane, a release gate the user asked for) carries
its reason on the same line:
  node tools/run-gate.js --scope all --filter '...' ruinae_tests   # gate-all-ok: ruinae audit measurement lane`;

function selfTest() {
    const block = [
        "node tools/run-gate.js --scope all --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' ruinae_tests",
        "node tools/run-gate.js --share 4 --filter 'x' --scope=all ruinae_tests > f:/tmp/g.log 2>&1",
        'node tools/run-cpu-tests.js --scope all ruinae_tests',
        'node tools/run-close-lanes.js --out f:/tmp/close',
        'ctest --test-dir build/windows-x64-release -C Release -j 30 --output-on-failure',
        '"/c/Program Files/CMake/bin/ctest.exe" --test-dir "F:/projects/iterum/build" -C Release -j 30 -V',
    ];
    const allow = [
        "node tools/run-gate.js --share 4 --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' ruinae_tests",
        "node tools/run-gate.js --base main --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' ruinae_tests",
        'node tools/run-cpu-tests.js ruinae_tests',
        "node tools/run-gate.js --scope all --filter 'x' ruinae_tests   # gate-all-ok: audit measurement lane",
        'node tools/run-close-lanes.js --dry-run',
        'ctest --test-dir build -C Release -j 30 -R "sineFold" --output-on-failure',
        'ctest --test-dir build -C Release -N',
        'Do NOT use `ctest -R dsp_core_tests` here',
        'ctest is serial by default',
        'tools/run-close-lanes.js keeps it out of the pool',
    ];
    let failed = 0;
    for (const c of block) if (!blocks(c)) { failed++; console.log(`FAIL (should block): ${c}`); }
    for (const c of allow) if (blocks(c)) { failed++; console.log(`FAIL (should allow): ${c}`); }
    console.log(failed ? `guard-test-scope self-test: ${failed} FAILED` : `guard-test-scope self-test: ${block.length + allow.length} cases OK`);
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
            const what = blocks(env.tool_input && env.tool_input.command);
            if (what) {
                process.stderr.write(MESSAGE(what) + '\n');
                process.exit(2);
            }
        } catch {
            // fail open
        }
        process.exit(0);
    });
    if (process.stdin.isTTY) process.exit(0);
}

module.exports = { blocks, violation };
