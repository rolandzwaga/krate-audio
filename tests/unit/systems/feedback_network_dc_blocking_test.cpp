// ==============================================================================
// FeedbackNetwork DC Blocking - Test-First Implementation
// ==============================================================================
// This test verifies that DC blocking filter prevents DC offset accumulation
// in feedback loops, specifically addressing the age parameter ramping issue.
//
// Root cause: BitCrusher quantization + IIR filter round-off creates tiny DC bias
// that accumulates through feedback iterations, causing slow ramping drift.
//
// Solution: DC blocking filter in feedback path removes accumulated DC before
// feeding back to delay input.
// ==============================================================================

#include "dsp/systems/feedback_network.h"
#include "dsp/core/block_context.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <array>
#include <cmath>
#include <algorithm>

using namespace Iterum::DSP;
using Catch::Approx;

namespace {
constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;
constexpr size_t kTestBufferSize = 8192;  // Longer buffer for better settling
constexpr float kMaxDelayMs = 2000.0f;

float measureMean(const float* buffer, size_t start, size_t count) {
    double sum = 0.0;
    for (size_t i = start; i < start + count; ++i) {
        sum += buffer[i];
    }
    return static_cast<float>(sum / count);
}

BlockContext makeContext() {
    return BlockContext{
        .sampleRate = kSampleRate,
        .blockSize = kBlockSize,
        .tempoBPM = 120.0,
        .isPlaying = false
    };
}
}

// ==============================================================================
// Test 1: DC Blocker Removes Constant DC Offset
// ==============================================================================

TEST_CASE("FeedbackNetwork: DC blocker removes constant DC offset in feedback path",
          "[systems][feedback-network][dc-blocking]") {
    // CRITICAL: DC blocker must remove any DC bias introduced by processing
    // This test feeds a constant DC offset through the feedback network and
    // verifies that DC blocker prevents it from accumulating.

    FeedbackNetwork network;
    network.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    network.setFeedbackAmount(0.8f);  // High feedback to amplify DC accumulation
    network.setDelayTimeMs(10.0f);    // Short delay for faster accumulation
    network.setFilterEnabled(false);
    network.setSaturationEnabled(false);

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    // Feed constant DC offset (simulating quantization bias)
    std::fill(left.begin(), left.end(), 0.001f);   // Small DC offset
    std::fill(right.begin(), right.end(), 0.001f);

    BlockContext ctx = makeContext();
    network.process(left.data(), right.data(), kTestBufferSize, ctx);

    // Measure output mean over second half (after settling)
    float meanL = measureMean(left.data(), kTestBufferSize / 2, kTestBufferSize / 2);

    // Without DC blocker: Mean would grow to ~0.005f or higher due to accumulation
    // With DC blocker: Mean should stay near input level (0.001f)
    REQUIRE(std::abs(meanL) < 0.002f);  // Should not accumulate significantly
}

// ==============================================================================
// Test 2: No Ramping with Constant Input (Main Bug Fix)
// ==============================================================================

TEST_CASE("FeedbackNetwork: Constant input produces constant output (no ramping)",
          "[systems][feedback-network][dc-blocking][ramping]") {
    // This is the PRIMARY test for the age parameter bug fix
    // Constant input should produce constant output, not ramping drift

    FeedbackNetwork network;
    network.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    network.setFeedbackAmount(0.5f);  // Moderate feedback
    network.setDelayTimeMs(20.0f);
    network.setFilterEnabled(false);   // Disabled to isolate DC blocker
    network.setSaturationEnabled(false);

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    std::fill(left.begin(), left.end(), 0.5f);
    std::fill(right.begin(), right.end(), 0.5f);

    BlockContext ctx = makeContext();
    network.process(left.data(), right.data(), kTestBufferSize, ctx);

    // Skip first half to avoid initial transients (smoothers, crossfade)
    // Measure mean over third quarter vs last quarter (both should be settled)
    float mean1 = measureMean(left.data(), kTestBufferSize / 2, kTestBufferSize / 4);
    float mean2 = measureMean(left.data(), kTestBufferSize * 3 / 4, kTestBufferSize / 4);

    // CRITICAL: Should be nearly identical (no ramping)
    // Without DC blocker: difference could be > 0.1f
    // With DC blocker: difference should be < 0.01f
    REQUIRE(std::abs(mean1 - mean2) < 0.01f);
}

// ==============================================================================
// Test 3: DC Blocker with Filter Enabled
// ==============================================================================

TEST_CASE("FeedbackNetwork: DC blocker works with feedback filter enabled",
          "[systems][feedback-network][dc-blocking][filter]") {
    // IIR filters can accumulate round-off errors creating DC drift
    // DC blocker should prevent this even with filter enabled

    FeedbackNetwork network;
    network.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    network.setFeedbackAmount(0.6f);
    network.setDelayTimeMs(15.0f);
    network.setFilterEnabled(true);
    network.setFilterType(FilterType::Lowpass);
    network.setFilterCutoff(8000.0f);   // Low cutoff increases accumulation risk
    network.setFilterResonance(0.707f);
    network.setSaturationEnabled(false);

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    std::fill(left.begin(), left.end(), 0.5f);
    std::fill(right.begin(), right.end(), 0.5f);

    BlockContext ctx = makeContext();
    network.process(left.data(), right.data(), kTestBufferSize, ctx);

    // Skip first half to avoid initial transients
    // Check for ramping in settled region
    float mean1 = measureMean(left.data(), kTestBufferSize / 2, kTestBufferSize / 4);
    float mean2 = measureMean(left.data(), kTestBufferSize * 3 / 4, kTestBufferSize / 4);

    // With filter + feedback, DC blocker is CRITICAL
    REQUIRE(std::abs(mean1 - mean2) < 0.02f);
}

// ==============================================================================
// Test 4: DC Blocker Preserves Audio Content
// ==============================================================================

TEST_CASE("FeedbackNetwork: DC blocker preserves AC audio content",
          "[systems][feedback-network][dc-blocking][audio]") {
    // DC blocker should only remove DC, not attenuate audio frequencies
    // Test with 440Hz sine wave (A4 note)

    FeedbackNetwork network;
    network.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    network.setFeedbackAmount(0.3f);  // Low feedback to isolate DC blocker effect
    network.setDelayTimeMs(10.0f);
    network.setFilterEnabled(false);
    network.setSaturationEnabled(false);

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    // Generate 440Hz sine wave
    constexpr float kFrequency = 440.0f;
    for (size_t i = 0; i < kTestBufferSize; ++i) {
        float phase = 2.0f * 3.14159265f * kFrequency * i / static_cast<float>(kSampleRate);
        left[i] = 0.5f * std::sin(phase);
        right[i] = left[i];
    }

    // Measure input RMS
    float inputRMS = 0.0f;
    for (size_t i = 512; i < 1536; ++i) {
        inputRMS += left[i] * left[i];
    }
    inputRMS = std::sqrt(inputRMS / 1024.0f);

    BlockContext ctx = makeContext();
    network.process(left.data(), right.data(), kTestBufferSize, ctx);

    // Measure output RMS (after settling)
    float outputRMS = 0.0f;
    for (size_t i = 512; i < 1536; ++i) {
        outputRMS += left[i] * left[i];
    }
    outputRMS = std::sqrt(outputRMS / 1024.0f);

    // DC blocker should NOT significantly attenuate 440Hz
    // Allow some attenuation from delay/feedback, but should be close
    REQUIRE(outputRMS > inputRMS * 0.3f);  // At least 30% of input level
}

// ==============================================================================
// Test 5: Zero Mean with Bipolar Signal
// ==============================================================================

TEST_CASE("FeedbackNetwork: DC blocker maintains zero mean for bipolar signals",
          "[systems][feedback-network][dc-blocking][bipolar]") {
    // Symmetric bipolar signals should maintain zero mean through feedback
    // This verifies DC blocker doesn't introduce its own bias

    FeedbackNetwork network;
    network.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    network.setFeedbackAmount(0.5f);
    network.setDelayTimeMs(20.0f);
    network.setFilterEnabled(false);
    network.setSaturationEnabled(false);

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    // Generate symmetric square wave
    for (size_t i = 0; i < kTestBufferSize; ++i) {
        left[i] = (i % 2 == 0) ? 0.5f : -0.5f;
        right[i] = left[i];
    }

    BlockContext ctx = makeContext();
    network.process(left.data(), right.data(), kTestBufferSize, ctx);

    // Measure mean over second half
    float mean = measureMean(left.data(), kTestBufferSize / 2, kTestBufferSize / 2);

    // Should be near zero (allow for quantization/rounding)
    REQUIRE(std::abs(mean) < 0.01f);
}
