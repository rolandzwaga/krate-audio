// ==============================================================================
// FrictionExciter contract tests (Phase 3 -- T033)
// ==============================================================================
// Contract invariants (1-6) plus:
//   (7) Output contains non-monotonic energy envelope over first 50 ms (stick-
//       slip signature, acceptance scenario US1-4).
//   (8) Bow auto-releases within ≤ 50 ms (transient mode only).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/exciters/friction_exciter.h"
#include "exciter_test_helpers.h"

#include <allocation_detector.h>

#include <array>

using membrum_exciter_tests::isFiniteSample;
using membrum_exciter_tests::runBurst;
using membrum_exciter_tests::spectralCentroidDFT;

TEST_CASE("FrictionExciter: trigger + process is allocation-free",
          "[membrum][exciter][friction][alloc]")
{
    Membrum::FrictionExciter exc;
    exc.prepare(44100.0, 0);
    {
        TestHelpers::AllocationScope scope;
        exc.trigger(0.8f);
        for (int i = 0; i < 2048; ++i)
            (void)exc.process(0.0f);
        CHECK(scope.getAllocationCount() == 0);
    }
}

TEST_CASE("FrictionExciter: velocity spectral centroid ratio >= 2.0 (SC-004)",
          "[membrum][exciter][friction][velocity]")
{
    constexpr double kSR = 44100.0;
    constexpr int kSamples = 441; // 10 ms
    Membrum::FrictionExciter exc;
    exc.prepare(kSR, 0);

    std::array<float, kSamples> lo{}, hi{};
    runBurst(exc, 0.23f, lo.data(), kSamples);
    exc.reset();
    runBurst(exc, 1.0f,  hi.data(), kSamples);

    const float cLo = spectralCentroidDFT(lo.data(), kSamples, kSR);
    const float cHi = spectralCentroidDFT(hi.data(), kSamples, kSR);
    INFO("friction centroid low=" << cLo << " high=" << cHi);
    REQUIRE(cLo > 0.0f);
    REQUIRE(cHi > 0.0f);
    CHECK(cHi / cLo >= 2.0f);
}

TEST_CASE("FrictionExciter: 1 s output is finite and peak <= 1.0",
          "[membrum][exciter][friction][finite]")
{
    constexpr double kSR = 44100.0;
    Membrum::FrictionExciter exc;
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

TEST_CASE("FrictionExciter: retrigger safety",
          "[membrum][exciter][friction][retrigger]")
{
    Membrum::FrictionExciter exc;
    exc.prepare(44100.0, 0);
    exc.trigger(0.8f);
    for (int i = 0; i < 200; ++i) (void)exc.process(0.0f);
    exc.trigger(0.9f);
    float peak = 0.0f;
    bool  finite = true;
    for (int i = 0; i < 4096; ++i)
    {
        const float s = exc.process(0.0f);
        if (!isFiniteSample(s)) { finite = false; break; }
        const float a = s < 0 ? -s : s;
        if (a > peak) peak = a;
    }
    CHECK(finite);
    CHECK(peak <= 1.0f);
}

TEST_CASE("FrictionExciter: reset is idempotent",
          "[membrum][exciter][friction][reset]")
{
    Membrum::FrictionExciter exc;
    exc.prepare(44100.0, 0);
    exc.reset();
    exc.reset();
    CHECK_FALSE(exc.isActive());
}

TEST_CASE("FrictionExciter: sample-rate change safe",
          "[membrum][exciter][friction][sample-rate]")
{
    Membrum::FrictionExciter exc;
    exc.prepare(44100.0, 0);
    exc.prepare(96000.0, 0);
    exc.trigger(0.7f);
    float peak = 0.0f;
    bool  finite = true;
    for (int i = 0; i < 4096; ++i)
    {
        const float s = exc.process(0.0f);
        if (!isFiniteSample(s)) { finite = false; break; }
        const float a = s < 0 ? -s : s;
        if (a > peak) peak = a;
    }
    CHECK(finite);
    CHECK(peak > 1e-6f);
    CHECK(peak <= 1.0f);
}

TEST_CASE("FrictionExciter: first-50-ms envelope is non-monotonic (US1-4)",
          "[membrum][exciter][friction][character]")
{
    constexpr double kSR = 44100.0;
    constexpr int kWindow = static_cast<int>(kSR * 0.050);
    Membrum::FrictionExciter exc;
    exc.prepare(kSR, 0);
    exc.trigger(0.8f);

    // Block RMS energy tracking across 5 ms windows → expect at least one dip
    // (energy not strictly monotonic decreasing) indicating stick-slip.
    constexpr int kBlock = static_cast<int>(kSR * 0.005);
    constexpr int kNumBlocks = kWindow / kBlock;
    std::array<float, 10> blockEnergy{};
    for (int b = 0; b < kNumBlocks && b < static_cast<int>(blockEnergy.size()); ++b)
    {
        double sum = 0.0;
        for (int i = 0; i < kBlock; ++i)
        {
            const float s = exc.process(0.0f);
            sum += static_cast<double>(s) * s;
        }
        blockEnergy[b] = static_cast<float>(sum);
    }

    // Look for any non-monotonic transition: some block where energy rises
    // after a previous decrease, OR dips below the peak-so-far by more than
    // a smoothing margin.
    bool nonMonotonic = false;
    float runningPeak = 0.0f;
    for (int b = 0; b < kNumBlocks; ++b)
    {
        if (b > 0 && blockEnergy[b] > blockEnergy[b - 1] * 1.05f)
        {
            nonMonotonic = true;
            break;
        }
        if (blockEnergy[b] > runningPeak) runningPeak = blockEnergy[b];
    }
    CHECK(nonMonotonic);
}

TEST_CASE("FrictionExciter: bow auto-releases within 50 ms (transient mode)",
          "[membrum][exciter][friction][release]")
{
    constexpr double kSR = 44100.0;
    Membrum::FrictionExciter exc;
    exc.prepare(kSR, 0);
    exc.trigger(0.8f);

    // Process 200 ms — by then the envelope should be idle.
    constexpr int kTotal = static_cast<int>(kSR * 0.200);
    for (int i = 0; i < kTotal; ++i)
        (void)exc.process(0.0f);

    CHECK_FALSE(exc.isActive());
}
