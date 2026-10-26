#!/usr/bin/env node
// Phase 12 scope fence (T035; FR-095, FR-096). Run from anywhere: node specs/profundum-phase12-ui/evidence/close/scope-fence.js
// Check 1: every path in (git diff --name-only BASE) U (untracked files) matches the allow-list.
// Check 2: git diff BASE -- dsp/include is append-only (no '-' lines) and every '+' line lies in a `public:` section.
// Prints offending paths and a last line `SCOPE GREEN` or `SCOPE RED`; exit code 0 / 1.
'use strict';
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const BASE = '40f6ebca';
const repo = execFileSync('git', ['rev-parse', '--show-toplevel'], { encoding: 'utf8' }).trim();
const git = (args) =>
    execFileSync('git', args, { cwd: repo, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'], maxBuffer: 64 << 20 });

const EXCLUDED = new Set(['.serena/project.yml']);
const ALLOWED_DSP_HEADERS = [
    'dsp/include/krate/dsp/systems/profundum_engine.h',
    'dsp/include/krate/dsp/systems/profundum_voice.h',
];
const ALLOWED_EXACT = new Set([
    ...ALLOWED_DSP_HEADERS,
    'dsp/tests/unit/systems/profundum_engine_display_test.cpp',
    'dsp/tests/CMakeLists.txt',
    'specs/Profundum-roadmap.md',
    // FR-095 allow-list addition (2026-10-24): behaviour-neutral test-only helpers, the listen-WAV ignore, the SC-035 verdicts.
    'dsp/tests/unit/processors/harmonic_fold_test_helpers.h',
    'dsp/tests/unit/processors/shape_character_metric_helpers.h',
    'dsp/tests/unit/processors/transient_shaper_test_helpers.h',
    'dsp/tests/unit/systems/anchor_oscillator_test_helpers.h',
    'dsp/tests/unit/systems/bass_morph_test_helpers.h',
    'dsp/tests/unit/systems/bass_stereo_field_test_helpers.h',
    'dsp/tests/unit/systems/mass_dynamics_test_helpers.h',
    'dsp/tests/unit/systems/profundum_core_test_helpers.h',
    'dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h',
    '.gitignore',
    'specs/profundum-phase9-voice-engine/compliance.md',
    'specs/profundum-phase9-voice-engine/evidence/listening-sheet.md',
]);
const ALLOWED_PREFIXES = ['plugins/profundum/', 'specs/profundum-phase12-ui/'];

const allowed = (p) => ALLOWED_EXACT.has(p) || ALLOWED_PREFIXES.some((pre) => p.startsWith(pre));

// ---- Check 1: the change set ----
const changed = new Set();
for (const line of git(['diff', '--name-only', BASE]).split('\n')) if (line.trim()) changed.add(line.trim());

// porcelain v1 -z: "XY path\0" ; renames / copies carry a second "orig\0" field.
const raw = git(['status', '--porcelain=v1', '-z', '--untracked-files=all']).split('\0');
for (let i = 0; i < raw.length; ++i) {
    const e = raw[i];
    if (e.length < 4) continue;
    const xy = e.slice(0, 2);
    changed.add(e.slice(3));
    if (xy[0] === 'R' || xy[0] === 'C') ++i;  // skip the original path of a rename / copy
}

const offending = [...changed].filter((p) => !EXCLUDED.has(p) && !allowed(p)).sort();
const shared = offending.filter((p) => p.startsWith('plugins/shared/'));
const otherDsp = offending.filter((p) => p.startsWith('dsp/'));
const rest = offending.filter((p) => !p.startsWith('plugins/shared/') && !p.startsWith('dsp/'));

console.log(`# Check 1: change set vs ${BASE} (tracked diff U untracked), ${changed.size} paths, ` +
            `${EXCLUDED.size} excluded by name (${[...EXCLUDED].join(', ')})`);
console.log(`plugins/shared/ paths: ${shared.length}`);
for (const p of shared) console.log(`  OFFENDING ${p}`);
console.log(`other dsp/ paths: ${otherDsp.length}`);
for (const p of otherDsp) console.log(`  OFFENDING ${p}`);
console.log(`other paths outside the allow-list: ${rest.length}`);
for (const p of rest) console.log(`  OFFENDING ${p}`);
const check1 = offending.length === 0;
console.log(`CHECK 1 ${check1 ? 'GREEN' : 'RED'} (${offending.length} offending)`);

// ---- Check 2: dsp/include append-only, inside public sections ----
const diff = git(['diff', '-U0', BASE, '--', 'dsp/include']).split('\n');
const problems = [];
const files = new Map();  // path -> added count
let file = null;
let newLine = 0;
let fileAccess = null;
const accessRe = /^\s*(public|private|protected)\s*:/;

// Forward scan: for each line, the access of the innermost enclosing class / struct / union at the line's start
// (null outside any class). Function bodies and other non-class braces inherit the class's access, so a member
// function body added in a public section counts as public; a nested class's own specifiers apply only inside it.
function accessByLine(lines) {
    const out = [];
    const stack = [];  // {isClass, access}
    let header = '';
    for (const raw of lines) {
        let cls = null;
        for (let k = stack.length - 1; k >= 0; --k) if (stack[k].isClass) { cls = stack[k]; break; }
        out.push(cls ? cls.access : null);
        const code = raw.replace(/\/\*.*?\*\//g, '').replace(/\/\/.*$/, '')
                        .replace(/"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*'/g, '""');
        const m = accessRe.exec(code);
        if (m && stack.length && stack[stack.length - 1].isClass) {
            stack[stack.length - 1].access = m[1];
            header = '';
            continue;
        }
        for (const ch of code) {
            if (ch === '{') {
                const isClass = /\b(class|struct|union)\b/.test(header) && !/\benum\b/.test(header);
                stack.push({ isClass, access: /\bclass\b/.test(header) ? 'private' : 'public' });
                header = '';
            } else if (ch === '}') {
                stack.pop();
                header = '';
            } else if (ch === ';') {
                header = '';
            } else {
                header += ch;
            }
        }
        header += ' ';
    }
    return out;
}
for (const l of diff) {
    if (l.startsWith('diff --git')) { file = null; continue; }
    if (l.startsWith('+++ ')) {
        file = l === '+++ /dev/null' ? null : l.slice(6);
        if (file === null) { problems.push('a dsp/include file was deleted'); continue; }
        files.set(file, 0);
        if (!ALLOWED_DSP_HEADERS.includes(file)) problems.push(`${file}: not one of the two allowed headers`);
        fileAccess = accessByLine(fs.readFileSync(path.join(repo, file), 'utf8').split('\n').map((s) => s.replace(/\r$/, '')));
        continue;
    }
    if (l.startsWith('--- ')) continue;
    const h = /^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@/.exec(l);
    if (h) { newLine = Number(h[1]); continue; }
    if (file === null) continue;
    if (l.startsWith('-')) { problems.push(`${file}: removed line "${l.slice(1).trim()}"`); continue; }
    if (l.startsWith('+')) {
        files.set(file, files.get(file) + 1);
        const text = l.slice(1);
        if (accessRe.test(text)) problems.push(`${file}:${newLine}: added access specifier "${text.trim()}"`);
        const spec = fileAccess[newLine - 1];
        if (spec !== 'public') problems.push(`${file}:${newLine}: added line in a ${spec ?? 'no'} section`);
        ++newLine;
    }
}
console.log(`# Check 2: git diff ${BASE} -- dsp/include (append-only, public sections)`);
for (const [f, n] of files) console.log(`${f}: ${n} added lines`);
for (const p of problems) console.log(`  OFFENDING ${p}`);
const check2 = problems.length === 0;
console.log(`CHECK 2 ${check2 ? 'GREEN' : 'RED'} (${problems.length} problems)`);

const green = check1 && check2;
console.log(`SCOPE ${green ? 'GREEN' : 'RED'}`);
process.exit(green ? 0 : 1);
