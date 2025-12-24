// ==============================================================================
// Unit Tests: DiffusionNetwork
// ==============================================================================
// Layer 2: DSP Processor Tests
// Feature: 015-diffusion-network
// Constitution Principle VIII: DSP algorithms must be independently testable
// Constitution Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/processors/diffusion_network.h"

#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

using namespace Iterum::DSP;
using Catch::Approx;

// ==============================================================================
// Test Helpers
// ==============================================================================

namespace {

constexpr float kTestSampleRate = 44100.0f;
constexpr size_t kTestBlockSize = 512;
constexpr float kTolerance = 1e-5f;
constexpr float kTestPi = 3.14159265358979323846f;
constexpr float kTestTwoPi = 2.0f * kTestPi;

// Generate a sine wave at specified frequency
inline void generateSine(float* buffer, size_t size, float frequency, float sampleRate) {
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = std::sin(kTestTwoPi * frequency * static_cast<float>(i) / sampleRate);
    }
}

// Generate white noise with optional seed for reproducibility
inline void generateWhiteNoise(float* buffer, size_t size, unsigned int seed = 42) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = dist(gen);
    }
}

// Generate impulse (single sample at 1.0, rest zeros)
inline void generateImpulse(float* buffer, size_t size) {
    std::fill(buffer, buffer + size, 0.0f);
    if (size > 0) buffer[0] = 1.0f;
}

// Calculate RMS of a buffer
inline float calculateRMS(const float* buffer, size_t size) {
    if (size == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(size));
}

// Calculate peak absolute value
inline float calculatePeak(const float* buffer, size_t size) {
    float peak = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        float absVal = std::abs(buffer[i]);
        if (absVal > peak) peak = absVal;
    }
    return peak;
}

// Convert linear amplitude to decibels
inline float linearToDb(float linear) {
    if (linear <= 0.0f) return -144.0f;
    return 20.0f * std::log10(linear);
}

// Check if buffer contains any NaN or Inf values
inline bool hasInvalidSamples(const float* buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (std::isnan(buffer[i]) || std::isinf(buffer[i])) {
            return true;
        }
    }
    return false;
}

// Check if two buffers are equal within tolerance
inline bool buffersEqual(const float* a, const float* b, size_t size, float tolerance = kTolerance) {
    for (size_t i = 0; i < size; ++i) {
        if (std::abs(a[i] - b[i]) > tolerance) {
            return false;
        }
    }
    return true;
}

// Calculate energy in a buffer (sum of squared samples)
inline float calculateEnergy(const float* buffer, size_t size) {
    float energy = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        energy += buffer[i] * buffer[i];
    }
    return energy;
}

// Find the sample index where cumulative energy reaches a threshold percentage
inline size_t findEnergyThresholdSample(const float* buffer, size_t size, float thresholdPercent) {
    float totalEnergy = calculateEnergy(buffer, size);
    float threshold = totalEnergy * thresholdPercent;
    float cumulative = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        cumulative += buffer[i] * buffer[i];
        if (cumulative >= threshold) {
            return i;
        }
    }
    return size;
}

// Calculate cross-correlation coefficient between two buffers at lag 0
inline float crossCorrelation(const float* a, const float* b, size_t size) {
    float sumA = 0.0f, sumB = 0.0f, sumAB = 0.0f;
    float sumA2 = 0.0f, sumB2 = 0.0f;

    for (size_t i = 0; i < size; ++i) {
        sumA += a[i];
        sumB += b[i];
        sumAB += a[i] * b[i];
        sumA2 += a[i] * a[i];
        sumB2 += b[i] * b[i];
    }

    float n = static_cast<float>(size);
    float numerator = n * sumAB - sumA * sumB;
    float denominator = std::sqrt((n * sumA2 - sumA * sumA) * (n * sumB2 - sumB * sumB));

    if (denominator < 1e-10f) return 0.0f;
    return numerator / denominator;
}

// Simple DFT for spectrum analysis (not optimized, for testing only)
inline std::vector<float> computeMagnitudeSpectrum(const float* buffer, size_t size) {
    std::vector<float> spectrum(size / 2 + 1);

    for (size_t k = 0; k <= size / 2; ++k) {
        float real = 0.0f, imag = 0.0f;
        for (size_t n = 0; n < size; ++n) {
            float angle = -kTestTwoPi * static_cast<float>(k * n) / static_cast<float>(size);
            real += buffer[n] * std::cos(angle);
            imag += buffer[n] * std::sin(angle);
        }
        spectrum[k] = std::sqrt(real * real + imag * imag) / static_cast<float>(size);
    }

    return spectrum;
}

} // anonymous namespace

// ==============================================================================
// Phase 2: AllpassStage Tests
// ==============================================================================

// T005: AllpassStage single sample processing
TEST_CASE("AllpassStage processes single samples correctly", "[diffusion][allpass][US1]") {
    AllpassStage stage;

    SECTION("prepare sets up delay line with correct max delay") {
        // Max delay of 50ms at 44.1kHz = 2205 samples
        stage.prepare(kTestSampleRate, 0.05f);  // 50ms max
        stage.reset();

        // Should be able to process without crash
        float output = stage.process(1.0f, 100.0f);  // 100 samples delay
        REQUIRE_FALSE(std::isnan(output));
    }

    SECTION("reset clears internal state") {
        stage.prepare(kTestSampleRate, 0.05f);

        // Feed some signal
        for (int i = 0; i < 100; ++i) {
            (void)stage.process(1.0f, 50.0f);
        }

        stage.reset();

        // After reset, processing silence should produce silence (eventually)
        float lastOutput = 0.0f;
        for (int i = 0; i < 1000; ++i) {
            lastOutput = stage.process(0.0f, 50.0f);
        }
        REQUIRE(std::abs(lastOutput) < 0.01f);
    }

    SECTION("implements Schroeder allpass formula") {
        // y[n] = -g * x[n] + x[n-D] + g * y[n-D]
        // For D=1 (1 sample delay) and first impulse input, we can verify
        stage.prepare(kTestSampleRate, 0.001f);  // 1ms max
        stage.reset();

        constexpr float g = 0.618033988749895f;  // Golden ratio inverse

        // Process impulse
        float y0 = stage.process(1.0f, 1.0f);  // First sample: x[0]=1, x[-1]=0, y[-1]=0
        // Expected: y[0] = -g * 1 + 0 + g * 0 = -g
        REQUIRE(y0 == Approx(-g).margin(0.001f));

        // Second sample: x[1]=0, x[0]=1, y[0]=-g
        float y1 = stage.process(0.0f, 1.0f);
        // Expected: y[1] = -g * 0 + 1 + g * (-g) = 1 - g^2
        float expected_y1 = 1.0f - g * g;
        REQUIRE(y1 == Approx(expected_y1).margin(0.001f));
    }
}

// T006: AllpassStage preserves frequency spectrum (flat response)
TEST_CASE("AllpassStage preserves frequency spectrum", "[diffusion][allpass][US1]") {
    AllpassStage stage;
    stage.prepare(kTestSampleRate, 0.05f);
    stage.reset();

    constexpr size_t kFFTSize = 4096;
    std::array<float, kFFTSize> impulse{};
    impulse[0] = 1.0f;

    std::array<float, kFFTSize> output{};

    // Process impulse through allpass
    for (size_t i = 0; i < kFFTSize; ++i) {
        output[i] = stage.process(impulse[i], 50.0f);  // 50 sample delay
    }

    // Compute magnitude spectra
    auto inputSpectrum = computeMagnitudeSpectrum(impulse.data(), kFFTSize);
    auto outputSpectrum = computeMagnitudeSpectrum(output.data(), kFFTSize);

    // Check that magnitudes are approximately equal (within 0.5dB = ~6% tolerance)
    // Skip DC and very high frequencies
    size_t startBin = 10;   // ~100Hz
    size_t endBin = kFFTSize / 4;  // ~5.5kHz

    for (size_t i = startBin; i < endBin; ++i) {
        if (inputSpectrum[i] > 1e-6f) {
            float ratio = outputSpectrum[i] / inputSpectrum[i];
            float ratioDb = 20.0f * std::log10(ratio);
            REQUIRE(std::abs(ratioDb) < 0.5f);  // Within ±0.5dB
        }
    }
}

// T007: AllpassStage supports delay time modulation
TEST_CASE("AllpassStage supports delay time modulation", "[diffusion][allpass][US4]") {
    AllpassStage stage;
    stage.prepare(kTestSampleRate, 0.05f);  // 50ms max delay
    stage.reset();

    SECTION("varying delay time produces valid output") {
        // Simulate LFO-modulated delay
        std::array<float, 1000> output{};
        float baseDelay = 100.0f;  // 100 samples
        float modDepth = 20.0f;    // ±20 samples

        for (size_t i = 0; i < output.size(); ++i) {
            // Sine LFO at 2Hz
            float lfo = std::sin(kTestTwoPi * 2.0f * static_cast<float>(i) / kTestSampleRate);
            float delay = baseDelay + modDepth * lfo;
            output[i] = stage.process(0.5f, delay);
        }

        // Verify no NaN or Inf values
        REQUIRE_FALSE(hasInvalidSamples(output.data(), output.size()));

        // Verify output is bounded
        float peak = calculatePeak(output.data(), output.size());
        REQUIRE(peak < 2.0f);  // Reasonable bound for allpass
    }

    SECTION("delay time clamped to valid range") {
        // Request delay beyond max - should clamp, not crash
        float output1 = stage.process(1.0f, 10000.0f);  // Way beyond max
        REQUIRE_FALSE(std::isnan(output1));

        // Negative delay should clamp to 0
        float output2 = stage.process(1.0f, -10.0f);
        REQUIRE_FALSE(std::isnan(output2));
    }
}

// ==============================================================================
// Phase 3: User Story 1 + 2 Tests - Basic Diffusion + Size Control (P1 MVP)
// ==============================================================================

// Tests for DiffusionNetwork basic functionality and size control
// (T015-T021)

// ==============================================================================
// Phase 4: User Story 3 Tests - Density Control (P2)
// ==============================================================================

// Tests for density parameter
// (T035-T039)

// ==============================================================================
// Phase 5: User Story 4 Tests - Modulation (P2)
// ==============================================================================

// Tests for LFO modulation
// (T047-T050)

// ==============================================================================
// Phase 6: User Story 5 Tests - Stereo Width Control (P2)
// ==============================================================================

// Tests for stereo width parameter
// (T059-T062)

// ==============================================================================
// Phase 7: User Story 6 Tests - Real-Time Safety (P1)
// ==============================================================================

// Tests for real-time safety compliance
// (T069-T072)

// ==============================================================================
// Phase 8: Edge Cases
// ==============================================================================

// Tests for edge cases and error handling
// (T078-T080)
