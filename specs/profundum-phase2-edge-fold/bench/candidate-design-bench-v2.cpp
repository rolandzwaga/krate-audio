// Reproduce: clang++ -std=c++20 -O2 -ffast-math -march=x86-64-v2 -DNDEBUG -Idsp/include <this file> -o bench2.exe
// then: pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe bench2.exe   (plan S9.5, review round 2)
// candidate-design-bench.cpp plus everything that bench omitted: the excitation per-sample smoothers (mix, log2 D, bias)
// with exp2 and a bilinear 49 x 9 lookup of (M, c) EVERY sample (plan S5.3 rev), smoothstep w, the ADAA offset fb; the
// Fold amount smoother, five 401-point table lerps with a clamped index, beta / w smoothsteps, the 8-segment windowed-peak
// detector with instant attack, and the full static map (recipSqrt saturator + sine fold + makeup). Load per SC-018:
// 0.5 Hz drive automation (targets re-latched every 32 samples) at max excitation, Fold amount swept 0..100 at 0.5 Hz.
// The input is a sum of 8 sines (the core itself is not in this bench, so the figure is the stage cost only).
#include <krate/dsp/primitives/one_pole_allpass.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/sigmoid.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <xmmintrin.h>
#include <pmmintrin.h>
using namespace Krate::DSP;
static constexpr size_t kSeconds = 20;
static constexpr float kP = 0.3552343859f;
volatile float sink;
template <size_t N> struct Half2x {
    std::array<OnePoleAllpass, N / 2> uA, uB, dA, dB;
    void set(const float* c) { for (size_t i = 0; i < N / 2; ++i) { uA[i].setCoefficient(c[2*i]); uB[i].setCoefficient(c[2*i+1]); dA[i].setCoefficient(c[2*i]); dB[i].setCoefficient(c[2*i+1]); } }
    void up(float x, float* o) { float a = x, b = x; for (auto& s : uA) a = s.process(a); for (auto& s : uB) b = s.process(b); o[0] = a; o[1] = b; }
    float down(float x0, float x1) { float a = x1, b = x0; for (auto& s : dA) a = s.process(a); for (auto& s : dB) b = s.process(b); return 0.5f * (a + b); }
};
static const float c8[8] = {0.0358f, 0.1341f, 0.2720f, 0.4243f, 0.5721f, 0.7063f, 0.8271f, 0.9415f};
static const float c4[4] = {0.04245f, 0.17074f, 0.39332f, 0.74571f};
static float smoothstep(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
struct Chan {
    Half2x<8> outer; Half2x<4> inner; Biquad lpBase, lpOs, n1, n2, n3; DCBlocker dc; float v1 = 0, s1 = 1;
    void init() { outer.set(c8); inner.set(c4);
        lpBase.configure(FilterType::Lowpass, 500.f, 0.7071f, 0.f, 48000.f); lpOs.configure(FilterType::Lowpass, 500.f, 0.7071f, 0.f, 192000.f);
        n1.configure(FilterType::Notch, 0.50f * 48000.f, 1.5f, 0.f, 96000.f); n2.configure(FilterType::Notch, 0.52f * 48000.f, 2.0f, 0.f, 96000.f); n3.configure(FilterType::Notch, 0.55f * 48000.f, 1.5f, 0.f, 96000.f);
        dc.prepare(48000.0, 2.0f); }
    float finish(float x, float* u4, float cm, float w) {
        float a = inner.down(u4[0], u4[1]), b = inner.down(u4[2], u4[3]); a = n3.process(n2.process(n1.process(a))); b = n3.process(n2.process(n1.process(b)));
        const float lo = lpBase.process(x); return cm * (lo + (1.0f - w) * (x - lo)) + dc.process(outer.down(a, b)); }
};
struct Exc {
    Chan ch[2]; OnePoleSmoother mixS, l2S, bS; std::array<float, 49 * 9> tM{}, tC{};
    void init() { ch[0].init(); ch[1].init(); mixS.configure(5.0f, 48000.0f); l2S.configure(5.0f, 48000.0f); bS.configure(5.0f, 48000.0f);
        for (size_t i = 0; i < tM.size(); ++i) { tM[i] = 0.3f + 0.001f * float(i); tC[i] = 0.95f + 1e-4f * float(i); }
        mixS.setTarget(1.0f); mixS.snapToTarget(); bS.setTarget(0.5f); bS.snapToTarget(); }
    float bil(const std::array<float, 49 * 9>& T, float l2, float b) const {
        const float pi = std::clamp((l2 + 4.0f) * 8.0f, 0.0f, 48.0f), pj = std::clamp(b * 16.0f, 0.0f, 8.0f);
        const size_t i = std::min<size_t>(size_t(pi), 47), j = std::min<size_t>(size_t(pj), 7); const float ti = pi - float(i), tj = pj - float(j);
        const float a = T[j * 49 + i] + (T[j * 49 + i + 1] - T[j * 49 + i]) * ti, c = T[(j + 1) * 49 + i] + (T[(j + 1) * 49 + i + 1] - T[(j + 1) * 49 + i]) * ti;
        return a + (c - a) * tj; }
    void process(float& L, float& R) {
        const float m = mixS.process(), l2 = l2S.process(), b = bS.process(); const float D = std::exp2(l2), Mk = bil(tM, l2, b), ce = bil(tC, l2, b);
        const float cm = 1.0f - m + m * ce, w = smoothstep(m * 10.0f), sb = std::sqrt(1.0f + b * b), fb = (b + b) / (sb + sb);
        float* xs[2] = {&L, &R};
        for (int c = 0; c < 2; ++c) { Chan& h = ch[c]; float u2[2], u4[4]; const float x = *xs[c]; h.outer.up(x, u2); h.inner.up(u2[0], u4); h.inner.up(u2[1], u4 + 2);
            for (float& u : u4) { const float v = D * u * (1.0f / kP) + b, s = std::sqrt(1.0f + v * v); const float y = (v + h.v1) / (s + h.s1); h.v1 = v; h.s1 = s;
                const float ul = h.lpOs.process(u); u = m * Mk * (y - fb) + (1.0f - m) * u - cm * (ul + (1.0f - w) * (u - ul)); }
            *xs[c] = std::clamp(h.finish(x, u4, cm, w), -6.0f, 6.0f); } }
};
struct Fold {
    Chan ch[2]; OnePoleSmoother aS; std::array<std::array<float, 401>, 5> T{}; std::array<float, 8> ring{}; float segMax = 0, env = 0; size_t segCount = 0, segLen = 750, pos = 0;
    void init() { ch[0].init(); ch[1].init(); aS.configure(10.0f, 48000.0f);
        for (size_t i = 0; i < 401; ++i) { const float a = float(i) * 0.25f; T[0][i] = 0.01f + 0.033f * a; T[1][i] = a > 65 ? 2.7f * (a - 65) / 35 : 0; T[2][i] = 0.33f; T[3][i] = 1.0f + 0.002f * a; T[4][i] = 0.9f; } }
    void process(float& L, float& R) {
        const float d = std::max(std::abs(L), std::abs(R)); segMax = std::max(segMax, d);
        if (++segCount == segLen) { ring[pos] = segMax; pos = (pos + 1) & 7; segMax = 0; segCount = 0; }
        float raw = segMax; for (float r : ring) raw = std::max(raw, r);
        env = raw > env ? raw : env + 0.0005f * (raw - env); env = detail::flushDenormal(env);
        const float a = aS.process(); const float p = a * 4.0f; const size_t i = std::min<size_t>(size_t(p), 399); const float t = p - float(i);
        const auto lerp = [&](int k) { return T[k][i] + (T[k][i + 1] - T[k][i]) * t; };
        const float g = lerp(0), th = lerp(1), V = lerp(2), mk = lerp(3), c = lerp(4);
        const float beta = smoothstep((a - 30.0f) * 0.1f), w = smoothstep(a * 0.1f), G = (1.0f - beta) + beta * kP / std::max(env, 1e-6f), iG = 1.0f / G;
        const float pg = kP / std::max(g, 1e-6f), gp = g / kP, Vt = V / std::max(th, 1e-3f), tV = th / V;
        float* xs[2] = {&L, &R};
        for (int k = 0; k < 2; ++k) { Chan& h = ch[k]; float u2[2], u4[4]; const float x = *xs[k]; h.outer.up(x, u2); h.inner.up(u2[0], u4); h.inner.up(u2[1], u4 + 2);
            for (float& u : u4) { const float q = G * u; const float s = pg * Sigmoid::recipSqrt(gp * q); const float z = th < 0.001f ? s : Vt * std::sin(tV * s);
                const float ul = h.lpOs.process(u); u = mk * z * iG - c * (ul + (1.0f - w) * (u - ul)); }
            *xs[k] = std::clamp(h.finish(x, u4, c, w), -6.0f, 6.0f); } }
};
double bench(int which) {
    Exc e; Fold f; e.init(); f.init();
    const size_t n = 48000 * kSeconds; double ph = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (size_t k = 0; k < n; ++k) {
        if ((k & 31) == 0) { const double tt = double(k) / 48000.0; const float s = float(0.5 + 0.5 * std::sin(6.283185307 * 0.5 * tt));
            e.l2S.setTarget(std::log2(0.0631f) + s * (std::log2(3.981f) - std::log2(0.0631f))); f.aS.setTarget(100.0f * s); }
        ph += 0.0085; float L = 0; for (int j = 1; j <= 8; ++j) L += 0.06f * float(std::sin(ph * j)); float R = 0.9f * L;
        if (which & 1) e.process(L, R);
        if (which & 2) f.process(L, R);
        sink = L + R;
    }
    return 100.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / double(kSeconds);
}
int main() {
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    for (int rep = 0; rep < 3; ++rep) { const double base = bench(0);
        std::printf("rep %d | baseline(sig gen) %.3f%% | exc %.3f%% | fold %.3f%% | both %.3f%%  (net of baseline)\n", rep, base, bench(1) - base, bench(2) - base, bench(3) - base); }
}
