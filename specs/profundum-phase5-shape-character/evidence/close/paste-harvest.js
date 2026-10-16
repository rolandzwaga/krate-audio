#!/usr/bin/env node
// Profundum Phase 5, task T024 close lane: paste the harvested tables into the headers (plan T030 / T040; C-4, C-5, C-6).
//
// Usage (after harvest.sh exited 0; never after a STOP AND SURFACE):
//   node specs/profundum-phase5-shape-character/evidence/close/paste-harvest.js
//   node specs/profundum-phase5-shape-character/evidence/close/paste-harvest.js --self-test
//   (--root <dir> overrides the repository root; the self-test uses it on synthetic copies)
//
// Reads evidence/harvest-loudness.txt and evidence/harvest-rlaw.txt ("HARVEST <name> {...}" lines) and replaces the text
// between "// HARVEST-BEGIN <name>" and "// HARVEST-END <name>" in
//   dsp/include/krate/dsp/systems/shape_morph.h          (kLoudnessTableDb, 101 floats)
//   dsp/include/krate/dsp/systems/bass_character_stage.h (kRectifyFlipEdges, kFractureFlipEdges, 5 {lo, hi} each)
// with the harvested initialiser. Exit 1 (nothing written) unless every marker pair occurs exactly once, every value
// parses, every count matches, every edge has lo <= hi, and no harvest file carries a STOP AND SURFACE line.
// Prints a diff summary per table. CommonJS, no dependencies.
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

const SPEC_EVIDENCE = 'specs/profundum-phase5-shape-character/evidence';
const SHAPE_MORPH_H = 'dsp/include/krate/dsp/systems/shape_morph.h';
const CHARACTER_H = 'dsp/include/krate/dsp/systems/bass_character_stage.h';

const TARGETS = [
    { name: 'kLoudnessTableDb', header: SHAPE_MORPH_H, source: 'harvest-loudness.txt', kind: 'floats', count: 101 },
    { name: 'kRectifyFlipEdges', header: CHARACTER_H, source: 'harvest-rlaw.txt', kind: 'edges', count: 5 },
    { name: 'kFractureFlipEdges', header: CHARACTER_H, source: 'harvest-rlaw.txt', kind: 'edges', count: 5 },
];

const NUM_RE = /^-?\d+(?:\.\d+)?$/;

class PasteError extends Error {}

function fail(msg) {
    throw new PasteError(msg);
}

function parseNumber(text, where) {
    const t = text.trim();
    if (!NUM_RE.test(t))
        fail(`${where}: malformed value "${t}"`);
    const v = Number(t);
    if (!Number.isFinite(v))
        fail(`${where}: non-finite value "${t}"`);
    return v;
}

function outerList(body, target) {
    const m = /^\{(.*)\}$/.exec(body);
    if (!m)
        fail(`${target.name}: initialiser is not a single {...} list`);
    return m[1].trim();
}

function parseFloats(body, target) {
    const values = outerList(body, target).split(',').map((s, i) => parseNumber(s, `${target.name}[${i}]`));
    if (values.length !== target.count)
        fail(`${target.name}: ${values.length} values, expected ${target.count}`);
    return values;
}

function parseEdges(body, target) {
    const inner = outerList(body, target);
    const pairRe = /\{([^{}]*)\}/g;
    const edges = [];
    for (const p of inner.matchAll(pairRe)) {
        const where = `${target.name}[${edges.length}]`;
        const parts = p[1].split(',');
        if (parts.length !== 2)
            fail(`${where}: expected {lo, hi}, got "{${p[1]}}"`);
        const lo = parseNumber(parts[0], `${where}.lo`);
        const hi = parseNumber(parts[1], `${where}.hi`);
        if (lo > hi)
            fail(`${where}: empty band, lo ${lo} > hi ${hi}`);
        edges.push({ lo, hi });
    }
    // Everything outside the {lo, hi} groups must be separators only.
    if (inner.replace(pairRe, '').replace(/[\s,]/g, '') !== '')
        fail(`${target.name}: stray text in initialiser "${inner}"`);
    if (edges.length !== target.count)
        fail(`${target.name}: ${edges.length} edge pairs, expected ${target.count}`);
    return edges;
}

function readHarvest(evidenceDir, target, cache) {
    const file = path.join(evidenceDir, target.source);
    if (!cache.has(file)) {
        if (!fs.existsSync(file))
            fail(`${file}: not found (run harvest.sh first)`);
        const text = fs.readFileSync(file, 'utf8').replace(/\r/g, '');
        if (text.includes('STOP AND SURFACE'))
            fail(`${file}: contains a STOP AND SURFACE line; return to the user, do not paste`);
        cache.set(file, text);
    }
    const prefix = `HARVEST ${target.name} `;
    const lines = cache.get(file).split('\n').filter((l) => l.startsWith(prefix));
    if (lines.length !== 1)
        fail(`${file}: "HARVEST ${target.name}" occurs ${lines.length} times, expected exactly 1`);
    const body = lines[0].slice(prefix.length).trim();
    return target.kind === 'floats' ? parseFloats(body, target) : parseEdges(body, target);
}

function fmt(v, precision) {
    // Edges are exact k/128 bisection values (8 decimals, close 2026-10-16); the loudness harvest prints 4.
    return `${v.toFixed(precision)}f`;
}

function render(target, values, indent, eol) {
    const inner = `${indent}    `;
    const out = [];
    if (target.kind === 'floats') {
        out.push(`${indent}static constexpr std::array<float, ${target.count}> ${target.name}{`);
        for (let i = 0; i < values.length; i += 10) {
            const row = values.slice(i, i + 10).map((v) => fmt(v, 4)).join(', ');
            out.push(`${inner}${row}${i + 10 < values.length ? ',' : '};'}`);
        }
    } else {
        out.push(`${indent}static constexpr std::array<FlipEdges, ${target.count}> ${target.name}{{`);
        for (const e of values)
            out.push(`${inner}{.lo = ${fmt(e.lo, 8)}, .hi = ${fmt(e.hi, 8)}},`);
        out.push(`${indent}}};`);
    }
    return out.join(eol) + eol;
}

// Old values between the markers, for the diff summary only (the replacement does not depend on it).
function oldNumbers(block) {
    const nums = block.match(/-?\d+(?:\.\d+)?(?=f\b)/g) || [];
    return nums.map(Number);
}

function escapeRe(s) {
    return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function replaceBlock(text, target, newValues, relHeader) {
    const eol = text.includes('\r\n') ? '\r\n' : '\n';
    const name = escapeRe(target.name);
    const beginRe = new RegExp(`^([ \\t]*)// HARVEST-BEGIN ${name}[ \\t]*\\r?$`, 'gm');
    const endRe = new RegExp(`^[ \\t]*// HARVEST-END ${name}[ \\t]*\\r?$`, 'gm');
    const begins = [...text.matchAll(beginRe)];
    const ends = [...text.matchAll(endRe)];
    if (begins.length !== 1)
        fail(`${relHeader}: "// HARVEST-BEGIN ${target.name}" occurs ${begins.length} times, expected exactly 1`);
    if (ends.length !== 1)
        fail(`${relHeader}: "// HARVEST-END ${target.name}" occurs ${ends.length} times, expected exactly 1`);
    const b = begins[0];
    const e = ends[0];
    const blockStart = text.indexOf('\n', b.index) + 1;
    const blockEnd = e.index;
    if (blockStart <= 0 || blockStart > blockEnd)
        fail(`${relHeader}: HARVEST-END ${target.name} does not follow HARVEST-BEGIN`);
    const oldBlock = text.slice(blockStart, blockEnd);
    const newBlock = render(target, newValues, b[1], eol);
    return { text: text.slice(0, blockStart) + newBlock + text.slice(blockEnd), oldBlock };
}

function summary(target, oldBlock, values) {
    const flat = target.kind === 'floats' ? values : values.flatMap((e) => [e.lo, e.hi]);
    const old = oldNumbers(oldBlock);
    if (old.length !== flat.length)
        return `${target.name}: replaced (${old.length} old values -> ${flat.length} new values)`;
    let changed = 0;
    let maxDelta = 0;
    let maxAt = -1;
    flat.forEach((v, i) => {
        const d = Math.abs(v - old[i]);
        if (d > 5e-5)
            changed++;
        if (d > maxDelta) {
            maxDelta = d;
            maxAt = i;
        }
    });
    let where = '';
    if (maxAt >= 0) {
        const idx = target.kind === 'floats' ? `${maxAt}` : `${Math.floor(maxAt / 2)}.${maxAt % 2 ? 'hi' : 'lo'}`;
        where = ` (at ${idx}: ${old[maxAt]} -> ${flat[maxAt].toFixed(8)})`;
    }
    return `${target.name}: ${changed}/${flat.length} values changed, max |delta| ${maxDelta.toFixed(8)}${where}`;
}

function paste(root) {
    const evidenceDir = path.join(root, SPEC_EVIDENCE);
    const cache = new Map();
    const headers = new Map();
    const lines = [];
    for (const target of TARGETS) {
        const values = readHarvest(evidenceDir, target, cache);
        const abs = path.join(root, target.header);
        if (!headers.has(abs)) {
            if (!fs.existsSync(abs))
                fail(`${target.header}: not found`);
            const original = fs.readFileSync(abs, 'utf8');
            headers.set(abs, { rel: target.header, original, text: original });
        }
        const h = headers.get(abs);
        const r = replaceBlock(h.text, target, values, h.rel);
        h.text = r.text;
        lines.push(`${h.rel}: ${summary(target, r.oldBlock, values)}`);
    }
    // Every target validated: only now write (all or nothing).
    for (const [abs, h] of headers) {
        if (h.text !== h.original)
            fs.writeFileSync(abs, h.text);
        else
            lines.push(`${h.rel}: unchanged`);
    }
    return lines;
}

// ---------------------------------------------------------------------------------------------------------------------
// --self-test: synthetic copies in a temp directory; checks the exit codes of good files and of each failure mode.

const SYN_SHAPE = [
    '#pragma once',
    'class ShapeMorph {',
    '    // HARVEST-BEGIN kLoudnessTableDb',
    '    static constexpr std::array<float, 101> kLoudnessTableDb{',
    `        ${Array.from({ length: 101 }, () => '1.0000f').join(', ')}};`,
    '    // HARVEST-END kLoudnessTableDb',
    '};',
    '',
].join('\n');

function synEdges(name) {
    return [
        `    // HARVEST-BEGIN ${name}`,
        `    static constexpr std::array<FlipEdges, 5> ${name}{{`,
        ...Array.from({ length: 5 }, () => '        {.lo = 0.5000f, .hi = 0.6000f},'),
        '    }};',
        `    // HARVEST-END ${name}`,
    ];
}

const SYN_CHARACTER = [
    '#pragma once',
    'class BassCharacterStage {',
    ...synEdges('kRectifyFlipEdges'),
    '',
    ...synEdges('kFractureFlipEdges'),
    '};',
    '',
].join('\n');

const SYN_LOUD = `HARVEST kLoudnessTableDb {${Array.from({ length: 101 }, (_, i) => (1 + i / 100).toFixed(4)).join(', ')}}\n`;
const SYN_RLAW = [
    'HARVEST kRectifyFlipEdges {{0.8398, 0.9102}, {0.8320, 0.9023}, {0.7852, 0.8711}, {0.7305, 0.8320}, {0.7227, 0.8242}}',
    'HARVEST kFractureFlipEdges {{0.1914, 0.3008}, {0.2148, 0.3086}, {0.3320, 0.3398}, {0.3492, 0.4023}, {0.3727, 0.4180}}',
    '',
    'algorithm,fixture,note,tone,a_lo,a_hi',
    'Rectify,sine,E0,0,0.8242,0.9102',
    '',
].join('\n');

const LOUD_TXT = `${SPEC_EVIDENCE}/harvest-loudness.txt`;
const RLAW_TXT = `${SPEC_EVIDENCE}/harvest-rlaw.txt`;

function makeTree(dir, files) {
    for (const [rel, content] of Object.entries(files)) {
        const abs = path.join(dir, rel);
        fs.mkdirSync(path.dirname(abs), { recursive: true });
        fs.writeFileSync(abs, content);
    }
}

function runOn(dir) {
    return spawnSync(process.execPath, [__filename, '--root', dir], { encoding: 'utf8' });
}

function verifyGood(dir, files) {
    const shape = fs.readFileSync(path.join(dir, SHAPE_MORPH_H), 'utf8');
    const ch = fs.readFileSync(path.join(dir, CHARACTER_H), 'utf8');
    if (!shape.includes('1.0000f, 1.0100f') || !shape.includes('2.0000f};'))
        return 'loudness values not pasted';
    if (!ch.includes('{.lo = 0.83980000f, .hi = 0.91020000f},') || !ch.includes('{.lo = 0.37270000f, .hi = 0.41800000f},'))
        return 'edge values not pasted';
    if ((shape.match(/HARVEST-BEGIN/g) || []).length !== 1 || (ch.match(/HARVEST-END/g) || []).length !== 2)
        return 'markers not preserved';
    if (files[SHAPE_MORPH_H].includes('\r\n') && /[^\r]\n/.test(shape + ch))
        return 'CRLF line endings not preserved';
    // A second run on its own output is a no-op (idempotent).
    const again = runOn(dir);
    if (again.status !== 0 || fs.readFileSync(path.join(dir, SHAPE_MORPH_H), 'utf8') !== shape
        || fs.readFileSync(path.join(dir, CHARACTER_H), 'utf8') !== ch)
        return 'second run not idempotent';
    return '';
}

function selfTest() {
    const base = fs.mkdtempSync(path.join(os.tmpdir(), 'paste-harvest-'));
    const good = { [SHAPE_MORPH_H]: SYN_SHAPE, [CHARACTER_H]: SYN_CHARACTER, [LOUD_TXT]: SYN_LOUD, [RLAW_TXT]: SYN_RLAW };
    const crlf = { ...good, [SHAPE_MORPH_H]: SYN_SHAPE.replace(/\n/g, '\r\n'), [CHARACTER_H]: SYN_CHARACTER.replace(/\n/g, '\r\n') };
    const cases = [
        { name: 'good files', files: good, expect: 0 },
        { name: 'good files, CRLF headers', files: crlf, expect: 0 },
        { name: 'missing HARVEST-END marker',
          files: { ...good, [CHARACTER_H]: SYN_CHARACTER.replace('    // HARVEST-END kFractureFlipEdges\n', '') }, expect: 1 },
        { name: 'missing HARVEST-BEGIN marker',
          files: { ...good, [SHAPE_MORPH_H]: SYN_SHAPE.replace('// HARVEST-BEGIN kLoudnessTableDb', '// kLoudnessTableDb') }, expect: 1 },
        { name: 'duplicate marker pair',
          files: { ...good, [CHARACTER_H]: SYN_CHARACTER.replace(/\};\n$/, `${synEdges('kRectifyFlipEdges').join('\n')}\n};\n`) }, expect: 1 },
        { name: 'missing HARVEST line', files: { ...good, [LOUD_TXT]: '\n' }, expect: 1 },
        { name: 'malformed loudness value', files: { ...good, [LOUD_TXT]: SYN_LOUD.replace('1.5000', '1.5O00') }, expect: 1 },
        { name: 'malformed edge value', files: { ...good, [RLAW_TXT]: SYN_RLAW.replace('0.7852', 'nan') }, expect: 1 },
        { name: 'wrong loudness count', files: { ...good, [LOUD_TXT]: SYN_LOUD.replace(', 2.0000}', '}') }, expect: 1 },
        { name: 'empty edge band (lo > hi)', files: { ...good, [RLAW_TXT]: SYN_RLAW.replace('{0.3492, 0.4023}', '{0.4492, 0.4023}') }, expect: 1 },
        { name: 'STOP AND SURFACE in harvest', files: { ...good, [RLAW_TXT]: `${SYN_RLAW}\nSTOP AND SURFACE (R-6): synthetic\n` }, expect: 1 },
    ];
    let failures = 0;
    cases.forEach((c, i) => {
        const dir = path.join(base, `case${i}`);
        makeTree(dir, c.files);
        const before = [SHAPE_MORPH_H, CHARACTER_H].map((rel) => fs.readFileSync(path.join(dir, rel), 'utf8'));
        const r = runOn(dir);
        let detail = '';
        if (r.status !== c.expect)
            detail = 'wrong exit code';
        else if (c.expect === 0)
            detail = verifyGood(dir, c.files);
        else if ([SHAPE_MORPH_H, CHARACTER_H].some((rel, k) => fs.readFileSync(path.join(dir, rel), 'utf8') !== before[k]))
            detail = 'a header was modified on a failing run';
        if (detail)
            failures++;
        const msg = `${r.stdout || ''}${r.stderr || ''}`.trim().split('\n').pop();
        console.log(`${detail ? 'FAIL' : 'PASS'} ${c.name}: exit ${r.status} (expected ${c.expect})${detail ? ` -- ${detail}` : ''} | ${msg}`);
    });
    fs.rmSync(base, { recursive: true, force: true });
    console.log(`self-test: ${cases.length - failures}/${cases.length} passed`);
    return failures === 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------------

function main(argv) {
    if (argv.includes('--self-test'))
        return selfTest();
    const ri = argv.indexOf('--root');
    const root = ri >= 0 ? path.resolve(argv[ri + 1] || '') : path.resolve(__dirname, '../../../..');
    try {
        for (const line of paste(root))
            console.log(line);
        console.log('paste-harvest: done; rebuild dsp_systems_tests next');
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
