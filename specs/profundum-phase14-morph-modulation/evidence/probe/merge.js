#!/usr/bin/env node
// Profundum Phase 14 (T021; FR-044; SC-017 (a) input; plan 5.7 step 1): merges the 32 Morph Mod probe shards.
//
// Usage: node merge.js
//
// Reads, next to this script, shard-0.csv .. shard-31.csv (written by the hidden cases
// Profundum_MorphModProbe_<k>of32, plugins/profundum/tests/integration/preset_probe_test.cpp) and writes one level up
// (the phase's evidence directory) probe-morphmod.csv: one header, a leading runId column on every row
// (runId = <git short hash>-<UTC yyyymmddThhmm>), rows ordered by defIndex with each shard's row order kept (the
// candidate order of plan 5.7 step 1, E1 before C3 per preset; select.js breaks candidate ties by this order).
// Fails (exit 1, the reason on stderr) when a shard file is missing or unreadable, the headers differ, a row has the
// wrong field count, a preset appears in two shards, or a preset sits in a shard other than defIndex % 32.
// A header-only shard (no definition with that index) is accepted.
// Last line on success: "PROBE MERGED runId=<id> presets=<n> rows=<m>".
'use strict';

const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const kShards = 32;
const inDir = __dirname;
const outDir = path.join(__dirname, '..');
const outFile = path.join(outDir, 'probe-morphmod.csv');

function fail(msg) {
    console.error(`probe merge: ${msg}`);
    process.exit(1);
}

/// Splits CSV text into raw record strings (quotes kept), dropping CR and empty records; null on unbalanced quotes.
function splitRecords(text) {
    const records = [];
    let cur = '';
    let quoted = false;
    for (const c of text) {
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

/// Splits one record into its fields, unquoted.
function splitFields(record) {
    const fields = [];
    let cur = '';
    let quoted = false;
    for (let i = 0; i < record.length; ++i) {
        const c = record[i];
        if (c === '"') {
            if (quoted && record[i + 1] === '"') {
                cur += '"';
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (c === ',' && !quoted) {
            fields.push(cur);
            cur = '';
        } else {
            cur += c;
        }
    }
    fields.push(cur);
    return fields;
}

function runIdNow() {
    let hash;
    try {
        hash = execSync('git rev-parse --short HEAD', { cwd: inDir, encoding: 'utf8' }).trim();
    } catch (e) {
        fail(`git rev-parse failed: ${e.message}`);
    }
    if (!/^[0-9a-f]{4,}$/.test(hash)) fail(`unexpected git short hash "${hash}"`);
    const d = new Date();
    const p2 = (n) => String(n).padStart(2, '0');
    const stamp = `${d.getUTCFullYear()}${p2(d.getUTCMonth() + 1)}${p2(d.getUTCDate())}T` +
                  `${p2(d.getUTCHours())}${p2(d.getUTCMinutes())}`;
    return `${hash}-${stamp}`;
}

let header = null;
let cPreset = -1;
let cDef = -1;
const rows = [];   // { raw, def, seq }
const owner = new Map();   // preset -> shard
for (let k = 0; k < kShards; ++k) {
    const file = path.join(inDir, `shard-${k}.csv`);
    if (!fs.existsSync(file)) fail(`missing shard ${file}`);
    const records = splitRecords(fs.readFileSync(file, 'utf8').replace(/^﻿/, ''));
    if (records === null) fail(`${file}: unbalanced quotes`);
    if (records.length < 1) fail(`${file}: empty (no header)`);
    const [h, ...data] = records;
    if (header === null) {
        header = h;
        const cols = splitFields(h);
        cPreset = cols.indexOf('preset');
        cDef = cols.indexOf('defIndex');
        if (cPreset < 0 || cDef < 0) fail(`${file}: header lacks preset / defIndex: ${h}`);
        if (cols.includes('runId')) fail(`${file}: shard header already carries runId`);
    } else if (h !== header) {
        fail(`${file}: header differs from shard-0.csv\n  ${h}\n  ${header}`);
    }
    const width = splitFields(header).length;
    for (const raw of data) {
        const f = splitFields(raw);
        if (f.length !== width) fail(`${file}: ${f.length} fields, header has ${width}: ${raw}`);
        const def = Number(f[cDef]);
        if (!Number.isInteger(def) || def < 0) fail(`${file}: bad defIndex "${f[cDef]}"`);
        if (def % kShards !== k) fail(`${file}: preset "${f[cPreset]}" defIndex ${def} belongs to shard ${def % kShards}`);
        const prev = owner.get(f[cPreset]);
        if (prev !== undefined && prev !== k) fail(`preset "${f[cPreset]}" appears in shards ${prev} and ${k}`);
        owner.set(f[cPreset], k);
        rows.push({ raw, def, seq: rows.length });
    }
    console.log(`probe merge: ${path.basename(file)} ${data.length} rows`);
}
if (rows.length === 0) fail('no data rows in any shard');

rows.sort((a, b) => a.def - b.def || a.seq - b.seq);
const runId = runIdNow();
const lines = [`runId,${header}`, ...rows.map((r) => `${runId},${r.raw}`)];
fs.writeFileSync(outFile, lines.join('\n') + '\n');
console.log(`probe merge: wrote ${outFile} rows=${rows.length}`);
console.log(`PROBE MERGED runId=${runId} presets=${owner.size} rows=${rows.length}`);
