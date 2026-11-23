#!/usr/bin/env node
// lint-ruinae-doc-counts.js
// =============================================================================
// Ruinae's user-facing documents quote counts that live in code: the number of
// filter types (RuinaeFilterType), global modulation sources (kNumGlobalSources)
// and factory presets (resources/presets/**). They went stale together
// (audit R-103: "Six filter types" vs 13, "ten modulation sources" vs 14,
// "65+ factory presets" vs 168). This lint reads the code counts and fails on
// every document sentence that disagrees.
//
// Usage: node tools/lint-ruinae-doc-counts.js [--self-test]
// Exit 0 = every quoted count matches the code, 1 = a mismatch, 2 = a count could not be read.
// =============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const ruinae = path.join(root, 'plugins', 'ruinae');

const WORDS = { one: 1, two: 2, three: 3, four: 4, five: 5, six: 6, seven: 7, eight: 8, nine: 9, ten: 10,
    eleven: 11, twelve: 12, thirteen: 13, fourteen: 14, fifteen: 15, sixteen: 16 };

function toNumber(token) {
    const t = token.toLowerCase();
    return Object.prototype.hasOwnProperty.call(WORDS, t) ? WORDS[t] : parseInt(t, 10);
}

function read(rel) {
    return fs.readFileSync(path.join(root, rel), 'utf8');
}

function countFilterTypes() {
    const src = read('plugins/ruinae/src/ruinae_types.h');
    const m = /enum\s+class\s+RuinaeFilterType[^{]*\{([\s\S]*?)\};/.exec(src);
    if (!m) return null;
    const names = m[1].split('\n')
        .map((l) => l.replace(/\/\/.*$/, '').replace(/\/\*[\s\S]*?\*\//g, '').trim())
        .filter(Boolean)
        .map((l) => l.replace(/[,\s].*$/, '').replace(/=.*$/, ''));
    const idx = names.indexOf('NumTypes');
    return idx < 0 ? null : idx;
}

function countGlobalSources() {
    const m = /kNumGlobalSources\s*=\s*(\d+)\s*;/.exec(read('plugins/shared/src/ui/mod_matrix_types.h'));
    return m ? parseInt(m[1], 10) : null;
}

function countPresets() {
    let n = 0;
    const walk = (dir) => {
        for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
            const p = path.join(dir, e.name);
            if (e.isDirectory()) walk(p);
            else if (e.name.endsWith('.vstpreset')) n++;
        }
    };
    walk(path.join(ruinae, 'resources', 'presets'));
    return n;
}

// Each rule: a regex whose group 1 is the quoted count (digits or a number word) and the code count it must equal.
function rules(counts) {
    const num = '(\\d+|[A-Za-z]+)';
    return [
        { what: 'filter types', want: counts.filters,
          re: [new RegExp(`${num}\\s+(?:filter types|Filter Types)`, 'g')] },
        { what: 'global modulation sources', want: counts.sources,
          re: [new RegExp(`houses ${num} different modulation sources`, 'g'),
               new RegExp(`any of the ${num} sources listed above`, 'g'),
               new RegExp(`\\*\\*${num} global sources\\*\\*`, 'g'),
               new RegExp(`${num} sources routed to`, 'g')] },
        { what: 'factory presets', want: counts.presets,
          re: [new RegExp(`${num}\\+?\\s+factory presets`, 'g')] },
    ];
}

function check(counts, docs) {
    const problems = [];
    for (const rule of rules(counts)) {
        for (const [file, text] of docs) {
            for (const re of rule.re) {
                re.lastIndex = 0;
                let m;
                while ((m = re.exec(text)) !== null) {
                    const got = toNumber(m[1]);
                    if (Number.isNaN(got)) continue; // not a count word ("the filter types")
                    if (got !== rule.want) {
                        problems.push(`${file}: says ${m[0].trim()} (${got}), code has ${rule.want} ${rule.what}`);
                    }
                }
            }
        }
    }
    return problems;
}

function selfTest() {
    const counts = { filters: 13, sources: 14, presets: 168 };
    const bad = [['d', 'Six filter types are available. houses ten different modulation sources. 65+ factory presets.']];
    const good = [['d', '13 filter types. houses fourteen different modulation sources. 168 factory presets. the filter types here']];
    const nBad = check(counts, bad).length;
    const nGood = check(counts, good).length;
    if (nBad !== 3 || nGood !== 0) {
        console.error(`self-test FAILED: bad doc found ${nBad} problems (want 3), good doc found ${nGood} (want 0)`);
        process.exit(1);
    }
    console.log('self-test ok');
}

function main() {
    if (process.argv.includes('--self-test')) { selfTest(); return; }
    const counts = { filters: countFilterTypes(), sources: countGlobalSources(), presets: countPresets() };
    if (!counts.filters || !counts.sources || !counts.presets) {
        console.error(`could not read a code count: ${JSON.stringify(counts)}`);
        process.exit(2);
    }
    const docFiles = ['plugins/ruinae/MANUAL.md', 'plugins/ruinae/README.md', 'plugins/ruinae/docs/index.html'];
    const docs = docFiles.map((f) => [f, read(f)]);
    const problems = check(counts, docs);
    if (problems.length) {
        console.error(`lint-ruinae-doc-counts: ${problems.length} stale count(s) (code: ${JSON.stringify(counts)})`);
        for (const p of problems) console.error('  ' + p);
        process.exit(1);
    }
    console.log(`lint-ruinae-doc-counts: ok (${JSON.stringify(counts)})`);
}

main();
