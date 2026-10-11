// Reproduce: clang++ -std=c++20 -O2 -ffast-math -march=x86-64-v2 -DNDEBUG -Idsp/include <this file> -o bench.exe
// then: pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe bench.exe   (plan S9.5)
// Plan-design CPU (plan S9.5): excitation (4x polyphase halfband from OnePoleAllpass, 500 Hz split LP at 4x, 3 notches @2fs, ADAA1, DCB)
// + Fold (same oversampler, split, 3 notches @2fs, saturator + sine fold, shared detector, DCB), stereo. Sections use OnePoleAllpass
// (per-call NaN check + 2 flushDenormal), so the lean inline sections of plan S3 can only be cheaper.
#include <krate/dsp/primitives/one_pole_allpass.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <xmmintrin.h>
#include <pmmintrin.h>
using namespace Krate::DSP;
static constexpr size_t kRun = 16, kSeconds = 20;
volatile float sink;
template <size_t N> struct Half2x {
    std::array<OnePoleAllpass, N / 2> uA, uB, dA, dB;
    void set(const float* c) { for (size_t i = 0; i < N / 2; ++i) { uA[i].setCoefficient(c[2*i]); uB[i].setCoefficient(c[2*i+1]); dA[i].setCoefficient(c[2*i]); dB[i].setCoefficient(c[2*i+1]); } }
    void up(float x, float* o) { float a = x, b = x; for (auto& s : uA) a = s.process(a); for (auto& s : uB) b = s.process(b); o[0] = a; o[1] = b; }
    float down(float x0, float x1) { float a = x1, b = x0; for (auto& s : dA) a = s.process(a); for (auto& s : dB) b = s.process(b); return 0.5f * (a + b); }
};
static const float c8[8] = {0.0358f, 0.1341f, 0.2720f, 0.4243f, 0.5721f, 0.7063f, 0.8271f, 0.9415f};
static const float c4[4] = {0.04245f, 0.17074f, 0.39332f, 0.74571f};
struct Stage {   // one channel of one stage
    Half2x<8> outer; Half2x<4> inner; Biquad lpBase, lpOs, n1, n2, n3; DCBlocker dc; float v1 = 0, s1 = 1;
    void init(bool fold) { outer.set(c8); inner.set(c4);
        lpBase.configure(FilterType::Lowpass, 500.f, 0.7071f, 0.f, 48000.f); lpOs.configure(FilterType::Lowpass, 500.f, 0.7071f, 0.f, 192000.f);
        n1.configure(FilterType::Notch, 0.50f * 48000.f, 1.5f, 0.f, 96000.f); n2.configure(FilterType::Notch, 0.52f * 48000.f, 2.0f, 0.f, 96000.f); n3.configure(FilterType::Notch, 0.55f * 48000.f, 1.5f, 0.f, 96000.f);
        dc.prepare(48000.0, 2.0f); (void)fold; }
    float exc(float x) { float u2[2], u4[4]; outer.up(x, u2); inner.up(u2[0], u4); inner.up(u2[1], u4 + 2);
        for (float& u : u4) { const float lo = lpOs.process(u); const float v = 4.0f * u + 0.5f, s = std::sqrt(1.0f + v * v);
            const float y = (v + v1) / (s + s1); v1 = v; s1 = s; u = 0.3f * (y - 0.447f) - 0.95f * lo; }
        float a = inner.down(u4[0], u4[1]), b = inner.down(u4[2], u4[3]); a = n3.process(n2.process(n1.process(a))); b = n3.process(n2.process(n1.process(b)));
        return 0.95f * lpBase.process(x) + dc.process(outer.down(a, b)); }
    float fold(float x, float g) { float u2[2], u4[4]; outer.up(x, u2); inner.up(u2[0], u4); inner.up(u2[1], u4 + 2);
        for (float& u : u4) { const float lo = lpOs.process(u); const float w = 1.2f * u * g; const float v = w / std::sqrt(1.0f + w * w);
            u = std::sin(3.0f * v) * 0.33f / g - 0.54f * lo; }
        float a = inner.down(u4[0], u4[1]), b = inner.down(u4[2], u4[3]); a = n3.process(n2.process(n1.process(a))); b = n3.process(n2.process(n1.process(b)));
        return 0.54f * lpBase.process(x) + dc.process(outer.down(a, b)); }
};
double bench(int which) {
    Stage eL, eR, fL, fR; eL.init(false); eR.init(false); fL.init(true); fR.init(true);
    float env = 0.3f; double ph = 0;
    const size_t n = 48000 * kSeconds;
    auto t0 = std::chrono::steady_clock::now();
    for (size_t k = 0; k < n; ++k) {
        ph += 0.0108; float L = 0.3f * float(std::sin(ph)), R = L;
        if (which & 1) { L = eL.exc(L); R = eR.exc(R); }
        if (which & 2) { const float a = std::max(std::abs(L), std::abs(R)); env = a > env ? env + 0.05f * (a - env) : env * 0.99995f; const float g = 0.355f / std::max(env, 1e-6f);
            L = fL.fold(L, g); R = fR.fold(R, g); }
        sink = L + R;
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    // subtract the test-signal generation cost (which == 0)
    return 100.0 * s / double(kSeconds);
}
int main() {
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    for (int rep = 0; rep < 3; ++rep) { const double base = bench(0);
        std::printf("rep %d | baseline(sig gen) %.3f%% | exc %.3f%% | fold %.3f%% | both %.3f%%  (net of baseline)\n", rep, base, bench(1) - base, bench(2) - base, bench(3) - base); }
}
