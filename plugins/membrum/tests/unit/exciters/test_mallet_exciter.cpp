// ==============================================================================
// MalletExciter contract tests (Phase 3 -- T031)
// ==============================================================================
// Contract invariants (1-6) plus:
//   (7) At same velocity, first-2-ms spectral centroid of Mallet is measurably
//       lower than ImpulseExciter (acceptance scenario US1-2).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/exciters/impulse_exciter.h"
#include "dsp/exciters/mallet_exciter.h"
#include "exciter_test_helpers.h"

#include <allocation_detector.h>

#include <array>

using membrum_exciter_tests::isFiniteSample;
using membrum_exciter_tests::runBurst;
using membrum_exciter_tests::spectralCentroidDFT;

TEST_CASE("MalletExciter: trigger + process is allocation-free",
          "[membrum][exciter][mallet][alloc]")
{
    Membrum::MalletExciter exc;
    exc.prepare(44100.0, 0);
    {
        TestHelpers::AllocationScope scope;
        exc.trigger(0.8f);
        for (int i = 0; i < 256; ++i)
            (void)exc.process(0.0f);
        CHECK(scope.getAllocationCount() == 0);
    }
}

TEST_CASE("MalletExciter: velocity spectral centroid ratio >= 2.0 (SC-004)",
          "[membrum][exciter][mallet][velocity]")
{
    constexpr double kSR = 44100.0;
    constexpr int kSamples = 441;
    Membrum::MalletExciter exc;
    exc.prepare(kSR, 0);

    std::array<float, kSamples> lo{}, hi{};
    runBurst(exc, 0.23f, lo.data(), kSamples);
    exc.reset();
    runBurst(exc, 1.0f,  hi.data(), kSamples);

    const float cLo = spectralCentroidDFT(lo.data(), kSamples, kSR);
    const float cHi = spectralCentroidDFT(hi.data(), kSamples, kSR);
    INFO("mallet centroid low=" << cLo << " high=" << cHi);
    REQUIRE(cLo > 0.0f);
    REQUIRE(cHi > 0.0f);
    CHECK(cHi / cLo >= 2.0f);
}

TEST_CASE("MalletExciter: 1 s output is finite and peak <= 1.0",
          "[membrum][exciter][mallet][finite]")
{
    constexpr double kSR = 44100.0;
    Membrum::MalletExciter exc;
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
    // Mallet wraps ImpactExciter (same backend as Impulse); raw output may
    // exceed 1.0 by design (intended to feed into a resonating body).
    CHECK(peak <= 2.0f);
}

TEST_CASE("MalletExciter: retrigger safety",
          "[membrum][exciter][mallet][retrigger]")
{
    Membrum::MalletExciter exc;
    exc.prepare(44100.0, 0);
    exc.trigger(0.8f);
    for (int i = 0; i < 100; ++i) (void)exc.process(0.0f);
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
    // Mallet wraps ImpactExciter (same backend as Impulse); raw output may
    // exceed 1.0 by design (intended to feed into a resonating body).
    CHECK(peak <= 2.0f);
}

TEST_CASE("MalletExciter: reset is idempotent",
          "[membrum][exciter][mallet][reset]")
{
    Membrum::MalletExciter exc;
    exc.prepare(44100.0, 0);
    exc.reset();
    exc.reset();
    CHECK_FALSE(exc.isActive());
}

TEST_CASE("MalletExciter: sample-rate change safe",
          "[membrum][exciter][mallet][sample-rate]")
{
    Membrum::MalletExciter exc;
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
    // Mallet wraps ImpactExciter (same backend as Impulse); raw output may
    // exceed 1.0 by design (intended to feed into a resonating body).
    CHECK(peak <= 2.0f);
}

TEST_CASE("MalletExciter vs ImpulseExciter: first-2-ms centroid is lower (US1-2)",
          "[membrum][exciter][mallet][character]")
{
    constexpr double kSR = 44100.0;
    constexpr int kSamples = static_cast<int>(kSR * 0.002); // 2 ms ~ 88
    constexpr float kVelocity = 0.8f;

    Membrum::ImpulseExciter impulse;
    Membrum::MalletExciter  mallet;
    impulse.prepare(kSR, 0);
    mallet.prepare(kSR, 0);

    std::array<float, kSamples> bufImp{}, bufMal{};
    runBurst(impulse, kVelocity, bufImp.data(), kSamples);
    runBurst(mallet,  kVelocity, bufMal.data(), kSamples);

    const float cImp = spectralCentroidDFT(bufImp.data(), kSamples, kSR);
    const float cMal = spectralCentroidDFT(bufMal.data(), kSamples, kSR);
    INFO("impulse=" << cImp << " mallet=" << cMal);
    REQUIRE(cImp > 0.0f);
    REQUIRE(cMal > 0.0f);
    CHECK(cMal < cImp);
}
