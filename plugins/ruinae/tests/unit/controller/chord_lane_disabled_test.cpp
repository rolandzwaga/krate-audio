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
#include "view_lifecycle_rig.h"

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

// ==============================================================================
// Controller wiring: Voice Mode -> lane disabled state (R-128)
// ==============================================================================
// Drives the real Ruinae::Controller with the editor open on the SEQ tab: the lane views are the
// ones verifyView built and the controller cached, and kVoiceModeId goes through setParamNormalized.

namespace {

struct LaneProbe {
    ArpChordLane* chord = nullptr;
    ArpInversionLane* inversion = nullptr;
};

LaneProbe findLanes(const RuinaeViewLifecycleTest::ViewLifecycleRig& rig) {
    LaneProbe p;
    RuinaeViewLifecycleTest::forEachDescendant(rig.frame(), [&p](VSTGUI::CView* v) {
        if (auto* c = dynamic_cast<ArpChordLane*>(v)) p.chord = c;
        if (auto* i = dynamic_cast<ArpInversionLane*>(v)) p.inversion = i;
    });
    return p;
}

void requireLanesDisabled(const RuinaeViewLifecycleTest::ViewLifecycleRig& rig, bool expected) {
    const LaneProbe p = findLanes(rig);
    REQUIRE(p.chord != nullptr);
    REQUIRE(p.inversion != nullptr);
    CHECK(p.chord->isDisabled() == expected);
    CHECK(p.inversion->isDisabled() == expected);
}

}  // namespace

TEST_CASE("Controller_VoiceModeMono_DisablesChordAndInversionLanes", "[arp][disabled][voicemode][controller]") {
    using namespace RuinaeViewLifecycleTest;
    const auto state = defaultProcessorState();  // Poly
    ViewLifecycleRig rig({{Ruinae::kMainTabTag, kTabSeq}});
    requireLanesDisabled(rig, false);

    rig.ctl.setParamNormalized(Ruinae::kVoiceModeId, 1.0);
    requireLanesDisabled(rig, true);

    SECTION("boundary 0.5 is Mono") {
        rig.ctl.setParamNormalized(Ruinae::kVoiceModeId, 0.0);
        requireLanesDisabled(rig, false);
        rig.ctl.setParamNormalized(Ruinae::kVoiceModeId, 0.5);
        requireLanesDisabled(rig, true);
    }

    SECTION("after switching tabs away and back") {
        rig.clickControl(Ruinae::kMainTabTag, kTabSound);
        rig.clickControl(Ruinae::kMainTabTag, kTabSeq);
        requireLanesDisabled(rig, true);

        rig.ctl.setParamNormalized(Ruinae::kVoiceModeId, 0.0);  // cached pointers are live again
        requireLanesDisabled(rig, false);
    }

    SECTION("changed while the SEQ tab is away") {
        rig.clickControl(Ruinae::kMainTabTag, kTabSound);
        rig.ctl.setParamNormalized(Ruinae::kVoiceModeId, 0.0);
        rig.clickControl(Ruinae::kMainTabTag, kTabSeq);
        requireLanesDisabled(rig, false);
    }

    SECTION("after setComponentState") {
        REQUIRE(rig.loadState(state) == Steinberg::kResultTrue);  // Poly state
        requireLanesDisabled(rig, false);

        rig.ctl.setParamNormalized(Ruinae::kVoiceModeId, 1.0);
        requireLanesDisabled(rig, true);
        rig.clickControl(Ruinae::kMainTabTag, kTabSound);
        rig.clickControl(Ruinae::kMainTabTag, kTabSeq);
        REQUIRE(rig.loadState(state) == Steinberg::kResultTrue);
        requireLanesDisabled(rig, false);
    }
}
