// ==============================================================================
// Band-level difference metric (test-only; Phase 11 spec Conventions, plan 6.3)
// ==============================================================================
// Mid M = (L + R) / 2 and side S = (L - R) / 2; five bands
// [20, 80), [80, 160), [160, 320), [320, 1280), [1280, min(20000, fs/2)).
// Welch power spectrum with Krate::DSP::FFT at size 8192, a periodic Hann window
// and 50 % overlap; bin power (normalised to mean square, so a 0 dBFS sine reads
// about -3 dB) is summed per band and the level is 10*log10(E + 1e-20) dB.
//
// Aggregate mode: one reading per render; the difference is the max over
// 5 bands x {M, S} of |delta dB|. Frame-wise mode: one reading per 8192-sample
// frame (50 % hop); the difference is the max over frames x bands x {M, S}.
// A band is excluded when BOTH readings are below -100 dB re full scale, full
// scale being a 0 dBFS sine's band level, computed once here.
//
// Allocates freely: test side only, never on an audio thread.
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/fft.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>
#include <vector>

namespace ProfundumPluginTest {

inline constexpr std::size_t kBandLevelBandCount = 5;

/// db[0][band] = M, db[1][band] = S.
struct BandLevelReading {
    std::array<std::array<double, 5>, 2> db{};
};

/// Band edges in Hz; the last edge is clamped to min(20000, fs/2).
inline constexpr std::array<double, 6> kBandEdgesHz{20.0, 80.0, 160.0, 320.0, 1280.0, 20000.0};

namespace bandmetric_detail {

inline constexpr std::size_t kWelchSize = 8192;
inline constexpr std::size_t kWelchHop = kWelchSize / 2;
inline constexpr double kLevelEpsilon = 1e-20;
inline constexpr double kExclusionDbBelowFullScale = 100.0;

/// FFT, Hann window and scratch buffers for one metric call.
struct Workspace {
    Krate::DSP::FFT fft;
    std::vector<float> window;
    std::vector<float> frame;
    std::vector<Krate::DSP::Complex> spectrum;
    double powerNorm = 0.0;  // 2 / (N * sum(w^2)): mean square per one-sided bin

    Workspace()
        : window(kWelchSize), frame(kWelchSize), spectrum(kWelchSize / 2 + 1) {
        fft.prepare(kWelchSize);
        double sumW2 = 0.0;
        for (std::size_t n = 0; n < kWelchSize; ++n) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(n) /
                                                  static_cast<double>(kWelchSize));
            window[n] = static_cast<float>(w);
            sumW2 += w * w;
        }
        powerNorm = 2.0 / (static_cast<double>(kWelchSize) * sumW2);
    }
};

/// Adds the band powers of one windowed frame of x[0, count) (zero-padded to
/// kWelchSize) to `bandPower`.
inline void addFramePower(Workspace& ws, const float* x, std::size_t count, double fs,
                          std::array<double, kBandLevelBandCount>& bandPower) {
    const std::size_t n = std::min(count, kWelchSize);
    for (std::size_t i = 0; i < kWelchSize; ++i)
        ws.frame[i] = i < n ? x[i] * ws.window[i] : 0.0f;
    ws.fft.forward(ws.frame.data(), ws.spectrum.data());

    const double binHz = fs / static_cast<double>(kWelchSize);
    const double topEdge = std::min(kBandEdgesHz[kBandLevelBandCount], fs * 0.5);
    for (std::size_t b = 0; b < kBandLevelBandCount; ++b) {
        const double lo = kBandEdgesHz[b];
        const double hi = (b + 1 == kBandLevelBandCount) ? topEdge : kBandEdgesHz[b + 1];
        double e = 0.0;
        for (std::size_t k = 0; k < ws.spectrum.size(); ++k) {
            const double f = static_cast<double>(k) * binHz;
            if (f < lo || f >= hi) continue;
            const double re = ws.spectrum[k].real;
            const double im = ws.spectrum[k].imag;
            e += (re * re + im * im) * ws.powerNorm;
        }
        bandPower[b] += e;
    }
}

/// Welch reading of M and S over [begin, begin + count).
inline BandLevelReading readingOverRange(Workspace& ws, const std::vector<float>& m,
                                         const std::vector<float>& s, std::size_t begin,
                                         std::size_t count, double fs) {
    std::array<std::array<double, kBandLevelBandCount>, 2> power{};
    std::size_t frames = 0;
    if (count < kWelchSize) {
        addFramePower(ws, m.data() + begin, count, fs, power[0]);
        addFramePower(ws, s.data() + begin, count, fs, power[1]);
        frames = 1;
    } else {
        for (std::size_t start = 0; start + kWelchSize <= count; start += kWelchHop) {
            addFramePower(ws, m.data() + begin + start, kWelchSize, fs, power[0]);
            addFramePower(ws, s.data() + begin + start, kWelchSize, fs, power[1]);
            ++frames;
        }
    }
    BandLevelReading r;
    for (std::size_t c = 0; c < 2; ++c)
        for (std::size_t b = 0; b < kBandLevelBandCount; ++b)
            r.db[c][b] = 10.0 * std::log10(power[c][b] / static_cast<double>(frames) + kLevelEpsilon);
    return r;
}

inline void midSide(std::span<const float> L, std::span<const float> R, std::vector<float>& m,
                    std::vector<float>& s) {
    const std::size_t n = std::min(L.size(), R.size());
    m.resize(n);
    s.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        m[i] = (L[i] + R[i]) * 0.5f;
        s[i] = (L[i] - R[i]) * 0.5f;
    }
}

/// Band level of a 0 dBFS sine (1 kHz at 48 kHz, read in its band [320, 1280)),
/// computed once.
inline double fullScaleDb() {
    static const double level = [] {
        constexpr double fs = 48000.0;
        constexpr std::size_t n = 4 * kWelchSize;
        std::vector<float> sine(n);
        for (std::size_t i = 0; i < n; ++i)
            sine[i] = static_cast<float>(
                std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / fs));
        const std::vector<float> zero(n, 0.0f);
        Workspace ws;
        const BandLevelReading r = readingOverRange(ws, sine, zero, 0, n, fs);
        return r.db[0][3];
    }();
    return level;
}

/// Max |delta dB| over bands x {M, S}, skipping bands below the floor in both.
inline double readingDifference(const BandLevelReading& a, const BandLevelReading& b,
                                double floorDb) {
    double worst = 0.0;
    for (std::size_t c = 0; c < 2; ++c) {
        for (std::size_t k = 0; k < kBandLevelBandCount; ++k) {
            if (a.db[c][k] < floorDb && b.db[c][k] < floorDb) continue;
            worst = std::max(worst, std::abs(a.db[c][k] - b.db[c][k]));
        }
    }
    return worst;
}

} // namespace bandmetric_detail

/// Welch M/S band levels of one stereo render (over min(L.size(), R.size())).
[[nodiscard]] inline BandLevelReading bandLevels(std::span<const float> L, std::span<const float> R,
                                                 double fs) {
    namespace d = bandmetric_detail;
    std::vector<float> m;
    std::vector<float> s;
    d::midSide(L, R, m, s);
    d::Workspace ws;
    return d::readingOverRange(ws, m, s, 0, m.size(), fs);
}

/// Band-level difference between two stereo renders, in dB (see file header).
[[nodiscard]] inline double bandLevelDifferenceDb(std::span<const float> L1, std::span<const float> R1,
                                                  std::span<const float> L2, std::span<const float> R2,
                                                  double fs, bool frameWise) {
    namespace d = bandmetric_detail;
    const double floorDb = d::fullScaleDb() - d::kExclusionDbBelowFullScale;

    if (!frameWise)
        return d::readingDifference(bandLevels(L1, R1, fs), bandLevels(L2, R2, fs), floorDb);

    std::vector<float> m1;
    std::vector<float> s1;
    std::vector<float> m2;
    std::vector<float> s2;
    d::midSide(L1, R1, m1, s1);
    d::midSide(L2, R2, m2, s2);
    const std::size_t n = std::min(m1.size(), m2.size());
    d::Workspace ws;

    if (n < d::kWelchSize) {
        return d::readingDifference(d::readingOverRange(ws, m1, s1, 0, n, fs),
                                    d::readingOverRange(ws, m2, s2, 0, n, fs), floorDb);
    }
    double worst = 0.0;
    for (std::size_t start = 0; start + d::kWelchSize <= n; start += d::kWelchHop) {
        const BandLevelReading a = d::readingOverRange(ws, m1, s1, start, d::kWelchSize, fs);
        const BandLevelReading b = d::readingOverRange(ws, m2, s2, start, d::kWelchSize, fs);
        worst = std::max(worst, d::readingDifference(a, b, floorDb));
    }
    return worst;
}

} // namespace ProfundumPluginTest
