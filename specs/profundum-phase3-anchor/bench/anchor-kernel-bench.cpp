// Reproduce: clang++ -std=c++20 -O2 -ffast-math -march=x86-64-v2 -DNDEBUG -Idsp/include <this file> -o anchor-bench.exe
// then: pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe anchor-bench.exe   (plan S9.8)
// Plan-design per-sample kernel of AnchorOscillator (plan S4.4-S4.8), worst-case load per SC-018: a waveform crossfade
// ALWAYS active (triangle <-> rounded sine, both evaluated every sample: an upper bound on "a switch every 100 ms"),
// the mode envelope ALWAYS on a raised-cosine segment (recurrence), two OnePoleSmoothers (level, a1), a 5 Hz vibrato
// f0PerSample re-evaluated on the U = 16 grid with the full S5 mirror (float bank eps + double eps-star + asin) and the
// triangle cap-taper count, host block 64, 48 kHz. Triangle K = 16 and K = 32 odd terms are timed separately (S1 C-1).
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/math_constants.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
#include <xmmintrin.h>
#include <pmmintrin.h>

using namespace Krate::DSP;

template <int K>
struct Kernel {
    double phi2 = 0.0, delta = 0.0;
    std::array<double, K> a{};
    int kActive = K;
    OnePoleSmoother level, a1;
    double rcC = 1.0, rcS = 0.0, rcCd = 1.0, rcSd = 0.0, g0 = 1.0, amp = 0.5;   // raised-cosine recurrence
    int rcN = 0, rcLen = 0;
    float xf = 0.0f; float xfStep = 0.0f;
    float shadow = 1.0f; float invFs = 1.0f / 48000.0f; double fs = 48000.0; int grid = 0;
    float sink = 0.0f;

    void prepare() {
        for (int k = 0; k < K; ++k) a[k] = 8.0 / (kPi * kPi) / ((2.0 * k + 1) * (2.0 * k + 1)) * ((k & 1) ? -1.0 : 1.0);
        level.configure(5.0f, 48000.0f); a1.configure(5.0f, 48000.0f);
        level.setTarget(0.5f); a1.setTarget(0.35f);
        startRc();
    }
    void startRc() { rcLen = 480; rcN = 0; rcC = 1.0; rcS = 0.0; const double w = 3.14159265358979 / rcLen; rcCd = std::cos(w); rcSd = std::sin(w); }
    void gridUpdate(float f) {
        const float arg = (kPi * f) * invFs;
        const float be = std::clamp(2.0f * std::sin(arg), -1.99f, 1.99f);
        const double eb = 2.0 * std::sin(3.14159265358979 * f / fs);
        const double es = 2.0 * std::sin(3.14159265358979 * static_cast<double>(f) / fs);
        const float dT = static_cast<float>(es / eb);
        const float m = dT / shadow; shadow = shadow * m;
        const float ee = std::clamp(be * shadow, -1.99f, 1.99f);
        delta = std::asin(0.5 * static_cast<double>(ee)) / 3.14159265358979;
        const double capHz = 0.8 * fs * 0.5;
        kActive = std::min(K, static_cast<int>((capHz / f + 1.0) * 0.5));
    }
    float tri(double u) const {   // Clenshaw for sum a_k sin((2k+1)x), c = 2cos2x = 2 - 4u^2
        const double c = 2.0 - 4.0 * u * u;
        double b1 = 0.0, b2 = 0.0;
        for (int k = kActive - 1; k >= 1; --k) { const double b0 = a[k] + c * b1 - b2; b2 = b1; b1 = b0; }
        const double b0 = a[0] + c * b1 - b2;
        return static_cast<float>(u * (b0 - b1));
    }
    void process(float* out, const float* f0, int n) {
        for (int i = 0; i < n; ++i) {
            if ((grid & 15) == 0) gridUpdate(f0[i]);
            const double p1 = phi2 >= 1.0 ? phi2 - 1.0 : phi2;
            const float ang = static_cast<float>(6.283185307179586 * (p1 >= 0.5 ? p1 - 1.0 : p1));
            const float u = std::sin(ang);
            const float wA = tri(u) * 1.7320f;
            const float wB = (1.75f * u - u * u * u) * 1.3720f;
            xf += xfStep; if (xf >= 1.0f || xf <= 0.0f) xfStep = -xfStep;
            const float c = 0.5f - 0.5f * std::cos(3.14159265f * xf);   // documented raised-cosine weight
            const float w = wA + c * (wB - wA);
            // envelope: raised cosine via rotation recurrence, restarted when done
            const double nc = rcC * rcCd - rcS * rcSd; rcS = rcS * rcCd + rcC * rcSd; rcC = nc;
            const double g = g0 + amp * (1.0 - rcC);
            if (++rcN >= rcLen) startRc();
            const float gain = level.process() * a1.process() * static_cast<float>(g) * 0.70710678f;
            out[i] = gain * w;
            phi2 += delta; if (phi2 >= 2.0) phi2 -= 2.0;
            grid = (grid + 1) & 31;
        }
    }
};

template <int K>
double runOnce() {
    Kernel<K> k; k.prepare(); k.xfStep = 1.0f / 240.0f;
    constexpr int kBlock = 64; constexpr double kFs = 48000.0; constexpr double kSeconds = 20.0;
    const int blocks = static_cast<int>(kSeconds * kFs / kBlock);
    std::vector<float> f0(kBlock), out(kBlock);
    std::vector<double> times; double t = 0.0;
    const auto start = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < kBlock; ++i, t += 1.0 / kFs) f0[i] = static_cast<float>(41.2 * std::pow(2.0, std::sin(6.283185307 * 5.0 * t) / 12.0));
        k.process(out.data(), f0.data(), kBlock);
        k.sink += out[kBlock - 1];
    }
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    // subtract the vibrato generation cost measured separately below
    std::printf("  (sink %g)\n", static_cast<double>(k.sink));
    return el;
}

double vibratoOnly() {
    constexpr int kBlock = 64; constexpr double kFs = 48000.0; constexpr double kSeconds = 20.0;
    const int blocks = static_cast<int>(kSeconds * kFs / kBlock);
    std::vector<float> f0(kBlock); double t = 0.0; float sink = 0.0f;
    const auto start = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < kBlock; ++i, t += 1.0 / kFs) f0[i] = static_cast<float>(41.2 * std::pow(2.0, std::sin(6.283185307 * 5.0 * t) / 12.0));
        sink += f0[kBlock - 1];
    }
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("  (sink %g)\n", static_cast<double>(sink));
    return el;
}

int main() {
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    for (int rep = 0; rep < 5; ++rep) {
        const double v = vibratoOnly();
        const double t16 = runOnce<16>();
        const double t32 = runOnce<32>();
        std::printf("rep %d: K=16 %.4f %% of one core, K=32 %.4f %% (vibrato generator %.4f %% subtracted)\n", rep,
                    (t16 - v) / 20.0 * 100.0, (t32 - v) / 20.0 * 100.0, v / 20.0 * 100.0);
    }
    return 0;
}
