// ==============================================================================
// Layer 2: Processor Tests - Random Source
// ==============================================================================
// Tests for the RandomSource modulation source.
//
// Reference: specs/008-modulation-system/spec.md (FR-021 to FR-025, SC-016)
// ==============================================================================

#include <krate/dsp/processors/random_source.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Output Range Tests (FR-025)
// =============================================================================

TEST_CASE("RandomSource output stays in [-1, +1]", "[processors][random_source]") {
    RandomSource src;
    src.prepare(44100.0);
    src.setRate(10.0f);  // Fast rate for many transitions

    for (int i = 0; i < 100000; ++i) {
        src.process();
        float val = src.getCurrentValue();
        REQUIRE(val >= -1.0f);
        REQUIRE(val <= 1.0f);
    }
}

// =============================================================================
// Rate Tests
// =============================================================================

TEST_CASE("RandomSource rate controls change frequency", "[processors][random_source]") {
    // Count how many distinct value changes occur at different rates
    auto countChanges = [](float rate, int numSamples) {
        RandomSource src;
        src.prepare(44100.0);
        src.setRate(rate);
        src.setSmoothness(0.0f);  // No smoothing for clear transitions

        int changes = 0;
        float prev = src.getCurrentValue();
        for (int i = 0; i < numSamples; ++i) {
            src.process();
            float val = src.getCurrentValue();
            if (std::abs(val - prev) > 0.01f) {
                ++changes;
                prev = val;
            }
        }
        return changes;
    };

    int slowChanges = countChanges(1.0f, 44100);
    int fastChanges = countChanges(20.0f, 44100);

    // Faster rate should produce more changes
    REQUIRE(fastChanges > slowChanges);
}

// =============================================================================
// Smoothness Tests
// =============================================================================

TEST_CASE("RandomSource smoothness smooths transitions", "[processors][random_source]") {
    // With 0% smoothness, transitions should be sharp (large jumps)
    // With high smoothness, transitions should be gradual
    auto measureMaxJump = [](float smoothness) {
        RandomSource src;
        src.prepare(44100.0);
        src.setRate(10.0f);
        src.setSmoothness(smoothness);

        float maxJump = 0.0f;
        float prev = src.getCurrentValue();
        for (int i = 0; i < 44100; ++i) {
            src.process();
            float val = src.getCurrentValue();
            float jump = std::abs(val - prev);
            maxJump = std::max(maxJump, jump);
            prev = val;
        }
        return maxJump;
    };

    float sharpMaxJump = measureMaxJump(0.0f);
    float smoothMaxJump = measureMaxJump(0.8f);

    // Smooth should have smaller max jumps
    REQUIRE(smoothMaxJump < sharpMaxJump);
}

// =============================================================================
// Interface Tests
// =============================================================================

TEST_CASE("RandomSource implements ModulationSource interface", "[processors][random_source]") {
    RandomSource src;
    src.prepare(44100.0);

    auto range = src.getSourceRange();
    REQUIRE(range.first == Approx(-1.0f));
    REQUIRE(range.second == Approx(1.0f));
}

// =============================================================================
// Statistical Distribution Test (SC-016)
// =============================================================================

TEST_CASE("RandomSource distribution is approximately uniform", "[processors][random_source][sc016]") {
    RandomSource src;
    src.prepare(44100.0);
    src.setRate(50.0f);
    src.setSmoothness(0.0f);

    // Collect 10000 samples in 4 bins
    constexpr int kNumBins = 4;
    std::array<int, kNumBins> bins = {};
    constexpr int kNumSamples = 441000;  // 10 seconds at 44.1kHz

    for (int i = 0; i < kNumSamples; ++i) {
        src.process();
    }

    // Sample at trigger points (rate=50Hz means ~882 samples per trigger)
    constexpr int kNumValues = 10000;
    for (int i = 0; i < kNumValues; ++i) {
        // Process enough samples to trigger a new value
        for (int j = 0; j < 882; ++j) {
            src.process();
        }
        float val = src.getCurrentValue();
        // Map [-1, 1] to bin index [0, 3]
        int bin = static_cast<int>((val + 1.0f) * 0.5f * kNumBins);
        bin = std::clamp(bin, 0, kNumBins - 1);
        bins[bin]++;
    }

    // Chi-squared test: expected = kNumValues / kNumBins = 2500
    float expected = static_cast<float>(kNumValues) / kNumBins;
    float chiSquared = 0.0f;
    for (int i = 0; i < kNumBins; ++i) {
        float diff = static_cast<float>(bins[i]) - expected;
        chiSquared += (diff * diff) / expected;
    }

    // Chi-squared critical value for 3 df at p=0.01 is 11.345
    // SC-016: passes at p > 0.01
    REQUIRE(chiSquared < 11.345f);
}
