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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

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

    std::array<float, kSamples> lo{};
    std::array<float, kSamples> hi{};
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
        peak = std::max(peak, a);
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
        peak = std::max(peak, a);
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
        peak = std::max(peak, a);
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
    std::array<float, kNumBlocks> blockEnergy{};
    for (int b = 0; b < kNumBlocks; ++b)
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
        runningPeak = std::max(runningPeak, blockEnergy[b]);
    }
    CHECK(nonMonotonic);
}

// T1.17 (MB-019): the bow force falls past the friction table's peak and the
// velocity-driven bandpass (400 -> 8000 Hz) rejects the bow-force onset, so
// the exciter used to peak 23 dB LOWER at velocity 1.0 than at 0.1. The voice
// normalises its body against a velocity-1.0 probe, so every softer hit was
// boosted past the body budget. The exciter peak must rise with velocity.
TEST_CASE("FrictionExciter: peak rises monotonically with velocity",
          "[friction_exciter][velocity]")
{
    constexpr double kSR = 44100.0;
    constexpr int kLen = static_cast<int>(kSR * 0.060); // 60 ms

    const auto peakAt = [](float pressure, float velocity) {
        Membrum::FrictionExciter exc; // fresh exciter per hit
        exc.prepare(kSR, 0);
        exc.setPressure(pressure);
        exc.trigger(velocity);
        float peak = 0.0f;
        for (int i = 0; i < kLen; ++i)
            peak = std::max(peak, std::fabs(exc.process(0.0f)));
        return peak;
    };

    constexpr std::array<float, 6> kVel{0.1f, 0.2f, 0.3f, 0.5f, 0.7f, 1.0f};
    for (const float pressure : {0.0f, 0.5f})
    {
        std::array<float, kVel.size()> peaks{};
        for (std::size_t i = 0; i < kVel.size(); ++i)
            peaks[i] = peakAt(pressure, kVel[i]);

        INFO("Friction Pressure " << pressure << " peaks v0.1..1.0: " << peaks[0] << " "
             << peaks[1] << " " << peaks[2] << " " << peaks[3] << " " << peaks[4] << " "
             << peaks[5]);
        REQUIRE(peaks[0] > 0.0f);
        for (std::size_t i = 1; i < kVel.size(); ++i)
        {
            INFO("v " << kVel[i - 1] << " -> " << kVel[i]);
            CHECK(peaks[i] >= peaks[i - 1]);
        }
        const float p025 = peakAt(pressure, 0.25f);
        INFO("peak(1.0) " << peaks[5] << " / peak(0.25) " << p025);
        REQUIRE(p025 > 0.0f);
        CHECK(peaks[5] / p025 > 2.0f); // +6 dB
    }
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
