// ==============================================================================
// Digital Delay Age 0% Ramping Investigation - Test-First
// ==============================================================================
// This test isolates the ramping issue observed in Age 0% configuration.
// The "Age parameter controls base dither level" test shows AGE0 ramping from
// 0.643 upward with constant 0.5 input and feedback=0, while AGE50 is stable.
//
// This is a minimal reproduction to identify the root cause.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/features/digital_delay.h"
#include "dsp/core/block_context.h"

#include <array>
#include <cmath>
#include <algorithm>

using Catch::Approx;
using namespace Iterum::DSP;

namespace {
constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;
constexpr size_t kTestBufferSize = 4410;  // ~100ms at 44.1kHz

float measureMean(const float* buffer, size_t start, size_t length) {
    float sum = 0.0f;
    for (size_t i = start; i < start + length; ++i) {
        sum += buffer[i];
    }
    return sum / static_cast<float>(length);
}
}

// ==============================================================================
// Test 1: Constant Input with Age 0% Should Produce Stable Output
// ==============================================================================

TEST_CASE("DigitalDelay Age 0% with constant input produces stable output (no ramping)",
          "[features][digital-delay][age][ramping][regression]") {
    // CRITICAL: With constant input and feedback=0, output should be stable
    // Current bug: Age 0% shows ramping from 0.643 upward
    // Expected: Output should be constant around 0.5 (input level)

    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);
    delay.setEra(DigitalEra::LoFi);
    delay.setMix(1.0f);  // 100% wet
    delay.setDelayTime(10.0f);  // Short delay
    delay.setFeedback(0.0f);  // No feedback - should eliminate accumulation
    delay.setAge(0.0f);  // Age 0% - this is the failing case
    delay.reset();  // Reset CharacterProcessor crossfade state
    delay.snapParameters();

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    // Constant input
    std::fill(left.begin(), left.end(), 0.5f);
    std::fill(right.begin(), right.end(), 0.5f);

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kTestBufferSize,
        .tempoBPM = 120.0,
        .isPlaying = false
    };

    delay.process(left.data(), right.data(), kTestBufferSize, ctx);

    // Measure mean at different points to check for ramping
    // Skip first 500 samples to avoid initial transients
    float mean1 = measureMean(left.data(), 500, 500);   // samples 500-1000
    float mean2 = measureMean(left.data(), 1500, 500);  // samples 1500-2000
    float mean3 = measureMean(left.data(), 3000, 500);  // samples 3000-3500

    // All means should be approximately equal (no ramping)
    // With constant input and no feedback, output should be stable
    REQUIRE(std::abs(mean1 - mean2) < 0.01f);
    REQUIRE(std::abs(mean2 - mean3) < 0.01f);
    REQUIRE(std::abs(mean1 - mean3) < 0.01f);

    // Output should be close to input level (0.5)
    // Allow some variance for quantization, but should not be 0.64+
    REQUIRE(mean1 == Approx(0.5f).margin(0.05f));
}

// ==============================================================================
// Test 2: Age 50% Comparison (Known Working Case)
// ==============================================================================

TEST_CASE("DigitalDelay Age 50% with constant input produces stable output (baseline)",
          "[features][digital-delay][age][ramping][regression]") {
    // This test documents the WORKING behavior at Age 50%
    // Age 50% correctly produces stable output around 0.5

    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);
    delay.setEra(DigitalEra::LoFi);
    delay.setMix(1.0f);
    delay.setDelayTime(10.0f);
    delay.setFeedback(0.0f);
    delay.setAge(0.5f);  // Age 50% - known to work correctly
    delay.reset();  // Reset CharacterProcessor crossfade state
    delay.snapParameters();

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    std::fill(left.begin(), left.end(), 0.5f);
    std::fill(right.begin(), right.end(), 0.5f);

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kTestBufferSize,
        .tempoBPM = 120.0,
        .isPlaying = false
    };

    delay.process(left.data(), right.data(), kTestBufferSize, ctx);

    // Age 50% should produce stable output
    float mean1 = measureMean(left.data(), 500, 500);
    float mean2 = measureMean(left.data(), 1500, 500);
    float mean3 = measureMean(left.data(), 3000, 500);

    // Should be stable (this currently passes)
    REQUIRE(std::abs(mean1 - mean2) < 0.01f);
    REQUIRE(std::abs(mean2 - mean3) < 0.01f);
    REQUIRE(mean1 == Approx(0.5f).margin(0.05f));
}

// ==============================================================================
// Test 3: Early Samples Analysis
// ==============================================================================

TEST_CASE("DigitalDelay Age 0% early samples show ramping pattern",
          "[features][digital-delay][age][ramping][regression][diagnostic]") {
    // This test examines the early samples to see where ramping starts
    // Helps identify if it's an initialization issue or accumulation

    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);
    delay.setEra(DigitalEra::LoFi);
    delay.setMix(1.0f);
    delay.setDelayTime(10.0f);
    delay.setFeedback(0.0f);
    delay.setAge(0.0f);
    delay.reset();  // Reset CharacterProcessor crossfade state
    delay.snapParameters();

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    std::fill(left.begin(), left.end(), 0.5f);
    std::fill(right.begin(), right.end(), 0.5f);

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kTestBufferSize,
        .tempoBPM = 120.0,
        .isPlaying = false
    };

    delay.process(left.data(), right.data(), kTestBufferSize, ctx);

    // Check if first valid output samples (after delay) are already high
    // Delay is 10ms = 441 samples at 44.1kHz
    // First output appears around sample 441+

    float mean_450_460 = measureMean(left.data(), 450, 10);
    float mean_500_510 = measureMean(left.data(), 500, 10);
    float mean_1000_1010 = measureMean(left.data(), 1000, 10);

    INFO("Mean at samples 450-460: " << mean_450_460);
    INFO("Mean at samples 500-510: " << mean_500_510);
    INFO("Mean at samples 1000-1010: " << mean_1000_1010);

    // All samples should be stable around 0.5
    float earlyToMid = std::abs(mean_450_460 - mean_500_510);
    float midToLate = std::abs(mean_500_510 - mean_1000_1010);

    INFO("Early to mid change: " << earlyToMid);
    INFO("Mid to late change: " << midToLate);

    // Should be stable (no ramping)
    REQUIRE(std::abs(mean_450_460 - mean_1000_1010) < 0.01f);
}
