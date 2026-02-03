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

// =============================================================================
// User Story 1: Band-Limited Sawtooth and Square Waveforms (Phase 4)
// =============================================================================

TEST_CASE("PolyBlepOscillator sawtooth shape and bounds", "[PolyBlepOscillator][US1]") {
    PolyBlepOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(440.0f);
    osc.setWaveform(OscWaveform::Sawtooth);

    // SC-009: All output values within [-1.1, 1.1]
    constexpr int kNumSamples = 10000;
    for (int i = 0; i < kNumSamples; ++i) {
        float sample = osc.process();
        REQUIRE(sample >= -1.1f);
        REQUIRE(sample <= 1.1f);
    }
}

// Helper: measure alias suppression for oscillator waveform via FFT
static float measureAliasSuppression(
    OscWaveform waveform, float freq, float sampleRate, size_t fftSize, int maxHarmonic
) {
    PolyBlepOscillator osc;
    osc.prepare(static_cast<double>(sampleRate));
    osc.setFrequency(freq);
    osc.setWaveform(waveform);

    std::vector<float> buffer(fftSize);
    for (size_t i = 0; i < fftSize; ++i) {
        buffer[i] = osc.process();
    }

    // Apply Hann window
    std::vector<float> window(fftSize);
    Window::generateHann(window.data(), fftSize);
    for (size_t i = 0; i < fftSize; ++i) {
        buffer[i] *= window[i];
    }

    // FFT
    FFT fft;
    fft.prepare(fftSize);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(buffer.data(), spectrum.data());

    // Measure fundamental
    size_t fundamentalBin = TestUtils::frequencyToBin(freq, sampleRate, fftSize);
    float fundamentalMag = spectrum[fundamentalBin].magnitude();
    float fundamentalDb = 20.0f * std::log10(fundamentalMag + 1e-10f);

    // Find worst aliased harmonic, excluding bins too close to intended harmonics
    // (spectral leakage from strong intended harmonics can contaminate alias bins)
    TestUtils::AliasingTestConfig config;
    config.testFrequencyHz = freq;
    config.sampleRate = sampleRate;
    config.fftSize = fftSize;
    config.maxHarmonic = maxHarmonic;

    auto aliasedBins = TestUtils::getAliasedBins(config);
    auto harmonicBins = TestUtils::getHarmonicBins(config);

    float worstAliasDb = -200.0f;
    for (size_t bin : aliasedBins) {
        if (bin >= fft.numBins()) continue;

        // Skip bins that are within 3 bins of a strong intended harmonic
        // (spectral leakage from Hann window)
        bool tooClose = false;
        for (size_t hBin : harmonicBins) {
            if (bin >= hBin - 3 && bin <= hBin + 3) { tooClose = true; break; }
        }
        // Also skip bins near fundamental
        if (bin >= fundamentalBin - 3 && bin <= fundamentalBin + 3) tooClose = true;
        if (tooClose) continue;

        float mag = spectrum[bin].magnitude();
        float db = 20.0f * std::log10(mag + 1e-10f);
        if (db > worstAliasDb) worstAliasDb = db;
    }
    return fundamentalDb - worstAliasDb;
}

TEST_CASE("PolyBlepOscillator sawtooth FFT alias suppression", "[PolyBlepOscillator][US1]") {
    // SC-001: At 1000 Hz / 44100 Hz, alias components >= 40 dB below fundamental
    float suppression = measureAliasSuppression(
        OscWaveform::Sawtooth, 1000.0f, 44100.0f, 8192, 30);
    INFO("Sawtooth alias suppression: " << suppression << " dB");
    REQUIRE(suppression >= 40.0f);
}

TEST_CASE("PolyBlepOscillator square FFT alias suppression", "[PolyBlepOscillator][US1]") {
    // SC-002: At 1000 Hz / 44100 Hz, alias components >= 40 dB below fundamental
    float suppression = measureAliasSuppression(
        OscWaveform::Square, 1000.0f, 44100.0f, 8192, 30);
    INFO("Square alias suppression: " << suppression << " dB");
    REQUIRE(suppression >= 40.0f);
}

TEST_CASE("PolyBlepOscillator processBlock matches sequential process", "[PolyBlepOscillator][US1]") {
    // SC-008: processBlock(output, N) produces identical output to N sequential process() calls

    SECTION("Sawtooth") {
        PolyBlepOscillator osc1, osc2;
        osc1.prepare(44100.0);
        osc2.prepare(44100.0);
        osc1.setFrequency(440.0f);
        osc2.setFrequency(440.0f);
        osc1.setWaveform(OscWaveform::Sawtooth);
        osc2.setWaveform(OscWaveform::Sawtooth);

        constexpr size_t kN = 512;
        std::array<float, kN> blockOutput{};
        std::array<float, kN> singleOutput{};

        osc1.processBlock(blockOutput.data(), kN);
        for (size_t i = 0; i < kN; ++i) {
            singleOutput[i] = osc2.process();
        }

        for (size_t i = 0; i < kN; ++i) {
            REQUIRE(blockOutput[i] == Approx(singleOutput[i]).margin(1e-7f));
        }
    }

    SECTION("Square") {
        PolyBlepOscillator osc1, osc2;
        osc1.prepare(44100.0);
        osc2.prepare(44100.0);
        osc1.setFrequency(440.0f);
        osc2.setFrequency(440.0f);
        osc1.setWaveform(OscWaveform::Square);
        osc2.setWaveform(OscWaveform::Square);

        constexpr size_t kN = 512;
        std::array<float, kN> blockOutput{};
        std::array<float, kN> singleOutput{};

        osc1.processBlock(blockOutput.data(), kN);
        for (size_t i = 0; i < kN; ++i) {
            singleOutput[i] = osc2.process();
        }

        for (size_t i = 0; i < kN; ++i) {
            REQUIRE(blockOutput[i] == Approx(singleOutput[i]).margin(1e-7f));
        }
    }
}

TEST_CASE("PolyBlepOscillator output bounds at various frequencies", "[PolyBlepOscillator][US1]") {
    // SC-009: All output values in [-1.1, 1.1] across various frequencies
    PolyBlepOscillator osc;
    osc.prepare(44100.0);

    const float freqs[] = {100.0f, 1000.0f, 5000.0f, 15000.0f};
    const OscWaveform waveforms[] = {OscWaveform::Sawtooth, OscWaveform::Square};

    for (auto wf : waveforms) {
        for (auto freq : freqs) {
            osc.reset();
            osc.setWaveform(wf);
            osc.setFrequency(freq);

            for (int i = 0; i < 10000; ++i) {
                float sample = osc.process();
                INFO("Waveform=" << static_cast<int>(wf) << " Freq=" << freq << " Sample " << i);
                REQUIRE(sample >= -1.1f);
                REQUIRE(sample <= 1.1f);
            }
        }
    }
}
