#!/usr/bin/env node
// =============================================================================
// lint-test-scope.js
// =============================================================================
// Test runs cover only what the change reaches (tools/run-gate.js / run-cpu-tests.js --scope affected, the
// default). The agent instructions (workflow prompts, agent definitions, skills, CLAUDE.md files) are where agents
// copy their commands from, so a whole-suite command there (run-gate / run-cpu-tests --scope all,
// run-close-lanes.js, an unselected ctest) becomes a whole-suite run in every task that copies it. This lint flags
// such a line unless it carries "gate-all-ok: <reason>". Runtime twin: tools/hooks/guard-test-scope.js (same
// matcher).
//
// Scanned: .claude/workflows, .claude/agents, .claude/skills, every CLAUDE.md.
//
// Usage:
//   node tools/lint-test-scope.js            # scan the tree; exit 1 on a hit
//   node tools/lint-test-scope.js --self-test
// =============================================================================
'use strict';
const fs = require('fs');
const path = require('path');
const { violation } = require('./hooks/guard-test-scope.js');

const ROOT = path.resolve(__dirname, '..');
const SCAN_DIRS = ['.claude/workflows', '.claude/agents', '.claude/skills'];
const SKIP_DIR = /^(build|_deps|extern|node_modules|\.git)$/;

function collect() {
    const out = [];
    const walk = (dir, filter) => {
        let entries;
        try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
        for (const e of entries) {
            const p = path.join(dir, e.name);
            if (e.isDirectory()) { if (!SKIP_DIR.test(e.name)) walk(p, filter); }
            else if (filter(e.name)) out.push(p);
        }
    };
    for (const d of SCAN_DIRS) walk(path.join(ROOT, d), (n) => /\.(js|md|mjs|json|ya?ml|txt)$/.test(n));
    out.push(path.join(ROOT, 'CLAUDE.md'));
    for (const d of ['dsp', 'plugins', 'tools']) walk(path.join(ROOT, d), (n) => n === 'CLAUDE.md');
    return out.filter((p) => fs.existsSync(p));
}

const scanText = (text) => text.split(/\r?\n/).map((l, i) => ({ line: i + 1, what: violation(l) })).filter((h) => h.what);

function selfTest() {
    const bad = [
        "  node tools/run-gate.js --share 4 --scope all --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' ruinae_tests",
        'ctest --test-dir build/windows-x64-release -C Release -j 30 --output-on-failure',
        'then run node tools/run-close-lanes.js --out ${OUT}',
    ];
    const good = [
        "  node tools/run-gate.js --share 4 --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' ruinae_tests",
        "  node tools/run-gate.js --share 4 --scope all --filter 'x' ruinae_tests   # gate-all-ok: the audit measures the whole suite",
        'any new lane runner cuts each executable into Catch2 shards (`tools/run-close-lanes.js`',
        'Do NOT use `ctest -R dsp_core_tests`',
    ];
    let failed = 0;
    for (const l of bad) if (!scanText(l).length) { failed++; console.log(`FAIL (should flag): ${l}`); }
    for (const l of good) if (scanText(l).length) { failed++; console.log(`FAIL (should pass): ${l}`); }
    console.log(failed ? `lint-test-scope self-test: ${failed} FAILED` : `lint-test-scope self-test: ${bad.length + good.length} cases OK`);
    process.exit(failed ? 1 : 0);
}

if (process.argv.includes('--self-test')) selfTest();

let total = 0;
for (const file of collect()) {
    for (const h of scanText(fs.readFileSync(file, 'utf8'))) {
        total++;
        console.log(`${path.relative(ROOT, file).replace(/\\/g, '/')}:${h.line}: ${h.what} without "gate-all-ok: <reason>"`);
    }
}
if (total) {
    console.log(`\nlint-test-scope: ${total} hit(s). Run only what the change reaches (run-gate.js / run-cpu-tests.js default,`);
    console.log('--base <ref> for committed work), or give the whole-suite run its reason: "# gate-all-ok: <reason>" on the same line.');
    process.exit(1);
}
console.log('lint-test-scope: OK');
