#!/usr/bin/env node
// =============================================================================
// lint-tidy-scope.js
// =============================================================================
// clang-tidy runs on what changed, never on a whole target by habit.
//
// tools/run-clang-tidy.ps1 / .sh analyze only the changed files by default
// (tools/tidy-changed-files.js); -Full / --full sweeps the whole target. The
// agent instructions (workflow prompts, agent definitions, skills, CLAUDE.md
// files) are where agents copy their commands from, so a -Full there becomes a
// 40-minute sweep in every task that copies it (Ruinae audit T6.10,
// 2026-11-23: one changed header, all 482 dsp TUs). This lint flags a line that
// runs a tidy script with -Full / --full and no `tidy-full-ok: <reason>` on the
// same line. The runtime twin is tools/hooks/guard-tidy-scope.js (same matcher).
//
// Scanned: .claude/workflows, .claude/agents, .claude/skills, every CLAUDE.md.
//
// Usage:
//   node tools/lint-tidy-scope.js            # scan the tree; exit 1 on a hit
//   node tools/lint-tidy-scope.js --self-test
// =============================================================================
'use strict';
const fs = require('fs');
const path = require('path');
const { blocks } = require('./hooks/guard-tidy-scope.js');

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

function scanText(text) {
    const hits = [];
    text.split(/\r?\n/).forEach((line, i) => { if (blocks(line)) hits.push(i + 1); });
    return hits;
}

function selfTest() {
    const bad = [
        '- clang-tidy: ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja -Jobs 4 -Full',
        "`./tools/run-clang-tidy.sh --target ${p} --full`",
    ];
    const good = [
        '- clang-tidy: ./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja -Jobs 4',
        '- clang-tidy (audit lane): ./tools/run-clang-tidy.ps1 -Target dsp -Full -Jobs 4  # tidy-full-ok: audit measures every TU',
        'Run a full sweep only for audits (-Full needs a reason).',
    ];
    let failed = 0;
    for (const l of bad) if (!scanText(l).length) { failed++; console.log(`FAIL (should flag): ${l}`); }
    for (const l of good) if (scanText(l).length) { failed++; console.log(`FAIL (should pass): ${l}`); }
    console.log(failed ? `lint-tidy-scope self-test: ${failed} FAILED` : `lint-tidy-scope self-test: ${bad.length + good.length} cases OK`);
    process.exit(failed ? 1 : 0);
}

if (process.argv.includes('--self-test')) selfTest();

let total = 0;
for (const file of collect()) {
    const hits = scanText(fs.readFileSync(file, 'utf8'));
    for (const line of hits) {
        total++;
        console.log(`${path.relative(ROOT, file).replace(/\\/g, '/')}:${line}: whole-target clang-tidy (-Full / --full) without "tidy-full-ok: <reason>"`);
    }
}
if (total) {
    console.log(`\nlint-tidy-scope: ${total} hit(s). Drop -Full / --full (the runners analyze what changed by default),`);
    console.log('or, for an audit or a clang-tidy upgrade, add "# tidy-full-ok: <reason>" on the same line.');
    process.exit(1);
}
console.log('lint-tidy-scope: OK');
