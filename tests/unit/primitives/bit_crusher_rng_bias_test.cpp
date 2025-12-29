// ==============================================================================
// BitCrusher RNG Bias Investigation - Test-First
// ==============================================================================
// Investigates whether the BitCrusher's RNG has a bias that could cause
// integration/ramping when applied as dither.
//
// Hypothesis: nextRandom() should have zero mean for TPDF dither to be DC-free.
// If RNG has bias, dither will introduce DC offset that accumulates.
// ==============================================================================

#include "dsp/primitives/bit_crusher.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <array>
#include <cmath>

using namespace Iterum::DSP;
using Catch::Approx;

namespace {
constexpr double kSampleRate = 44100.0;
}

// ==============================================================================
// Test 1: RNG Long-Term Bias Test (10000 samples)
// ==============================================================================

TEST_CASE("BitCrusher RNG has zero mean over long runs", "[primitives][bit-crusher][rng][bias][long]") {
    // CRITICAL: Test RNG bias over 10000 samples (same as failing test)
    // This will reveal if RNG has long-term drift that short tests miss

    BitCrusher crusher;
    crusher.prepare(kSampleRate);
    crusher.setBitDepth(16.0f);
    crusher.setDither(0.5f); // Same as failing test

    constexpr size_t kNumSamples = 10000;
    std::array<float, kNumSamples> buffer{};

    // Process with constant 0.5f input (same as failing test)
    std::fill(buffer.begin(), buffer.end(), 0.5f);
    crusher.process(buffer.data(), kNumSamples);

    // Measure mean at same points as failing test
    double sum_0_10 = 0.0;
    double sum_100_110 = 0.0;
    double sum_500_510 = 0.0;
    double sum_1000_1010 = 0.0;

    for (size_t i = 0; i < 10; ++i) {
        sum_0_10 += buffer[i];
        sum_100_110 += buffer[100 + i];
        sum_500_510 += buffer[500 + i];
        sum_1000_1010 += buffer[1000 + i];
    }

    float mean_0_10 = sum_0_10 / 10.0;
    float mean_100_110 = sum_100_110 / 10.0;
    float mean_500_510 = sum_500_510 / 10.0;
    float mean_1000_1010 = sum_1000_1010 / 10.0;

    INFO("Mean at 0-10: " << mean_0_10);
    INFO("Mean at 100-110: " << mean_100_110);
    INFO("Mean at 500-510: " << mean_500_510);
    INFO("Mean at 1000-1010: " << mean_1000_1010);

    // All means should be close to 0.5 (no ramping)
    REQUIRE(std::abs(mean_0_10 - 0.5f) < 0.05f);
    REQUIRE(std::abs(mean_100_110 - 0.5f) < 0.05f);
    REQUIRE(std::abs(mean_500_510 - 0.5f) < 0.05f);
    REQUIRE(std::abs(mean_1000_1010 - 0.5f) < 0.05f);

    // No ramping between measurement points
    REQUIRE(std::abs(mean_0_10 - mean_1000_1010) < 0.01f);
}

// ==============================================================================
// Test 2: TPDF (r1 + r2) Should Have Zero Mean
// ==============================================================================

TEST_CASE("BitCrusher TPDF dither has zero mean", "[primitives][bit-crusher][rng][bias][tpdf]") {
    // Even if individual random values have slight bias,
    // TPDF (sum of two uniform) should have zero mean

    BitCrusher crusher;
    crusher.prepare(kSampleRate);
    crusher.setBitDepth(16.0f);
    crusher.setDither(1.0f); // Full dither

    constexpr size_t kNumSamples = 100000;
    std::array<float, kNumSamples> buffer{};

    // Process silence with dither
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    crusher.process(buffer.data(), kNumSamples);

    // Measure mean
    double sum = 0.0;
    for (size_t i = 0; i < kNumSamples; ++i) {
        sum += buffer[i];
    }
    double mean = sum / kNumSamples;

    INFO("TPDF mean: " << mean);
    REQUIRE(std::abs(mean) < 0.001);
}

// ==============================================================================
// Test 3: Constant Input with Dither Should Not Ramp
// ==============================================================================

TEST_CASE("BitCrusher with dither does not cause ramping on constant input",
          "[primitives][bit-crusher][rng][bias][ramping]") {
    // This is the key test: constant input + dither should produce constant output (plus noise)
    // If dither has DC bias, output will ramp over time

    BitCrusher crusher;
    crusher.prepare(kSampleRate);
    crusher.setBitDepth(16.0f);
    crusher.setDither(0.5f); // 50% dither (same as CharacterProcessor default)

    constexpr size_t kBufferSize = 10000;
    std::array<float, kBufferSize> buffer{};
    std::fill(buffer.begin(), buffer.end(), 0.5f); // Constant input

    crusher.process(buffer.data(), kBufferSize);

    // Measure mean over first quarter vs last quarter
    double sum1 = 0.0, sum2 = 0.0;
    for (size_t i = 0; i < kBufferSize / 4; ++i) {
        sum1 += buffer[i];
        sum2 += buffer[kBufferSize * 3 / 4 + i];
    }
    double mean1 = sum1 / (kBufferSize / 4);
    double mean2 = sum2 / (kBufferSize / 4);

    INFO("First quarter mean: " << mean1);
    INFO("Last quarter mean: " << mean2);
    INFO("Difference: " << std::abs(mean1 - mean2));

    // Should not ramp - means should be nearly identical
    REQUIRE(std::abs(mean1 - mean2) < 0.01);
}

// ==============================================================================
// Test 4: Compare Age 0% vs Age 50% Dither Behavior
// ==============================================================================

TEST_CASE("BitCrusher 16-bit vs 10-bit dither behavior",
          "[primitives][bit-crusher][rng][bias][age]") {
    // Age 0% = 16-bit, Age 50% = 10-bit
    // If 16-bit shows ramping but 10-bit doesn't, it's bit-depth specific

    constexpr size_t kBufferSize = 10000;

    // Test 16-bit (Age 0%)
    BitCrusher crusher16;
    crusher16.prepare(kSampleRate);
    crusher16.setBitDepth(16.0f);
    crusher16.setDither(0.5f);

    std::array<float, kBufferSize> buffer16{};
    std::fill(buffer16.begin(), buffer16.end(), 0.5f);
    crusher16.process(buffer16.data(), kBufferSize);

    double sum16_1 = 0.0, sum16_2 = 0.0;
    for (size_t i = 0; i < kBufferSize / 4; ++i) {
        sum16_1 += buffer16[i];
        sum16_2 += buffer16[kBufferSize * 3 / 4 + i];
    }
    double mean16_1 = sum16_1 / (kBufferSize / 4);
    double mean16_2 = sum16_2 / (kBufferSize / 4);

    // Test 10-bit (Age 50%)
    BitCrusher crusher10;
    crusher10.prepare(kSampleRate);
    crusher10.setBitDepth(10.0f);
    crusher10.setDither(0.5f);

    std::array<float, kBufferSize> buffer10{};
    std::fill(buffer10.begin(), buffer10.end(), 0.5f);
    crusher10.process(buffer10.data(), kBufferSize);

    double sum10_1 = 0.0, sum10_2 = 0.0;
    for (size_t i = 0; i < kBufferSize / 4; ++i) {
        sum10_1 += buffer10[i];
        sum10_2 += buffer10[kBufferSize * 3 / 4 + i];
    }
    double mean10_1 = sum10_1 / (kBufferSize / 4);
    double mean10_2 = sum10_2 / (kBufferSize / 4);

    INFO("16-bit: first=" << mean16_1 << " last=" << mean16_2 << " diff=" << std::abs(mean16_1 - mean16_2));
    INFO("10-bit: first=" << mean10_1 << " last=" << mean10_2 << " diff=" << std::abs(mean10_1 - mean10_2));

    // BOTH should not ramp
    REQUIRE(std::abs(mean16_1 - mean16_2) < 0.01);
    REQUIRE(std::abs(mean10_1 - mean10_2) < 0.01);
}
