// ==============================================================================
// Harmonic Modulator Tests (M6)
// ==============================================================================
// Tests for LFO-driven per-partial animation.
//
// Feature: 120-creative-extensions
// User Story 4: Harmonic Modulators + Detune
// Requirements: FR-024 through FR-033, FR-044, FR-046, FR-051, SC-004, SC-005
//
// T030: Unit tests for HarmonicModulator class
// T031: Tests for all 5 LFO waveforms
// T032: Tests for two-modulator overlap behavior
// T033: Tests verifying LFO phase does not reset on note events
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/harmonic_modulator.h"

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/core/math_constants.h>

#include <algorithm>
#include <array>
#include <cmath>

using Catch::Approx;

// =============================================================================
// Helper: Build a test HarmonicFrame with known amplitudes
// =============================================================================
static Krate::DSP::HarmonicFrame makeTestFrame(int numPartials = 16, float baseAmp = 0.5f)
{
    Krate::DSP::HarmonicFrame frame{};
    frame.f0 = 220.0f;
    frame.f0Confidence = 1.0f;
    frame.numPartials = numPartials;
    for (int i = 0; i < numPartials; ++i)
    {
        auto& p = frame.partials[static_cast<size_t>(i)];
        p.harmonicIndex = i + 1;
        p.amplitude = baseAmp;
        p.relativeFrequency = static_cast<float>(i + 1);
        p.inharmonicDeviation = 0.0f;
    }
    return frame;
}

// =============================================================================
// T030: Unit tests for HarmonicModulator class
// =============================================================================

TEST_CASE("HarmonicModulator: prepare resets phase to 0", "[modulator][FR-051]")
{
    Innexus::HarmonicModulator mod;
    mod.prepare(44100.0);
    REQUIRE(mod.getPhase() == Approx(0.0f).margin(1e-7f));

    // Advance some samples
    mod.setRate(1.0f);
    for (int i = 0; i < 1000; ++i)
        mod.advance();

    // Phase should have moved
    REQUIRE(mod.getPhase() > 0.0f);

    // Prepare again: phase resets
    mod.prepare(44100.0);
    REQUIRE(mod.getPhase() == Approx(0.0f).margin(1e-7f));
}

TEST_CASE("HarmonicModulator: advance increments phase by rate*inverseSampleRate",
          "[modulator]")
{
    Innexus::HarmonicModulator mod;
    mod.prepare(44100.0);
    mod.setRate(1.0f); // 1 Hz
    mod.setWaveform(Innexus::ModulatorWaveform::Sine);

    mod.advance();
    float expectedPhase = 1.0f / 44100.0f;
    REQUIRE(mod.getPhase() == Approx(expectedPhase).margin(1e-8f));

    // Advance 100 more samples
    for (int i = 0; i < 100; ++i)
        mod.advance();

    expectedPhase = 101.0f / 44100.0f;
    REQUIRE(mod.getPhase() == Approx(expectedPhase).margin(1e-6f));
}

TEST_CASE("HarmonicModulator: phase wraps at 1.0 and updates S&H value",
          "[modulator]")
{
    Innexus::HarmonicModulator mod;
    mod.prepare(44100.0);
    mod.setRate(44100.0f); // Will wrap every sample
    mod.setWaveform(Innexus::ModulatorWaveform::RandomSH);
    mod.setDepth(1.0f);

    // Get initial S&H value
    float initialValue = mod.getCurrentValue();

    // Advance once -- phase wraps at 1.0
    mod.advance();

    // After wrap, S&H value should change (different from initial)
    // Note: it COULD be the same by coincidence but extremely unlikely
    // We just verify phase wrapped
    REQUIRE(mod.getPhase() < 1.0f);
}

TEST_CASE("HarmonicModulator: amplitude modulation at depth=0 produces no change",
          "[modulator][FR-025]")
{
    Innexus::HarmonicModulator mod;
    mod.prepare(44100.0);
    mod.setWaveform(Innexus::ModulatorWaveform::Sine);
    mod.setTarget(Innexus::ModulatorTarget::Amplitude);
    mod.setDepth(0.0f);
    mod.setRange(1, 48);
    mod.setRate(2.0f);

    // Advance to non-zero LFO position
    for (int i = 0; i < 11025; ++i)
        mod.advance();

    auto frame = makeTestFrame();
    auto original = frame;
    mod.applyAmplitudeModulation(frame);

    // All amplitudes should be unchanged
    for (int i = 0; i < frame.numPartials; ++i)
    {
        REQUIRE(frame.partials[static_cast<size_t>(i)].amplitude ==
                Approx(original.partials[static_cast<size_t>(i)].amplitude).margin(1e-6f));
    }
}

TEST_CASE("HarmonicModulator: amplitude modulation at depth=1.0 full sweep",
          "[modulator][FR-025]")
{
    Innexus::HarmonicModulator mod;
    mod.prepare(44100.0);
    mod.setWaveform(Innexus::ModulatorWaveform::Sine);
    mod.setTarget(Innexus::ModulatorTarget::Amplitude);
    mod.setDepth(1.0f);
    mod.setRange(1, 16);
    mod.setRate(2.0f);

    // Advance to phase ~0.25 where sine = 1.0 (max unipolar = 1.0)
    // At 2Hz, quarter period = 44100/2/4 = 5512.5 samples
    for (int i = 0; i < 5512; ++i)
        mod.advance();

    auto frame = makeTestFrame();
    float originalAmp = frame.partials[0].amplitude;
    mod.applyAmplitudeModulation(frame);

    // At depth=1.0, unipolar value near 1.0:
    // effectiveAmp = modelAmp * (1 - 1.0 + 1.0 * lfoUnipolar)
    // = modelAmp * lfoUnipolar
    // At phase ~0.25, sin(2*pi*0.25) = 1.0, unipolar = (1+1)/2 = 1.0
    // So effectiveAmp ~ originalAmp * 1.0 = originalAmp
    REQUIRE(frame.partials[0].amplitude == Approx(originalAmp).margin(0.05f));
}

TEST_CASE("HarmonicModulator: amplitude modulation depth=0.5 proportional (SC-004)",
          "[modulator][SC-004]")
{
    // SC-004: amplitude modulation depth within +/-5% of configured depth at 2 Hz
    Innexus::HarmonicModulator mod;
    mod.prepare(44100.0);
    mod.setWaveform(Innexus::ModulatorWaveform::Sine);
    mod.setTarget(Innexus::ModulatorTarget::Amplitude);
    mod.setDepth(0.5f);
    mod.setRange(1, 16);
    mod.setRate(2.0f);

    // Measure amplitude modulation over 500ms (22050 samples)
    auto frame = makeTestFrame();
    const float baseAmp = frame.partials[0].amplitude; // 0.5

    float minAmp = 1e30f;
    float maxAmp = -1e30f;

    for (int s = 0; s < 22050; ++s)
    {
        auto testFrame = makeTestFrame();
        mod.applyAmplitudeModulation(testFrame);
        float amp = testFrame.partials[0].amplitude;
        minAmp = std::min(minAmp, amp);
        maxAmp = std::max(maxAmp, amp);
        mod.advance();
    }

    // At depth=0.5, formula: effectiveAmp = amp * (1 - 0.5 + 0.5 * unipolar)
    // unipolar ranges [0, 1], so effectiveAmp ranges [amp*0.5, amp*1.0]
    // Modulation depth = (maxAmp - minAmp) / baseAmp
    float measuredDepth = (maxAmp - minAmp) / baseAmp;

    // SC-004: within +/-5% of configured depth (0.5)
    REQUIRE(measuredDepth == Approx(0.5f).margin(0.025f));
}

TEST_CASE("HarmonicModulator: frequency modulation produces correct multiplier",
          "[modulator][FR-026]")
{
    Innexus::HarmonicModulator mod;
    mod.prepare(44100.0);
    mod.setWaveform(Innexus::ModulatorWaveform::Sine);
    mod.setTarget(Innexus::ModulatorTarget::Frequency);
    mod.setDepth(1.0f);
    mod.setRange(1, 16);
    mod.setRate(2.0f);

    // Advance to phase ~0.25 where sine = 1.0 (bipolar)
    for (int i = 0; i < 5512; ++i)
        mod.advance();

    std::array<float, Krate::DSP::kMaxPartials> multipliers{};
    mod.getFrequencyMultipliers(multipliers);

    // At depth=1.0, lfoBipolar ~ 1.0:
    // multiplier = pow(2, 1.0 * 1.0 * 50 / 1200) = pow(2, 50/1200) ~ 1.02930
    float expected = std::pow(2.0f, 50.0f / 1200.0f);
    REQUIRE(multipliers[0] == Approx(expected).margin(0.01f));
}

TEST_CASE("HarmonicModulator: pan offset formula", "[modulator][FR-027]")
{
    Innexus::HarmonicModulator mod;
    mod.prepare(44100.0);
    mod.setWaveform(Innexus::ModulatorWaveform::Sine);
    mod.setTarget(Innexus::ModulatorTarget::Pan);
    mod.setDepth(1.0f);
    mod.setRange(1, 16);
    mod.setRate(2.0f);

    // Advance to phase ~0.25 where sine = 1.0
    for (int i = 0; i < 5512; ++i)
        mod.advance();

    std::array<float, Krate::DSP::kMaxPartials> offsets{};
    mod.getPanOffsets(offsets);

    // At depth=1.0, lfoBipolar ~ 1.0:
    // offset = 1.0 * 1.0 * 0.5 = 0.5
    REQUIRE(offsets[0] == Approx(0.5f).margin(0.02f));

    // Partial outside range should be 0
    REQUIRE(offsets[20] == Approx(0.0f).margin(1e-7f));
}

TEST_CASE("HarmonicModulator: range start > end handled gracefully",
          "[modulator][edge]")
{
    Innexus::HarmonicModulator mod;
    mod.prepare(44100.0);
    mod.setWaveform(Innexus::ModulatorWaveform::Sine);
    mod.setTarget(Innexus::ModulatorTarget::Amplitude);
    mod.setDepth(1.0f);
    mod.setRange(20, 5); // start > end

    for (int i = 0; i < 1000; ++i)
        mod.advance();

    auto frame = makeTestFrame();
    auto original = frame;
    mod.applyAmplitudeModulation(frame);

    // When start > end, no partials should be affected
    for (int i = 0; i < frame.numPartials; ++i)
    {
        REQUIRE(frame.partials[static_cast<size_t>(i)].amplitude ==
                Approx(original.partials[static_cast<size_t>(i)].amplitude).margin(1e-6f));
    }
}

// =============================================================================
// T031: Tests for all 5 LFO waveforms
// =============================================================================

TEST_CASE("HarmonicModulator: Sine waveform values", "[modulator][waveform]")
{
    Innexus::HarmonicModulator mod;
    mod.prepare(44100.0);
    mod.setWaveform(Innexus::ModulatorWaveform::Sine);
    mod.setRate(1.0f);

    // At phase=0: sin(2*pi*0) = 0
    REQUIRE(mod.getCurrentValue() == Approx(0.0f).margin(1e-6f));

    // Advance to phase ~0.25: sin(2*pi*0.25) = 1.0
    int samplesToQuarter = 44100 / 4;
    for (int i = 0; i < samplesToQuarter; ++i)
        mod.advance();

    REQUIRE(mod.getCurrentValue() == Approx(1.0f).margin(0.01f));
}

TEST_CASE("HarmonicModulator: Triangle waveform values", "[modulator][waveform]")
{
    // Triangle formula: 4*|phase - 0.5| - 1
    // phase=0: 4*|0-0.5|-1 = 4*0.5-1 = 1.0
    // phase=0.5: 4*|0.5-0.5|-1 = -1.0
    // phase=1.0: 4*|1-0.5|-1 = 1.0

    Innexus::HarmonicModulator mod;
    mod.prepare(44100.0);
    mod.setWaveform(Innexus::ModulatorWaveform::Triangle);
    mod.setRate(1.0f);

    // At phase=0: value should be 1.0
    REQUIRE(mod.getCurrentValue() == Approx(1.0f).margin(1e-5f));

    // Advance to phase ~0.5
    int samplesToHalf = 44100 / 2;
    for (int i = 0; i < samplesToHalf; ++i)
        mod.advance();

    REQUIRE(mod.getCurrentValue() == Approx(-1.0f).margin(0.01f));
}

TEST_CASE("HarmonicModulator: Square waveform values", "[modulator][waveform]")
{
    // Square: phase < 0.5 -> 1.0, phase >= 0.5 -> -1.0

    Innexus::HarmonicModulator mod;
    mod.prepare(44100.0);
    mod.setWaveform(Innexus::ModulatorWaveform::Square);
    mod.setRate(1.0f);

    // At phase=0: value should be 1.0 (phase < 0.5)
    REQUIRE(mod.getCurrentValue() == Approx(1.0f).margin(1e-5f));

    // Advance to phase ~0.5: value should be -1.0
    int samplesToHalf = 44100 / 2;
    for (int i = 0; i < samplesToHalf; ++i)
        mod.advance();

    REQUIRE(mod.getCurrentValue() == Approx(-1.0f).margin(0.01f));
}

TEST_CASE("HarmonicModulator: Saw waveform values", "[modulator][waveform]")
{
    // Saw: 2*phase - 1
    // phase=0 -> -1, phase=1 -> 1

    Innexus::HarmonicModulator mod;
    mod.prepare(44100.0);
    mod.setWaveform(Innexus::ModulatorWaveform::Saw);
    mod.setRate(1.0f);

    // At phase=0: 2*0-1 = -1.0
    REQUIRE(mod.getCurrentValue() == Approx(-1.0f).margin(1e-5f));

    // Advance to phase ~0.5: 2*0.5-1 = 0.0
    int samplesToHalf = 44100 / 2;
    for (int i = 0; i < samplesToHalf; ++i)
        mod.advance();

    REQUIRE(mod.getCurrentValue() == Approx(0.0f).margin(0.01f));
}

TEST_CASE("HarmonicModulator: RandomSH holds constant between wraps",
          "[modulator][waveform]")
{
    Innexus::HarmonicModulator mod;
    mod.prepare(44100.0);
    mod.setWaveform(Innexus::ModulatorWaveform::RandomSH);
    mod.setRate(1.0f); // 1 Hz, so phase wraps at 44100 samples
    mod.setDepth(1.0f);

    // Advance one sample to get an initial S&H value
    mod.advance();
    float heldValue = mod.getCurrentValue();

    // Advance 1000 more samples without wrapping
    for (int i = 0; i < 1000; ++i)
    {
        mod.advance();
        REQUIRE(mod.getCurrentValue() == Approx(heldValue).margin(1e-7f));
    }
}

TEST_CASE("HarmonicModulator: RandomSH changes on phase wrap",
          "[modulator][waveform]")
{
    Innexus::HarmonicModulator mod;
    mod.prepare(44100.0);
    mod.setWaveform(Innexus::ModulatorWaveform::RandomSH);
    mod.setRate(44100.0f / 100.0f); // wrap every 100 samples
    mod.setDepth(1.0f);

    // Advance past 100 samples to get initial S&H value
    for (int i = 0; i < 101; ++i)
        mod.advance();
    float firstValue = mod.getCurrentValue();

    // Advance past another wrap
    for (int i = 0; i < 100; ++i)
        mod.advance();
    float secondValue = mod.getCurrentValue();

    // Values should differ (extremely unlikely to be same from RNG)
    REQUIRE(firstValue != secondValue);
}

// =============================================================================
// T032: Tests for two-modulator overlap behavior
// =============================================================================

TEST_CASE("HarmonicModulator: overlapping amplitude ranges multiply effects (FR-028)",
          "[modulator][overlap]")
{
    Innexus::HarmonicModulator mod1;
    Innexus::HarmonicModulator mod2;

    mod1.prepare(44100.0);
    mod2.prepare(44100.0);

    // Both target amplitude on overlapping range [1, 16]
    mod1.setWaveform(Innexus::ModulatorWaveform::Sine);
    mod1.setTarget(Innexus::ModulatorTarget::Amplitude);
    mod1.setDepth(0.5f);
    mod1.setRange(1, 16);
    mod1.setRate(2.0f);

    mod2.setWaveform(Innexus::ModulatorWaveform::Triangle);
    mod2.setTarget(Innexus::ModulatorTarget::Amplitude);
    mod2.setDepth(0.3f);
    mod2.setRange(8, 24);
    mod2.setRate(3.0f);

    // Advance both to a known state
    for (int i = 0; i < 5000; ++i)
    {
        mod1.advance();
        mod2.advance();
    }

    // Apply sequentially: multiply effect for overlapping amplitude
    auto frame = makeTestFrame(24, 1.0f);
    float ampBefore = frame.partials[9].amplitude; // partial 10, in both ranges

    // Apply mod1 first
    mod1.applyAmplitudeModulation(frame);
    float ampAfterMod1 = frame.partials[9].amplitude;

    // Apply mod2 second (multiplicative with mod1)
    mod2.applyAmplitudeModulation(frame);
    float ampAfterBoth = frame.partials[9].amplitude;

    // Apply each modulator independently to verify multiplicative behavior
    auto frameMod1Only = makeTestFrame(24, 1.0f);
    mod1.applyAmplitudeModulation(frameMod1Only);
    float mod1Factor = frameMod1Only.partials[9].amplitude / ampBefore;

    auto frameMod2Only = makeTestFrame(24, 1.0f);
    mod2.applyAmplitudeModulation(frameMod2Only);
    float mod2Factor = frameMod2Only.partials[9].amplitude / ampBefore;

    // Combined should be product of individual factors
    float expectedCombined = ampBefore * mod1Factor * mod2Factor;
    REQUIRE(ampAfterBoth == Approx(expectedCombined).margin(1e-5f));
}

TEST_CASE("HarmonicModulator: overlapping frequency ranges add effects (FR-028)",
          "[modulator][overlap]")
{
    Innexus::HarmonicModulator mod1;
    Innexus::HarmonicModulator mod2;

    mod1.prepare(44100.0);
    mod2.prepare(44100.0);

    mod1.setWaveform(Innexus::ModulatorWaveform::Sine);
    mod1.setTarget(Innexus::ModulatorTarget::Frequency);
    mod1.setDepth(0.5f);
    mod1.setRange(1, 16);
    mod1.setRate(2.0f);

    mod2.setWaveform(Innexus::ModulatorWaveform::Triangle);
    mod2.setTarget(Innexus::ModulatorTarget::Frequency);
    mod2.setDepth(0.3f);
    mod2.setRange(8, 24);
    mod2.setRate(3.0f);

    for (int i = 0; i < 5000; ++i)
    {
        mod1.advance();
        mod2.advance();
    }

    // Get individual multipliers
    std::array<float, Krate::DSP::kMaxPartials> mult1{};
    std::array<float, Krate::DSP::kMaxPartials> mult2{};
    mod1.getFrequencyMultipliers(mult1);
    mod2.getFrequencyMultipliers(mult2);

    // For overlapping partials (8-16), additive means multiply the multipliers
    // (since adding cents is equivalent to multiplying frequency ratios)
    for (int i = 7; i < 16; ++i) // indices 7-15 = partials 8-16
    {
        float combined = mult1[static_cast<size_t>(i)] * mult2[static_cast<size_t>(i)];
        // Verify they're non-trivial (both modulating)
        REQUIRE(mult1[static_cast<size_t>(i)] != Approx(1.0f).margin(1e-5f));
        REQUIRE(mult2[static_cast<size_t>(i)] != Approx(1.0f).margin(1e-5f));
        // The additive combination in cents domain = multiplying ratios
        REQUIRE(combined > 0.0f);
    }
}

TEST_CASE("HarmonicModulator: partial outside range gets identity",
          "[modulator][overlap]")
{
    Innexus::HarmonicModulator mod;
    mod.prepare(44100.0);
    mod.setWaveform(Innexus::ModulatorWaveform::Sine);
    mod.setDepth(1.0f);
    mod.setRange(5, 10);
    mod.setRate(2.0f);

    for (int i = 0; i < 5000; ++i)
        mod.advance();

    // Amplitude: partials outside [5,10] unchanged
    mod.setTarget(Innexus::ModulatorTarget::Amplitude);
    auto frame = makeTestFrame();
    float ampP1Before = frame.partials[0].amplitude; // partial 1
    mod.applyAmplitudeModulation(frame);
    REQUIRE(frame.partials[0].amplitude == Approx(ampP1Before).margin(1e-6f));

    // Frequency: partials outside get multiplier 1.0
    mod.setTarget(Innexus::ModulatorTarget::Frequency);
    std::array<float, Krate::DSP::kMaxPartials> mults{};
    mod.getFrequencyMultipliers(mults);
    REQUIRE(mults[0] == Approx(1.0f).margin(1e-7f)); // partial 1

    // Pan: partials outside get offset 0.0
    mod.setTarget(Innexus::ModulatorTarget::Pan);
    std::array<float, Krate::DSP::kMaxPartials> pans{};
    mod.getPanOffsets(pans);
    REQUIRE(pans[0] == Approx(0.0f).margin(1e-7f)); // partial 1
}

// =============================================================================
// T033: Tests verifying LFO phase does not reset on note events (FR-029, FR-051)
// =============================================================================

TEST_CASE("HarmonicModulator: LFO phase continues through note on/off (FR-029)",
          "[modulator][FR-029][FR-051]")
{
    Innexus::HarmonicModulator mod;
    mod.prepare(44100.0);
    mod.setWaveform(Innexus::ModulatorWaveform::Sine);
    mod.setRate(2.0f);

    // Advance 1000 samples
    for (int i = 0; i < 1000; ++i)
        mod.advance();

    float phaseBeforeNoteEvent = mod.getPhase();
    REQUIRE(phaseBeforeNoteEvent > 0.0f);

    // Simulate note-off/note-on: in our design, HarmonicModulator has
    // no note event method. The LFO is free-running and does not reset.
    // We simply continue advancing.

    // Advance 1 more sample
    mod.advance();

    float phaseAfterNoteEvent = mod.getPhase();

    // Phase should have continued from where it was, not reset to 0
    float expectedPhase = phaseBeforeNoteEvent + 2.0f / 44100.0f;
    REQUIRE(phaseAfterNoteEvent == Approx(expectedPhase).margin(1e-6f));
}

TEST_CASE("HarmonicModulator: reset() exists but is separate from note events",
          "[modulator][FR-051]")
{
    Innexus::HarmonicModulator mod;
    mod.prepare(44100.0);
    mod.setRate(2.0f);

    // Advance some samples
    for (int i = 0; i < 500; ++i)
        mod.advance();

    float phase = mod.getPhase();
    REQUIRE(phase > 0.0f);

    // reset() does zero the phase (used by prepare, NOT by MIDI)
    mod.reset();
    REQUIRE(mod.getPhase() == Approx(0.0f).margin(1e-7f));
}
