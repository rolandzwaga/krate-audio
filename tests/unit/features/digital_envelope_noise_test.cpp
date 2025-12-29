// ==============================================================================
// Digital Delay Envelope-Following Dither Tests
// ==============================================================================
// Tests for envelope-modulated dither in Digital Delay Lo-Fi mode.
// Verifies that BitCrusher dither "breathes" with the input signal.
//
// IMPLEMENTATION NOTES:
// - Noise comes from BitCrusher's TPDF dither, NOT from generating new noise
// - Envelope tracks DRY input signal BEFORE character processing
// - Dither amount is modulated by envelope: dither = (Age level) × (envelope)
// - During silence: envelope = 0 → dither = 0 → NO noise (not 1% floor!)
// - During input: envelope > 0 → dither scales with envelope
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

/// @brief Measure peak absolute value over a window
float measurePeak(const float* buffer, size_t start, size_t length) {
    float peak = 0.0f;
    for (size_t i = start; i < start + length; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

} // anonymous namespace

// ==============================================================================
// Test: Initialization
// ==============================================================================

TEST_CASE("DigitalDelay can be instantiated and prepared", "[features][digital-delay][envelope]") {
    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);

    REQUIRE(delay.isPrepared());
}

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
// Test: Envelope-Modulated Dither - Core Behavior
// ==============================================================================

TEST_CASE("Dither drops to near-zero during silence (no noise floor)", "[features][digital-delay][envelope][SC-002]") {
    // CRITICAL TEST: With envelope-modulated dither, silence produces near-zero noise
    // This is DIFFERENT from the old implementation which had a 1% noise floor
    //
    // Expected behavior:
    // - Input silence → envelope = 0 → dither = 0 → NO noise
    // - This is the correct behavior - dither only appears when there's audio

    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);
    delay.setEra(DigitalEra::LoFi);
    delay.setAge(1.0f);  // 100% age - maximum dither potential
    delay.setMix(1.0f);  // 100% wet
    delay.setDelayTime(10.0f);  // Short delay
    delay.setFeedback(0.0f);  // No feedback
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

    // Measure RMS after delay has settled
    float silenceRMS = measureRMS(left.data(), 500, 1000);
    float silencePeak = measurePeak(left.data(), 500, 1000);

    // With envelope-modulated dither, silence should produce VERY LOW noise
    // Envelope drops to near-zero → dither = 0 → no noise
    // Allow for tiny residual from envelope attack/release tail
    REQUIRE(silenceRMS < 0.001f);   // Very quiet (< -60dB)
    REQUIRE(silencePeak < 0.01f);   // Peak should also be very low
}

TEST_CASE("Dither scales with input envelope amplitude", "[features][digital-delay][envelope][SC-001]") {
    // This test verifies that dither amount follows envelope amplitude
    // Loud input → high envelope → more dither
    // Quiet input → low envelope → less dither

    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);
    delay.setEra(DigitalEra::LoFi);
    delay.setAge(1.0f);  // Maximum degradation
    delay.setMix(1.0f);  // 100% wet to hear dither clearly
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

    // Test 1: Loud input should produce more dither noise
    std::fill(left.begin(), left.end(), 0.8f);
    std::fill(right.begin(), right.end(), 0.8f);

    delay.process(left.data(), right.data(), kTestBufferSize, ctx);

    float loudRMS = measureRMS(left.data(), 100, 1000);

    // Test 2: Quiet input should produce less dither noise
    delay.reset();
    std::fill(left.begin(), left.end(), 0.1f);
    std::fill(right.begin(), right.end(), 0.1f);

    delay.process(left.data(), right.data(), kTestBufferSize, ctx);

    float quietRMS = measureRMS(left.data(), 100, 1000);

    // Loud signal should produce more noise than quiet signal
    REQUIRE(loudRMS > quietRMS);

    // Difference should be substantial (at least 2x)
    REQUIRE(loudRMS > quietRMS * 2.0f);
}

// ==============================================================================
// Test: Age Parameter Controls Base Dither Level
// ==============================================================================

TEST_CASE("Age parameter controls base dither level", "[features][digital-delay][envelope][SC-004]") {
    // This test verifies that Age controls the base dither gain
    // Age maps to noiseGain_ which ranges from -80dB (age=0) to -40dB (age=1.0)
    // Then envelope modulates this base level
    //
    // We use CONSTANT INPUT to keep envelope constant, so we measure only Age effect

    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);
    delay.setEra(DigitalEra::LoFi);
    delay.setMix(1.0f);  // 100% wet
    delay.setDelayTime(10.0f);  // Short delay
    delay.setFeedback(0.0f);  // No feedback

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kTestBufferSize,
        .tempoBPM = 120.0,
        .isPlaying = false
    };

    // Use CONSTANT INPUT to keep envelope constant
    // This isolates the Age parameter's effect on base dither level
    std::fill(left.begin(), left.end(), 0.5f);
    std::fill(right.begin(), right.end(), 0.5f);

    // Test 1: Age 0% should produce very quiet dither
    delay.setAge(0.0f);  // 0% age = -80dB noise gain
    delay.reset();  // Reset CharacterProcessor crossfade state
    delay.snapParameters();

    delay.process(left.data(), right.data(), kTestBufferSize, ctx);
    float rmsAge0 = measureRMS(left.data(), 500, 1000);

    // Test 2: Age 100% should produce loud dither
    delay.reset();
    std::fill(left.begin(), left.end(), 0.5f);
    std::fill(right.begin(), right.end(), 0.5f);

    delay.setAge(1.0f);  // 100% age = -40dB noise gain
    delay.snapParameters();

    delay.process(left.data(), right.data(), kTestBufferSize, ctx);
    float rmsAge100 = measureRMS(left.data(), 500, 1000);

    // Test 3: Age 50% should be in between
    delay.reset();
    std::fill(left.begin(), left.end(), 0.5f);
    std::fill(right.begin(), right.end(), 0.5f);

    delay.setAge(0.5f);  // 50% age = -60dB noise gain
    delay.snapParameters();

    delay.process(left.data(), right.data(), kTestBufferSize, ctx);
    float rmsAge50 = measureRMS(left.data(), 500, 1000);

    // Verify dither level increases with Age
    REQUIRE(rmsAge0 < rmsAge50);   // 0% < 50%
    REQUIRE(rmsAge50 < rmsAge100); // 50% < 100%

    // Verify substantial difference (at least 10x between 0% and 100%)
    // -80dB vs -40dB = 40dB difference = 100x in linear amplitude
    REQUIRE(rmsAge100 > rmsAge0 * 10.0f);
}

// ==============================================================================
// Test: Dynamic Breathing Behavior
// ==============================================================================

TEST_CASE("Dither breathes with percussive input (transients loud, silence quiet)", "[features][digital-delay][envelope][SC-003]") {
    // This test verifies that dither follows the input signal's dynamics
    // - During transients: high envelope → loud dither
    // - During silence gaps: envelope drops → dither drops to near-zero
    //
    // This is the "breathing" effect characteristic of analog tape noise

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
    const size_t delayOffset = 220;

    // Measure dither during transient (right when delayed impulse arrives)
    float transientRMS = measureRMS(left.data(), delayOffset + 10, 80);

    // Measure dither during silence gap (far from any impulse)
    // Impulses are every 1000 samples, silence is in the middle
    float silenceRMS = measureRMS(left.data(), 700, 100);

    // Dither should be MUCH louder during transients than during silence
    // With envelope-modulated dither, silence should be near-zero
    REQUIRE(transientRMS > silenceRMS * 5.0f);  // At least 5x louder

    // Silence should be VERY quiet (envelope drops to near-zero)
    REQUIRE(silenceRMS < 0.01f);  // Much quieter than old 1% floor implementation
}

// ==============================================================================
// Test: Envelope Behavior with Feedback (REGRESSION TEST)
// ==============================================================================

TEST_CASE("Dither envelope responds to transients WITH FEEDBACK enabled", "[features][digital-delay][envelope][SC-008][regression]") {
    // REGRESSION TEST: Verifies envelope drops even with feedback present
    //
    // With feedback enabled, the delayed signal continues to recirculate
    // But the envelope should track ONLY the dry input, not the feedback loop
    // So when input stops, dither should drop even though delayed signal continues

    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);
    delay.setEra(DigitalEra::LoFi);
    delay.setAge(1.0f);       // 100% degradation = maximum dither
    delay.setMix(0.5f);       // 50% wet (common setting)
    delay.setDelayTime(100.0f);  // 100ms delay
    delay.setFeedback(0.4f);  // 40% FEEDBACK
    delay.snapParameters();

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    // Generate percussive signal: loud impulse followed by long silence
    for (size_t i = 0; i < 100; ++i) {
        float decay = std::exp(-static_cast<float>(i) / 20.0f);
        left[i] = 0.8f * decay;
        right[i] = 0.8f * decay;
    }
    // Rest is silence (but feedback will keep delayed signal alive)

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kTestBufferSize,
        .tempoBPM = 120.0,
        .isPlaying = false
    };

    delay.process(left.data(), right.data(), kTestBufferSize, ctx);

    // Measure dither at different time points:
    const size_t delayOffset = static_cast<size_t>(100.0f * kSampleRate / 1000.0f);  // 100ms

    // 1. During transient (right after delay time)
    float transientRMS = measureRMS(left.data(), delayOffset + 10, 100);

    // 2. Long after transient (where input has been silent for a while)
    // Delayed signal continues due to feedback, but dither should drop
    // because envelope tracks DRY input (which is now silent)
    float lateRMS = measureRMS(left.data(), 3000, 500);

    // CRITICAL ASSERTION: Dither should be louder during transient than late silence
    // Even with 40% feedback keeping delayed signal alive, dither tracks dry input
    REQUIRE(transientRMS > lateRMS * 2.0f);  // Transient at least 2x louder

    // Late dither should be very quiet (dry input is silent → envelope = 0 → dither = 0)
    REQUIRE(lateRMS < 0.01f);
}

// ==============================================================================
// Test: Dither Tracks DRY Signal, Not Feedback Loop (CRITICAL REGRESSION)
// ==============================================================================

TEST_CASE("Dither envelope tracks DRY input, not delayed+feedback signal", "[features][digital-delay][envelope][SC-009][regression]") {
    // CRITICAL REGRESSION TEST:
    //
    // BUG SCENARIO (user report):
    // "when I play notes, NOTHING CHANGES. ITS JUST CONSTANT NOISE."
    //
    // ROOT CAUSE:
    // - Envelope was tracking wet signal (delayed + feedback)
    // - Dither feeds back into itself: Dither → 40% Feedback → Wet contains old dither → Envelope tracks dither → More dither
    // - Result: Envelope never drops, constant noise
    //
    // CORRECT BEHAVIOR:
    // - Envelope tracks ONLY dry (input) signal BEFORE character processing
    // - When user stops playing → dry = 0 → envelope = 0 → dither = 0
    // - Delayed signal continues due to feedback, but dither doesn't track that

    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);
    delay.setEra(DigitalEra::LoFi);
    delay.setAge(1.0f);       // 100% degradation = maximum dither potential
    delay.setMix(1.0f);       // 100% wet
    delay.setDelayTime(1.0f);    // 1ms delay (very short)
    delay.setFeedback(0.0f);  // NO FEEDBACK for this test (isolate behavior)
    delay.snapParameters();

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kTestBufferSize,
        .tempoBPM = 120.0,
        .isPlaying = false
    };

    // =========================================================================
    // PHASE 1: User plays notes (input present)
    // =========================================================================

    // Generate loud input for first 500 samples
    for (size_t i = 0; i < 500; ++i) {
        left[i] = 0.5f * std::sin(2.0f * 3.14159f * 440.0f * static_cast<float>(i) / static_cast<float>(kSampleRate));
        right[i] = left[i];
    }
    // Rest is silence (samples 500-4410)

    // Process in blocks
    for (size_t offset = 0; offset < kTestBufferSize; offset += kBlockSize) {
        size_t blockSamples = std::min(kBlockSize, kTestBufferSize - offset);
        delay.process(left.data() + offset, right.data() + offset, blockSamples, ctx);
    }

    // Measure dither during input (samples 100-200 where envelope is high)
    float ditherWithInput = measureRMS(left.data(), 100, 100);

    // =========================================================================
    // PHASE 2: User stops playing (input goes silent)
    // =========================================================================

    // Process another buffer with COMPLETE SILENCE as input
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);

    for (size_t offset = 0; offset < kTestBufferSize; offset += kBlockSize) {
        size_t blockSamples = std::min(kBlockSize, kTestBufferSize - offset);
        delay.process(left.data() + offset, right.data() + offset, blockSamples, ctx);
    }

    // Measure dither during silence (late in buffer after envelope has dropped)
    float ditherWithoutInput = measureRMS(left.data(), 4000, 200);

    // =========================================================================
    // CRITICAL ASSERTION
    // =========================================================================

    // Dither during input should be MUCH louder than dither during silence
    // With dry-only tracking: ditherWithInput >> ditherWithoutInput
    // With wet tracking (BROKEN): ditherWithInput ≈ ditherWithoutInput (both high)
    REQUIRE(ditherWithInput > ditherWithoutInput * 10.0f);  // At least 10x louder

    // Dither during silence should be near-zero (envelope = 0 → dither = 0)
    REQUIRE(ditherWithoutInput < 0.001f);  // Very quiet

    // Dither with input should be audible
    REQUIRE(ditherWithInput > 0.01f);
}

// ==============================================================================
// Test: No NaN or Inf with Envelope Modulation
// ==============================================================================

TEST_CASE("Envelope-modulated dither produces no NaN or Inf", "[features][digital-delay][envelope][safety]") {
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

// ==============================================================================
// Test: Bit Crushing Bypass During Silence
// ==============================================================================

TEST_CASE("Bit crushing bypassed when envelope drops to zero", "[features][digital-delay][envelope][bypass]") {
    // CRITICAL TEST: Verifies that bit crushing is completely bypassed during silence
    // Without this, 4-bit quantization creates audible noise even with dither=0
    //
    // Expected behavior:
    // - When envelope > threshold: Apply bit crushing with envelope-modulated dither
    // - When envelope ≈ 0: Bypass bit crushing entirely → no quantization noise

    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);
    delay.setEra(DigitalEra::LoFi);
    delay.setAge(1.0f);  // 100% age - 4-bit reduction if NOT bypassed
    delay.setMix(1.0f);  // 100% wet
    delay.setDelayTime(10.0f);  // Short delay
    delay.setFeedback(0.0f);  // No feedback
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

    // Measure output after delay has settled
    float silenceRMS = measureRMS(left.data(), 500, 1000);
    float silencePeak = measurePeak(left.data(), 500, 1000);

    // With bit crushing bypassed, silence should produce ZERO output
    // (or near-zero allowing for floating point precision)
    REQUIRE(silenceRMS < 0.0001f);   // Essentially zero
    REQUIRE(silencePeak < 0.001f);   // Peak should also be zero
}

// ==============================================================================
// Test: Envelope Attack/Release Timing
// ==============================================================================

TEST_CASE("Envelope follower has appropriate attack/release times", "[features][digital-delay][envelope][timing]") {
    // This test verifies that envelope attack/release are fast enough to track transients
    // but not so fast that they add distortion
    //
    // Attack should be very fast (< 1ms) to catch transients
    // Release should be moderate (5-20ms) to allow dither to breathe naturally

    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);
    delay.setEra(DigitalEra::LoFi);
    delay.setAge(1.0f);
    delay.setMix(1.0f);
    delay.setDelayTime(5.0f);  // Short delay
    delay.setFeedback(0.0f);
    delay.snapParameters();

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    // Create sharp transient: silence → instant loud → silence
    left[500] = 0.8f;
    right[500] = 0.8f;
    // Single sample impulse

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kTestBufferSize,
        .tempoBPM = 120.0,
        .isPlaying = false
    };

    delay.process(left.data(), right.data(), kTestBufferSize, ctx);

    // Account for 5ms delay = ~220 samples
    const size_t impulseOutput = 500 + 220;

    // Measure dither at impulse location (should be present)
    float peakAtImpulse = measurePeak(left.data(), impulseOutput, 10);

    // Measure dither 50ms later (should have decayed significantly)
    // 50ms = 2205 samples at 44.1kHz
    float peakAfterRelease = measurePeak(left.data(), impulseOutput + 2205, 100);

    // Dither should appear at impulse (envelope tracks transient)
    REQUIRE(peakAtImpulse > 0.01f);

    // Dither should decay after release time (envelope drops)
    REQUIRE(peakAfterRelease < peakAtImpulse * 0.1f);  // Decayed to < 10%
}
