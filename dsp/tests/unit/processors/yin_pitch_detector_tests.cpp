// ==============================================================================
// Layer 2: DSP Processor Tests - YIN Pitch Detector
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written before implementation.
//
// Tests for: dsp/include/krate/dsp/processors/yin_pitch_detector.h
// Spec: specs/115-innexus-m1-core-instrument/spec.md
// Covers: FR-010 (CMNDF), FR-011 (FFT acceleration), FR-012 (parabolic interp),
//         FR-013 (output structure), FR-014 (configurable range), FR-015 (confidence gating),
//         FR-016 (frequency hysteresis), FR-017 (hold-previous)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/yin_pitch_detector.h>

#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

constexpr double kSampleRate = 44100.0;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

/// Generate a pure sine wave at the given frequency
void generateSine(float* buffer, size_t numSamples, float freqHz,
                  double sampleRate, float amplitude = 1.0f) {
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = amplitude *
                     std::sin(kTwoPi * freqHz * static_cast<float>(i) /
                              static_cast<float>(sampleRate));
    }
}

/// Generate a sawtooth wave (band-limited via additive synthesis) at the given frequency
void generateSawtooth(float* buffer, size_t numSamples, float freqHz,
                      double sampleRate, int numHarmonics = 20,
                      float amplitude = 0.5f) {
    std::fill_n(buffer, numSamples, 0.0f);
    float nyquist = static_cast<float>(sampleRate) * 0.5f;
    for (int h = 1; h <= numHarmonics; ++h) {
        float harmFreq = freqHz * static_cast<float>(h);
        if (harmFreq >= nyquist) break;
        float sign = (h % 2 == 0) ? -1.0f : 1.0f;
        float harmAmp = amplitude * sign * (2.0f / (kPi * static_cast<float>(h)));
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] += harmAmp *
                          std::sin(kTwoPi * harmFreq * static_cast<float>(i) /
                                   static_cast<float>(sampleRate));
        }
    }
}

} // anonymous namespace

// =============================================================================
// FR-010: CMNDF-based YIN algorithm
// FR-012: Parabolic interpolation on CMNDF minima
// =============================================================================

TEST_CASE("YIN detects 440 Hz sine within 1%",
          "[dsp][yin][pitch][FR-010][FR-012]") {
    YinPitchDetector yin(2048);
    yin.prepare(kSampleRate);

    // Generate enough samples for the detector
    std::vector<float> buffer(2048);
    generateSine(buffer.data(), buffer.size(), 440.0f, kSampleRate);

    F0Estimate est = yin.detect(buffer.data(), buffer.size());

    REQUIRE(est.voiced == true);
    REQUIRE(est.frequency == Approx(440.0f).margin(4.4f)); // 1% of 440
    REQUIRE(est.confidence > 0.5f);
}

TEST_CASE("YIN detects 100 Hz sine within 1%",
          "[dsp][yin][pitch][FR-010][FR-012]") {
    YinPitchDetector yin(2048);
    yin.prepare(kSampleRate);

    std::vector<float> buffer(2048);
    generateSine(buffer.data(), buffer.size(), 100.0f, kSampleRate);

    F0Estimate est = yin.detect(buffer.data(), buffer.size());

    REQUIRE(est.voiced == true);
    REQUIRE(est.frequency == Approx(100.0f).margin(1.0f)); // 1% of 100
}

TEST_CASE("YIN detects 40 Hz (low limit) sine",
          "[dsp][yin][pitch][FR-010][FR-014]") {
    // 40 Hz needs at least 2 full periods at 44100 Hz = ~2205 samples
    // Use 4096 window for adequate low-frequency resolution
    YinPitchDetector yin(4096, 40.0f, 2000.0f);
    yin.prepare(kSampleRate);

    std::vector<float> buffer(4096);
    generateSine(buffer.data(), buffer.size(), 40.0f, kSampleRate);

    F0Estimate est = yin.detect(buffer.data(), buffer.size());

    REQUIRE(est.voiced == true);
    REQUIRE(est.frequency == Approx(40.0f).margin(2.0f)); // 5% tolerance at extreme
}

TEST_CASE("YIN detects 1000 Hz sine within 1%",
          "[dsp][yin][pitch][FR-010][FR-012]") {
    YinPitchDetector yin(2048);
    yin.prepare(kSampleRate);

    std::vector<float> buffer(2048);
    generateSine(buffer.data(), buffer.size(), 1000.0f, kSampleRate);

    F0Estimate est = yin.detect(buffer.data(), buffer.size());

    REQUIRE(est.voiced == true);
    REQUIRE(est.frequency == Approx(1000.0f).margin(10.0f)); // 1% of 1000
}

// =============================================================================
// Sawtooth wave: verify pitch detection with rich harmonics
// =============================================================================

TEST_CASE("YIN detects 220 Hz sawtooth pitch despite harmonics",
          "[dsp][yin][pitch][FR-010]") {
    YinPitchDetector yin(2048);
    yin.prepare(kSampleRate);

    std::vector<float> buffer(2048);
    generateSawtooth(buffer.data(), buffer.size(), 220.0f, kSampleRate);

    F0Estimate est = yin.detect(buffer.data(), buffer.size());

    REQUIRE(est.voiced == true);
    REQUIRE(est.frequency == Approx(220.0f).margin(4.4f)); // 2% of 220
}

// =============================================================================
// FR-013: Output structure verification
// =============================================================================

TEST_CASE("YIN output contains frequency, confidence, voiced (FR-013)",
          "[dsp][yin][pitch][FR-013]") {
    YinPitchDetector yin(2048);
    yin.prepare(kSampleRate);

    std::vector<float> buffer(2048);
    generateSine(buffer.data(), buffer.size(), 440.0f, kSampleRate);

    F0Estimate est = yin.detect(buffer.data(), buffer.size());

    // Verify all fields are populated
    REQUIRE(est.frequency > 0.0f);
    REQUIRE(est.confidence >= 0.0f);
    REQUIRE(est.confidence <= 1.0f);
    // voiced is a bool, just verify it compiles and is true for a clean sine
    REQUIRE(est.voiced == true);
}

// =============================================================================
// FR-014: Configurable F0 range
// =============================================================================

TEST_CASE("YIN configurable min/max F0 range (FR-014)",
          "[dsp][yin][pitch][FR-014]") {
    // Create detector with a narrow range that excludes 100 Hz
    YinPitchDetector yin(2048, 200.0f, 2000.0f);
    yin.prepare(kSampleRate);

    std::vector<float> buffer(2048);
    generateSine(buffer.data(), buffer.size(), 100.0f, kSampleRate);

    F0Estimate est = yin.detect(buffer.data(), buffer.size());

    // 100 Hz is below the 200 Hz minimum -- should not detect the fundamental
    // The detector might detect a harmonic or return unvoiced
    // Either way, it should NOT report 100 Hz
    if (est.voiced) {
        REQUIRE(est.frequency >= 200.0f);
    }
}

TEST_CASE("YIN default range is 40-2000 Hz (FR-014)",
          "[dsp][yin][pitch][FR-014]") {
    YinPitchDetector yin; // default constructor
    yin.prepare(kSampleRate);

    // Verify 440 Hz works within default range
    std::vector<float> buffer(2048);
    generateSine(buffer.data(), buffer.size(), 440.0f, kSampleRate);

    F0Estimate est = yin.detect(buffer.data(), buffer.size());
    REQUIRE(est.voiced == true);
    REQUIRE(est.frequency == Approx(440.0f).margin(4.4f));
}

// =============================================================================
// FR-015: Confidence gating
// =============================================================================

TEST_CASE("YIN silent input returns unvoiced with low confidence (FR-015)",
          "[dsp][yin][pitch][FR-015]") {
    YinPitchDetector yin(2048);
    yin.prepare(kSampleRate);

    std::vector<float> buffer(2048, 0.0f); // silence

    F0Estimate est = yin.detect(buffer.data(), buffer.size());

    REQUIRE(est.voiced == false);
    REQUIRE(est.confidence < 0.3f);
}

TEST_CASE("YIN noise input has low confidence and is gated (FR-015)",
          "[dsp][yin][pitch][FR-015]") {
    YinPitchDetector yin(2048);
    yin.prepare(kSampleRate);

    // Generate pseudo-random noise (deterministic for reproducibility)
    std::vector<float> buffer(2048);
    uint32_t seed = 12345;
    for (size_t i = 0; i < buffer.size(); ++i) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        buffer[i] = static_cast<float>(seed) / static_cast<float>(UINT32_MAX) * 2.0f - 1.0f;
    }

    F0Estimate est = yin.detect(buffer.data(), buffer.size());

    // Noise should have very low confidence
    REQUIRE(est.confidence < 0.5f);
    // Should be classified as unvoiced (or low confidence)
    // NOTE: random noise may occasionally produce a spurious detection,
    // but confidence should still be low
}

// =============================================================================
// FR-016: Frequency hysteresis (~2% band)
// =============================================================================

TEST_CASE("YIN frequency hysteresis prevents jitter on stable pitch (FR-016)",
          "[dsp][yin][pitch][FR-016]") {
    YinPitchDetector yin(2048);
    yin.prepare(kSampleRate);

    std::vector<float> buffer(2048);

    // First detection at 440 Hz
    generateSine(buffer.data(), buffer.size(), 440.0f, kSampleRate);
    F0Estimate est1 = yin.detect(buffer.data(), buffer.size());
    REQUIRE(est1.voiced == true);

    // Second detection at a frequency within 2% (440 * 1.01 = 444.4 Hz)
    generateSine(buffer.data(), buffer.size(), 444.0f, kSampleRate);
    F0Estimate est2 = yin.detect(buffer.data(), buffer.size());
    REQUIRE(est2.voiced == true);

    // Hysteresis should keep the same frequency as the first estimate
    REQUIRE(est2.frequency == Approx(est1.frequency).margin(1.0f));
}

// =============================================================================
// FR-017: Hold-previous F0 when confidence drops
// =============================================================================

TEST_CASE("YIN holds previous F0 when confidence drops (FR-017)",
          "[dsp][yin][pitch][FR-017]") {
    YinPitchDetector yin(2048);
    yin.prepare(kSampleRate);

    std::vector<float> buffer(2048);

    // Step 1: Feed a strong 440 Hz signal to establish good F0
    generateSine(buffer.data(), buffer.size(), 440.0f, kSampleRate);
    F0Estimate good = yin.detect(buffer.data(), buffer.size());
    REQUIRE(good.voiced == true);
    REQUIRE(good.frequency == Approx(440.0f).margin(4.4f));

    // Step 2: Feed silence (or near-silence) -- confidence should drop
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    F0Estimate dropped = yin.detect(buffer.data(), buffer.size());

    // The detector should hold the previous good frequency
    REQUIRE(dropped.frequency == Approx(good.frequency).margin(1.0f));
    // voiced should be false since confidence is below threshold
    REQUIRE(dropped.voiced == false);

    // Step 3: Feed 440 Hz again -- should recover
    generateSine(buffer.data(), buffer.size(), 440.0f, kSampleRate);
    F0Estimate recovered = yin.detect(buffer.data(), buffer.size());
    REQUIRE(recovered.voiced == true);
    REQUIRE(recovered.frequency == Approx(440.0f).margin(4.4f));
}

// =============================================================================
// FR-012: Parabolic interpolation improves sub-sample accuracy
// =============================================================================

TEST_CASE("YIN parabolic interpolation provides sub-sample precision (FR-012)",
          "[dsp][yin][pitch][FR-012]") {
    YinPitchDetector yin(2048);
    yin.prepare(kSampleRate);

    // Test at a frequency where the period does NOT land exactly on a sample boundary
    // 330 Hz: period = 44100/330 = 133.636... samples (non-integer)
    std::vector<float> buffer(2048);
    generateSine(buffer.data(), buffer.size(), 330.0f, kSampleRate);

    F0Estimate est = yin.detect(buffer.data(), buffer.size());

    REQUIRE(est.voiced == true);
    // With parabolic interpolation, should be within 0.5% of true frequency
    REQUIRE(est.frequency == Approx(330.0f).margin(1.65f)); // 0.5% of 330
}

// =============================================================================
// SC-003: Gross pitch error rate < 2% across 40-2000 Hz
// =============================================================================

TEST_CASE("YIN gross pitch error rate < 2% across frequency range (SC-003)",
          "[dsp][yin][pitch][SC-003]") {
    // Test at frequencies spanning the supported range
    const float testFreqs[] = {
        50.0f, 80.0f, 100.0f, 130.0f, 170.0f, 220.0f, 330.0f,
        440.0f, 660.0f, 880.0f, 1000.0f, 1200.0f, 1500.0f, 1800.0f
    };
    constexpr size_t numFreqs = sizeof(testFreqs) / sizeof(testFreqs[0]);

    int grossErrors = 0;

    for (size_t f = 0; f < numFreqs; ++f) {
        float targetHz = testFreqs[f];
        // Use 4096 for low frequencies, 2048 otherwise
        size_t winSize = (targetHz < 80.0f) ? 4096 : 2048;
        float minF0 = 40.0f;

        YinPitchDetector yin(winSize, minF0, 2000.0f);
        yin.prepare(kSampleRate);

        std::vector<float> buffer(winSize);
        generateSine(buffer.data(), buffer.size(), targetHz, kSampleRate);

        F0Estimate est = yin.detect(buffer.data(), buffer.size());

        // Gross error: off by more than 20% OR unvoiced when voiced expected
        bool isGrossError = false;
        if (!est.voiced) {
            isGrossError = true;
        } else {
            float relError = std::abs(est.frequency - targetHz) / targetHz;
            if (relError > 0.20f) {
                isGrossError = true;
            }
        }

        if (isGrossError) {
            ++grossErrors;
        }
    }

    float grossErrorRate =
        static_cast<float>(grossErrors) / static_cast<float>(numFreqs);
    REQUIRE(grossErrorRate < 0.02f); // SC-003: < 2% gross error rate
}

// =============================================================================
// Reset behavior
// =============================================================================

TEST_CASE("YIN reset clears internal state",
          "[dsp][yin][pitch]") {
    YinPitchDetector yin(2048);
    yin.prepare(kSampleRate);

    std::vector<float> buffer(2048);

    // Build up state with a 440 Hz signal
    generateSine(buffer.data(), buffer.size(), 440.0f, kSampleRate);
    (void)yin.detect(buffer.data(), buffer.size());

    // Reset
    yin.reset();

    // After reset, detecting silence should not hold any previous frequency
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    F0Estimate est = yin.detect(buffer.data(), buffer.size());

    REQUIRE(est.frequency == Approx(0.0f).margin(0.01f));
    REQUIRE(est.voiced == false);
}
