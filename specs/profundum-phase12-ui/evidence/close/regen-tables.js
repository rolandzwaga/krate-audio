#!/usr/bin/env node
// ==============================================================================
// Profundum Phase 12 - regen-tables.js (T033; FR-005, FR-007; plan 3.2)
// ==============================================================================
// Helper of regen-tables.sh. Reads the Profundum_UiContractTables probe log, writes the two CSVs next to it
// (d3-macro-rows.csv, d4-law-table.csv) and rewrites the GENERATED blocks of D3 / D4 from them.
//
//   node regen-tables.js <probe.log> <csvOutDir> <macro-mapping.md> <morph-laws.md>
//
// The data columns come from the probe. The doc-only columns the probe does not print are kept from the
// existing block, by row:
//   D3: Unit (kept when the row's target is unchanged), the label after the registered ID number (kept when the
//       number is unchanged), the Flags text (kept when its flag set - inert / no control - is unchanged).
//   D4: the doc keeps its header and fence; a lo / hi cell keeps its existing spelling when it is the same float32
//       value as the probe's (so 0.1 is not rewritten as 0.100000001); an "(open)" mark from the probe is kept.
// Last stdout line: IDENTICAL | CHANGED. Exit 2 on a malformed log or doc.
// ==============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

function fail(msg) {
    console.error(`[regen-tables.js] ${msg}`);
    process.exit(2);
}

const [logPath, outDir, d3Path, d4Path] = process.argv.slice(2);
if (!logPath || !outDir || !d3Path || !d4Path) {
    fail('usage: node regen-tables.js <probe.log> <csvOutDir> <macro-mapping.md> <morph-laws.md>');
}

const D3_HEADER = 'row,macro,target,amount,curve,registered_id,no_control,inert';
const D4_HEADER = 'leaf,name,law,axis,lo,hi,registered_id';
const D3_ROWS = 16;
const D4_ROWS = 62;

// ---- 1. split the probe log ----
const logLines = fs.readFileSync(logPath, 'utf8').split(/\r?\n/);
function extractCsv(marker, header, expectedRows) {
    const at = logLines.findIndex((l) => l.startsWith(marker));
    if (at < 0) fail(`marker "${marker}" not in ${logPath}`);
    if (logLines[at + 1] !== header) fail(`unexpected header after "${marker}": ${logLines[at + 1]}`);
    const rows = [];
    for (let i = at + 2; i < logLines.length && /^\d+,/.test(logLines[i]); ++i) rows.push(logLines[i]);
    if (rows.length !== expectedRows) fail(`${marker}: ${rows.length} rows, expected ${expectedRows}`);
    return rows;
}
const d3Csv = extractCsv('# D3 GENERATED', D3_HEADER, D3_ROWS);
const d4Csv = extractCsv('# D4 GENERATED', D4_HEADER, D4_ROWS);
fs.mkdirSync(outDir, { recursive: true });
fs.writeFileSync(path.join(outDir, 'd3-macro-rows.csv'), [D3_HEADER, ...d3Csv, ''].join('\n'));
fs.writeFileSync(path.join(outDir, 'd4-law-table.csv'), [D4_HEADER, ...d4Csv, ''].join('\n'));

// ---- helpers ----
const toAscii = (t) => t.replace(/−/g, '-').trim();
function sameFloat(docText, probeText) {
    const a = Number(toAscii(docText));
    const b = Number(toAscii(probeText));
    return docText.trim() !== '' && Number.isFinite(a) && Number.isFinite(b) && Math.fround(a) === Math.fround(b);
}

/** Returns { eol, before, inner (array of lines), after } around one GENERATED block. */
function splitBlock(text, beginMarker, docPath) {
    const eol = text.includes('\r\n') ? '\r\n' : '\n';
    const b = text.indexOf(beginMarker);
    if (b < 0) fail(`${docPath}: "${beginMarker}" not found`);
    const innerStart = text.indexOf(eol, b) + eol.length;
    const e = text.indexOf('<!-- GENERATED-END -->', innerStart);
    if (e < 0) fail(`${docPath}: no GENERATED-END after "${beginMarker}"`);
    const innerText = text.slice(innerStart, e);
    const inner = innerText.split(eol);
    if (inner.length > 0 && inner[inner.length - 1] === '') inner.pop();
    return { eol, before: text.slice(0, innerStart), inner, after: text.slice(e) };
}
const joinBlock = (blk, lines) => blk.before + lines.map((l) => l + blk.eol).join('') + blk.after;

// ---- 2. D3: the macro-rows markdown table ----
function regenD3(docPath) {
    const text = fs.readFileSync(docPath, 'utf8');
    const blk = splitBlock(text, '<!-- GENERATED-BEGIN macro-rows -->', docPath);
    if (blk.inner.length < 2) fail(`${docPath}: macro-rows block has no table header`);
    const [head, sep] = blk.inner;
    const existing = new Map();
    for (const line of blk.inner.slice(2)) {
        const cells = line.split('|').slice(1, -1).map((c) => c.trim());
        if (cells.length === 8) existing.set(cells[0], cells);
    }
    const out = [head, sep];
    for (const line of d3Csv) {
        const [row, macro, target, amount, curve, id, noControl, inert] = line.split(',');
        const old = existing.get(row);
        const amountCell = old && sameFloat(old[3], amount) ? old[3] : amount.replace(/^-/, '−');
        const unitCell = old && old[2] === target ? old[5] : 'UNIT NOT RECORDED - review';
        let idCell;
        if (noControl === 'yes') {
            idCell = '—';
        } else if (old && (old[6] === id || old[6].startsWith(`${id} `))) {
            idCell = old[6];
        } else {
            idCell = id;
        }
        const wantInert = inert === 'yes';
        const wantNoCtl = noControl === 'yes';
        let flagsCell;
        if (old && /inert/.test(old[7]) === wantInert && /no control/.test(old[7]) === wantNoCtl) {
            flagsCell = old[7];
        } else {
            const flags = [];
            if (wantNoCtl) flags.push('**no control**');
            if (wantInert) flags.push('**inert** (amount 0)');
            flagsCell = flags.join('; ');
        }
        const cells = [row, macro, target, amountCell, curve, unitCell, idCell, flagsCell];
        out.push('|' + cells.map((c) => ' ' + c + (c ? ' ' : '')).join('|') + '|');
    }
    return { text, next: joinBlock(blk, out) };
}

// ---- 3. D4: the law-table CSV inside a ```csv fence ----
function regenD4(docPath) {
    const text = fs.readFileSync(docPath, 'utf8');
    const blk = splitBlock(text, '<!-- GENERATED-BEGIN law-table -->', docPath);
    const lines = blk.inner;
    if (lines.length < 3 || !lines[0].startsWith('```') || lines[lines.length - 1] !== '```') {
        fail(`${docPath}: law-table block is not a fenced CSV`);
    }
    const docHeader = lines[1];
    if (docHeader !== 'leaf,law,axis,lo,hi,id') fail(`${docPath}: unexpected law-table header: ${docHeader}`);
    const existing = new Map();
    for (const line of lines.slice(2, -1)) {
        const cells = line.split(',');
        if (cells.length === 6) existing.set(cells[0], cells);
    }
    const out = [lines[0], docHeader];
    for (const line of d4Csv) {
        const [, name, law, axis, lo, hiRaw, id] = line.split(',');
        const open = hiRaw.endsWith(' (open)');
        const hi = open ? hiRaw.slice(0, -' (open)'.length) : hiRaw;
        const old = existing.get(name);
        const loCell = old && sameFloat(old[3], lo) ? old[3] : lo;
        const oldHi = old ? old[4].replace(/ \(open\)$/, '') : '';
        const hiCell = (old && sameFloat(oldHi, hi) ? oldHi : hi) + (open ? ' (open)' : '');
        out.push([name, law, axis, loCell, hiCell, id].join(','));
    }
    out.push(lines[lines.length - 1]);
    return { text, next: joinBlock(blk, out) };
}

// ---- 4. write and report ----
let changed = false;
for (const [docPath, regen] of [[d3Path, regenD3], [d4Path, regenD4]]) {
    const { text, next } = regen(docPath);
    if (next === text) {
        console.log(`${path.basename(docPath)}: identical`);
    } else {
        fs.writeFileSync(docPath, next);
        changed = true;
        const a = text.split(/\r?\n/);
        const b = next.split(/\r?\n/);
        let diffs = 0;
        for (let i = 0; i < Math.max(a.length, b.length); ++i) if (a[i] !== b[i]) ++diffs;
        console.log(`${path.basename(docPath)}: changed (${diffs} line(s) differ)`);
    }
}
console.log(changed ? 'CHANGED' : 'IDENTICAL');
