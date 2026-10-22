#!/usr/bin/env node
// Profundum Phase 9, task T025 (tasks.md deviation 9): the consumer [long] shard list, computed, never hand-kept.
//
// The phase modified one shared header, dsp/include/krate/dsp/systems/mass_dynamics.h (T005, append-only opt-in).
// Root CLAUDE.md ("Run only the tests the change set can reach") requires the full suites of every consumer of a
// MODIFIED shared header, so this script:
//   1. builds the include graph over dsp/tests/**/*.{h,cpp} plus dsp/include/**/*.h (so a test that reaches
//      mass_dynamics.h only through another KrateDSP header is still a consumer), following #include "..." lines
//      (resolved against the including file's directory, dsp/tests, dsp/tests/unit, tests/test_helpers and
//      dsp/include) and #include <krate/...> lines (resolved against dsp/include);
//   2. takes the transitive reverse closure of mass_dynamics.h and keeps the consumer .cpp files under dsp/tests
//      that are NOT Phase 9 TUs (the Phase 9 TUs run in suites.sh lane 3b by their [voice-engine] tag);
//   3. extracts every TEST_CASE("<name>", "<tags>") of those files whose tags contain [long] and none of
//      [performance] / [perf] / [benchmark] / [!benchmark] (timing cases belong to the isolated CPU lane), and no
//      hidden tag ([.] / [.xxx] / [!hide]: probes, never gates);
//   4. cross-checks each name against `dsp_systems_tests.exe --list-tests "[long]" --reporter xml` (the existing
//      binary). The XML listing carries each case's SourceInfo file, so the listed [long] cases of the consumer TUs
//      (same tag filter) are also added: that recovers the macro-generated TEST_CASEs (stringised names such as
//      bass_stereo_field_monosum_test.cpp's "BassStereoField_GravityFloor_" #Arm) that a source scan cannot expand.
// stdout: one TEST_CASE name per line (sorted). stderr: the closure headers, per-TU case counts, the cross-check
// result. Exit 0 when the cross-check passed (or was skipped with no macro-generated case); 1 when a source-derived
// name is missing from the binary's list, the listing failed, or macro-generated cases exist and the binary does not;
// 2 on a usage / IO error.
//
// Usage (from anywhere): node specs/profundum-phase9-voice-engine/evidence/close/consumer-cases.js [--exe <path>]

'use strict';

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const REPO = path.resolve(__dirname, '..', '..', '..', '..');
const DSP_INCLUDE = path.join(REPO, 'dsp', 'include');
const DSP_TESTS = path.join(REPO, 'dsp', 'tests');
const BASE_COMMIT = '0ae33b87';   // suites.sh BASE_COMMIT
// Close (2026-10-21): every MODIFIED header under dsp/include against the base commit (14 at the close, the CPU
// optimisation group's and the limiter look-ahead's), not mass_dynamics.h alone; new (untracked) headers have no
// consumers outside the phase and are not roots.
function rootHeaders() {
    const { execSync } = require('child_process');
    const out = execSync(`git diff --name-only ${BASE_COMMIT} -- dsp/include`, { cwd: REPO, encoding: 'utf8' });
    const roots = out
        .split(/\r?\n/)
        .map((l) => l.trim())
        .filter((l) => l.endsWith('.h'))
        .map((l) => path.join(REPO, l));
    if (roots.length === 0) roots.push(path.join(DSP_INCLUDE, 'krate', 'dsp', 'systems', 'mass_dynamics.h'));
    return roots;
}
const ROOT_HEADERS = rootHeaders();

// The fourteen Phase 9 TUs (tasks.md "File map"; dsp/tests/CMakeLists.txt, run-log/T024.txt).
const PHASE9_TUS = new Set(
    [
        'profundum_voice_engine_helpers_test.cpp',
        'profundum_motion_test.cpp',
        'profundum_macro_matrix_test.cpp',
        'profundum_voice_test.cpp',
        'profundum_engine_test.cpp',
        'profundum_engine_nonfinite_test.cpp',
        'profundum_engine_click_test.cpp',
        'profundum_engine_audibility_test.cpp',
        'profundum_engine_chain_test.cpp',
        'profundum_engine_aliasing_test.cpp',
        'profundum_engine_soak_test.cpp',
        'profundum_engine_probe_test.cpp',
        'profundum_engine_perf_test.cpp',
        'mass_dynamics_oversampling_test.cpp',
    ].map((f) => path.join(DSP_TESTS, 'unit', 'systems', f)),
);

const EXCLUDED_TAGS = ['[performance]', '[perf]', '[benchmark]', '[!benchmark]'];

function parseArgs(argv) {
    let exe = path.join(REPO, 'build', 'windows-x64-release', 'bin', 'Release', 'dsp_systems_tests.exe');
    for (let i = 2; i < argv.length; ++i) {
        if (argv[i] === '--exe' && i + 1 < argv.length) {
            exe = path.resolve(argv[++i]);
        } else {
            process.stderr.write(`usage: node consumer-cases.js [--exe <dsp_systems_tests.exe>]\n`);
            process.exit(2);
        }
    }
    return { exe };
}

function walk(dir, exts, out) {
    let entries;
    try {
        entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
        return out;
    }
    for (const e of entries) {
        const p = path.join(dir, e.name);
        if (e.isDirectory()) {
            walk(p, exts, out);
        } else if (exts.includes(path.extname(e.name).toLowerCase())) {
            out.push(p);
        }
    }
    return out;
}

const key = (p) => path.resolve(p).toLowerCase(); // Windows paths are case-insensitive

function resolveInclude(fromFile, spec, angled, known) {
    const candidates = [];
    if (angled) {
        if (spec.startsWith('krate/')) {
            candidates.push(path.join(DSP_INCLUDE, spec));
        }
    } else {
        candidates.push(
            path.join(path.dirname(fromFile), spec),
            path.join(DSP_TESTS, spec),
            path.join(DSP_TESTS, 'unit', spec),
            path.join(REPO, 'tests', 'test_helpers', spec),
            path.join(DSP_INCLUDE, spec),
        );
    }
    for (const c of candidates) {
        if (known.has(key(c))) {
            return known.get(key(c));
        }
    }
    return null;
}

// Removes /* */ comments and // comments while leaving string literals intact.
function stripComments(src) {
    let out = '';
    let i = 0;
    const n = src.length;
    while (i < n) {
        const c = src[i];
        const d = src[i + 1];
        if (c === '"' || c === "'") {
            const q = c;
            out += c;
            ++i;
            while (i < n && src[i] !== q && src[i] !== '\n') {
                if (src[i] === '\\' && i + 1 < n) {
                    out += src[i] + src[i + 1];
                    i += 2;
                } else {
                    out += src[i++];
                }
            }
            if (i < n) out += src[i++];
        } else if (c === '/' && d === '/') {
            while (i < n && src[i] !== '\n') ++i;
        } else if (c === '/' && d === '*') {
            i += 2;
            while (i < n && !(src[i] === '*' && src[i + 1] === '/')) {
                if (src[i] === '\n') out += '\n';
                ++i;
            }
            i += 2;
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

// Concatenated C++ string literals ("a" "b") -> their text; null when `s` is not only literals and whitespace.
function literalText(s) {
    const re = /\s*"((?:[^"\\]|\\.)*)"\s*/y;
    let text = '';
    let pos = 0;
    while (pos < s.length) {
        re.lastIndex = pos;
        const m = re.exec(s);
        if (!m) return null;
        text += m[1].replace(/\\(.)/g, '$1');
        pos = re.lastIndex;
    }
    return text;
}

// Every TEST_CASE( ... ) of a source: [{name, tags}] plus the count of calls whose arguments were not literals.
function extractCases(src) {
    const cases = [];
    let unparsed = 0;
    const re = /\bTEST_CASE\s*\(/g;
    let m;
    while ((m = re.exec(src)) !== null) {
        // Find the matching ')' and the top-level comma, skipping string literals.
        let i = re.lastIndex;
        let depth = 1;
        let comma = -1;
        while (i < src.length && depth > 0) {
            const c = src[i];
            if (c === '"') {
                ++i;
                while (i < src.length && src[i] !== '"') i += src[i] === '\\' ? 2 : 1;
            } else if (c === '(') {
                ++depth;
            } else if (c === ')') {
                --depth;
                if (depth === 0) break;
            } else if (c === ',' && depth === 1 && comma < 0) {
                comma = i;
            }
            ++i;
        }
        const args = src.slice(re.lastIndex, i);
        const nameArg = comma < 0 ? args : src.slice(re.lastIndex, comma);
        const tagArg = comma < 0 ? '""' : src.slice(comma + 1, i);
        const name = literalText(nameArg);
        const tags = literalText(tagArg);
        if (name === null || tags === null) {
            ++unparsed;
        } else {
            cases.push({ name, tags });
        }
        re.lastIndex = i + 1;
    }
    return { cases, unparsed };
}

function isHidden(tags) {
    return /\[\.[^\]]*\]/.test(tags) || tags.includes('[!hide]');
}

function main() {
    const { exe } = parseArgs(process.argv);
    for (const h of ROOT_HEADERS) {
        if (!fs.existsSync(h)) {
            process.stderr.write(`consumer-cases: ${h} not found\n`);
            process.exit(2);
        }
    }

    // 1. Include graph.
    const files = [...walk(DSP_TESTS, ['.h', '.hpp', '.cpp'], []), ...walk(DSP_INCLUDE, ['.h', '.hpp'], [])];
    const known = new Map(files.map((f) => [key(f), f]));
    const sources = new Map();
    const includedBy = new Map(); // key(header) -> Set(file)
    const incRe = /^[ \t]*#[ \t]*include[ \t]*(?:"([^"]+)"|<([^>]+)>)/gm;
    for (const f of files) {
        const src = fs.readFileSync(f, 'utf8');
        sources.set(f, src);
        const body = stripComments(src);
        let m;
        while ((m = incRe.exec(body)) !== null) {
            const target = resolveInclude(f, m[1] !== undefined ? m[1] : m[2], m[1] === undefined, known);
            if (!target) continue;
            const k = key(target);
            if (!includedBy.has(k)) includedBy.set(k, new Set());
            includedBy.get(k).add(f);
        }
    }

    // 2. Transitive reverse closure of every modified header.
    const closure = new Set();
    const queue = [...ROOT_HEADERS];
    const seen = new Set(ROOT_HEADERS.map(key));
    while (queue.length > 0) {
        const cur = queue.shift();
        for (const f of includedBy.get(key(cur)) || []) {
            if (seen.has(key(f))) continue;
            seen.add(key(f));
            closure.add(f);
            queue.push(f);
        }
    }
    const rel = (p) => path.relative(REPO, p).split(path.sep).join('/');
    const consumerTus = [...closure]
        .filter((f) => f.toLowerCase().endsWith('.cpp') && key(f).startsWith(key(DSP_TESTS) + path.sep))
        .filter((f) => !PHASE9_TUS.has(path.resolve(f)))
        .sort((a, b) => rel(a).localeCompare(rel(b)));
    const phase9InClosure = [...closure].filter((f) => PHASE9_TUS.has(path.resolve(f))).length;
    const headersInClosure = [...closure].filter((f) => !f.toLowerCase().endsWith('.cpp')).map(rel).sort();

    process.stderr.write(
        `consumer-cases: include closure of ${ROOT_HEADERS.length} modified header(s) [${ROOT_HEADERS.map(rel).join(' ')}]: ${closure.size} files ` +
            `(${headersInClosure.length} headers, ${consumerTus.length} non-Phase-9 TUs, ${phase9InClosure} Phase 9 TUs)\n`,
    );
    for (const h of headersInClosure) process.stderr.write(`  header ${h}\n`);

    // 3. [long] cases of the consumer TUs.
    let unparsedTotal = 0;
    const names = [];
    for (const f of consumerTus) {
        const { cases, unparsed } = extractCases(stripComments(sources.get(f)));
        unparsedTotal += unparsed;
        const long = cases.filter(
            (c) => c.tags.includes('[long]') && !EXCLUDED_TAGS.some((t) => c.tags.includes(t)) && !isHidden(c.tags),
        );
        process.stderr.write(`  TU ${rel(f)}: ${cases.length} cases, ${long.length} [long] shards` +
            (unparsed ? `, ${unparsed} UNPARSED TEST_CASE` : '') + '\n');
        for (const c of long) names.push(c.name);
    }
    const unique = [...new Set(names)].sort();
    if (unique.length !== names.length) {
        process.stderr.write(`consumer-cases: ${names.length - unique.length} duplicate TEST_CASE name(s) collapsed\n`);
    }

    // 4. Cross-check against the binary, which also reports each case's source file (the XML listing's SourceInfo):
    //    (a) every source-derived name must be listed; (b) every listed [long] case whose file is a consumer TU is
    //    added, which recovers the macro-generated TEST_CASEs (stringised names) a source scan cannot expand.
    let exitCode = 0;
    let finalNames = unique;
    if (fs.existsSync(exe)) {
        let listed;
        try {
            listed = execFileSync(exe, ['--list-tests', '[long]', '--reporter', 'xml'], {
                encoding: 'utf8',
                timeout: 120000,
                maxBuffer: 64 * 1024 * 1024,
                windowsHide: true,
            });
        } catch (e) {
            // Catch2 may return the listed count as the exit code; keep stdout when present.
            listed = e.stdout ? String(e.stdout) : null;
            if (listed === null) {
                process.stderr.write(`consumer-cases: RED: ${rel(exe)} --list-tests failed: ${e.message}\n`);
                process.exit(1);
            }
        }
        const unxml = (t) =>
            t.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&apos;/g, "'").replace(/&amp;/g, '&');
        const tagOf = (block, tag) => {
            const m = new RegExp('<' + tag + '>([\\s\\S]*?)</' + tag + '>').exec(block);
            return m ? unxml(m[1].trim()) : '';
        };
        const listedCases = [];
        for (const m of listed.matchAll(/<TestCase>([\s\S]*?)<\/TestCase>/g)) {
            listedCases.push({ name: tagOf(m[1], 'Name'), tags: tagOf(m[1], 'Tags'), file: tagOf(m[1], 'File') });
        }
        if (listedCases.length === 0) {
            process.stderr.write(`consumer-cases: RED: ${rel(exe)} --list-tests "[long]" listed no case\n`);
            process.exit(1);
        }
        const listedNames = new Set(listedCases.map((c) => c.name));
        let missing = unique.filter((n) => !listedNames.has(n));
        // Close (2026-10-21, T025-3): a consumer TU of another executable (dsp_processors_tests, dsp_effects_tests,
        // ...) lists its cases there; those names are resolved against the sibling binaries and emitted as
        // "<exe>::<name>" so suites.sh runs them through that binary.
        const otherExe = new Map();   // name -> exe basename without .exe
        if (missing.length > 0) {
            const dir = path.dirname(exe);
            for (const sib of ['dsp_processors_tests', 'dsp_effects_tests', 'dsp_primitives_tests', 'dsp_core_tests']) {
                const sibExe = path.join(dir, sib + '.exe');
                if (!fs.existsSync(sibExe) || missing.length === 0) continue;
                let out;
                try {
                    out = execFileSync(sibExe, ['--list-tests', '[long]', '--verbosity', 'quiet'], {
                        encoding: 'utf8', timeout: 120000, maxBuffer: 64 * 1024 * 1024, windowsHide: true,
                    });
                } catch (e) {
                    out = e.stdout ? String(e.stdout) : '';
                }
                const sibNames = new Set(out.split(/\r?\n/).map((l) => l.trim()).filter((l) => l.length > 0));
                for (const n of missing) if (sibNames.has(n)) otherExe.set(n, sib);
                missing = missing.filter((n) => !otherExe.has(n));
            }
            for (const [n, e] of otherExe) process.stderr.write(`  OTHER-EXE ${e} ${n}\n`);
        }
        if (missing.length > 0) {
            exitCode = 1;
            process.stderr.write(`consumer-cases: RED: ${missing.length} name(s) not in ${rel(exe)} --list-tests "[long]":\n`);
            for (const n of missing) process.stderr.write(`  MISSING ${n}\n`);
        }
        const consumerKeys = new Set(consumerTus.map(key));
        const fromBinary = listedCases.filter(
            (c) =>
                c.file !== '' &&
                consumerKeys.has(key(path.isAbsolute(c.file) ? c.file : path.join(REPO, c.file))) &&
                !EXCLUDED_TAGS.some((t) => c.tags.includes(t)) &&
                !isHidden(c.tags),
        );
        const sourceSet = new Set(unique);
        const added = [...new Set(fromBinary.map((c) => c.name).filter((n) => !sourceSet.has(n)))].sort();
        for (const n of added) process.stderr.write(`  BINARY-ONLY (macro-generated) ${n}\n`);
        finalNames = [...new Set([...unique, ...added])].map((n) => (otherExe.has(n) ? otherExe.get(n) + '::' + n : n)).sort();
        process.stderr.write(
            `consumer-cases: cross-check ${missing.length === 0 ? 'GREEN' : 'RED'}: ${unique.length} source names, ` +
                `${missing.length} missing from the binary; ${added.length} macro-generated name(s) added from the ` +
                `binary's SourceInfo (${listedCases.length} [long] cases listed)\n`,
        );
    } else if (unparsedTotal > 0) {
        exitCode = 1;
        process.stderr.write(
            `consumer-cases: RED: ${rel(exe)} does not exist and ${unparsedTotal} macro-generated TEST_CASE(s) cannot be ` +
                `expanded from the sources; build dsp_systems_tests first\n`,
        );
    } else {
        process.stderr.write(`consumer-cases: cross-check SKIPPED: ${rel(exe)} does not exist\n`);
    }

    // User ruling 2026-10-21 (close): the four long Vorago cases are ruled out of this close's consumer lane. The
    // three modified headers on Vorago's path (harmonic_oscillator_bank.h: the continuity pass skipped only when the
    // Phase 4 opt-in ran a coefficient writer, and Vorago never enables it; tanh_adaa.h: the previous sample's F1
    // kept, the same expression on the same inputs; true_peak_limiter.h: the look-ahead is default-off with a
    // bit-identity test for the off path) are bit-identical for Vorago, and vorago_tests plus the Vorago per-push
    // cases of dsp_systems_tests are green on the final bits; 4 h soaks cannot change a verdict a bit-identical
    // change cannot reach. The seven short Vorago cases stay.
    const RULED_OUT = new Set(['VoragoEngine_OvernightSoak', 'VoragoEngine_OvernightEvolution',
                               'VoragoEngine_ConfigurationFuzz', 'VoragoMacro_SweepAxes']);
    const ruledOut = finalNames.filter((n) => RULED_OUT.has(n));
    for (const n of ruledOut) process.stderr.write(`  RULED-OUT (user ruling 2026-10-21, bit-identical path) ${n}\n`);
    finalNames = finalNames.filter((n) => !RULED_OUT.has(n));
    process.stdout.write(finalNames.map((n) => n + '\n').join(''));
    process.stderr.write(`consumer-cases: ${finalNames.length} consumer [long] shard(s)\n`);
    process.exit(exitCode);
}

main();
