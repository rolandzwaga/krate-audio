// ==============================================================================
// Layer 1: DSP Primitives - Two-Pole Lowpass Filter Tests
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Constitution Principle XII: Test-First Development
//
// Tests for: dsp/include/krate/dsp/primitives/two_pole_lp.h
// Specification: specs/084-karplus-strong/spec.md (FR-014: 12dB/oct brightness)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/primitives/two_pole_lp.h>

#include <array>
#include <cmath>
#include <limits>
#include <random>

using namespace Krate::DSP;
using Catch::Approx;

// ==============================================================================
// Test Helpers (static to avoid ODR conflicts with other test files)
// ==============================================================================

namespace {

/// Generate a sine wave for testing
void generateSineWave(float* buffer, size_t numSamples, float frequency, double sampleRate, float amplitude = 1.0f) {
    const float phase_increment = static_cast<float>(2.0 * 3.14159265358979 * frequency / sampleRate);
    float phase = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = amplitude * std::sin(phase);
        phase += phase_increment;
        if (phase > 6.28318530718f) phase -= 6.28318530718f;
    }
}

/// Calculate RMS (Root Mean Square) of a buffer
float calculateRMS(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    float sumSquares = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        sumSquares += buffer[i] * buffer[i];
    }
    return std::sqrt(sumSquares / static_cast<float>(numSamples));
}

/// Convert linear amplitude to dB
float linearToDb(float linear) {
    if (linear <= 0.0f) return -144.0f;
    return 20.0f * std::log10(linear);
}

} // anonymous namespace

// ==============================================================================
// TC-TLP-001: Frequency response is approximately -3dB at cutoff (Butterworth)
// ==============================================================================

TEST_CASE("TwoPoleLP frequency response at cutoff", "[two_pole_lp][response]") {

    SECTION("TC-TLP-001: Response at cutoff is approximately -3dB") {
        TwoPoleLP filter;
        filter.prepare(44100.0);
        filter.setCutoff(1000.0f);

        // Generate 1kHz sine wave (at cutoff)
        constexpr size_t kNumSamples = 4410;  // 100ms
        std::array<float, kNumSamples> buffer;
        generateSineWave(buffer.data(), kNumSamples, 1000.0f, 44100.0);

        // Measure input RMS
        const float inputRMS = calculateRMS(buffer.data(), kNumSamples);

        // Process through filter
        filter.processBlock(buffer.data(), kNumSamples);

        // Measure output RMS (skip first 500 samples for settling)
        const float outputRMS = calculateRMS(buffer.data() + 500, kNumSamples - 500);

        // Calculate attenuation in dB
        const float attenuationDb = linearToDb(inputRMS) - linearToDb(outputRMS);

        // Butterworth at cutoff should be -3dB (within 0.5dB tolerance)
        REQUIRE(attenuationDb == Approx(3.0f).margin(0.5f));
    }
}

// ==============================================================================
// TC-TLP-002: Frequency response is -12dB one octave above cutoff (12dB/oct)
// ==============================================================================

TEST_CASE("TwoPoleLP frequency response one octave above cutoff", "[two_pole_lp][response]") {

    SECTION("TC-TLP-002: Response one octave above cutoff is -12dB") {
        TwoPoleLP filter;
        filter.prepare(44100.0);
        filter.setCutoff(1000.0f);

        // Generate 2kHz sine wave (one octave above 1kHz cutoff)
        constexpr size_t kNumSamples = 4410;  // 100ms
        std::array<float, kNumSamples> buffer;
        generateSineWave(buffer.data(), kNumSamples, 2000.0f, 44100.0);

        const float inputRMS = calculateRMS(buffer.data(), kNumSamples);

        filter.processBlock(buffer.data(), kNumSamples);

        // Skip settling time
        const float outputRMS = calculateRMS(buffer.data() + 500, kNumSamples - 500);

        const float attenuationDb = linearToDb(inputRMS) - linearToDb(outputRMS);

        // 12dB/octave slope: one octave above cutoff should be ~12dB down
        // Adding the 3dB at cutoff = ~15dB total, but we measure from flat
        // At 2x frequency, 2nd order = 12dB/oct, so relative to passband ~12dB attenuation
        // Actually for Butterworth 2nd order: at 2*fc, attenuation = 10*log10(1 + (2)^4) = 12.3dB
        REQUIRE(attenuationDb >= 11.0f);
        REQUIRE(attenuationDb <= 14.0f);
    }
}

// ==============================================================================
// TC-TLP-003: Passband is flat (within 0.5dB) below cutoff/2
// ==============================================================================

TEST_CASE("TwoPoleLP passband is flat below cutoff/2", "[two_pole_lp][response]") {

    SECTION("TC-TLP-003: Response at cutoff/4 is within 0.5dB of unity") {
        TwoPoleLP filter;
        filter.prepare(44100.0);
        filter.setCutoff(4000.0f);  // Use 4kHz cutoff

        // Generate 1kHz sine wave (cutoff/4)
        constexpr size_t kNumSamples = 4410;  // 100ms
        std::array<float, kNumSamples> buffer;
        generateSineWave(buffer.data(), kNumSamples, 1000.0f, 44100.0);

        const float inputRMS = calculateRMS(buffer.data(), kNumSamples);

        filter.processBlock(buffer.data(), kNumSamples);

        // Skip settling time
        const float outputRMS = calculateRMS(buffer.data() + 500, kNumSamples - 500);

        const float attenuationDb = linearToDb(inputRMS) - linearToDb(outputRMS);

        // Passband should be flat (less than 0.5dB attenuation)
        REQUIRE(attenuationDb < 0.5f);
        REQUIRE(attenuationDb > -0.5f);  // And not boosted
    }
}

// ==============================================================================
// TC-TLP-004: NaN/Inf input handling (returns 0, resets state)
// ==============================================================================

TEST_CASE("TwoPoleLP NaN/Inf handling", "[two_pole_lp][safety]") {

    TwoPoleLP filter;
    filter.prepare(44100.0);
    filter.setCutoff(1000.0f);

    // Process some normal samples first to set up state
    (void)filter.process(0.5f);
    (void)filter.process(0.3f);

    SECTION("NaN input returns 0 and resets state") {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float result = filter.process(nan);
        REQUIRE(result == 0.0f);

        // Next sample should start from reset state
        const float nextResult = filter.process(1.0f);
        REQUIRE_FALSE(std::isnan(nextResult));
    }

    SECTION("Positive infinity returns 0 and resets state") {
        const float inf = std::numeric_limits<float>::infinity();
        const float result = filter.process(inf);
        REQUIRE(result == 0.0f);

        const float nextResult = filter.process(1.0f);
        REQUIRE_FALSE(std::isinf(nextResult));
    }

    SECTION("Negative infinity returns 0 and resets state") {
        const float neg_inf = -std::numeric_limits<float>::infinity();
        const float result = filter.process(neg_inf);
        REQUIRE(result == 0.0f);

        const float nextResult = filter.process(1.0f);
        REQUIRE_FALSE(std::isinf(nextResult));
    }
}

// ==============================================================================
// TC-TLP-005: Returns input unchanged if not prepared
// ==============================================================================

TEST_CASE("TwoPoleLP unprepared filter returns input unchanged", "[two_pole_lp][unprepared]") {

    TwoPoleLP filter;  // NOT prepared

    SECTION("Single sample returns unchanged") {
        REQUIRE(filter.process(0.5f) == 0.5f);
        REQUIRE(filter.process(-0.7f) == -0.7f);
        REQUIRE(filter.process(0.0f) == 0.0f);
        REQUIRE(filter.process(1.0f) == 1.0f);
    }

    SECTION("Block returns unchanged") {
        std::array<float, 16> buffer = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f,
                                         -0.1f, -0.2f, -0.3f, -0.4f, -0.5f, -0.6f, -0.7f, -0.8f};
        std::array<float, 16> original = buffer;

        filter.processBlock(buffer.data(), buffer.size());

        for (size_t i = 0; i < buffer.size(); ++i) {
            REQUIRE(buffer[i] == original[i]);
        }
    }
}

// ==============================================================================
// processBlock produces bit-identical output to process()
// ==============================================================================

TEST_CASE("TwoPoleLP processBlock produces bit-identical output to process()", "[two_pole_lp][block]") {

    TwoPoleLP filter1;
    TwoPoleLP filter2;
    filter1.prepare(44100.0);
    filter2.prepare(44100.0);
    filter1.setCutoff(2000.0f);
    filter2.setCutoff(2000.0f);

    constexpr size_t kNumSamples = 256;
    std::array<float, kNumSamples> input;
    std::array<float, kNumSamples> output1;  // sample-by-sample
    std::array<float, kNumSamples> output2;  // block

    // Generate random input
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (size_t i = 0; i < kNumSamples; ++i) {
        input[i] = dist(rng);
    }

    // Process sample-by-sample
    for (size_t i = 0; i < kNumSamples; ++i) {
        output1[i] = filter1.process(input[i]);
    }

    // Process as block
    std::copy(input.begin(), input.end(), output2.begin());
    filter2.processBlock(output2.data(), kNumSamples);

    // Outputs must be bit-identical
    for (size_t i = 0; i < kNumSamples; ++i) {
        REQUIRE(output1[i] == output2[i]);
    }
}

// ==============================================================================
// reset() clears filter state
// ==============================================================================

TEST_CASE("TwoPoleLP reset clears state", "[two_pole_lp][reset]") {

    TwoPoleLP filter;
    filter.prepare(44100.0);
    filter.setCutoff(1000.0f);

    // Process some samples to build up state
    for (int i = 0; i < 100; ++i) {
        (void)filter.process(1.0f);
    }

    // Capture output before reset
    const float beforeReset = filter.process(0.0f);
    REQUIRE(beforeReset > 0.01f);  // State should cause non-zero output

    // Reset
    filter.reset();

    // After reset, output should start from zero state
    const float afterReset = filter.process(0.0f);
    REQUIRE(afterReset == 0.0f);
}

// ==============================================================================
// getCutoff returns current cutoff frequency
// ==============================================================================

TEST_CASE("TwoPoleLP getCutoff returns current cutoff", "[two_pole_lp][api]") {

    TwoPoleLP filter;
    filter.prepare(44100.0);

    filter.setCutoff(1000.0f);
    REQUIRE(filter.getCutoff() == Approx(1000.0f));

    filter.setCutoff(5000.0f);
    REQUIRE(filter.getCutoff() == Approx(5000.0f));

    filter.setCutoff(200.0f);
    REQUIRE(filter.getCutoff() == Approx(200.0f));
}

// ==============================================================================
// Long-term stability test
// ==============================================================================

TEST_CASE("TwoPoleLP 100k sample stability test", "[two_pole_lp][stability]") {

    TwoPoleLP filter;
    filter.prepare(44100.0);
    filter.setCutoff(1000.0f);

    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    constexpr size_t kNumSamples = 100000;
    float output = 0.0f;

    for (size_t i = 0; i < kNumSamples; ++i) {
        output = filter.process(dist(rng));

        // Verify no NaN or Inf at sample level (check every 10000 samples for speed)
        if (i % 10000 == 0) {
            REQUIRE_FALSE(std::isnan(output));
            REQUIRE_FALSE(std::isinf(output));
        }
    }

    // Final output should be valid
    REQUIRE_FALSE(std::isnan(output));
    REQUIRE_FALSE(std::isinf(output));
    REQUIRE(std::abs(output) <= 10.0f);  // Should be bounded
}

// ==============================================================================
// noexcept verification
// ==============================================================================

TEST_CASE("TwoPoleLP methods are noexcept", "[two_pole_lp][safety]") {

    TwoPoleLP filter;
    STATIC_REQUIRE(noexcept(filter.prepare(44100.0)));
    STATIC_REQUIRE(noexcept(filter.setCutoff(1000.0f)));
    STATIC_REQUIRE(noexcept(filter.getCutoff()));
    STATIC_REQUIRE(noexcept(filter.process(0.5f)));
    STATIC_REQUIRE(noexcept(filter.reset()));
}
