#!/usr/bin/env node
// Profundum Phase 14 (T003; FR-062; plan 5.8; tasks Deviation 2): merges the four pre-phase harvest shards.
//
// Usage: node merge.js
//
// Reads, next to this script, fingerprints-0..3.csv and decoded-0..3.csv (written by the hidden cases
// Profundum_MorphModPrePhaseHarvest_<k>of4, plugins/profundum/tests/integration/morph_mod_preset_test.cpp) and writes
// one level up (the phase's evidence directory):
//   prephase-fingerprints.csv  one header, rows sorted by preset, note, channel
//   prephase-decoded.csv       one header, rows sorted by preset, offset
// Fails (exit 1, the reason on stderr) when a shard file is missing or empty, the shards' headers differ, a preset
// appears in two shards, or a shard's fingerprint and decoded files name different presets.
// Last line on success: "PREPHASE MERGED presets=<n> rows=<m> (fingerprints=<a> decoded=<b>)", m = a + b.
'use strict';

const fs = require('fs');
const path = require('path');

const kShards = 4;
const inDir = __dirname;
const outDir = path.join(__dirname, '..');

function fail(msg) {
    console.error(`prephase merge: ${msg}`);
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

/// Reads one kind's four shards: { header, rows: [{ raw, fields, shard }] }.
function readShards(kind) {
    let header = null;
    const rows = [];
    for (let k = 0; k < kShards; ++k) {
        const file = path.join(inDir, `${kind}-${k}.csv`);
        if (!fs.existsSync(file)) fail(`missing shard ${file}`);
        const records = splitRecords(fs.readFileSync(file, 'utf8').replace(/^﻿/, ''));
        if (records === null) fail(`${file}: unbalanced quotes`);
        if (records.length < 2) fail(`${file}: no data rows`);
        const [h, ...data] = records;
        if (header === null) {
            header = h;
        } else if (h !== header) {
            fail(`${file}: header differs from ${kind}-0.csv\n  ${h}\n  ${header}`);
        }
        for (const raw of data) rows.push({ raw, fields: splitFields(raw), shard: k });
        console.log(`prephase merge: ${path.basename(file)} ${data.length} rows`);
    }
    return { header, rows };
}

/// preset -> shard; fails when a preset appears in two shards.
function presetShards(kind, rows) {
    const owner = new Map();
    for (const r of rows) {
        const preset = r.fields[0];
        const prev = owner.get(preset);
        if (prev !== undefined && prev !== r.shard) {
            fail(`${kind}: preset "${preset}" appears in shards ${prev} and ${r.shard}`);
        }
        owner.set(preset, r.shard);
    }
    return owner;
}

const cmp = (a, b) => (a < b ? -1 : a > b ? 1 : 0);

const fp = readShards('fingerprints');
const dec = readShards('decoded');
const fpOwner = presetShards('fingerprints', fp.rows);
const decOwner = presetShards('decoded', dec.rows);
for (const [preset, shard] of fpOwner) {
    if (decOwner.get(preset) !== shard) fail(`preset "${preset}": fingerprints in shard ${shard}, decoded in shard ${decOwner.get(preset)}`);
}
for (const [preset] of decOwner) {
    if (!fpOwner.has(preset)) fail(`preset "${preset}" has decoded rows but no fingerprint rows`);
}

// preset,note,channel,...
fp.rows.sort((a, b) => cmp(a.fields[0], b.fields[0]) || Number(a.fields[1]) - Number(b.fields[1]) || cmp(a.fields[2], b.fields[2]));
// preset,field,offset,length,hex
dec.rows.sort((a, b) => cmp(a.fields[0], b.fields[0]) || Number(a.fields[2]) - Number(b.fields[2]));

const write = (name, header, rows) => {
    const file = path.join(outDir, name);
    fs.writeFileSync(file, [header, ...rows.map((r) => r.raw)].join('\n') + '\n');
    console.log(`prephase merge: wrote ${file} rows=${rows.length}`);
};
write('prephase-fingerprints.csv', fp.header, fp.rows);
write('prephase-decoded.csv', dec.header, dec.rows);

console.log(`PREPHASE MERGED presets=${fpOwner.size} rows=${fp.rows.length + dec.rows.length} ` +
            `(fingerprints=${fp.rows.length} decoded=${dec.rows.length})`);
