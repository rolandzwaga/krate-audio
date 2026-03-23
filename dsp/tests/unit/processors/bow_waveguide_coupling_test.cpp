// =============================================================================
// Bow + Waveguide Coupling Tests (Spec 130, Phase 10)
// =============================================================================
// Tests for BowExciter coupled with WaveguideString resonator.
// Covers: SC-003 (sustained tone), SC-010 (pitch-range loudness consistency),
// T108 (multiple independent instances).
// =============================================================================

#include <krate/dsp/processors/bow_exciter.h>
#include <krate/dsp/processors/waveguide_string.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

namespace {

constexpr double kSampleRate = 44100.0;

/// Helper: Run BowExciter + WaveguideString in a feedback loop for a given
/// number of samples. Returns the output buffer.
std::vector<float> runBowWaveguideFeedbackLoop(
    float frequency,
    int numSamples,
    float pressure = 0.3f,
    float speed = 0.5f,
    float position = 0.13f,
    float velocity = 0.8f)
{
    BowExciter bow;
    bow.prepare(kSampleRate);
    bow.setPressure(pressure);
    bow.setSpeed(speed);
    bow.setPosition(position);

    WaveguideString waveguide;
    waveguide.prepare(kSampleRate);
    waveguide.prepareVoice(0);
    waveguide.setDecay(2.0f);
    waveguide.setBrightness(0.3f);
    waveguide.setStiffness(0.0f);
    waveguide.setPickPosition(position);
    waveguide.noteOn(frequency, velocity);

    bow.trigger(velocity);

    std::vector<float> output(static_cast<size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i) {
        float fbVel = waveguide.getFeedbackVelocity();
        bow.setEnvelopeValue(1.0f);
        bow.setResonatorEnergy(waveguide.getControlEnergy());
        float excitation = bow.process(fbVel);
        float out = waveguide.process(excitation);
        output[static_cast<size_t>(i)] = out;
    }
    return output;
}

/// Compute RMS of a buffer range.
float computeRMS(const std::vector<float>& buffer, size_t start, size_t end)
{
    if (start >= end || end > buffer.size())
        return 0.0f;
    double sumSq = 0.0;
    for (size_t i = start; i < end; ++i)
        sumSq += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(end - start)));
}

} // namespace

// =============================================================================
// T107a: SC-003 - BowExciter + WaveguideString sustained tone at 220 Hz
// =============================================================================

TEST_CASE("BowExciter + WaveguideString produces sustained tone at 220 Hz (SC-003)",
          "[processors][bow_exciter][waveguide][coupling]")
{
    constexpr int kDuration = 88200; // 2 seconds at 44100 Hz

    auto output = runBowWaveguideFeedbackLoop(220.0f, kDuration);

    // Measure peak in last 0.5s (steady state)
    float peakLast500ms = 0.0f;
    for (size_t i = output.size() - 22050; i < output.size(); ++i)
        peakLast500ms = std::max(peakLast500ms, std::abs(output[i]));

    INFO("Peak in last 500ms: " << peakLast500ms);

    // SC-003: sustained tone should not decay to silence
    // -40 dBFS = 0.01
    REQUIRE(peakLast500ms > 0.01f);

    // Also verify no NaN/Inf
    bool hasNaN = false;
    bool hasInf = false;
    for (float s : output) {
        if (s != s) hasNaN = true;        // NaN check (works with -ffast-math)
        if (s > 1e30f || s < -1e30f) hasInf = true;
    }
    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
}

// =============================================================================
// T108: Multiple BowExciter instances don't share state
// =============================================================================

TEST_CASE("Multiple BowExciter instances produce independent output",
          "[processors][bow_exciter]")
{
    constexpr int kNumSamples = 4410; // 100ms

    // Create 4 independent BowExciter instances with different velocities
    std::array<BowExciter, 4> bows;
    std::array<float, 4> velocities = {0.2f, 0.5f, 0.8f, 1.0f};

    for (int i = 0; i < 4; ++i) {
        bows[static_cast<size_t>(i)].prepare(kSampleRate);
        bows[static_cast<size_t>(i)].setPressure(0.3f);
        bows[static_cast<size_t>(i)].setSpeed(0.5f);
        bows[static_cast<size_t>(i)].setPosition(0.13f);
        bows[static_cast<size_t>(i)].trigger(velocities[static_cast<size_t>(i)]);
    }

    // Accumulate output from each instance
    std::array<float, 4> maxAbs = {0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> fbVels = {0.0f, 0.0f, 0.0f, 0.0f};

    for (int s = 0; s < kNumSamples; ++s) {
        for (int i = 0; i < 4; ++i) {
            auto idx = static_cast<size_t>(i);
            bows[idx].setEnvelopeValue(1.0f);
            float out = bows[idx].process(fbVels[idx]);
            maxAbs[idx] = std::max(maxAbs[idx], std::abs(out));
            fbVels[idx] = out * 0.99f;
        }
    }

    // All 4 instances should produce output
    for (int i = 0; i < 4; ++i) {
        INFO("Instance " << i << " velocity=" << velocities[static_cast<size_t>(i)]
                         << " maxAbs=" << maxAbs[static_cast<size_t>(i)]);
        REQUIRE(maxAbs[static_cast<size_t>(i)] > 0.0f);
    }

    // Higher velocity should produce higher output (monotonic relationship)
    // At minimum, the lowest velocity instance should differ from the highest
    REQUIRE(maxAbs[0] != Approx(maxAbs[3]).margin(1e-6f));
}

// =============================================================================
// T109b: SC-010 Pitch-range loudness consistency (65 Hz, 220 Hz, 880 Hz)
// =============================================================================

TEST_CASE("BowExciter + WaveguideString loudness consistency across pitch range (SC-010)",
          "[processors][bow_exciter][waveguide][coupling]")
{
    // Run at 3 pitches with identical parameters
    constexpr int kDuration = 88200; // 2 seconds
    constexpr size_t kSteadyStateStart = 22050; // start measuring at 500ms

    // Measure RMS at steady state for each pitch
    auto output65  = runBowWaveguideFeedbackLoop(65.0f,  kDuration);
    auto output220 = runBowWaveguideFeedbackLoop(220.0f, kDuration);
    auto output880 = runBowWaveguideFeedbackLoop(880.0f, kDuration);

    float rms65  = computeRMS(output65,  kSteadyStateStart, output65.size());
    float rms220 = computeRMS(output220, kSteadyStateStart, output220.size());
    float rms880 = computeRMS(output880, kSteadyStateStart, output880.size());

    INFO("RMS at 65 Hz:  " << rms65);
    INFO("RMS at 220 Hz: " << rms220);
    INFO("RMS at 880 Hz: " << rms880);

    // All should be non-zero (sustained)
    REQUIRE(rms65  > 1e-6f);
    REQUIRE(rms220 > 1e-6f);
    REQUIRE(rms880 > 1e-6f);

    // SC-010: within +/-6 dB of each other
    // 6 dB = factor of ~2.0 (for amplitude) or ~4.0 for power
    // In dB: |20*log10(rmsA/rmsB)| < 6
    auto dBDiff = [](float a, float b) -> float {
        return 20.0f * std::log10(a / b);
    };

    float diff_65_220  = dBDiff(rms65, rms220);
    float diff_220_880 = dBDiff(rms220, rms880);
    float diff_65_880  = dBDiff(rms65, rms880);

    INFO("dB difference 65 vs 220:  " << diff_65_220);
    INFO("dB difference 220 vs 880: " << diff_220_880);
    INFO("dB difference 65 vs 880:  " << diff_65_880);

    REQUIRE(std::abs(diff_65_220)  < 6.0f);
    REQUIRE(std::abs(diff_220_880) < 6.0f);
    REQUIRE(std::abs(diff_65_880)  < 6.0f);
}
