// tasks.md D-1 (2026-10-11): fold-index law candidates. Run once per law, concurrently:
//   FOLD_LAW=linear|1.5|2|3|smooth node specs/profundum-phase2-edge-fold/evidence/d1law.js
// Static steady state of the Fold (S4.3 map, detector gain G = (1 - beta) + beta * P / peak) on one period, 16384 points,
// FFT; harmonics n with n*f0 < fs/2. Gates (D-1): every 0.25 pair |dC| <= 0.04 oct, |dL|, |dCF| <= 0.25 dB,
// |dTHD| <= max(3 x median, 1) dB on the calibration sine (E0/E1/E2/C2 x 44.1/48/96 kHz) and the C2 mid grid
// (44.1/48 kHz); SC-006 THD strictly increasing, min step >= +0.05 dB, H non-decreasing, H(100) >= 4 H(30), regime
// rises >= 6 dB, mid-grid centroid rise >= 0.5 oct; SC-012(d) |T1| >= -6 dB below 65, >= -12 dB on [65, 100], angle <= 5 deg,
// Re(T1) > 0, on the sine and the saw anchor (E0/E1/E2/C2, 48 kHz).
const M = require('./lib.js');
const N = 16384;
const law = M.FOLD.foldLaw;
const fails = [];
const tone = (amps) => { const xs = new Float64Array(N); for (let k = 0; k < N; k++) { let s = 0;
  for (let j = 0; j < amps.length; j++) if (amps[j]) s += amps[j] * Math.sin((j + 1) * 2 * Math.PI * k / N); xs[k] = s; } return xs; };
const spectrum = (y) => { const re = Float64Array.from(y), im = new Float64Array(N); M.fft(re, im); return { re, im }; };
// Descriptors of one rendered period: x input, y output, Nh harmonics.
function descr(x, y, sp, Nh) {
  let p1 = 0, rest = 0, num = 0, den = 0, h = 0, msx = 0, ms = 0, pk = 0; const mean = sp.re[0] / N;
  const w = (n) => (sp.re[n] ** 2 + sp.im[n] ** 2) * 2 / N / N;
  for (let n = 1; n <= Nh; n++) { const wn = w(n); if (n === 1) p1 = wn; else rest += wn; num += Math.log2(n) * wn; den += wn; }
  for (let n = 2; n <= Nh; n++) if (w(n) >= p1 * 1e-6) h++;
  for (let k = 0; k < N; k++) { msx += x[k] * x[k]; const v = y[k] - mean; ms += v * v; pk = Math.max(pk, Math.abs(v)); }
  return { L: 10 * Math.log10(ms / msx), CF: 20 * Math.log10(pk / Math.sqrt(ms / N)), C: num / den, thd: 10 * Math.log10(rest / p1), H: h,
    h1: [sp.re[1], sp.im[1]] };
}
function sweep(amps) {
  const x = tone(amps); let pk = 0; for (const v of x) pk = Math.max(pk, Math.abs(v));
  const out = [];
  for (let i = 0; i <= 400; i++) { const a = i / 4, p = M.foldParams(a), G = (1 - p.beta) + p.beta * M.P_CAL / pk, F = M.foldMap(a);
    const y = x.map((v) => F(G * v) / G); out.push({ y, sp: spectrum(y) }); }
  return { x, xsp: spectrum(x), out };
}
function pairs(rows, label, cBar) {
  let wL = [0, 0], wCF = [0, 0], wC = [0, 0]; const dthd = [];
  for (let i = 1; i <= 400; i++) { const a = rows[i], b = rows[i - 1];
    const l = Math.abs(a.L - b.L), c = Math.abs(a.CF - b.CF), o = Math.abs(a.C - b.C);
    if (l > wL[0]) wL = [l, i / 4]; if (c > wCF[0]) wCF = [c, i / 4]; if (o > wC[0]) wC = [o, i / 4];
    if (a.thd >= -60 && b.thd >= -60) dthd.push([Math.abs(a.thd - b.thd), i / 4]); }
  const med = dthd.map((d) => d[0]).sort((p, q) => p - q)[Math.floor(dthd.length / 2)], thr = Math.max(3 * med, 1);
  const wT = dthd.reduce((m, d) => (d[0] > m[0] ? d : m), [0, 0]);
  if (wC[0] > cBar) fails.push(`${label} |dC| ${wC[0].toFixed(4)} @${wC[1]}`);
  if (wL[0] > 0.25) fails.push(`${label} |dL|`); if (wCF[0] > 0.25) fails.push(`${label} |dCF|`); if (wT[0] > thr) fails.push(`${label} |dTHD|`);
  return `|dL| ${wL[0].toFixed(3)} @${wL[1]}, |dCF| ${wCF[0].toFixed(3)} @${wCF[1]}, |dC| ${wC[0].toFixed(4)} @${wC[1]}, |dTHD| ${wT[0].toFixed(3)} @${wT[1]} (bound ${thr.toFixed(3)})`;
}
function t1(rows, xsp, label) { // SC-012(d) static T1
  const h1in = [xsp.re[1], xsp.im[1]]; let minLo = 1e9, minHi = 1e9, maxAng = 0, flip = false;
  for (let i = 0; i <= 400; i++) { const [re, im] = rows[i].h1; const den = h1in[0] ** 2 + h1in[1] ** 2;
    const tr = (re * h1in[0] + im * h1in[1]) / den, ti = (im * h1in[0] - re * h1in[1]) / den;
    const mag = 10 * Math.log10(tr * tr + ti * ti), ang = Math.abs(Math.atan2(ti, tr)) * 180 / Math.PI;
    if (i < 260) minLo = Math.min(minLo, mag); else minHi = Math.min(minHi, mag); maxAng = Math.max(maxAng, ang); if (!(tr > 0)) flip = true; }
  if (minLo < -6 || minHi < -12 || maxAng > 5 || flip) fails.push(`${label} T1`);
  return `|T1| min ${minLo.toFixed(2)} (<65) / ${minHi.toFixed(2)} dB ([65,100]), max angle ${maxAng.toFixed(2)} deg, flip ${flip}`;
}
const t0 = Date.now();
console.log(`== D-1 fold-index law ${law} (thetaMax ${M.FOLD.thetaMax}); theta at 65/70/82.5/90/99.75/100: ` +
  [65, 70, 82.5, 90, 99.75, 100].map((a) => M.foldParams(a).theta.toFixed(4)).join(' / '));
// Calibration sine: the static output is note-independent except for the harmonic count Nh.
const sine = sweep([M.P_CAL]);
for (const note of ['E0', 'E1', 'E2', 'C2']) for (const fs of [44100, 48000, 96000]) {
  const f0 = M.NOTES[note], Nh = Math.min(Math.floor((fs / 2 - 1e-9) / f0), N / 2 - 1);
  const rows = sine.out.map((o) => descr(sine.x, o.y, o.sp, Nh)), lab = `sine ${note} ${fs}`;
  let mono = true, minStep = [1e9, 0], hMono = true;
  for (let i = 1; i <= 400; i++) { const d = rows[i].thd - rows[i - 1].thd; if (!(d > 0)) mono = false; if (d < minStep[0]) minStep = [d, i / 4];
    if (rows[i].H < rows[i - 1].H) hMono = false; }
  const r = (a) => rows[Math.round(a * 4)];
  const rises = [r(30).thd - r(0).thd, r(65).thd - r(30).thd, r(100).thd - r(65).thd];
  if (!mono || minStep[0] < 0.05) fails.push(`${lab} THD min step ${minStep[0].toFixed(4)} @${minStep[1]}`);
  if (!hMono) fails.push(`${lab} H not monotone`); if (r(100).H < 4 * r(30).H) fails.push(`${lab} H(100)`);
  if (rises.some((v) => v < 6)) fails.push(`${lab} regime rise`);
  console.log(`${lab}: ${pairs(rows, lab, 0.04)}\n   THD min step ${minStep[0].toFixed(4)} dB @${minStep[1]}, H ${r(30).H} -> ${r(100).H}, rises ${rises.map((v) => v.toFixed(2)).join(' / ')}; ${t1(rows, sine.xsp, lab)}`);
}
for (const fs of [44100, 48000]) {
  const f0 = M.NOTES.C2, Nh = Math.floor((fs / 2 - 1e-9) / f0), s = sweep(M.coreAmps(M.STATES.mid, f0, fs));
  const rows = s.out.map((o) => descr(s.x, o.y, o.sp, Nh)), lab = `mid C2 ${fs}`, rise = rows[400].C - rows[0].C;
  if (rise < 0.5) fails.push(`${lab} centroid rise`);
  console.log(`${lab}: ${pairs(rows, lab, 0.04)}\n   C(0) ${rows[0].C.toFixed(3)} C(100) ${rows[400].C.toFixed(3)} rise ${rise.toFixed(3)} oct (bar 0.5)`);
}
for (const note of ['E0', 'E1', 'E2', 'C2']) { const fs = 48000, f0 = M.NOTES[note], Nh = Math.floor((fs / 2 - 1e-9) / f0);
  const s = sweep(M.coreAmps(M.STATES.saw, f0, fs)), rows = s.out.map((o) => descr(s.x, o.y, o.sp, Nh));
  console.log(`saw ${note} ${fs}: ${t1(rows, s.xsp, `saw ${note}`)}`); }
console.log(`VERDICT ${law}: ${fails.length ? 'FAIL ' + fails.join('; ') : 'PASS'}  (${((Date.now() - t0) / 1000).toFixed(1)} s)`);
