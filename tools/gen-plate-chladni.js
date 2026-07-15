#!/usr/bin/env node
// ==============================================================================
// gen-plate-chladni.js -- generate the free-plate Chladni modal table for
// plate_modes.h (Membrum). Referenced by the header comment; committed here so
// the table can be regenerated / extended deterministically.
//
// Free-plate power law (Rossing & Peterson cymbal exponent):
//   chladni(m,n) = (m + 2n)^P * (1 + kappa*n)
// with P = 1.7, kappa = 0.11. m = nodal diameters, n = nodal circles.
// Rigid-body modes (0,0) translation and (1,0) tilt are excluded; the
// fundamental is (2,0). Modes are sorted ascending by chladni value (ties
// broken by m then n for determinism) and normalized to the (2,0) fundamental.
//
// Usage:  node tools/gen-plate-chladni.js [count]
//   count defaults to 96. Prints the two C++ arrays (indices + ratios).
// ==============================================================================
'use strict';

const P = 1.7;
const KAPPA = 0.11;
const count = parseInt(process.argv[2] || '96', 10);

function chladni(m, n) {
  return Math.pow(m + 2 * n, P) * (1 + KAPPA * n);
}

// Enumerate a generous (m,n) grid, exclude rigid-body modes, sort, take `count`.
const modes = [];
for (let m = 0; m <= 40; ++m) {
  for (let n = 0; n <= 40; ++n) {
    if (m === 0 && n === 0) continue; // translation
    if (m === 1 && n === 0) continue; // tilt
    modes.push({ m, n, c: chladni(m, n) });
  }
}
modes.sort((a, b) => (a.c - b.c) || (a.m - b.m) || (a.n - b.n));
const sel = modes.slice(0, count);

const f0 = chladni(2, 0); // fundamental normalization
const ratios = sel.map((e) => e.c / f0);

// ---- Emit C++ ---------------------------------------------------------------
function fmtIndices() {
  const lines = [];
  for (let i = 0; i < sel.length; i += 8) {
    const row = sel.slice(i, i + 8)
      .map((e) => `{${e.m}, ${e.n}}`).join(', ');
    lines.push('    ' + row + ',');
  }
  return lines.join('\n');
}
function fmtRatios() {
  const lines = [];
  for (let i = 0; i < ratios.length; i += 8) {
    const row = ratios.slice(i, i + 8)
      .map((r) => r.toFixed(4).padStart(9) + 'f').join(', ');
    lines.push('    ' + row + ',');
  }
  return lines.join('\n');
}

console.log(`// count = ${sel.length}, P = ${P}, kappa = ${KAPPA}`);
console.log(`inline constexpr PlateModeIndices kPlateIndices[kPlateMaxModeCount] = {`);
console.log(fmtIndices());
console.log(`};`);
console.log(``);
console.log(`inline constexpr float kPlateRatios[kPlateMaxModeCount] = {`);
console.log(fmtRatios());
console.log(`};`);
