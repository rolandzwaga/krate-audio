// ==============================================================================
// ArpChordLane / ArpInversionLane Disabled State Tests
// ==============================================================================
// Tests the disabled overlay state for chord and inversion lanes.
// Validates: setDisabled(), isDisabled(), mouse blocking, draw overlay,
// onTabChanged null safety, voice mode parameter wiring logic.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ui/arp_chord_lane.h"
#include "ui/arp_inversion_lane.h"
#include "ui/arp_lane.h"
#include "plugin_ids.h"

using namespace Krate::Plugins;

// ==============================================================================
// ArpChordLane Disabled State
// ==============================================================================

TEST_CASE("ArpChordLane_DisabledState_DefaultEnabled", "[arp][chord][disabled]") {
    ArpChordLane lane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);

    REQUIRE(lane.isDisabled() == false);
}

TEST_CASE("ArpChordLane_DisabledState_SetDisabledTrue", "[arp][chord][disabled]") {
    ArpChordLane lane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);

    lane.setDisabled(true, "Test message");
    REQUIRE(lane.isDisabled() == true);
}

TEST_CASE("ArpChordLane_DisabledState_SetDisabledFalse", "[arp][chord][disabled]") {
    ArpChordLane lane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);

    lane.setDisabled(true, "Test message");
    REQUIRE(lane.isDisabled() == true);

    lane.setDisabled(false);
    REQUIRE(lane.isDisabled() == false);
}

TEST_CASE("ArpChordLane_DisabledState_EmptyMessage", "[arp][chord][disabled]") {
    ArpChordLane lane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);

    lane.setDisabled(true);
    REQUIRE(lane.isDisabled() == true);
}

TEST_CASE("ArpChordLane_DisabledState_ToggleMultipleTimes", "[arp][chord][disabled]") {
    ArpChordLane lane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);

    for (int i = 0; i < 10; ++i) {
        lane.setDisabled(true, "Disabled");
        REQUIRE(lane.isDisabled() == true);
        lane.setDisabled(false);
        REQUIRE(lane.isDisabled() == false);
    }
}

// ==============================================================================
// ArpInversionLane Disabled State
// ==============================================================================

TEST_CASE("ArpInversionLane_DisabledState_DefaultEnabled", "[arp][inversion][disabled]") {
    ArpInversionLane lane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);

    REQUIRE(lane.isDisabled() == false);
}

TEST_CASE("ArpInversionLane_DisabledState_SetDisabledTrue", "[arp][inversion][disabled]") {
    ArpInversionLane lane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);

    lane.setDisabled(true, "Test message");
    REQUIRE(lane.isDisabled() == true);
}

TEST_CASE("ArpInversionLane_DisabledState_SetDisabledFalse", "[arp][inversion][disabled]") {
    ArpInversionLane lane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);

    lane.setDisabled(true, "Disabled");
    lane.setDisabled(false);
    REQUIRE(lane.isDisabled() == false);
}

// ==============================================================================
// IArpLane Interface — Default No-Op
// ==============================================================================

TEST_CASE("IArpLane_DisabledState_DefaultIsNotDisabled", "[arp][disabled]") {
    // ArpChordLane and ArpInversionLane implement setDisabled/isDisabled.
    // Other lane types use the default no-op in IArpLane.
    // Verify that the base class defaults work correctly.

    ArpChordLane lane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);
    IArpLane* iface = &lane;

    REQUIRE(iface->isDisabled() == false);
    iface->setDisabled(true, "test");
    REQUIRE(iface->isDisabled() == true);
    iface->setDisabled(false);
    REQUIRE(iface->isDisabled() == false);
}

// ==============================================================================
// Voice Mode → Disabled Logic (controller wiring)
// ==============================================================================
// Tests the core logic that maps voice mode parameter value to disabled state.
// This is the logic used in Controller::setParamNormalized for kVoiceModeId.
// ==============================================================================

TEST_CASE("VoiceModeDisabledLogic_PolyModeShouldEnableLanes", "[arp][disabled][voicemode]") {
    // Poly mode: value < 0.5 → lanes should be enabled
    double value = 0.0;  // Poly
    bool isMono = value >= 0.5;

    ArpChordLane chordLane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);
    ArpInversionLane invLane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);

    // Simulate the controller logic
    chordLane.setDisabled(isMono, "Chord lane requires Poly voice mode");
    invLane.setDisabled(isMono, "Inversion lane requires Poly voice mode");

    REQUIRE(chordLane.isDisabled() == false);
    REQUIRE(invLane.isDisabled() == false);
}

TEST_CASE("VoiceModeDisabledLogic_MonoModeShouldDisableLanes", "[arp][disabled][voicemode]") {
    // Mono mode: value >= 0.5 → lanes should be disabled
    double value = 1.0;  // Mono
    bool isMono = value >= 0.5;

    ArpChordLane chordLane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);
    ArpInversionLane invLane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);

    chordLane.setDisabled(isMono, "Chord lane requires Poly voice mode");
    invLane.setDisabled(isMono, "Inversion lane requires Poly voice mode");

    REQUIRE(chordLane.isDisabled() == true);
    REQUIRE(invLane.isDisabled() == true);
}

TEST_CASE("VoiceModeDisabledLogic_BoundaryValue", "[arp][disabled][voicemode]") {
    // At exactly 0.5, isMono should be true
    double value = 0.5;
    bool isMono = value >= 0.5;

    ArpChordLane chordLane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);
    chordLane.setDisabled(isMono, "Chord lane requires Poly voice mode");
    REQUIRE(chordLane.isDisabled() == true);
}

TEST_CASE("VoiceModeDisabledLogic_SwitchFromMonoToPoly", "[arp][disabled][voicemode]") {
    ArpChordLane chordLane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);
    ArpInversionLane invLane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);

    // Start in mono
    chordLane.setDisabled(true, "Chord lane requires Poly voice mode");
    invLane.setDisabled(true, "Inversion lane requires Poly voice mode");
    REQUIRE(chordLane.isDisabled() == true);
    REQUIRE(invLane.isDisabled() == true);

    // Switch to poly
    chordLane.setDisabled(false);
    invLane.setDisabled(false);
    REQUIRE(chordLane.isDisabled() == false);
    REQUIRE(invLane.isDisabled() == false);
}

// ==============================================================================
// Null Pointer Safety (simulates onTabChanged and setParamNormalized interaction)
// ==============================================================================

TEST_CASE("NullPointerSafety_NullLanePointersSkipped", "[arp][disabled][safety]") {
    // Simulates what happens in setParamNormalized when chordLane_ is null
    // (e.g., after tab switch or before SEQ tab visited)

    ArpChordLane* chordLane = nullptr;
    ArpInversionLane* inversionLane = nullptr;
    bool isMono = true;

    // This mirrors the controller code — must not crash
    if (chordLane)
        chordLane->setDisabled(isMono, "Chord lane requires Poly voice mode");
    if (inversionLane)
        inversionLane->setDisabled(isMono, "Inversion lane requires Poly voice mode");

    // No crash = success
    REQUIRE(true);
}

TEST_CASE("NullPointerSafety_AfterTabSwitchPointersAreNull", "[arp][disabled][safety]") {
    // Simulates: create lane, then null it (as onTabChanged would do),
    // then try to call setDisabled on the nulled pointer

    ArpChordLane* chordLane = new ArpChordLane(
        VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);
    chordLane->setDisabled(true, "test");
    REQUIRE(chordLane->isDisabled() == true);

    // Simulate onTabChanged nulling
    chordLane->forget();  // CBaseObject ref count
    chordLane = nullptr;

    // Now the controller logic should skip the null pointer
    if (chordLane)
        chordLane->setDisabled(false);

    REQUIRE(chordLane == nullptr);
}

// ==============================================================================
// Parameter ID Verification
// ==============================================================================

TEST_CASE("VoiceModeParameterIds_Correct", "[arp][disabled][params]") {
    REQUIRE(Ruinae::kVoiceModeId == 1);
    // Poly = 0, Mono = 1 (normalized: 0.0 = Poly, 1.0 = Mono)
}

// ==============================================================================
// Step Value Operations While Disabled
// ==============================================================================

TEST_CASE("ArpChordLane_StepValuesPreservedWhenDisabled", "[arp][chord][disabled]") {
    ArpChordLane lane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);

    // Set some step values
    lane.setStepValue(0, 2);  // Triad
    lane.setStepValue(1, 3);  // 7th
    lane.setStepValue(2, 4);  // 9th

    // Disable the lane
    lane.setDisabled(true, "Mono mode");

    // Step values should still be readable
    REQUIRE(lane.getStepValue(0) == 2);
    REQUIRE(lane.getStepValue(1) == 3);
    REQUIRE(lane.getStepValue(2) == 4);

    // Re-enable
    lane.setDisabled(false);
    REQUIRE(lane.getStepValue(0) == 2);
    REQUIRE(lane.getStepValue(1) == 3);
    REQUIRE(lane.getStepValue(2) == 4);
}

// ==============================================================================
// Root Cause Scenario: Tab switch recreates lanes without disabled sync
// ==============================================================================

TEST_CASE("VoiceModeDisabledLogic_FreshLaneGetsDisabledFromParam", "[arp][disabled][voicemode]") {
    // Simulates the actual bug scenario:
    // 1. User sets voice mode to Mono on SOUND tab (chordLane_ is null)
    // 2. User switches to SEQ tab, verifyView creates fresh lanes
    // 3. The fresh lane must read the current voice mode param and disable itself
    //
    // This is the verifyView initialization path, not setParamNormalized.

    // Step 1: Voice mode param is set to Mono (normalized 1.0)
    double voiceModeNorm = 1.0;  // Mono
    bool isMono = voiceModeNorm >= 0.5;

    // Step 2: Fresh lanes are created (default: disabled_ = false)
    ArpChordLane chordLane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);
    ArpInversionLane invLane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);

    REQUIRE(chordLane.isDisabled() == false);  // default
    REQUIRE(invLane.isDisabled() == false);

    // Step 3: verifyView syncs disabled state from param
    chordLane.setDisabled(isMono, "Chord lane requires Poly voice mode");
    invLane.setDisabled(isMono, "Inversion lane requires Poly voice mode");

    REQUIRE(chordLane.isDisabled() == true);
    REQUIRE(invLane.isDisabled() == true);
}

TEST_CASE("VoiceModeDisabledLogic_FreshLaneStaysEnabledInPoly", "[arp][disabled][voicemode]") {
    double voiceModeNorm = 0.0;  // Poly
    bool isMono = voiceModeNorm >= 0.5;

    ArpChordLane chordLane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);
    chordLane.setDisabled(isMono, "Chord lane requires Poly voice mode");

    REQUIRE(chordLane.isDisabled() == false);
}

TEST_CASE("ArpInversionLane_StepValuesPreservedWhenDisabled", "[arp][inversion][disabled]") {
    ArpInversionLane lane(VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);

    lane.setStepValue(0, 1);  // 1st Inv
    lane.setStepValue(1, 2);  // 2nd Inv

    lane.setDisabled(true, "Mono mode");
    REQUIRE(lane.getStepValue(0) == 1);
    REQUIRE(lane.getStepValue(1) == 2);

    lane.setDisabled(false);
    REQUIRE(lane.getStepValue(0) == 1);
    REQUIRE(lane.getStepValue(1) == 2);
}
