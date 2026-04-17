// ==============================================================================
// NoiseLayer unit tests (Phase 7)
// ==============================================================================
// Verifies:
//  (1) Allocation-free: configure + trigger + processBlock make no heap calls.
//  (2) Envelope triggers and decays -- active after trigger, inert when mix=0.
//  (3) Filter cutoff parameter shifts spectral centroid.
//  (4) Decay parameter lengthens the RMS tail.
//  (5) processBlock output matches processSample sample-by-sample.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "dsp/noise_layer.h"

#include <allocation_detector.h>

#include <cmath>
#include <cstddef>
#include <vector>

using Catch::Approx;

namespace {

float rms(const float* x, std::size_t n) noexcept
{
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) s += static_cast<double>(x[i]) * x[i];
    return static_cast<float>(std::sqrt(s / static_cast<double>(std::max<std::size_t>(1u, n))));
}

// Cheap spectral centroid via sample-delay autocorrelation proxy.
// Higher returned value => higher-frequency content.
float brightness(const float* x, std::size_t n) noexcept
{
    double num = 0.0, den = 0.0;
    for (std::size_t i = 1; i < n; ++i)
    {
        const double d = static_cast<double>(x[i] - x[i - 1]);
        num += d * d;
        den += static_cast<double>(x[i]) * x[i];
    }
    if (den <= 0.0) return 0.0f;
    return static_cast<float>(num / den);
}

} // namespace

TEST_CASE("NoiseLayer: allocation-free after prepare", "[membrum][dsp][noise_layer][alloc]")
{
    Membrum::NoiseLayer layer;
    layer.prepare(48000.0, /*voiceId*/ 0);

    Membrum::NoiseLayerParams p{};
    p.mix       = 0.5f;
    p.cutoff    = 0.5f;
    p.resonance = 0.3f;
    p.decay     = 0.3f;
    p.color     = 0.5f;

    {
        TestHelpers::AllocationScope scope;
        layer.configure(p);
        layer.trigger(1.0f);
        std::vector<float> out(1024, 0.0f);
        layer.processBlock(out.data(), static_cast<int>(out.size()));
        CHECK(scope.getAllocationCount() == 0);
    }
}

TEST_CASE("NoiseLayer: mix=0 produces silence, mix>0 produces energy",
          "[membrum][dsp][noise_layer]")
{
    constexpr double kSR = 48000.0;
    constexpr int    kN  = 2048;
    Membrum::NoiseLayer layer;
    layer.prepare(kSR, 0u);

    Membrum::NoiseLayerParams p{};
    p.mix = 0.0f; p.cutoff = 0.5f; p.resonance = 0.3f; p.decay = 0.5f; p.color = 0.5f;
    layer.configure(p);
    layer.trigger(1.0f);

    std::vector<float> silent(kN, 1.0f);
    layer.processBlock(silent.data(), kN);
    CHECK(rms(silent.data(), kN) == 0.0f);

    p.mix = 0.8f;
    layer.configure(p);
    layer.trigger(1.0f);
    std::vector<float> active(kN, 0.0f);
    layer.processBlock(active.data(), kN);
    CHECK(rms(active.data(), kN) > 0.0f);
}

TEST_CASE("NoiseLayer: higher cutoff increases brightness",
          "[membrum][dsp][noise_layer]")
{
    constexpr double kSR = 48000.0;
    constexpr int    kN  = 4096;
    Membrum::NoiseLayer layer;
    layer.prepare(kSR, 7u);

    Membrum::NoiseLayerParams p{};
    p.mix = 0.9f; p.resonance = 0.3f; p.decay = 0.9f; p.color = 0.5f;

    p.cutoff = 0.15f;
    layer.configure(p);
    layer.trigger(1.0f);
    std::vector<float> low(kN, 0.0f);
    layer.processBlock(low.data(), kN);
    const float b_low = brightness(low.data() + 128, kN - 128);

    layer.reset();
    p.cutoff = 0.9f;
    layer.configure(p);
    layer.trigger(1.0f);
    std::vector<float> high(kN, 0.0f);
    layer.processBlock(high.data(), kN);
    const float b_high = brightness(high.data() + 128, kN - 128);

    INFO("brightness low=" << b_low << " high=" << b_high);
    CHECK(b_high > b_low);
}

TEST_CASE("NoiseLayer: longer decay produces longer tail",
          "[membrum][dsp][noise_layer]")
{
    constexpr double kSR = 48000.0;
    constexpr int    kN  = 8192;
    Membrum::NoiseLayer layer;
    layer.prepare(kSR, 1u);

    Membrum::NoiseLayerParams p{};
    p.mix = 0.9f; p.cutoff = 0.5f; p.resonance = 0.3f; p.color = 0.5f;

    p.decay = 0.05f;
    layer.configure(p);
    layer.trigger(1.0f);
    std::vector<float> shortTail(kN, 0.0f);
    layer.processBlock(shortTail.data(), kN);
    const int tailStart = kN / 2;
    const float rms_short = rms(shortTail.data() + tailStart, kN - tailStart);

    layer.reset();
    p.decay = 0.9f;
    layer.configure(p);
    layer.trigger(1.0f);
    std::vector<float> longTail(kN, 0.0f);
    layer.processBlock(longTail.data(), kN);
    const float rms_long = rms(longTail.data() + tailStart, kN - tailStart);

    INFO("rms tail short=" << rms_short << " long=" << rms_long);
    CHECK(rms_long > rms_short * 5.0f);
}

TEST_CASE("NoiseLayer: processBlock equals processSample",
          "[membrum][dsp][noise_layer]")
{
    constexpr double kSR = 44100.0;
    constexpr int    kN  = 512;
    Membrum::NoiseLayerParams p{};
    p.mix = 0.75f; p.cutoff = 0.4f; p.resonance = 0.2f; p.decay = 0.3f; p.color = 0.5f;

    Membrum::NoiseLayer a; a.prepare(kSR, 42u); a.configure(p); a.trigger(1.0f);
    Membrum::NoiseLayer b; b.prepare(kSR, 42u); b.configure(p); b.trigger(1.0f);

    std::vector<float> out(kN, 0.0f);
    a.processBlock(out.data(), kN);

    for (int i = 0; i < kN; ++i)
    {
        const float s = b.processSample();
        REQUIRE(s == Approx(out[static_cast<std::size_t>(i)]).margin(1e-6f));
    }
}
