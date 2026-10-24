// ==============================================================================
// Test helpers: TransientShaper fixtures and measurement helpers
//                    (Profundum Phase 6, specs/profundum-phase6-mass-dynamics)
// ==============================================================================
// tasks.md T005 (deviation 3). Test-side only: allocation is fine here.
//
// LAYER RULE: no systems/ include, directly or indirectly. transient_shaper_test.cpp
// lives in dsp_processors_tests, and no processors TU includes a systems helper
// (Phase 5 deviation 3). The systems helper (mass_dynamics_test_helpers.h, T011)
// includes this header.
//
// Every helper is an `inline` function in namespace Krate::DSP::TransientShaperTest
// (never an anonymous namespace in a header; tasks.md rule 5).
//
// Infinities. This header is included by TUs compiled under the shipping
// -ffast-math / /fp:fast mode, so the +inf transientTimeMs returns is built from
// its bit pattern (ProfundumTest::posInf) and classified with ProfundumTest::isPosInf
// (spectral_shape_recipe_test_helpers.h, via harmonic_fold_test_helpers.h).
// ==============================================================================

#pragma once

#include "harmonic_fold_test_helpers.h"

#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Krate::DSP::TransientShaperTest {

// -----------------------------------------------------------------------------
// Notes (tasks.md rule 8): 440·2^((m − 69)/12) in double
// -----------------------------------------------------------------------------

inline double noteHz(int midi) { return 440.0 * std::exp2((static_cast<double>(midi) - 69.0) / 12.0); }

inline const double kE0Hz = noteHz(16);   // 20.60172
inline const double kE1Hz = noteHz(28);   // 41.20344
inline const double kE2Hz = noteHz(40);   // 82.40689
inline const double kC3Hz = noteHz(48);   // 130.81278

// -----------------------------------------------------------------------------
// Fixtures (tasks.md rule 9)
// -----------------------------------------------------------------------------

enum class Tone { Sine, Saw };

/// Raised-cosine onset / crossfade length: 0.5 ms (24 samples at 48 kHz).
inline constexpr double kRampMs = 0.5;

/// Whole-sample count of `seconds` at `fs`.
inline std::size_t secondsToSamples(double fs, double seconds) {
    return static_cast<std::size_t>(std::llround(seconds * fs));
}

/// round(0.5 ms · fs).
inline std::size_t rampSamples(double fs) { return secondsToSamples(fs, kRampMs * 0.001); }

/// Raised-cosine weight 0.5·(1 − cos(π·k / R)) for k in [0, R): 0 at k = 0, < 1 at k = R − 1.
inline double rampWeight(std::size_t k, std::size_t R) {
    return 0.5 * (1.0 - std::cos(HarmonicFoldTest::kTwoPi * 0.5 * static_cast<double>(k) / static_cast<double>(R)));
}

/// The steady tone at −12 dBFS RMS, starting at a zero crossing (sample 0 is 0):
/// Sine = HarmonicFoldTest::calibrationSine, Saw = recipeAdditive(kSawAnchor).
inline std::vector<float> steadyTone(Tone tone, double f0, double fs, double seconds) {
    if (tone == Tone::Sine)
        return HarmonicFoldTest::calibrationSine(fs, f0, seconds);
    return HarmonicFoldTest::recipeAdditive(SpectralShapeRecipe::kSawAnchor, f0, fs, seconds);
}

/// Sample index of the burst onset.
inline std::size_t onsetIndex(double fs, double preRollSeconds) { return secondsToSamples(fs, preRollSeconds); }

/// Pre-roll zeros, then the tone from its sample 0 weighted by a 0.5 ms raised cosine, held for
/// `holdSeconds`, then `tailSeconds` of zeros. Length = preRoll + hold + tail samples.
inline std::vector<float> burst(Tone tone, double f0, double fs, double holdSeconds = 1.0,
                                double preRollSeconds = 0.0, double tailSeconds = 0.25) {
    REQUIRE(holdSeconds > 0.0);
    REQUIRE(preRollSeconds >= 0.0);
    REQUIRE(tailSeconds >= 0.0);
    const std::size_t pre = secondsToSamples(fs, preRollSeconds);
    const std::size_t tail = secondsToSamples(fs, tailSeconds);
    const std::vector<float> x = steadyTone(tone, f0, fs, holdSeconds);
    const std::size_t hold = x.size();
    const std::size_t R = rampSamples(fs);
    REQUIRE(R >= 1u);
    REQUIRE(hold >= R);
    std::vector<float> y(pre + hold + tail, 0.0f);
    for (std::size_t k = 0; k < hold; ++k) {
        y[pre + k] = (k < R) ? static_cast<float>(static_cast<double>(x[k]) * rampWeight(k, R)) : x[k];
    }
    return y;
}

/// The tone ×10^(−12/20) for `lowSeconds`, then ×1 for `highSeconds`, joined by a 0.5 ms
/// raised-cosine gain crossfade starting at round(lowSeconds · fs).
inline std::vector<float> twoLevelStep(Tone tone, double f0, double fs, double lowSeconds, double highSeconds) {
    REQUIRE(lowSeconds > 0.0);
    REQUIRE(highSeconds > 0.0);
    const std::size_t s0 = secondsToSamples(fs, lowSeconds);
    const std::size_t total = s0 + secondsToSamples(fs, highSeconds);
    std::vector<float> x = steadyTone(tone, f0, fs, static_cast<double>(total) / fs);
    REQUIRE(x.size() == total);
    const std::size_t R = rampSamples(fs);
    REQUIRE(R >= 1u);
    REQUIRE(s0 + R <= total);
    const double gLow = std::pow(10.0, -12.0 / 20.0);
    for (std::size_t n = 0; n < s0 + R; ++n) {
        const double g = (n < s0) ? gLow : gLow + (1.0 - gLow) * rampWeight(n - s0, R);
        x[n] = static_cast<float>(static_cast<double>(x[n]) * g);
    }
    return x;
}

// -----------------------------------------------------------------------------
// Measurements
// -----------------------------------------------------------------------------

/// SC-008(c) transient time: peak = max of gainDb over [onset, onset + searchMs]; the first
/// sample after the peak index with gain <= peak − 3 dB, minus the onset, in ms. +inf (bit
/// pattern, ProfundumTest::posInf) when the gain never falls 3 dB.
inline double transientTimeMs(std::span<const float> gainDb, std::size_t onset, double fs, double searchMs = 50.0) {
    REQUIRE(onset < gainDb.size());
    REQUIRE(fs > 0.0);
    const std::size_t searchEnd = std::min(gainDb.size(), onset + secondsToSamples(fs, searchMs * 0.001) + 1);
    std::size_t peakIdx = onset;
    for (std::size_t i = onset + 1; i < searchEnd; ++i) {
        if (gainDb[i] > gainDb[peakIdx])
            peakIdx = i;
    }
    const double threshold = static_cast<double>(gainDb[peakIdx]) - 3.0;
    for (std::size_t i = peakIdx + 1; i < gainDb.size(); ++i) {
        if (static_cast<double>(gainDb[i]) <= threshold)
            return static_cast<double>(i - onset) * 1000.0 / fs;
    }
    return ProfundumTest::posInf();
}

/// D_tr window: round(max(0.05, 1.5 · maxDurationMs / 1000) · fs) samples.
inline std::size_t dTrWindow(double fs, double maxDurationMs) {
    return secondsToSamples(fs, std::max(0.05, 1.5 * maxDurationMs / 1000.0));
}

/// D_tr = 10·log10(energy [onset, onset + W) / energy [steadyStart, steadyStart + W)).
inline double dTrDb(const float* y, std::size_t n, std::size_t onset, std::size_t steadyStart, std::size_t window) {
    REQUIRE(y != nullptr);
    REQUIRE(window > 0u);
    REQUIRE(onset + window <= n);
    REQUIRE(steadyStart + window <= n);
    double eOnset = 0.0;
    double eSteady = 0.0;
    for (std::size_t i = 0; i < window; ++i) {
        eOnset += static_cast<double>(y[onset + i]) * static_cast<double>(y[onset + i]);
        eSteady += static_cast<double>(y[steadyStart + i]) * static_cast<double>(y[steadyStart + i]);
    }
    return ProfundumTest::energyRatioDb(eOnset, eSteady);
}

/// max − min of gainDb over [begin, end).
inline double rippleDb(std::span<const float> gainDb, std::size_t begin, std::size_t end) {
    REQUIRE(begin < end);
    REQUIRE(end <= gainDb.size());
    const auto [lo, hi] = std::minmax_element(gainDb.begin() + static_cast<std::ptrdiff_t>(begin),
                                              gainDb.begin() + static_cast<std::ptrdiff_t>(end));
    return static_cast<double>(*hi) - static_cast<double>(*lo);
}

/// Mean of v over [begin, end), in double.
inline double meanOf(std::span<const float> v, std::size_t begin, std::size_t end) {
    REQUIRE(begin < end);
    REQUIRE(end <= v.size());
    double sum = 0.0;
    for (std::size_t i = begin; i < end; ++i)
        sum += static_cast<double>(v[i]);
    return sum / static_cast<double>(end - begin);
}

/// RMS dBFS of y over its first HarmonicFoldTest::integerPeriodLength(n, fs, f0, 0.0) samples.
inline double levelDbPeriods(const float* y, std::size_t n, double fs, double f0) {
    REQUIRE(y != nullptr);
    const std::size_t len = HarmonicFoldTest::integerPeriodLength(n, fs, f0, 0.0);
    double sum = 0.0;
    for (std::size_t i = 0; i < len; ++i)
        sum += static_cast<double>(y[i]) * static_cast<double>(y[i]);
    return HarmonicFoldTest::safeDb10(sum / static_cast<double>(len));
}

/// 10·log10(Σ side² / Σ mid²), side = (L − R)/2, mid = (L + R)/2.
inline double sideMidRatioDb(const float* L, const float* R, std::size_t n) {
    REQUIRE(L != nullptr);
    REQUIRE(R != nullptr);
    double eSide = 0.0;
    double eMid = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double l = static_cast<double>(L[i]);
        const double r = static_cast<double>(R[i]);
        const double s = 0.5 * (l - r);
        const double m = 0.5 * (l + r);
        eSide += s * s;
        eMid += m * m;
    }
    return ProfundumTest::energyRatioDb(eSide, eMid);
}

/// Finite by bit pattern: exponent bits not all ones (fast-math immune, no FP comparison).
inline bool isFiniteBits(float x) noexcept {
    return (std::bit_cast<std::uint32_t>(x) & 0x7F800000u) != 0x7F800000u;
}

}  // namespace Krate::DSP::TransientShaperTest
