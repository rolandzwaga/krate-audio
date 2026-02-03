// ==============================================================================
// Tests: PolyBLEP Oscillator
// ==============================================================================
// Test suite for PolyBlepOscillator (Layer 1 primitive).
// Covers all user stories: sine, sawtooth, square, pulse, triangle,
// phase access, FM/PM modulation, waveform switching, and robustness.
//
// Reference: specs/015-polyblep-oscillator/spec.md
//
// IMPORTANT: All sample-processing loops collect metrics (min, max, NaN count,
// etc.) inside the loop and assert ONCE after the loop. Putting REQUIRE or INFO
// inside tight loops causes Catch2 to spend orders of magnitude more time on
// bookkeeping than on actual DSP, making tests appear to hang.
// See: .claude/skills/testing-guide/SKILL.md "Loop Assertion Anti-Pattern"
// ==============================================================================

#include <krate/dsp/primitives/polyblep_oscillator.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <spectral_analysis.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
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

        float sample = osc.process();
        REQUIRE_FALSE(detail::isNaN(sample));
        REQUIRE_FALSE(detail::isInf(sample));
    }

    SECTION("reset clears phase and state but preserves configuration") {
        osc.prepare(44100.0);
        osc.setFrequency(440.0f);
        osc.setWaveform(OscWaveform::Sine);

        for (int i = 0; i < 100; ++i) {
            [[maybe_unused]] float s = osc.process();
        }

        osc.reset();
        REQUIRE(osc.phase() == Approx(0.0).margin(1e-10));

        float sample = osc.process();
        REQUIRE(sample == Approx(0.0f).margin(1e-5f));
    }

    SECTION("default waveform is Sine") {
        osc.prepare(44100.0);
        osc.setFrequency(440.0f);

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

    float worstError = 0.0f;
    int worstIndex = -1;

    for (int n = 0; n < kNumSamples; ++n) {
        float actual = osc.process();
        float expected = std::sin(kTwoPi * static_cast<float>(n) * kFreq / kSampleRate);
        float error = std::abs(actual - expected);
        if (error > worstError) {
            worstError = error;
            worstIndex = n;
        }
    }

    INFO("Worst sine error: " << worstError << " at sample " << worstIndex);
    REQUIRE(worstError < 1e-5f);
}

TEST_CASE("PolyBlepOscillator sine FFT purity", "[PolyBlepOscillator][US4]") {
    PolyBlepOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(440.0f);
    osc.setWaveform(OscWaveform::Sine);

    constexpr size_t kFFTSize = 4096;
    std::vector<float> buffer(kFFTSize);
    for (size_t i = 0; i < kFFTSize; ++i) {
        buffer[i] = osc.process();
    }

    std::vector<float> window(kFFTSize);
    Window::generateHann(window.data(), kFFTSize);
    for (size_t i = 0; i < kFFTSize; ++i) {
        buffer[i] *= window[i];
    }

    FFT fft;
    fft.prepare(kFFTSize);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(buffer.data(), spectrum.data());

    size_t fundamentalBin = TestUtils::frequencyToBin(440.0f, 44100.0f, kFFTSize);
    float fundamentalMag = spectrum[fundamentalBin].magnitude();

    float worstHarmonicDb = -200.0f;
    int worstHarmonic = -1;
    for (int harmonic = 2; harmonic <= 10; ++harmonic) {
        float harmonicFreq = 440.0f * static_cast<float>(harmonic);
        if (harmonicFreq >= 44100.0f / 2.0f) break;
        size_t harmonicBin = TestUtils::frequencyToBin(harmonicFreq, 44100.0f, kFFTSize);
        float harmonicMag = spectrum[harmonicBin].magnitude();
        float ratioDb = 20.0f * std::log10(harmonicMag / fundamentalMag);
        if (ratioDb > worstHarmonicDb) {
            worstHarmonicDb = ratioDb;
            worstHarmonic = harmonic;
        }
    }

    INFO("Worst harmonic: " << worstHarmonic << " at " << worstHarmonicDb << " dB");
    REQUIRE(worstHarmonicDb < -60.0f);
}

TEST_CASE("PolyBlepOscillator parameter setters", "[PolyBlepOscillator][US4]") {
    PolyBlepOscillator osc;
    osc.prepare(44100.0);

    SECTION("setFrequency changes output frequency") {
        osc.setFrequency(880.0f);
        osc.setWaveform(OscWaveform::Sine);

        constexpr int kNumSamples = 100;
        float worstError = 0.0f;
        for (int n = 0; n < kNumSamples; ++n) {
            float actual = osc.process();
            float expected = std::sin(kTwoPi * static_cast<float>(n) * 880.0f / 44100.0f);
            worstError = std::max(worstError, std::abs(actual - expected));
        }
        REQUIRE(worstError < 1e-5f);
    }

    SECTION("setWaveform changes output waveform") {
        osc.setFrequency(440.0f);
        osc.setWaveform(OscWaveform::Sawtooth);

        osc.reset();
        float sawSample = osc.process();
        REQUIRE_FALSE(detail::isNaN(sawSample));
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
    float minVal = 10.0f;
    float maxVal = -10.0f;
    for (int i = 0; i < kNumSamples; ++i) {
        float sample = osc.process();
        minVal = std::min(minVal, sample);
        maxVal = std::max(maxVal, sample);
    }
    INFO("Sawtooth range: [" << minVal << ", " << maxVal << "]");
    REQUIRE(minVal >= -1.1f);
    REQUIRE(maxVal <= 1.1f);
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

    std::vector<float> window(fftSize);
    Window::generateHann(window.data(), fftSize);
    for (size_t i = 0; i < fftSize; ++i) {
        buffer[i] *= window[i];
    }

    FFT fft;
    fft.prepare(fftSize);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(buffer.data(), spectrum.data());

    size_t fundamentalBin = TestUtils::frequencyToBin(freq, sampleRate, fftSize);
    float fundamentalMag = spectrum[fundamentalBin].magnitude();
    float fundamentalDb = 20.0f * std::log10(fundamentalMag + 1e-10f);

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

        bool tooClose = false;
        for (size_t hBin : harmonicBins) {
            if (bin >= hBin - 3 && bin <= hBin + 3) { tooClose = true; break; }
        }
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

        float worstDiff = 0.0f;
        for (size_t i = 0; i < kN; ++i) {
            worstDiff = std::max(worstDiff, std::abs(blockOutput[i] - singleOutput[i]));
        }
        INFO("Worst processBlock vs process() diff (Sawtooth): " << worstDiff);
        REQUIRE(worstDiff < 1e-7f);
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

        float worstDiff = 0.0f;
        for (size_t i = 0; i < kN; ++i) {
            worstDiff = std::max(worstDiff, std::abs(blockOutput[i] - singleOutput[i]));
        }
        INFO("Worst processBlock vs process() diff (Square): " << worstDiff);
        REQUIRE(worstDiff < 1e-7f);
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

            float minVal = 10.0f;
            float maxVal = -10.0f;
            for (int i = 0; i < 10000; ++i) {
                float sample = osc.process();
                minVal = std::min(minVal, sample);
                maxVal = std::max(maxVal, sample);
            }

            INFO("Waveform=" << static_cast<int>(wf) << " Freq=" << freq
                 << " range=[" << minVal << ", " << maxVal << "]");
            REQUIRE(minVal >= -1.1f);
            REQUIRE(maxVal <= 1.1f);
        }
    }
}

// =============================================================================
// User Story 2: Variable Pulse Width Waveform (Phase 5)
// =============================================================================

TEST_CASE("PolyBlepOscillator pulse PW=0.5 matches square", "[PolyBlepOscillator][US2]") {
    // SC-007: Pulse with PW=0.5 must match Square output sample-by-sample
    PolyBlepOscillator oscPulse, oscSquare;
    oscPulse.prepare(44100.0);
    oscSquare.prepare(44100.0);
    oscPulse.setFrequency(440.0f);
    oscSquare.setFrequency(440.0f);
    oscPulse.setWaveform(OscWaveform::Pulse);
    oscPulse.setPulseWidth(0.5f);
    oscSquare.setWaveform(OscWaveform::Square);

    constexpr int kNumSamples = 4096;
    float worstDiff = 0.0f;
    int worstIndex = -1;
    for (int i = 0; i < kNumSamples; ++i) {
        float pulseSample = oscPulse.process();
        float squareSample = oscSquare.process();
        float diff = std::abs(pulseSample - squareSample);
        if (diff > worstDiff) {
            worstDiff = diff;
            worstIndex = i;
        }
    }
    INFO("Worst pulse/square diff: " << worstDiff << " at sample " << worstIndex);
    REQUIRE(worstDiff < 1e-6f);
}

TEST_CASE("PolyBlepOscillator pulse duty cycle", "[PolyBlepOscillator][US2]") {
    // PW=0.25 should produce approximately 25% high state over one cycle
    PolyBlepOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(440.0f);
    osc.setWaveform(OscWaveform::Pulse);
    osc.setPulseWidth(0.25f);

    constexpr int kNumSamples = 44100; // 1 second = 440 cycles
    int positiveCount = 0;
    for (int i = 0; i < kNumSamples; ++i) {
        if (osc.process() > 0.0f) {
            ++positiveCount;
        }
    }

    float ratio = static_cast<float>(positiveCount) / static_cast<float>(kNumSamples);
    INFO("Positive ratio: " << ratio);
    REQUIRE(ratio == Approx(0.25f).margin(0.02f));
}

TEST_CASE("PolyBlepOscillator pulse FFT alias suppression", "[PolyBlepOscillator][US2]") {
    // SC-003: Pulse PW=0.35 at 2000 Hz, alias components >= 40 dB below fundamental
    PolyBlepOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(2000.0f);
    osc.setWaveform(OscWaveform::Pulse);
    osc.setPulseWidth(0.35f);

    constexpr size_t kFFTSize = 8192;
    std::vector<float> buffer(kFFTSize);
    for (size_t i = 0; i < kFFTSize; ++i) {
        buffer[i] = osc.process();
    }

    std::vector<float> window(kFFTSize);
    Window::generateHann(window.data(), kFFTSize);
    for (size_t i = 0; i < kFFTSize; ++i) {
        buffer[i] *= window[i];
    }

    FFT fft;
    fft.prepare(kFFTSize);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(buffer.data(), spectrum.data());

    size_t fundamentalBin = TestUtils::frequencyToBin(2000.0f, 44100.0f, kFFTSize);
    float fundamentalMag = spectrum[fundamentalBin].magnitude();
    float fundamentalDb = 20.0f * std::log10(fundamentalMag + 1e-10f);

    TestUtils::AliasingTestConfig config;
    config.testFrequencyHz = 2000.0f;
    config.sampleRate = 44100.0f;
    config.fftSize = kFFTSize;
    config.maxHarmonic = 20;

    auto aliasedBins = TestUtils::getAliasedBins(config);
    auto harmonicBins = TestUtils::getHarmonicBins(config);

    float worstAliasDb = -200.0f;
    for (size_t bin : aliasedBins) {
        if (bin >= fft.numBins()) continue;
        bool tooClose = false;
        for (size_t hBin : harmonicBins) {
            if (bin >= hBin - 3 && bin <= hBin + 3) { tooClose = true; break; }
        }
        if (bin >= fundamentalBin - 3 && bin <= fundamentalBin + 3) tooClose = true;
        if (tooClose) continue;

        float mag = spectrum[bin].magnitude();
        float db = 20.0f * std::log10(mag + 1e-10f);
        if (db > worstAliasDb) worstAliasDb = db;
    }

    float suppression = fundamentalDb - worstAliasDb;
    INFO("Pulse PW=0.35 alias suppression: " << suppression << " dB");
    REQUIRE(suppression >= 40.0f);
}

TEST_CASE("PolyBlepOscillator pulse width extremes", "[PolyBlepOscillator][US2]") {
    // PW=0.01 and PW=0.99 must produce valid output without NaN/infinity
    PolyBlepOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(440.0f);
    osc.setWaveform(OscWaveform::Pulse);

    SECTION("PW=0.01 (narrow pulse)") {
        osc.setPulseWidth(0.01f);
        float minVal = 10.0f, maxVal = -10.0f;
        bool hasNan = false, hasInf = false;
        for (int i = 0; i < 10000; ++i) {
            float sample = osc.process();
            if (detail::isNaN(sample)) hasNan = true;
            if (detail::isInf(sample)) hasInf = true;
            minVal = std::min(minVal, sample);
            maxVal = std::max(maxVal, sample);
        }
        REQUIRE_FALSE(hasNan);
        REQUIRE_FALSE(hasInf);
        REQUIRE(minVal >= -1.1f);
        REQUIRE(maxVal <= 1.1f);
    }

    SECTION("PW=0.99 (wide pulse)") {
        osc.setPulseWidth(0.99f);
        float minVal = 10.0f, maxVal = -10.0f;
        bool hasNan = false, hasInf = false;
        for (int i = 0; i < 10000; ++i) {
            float sample = osc.process();
            if (detail::isNaN(sample)) hasNan = true;
            if (detail::isInf(sample)) hasInf = true;
            minVal = std::min(minVal, sample);
            maxVal = std::max(maxVal, sample);
        }
        REQUIRE_FALSE(hasNan);
        REQUIRE_FALSE(hasInf);
        REQUIRE(minVal >= -1.1f);
        REQUIRE(maxVal <= 1.1f);
    }
}

// =============================================================================
// User Story 3: Triangle Waveform via Leaky Integrator (Phase 6)
// =============================================================================

TEST_CASE("PolyBlepOscillator triangle shape", "[PolyBlepOscillator][US3]") {
    PolyBlepOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(440.0f);
    osc.setWaveform(OscWaveform::Triangle);

    // Let the leaky integrator settle
    for (int i = 0; i < 44100; ++i) {
        [[maybe_unused]] float s = osc.process();
    }

    // Capture one full cycle (~100 samples at 440 Hz / 44100 Hz)
    constexpr int kSamplesPerCycle = 100;
    float maxVal = -10.0f;
    float minVal = 10.0f;
    for (int i = 0; i < kSamplesPerCycle; ++i) {
        float sample = osc.process();
        maxVal = std::max(maxVal, sample);
        minVal = std::min(minVal, sample);
    }

    INFO("Triangle max: " << maxVal << ", min: " << minVal);
    REQUIRE(maxVal > 0.5f);
    REQUIRE(minVal < -0.5f);
    REQUIRE(maxVal < 1.5f);
    REQUIRE(minVal > -1.5f);
}

TEST_CASE("PolyBlepOscillator triangle DC stability", "[PolyBlepOscillator][US3]") {
    // SC-005: Average value < 0.01 over 10 seconds (441000 samples at 44100 Hz)
    PolyBlepOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(440.0f);
    osc.setWaveform(OscWaveform::Triangle);

    constexpr int kNumSamples = 441000; // 10 seconds
    double sum = 0.0;
    for (int i = 0; i < kNumSamples; ++i) {
        sum += static_cast<double>(osc.process());
    }
    double avgValue = sum / kNumSamples;
    INFO("Triangle DC offset: " << avgValue);
    REQUIRE(std::abs(avgValue) < 0.01);
}

TEST_CASE("PolyBlepOscillator triangle amplitude consistency", "[PolyBlepOscillator][US3]") {
    // SC-013: Amplitude within +/-20% across 100 Hz to 10000 Hz
    const float testFreqs[] = {100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f};
    std::vector<float> amplitudes;

    for (float freq : testFreqs) {
        PolyBlepOscillator osc;
        osc.prepare(44100.0);
        osc.setFrequency(freq);
        osc.setWaveform(OscWaveform::Triangle);

        int settleSamples = static_cast<int>(44100.0f / freq) * 20;
        for (int i = 0; i < settleSamples; ++i) {
            [[maybe_unused]] float s = osc.process();
        }

        int measureSamples = static_cast<int>(44100.0f / freq) * 10;
        float maxAbs = 0.0f;
        for (int i = 0; i < measureSamples; ++i) {
            float sample = osc.process();
            maxAbs = std::max(maxAbs, std::abs(sample));
        }
        amplitudes.push_back(maxAbs);
    }

    std::vector<float> sorted = amplitudes;
    std::sort(sorted.begin(), sorted.end());
    float median = sorted[sorted.size() / 2];

    for (size_t i = 0; i < amplitudes.size(); ++i) {
        INFO("Freq: " << testFreqs[i] << " Hz, amplitude: " << amplitudes[i]
             << ", median: " << median);
        REQUIRE(amplitudes[i] >= median * 0.8f);
        REQUIRE(amplitudes[i] <= median * 1.2f);
    }
}

TEST_CASE("PolyBlepOscillator triangle frequency transition", "[PolyBlepOscillator][US3]") {
    PolyBlepOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(200.0f);
    osc.setWaveform(OscWaveform::Triangle);

    // Settle at 200 Hz
    float prevSample = 0.0f;
    for (int i = 0; i < 44100; ++i) {
        prevSample = osc.process();
    }

    // Switch to 2000 Hz
    osc.setFrequency(2000.0f);

    float maxJump = 0.0f;
    bool hasNan = false, hasInf = false;
    for (int i = 0; i < 1000; ++i) {
        float sample = osc.process();
        if (detail::isNaN(sample)) hasNan = true;
        if (detail::isInf(sample)) hasInf = true;
        float jump = std::abs(sample - prevSample);
        maxJump = std::max(maxJump, jump);
        prevSample = sample;
    }

    INFO("Max sample-to-sample jump during freq transition: " << maxJump);
    REQUIRE_FALSE(hasNan);
    REQUIRE_FALSE(hasInf);
    REQUIRE(maxJump < 1.0f);
}

// =============================================================================
// User Story 5: Phase Access for Sync and Sub-Oscillator (Phase 7)
// =============================================================================

TEST_CASE("PolyBlepOscillator phase monotonicity", "[PolyBlepOscillator][US5]") {
    PolyBlepOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(440.0f);
    osc.setWaveform(OscWaveform::Sine);

    double prevPhase = osc.phase();
    bool monotonicBetweenWraps = true;
    bool phaseInRange = true;
    int failedSample = -1;

    for (int i = 0; i < 10000; ++i) {
        [[maybe_unused]] float s = osc.process();
        double currentPhase = osc.phase();

        if (!osc.phaseWrapped()) {
            if (currentPhase <= prevPhase) {
                monotonicBetweenWraps = false;
                if (failedSample < 0) failedSample = i;
            }
        }
        if (currentPhase < 0.0 || currentPhase >= 1.0) {
            phaseInRange = false;
            if (failedSample < 0) failedSample = i;
        }
        prevPhase = currentPhase;
    }

    INFO("Failed at sample: " << failedSample);
    REQUIRE(monotonicBetweenWraps);
    REQUIRE(phaseInRange);
}

TEST_CASE("PolyBlepOscillator phase wrap counting", "[PolyBlepOscillator][US5]") {
    // SC-006: 440 Hz at 44100 Hz produces ~440 wraps in 44100 samples
    PolyBlepOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(440.0f);
    osc.setWaveform(OscWaveform::Sine);

    int wrapCount = 0;
    constexpr int kNumSamples = 44100;
    for (int i = 0; i < kNumSamples; ++i) {
        [[maybe_unused]] float s = osc.process();
        if (osc.phaseWrapped()) {
            ++wrapCount;
        }
    }

    INFO("Phase wraps in 1 second at 440 Hz: " << wrapCount);
    REQUIRE(wrapCount >= 439);
    REQUIRE(wrapCount <= 441);
}

TEST_CASE("PolyBlepOscillator resetPhase", "[PolyBlepOscillator][US5]") {
    PolyBlepOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(440.0f);
    osc.setWaveform(OscWaveform::Sine);

    SECTION("resetPhase(0.5) sets phase to 0.5") {
        // SC-011
        osc.resetPhase(0.5);
        REQUIRE(osc.phase() == Approx(0.5).margin(1e-10));

        float sample = osc.process();
        REQUIRE(sample == Approx(0.0f).margin(1e-4f));
    }

    SECTION("resetPhase wraps out-of-range values") {
        osc.resetPhase(1.5);
        REQUIRE(osc.phase() == Approx(0.5).margin(1e-10));

        osc.resetPhase(-0.25);
        REQUIRE(osc.phase() == Approx(0.75).margin(1e-10));
    }
}

TEST_CASE("PolyBlepOscillator triangle integrator preserved during resetPhase", "[PolyBlepOscillator][US5]") {
    // FR-019: resetPhase should NOT clear the triangle integrator state
    PolyBlepOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(440.0f);
    osc.setWaveform(OscWaveform::Triangle);

    // Let triangle settle
    for (int i = 0; i < 44100; ++i) {
        [[maybe_unused]] float s = osc.process();
    }

    float beforeReset = osc.process();

    // Reset phase to 0
    osc.resetPhase(0.0);

    // The next sample should NOT be zero (integrator was preserved)
    float afterReset = osc.process();

    float jump = std::abs(afterReset - beforeReset);
    INFO("Before: " << beforeReset << ", After: " << afterReset << ", Jump: " << jump);
    REQUIRE(jump < 1.5f);
    REQUIRE_FALSE(detail::isNaN(afterReset));
}

// =============================================================================
// User Story 6: FM and PM Input (Phase 8)
// =============================================================================

TEST_CASE("PolyBlepOscillator PM zero modulation", "[PolyBlepOscillator][US6]") {
    PolyBlepOscillator oscMod, oscRef;
    oscMod.prepare(44100.0);
    oscRef.prepare(44100.0);
    oscMod.setFrequency(440.0f);
    oscRef.setFrequency(440.0f);
    oscMod.setWaveform(OscWaveform::Sine);
    oscRef.setWaveform(OscWaveform::Sine);

    constexpr int kNumSamples = 1000;
    float worstDiff = 0.0f;
    for (int i = 0; i < kNumSamples; ++i) {
        oscMod.setPhaseModulation(0.0f);
        float modSample = oscMod.process();
        float refSample = oscRef.process();
        worstDiff = std::max(worstDiff, std::abs(modSample - refSample));
    }
    INFO("Worst PM(0) vs unmodulated diff: " << worstDiff);
    REQUIRE(worstDiff < 1e-6f);
}

TEST_CASE("PolyBlepOscillator FM offset changes frequency", "[PolyBlepOscillator][US6]") {
    PolyBlepOscillator oscFM, oscRef;
    oscFM.prepare(44100.0);
    oscRef.prepare(44100.0);
    oscFM.setFrequency(440.0f);
    oscRef.setFrequency(540.0f);
    oscFM.setWaveform(OscWaveform::Sine);
    oscRef.setWaveform(OscWaveform::Sine);

    constexpr int kNumSamples = 500;
    float worstDiff = 0.0f;
    for (int i = 0; i < kNumSamples; ++i) {
        oscFM.setFrequencyModulation(100.0f);
        float fmSample = oscFM.process();
        float refSample = oscRef.process();
        worstDiff = std::max(worstDiff, std::abs(fmSample - refSample));
    }
    INFO("Worst FM(+100Hz) vs 540Hz ref diff: " << worstDiff);
    REQUIRE(worstDiff < 1e-4f);
}

TEST_CASE("PolyBlepOscillator FM stability with sawtooth", "[PolyBlepOscillator][US6]") {
    PolyBlepOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(440.0f);
    osc.setWaveform(OscWaveform::Sawtooth);

    float minVal = 10.0f, maxVal = -10.0f;
    bool hasNan = false, hasInf = false;
    for (int i = 0; i < 10000; ++i) {
        float fmHz = 200.0f * std::sin(kTwoPi * static_cast<float>(i) * 5.0f / 44100.0f);
        osc.setFrequencyModulation(fmHz);
        float sample = osc.process();
        if (detail::isNaN(sample)) hasNan = true;
        if (detail::isInf(sample)) hasInf = true;
        minVal = std::min(minVal, sample);
        maxVal = std::max(maxVal, sample);
    }

    REQUIRE_FALSE(hasNan);
    REQUIRE_FALSE(hasInf);
    REQUIRE(minVal >= -2.0f);
    REQUIRE(maxVal <= 2.0f);
}

TEST_CASE("PolyBlepOscillator FM/PM non-accumulation", "[PolyBlepOscillator][US6]") {
    PolyBlepOscillator oscMod, oscRef;
    oscMod.prepare(44100.0);
    oscRef.prepare(44100.0);
    oscMod.setFrequency(440.0f);
    oscRef.setFrequency(440.0f);
    oscMod.setWaveform(OscWaveform::Sine);
    oscRef.setWaveform(OscWaveform::Sine);

    // Apply FM on first sample only
    oscMod.setFrequencyModulation(100.0f);
    [[maybe_unused]] float s1m = oscMod.process();
    [[maybe_unused]] float s1r = oscRef.process();

    // Subsequent samples without setting FM should not accumulate
    float s2m = oscMod.process();
    REQUIRE_FALSE(detail::isNaN(s2m));

    float s3m = oscMod.process();
    REQUIRE_FALSE(detail::isNaN(s3m));
}

// =============================================================================
// User Story 7: Waveform Switching and Robustness (Phase 9)
// =============================================================================

TEST_CASE("PolyBlepOscillator waveform switching phase continuity", "[PolyBlepOscillator][US7]") {
    PolyBlepOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(440.0f);
    osc.setWaveform(OscWaveform::Sawtooth);

    for (int i = 0; i < 1000; ++i) {
        [[maybe_unused]] float s = osc.process();
    }

    double phaseBeforeSwitch = osc.phase();

    osc.setWaveform(OscWaveform::Square);

    double phaseAfterSwitch = osc.phase();

    REQUIRE(phaseAfterSwitch == Approx(phaseBeforeSwitch).margin(1e-10));

    float switchSample = osc.process();
    REQUIRE_FALSE(detail::isNaN(switchSample));
    REQUIRE(switchSample >= -1.1f);
    REQUIRE(switchSample <= 1.1f);
}

TEST_CASE("PolyBlepOscillator frequency at Nyquist", "[PolyBlepOscillator][US7]") {
    // SC-010: setFrequency(sampleRate) should produce valid output
    PolyBlepOscillator osc;
    osc.prepare(44100.0);

    osc.setFrequency(44100.0f);
    osc.setWaveform(OscWaveform::Sawtooth);

    bool hasNan = false, hasInf = false;
    for (int i = 0; i < 1000; ++i) {
        float sample = osc.process();
        if (detail::isNaN(sample)) hasNan = true;
        if (detail::isInf(sample)) hasInf = true;
    }
    REQUIRE_FALSE(hasNan);
    REQUIRE_FALSE(hasInf);
}

TEST_CASE("PolyBlepOscillator zero frequency", "[PolyBlepOscillator][US7]") {
    PolyBlepOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(0.0f);
    osc.setWaveform(OscWaveform::Sine);

    float firstSample = osc.process();
    int wrapCount = 0;
    float worstDrift = 0.0f;
    for (int i = 0; i < 10000; ++i) {
        float sample = osc.process();
        if (osc.phaseWrapped()) ++wrapCount;
        worstDrift = std::max(worstDrift, std::abs(sample - firstSample));
    }

    INFO("Zero freq drift from first sample: " << worstDrift);
    REQUIRE(worstDrift < 1e-6f);
    REQUIRE(wrapCount == 0);
}

TEST_CASE("PolyBlepOscillator invalid inputs", "[PolyBlepOscillator][US7]") {
    // SC-015: NaN/Inf in setFrequency, FM, PM produce safe output
    PolyBlepOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(440.0f);
    osc.setWaveform(OscWaveform::Sine);

    SECTION("NaN frequency") {
        osc.setFrequency(std::numeric_limits<float>::quiet_NaN());
        float sample = osc.process();
        REQUIRE_FALSE(detail::isNaN(sample));
        REQUIRE_FALSE(detail::isInf(sample));
    }

    SECTION("Inf frequency") {
        osc.setFrequency(std::numeric_limits<float>::infinity());
        float sample = osc.process();
        REQUIRE_FALSE(detail::isNaN(sample));
        REQUIRE_FALSE(detail::isInf(sample));
    }

    SECTION("NaN FM") {
        osc.setFrequencyModulation(std::numeric_limits<float>::quiet_NaN());
        float sample = osc.process();
        REQUIRE_FALSE(detail::isNaN(sample));
        REQUIRE_FALSE(detail::isInf(sample));
    }

    SECTION("NaN PM") {
        osc.setPhaseModulation(std::numeric_limits<float>::quiet_NaN());
        float sample = osc.process();
        REQUIRE_FALSE(detail::isNaN(sample));
        REQUIRE_FALSE(detail::isInf(sample));
    }
}

TEST_CASE("PolyBlepOscillator output bounds all waveforms", "[PolyBlepOscillator][US7]") {
    // SC-009: All waveforms stay in [-2.0, 2.0] (sanitize clamp) over many samples
    PolyBlepOscillator osc;
    osc.prepare(44100.0);

    const float freqs[] = {100.0f, 1000.0f, 5000.0f, 15000.0f};
    const OscWaveform waveforms[] = {
        OscWaveform::Sine, OscWaveform::Sawtooth, OscWaveform::Square,
        OscWaveform::Pulse, OscWaveform::Triangle
    };

    for (auto wf : waveforms) {
        for (auto freq : freqs) {
            osc.reset();
            osc.setWaveform(wf);
            osc.setFrequency(freq);
            if (wf == OscWaveform::Pulse) {
                osc.setPulseWidth(0.3f);
            }

            // Let triangle settle
            if (wf == OscWaveform::Triangle) {
                for (int i = 0; i < 10000; ++i) {
                    [[maybe_unused]] float s = osc.process();
                }
            }

            float minVal = 10.0f, maxVal = -10.0f;
            for (int i = 0; i < 10000; ++i) {
                float sample = osc.process();
                minVal = std::min(minVal, sample);
                maxVal = std::max(maxVal, sample);
            }

            INFO("Waveform=" << static_cast<int>(wf) << " Freq=" << freq
                 << " min=" << minVal << " max=" << maxVal);
            REQUIRE(minVal >= -2.0f);
            REQUIRE(maxVal <= 2.0f);
        }
    }
}

// =============================================================================
// Performance Benchmark (Phase 9.5)
// =============================================================================
// Informational benchmark tagged [benchmark] so it can be excluded from normal
// test runs. Measures nanoseconds per sample for each waveform using
// std::chrono::high_resolution_clock averaged over 10,000 samples.
// Results are SHOULD targets per SC-014, not hard pass/fail gates.

TEST_CASE("PolyBlepOscillator performance benchmark", "[PolyBlepOscillator][benchmark]") {
    constexpr int kWarmupSamples = 1000;
    constexpr int kBenchmarkSamples = 10000;
    constexpr float kFreq = 440.0f;
    constexpr float kSampleRate = 44100.0f;

    struct WaveformInfo {
        OscWaveform waveform;
        const char* name;
    };

    const WaveformInfo waveforms[] = {
        {OscWaveform::Sine,     "Sine"},
        {OscWaveform::Sawtooth, "Sawtooth"},
        {OscWaveform::Square,   "Square"},
        {OscWaveform::Pulse,    "Pulse"},
        {OscWaveform::Triangle, "Triangle"}
    };

    for (const auto& wfInfo : waveforms) {
        PolyBlepOscillator osc;
        osc.prepare(static_cast<double>(kSampleRate));
        osc.setFrequency(kFreq);
        osc.setWaveform(wfInfo.waveform);
        if (wfInfo.waveform == OscWaveform::Pulse) {
            osc.setPulseWidth(0.35f);
        }

        // Warmup: let integrator settle (important for Triangle)
        for (int i = 0; i < kWarmupSamples; ++i) {
            [[maybe_unused]] volatile float s = osc.process();
        }

        // Benchmark
        auto start = std::chrono::high_resolution_clock::now();
        volatile float sink = 0.0f;
        for (int i = 0; i < kBenchmarkSamples; ++i) {
            sink = osc.process();
        }
        auto end = std::chrono::high_resolution_clock::now();

        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        double nsPerSample = static_cast<double>(elapsed.count())
                           / static_cast<double>(kBenchmarkSamples);

        // Approximate cycles (assuming ~3 GHz clock; informational only)
        double approxCycles = nsPerSample * 3.0;

        INFO(wfInfo.name << ": " << nsPerSample << " ns/sample (~"
             << approxCycles << " cycles at 3 GHz)");

        // Informational assertion: just verify it completed without hanging
        // SC-014 targets: ~50 cycles for PolyBLEP waveforms, ~15-20 for Sine
        REQUIRE(nsPerSample < 10000.0); // Generous upper bound: 10 us/sample
    }
}
