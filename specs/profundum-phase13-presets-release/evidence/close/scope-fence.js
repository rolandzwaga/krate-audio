#!/usr/bin/env node
// Phase 13 T031 — scope fence, ODR re-sweep, greps (FR-057 scope, non-goals).
//
//   node specs/profundum-phase13-presets-release/evidence/close/scope-fence.js
//
// 1. Scope: the union of `git diff --name-only e6095b24` and `git status --porcelain --untracked-files=all` must
//    match the tasks.md T031 allow-list. Anything else (any dsp/include, dsp/src, other plugins/shared or other
//    plugin path, other spec folder) is RED. `.serena/project.yml` is excluded by name.
// 2. ODR: every claimed name of T001 step 4 is defined exactly once (comments stripped; per-name scope notes below).
// 3. Portability: std::isnan|isinf|isfinite|fpclassify in the code of the NEW files (added since e6095b24 or
//    untracked) = 0. Comment mentions are listed, not counted.
// 4. kMsgLoadPresetState appears in code only in plugin_ids.h, processor.cpp, controller.cpp and tests.
// Prints `SCOPE GREEN` or `SCOPE RED` as the last line; exit code 0 / 1.

'use strict';
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const BASE = 'e6095b24';
const ROOT = path.resolve(__dirname, '..', '..', '..', '..');
const git = (args) => execFileSync('git', ['-c', 'core.quotepath=off', ...args], { cwd: ROOT, encoding: 'utf8',
    maxBuffer: 64 * 1024 * 1024, stdio: ['ignore', 'pipe', 'ignore'] });
const zsplit = (s) => s.split('\0').filter((x) => x.length > 0);

let red = 0;
const fail = (msg) => { red++; console.log(`  RED   ${msg}`); };
const pass = (msg) => console.log(`  ok    ${msg}`);

// ---------------------------------------------------------------- 1. scope
const diffPaths = zsplit(git(['diff', '--name-only', '-z', BASE]));
const statusRaw = zsplit(git(['status', '--porcelain', '--untracked-files=all', '-z']));
const statusPaths = [];
for (let i = 0; i < statusRaw.length; i++) {
    const e = statusRaw[i];
    statusPaths.push(e.slice(3));
    if (e[0] === 'R' || e[0] === 'C') i++;  // rename / copy: the next entry is the source path
}
const untracked = statusRaw.filter((e) => e.startsWith('?? ')).map((e) => e.slice(3));
const added = zsplit(git(['diff', '--name-only', '-z', '--diff-filter=A', BASE]));
const changed = [...new Set([...diffPaths, ...statusPaths])].sort();

const allowExact = new Set([
    'tools/profundum_preset_defs.h',
    'tools/profundum_preset_generator.cpp',
    'CMakeLists.txt',
    'plugins/shared/src/preset/preset_manager.cpp',             // FR-065, 2026-10-24
    'plugins/shared/src/ui/preset_browser_view.h',
    'plugins/shared/src/ui/preset_browser_view.cpp',
    'plugins/shared/tests/test_preset_description.cpp',         // T037 test file (run-log/T037.txt:14)
    'plugins/shared/tests/CMakeLists.txt',
    'tools/check-preset-generator-determinism.js',
    'tools/ci-test-shard.js',
    'tools/ci-test-times.json',
    '.claude/skills/release/SKILL.md',
    'dsp/tests/unit/systems/profundum_engine_audibility_test.cpp',
    'dsp/tests/unit/systems/profundum_audibility_readers.h',
    'specs/Profundum-roadmap.md',
]);
const allowPrefix = ['plugins/profundum/', 'specs/profundum-phase13-presets-release/'];
const excludedByName = new Set(['.serena/project.yml']);

console.log(`[1] Scope fence: ${changed.length} paths (git diff --name-only ${BASE} U git status --porcelain --untracked-files=all)`);
const outside = [];
const counts = { prefix: {}, exact: 0, excluded: 0 };
for (const p of changed) {
    if (excludedByName.has(p)) { counts.excluded++; continue; }
    if (allowExact.has(p)) { counts.exact++; pass(p); continue; }
    const pre = allowPrefix.find((a) => p.startsWith(a));
    if (pre) { counts.prefix[pre] = (counts.prefix[pre] || 0) + 1; continue; }
    outside.push(p);
}
for (const [pre, n] of Object.entries(counts.prefix)) pass(`${pre}** : ${n} paths`);
console.log(`  ok    excluded by name (.serena/project.yml): ${counts.excluded}`);
for (const p of outside) fail(`outside the allow-list: ${p}`);
console.log(`  scope: ${outside.length} path(s) outside the allow-list`);

// ---------------------------------------------------------------- source corpus
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
        cache.set(p, { raw, code: strip(raw, false), withStrings: strip(raw, true) });
    }
    return cache.get(p);
}
const lineOf = (text, idx) => text.slice(0, idx).split('\n').length;

const KEYWORDS = new Set(['return', 'else', 'case', 'new', 'delete', 'throw', 'co_return', 'sizeof', 'decltype', 'do',
    'if', 'while', 'for', 'switch', 'goto', 'typedef', 'using', 'namespace', 'operator']);
// The text before a match must end in a declarator-ish token: an identifier (not a keyword), `>`, `*` or a single `&`.
function typeishPrefix(code, idx) {
    let j = idx - 1;
    while (j >= 0 && /\s/.test(code[j])) j--;
    if (j < 0) return false;
    const ch = code[j];
    if (ch === '>' ) return code[j - 1] !== '-' && code[j - 1] !== '>';
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

// Definitions of `name` in one file's code; returns the 1-based lines.
function definitions(name, kind, file) {
    const { code, withStrings } = load(file);
    const hits = [];
    const id = name.replace(/[$]/g, '\\$');
    const add = (idx) => { const l = lineOf(code, idx); if (!hits.includes(l)) hits.push(l); };
    let re;
    if (kind === 'namespace') {
        re = new RegExp(`(?<!\\busing\\s+)\\bnamespace\\s+(?:[\\w]+::)*${id}\\b(?!\\s*=)`, 'g');  // not `using namespace`
        for (let m; (m = re.exec(code));) add(m.index);
        return hits.length > 0 ? [hits[0]] : [];  // a namespace is "defined" by the file that opens it
    }
    if (kind === 'string') {
        re = new RegExp(`"${id}"`, 'g');
        for (let m; (m = re.exec(withStrings));) add(m.index);
        return hits;
    }
    // type
    re = new RegExp(`\\b(?:class|struct|union|enum(?:\\s+class|\\s+struct)?)\\s+(?:alignas\\([^)]*\\)\\s+)?${id}\\b(?!\\s*;)`, 'g');
    for (let m; (m = re.exec(code));) add(m.index);
    // alias
    re = new RegExp(`\\busing\\s+${id}\\s*=`, 'g');
    for (let m; (m = re.exec(code));) add(m.index);
    // function (free, member in-class, or out-of-line `Cls::name`)
    re = new RegExp(`(?<![\\w.>:])((?:\\w+::)*)${id}\\s*\\(`, 'g');
    for (let m; (m = re.exec(code));) {
        if (code[m.index - 1] === '~') continue;
        if (code.slice(Math.max(0, m.index - 2), m.index) === '->') continue;
        if (typeishPrefix(code, m.index) && isFunctionBody(code, m.index + m[0].length - 1)) add(m.index);
    }
    // variable / constant / data member: `T name = …;`, `T name{…};`, `T name;`, `auto name = [](…)`
    re = new RegExp(`(?<![\\w.>:])${id}\\s*(=(?!=)|\\{|;|\\[)`, 'g');
    for (let m; (m = re.exec(code));) {
        if (!typeishPrefix(code, m.index)) continue;
        // `x = …` statements have an operator or `(`/`,` before them; typeishPrefix already rejects those.
        // Reject `case`/`return` handled by KEYWORDS; reject `Cls name{` where name is the ctor (type kind catches it).
        add(m.index);
    }
    return hits;
}

// ---------------------------------------------------------------- 2. ODR re-sweep
// [name, kind, scopeFilter, expectation note]. kind: 'symbol' (type / alias / function / variable), 'namespace',
// 'string', 'file'. scopeFilter limits the counted files where T001 ruled the outside hits not an ODR hazard.
const inProfundumPlugin = (p) => p.startsWith('plugins/profundum/');
const notVorago = (p) => !p.startsWith('plugins/vorago/');
const claimed = [
    // §4.1 lift table L1-L11 and shim
    ...['auCellOnsetESub', 'auCellAt13', 'auCellAt17', 'auDropCell', 'auCellAt23', 'auCellAt24', 'auCellAt25', 'auDlk',
        'auCellAt27', 'auOnsetDyn', 'auCellAt28', 'auNoteMemoryPhase', 'AuNoteMemoryRow', 'auNoteMemoryRows',
        'auCellNoteMemory', 'auMacroAxisRead', 'auMacroCellOn'].map((n) => [n, 'symbol']),
    ['ProfundumAudibility', 'namespace'],
    ['ScriptRenderHook', 'symbol'], ['ScriptRenderScope', 'symbol'], ['ScriptRenderFn', 'symbol'],
    ['scriptRenderHook', 'symbol'],
    ['profundum_audibility_readers', 'file'],
    // §4.3
    ['ProfundumPresetDef', 'symbol'], ['CapabilityInfo', 'symbol'], ['kFactoryDefaultSeed', 'symbol'],
    // §4.4 (VoragoTest::PresetHost is the one expected outside hit, checked separately)
    ['PresetHost', 'symbol', notVorago], ['writeFactoryTree', 'symbol'],
    // §4.6 (openPresetBrowser / presetBrowserView_ are per-Controller class members in every plugin: T001)
    ['kMsgLoadPresetState', 'symbol'], ['ProfundumLoadPresetState', 'string'], ['setEngineSeamsForTest', 'symbol'],
    ['engineSeamsForTest_', 'symbol'], ['kPresetBrowserTag', 'symbol'], ['loadPresetStream', 'symbol'],
    ['openPresetBrowser', 'symbol', inProfundumPlugin], ['makeProfundumPresetTabLabels', 'symbol'],
    ['presetBrowserView_', 'symbol', inProfundumPlugin],
    // §4.7
    ...['PresetArmContext', 'renderScriptThroughProcessor', 'effectiveScriptFor', 'processorRenderHook',
        'PresetRenderReadings', 'ShowcaseArm', 'PresetSweepObserver', 'presetFloatIsFinite', 'fundamentalLossFlagged',
        'presetReadings', 'showcaseColumn', 'describePreset', 'presetDistance'].map((n) => [n, 'symbol']),
    // §8
    ['PresetHostComponentHandler', 'symbol'],
];

console.log(`\n[2] ODR re-sweep: ${claimed.length} claimed names, defined exactly once (${corpus.length} source files under dsp/ plugins/ tools/ tests/, comments stripped)`);
for (const [name, kind, filter] of claimed) {
    let defs = [];
    if (kind === 'file') {
        defs = corpus.filter((p) => path.basename(p).replace(/\.[^.]+$/, '') === name).map((p) => `${p}`);
    } else {
        const word = new RegExp(`\\b${name}\\b`);
        for (const p of corpus) {
            if (filter && !filter(p)) continue;
            if (!word.test(load(p).raw)) continue;
            for (const l of definitions(name, kind === 'symbol' ? 'symbol' : kind, p)) defs.push(`${p}:${l}`);
        }
    }
    const msg = `${name} (${kind}${filter ? ', scoped' : ''}): ${defs.length} definition(s) ${defs.join(' ')}`;
    if (defs.length === 1) pass(msg); else fail(msg);
}
{   // the expected outside hit: VoragoTest::PresetHost, exactly one definition
    const vor = [];
    for (const p of corpus.filter((x) => x.startsWith('plugins/vorago/') && /\bPresetHost\b/.test(load(x).raw))) {
        for (const l of definitions('PresetHost', 'symbol', p)) vor.push(`${p}:${l}`);
    }
    const msg = `PresetHost under plugins/vorago (expected VoragoTest::PresetHost only): ${vor.length} ${vor.join(' ')}`;
    if (vor.length === 1) pass(msg); else fail(msg);
}

// ---------------------------------------------------------------- 3. portability greps over the new files
const newFiles = [...new Set([...added, ...untracked])].filter((p) => SRC_EXT.test(p) && fs.existsSync(path.join(ROOT, p))).sort();
console.log(`\n[3] std::isnan|std::isinf|std::isfinite|std::fpclassify over ${newFiles.length} new source files (code only)`);
const fpRe = /std::(isnan|isinf|isfinite|fpclassify)\b/g;
let fpCode = 0;
for (const p of newFiles) {
    const { raw, code } = load(p);
    for (let m; (m = fpRe.exec(code));) { fpCode++; fail(`${p}:${lineOf(code, m.index)} ${m[0]}`); }
    let comments = 0;
    for (let m; (m = fpRe.exec(raw));) comments++;
    if (comments > 0) console.log(`  note  ${p}: ${comments} mention(s) in total, ${comments - (code.match(fpRe) || []).length} in comments (not counted)`);
}
for (const p of newFiles) console.log(`        new: ${p}`);
if (fpCode === 0) pass('code hits: 0');

// ---------------------------------------------------------------- 4. kMsgLoadPresetState users
console.log('\n[4] kMsgLoadPresetState in code: only plugin_ids.h, processor.cpp, controller.cpp and tests');
const msgAllowed = new Set(['plugins/profundum/src/plugin_ids.h', 'plugins/profundum/src/processor/processor.cpp',
    'plugins/profundum/src/controller/controller.cpp']);
for (const p of corpus) {
    const { raw, code } = load(p);
    if (!raw.includes('kMsgLoadPresetState')) continue;
    const n = (code.match(/\bkMsgLoadPresetState\b/g) || []).length;
    if (n === 0) { console.log(`  note  ${p}: comments only`); continue; }
    const isTest = p.startsWith('plugins/profundum/tests/');
    const msg = `${p}: ${n} code use(s)`;
    if (msgAllowed.has(p) || isTest) pass(msg); else fail(msg);
}

console.log(`\nRED findings: ${red}`);
console.log(red === 0 ? 'SCOPE GREEN' : 'SCOPE RED');
process.exit(red === 0 ? 0 : 1);
