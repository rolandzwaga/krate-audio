#!/usr/bin/env node
// =============================================================================
// Profundum Phase 1 -- SpectralShapeRecipe reference model + recipe-side gates
// =============================================================================
// Double-precision JS model of the recipe law fixed in plan.md section 3. It is
// the calibration record for every constant in that section: run it after ANY
// constant change and paste the output into the plan's calibration table.
//
//   node specs/profundum-phase1-harmonic-core/recipe-model.js
//
// It checks the recipe-vector halves of SC-002, SC-004 (as amended by plan
// conflict C-1/C-2, including the FR-016 depth-1 clause), SC-005, SC-007,
// SC-011(a), SC-011(c) (the 0.1 dB / L_f 1-cent continuity clause, C-8),
// SC-019, FR-006 (L_c, L_f) and FR-010. The C++ tests remain the pass/fail authority; this script only proves
// the constants are feasible before code is written.
// =============================================================================
'use strict';

const P0 = 4 * Math.pow(10, -1.2); // FR-005: 2 * (10^(-12/20) / sqrt(1/2))^2

const K = {
  depthTriangle: 0.5,       // p(kDepthTriangle) = 2
  sineResidual: 1e-3,       // R(1): n>=2 residual gain at depth 1
  bodyCentreOct: 1.75,      // log2 n of the Body centre at shift 0 (n ~ 3.4)
  edgeCentreOct: 3.8,       // log2 n of the Edge shelf corner at shift 0 (n ~ 13.9)
  shiftOct: 0.75,           // centre travel per unit shift (octaves of n)
  shiftGainOct: 3.0,        // envelope gain compensation: 2^(shiftGainOct * shift)
  bodyWidthBroad: 0.45,     // sigma_B (octaves) at curvature 0
  bodyWidthNarrow: 0.15,    // sigma_B (octaves) at curvature 1
  bodyWidthRef: 0.6,        // peakedness reference width
  bodyPeakExp: 1.5,         // peakedness gain (ref / sigma_B)^exp
  edgeWidth: 0.35,          // lower-flank sigma of the Edge half-Gaussian shelf
  bodyGain: 18,             // linear Body gain at body = 1 (before compensation)
  edgeGain: 50,             // linear Edge gain at edge = 1 (before compensation)
  guardOnsetHz: 32.70,      // kLowNoteGuardOnsetHz (C1)
  guardSlope: 0.5,          // extra n^-0.5 per octave below the onset
  capFraction: 0.8,         // kNyquistCapFraction
  capTaperCents: 1430,      // total cap taper width below capHz (cents), C-8
  capTailCents: 10,         // final linear-amplitude segment to exact 0 at capHz (cents)
  capTaperDbPerCent: 0.09,  // dB-linear slope of the taper above the tail (SC-011(c) bar 0.1)
};

const smoothstep = (x) => (x <= 0 ? 0 : x >= 1 ? 1 : x * x * (3 - 2 * x));

function shape(c, N = 64) {
  const p = 1 + 2 * c.depth;
  const R = 1 - (1 - K.sineResidual) * smoothstep((c.depth - K.depthTriangle) / (1 - K.depthTriangle));
  const b = c.body * c.body;
  const uB = K.bodyCentreOct + K.shiftOct * c.shift;
  const uE = K.edgeCentreOct + K.shiftOct * c.shift;
  const sB = K.bodyWidthBroad + (K.bodyWidthNarrow - K.bodyWidthBroad) * c.bodyCurvature;
  const comp = Math.pow(2, K.shiftGainOct * c.shift);
  const peak = Math.pow(K.bodyWidthRef / sB, K.bodyPeakExp);
  const g = new Float64Array(N);
  g[0] = 1;
  for (let n = 2; n <= N; ++n) {
    const u = Math.log2(n);
    const GB = Math.exp(-((u - uB) ** 2) / (2 * sB * sB));
    const GE = u >= uE ? 1 : Math.exp(-((u - uE) ** 2) / (2 * K.edgeWidth * K.edgeWidth));
    const env = 1 + comp * (K.bodyGain * b * peak * GB + K.edgeGain * c.edge * GE);
    const par = n % 2 === 0 ? 1 + c.bodyEmphasis : 1;
    g[n - 1] = R * Math.pow(n, -p) * env * par;
  }
  return normalise(g);
}

function normalise(g) {
  let s = 0;
  for (const v of g) s += v * v;
  const k = Math.sqrt(P0 / s);
  return g.map((v) => v * k);
}

// y = cents below capHz. dB-linear (K.capTaperDbPerCent) from 0 dB at y = capTaperCents
// down to -capTaperDbPerCent*(capTaperCents - capTailCents) dB at y = capTailCents, then
// linear in amplitude to exactly 0 at y = 0 (C-8). Continuous at both joins.
function capFactor(y) {
  if (y >= K.capTaperCents) return 1;
  if (y <= 0) return 0;
  const floor = Math.pow(10, -K.capTaperDbPerCent * (K.capTaperCents - K.capTailCents) / 20);
  if (y <= K.capTailCents) return floor * (y / K.capTailCents);
  return Math.pow(10, -K.capTaperDbPerCent * (K.capTaperCents - y) / 20);
}

function mask(f0, fs, N) {
  const cap = K.capFraction * fs / 2;
  const octBelow = Math.max(0, Math.log2(K.guardOnsetHz / f0));
  const m = new Float64Array(N);
  for (let n = 1; n <= N; ++n) {
    let v = Math.pow(n, -K.guardSlope * octBelow);
    if (n >= 2) {
      v *= capFactor(1200 * Math.log2(cap / (n * f0)));
    }
    m[n - 1] = v;
  }
  return m;
}

const full = (c, f0, fs, N = 64) => {
  const s = shape(c, N);
  const m = mask(f0, fs, N);
  return normalise(s.map((v, i) => v * m[i]));
};

function desc(a) {
  const w = Array.from(a, (v) => v * v);
  const tot = w.reduce((x, y) => x + y, 0);
  let body = 0, pres = 0, odd = 0, even = 0, cs = 0;
  w.forEach((v, i) => {
    const n = i + 1;
    if (n >= 2 && n <= 8) body += v;
    if (n >= 9) pres += v;
    if (n >= 3 && n % 2) odd += v;
    if (n % 2 === 0) even += v;
    cs += Math.log2(n) * v;
  });
  const C = cs / tot;
  let vs = 0;
  w.forEach((v, i) => { vs += (Math.log2(i + 1) - C) ** 2 * v; });
  const db = (x) => 10 * Math.log10(x);
  const oe = db(odd / even);
  return { sub: w[0], body, pres, Rbody: db(body / tot), Rpres: db(pres / tot), C,
    sigma: Math.sqrt(vs / tot), h1rest: db(w[0] / (tot - w[0])), oe, oeClip: Math.max(-30, Math.min(30, oe)) };
}

const dist = (x, y, colour) => Math.sqrt((x.Rbody - y.Rbody) ** 2 + (x.Rpres - y.Rpres) ** 2 +
  (6 * (x.C - y.C)) ** 2 + (6 * (x.sigma - y.sigma)) ** 2 + (colour ? (x.oeClip - y.oeClip) ** 2 : 0));

const C = (depth, body, bodyCurvature, bodyEmphasis, edge, shift) => ({ depth, body, bodyCurvature, bodyEmphasis, edge, shift });
const NAMED = {
  kSineAnchor: C(1, 0, 0.5, 0, 0, 0), kTriangleAnchor: C(0.5, 0, 0.5, -1, 0, 0), kSawAnchor: C(0, 0, 0.5, 0, 0, 0),
  kHeavy: C(0.5, 0.3, 0.5, 0, 0, -1), kHollow: C(0.5, 1.0, 1.0, 0, 0, 0.5), kGrowl: C(0.0, 0.5, 0.0, 0, 1.0, 1.0),
};
const COLOURS = {
  kBodyRound: C(0.2, 0.0, 0.1, -0.8, 0.1, -0.4), kBodyHollow: C(0.0, 0.25, 0.6, -1.0, 0.9, 0.85),
  kBodyWoody: C(0.65, 0.5, 0.95, -0.1, 0.3, 0.05), kBodyNasal: C(0.6, 0.7, 0.95, -0.2, 0.3, -0.95),
  kBodyThick: C(0.5, 0.9, 0.05, 0.0, 0.0, -0.05),
};

if (require.main === module) {
  const sweep = (lo, hi) => Array.from({ length: 33 }, (_, i) => lo + ((hi - lo) * i) / 32);
  const mid = C(0.5, 0.5, 0.5, 0, 0.5, 0);
  const G3 = [0, 0.5, 1], GS = [-1, 0, 1];
  const strict = (v) => v.every((x, i) => i === 0 || x > v[i - 1]);
  const run = (field, lo, hi, key, st) => sweep(lo, hi).map((x) => desc(shape({ ...st, [field]: x }))[key]);
  const out = [];
  let ok = true;
  const rec = (name, pass, detail) => { out.push(`${pass ? 'PASS' : 'FAIL'}  ${name}  ${detail}`); ok = ok && pass; };

  { let m = true; for (const b of G3) for (const e of G3) for (const s of GS) {
      const xs = [...sweep(0, 1), 0.5].sort((x, y) => x - y);
      if (!strict(xs.map((d) => desc(shape({ ...mid, depth: d, body: b, edge: e, shift: s })).h1rest).filter((v, i, a) => i === 0 || xs[i] !== xs[i - 1]))) m = false; }
    rec('SC-002 depth strictly monotone in h1/rest (27 grid points)', m, ''); }
  let mBody = true, spanBody = 99, nBody6 = 0, ratioLow = 99, nLow = 0, mEdge = true, spanEdge = 99, mShift = true, spanShift = 99;
  for (const a of G3) for (const b of G3) for (const s of GS) {
    let v = run('body', 0, 1, 'Rbody', { ...mid, depth: a, edge: b, shift: s });
    mBody = mBody && strict(v);
    // C-1: headroom = -R_body(body = 0). >= 6 dB -> the spec's 6 dB bar; < 6 dB -> span >= 0.7 x headroom.
    const headroom = -v[0];
    if (headroom >= 6) { spanBody = Math.min(spanBody, v[32] - v[0]); ++nBody6; }
    else { ratioLow = Math.min(ratioLow, (v[32] - v[0]) / headroom); ++nLow; }
    v = run('edge', 0, 1, 'Rpres', { ...mid, depth: a, body: b, shift: s });
    mEdge = mEdge && strict(v); spanEdge = Math.min(spanEdge, v[32] - v[0]);
  }
  for (const a of [0, 0.5]) for (const b of G3) for (const e of G3) { if (b + e === 0) continue;
    const v = run('shift', -1, 1, 'C', { ...mid, depth: a, body: b, edge: e });
    mShift = mShift && strict(v); spanShift = Math.min(spanShift, v[32] - v[0]); }
  rec('SC-004 Body strictly monotone (27 points)', mBody, '');
  rec(`SC-004 Body span >= 6 dB where headroom >= 6 dB (C-1, ${nBody6} points)`, nBody6 === 22 && spanBody >= 6, `min ${spanBody.toFixed(2)} dB`);
  rec(`SC-004 Body span >= 0.7 x headroom where headroom < 6 dB (C-1, ${nLow} points)`, nLow === 5 && ratioLow >= 0.7, `min ${ratioLow.toFixed(3)} x headroom`);
  rec('SC-004 Edge strictly monotone, span >= 6 dB (27 points)', mEdge && spanEdge >= 6, `min ${spanEdge.toFixed(2)} dB`);
  rec('SC-004 Shift strictly monotone, span >= 1 oct (depth<1, body+edge>0; C-2)', mShift && spanShift >= 1, `min ${spanShift.toFixed(3)} oct`);
  { // C-2 / FR-016 amendment at depth 1: strictly increasing when body+edge > 0 (vectors rounded to float,
    // as the C++ recipe produces them), constant at body = edge = 0.
    let st = true, cst = true, minRel = 1e9;
    for (const b of G3) for (const e of G3) {
      const v = sweep(-1, 1).map((x) => desc(shape({ ...mid, depth: 1, body: b, edge: e, shift: x }).map(Math.fround)).C);
      if (b + e === 0) { cst = cst && v.every((x) => Math.abs(x - v[0]) <= 1e-6 * Math.abs(v[0])); continue; }
      st = st && strict(v);
      for (let i = 1; i < v.length; ++i) minRel = Math.min(minRel, (v[i] - v[i - 1]) / v[i - 1]);
    }
    rec('FR-016/C-2 depth 1: C strictly increasing (8 points, float vectors), constant at body=edge=0', st && cst, `min per-step dC/C ${minRel.toExponential(2)}`);
  }
  const bm = desc(shape({ ...mid, body: 1 })), em = desc(shape({ ...mid, edge: 1 })), sm = desc(shape({ ...mid, shift: 1 }));
  const ep = Math.min(dist(bm, em), dist(bm, sm), dist(em, sm));
  rec('SC-004 endpoint pairwise distance >= 6 dB', ep >= 6, `min ${ep.toFixed(2)} dB`);
  const cv = sweep(0, 1).map((x) => desc(shape({ ...mid, bodyCurvature: x })).sigma);
  const cDir = Math.sign(cv[32] - cv[0]);
  rec('SC-005 curvature: sigma strictly monotone, span >= 0.25 oct', cv.every((x, i) => i === 0 || Math.sign(x - cv[i - 1]) === cDir) && Math.abs(cv[32] - cv[0]) >= 0.25,
    `span ${(cv[32] - cv[0]).toFixed(3)} oct`);
  const ev = sweep(-1, 0).map((x) => desc(shape({ ...mid, bodyEmphasis: x })).oe);
  rec('SC-005 emphasis: odd/even strictly decreasing, span >= 20 dB', ev.every((x, i) => i === 0 || x < ev[i - 1]) && ev[1] - ev[32] >= 20,
    `${ev[1].toFixed(1)} (2nd point) -> ${ev[32].toFixed(1)} dB; first point +inf`);
  let worst = -1e9;
  for (const b of G3) for (const e of G3) for (const s of GS) for (const cu of G3)
    worst = Math.max(worst, -desc(shape(C(1, b, cu, 0, e, s))).h1rest);
  rec('FR-010 depth 1: rest <= -30 dB re h1 at any B/E/S', worst <= -30, `worst ${worst.toFixed(1)} dB`);
  rec('FR-018 depth 1, B=E=0: rest <= -60 dB', -desc(shape(NAMED.kSineAnchor)).h1rest <= -60, `${(-desc(shape(NAMED.kSineAnchor)).h1rest).toFixed(1)} dB`);
  const db = (x) => 10 * Math.log10(x);
  for (const [nm, ord] of [['kHeavy', ['sub', 'body', 'pres']], ['kHollow', ['body', 'sub', 'pres']], ['kGrowl', ['pres', 'body', 'sub']]]) {
    for (const f0 of [32.70, 65.41, 130.81]) {
      const d = desc(full(NAMED[nm], f0, 48000));
      const m = Math.min(db(d[ord[0]] / d[ord[1]]), db(d[ord[1]] / d[ord[2]]));
      rec(`SC-007 ${nm} ordering, margin >= 3 dB @${f0} Hz`, m >= 3, `min margin ${m.toFixed(1)} dB`);
    }
  }
  const D = Object.fromEntries(Object.entries(COLOURS).map(([k, c]) => [k, desc(shape(c))]));
  const ks = Object.keys(D);
  let mn = 1e9; for (let i = 0; i < 5; ++i) for (let j = i + 1; j < 5; ++j) mn = Math.min(mn, dist(D[ks[i]], D[ks[j]], true));
  rec('SC-019 pairwise Body-colour distance >= 6 dB', mn >= 6, `min ${mn.toFixed(2)} dB`);
  const oth = (k, f) => ks.filter((x) => x !== k).map((x) => f(D[x]));
  const rM = Math.min(...oth('kBodyRound', (d) => d.C)) - D.kBodyRound.C;
  const hM = D.kBodyHollow.oeClip - Math.max(...oth('kBodyHollow', (d) => d.oeClip));
  const nM = Math.min(...oth('kBodyNasal', (d) => d.sigma)) - D.kBodyNasal.sigma;
  const tM = D.kBodyThick.Rbody - Math.max(...oth('kBodyThick', (d) => d.Rbody));
  rec('SC-019 extremal orderings (>= 0.05 oct / >= 1 dB)', rM >= 0.05 && hM >= 1 && nM >= 0.05 && tM >= 1,
    `round C ${rM.toFixed(3)} oct, hollow o/e ${hM.toFixed(2)} dB, nasal sigma ${nM.toFixed(3)} oct, thick Rbody ${tM.toFixed(2)} dB`);
  for (const [nm, c] of [['kSawAnchor', NAMED.kSawAnchor], ['mid grid', mid]]) {
    const a = desc(full(c, 32.70, 48000)), b = desc(full(c, 16.35, 48000));
    rec(`SC-011(a) guard reduces R_pres >= 6 dB one octave below onset (${nm})`, a.Rpres - b.Rpres >= 6, `${(a.Rpres - b.Rpres).toFixed(2)} dB`);
  }
  // FR-006 Lipschitz ceilings (L_c over controls, L_f over 1-cent f0 steps incl. guard onset and cap taper)
  let s = 11; const rnd = () => { s = (s * 16807) % 2147483647; return s / 2147483647; };
  const F = [['depth', 0, 1], ['body', 0, 1], ['bodyCurvature', 0, 1], ['bodyEmphasis', -1, 0], ['edge', 0, 1], ['shift', -1, 1]];
  let Lc = 0, Lf = 0;
  for (let t = 0; t < 60000; ++t) {
    const c = {}; for (const [f, lo, hi] of F) c[f] = lo + rnd() * (hi - lo);
    const [f, , hi] = F[t % 6]; const c2 = { ...c, [f]: Math.min(hi, c[f] + 0.002) }; const dd = c2[f] - c[f]; if (dd <= 0) continue;
    const a = shape(c, 96), b = shape(c2, 96);
    for (let i = 0; i < 96; ++i) Lc = Math.max(Lc, Math.abs(a[i] - b[i]) / dd / Math.sqrt(P0));
  }
  for (const fs of [44100, 48000, 96000]) for (let t = 0; t < 400; ++t) {
    const c = {}; for (const [f, lo, hi] of F) c[f] = lo + rnd() * (hi - lo);
    const ceil = K.capFraction * fs / 2; const f0 = Math.min(ceil, 8 * Math.pow(ceil / 8, rnd())); const f1 = Math.min(ceil, f0 * Math.pow(2, 1 / 1200));
    const oct = Math.log2(f1 / f0); if (oct <= 0) continue;
    const a = full(c, f0, fs, 96), b = full(c, f1, fs, 96);
    for (let i = 0; i < 96; ++i) Lf = Math.max(Lf, Math.abs(a[i] - b[i]) / oct / Math.sqrt(P0));
  }
  { // SC-011(c): MIDI 0-127 in 1-cent steps at 3 rates, N = 96, over the named, colour, mid-grid and
    // max-level (L_max) states. <= 0.1 dB where both >= -80 dB re h1, else |da| <= 8 sqrt(P0)/1200.
    const states = [...Object.values(NAMED), ...Object.values(COLOURS), mid, C(0, 1, 1, 0, 1, 1)];
    let wDb = 0, wAbs = 0, LfSweep = 0, lMax = -1e9;
    for (const c of states) {
      const sh = shape(c, 96);
      for (let i = 1; i < 96; ++i) lMax = Math.max(lMax, 20 * Math.log10(sh[i] / sh[0]));
      for (const fs of [44100, 48000, 96000]) {
        const ceil = K.capFraction * fs / 2;
        let prev = null, pf = 0;
        for (let ct = 0; ct <= 12700; ++ct) {
          const f0 = Math.min(ceil, 8.175798915643707 * Math.pow(2, ct / 1200));
          const m = mask(f0, fs, 96);
          const v = normalise(sh.map((x, i) => x * m[i]));
          if (prev && f0 > pf) {
            const oct = Math.log2(f0 / pf);
            for (let i = 0; i < 96; ++i) {
              const d = Math.abs(v[i] - prev[i]);
              LfSweep = Math.max(LfSweep, d / oct / Math.sqrt(P0));
              const la = v[i] > 0 ? 20 * Math.log10(v[i] / v[0]) : -1e9, lb = prev[i] > 0 ? 20 * Math.log10(prev[i] / prev[0]) : -1e9;
              if (la >= -80 && lb >= -80) wDb = Math.max(wDb, Math.abs(20 * Math.log10(v[i] / prev[i])));
              else wAbs = Math.max(wAbs, d / (8 * Math.sqrt(P0) / 1200));
            }
          }
          prev = v; pf = f0;
        }
      }
    }
    rec('SC-011(c) 1-cent continuity: <= 0.1 dB where both >= -80 dB re h1 (14 states, N=96, 3 rates)', wDb <= 0.1, `worst ${wDb.toFixed(4)} dB (max pre-mask level re h1 ${lMax.toFixed(2)} dB)`);
    rec('SC-011(c) 1-cent continuity: |da| <= L_f/1200 elsewhere', wAbs <= 1, `worst ${wAbs.toFixed(3)} x bound`);
    rec('FR-006 L_f <= 8 sqrt(P0)/oct over the SC-011(c) sweep', LfSweep <= 8, `measured ${LfSweep.toFixed(2)} sqrt(P0)/oct`);
  }
  rec('FR-006 L_c <= 8 sqrt(P0) (60k samples, N=96)', Lc <= 8, `measured ${Lc.toFixed(2)} sqrt(P0)`);
  rec('FR-006 L_f <= 8 sqrt(P0)/oct (1-cent steps, 8 Hz..clamp, 3 rates)', Lf <= 8, `measured ${Lf.toFixed(2)} sqrt(P0)/oct`);
  console.log(out.join('\n'));
  console.log(ok ? '\nALL RECIPE-SIDE GATES PASS' : '\nRECIPE-SIDE GATE FAILURE');
  process.exitCode = ok ? 0 : 1;
}

module.exports = { P0, K, shape, capFactor, mask, full, desc, dist, NAMED, COLOURS };
