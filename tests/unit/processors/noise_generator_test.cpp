// ==============================================================================
// Unit Tests: NoiseGenerator
// ==============================================================================
// Layer 2: DSP Processor Tests
// Constitution Principle VIII: DSP algorithms must be independently testable
// Constitution Principle XII: Test-First Development
//
// Test organization by User Story:
// - US1: White Noise Generation [US1]
// - US2: Pink Noise Generation [US2]
// - US3: Tape Hiss Generation [US3]
// - US4: Vinyl Crackle Generation [US4]
// - US5: Asperity Noise Generation [US5]
// - US6: Multi-Noise Mixing [US6]
//
// Success Criteria tags:
// - [SC-001] through [SC-008]
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/processors/noise_generator.h"
#include "dsp/primitives/fft.h"

#include <array>
#include <cmath>
#include <vector>
#include <numeric>

using namespace Iterum::DSP;
using Catch::Approx;

// ==============================================================================
// Test Helpers
// ==============================================================================

namespace {

constexpr float kSampleRate = 44100.0f;
constexpr size_t kBlockSize = 512;
constexpr float kTwoPi = 6.283185307179586f;

// Calculate RMS of a buffer
inline float calculateRMS(const float* buffer, size_t size) {
    if (size == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(size));
}

// Find peak absolute value in buffer
inline float findPeak(const float* buffer, size_t size) {
    float peak = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

// Convert linear amplitude to decibels
inline float linearToDb(float linear) {
    if (linear <= 0.0f) return -144.0f;
    return 20.0f * std::log10(linear);
}

// Convert dB to linear
inline float dbToLinear(float dB) {
    return std::pow(10.0f, dB / 20.0f);
}

// Check if buffer is all zeros
inline bool isAllZeros(const float* buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (buffer[i] != 0.0f) return false;
    }
    return true;
}

// Check if buffer has any non-zero values
inline bool hasNonZeroValues(const float* buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (buffer[i] != 0.0f) return true;
    }
    return false;
}

// Measure energy in a frequency band using FFT
// Returns average magnitude in the frequency range [freqLow, freqHigh] Hz
inline float measureBandEnergy(const float* buffer, size_t size,
                                float freqLow, float freqHigh, float sampleRate) {
    // Use a power of 2 size for FFT
    constexpr size_t fftSize = 4096;
    if (size < fftSize) return 0.0f;

    FFT fft;
    fft.prepare(fftSize);

    // Copy and window the input
    std::vector<float> input(fftSize);
    std::vector<Complex> output(fftSize / 2 + 1);

    // Use Hanning window
    for (size_t i = 0; i < fftSize; ++i) {
        float window = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(fftSize));
        input[i] = buffer[i] * window;
    }

    // Perform FFT
    fft.forward(input.data(), output.data());

    // Calculate frequency resolution
    float binWidth = sampleRate / static_cast<float>(fftSize);
    size_t binLow = static_cast<size_t>(freqLow / binWidth);
    size_t binHigh = static_cast<size_t>(freqHigh / binWidth);

    if (binLow >= output.size()) binLow = output.size() - 1;
    if (binHigh >= output.size()) binHigh = output.size() - 1;
    if (binLow > binHigh) std::swap(binLow, binHigh);

    // Average magnitude in band
    float sumMag = 0.0f;
    size_t count = 0;
    for (size_t i = binLow; i <= binHigh; ++i) {
        float mag = std::sqrt(output[i].real * output[i].real +
                              output[i].imag * output[i].imag);
        sumMag += mag;
        count++;
    }

    return (count > 0) ? (sumMag / static_cast<float>(count)) : 0.0f;
}

} // anonymous namespace

// ==============================================================================
// User Story 1: White Noise Generation [US1]
// ==============================================================================

TEST_CASE("NoiseGenerator prepare() initializes correctly", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    // All noise types should be disabled by default
    REQUIRE_FALSE(noise.isNoiseEnabled(NoiseType::White));
    REQUIRE_FALSE(noise.isNoiseEnabled(NoiseType::Pink));
    REQUIRE_FALSE(noise.isNoiseEnabled(NoiseType::TapeHiss));
    REQUIRE_FALSE(noise.isNoiseEnabled(NoiseType::VinylCrackle));
    REQUIRE_FALSE(noise.isNoiseEnabled(NoiseType::Asperity));
    REQUIRE_FALSE(noise.isAnyEnabled());
}

TEST_CASE("White noise: output is zero when disabled", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    // Leave white noise disabled (default)
    std::array<float, kBlockSize> buffer;
    buffer.fill(0.5f); // Fill with non-zero to verify it gets zeroed

    noise.process(buffer.data(), buffer.size());

    REQUIRE(isAllZeros(buffer.data(), buffer.size()));
}

TEST_CASE("White noise: output is non-zero when enabled", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::White, true);
    noise.setNoiseLevel(NoiseType::White, 0.0f); // 0 dB = unity gain

    // Generate a larger buffer for statistical reliability
    constexpr size_t largeSize = 4096;
    std::vector<float> buffer(largeSize, 0.0f);

    // Process multiple blocks to let smoother settle
    for (size_t i = 0; i < largeSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    REQUIRE(hasNonZeroValues(buffer.data(), buffer.size()));
}

TEST_CASE("White noise: samples in [-1.0, 1.0] range (SC-003)", "[noise][US1][SC-003]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::White, true);
    noise.setNoiseLevel(NoiseType::White, 0.0f);

    // Generate many samples
    constexpr size_t testSize = 44100; // 1 second
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // All samples must be in valid range
    for (size_t i = 0; i < testSize; ++i) {
        REQUIRE(buffer[i] >= -1.0f);
        REQUIRE(buffer[i] <= 1.0f);
    }
}

TEST_CASE("White noise: level at -20dB produces ~0.1 amplitude", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::White, true);
    noise.setNoiseLevel(NoiseType::White, -20.0f);

    // Generate enough samples for statistical reliability
    constexpr size_t testSize = 44100; // 1 second
    std::vector<float> buffer(testSize);

    // Process multiple blocks to let smoother settle
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // Skip first part where smoother is settling (first 10ms)
    constexpr size_t skipSamples = 441;
    float rms = calculateRMS(buffer.data() + skipSamples, testSize - skipSamples);

    // -20dB = 0.1 linear, but white noise has RMS of ~0.577 for uniform [-1,1]
    // So expected RMS ≈ 0.1 * 0.577 ≈ 0.058
    // Allow reasonable tolerance
    float expectedRMS = 0.1f * 0.577f;
    REQUIRE(rms == Approx(expectedRMS).margin(0.03f));
}

TEST_CASE("White noise: level at 0dB produces ~1.0 amplitude", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::White, true);
    noise.setNoiseLevel(NoiseType::White, 0.0f);

    // Generate enough samples for statistical reliability
    constexpr size_t testSize = 44100; // 1 second
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // Skip first part where smoother is settling
    constexpr size_t skipSamples = 441;
    float rms = calculateRMS(buffer.data() + skipSamples, testSize - skipSamples);

    // White noise from uniform [-1,1] has RMS of ~0.577
    float expectedRMS = 0.577f;
    REQUIRE(rms == Approx(expectedRMS).margin(0.05f));
}

TEST_CASE("White noise: setNoiseLevel affects output amplitude", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::White, true);

    constexpr size_t testSize = 8192;
    std::vector<float> bufferLoud(testSize);
    std::vector<float> bufferQuiet(testSize);

    // Generate at 0dB
    noise.setNoiseLevel(NoiseType::White, 0.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(bufferLoud.data() + i * kBlockSize, kBlockSize);
    }

    // Reset and generate at -20dB
    noise.reset();
    noise.setNoiseLevel(NoiseType::White, -20.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(bufferQuiet.data() + i * kBlockSize, kBlockSize);
    }

    float rmsLoud = calculateRMS(bufferLoud.data() + 1000, testSize - 1000);
    float rmsQuiet = calculateRMS(bufferQuiet.data() + 1000, testSize - 1000);

    // -20dB difference = 10x amplitude difference
    float ratio = rmsLoud / rmsQuiet;
    REQUIRE(ratio == Approx(10.0f).margin(2.0f));
}

TEST_CASE("White noise: spectral flatness within 3dB (SC-001)", "[noise][US1][SC-001]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::White, true);
    noise.setNoiseLevel(NoiseType::White, 0.0f);

    // Generate 10 seconds of white noise for spectral analysis
    constexpr size_t testSize = 441000; // 10 seconds at 44.1kHz
    std::vector<float> buffer(testSize);

    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(buffer.data() + i * kBlockSize, kBlockSize);
    }

    // Skip initial smoother settling
    const float* analysisStart = buffer.data() + 4410; // Skip first 100ms
    size_t analysisSize = testSize - 4410;

    // Measure energy at different frequency bands
    // Using wider bands for more stable measurements
    float energy1k = measureBandEnergy(analysisStart, analysisSize, 800.0f, 1200.0f, kSampleRate);
    float energy4k = measureBandEnergy(analysisStart, analysisSize, 3500.0f, 4500.0f, kSampleRate);

    // Convert to dB
    float db1k = linearToDb(energy1k);
    float db4k = linearToDb(energy4k);

    // Energy should be within 3dB across these bands for white noise
    float difference = std::abs(db1k - db4k);
    REQUIRE(difference < 3.0f);
}

TEST_CASE("White noise: different seeds produce different sequences", "[noise][US1]") {
    NoiseGenerator noise1;
    NoiseGenerator noise2;

    noise1.prepare(kSampleRate, kBlockSize);
    noise2.prepare(kSampleRate, kBlockSize);

    noise1.setNoiseEnabled(NoiseType::White, true);
    noise2.setNoiseEnabled(NoiseType::White, true);
    noise1.setNoiseLevel(NoiseType::White, 0.0f);
    noise2.setNoiseLevel(NoiseType::White, 0.0f);

    // Reset to get different sequences
    noise2.reset();

    std::array<float, kBlockSize> buffer1, buffer2;
    noise1.process(buffer1.data(), buffer1.size());
    noise2.process(buffer2.data(), buffer2.size());

    // Sequences should be different
    bool allSame = true;
    for (size_t i = 0; i < kBlockSize; ++i) {
        if (buffer1[i] != buffer2[i]) {
            allSame = false;
            break;
        }
    }
    REQUIRE_FALSE(allSame);
}

TEST_CASE("White noise: master level affects output", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::White, true);
    noise.setNoiseLevel(NoiseType::White, 0.0f);

    constexpr size_t testSize = 8192;
    std::vector<float> bufferNormal(testSize);
    std::vector<float> bufferQuiet(testSize);

    // Generate with master at 0dB
    noise.setMasterLevel(0.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(bufferNormal.data() + i * kBlockSize, kBlockSize);
    }

    // Reset and generate with master at -20dB
    noise.reset();
    noise.setMasterLevel(-20.0f);
    for (size_t i = 0; i < testSize / kBlockSize; ++i) {
        noise.process(bufferQuiet.data() + i * kBlockSize, kBlockSize);
    }

    float rmsNormal = calculateRMS(bufferNormal.data() + 1000, testSize - 1000);
    float rmsQuiet = calculateRMS(bufferQuiet.data() + 1000, testSize - 1000);

    // -20dB difference = 10x amplitude difference
    float ratio = rmsNormal / rmsQuiet;
    REQUIRE(ratio == Approx(10.0f).margin(2.0f));
}

TEST_CASE("White noise: processMix adds noise to input", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseEnabled(NoiseType::White, true);
    noise.setNoiseLevel(NoiseType::White, -20.0f);

    // Create input with known DC offset
    std::array<float, kBlockSize> input;
    std::array<float, kBlockSize> output;
    input.fill(0.5f);

    // Process multiple blocks to let smoother settle
    for (int i = 0; i < 10; ++i) {
        noise.processMix(input.data(), output.data(), kBlockSize);
    }

    // Output should contain input (0.5) plus noise
    // Mean should be approximately 0.5, but with some variation from noise
    float sum = 0.0f;
    for (size_t i = 0; i < kBlockSize; ++i) {
        sum += output[i];
    }
    float mean = sum / static_cast<float>(kBlockSize);

    REQUIRE(mean == Approx(0.5f).margin(0.1f));

    // Output should not be exactly 0.5 everywhere (noise added)
    bool hasVariation = false;
    for (size_t i = 0; i < kBlockSize; ++i) {
        if (std::abs(output[i] - 0.5f) > 0.01f) {
            hasVariation = true;
            break;
        }
    }
    REQUIRE(hasVariation);
}

TEST_CASE("NoiseGenerator getNoiseLevel returns set value", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    noise.setNoiseLevel(NoiseType::White, -15.0f);
    REQUIRE(noise.getNoiseLevel(NoiseType::White) == Approx(-15.0f));

    noise.setNoiseLevel(NoiseType::Pink, -30.0f);
    REQUIRE(noise.getNoiseLevel(NoiseType::Pink) == Approx(-30.0f));
}

TEST_CASE("NoiseGenerator isNoiseEnabled returns set value", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    REQUIRE_FALSE(noise.isNoiseEnabled(NoiseType::White));

    noise.setNoiseEnabled(NoiseType::White, true);
    REQUIRE(noise.isNoiseEnabled(NoiseType::White));

    noise.setNoiseEnabled(NoiseType::White, false);
    REQUIRE_FALSE(noise.isNoiseEnabled(NoiseType::White));
}

TEST_CASE("NoiseGenerator isAnyEnabled returns true when any enabled", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    REQUIRE_FALSE(noise.isAnyEnabled());

    noise.setNoiseEnabled(NoiseType::Pink, true);
    REQUIRE(noise.isAnyEnabled());

    noise.setNoiseEnabled(NoiseType::Pink, false);
    REQUIRE_FALSE(noise.isAnyEnabled());
}

TEST_CASE("NoiseGenerator level clamped to valid range", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    // Set below minimum
    noise.setNoiseLevel(NoiseType::White, -200.0f);
    REQUIRE(noise.getNoiseLevel(NoiseType::White) == Approx(-96.0f));

    // Set above maximum
    noise.setNoiseLevel(NoiseType::White, 50.0f);
    REQUIRE(noise.getNoiseLevel(NoiseType::White) == Approx(12.0f));
}

TEST_CASE("NoiseGenerator getMasterLevel returns set value", "[noise][US1]") {
    NoiseGenerator noise;
    noise.prepare(kSampleRate, kBlockSize);

    REQUIRE(noise.getMasterLevel() == Approx(0.0f)); // Default

    noise.setMasterLevel(-6.0f);
    REQUIRE(noise.getMasterLevel() == Approx(-6.0f));
}
