#!/usr/bin/env node
// Profundum Phase 9, task T014: paste the harvested macro rows and the SC-011 fingerprint (plan S10.5; tasks.md T014,
// deviations 6 and 12). Structure after specs/profundum-phase7-stereo-field/evidence/close/paste-harvest.js.
//
// Usage (from harvest.sh, or by hand after both harvest cases exited 0; never after a HARVEST RED):
//   node specs/profundum-phase9-voice-engine/evidence/close/paste-harvest.js [macro-rows] [fingerprint]
//   (no block named = both; --root <dir> overrides the repository root)
//
// macro-rows  : evidence/macro-rows.inc -> dsp/include/krate/dsp/systems/profundum_macro_matrix.h, the lines between
//               "// HARVEST-BEGIN macro-rows" and "// HARVEST-END macro-rows".
//               The .inc must be exactly: the kRows declaration line, 16 row lines in the plan S4.2 order (macro and
//               target per row as in the seed table below), "}};". Each amount is a finite float literal with an f
//               suffix whose sign equals the seed's and whose magnitude lies in [1, 4] x the seed's; each curve is
//               Linear, Exponential or SCurve (never Stepped, FR-050). Refused when evidence/macro-rows.txt carries a
//               "HARVEST RED" line.
// fingerprint : evidence/fingerprint.inc -> dsp/tests/unit/systems/profundum_engine_test.cpp, the lines between
//               "// HARVEST-BEGIN fingerprint" and "// HARVEST-END fingerprint". The .inc must hold
//               "constexpr bool kPeFingerprintHarvested = true;", the kPeFingerprints declaration, exactly 4 entries
//               (".rms = ") and only finite numbers.
// Both: each marker pair must occur exactly once; a "// harvested <date> from evidence/<file> (plan S10.5, T014)" line
// is put right after the BEGIN marker (a previous one is replaced, never stacked); the file's line endings are kept.
// Everything is validated before anything is written (all or nothing). Prints the removed / added lines.
// CommonJS, no dependencies.
'use strict';

const fs = require('fs');
const path = require('path');

const SPEC_EVIDENCE = 'specs/profundum-phase9-voice-engine/evidence';

// Plan S4.2 (FR-052): macro, target and seed amount per row, in order.
const SEED_ROWS = [
    ['Weight', 'RecipeDepth', 0.25],
    ['Weight', 'RecipeBody', 0.25],
    ['Weight', 'AnchorLevel', 0.25],
    ['DarkBright', 'RecipeEdge', 0.5],
    ['DarkBright', 'RecipeShift', 0.5],
    ['DarkBright', 'Shape', 0.0],   // ruling B10-2 (2026-10-20): dropped, amount stays 0
    ['DarkBright', 'CharacterTone', 0.3],
    ['TightLoose', 'AmpAttackOct', 2.0],
    ['TightLoose', 'AmpDecayOct', 2.0],
    ['TightLoose', 'AmpReleaseOct', 2.0],
    ['TightLoose', 'TransientAttackDb', -6.0],
    ['TightLoose', 'CompressionAmount', -0.5],
    ['CleanViolent', 'ExcitationMix', 0.5],
    ['CleanViolent', 'ExcitationDrive', 0.5],
    ['CleanViolent', 'FoldAmount', 50.0],
    ['CleanViolent', 'CharacterAmount', 0.5],
];
const MAX_SCALE = 4.0;           // the harvest's ceiling (tasks.md T014)
const SCALE_SLACK = 1e-6;        // float rounding of seed x k
const CURVES = new Set(['Linear', 'Exponential', 'SCurve']);

const FLOAT_F_RE = /^[-+]?(?:\d+\.\d*|\.\d+|\d+\.?\d*[eE][-+]?\d+|\d+\.\d*[eE][-+]?\d+)f$/;
const DECL_RE = /^\s*static constexpr std::array<ProfundumMacroRow, kNumTargets> kRows\{\{\s*$/;
const CLOSE_RE = /^\s*\}\};\s*$/;
const ROW_RE =
    /^\s*\{\.macro = ProfundumMacro::(\w+), \.target = ProfundumMacroTarget::(\w+), \.amount = (\S+), \.curve = ModCurve::(\w+)\},\s*$/;
const HARVESTED_RE = /^[ \t]*\/\/ harvested \S+ from evidence\/\S+ \(plan S10\.5, T014\)[ \t]*$/;

class PasteError extends Error {}

function fail(msg) {
    throw new PasteError(msg);
}

function readInc(file) {
    if (!fs.existsSync(file))
        fail(`${file}: not found (run the harvest first)`);
    const lines = fs.readFileSync(file, 'utf8').replace(/\r/g, '').split('\n');
    while (lines.length > 0 && lines[lines.length - 1].trim() === '')
        lines.pop();
    if (lines.length === 0)
        fail(`${file}: empty`);
    return lines;
}

function validateMacroRows(evidenceDir) {
    const txt = path.join(evidenceDir, 'macro-rows.txt');
    if (fs.existsSync(txt) && /^HARVEST RED/m.test(fs.readFileSync(txt, 'utf8')))
        fail(`${txt}: carries a HARVEST RED line; STOP AND SURFACE, do not paste`);
    const file = path.join(evidenceDir, 'macro-rows.inc');
    const lines = readInc(file);
    if (lines.length !== SEED_ROWS.length + 2)
        fail(`${file}: ${lines.length} lines, expected ${SEED_ROWS.length + 2} (declaration, 16 rows, "}};")`);
    if (!DECL_RE.test(lines[0]))
        fail(`${file}: line 1 is not the kRows declaration: "${lines[0].trim()}"`);
    if (!CLOSE_RE.test(lines[lines.length - 1]))
        fail(`${file}: the last line is not "}};": "${lines[lines.length - 1].trim()}"`);
    SEED_ROWS.forEach(([macro, target, seed], i) => {
        const line = lines[i + 1];
        const m = ROW_RE.exec(line);
        if (!m)
            fail(`${file} row ${i}: malformed: "${line.trim()}"`);
        if (m[1] !== macro || m[2] !== target)
            fail(`${file} row ${i}: ${m[1]} / ${m[2]}, expected ${macro} / ${target} (plan S4.2 order)`);
        if (!FLOAT_F_RE.test(m[3]))
            fail(`${file} row ${i}: amount "${m[3]}" is not a float literal with an f suffix`);
        const v = Number(m[3].slice(0, -1));
        if (!Number.isFinite(v))
            fail(`${file} row ${i}: non-finite amount "${m[3]}"`);
        if (seed === 0) {   // a dropped row (B10-2) must stay exactly 0
            if (v !== 0)
                fail(`${file} row ${i}: dropped row (seed 0) carries ${m[3]}`);
        } else {
            if (Math.sign(v) !== Math.sign(seed))
                fail(`${file} row ${i}: sign of ${m[3]} differs from the seed ${seed} (FR-052 directions are normative)`);
            const k = v / seed;
            if (k < 1 - SCALE_SLACK || k > MAX_SCALE + SCALE_SLACK)
                fail(`${file} row ${i}: ${m[3]} is ${k} x the seed ${seed}, outside [1, ${MAX_SCALE}]`);
        }
        if (!CURVES.has(m[4]))
            fail(`${file} row ${i}: curve ${m[4]} (allowed: ${[...CURVES].join(', ')})`);
    });
    return lines;
}

function validateFingerprint(evidenceDir) {
    const file = path.join(evidenceDir, 'fingerprint.inc');
    const lines = readInc(file);
    const text = lines.join('\n');
    if (lines[0].trim() !== 'constexpr bool kPeFingerprintHarvested = true;')
        fail(`${file}: line 1 must be "constexpr bool kPeFingerprintHarvested = true;"`);
    if (!/^constexpr std::array<PeFingerprint, kPeFingerprintCount> kPeFingerprints\{\{$/.test((lines[1] || '').trim()))
        fail(`${file}: line 2 is not the kPeFingerprints declaration`);
    if (lines[lines.length - 1].trim() !== '}};')
        fail(`${file}: the last line is not "}};"`);
    const entries = (text.match(/\.rms = /g) || []).length;
    if (entries !== 4)
        fail(`${file}: ${entries} fingerprint entries, expected 4 (MONO L / R, POLY L / R)`);
    if (/nan|inf/i.test(text.replace(/kPeFingerprint\w*|\/\/[^\n]*/g, '')))
        fail(`${file}: carries a non-finite value`);
    const numbers = text.replace(/\/\/[^\n]*/g, '').match(/[-+]?\d+\.\d+e[-+]\d+f?/g) || [];
    if (numbers.length !== 4 * (4 + 32))
        fail(`${file}: ${numbers.length} numeric literals, expected ${4 * (4 + 32)}`);
    for (const n of numbers) {
        if (!Number.isFinite(Number(n.replace(/f$/, ''))))
            fail(`${file}: non-finite literal ${n}`);
    }
    return lines;
}

const TARGETS = {
    'macro-rows': {
        header: 'dsp/include/krate/dsp/systems/profundum_macro_matrix.h',
        source: 'macro-rows.txt',
        validate: validateMacroRows,
    },
    fingerprint: {
        header: 'dsp/tests/unit/systems/profundum_engine_test.cpp',
        source: 'fingerprint.inc',
        validate: validateFingerprint,
    },
};

function escapeRe(s) {
    return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function replaceBlock(text, name, newBody, rel, source, isoDate) {
    const eol = text.includes('\r\n') ? '\r\n' : '\n';
    const n = escapeRe(name);
    const begins = [...text.matchAll(new RegExp(`^([ \\t]*)// HARVEST-BEGIN ${n}[ \\t]*\\r?$`, 'gm'))];
    const ends = [...text.matchAll(new RegExp(`^[ \\t]*// HARVEST-END ${n}[ \\t]*\\r?$`, 'gm'))];
    if (begins.length !== 1)
        fail(`${rel}: "// HARVEST-BEGIN ${name}" occurs ${begins.length} times, expected exactly 1`);
    if (ends.length !== 1)
        fail(`${rel}: "// HARVEST-END ${name}" occurs ${ends.length} times, expected exactly 1`);
    const indent = begins[0][1];
    const blockStart = text.indexOf('\n', begins[0].index) + 1;
    const blockEnd = ends[0].index;
    if (blockStart <= 0 || blockStart > blockEnd)
        fail(`${rel}: HARVEST-END ${name} does not follow HARVEST-BEGIN`);
    const oldLines = text.slice(blockStart, blockEnd).split(/\r?\n/);
    if (oldLines.length > 0 && oldLines[oldLines.length - 1] === '')
        oldLines.pop();
    const newLines = [`${indent}// harvested ${isoDate} from evidence/${source} (plan S10.5, T014)`, ...newBody];
    return {
        text: text.slice(0, blockStart) + newLines.join(eol) + eol + text.slice(blockEnd),
        oldLines: oldLines.filter((l) => !HARVESTED_RE.test(l)),
        newLines: newBody,
    };
}

function paste(root, names, isoDate) {
    const evidenceDir = path.join(root, SPEC_EVIDENCE);
    const writes = [];
    const report = [];
    for (const name of names) {
        const target = TARGETS[name];
        if (!target)
            fail(`unknown block "${name}"; expected one of: ${Object.keys(TARGETS).join(', ')}`);
        const body = target.validate(evidenceDir).map((l) => l.replace(/\s+$/, ''));
        const abs = path.join(root, target.header);
        if (!fs.existsSync(abs))
            fail(`${target.header}: not found`);
        const original = fs.readFileSync(abs, 'utf8');
        const r = replaceBlock(original, name, body, target.header, target.source, isoDate);
        const oldSet = new Set(r.oldLines);
        const newSet = new Set(r.newLines);
        report.push(`${target.header} [${name}]:`);
        r.oldLines.filter((l) => !newSet.has(l)).forEach((l) => report.push(`- ${l}`));
        r.newLines.filter((l) => !oldSet.has(l)).forEach((l) => report.push(`+ ${l}`));
        writes.push({ abs, rel: target.header, original, text: r.text });
    }
    for (const w of writes) {
        if (w.text !== w.original)
            fs.writeFileSync(w.abs, w.text);
        else
            report.push(`${w.rel}: unchanged`);
    }
    return report;
}

function main(argv) {
    let root = path.resolve(__dirname, '..', '..', '..', '..');
    const names = [];
    for (let i = 0; i < argv.length; ++i) {
        if (argv[i] === '--root') {
            if (i + 1 >= argv.length) {
                console.error('paste-harvest: --root needs a directory');
                return 1;
            }
            root = path.resolve(argv[++i]);
        } else {
            names.push(argv[i]);
        }
    }
    const blocks = names.length > 0 ? [...new Set(names)] : Object.keys(TARGETS);
    const isoDate = new Date().toISOString().slice(0, 10);
    try {
        for (const line of paste(root, blocks, isoDate))
            console.log(line);
        console.log(`paste-harvest: pasted ${blocks.join(', ')}`);
        return 0;
    } catch (err) {
        if (err instanceof PasteError) {
            console.error(`paste-harvest: ${err.message}`);
            return 1;
        }
        throw err;
    }
}

process.exitCode = main(process.argv.slice(2));
