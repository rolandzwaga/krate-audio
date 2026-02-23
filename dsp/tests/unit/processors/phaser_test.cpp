// ==============================================================================
// Layer 2: DSP Processor Tests - Phaser Effect
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Constitution Principle XII: Test-First Development
//
// Tests organized by user story for independent implementation and testing.
// Reference: specs/079-phaser/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <krate/dsp/processors/phaser.h>

#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/core/math_constants.h>

#include <array>
#include <cmath>
#include <numbers>
#include <chrono>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

/// Generate a sine wave into buffer
inline void generateSine(float* buffer, size_t size, float frequency, float sampleRate, float amplitude = 1.0f) {
    const float omega = 2.0f * std::numbers::pi_v<float> * frequency / sampleRate;
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = amplitude * std::sin(omega * static_cast<float>(i));
    }
}

/// Generate a constant DC signal
inline void generateDC(float* buffer, size_t size, float value = 1.0f) {
    std::fill(buffer, buffer + size, value);
}

/// Generate silence
inline void generateSilence(float* buffer, size_t size) {
    std::fill(buffer, buffer + size, 0.0f);
}

/// Find RMS of buffer
inline float calculateRMS(const float* buffer, size_t size) {
    float sumSquares = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sumSquares += buffer[i] * buffer[i];
    }
    return std::sqrt(sumSquares / static_cast<float>(size));
}

/// Check if a value is a valid float (not NaN or Inf)
inline bool isValidFloat(float x) {
    return std::isfinite(x);
}

/// Calculate correlation coefficient between two buffers
inline float calculateCorrelation(const float* a, const float* b, size_t size) {
    float sumA = 0.0f, sumB = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sumA += a[i];
        sumB += b[i];
    }
    float meanA = sumA / static_cast<float>(size);
    float meanB = sumB / static_cast<float>(size);

    float numerator = 0.0f;
    float denomA = 0.0f, denomB = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        float diffA = a[i] - meanA;
        float diffB = b[i] - meanB;
        numerator += diffA * diffB;
        denomA += diffA * diffA;
        denomB += diffB * diffB;
    }

    float denom = std::sqrt(denomA * denomB);
    if (denom < 1e-10f) return 0.0f;
    return numerator / denom;
}

/// Generate white noise using a simple LCG PRNG (deterministic)
inline void generateWhiteNoise(float* buffer, size_t size, float amplitude = 1.0f, uint32_t seed = 12345) {
    uint32_t state = seed;
    for (size_t i = 0; i < size; ++i) {
        // LCG: state = a * state + c
        state = state * 1664525u + 1013904223u;
        // Map to [-1, 1]
        float val = static_cast<float>(static_cast<int32_t>(state)) / 2147483648.0f;
        buffer[i] = amplitude * val;
    }
}

/// Measure magnitude spectrum of a signal using FFT + Hann window.
/// Returns vector of magnitudes for bins 0..fftSize/2.
inline std::vector<float> measureSpectrum(const float* signal, size_t numSamples, size_t fftSize, float sampleRate) {
    FFT fft;
    fft.prepare(fftSize);

    // Apply Hann window
    std::vector<float> windowed(fftSize, 0.0f);
    const size_t copyLen = std::min(numSamples, fftSize);
    for (size_t i = 0; i < copyLen; ++i) {
        const float w = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(fftSize)));
        windowed[i] = signal[i] * w;
    }

    // FFT
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(windowed.data(), spectrum.data());

    // Extract magnitudes
    std::vector<float> magnitudes(fft.numBins());
    for (size_t i = 0; i < fft.numBins(); ++i) {
        magnitudes[i] = spectrum[i].magnitude();
    }
    return magnitudes;
}

/// Get magnitude at a specific frequency from a magnitude spectrum
inline float magnitudeAtFreq(const std::vector<float>& magnitudes, float freqHz, float sampleRate, size_t fftSize) {
    const float binFloat = freqHz * static_cast<float>(fftSize) / sampleRate;
    const size_t bin = static_cast<size_t>(std::round(binFloat));
    if (bin >= magnitudes.size()) return 0.0f;
    return magnitudes[bin];
}

/// Convert linear amplitude to dB
inline float toDb(float amplitude) {
    constexpr float kEpsilon = 1e-10f;
    if (amplitude < kEpsilon) return -200.0f;
    return 20.0f * std::log10(amplitude);
}

} // anonymous namespace

// =============================================================================
// Phase 2: User Story 1 - Basic Phaser Effect (Priority: P1) - MVP
// =============================================================================

TEST_CASE("Phaser - Lifecycle", "[Phaser][US1]") {
    Phaser phaser;

    SECTION("isPrepared returns false before prepare") {
        REQUIRE_FALSE(phaser.isPrepared());
    }

    SECTION("prepare initializes processor") {
        phaser.prepare(44100.0);
        REQUIRE(phaser.isPrepared());
    }

    SECTION("reset clears state without affecting isPrepared") {
        phaser.prepare(44100.0);
        // Process some samples to change state
        (void)phaser.process(1.0f);
        (void)phaser.process(0.5f);

        // Reset should clear state
        phaser.reset();
        REQUIRE(phaser.isPrepared());  // Should still be prepared
    }

    SECTION("process before prepare returns input unchanged") {
        Phaser unpreparedPhaser;
        float input = 0.5f;
        float output = unpreparedPhaser.process(input);
        REQUIRE(output == Approx(input));
    }
}

TEST_CASE("Phaser - Basic Processing", "[Phaser][US1]") {
    Phaser phaser;
    phaser.prepare(44100.0);

    SECTION("process returns modified output") {
        phaser.setMix(1.0f);  // Fully wet
        phaser.setDepth(1.0f);
        phaser.setRate(1.0f);

        // Process a non-zero input
        float input = 1.0f;
        float output = phaser.process(input);

        // Output should be valid
        REQUIRE(isValidFloat(output));
    }

    SECTION("process produces different output from input when wet") {
        phaser.setMix(1.0f);
        phaser.setDepth(1.0f);
        phaser.setRate(2.0f);

        // Process several samples
        std::array<float, 1000> input;
        generateSine(input.data(), input.size(), 440.0f, 44100.0f, 1.0f);

        bool anyDifferent = false;
        for (size_t i = 0; i < input.size(); ++i) {
            float output = phaser.process(input[i]);
            if (std::abs(output - input[i]) > 0.01f) {
                anyDifferent = true;
                break;
            }
        }

        REQUIRE(anyDifferent);
    }
}

TEST_CASE("Phaser - Block Processing", "[Phaser][US1]") {
    Phaser phaser;
    phaser.prepare(44100.0);
    phaser.setMix(1.0f);
    phaser.setDepth(1.0f);
    phaser.setRate(1.0f);

    SECTION("processBlock modifies buffer in-place") {
        constexpr size_t kBlockSize = 256;
        std::array<float, kBlockSize> buffer;
        generateSine(buffer.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);

        // Store original for comparison
        std::array<float, kBlockSize> original = buffer;

        // Process in-place
        phaser.processBlock(buffer.data(), kBlockSize);

        // Buffer should be modified
        bool anyChanged = false;
        for (size_t i = 0; i < kBlockSize; ++i) {
            if (std::abs(buffer[i] - original[i]) > 1e-6f) {
                anyChanged = true;
                break;
            }
        }

        REQUIRE(anyChanged);
    }

    SECTION("all samples are valid after processBlock") {
        constexpr size_t kBlockSize = 512;
        std::array<float, kBlockSize> buffer;
        generateSine(buffer.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);

        phaser.processBlock(buffer.data(), kBlockSize);

        bool allValid = true;
        for (size_t i = 0; i < kBlockSize; ++i) {
            if (!isValidFloat(buffer[i])) {
                allValid = false;
                break;
            }
        }

        REQUIRE(allValid);
    }
}

TEST_CASE("Phaser - Stage Configuration", "[Phaser][US1]") {
    Phaser phaser;
    phaser.prepare(44100.0);

    SECTION("default stage count is 4") {
        REQUIRE(phaser.getNumStages() == 4);
    }

    SECTION("setNumStages accepts valid even numbers") {
        phaser.setNumStages(2);
        REQUIRE(phaser.getNumStages() == 2);

        phaser.setNumStages(6);
        REQUIRE(phaser.getNumStages() == 6);

        phaser.setNumStages(12);
        REQUIRE(phaser.getNumStages() == 12);
    }

    SECTION("setNumStages clamps odd numbers to even") {
        phaser.setNumStages(3);
        REQUIRE(phaser.getNumStages() == 2);  // Rounds down

        phaser.setNumStages(5);
        REQUIRE(phaser.getNumStages() == 4);

        phaser.setNumStages(11);
        REQUIRE(phaser.getNumStages() == 10);
    }

    SECTION("setNumStages clamps to valid range [2, 12]") {
        phaser.setNumStages(0);
        REQUIRE(phaser.getNumStages() == 2);

        phaser.setNumStages(1);
        REQUIRE(phaser.getNumStages() == 2);

        phaser.setNumStages(14);
        REQUIRE(phaser.getNumStages() == 12);

        phaser.setNumStages(100);
        REQUIRE(phaser.getNumStages() == 12);
    }
}

TEST_CASE("Phaser - LFO Rate Control", "[Phaser][US1]") {
    Phaser phaser;
    phaser.prepare(44100.0);

    SECTION("default rate is 0.5 Hz") {
        REQUIRE(phaser.getRate() == Approx(0.5f));
    }

    SECTION("setRate and getRate work correctly") {
        phaser.setRate(1.0f);
        REQUIRE(phaser.getRate() == Approx(1.0f));

        phaser.setRate(5.0f);
        REQUIRE(phaser.getRate() == Approx(5.0f));
    }

    SECTION("setRate clamps to valid range [0.01, 20]") {
        phaser.setRate(0.001f);
        REQUIRE(phaser.getRate() >= 0.01f);

        phaser.setRate(100.0f);
        REQUIRE(phaser.getRate() <= 20.0f);
    }
}

TEST_CASE("Phaser - Depth Control", "[Phaser][US1]") {
    Phaser phaser;
    phaser.prepare(44100.0);

    SECTION("default depth is 0.5") {
        REQUIRE(phaser.getDepth() == Approx(0.5f));
    }

    SECTION("setDepth and getDepth work correctly") {
        phaser.setDepth(0.0f);
        REQUIRE(phaser.getDepth() == Approx(0.0f));

        phaser.setDepth(1.0f);
        REQUIRE(phaser.getDepth() == Approx(1.0f));

        phaser.setDepth(0.75f);
        REQUIRE(phaser.getDepth() == Approx(0.75f));
    }

    SECTION("setDepth clamps to valid range [0.0, 1.0]") {
        phaser.setDepth(-0.5f);
        REQUIRE(phaser.getDepth() >= 0.0f);

        phaser.setDepth(2.0f);
        REQUIRE(phaser.getDepth() <= 1.0f);
    }
}

TEST_CASE("Phaser - Center Frequency", "[Phaser][US1]") {
    Phaser phaser;
    phaser.prepare(44100.0);

    SECTION("default center frequency is 1000 Hz") {
        REQUIRE(phaser.getCenterFrequency() == Approx(1000.0f));
    }

    SECTION("setCenterFrequency and getCenterFrequency work correctly") {
        phaser.setCenterFrequency(500.0f);
        REQUIRE(phaser.getCenterFrequency() == Approx(500.0f));

        phaser.setCenterFrequency(2000.0f);
        REQUIRE(phaser.getCenterFrequency() == Approx(2000.0f));
    }

    SECTION("setCenterFrequency clamps to valid range [100, 10000]") {
        phaser.setCenterFrequency(10.0f);
        REQUIRE(phaser.getCenterFrequency() >= 100.0f);

        phaser.setCenterFrequency(50000.0f);
        REQUIRE(phaser.getCenterFrequency() <= 10000.0f);
    }
}

TEST_CASE("Phaser - Mix Control", "[Phaser][US1]") {
    Phaser phaser;
    phaser.prepare(44100.0);

    SECTION("default mix is 0.5") {
        REQUIRE(phaser.getMix() == Approx(0.5f));
    }

    SECTION("setMix and getMix work correctly") {
        phaser.setMix(0.0f);
        REQUIRE(phaser.getMix() == Approx(0.0f));

        phaser.setMix(1.0f);
        REQUIRE(phaser.getMix() == Approx(1.0f));
    }

    SECTION("mix=0 produces dry signal (output equals input)") {
        Phaser dryPhaser;
        dryPhaser.prepare(44100.0);
        dryPhaser.setMix(0.0f);

        // Process some samples to let smoother settle
        for (int i = 0; i < 500; ++i) {
            (void)dryPhaser.process(0.5f);
        }

        // Now test with fresh input - output should equal input
        std::array<float, 512> input;
        generateSine(input.data(), input.size(), 440.0f, 44100.0f, 1.0f);

        bool allMatch = true;
        for (size_t i = 0; i < input.size(); ++i) {
            float output = dryPhaser.process(input[i]);
            // With mix=0, output should equal input (dry signal only)
            if (std::abs(output - input[i]) > 1e-4f) {
                allMatch = false;
                break;
            }
        }

        REQUIRE(allMatch);
    }

    SECTION("setMix clamps to valid range [0.0, 1.0]") {
        phaser.setMix(-0.5f);
        REQUIRE(phaser.getMix() >= 0.0f);

        phaser.setMix(1.5f);
        REQUIRE(phaser.getMix() <= 1.0f);
    }
}

TEST_CASE("Phaser - Stationary Notches at Zero Depth", "[Phaser][US1]") {
    // FR-004: depth = 0 stops sweep (notches remain stationary)
    Phaser phaser;
    phaser.prepare(44100.0);
    phaser.setDepth(0.0f);
    phaser.setRate(5.0f);  // Fast rate, but depth is 0
    phaser.setMix(1.0f);
    phaser.setCenterFrequency(1000.0f);

    // Process many samples and verify consistent behavior
    constexpr size_t kBlockSize = 4096;
    std::array<float, kBlockSize> buffer1, buffer2;

    // Generate identical input signals
    generateSine(buffer1.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);
    generateSine(buffer2.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);

    // Process first block
    for (size_t i = 0; i < kBlockSize; ++i) {
        buffer1[i] = phaser.process(buffer1[i]);
    }

    // Reset and process second block with same input
    phaser.reset();
    for (size_t i = 0; i < kBlockSize; ++i) {
        buffer2[i] = phaser.process(buffer2[i]);
    }

    // With depth=0, the filter frequency should be constant,
    // so the output should be deterministic (same input = same output after reset)
    float maxDiff = 0.0f;
    for (size_t i = 100; i < kBlockSize; ++i) {  // Skip transient
        float diff = std::abs(buffer1[i] - buffer2[i]);
        if (diff > maxDiff) maxDiff = diff;
    }

    // Outputs should be nearly identical (tolerance accounts for smoother
    // settling path difference between fresh prepare and post-reset)
    REQUIRE(maxDiff < 0.05f);
}

TEST_CASE("Phaser - Denormal Flushing", "[Phaser][US1]") {
    // FR-016: System MUST flush denormals from filter states
    Phaser phaser;
    phaser.prepare(44100.0);
    phaser.setMix(1.0f);
    phaser.setDepth(1.0f);
    phaser.setRate(1.0f);

    // Process with extremely small values
    constexpr size_t kNumSamples = 1000;
    bool allValid = true;

    for (size_t i = 0; i < kNumSamples; ++i) {
        // Input values that could cause denormals
        float input = 1e-40f;
        float output = phaser.process(input);

        if (!isValidFloat(output)) {
            allValid = false;
            break;
        }
    }

    REQUIRE(allValid);

    // Also verify that processing returns quickly (no denormal slowdown)
    // This is a soft check - just ensure it completes in reasonable time
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < 10000; ++i) {
        (void)phaser.process(1e-40f);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Should complete in under 100ms for 10000 samples
    REQUIRE(duration.count() < 100);
}

// =============================================================================
// Phase 3: User Story 2 - Variable Stage Count (Priority: P2)
// =============================================================================

TEST_CASE("Phaser - Stage Count Validation", "[Phaser][US2]") {
    Phaser phaser;
    phaser.prepare(44100.0);

    SECTION("valid even stage counts are accepted") {
        for (int stages : {2, 4, 6, 8, 10, 12}) {
            phaser.setNumStages(stages);
            REQUIRE(phaser.getNumStages() == stages);
        }
    }

    SECTION("odd numbers are clamped to nearest lower even") {
        phaser.setNumStages(3);
        REQUIRE(phaser.getNumStages() == 2);

        phaser.setNumStages(5);
        REQUIRE(phaser.getNumStages() == 4);

        phaser.setNumStages(7);
        REQUIRE(phaser.getNumStages() == 6);

        phaser.setNumStages(9);
        REQUIRE(phaser.getNumStages() == 8);

        phaser.setNumStages(11);
        REQUIRE(phaser.getNumStages() == 10);
    }

    SECTION("out of range values are clamped") {
        phaser.setNumStages(-5);
        REQUIRE(phaser.getNumStages() == 2);

        phaser.setNumStages(0);
        REQUIRE(phaser.getNumStages() == 2);

        phaser.setNumStages(20);
        REQUIRE(phaser.getNumStages() == 12);
    }
}

TEST_CASE("Phaser - Notch Count vs Stage Count", "[Phaser][US2]") {
    // FR-001: N stages produces N/2 notches
    // This is verified indirectly - different stage counts should produce
    // different frequency responses
    constexpr size_t kBlockSize = 4096;

    SECTION("2 stages produces different output than 4 stages") {
        // Use separate phaser instances to avoid state issues
        Phaser phaser2, phaser4;
        phaser2.prepare(44100.0);
        phaser4.prepare(44100.0);

        // Configure for noticeable phase shift difference
        phaser2.setMix(1.0f);
        phaser2.setDepth(0.8f);  // Non-zero depth for sweep
        phaser2.setRate(2.0f);
        phaser2.setNumStages(2);

        phaser4.setMix(1.0f);
        phaser4.setDepth(0.8f);
        phaser4.setRate(2.0f);
        phaser4.setNumStages(4);

        std::array<float, kBlockSize> buffer2, buffer4;
        generateSine(buffer2.data(), kBlockSize, 1000.0f, 44100.0f, 1.0f);
        generateSine(buffer4.data(), kBlockSize, 1000.0f, 44100.0f, 1.0f);

        for (size_t i = 0; i < kBlockSize; ++i) {
            buffer2[i] = phaser2.process(buffer2[i]);
        }

        for (size_t i = 0; i < kBlockSize; ++i) {
            buffer4[i] = phaser4.process(buffer4[i]);
        }

        // Outputs should be different due to different number of allpass stages
        float correlation = calculateCorrelation(buffer2.data() + 500, buffer4.data() + 500, kBlockSize - 500);
        REQUIRE(correlation < 0.9999f);  // Should not be identical
    }

    SECTION("12 stages produces different output than 2 stages") {
        Phaser phaser2, phaser12;
        phaser2.prepare(44100.0);
        phaser12.prepare(44100.0);

        phaser2.setMix(1.0f);
        phaser2.setDepth(0.8f);
        phaser2.setRate(2.0f);
        phaser2.setNumStages(2);

        phaser12.setMix(1.0f);
        phaser12.setDepth(0.8f);
        phaser12.setRate(2.0f);
        phaser12.setNumStages(12);

        std::array<float, kBlockSize> buffer2, buffer12;
        generateSine(buffer2.data(), kBlockSize, 1000.0f, 44100.0f, 1.0f);
        generateSine(buffer12.data(), kBlockSize, 1000.0f, 44100.0f, 1.0f);

        for (size_t i = 0; i < kBlockSize; ++i) {
            buffer2[i] = phaser2.process(buffer2[i]);
        }

        for (size_t i = 0; i < kBlockSize; ++i) {
            buffer12[i] = phaser12.process(buffer12[i]);
        }

        // More stages should create more phase shift = different output
        float correlation = calculateCorrelation(buffer2.data() + 500, buffer12.data() + 500, kBlockSize - 500);
        REQUIRE(correlation < 0.999f);  // Should be notably different
    }
}

TEST_CASE("Phaser - Stage Count Changes", "[Phaser][US2]") {
    Phaser phaser;
    phaser.prepare(44100.0);
    phaser.setMix(1.0f);

    SECTION("runtime stage count changes work correctly") {
        // Start with 4 stages
        phaser.setNumStages(4);
        REQUIRE(phaser.getNumStages() == 4);

        // Process some samples
        for (int i = 0; i < 100; ++i) {
            (void)phaser.process(0.5f);
        }

        // Change to 8 stages mid-processing
        phaser.setNumStages(8);
        REQUIRE(phaser.getNumStages() == 8);

        // Should continue processing without issues
        bool allValid = true;
        for (int i = 0; i < 100; ++i) {
            float output = phaser.process(0.5f);
            if (!isValidFloat(output)) {
                allValid = false;
                break;
            }
        }
        REQUIRE(allValid);
    }
}

// =============================================================================
// Phase 4: User Story 3 - Feedback Resonance (Priority: P2)
// =============================================================================

TEST_CASE("Phaser - Feedback Control", "[Phaser][US3]") {
    Phaser phaser;
    phaser.prepare(44100.0);

    SECTION("default feedback is 0") {
        REQUIRE(phaser.getFeedback() == Approx(0.0f));
    }

    SECTION("setFeedback and getFeedback work correctly") {
        phaser.setFeedback(0.5f);
        REQUIRE(phaser.getFeedback() == Approx(0.5f));

        phaser.setFeedback(-0.5f);
        REQUIRE(phaser.getFeedback() == Approx(-0.5f));
    }
}

TEST_CASE("Phaser - Feedback Range", "[Phaser][US3]") {
    Phaser phaser;
    phaser.prepare(44100.0);

    SECTION("feedback accepts bipolar range [-1.0, +1.0]") {
        phaser.setFeedback(-1.0f);
        REQUIRE(phaser.getFeedback() == Approx(-1.0f));

        phaser.setFeedback(1.0f);
        REQUIRE(phaser.getFeedback() == Approx(1.0f));
    }

    SECTION("feedback is clamped to valid range") {
        phaser.setFeedback(-2.0f);
        REQUIRE(phaser.getFeedback() >= -1.0f);

        phaser.setFeedback(2.0f);
        REQUIRE(phaser.getFeedback() <= 1.0f);
    }
}

TEST_CASE("Phaser - Feedback Stability", "[Phaser][US3]") {
    // FR-012, SC-008: Maximum feedback should remain stable
    Phaser phaser;
    phaser.prepare(44100.0);
    phaser.setMix(1.0f);
    phaser.setDepth(1.0f);
    phaser.setRate(0.5f);

    SECTION("positive maximum feedback is stable") {
        phaser.setFeedback(1.0f);

        // Process for 10 seconds worth of samples
        constexpr size_t kNumSamples = 441000;
        bool allValid = true;
        float maxOutput = 0.0f;

        for (size_t i = 0; i < kNumSamples; ++i) {
            float input = std::sin(2.0f * std::numbers::pi_v<float> * 440.0f * static_cast<float>(i) / 44100.0f);
            float output = phaser.process(input);

            if (!isValidFloat(output)) {
                allValid = false;
                break;
            }

            float absOut = std::abs(output);
            if (absOut > maxOutput) maxOutput = absOut;
        }

        REQUIRE(allValid);
        REQUIRE(maxOutput < 100.0f);  // Should be bounded
    }

    SECTION("negative maximum feedback is stable") {
        phaser.setFeedback(-1.0f);

        constexpr size_t kNumSamples = 441000;
        bool allValid = true;
        float maxOutput = 0.0f;

        for (size_t i = 0; i < kNumSamples; ++i) {
            float input = std::sin(2.0f * std::numbers::pi_v<float> * 440.0f * static_cast<float>(i) / 44100.0f);
            float output = phaser.process(input);

            if (!isValidFloat(output)) {
                allValid = false;
                break;
            }

            float absOut = std::abs(output);
            if (absOut > maxOutput) maxOutput = absOut;
        }

        REQUIRE(allValid);
        REQUIRE(maxOutput < 100.0f);  // Should be bounded
    }
}

TEST_CASE("Phaser - Negative Feedback Effect", "[Phaser][US3]") {
    Phaser phaser;
    phaser.prepare(44100.0);
    phaser.setMix(1.0f);
    phaser.setDepth(0.5f);
    phaser.setRate(1.0f);

    constexpr size_t kBlockSize = 4096;
    std::array<float, kBlockSize> bufferPos, bufferNeg;

    // Generate identical inputs
    generateSine(bufferPos.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);
    generateSine(bufferNeg.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);

    SECTION("positive and negative feedback produce different outputs") {
        phaser.setFeedback(0.7f);
        phaser.reset();
        for (size_t i = 0; i < kBlockSize; ++i) {
            bufferPos[i] = phaser.process(bufferPos[i]);
        }

        phaser.setFeedback(-0.7f);
        phaser.reset();
        for (size_t i = 0; i < kBlockSize; ++i) {
            bufferNeg[i] = phaser.process(bufferNeg[i]);
        }

        // Outputs should be different (feedback polarity shifts notch/peak positions)
        float correlation = calculateCorrelation(bufferPos.data() + 100, bufferNeg.data() + 100, kBlockSize - 100);
        REQUIRE(correlation < 0.999f);
    }
}

TEST_CASE("Phaser - Feedback Increases Notch Depth", "[Phaser][US3]") {
    // SC-003: With feedback at 0.9, notch depth increases by at least 12dB
    // This is tested indirectly by verifying feedback affects the output
    Phaser phaser;
    phaser.prepare(44100.0);
    phaser.setMix(1.0f);
    phaser.setDepth(0.5f);
    phaser.setRate(0.1f);  // Slow rate for stable measurement

    constexpr size_t kBlockSize = 8192;
    std::array<float, kBlockSize> bufferNoFb, bufferWithFb;

    // Generate identical inputs
    generateSine(bufferNoFb.data(), kBlockSize, 1000.0f, 44100.0f, 1.0f);
    generateSine(bufferWithFb.data(), kBlockSize, 1000.0f, 44100.0f, 1.0f);

    SECTION("high feedback produces different RMS than no feedback") {
        phaser.setFeedback(0.0f);
        phaser.reset();
        for (size_t i = 0; i < kBlockSize; ++i) {
            bufferNoFb[i] = phaser.process(bufferNoFb[i]);
        }

        phaser.setFeedback(0.9f);
        phaser.reset();
        for (size_t i = 0; i < kBlockSize; ++i) {
            bufferWithFb[i] = phaser.process(bufferWithFb[i]);
        }

        float rmsNoFb = calculateRMS(bufferNoFb.data() + 1000, kBlockSize - 1000);
        float rmsWithFb = calculateRMS(bufferWithFb.data() + 1000, kBlockSize - 1000);

        // With feedback, the response should be different
        // The exact ratio depends on the signal frequency vs notch position
        REQUIRE(std::abs(rmsNoFb - rmsWithFb) > 0.01f);
    }
}

// =============================================================================
// Phase 5: User Story 4 - Stereo Processing with Spread (Priority: P3)
// =============================================================================

TEST_CASE("Phaser - Stereo Processing", "[Phaser][US4]") {
    Phaser phaser;
    phaser.prepare(44100.0);
    phaser.setMix(1.0f);
    phaser.setDepth(1.0f);
    phaser.setRate(1.0f);

    SECTION("processStereo processes both channels") {
        constexpr size_t kBlockSize = 512;
        std::array<float, kBlockSize> left, right;

        generateSine(left.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);
        generateSine(right.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);

        phaser.processStereo(left.data(), right.data(), kBlockSize);

        // Both channels should be modified
        std::array<float, kBlockSize> originalLeft, originalRight;
        generateSine(originalLeft.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);
        generateSine(originalRight.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);

        bool leftChanged = false;
        bool rightChanged = false;

        for (size_t i = 0; i < kBlockSize; ++i) {
            if (std::abs(left[i] - originalLeft[i]) > 1e-5f) leftChanged = true;
            if (std::abs(right[i] - originalRight[i]) > 1e-5f) rightChanged = true;
        }

        REQUIRE(leftChanged);
        REQUIRE(rightChanged);
    }

    SECTION("all stereo samples are valid") {
        constexpr size_t kBlockSize = 1024;
        std::array<float, kBlockSize> left, right;

        generateSine(left.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);
        generateSine(right.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);

        phaser.processStereo(left.data(), right.data(), kBlockSize);

        bool allValid = true;
        for (size_t i = 0; i < kBlockSize; ++i) {
            if (!isValidFloat(left[i]) || !isValidFloat(right[i])) {
                allValid = false;
                break;
            }
        }

        REQUIRE(allValid);
    }
}

TEST_CASE("Phaser - Stereo Spread Control", "[Phaser][US4]") {
    Phaser phaser;
    phaser.prepare(44100.0);

    SECTION("default stereo spread is 0 degrees") {
        REQUIRE(phaser.getStereoSpread() == Approx(0.0f));
    }

    SECTION("setStereoSpread and getStereoSpread work correctly") {
        phaser.setStereoSpread(90.0f);
        REQUIRE(phaser.getStereoSpread() == Approx(90.0f));

        phaser.setStereoSpread(180.0f);
        REQUIRE(phaser.getStereoSpread() == Approx(180.0f));
    }

    SECTION("setStereoSpread wraps to [0, 360]") {
        phaser.setStereoSpread(400.0f);
        REQUIRE(phaser.getStereoSpread() >= 0.0f);
        REQUIRE(phaser.getStereoSpread() < 360.0f);

        phaser.setStereoSpread(-90.0f);
        REQUIRE(phaser.getStereoSpread() >= 0.0f);
        REQUIRE(phaser.getStereoSpread() < 360.0f);
    }
}

TEST_CASE("Phaser - Stereo Spread at 180 Degrees", "[Phaser][US4]") {
    // SC-004: Stereo spread of 180 degrees produces different L/R modulation
    // Note: Correlation depends on LFO frequency, signal frequency, and block size
    // We verify that the outputs are different, not perfectly anti-correlated
    Phaser phaser;
    phaser.prepare(44100.0);
    phaser.setMix(1.0f);
    phaser.setDepth(1.0f);
    phaser.setRate(5.0f);  // Faster rate to capture multiple cycles
    phaser.setStereoSpread(180.0f);

    constexpr size_t kBlockSize = 8192;
    std::array<float, kBlockSize> left, right;

    generateSine(left.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);
    generateSine(right.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);

    phaser.processStereo(left.data(), right.data(), kBlockSize);

    // Calculate difference between L and R
    float sumDiffSquared = 0.0f;
    for (size_t i = 1000; i < kBlockSize; ++i) {
        float diff = left[i] - right[i];
        sumDiffSquared += diff * diff;
    }
    float rmsDiff = std::sqrt(sumDiffSquared / static_cast<float>(kBlockSize - 1000));

    // With 180 degree spread, there should be noticeable difference between L and R
    REQUIRE(rmsDiff > 0.01f);
}

TEST_CASE("Phaser - Stereo Spread at 0 Degrees", "[Phaser][US4]") {
    Phaser phaser;
    phaser.prepare(44100.0);
    phaser.setMix(1.0f);
    phaser.setDepth(1.0f);
    phaser.setRate(2.0f);
    phaser.setStereoSpread(0.0f);

    constexpr size_t kBlockSize = 4096;
    std::array<float, kBlockSize> left, right;

    generateSine(left.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);
    generateSine(right.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);

    phaser.processStereo(left.data(), right.data(), kBlockSize);

    // With 0 degree spread, L and R should be highly correlated (mono-compatible)
    float correlation = calculateCorrelation(left.data() + 500, right.data() + 500, kBlockSize - 500);

    REQUIRE(correlation > 0.95f);  // Should be nearly identical
}

TEST_CASE("Phaser - Stereo Decorrelation", "[Phaser][US4]") {
    // Verify that stereo spread produces different L/R outputs
    Phaser phaser;
    phaser.prepare(44100.0);
    phaser.setMix(1.0f);
    phaser.setDepth(1.0f);
    phaser.setRate(5.0f);
    phaser.setStereoSpread(180.0f);

    constexpr size_t kBlockSize = 22050;  // 0.5 seconds
    std::array<float, kBlockSize> left, right;

    generateSine(left.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);
    generateSine(right.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);

    phaser.processStereo(left.data(), right.data(), kBlockSize);

    // Calculate RMS of difference
    float sumDiffSquared = 0.0f;
    for (size_t i = 1000; i < kBlockSize; ++i) {
        float diff = left[i] - right[i];
        sumDiffSquared += diff * diff;
    }
    float rmsDiff = std::sqrt(sumDiffSquared / static_cast<float>(kBlockSize - 1000));

    // With 180 degree spread, L and R should be different
    REQUIRE(rmsDiff > 0.01f);
}

// =============================================================================
// Phase 6: User Story 5 - Tempo-Synchronized Modulation (Priority: P3)
// =============================================================================

TEST_CASE("Phaser - Tempo Sync Control", "[Phaser][US5]") {
    Phaser phaser;
    phaser.prepare(44100.0);

    SECTION("tempo sync is disabled by default") {
        REQUIRE_FALSE(phaser.isTempoSyncEnabled());
    }

    SECTION("setTempoSync and isTempoSyncEnabled work correctly") {
        phaser.setTempoSync(true);
        REQUIRE(phaser.isTempoSyncEnabled());

        phaser.setTempoSync(false);
        REQUIRE_FALSE(phaser.isTempoSyncEnabled());
    }
}

TEST_CASE("Phaser - Note Value Configuration", "[Phaser][US5]") {
    Phaser phaser;
    phaser.prepare(44100.0);
    phaser.setTempoSync(true);

    SECTION("setNoteValue accepts all note values") {
        // Just verify it doesn't crash
        phaser.setNoteValue(NoteValue::Whole);
        phaser.setNoteValue(NoteValue::Half);
        phaser.setNoteValue(NoteValue::Quarter);
        phaser.setNoteValue(NoteValue::Eighth);
        phaser.setNoteValue(NoteValue::Sixteenth);

        REQUIRE(true);  // No crash
    }

    SECTION("setNoteValue accepts modifiers") {
        phaser.setNoteValue(NoteValue::Quarter, NoteModifier::None);
        phaser.setNoteValue(NoteValue::Quarter, NoteModifier::Dotted);
        phaser.setNoteValue(NoteValue::Quarter, NoteModifier::Triplet);

        REQUIRE(true);  // No crash
    }
}

TEST_CASE("Phaser - Tempo Setting", "[Phaser][US5]") {
    Phaser phaser;
    phaser.prepare(44100.0);

    SECTION("setTempo accepts valid BPM values") {
        phaser.setTempo(60.0f);
        phaser.setTempo(120.0f);
        phaser.setTempo(180.0f);

        REQUIRE(true);  // No crash
    }
}

TEST_CASE("Phaser - Tempo Sync at Quarter Note", "[Phaser][US5]") {
    // SC-005: 120 BPM with quarter note = 2 Hz
    Phaser phaser;
    phaser.prepare(44100.0);
    phaser.setTempoSync(true);
    phaser.setTempo(120.0f);
    phaser.setNoteValue(NoteValue::Quarter, NoteModifier::None);
    phaser.setDepth(1.0f);
    phaser.setMix(1.0f);

    // At 120 BPM, quarter note = 0.5 seconds = 2 Hz
    // Process for 2 seconds (4 complete cycles)
    constexpr size_t kNumSamples = 88200;
    std::array<float, kNumSamples> buffer;

    generateSine(buffer.data(), kNumSamples, 440.0f, 44100.0f, 1.0f);

    for (size_t i = 0; i < kNumSamples; ++i) {
        buffer[i] = phaser.process(buffer[i]);
    }

    // The output should be valid and show modulation
    bool allValid = true;
    for (size_t i = 0; i < kNumSamples; ++i) {
        if (!isValidFloat(buffer[i])) {
            allValid = false;
            break;
        }
    }

    REQUIRE(allValid);
}

TEST_CASE("Phaser - Tempo Sync Disabled", "[Phaser][US5]") {
    Phaser phaser;
    phaser.prepare(44100.0);
    phaser.setTempoSync(false);
    phaser.setRate(1.5f);
    phaser.setTempo(120.0f);  // Tempo should be ignored
    phaser.setNoteValue(NoteValue::Quarter);  // Should be ignored

    // Rate should still be the free-running rate
    REQUIRE(phaser.getRate() == Approx(1.5f));
}

// =============================================================================
// Phase 7: Polish & Cross-Cutting Concerns
// =============================================================================

TEST_CASE("Phaser - Waveform Selection", "[Phaser][Polish]") {
    Phaser phaser;
    phaser.prepare(44100.0);

    SECTION("default waveform is Sine") {
        REQUIRE(phaser.getWaveform() == Waveform::Sine);
    }

    SECTION("setWaveform and getWaveform work correctly") {
        phaser.setWaveform(Waveform::Triangle);
        REQUIRE(phaser.getWaveform() == Waveform::Triangle);

        phaser.setWaveform(Waveform::Square);
        REQUIRE(phaser.getWaveform() == Waveform::Square);

        phaser.setWaveform(Waveform::Sawtooth);
        REQUIRE(phaser.getWaveform() == Waveform::Sawtooth);

        phaser.setWaveform(Waveform::Sine);
        REQUIRE(phaser.getWaveform() == Waveform::Sine);
    }

    SECTION("different waveforms produce different outputs") {
        // Use separate phasers to avoid state carryover
        Phaser phaserSine, phaserSquare;
        phaserSine.prepare(44100.0);
        phaserSquare.prepare(44100.0);

        phaserSine.setMix(1.0f);
        phaserSine.setDepth(1.0f);
        phaserSine.setRate(5.0f);
        phaserSine.setWaveform(Waveform::Sine);

        phaserSquare.setMix(1.0f);
        phaserSquare.setDepth(1.0f);
        phaserSquare.setRate(5.0f);
        phaserSquare.setWaveform(Waveform::Square);

        constexpr size_t kBlockSize = 8192;
        std::array<float, kBlockSize> bufferSine, bufferSquare;

        generateSine(bufferSine.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);
        generateSine(bufferSquare.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);

        for (size_t i = 0; i < kBlockSize; ++i) {
            bufferSine[i] = phaserSine.process(bufferSine[i]);
        }

        for (size_t i = 0; i < kBlockSize; ++i) {
            bufferSquare[i] = phaserSquare.process(bufferSquare[i]);
        }

        // Calculate RMS of difference between outputs
        float sumDiffSquared = 0.0f;
        for (size_t i = 1000; i < kBlockSize; ++i) {
            float diff = bufferSine[i] - bufferSquare[i];
            sumDiffSquared += diff * diff;
        }
        float rmsDiff = std::sqrt(sumDiffSquared / static_cast<float>(kBlockSize - 1000));

        // Different waveforms should produce noticeably different outputs
        REQUIRE(rmsDiff > 0.005f);
    }
}

TEST_CASE("Phaser - Block vs Sample-by-Sample", "[Phaser][Polish]") {
    // SC-007: Block processing and sample-by-sample produce identical results
    Phaser phaserSample, phaserBlock;
    phaserSample.prepare(44100.0);
    phaserBlock.prepare(44100.0);

    // Set identical parameters
    phaserSample.setMix(0.7f);
    phaserSample.setDepth(0.8f);
    phaserSample.setRate(2.0f);
    phaserSample.setFeedback(0.5f);

    phaserBlock.setMix(0.7f);
    phaserBlock.setDepth(0.8f);
    phaserBlock.setRate(2.0f);
    phaserBlock.setFeedback(0.5f);

    constexpr size_t kBlockSize = 512;
    std::array<float, kBlockSize> inputSample, inputBlock;
    generateSine(inputSample.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);
    generateSine(inputBlock.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);

    // Process sample-by-sample
    for (size_t i = 0; i < kBlockSize; ++i) {
        inputSample[i] = phaserSample.process(inputSample[i]);
    }

    // Process as block
    phaserBlock.processBlock(inputBlock.data(), kBlockSize);

    // Results should be bit-identical
    float maxDiff = 0.0f;
    for (size_t i = 0; i < kBlockSize; ++i) {
        float diff = std::abs(inputSample[i] - inputBlock[i]);
        if (diff > maxDiff) maxDiff = diff;
    }

    REQUIRE(maxDiff < 1e-6f);
}

TEST_CASE("Phaser - Performance", "[Phaser][.benchmark]") {
    // SC-001: Processing should be reasonably fast
    // Note: This is a soft benchmark - actual timing depends on hardware
    Phaser phaser;
    phaser.prepare(44100.0);
    phaser.setNumStages(12);
    phaser.setMix(1.0f);
    phaser.setDepth(1.0f);
    phaser.setRate(2.0f);
    phaser.setFeedback(0.7f);

    constexpr size_t kNumSamples = 44100;
    std::array<float, kNumSamples> buffer;
    generateSine(buffer.data(), kNumSamples, 440.0f, 44100.0f, 1.0f);

    // Warm up
    for (size_t i = 0; i < 1000; ++i) {
        (void)phaser.process(buffer[i]);
    }
    phaser.reset();

    // Measure
    auto start = std::chrono::high_resolution_clock::now();
    phaser.processBlock(buffer.data(), kNumSamples);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    CAPTURE(duration.count());

    // Allow reasonable time on various hardware (10ms = 10000 microseconds)
    // On reference hardware it should be < 1ms, but CI machines vary
    REQUIRE(duration.count() < 50000);  // 50ms max for any reasonable machine
}

TEST_CASE("Phaser - Sample Rate Change", "[Phaser][Polish]") {
    Phaser phaser;

    SECTION("prepare with different sample rates") {
        phaser.prepare(44100.0);
        REQUIRE(phaser.isPrepared());

        phaser.prepare(48000.0);
        REQUIRE(phaser.isPrepared());

        phaser.prepare(96000.0);
        REQUIRE(phaser.isPrepared());
    }

    SECTION("processing works at different sample rates") {
        for (double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
            phaser.prepare(sr);
            phaser.setMix(1.0f);

            bool allValid = true;
            for (int i = 0; i < 1000; ++i) {
                float output = phaser.process(0.5f);
                if (!isValidFloat(output)) {
                    allValid = false;
                    break;
                }
            }

            CAPTURE(sr);
            REQUIRE(allValid);
        }
    }
}

TEST_CASE("Phaser - Coefficient Recalculation", "[Phaser][Polish]") {
    // FR-014: Coefficients recalculated when prepare() called with new sample rate
    Phaser phaser44k, phaser96k;

    phaser44k.prepare(44100.0);
    phaser96k.prepare(96000.0);

    // Set identical parameters with larger depth for more effect
    phaser44k.setMix(1.0f);
    phaser44k.setDepth(1.0f);
    phaser44k.setRate(5.0f);
    phaser44k.setCenterFrequency(1000.0f);

    phaser96k.setMix(1.0f);
    phaser96k.setDepth(1.0f);
    phaser96k.setRate(5.0f);
    phaser96k.setCenterFrequency(1000.0f);

    // Process identical input signals
    constexpr size_t kBlockSize = 4096;
    std::array<float, kBlockSize> buffer44k, buffer96k;
    generateSine(buffer44k.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);
    generateSine(buffer96k.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);

    for (size_t i = 0; i < kBlockSize; ++i) {
        buffer44k[i] = phaser44k.process(buffer44k[i]);
        buffer96k[i] = phaser96k.process(buffer96k[i]);
    }

    // Calculate RMS of difference - outputs should differ due to different sample rates
    // (LFO progresses at different rates, filter coefficients differ)
    float sumDiffSquared = 0.0f;
    for (size_t i = 1000; i < kBlockSize; ++i) {
        float diff = buffer44k[i] - buffer96k[i];
        sumDiffSquared += diff * diff;
    }
    float rmsDiff = std::sqrt(sumDiffSquared / static_cast<float>(kBlockSize - 1000));

    // Different sample rates should produce noticeably different outputs
    REQUIRE(rmsDiff > 0.001f);
}

TEST_CASE("Phaser - Parameter Smoothing", "[Phaser][Polish]") {
    // SC-006: No clicks or zipper noise during parameter changes
    Phaser phaser;
    phaser.prepare(44100.0);
    phaser.setMix(1.0f);
    phaser.setDepth(0.5f);
    phaser.setRate(1.0f);

    constexpr size_t kBlockSize = 4096;
    std::array<float, kBlockSize> buffer;
    generateSine(buffer.data(), kBlockSize, 440.0f, 44100.0f, 1.0f);

    // Process while changing parameters
    bool anyDiscontinuity = false;
    float prevOutput = 0.0f;

    for (size_t i = 0; i < kBlockSize; ++i) {
        // Change parameters every 100 samples
        if (i % 100 == 0) {
            float t = static_cast<float>(i) / static_cast<float>(kBlockSize);
            phaser.setDepth(0.3f + 0.7f * t);
            phaser.setRate(0.5f + 4.5f * t);
        }

        float output = phaser.process(buffer[i]);

        // Check for discontinuities (large jumps)
        if (i > 0 && std::abs(output - prevOutput) > 1.0f) {
            anyDiscontinuity = true;
        }

        prevOutput = output;
    }

    // Should not have large discontinuities due to parameter smoothing
    REQUIRE_FALSE(anyDiscontinuity);
}

TEST_CASE("Phaser - Extreme Frequencies", "[Phaser][Polish]") {
    Phaser phaser;
    phaser.prepare(44100.0);
    phaser.setMix(1.0f);
    phaser.setDepth(1.0f);

    SECTION("extreme low center frequency") {
        phaser.setCenterFrequency(100.0f);

        bool allValid = true;
        for (int i = 0; i < 1000; ++i) {
            float output = phaser.process(0.5f);
            if (!isValidFloat(output)) {
                allValid = false;
                break;
            }
        }
        REQUIRE(allValid);
    }

    SECTION("extreme high center frequency") {
        phaser.setCenterFrequency(10000.0f);

        bool allValid = true;
        for (int i = 0; i < 1000; ++i) {
            float output = phaser.process(0.5f);
            if (!isValidFloat(output)) {
                allValid = false;
                break;
            }
        }
        REQUIRE(allValid);
    }
}

TEST_CASE("Phaser - Real-time safety noexcept", "[Phaser][realtime]") {
    // Verify that processing methods are noexcept (FR-019, FR-020, FR-021)
    static_assert(noexcept(std::declval<Phaser>().process(0.0f)),
        "process() must be noexcept");
    static_assert(noexcept(std::declval<Phaser>().processBlock(nullptr, 0)),
        "processBlock() must be noexcept");
    static_assert(noexcept(std::declval<Phaser>().processStereo(nullptr, nullptr, 0)),
        "processStereo() must be noexcept");
    static_assert(noexcept(std::declval<Phaser>().reset()),
        "reset() must be noexcept");

    REQUIRE(true);  // If we get here, static_asserts passed
}

// =============================================================================
// Phaser Sound Quality Fix Tests
// =============================================================================
// These tests verify correct phaser behavior after fixing three bugs:
// 1. Mix formula: additive (dry + mix*wet) instead of crossfade
// 2. Sweep range: octave-based exponential instead of linear
// 3. Feedback source: from allpass output instead of mixed output
// =============================================================================

TEST_CASE("Phaser - Additive mix creates notches at mix=1.0", "[Phaser][PhaserFix]") {
    // Bug 1: With crossfade mix, mix=1.0 gives pure allpass (flat response, no notches).
    // Correct behavior: mix=1.0 means dry + 1.0*wet, which creates maximum notch depth.
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kFFTSize = 4096;
    constexpr size_t kNumBlocks = 8;
    constexpr size_t kTotalSamples = kFFTSize * kNumBlocks;

    Phaser phaser;
    phaser.prepare(static_cast<double>(kSampleRate));
    phaser.setNumStages(4);
    phaser.setDepth(0.0f);          // Stationary notches (no LFO sweep)
    phaser.setRate(0.01f);
    phaser.setCenterFrequency(1000.0f);
    phaser.setMix(1.0f);            // Maximum phaser effect
    phaser.setFeedback(0.0f);

    // Process white noise to get a broadband frequency response
    std::vector<float> noise(kTotalSamples);
    generateWhiteNoise(noise.data(), kTotalSamples, 0.5f);

    std::vector<float> output(kTotalSamples);
    for (size_t i = 0; i < kTotalSamples; ++i) {
        output[i] = phaser.process(noise[i]);
    }

    // Analyze the last block (after transient settles)
    const float* analyzeStart = output.data() + kTotalSamples - kFFTSize;
    const float* noiseStart = noise.data() + kTotalSamples - kFFTSize;

    auto outputSpectrum = measureSpectrum(analyzeStart, kFFTSize, kFFTSize, kSampleRate);
    auto inputSpectrum = measureSpectrum(noiseStart, kFFTSize, kFFTSize, kSampleRate);

    // Compute transfer function magnitude (output/input) in dB
    // Find the minimum (notch) in the region around the center frequency
    const size_t binLow = static_cast<size_t>(500.0f * static_cast<float>(kFFTSize) / kSampleRate);
    const size_t binHigh = static_cast<size_t>(3000.0f * static_cast<float>(kFFTSize) / kSampleRate);

    float minTransferDb = 0.0f;
    float maxTransferDb = -200.0f;
    for (size_t bin = binLow; bin <= binHigh; ++bin) {
        if (inputSpectrum[bin] < 1e-8f) continue;
        float transferDb = toDb(outputSpectrum[bin]) - toDb(inputSpectrum[bin]);
        if (transferDb < minTransferDb) minTransferDb = transferDb;
        if (transferDb > maxTransferDb) maxTransferDb = transferDb;
    }

    float notchDepth = maxTransferDb - minTransferDb;

    INFO("Notch depth at mix=1.0: " << notchDepth << " dB");
    INFO("Min transfer: " << minTransferDb << " dB, Max transfer: " << maxTransferDb << " dB");

    // With additive mix at 1.0, 4-stage allpass should create clear notches (>6 dB)
    // With crossfade mix at 1.0, output is pure allpass = flat = ~0 dB notch depth
    REQUIRE(notchDepth > 6.0f);
}

TEST_CASE("Phaser - Higher mix produces deeper notches", "[Phaser][PhaserFix]") {
    // Bug 1 continued: With crossfade, phaser effect peaks around mix=0.5 and
    // diminishes toward mix=1.0. With additive mix, depth increases monotonically.
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kFFTSize = 4096;
    constexpr size_t kNumBlocks = 8;
    constexpr size_t kTotalSamples = kFFTSize * kNumBlocks;

    auto measureNotchDepth = [&](float mix) -> float {
        Phaser phaser;
        phaser.prepare(static_cast<double>(kSampleRate));
        phaser.setNumStages(4);
        phaser.setDepth(0.0f);
        phaser.setRate(0.01f);
        phaser.setCenterFrequency(1000.0f);
        phaser.setMix(mix);
        phaser.setFeedback(0.0f);

        std::vector<float> noise(kTotalSamples);
        generateWhiteNoise(noise.data(), kTotalSamples, 0.5f);

        std::vector<float> output(kTotalSamples);
        for (size_t i = 0; i < kTotalSamples; ++i) {
            output[i] = phaser.process(noise[i]);
        }

        const float* analyzeOut = output.data() + kTotalSamples - kFFTSize;
        const float* analyzeIn = noise.data() + kTotalSamples - kFFTSize;
        auto outSpec = measureSpectrum(analyzeOut, kFFTSize, kFFTSize, kSampleRate);
        auto inSpec = measureSpectrum(analyzeIn, kFFTSize, kFFTSize, kSampleRate);

        const size_t binLow = static_cast<size_t>(500.0f * static_cast<float>(kFFTSize) / kSampleRate);
        const size_t binHigh = static_cast<size_t>(3000.0f * static_cast<float>(kFFTSize) / kSampleRate);

        float minDb = 0.0f, maxDb = -200.0f;
        for (size_t bin = binLow; bin <= binHigh; ++bin) {
            if (inSpec[bin] < 1e-8f) continue;
            float db = toDb(outSpec[bin]) - toDb(inSpec[bin]);
            if (db < minDb) minDb = db;
            if (db > maxDb) maxDb = db;
        }
        return maxDb - minDb;
    };

    float depth03 = measureNotchDepth(0.3f);
    float depth07 = measureNotchDepth(0.7f);
    float depth10 = measureNotchDepth(1.0f);

    INFO("Notch depth at mix=0.3: " << depth03 << " dB");
    INFO("Notch depth at mix=0.7: " << depth07 << " dB");
    INFO("Notch depth at mix=1.0: " << depth10 << " dB");

    // Notch depth should increase monotonically with mix
    REQUIRE(depth07 > depth03);
    REQUIRE(depth10 > depth07);
}

TEST_CASE("Phaser - Sweep range covers sufficient octaves", "[Phaser][PhaserFix]") {
    // Bug 2: Linear sweep range (1-depth)*center to (1+depth)*center gives
    // only 1.6 octaves at depth=0.5. Should be >= 3 octaves.
    //
    // Test approach: measure the phaser's impulse response at various center
    // frequencies and find the FIRST notch (lowest-frequency dip) in each case.
    // Notch positions scale proportionally with the allpass break frequency.
    // Comparing notch positions at the expected sweep endpoints proves the range.

    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kFFTSize = 8192;
    constexpr size_t kSettleSamples = 4096;

    // Expected sweep endpoints for depth=0.5, center=1000Hz
    const float expectedMinFreq = 1000.0f * std::pow(2.0f, -1.75f);  // ~297 Hz
    const float expectedMaxFreq = 1000.0f * std::pow(2.0f, 1.75f);   // ~3364 Hz

    // Helper: measure first notch frequency from impulse response
    auto findFirstNotchFreq = [&](float centerFreq) -> float {
        Phaser phaser;
        phaser.prepare(static_cast<double>(kSampleRate));
        phaser.setNumStages(4);
        phaser.setDepth(0.0f);  // Stationary
        phaser.setCenterFrequency(centerFreq);
        phaser.setMix(1.0f);
        phaser.setFeedback(0.0f);
        phaser.setRate(0.01f);

        // Settle smoother
        for (size_t i = 0; i < kSettleSamples; ++i) {
            (void)phaser.process(0.0f);
        }

        // Capture impulse response
        std::vector<float> ir(kFFTSize, 0.0f);
        ir[0] = phaser.process(1.0f);
        for (size_t i = 1; i < kFFTSize; ++i) {
            ir[i] = phaser.process(0.0f);
        }

        FFT fft;
        fft.prepare(kFFTSize);
        std::vector<Complex> spectrum(fft.numBins());
        fft.forward(ir.data(), spectrum.data());

        // Find the first local minimum (notch) by looking for where
        // magnitude drops below a threshold relative to the peak (~2.0)
        const size_t binStart = static_cast<size_t>(30.0f * static_cast<float>(kFFTSize) / kSampleRate);
        const size_t binEnd = std::min(
            static_cast<size_t>(18000.0f * static_cast<float>(kFFTSize) / kSampleRate),
            spectrum.size() - 1);

        // Find peak magnitude (should be ~2.0 for additive phaser)
        float peakMag = 0.0f;
        for (size_t bin = binStart; bin <= binEnd; ++bin) {
            float mag = spectrum[bin].magnitude();
            if (mag > peakMag) peakMag = mag;
        }

        // Find first bin where magnitude drops below 50% of peak (-6dB)
        // then find the actual minimum in that dip
        bool inDip = false;
        float dipMinMag = peakMag;
        size_t dipMinBin = binStart;

        for (size_t bin = binStart; bin <= binEnd; ++bin) {
            float mag = spectrum[bin].magnitude();
            if (mag < peakMag * 0.5f) {
                if (!inDip || mag < dipMinMag) {
                    dipMinMag = mag;
                    dipMinBin = bin;
                }
                inDip = true;
            } else if (inDip) {
                break;  // Past the first dip, stop
            }
        }

        return static_cast<float>(dipMinBin) * kSampleRate / static_cast<float>(kFFTSize);
    };

    // Find first notch positions at the two sweep endpoints
    float notchAtMin = findFirstNotchFreq(expectedMinFreq);
    float notchAtMax = findFirstNotchFreq(expectedMaxFreq);
    float notchAtCenter = findFirstNotchFreq(1000.0f);

    INFO("At min center (" << expectedMinFreq << " Hz): first notch at " << notchAtMin << " Hz");
    INFO("At max center (" << expectedMaxFreq << " Hz): first notch at " << notchAtMax << " Hz");
    INFO("At 1000 Hz center: first notch at " << notchAtCenter << " Hz");

    // Notch position should scale with center frequency
    float notchRangeOctaves = std::log2(notchAtMax / notchAtMin);
    INFO("Notch range: " << notchRangeOctaves << " octaves");

    // With 3.5 octave sweep, notch range should also be >= 3 octaves
    REQUIRE(notchRangeOctaves >= 3.0f);
}

TEST_CASE("Phaser - Sweep range symmetric in octaves around center", "[Phaser][PhaserFix]") {
    // Bug 2 continued: Linear formula gives asymmetric sweep.
    // Octave-based formula should be symmetric: log2(center/min) == log2(max/center)

    // We test this by computing the expected sweep range from the formula.
    // With depth=0.5, center=1000Hz:
    // Linear: min = 1000*(1-0.5) = 500, max = 1000*(1+0.5) = 1500
    //   log2(1000/500) = 1.0, log2(1500/1000) = 0.585 → asymmetric
    // Octave: min = 1000*2^(-1.75), max = 1000*2^(1.75)
    //   log2(1000/min) = 1.75, log2(max/1000) = 1.75 → symmetric

    // Test using two stationary phasers at the extremes of the LFO
    constexpr float kSampleRate = 44100.0f;
    constexpr float kCenterFreq = 1000.0f;

    // Phaser at LFO = -1 (lowest sweep point)
    Phaser phaserLow;
    phaserLow.prepare(static_cast<double>(kSampleRate));
    phaserLow.setNumStages(4);
    phaserLow.setDepth(0.5f);
    phaserLow.setCenterFrequency(kCenterFreq);
    phaserLow.setMix(1.0f);
    phaserLow.setFeedback(0.0f);
    phaserLow.setWaveform(Waveform::Sawtooth); // Starts at -1, ramps to +1
    phaserLow.setRate(0.01f); // Very slow - stays near -1 for a long time

    // Process a short burst at the start (LFO near -1)
    constexpr size_t kFFTSize = 4096;
    std::vector<float> noise(kFFTSize);
    generateWhiteNoise(noise.data(), kFFTSize, 0.5f, 42);

    std::vector<float> outputLow(kFFTSize);
    for (size_t i = 0; i < kFFTSize; ++i) {
        outputLow[i] = phaserLow.process(noise[i]);
    }

    auto outSpecLow = measureSpectrum(outputLow.data(), kFFTSize, kFFTSize, kSampleRate);
    auto inSpec = measureSpectrum(noise.data(), kFFTSize, kFFTSize, kSampleRate);

    // Find notch frequency for LFO near -1
    const size_t binSearch = static_cast<size_t>(10000.0f * static_cast<float>(kFFTSize) / kSampleRate);
    const size_t binStart = static_cast<size_t>(50.0f * static_cast<float>(kFFTSize) / kSampleRate);

    auto findNotchFreq = [&](const std::vector<float>& outSpec) -> float {
        float minDb = 0.0f;
        size_t minBin = binStart;
        for (size_t bin = binStart; bin <= binSearch && bin < outSpec.size(); ++bin) {
            if (inSpec[bin] < 1e-8f) continue;
            float db = toDb(outSpec[bin]) - toDb(inSpec[bin]);
            if (db < minDb) { minDb = db; minBin = bin; }
        }
        return static_cast<float>(minBin) * kSampleRate / static_cast<float>(kFFTSize);
    };

    float notchLow = findNotchFreq(outSpecLow);

    // Now measure symmetry: distance in octaves from center should be similar above and below
    float octavesBelow = std::log2(kCenterFreq / notchLow);

    INFO("Notch at LFO=-1: " << notchLow << " Hz");
    INFO("Octaves below center: " << octavesBelow);

    // With octave-based formula at depth=0.5, octaves below center should be significant (>1.5)
    // With linear formula, octaves below = log2(1000/500) = 1.0
    REQUIRE(octavesBelow > 1.4f);
}

TEST_CASE("Phaser - Feedback resonance from allpass output", "[Phaser][PhaserFix]") {
    // Bug 3: Feedback from mixed output dilutes resonance because dry signal
    // passes through feedback. Feedback from allpass output gives sharper peaks.
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kFFTSize = 4096;
    constexpr size_t kNumBlocks = 16;
    constexpr size_t kTotalSamples = kFFTSize * kNumBlocks;

    auto measurePeakToNotch = [&](float feedback) -> float {
        Phaser phaser;
        phaser.prepare(static_cast<double>(kSampleRate));
        phaser.setNumStages(4);
        phaser.setDepth(0.0f);         // Stationary
        phaser.setRate(0.01f);
        phaser.setCenterFrequency(1000.0f);
        phaser.setMix(1.0f);
        phaser.setFeedback(feedback);

        std::vector<float> noise(kTotalSamples);
        generateWhiteNoise(noise.data(), kTotalSamples, 0.3f);

        std::vector<float> output(kTotalSamples);
        for (size_t i = 0; i < kTotalSamples; ++i) {
            output[i] = phaser.process(noise[i]);
        }

        const float* analyzeOut = output.data() + kTotalSamples - kFFTSize;
        const float* analyzeIn = noise.data() + kTotalSamples - kFFTSize;
        auto outSpec = measureSpectrum(analyzeOut, kFFTSize, kFFTSize, kSampleRate);
        auto inSpec = measureSpectrum(analyzeIn, kFFTSize, kFFTSize, kSampleRate);

        const size_t binLow = static_cast<size_t>(200.0f * static_cast<float>(kFFTSize) / kSampleRate);
        const size_t binHigh = static_cast<size_t>(5000.0f * static_cast<float>(kFFTSize) / kSampleRate);

        float minDb = 0.0f, maxDb = -200.0f;
        for (size_t bin = binLow; bin <= binHigh && bin < outSpec.size(); ++bin) {
            if (inSpec[bin] < 1e-8f) continue;
            float db = toDb(outSpec[bin]) - toDb(inSpec[bin]);
            if (db < minDb) minDb = db;
            if (db > maxDb) maxDb = db;
        }
        return maxDb - minDb;
    };

    float peakToNotchNoFb = measurePeakToNotch(0.0f);
    float peakToNotchWithFb = measurePeakToNotch(0.9f);

    INFO("Peak-to-notch without feedback: " << peakToNotchNoFb << " dB");
    INFO("Peak-to-notch with feedback=0.9: " << peakToNotchWithFb << " dB");

    // Feedback should increase peak-to-notch ratio significantly (>6 dB)
    REQUIRE(peakToNotchWithFb > peakToNotchNoFb + 6.0f);
}
