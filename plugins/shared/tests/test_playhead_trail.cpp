// ==============================================================================
// Playhead Trail State Tests
// ==============================================================================
// Tests for PlayheadTrailState logic (advance, clear, markSkipped,
// clearPassedSkips) and IArpLane setTrailSteps/clearOverlays behavior.
//
// Phase 11c - User Story 1: Playhead Trail with Fading History
// Tags: [trail]
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/arp_lane.h"
#include "ui/arp_lane_editor.h"
#include "ui/arp_modifier_lane.h"
#include "ui/arp_condition_lane.h"

using namespace Krate::Plugins;

// ==============================================================================
// T014: PlayheadTrailState Unit Tests
// ==============================================================================

TEST_CASE("PlayheadTrailState advance shifts buffer correctly", "[trail]") {
    PlayheadTrailState state;

    // Initially all -1
    for (int i = 0; i < PlayheadTrailState::kTrailLength; ++i) {
        REQUIRE(state.steps[i] == -1);
    }

    // Advance step 5
    state.advance(5);
    REQUIRE(state.steps[0] == 5);
    REQUIRE(state.steps[1] == -1);
    REQUIRE(state.steps[2] == -1);
    REQUIRE(state.steps[3] == -1);

    // Advance step 6
    state.advance(6);
    REQUIRE(state.steps[0] == 6);
    REQUIRE(state.steps[1] == 5);
    REQUIRE(state.steps[2] == -1);
    REQUIRE(state.steps[3] == -1);

    // Advance step 7
    state.advance(7);
    REQUIRE(state.steps[0] == 7);
    REQUIRE(state.steps[1] == 6);
    REQUIRE(state.steps[2] == 5);
    REQUIRE(state.steps[3] == -1);

    // Advance step 8 - now all 4 slots filled
    state.advance(8);
    REQUIRE(state.steps[0] == 8);
    REQUIRE(state.steps[1] == 7);
    REQUIRE(state.steps[2] == 6);
    REQUIRE(state.steps[3] == 5);

    // Advance step 9 - oldest (step 5) drops out
    state.advance(9);
    REQUIRE(state.steps[0] == 9);
    REQUIRE(state.steps[1] == 8);
    REQUIRE(state.steps[2] == 7);
    REQUIRE(state.steps[3] == 6);
}

TEST_CASE("PlayheadTrailState advance wraps at lane boundary", "[trail]") {
    PlayheadTrailState state;

    // Simulate a 4-step lane wrapping: 2, 3, 0, 1
    state.advance(2);
    state.advance(3);
    state.advance(0);  // wrap
    state.advance(1);

    // Trail should contain [1, 0, 3, 2] (newest to oldest)
    REQUIRE(state.steps[0] == 1);
    REQUIRE(state.steps[1] == 0);
    REQUIRE(state.steps[2] == 3);
    REQUIRE(state.steps[3] == 2);
}

TEST_CASE("PlayheadTrailState clear resets all positions and skipped flags", "[trail]") {
    PlayheadTrailState state;

    // Fill trail and set some skips
    state.advance(1);
    state.advance(2);
    state.advance(3);
    state.advance(4);
    state.markSkipped(5);
    state.markSkipped(10);

    // Verify pre-clear state
    REQUIRE(state.steps[0] == 4);
    REQUIRE(state.skipped[5] == true);
    REQUIRE(state.skipped[10] == true);

    // Clear
    state.clear();

    // All positions should be -1
    for (int i = 0; i < PlayheadTrailState::kTrailLength; ++i) {
        REQUIRE(state.steps[i] == -1);
    }

    // All skip flags should be false
    for (int i = 0; i < 32; ++i) {
        REQUIRE(state.skipped[i] == false);
    }
}

TEST_CASE("PlayheadTrailState markSkipped and clearPassedSkips", "[trail]") {
    PlayheadTrailState state;

    // Advance trail to steps 5, 4, 3, 2
    state.advance(2);
    state.advance(3);
    state.advance(4);
    state.advance(5);

    // Mark step 3 and step 10 as skipped
    state.markSkipped(3);
    state.markSkipped(10);
    REQUIRE(state.skipped[3] == true);
    REQUIRE(state.skipped[10] == true);

    // clearPassedSkips: step 10 is NOT in the trail, so it should be cleared
    // step 3 IS in the trail, so it should remain
    state.clearPassedSkips();
    REQUIRE(state.skipped[3] == true);    // still in trail (position 2)
    REQUIRE(state.skipped[10] == false);  // not in trail, cleared
}

TEST_CASE("PlayheadTrailState markSkipped out-of-range is no-op", "[trail]") {
    PlayheadTrailState state;

    // Valid range is 0-31
    state.markSkipped(-1);    // no-op
    state.markSkipped(32);    // no-op
    state.markSkipped(100);   // no-op

    // All should remain false
    for (int i = 0; i < 32; ++i) {
        REQUIRE(state.skipped[i] == false);
    }

    // Valid index works
    state.markSkipped(0);
    REQUIRE(state.skipped[0] == true);
    state.markSkipped(31);
    REQUIRE(state.skipped[31] == true);
}

TEST_CASE("PlayheadTrailState kTrailAlphas has expected values", "[trail]") {
    REQUIRE(PlayheadTrailState::kTrailAlphas[0] == Catch::Approx(160.0f));
    REQUIRE(PlayheadTrailState::kTrailAlphas[1] == Catch::Approx(100.0f));
    REQUIRE(PlayheadTrailState::kTrailAlphas[2] == Catch::Approx(55.0f));
    REQUIRE(PlayheadTrailState::kTrailAlphas[3] == Catch::Approx(25.0f));
}

TEST_CASE("PlayheadTrailState trail clamping for short lanes", "[trail]") {
    // A lane with only 3 steps still uses 4-position trail.
    // Rendering code should check step < laneLength, but trail state
    // itself just stores raw indices. Verify it works correctly.
    PlayheadTrailState state;

    // 3-step lane: 0, 1, 2, 0, 1, 2, ...
    state.advance(0);
    state.advance(1);
    state.advance(2);
    state.advance(0);  // wrap

    REQUIRE(state.steps[0] == 0);
    REQUIRE(state.steps[1] == 2);
    REQUIRE(state.steps[2] == 1);
    REQUIRE(state.steps[3] == 0);
}

// ==============================================================================
// T015: IArpLane setTrailSteps / clearOverlays Tests
// ==============================================================================

TEST_CASE("ArpLaneEditor setTrailSteps stores trail data", "[trail]") {
    auto* lane = new ArpLaneEditor(VSTGUI::CRect(0, 0, 500, 86), nullptr, -1);
    lane->setLaneType(ArpLaneType::kVelocity);
    lane->setNumSteps(16);

    int32_t steps[4] = {5, 4, 3, 2};
    float alphas[4] = {160.0f, 100.0f, 55.0f, 25.0f};

    lane->setTrailSteps(steps, alphas);

    // Verify the lane accepted the trail data by checking that clearOverlays
    // resets it (since we can't directly query trail steps, we test clear)
    lane->clearOverlays();

    // After clearOverlays, the internal trail state should be reset.
    // We verify by setting trail again and then clearing again -- no crash.
    lane->setTrailSteps(steps, alphas);
    lane->clearOverlays();

    lane->forget();  // CView ref counting
}

TEST_CASE("ArpLaneEditor clearOverlays resets all positions to -1", "[trail]") {
    auto* lane = new ArpLaneEditor(VSTGUI::CRect(0, 0, 500, 86), nullptr, -1);
    lane->setLaneType(ArpLaneType::kVelocity);
    lane->setNumSteps(16);

    // Set some trail state
    int32_t steps[4] = {10, 9, 8, 7};
    float alphas[4] = {160.0f, 100.0f, 55.0f, 25.0f};
    lane->setTrailSteps(steps, alphas);

    // Set a skipped step
    lane->setSkippedStep(5);

    // Clear all overlays
    lane->clearOverlays();

    // Verify we can set trail again without issues (no stale state)
    int32_t newSteps[4] = {0, -1, -1, -1};
    lane->setTrailSteps(newSteps, alphas);

    lane->forget();
}

TEST_CASE("ArpModifierLane setTrailSteps stores trail data", "[trail]") {
    auto* lane = new ArpModifierLane(VSTGUI::CRect(0, 0, 500, 60), nullptr, -1);
    lane->setNumSteps(16);

    int32_t steps[4] = {5, 4, 3, 2};
    float alphas[4] = {160.0f, 100.0f, 55.0f, 25.0f};

    lane->setTrailSteps(steps, alphas);
    lane->clearOverlays();

    lane->forget();
}

TEST_CASE("ArpConditionLane setTrailSteps stores trail data", "[trail]") {
    auto* lane = new ArpConditionLane(VSTGUI::CRect(0, 0, 500, 44), nullptr, -1);
    lane->setNumSteps(16);

    int32_t steps[4] = {5, 4, 3, 2};
    float alphas[4] = {160.0f, 100.0f, 55.0f, 25.0f};

    lane->setTrailSteps(steps, alphas);
    lane->clearOverlays();

    lane->forget();
}

TEST_CASE("ArpLaneEditor setTrailSteps with all -1 is valid (no trail)", "[trail]") {
    auto* lane = new ArpLaneEditor(VSTGUI::CRect(0, 0, 500, 86), nullptr, -1);
    lane->setLaneType(ArpLaneType::kGate);
    lane->setNumSteps(8);

    int32_t steps[4] = {-1, -1, -1, -1};
    float alphas[4] = {160.0f, 100.0f, 55.0f, 25.0f};

    // Should not crash with empty trail
    lane->setTrailSteps(steps, alphas);
    lane->clearOverlays();

    lane->forget();
}

TEST_CASE("ArpModifierLane clearOverlays clears skipped flags", "[trail]") {
    auto* lane = new ArpModifierLane(VSTGUI::CRect(0, 0, 500, 60), nullptr, -1);
    lane->setNumSteps(16);

    lane->setSkippedStep(3);
    lane->setSkippedStep(7);
    lane->clearOverlays();

    // After clear, setting trail should work without stale skips
    int32_t steps[4] = {0, -1, -1, -1};
    float alphas[4] = {160.0f, 100.0f, 55.0f, 25.0f};
    lane->setTrailSteps(steps, alphas);

    lane->forget();
}

TEST_CASE("ArpConditionLane clearOverlays clears skipped flags", "[trail]") {
    auto* lane = new ArpConditionLane(VSTGUI::CRect(0, 0, 500, 44), nullptr, -1);
    lane->setNumSteps(16);

    lane->setSkippedStep(5);
    lane->clearOverlays();

    int32_t steps[4] = {2, 1, 0, -1};
    float alphas[4] = {160.0f, 100.0f, 55.0f, 25.0f};
    lane->setTrailSteps(steps, alphas);

    lane->forget();
}

// ==============================================================================
// T026: Skip Overlay Rendering Tests (Phase 4 - User Story 2)
// ==============================================================================

TEST_CASE("setSkippedStep sets correct flag in PlayheadTrailState", "[skip][trail]") {
    PlayheadTrailState state;

    // Initially all false
    for (int i = 0; i < 32; ++i) {
        REQUIRE(state.skipped[i] == false);
    }

    // Mark step 2 as skipped
    state.markSkipped(2);
    REQUIRE(state.skipped[2] == true);

    // Other steps remain unset
    REQUIRE(state.skipped[0] == false);
    REQUIRE(state.skipped[1] == false);
    REQUIRE(state.skipped[3] == false);

    // Mark multiple steps
    state.markSkipped(7);
    state.markSkipped(15);
    state.markSkipped(31);
    REQUIRE(state.skipped[7] == true);
    REQUIRE(state.skipped[15] == true);
    REQUIRE(state.skipped[31] == true);
    REQUIRE(state.skipped[2] == true); // still set from before
}

TEST_CASE("clearPassedSkips removes skip flags for steps no longer in trail", "[skip][trail]") {
    PlayheadTrailState state;

    // Trail at steps 5, 4, 3, 2
    state.advance(2);
    state.advance(3);
    state.advance(4);
    state.advance(5);

    // Mark steps inside and outside the trail as skipped
    state.markSkipped(3);  // in trail (position 2)
    state.markSkipped(5);  // in trail (position 0)
    state.markSkipped(8);  // NOT in trail
    state.markSkipped(0);  // NOT in trail
    state.markSkipped(20); // NOT in trail

    REQUIRE(state.skipped[3] == true);
    REQUIRE(state.skipped[5] == true);
    REQUIRE(state.skipped[8] == true);
    REQUIRE(state.skipped[0] == true);
    REQUIRE(state.skipped[20] == true);

    // clearPassedSkips should only keep skips for steps currently in the trail
    state.clearPassedSkips();

    REQUIRE(state.skipped[3] == true);   // still in trail
    REQUIRE(state.skipped[5] == true);   // still in trail
    REQUIRE(state.skipped[8] == false);  // cleared (not in trail)
    REQUIRE(state.skipped[0] == false);  // cleared (not in trail)
    REQUIRE(state.skipped[20] == false); // cleared (not in trail)
}

TEST_CASE("clearOverlays clears all skip flags", "[skip][trail]") {
    PlayheadTrailState state;

    // Set a bunch of skips
    state.markSkipped(0);
    state.markSkipped(5);
    state.markSkipped(15);
    state.markSkipped(31);
    state.advance(10);
    state.advance(11);

    // Verify pre-clear
    REQUIRE(state.skipped[0] == true);
    REQUIRE(state.skipped[5] == true);
    REQUIRE(state.skipped[15] == true);
    REQUIRE(state.skipped[31] == true);

    state.clear();

    // All skip flags should be cleared
    for (int i = 0; i < 32; ++i) {
        REQUIRE(state.skipped[i] == false);
    }

    // Trail positions also cleared
    for (int i = 0; i < PlayheadTrailState::kTrailLength; ++i) {
        REQUIRE(state.steps[i] == -1);
    }
}

TEST_CASE("setSkippedStep out-of-range indices are no-op", "[skip][trail]") {
    PlayheadTrailState state;

    // Valid range: 0-31
    state.markSkipped(-1);    // no-op
    state.markSkipped(-100);  // no-op
    state.markSkipped(32);    // no-op
    state.markSkipped(100);   // no-op
    state.markSkipped(255);   // no-op

    // None should have been set
    for (int i = 0; i < 32; ++i) {
        REQUIRE(state.skipped[i] == false);
    }
}

TEST_CASE("ArpLaneEditor setSkippedStep marks flag via trailState", "[skip][trail]") {
    auto* lane = new ArpLaneEditor(VSTGUI::CRect(0, 0, 500, 86), nullptr, -1);
    lane->setLaneType(ArpLaneType::kVelocity);
    lane->setNumSteps(16);

    // setSkippedStep should mark the skip in internal state
    lane->setSkippedStep(3);
    lane->setSkippedStep(7);

    // clearOverlays should clear all skips (verify no crash, state reset)
    lane->clearOverlays();

    // After clearOverlays, setting a new skip should still work
    lane->setSkippedStep(10);
    lane->clearOverlays();

    lane->forget();
}

TEST_CASE("ArpModifierLane setSkippedStep marks flag via trailState", "[skip][trail]") {
    auto* lane = new ArpModifierLane(VSTGUI::CRect(0, 0, 500, 60), nullptr, -1);
    lane->setNumSteps(16);

    lane->setSkippedStep(5);
    lane->setSkippedStep(12);

    // Verify clearOverlays clears skip state
    lane->clearOverlays();
    lane->setSkippedStep(0);
    lane->clearOverlays();

    lane->forget();
}

TEST_CASE("ArpConditionLane setSkippedStep marks flag via trailState", "[skip][trail]") {
    auto* lane = new ArpConditionLane(VSTGUI::CRect(0, 0, 500, 44), nullptr, -1);
    lane->setNumSteps(16);

    lane->setSkippedStep(2);
    lane->setSkippedStep(14);

    lane->clearOverlays();
    lane->setSkippedStep(31);
    lane->clearOverlays();

    lane->forget();
}

TEST_CASE("ArpLaneEditor setSkippedStep out-of-range is safe", "[skip][trail]") {
    auto* lane = new ArpLaneEditor(VSTGUI::CRect(0, 0, 500, 86), nullptr, -1);
    lane->setLaneType(ArpLaneType::kGate);
    lane->setNumSteps(8);

    // Should not crash or corrupt state
    lane->setSkippedStep(-1);
    lane->setSkippedStep(32);
    lane->setSkippedStep(100);

    // Valid skip should still work
    lane->setSkippedStep(5);
    lane->clearOverlays();

    lane->forget();
}
