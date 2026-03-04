// ==============================================================================
// Tests: Dual-Window STFT Configuration Validation
// ==============================================================================
// Validates that the two STFT configurations (long=4096/hop2048,
// short=1024/hop512) with Blackman-Harris windows produce correct
// spectral analysis output.
//
// Covers: FR-018 (dual STFT passes), FR-019 (Blackman-Harris),
//         FR-020 (update rate difference), FR-021 (magnitude output)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <krate/dsp/primitives/stft.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/core/math_constants.h>

#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Configuration Constants (mirrors plugins/innexus/src/dsp/dual_stft_config.h)
// =============================================================================

namespace {

constexpr size_t kLongFftSize = 4096;
constexpr size_t kLongHopSize = 2048;
constexpr size_t kShortFftSize = 1024;
constexpr size_t kShortHopSize = 512;
constexpr double kSampleRate = 44100.0;

/// Generate a sine wave signal
void generateSine(float* output, size_t numSamples,
                  float frequency, double sampleRate, float amplitude = 1.0f) {
    for (size_t i = 0; i < numSamples; ++i) {
        output[i] = amplitude * std::sin(
            kTwoPi * frequency * static_cast<float>(i) / static_cast<float>(sampleRate));
    }
}

} // anonymous namespace

// =============================================================================
// FR-018: Dual STFT Bin Count Verification
// =============================================================================

TEST_CASE("Dual STFT: Long window (4096) produces 2049 bins",
          "[innexus][stft][dual-stft][FR-018]") {
    SpectralBuffer longBuffer;
    longBuffer.prepare(kLongFftSize);

    REQUIRE(longBuffer.numBins() == 2049);
}

TEST_CASE("Dual STFT: Short window (1024) produces 513 bins",
          "[innexus][stft][dual-stft][FR-018]") {
    SpectralBuffer shortBuffer;
    shortBuffer.prepare(kShortFftSize);

    REQUIRE(shortBuffer.numBins() == 513);
}

// =============================================================================
// Frequency Resolution Verification
// =============================================================================

TEST_CASE("Dual STFT: Long window bin spacing is ~10.77 Hz",
          "[innexus][stft][dual-stft][FR-018]") {
    const double binSpacing = kSampleRate / static_cast<double>(kLongFftSize);
    // 44100 / 4096 = 10.7666...
    REQUIRE(binSpacing == Approx(10.7666).margin(0.001));
}

TEST_CASE("Dual STFT: Short window bin spacing is ~43.07 Hz",
          "[innexus][stft][dual-stft][FR-018]") {
    const double binSpacing = kSampleRate / static_cast<double>(kShortFftSize);
    // 44100 / 1024 = 43.066...
    REQUIRE(binSpacing == Approx(43.066).margin(0.001));
}

TEST_CASE("Dual STFT: Long window has higher frequency resolution than short window",
          "[innexus][stft][dual-stft][FR-018]") {
    const double longBinSpacing = kSampleRate / static_cast<double>(kLongFftSize);
    const double shortBinSpacing = kSampleRate / static_cast<double>(kShortFftSize);

    // Long window bin spacing should be ~4x smaller (better resolution)
    REQUIRE(longBinSpacing < shortBinSpacing);
    REQUIRE(shortBinSpacing / longBinSpacing == Approx(4.0).margin(0.01));
}

// =============================================================================
// FR-019: BlackmanHarris Window Usage
// =============================================================================

TEST_CASE("Dual STFT: Long STFT accepts BlackmanHarris window without failure",
          "[innexus][stft][dual-stft][FR-019]") {
    STFT longStft;
    longStft.prepare(kLongFftSize, kLongHopSize, WindowType::BlackmanHarris);

    REQUIRE(longStft.isPrepared());
    REQUIRE(longStft.fftSize() == kLongFftSize);
    REQUIRE(longStft.hopSize() == kLongHopSize);
    REQUIRE(longStft.windowType() == WindowType::BlackmanHarris);
}

TEST_CASE("Dual STFT: Short STFT accepts BlackmanHarris window without failure",
          "[innexus][stft][dual-stft][FR-019]") {
    STFT shortStft;
    shortStft.prepare(kShortFftSize, kShortHopSize, WindowType::BlackmanHarris);

    REQUIRE(shortStft.isPrepared());
    REQUIRE(shortStft.fftSize() == kShortFftSize);
    REQUIRE(shortStft.hopSize() == kShortHopSize);
    REQUIRE(shortStft.windowType() == WindowType::BlackmanHarris);
}

// =============================================================================
// FR-020: Update Rate Difference
// =============================================================================

TEST_CASE("Dual STFT: Long window update rate is slower than short window by 4x",
          "[innexus][stft][dual-stft][FR-020]") {
    // Long: 44100 / 2048 = 21.533 Hz
    // Short: 44100 / 512 = 86.133 Hz
    const double longUpdateRate = kSampleRate / static_cast<double>(kLongHopSize);
    const double shortUpdateRate = kSampleRate / static_cast<double>(kShortHopSize);

    REQUIRE(longUpdateRate == Approx(21.533).margin(0.01));
    REQUIRE(shortUpdateRate == Approx(86.133).margin(0.01));

    // Short window updates 4x more frequently
    REQUIRE(shortUpdateRate / longUpdateRate == Approx(4.0).margin(0.01));
}

TEST_CASE("Dual STFT: Long window produces fewer frames than short for same input",
          "[innexus][stft][dual-stft][FR-020]") {
    STFT longStft;
    STFT shortStft;
    SpectralBuffer longBuffer;
    SpectralBuffer shortBuffer;

    longStft.prepare(kLongFftSize, kLongHopSize, WindowType::BlackmanHarris);
    shortStft.prepare(kShortFftSize, kShortHopSize, WindowType::BlackmanHarris);
    longBuffer.prepare(kLongFftSize);
    shortBuffer.prepare(kShortFftSize);

    // Generate 8192 samples of test signal (enough for multiple frames)
    constexpr size_t kTestLength = 8192;
    std::vector<float> signal(kTestLength);
    generateSine(signal.data(), kTestLength, 440.0f, kSampleRate);

    // Push all samples into both STFTs
    longStft.pushSamples(signal.data(), kTestLength);
    shortStft.pushSamples(signal.data(), kTestLength);

    // Count how many frames each produces
    int longFrames = 0;
    while (longStft.canAnalyze()) {
        longStft.analyze(longBuffer);
        ++longFrames;
    }

    int shortFrames = 0;
    while (shortStft.canAnalyze()) {
        shortStft.analyze(shortBuffer);
        ++shortFrames;
    }

    // Short window should produce ~4x more frames
    REQUIRE(shortFrames > longFrames);
    // With 8192 samples:
    // Long: needs 4096 for first frame, then 2048 per hop -> (8192-4096)/2048 + 1 = 3 frames
    // Short: needs 1024 for first frame, then 512 per hop -> (8192-1024)/512 + 1 = 15 frames
    REQUIRE(longFrames >= 2);
    REQUIRE(shortFrames >= 8);
}

// =============================================================================
// FR-021: Magnitude Output Validation
// =============================================================================

TEST_CASE("Dual STFT: Both windows produce non-zero magnitude at signal frequency",
          "[innexus][stft][dual-stft][FR-021]") {
    STFT longStft;
    STFT shortStft;
    SpectralBuffer longBuffer;
    SpectralBuffer shortBuffer;

    longStft.prepare(kLongFftSize, kLongHopSize, WindowType::BlackmanHarris);
    shortStft.prepare(kShortFftSize, kShortHopSize, WindowType::BlackmanHarris);
    longBuffer.prepare(kLongFftSize);
    shortBuffer.prepare(kShortFftSize);

    // Generate a 440 Hz sine wave (long enough for analysis)
    constexpr float kTestFreq = 440.0f;
    constexpr size_t kTestLength = 8192;
    std::vector<float> signal(kTestLength);
    generateSine(signal.data(), kTestLength, kTestFreq, kSampleRate);

    // Feed into both STFTs and analyze one frame each
    longStft.pushSamples(signal.data(), kTestLength);
    shortStft.pushSamples(signal.data(), kTestLength);

    REQUIRE(longStft.canAnalyze());
    REQUIRE(shortStft.canAnalyze());

    longStft.analyze(longBuffer);
    shortStft.analyze(shortBuffer);

    // Find the expected bin for 440 Hz in each
    // Long: bin = round(440 * 4096 / 44100) = round(40.87) = 41
    const size_t longExpectedBin = static_cast<size_t>(
        std::round(kTestFreq * static_cast<float>(kLongFftSize) / static_cast<float>(kSampleRate)));
    // Short: bin = round(440 * 1024 / 44100) = round(10.22) = 10
    const size_t shortExpectedBin = static_cast<size_t>(
        std::round(kTestFreq * static_cast<float>(kShortFftSize) / static_cast<float>(kSampleRate)));

    // Both should have significant magnitude at the expected bin
    const float longMag = longBuffer.getMagnitude(longExpectedBin);
    const float shortMag = shortBuffer.getMagnitude(shortExpectedBin);

    INFO("Long window expected bin: " << longExpectedBin << ", magnitude: " << longMag);
    INFO("Short window expected bin: " << shortExpectedBin << ", magnitude: " << shortMag);

    REQUIRE(longMag > 0.0f);
    REQUIRE(shortMag > 0.0f);

    // The peak bin should be the dominant bin in the neighborhood
    // Check that it's larger than neighbors (peak detection suitability)
    if (longExpectedBin > 0 && longExpectedBin < longBuffer.numBins() - 1) {
        REQUIRE(longMag > longBuffer.getMagnitude(longExpectedBin - 3));
        REQUIRE(longMag > longBuffer.getMagnitude(longExpectedBin + 3));
    }

    if (shortExpectedBin > 0 && shortExpectedBin < shortBuffer.numBins() - 1) {
        REQUIRE(shortMag > shortBuffer.getMagnitude(shortExpectedBin - 3));
        REQUIRE(shortMag > shortBuffer.getMagnitude(shortExpectedBin + 3));
    }
}

TEST_CASE("Dual STFT: Identical signals produce consistent magnitude peaks",
          "[innexus][stft][dual-stft][FR-021]") {
    STFT longStft;
    STFT shortStft;
    SpectralBuffer longBuffer;
    SpectralBuffer shortBuffer;

    longStft.prepare(kLongFftSize, kLongHopSize, WindowType::BlackmanHarris);
    shortStft.prepare(kShortFftSize, kShortHopSize, WindowType::BlackmanHarris);
    longBuffer.prepare(kLongFftSize);
    shortBuffer.prepare(kShortFftSize);

    // Use a frequency that's well within both windows' resolution
    constexpr float kTestFreq = 1000.0f;
    constexpr size_t kTestLength = 8192;
    std::vector<float> signal(kTestLength);
    generateSine(signal.data(), kTestLength, kTestFreq, kSampleRate);

    longStft.pushSamples(signal.data(), kTestLength);
    shortStft.pushSamples(signal.data(), kTestLength);

    longStft.analyze(longBuffer);
    shortStft.analyze(shortBuffer);

    // Find peak bin in each spectrum
    float longPeakMag = 0.0f;
    size_t longPeakBin = 0;
    for (size_t i = 1; i < longBuffer.numBins() - 1; ++i) {
        const float mag = longBuffer.getMagnitude(i);
        if (mag > longPeakMag) {
            longPeakMag = mag;
            longPeakBin = i;
        }
    }

    float shortPeakMag = 0.0f;
    size_t shortPeakBin = 0;
    for (size_t i = 1; i < shortBuffer.numBins() - 1; ++i) {
        const float mag = shortBuffer.getMagnitude(i);
        if (mag > shortPeakMag) {
            shortPeakMag = mag;
            shortPeakBin = i;
        }
    }

    // Both should find the peak at the expected frequency
    const double longPeakFreq = static_cast<double>(longPeakBin) * kSampleRate / static_cast<double>(kLongFftSize);
    const double shortPeakFreq = static_cast<double>(shortPeakBin) * kSampleRate / static_cast<double>(kShortFftSize);

    INFO("Long window peak: bin " << longPeakBin << " = " << longPeakFreq << " Hz");
    INFO("Short window peak: bin " << shortPeakBin << " = " << shortPeakFreq << " Hz");

    // Both should be within one bin of 1000 Hz
    const double longBinSpacing = kSampleRate / static_cast<double>(kLongFftSize);
    const double shortBinSpacing = kSampleRate / static_cast<double>(kShortFftSize);

    REQUIRE(std::abs(longPeakFreq - 1000.0) < longBinSpacing);
    REQUIRE(std::abs(shortPeakFreq - 1000.0) < shortBinSpacing);
}

TEST_CASE("Dual STFT: Both windows produce magnitude suitable for peak detection",
          "[innexus][stft][dual-stft][FR-021]") {
    STFT longStft;
    STFT shortStft;
    SpectralBuffer longBuffer;
    SpectralBuffer shortBuffer;

    longStft.prepare(kLongFftSize, kLongHopSize, WindowType::BlackmanHarris);
    shortStft.prepare(kShortFftSize, kShortHopSize, WindowType::BlackmanHarris);
    longBuffer.prepare(kLongFftSize);
    shortBuffer.prepare(kShortFftSize);

    // Generate a multi-harmonic signal (like a real instrument)
    constexpr float kFundamental = 220.0f;
    constexpr size_t kTestLength = 8192;
    std::vector<float> signal(kTestLength, 0.0f);

    // Add fundamental + 3 harmonics with decreasing amplitudes
    std::vector<float> temp(kTestLength);
    const float harmonicAmplitudes[] = {1.0f, 0.5f, 0.25f, 0.125f};

    for (int h = 0; h < 4; ++h) {
        const float freq = kFundamental * static_cast<float>(h + 1);
        generateSine(temp.data(), kTestLength, freq, kSampleRate, harmonicAmplitudes[h]);
        for (size_t i = 0; i < kTestLength; ++i) {
            signal[i] += temp[i];
        }
    }

    longStft.pushSamples(signal.data(), kTestLength);
    shortStft.pushSamples(signal.data(), kTestLength);

    longStft.analyze(longBuffer);
    shortStft.analyze(shortBuffer);

    // Verify that magnitude values are finite and non-negative
    bool longAllFinite = true;
    bool shortAllFinite = true;
    bool longAllNonNeg = true;
    bool shortAllNonNeg = true;

    for (size_t i = 0; i < longBuffer.numBins(); ++i) {
        const float mag = longBuffer.getMagnitude(i);
        if (!std::isfinite(mag)) longAllFinite = false;
        if (mag < 0.0f) longAllNonNeg = false;
    }

    for (size_t i = 0; i < shortBuffer.numBins(); ++i) {
        const float mag = shortBuffer.getMagnitude(i);
        if (!std::isfinite(mag)) shortAllFinite = false;
        if (mag < 0.0f) shortAllNonNeg = false;
    }

    REQUIRE(longAllFinite);
    REQUIRE(shortAllFinite);
    REQUIRE(longAllNonNeg);
    REQUIRE(shortAllNonNeg);

    // Verify we can detect peaks at harmonic frequencies in the long window
    // (long window has better frequency resolution for resolving harmonics)
    for (int h = 0; h < 4; ++h) {
        const float freq = kFundamental * static_cast<float>(h + 1);
        const size_t expectedBin = static_cast<size_t>(
            std::round(freq * static_cast<float>(kLongFftSize) / static_cast<float>(kSampleRate)));

        if (expectedBin > 0 && expectedBin < longBuffer.numBins() - 1) {
            const float centerMag = longBuffer.getMagnitude(expectedBin);
            INFO("Harmonic " << (h + 1) << " at " << freq << " Hz, bin "
                 << expectedBin << ", magnitude " << centerMag);
            REQUIRE(centerMag > 0.01f);
        }
    }
}
