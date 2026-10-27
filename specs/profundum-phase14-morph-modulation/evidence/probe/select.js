#!/usr/bin/env node
// Profundum Phase 14 (T021; FR-044, FR-046; plan 5.7 steps 2-4; R4 / P-9): the conversion selection.
//
// Usage: node select.js [--rule=ii|i]
//
// Reads ../probe-morphmod.csv (merge.js) and writes ../conversions.md. The default rule is the ruled one:
// R4 (evidence/plan-rulings.md, FR-046, user 2026-10-26) = rule (ii), the value of kMorphModConversionRule (T023).
// --rule=i exists only because both rules are built (tasks ruling table); it is never the ruled selection.
//
// Plan 5.7, as tasks.md T021 pins it:
//   candidate   = the (preset, column, nine C-10 fields) of a probe row; it has one row at E1 and one at C3.
//   carriable   = P carries column X when some candidate of (P, X) passes at both notes.
//   margin      = min(contrast_E1, contrast_C3) - 3 (dB) of that candidate; (P, X)'s margin is its best candidate's
//                 (ties: probe-file order, i.e. the plan 5.7 step 1 candidate order).
//   eligible    = !singleState AND P has not converted yet AND P's primary keeps >= 1 other primary after every
//                 conversion chosen so far (the count of presets whose current primary is P's primary is >= 2).
//   rule (i)    = columns in order M-ENV, M-CYCLE, M-VEL, M-WHEEL; each picks the eligible carrier with the largest
//                 margin (ties: definition order).
//   rule (ii)   = presets in descending best-margin order (ties: definition order; a preset's best column ties in
//                 column order); each converts for its best-margin column if still eligible.
//   A preset converts at most once. A column left with no conversion has no eligible carrier: it reports
//   "NEW PRESET NEEDED" with its best probe row (the carriable candidate with the largest margin over every preset,
//   ties: definition order), whose preset is the base the probe names. A column with no carriable candidate in ANY
//   preset is the FR-023 stop: "NO PASSING CANDIDATE" with its best measured contrast (the largest
//   min(contrast_E1, contrast_C3)); select.js then exits 3 after writing conversions.md.
// Exit: 0 (selection written), 1 (bad input), 3 (FR-023 stop).
// Last stdout line: "SELECTION <n> conversions, <m> new" (followed by nothing on exit 0 or 3).
'use strict';

const fs = require('fs');
const path = require('path');

const kColumns = ['M-ENV', 'M-CYCLE', 'M-VEL', 'M-WHEEL'];
const kNotes = ['E1', 'C3'];
const kBarDb = 3;
const kFields = ['source', 'amount', 'attackMs', 'decayMs', 'sustain', 'cycleShape', 'cycleRateHz', 'cycleSync',
                 'cycleSyncRate'];
const kCandidatesPerPreset = 16;
const evDir = path.join(__dirname, '..');
const inFile = path.join(evDir, 'probe-morphmod.csv');
const outFile = path.join(evDir, 'conversions.md');

function fail(msg) {
    console.error(`probe select: ${msg}`);
    process.exit(1);
}

let rule = 'ii';
for (const a of process.argv.slice(2)) {
    const m = /^--rule=(i|ii)$/.exec(a);
    if (!m) fail(`unknown argument "${a}" (usage: node select.js [--rule=ii|i])`);
    rule = m[1];
}

/// RFC-4180 style parse: quoted fields, doubled quotes; CR dropped outside quotes.
function parseCsv(text) {
    const rows = [];
    let row = [];
    let field = '';
    let quoted = false;
    let any = false;
    for (let i = 0; i < text.length; ++i) {
        const c = text[i];
        if (quoted) {
            if (c === '"') {
                if (text[i + 1] === '"') {
                    field += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c === '"') {
            quoted = true;
            any = true;
        } else if (c === ',') {
            row.push(field);
            field = '';
            any = true;
        } else if (c === '\n') {
            row.push(field);
            if (row.length > 1 || row[0] !== '') rows.push(row);
            row = [];
            field = '';
            any = false;
        } else if (c !== '\r') {
            field += c;
            any = true;
        }
    }
    if (quoted) fail(`${inFile}: unbalanced quotes`);
    if (any) {
        row.push(field);
        rows.push(row);
    }
    return rows;
}

if (!fs.existsSync(inFile)) fail(`missing ${inFile} (run merge.js first)`);
const table = parseCsv(fs.readFileSync(inFile, 'utf8').replace(/^﻿/, ''));
if (table.length < 2) fail(`${inFile}: no data rows`);
const header = table[0];
const col = (name) => {
    const i = header.indexOf(name);
    if (i < 0) fail(`${inFile}: no column "${name}"`);
    return i;
};
const C = {
    runId: col('runId'), preset: col('preset'), def: col('defIndex'), column: col('column'), note: col('note'),
    t0: col('t0'), pLo: col('p_lo'), pHi: col('p_hi'), dLo: col('dnl_lo'), dHi: col('dnl_hi'),
    contrast: col('contrast'), pass: col('pass'), primary: col('primary'), single: col('singleState'),
};
const fieldCols = kFields.map(col);

const num = (text, what) => {
    const v = Number(text);
    if (text === '' || !Number.isFinite(v)) fail(`${what}: not a finite number "${text}"`);
    return v;
};

// ---- presets and candidates (in file order) ------------------------------------------------------------------
const presets = new Map();      // name -> { name, def, primary, single, primaryNow }
const candidates = new Map();   // key -> { preset, column, fields, order, rows: { E1, C3 } }
const runIds = new Set();
for (let i = 1; i < table.length; ++i) {
    const r = table[i];
    const where = `${path.basename(inFile)} line ${i + 1}`;
    if (r.length !== header.length) fail(`${where}: ${r.length} fields, header has ${header.length}`);
    runIds.add(r[C.runId]);
    const name = r[C.preset];
    const def = num(r[C.def], `${where} defIndex`);
    const single = r[C.single];
    if (single !== 'true' && single !== 'false') fail(`${where}: singleState "${single}"`);
    if (r[C.pass] !== 'PASS' && r[C.pass] !== 'FAIL') fail(`${where}: pass "${r[C.pass]}"`);
    if (!kColumns.includes(r[C.column])) fail(`${where}: unknown column "${r[C.column]}"`);
    if (!kNotes.includes(r[C.note])) fail(`${where}: unknown note "${r[C.note]}"`);
    let p = presets.get(name);
    if (!p) {
        p = { name, def, primary: r[C.primary], single: single === 'true' };
        presets.set(name, p);
    } else if (p.def !== def || p.primary !== r[C.primary] || p.single !== (single === 'true')) {
        fail(`${where}: preset "${name}" changes defIndex / primary / singleState between rows`);
    }
    const fields = fieldCols.map((c) => r[c]);
    const key = [name, r[C.column], ...fields].join('|');
    let cand = candidates.get(key);
    if (!cand) {
        cand = { preset: p, column: r[C.column], fields, order: candidates.size, rows: {} };
        candidates.set(key, cand);
    }
    if (cand.rows[r[C.note]]) fail(`${where}: second ${r[C.note]} row for candidate ${key}`);
    cand.rows[r[C.note]] = {
        line: i + 1, pass: r[C.pass] === 'PASS', contrast: num(r[C.contrast], `${where} contrast`),
        t0: r[C.t0], pLo: r[C.pLo], pHi: r[C.pHi], dLo: r[C.dLo], dHi: r[C.dHi],
    };
}
if (runIds.size !== 1) fail(`${inFile}: ${runIds.size} run ids (${[...runIds].join(', ')}); expected one merge`);
const runId = [...runIds][0];
const defs = new Set();
for (const p of presets.values()) {
    if (defs.has(p.def)) fail(`defIndex ${p.def} names two presets`);
    defs.add(p.def);
}
const perPreset = new Map();
for (const cand of candidates.values()) {
    for (const n of kNotes) {
        if (!cand.rows[n]) fail(`candidate ${cand.preset.name} ${cand.column} [${cand.fields.join(' ')}] has no ${n} row`);
    }
    perPreset.set(cand.preset.name, (perPreset.get(cand.preset.name) || 0) + 1);
    cand.carriable = kNotes.every((n) => cand.rows[n].pass);
    cand.minContrast = Math.min(...kNotes.map((n) => cand.rows[n].contrast));
    cand.margin = cand.minContrast - kBarDb;
}
for (const p of presets.values()) {
    const n = perPreset.get(p.name) || 0;
    if (n !== kCandidatesPerPreset) fail(`preset "${p.name}" has ${n} candidates, expected ${kCandidatesPerPreset}`);
}

// ---- plan 10: a conversion that fails a Phase 13 gate is replaced by the next carrier --------------------------
// ../gate-exclusions.csv (preset, column, gate, reading) names each (P, X) a gate measured red; its candidates are not
// carriable, so P falls to its next column (or none) and X to its next carrier.
const exclFile = path.join(evDir, 'gate-exclusions.csv');
const excluded = new Set();
if (fs.existsSync(exclFile)) {
    const ex = parseCsv(fs.readFileSync(exclFile, 'utf8').replace(/^﻿/, ''));
    if (ex.length < 1 || ex[0].join(',') !== 'preset,column,gate,reading') fail(`${exclFile}: bad header`);
    for (let i = 1; i < ex.length; ++i) {
        const [preset, column, gate, reading] = ex[i];
        if (!presets.has(preset)) fail(`${exclFile} line ${i + 1}: unknown preset "${preset}"`);
        if (!kColumns.includes(column)) fail(`${exclFile} line ${i + 1}: unknown column "${column}"`);
        if (!gate || !reading) fail(`${exclFile} line ${i + 1}: a gate and its reading are required`);
        excluded.add(`${preset}|${column}`);
        console.log(`EXCLUDED ${preset} ${column}: ${gate} ${reading}`);
    }
}
for (const cand of candidates.values()) {
    if (excluded.has(`${cand.preset.name}|${cand.column}`)) cand.carriable = false;
}

// ---- plan 5.7 step 2: (P, X) carriers and their best candidates ----------------------------------------------
const better = (a, b) => a.margin > b.margin || (a.margin === b.margin && a.order < b.order);   // a beats b
const carrier = new Map();   // `${preset}|${column}` -> best carriable candidate
for (const cand of candidates.values()) {
    if (!cand.carriable) continue;
    const k = `${cand.preset.name}|${cand.column}`;
    const cur = carrier.get(k);
    if (!cur || better(cand, cur)) carrier.set(k, cand);
}

// ---- plan 5.7 steps 3-4 -------------------------------------------------------------------------------------
const primaryCount = new Map();
for (const p of presets.values()) primaryCount.set(p.primary, (primaryCount.get(p.primary) || 0) + 1);
const converted = new Map();   // preset name -> { preset, column, cand }
const eligible = (p) => !p.single && !converted.has(p.name) && primaryCount.get(p.primary) >= 2;
const convert = (cand) => {
    converted.set(cand.preset.name, { preset: cand.preset, column: cand.column, cand });
    primaryCount.set(cand.preset.primary, primaryCount.get(cand.preset.primary) - 1);
};
const byMarginThenDef = (a, b) => b.margin - a.margin || a.preset.def - b.preset.def;

if (rule === 'i') {
    for (const x of kColumns) {
        const list = [...carrier.values()].filter((c) => c.column === x).sort(byMarginThenDef);
        const pick = list.find((c) => eligible(c.preset));
        if (pick) convert(pick);
    }
} else {
    const bestOf = [];   // per preset: its best-margin column's candidate (column order breaks ties)
    for (const p of presets.values()) {
        let best = null;
        for (const x of kColumns) {
            const c = carrier.get(`${p.name}|${x}`);
            if (c && (!best || c.margin > best.margin)) best = c;
        }
        if (best) bestOf.push(best);
    }
    bestOf.sort(byMarginThenDef);
    for (const c of bestOf) {
        if (eligible(c.preset)) convert(c);
    }
}

// ---- the report ---------------------------------------------------------------------------------------------
const fmt = (v) => (Number.isFinite(v) ? v.toFixed(3) : String(v));
const fieldText = (c) => kFields.map((f, i) => `${f}=${c.fields[i]}`).join(' ');
const noteText = (c) => kNotes.map((n) => {
    const r = c.rows[n];
    return `${n}: ${fmt(r.contrast)} dB ${r.pass ? 'PASS' : 'FAIL'} (t0 ${r.t0 || 'none'} s, p_lo ${r.pLo || '-'}, ` +
           `p_hi ${r.pHi || '-'}; line ${r.line})`;
}).join('; ');

const lines = [];
lines.push('# Phase 14 Morph Mod conversions (FR-046; plan 5.7 steps 2-4)');
lines.push('');
lines.push(`Generated by \`evidence/probe/select.js\` from \`evidence/probe-morphmod.csv\`, probe run \`${runId}\`.`);
lines.push(`Rule: (${rule})${rule === 'ii' ? ' = R4 / kMorphModConversionRule' : ' (NOT the R4 ruling; comparison only)'}.`);
lines.push(`Presets probed: ${presets.size}; candidates: ${candidates.size}; bar ${kBarDb} dB D_nl at E1 and C3; ` +
           'margin = min(contrast_E1, contrast_C3) - 3 dB.');
lines.push('');
lines.push('## Conversions');
lines.push('');
lines.push('| column | preset | defIndex | old primary | margin (dB) | winning candidate | readings |');
lines.push('|---|---|---|---|---|---|---|');
const conv = [...converted.values()].sort((a, b) =>
    kColumns.indexOf(a.column) - kColumns.indexOf(b.column) || a.preset.def - b.preset.def);
for (const c of conv) {
    lines.push(`| ${c.column} | ${c.preset.name} | ${c.preset.def} | ${c.preset.primary} | ${fmt(c.cand.margin)} | ` +
               `${fieldText(c.cand)} | ${noteText(c.cand)} |`);
}
if (conv.length === 0) lines.push('| (none) | | | | | | |');

lines.push('');
lines.push('## Columns without a conversion');
lines.push('');
let newCount = 0;
let stops = 0;
const stdoutNotes = [];
for (const x of kColumns) {
    if (conv.some((c) => c.column === x)) continue;
    const all = [...candidates.values()].filter((c) => c.column === x);
    const carriers = [...carrier.values()].filter((c) => c.column === x);
    if (carriers.length === 0) {
        ++stops;
        let best = null;
        for (const c of all) {
            if (!best || c.minContrast > best.minContrast ||
                (c.minContrast === best.minContrast && c.preset.def < best.preset.def)) best = c;
        }
        const text = best ? `best measured contrast ${fmt(best.minContrast)} dB (min over E1 / C3) on ` +
                            `${best.preset.name} (defIndex ${best.preset.def}), ${fieldText(best)}; ${noteText(best)}`
                          : 'no probe rows';
        lines.push(`- **${x}: NO PASSING CANDIDATE** in any preset (FR-023 stop, STOP AND SURFACE): ${text}`);
        stdoutNotes.push(`NO PASSING CANDIDATE ${x}: ${text}`);
        continue;
    }
    ++newCount;
    const best = [...carriers].sort((a, b) => b.margin - a.margin || a.preset.def - b.preset.def ||
                                              a.order - b.order)[0];
    const why = carriers.map((c) => {
        const p = c.preset;
        const reason = p.single ? 'single-state claimant'
                     : converted.has(p.name) ? `converted to ${converted.get(p.name).column}`
                     : `primary "${p.primary}" has no other primary left`;
        return `${p.name} (${fmt(c.margin)} dB: ${reason})`;
    }).join('; ');
    lines.push(`- **${x}: NEW PRESET NEEDED.** Best probe row: base preset ${best.preset.name} (defIndex ` +
               `${best.preset.def}), margin ${fmt(best.margin)} dB, ${fieldText(best)}; ${noteText(best)}. ` +
               `Carriers, none eligible: ${why}.`);
    stdoutNotes.push(`NEW PRESET NEEDED ${x}: base ${best.preset.name} margin ${fmt(best.margin)} dB ${fieldText(best)}`);
}
if (newCount === 0 && stops === 0) lines.push('(none: every column has a conversion)');

lines.push('');
lines.push('## Carriers per column (every (preset, column) with a candidate passing at both notes)');
lines.push('');
for (const x of kColumns) {
    const list = [...carrier.values()].filter((c) => c.column === x).sort(byMarginThenDef);
    lines.push(`- ${x}: ${list.length === 0 ? '(none)' : list.map((c) =>
        `${c.preset.name} ${fmt(c.margin)}${c.preset.single ? ' [single-state]' : ''}`).join(', ')}`);
}
lines.push('');
fs.writeFileSync(outFile, lines.join('\n'));

for (const c of conv) {
    console.log(`CONVERT ${c.column} ${c.preset.name} (defIndex ${c.preset.def}, old primary "${c.preset.primary}") ` +
                `margin ${fmt(c.cand.margin)} dB ${fieldText(c.cand)}`);
}
for (const n of stdoutNotes) console.log(n);
console.log(`probe select: wrote ${outFile} (rule ${rule}, run ${runId})`);
console.log(`SELECTION ${conv.length} conversions, ${newCount} new`);
process.exit(stops > 0 ? 3 : 0);
