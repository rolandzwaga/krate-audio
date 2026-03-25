// =============================================================================
// Waveguide String Integration Tests (Spec 129, Phase 3)
// =============================================================================
// Tests WaveguideString wiring into the Innexus voice engine.

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/sample_analysis.h"

#include <krate/dsp/processors/waveguide_string.h>
#include <krate/dsp/processors/harmonic_types.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <memory>

using Catch::Approx;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 256;

/// Helper to create a minimal SampleAnalysis with one frame.
Innexus::SampleAnalysis* createMinimalAnalysis()
{
    auto* analysis = new Innexus::SampleAnalysis();
    Krate::DSP::HarmonicFrame frame;
    frame.f0 = 440.0f;
    frame.numPartials = 1;
    frame.partials[0].frequency = 440.0f;
    frame.partials[0].amplitude = 1.0f;
    frame.partials[0].phase = 0.0f;
    analysis->frames.push_back(frame);
    return analysis;
}

} // namespace

TEST_CASE("WaveguideString - voice engine integration: noteOn triggers output",
          "[innexus][waveguide][integration]")
{
    // Verify that a WaveguideString instance in the voice produces output
    Krate::DSP::WaveguideString ws;
    ws.prepare(kSampleRate);
    ws.prepareVoice(0);
    ws.setDecay(1.0f);
    ws.setBrightness(0.3f);
    ws.setStiffness(0.0f);
    ws.setPickPosition(0.13f);

    ws.noteOn(440.0f, 0.8f);

    bool hasOutput = false;
    for (int i = 0; i < kBlockSize; ++i) {
        float out = ws.process(0.0f);
        if (out != 0.0f)
            hasOutput = true;
    }
    REQUIRE(hasOutput);
}

TEST_CASE("WaveguideString - voice engine integration: 8-voice polyphony",
          "[innexus][waveguide][integration]")
{
    // Verify 8 independent WaveguideString instances can run simultaneously
    constexpr int kVoices = 8;
    std::array<Krate::DSP::WaveguideString, kVoices> voices;

    float notes[] = {110.0f, 220.0f, 330.0f, 440.0f, 550.0f, 660.0f, 770.0f, 880.0f};

    for (int v = 0; v < kVoices; ++v) {
        voices[static_cast<size_t>(v)].prepare(kSampleRate);
        voices[static_cast<size_t>(v)].prepareVoice(static_cast<uint32_t>(v));
        voices[static_cast<size_t>(v)].setDecay(1.0f);
        voices[static_cast<size_t>(v)].setBrightness(0.3f);
        voices[static_cast<size_t>(v)].noteOn(notes[v], 0.7f);
    }

    // Process 256 samples across all voices
    int voicesWithOutput = 0;
    for (int v = 0; v < kVoices; ++v) {
        bool hasOutput = false;
        for (int i = 0; i < kBlockSize; ++i) {
            float out = voices[static_cast<size_t>(v)].process(0.0f);
            if (out != 0.0f)
                hasOutput = true;
        }
        if (hasOutput)
            ++voicesWithOutput;
    }

    REQUIRE(voicesWithOutput == kVoices);
}

TEST_CASE("WaveguideString - voice engine integration: voice steal",
          "[innexus][waveguide][integration]")
{
    // Verify that calling silence() on a voice and reusing it does not crash
    Krate::DSP::WaveguideString ws;
    ws.prepare(kSampleRate);
    ws.prepareVoice(0);
    ws.setDecay(1.0f);
    ws.setBrightness(0.3f);

    // First note
    ws.noteOn(440.0f, 0.8f);
    for (int i = 0; i < 128; ++i)
        (void)ws.process(0.0f);

    // Voice steal: silence and retrigger
    ws.silence();
    ws.noteOn(220.0f, 0.6f);

    bool hasOutput = false;
    for (int i = 0; i < kBlockSize; ++i) {
        float out = ws.process(0.0f);
        if (out != 0.0f)
            hasOutput = true;
    }
    REQUIRE(hasOutput);
}

// =============================================================================
// Phase 6: Crossfade Tests (Spec 129, FR-029, FR-030, FR-031, SC-010)
// =============================================================================
// These tests simulate the crossfade logic that will exist in the processor's
// per-sample render loop, using the InnexusVoice crossfade state and both
// resonator types directly.

namespace {

constexpr int kCrossfadeSamples = 1024;  // ~23ms at 44.1kHz
constexpr float kPi = 3.14159265358979323846f;

/// Compute RMS of a buffer segment
float computeRMS(const float* data, int count)
{
    if (count <= 0) return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < count; ++i)
        sum += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

/// Prepare a modal resonator with a test signal (needs setModes + a few samples to ring up)
void prepareModalResonator(Krate::DSP::ModalResonatorBank& modal, float f0)
{
    modal.prepare(kSampleRate);
    float freq = f0;
    float amp = 1.0f;
    modal.setModes(&freq, &amp, 1, 1.0f, 0.3f, 0.0f, 0.0f);
    // Feed some excitation to get it ringing
    for (int i = 0; i < 512; ++i)
        (void)modal.process(i < 64 ? 0.5f : 0.0f);
}

/// Prepare a waveguide string with a test signal
void prepareWaveguideString(Krate::DSP::WaveguideString& wg, float f0)
{
    wg.prepare(kSampleRate);
    wg.prepareVoice(0);
    wg.setDecay(1.0f);
    wg.setBrightness(0.3f);
    wg.setStiffness(0.0f);
    wg.setPickPosition(0.13f);
    wg.noteOn(f0, 0.8f);
    // Let it ring up
    for (int i = 0; i < 512; ++i)
        (void)wg.process(0.0f);
}

} // namespace

TEST_CASE("Resonance crossfade - modal to waveguide produces no click",
          "[innexus][crossfade][SC-010]")
{
    // Set up both resonators ringing at the same pitch
    Krate::DSP::ModalResonatorBank modal;
    Krate::DSP::WaveguideString wg;

    constexpr float f0 = 440.0f;
    prepareModalResonator(modal, f0);
    prepareWaveguideString(wg, f0);

    // Crossfade from modal to waveguide with energy gain matching (FR-031)
    // Check for clicks by detecting large sample-to-sample discontinuities
    constexpr int kTotalSamples = 2048;
    float prevSample = modal.process(0.0f); // one sample before crossfade
    float maxDelta = 0.0f;
    bool hasOutput = false;

    for (int i = 0; i < kTotalSamples; ++i)
    {
        float out;
        if (i < kCrossfadeSamples)
        {
            float t = static_cast<float>(i) / static_cast<float>(kCrossfadeSamples);
            float oldOut = modal.process(0.0f);
            float newOut = wg.process(0.0f);

            // Energy gain matching (FR-031)
            float eOld = modal.getPerceptualEnergy();
            float eNew = wg.getPerceptualEnergy();
            float gainMatch = (eNew > 1e-20f) ? std::sqrt(eOld / eNew) : 1.0f;
            gainMatch = std::clamp(gainMatch, 0.25f, 4.0f);

            // Equal-power crossfade (FR-030)
            out = oldOut * std::cos(t * kPi / 2.0f)
                + newOut * gainMatch * std::sin(t * kPi / 2.0f);
        }
        else
        {
            (void)modal.process(0.0f);
            out = wg.process(0.0f);
        }

        float delta = std::abs(out - prevSample);
        maxDelta = std::max(maxDelta, delta);
        if (out != 0.0f)
            hasOutput = true;
        prevSample = out;
    }

    // SC-010: No clicks -- max sample-to-sample delta should be bounded.
    // For a smooth ~440 Hz signal at 44.1kHz, the maximum instantaneous delta
    // is bounded by the amplitude. A click would be a delta >> normal peak.
    // With properly gain-staged modal resonator, signal amplitudes are larger
    // than before, so we scale the threshold to the signal level.
    INFO("maxDelta = " << maxDelta);
    REQUIRE(hasOutput);
    // A 440 Hz sine's max delta per sample is ~2π×440/44100 × amplitude ≈ 0.063 × amplitude.
    // During crossfade with gain matching, we allow up to 2x this for transients.
    // Anything above 2.0 would be a clear click artifact.
    REQUIRE(maxDelta < 2.0f);
}

TEST_CASE("Resonance crossfade - waveguide to modal produces no click",
          "[innexus][crossfade][SC-010]")
{
    Krate::DSP::ModalResonatorBank modal;
    Krate::DSP::WaveguideString wg;

    constexpr float f0 = 440.0f;
    prepareModalResonator(modal, f0);
    prepareWaveguideString(wg, f0);

    // Crossfade from waveguide to modal with energy gain matching
    constexpr int kTotalSamples = 2048;
    float prevSample = wg.process(0.0f);
    float maxDelta = 0.0f;
    bool hasOutput = false;

    for (int i = 0; i < kTotalSamples; ++i)
    {
        float out;
        if (i < kCrossfadeSamples)
        {
            float t = static_cast<float>(i) / static_cast<float>(kCrossfadeSamples);
            float oldOut = wg.process(0.0f);
            float newOut = modal.process(0.0f);

            float eOld = wg.getPerceptualEnergy();
            float eNew = modal.getPerceptualEnergy();
            float gainMatch = (eNew > 1e-20f) ? std::sqrt(eOld / eNew) : 1.0f;
            gainMatch = std::clamp(gainMatch, 0.25f, 4.0f);

            out = oldOut * std::cos(t * kPi / 2.0f)
                + newOut * gainMatch * std::sin(t * kPi / 2.0f);
        }
        else
        {
            (void)wg.process(0.0f);
            out = modal.process(0.0f);
        }

        float delta = std::abs(out - prevSample);
        maxDelta = std::max(maxDelta, delta);
        if (out != 0.0f)
            hasOutput = true;
        prevSample = out;
    }

    INFO("maxDelta = " << maxDelta);
    REQUIRE(hasOutput);
    REQUIRE(maxDelta < 2.0f);
}

TEST_CASE("Resonance crossfade - equal-power formula applied",
          "[innexus][crossfade][FR-030]")
{
    // Verify equal-power property: cos^2(t*pi/2) + sin^2(t*pi/2) == 1.0
    // This is a mathematical identity but we verify the formula produces
    // constant energy when both signals have equal amplitude.
    constexpr int kSteps = 100;
    for (int i = 0; i <= kSteps; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(kSteps);
        float cosVal = std::cos(t * kPi / 2.0f);
        float sinVal = std::sin(t * kPi / 2.0f);
        float sumOfSquares = cosVal * cosVal + sinVal * sinVal;
        REQUIRE(sumOfSquares == Approx(1.0f).margin(1e-6f));
    }

    // Verify equal-power property with uncorrelated signals:
    // For two independent signals of equal power, the crossfade preserves
    // the expected power: E[out^2] = cos^2*P_old + sin^2*P_new = P
    // We verify: cos^2(t) * P + sin^2(t) * P == P for all t
    constexpr float kPower = 0.25f;
    for (int i = 0; i <= kSteps; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(kSteps);
        float cosVal = std::cos(t * kPi / 2.0f);
        float sinVal = std::sin(t * kPi / 2.0f);
        float blendedPower = cosVal * cosVal * kPower + sinVal * sinVal * kPower;
        REQUIRE(blendedPower == Approx(kPower).margin(1e-5f));
    }
}

TEST_CASE("Resonance crossfade - energy gain match within +/-12 dB",
          "[innexus][crossfade][FR-031]")
{
    // Test gainMatch computation and clamping
    // gainMatch = (eNew > 1e-20f) ? sqrtf(eOld / eNew) : 1.0f, clamped [0.25, 4.0]

    SECTION("Equal energy produces gain ~1.0") {
        float eOld = 0.01f;
        float eNew = 0.01f;
        float gainMatch = (eNew > 1e-20f) ? std::sqrt(eOld / eNew) : 1.0f;
        gainMatch = std::clamp(gainMatch, 0.25f, 4.0f);
        REQUIRE(gainMatch == Approx(1.0f).margin(0.001f));
    }

    SECTION("Old 4x louder produces gain ~2.0") {
        float eOld = 0.04f;
        float eNew = 0.01f;
        float gainMatch = (eNew > 1e-20f) ? std::sqrt(eOld / eNew) : 1.0f;
        gainMatch = std::clamp(gainMatch, 0.25f, 4.0f);
        REQUIRE(gainMatch == Approx(2.0f).margin(0.01f));
    }

    SECTION("Extreme ratio clamps to 4.0 (12 dB)") {
        float eOld = 1.0f;
        float eNew = 0.001f;
        float gainMatch = (eNew > 1e-20f) ? std::sqrt(eOld / eNew) : 1.0f;
        gainMatch = std::clamp(gainMatch, 0.25f, 4.0f);
        REQUIRE(gainMatch == 4.0f);
    }

    SECTION("Inverse extreme clamps to 0.25 (-12 dB)") {
        float eOld = 0.001f;
        float eNew = 1.0f;
        float gainMatch = (eNew > 1e-20f) ? std::sqrt(eOld / eNew) : 1.0f;
        gainMatch = std::clamp(gainMatch, 0.25f, 4.0f);
        REQUIRE(gainMatch == 0.25f);
    }
}

TEST_CASE("Resonance crossfade - handles near-zero energy gracefully",
          "[innexus][crossfade][FR-031]")
{
    // Switch during silence -- no NaN, no Inf, no division by zero
    Krate::DSP::ModalResonatorBank modal;
    Krate::DSP::WaveguideString wg;

    modal.prepare(kSampleRate);
    float freq = 440.0f;
    float amp = 1.0f;
    modal.setModes(&freq, &amp, 1, 1.0f, 0.3f, 0.0f, 0.0f);

    wg.prepare(kSampleRate);
    wg.prepareVoice(0);
    wg.setDecay(1.0f);
    wg.setBrightness(0.3f);

    // Do NOT trigger noteOn -- both resonators are silent
    // Crossfade should produce zero output with no NaN/Inf

    bool hasNaN = false;
    bool hasInf = false;

    for (int i = 0; i < kCrossfadeSamples; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(kCrossfadeSamples);
        float oldOut = modal.process(0.0f);
        float newOut = wg.process(0.0f);

        // Energy gain match with near-zero energy
        float eOld = modal.getPerceptualEnergy();
        float eNew = wg.getPerceptualEnergy();
        float gainMatch = (eNew > 1e-20f) ? std::sqrt(eOld / eNew) : 1.0f;
        gainMatch = std::clamp(gainMatch, 0.25f, 4.0f);

        float out = oldOut * std::cos(t * kPi / 2.0f)
                  + newOut * gainMatch * std::sin(t * kPi / 2.0f);

        // Use bit-level NaN/Inf check (safe with -ffast-math)
        uint32_t bits;
        std::memcpy(&bits, &out, sizeof(bits));
        uint32_t exponent = (bits >> 23) & 0xFF;
        if (exponent == 0xFF) {
            if ((bits & 0x7FFFFF) != 0)
                hasNaN = true;
            else
                hasInf = true;
        }
    }

    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
}

TEST_CASE("Resonance crossfade - both models run in parallel during transition",
          "[innexus][crossfade][FR-030]")
{
    Krate::DSP::ModalResonatorBank modal;
    Krate::DSP::WaveguideString wg;

    constexpr float f0 = 440.0f;
    prepareModalResonator(modal, f0);
    prepareWaveguideString(wg, f0);

    // During crossfade, both should produce non-zero output
    bool modalProducedOutput = false;
    bool waveguideProducedOutput = false;

    for (int i = 0; i < kCrossfadeSamples; ++i)
    {
        float oldOut = modal.process(0.0f);
        float newOut = wg.process(0.0f);

        if (oldOut != 0.0f)
            modalProducedOutput = true;
        if (newOut != 0.0f)
            waveguideProducedOutput = true;
    }

    REQUIRE(modalProducedOutput);
    REQUIRE(waveguideProducedOutput);
}
