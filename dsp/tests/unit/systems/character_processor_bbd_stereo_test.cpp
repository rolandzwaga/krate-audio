// ==============================================================================
// Regression Test: CharacterProcessor BBD Mode Stereo Processing
// ==============================================================================
// Tests that BBD mode processes left and right channels independently without
// state bleeding between channels.
//
// BUG: The bbdBandwidth_ (MultimodeFilter) and bbdSaturation_ (SaturationProcessor)
// were shared between L/R channels. When processing L then R through the same
// instance, the filter/saturation state from L would affect R, causing different
// (incorrect) artifacts in the right channel - audible as crackling in R only.
//
// FIX: Use separate L/R instances for bbdBandwidth_ and bbdSaturation_, similar
// to how bitCrusherL_/bitCrusherR_ are handled in DigitalVintage mode.
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline
// - Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/systems/character_processor.h>

#include <array>
#include <cmath>
#include <numeric>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Regression Tests for BBD Stereo Processing Bug
// =============================================================================

TEST_CASE("BBD mode: L/R channels should be processed independently",
          "[regression][character-processor][bbd][stereo]") {
    // This test verifies that processing L and R channels doesn't cause
    // state bleeding between them.
    //
    // The bug manifested as:
    // - Crackling only in the right speaker
    // - Different frequency response between L/R
    // - Artifacts that appeared only after L was processed

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    CharacterProcessor processor;
    processor.prepare(kSampleRate, kBlockSize);
    processor.setMode(CharacterMode::BBD);
    processor.setBBDBandwidth(5000.0f);  // Moderate bandwidth for visible filtering
    processor.setBBDSaturation(0.3f);    // Some saturation
    processor.reset();  // Snap smoothers to current values

    SECTION("identical L/R input produces symmetrical output") {
        // Create identical test signal for both channels
        std::vector<float> inputL(kBlockSize);
        std::vector<float> inputR(kBlockSize);

        // 1kHz sine wave
        for (size_t i = 0; i < kBlockSize; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(kSampleRate);
            inputL[i] = 0.5f * std::sin(2.0f * 3.14159f * 1000.0f * phase);
            inputR[i] = inputL[i];  // Identical
        }

        // Process stereo
        processor.processStereo(inputL.data(), inputR.data(), kBlockSize);

        // Calculate RMS for each channel (excluding noise variations)
        float sumL = 0.0f, sumR = 0.0f;
        for (size_t i = 0; i < kBlockSize; ++i) {
            sumL += inputL[i] * inputL[i];
            sumR += inputR[i] * inputR[i];
        }
        float rmsL = std::sqrt(sumL / kBlockSize);
        float rmsR = std::sqrt(sumR / kBlockSize);

        // L and R should have similar RMS (allowing for noise differences)
        // With the bug, R would have significantly different level due to filter state
        INFO("RMS L: " << rmsL << ", RMS R: " << rmsR);
        REQUIRE(rmsL > 0.0f);  // Should produce output
        REQUIRE(rmsR > 0.0f);
        REQUIRE(rmsR == Approx(rmsL).margin(rmsL * 0.25f));  // Within 25%
    }

    SECTION("L and R channels process consistently across multiple blocks") {
        // Process multiple blocks and verify L/R stay consistent
        // This tests that filter/saturation state doesn't bleed between channels

        float maxDiff = 0.0f;

        for (int block = 0; block < 10; ++block) {
            std::vector<float> left(kBlockSize);
            std::vector<float> right(kBlockSize);

            // Same sine wave for both channels
            for (size_t i = 0; i < kBlockSize; ++i) {
                float phase = static_cast<float>(block * kBlockSize + i) / static_cast<float>(kSampleRate);
                float sample = 0.5f * std::sin(2.0f * 3.14159f * 1000.0f * phase);
                left[i] = sample;
                right[i] = sample;
            }

            processor.processStereo(left.data(), right.data(), kBlockSize);

            // Check L/R similarity (allowing for noise differences)
            for (size_t i = 0; i < kBlockSize; ++i) {
                maxDiff = std::max(maxDiff, std::abs(left[i] - right[i]));
            }
        }

        INFO("Max L/R difference across 10 blocks: " << maxDiff);

        // L and R should be similar (difference mainly from noise)
        // With the bug, filter state bleeding would cause much larger differences
        REQUIRE(maxDiff < 0.5f);
    }

    SECTION("impulse response is similar for both channels") {
        // Send an impulse and verify both channels respond similarly
        std::vector<float> impulseL(kBlockSize, 0.0f);
        std::vector<float> impulseR(kBlockSize, 0.0f);
        impulseL[0] = 1.0f;
        impulseR[0] = 1.0f;

        CharacterProcessor impulseProcessor;
        impulseProcessor.prepare(kSampleRate, kBlockSize);
        impulseProcessor.setMode(CharacterMode::BBD);
        impulseProcessor.setBBDBandwidth(5000.0f);
        impulseProcessor.setBBDSaturation(0.3f);
        impulseProcessor.reset();

        impulseProcessor.processStereo(impulseL.data(), impulseR.data(), kBlockSize);

        // Find peak positions and values
        float peakL = 0.0f, peakR = 0.0f;
        size_t peakPosL = 0, peakPosR = 0;

        for (size_t i = 0; i < kBlockSize; ++i) {
            if (std::abs(impulseL[i]) > std::abs(peakL)) {
                peakL = impulseL[i];
                peakPosL = i;
            }
            if (std::abs(impulseR[i]) > std::abs(peakR)) {
                peakR = impulseR[i];
                peakPosR = i;
            }
        }

        INFO("Peak L: " << peakL << " at " << peakPosL);
        INFO("Peak R: " << peakR << " at " << peakPosR);

        // Peaks should be at similar positions (allowing for noise differences)
        REQUIRE(std::abs(static_cast<int>(peakPosL) - static_cast<int>(peakPosR)) <= 5);

        // Peak amplitudes should be similar
        REQUIRE(std::abs(peakL) > 0.1f);  // Should have some output
        REQUIRE(std::abs(peakR) > 0.1f);
        REQUIRE(std::abs(peakR) == Approx(std::abs(peakL)).margin(std::abs(peakL) * 0.3f));
    }
}

TEST_CASE("BBD mode: continuous signal doesn't accumulate channel differences",
          "[regression][character-processor][bbd][stereo]") {
    // Process many blocks and verify L/R don't diverge over time
    // With the bug, state bleeding would cause cumulative differences

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;
    constexpr size_t kNumBlocks = 100;

    CharacterProcessor processor;
    processor.prepare(kSampleRate, kBlockSize);
    processor.setMode(CharacterMode::BBD);
    processor.setBBDBandwidth(5000.0f);
    processor.setBBDSaturation(0.3f);
    processor.reset();  // Snap smoothers

    std::vector<float> maxDiffPerBlock;

    for (size_t block = 0; block < kNumBlocks; ++block) {
        std::vector<float> left(kBlockSize);
        std::vector<float> right(kBlockSize);

        // Identical input for both channels
        for (size_t i = 0; i < kBlockSize; ++i) {
            float phase = static_cast<float>(block * kBlockSize + i) / static_cast<float>(kSampleRate);
            float sample = 0.5f * std::sin(2.0f * 3.14159f * 440.0f * phase);
            left[i] = sample;
            right[i] = sample;
        }

        processor.processStereo(left.data(), right.data(), kBlockSize);

        // Calculate max difference in this block
        float maxDiff = 0.0f;
        for (size_t i = 0; i < kBlockSize; ++i) {
            maxDiff = std::max(maxDiff, std::abs(left[i] - right[i]));
        }
        maxDiffPerBlock.push_back(maxDiff);
    }

    // Calculate average difference over all blocks
    float avgDiff = std::accumulate(maxDiffPerBlock.begin(), maxDiffPerBlock.end(), 0.0f) /
                    static_cast<float>(maxDiffPerBlock.size());

    // Check that difference doesn't grow over time (cumulative state bleeding)
    float firstHalfAvg = std::accumulate(maxDiffPerBlock.begin(),
                                          maxDiffPerBlock.begin() + kNumBlocks/2, 0.0f) /
                         static_cast<float>(kNumBlocks/2);
    float secondHalfAvg = std::accumulate(maxDiffPerBlock.begin() + kNumBlocks/2,
                                           maxDiffPerBlock.end(), 0.0f) /
                          static_cast<float>(kNumBlocks/2);

    INFO("Avg L/R diff first half: " << firstHalfAvg);
    INFO("Avg L/R diff second half: " << secondHalfAvg);
    INFO("Overall avg diff: " << avgDiff);

    // Difference should stay relatively constant, not grow
    // Allow for noise-induced variance but not systematic growth
    REQUIRE(secondHalfAvg < firstHalfAvg * 2.0f + 0.02f);

    // Overall difference should be small (just noise differences)
    REQUIRE(avgDiff < 0.15f);
}

TEST_CASE("BBD mode: RMS levels match between L and R channels",
          "[regression][character-processor][bbd][stereo]") {
    // Test that the bandwidth filter and saturation apply equally to L and R
    // by verifying RMS output levels are similar

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 2048;

    CharacterProcessor processor;
    processor.prepare(kSampleRate, kBlockSize);
    processor.setMode(CharacterMode::BBD);
    processor.setBBDBandwidth(3000.0f);  // Low cutoff to clearly see filtering effect
    processor.setBBDSaturation(0.3f);
    processor.reset();

    // Generate signal with mixed frequencies
    std::vector<float> left(kBlockSize);
    std::vector<float> right(kBlockSize);

    for (size_t i = 0; i < kBlockSize; ++i) {
        float phase = static_cast<float>(i) / static_cast<float>(kSampleRate);
        // Mix of low (500Hz) and high (6000Hz) frequencies
        float lowFreq = 0.3f * std::sin(2.0f * 3.14159f * 500.0f * phase);
        float highFreq = 0.3f * std::sin(2.0f * 3.14159f * 6000.0f * phase);
        left[i] = lowFreq + highFreq;
        right[i] = lowFreq + highFreq;  // Identical
    }

    processor.processStereo(left.data(), right.data(), kBlockSize);

    // Calculate RMS for each channel
    float sumL = 0.0f, sumR = 0.0f;
    for (size_t i = 0; i < kBlockSize; ++i) {
        sumL += left[i] * left[i];
        sumR += right[i] * right[i];
    }
    float rmsL = std::sqrt(sumL / kBlockSize);
    float rmsR = std::sqrt(sumR / kBlockSize);

    INFO("RMS L: " << rmsL << ", RMS R: " << rmsR);

    // Both channels should have similar RMS
    REQUIRE(rmsL > 0.0f);
    REQUIRE(rmsR > 0.0f);
    REQUIRE(rmsR == Approx(rmsL).margin(rmsL * 0.2f));  // Within 20%
}

TEST_CASE("BBD mode: saturation applies equally to both channels",
          "[regression][character-processor][bbd][stereo]") {
    // Test that saturation (bbdSaturation_) applies equally to L and R

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    CharacterProcessor processor;
    processor.prepare(kSampleRate, kBlockSize);
    processor.setMode(CharacterMode::BBD);
    processor.setBBDBandwidth(15000.0f);  // Wide bandwidth (minimal filtering)
    processor.setBBDSaturation(0.8f);     // High saturation
    processor.reset();

    // Generate hot signal that will saturate
    std::vector<float> left(kBlockSize);
    std::vector<float> right(kBlockSize);

    for (size_t i = 0; i < kBlockSize; ++i) {
        float phase = static_cast<float>(i) / static_cast<float>(kSampleRate);
        float sample = 1.5f * std::sin(2.0f * 3.14159f * 440.0f * phase);  // Hot signal
        left[i] = sample;
        right[i] = sample;  // Identical
    }

    processor.processStereo(left.data(), right.data(), kBlockSize);

    // Calculate peak values
    float peakL = 0.0f, peakR = 0.0f;
    for (size_t i = 0; i < kBlockSize; ++i) {
        peakL = std::max(peakL, std::abs(left[i]));
        peakR = std::max(peakR, std::abs(right[i]));
    }

    INFO("Peak L: " << peakL << ", Peak R: " << peakR);

    // Both channels should have similar peak levels
    REQUIRE(peakL > 0.0f);
    REQUIRE(peakR > 0.0f);
    REQUIRE(peakR == Approx(peakL).margin(peakL * 0.2f));  // Within 20%
}
