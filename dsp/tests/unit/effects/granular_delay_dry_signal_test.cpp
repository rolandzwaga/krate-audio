// ==============================================================================
// Granular Delay Dry Signal Diagnostic Test
// ==============================================================================
// Verifies that the dry signal is ALWAYS present at the expected level,
// regardless of granular engine state.
//
// User report: "if I hit notes repeatedly, I sometimes 'miss' notes, so a
// single input note will simply not be audible... I would expect to hear the
// original signal at all times. Note: The dry/wet mix is set to 50%"
//
// This test verifies:
// 1. Dry signal at 50% mix is always present at 50% level
// 2. Output never drops below dry signal level
// 3. Grain scheduler state doesn't affect dry signal
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <krate/dsp/effects/granular_delay.h>
#include <krate/dsp/core/block_context.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using namespace Krate::DSP;
using Catch::Matchers::WithinRel;
using Catch::Matchers::WithinAbs;

namespace {

/// Generate an impulse (single sample of 1.0, rest silence)
std::vector<float> generateImpulse(size_t numSamples, size_t impulsePosition = 0) {
    std::vector<float> buffer(numSamples, 0.0f);
    if (impulsePosition < numSamples) {
        buffer[impulsePosition] = 1.0f;
    }
    return buffer;
}

/// Generate a short "note" (burst of samples)
std::vector<float> generateNote(size_t numSamples, size_t noteStart, size_t noteLength, float amplitude = 1.0f) {
    std::vector<float> buffer(numSamples, 0.0f);
    for (size_t i = noteStart; i < std::min(noteStart + noteLength, numSamples); ++i) {
        // Simple sine burst to simulate a note
        float t = static_cast<float>(i - noteStart) / static_cast<float>(noteLength);
        float envelope = std::sin(t * 3.14159f);  // Simple fade in/out
        buffer[i] = amplitude * envelope * std::sin(t * 20.0f * 3.14159f);  // ~440Hz at 44.1kHz
    }
    return buffer;
}

/// Find minimum and maximum absolute values in output
std::pair<float, float> findMinMaxAbs(const std::vector<float>& input,
                                       const std::vector<float>& output,
                                       float threshold = 0.01f) {
    float minRatio = 1000.0f;
    float maxRatio = 0.0f;

    for (size_t i = 0; i < input.size(); ++i) {
        float inAbs = std::abs(input[i]);
        if (inAbs > threshold) {  // Only consider samples where input is significant
            float outAbs = std::abs(output[i]);
            float ratio = outAbs / inAbs;
            minRatio = std::min(minRatio, ratio);
            maxRatio = std::max(maxRatio, ratio);
        }
    }
    return {minRatio, maxRatio};
}

}  // namespace

// =============================================================================
// Dry Signal Always Present Tests
// =============================================================================

TEST_CASE("Granular delay dry signal is always present at 50% mix",
          "[diagnostic][granular][dry-signal]") {

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 4410;  // 100ms blocks

    GranularDelay delay;
    delay.prepare(kSampleRate);

    // Set to 50% dry/wet
    delay.setDryWet(0.5f);

    // Various granular settings
    delay.setGrainSize(100.0f);
    delay.setDensity(10.0f);
    delay.setDelayTime(200.0f);
    delay.setPitch(0.0f);
    delay.setFeedback(0.0f);  // No feedback for cleaner test

    BlockContext ctx{.sampleRate = kSampleRate, .tempoBPM = 120.0};

    SECTION("Single impulse produces immediate output at >= 50% level") {
        auto inputL = generateImpulse(kBlockSize, 0);
        auto inputR = inputL;
        std::vector<float> outputL(kBlockSize, 0.0f);
        std::vector<float> outputR(kBlockSize, 0.0f);

        delay.process(inputL.data(), inputR.data(),
                     outputL.data(), outputR.data(),
                     kBlockSize, ctx);

        // The dry signal should produce exactly 0.5 * 1.0 = 0.5 at sample 0
        // (wet signal adds to this, so output >= 0.5)
        INFO("Output[0] = " << outputL[0] << ", expected >= 0.5");
        REQUIRE(outputL[0] >= 0.49f);  // Allow small tolerance for smoother
    }

    SECTION("Continuous signal always has output >= 50% of input") {
        // Create a continuous tone
        std::vector<float> inputL(kBlockSize);
        for (size_t i = 0; i < kBlockSize; ++i) {
            inputL[i] = std::sin(static_cast<float>(i) * 0.1f);
        }
        auto inputR = inputL;
        std::vector<float> outputL(kBlockSize, 0.0f);
        std::vector<float> outputR(kBlockSize, 0.0f);

        delay.process(inputL.data(), inputR.data(),
                     outputL.data(), outputR.data(),
                     kBlockSize, ctx);

        // Check that every sample has output >= 50% of input (allowing for phase)
        // We use RMS comparison over small windows
        constexpr size_t windowSize = 100;
        for (size_t start = 0; start < kBlockSize - windowSize; start += windowSize) {
            float inputRMS = 0.0f;
            float outputRMS = 0.0f;
            for (size_t i = start; i < start + windowSize; ++i) {
                inputRMS += inputL[i] * inputL[i];
                outputRMS += outputL[i] * outputL[i];
            }
            inputRMS = std::sqrt(inputRMS / windowSize);
            outputRMS = std::sqrt(outputRMS / windowSize);

            // Output should be at least 45% of input (allowing some tolerance)
            // Since wet signal adds, it should actually be higher
            INFO("Window starting at " << start << ": input RMS = " << inputRMS
                 << ", output RMS = " << outputRMS);
            if (inputRMS > 0.1f) {  // Only check where input is significant
                REQUIRE(outputRMS >= inputRMS * 0.45f);
            }
        }
    }

    SECTION("Repeated notes over time all produce output") {
        // Simulate the user's scenario: hitting notes repeatedly
        constexpr size_t numBlocks = 10;
        constexpr size_t noteLength = 441;  // 10ms note

        // Seed for reproducibility
        delay.seed(42);

        size_t missingNotes = 0;

        for (size_t block = 0; block < numBlocks; ++block) {
            // Create a note at the start of each block
            auto inputL = generateNote(kBlockSize, 0, noteLength, 1.0f);
            auto inputR = inputL;
            std::vector<float> outputL(kBlockSize, 0.0f);
            std::vector<float> outputR(kBlockSize, 0.0f);

            delay.process(inputL.data(), inputR.data(),
                         outputL.data(), outputR.data(),
                         kBlockSize, ctx);

            // Check if the note is present in output (during note duration)
            float inputEnergy = 0.0f;
            float outputEnergy = 0.0f;
            for (size_t i = 0; i < noteLength; ++i) {
                inputEnergy += inputL[i] * inputL[i];
                outputEnergy += outputL[i] * outputL[i];
            }

            float inputRMS = std::sqrt(inputEnergy / noteLength);
            float outputRMS = std::sqrt(outputEnergy / noteLength);

            INFO("Block " << block << ": input RMS = " << inputRMS
                 << ", output RMS = " << outputRMS);

            // Output should be at least 40% of input (dry signal alone would be 50%)
            if (inputRMS > 0.1f && outputRMS < inputRMS * 0.4f) {
                missingNotes++;
                WARN("Block " << block << " has low output - possible missing note");
            }
        }

        // All notes should be present
        REQUIRE(missingNotes == 0);
    }
}

TEST_CASE("Granular dry signal is independent of grain scheduler state",
          "[diagnostic][granular][dry-signal]") {

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 1024;

    GranularDelay delay;
    delay.prepare(kSampleRate);
    delay.setDryWet(0.5f);
    delay.setFeedback(0.0f);
    delay.seed(12345);

    BlockContext ctx{.sampleRate = kSampleRate, .tempoBPM = 120.0};

    SECTION("Dry signal present with very low density (rare grains)") {
        delay.setDensity(1.0f);  // Only 1 grain per second
        delay.setGrainSize(50.0f);

        auto inputL = generateImpulse(kBlockSize, 512);
        auto inputR = inputL;
        std::vector<float> outputL(kBlockSize, 0.0f);
        std::vector<float> outputR(kBlockSize, 0.0f);

        delay.process(inputL.data(), inputR.data(),
                     outputL.data(), outputR.data(),
                     kBlockSize, ctx);

        // Dry signal at 50%: impulse * 0.5 = 0.5
        INFO("Output at impulse position: " << outputL[512]);
        REQUIRE(std::abs(outputL[512]) >= 0.45f);
    }

    SECTION("Dry signal present with very high density (many grains)") {
        delay.setDensity(100.0f);  // 100 grains per second
        delay.setGrainSize(200.0f);  // Long overlapping grains

        auto inputL = generateImpulse(kBlockSize, 512);
        auto inputR = inputL;
        std::vector<float> outputL(kBlockSize, 0.0f);
        std::vector<float> outputR(kBlockSize, 0.0f);

        delay.process(inputL.data(), inputR.data(),
                     outputL.data(), outputR.data(),
                     kBlockSize, ctx);

        // Dry signal should still be present
        INFO("Output at impulse position: " << outputL[512]);
        REQUIRE(std::abs(outputL[512]) >= 0.45f);
    }

    SECTION("Dry signal present with extreme position spray") {
        delay.setDensity(20.0f);
        delay.setPositionSpray(1.0f);  // Maximum randomness
        delay.setDelayTime(500.0f);

        auto inputL = generateImpulse(kBlockSize, 512);
        auto inputR = inputL;
        std::vector<float> outputL(kBlockSize, 0.0f);
        std::vector<float> outputR(kBlockSize, 0.0f);

        delay.process(inputL.data(), inputR.data(),
                     outputL.data(), outputR.data(),
                     kBlockSize, ctx);

        // Dry signal should still be present
        INFO("Output at impulse position: " << outputL[512]);
        REQUIRE(std::abs(outputL[512]) >= 0.45f);
    }
}

TEST_CASE("Granular output level analysis over many blocks",
          "[diagnostic][granular][level]") {

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 4410;  // 100ms
    constexpr size_t kNumBlocks = 50;  // 5 seconds total

    GranularDelay delay;
    delay.prepare(kSampleRate);
    delay.setDryWet(0.5f);
    delay.setFeedback(0.3f);
    delay.setDensity(10.0f);
    delay.setGrainSize(100.0f);
    delay.setDelayTime(200.0f);
    delay.seed(98765);

    BlockContext ctx{.sampleRate = kSampleRate, .tempoBPM = 120.0};

    float minOutputLevel = 1000.0f;
    float maxOutputLevel = 0.0f;
    size_t blocksWithLowOutput = 0;

    for (size_t block = 0; block < kNumBlocks; ++block) {
        // Create constant amplitude input
        std::vector<float> inputL(kBlockSize, 0.5f);  // Constant 0.5 level
        auto inputR = inputL;
        std::vector<float> outputL(kBlockSize, 0.0f);
        std::vector<float> outputR(kBlockSize, 0.0f);

        delay.process(inputL.data(), inputR.data(),
                     outputL.data(), outputR.data(),
                     kBlockSize, ctx);

        // Calculate average output level for this block
        float outputSum = 0.0f;
        for (size_t i = 0; i < kBlockSize; ++i) {
            outputSum += std::abs(outputL[i]);
        }
        float avgOutput = outputSum / kBlockSize;

        minOutputLevel = std::min(minOutputLevel, avgOutput);
        maxOutputLevel = std::max(maxOutputLevel, avgOutput);

        // Expected minimum: dry signal = 0.5 * 0.5 = 0.25
        if (avgOutput < 0.2f) {
            blocksWithLowOutput++;
            WARN("Block " << block << " has low average output: " << avgOutput);
        }
    }

    INFO("Min output level across all blocks: " << minOutputLevel);
    INFO("Max output level across all blocks: " << maxOutputLevel);
    INFO("Blocks with low output: " << blocksWithLowOutput);

    // Dry signal should ensure minimum level is at least 0.2 (50% of 0.5 input, minus some tolerance)
    REQUIRE(minOutputLevel >= 0.15f);
    REQUIRE(blocksWithLowOutput == 0);
}

TEST_CASE("Short repeated notes at various intervals",
          "[diagnostic][granular][dry-signal][notes]") {

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 4410;  // 100ms blocks

    GranularDelay delay;
    delay.prepare(kSampleRate);
    delay.setDryWet(0.5f);  // 50% dry/wet
    delay.setFeedback(0.3f);
    delay.setGrainSize(100.0f);
    delay.setDensity(10.0f);
    delay.setDelayTime(200.0f);
    delay.seed(54321);

    BlockContext ctx{.sampleRate = kSampleRate, .tempoBPM = 120.0};

    SECTION("Short notes (5ms) every 50ms should all be audible") {
        constexpr size_t noteLength = 221;    // ~5ms
        constexpr size_t noteInterval = 2205; // ~50ms (like playing at 20 notes/sec)
        constexpr size_t numNotes = 20;

        size_t missingNotes = 0;

        for (size_t note = 0; note < numNotes; ++note) {
            // Create input with note at the start
            std::vector<float> inputL(noteInterval, 0.0f);
            std::vector<float> inputR(noteInterval, 0.0f);

            // Add impulse-like note
            for (size_t i = 0; i < noteLength && i < noteInterval; ++i) {
                float t = static_cast<float>(i) / noteLength;
                float envelope = 0.5f * (1.0f - std::cos(t * 2.0f * 3.14159f));  // Hann fade
                inputL[i] = envelope;
                inputR[i] = envelope;
            }

            std::vector<float> outputL(noteInterval, 0.0f);
            std::vector<float> outputR(noteInterval, 0.0f);

            delay.process(inputL.data(), inputR.data(),
                         outputL.data(), outputR.data(),
                         noteInterval, ctx);

            // Calculate energy during note duration
            float inputEnergy = 0.0f;
            float outputEnergy = 0.0f;
            for (size_t i = 0; i < noteLength; ++i) {
                inputEnergy += inputL[i] * inputL[i];
                outputEnergy += outputL[i] * outputL[i];
            }

            float inputRMS = std::sqrt(inputEnergy / noteLength);
            float outputRMS = std::sqrt(outputEnergy / noteLength);

            // At 50% dry, we expect at least 40% of input level (some tolerance)
            if (inputRMS > 0.1f && outputRMS < inputRMS * 0.4f) {
                missingNotes++;
                WARN("Note " << note << " may be missing: input RMS = " << inputRMS
                     << ", output RMS = " << outputRMS);
            }
        }

        INFO("Missing notes: " << missingNotes << " out of " << numNotes);
        REQUIRE(missingNotes == 0);
    }

    SECTION("Very short notes (1ms) should still be audible") {
        constexpr size_t noteLength = 44;    // ~1ms
        constexpr size_t noteInterval = 2205; // ~50ms

        // Single note test
        std::vector<float> inputL(noteInterval, 0.0f);
        std::vector<float> inputR(noteInterval, 0.0f);

        // Add very short impulse
        for (size_t i = 0; i < noteLength; ++i) {
            inputL[i] = 1.0f;
            inputR[i] = 1.0f;
        }

        std::vector<float> outputL(noteInterval, 0.0f);
        std::vector<float> outputR(noteInterval, 0.0f);

        delay.process(inputL.data(), inputR.data(),
                     outputL.data(), outputR.data(),
                     noteInterval, ctx);

        // Check that output during note is at least 40% of input
        float maxInput = 0.0f;
        float maxOutput = 0.0f;
        for (size_t i = 0; i < noteLength; ++i) {
            maxInput = std::max(maxInput, std::abs(inputL[i]));
            maxOutput = std::max(maxOutput, std::abs(outputL[i]));
        }

        INFO("Max input: " << maxInput << ", max output: " << maxOutput);
        REQUIRE(maxOutput >= maxInput * 0.4f);
    }
}

TEST_CASE("Dry signal at 100% bypasses granular engine",
          "[diagnostic][granular][bypass]") {

    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 1024;

    GranularDelay delay;
    delay.prepare(kSampleRate);
    delay.setDryWet(0.0f);  // 100% dry, 0% wet
    delay.reset();  // Snap smoothers to current values (including dryWet=0)

    BlockContext ctx{.sampleRate = kSampleRate, .tempoBPM = 120.0};

    // Create test signal
    std::vector<float> inputL(kBlockSize);
    for (size_t i = 0; i < kBlockSize; ++i) {
        inputL[i] = static_cast<float>(i) / kBlockSize;
    }
    auto inputR = inputL;
    std::vector<float> outputL(kBlockSize, 0.0f);
    std::vector<float> outputR(kBlockSize, 0.0f);

    delay.process(inputL.data(), inputR.data(),
                 outputL.data(), outputR.data(),
                 kBlockSize, ctx);

    // At 0% wet, output should exactly equal input
    for (size_t i = 0; i < kBlockSize; ++i) {
        REQUIRE_THAT(outputL[i], WithinAbs(inputL[i], 0.001f));
    }
}
