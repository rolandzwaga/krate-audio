#!/usr/bin/env node
// ==============================================================================
// aggregate-build-times.js  —  turn build-timing data into a ranked short report
// ==============================================================================
// Three data sources, used opportunistically (any subset that's present):
//
//   1. <build>/.ninja_log  (ZERO-CONFIG baseline, any Ninja build)
//        Per-output wall-clock (end-start ms). Ranks the slowest translation
//        units even without KRATE_BUILD_TIMING. Always try this first.
//
//   2. -ftime-trace JSONs  (Clang/GCC, KRATE_BUILD_TIMING=ON)
//        Per-TU Chrome-trace files (<obj>.json) with "Source" events whose
//        args.detail is an included file and dur is microseconds spent on it.
//        Summed across every TU -> the headers that cost the most globally
//        (blast radius), which .ninja_log alone can't show.
//
//   3. --msvc-log <file>  (MSVC /Bt+ /d2cgsummary, KRATE_BUILD_TIMING=ON)
//        Captured build stdout. Best-effort parse of `time(...)=N s ... [path]`
//        front-end (c1xx) + back-end (c2) per file.
//
// Usage:
//   node tools/aggregate-build-times.js --build build/linux-timing
//   node tools/aggregate-build-times.js --build build/windows-ninja-timing --msvc-log build.log
//   node tools/aggregate-build-times.js --build build/windows-ninja-dev --out specs/_architecture_/build-times.md
//
// Flags: --build <dir> (required), --msvc-log <file>, --out <file> (default: stdout),
//        --top <N> (default 20).
// ==============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

// ---- args ----
const argv = process.argv.slice(2);
function flag(name, def) {
    const i = argv.indexOf(name);
    return i >= 0 && i + 1 < argv.length ? argv[i + 1] : def;
}
const buildDir = flag('--build', null);
const msvcLog = flag('--msvc-log', null);
const outFile = flag('--out', null);
const topN = parseInt(flag('--top', '20'), 10) || 20;

if (!buildDir) {
    console.error('ERROR: --build <dir> is required (the CMake binary dir).');
    console.error('e.g. node tools/aggregate-build-times.js --build build/linux-timing');
    process.exit(1);
}
if (!fs.existsSync(buildDir)) {
    console.error(`ERROR: build dir not found: ${buildDir}`);
    process.exit(1);
}

const fmtMs = (ms) => (ms >= 1000 ? (ms / 1000).toFixed(2) + ' s' : Math.round(ms) + ' ms');

// ---- 1. .ninja_log -> slowest targets ----
function parseNinjaLog(dir) {
    const p = path.join(dir, '.ninja_log');
    if (!fs.existsSync(p)) return null;
    const lines = fs.readFileSync(p, 'utf8').split(/\r?\n/);
    const byOutput = new Map(); // output -> max duration ms (last build wins on ties)
    for (const line of lines) {
        if (!line || line.startsWith('#')) continue;
        const parts = line.split('\t');
        if (parts.length < 4) continue;
        const start = parseInt(parts[0], 10);
        const end = parseInt(parts[1], 10);
        const output = parts[3];
        if (!Number.isFinite(start) || !Number.isFinite(end)) continue;
        const dur = end - start;
        // Only real compile/link outputs are interesting; keep the largest seen.
        const prev = byOutput.get(output);
        if (prev === undefined || dur > prev) byOutput.set(output, dur);
    }
    const rows = [...byOutput.entries()]
        .filter(([out]) => /\.(o|obj)$/.test(out)) // TUs, not link steps or stamps
        .map(([out, dur]) => ({ out, dur }))
        .sort((a, b) => b.dur - a.dur);
    return rows;
}

// ---- 2. -ftime-trace JSON -> worst headers ----
function findTraceFiles(dir) {
    const results = [];
    const stack = [dir];
    while (stack.length) {
        const d = stack.pop();
        let entries;
        try {
            entries = fs.readdirSync(d, { withFileTypes: true });
        } catch {
            continue;
        }
        for (const e of entries) {
            const full = path.join(d, e.name);
            if (e.isDirectory()) {
                stack.push(full);
            } else if (e.isFile() && e.name.endsWith('.json')) {
                results.push(full);
            }
        }
    }
    return results;
}

function parseTraceFiles(files) {
    const headerUs = new Map(); // file -> total microseconds across all TUs
    const headerCount = new Map(); // file -> how many TUs included it
    let tuCount = 0;
    for (const f of files) {
        let json;
        try {
            json = JSON.parse(fs.readFileSync(f, 'utf8'));
        } catch {
            continue;
        }
        if (!json || !Array.isArray(json.traceEvents)) continue; // not a time-trace file
        tuCount++;
        for (const ev of json.traceEvents) {
            if (ev.name !== 'Source' || !ev.args || !ev.args.detail || !ev.dur) continue;
            const detail = String(ev.args.detail);
            headerUs.set(detail, (headerUs.get(detail) || 0) + ev.dur);
            headerCount.set(detail, (headerCount.get(detail) || 0) + 1);
        }
    }
    if (tuCount === 0) return null;
    const rows = [...headerUs.entries()]
        .map(([file, us]) => ({ file, ms: us / 1000, count: headerCount.get(file) || 0 }))
        .sort((a, b) => b.ms - a.ms);
    return { rows, tuCount };
}

// ---- 3. MSVC /Bt+ log -> slowest files ----
function parseMsvcLog(file) {
    if (!file || !fs.existsSync(file)) return null;
    const text = fs.readFileSync(file, 'utf8');
    // /Bt+ lines look like:  time(...\c1xx.dll)=0.12345s < ... > [F:\path\file.cpp]
    const re = /time\([^)]*\b(c1xx|c1|c2)\b[^)]*\)=([\d.]+)s.*?\[([^\]]+)\]/g;
    const byFile = new Map(); // file -> {front, back}
    let m;
    while ((m = re.exec(text)) !== null) {
        const pass = m[1];
        const secs = parseFloat(m[2]);
        const src = m[3];
        if (!Number.isFinite(secs)) continue;
        const rec = byFile.get(src) || { front: 0, back: 0 };
        if (pass === 'c2') rec.back += secs;
        else rec.front += secs;
        byFile.set(src, rec);
    }
    if (byFile.size === 0) return null;
    const rows = [...byFile.entries()]
        .map(([file, r]) => ({ file, ms: (r.front + r.back) * 1000, front: r.front * 1000, back: r.back * 1000 }))
        .sort((a, b) => b.ms - a.ms);
    return rows;
}

// ---- assemble report ----
const lines = [];
const push = (s) => lines.push(s);

push('# Build-Time Report');
push('');
push(`Generated by \`tools/aggregate-build-times.js\` from \`${buildDir}\`. Regenerable — ` +
    'timings are machine/parallelism-dependent; read the *ranking*, not absolute ms. ' +
    'See `tools/BUILD-TIMING.md` for how to refresh with header-level detail.');
push('');

const ninja = parseNinjaLog(buildDir);
if (ninja && ninja.length) {
    push(`## Slowest translation units (\`.ninja_log\`, wall-clock)`);
    push('');
    push('| # | ms | object |');
    push('|--:|---:|--------|');
    ninja.slice(0, topN).forEach((r, i) => {
        push(`| ${i + 1} | ${Math.round(r.dur)} | \`${r.out}\` |`);
    });
    push('');
    const total = ninja.reduce((a, r) => a + r.dur, 0);
    push(`_${ninja.length} compiled objects, ${fmtMs(total)} of summed compile wall-clock (parallelism not counted)._`);
    push('');
} else {
    push('## Slowest translation units');
    push('');
    push(`_No \`.ninja_log\` in ${buildDir} — is this a Ninja build that has been built at least once?_`);
    push('');
}

const traces = parseTraceFiles(findTraceFiles(buildDir));
if (traces) {
    push(`## Worst headers (\`-ftime-trace\`, summed across ${traces.tuCount} TUs)`);
    push('');
    push('| # | total ms | in # TUs | avg ms/TU | file |');
    push('|--:|---------:|---------:|----------:|------|');
    traces.rows.slice(0, topN).forEach((r, i) => {
        const avg = r.count ? (r.ms / r.count).toFixed(1) : '0';
        push(`| ${i + 1} | ${Math.round(r.ms)} | ${r.count} | ${avg} | \`${r.file}\` |`);
    });
    push('');
    push('_A header high on this list but included in few TUs is a heavy include; one included in many TUs is a broad dependency — both are worth breaking up (see D4)._');
    push('');
} else {
    push('## Worst headers (`-ftime-trace`)');
    push('');
    push('_No `-ftime-trace` JSONs found. Configure with a timing preset ' +
        '(`windows-ninja-timing` / `linux-timing`) or `-DKRATE_BUILD_TIMING=ON` and rebuild. ' +
        'On MSVC there are no trace JSONs — use `--msvc-log` instead._');
    push('');
}

if (msvcLog) {
    const msvc = parseMsvcLog(msvcLog);
    if (msvc) {
        push(`## Slowest files (MSVC \`/Bt+\`, front-end + back-end)`);
        push('');
        push('| # | total ms | front ms | back ms | file |');
        push('|--:|---------:|---------:|--------:|------|');
        msvc.slice(0, topN).forEach((r, i) => {
            push(`| ${i + 1} | ${Math.round(r.ms)} | ${Math.round(r.front)} | ${Math.round(r.back)} | \`${r.file}\` |`);
        });
        push('');
    } else {
        push('## Slowest files (MSVC `/Bt+`)');
        push('');
        push(`_Could not parse timing lines from ${msvcLog}. Ensure the build ran with ` +
            '`KRATE_BUILD_TIMING=ON` (adds `/Bt+ /d2cgsummary`) and that stdout was captured._');
        push('');
    }
}

const report = lines.join('\n') + '\n';
if (outFile) {
    fs.mkdirSync(path.dirname(path.resolve(outFile)), { recursive: true });
    fs.writeFileSync(outFile, report);
    console.error(`Wrote ${outFile}`);
} else {
    process.stdout.write(report);
}
