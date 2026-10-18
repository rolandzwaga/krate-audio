// ==============================================================================
// Layer 2: Processor Tests - PeriodPeakDetector
//                              (Profundum Phase 6, specs/profundum-phase6-mass-dynamics)
// ==============================================================================
// Reference: specs/profundum-phase6-mass-dynamics/plan.md (S1 C-4, S3, R-11)
//            specs/profundum-phase6-mass-dynamics/tasks.md (T002 wires this TU, T004 fills it)
//
// SCOPE (tags [processors][profundum][period-peak]): the constants match HarmonicFold's
//   private detector (R-11 drift guard), the held value bounds the input, the D-4 steady
//   ripple on a non-integer period, the hold length and the pending-segment boundary,
//   setters and lifecycle (no allocation).
//   Layer rule: no systems/ include, directly or through a helper.
// ==============================================================================

#include "harmonic_fold_test_helpers.h"

// allocation_detector.h ONLY. NEVER allocation_operator_overrides.h: the global operator
// new/delete replacements are included from exactly one TU of dsp_processors_tests
// (brownian_drift_test.cpp); a second include is a duplicate-symbol link error.
#include <allocation_detector.h>

#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/period_peak_detector.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace Krate::DSP;
namespace hft = Krate::DSP::HarmonicFoldTest;

namespace {

constexpr int kNoteE0 = 16;  // 20.6017 Hz
constexpr int kNoteE1 = 28;  // 41.2034 Hz
constexpr int kNoteE2 = 40;  // 82.4069 Hz
constexpr int kNoteC3 = 48;  // 130.813 Hz

/// MIDI note -> Hz in double (A4 = 440).
double midiHz(int note) { return 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0); }

/// (max - min) / mean over [first, last) (the HarmonicFold D-4 ripple, harmonic_fold_test.cpp).
double rippleOf(const double* first, const double* last) {
    REQUIRE(last > first);
    const auto [mn, mx] = std::minmax_element(first, last);
    double sum = 0.0;
    for (const double* p = first; p != last; ++p)
        sum += *p;
    const double mean = sum / static_cast<double>(last - first);
    REQUIRE(mean > 0.0);
    return (*mx - *mn) / mean;
}

/// The HarmonicFold D-4 fixture tone: y = 0.2 * sum_{h=1..partials} sin(2 pi h f0 n / fs) / h, in
/// double, cast to float once. Each partial is a rotating phasor re-anchored to the exact
/// sin / cos every 4096 samples (drift far below float resolution).
std::vector<float> d4Saw(double f0, double fs, int partials, std::size_t total) {
    std::vector<double> acc(total, 0.0);
    constexpr std::size_t kAnchor = 4096;
    for (int h = 1; h <= partials; ++h) {
        const double w = hft::kTwoPi * static_cast<double>(h) * f0 / fs;
        const double cw = std::cos(w);
        const double sw = std::sin(w);
        const double g = 1.0 / static_cast<double>(h);
        double s = 0.0;
        double c = 1.0;
        for (std::size_t n = 0; n < total; ++n) {
            if (n % kAnchor == 0) {
                s = std::sin(w * static_cast<double>(n));
                c = std::cos(w * static_cast<double>(n));
            }
            acc[n] += g * s;
            const double sNext = s * cw + c * sw;
            c = c * cw - s * sw;
            s = sNext;
        }
    }
    std::vector<float> y(total, 0.0f);
    for (std::size_t n = 0; n < total; ++n)
        y[n] = static_cast<float>(0.2 * acc[n]);
    return y;
}

/// Ripple of held() over [settle, x.size()) for a one-channel detector at `holdMs`.
double heldRipple(const std::vector<float>& x, double fs, float holdMs, std::size_t settle) {
    REQUIRE(settle < x.size());
    PeriodPeakDetector det;
    det.prepare(fs, holdMs);
    std::vector<double> held(x.size(), 0.0);
    for (std::size_t n = 0; n < x.size(); ++n)
        held[n] = static_cast<double>(det.process(x[n]));
    return rippleOf(held.data() + settle, held.data() + held.size());
}

/// Non-finite floats from their IEEE-754 bit patterns through a volatile sink (no
/// numeric_limits literal under -ffinite-math-only).
float floatFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t b = sink;
    return std::bit_cast<float>(b);
}

}  // namespace

// =============================================================================
// R-11: the duplicated detector keeps HarmonicFold's constants
// =============================================================================

TEST_CASE("PeriodPeakDetector_ConstantsMatchHarmonicFold", "[processors][profundum][period-peak]") {
    static_assert(PeriodPeakDetector::kSegments == HarmonicFold::kDetectorSegments);
    static_assert(PeriodPeakDetector::kPeakOversampling == HarmonicFold::kDetectorPeakOversampling);
    static_assert(PeriodPeakDetector::kMaxHoldMs == HarmonicFold::kDetectorHoldMs);
    static_assert(PeriodPeakDetector::kMinHoldMs == 1.0f);
    static_assert(PeriodPeakDetector::kMaxChannels == 2);
    CHECK(PeriodPeakDetector::kSegments == HarmonicFold::kDetectorSegments);
    CHECK(PeriodPeakDetector::kPeakOversampling == HarmonicFold::kDetectorPeakOversampling);
    CHECK(PeriodPeakDetector::kMaxHoldMs == HarmonicFold::kDetectorHoldMs);
    CHECK(PeriodPeakDetector::kMinHoldMs == 1.0f);
    CHECK(PeriodPeakDetector::kMaxChannels == 2u);
}

// =============================================================================
// The held value bounds the input (one channel and two channels)
// =============================================================================

TEST_CASE("PeriodPeakDetector_HeldBoundsInput", "[processors][profundum][period-peak]") {
    constexpr double fs = 48000.0;
    constexpr float kHoldMs = 50.0f;
    struct Tone {
        const char* name;
        std::vector<float> x;
    };
    const std::array<Tone, 2> tones{{
        {"kSawAnchor E1", hft::recipeAdditive(SpectralShapeRecipe::kSawAnchor, midiHz(kNoteE1), fs, 3.0)},
        {"sine E0", hft::calibrationSine(fs, midiHz(kNoteE0), 3.0)},
    }};
    constexpr std::size_t kDelay = 100;
    for (const Tone& t : tones) {
        const std::vector<float>& x = t.x;
        REQUIRE(x.size() > kDelay);

        PeriodPeakDetector mono;
        mono.prepare(fs, kHoldMs);
        std::size_t monoViolations = 0;
        float monoMinMargin = 1.0e30f;
        for (const float v : x) {
            const float h = mono.process(v);
            const bool heldMatches = (mono.held() == h);
            if (!(h >= std::abs(v)) || !heldMatches)
                ++monoViolations;
            monoMinMargin = std::min(monoMinMargin, h - std::abs(v));
        }

        PeriodPeakDetector stereo;
        stereo.prepare(fs, kHoldMs);
        std::size_t stereoViolations = 0;
        float stereoMinMargin = 1.0e30f;
        for (std::size_t n = 0; n < x.size(); ++n) {
            const float xL = x[n];
            const float xR = (n >= kDelay) ? 0.5f * x[n - kDelay] : 0.0f;
            const float h = stereo.process(xL, xR);
            const float bound = std::max(std::abs(xL), std::abs(xR));
            if (!(h >= bound))
                ++stereoViolations;
            stereoMinMargin = std::min(stereoMinMargin, h - bound);
        }

        std::printf("PeriodPeakDetector_HeldBoundsInput %s (hold %.1f ms): one channel %zu violations "
                    "(min held - |x| = %.6g), two channels %zu violations (min held - max|x| = %.6g)\n",
                    t.name, static_cast<double>(kHoldMs), monoViolations, static_cast<double>(monoMinMargin),
                    stereoViolations, static_cast<double>(stereoMinMargin));
        INFO(t.name);
        CHECK(monoViolations == 0u);
        CHECK(stereoViolations == 0u);
        CHECK(mono.stateFinite());
        CHECK(stereo.stateFinite());
    }
}

// =============================================================================
// D-4 (harmonic_fold_test.cpp HarmonicFold_DetectorSteadyOnNonIntegerPeriod): steady held
// value on a non-integer sample period
// =============================================================================

TEST_CASE("PeriodPeakDetector_SteadyOnNonIntegerPeriod", "[processors][profundum][period-peak]") {
    constexpr float kHoldMs = 50.0f;
    // Bars (user ruling 2026-10-17, spec FR-004 row / Clarifications "Group 2 gate ruling"): the 64-partial arm keeps
    // HarmonicFold's D-4 figure, 1e-4. The wide (Gibbs) arm is the raw held value with no follower (HarmonicFold's
    // test reads its follower, whose 0.5 % release hysteresis absorbs raw ripple up to 5e-3, over a 125 ms hold):
    // 2.3e-3 = 0.02 dB peak-to-peak, one tenth of SC-009's 0.2 dB on the gain trace.
    constexpr double kBar64 = 1.0e-4;
    constexpr double kBarWide = 2.3e-3;
    const double f0 = midiHz(kNoteE1);
    // 64 partials (the core's count) and a wide-band saw to 0.45 fs (sharp Gibbs peaks).
    for (const bool wide : {false, true})
    for (const double fs : {44100.0, 48000.0, 96000.0}) {
        const int partials = wide ? static_cast<int>(0.45 * fs / f0) : 64;
        const double kBar = wide ? kBarWide : kBar64;
        const auto settle = static_cast<std::size_t>(1.0 * fs);
        const auto window = static_cast<std::size_t>(2.0 * fs);
        const std::vector<float> y = d4Saw(f0, fs, partials, settle + window);
        const double ripple = heldRipple(y, fs, kHoldMs, settle);
        std::printf("PeriodPeakDetector_SteadyOnNonIntegerPeriod %d partials, fs %.0f, f0 %.4f Hz, hold %.1f ms: "
                    "ripple %.3g (bar %.1e)\n",
                    partials, fs, f0, static_cast<double>(kHoldMs), ripple, kBar);
        INFO(partials << " partials, fs " << fs << ": ripple " << ripple);
        CHECK(ripple <= kBar);
    }

    // Recorded, not gated: kSawAnchor with the hold at exactly one period (1000 / f0 ms), the
    // compressor's C-6 configuration, 48 kHz.
    constexpr double kRecordFs = 48000.0;
    for (const int note : {kNoteE0, kNoteE1, kNoteE2, kNoteC3}) {
        const double hz = midiHz(note);
        const auto holdMs = static_cast<float>(1000.0 / hz);
        const std::vector<float> x = hft::recipeAdditive(SpectralShapeRecipe::kSawAnchor, hz, kRecordFs, 3.0);
        const double ripple = heldRipple(x, kRecordFs, holdMs, static_cast<std::size_t>(1.0 * kRecordFs));
        std::printf("PeriodPeakDetector_SteadyOnNonIntegerPeriod [record] kSawAnchor note %d (%.4f Hz), fs %.0f, "
                    "hold one period %.3f ms: ripple %.3g\n",
                    note, hz, kRecordFs, static_cast<double>(holdMs), ripple);
    }
}

// =============================================================================
// Hold length and the pending segment boundary
// =============================================================================

TEST_CASE("PeriodPeakDetector_HoldLengthAndBoundary", "[processors][profundum][period-peak]") {
    constexpr double fs = 48000.0;
    // hold 10 ms: W = 480, segLen = 60; an impulse at segment offset o is held 9 * 60 - 1 - o
    // samples (in [480, 540]). The oversampler's decaying tail (< 1e-2 one segment on) follows;
    // it is not exactly 0 without FTZ, so "then 0" is read as "then below kTailBound".
    constexpr float kTailBound = 1.0e-2f;
    PeriodPeakDetector det;
    det.prepare(fs, 10.0f);

    // Part 1: impulse at sample 0 (offset 0 of the first segment), then zeros to sample 599.
    std::vector<float> h1;
    for (std::size_t n = 0; n < 600; ++n)
        h1.push_back(det.process(n == 0 ? 1.0f : 0.0f));
    std::size_t len1 = 0;
    while (len1 < h1.size() && h1[len1] == 1.0f)
        ++len1;
    float tail1 = 0.0f;
    for (std::size_t n = len1; n < h1.size(); ++n)
        tail1 = std::max(tail1, h1[n]);

    // Part 2: 600 samples is a segment boundary. 30 zeros put the detector mid-segment;
    // setHoldMs(20) there (segLen 120 pending); 30 more zeros complete the 60-sample segment;
    // the impulse lands at offset 0 of the first 120-sample segment.
    for (std::size_t n = 0; n < 30; ++n)
        (void)det.process(0.0f);
    det.setHoldMs(20.0f);
    const float holdAfterCall = det.holdMs();
    for (std::size_t n = 0; n < 30; ++n)
        (void)det.process(0.0f);
    std::vector<float> h2;
    for (std::size_t n = 0; n < 1300; ++n)
        h2.push_back(det.process(n == 0 ? 1.0f : 0.0f));
    std::size_t len2 = 0;
    while (len2 < h2.size() && h2[len2] == 1.0f)
        ++len2;
    float tail2 = 0.0f;
    for (std::size_t n = len2; n < h2.size(); ++n)
        tail2 = std::max(tail2, h2[n]);

    std::printf("PeriodPeakDetector_HoldLengthAndBoundary: hold 10 ms held %zu samples (bar [480, 540]), tail max "
                "%.3g; hold 20 ms held %zu samples (bar [960, 1080]), tail max %.3g; holdMs() after call %.3f\n",
                len1, static_cast<double>(tail1), len2, static_cast<double>(tail2),
                static_cast<double>(holdAfterCall));
    CHECK(len1 >= 480u);
    CHECK(len1 <= 540u);
    CHECK(tail1 < kTailBound);
    CHECK(holdAfterCall == 20.0f);
    CHECK(len2 >= 960u);
    CHECK(len2 <= 1080u);
    CHECK(tail2 < kTailBound);
    CHECK(det.stateFinite());
}

// =============================================================================
// Setters and lifecycle, allocation-free
// =============================================================================

TEST_CASE("PeriodPeakDetector_SettersAndLifecycle", "[processors][profundum][period-peak]") {
    const float nanValue = floatFromBits(0x7FC00000u);
    const float posInf = floatFromBits(0x7F800000u);
    const float negInf = floatFromBits(0xFF800000u);

    PeriodPeakDetector det;
    float afterNan = 0.0f;
    float afterPosInf = 0.0f;
    float afterNegInf = 0.0f;
    float afterLow = 0.0f;
    float afterHigh = 0.0f;
    float heldBeforeReset = 0.0f;
    float heldAfterReset = -1.0f;
    bool finiteAfterReset = false;
    float stereoHeld = 0.0f;
    std::size_t observed = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        det.prepare(48000.0, 50.0f);
        det.setHoldMs(nanValue);
        afterNan = det.holdMs();
        det.setHoldMs(posInf);
        afterPosInf = det.holdMs();
        det.setHoldMs(negInf);
        afterNegInf = det.holdMs();
        det.setHoldMs(0.1f);
        afterLow = det.holdMs();
        det.setHoldMs(500.0f);
        afterHigh = det.holdMs();
        for (int n = 0; n < 4096; ++n)
            (void)det.process(0.4f * std::sin(0.01f * static_cast<float>(n)));
        for (int n = 0; n < 512; ++n)
            stereoHeld = det.process(0.3f, -0.5f);
        heldBeforeReset = det.held();
        det.reset();
        heldAfterReset = det.held();
        finiteAfterReset = det.stateFinite();
        // AllocationScope latches in its destructor, so the live figure is read while it is open.
        observed = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    std::printf("PeriodPeakDetector_SettersAndLifecycle: NaN -> %.3f, +Inf -> %.3f, -Inf -> %.3f, 0.1 -> %.3f, "
                "500 -> %.3f; held before reset %.6f, after %.6f; allocations %zu\n",
                static_cast<double>(afterNan), static_cast<double>(afterPosInf), static_cast<double>(afterNegInf),
                static_cast<double>(afterLow), static_cast<double>(afterHigh),
                static_cast<double>(heldBeforeReset), static_cast<double>(heldAfterReset), observed);
    CHECK(afterNan == 50.0f);
    CHECK(afterPosInf == 50.0f);
    CHECK(afterNegInf == 50.0f);
    CHECK(afterLow == PeriodPeakDetector::kMinHoldMs);
    CHECK(afterHigh == PeriodPeakDetector::kMaxHoldMs);
    CHECK(stereoHeld >= 0.5f);
    CHECK(heldBeforeReset > 0.0f);
    CHECK(heldAfterReset == 0.0f);
    CHECK(finiteAfterReset);
    REQUIRE(observed == 0u);

    // Positive control: the detector sees an allocation inside an identical scope.
    std::size_t control = 0;
    int probeObserved = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        int* probe = new int(42);
        volatile int* probeSink = probe;  // defeat new/delete elision
        probeObserved = *probeSink;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        delete probe;
        control = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    CHECK(probeObserved == 42);
    CHECK(control >= 1u);
}
