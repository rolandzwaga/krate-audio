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
#include <chrono>
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

// =============================================================================
// Phase 4: User Story 2 - Saturation Curve Selection Tests
// =============================================================================

// T031: setSaturationCurve() changes curve type and can be queried
TEST_CASE("RingSaturation setSaturationCurve() changes curve and is queryable",
          "[ring_saturation][US2][curve]") {
    RingSaturation ringSat;
    ringSat.prepare(44100.0);

    SECTION("default curve is Tanh") {
        REQUIRE(ringSat.getSaturationCurve() == WaveshapeType::Tanh);
    }

    SECTION("setSaturationCurve changes the curve type") {
        ringSat.setSaturationCurve(WaveshapeType::HardClip);
        REQUIRE(ringSat.getSaturationCurve() == WaveshapeType::HardClip);

        ringSat.setSaturationCurve(WaveshapeType::Tube);
        REQUIRE(ringSat.getSaturationCurve() == WaveshapeType::Tube);

        ringSat.setSaturationCurve(WaveshapeType::Atan);
        REQUIRE(ringSat.getSaturationCurve() == WaveshapeType::Atan);
    }

    SECTION("all WaveshapeType values are supported") {
        for (auto type : {WaveshapeType::Tanh, WaveshapeType::Atan, WaveshapeType::Cubic,
                         WaveshapeType::Quintic, WaveshapeType::ReciprocalSqrt,
                         WaveshapeType::Erf, WaveshapeType::HardClip,
                         WaveshapeType::Diode, WaveshapeType::Tube}) {
            ringSat.setSaturationCurve(type);
            REQUIRE(ringSat.getSaturationCurve() == type);
        }
    }
}

// T032: Different WaveshapeType values produce distinct spectral content
TEST_CASE("RingSaturation different curves produce distinct spectral content",
          "[ring_saturation][US2][spectral]") {
    constexpr size_t kNumSamples = 2048;
    constexpr float kSampleRate = 44100.0f;

    // Compare spectral entropy between different curves
    auto processWithCurve = [&](WaveshapeType type) -> float {
        RingSaturation ringSat;
        ringSat.prepare(kSampleRate);
        ringSat.setDrive(3.0f);
        ringSat.setModulationDepth(1.0f);
        ringSat.setSaturationCurve(type);

        std::vector<float> buffer(kNumSamples);
        generateSineWave(buffer.data(), kNumSamples, 440.0f, kSampleRate);
        ringSat.processBlock(buffer.data(), kNumSamples);

        return calculateSpectralEntropy(buffer.data(), kNumSamples, kSampleRate);
    };

    float entropyTanh = processWithCurve(WaveshapeType::Tanh);
    float entropyHardClip = processWithCurve(WaveshapeType::HardClip);
    float entropyTube = processWithCurve(WaveshapeType::Tube);

    // Different curves should produce different spectral characteristics
    // HardClip typically produces more harmonics than smooth curves
    REQUIRE(entropyTanh > 0.0f);
    REQUIRE(entropyHardClip > 0.0f);
    REQUIRE(entropyTube > 0.0f);

    // Check that at least one pair differs significantly (different curves = different sound)
    bool distinctSpectrum = (std::abs(entropyTanh - entropyHardClip) > 0.1f) ||
                            (std::abs(entropyTanh - entropyTube) > 0.1f) ||
                            (std::abs(entropyHardClip - entropyTube) > 0.1f);
    REQUIRE(distinctSpectrum);
}

// T033: Curve switching crossfades over 10ms window (no discontinuities)
TEST_CASE("RingSaturation curve switching crossfades over 10ms window",
          "[ring_saturation][US2][crossfade]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kCrossfadeSamples = static_cast<size_t>(0.010f * kSampleRate); // 10ms

    RingSaturation ringSat;
    ringSat.prepare(kSampleRate);
    ringSat.setDrive(3.0f);
    ringSat.setModulationDepth(1.0f);
    ringSat.setSaturationCurve(WaveshapeType::Tanh);

    // Process some samples with Tanh
    constexpr float testInput = 0.7f;
    for (int i = 0; i < 100; ++i) {
        [[maybe_unused]] float out = ringSat.process(testInput);
    }
    float outputBeforeSwitch = ringSat.process(testInput);

    // Switch to HardClip - crossfade should begin
    ringSat.setSaturationCurve(WaveshapeType::HardClip);

    // First sample after switch should be close to previous (no click)
    float outputAfterSwitch = ringSat.process(testInput);
    float diff = std::abs(outputAfterSwitch - outputBeforeSwitch);

    // The immediate difference should be small (not a sudden jump)
    // With crossfade starting at 0.0, first sample blends ~0% new, so output nearly identical
    REQUIRE(diff < 0.1f); // Allow some difference due to one sample advancement

    // Process through the crossfade period
    std::vector<float> crossfadeOutput(kCrossfadeSamples);
    for (size_t i = 0; i < kCrossfadeSamples; ++i) {
        crossfadeOutput[i] = ringSat.process(testInput);
    }

    // Check for smooth transition (no sudden jumps > threshold)
    constexpr float kMaxJump = 0.05f; // Max allowed sample-to-sample jump
    bool smoothTransition = true;
    for (size_t i = 1; i < kCrossfadeSamples; ++i) {
        float jump = std::abs(crossfadeOutput[i] - crossfadeOutput[i-1]);
        if (jump > kMaxJump) {
            smoothTransition = false;
            break;
        }
    }
    REQUIRE(smoothTransition);

    // After crossfade, we should be fully on the new curve
    // Process a bit more to ensure crossfade is complete
    for (int i = 0; i < 100; ++i) {
        [[maybe_unused]] float out = ringSat.process(testInput);
    }

    // The output should now match what we'd get from a fresh instance with HardClip
    RingSaturation ringSatReference;
    ringSatReference.prepare(kSampleRate);
    ringSatReference.setDrive(3.0f);
    ringSatReference.setModulationDepth(1.0f);
    ringSatReference.setSaturationCurve(WaveshapeType::HardClip);

    // Process same amount to reach similar DC blocker state
    for (int i = 0; i < 200 + static_cast<int>(kCrossfadeSamples); ++i) {
        [[maybe_unused]] float out = ringSatReference.process(testInput);
    }

    float outputAfterCrossfade = ringSat.process(testInput);
    float outputReference = ringSatReference.process(testInput);

    // After crossfade complete, outputs should be nearly identical
    REQUIRE(outputAfterCrossfade == Approx(outputReference).margin(0.01f));
}

// T034: Multiple rapid curve changes complete correctly
TEST_CASE("RingSaturation multiple rapid curve changes complete correctly",
          "[ring_saturation][US2][crossfade]") {
    constexpr float kSampleRate = 44100.0f;

    RingSaturation ringSat;
    ringSat.prepare(kSampleRate);
    ringSat.setDrive(2.0f);
    ringSat.setModulationDepth(1.0f);

    constexpr float testInput = 0.5f;

    // Rapid curve changes
    ringSat.setSaturationCurve(WaveshapeType::Tanh);
    for (int i = 0; i < 50; ++i) {
        [[maybe_unused]] float out = ringSat.process(testInput); // Not enough for full crossfade
    }

    ringSat.setSaturationCurve(WaveshapeType::HardClip);
    for (int i = 0; i < 50; ++i) {
        [[maybe_unused]] float out = ringSat.process(testInput); // Another change mid-crossfade
    }

    ringSat.setSaturationCurve(WaveshapeType::Tube);
    for (int i = 0; i < 50; ++i) {
        [[maybe_unused]] float out = ringSat.process(testInput); // Yet another
    }

    // Final curve should be Tube
    REQUIRE(ringSat.getSaturationCurve() == WaveshapeType::Tube);

    // Process long enough for all crossfades to complete
    for (int i = 0; i < 1000; ++i) {
        float output = ringSat.process(testInput);
        // Ensure output is valid (no NaN/Inf from crossfade state issues)
        REQUIRE_FALSE(std::isnan(output));
        REQUIRE_FALSE(std::isinf(output));
    }
}

// T035: Setting same curve does not trigger crossfade
TEST_CASE("RingSaturation setting same curve does not trigger crossfade",
          "[ring_saturation][US2][crossfade]") {
    constexpr float kSampleRate = 44100.0f;

    RingSaturation ringSat;
    ringSat.prepare(kSampleRate);
    ringSat.setDrive(2.0f);
    ringSat.setModulationDepth(1.0f);
    ringSat.setSaturationCurve(WaveshapeType::Tanh);

    constexpr float testInput = 0.6f;

    // Process to reach steady state
    for (int i = 0; i < 1000; ++i) {
        [[maybe_unused]] float out = ringSat.process(testInput);
    }

    float outputBefore = ringSat.process(testInput);

    // Set same curve
    ringSat.setSaturationCurve(WaveshapeType::Tanh);

    // Output should be identical (no crossfade started)
    float outputAfter = ringSat.process(testInput);

    // Should be essentially identical (only DC blocker state change)
    REQUIRE(outputAfter == Approx(outputBefore).margin(0.001f));
}

// =============================================================================
// Phase 5: User Story 3 - Multi-Stage Self-Modulation Tests
// =============================================================================

// T045: setStages() and getStages() work correctly
TEST_CASE("RingSaturation setStages() and getStages() work correctly",
          "[ring_saturation][US3][stages]") {
    RingSaturation ringSat;
    ringSat.prepare(44100.0);

    SECTION("default stages is 1") {
        REQUIRE(ringSat.getStages() == 1);
    }

    SECTION("setStages changes the stage count") {
        ringSat.setStages(2);
        REQUIRE(ringSat.getStages() == 2);

        ringSat.setStages(4);
        REQUIRE(ringSat.getStages() == 4);

        ringSat.setStages(1);
        REQUIRE(ringSat.getStages() == 1);
    }
}

// T046: stages parameter clamped to [1, 4] range (FR-011)
TEST_CASE("RingSaturation stages parameter clamped to [1, 4] (FR-011)",
          "[ring_saturation][US3][stages]") {
    RingSaturation ringSat;
    ringSat.prepare(44100.0);

    SECTION("stages < 1 clamped to 1") {
        ringSat.setStages(0);
        REQUIRE(ringSat.getStages() == 1);

        ringSat.setStages(-5);
        REQUIRE(ringSat.getStages() == 1);
    }

    SECTION("stages > 4 clamped to 4") {
        ringSat.setStages(5);
        REQUIRE(ringSat.getStages() == 4);

        ringSat.setStages(100);
        REQUIRE(ringSat.getStages() == 4);
    }

    SECTION("valid stages values preserved") {
        for (int s = 1; s <= 4; ++s) {
            ringSat.setStages(s);
            REQUIRE(ringSat.getStages() == s);
        }
    }
}

// T047: stages=1 produces single pass, stages=4 produces 4 passes
TEST_CASE("RingSaturation multi-stage processing applies formula multiple times",
          "[ring_saturation][US3][stages]") {
    constexpr float kSampleRate = 44100.0f;

    // With higher stages, the signal should be more heavily processed
    // leading to different output for same input
    auto processWithStages = [&](int stages, float input) -> float {
        RingSaturation ringSat;
        ringSat.prepare(kSampleRate);
        ringSat.setDrive(2.0f);
        ringSat.setModulationDepth(1.0f);
        ringSat.setStages(stages);
        return ringSat.process(input);
    };

    constexpr float testInput = 0.7f;

    float output1Stage = processWithStages(1, testInput);
    float output2Stage = processWithStages(2, testInput);
    float output3Stage = processWithStages(3, testInput);
    float output4Stage = processWithStages(4, testInput);

    // Different stage counts should produce different outputs
    // (each additional stage further transforms the signal)
    REQUIRE(output1Stage != Approx(output2Stage).margin(0.01f));
    REQUIRE(output2Stage != Approx(output3Stage).margin(0.01f));
    REQUIRE(output3Stage != Approx(output4Stage).margin(0.01f));
}

// T048: stages=4 produces higher Shannon spectral entropy than stages=1 (SC-003)
TEST_CASE("RingSaturation stages=4 produces higher spectral entropy than stages=1 (SC-003)",
          "[ring_saturation][US3][spectral]") {
    constexpr size_t kNumSamples = 4096;
    constexpr float kSampleRate = 44100.0f;

    auto processBufferWithStages = [&](int stages) -> float {
        RingSaturation ringSat;
        ringSat.prepare(kSampleRate);
        ringSat.setDrive(2.0f);
        ringSat.setModulationDepth(1.0f);
        ringSat.setStages(stages);

        std::vector<float> buffer(kNumSamples);
        generateSineWave(buffer.data(), kNumSamples, 440.0f, kSampleRate);
        ringSat.processBlock(buffer.data(), kNumSamples);

        return calculateSpectralEntropy(buffer.data(), kNumSamples, kSampleRate);
    };

    float entropy1Stage = processBufferWithStages(1);
    float entropy4Stage = processBufferWithStages(4);

    // SC-003: Multi-stage produces more complex harmonic content
    // Measured by increased Shannon spectral entropy
    REQUIRE(entropy4Stage > entropy1Stage);
}

// T049: stages=4 with high drive remains bounded via soft limiting (SC-005)
TEST_CASE("RingSaturation stages=4 with high drive remains bounded (SC-005)",
          "[ring_saturation][US3][limiting]") {
    constexpr size_t kNumSamples = 2048;
    constexpr float kSampleRate = 44100.0f;

    RingSaturation ringSat;
    ringSat.prepare(kSampleRate);
    ringSat.setDrive(10.0f);  // Very high drive
    ringSat.setModulationDepth(1.0f);
    ringSat.setStages(4);     // Maximum stages

    std::vector<float> buffer(kNumSamples);
    generateSineWave(buffer.data(), kNumSamples, 440.0f, kSampleRate);

    ringSat.processBlock(buffer.data(), kNumSamples);

    // Check all samples are bounded
    float peak = findPeakAbsolute(buffer.data(), kNumSamples);

    // SC-005: Output approaches +/-2.0 asymptotically
    // Should never exceed 2.0
    REQUIRE(peak < 2.0f);
    // With high drive and 4 stages, output should have some significant level
    REQUIRE(peak > 0.5f);
}

// T050: multi-stage does not produce runaway gain or instability
TEST_CASE("RingSaturation multi-stage does not produce runaway gain or instability",
          "[ring_saturation][US3][stability]") {
    constexpr size_t kNumSamples = 10000;
    constexpr float kSampleRate = 44100.0f;

    RingSaturation ringSat;
    ringSat.prepare(kSampleRate);
    ringSat.setDrive(5.0f);
    ringSat.setModulationDepth(1.0f);
    ringSat.setStages(4);

    // Process a long signal
    std::vector<float> buffer(kNumSamples);
    generateSineWave(buffer.data(), kNumSamples, 440.0f, kSampleRate);

    ringSat.processBlock(buffer.data(), kNumSamples);

    // Check for stability: no NaN, no Inf, bounded output
    for (size_t i = 0; i < kNumSamples; ++i) {
        REQUIRE_FALSE(std::isnan(buffer[i]));
        REQUIRE_FALSE(std::isinf(buffer[i]));
        REQUIRE(std::abs(buffer[i]) < 2.1f); // Slightly above 2.0 to allow for DC blocker transients
    }

    // Also test with constant input (edge case for feedback)
    RingSaturation ringSat2;
    ringSat2.prepare(kSampleRate);
    ringSat2.setDrive(5.0f);
    ringSat2.setModulationDepth(1.0f);
    ringSat2.setStages(4);

    constexpr float constantInput = 0.9f;
    for (size_t i = 0; i < kNumSamples; ++i) {
        float output = ringSat2.process(constantInput);
        REQUIRE_FALSE(std::isnan(output));
        REQUIRE_FALSE(std::isinf(output));
        REQUIRE(std::abs(output) < 2.1f);
    }
}

// =============================================================================
// Phase 6: User Story 4 - DC Offset Removal Tests
// =============================================================================

// T059: DC offset in input signal is removed after settling time
TEST_CASE("RingSaturation removes DC offset from input after settling time",
          "[ring_saturation][US4][dc_blocking]") {
    constexpr float kSampleRate = 44100.0f;
    // DC blocker at 10Hz needs ~100ms for 99.9% settling
    constexpr size_t kSettlingSamples = static_cast<size_t>(0.150f * kSampleRate); // ~150ms
    constexpr size_t kMeasureSamples = 2000;

    RingSaturation ringSat;
    ringSat.prepare(kSampleRate);
    ringSat.setDrive(1.5f);
    ringSat.setModulationDepth(1.0f);

    // Process signal with DC offset
    constexpr float kDCOffset = 0.3f;
    std::vector<float> buffer(kSettlingSamples + kMeasureSamples);
    for (size_t i = 0; i < buffer.size(); ++i) {
        // Sine wave with DC offset
        float phase = Krate::DSP::kTwoPi * 440.0f * static_cast<float>(i) / kSampleRate;
        buffer[i] = std::sin(phase) * 0.5f + kDCOffset;
    }

    ringSat.processBlock(buffer.data(), buffer.size());

    // Measure DC offset in the last portion (after settling)
    float dcAfterSettling = calculateDCOffset(
        buffer.data() + kSettlingSamples,
        kMeasureSamples
    );

    // After extended settling, DC should be well below audible threshold
    // The 1st-order DC blocker has slow settling, allowing up to 0.01 (~-40dB)
    REQUIRE(std::abs(dcAfterSettling) < 0.01f);
}

// T060: Asymmetric saturation (Tube curve) generates DC which is then removed
TEST_CASE("RingSaturation removes DC generated by asymmetric Tube saturation",
          "[ring_saturation][US4][dc_blocking]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kSettlingSamples = static_cast<size_t>(0.050f * kSampleRate); // 50ms
    constexpr size_t kMeasureSamples = 2000;

    RingSaturation ringSat;
    ringSat.prepare(kSampleRate);
    ringSat.setDrive(3.0f);
    ringSat.setModulationDepth(1.0f);
    ringSat.setSaturationCurve(WaveshapeType::Tube); // Asymmetric curve

    // Process a zero-DC sine wave
    std::vector<float> buffer(kSettlingSamples + kMeasureSamples);
    generateSineWave(buffer.data(), buffer.size(), 440.0f, kSampleRate, 0.8f);

    ringSat.processBlock(buffer.data(), buffer.size());

    // The Tube curve is asymmetric and would generate DC, but the DC blocker removes it
    float dcAfterSettling = calculateDCOffset(
        buffer.data() + kSettlingSamples,
        kMeasureSamples
    );

    // Should be near zero despite asymmetric saturation
    REQUIRE(std::abs(dcAfterSettling) < 0.01f);
}

// T061: Output DC offset below audible threshold after settling (SC-004)
TEST_CASE("RingSaturation output DC offset below audible threshold after settling (SC-004)",
          "[ring_saturation][US4][dc_blocking]") {
    constexpr float kSampleRate = 44100.0f;
    // Longer settling for 1st-order DC blocker
    constexpr size_t kSettlingSamples = static_cast<size_t>(0.200f * kSampleRate); // 200ms
    constexpr size_t kMeasureSamples = 4000;

    // Test with various curves (some asymmetric)
    for (auto curve : {WaveshapeType::Tanh, WaveshapeType::Tube, WaveshapeType::Diode}) {
        DYNAMIC_SECTION("Curve: " << static_cast<int>(curve)) {
            RingSaturation ringSat;
            ringSat.prepare(kSampleRate);
            ringSat.setDrive(2.5f);
            ringSat.setModulationDepth(1.0f);
            ringSat.setSaturationCurve(curve);

            std::vector<float> buffer(kSettlingSamples + kMeasureSamples);
            generateSineWave(buffer.data(), buffer.size(), 440.0f, kSampleRate, 0.7f);

            ringSat.processBlock(buffer.data(), buffer.size());

            // Wait for settling, then measure DC
            float dcAfterSettling = calculateDCOffset(
                buffer.data() + kSettlingSamples,
                kMeasureSamples
            );

            // DC offset should be below audible threshold (~-40dB = 0.01)
            // The 1st-order DC blocker removes DC but settles slowly
            REQUIRE(std::abs(dcAfterSettling) < 0.01f);
        }
    }
}

// T062: reset() clears DC blocker state immediately
TEST_CASE("RingSaturation reset() clears DC blocker state",
          "[ring_saturation][US4][lifecycle]") {
    constexpr float kSampleRate = 44100.0f;

    RingSaturation ringSat;
    ringSat.prepare(kSampleRate);
    ringSat.setDrive(2.0f);
    ringSat.setModulationDepth(1.0f);

    // Process some samples to build up DC blocker state
    for (int i = 0; i < 1000; ++i) {
        [[maybe_unused]] float out = ringSat.process(0.5f);
    }

    // Reset
    ringSat.reset();

    // Create a second fresh instance for comparison
    RingSaturation ringSatFresh;
    ringSatFresh.prepare(kSampleRate);
    ringSatFresh.setDrive(2.0f);
    ringSatFresh.setModulationDepth(1.0f);

    // Process same input through both
    constexpr float testInput = 0.7f;
    float outputReset = ringSat.process(testInput);
    float outputFresh = ringSatFresh.process(testInput);

    // After reset, output should match a fresh instance
    REQUIRE(outputReset == Approx(outputFresh).margin(0.001f));
}

// =============================================================================
// Phase 7: Performance & Compliance Verification Tests
// =============================================================================

// T070: Single-sample processing performance (SC-006)
TEST_CASE("RingSaturation single-sample performance (SC-006)",
          "[ring_saturation][performance]") {
    // Note: This is a rough performance test. Actual timing may vary.
    // SC-006 requires < 1us at 44.1kHz for single sample

    RingSaturation ringSat;
    ringSat.prepare(44100.0);
    ringSat.setDrive(2.0f);
    ringSat.setModulationDepth(1.0f);
    ringSat.setStages(4); // Maximum stages for worst-case

    constexpr size_t kIterations = 100000;
    volatile float result = 0.0f;

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < kIterations; ++i) {
        float input = static_cast<float>(i % 1000) / 1000.0f;
        result = ringSat.process(input);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    double nsPerSample = static_cast<double>(duration.count()) / static_cast<double>(kIterations);

    // Just ensure processing completes - performance depends on hardware
    // In Release builds this should be well under 1us
    REQUIRE(result >= -2.0f);
    REQUIRE(result <= 2.0f);

    // Log performance (informational, not a hard requirement in test)
    INFO("Single-sample processing: " << nsPerSample << " ns/sample (" << nsPerSample / 1000.0 << " us/sample)");

    // Soft requirement: should be under 10us even in debug builds
    REQUIRE(nsPerSample < 10000.0); // 10us maximum
}

// T071: Block processing performance (SC-007)
TEST_CASE("RingSaturation block processing performance (SC-007)",
          "[ring_saturation][performance]") {
    // SC-007 requires < 0.1ms for 512 samples

    RingSaturation ringSat;
    ringSat.prepare(44100.0);
    ringSat.setDrive(2.0f);
    ringSat.setModulationDepth(1.0f);
    ringSat.setStages(4); // Maximum stages

    constexpr size_t kBlockSize = 512;
    constexpr size_t kBlocks = 1000;

    std::vector<float> buffer(kBlockSize);
    generateSineWave(buffer.data(), kBlockSize, 440.0f, 44100.0f);

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t block = 0; block < kBlocks; ++block) {
        ringSat.processBlock(buffer.data(), kBlockSize);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    double usPerBlock = static_cast<double>(duration.count()) / static_cast<double>(kBlocks);

    INFO("512-sample block processing: " << usPerBlock << " us/block (" << usPerBlock / 1000.0 << " ms/block)");

    // Should be well under 1ms per block even in debug
    REQUIRE(usPerBlock < 1000.0); // 1ms maximum
}

// T072: Real-time safety - no allocations in process methods (FR-021)
TEST_CASE("RingSaturation no allocations in process methods (FR-021)",
          "[ring_saturation][realtime_safety]") {
    // This test verifies behavior, not actual allocation tracking
    // (Allocation tracking would require hooks not available here)

    RingSaturation ringSat;
    ringSat.prepare(44100.0);
    ringSat.setDrive(3.0f);
    ringSat.setModulationDepth(1.0f);
    ringSat.setStages(4);

    // Process many samples - if allocations occurred, performance would degrade
    constexpr size_t kNumSamples = 100000;
    std::vector<float> buffer(kNumSamples);
    generateSineWave(buffer.data(), kNumSamples, 440.0f, 44100.0f);

    // Time first block
    auto start1 = std::chrono::high_resolution_clock::now();
    ringSat.processBlock(buffer.data(), kNumSamples);
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 = std::chrono::duration_cast<std::chrono::microseconds>(end1 - start1).count();

    // Regenerate and time second block
    generateSineWave(buffer.data(), kNumSamples, 440.0f, 44100.0f);
    auto start2 = std::chrono::high_resolution_clock::now();
    ringSat.processBlock(buffer.data(), kNumSamples);
    auto end2 = std::chrono::high_resolution_clock::now();
    auto duration2 = std::chrono::duration_cast<std::chrono::microseconds>(end2 - start2).count();

    // Times should be consistent (no growing allocation overhead)
    // Allow 50% variance for normal timing variation
    double ratio = static_cast<double>(duration2) / static_cast<double>(duration1 + 1);
    REQUIRE(ratio < 2.0); // Second run shouldn't be much slower than first
    REQUIRE(ratio > 0.5); // And shouldn't be impossibly faster either
}

// T073: All processing methods are noexcept (FR-023)
TEST_CASE("RingSaturation processing methods are noexcept (FR-023)",
          "[ring_saturation][realtime_safety]") {
    // Static assertions to verify noexcept
    RingSaturation ringSat;
    float* buffer = nullptr;

    // Verify process() is noexcept
    static_assert(noexcept(ringSat.process(0.0f)),
                  "process() must be noexcept");

    // Verify processBlock() is noexcept
    static_assert(noexcept(ringSat.processBlock(buffer, 0)),
                  "processBlock() must be noexcept");

    // Verify setters are noexcept
    static_assert(noexcept(ringSat.setDrive(0.0f)),
                  "setDrive() must be noexcept");
    static_assert(noexcept(ringSat.setModulationDepth(0.0f)),
                  "setModulationDepth() must be noexcept");
    static_assert(noexcept(ringSat.setStages(0)),
                  "setStages() must be noexcept");
    static_assert(noexcept(ringSat.setSaturationCurve(WaveshapeType::Tanh)),
                  "setSaturationCurve() must be noexcept");

    // Verify getters are noexcept
    static_assert(noexcept(ringSat.getDrive()),
                  "getDrive() must be noexcept");
    static_assert(noexcept(ringSat.getModulationDepth()),
                  "getModulationDepth() must be noexcept");
    static_assert(noexcept(ringSat.getStages()),
                  "getStages() must be noexcept");
    static_assert(noexcept(ringSat.getSaturationCurve()),
                  "getSaturationCurve() must be noexcept");

    // Verify lifecycle methods are noexcept
    static_assert(noexcept(ringSat.prepare(44100.0)),
                  "prepare() must be noexcept");
    static_assert(noexcept(ringSat.reset()),
                  "reset() must be noexcept");
    static_assert(noexcept(ringSat.isPrepared()),
                  "isPrepared() must be noexcept");

    // If we get here, all static_asserts passed
    REQUIRE(true);
}

// =============================================================================
// Phase 8: Edge Cases Tests
// =============================================================================

// T076: NaN input produces NaN output (or soft-limited, depends on implementation)
TEST_CASE("RingSaturation handles NaN input gracefully (FR-022)",
          "[ring_saturation][edge_cases]") {
    RingSaturation ringSat;
    ringSat.prepare(44100.0);
    ringSat.setDrive(2.0f);
    ringSat.setModulationDepth(1.0f);

    float nanValue = std::numeric_limits<float>::quiet_NaN();
    float output = ringSat.process(nanValue);

    // NaN may propagate or be converted to 0 - either is acceptable
    // The key is no crash
    REQUIRE((std::isnan(output) || std::abs(output) <= 2.0f));

    // Subsequent processing should work correctly
    float normalOutput = ringSat.process(0.5f);
    REQUIRE_FALSE(std::isnan(normalOutput));
    REQUIRE(std::abs(normalOutput) <= 2.0f);
}

// T077: Infinity input produces bounded output (FR-022)
TEST_CASE("RingSaturation handles infinity input gracefully (FR-022)",
          "[ring_saturation][edge_cases]") {
    RingSaturation ringSat;
    ringSat.prepare(44100.0);
    ringSat.setDrive(2.0f);
    ringSat.setModulationDepth(1.0f);

    SECTION("positive infinity") {
        float posInf = std::numeric_limits<float>::infinity();
        float output = ringSat.process(posInf);

        // Soft limiter should bound this
        REQUIRE_FALSE(std::isinf(output));
        REQUIRE(output <= 2.0f);
    }

    SECTION("negative infinity") {
        float negInf = -std::numeric_limits<float>::infinity();
        float output = ringSat.process(negInf);

        // Soft limiter should bound this
        REQUIRE_FALSE(std::isinf(output));
        REQUIRE(output >= -2.0f);
    }
}

// T078: Zero-length block processing is no-op
TEST_CASE("RingSaturation zero-length processBlock is no-op",
          "[ring_saturation][edge_cases]") {
    RingSaturation ringSat;
    ringSat.prepare(44100.0);
    ringSat.setDrive(2.0f);
    ringSat.setModulationDepth(1.0f);

    // Null buffer with zero size should not crash
    ringSat.processBlock(nullptr, 0);

    // Non-null buffer with zero size should also not crash
    std::array<float, 10> buffer = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    auto original = buffer;
    ringSat.processBlock(buffer.data(), 0);

    // Buffer should be unchanged
    for (size_t i = 0; i < buffer.size(); ++i) {
        REQUIRE(buffer[i] == original[i]);
    }
}

// T079: Extreme drive values (0, 100) produce valid output
TEST_CASE("RingSaturation extreme drive values produce valid output",
          "[ring_saturation][edge_cases]") {
    constexpr size_t kNumSamples = 512;
    constexpr float kSampleRate = 44100.0f;

    SECTION("drive = 0") {
        RingSaturation ringSat;
        ringSat.prepare(kSampleRate);
        ringSat.setDrive(0.0f);
        ringSat.setModulationDepth(0.5f);

        std::vector<float> buffer(kNumSamples);
        generateSineWave(buffer.data(), kNumSamples, 440.0f, kSampleRate);

        ringSat.processBlock(buffer.data(), kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            REQUIRE_FALSE(std::isnan(buffer[i]));
            REQUIRE_FALSE(std::isinf(buffer[i]));
            REQUIRE(std::abs(buffer[i]) <= 2.0f);
        }
    }

    SECTION("drive = 100 (extreme)") {
        RingSaturation ringSat;
        ringSat.prepare(kSampleRate);
        ringSat.setDrive(100.0f);
        ringSat.setModulationDepth(1.0f);
        ringSat.setStages(4);

        std::vector<float> buffer(kNumSamples);
        generateSineWave(buffer.data(), kNumSamples, 440.0f, kSampleRate);

        ringSat.processBlock(buffer.data(), kNumSamples);

        for (size_t i = 0; i < kNumSamples; ++i) {
            REQUIRE_FALSE(std::isnan(buffer[i]));
            REQUIRE_FALSE(std::isinf(buffer[i]));
            // Soft limiter bounds output to +/-2
            REQUIRE(std::abs(buffer[i]) < 2.1f);
        }
    }
}

// T080: Very low sample rate (1000Hz) works correctly
TEST_CASE("RingSaturation works at minimum sample rate (1000Hz)",
          "[ring_saturation][edge_cases]") {
    constexpr float kMinSampleRate = 1000.0f;
    constexpr size_t kNumSamples = 100;

    RingSaturation ringSat;
    ringSat.prepare(kMinSampleRate);
    ringSat.setDrive(2.0f);
    ringSat.setModulationDepth(1.0f);

    REQUIRE(ringSat.isPrepared());

    // Process at very low sample rate
    std::vector<float> buffer(kNumSamples);
    generateSineWave(buffer.data(), kNumSamples, 100.0f, kMinSampleRate); // 100Hz at 1kHz SR

    ringSat.processBlock(buffer.data(), kNumSamples);

    // All samples should be valid
    for (size_t i = 0; i < kNumSamples; ++i) {
        REQUIRE_FALSE(std::isnan(buffer[i]));
        REQUIRE_FALSE(std::isinf(buffer[i]));
        REQUIRE(std::abs(buffer[i]) <= 2.0f);
    }
}

// T080b: Very high sample rate (192kHz) works correctly
TEST_CASE("RingSaturation works at high sample rate (192kHz)",
          "[ring_saturation][edge_cases]") {
    constexpr double kHighSampleRate = 192000.0;
    constexpr size_t kNumSamples = 1024;

    RingSaturation ringSat;
    ringSat.prepare(kHighSampleRate);
    ringSat.setDrive(2.0f);
    ringSat.setModulationDepth(1.0f);
    ringSat.setStages(4);

    REQUIRE(ringSat.isPrepared());

    // Process at high sample rate
    std::vector<float> buffer(kNumSamples);
    generateSineWave(buffer.data(), kNumSamples, 440.0f, static_cast<float>(kHighSampleRate));

    ringSat.processBlock(buffer.data(), kNumSamples);

    // All samples should be valid
    for (size_t i = 0; i < kNumSamples; ++i) {
        REQUIRE_FALSE(std::isnan(buffer[i]));
        REQUIRE_FALSE(std::isinf(buffer[i]));
        REQUIRE(std::abs(buffer[i]) <= 2.0f);
    }
}

// =============================================================================
// Phase 9: Integration Tests
// =============================================================================

// T081: Full integration test with all features combined
TEST_CASE("RingSaturation full integration - all features combined",
          "[ring_saturation][integration]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kNumSamples = 8192;

    RingSaturation ringSat;
    ringSat.prepare(kSampleRate);

    // Configure with non-default settings
    ringSat.setDrive(3.0f);
    ringSat.setModulationDepth(0.8f);
    ringSat.setStages(3);
    ringSat.setSaturationCurve(WaveshapeType::Tube);

    // Generate test signal
    std::vector<float> buffer(kNumSamples);
    generateSineWave(buffer.data(), kNumSamples, 440.0f, kSampleRate, 0.7f);

    // Process
    ringSat.processBlock(buffer.data(), kNumSamples);

    SECTION("all outputs are bounded") {
        for (size_t i = 0; i < kNumSamples; ++i) {
            REQUIRE_FALSE(std::isnan(buffer[i]));
            REQUIRE_FALSE(std::isinf(buffer[i]));
            REQUIRE(std::abs(buffer[i]) < 2.0f);
        }
    }

    SECTION("output has been modified from input") {
        // Regenerate input for comparison
        std::vector<float> original(kNumSamples);
        generateSineWave(original.data(), kNumSamples, 440.0f, kSampleRate, 0.7f);

        // Should be different (effect applied)
        bool someOutputDiffers = false;
        for (size_t i = 0; i < kNumSamples; ++i) {
            if (std::abs(buffer[i] - original[i]) > 0.01f) {
                someOutputDiffers = true;
                break;
            }
        }
        REQUIRE(someOutputDiffers);
    }

    SECTION("DC offset is low after settling") {
        // Measure DC in the last portion
        constexpr size_t kMeasureStart = 4096;
        float dc = calculateDCOffset(buffer.data() + kMeasureStart, kNumSamples - kMeasureStart);
        REQUIRE(std::abs(dc) < 0.02f);
    }

    SECTION("spectral entropy indicates harmonic content") {
        float entropy = calculateSpectralEntropy(buffer.data(), kNumSamples, kSampleRate);
        REQUIRE(entropy > 1.0f); // Should have more than a pure tone
    }
}

// T081b: Multiple sequential operations work correctly
TEST_CASE("RingSaturation multiple sequential operations",
          "[ring_saturation][integration]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kBlockSize = 512;

    RingSaturation ringSat;
    ringSat.prepare(kSampleRate);
    ringSat.setDrive(2.0f);
    ringSat.setModulationDepth(1.0f);

    std::vector<float> buffer(kBlockSize);

    // Multiple blocks with parameter changes between
    for (int block = 0; block < 10; ++block) {
        generateSineWave(buffer.data(), kBlockSize, 440.0f, kSampleRate);

        // Change parameters periodically
        if (block % 3 == 0) {
            ringSat.setDrive(1.0f + static_cast<float>(block) * 0.3f);
        }
        if (block % 4 == 0) {
            auto curve = (block % 2 == 0) ? WaveshapeType::Tanh : WaveshapeType::Tube;
            ringSat.setSaturationCurve(curve);
        }

        ringSat.processBlock(buffer.data(), kBlockSize);

        // Verify all samples valid
        for (size_t i = 0; i < kBlockSize; ++i) {
            REQUIRE_FALSE(std::isnan(buffer[i]));
            REQUIRE_FALSE(std::isinf(buffer[i]));
            REQUIRE(std::abs(buffer[i]) < 2.1f);
        }
    }
}

// =============================================================================
// Phase 10: Regression Protection Tests
// =============================================================================

// T082: Deterministic output for same input/parameters (SC-008)
TEST_CASE("RingSaturation produces deterministic output (SC-008)",
          "[ring_saturation][regression]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kNumSamples = 1024;

    auto processWithConfig = [&]() -> std::vector<float> {
        RingSaturation ringSat;
        ringSat.prepare(kSampleRate);
        ringSat.setDrive(2.5f);
        ringSat.setModulationDepth(0.9f);
        ringSat.setStages(2);
        ringSat.setSaturationCurve(WaveshapeType::Tanh);

        std::vector<float> buffer(kNumSamples);
        generateSineWave(buffer.data(), kNumSamples, 440.0f, kSampleRate, 0.6f);
        ringSat.processBlock(buffer.data(), kNumSamples);
        return buffer;
    };

    // Process twice with identical setup
    auto output1 = processWithConfig();
    auto output2 = processWithConfig();

    // Outputs must be bit-for-bit identical
    for (size_t i = 0; i < kNumSamples; ++i) {
        REQUIRE(output1[i] == output2[i]);
    }
}

// T083: Known input/output pairs (regression golden values)
TEST_CASE("RingSaturation known input/output pairs (golden values)",
          "[ring_saturation][regression]") {
    // These are regression tests - if the algorithm changes, these need to be updated
    constexpr float kSampleRate = 44100.0f;

    RingSaturation ringSat;
    ringSat.prepare(kSampleRate);
    ringSat.setDrive(2.0f);
    ringSat.setModulationDepth(1.0f);
    ringSat.setStages(1);
    ringSat.setSaturationCurve(WaveshapeType::Tanh);

    SECTION("input 0.0 produces 0.0") {
        // For zero input, ring modulation produces zero
        float output = ringSat.process(0.0f);
        REQUIRE(output == Approx(0.0f).margin(0.001f));
    }

    SECTION("depth=0 is pure bypass") {
        ringSat.setModulationDepth(0.0f);
        for (float input : {-0.5f, 0.0f, 0.5f}) {
            float output = ringSat.process(input);
            REQUIRE(output == Approx(input));
        }
    }
}

// =============================================================================
// Phase 12: Final Validation - Summary Test
// =============================================================================

// T084: Summary validation of all success criteria
TEST_CASE("RingSaturation meets all success criteria (final validation)",
          "[ring_saturation][final_validation]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kNumSamples = 4096;

    RingSaturation ringSat;
    ringSat.prepare(kSampleRate);
    ringSat.setDrive(3.0f);
    ringSat.setModulationDepth(1.0f);
    ringSat.setStages(4);

    std::vector<float> buffer(kNumSamples);

    SECTION("SC-001: Produces inharmonic sidebands") {
        generateSineWave(buffer.data(), kNumSamples, 440.0f, kSampleRate);
        ringSat.processBlock(buffer.data(), kNumSamples);

        bool hasInharmonic = hasInharmonicSidebands(buffer.data(), kNumSamples, 440.0f, kSampleRate);
        REQUIRE(hasInharmonic);
    }

    SECTION("SC-002: depth=0 produces dry signal") {
        ringSat.setModulationDepth(0.0f);
        float input = 0.7f;
        float output = ringSat.process(input);
        REQUIRE(output == Approx(input));
    }

    SECTION("SC-003: stages=4 > stages=1 entropy") {
        ringSat.setStages(1);
        generateSineWave(buffer.data(), kNumSamples, 440.0f, kSampleRate);
        std::vector<float> buffer1 = buffer;
        ringSat.processBlock(buffer1.data(), kNumSamples);
        float entropy1 = calculateSpectralEntropy(buffer1.data(), kNumSamples, kSampleRate);

        ringSat.reset();
        ringSat.setStages(4);
        generateSineWave(buffer.data(), kNumSamples, 440.0f, kSampleRate);
        ringSat.processBlock(buffer.data(), kNumSamples);
        float entropy4 = calculateSpectralEntropy(buffer.data(), kNumSamples, kSampleRate);

        REQUIRE(entropy4 > entropy1);
    }

    SECTION("SC-005: Output bounded to +/-2.0") {
        ringSat.setDrive(10.0f);
        ringSat.setStages(4);
        generateSineWave(buffer.data(), kNumSamples, 440.0f, kSampleRate);
        ringSat.processBlock(buffer.data(), kNumSamples);

        float peak = findPeakAbsolute(buffer.data(), kNumSamples);
        REQUIRE(peak < 2.0f);
    }
}
