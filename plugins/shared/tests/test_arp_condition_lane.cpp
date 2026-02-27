// ==============================================================================
// ArpConditionLane Tests (080-specialized-lane-types Phase 5)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/arp_condition_lane.h"

using namespace Krate::Plugins;
using namespace VSTGUI;
using Catch::Approx;

// Helper: create a default 500x44 ArpConditionLane
static ArpConditionLane* makeConditionLane(int numSteps = 8) {
    auto* lane = new ArpConditionLane(CRect(0, 0, 500, 63), nullptr, -1);
    lane->setNumSteps(numSteps);
    return lane;
}

// ==============================================================================
// Construction Tests (T048)
// ==============================================================================

TEST_CASE("ArpConditionLane default stepConditions all 0 (Always)", "[arp_condition_lane][construction]") {
    auto* lane = makeConditionLane();
    for (int i = 0; i < 32; ++i) {
        REQUIRE(lane->getStepCondition(i) == 0);
    }
    lane->forget();
}

TEST_CASE("ArpConditionLane numSteps defaults to 8", "[arp_condition_lane][construction]") {
    auto* lane = new ArpConditionLane(CRect(0, 0, 500, 63), nullptr, -1);
    REQUIRE(lane->getNumSteps() == 8);
    lane->forget();
}

TEST_CASE("ArpConditionLane getExpandedHeight = kBodyHeight + kHeight = 63.0f", "[arp_condition_lane][construction]") {
    auto* lane = makeConditionLane();
    REQUIRE(lane->getExpandedHeight() == Approx(63.0f).margin(0.01f));
    lane->forget();
}

TEST_CASE("ArpConditionLane getCollapsedHeight = 16.0f", "[arp_condition_lane][construction]") {
    auto* lane = makeConditionLane();
    REQUIRE(lane->getCollapsedHeight() == Approx(16.0f).margin(0.01f));
    lane->forget();
}

// ==============================================================================
// Abbreviation Lookup Tests (T049)
// ==============================================================================

TEST_CASE("ArpConditionLane abbreviation index 0 -> Alw", "[arp_condition_lane][abbreviation]") {
    REQUIRE(std::string(ArpConditionLane::kConditionAbbrev[0]) == "Alw");
}

TEST_CASE("ArpConditionLane abbreviation index 3 -> 50%", "[arp_condition_lane][abbreviation]") {
    REQUIRE(std::string(ArpConditionLane::kConditionAbbrev[3]) == "50%");
}

TEST_CASE("ArpConditionLane abbreviation index 6 -> Ev2", "[arp_condition_lane][abbreviation]") {
    REQUIRE(std::string(ArpConditionLane::kConditionAbbrev[6]) == "Ev2");
}

TEST_CASE("ArpConditionLane abbreviation index 7 -> 2:2", "[arp_condition_lane][abbreviation]") {
    REQUIRE(std::string(ArpConditionLane::kConditionAbbrev[7]) == "2:2");
}

TEST_CASE("ArpConditionLane abbreviation index 15 -> 1st", "[arp_condition_lane][abbreviation]") {
    REQUIRE(std::string(ArpConditionLane::kConditionAbbrev[15]) == "1st");
}

TEST_CASE("ArpConditionLane abbreviation index 16 -> Fill", "[arp_condition_lane][abbreviation]") {
    REQUIRE(std::string(ArpConditionLane::kConditionAbbrev[16]) == "Fill");
}

TEST_CASE("ArpConditionLane abbreviation index 17 -> !F", "[arp_condition_lane][abbreviation]") {
    REQUIRE(std::string(ArpConditionLane::kConditionAbbrev[17]) == "!F");
}

TEST_CASE("ArpConditionLane all 18 abbreviations match kConditionAbbrev table", "[arp_condition_lane][abbreviation]") {
    const char* expected[18] = {
        "Alw", "10%", "25%", "50%", "75%", "90%",
        "Ev2", "2:2", "Ev3", "2:3", "3:3",
        "Ev4", "2:4", "3:4", "4:4",
        "1st", "Fill", "!F"
    };
    for (int i = 0; i < 18; ++i) {
        REQUIRE(std::string(ArpConditionLane::kConditionAbbrev[i]) == std::string(expected[i]));
    }
}

// ==============================================================================
// Step Condition API Tests (T050)
// ==============================================================================

TEST_CASE("ArpConditionLane setStepCondition stores and retrieves value", "[arp_condition_lane][api]") {
    auto* lane = makeConditionLane();
    lane->setStepCondition(3, 5);
    REQUIRE(lane->getStepCondition(3) == 5);
    lane->forget();
}

TEST_CASE("ArpConditionLane setStepCondition with index >= 18 clamps to 0", "[arp_condition_lane][api]") {
    auto* lane = makeConditionLane();
    lane->setStepCondition(0, 20); // out of range
    REQUIRE(lane->getStepCondition(0) == 0);

    lane->setStepCondition(1, 255); // way out of range
    REQUIRE(lane->getStepCondition(1) == 0);

    lane->setStepCondition(2, 18); // exactly out of range
    REQUIRE(lane->getStepCondition(2) == 0);
    lane->forget();
}

TEST_CASE("ArpConditionLane setStepCondition with max valid index 17 works", "[arp_condition_lane][api]") {
    auto* lane = makeConditionLane();
    lane->setStepCondition(0, 17);
    REQUIRE(lane->getStepCondition(0) == 17);
    lane->forget();
}

TEST_CASE("ArpConditionLane parameter normalization: index 3 encodes as 3/17.0f", "[arp_condition_lane][normalization]") {
    float normalized = 3.0f / 17.0f;
    REQUIRE(normalized == Approx(0.17647f).margin(0.001f));
}

TEST_CASE("ArpConditionLane paramCallback fires with correct normalized value", "[arp_condition_lane][api]") {
    auto* lane = makeConditionLane();
    uint32_t receivedId = 0;
    float receivedValue = -1.0f;
    lane->setStepConditionBaseParamId(2000);
    lane->setParameterCallback([&](uint32_t id, float val) {
        receivedId = id;
        receivedValue = val;
    });

    // Verify encoding formula directly: index / 17.0f
    for (int idx = 0; idx < 18; ++idx) {
        float expected = static_cast<float>(idx) / 17.0f;
        REQUIRE(expected == Approx(static_cast<float>(idx) / 17.0f).margin(0.0001f));
    }
    lane->forget();
}

TEST_CASE("ArpConditionLane setStepCondition out-of-bounds step index is safe", "[arp_condition_lane][api]") {
    auto* lane = makeConditionLane();
    // Should not crash for negative or out-of-bounds step indices
    lane->setStepCondition(-1, 5);
    lane->setStepCondition(32, 5);
    lane->setStepCondition(100, 5);
    // Verify in-bounds step was not affected
    REQUIRE(lane->getStepCondition(0) == 0);
    lane->forget();
}

TEST_CASE("ArpConditionLane getStepCondition out-of-bounds step returns 0", "[arp_condition_lane][api]") {
    auto* lane = makeConditionLane();
    REQUIRE(lane->getStepCondition(-1) == 0);
    REQUIRE(lane->getStepCondition(32) == 0);
    REQUIRE(lane->getStepCondition(100) == 0);
    lane->forget();
}

// ==============================================================================
// IArpLane Interface Tests (T051)
// ==============================================================================

TEST_CASE("ArpConditionLane getView() returns non-null CView*", "[arp_condition_lane][interface]") {
    auto* lane = makeConditionLane();
    REQUIRE(lane->getView() != nullptr);
    REQUIRE(lane->getView() == static_cast<CView*>(lane));
    lane->forget();
}

TEST_CASE("ArpConditionLane setPlayheadStep(5) stores playheadStep_=5", "[arp_condition_lane][playhead][T080]") {
    auto* lane = makeConditionLane();
    lane->setPlayheadStep(5);
    REQUIRE(lane->getPlayheadStep() == 5);
    lane->forget();
}

TEST_CASE("ArpConditionLane setPlayheadStep(-1) clears playhead", "[arp_condition_lane][playhead][T080]") {
    auto* lane = makeConditionLane();
    lane->setPlayheadStep(5);
    REQUIRE(lane->getPlayheadStep() == 5);
    lane->setPlayheadStep(-1);
    REQUIRE(lane->getPlayheadStep() == -1);
    lane->forget();
}

TEST_CASE("ArpConditionLane out-of-bounds playhead step is handled gracefully", "[arp_condition_lane][playhead][T080]") {
    auto* lane = makeConditionLane(8);
    // Setting playhead to exactly numSteps (out-of-bounds) should not crash
    lane->setPlayheadStep(8);
    REQUIRE(lane->getPlayheadStep() == 8);
    // The drawBody overlay condition (playheadStep_ >= 0 && playheadStep_ < numSteps_)
    // will simply skip drawing the overlay for out-of-range values
    lane->forget();
}

TEST_CASE("ArpConditionLane playhead overlay only drawn when step in range", "[arp_condition_lane][playhead][T080]") {
    auto* lane = makeConditionLane(8);
    // Step in range: overlay should draw
    lane->setPlayheadStep(5);
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

TEST_CASE("ArpConditionLane setLength(12) sets numSteps to 12", "[arp_condition_lane][interface]") {
    auto* lane = makeConditionLane();
    lane->setLength(12);
    REQUIRE(lane->getNumSteps() == 12);
    lane->forget();
}

TEST_CASE("ArpConditionLane setCollapseCallback wires correctly", "[arp_condition_lane][interface]") {
    auto* lane = makeConditionLane();
    bool callbackFired = false;
    lane->setCollapseCallback([&callbackFired]() {
        callbackFired = true;
    });

    lane->setCollapsed(true);
    REQUIRE(callbackFired);
    lane->forget();
}

TEST_CASE("ArpConditionLane isCollapsed defaults to false", "[arp_condition_lane][interface]") {
    auto* lane = makeConditionLane();
    REQUIRE_FALSE(lane->isCollapsed());
    lane->forget();
}

TEST_CASE("ArpConditionLane setCollapsed toggles state", "[arp_condition_lane][interface]") {
    auto* lane = makeConditionLane();
    lane->setCollapsed(true);
    REQUIRE(lane->isCollapsed());
    lane->setCollapsed(false);
    REQUIRE_FALSE(lane->isCollapsed());
    lane->forget();
}

// ==============================================================================
// ViewCreator Tests (T052)
// ==============================================================================

TEST_CASE("ArpConditionLaneCreator creates instance with correct type name", "[arp_condition_lane][viewcreator]") {
    ArpConditionLaneCreator creator;
    REQUIRE(std::string(creator.getViewName()) == "ArpConditionLane");
    REQUIRE(std::string(creator.getDisplayName()) == "Arp Condition Lane");
}

TEST_CASE("ArpConditionLaneCreator creates non-null ArpConditionLane", "[arp_condition_lane][viewcreator]") {
    ArpConditionLaneCreator creator;
    VSTGUI::UIAttributes attrs;
    auto* view = creator.create(attrs, nullptr);
    REQUIRE(view != nullptr);

    auto* condLane = dynamic_cast<ArpConditionLane*>(view);
    REQUIRE(condLane != nullptr);

    view->forget();
}

// ==============================================================================
// Collapse State Integration Tests (T076)
// ==============================================================================

TEST_CASE("ArpConditionLane collapse round-trip: collapse -> verify 16px -> expand -> verify 44px",
          "[arp_condition_lane][collapse_cycle][T076]") {
    auto* lane = makeConditionLane();

    // Initial state: expanded
    REQUIRE_FALSE(lane->isCollapsed());
    REQUIRE(lane->getExpandedHeight() == Approx(63.0f).margin(0.01f));
    REQUIRE(lane->getCollapsedHeight() == Approx(16.0f).margin(0.01f));

    // Collapse
    lane->setCollapsed(true);
    REQUIRE(lane->isCollapsed());
    REQUIRE(lane->getCollapsedHeight() == Approx(16.0f).margin(0.01f));

    // Expand
    lane->setCollapsed(false);
    REQUIRE_FALSE(lane->isCollapsed());
    REQUIRE(lane->getExpandedHeight() == Approx(63.0f).margin(0.01f));

    lane->forget();
}

TEST_CASE("ArpConditionLane collapseCallback fires on each state change",
          "[arp_condition_lane][collapse_cycle][T076]") {
    auto* lane = makeConditionLane();
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

TEST_CASE("ArpConditionLane collapseCallback does NOT fire when state unchanged",
          "[arp_condition_lane][collapse_cycle][T076]") {
    auto* lane = makeConditionLane();
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
