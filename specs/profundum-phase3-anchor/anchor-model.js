#!/usr/bin/env node
// =============================================================================
// Profundum Phase 3 -- AnchorOscillator design model (plan.md S9)
// =============================================================================
// Double-precision (and Math.fround float32) model of the plan's design choices. Every figure in
// plan.md S9 is printed by one section of this script. The C++ tests are the pass/fail authority;
// this script only proves the constants and mechanisms are feasible before code is written.
//
//   node specs/profundum-phase3-anchor/anchor-model.js [eps|mcf|cal|tri|onset|bridge|alias|all]
//
// Float32 arithmetic is emulated with Math.fround in the operation order of the C++ headers it
// mirrors (harmonic_oscillator_bank.h:1057-1067, profundum_core.h:666-756). Math.sin of a float32
// argument rounded to float32 stands in for std::sin(float); a 1-ulp difference is modelled
// separately (section eps, "mirror +-1ulp").
// =============================================================================
'use strict';

const path = require('path');
const R = require(path.join(__dirname, '..', 'profundum-phase1-harmonic-core', 'recipe-model.js'));

const f32 = Math.fround;
const midiHz = (m) => 440 * Math.pow(2, (m - 69) / 12);
const NOTES = { E0: 16, E1: 28, E2: 40, C2: 36, C3: 48, C4: 60 };
const RATES = [22050, 44100, 48000, 96000, 192000];
const db10 = (x) => 10 * Math.log10(Math.max(x, 1e-300));
const db20 = (x) => 20 * Math.log10(Math.max(Math.abs(x), 1e-300));
const clamp = (x, lo, hi) => Math.min(Math.max(x, lo), hi);
const ulpUp = (x) => { const b = new Float32Array([x]); const u = new Uint32Array(b.buffer); u[0] += 1; return b[0]; };
const ulpDn = (x) => { const b = new Float32Array([x]); const u = new Uint32Array(b.buffer); u[0] -= 1; return b[0]; };

// ---- S5 mirror of the core's h1 coefficient chain (static note, Reset noteOn) ----
const kPiF = f32(Math.PI);
function bankEps(base, fs) {   // bank recalculateFrequencies, float: 2.0f*sin(kPi*freq*invFs)
  const invFs = f32(1 / f32(fs));
  const arg = f32(f32(kPiF * base) * invFs);
  return clamp(f32(2 * f32(Math.sin(arg))), f32(-1.99), f32(1.99));
}
const epsBaseD = (b, fs) => clamp(2 * Math.sin(Math.PI * b / fs), -1.99, 1.99);   // core setBase, double
function epsEffMirror(f, fs, bankEpsOverride) {
  const cap = 0.8 * fs / 2;
  const base = f;                                  // Reset noteOn: setBase(f), no chase
  const eb = epsBaseD(base, fs);
  const epsStar = f >= f32(cap) ? 2 * Math.sin(Math.PI * f32(cap) / fs) : 2 * Math.sin(Math.PI * f / fs);
  const dT = Math.abs(eb) > 1e-9 ? f32(epsStar / eb) : 1;
  let shadow = f32(1);
  const m = f32(dT / shadow);
  shadow = f32(shadow * m);
  const be = bankEpsOverride !== undefined ? bankEpsOverride : bankEps(base, fs);
  return clamp(f32(be * shadow), f32(-1.99), f32(1.99));
}
const thetaOf = (eps) => 2 * Math.asin(eps / 2);     // exact MCF rotation per sample for coefficient eps

function sectionEps() {
  console.log('\n== S9.1 eps: 60 s drift of three Anchor increment laws against the h1 MCF rotation 2*asin(epsEff/2) ==');
  console.log('note  rate    f0(float)    |doubleLaw| deg   |mirror| deg   |mirror+-1ulp| deg (worst)');
  let worstD = 0; let worstM1 = 0;
  for (const [name, midi] of Object.entries(NOTES)) {
    if (name === 'C4') continue;
    for (const fs of RATES) {
      const f = f32(midiHz(midi));
      const eE = epsEffMirror(f, fs);
      const th = thetaOf(eE);
      const N = 60 * fs;
      const dLaw = Math.abs(2 * Math.PI * f / fs - th) * N / (2 * Math.PI) * 360;
      const dMir = Math.abs(thetaOf(epsEffMirror(f, fs)) - th) * N / (2 * Math.PI) * 360;
      const be = bankEps(f, fs);
      const d1 = Math.max(Math.abs(thetaOf(epsEffMirror(f, fs, ulpUp(be))) - th),
                          Math.abs(thetaOf(epsEffMirror(f, fs, ulpDn(be))) - th)) * N / (2 * Math.PI) * 360;
      worstD = Math.max(worstD, dLaw); worstM1 = Math.max(worstM1, d1);
      console.log(`${name.padEnd(4)} ${String(fs).padEnd(7)} ${f.toFixed(5).padStart(10)}   ${dLaw.toFixed(4).padStart(10)}      ${dMir.toFixed(4).padStart(8)}      ${d1.toFixed(4).padStart(8)}`);
    }
  }
  console.log(`worst double-law drift ${worstD.toFixed(3)} deg; worst 1-ulp mirror drift ${worstM1.toFixed(3)} deg (bar 0.5 deg)`);
}

// ---- MCF float state rounding (D-1 ISA clause): plain and FMA-contracted recurrences ----
function mcfPhase(s, c, eps) {   // s = K sin(psi), c = K cos(psi + theta/2)
  const th = thetaOf(eps);
  const kc = (c + s * Math.sin(th / 2)) / Math.cos(th / 2);
  return Math.atan2(s, kc);
}
function sectionMcf() {
  console.log('\n== S9.2 mcf: float MCF state-rounding phase walk over 60 s vs exact rotation (no Anchor error) ==');
  for (const [name, midi] of [['E1', 28], ['C3', 48], ['E0', 16]]) {
    for (const fs of [48000, 96000]) {
      const eps = epsEffMirror(f32(midiHz(midi)), fs);
      const th = thetaOf(eps);
      const N = 60 * fs; const N1 = fs;
      for (const fma of [false, true]) {
        let s = f32(0); let c = f32(1); let at1 = 0;
        for (let n = 1; n <= N; ++n) {
          if (fma) { s = f32(s + eps * c); c = f32(c - eps * s); } else { s = f32(s + f32(eps * c)); c = f32(c - f32(eps * s)); }
          if (n === N1) at1 = mcfPhase(s, c, eps) - ((th * N1) % (2 * Math.PI));
        }
        let e60 = mcfPhase(s, c, eps) - ((th * N) % (2 * Math.PI));
        const wrap = (x) => Math.atan2(Math.sin(x), Math.cos(x));
        console.log(`${name} ${fs} ${fma ? 'FMA ' : 'mul+add'}: dtheta(1 s) ${(wrap(at1) * 180 / Math.PI).toExponential(2)} deg, dtheta(60 s) - dtheta(1 s) ${(wrap(e60 - at1) * 180 / Math.PI).toExponential(2)} deg`);
      }
    }
  }
}

// ---- Calibration (FR-024, SC-024; plan S1 C-2) ----
function sectionCal() {
  console.log('\n== S9.3 cal: h1 share rho = a1^2/P0 per named state, summed-signal rise at k = 1 ==');
  const states = { ...R.NAMED, kDefaultControls: { depth: 0.5, body: 0.5, bodyCurvature: 0.5, bodyEmphasis: 0, edge: 0.5, shift: 0 } };
  delete states.kTriangleAnchor;
  const k = 1;
  console.log('state             note  a1(per ch)  rho     h1 rise @L=1   RMS rise @L=0.5  @L=0.7  @L=1');
  for (const [sn, c] of Object.entries(states)) {
    for (const [nn, midi] of [['E1', 28], ['C2', 36]]) {
      const a = R.full(c, midiHz(midi), 48000, 64);
      const a1 = a[0] * Math.SQRT1_2;
      const rho = a[0] * a[0] / R.P0;
      const rise = (L) => db10(1 + rho * ((1 + k * L) ** 2 - 1));
      console.log(`${sn.padEnd(17)} ${nn}    ${a1.toFixed(5)}    ${rho.toFixed(3)}   ${db20(1 + k).toFixed(2)} dB       ${rise(0.5).toFixed(2).padStart(5)} dB      ${rise(0.7).toFixed(2).padStart(5)}  ${rise(1).toFixed(2).padStart(5)}`);
    }
  }
  console.log(`kMaxCoreH1Amplitude = sqrt(P0)*0.70710678 = ${(Math.sqrt(R.P0) * Math.SQRT1_2).toFixed(10)} (HarmonicFold::kCalibrationPeak 0.3552343859)`);
}

// ---- Triangle truncation (plan S1 C-1) ----
function sectionTri() {
  console.log('\n== S9.4 tri: odd 1/n^2 series truncated at n <= 2K-1 ==');
  const full = (Math.PI ** 4) / 96 - 1;   // sum over odd n >= 3 of n^-4
  console.log(`untruncated THD ${db10(full).toFixed(3)} dB`);
  for (const K of [8, 16, 32]) {
    let s = 0; let p = 1;
    for (let k = 1; k < K; ++k) { const n = 2 * k + 1; s += n ** -4; p += n ** -4; }
    // crest: peak of sum_k (-1)^k sin((2k+1)x)/(2k+1)^2 at x = pi/2 equals sum 1/n^2
    let peak = 0; for (let k = 0; k < K; ++k) peak += (2 * k + 1) ** -2;
    const rms = Math.sqrt(p / 2);
    console.log(`K=${K} (n<=${2 * K - 1}): THD ${db10(s).toFixed(3)} dB, omitted tail ${db10(full - s).toFixed(1)} dB re h1, first omitted n=${2 * K + 1} at ${db20((2 * K + 1) ** -2).toFixed(1)} dB, crest ${(peak / rms).toFixed(4)}`);
  }
}

// ---- Asymmetric-sine onset ramp (FR-016, SC-017; plan S4.6) ----
const kNormAsym = 1 / Math.sqrt(0.53125);
function asymRender(f, fs, rampPeriods, n) {
  const d = f / fs; const N = Math.max(1, Math.round(rampPeriods * fs / f));
  const y = new Float64Array(n);
  for (let i = 0; i < n; ++i) {
    const x = 2 * Math.PI * d * i;
    const r = i >= N ? 1 : 0.5 * (1 - Math.cos(Math.PI * i / N));
    y[i] = kNormAsym * (Math.sin(x) - 0.25 * Math.cos(2 * x) * r);
  }
  return y;
}
function sectionOnset() {
  console.log('\n== S9.5 onset: asymmetric-sine h2 onset ramp (raised cosine over P periods of f0); max |dy| first 10 ms / steady max |dy| ==');
  for (const P of [0.25, 0.5, 1.0]) {
    let worst = 0; let where = '';
    for (const [nn, midi] of Object.entries(NOTES)) {
      if (nn === 'C4') continue;
      for (const fs of RATES) {
        const f = midiHz(midi);
        const n10 = Math.ceil(0.01 * fs);
        const per = Math.ceil(fs / f);
        const y = asymRender(f, fs, P, Math.max(n10, 2 * per) + 4 * per + 2);
        let on = 0; for (let i = 1; i <= n10; ++i) on = Math.max(on, Math.abs(y[i] - y[i - 1]));
        let st = 0; for (let i = y.length - 2 * per; i < y.length; ++i) st = Math.max(st, Math.abs(y[i] - y[i - 1]));
        const ratio = on / st;
        if (ratio > worst) { worst = ratio; where = `${nn} ${fs}`; }
      }
    }
    console.log(`P=${P}: worst ratio ${worst.toFixed(4)} at ${where} (bar 1.1)`);
  }
  console.log(`first sample: ${asymRender(41.2, 48000, 0.5, 1)[0]} (FR-016 needs exactly 0)`);
}

// ---- Hermite bridge rule (FR-041; plan S1 C-3, S4.7) ----
const h00 = (t) => 2 * t ** 3 - 3 * t ** 2 + 1; const h10 = (t) => t ** 3 - 2 * t ** 2 + t; const h01 = (t) => -2 * t ** 3 + 3 * t ** 2;
function bridge(g0, gp, g1, N) { const a = new Float64Array(N + 1); for (let n = 0; n <= N; ++n) { const t = n / N; a[n] = h00(t) * g0 + h10(t) * N * gp + h01(t) * g1; } return a; }
function bridgeFits(g0, gp, g1, N, lo, hi) {   // closed-form extremum test (plan S4.7)
  const m0 = N * gp; const D = g1 - g0;
  // g'(t) = m0(3t^2-4t+1) + 6D(t - t^2) = (3m0 - 6D)t^2 + (6D - 4m0)t + m0
  const A = 3 * m0 - 6 * D; const B = 6 * D - 4 * m0; const C = m0;
  const roots = [];
  if (Math.abs(A) < 1e-15) { if (Math.abs(B) > 1e-15) roots.push(-C / B); } else {
    const disc = B * B - 4 * A * C; if (disc >= 0) { roots.push((-B - Math.sqrt(disc)) / (2 * A), (-B + Math.sqrt(disc)) / (2 * A)); }
  }
  for (const t of roots) { if (t > 0 && t < 1) { const g = h00(t) * g0 + h10(t) * m0 + h01(t) * g1; if (g < lo || g > hi) return false; } }
  return true;
}
function bridgeLength(g0, gp, g1, Nnom, lo, hi) {   // largest N <= Nnom that fits (monotone in N)
  if (bridgeFits(g0, gp, g1, Nnom, lo, hi)) return Nnom;
  let a = 1; let b = Nnom;   // fits(a) assumed; bisection on the integer grid
  if (!bridgeFits(g0, gp, g1, 1, lo, hi)) return 1;
  while (b - a > 1) { const m = (a + b) >> 1; if (bridgeFits(g0, gp, g1, m, lo, hi)) a = m; else b = m; }
  return a;
}
const maxD2 = (a) => { let m = 0; for (let i = 2; i < a.length; ++i) m = Math.max(m, Math.abs(a[i] - 2 * a[i - 1] + a[i - 2])); return m; };
function sectionBridge() {
  console.log('\n== S9.6 bridge: Hermite bridge length rule, worst cases on the raised-cosine curves it leaves ==');
  const fs = 48000;
  const cases = [];
  const B = Math.pow(10, 12 / 20);
  // PUNCH bridged arm from every point of a 1 ms rise (A = B-1, N = 48), target B over kPunchRiseMs = 48 samples.
  const Nr = Math.round(0.001 * fs);
  for (let i = 1; i < Nr; ++i) {
    const u = Math.PI * i / Nr; const g = 1 + (B - 1) * 0.5 * (1 - Math.cos(u)); const gp = (B - 1) * Math.PI / (2 * Nr) * Math.sin(u);
    cases.push(['PUNCH re-arm mid-rise', g, gp, B, Nr, 0, Math.max(B, g), (B - 1) * Math.PI ** 2 / (2 * Nr * Nr)]);
  }
  // GHOST bridged arm from every point of a 1 ms emergence (A = 1, N = 48), target 0 over kGhostDipMs.
  const Nd = Math.round(0.002 * fs);
  for (let i = 1; i < Nr; ++i) {
    const u = Math.PI * i / Nr; const g = 0.5 * (1 - Math.cos(u)); const gp = Math.PI / (2 * Nr) * Math.sin(u);
    cases.push(['GHOST re-arm mid-emergence', g, gp, 0, Nd, 0, Math.max(B, g), Math.PI ** 2 / (2 * Nr * Nr)]);
  }
  // Mode switch to steady 1 from a 5 ms PUNCH fall (A = 1-B, N = 240) over kModeBridgeMs.
  const Nf = Math.round(0.005 * fs); const Nm = Math.round(0.005 * fs);
  for (let i = 1; i < Nf; ++i) {
    const u = Math.PI * i / Nf; const g = B + (1 - B) * 0.5 * (1 - Math.cos(u)); const gp = (1 - B) * Math.PI / (2 * Nf) * Math.sin(u);
    cases.push(['mode switch mid-fall', g, gp, 1, Nm, 0, Math.max(B, g), (B - 1) * Math.PI ** 2 / (2 * Nf * Nf)]);
  }
  const summary = {};
  for (const [label, g0, gp, g1, Nnom, lo, hi, rcCurv] of cases) {
    const N = bridgeLength(g0, gp, g1, Nnom, lo, hi);
    const a = bridge(g0, gp, g1, N);
    let mn = Infinity; let mx = -Infinity; for (const v of a) { mn = Math.min(mn, v); mx = Math.max(mx, v); }
    const D = g1 - g0; const m0 = N * gp;
    const analytic = Math.max(Math.abs(6 * D - 4 * m0), Math.abs(6 * D - 2 * m0)) / (N * N);
    const slopeJump = Math.abs((a[1] - a[0]) - gp);   // incoming slope continuity (first difference vs g'(0))
    const s = summary[label] || (summary[label] = { minN: Infinity, maxCurvRatio: 0, out: 0, maxD2ratio: 0, worstSlopeJump: 0 });
    s.minN = Math.min(s.minN, N);
    s.maxCurvRatio = Math.max(s.maxCurvRatio, maxD2(a) / rcCurv);
    s.maxD2ratio = Math.max(s.maxD2ratio, maxD2(a) / analytic);
    s.worstSlopeJump = Math.max(s.worstSlopeJump, slopeJump);
    if (mn < lo - 1e-12 || mx > hi + 1e-12) s.out++;
  }
  for (const [label, s] of Object.entries(summary)) {
    console.log(`${label.padEnd(28)} min N_b ${String(s.minN).padStart(3)}, out-of-range ${s.out}, max |d2g| / analytic ${s.maxD2ratio.toFixed(4)}, max |d2g| / incoming raised-cosine peak ${s.maxCurvRatio.toFixed(3)}`);
  }
}

// ---- Alias positive control (SC-010) ----
function sectionAlias() {
  console.log('\n== S9.7 alias: naive sampled triangle, folded power of harmonics above fs/2 (dBFS re 0.5) ==');
  const a1 = Math.sqrt(R.P0) * Math.SQRT1_2; const k = 1;
  for (const [nn, midi, fs, boostDb] of [['C4', 60, 44100, 0], ['C4', 60, 44100, 12], ['E2', 40, 44100, 12], ['E0', 16, 44100, 12]]) {
    const f = midiHz(midi);
    const G = k * a1 / Math.SQRT2 * Math.pow(10, boostDb / 20);   // unit-RMS gain
    const peak = Math.sqrt(3) * G;                               // triangle peak at RMS G
    let p = 0;
    for (let n = 1; n * f < 400 * fs; n += 2) if (n * f >= fs / 2) { const a = peak * 8 / (Math.PI ** 2 * n * n); p += a * a / 2; }
    console.log(`${nn} ${fs} boost ${boostDb} dB: ${db10(p / 0.5).toFixed(1)} dBFS (bar -80)`);
  }
}

const which = process.argv[2] || 'all';
const sections = { eps: sectionEps, mcf: sectionMcf, cal: sectionCal, tri: sectionTri, onset: sectionOnset, bridge: sectionBridge, alias: sectionAlias };
for (const [k, fn] of Object.entries(sections)) if (which === 'all' || which === k) fn();
