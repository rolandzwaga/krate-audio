#!/usr/bin/env node
// Phase 14 T030 — scope fence, ODR re-sweep, greps (FR-018, FR-019, non-goals).
//
//   node specs/profundum-phase14-morph-modulation/evidence/close/scope-fence.js
//
// 1. Scope: the union of `git diff --name-only 3dfc5b7d` and `git status --porcelain --untracked-files=all` must
//    match the tasks.md T030 allow-list: the ten dsp/ files of "Rules -> dsp/ fence", plugins/profundum/**,
//    tools/profundum_preset_defs.h, specs/profundum-phase14-morph-modulation/**,
//    specs/profundum-phase13-presets-release/evidence/coverage-matrix*, specs/Profundum-roadmap.md.
//    Excluded by name: .serena/project.yml, .claude/workflows/profundum-phase.js (pre-phase working-tree changes).
//    Deviation (recorded in run-log/T030.txt): `git diff --name-only 3dfc5b7d` compares the base with the working
//    tree, so it also lists the paths of the two non-phase commits the user made on the branch after the phase base
//    (NON_PHASE_COMMITS below; profundum-phase.js is among them). A path reached ONLY through those commits (in
//    `git diff 3dfc5b7d HEAD`, not in `git diff HEAD`, not untracked) is excluded and listed; a path those commits
//    touched that the working tree also changes is still fenced. Any other commit after the base is RED.
// 2. ODR: every name claimed in tasks.md "Rules -> ODR" is defined at most once (a claimed name the build did not
//    adopt has 0 definitions and is listed); no roadmap hazard name is defined on a phase line.
// 3. std::isnan|std::isinf|std::isfinite|std::fpclassify|infinity()|quiet_NaN() over the phase's new and changed
//    source lines (untracked source files whole; tracked files: the lines added since 3dfc5b7d) = 0 in code;
//    mentions inside comments are listed, not counted (the rule of tools/lint-nonfinite-symbols.js).
// 4. Invariants: static_assert(kMorphStateBytes == 201) present and morph_state.h untouched; kSoundRows 60 rows and
//    its declaration line unchanged against 3dfc5b7d; enum class ProfundumCall unchanged against 3dfc5b7d;
//    kCurrentStateVersion = 3; plugins/profundum/version.json unchanged.
// 5. node tools/lint-odr.js and node tools/lint-layers.js exit 0.
// Prints `SCOPE GREEN` or `SCOPE RED` as the last line; exit code 0 / 1.

'use strict';
const { execFileSync, spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const BASE = '3dfc5b7d';
const ROOT = path.resolve(__dirname, '..', '..', '..', '..');
const git = (args) => execFileSync('git', ['-c', 'core.quotepath=off', ...args], { cwd: ROOT, encoding: 'utf8',
    maxBuffer: 256 * 1024 * 1024, stdio: ['ignore', 'pipe', 'ignore'] });
const zsplit = (s) => s.split('\0').filter((x) => x.length > 0);

let red = 0;
const fail = (msg) => { red++; console.log(`  RED   ${msg}`); };
const pass = (msg) => console.log(`  ok    ${msg}`);

// ---------------------------------------------------------------- 1. scope
// Commits on the branch after the phase base that are not phase work (user commits, 2026-10-26).
const NON_PHASE_COMMITS = new Map([
    ['4fb6c248', 'build(tests): shard every test run, scope gates to real consumers, quiet pass for wall-clock tests'],
    ['e501b577', 'docs(disrumpo,innexus): record the update-check editor changes as unreleased'],
]);
const postBase = git(['rev-list', '--abbrev-commit', '--abbrev=8', `${BASE}..HEAD`]).split('\n').filter(Boolean);
console.log(`[1] Scope fence (base ${BASE}, HEAD ${git(['rev-parse', '--short=8', 'HEAD']).trim()})`);
for (const c of postBase) {
    const known = [...NON_PHASE_COMMITS.keys()].find((k) => c.startsWith(k) || k.startsWith(c));
    if (known) pass(`post-base commit ${c} is a named non-phase commit: ${NON_PHASE_COMMITS.get(known)}`);
    else fail(`post-base commit ${c} is not a named non-phase commit (a phase commit must be fenced by path)`);
}

const diffPaths = zsplit(git(['diff', '--name-only', '-z', BASE]));
const statusRaw = zsplit(git(['status', '--porcelain', '--untracked-files=all', '-z']));
const statusPaths = [];
for (let i = 0; i < statusRaw.length; i++) {
    const e = statusRaw[i];
    statusPaths.push(e.slice(3));
    if (e[0] === 'R' || e[0] === 'C') i++;  // rename / copy: the next entry is the source path
}
const untracked = new Set(statusRaw.filter((e) => e.startsWith('?? ')).map((e) => e.slice(3)));
const committedSinceBase = new Set(zsplit(git(['diff', '--name-only', '-z', BASE, 'HEAD'])));
const workingVsHead = new Set(zsplit(git(['diff', '--name-only', '-z', 'HEAD'])));
const changed = [...new Set([...diffPaths, ...statusPaths])].sort();

const dspFence = new Set([
    'dsp/include/krate/dsp/systems/profundum_morph_modulator.h',          // new
    'dsp/tests/unit/systems/profundum_morph_modulator_test.cpp',          // new
    'dsp/tests/unit/systems/profundum_engine_morphmod_test.cpp',          // new
    'dsp/include/krate/dsp/systems/profundum_engine.h',                   // append-only edits
    'dsp/include/krate/dsp/systems/profundum_engine_observer.h',
    'dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h',
    'dsp/tests/CMakeLists.txt',
    'dsp/tests/unit/systems/profundum_engine_perf_test.cpp',
    'dsp/tests/unit/systems/profundum_engine_display_test.cpp',
    'dsp/tests/unit/systems/profundum_voice_engine_helpers_test.cpp',
]);
const allowExact = new Set([...dspFence, 'tools/profundum_preset_defs.h', 'specs/Profundum-roadmap.md']);
const allowPrefix = ['plugins/profundum/', 'specs/profundum-phase14-morph-modulation/',
    'specs/profundum-phase13-presets-release/evidence/coverage-matrix'];
const excludedByName = new Set(['.serena/project.yml', '.claude/workflows/profundum-phase.js']);

console.log(`  ${changed.length} paths (git diff --name-only ${BASE} U git status --porcelain --untracked-files=all)`);
const outside = [];
const nonPhaseOnly = [];
const counts = { prefix: {}, excluded: [] };
const phasePaths = [];
for (const p of changed) {
    if (excludedByName.has(p)) { counts.excluded.push(p); continue; }
    if (committedSinceBase.has(p) && !workingVsHead.has(p) && !untracked.has(p)) { nonPhaseOnly.push(p); continue; }
    phasePaths.push(p);
    if (allowExact.has(p)) { pass(p); continue; }
    const pre = allowPrefix.find((a) => p.startsWith(a));
    if (pre) { counts.prefix[pre] = (counts.prefix[pre] || 0) + 1; continue; }
    outside.push(p);
}
for (const [pre, n] of Object.entries(counts.prefix)) pass(`${pre}${pre.endsWith('/') ? '**' : '*'} : ${n} paths`);
pass(`excluded by name: ${counts.excluded.length} (${counts.excluded.join(', ')})`);
pass(`reached only through the non-phase commits: ${nonPhaseOnly.length} paths (listed at the end)`);
for (const p of outside) fail(`outside the allow-list: ${p}`);
console.log(`  scope: ${outside.length} path(s) outside the allow-list`);

// ---------------------------------------------------------------- source corpus and helpers
const SRC_EXT = /\.(h|hpp|hh|inl|c|cc|cpp|cxx)$/;
const corpus = zsplit(git(['ls-files', '-co', '--exclude-standard', '-z', '--', 'dsp', 'plugins', 'tools', 'tests']))
    .filter((p) => SRC_EXT.test(p) && fs.existsSync(path.join(ROOT, p)));

// Replace comments with spaces (newlines kept so line numbers hold); `keepStrings` false also blanks literals.
function strip(text, keepStrings) {
    let out = '';
    let i = 0;
    const n = text.length;
    while (i < n) {
        const c = text[i];
        const d = text[i + 1];
        if (c === '/' && d === '/') {
            while (i < n && text[i] !== '\n') { out += ' '; i++; }
        } else if (c === '/' && d === '*') {
            out += '  '; i += 2;
            while (i < n && !(text[i] === '*' && text[i + 1] === '/')) { out += text[i] === '\n' ? '\n' : ' '; i++; }
            out += '  '; i += 2;
        } else if (c === 'R' && d === '"' && !/\w/.test(text[i - 1] || '')) {
            const open = text.indexOf('(', i + 2);
            const delim = ')' + text.slice(i + 2, open) + '"';
            const close = text.indexOf(delim, open);
            const end = close < 0 ? n : close + delim.length;
            for (; i < end; i++) out += keepStrings || text[i] === '\n' ? text[i] : ' ';
        } else if (c === '"' || (c === '\'' && !/[0-9A-Fa-f]/.test(text[i - 1] || ''))) {
            out += c; i++;
            while (i < n && text[i] !== c && text[i] !== '\n') {
                if (text[i] === '\\') { out += keepStrings ? text[i] + (text[i + 1] || '') : '  '; i += 2; continue; }
                out += keepStrings ? text[i] : ' '; i++;
            }
            if (i < n) { out += text[i]; i++; }
        } else { out += c; i++; }
    }
    return out;
}

const cache = new Map();
function load(p) {
    if (!cache.has(p)) {
        const raw = fs.readFileSync(path.join(ROOT, p), 'utf8').replace(/\r\n/g, '\n');
        cache.set(p, { raw, code: strip(raw, false) });
    }
    return cache.get(p);
}
const lineOf = (text, idx) => text.slice(0, idx).split('\n').length;

const KEYWORDS = new Set(['return', 'else', 'case', 'new', 'delete', 'throw', 'co_return', 'sizeof', 'decltype', 'do',
    'if', 'while', 'for', 'switch', 'goto', 'typedef', 'using', 'namespace', 'operator', 'friend', 'class', 'struct',
    'union', 'enum']);  // `class X;` / `friend class X;` are declarations, not variables
// The text before a match must end in a declarator-ish token: an identifier (not a keyword), `>`, `*` or a single `&`.
function typeishPrefix(code, idx) {
    let j = idx - 1;
    while (j >= 0 && /\s/.test(code[j])) j--;
    if (j < 0) return false;
    const ch = code[j];
    if (ch === '>') return code[j - 1] !== '-' && code[j - 1] !== '>';
    if (ch === '*') return true;
    if (ch === '&') return code[j - 1] !== '&' || /[\w>]/.test(code[j - 2] || '');  // `T&&` ok, `a && b` not
    if (!/\w/.test(ch)) return false;
    let k = j;
    while (k >= 0 && /\w/.test(code[k])) k--;
    return !KEYWORDS.has(code.slice(k + 1, j + 1));
}
// After the `(` of a function name: find the matching `)`, skip qualifiers / trailing return; `{` = definition.
function isFunctionBody(code, openIdx) {
    let depth = 0;
    let i = openIdx;
    for (; i < code.length; i++) {
        if (code[i] === '(') depth++;
        else if (code[i] === ')' && --depth === 0) break;
    }
    const rest = code.slice(i + 1, i + 400);
    const m = rest.match(/^\s*(?:(?:const|noexcept(?:\([^)]*\))?|override|final|mutable|&|&&)\s*)*(?:->\s*[\w:<>,\s*&]+?)?\s*(\{|:(?!:))/);
    return m !== null;
}

// Definitions of `name` (type / alias / function / variable) in one file's code; returns the 1-based lines.
function definitions(name, file) {
    const { code } = load(file);
    const hits = [];
    const add = (idx) => { const l = lineOf(code, idx); if (!hits.includes(l)) hits.push(l); };
    let re = new RegExp(`\\b(?:class|struct|union|enum(?:\\s+class|\\s+struct)?)\\s+(?:alignas\\([^)]*\\)\\s+)?${name}\\b(?!\\s*;)`, 'g');
    for (let m; (m = re.exec(code));) {
        if (/\bfriend\s+$/.test(code.slice(Math.max(0, m.index - 16), m.index))) continue;
        add(m.index);
    }
    re = new RegExp(`\\busing\\s+${name}\\s*=`, 'g');
    for (let m; (m = re.exec(code));) add(m.index);
    re = new RegExp(`(?<![\\w.>:~])((?:\\w+::)*)${name}\\s*\\(`, 'g');
    for (let m; (m = re.exec(code));) {
        if (typeishPrefix(code, m.index) && isFunctionBody(code, m.index + m[0].length - 1)) add(m.index);
    }
    re = new RegExp(`(?<![\\w.>:])${name}\\s*(=(?!=)|\\{|;|\\[)`, 'g');
    for (let m; (m = re.exec(code));) {
        if (typeishPrefix(code, m.index)) add(m.index);
    }
    return hits;
}

// Lines of `p` added since BASE (1-based, current file numbering); untracked or new files: every line.
function addedLines(p) {
    const total = load(p).raw.split('\n').length;
    if (untracked.has(p)) return new Set(Array.from({ length: total }, (_, i) => i + 1));
    const diff = git(['diff', '-U0', '--no-color', BASE, '--', p]);
    const set = new Set();
    for (const m of diff.matchAll(/^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@/gm)) {
        const start = Number(m[1]);
        const len = m[2] === undefined ? 1 : Number(m[2]);
        for (let l = start; l < start + len; l++) set.add(l);
    }
    return set;
}
const phaseSources = phasePaths.filter((p) => SRC_EXT.test(p) && fs.existsSync(path.join(ROOT, p)));

// ---------------------------------------------------------------- 2. ODR re-sweep
const claimed = ['ProfundumMorphModulator', 'MorphModulator', 'MorphModParams', 'MorphModConfig', 'MorphModSource',
    'MorphModSettings', 'MorphIndicatorSlider', 'MorphIndicatorSliderCreator', 'MorphModOverride',
    'morphModTrajectoryCell', 'morphModSourceCell', 'withMorphMod', 'handleMorphModParamChange',
    'registerMorphModParams', 'morphModConfigOf', 'kMorphModSourceLabels', 'kStateMorphModOffset'];
console.log(`\n[2] ODR re-sweep: ${claimed.length} claimed names (${corpus.length} source files under dsp/ plugins/ tools/ tests/, comments and literals stripped)`);
for (const name of claimed) {
    const word = new RegExp(`\\b${name}\\b`);
    const defs = [];
    let mentions = 0;
    for (const p of corpus) {
        if (!word.test(load(p).raw)) continue;
        mentions++;
        for (const l of definitions(name, p)) defs.push(`${p}:${l}`);
    }
    if (defs.length === 1) pass(`${name}: 1 definition ${defs[0]}`);
    else if (defs.length === 0) pass(`${name}: 0 definitions (claimed, not adopted as a C++ name; ${mentions} file(s) mention the word) - no ODR hazard`);
    else fail(`${name}: ${defs.length} definitions ${defs.join(' ')}`);
}
const hazards = ['CharacterProcessor', 'SubOscillator', 'StereoField', 'MorphEngine', 'MorphNode', 'TransientDetector',
    'HarmonicCloud', 'SpectralState', 'MacroMapper'];
let hazardHits = 0;
for (const p of phaseSources) {
    const lines = addedLines(p);
    for (const name of hazards) {
        for (const l of definitions(name, p)) {
            if (lines.has(l)) { hazardHits++; fail(`roadmap hazard name ${name} defined on a phase line ${p}:${l}`); }
        }
    }
}
if (hazardHits === 0) pass(`roadmap hazard names (${hazards.join(', ')}) defined on phase lines: 0`);

// ---------------------------------------------------------------- 3. non-finite symbol grep over phase lines
const fpRe = /std::isnan|std::isinf|std::isfinite|std::fpclassify|infinity\(\)|quiet_NaN\(\)/g;
console.log(`\n[3] ${fpRe.source} over the new and changed lines of ${phaseSources.length} phase source files (code counted; comment mentions listed, as tools/lint-nonfinite-symbols.js does)`);
let fpHits = 0;
let fpComments = 0;
let linesScanned = 0;
for (const p of phaseSources) {
    const lines = addedLines(p);
    const raw = load(p).raw.split('\n');
    const code = load(p).code.split('\n');
    for (const l of lines) {
        const text = raw[l - 1];
        if (text === undefined) continue;
        linesScanned++;
        const all = text.match(fpRe);
        if (!all) continue;
        const inCode = code[l - 1].match(fpRe) || [];
        if (inCode.length > 0) { fpHits += inCode.length; fail(`${p}:${l} ${inCode.join(' ')} | ${text.trim()}`); }
        if (all.length > inCode.length) {
            fpComments += all.length - inCode.length;
            console.log(`  note  comment mention (not counted) ${p}:${l} | ${text.trim()}`);
        }
    }
}
console.log(`  ${linesScanned} lines scanned; comment mentions: ${fpComments}`);
if (fpHits === 0) pass('code hits: 0');

// ---------------------------------------------------------------- 4. invariants
console.log('\n[4] Invariants against ' + BASE);
const showBase = (p) => git(['show', `${BASE}:${p}`]).replace(/\r\n/g, '\n');
{
    const p = 'dsp/include/krate/dsp/systems/morph_state.h';
    const present = /static_assert\(kMorphStateBytes == 201\);/.test(load(p).raw);
    const untouched = !changed.includes(p);
    const msg = `${p}: static_assert(kMorphStateBytes == 201) ${present ? 'present' : 'MISSING'}, file ${untouched ? 'untouched' : 'CHANGED'}`;
    if (present && untouched) pass(msg); else fail(msg);
}
{
    const p = 'plugins/profundum/src/parameters/sound_param_table.h';
    const re = /inline constexpr std::array<SoundParamRow, (\d+)> kSoundRows\b[^\n]*/;
    const now = load(p).raw.match(re);
    const then = showBase(p).match(re);
    const ok = now && then && now[1] === '60' && now[0] === then[0];
    const msg = `${p}: kSoundRows ${now ? now[1] : '?'} rows (base ${then ? then[1] : '?'}), declaration line ${now && then && now[0] === then[0] ? 'unchanged' : 'CHANGED'}`;
    if (ok) pass(msg); else fail(msg);
}
{
    const p = 'dsp/include/krate/dsp/systems/profundum_engine_observer.h';
    const re = /enum class ProfundumCall\b[^{]*\{[^}]*\};/;
    const norm = (s) => (s ? s[0].replace(/\s+/g, ' ') : null);
    const now = norm(load(p).raw.match(re));
    const then = norm(showBase(p).match(re));
    const n = now ? now.slice(now.indexOf('{') + 1, now.lastIndexOf('}')).split(',').length : 0;
    const msg = `${p}: enum class ProfundumCall ${now !== null && now === then ? `unchanged (${n} enumerators)` : 'CHANGED or missing'}`;
    if (now !== null && now === then) pass(msg); else fail(msg);
}
{
    const p = 'plugins/profundum/src/plugin_ids.h';
    const m = load(p).raw.match(/kCurrentStateVersion\s*=\s*(\d+)\s*;/);
    const msg = `${p}: kCurrentStateVersion = ${m ? m[1] : '?'}`;
    if (m && m[1] === '3') pass(msg); else fail(msg);
}
{
    const p = 'plugins/profundum/version.json';
    const unchanged = !changed.includes(p) && load(p).raw.replace(/\s+$/, '') === showBase(p).replace(/\s+$/, '');
    const ver = (load(p).raw.match(/"version"\s*:\s*"([^"]+)"/) || [])[1];
    const msg = `${p}: ${unchanged ? 'unchanged' : 'CHANGED'} (version ${ver})`;
    if (unchanged) pass(msg); else fail(msg);
}

// ---------------------------------------------------------------- 5. lints
console.log('\n[5] Lints');
for (const tool of ['tools/lint-odr.js', 'tools/lint-layers.js']) {
    const r = spawnSync(process.execPath, [tool], { cwd: ROOT, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
    const out = `${r.stdout || ''}${r.stderr || ''}`.trim().split('\n');
    const msg = `node ${tool}: exit ${r.status} | ${out[out.length - 1]}`;
    if (r.status === 0) pass(msg); else { fail(msg); for (const l of out.slice(-20)) console.log(`        ${l}`); }
}

console.log(`\nPaths reached only through the non-phase commits (${nonPhaseOnly.length}, not fenced):`);
for (const p of nonPhaseOnly) console.log(`        ${p}`);

console.log(`\nRED findings: ${red}`);
console.log(red === 0 ? 'SCOPE GREEN' : 'SCOPE RED');
process.exit(red === 0 ? 0 : 1);
