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
#include "exciter_test_helpers.h"

#include <allocation_detector.h>

#include <array>
#include <cmath>

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
