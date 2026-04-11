// ==============================================================================
// FMImpulseExciter contract tests (Phase 3 -- T034)
// ==============================================================================
// Contract invariants (1-6) plus:
//   (7) First-50-ms output contains inharmonic sidebands at carrier:modulator
//       ratio 1:1.4 (acceptance scenario US1-5).
//   (8) Modulation index envelope decays faster than amplitude envelope
//       (spectral centroid early → centroid later ratio).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/exciters/fm_impulse_exciter.h"
#include "exciter_test_helpers.h"

#include <allocation_detector.h>

#include <array>
#include <cmath>

using membrum_exciter_tests::isFiniteSample;
using membrum_exciter_tests::runBurst;
using membrum_exciter_tests::spectralCentroidDFT;

TEST_CASE("FMImpulseExciter: trigger + process is allocation-free",
          "[membrum][exciter][fmimpulse][alloc]")
{
    Membrum::FMImpulseExciter exc;
    exc.prepare(44100.0, 0);
    {
        TestHelpers::AllocationScope scope;
        exc.trigger(0.8f);
        for (int i = 0; i < 2048; ++i)
            (void)exc.process(0.0f);
        CHECK(scope.getAllocationCount() == 0);
    }
}

TEST_CASE("FMImpulseExciter: velocity spectral centroid ratio >= 2.0 (SC-004)",
          "[membrum][exciter][fmimpulse][velocity]")
{
    constexpr double kSR = 44100.0;
    constexpr int kSamples = 441; // 10 ms
    Membrum::FMImpulseExciter exc;
    exc.prepare(kSR, 0);

    std::array<float, kSamples> lo{}, hi{};
    runBurst(exc, 0.23f, lo.data(), kSamples);
    exc.reset();
    runBurst(exc, 1.0f,  hi.data(), kSamples);

    const float cLo = spectralCentroidDFT(lo.data(), kSamples, kSR);
    const float cHi = spectralCentroidDFT(hi.data(), kSamples, kSR);
    INFO("fmimpulse centroid low=" << cLo << " high=" << cHi);
    REQUIRE(cLo > 0.0f);
    REQUIRE(cHi > 0.0f);
    CHECK(cHi / cLo >= 2.0f);
}

TEST_CASE("FMImpulseExciter: 1 s output is finite and peak <= 1.0",
          "[membrum][exciter][fmimpulse][finite]")
{
    constexpr double kSR = 44100.0;
    Membrum::FMImpulseExciter exc;
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

TEST_CASE("FMImpulseExciter: retrigger safety",
          "[membrum][exciter][fmimpulse][retrigger]")
{
    Membrum::FMImpulseExciter exc;
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

TEST_CASE("FMImpulseExciter: reset is idempotent",
          "[membrum][exciter][fmimpulse][reset]")
{
    Membrum::FMImpulseExciter exc;
    exc.prepare(44100.0, 0);
    exc.reset();
    exc.reset();
    CHECK_FALSE(exc.isActive());
}

TEST_CASE("FMImpulseExciter: sample-rate change safe",
          "[membrum][exciter][fmimpulse][sample-rate]")
{
    Membrum::FMImpulseExciter exc;
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

TEST_CASE("FMImpulseExciter: first-50-ms has energy at 1:1.4 sideband (US1-5)",
          "[membrum][exciter][fmimpulse][character]")
{
    constexpr double kSR = 44100.0;
    constexpr int kSamples = static_cast<int>(kSR * 0.050);
    Membrum::FMImpulseExciter exc;
    exc.prepare(kSR, 0);
    std::array<float, kSamples> buf{};
    runBurst(exc, 1.0f, buf.data(), kSamples);

    // At velocity 1.0, FMImpulseExciter uses base freq = 2500 Hz, carrier
    // ratio 1.0, modulator ratio 1.4 → modulator at 3500 Hz. In FM the
    // modulator does not appear directly in the output; instead it generates
    // sidebands around the carrier at f_c ± k*f_m:
    //     ..., 2500 - 3500 = −1000 (aliases to +1000),
    //          2500 + 3500 = 6000,
    //          2500 + 7000 = 9500, ...
    // Verify energy at the carrier (2500 Hz) and the first upper 1:1.4
    // sideband (6000 Hz) both dominate a far control band — the inharmonic
    // signature of the 1:1.4 Chowning bell (FR-014 / US1-5).
    auto goertzel = [&](float freq) noexcept {
        const float w = 2.0f * 3.14159265f * freq / static_cast<float>(kSR);
        float re = 0.0f, im = 0.0f;
        for (int n = 0; n < kSamples; ++n)
        {
            re += buf[n] * std::cos(w * n);
            im -= buf[n] * std::sin(w * n);
        }
        return std::sqrt(re * re + im * im);
    };
    const float magCarrier  = goertzel(2500.0f);
    const float magSideband = goertzel(6000.0f); // f_c + f_m at 1:1.4
    const float magControl  = goertzel(100.0f);  // far sub-band
    INFO("magCarrier="  << magCarrier
         << " magSideband=" << magSideband
         << " magControl="  << magControl);
    REQUIRE(magCarrier  > magControl * 1.5f);
    REQUIRE(magSideband > magControl * 1.5f);
}

TEST_CASE("FMImpulseExciter: modIndex envelope decays faster than amp envelope",
          "[membrum][exciter][fmimpulse][envelopes]")
{
    constexpr double kSR = 44100.0;
    Membrum::FMImpulseExciter exc;
    exc.prepare(kSR, 0);
    exc.trigger(1.0f);

    // Measure spectral centroid in an early window (0-10 ms) vs late window
    // (40-50 ms). If modIndex decays faster than amplitude, the late window
    // should show a LOWER centroid (fewer sidebands survive while the
    // carrier is still audible).
    constexpr int kWindow = static_cast<int>(kSR * 0.010);
    constexpr int kGap    = static_cast<int>(kSR * 0.030);
    std::array<float, kWindow> early{};
    std::array<float, kWindow> late{};
    for (int i = 0; i < kWindow; ++i) early[i] = exc.process(0.0f);
    for (int i = 0; i < kGap; ++i)    (void)exc.process(0.0f);
    for (int i = 0; i < kWindow; ++i) late[i]  = exc.process(0.0f);

    const float cEarly = spectralCentroidDFT(early.data(), kWindow, kSR);
    const float cLate  = spectralCentroidDFT(late.data(),  kWindow, kSR);
    INFO("early centroid=" << cEarly << " late centroid=" << cLate);
    REQUIRE(cEarly > 0.0f);
    REQUIRE(cLate  > 0.0f);
    CHECK(cLate < cEarly);
}
