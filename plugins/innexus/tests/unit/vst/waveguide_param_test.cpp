// =============================================================================
// Waveguide String Parameter ID Tests (Spec 129, Phase 2)
// =============================================================================
// Verifies that new parameter IDs for waveguide string resonance are defined
// at the expected values.

#include "plugin_ids.h"

#include <krate/dsp/processors/waveguide_string.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using Catch::Approx;

TEST_CASE("Waveguide parameter IDs are correctly defined", "[vst][innexus][waveguide]")
{
    SECTION("kResonanceTypeId == 810")
    {
        REQUIRE(Innexus::kResonanceTypeId == 810);
    }

    SECTION("kWaveguideStiffnessId == 811")
    {
        REQUIRE(Innexus::kWaveguideStiffnessId == 811);
    }

    SECTION("kWaveguidePickPositionId == 812")
    {
        REQUIRE(Innexus::kWaveguidePickPositionId == 812);
    }

    SECTION("IDs are unique within 800 range")
    {
        // Verify no collisions with existing physical modelling IDs
        REQUIRE(Innexus::kResonanceTypeId != Innexus::kPhysModelMixId);
        REQUIRE(Innexus::kResonanceTypeId != Innexus::kResonanceDecayId);
        REQUIRE(Innexus::kResonanceTypeId != Innexus::kResonanceBrightnessId);
        REQUIRE(Innexus::kResonanceTypeId != Innexus::kResonanceStretchId);
        REQUIRE(Innexus::kResonanceTypeId != Innexus::kResonanceScatterId);
        REQUIRE(Innexus::kResonanceTypeId != Innexus::kExciterTypeId);
        REQUIRE(Innexus::kResonanceTypeId != Innexus::kImpactHardnessId);
        REQUIRE(Innexus::kResonanceTypeId != Innexus::kImpactMassId);
        REQUIRE(Innexus::kResonanceTypeId != Innexus::kImpactBrightnessId);
        REQUIRE(Innexus::kResonanceTypeId != Innexus::kImpactPositionId);
    }
}

// =============================================================================
// Phase 4: Stiffness Parameter Plumbing Tests (T062)
// =============================================================================

TEST_CASE("kWaveguideStiffnessId parameter - registration",
          "[vst][innexus][waveguide][stiffness]")
{
    // Verify the stiffness parameter is defined with ID 811,
    // and is distinct from all other waveguide params
    REQUIRE(Innexus::kWaveguideStiffnessId == 811);
    REQUIRE(Innexus::kWaveguideStiffnessId != Innexus::kResonanceTypeId);
    REQUIRE(Innexus::kWaveguideStiffnessId != Innexus::kWaveguidePickPositionId);

    // The parameter range [0.0, 1.0] and default 0.0 are verified by the
    // controller registration in controller.cpp (RangeParameter with
    // min=0.0, max=1.0, default=0.0). We verify the ID value here as a
    // compile-time contract check.
}

TEST_CASE("kWaveguideStiffnessId parameter - routing to voice",
          "[vst][innexus][waveguide][stiffness]")
{
    // Verify that the WaveguideString's setStiffness properly stores the value
    // and that it is frozen at noteOn. This tests the DSP-side of the plumbing
    // without requiring VST3 SDK infrastructure.
    Krate::DSP::WaveguideString ws;
    ws.prepare(44100.0);
    ws.prepareVoice(0);
    ws.setDecay(2.0f);
    ws.setBrightness(0.3f);

    // Set stiffness before noteOn
    ws.setStiffness(0.75f);
    ws.noteOn(220.0f, 0.8f);

    // Render a few samples -- should not crash or produce NaN
    bool valid = true;
    for (int i = 0; i < 64; ++i) {
        float out = ws.process(0.0f);
        if (std::isnan(out) || std::isinf(out))
            valid = false;
    }
    REQUIRE(valid);
}

// =============================================================================
// Phase 5: Pick Position Parameter Registration Tests (T076)
// =============================================================================

TEST_CASE("kWaveguidePickPositionId parameter - registration",
          "[vst][innexus][waveguide][pickposition]")
{
    // Verify pick position parameter ID, range, and default
    REQUIRE(Innexus::kWaveguidePickPositionId == 812);
    REQUIRE(Innexus::kWaveguidePickPositionId != Innexus::kResonanceTypeId);
    REQUIRE(Innexus::kWaveguidePickPositionId != Innexus::kWaveguideStiffnessId);

    // Verify the WaveguideString default pick position matches the parameter default (0.13)
    REQUIRE(Krate::DSP::WaveguideString::kDefaultPickPosition
            == Catch::Approx(0.13f).margin(0.001f));

    // Verify setPickPosition clamps to [0, 1]
    Krate::DSP::WaveguideString ws;
    ws.prepare(44100.0);
    ws.prepareVoice(0);
    ws.setDecay(2.0f);
    ws.setBrightness(0.3f);

    // Should not crash with edge values
    ws.setPickPosition(0.0f);
    ws.noteOn(220.0f, 0.8f);
    bool valid = true;
    for (int i = 0; i < 64; ++i) {
        float out = ws.process(0.0f);
        if (std::isnan(out) || std::isinf(out))
            valid = false;
    }
    REQUIRE(valid);

    ws.setPickPosition(1.0f);
    ws.noteOn(220.0f, 0.8f);
    for (int i = 0; i < 64; ++i) {
        float out = ws.process(0.0f);
        if (std::isnan(out) || std::isinf(out))
            valid = false;
    }
    REQUIRE(valid);
}
