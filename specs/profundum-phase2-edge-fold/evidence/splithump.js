// Phase 2 close (user request 2026-10-11): characterise the excitation's +1.5 dB hump at 400-700 Hz and attribute it.
//   node specs/profundum-phase2-edge-fold/evidence/splithump.js            (~20 s)
// Three measurements on evidence/lib.js's ExcStage (the plan's model of profundum_core.h exciteRun):
//   1. LINEAR transfer of the stage with the waveshaper replaced by identity (y = v): split + recombination only,
//      per sample rate and mix, as a sine sweep at the operating level. Attribution: this is the hump's origin if it
//      reproduces the full stage's figure.
//   2. The closed form H = cm*LP*(1 - Dos) + g*Dos (LP = the 500 Hz 2nd-order split, Dos = the halfband round trip).
//   3. FULL stage per-harmonic |T(n)| on the mid grid and the saw, drive {-12, 0, +12} dB x mix {0.25, 0.5, 1},
//      at E0/E1/C2/E2, 44.1/48/96 kHz, against the linear transfer at the same harmonic: the difference is the
//      nonlinear path's own contribution (generated harmonics), not the split.
const M = require('./lib.js');
const P_CAL = M.P_CAL;
const F = 4;

// ExcStage with the waveshaper replaced by identity (y = v, fb = b): the linear part of the stage.
class LinearStage extends M.ExcStage {
  proc(x, m, D, b, Mk, c) {
    if (m === 0 && this.idle) return x; this.idle = false;
    const cm = 1 - m + m * c, w = M.smooth(m / 0.1), fb = b;
    const lo = this.lb.proc(x), base = cm * (lo + (1 - w) * (x - lo)); this.os.up(x, this.buf);
    for (let i = 0; i < this.F; i++) { const u = this.buf[i], v = D * u / P_CAL + b, y = v;
      const ul = this.lo.proc(u); this.buf[i] = m * Mk * (y - fb) + (1 - m) * u - cm * (ul + (1 - w) * (u - ul)); }
    return base + this.dc.proc(this.os.down(this.buf, this.hook));
  }
}
function goertzel(x, from, len, hz, fs) { let re = 0, im = 0; for (let k = 0; k < len; k++) { const t = 2 * Math.PI * hz * (from + k) / fs; re += x[from + k] * Math.cos(t); im -= x[from + k] * Math.sin(t); } return [re, im]; }
const mag = (c) => Math.hypot(c[0], c[1]);
const db = (r) => 20 * Math.log10(r);
function transfer(stage, hz, fs, args, amp) {
  const n = Math.round(1.0 * fs), from = Math.round(0.5 * fs), len = Math.round(Math.floor((n - from) / (fs / hz)) * fs / hz);
  const x = new Float64Array(n), y = new Float64Array(n);
  for (let k = 0; k < n; k++) { x[k] = amp * Math.sin(2 * Math.PI * hz * k / fs); y[k] = stage.proc(x[k], ...args); }
  return mag(goertzel(y, from, len, hz, fs)) / mag(goertzel(x, from, len, hz, fs));
}

const probes = [100, 150, 200, 250, 300, 350, 400, 450, 500, 550, 600, 700, 800, 1000, 1200, 1500, 2000, 3000, 5000];
console.log('1. LINEAR stage (waveshaper = identity), |T| dB at the operating level, drive 0 dB, asymmetry 0');
for (const fs of [44100, 48000, 96000]) {
  for (const m of [0.05, 0.25, 0.5, 1.0]) {
    const st = M.excStatic(1, 0);
    // The linear path's gain through Mk*D/P_CAL differs from the nonlinear operating gain c; normalise to the
    // 100 Hz reading so the table shows shape (the hump), not level.
    const vals = probes.map((hz) => { const s = new LinearStage(fs, F); s.seed(0); return transfer(s, hz, fs, [m, 1, 0, st.makeup, st.c], 0.1); });
    const ref = vals[0];
    const peak = Math.max(...vals.map((v) => db(v / ref)));
    const peakHz = probes[vals.findIndex((v) => db(v / ref) === peak)];
    console.log(`  fs ${fs} mix ${m.toFixed(2)}: ` + probes.map((hz, i) => `${hz}:${db(vals[i] / ref).toFixed(2)}`).join(' ') + `  | peak +${peak.toFixed(2)} dB @ ${peakHz} Hz`);
  }
}

console.log('\n2. Closed form H = LP*(1 - Dos) + Dos (cm = g), |H| dB and delay (samples): origin = split + recombination');
for (const fs of [44100, 48000, 96000]) {
  const line = [];
  for (const hz of probes) {
    const nn = Math.round(fs), os = new M.OSHB(F), buf = new Float64Array(F), lp = new M.Cascade([M.lpCoeffs(500, fs, Math.SQRT1_2)]);
    const xin = new Float64Array(nn), d = new Float64Array(nn), l = new Float64Array(nn);
    for (let k = 0; k < nn; k++) { xin[k] = Math.sin(2 * Math.PI * hz * k / fs); os.up(xin[k], buf); d[k] = os.down(buf, (v) => v); l[k] = lp.proc(xin[k]); }
    const from = Math.round(nn / 2), len = Math.round(Math.floor((nn - from) / (fs / hz)) * fs / hz);
    const X = goertzel(xin, from, len, hz, fs), D = goertzel(d, from, len, hz, fs), L = goertzel(l, from, len, hz, fs);
    const div = (a, b) => { const den = b[0] * b[0] + b[1] * b[1]; return [(a[0] * b[0] + a[1] * b[1]) / den, (a[1] * b[0] - a[0] * b[1]) / den]; };
    const Dos = div(D, X), LP = div(L, X), om = [1 - Dos[0], -Dos[1]];
    const H = [LP[0] * om[0] - LP[1] * om[1] + Dos[0], LP[0] * om[1] + LP[1] * om[0] + Dos[1]];
    line.push(`${hz}:${db(mag(H)).toFixed(2)}/${(-Math.atan2(H[1], H[0]) / (2 * Math.PI * hz / fs)).toFixed(2)}`);
  }
  console.log(`  fs ${fs}: ` + line.join(' '));
}

console.log('\n3. FULL stage per-harmonic |T(n)| dB (mid grid / saw) minus the LINEAR stage at the same harmonic = nonlinear contribution');
console.log('   columns: harmonic Hz: full|linear|diff. Hump band 300-900 Hz; h1 also shown.');
for (const [stName, st] of [['mid', M.STATES.mid], ['saw', M.STATES.saw]]) {
  for (const [nName, f0] of [['E0', M.NOTES.E0], ['E1', M.NOTES.E1], ['C2', M.NOTES.C2], ['E2', M.NOTES.E2]]) {
    for (const fs of [44100, 48000, 96000]) {
      const amps = M.coreAmps(st, f0, fs);
      for (const dDb of [-12, 0, 12]) for (const m of [0.25, 0.5, 1.0]) {
        const D = Math.pow(2, dDb / 6.0205999);
        const p = M.excStatic(D, 0);
        const settle = Math.round(1.0 * fs), periods = Math.floor(0.5 * fs / (fs / f0)), len = Math.round(periods * fs / f0), n = settle + len;
        const x = M.synth(amps, f0, fs, n);
        const full = new M.ExcStage(fs, F); full.seed(0); const lin = new LinearStage(fs, F); lin.seed(0);
        const yF = new Float64Array(n), yL = new Float64Array(n);
        for (let k = 0; k < n; k++) { yF[k] = full.proc(x[k], m, D, 0, p.makeup, p.c); yL[k] = lin.proc(x[k], m, D, 0, p.makeup, p.c); }
        const cells = [];
        const X1 = mag(goertzel(x, settle, len, f0, fs));
        const l1 = mag(goertzel(yL, settle, len, f0, fs)) / X1;   // normalise the linear stage to its own h1
        for (let h = 1; h <= 40; h++) {
          const hz = h * f0; if (h > 1 && (hz < 300 || hz > 900)) continue; if (hz > 900) break;
          const X = mag(goertzel(x, settle, len, hz, fs)); if (X < 1e-9) continue;
          const tF = mag(goertzel(yF, settle, len, hz, fs)) / X, tL = mag(goertzel(yL, settle, len, hz, fs)) / X / l1;
          cells.push(`h${h} ${hz.toFixed(0)}: ${db(tF).toFixed(2)}|${db(tL).toFixed(2)}|${db(tF / tL).toFixed(2)}`);
        }
        console.log(`  ${stName} ${nName} fs ${fs} drive ${dDb >= 0 ? '+' : ''}${dDb} dB mix ${m}: ` + cells.join('  '));
      }
    }
  }
}
