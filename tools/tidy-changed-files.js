#!/usr/bin/env node
// ==============================================================================
// tidy-changed-files.js  —  which translation units must clang-tidy analyze?
// ==============================================================================
// run-clang-tidy.ps1 / .sh analyze ONLY what changed by default; this tool picks
// those files. A full sweep needs -Full / --full (guarded by
// tools/hooks/guard-tidy-scope.js and tools/lint-tidy-scope.js).
//
// WHY: a task that changed one DSP header (fdn_reverb.h) and its test ran
// clang-tidy over all 482 dsp TUs at -Jobs 4, ~40 min, to re-check 2 files
// (Ruinae audit T6.10, 2026-11-23). A memory note did not stop it; the default did.
//
// Selection, restricted to files under the caller's --roots:
//   - a changed .cpp that the compile db compiles      -> analyzed itself
//   - a changed header (.h/.hpp)                       -> ONE translation unit that includes it. Header
//     diagnostics come from HeaderFilterRegex (.clang-tidy), so any includer reports them; the nearest
//     includer (fewest include hops) is chosen, preferring one named after the header
//     (<stem>_test.cpp, test_<stem>.cpp, <stem>.cpp), then the one with the fewest direct includes (so
//     dsp/lint_all_headers.cpp is the last resort, not the default).
//   - a header no compiled TU under --roots includes   -> reported, nothing analyzed for it.
// "Changed" = `git diff --name-only <base>` (default HEAD: staged + unstaged) plus untracked files.
// --base main widens it to everything since main.
//
// Usage:
//   node tools/tidy-changed-files.js --roots dsp/include,dsp/tests,dsp --compile-db build/windows-ninja/compile_commands.json [--base HEAD]
//   node tools/tidy-changed-files.js --self-test
// Output: one repo-relative TU path per line on stdout; the reasons on stderr.
// Exit 0 always on success (an empty list = nothing to analyze); 2 on usage error.
// ==============================================================================
'use strict';
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const SRC_EXT = /\.(cpp|cc|cxx)$/i;
const HDR_EXT = /\.(h|hpp|hh|inl)$/i;
const SCAN_ROOTS = ['dsp', 'plugins', 'tests', 'tools/membrum-fit'];
const SKIP_DIR = /^(build|_deps|extern|node_modules|\.git)$/;

const rel = (p) => path.relative(ROOT, p).replace(/\\/g, '/');

function walk(dir, out) {
    let entries;
    try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
    for (const e of entries) {
        const p = path.join(dir, e.name);
        if (e.isDirectory()) { if (!SKIP_DIR.test(e.name)) walk(p, out); }
        else if (SRC_EXT.test(e.name) || HDR_EXT.test(e.name)) out.push(rel(p));
    }
}

// Include resolution: the including file's own directory first, then the project's include roots.
function includeCandidates(file, inc) {
    const out = [path.posix.normalize(path.posix.join(path.posix.dirname(file), inc))];
    out.push('dsp/include/' + inc);
    const pm = file.match(/^(plugins\/[^/]+|tools\/membrum-fit)\//);
    if (pm) { out.push(`${pm[1]}/src/${inc}`, `${pm[1]}/tests/${inc}`, `${pm[1]}/${inc}`); }
    out.push('plugins/shared/src/' + inc, 'plugins/shared/tests/' + inc, 'tests/' + inc, 'plugins/' + inc);
    return out;
}

function buildGraph(files) {
    const exists = new Set(files);
    const includers = new Map();     // header -> Set(files that include it directly)
    const includeCount = new Map();  // file -> number of resolved direct includes
    const incRe = /^\s*#\s*include\s*[<"]([^>"]+)[>"]/gm;
    for (const f of files) {
        let txt;
        try { txt = fs.readFileSync(path.join(ROOT, f), 'utf8'); } catch { continue; }
        let n = 0;
        for (const m of txt.matchAll(incRe)) {
            const hit = includeCandidates(f, m[1]).find((c) => exists.has(c));
            if (!hit) continue;
            n++;
            if (!includers.has(hit)) includers.set(hit, new Set());
            includers.get(hit).add(f);
        }
        includeCount.set(f, n);
    }
    return { includers, includeCount };
}

function underRoots(file, roots) {
    return roots.some((r) => {
        if (r.endsWith('!')) {                       // non-recursive root (e.g. "dsp!" = files directly in dsp/)
            const d = r.slice(0, -1);
            return path.posix.dirname(file) === d;
        }
        return file === r || file.startsWith(r + '/');
    });
}

function pickTu(header, graph, isCandidateTu) {
    const stem = path.posix.basename(header).replace(HDR_EXT, '');
    const seen = new Set([header]);
    let level = [header];
    while (level.length) {
        const tus = [];
        const next = [];
        for (const h of level) {
            for (const f of graph.includers.get(h) || []) {
                if (seen.has(f)) continue;
                seen.add(f);
                if (SRC_EXT.test(f)) { if (isCandidateTu(f)) tus.push(f); }
                else next.push(f);
            }
        }
        if (tus.length) {
            const named = (f) => {
                const b = path.posix.basename(f).replace(SRC_EXT, '');
                return b === stem || b === `${stem}_test` || b === `test_${stem}` || b === `${stem}_tests`;
            };
            tus.sort((a, b) => (named(b) - named(a))
                || ((graph.includeCount.get(a) || 0) - (graph.includeCount.get(b) || 0))
                || a.localeCompare(b));
            return tus[0];
        }
        level = next;
    }
    return null;
}

function changedFiles(base) {
    const git = (args) => execFileSync('git', args, { cwd: ROOT, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] })
        .split(/\r?\n/).map((s) => s.trim()).filter(Boolean);
    const files = new Set([...git(['diff', '--name-only', base]), ...git(['ls-files', '--others', '--exclude-standard'])]);
    return [...files].filter((f) => fs.existsSync(path.join(ROOT, f)));
}

function compiledSet(dbPath) {
    const set = new Set();
    if (!dbPath || !fs.existsSync(dbPath)) return null;   // no db: every .cpp counts as compiled
    // An MSYS / Git Bash path (/f/projects/...) on Windows means F:/projects/...
    const native = (p) => (process.platform === 'win32' ? p.replace(/^\/([a-zA-Z])\//, '$1:/') : p);
    for (const e of JSON.parse(fs.readFileSync(dbPath, 'utf8'))) {
        const file = native(e.file);
        const abs = path.isAbsolute(file) ? file : path.join(native(e.directory), file);
        set.add(rel(path.resolve(abs)).toLowerCase());
    }
    return set;
}

function select({ changed, roots, compiled, graph }) {
    const isCompiled = (f) => !compiled || compiled.has(f.toLowerCase());
    const isCandidateTu = (f) => underRoots(f, roots) && isCompiled(f);
    const picked = new Map();   // tu -> reason
    const notes = [];
    for (const f of changed.filter((c) => underRoots(c, roots))) {
        if (SRC_EXT.test(f)) {
            if (isCompiled(f)) picked.set(f, picked.get(f) || 'changed');
            else notes.push(`skip ${f}: not in compile_commands.json on this platform`);
        } else if (HDR_EXT.test(f)) {
            const tu = pickTu(f, graph, isCandidateTu);
            if (tu) { if (!picked.has(tu)) picked.set(tu, `includes changed ${f}`); }
            else notes.push(`no compiled TU under the target includes ${f}: nothing to analyze for it`);
        }
    }
    return { picked, notes };
}

function selfTest() {
    const inc = (m) => new Map(Object.entries(m).map(([k, v]) => [k, new Set(v)]));
    const graph = {
        includers: inc({
            'dsp/include/krate/dsp/effects/rev.h': ['dsp/tests/unit/effects/other_test.cpp', 'dsp/tests/unit/effects/rev_test.cpp', 'dsp/lint_all_headers.cpp'],
            'dsp/include/krate/dsp/core/util.h': ['dsp/include/krate/dsp/effects/rev.h', 'dsp/lint_all_headers.cpp'],
            'plugins/p/src/engine.h': ['plugins/p/src/proc.cpp'],
        }),
        includeCount: new Map([['dsp/lint_all_headers.cpp', 400], ['dsp/tests/unit/effects/other_test.cpp', 3],
            ['dsp/tests/unit/effects/rev_test.cpp', 5], ['plugins/p/src/proc.cpp', 4]]),
    };
    const dspRoots = ['dsp/include', 'dsp/tests', 'dsp!'];
    const cases = [
        ['header -> its named test, not lint_all_headers', ['dsp/include/krate/dsp/effects/rev.h'], dspRoots, ['dsp/tests/unit/effects/rev_test.cpp']],
        ['header without a named TU -> fewest-include TU at the nearest level', ['dsp/include/krate/dsp/core/util.h'], dspRoots, ['dsp/lint_all_headers.cpp']],
        ['changed .cpp -> itself', ['dsp/tests/unit/effects/other_test.cpp'], dspRoots, ['dsp/tests/unit/effects/other_test.cpp']],
        ['file outside the roots -> nothing', ['plugins/p/src/proc.cpp'], dspRoots, []],
        ['plugin header -> its includer', ['plugins/p/src/engine.h'], ['plugins/p/src'], ['plugins/p/src/proc.cpp']],
        ['orphan header -> nothing', ['plugins/p/src/orphan.h'], ['plugins/p/src'], []],
        ['non-source change -> nothing', ['plugins/p/CHANGELOG.md'], ['plugins/p/src'], []],
    ];
    let failed = 0;
    for (const [name, changed, roots, want] of cases) {
        const got = [...select({ changed, roots, compiled: null, graph }).picked.keys()].sort();
        if (JSON.stringify(got) !== JSON.stringify([...want].sort())) {
            failed++;
            console.log(`FAIL ${name}: got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`);
        }
    }
    console.log(failed ? `tidy-changed-files self-test: ${failed} FAILED` : `tidy-changed-files self-test: ${cases.length} cases OK`);
    process.exit(failed ? 1 : 0);
}

function main() {
    const argv = process.argv.slice(2);
    if (argv.includes('--self-test')) selfTest();
    const arg = (name, dflt) => { const i = argv.indexOf(name); return i >= 0 && i + 1 < argv.length ? argv[i + 1] : dflt; };
    const rootsArg = arg('--roots', null);
    if (!rootsArg) { console.error('usage: node tools/tidy-changed-files.js --roots a,b[,dir!] [--compile-db <json>] [--base <ref>]'); process.exit(2); }
    const roots = rootsArg.split(',').map((r) => r.trim().replace(/\\/g, '/').replace(/\/$/, '')).filter(Boolean);
    const base = arg('--base', 'HEAD');
    const dbArg = arg('--compile-db', null);
    const compiled = compiledSet(dbArg && path.resolve(ROOT, dbArg));

    const changed = changedFiles(base);
    const relevant = changed.filter((f) => underRoots(f, roots) && (SRC_EXT.test(f) || HDR_EXT.test(f)));
    let graph = { includers: new Map(), includeCount: new Map() };
    if (relevant.some((f) => HDR_EXT.test(f))) {
        const all = [];
        for (const r of SCAN_ROOTS) walk(path.join(ROOT, r), all);
        graph = buildGraph(all);
    }
    const { picked, notes } = select({ changed, roots, compiled, graph });
    console.error(`[tidy-changed] base ${base}: ${relevant.length} changed source file(s) under ${roots.join(', ')} -> ${picked.size} TU(s)`);
    for (const n of notes) console.error(`[tidy-changed] ${n}`);
    for (const [tu, why] of picked) { console.error(`[tidy-changed]   ${tu}  (${why})`); console.log(tu); }
}

main();
