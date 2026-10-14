#!/usr/bin/env node
// =============================================================================
// Profundum Phase 4 -- Drop / DropPhaseLock / glide design model (plan.md S10)
// =============================================================================
// Double-precision model of the plan's design. Every figure in plan.md S10 is printed by one
// section of this script. The C++ tests are the pass/fail authority; this script proves the
// constants and mechanisms are feasible before code is written.
//
//   node specs/profundum-phase4-drop-note-behaviour/drop-model.js [shape|integral|lock|lockall|lock2all|lock3 N|perSample|slope|nyquist|glide|bound|all]
// =============================================================================
'use strict';
const GLN = +(process.env.GLN || 16), GLS = +(process.env.GLS || 16), GLS_LONG = +(process.env.GLS_LONG || process.env.GLS || 16);

// ---- plan S3.2 constants ----
const K = 8.0;            // kExpRate: exponential family e^{-K u}
const DF = 0.05;          // kLandingFilletFraction
const DON = 0.05;         // kOnsetFilletFraction (bridged triggers only)
const EPS = 0.15;         // kElasticOvershootFraction (spec D-2)
const Q = 0.45;           // kElasticPeakRatio (< kElasticDecay 0.5, sampling margin)
const REB = 0.25;         // kMaxReboundFraction (spec D-3)
const RSF = 0.5;          // kReboundSettleFactor (spec D-3)
const ELAY = [1.0, 1.15, 1.30, 1.45, 1.60, 1.75, 2.0];   // elastic knot layout (units of T)
const RLAY = [1.0, 1.0, 1.0 + RSF, 1.0 + RSF, 1.0 + RSF, 1.0 + RSF, 1.0 + RSF];
const CEL = 1.5 * EPS / (ELAY[1] - ELAY[0]);              // elastic landing slope (per u, units of A)
const midiHz = (m) => 440 * Math.pow(2, (m - 69) / 12);
const NOTES = { E0: 16, E1: 28, E2: 40, C2: 36, C3: 48 };

// cubic Hermite on [x0, x1]
function herm(x, x0, x1, y0, m0, y1, m1) {
  const L = x1 - x0; const t = (x - x0) / L;
  const t2 = t * t, t3 = t2 * t;
  return (2 * t3 - 3 * t2 + 1) * y0 + (t3 - 2 * t2 + t) * L * m0 + (-2 * t3 + 3 * t2) * y1 + (t3 - t2) * L * m1;
}
function hermD(x, x0, x1, y0, m0, y1, m1) {
  const L = x1 - x0; const t = (x - x0) / L;
  const t2 = t * t;
  return ((6 * t2 - 6 * t) * y0 + (3 * t2 - 4 * t + 1) * L * m0 + (-6 * t2 + 6 * t) * y1 + (3 * t2 - 2 * t) * L * m1) / L;
}

// ---- base families, normalised fall p(u) on [0,1] ----
const ex = (u) => Math.exp(-K * u);
function fam(id, u) {   // returns [p, p']
  const a = 1 - DF;
  if (id === 0) {        // exponential
    if (u <= a) return [ex(u), -K * ex(u)];
    return [herm(u, a, 1, ex(a), -K * ex(a), 0, 0), hermD(u, a, 1, ex(a), -K * ex(a), 0, 0)];
  }
  if (id === 1) {        // linear
    if (u <= a) return [1 - u, -1];
    return [herm(u, a, 1, DF, -1, 0, 0), hermD(u, a, 1, DF, -1, 0, 0)];
  }
  // 2 logarithmic, 3 elastic fall: mirror of exponential, landing fillet
  const end = id === 3 ? -CEL : 0;
  if (u <= a) { const [pe, de] = fam(0, 1 - u); return [1 - pe, de]; }
  const y0 = 1 - ex(DF), m0 = -K * ex(DF);
  return [herm(u, a, 1, y0, m0, 0, end), hermD(u, a, 1, y0, m0, 0, end)];
}
function blendOf(s) {
  if (s <= 1 / 3) return [0, 1, 3 * s];
  if (s <= 2 / 3) return [1, 2, 3 * s - 1];
  return [2, 3, 3 * s - 2];
}
function pFall(s, u) {
  const [a, b, w] = blendOf(s);
  const [pa, da] = fam(a, u), [pb, db] = fam(b, u);
  return [(1 - w) * pa + w * pb, (1 - w) * da + w * db];
}

// ---- the planned curve d(u) in semitones, u = t/T; bridged adds the onset fillet ----
function makeCurve(A, s, R, bridged = false) {
  const rhoR = REB * R * A;
  const we = s > 2 / 3 ? 3 * s - 2 : 0;
  const knots = [];
  if (rhoR > 0 || we > 0) {
    const t = ELAY.map((e, i) => (1 - we) * RLAY[i] + we * e);
    const D1 = rhoR + we * EPS * A, D2 = Q * we * EPS * A, D3 = Q * Q * we * EPS * A;
    const v = [-(A + rhoR) * we * CEL, 0, 0, 0, 0, 0, 0];
    const safe = (num, den) => (den > 0 ? num / den : 0);
    v[2] = Math.min(1.5 * safe(D1, t[2] - t[1]), 1.5 * safe(D2, t[3] - t[2]));
    v[4] = -Math.min(1.5 * safe(D2, t[4] - t[3]), 1.5 * safe(D3, t[5] - t[4]));
    const y = [-rhoR, -D1, 0, D2, 0, -D3, 0];
    for (let i = 0; i < 7; ++i) knots.push([t[i], y[i], v[i]]);
  }
  const end = knots.length ? knots[6][0] : 1;
  const [p0, dp0] = pFall(s, DON);
  function d(u) {
    if (u <= 0) return A;
    if (u <= 1) {
      let p;
      if (bridged && u <= DON) p = herm(u, 0, DON, 1, 0, p0, dp0);
      else p = pFall(s, u)[0];
      return (A + rhoR) * p - rhoR;
    }
    if (!knots.length || u >= end) return 0;
    for (let i = 0; i < 6; ++i) {
      const [x0, y0, m0] = knots[i], [x1, y1, m1] = knots[i + 1];
      if (x1 > x0 && u >= x0 && u <= x1) return herm(u, x0, x1, y0, m0, y1, m1);
    }
    return 0;
  }
  const breaks = [0, DF, 1 - DF, 1];
  if (bridged) breaks.splice(1, 0, DON);
  for (const k of knots) breaks.push(k[0]);
  const br = [...new Set(breaks)].sort((a, b) => a - b).filter((x) => x <= end);
  return { d, end, knots, breaks: br, rhoR, we };
}

// ---- Gauss-Legendre ----
function glNodes(n) {
  const x = [], w = [];
  for (let i = 1; i <= n; ++i) {
    let z = Math.cos(Math.PI * (i - 0.25) / (n + 0.5)), pp = 1;
    for (let it = 0; it < 100; ++it) {
      let p1 = 1, p2 = 0;
      for (let j = 1; j <= n; ++j) { const p3 = p2; p2 = p1; p1 = ((2 * j - 1) * z * p2 - (j - 1) * p3) / j; }
      pp = n * (z * p1 - p2) / (z * z - 1);
      const z1 = z; z = z1 - p1 / pp; if (Math.abs(z - z1) < 1e-16) break;
    }
    x.push(z); w.push(2 / ((1 - z * z) * pp * pp));
  }
  return { x, w };
}
const GL = {};
function glInt(f, a, b, n, sub = 1) {
  const g = GL[n] || (GL[n] = glNodes(n));
  let s = 0; const h = (b - a) / sub;
  for (let k = 0; k < sub; ++k) {
    const lo = a + k * h, hi = lo + h, c = 0.5 * (lo + hi), r = 0.5 * (hi - lo);
    for (let i = 0; i < n; ++i) s += g.w[i] * r * f(c + r * g.x[i]);
  }
  return s;
}
function simpson(f, a, b, n) {   // n even
  const h = (b - a) / n; let s = f(a) + f(b);
  for (let i = 1; i < n; ++i) s += (i & 1 ? 4 : 2) * f(a + i * h);
  return s * h / 3;
}
// phase integral of (2^{d/12} - 1) over the planned curve, in units of T (multiply by T seconds).
// n = 0 -> brute-force Simpson reference; otherwise GL-n per piece, `sub` sub-intervals on the long
// exponential piece [DF, 1-DF].
function phaseIntegralU(c, extra, n, sub) {
  const f = (u) => Math.pow(2, (c.d(u) + (extra ? extra(u) : 0)) / 12) - 1;
  let s = 0;
  for (let i = 0; i + 1 < c.breaks.length; ++i) {
    const a = c.breaks[i], b = c.breaks[i + 1];
    if (!(b > a)) continue;
    if (!n) s += simpson(f, a, b, 20000);
    else s += glInt(f, a, b, n, b - a > 0.5 ? sub : 1);
  }
  return s;
}

// =============================================================================
function secShape() {
  console.log('== shape (SC-001/SC-003 feasibility) ==');
  const N = 200001;
  const alpha = (s) => { let a = 0; for (let i = 0; i < N; ++i) a += pFall(s, i / (N - 1))[0]; return a / N; };
  for (const [nm, s] of [['exp', 0], ['lin', 1 / 3], ['log', 2 / 3], ['el', 1]]) {
    let minp = 1, mono = true, prev = 2;
    for (let i = 0; i < N; ++i) { const p = pFall(s, i / (N - 1))[0]; minp = Math.min(minp, p); if (p > prev + 1e-15) mono = false; prev = p; }
    const [, d0] = pFall(s, 0);
    console.log(`  ${nm}: alpha=${alpha(s).toFixed(5)} min p on [0,1]=${minp.toExponential(3)} monotone=${mono} p'(0)=${d0.toFixed(4)} p'(1)=${pFall(s, 1)[1].toFixed(4)}`);
  }
  const r2 = (xs, ys) => { const n = xs.length, mx = xs.reduce((a, b) => a + b) / n, my = ys.reduce((a, b) => a + b) / n;
    let sxy = 0, sxx = 0, syy = 0; for (let i = 0; i < n; ++i) { sxy += (xs[i] - mx) * (ys[i] - my); sxx += (xs[i] - mx) ** 2; syy += (ys[i] - my) ** 2; }
    return [sxy * sxy / (sxx * syy), sxy / sxx]; };
  { const xs = [], ys = []; for (let u = 0.05; u <= 0.8; u += 0.001) { xs.push(u); ys.push(Math.log(pFall(0, u)[0])); }
    const [r, sl] = r2(xs, ys); console.log(`  exp ln p R^2=${r.toFixed(9)} fitted rate=${(-sl).toFixed(4)}`); }
  { const xs = [], ys = []; for (let u = 0.2; u <= 0.95; u += 0.001) { xs.push(1 - u); ys.push(Math.log(1 - pFall(2 / 3, u)[0])); }
    const [r] = r2(xs, ys); console.log(`  log ln(1-p) R^2=${r.toFixed(9)}`); }
  { let m = 0; for (let u = 0.05; u <= 0.95; u += 0.0005) m = Math.max(m, Math.abs(pFall(1 / 3, u)[0] - (1 - u))); console.log(`  lin max|p-(1-u)| on [0.05,0.95]=${m.toExponential(3)}`); }
  for (const R of [0, 0.5, 1]) for (const s of [0, 1 / 3, 2 / 3, 5 / 6, 1]) {
    const c = makeCurve(1, s, R); const M = 400001; let mn = 0, zc = 0, peaks = [], ext = 0, sgn = 0, firstZero = -1;
    let crossedZero = false;
    for (let i = 0; i < M; ++i) { const u = 2.2 * i / (M - 1); const d = c.d(u);
      mn = Math.min(mn, d);
      if (!crossedZero && d <= 0 && u > 0) { crossedZero = true; firstZero = u; continue; }
      if (crossedZero) { const sg = d > 1e-15 ? 1 : d < -1e-15 ? -1 : 0; if (sg !== 0 && sgn !== 0 && sg !== sgn) { zc++; peaks.push(ext); ext = 0; } if (sg !== 0) sgn = sg; if (Math.abs(d) > Math.abs(ext)) ext = d; } }
    peaks.push(ext);
    const rhoLaw = (s > 2 / 3 ? 3 * s - 2 : 0) * EPS + R * REB;
    console.log(`  s=${s.toFixed(3)} R=${R}: rho=${(-mn).toFixed(6)} law=${rhoLaw.toFixed(6)} first d<=0 at u=${firstZero.toFixed(4)} zero-crossings after=${zc} peaks=${peaks.map((p) => p.toFixed(4)).join(',')} end=${c.end.toFixed(3)}`);
  }
  { let worst = 0; for (let k = 0; k < 192; ++k) { const ca = makeCurve(1, k / 192, 0), cb = makeCurve(1, (k + 1) / 192, 0);
      for (let u = 0; u <= 2; u += 0.0005) worst = Math.max(worst, Math.abs(ca.d(u) - cb.d(u))); }
    console.log(`  shape continuity 192 steps: max|dp|=${worst.toFixed(5)} (bar 0.025)`); }
}

function secIntegral() {
  console.log('== integral (SC-024: GL vs brute force, relative) ==');
  const cfgs = [[8, 1], [8, 4], [16, 4], [24, 4]];
  const worst = cfgs.map(() => [0, '']);
  for (const A of [1, 12, 48]) for (const R of [0, 0.5, 1]) for (let k = 0; k <= 64; k += 4) for (const br of [false, true]) {
    const s = k / 64; const c = makeCurve(A, s, R, br);
    const ref = phaseIntegralU(c, null, 0, 0);
    cfgs.forEach(([n, sub], i) => {
      const rel = Math.abs(phaseIntegralU(c, null, n, sub) - ref) / Math.abs(ref);
      if (rel > worst[i][0]) worst[i] = [rel, `A=${A} s=${s.toFixed(4)} R=${R} bridged=${br}`];
    });
  }
  cfgs.forEach(([n, sub], i) => console.log(`  GL${n} (x${sub} on [DF,1-DF]): worst rel err ${worst[i][0].toExponential(3)} at ${worst[i][1]}`));
}

// ---- staircase-exact render of the core's h1 excess phase (plan S5.3) ----
// The core samples f0PerSample at every 16th sample (profundum_core.h:253-266) and holds it; f is clamped
// to [kMinF0Hz = 8, capHz] (profundum_core.h:805-811).
function bump(x) { return (x > 0 && x < 1) ? 16 * x * x * (1 - x) * (1 - x) : 0; }
function renderExcess(c, A, T, fres, fs, g1, D, capHz, trim) {
  const N = Math.ceil(Math.max(c.end * T, D) * fs) + 64;
  let held = 0, S = 0, minF = 1e9, maxF = 0, maxUp = 0, prevDc = A, maxC = 0;
  let g2 = 0, tw = trim ? trim.xw * D : Infinity, Dw = D - tw;
  for (let n = 0; n < N; ++n) {
    const t = n / fs;
    if (trim && n === Math.round(tw * fs)) { tw = n / fs; Dw = D - tw; g2 = trim.solve(S, tw, Dw); }
    const cc = g1 * bump(t / D) + (trim && t >= tw ? g2 * bump((t - tw) / Dw) : 0);
    const dc = c.d(t / T) + cc;
    maxC = Math.max(maxC, Math.abs(cc));
    if (n > 0) maxUp = Math.max(maxUp, dc - prevDc); prevDc = dc;
    if (n % 16 === 0) {
      held = Math.min(Math.max(fres * Math.pow(2, dc / 12), 8), capHz);
      minF = Math.min(minF, held); maxF = Math.max(maxF, held);
    }
    S += (held - fres) / fs;
  }
  return { S, minF, maxF, maxUp, maxC, g2 };
}
// prediction: continuous integral of the corrected curve + Euler-Maclaurin endpoint terms of the
// left-Riemann sample-and-hold (plan S5.3)
function contExcess(c, A, T, fres, g1, D, from) {
  const f = (t) => fres * (Math.pow(2, (c.d(t / T) + g1 * bump(t / D)) / 12) - 1);
  const br = [...new Set([...c.breaks.map((u) => u * T), D])].filter((x) => x >= from).concat([from]).sort((a, b) => a - b);
  const end = Math.max(c.end * T, D);
  let s = 0;
  for (let i = 0; i + 1 < br.length; ++i) { const a = br[i], b = Math.min(br[i + 1], end); if (b > a) s += glInt(f, a, b, 16, (b - a) > 0.5 * T ? 4 : 1); }
  return { s, f };
}
function predictExcess(c, A, T, fres, fs, g1, D) {
  const { s, f } = contExcess(c, A, T, fres, g1, D, 0);
  const h = 16 / fs;
  const dt = 1e-9 * T;
  const gp0 = (f(dt) - f(0)) / dt;
  return s + 0.5 * h * f(0) - h * h / 12 * gp0;
}
function solveGamma(fn, target) {
  let g = 0;
  for (let it = 0; it < 80; ++it) {
    const f0 = fn(g) - target;
    if (Math.abs(f0) < 1e-11) break;
    const e = 1e-5 * Math.max(1, Math.abs(g));
    const f1 = fn(g + e) - target;
    let step = -f0 * e / (f1 - f0);
    if (!isFinite(step)) break;
    step = Math.max(-12, Math.min(12, step));
    g += step;
  }
  return g;
}
const wrap = (x) => 360 * (x - Math.round(x));

function lockGrid(rates, useTrim, xw) {
  let worstPred = 0, worstPredCell = '', worstTheta = 0, worstThetaCell = '', negCtl = 0;
  const peakByT = {}; const upByT = {}; let clampHits = 0; const clampCells = [];
  for (const fs of rates) for (const nm of ['E0', 'E1', 'E2', 'C3']) for (const A of [6, 12, 24, 48]) for (const T of [0.010, 0.060, 0.200, 1.0])
    for (const s of [0, 1 / 3, 2 / 3, 1]) for (const R of [0, 1]) {
      const fres = midiHz(NOTES[nm]); const cap = 0.8 * fs / 2;
      const c = makeCurve(A, s, R); const D = c.end * T;
      const ex0 = renderExcess(c, A, T, fres, fs, 0, D, cap, null);
      const pr0 = predictExcess(c, A, T, fres, fs, 0, D);
      const ep = Math.abs(ex0.S - pr0) * 360;
      if (ep > worstPred) { worstPred = ep; worstPredCell = `${nm} fs=${fs} A=${A} T=${T} s=${s.toFixed(2)} R=${R} Phi=${ex0.S.toFixed(3)}`; }
      negCtl = Math.max(negCtl, Math.abs(wrap(ex0.S)));
      const target = Math.round(pr0);
      const g = solveGamma((x) => predictExcess(c, A, T, fres, fs, x, D), target);
      let trim = null;
      if (useTrim) {
        trim = { xw, solve: (Sacc, tw, Dw) => {
          // remaining planned excess from tw with g1, plus the trim bump; EM first-order at tw (grid offset)
          const fnr = (g2) => {
            const f = (t) => fres * (Math.pow(2, (c.d(t / T) + g * bump(t / D) + g2 * bump((t - tw) / Dw)) / 12) - 1);
            const br = [...new Set([...c.breaks.map((u) => u * T), D, tw])].filter((x) => x >= tw).sort((a, b) => a - b);
            let s = 0; for (let i = 0; i + 1 < br.length; ++i) if (br[i + 1] > br[i]) s += glInt(f, br[i], br[i + 1], 16, 1);
            const nw = Math.round(tw * fs); const phase = nw % 16; // samples since last update
            const h = 16 / fs; const held = Math.min(Math.max(fres * Math.pow(2, (c.d((nw - phase) / fs / T) + g * bump((nw - phase) / fs / D)) / 12), 8), cap) - fres;
            const r0 = (16 - phase) % 16;   // samples until next update
            const tNext = tw + r0 / fs;
            // held value until tNext, then left-Riemann from tNext: integral from tNext + h/2 g(tNext) - h^2/12 g'(tNext)
            const dt = 1e-9;
            const sHead = glInt(f, tw, tNext, 8, 1);
            return s - sHead + held * r0 / fs + 0.5 * h * f(tNext) - h * h / 12 * (f(tNext + dt) - f(tNext)) / dt;
          };
          return solveGamma((g2) => Sacc + fnr(g2), target);
        } };
      }
      const ex = renderExcess(c, A, T, fres, fs, g, D, cap, trim);
      const th = Math.abs(wrap(ex.S));
      if (th > worstTheta) { worstTheta = th; worstThetaCell = `${nm} fs=${fs} A=${A} T=${T} s=${s.toFixed(2)} R=${R} g1=${g.toFixed(2)} g2=${ex.g2.toFixed(3)}`; }
      if (ex.minF <= 8 || ex.maxF >= cap) { clampHits++; if (clampCells.length < 8) clampCells.push(`${nm} A=${A} T=${T} s=${s.toFixed(2)} R=${R} g=${g.toFixed(1)} f=[${ex.minF.toFixed(1)},${ex.maxF.toFixed(0)}]`); }
      const key = `${Math.round(T * 1000)}ms`;
      peakByT[key] = Math.max(peakByT[key] || 0, ex.maxC);
      upByT[key] = Math.max(upByT[key] || 0, ex.maxUp);
    }
  return { worstPred, worstPredCell, worstTheta, worstThetaCell, negCtl, peakByT, upByT, clampHits, clampCells };
}
function secLock(rates, trims) {
  console.log(`== lock (SC-013 feasibility: staircase-exact render vs plan), rates ${rates.join('/')} ==`);
  for (const [useTrim, xw] of trims) {
    const r = lockGrid(rates, useTrim, xw);
    console.log(` -- ${useTrim ? `two-stage (trim at x=${xw})` : 'single-stage'}`);
    console.log(`  open-loop prediction error (uncorrected): worst ${r.worstPred.toFixed(3)} deg at ${r.worstPredCell}`);
    console.log(`  negative control (no correction) worst |theta| = ${r.negCtl.toFixed(1)} deg`);
    console.log(`  corrected landing worst |theta| = ${r.worstTheta.toFixed(4)} deg at ${r.worstThetaCell}`);
    console.log(`  peak |c| (semitones) by T: ${JSON.stringify(r.peakByT, (k, v) => typeof v === 'number' ? +v.toFixed(2) : v)}`);
    console.log(`  max upward step of d_c per sample by T: ${JSON.stringify(r.upByT, (k, v) => typeof v === 'number' ? +v.toFixed(4) : v)}`);
    console.log(`  cells hitting the core's [8 Hz, cap] clamp: ${r.clampHits} ${r.clampCells.join(' | ')}`);
  }
}

function secNyquist() {
  console.log('== nyquist (SC-015: slope bound per pitch-update interval) ==');
  const capFrac = 0.8, U = 16;
  const riseMax = 12 * Math.log2(1 / capFrac);
  console.log(`  max rise between mask build and next pitch update: 12*log2(1/0.8) = ${riseMax.toFixed(4)} st over ${U} samples -> ${(riseMax / U).toFixed(5)} st/sample`);
  const sig = 0.15;
  console.log(`  sigma_up = ${sig}: continuous laws rise <= ${(sig * U).toFixed(2)} st/16 samples; quantized output <= floor(${(sig * U).toFixed(2)})+1 = ${Math.floor(sig * U) + 1} st`);
  for (const fs of [44100, 48000, 96000]) for (const A of [12, 48]) {
    const L = Math.ceil(1.5 * A / sig); console.log(`  bridge rise from 0 to A=${A} at fs=${fs}: ${L} samples = ${(1000 * L / fs).toFixed(2)} ms`);
  }
}

function secGlide() {
  console.log('== glide (SC-007/SC-008 double-counter laws) ==');
  let worstDur = 0, worstStep = 0;
  for (const fs of [44100, 48000, 96000]) {
    for (const d of [1, 7, 12, 24]) for (const T of [120, 10000]) {
      const rate = 12 * 1000 / (T * fs); let pos = 0, n = 0; while (pos < d) { ++n; pos = Math.min(d, n * rate); }
      worstDur = Math.max(worstDur, Math.abs(n - T * d / 12 * fs / 1000));
    }
    for (const d of [1, 5, 12, 24]) for (const T of [50, 500, 10000]) {
      const N = T * fs / 1000; let last = 0;
      for (let n = 1; n <= Math.ceil(N) + 2; ++n) {
        const pos = Math.min(1, n / N) * d; const out = d - Math.ceil(d - pos);
        if (out !== last) { worstStep = Math.max(worstStep, Math.abs(n - Math.round(out * N / d))); last = out; }
      }
    }
  }
  console.log(`  constant-rate duration error worst ${worstDur.toFixed(3)} samples (bar 1); quantized step-time error worst ${worstStep} samples (bar 1)`);
}

const arg = process.argv[2] || 'all';
if (arg === 'shape' || arg === 'all') secShape();
if (arg === 'integral' || arg === 'all') secIntegral();
if (arg === 'lock' || arg === 'all') secLock([48000], [[false, 0], [true, 0.75]]);
if (arg === 'lockall') secLock([44100, 48000, 96000], [[true, 0.75]]);
if (arg === 'nyquist' || arg === 'all') secNyquist();
if (arg === 'glide' || arg === 'all') secGlide();

// ---- lock2: clamp-aware prediction, floor/ceil target choice, feasibility census (plan S5.4) ----
function predictClamped(c, T, fres, fs, g1, D, cap) {
  const f = (t) => Math.min(Math.max(fres * Math.pow(2, (c.d(t / T) + g1 * bump(t / D)) / 12), 8), cap) - fres;
  const br = [...new Set([...c.breaks.map((u) => u * T), D])].sort((a, b) => a - b);
  const end = Math.max(c.end * T, D);
  let s = 0;
  for (let i = 0; i + 1 < br.length; ++i) { const a = br[i], b = Math.min(br[i + 1], end); if (b > a) s += glInt(f, a, b, 16, 16); }
  const h = 16 / fs, dt = 1e-9 * T;
  return s + 0.5 * h * f(0) - h * h / 12 * (f(dt) - f(0)) / dt;
}
function bisectGamma(fn, target, lim) {
  let lo = -lim, hi = lim; const flo = fn(lo) - target, fhi = fn(hi) - target;
  if (flo > 0 || fhi < 0) return NaN;
  for (let i = 0; i < 70; ++i) { const m = 0.5 * (lo + hi); if (fn(m) - target > 0) hi = m; else lo = m; }
  return 0.5 * (lo + hi);
}
function secLock2(rates) {
  console.log(`== lock2: clamp-aware single-stage, nearest-feasible target, rates ${rates.join('/')} ==`);
  const lim = 48; const buckets = {}; let worstPred = 0, wpc = '', worstTh = {}, wtc = {};
  for (const fs of rates) for (const nm of ['E0', 'E1', 'E2', 'C3']) for (const A of [6, 12, 24, 48]) for (const T of [0.010, 0.060, 0.200, 1.0])
    for (const s of [0, 1 / 3, 2 / 3, 1]) for (const R of [0, 1]) {
      const fres = midiHz(NOTES[nm]); const cap = 0.8 * fs / 2; const c = makeCurve(A, s, R); const D = c.end * T;
      const ex0 = renderExcess(c, A, T, fres, fs, 0, D, cap, null);
      const p0 = predictClamped(c, T, fres, fs, 0, D, cap);
      const e = Math.abs(ex0.S - p0) * 360; if (e > worstPred) { worstPred = e; wpc = `${nm} A=${A} T=${T} s=${s.toFixed(2)} R=${R}`; }
      const fn = (g) => predictClamped(c, T, fres, fs, g, D, cap);
      const cands = [Math.floor(p0), Math.ceil(p0)].map((n) => [n, bisectGamma(fn, n, lim)]).filter((x) => isFinite(x[1]));
      cands.sort((a, b) => Math.abs(a[1]) - Math.abs(b[1]));
      const key = `${Math.round(T * 1000)}ms`; const bk = buckets[key] || (buckets[key] = { le3: 0, le12: 0, le24: 0, le48: 0, inf: 0, n: 0, maxG: 0 });
      bk.n++;
      if (!cands.length) { bk.inf++; continue; }
      const g = cands[0][1]; const ag = Math.abs(g); bk.maxG = Math.max(bk.maxG, ag);
      if (ag <= 3) bk.le3++; else if (ag <= 12) bk.le12++; else if (ag <= 24) bk.le24++; else bk.le48++;
      const ex = renderExcess(c, A, T, fres, fs, g, D, cap, null);
      const th = Math.abs(wrap(ex.S));
      const tb = ag <= 12 ? 'le12' : 'gt12';
      if (!(tb in worstTh) || th > worstTh[tb]) { worstTh[tb] = th; wtc[tb] = `${nm} fs=${fs} A=${A} T=${T} s=${s.toFixed(2)} R=${R} g=${g.toFixed(2)}`; }
    }
  console.log(`  clamp-aware open-loop prediction error (uncorrected): worst ${worstPred.toFixed(4)} deg at ${wpc}`);
  console.log(`  required |gamma| census by T (counts): ${JSON.stringify(buckets, (k, v) => typeof v === 'number' ? +v.toFixed(2) : v)}`);
  for (const k of Object.keys(worstTh)) console.log(`  corrected landing worst |theta| (|gamma| ${k}) = ${worstTh[k].toFixed(4)} deg at ${wtc[k]}`);
}
if (arg === 'lock2') secLock2([48000]);
if (arg === 'lock2all') secLock2([44100, 48000, 96000]);

// ---- lock3: hybrid predictor - first M0 pitch updates summed exactly, EM from there (plan S5.3) ----
function predictHybrid(c, T, fres, fs, g1, D, cap, M0) {
  const f = (t) => Math.min(Math.max(fres * Math.pow(2, (c.d(t / T) + g1 * bump(t / D)) / 12), 8), cap) - fres;
  const h = 16 / fs; const t0 = M0 * h;
  let s = 0; for (let j = 0; j < M0; ++j) s += h * f(j * h);
  const end = Math.max(c.end * T, D);
  if (t0 >= end) return s;
  const br = [...new Set([...c.breaks.map((u) => u * T), D, t0])].filter((x) => x >= t0).sort((a, b) => a - b);
  for (let i = 0; i + 1 < br.length; ++i) { const a = br[i], b = Math.min(br[i + 1], end); if (b > a) s += glInt(f, a, b, GLN, (b - a) > 0.5 * T ? GLS_LONG : GLS); }
  const dt = 1e-9 * T;
  return s + 0.5 * h * f(t0) - h * h / 12 * (f(t0 + dt) - f(t0)) / dt;
}
function secLock3(rates, M0) {
  let worstPred = 0, wpc = '', worstTh = 0, wtc = '';
  for (const fs of rates) for (const nm of ['E0', 'E1', 'E2', 'C3']) for (const A of [6, 12, 24, 48]) for (const T of [0.010, 0.060, 0.200, 1.0])
    for (const s of [0, 1 / 3, 2 / 3, 1]) for (const R of [0, 1]) {
      const fres = midiHz(NOTES[nm]); const cap = 0.8 * fs / 2; const c = makeCurve(A, s, R); const D = c.end * T;
      const ex0 = renderExcess(c, A, T, fres, fs, 0, D, cap, null);
      const fn = (g) => predictHybrid(c, T, fres, fs, g, D, cap, M0);
      const p0 = fn(0);
      const e = Math.abs(ex0.S - p0) * 360; if (e > worstPred) { worstPred = e; wpc = `${nm} fs=${fs} A=${A} T=${T} s=${s.toFixed(2)} R=${R}`; }
      const cands = [Math.floor(p0), Math.ceil(p0)].map((n) => [n, bisectGamma(fn, n, 48)]).filter((x) => isFinite(x[1]));
      cands.sort((a, b) => Math.abs(a[1]) - Math.abs(b[1]));
      const g = cands[0][1];
      const th = Math.abs(wrap(renderExcess(c, A, T, fres, fs, g, D, cap, null).S));
      if (th > worstTh) { worstTh = th; wtc = `${nm} fs=${fs} A=${A} T=${T} s=${s.toFixed(2)} R=${R} g=${g.toFixed(2)}`; }
    }
  console.log(`== lock3 hybrid M0=${M0}, rates ${rates.join('/')}: prediction error worst ${worstPred.toFixed(4)} deg at ${wpc}; corrected |theta| worst ${worstTh.toFixed(4)} deg at ${wtc}`);
}
if (arg === 'lock3') secLock3([44100, 48000, 96000], +(process.argv[3] || 8));

// ---- perSample: SC-024 second clause - prediction at U = 1 vs the per-sample double sum ----
function secPerSample() {
  let worst = 0, cell = '';
  for (const fs of [44100, 48000, 96000]) for (const nm of ['E0', 'C3']) for (const A of [1, 12, 48]) for (const T of [0.001, 0.020, 0.200, 4.0])
    for (const s of [0, 1 / 3, 2 / 3, 5 / 6, 1]) for (const R of [0, 0.5, 1]) {
      const fres = midiHz(NOTES[nm]); const c = makeCurve(A, s, R); const end = c.end * T;
      const N = Math.ceil(end * fs) + 2; let S = 0;
      for (let n = 0; n < N; ++n) S += fres * (Math.pow(2, c.d(n / fs / T) / 12) - 1) / fs;
      const f = (t) => fres * (Math.pow(2, c.d(t / T) / 12) - 1);
      const h = 1 / fs; let P = 0; for (let j = 0; j < 4; ++j) P += h * f(j * h);
      const t0 = 4 * h; const br = [...new Set([...c.breaks.map((u) => u * T), t0])].filter((x) => x >= t0).sort((a, b) => a - b);
      for (let i = 0; i + 1 < br.length; ++i) if (br[i + 1] > br[i]) P += glInt(f, br[i], Math.min(br[i + 1], end), 8, (br[i + 1] - br[i]) > 0.5 * T ? 4 : 1);
      const dt = 1e-9 * T; P += 0.5 * h * f(t0) - h * h / 12 * (f(t0 + dt) - f(t0)) / dt;
      const e = Math.abs(S - P); if (e > worst) { worst = e; cell = `${nm} fs=${fs} A=${A} T=${T} s=${s.toFixed(2)} R=${R} Phi=${S.toFixed(4)}`; }
    }
  console.log(`== perSample (U=1, GL8, M0=4): worst |sum - predicted| = ${worst.toExponential(3)} cycles at ${cell}`);
}
if (arg === 'perSample') secPerSample();

// ---- slope: SC-013 x SC-015 interaction - does the sigma_up cap on d_c bind inside the SC-013 grid? ----
function secSlope(rates) {
  const sig = 0.15; const bind = {}; let worstUncorr = 0, wu = '';
  for (const fs of rates) for (const nm of ['E0', 'E1', 'E2', 'C3']) for (const A of [6, 12, 24, 48]) for (const T of [0.010, 0.060, 0.200, 1.0])
    for (const s of [0, 1 / 3, 2 / 3, 1]) for (const R of [0, 1]) {
      const fres = midiHz(NOTES[nm]); const cap = 0.8 * fs / 2; const c = makeCurve(A, s, R); const D = c.end * T;
      const fn = (g) => predictHybrid(c, T, fres, fs, g, D, cap, 4);
      const p0 = fn(0);
      const cands = [Math.floor(p0), Math.ceil(p0)].map((n) => [n, bisectGamma(fn, n, 48)]).filter((x) => isFinite(x[1]));
      cands.sort((a, b) => Math.abs(a[1]) - Math.abs(b[1]));
      const g = cands[0][1];
      const r0 = renderExcess(c, A, T, fres, fs, 0, D, cap, null); if (r0.maxUp > worstUncorr) { worstUncorr = r0.maxUp; wu = `${nm} fs=${fs} A=${A} T=${T} s=${s.toFixed(2)} R=${R}`; }
      // feasible candidates under the slope cap
      const ok = cands.filter(([, gg]) => renderExcess(c, A, T, fres, fs, gg, D, cap, null).maxUp <= sig);
      const key = `${Math.round(T * 1000)}ms`; const b = bind[key] || (bind[key] = { n: 0, capBinds: 0, cells: [] });
      b.n++; if (!ok.length) { b.capBinds++; if (b.cells.length < 4) b.cells.push(`${nm}/${fs}/A${A}/s${s.toFixed(2)}/R${R}/g${g.toFixed(1)}`); }
    }
  console.log(`== slope (sigma_up = ${sig}) rates ${rates.join('/')}: uncorrected curve worst upward step ${worstUncorr.toFixed(4)} st/sample at ${wu}`);
  console.log(`  cells where no landing target meets the slope cap: ${JSON.stringify(bind)}`);
}
if (arg === 'slope') secSlope([44100, 48000, 96000]);

// ---- bound: SC-013(c) solver-independent analytic peak of the correction (plan C-5) ----
// Inputs: the planned curve d, f_res, the landing D and the residual r = n* - Phi_ref(0), where Phi_ref(0)
// is the staircase-exact uncorrected excess (the test measures it from the bypass render). Nothing here
// reads the solver's gamma. b(x) = 16x^2(1-x)^2, mean 8/15, peak/mean 15/8.
//   gamma > 0:  2^{gb/12} - 1 >= g*b*ln2/12      ->  g <= (12/ln2) * (15/8) |r| / (f_res * kappa * D)
//               2^{gb/12} - 1 >= (2^{g*beta/12}-1) on {b >= beta}  ->  g <= (12/beta) log2(1 + |r|/(f_res W_beta))
//   gamma < 0:  1 - 2^{-|g|b/12} >= b (1 - 2^{-|g|/12})  ->  |g| <= -12 log2(1 - (15/8)|r|/(f_res kappa D))
//               1 - 2^{-|g|b/12} >= (1 - 2^{-|g|beta/12}) on {b >= beta} -> |g| <= -(12/beta) log2(1 - |r|/(f_res W_beta))
// kappa = b-weighted mean of 2^{d/12} over samples that stay inside [8 Hz, cap] at d + g_an*b (fixed point);
// W_beta = integral of 2^{d/12} over {b >= beta} with the same indicator. gamma_an = min over the valid forms.
function analyticPeak(c, T, fres, D, r, cap) {
  // L(g): a lower bound on the phase |gamma| = g moves, built only from d, f_res, D and the profile b.
  //   unclamped at g, gamma > 0:  f_res 2^{d/12} * max(g b ln2/12, [b >= beta](2^{g beta/12} - 1))   (convexity)
  //   unclamped at g, gamma < 0:  f_res 2^{d/12} * max(b (1 - 2^{-g/12}), [b >= beta](1 - 2^{-g beta/12})) (concavity)
  //   clamped at g (pitch pushed through 8 Hz or the cap): the saturated move |clamp(f_g) - f_0|, exact
  //   already clamped at gamma = 0: 0
  // L is non-decreasing in g, so gamma_an = inf{g : L(g) >= |r|} by bisection; +Inf if L(48) < |r|.
  // Integrated on D's midpoint grid (4000 points); summed, L(g) in cycles.
  const M = 4000, dt = D / M, ln2 = Math.LN2, betas = [0.25, 0.5, 0.75];
  const sgn = r >= 0 ? 1 : -1, ar = Math.abs(r);
  if (ar === 0) return 0;
  const pts = [];
  for (let i = 0; i < M; ++i) { const t = (i + 0.5) * dt; pts.push([bump(t / D), c.d(t / T)]); }
  const clampF = (f) => Math.min(Math.max(f, 8), cap);
  const L = (g) => {
    let sum = 0;
    for (const [bb, dd] of pts) {
      const f0 = fres * Math.pow(2, dd / 12);
      if (f0 < 8 || f0 > cap) continue;
      const fg = fres * Math.pow(2, (dd + sgn * g * bb) / 12);
      if (fg < 8 || fg > cap) { sum += Math.abs(clampF(fg) - f0) * dt; continue; }
      let m;
      if (sgn > 0) { m = g * bb * ln2 / 12; for (const be of betas) if (bb >= be) m = Math.max(m, Math.pow(2, g * be / 12) - 1); }
      else { m = bb * (1 - Math.pow(2, -g / 12)); for (const be of betas) if (bb >= be) m = Math.max(m, 1 - Math.pow(2, -g * be / 12)); }
      sum += f0 * m * dt;
    }
    return sum;
  };
  if (L(48) < ar) return Infinity;
  let lo = 0, hi = 48;
  for (let i = 0; i < 60; ++i) { const mid = 0.5 * (lo + hi); if (L(mid) >= ar) hi = mid; else lo = mid; }
  return hi;
}
function secBound(rates) {
  let worst = 0, wc = '', over = 0, n = 0, inf = 0; const byT = {};
  for (const fs of rates) for (const nm of ['E0', 'E1', 'E2', 'C3']) for (const A of [6, 12, 24, 48]) for (const T of [0.010, 0.060, 0.200, 1.0])
    for (const s of [0, 1 / 3, 2 / 3, 1]) for (const R of [0, 1]) {
      const fres = midiHz(NOTES[nm]); const cap = 0.8 * fs / 2; const c = makeCurve(A, s, R); const D = c.end * T;
      const fn = (g) => predictHybrid(c, T, fres, fs, g, D, cap, 4);
      const p0 = fn(0);
      const cands = [Math.floor(p0), Math.ceil(p0)].map((k) => [k, bisectGamma(fn, k, 48)]).filter((x) => isFinite(x[1]));
      cands.sort((a, b) => Math.abs(a[1]) - Math.abs(b[1]));
      const [nstar, g] = cands[0];
      const ref0 = renderExcess(c, A, T, fres, fs, 0, D, cap, null).S;      // independent Phi_ref(0)
      const gan = analyticPeak(c, T, fres, D, nstar - ref0, cap);
      n++; if (!isFinite(gan)) { inf++; if (process.env.SHOWINF) console.log(`  unbounded: ${nm}/${fs}/A${A}/T${T}/s${s.toFixed(2)}/R${R}/g${g.toFixed(2)}/r${(nstar - ref0).toFixed(3)}`); continue; }
      const ratio = gan > 0 ? Math.abs(g) / gan : 0;
      const key = `${Math.round(T * 1000)}ms`; const bk = byT[key] || (byT[key] = { maxRatio: 0, minRatio: 1e9 });
      bk.maxRatio = Math.max(bk.maxRatio, ratio); if (ratio < bk.minRatio) { bk.minRatio = ratio; bk.minCell = `${nm}/${fs}/A${A}/s${s.toFixed(2)}/R${R}/g${g.toFixed(2)}/gan${gan.toFixed(2)}`; }
      bk.lt05 = (bk.lt05 || 0) + (ratio < 0.5 ? 1 : 0); bk.lt09 = (bk.lt09 || 0) + (ratio < 0.9 ? 1 : 0);
      if (Math.abs(g) > 1.05 * gan) over++;
      if (ratio > worst) { worst = ratio; wc = `${nm} fs=${fs} A=${A} T=${T} s=${s.toFixed(2)} R=${R} g=${g.toFixed(3)} gan=${gan.toFixed(3)}`; }
    }
  console.log(`== bound (solver-independent gamma_an), rates ${rates.join('/')}: ${n} cells, ${inf} unbounded, ${over} with |gamma| > 1.05 gamma_an`);
  console.log(`  worst |gamma|/gamma_an = ${worst.toFixed(4)} at ${wc}`);
  console.log(`  ratio range by T: ${JSON.stringify(byT, (k, v) => typeof v === 'number' ? +v.toFixed(4) : v)}`);
}
if (arg === 'bound') secBound([44100, 48000, 96000]);
