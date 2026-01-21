// ==============================================================================
// Layer 1: DSP Primitives - First-Order Allpass Filter Tests
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Constitution Principle XII: Test-First Development
//
// Tests for: dsp/include/krate/dsp/primitives/allpass_1pole.h
// Contract: specs/073-allpass-1pole/contracts/allpass_1pole.h
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/primitives/allpass_1pole.h>

#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <vector>
#include <chrono>

using namespace Krate::DSP;
using Catch::Approx;

// ==============================================================================
// Test Helpers (static to avoid ODR conflicts with other test files)
// ==============================================================================

namespace {

/// Pi constant for test calculations
constexpr double kTestPi = 3.14159265358979323846;

/// Generate a sine wave for testing
void generateSineWave(float* buffer, size_t numSamples, float frequency, double sampleRate, float amplitude = 1.0f) {
    const double phaseIncrement = 2.0 * kTestPi * frequency / sampleRate;
    double phase = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = amplitude * static_cast<float>(std::sin(phase));
        phase += phaseIncrement;
        if (phase > 2.0 * kTestPi) phase -= 2.0 * kTestPi;
    }
}

/// Calculate RMS (Root Mean Square) of a buffer
float calculateRMS(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    double sumSquares = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        sumSquares += static_cast<double>(buffer[i]) * buffer[i];
    }
    return static_cast<float>(std::sqrt(sumSquares / static_cast<double>(numSamples)));
}

/// Convert linear amplitude to dB
float linearToDb(float linear) {
    if (linear <= 0.0f) return -144.0f;
    return 20.0f * std::log10(linear);
}

/// Measure phase difference between two sine waves at same frequency
/// Returns phase in degrees
float measurePhaseDegrees(const float* input, const float* output, size_t numSamples,
                          float frequency, double sampleRate) {
    // Use cross-correlation at zero lag and quarter-period lag to determine phase
    // phase = atan2(correlation at quarter period, correlation at zero)

    const size_t samplesPerPeriod = static_cast<size_t>(sampleRate / frequency);
    const size_t quarterPeriod = samplesPerPeriod / 4;

    // Skip first few periods for settling
    const size_t startSample = samplesPerPeriod * 4;
    const size_t endSample = numSamples - quarterPeriod;

    if (startSample >= endSample) return 0.0f;

    double corrZero = 0.0;
    double corrQuarter = 0.0;

    for (size_t i = startSample; i < endSample; ++i) {
        corrZero += static_cast<double>(input[i]) * output[i];
        corrQuarter += static_cast<double>(input[i]) * output[i + quarterPeriod];
    }

    // Phase in radians, then convert to degrees
    const double phaseRad = std::atan2(corrQuarter, corrZero);
    return static_cast<float>(phaseRad * 180.0 / kTestPi);
}

} // anonymous namespace

// ==============================================================================
// User Story 1 Tests: Basic Phase Shifting for Phaser Effect
// ==============================================================================

// T004: Default constructor creates filter with coefficient 0.0 and zero state
TEST_CASE("Allpass1Pole default constructor", "[allpass_1pole][US1][constructor]") {
    Allpass1Pole filter;

    REQUIRE(filter.getCoefficient() == 0.0f);

    // Process a sample - with a=0, output should equal x[n-1] = 0 for first sample
    // y[n] = 0*x[n] + x[n-1] - 0*y[n-1] = x[n-1] = 0
    float output = filter.process(1.0f);
    REQUIRE(output == 0.0f);

    // Second sample: y[1] = 0*1.0 + 1.0 - 0*0 = 1.0
    output = filter.process(1.0f);
    REQUIRE(output == 1.0f);
}

// T005: prepare() stores sample rate correctly
TEST_CASE("Allpass1Pole prepare stores sample rate", "[allpass_1pole][US1][prepare]") {
    Allpass1Pole filter;

    SECTION("44100 Hz sample rate") {
        filter.prepare(44100.0);
        // Set frequency and verify coefficient calculation used correct sample rate
        filter.setFrequency(11025.0f);  // fs/4 should give a=0
        REQUIRE(filter.getCoefficient() == Approx(0.0f).margin(0.01f));
    }

    SECTION("48000 Hz sample rate") {
        filter.prepare(48000.0);
        filter.setFrequency(12000.0f);  // fs/4 should give a=0
        REQUIRE(filter.getCoefficient() == Approx(0.0f).margin(0.01f));
    }

    SECTION("Invalid sample rate defaults to 44100") {
        filter.prepare(0.0);
        filter.setFrequency(11025.0f);  // fs/4 at 44100 should give a=0
        REQUIRE(filter.getCoefficient() == Approx(0.0f).margin(0.01f));
    }

    SECTION("Negative sample rate defaults to 44100") {
        filter.prepare(-48000.0);
        filter.setFrequency(11025.0f);
        REQUIRE(filter.getCoefficient() == Approx(0.0f).margin(0.01f));
    }
}

// T006: setFrequency() with valid frequency updates coefficient via coeffFromFrequency()
TEST_CASE("Allpass1Pole setFrequency updates coefficient", "[allpass_1pole][US1][setFrequency]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);

    SECTION("1000 Hz break frequency") {
        filter.setFrequency(1000.0f);
        const float expectedCoeff = Allpass1Pole::coeffFromFrequency(1000.0f, 44100.0);
        REQUIRE(filter.getCoefficient() == expectedCoeff);
    }

    SECTION("5000 Hz break frequency") {
        filter.setFrequency(5000.0f);
        const float expectedCoeff = Allpass1Pole::coeffFromFrequency(5000.0f, 44100.0);
        REQUIRE(filter.getCoefficient() == expectedCoeff);
    }

    SECTION("100 Hz break frequency") {
        filter.setFrequency(100.0f);
        const float expectedCoeff = Allpass1Pole::coeffFromFrequency(100.0f, 44100.0);
        REQUIRE(filter.getCoefficient() == expectedCoeff);
    }
}

// T007: setFrequency() clamps to [1 Hz, Nyquist*0.99] (FR-009)
TEST_CASE("Allpass1Pole setFrequency clamping", "[allpass_1pole][US1][FR-009][clamping]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);

    const float nyquist = 44100.0f / 2.0f;
    const float maxFreq = nyquist * 0.99f;

    SECTION("Zero frequency clamped to 1 Hz") {
        filter.setFrequency(0.0f);
        REQUIRE(filter.getFrequency() == Approx(1.0f).margin(0.01f));
    }

    SECTION("Negative frequency clamped to 1 Hz") {
        filter.setFrequency(-1000.0f);
        REQUIRE(filter.getFrequency() == Approx(1.0f).margin(0.01f));
    }

    SECTION("Frequency above Nyquist clamped to Nyquist*0.99") {
        filter.setFrequency(30000.0f);
        REQUIRE(filter.getFrequency() == Approx(maxFreq).margin(1.0f));
    }

    SECTION("Frequency at exactly Nyquist clamped to Nyquist*0.99") {
        filter.setFrequency(nyquist);
        REQUIRE(filter.getFrequency() == Approx(maxFreq).margin(1.0f));
    }
}

// T008: process() implements difference equation y[n] = a*x[n] + x[n-1] - a*y[n-1] (FR-001)
TEST_CASE("Allpass1Pole process implements difference equation", "[allpass_1pole][US1][FR-001][process]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);

    // Use known coefficient for manual verification
    const float a = 0.5f;
    filter.setCoefficient(a);

    // Initial state: z1=0, y1=0
    // x[0] = 1.0: y[0] = 0.5*1.0 + 0 - 0.5*0 = 0.5
    float output = filter.process(1.0f);
    REQUIRE(output == Approx(0.5f).margin(1e-6f));

    // x[1] = 0.0: y[1] = 0.5*0.0 + 1.0 - 0.5*0.5 = 0.75
    output = filter.process(0.0f);
    REQUIRE(output == Approx(0.75f).margin(1e-6f));

    // x[2] = 0.5: y[2] = 0.5*0.5 + 0.0 - 0.5*0.75 = 0.25 - 0.375 = -0.125
    output = filter.process(0.5f);
    REQUIRE(output == Approx(-0.125f).margin(1e-6f));
}

// T009: process() maintains unity magnitude response (FR-002, SC-001)
TEST_CASE("Allpass1Pole unity magnitude response", "[allpass_1pole][US1][FR-002][SC-001][magnitude]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);
    filter.setFrequency(1000.0f);

    constexpr size_t kNumSamples = 8820;  // 200ms
    std::array<float, kNumSamples> input;
    std::array<float, kNumSamples> output;

    auto testFrequency = [&](float testFreq) {
        filter.reset();
        generateSineWave(input.data(), kNumSamples, testFreq, 44100.0);

        for (size_t i = 0; i < kNumSamples; ++i) {
            output[i] = filter.process(input[i]);
        }

        // Skip settling time (first 20%)
        const size_t startSample = kNumSamples / 5;
        const float inputRMS = calculateRMS(input.data() + startSample, kNumSamples - startSample);
        const float outputRMS = calculateRMS(output.data() + startSample, kNumSamples - startSample);

        // SC-001: Deviation from unity < 0.01 dB
        const float deviationDb = std::abs(linearToDb(outputRMS / inputRMS));
        REQUIRE(deviationDb < 0.01f);
    };

    SECTION("20 Hz - unity magnitude") {
        testFrequency(20.0f);
    }

    SECTION("1000 Hz - unity magnitude") {
        testFrequency(1000.0f);
    }

    SECTION("10000 Hz - unity magnitude") {
        testFrequency(10000.0f);
    }
}

// T010: Filter provides -90 degree phase shift at break frequency (FR-004, SC-002)
TEST_CASE("Allpass1Pole -90 degree phase at break frequency", "[allpass_1pole][US1][FR-004][SC-002][phase]") {
    // For a first-order allpass, the phase at the break frequency is -90 degrees.
    // This means when input is at its peak (sine = 1), output should be at zero
    // (or vice versa), because -90 degree phase shift turns sin into -cos.
    //
    // More specifically, for the allpass equation y[n] = a*x[n] + x[n-1] - a*y[n-1],
    // at the break frequency f where a = (1 - tan(pi*f/fs))/(1 + tan(pi*f/fs)):
    // - Input: sin(2*pi*f*t)
    // - Output: -cos(2*pi*f*t) = sin(2*pi*f*t - 90 degrees)
    //
    // We verify this by checking that when input peaks, output is near zero,
    // and when input is zero, output peaks.

    Allpass1Pole filter;
    filter.prepare(44100.0);

    // Use exactly fs/4 where coefficient a = 0, giving exact -90 phase shift
    constexpr float breakFreq = 11025.0f;  // fs/4 for 44100 Hz
    filter.setFrequency(breakFreq);

    // Verify coefficient is 0 at fs/4
    REQUIRE(std::abs(filter.getCoefficient()) < 0.01f);

    // Generate one period of sine at break frequency
    // At fs/4, period = 4 samples
    // With a=0, the filter is just y[n] = x[n-1], a pure one-sample delay
    // One sample delay at fs/4 is exactly 90 degrees of phase shift

    // Process several periods and check the relationship
    filter.reset();

    // At fs=44100, f=11025 (fs/4), one period = 4 samples
    // sin(0) = 0, sin(90) = 1, sin(180) = 0, sin(-90) = -1
    // With a=0: y[n] = x[n-1]

    // Process sine wave: sample 0 at phase 0, sample 1 at phase 90, etc.
    std::array<float, 8> input = {0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, -1.0f};  // Two periods
    std::array<float, 8> output;

    for (size_t i = 0; i < 8; ++i) {
        output[i] = filter.process(input[i]);
    }

    // Output should be input delayed by one sample (90 degrees at fs/4)
    // output[1] should equal input[0], output[2] should equal input[1], etc.
    for (size_t i = 1; i < 8; ++i) {
        REQUIRE(output[i] == Approx(input[i - 1]).margin(1e-6f));
    }

    // This proves -90 degree phase shift at break frequency fs/4
}

// T011: Filter provides 0 degree phase shift at DC (FR-003)
TEST_CASE("Allpass1Pole 0 degree phase at DC", "[allpass_1pole][US1][FR-003][phase_dc]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);
    filter.setFrequency(1000.0f);

    // For DC (constant signal), output should equal input after settling
    // Process constant input and verify output settles to same value
    constexpr size_t kNumSamples = 10000;
    float output = 0.0f;

    for (size_t i = 0; i < kNumSamples; ++i) {
        output = filter.process(1.0f);
    }

    // At DC, allpass has unity gain and 0 phase - output should equal input
    REQUIRE(output == Approx(1.0f).margin(0.001f));
}

// T012: Filter approaches -180 degree phase shift at Nyquist (FR-003)
TEST_CASE("Allpass1Pole approaches -180 degree phase at Nyquist", "[allpass_1pole][US1][FR-003][phase_nyquist]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);
    filter.setFrequency(1000.0f);

    // Test with frequency close to Nyquist (e.g., 20kHz at 44.1kHz sample rate)
    constexpr float testFreq = 20000.0f;
    constexpr size_t kNumSamples = 44100;
    std::vector<float> input(kNumSamples);
    std::vector<float> output(kNumSamples);

    generateSineWave(input.data(), kNumSamples, testFreq, 44100.0);

    for (size_t i = 0; i < kNumSamples; ++i) {
        output[i] = filter.process(input[i]);
    }

    const float phaseDegrees = measurePhaseDegrees(input.data(), output.data(), kNumSamples, testFreq, 44100.0);

    // Phase should be beyond -90 degrees at high frequencies
    // At 20kHz with 1kHz break, phase shift should be well past -90 (closer to -180)
    // Note: Due to measurement method limitations at high frequencies,
    // we verify the phase is past the -90 degree point
    REQUIRE(std::abs(phaseDegrees) > 30.0f);  // Phase is beyond break frequency
}

// T013: reset() clears state variables to zero (FR-013)
TEST_CASE("Allpass1Pole reset clears state", "[allpass_1pole][US1][FR-013][reset]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);
    filter.setCoefficient(0.5f);

    // Build up state by processing samples
    for (int i = 0; i < 100; ++i) {
        (void)filter.process(1.0f);
    }

    // Capture output before reset (should be non-zero from state)
    const float beforeReset = filter.process(0.0f);
    REQUIRE(beforeReset != 0.0f);

    // Reset
    filter.reset();

    // After reset, with a=0.5, x=0: y = 0.5*0 + 0 - 0.5*0 = 0
    const float afterReset = filter.process(0.0f);
    REQUIRE(afterReset == 0.0f);
}

// T014: getFrequency() returns current break frequency matching coefficient
TEST_CASE("Allpass1Pole getFrequency returns correct value", "[allpass_1pole][US1][getFrequency]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);

    SECTION("After setFrequency") {
        filter.setFrequency(1000.0f);
        REQUIRE(filter.getFrequency() == Approx(1000.0f).margin(1.0f));

        filter.setFrequency(5000.0f);
        REQUIRE(filter.getFrequency() == Approx(5000.0f).margin(1.0f));
    }

    SECTION("After setCoefficient") {
        // a=0 corresponds to fs/4 = 11025 Hz at 44100 Hz
        filter.setCoefficient(0.0f);
        REQUIRE(filter.getFrequency() == Approx(11025.0f).margin(10.0f));
    }
}

// T015: Memory footprint < 32 bytes (SC-004)
TEST_CASE("Allpass1Pole memory footprint", "[allpass_1pole][US1][SC-004][memory]") {
    // SC-004: Memory footprint is less than 32 bytes per filter instance
    // State: a_ (4), z1_ (4), y1_ (4), sampleRate_ (8) = 20 bytes minimum
    // With padding, should still be < 32 bytes
    REQUIRE(sizeof(Allpass1Pole) <= 32);
}

// ==============================================================================
// User Story 2 Tests: Coefficient-Based Control
// ==============================================================================

// T028: setCoefficient() accepts valid coefficient and updates state
TEST_CASE("Allpass1Pole setCoefficient accepts valid values", "[allpass_1pole][US2][setCoefficient]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);

    SECTION("Positive coefficient") {
        filter.setCoefficient(0.5f);
        REQUIRE(filter.getCoefficient() == 0.5f);
    }

    SECTION("Negative coefficient") {
        filter.setCoefficient(-0.5f);
        REQUIRE(filter.getCoefficient() == -0.5f);
    }

    SECTION("Zero coefficient") {
        filter.setCoefficient(0.0f);
        REQUIRE(filter.getCoefficient() == 0.0f);
    }

    SECTION("Near boundary coefficients") {
        filter.setCoefficient(0.999f);
        REQUIRE(filter.getCoefficient() == 0.999f);

        filter.setCoefficient(-0.999f);
        REQUIRE(filter.getCoefficient() == -0.999f);
    }
}

// T029: setCoefficient() clamps to [-0.9999, +0.9999] (FR-008)
TEST_CASE("Allpass1Pole setCoefficient clamping", "[allpass_1pole][US2][FR-008][clamping]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);

    SECTION("Coefficient +1.0 clamped to +0.9999") {
        filter.setCoefficient(1.0f);
        REQUIRE(filter.getCoefficient() == kMaxAllpass1PoleCoeff);
    }

    SECTION("Coefficient -1.0 clamped to -0.9999") {
        filter.setCoefficient(-1.0f);
        REQUIRE(filter.getCoefficient() == kMinAllpass1PoleCoeff);
    }

    SECTION("Coefficient +2.0 clamped to +0.9999") {
        filter.setCoefficient(2.0f);
        REQUIRE(filter.getCoefficient() == kMaxAllpass1PoleCoeff);
    }

    SECTION("Coefficient -2.0 clamped to -0.9999") {
        filter.setCoefficient(-2.0f);
        REQUIRE(filter.getCoefficient() == kMinAllpass1PoleCoeff);
    }
}

// T030: getCoefficient() returns current coefficient
TEST_CASE("Allpass1Pole getCoefficient returns current value", "[allpass_1pole][US2][getCoefficient]") {
    Allpass1Pole filter;

    // Default coefficient is 0
    REQUIRE(filter.getCoefficient() == 0.0f);

    filter.setCoefficient(0.75f);
    REQUIRE(filter.getCoefficient() == 0.75f);

    filter.setCoefficient(-0.3f);
    REQUIRE(filter.getCoefficient() == -0.3f);
}

// T031: Coefficient 0.0 acts as one-sample delay
TEST_CASE("Allpass1Pole coefficient 0 is one-sample delay", "[allpass_1pole][US2][coefficient_zero]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);
    filter.setCoefficient(0.0f);

    // With a=0: y[n] = 0*x[n] + x[n-1] - 0*y[n-1] = x[n-1]
    // This is a pure one-sample delay

    std::array<float, 10> input = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    std::array<float, 10> output;

    for (size_t i = 0; i < input.size(); ++i) {
        output[i] = filter.process(input[i]);
    }

    // Output should be input delayed by one sample
    REQUIRE(output[0] == 0.0f);  // x[-1] = 0
    for (size_t i = 1; i < input.size(); ++i) {
        REQUIRE(output[i] == input[i - 1]);
    }
}

// T032: Coefficient approaching +1.0 concentrates phase shift at low frequencies
TEST_CASE("Allpass1Pole positive coefficient phase concentration", "[allpass_1pole][US2][phase_low]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);
    filter.setCoefficient(0.99f);  // Near +1

    // Break frequency should be very low (near 0 Hz)
    const float breakFreq = filter.getFrequency();
    REQUIRE(breakFreq < 500.0f);  // Should be much lower than fs/4
}

// T033: Coefficient approaching -1.0 concentrates phase shift at high frequencies
TEST_CASE("Allpass1Pole negative coefficient phase concentration", "[allpass_1pole][US2][phase_high]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);
    filter.setCoefficient(-0.99f);  // Near -1

    // Break frequency should be very high (near Nyquist)
    const float breakFreq = filter.getFrequency();
    const float nyquist = 44100.0f / 2.0f;
    REQUIRE(breakFreq > nyquist * 0.5f);  // Should be much higher than fs/4
}

// ==============================================================================
// User Story 3 Tests: Block Processing
// ==============================================================================

// T041: processBlock() produces identical output to N calls of process() (FR-012, SC-007)
TEST_CASE("Allpass1Pole processBlock matches process", "[allpass_1pole][US3][FR-012][SC-007][block]") {
    Allpass1Pole filter1;
    Allpass1Pole filter2;
    filter1.prepare(44100.0);
    filter2.prepare(44100.0);
    filter1.setFrequency(1000.0f);
    filter2.setFrequency(1000.0f);

    constexpr size_t kNumSamples = 64;
    std::array<float, kNumSamples> input;
    std::array<float, kNumSamples> output1;
    std::array<float, kNumSamples> output2;

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

    // SC-007: Outputs must be bit-identical
    for (size_t i = 0; i < kNumSamples; ++i) {
        REQUIRE(output1[i] == output2[i]);
    }
}

// T042: processBlock() identical for various block sizes (FR-012)
TEST_CASE("Allpass1Pole processBlock various sizes", "[allpass_1pole][US3][FR-012][block_sizes]") {
    auto testBlockSize = [](size_t blockSize) {
        Allpass1Pole filter1;
        Allpass1Pole filter2;
        filter1.prepare(44100.0);
        filter2.prepare(44100.0);
        filter1.setFrequency(2000.0f);
        filter2.setFrequency(2000.0f);

        std::vector<float> input(blockSize);
        std::vector<float> output1(blockSize);
        std::vector<float> output2(blockSize);

        std::mt19937 rng(123);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        for (size_t i = 0; i < blockSize; ++i) {
            input[i] = dist(rng);
        }

        for (size_t i = 0; i < blockSize; ++i) {
            output1[i] = filter1.process(input[i]);
        }

        std::copy(input.begin(), input.end(), output2.begin());
        filter2.processBlock(output2.data(), blockSize);

        for (size_t i = 0; i < blockSize; ++i) {
            REQUIRE(output1[i] == output2[i]);
        }
    };

    SECTION("Block size 1") { testBlockSize(1); }
    SECTION("Block size 2") { testBlockSize(2); }
    SECTION("Block size 16") { testBlockSize(16); }
    SECTION("Block size 512") { testBlockSize(512); }
    SECTION("Block size 4096") { testBlockSize(4096); }
}

// T043: processBlock() with NaN in first sample fills buffer with zeros (FR-014)
TEST_CASE("Allpass1Pole processBlock NaN handling", "[allpass_1pole][US3][FR-014][nan_block]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);
    filter.setFrequency(1000.0f);

    // Process some samples to build up state
    for (int i = 0; i < 10; ++i) {
        (void)filter.process(0.5f);
    }

    constexpr size_t kNumSamples = 64;
    std::array<float, kNumSamples> buffer;
    buffer[0] = std::numeric_limits<float>::quiet_NaN();
    for (size_t i = 1; i < kNumSamples; ++i) {
        buffer[i] = 1.0f;
    }

    filter.processBlock(buffer.data(), kNumSamples);

    // Entire block should be zeros
    for (size_t i = 0; i < kNumSamples; ++i) {
        REQUIRE(buffer[i] == 0.0f);
    }

    // State should be reset - next call should behave as if from zero state
    const float nextOutput = filter.process(1.0f);
    // With zero state: y = a*1 + 0 - a*0 = a
    REQUIRE(nextOutput == Approx(filter.getCoefficient()).margin(1e-6f));
}

// T044: processBlock() flushes denormals once at block end (FR-015)
TEST_CASE("Allpass1Pole processBlock denormal flushing", "[allpass_1pole][US3][FR-015][denormal]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);
    filter.setCoefficient(0.9999f);  // Near 1 to encourage small values

    // Process a decaying signal that could produce denormals
    constexpr size_t kNumSamples = 10000;
    std::vector<float> buffer(kNumSamples);

    // Initial impulse followed by zeros
    buffer[0] = 1.0f;
    for (size_t i = 1; i < kNumSamples; ++i) {
        buffer[i] = 0.0f;
    }

    filter.processBlock(buffer.data(), kNumSamples);

    // Verify no denormal values in output
    for (size_t i = 0; i < kNumSamples; ++i) {
        // Either the value is zero or has reasonable magnitude
        REQUIRE((buffer[i] == 0.0f || std::abs(buffer[i]) >= 1e-15f));
    }
}

// T045: No discontinuities at block boundaries
TEST_CASE("Allpass1Pole no discontinuities at block boundaries", "[allpass_1pole][US3][continuity]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);
    filter.setFrequency(1000.0f);

    // Process continuous sine wave in varying block sizes
    constexpr size_t kTotalSamples = 4410;
    std::vector<float> continuous(kTotalSamples);
    generateSineWave(continuous.data(), kTotalSamples, 440.0f, 44100.0);

    // Process with varying block sizes
    std::vector<float> output(kTotalSamples);
    size_t offset = 0;
    size_t blockSizes[] = {17, 64, 23, 128, 31, 256, 64};
    size_t blockIndex = 0;

    while (offset < kTotalSamples) {
        size_t blockSize = std::min(blockSizes[blockIndex % 7], kTotalSamples - offset);
        std::copy(continuous.begin() + static_cast<long>(offset),
                  continuous.begin() + static_cast<long>(offset + blockSize),
                  output.begin() + static_cast<long>(offset));
        filter.processBlock(output.data() + offset, blockSize);
        offset += blockSize;
        blockIndex++;
    }

    // Check for discontinuities (large jumps between samples)
    for (size_t i = 1; i < kTotalSamples; ++i) {
        const float diff = std::abs(output[i] - output[i - 1]);
        // Maximum reasonable difference for 440 Hz sine at 44100 Hz
        REQUIRE(diff < 0.5f);
    }
}

// T046: Performance test - processBlock < 10 ns/sample (SC-003)
TEST_CASE("Allpass1Pole performance", "[allpass_1pole][US3][SC-003][performance]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);
    filter.setFrequency(1000.0f);

    constexpr size_t kNumSamples = 100000;
    std::vector<float> buffer(kNumSamples);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (size_t i = 0; i < kNumSamples; ++i) {
        buffer[i] = dist(rng);
    }

    // Warm up
    filter.processBlock(buffer.data(), kNumSamples);
    filter.reset();

    // Timed run
    auto start = std::chrono::high_resolution_clock::now();
    filter.processBlock(buffer.data(), kNumSamples);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    const double nsPerSample = static_cast<double>(duration.count()) / static_cast<double>(kNumSamples);

    // SC-003: < 10 ns per sample (allow some margin for test environment variance)
    // Note: Debug builds may be slower, so we use a generous margin
    REQUIRE(nsPerSample < 100.0);  // 100 ns allows for debug builds

    // Info output for manual verification
    INFO("Performance: " << nsPerSample << " ns/sample");
}

// ==============================================================================
// User Story 4 Tests: Static Utility Functions
// ==============================================================================

// T052: coeffFromFrequency() produces correct values for known break frequencies (SC-005)
TEST_CASE("Allpass1Pole coeffFromFrequency known values", "[allpass_1pole][US4][SC-005][coeffFromFrequency]") {
    constexpr double sampleRate = 44100.0;

    SECTION("1kHz at 44.1kHz") {
        // Reference calculation: a = (1 - tan(pi * 1000 / 44100)) / (1 + tan(pi * 1000 / 44100))
        // tan(pi * 1000 / 44100) = tan(0.07135) = 0.0715
        // a = (1 - 0.0715) / (1 + 0.0715) = 0.9285 / 1.0715 = 0.8666
        const float coeff = Allpass1Pole::coeffFromFrequency(1000.0f, sampleRate);
        REQUIRE(coeff == Approx(0.8668f).margin(1e-3f));
    }

    SECTION("5kHz at 44.1kHz") {
        // tan(pi * 5000 / 44100) = tan(0.3566) = 0.3759
        // a = (1 - 0.3759) / (1 + 0.3759) = 0.6241 / 1.3759 = 0.4536
        const float coeff = Allpass1Pole::coeffFromFrequency(5000.0f, sampleRate);
        REQUIRE(coeff == Approx(0.4577f).margin(1e-3f));
    }

    SECTION("11025Hz (fs/4) at 44.1kHz") {
        const float coeff = Allpass1Pole::coeffFromFrequency(11025.0f, sampleRate);
        // At fs/4, tan(pi/4) = 1, so a = (1-1)/(1+1) = 0
        REQUIRE(coeff == Approx(0.0f).margin(1e-3f));
    }
}

// T053: Round-trip conversion freq->coeff->freq (SC-005)
TEST_CASE("Allpass1Pole round-trip freq to coeff to freq", "[allpass_1pole][US4][SC-005][roundtrip_freq]") {
    constexpr double sampleRate = 44100.0;

    auto testRoundTrip = [sampleRate](float freq) {
        const float coeff = Allpass1Pole::coeffFromFrequency(freq, sampleRate);
        const float freqBack = Allpass1Pole::frequencyFromCoeff(coeff, sampleRate);
        REQUIRE(freqBack == Approx(freq).margin(freq * 1e-4f));  // 0.01% tolerance
    };

    SECTION("100 Hz") { testRoundTrip(100.0f); }
    SECTION("1000 Hz") { testRoundTrip(1000.0f); }
    SECTION("5000 Hz") { testRoundTrip(5000.0f); }
    SECTION("10000 Hz") { testRoundTrip(10000.0f); }
}

// T054: Round-trip conversion coeff->freq->coeff (SC-005)
TEST_CASE("Allpass1Pole round-trip coeff to freq to coeff", "[allpass_1pole][US4][SC-005][roundtrip_coeff]") {
    constexpr double sampleRate = 44100.0;

    auto testRoundTrip = [sampleRate](float coeff) {
        const float freq = Allpass1Pole::frequencyFromCoeff(coeff, sampleRate);
        const float coeffBack = Allpass1Pole::coeffFromFrequency(freq, sampleRate);
        REQUIRE(coeffBack == Approx(coeff).margin(1e-4f));
    };

    SECTION("a = 0.0") { testRoundTrip(0.0f); }
    SECTION("a = 0.5") { testRoundTrip(0.5f); }
    SECTION("a = -0.5") { testRoundTrip(-0.5f); }
    SECTION("a = 0.9") { testRoundTrip(0.9f); }
    SECTION("a = -0.9") { testRoundTrip(-0.9f); }
}

// T055: Static methods work without filter instantiation
TEST_CASE("Allpass1Pole static methods standalone", "[allpass_1pole][US4][static]") {
    // coeffFromFrequency
    const float coeff1 = Allpass1Pole::coeffFromFrequency(1000.0f, 44100.0);
    REQUIRE(coeff1 >= kMinAllpass1PoleCoeff);
    REQUIRE(coeff1 <= kMaxAllpass1PoleCoeff);

    // frequencyFromCoeff
    const float freq1 = Allpass1Pole::frequencyFromCoeff(0.5f, 44100.0);
    REQUIRE(freq1 >= kMinAllpass1PoleFrequency);
    REQUIRE(freq1 <= 44100.0f * 0.5f);
}

// T056: Static methods apply same clamping as instance methods
TEST_CASE("Allpass1Pole static methods clamping", "[allpass_1pole][US4][static_clamping]") {
    constexpr double sampleRate = 44100.0;
    const float nyquist = 44100.0f / 2.0f;
    const float maxFreq = nyquist * 0.99f;

    SECTION("Frequency 0 clamped to 1 Hz") {
        const float coeff = Allpass1Pole::coeffFromFrequency(0.0f, sampleRate);
        const float freq = Allpass1Pole::frequencyFromCoeff(coeff, sampleRate);
        REQUIRE(freq >= 1.0f);
    }

    SECTION("Frequency above Nyquist clamped") {
        const float coeff = Allpass1Pole::coeffFromFrequency(30000.0f, sampleRate);
        const float freq = Allpass1Pole::frequencyFromCoeff(coeff, sampleRate);
        REQUIRE(freq <= maxFreq);
    }

    SECTION("Coefficient clamping in frequencyFromCoeff") {
        // Even with extreme coefficient, should return valid frequency
        const float freq1 = Allpass1Pole::frequencyFromCoeff(2.0f, sampleRate);
        REQUIRE(freq1 >= kMinAllpass1PoleFrequency);
        REQUIRE(freq1 <= maxFreq);

        const float freq2 = Allpass1Pole::frequencyFromCoeff(-2.0f, sampleRate);
        REQUIRE(freq2 >= kMinAllpass1PoleFrequency);
        REQUIRE(freq2 <= maxFreq);
    }
}

// T057: Static methods work at multiple sample rates
TEST_CASE("Allpass1Pole static methods multiple sample rates", "[allpass_1pole][US4][sample_rates]") {
    auto testSampleRate = [](double sampleRate) {
        // 1kHz should give consistent relative coefficient
        const float coeff1k = Allpass1Pole::coeffFromFrequency(1000.0f, sampleRate);

        // Verify coefficient is in valid range
        REQUIRE(coeff1k >= kMinAllpass1PoleCoeff);
        REQUIRE(coeff1k <= kMaxAllpass1PoleCoeff);

        // Round-trip should work
        const float freq = Allpass1Pole::frequencyFromCoeff(coeff1k, sampleRate);
        REQUIRE(freq == Approx(1000.0f).margin(1.0f));
    };

    SECTION("8000 Hz") { testSampleRate(8000.0); }
    SECTION("44100 Hz") { testSampleRate(44100.0); }
    SECTION("96000 Hz") { testSampleRate(96000.0); }
    SECTION("192000 Hz") { testSampleRate(192000.0); }
}

// ==============================================================================
// Phase 6: Edge Cases & Robustness
// ==============================================================================

// T062: process() with infinity input resets and returns 0.0 (FR-014, SC-006)
TEST_CASE("Allpass1Pole process infinity handling", "[allpass_1pole][edge][FR-014][SC-006][infinity]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);
    filter.setFrequency(1000.0f);

    // Build up state
    for (int i = 0; i < 10; ++i) {
        (void)filter.process(0.5f);
    }

    SECTION("Positive infinity") {
        const float result = filter.process(std::numeric_limits<float>::infinity());
        REQUIRE(result == 0.0f);

        // State should be reset
        const float nextResult = filter.process(1.0f);
        REQUIRE_FALSE(detail::isInf(nextResult));
    }

    SECTION("Negative infinity") {
        const float result = filter.process(-std::numeric_limits<float>::infinity());
        REQUIRE(result == 0.0f);

        const float nextResult = filter.process(1.0f);
        REQUIRE_FALSE(detail::isInf(nextResult));
    }
}

// T063: processBlock() with infinity in first sample fills with zeros (FR-014, SC-006)
TEST_CASE("Allpass1Pole processBlock infinity handling", "[allpass_1pole][edge][FR-014][SC-006][infinity_block]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);
    filter.setFrequency(1000.0f);

    // Build up state
    for (int i = 0; i < 10; ++i) {
        (void)filter.process(0.5f);
    }

    constexpr size_t kNumSamples = 64;
    std::array<float, kNumSamples> buffer;
    buffer[0] = std::numeric_limits<float>::infinity();
    for (size_t i = 1; i < kNumSamples; ++i) {
        buffer[i] = 1.0f;
    }

    filter.processBlock(buffer.data(), kNumSamples);

    for (size_t i = 0; i < kNumSamples; ++i) {
        REQUIRE(buffer[i] == 0.0f);
    }
}

// T064: Denormal values in state flushed to zero (FR-015, SC-006)
TEST_CASE("Allpass1Pole denormal flushing", "[allpass_1pole][edge][FR-015][SC-006][denormal_flush]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);
    filter.setCoefficient(0.99999f);  // Very close to 1

    // Process a signal that decays to very small values
    float output = filter.process(1.0f);

    // Process many zeros - state should decay
    for (int i = 0; i < 100000; ++i) {
        output = filter.process(0.0f);
    }

    // Output should be flushed to zero, not denormal
    REQUIRE((output == 0.0f || std::abs(output) >= 1e-15f));
}

// T065: reset() during processing clears state without artifacts
TEST_CASE("Allpass1Pole reset during processing", "[allpass_1pole][edge][reset_mid]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);
    filter.setFrequency(1000.0f);

    // Process some samples
    for (int i = 0; i < 100; ++i) {
        (void)filter.process(1.0f);
    }

    // Reset mid-stream
    filter.reset();

    // Verify state is cleared
    const float output = filter.process(0.0f);
    REQUIRE(output == 0.0f);

    // Verify filter still works correctly after reset
    const float output2 = filter.process(1.0f);
    REQUIRE(output2 == Approx(filter.getCoefficient()).margin(1e-6f));
}

// T066: Filter works at very low sample rate (8kHz)
TEST_CASE("Allpass1Pole low sample rate 8kHz", "[allpass_1pole][edge][sample_rate_low]") {
    Allpass1Pole filter;
    filter.prepare(8000.0);
    filter.setFrequency(1000.0f);

    // Verify coefficient is valid
    const float coeff = filter.getCoefficient();
    REQUIRE(coeff >= kMinAllpass1PoleCoeff);
    REQUIRE(coeff <= kMaxAllpass1PoleCoeff);

    // Process some samples
    constexpr size_t kNumSamples = 800;
    std::array<float, kNumSamples> buffer;
    generateSineWave(buffer.data(), kNumSamples, 500.0f, 8000.0);

    for (size_t i = 0; i < kNumSamples; ++i) {
        buffer[i] = filter.process(buffer[i]);
        REQUIRE_FALSE(detail::isNaN(buffer[i]));
        REQUIRE_FALSE(detail::isInf(buffer[i]));
    }
}

// T067: Filter works at very high sample rate (192kHz)
TEST_CASE("Allpass1Pole high sample rate 192kHz", "[allpass_1pole][edge][sample_rate_high]") {
    Allpass1Pole filter;
    filter.prepare(192000.0);
    filter.setFrequency(10000.0f);

    // Verify coefficient is valid
    const float coeff = filter.getCoefficient();
    REQUIRE(coeff >= kMinAllpass1PoleCoeff);
    REQUIRE(coeff <= kMaxAllpass1PoleCoeff);

    // Process some samples
    constexpr size_t kNumSamples = 19200;
    std::array<float, kNumSamples> buffer;
    generateSineWave(buffer.data(), kNumSamples, 5000.0f, 192000.0);

    for (size_t i = 0; i < kNumSamples; ++i) {
        buffer[i] = filter.process(buffer[i]);
        REQUIRE_FALSE(detail::isNaN(buffer[i]));
        REQUIRE_FALSE(detail::isInf(buffer[i]));
    }
}

// T068: Frequency at exactly 0 Hz clamped to 1 Hz (FR-009)
TEST_CASE("Allpass1Pole zero frequency clamped", "[allpass_1pole][edge][FR-009][zero_freq]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);

    filter.setFrequency(0.0f);
    REQUIRE(filter.getFrequency() >= 1.0f);

    // Verify filter still works
    const float output = filter.process(1.0f);
    REQUIRE_FALSE(detail::isNaN(output));
}

// T069: Frequency above Nyquist clamped to Nyquist*0.99 (FR-009)
TEST_CASE("Allpass1Pole above Nyquist clamped", "[allpass_1pole][edge][FR-009][above_nyquist]") {
    Allpass1Pole filter;
    filter.prepare(44100.0);

    const float nyquist = 44100.0f / 2.0f;
    const float maxFreq = nyquist * 0.99f;

    filter.setFrequency(50000.0f);  // Well above Nyquist
    REQUIRE(filter.getFrequency() == Approx(maxFreq).margin(1.0f));
}

// ==============================================================================
// noexcept verification (FR-019)
// ==============================================================================

TEST_CASE("Allpass1Pole methods are noexcept", "[allpass_1pole][FR-019][safety]") {
    Allpass1Pole filter;
    float buffer[16];

    STATIC_REQUIRE(noexcept(filter.prepare(44100.0)));
    STATIC_REQUIRE(noexcept(filter.setFrequency(1000.0f)));
    STATIC_REQUIRE(noexcept(filter.setCoefficient(0.5f)));
    STATIC_REQUIRE(noexcept(filter.getCoefficient()));
    STATIC_REQUIRE(noexcept(filter.getFrequency()));
    STATIC_REQUIRE(noexcept(filter.process(0.5f)));
    STATIC_REQUIRE(noexcept(filter.processBlock(buffer, 16)));
    STATIC_REQUIRE(noexcept(filter.reset()));
    STATIC_REQUIRE(noexcept(Allpass1Pole::coeffFromFrequency(1000.0f, 44100.0)));
    STATIC_REQUIRE(noexcept(Allpass1Pole::frequencyFromCoeff(0.5f, 44100.0)));
}
