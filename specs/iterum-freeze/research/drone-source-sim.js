// Drone source simulation for the reconcile of design.md 2.5.3 (R2 + finding 10).
// K Hann-windowed read heads at 75 % (K = 4) or more overlap, pass length Lp, starts drawn uniformly over a
// 2 s window, optionally aligned (+/- 5 ms search, 5 ms span) to the other heads' sum. No pitch shifting:
// this measures the source that feeds the voices. Run: node specs/iterum-freeze/research/drone-source-sim.js
// Output of 2026-10-29 is quoted in design.md 2.5.3 and 8 (finding 10).
'use strict';
const sr = 44100;
const W = 2 * sr;

function lcg(seed) {
  let s = seed;
  return () => { s = (s * 1103515245 + 12345) & 0x7fffffff; return s / 0x7fffffff; };
}

// The D8 (3) drum window: kick (60 Hz, 80 ms exponential decay, peak 0.8) on every beat, hat (white noise,
// 20 ms decay, peak 0.25) on every off-beat, a 233 Hz pad at 0.05; 120 BPM.
function drumWindow() {
  const x = new Float32Array(W);
  const rnd = lcg(12345);
  for (let i = 0; i < W; i++) {
    const t = i / sr;
    const beatT = t % 0.5;
    const offT = (t + 0.25) % 0.5;
    x[i] = 0.8 * Math.sin(2 * Math.PI * 60 * beatT) * Math.exp(-beatT / 0.08)
         + 0.25 * (2 * rnd() - 1) * Math.exp(-offT / 0.02)
         + 0.05 * Math.sin(2 * Math.PI * 233 * t);
  }
  return x;
}

function toneWindow(hz) {
  const x = new Float32Array(W);
  for (let i = 0; i < W; i++) x[i] = 0.5 * Math.sin(2 * Math.PI * hz * i / sr);
  return x;
}

function render(win, K, LpMs, align, seed) {
  const Lp = Math.round(LpMs * sr / 1000);
  const N = 4 * sr;
  const out = new Float32Array(N);
  const rnd = lcg(seed);
  const hann = (k) => 0.5 - 0.5 * Math.cos(2 * Math.PI * k / Lp);
  const heads = [];
  for (let h = 0; h < K; h++) heads.push({ start: -1, t0: 0 });
  const stagger = Lp / K;
  const norm = 2 / K;  // K Hann windows staggered by Lp / K sum to K / 2
  const F = Math.round(0.005 * sr);
  const R = Math.round(0.005 * sr);
  for (let n = 0; n < N; n++) {
    for (let h = 0; h < K; h++) {
      const hd = heads[h];
      const off = Math.round(h * stagger);
      if (n >= off && (n - off) % Lp === 0) {
        let s = Math.floor(rnd() * (W - Lp - 2 * R)) + R;
        if (align) {
          const mix = new Float32Array(F);
          for (const o of heads) {
            if (o === hd || o.start < 0) continue;
            for (let k = 0; k < F; k++) {
              const p = n + k - o.t0;
              if (p >= 0 && p < Lp) mix[k] += hann(p) * win[o.start + p];
            }
          }
          let me = 0;
          for (let k = 0; k < F; k++) me += mix[k] * mix[k];
          if (me > 1e-9) {
            let best = s, bestC = -Infinity;
            for (let d = -R; d <= R; d++) {
              const ss = s + d;
              let c = 0, e = 0;
              for (let k = 0; k < F; k++) { c += mix[k] * win[ss + k]; e += win[ss + k] * win[ss + k]; }
              const cn = c / Math.sqrt(e * me + 1e-12);
              if (cn > bestC) { bestC = cn; best = ss; }
            }
            s = best;
          }
        }
        hd.start = s;
        hd.t0 = n;
      }
      const p = n - hd.t0;
      if (hd.start >= 0 && p >= 0 && p < Lp) out[n] += norm * hann(p) * win[hd.start + p];
    }
  }
  return out;
}

function depth(x, from, to) {
  const w = Math.round(0.01 * sr);
  const r = [];
  for (let i = from; i + w <= to; i += w) {
    let s = 0;
    for (let k = 0; k < w; k++) s += x[i + k] * x[i + k];
    r.push(Math.sqrt(s / w) + 1e-12);
  }
  r.sort((a, b) => a - b);
  const q = (p) => r[Math.min(r.length - 1, Math.floor(p * r.length))];
  return { p95p5: 20 * Math.log10(q(0.95) / q(0.05)), maxmin: 20 * Math.log10(r[r.length - 1] / r[0]) };
}

function levelDb(x, from, to, refRms) {
  let s = 0;
  for (let i = from; i < to; i++) s += x[i] * x[i];
  return 20 * Math.log10(Math.sqrt(s / (to - from)) / refRms);
}

const from = sr;
const to = 4 * sr;
const drum = drumWindow();
const dIn = depth(drum, 0, W);
console.log(`drum window itself              p95/p5 ${dIn.p95p5.toFixed(1)} dB  max/min ${dIn.maxmin.toFixed(1)} dB`);
for (const [K, Lp, align] of [[4, 200, false], [4, 200, true], [4, 500, true], [8, 200, false], [8, 200, true],
                              [16, 500, false], [64, 200, false], [128, 500, false]]) {
  const d = depth(render(drum, K, Lp, align, 7), from, to);
  console.log(`drum K=${K} Lp=${Lp} ms align=${align}`.padEnd(32) +
              ` p95/p5 ${d.p95p5.toFixed(1)} dB  max/min ${d.maxmin.toFixed(1)} dB`);
}
for (const hz of [200, 222, 233, 247, 260, 261.63, 311]) {
  const tw = toneWindow(hz);
  for (const align of [false, true]) {
    const o = render(tw, 4, 200, align, 3);
    const d = depth(o, from, to);
    console.log(`tone ${hz} Hz K=4 Lp=200 ms align=${align}`.padEnd(40) +
                ` max/min ${d.maxmin.toFixed(1)} dB  level ${levelDb(o, from, to, 0.5 / Math.SQRT2).toFixed(1)} dB re window`);
  }
}
