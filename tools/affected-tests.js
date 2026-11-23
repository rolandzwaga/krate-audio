#!/usr/bin/env node
// ==============================================================================
// affected-tests.js  —  which test cases can a change reach?
// ==============================================================================
// tools/run-gate.js and tools/run-cpu-tests.js run ONLY the cases this module selects (default --scope affected);
// a whole-suite run needs --scope all and a "# gate-all-ok: <reason>" comment (tools/hooks/guard-test-scope.js,
// tools/lint-test-scope.js). CI keeps whole suites (the CI env var makes --scope all the default there).
//
// WHY: "run only the tests the change set can reach" was a CLAUDE.md rule and a memory note, and runs still covered
// whole executables (a close-out gate of nine suites ran 6 h on code the change never touched, 2026-11-23). The user
// asked for a mechanical check, as for clang-tidy (tools/tidy-changed-files.js).
//
// A test case is affected when the source file that defines it (Catch2's own listing: --list-tests --reporter xml)
// is in the reach of the change:
//   - the changed files themselves (staged + unstaged + untracked vs --base, default HEAD);
//   - a changed X.cpp also seeds its X.h / X.hpp (a test exercises a compiled source through its header);
//   - a changed non-source file (preset, golden, doc, data) seeds every TEST source whose string literals name its
//     basename or two consecutive directories of its path (e.g. "resources/presets"): tests that read files are
//     consumers too. A file under plugins/<p>/ only reaches plugins/<p>/ tests and the shared test areas;
//   - then every file that (transitively) #includes something reached (include strings resolved by path suffix, only
//     inside the includer's own plugin or the shared areas, as tools/header-consumers.js does).
// A changed CMakeLists.txt / *.cmake / CMakePresets.json is build-wide: every case is affected.
//
// CLI (dry run, shows the reach):  node tools/affected-tests.js [--base <ref>]
// ==============================================================================
'use strict';
const fs = require('fs');
const path = require('path');
const { execFileSync, spawnSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const SRC = /\.(cpp|cc|cxx|h|hpp|hh|inl)$/i;
const SCAN_ROOTS = ['dsp', 'plugins', 'tests', 'tools/membrum-fit'];
const SKIP_DIR = /^(build|_deps|extern|node_modules|\.git|\..*)$/;
const BUILD_WIDE = /(^|\/)CMakeLists\.txt$|\.cmake$|^CMakePresets\.json$/;

const norm = (p) => p.replace(/\\/g, '/');
const relOf = (abs) => norm(path.relative(ROOT, abs));

function changedFiles(base = 'HEAD') {
    const git = (args) => execFileSync('git', args, { cwd: ROOT, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] })
        .split(/\r?\n/).map((s) => norm(s.trim())).filter(Boolean);
    return [...new Set([...git(['diff', '--name-only', base]), ...git(['ls-files', '--others', '--exclude-standard'])])];
}

let graphCache = null;
function graph() {
    if (graphCache) return graphCache;
    const files = [];
    const walk = (dir) => {
        let entries;
        try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
        for (const e of entries) {
            const p = path.join(dir, e.name);
            if (e.isDirectory()) { if (!SKIP_DIR.test(e.name)) walk(p); }
            else if (SRC.test(e.name)) files.push(relOf(p));
        }
    };
    for (const r of SCAN_ROOTS) walk(path.join(ROOT, r));
    const area = (f) => { const m = /^plugins\/([^/]+)\//.exec(f); return m && m[1] !== 'shared' ? `plugins/${m[1]}` : 'shared'; };
    const byTail = new Map();
    for (const f of files) { const b = path.posix.basename(f); if (!byTail.has(b)) byTail.set(b, []); byTail.get(b).push(f); }
    const includers = new Map();
    const text = new Map();
    for (const f of files) {
        let s;
        try { s = fs.readFileSync(path.join(ROOT, f), 'utf8'); } catch { continue; }
        text.set(f, s);
        for (const m of s.matchAll(/^\s*#\s*include\s*[<"]([^>"]+)[>"]/gm)) {
            const inc = norm(m[1]);
            for (const t of byTail.get(path.posix.basename(inc)) || []) {
                if (area(t) !== 'shared' && area(t) !== area(f)) continue;
                if (t === inc || t.endsWith('/' + inc)) { if (!includers.has(t)) includers.set(t, new Set()); includers.get(t).add(f); }
            }
        }
    }
    graphCache = { files, includers, text };
    return graphCache;
}

// Strings a test that reads a non-source file would contain: its basename, and each pair of consecutive directories
// below the top-level one ("plugins", "dsp", "specs", ...).
function dataTokens(file) {
    const segs = file.split('/');
    const dirs = segs.slice(1, -1);
    const out = [segs[segs.length - 1]];
    for (let i = 0; i + 1 < dirs.length; ++i) out.push(`${dirs[i]}/${dirs[i + 1]}`);
    return out;
}

// { changed, buildWide: <file>|null, reached: Set<repo-relative file> }
function reach(base = 'HEAD', changedOverride = null) {
    const changed = changedOverride || changedFiles(base);
    const buildWide = changed.find((f) => BUILD_WIDE.test(f)) || null;
    if (buildWide) return { changed, buildWide, reached: null };
    const g = graph();
    const seeds = new Set();
    for (const f of changed) {
        if (SRC.test(f)) {
            seeds.add(f);
            if (/\.(cpp|cc|cxx)$/i.test(f)) {
                for (const ext of ['.h', '.hpp']) {
                    const h = f.replace(/\.(cpp|cc|cxx)$/i, ext);
                    if (g.text.has(h)) seeds.add(h);
                }
            }
        } else {
            // Only test sources read data files, and they name them in string literals. A plugin's file is read by
            // that plugin's tests (or the shared test helpers), never another plugin's.
            const owner = /^plugins\/([^/]+)\//.exec(f);
            const inScope = (file) => /(^|\/)tests\//.test(file)
                && (!owner || owner[1] === 'shared' || file.startsWith(`plugins/${owner[1]}/`) || !/^plugins\/(?!shared\/)/.test(file));
            const literals = dataTokens(f).map((t) => new RegExp(`"[^"\\n]*${t.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}[^"\\n]*"`));
            for (const [file, s] of g.text) if (inScope(file) && literals.some((re) => re.test(s))) seeds.add(file);
        }
    }
    const reached = new Set(seeds);
    const queue = [...seeds];
    while (queue.length) {
        for (const up of g.includers.get(queue.pop()) || []) if (!reached.has(up)) { reached.add(up); queue.push(up); }
    }
    return { changed, buildWide: null, reached };
}

const decodeXml = (s) => s.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&apos;/g, "'")
    .replace(/&#(\d+);/g, (_, d) => String.fromCodePoint(+d)).replace(/&amp;/g, '&');

// Every case the filter selects in one executable, with its defining file: [{ name, file }] (file repo-relative),
// or null when the listing fails.
function listCases(exePath, filter, extraArgs = []) {
    const r = spawnSync(exePath, [filter, '--list-tests', '--reporter', 'xml', '--order', 'decl', ...extraArgs],
        { encoding: 'utf8', maxBuffer: 256 * 1024 * 1024, windowsHide: true });
    if (r.status !== 0 && !r.stdout) return null;
    const out = [];
    for (const m of (r.stdout || '').matchAll(/<TestCase>([\s\S]*?)<\/TestCase>/g)) {
        const name = /<Name>([\s\S]*?)<\/Name>/.exec(m[1]);
        const file = /<File>([\s\S]*?)<\/File>/.exec(m[1]);
        if (!name) continue;
        out.push({ name: decodeXml(name[1]), file: file ? relOf(path.resolve(decodeXml(file[1]))) : '' });
    }
    return out;
}

const isAffected = (r, file) => !r.reached || r.reached.has(file);

module.exports = { changedFiles, reach, listCases, isAffected, dataTokens };

if (require.main === module) {
    const argv = process.argv.slice(2);
    const i = argv.indexOf('--base');
    const base = i >= 0 ? argv[i + 1] : 'HEAD';
    const r = reach(base);
    console.log(`changed since ${base}: ${r.changed.length} file(s)`);
    for (const f of r.changed) console.log(`  ${f}`);
    if (r.buildWide) { console.log(`build-wide change (${r.buildWide}): every test case is affected`); process.exit(0); }
    const tus = [...r.reached].filter((f) => /(^|\/)tests\//.test(f) && /\.(cpp|cc|cxx)$/i.test(f)).sort();
    console.log(`test TUs in reach: ${tus.length}`);
    for (const t of tus) console.log(`  ${t}`);
}
