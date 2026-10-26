#!/usr/bin/env node
// Profundum Phase 6, task T019: paste the harvested calibration tables into the headers (plan S8; tasks.md deviation 6).
//
// Usage (from harvest.sh, or by hand after a harvest exited 0; never after a STOP AND SURFACE):
//   node specs/profundum-phase6-mass-dynamics/evidence/close/paste-harvest.js duration-table amount-table
//   node specs/profundum-phase6-mass-dynamics/evidence/close/paste-harvest.js mass-law-rows
//   node specs/profundum-phase6-mass-dynamics/evidence/close/paste-harvest.js --self-test
//   (--root <dir> overrides the repository root; the self-test uses it on synthetic copies)
//
// For each name:
//   duration-table -> evidence/duration-table.txt  -> dsp/include/krate/dsp/processors/transient_shaper.h (11 lines)
//   amount-table   -> evidence/amount-table.txt    -> dsp/include/krate/dsp/systems/mass_dynamics.h      (5 lines)
//   mass-law-rows  -> evidence/mass-law-table.txt  -> dsp/include/krate/dsp/systems/mass_law.h           (7 lines; G8-5)
// reads the lines between "BEGIN-INITIALIZER <name>" and "END-INITIALIZER <name>" and puts them between
// "// HARVEST-BEGIN <name>" and "// HARVEST-END <name>", keeping the marker lines, any non-initializer lines inside the
// markers (the kRows "static constexpr ... {{" / "}};" wrapper) and the indentation of the old initializer lines, and
// adding "// harvested <ISO date> from evidence/<file> (plan S8)" right after the BEGIN marker (a previous such line is
// replaced, never stacked).
// Exit 1 (nothing written) unless each marker pair occurs exactly once in its header, the initializer line count matches,
// every line is a "{...}," initializer with the expected number of numeric fields, and every number is a finite float
// literal with an `f` suffix. An evidence file carrying a "STOP AND SURFACE" line is refused (return to the user).
// Prints a before / after summary per line. CommonJS, no dependencies.
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

const SPEC_EVIDENCE = 'specs/profundum-phase6-mass-dynamics/evidence';

const TARGETS = {
    'duration-table': {
        source: 'duration-table.txt',
        header: 'dsp/include/krate/dsp/processors/transient_shaper.h',
        count: 11,
        fields: 2,
    },
    'amount-table': {
        source: 'amount-table.txt',
        header: 'dsp/include/krate/dsp/systems/mass_dynamics.h',
        count: 5,
        fields: 5,
    },
    'mass-law-rows': {
        source: 'mass-law-table.txt',
        header: 'dsp/include/krate/dsp/systems/mass_law.h',
        count: 7,
        fields: 4,
    },
};

// A numeric token: optional sign, digits with optional fraction (or a leading '.'), optional exponent, optional suffix.
// Identifiers (Target::Depth, ModCurve::Linear, .durationMs) are excluded by the look-behind on word chars and '.'
// before a leading digit is handled by requiring no word char immediately before.
const NUM_TOKEN_RE = /(?<![\w.])[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?[A-Za-z_]*/g;
const FLOAT_F_RE = /^[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?f$/;

class PasteError extends Error {}

function fail(msg) {
    throw new PasteError(msg);
}

function escapeRe(s) {
    return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function isInitializerLine(line) {
    return /^\s*\{.*\},\s*$/.test(line);
}

// Validates one initializer line; returns its numbers.
function parseLine(line, name, index, fields) {
    const where = `${name}[${index}]`;
    const t = line.trim();
    if (!isInitializerLine(t))
        fail(`${where}: not a "{...}," initializer line: "${t}"`);
    if (/\b(?:nan|inf)/i.test(t))
        fail(`${where}: non-finite value in "${t}"`);
    const tokens = t.match(NUM_TOKEN_RE) || [];
    const values = tokens.map((tok) => {
        if (!FLOAT_F_RE.test(tok))
            fail(`${where}: malformed value "${tok}" (expected a float literal with an f suffix)`);
        const v = Number(tok.slice(0, -1));
        if (!Number.isFinite(v))
            fail(`${where}: non-finite value "${tok}"`);
        return v;
    });
    if (values.length !== fields)
        fail(`${where}: ${values.length} numeric fields, expected ${fields}: "${t}"`);
    return values;
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
    if (body.length !== target.count)
        fail(`${file}: ${body.length} initializer lines for ${name}, expected ${target.count}`);
    const values = body.map((l, i) => parseLine(l, name, i, target.fields));
    return { lines: body.map((l) => l.trim()), values };
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
    const rowIdx = oldLines.map((l, i) => (isInitializerLine(l) ? i : -1)).filter((i) => i >= 0);
    if (rowIdx.length !== target.count)
        fail(`${relHeader}: ${rowIdx.length} initializer lines between the ${name} markers, expected ${target.count}`);
    for (let i = 1; i < rowIdx.length; ++i) {
        if (rowIdx[i] !== rowIdx[i - 1] + 1)
            fail(`${relHeader}: the ${name} initializer lines are not contiguous`);
    }
    const rowIndent = /^[ \t]*/.exec(oldLines[rowIdx[0]])[0];
    const oldRows = rowIdx.map((i) => oldLines[i]);
    const oldValues = oldRows.map((l, i) => parseLine(l, `${relHeader} ${name} (old)`, i, target.fields));

    const harvestedRe = /^[ \t]*\/\/ harvested \S+ from evidence\/\S+ \(plan S8\)[ \t]*$/;
    const before = oldLines.slice(0, rowIdx[0]).filter((l) => !harvestedRe.test(l));
    const after = oldLines.slice(rowIdx[rowIdx.length - 1] + 1);
    const newLines = [
        `${markerIndent}// harvested ${isoDate} from evidence/${target.source} (plan S8)`,
        ...before,
        ...harvest.lines.map((l) => `${rowIndent}${l}`),
        ...after,
    ];
    const newBlock = newLines.join(eol) + eol;
    return {
        text: text.slice(0, blockStart) + newBlock + text.slice(blockEnd),
        oldRows: oldRows.map((l) => l.trim()),
        oldValues,
    };
}

function summary(name, relHeader, r, harvest) {
    const out = [`${relHeader} [${name}]:`];
    harvest.lines.forEach((line, i) => {
        const deltas = harvest.values[i].map((v, k) => v - r.oldValues[i][k]);
        const maxAbs = Math.max(...deltas.map(Math.abs));
        out.push(`  ${i}: before ${r.oldRows[i]}`);
        out.push(`  ${i}: after  ${line}   (max |delta| ${maxAbs.toPrecision(6)})`);
    });
    return out;
}

function paste(root, names, isoDate) {
    if (names.length === 0)
        fail(`no table named; expected one or more of: ${Object.keys(TARGETS).join(', ')}`);
    const evidenceDir = path.join(root, SPEC_EVIDENCE);
    const headers = new Map();
    const report = [];
    const seen = new Set();
    for (const name of names) {
        const target = TARGETS[name];
        if (!target)
            fail(`unknown table "${name}"; expected one of: ${Object.keys(TARGETS).join(', ')}`);
        if (seen.has(name))
            fail(`table "${name}" named twice`);
        seen.add(name);
        const harvest = readHarvest(evidenceDir, name, target);
        const abs = path.join(root, target.header);
        if (!headers.has(abs)) {
            if (!fs.existsSync(abs))
                fail(`${target.header}: not found`);
            const original = fs.readFileSync(abs, 'utf8');
            headers.set(abs, { rel: target.header, original, text: original });
        }
        const h = headers.get(abs);
        const r = replaceBlock(h.text, name, target, harvest, h.rel, isoDate);
        h.text = r.text;
        report.push(...summary(name, h.rel, r, harvest));
    }
    // Every table validated: only now write (all or nothing).
    for (const [abs, h] of headers) {
        if (h.text !== h.original)
            fs.writeFileSync(abs, h.text);
        else
            report.push(`${h.rel}: unchanged`);
    }
    return report;
}

// ---------------------------------------------------------------------------------------------------------------------
// --self-test: synthetic copies in a temp directory; checks the exit codes of a good file and of each failure mode.

function synHeaders() {
    const dur = Array.from({ length: 11 }, (_, i) => `        {.durationMs = ${i + 1}.0f, .slowAttackArgMs = ${2 * (i + 1)}.0f},`);
    const amt = Array.from({ length: 5 }, (_, i) =>
        `        {.amount = ${(i / 4).toFixed(2)}f, .thresholdDb = ${-4 * i}.0f, .slope = 0.25f, .kneeDb = 6.0f, .makeupDb = 0.0f},`);
    const law = ['Depth', 'Body', 'TransientDuration', 'FoldAmount', 'CompressionAmount', 'AnchorLevel', 'OutputTrim'].map(
        (t) => `        {Target::${t}, 0.0f, 1.0f, 0.5f, 0.5f, ModCurve::Linear},`);
    return {
        'dsp/include/krate/dsp/processors/transient_shaper.h': [
            '#pragma once', 'struct S {', '    static constexpr std::array<DurationRow, 11> kDurationTable{{',
            '        // HARVEST-BEGIN duration-table', ...dur, '        // HARVEST-END duration-table', '    }};', '};', '',
        ].join('\r\n'),
        'dsp/include/krate/dsp/systems/mass_dynamics.h': [
            '#pragma once', 'struct C {', '    static constexpr std::array<AmountRow, 5> kAmountTable{{',
            '        // HARVEST-BEGIN amount-table', ...amt, '        // HARVEST-END amount-table', '    }};', '};', '',
        ].join('\n'),
        'dsp/include/krate/dsp/systems/mass_law.h': [
            '#pragma once', 'struct L {', '    // HARVEST-BEGIN mass-law-rows',
            '    static constexpr std::array<Row, kNumTargets> kRows{{', ...law, '    }};',
            '    // HARVEST-END mass-law-rows', '};', '',
        ].join('\n'),
    };
}

function synEvidence() {
    const dur = Array.from({ length: 11 }, (_, i) =>
        `        {.durationMs = ${i + 1}.0f, .slowAttackArgMs = ${(2.5 * (i + 1)).toFixed(6)}f},`);
    const amt = Array.from({ length: 5 }, (_, i) =>
        `        {.amount = ${(i / 4).toFixed(2)}f, .thresholdDb = ${(-4.5 * i).toFixed(6)}f, .slope = 0.25f, .kneeDb = 6.0f, .makeupDb = ${(0.8 * i).toFixed(6)}f},`);
    const law = ['Depth', 'Body', 'TransientDuration', 'FoldAmount', 'CompressionAmount', 'AnchorLevel', 'OutputTrim'].map(
        (t, i) => `        {Target::${t}, 0.0f, 1.0f, ${(0.1 * i).toFixed(3)}f, 1.5e-05f, ModCurve::SCurve},`);
    const wrap = (name, rows) => [`BEGIN-INITIALIZER ${name}`, ...rows, `END-INITIALIZER ${name}`, '# trailer', 'row 0: 1.0 2.0', ''].join('\n');
    return {
        'duration-table.txt': wrap('duration-table', dur),
        'amount-table.txt': wrap('amount-table', amt),
        'mass-law-table.txt': wrap('mass-law-rows', law),
    };
}

function writeTree(dir, headers, evidence) {
    for (const [rel, text] of Object.entries(headers)) {
        fs.mkdirSync(path.dirname(path.join(dir, rel)), { recursive: true });
        fs.writeFileSync(path.join(dir, rel), text);
    }
    fs.mkdirSync(path.join(dir, SPEC_EVIDENCE), { recursive: true });
    for (const [file, text] of Object.entries(evidence))
        fs.writeFileSync(path.join(dir, SPEC_EVIDENCE, file), text);
}

function runChild(dir, names) {
    const r = spawnSync(process.execPath, [__filename, '--root', dir, ...names], { encoding: 'utf8' });
    return { status: r.status, out: (r.stdout || '') + (r.stderr || '') };
}

function selfTest() {
    const base = fs.mkdtempSync(path.join(os.tmpdir(), 'paste-harvest-p6-'));
    const all = ['duration-table', 'amount-table', 'mass-law-rows'];
    const cases = [];
    const check = (label, ok, detail) => {
        cases.push({ label, ok });
        console.log(`${ok ? 'PASS' : 'FAIL'} ${label}${detail ? ` -- ${detail}` : ''}`);
    };
    let n = 0;
    const fresh = (mutHeaders, mutEvidence) => {
        const dir = path.join(base, `case${n++}`);
        const h = synHeaders();
        const e = synEvidence();
        if (mutHeaders)
            mutHeaders(h);
        if (mutEvidence)
            mutEvidence(e);
        writeTree(dir, h, e);
        return { dir, headers: h };
    };
    const unchanged = (c) => Object.entries(c.headers).every(([rel, t]) => fs.readFileSync(path.join(c.dir, rel), 'utf8') === t);
    const lastLine = (out) => out.trim().split('\n').pop();

    try {
        // 1. Good files: exit 0, rows replaced, wrappers / CRLF / indentation kept, one harvested comment.
        {
            const c = fresh();
            const r = runChild(c.dir, all);
            const ts = fs.readFileSync(path.join(c.dir, 'dsp/include/krate/dsp/processors/transient_shaper.h'), 'utf8');
            const ml = fs.readFileSync(path.join(c.dir, 'dsp/include/krate/dsp/systems/mass_law.h'), 'utf8');
            const md = fs.readFileSync(path.join(c.dir, 'dsp/include/krate/dsp/systems/mass_dynamics.h'), 'utf8');
            const ok = r.status === 0
                && ts.includes('        {.durationMs = 11.0f, .slowAttackArgMs = 27.500000f},\r\n')
                && !/[^\r]\n/.test(ts)
                && ts.includes('        // harvested ') && ts.includes(' from evidence/duration-table.txt (plan S8)\r\n')
                && md.includes('        {.amount = 1.00f, .thresholdDb = -18.000000f, .slope = 0.25f, .kneeDb = 6.0f, .makeupDb = 3.200000f},\n')
                && ml.includes('    static constexpr std::array<Row, kNumTargets> kRows{{\n        {Target::Depth, 0.0f, 1.0f, 0.000f, 1.5e-05f, ModCurve::SCurve},')
                && ml.includes('ModCurve::SCurve},\n    }};\n    // HARVEST-END mass-law-rows')
                && !ml.includes('ModCurve::Linear');
            check('good files -> exit 0, rows pasted, wrapper, CRLF and indentation kept', ok, `exit ${r.status}`);
            // Re-paste: still exactly one harvested comment per block.
            const r2 = runChild(c.dir, all);
            const ts2 = fs.readFileSync(path.join(c.dir, 'dsp/include/krate/dsp/processors/transient_shaper.h'), 'utf8');
            const count = (ts2.match(/\/\/ harvested /g) || []).length;
            check('re-paste -> exit 0, harvested comment replaced not stacked', r2.status === 0 && count === 1, `exit ${r2.status}, comments ${count}`);
        }
        // 2. Missing marker.
        {
            const c = fresh((h) => {
                h['dsp/include/krate/dsp/systems/mass_dynamics.h'] = h['dsp/include/krate/dsp/systems/mass_dynamics.h'].replace('        // HARVEST-END amount-table\n', '');
            });
            const r = runChild(c.dir, all);
            check('missing HARVEST-END marker -> exit 1, nothing written', r.status === 1 && unchanged(c), `exit ${r.status}: ${lastLine(r.out)}`);
        }
        // 3. Duplicated marker.
        {
            const c = fresh((h) => {
                const k = 'dsp/include/krate/dsp/systems/mass_law.h';
                h[k] = h[k].replace('#pragma once\n', '#pragma once\n    // HARVEST-BEGIN mass-law-rows\n');
            });
            const r = runChild(c.dir, all);
            check('duplicated HARVEST-BEGIN marker -> exit 1, nothing written', r.status === 1 && unchanged(c), `exit ${r.status}: ${lastLine(r.out)}`);
        }
        // 4. Wrong line count.
        {
            const c = fresh(null, (e) => {
                e['duration-table.txt'] = e['duration-table.txt'].replace(/^ {8}\{\.durationMs = 11\.0f.*\n/m, '');
            });
            const r = runChild(c.dir, all);
            check('wrong initializer line count (10 of 11) -> exit 1, nothing written', r.status === 1 && unchanged(c), `exit ${r.status}: ${lastLine(r.out)}`);
        }
        // 5. Malformed values: missing f suffix, nan, garbage.
        for (const [label, bad] of [
            ['missing f suffix', '-9.000000,'],
            ['nan', 'nanf,'],
            ['garbage', '-9.0x0f,'],
        ]) {
            const c = fresh(null, (e) => {
                e['amount-table.txt'] = e['amount-table.txt'].replace('.thresholdDb = -4.500000f,', `.thresholdDb = ${bad}`);
            });
            const r = runChild(c.dir, all);
            check(`malformed value (${label}) -> exit 1, nothing written`, r.status === 1 && unchanged(c), `exit ${r.status}: ${lastLine(r.out)}`);
        }
        // 6. STOP AND SURFACE in the evidence file is refused.
        {
            const c = fresh(null, (e) => {
                e['mass-law-table.txt'] += 'STOP AND SURFACE: FR-021 ruling needed (R-6) synthetic\n';
            });
            const r = runChild(c.dir, ['mass-law-rows']);
            check('STOP AND SURFACE in evidence -> exit 1, nothing written', r.status === 1 && unchanged(c), `exit ${r.status}: ${lastLine(r.out)}`);
        }
        // 7. Unknown table name.
        {
            const c = fresh();
            const r = runChild(c.dir, ['kRows']);
            check('unknown table name -> exit 1', r.status === 1 && unchanged(c), `exit ${r.status}: ${lastLine(r.out)}`);
        }
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
