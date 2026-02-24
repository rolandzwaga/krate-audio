// ==============================================================================
// ArpLaneContainer Tests (079-layout-framework + 080-specialized-lane-types)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/arp_lane_container.h"
#include "ui/arp_modifier_lane.h"
#include "ui/arp_condition_lane.h"

using namespace Krate::Plugins;
using namespace VSTGUI;
using Catch::Approx;

// Helper: create a default ArpLaneContainer sized 500x390
static ArpLaneContainer makeContainer(float viewportHeight = 390.0f) {
    ArpLaneContainer container(CRect(0, 0, 500, viewportHeight));
    container.setViewportHeight(viewportHeight);
    return container;
}

// Helper: create an ArpLaneEditor with a given height
// Note: we use `new` because ArpLaneContainer::addLane takes ownership via addView
static ArpLaneEditor* makeArpLane(float height = 86.0f) {
    auto* lane = new ArpLaneEditor(CRect(0, 0, 500, height), nullptr, -1);
    lane->setNumSteps(16);
    return lane;
}

// ==============================================================================
// addLane / getLaneCount Tests (T011)
// ==============================================================================

TEST_CASE("ArpLaneContainer addLane increments lane count", "[arp_lane_container][lanes]") {
    auto container = makeContainer();

    auto* lane1 = makeArpLane();
    auto* lane2 = makeArpLane();

    container.addLane(lane1);
    REQUIRE(container.getLaneCount() == 1);

    container.addLane(lane2);
    REQUIRE(container.getLaneCount() == 2);
}

// ==============================================================================
// recalculateLayout Tests (T012)
// ==============================================================================

TEST_CASE("recalculateLayout with two expanded lanes sums heights", "[arp_lane_container][layout]") {
    auto container = makeContainer();

    auto* lane1 = makeArpLane(86.0f);
    auto* lane2 = makeArpLane(86.0f);

    container.addLane(lane1);
    container.addLane(lane2);
    // addLane calls recalculateLayout automatically

    REQUIRE(container.getTotalContentHeight() == Approx(172.0f).margin(0.01f));
}

TEST_CASE("recalculateLayout with one collapsed lane reduces total height", "[arp_lane_container][layout]") {
    auto container = makeContainer();

    auto* lane1 = makeArpLane(86.0f);
    auto* lane2 = makeArpLane(86.0f);

    container.addLane(lane1);
    container.addLane(lane2);

    // Collapse lane 0
    lane1->setCollapsed(true);
    // The collapse callback triggers recalculateLayout

    // Collapsed lane = 16.0f (kHeaderHeight), expanded lane = 86.0f
    REQUIRE(container.getTotalContentHeight() == Approx(16.0f + 86.0f).margin(0.01f));
}

// ==============================================================================
// Scroll Logic Tests (T013)
// ==============================================================================

TEST_CASE("getMaxScrollOffset returns 0 when content fits in viewport", "[arp_lane_container][scroll]") {
    auto container = makeContainer(390.0f);

    auto* lane1 = makeArpLane(86.0f);
    auto* lane2 = makeArpLane(86.0f);

    container.addLane(lane1);
    container.addLane(lane2);

    // Total content = 172.0f, viewport = 390.0f -> no scroll needed
    REQUIRE(container.getMaxScrollOffset() == Approx(0.0f).margin(0.01f));
}

TEST_CASE("getMaxScrollOffset returns positive when content exceeds viewport", "[arp_lane_container][scroll]") {
    // Use a small viewport to force scrolling
    auto container = makeContainer(100.0f);

    auto* lane1 = makeArpLane(86.0f);
    auto* lane2 = makeArpLane(86.0f);

    container.addLane(lane1);
    container.addLane(lane2);

    // Total content = 172.0f, viewport = 100.0f -> max scroll = 72.0f
    REQUIRE(container.getMaxScrollOffset() == Approx(72.0f).margin(0.01f));
}

// ==============================================================================
// removeLane Tests (T014)
// ==============================================================================

TEST_CASE("removeLane decrements count and recalculates layout", "[arp_lane_container][lanes]") {
    auto container = makeContainer();

    auto* lane1 = makeArpLane(86.0f);
    auto* lane2 = makeArpLane(86.0f);

    container.addLane(lane1);
    container.addLane(lane2);
    REQUIRE(container.getLaneCount() == 2);
    REQUIRE(container.getTotalContentHeight() == Approx(172.0f).margin(0.01f));

    container.removeLane(lane1);
    REQUIRE(container.getLaneCount() == 1);
    REQUIRE(container.getTotalContentHeight() == Approx(86.0f).margin(0.01f));
}

TEST_CASE("removeLane repositions remaining lanes correctly", "[arp_lane_container][lanes]") {
    auto container = makeContainer();

    auto* lane1 = makeArpLane(86.0f);
    auto* lane2 = makeArpLane(86.0f);

    container.addLane(lane1);
    container.addLane(lane2);

    // Before removal: lane2 should be at y=86
    CRect lane2Rect = lane2->getViewSize();
    REQUIRE(static_cast<float>(lane2Rect.top) == Approx(86.0f).margin(0.01f));

    // Remove lane1: lane2 should now be at y=0
    container.removeLane(lane1);
    lane2Rect = lane2->getViewSize();
    REQUIRE(static_cast<float>(lane2Rect.top) == Approx(0.0f).margin(0.01f));
}

// ==============================================================================
// Left-Alignment Tests (T043)
// ==============================================================================

TEST_CASE("All lanes have the same left origin after recalculateLayout", "[arp_lane_container][alignment]") {
    auto container = makeContainer();

    // Create lanes with different step counts to simulate different bar widths
    auto* lane1 = makeArpLane(86.0f);
    auto* lane2 = makeArpLane(86.0f);

    lane1->setNumSteps(16);
    lane2->setNumSteps(8);

    container.addLane(lane1);
    container.addLane(lane2);

    // After recalculateLayout (called by addLane), both lanes should share
    // the same left origin (left-alignment regardless of step count)
    CRect lane1Rect = lane1->getViewSize();
    CRect lane2Rect = lane2->getViewSize();

    REQUIRE(static_cast<float>(lane1Rect.left) ==
            Approx(static_cast<float>(lane2Rect.left)).margin(0.01f));
}

TEST_CASE("Left-alignment preserved with three lanes of different step counts", "[arp_lane_container][alignment]") {
    auto container = makeContainer();

    auto* lane1 = makeArpLane(86.0f);
    auto* lane2 = makeArpLane(86.0f);
    auto* lane3 = makeArpLane(86.0f);

    lane1->setNumSteps(16);
    lane2->setNumSteps(8);
    lane3->setNumSteps(32);

    container.addLane(lane1);
    container.addLane(lane2);
    container.addLane(lane3);

    CRect r1 = lane1->getViewSize();
    CRect r2 = lane2->getViewSize();
    CRect r3 = lane3->getViewSize();

    // All lanes must share the same left origin
    REQUIRE(static_cast<float>(r1.left) == Approx(static_cast<float>(r2.left)).margin(0.01f));
    REQUIRE(static_cast<float>(r2.left) == Approx(static_cast<float>(r3.left)).margin(0.01f));
}

// ==============================================================================
// Dynamic Height with Collapse Tests (T052)
// ==============================================================================

TEST_CASE("Collapsing both lanes reduces totalContentHeight to 32px",
          "[arp_lane_container][collapse][dynamic_height]") {
    auto container = makeContainer(390.0f);

    auto* lane1 = makeArpLane(86.0f);
    auto* lane2 = makeArpLane(86.0f);

    container.addLane(lane1);
    container.addLane(lane2);

    // Both expanded: 86 + 86 = 172
    REQUIRE(container.getTotalContentHeight() == Approx(172.0f).margin(0.01f));

    // Collapse lane 0 -> 16 + 86 = 102 (FR-011)
    lane1->setCollapsed(true);
    REQUIRE(container.getTotalContentHeight() == Approx(16.0f + 86.0f).margin(0.01f));

    // Collapse lane 1 as well -> 16 + 16 = 32 (SC-004)
    lane2->setCollapsed(true);
    REQUIRE(container.getTotalContentHeight() == Approx(32.0f).margin(0.01f));
}

TEST_CASE("Expanding both lanes restores totalContentHeight",
          "[arp_lane_container][collapse][dynamic_height]") {
    auto container = makeContainer(390.0f);

    auto* lane1 = makeArpLane(86.0f);
    auto* lane2 = makeArpLane(86.0f);

    container.addLane(lane1);
    container.addLane(lane2);

    // Collapse both
    lane1->setCollapsed(true);
    lane2->setCollapsed(true);
    REQUIRE(container.getTotalContentHeight() == Approx(32.0f).margin(0.01f));

    // Expand both -> back to 172
    lane1->setCollapsed(false);
    lane2->setCollapsed(false);
    REQUIRE(container.getTotalContentHeight() == Approx(172.0f).margin(0.01f));
}

// ==============================================================================
// Scroll Clamping After Collapse Tests (T053)
// ==============================================================================

TEST_CASE("Scroll offset clamps to 0 when content shrinks below viewport after collapse",
          "[arp_lane_container][collapse][scroll_clamp]") {
    // Use a small viewport so scrolling is initially possible
    auto container = makeContainer(390.0f);

    auto* lane1 = makeArpLane(86.0f);
    auto* lane2 = makeArpLane(86.0f);

    container.addLane(lane1);
    container.addLane(lane2);

    // Content = 172, viewport = 390 -> max scroll = 0, so scrollOffset stays 0
    // Manually set scrollOffset to 30 via setScrollOffset
    // (since max scroll is 0, it would clamp immediately -- so use a smaller viewport first)
    container.setViewportHeight(100.0f);
    // Now max scroll = 172 - 100 = 72
    container.setScrollOffset(30.0f);
    REQUIRE(container.getScrollOffset() == Approx(30.0f).margin(0.01f));

    // Restore viewport to 390
    container.setViewportHeight(390.0f);

    // Collapse both lanes -> content = 32, viewport = 390 -> max scroll = 0
    lane1->setCollapsed(true);
    lane2->setCollapsed(true);

    // After recalculateLayout (triggered by collapse callback), scrollOffset must be 0
    REQUIRE(container.getTotalContentHeight() == Approx(32.0f).margin(0.01f));
    REQUIRE(container.getScrollOffset() == Approx(0.0f).margin(0.01f));
}

// ==============================================================================
// Wheel Scroll Tests (T065) - Tests scrollByWheelDelta which is the core
// logic called by onMouseWheelEvent. Tests use scrollByWheelDelta directly
// to avoid VSTGUI event dispatch infrastructure requirements in unit tests.
// ==============================================================================

TEST_CASE("scrollByWheelDelta with deltaY=-3 increases scrollOffset by 60",
          "[arp_lane_container][scroll][wheel]") {
    // Use a small viewport so scrolling is possible
    auto container = makeContainer(100.0f);

    // Add enough lanes to exceed viewport (86 + 86 + 86 = 258, viewport = 100)
    auto* lane1 = makeArpLane(86.0f);
    auto* lane2 = makeArpLane(86.0f);
    auto* lane3 = makeArpLane(86.0f);

    container.addLane(lane1);
    container.addLane(lane2);
    container.addLane(lane3);

    // Total content = 258, viewport = 100 -> maxScrollOffset = 158
    REQUIRE(container.getMaxScrollOffset() == Approx(158.0f).margin(0.01f));

    // Initially scrollOffset is 0
    REQUIRE(container.getScrollOffset() == Approx(0.0f).margin(0.01f));

    // Simulate mouse wheel with deltaY = -3.0 (scroll down)
    // Formula: scrollDelta = -deltaY * 20.0 = -(-3.0) * 20.0 = 60.0
    bool changed = container.scrollByWheelDelta(-3.0f);
    REQUIRE(changed);
    REQUIRE(container.getScrollOffset() == Approx(60.0f).margin(0.01f));
}

TEST_CASE("scrollByWheelDelta clamps scrollOffset at maxScrollOffset",
          "[arp_lane_container][scroll][wheel]") {
    // Use a small viewport: content = 172, viewport = 100 -> maxScroll = 72
    auto container = makeContainer(100.0f);

    auto* lane1 = makeArpLane(86.0f);
    auto* lane2 = makeArpLane(86.0f);

    container.addLane(lane1);
    container.addLane(lane2);

    // maxScrollOffset = 172 - 100 = 72
    REQUIRE(container.getMaxScrollOffset() == Approx(72.0f).margin(0.01f));

    // Scroll down by a large amount (deltaY = -10 -> delta = 200px)
    // Should clamp at maxScrollOffset = 72
    bool changed = container.scrollByWheelDelta(-10.0f);
    REQUIRE(changed);
    REQUIRE(container.getScrollOffset() == Approx(72.0f).margin(0.01f));
}

TEST_CASE("scrollByWheelDelta clamps scrollOffset at 0 when scrolling up past top",
          "[arp_lane_container][scroll][wheel]") {
    auto container = makeContainer(100.0f);

    auto* lane1 = makeArpLane(86.0f);
    auto* lane2 = makeArpLane(86.0f);

    container.addLane(lane1);
    container.addLane(lane2);

    // Set initial scroll position to 30
    container.setScrollOffset(30.0f);
    REQUIRE(container.getScrollOffset() == Approx(30.0f).margin(0.01f));

    // Scroll up by a large amount (deltaY = +10 -> delta = -200px)
    // Should clamp at 0
    bool changed = container.scrollByWheelDelta(10.0f);
    REQUIRE(changed);
    REQUIRE(container.getScrollOffset() == Approx(0.0f).margin(0.01f));
}

TEST_CASE("scrollByWheelDelta returns false when no scroll change occurs",
          "[arp_lane_container][scroll][wheel]") {
    // Content fits in viewport -> no scroll possible
    auto container = makeContainer(390.0f);

    auto* lane1 = makeArpLane(86.0f);
    auto* lane2 = makeArpLane(86.0f);

    container.addLane(lane1);
    container.addLane(lane2);

    // maxScrollOffset = 0 (content 172 < viewport 390)
    REQUIRE(container.getMaxScrollOffset() == Approx(0.0f).margin(0.01f));

    // Scrolling should not change offset
    bool changed = container.scrollByWheelDelta(-3.0f);
    REQUIRE_FALSE(changed);
    REQUIRE(container.getScrollOffset() == Approx(0.0f).margin(0.01f));
}

// ==============================================================================
// Mouse Event Routing Through Scroll Offset Tests (T066)
// ==============================================================================

TEST_CASE("Scroll offset translates child lane positions in recalculateLayout",
          "[arp_lane_container][scroll][mouse_routing]") {
    // Use a viewport smaller than content to enable scrolling
    auto container = makeContainer(100.0f);

    auto* lane1 = makeArpLane(80.0f);
    auto* lane2 = makeArpLane(80.0f);

    container.addLane(lane1);
    container.addLane(lane2);

    // Without scroll: lane1 at y=0..80, lane2 at y=80..160
    // Set scrollOffset to 50: lane1 should be at y=-50..30, lane2 at y=30..110
    container.setScrollOffset(50.0f);

    // Verify lane positions reflect scroll offset translation
    CRect lane1Rect = lane1->getViewSize();
    CRect lane2Rect = lane2->getViewSize();

    // lane1: content y=0, visual y = 0 - 50 = -50
    REQUIRE(static_cast<float>(lane1Rect.top) == Approx(-50.0f).margin(0.01f));
    REQUIRE(static_cast<float>(lane1Rect.bottom) == Approx(30.0f).margin(0.01f));

    // lane2: content y=80, visual y = 80 - 50 = 30
    REQUIRE(static_cast<float>(lane2Rect.top) == Approx(30.0f).margin(0.01f));
    REQUIRE(static_cast<float>(lane2Rect.bottom) == Approx(110.0f).margin(0.01f));
}

// ==============================================================================
// IArpLane Interface Tests (080-specialized-lane-types T001)
// ==============================================================================

TEST_CASE("ArpLaneContainer accepts IArpLane* (ArpLaneEditor)", "[arp_lane_container][iarplane]") {
    auto container = makeContainer();

    // Create ArpLaneEditors and pass as IArpLane* (implicit conversion)
    auto* lane1 = makeArpLane(86.0f);
    auto* lane2 = makeArpLane(86.0f);

    IArpLane* iLane1 = lane1;
    IArpLane* iLane2 = lane2;

    container.addLane(iLane1);
    container.addLane(iLane2);

    REQUIRE(container.getLaneCount() == 2);
    REQUIRE(container.getLane(0) == iLane1);
    REQUIRE(container.getLane(1) == iLane2);
}

TEST_CASE("IArpLane collapse callback triggers container relayout", "[arp_lane_container][iarplane]") {
    auto container = makeContainer();

    auto* lane1 = makeArpLane(86.0f);
    auto* lane2 = makeArpLane(86.0f);

    container.addLane(lane1);
    container.addLane(lane2);

    // Both expanded: total = 172
    REQUIRE(container.getTotalContentHeight() == Approx(172.0f).margin(0.01f));

    // Collapse via IArpLane interface
    IArpLane* iLane1 = lane1;
    iLane1->setCollapsed(true);

    // Container should recalculate: collapsed 16 + expanded 86 = 102
    REQUIRE(container.getTotalContentHeight() == Approx(102.0f).margin(0.01f));
}

TEST_CASE("removeLane with IArpLane* works correctly", "[arp_lane_container][iarplane]") {
    auto container = makeContainer();

    auto* lane1 = makeArpLane(86.0f);
    auto* lane2 = makeArpLane(86.0f);

    container.addLane(lane1);
    container.addLane(lane2);
    REQUIRE(container.getLaneCount() == 2);

    IArpLane* iLane1 = lane1;
    container.removeLane(iLane1);
    REQUIRE(container.getLaneCount() == 1);
    REQUIRE(container.getTotalContentHeight() == Approx(86.0f).margin(0.01f));
}

TEST_CASE("getLane returns IArpLane* with correct interface methods", "[arp_lane_container][iarplane]") {
    auto container = makeContainer();

    auto* lane = makeArpLane(86.0f);
    container.addLane(lane);

    IArpLane* retrieved = container.getLane(0);
    REQUIRE(retrieved != nullptr);
    REQUIRE(retrieved->getView() != nullptr);
    REQUIRE(retrieved->getExpandedHeight() == Approx(86.0f).margin(0.01f));
    REQUIRE(retrieved->getCollapsedHeight() == Approx(16.0f).margin(0.01f));
    REQUIRE_FALSE(retrieved->isCollapsed());
}

// ==============================================================================
// Mixed Lane Type Tests (080-specialized-lane-types T064)
// ==============================================================================

TEST_CASE("Container accepts mixed IArpLane types (ArpLaneEditor + ArpModifierLane + ArpConditionLane)",
          "[arp_lane_container][mixed_types][T064]") {
    auto container = makeContainer();

    auto* editorLane = makeArpLane(86.0f);
    auto* modifierLane = new ArpModifierLane(CRect(0, 0, 500, 60), nullptr, -1);
    modifierLane->setNumSteps(8);
    auto* conditionLane = new ArpConditionLane(CRect(0, 0, 500, 44), nullptr, -1);
    conditionLane->setNumSteps(8);

    container.addLane(editorLane);
    container.addLane(modifierLane);
    container.addLane(conditionLane);

    REQUIRE(container.getLaneCount() == 3);
}

TEST_CASE("Container recalculateLayout uses IArpLane interface for mixed types",
          "[arp_lane_container][mixed_types][T064]") {
    auto container = makeContainer();

    auto* editorLane = makeArpLane(86.0f);
    auto* modifierLane = new ArpModifierLane(CRect(0, 0, 500, 60), nullptr, -1);
    modifierLane->setNumSteps(8);
    auto* conditionLane = new ArpConditionLane(CRect(0, 0, 500, 44), nullptr, -1);
    conditionLane->setNumSteps(8);

    container.addLane(editorLane);
    container.addLane(modifierLane);
    container.addLane(conditionLane);

    // 86 + 60 + 44 = 190
    REQUIRE(container.getTotalContentHeight() == Approx(190.0f).margin(0.01f));
}

TEST_CASE("Collapse callback from any mixed lane type triggers relayout",
          "[arp_lane_container][mixed_types][T064]") {
    auto container = makeContainer();

    auto* editorLane = makeArpLane(86.0f);
    auto* modifierLane = new ArpModifierLane(CRect(0, 0, 500, 60), nullptr, -1);
    modifierLane->setNumSteps(8);
    auto* conditionLane = new ArpConditionLane(CRect(0, 0, 500, 44), nullptr, -1);
    conditionLane->setNumSteps(8);

    container.addLane(editorLane);
    container.addLane(modifierLane);
    container.addLane(conditionLane);

    // Collapse modifier lane
    modifierLane->setCollapsed(true);
    // Modifier lane collapses to 16.0f header height
    // Total: 86 + 16 + 44 = 146
    REQUIRE(container.getTotalContentHeight() == Approx(146.0f).margin(0.01f));

    // Collapse condition lane
    conditionLane->setCollapsed(true);
    // Total: 86 + 16 + 16 = 118
    REQUIRE(container.getTotalContentHeight() == Approx(118.0f).margin(0.01f));

    // Expand modifier lane back
    modifierLane->setCollapsed(false);
    // Total: 86 + 60 + 16 = 162
    REQUIRE(container.getTotalContentHeight() == Approx(162.0f).margin(0.01f));
}

TEST_CASE("getLane returns correct IArpLane for each mixed type",
          "[arp_lane_container][mixed_types][T064]") {
    auto container = makeContainer();

    auto* editorLane = makeArpLane(86.0f);
    auto* modifierLane = new ArpModifierLane(CRect(0, 0, 500, 60), nullptr, -1);
    modifierLane->setNumSteps(8);
    auto* conditionLane = new ArpConditionLane(CRect(0, 0, 500, 44), nullptr, -1);
    conditionLane->setNumSteps(8);

    container.addLane(editorLane);
    container.addLane(modifierLane);
    container.addLane(conditionLane);

    IArpLane* lane0 = container.getLane(0);
    IArpLane* lane1 = container.getLane(1);
    IArpLane* lane2 = container.getLane(2);

    REQUIRE(lane0 != nullptr);
    REQUIRE(lane1 != nullptr);
    REQUIRE(lane2 != nullptr);

    // Verify expanded heights correspond to the specific lane types
    REQUIRE(lane0->getExpandedHeight() == Approx(86.0f).margin(0.01f));
    REQUIRE(lane1->getExpandedHeight() == Approx(60.0f).margin(0.01f));
    REQUIRE(lane2->getExpandedHeight() == Approx(44.0f).margin(0.01f));

    // Verify getView returns a non-null CView for each
    REQUIRE(lane0->getView() != nullptr);
    REQUIRE(lane1->getView() != nullptr);
    REQUIRE(lane2->getView() != nullptr);
}

// ==============================================================================
// Collapse/Expand Integration Cycle Tests (T074)
// ==============================================================================

TEST_CASE("Full collapse/expand cycle: ArpModifierLane collapses to 16px, expands to 60px",
          "[arp_lane_container][collapse_cycle][T074]") {
    auto container = makeContainer();

    auto* editorLane = makeArpLane(86.0f);
    auto* modifierLane = new ArpModifierLane(CRect(0, 0, 500, 60), nullptr, -1);
    modifierLane->setNumSteps(8);

    container.addLane(editorLane);
    container.addLane(modifierLane);

    // Initial state: both expanded -> 86 + 60 = 146
    REQUIRE(container.getTotalContentHeight() == Approx(146.0f).margin(0.01f));

    // Collapse modifier lane
    modifierLane->setCollapsed(true);
    REQUIRE(modifierLane->isCollapsed());
    // Modifier height = 16.0f (collapsed), editor = 86 -> total = 102
    REQUIRE(container.getTotalContentHeight() == Approx(102.0f).margin(0.01f));

    // Expand modifier lane back
    modifierLane->setCollapsed(false);
    REQUIRE_FALSE(modifierLane->isCollapsed());
    // Modifier height restored to 60.0f -> total = 146
    REQUIRE(container.getTotalContentHeight() == Approx(146.0f).margin(0.01f));
}

TEST_CASE("Full collapse/expand cycle: ArpConditionLane collapses to 16px, expands to 44px",
          "[arp_lane_container][collapse_cycle][T074]") {
    auto container = makeContainer();

    auto* editorLane = makeArpLane(86.0f);
    auto* conditionLane = new ArpConditionLane(CRect(0, 0, 500, 44), nullptr, -1);
    conditionLane->setNumSteps(8);

    container.addLane(editorLane);
    container.addLane(conditionLane);

    // Initial state: both expanded -> 86 + 44 = 130
    REQUIRE(container.getTotalContentHeight() == Approx(130.0f).margin(0.01f));

    // Collapse condition lane
    conditionLane->setCollapsed(true);
    REQUIRE(conditionLane->isCollapsed());
    // Condition height = 16.0f (collapsed), editor = 86 -> total = 102
    REQUIRE(container.getTotalContentHeight() == Approx(102.0f).margin(0.01f));

    // Expand condition lane back
    conditionLane->setCollapsed(false);
    REQUIRE_FALSE(conditionLane->isCollapsed());
    // Condition height restored to 44.0f -> total = 130
    REQUIRE(container.getTotalContentHeight() == Approx(130.0f).margin(0.01f));
}

TEST_CASE("Collapse all new lane types individually, container recalculates each time",
          "[arp_lane_container][collapse_cycle][T074]") {
    auto container = makeContainer();

    auto* modifierLane = new ArpModifierLane(CRect(0, 0, 500, 60), nullptr, -1);
    modifierLane->setNumSteps(8);
    auto* conditionLane = new ArpConditionLane(CRect(0, 0, 500, 44), nullptr, -1);
    conditionLane->setNumSteps(8);

    container.addLane(modifierLane);
    container.addLane(conditionLane);

    // Both expanded: 60 + 44 = 104
    REQUIRE(container.getTotalContentHeight() == Approx(104.0f).margin(0.01f));

    // Collapse modifier: 16 + 44 = 60
    modifierLane->setCollapsed(true);
    REQUIRE(container.getTotalContentHeight() == Approx(60.0f).margin(0.01f));

    // Collapse condition: 16 + 16 = 32
    conditionLane->setCollapsed(true);
    REQUIRE(container.getTotalContentHeight() == Approx(32.0f).margin(0.01f));

    // Expand modifier: 60 + 16 = 76
    modifierLane->setCollapsed(false);
    REQUIRE(container.getTotalContentHeight() == Approx(76.0f).margin(0.01f));

    // Expand condition: 60 + 44 = 104
    conditionLane->setCollapsed(false);
    REQUIRE(container.getTotalContentHeight() == Approx(104.0f).margin(0.01f));
}

// ==============================================================================
// Cross-Lane Alignment Verification Tests (080-specialized-lane-types T087, T088)
// ==============================================================================

TEST_CASE("T087: ArpModifierLane and ArpLaneEditor step 0 content x-origin are equal (FR-049)",
          "[arp_lane_container][alignment][T087][FR-049]") {
    // Construct ArpModifierLane and ArpLaneEditor at the same width and step count
    auto* modLane = new ArpModifierLane(CRect(0, 0, 500, 60), nullptr, -1);
    modLane->setNumSteps(8);
    auto* pitchLane = new ArpLaneEditor(CRect(0, 0, 500, 200), nullptr, -1);
    pitchLane->setLaneType(ArpLaneType::kPitch);
    pitchLane->setNumSteps(8);

    // ArpLaneEditor step content origin = barArea.left = vs.left + barAreaLeftOffset_
    CRect barArea = pitchLane->getBarArea();
    float pitchContentLeft = static_cast<float>(barArea.left);

    // ArpModifierLane step content origin = vs.left + kLeftMargin
    float modContentLeft = static_cast<float>(modLane->getViewSize().left) +
        ArpModifierLane::kLeftMargin;

    REQUIRE(pitchContentLeft == Approx(modContentLeft).margin(0.01f));

    // Also verify the step widths are equal: both should divide
    // (viewWidth - leftMargin) by numSteps
    float pitchStepWidth = static_cast<float>(barArea.getWidth()) / 8.0f;
    float modContentWidth = static_cast<float>(modLane->getViewSize().getWidth()) -
        ArpModifierLane::kLeftMargin;
    float modStepWidth = modContentWidth / 8.0f;

    REQUIRE(pitchStepWidth == Approx(modStepWidth).margin(0.01f));

    modLane->forget();
    delete pitchLane;
}

TEST_CASE("T088: All lane types have equal step origins and widths at same step count (FR-050)",
          "[arp_lane_container][alignment][T088][FR-050]") {
    // Construct all three lane types at the same width (500px) and step count (8)
    auto* pitchLane = new ArpLaneEditor(CRect(0, 0, 500, 200), nullptr, -1);
    pitchLane->setLaneType(ArpLaneType::kPitch);
    pitchLane->setNumSteps(8);

    auto* modLane = new ArpModifierLane(CRect(0, 0, 500, 60), nullptr, -1);
    modLane->setNumSteps(8);

    auto* condLane = new ArpConditionLane(CRect(0, 0, 500, 44), nullptr, -1);
    condLane->setNumSteps(8);

    // --- Compute step content left origin for each lane type ---

    // ArpLaneEditor (pitch): barArea.left = vs.left + barAreaLeftOffset_
    CRect barArea = pitchLane->getBarArea();
    float pitchOriginX = static_cast<float>(barArea.left);
    float pitchStepWidth = static_cast<float>(barArea.getWidth()) / 8.0f;

    // ArpModifierLane: contentLeft = vs.left + kLeftMargin
    float modOriginX = static_cast<float>(modLane->getViewSize().left) +
        ArpModifierLane::kLeftMargin;
    float modContentWidth = static_cast<float>(modLane->getViewSize().getWidth()) -
        ArpModifierLane::kLeftMargin;
    float modStepWidth = modContentWidth / 8.0f;

    // ArpConditionLane: contentLeft = vs.left + kLeftMargin
    float condOriginX = static_cast<float>(condLane->getViewSize().left) +
        ArpConditionLane::kLeftMargin;
    float condContentWidth = static_cast<float>(condLane->getViewSize().getWidth()) -
        ArpConditionLane::kLeftMargin;
    float condStepWidth = condContentWidth / 8.0f;

    // --- All three step origins MUST be equal ---
    REQUIRE(pitchOriginX == Approx(modOriginX).margin(0.01f));
    REQUIRE(pitchOriginX == Approx(condOriginX).margin(0.01f));

    // --- All three step widths MUST be equal ---
    REQUIRE(pitchStepWidth == Approx(modStepWidth).margin(0.01f));
    REQUIRE(pitchStepWidth == Approx(condStepWidth).margin(0.01f));

    // --- Verify specific step boundary positions match ---
    // Step 3 left-edge: originX + 3 * stepWidth
    float pitchStep3 = pitchOriginX + 3.0f * pitchStepWidth;
    float modStep3 = modOriginX + 3.0f * modStepWidth;
    float condStep3 = condOriginX + 3.0f * condStepWidth;
    REQUIRE(pitchStep3 == Approx(modStep3).margin(0.01f));
    REQUIRE(pitchStep3 == Approx(condStep3).margin(0.01f));

    modLane->forget();
    condLane->forget();
    delete pitchLane;
}
