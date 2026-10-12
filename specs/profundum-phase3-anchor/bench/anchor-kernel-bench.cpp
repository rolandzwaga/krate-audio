// Reproduce: "/c/Program Files/LLVM/bin/clang++.exe" -std=c++20 -O2 -ffast-math -march=x86-64-v2 -DNDEBUG -Idsp/include <this file> -o anchor-bench.exe
// then: pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe anchor-bench.exe   (plan S9.8)
// Plan-design per-sample kernel of AnchorOscillator (plan S4.4-S4.8), worst-case load per SC-018: a waveform crossfade
// ALWAYS active (triangle <-> rounded sine, both evaluated every sample: an upper bound on "a switch every 100 ms"),
// the mode envelope ALWAYS on a raised-cosine segment (recurrence), two OnePoleSmoothers (level, a1), a 5 Hz vibrato
// f0PerSample re-evaluated on the U = 16 grid with the full S5 mirror (float bank eps + double eps-star + asin) and the
// triangle cap-taper count, host block 64, 48 kHz. Triangle K = 16 and K = 32 odd terms are timed separately (S1 C-1).
// T005 adds a third configuration, "K = 16, taper band": f0 = 494 Hz under the same 5 Hz +-1-semitone vibrato, FOLLOW
// ratio 1.25, so f_T = min(1.25 * delta_lock, capHz / fs) * fs sits in the cap-taper band and the S4.5 refreshTaper(f_T)
// slow path runs at every grid sample: real float std::log2 / std::pow per band term (odd n <= 31 with n*f_T in
// (taperStartHz, capHz), at most 9 here) plus T(2 f_T) and T(3 f_T), keyed bitwise on f_T. The tapered coefficients feed
// the Clenshaw triangle and a3e = 0.25 * T(3 f_T) feeds the rounded sine. The two E1 configurations are unchanged.
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/math_constants.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <xmmintrin.h>
#include <pmmintrin.h>

using namespace Krate::DSP;

// Cap taper constants as SpectralShapeRecipe (processors/spectral_shape_recipe.h :71-74, evaluateMask :168-180), 48 kHz.
constexpr float kBenchCapTaperCents = 1430.0f, kBenchCapTailCents = 10.0f, kBenchCapTaperDbPerCent = 0.09f;

template <int K, bool kTaperBand = false>
struct Kernel {
    double phi2 = 0.0, delta = 0.0;
    std::array<double, K> a{};
    std::array<double, K> aw{};          // taper-band config: a_k * T((2k+1) f_T)
    float capHz = 0.8f * 48000.0f * 0.5f, taperStartHz = 0.0f, tailFloor = 0.0f, taperFr = -1.0f;
    float a3e = 0.25f, a2e = 0.25f;
    float followRatio = 1.25f;
    int taperCalls = 0;                  // log2 + pow pairs evaluated (reported)
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
        taperStartHz = capHz * std::exp2(-kBenchCapTaperCents / 1200.0f);
        tailFloor = std::pow(10.0f, -kBenchCapTaperDbPerCent * (kBenchCapTaperCents - kBenchCapTailCents) / 20.0f);
        aw = a;
        startRc();
    }
    float taper(float nf) {              // T(nf), plan S4.5 (recipe cap branch without the low-note guard)
        if (nf >= capHz) return 0.0f;
        if (nf <= taperStartHz) return 1.0f;
        ++taperCalls;
        const float y = 1200.0f * std::log2(capHz / nf);
        if (y >= kBenchCapTaperCents) return 1.0f;
        return y > kBenchCapTailCents ? std::pow(10.0f, -kBenchCapTaperDbPerCent * (kBenchCapTaperCents - y) / 20.0f)
                                      : tailFloor * (std::max(0.0f, y) / kBenchCapTailCents);
    }
    void refreshTaper(float fT) {        // plan S4.5
        int ka = 0;
        while (ka < K && static_cast<float>(2 * ka + 1) * fT < capHz) ++ka;
        kActive = ka;
        if (31.0f * fT <= taperStartHz) { aw = a; a3e = 0.25f; a2e = 0.25f; return; }   // fast path
        for (int k = 0; k < ka; ++k) aw[k] = a[k] * static_cast<double>(taper(static_cast<float>(2 * k + 1) * fT));
        a2e = 0.25f * taper(2.0f * fT);
        a3e = 0.25f * taper(3.0f * fT);
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
        if constexpr (kTaperBand) {
            delta = std::min(static_cast<double>(followRatio) * delta, static_cast<double>(capHz) / fs);   // S4.10
            const float fT = static_cast<float>(delta * fs);
            if (std::memcmp(&fT, &taperFr, sizeof(float)) != 0) { refreshTaper(fT); taperFr = fT; }
        } else {
            const double capHzD = 0.8 * fs * 0.5;
            kActive = std::min(K, static_cast<int>((capHzD / f + 1.0) * 0.5));
        }
    }
    float tri(double u) const {   // Clenshaw for sum a_k sin((2k+1)x), c = 2cos2x = 2 - 4u^2
        const double c = 2.0 - 4.0 * u * u;
        double b1 = 0.0, b2 = 0.0;
        const std::array<double, K>& co = kTaperBand ? aw : a;
        for (int k = kActive - 1; k >= 1; --k) { const double b0 = co[k] + c * b1 - b2; b2 = b1; b1 = b0; }
        const double b0 = co[0] + c * b1 - b2;
        return static_cast<float>(u * (b0 - b1));
    }
    void process(float* out, const float* f0, int n) {
        for (int i = 0; i < n; ++i) {
            if ((grid & 15) == 0) gridUpdate(f0[i]);
            const double p1 = phi2 >= 1.0 ? phi2 - 1.0 : phi2;
            const float ang = static_cast<float>(6.283185307179586 * (p1 >= 0.5 ? p1 - 1.0 : p1));
            const float u = std::sin(ang);
            const float wA = tri(u) * 1.7320f;
            float wB;
            if constexpr (kTaperBand) wB = (u + a3e * (3.0f * u - 4.0f * u * u * u)) * 1.3720f;   // S4.5 rounded sine
            else wB = (1.75f * u - u * u * u) * 1.3720f;
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

template <int K, bool kTaperBand = false>
double runOnce(double baseHz = 41.2) {
    Kernel<K, kTaperBand> k; k.prepare(); k.xfStep = 1.0f / 240.0f;
    constexpr int kBlock = 64; constexpr double kFs = 48000.0; constexpr double kSeconds = 20.0;
    const int blocks = static_cast<int>(kSeconds * kFs / kBlock);
    std::vector<float> f0(kBlock), out(kBlock);
    std::vector<double> times; double t = 0.0;
    const auto start = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < kBlock; ++i, t += 1.0 / kFs) f0[i] = static_cast<float>(baseHz * std::pow(2.0, std::sin(6.283185307 * 5.0 * t) / 12.0));
        k.process(out.data(), f0.data(), kBlock);
        k.sink += out[kBlock - 1];
    }
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    // subtract the vibrato generation cost measured separately below
    std::printf("  (sink %g, taper log2+pow pairs %d)\n", static_cast<double>(k.sink), k.taperCalls);
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
        const double tTb = runOnce<16, true>(494.0);
        std::printf("rep %d: K=16 %.4f %% of one core, K=32 %.4f %%, K=16 taper band %.4f %% (vibrato generator %.4f %% subtracted)\n",
                    rep, (t16 - v) / 20.0 * 100.0, (t32 - v) / 20.0 * 100.0, (tTb - v) / 20.0 * 100.0, v / 20.0 * 100.0);
    }
    return 0;
}
