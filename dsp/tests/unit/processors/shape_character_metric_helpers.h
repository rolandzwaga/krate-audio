// ==============================================================================
// Test helpers: Shape & Character metric helpers (processors level)
//                          (Profundum Phase 5, specs/profundum-phase5-shape-character)
// ==============================================================================
// tasks.md T006 (deviation 3). Test-side only: allocation is fine here.
//
// Holds the spec's measurement definitions that both the processors-level
// Rectifier tests and the systems-level helper (shape_character_test_helpers.h,
// T008, which includes this header) need:
//   - differential click count  (spec "Differential click count", SC-003/009/021)
//   - steady-state max sample delta and the D-11 per-sample ratio (spec D-11)
//   - centroid C, inharmonic fraction I, level L (spec "Descriptor definitions")
//   - seeded white noise (xorshift32)
//
// LAYER RULE: no systems/ include, directly or indirectly (processors TUs
// include this header). The analysis primitives are reused from
// harmonic_fold_test_helpers.h (bh7PowerSpectrum, aliasFftLength, aliasedPower,
// integerPeriodLength, safeDb10).
//
// Every helper is an `inline` function in namespace
// Krate::DSP::ShapeCharacterTest (never an anonymous namespace in a header).
// ==============================================================================

#pragma once

#include "artifact_detection.h"
#include "harmonic_fold_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Krate::DSP::ShapeCharacterTest {

// -----------------------------------------------------------------------------
// Differential click count
// -----------------------------------------------------------------------------

struct ClickCount {
    std::size_t counted;               ///< transition detections with no reference detection within ±windowN
    std::size_t transitionDetections;  ///< raw ClickDetector detections on the transition render
    std::size_t referenceDetections;   ///< raw ClickDetector detections on the reference render(s), summed
    std::size_t windowN;               ///< N = floor(fs / f0 / 8) samples
    std::size_t guardedOut = 0;        ///< unmatched detections dropped by the amplitude guard
};

/// max |x[k] - x[k-1]| over k in [i - N, i + N] (clipped to [1, n - 1]).
inline double maxDeltaAround(const float* x, std::size_t n, std::size_t i, std::size_t N) {
    if (n < 2)
        return 0.0;
    const std::size_t lo = std::max<std::size_t>(i > N ? i - N : 0, 1);
    const std::size_t hi = std::min(n - 1, i + N);
    double m = 0.0;
    for (std::size_t k = lo; k <= hi; ++k)
        m = std::max(m, std::abs(static_cast<double>(x[k]) - static_cast<double>(x[k - 1])));
    return m;
}

/// Spec "Differential click count" (Phase 4 ruling B). `ClickDetector` with its documented
/// defaults (artifact_detection.h:38-44); the only change is `sampleRate = fs`. The detector is
/// never calibrated on the reference. `transition` and `reference` are phase-aligned sample for
/// sample, all `n` samples long. A transition detection counts only if NEITHER reference has a
/// detection within ±N samples, N = floor(fs / f0 / 8) (≤ 1/8 of the period of f0).
/// `refStart` is the stage held at the transition's start setting, `refEnd` the stage held at its
/// end setting (Group 3 gate ruling 2026-10-16, spec "Differential click count": steady detections
/// of the end setting are not transition artefacts). `refEnd` may be nullptr (one reference).
/// Amplitude guard (user ruling 2026-10-16, Clarifications "Group 9 rulings, round 2"; spec "Differential
/// click count"): a detection with no reference detection within ±N counts only if the transition's max
/// |Δy| over [i − N, i + N] exceeds 1.1 × the larger of the references' max |Δy| over the same window (one
/// reference: that reference); the ones it drops are reported as guardedOut.
inline ClickCount differentialClicks(const float* transition, const float* refStart, const float* refEnd,
                                     std::size_t n, double fs, double f0) {
    REQUIRE(transition != nullptr);
    REQUIRE(refStart != nullptr);
    const float* reference = refStart;
    REQUIRE(fs > 0.0);
    REQUIRE(f0 > 0.0);

    TestUtils::ClickDetectorConfig cfg{};   // documented defaults (artifact_detection.h:38-44)
    cfg.sampleRate = static_cast<float>(fs);
    REQUIRE(cfg.isValid());

    TestUtils::ClickDetector trDetector(cfg);
    trDetector.prepare();
    const std::vector<TestUtils::ClickDetection> tr = trDetector.detect(transition, n);

    TestUtils::ClickDetector refDetector(cfg);
    refDetector.prepare();
    std::vector<TestUtils::ClickDetection> ref = refDetector.detect(reference, n);
    if (refEnd != nullptr && refEnd != refStart) {
        TestUtils::ClickDetector endDetector(cfg);
        endDetector.prepare();
        const std::vector<TestUtils::ClickDetection> refE = endDetector.detect(refEnd, n);
        ref.insert(ref.end(), refE.begin(), refE.end());
    }

    const auto windowN = static_cast<std::size_t>(std::floor(fs / f0 / 8.0));

    std::vector<std::size_t> refIdx;
    refIdx.reserve(ref.size());
    for (const TestUtils::ClickDetection& d : ref)
        refIdx.push_back(d.sampleIndex);
    std::sort(refIdx.begin(), refIdx.end());

    constexpr double kGuardRatio = 1.1;
    std::size_t counted = 0;
    std::size_t guardedOut = 0;
    for (const TestUtils::ClickDetection& d : tr) {
        const std::size_t i = d.sampleIndex;
        const std::size_t lo = i > windowN ? i - windowN : 0;
        const auto first = std::lower_bound(refIdx.begin(), refIdx.end(), lo);
        const bool matched = first != refIdx.end() && *first <= i + windowN;
        if (matched)
            continue;
        double refMax = maxDeltaAround(refStart, n, i, windowN);
        if (refEnd != nullptr && refEnd != refStart)
            refMax = std::max(refMax, maxDeltaAround(refEnd, n, i, windowN));
        if (maxDeltaAround(transition, n, i, windowN) > kGuardRatio * refMax)
            ++counted;
        else
            ++guardedOut;
    }

    return ClickCount{.counted = counted,
                      .transitionDetections = tr.size(),
                      .referenceDetections = ref.size(),
                      .windowN = windowN,
                      .guardedOut = guardedOut};
}

/// One-reference form (the reference held at the start setting only).
inline ClickCount differentialClicks(const float* transition, const float* reference, std::size_t n, double fs,
                                     double f0) {
    return differentialClicks(transition, reference, nullptr, n, fs, f0);
}

// -----------------------------------------------------------------------------
// Sample-delta bounds
// -----------------------------------------------------------------------------

/// max |y[k] − y[k−1]| for k in [begin, end). Requires 1 ≤ begin < end.
inline double steadyDeltaMax(const float* y, std::size_t begin, std::size_t end) {
    REQUIRE(y != nullptr);
    REQUIRE(begin >= 1u);
    REQUIRE(begin < end);
    double m = 0.0;
    for (std::size_t k = begin; k < end; ++k)
        m = std::max(m, std::abs(static_cast<double>(y[k]) - static_cast<double>(y[k - 1])));
    return m;
}

/// Spec D-11, per sample, for a crossfade y = ga·xa + gb·xb (ga, gb ≥ 0):
/// max over k in [begin, end) of |y[k] − y[k−1]| /
///   (ga[k]·Da + gb[k]·Db + |ga[k] − ga[k−1]|·maxXa + |gb[k] − gb[k−1]|·maxXb).
/// Da, Db are the steady-state max sample deltas of each branch alone; maxXa, maxXb their max |x|.
/// A sample whose delta is zero contributes 0; a nonzero delta over a zero bound is numeric_limits<double>::max() (over
/// every bar). Not infinity(): the fast-math TUs that include this header make an infinity constant undefined.
/// Requires 1 ≤ begin < end.
inline double d11Ratio(const float* y, const float* ga, const float* gb, double Da, double Db, double maxXa,
                       double maxXb, std::size_t begin, std::size_t end) {
    REQUIRE(y != nullptr);
    REQUIRE(ga != nullptr);
    REQUIRE(gb != nullptr);
    REQUIRE(begin >= 1u);
    REQUIRE(begin < end);
    double worst = 0.0;
    for (std::size_t k = begin; k < end; ++k) {
        const double dy = std::abs(static_cast<double>(y[k]) - static_cast<double>(y[k - 1]));
        if (dy == 0.0)
            continue;
        const double gak = static_cast<double>(ga[k]);
        const double gbk = static_cast<double>(gb[k]);
        const double dga = std::abs(gak - static_cast<double>(ga[k - 1]));
        const double dgb = std::abs(gbk - static_cast<double>(gb[k - 1]));
        const double bound = gak * Da + gbk * Db + dga * maxXa + dgb * maxXb;
        if (bound <= 0.0)
            return std::numeric_limits<double>::max();
        worst = std::max(worst, dy / bound);
    }
    return worst;
}

// -----------------------------------------------------------------------------
// Descriptors C, I, L (spec "Descriptor definitions")
// -----------------------------------------------------------------------------

/// Spec C: power-weighted mean of log2(f / f0) over all BH7 bins with 20 Hz ≤ f ≤ min(fs/2, 20 kHz),
/// on the last `HarmonicFoldTest::aliasFftLength(fs, f0)` samples of y[0..n). Octaves above f0.
inline double centroidOct(const float* y, std::size_t n, double fs, double f0) {
    REQUIRE(y != nullptr);
    REQUIRE(fs > 0.0);
    REQUIRE(f0 > 0.0);
    const std::size_t L = HarmonicFoldTest::aliasFftLength(fs, f0);
    REQUIRE(n >= L);
    const std::vector<double> p = HarmonicFoldTest::bh7PowerSpectrum(y + (n - L), L);
    const double binHz = fs / static_cast<double>(L);
    const double fMax = std::min(fs * 0.5, 20000.0);
    double num = 0.0;
    double den = 0.0;
    for (std::size_t k = 1; k < p.size(); ++k) {
        const double f = static_cast<double>(k) * binHz;
        if (f < 20.0 || f > fMax)
            continue;
        num += p[k] * std::log2(f / f0);
        den += p[k];
    }
    REQUIRE(den > 0.0);
    return num / den;
}

/// Spec I: power outside ±8 bins of every n·f0 < fs/2 (bins 0–8 excluded) over total power, in dB,
/// on the BH7 `aliasFftLength` frame (the last L samples of y[0..n)). This is literally the
/// `aliasedPower` mask applied to the total: aliasedDbfs − totalDb (both re 0.5).
inline double inharmonicFractionDb(const float* y, std::size_t n, double fs, double f0) {
    REQUIRE(y != nullptr);
    const std::size_t L = HarmonicFoldTest::aliasFftLength(fs, f0);
    REQUIRE(n >= L);
    const HarmonicFoldTest::AliasResult a = HarmonicFoldTest::aliasedPower(y + (n - L), L, fs, f0);
    return a.aliasedDbfs - a.totalDb;
}

/// Spec L: RMS dBFS, unwindowed, over the last `integerPeriodLength(n, fs, f0, 1.0)` samples.
inline double levelDb(const float* y, std::size_t n, double fs, double f0) {
    REQUIRE(y != nullptr);
    const std::size_t len = HarmonicFoldTest::integerPeriodLength(n, fs, f0, 1.0);
    const float* x = y + (n - len);
    double sum = 0.0;
    for (std::size_t i = 0; i < len; ++i)
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    return HarmonicFoldTest::safeDb10(sum / static_cast<double>(len));
}

// -----------------------------------------------------------------------------
// Seeded noise
// -----------------------------------------------------------------------------

/// Uniform white noise from xorshift32 (13, 17, 5), mapped to [−1, 1), then scaled so the RMS of
/// the n returned samples equals `rmsDbfs` (computed in double). Deterministic per seed; seed != 0.
inline std::vector<float> whiteNoise(std::size_t n, std::uint32_t seed, double rmsDbfs) {
    REQUIRE(n > 0u);
    REQUIRE(seed != 0u);
    std::uint32_t s = seed;
    std::vector<double> raw(n, 0.0);
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        const double u = static_cast<double>(s >> 8) * (1.0 / 16777216.0);   // [0, 1)
        raw[i] = 2.0 * u - 1.0;
        sum += raw[i] * raw[i];
    }
    const double rms = std::sqrt(sum / static_cast<double>(n));
    REQUIRE(rms > 0.0);
    const double scale = std::pow(10.0, rmsDbfs / 20.0) / rms;
    std::vector<float> out(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i)
        out[i] = static_cast<float>(raw[i] * scale);
    return out;
}

}  // namespace Krate::DSP::ShapeCharacterTest
