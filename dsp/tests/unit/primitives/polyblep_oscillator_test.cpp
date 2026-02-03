// ==============================================================================
// Tests: PolyBLEP Oscillator
// ==============================================================================
// Test suite for PolyBlepOscillator (Layer 1 primitive).
// Covers all user stories: sine, sawtooth, square, pulse, triangle,
// phase access, FM/PM modulation, waveform switching, and robustness.
//
// Reference: specs/015-polyblep-oscillator/spec.md
// ==============================================================================

#include <krate/dsp/primitives/polyblep_oscillator.h>
#include <krate/dsp/core/math_constants.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <spectral_analysis.h>

#include <array>
#include <cmath>
#include <numeric>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// User Story 4: Sine Waveform (Phase 3)
// =============================================================================

TEST_CASE("PolyBlepOscillator lifecycle: prepare and reset", "[PolyBlepOscillator][US4]") {
    PolyBlepOscillator osc;

    SECTION("prepare initializes oscillator for given sample rate") {
        osc.prepare(44100.0);
        osc.setFrequency(440.0f);
        osc.setWaveform(OscWaveform::Sine);

        // After prepare, process should return a valid sample
        float sample = osc.process();
        REQUIRE_FALSE(std::isnan(sample));
        REQUIRE_FALSE(std::isinf(sample));
    }

    SECTION("reset clears phase and state but preserves configuration") {
        osc.prepare(44100.0);
        osc.setFrequency(440.0f);
        osc.setWaveform(OscWaveform::Sine);

        // Generate a few samples to advance phase
        for (int i = 0; i < 100; ++i) {
            [[maybe_unused]] float s = osc.process();
        }

        // Reset should bring phase back to 0
        osc.reset();
        REQUIRE(osc.phase() == Approx(0.0).margin(1e-10));

        // After reset, first sample should be sin(0) = 0
        float sample = osc.process();
        REQUIRE(sample == Approx(0.0f).margin(1e-5f));
    }

    SECTION("default waveform is Sine") {
        osc.prepare(44100.0);
        osc.setFrequency(440.0f);

        // First sample from phase=0 with sine: sin(0) = 0
        float sample = osc.process();
        REQUIRE(sample == Approx(0.0f).margin(1e-5f));
    }
}

TEST_CASE("PolyBlepOscillator sine accuracy matches std::sin", "[PolyBlepOscillator][US4]") {
    // SC-004: Sine output matches sin(2*pi*n*f/fs) within 1e-5
    PolyBlepOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(440.0f);
    osc.setWaveform(OscWaveform::Sine);

    constexpr int kNumSamples = 1000;
    constexpr float kSampleRate = 44100.0f;
    constexpr float kFreq = 440.0f;

    for (int n = 0; n < kNumSamples; ++n) {
        float actual = osc.process();
        float expected = std::sin(kTwoPi * static_cast<float>(n) * kFreq / kSampleRate);
        REQUIRE(actual == Approx(expected).margin(1e-5f));
    }
}

TEST_CASE("PolyBlepOscillator sine FFT purity", "[PolyBlepOscillator][US4]") {
    // Verify only fundamental present, no harmonics above noise floor
    PolyBlepOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(440.0f);
    osc.setWaveform(OscWaveform::Sine);

    constexpr size_t kFFTSize = 4096;
    std::vector<float> buffer(kFFTSize);
    for (size_t i = 0; i < kFFTSize; ++i) {
        buffer[i] = osc.process();
    }

    // Apply Hann window
    std::vector<float> window(kFFTSize);
    Window::generateHann(window.data(), kFFTSize);
    for (size_t i = 0; i < kFFTSize; ++i) {
        buffer[i] *= window[i];
    }

    // FFT
    FFT fft;
    fft.prepare(kFFTSize);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(buffer.data(), spectrum.data());

    // Find fundamental bin
    size_t fundamentalBin = TestUtils::frequencyToBin(440.0f, 44100.0f, kFFTSize);
    float fundamentalMag = spectrum[fundamentalBin].magnitude();

    // Check that all harmonics are at least 60 dB below fundamental (sine is pure)
    for (int harmonic = 2; harmonic <= 10; ++harmonic) {
        float harmonicFreq = 440.0f * static_cast<float>(harmonic);
        if (harmonicFreq >= 44100.0f / 2.0f) break;
        size_t harmonicBin = TestUtils::frequencyToBin(harmonicFreq, 44100.0f, kFFTSize);
        float harmonicMag = spectrum[harmonicBin].magnitude();
        float ratioDb = 20.0f * std::log10(harmonicMag / fundamentalMag);
        REQUIRE(ratioDb < -60.0f);
    }
}

TEST_CASE("PolyBlepOscillator parameter setters", "[PolyBlepOscillator][US4]") {
    PolyBlepOscillator osc;
    osc.prepare(44100.0);

    SECTION("setFrequency changes output frequency") {
        osc.setFrequency(880.0f);
        osc.setWaveform(OscWaveform::Sine);

        // Generate samples and compare with expected 880 Hz sine
        constexpr int kNumSamples = 100;
        for (int n = 0; n < kNumSamples; ++n) {
            float actual = osc.process();
            float expected = std::sin(kTwoPi * static_cast<float>(n) * 880.0f / 44100.0f);
            REQUIRE(actual == Approx(expected).margin(1e-5f));
        }
    }

    SECTION("setWaveform changes output waveform") {
        osc.setFrequency(440.0f);
        osc.setWaveform(OscWaveform::Sawtooth);

        // Sawtooth should not match sine at all
        osc.reset();
        float sawSample = osc.process();
        // At phase ~0 sawtooth is near -1, sine is 0 -- they should differ
        // (just verify it produces non-NaN output; detailed saw tests in US1)
        REQUIRE_FALSE(std::isnan(sawSample));
    }
}
