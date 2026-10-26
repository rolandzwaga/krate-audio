#!/usr/bin/env node
//
// lint-serial-test-runs.js
// =============================================================================
// Every non-timing multi-case test run goes through tools/run-gate.js, which
// cuts each test executable into Catch2 shards (--shard-count / --shard-index)
// and feeds them to a cpu-count worker pool. Running a whole executable, a tag
// across it, or a `for t in ...` list as ONE process uses one core of 32.
//
// Why a lint: the phase workflows' gates did exactly that for months — about
// 14 min per gate, ~16 gates per phase — and nobody noticed until the user
// asked why a workflow looked stalled (2026-10-26). The same gate sharded took
// 2 min 15 s. The instructions live in prose (workflow prompts, skills, agent
// definitions, CLAUDE.md files), so a new workflow or plugin copies whatever
// command it finds; this lint keeps every copy sharded.
//
// What it flags (in .claude/workflows, .claude/agents, .claude/skills, the root,
// dsp/ and every plugins/*/CLAUDE.md):
//   1. a test-executable invocation (`<name>_tests.exe`, `<target>.exe`,
//      `$t.exe`, `${t}.exe`) followed by nothing, a pipe / redirect, or a
//      filter that starts with `[`, `~` or `${` — a whole-executable or tag run;
//   2. `ctest --test-dir ...` without -j / --parallel (ctest is serial by default).
// What it allows: a single case by name (`<exe> "SomeTest*"`), --list-tests /
// --list-tags / --help, an explicit --shard-count, and a plain mention
// (`profundum_tests.exe` inside prose or a table).
//
// Exceptions: a line containing `lint-serial-ok` (or the line after one), and
// any line between `lint-serial-ok-begin` and `lint-serial-ok-end` markers —
// for example the legacy serial gate prompt kept byte-identical for
// args.serial_gates on a resumed workflow run. Give the reason in the marker.
//
// Ratchet: tools/lint-serial-test-runs-baseline.json holds a per-file count of
// known sites that cannot change yet (each with its reason); a new file or a
// higher count fails, and a fixed site prints "lower it".
//
// Timing tests (CPU budgets, benchmarks) are the one class that runs alone:
// node tools/run-cpu-tests.js. They are not test-executable invocations here.
//
// Usage:
//   node tools/lint-serial-test-runs.js              # report, exit 1 on violations
//   node tools/lint-serial-test-runs.js --root <dir> # lint another tree
//   node tools/lint-serial-test-runs.js --self-test  # check the detector itself
//
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================
'use strict';
const fs = require('fs');
const path = require('path');

const EXE = /((?:[\w./:${}<>-]*\/)?(?:[A-Za-z0-9_]+_tests|<target>|<test-target>|\$t|\$\{t\}))\.exe\b/g;

// Returns a reason string when the line runs a test executable serially, else null.
function checkLine(line) {
    if (/\bctest\b/.test(line) && /--test-dir/.test(line) && !/(\s-j\s*\d*|--parallel)/.test(line)) {
        return 'ctest without -j / --parallel runs one case at a time';
    }
    EXE.lastIndex = 0;
    let m;
    while ((m = EXE.exec(line)) !== null) {
        let rest = line.slice(m.index + m[0].length);
        const before = line.slice(0, m.index);
        // A quoted path: drop the closing quote that matches an opening one before the path.
        if ((rest.startsWith('"') && /"[^"]*$/.test(before)) || (rest.startsWith("'") && /'[^']*$/.test(before))) {
            rest = rest.slice(1);
        }
        // A mention, not a command: the path is followed directly by prose / markup punctuation.
        if (rest.length > 0 && !/^\s/.test(rest)) continue;
        const arg = rest.trim();
        if (/--shard-count|--list-tests|--list-tags|--list-reporters|--help|\s-\?/.test(rest)) continue;
        // A single case by name: a quoted (or bare) case pattern that is not a tag or exclusion.
        const q = /^(["'])(.*?)\1/.exec(arg);
        if (q && q[2] !== '' && !/^[[~$]/.test(q[2])) continue;
        if (!q && arg !== '' && /^[A-Za-z*]/.test(arg) && !/^(2>|\||>|&&|;)/.test(arg)) continue;
        return `serial run of ${m[1]}.exe — use node tools/run-gate.js (sharded); a single case by name is the only direct run`;
    }
    return null;
}

function lintText(text) {
    const out = [];
    const lines = text.split(/\r?\n/);
    let inRegion = false;
    for (let i = 0; i < lines.length; ++i) {
        const l = lines[i];
        if (/lint-serial-ok-begin/.test(l)) { inRegion = true; continue; }
        if (/lint-serial-ok-end/.test(l)) { inRegion = false; continue; }
        if (inRegion || /lint-serial-ok/.test(l) || (i > 0 && /lint-serial-ok(?!-)/.test(lines[i - 1]))) continue;
        const why = checkLine(l);
        if (why) out.push({ line: i + 1, why, text: l.trim().slice(0, 160) });
    }
    return out;
}

function filesUnder(root) {
    const list = [];
    const add = (p) => { if (fs.existsSync(p)) list.push(p); };
    const walk = (dir, re) => {
        if (!fs.existsSync(dir)) return;
        for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
            const p = path.join(dir, e.name);
            if (e.isDirectory()) walk(p, re);
            else if (re.test(e.name)) list.push(p);
        }
    };
    walk(path.join(root, '.claude', 'workflows'), /\.js$/);
    walk(path.join(root, '.claude', 'agents'), /\.md$/);
    walk(path.join(root, '.claude', 'skills'), /\.md$/);
    add(path.join(root, 'CLAUDE.md'));
    add(path.join(root, 'dsp', 'CLAUDE.md'));
    const plugins = path.join(root, 'plugins');
    if (fs.existsSync(plugins)) {
        for (const e of fs.readdirSync(plugins, { withFileTypes: true })) {
            if (e.isDirectory()) add(path.join(plugins, e.name, 'CLAUDE.md'));
        }
    }
    return list;
}

function selfTest() {
    const bad = [
        'build/windows-x64-release/bin/Release/profundum_tests.exe 2>&1 | tail -5',
        'build/windows-x64-release/bin/Release/dsp_core_tests.exe',
        '"F:/projects/iterum/build/windows-x64-release/bin/Release/dsp_primitives_tests.exe"',
        '"F:/projects/iterum/build/windows-x64-release/bin/Release/dsp_primitives_tests.exe" "[wavefold_math]"',
        "build/windows-x64-release/bin/Release/vorago_tests.exe '~[performance]~[perf]' > log.txt",
        'Then run each: build/windows-x64-release/bin/Release/<target>.exe ${PERF_FILTER} 2>&1 | tail -5',
        'for t in a b; do build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3; done',
        'ctest --test-dir build/windows-x64-release -C Release --output-on-failure',
    ];
    const good = [
        'node tools/run-gate.js --filter \'~[performance]~[perf]~[benchmark]~[!benchmark]\' profundum_tests',
        'dsp_core_tests.exe "SomeTest*"',
        '"F:/projects/iterum/build/windows-x64-release/bin/Release/dsp_processors_tests.exe" "*lambert*"',
        '"F:/projects/iterum/build/windows-x64-release/bin/Release/dsp_core_tests.exe" --list-tags',
        '(${dispatch.test_targets.map(t => `build/windows-x64-release/bin/Release/${t}.exe "<TestName>*"`)',
        '| Iterum | `plugin_tests` | `.../bin/Release/plugin_tests.exe`, `.../approval_tests.exe` | `Iterum.vst3` |',
        'Run one suite by passing the case name positionally (`dsp_core_tests.exe "SomeTest*"`), NOT',
        'ctest --test-dir build/windows-x64-release -C Release -j 30 --output-on-failure',
        '"$EXE" "Profundum_MorphModPrePhaseHarvest_${k}of4" > "$log" 2>&1',
        'build/windows-x64-release/bin/Release/x_tests.exe 2>&1  # lint-serial-ok: probe of one hidden case',
    ];
    let fails = 0;
    for (const l of bad) if (!checkLine(l)) { console.log(`SELF-TEST MISSED: ${l}`); fails++; }
    for (const l of good) if (lintText(l).length) { console.log(`SELF-TEST FALSE POSITIVE: ${l}`); fails++; }
    const region = 'a\n// lint-serial-ok-begin: legacy\nbuild/windows-x64-release/bin/Release/a_tests.exe\n// lint-serial-ok-end\n';
    if (lintText(region).length) { console.log('SELF-TEST FALSE POSITIVE: allow region'); fails++; }
    console.log(fails ? `self-test FAILED (${fails})` : `self-test OK (${bad.length} serial runs caught, ${good.length + 1} allowed)`);
    return fails ? 1 : 0;
}

function main() {
    if (process.argv.includes('--self-test')) process.exit(selfTest());
    const ri = process.argv.indexOf('--root');
    const root = path.resolve(ri >= 0 ? process.argv[ri + 1] : path.join(__dirname, '..'));
    // Ratchet: tools/lint-serial-test-runs-baseline.json allows a per-file count of known sites until they are fixed.
    const basePath = path.join(root, 'tools', 'lint-serial-test-runs-baseline.json');
    const baseline = fs.existsSync(basePath) ? (JSON.parse(fs.readFileSync(basePath, 'utf8')).files || {}) : {};
    let total = 0, baselined = 0;
    const files = filesUnder(root);
    for (const f of files) {
        const rel = path.relative(root, f).replace(/\\/g, '/');
        const hits = lintText(fs.readFileSync(f, 'utf8'));
        const allowed = baseline[rel] || 0;
        if (hits.length && hits.length <= allowed) {
            baselined += hits.length;
            if (hits.length < allowed) console.log(`${rel}: ${hits.length} serial run(s), baseline allows ${allowed}: lower it`);
            continue;
        }
        if (!hits.length && allowed) console.log(`${rel}: clean, drop its baseline entry (${allowed})`);
        for (const v of hits) {
            console.log(`${rel}:${v.line}: ${v.why}\n    ${v.text}`);
            total++;
        }
    }
    if (total) {
        console.log(`\n${total} serial test run(s) in ${files.length} files. Use node tools/run-gate.js --filter '<filter>~[performance]~[perf]~[benchmark]~[!benchmark]' <exe ...>.`);
        process.exit(1);
    }
    console.log(`lint-serial-test-runs: clean (${files.length} files${baselined ? `, ${baselined} baselined in tools/lint-serial-test-runs-baseline.json` : ''})`);
}

try { main(); } catch (e) { console.error(e.stack || String(e)); process.exit(2); }
