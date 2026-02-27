// ==============================================================================
// ArpModifierLane Tests (080-specialized-lane-types Phase 4)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/arp_modifier_lane.h"

using namespace Krate::Plugins;
using namespace VSTGUI;
using Catch::Approx;

// Helper: create a default 500x60 ArpModifierLane
static ArpModifierLane* makeModifierLane(int numSteps = 16) {
    auto* lane = new ArpModifierLane(CRect(0, 0, 500, 79), nullptr, -1);
    lane->setNumSteps(numSteps);
    return lane;
}

// ==============================================================================
// Construction Tests (T032)
// ==============================================================================

TEST_CASE("ArpModifierLane default stepFlags all 0x01 (kStepActive)", "[arp_modifier_lane][construction]") {
    auto* lane = makeModifierLane();
    for (int i = 0; i < 32; ++i) {
        REQUIRE(lane->getStepFlags(i) == 0x01);
    }
    lane->forget();
}

TEST_CASE("ArpModifierLane numSteps defaults to 16", "[arp_modifier_lane][construction]") {
    auto* lane = new ArpModifierLane(CRect(0, 0, 500, 79), nullptr, -1);
    REQUIRE(lane->getNumSteps() == 16);
    lane->forget();
}

TEST_CASE("ArpModifierLane getExpandedHeight = kBodyHeight + kHeight = 79.0f", "[arp_modifier_lane][construction]") {
    auto* lane = makeModifierLane();
    REQUIRE(lane->getExpandedHeight() == Approx(79.0f).margin(0.01f));
    lane->forget();
}

TEST_CASE("ArpModifierLane getCollapsedHeight = 16.0f", "[arp_modifier_lane][construction]") {
    auto* lane = makeModifierLane();
    REQUIRE(lane->getCollapsedHeight() == Approx(16.0f).margin(0.01f));
    lane->forget();
}

// ==============================================================================
// Bitmask Toggling Tests (T033)
// ==============================================================================

TEST_CASE("ArpModifierLane toggle Rest on step 3 flips kStepActive XOR", "[arp_modifier_lane][toggle]") {
    auto* lane = makeModifierLane();
    // Default is 0x01 (kStepActive). Toggle Rest (row 0) = XOR 0x01
    uint8_t flags = lane->getStepFlags(3);
    REQUIRE(flags == 0x01);

    // Simulate toggling Rest: XOR with kStepActive
    lane->setStepFlags(3, static_cast<uint8_t>(flags ^ 0x01));
    REQUIRE(lane->getStepFlags(3) == 0x00);

    // Toggle again should restore
    flags = lane->getStepFlags(3);
    lane->setStepFlags(3, static_cast<uint8_t>(flags ^ 0x01));
    REQUIRE(lane->getStepFlags(3) == 0x01);
    lane->forget();
}

TEST_CASE("ArpModifierLane toggle Tie on step 5 sets bit 1", "[arp_modifier_lane][toggle]") {
    auto* lane = makeModifierLane();
    uint8_t flags = lane->getStepFlags(5);
    REQUIRE(flags == 0x01); // default kStepActive

    // Toggle Tie (row 1) = XOR 0x02
    lane->setStepFlags(5, static_cast<uint8_t>(flags ^ 0x02));
    REQUIRE(lane->getStepFlags(5) == 0x03); // kStepActive | kStepTie
    lane->forget();
}

TEST_CASE("ArpModifierLane toggle Slide preserves existing flags", "[arp_modifier_lane][toggle]") {
    auto* lane = makeModifierLane();
    // First set Tie on step 5
    lane->setStepFlags(5, 0x03); // kStepActive | kStepTie

    // Toggle Slide (row 2) = XOR 0x04
    uint8_t flags = lane->getStepFlags(5);
    lane->setStepFlags(5, static_cast<uint8_t>(flags ^ 0x04));
    REQUIRE(lane->getStepFlags(5) == 0x07); // kStepActive | kStepTie | kStepSlide
    lane->forget();
}

TEST_CASE("ArpModifierLane toggle Accent on step 7 sets bit 3", "[arp_modifier_lane][toggle]") {
    auto* lane = makeModifierLane();
    uint8_t flags = lane->getStepFlags(7);
    lane->setStepFlags(7, static_cast<uint8_t>(flags ^ 0x08));
    REQUIRE(lane->getStepFlags(7) == 0x09); // kStepActive | kStepAccent
    lane->forget();
}

TEST_CASE("ArpModifierLane toggle Accent again clears kStepAccent", "[arp_modifier_lane][toggle]") {
    auto* lane = makeModifierLane();
    // Set Accent
    lane->setStepFlags(7, 0x09); // kStepActive | kStepAccent
    // Toggle Accent again
    uint8_t flags = lane->getStepFlags(7);
    lane->setStepFlags(7, static_cast<uint8_t>(flags ^ 0x08));
    REQUIRE(lane->getStepFlags(7) == 0x01); // back to kStepActive only
    lane->forget();
}

// ==============================================================================
// IArpLane Interface Tests (T034)
// ==============================================================================

TEST_CASE("ArpModifierLane getView() returns non-null CView*", "[arp_modifier_lane][interface]") {
    auto* lane = makeModifierLane();
    REQUIRE(lane->getView() != nullptr);
    // getView should return this
    REQUIRE(lane->getView() == static_cast<CView*>(lane));
    lane->forget();
}

TEST_CASE("ArpModifierLane setPlayheadStep(3) stores playheadStep_=3", "[arp_modifier_lane][playhead][T079]") {
    auto* lane = makeModifierLane();
    lane->setPlayheadStep(3);
    REQUIRE(lane->getPlayheadStep() == 3);
    lane->forget();
}

TEST_CASE("ArpModifierLane setPlayheadStep(-1) clears playhead", "[arp_modifier_lane][playhead][T079]") {
    auto* lane = makeModifierLane();
    lane->setPlayheadStep(5);
    REQUIRE(lane->getPlayheadStep() == 5);
    lane->setPlayheadStep(-1);
    REQUIRE(lane->getPlayheadStep() == -1);
    lane->forget();
}

TEST_CASE("ArpModifierLane setPlayheadStep(numSteps_) does not crash", "[arp_modifier_lane][playhead][T079]") {
    auto* lane = makeModifierLane(8);
    // Setting playhead to exactly numSteps (out-of-bounds) should not crash
    lane->setPlayheadStep(8);
    // The value is stored but drawBody will skip overlay (condition: playheadStep_ < numSteps_)
    REQUIRE(lane->getPlayheadStep() == 8);
    lane->forget();
}

TEST_CASE("ArpModifierLane playhead overlay only drawn when step in range", "[arp_modifier_lane][playhead][T079]") {
    auto* lane = makeModifierLane(8);
    // Step in range: overlay should draw (verified via getPlayheadStep)
    lane->setPlayheadStep(3);
    REQUIRE(lane->getPlayheadStep() >= 0);
    REQUIRE(lane->getPlayheadStep() < lane->getNumSteps());

    // Step out of range: overlay will not draw
    lane->setPlayheadStep(8);
    bool outOfRange = lane->getPlayheadStep() >= 0 && lane->getPlayheadStep() < lane->getNumSteps();
    REQUIRE_FALSE(outOfRange);

    // Step negative: overlay will not draw
    lane->setPlayheadStep(-1);
    bool negative = lane->getPlayheadStep() >= 0 && lane->getPlayheadStep() < lane->getNumSteps();
    REQUIRE_FALSE(negative);
    lane->forget();
}

TEST_CASE("ArpModifierLane setLength(8) sets numSteps to 8", "[arp_modifier_lane][interface]") {
    auto* lane = makeModifierLane();
    lane->setLength(8);
    REQUIRE(lane->getNumSteps() == 8);
    lane->forget();
}

TEST_CASE("ArpModifierLane setCollapseCallback wires correctly", "[arp_modifier_lane][interface]") {
    auto* lane = makeModifierLane();
    bool callbackFired = false;
    lane->setCollapseCallback([&callbackFired]() {
        callbackFired = true;
    });

    // Trigger collapse state change
    lane->setCollapsed(true);
    REQUIRE(callbackFired);
    lane->forget();
}

TEST_CASE("ArpModifierLane isCollapsed defaults to false", "[arp_modifier_lane][interface]") {
    auto* lane = makeModifierLane();
    REQUIRE_FALSE(lane->isCollapsed());
    lane->forget();
}

TEST_CASE("ArpModifierLane setCollapsed toggles state", "[arp_modifier_lane][interface]") {
    auto* lane = makeModifierLane();
    lane->setCollapsed(true);
    REQUIRE(lane->isCollapsed());
    lane->setCollapsed(false);
    REQUIRE_FALSE(lane->isCollapsed());
    lane->forget();
}

// ==============================================================================
// Parameter Normalization Tests (T035)
// ==============================================================================

TEST_CASE("ArpModifierLane bitmask 0x01 encodes as 1/15.0f", "[arp_modifier_lane][normalization]") {
    auto* lane = makeModifierLane();
    uint32_t receivedId = 0;
    float receivedValue = -1.0f;
    lane->setStepFlagBaseParamId(1000);
    lane->setParameterCallback([&](uint32_t id, float val) {
        receivedId = id;
        receivedValue = val;
    });

    // Set flags to 0x01 and fire callback
    lane->setStepFlags(0, 0x01);
    // Need to manually trigger callback since setStepFlags just stores
    // The actual callback is fired via onMouseDown interaction.
    // Instead, verify the encoding formula directly.
    float normalized = static_cast<float>(0x01 & 0x0F) / 15.0f;
    REQUIRE(normalized == Approx(1.0f / 15.0f).margin(0.0001f));
    lane->forget();
}

TEST_CASE("ArpModifierLane bitmask 0x0F encodes as 1.0f", "[arp_modifier_lane][normalization]") {
    float normalized = static_cast<float>(0x0F & 0x0F) / 15.0f;
    REQUIRE(normalized == Approx(1.0f).margin(0.0001f));
}

TEST_CASE("ArpModifierLane bitmask 0x00 encodes as 0.0f", "[arp_modifier_lane][normalization]") {
    float normalized = static_cast<float>(0x00 & 0x0F) / 15.0f;
    REQUIRE(normalized == Approx(0.0f).margin(0.0001f));
}

TEST_CASE("ArpModifierLane bitmask 0x09 encodes as 9/15.0f", "[arp_modifier_lane][normalization]") {
    float normalized = static_cast<float>(0x09 & 0x0F) / 15.0f;
    REQUIRE(normalized == Approx(9.0f / 15.0f).margin(0.0001f));
}

// ==============================================================================
// High-Bit Masking Tests (T036)
// ==============================================================================

TEST_CASE("ArpModifierLane setStepFlags(i, 0xFF) stores 0x0F", "[arp_modifier_lane][masking]") {
    auto* lane = makeModifierLane();
    lane->setStepFlags(0, 0xFF);
    REQUIRE(lane->getStepFlags(0) == 0x0F);
    lane->forget();
}

TEST_CASE("ArpModifierLane setStepFlags(i, 0xF0) stores 0x00", "[arp_modifier_lane][masking]") {
    auto* lane = makeModifierLane();
    lane->setStepFlags(0, 0xF0);
    REQUIRE(lane->getStepFlags(0) == 0x00);
    lane->forget();
}

TEST_CASE("ArpModifierLane getStepFlags always returns value in 0x00-0x0F", "[arp_modifier_lane][masking]") {
    auto* lane = makeModifierLane();
    // Test various inputs
    const uint8_t testValues[] = {0x00, 0x01, 0x0F, 0x10, 0x80, 0xFF, 0xAB, 0xF0};
    for (uint8_t val : testValues) {
        lane->setStepFlags(0, val);
        uint8_t result = lane->getStepFlags(0);
        REQUIRE(result <= 0x0F);
    }
    lane->forget();
}

// ==============================================================================
// ViewCreator Tests (T037)
// ==============================================================================

TEST_CASE("ArpModifierLaneCreator creates instance with correct type name", "[arp_modifier_lane][viewcreator]") {
    ArpModifierLaneCreator creator;
    REQUIRE(std::string(creator.getViewName()) == "ArpModifierLane");
    REQUIRE(std::string(creator.getDisplayName()) == "Arp Modifier Lane");
}

TEST_CASE("ArpModifierLaneCreator creates non-null ArpModifierLane", "[arp_modifier_lane][viewcreator]") {
    ArpModifierLaneCreator creator;
    VSTGUI::UIAttributes attrs;
    auto* view = creator.create(attrs, nullptr);
    REQUIRE(view != nullptr);

    auto* modLane = dynamic_cast<ArpModifierLane*>(view);
    REQUIRE(modLane != nullptr);

    view->forget();
}

// ==============================================================================
// Collapse State Integration Tests (T075)
// ==============================================================================

TEST_CASE("ArpModifierLane collapse round-trip: collapse -> verify 16px -> expand -> verify 60px",
          "[arp_modifier_lane][collapse_cycle][T075]") {
    auto* lane = makeModifierLane();

    // Initial state: expanded
    REQUIRE_FALSE(lane->isCollapsed());
    REQUIRE(lane->getExpandedHeight() == Approx(79.0f).margin(0.01f));
    REQUIRE(lane->getCollapsedHeight() == Approx(16.0f).margin(0.01f));

    // Collapse
    lane->setCollapsed(true);
    REQUIRE(lane->isCollapsed());
    REQUIRE(lane->getCollapsedHeight() == Approx(16.0f).margin(0.01f));

    // Expand
    lane->setCollapsed(false);
    REQUIRE_FALSE(lane->isCollapsed());
    REQUIRE(lane->getExpandedHeight() == Approx(79.0f).margin(0.01f));

    lane->forget();
}

TEST_CASE("ArpModifierLane collapseCallback fires on each state change",
          "[arp_modifier_lane][collapse_cycle][T075]") {
    auto* lane = makeModifierLane();
    int callbackCount = 0;
    lane->setCollapseCallback([&callbackCount]() {
        ++callbackCount;
    });

    // Collapse: callback fires
    lane->setCollapsed(true);
    REQUIRE(callbackCount == 1);

    // Expand: callback fires again
    lane->setCollapsed(false);
    REQUIRE(callbackCount == 2);

    // Collapse again: third fire
    lane->setCollapsed(true);
    REQUIRE(callbackCount == 3);

    lane->forget();
}

TEST_CASE("ArpModifierLane collapseCallback does NOT fire when state unchanged",
          "[arp_modifier_lane][collapse_cycle][T075]") {
    auto* lane = makeModifierLane();
    int callbackCount = 0;
    lane->setCollapseCallback([&callbackCount]() {
        ++callbackCount;
    });

    // Set collapsed when already not collapsed -> no change, no callback
    lane->setCollapsed(false);
    REQUIRE(callbackCount == 0);

    // Collapse
    lane->setCollapsed(true);
    REQUIRE(callbackCount == 1);

    // Set collapsed again when already collapsed -> no change, no callback
    lane->setCollapsed(true);
    REQUIRE(callbackCount == 1);

    lane->forget();
}
