#!/usr/bin/env node
// =============================================================================
// Profundum Phase 5 -- Shape & Character reference model (plan S10)
// =============================================================================
// Double-precision JS model of the design fixed in plan.md S3-S6. It proves
// feasibility and fixes constants before any header is written; the C++ tests
// remain the pass/fail authority.
//
//   node specs/profundum-phase5-shape-character/shape-character-model.js salience
//   node specs/profundum-phase5-shape-character/shape-character-model.js fracture
//   node specs/profundum-phase5-shape-character/shape-character-model.js alias [E0|E1|E2] [44100|48000|96000]
//   node specs/profundum-phase5-shape-character/shape-character-model.js shs | fracvar | fracflip | descr | flip | shape
//   (alias runs one note x rate per call; the plan's S10.3 table is the 9-cell survey, three rates run concurrently)
//
// Reuses (read 2026-10-15):
//  - Phase 2 evidence/lib.js: OSHB (the PolyphaseHalfbandOversampler model,
//    halfband-design.js), Cascade + preStages (the three 2fs residual notches,
//    biquad.h:712-718), DcBlock (dc_blocker.h:190-205), aliased()/fftLen()
//    (the profundum_core_test_helpers.h:265-306 mask), coreAmps() and the Fold
//    render (Fold-100 input).
//  - Phase 1 recipe-model.js: the four fixture spectra.
// Perceived pitch: the Phase 3 SC-008(r) harmonic-sum estimator's salience,
// S(f) = sum_{h=1..15} 0.84^(h-1) |A(h f)| (anchor_oscillator_test_helpers.h:213-227),
// evaluated on the exact line spectrum (period analysis), not on a Hann frame.
// =============================================================================
'use strict';

const path = require('path');
const L = require(path.join(__dirname, '..', 'profundum-phase2-edge-fold', 'evidence', 'lib.js'));
const R = require(path.join(__dirname, '..', 'profundum-phase1-harmonic-core', 'recipe-model.js'));

const P = L.P_CAL;                                  // -12 dBFS RMS sine peak
const NOTES = { E0: 20.601722, E1: 41.203445, E2: 82.406889, C2: 65.406391, C3: 130.812783 };
const FIX = { sine: R.NAMED.kSineAnchor, tri: R.NAMED.kTriangleAnchor, saw: R.NAMED.kSawAnchor, growl: R.NAMED.kGrowl };
const db20 = (v) => 20 * Math.log10(Math.max(v, 1e-300));
const db10 = (v) => 10 * Math.log10(Math.max(v, 1e-300));
const f2 = (v) => v.toFixed(2);

// ---------------------------------------------------------------- design constants (plan S5, S6)
const TONE = { pivotHarmonic: Number(process.env.TONE_K || 4), maxShelfDb: Number(process.env.TONE_DB || 6) }; // keytracked first-order tilt, unity at the pivot
const RECT = { halfEnd: 0.4 };                           // Amount law: wet 0->1 and b = 0 over [0, halfEnd]; b 0->1 over [halfEnd, 1]
const FRAC = { gMin: 0.5, gMax: 3.0, betaMax: 0.9 };     // Fracture drive and bias depth at Amount 1 (argument units, x/P)
const smooth = (t) => (t <= 0 ? 0 : t >= 1 ? 1 : t * t * (3 - 2 * t));

// Keytracked first-order tilt gain at harmonic number n (frequency n*f0, pivot k*f0), analog one-pole split.
function toneGain(n, tone) {
  const G = (2 * tone - 1) * TONE.maxShelfDb, gl = Math.pow(10, -G / 20), gh = Math.pow(10, G / 20);
  const r = n / TONE.pivotHarmonic;                    // f / fc
  const lpRe = 1 / (1 + r * r), lpIm = -r / (1 + r * r);  // 1 / (1 + j r)
  const re = gl * lpRe + gh * (1 - lpRe), im = gl * lpIm - gh * lpIm;
  return Math.hypot(re, im) * Math.sqrt(2 / (gl * gl + gh * gh));
}

// ---------------------------------------------------------------- period analysis
// Samples `periods` periods of the fixture (M points per period) and returns line magnitudes
// indexed in units of f0/periods (index k -> k*f0/periods). DC is dropped (stage DC blocker).
function lineSpectrum(amps, map, periods = 1, M = 4096) {
  const N = M * periods, y = new Float64Array(N);
  for (let i = 0; i < N; i++) {
    const ph = (2 * Math.PI * i) / M;                  // phase of f0
    let x = 0; for (let j = 0; j < amps.length; j++) if (amps[j]) x += amps[j] * Math.sin((j + 1) * ph);
    y[i] = map(x, i / M);                              // second arg: time in periods of f0
  }
  const K = Math.min(N / 2 - 1, 64 * periods * 4), mag = new Float64Array(K + 1);
  for (let k = 1; k <= K; k++) {
    let re = 0, im = 0; const w = (2 * Math.PI * k) / N;
    for (let i = 0; i < N; i++) { re += y[i] * Math.cos(w * i); im -= y[i] * Math.sin(w * i); }
    mag[k] = (2 * Math.hypot(re, im)) / N;
  }
  return mag;
}
// SHS salience of candidate fundamental `c` (in the same index units) on a line spectrum.
function salience(mag, c) {
  let s = 0, w = 1;
  for (let h = 1; h <= 15; h++) { const k = h * c; if (Number.isInteger(k) && k < mag.length) s += w * mag[k]; w *= 0.84; }
  return s;
}

// ---------------------------------------------------------------- Rectify (plan S5.5)
const rectLaw = (x, b) => (1 - b) * Math.max(x, 0) + b * Math.abs(x);
function rectAmount(A) { return { wet: Math.min(1, A / RECT.halfEnd), b: smooth((A - RECT.halfEnd) / (1 - RECT.halfEnd)) }; }
// Level-preserving makeup on the calibration sine (AC RMS of the law re the sine's RMS).
function rectMakeup(b) {
  let s = 0, m = 0; const M = 4096;
  for (let i = 0; i < M; i++) { const v = rectLaw(Math.sin((2 * Math.PI * i) / M), b); s += v; m += v * v; }
  const mean = s / M, ac = Math.sqrt(m / M - mean * mean); return Math.SQRT1_2 / ac;
}
function rectSpectrum(amps, A, tone) {
  const { wet, b } = rectAmount(A), mk = rectMakeup(b);
  const mag = lineSpectrum(amps, (x) => (1 - wet) * x + wet * mk * rectLaw(x, b));
  for (let k = 1; k < mag.length; k++) mag[k] *= toneGain(k, tone);
  return mag;
}

function reportSalience() {
  console.log('\n== S10.1 Rectify: SHS salience ratio S(2f0)/S(f0), dB (spec ambiguity rule: |ratio| < 3 dB is ambiguous) ==');
  console.log('   law: wet = min(1, A/0.4), b = smoothstep((A-0.4)/0.6); Tone post tilt +-12 dB shelf pair at 4 f0');
  for (const fx of ['sine', 'tri', 'saw', 'growl']) {
    const amps = Array.from(R.full(FIX[fx], NOTES.E1, 48000, 64), (v) => v * Math.SQRT1_2);
    for (const tone of [0, 0.5, 1]) {
      const row = [];
      for (let A = 0; A <= 1.0001; A += 0.1) { const m = rectSpectrum(amps, A, tone); row.push(db20(salience(m, 2) / salience(m, 1))); }
      console.log(`  ${fx.padEnd(5)} Tone ${tone.toFixed(1)}: ` + row.map((v) => f2(v).padStart(6)).join(' '));
    }
  }
  // Mapping-independent ceiling: pure full-wave (wet 1, b 1), no tilt, on an exact sine and exact triangle.
  const pure = (fn) => { const M = 4096, y = []; for (let i = 0; i < M; i++) y.push(Math.abs(fn((2 * Math.PI * i) / M))); return y; };
  const specOf = (y) => { const N = y.length, mag = new Float64Array(80); for (let k = 1; k < 80; k++) { let re = 0, im = 0; for (let i = 0; i < N; i++) { re += y[i] * Math.cos((2 * Math.PI * k * i) / N); im -= y[i] * Math.sin((2 * Math.PI * k * i) / N); } mag[k] = (2 * Math.hypot(re, im)) / N; } return mag; };
  const tri = (p) => (2 / Math.PI) * Math.asin(Math.sin(p));
  for (const [nm, fn] of [['|sin|', Math.sin], ['|triangle|', tri]]) { const m = specOf(pure(fn)); console.log(`  ceiling ${nm.padEnd(10)}: S(2f0)/S(f0) = ${f2(db20(salience(m, 2) / salience(m, 1)))} dB`); }
}

// ---------------------------------------------------------------- Fracture (plan S5.8)
// y = (tanh(g (x/P + beta s)) - tanh(g beta s)) * P / g * mk, s = sin(2 pi t / 2) (the /2 phasor), t in periods of f0.
function fracMap(A) {
  const g = FRAC.gMin + (FRAC.gMax - FRAC.gMin) * A, beta = FRAC.betaMax * A;
  return (x, t) => { const s = Math.sin(Math.PI * t); return (Math.tanh(g * (x / P + beta * s)) - Math.tanh(g * beta * s)) * P / g; };
}
function reportFracture() {
  console.log('\n== S10.2 Fracture: odd-f0/2 sideband power re total (dB) and SHS ratio S(f0/2)/S(f0) (dB), E1 ==');
  console.log(`   law: g = ${FRAC.gMin} + ${FRAC.gMax - FRAC.gMin}*A, beta = ${FRAC.betaMax}*A, s = sin(pi t) (f0/2 phasor), offset tanh(g beta s) removed`);
  for (const fx of ['sine', 'tri', 'saw', 'growl']) {
    const amps = Array.from(R.full(FIX[fx], NOTES.E1, 48000, 64), (v) => v * Math.SQRT1_2);
    const sb = [], sr = [];
    for (let A = 0; A <= 1.0001; A += 0.1) {
      const m = lineSpectrum(amps, fracMap(A), 2, 2048);
      let odd = 0, tot = 0; for (let k = 1; k < m.length; k++) { tot += m[k] * m[k]; if (k % 2) odd += m[k] * m[k]; }
      sb.push(db10(odd / tot)); sr.push(db20(salience(m, 1) / salience(m, 2)));
    }
    console.log(`  ${fx.padEnd(5)} sideband  : ` + sb.map((v) => f2(Math.max(v, -99)).padStart(7)).join(' '));
    console.log(`  ${fx.padEnd(5)} S(f0/2)/S(f0): ` + sr.map((v) => f2(v).padStart(7)).join(' '));
  }
}

module.exports = { toneGain, lineSpectrum, salience, rectLaw, rectAmount, rectMakeup, rectSpectrum, fracMap, FIX, NOTES, P, L, R };

const arg = process.argv[2] || 'all';
if (require.main === module) {
  if (arg === 'all' || arg === 'salience') reportSalience();
  if (arg === 'all' || arg === 'fracture') reportFracture();
}

// ---------------------------------------------------------------- faithful SHS stage 1 (anchor_oscillator_test_helpers.h:227-280)
// Hann frame of 262144 samples, linear-interpolated magnitude, 0.05-cent grid over [f0/2.2, 2.2 f0].
// Returns { f, s1, s2 } with the best candidate and the salience at f0 and 2 f0 (and f0/2).
function shsStage1(x, fs, f0) {
  const N = 262144, re = new Float64Array(N), im = new Float64Array(N);
  for (let i = 0; i < N; i++) re[i] = x[i] * (0.5 - 0.5 * Math.cos((2 * Math.PI * i) / N));
  L.fft(re, im);
  const mag = new Float64Array(N / 2 + 1); for (let k = 0; k <= N / 2; k++) mag[k] = Math.hypot(re[k], im[k]);
  const bin = fs / N, im2 = (hz) => { const p = hz / bin, k = Math.floor(p); if (k + 1 >= mag.length) return 0; const fr = p - k; return mag[k] * (1 - fr) + mag[k + 1] * fr; };
  const score = (f) => { let s = 0, w = 1; for (let h = 1; h <= 15; h++) { s += w * im2(h * f); w *= 0.84; } return s; };
  const lo = Math.log2(f0 / 2.2), hi = Math.log2(f0 * 2.2), step = 0.05 / 1200; let best = lo, bs = -1;
  for (let v = lo; v <= hi; v += step) { const s = score(Math.pow(2, v)); if (s > bs) { bs = s; best = v; } }
  // peak salience near each candidate (+-2 cents) so a bin vertex does not bias the comparison
  const near = (f) => { let m = 0; for (let c = -2; c <= 2; c += 0.05) m = Math.max(m, score(f * Math.pow(2, c / 1200))); return m; };
  return { f: Math.pow(2, best), s1: near(f0), s2: near(2 * f0), sh: near(f0 / 2) };
}
function reportShsReal() {
  console.log('\n== S10.1b Faithful SHS stage 1 on rendered fixtures (48 kHz, 262144-sample Hann frame) ==');
  const fs = 48000, N = 262144;
  for (const note of ['E1', 'C3']) for (const fx of ['sine', 'tri', 'saw', 'growl']) {
    const f0 = NOTES[note], amps = Array.from(R.full(FIX[fx], f0, fs, 64), (v) => v * Math.SQRT1_2);
    const x = new Float64Array(N), w = (2 * Math.PI * f0) / fs;
    for (let i = 0; i < N; i++) { let s = 0; for (let j = 0; j < amps.length; j++) if (amps[j]) s += amps[j] * Math.sin((j + 1) * w * i); x[i] = s; }
    const raw = shsStage1(x, fs, f0);
    const y = Float64Array.from(x, (v) => Math.abs(v));      // pure full wave (DC is harmless to SHS: h >= 1 only)
    const fw = shsStage1(y, fs, f0);
    console.log(`  ${note} ${fx.padEnd(5)} raw: f^/f0 ${f2(raw.f / f0)}  S(2f0)/S(f0) ${f2(db20(raw.s2 / raw.s1))} dB  S(f0/2)/S(f0) ${f2(db20(raw.sh / raw.s1))} dB` +
      ` | full-wave: f^/f0 ${f2(fw.f / f0)}  S(2f0)/S(f0) ${f2(db20(fw.s2 / fw.s1))} dB`);
  }
}
if (require.main === module && (arg === 'all' || arg === 'shs')) reportShsReal();

// ---------------------------------------------------------------- Fracture variants (plan S5.8 design search)
// K = 'bias'  : the S10.2 law (tanh bias flip, offset removed).
// K = 'gate'  : y = x (1 - d q), q = 0.5 - 0.5 cos(pi t), d = dMax A (period-alternating gain).
// K = 'both'  : gate, then the bias-flip clip at (g, beta).
function fracVariant(kind, A, o = {}) {
  const dMax = o.dMax ?? 1.0, gMin = o.gMin ?? FRAC.gMin, gMax = o.gMax ?? FRAC.gMax, bMax = o.betaMax ?? FRAC.betaMax;
  const g = gMin + (gMax - gMin) * A, beta = bMax * A, d = dMax * A;
  const clip = (x, s) => (Math.tanh(g * (x / P + beta * s)) - Math.tanh(g * beta * s)) * P / g;
  return (x, t) => {
    const s = Math.sin(Math.PI * t), q = 0.5 - 0.5 * Math.cos(Math.PI * t);
    if (kind === 'gate') return x * (1 - d * q);
    if (kind === 'both') return clip(x * (1 - d * q), s);
    return clip(x, s);
  };
}
function reportFractureVariants() {
  const kinds = (process.env.FRAC_KINDS || 'gate,both').split(',');
  for (const kind of kinds) {
    console.log(`\n== S10.2b Fracture variant '${kind}': S(f0/2)/S(f0) dB over A = 0..1 (E1); sideband dB re total at A = 1 ==`);
    for (const fx of ['sine', 'tri', 'saw', 'growl']) {
      const amps = Array.from(R.full(FIX[fx], NOTES.E1, 48000, 64), (v) => v * Math.SQRT1_2);
      const sr = []; let sbEnd = 0;
      for (let A = 0; A <= 1.0001; A += 0.1) {
        const m = lineSpectrum(amps, fracVariant(kind, A), 2, 2048);
        let odd = 0, tot = 0; for (let k = 1; k < m.length; k++) { tot += m[k] * m[k]; if (k % 2) odd += m[k] * m[k]; }
        sr.push(db20(salience(m, 1) / salience(m, 2))); sbEnd = db10(odd / tot);
      }
      console.log(`  ${fx.padEnd(5)}: ` + sr.map((v) => f2(v).padStart(6)).join(' ') + `   | sideband@1 ${f2(sbEnd)}`);
    }
  }
}
if (require.main === module && (arg === 'all' || arg === 'fracvar')) reportFractureVariants();

// K = 'flip' : y = x ((1 - a) + a sigma(t)), sigma = tanh(kS sin(pi t)) / tanh(kS) (smoothed f0/2 polarity square), a = A.
function fracFlip(A, kS = 4) {
  return (x, t) => { const sg = Math.tanh(kS * Math.sin(Math.PI * t)) / Math.tanh(kS); return x * ((1 - A) + A * sg); };
}
function reportFractureFlip() {
  console.log(`\n== S10.2c Fracture variant 'flip' (polarity alternation): S(f0/2)/S(f0) dB over A = 0..1 (E1), and A at the 0 dB crossing ==`);
  for (const fx of ['sine', 'tri', 'saw', 'growl']) {
    const amps = Array.from(R.full(FIX[fx], NOTES.E1, 48000, 64), (v) => v * Math.SQRT1_2);
    const ratio = (A) => { const m = lineSpectrum(amps, fracFlip(A), 2, 2048); return db20(salience(m, 1) / salience(m, 2)); };
    const sr = []; for (let A = 0; A <= 1.0001; A += 0.1) sr.push(ratio(A));
    const cross = (lev) => { let lo = 0, hi = 1; if (ratio(hi) < lev) return NaN; for (let i = 0; i < 30; i++) { const mid = 0.5 * (lo + hi); if (ratio(mid) < lev) lo = mid; else hi = mid; } return 0.5 * (lo + hi); };
    console.log(`  ${fx.padEnd(5)}: ` + sr.map((v) => f2(v).padStart(6)).join(' ') + `  | A(-1 dB) ${f2(cross(-1))}  A(0 dB) ${f2(cross(0))}  A(+1 dB) ${f2(cross(1))}`);
  }
}
if (require.main === module && (arg === 'all' || arg === 'fracflip')) reportFractureFlip();

// ---------------------------------------------------------------- S10.3 aliasing: the OversampledResidualPath (plan S3)
// out = D_lin(sum g c T(u)) + DC2Hz(D_res+notch(sum g (y_b - c T(u)))), D = halfband decimation (OSHB).
// variant 'resid' = plan design; 'plain' = one decimation of y with no notch and no residual split;
// 'naive' = the branch map at the base rate (positive control).
const TP = { preK: Number(process.env.PRE_K || 1), preDb: Number(process.env.PRE_DB || 6), postK: Number(process.env.POST_K || 2), postDb: Number(process.env.POST_DB || 12) };
class Tilt1 { // keytracked first-order tilt at rate fsr: y = gL lp + gH (x - lp), OnePoleLP (one_pole.h:111,135)
  constructor(fsr, fc, tone, db = TONE.maxShelfDb) { const G = (2 * tone - 1) * db; this.gl = Math.pow(10, -G / 20); this.gh = Math.pow(10, G / 20);
    this.a = Math.exp(-2 * Math.PI * fc / fsr); this.s = 0; const nm = Math.sqrt(2 / (this.gl * this.gl + this.gh * this.gh)); this.gl *= nm; this.gh *= nm; }
  p(x) { this.s = (1 - this.a) * x + this.a * this.s; return this.gl * this.s + this.gh * (x - this.s); }
}
class Dc1 { constructor(fc, fsr) { this.R = Math.exp(-2 * Math.PI * fc / fsr); this.x1 = 0; this.y1 = 0; } p(x) { const y = x - this.x1 + this.R * this.y1; this.x1 = x; this.y1 = y; return y; } }
const tubeCurve = (x) => { const l = Math.tanh(x * 0.5) * 2, a = l + 0.3 * l * l - 0.15 * l * l * l; return Math.tanh(a); }; // sigmoid.h:333-342
const diode = (x) => (x >= 0 ? 1 - Math.exp(-x * 1.5) : x / (1 - 0.5 * x));                                            // sigmoid.h:358-366
// h1 coefficient of a static map on the calibration sine (prepare-time table in C++).
function h1c(map) { const M = 2048; let re = 0; for (let i = 0; i < M; i++) { const s = Math.sin((2 * Math.PI * i) / M); re += map(P * s) * s; } return (2 * re / M) / P; }
const ADAA = (process.env.ADAA || '1') === '1';
const rectF = (x, b) => (1 - b) * (x > 0 ? 0.5 * x * x : 0) + b * 0.5 * x * Math.abs(x);                     // antiderivative of rectLaw
const diodeF = (x) => (x >= 0 ? x + Math.exp(-1.5 * x) / 1.5 - 1 / 1.5 : -4 * Math.log(1 - 0.5 * x) + 4 * (1 - 0.5 * x) - 4);
class Adaa1 { constructor(f, F) { this.f = f; this.F = F; this.v1 = 0; this.F1 = F(0); }
  p(v) { const Fv = this.F(v), d = v - this.v1; const y = Math.abs(d) > 1e-5 * (1 + Math.abs(v)) ? (Fv - this.F1) / d : this.f(0.5 * (v + this.v1)); this.v1 = v; this.F1 = Fv; return y; } }
const rectF2 = (x, b) => (1 - b) * (x > 0 ? x * x * x / 6 : 0) + b * x * x * Math.abs(x) / 6;
const diodeF2 = (x) => (x >= 0 ? 0.5 * x * x - Math.exp(-1.5 * x) / 2.25 - x / 1.5 + 1 / 2.25 : 8 * ((1 - 0.5 * x) * Math.log(1 - 0.5 * x) - (1 - 0.5 * x)) - x * x + 8);
class Adaa2 { // Bilbao et al. 2017, double precision; one-sample delay at the processing rate
  constructor(f, F1, F2) { this.f = f; this.F1 = F1; this.F2 = F2; this.x1 = 0; this.x2 = 0; this.d1 = F1(0); }
  D(a, b) { const d = a - b; return Math.abs(d) > Number(process.env.ADAA_EPS || 1e-5) ? (this.F2(a) - this.F2(b)) / d : this.F1(0.5 * (a + b)); }
  p(x) { const D0 = this.D(x, this.x1); const den = x - this.x2; let y;
    if (Math.abs(den) > Number(process.env.ADAA_EPS || 1e-5)) y = 2 * (D0 - this.d1) / den;
    else { const xb = 0.5 * (x + this.x2), dd = xb - this.x1; y = Math.abs(dd) > Number(process.env.ADAA_EPS || 1e-5) ? (2 / dd) * (this.F1(xb) + (this.F2(this.x1) - this.F2(xb)) / dd) : this.f(0.5 * (xb + this.x1)); }
    this.x2 = this.x1; this.x1 = x; this.d1 = D0; return y; } }
const ADAA_ORDER = Number(process.env.ADAA_ORDER || 2);
const ALG = {
  // Each returns a per-channel processor factory: (fsr, f0, A, tone) -> { y(u, tSec) , c }
  rect: (fsr, f0, A, tone) => { const { wet, b } = rectAmount(A), mk = rectMakeup(b), post = new Tilt1(fsr, TP.postK * f0, tone, TP.postDb);
    const map = (u) => (1 - wet) * u + wet * mk * rectLaw(u, b); const ad = ADAA_ORDER === 2 ? new Adaa2((v) => rectLaw(v, b), (v) => rectF(v, b), (v) => rectF2(v, b)) : new Adaa1((v) => rectLaw(v, b), (v) => rectF(v, b));
    return { c: h1c(map), y: (u) => post.p(ADAA ? (1 - wet) * u + wet * mk * ad.p(u) : map(u)) }; },
  tube: (fsr, f0, A, tone) => { const gIn = Math.pow(10, (-6 + Number(process.env.TUBE_SPAN || 18) * A) / 20), pre = new Tilt1(fsr, TP.preK * f0, tone, TP.preDb), dc = new Dc1(10, fsr);
    const raw = (u) => tubeCurve(gIn * u); const c0 = h1c(raw), mk = 1 / c0; return { c: 1, y: (u) => mk * dc.p(raw(pre.p(u))) }; },
  fold: (fsr, f0, A, tone) => { const th = 0.1 + (Number(process.env.THETA_MAX || 2.5) - 0.1) * A, mk = 1 / Math.sin(Math.min(th, Math.PI / 2)), post = new Tilt1(fsr, TP.postK * f0, tone, TP.postDb), dc = new Dc1(10, fsr);
    const map = (u) => mk * P * Math.sin(th * u / P); return { c: h1c(map), y: (u) => post.p(dc.p(map(u))) }; },
  bite: (fsr, f0, A, tone) => { const g = Math.pow(10, (-12 + Number(process.env.BITE_SPAN || 30) * A) / 20), pre = new Tilt1(fsr, TP.preK * f0, tone, TP.preDb);
    const raw = (u) => diode(g * u / P); const mk = 1 / h1c(raw); const ad = ADAA_ORDER === 2 ? new Adaa2(diode, diodeF, diodeF2) : new Adaa1(diode, diodeF);
    const pp = new Tilt1(fsr, TP.postK * f0, tone, Number(process.env.PP_DB || 6));
    return { c: 1, y: (u) => pp.p(mk * (ADAA ? ad.p(g * pre.p(u) / P) : raw(pre.p(u)))) }; },
  crush: (fsr, f0, A, tone) => { // pitch-synchronous hold at m f0 + B-bit quantiser, causal 4-sample B-spline steps (plan S5.7)
    const m = Math.round(64 * Math.pow(4 / 64, A)), bits = 12 - 8 * A, lv = Math.pow(2, bits) - 1, post = new Tilt1(fsr, TP.postK * f0, tone, TP.postDb);
    const q = (v) => { const s = Math.round(v * lv * 0.5); const mx = lv * 0.5; return Math.max(-mx, Math.min(mx, s)) / mx; };
    const S = (v) => (v <= 0 ? 0 : v >= 4 ? 1 : v < 1 ? v ** 4 / 24 : v < 2 ? 0.5 - (3 * (2 - v) ** 4 - 8 * (2 - v) ** 3 + 16 * (2 - v)) / 24 : v < 3 ? 0.5 + (3 * (v - 2) ** 4 - 8 * (v - 2) ** 3 + 16 * (v - 2)) / 24 : 1 - (4 - v) ** 4 / 24);
    let ph = 0, uPrev = 0, held = 0, n = 0; const steps = []; const inc = m * f0 / fsr;
    return { c: 1, y: (u) => { const p0 = ph; ph += inc; let out = held;
      if (Math.floor(ph) > Math.floor(p0)) { const tau = (Math.ceil(p0) - p0) / inc; const ui = uPrev + tau * (u - uPrev); const nv = P * q(ui / P * 0.9) / 0.9;
        steps.push({ t: n - 1 + tau, d: nv - held }); held = nv; }
      out = held; for (let i = steps.length - 1; i >= 0; i--) { const v = n - steps[i].t; if (v >= 4) { steps.splice(i, 1); continue; } out -= steps[i].d * (1 - S(v)); }
      uPrev = u; n++; return post.p(out); } }; },
  // 'flipclip' (plan S5.8 recommendation): v = x ((1 - a) + a sigma), then the bias-flip clip with tanh ADAA1 (F1 = ln cosh).
  // Tube with a branch-local extra 2x (8x total at fs <= 100 kHz): halfband 2x around TubeStage at the 4fs rate.
  tube8: (fsr, f0, A, tone) => { const gIn = Math.pow(10, (-6 + Number(process.env.TUBE_SPAN || 18) * A) / 20), pre = new Tilt1(fsr, TP.preK * f0, tone, TP.preDb), dc = new Dc1(10, 2 * fsr);
    const raw = (u) => tubeCurve(gIn * u); const mk = 1 / h1c(raw), os = new L.OSHB(2), b = new Float64Array(2);
    const pp = new Tilt1(fsr, TP.postK * f0, tone, Number(process.env.PP_DB || 6));
    return { c: 1, y: (u) => { os.up(pre.p(u), b); b[0] = dc.p(raw(b[0])); b[1] = dc.p(raw(b[1])); return pp.p(mk * os.down(b, null)); } }; },
  fracture2: (fsr, f0, A, tone) => { const post = new Tilt1(fsr, TP.postK * f0, tone, TP.postDb); let t = 0; const dt = f0 / fsr;
    const g = 0.5 + (Number(process.env.FG || 2.0) - 0.5) * A, beta = Number(process.env.FB || 0.5) * A, kS = 4;
    const lc = (v) => { const a = Math.abs(v); return a + Math.log1p(Math.exp(-2 * a)) - Math.LN2; };   // ln cosh, overflow-safe
    let v1 = 0, F1 = 0;
    return { c: 1, y: (u) => { const sg = Math.tanh(kS * Math.sin(Math.PI * t)) / Math.tanh(kS), s = Math.sin(Math.PI * t);
      const x = u * ((1 - A) + A * sg), v = g * (x / P + beta * s), Fv = lc(v), d = v - v1;
      const th = Math.abs(d) > 1e-6 ? (Fv - F1) / d : Math.tanh(0.5 * (v + v1)); v1 = v; F1 = Fv; t += dt;
      return post.p((th - Math.tanh(g * beta * s)) * P / g); } }; },
  fracture: (fsr, f0, A, tone) => { const kind = process.env.FRAC || 'bias', post = new Tilt1(fsr, TP.postK * f0, tone, TP.postDb); let t = 0; const dt = f0 / fsr;
    const fb = fracVariant('bias', A), ff = fracFlip(A); return { c: 1, y: (u) => { const v = kind === 'flip' ? ff(u, t) : fb(u, t); t += dt; return post.p(v); } }; },
};
function stageRender(alg, x, fs, f0, A, tone, variant) {
  const n = x.length, out = new Float64Array(n);
  if (variant === 'naive') { const pr = ALG[alg](fs, f0, A, tone); for (let k = 0; k < n; k++) out[k] = pr.y(x[k]); return out; }
  const F = Number(process.env.FORCE_F || (fs <= 100000 ? 4 : 2)), fsr = F * fs, pr = ALG[alg](fsr, f0, A, tone), up = new L.OSHB(F), dl = new L.OSHB(F), dr = new L.OSHB(F);
  const pre = new L.Cascade(L.preStages(2 * fs, Number(process.env.PRE_FC || 1), fs, Number(process.env.PRE_BQ || 4))), hook = F === 4 ? (v) => pre.proc(v) : null, dc = new Dc1(2, fs);
  const ub = new Float64Array(F), lb = new Float64Array(F), rb = new Float64Array(F);
  for (let k = 0; k < n; k++) {
    up.up(x[k], ub);
    for (let i = 0; i < F; i++) { const y = pr.y(ub[i]); if (variant === 'plain') { rb[i] = y; } else { lb[i] = pr.c * ub[i]; rb[i] = y - lb[i]; } }
    out[k] = variant === 'plain' ? dr.down(rb, null) : dl.down(lb, null) + dc.p(dr.down(rb, hook));
  }
  return out;
}
function inputSignal(fx, src, f0, fs, n) {
  const amps = L.coreAmps(FIX[fx], f0, fs);
  if (src === 'fold100') return L.render({ amps, f0, fs, F: 4, seconds: n / fs, preFc: 1, fold: L.foldParamsFor(100, amps) });
  const x = new Float64Array(n), w = (2 * Math.PI * f0) / fs; for (let k = 0; k < n; k++) { let s = 0; for (let j = 0; j < amps.length; j++) if (amps[j]) s += amps[j] * Math.sin((j + 1) * w * k); x[k] = s; } return x;
}
function reportAlias() {
  const notes = process.argv[3] ? [process.argv[3]] : ['E0', 'E2'], rates = process.argv[4] ? [Number(process.argv[4])] : [44100];
  const algs = (process.env.ALGS || 'rect,tube8,fold,bite,crush,fracture2').split(','), variants = (process.env.VARIANTS || 'plain,resid').split(',');
  console.log('\n== S10.3 Aliased power, dBFS (bar -80), Amount 1, worst over Tone {0,1} x inputs {saw, growl} x {raw, Fold-100} ==');
  for (const note of notes) for (const fs of rates) {
    const f0 = NOTES[note], Lf = L.fftLen(fs, f0 / 2), n = Lf + Math.round(0.7 * fs), inputs = {};
    for (const fx of ['saw', 'growl']) for (const src of ['raw', 'fold100']) inputs[`${fx}/${src}`] = inputSignal(fx, src, f0, fs, n);
    const inA = Object.entries(inputs).map(([k, x]) => `${k} ${f2(L.aliased(x.subarray(n - Lf), Lf, fs, f0))}`).join(', ');
    console.log(`  ${note} ${fs} (FFT 2^${Math.log2(Lf)}): input A: ${inA}`);
    for (const alg of algs) {
      const cells = {};
      for (const v of variants) { let worst = -1e9, at = '';
        for (const [k, x] of Object.entries(inputs)) for (const tone of [0, 1]) {
          const lat = alg.startsWith('fracture') ? 2 : 1, y = stageRender(alg, x, fs, f0, 1, tone, v), a = L.aliased(y.subarray(n - Lf), Lf, fs, f0 / lat);
          if (a > worst) { worst = a; at = `${k} T${tone}`; } }
        cells[v] = `${f2(worst)} (${at})`; }
      console.log(`    ${alg.padEnd(8)} ` + Object.entries(cells).map(([v, s]) => `${v}: ${s}`).join(' | '));
    }
  }
}
if (require.main === module && (arg === 'all' || arg === 'alias')) reportAlias();

// ---------------------------------------------------------------- S10.4 descriptors, SC-005 / SC-006 / SC-007 (static, line spectra)
// Spec "Descriptor definitions": w_n, E_low (n 2..8), E_high (n >= 9), R_oe, THD, I (off the n f0 lattice), C (log2 f/f0,
// 20 Hz .. min(fs/2, 20 kHz)); OQ-S floor: clamp >= -60 dB, counts only if one side > -40 dB re total (THD: re w1).
const DES = { tubeMaxDb: Number(process.env.TUBE_MAX_DB || 12), thetaMax: Number(process.env.THETA_MAX || 2.5), biteSpan: Number(process.env.BITE_SPAN || 30) };
function tiltC(n, tone, k = TONE.pivotHarmonic, db = TONE.maxShelfDb) { const G = (2 * tone - 1) * db; let gl = Math.pow(10, -G / 20), gh = Math.pow(10, G / 20); const nm = Math.sqrt(2 / (gl * gl + gh * gh)); gl *= nm; gh *= nm;
  const r = n / k, d = 1 + r * r, lre = 1 / d, lim = -r / d; return [gl * lre + gh * (1 - lre), gl * lim - gh * lim]; }
// Static output line spectrum (index units f0/2, 2 periods) of algorithm `alg` on fixture amps.
function staticSpectrum(alg, amps, A, tone) {
  const M = 2048, N = 2 * M, x = new Float64Array(N), pre = alg === 'tube' || alg === 'bite';
  for (let i = 0; i < N; i++) { const ph = (2 * Math.PI * i) / M; let s = 0;
    for (let j = 0; j < amps.length; j++) if (amps[j]) { const n = j + 1; if (pre) { const [re, im] = tiltC(n, tone, TP.preK, TP.preDb); s += amps[j] * (re * Math.sin(n * ph) + im * Math.cos(n * ph)); } else s += amps[j] * Math.sin(n * ph); }
    x[i] = s; }
  const y = new Float64Array(N);
  if (alg === 'rect') { const { wet, b } = rectAmount(A), mk = rectMakeup(b); for (let i = 0; i < N; i++) y[i] = (1 - wet) * x[i] + wet * mk * rectLaw(x[i], b); }
  else if (alg === 'tube') { const g = Math.pow(10, (-6 + (DES.tubeMaxDb + 6) * A) / 20); let c = 0; const mp = (u) => tubeCurve(g * u); c = h1c(mp); for (let i = 0; i < N; i++) y[i] = mp(x[i]) / c; }
  else if (alg === 'fold') { const th = 0.1 + (DES.thetaMax - 0.1) * A, mk = 1 / Math.sin(Math.min(th, Math.PI / 2)); for (let i = 0; i < N; i++) y[i] = mk * P * Math.sin(th * x[i] / P); }
  else if (alg === 'bite') { const g = Math.pow(10, (-12 + DES.biteSpan * A) / 20), mp = (u) => diode(g * u / P), c = h1c(mp); for (let i = 0; i < N; i++) y[i] = mp(x[i]) / c; }
  else if (alg === 'crush') { const m = Math.round(64 * Math.pow(4 / 64, A)), lv = Math.pow(2, 12 - 8 * A) - 1, mx = lv * 0.5;
    for (let i = 0; i < N; i++) { const k = Math.floor((i % M) * m / M), ih = Math.floor(i / M) * M + Math.round(k * M / m); const v = x[ih % N] / P * 0.9; y[i] = P * Math.max(-mx, Math.min(mx, Math.round(v * mx))) / mx / 0.9; } }
  else if (alg === 'fracture') { const g = 0.5 + 1.5 * A, beta = 0.5 * A, kS = 4;
    for (let i = 0; i < N; i++) { const t = i / M, sg = Math.tanh(kS * Math.sin(Math.PI * t)) / Math.tanh(kS), s = Math.sin(Math.PI * t), xx = x[i] * ((1 - A) + A * sg);
      y[i] = (Math.tanh(g * (xx / P + beta * s)) - Math.tanh(g * beta * s)) * P / g; } }
  const K = 2 * 160, mag = new Float64Array(K + 1);
  for (let k = 1; k <= K; k++) { let re = 0, im = 0; const w = (2 * Math.PI * k) / N; for (let i = 0; i < N; i++) { re += y[i] * Math.cos(w * i); im -= y[i] * Math.sin(w * i); } mag[k] = (2 * Math.hypot(re, im)) / N; }
  const prePost = (process.env.PREPOST || '1') === '1';
  if (!pre || prePost) for (let k = 1; k <= K; k++) mag[k] *= Math.hypot(...tiltC(k / 2, tone, TP.postK, pre ? Number(process.env.PP_DB || 6) : TP.postDb));
  return mag;
}
function descriptors(mag, f0, fs) {
  const top = Math.min(fs / 2, 20000); let tot = 0, w1 = 0, lo = 0, hi = 0, odd = 0, even = 0, inh = 0, cn = 0, cd = 0;
  for (let k = 1; k < mag.length; k++) { const f = (k * f0) / 2; if (f >= fs / 2) break; const p = mag[k] * mag[k] / 2; tot += p;
    if (f >= 20 && f <= top) { cn += p * Math.log2(f / f0); cd += p; }
    if (k % 2) { inh += p; continue; } const n = k / 2; if (n === 1) w1 += p; else { if (n <= 8) lo += p; else hi += p; if (n % 2) odd += p; else even += p; } }
  const r = (a, b) => db10(a / b);
  return { Elow: r(lo, tot), Ehigh: r(hi, tot), Roe: [r(odd, tot), r(even, tot)], THD: r(lo + hi, w1), I: r(inh, tot), C: cn / cd, w1rel: r(w1, tot) };
}
// OQ-S separation: returns the max qualifying dB difference and the centroid difference.
function separation(a, b) {
  const cl = (v) => Math.max(v, -60), counts = (u, v, thr = -40) => u > thr || v > thr; let best = 0, why = '';
  for (const k of ['Elow', 'Ehigh', 'I']) if (counts(a[k], b[k])) { const d = Math.abs(cl(a[k]) - cl(b[k])); if (d > best) { best = d; why = k; } }
  if (counts(a.Roe[0], b.Roe[0]) || counts(a.Roe[1], b.Roe[1])) { const d = Math.abs((cl(a.Roe[0]) - cl(a.Roe[1])) - (cl(b.Roe[0]) - cl(b.Roe[1]))); if (d > best) { best = d; why = 'Roe'; } }
  if (counts(a.THD, b.THD)) { const d = Math.abs(cl(a.THD) - cl(b.THD)); if (d > best) { best = d; why = 'THD'; } }
  return { db: best, why, dC: Math.abs(a.C - b.C) };
}
const ALGS6 = ['tube', 'fold', 'rect', 'bite', 'crush', 'fracture'];
function reportDescriptors() {
  const fs = 48000;
  console.log('\n== S10.4 SC-005 pairwise separation at Amount 1, Tone 0.5 (bar: >= 3 dB on one floored descriptor, or >= 0.25 oct C) ==');
  for (const note of ['E1', 'C2']) for (const fx of ['saw', 'sine']) {
    const f0 = NOTES[note], amps = Array.from(R.full(FIX[fx], f0, fs, 64), (v) => v * Math.SQRT1_2), D = {};
    for (const a of ALGS6) D[a] = descriptors(staticSpectrum(a, amps, 1, 0.5), f0, fs);
    let worst = 1e9, wp = '';
    for (let i = 0; i < 6; i++) for (let j = i + 1; j < 6; j++) { const s = separation(D[ALGS6[i]], D[ALGS6[j]]); const margin = Math.max(s.db / 3, s.dC / 0.25); if (margin < worst) { worst = margin; wp = `${ALGS6[i]}-${ALGS6[j]} ${f2(s.db)} dB (${s.why}), dC ${s.dC.toFixed(3)}`; } }
    console.log(`  ${note} ${fx.padEnd(4)}: weakest pair ${wp} -> margin x${f2(worst)}  | C: ` + ALGS6.map((a) => `${a} ${D[a].C.toFixed(2)}`).join(', '));
  }
  console.log('\n== S10.4b SC-006 Tone span C(1) - C(0) (oct, bar 0.25) and SC-007 Amount movement (max floored move dB of THD/I/Roe, or dC), Tone 0.5 ==');
  for (const fx of ['saw', 'sine']) for (const note of ['E1', 'C2']) {
    const f0 = NOTES[note], amps = Array.from(R.full(FIX[fx], f0, fs, 64), (v) => v * Math.SQRT1_2), row = [];
    for (const a of ALGS6) {
      let span = 1e9, minStep = 1e9; for (const A of [0.5, 1]) { const Cs = []; for (let t = 0; t <= 10; t++) Cs.push(descriptors(staticSpectrum(a, amps, A, t / 10), f0, fs).C); span = Math.min(span, Cs[10] - Cs[0]); for (let t = 1; t <= 10; t++) minStep = Math.min(minStep, Cs[t] - Cs[t - 1]); }
      const d0 = descriptors(staticSpectrum(a, amps, 0, 0.5), f0, fs), d1 = descriptors(staticSpectrum(a, amps, 1, 0.5), f0, fs);
      const cl = (v) => Math.max(v, -60), cnt = (u, v) => u > -40 || v > -40; let mv = 0;
      for (const k of ['THD', 'I']) if (cnt(d0[k], d1[k])) mv = Math.max(mv, Math.abs(cl(d1[k]) - cl(d0[k])));
      if (cnt(d0.Roe[0], d1.Roe[0]) || cnt(d0.Roe[1], d1.Roe[1])) mv = Math.max(mv, Math.abs((cl(d1.Roe[0]) - cl(d1.Roe[1])) - (cl(d0.Roe[0]) - cl(d0.Roe[1]))));
      row.push(`${a} span ${span.toFixed(2)}${minStep < -0.005 ? '(NONMONO ' + minStep.toFixed(3) + ')' : ''} amt ${f2(mv)}dB/dC ${Math.abs(d1.C - d0.C).toFixed(2)}`);
    }
    console.log(`  ${fx.padEnd(4)} ${note}: ` + row.join(' | '));
  }
}
if (require.main === module && (arg === 'all' || arg === 'descr')) reportDescriptors();

// ---------------------------------------------------------------- S10.5 Rectify flip bands under the chosen Tone (post k = 2 f0, +-12 dB)
// Band edges: A where S(2f0)/S(f0) crosses -thr, 0 and +thr dB (bisection), per half-wave-symmetric fixture, Tone and note.
function reportFlipBands() {
  const thr = Number(process.env.AMBIG_DB || 1), fs = 48000;
  console.log(`\n== S10.5 Rectify flip bands, ambiguity +-${thr} dB (A at -thr / 0 / +thr; ratio at A = 1) ==`);
  for (const tone of [0, 0.5, 1]) {
    const rows = []; let lo = -1, hi = 2;
    for (const fx of ['sine', 'tri']) for (const note of ['E0', 'E1', 'E2', 'C2', 'C3']) {
      const f0 = NOTES[note], amps = Array.from(R.full(FIX[fx], f0, fs, 64), (v) => v * Math.SQRT1_2);
      const ratio = (A) => { const { wet, b } = rectAmount(A), mk = rectMakeup(b); const m = lineSpectrum(amps, (x) => (1 - wet) * x + wet * mk * rectLaw(x, b));
        for (let k = 1; k < m.length; k++) m[k] *= Math.hypot(...tiltC(k, tone, TP.postK, TP.postDb)); return db20(salience(m, 2) / salience(m, 1)); };
      const cross = (lev) => { if (ratio(1) < lev) return NaN; let a = 0, b = 1; for (let i = 0; i < 25; i++) { const mid = 0.5 * (a + b); if (ratio(mid) < lev) a = mid; else b = mid; } return 0.5 * (a + b); };
      const c = [cross(-thr), cross(0), cross(thr)]; lo = Math.max(lo, c[0]); hi = Math.min(hi, isNaN(c[2]) ? 1 : c[2]);
      rows.push(`${fx}/${note} ${c.map((v) => (isNaN(v) ? ' n/a' : v.toFixed(3))).join('/')} (r1 ${f2(ratio(1))})`);
    }
    console.log(`  Tone ${tone}: ` + rows.join(', '));
    console.log(`     common band [max lower edge, min upper edge] = [${lo.toFixed(3)}, ${hi.toFixed(3)}]${lo < hi ? '' : '  EMPTY'}`);
  }
}
if (require.main === module && (arg === 'all' || arg === 'flip')) reportFlipBands();

// ---------------------------------------------------------------- S10.6 ShapeMorph static model (plan S4): per-fixture level vs SHAPE,
// shared loudness table on the four-fixture mean, SC-002 (+-2.5 dB per fixture, per-step bars) and SC-001 separation.
const SH = {
  roundDrive: Number(process.env.R_DRIVE || 0.8), roundK0: Number(process.env.R_K0 || 10), roundK1: Number(process.env.R_K1 || 20), roundQ: 0.7071,
  notchK: Number(process.env.H_NK || 3), notchQ: Number(process.env.H_NQ || 1.0), peakK: Number(process.env.H_PK || 9), peakQ: Number(process.env.H_PQ || 3), peakGain: Number(process.env.H_PG || 1.0),
  biteK: Number(process.env.B_K || 4), biteDb: Number(process.env.B_DB || 12), biteDrive0Db: -12, biteDrive1Db: Number(process.env.B_DRV || 15),
};
// Analog prototypes in harmonic-number units (keytracked): s = j n / k.
const cdiv = (a, b) => { const d = b[0] * b[0] + b[1] * b[1]; return [(a[0] * b[0] + a[1] * b[1]) / d, (a[1] * b[0] - a[0] * b[1]) / d]; };
const lp2 = (n, k, Q) => { const w = n / k; return cdiv([1, 0], [1 - w * w, w / Q]); };
const notch2 = (n, k, Q) => { const w = n / k; return cdiv([1 - w * w, 0], [1 - w * w, w / Q]); };
const bp2 = (n, k, Q) => { const w = n / k; return cdiv([0, w / Q], [1 - w * w, w / Q]); };   // constant 0 dB peak
const tilt1 = (n, k, db) => { let gl = Math.pow(10, -db / 20), gh = Math.pow(10, db / 20); const nm = Math.sqrt(2 / (gl * gl + gh * gh)); gl *= nm; gh *= nm; const r = n / k, d = 1 + r * r; return [gl / d + gh * r * r / d, (gh - gl) * r / d]; };
// Complex line spectrum (harmonics 1..K) of one period y[0..M).
function cspec(y, K) { const M = y.length, out = []; for (let k = 1; k <= K; k++) { let re = 0, im = 0; for (let i = 0; i < M; i++) { const w = (2 * Math.PI * k * i) / M; re += y[i] * Math.cos(w); im -= y[i] * Math.sin(w); } out.push([2 * re / M, 2 * im / M]); } return out; }
function synth(spec, M) { const y = new Float64Array(M); spec.forEach(([re, im], j) => { const n = j + 1; for (let i = 0; i < M; i++) { const w = (2 * Math.PI * n * i) / M; y[i] += re * Math.cos(w) - im * Math.sin(w); } }); return y; }
function shapeSpectrum(amps, s) {
  const M = 1024, K = 64, xs = amps.map((a) => [0, -a]);   // a sin -> phasor (0, -a) in the cos/sin convention above
  const x = synth(xs, M), out = Array.from({ length: K }, () => [0, 0]);
  const add = (spec, w) => spec.forEach((c, j) => { out[j][0] += w * c[0]; out[j][1] += w * c[1]; });
  const t = Math.min(1, 2 * s), t2 = Math.max(0, 2 * s - 1), wR = s <= 0.5 ? 1 - t : 0, wH = s <= 0.5 ? t : 1 - t2, wB = s <= 0.5 ? 0 : t2;
  if (wR > 0) { const kR = SH.roundK0 * Math.pow(SH.roundK1 / SH.roundK0, t), g = SH.roundDrive;
    const d = cspec(Float64Array.from(x, (v) => P * Math.tanh(g * v / P) / g), K); add(d.map((c, j) => { const h = lp2(j + 1, kR, SH.roundQ); return [c[0] * h[0] - c[1] * h[1], c[0] * h[1] + c[1] * h[0]]; }), wR); }
  if (wH > 0) add(xs.concat(Array(K - xs.length).fill([0, 0])).slice(0, K).map((c, j) => { const n = j + 1, a = notch2(n, SH.notchK, SH.notchQ), b = bp2(n, SH.peakK, SH.peakQ), h = [a[0] + SH.peakGain * b[0], a[1] + SH.peakGain * b[1]]; return [c[0] * h[0] - c[1] * h[1], c[0] * h[1] + c[1] * h[0]]; }), wH);
  if (wB > 0) { const g = Math.pow(10, (SH.biteDrive0Db + (SH.biteDrive1Db - SH.biteDrive0Db) * t2) / 20);
    const pre = synth(xs.concat(Array(K - xs.length).fill([0, 0])).slice(0, K).map((c, j) => { const h = tilt1(j + 1, SH.biteK, SH.biteDb * t2); return [c[0] * h[0] - c[1] * h[1], c[0] * h[1] + c[1] * h[0]]; }), M);
    const raw = (u) => diode(g * u / P), c1 = h1c(raw); add(cspec(Float64Array.from(pre, (v) => raw(v) / c1 * 1), K), wB); }
  return out.map((c) => Math.hypot(c[0], c[1]));
}
function reportShape() {
  const fs = 48000, f0 = NOTES.E1, levels = {}, specs = {};
  for (const fx of ['saw', 'growl', 'tri', 'sine']) { const amps = Array.from(R.full(FIX[fx], f0, fs, 64), (v) => v * Math.SQRT1_2);
    const L0 = db10(amps.reduce((a, v) => a + v * v / 2, 0)); levels[fx] = []; specs[fx] = [];
    for (let i = 0; i <= 100; i++) { const m = shapeSpectrum(amps, i / 100); specs[fx].push(m); levels[fx].push(db10(m.reduce((a, v) => a + v * v / 2, 0)) - L0); } }
  const CAL = (process.env.CAL_SET || 'saw,tri,sine').split(','); const table = []; for (let i = 0; i <= 100; i++) table.push(-(CAL.reduce((a, fx) => a + levels[fx][i], 0) / CAL.length));
  console.log('\n== S10.6 ShapeMorph static model, E1: per-fixture level after the shared table (dB re own sweep mean): min / max ==');
  for (const fx of ['saw', 'growl', 'tri', 'sine']) { const v = levels[fx].map((l, i) => l + table[i]), mean = v.reduce((a, b) => a + b) / v.length;
    let mn = 1e9, mx = -1e9, step = 0; v.forEach((l, i) => { mn = Math.min(mn, l - mean); mx = Math.max(mx, l - mean); if (i) step = Math.max(step, Math.abs(l - v[i - 1])); });
    console.log(`  ${fx.padEnd(5)} ${f2(mn)} / ${f2(mx)} dB (bar +-2.5); max |dL| per 0.01 step ${f2(step)} dB (bar 0.5); raw level at s = 0 / 0.5 / 1: ${[0, 50, 100].map((i) => f2(levels[fx][i])).join(' / ')}`); }
  console.log('  table (dB) at s = 0, 0.25, 0.5, 0.75, 1: ' + [0, 25, 50, 75, 100].map((i) => f2(table[i])).join(', '));
  for (const fx of ['saw', 'growl']) { const D = [0, 50, 100].map((i) => { const m = specs[fx][i], mag = new Float64Array(2 * m.length + 1); m.forEach((v, j) => { mag[2 * (j + 1)] = v; }); return descriptors(mag, f0, fs); });
    const pr = [[0, 1, 'ROUND-HOLLOW'], [1, 2, 'HOLLOW-BITE'], [0, 2, 'ROUND-BITE']].map(([a, b, nm]) => { const sp = separation(D[a], D[b]); return `${nm} ${f2(sp.db)} dB (${sp.why}) dC ${sp.dC.toFixed(2)}`; });
    console.log(`  SC-001 ${fx}: ` + pr.join(' | ')); }
}
if (require.main === module && (arg === 'all' || arg === 'shape')) reportShape();
