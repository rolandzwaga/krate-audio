#!/usr/bin/env node
// Profundum Phase 7, task T015: paste the harvested wide-floor pair into bass_stereo_field.h (plan S9.1; C-13).
//
// Usage (from harvest.sh, or by hand after a harvest step exited 0; never after a STOP AND SURFACE):
//   node specs/profundum-phase7-stereo-field/evidence/close/paste-harvest.js wide-floor
//   node specs/profundum-phase7-stereo-field/evidence/close/paste-harvest.js --self-test
//   (--root <dir> overrides the repository root; the self-test uses it on synthetic copies)
//
// wide-floor -> evidence/harvest-wide-floor.txt -> dsp/include/krate/dsp/systems/bass_stereo_field.h (2 lines):
// reads the lines between "BEGIN-INITIALIZER wide-floor" and "END-INITIALIZER wide-floor" (exactly two, in order:
//   static constexpr float kSideFloorGain = <v>f;
//   static constexpr float kWideOffsetScale = <v>f;
// ) and puts them between "// HARVEST-BEGIN wide-floor" and "// HARVEST-END wide-floor", keeping the marker lines and
// the indentation of the old constant lines, and adding
// "// harvested <ISO date> from evidence/harvest-wide-floor.txt (plan S9.1)" right after the BEGIN marker (a previous
// such line is replaced, never stacked).
// Exit 1 (nothing written) unless the marker pair occurs exactly once, the evidence block and the header block each hold
// exactly the two constant lines, both numbers are finite float literals with an `f` suffix,
// 0 < kSideFloorGain < 1 and 0 < kWideOffsetScale <= 1. An evidence file carrying a "STOP AND SURFACE" line is refused.
// Prints a before / after summary. CommonJS, no dependencies.
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

const SPEC_EVIDENCE = 'specs/profundum-phase7-stereo-field/evidence';

const TARGETS = {
    'wide-floor': {
        source: 'harvest-wide-floor.txt',
        header: 'dsp/include/krate/dsp/systems/bass_stereo_field.h',
        // In order; each with its admissible range.
        constants: [
            { name: 'kSideFloorGain', ok: (v) => v > 0 && v < 1, range: '0 < v < 1' },
            { name: 'kWideOffsetScale', ok: (v) => v > 0 && v <= 1, range: '0 < v <= 1' },
        ],
    },
};

const FLOAT_F_RE = /^[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?f$/;
// "static constexpr float <name> = <token>;" with an optional trailing // comment (the header's seed comments).
const CONST_RE = /^\s*static constexpr float (\w+) = (\S+);\s*(?:\/\/.*)?$/;
const HARVESTED_RE = /^[ \t]*\/\/ harvested \S+ from evidence\/\S+ \(plan S9\.1\)[ \t]*$/;

class PasteError extends Error {}

function fail(msg) {
    throw new PasteError(msg);
}

// Validates one constant line against the expected name; returns its value token and value.
function parseConst(line, where, spec) {
    const t = line.trim();
    const m = CONST_RE.exec(t);
    if (!m)
        fail(`${where}: not a "static constexpr float ${spec.name} = <v>f;" line: "${t}"`);
    if (m[1] !== spec.name)
        fail(`${where}: expected ${spec.name}, found ${m[1]}`);
    const tok = m[2];
    if (!FLOAT_F_RE.test(tok))
        fail(`${where}: malformed value "${tok}" (expected a finite float literal with an f suffix)`);
    const v = Number(tok.slice(0, -1));
    if (!Number.isFinite(v))
        fail(`${where}: non-finite value "${tok}"`);
    if (!spec.ok(v))
        fail(`${where}: ${spec.name} = ${tok} outside ${spec.range}`);
    return { tok, v };
}

function readHarvest(evidenceDir, name, target) {
    const file = path.join(evidenceDir, target.source);
    if (!fs.existsSync(file))
        fail(`${file}: not found (run the harvest first)`);
    const text = fs.readFileSync(file, 'utf8').replace(/\r/g, '');
    if (text.includes('STOP AND SURFACE'))
        fail(`${file}: contains a STOP AND SURFACE line; return to the user, do not paste`);
    const lines = text.split('\n');
    const b = lines.reduce((acc, l, i) => (l.trim() === `BEGIN-INITIALIZER ${name}` ? [...acc, i] : acc), []);
    const e = lines.reduce((acc, l, i) => (l.trim() === `END-INITIALIZER ${name}` ? [...acc, i] : acc), []);
    if (b.length !== 1 || e.length !== 1)
        fail(`${file}: BEGIN/END-INITIALIZER ${name} occur ${b.length}/${e.length} times, expected exactly 1 each`);
    if (e[0] <= b[0])
        fail(`${file}: END-INITIALIZER ${name} does not follow BEGIN-INITIALIZER`);
    const body = lines.slice(b[0] + 1, e[0]).filter((l) => l.trim() !== '');
    if (body.length !== target.constants.length)
        fail(`${file}: ${body.length} lines for ${name}, expected ${target.constants.length}`);
    const parsed = body.map((l, i) => parseConst(l, `${file} ${name}[${i}]`, target.constants[i]));
    // The pasted line is the bare declaration (no trailing comment).
    return {
        lines: parsed.map((p, i) => `static constexpr float ${target.constants[i].name} = ${p.tok};`),
        values: parsed.map((p) => p.v),
    };
}

function escapeRe(s) {
    return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function replaceBlock(text, name, target, harvest, relHeader, isoDate) {
    const eol = text.includes('\r\n') ? '\r\n' : '\n';
    const n = escapeRe(name);
    const beginRe = new RegExp(`^([ \\t]*)// HARVEST-BEGIN ${n}[ \\t]*\\r?$`, 'gm');
    const endRe = new RegExp(`^[ \\t]*// HARVEST-END ${n}[ \\t]*\\r?$`, 'gm');
    const begins = [...text.matchAll(beginRe)];
    const ends = [...text.matchAll(endRe)];
    if (begins.length !== 1)
        fail(`${relHeader}: "// HARVEST-BEGIN ${name}" occurs ${begins.length} times, expected exactly 1`);
    if (ends.length !== 1)
        fail(`${relHeader}: "// HARVEST-END ${name}" occurs ${ends.length} times, expected exactly 1`);
    const markerIndent = begins[0][1];
    const blockStart = text.indexOf('\n', begins[0].index) + 1;
    const blockEnd = ends[0].index;
    if (blockStart <= 0 || blockStart > blockEnd)
        fail(`${relHeader}: HARVEST-END ${name} does not follow HARVEST-BEGIN`);

    const oldLines = text.slice(blockStart, blockEnd).split(/\r?\n/);
    if (oldLines.length > 0 && oldLines[oldLines.length - 1] === '')
        oldLines.pop();
    const kept = oldLines.filter((l) => !HARVESTED_RE.test(l));
    if (kept.length !== target.constants.length)
        fail(`${relHeader}: ${kept.length} lines between the ${name} markers (besides the harvested comment), expected ${target.constants.length}`);
    const oldValues = kept.map((l, i) => parseConst(l, `${relHeader} ${name} (old)[${i}]`, target.constants[i]).v);
    const rowIndent = /^[ \t]*/.exec(kept[0])[0];
    const newLines = [
        `${markerIndent}// harvested ${isoDate} from evidence/${target.source} (plan S9.1)`,
        ...harvest.lines.map((l) => `${rowIndent}${l}`),
    ];
    return {
        text: text.slice(0, blockStart) + newLines.join(eol) + eol + text.slice(blockEnd),
        oldRows: kept.map((l) => l.trim()),
        oldValues,
    };
}

function paste(root, names, isoDate) {
    if (names.length === 0)
        fail(`no block named; expected one of: ${Object.keys(TARGETS).join(', ')}`);
    const evidenceDir = path.join(root, SPEC_EVIDENCE);
    const report = [];
    const writes = [];
    const seen = new Set();
    for (const name of names) {
        const target = TARGETS[name];
        if (!target)
            fail(`unknown block "${name}"; expected one of: ${Object.keys(TARGETS).join(', ')}`);
        if (seen.has(name))
            fail(`block "${name}" named twice`);
        seen.add(name);
        const harvest = readHarvest(evidenceDir, name, target);
        const abs = path.join(root, target.header);
        if (!fs.existsSync(abs))
            fail(`${target.header}: not found`);
        const original = fs.readFileSync(abs, 'utf8');
        const r = replaceBlock(original, name, target, harvest, target.header, isoDate);
        report.push(`${target.header} [${name}]:`);
        harvest.lines.forEach((line, i) => {
            report.push(`  ${i}: before ${r.oldRows[i]}`);
            report.push(`  ${i}: after  ${line}   (delta ${(harvest.values[i] - r.oldValues[i]).toPrecision(6)})`);
        });
        writes.push({ abs, rel: target.header, original, text: r.text });
    }
    // Everything validated: only now write (all or nothing).
    for (const w of writes) {
        if (w.text !== w.original)
            fs.writeFileSync(w.abs, w.text);
        else
            report.push(`${w.rel}: unchanged`);
    }
    return report;
}

// ---------------------------------------------------------------------------------------------------------------------
// --self-test: synthetic copies in a temp directory; checks the exit codes of a good file and of each failure mode.

const SYN_HEADER_REL = 'dsp/include/krate/dsp/systems/bass_stereo_field.h';

function synHeader(eol) {
    return [
        '#pragma once',
        'class BassStereoField {',
        'public:',
        '    static constexpr float kDefaultTempoBpm = 120.0f;',
        '    // HARVEST-BEGIN wide-floor',
        '    static constexpr float kSideFloorGain = 0.5f;    // seed (plan S1 C-11), harvested in T015 (plan S9.1)',
        '    static constexpr float kWideOffsetScale = 1.0f;  // seed (plan S1 C-11), harvested in T015 (plan S9.1)',
        '    // HARVEST-END wide-floor',
        '    static constexpr float kWideCeilingRectifyFracture = 0.10f;  // Phase 5 handoff',
        '};',
        '',
    ].join(eol);
}

function synEvidence(floor, wide) {
    return [
        '# harvest-wide-floor (synthetic)',
        'BEGIN-INITIALIZER wide-floor',
        `static constexpr float kSideFloorGain = ${floor};`,
        `static constexpr float kWideOffsetScale = ${wide};`,
        'END-INITIALIZER wide-floor',
        '# candidate table',
        'g 0.71 FAIL',
        '',
    ].join('\n');
}

function writeTree(dir, header, evidence) {
    const h = path.join(dir, SYN_HEADER_REL);
    fs.mkdirSync(path.dirname(h), { recursive: true });
    fs.writeFileSync(h, header);
    fs.mkdirSync(path.join(dir, SPEC_EVIDENCE), { recursive: true });
    if (evidence !== null)
        fs.writeFileSync(path.join(dir, SPEC_EVIDENCE, 'harvest-wide-floor.txt'), evidence);
}

function runChild(dir, names) {
    const r = spawnSync(process.execPath, [__filename, '--root', dir, ...names], { encoding: 'utf8' });
    return { status: r.status, out: (r.stdout || '') + (r.stderr || '') };
}

function selfTest() {
    const base = fs.mkdtempSync(path.join(os.tmpdir(), 'paste-harvest-p7-'));
    const cases = [];
    const check = (label, ok, detail) => {
        cases.push({ label, ok });
        console.log(`${ok ? 'PASS' : 'FAIL'} ${label}${detail ? ` -- ${detail}` : ''}`);
    };
    let n = 0;
    const fresh = (header, evidence) => {
        const dir = path.join(base, `case${n++}`);
        writeTree(dir, header, evidence);
        return { dir, header };
    };
    const readH = (c) => fs.readFileSync(path.join(c.dir, SYN_HEADER_REL), 'utf8');
    const unchanged = (c) => readH(c) === c.header;
    const lastLine = (out) => out.trim().split('\n').pop();
    const expectFail = (label, header, evidence, names = ['wide-floor']) => {
        const c = fresh(header, evidence);
        const r = runChild(c.dir, names);
        check(`${label} -> exit 1, nothing written`, r.status === 1 && unchanged(c), `exit ${r.status}: ${lastLine(r.out)}`);
    };
    const good = synEvidence('0.25f', '0.75f');

    try {
        // 1. Good file: exit 0, both lines pasted with the old indentation, markers and neighbours kept, LF kept.
        {
            const c = fresh(synHeader('\n'), good);
            const r = runChild(c.dir, ['wide-floor']);
            const h = readH(c);
            const want = new RegExp([
                '\\n {4}// HARVEST-BEGIN wide-floor',
                ' {4}// harvested \\d{4}-\\d{2}-\\d{2} from evidence/harvest-wide-floor\\.txt \\(plan S9\\.1\\)',
                ' {4}static constexpr float kSideFloorGain = 0\\.25f;',
                ' {4}static constexpr float kWideOffsetScale = 0\\.75f;',
                ' {4}// HARVEST-END wide-floor',
                ' {4}static constexpr float kWideCeilingRectifyFracture = 0\\.10f;',
            ].join('\\n'));
            const ok = r.status === 0 && want.test(h) && !h.includes('seed (plan S1 C-11)') && !h.includes('\r');
            check('good file -> exit 0, pair pasted, markers, indentation and neighbours kept', ok, `exit ${r.status}`);
            // Re-paste: still exactly one harvested comment.
            const r2 = runChild(c.dir, ['wide-floor']);
            const count = (readH(c).match(/\/\/ harvested /g) || []).length;
            check('re-paste -> exit 0, harvested comment replaced not stacked', r2.status === 0 && count === 1,
                `exit ${r2.status}, comments ${count}`);
        }
        // 2. CRLF header stays CRLF; the bounds 0.1 / 0.5 (C-13 escalation floor) are admissible.
        {
            const c = fresh(synHeader('\r\n'), synEvidence('0.1f', '0.5f'));
            const r = runChild(c.dir, ['wide-floor']);
            const h = readH(c);
            const ok = r.status === 0 && !/[^\r]\n/.test(h) && h.includes('    static constexpr float kSideFloorGain = 0.1f;\r\n');
            check('CRLF header -> exit 0, CRLF kept', ok, `exit ${r.status}`);
        }
        // 3. Missing / duplicated markers.
        expectFail('missing HARVEST-END marker', synHeader('\n').replace('    // HARVEST-END wide-floor\n', ''), good);
        expectFail('duplicated HARVEST-BEGIN marker',
            synHeader('\n').replace('#pragma once\n', '#pragma once\n    // HARVEST-BEGIN wide-floor\n'), good);
        expectFail('duplicated BEGIN-INITIALIZER in evidence', synHeader('\n'), `${good}BEGIN-INITIALIZER wide-floor\n`);
        // 4. Wrong line count (evidence and header).
        expectFail('evidence block with 1 line', synHeader('\n'),
            good.replace('static constexpr float kWideOffsetScale = 0.75f;\n', ''));
        expectFail('evidence block with 3 lines', synHeader('\n'),
            good.replace('END-INITIALIZER', 'static constexpr float kWideOffsetScale = 0.5f;\nEND-INITIALIZER'));
        expectFail('header block with 3 lines',
            synHeader('\n').replace('    // HARVEST-END', '    static constexpr float kExtra = 1.0f;\n    // HARVEST-END'), good);
        // 5. Malformed / out-of-range values.
        for (const [label, floor, wide] of [
            ['missing f suffix', '0.25', '0.75f'],
            ['nan', 'nanf', '0.75f'],
            ['garbage', '0.2x5f', '0.75f'],
            ['kSideFloorGain = 1, must be < 1', '1.0f', '0.75f'],
            ['kSideFloorGain = 0', '0.0f', '0.75f'],
            ['kWideOffsetScale > 1', '0.25f', '1.25f'],
            ['kWideOffsetScale negative', '0.25f', '-0.5f'],
        ])
            expectFail(`malformed value (${label})`, synHeader('\n'), synEvidence(floor, wide));
        // 6. Constants in the wrong order.
        expectFail('constants in the wrong order', synHeader('\n'), [
            'BEGIN-INITIALIZER wide-floor',
            'static constexpr float kWideOffsetScale = 0.75f;',
            'static constexpr float kSideFloorGain = 0.25f;',
            'END-INITIALIZER wide-floor',
            '',
        ].join('\n'));
        // 7. STOP AND SURFACE in the evidence file is refused; missing evidence; unknown block name.
        expectFail('STOP AND SURFACE in evidence', synHeader('\n'),
            `${good}STOP AND SURFACE: no g_floor >= 0.1 passes (plan C-13)\n`);
        expectFail('missing evidence file', synHeader('\n'), null);
        expectFail('unknown block name', synHeader('\n'), good, ['kSideFloorGain']);
    } finally {
        fs.rmSync(base, { recursive: true, force: true });
    }
    const failed = cases.filter((c) => !c.ok).length;
    console.log(`self-test: ${cases.length - failed}/${cases.length} passed`);
    return failed === 0 ? 0 : 1;
}

function main(argv) {
    if (argv.includes('--self-test'))
        return selfTest();
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
    const isoDate = new Date().toISOString().slice(0, 10);
    try {
        for (const line of paste(root, names, isoDate))
            console.log(line);
        console.log(`paste-harvest: pasted ${names.join(', ')}`);
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
