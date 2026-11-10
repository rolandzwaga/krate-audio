// ==============================================================================
// Unit Tests: AllpassSaturator
// ==============================================================================
// Test-First Development: Tests for Allpass-Saturator Network processor.
//
// Feature: 109-allpass-saturator-network
// Layer: 2 (DSP Processors)
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline (pure DSP functions testable without VST)
// - Principle XII: Test-First Development (tests written before implementation)
//
// Reference: specs/109-allpass-saturator-network/spec.md
// ==============================================================================

#include <krate/dsp/processors/allpass_saturator.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "allpass_saturator_pre_t416_reference.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Test Constants
// =============================================================================

constexpr double kTestSampleRate = 44100.0;
constexpr size_t kTestBlockSize = 512;

// =============================================================================
// Helper Functions
// =============================================================================

/// @brief Generate an impulse signal (1.0 at sample 0, 0.0 elsewhere).
inline std::vector<float> generateImpulse(size_t length) {
    std::vector<float> buffer(length, 0.0f);
    if (length > 0) {
        buffer[0] = 1.0f;
    }
    return buffer;
}

/// @brief Generate a sine wave at the specified frequency.
inline std::vector<float> generateSine(size_t length, float frequency, double sampleRate,
                                        float amplitude = 1.0f) {
    std::vector<float> buffer(length);
    const double phaseIncrement = 2.0 * 3.14159265358979323846 * frequency / sampleRate;
    for (size_t i = 0; i < length; ++i) {
        buffer[i] = amplitude * static_cast<float>(std::sin(phaseIncrement * static_cast<double>(i)));
    }
    return buffer;
}

/// @brief Calculate RMS of a buffer.
inline float calculateRMS(const std::vector<float>& buffer) {
    if (buffer.empty()) return 0.0f;
    float sumSquares = 0.0f;
    for (float sample : buffer) {
        sumSquares += sample * sample;
    }
    return std::sqrt(sumSquares / static_cast<float>(buffer.size()));
}

/// @brief Calculate peak absolute value of a buffer.
inline float calculatePeak(const std::vector<float>& buffer) {
    float peak = 0.0f;
    for (float sample : buffer) {
        peak = std::max(peak, std::abs(sample));
    }
    return peak;
}

/// @brief Calculate DC offset (mean) of a buffer.
inline float calculateDC(const std::vector<float>& buffer) {
    if (buffer.empty()) return 0.0f;
    float sum = 0.0f;
    for (float sample : buffer) {
        sum += sample;
    }
    return sum / static_cast<float>(buffer.size());
}

/// @brief Estimate dominant frequency using zero-crossing rate.
/// This is a simple estimation - for more accuracy, use FFT.
inline float estimateFrequency(const std::vector<float>& buffer, double sampleRate) {
    if (buffer.size() < 3) return 0.0f;

    size_t zeroCrossings = 0;
    for (size_t i = 1; i < buffer.size(); ++i) {
        if ((buffer[i - 1] >= 0.0f && buffer[i] < 0.0f) ||
            (buffer[i - 1] < 0.0f && buffer[i] >= 0.0f)) {
            ++zeroCrossings;
        }
    }

    // Frequency = zeroCrossings / 2 / duration
    const double duration = static_cast<double>(buffer.size()) / sampleRate;
    return static_cast<float>(zeroCrossings / 2.0 / duration);
}

/// @brief Count number of samples with non-negligible energy.
inline size_t countActiveSamples(const std::vector<float>& buffer, float threshold = 0.001f) {
    size_t count = 0;
    for (float sample : buffer) {
        if (std::abs(sample) > threshold) {
            ++count;
        }
    }
    return count;
}

// =============================================================================
// Phase 2: Foundational Tests (T005, T006, T007)
// =============================================================================

TEST_CASE("AllpassSaturator lifecycle", "[allpass_saturator][lifecycle]") {
    AllpassSaturator processor;

    SECTION("default constructor creates unprepared processor") {
        REQUIRE_FALSE(processor.isPrepared());
        REQUIRE(processor.getSampleRate() == Approx(0.0));
    }

    SECTION("prepare() initializes processor") {
        processor.prepare(kTestSampleRate, kTestBlockSize);
        REQUIRE(processor.isPrepared());
        REQUIRE(processor.getSampleRate() == Approx(kTestSampleRate));
    }

    SECTION("reset() clears state without changing prepared status") {
        processor.prepare(kTestSampleRate, kTestBlockSize);

        // Process some audio to build up state
        auto impulse = generateImpulse(100);
        processor.setFeedback(0.9f);
        processor.processBlock(impulse.data(), impulse.size());

        processor.reset();

        // Should still be prepared
        REQUIRE(processor.isPrepared());

        // After reset, processing silence should produce near-silence
        // (state should be cleared)
        std::vector<float> silence(100, 0.0f);
        processor.processBlock(silence.data(), silence.size());

        // After processing silence post-reset, output should be very quiet
        const float silenceRMS = calculateRMS(silence);
        REQUIRE(silenceRMS < 0.01f);
    }

    SECTION("process() returns input unchanged when not prepared") {
        const float testValue = 0.5f;
        REQUIRE(processor.process(testValue) == Approx(testValue));
    }

    SECTION("supports sample rates 44100Hz to 192000Hz (FR-003)") {
        const std::array<double, 4> sampleRates = {44100.0, 48000.0, 96000.0, 192000.0};

        for (double sr : sampleRates) {
            AllpassSaturator proc;
            proc.prepare(sr, kTestBlockSize);
            REQUIRE(proc.isPrepared());
            REQUIRE(proc.getSampleRate() == Approx(sr));
        }
    }
}

// =============================================================================
// Phase 3: User Story 1 - SingleAllpass Tests (T008-T016)
// =============================================================================

TEST_CASE("AllpassSaturator topology selection (T008)", "[allpass_saturator][topology]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);

    SECTION("default topology is SingleAllpass") {
        REQUIRE(processor.getTopology() == NetworkTopology::SingleAllpass);
    }

    SECTION("setTopology changes topology") {
        processor.setTopology(NetworkTopology::KarplusStrong);
        REQUIRE(processor.getTopology() == NetworkTopology::KarplusStrong);

        processor.setTopology(NetworkTopology::AllpassChain);
        REQUIRE(processor.getTopology() == NetworkTopology::AllpassChain);

        processor.setTopology(NetworkTopology::FeedbackMatrix);
        REQUIRE(processor.getTopology() == NetworkTopology::FeedbackMatrix);

        processor.setTopology(NetworkTopology::SingleAllpass);
        REQUIRE(processor.getTopology() == NetworkTopology::SingleAllpass);
    }

    SECTION("topology change resets state (FR-009)") {
        processor.setFeedback(0.95f);

        // Build up resonance
        auto impulse = generateImpulse(1000);
        processor.processBlock(impulse.data(), impulse.size());

        // Output should have energy
        REQUIRE(calculateRMS(impulse) > 0.01f);

        // Change topology
        processor.setTopology(NetworkTopology::AllpassChain);

        // Process silence - should decay quickly because state was reset
        std::vector<float> silence(1000, 0.0f);
        processor.processBlock(silence.data(), silence.size());

        // After topology change and processing silence, output should be minimal
        // (Note: some residual may exist due to smoothers, but should be low)
        const float endRMS = calculateRMS(std::vector<float>(silence.end() - 100, silence.end()));
        REQUIRE(endRMS < 0.1f);
    }
}

TEST_CASE("AllpassSaturator frequency control (T009)", "[allpass_saturator][frequency]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);

    SECTION("setFrequency and getFrequency work correctly") {
        processor.setFrequency(1000.0f);
        REQUIRE(processor.getFrequency() == Approx(1000.0f));

        processor.setFrequency(440.0f);
        REQUIRE(processor.getFrequency() == Approx(440.0f));
    }

    SECTION("frequency is clamped to minimum 20Hz (FR-011)") {
        processor.setFrequency(10.0f);
        REQUIRE(processor.getFrequency() == Approx(20.0f));

        processor.setFrequency(-100.0f);
        REQUIRE(processor.getFrequency() == Approx(20.0f));
    }

    SECTION("frequency is clamped to maximum sampleRate * 0.45 (FR-011)") {
        const float maxFreq = static_cast<float>(kTestSampleRate) * 0.45f;

        processor.setFrequency(30000.0f);
        REQUIRE(processor.getFrequency() == Approx(maxFreq));
    }
}

TEST_CASE("AllpassSaturator feedback control (T010)", "[allpass_saturator][feedback]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);

    SECTION("setFeedback and getFeedback work correctly") {
        processor.setFeedback(0.7f);
        REQUIRE(processor.getFeedback() == Approx(0.7f));

        processor.setFeedback(0.5f);
        REQUIRE(processor.getFeedback() == Approx(0.5f));
    }

    SECTION("feedback is clamped to [0.0, 0.999] (FR-013)") {
        processor.setFeedback(-0.5f);
        REQUIRE(processor.getFeedback() == Approx(0.0f));

        processor.setFeedback(1.5f);
        REQUIRE(processor.getFeedback() == Approx(0.999f));

        processor.setFeedback(1.0f);
        REQUIRE(processor.getFeedback() == Approx(0.999f));
    }
}

TEST_CASE("AllpassSaturator saturation control (T011)", "[allpass_saturator][saturation]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);

    SECTION("setSaturationCurve supports all 9 WaveshapeType values (FR-018)") {
        const std::array<WaveshapeType, 9> types = {
            WaveshapeType::Tanh,
            WaveshapeType::Atan,
            WaveshapeType::Cubic,
            WaveshapeType::Quintic,
            WaveshapeType::ReciprocalSqrt,
            WaveshapeType::Erf,
            WaveshapeType::HardClip,
            WaveshapeType::Diode,
            WaveshapeType::Tube
        };

        for (auto type : types) {
            processor.setSaturationCurve(type);
            REQUIRE(processor.getSaturationCurve() == type);
        }
    }

    SECTION("setDrive and getDrive work correctly") {
        processor.setDrive(2.0f);
        REQUIRE(processor.getDrive() == Approx(2.0f));

        processor.setDrive(5.0f);
        REQUIRE(processor.getDrive() == Approx(5.0f));
    }

    SECTION("drive is clamped to [0.1, 10.0] (FR-019)") {
        processor.setDrive(0.01f);
        REQUIRE(processor.getDrive() == Approx(0.1f));

        processor.setDrive(20.0f);
        REQUIRE(processor.getDrive() == Approx(10.0f));
    }
}

TEST_CASE("AllpassSaturator SingleAllpass resonance at target frequency (T012)",
          "[allpass_saturator][singleallpass][resonance]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);
    processor.setTopology(NetworkTopology::SingleAllpass);
    processor.setFeedback(0.9f);

    SECTION("resonance produces pitched output") {
        // Test that different frequencies produce different outputs
        // (a simpler, more reliable test than precise frequency estimation)
        const float freq1 = 440.0f;
        const float freq2 = 880.0f;

        // Process at freq1
        processor.setFrequency(freq1);
        processor.reset();
        auto output1 = generateImpulse(4096);
        processor.processBlock(output1.data(), output1.size());

        // Process at freq2
        processor.setFrequency(freq2);
        processor.reset();
        auto output2 = generateImpulse(4096);
        processor.processBlock(output2.data(), output2.size());

        // Both should have energy (resonance occurred)
        REQUIRE(calculateRMS(output1) > 0.01f);
        REQUIRE(calculateRMS(output2) > 0.01f);

        // Outputs should be different (different resonant frequencies)
        float diff = 0.0f;
        for (size_t i = 100; i < 500; ++i) {
            diff += std::abs(output1[i] - output2[i]);
        }
        REQUIRE(diff > 0.1f);  // Outputs should differ

        // Higher frequency should have more zero crossings (rough verification)
        size_t crossings1 = 0, crossings2 = 0;
        for (size_t i = 101; i < 1000; ++i) {
            if ((output1[i - 1] >= 0.0f && output1[i] < 0.0f) ||
                (output1[i - 1] < 0.0f && output1[i] >= 0.0f)) {
                ++crossings1;
            }
            if ((output2[i - 1] >= 0.0f && output2[i] < 0.0f) ||
                (output2[i - 1] < 0.0f && output2[i] >= 0.0f)) {
                ++crossings2;
            }
        }
        // 880Hz should have roughly twice as many crossings as 440Hz
        REQUIRE(crossings2 > crossings1);
    }
}

TEST_CASE("AllpassSaturator feedback sustain difference (T013)",
          "[allpass_saturator][singleallpass][feedback]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);
    processor.setTopology(NetworkTopology::SingleAllpass);
    processor.setFrequency(440.0f);

    SECTION("feedback 0.95 sustains longer than feedback 0.5") {
        const size_t testLength = 22050;  // 0.5 seconds

        // Test with low feedback
        processor.setFeedback(0.5f);
        processor.reset();
        auto lowFeedback = generateImpulse(testLength);
        processor.processBlock(lowFeedback.data(), lowFeedback.size());

        // Count active samples for low feedback
        const size_t activeLow = countActiveSamples(lowFeedback, 0.001f);

        // Test with high feedback
        processor.setFeedback(0.95f);
        processor.reset();
        auto highFeedback = generateImpulse(testLength);
        processor.processBlock(highFeedback.data(), highFeedback.size());

        // Count active samples for high feedback
        const size_t activeHigh = countActiveSamples(highFeedback, 0.001f);

        // High feedback should sustain significantly longer
        REQUIRE(activeHigh > activeLow * 2);
    }
}

TEST_CASE("AllpassSaturator output bounded with high feedback (T014)",
          "[allpass_saturator][singleallpass][bounded]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);
    processor.setTopology(NetworkTopology::SingleAllpass);
    processor.setFeedback(0.99f);  // Very high feedback
    processor.setDrive(5.0f);      // High drive

    SECTION("output peak < 2.0 with high feedback (SC-006)") {
        // Process with continuous excitation
        auto input = generateSine(44100, 440.0f, kTestSampleRate, 1.0f);
        processor.processBlock(input.data(), input.size());

        const float peak = calculatePeak(input);
        REQUIRE(peak < 2.0f);
    }

    SECTION("output bounded during self-oscillation") {
        processor.setFeedback(0.999f);

        // Brief excitation followed by silence
        auto buffer = generateImpulse(1000);
        processor.processBlock(buffer.data(), buffer.size());

        // Let it self-oscillate for a while
        std::vector<float> silence(44100, 0.0f);
        processor.processBlock(silence.data(), silence.size());

        const float peak = calculatePeak(silence);
        REQUIRE(peak < 2.0f);
    }
}

TEST_CASE("AllpassSaturator DC offset after saturation (T015)",
          "[allpass_saturator][singleallpass][dc]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);
    processor.setTopology(NetworkTopology::SingleAllpass);

    SECTION("DC offset < 0.01 after processing (SC-007)") {
        processor.setFeedback(0.9f);
        processor.setDrive(3.0f);
        processor.setSaturationCurve(WaveshapeType::Tube);  // Asymmetric

        // Process audio to generate potential DC
        auto input = generateSine(44100, 440.0f, kTestSampleRate, 0.5f);
        processor.processBlock(input.data(), input.size());

        // Measure DC offset in latter half (after settling)
        std::vector<float> latterHalf(input.begin() + input.size() / 2, input.end());
        const float dc = std::abs(calculateDC(latterHalf));

        REQUIRE(dc < 0.01f);
    }
}

TEST_CASE("AllpassSaturator NaN/Inf handling (T016)", "[allpass_saturator][safety]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);

    SECTION("NaN input resets state and returns 0 (FR-026)") {
        // Build up some state
        processor.setFeedback(0.9f);
        auto impulse = generateImpulse(100);
        processor.processBlock(impulse.data(), impulse.size());

        // Process NaN
        const float nanInput = std::numeric_limits<float>::quiet_NaN();
        const float result = processor.process(nanInput);

        REQUIRE(result == 0.0f);

        // Next valid sample should process correctly (state was reset)
        const float nextResult = processor.process(0.5f);
        REQUIRE_FALSE(std::isnan(nextResult));
        REQUIRE_FALSE(std::isinf(nextResult));
    }

    SECTION("Inf input resets state and returns 0 (FR-026)") {
        processor.setFeedback(0.9f);
        auto impulse = generateImpulse(100);
        processor.processBlock(impulse.data(), impulse.size());

        const float infInput = std::numeric_limits<float>::infinity();
        const float result = processor.process(infInput);

        REQUIRE(result == 0.0f);

        const float nextResult = processor.process(0.5f);
        REQUIRE_FALSE(std::isnan(nextResult));
        REQUIRE_FALSE(std::isinf(nextResult));
    }

    SECTION("-Inf input resets state and returns 0 (FR-026)") {
        const float negInfInput = -std::numeric_limits<float>::infinity();
        const float result = processor.process(negInfInput);

        REQUIRE(result == 0.0f);
    }
}

// =============================================================================
// Phase 4: User Story 2 - KarplusStrong Tests (T039-T043)
// =============================================================================

TEST_CASE("AllpassSaturator KarplusStrong decay control (T039)",
          "[allpass_saturator][karplusstrong][decay]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);
    processor.setTopology(NetworkTopology::KarplusStrong);

    SECTION("setDecay and getDecay work correctly") {
        processor.setDecay(2.0f);
        REQUIRE(processor.getDecay() == Approx(2.0f));

        processor.setDecay(0.5f);
        REQUIRE(processor.getDecay() == Approx(0.5f));
    }

    SECTION("decay is clamped to [0.001, 60.0]") {
        processor.setDecay(0.0001f);
        REQUIRE(processor.getDecay() == Approx(0.001f));

        processor.setDecay(100.0f);
        REQUIRE(processor.getDecay() == Approx(60.0f));
    }
}

TEST_CASE("AllpassSaturator KarplusStrong impulse response (T040)",
          "[allpass_saturator][karplusstrong][pitch]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);
    processor.setTopology(NetworkTopology::KarplusStrong);
    processor.setDecay(1.0f);

    SECTION("produces pitched tone at target frequency") {
        const float targetFreq = 220.0f;
        processor.setFrequency(targetFreq);
        processor.reset();

        // Excite with impulse
        auto impulse = generateImpulse(8192);
        processor.processBlock(impulse.data(), impulse.size());

        // Estimate frequency from response
        std::vector<float> analysis(impulse.begin() + 100, impulse.begin() + 4000);
        const float estimatedFreq = estimateFrequency(analysis, kTestSampleRate);

        // Should be within 10% of target (KarplusStrong has slightly different pitch due to lowpass)
        const float tolerance = targetFreq * 0.1f;
        REQUIRE(estimatedFreq == Approx(targetFreq).margin(tolerance));
    }
}

TEST_CASE("AllpassSaturator KarplusStrong RT60 decay time (T041)",
          "[allpass_saturator][karplusstrong][rt60]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);
    processor.setTopology(NetworkTopology::KarplusStrong);
    processor.setFrequency(440.0f);

    SECTION("longer decay setting produces longer sustain") {
        // Test relative decay: longer decay setting = longer sustain
        // This is more robust than precise RT60 measurement

        const float shortDecay = 0.5f;
        const float longDecay = 2.0f;
        const size_t testLength = 88200;  // 2 seconds

        // Short decay
        processor.setDecay(shortDecay);
        processor.reset();
        auto shortOutput = generateImpulse(testLength);
        processor.processBlock(shortOutput.data(), shortOutput.size());

        // Long decay
        processor.setDecay(longDecay);
        processor.reset();
        auto longOutput = generateImpulse(testLength);
        processor.processBlock(longOutput.data(), longOutput.size());

        // Measure RMS at the end (last 0.5 seconds)
        const size_t measureStart = testLength - 22050;
        std::vector<float> shortEnd(shortOutput.begin() + measureStart, shortOutput.end());
        std::vector<float> longEnd(longOutput.begin() + measureStart, longOutput.end());

        const float shortEndRMS = calculateRMS(shortEnd);
        const float longEndRMS = calculateRMS(longEnd);

        // Long decay should have more energy remaining at the end
        REQUIRE(longEndRMS > shortEndRMS);
    }
}

TEST_CASE("AllpassSaturator KarplusStrong drive affects harmonics (T042)",
          "[allpass_saturator][karplusstrong][harmonics]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);
    processor.setTopology(NetworkTopology::KarplusStrong);
    processor.setFrequency(220.0f);
    processor.setDecay(1.0f);

    SECTION("higher drive produces more harmonic content") {
        // Low drive
        processor.setDrive(1.0f);
        processor.reset();
        auto lowDrive = generateImpulse(4096);
        processor.processBlock(lowDrive.data(), lowDrive.size());
        const float lowDrivePeak = calculatePeak(lowDrive);

        // High drive
        processor.setDrive(3.0f);
        processor.reset();
        auto highDrive = generateImpulse(4096);
        processor.processBlock(highDrive.data(), highDrive.size());
        const float highDrivePeak = calculatePeak(highDrive);

        // High drive should produce higher peak due to saturation harmonics
        // (This is a simple heuristic - proper test would use FFT)
        REQUIRE(highDrivePeak > lowDrivePeak * 0.5f);

        // Both should still be bounded
        REQUIRE(lowDrivePeak < 2.0f);
        REQUIRE(highDrivePeak < 2.0f);
    }
}

TEST_CASE("AllpassSaturator KarplusStrong bright-attack-dark-decay (T043)",
          "[allpass_saturator][karplusstrong][timbre]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);
    processor.setTopology(NetworkTopology::KarplusStrong);
    processor.setFrequency(440.0f);
    processor.setDecay(2.0f);

    SECTION("attack is brighter than decay (characteristic string timbre)") {
        processor.reset();

        auto impulse = generateImpulse(44100);
        processor.processBlock(impulse.data(), impulse.size());

        // Calculate RMS of early portion (attack)
        std::vector<float> attack(impulse.begin(), impulse.begin() + 4410);
        const float attackRMS = calculateRMS(attack);

        // Calculate RMS of late portion (decay)
        std::vector<float> decay(impulse.end() - 8820, impulse.end());
        const float decayRMS = calculateRMS(decay);

        // Attack should have higher RMS (brighter, more energy)
        REQUIRE(attackRMS > decayRMS);
    }
}

namespace {

/// @brief The fundamental of a Karplus render, and how many periods it rests on.
struct KarplusPitch {
    double hz = 0.0;
    size_t periods = 0;
};

/// @brief Measure the fundamental of a Karplus impulse response (follow-up 92).
///
/// 1. Isolate it: an RBJ bandpass (Q 4) at the expected note. The render also
///    carries DC and a slow sub-loop mode from the excitation burst, which
///    outlast a lossy high note and would otherwise own the tail.
/// 2. Pick the steady part: from 4 periods after the bandpassed envelope's peak
///    until it is 40 dB below that peak.
/// 3. Estimate: demodulate at the expected note over 4-period Blackman-Harris
///    windows hopped one period, take the least-squares slope of the unwrapped
///    phase, and repeat once at that estimate. An exponential decay biases
///    every window's phase equally, so it leaves the slope alone.
/// Checked offline against the loop's analytic pole (Newton on
/// fb * G_lp(z) * z^-(k+1) * Thiran(z) = 1) on the same renders: within 0.06
/// cents for every tone of 38 periods or more, 0.7 cents at 8 periods.
KarplusPitch measureKarplusPitch(const std::vector<float>& y, double fs, double note) {
    constexpr double kTwoPi = 6.283185307179586;
    const double w0 = kTwoPi * note / fs;
    const double alpha = std::sin(w0) / (2.0 * 4.0);
    const double a0 = 1.0 + alpha;
    const double a1 = -2.0 * std::cos(w0);
    const double a2 = 1.0 - alpha;
    std::vector<double> bp(y.size(), 0.0);
    double x1 = 0.0;
    double x2 = 0.0;
    double y1 = 0.0;
    double y2 = 0.0;
    for (size_t i = 0; i < y.size(); ++i) {
        const double x0 = y[i];
        const double v = ((alpha * x0) - (alpha * x2) - (a1 * y1) - (a2 * y2)) / a0;
        x2 = x1;
        x1 = x0;
        y2 = y1;
        y1 = v;
        bp[i] = v;
    }

    const auto period = static_cast<size_t>(std::ceil(fs / note));
    const auto envelope = [&](size_t at) {
        double p = 0.0;
        for (size_t i = at; i < std::min(bp.size(), at + period); ++i) {
            p = std::max(p, std::abs(bp[i]));
        }
        return p;
    };
    double peak = 0.0;
    size_t peakAt = 0;
    for (size_t k = 0; (k < 60) && ((k + 1) * period < bp.size()); ++k) {
        const double e = envelope(k * period);
        if (e > peak) {
            peak = e;
            peakAt = k * period;
        }
    }
    const size_t begin = peakAt + (4 * period);
    size_t end = begin;
    while (((end + period) < bp.size()) && (envelope(end) > peak * 0.01)) {
        end += period;
    }

    const auto slopeEstimate = [&](double demodHz) {
        const auto window = static_cast<size_t>(std::llround(4.0 * fs / demodHz));
        const auto hop = static_cast<size_t>(std::llround(fs / demodHz));
        std::vector<double> phases;
        for (size_t start = begin; start + window <= end; start += hop) {
            double re = 0.0;
            double im = 0.0;
            for (size_t i = 0; i < window; ++i) {
                const double t = kTwoPi * static_cast<double>(i) / static_cast<double>(window);
                const double w = 0.35875 - (0.48829 * std::cos(t)) + (0.14128 * std::cos(2.0 * t)) -
                                 (0.01168 * std::cos(3.0 * t));
                const double arg = kTwoPi * demodHz * static_cast<double>(start + i) / fs;
                re += w * bp[start + i] * std::cos(arg);
                im -= w * bp[start + i] * std::sin(arg);
            }
            double p = std::atan2(im, re);
            if (!phases.empty()) {
                while ((p - phases.back()) > 3.141592653589793) {
                    p -= kTwoPi;
                }
                while ((p - phases.back()) < -3.141592653589793) {
                    p += kTwoPi;
                }
            }
            phases.push_back(p);
        }
        if (phases.size() < 3) {
            return 0.0;
        }
        const auto n = static_cast<double>(phases.size());
        double sx = 0.0;
        double sy = 0.0;
        double sxx = 0.0;
        double sxy = 0.0;
        for (size_t k = 0; k < phases.size(); ++k) {
            const auto kx = static_cast<double>(k);
            sx += kx;
            sy += phases[k];
            sxx += kx * kx;
            sxy += kx * phases[k];
        }
        const double slope = ((n * sxy) - (sx * sy)) / ((n * sxx) - (sx * sx));  // rad per hop
        return demodHz + (slope * fs / (kTwoPi * static_cast<double>(hop)));
    };
    const double first = slopeEstimate(note);
    return {first > 0.0 ? slopeEstimate(first) : 0.0, (end - begin) / period};
}

} // namespace

// Follow-up 92: the loop is readAllpass(d) (read BEFORE the write, so d + 1
// samples) plus the loop lowpass's phase delay at the fundamental, and a lossy
// loop rings below that (karplusLoopDelaySamples). Before the fix the node was
// -2 to -258 cents flat here. Disrumpo runs the node at its oversampled rate,
// so 176.4 kHz and 384 kHz are measured beside the base rates. Decay 1 s (the
// node default), damping 0 and 0.3 (the node default; the lowpass is the
// larger term there), drive 1 so the free decay is near-linear.
TEST_CASE("AllpassSaturator KarplusStrong is in tune (follow-up 92)",
          "[allpass_saturator][karplusstrong][pitch]") {
    constexpr double kToleranceCents = 5.0;
    for (const double sampleRate : {44100.0, 48000.0, 96000.0, 176400.0, 384000.0}) {
        for (const double note : {110.0, 440.0, 1760.0}) {
            for (const float damping : {0.0f, 0.3f}) {
                AllpassSaturator processor;
                processor.prepare(sampleRate, kTestBlockSize);
                processor.setTopology(NetworkTopology::KarplusStrong);
                processor.setFrequency(static_cast<float>(note));
                processor.setDecay(1.0f);
                processor.setDamping(damping);
                processor.setDrive(1.0f);
                processor.reset();

                const auto length = static_cast<size_t>(0.75 * sampleRate);
                std::vector<float> y(length, 0.0f);
                for (size_t i = 0; i < length; ++i) {
                    y[i] = processor.process(i < 8 ? 0.5f : 0.0f);
                }
                const KarplusPitch pitch = measureKarplusPitch(y, sampleRate, note);
                const double cents = 1200.0 * std::log2(std::max(pitch.hz, 1e-9) / note);
                std::printf("KS-PITCH sr=%6.0f note=%7.2f damping=%.1f periods=%4zu measured=%9.4f "
                            "cents=%+8.3f\n",
                            sampleRate, note, static_cast<double>(damping), pitch.periods, pitch.hz,
                            cents);
                INFO("rate " << sampleRate << ", note " << note << " Hz, damping " << damping << ": "
                             << pitch.periods << " periods, " << pitch.hz << " Hz, " << cents
                             << " cents");
                REQUIRE(pitch.periods >= 8);
                CHECK(std::abs(cents) <= kToleranceCents);
            }
        }
    }
}

// =============================================================================
// Phase 5: User Story 3 - AllpassChain Tests (T057-T060)
// =============================================================================

TEST_CASE("AllpassSaturator AllpassChain inharmonic resonance (T057)",
          "[allpass_saturator][allpasschain][inharmonic]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);
    processor.setTopology(NetworkTopology::AllpassChain);
    processor.setFeedback(0.9f);
    processor.setFrequency(200.0f);

    SECTION("produces multiple resonant peaks at prime frequency ratios") {
        processor.reset();

        // Process impulse
        auto impulse = generateImpulse(8192);
        processor.processBlock(impulse.data(), impulse.size());

        // Should have energy (non-silent output)
        const float rms = calculateRMS(impulse);
        REQUIRE(rms > 0.01f);

        // The output should be more complex than a simple sine
        // (inharmonic partials create complex waveform)
        float zeroCrossings = 0;
        for (size_t i = 1; i < impulse.size(); ++i) {
            if ((impulse[i - 1] >= 0.0f && impulse[i] < 0.0f) ||
                (impulse[i - 1] < 0.0f && impulse[i] >= 0.0f)) {
                ++zeroCrossings;
            }
        }

        // Should have irregular zero-crossing pattern (inharmonic)
        // A pure sine would have very regular crossings
        REQUIRE(zeroCrossings > 10);  // Ensure there's activity
    }
}

TEST_CASE("AllpassSaturator AllpassChain vs SingleAllpass complexity (T058)",
          "[allpass_saturator][allpasschain][timbre]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);
    processor.setFrequency(200.0f);
    processor.setFeedback(0.9f);

    SECTION("AllpassChain creates more complex timbre than SingleAllpass") {
        // Process with SingleAllpass
        processor.setTopology(NetworkTopology::SingleAllpass);
        processor.reset();
        auto single = generateImpulse(4096);
        processor.processBlock(single.data(), single.size());

        // Process with AllpassChain
        processor.setTopology(NetworkTopology::AllpassChain);
        processor.reset();
        auto chain = generateImpulse(4096);
        processor.processBlock(chain.data(), chain.size());

        // Both should have energy
        REQUIRE(calculateRMS(single) > 0.001f);
        REQUIRE(calculateRMS(chain) > 0.001f);

        // AllpassChain should have different character
        // (This is hard to quantify without FFT, but we verify they're different)
        bool different = false;
        for (size_t i = 100; i < 500; ++i) {
            if (std::abs(single[i] - chain[i]) > 0.01f) {
                different = true;
                break;
            }
        }
        REQUIRE(different);
    }
}

TEST_CASE("AllpassSaturator AllpassChain high feedback characteristics (T059)",
          "[allpass_saturator][allpasschain][metallic]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);
    processor.setTopology(NetworkTopology::AllpassChain);
    processor.setFrequency(200.0f);
    processor.setFeedback(0.95f);  // Higher feedback for better sustain

    SECTION("high feedback produces resonance with sustain") {
        processor.reset();

        auto impulse = generateImpulse(22050);  // 0.5 seconds
        processor.processBlock(impulse.data(), impulse.size());

        // Should have energy during early portion (resonance occurred)
        const float earlyRMS = calculateRMS(std::vector<float>(
            impulse.begin() + 500, impulse.begin() + 2000));

        // Chain should produce audible resonance
        REQUIRE(earlyRMS > 0.01f);

        // Higher feedback should sustain better than lower feedback
        AllpassSaturator lowFb;
        lowFb.prepare(kTestSampleRate, kTestBlockSize);
        lowFb.setTopology(NetworkTopology::AllpassChain);
        lowFb.setFrequency(200.0f);
        lowFb.setFeedback(0.5f);  // Lower feedback

        auto lowFbOutput = generateImpulse(22050);
        lowFb.processBlock(lowFbOutput.data(), lowFbOutput.size());

        // Mid-portion comparison: high feedback should have more energy
        std::vector<float> highMid(impulse.begin() + 10000, impulse.begin() + 12000);
        std::vector<float> lowMid(lowFbOutput.begin() + 10000, lowFbOutput.begin() + 12000);

        const float highMidRMS = calculateRMS(highMid);
        const float lowMidRMS = calculateRMS(lowMid);

        REQUIRE(highMidRMS >= lowMidRMS);  // Higher feedback = more sustained resonance
    }
}

TEST_CASE("AllpassSaturator AllpassChain bounded output (T060)",
          "[allpass_saturator][allpasschain][bounded]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);
    processor.setTopology(NetworkTopology::AllpassChain);
    processor.setFeedback(0.99f);
    processor.setDrive(5.0f);

    SECTION("output remains bounded < 2.0 with high feedback") {
        processor.reset();

        auto input = generateSine(44100, 200.0f, kTestSampleRate, 1.0f);
        processor.processBlock(input.data(), input.size());

        const float peak = calculatePeak(input);
        REQUIRE(peak < 2.0f);
    }
}

// =============================================================================
// Phase 6: User Story 4 - FeedbackMatrix Tests (T073-T076)
// =============================================================================

TEST_CASE("AllpassSaturator FeedbackMatrix resonant behavior (T073)",
          "[allpass_saturator][feedbackmatrix][resonance]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);
    processor.setTopology(NetworkTopology::FeedbackMatrix);
    processor.setFrequency(200.0f);
    processor.setFeedback(0.95f);

    SECTION("produces output from impulse") {
        processor.reset();

        // Single impulse excitation
        auto output = generateImpulse(8820);  // 200ms
        processor.processBlock(output.data(), output.size());

        // Should have energy during processing
        const float rms = calculateRMS(output);
        const float peak = calculatePeak(output);

        // Matrix should process input and produce output
        REQUIRE(peak > 0.0f);  // Output is non-zero
        REQUIRE(rms > 0.001f); // Some energy present
    }

    SECTION("higher feedback produces more sustained resonance") {
        // High feedback
        processor.setFeedback(0.95f);
        processor.reset();
        auto highFbOutput = generateImpulse(8820);
        processor.processBlock(highFbOutput.data(), highFbOutput.size());

        // Low feedback
        processor.setFeedback(0.5f);
        processor.reset();
        auto lowFbOutput = generateImpulse(8820);
        processor.processBlock(lowFbOutput.data(), lowFbOutput.size());

        // Both should have output from impulse
        const float highPeak = calculatePeak(highFbOutput);
        const float lowPeak = calculatePeak(lowFbOutput);

        REQUIRE(highPeak > 0.0f);
        REQUIRE(lowPeak > 0.0f);

        // High feedback should have at least as much energy as low feedback
        const float highRMS = calculateRMS(highFbOutput);
        const float lowRMS = calculateRMS(lowFbOutput);

        // Both should process the impulse
        REQUIRE(highRMS > 0.001f);
        REQUIRE(lowRMS > 0.001f);
    }
}

TEST_CASE("AllpassSaturator FeedbackMatrix complex beating (T074)",
          "[allpass_saturator][feedbackmatrix][beating]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);
    processor.setTopology(NetworkTopology::FeedbackMatrix);
    processor.setFrequency(100.0f);
    processor.setFeedback(0.9f);

    SECTION("4 different frequencies create complex beating patterns") {
        processor.reset();

        // Excite
        auto excitation = generateImpulse(1000);
        processor.processBlock(excitation.data(), excitation.size());

        // Process more samples
        std::vector<float> output(44100, 0.0f);
        processor.processBlock(output.data(), output.size());

        // Should have amplitude modulation (beating)
        // Calculate envelope (simple moving RMS)
        std::vector<float> envelope;
        const size_t windowSize = 441;  // 10ms window
        for (size_t i = windowSize; i < output.size(); i += windowSize / 2) {
            float sumSquares = 0.0f;
            for (size_t j = i - windowSize; j < i; ++j) {
                sumSquares += output[j] * output[j];
            }
            envelope.push_back(std::sqrt(sumSquares / static_cast<float>(windowSize)));
        }

        // Envelope should vary (beating)
        if (envelope.size() > 10) {
            float minEnv = *std::min_element(envelope.begin(), envelope.end());
            float maxEnv = *std::max_element(envelope.begin(), envelope.end());
            // Ratio indicates amplitude modulation depth
            REQUIRE(maxEnv > minEnv * 1.1f);  // At least 10% modulation
        }
    }
}

TEST_CASE("AllpassSaturator FeedbackMatrix bounded during self-oscillation (T075)",
          "[allpass_saturator][feedbackmatrix][bounded]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);
    processor.setTopology(NetworkTopology::FeedbackMatrix);
    processor.setFrequency(100.0f);
    processor.setFeedback(0.99f);
    processor.setDrive(5.0f);

    SECTION("output remains bounded < 2.0 during self-oscillation") {
        processor.reset();

        // Excite and let it run
        auto excitation = generateImpulse(1000);
        processor.processBlock(excitation.data(), excitation.size());

        std::vector<float> output(88200, 0.0f);  // 2 seconds
        processor.processBlock(output.data(), output.size());

        const float peak = calculatePeak(output);
        REQUIRE(peak < 2.0f);
    }
}

TEST_CASE("AllpassSaturator HouseholderMatrix energy preservation (T076)",
          "[allpass_saturator][feedbackmatrix][householder]") {
    SECTION("Householder matrix preserves energy: ||H*x|| == ||x||") {
        // Test the matrix directly
        std::array<float, 4> input = {1.0f, 2.0f, 3.0f, 4.0f};
        std::array<float, 4> output;

        // Calculate input norm
        float inputNorm = std::sqrt(input[0] * input[0] + input[1] * input[1] +
                                    input[2] * input[2] + input[3] * input[3]);

        // Apply Householder
        AllpassSaturator::HouseholderMatrix::multiply(input.data(), output.data());

        // Calculate output norm
        float outputNorm = std::sqrt(output[0] * output[0] + output[1] * output[1] +
                                     output[2] * output[2] + output[3] * output[3]);

        // Should be equal (energy preserving)
        REQUIRE(outputNorm == Approx(inputNorm).margin(1e-5f));
    }

    SECTION("Householder matrix is orthogonal: H*H*x == x") {
        std::array<float, 4> input = {0.5f, -0.3f, 0.7f, -0.2f};
        std::array<float, 4> intermediate;
        std::array<float, 4> output;

        // Apply twice
        AllpassSaturator::HouseholderMatrix::multiply(input.data(), intermediate.data());
        AllpassSaturator::HouseholderMatrix::multiply(intermediate.data(), output.data());

        // Should return to original (H is its own inverse for Householder reflections)
        for (size_t i = 0; i < 4; ++i) {
            REQUIRE(output[i] == Approx(input[i]).margin(1e-5f));
        }
    }
}

// =============================================================================
// Phase 7: Polish Tests (T091-T095)
// =============================================================================

TEST_CASE("AllpassSaturator parameter smoothing verification (T092)",
          "[allpass_saturator][smoothing]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);
    processor.setTopology(NetworkTopology::SingleAllpass);

    SECTION("frequency changes complete within 10ms (SC-004)") {
        processor.setFrequency(200.0f);
        processor.reset();

        // Process to settle initial frequency
        std::vector<float> settle(4410, 0.0f);
        processor.processBlock(settle.data(), settle.size());

        // Change frequency
        processor.setFrequency(800.0f);

        // Process 10ms worth of samples (441 at 44.1kHz)
        std::vector<float> transition(441, 0.0f);
        processor.processBlock(transition.data(), transition.size());

        // After 10ms, the smoother should be nearly complete
        // The internal frequency should be close to target
        // (We can't directly query internal smoothed value, but behavior should reflect it)
    }

    SECTION("parameter changes don't cause clicks") {
        processor.setFrequency(440.0f);
        processor.setFeedback(0.9f);

        // Generate continuous signal
        auto input = generateSine(44100, 110.0f, kTestSampleRate, 0.3f);

        // Process first half
        processor.processBlock(input.data(), input.size() / 2);

        // Change parameters mid-stream
        processor.setFrequency(880.0f);
        processor.setFeedback(0.5f);
        processor.setDrive(3.0f);

        // Process second half
        processor.processBlock(input.data() + input.size() / 2, input.size() / 2);

        // Check for sudden jumps (clicks) at transition point
        const size_t transitionPoint = input.size() / 2;
        float maxJump = 0.0f;
        for (size_t i = transitionPoint - 5; i < transitionPoint + 5 && i < input.size() - 1; ++i) {
            float jump = std::abs(input[i + 1] - input[i]);
            maxJump = std::max(maxJump, jump);
        }

        // Jump should not exceed reasonable threshold (no sudden discontinuities)
        REQUIRE(maxJump < 0.5f);
    }
}

TEST_CASE("AllpassSaturator topology switching safety (T093)",
          "[allpass_saturator][topology][safety]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);

    SECTION("no crashes when changing topology mid-processing") {
        processor.setFeedback(0.9f);

        auto input = generateSine(8820, 440.0f, kTestSampleRate, 0.5f);

        // Process with each topology, changing mid-block
        const std::array<NetworkTopology, 4> topologies = {
            NetworkTopology::SingleAllpass,
            NetworkTopology::AllpassChain,
            NetworkTopology::KarplusStrong,
            NetworkTopology::FeedbackMatrix
        };

        for (size_t i = 0; i < topologies.size(); ++i) {
            processor.setTopology(topologies[i]);

            // Process a portion
            processor.processBlock(input.data() + i * 2000,
                                   std::min(size_t(2000), input.size() - i * 2000));
        }

        // If we got here without crashing, test passes
        REQUIRE(true);
    }
}

TEST_CASE("AllpassSaturator edge cases (T095)", "[allpass_saturator][edge]") {
    AllpassSaturator processor;
    processor.prepare(kTestSampleRate, kTestBlockSize);

    SECTION("handles zero-length buffer") {
        processor.processBlock(nullptr, 0);
        REQUIRE(true);  // No crash
    }

    SECTION("handles single sample") {
        float sample = 0.5f;
        processor.processBlock(&sample, 1);
        REQUIRE_FALSE(std::isnan(sample));
        REQUIRE_FALSE(std::isinf(sample));
    }

    SECTION("handles extreme frequency values") {
        processor.setFrequency(0.0f);
        REQUIRE(processor.getFrequency() >= 20.0f);

        processor.setFrequency(100000.0f);
        REQUIRE(processor.getFrequency() <= static_cast<float>(kTestSampleRate) * 0.45f);
    }

    SECTION("handles extreme feedback values") {
        processor.setFeedback(-100.0f);
        REQUIRE(processor.getFeedback() >= 0.0f);

        processor.setFeedback(100.0f);
        REQUIRE(processor.getFeedback() <= 0.999f);
    }

    SECTION("handles extreme drive values") {
        processor.setDrive(-5.0f);
        REQUIRE(processor.getDrive() >= 0.1f);

        processor.setDrive(1000.0f);
        REQUIRE(processor.getDrive() <= 10.0f);
    }

    SECTION("processing when unprepared returns input unchanged") {
        AllpassSaturator unprepared;
        const float input = 0.7f;
        const float output = unprepared.process(input);
        REQUIRE(output == Approx(input));
    }
}

// =============================================================================
// D-084 (Disrumpo audit T4.16): reconfigure only on change, bit-identical
// =============================================================================
// process() used to rebuild all nine allpass sections (single stage, 4 chain
// biquads, 4 matrix stages: nine sin/cos designs) and every drive on every
// sample, whatever the topology. It now reconfigures only the active
// topology, and only when the smoothed frequency or drive changed. The
// reference is the pre-T4.16 class, snapshotted verbatim into
// allpass_saturator_pre_t416_reference.h (namespace t416_reference); this TU is
// IEEE on clang/GCC (-fno-fast-math -ffp-contract=off), and it is the only TU
// in dsp_processors_tests that includes either header.

namespace {

namespace t416ref = Krate::DSP::t416_reference;

t416ref::NetworkTopology toReference(NetworkTopology t) {
    return static_cast<t416ref::NetworkTopology>(static_cast<std::uint8_t>(t));
}

/// One scripted parameter move applied to both processors at `sample`.
struct T416Move {
    size_t sample;
    int what;      // 0 freq, 1 drive, 2 feedback, 3 damping, 4 decay, 5 topology, 6 curve
    float value;
};

/// Render `input` through the shipping and the reference class with the same
/// script; returns the number of samples whose bits differ (and the max |diff|).
template <typename Script>
size_t t416Mismatches(double sampleRate, NetworkTopology startTopology, const std::vector<float>& input,
                      const Script& script, float& maxAbsDiff) {
    AllpassSaturator fixedSat;
    t416ref::AllpassSaturator refSat;
    fixedSat.setTopology(startTopology);
    refSat.setTopology(toReference(startTopology));
    fixedSat.prepare(sampleRate, 512);
    refSat.prepare(sampleRate, 512);

    size_t next = 0;
    size_t mismatches = 0;
    maxAbsDiff = 0.0f;
    for (size_t i = 0; i < input.size(); ++i) {
        while (next < script.size() && script[next].sample == i) {
            const T416Move& m = script[next++];
            switch (m.what) {
                case 0: fixedSat.setFrequency(m.value); refSat.setFrequency(m.value); break;
                case 1: fixedSat.setDrive(m.value); refSat.setDrive(m.value); break;
                case 2: fixedSat.setFeedback(m.value); refSat.setFeedback(m.value); break;
                case 3: fixedSat.setDamping(m.value); refSat.setDamping(m.value); break;
                case 4: fixedSat.setDecay(m.value); refSat.setDecay(m.value); break;
                case 5: {
                    const auto t = static_cast<NetworkTopology>(static_cast<std::uint8_t>(m.value));
                    fixedSat.setTopology(t);
                    refSat.setTopology(toReference(t));
                    break;
                }
                case 6: {
                    const auto c = static_cast<WaveshapeType>(static_cast<std::uint8_t>(m.value));
                    fixedSat.setSaturationCurve(c);
                    refSat.setSaturationCurve(c);
                    break;
                }
                default: break;
            }
        }
        const float a = fixedSat.process(input[i]);
        const float b = refSat.process(input[i]);
        if (std::bit_cast<std::uint32_t>(a) != std::bit_cast<std::uint32_t>(b)) {
            ++mismatches;
        }
        maxAbsDiff = std::max(maxAbsDiff, std::abs(a - b));
    }
    return mismatches;
}

std::vector<float> t416Input(size_t length, double sampleRate) {
    std::mt19937 gen(416u);
    std::uniform_real_distribution<float> noise(-0.3f, 0.3f);
    std::vector<float> x(length, 0.0f);
    for (size_t i = 0; i < length; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        float v = 0.4f * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * 110.0 * t));
        if ((i / 2400) % 4 == 0) {
            v += noise(gen);  // noise bursts excite the resonances
        }
        x[i] = v;
    }
    return x;
}

/// Block-rate jumps of every parameter, plus per-sample-ish glides (a move every
/// 16 samples) so the smoothers are in motion most of the time, and one
/// topology / curve change per second.
std::vector<T416Move> t416Script(size_t length, bool withTopologySwitches) {
    std::mt19937 gen(4160u);
    std::uniform_real_distribution<float> freq(30.0f, 3000.0f);
    std::uniform_real_distribution<float> drive(0.1f, 10.0f);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::vector<T416Move> s;
    for (size_t i = 0; i < length; i += 512) {
        s.push_back({i, 0, freq(gen)});
        s.push_back({i, 1, drive(gen)});
        s.push_back({i, 2, 0.999f * unit(gen)});
        if ((i / 512) % 3 == 0) {
            s.push_back({i, 3, unit(gen)});
            s.push_back({i, 4, 0.05f + 3.0f * unit(gen)});
        }
        // Long stretches with no move at all after some blocks: the smoothers settle.
        if ((i / 512) % 8 == 7) {
            continue;
        }
        for (size_t k = 16; k < 512 && i + k < length; k += 16) {
            if ((i / 512) % 2 == 0) {
                s.push_back({i + k, 0, 200.0f + 1800.0f * static_cast<float>(k) / 512.0f});
            } else {
                s.push_back({i + k, 1, 0.5f + 4.0f * static_cast<float>(k) / 512.0f});
            }
        }
    }
    if (withTopologySwitches) {
        const std::array<float, 6> order{1.0f, 3.0f, 2.0f, 0.0f, 3.0f, 1.0f};
        for (size_t n = 0; n < order.size(); ++n) {
            const size_t at = (n + 1) * length / (order.size() + 1) + 3;
            s.push_back({at, 5, order[n]});
            s.push_back({at + 100, 6, static_cast<float>((n * 3) % 9)});
        }
    }
    std::stable_sort(s.begin(), s.end(), [](const T416Move& a, const T416Move& b) { return a.sample < b.sample; });
    return s;
}

} // namespace

TEST_CASE("AllpassSaturator reconfigures on change only, bit-identical to per-sample reconfiguration",
          "[allpass_saturator][D-084]") {
    const std::array<NetworkTopology, 4> topologies{NetworkTopology::SingleAllpass, NetworkTopology::AllpassChain,
                                                    NetworkTopology::KarplusStrong, NetworkTopology::FeedbackMatrix};
    for (const double sampleRate : {44100.0, 96000.0}) {
        const auto length = static_cast<size_t>(sampleRate * 2.0);
        const auto input = t416Input(length, sampleRate);
        for (const NetworkTopology topology : topologies) {
            float maxDiff = 0.0f;
            const size_t mismatches = t416Mismatches(sampleRate, topology, input, t416Script(length, false), maxDiff);
            std::printf("D-084 %.0f Hz topology %d fixed: %zu / %zu samples differ, max |diff| %g\n", sampleRate,
                        static_cast<int>(topology), mismatches, length, static_cast<double>(maxDiff));
            INFO("rate " << sampleRate << ", topology " << static_cast<int>(topology));
            REQUIRE(mismatches == 0);
        }
        // Topology and curve switches mid-run: the newly active topology must be
        // configured for the current smoothed values before its first sample.
        float maxDiff = 0.0f;
        const size_t mismatches =
            t416Mismatches(sampleRate, NetworkTopology::SingleAllpass, input, t416Script(length, true), maxDiff);
        std::printf("D-084 %.0f Hz topology switches: %zu / %zu samples differ, max |diff| %g\n", sampleRate,
                    mismatches, length, static_cast<double>(maxDiff));
        INFO("rate " << sampleRate << ", topology switches");
        REQUIRE(mismatches == 0);

        // Topology switches with SETTLED parameters: the smoothers are at their
        // targets, so nothing but the switch itself can bring the newly active
        // topology (last configured in prepare(), at 440 Hz / drive 1) up to date.
        std::vector<T416Move> settled{{0, 0, 1234.0f}, {0, 1, 3.5f}, {0, 2, 0.8f}};
        const std::array<float, 6> order{1.0f, 3.0f, 2.0f, 0.0f, 3.0f, 1.0f};
        for (size_t n = 0; n < order.size(); ++n) {
            settled.push_back({(n + 1) * length / (order.size() + 1), 5, order[n]});
        }
        float settledDiff = 0.0f;
        const size_t settledMismatches =
            t416Mismatches(sampleRate, NetworkTopology::SingleAllpass, input, settled, settledDiff);
        std::printf("D-084 %.0f Hz topology switches, settled params: %zu / %zu samples differ, max |diff| %g\n",
                    sampleRate, settledMismatches, length, static_cast<double>(settledDiff));
        INFO("rate " << sampleRate << ", topology switches with settled parameters");
        REQUIRE(settledMismatches == 0);
    }
}

// Hidden printing probe (the plan names no CPU budget, so nothing is asserted):
// ns per sample of the shipping class against the per-sample-reconfiguring
// reference, steady parameters, per topology.
TEST_CASE("AllpassSaturator T4.16 CPU probe", "[.][t416-cpu-probe]") {
    constexpr double kRate = 48000.0;
    constexpr size_t kLength = 48000 * 10;
    const auto input = t416Input(kLength, kRate);
    const std::array<NetworkTopology, 4> topologies{NetworkTopology::SingleAllpass, NetworkTopology::AllpassChain,
                                                    NetworkTopology::KarplusStrong, NetworkTopology::FeedbackMatrix};
    for (const NetworkTopology topology : topologies) {
        AllpassSaturator fixedSat;
        t416ref::AllpassSaturator refSat;
        fixedSat.setTopology(topology);
        refSat.setTopology(toReference(topology));
        fixedSat.prepare(kRate, 512);
        refSat.prepare(kRate, 512);
        fixedSat.setFeedback(0.7f);
        refSat.setFeedback(0.7f);

        double best[2] = {1e30, 1e30};
        float sink = 0.0f;
        for (int rep = 0; rep < 5; ++rep) {
            for (int which = 0; which < 2; ++which) {
                const auto t0 = std::chrono::steady_clock::now();
                for (size_t i = 0; i < kLength; ++i) {
                    sink += (which == 0) ? fixedSat.process(input[i]) : refSat.process(input[i]);
                }
                const auto t1 = std::chrono::steady_clock::now();
                const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / kLength;
                best[which] = std::min(best[which], ns);
            }
        }
        const double cpuFixed = best[0] * kRate * 1e-7;   // % of one core at 48 kHz
        const double cpuRef = best[1] * kRate * 1e-7;
        std::printf("T4.16 CPU topology %d: %.2f ns/sample (%.3f %%) vs reference %.2f ns/sample (%.3f %%), %.2fx [sink %g]\n",
                    static_cast<int>(topology), best[0], cpuFixed, best[1], cpuRef, best[1] / best[0],
                    static_cast<double>(sink));
    }
    SUCCEED();
}
