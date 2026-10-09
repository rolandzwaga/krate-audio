#!/usr/bin/env node
// ==============================================================================
// gen-vorago-material-tables.js -- generate the three new modal ratio tables
// that Vorago Phase 10 appends to ContinuousBody (dsp/include/krate/dsp/systems/
// continuous_body.h, below kPlateRatios at :705):
//
//   kRoomModeRatios      rectangular-room eigenmodes   (StoneChamber, CavernWall)
//   kClampedPlateRatios  clamped-edge circular plate   (SteelTank)
//   kBarRatios           free-free flexural bar        (CathedralColumn)
//
// Spec:  specs/vorago-phase10-voice-engine/spec.md  FR-033, FR-034
// Plan:  specs/vorago-phase10-voice-engine/plan.md  S4.1
//
// The tables are GENERATED, never typed. The output below is pasted into the
// header as `static constexpr std::array<float, kModeCountCeiling>` literals
// (never #include'd), exactly as kGlassRatios and kPlateRatios are literals
// today (continuous_body.h:678-711).
//
// THIS SCRIPT IS ITS OWN TEST. The assertion block is written first and runs on
// every invocation:
//   * every table is strictly ascending over all 32 PRINTED (4-decimal) values
//     -- the property continuous_body.h:669-671 says makes FR-043's Nyquist
//     *prefix* truncation exact, and the property the header's
//     `static_assert(ratioTableStrictlyAscending(...))` re-checks at compile time;
//   * r[0] prints as exactly 1.0000;
//   * every consecutive room-mode gap is >= 1.015 (the 1.5 % thinning rule).
// A negative self-check feeds a deliberately UN-THINNED room table through the
// same assertions and requires them to fire; if they do not, the script exits
// non-zero, because assertions that cannot fail are not assertions.
//
// Usage:  node tools/gen-vorago-material-tables.js
//   Exit 0 and three paste-ready blocks on stdout; non-zero and a reason on
//   stderr if any table is bad.
// ==============================================================================
'use strict';

/// Mirrors ContinuousBody::kModeCountCeiling (continuous_body.h) -- every table
/// is exactly this long.
const kModeCountCeiling = 32;

/// FR-033 / plan S4.1: the room series is thinned to a minimum spacing of 1.5 %
/// so strict ascent survives the 4-decimal quantisation with a wide margin
/// (1.5 % of 1.0 is 0.015, 150x the 1e-4 print resolution).
const kRoomMinGapRatio = 1.015;

// =============================================================================
// ASSERTIONS -- written before the generators, on purpose.
// =============================================================================

class TableAssertionError extends Error {}

function bad(name, msg) {
  throw new TableAssertionError(name + ': ' + msg);
}

/// The 4-decimal fixed rendering that lands in the header. Everything the C++
/// static_assert sees is this, not the double the generator computed, so the
/// assertions below run on this.
function printed(v) {
  return v.toFixed(4);
}

/**
 * @param {string} name    table name, for the failure message
 * @param {number[]} exact the generated doubles, length kModeCountCeiling
 * @param {{minGapRatio?: number}} opts
 */
function assertTable(name, exact, opts) {
  const minGapRatio = opts && opts.minGapRatio;

  if (exact.length !== kModeCountCeiling) {
    bad(name, 'expected ' + kModeCountCeiling + ' entries, got ' + exact.length);
  }
  for (let i = 0; i < exact.length; ++i) {
    if (!Number.isFinite(exact[i])) {
      bad(name, 'entry ' + i + ' is not finite (' + exact[i] + ')');
    }
  }

  // (1) r[0] == 1.0000 exactly, as printed.
  if (printed(exact[0]) !== '1.0000') {
    bad(name, 'r[0] must print as 1.0000, got ' + printed(exact[0]));
  }

  // (2) Strict ascent over all 32 PRINTED values -- the header's static_assert
  //     operates on the literals, so this must hold after rounding, not before.
  const rounded = exact.map(function (v) { return Number(printed(v)); });
  for (let i = 1; i < rounded.length; ++i) {
    if (!(rounded[i] > rounded[i - 1])) {
      bad(name, 'not strictly ascending at printed index ' + i + ': '
        + printed(exact[i - 1]) + 'f then ' + printed(exact[i]) + 'f');
    }
  }

  // (3) Minimum spacing, checked on the EXACT values because that is what the
  //     thinning law is stated over. (Rounding to 4 decimals perturbs a ratio
  //     near 1.015 by at most ~1e-4, so checking the printed values here would
  //     be testing the printer, not the law.)
  if (minGapRatio !== undefined) {
    for (let i = 1; i < exact.length; ++i) {
      const gap = exact[i] / exact[i - 1];
      if (!(gap >= minGapRatio)) {
        bad(name, 'gap ' + gap.toFixed(6) + ' at index ' + i
          + ' is below the ' + minGapRatio + ' minimum-spacing rule ('
          + printed(exact[i - 1]) + 'f -> ' + printed(exact[i]) + 'f)');
      }
    }
  }
}

// =============================================================================
// GENERATORS
// =============================================================================

// ---- kRoomModeRatios --------------------------------------------------------
//
// Rayleigh's rectangular-room eigenvalue equation (Kuttruff, Room Acoustics
// section 3.1):
//
//   f(nx,ny,nz) = (c/2) * sqrt((nx/Lx)^2 + (ny/Ly)^2 + (nz/Lz)^2)
//
// over nx,ny,nz in [0,4], excluding (0,0,0). Because the series is normalised
// by its own first entry, c and the absolute room size both cancel: only the
// PROPORTIONS matter. Sepmeyer's low-degeneracy set 1 : 1.26 : 1.59 is used,
// which is exactly why the result is not a degenerate comb.

/// Room proportions Lx : Ly : Lz (Sepmeyer). Normalisation cancels the scale.
const kRoomProportions = [1.0, 1.26, 1.59];
const kRoomModeOrderMax = 4;

/**
 * @param {number} minGapRatio 1.0 disables thinning (used by the self-check).
 * @returns {{ratios: number[], modes: Array<Array<number>>}}
 */
function roomModeSeries(minGapRatio) {
  const lx = kRoomProportions[0];
  const ly = kRoomProportions[1];
  const lz = kRoomProportions[2];

  const raw = [];
  for (let nx = 0; nx <= kRoomModeOrderMax; ++nx) {
    for (let ny = 0; ny <= kRoomModeOrderMax; ++ny) {
      for (let nz = 0; nz <= kRoomModeOrderMax; ++nz) {
        if (nx === 0 && ny === 0 && nz === 0) continue; // not a mode
        const f = Math.sqrt(
          (nx / lx) * (nx / lx) + (ny / ly) * (ny / ly) + (nz / lz) * (nz / lz));
        raw.push({ nx: nx, ny: ny, nz: nz, f: f });
      }
    }
  }

  // Ascending, ties broken by the index triple so the output is deterministic.
  raw.sort(function (a, b) {
    return (a.f - b.f) || (a.nx - b.nx) || (a.ny - b.ny) || (a.nz - b.nz);
  });

  const f0 = raw[0].f; // normalise by the first -> r[0] = 1.0 exactly
  const ratios = [];
  const modes = [];
  for (let i = 0; i < raw.length; ++i) {
    const r = raw[i].f / f0;
    if (ratios.length === 0 || r / ratios[ratios.length - 1] >= minGapRatio) {
      ratios.push(r);
      modes.push([raw[i].nx, raw[i].ny, raw[i].nz]);
    }
  }
  return { ratios: ratios, modes: modes };
}

// ---- kClampedPlateRatios ----------------------------------------------------
//
// Clamped-edge circular plate: Leissa, Vibration of Plates (NASA SP-160),
// Table 4.4; Fletcher & Rossing, The Physics of Musical Instruments, ch. 3.
// The eight published values verbatim, then a constant-modal-density LINEAR
// continuation of the least-squares slope taken over the published k = 4..8,
// anchored at k = 8 -- exactly the technique kPlateRatios documents for the
// free plate (continuous_body.h:690-711), because a thin plate's modal density
// is asymptotically constant (Cremer & Heckl).

const kClampedPlatePublished = [
  1.000, 2.080, 3.410, 3.890, 5.000, 5.950, 6.820, 8.280,
];
/// 1-based mode indices the LSQ slope is fitted over (the published tail).
const kClampedPlateFitFrom = 4;
const kClampedPlateFitTo = 8;

/// Least-squares slope of r against the 1-based mode index k over [from, to].
function lsqSlope(values, from, to) {
  const n = to - from + 1;
  let meanK = 0;
  let meanR = 0;
  for (let k = from; k <= to; ++k) {
    meanK += k;
    meanR += values[k - 1];
  }
  meanK /= n;
  meanR /= n;

  let num = 0;
  let den = 0;
  for (let k = from; k <= to; ++k) {
    num += (k - meanK) * (values[k - 1] - meanR);
    den += (k - meanK) * (k - meanK);
  }
  return num / den;
}

function clampedPlateSeries() {
  const slope = lsqSlope(
    kClampedPlatePublished, kClampedPlateFitFrom, kClampedPlateFitTo);
  const ratios = kClampedPlatePublished.slice();
  const anchorK = kClampedPlatePublished.length;      // k = 8
  const anchor = kClampedPlatePublished[anchorK - 1]; // r[8] = 8.280
  for (let k = anchorK + 1; k <= kModeCountCeiling; ++k) {
    ratios.push(anchor + slope * (k - anchorK));
  }
  return { ratios: ratios, slope: slope };
}

// ---- kBarRatios -------------------------------------------------------------
//
// Free-free flexural bar / column (Fletcher & Rossing, ch. 2 -- the series a
// marimba bar is tuned AWAY from):
//
//   r[n] = (beta_n / beta_1)^2
//
// with the five tabulated roots of cos(beta)cosh(beta) = 1 and the asymptotic
// beta_n = (2n+1)*pi/2 for n >= 6.

const kBarBetaTabulated = [4.73004, 7.85320, 10.99561, 14.13717, 17.27876];

function barBeta(n) {
  if (n <= kBarBetaTabulated.length) return kBarBetaTabulated[n - 1];
  return ((2 * n + 1) * Math.PI) / 2;
}

function barSeries() {
  const beta1 = barBeta(1);
  const ratios = [];
  for (let n = 1; n <= kModeCountCeiling; ++n) {
    const q = barBeta(n) / beta1;
    ratios.push(q * q);
  }
  return ratios;
}

// =============================================================================
// EMIT
// =============================================================================

/**
 * Renders the initialiser rows in continuous_body.h's house style: four cells
 * per row, cells left-aligned at a common width (widest cell + 1), a trailing
 * `//` on every row but the last so clang-format cannot reflow the block.
 * @param {number[]} values
 * @param {Object} rowNotes row index -> trailing comment text
 */
function fmtRows(values, rowNotes) {
  const notes = rowNotes || {};
  const cells = values.map(function (v) { return printed(v) + 'f,'; });
  let width = 0;
  for (let i = 0; i < cells.length; ++i) {
    width = Math.max(width, cells[i].length);
  }
  width += 1;

  const lines = [];
  const rowCount = Math.ceil(cells.length / 4);
  for (let row = 0; row < rowCount; ++row) {
    const slice = cells.slice(row * 4, row * 4 + 4);
    const isLast = row === rowCount - 1;
    let line = '        ' + slice.map(function (c) {
      return c.padEnd(width);
    }).join('');
    if (isLast) {
      line = line.replace(/\s+$/, '');
    } else {
      line += '//';
      if (notes[row]) line += ' ' + notes[row];
    }
    lines.push(line);
  }
  return lines.join('\n');
}

function emit(docLines, name, values, rowNotes) {
  for (let i = 0; i < docLines.length; ++i) console.log(docLines[i]);
  console.log('    static constexpr std::array<float, kModeCountCeiling> '
    + name + ' = {');
  console.log(fmtRows(values, rowNotes));
  console.log('    };');
  console.log('');
}

// =============================================================================
// MAIN
// =============================================================================

function main() {
  // --- Negative self-check: the UN-THINNED room set must FAIL the assertions.
  //     Without it, "the assertions passed" would carry no information.
  const unthinned = roomModeSeries(1.0).ratios.slice(0, kModeCountCeiling);
  let selfCheckFired = false;
  try {
    assertTable('kRoomModeRatios[un-thinned self-check]', unthinned,
      { minGapRatio: kRoomMinGapRatio });
  } catch (e) {
    if (!(e instanceof TableAssertionError)) throw e;
    selfCheckFired = true;
    console.error('self-check OK (un-thinned room table rejected) -- ' + e.message);
  }
  if (!selfCheckFired) {
    console.error('SELF-CHECK FAILED: the un-thinned room table passed the '
      + 'assertions, so the assertions are not testing anything.');
    return 1;
  }

  // --- Generate.
  const room = roomModeSeries(kRoomMinGapRatio);
  if (room.ratios.length < kModeCountCeiling) {
    console.error('kRoomModeRatios: thinning at ' + kRoomMinGapRatio
      + ' left only ' + room.ratios.length + ' of the required '
      + kModeCountCeiling + ' modes; raise kRoomModeOrderMax.');
    return 1;
  }
  const roomRatios = room.ratios.slice(0, kModeCountCeiling);
  const plate = clampedPlateSeries();
  const barRatios = barSeries();

  // --- Assert.
  try {
    assertTable('kRoomModeRatios', roomRatios, { minGapRatio: kRoomMinGapRatio });
    assertTable('kClampedPlateRatios', plate.ratios);
    assertTable('kBarRatios', barRatios);
  } catch (e) {
    if (!(e instanceof TableAssertionError)) throw e;
    console.error('ASSERTION FAILED -- ' + e.message);
    return 1;
  }

  // --- Print.
  const lastRoom = room.modes[kModeCountCeiling - 1];
  emit([
    '    /// StoneChamber / CavernWall: rectangular-room eigenmodes. Rayleigh\'s',
    '    /// equation f(nx,ny,nz) = (c/2)*sqrt((nx/Lx)^2 + (ny/Ly)^2 + (nz/Lz)^2)',
    '    /// (Kuttruff, Room Acoustics, section 3.1) over nx,ny,nz in [0,'
      + kRoomModeOrderMax + ']',
    '    /// excluding (0,0,0), room proportions ' + kRoomProportions[0] + ' : '
      + kRoomProportions[1] + ' : ' + kRoomProportions[2] + ' (Sepmeyer\'s',
    '    /// low-degeneracy set, which is why the series is not a degenerate comb).',
    '    ///',
    '    /// Sorted ascending, normalised by the first (so r[0] = 1.0 exactly),',
    '    /// thinned to a minimum spacing of '
      + ((kRoomMinGapRatio - 1) * 100).toFixed(1) + ' %, first ' + kModeCountCeiling
      + ' kept',
    '    /// (' + room.ratios.length + ' modes survive the thinning; the '
      + kModeCountCeiling + 'nd kept mode is',
    '    /// (nx,ny,nz) = (' + lastRoom[0] + ',' + lastRoom[1] + ',' + lastRoom[2]
      + ')).',
    '    /// Generated by tools/gen-vorago-material-tables.js -- never typed.',
  ], 'kRoomModeRatios', roomRatios);

  emit([
    '    /// SteelTank: clamped-edge circular plate. The first eight are Leissa,',
    '    /// Vibration of Plates (NASA SP-160), Table 4.4, verbatim (see also',
    '    /// Fletcher & Rossing, The Physics of Musical Instruments, ch. 3); the',
    '    /// tail is a constant-modal-density linear continuation of slope '
      + plate.slope.toFixed(4),
    '    /// (LSQ over the published k = ' + kClampedPlateFitFrom + '..'
      + kClampedPlateFitTo + ') anchored at k = ' + kClampedPlateFitTo + '.',
    '    ///',
    '    /// The same technique kPlateRatios documents for the FREE plate, for the',
    '    /// same reason: a thin plate\'s modal density is asymptotically constant',
    '    /// (Cremer & Heckl). The clamped low modes sit closer together than the',
    '    /// free plate\'s, which is part of what makes this material dark.',
    '    /// Generated by tools/gen-vorago-material-tables.js -- never typed.',
  ], 'kClampedPlateRatios', plate.ratios, {
    0: 'Leissa, published',
    1: 'Leissa, published',
    2: 'plate-density continuation',
  });

  emit([
    '    /// CathedralColumn: free-free flexural bar/column -- the series a marimba',
    '    /// bar is tuned away from (Fletcher & Rossing, The Physics of Musical',
    '    /// Instruments, ch. 2). r[n] = (beta_n / beta_1)^2, with',
    '    /// beta_1..beta_5 = ' + kBarBetaTabulated.map(function (b) {
      return b.toFixed(5);
    }).join(', '),
    '    /// (the tabulated roots of cos(beta)cosh(beta) = 1) and the asymptotic',
    '    /// beta_n = (2n+1)*pi/2 for n >= 6.',
    '    /// Generated by tools/gen-vorago-material-tables.js -- never typed.',
  ], 'kBarRatios', barRatios, {
    0: 'beta_1..beta_4 tabulated',
    1: 'beta_5 tabulated, then asymptotic',
  });

  console.error('kClampedPlateRatios LSQ slope over k = ' + kClampedPlateFitFrom
    + '..' + kClampedPlateFitTo + ': ' + plate.slope.toFixed(6)
    + ' (printed as ' + plate.slope.toFixed(4) + ' in the header comment)');
  console.error('kBarRatios first eight: '
    + barRatios.slice(0, 8).map(printed).join(', '));
  return 0;
}

process.exit(main());
