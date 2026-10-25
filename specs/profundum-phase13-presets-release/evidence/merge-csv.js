#!/usr/bin/env node
// Profundum Phase 13 (T016; FR-020 - FR-022; SC-009; plan 5.7): merges the per-group CSVs of a directory.
//
// Usage: node merge-csv.js <dir> <out> <expectedRows> [--group-min]
//
// Reads every *.csv directly under <dir> (sorted by file name), checks that they share one header line, and writes
// the header once followed by every data record to <out>. Records are split with a quoted-CSV scanner, so a quoted
// field that holds a comma or a newline is one record. Exits non-zero (and prints why) unless the merged data-record
// count equals <expectedRows> (102 for the probe table; T018 reuses it for coverage-matrix.csv).
//
// --group-min (coverage-matrix.csv only; SC-007 "the minimum margin per group is reported in coverage-matrix.csv"):
// prepends a "group" field to every record, the C-2.1 group (spec.md C-2.1 table) of the record's "column" field
// (AT-01..03 Core, 04..09 Character, 10..18 Anchor, 19..23 Pitch, 24..25 Envelope, 26..28 Mass, 29..33 Field,
// 34..40 Motion, 41..42 Morph, 43..46 NoteMemory, 47..50 Macros; a reference column sits in the group of the Phase 9
// row its reverse arm runs: R-LOCK (AT-11) and R-SINE (AT-14) Anchor, R-SOLID (AT-29) Field). After the data records
// it appends one record per group: a copy of that group's minimum-margin record with role "group-min". The summary
// records do not count toward <expectedRows>. A column outside the table is an error.
'use strict';

const fs = require('fs');
const path = require('path');

function fail(msg) {
    console.error(`merge-csv: ${msg}`);
    process.exit(1);
}

/// Splits CSV text into raw record strings (quotes kept), dropping CR and empty records.
function splitRecords(text) {
    const records = [];
    let cur = '';
    let quoted = false;
    for (let i = 0; i < text.length; ++i) {
        const c = text[i];
        if (c === '"') {
            quoted = !quoted;   // a doubled quote toggles twice: still balanced
            cur += c;
        } else if (c === '\n' && !quoted) {
            if (cur.length > 0) records.push(cur);
            cur = '';
        } else if (c === '\r' && !quoted) {
            // dropped: records are written with LF
        } else {
            cur += c;
        }
    }
    if (quoted) return null;
    if (cur.length > 0) records.push(cur);
    return records;
}

const argv = process.argv.slice(2);
const groupMin = argv.includes('--group-min');
const args = argv.filter((a) => a !== '--group-min');
if (args.length !== 3) fail('usage: node merge-csv.js <dir> <out> <expectedRows> [--group-min]');
const [dir, out, expectedText] = args;
if (!/^\d+$/.test(expectedText)) fail(`expectedRows must be a non-negative integer, got "${expectedText}"`);
const expected = Number(expectedText);

if (!fs.existsSync(dir) || !fs.statSync(dir).isDirectory()) fail(`not a directory: ${dir}`);
const outAbs = path.resolve(out);
const files = fs
    .readdirSync(dir)
    .filter((f) => f.toLowerCase().endsWith('.csv'))
    .map((f) => path.join(dir, f))
    .filter((f) => path.resolve(f) !== outAbs && fs.statSync(f).isFile())
    .sort();
if (files.length === 0) fail(`no .csv files under ${dir}`);

let header = null;
const rows = [];
for (const f of files) {
    const records = splitRecords(fs.readFileSync(f, 'utf8').replace(/^﻿/, ''));
    if (records === null) fail(`${f}: unbalanced quotes`);
    if (records.length === 0) fail(`${f}: empty (no header)`);
    const [h, ...data] = records;
    if (header === null) {
        header = h;
    } else if (h !== header) {
        fail(`${f}: header differs from ${files[0]}\n  ${h}\n  ${header}`);
    }
    console.log(`merge-csv: ${path.basename(f)} ${data.length} rows`);
    rows.push(...data);
}

/// Splits one record into its raw fields (quotes kept).
function splitFields(record) {
    const fields = [];
    let cur = '';
    let quoted = false;
    for (const c of record) {
        if (c === '"') quoted = !quoted;
        if (c === ',' && !quoted) {
            fields.push(cur);
            cur = '';
        } else {
            cur += c;
        }
    }
    fields.push(cur);
    return fields;
}

const unquote = (f) => (f.startsWith('"') && f.endsWith('"') ? f.slice(1, -1).replace(/""/g, '"') : f);

const kGroupByAt = [
    [1, 3, 'Core'], [4, 9, 'Character'], [10, 18, 'Anchor'], [19, 23, 'Pitch'], [24, 25, 'Envelope'], [26, 28, 'Mass'],
    [29, 33, 'Field'], [34, 40, 'Motion'], [41, 42, 'Morph'], [43, 46, 'NoteMemory'], [47, 50, 'Macros'],
];
const kGroupByRef = { 'R-LOCK': 'Anchor', 'R-SINE': 'Anchor', 'R-SOLID': 'Field' };

function groupOf(column) {
    const at = /^AT-(\d+)\b/.exec(column);
    if (at) {
        const n = Number(at[1]);
        const hit = kGroupByAt.find(([lo, hi]) => n >= lo && n <= hi);
        if (hit) return hit[2];
    }
    const ref = /^(R-[A-Z]+)\b/.exec(column);
    if (ref && kGroupByRef[ref[1]]) return kGroupByRef[ref[1]];
    return null;
}

let outHeader = header;
let outRows = rows;
if (groupMin) {
    const names = splitFields(header).map(unquote);
    const iColumn = names.indexOf('column');
    const iRole = names.indexOf('role');
    const iMargin = names.indexOf('margin');
    if (iColumn < 0 || iRole < 0 || iMargin < 0) fail('--group-min needs header fields column, role and margin');
    const best = new Map();   // group -> { margin, fields }
    outRows = rows.map((r) => {
        const fields = splitFields(r);
        const column = unquote(fields[iColumn] ?? '');
        const group = groupOf(column);
        if (group === null) fail(`--group-min: column "${column}" is in no C-2.1 group`);
        const margin = Number(fields[iMargin]);
        if (!Number.isFinite(margin)) fail(`--group-min: non-numeric margin "${fields[iMargin]}" in ${r}`);
        const cur = best.get(group);
        if (cur === undefined || margin < cur.margin) best.set(group, { margin, fields });
        return `"${group}",${r}`;
    });
    outHeader = `group,${header}`;
    for (const [, , g] of kGroupByAt) {
        const b = best.get(g);
        if (b === undefined) continue;
        const f = [...b.fields];
        f[iRole] = 'group-min';
        outRows.push(`"${g}",${f.join(',')}`);
        console.log(`merge-csv: group-min ${g} margin=${b.margin} (${unquote(b.fields[0])} / ${unquote(b.fields[iColumn])})`);
    }
    console.log(`merge-csv: group-min groups=${best.size}`);
}

fs.mkdirSync(path.dirname(outAbs), { recursive: true });
fs.writeFileSync(outAbs, [outHeader, ...outRows].join('\n') + '\n');

if (rows.length !== expected) {
    fail(`${out}: ${rows.length} rows from ${files.length} files, expected ${expected}`);
}
console.log(`merge-csv: ${out} rows=${rows.length} files=${files.length} OK`);
