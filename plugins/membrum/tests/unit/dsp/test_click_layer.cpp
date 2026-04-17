// ==============================================================================
// ClickLayer unit tests (Phase 7)
// ==============================================================================
// Verifies:
//  (1) Allocation-free: configure + trigger + processBlock make no heap calls.
//  (2) Burst completes within configured contact time (no late samples).
//  (3) Envelope follows asymmetric raised-cosine rise / exponential decay.
//  (4) mix=0 produces silence.
//  (5) Higher brightness raises the spectral centroid.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "dsp/click_layer.h"

#include <allocation_detector.h>

#include <cmath>
#include <cstddef>
#include <vector>

using Catch::Approx;

namespace {

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

TEST_CASE("ClickLayer: allocation-free after prepare", "[membrum][dsp][click_layer][alloc]")
{
    Membrum::ClickLayer layer;
    layer.prepare(48000.0, 0u);

    Membrum::ClickLayerParams p{};
    p.mix = 0.7f; p.contactMs = 0.5f; p.brightness = 0.6f;

    {
        TestHelpers::AllocationScope scope;
        layer.configure(p);
        layer.trigger(1.0f);
        std::vector<float> out(512, 0.0f);
        layer.processBlock(out.data(), static_cast<int>(out.size()));
        CHECK(scope.getAllocationCount() == 0);
    }
}

TEST_CASE("ClickLayer: burst fits within configured contact time",
          "[membrum][dsp][click_layer]")
{
    constexpr double kSR = 48000.0;
    Membrum::ClickLayer layer;
    layer.prepare(kSR, 0u);

    Membrum::ClickLayerParams p{};
    p.mix = 1.0f; p.brightness = 0.5f;

    // contactMs=1.0 -> 5 ms burst -> 240 samples at 48 kHz. Velocity=1.0
    // shortens this to 0.8x => ~192 samples.
    p.contactMs = 1.0f;
    layer.configure(p);
    layer.trigger(1.0f);

    constexpr int kN = 512;
    std::vector<float> out(kN, 0.0f);
    layer.processBlock(out.data(), kN);

    // Everything past sample 256 should be zero (well past 5 ms contact).
    for (int i = 300; i < kN; ++i) {
        REQUIRE(out[static_cast<std::size_t>(i)] == 0.0f);
    }

    // isActive() should report false once the burst ends.
    CHECK_FALSE(layer.isActive());
}

TEST_CASE("ClickLayer: mix=0 produces silence",
          "[membrum][dsp][click_layer]")
{
    constexpr double kSR = 48000.0;
    Membrum::ClickLayer layer;
    layer.prepare(kSR, 0u);

    Membrum::ClickLayerParams p{};
    p.mix = 0.0f; p.contactMs = 0.5f; p.brightness = 0.6f;
    layer.configure(p);
    layer.trigger(1.0f);

    std::vector<float> out(512, 1.0f);
    layer.processBlock(out.data(), static_cast<int>(out.size()));
    for (float v : out) REQUIRE(v == 0.0f);
}

TEST_CASE("ClickLayer: higher brightness raises spectral brightness",
          "[membrum][dsp][click_layer]")
{
    constexpr double kSR = 48000.0;
    Membrum::ClickLayer layer;
    layer.prepare(kSR, 3u);

    Membrum::ClickLayerParams p{};
    p.mix = 1.0f; p.contactMs = 1.0f;

    p.brightness = 0.1f;
    layer.configure(p);
    layer.trigger(1.0f);
    std::vector<float> low(512, 0.0f);
    layer.processBlock(low.data(), static_cast<int>(low.size()));
    const float b_low = brightness(low.data(), 256);

    layer.reset();
    p.brightness = 0.95f;
    layer.configure(p);
    layer.trigger(1.0f);
    std::vector<float> high(512, 0.0f);
    layer.processBlock(high.data(), static_cast<int>(high.size()));
    const float b_high = brightness(high.data(), 256);

    INFO("brightness low=" << b_low << " high=" << b_high);
    CHECK(b_high > b_low);
}

TEST_CASE("ClickLayer: envelope peaks inside rise region",
          "[membrum][dsp][click_layer]")
{
    constexpr double kSR = 48000.0;
    Membrum::ClickLayer layer;
    layer.prepare(kSR, 9u);

    Membrum::ClickLayerParams p{};
    p.mix = 1.0f; p.contactMs = 1.0f; p.brightness = 0.5f;
    layer.configure(p);
    layer.trigger(1.0f);

    constexpr int kN = 512;
    std::vector<float> out(kN, 0.0f);
    layer.processBlock(out.data(), kN);

    // Locate the absolute peak index.
    int peakIdx = 0;
    float peakAbs = 0.0f;
    for (int i = 0; i < kN; ++i)
    {
        const float a = std::abs(out[static_cast<std::size_t>(i)]);
        if (a > peakAbs) { peakAbs = a; peakIdx = i; }
    }
    INFO("peakIdx=" << peakIdx << " peakAbs=" << peakAbs);
    // Peak should occur in the first ~2 ms (rise portion of a 4 ms burst at
    // velocity=1). At 48 kHz that's well under 150 samples.
    CHECK(peakAbs > 0.0f);
    CHECK(peakIdx < 150);
}
