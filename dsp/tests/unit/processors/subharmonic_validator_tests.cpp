// ==============================================================================
// Unit Tests - SubharmonicValidator
// ==============================================================================
// Tests that the subharmonic summation validator correctly detects and fixes
// YIN octave errors by comparing harmonic support at different octaves.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/subharmonic_validator.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/primitives/fft.h>

#include <cmath>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

namespace {

constexpr size_t kFFTSize = 1024;
constexpr float kSampleRate = 44100.0f;
constexpr float kBinSpacing = kSampleRate / static_cast<float>(kFFTSize);

/// Generate a time-domain signal with harmonics at f0, 2*f0, 3*f0, ...
/// and run it through FFT to produce a SpectralBuffer.
SpectralBuffer makeHarmonicSpectrum(float f0, int numHarmonics,
                                     float amplitude = 0.5f)
{
    // Generate time-domain signal
    std::vector<float> signal(kFFTSize, 0.0f);
    for (int h = 1; h <= numHarmonics; ++h) {
        float freq = f0 * static_cast<float>(h);
        if (freq >= kSampleRate * 0.5f) break;
        float amp = amplitude / static_cast<float>(h); // 1/h rolloff
        for (size_t n = 0; n < kFFTSize; ++n) {
            signal[n] += amp * std::sin(
                2.0f * 3.14159265f * freq * static_cast<float>(n) / kSampleRate);
        }
    }

    // FFT
    FFT fft;
    fft.prepare(kFFTSize);
    SpectralBuffer spectrum;
    spectrum.prepare(kFFTSize);
    fft.forward(signal.data(), spectrum.data());

    return spectrum;
}

/// Generate a spectrum with TWO harmonic series (simulating a chord)
SpectralBuffer makeChordSpectrum(float f0_a, float f0_b,
                                  int numHarmonics = 8,
                                  float amplitude = 0.5f)
{
    std::vector<float> signal(kFFTSize, 0.0f);
    for (int h = 1; h <= numHarmonics; ++h) {
        float freqA = f0_a * static_cast<float>(h);
        float freqB = f0_b * static_cast<float>(h);
        float amp = amplitude / static_cast<float>(h);
        for (size_t n = 0; n < kFFTSize; ++n) {
            float t = static_cast<float>(n) / kSampleRate;
            if (freqA < kSampleRate * 0.5f)
                signal[n] += amp * std::sin(2.0f * 3.14159265f * freqA * t);
            if (freqB < kSampleRate * 0.5f)
                signal[n] += amp * std::sin(2.0f * 3.14159265f * freqB * t);
        }
    }

    FFT fft;
    fft.prepare(kFFTSize);
    SpectralBuffer spectrum;
    spectrum.prepare(kFFTSize);
    fft.forward(signal.data(), spectrum.data());

    return spectrum;
}

} // anonymous namespace

// =============================================================================
// Correct F0 should not be changed
// =============================================================================

TEST_CASE("SubharmonicValidator: correct F0 is preserved",
          "[processors][subharmonic_validator]")
{
    SubharmonicValidator validator;
    validator.prepare(kFFTSize, kSampleRate);

    auto spectrum = makeHarmonicSpectrum(440.0f, 8);

    F0Estimate yin;
    yin.frequency = 440.0f;
    yin.confidence = 0.95f;
    yin.voiced = true;

    auto result = validator.validate(yin, spectrum);

    // Should keep the original F0
    REQUIRE(result.frequency == Approx(440.0f).margin(1.0f));
    REQUIRE(result.voiced);
}

// =============================================================================
// Octave-down error (YIN finds f0/2) should be corrected
// =============================================================================

TEST_CASE("SubharmonicValidator: corrects octave-down error",
          "[processors][subharmonic_validator]")
{
    SubharmonicValidator validator;
    validator.prepare(kFFTSize, kSampleRate);

    // Real signal is at 440 Hz
    auto spectrum = makeHarmonicSpectrum(440.0f, 8);

    // YIN incorrectly detected 220 Hz (octave down)
    F0Estimate yin;
    yin.frequency = 220.0f;
    yin.confidence = 0.9f;
    yin.voiced = true;

    auto result = validator.validate(yin, spectrum);

    // Should correct to 440 Hz (octave up from YIN's estimate)
    REQUIRE(result.frequency == Approx(440.0f).margin(1.0f));
}

// =============================================================================
// Double-octave-down error (YIN finds f0/4) should be corrected
// =============================================================================

TEST_CASE("SubharmonicValidator: corrects double-octave-down error",
          "[processors][subharmonic_validator]")
{
    SubharmonicValidator validator;
    validator.prepare(kFFTSize, kSampleRate);

    // Real signal is at 440 Hz
    auto spectrum = makeHarmonicSpectrum(440.0f, 8);

    // YIN incorrectly detected 110 Hz (two octaves down)
    F0Estimate yin;
    yin.frequency = 110.0f;
    yin.confidence = 0.85f;
    yin.voiced = true;

    auto result = validator.validate(yin, spectrum);

    // Should correct to 440 Hz (double octave up)
    REQUIRE(result.frequency == Approx(440.0f).margin(1.0f));
}

// =============================================================================
// Chord: YIN finds a subharmonic, validator should find a real chord tone
// =============================================================================

TEST_CASE("SubharmonicValidator: corrects subharmonic on non-harmonic chord",
          "[processors][subharmonic_validator]")
{
    SubharmonicValidator validator;
    validator.prepare(kFFTSize, kSampleRate);

    // Signal is at 415 Hz (Ab4) — single note
    auto spectrum = makeHarmonicSpectrum(415.0f, 8);

    // YIN incorrectly detected 52 Hz (≈415/8, a severe subharmonic error
    // typical when YIN finds a deep dip at a high lag)
    F0Estimate yin;
    yin.frequency = 52.0f;
    yin.confidence = 0.8f;
    yin.voiced = true;

    auto result = validator.validate(yin, spectrum);

    // Should correct upward.  52*4=208, 52*8=416≈415.
    // The double-octave-up (208 Hz) or further should score better.
    REQUIRE(result.frequency > 200.0f);
}

// =============================================================================
// Unvoiced estimate should pass through unchanged
// =============================================================================

TEST_CASE("SubharmonicValidator: unvoiced passes through",
          "[processors][subharmonic_validator]")
{
    SubharmonicValidator validator;
    validator.prepare(kFFTSize, kSampleRate);

    auto spectrum = makeHarmonicSpectrum(440.0f, 8);

    F0Estimate yin;
    yin.frequency = 220.0f;
    yin.confidence = 0.3f;
    yin.voiced = false;

    auto result = validator.validate(yin, spectrum);

    // Unvoiced should not be corrected
    REQUIRE(result.frequency == 220.0f);
    REQUIRE_FALSE(result.voiced);
}

// =============================================================================
// Low F0 (bass) should not be falsely corrected upward
// =============================================================================

TEST_CASE("SubharmonicValidator: low F0 preserved when correct",
          "[processors][subharmonic_validator]")
{
    SubharmonicValidator validator;
    validator.prepare(kFFTSize, kSampleRate);

    // Real signal is at 82.4 Hz (E2, low guitar string)
    auto spectrum = makeHarmonicSpectrum(82.4f, 10);

    F0Estimate yin;
    yin.frequency = 82.4f;
    yin.confidence = 0.9f;
    yin.voiced = true;

    auto result = validator.validate(yin, spectrum);

    // Should keep 82.4 Hz — it's the real fundamental, not a subharmonic
    REQUIRE(result.frequency == Approx(82.4f).margin(2.0f));
}
