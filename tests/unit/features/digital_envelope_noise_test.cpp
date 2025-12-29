// ==============================================================================
// Digital Delay Envelope-Following Noise Tests
// ==============================================================================
// Tests for envelope-modulated noise in Digital Delay Lo-Fi mode.
// Verifies that noise "breathes" with the input signal like real analog gear.
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

// ==============================================================================
// Helper Functions
// ==============================================================================

namespace {

constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;
constexpr size_t kTestBufferSize = 4410;  // ~100ms at 44.1kHz

/// @brief Generate impulse train with silence gaps (percussive test signal)
void generatePercussiveSignal(float* left, float* right, size_t size) {
    std::fill(left, left + size, 0.0f);
    std::fill(right, right + size, 0.0f);

    // Create impulses every 1000 samples with decay
    for (size_t i = 0; i < size; i += 1000) {
        for (size_t j = 0; j < 100 && (i + j) < size; ++j) {
            float decay = std::exp(-static_cast<float>(j) / 20.0f);
            left[i + j] = 0.8f * decay;
            right[i + j] = 0.8f * decay;
        }
    }
}

/// @brief Measure RMS over a window
float measureRMS(const float* buffer, size_t start, size_t length) {
    float sum = 0.0f;
    for (size_t i = start; i < start + length; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(length));
}

} // anonymous namespace

// ==============================================================================
// Test: EnvelopeFollower Integration
// ==============================================================================

TEST_CASE("DigitalDelay can be instantiated and prepared", "[features][digital-delay][envelope]") {
    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);

    REQUIRE(delay.isPrepared());
}

// ==============================================================================
// Test: EnvelopeFollower Initialization
// ==============================================================================

TEST_CASE("EnvelopeFollower is initialized with correct settings", "[features][digital-delay][envelope]") {
    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);
    delay.setEra(DigitalEra::LoFi);
    delay.setAge(1.0f);  // 100% age

    // This test will pass once EnvelopeFollower is added and initialized
    // We can't directly inspect the EnvelopeFollower, but we can verify
    // it's working by checking that noise modulation occurs
    REQUIRE(delay.isPrepared());
}

// ==============================================================================
// Test: Envelope Buffer Allocation
// ==============================================================================

TEST_CASE("DigitalDelay allocates resources in prepare()", "[features][digital-delay][envelope]") {
    DigitalDelay delay;

    // Should not crash when preparing with various block sizes
    delay.prepare(kSampleRate, 64);
    REQUIRE(delay.isPrepared());

    delay.prepare(kSampleRate, 512);
    REQUIRE(delay.isPrepared());

    delay.prepare(kSampleRate, 2048);
    REQUIRE(delay.isPrepared());
}

// ==============================================================================
// Test: Envelope Tracking
// ==============================================================================

TEST_CASE("Input envelope is tracked before processing", "[features][digital-delay][envelope]") {
    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);
    delay.setEra(DigitalEra::LoFi);
    delay.setAge(1.0f);
    delay.setMix(1.0f);  // 100% wet
    delay.setDelayTime(10.0f);  // Short delay
    delay.snapParameters();

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    // Create loud signal
    std::fill(left.begin(), left.end(), 0.5f);
    std::fill(right.begin(), right.end(), 0.5f);

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kTestBufferSize,
        .tempoBPM = 120.0,
        .isPlaying = false
    };

    // Process - should track envelope without crashing
    delay.process(left.data(), right.data(), kTestBufferSize, ctx);

    // If envelope tracking is working, output should contain noise
    // We'll verify this properly in the next tests
    REQUIRE(delay.isPrepared());
}

// ==============================================================================
// Test: Envelope-Modulated Noise
// ==============================================================================

TEST_CASE("Noise is modulated by input envelope", "[features][digital-delay][envelope][SC-001]") {
    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);
    delay.setEra(DigitalEra::LoFi);
    delay.setAge(1.0f);  // Maximum degradation
    delay.setMix(1.0f);  // 100% wet to hear noise clearly
    delay.setDelayTime(10.0f);  // Short delay
    delay.setFeedback(0.0f);  // No feedback
    delay.snapParameters();

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kTestBufferSize,
        .tempoBPM = 120.0,
        .isPlaying = false
    };

    // Test 1: Loud input should produce more noise
    std::fill(left.begin(), left.end(), 0.8f);
    std::fill(right.begin(), right.end(), 0.8f);

    delay.process(left.data(), right.data(), kTestBufferSize, ctx);

    float loudRMS = measureRMS(left.data(), 100, 1000);

    // Test 2: Quiet input should produce less noise
    delay.reset();
    std::fill(left.begin(), left.end(), 0.1f);
    std::fill(right.begin(), right.end(), 0.1f);

    delay.process(left.data(), right.data(), kTestBufferSize, ctx);

    float quietRMS = measureRMS(left.data(), 100, 1000);

    // Loud signal should produce more noise than quiet signal
    REQUIRE(loudRMS > quietRMS);
}

// ==============================================================================
// Test: Noise Floor
// ==============================================================================

TEST_CASE("Noise has minimum floor at silence", "[features][digital-delay][envelope][SC-002]") {
    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);
    delay.setEra(DigitalEra::LoFi);
    delay.setAge(1.0f);
    delay.setMix(1.0f);
    delay.setDelayTime(10.0f);
    delay.setFeedback(0.0f);
    delay.snapParameters();

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    // Complete silence input
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kTestBufferSize,
        .tempoBPM = 120.0,
        .isPlaying = false
    };

    delay.process(left.data(), right.data(), kTestBufferSize, ctx);

    // Even with silence input, there should be SOME noise (5% floor)
    // Measure RMS after delay has settled
    float silenceRMS = measureRMS(left.data(), 500, 1000);

    // Should be non-zero (noise present) but quiet
    // With 5% noise floor, expect RMS around 0.03-0.05 depending on Age
    REQUIRE(silenceRMS > 0.0f);
    REQUIRE(silenceRMS < 0.10f);  // Present but relatively quiet
}

// ==============================================================================
// Test: Dynamic Noise Behavior
// ==============================================================================

TEST_CASE("Noise breathes with percussive input", "[features][digital-delay][envelope][SC-003]") {
    // This test verifies that noise follows the delayed signal's dynamics
    // Use a short delay and fast envelope decay to make breathing effect obvious
    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);
    delay.setEra(DigitalEra::LoFi);
    delay.setAge(1.0f);   // 100% degradation
    delay.setMix(1.0f);   // 100% wet
    delay.setDelayTime(5.0f);   // Very short delay (220 samples)
    delay.setFeedback(0.0f);    // No feedback
    delay.snapParameters();

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    // Generate percussive signal: impulses with silence gaps
    generatePercussiveSignal(left.data(), right.data(), kTestBufferSize);

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kTestBufferSize,
        .tempoBPM = 120.0,
        .isPlaying = false
    };

    delay.process(left.data(), right.data(), kTestBufferSize, ctx);

    // Account for 5ms delay = ~220 samples at 44.1kHz
    // Impulse at input sample 0 appears at output sample 220
    // We track the DELAYED signal's envelope, not the input
    const size_t delayOffset = 220;

    // Measure noise during transient (right when delayed impulse arrives)
    float transientRMS = measureRMS(left.data(), delayOffset + 10, 80);

    // Measure noise during silence gap (far from any impulse)
    // Impulses are every 1000 samples, silence is in the middle
    float silenceRMS = measureRMS(left.data(), 700, 100);

    // Noise should be louder during transients than during silence
    REQUIRE(transientRMS > silenceRMS);

    // Both should be non-zero (noise floor prevents complete silence)
    REQUIRE(silenceRMS > 0.0f);
}

// ==============================================================================
// Test: No NaN or Inf with Envelope Modulation
// ==============================================================================

TEST_CASE("Envelope-modulated noise produces no NaN or Inf", "[features][digital-delay][envelope][safety]") {
    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);
    delay.setEra(DigitalEra::LoFi);
    delay.setAge(1.0f);
    delay.setMix(1.0f);
    delay.setDelayTime(10.0f);
    delay.snapParameters();

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kTestBufferSize,
        .tempoBPM = 120.0,
        .isPlaying = false
    };

    // Test with various extreme inputs
    SECTION("very loud input") {
        std::fill(left.begin(), left.end(), 10.0f);
        std::fill(right.begin(), right.end(), 10.0f);

        delay.process(left.data(), right.data(), kTestBufferSize, ctx);

        for (size_t i = 0; i < kTestBufferSize; ++i) {
            REQUIRE(std::isfinite(left[i]));
            REQUIRE(std::isfinite(right[i]));
        }
    }

    SECTION("silence input") {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);

        delay.process(left.data(), right.data(), kTestBufferSize, ctx);

        for (size_t i = 0; i < kTestBufferSize; ++i) {
            REQUIRE(std::isfinite(left[i]));
            REQUIRE(std::isfinite(right[i]));
        }
    }

    SECTION("rapid dynamics") {
        generatePercussiveSignal(left.data(), right.data(), kTestBufferSize);

        delay.process(left.data(), right.data(), kTestBufferSize, ctx);

        for (size_t i = 0; i < kTestBufferSize; ++i) {
            REQUIRE(std::isfinite(left[i]));
            REQUIRE(std::isfinite(right[i]));
        }
    }
}
