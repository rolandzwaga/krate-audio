// ==============================================================================
// FeedbackExciter contract tests (Phase 3 -- T035)
// ==============================================================================
// Contract invariants (1-6) plus:
//   (7) With non-zero bodyFeedback, output self-sustains longer than without.
//   (8) Peak ≤ 0 dBFS for any bodyFeedback in [-1,+1] at all velocities
//       (SC-008, US1-6 energy limiter).
//   (9) Allocation-detector wraps the full process(bodyFeedback) path.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/exciters/feedback_exciter.h"
#include "dsp/float_guard.h"
#include "exciter_test_helpers.h"

#include <allocation_detector.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

using membrum_exciter_tests::isFiniteSample;
using membrum_exciter_tests::runBurst;
using membrum_exciter_tests::spectralCentroidDFT;

TEST_CASE("FeedbackExciter: trigger + process(bodyFeedback) is allocation-free",
          "[membrum][exciter][feedback][alloc]")
{
    Membrum::FeedbackExciter exc;
    exc.prepare(44100.0, 0);
    {
        TestHelpers::AllocationScope scope;
        exc.trigger(0.8f);
        for (int i = 0; i < 1024; ++i)
        {
            // alternating feedback to exercise full path
            const float fb = (i & 1) ? 0.6f : -0.6f;
            (void)exc.process(fb);
        }
        CHECK(scope.getAllocationCount() == 0);
    }
}

TEST_CASE("FeedbackExciter: velocity spectral centroid ratio >= 2.0 (SC-004)",
          "[membrum][exciter][feedback][velocity]")
{
    constexpr double kSR = 44100.0;
    constexpr int kSamples = 441;
    Membrum::FeedbackExciter exc;
    exc.prepare(kSR, 0);

    std::array<float, kSamples> lo{}, hi{};
    runBurst(exc, 0.23f, lo.data(), kSamples, 0.0f);
    exc.reset();
    runBurst(exc, 1.0f,  hi.data(), kSamples, 0.0f);

    const float cLo = spectralCentroidDFT(lo.data(), kSamples, kSR);
    const float cHi = spectralCentroidDFT(hi.data(), kSamples, kSR);
    INFO("feedback centroid low=" << cLo << " high=" << cHi);
    REQUIRE(cLo > 0.0f);
    REQUIRE(cHi > 0.0f);
    CHECK(cHi / cLo >= 2.0f);
}

TEST_CASE("FeedbackExciter: 1 s output is finite and peak <= 1.0",
          "[membrum][exciter][feedback][finite]")
{
    constexpr double kSR = 44100.0;
    Membrum::FeedbackExciter exc;
    exc.prepare(kSR, 0);
    exc.trigger(1.0f);
    float peak = 0.0f;
    bool  finite = true;
    for (int i = 0; i < static_cast<int>(kSR); ++i)
    {
        const float s = exc.process(0.0f);
        if (!isFiniteSample(s)) { finite = false; break; }
        const float a = s < 0 ? -s : s;
        if (a > peak) peak = a;
    }
    CHECK(finite);
    CHECK(peak <= 1.0f);
}

TEST_CASE("FeedbackExciter: retrigger safety",
          "[membrum][exciter][feedback][retrigger]")
{
    Membrum::FeedbackExciter exc;
    exc.prepare(44100.0, 0);
    exc.trigger(0.8f);
    for (int i = 0; i < 200; ++i) (void)exc.process(0.0f);
    exc.trigger(0.9f);
    float peak = 0.0f;
    bool  finite = true;
    for (int i = 0; i < 4096; ++i)
    {
        const float s = exc.process(0.4f);
        if (!isFiniteSample(s)) { finite = false; break; }
        const float a = s < 0 ? -s : s;
        if (a > peak) peak = a;
    }
    CHECK(finite);
    CHECK(peak <= 1.0f);
}

TEST_CASE("FeedbackExciter: reset is idempotent",
          "[membrum][exciter][feedback][reset]")
{
    Membrum::FeedbackExciter exc;
    exc.prepare(44100.0, 0);
    exc.reset();
    exc.reset();
    CHECK_FALSE(exc.isActive());
}

TEST_CASE("FeedbackExciter: sample-rate change safe",
          "[membrum][exciter][feedback][sample-rate]")
{
    Membrum::FeedbackExciter exc;
    exc.prepare(44100.0, 0);
    exc.prepare(96000.0, 0);
    exc.trigger(0.7f);
    float peak = 0.0f;
    bool  finite = true;
    for (int i = 0; i < 4096; ++i)
    {
        const float s = exc.process(0.2f);
        if (!isFiniteSample(s)) { finite = false; break; }
        const float a = s < 0 ? -s : s;
        if (a > peak) peak = a;
    }
    CHECK(finite);
    CHECK(peak > 1e-6f);
    CHECK(peak <= 1.0f);
}

TEST_CASE("FeedbackExciter: non-zero body feedback sustains longer than zero",
          "[membrum][exciter][feedback][sustain]")
{
    constexpr double kSR = 44100.0;
    constexpr int kTotal = static_cast<int>(kSR * 0.5); // 500 ms
    Membrum::FeedbackExciter excA;
    Membrum::FeedbackExciter excB;
    excA.prepare(kSR, 0);
    excB.prepare(kSR, 0);

    excA.trigger(0.9f); // zero-feedback control
    excB.trigger(0.9f); // non-zero feedback
    float energyA = 0.0f, energyB = 0.0f;
    for (int i = 0; i < kTotal; ++i)
    {
        // Simulate body feedback with a decaying sinusoid (a synthetic
        // "ringing body" at 220 Hz with 300 ms decay).
        const float t = static_cast<float>(i) / static_cast<float>(kSR);
        const float bodySim =
            0.5f * std::sin(2.0f * 3.14159265f * 220.0f * t)
                 * std::exp(-t * 4.0f);

        const float a = excA.process(0.0f);
        const float b = excB.process(bodySim);
        energyA += a * a;
        energyB += b * b;
    }
    INFO("energyA(no fb)=" << energyA << " energyB(with fb)=" << energyB);
    CHECK(energyB > energyA);
}

// Build a non-finite float from its IEEE-754 bit pattern, routed through a
// volatile sink so the optimizer cannot constant-fold it. The VST3 SDK enables
// -ffast-math (-ffinite-math-only) globally; under that flag the compiler
// assumes NaN/Inf never occur, so std::numeric_limits<float>::infinity() /
// quiet_NaN() are UB and get folded to finite garbage (which then sails past
// the production guard and breaks this test). The volatile read forces a real
// non-finite bit pattern to exist at runtime regardless of the FP mode.
static float nonFiniteFromBits(std::uint32_t bits) noexcept
{
    volatile std::uint32_t v = bits;
    const std::uint32_t    read = v;  // volatile load defeats constant folding
    float f = 0.0f;
    std::memcpy(&f, &read, sizeof(f));
    return f;
}

TEST_CASE("FeedbackExciter: non-finite body feedback never reaches the output "
          "(correctness audit Finding 7)",
          "[membrum][exciter][feedback][robustness]")
{
    constexpr double kSR = 44100.0;

    // A single NaN/+-Inf body-feedback sample must not poison the voice: every
    // subsequent output must stay finite. Without the input guard an Inf becomes
    // Inf*0 = NaN inside the RMS energy limiter and propagates for the life of
    // the voice -- and the downstream +/-1 clamp cannot rescue it, because under
    // the SDK's global -ffast-math (-ffinite-math-only) `NaN > 1.0f` folds to
    // false. The guard lives in a -fno-fast-math TU (Membrum::DSP::isNonFinite)
    // precisely so it is not optimised away; we reuse it here to check the
    // OUTPUT, so the assertion itself cannot be folded either.
    //
    // NOTE: this deliberately does NOT compare against process(0.0f). Under
    // -ffast-math the compiler constant-propagates a literal-0 argument and
    // optimises that call site differently from a runtime-zeroed one, so exact
    // bit-equality between the two is unattainable even when the guard is
    // perfect (verified: the comparison diverges on gcc -ffast-math regardless
    // of the guard). Output finiteness is the property that actually matters.
    const float kQuietNaN = nonFiniteFromBits(0x7FC00000u);
    const float kPosInf   = nonFiniteFromBits(0x7F800000u);
    const float kNegInf   = nonFiniteFromBits(0xFF800000u);
    for (float poison : {kQuietNaN, kPosInf, kNegInf})
    {
        Membrum::FeedbackExciter exc;
        exc.prepare(kSR, 0);
        exc.trigger(0.9f);

        for (int i = 0; i < 32; ++i) (void)exc.process(0.5f);

        (void)exc.process(poison);  // the pathological sample

        int nonFiniteOutputs = 0;
        for (int i = 0; i < 4096; ++i)
        {
            const float fb  = (i & 1) ? 0.6f : -0.6f;
            const float out = exc.process(fb);
            if (Membrum::DSP::isNonFinite(out)) ++nonFiniteOutputs;
        }
        CHECK(nonFiniteOutputs == 0);
    }
}

TEST_CASE("FeedbackExciter: peak <= 0 dBFS for extreme bodyFeedback (SC-008)",
          "[membrum][exciter][feedback][stability]")
{
    constexpr double kSR = 44100.0;
    constexpr int kTotal = static_cast<int>(kSR * 0.250);
    Membrum::FeedbackExciter exc;
    exc.prepare(kSR, 0);

    // Test at extreme velocities and pathological body feedback.
    for (float velocity : {0.1f, 0.5f, 1.0f})
    {
        for (float fbDrive : {-1.0f, -0.7f, 0.0f, 0.7f, 1.0f})
        {
            exc.reset();
            exc.trigger(velocity);
            float peak = 0.0f;
            bool  finite = true;
            for (int i = 0; i < kTotal; ++i)
            {
                // Drive with a full-scale square wave — the worst case for a
                // feedback loop (both signs saturate the energy follower).
                const float fb = (i & 64) ? fbDrive : -fbDrive;
                const float s  = exc.process(fb);
                if (!isFiniteSample(s)) { finite = false; break; }
                const float a = s < 0 ? -s : s;
                if (a > peak) peak = a;
            }
            INFO("velocity=" << velocity << " fbDrive=" << fbDrive
                 << " peak=" << peak);
            CHECK(finite);
            CHECK(peak <= 1.0f);
        }
    }
}
