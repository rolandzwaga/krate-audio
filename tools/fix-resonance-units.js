// Remap mis-scaled filter.resonance values in the Ruinae preset generator.
//
// The preset field is a raw Q in [0.1, 30] (filter_params.h: "0.1-30.0"), but
// most presets were authored as if it were a normalized 0-1 fraction. A written
// 0.3 therefore produced Q = 0.3 -- below Butterworth, so no resonant peak at
// all across nearly the whole bank.
//
// Values <= 1.0 are treated as the intended normalized fraction and mapped onto
// a musical Q range. Values above 1.0 are already real Q (self-oscillating
// filter presets at 18-25, envelope filter, comb) and are left alone.
//
// One-shot migration; kept in-tree as the record of how the values were chosen.

const fs = require('fs');

const GENERATOR = 'tools/ruinae_preset_generator.cpp';

const Q_MIN = 0.7;   // Butterworth: the flat, non-resonant baseline
const Q_MAX = 12.0;  // strongly resonant, still clear of self-oscillation

// Matches `.filter.resonance = <number>f`. Case-sensitive, so the unrelated
// `.globalFilter.resonance` (already authored with correct Q values) is skipped.
const PATTERN = /\.filter\.resonance\s*=\s*([0-9]*\.?[0-9]+)f/g;

const source = fs.readFileSync(GENERATOR, 'utf8');

let changed = 0;
let skipped = 0;

const updated = source.replace(PATTERN, (match, numberText) => {
    const written = parseFloat(numberText);

    if (written > 1.0) {
        skipped++;
        return match;
    }

    const q = Q_MIN + written * (Q_MAX - Q_MIN);
    // Always emit a decimal point: a whole number would render as "12f", which
    // C++ reads as an invalid literal suffix rather than a float.
    const formatted = q.toFixed(2);
    changed++;

    return match.replace(`${numberText}f`, `${formatted}f`);
});

fs.writeFileSync(GENERATOR, updated);

console.log(`remapped ${changed} value(s) onto Q [${Q_MIN}, ${Q_MAX}]`);
console.log(`left ${skipped} value(s) above 1.0 untouched (already real Q)`);
