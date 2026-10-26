// Polyphase IIR halfband (Valenzuela-Constantinides) coefficient design + numeric verification (plan S3.2).
// node specs/profundum-phase2-edge-fold/halfband-design.js   -> prints coefficients, stopband, ripple, group delay.
// Note: the printed gains include the +6.02 dB interpolation gain of the raw upsampler impulse response.
// Polyphase IIR halfband (Valenzuela-Constantinides) coefficient design + numeric verification.
function transitionParam(tbw) {
  let k = Math.tan((1 - tbw * 2) * Math.PI / 4); k *= k;
  const kk = Math.pow(1 - k * k, 0.25), e = 0.5 * (1 - kk) / (1 + kk), e2 = e * e, e4 = e2 * e2;
  return { k, q: e * (1 + e4 * (2 + e4 * (15 + 150 * e4))) };
}
function accNum(q, order, c) { let i = 0, j = 1, acc = 0, t; do { t = Math.pow(q, i * (i + 1)) * Math.sin((i * 2 + 1) * c * Math.PI / order) * j; acc += t; j = -j; ++i; } while (Math.abs(t) > 1e-100); return acc; }
function accDen(q, order, c) { let i = 1, j = -1, acc = 0, t; do { t = Math.pow(q, i * i) * Math.cos(i * 2 * c * Math.PI / order) * j; acc += t; j = -j; ++i; } while (Math.abs(t) > 1e-100); return acc; }
function design(n, tbw) {
  const { k, q } = transitionParam(tbw), order = n * 2 + 1, a = [];
  for (let i = 0; i < n; i++) { const c = i + 1, ww = accNum(q, order, c) * Math.pow(q, 0.25) / (accDen(q, order, c) + 0.5), w2 = ww * ww;
    const x = Math.sqrt((1 - w2 * k) * (1 - w2 / k)) / (1 + w2); a.push((1 - x) / (1 + x)); }
  return a;
}
class AP { constructor(a) { this.a = a; this.x1 = 0; this.y1 = 0; } p(x) { const y = this.a * x + this.x1 - this.a * this.y1; this.x1 = x; this.y1 = y; return y; } }
class Up2 { constructor(c) { this.p0 = c.filter((_, i) => i % 2 === 0).map((a) => new AP(a)); this.p1 = c.filter((_, i) => i % 2 === 1).map((a) => new AP(a)); }
  proc(x, out) { let e = x, o = x; for (const s of this.p0) e = s.p(e); for (const s of this.p1) o = s.p(o); out[0] = e; out[1] = o; } }
class Down2 { constructor(c) { this.p0 = c.filter((_, i) => i % 2 === 0).map((a) => new AP(a)); this.p1 = c.filter((_, i) => i % 2 === 1).map((a) => new AP(a)); }
  proc(x0, x1) { let a = x1, b = x0; for (const s of this.p0) a = s.p(a); for (const s of this.p1) b = s.p(b); return 0.5 * (a + b); } }
module.exports = { design, Up2, Down2, AP };
if (require.main === module) {
  // Verify: impulse response of Up2 interleaved -> magnitude at normalized freqs of the 2x rate.
  for (const [n, tbw] of [[8, 0.05], [10, 0.05], [12, 0.04], [6, 0.1], [8, 0.1]]) {
    const c = design(n, tbw), up = new Up2(c), L = 1 << 14, h = new Float64Array(2 * L), o = [0, 0];
    for (let i = 0; i < L; i++) { up.proc(i === 0 ? 1 : 0, o); h[2 * i] = o[0]; h[2 * i + 1] = o[1]; }
    const H = (f) => { let re = 0, im = 0; for (let k = 0; k < h.length; k++) { re += h[k] * Math.cos(2 * Math.PI * f * k); im -= h[k] * Math.sin(2 * Math.PI * f * k); } return [re, im]; };
    // f in cycles/sample at 2x rate; base Nyquist = 0.25. Upsampler gain should be ~1 in passband.
    let stop = -1e9; for (let f = 0.25 + tbw / 2 + 0.002; f <= 0.5; f += 0.002) { const [r, i] = H(f); stop = Math.max(stop, 20 * Math.log10(Math.hypot(r, i))); }
    let ripple = 0; for (let f = 0; f <= 0.25 - tbw / 2; f += 0.002) { const [r, i] = H(f); ripple = Math.max(ripple, Math.abs(20 * Math.log10(Math.hypot(r, i)))); }
    const ph = (f) => { const [r, i] = H(f); return Math.atan2(i, r); };
    const gd = -(ph(0.0005) - ph(0.0004)) / (2 * Math.PI * 0.0001) / 2;  // in base-rate samples
    const at = (x) => { const [r, i] = H(x * 0.5); return 20 * Math.log10(Math.hypot(r, i)); };
    console.log(`N=${n} tbw=${tbw}: coefs [${c.map((v) => v.toFixed(4)).join(', ')}]\n   stopband max ${stop.toFixed(1)} dB (f >= ${(0.25 + tbw / 2).toFixed(3)}*fs2x), passband ripple ${ripple.toExponential(2)} dB, ` +
      `low-f group delay ${gd.toFixed(2)} base smp; gain at 0.45/0.5/0.55/0.6 base-fs: ${[0.45, 0.5, 0.55, 0.6].map(at).map((v) => v.toFixed(1)).join('/')}`);
  }
}
