// =============================================================================
// Exciter Type Switching Test (Spec 130, Phase 10 T109)
// =============================================================================
// Verifies that switching from ExciterType::Impact to ExciterType::Bow
// mid-note does not crash and the voice remains in a valid state.

#include "processor/innexus_voice.h"
#include "plugin_ids.h"

#include <krate/dsp/processors/bow_exciter.h>
#include <krate/dsp/processors/impact_exciter.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Switching Impact to Bow mid-note does not crash",
          "[innexus][voice][bow][exciter_switch]")
{
    Innexus::InnexusVoice voice;
    voice.prepare(44100.0, 0);

    // Start with Impact exciter active
    voice.active = true;
    voice.midiNote = 57; // A3
    voice.velocityGain = 0.8f;
    voice.impactExciter.trigger(0.8f, 0.5f, 0.3f, 0.0f, 0.13f, 220.0f);

    // Process a few samples with Impact exciter (transient burst -- may finish quickly)
    for (int i = 0; i < 100; ++i) {
        float fbVel = voice.waveguideString.getFeedbackVelocity();
        float excitation = voice.impactExciter.process(fbVel);
        (void)excitation; // We're just checking it doesn't crash
    }

    // Now switch to Bow exciter mid-note (simulating parameter change)
    voice.bowExciter.setPressure(0.3f);
    voice.bowExciter.setSpeed(0.5f);
    voice.bowExciter.setPosition(0.13f);
    voice.bowExciter.trigger(0.8f);

    // Process a few samples with Bow exciter
    // The voice should remain valid - no crash, no undefined state
    bool hasOutput = false;
    for (int i = 0; i < 500; ++i) {
        float fbVel = voice.waveguideString.getFeedbackVelocity();
        voice.bowExciter.setEnvelopeValue(1.0f);
        float excitation = voice.bowExciter.process(fbVel);
        if (excitation != 0.0f)
            hasOutput = true;
    }

    // Bow exciter should be active and producing output
    REQUIRE(voice.bowExciter.isActive());
    REQUIRE(hasOutput);

    // Voice should still be valid
    REQUIRE(voice.active);
    REQUIRE(voice.midiNote == 57);
}

TEST_CASE("Switching Bow to Impact mid-note does not crash",
          "[innexus][voice][bow][exciter_switch]")
{
    Innexus::InnexusVoice voice;
    voice.prepare(44100.0, 0);

    // Start with Bow exciter active
    voice.active = true;
    voice.midiNote = 57;
    voice.velocityGain = 0.8f;
    voice.bowExciter.setPressure(0.3f);
    voice.bowExciter.setSpeed(0.5f);
    voice.bowExciter.setPosition(0.13f);
    voice.bowExciter.trigger(0.8f);

    // Process a few samples with Bow exciter
    for (int i = 0; i < 100; ++i) {
        voice.bowExciter.setEnvelopeValue(1.0f);
        float excitation = voice.bowExciter.process(0.0f);
        (void)excitation;
    }

    REQUIRE(voice.bowExciter.isActive());

    // Switch to Impact exciter mid-note
    voice.impactExciter.trigger(0.8f, 0.5f, 0.3f, 0.0f, 0.13f, 220.0f);

    // Process samples with Impact exciter - should not crash
    for (int i = 0; i < 500; ++i) {
        float fbVel = voice.waveguideString.getFeedbackVelocity();
        float excitation = voice.impactExciter.process(fbVel);
        (void)excitation;
    }

    // Voice should remain valid
    REQUIRE(voice.active);
    REQUIRE(voice.midiNote == 57);
}
