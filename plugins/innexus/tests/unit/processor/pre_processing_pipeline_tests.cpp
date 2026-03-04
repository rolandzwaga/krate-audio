// ==============================================================================
// Pre-Processing Pipeline Tests (Phase 2)
// ==============================================================================
// Tests that the analysis signal pre-processing pipeline correctly removes DC
// offset, applies high-pass filtering, gates noise, suppresses transients, and
// operates on a separate analysis buffer without modifying audio output.
//
// Feature: 115-innexus-m1-core-instrument
// User Story: US3 (Graceful Handling of Difficult Source Material)
// Requirements: FR-005, FR-006, FR-007, FR-008, FR-009
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/pre_processing_pipeline.h"

#include <array>
#include <cmath>
#include <numeric>
#include <vector>

using Catch::Approx;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 2.0f * kPi;

/// Generate a sine wave at the given frequency, starting from a given phase offset
void generateSine(float* buffer, size_t numSamples, float frequency,
                  double sampleRate, float amplitude = 1.0f,
                  size_t sampleOffset = 0) {
    const float omega = kTwoPi * frequency / static_cast<float>(sampleRate);
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = amplitude * std::sin(
            omega * static_cast<float>(sampleOffset + i));
    }
}

/// Compute RMS of a buffer
float computeRMS(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    float sumSquared = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        sumSquared += buffer[i] * buffer[i];
    }
    return std::sqrt(sumSquared / static_cast<float>(numSamples));
}

/// Compute peak absolute value of a buffer
float computePeak(const float* buffer, size_t numSamples) {
    float peak = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

/// Linear amplitude to dB
float linearToDb(float linear) {
    if (linear <= 0.0f) return -200.0f;
    return 20.0f * std::log10(linear);
}

/// Process a buffer through the pipeline in blocks
void processInBlocks(Innexus::PreProcessingPipeline& pipeline,
                     float* buffer, size_t totalSamples,
                     size_t blockSize = kBlockSize) {
    size_t processed = 0;
    while (processed < totalSamples) {
        size_t thisBlock = std::min(blockSize, totalSamples - processed);
        pipeline.processBlock(buffer + processed, thisBlock);
        processed += thisBlock;
    }
}

} // anonymous namespace

// =============================================================================
// FR-005: DC Offset Removal
// =============================================================================

TEST_CASE("PreProcessingPipeline: DC offset removal converges to within 1% at 13ms",
          "[innexus][preprocessing][FR-005]") {
    Innexus::PreProcessingPipeline pipeline;
    pipeline.prepare(kSampleRate);

    // FR-005 / T014: DC offset must converge to within 1% of zero within
    // 13ms at 44.1kHz. The pipeline uses a sample-by-sample IIR DC estimator
    // (one-pole LP at 50 Hz, tau=3.18ms) plus a 4th-order Butterworth HPF
    // at 39 Hz. The DC estimator converges fast; any residual is further
    // suppressed by the HPF.
    const size_t at13ms = static_cast<size_t>(0.013 * kSampleRate);

    // Create a signal with a constant DC offset of 0.1
    // Process enough samples to reach 13ms, then check the last sample
    const size_t totalSamples = at13ms + kBlockSize;
    std::vector<float> buffer(totalSamples, 0.1f);

    processInBlocks(pipeline, buffer.data(), totalSamples);

    // After 13ms, the residual DC must be within 1% of the original offset.
    // Original offset = 0.1, so 1% = 0.001.
    // Check a range of samples around and after the 13ms mark to be thorough.
    float maxAfter13ms = 0.0f;
    for (size_t i = at13ms; i < totalSamples; ++i) {
        maxAfter13ms = std::max(maxAfter13ms, std::abs(buffer[i]));
    }
    REQUIRE(maxAfter13ms < 0.001f);
}

TEST_CASE("PreProcessingPipeline: DC offset removal does not affect AC signal",
          "[innexus][preprocessing][FR-005]") {
    Innexus::PreProcessingPipeline pipeline;
    pipeline.prepare(kSampleRate);

    // Let the pipeline settle with a 440 Hz sine for 0.5 seconds
    const size_t warmupSamples = 22050;
    std::vector<float> warmup(warmupSamples);
    generateSine(warmup.data(), warmupSamples, 440.0f, kSampleRate);
    processInBlocks(pipeline, warmup.data(), warmupSamples);

    // Now process a continuation of the same sine
    std::vector<float> processed(kBlockSize);
    generateSine(processed.data(), kBlockSize, 440.0f, kSampleRate,
                 1.0f, warmupSamples);
    pipeline.processBlock(processed.data(), kBlockSize);

    // Compare RMS to expected input RMS
    float outputRMS = computeRMS(processed.data(), kBlockSize);
    float inputRMS = 1.0f / std::sqrt(2.0f); // sine RMS

    // Allow up to 1 dB loss from the combined filters at 440 Hz
    float ratioDb = linearToDb(outputRMS / inputRMS);
    REQUIRE(ratioDb > -1.0f);
}

// =============================================================================
// FR-006: High-Pass Filter (4th-order Butterworth at 39 Hz)
// =============================================================================

TEST_CASE("PreProcessingPipeline: HPF attenuates 20 Hz significantly",
          "[innexus][preprocessing][FR-006]") {
    Innexus::PreProcessingPipeline pipeline;
    pipeline.prepare(kSampleRate);

    // Generate a long 20 Hz sine to let the filters settle
    const size_t totalSamples = 88200; // 2 seconds
    std::vector<float> buffer(totalSamples);
    generateSine(buffer.data(), totalSamples, 20.0f, kSampleRate);

    processInBlocks(pipeline, buffer.data(), totalSamples);

    // Measure RMS of the last quarter (well after settling)
    size_t startCheck = totalSamples * 3 / 4;
    float outputRMS = computeRMS(buffer.data() + startCheck,
                                 totalSamples - startCheck);
    float inputRMS = 1.0f / std::sqrt(2.0f);

    float attenuationDb = linearToDb(outputRMS / inputRMS);

    // IIR DC estimator + 4th-order Butterworth HPF at 39 Hz provides
    // >12 dB attenuation at 20 Hz.
    REQUIRE(attenuationDb < -12.0f);
}

TEST_CASE("PreProcessingPipeline: HPF passes 100 Hz with less than 1 dB loss",
          "[innexus][preprocessing][FR-006]") {
    Innexus::PreProcessingPipeline pipeline;
    pipeline.prepare(kSampleRate);

    // Generate a long 100 Hz sine to let the filters settle
    const size_t totalSamples = 44100;
    std::vector<float> buffer(totalSamples);
    generateSine(buffer.data(), totalSamples, 100.0f, kSampleRate);

    processInBlocks(pipeline, buffer.data(), totalSamples);

    // Measure RMS of the last quarter
    size_t startCheck = totalSamples * 3 / 4;
    float outputRMS = computeRMS(buffer.data() + startCheck,
                                 totalSamples - startCheck);
    float inputRMS = 1.0f / std::sqrt(2.0f);

    float lossDb = linearToDb(outputRMS / inputRMS);

    // HPF cutoff (39 Hz) is well below 100 Hz, so passband loss is minimal
    REQUIRE(lossDb > -1.0f);
}

// =============================================================================
// FR-007: Noise Gate
// =============================================================================

TEST_CASE("PreProcessingPipeline: Noise gate suppresses sub-threshold signal",
          "[innexus][preprocessing][FR-007]") {
    Innexus::PreProcessingPipeline pipeline;
    pipeline.prepare(kSampleRate);

    // Default threshold is -60 dB = 0.001 linear amplitude
    // Generate a very quiet 440 Hz sine well below the threshold
    const float quietAmplitude = 0.0001f; // -80 dB
    const size_t totalSamples = 4096;
    std::vector<float> buffer(totalSamples);
    generateSine(buffer.data(), totalSamples, 440.0f, kSampleRate,
                 quietAmplitude);

    processInBlocks(pipeline, buffer.data(), totalSamples);

    // After processing, the last block should be near zero (gated)
    float lastBlockRMS = computeRMS(buffer.data() + totalSamples - kBlockSize,
                                    kBlockSize);
    REQUIRE(lastBlockRMS < quietAmplitude * 0.1f);
}

TEST_CASE("PreProcessingPipeline: Noise gate passes above-threshold signal",
          "[innexus][preprocessing][FR-007]") {
    Innexus::PreProcessingPipeline pipeline;
    pipeline.prepare(kSampleRate);

    // Generate a 440 Hz sine at normal amplitude (well above -60 dB)
    const size_t totalSamples = 4096;
    std::vector<float> buffer(totalSamples);
    generateSine(buffer.data(), totalSamples, 440.0f, kSampleRate, 0.5f);

    processInBlocks(pipeline, buffer.data(), totalSamples);

    // Output should still have significant energy
    float outputRMS = computeRMS(buffer.data() + totalSamples - kBlockSize,
                                 kBlockSize);
    REQUIRE(outputRMS > 0.1f);
}

TEST_CASE("PreProcessingPipeline: Noise gate threshold is configurable",
          "[innexus][preprocessing][FR-007]") {
    Innexus::PreProcessingPipeline pipeline;
    pipeline.prepare(kSampleRate);

    // Set a very high threshold (-20 dB = 0.1 linear)
    pipeline.setNoiseGateThreshold(-20.0f);

    // Generate a signal at -40 dB (below -20 dB threshold)
    const float amplitude = 0.01f; // -40 dB
    const size_t totalSamples = 4096;
    std::vector<float> buffer(totalSamples);
    generateSine(buffer.data(), totalSamples, 440.0f, kSampleRate, amplitude);

    processInBlocks(pipeline, buffer.data(), totalSamples);

    // Should be gated
    float lastBlockRMS = computeRMS(buffer.data() + totalSamples - kBlockSize,
                                    kBlockSize);
    REQUIRE(lastBlockRMS < amplitude * 0.1f);
}

// =============================================================================
// FR-008: Transient Suppression
// =============================================================================

TEST_CASE("PreProcessingPipeline: Transient suppression attenuates impulse",
          "[innexus][preprocessing][FR-008]") {
    Innexus::PreProcessingPipeline pipeline;
    pipeline.prepare(kSampleRate);

    // Warm up the pipeline with a steady sine for 500ms
    // so the slow envelope establishes a baseline
    const size_t warmupSamples = static_cast<size_t>(0.5 * kSampleRate);
    std::vector<float> warmup(warmupSamples);
    generateSine(warmup.data(), warmupSamples, 440.0f, kSampleRate, 0.3f);
    processInBlocks(pipeline, warmup.data(), warmupSamples);

    // Create a block with the same sine plus a sharp impulse transient
    std::vector<float> impulseBlock(kBlockSize);
    generateSine(impulseBlock.data(), kBlockSize, 440.0f, kSampleRate,
                 0.3f, warmupSamples);

    // Add sharp impulse near the start (much larger than steady-state)
    impulseBlock[10] = 1.0f;
    impulseBlock[11] = -1.0f;

    float impulseInputPeak = computePeak(impulseBlock.data(), kBlockSize);

    pipeline.processBlock(impulseBlock.data(), kBlockSize);

    float impulseOutputPeak = computePeak(impulseBlock.data(), kBlockSize);

    // The impulse should be attenuated relative to the input peak
    // The transient suppression with 10:1 ratio should reduce the impulse
    // considerably (peak 1.0 vs baseline ~0.3 gives ratio ~3.3, well above
    // the threshold of 2.0, so gain reduction kicks in)
    REQUIRE(impulseOutputPeak < impulseInputPeak * 0.8f);
}

TEST_CASE("PreProcessingPipeline: Steady-state signal passes through transient suppression",
          "[innexus][preprocessing][FR-008]") {
    Innexus::PreProcessingPipeline pipeline;
    pipeline.prepare(kSampleRate);

    // Process a long steady-state sine to let all envelope followers settle
    const size_t totalSamples = 88200; // 2 seconds
    std::vector<float> buffer(totalSamples);
    generateSine(buffer.data(), totalSamples, 440.0f, kSampleRate, 0.5f);

    processInBlocks(pipeline, buffer.data(), totalSamples);

    // After settling, the last block should have comparable RMS
    float outputRMS = computeRMS(buffer.data() + totalSamples - kBlockSize,
                                 kBlockSize);
    float inputRMS = 0.5f / std::sqrt(2.0f); // Input sine RMS

    // With the transient detection threshold, steady-state signal should not
    // be attenuated by the transient suppressor. Only the DC blocker and HPF
    // affect the signal at 440 Hz, which should be < 0.5 dB combined.
    float lossDb = linearToDb(outputRMS / inputRMS);
    REQUIRE(lossDb > -1.0f);
}

// =============================================================================
// FR-009: Separate Analysis Path
// =============================================================================

TEST_CASE("PreProcessingPipeline: processBlock operates on analysis buffer only",
          "[innexus][preprocessing][FR-009]") {
    Innexus::PreProcessingPipeline pipeline;
    pipeline.prepare(kSampleRate);

    // Create two separate buffers: one for "audio" (untouched) and one for analysis
    std::vector<float> audioBuffer(kBlockSize);
    std::vector<float> analysisBuffer(kBlockSize);

    generateSine(audioBuffer.data(), kBlockSize, 440.0f, kSampleRate);

    // Copy audio into analysis buffer
    std::copy(audioBuffer.begin(), audioBuffer.end(), analysisBuffer.begin());

    // Keep original copy of audio for comparison
    std::vector<float> originalAudio(audioBuffer.begin(), audioBuffer.end());

    // Process only the analysis buffer
    pipeline.processBlock(analysisBuffer.data(), kBlockSize);

    // Audio buffer must be completely unchanged
    bool audioUnchanged = true;
    for (size_t i = 0; i < kBlockSize; ++i) {
        if (audioBuffer[i] != originalAudio[i]) {
            audioUnchanged = false;
            break;
        }
    }
    REQUIRE(audioUnchanged);

    // Analysis buffer should be modified (not identical to original)
    bool analysisModified = false;
    for (size_t i = 0; i < kBlockSize; ++i) {
        if (analysisBuffer[i] != originalAudio[i]) {
            analysisModified = true;
            break;
        }
    }
    REQUIRE(analysisModified);
}

// =============================================================================
// Pipeline Reset
// =============================================================================

TEST_CASE("PreProcessingPipeline: reset clears state",
          "[innexus][preprocessing]") {
    Innexus::PreProcessingPipeline pipeline;
    pipeline.prepare(kSampleRate);

    // Process a signal with DC=0.1 for a while to build up filter state
    const size_t warmupSamples = 22050;
    std::vector<float> warmup(warmupSamples, 0.1f);
    processInBlocks(pipeline, warmup.data(), warmupSamples);

    // Reset the pipeline
    pipeline.reset();

    // Now process a signal with a DIFFERENT DC level (0.5)
    // After reset, the pipeline should adapt to the new DC level
    // (not be stuck tracking the old 0.1 level)
    const size_t settleSamples = static_cast<size_t>(0.05 * kSampleRate);
    std::vector<float> newDc(settleSamples, 0.5f);
    processInBlocks(pipeline, newDc.data(), settleSamples);

    // After settling, the output should be near zero (DC removed)
    float lastBlockRMS = computeRMS(newDc.data() + settleSamples - kBlockSize,
                                    kBlockSize);
    REQUIRE(lastBlockRMS < 0.01f);

    // Verify it adapted to the new level, not the old one
    // (if state wasn't cleared, the DC estimate would still be near 0.1
    // and would produce large residual from the 0.5 input)
}
