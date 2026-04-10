// ==============================================================================
// NoiseBurstExciter contract tests (Phase 3 -- T032)
// ==============================================================================
// Contract invariants (1-6) plus:
//   (7) First-20-ms spectral centroid > 2x Impulse centroid at same velocity
//       (acceptance scenario US1-3).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/exciters/impulse_exciter.h"
#include "dsp/exciters/noise_burst_exciter.h"
#include "exciter_test_helpers.h"

#include <allocation_detector.h>

#include <array>

using membrum_exciter_tests::isFiniteSample;
using membrum_exciter_tests::runBurst;
using membrum_exciter_tests::spectralCentroidDFT;

TEST_CASE("NoiseBurstExciter: trigger + process is allocation-free",
          "[membrum][exciter][noiseburst][alloc]")
{
    Membrum::NoiseBurstExciter exc;
    exc.prepare(44100.0, 0);
    {
        TestHelpers::AllocationScope scope;
        exc.trigger(0.8f);
        for (int i = 0; i < 256; ++i)
            (void)exc.process(0.0f);
        CHECK(scope.getAllocationCount() == 0);
    }
}

TEST_CASE("NoiseBurstExciter: velocity spectral centroid ratio >= 2.0 (SC-004)",
          "[membrum][exciter][noiseburst][velocity]")
{
    constexpr double kSR = 44100.0;
    constexpr int kSamples = 441; // 10 ms
    Membrum::NoiseBurstExciter exc;
    exc.prepare(kSR, 0);

    std::array<float, kSamples> lo{}, hi{};
    runBurst(exc, 0.23f, lo.data(), kSamples);
    exc.reset();
    runBurst(exc, 1.0f,  hi.data(), kSamples);

    const float cLo = spectralCentroidDFT(lo.data(), kSamples, kSR);
    const float cHi = spectralCentroidDFT(hi.data(), kSamples, kSR);
    INFO("noiseburst centroid low=" << cLo << " high=" << cHi);
    REQUIRE(cLo > 0.0f);
    REQUIRE(cHi > 0.0f);
    CHECK(cHi / cLo >= 2.0f);
}

TEST_CASE("NoiseBurstExciter: 1 s output is finite and peak <= 1.0",
          "[membrum][exciter][noiseburst][finite]")
{
    constexpr double kSR = 44100.0;
    Membrum::NoiseBurstExciter exc;
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

TEST_CASE("NoiseBurstExciter: retrigger safety",
          "[membrum][exciter][noiseburst][retrigger]")
{
    Membrum::NoiseBurstExciter exc;
    exc.prepare(44100.0, 0);
    exc.trigger(0.8f);
    for (int i = 0; i < 50; ++i) (void)exc.process(0.0f);
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

TEST_CASE("NoiseBurstExciter: reset is idempotent",
          "[membrum][exciter][noiseburst][reset]")
{
    Membrum::NoiseBurstExciter exc;
    exc.prepare(44100.0, 0);
    exc.reset();
    exc.reset();
    CHECK_FALSE(exc.isActive());
}

TEST_CASE("NoiseBurstExciter: sample-rate change safe",
          "[membrum][exciter][noiseburst][sample-rate]")
{
    Membrum::NoiseBurstExciter exc;
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

TEST_CASE("NoiseBurstExciter vs Impulse: first-20-ms centroid > 2x (US1-3)",
          "[membrum][exciter][noiseburst][character]")
{
    constexpr double kSR = 44100.0;
    constexpr int kSamples = static_cast<int>(kSR * 0.020); // 20 ms
    constexpr float kVelocity = 0.8f;

    Membrum::ImpulseExciter   impulse;
    Membrum::NoiseBurstExciter nb;
    impulse.prepare(kSR, 0);
    nb.prepare(kSR, 0);

    std::array<float, kSamples> bufImp{}, bufNb{};
    runBurst(impulse, kVelocity, bufImp.data(), kSamples);
    runBurst(nb,      kVelocity, bufNb.data(),  kSamples);

    const float cImp = spectralCentroidDFT(bufImp.data(), kSamples, kSR);
    const float cNb  = spectralCentroidDFT(bufNb.data(),  kSamples, kSR);
    INFO("impulse=" << cImp << " noiseburst=" << cNb);
    REQUIRE(cImp > 0.0f);
    REQUIRE(cNb  > 0.0f);
    CHECK(cNb > 2.0f * cImp);
}
