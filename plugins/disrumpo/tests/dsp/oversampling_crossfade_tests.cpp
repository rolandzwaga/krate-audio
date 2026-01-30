// ==============================================================================
// Oversampling Crossfade Transition Tests (User Story 4)
// ==============================================================================
// Tests for smooth crossfade transitions when oversampling factor changes.
// Verifies 8ms duration, click-free output, equal-power curve, abort-and-restart,
// and hysteresis behavior.
//
// Reference: specs/009-intelligent-oversampling/spec.md
// Tasks: T11.058, T11.059, T11.060, T11.061, T11.062
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/band_processor.h"
#include "dsp/distortion_types.h"
#include "dsp/morph_node.h"
#include "test_helpers/artifact_detection.h"

#include <array>
#include <cmath>
#include <vector>

using namespace Disrumpo;

// =============================================================================
// T11.058: 8ms crossfade duration (SC-005)
// =============================================================================

TEST_CASE("BandProcessor: crossfade duration is approximately 8ms",
          "[oversampling][crossfade][SC-005]") {

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 64;

    BandProcessor bp;
    bp.prepare(kSampleRate, kBlockSize);

    // Set up with a type that has drive > 0 so oversampling is active
    bp.setDistortionType(DistortionType::SoftClip);
    DistortionCommonParams params;
    params.drive = 0.5f;
    params.mix = 1.0f;
    params.toneHz = 4000.0f;
    bp.setDistortionCommonParams(params);
    CHECK(bp.getOversampleFactor() == 2);
    CHECK_FALSE(bp.isOversampleTransitioning());

    // Trigger a type change that changes the factor (2x -> 4x)
    bp.setDistortionType(DistortionType::HardClip);
    CHECK(bp.getOversampleFactor() == 4);
    CHECK(bp.isOversampleTransitioning());

    // Process blocks until crossfade completes
    // 8ms at 44100 Hz = ~353 samples
    constexpr size_t kExpectedSamples = static_cast<size_t>(0.008 * kSampleRate);
    size_t totalProcessed = 0;

    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};

    while (bp.isOversampleTransitioning() && totalProcessed < kExpectedSamples * 3) {
        // Fill with sine wave
        for (size_t i = 0; i < kBlockSize; ++i) {
            float phase = 2.0f * 3.14159f * 440.0f *
                          static_cast<float>(totalProcessed + i) / static_cast<float>(kSampleRate);
            left[i] = 0.3f * std::sin(phase);
            right[i] = 0.3f * std::sin(phase);
        }

        bp.processBlock(left.data(), right.data(), kBlockSize);
        totalProcessed += kBlockSize;
    }

    // Crossfade should have completed within approximately 8ms (with some tolerance
    // for block granularity)
    CHECK_FALSE(bp.isOversampleTransitioning());

    // The total samples processed should be approximately 8ms worth
    // Allow tolerance for block-boundary quantization (up to 2 blocks extra)
    CHECK(totalProcessed <= kExpectedSamples + kBlockSize * 2);
    CHECK(totalProcessed >= kExpectedSamples - kBlockSize);
}

// =============================================================================
// T11.059: Click-free transitions (no sudden amplitude discontinuities)
// =============================================================================

TEST_CASE("BandProcessor: click-free transitions during factor change",
          "[oversampling][crossfade][click-free]") {

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kTotalSamples = 4096;

    BandProcessor bp;
    bp.prepare(kSampleRate, kTotalSamples);

    // Set up with moderate drive
    DistortionCommonParams params;
    params.drive = 0.3f;
    params.mix = 1.0f;
    params.toneHz = 4000.0f;
    bp.setDistortionCommonParams(params);

    // Start with SoftClip (2x)
    bp.setDistortionType(DistortionType::SoftClip);

    // Generate and process a sustained signal first to settle
    std::vector<float> left(kTotalSamples);
    std::vector<float> right(kTotalSamples);
    for (size_t i = 0; i < kTotalSamples; ++i) {
        float phase = 2.0f * 3.14159f * 440.0f *
                      static_cast<float>(i) / static_cast<float>(kSampleRate);
        left[i] = 0.3f * std::sin(phase);
        right[i] = 0.3f * std::sin(phase);
    }
    bp.processBlock(left.data(), right.data(), kTotalSamples);

    // Now trigger factor change (2x -> 4x) and process through transition
    for (size_t i = 0; i < kTotalSamples; ++i) {
        float phase = 2.0f * 3.14159f * 440.0f *
                      static_cast<float>(i) / static_cast<float>(kSampleRate);
        left[i] = 0.3f * std::sin(phase);
        right[i] = 0.3f * std::sin(phase);
    }

    bp.setDistortionType(DistortionType::HardClip);
    bp.processBlock(left.data(), right.data(), kTotalSamples);

    // Use derivative-based click detection
    Krate::DSP::TestUtils::ClickDetectorConfig clickConfig;
    clickConfig.sampleRate = static_cast<float>(kSampleRate);
    clickConfig.frameSize = 512;
    clickConfig.hopSize = 256;
    clickConfig.detectionThreshold = 8.0f; // Conservative threshold
    clickConfig.energyThresholdDb = -60.0f;

    Krate::DSP::TestUtils::ClickDetector detector(clickConfig);
    detector.prepare();

    auto clicks = detector.detect(left.data(), kTotalSamples);

    // Should have zero or very few clicks during the transition
    INFO("Clicks detected: " << clicks.size());
    CHECK(clicks.size() <= 2); // Allow a couple due to distortion itself
}

// =============================================================================
// T11.060: Equal-power crossfade curve (FR-011)
// =============================================================================

TEST_CASE("BandProcessor: crossfade uses equal-power curve",
          "[oversampling][crossfade][FR-011]") {

    // Test the crossfade utility function directly to verify equal-power property
    // oldGain^2 + newGain^2 = 1 throughout transition
    for (int step = 0; step <= 100; ++step) {
        float position = static_cast<float>(step) / 100.0f;
        float fadeOut = 0.0f;
        float fadeIn = 0.0f;
        Krate::DSP::equalPowerGains(position, fadeOut, fadeIn);

        float powerSum = fadeOut * fadeOut + fadeIn * fadeIn;
        INFO("Position: " << position << " fadeOut: " << fadeOut
             << " fadeIn: " << fadeIn << " powerSum: " << powerSum);
        CHECK(powerSum == Catch::Approx(1.0f).margin(0.01f));
    }
}

// =============================================================================
// T11.061: Abort-and-restart behavior (FR-010)
// =============================================================================

TEST_CASE("BandProcessor: abort-and-restart during active crossfade",
          "[oversampling][crossfade][FR-010]") {

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 64;

    BandProcessor bp;
    bp.prepare(kSampleRate, kBlockSize);

    DistortionCommonParams params;
    params.drive = 0.5f;
    params.mix = 1.0f;
    params.toneHz = 4000.0f;
    bp.setDistortionCommonParams(params);

    // Start with 2x type
    bp.setDistortionType(DistortionType::SoftClip);
    CHECK(bp.getOversampleFactor() == 2);

    // Trigger transition to 4x
    bp.setDistortionType(DistortionType::HardClip);
    CHECK(bp.getOversampleFactor() == 4);
    CHECK(bp.isOversampleTransitioning());

    // Process one block to advance crossfade partially
    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};
    for (size_t i = 0; i < kBlockSize; ++i) {
        left[i] = 0.3f;
        right[i] = 0.3f;
    }
    bp.processBlock(left.data(), right.data(), kBlockSize);

    // Now trigger ANOTHER change mid-crossfade (4x -> 1x)
    bp.setDistortionType(DistortionType::Bitcrush);
    CHECK(bp.getOversampleFactor() == 1);
    // Should restart the crossfade
    CHECK(bp.isOversampleTransitioning());

    // Process until crossfade completes - should not crash
    size_t processed = 0;
    while (bp.isOversampleTransitioning() && processed < 44100) {
        for (size_t i = 0; i < kBlockSize; ++i) {
            left[i] = 0.3f;
            right[i] = 0.3f;
        }
        bp.processBlock(left.data(), right.data(), kBlockSize);
        processed += kBlockSize;
    }

    CHECK_FALSE(bp.isOversampleTransitioning());
    CHECK(bp.getOversampleFactor() == 1);
}

// =============================================================================
// T11.062: Hysteresis - no transition within same factor region (FR-017)
// =============================================================================

TEST_CASE("BandProcessor: hysteresis prevents unnecessary transitions",
          "[oversampling][crossfade][FR-017]") {

    constexpr double kSampleRate = 44100.0;

    BandProcessor bp;
    bp.prepare(kSampleRate, 512);

    SECTION("changing between types with same factor does not trigger crossfade") {
        bp.setDistortionType(DistortionType::SoftClip);
        CHECK(bp.getOversampleFactor() == 2);
        CHECK_FALSE(bp.isOversampleTransitioning());

        // Change to another 2x type - same factor, no crossfade
        bp.setDistortionType(DistortionType::Tube);
        CHECK(bp.getOversampleFactor() == 2);
        CHECK_FALSE(bp.isOversampleTransitioning());

        // Change to another 2x type
        bp.setDistortionType(DistortionType::Tape);
        CHECK(bp.getOversampleFactor() == 2);
        CHECK_FALSE(bp.isOversampleTransitioning());
    }

    SECTION("morph within same factor region does not trigger crossfade") {
        // Set up morph between two 4x types
        std::array<MorphNode, kMaxMorphNodes> nodes = {{
            MorphNode(0, 0.0f, 0.0f, DistortionType::HardClip),  // 4x
            MorphNode(1, 1.0f, 0.0f, DistortionType::Fuzz),      // 4x
            MorphNode(2, 0.0f, 1.0f, DistortionType::HardClip),
            MorphNode(3, 1.0f, 1.0f, DistortionType::Fuzz)
        }};
        bp.setMorphNodes(nodes, 2);
        bp.setMorphMode(MorphMode::Linear1D);
        bp.setMorphPosition(0.0f, 0.0f);
        CHECK(bp.getOversampleFactor() == 4);

        // Process enough audio to complete any initial crossfade
        // (node setup may trigger crossfade from default factor to 4x)
        constexpr size_t kFlushSize = 512;
        std::array<float, kFlushSize> flushL{};
        std::array<float, kFlushSize> flushR{};
        for (int i = 0; i < 10; ++i) {
            bp.processBlock(flushL.data(), flushR.data(), kFlushSize);
        }
        CHECK_FALSE(bp.isOversampleTransitioning());

        // Move morph position - both nodes are 4x, so factor stays 4
        bp.setMorphPosition(0.5f, 0.0f);
        CHECK(bp.getOversampleFactor() == 4);
        CHECK_FALSE(bp.isOversampleTransitioning());

        bp.setMorphPosition(1.0f, 0.0f);
        CHECK(bp.getOversampleFactor() == 4);
        CHECK_FALSE(bp.isOversampleTransitioning());
    }

    SECTION("changing to different factor DOES trigger crossfade") {
        bp.setDistortionType(DistortionType::SoftClip);
        CHECK(bp.getOversampleFactor() == 2);
        CHECK_FALSE(bp.isOversampleTransitioning());

        // Change to 4x type
        bp.setDistortionType(DistortionType::HardClip);
        CHECK(bp.getOversampleFactor() == 4);
        CHECK(bp.isOversampleTransitioning());
    }
}
