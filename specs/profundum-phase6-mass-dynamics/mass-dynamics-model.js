#!/usr/bin/env node
// Profundum Phase 6 plan model (specs/profundum-phase6-mass-dynamics/plan.md S10).
// Offline, double precision, NOT evidence for compliance: it sizes the design before any header
// exists. Every figure it prints is quoted in plan.md S10 with the command that produced it.
//
//   node specs/profundum-phase6-mass-dynamics/mass-dynamics-model.js [kweight|shaper|comp|all]
//
// Models (each a faithful port of the reused header's arithmetic, cited in plan.md S0):
//   - EnvelopeFollower coefficient exp(-2pi / (T_ms * 1e-3 * fs)) (envelope_follower.h:359-365)
//   - DynamicsProcessor GR computer + 5 ms OnePoleSmoother exp(-5000 / (5 * fs))
//     (dynamics_processor.h:182-196, 367-398; smoother.h:77-93)
//   - Biquad RBJ high-pass, Q = 1/sqrt(2) (biquad.h kButterworthQ)
//   - BS.1770-4 K-weighting (48 kHz coefficients from the Recommendation)
//   - HarmonicFold's segmented ring-max hold (harmonic_fold.h:402-427), without the 4x true-peak
//     refinement (it changes ripple, not onset timing; the ripple evidence is HarmonicFold's D-4).
'use strict';

const TWO_PI = 2 * Math.PI;
const noteHz = (m) => 440 * Math.pow(2, (m - 69) / 12);
const E0 = noteHz(16), E1 = noteHz(28), E2 = noteHz(40), C3 = noteHz(48);
const NOTES = [['E0', E0], ['E1', E1], ['E2', E2], ['C3', C3]];
const CAL_RMS = Math.pow(10, -12 / 20);   // the core's -12 dBFS RMS calibration level

// ---------------------------------------------------------------------------------------------
// Signals
// ---------------------------------------------------------------------------------------------
function tone(kind, f0, fs, seconds, { onsetMs = 0.5, rms = CAL_RMS } = {}) {
  const n = Math.round(seconds * fs);
  const x = new Float64Array(n);
  const P = kind === 'sine' ? 1 : Math.min(64, Math.floor((0.45 * fs) / f0));   // kSawAnchor: 1/n, 64 partials
  let pw = 0;
  for (let k = 1; k <= P; ++k) pw += 0.5 / (k * k);
  const g = rms / Math.sqrt(kind === 'sine' ? 0.5 : pw);
  const on = Math.max(1, Math.round((onsetMs * 1e-3) * fs));
  for (let i = 0; i < n; ++i) {
    let s = 0;
    const ph = (TWO_PI * f0 * i) / fs;
    if (kind === 'sine') s = Math.sin(ph);
    else for (let k = 1; k <= P; ++k) s += Math.sin(k * ph) / k;
    const env = i < on ? 0.5 - 0.5 * Math.cos((Math.PI * i) / on) : 1;
    x[i] = g * s * env;
  }
  return x;
}

// ---------------------------------------------------------------------------------------------
// Building blocks
// ---------------------------------------------------------------------------------------------
const efCoeff = (ms, fs) => Math.exp(-TWO_PI / (ms * 1e-3 * fs));            // envelope_follower.h:359-365
const dB = (v) => (v > 0 ? Math.max(20 * Math.log10(v), -144) : -144);       // gainToDb, db_utils.h:313-320

class RingMaxHold {            // harmonic_fold.h:402-427 structure, settable hold
  constructor(holdSamples, segs = 8) { this.segs = segs; this.ring = new Float64Array(segs); this.setHold(holdSamples); this.reset(); }
  setHold(h) { this.segLen = Math.max(1, Math.ceil(h / this.segs)); }
  reset() { this.ring.fill(0); this.ringMax = 0; this.segMax = 0; this.segCount = 0; this.pos = 0; }
  push(d) {
    this.segMax = Math.max(this.segMax, d);
    if (++this.segCount >= this.segLen) {
      this.ring[this.pos] = this.segMax; this.pos = (this.pos + 1) % this.segs;
      this.segMax = 0; this.segCount = 0; this.ringMax = 0;
      for (const r of this.ring) this.ringMax = Math.max(this.ringMax, r);
    }
    return Math.max(this.segMax, this.ringMax);
  }
}

class Biquad {                 // RBJ high-pass, TDF2
  constructor(type, f, q, fs) { this.z1 = 0; this.z2 = 0; this.set(type, f, q, fs); }
  set(type, f, q, fs) {
    const w = (TWO_PI * f) / fs, c = Math.cos(w), al = Math.sin(w) / (2 * q), a0 = 1 + al;
    if (type === 'hp') { this.b0 = (1 + c) / 2 / a0; this.b1 = -(1 + c) / a0; this.b2 = this.b0; }
    else { this.b0 = (1 - c) / 2 / a0; this.b1 = (1 - c) / a0; this.b2 = this.b0; }
    this.a1 = (-2 * c) / a0; this.a2 = (1 - al) / a0;
  }
  p(x) { const y = this.b0 * x + this.z1; this.z1 = this.b1 * x - this.a1 * y + this.z2; this.z2 = this.b2 * x - this.a2 * y; return y; }
}

// ---------------------------------------------------------------------------------------------
// (1) K-weighting offsets (SC-003 feasibility; FR-050 / SC-026 reference)
// ---------------------------------------------------------------------------------------------
function kWeightOffsetDb(x, fs) {
  // BS.1770-4 coefficients are specified at 48 kHz; the model runs at 48 kHz only.
  const s1 = { b: [1.53512485958697, -2.69169618940638, 1.19839281085285], a: [-1.69065929318241, 0.73248077421585] };
  const s2 = { b: [1.0, -2.0, 1.0], a: [-1.99004745483398, 0.99007225036621] };
  const run = (c, v) => { const y = new Float64Array(v.length); let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (let i = 0; i < v.length; ++i) { const o = c.b[0] * v[i] + c.b[1] * x1 + c.b[2] * x2 - c.a[0] * y1 - c.a[1] * y2; x2 = x1; x1 = v[i]; y2 = y1; y1 = o; y[i] = o; } return y; };
  const k = run(s2, run(s1, x));
  const from = Math.floor(x.length / 2);
  let a = 0, b = 0;
  for (let i = from; i < x.length; ++i) { a += k[i] * k[i]; b += x[i] * x[i]; }
  return { offset: 10 * Math.log10(a / b), lkfs: -0.691 + 10 * Math.log10(a / (x.length - from)) };
}

function runKWeight() {
  const fs = 48000;
  console.log('K-weighting: K-weighted mean square relative to unweighted, steady tone (dB)');
  const ref = new Float64Array(fs * 2); for (let i = 0; i < ref.length; ++i) ref[i] = Math.sin((TWO_PI * 997 * i) / fs);
  console.log(`  997 Hz full-scale sine: ${kWeightOffsetDb(ref, fs).lkfs.toFixed(3)} LKFS (BS.1770: -3.01)`);
  for (const [name, f0] of NOTES) {
    const s = kWeightOffsetDb(tone('sine', f0, fs, 4), fs).offset;
    const w = kWeightOffsetDb(tone('saw', f0, fs, 4), fs).offset;
    console.log(`  ${name} (${f0.toFixed(2)} Hz): sine ${s.toFixed(2)}  saw ${w.toFixed(2)}  saw-sine ${(w - s).toFixed(2)}`);
  }
}

// ---------------------------------------------------------------------------------------------
// (2) TransientShaper onset model (SC-008(b)(c) feasibility; FR-008 duration table seed)
// ---------------------------------------------------------------------------------------------
const SHAPER = { holdMs: 50, fastAttackMs: 0.5, fastReleaseMs: 40, slowReleaseRealMs: 60, tick: 8, K: 12 };

function shaperGainTrace(x, fs, slowAttackArgMs, attackDb, sustainDb) {
  const hold = new RingMaxHold(Math.round((SHAPER.holdMs * 1e-3) * fs));
  const fa = efCoeff(SHAPER.fastAttackMs, fs), fr = efCoeff(SHAPER.fastReleaseMs, fs);
  // slow follower ticked once per SHAPER.tick samples: its argument is real time / tick (plan S3.3)
  const sa = efCoeff(slowAttackArgMs, fs), sr = efCoeff(SHAPER.slowReleaseRealMs / SHAPER.tick, fs);
  let fast = 0, slow = 0, slowPrev = 0, m = 0;
  const g = new Float64Array(x.length);
  for (let i = 0; i < x.length; ++i) {
    m = hold.push(Math.abs(x[i]));
    fast = m > fast ? m + fa * (fast - m) : m + fr * (fast - m);
    if (i % SHAPER.tick === 0) { slowPrev = slow; slow = m > slow ? m + sa * (slow - m) : m + sr * (slow - m); }
    const ph = (i % SHAPER.tick) / SHAPER.tick;
    const sI = slowPrev + (slow - slowPrev) * ph;                     // linear interpolation across the tick
    const c = Math.min(1, Math.max(0, (dB(fast) - dB(sI)) / SHAPER.K));
    g[i] = attackDb * c + sustainDb * (1 - c);
  }
  return g;
}

function transientTimeMs(g, fs) {
  let peak = -Infinity, ip = 0;
  const lim = Math.min(g.length, Math.round(0.1 * fs));
  for (let i = 0; i < lim; ++i) if (g[i] > peak) { peak = g[i]; ip = i; }
  for (let i = ip; i < g.length; ++i) if (g[i] <= peak - 3) return { t: (1e3 * i) / fs, peak };
  return { t: Infinity, peak };
}

function runShaper() {
  const fs = 48000;
  const cells = [['sine', E1], ['saw', E1], ['sine', C3], ['saw', C3]];
  const sig = cells.map(([k, f]) => tone(k, f, fs, 3));
  const tt = (arg) => sig.map((x) => transientTimeMs(shaperGainTrace(x, fs, arg, 12, 0), fs));
  console.log('TransientShaper: attack +12 dB, sustain 0 dB; transient time = onset to 3 dB below the onset gain peak');
  console.log(`  design: hold ${SHAPER.holdMs} ms (8 segments), fast EF ${SHAPER.fastAttackMs}/${SHAPER.fastReleaseMs} ms,`
    + ` slow EF ticked /${SHAPER.tick}, slow release ${SHAPER.slowReleaseRealMs} ms real, K = ${SHAPER.K} dB`);
  const argForStep = (d) => (d / 0.4382) * TWO_PI / SHAPER.tick;      // step-onset closed form (plan S3.3)
  for (const d of [5, 10, 20, 40, 100, 250]) {
    // one table entry per duration: bisect the argument so the MEAN log error over the 4 cells is 0
    let lo = 0.1, hi = 500;
    for (let it = 0; it < 40; ++it) {
      const mid = Math.sqrt(lo * hi);
      const r = tt(mid);
      const meanLog = r.reduce((s, v) => s + Math.log(v.t / d), 0) / r.length;
      if (meanLog > 0) hi = mid; else lo = mid;
    }
    const arg = Math.sqrt(lo * hi), r = tt(arg);
    const errs = r.map((v) => (100 * (v.t - d)) / d);
    console.log(`  d=${String(d).padStart(3)} ms: arg ${arg.toFixed(3)} ms (step closed form ${argForStep(d).toFixed(3)})`
      + `  t3dB [sine E1, saw E1, sine C3, saw C3] = ${r.map((v) => v.t.toFixed(2)).join(', ')} ms`
      + `  err % = ${errs.map((e) => e.toFixed(1)).join(', ')}  peak dB = ${r.map((v) => v.peak.toFixed(2)).join(', ')}`);
  }
}

// ---------------------------------------------------------------------------------------------
// (3) Bass compressor: program-dependent release (SC-007) and the hold's effect
// ---------------------------------------------------------------------------------------------
function compRun(x, fs, f0, { useHold, fixedReleaseMs = null, thrDb = -24, ratio = 4, kneeDb = 6, attackMs = 10 }) {
  const corner = Math.min(500, Math.max(20, 2 * f0));
  const hp = new Biquad('hp', corner, Math.SQRT1_2, fs);
  const hold = new RingMaxHold(Math.round((2 / corner) * fs));
  const smC = Math.exp(-5000 / (5 * fs));
  const interval = 32, nInt = Math.round((0.3 * fs) / interval);
  const ring = new Int32Array(nInt); let ringPos = 0, ringSum = 0, cnt = 0;
  let relMs = fixedReleaseMs ?? 50, env = 0, gr = 0;
  const out = new Float64Array(x.length);
  const ks = thrDb - kneeDb / 2, ke = thrDb + kneeDb / 2, slope = 1 - 1 / ratio;
  for (let i = 0; i < x.length; ++i) {
    const d = Math.abs(hp.p(x[i]));
    const m = useHold ? hold.push(d) : d;
    const a = efCoeff(attackMs, fs), r = efCoeff(relMs, fs);
    env = m > env ? m + a * (env - m) : m + r * (env - m);          // EF Peak mode, attack > min
    const L = dB(env);
    let t = 0;
    if (L > ke) t = (L - thrDb) * slope; else if (L >= ks) { const u = L - ks; t = (slope * u * u) / (2 * kneeDb); }
    gr = Math.abs(gr - t) < 1e-4 ? t : t + smC * (gr - t);          // OnePoleSmoother 5 ms
    out[i] = gr;
    if (gr > 1) ++cnt;
    if ((i + 1) % interval === 0) {                                 // FR-049 interval: the D-5 rule
      ringSum += cnt - ring[ringPos]; ring[ringPos] = cnt; ringPos = (ringPos + 1) % nInt; cnt = 0;
      if (fixedReleaseMs === null) relMs = 50 + 450 * (ringSum / (nInt * interval));
    }
  }
  return out;
}

function recoveryMs(gr, fs, from, to) {
  let peak = 0, ip = from;
  for (let i = from; i < to; ++i) if (gr[i] > peak) peak = gr[i];
  // a plateau's recovery starts at its LAST sample within 0.1 dB of the peak (plan S9, SC-007)
  for (let i = from; i < to; ++i) if (gr[i] >= peak - 0.1) ip = i;
  for (let i = ip; i < gr.length; ++i) if (gr[i] <= peak / 2) return { t: (1e3 * (i - ip)) / fs, peak };
  return { t: Infinity, peak };
}

function runComp() {
  const fs = 48000;
  console.log('Bass compressor SC-007: base at threshold, +12 dB events of 20 ms and 1 s (kSawAnchor), amount-1 seed');
  for (const [name, f0] of [['E1', E1], ['C3', C3]]) {
    // the detector reading of the steady tone sets the base so it sits AT the threshold
    const probe = tone('saw', f0, fs, 1, { rms: 1 });
    const hp = new Biquad('hp', Math.min(500, Math.max(20, 2 * f0)), Math.SQRT1_2, fs);
    let pk = 0; for (let i = fs / 2; i < probe.length; ++i) pk = Math.max(pk, Math.abs(hp.p(probe[i])));
    const thrDb = -24, baseGain = Math.pow(10, thrDb / 20) / pk;
    const mk = (evMs) => {
      const sec = 4, x = tone('saw', f0, fs, sec, { rms: baseGain });
      const s = Math.round(2 * fs), e = s + Math.round((evMs * 1e-3) * fs);
      for (let i = s; i < e; ++i) x[i] *= 4;                          // +12 dB
      return { x, s, e };
    };
    for (const useHold of [false, true]) {
      for (const fixed of [null, 275]) {
        const res = [20, 1000].map((ev) => { const { x, s, e } = mk(ev); const g = compRun(x, fs, f0, { useHold, fixedReleaseMs: fixed, thrDb });
          return recoveryMs(g, fs, s, e + Math.round(0.2 * fs)); });
        console.log(`  ${name} hold=${useHold ? '1 period' : 'none   '} release=${fixed === null ? 'D-5 rule' : 'fixed 275'}:`
          + ` short ${res[0].t.toFixed(1)} ms (peak GR ${res[0].peak.toFixed(2)} dB), long ${res[1].t.toFixed(1)} ms`
          + ` (peak ${res[1].peak.toFixed(2)} dB), ratio ${(res[0].t / res[1].t).toFixed(3)} (bar <= 0.5)`);
      }
    }
  }
  // steady ripple of the GR with and without the hold (SC-006(a) feasibility; plain |x|, no true peak)
  console.log('Bass compressor steady GR ripple, kSawAnchor at -12 dBFS RMS, amount-1 seed (peak-to-peak dB, window 2-3 s)');
  for (const [name, f0] of NOTES) {
    const x = tone('saw', f0, fs, 3);
    const row = [false, true].map((useHold) => { const g = compRun(x, fs, f0, { useHold });
      let lo = Infinity, hi = -Infinity; for (let i = 2 * fs; i < 3 * fs; ++i) { lo = Math.min(lo, g[i]); hi = Math.max(hi, g[i]); }
      return `${useHold ? 'hold' : 'none'} ${(hi - lo).toFixed(3)} (mean GR ${((hi + lo) / 2).toFixed(2)})`; });
    console.log(`  ${name}: ${row.join('   ')}`);
  }
}

const what = process.argv[2] || 'all';
if (what === 'kweight' || what === 'all') runKWeight();
if (what === 'shaper' || what === 'all') runShaper();
if (what === 'comp' || what === 'all') runComp();
