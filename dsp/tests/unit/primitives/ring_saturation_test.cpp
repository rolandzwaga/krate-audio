// ==============================================================================
// Unit Tests: RingSaturation Primitive
// ==============================================================================
// Tests for the ring saturation (self-modulation distortion) primitive.
//
// Feature: 108-ring-saturation
// Layer: 1 (Primitives)
// Test-First: Tests written BEFORE implementation per Constitution Principle XII
//
// Reference: specs/108-ring-saturation/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <spectral_analysis.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/core/math_constants.h>

#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

using Catch::Approx;

// Include the RingSaturation header
#include <krate/dsp/primitives/ring_saturation.h>

using namespace Krate::DSP;

// =============================================================================
// Test Helper: Shannon Spectral Entropy (T002)
// =============================================================================
// Calculates spectral entropy for SC-003 verification.
// Higher entropy indicates more complex/distributed spectral content.
// Formula: H = -sum(p_i * log2(p_i)) where p_i is normalized magnitude

namespace {

/// @brief Calculate Shannon spectral entropy from an audio buffer
/// @param buffer Audio samples to analyze
/// @param numSamples Number of samples
/// @param sampleRate Sample rate in Hz
/// @return Shannon entropy in bits (higher = more complex spectrum)
[[nodiscard]] float calculateSpectralEntropy(
    const float* buffer,
    size_t numSamples,
    float sampleRate
) {
    using namespace Krate::DSP;

    // Use FFT size that's a power of 2 and fits the buffer
    size_t fftSize = 1;
    while (fftSize * 2 <= numSamples && fftSize < 4096) {
        fftSize *= 2;
    }

    if (fftSize < 64) {
        return 0.0f; // Not enough samples for meaningful analysis
    }

    // Apply Hann window to reduce spectral leakage
    std::vector<float> windowed(fftSize);
    std::vector<float> window(fftSize);
    Window::generateHann(window.data(), fftSize);

    for (size_t i = 0; i < fftSize; ++i) {
        windowed[i] = buffer[i] * window[i];
    }

    // Perform FFT
    FFT fft;
    fft.prepare(fftSize);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(windowed.data(), spectrum.data());

    // Calculate magnitude spectrum and total power
    const size_t numBins = fft.numBins();
    std::vector<float> magnitudes(numBins);
    float totalMagnitude = 0.0f;

    for (size_t i = 0; i < numBins; ++i) {
        magnitudes[i] = spectrum[i].magnitude();
        totalMagnitude += magnitudes[i];
    }

    // Avoid division by zero
    if (totalMagnitude < 1e-10f) {
        return 0.0f;
    }

    // Calculate normalized probability distribution and entropy
    // H = -sum(p_i * log2(p_i)) for p_i > 0
    float entropy = 0.0f;
    constexpr float kLog2 = 0.693147180559945f; // ln(2)

    for (size_t i = 0; i < numBins; ++i) {
        const float p = magnitudes[i] / totalMagnitude;
        if (p > 1e-10f) {
            // log2(p) = ln(p) / ln(2)
            entropy -= p * (std::log(p) / kLog2);
        }
    }

    return entropy;
}

/// @brief Generate a sine wave into a buffer
/// @param buffer Output buffer
/// @param numSamples Number of samples
/// @param frequencyHz Frequency in Hz
/// @param sampleRate Sample rate in Hz
/// @param amplitude Peak amplitude (default 1.0)
void generateSineWave(
    float* buffer,
    size_t numSamples,
    float frequencyHz,
    float sampleRate,
    float amplitude = 1.0f
) {
    const float phaseIncrement = Krate::DSP::kTwoPi * frequencyHz / sampleRate;
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = amplitude * std::sin(phaseIncrement * static_cast<float>(i));
    }
}

/// @brief Calculate DC offset of a buffer
/// @param buffer Input buffer
/// @param numSamples Number of samples
/// @return Mean value (DC offset)
[[nodiscard]] float calculateDCOffset(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        sum += buffer[i];
    }
    return sum / static_cast<float>(numSamples);
}

/// @brief Calculate RMS level of a buffer
/// @param buffer Input buffer
/// @param numSamples Number of samples
/// @return RMS level
[[nodiscard]] float calculateRMS(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    float sumSquares = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        sumSquares += buffer[i] * buffer[i];
    }
    return std::sqrt(sumSquares / static_cast<float>(numSamples));
}

/// @brief Find peak absolute value in buffer
/// @param buffer Input buffer
/// @param numSamples Number of samples
/// @return Maximum absolute value
[[nodiscard]] float findPeakAbsolute(const float* buffer, size_t numSamples) {
    float peak = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

/// @brief Check if signal contains inharmonic sidebands (not integer multiples of fundamental)
/// @param buffer Audio samples
/// @param numSamples Number of samples
/// @param fundamentalHz Fundamental frequency
/// @param sampleRate Sample rate
/// @return true if inharmonic content is detected
[[nodiscard]] bool hasInharmonicSidebands(
    const float* buffer,
    size_t numSamples,
    float fundamentalHz,
    float sampleRate
) {
    using namespace Krate::DSP;

    // Use FFT for spectral analysis
    size_t fftSize = 2048;
    if (numSamples < fftSize) {
        fftSize = 1;
        while (fftSize * 2 <= numSamples) fftSize *= 2;
    }

    // Window and FFT
    std::vector<float> windowed(fftSize);
    std::vector<float> window(fftSize);
    Window::generateHann(window.data(), fftSize);

    for (size_t i = 0; i < fftSize; ++i) {
        windowed[i] = buffer[i] * window[i];
    }

    FFT fft;
    fft.prepare(fftSize);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(windowed.data(), spectrum.data());

    // Calculate frequency resolution
    const float binWidth = sampleRate / static_cast<float>(fftSize);
    const size_t fundamentalBin = static_cast<size_t>(fundamentalHz / binWidth + 0.5f);

    // Find total energy and energy at harmonic frequencies
    float totalEnergy = 0.0f;
    float harmonicEnergy = 0.0f;
    const size_t numBins = fft.numBins();

    for (size_t i = 1; i < numBins; ++i) { // Skip DC
        const float mag = spectrum[i].magnitude();
        const float energy = mag * mag;
        totalEnergy += energy;

        // Check if this bin is a harmonic (integer multiple of fundamental)
        const float freq = static_cast<float>(i) * binWidth;
        const float harmonicNumber = freq / fundamentalHz;
        const float nearestHarmonic = std::round(harmonicNumber);

        // Allow some tolerance for harmonic detection (within 0.5 bins)
        if (std::abs(harmonicNumber - nearestHarmonic) < 0.1f && nearestHarmonic >= 1.0f) {
            harmonicEnergy += energy;
        }
    }

    // If significant energy exists outside harmonics, we have inharmonic content
    const float inharmonicEnergy = totalEnergy - harmonicEnergy;
    const float inharmonicRatio = (totalEnergy > 1e-10f) ? (inharmonicEnergy / totalEnergy) : 0.0f;

    // Consider inharmonic if more than 5% of energy is non-harmonic
    return inharmonicRatio > 0.05f;
}

} // anonymous namespace

// =============================================================================
// Test Placeholder - Will be filled in subsequent phases
// =============================================================================

TEST_CASE("RingSaturation test infrastructure compiles", "[ring_saturation][infrastructure]") {
    // Test that our helper functions compile and work
    constexpr size_t kNumSamples = 1024;
    constexpr float kSampleRate = 44100.0f;

    std::vector<float> buffer(kNumSamples);
    generateSineWave(buffer.data(), kNumSamples, 440.0f, kSampleRate);

    // Test helper functions
    float dc = calculateDCOffset(buffer.data(), kNumSamples);
    float rms = calculateRMS(buffer.data(), kNumSamples);
    float peak = findPeakAbsolute(buffer.data(), kNumSamples);
    float entropy = calculateSpectralEntropy(buffer.data(), kNumSamples, kSampleRate);

    // Sine wave should have near-zero DC (may have slight offset due to non-integer periods)
    REQUIRE(std::abs(dc) < 0.02f);

    // Sine wave RMS should be ~0.707
    REQUIRE(rms == Approx(0.707f).margin(0.01f));

    // Sine wave peak should be ~1.0
    REQUIRE(peak == Approx(1.0f).margin(0.01f));

    // Pure sine should have low entropy (energy concentrated at one frequency)
    REQUIRE(entropy > 0.0f);
    REQUIRE(entropy < 5.0f); // Low entropy for pure tone
}

// =============================================================================
// Phase 3: User Story 1 - Basic Self-Modulation Distortion Tests
// =============================================================================

// T009: Default constructor test
TEST_CASE("RingSaturation default constructor initializes with correct defaults",
          "[ring_saturation][US1][construction]") {
    RingSaturation ringSat;

    SECTION("default drive is 1.0") {
        REQUIRE(ringSat.getDrive() == Approx(1.0f));
    }

    SECTION("default modulation depth is 1.0") {
        REQUIRE(ringSat.getModulationDepth() == Approx(1.0f));
    }

    SECTION("default stages is 1") {
        REQUIRE(ringSat.getStages() == 1);
    }

    SECTION("default saturation curve is Tanh") {
        REQUIRE(ringSat.getSaturationCurve() == WaveshapeType::Tanh);
    }

    SECTION("not prepared initially") {
        REQUIRE_FALSE(ringSat.isPrepared());
    }
}

// T010: Depth=0 returns input unchanged (SC-002)
TEST_CASE("RingSaturation with depth=0 returns input unchanged (SC-002)",
          "[ring_saturation][US1][bypass]") {
    RingSaturation ringSat;
    ringSat.prepare(44100.0);
    ringSat.setModulationDepth(0.0f);
    ringSat.setDrive(2.0f);

    SECTION("single sample processing") {
        for (float input : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
            float output = ringSat.process(input);
            REQUIRE(output == Approx(input).margin(1e-6f));
        }
    }

    SECTION("block processing") {
        constexpr size_t kNumSamples = 512;
        std::vector<float> buffer(kNumSamples);
        generateSineWave(buffer.data(), kNumSamples, 440.0f, 44100.0f);

        std::vector<float> original = buffer; // Copy before processing
        ringSat.processBlock(buffer.data(), kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            REQUIRE(buffer[i] == Approx(original[i]).margin(1e-6f));
        }
    }
}

// T011: Depth=1.0 and drive=2.0 produces inharmonic sidebands (SC-001)
TEST_CASE("RingSaturation with depth=1.0 produces inharmonic sidebands on 440Hz sine (SC-001)",
          "[ring_saturation][US1][spectral]") {
    RingSaturation ringSat;
    ringSat.prepare(44100.0);
    ringSat.setDrive(2.0f);
    ringSat.setModulationDepth(1.0f);

    constexpr size_t kNumSamples = 4096;
    constexpr float kSampleRate = 44100.0f;
    constexpr float kFundamental = 440.0f;

    std::vector<float> buffer(kNumSamples);
    generateSineWave(buffer.data(), kNumSamples, kFundamental, kSampleRate);

    // Process through ring saturation
    ringSat.processBlock(buffer.data(), kNumSamples);

    // The output should contain inharmonic sidebands from self-modulation
    // Ring modulation creates sum and difference frequencies
    bool hasInharmonic = hasInharmonicSidebands(buffer.data(), kNumSamples, kFundamental, kSampleRate);
    REQUIRE(hasInharmonic);
}

// T012: Modulation depth controls effect scaling
TEST_CASE("RingSaturation modulation depth parameter controls effect scaling",
          "[ring_saturation][US1][depth]") {
    RingSaturation ringSat;
    ringSat.prepare(44100.0);
    ringSat.setDrive(2.0f);

    constexpr size_t kNumSamples = 1024;
    constexpr float kSampleRate = 44100.0f;

    // Test with constant input to see depth effect
    const float testInput = 0.7f;

    SECTION("depth=0.5 produces intermediate effect between depth=0 and depth=1") {
        // With depth=0, output = input (no effect)
        ringSat.setModulationDepth(0.0f);
        float outputDepth0 = ringSat.process(testInput);

        ringSat.reset();
        ringSat.prepare(44100.0);

        // With depth=1, full effect
        ringSat.setModulationDepth(1.0f);
        float outputDepth1 = ringSat.process(testInput);

        ringSat.reset();
        ringSat.prepare(44100.0);

        // With depth=0.5, should be halfway
        ringSat.setModulationDepth(0.5f);
        float outputDepth05 = ringSat.process(testInput);

        // The difference from dry signal should be proportional to depth
        float diffDepth1 = std::abs(outputDepth1 - outputDepth0);
        float diffDepth05 = std::abs(outputDepth05 - outputDepth0);

        // depth=0.5 should have roughly half the difference from dry as depth=1
        if (diffDepth1 > 0.01f) { // Only test if there's meaningful difference
            REQUIRE(diffDepth05 == Approx(diffDepth1 * 0.5f).margin(0.05f));
        }
    }

    SECTION("depth is clamped to [0.0, 1.0]") {
        ringSat.setModulationDepth(-0.5f);
        REQUIRE(ringSat.getModulationDepth() == Approx(0.0f));

        ringSat.setModulationDepth(1.5f);
        REQUIRE(ringSat.getModulationDepth() == Approx(1.0f));

        ringSat.setModulationDepth(0.7f);
        REQUIRE(ringSat.getModulationDepth() == Approx(0.7f));
    }
}

// T013: Drive parameter affects saturation intensity
TEST_CASE("RingSaturation drive parameter affects saturation intensity",
          "[ring_saturation][US1][drive]") {
    constexpr size_t kNumSamples = 1024;
    constexpr float kSampleRate = 44100.0f;

    SECTION("higher drive produces more saturation") {
        std::vector<float> bufferLowDrive(kNumSamples);
        std::vector<float> bufferHighDrive(kNumSamples);

        generateSineWave(bufferLowDrive.data(), kNumSamples, 440.0f, kSampleRate);
        generateSineWave(bufferHighDrive.data(), kNumSamples, 440.0f, kSampleRate);

        RingSaturation ringSatLow;
        ringSatLow.prepare(kSampleRate);
        ringSatLow.setDrive(1.0f);
        ringSatLow.setModulationDepth(1.0f);
        ringSatLow.processBlock(bufferLowDrive.data(), kNumSamples);

        RingSaturation ringSatHigh;
        ringSatHigh.prepare(kSampleRate);
        ringSatHigh.setDrive(5.0f);
        ringSatHigh.setModulationDepth(1.0f);
        ringSatHigh.processBlock(bufferHighDrive.data(), kNumSamples);

        // Higher drive should produce more harmonic content (higher entropy)
        float entropyLow = calculateSpectralEntropy(bufferLowDrive.data(), kNumSamples, kSampleRate);
        float entropyHigh = calculateSpectralEntropy(bufferHighDrive.data(), kNumSamples, kSampleRate);

        REQUIRE(entropyHigh > entropyLow);
    }

    SECTION("drive=0 produces output = input * (1 - depth) per spec edge case") {
        RingSaturation ringSat;
        ringSat.prepare(kSampleRate);
        ringSat.setDrive(0.0f);
        ringSat.setModulationDepth(0.5f);

        // With drive=0 and depth=0.5: output = input * (1 - 0.5) = input * 0.5
        float input = 0.8f;
        float output = ringSat.process(input);
        REQUIRE(output == Approx(input * 0.5f).margin(0.01f));
    }

    SECTION("negative drive is clamped to 0") {
        RingSaturation ringSat;
        ringSat.prepare(kSampleRate);
        ringSat.setDrive(-2.0f);
        REQUIRE(ringSat.getDrive() == Approx(0.0f));
    }
}

// T014: Unprepared processor returns input unchanged
TEST_CASE("RingSaturation unprepared processor returns input unchanged",
          "[ring_saturation][US1][lifecycle]") {
    RingSaturation ringSat;
    // NOT calling prepare()

    SECTION("single sample returns input") {
        for (float input : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
            float output = ringSat.process(input);
            REQUIRE(output == Approx(input));
        }
    }

    SECTION("block processing does nothing when unprepared") {
        constexpr size_t kNumSamples = 256;
        std::vector<float> buffer(kNumSamples);
        generateSineWave(buffer.data(), kNumSamples, 440.0f, 44100.0f);

        std::vector<float> original = buffer;
        ringSat.processBlock(buffer.data(), kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            REQUIRE(buffer[i] == Approx(original[i]));
        }
    }
}

// T015: Lifecycle methods work correctly
TEST_CASE("RingSaturation prepare() and reset() lifecycle methods work correctly",
          "[ring_saturation][US1][lifecycle]") {
    RingSaturation ringSat;

    SECTION("prepare() marks processor as prepared") {
        REQUIRE_FALSE(ringSat.isPrepared());
        ringSat.prepare(44100.0);
        REQUIRE(ringSat.isPrepared());
    }

    SECTION("prepare() can be called multiple times at different sample rates") {
        ringSat.prepare(44100.0);
        REQUIRE(ringSat.isPrepared());

        ringSat.prepare(96000.0);
        REQUIRE(ringSat.isPrepared());
    }

    SECTION("reset() clears state but preserves prepared status") {
        ringSat.prepare(44100.0);
        ringSat.setDrive(3.0f);
        ringSat.setModulationDepth(0.7f);
        ringSat.setStages(2);

        // Process some samples to change internal state
        for (int i = 0; i < 100; ++i) {
            [[maybe_unused]] float output = ringSat.process(0.5f);
        }

        ringSat.reset();

        // Should still be prepared
        REQUIRE(ringSat.isPrepared());

        // Parameters should be preserved
        REQUIRE(ringSat.getDrive() == Approx(3.0f));
        REQUIRE(ringSat.getModulationDepth() == Approx(0.7f));
        REQUIRE(ringSat.getStages() == 2);
    }
}

// T027b: processBlock produces identical output to N sequential process() calls (FR-020)
TEST_CASE("RingSaturation processBlock() produces identical output to N process() calls (FR-020)",
          "[ring_saturation][US1][block]") {
    constexpr size_t kNumSamples = 256;
    constexpr float kSampleRate = 44100.0f;

    std::vector<float> inputBuffer(kNumSamples);
    generateSineWave(inputBuffer.data(), kNumSamples, 440.0f, kSampleRate);

    // Process using processBlock
    RingSaturation ringSatBlock;
    ringSatBlock.prepare(kSampleRate);
    ringSatBlock.setDrive(2.0f);
    ringSatBlock.setModulationDepth(1.0f);

    std::vector<float> blockOutput = inputBuffer;
    ringSatBlock.processBlock(blockOutput.data(), kNumSamples);

    // Process using individual process() calls
    RingSaturation ringSatSample;
    ringSatSample.prepare(kSampleRate);
    ringSatSample.setDrive(2.0f);
    ringSatSample.setModulationDepth(1.0f);

    std::vector<float> sampleOutput(kNumSamples);
    for (size_t i = 0; i < kNumSamples; ++i) {
        sampleOutput[i] = ringSatSample.process(inputBuffer[i]);
    }

    // Both outputs should be identical
    for (size_t i = 0; i < kNumSamples; ++i) {
        REQUIRE(blockOutput[i] == Approx(sampleOutput[i]).margin(1e-7f));
    }
}

// =============================================================================
// Constants Tests (from contract)
// =============================================================================

TEST_CASE("RingSaturation constants are correct", "[ring_saturation][US1][constants]") {
    REQUIRE(RingSaturation::kMinStages == 1);
    REQUIRE(RingSaturation::kMaxStages == 4);
    REQUIRE(RingSaturation::kDCBlockerCutoffHz == Approx(10.0f));
    REQUIRE(RingSaturation::kCrossfadeTimeMs == Approx(10.0f));
    REQUIRE(RingSaturation::kSoftLimitScale == Approx(2.0f));
}
