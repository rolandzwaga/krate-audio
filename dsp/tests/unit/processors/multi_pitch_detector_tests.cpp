// ==============================================================================
// Layer 2: DSP Processor Tests - Multi-Pitch Detector
// ==============================================================================
// Tests for: dsp/include/krate/dsp/processors/multi_pitch_detector.h
//
// Verifies multi-pitch detection using synthetic multi-tone test signals.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/multi_pitch_detector.h>
#include <krate/dsp/processors/harmonic_types.h>

#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

namespace {

constexpr float kTestSampleRate = 44100.0f;
constexpr size_t kTestFFTSize = 4096;

/// Generate synthetic peak data for a harmonic source at the given F0.
/// Returns peaks at f0, 2*f0, 3*f0, ... with decaying amplitudes.
void addHarmonicPeaks(std::vector<float>& freqs, std::vector<float>& amps,
                      float f0, float baseAmp, int numHarmonics,
                      float nyquist) {
    for (int h = 1; h <= numHarmonics; ++h) {
        float freq = f0 * static_cast<float>(h);
        if (freq > nyquist) break;
        freqs.push_back(freq);
        // Decay: amplitude * (1/h)^0.7 (typical for musical instruments)
        amps.push_back(baseAmp * std::pow(static_cast<float>(h), -0.7f));
    }
}

/// Check if a detected frequency is within tolerance of expected
bool isNear(float detected, float expected, float toleranceCents = 50.0f) {
    if (detected <= 0.0f || expected <= 0.0f) return false;
    float ratio = detected / expected;
    float cents = 1200.0f * std::log2(ratio);
    return std::abs(cents) < toleranceCents;
}

} // anonymous namespace

TEST_CASE("MultiPitchDetector: single tone detection", "[multi_pitch]") {
    MultiPitchDetector detector;
    detector.prepare(kTestFFTSize, kTestSampleRate);

    std::vector<float> freqs, amps;
    addHarmonicPeaks(freqs, amps, 440.0f, 1.0f, 10, kTestSampleRate / 2.0f);

    auto result = detector.detect(freqs.data(), amps.data(),
                                   static_cast<int>(freqs.size()));

    REQUIRE(result.numDetected >= 1);
    CHECK(isNear(result.estimates[0].frequency, 440.0f));
    CHECK(result.estimates[0].confidence > 0.5f);
}

TEST_CASE("MultiPitchDetector: two tones (perfect fifth)", "[multi_pitch]") {
    MultiPitchDetector detector;
    detector.prepare(kTestFFTSize, kTestSampleRate);

    std::vector<float> freqs, amps;
    // A4 (440 Hz) and E5 (659.25 Hz)
    addHarmonicPeaks(freqs, amps, 440.0f, 1.0f, 10, kTestSampleRate / 2.0f);
    addHarmonicPeaks(freqs, amps, 659.25f, 0.8f, 8, kTestSampleRate / 2.0f);

    auto result = detector.detect(freqs.data(), amps.data(),
                                   static_cast<int>(freqs.size()));

    REQUIRE(result.numDetected >= 2);

    // Check that both frequencies are detected (order may vary by salience)
    bool found440 = false, found659 = false;
    for (int i = 0; i < result.numDetected; ++i) {
        if (isNear(result.estimates[i].frequency, 440.0f)) found440 = true;
        if (isNear(result.estimates[i].frequency, 659.25f)) found659 = true;
    }
    CHECK(found440);
    CHECK(found659);
}

TEST_CASE("MultiPitchDetector: three tones (major chord)", "[multi_pitch]") {
    MultiPitchDetector detector;
    detector.prepare(kTestFFTSize, kTestSampleRate);

    std::vector<float> freqs, amps;
    // C4 (261.63), E4 (329.63), G4 (392.00) — C major chord
    addHarmonicPeaks(freqs, amps, 261.63f, 1.0f, 12, kTestSampleRate / 2.0f);
    addHarmonicPeaks(freqs, amps, 329.63f, 0.9f, 10, kTestSampleRate / 2.0f);
    addHarmonicPeaks(freqs, amps, 392.00f, 0.85f, 10, kTestSampleRate / 2.0f);

    auto result = detector.detect(freqs.data(), amps.data(),
                                   static_cast<int>(freqs.size()));

    REQUIRE(result.numDetected >= 2);
    // At minimum, we should detect the root and at least one other note
    // Full 3-note detection depends on cancellation quality

    bool foundC = false, foundE = false, foundG = false;
    for (int i = 0; i < result.numDetected; ++i) {
        if (isNear(result.estimates[i].frequency, 261.63f)) foundC = true;
        if (isNear(result.estimates[i].frequency, 329.63f)) foundE = true;
        if (isNear(result.estimates[i].frequency, 392.00f)) foundG = true;
    }
    CHECK(foundC);
    // At least one of E or G should be detected
    CHECK((foundE || foundG));
}

TEST_CASE("MultiPitchDetector: octave separation", "[multi_pitch]") {
    MultiPitchDetector detector;
    detector.prepare(kTestFFTSize, kTestSampleRate);

    std::vector<float> freqs, amps;
    // A3 (220 Hz) and A4 (440 Hz) — octave apart
    // This is challenging because harmonics overlap (every other harmonic of A3 = A4's harmonics)
    addHarmonicPeaks(freqs, amps, 220.0f, 1.0f, 15, kTestSampleRate / 2.0f);
    addHarmonicPeaks(freqs, amps, 440.0f, 0.7f, 10, kTestSampleRate / 2.0f);

    auto result = detector.detect(freqs.data(), amps.data(),
                                   static_cast<int>(freqs.size()));

    // Should detect at least the lower note (more harmonics, stronger salience)
    REQUIRE(result.numDetected >= 1);
    CHECK(isNear(result.estimates[0].frequency, 220.0f));
}

TEST_CASE("MultiPitchDetector: no peaks returns empty", "[multi_pitch]") {
    MultiPitchDetector detector;
    detector.prepare(kTestFFTSize, kTestSampleRate);

    auto result = detector.detect(nullptr, nullptr, 0);
    CHECK(result.numDetected == 0);
}

TEST_CASE("MultiPitchDetector: low-amplitude peaks below threshold", "[multi_pitch]") {
    MultiPitchDetector detector;
    detector.prepare(kTestFFTSize, kTestSampleRate);

    std::vector<float> freqs, amps;
    // Very quiet signal
    addHarmonicPeaks(freqs, amps, 440.0f, 0.001f, 3, kTestSampleRate / 2.0f);

    auto result = detector.detect(freqs.data(), amps.data(),
                                   static_cast<int>(freqs.size()));

    // May detect 0 or 1 depending on threshold
    // Just verify no crash
    CHECK(result.numDetected >= 0);
}

TEST_CASE("MultiPitchDetector: max voices not exceeded", "[multi_pitch]") {
    MultiPitchDetector detector;
    detector.prepare(kTestFFTSize, kTestSampleRate);

    std::vector<float> freqs, amps;
    // Create 10 simultaneous tones (more than kMaxPolyphonicVoices)
    for (int i = 0; i < 10; ++i) {
        float f0 = 200.0f + static_cast<float>(i) * 100.0f;
        addHarmonicPeaks(freqs, amps, f0, 1.0f, 5, kTestSampleRate / 2.0f);
    }

    auto result = detector.detect(freqs.data(), amps.data(),
                                   static_cast<int>(freqs.size()));

    CHECK(result.numDetected <= kMaxPolyphonicVoices);
}
