#!/usr/bin/env node
// =============================================================================
// Profundum Phase 2 -- Edge excitation + HarmonicFold reference model (plan S9)
// =============================================================================
// Double-precision JS model of the design fixed in plan.md S3-S5. It is the
// calibration record for the constants there: run it after ANY constant change
// and paste the output into the plan's calibration table (S9).
//
//   node specs/profundum-phase2-edge-fold/edge-fold-model.js            # all
//   node specs/profundum-phase2-edge-fold/edge-fold-model.js phase      # S9.1 only
//   node specs/profundum-phase2-edge-fold/edge-fold-model.js fold       # S9.3 only
//   node specs/profundum-phase2-edge-fold/edge-fold-model.js alias      # S9.4 only
//
// Models, each mirroring a real header read 2026-10-10:
//  - Oversampler<F,2> Economy/ZeroLatency (oversampler.h:491-528 filter design,
//    :587-672 zero-stuff x2 gain / filter / decimate-even), BiquadCascade<4>
//    Butterworth Q (biquad.h:208-218), RBJ low-pass (biquad.h:676-692), TDF2.
//  - Core input spectra from Phase 1's recipe model (full(c, f0, fs)), all sine
//    phase 0, centre pan gain sqrt(1/2) per channel -> -12 dBFS RMS per channel.
//  - Aliased power: BH7, Parseval-normalised, dB re 0.5, +-8-bin exclusion around
//    n*f0 < fs/2 and bins 0-8 (profundum_core_test_helpers.h:273-305), FFT length
//    per spec D-6 (every m = 1..4 alias >= 16 bins off the grid).
// The C++ tests remain the pass/fail authority; this script proves feasibility
// and fixes constants before code is written.
// =============================================================================
'use strict';

const path = require('path');
const R = require(path.join(__dirname, '..', 'profundum-phase1-harmonic-core', 'recipe-model.js'));
const HB = require(path.join(__dirname, 'halfband-design.js'));

// ---------------------------------------------------------------- constants (plan S3-S5)
const P_CAL = 0.25118864315095801 * Math.SQRT2;  // calibration sine peak (-12 dBFS RMS)
const EXC = {
  driveMinDb: -12, driveMaxDb: Number(process.env.DRIVE_MAX_DB || 12),   // excitationDriveLaw: dB-linear over drive
  edgeRangeDb: 12,                   // FR-004
  maxBias: 0.5,                      // asymmetry 1 -> bias 0.5 in the transfer's argument domain
  dcHz: 2.0,                         // residual DC remover corner
};
const FOLD = {
  softEnd: 30, compEnd: 65,
  gSoft: Number(process.env.G_SOFT || 0.43),      // soft-regime drive at kSoftEnd (argument units: x / P_CAL)
  gComp: Number(process.env.G_COMP || 1.20),      // compression-regime drive at kCompressionEnd
  normRampEnd: 40,  // level normalisation beta: 0 at kSoftEnd -> 1 here (smoothstep)
  thetaMax: Number(process.env.THETA_MAX || 3.0),   // sine-fold index at amount 100 (rad at the calibration peak)
  gCont: Number(process.env.G_CONT || 1),  // fraction of the compression drive slope continued through the fold regime
  // theta(a) = thetaMax * s(t), t = (a - compEnd)/(100 - compEnd) (tasks.md D-1 ruling 2026-10-11):
  // FOLD_LAW = p (number) -> ease-out s(t) = 1 - (1 - t)^p; 'smooth' -> t^2 (3 - 2t); 'linear' -> t (the pre-D-1 law).
  foldLaw: process.env.FOLD_LAW || '1.5',  // D-1 measured choice (evidence/d1law.js)
  dcHz: 2.0,
};

// ---------------------------------------------------------------- filters
function lpCoeffs(fc, fs, Q) {
  const w = 2 * Math.PI * fc / fs, s = Math.sin(w), c = Math.cos(w), al = s / (2 * Q), a0 = 1 + al;
  return { b0: (1 - c) / 2 / a0, b1: (1 - c) / a0, b2: (1 - c) / 2 / a0, a1: -2 * c / a0, a2: (1 - al) / a0 };
}
function notchCoeffs(f, fs, Q) { // biquad.h:712-718
  const w = 2 * Math.PI * f / fs, c = Math.cos(w), al = Math.sin(w) / (2 * Q), a0 = 1 + al;
  return { b0: 1 / a0, b1: -2 * c / a0, b2: 1 / a0, a1: -2 * c / a0, a2: (1 - al) / a0 };
}
function preStages(rate, fc, fs, nbq) {
  if ((process.env.PRE_KIND || 'notch') === 'notch') return JSON.parse(process.env.NOTCHES || '[[0.5,1.5],[0.52,2.0],[0.55,1.5]]').map(([f, q]) => notchCoeffs(f * fs, rate, q));
  return butterN(rate, fc * fs, nbq);
}
function butterN(fs, fc, N) {
  const st = [];
  for (let k = 0; k < N; k++) st.push(lpCoeffs(fc, fs, 1 / (2 * Math.cos(Math.PI * (2 * k + 1) / (4 * N)))));
  return st;
}
function butter8(fs, fc) {
  const N = 4, st = [];
  for (let k = 0; k < N; k++) st.push(lpCoeffs(fc, fs, 1 / (2 * Math.cos(Math.PI * (2 * k + 1) / (4 * N)))));
  return st;
}
class Cascade {
  constructor(coeffs) { this.c = coeffs; this.z = new Float64Array(coeffs.length * 2); }
  proc(x) {
    for (let i = 0; i < this.c.length; i++) {
      const c = this.c[i], z = this.z;
      const y = c.b0 * x + z[2 * i];
      z[2 * i] = c.b1 * x - c.a1 * y + z[2 * i + 1];
      z[2 * i + 1] = c.b2 * x - c.a2 * y;
      x = y;
    }
    return x;
  }
}
// Polyphase IIR halfband oversampler (plan S2.3): outer stage (fs<->2fs) HB_N/HB_TBW, inner stage (2fs<->4fs) N=4.
class OSHB {
  constructor(F) {
    this.F = F;
    const outer = HB.design(Number(process.env.HB_N || 8), Number(process.env.HB_TBW || 0.05));
    this.u0 = new HB.Up2(outer); this.d0 = new HB.Down2(outer);
    if (F === 4) { const inner = HB.design(4, 0.25); this.u1 = new HB.Up2(inner); this.d1 = new HB.Down2(inner); }
    this.t = [0, 0]; this.t2 = [0, 0];
  }
  up(x, out) {
    this.u0.proc(x, this.t);
    if (this.F === 2) { out[0] = this.t[0]; out[1] = this.t[1]; return; }
    this.u1.proc(this.t[0], this.t2); out[0] = this.t2[0]; out[1] = this.t2[1];
    this.u1.proc(this.t[1], this.t2); out[2] = this.t2[0]; out[3] = this.t2[1];
  }
  down(v, hook2x) {
    if (this.F === 2) return this.d0.proc(v[0], v[1]);
    let a = this.d1.proc(v[0], v[1]), b = this.d1.proc(v[2], v[3]);
    if (hook2x) { a = hook2x(a); b = hook2x(b); }
    return this.d0.proc(a, b);
  }
}
// One channel of Oversampler<F,*> Economy: per-sample equivalent of the block code.
class OS {
  constructor(F, fs) {
    this.F = F;
    const rep = (st) => (process.env.STEEP ? [].concat(...Array(Number(process.env.STEEP)).fill(st)) : st); // diagnostic only
    const s1 = rep(butter8(2 * fs, 0.45 * fs));
    this.up0 = new Cascade(s1); this.dn0 = new Cascade(s1);
    if (F === 4) { const s2 = rep(butter8(4 * fs, 0.45 * fs)); this.up1 = new Cascade(s2); this.dn1 = new Cascade(s2); }
  }
  up(x, out) {
    const a = this.up0.proc(2 * x), b = this.up0.proc(0);
    if (this.F === 2) { out[0] = a; out[1] = b; return; }
    out[0] = this.up1.proc(2 * a); out[1] = this.up1.proc(0); out[2] = this.up1.proc(2 * b); out[3] = this.up1.proc(0);
  }
  down(v) {
    if (this.F === 2) { const a = this.dn0.proc(v[0]); this.dn0.proc(v[1]); return a; }
    const p = this.dn1.proc(v[0]); this.dn1.proc(v[1]); const q = this.dn1.proc(v[2]); this.dn1.proc(v[3]);
    const a = this.dn0.proc(p); this.dn0.proc(q); return a;
  }
}
class DcBlock { // dc_blocker.h:190-205, R = exp(-2 pi fc / fs)
  constructor(fc, fs) { this.R = Math.exp(-2 * Math.PI * fc / fs); this.x1 = 0; this.y1 = 0; }
  proc(x) { const y = x - this.x1 + this.R * this.y1; this.x1 = x; this.y1 = y; return y; }
}

// ---------------------------------------------------------------- transfers
const sig = (v) => v / Math.sqrt(1 + v * v);                 // Sigmoid::recipSqrt (sigmoid.h:188)
const adaa1 = (v, v1, s, s1) => (v + v1) / (s + s1);         // closed-form ADAA1 of sig, F1 = sqrt(1+v^2)
const driveLaw = (d) => Math.pow(10, (EXC.driveMinDb + (EXC.driveMaxDb - EXC.driveMinDb) * d) / 20);
const edgeGain = (e) => Math.pow(10, EXC.edgeRangeDb * (e - 1) / 20);

// One-period static analysis of a memoryless map g on x(t) = sum a_n sin(n t): harmonic powers.
function periodAnalysis(amps, g, M = 8192) {
  const xs = new Float64Array(M), ys = new Float64Array(M);
  for (let k = 0; k < M; k++) {
    const t = 2 * Math.PI * k / M; let x = 0;
    for (let n = 0; n < amps.length; n++) if (amps[n] !== 0) x += amps[n] * Math.sin((n + 1) * t);
    xs[k] = x; ys[k] = g(x);
  }
  return { xs, ys, M };
}
function harmonics(ys, M, N) { // complex coefficients of sin/cos, n = 0..N
  const out = [];
  let mean = 0; for (let k = 0; k < M; k++) mean += ys[k]; mean /= M;
  out.push({ re: mean, im: 0, p: mean * mean });
  for (let n = 1; n <= N; n++) {
    let s = 0, c = 0;
    for (let k = 0; k < M; k++) { const t = 2 * Math.PI * n * k / M; s += ys[k] * Math.sin(t); c += ys[k] * Math.cos(t); }
    s *= 2 / M; c *= 2 / M; out.push({ re: s, im: c, p: (s * s + c * c) / 2 });
  }
  return out;
}
function desc(ys, M, Nmax = 400) {
  const h = harmonics(ys, M, Nmax);
  let rest = 0, cnt = 0, ms = 0, pk = 0, even = 0;
  for (let n = 2; n <= Nmax; n++) { rest += h[n].p; if (h[n].p >= h[1].p * 1e-6) cnt++; if (n % 2 === 0) even += h[n].p; }
  let mean = 0; for (let k = 0; k < M; k++) mean += ys[k]; mean /= M;
  for (let k = 0; k < M; k++) { const v = ys[k] - mean; ms += v * v; pk = Math.max(pk, Math.abs(v)); }
  ms /= M;
  return { thd: 10 * Math.log10(rest / h[1].p), H: cnt, rms: Math.sqrt(ms), cf: 20 * Math.log10(pk / Math.sqrt(ms)),
    h1: h[1], even: 10 * Math.log10(Math.max(even, 1e-300) / h[1].p) };
}

// ---------------------------------------------------------------- excitation tables (plan S3.4)
// For the calibration sine x = P_CAL sin t: makeup M = rms(x)/rms(f - mean f), c = M * h1(f) / P_CAL.
function excStatic(D, b) {
  const fb = sig(b);
  const { ys, M } = periodAnalysis([P_CAL], (x) => sig(D * x / P_CAL + b) - fb, 2048);
  const d = desc(ys, M, 60);
  const makeup = (P_CAL / Math.SQRT2) / d.rms;
  return { makeup, c: makeup * d.h1.re / P_CAL, thd: d.thd };
}

// ---------------------------------------------------------------- fold static map (plan S4.3)
const smooth = (t) => (t <= 0 ? 0 : t >= 1 ? 1 : t * t * (3 - 2 * t));
function foldShape(t) { // D-1: the fold-index law over the fold regime, s(0) = 0, s(1) = 1
  if (FOLD.foldLaw === 'linear') return t;
  if (FOLD.foldLaw === 'smooth') return t * t * (3 - 2 * t);
  return 1 - Math.pow(1 - t, Number(FOLD.foldLaw));
}
function foldParams(a) {
  const p = {};
  if (a <= FOLD.softEnd) { const t = a / FOLD.softEnd; p.g = FOLD.gSoft * t; p.theta = 0; }
  else if (a <= FOLD.compEnd) { const t = (a - FOLD.softEnd) / (FOLD.compEnd - FOLD.softEnd);
    p.g = FOLD.gSoft * Math.pow(FOLD.gComp / FOLD.gSoft, t); p.theta = 0; }
  else { const t = (a - FOLD.compEnd) / (100 - FOLD.compEnd); p.g = FOLD.gComp * Math.pow(FOLD.gComp / FOLD.gSoft, FOLD.gCont * t * (100 - FOLD.compEnd) / (FOLD.compEnd - FOLD.softEnd)); p.theta = FOLD.thetaMax * foldShape(t); }
  p.beta = smooth((a - FOLD.softEnd) / (FOLD.normRampEnd - FOLD.softEnd));
  return p;
}
const satN = (u, g) => (g < 1e-6 ? u : sig(g * u / P_CAL) * P_CAL / g);   // unity small-signal gain
// Static map in the normalised domain (u already scaled by the detector), with the makeup table.
const rawCache = new Map();
function rawMap(a) { // un-made-up static map: soft/comp saturator, fold of the comp-end saturator
  const p = foldParams(a);
  if (a <= FOLD.compEnd) return (u) => satN(u, p.g);
  const vce = satN(P_CAL, p.g), th = p.theta;
  return (u) => { const v = satN(u, p.g); return th < 1e-6 ? v : Math.sin(th * v / vce) * vce / th; };
}
function foldMakeup(a) { // static peak-preserving makeup: max |F(u)| over |u| <= P_CAL equals P_CAL
  const k = Math.round(a * 4); if (rawCache.has(k)) return rawCache.get(k);
  const f = rawMap(a); let pk = 0; for (let i = 1; i <= 4096; i++) pk = Math.max(pk, Math.abs(f(P_CAL * i / 4096)));
  const m = P_CAL / pk; rawCache.set(k, m); return m;
}
function foldMap(a) { const f = rawMap(a), m = foldMakeup(a); return (u) => m * f(u); }

// ---------------------------------------------------------------- render + alias measure
function coreAmps(c, f0, fs) { return Array.from(R.full(c, f0, fs, 64), (v) => v * Math.SQRT1_2); }
function bh7(L) {
  const a = [0.27105140069342, -0.43329793923448, 0.21812299954311, -0.06592544638803, 0.01081174209837, -0.00077658482522, 0.00001388721735];
  const w = new Float64Array(L);
  for (let k = 0; k < L; k++) { let s = 0; for (let j = 0; j < 7; j++) s += a[j] * Math.cos(2 * Math.PI * j * k / L); w[k] = s; }
  return w;
}
function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) { let bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; } }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) { let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const ur = re[i + k], ui = im[i + k], xr = re[i + k + len / 2], xi = im[i + k + len / 2];
        const vr = xr * cr - xi * ci, vi = xr * ci + xi * cr;
        re[i + k] = ur + vr; im[i + k] = ui + vi; re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
        const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t; } } }
}
function fftLen(fs, f0) {
  let L = 8192; while (L < 64 * fs / f0) L <<= 1;
  for (;;) {
    const bin = fs / L; let ok = true;
    for (let m = 1; m <= 4; m++) { const r = (m * fs) % f0; if (Math.min(r, f0 - r) / bin < 16) ok = false; }
    if (ok || L >= (1 << 22)) return L; L <<= 1;
  }
}
function aliased(x, L, fs, f0) {
  const w = bh7(L), re = new Float64Array(L), im = new Float64Array(L);
  let sxw = 0, sw = 0;
  for (let k = 0; k < L; k++) { re[k] = x[k] * w[k]; sxw += re[k] * re[k]; sw += w[k] * w[k]; }
  fft(re, im);
  const half = L / 2, p = new Float64Array(half + 1); let sp = 0;
  for (let k = 0; k <= half; k++) { p[k] = re[k] * re[k] + im[k] * im[k]; sp += p[k]; }
  const sc = (sxw / sw) / sp, ex = new Uint8Array(half + 1), bin = fs / L;
  for (let k = 0; k <= 8; k++) ex[k] = 1;
  for (let n = 1; n * f0 < fs / 2; n++) { const c = n * f0 / bin;
    for (let k = Math.max(0, Math.ceil(c - 8)); k <= Math.min(half, Math.floor(c + 8)); k++) ex[k] = 1; }
  let al = 0; for (let k = 1; k <= half; k++) if (!ex[k]) al += p[k] * sc;
  return 10 * Math.log10(Math.max(al, 1e-300) / 0.5);
}
// Render one channel: core -> [excitation] -> [fold], with the plan's linear-bypass structure.
function render({ amps, f0, fs, F, exc, fold, adaa = true, naiveExc = false, bypass = true, seconds, preFc = 0, preFcFold = preFc, xin = null }) {
  const n = Math.round(seconds * fs), out = new Float64Array(n);
  const w = 2 * Math.PI * f0 / fs;
  const mkOS = () => ((process.env.OS_KIND || 'hb') === 'hb' ? new OSHB(F) : new OS(F, fs));
  const osE = mkOS(), osF = mkOS(), dcE = new DcBlock(EXC.dcHz, fs), dcF = new DcBlock(FOLD.dcHz, fs);
  const ub = new Float64Array(F), vb = new Float64Array(F);
  const nbq = Number(process.env.PRE_BQ || 4), at2 = (process.env.PRE_AT || '2') === '2' && F === 4 && (process.env.OS_KIND || 'hb') === 'hb';
  const preRate = at2 ? 2 * fs : F * fs;
  const preE = preFc > 0 ? new Cascade(preStages(preRate, preFc, fs, nbq)) : null, preF = preFcFold > 0 ? new Cascade(preStages(preRate, preFcFold, fs, nbq)) : null;
  const hookE = at2 && preE ? (v) => preE.proc(v) : null, hookF = at2 && preF ? (v) => preF.proc(v) : null;
  let v1 = exc ? exc.b : 0, s1 = Math.sqrt(1 + v1 * v1);
  const fb = exc ? sig(exc.b) : 0;
  const fmap = fold ? foldMap(fold.a) : null;
  const splitHz = Number(process.env.SPLIT_HZ || 500);
  const lp2 = (rate) => new Cascade([lpCoeffs(splitHz, rate, Math.SQRT1_2)]);
  const sE = splitHz > 0 ? { b: lp2(fs), o: lp2(F * fs) } : null, sF = splitHz > 0 ? { b: lp2(fs), o: lp2(F * fs) } : null;
  const wF = fold ? smooth(fold.a / 10) : 1;   // Fold engage weight: 0 at amount 0, 1 from amount 10
  const cf = fold ? fold.c : 1, env = fold ? fold.env : 1;
  for (let k = 0; k < n; k++) {
    let x = 0; if (xin) x = xin[k]; else for (let j = 0; j < amps.length; j++) if (amps[j] !== 0) x += amps[j] * Math.sin((j + 1) * w * k);
    if (exc) {
      if (naiveExc) { x = exc.M * (sig(exc.D * x / P_CAL + exc.b) - fb); }
      else {
        osE.up(x, ub);
        for (let i = 0; i < F; i++) {
          const v = exc.D * ub[i] / P_CAL + exc.b, s = Math.sqrt(1 + v * v);
          const y = adaa ? adaa1(v, v1, s, s1) : v / s;
          v1 = v; s1 = s;
          const lin = sE ? sE.o.proc(ub[i]) : ub[i];
          vb[i] = exc.M * (y - fb) - (bypass ? exc.c * lin : 0);
          if (preE && !hookE) vb[i] = preE.proc(vb[i]);
        }
        const r = dcE.proc(osE.down(vb, hookE));
        x = (bypass ? exc.c * (sE ? sE.b.proc(x) : x) : 0) + r;
      }
    }
    if (fold) {
      const g = P_CAL / env;  // steady-state normalisation (beta = 1 at the measured amounts)
      osF.up(x, ub);
      for (let i = 0; i < F; i++) { const lo = sF ? sF.o.proc(ub[i]) : ub[i]; const lin = sF ? lo + (1 - wF) * (ub[i] - lo) : ub[i];
        vb[i] = fmap(ub[i] * g) / g - (bypass ? cf * lin : 0); if (preF && !hookF) vb[i] = preF.proc(vb[i]); }
      const r = dcF.proc(osF.down(vb, hookF));
      const xl = sF ? sF.b.proc(x) : x;
      x = (bypass ? cf * (sF ? xl + (1 - wF) * (x - xl) : x) : 0) + r;
    }
    out[k] = x;
  }
  return out;
}

// ---------------------------------------------------------------- reports
const NOTES = { E0: 20.601722, E1: 41.203445, E2: 82.406889, C2: 65.406391 };
const STATES = { saw: R.NAMED.kSawAnchor, sc009: { ...R.NAMED.kSawAnchor, edge: 1, shift: 1 }, growl: R.NAMED.kGrowl,
  mid: { depth: 0.5, body: 0.5, bodyCurvature: 0.5, bodyEmphasis: 0, edge: 0.5, shift: 0 }, sine: R.NAMED.kSineAnchor };
const f2 = (v) => v.toFixed(2);

function reportPhase() {
  console.log('\n== S9.1 Oversampler Economy chain: h1 phase / group delay (base-rate samples) ==');
  // computed analytically from the cascade transfer functions
  const H = (st, f, fs) => { let re = 1, im = 0; const w = 2 * Math.PI * f / fs;
    for (const c of st) { const zr1 = Math.cos(w), zi1 = -Math.sin(w), zr2 = Math.cos(2 * w), zi2 = -Math.sin(2 * w);
      const nr = c.b0 + c.b1 * zr1 + c.b2 * zr2, ni = c.b1 * zi1 + c.b2 * zi2, dr = 1 + c.a1 * zr1 + c.a2 * zr2, di = c.a1 * zi1 + c.a2 * zi2;
      const d = dr * dr + di * di, hr = (nr * dr + ni * di) / d, hi = (ni * dr - nr * di) / d; const r = re * hr - im * hi; im = re * hi + im * hr; re = r; }
    return [re, im]; };
  const chain = (fs, F, f) => { let re = 1, im = 0; for (let s = 0; s < (F === 2 ? 1 : 2); s++) { const sr = fs * (1 << (s + 1)), st = butter8(sr, 0.45 * fs);
    for (let r = 0; r < 2; r++) { const [a, b] = H(st, f, sr); const t = re * a - im * b; im = re * b + im * a; re = t; } } return Math.atan2(im, re); };
  for (const fs of [44100, 48000, 96000]) for (const F of [2, 4]) {
    const gd = -(chain(fs, F, 10.01) - chain(fs, F, 10)) / (2 * Math.PI * 0.01) * fs;
    console.log(`  ${fs} Hz ${F}x: group delay ${gd.toFixed(2)} smp; h1 phase E0 ${f2(chain(fs, F, NOTES.E0) * 180 / Math.PI)} deg, ` +
      `E1 ${f2(chain(fs, F, NOTES.E1) * 180 / Math.PI)}, E2 ${f2(chain(fs, F, NOTES.E2) * 180 / Math.PI)}`);
  }
}

function reportExcitation() {
  console.log('\n== S9.2 Excitation static law (calibration sine) ==');
  for (const drive of [0, 0.25, 0.5, 0.75, 1]) for (const e of [0, 1]) {
    const D = driveLaw(drive) * edgeGain(e), s0 = excStatic(D, 0), s1 = excStatic(D, EXC.maxBias);
    console.log(`  drive ${drive} edge ${e}: Deff ${D.toFixed(3)}  THD(asym0) ${f2(s0.thd)} dB  makeup ${s0.makeup.toFixed(3)}/${s1.makeup.toFixed(3)}  c ${s0.c.toFixed(3)}/${s1.c.toFixed(3)}`);
  }
  console.log('  level law on other inputs (mix 1, sine-calibrated makeup), dB re input RMS, asym 0 / 1:');
  for (const [nm, st] of Object.entries({ sine: STATES.sine, mid: STATES.mid, saw: STATES.saw })) for (const note of ['E1', 'C2']) {
    const amps = coreAmps(st, NOTES[note], 48000); let line = `   ${nm} ${note}:`;
    for (const drive of [0, 0.5, 1]) for (const asym of [0, 1]) {
      const D = driveLaw(drive) * edgeGain(st.edge === undefined ? 1 : st.edge), b = asym * EXC.maxBias, s = excStatic(D, b), fb = sig(b);
      const pa = periodAnalysis(amps, (x) => s.makeup * (sig(D * x / P_CAL + b) - fb));
      const din = desc(pa.xs, pa.M, 200), dout = desc(pa.ys, pa.M, 200);
      line += ` d${drive}a${asym} ${f2(20 * Math.log10(dout.rms / din.rms))}`;
    }
    console.log(line);
  }
  // even harmonics (SC-003): kSineAnchor, drive 0.5, edge 0.5
  const D = driveLaw(0.5) * edgeGain(0.5); let line = '  SC-003(a) E_even re h1 at drive 0.5/edge 0.5, asym 0..1:';
  for (const asym of [0.03125, 0.25, 0.5, 1]) { const b = asym * EXC.maxBias, fb = sig(b);
    const d = desc(periodAnalysis([P_CAL], (x) => sig(D * x / P_CAL + b) - fb).ys, 8192, 60); line += ` ${asym}:${f2(d.even)}`; }
  console.log(line);
  const tri = coreAmps(R.NAMED.kTriangleAnchor, NOTES.C2, 48000), Dm = driveLaw(1);
  const pt = periodAnalysis(tri, (x) => sig(Dm * x / P_CAL + EXC.maxBias)); const dt = desc(pt.ys, pt.M, 200), di = desc(pt.xs, pt.M, 200);
  console.log(`  SC-003(b) triangle at max excitation: E_even in ${f2(di.even)} -> out ${f2(dt.even)} dB re h1`);
}

function reportFold() {
  console.log('\n== S9.3 Fold static sweep on the calibration sine (normalised domain, beta = 1 above 40) ==');
  console.log('  amount  THD dB   H   L dB   CF dB  |T1| dB  h1 sign');
  const ref = desc(periodAnalysis([P_CAL], (x) => x).ys, 8192, 60);
  const rows = [];
  for (let a = 0; a <= 100; a += 2.5) {
    const d = desc(periodAnalysis([P_CAL], foldMap(a)).ys, 8192, 400);
    rows.push({ a, ...d });
    console.log(`  ${a.toFixed(1).padStart(6)}  ${f2(d.thd).padStart(7)} ${String(d.H).padStart(3)} ${f2(20 * Math.log10(d.rms / ref.rms)).padStart(6)} ${f2(d.cf).padStart(6)}  ${f2(20 * Math.log10(Math.abs(d.h1.re) / P_CAL)).padStart(6)}  ${d.h1.re > 0 ? '+' : '-'}`);
  }
  const at = (a) => rows.find((r) => Math.abs(r.a - a) < 1e-9);
  console.log(`  SC-006: H(100) ${at(100).H} vs 4*H(30) ${4 * at(30).H}; THD rise soft ${f2(at(30).thd - at(2.5).thd)} comp ${f2(at(65).thd - at(30).thd)} fold ${f2(at(100).thd - at(65).thd)}`);
  console.log(`  SC-007(a): comp-regime THD +${f2(at(65).thd - at(30).thd)} dB, H ${at(30).H} -> ${at(65).H}, dL ${f2(20 * Math.log10(at(65).rms / at(30).rms))}, dCF ${f2(at(65).cf - at(30).cf)}`);
  // SC-007(c) distance vs plain tanh(g x), THD-matched, over the compression THD interval
  const tanhRows = []; for (let i = 0; i < 401; i++) { const g = 0.1 * Math.pow(1000, i / 400);
    const d = desc(periodAnalysis([P_CAL], (x) => Math.tanh(g * x), 4096).ys, 4096, 60); tanhRows.push({ thd: d.thd, L: 20 * Math.log10(d.rms / ref.rms), cf: d.cf }); }
  const lo = at(30).thd, hi = at(65).thd, dists = [];
  for (let i = 0; i < 33; i++) { const target = lo + (hi - lo) * i / 32;
    let a = FOLD.softEnd, b = FOLD.compEnd; for (let it = 0; it < 40; it++) { const m = (a + b) / 2; (desc(periodAnalysis([P_CAL], foldMap(m), 4096).ys, 4096, 60).thd < target ? (a = m) : (b = m)); }
    const fd = desc(periodAnalysis([P_CAL], foldMap((a + b) / 2), 4096).ys, 4096, 60);
    let j = 0; while (j < tanhRows.length - 1 && tanhRows[j + 1].thd < target) j++;
    const t0 = tanhRows[j], t1 = tanhRows[j + 1]; if (!(target >= tanhRows[0].thd && target <= tanhRows[400].thd)) throw new Error('THD outside tanh reference range'); const u = Math.min(1, Math.max(0, (target - t0.thd) / (t1.thd - t0.thd)));
    const L = t0.L + u * (t1.L - t0.L), cf = t0.cf + u * (t1.cf - t0.cf);
    dists.push(Math.hypot(20 * Math.log10(fd.rms / ref.rms) - L, fd.cf - cf)); }
  dists.sort((x, y) => x - y);
  console.log(`  SC-007(c): median distance from plain tanh ${f2(dists[16])} dB (bar 3)`);
  // saw input T1 at 100 (peak-normalised)
  for (const note of ['E1', 'C2']) { const amps = coreAmps(STATES.saw, NOTES[note], 48000);
    const pk = desc(periodAnalysis(amps, (x) => x).ys, 8192, 10); const xs = periodAnalysis(amps, (x) => x); let peak = 0; for (const v of xs.ys) peak = Math.max(peak, Math.abs(v));
    const g = P_CAL / peak; const h1in = harmonics(xs.ys, xs.M, 1)[1];
    for (const a of [65, 82.5, 100]) { const fm = foldMap(a); const pa = periodAnalysis(amps, (x) => fm(x * g) / g); const h1 = harmonics(pa.ys, pa.M, 1)[1];
      const mag = Math.hypot(h1.re, h1.im) / Math.hypot(h1in.re, h1in.im), ph = (Math.atan2(h1.im, h1.re) - Math.atan2(h1in.im, h1in.re)) * 180 / Math.PI;
      console.log(`  saw ${note} amount ${a}: |T1| ${f2(20 * Math.log10(mag))} dB, angle ${f2(ph)} deg (static map)`); } void pk; }
}

function reportFoldLevels() {
  console.log('  fold level on peak-normalised core inputs, dB re input RMS (amount 30 / 65 / 82.5 / 100):');
  for (const [nm, st] of Object.entries({ saw: STATES.saw, mid: STATES.mid, sc009: STATES.sc009 })) {
    const amps = coreAmps(st, NOTES.C2, 48000), xs = periodAnalysis(amps, (x) => x); let pk = 0; for (const v of xs.ys) pk = Math.max(pk, Math.abs(v));
    const g = P_CAL / pk, din = desc(xs.ys, xs.M, 200); let line = '   ' + nm + ' (CF ' + f2(din.cf) + ' dB):';
    for (const a of [30, 65, 82.5, 100]) { const fm = foldMap(a); const d = desc(periodAnalysis(amps, (x) => fm(x * g) / g).ys, 8192, 200); line += ' ' + f2(20 * Math.log10(d.rms / din.rms)); }
    console.log(line);
  }
}
function excParams(drive, asym, edge) {
  const D = driveLaw(drive) * edgeGain(edge), b = asym * EXC.maxBias, s = excStatic(D, b);
  return { D, b, M: s.makeup, c: s.c };
}
function foldParamsFor(a, amps) {
  const xs = periodAnalysis(amps, (x) => x); let env = 0; for (const v of xs.ys) env = Math.max(env, Math.abs(v));
  const h = desc(periodAnalysis([P_CAL], foldMap(a), 4096).ys, 4096, 10).h1.re / P_CAL;
  return { a, c: h, env };
}
function reportAlias(which = ['E0', 'E1', 'E2'], rates = [44100, 48000, 96000]) {
  console.log('\n== S9.4 Aliased power, dBFS (bar -80): plan design (4x halfband, 3-notch residual pre-filter @2fs, 500 Hz split) ==');
  for (const note of which) for (const fs of rates) {
    const f0 = NOTES[note], L = fftLen(fs, f0), secs = (L + Math.round(0.7 * fs)) / fs, res = {};
    const put = (k, v, n) => { if (!(k in res) || v > res[k][0]) res[k] = [v, n]; };
    for (const [nm, st] of Object.entries(STATES)) {
      const amps = coreAmps(st, f0, fs), tail = (y) => y.subarray(y.length - L);
      const eMax = render({ amps, f0, fs, F: 4, seconds: secs, preFc: 1, exc: excParams(1, 1, st.edge) });
      put('excMax', aliased(tail(eMax), L, fs, f0), nm);
      put('excSym', aliased(tail(render({ amps, f0, fs, F: 4, seconds: secs, preFc: 1, exc: excParams(1, 0, st.edge) })), L, fs, f0), nm);
      for (const am of [30, 65, 100]) put('fold' + am, aliased(tail(render({ amps, f0, fs, F: 4, seconds: secs, preFc: 1, fold: foldParamsFor(am, amps) })), L, fs, f0), nm);
      let ep = 0; for (const v of tail(eMax)) ep = Math.max(ep, Math.abs(v));
      const fp = foldParamsFor(100, amps); fp.env = ep;
      put('chain', aliased(tail(render({ amps, f0, fs, F: 4, seconds: secs, preFc: 1, fold: fp, xin: eMax })), L, fs, f0), nm);
    }
    console.log(`  ${note} ${fs} (FFT 2^${Math.log2(L)}): ` + Object.entries(res).map(([k, [v, n]]) => `${k} ${f2(v)} (${n})`).join(', '));
  }
  const fs = 48000, f0 = NOTES.E1, L = fftLen(fs, f0), amps = coreAmps(STATES.sc009, f0, fs);
  const y = render({ amps, f0, fs, F: 2, seconds: (L + 0.7 * fs) / fs, exc: excParams(1, 0, 1), naiveExc: true });
  console.log(`  positive control (naive base-rate transfer at Deff 3.98, E1 48 kHz, SC-009 state): ${f2(aliased(y.subarray(y.length - L), L, fs, f0))} dBFS (must read > -80)`);
}

const arg = process.argv[2] || 'all';
if (arg === 'all' || arg === 'phase') reportPhase();
if (arg === 'all' || arg === 'exc') reportExcitation();
if (arg === 'all' || arg === 'fold') { reportFold(); reportFoldLevels(); }
if (arg === 'all' || arg === 'alias') reportAlias(process.argv[3] ? [process.argv[3]] : undefined, process.argv[4] ? [Number(process.argv[4])] : undefined);
