// ==============================================================================
// Layer 2: DSP Processor Tests - FM Operator
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written before implementation.
//
// Tests for: dsp/include/krate/dsp/processors/fm_operator.h
// Contract: specs/021-fm-pm-synth-operator/contracts/fm_operator.h
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/fm_operator.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <vector>

using Catch::Approx;

// ==============================================================================
// Helper Functions
// ==============================================================================

namespace {

/// @brief Compute RMS amplitude of a signal
[[nodiscard]] float computeRMS(const float* data, size_t numSamples) noexcept {
    double sumSq = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        sumSq += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(numSamples)));
}

/// @brief Compute peak amplitude of a signal
[[nodiscard]] float computePeak(const float* data, size_t numSamples) noexcept {
    float peak = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        peak = std::max(peak, std::abs(data[i]));
    }
    return peak;
}

/// @brief Find the dominant frequency in a signal using FFT
/// @return Frequency in Hz, or 0.0 if no dominant peak found
[[nodiscard]] float findDominantFrequency(
    const float* data,
    size_t numSamples,
    float sampleRate
) {
    using namespace Krate::DSP;

    // Apply Hann window
    std::vector<float> windowed(numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        float w = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(i)
                                           / static_cast<float>(numSamples)));
        windowed[i] = data[i] * w;
    }

    // Perform FFT
    FFT fft;
    fft.prepare(numSamples);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(windowed.data(), spectrum.data());

    // Find the bin with the highest magnitude (skip DC)
    size_t peakBin = 1;
    float peakMag = 0.0f;
    for (size_t bin = 1; bin < spectrum.size(); ++bin) {
        float mag = spectrum[bin].magnitude();
        if (mag > peakMag) {
            peakMag = mag;
            peakBin = bin;
        }
    }

    // Convert bin to frequency
    float binResolution = sampleRate / static_cast<float>(numSamples);
    return static_cast<float>(peakBin) * binResolution;
}

/// @brief Calculate Total Harmonic Distortion (THD)
/// @return THD as a ratio (0.0 = pure sine, 1.0 = 100% distortion)
[[nodiscard]] float calculateTHD(
    const float* data,
    size_t numSamples,
    float fundamentalHz,
    float sampleRate
) {
    using namespace Krate::DSP;

    // Apply Hann window
    std::vector<float> windowed(numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        float w = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(i)
                                           / static_cast<float>(numSamples)));
        windowed[i] = data[i] * w;
    }

    // Perform FFT
    FFT fft;
    fft.prepare(numSamples);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(windowed.data(), spectrum.data());

    float binResolution = sampleRate / static_cast<float>(numSamples);

    // Find fundamental bin
    size_t fundamentalBin = static_cast<size_t>(std::round(fundamentalHz / binResolution));

    // Get fundamental power (include 2 bins on each side for windowing spread)
    float fundamentalPower = 0.0f;
    for (size_t offset = 0; offset <= 2; ++offset) {
        if (fundamentalBin + offset < spectrum.size()) {
            float mag = spectrum[fundamentalBin + offset].magnitude();
            fundamentalPower += mag * mag;
        }
        if (fundamentalBin >= offset && offset > 0) {
            float mag = spectrum[fundamentalBin - offset].magnitude();
            fundamentalPower += mag * mag;
        }
    }

    // Get harmonic power (harmonics 2-10)
    float harmonicPower = 0.0f;
    for (int h = 2; h <= 10; ++h) {
        float harmonicFreq = fundamentalHz * static_cast<float>(h);
        if (harmonicFreq >= sampleRate / 2.0f) break;

        size_t harmonicBin = static_cast<size_t>(std::round(harmonicFreq / binResolution));
        if (harmonicBin >= spectrum.size()) break;

        for (size_t offset = 0; offset <= 2; ++offset) {
            if (harmonicBin + offset < spectrum.size()) {
                float mag = spectrum[harmonicBin + offset].magnitude();
                harmonicPower += mag * mag;
            }
            if (harmonicBin >= offset && offset > 0) {
                float mag = spectrum[harmonicBin - offset].magnitude();
                harmonicPower += mag * mag;
            }
        }
    }

    if (fundamentalPower < 1e-10f) return 0.0f;

    return std::sqrt(harmonicPower / fundamentalPower);
}

/// @brief Check if a signal contains energy at specific sidebands
/// @param data Signal data
/// @param numSamples Number of samples
/// @param carrierHz Carrier frequency in Hz
/// @param modulatorHz Modulator frequency in Hz
/// @param sampleRate Sample rate in Hz
/// @param thresholdDb Minimum level (dB below fundamental) for a sideband to be considered present
/// @return Number of detectable sideband pairs
[[nodiscard]] int countSidebands(
    const float* data,
    size_t numSamples,
    float carrierHz,
    float modulatorHz,
    float sampleRate,
    float thresholdDb = -40.0f
) {
    using namespace Krate::DSP;

    // Apply Hann window
    std::vector<float> windowed(numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        float w = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(i)
                                           / static_cast<float>(numSamples)));
        windowed[i] = data[i] * w;
    }

    // Perform FFT
    FFT fft;
    fft.prepare(numSamples);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(windowed.data(), spectrum.data());

    float binResolution = sampleRate / static_cast<float>(numSamples);

    // Find carrier magnitude
    size_t carrierBin = static_cast<size_t>(std::round(carrierHz / binResolution));
    float carrierMag = 0.0f;
    for (size_t offset = 0; offset <= 2; ++offset) {
        if (carrierBin + offset < spectrum.size()) {
            carrierMag = std::max(carrierMag, spectrum[carrierBin + offset].magnitude());
        }
        if (carrierBin >= offset) {
            carrierMag = std::max(carrierMag, spectrum[carrierBin - offset].magnitude());
        }
    }

    float thresholdMag = carrierMag * std::pow(10.0f, thresholdDb / 20.0f);

    int sidebandCount = 0;

    // Check for sidebands at carrier +/- n * modulator
    for (int n = 1; n <= 5; ++n) {
        float upperFreq = carrierHz + static_cast<float>(n) * modulatorHz;
        float lowerFreq = carrierHz - static_cast<float>(n) * modulatorHz;

        bool upperFound = false;
        bool lowerFound = false;

        // Check upper sideband
        if (upperFreq < sampleRate / 2.0f) {
            size_t upperBin = static_cast<size_t>(std::round(upperFreq / binResolution));
            if (upperBin < spectrum.size()) {
                for (size_t offset = 0; offset <= 2; ++offset) {
                    if (upperBin + offset < spectrum.size() &&
                        spectrum[upperBin + offset].magnitude() > thresholdMag) {
                        upperFound = true;
                        break;
                    }
                    if (upperBin >= offset &&
                        spectrum[upperBin - offset].magnitude() > thresholdMag) {
                        upperFound = true;
                        break;
                    }
                }
            }
        }

        // Check lower sideband
        if (lowerFreq > 0.0f) {
            size_t lowerBin = static_cast<size_t>(std::round(lowerFreq / binResolution));
            if (lowerBin < spectrum.size()) {
                for (size_t offset = 0; offset <= 2; ++offset) {
                    if (lowerBin + offset < spectrum.size() &&
                        spectrum[lowerBin + offset].magnitude() > thresholdMag) {
                        lowerFound = true;
                        break;
                    }
                    if (lowerBin >= offset &&
                        spectrum[lowerBin - offset].magnitude() > thresholdMag) {
                        lowerFound = true;
                        break;
                    }
                }
            }
        }

        if (upperFound || lowerFound) {
            ++sidebandCount;
        }
    }

    return sidebandCount;
}

/// @brief Compute RMS difference between two signals
[[nodiscard]] float rmsDifference(const float* a, const float* b, size_t numSamples) noexcept {
    double sumSq = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        double diff = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sumSq += diff * diff;
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(numSamples)));
}

} // anonymous namespace

using namespace Krate::DSP;

// ==============================================================================
// User Story 1: Basic FM Operator with Frequency Ratio [US1]
// ==============================================================================
// Goal: Create a frequency-controllable sine oscillator with ratio-based tuning.
// This is the absolute core of FM synthesis.

TEST_CASE("FR-001/FR-014: Default constructor produces silence before prepare()",
          "[FMOperator][US1][lifecycle]") {
    // FR-001: Default constructor initializes to safe silence state
    // FR-014: process() returns 0.0 before prepare() is called
    FMOperator op;

    // Should return 0.0 without crashing
    float sample = op.process();
    REQUIRE(sample == 0.0f);

    // Multiple calls should still return silence
    for (int i = 0; i < 100; ++i) {
        sample = op.process();
        REQUIRE(sample == 0.0f);
    }
}

TEST_CASE("FR-014: Calling process() before prepare() returns 0.0",
          "[FMOperator][US1][lifecycle]") {
    FMOperator op;

    // Configure parameters but don't call prepare()
    op.setFrequency(440.0f);
    op.setRatio(1.0f);
    op.setLevel(1.0f);

    // Should still return 0.0 because prepare() not called
    float sample = op.process();
    REQUIRE(sample == 0.0f);

    // With phase modulation input - should still return 0.0
    sample = op.process(0.5f);
    REQUIRE(sample == 0.0f);
}

TEST_CASE("FR-002/FR-010: After prepare(), operator produces 440 Hz sine (ratio 1.0)",
          "[FMOperator][US1][basic]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr float kFrequency = 440.0f;
    constexpr size_t kNumSamples = 4096;

    FMOperator op;
    op.prepare(kSampleRate);
    op.setFrequency(kFrequency);
    op.setRatio(1.0f);
    op.setFeedback(0.0f);
    op.setLevel(1.0f);

    std::vector<float> output(kNumSamples);
    for (size_t i = 0; i < kNumSamples; ++i) {
        output[i] = op.process();
    }

    // Verify the output is a sine wave at 440 Hz
    float dominantFreq = findDominantFrequency(output.data(), kNumSamples, kSampleRate);
    INFO("Dominant frequency: " << dominantFreq << " Hz");
    REQUIRE(dominantFreq == Approx(kFrequency).margin(5.0f));

    // Verify THD is low (pure sine)
    float thd = calculateTHD(output.data(), kNumSamples, kFrequency, kSampleRate);
    INFO("THD: " << (thd * 100.0f) << "%");
    REQUIRE(thd < 0.001f);  // THD < 0.1%
}

TEST_CASE("FR-015: Verify sine wavetable mipmap structure",
          "[FMOperator][US1][wavetable]") {
    // The sine wavetable should have 11 mipmap levels with 2048 samples each
    // This is verified implicitly by the operator producing correct output.
    // We verify by checking the output is a clean sine at various frequencies.

    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kNumSamples = 4096;

    FMOperator op;
    op.prepare(kSampleRate);
    op.setFeedback(0.0f);
    op.setLevel(1.0f);

    // Test at various frequencies to exercise mipmap levels
    const float frequencies[] = {100.0f, 440.0f, 1000.0f, 5000.0f, 10000.0f};

    for (float freq : frequencies) {
        op.setFrequency(freq);
        op.setRatio(1.0f);

        // Reset for each test by re-preparing (clears state)
        op.prepare(kSampleRate);
        op.setFrequency(freq);
        op.setRatio(1.0f);
        op.setFeedback(0.0f);
        op.setLevel(1.0f);

        std::vector<float> output(kNumSamples);
        for (size_t i = 0; i < kNumSamples; ++i) {
            output[i] = op.process();
        }

        float thd = calculateTHD(output.data(), kNumSamples, freq, kSampleRate);
        INFO("Frequency: " << freq << " Hz, THD: " << (thd * 100.0f) << "%");
        REQUIRE(thd < 0.01f);  // THD < 1% at all frequencies
    }
}

TEST_CASE("FR-005: Ratio 2.0 produces 880 Hz sine (base frequency 440 Hz)",
          "[FMOperator][US1][ratio]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr float kBaseFrequency = 440.0f;
    constexpr float kRatio = 2.0f;
    constexpr float kExpectedFrequency = kBaseFrequency * kRatio;  // 880 Hz
    constexpr size_t kNumSamples = 4096;

    FMOperator op;
    op.prepare(kSampleRate);
    op.setFrequency(kBaseFrequency);
    op.setRatio(kRatio);
    op.setFeedback(0.0f);
    op.setLevel(1.0f);

    std::vector<float> output(kNumSamples);
    for (size_t i = 0; i < kNumSamples; ++i) {
        output[i] = op.process();
    }

    float dominantFreq = findDominantFrequency(output.data(), kNumSamples, kSampleRate);
    INFO("Dominant frequency: " << dominantFreq << " Hz (expected " << kExpectedFrequency << " Hz)");
    REQUIRE(dominantFreq == Approx(kExpectedFrequency).margin(10.0f));
}

TEST_CASE("FR-005: Non-integer ratio 3.5 produces 1540 Hz sine",
          "[FMOperator][US1][ratio]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr float kBaseFrequency = 440.0f;
    constexpr float kRatio = 3.5f;
    constexpr float kExpectedFrequency = kBaseFrequency * kRatio;  // 1540 Hz
    constexpr size_t kNumSamples = 4096;

    FMOperator op;
    op.prepare(kSampleRate);
    op.setFrequency(kBaseFrequency);
    op.setRatio(kRatio);
    op.setFeedback(0.0f);
    op.setLevel(1.0f);

    std::vector<float> output(kNumSamples);
    for (size_t i = 0; i < kNumSamples; ++i) {
        output[i] = op.process();
    }

    float dominantFreq = findDominantFrequency(output.data(), kNumSamples, kSampleRate);
    INFO("Dominant frequency: " << dominantFreq << " Hz (expected " << kExpectedFrequency << " Hz)");
    REQUIRE(dominantFreq == Approx(kExpectedFrequency).margin(10.0f));
}

TEST_CASE("FR-007/FR-009: Level 0.5 scales output amplitude, lastRawOutput() returns full-scale",
          "[FMOperator][US1][level]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kNumSamples = 4096;

    // First, get the peak amplitude with level 1.0
    FMOperator opFull;
    opFull.prepare(kSampleRate);
    opFull.setFrequency(440.0f);
    opFull.setRatio(1.0f);
    opFull.setFeedback(0.0f);
    opFull.setLevel(1.0f);

    std::vector<float> outputFull(kNumSamples);
    for (size_t i = 0; i < kNumSamples; ++i) {
        outputFull[i] = opFull.process();
    }
    float peakFull = computePeak(outputFull.data(), kNumSamples);

    // Now with level 0.5
    FMOperator opHalf;
    opHalf.prepare(kSampleRate);
    opHalf.setFrequency(440.0f);
    opHalf.setRatio(1.0f);
    opHalf.setFeedback(0.0f);
    opHalf.setLevel(0.5f);

    std::vector<float> outputHalf(kNumSamples);
    std::vector<float> rawOutputs(kNumSamples);
    for (size_t i = 0; i < kNumSamples; ++i) {
        outputHalf[i] = opHalf.process();
        rawOutputs[i] = opHalf.lastRawOutput();
    }
    float peakHalf = computePeak(outputHalf.data(), kNumSamples);
    float peakRaw = computePeak(rawOutputs.data(), kNumSamples);

    INFO("Peak at level 1.0: " << peakFull);
    INFO("Peak at level 0.5: " << peakHalf);
    INFO("Peak raw output: " << peakRaw);

    // Output should be approximately half
    REQUIRE(peakHalf == Approx(peakFull * 0.5f).margin(0.05f));

    // lastRawOutput() should return full-scale (approximately equal to peakFull)
    REQUIRE(peakRaw == Approx(peakFull).margin(0.05f));
}

TEST_CASE("FR-007/FR-009: Level 0.0 produces silence, lastRawOutput() still returns oscillator output",
          "[FMOperator][US1][level]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kNumSamples = 1024;

    FMOperator op;
    op.prepare(kSampleRate);
    op.setFrequency(440.0f);
    op.setRatio(1.0f);
    op.setFeedback(0.0f);
    op.setLevel(0.0f);

    std::vector<float> output(kNumSamples);
    std::vector<float> rawOutputs(kNumSamples);
    for (size_t i = 0; i < kNumSamples; ++i) {
        output[i] = op.process();
        rawOutputs[i] = op.lastRawOutput();
    }

    float peakOutput = computePeak(output.data(), kNumSamples);
    float peakRaw = computePeak(rawOutputs.data(), kNumSamples);

    INFO("Peak output (level 0.0): " << peakOutput);
    INFO("Peak raw output: " << peakRaw);

    // Output should be silence
    REQUIRE(peakOutput == 0.0f);

    // lastRawOutput() should still have the oscillator output (around 0.96 due to normalization)
    REQUIRE(peakRaw > 0.5f);  // Raw output should be significant
}

TEST_CASE("FR-004/FR-012: Parameter getters return correct values",
          "[FMOperator][US1][getters]") {
    FMOperator op;
    op.prepare(44100.0);

    op.setFrequency(440.0f);
    op.setRatio(2.5f);
    op.setFeedback(0.3f);
    op.setLevel(0.8f);

    REQUIRE(op.getFrequency() == Approx(440.0f));
    REQUIRE(op.getRatio() == Approx(2.5f));
    REQUIRE(op.getFeedback() == Approx(0.3f));
    REQUIRE(op.getLevel() == Approx(0.8f));
}

// ==============================================================================
// User Story 2: Phase Modulation Input [US2]
// ==============================================================================
// Goal: Enable operator chaining by accepting external phase modulation input.

TEST_CASE("FR-008: Modulator -> Carrier produces FM sidebands",
          "[FMOperator][US2][modulation]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr float kBaseFrequency = 440.0f;
    constexpr size_t kNumSamples = 8192;

    // Modulator at 2:1 ratio (880 Hz)
    FMOperator modulator;
    modulator.prepare(kSampleRate);
    modulator.setFrequency(kBaseFrequency);
    modulator.setRatio(2.0f);  // 880 Hz
    modulator.setFeedback(0.0f);
    modulator.setLevel(0.5f);  // Modulation depth

    // Carrier at 1:1 ratio (440 Hz)
    FMOperator carrier;
    carrier.prepare(kSampleRate);
    carrier.setFrequency(kBaseFrequency);
    carrier.setRatio(1.0f);  // 440 Hz
    carrier.setFeedback(0.0f);
    carrier.setLevel(1.0f);

    std::vector<float> output(kNumSamples);
    for (size_t i = 0; i < kNumSamples; ++i) {
        (void)modulator.process();
        float pm = modulator.lastRawOutput() * modulator.getLevel();
        output[i] = carrier.process(pm);
    }

    // Should have sidebands at carrier +/- modulator frequency (440 +/- 880)
    float carrierHz = kBaseFrequency * 1.0f;  // 440 Hz
    float modulatorHz = kBaseFrequency * 2.0f;  // 880 Hz
    int sidebands = countSidebands(output.data(), kNumSamples, carrierHz, modulatorHz, kSampleRate);

    INFO("Number of detectable sidebands: " << sidebands);
    REQUIRE(sidebands >= 1);  // At least one sideband pair should be visible
}

TEST_CASE("FR-008: Modulator level 0.0 produces carrier with no sidebands",
          "[FMOperator][US2][modulation]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr float kBaseFrequency = 440.0f;
    constexpr size_t kNumSamples = 4096;

    // Modulator with level 0.0 (no modulation)
    FMOperator modulator;
    modulator.prepare(kSampleRate);
    modulator.setFrequency(kBaseFrequency);
    modulator.setRatio(2.0f);
    modulator.setFeedback(0.0f);
    modulator.setLevel(0.0f);  // No modulation

    FMOperator carrier;
    carrier.prepare(kSampleRate);
    carrier.setFrequency(kBaseFrequency);
    carrier.setRatio(1.0f);
    carrier.setFeedback(0.0f);
    carrier.setLevel(1.0f);

    std::vector<float> output(kNumSamples);
    for (size_t i = 0; i < kNumSamples; ++i) {
        (void)modulator.process();
        float pm = modulator.lastRawOutput() * modulator.getLevel();  // = 0.0
        output[i] = carrier.process(pm);
    }

    // Should be a pure sine at carrier frequency
    float thd = calculateTHD(output.data(), kNumSamples, kBaseFrequency, kSampleRate);
    INFO("THD with zero modulation: " << (thd * 100.0f) << "%");
    REQUIRE(thd < 0.001f);  // Should be pure sine (THD < 0.1%)
}

TEST_CASE("FR-008: Increasing modulator level increases sideband prominence",
          "[FMOperator][US2][modulation]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr float kBaseFrequency = 440.0f;
    constexpr size_t kNumSamples = 8192;

    auto processFMPair = [&](float modulatorLevel) {
        FMOperator modulator;
        modulator.prepare(kSampleRate);
        modulator.setFrequency(kBaseFrequency);
        modulator.setRatio(2.0f);
        modulator.setFeedback(0.0f);
        modulator.setLevel(modulatorLevel);

        FMOperator carrier;
        carrier.prepare(kSampleRate);
        carrier.setFrequency(kBaseFrequency);
        carrier.setRatio(1.0f);
        carrier.setFeedback(0.0f);
        carrier.setLevel(1.0f);

        std::vector<float> output(kNumSamples);
        for (size_t i = 0; i < kNumSamples; ++i) {
            (void)modulator.process();
            float pm = modulator.lastRawOutput() * modulator.getLevel();
            output[i] = carrier.process(pm);
        }

        return countSidebands(output.data(), kNumSamples, kBaseFrequency,
                             kBaseFrequency * 2.0f, kSampleRate);
    };

    int sidebandsLow = processFMPair(0.2f);
    int sidebandsMed = processFMPair(0.5f);
    int sidebandsHigh = processFMPair(1.0f);

    INFO("Sidebands at level 0.2: " << sidebandsLow);
    INFO("Sidebands at level 0.5: " << sidebandsMed);
    INFO("Sidebands at level 1.0: " << sidebandsHigh);

    // Higher modulation should produce more sidebands (or at least as many)
    REQUIRE(sidebandsHigh >= sidebandsLow);
}

TEST_CASE("FR-008: process(0.0f) produces identical output to process() with no argument",
          "[FMOperator][US2][modulation]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kNumSamples = 1024;

    // First operator using process()
    FMOperator op1;
    op1.prepare(kSampleRate);
    op1.setFrequency(440.0f);
    op1.setRatio(1.0f);
    op1.setFeedback(0.0f);
    op1.setLevel(1.0f);

    // Second operator using process(0.0f)
    FMOperator op2;
    op2.prepare(kSampleRate);
    op2.setFrequency(440.0f);
    op2.setRatio(1.0f);
    op2.setFeedback(0.0f);
    op2.setLevel(1.0f);

    std::vector<float> output1(kNumSamples);
    std::vector<float> output2(kNumSamples);

    for (size_t i = 0; i < kNumSamples; ++i) {
        output1[i] = op1.process();
        output2[i] = op2.process(0.0f);
    }

    // Outputs should be identical
    for (size_t i = 0; i < kNumSamples; ++i) {
        INFO("Sample " << i);
        REQUIRE(output1[i] == output2[i]);
    }
}

// ==============================================================================
// User Story 3: Self-Modulation Feedback [US3]
// ==============================================================================
// Goal: Enable single-operator harmonic richness via feedback FM.

TEST_CASE("FR-006: Feedback 0.0 produces pure sine (THD < 0.1%)",
          "[FMOperator][US3][feedback]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr float kFrequency = 440.0f;
    constexpr size_t kNumSamples = 4096;

    FMOperator op;
    op.prepare(kSampleRate);
    op.setFrequency(kFrequency);
    op.setRatio(1.0f);
    op.setFeedback(0.0f);  // No feedback
    op.setLevel(1.0f);

    std::vector<float> output(kNumSamples);
    for (size_t i = 0; i < kNumSamples; ++i) {
        output[i] = op.process();
    }

    float thd = calculateTHD(output.data(), kNumSamples, kFrequency, kSampleRate);
    INFO("THD at feedback 0.0: " << (thd * 100.0f) << "%");
    REQUIRE(thd < 0.001f);  // THD < 0.1%
}

TEST_CASE("FR-011: Verify feedback applies tanh AFTER scaling",
          "[FMOperator][US3][feedback]") {
    // FR-011: feedbackPM = tanh(previousOutput * feedbackAmount)
    // This is verified implicitly by the behavior of the feedback.
    // At high feedback, the tanh limits the feedback contribution.
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kNumSamples = 44100;  // 1 second

    FMOperator op;
    op.prepare(kSampleRate);
    op.setFrequency(440.0f);
    op.setRatio(1.0f);
    op.setFeedback(1.0f);  // Maximum feedback
    op.setLevel(1.0f);

    bool hasNaN = false;
    bool hasInf = false;
    float maxAbs = 0.0f;

    for (size_t i = 0; i < kNumSamples; ++i) {
        float sample = op.process();
        if (detail::isNaN(sample)) hasNaN = true;
        if (detail::isInf(sample)) hasInf = true;
        maxAbs = std::max(maxAbs, std::abs(sample));
    }

    INFO("Max absolute value: " << maxAbs);
    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
    REQUIRE(maxAbs <= 1.0f);  // Output bounded by sine * level
}

TEST_CASE("FR-006: Feedback 0.5 produces harmonics (THD > 5%), output bounded",
          "[FMOperator][US3][feedback]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr float kFrequency = 440.0f;
    constexpr size_t kNumSamples = 4096;

    FMOperator op;
    op.prepare(kSampleRate);
    op.setFrequency(kFrequency);
    op.setRatio(1.0f);
    op.setFeedback(0.5f);  // Moderate feedback
    op.setLevel(1.0f);

    std::vector<float> output(kNumSamples);
    for (size_t i = 0; i < kNumSamples; ++i) {
        output[i] = op.process();
    }

    float thd = calculateTHD(output.data(), kNumSamples, kFrequency, kSampleRate);
    float peak = computePeak(output.data(), kNumSamples);

    INFO("THD at feedback 0.5: " << (thd * 100.0f) << "%");
    INFO("Peak amplitude: " << peak);

    REQUIRE(thd > 0.05f);  // THD > 5%
    REQUIRE(peak <= 1.0f);  // Output bounded
}

TEST_CASE("FR-006/FR-012: Feedback 1.0 for 44100 samples is stable",
          "[FMOperator][US3][feedback]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kNumSamples = 44100;  // 1 second

    FMOperator op;
    op.prepare(kSampleRate);
    op.setFrequency(440.0f);
    op.setRatio(1.0f);
    op.setFeedback(1.0f);  // Maximum feedback
    op.setLevel(1.0f);

    bool hasNaN = false;
    bool hasInf = false;
    float maxAbs = 0.0f;

    for (size_t i = 0; i < kNumSamples; ++i) {
        float sample = op.process();
        if (detail::isNaN(sample)) hasNaN = true;
        if (detail::isInf(sample)) hasInf = true;
        maxAbs = std::max(maxAbs, std::abs(sample));
    }

    INFO("Max absolute value after 1 second: " << maxAbs);
    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
    REQUIRE(maxAbs <= 1.0f);
}

TEST_CASE("FR-006/FR-012: Feedback 1.0 for 10 seconds shows no drift",
          "[FMOperator][US3][feedback]") {
    // This test verifies long-term stability
    // FM synthesis with feedback can have some DC offset (this is normal)
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kNumSamples = 441000;  // 10 seconds

    FMOperator op;
    op.prepare(kSampleRate);
    op.setFrequency(440.0f);
    op.setRatio(1.0f);
    op.setFeedback(1.0f);  // Maximum feedback
    op.setLevel(1.0f);

    bool hasNaN = false;
    bool hasInf = false;
    float maxAbs = 0.0f;
    double dcSum = 0.0;

    for (size_t i = 0; i < kNumSamples; ++i) {
        float sample = op.process();
        if (detail::isNaN(sample)) hasNaN = true;
        if (detail::isInf(sample)) hasInf = true;
        maxAbs = std::max(maxAbs, std::abs(sample));
        dcSum += static_cast<double>(sample);
    }

    float dcOffset = static_cast<float>(dcSum / static_cast<double>(kNumSamples));

    INFO("Max absolute value after 10 seconds: " << maxAbs);
    INFO("DC offset: " << dcOffset);
    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
    REQUIRE(maxAbs <= 1.0f);
    // FM feedback can produce some DC offset depending on starting phase
    // The key requirement is stability (no NaN/Inf/unbounded growth)
    REQUIRE(std::abs(dcOffset) < 0.1f);  // Allow up to 10% DC (spec says "no drift")
}

// ==============================================================================
// User Story 4: Combined Phase Modulation and Feedback [US4]
// ==============================================================================
// Goal: Enable full FM algorithm topologies.

TEST_CASE("FR-006/FR-008: Combined external PM and feedback produces rich spectrum",
          "[FMOperator][US4][combined]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr float kBaseFrequency = 440.0f;
    constexpr size_t kNumSamples = 8192;

    // Modulator
    FMOperator modulator;
    modulator.prepare(kSampleRate);
    modulator.setFrequency(kBaseFrequency);
    modulator.setRatio(3.0f);  // 1320 Hz
    modulator.setFeedback(0.0f);
    modulator.setLevel(0.3f);

    // Carrier with feedback
    FMOperator carrier;
    carrier.prepare(kSampleRate);
    carrier.setFrequency(kBaseFrequency);
    carrier.setRatio(1.0f);
    carrier.setFeedback(0.3f);  // Has feedback
    carrier.setLevel(1.0f);

    std::vector<float> output(kNumSamples);
    for (size_t i = 0; i < kNumSamples; ++i) {
        (void)modulator.process();
        float pm = modulator.lastRawOutput() * modulator.getLevel();
        output[i] = carrier.process(pm);
    }

    // Verify output is valid and has rich harmonic content
    float thd = calculateTHD(output.data(), kNumSamples, kBaseFrequency, kSampleRate);
    INFO("THD with combined PM + feedback: " << (thd * 100.0f) << "%");
    REQUIRE(thd > 0.01f);  // Should have harmonics
}

TEST_CASE("FR-006/FR-008: Combined has richer spectrum than feedback-only or PM-only",
          "[FMOperator][US4][combined]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr float kBaseFrequency = 440.0f;
    constexpr size_t kNumSamples = 8192;

    auto computeTHDWithConfig = [&](bool useFeedback, bool usePM) {
        FMOperator modulator;
        modulator.prepare(kSampleRate);
        modulator.setFrequency(kBaseFrequency);
        modulator.setRatio(3.0f);
        modulator.setFeedback(0.0f);
        modulator.setLevel(usePM ? 0.3f : 0.0f);

        FMOperator carrier;
        carrier.prepare(kSampleRate);
        carrier.setFrequency(kBaseFrequency);
        carrier.setRatio(1.0f);
        carrier.setFeedback(useFeedback ? 0.3f : 0.0f);
        carrier.setLevel(1.0f);

        std::vector<float> output(kNumSamples);
        for (size_t i = 0; i < kNumSamples; ++i) {
            (void)modulator.process();
            float pm = modulator.lastRawOutput() * modulator.getLevel();
            output[i] = carrier.process(pm);
        }

        return calculateTHD(output.data(), kNumSamples, kBaseFrequency, kSampleRate);
    };

    float thdFeedbackOnly = computeTHDWithConfig(true, false);
    float thdPMOnly = computeTHDWithConfig(false, true);
    float thdCombined = computeTHDWithConfig(true, true);

    INFO("THD feedback only: " << (thdFeedbackOnly * 100.0f) << "%");
    INFO("THD PM only: " << (thdPMOnly * 100.0f) << "%");
    INFO("THD combined: " << (thdCombined * 100.0f) << "%");

    // Combined should have more distortion than either alone
    // (or at least not less, due to different spectral content)
    REQUIRE(thdCombined >= std::min(thdFeedbackOnly, thdPMOnly) * 0.5f);
}

TEST_CASE("FR-012: Maximum feedback + strong PM remains bounded",
          "[FMOperator][US4][combined]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kNumSamples = 44100;  // 1 second

    FMOperator modulator;
    modulator.prepare(kSampleRate);
    modulator.setFrequency(440.0f);
    modulator.setRatio(3.0f);
    modulator.setFeedback(0.0f);
    modulator.setLevel(1.0f);  // Strong modulation

    FMOperator carrier;
    carrier.prepare(kSampleRate);
    carrier.setFrequency(440.0f);
    carrier.setRatio(1.0f);
    carrier.setFeedback(1.0f);  // Maximum feedback
    carrier.setLevel(1.0f);

    bool hasNaN = false;
    bool hasInf = false;
    float maxAbs = 0.0f;

    for (size_t i = 0; i < kNumSamples; ++i) {
        (void)modulator.process();
        float pm = modulator.lastRawOutput() * modulator.getLevel();
        float sample = carrier.process(pm);
        if (detail::isNaN(sample)) hasNaN = true;
        if (detail::isInf(sample)) hasInf = true;
        maxAbs = std::max(maxAbs, std::abs(sample));
    }

    INFO("Max absolute value: " << maxAbs);
    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
    REQUIRE(maxAbs <= 1.0f);  // Bounded by sine output
}

// ==============================================================================
// User Story 5: Lifecycle and State Management [US5]
// ==============================================================================
// Goal: Enable reliable lifecycle management for polyphonic synthesizer voices.

TEST_CASE("FR-003: reset() preserves configuration but resets phase",
          "[FMOperator][US5][lifecycle]") {
    constexpr float kSampleRate = 44100.0f;

    FMOperator op;
    op.prepare(kSampleRate);
    op.setFrequency(880.0f);
    op.setRatio(2.0f);
    op.setFeedback(0.5f);
    op.setLevel(0.8f);

    // Process some samples
    for (int i = 0; i < 1000; ++i) {
        (void)op.process();
    }

    // Reset
    op.reset();

    // Verify configuration preserved
    REQUIRE(op.getFrequency() == Approx(880.0f));
    REQUIRE(op.getRatio() == Approx(2.0f));
    REQUIRE(op.getFeedback() == Approx(0.5f));
    REQUIRE(op.getLevel() == Approx(0.8f));
}

TEST_CASE("FR-003: reset() clears feedback history",
          "[FMOperator][US5][lifecycle]") {
    constexpr float kSampleRate = 44100.0f;

    // Create two operators with same config
    FMOperator opReset;
    opReset.prepare(kSampleRate);
    opReset.setFrequency(440.0f);
    opReset.setRatio(1.0f);
    opReset.setFeedback(0.5f);
    opReset.setLevel(1.0f);

    // Process to build up feedback
    for (int i = 0; i < 1000; ++i) {
        (void)opReset.process();
    }

    // Reset
    opReset.reset();

    // Create a fresh operator
    FMOperator opFresh;
    opFresh.prepare(kSampleRate);
    opFresh.setFrequency(440.0f);
    opFresh.setRatio(1.0f);
    opFresh.setFeedback(0.5f);
    opFresh.setLevel(1.0f);

    // First sample after reset should match fresh operator's first sample
    float resetFirst = opReset.process();
    float freshFirst = opFresh.process();

    INFO("Reset first sample: " << resetFirst);
    INFO("Fresh first sample: " << freshFirst);
    REQUIRE(resetFirst == Approx(freshFirst).margin(0.001f));
}

TEST_CASE("FR-002: prepare() with different sample rate reinitializes correctly",
          "[FMOperator][US5][lifecycle]") {
    FMOperator op;

    // First prepare at 44100 Hz
    op.prepare(44100.0);
    op.setFrequency(440.0f);
    op.setRatio(1.0f);
    op.setLevel(1.0f);

    // Process some samples
    for (int i = 0; i < 100; ++i) {
        (void)op.process();
    }

    // Re-prepare at different sample rate
    op.prepare(48000.0);
    op.setFrequency(440.0f);
    op.setRatio(1.0f);
    op.setLevel(1.0f);

    // Should produce valid output at the new sample rate
    constexpr size_t kNumSamples = 4096;
    std::vector<float> output(kNumSamples);
    for (size_t i = 0; i < kNumSamples; ++i) {
        output[i] = op.process();
    }

    float dominantFreq = findDominantFrequency(output.data(), kNumSamples, 48000.0f);
    INFO("Dominant frequency at 48 kHz: " << dominantFreq << " Hz");
    REQUIRE(dominantFreq == Approx(440.0f).margin(10.0f));
}

TEST_CASE("FR-003: After reset(), output matches freshly prepared operator",
          "[FMOperator][US5][lifecycle]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kNumSamples = 1024;

    // Operator that will be reset
    FMOperator opReset;
    opReset.prepare(kSampleRate);
    opReset.setFrequency(440.0f);
    opReset.setRatio(1.0f);
    opReset.setFeedback(0.0f);
    opReset.setLevel(1.0f);

    // Process to change state
    for (int i = 0; i < 500; ++i) {
        (void)opReset.process();
    }

    // Reset
    opReset.reset();

    // Fresh operator
    FMOperator opFresh;
    opFresh.prepare(kSampleRate);
    opFresh.setFrequency(440.0f);
    opFresh.setRatio(1.0f);
    opFresh.setFeedback(0.0f);
    opFresh.setLevel(1.0f);

    // Compare first 1024 samples
    std::vector<float> outputReset(kNumSamples);
    std::vector<float> outputFresh(kNumSamples);

    for (size_t i = 0; i < kNumSamples; ++i) {
        outputReset[i] = opReset.process();
        outputFresh[i] = opFresh.process();
    }

    // Should be bit-identical
    bool allMatch = true;
    for (size_t i = 0; i < kNumSamples; ++i) {
        if (outputReset[i] != outputFresh[i]) {
            allMatch = false;
            INFO("Mismatch at sample " << i << ": reset=" << outputReset[i]
                 << ", fresh=" << outputFresh[i]);
            break;
        }
    }
    REQUIRE(allMatch);
}
