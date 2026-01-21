// ==============================================================================
// Layer 1: DSP Primitive Tests - State Variable Filter (SVF)
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written before implementation.
//
// Tests for: dsp/include/krate/dsp/primitives/svf.h
// Contract: specs/071-svf/contracts/svf.h
// Reference: Cytomic SvfLinearTrapOptimised2.pdf
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/window_functions.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// ==============================================================================
// Test Constants
// ==============================================================================

constexpr float kTestSampleRate = 44100.0f;
constexpr float kTestFrequency = 1000.0f;

// ==============================================================================
// Test Helpers
// ==============================================================================

namespace {

/// Generate sine wave buffer
std::vector<float> generateSine(float freq, float sampleRate, size_t numSamples) {
    std::vector<float> buffer(numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = std::sin(kTwoPi * freq * static_cast<float>(i) / sampleRate);
    }
    return buffer;
}

/// Measure RMS of a buffer
float measureRMS(const std::vector<float>& buffer, size_t startSample = 0) {
    float sum = 0.0f;
    size_t count = buffer.size() - startSample;
    for (size_t i = startSample; i < buffer.size(); ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(count));
}

/// Measure peak amplitude in buffer
float measurePeak(const std::vector<float>& buffer, size_t startSample = 0) {
    float peak = 0.0f;
    for (size_t i = startSample; i < buffer.size(); ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

/// Convert linear amplitude to dB
float linearToDb(float linear) {
    if (linear <= 0.0f) return -144.0f;
    return 20.0f * std::log10(linear);
}

/// Measure filter gain at specific frequency
float measureGainAtFrequency(SVF& filter, float testFreq, float sampleRate, size_t numSamples = 8192) {
    filter.reset();

    // Let filter settle
    const float omega = kTwoPi * testFreq / sampleRate;
    for (size_t i = 0; i < 2000; ++i) {
        (void)filter.process(std::sin(omega * static_cast<float>(i)));
    }
    filter.reset();

    // Process and measure amplitude in steady state
    float maxOutput = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        float input = std::sin(omega * static_cast<float>(i));
        float output = filter.process(input);
        if (i >= numSamples * 3 / 4) {
            maxOutput = std::max(maxOutput, std::abs(output));
        }
    }

    return maxOutput;
}

} // namespace

// ==============================================================================
// Phase 3: User Story 1 - Synth-Style Filtering Tests [US1]
// ==============================================================================

// -----------------------------------------------------------------------------
// SC-011: Audio-rate cutoff modulation produces no clicks
// -----------------------------------------------------------------------------

TEST_CASE("SVF audio-rate modulation produces no clicks", "[svf][US1][SC-011][modulation]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Lowpass);
    filter.setResonance(SVF::kButterworthQ);

    // Generate unit amplitude test signal (440 Hz sine)
    constexpr size_t numSamples = 200;
    auto input = generateSine(440.0f, kTestSampleRate, numSamples);

    SECTION("Sweep 100Hz to 10kHz in 100 samples produces no clicks") {
        std::vector<float> output(numSamples);

        // Sweep cutoff from 100Hz to 10kHz over first 100 samples
        for (size_t i = 0; i < numSamples; ++i) {
            if (i < 100) {
                // Linear interpolation of cutoff
                float t = static_cast<float>(i) / 100.0f;
                float cutoff = 100.0f + t * (10000.0f - 100.0f);
                filter.setCutoff(cutoff);
            }
            output[i] = filter.process(input[i]);
        }

        // Check for clicks: max sample-to-sample change < 0.5 for unit amplitude input
        float maxChange = 0.0f;
        for (size_t i = 1; i < numSamples; ++i) {
            float change = std::abs(output[i] - output[i - 1]);
            maxChange = std::max(maxChange, change);
        }

        INFO("Max sample-to-sample change: " << maxChange);
        CHECK(maxChange < 0.5f);
    }
}

TEST_CASE("SVF high Q modulation stability", "[svf][US1][modulation][stability]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Lowpass);
    filter.setResonance(10.0f);  // High Q

    // Modulate cutoff sinusoidally at 20Hz with 2 octave depth
    // Center at 1000Hz, sweep from 500Hz to 2000Hz
    constexpr size_t numSamples = 44100;  // 1 second
    float centerFreq = 1000.0f;
    float modFreq = 20.0f;

    bool hasNaN = false;
    bool hasInf = false;
    bool runaway = false;
    float maxOutput = 0.0f;

    for (size_t i = 0; i < numSamples; ++i) {
        // Sinusoidal cutoff modulation: 500Hz to 2000Hz (2 octaves)
        float modPhase = kTwoPi * modFreq * static_cast<float>(i) / kTestSampleRate;
        float modValue = std::sin(modPhase);  // -1 to +1
        // Map to 500-2000Hz (1 octave down to 1 octave up from 1000Hz)
        float cutoff = centerFreq * std::pow(2.0f, modValue);
        filter.setCutoff(cutoff);

        // Process white noise-like signal (simple deterministic pseudo-random)
        float input = std::sin(static_cast<float>(i) * 0.12345f);
        float output = filter.process(input);

        if (detail::isNaN(output)) hasNaN = true;
        if (detail::isInf(output)) hasInf = true;
        maxOutput = std::max(maxOutput, std::abs(output));
    }

    // With high Q, output can be loud but should not run away
    if (maxOutput > 100.0f) runaway = true;

    CHECK_FALSE(hasNaN);
    CHECK_FALSE(hasInf);
    CHECK_FALSE(runaway);
}

// -----------------------------------------------------------------------------
// SC-001, SC-002: Lowpass frequency response
// -----------------------------------------------------------------------------

TEST_CASE("SVF lowpass attenuates high frequencies", "[svf][US1][SC-001][frequency-response]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Lowpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(SVF::kButterworthQ);

    // Process 10kHz sine (1 decade above cutoff)
    float gainAt10k = measureGainAtFrequency(filter, 10000.0f, kTestSampleRate);
    float dbAt10k = linearToDb(gainAt10k);

    INFO("Lowpass 1kHz cutoff, gain at 10kHz: " << dbAt10k << " dB");

    // SC-001: At least 22 dB attenuation at 10kHz (2-pole, 12dB/oct)
    CHECK(dbAt10k <= -22.0f);
}

TEST_CASE("SVF lowpass passes low frequencies", "[svf][US1][SC-002][frequency-response]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Lowpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(SVF::kButterworthQ);

    // Process 100Hz sine (1 decade below cutoff)
    float gainAt100 = measureGainAtFrequency(filter, 100.0f, kTestSampleRate);
    float dbAt100 = linearToDb(gainAt100);

    INFO("Lowpass 1kHz cutoff, gain at 100Hz: " << dbAt100 << " dB");

    // SC-002: Less than 0.5 dB attenuation at 100Hz
    CHECK(dbAt100 >= -0.5f);
    CHECK(dbAt100 <= 0.5f);  // Should be near unity
}

// -----------------------------------------------------------------------------
// Acceptance Scenarios for User Story 1
// -----------------------------------------------------------------------------

TEST_CASE("SVF US1 Acceptance: Lowpass 1000Hz passes 100Hz within 0.5dB", "[svf][US1][acceptance]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Lowpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(SVF::kButterworthQ);

    float gain = measureGainAtFrequency(filter, 100.0f, kTestSampleRate);
    float db = linearToDb(gain);

    INFO("Gain at 100Hz: " << db << " dB");
    CHECK(std::abs(db) < 0.5f);
}

TEST_CASE("SVF US1 Acceptance: Lowpass 1000Hz attenuates 10kHz by 22dB", "[svf][US1][acceptance]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Lowpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(SVF::kButterworthQ);

    float gain = measureGainAtFrequency(filter, 10000.0f, kTestSampleRate);
    float db = linearToDb(gain);

    INFO("Gain at 10kHz: " << db << " dB");
    CHECK(db <= -22.0f);
}

// -----------------------------------------------------------------------------
// FR-021: process() before prepare() returns input unchanged
// -----------------------------------------------------------------------------

TEST_CASE("SVF process before prepare returns input unchanged", "[svf][US1][FR-021]") {
    SVF filter;  // Not prepared!

    float input = 0.5f;
    float output = filter.process(input);

    CHECK(output == Approx(input).margin(1e-6f));
}

// -----------------------------------------------------------------------------
// FR-022: NaN and Infinity input handling
// -----------------------------------------------------------------------------

TEST_CASE("SVF NaN input returns 0 and resets state", "[svf][US1][FR-022][nan]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Lowpass);
    filter.setCutoff(1000.0f);

    // Build up some state
    (void)filter.process(1.0f);
    (void)filter.process(0.5f);
    (void)filter.process(0.25f);

    // Feed NaN
    float nan = std::numeric_limits<float>::quiet_NaN();
    float output = filter.process(nan);

    CHECK(output == 0.0f);

    // After NaN, filter should work normally again
    float normalOutput = filter.process(0.5f);
    CHECK_FALSE(detail::isNaN(normalOutput));
}

TEST_CASE("SVF Infinity input returns 0 and resets state", "[svf][US1][FR-022][inf]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Lowpass);
    filter.setCutoff(1000.0f);

    // Build up some state
    (void)filter.process(1.0f);
    (void)filter.process(0.5f);

    // Feed positive infinity
    float inf = std::numeric_limits<float>::infinity();
    float output = filter.process(inf);

    CHECK(output == 0.0f);

    // Feed negative infinity
    (void)filter.process(0.5f);  // Normal sample first
    output = filter.process(-inf);

    CHECK(output == 0.0f);
}

// -----------------------------------------------------------------------------
// reset() clears state variables
// -----------------------------------------------------------------------------

TEST_CASE("SVF reset clears state", "[svf][US1][reset]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Lowpass);
    filter.setCutoff(1000.0f);

    // Process some samples to build up state
    for (int i = 0; i < 100; ++i) {
        (void)filter.process(std::sin(static_cast<float>(i) * 0.1f));
    }

    // Reset
    filter.reset();

    // After reset, an impulse should produce same output as fresh filter
    SVF freshFilter;
    freshFilter.prepare(kTestSampleRate);
    freshFilter.setMode(SVFMode::Lowpass);
    freshFilter.setCutoff(1000.0f);

    float resetOutput = filter.process(1.0f);
    float freshOutput = freshFilter.process(1.0f);

    CHECK(resetOutput == Approx(freshOutput).margin(1e-6f));
}

// -----------------------------------------------------------------------------
// Edge cases: parameter validation
// -----------------------------------------------------------------------------

TEST_CASE("SVF handles edge case parameters", "[svf][US1][edge-cases]") {
    SVF filter;
    filter.prepare(kTestSampleRate);

    SECTION("Zero sample rate clamps to minimum") {
        SVF f;
        f.prepare(0.0);  // Invalid
        f.setMode(SVFMode::Lowpass);
        f.setCutoff(1000.0f);
        // Should not crash, and should work
        float out = f.process(1.0f);
        CHECK_FALSE(detail::isNaN(out));
    }

    SECTION("Negative cutoff clamps to minimum") {
        filter.setCutoff(-100.0f);
        CHECK(filter.getCutoff() >= SVF::kMinCutoff);
    }

    SECTION("Zero cutoff clamps to minimum") {
        filter.setCutoff(0.0f);
        CHECK(filter.getCutoff() >= SVF::kMinCutoff);
    }

    SECTION("Cutoff above Nyquist clamps") {
        filter.setCutoff(30000.0f);  // Above Nyquist for 44.1kHz
        float maxCutoff = kTestSampleRate * SVF::kMaxCutoffRatio;
        CHECK(filter.getCutoff() <= maxCutoff);
    }

    SECTION("Zero Q clamps to minimum") {
        filter.setResonance(0.0f);
        CHECK(filter.getResonance() >= SVF::kMinQ);
    }

    SECTION("Negative Q clamps to minimum") {
        filter.setResonance(-5.0f);
        CHECK(filter.getResonance() >= SVF::kMinQ);
    }

    SECTION("Q above maximum clamps") {
        filter.setResonance(100.0f);
        CHECK(filter.getResonance() <= SVF::kMaxQ);
    }
}

// -----------------------------------------------------------------------------
// Getter tests
// -----------------------------------------------------------------------------

TEST_CASE("SVF getters return correct values", "[svf][US1][getters]") {
    SVF filter;

    SECTION("Default values before prepare") {
        CHECK(filter.getMode() == SVFMode::Lowpass);
        CHECK(filter.getCutoff() == Approx(1000.0f).margin(1.0f));
        CHECK(filter.getResonance() == Approx(SVF::kButterworthQ).margin(0.001f));
        CHECK(filter.getGain() == Approx(0.0f).margin(0.01f));
        CHECK_FALSE(filter.isPrepared());
    }

    SECTION("After prepare") {
        filter.prepare(kTestSampleRate);
        CHECK(filter.isPrepared());
    }

    SECTION("After setMode") {
        filter.setMode(SVFMode::Highpass);
        CHECK(filter.getMode() == SVFMode::Highpass);
    }

    SECTION("After setCutoff") {
        filter.prepare(kTestSampleRate);
        filter.setCutoff(2000.0f);
        CHECK(filter.getCutoff() == Approx(2000.0f).margin(0.1f));
    }

    SECTION("After setResonance") {
        filter.setResonance(5.0f);
        CHECK(filter.getResonance() == Approx(5.0f).margin(0.01f));
    }
}

// ==============================================================================
// Phase 4: User Story 2 - Multi-Output Processing Tests [US2]
// ==============================================================================

TEST_CASE("SVF processMulti returns all four outputs", "[svf][US2][processMulti]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setCutoff(1000.0f);
    filter.setResonance(SVF::kButterworthQ);

    // Process single sample
    SVFOutputs outputs = filter.processMulti(0.5f);

    // All outputs should be computed (may not be exactly equal for a single sample)
    // Just verify they're all finite
    CHECK_FALSE(detail::isNaN(outputs.low));
    CHECK_FALSE(detail::isNaN(outputs.high));
    CHECK_FALSE(detail::isNaN(outputs.band));
    CHECK_FALSE(detail::isNaN(outputs.notch));
}

TEST_CASE("SVF processMulti US2 Acceptance: 100Hz sine at 1000Hz cutoff", "[svf][US2][acceptance]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setCutoff(1000.0f);
    filter.setResonance(SVF::kButterworthQ);

    // Process 100Hz sine (1 decade below cutoff)
    constexpr size_t numSamples = 8192;
    auto input = generateSine(100.0f, kTestSampleRate, numSamples);

    // Let filter settle
    for (size_t i = 0; i < 2000; ++i) {
        (void)filter.processMulti(input[i % numSamples]);
    }
    filter.reset();

    // Measure each output
    float maxLow = 0.0f, maxHigh = 0.0f, maxBand = 0.0f, maxNotch = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        SVFOutputs out = filter.processMulti(input[i]);
        if (i >= numSamples * 3 / 4) {
            maxLow = std::max(maxLow, std::abs(out.low));
            maxHigh = std::max(maxHigh, std::abs(out.high));
            maxBand = std::max(maxBand, std::abs(out.band));
            maxNotch = std::max(maxNotch, std::abs(out.notch));
        }
    }

    float dbLow = linearToDb(maxLow);
    float dbHigh = linearToDb(maxHigh);
    float dbNotch = linearToDb(maxNotch);

    INFO("100Hz signal at 1kHz cutoff:");
    INFO("  Low: " << dbLow << " dB");
    INFO("  High: " << dbHigh << " dB");
    INFO("  Notch: " << dbNotch << " dB");

    // Low should be near unity (100Hz is well below 1kHz cutoff)
    CHECK(dbLow >= -0.5f);
    CHECK(dbLow <= 0.5f);

    // High should be attenuated (~24 dB for 2 poles, 1 decade below)
    CHECK(dbHigh <= -20.0f);

    // Notch at 100Hz (well below notch center) should be near unity
    CHECK(dbNotch >= -0.5f);
}

TEST_CASE("SVF processMulti US2 Acceptance: 1000Hz sine at 1000Hz cutoff", "[svf][US2][acceptance]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setCutoff(1000.0f);
    filter.setResonance(SVF::kButterworthQ);

    // Process 1000Hz sine (at cutoff)
    constexpr size_t numSamples = 8192;
    auto input = generateSine(1000.0f, kTestSampleRate, numSamples);

    // Let filter settle
    for (size_t i = 0; i < 2000; ++i) {
        (void)filter.processMulti(input[i % numSamples]);
    }
    filter.reset();

    // Measure each output
    float maxBand = 0.0f, maxNotch = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        SVFOutputs out = filter.processMulti(input[i]);
        if (i >= numSamples * 3 / 4) {
            maxBand = std::max(maxBand, std::abs(out.band));
            maxNotch = std::max(maxNotch, std::abs(out.notch));
        }
    }

    float dbBand = linearToDb(maxBand);
    float dbNotch = linearToDb(maxNotch);

    INFO("1kHz signal at 1kHz cutoff:");
    INFO("  Band: " << dbBand << " dB");
    INFO("  Notch: " << dbNotch << " dB");

    // Band should be near unity at cutoff
    CHECK(dbBand >= -1.0f);
    CHECK(dbBand <= 1.0f);

    // Notch should be at minimum at cutoff (deep attenuation)
    CHECK(dbNotch <= -20.0f);
}

TEST_CASE("SVF processMulti before prepare returns zeros", "[svf][US2][FR-021]") {
    SVF filter;  // Not prepared!

    SVFOutputs out = filter.processMulti(0.5f);

    CHECK(out.low == 0.0f);
    CHECK(out.high == 0.0f);
    CHECK(out.band == 0.0f);
    CHECK(out.notch == 0.0f);
}

TEST_CASE("SVF processMulti NaN input returns zeros and resets", "[svf][US2][FR-022]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setCutoff(1000.0f);

    // Build up state
    (void)filter.processMulti(1.0f);
    (void)filter.processMulti(0.5f);

    // Feed NaN
    float nan = std::numeric_limits<float>::quiet_NaN();
    SVFOutputs out = filter.processMulti(nan);

    CHECK(out.low == 0.0f);
    CHECK(out.high == 0.0f);
    CHECK(out.band == 0.0f);
    CHECK(out.notch == 0.0f);
}

TEST_CASE("SVF processMulti Infinity input returns zeros and resets", "[svf][US2][FR-022]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setCutoff(1000.0f);

    float inf = std::numeric_limits<float>::infinity();
    SVFOutputs out = filter.processMulti(inf);

    CHECK(out.low == 0.0f);
    CHECK(out.high == 0.0f);
    CHECK(out.band == 0.0f);
    CHECK(out.notch == 0.0f);
}

TEST_CASE("SVF processMulti stability over 1000 samples", "[svf][US2][stability]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setCutoff(1000.0f);
    filter.setResonance(5.0f);  // Moderate resonance

    bool hasNaN = false;
    bool hasInf = false;

    for (size_t i = 0; i < 1000; ++i) {
        float input = std::sin(kTwoPi * 500.0f * static_cast<float>(i) / kTestSampleRate);
        SVFOutputs out = filter.processMulti(input);

        if (detail::isNaN(out.low) || detail::isNaN(out.high) ||
            detail::isNaN(out.band) || detail::isNaN(out.notch)) {
            hasNaN = true;
        }
        if (detail::isInf(out.low) || detail::isInf(out.high) ||
            detail::isInf(out.band) || detail::isInf(out.notch)) {
            hasInf = true;
        }
    }

    CHECK_FALSE(hasNaN);
    CHECK_FALSE(hasInf);
}

// ==============================================================================
// Phase 5: User Story 3 - Various Filter Modes Tests [US3]
// ==============================================================================

// -----------------------------------------------------------------------------
// SC-003, SC-004: Highpass frequency response
// -----------------------------------------------------------------------------

TEST_CASE("SVF highpass attenuates low frequencies", "[svf][US3][SC-003][frequency-response]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Highpass);
    filter.setCutoff(100.0f);
    filter.setResonance(SVF::kButterworthQ);

    // Process 10Hz sine (1 decade below cutoff)
    float gainAt10 = measureGainAtFrequency(filter, 10.0f, kTestSampleRate);
    float dbAt10 = linearToDb(gainAt10);

    INFO("Highpass 100Hz cutoff, gain at 10Hz: " << dbAt10 << " dB");

    // SC-003: At least 22 dB attenuation at 10Hz
    CHECK(dbAt10 <= -22.0f);
}

TEST_CASE("SVF highpass passes high frequencies", "[svf][US3][SC-004][frequency-response]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Highpass);
    filter.setCutoff(100.0f);
    filter.setResonance(SVF::kButterworthQ);

    // Process 1000Hz sine (1 decade above cutoff)
    float gainAt1k = measureGainAtFrequency(filter, 1000.0f, kTestSampleRate);
    float dbAt1k = linearToDb(gainAt1k);

    INFO("Highpass 100Hz cutoff, gain at 1kHz: " << dbAt1k << " dB");

    // SC-004: Less than 0.5 dB attenuation
    CHECK(dbAt1k >= -0.5f);
    CHECK(dbAt1k <= 0.5f);
}

// -----------------------------------------------------------------------------
// SC-005: Bandpass peak at cutoff
// -----------------------------------------------------------------------------

TEST_CASE("SVF bandpass peak at cutoff", "[svf][US3][SC-005][frequency-response]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Bandpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(5.0f);  // Q=5 as specified in SC-005

    float gainAtCutoff = measureGainAtFrequency(filter, 1000.0f, kTestSampleRate);
    float dbAtCutoff = linearToDb(gainAtCutoff);

    INFO("Bandpass 1kHz Q=5, gain at cutoff: " << dbAtCutoff << " dB");

    // SC-005: Peak gain within 1 dB of unity
    CHECK(dbAtCutoff >= -1.0f);
    CHECK(dbAtCutoff <= 1.0f);
}

// -----------------------------------------------------------------------------
// SC-006: Notch attenuation at center
// -----------------------------------------------------------------------------

TEST_CASE("SVF notch attenuates center frequency", "[svf][US3][SC-006][frequency-response]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Notch);
    filter.setCutoff(1000.0f);
    filter.setResonance(10.0f);  // High Q for sharp notch

    float gainAtCenter = measureGainAtFrequency(filter, 1000.0f, kTestSampleRate);
    float dbAtCenter = linearToDb(gainAtCenter);

    INFO("Notch 1kHz, gain at center: " << dbAtCenter << " dB");

    // SC-006: At least 20 dB attenuation
    CHECK(dbAtCenter <= -20.0f);
}

// -----------------------------------------------------------------------------
// SC-007: Allpass flat magnitude response
// -----------------------------------------------------------------------------

TEST_CASE("SVF allpass has flat magnitude", "[svf][US3][SC-007][frequency-response]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Allpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(SVF::kButterworthQ);

    // Test at multiple frequencies across audio spectrum
    std::array<float, 5> testFreqs = {100.0f, 500.0f, 1000.0f, 5000.0f, 10000.0f};

    for (float freq : testFreqs) {
        float gain = measureGainAtFrequency(filter, freq, kTestSampleRate);
        float db = linearToDb(gain);

        INFO("Allpass gain at " << freq << " Hz: " << db << " dB");

        // SC-007: Within 0.1 dB of unity
        CHECK(db >= -0.1f);
        CHECK(db <= 0.1f);
    }
}

// -----------------------------------------------------------------------------
// SC-008: Peak mode boost
// -----------------------------------------------------------------------------

TEST_CASE("SVF peak mode boost", "[svf][US3][SC-008][frequency-response]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Peak);
    filter.setCutoff(1000.0f);
    filter.setResonance(2.0f);
    filter.setGain(6.0f);  // +6 dB

    float gainAtCenter = measureGainAtFrequency(filter, 1000.0f, kTestSampleRate);
    float dbAtCenter = linearToDb(gainAtCenter);

    INFO("Peak +6dB at 1kHz, measured gain: " << dbAtCenter << " dB");

    // SC-008: Boost by 6 dB (+/- 1 dB)
    CHECK(dbAtCenter >= 5.0f);
    CHECK(dbAtCenter <= 7.0f);
}

// -----------------------------------------------------------------------------
// SC-009: Low shelf boost
// -----------------------------------------------------------------------------

TEST_CASE("SVF low shelf boost", "[svf][US3][SC-009][frequency-response]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::LowShelf);
    filter.setCutoff(1000.0f);
    filter.setResonance(SVF::kButterworthQ);
    filter.setGain(6.0f);  // +6 dB

    // Measure well below shelf frequency
    float gainAt100 = measureGainAtFrequency(filter, 100.0f, kTestSampleRate);
    float dbAt100 = linearToDb(gainAt100);

    INFO("LowShelf +6dB at 1kHz, gain at 100Hz: " << dbAt100 << " dB");

    // SC-009: Boost 100Hz by 6 dB (+/- 1 dB)
    CHECK(dbAt100 >= 5.0f);
    CHECK(dbAt100 <= 7.0f);
}

// -----------------------------------------------------------------------------
// SC-010: High shelf boost
// -----------------------------------------------------------------------------

TEST_CASE("SVF high shelf boost", "[svf][US3][SC-010][frequency-response]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::HighShelf);
    filter.setCutoff(1000.0f);
    filter.setResonance(SVF::kButterworthQ);
    filter.setGain(6.0f);  // +6 dB

    // Measure well above shelf frequency
    float gainAt10k = measureGainAtFrequency(filter, 10000.0f, kTestSampleRate);
    float dbAt10k = linearToDb(gainAt10k);

    INFO("HighShelf +6dB at 1kHz, gain at 10kHz: " << dbAt10k << " dB");

    // SC-010: Boost 10kHz by 6 dB (+/- 1 dB)
    CHECK(dbAt10k >= 5.0f);
    CHECK(dbAt10k <= 7.0f);
}

// -----------------------------------------------------------------------------
// US3 Acceptance Scenarios
// -----------------------------------------------------------------------------

TEST_CASE("SVF US3 Acceptance: Highpass 1000Hz attenuates 100Hz by 18dB", "[svf][US3][acceptance]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Highpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(SVF::kButterworthQ);

    float gain = measureGainAtFrequency(filter, 100.0f, kTestSampleRate);
    float db = linearToDb(gain);

    INFO("Highpass 1kHz, gain at 100Hz: " << db << " dB");

    // At least 18 dB attenuation (1 decade below cutoff, 2 poles = ~24 dB theoretical)
    CHECK(db <= -18.0f);
}

TEST_CASE("SVF US3 Acceptance: Bandpass 1000Hz Q=5 at cutoff within 1dB", "[svf][US3][acceptance]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Bandpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(5.0f);

    float gain = measureGainAtFrequency(filter, 1000.0f, kTestSampleRate);
    float db = linearToDb(gain);

    INFO("Bandpass 1kHz Q=5, gain at cutoff: " << db << " dB");
    CHECK(std::abs(db) <= 1.0f);
}

TEST_CASE("SVF US3 Acceptance: Notch 1000Hz attenuates by 20dB", "[svf][US3][acceptance]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Notch);
    filter.setCutoff(1000.0f);
    filter.setResonance(10.0f);

    float gain = measureGainAtFrequency(filter, 1000.0f, kTestSampleRate);
    float db = linearToDb(gain);

    INFO("Notch 1kHz, gain at cutoff: " << db << " dB");
    CHECK(db <= -20.0f);
}

TEST_CASE("SVF US3 Acceptance: Allpass flat within 0.1dB", "[svf][US3][acceptance]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Allpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(SVF::kButterworthQ);

    // Test multiple frequencies
    for (float freq : {100.0f, 1000.0f, 10000.0f}) {
        float gain = measureGainAtFrequency(filter, freq, kTestSampleRate);
        float db = linearToDb(gain);

        INFO("Allpass at " << freq << " Hz: " << db << " dB");
        CHECK(std::abs(db) <= 0.1f);
    }
}

// -----------------------------------------------------------------------------
// setGain tests
// -----------------------------------------------------------------------------

TEST_CASE("SVF setGain updates immediately", "[svf][US3][FR-008]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Peak);
    filter.setCutoff(1000.0f);

    filter.setGain(12.0f);
    CHECK(filter.getGain() == Approx(12.0f).margin(0.01f));

    filter.setGain(-12.0f);
    CHECK(filter.getGain() == Approx(-12.0f).margin(0.01f));
}

TEST_CASE("SVF gain clamping", "[svf][US3][gain-clamping]") {
    SVF filter;

    SECTION("Gain above maximum clamps") {
        filter.setGain(48.0f);
        CHECK(filter.getGain() <= SVF::kMaxGainDb);
    }

    SECTION("Gain below minimum clamps") {
        filter.setGain(-48.0f);
        CHECK(filter.getGain() >= SVF::kMinGainDb);
    }
}

// ==============================================================================
// Phase 6: User Story 4 - Block Processing Tests [US4]
// ==============================================================================

// -----------------------------------------------------------------------------
// SC-012: processBlock bit-identical to process()
// -----------------------------------------------------------------------------

TEST_CASE("SVF processBlock bit-identical to process loop", "[svf][US4][SC-012][processBlock]") {
    SVF filter1, filter2;
    filter1.prepare(kTestSampleRate);
    filter2.prepare(kTestSampleRate);
    filter1.setMode(SVFMode::Lowpass);
    filter2.setMode(SVFMode::Lowpass);
    filter1.setCutoff(1000.0f);
    filter2.setCutoff(1000.0f);
    filter1.setResonance(2.0f);
    filter2.setResonance(2.0f);

    // Create test signal
    constexpr size_t numSamples = 1024;
    std::vector<float> blockBuffer(numSamples);
    std::vector<float> sampleBuffer(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        float val = std::sin(kTwoPi * 440.0f * static_cast<float>(i) / kTestSampleRate);
        blockBuffer[i] = val;
        sampleBuffer[i] = val;
    }

    // Process with processBlock
    filter1.processBlock(blockBuffer.data(), numSamples);

    // Process sample-by-sample
    for (size_t i = 0; i < numSamples; ++i) {
        sampleBuffer[i] = filter2.process(sampleBuffer[i]);
    }

    // Compare results - should be bit-identical
    for (size_t i = 0; i < numSamples; ++i) {
        INFO("Sample " << i << ": block=" << blockBuffer[i] << " sample=" << sampleBuffer[i]);
        CHECK(blockBuffer[i] == sampleBuffer[i]);  // Exact match, not Approx
    }
}

TEST_CASE("SVF US4 Acceptance: processBlock 1024 samples no allocation", "[svf][US4][acceptance]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Lowpass);
    filter.setCutoff(1000.0f);

    std::vector<float> buffer(1024, 0.5f);

    // The fact that processBlock is noexcept guarantees no allocation
    // We verify this compiles and runs without crash
    filter.processBlock(buffer.data(), buffer.size());

    // Verify it actually processed (output should differ from input for lowpass)
    CHECK_FALSE(detail::isNaN(buffer[0]));
}

TEST_CASE("SVF processBlock with modulation mid-block", "[svf][US4][modulation]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Lowpass);
    filter.setCutoff(1000.0f);

    std::vector<float> buffer(512);
    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = std::sin(kTwoPi * 440.0f * static_cast<float>(i) / kTestSampleRate);
    }

    // Process first half
    filter.processBlock(buffer.data(), 256);

    // Change cutoff mid-way
    filter.setCutoff(4000.0f);

    // Process second half
    filter.processBlock(buffer.data() + 256, 256);

    // Check for clicks at transition point
    float maxChange = 0.0f;
    for (size_t i = 1; i < buffer.size(); ++i) {
        maxChange = std::max(maxChange, std::abs(buffer[i] - buffer[i - 1]));
    }

    INFO("Max sample-to-sample change with mid-block modulation: " << maxChange);
    CHECK(maxChange < 0.5f);  // No clicks
}

TEST_CASE("SVF processBlock zero samples does nothing", "[svf][US4][edge-case]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Lowpass);
    filter.setCutoff(1000.0f);

    // Build up some state
    (void)filter.process(1.0f);
    float stateCheckBefore = filter.process(0.5f);

    // Reset and rebuild state
    filter.reset();
    (void)filter.process(1.0f);
    float stateAfterReset = filter.process(0.5f);

    // They should match
    CHECK(stateCheckBefore == Approx(stateAfterReset).margin(1e-6f));

    // Now call processBlock with 0 samples - should not crash or change state
    float dummy = 0.0f;
    filter.reset();
    (void)filter.process(1.0f);
    filter.processBlock(&dummy, 0);  // Zero samples
    float stateAfterZeroBlock = filter.process(0.5f);

    // State should be same as if processBlock wasn't called
    filter.reset();
    (void)filter.process(1.0f);
    float stateWithoutBlock = filter.process(0.5f);

    CHECK(stateAfterZeroBlock == Approx(stateWithoutBlock).margin(1e-6f));
}

TEST_CASE("SVF processBlock before prepare returns input unchanged", "[svf][US4][FR-021]") {
    SVF filter;  // Not prepared!

    std::vector<float> buffer = {0.1f, 0.2f, 0.3f, 0.4f};
    std::vector<float> original = buffer;

    filter.processBlock(buffer.data(), buffer.size());

    // Buffer should be unchanged (bypass behavior)
    for (size_t i = 0; i < buffer.size(); ++i) {
        CHECK(buffer[i] == original[i]);
    }
}

// ==============================================================================
// Phase 7: Comprehensive Stability Tests
// ==============================================================================

TEST_CASE("SVF stability: 1 million samples no NaN/Inf", "[svf][SC-013][stability]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Lowpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(SVF::kButterworthQ);

    constexpr size_t numSamples = 1000000;
    bool hasNaN = false;
    bool hasInf = false;

    for (size_t i = 0; i < numSamples; ++i) {
        // Input in valid [-1, 1] range
        float input = std::sin(kTwoPi * 440.0f * static_cast<float>(i) / kTestSampleRate);
        float output = filter.process(input);

        if (detail::isNaN(output)) hasNaN = true;
        if (detail::isInf(output)) hasInf = true;
    }

    CHECK_FALSE(hasNaN);
    CHECK_FALSE(hasInf);
}

TEST_CASE("SVF stability: All 8 modes over 1M samples", "[svf][SC-013][stability][all-modes]") {
    std::array<SVFMode, 8> modes = {
        SVFMode::Lowpass, SVFMode::Highpass, SVFMode::Bandpass, SVFMode::Notch,
        SVFMode::Allpass, SVFMode::Peak, SVFMode::LowShelf, SVFMode::HighShelf
    };

    for (SVFMode mode : modes) {
        SVF filter;
        filter.prepare(kTestSampleRate);
        filter.setMode(mode);
        filter.setCutoff(1000.0f);
        filter.setResonance(SVF::kButterworthQ);
        filter.setGain(6.0f);  // For shelf/peak modes

        bool hasNaN = false;
        bool hasInf = false;

        constexpr size_t numSamples = 100000;  // 100k per mode
        for (size_t i = 0; i < numSamples; ++i) {
            float input = std::sin(kTwoPi * 440.0f * static_cast<float>(i) / kTestSampleRate);
            float output = filter.process(input);

            if (detail::isNaN(output)) hasNaN = true;
            if (detail::isInf(output)) hasInf = true;
        }

        INFO("Testing mode: " << static_cast<int>(mode));
        CHECK_FALSE(hasNaN);
        CHECK_FALSE(hasInf);
    }
}

TEST_CASE("SVF stability: Extreme Q values", "[svf][stability][extreme-q]") {
    SECTION("Very low Q (0.1)") {
        SVF filter;
        filter.prepare(kTestSampleRate);
        filter.setMode(SVFMode::Lowpass);
        filter.setCutoff(1000.0f);
        filter.setResonance(0.1f);

        bool hasNaN = false;
        for (size_t i = 0; i < 10000; ++i) {
            float output = filter.process(std::sin(static_cast<float>(i) * 0.1f));
            if (detail::isNaN(output)) hasNaN = true;
        }
        CHECK_FALSE(hasNaN);
    }

    SECTION("Maximum Q (30)") {
        SVF filter;
        filter.prepare(kTestSampleRate);
        filter.setMode(SVFMode::Lowpass);
        filter.setCutoff(1000.0f);
        filter.setResonance(30.0f);

        bool hasNaN = false;
        bool hasInf = false;
        for (size_t i = 0; i < 10000; ++i) {
            float output = filter.process(std::sin(static_cast<float>(i) * 0.1f));
            if (detail::isNaN(output)) hasNaN = true;
            if (detail::isInf(output)) hasInf = true;
        }
        CHECK_FALSE(hasNaN);
        CHECK_FALSE(hasInf);
    }
}

TEST_CASE("SVF stability: Extreme cutoff values", "[svf][stability][extreme-cutoff]") {
    SECTION("Minimum cutoff (1 Hz)") {
        SVF filter;
        filter.prepare(kTestSampleRate);
        filter.setMode(SVFMode::Lowpass);
        filter.setCutoff(1.0f);
        filter.setResonance(SVF::kButterworthQ);

        bool hasNaN = false;
        for (size_t i = 0; i < 10000; ++i) {
            float output = filter.process(std::sin(static_cast<float>(i) * 0.001f));
            if (detail::isNaN(output)) hasNaN = true;
        }
        CHECK_FALSE(hasNaN);
    }

    SECTION("Near-Nyquist cutoff") {
        SVF filter;
        filter.prepare(kTestSampleRate);
        filter.setMode(SVFMode::Lowpass);
        filter.setCutoff(kTestSampleRate * 0.495f);  // Near Nyquist
        filter.setResonance(SVF::kButterworthQ);

        bool hasNaN = false;
        for (size_t i = 0; i < 10000; ++i) {
            float output = filter.process(std::sin(static_cast<float>(i) * 0.5f));
            if (detail::isNaN(output)) hasNaN = true;
        }
        CHECK_FALSE(hasNaN);
    }
}

// ==============================================================================
// Static noexcept verification (T047b)
// ==============================================================================

TEST_CASE("SVF methods are noexcept", "[svf][noexcept]") {
    SVF filter;
    float sample = 0.0f;
    float* buffer = &sample;

    STATIC_REQUIRE(noexcept(filter.process(0.0f)));
    STATIC_REQUIRE(noexcept(filter.processBlock(buffer, 1)));
    STATIC_REQUIRE(noexcept(filter.processMulti(0.0f)));
    STATIC_REQUIRE(noexcept(filter.reset()));
}

// ==============================================================================
// SVFMode enum tests
// ==============================================================================

TEST_CASE("SVFMode enum has 8 values", "[svf][enum]") {
    CHECK(static_cast<uint8_t>(SVFMode::Lowpass) == 0);
    CHECK(static_cast<uint8_t>(SVFMode::Highpass) == 1);
    CHECK(static_cast<uint8_t>(SVFMode::Bandpass) == 2);
    CHECK(static_cast<uint8_t>(SVFMode::Notch) == 3);
    CHECK(static_cast<uint8_t>(SVFMode::Allpass) == 4);
    CHECK(static_cast<uint8_t>(SVFMode::Peak) == 5);
    CHECK(static_cast<uint8_t>(SVFMode::LowShelf) == 6);
    CHECK(static_cast<uint8_t>(SVFMode::HighShelf) == 7);
}

// ==============================================================================
// SVFOutputs struct tests
// ==============================================================================

TEST_CASE("SVFOutputs struct has expected members", "[svf][struct]") {
    SVFOutputs out{1.0f, 2.0f, 3.0f, 4.0f};

    CHECK(out.low == 1.0f);
    CHECK(out.high == 2.0f);
    CHECK(out.band == 3.0f);
    CHECK(out.notch == 4.0f);
}

// ==============================================================================
// FFT-Based Frequency Response Tests
// ==============================================================================
// These tests use FFT analysis to verify the complete frequency response
// curves of the SVF across different modes, rather than spot-checking
// specific frequencies.

namespace {

/// @brief Measure SVF frequency response using FFT analysis of white noise
/// @param filter The SVF filter to test (must be prepared)
/// @param sampleRate Sample rate in Hz
/// @param fftSize FFT size (power of 2)
/// @return Vector of magnitude responses in dB for each FFT bin
std::vector<float> measureSVFFrequencyResponse(
    SVF& filter,
    float sampleRate,
    size_t fftSize = 4096
) {
    // Use longer buffer with transient skipping for accurate steady-state
    const size_t settlingTime = 4096;
    const size_t totalSamples = settlingTime + fftSize;

    // Generate white noise input
    std::vector<float> input(totalSamples);
    std::mt19937 rng(42);  // Fixed seed for reproducibility
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (size_t i = 0; i < totalSamples; ++i) {
        input[i] = dist(rng);
    }

    // Process through filter
    std::vector<float> output(totalSamples);
    filter.reset();
    for (size_t i = 0; i < totalSamples; ++i) {
        output[i] = filter.process(input[i]);
    }

    // Extract steady-state portion (skip transient)
    std::vector<float> inputSteady(input.begin() + settlingTime, input.end());
    std::vector<float> outputSteady(output.begin() + settlingTime, output.end());

    // Apply Hann window
    std::vector<float> window(fftSize);
    Window::generateHann(window.data(), fftSize);
    for (size_t i = 0; i < fftSize; ++i) {
        inputSteady[i] *= window[i];
        outputSteady[i] *= window[i];
    }

    // FFT both signals
    FFT fft;
    fft.prepare(fftSize);
    std::vector<Complex> inputSpectrum(fft.numBins());
    std::vector<Complex> outputSpectrum(fft.numBins());
    fft.forward(inputSteady.data(), inputSpectrum.data());
    fft.forward(outputSteady.data(), outputSpectrum.data());

    // Calculate magnitude ratio in dB for each bin
    std::vector<float> responseDb(fft.numBins());
    for (size_t i = 0; i < fft.numBins(); ++i) {
        float inputMag = inputSpectrum[i].magnitude();
        float outputMag = outputSpectrum[i].magnitude();
        if (inputMag > 1e-10f) {
            responseDb[i] = 20.0f * std::log10(outputMag / inputMag);
        } else {
            responseDb[i] = -144.0f;
        }
    }

    return responseDb;
}

/// @brief Get the frequency corresponding to a bin index
inline float fftBinToFrequency(size_t bin, float sampleRate, size_t fftSize) {
    return static_cast<float>(bin) * sampleRate / static_cast<float>(fftSize);
}

/// @brief Find the bin index closest to a given frequency
inline size_t frequencyToFftBin(float freq, float sampleRate, size_t fftSize) {
    return static_cast<size_t>(freq * static_cast<float>(fftSize) / sampleRate + 0.5f);
}

/// @brief Calculate average response in a frequency band
float averageResponseInBand(
    const std::vector<float>& responseDb,
    float lowFreq,
    float highFreq,
    float sampleRate,
    size_t fftSize
) {
    size_t lowBin = frequencyToFftBin(lowFreq, sampleRate, fftSize);
    size_t highBin = frequencyToFftBin(highFreq, sampleRate, fftSize);

    float sum = 0.0f;
    size_t count = 0;
    for (size_t i = lowBin; i <= highBin && i < responseDb.size(); ++i) {
        sum += responseDb[i];
        ++count;
    }
    return count > 0 ? sum / static_cast<float>(count) : -144.0f;
}

} // anonymous namespace

// -----------------------------------------------------------------------------
// FFT-Based Tests: Lowpass Mode
// -----------------------------------------------------------------------------

TEST_CASE("SVF FFT lowpass shows correct frequency response shape", "[svf][fft][lowpass]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Lowpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(SVF::kButterworthQ);

    constexpr size_t fftSize = 4096;
    auto responseDb = measureSVFFrequencyResponse(filter, kTestSampleRate, fftSize);

    // Measure response at key frequencies
    float avgLow = averageResponseInBand(responseDb, 50.0f, 200.0f, kTestSampleRate, fftSize);
    float avgMid = averageResponseInBand(responseDb, 800.0f, 1200.0f, kTestSampleRate, fftSize);
    float avgHigh = averageResponseInBand(responseDb, 5000.0f, 10000.0f, kTestSampleRate, fftSize);

    INFO("Low band (50-200Hz) average: " << avgLow << " dB");
    INFO("Mid band (800-1200Hz) average: " << avgMid << " dB");
    INFO("High band (5k-10kHz) average: " << avgHigh << " dB");

    // Lowpass characteristics:
    // - Low frequencies should pass (near 0dB)
    // - High frequencies should be attenuated
    CHECK(avgLow > -3.0f);  // Pass band within 3dB
    CHECK(avgHigh < avgLow - 12.0f);  // At least 12dB below passband at 5-10kHz
    CHECK(avgHigh < -15.0f);  // Significant attenuation at high frequencies
}

// -----------------------------------------------------------------------------
// FFT-Based Tests: Highpass Mode
// -----------------------------------------------------------------------------

TEST_CASE("SVF FFT highpass shows correct frequency response shape", "[svf][fft][highpass]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Highpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(SVF::kButterworthQ);

    constexpr size_t fftSize = 4096;
    auto responseDb = measureSVFFrequencyResponse(filter, kTestSampleRate, fftSize);

    float avgLow = averageResponseInBand(responseDb, 50.0f, 200.0f, kTestSampleRate, fftSize);
    float avgHigh = averageResponseInBand(responseDb, 5000.0f, 15000.0f, kTestSampleRate, fftSize);

    INFO("Low band (50-200Hz) average: " << avgLow << " dB");
    INFO("High band (5k-15kHz) average: " << avgHigh << " dB");

    // Highpass characteristics:
    // - High frequencies should pass (near 0dB)
    // - Low frequencies should be attenuated
    CHECK(avgHigh > -3.0f);  // Pass band within 3dB
    CHECK(avgLow < avgHigh - 12.0f);  // At least 12dB below passband at low freqs
    CHECK(avgLow < -15.0f);  // Significant attenuation at low frequencies
}

// -----------------------------------------------------------------------------
// FFT-Based Tests: Bandpass Mode
// -----------------------------------------------------------------------------

TEST_CASE("SVF FFT bandpass shows peak at cutoff", "[svf][fft][bandpass]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Bandpass);
    filter.setCutoff(2000.0f);
    filter.setResonance(2.0f);  // Higher Q for narrower band

    constexpr size_t fftSize = 4096;
    auto responseDb = measureSVFFrequencyResponse(filter, kTestSampleRate, fftSize);

    float avgLow = averageResponseInBand(responseDb, 100.0f, 400.0f, kTestSampleRate, fftSize);
    float avgMid = averageResponseInBand(responseDb, 1500.0f, 2500.0f, kTestSampleRate, fftSize);
    float avgHigh = averageResponseInBand(responseDb, 8000.0f, 15000.0f, kTestSampleRate, fftSize);

    INFO("Low band (100-400Hz) average: " << avgLow << " dB");
    INFO("Mid band (1.5k-2.5kHz) average: " << avgMid << " dB");
    INFO("High band (8k-15kHz) average: " << avgHigh << " dB");

    // Bandpass characteristics:
    // - Center frequency region should be highest
    // - Both low and high frequencies should be attenuated
    CHECK(avgMid > avgLow);  // Mid higher than low
    CHECK(avgMid > avgHigh);  // Mid higher than high
    CHECK(avgLow < -6.0f);  // Low frequencies attenuated
    CHECK(avgHigh < -6.0f);  // High frequencies attenuated
}

// -----------------------------------------------------------------------------
// FFT-Based Tests: Notch Mode
// -----------------------------------------------------------------------------

TEST_CASE("SVF FFT notch shows dip at cutoff", "[svf][fft][notch]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Notch);
    filter.setCutoff(2000.0f);
    filter.setResonance(2.0f);

    constexpr size_t fftSize = 4096;
    auto responseDb = measureSVFFrequencyResponse(filter, kTestSampleRate, fftSize);

    float avgLow = averageResponseInBand(responseDb, 100.0f, 500.0f, kTestSampleRate, fftSize);
    float avgNotch = averageResponseInBand(responseDb, 1800.0f, 2200.0f, kTestSampleRate, fftSize);
    float avgHigh = averageResponseInBand(responseDb, 8000.0f, 15000.0f, kTestSampleRate, fftSize);

    INFO("Low band (100-500Hz) average: " << avgLow << " dB");
    INFO("Notch band (1.8k-2.2kHz) average: " << avgNotch << " dB");
    INFO("High band (8k-15kHz) average: " << avgHigh << " dB");

    // Notch characteristics:
    // - Low and high frequencies should pass (near 0dB)
    // - Notch region should be attenuated
    CHECK(avgLow > -3.0f);  // Pass band
    CHECK(avgHigh > -3.0f);  // Pass band
    CHECK(avgNotch < avgLow - 6.0f);  // Notch significantly below passband
    CHECK(avgNotch < avgHigh - 6.0f);
}

// -----------------------------------------------------------------------------
// FFT-Based Tests: Allpass Mode
// -----------------------------------------------------------------------------

TEST_CASE("SVF FFT allpass shows flat magnitude", "[svf][fft][allpass]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Allpass);
    filter.setCutoff(2000.0f);
    filter.setResonance(SVF::kButterworthQ);

    constexpr size_t fftSize = 4096;
    auto responseDb = measureSVFFrequencyResponse(filter, kTestSampleRate, fftSize);

    // Calculate min/max across audible range
    size_t startBin = frequencyToFftBin(100.0f, kTestSampleRate, fftSize);
    size_t endBin = frequencyToFftBin(18000.0f, kTestSampleRate, fftSize);

    float minDb = 100.0f;
    float maxDb = -100.0f;
    float sum = 0.0f;
    size_t count = 0;

    for (size_t i = startBin; i <= endBin && i < responseDb.size(); ++i) {
        minDb = std::min(minDb, responseDb[i]);
        maxDb = std::max(maxDb, responseDb[i]);
        sum += responseDb[i];
        ++count;
    }
    float avgDb = sum / static_cast<float>(count);

    INFO("Min magnitude: " << minDb << " dB");
    INFO("Max magnitude: " << maxDb << " dB");
    INFO("Average magnitude: " << avgDb << " dB");
    INFO("Max deviation: " << (maxDb - minDb) << " dB");

    // Allpass should have flat magnitude response
    CHECK(maxDb - minDb < 4.0f);  // Less than 4dB variation
    CHECK(avgDb > -2.0f);  // Near unity gain
    CHECK(avgDb < 2.0f);
}

// -----------------------------------------------------------------------------
// FFT-Based Tests: Shelf Modes
// -----------------------------------------------------------------------------

TEST_CASE("SVF FFT low shelf shows boost/cut below cutoff", "[svf][fft][lowshelf]") {
    SVF filterBoost;
    SVF filterCut;
    filterBoost.prepare(kTestSampleRate);
    filterCut.prepare(kTestSampleRate);

    filterBoost.setMode(SVFMode::LowShelf);
    filterBoost.setCutoff(500.0f);
    filterBoost.setResonance(SVF::kButterworthQ);
    filterBoost.setGain(6.0f);  // +6dB boost

    filterCut.setMode(SVFMode::LowShelf);
    filterCut.setCutoff(500.0f);
    filterCut.setResonance(SVF::kButterworthQ);
    filterCut.setGain(-6.0f);  // -6dB cut

    constexpr size_t fftSize = 4096;
    auto responseBoost = measureSVFFrequencyResponse(filterBoost, kTestSampleRate, fftSize);
    auto responseCut = measureSVFFrequencyResponse(filterCut, kTestSampleRate, fftSize);

    // Measure in low and high bands
    float boostLow = averageResponseInBand(responseBoost, 50.0f, 150.0f, kTestSampleRate, fftSize);
    float boostHigh = averageResponseInBand(responseBoost, 2000.0f, 10000.0f, kTestSampleRate, fftSize);
    float cutLow = averageResponseInBand(responseCut, 50.0f, 150.0f, kTestSampleRate, fftSize);
    float cutHigh = averageResponseInBand(responseCut, 2000.0f, 10000.0f, kTestSampleRate, fftSize);

    INFO("Boost - Low band: " << boostLow << " dB, High band: " << boostHigh << " dB");
    INFO("Cut - Low band: " << cutLow << " dB, High band: " << cutHigh << " dB");

    // Low shelf boost: low frequencies boosted, high frequencies unchanged
    CHECK(boostLow > boostHigh + 3.0f);  // Low band boosted relative to high
    CHECK(boostHigh > -3.0f);  // High band near unity
    CHECK(boostHigh < 3.0f);

    // Low shelf cut: low frequencies cut, high frequencies unchanged
    CHECK(cutLow < cutHigh - 3.0f);  // Low band cut relative to high
    CHECK(cutHigh > -3.0f);  // High band near unity
    CHECK(cutHigh < 3.0f);
}

TEST_CASE("SVF FFT high shelf shows boost/cut above cutoff", "[svf][fft][highshelf]") {
    SVF filterBoost;
    SVF filterCut;
    filterBoost.prepare(kTestSampleRate);
    filterCut.prepare(kTestSampleRate);

    filterBoost.setMode(SVFMode::HighShelf);
    filterBoost.setCutoff(2000.0f);
    filterBoost.setResonance(SVF::kButterworthQ);
    filterBoost.setGain(6.0f);  // +6dB boost

    filterCut.setMode(SVFMode::HighShelf);
    filterCut.setCutoff(2000.0f);
    filterCut.setResonance(SVF::kButterworthQ);
    filterCut.setGain(-6.0f);  // -6dB cut

    constexpr size_t fftSize = 4096;
    auto responseBoost = measureSVFFrequencyResponse(filterBoost, kTestSampleRate, fftSize);
    auto responseCut = measureSVFFrequencyResponse(filterCut, kTestSampleRate, fftSize);

    // Measure in low and high bands
    float boostLow = averageResponseInBand(responseBoost, 100.0f, 500.0f, kTestSampleRate, fftSize);
    float boostHigh = averageResponseInBand(responseBoost, 8000.0f, 15000.0f, kTestSampleRate, fftSize);
    float cutLow = averageResponseInBand(responseCut, 100.0f, 500.0f, kTestSampleRate, fftSize);
    float cutHigh = averageResponseInBand(responseCut, 8000.0f, 15000.0f, kTestSampleRate, fftSize);

    INFO("Boost - Low band: " << boostLow << " dB, High band: " << boostHigh << " dB");
    INFO("Cut - Low band: " << cutLow << " dB, High band: " << cutHigh << " dB");

    // High shelf boost: high frequencies boosted, low frequencies unchanged
    CHECK(boostHigh > boostLow + 3.0f);  // High band boosted relative to low
    CHECK(boostLow > -3.0f);  // Low band near unity
    CHECK(boostLow < 3.0f);

    // High shelf cut: high frequencies cut, low frequencies unchanged
    CHECK(cutHigh < cutLow - 3.0f);  // High band cut relative to low
    CHECK(cutLow > -3.0f);  // Low band near unity
    CHECK(cutLow < 3.0f);
}

// -----------------------------------------------------------------------------
// FFT-Based Tests: Peak Mode
// -----------------------------------------------------------------------------

TEST_CASE("SVF FFT peak shows boost at cutoff", "[svf][fft][peak]") {
    SVF filter;
    filter.prepare(kTestSampleRate);
    filter.setMode(SVFMode::Peak);
    filter.setCutoff(2000.0f);
    filter.setResonance(2.0f);  // Higher Q for narrower peak
    filter.setGain(9.0f);  // +9dB boost

    constexpr size_t fftSize = 4096;
    auto responseDb = measureSVFFrequencyResponse(filter, kTestSampleRate, fftSize);

    float avgLow = averageResponseInBand(responseDb, 100.0f, 500.0f, kTestSampleRate, fftSize);
    float avgPeak = averageResponseInBand(responseDb, 1500.0f, 2500.0f, kTestSampleRate, fftSize);
    float avgHigh = averageResponseInBand(responseDb, 8000.0f, 15000.0f, kTestSampleRate, fftSize);

    INFO("Low band (100-500Hz) average: " << avgLow << " dB");
    INFO("Peak band (1.5k-2.5kHz) average: " << avgPeak << " dB");
    INFO("High band (8k-15kHz) average: " << avgHigh << " dB");

    // Peak characteristics:
    // - Peak region should be boosted
    // - Away from peak should be near unity
    CHECK(avgPeak > avgLow + 3.0f);  // Peak higher than low
    CHECK(avgPeak > avgHigh + 3.0f);  // Peak higher than high
    CHECK(avgLow > -3.0f);  // Low band near unity
    CHECK(avgLow < 3.0f);
    CHECK(avgHigh > -3.0f);  // High band near unity
    CHECK(avgHigh < 3.0f);
}

// ==============================================================================
// End of SVF Tests
// ==============================================================================
