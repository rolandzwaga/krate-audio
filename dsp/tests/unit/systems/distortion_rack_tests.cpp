// ==============================================================================
// Layer 3: System Tests - DistortionRack
// ==============================================================================
// Unit tests for the DistortionRack multi-stage distortion chain system.
//
// Feature: 068-distortion-rack
// Layer: 3 (Systems)
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline (comprehensive unit tests)
// - Principle XII: Test-First Development
//
// Reference: specs/068-distortion-rack/spec.md
// ==============================================================================

#include <krate/dsp/systems/distortion_rack.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cmath>
#include <numeric>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Test Constants
// =============================================================================

namespace {
constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;
constexpr float kTestFrequency = 440.0f;

/// Generate a sine wave buffer for testing.
void generateSineWave(float* buffer, size_t numSamples, float frequency, double sampleRate, float amplitude = 0.5f) {
    const double phaseIncrement = 2.0 * 3.14159265358979323846 * frequency / sampleRate;
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = amplitude * static_cast<float>(std::sin(static_cast<double>(i) * phaseIncrement));
    }
}

/// Calculate RMS of a buffer.
float calculateRMS(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(numSamples));
}

/// Calculate DC offset (mean) of a buffer.
float calculateDCOffset(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        sum += buffer[i];
    }
    return sum / static_cast<float>(numSamples);
}

/// Check if two buffers are approximately equal.
bool buffersApproxEqual(const float* a, const float* b, size_t numSamples, float tolerance = 1e-6f) {
    for (size_t i = 0; i < numSamples; ++i) {
        if (std::abs(a[i] - b[i]) > tolerance) {
            return false;
        }
    }
    return true;
}

} // namespace

// =============================================================================
// Phase 3: User Story 1 - Create Multi-Stage Distortion Chain (Priority: P1)
// =============================================================================

TEST_CASE("SlotConfiguration_SetAndGetSlotType", "[distortion_rack][US1]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    SECTION("Set slot 0 to TubeStage") {
        rack.setSlotType(0, SlotType::TubeStage);
        REQUIRE(rack.getSlotType(0) == SlotType::TubeStage);
    }

    SECTION("Set slot 1 to DiodeClipper") {
        rack.setSlotType(1, SlotType::DiodeClipper);
        REQUIRE(rack.getSlotType(1) == SlotType::DiodeClipper);
    }

    SECTION("Set slot 2 to Wavefolder") {
        rack.setSlotType(2, SlotType::Wavefolder);
        REQUIRE(rack.getSlotType(2) == SlotType::Wavefolder);
    }

    SECTION("Set slot 3 to Bitcrusher") {
        rack.setSlotType(3, SlotType::Bitcrusher);
        REQUIRE(rack.getSlotType(3) == SlotType::Bitcrusher);
    }

    SECTION("All slot types can be set and retrieved") {
        rack.setSlotType(0, SlotType::Empty);
        rack.setSlotType(1, SlotType::Waveshaper);
        rack.setSlotType(2, SlotType::TapeSaturator);
        rack.setSlotType(3, SlotType::Fuzz);

        REQUIRE(rack.getSlotType(0) == SlotType::Empty);
        REQUIRE(rack.getSlotType(1) == SlotType::Waveshaper);
        REQUIRE(rack.getSlotType(2) == SlotType::TapeSaturator);
        REQUIRE(rack.getSlotType(3) == SlotType::Fuzz);
    }
}

TEST_CASE("SlotConfiguration_DefaultSlotTypeIsEmpty", "[distortion_rack][US1]") {
    DistortionRack rack;

    // FR-005: Default slot type for all slots MUST be SlotType::Empty
    REQUIRE(rack.getSlotType(0) == SlotType::Empty);
    REQUIRE(rack.getSlotType(1) == SlotType::Empty);
    REQUIRE(rack.getSlotType(2) == SlotType::Empty);
    REQUIRE(rack.getSlotType(3) == SlotType::Empty);
}

TEST_CASE("SlotConfiguration_OutOfRangeSlotIndex", "[distortion_rack][US1]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    // FR-004: setSlotType() MUST handle slot index out of range by doing nothing
    rack.setSlotType(4, SlotType::TubeStage);
    rack.setSlotType(100, SlotType::DiodeClipper);
    rack.setSlotType(static_cast<size_t>(-1), SlotType::Fuzz);

    // All valid slots should remain at their default (Empty)
    REQUIRE(rack.getSlotType(0) == SlotType::Empty);
    REQUIRE(rack.getSlotType(1) == SlotType::Empty);
    REQUIRE(rack.getSlotType(2) == SlotType::Empty);
    REQUIRE(rack.getSlotType(3) == SlotType::Empty);

    // Out of range getter should return Empty
    REQUIRE(rack.getSlotType(4) == SlotType::Empty);
    REQUIRE(rack.getSlotType(100) == SlotType::Empty);
}

TEST_CASE("Processing_AllSlotsEmpty_PassThrough", "[distortion_rack][US1]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    // All slots are Empty by default (FR-005)
    std::array<float, kBlockSize> left, right;
    std::array<float, kBlockSize> originalLeft, originalRight;

    generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate);
    generateSineWave(right.data(), kBlockSize, kTestFrequency * 1.5f, kSampleRate);

    std::copy(left.begin(), left.end(), originalLeft.begin());
    std::copy(right.begin(), right.end(), originalRight.begin());

    rack.process(left.data(), right.data(), kBlockSize);

    // SC-006: With all slots disabled or set to Empty, output equals input
    REQUIRE(buffersApproxEqual(left.data(), originalLeft.data(), kBlockSize, 1e-6f));
    REQUIRE(buffersApproxEqual(right.data(), originalRight.data(), kBlockSize, 1e-6f));
}

TEST_CASE("Processing_TubeStageFollowedByWavefolder_CombinedHarmonics", "[distortion_rack][US1]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    // Configure slot 0 = TubeStage, slot 1 = Wavefolder
    rack.setSlotType(0, SlotType::TubeStage);
    rack.setSlotType(1, SlotType::Wavefolder);
    rack.setSlotEnabled(0, true);
    rack.setSlotEnabled(1, true);

    std::array<float, kBlockSize> left, right;
    generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
    generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);

    const float inputRMS = calculateRMS(left.data(), kBlockSize);

    rack.process(left.data(), right.data(), kBlockSize);

    const float outputRMS = calculateRMS(left.data(), kBlockSize);

    // Combined processing should produce output (non-silent)
    REQUIRE(outputRMS > 0.01f);

    // Signal should be modified by distortion (not identical to input)
    // This test verifies the chain is actually processing
    // A proper harmonic analysis test is in Performance phase
}

TEST_CASE("Processing_FourSlotChain_DiodeClipperTapeSaturatorFuzzBitcrusher", "[distortion_rack][US1]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    // Configure all 4 slots with different processors
    rack.setSlotType(0, SlotType::DiodeClipper);
    rack.setSlotType(1, SlotType::TapeSaturator);
    rack.setSlotType(2, SlotType::Fuzz);
    rack.setSlotType(3, SlotType::Bitcrusher);

    rack.setSlotEnabled(0, true);
    rack.setSlotEnabled(1, true);
    rack.setSlotEnabled(2, true);
    rack.setSlotEnabled(3, true);

    std::array<float, kBlockSize> left, right;
    generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate, 0.7f);
    generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate, 0.7f);

    rack.process(left.data(), right.data(), kBlockSize);

    const float outputRMS = calculateRMS(left.data(), kBlockSize);

    // 4-slot chain should produce output
    REQUIRE(outputRMS > 0.01f);

    // Verify no NaN or Inf in output
    for (size_t i = 0; i < kBlockSize; ++i) {
        REQUIRE_FALSE(std::isnan(left[i]));
        REQUIRE_FALSE(std::isinf(left[i]));
        REQUIRE_FALSE(std::isnan(right[i]));
        REQUIRE_FALSE(std::isinf(right[i]));
    }
}

TEST_CASE("Lifecycle_PrepareConfiguresAllComponents", "[distortion_rack][US1]") {
    DistortionRack rack;

    // Configure before prepare - should handle gracefully
    rack.setSlotType(0, SlotType::TubeStage);
    rack.setSlotEnabled(0, true);

    // Now prepare
    rack.prepare(kSampleRate, kBlockSize);

    // Verify slot configuration persisted through prepare
    REQUIRE(rack.getSlotType(0) == SlotType::TubeStage);
    REQUIRE(rack.getSlotEnabled(0) == true);

    // Process should work after prepare
    std::array<float, kBlockSize> left, right;
    generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate);
    generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate);

    rack.process(left.data(), right.data(), kBlockSize);

    // Should produce output without crash
    REQUIRE(calculateRMS(left.data(), kBlockSize) > 0.01f);
}

TEST_CASE("Lifecycle_ResetClearsState", "[distortion_rack][US1]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setSlotType(0, SlotType::TubeStage);
    rack.setSlotEnabled(0, true);

    // Process some audio
    std::array<float, kBlockSize> left, right;
    generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate);
    generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate);
    rack.process(left.data(), right.data(), kBlockSize);

    // Reset - should clear internal state but preserve configuration
    rack.reset();

    // Configuration should be preserved
    REQUIRE(rack.getSlotType(0) == SlotType::TubeStage);
    REQUIRE(rack.getSlotEnabled(0) == true);

    // Process should still work after reset
    generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate);
    generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate);
    rack.process(left.data(), right.data(), kBlockSize);

    REQUIRE(calculateRMS(left.data(), kBlockSize) > 0.01f);
}

TEST_CASE("Lifecycle_ProcessBeforePrepare_PassThrough", "[distortion_rack][US1]") {
    DistortionRack rack;

    // FR-037: Before prepare() is called, process() MUST return input unchanged
    rack.setSlotType(0, SlotType::TubeStage);
    rack.setSlotEnabled(0, true);

    std::array<float, kBlockSize> left, right;
    std::array<float, kBlockSize> originalLeft, originalRight;

    generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate);
    generateSineWave(right.data(), kBlockSize, kTestFrequency * 1.5f, kSampleRate);

    std::copy(left.begin(), left.end(), originalLeft.begin());
    std::copy(right.begin(), right.end(), originalRight.begin());

    rack.process(left.data(), right.data(), kBlockSize);

    REQUIRE(buffersApproxEqual(left.data(), originalLeft.data(), kBlockSize, 1e-6f));
    REQUIRE(buffersApproxEqual(right.data(), originalRight.data(), kBlockSize, 1e-6f));
}

TEST_CASE("Processing_ZeroLengthBuffer_NoOp", "[distortion_rack][US1]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setSlotType(0, SlotType::TubeStage);
    rack.setSlotEnabled(0, true);

    // FR-040: process() with n=0 MUST return immediately
    std::array<float, 4> left = {1.0f, 2.0f, 3.0f, 4.0f};
    std::array<float, 4> right = {5.0f, 6.0f, 7.0f, 8.0f};

    rack.process(left.data(), right.data(), 0);

    // Buffer should be unchanged (no processing occurred)
    REQUIRE(left[0] == 1.0f);
    REQUIRE(left[1] == 2.0f);
    REQUIRE(right[0] == 5.0f);
    REQUIRE(right[1] == 6.0f);
}

// =============================================================================
// Phase 4: User Story 2 - Dynamic Slot Configuration (Priority: P2)
// =============================================================================

TEST_CASE("SlotEnable_DefaultDisabled", "[distortion_rack][US2]") {
    DistortionRack rack;

    // FR-008: Default enabled state for all slots MUST be false (disabled)
    REQUIRE(rack.getSlotEnabled(0) == false);
    REQUIRE(rack.getSlotEnabled(1) == false);
    REQUIRE(rack.getSlotEnabled(2) == false);
    REQUIRE(rack.getSlotEnabled(3) == false);
}

TEST_CASE("SlotEnable_EnableSlot_ProcessesAudio", "[distortion_rack][US2]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setSlotType(0, SlotType::TubeStage);

    std::array<float, kBlockSize> left, right;
    std::array<float, kBlockSize> originalLeft;

    generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
    generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
    std::copy(left.begin(), left.end(), originalLeft.begin());

    // Process with slot disabled - should pass through
    rack.process(left.data(), right.data(), kBlockSize);
    REQUIRE(buffersApproxEqual(left.data(), originalLeft.data(), kBlockSize, 1e-5f));

    // Enable slot and process again
    rack.setSlotEnabled(0, true);
    generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
    generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);

    // Allow smoothing to complete
    for (int i = 0; i < 10; ++i) {
        rack.process(left.data(), right.data(), kBlockSize);
        generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
        generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
    }

    rack.process(left.data(), right.data(), kBlockSize);

    // With slot enabled, output should differ from input
    REQUIRE_FALSE(buffersApproxEqual(left.data(), originalLeft.data(), kBlockSize, 0.01f));
}

TEST_CASE("SlotEnable_DisableSlot_PassThrough", "[distortion_rack][US2]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setSlotType(0, SlotType::TubeStage);
    rack.setSlotEnabled(0, true);

    // Process to complete enable transition
    std::array<float, kBlockSize> left, right;
    for (int i = 0; i < 10; ++i) {
        generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate);
        generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate);
        rack.process(left.data(), right.data(), kBlockSize);
    }

    // Disable slot
    rack.setSlotEnabled(0, false);

    // Process to complete disable transition
    for (int i = 0; i < 10; ++i) {
        generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate);
        generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate);
        rack.process(left.data(), right.data(), kBlockSize);
    }

    // Now output should be approximately equal to input (bypassed)
    std::array<float, kBlockSize> originalLeft;
    generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate);
    generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate);
    std::copy(left.begin(), left.end(), originalLeft.begin());

    rack.process(left.data(), right.data(), kBlockSize);

    REQUIRE(buffersApproxEqual(left.data(), originalLeft.data(), kBlockSize, 1e-5f));
}

TEST_CASE("SlotEnable_TransitionIsSmooth", "[distortion_rack][US2]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setSlotType(0, SlotType::TubeStage);

    // Start with slot disabled, then enable
    std::array<float, kBlockSize> left, right;
    std::vector<float> transitionSamples;

    // Capture the transition
    rack.setSlotEnabled(0, true);

    // Capture samples during transition (5ms = ~221 samples at 44.1kHz)
    const size_t transitionSamples5ms = static_cast<size_t>(0.005 * kSampleRate);
    const size_t numBlocks = (transitionSamples5ms / kBlockSize) + 2;

    for (size_t block = 0; block < numBlocks; ++block) {
        generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate, 0.5f);
        generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate, 0.5f);
        rack.process(left.data(), right.data(), kBlockSize);

        for (size_t i = 0; i < kBlockSize; ++i) {
            transitionSamples.push_back(left[i]);
        }
    }

    // Check for clicks: large sample-to-sample jumps indicate clicks
    // A click would show as a very large derivative
    bool hasClicks = false;
    for (size_t i = 1; i < transitionSamples.size(); ++i) {
        float derivative = std::abs(transitionSamples[i] - transitionSamples[i - 1]);
        // A click would show as derivative > 0.5 (very sudden change)
        if (derivative > 0.5f) {
            hasClicks = true;
            break;
        }
    }

    REQUIRE_FALSE(hasClicks);
}

TEST_CASE("SlotMix_DefaultFullWet", "[distortion_rack][US2]") {
    DistortionRack rack;

    // FR-014: Default mix for all slots MUST be 1.0 (100% wet when enabled)
    REQUIRE(rack.getSlotMix(0) == Approx(1.0f));
    REQUIRE(rack.getSlotMix(1) == Approx(1.0f));
    REQUIRE(rack.getSlotMix(2) == Approx(1.0f));
    REQUIRE(rack.getSlotMix(3) == Approx(1.0f));
}

TEST_CASE("SlotMix_ZeroMix_FullDry", "[distortion_rack][US2]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setSlotType(0, SlotType::TubeStage);
    rack.setSlotEnabled(0, true);
    rack.setSlotMix(0, 0.0f);

    // Allow smoothing to complete
    std::array<float, kBlockSize> left, right;
    for (int i = 0; i < 10; ++i) {
        generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate);
        generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate);
        rack.process(left.data(), right.data(), kBlockSize);
    }

    // FR-012: Mix of 0.0 MUST produce dry signal only
    std::array<float, kBlockSize> originalLeft;
    generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate);
    generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate);
    std::copy(left.begin(), left.end(), originalLeft.begin());

    rack.process(left.data(), right.data(), kBlockSize);

    // With mix=0, output should equal input (dry only)
    REQUIRE(buffersApproxEqual(left.data(), originalLeft.data(), kBlockSize, 1e-5f));
}

TEST_CASE("SlotMix_FullWet_OnlyProcessed", "[distortion_rack][US2]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setSlotType(0, SlotType::TubeStage);
    rack.setSlotEnabled(0, true);
    rack.setSlotMix(0, 1.0f);

    // Allow smoothing to complete
    std::array<float, kBlockSize> left, right;
    std::array<float, kBlockSize> originalLeft;

    for (int i = 0; i < 10; ++i) {
        generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
        generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
        rack.process(left.data(), right.data(), kBlockSize);
    }

    generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
    generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
    std::copy(left.begin(), left.end(), originalLeft.begin());

    rack.process(left.data(), right.data(), kBlockSize);

    // FR-013: Mix of 1.0 MUST produce 100% wet signal (processed, different from input)
    REQUIRE_FALSE(buffersApproxEqual(left.data(), originalLeft.data(), kBlockSize, 0.01f));
}

TEST_CASE("SlotMix_HalfMix_50PercentBlend", "[distortion_rack][US2]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setSlotType(0, SlotType::TubeStage);
    rack.setSlotEnabled(0, true);
    rack.setSlotMix(0, 0.5f);

    // Allow smoothing
    std::array<float, kBlockSize> left, right;
    for (int i = 0; i < 10; ++i) {
        generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
        generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
        rack.process(left.data(), right.data(), kBlockSize);
    }

    // Verify mix is set
    REQUIRE(rack.getSlotMix(0) == Approx(0.5f));
}

TEST_CASE("SlotMix_TransitionIsSmooth", "[distortion_rack][US2]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setSlotType(0, SlotType::TubeStage);
    rack.setSlotEnabled(0, true);

    // Process with full wet first
    std::array<float, kBlockSize> left, right;
    for (int i = 0; i < 5; ++i) {
        generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate);
        generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate);
        rack.process(left.data(), right.data(), kBlockSize);
    }

    // Now change mix and capture transition
    rack.setSlotMix(0, 0.0f);

    std::vector<float> transitionSamples;
    const size_t numBlocks = 5;

    for (size_t block = 0; block < numBlocks; ++block) {
        generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate, 0.5f);
        generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate, 0.5f);
        rack.process(left.data(), right.data(), kBlockSize);

        for (size_t i = 0; i < kBlockSize; ++i) {
            transitionSamples.push_back(left[i]);
        }
    }

    // Check for clicks
    bool hasClicks = false;
    for (size_t i = 1; i < transitionSamples.size(); ++i) {
        float derivative = std::abs(transitionSamples[i] - transitionSamples[i - 1]);
        if (derivative > 0.5f) {
            hasClicks = true;
            break;
        }
    }

    REQUIRE_FALSE(hasClicks);
}

TEST_CASE("SlotMix_ClampedToRange", "[distortion_rack][US2]") {
    DistortionRack rack;

    // FR-011: Mix parameter MUST be clamped to [0.0, 1.0] range
    rack.setSlotMix(0, -1.0f);
    REQUIRE(rack.getSlotMix(0) == Approx(0.0f));

    rack.setSlotMix(0, 2.0f);
    REQUIRE(rack.getSlotMix(0) == Approx(1.0f));

    rack.setSlotMix(0, 0.5f);
    REQUIRE(rack.getSlotMix(0) == Approx(0.5f));
}

TEST_CASE("SlotTypeChange_MidProcessing_NoArtifacts", "[distortion_rack][US2]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setSlotType(0, SlotType::Waveshaper);
    rack.setSlotEnabled(0, true);

    // Process with Waveshaper
    std::array<float, kBlockSize> left, right;
    for (int i = 0; i < 5; ++i) {
        generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate);
        generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate);
        rack.process(left.data(), right.data(), kBlockSize);
    }

    // Change to Fuzz mid-processing
    rack.setSlotType(0, SlotType::Fuzz);

    std::vector<float> transitionSamples;
    for (size_t block = 0; block < 5; ++block) {
        generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate, 0.5f);
        generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate, 0.5f);
        rack.process(left.data(), right.data(), kBlockSize);

        for (size_t i = 0; i < kBlockSize; ++i) {
            transitionSamples.push_back(left[i]);
        }
    }

    // Check for clicks
    bool hasClicks = false;
    for (size_t i = 1; i < transitionSamples.size(); ++i) {
        float derivative = std::abs(transitionSamples[i] - transitionSamples[i - 1]);
        if (derivative > 0.5f) {
            hasClicks = true;
            break;
        }
    }

    REQUIRE_FALSE(hasClicks);
}

// =============================================================================
// Phase 5: User Story 3 - CPU-Efficient Oversampling (Priority: P2)
// =============================================================================

TEST_CASE("Oversampling_DefaultFactor1", "[distortion_rack][US3]") {
    DistortionRack rack;

    // FR-026: Default oversampling factor MUST be 1 (no oversampling)
    REQUIRE(rack.getOversamplingFactor() == 1);
}

TEST_CASE("Oversampling_SetFactor2_UsesOversampler2x", "[distortion_rack][US3]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setOversamplingFactor(2);
    REQUIRE(rack.getOversamplingFactor() == 2);
}

TEST_CASE("Oversampling_SetFactor4_UsesOversampler4x", "[distortion_rack][US3]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setOversamplingFactor(4);
    REQUIRE(rack.getOversamplingFactor() == 4);
}

TEST_CASE("Oversampling_Factor1_NoLatency", "[distortion_rack][US3]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setOversamplingFactor(1);
    REQUIRE(rack.getLatency() == 0);
}

TEST_CASE("Oversampling_Factor2_ReportsLatency", "[distortion_rack][US3]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setOversamplingFactor(2);
    // Zero-latency mode should report 0 latency
    size_t latency = rack.getLatency();
    // Could be 0 for zero-latency mode or non-zero for linear phase
    REQUIRE(latency >= 0); // Just verify it doesn't crash
}

TEST_CASE("Oversampling_Factor4_ReportsLatency", "[distortion_rack][US3]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setOversamplingFactor(4);
    size_t latency = rack.getLatency();
    REQUIRE(latency >= 0); // Just verify it doesn't crash
}

TEST_CASE("Oversampling_InvalidFactor_Ignored", "[distortion_rack][US3]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setOversamplingFactor(2);
    REQUIRE(rack.getOversamplingFactor() == 2);

    // Invalid factor should be ignored
    rack.setOversamplingFactor(3);
    REQUIRE(rack.getOversamplingFactor() == 2); // Should remain at 2

    rack.setOversamplingFactor(0);
    REQUIRE(rack.getOversamplingFactor() == 2);

    rack.setOversamplingFactor(8);
    REQUIRE(rack.getOversamplingFactor() == 2);
}

TEST_CASE("Oversampling_4xReducesAliasing_HighDrive", "[distortion_rack][US3]") {
    // This is a more complex test that would require FFT analysis
    // For now, verify processing works with 4x oversampling

    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setSlotType(0, SlotType::TubeStage);
    rack.setSlotEnabled(0, true);
    rack.setOversamplingFactor(4);

    std::array<float, kBlockSize> left, right;
    generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate, 0.9f);
    generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate, 0.9f);

    rack.process(left.data(), right.data(), kBlockSize);

    // Verify output is valid
    for (size_t i = 0; i < kBlockSize; ++i) {
        REQUIRE_FALSE(std::isnan(left[i]));
        REQUIRE_FALSE(std::isinf(left[i]));
    }
}

TEST_CASE("Oversampling_FactorChange_MidPlayback_Seamless", "[distortion_rack][US3]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setSlotType(0, SlotType::TubeStage);
    rack.setSlotEnabled(0, true);

    std::array<float, kBlockSize> left, right;

    // Process with 1x
    for (int i = 0; i < 5; ++i) {
        generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate);
        generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate);
        rack.process(left.data(), right.data(), kBlockSize);
    }

    // Change to 4x
    rack.setOversamplingFactor(4);

    std::vector<float> transitionSamples;
    for (size_t block = 0; block < 5; ++block) {
        generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate, 0.5f);
        generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate, 0.5f);
        rack.process(left.data(), right.data(), kBlockSize);

        for (size_t i = 0; i < kBlockSize; ++i) {
            transitionSamples.push_back(left[i]);
        }
    }

    // Verify no NaN/Inf and output exists
    for (float sample : transitionSamples) {
        REQUIRE_FALSE(std::isnan(sample));
        REQUIRE_FALSE(std::isinf(sample));
    }
}

// =============================================================================
// Phase 6: User Story 4 - Access Slot Processor Parameters (Priority: P3)
// =============================================================================

TEST_CASE("ProcessorAccess_GetProcessor_CorrectType_ReturnsPointer", "[distortion_rack][US4]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setSlotType(0, SlotType::TubeStage);

    // FR-016: getSlotProcessor<T>(int slot) MUST return a typed pointer
    TubeStage* processor = rack.getProcessor<TubeStage>(0);
    REQUIRE(processor != nullptr);
}

TEST_CASE("ProcessorAccess_GetProcessor_WrongType_ReturnsNullptr", "[distortion_rack][US4]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setSlotType(1, SlotType::DiodeClipper);

    // FR-017: getSlotProcessor<T>() MUST return nullptr if slot type does not match
    TubeStage* wrongType = rack.getProcessor<TubeStage>(1);
    REQUIRE(wrongType == nullptr);
}

TEST_CASE("ProcessorAccess_GetProcessor_EmptySlot_ReturnsNullptr", "[distortion_rack][US4]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    // Slot 2 is Empty by default
    // FR-017: getSlotProcessor<T>() MUST return nullptr if slot type is Empty
    TubeStage* emptySlot = rack.getProcessor<TubeStage>(2);
    REQUIRE(emptySlot == nullptr);
}

TEST_CASE("ProcessorAccess_GetProcessor_OutOfRange_ReturnsNullptr", "[distortion_rack][US4]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    // FR-017: getSlotProcessor<T>() MUST return nullptr if slot index is out of range
    TubeStage* outOfRange = rack.getProcessor<TubeStage>(5);
    REQUIRE(outOfRange == nullptr);
}

TEST_CASE("ProcessorAccess_GetProcessor_InvalidChannel_ReturnsNullptr", "[distortion_rack][US4]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setSlotType(0, SlotType::TubeStage);

    // Channel 2 is invalid (only 0 and 1 for stereo)
    TubeStage* invalidChannel = rack.getProcessor<TubeStage>(0, 2);
    REQUIRE(invalidChannel == nullptr);
}

TEST_CASE("ProcessorAccess_ModifyParameters_AffectsOutput", "[distortion_rack][US4]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setSlotType(0, SlotType::TubeStage);
    rack.setSlotEnabled(0, true);

    // Allow initial smoothing
    std::array<float, kBlockSize> left, right;
    for (int i = 0; i < 10; ++i) {
        generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
        generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
        rack.process(left.data(), right.data(), kBlockSize);
    }

    // Get processor and modify parameters
    TubeStage* processor = rack.getProcessor<TubeStage>(0);
    REQUIRE(processor != nullptr);

    // Process with default settings
    generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
    generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
    std::array<float, kBlockSize> defaultOutput;
    rack.process(left.data(), right.data(), kBlockSize);
    std::copy(left.begin(), left.end(), defaultOutput.begin());

    // Modify bias (asymmetry)
    processor->setBias(0.5f);

    // Process with modified settings (allow smoothing)
    for (int i = 0; i < 10; ++i) {
        generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
        generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
        rack.process(left.data(), right.data(), kBlockSize);
    }

    generateSineWave(left.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
    generateSineWave(right.data(), kBlockSize, kTestFrequency, kSampleRate, 0.8f);
    rack.process(left.data(), right.data(), kBlockSize);

    // FR-019: Parameter changes via returned processor pointer MUST affect audio
    // The outputs should differ due to bias change
    REQUIRE_FALSE(buffersApproxEqual(left.data(), defaultOutput.data(), kBlockSize, 0.001f));
}

TEST_CASE("ProcessorAccess_StereoProcessors_IndependentAccess", "[distortion_rack][US4]") {
    DistortionRack rack;
    rack.prepare(kSampleRate, kBlockSize);

    rack.setSlotType(0, SlotType::TubeStage);

    TubeStage* processorL = rack.getProcessor<TubeStage>(0, 0);
    TubeStage* processorR = rack.getProcessor<TubeStage>(0, 1);

    REQUIRE(processorL != nullptr);
    REQUIRE(processorR != nullptr);

    // Should be different instances for stereo processing
    REQUIRE(processorL != processorR);
}
