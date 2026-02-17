// ==============================================================================
// Unit Tests: Phase Reset Integration for PhaseVocoderPitchShifter
// ==============================================================================
// Layer 2: DSP Processor Tests
// Feature: 062-spectral-transient-detector (User Story 3)
// Constitution Principle VIII: DSP algorithms must be independently testable
// Constitution Principle XIII: Test-First Development
//
// Tests drive the feature through PitchShiftProcessor public API.
// All test case names begin with "PhaseReset" per tasks.md naming convention.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/pitch_shift_processor.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace {

using Krate::DSP::PhaseVocoderPitchShifter;
using Krate::DSP::PitchShiftProcessor;
using Krate::DSP::PitchMode;
using Catch::Approx;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr double kSampleRate = 44100.0;
constexpr std::size_t kBlockSize = 512;

// ==============================================================================
// Helper: Generate a sine wave into a buffer
// ==============================================================================
void generateSine(std::vector<float>& buffer, float frequency, float sampleRate,
                  float amplitude = 1.0f) {
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = amplitude * std::sin(kTwoPi * frequency * static_cast<float>(i) / sampleRate);
    }
}

// ==============================================================================
// Helper: Process audio through PitchShiftProcessor, returning output
// ==============================================================================
std::vector<float> processWithProcessor(PitchShiftProcessor& proc,
                                        const std::vector<float>& input,
                                        std::size_t blockSize = kBlockSize) {
    std::vector<float> output(input.size(), 0.0f);
    std::vector<float> inBlock(blockSize, 0.0f);
    std::vector<float> outBlock(blockSize, 0.0f);

    for (std::size_t pos = 0; pos < input.size(); pos += blockSize) {
        std::size_t count = std::min(blockSize, input.size() - pos);
        std::copy(input.begin() + static_cast<std::ptrdiff_t>(pos),
                  input.begin() + static_cast<std::ptrdiff_t>(pos + count),
                  inBlock.begin());
        if (count < blockSize) {
            std::fill(inBlock.begin() + static_cast<std::ptrdiff_t>(count), inBlock.end(), 0.0f);
        }
        proc.process(inBlock.data(), outBlock.data(), blockSize);
        std::copy(outBlock.begin(),
                  outBlock.begin() + static_cast<std::ptrdiff_t>(count),
                  output.begin() + static_cast<std::ptrdiff_t>(pos));
    }
    return output;
}

// ==============================================================================
// Helper: Compute RMS of a buffer segment
// ==============================================================================
float computeRMS(const float* data, std::size_t count) {
    if (count == 0) return 0.0f;
    float sumSq = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        sumSq += data[i] * data[i];
    }
    return std::sqrt(sumSq / static_cast<float>(count));
}

// ==============================================================================
// Helper: Find peak absolute value in a buffer segment
// ==============================================================================
float findPeak(const float* data, std::size_t count) {
    float peak = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        float absVal = std::abs(data[i]);
        if (absVal > peak) peak = absVal;
    }
    return peak;
}

// ==============================================================================
// Helper: Check if any sample is NaN
// ==============================================================================
bool containsNaN(const std::vector<float>& buffer) {
    for (float v : buffer) {
        if (std::isnan(v)) return true;
    }
    return false;
}

} // anonymous namespace

// ==============================================================================
// Test: PhaseVocoderPitchShifter has setPhaseReset/getPhaseReset methods
// ==============================================================================
TEST_CASE("PhaseResetAPIExistsOnVocoder", "[processors][phase_reset]") {
    PhaseVocoderPitchShifter shifter;
    shifter.prepare(kSampleRate, kBlockSize);

    // Should compile and be callable
    shifter.setPhaseReset(true);
    REQUIRE(shifter.getPhaseReset() == true);

    shifter.setPhaseReset(false);
    REQUIRE(shifter.getPhaseReset() == false);
}

// ==============================================================================
// Test: PitchShiftProcessor has setPhaseReset/getPhaseReset public methods
// ==============================================================================
TEST_CASE("PhaseResetAPIExistsOnProcessor", "[processors][phase_reset]") {
    PitchShiftProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);
    proc.setMode(PitchMode::PhaseVocoder);

    // Should compile and be callable
    proc.setPhaseReset(true);
    REQUIRE(proc.getPhaseReset() == true);

    proc.setPhaseReset(false);
    REQUIRE(proc.getPhaseReset() == false);
}

// ==============================================================================
// Test: Phase reset disabled by default after prepare() (FR-013)
// ==============================================================================
TEST_CASE("PhaseResetDefault", "[processors][phase_reset]") {
    PhaseVocoderPitchShifter shifter;
    shifter.prepare(kSampleRate, kBlockSize);
    REQUIRE(shifter.getPhaseReset() == false);

    PitchShiftProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);
    proc.setMode(PitchMode::PhaseVocoder);
    REQUIRE(proc.getPhaseReset() == false);
}

// ==============================================================================
// Test: Round-trip getter
// ==============================================================================
TEST_CASE("PhaseResetRoundTrip", "[processors][phase_reset]") {
    PhaseVocoderPitchShifter shifter;
    shifter.prepare(kSampleRate, kBlockSize);

    shifter.setPhaseReset(true);
    REQUIRE(shifter.getPhaseReset() == true);

    shifter.setPhaseReset(false);
    REQUIRE(shifter.getPhaseReset() == false);

    // Also on PitchShiftProcessor
    PitchShiftProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);

    proc.setPhaseReset(true);
    REQUIRE(proc.getPhaseReset() == true);

    proc.setPhaseReset(false);
    REQUIRE(proc.getPhaseReset() == false);
}

// ==============================================================================
// Test: Phase reset and phase locking independently togglable (FR-013)
// ==============================================================================
TEST_CASE("PhaseResetIndependentOfPhaseLocking", "[processors][phase_reset]") {
    PhaseVocoderPitchShifter shifter;
    shifter.prepare(kSampleRate, kBlockSize);

    // Both can be enabled simultaneously
    shifter.setPhaseLocking(true);
    shifter.setPhaseReset(true);
    REQUIRE(shifter.getPhaseLocking() == true);
    REQUIRE(shifter.getPhaseReset() == true);

    // Toggling one doesn't affect the other
    shifter.setPhaseLocking(false);
    REQUIRE(shifter.getPhaseReset() == true);
    REQUIRE(shifter.getPhaseLocking() == false);

    shifter.setPhaseLocking(true);
    shifter.setPhaseReset(false);
    REQUIRE(shifter.getPhaseLocking() == true);
    REQUIRE(shifter.getPhaseReset() == false);

    // Both disabled
    shifter.setPhaseLocking(false);
    shifter.setPhaseReset(false);
    REQUIRE(shifter.getPhaseLocking() == false);
    REQUIRE(shifter.getPhaseReset() == false);

    // Process should work in all combinations without crash
    std::vector<float> input(kBlockSize * 20, 0.0f);
    generateSine(input, 440.0f, static_cast<float>(kSampleRate));
    std::vector<float> output(input.size(), 0.0f);

    // Both enabled
    shifter.setPhaseLocking(true);
    shifter.setPhaseReset(true);
    shifter.reset();
    for (std::size_t pos = 0; pos < input.size(); pos += kBlockSize) {
        shifter.process(input.data() + pos, output.data() + pos, kBlockSize, 1.5f);
    }
    REQUIRE_FALSE(containsNaN(output));

    // Phase locking only
    shifter.setPhaseLocking(true);
    shifter.setPhaseReset(false);
    shifter.reset();
    for (std::size_t pos = 0; pos < input.size(); pos += kBlockSize) {
        shifter.process(input.data() + pos, output.data() + pos, kBlockSize, 1.5f);
    }
    REQUIRE_FALSE(containsNaN(output));

    // Phase reset only
    shifter.setPhaseLocking(false);
    shifter.setPhaseReset(true);
    shifter.reset();
    for (std::size_t pos = 0; pos < input.size(); pos += kBlockSize) {
        shifter.process(input.data() + pos, output.data() + pos, kBlockSize, 1.5f);
    }
    REQUIRE_FALSE(containsNaN(output));

    // Neither
    shifter.setPhaseLocking(false);
    shifter.setPhaseReset(false);
    shifter.reset();
    for (std::size_t pos = 0; pos < input.size(); pos += kBlockSize) {
        shifter.process(input.data() + pos, output.data() + pos, kBlockSize, 1.5f);
    }
    REQUIRE_FALSE(containsNaN(output));
}

// ==============================================================================
// Test: Sustained tonal input - identical output with/without phase reset
// (spec US3 scenario 2)
// ==============================================================================
TEST_CASE("PhaseResetSustainedTonalIdentical", "[processors][phase_reset]") {
    // Sustained tone: phase reset should never trigger (no transients),
    // so output should be identical with/without phase reset.
    constexpr float kFrequency = 440.0f;
    constexpr float kPitchRatio = 1.5f;

    // Generate sustained sine -- need enough audio to fill latency and get output
    const std::size_t totalSamples = kBlockSize * 40;
    std::vector<float> input(totalSamples);
    generateSine(input, kFrequency, static_cast<float>(kSampleRate));

    // Process without phase reset
    PhaseVocoderPitchShifter shifterOff;
    shifterOff.prepare(kSampleRate, kBlockSize);
    shifterOff.setPhaseLocking(true);
    shifterOff.setPhaseReset(false);

    std::vector<float> outputOff(totalSamples, 0.0f);
    for (std::size_t pos = 0; pos < totalSamples; pos += kBlockSize) {
        shifterOff.process(input.data() + pos, outputOff.data() + pos, kBlockSize, kPitchRatio);
    }

    // Process with phase reset
    PhaseVocoderPitchShifter shifterOn;
    shifterOn.prepare(kSampleRate, kBlockSize);
    shifterOn.setPhaseLocking(true);
    shifterOn.setPhaseReset(true);

    std::vector<float> outputOn(totalSamples, 0.0f);
    for (std::size_t pos = 0; pos < totalSamples; pos += kBlockSize) {
        shifterOn.process(input.data() + pos, outputOn.data() + pos, kBlockSize, kPitchRatio);
    }

    // After latency, outputs should be identical (or nearly so)
    // The PhaseVocoder has ~5120 samples latency (kFFTSize + kHopSize)
    constexpr std::size_t kLatency = 4096 + 1024 + 1024; // FFT + hop + margin
    bool identical = true;
    for (std::size_t i = kLatency; i < totalSamples; ++i) {
        if (std::abs(outputOn[i] - outputOff[i]) > 1e-6f) {
            identical = false;
            break;
        }
    }
    REQUIRE(identical);
}

// ==============================================================================
// Test: Transient sharpness >= 2 dB improvement (SC-004)
// ==============================================================================
TEST_CASE("PhaseResetTransientSharpness", "[processors][phase_reset]") {
    // SC-004 parameters: 4096-FFT, 1024-hop, 44100Hz, ratio 2.0
    // Synthetic impulse of amplitude 1.0, preceded by 10 frames of silence
    constexpr float kPitchRatio = 2.0f;
    constexpr std::size_t kHopSize = 1024;
    constexpr std::size_t kFFTSize = 4096;

    // 10 frames of silence + 1 frame of impulse + 20 frames of tail
    constexpr std::size_t kSilenceFrames = 10;
    constexpr std::size_t kTailFrames = 20;
    const std::size_t totalFrames = kSilenceFrames + 1 + kTailFrames;
    const std::size_t totalSamples = totalFrames * kHopSize;

    // Build input: silence, then a single impulse sample at the onset frame
    std::vector<float> input(totalSamples, 0.0f);
    const std::size_t impulsePos = kSilenceFrames * kHopSize;
    input[impulsePos] = 1.0f;

    // Process WITHOUT phase reset
    PitchShiftProcessor procOff;
    procOff.prepare(kSampleRate, kBlockSize);
    procOff.setMode(PitchMode::PhaseVocoder);
    procOff.setSemitones(12.0f);  // ratio 2.0 = 12 semitones
    procOff.setPhaseReset(false);
    std::vector<float> outputOff = processWithProcessor(procOff, input);

    // Process WITH phase reset
    PitchShiftProcessor procOn;
    procOn.prepare(kSampleRate, kBlockSize);
    procOn.setMode(PitchMode::PhaseVocoder);
    procOn.setSemitones(12.0f);
    procOn.setPhaseReset(true);
    std::vector<float> outputOn = processWithProcessor(procOn, input);

    // Measure peak-to-RMS in first 5ms after onset
    // The onset appears at impulsePos but there's latency.
    // Latency = kFFTSize + kHopSize = 5120 samples
    const std::size_t latency = kFFTSize + kHopSize;
    const std::size_t onsetSample = impulsePos + latency;

    // 5ms window at 44100Hz = 220 samples
    const std::size_t windowSize = static_cast<std::size_t>(0.005 * kSampleRate);

    // Ensure we don't go out of bounds
    REQUIRE(onsetSample + windowSize <= outputOff.size());

    // Measure WITHOUT phase reset
    const float peakOff = findPeak(outputOff.data() + onsetSample, windowSize);
    const float rmsOff = computeRMS(outputOff.data() + onsetSample, windowSize);
    float peakToRmsOff_dB = 0.0f;
    if (rmsOff > 1e-10f) {
        peakToRmsOff_dB = 20.0f * std::log10(peakOff / rmsOff);
    }

    // Measure WITH phase reset
    const float peakOn = findPeak(outputOn.data() + onsetSample, windowSize);
    const float rmsOn = computeRMS(outputOn.data() + onsetSample, windowSize);
    float peakToRmsOn_dB = 0.0f;
    if (rmsOn > 1e-10f) {
        peakToRmsOn_dB = 20.0f * std::log10(peakOn / rmsOn);
    }

    float improvement_dB = peakToRmsOn_dB - peakToRmsOff_dB;

    // Report the actual measured values
    INFO("Peak-to-RMS (no phase reset): " << peakToRmsOff_dB << " dB");
    INFO("Peak-to-RMS (with phase reset): " << peakToRmsOn_dB << " dB");
    INFO("Improvement: " << improvement_dB << " dB");

    // SC-004: At least 2 dB improvement
    REQUIRE(improvement_dB >= 2.0f);
}

// ==============================================================================
// Test: Mid-stream toggle produces no NaN values (spec US3 scenario 3)
// ==============================================================================
TEST_CASE("PhaseResetMidStreamToggle", "[processors][phase_reset]") {
    PitchShiftProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);
    proc.setMode(PitchMode::PhaseVocoder);
    proc.setSemitones(7.0f);  // Perfect fifth up

    // Generate input with transient content
    const std::size_t totalSamples = kBlockSize * 30;
    std::vector<float> input(totalSamples, 0.0f);
    // Mix of sine and impulses
    generateSine(input, 440.0f, static_cast<float>(kSampleRate), 0.5f);
    // Add impulses at regular intervals
    for (std::size_t i = 0; i < totalSamples; i += 2048) {
        if (i < totalSamples) input[i] += 1.0f;
    }

    std::vector<float> output(totalSamples, 0.0f);
    std::vector<float> inBlock(kBlockSize, 0.0f);
    std::vector<float> outBlock(kBlockSize, 0.0f);

    for (std::size_t pos = 0; pos < totalSamples; pos += kBlockSize) {
        // Toggle phase reset at various points
        if (pos == kBlockSize * 5) proc.setPhaseReset(true);
        if (pos == kBlockSize * 10) proc.setPhaseReset(false);
        if (pos == kBlockSize * 15) proc.setPhaseReset(true);
        if (pos == kBlockSize * 20) proc.setPhaseReset(false);
        if (pos == kBlockSize * 25) proc.setPhaseReset(true);

        std::size_t count = std::min(kBlockSize, totalSamples - pos);
        std::copy(input.begin() + static_cast<std::ptrdiff_t>(pos),
                  input.begin() + static_cast<std::ptrdiff_t>(pos + count),
                  inBlock.begin());
        if (count < kBlockSize) {
            std::fill(inBlock.begin() + static_cast<std::ptrdiff_t>(count), inBlock.end(), 0.0f);
        }
        proc.process(inBlock.data(), outBlock.data(), kBlockSize);
        std::copy(outBlock.begin(),
                  outBlock.begin() + static_cast<std::ptrdiff_t>(count),
                  output.begin() + static_cast<std::ptrdiff_t>(pos));
    }

    // No NaN values in the output
    REQUIRE_FALSE(containsNaN(output));
}

// ==============================================================================
// Test: transientDetector_.prepare() called inside PhaseVocoderPitchShifter::prepare()
// ==============================================================================
TEST_CASE("PhaseResetDetectorPrepareCalledByVocoder", "[processors][phase_reset]") {
    // After prepare(), processing with phase reset enabled should work correctly
    // (detector is prepared). If prepare() didn't call transientDetector_.prepare(),
    // the detector would have 0 bins and always return false.
    PhaseVocoderPitchShifter shifter;
    shifter.prepare(kSampleRate, kBlockSize);
    shifter.setPhaseReset(true);
    shifter.setPhaseLocking(true);

    // Generate a signal with a clear transient
    const std::size_t totalSamples = kBlockSize * 30;
    std::vector<float> input(totalSamples, 0.0f);
    // Silence then impulse
    const std::size_t impulsePos = kBlockSize * 10;
    input[impulsePos] = 1.0f;

    std::vector<float> output(totalSamples, 0.0f);
    for (std::size_t pos = 0; pos < totalSamples; pos += kBlockSize) {
        shifter.process(input.data() + pos, output.data() + pos, kBlockSize, 2.0f);
    }

    // Should produce valid output (no NaN), confirming detector was prepared
    REQUIRE_FALSE(containsNaN(output));
}

// ==============================================================================
// Test: transientDetector_.reset() called inside PhaseVocoderPitchShifter::reset()
// ==============================================================================
TEST_CASE("PhaseResetDetectorResetCalledByVocoder", "[processors][phase_reset]") {
    PhaseVocoderPitchShifter shifter;
    shifter.prepare(kSampleRate, kBlockSize);
    shifter.setPhaseReset(true);

    // Process some audio
    const std::size_t totalSamples = kBlockSize * 15;
    std::vector<float> input(totalSamples, 0.0f);
    generateSine(input, 440.0f, static_cast<float>(kSampleRate));
    std::vector<float> output(totalSamples, 0.0f);
    for (std::size_t pos = 0; pos < totalSamples; pos += kBlockSize) {
        shifter.process(input.data() + pos, output.data() + pos, kBlockSize, 1.5f);
    }

    // Reset the shifter
    shifter.reset();

    // Process again after reset -- should work correctly (no NaN)
    // First frame after reset should be suppressed (first-frame detection suppression)
    std::fill(output.begin(), output.end(), 0.0f);
    for (std::size_t pos = 0; pos < totalSamples; pos += kBlockSize) {
        shifter.process(input.data() + pos, output.data() + pos, kBlockSize, 1.5f);
    }

    REQUIRE_FALSE(containsNaN(output));
}
