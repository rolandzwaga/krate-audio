#!/usr/bin/env node
//
// lint-wallclock-tags.js
// =============================================================================
// Every per-push TEST_CASE that asserts on its own wall clock carries the
// [wallclock] tag (or a timing tag: [performance] [perf] [benchmark] [long],
// or a hidden one: [.anything]).
//
// Why: gates shard every executable across the whole machine
// (tools/run-gate.js). A case that checks elapsed time against a bar reads
// 1.5-2x slower beside 30 busy shards and goes red on code nobody touched:
// Profundum's render guards read 17-23 s against a 15 s bar, and a Seraphis
// voice-steal per-block budget went red, in one Phase 14 afternoon
// (2026-10-26). run-gate runs [wallclock] cases in a quiet second pass on a
// small pool, but only if they are tagged; this lint keeps them tagged.
//
// What counts as "asserts on its wall clock": a REQUIRE / CHECK in the case
// (or in a file-local void helper the case calls) on a value derived from a
// clock reading — seeded by `x = ... .count()` / `duration_cast` /
// `duration<` / a helper that returns one, and followed through assignments
// into names that read as a time quantity (…Us, …Ms, seconds, elapsed, …),
// in a test file that reads steady_clock / high_resolution_clock /
// system_clock / QueryPerformanceCounter (itself or a test header it includes).
//
// Usage:
//   node tools/lint-wallclock-tags.js              # report, exit 1 on untagged cases
//   node tools/lint-wallclock-tags.js --root <dir> # lint another tree
//
// Exit codes: 0 = clean, 1 = untagged wall-clock cases, 2 = internal error.
// =============================================================================
'use strict';
const fs = require('fs');
const path = require('path');

const TAGGED = /\[(\.[^\]]*|performance|perf|benchmark|!benchmark|long|wallclock)\]/;
const CLOCK = /steady_clock|high_resolution_clock|system_clock::now|QueryPerformanceCounter/;
const ASSERT = /\b(REQUIRE|CHECK)(_THAT|_FALSE)?\s*\(/;
const SKIP = new Set(['auto', 'const', 'double', 'float', 'return', 'int', 'size_t', 'std', 'max', 'min']);
const TIMEISH = /(Us|Ns|Ms|Micros|Nanos|Millis|Seconds|Secs|Sec|Time|Duration|Elapsed|Wall\w*)$|^(us|ns|ms|seconds|secs|elapsed|wall\w*|duration)$/;
const HEAD = /^\s*(?:TEST_CASE|TEMPLATE_TEST_CASE|TEST_CASE_METHOD|SCENARIO)\s*\(((?:[^()]|\([^()]*\))*)\)/;

function testFiles(root) {
    const files = [];
    const walk = (d) => {
        if (!fs.existsSync(d)) return;
        for (const e of fs.readdirSync(d, { withFileTypes: true })) {
            const p = path.join(d, e.name);
            if (e.isDirectory()) { if (!['build', 'extern', '.git'].includes(e.name)) walk(p); }
            else if (/\.(cpp|h)$/.test(e.name)) files.push(p);
        }
    };
    walk(path.join(root, 'dsp', 'tests'));
    const plugins = path.join(root, 'plugins');
    if (fs.existsSync(plugins)) for (const p of fs.readdirSync(plugins)) walk(path.join(plugins, p, 'tests'));
    walk(path.join(root, 'tests'));
    return files;
}

function derivedNames(s) {
    const names = new Set();
    if (!CLOCK.test(s)) return names;
    const assigns = [...s.matchAll(/([A-Za-z_]\w*)\s*(?:\+|-|\*|\/)?=(?!=)\s*([^;]*);/g)].map((m) => [m[1], m[2]]);
    const clockFns = [...s.matchAll(/(\w+)\s*\([^()]*\)\s*(?:noexcept\s*)?\{\s*return[^;]*(?:\.count\(\)|duration_cast)/g)].map((m) => m[1]);
    const seed = new RegExp(`\\.count\\(\\)|duration_cast|duration<${clockFns.length ? `|\\b(${clockFns.join('|')})\\s*\\(` : ''}`);
    for (const [l, r] of assigns) if (seed.test(r) && (l.length >= 3 || TIMEISH.test(l)) && !SKIP.has(l)) names.add(l);
    for (let grew = true; grew && names.size;) {
        grew = false;
        const re = new RegExp(`\\b(${[...names].join('|')})\\b`);
        for (const [l, r] of assigns) {
            if (names.has(l) || l.length < 2 || SKIP.has(l) || !TIMEISH.test(l) || !re.test(r)) continue;
            names.add(l); grew = true;
        }
    }
    return names;
}

function lint(root) {
    const files = testFiles(root);
    const text = new Map(files.map((f) => [f, fs.readFileSync(f, 'utf8')]));
    const byBase = new Map();
    for (const f of files) { const b = path.basename(f); if (!byBase.has(b)) byBase.set(b, []); byBase.get(b).push(f); }
    const out = [];
    for (const f of files.filter((x) => x.endsWith('.cpp'))) {
        const s = text.get(f);
        let vis = s;
        for (const m of s.matchAll(/#\s*include\s*"([^"]+\.h)"/g)) for (const h of byBase.get(path.basename(m[1])) || []) vis += '\n' + text.get(h);
        const names = derivedNames(vis);
        if (!names.size) continue;
        const re = new RegExp(`\\b(${[...names].join('|')})\\b`);
        const helpers = [];
        for (const m of s.matchAll(/\n(?:static\s+|inline\s+)?void\s+(\w+)\s*\([^)]*\)\s*\{/g)) {
            const body = s.slice(m.index, s.indexOf('\n}', m.index) + 2);
            if (body.split('\n').some((l) => ASSERT.test(l) && re.test(l))) helpers.push(m[1]);
        }
        const starts = [...s.matchAll(/^\s*(?:TEST_CASE|TEMPLATE_TEST_CASE|TEST_CASE_METHOD|SCENARIO)\s*\(/gm)].map((m) => m.index);
        starts.forEach((at, i) => {
            const p = s.slice(at, starts[i + 1] ?? s.length);
            const head = HEAD.exec(p);
            if (!head || TAGGED.test(head[1])) return;
            const direct = p.split('\n').find((l) => ASSERT.test(l) && re.test(l));
            const via = helpers.find((h) => new RegExp(`\\b${h}\\s*\\(`).test(p));
            if (!direct && !via) return;
            const line = s.slice(0, at + p.search(/\S/)).split('\n').length;
            const name = (/"([^"]*)"/.exec(head[1]) || [, head[1]])[1];
            out.push({ file: path.relative(root, f).replace(/\\/g, '/'), line, name, why: direct ? direct.trim().slice(0, 100) : `via ${via}()` });
        });
    }
    return out;
}

try {
    const ri = process.argv.indexOf('--root');
    const root = path.resolve(ri >= 0 ? process.argv[ri + 1] : path.join(__dirname, '..'));
    const hits = lint(root);
    for (const h of hits) console.log(`${h.file}:${h.line}: "${h.name}" asserts on wall-clock time without [wallclock]\n    ${h.why}`);
    if (hits.length) {
        console.log(`\n${hits.length} untagged wall-clock case(s). Add [wallclock] to each TEST_CASE's tags: tools/run-gate.js then runs it in its quiet second pass.`);
        process.exit(1);
    }
    console.log('lint-wallclock-tags: clean');
} catch (e) { console.error(e.stack || String(e)); process.exit(2); }
