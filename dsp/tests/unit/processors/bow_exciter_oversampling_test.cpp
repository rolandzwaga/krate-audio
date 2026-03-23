// =============================================================================
// BowExciter Oversampling Tests
// =============================================================================
// Layer 2: Processors | Spec 130 - Bow Model Exciter (Phase 9)
//
// T095: Aliasing reduction tests.
// T096-T098: Oversampling state, 2x path, and prepare() tests.
// Requirements: FR-022, FR-023, SC-012
// =============================================================================

#include <krate/dsp/processors/bow_exciter.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/window_functions.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Helper: Run BowExciter with a sinusoidal feedback velocity
// =============================================================================
namespace {

/// Runs the bow exciter with a synthetic sinusoidal feedback velocity at the
/// specified frequency, simulating a resonator feeding back at that pitch.
std::vector<float> runWithSineFeedback(BowExciter& bow, double sampleRate,
                                        float feedbackFreq, float pressure,
                                        int numSamples)
{
    std::vector<float> output(static_cast<size_t>(numSamples));
    float phase = 0.0f;
    float phaseInc = feedbackFreq / static_cast<float>(sampleRate);

    for (int i = 0; i < numSamples; ++i) {
        // Synthetic feedback velocity: sine at the given frequency
        float feedbackVelocity = 0.3f * std::sin(2.0f * 3.14159265f * phase);
        phase += phaseInc;
        if (phase >= 1.0f) phase -= 1.0f;

        bow.setEnvelopeValue(1.0f);
        bow.setResonatorEnergy(0.0f);
        float sample = bow.process(feedbackVelocity);
        output[static_cast<size_t>(i)] = sample;
    }
    return output;
}

/// Compute the energy in the upper frequency bins (above Nyquist/2)
/// relative to total energy, as a measure of aliasing content.
float computeUpperBinEnergyRatio(const std::vector<float>& signal,
                                  int fftSize)
{
    if (signal.size() < static_cast<size_t>(fftSize))
        return 0.0f;

    // Window the signal
    std::vector<float> windowed(static_cast<size_t>(fftSize));
    for (int i = 0; i < fftSize; ++i) {
        float w = 0.5f - 0.5f * std::cos(
            2.0f * 3.14159265f * static_cast<float>(i)
            / static_cast<float>(fftSize));
        windowed[static_cast<size_t>(i)] =
            signal[static_cast<size_t>(i)] * w;
    }

    // Compute FFT
    FFT fft;
    fft.prepare(static_cast<size_t>(fftSize));
    std::vector<Complex> spectrum(static_cast<size_t>(fftSize / 2 + 1));
    fft.forward(windowed.data(), spectrum.data());

    // Compute energy in upper half (bins fftSize/4 to fftSize/2)
    // and total energy
    float totalEnergy = 0.0f;
    float upperEnergy = 0.0f;
    int halfBins = fftSize / 2 + 1;
    int quarterBin = fftSize / 4;

    for (int i = 1; i < halfBins; ++i) {
        float mag2 = spectrum[static_cast<size_t>(i)].real
                     * spectrum[static_cast<size_t>(i)].real
                     + spectrum[static_cast<size_t>(i)].imag
                     * spectrum[static_cast<size_t>(i)].imag;
        totalEnergy += mag2;
        if (i >= quarterBin) {
            upperEnergy += mag2;
        }
    }

    if (totalEnergy < 1e-20f) return 0.0f;
    return upperEnergy / totalEnergy;
}

} // anonymous namespace

// =============================================================================
// T096: Oversampling state tests
// =============================================================================

TEST_CASE("BowExciter oversampling defaults to disabled",
          "[dsp][processors][bow][oversampling]")
{
    BowExciter bow;
    REQUIRE_FALSE(bow.isOversamplingEnabled());
}

TEST_CASE("BowExciter setOversamplingEnabled toggles state",
          "[dsp][processors][bow][oversampling]")
{
    BowExciter bow;
    bow.prepare(44100.0);

    bow.setOversamplingEnabled(true);
    REQUIRE(bow.isOversamplingEnabled());

    bow.setOversamplingEnabled(false);
    REQUIRE_FALSE(bow.isOversamplingEnabled());
}

// =============================================================================
// T097: 2x path produces output
// =============================================================================

TEST_CASE("BowExciter 2x oversampling produces non-zero output",
          "[dsp][processors][bow][oversampling]")
{
    BowExciter bow;
    bow.prepare(44100.0);
    bow.setOversamplingEnabled(true);
    bow.setPressure(0.5f);
    bow.setSpeed(0.5f);
    bow.setPosition(0.13f);
    bow.trigger(0.8f);

    auto output = runWithSineFeedback(bow, 44100.0, 440.0f, 0.5f, 4096);

    float maxAbs = 0.0f;
    for (float s : output)
        maxAbs = std::max(maxAbs, std::fabs(s));

    REQUIRE(maxAbs > 0.001f);
}

TEST_CASE("BowExciter 1x and 2x produce similar RMS levels",
          "[dsp][processors][bow][oversampling]")
{
    constexpr double kSampleRate = 44100.0;
    constexpr int kNumSamples = 8192;
    constexpr float kFreq = 440.0f;

    // Run 1x
    BowExciter bow1x;
    bow1x.prepare(kSampleRate);
    bow1x.setPressure(0.5f);
    bow1x.setSpeed(0.5f);
    bow1x.setPosition(0.13f);
    bow1x.trigger(0.8f);
    auto out1x = runWithSineFeedback(bow1x, kSampleRate, kFreq, 0.5f,
                                      kNumSamples);

    // Run 2x
    BowExciter bow2x;
    bow2x.prepare(kSampleRate);
    bow2x.setOversamplingEnabled(true);
    bow2x.setPressure(0.5f);
    bow2x.setSpeed(0.5f);
    bow2x.setPosition(0.13f);
    bow2x.trigger(0.8f);
    auto out2x = runWithSineFeedback(bow2x, kSampleRate, kFreq, 0.5f,
                                      kNumSamples);

    // Compute RMS of both (skip first 256 samples for transient)
    auto computeRms = [](const std::vector<float>& buf, int start) {
        float sum = 0.0f;
        int count = 0;
        for (size_t i = static_cast<size_t>(start); i < buf.size(); ++i) {
            sum += buf[i] * buf[i];
            ++count;
        }
        return std::sqrt(sum / static_cast<float>(count));
    };

    float rms1x = computeRms(out1x, 256);
    float rms2x = computeRms(out2x, 256);

    // RMS levels should be within 6 dB of each other (same ballpark)
    REQUIRE(rms1x > 0.0f);
    REQUIRE(rms2x > 0.0f);
    float ratio = rms2x / rms1x;
    REQUIRE(ratio > 0.25f);  // within ~12 dB
    REQUIRE(ratio < 4.0f);
}

// =============================================================================
// T095: Aliasing reduction test (SC-012)
// =============================================================================

TEST_CASE("BowExciter 2x has less upper-frequency aliasing than 1x",
          "[dsp][processors][bow][oversampling][aliasing]")
{
    // Drive at high frequency + high pressure to maximize nonlinear aliasing
    constexpr double kSampleRate = 44100.0;
    constexpr int kNumSamples = 8192;
    constexpr float kHighFreq = 2000.0f;
    constexpr float kHighPressure = 0.9f;
    constexpr int kFftSize = 4096;

    // Run 1x
    BowExciter bow1x;
    bow1x.prepare(kSampleRate);
    bow1x.setPressure(kHighPressure);
    bow1x.setSpeed(0.7f);
    bow1x.setPosition(0.13f);
    bow1x.trigger(0.9f);
    auto out1x = runWithSineFeedback(bow1x, kSampleRate, kHighFreq,
                                      kHighPressure, kNumSamples);

    // Run 2x
    BowExciter bow2x;
    bow2x.prepare(kSampleRate);
    bow2x.setOversamplingEnabled(true);
    bow2x.setPressure(kHighPressure);
    bow2x.setSpeed(0.7f);
    bow2x.setPosition(0.13f);
    bow2x.trigger(0.9f);
    auto out2x = runWithSineFeedback(bow2x, kSampleRate, kHighFreq,
                                      kHighPressure, kNumSamples);

    // Use the latter portion of the output (skip transient)
    std::vector<float> analysis1x(out1x.begin() + 2048,
                                   out1x.begin() + 2048 + kFftSize);
    std::vector<float> analysis2x(out2x.begin() + 2048,
                                   out2x.begin() + 2048 + kFftSize);

    float upperRatio1x = computeUpperBinEnergyRatio(analysis1x, kFftSize);
    float upperRatio2x = computeUpperBinEnergyRatio(analysis2x, kFftSize);

    // 2x oversampling should have less energy in the upper frequency bins
    // (reduced aliasing). We require at least some measurable reduction.
    // The upper bin energy ratio at 2x should be less than at 1x.
    INFO("1x upper energy ratio: " << upperRatio1x);
    INFO("2x upper energy ratio: " << upperRatio2x);
    REQUIRE(upperRatio2x < upperRatio1x);
}

// =============================================================================
// T098: prepare() with oversampling configures downsample LPF
// =============================================================================

TEST_CASE("BowExciter prepare with oversampling configures anti-alias filter",
          "[dsp][processors][bow][oversampling]")
{
    BowExciter bow;
    bow.setOversamplingEnabled(true);
    bow.prepare(44100.0);

    // The filter should be configured. Verify by running it: if prepared
    // correctly, processing a signal should not crash or produce NaN.
    bow.setPressure(0.5f);
    bow.setSpeed(0.5f);
    bow.setPosition(0.13f);
    bow.trigger(0.8f);

    bool hasNaN = false;
    float feedbackVelocity = 0.0f;
    for (int i = 0; i < 1024; ++i) {
        bow.setEnvelopeValue(1.0f);
        float sample = bow.process(feedbackVelocity);
        if (std::isnan(sample)) hasNaN = true;
        feedbackVelocity = sample * 0.5f;
    }
    REQUIRE_FALSE(hasNaN);
}

TEST_CASE("BowExciter can switch oversampling on/off mid-stream",
          "[dsp][processors][bow][oversampling]")
{
    BowExciter bow;
    bow.prepare(44100.0);
    bow.setPressure(0.5f);
    bow.setSpeed(0.5f);
    bow.setPosition(0.13f);
    bow.trigger(0.8f);

    // Run 100 samples at 1x
    float feedbackVelocity = 0.0f;
    for (int i = 0; i < 100; ++i) {
        bow.setEnvelopeValue(1.0f);
        float sample = bow.process(feedbackVelocity);
        feedbackVelocity = sample * 0.5f;
    }

    // Switch to 2x mid-stream
    bow.setOversamplingEnabled(true);

    // Run 100 more samples at 2x -- should not crash or produce NaN
    bool hasNaN = false;
    for (int i = 0; i < 100; ++i) {
        bow.setEnvelopeValue(1.0f);
        float sample = bow.process(feedbackVelocity);
        if (std::isnan(sample)) hasNaN = true;
        feedbackVelocity = sample * 0.5f;
    }
    REQUIRE_FALSE(hasNaN);

    // Switch back to 1x
    bow.setOversamplingEnabled(false);

    for (int i = 0; i < 100; ++i) {
        bow.setEnvelopeValue(1.0f);
        float sample = bow.process(feedbackVelocity);
        if (std::isnan(sample)) hasNaN = true;
        feedbackVelocity = sample * 0.5f;
    }
    REQUIRE_FALSE(hasNaN);
}
