// ==============================================================================
// Layer 1: DSP Primitives - One-Pole Filter Tests
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Constitution Principle XII: Test-First Development
//
// Tests for: dsp/include/krate/dsp/primitives/one_pole.h
// Contract: specs/070-filter-foundations/contracts/one_pole.h
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/primitives/one_pole.h>

#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

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
// OnePoleLP Tests - SC-001, SC-002: Frequency Response
// ==============================================================================

TEST_CASE("OnePoleLP frequency response", "[one_pole][LP][response]") {

    SECTION("SC-001: 1kHz cutoff attenuates 10kHz by at least 18dB") {
        OnePoleLP filter;
        filter.prepare(44100.0);
        filter.setCutoff(1000.0f);

        // Generate 10kHz sine wave (1 octave = 6dB, 10kHz is ~3.3 octaves above 1kHz)
        // For 6dB/octave filter, expect ~20dB attenuation
        constexpr size_t kNumSamples = 4410;  // 100ms
        std::array<float, kNumSamples> buffer;
        generateSineWave(buffer.data(), kNumSamples, 10000.0f, 44100.0);

        // Measure input RMS
        const float inputRMS = calculateRMS(buffer.data(), kNumSamples);

        // Process through filter
        filter.processBlock(buffer.data(), kNumSamples);

        // Measure output RMS (skip first 1000 samples for settling)
        const float outputRMS = calculateRMS(buffer.data() + 1000, kNumSamples - 1000);

        // Calculate attenuation in dB
        const float attenuationDb = linearToDb(inputRMS) - linearToDb(outputRMS);

        // Should attenuate by at least 18dB (within 2dB of theoretical 20dB)
        REQUIRE(attenuationDb >= 18.0f);
    }

    SECTION("SC-002: 1kHz cutoff passes 100Hz with less than 0.5dB attenuation") {
        OnePoleLP filter;
        filter.prepare(44100.0);
        filter.setCutoff(1000.0f);

        constexpr size_t kNumSamples = 4410;  // 100ms
        std::array<float, kNumSamples> buffer;
        generateSineWave(buffer.data(), kNumSamples, 100.0f, 44100.0);

        const float inputRMS = calculateRMS(buffer.data(), kNumSamples);

        filter.processBlock(buffer.data(), kNumSamples);

        // Skip settling time
        const float outputRMS = calculateRMS(buffer.data() + 500, kNumSamples - 500);

        const float attenuationDb = linearToDb(inputRMS) - linearToDb(outputRMS);

        // Should pass with less than 0.5dB attenuation
        REQUIRE(attenuationDb < 0.5f);
    }
}

// ==============================================================================
// OnePoleLP Tests - SC-009: processBlock matches process()
// ==============================================================================

TEST_CASE("OnePoleLP processBlock produces bit-identical output to process()", "[one_pole][LP][block]") {

    OnePoleLP filter1;
    OnePoleLP filter2;
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

    // SC-009: Outputs must be bit-identical
    for (size_t i = 0; i < kNumSamples; ++i) {
        REQUIRE(output1[i] == output2[i]);
    }
}

// ==============================================================================
// OnePoleLP Tests - SC-010: Long-term stability (1M samples)
// ==============================================================================

TEST_CASE("OnePoleLP 1M sample stability test", "[one_pole][LP][stability]") {

    OnePoleLP filter;
    filter.prepare(44100.0);
    filter.setCutoff(1000.0f);

    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    constexpr size_t kNumSamples = 1000000;
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
// OnePoleLP Tests - FR-027: Unprepared filter returns input unchanged
// ==============================================================================

TEST_CASE("OnePoleLP unprepared filter returns input unchanged", "[one_pole][LP][unprepared]") {

    OnePoleLP filter;  // NOT prepared

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
// OnePoleLP Tests - FR-034: NaN/Inf handling
// ==============================================================================

TEST_CASE("OnePoleLP NaN/Inf handling", "[one_pole][LP][safety]") {

    OnePoleLP filter;
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
// OnePoleLP Tests - reset() and basic operations
// ==============================================================================

TEST_CASE("OnePoleLP reset clears state", "[one_pole][LP][reset]") {

    OnePoleLP filter;
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

TEST_CASE("OnePoleLP edge cases for parameters", "[one_pole][LP][edge]") {

    OnePoleLP filter;

    SECTION("Zero sample rate defaults to 44100") {
        filter.prepare(0.0);
        filter.setCutoff(1000.0f);
        // Should still work
        const float result = filter.process(0.5f);
        REQUIRE_FALSE(std::isnan(result));
    }

    SECTION("Negative sample rate defaults to 44100") {
        filter.prepare(-44100.0);
        filter.setCutoff(1000.0f);
        const float result = filter.process(0.5f);
        REQUIRE_FALSE(std::isnan(result));
    }

    SECTION("Zero cutoff is clamped to minimum") {
        filter.prepare(44100.0);
        filter.setCutoff(0.0f);
        const float result = filter.process(0.5f);
        REQUIRE_FALSE(std::isnan(result));
    }

    SECTION("Negative cutoff is clamped to minimum") {
        filter.prepare(44100.0);
        filter.setCutoff(-1000.0f);
        const float result = filter.process(0.5f);
        REQUIRE_FALSE(std::isnan(result));
    }

    SECTION("Cutoff exceeding Nyquist is clamped") {
        filter.prepare(44100.0);
        filter.setCutoff(30000.0f);  // Above Nyquist
        const float result = filter.process(0.5f);
        REQUIRE_FALSE(std::isnan(result));
    }
}

// ==============================================================================
// OnePoleHP Tests - SC-003, SC-004: Frequency Response
// ==============================================================================

TEST_CASE("OnePoleHP frequency response", "[one_pole][HP][response]") {

    SECTION("SC-003: 100Hz cutoff attenuates 10Hz by at least 18dB") {
        OnePoleHP filter;
        filter.prepare(44100.0);
        filter.setCutoff(100.0f);

        // Generate 10Hz sine wave
        constexpr size_t kNumSamples = 44100;  // 1 second for low frequency
        std::vector<float> buffer(kNumSamples);
        generateSineWave(buffer.data(), kNumSamples, 10.0f, 44100.0);

        const float inputRMS = calculateRMS(buffer.data(), kNumSamples);

        filter.processBlock(buffer.data(), kNumSamples);

        // Skip settling time
        const float outputRMS = calculateRMS(buffer.data() + 4410, kNumSamples - 4410);

        const float attenuationDb = linearToDb(inputRMS) - linearToDb(outputRMS);

        // Should attenuate by at least 18dB
        REQUIRE(attenuationDb >= 18.0f);
    }

    SECTION("SC-004: 100Hz cutoff passes 1000Hz with less than 0.5dB attenuation") {
        OnePoleHP filter;
        filter.prepare(44100.0);
        filter.setCutoff(100.0f);

        constexpr size_t kNumSamples = 4410;
        std::array<float, kNumSamples> buffer;
        generateSineWave(buffer.data(), kNumSamples, 1000.0f, 44100.0);

        const float inputRMS = calculateRMS(buffer.data(), kNumSamples);

        filter.processBlock(buffer.data(), kNumSamples);

        const float outputRMS = calculateRMS(buffer.data() + 500, kNumSamples - 500);

        const float attenuationDb = linearToDb(inputRMS) - linearToDb(outputRMS);

        REQUIRE(attenuationDb < 0.5f);
    }
}

// ==============================================================================
// OnePoleHP Tests - DC rejection
// ==============================================================================

TEST_CASE("OnePoleHP DC rejection", "[one_pole][HP][dc]") {

    OnePoleHP filter;
    filter.prepare(44100.0);
    filter.setCutoff(20.0f);  // 20Hz cutoff for DC blocking

    // Apply constant DC signal
    constexpr size_t kNumSamples = 44100;  // 1 second
    float output = 0.0f;

    for (size_t i = 0; i < kNumSamples; ++i) {
        output = filter.process(1.0f);  // Constant DC
    }

    // Time constant tau = 1 / (2*pi*fc) = 1 / (2*pi*20) ~= 8ms
    // After 1 second (many time constants), DC should be mostly rejected
    // The output should decay to less than 1% of input
    REQUIRE(std::abs(output) < 0.01f);
}

// ==============================================================================
// OnePoleHP Tests - SC-009: processBlock matches process()
// ==============================================================================

TEST_CASE("OnePoleHP processBlock produces bit-identical output to process()", "[one_pole][HP][block]") {

    OnePoleHP filter1;
    OnePoleHP filter2;
    filter1.prepare(44100.0);
    filter2.prepare(44100.0);
    filter1.setCutoff(200.0f);
    filter2.setCutoff(200.0f);

    constexpr size_t kNumSamples = 256;
    std::array<float, kNumSamples> input;
    std::array<float, kNumSamples> output1;
    std::array<float, kNumSamples> output2;

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (size_t i = 0; i < kNumSamples; ++i) {
        input[i] = dist(rng);
    }

    for (size_t i = 0; i < kNumSamples; ++i) {
        output1[i] = filter1.process(input[i]);
    }

    std::copy(input.begin(), input.end(), output2.begin());
    filter2.processBlock(output2.data(), kNumSamples);

    for (size_t i = 0; i < kNumSamples; ++i) {
        REQUIRE(output1[i] == output2[i]);
    }
}

// ==============================================================================
// OnePoleHP Tests - FR-027: Unprepared filter returns input unchanged
// ==============================================================================

TEST_CASE("OnePoleHP unprepared filter returns input unchanged", "[one_pole][HP][unprepared]") {

    OnePoleHP filter;  // NOT prepared

    REQUIRE(filter.process(0.5f) == 0.5f);
    REQUIRE(filter.process(-0.7f) == -0.7f);
    REQUIRE(filter.process(0.0f) == 0.0f);
}

// ==============================================================================
// OnePoleHP Tests - FR-034: NaN/Inf handling
// ==============================================================================

TEST_CASE("OnePoleHP NaN/Inf handling", "[one_pole][HP][safety]") {

    OnePoleHP filter;
    filter.prepare(44100.0);
    filter.setCutoff(100.0f);

    (void)filter.process(0.5f);
    (void)filter.process(0.3f);

    SECTION("NaN input returns 0 and resets state") {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float result = filter.process(nan);
        REQUIRE(result == 0.0f);
    }

    SECTION("Infinity input returns 0 and resets state") {
        const float inf = std::numeric_limits<float>::infinity();
        const float result = filter.process(inf);
        REQUIRE(result == 0.0f);
    }
}

// ==============================================================================
// OnePoleHP Tests - reset()
// ==============================================================================

TEST_CASE("OnePoleHP reset clears state", "[one_pole][HP][reset]") {

    OnePoleHP filter;
    filter.prepare(44100.0);
    filter.setCutoff(100.0f);

    // Process to build state
    for (int i = 0; i < 100; ++i) {
        (void)filter.process(1.0f);
    }

    filter.reset();

    // After reset, state should be cleared
    // Processing a constant should give the same result as fresh
    OnePoleHP fresh;
    fresh.prepare(44100.0);
    fresh.setCutoff(100.0f);

    const float resetResult = filter.process(1.0f);
    const float freshResult = fresh.process(1.0f);

    REQUIRE(resetResult == Approx(freshResult).margin(1e-6f));
}

// ==============================================================================
// LeakyIntegrator Tests - SC-005: Time constant verification
// ==============================================================================

TEST_CASE("LeakyIntegrator time constant verification", "[one_pole][leaky][time_constant]") {

    SECTION("SC-005: leak=0.999 at 44100Hz produces time constant within 5% of 22.68ms") {
        LeakyIntegrator integrator;
        integrator.setLeak(0.999f);

        // Time constant tau = -1 / (fs * ln(leak))
        // For leak=0.999, fs=44100:
        // tau = -1 / (44100 * ln(0.999)) = -1 / (44100 * -0.001001) = 22.67ms

        const double sampleRate = 44100.0;
        const float leak = 0.999f;
        const float theoreticalTauMs = static_cast<float>(-1000.0 / (sampleRate * std::log(leak)));

        // Verify theoretical is close to 22.68ms
        REQUIRE(theoreticalTauMs == Approx(22.68f).margin(0.5f));

        // Test: Apply a unit impulse and measure decay
        // After one time constant, amplitude should decay to 1/e (~0.368)
        integrator.reset();

        // Apply impulse
        float output = integrator.process(1.0f);

        // Wait for one time constant worth of samples
        const int samplesPerTau = static_cast<int>(sampleRate * theoreticalTauMs / 1000.0f);

        for (int i = 0; i < samplesPerTau; ++i) {
            output = integrator.process(0.0f);
        }

        // After one time constant, should be approximately 1/e = 0.368
        const float expectedDecay = std::exp(-1.0f);
        REQUIRE(output == Approx(expectedDecay).margin(0.05f));  // 5% tolerance
    }
}

// ==============================================================================
// LeakyIntegrator Tests - Exponential decay behavior
// ==============================================================================

TEST_CASE("LeakyIntegrator exponential decay", "[one_pole][leaky][decay]") {

    LeakyIntegrator integrator(0.995f);

    // Apply burst of 1.0 samples
    for (int i = 0; i < 10; ++i) {
        (void)integrator.process(1.0f);
    }

    // Capture peak
    float peak = integrator.getState();
    REQUIRE(peak > 0.0f);

    // Apply zeros and verify smooth decay
    float previous = peak;
    for (int i = 0; i < 1000; ++i) {
        float current = integrator.process(0.0f);

        // Each sample should be smaller than previous (decay)
        REQUIRE(current <= previous);
        REQUIRE(current >= 0.0f);

        previous = current;
    }

    // After 1000 samples of decay, should be significantly reduced
    REQUIRE(previous < peak * 0.01f);
}

// ==============================================================================
// LeakyIntegrator Tests - SC-009: processBlock matches process()
// ==============================================================================

TEST_CASE("LeakyIntegrator processBlock produces bit-identical output", "[one_pole][leaky][block]") {

    LeakyIntegrator int1(0.99f);
    LeakyIntegrator int2(0.99f);

    constexpr size_t kNumSamples = 256;
    std::array<float, kNumSamples> input;
    std::array<float, kNumSamples> output1;
    std::array<float, kNumSamples> output2;

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);  // Positive for envelope
    for (size_t i = 0; i < kNumSamples; ++i) {
        input[i] = dist(rng);
    }

    for (size_t i = 0; i < kNumSamples; ++i) {
        output1[i] = int1.process(input[i]);
    }

    std::copy(input.begin(), input.end(), output2.begin());
    int2.processBlock(output2.data(), kNumSamples);

    for (size_t i = 0; i < kNumSamples; ++i) {
        REQUIRE(output1[i] == output2[i]);
    }
}

// ==============================================================================
// LeakyIntegrator Tests - FR-034: NaN/Inf handling
// ==============================================================================

TEST_CASE("LeakyIntegrator NaN/Inf handling", "[one_pole][leaky][safety]") {

    LeakyIntegrator integrator(0.99f);

    (void)integrator.process(0.5f);

    SECTION("NaN input returns 0 and resets state") {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float result = integrator.process(nan);
        REQUIRE(result == 0.0f);
        REQUIRE(integrator.getState() == 0.0f);
    }

    SECTION("Infinity input returns 0 and resets state") {
        const float inf = std::numeric_limits<float>::infinity();
        const float result = integrator.process(inf);
        REQUIRE(result == 0.0f);
    }
}

// ==============================================================================
// LeakyIntegrator Tests - reset()
// ==============================================================================

TEST_CASE("LeakyIntegrator reset clears state to zero", "[one_pole][leaky][reset]") {

    LeakyIntegrator integrator(0.99f);

    // Build up state
    for (int i = 0; i < 100; ++i) {
        (void)integrator.process(1.0f);
    }

    REQUIRE(integrator.getState() > 0.0f);

    integrator.reset();

    REQUIRE(integrator.getState() == 0.0f);
}

// ==============================================================================
// LeakyIntegrator Tests - Edge cases
// ==============================================================================

TEST_CASE("LeakyIntegrator edge cases", "[one_pole][leaky][edge]") {

    SECTION("Leak coefficient outside [0,1) is clamped") {
        LeakyIntegrator integrator;

        integrator.setLeak(-0.5f);
        REQUIRE(integrator.getLeak() == 0.0f);

        integrator.setLeak(1.0f);
        REQUIRE(integrator.getLeak() < 1.0f);

        integrator.setLeak(2.0f);
        REQUIRE(integrator.getLeak() < 1.0f);
    }

    SECTION("Constructor with leak parameter works") {
        LeakyIntegrator integrator(0.95f);
        REQUIRE(integrator.getLeak() == Approx(0.95f));
    }

    SECTION("getLeak returns correct value") {
        LeakyIntegrator integrator;
        integrator.setLeak(0.987f);
        REQUIRE(integrator.getLeak() == Approx(0.987f));
    }

    SECTION("getState returns current state") {
        LeakyIntegrator integrator(0.9f);
        (void)integrator.process(1.0f);
        REQUIRE(integrator.getState() == 1.0f);

        (void)integrator.process(0.0f);
        REQUIRE(integrator.getState() == 0.9f);
    }
}

// ==============================================================================
// noexcept verification
// ==============================================================================

TEST_CASE("One-pole filter methods are noexcept", "[one_pole][safety]") {

    SECTION("OnePoleLP methods are noexcept") {
        OnePoleLP filter;
        STATIC_REQUIRE(noexcept(filter.prepare(44100.0)));
        STATIC_REQUIRE(noexcept(filter.setCutoff(1000.0f)));
        STATIC_REQUIRE(noexcept(filter.process(0.5f)));
        STATIC_REQUIRE(noexcept(filter.reset()));
    }

    SECTION("OnePoleHP methods are noexcept") {
        OnePoleHP filter;
        STATIC_REQUIRE(noexcept(filter.prepare(44100.0)));
        STATIC_REQUIRE(noexcept(filter.setCutoff(1000.0f)));
        STATIC_REQUIRE(noexcept(filter.process(0.5f)));
        STATIC_REQUIRE(noexcept(filter.reset()));
    }

    SECTION("LeakyIntegrator methods are noexcept") {
        LeakyIntegrator integrator;
        STATIC_REQUIRE(noexcept(integrator.setLeak(0.99f)));
        STATIC_REQUIRE(noexcept(integrator.getLeak()));
        STATIC_REQUIRE(noexcept(integrator.process(0.5f)));
        STATIC_REQUIRE(noexcept(integrator.reset()));
        STATIC_REQUIRE(noexcept(integrator.getState()));
    }
}
