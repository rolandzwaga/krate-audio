// ==============================================================================
// ArpLaneContainer Tests (079-layout-framework)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/arp_lane_container.h"

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
