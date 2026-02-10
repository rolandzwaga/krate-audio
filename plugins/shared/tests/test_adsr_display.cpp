// ==============================================================================
// ADSRDisplay Coordinate Conversion and Hit Testing Tests
// ==============================================================================
// T013: Coordinate conversion tests
// T014: Control point hit testing tests
//
// These tests MUST be written and FAIL before implementation begins
// (Constitution Principle XII: Test-First Development).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/adsr_display.h"

using namespace Krate::Plugins;
using namespace VSTGUI;
using Catch::Approx;

// Helper: create a default 140x90 ADSRDisplay (matching editor.uidesc dimensions)
static ADSRDisplay makeDisplay() {
    return ADSRDisplay(CRect(0, 0, 140, 90), nullptr, -1);
}

// Helper: create display with specific ADSR values
static ADSRDisplay makeDisplayWithValues(float attackMs, float decayMs,
                                          float sustainLevel, float releaseMs) {
    ADSRDisplay display(CRect(0, 0, 140, 90), nullptr, -1);
    display.setAttackMs(attackMs);
    display.setDecayMs(decayMs);
    display.setSustainLevel(sustainLevel);
    display.setReleaseMs(releaseMs);
    return display;
}

// =============================================================================
// T013: Coordinate Conversion Tests
// =============================================================================

TEST_CASE("recalculateLayout produces valid segment positions", "[adsr_display][coord]") {
    auto display = makeDisplayWithValues(10.0f, 50.0f, 0.5f, 100.0f);
    auto layout = display.getLayout();

    // Attack starts at left edge
    REQUIRE(layout.attackStartX >= 0.0f);
    // Segments are in order
    REQUIRE(layout.attackEndX > layout.attackStartX);
    REQUIRE(layout.decayEndX > layout.attackEndX);
    REQUIRE(layout.sustainEndX > layout.decayEndX);
    REQUIRE(layout.releaseEndX > layout.sustainEndX);
    // Top is above bottom
    REQUIRE(layout.topY < layout.bottomY);
}

TEST_CASE("Segment positions respect 15% minimum width", "[adsr_display][coord]") {
    // Extreme ratio: very short attack, very long release
    auto display = makeDisplayWithValues(0.1f, 0.1f, 0.5f, 10000.0f);
    auto layout = display.getLayout();

    float totalWidth = layout.releaseEndX - layout.attackStartX;
    float minSegWidth = totalWidth * 0.15f;

    // Each time segment should have at least 15% of total width
    float attackWidth = layout.attackEndX - layout.attackStartX;
    float decayWidth = layout.decayEndX - layout.attackEndX;
    // Sustain is fixed 25%, so skip its minimum check
    float releaseWidth = layout.releaseEndX - layout.sustainEndX;

    // Attack and decay should have at least near the minimum
    // (exact enforcement depends on implementation, but must be visible)
    REQUIRE(attackWidth >= minSegWidth * 0.9f);
    REQUIRE(decayWidth >= minSegWidth * 0.9f);
    REQUIRE(releaseWidth >= minSegWidth * 0.9f);
}

TEST_CASE("Level 1.0 maps to topY, level 0.0 maps to bottomY", "[adsr_display][coord]") {
    auto display = makeDisplay();
    auto layout = display.getLayout();

    float topPixel = display.levelToPixelY(1.0f);
    float bottomPixel = display.levelToPixelY(0.0f);

    REQUIRE(topPixel == Approx(layout.topY).margin(1.0f));
    REQUIRE(bottomPixel == Approx(layout.bottomY).margin(1.0f));
}

TEST_CASE("pixelYToLevel inverse of levelToPixelY", "[adsr_display][coord]") {
    auto display = makeDisplay();

    // Test several levels
    for (float level : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        float pixelY = display.levelToPixelY(level);
        float recovered = display.pixelYToLevel(pixelY);
        REQUIRE(recovered == Approx(level).margin(0.01f));
    }
}

TEST_CASE("Peak point X corresponds to attack time", "[adsr_display][coord]") {
    auto display = makeDisplayWithValues(10.0f, 50.0f, 0.5f, 100.0f);
    auto layout = display.getLayout();

    // Peak point is at the end of attack segment, top of display
    auto peakPoint = display.getControlPointPosition(ADSRDisplay::DragTarget::PeakPoint);
    REQUIRE(peakPoint.x == Approx(layout.attackEndX).margin(1.0f));
    REQUIRE(peakPoint.y == Approx(layout.topY).margin(1.0f));
}

TEST_CASE("Sustain point position reflects decay time and sustain level", "[adsr_display][coord]") {
    auto display = makeDisplayWithValues(10.0f, 50.0f, 0.7f, 100.0f);
    auto layout = display.getLayout();

    auto sustainPoint = display.getControlPointPosition(ADSRDisplay::DragTarget::SustainPoint);
    REQUIRE(sustainPoint.x == Approx(layout.decayEndX).margin(1.0f));

    float expectedY = display.levelToPixelY(0.7f);
    REQUIRE(sustainPoint.y == Approx(expectedY).margin(1.0f));
}

TEST_CASE("End point position reflects release time", "[adsr_display][coord]") {
    auto display = makeDisplayWithValues(10.0f, 50.0f, 0.5f, 100.0f);
    auto layout = display.getLayout();

    auto endPoint = display.getControlPointPosition(ADSRDisplay::DragTarget::EndPoint);
    REQUIRE(endPoint.x == Approx(layout.releaseEndX).margin(1.0f));
    REQUIRE(endPoint.y == Approx(layout.bottomY).margin(1.0f));
}

TEST_CASE("Coordinate round-trip accuracy within 0.01 tolerance (SC-012)", "[adsr_display][coord][roundtrip]") {
    auto display = makeDisplayWithValues(100.0f, 200.0f, 0.6f, 300.0f);

    // Round-trip: set attack -> get layout -> attackEndX -> convert back to time
    // The pixel positions encode the parameter values; converting back should be close
    auto layout = display.getLayout();

    // Sustain level round-trip via pixel
    float sustainPixelY = display.levelToPixelY(0.6f);
    float recoveredLevel = display.pixelYToLevel(sustainPixelY);
    REQUIRE(recoveredLevel == Approx(0.6f).margin(0.01f));
}

// =============================================================================
// T014: Control Point Hit Testing Tests
// =============================================================================

TEST_CASE("Hit test detects Peak point within 12px radius", "[adsr_display][hittest]") {
    auto display = makeDisplayWithValues(50.0f, 100.0f, 0.5f, 200.0f);
    auto peakPos = display.getControlPointPosition(ADSRDisplay::DragTarget::PeakPoint);

    // Test point right on the peak
    CPoint onPeak(peakPos.x, peakPos.y);
    REQUIRE(display.hitTest(onPeak) == ADSRDisplay::DragTarget::PeakPoint);

    // Test point 10px away (within 12px radius)
    CPoint nearPeak(peakPos.x + 10, peakPos.y);
    REQUIRE(display.hitTest(nearPeak) == ADSRDisplay::DragTarget::PeakPoint);

    // Test point 15px away (outside 12px radius)
    CPoint farFromPeak(peakPos.x + 15, peakPos.y);
    REQUIRE(display.hitTest(farFromPeak) != ADSRDisplay::DragTarget::PeakPoint);
}

TEST_CASE("Hit test detects Sustain point within 12px radius", "[adsr_display][hittest]") {
    auto display = makeDisplayWithValues(50.0f, 100.0f, 0.5f, 200.0f);
    auto sustainPos = display.getControlPointPosition(ADSRDisplay::DragTarget::SustainPoint);

    CPoint onSustain(sustainPos.x, sustainPos.y);
    REQUIRE(display.hitTest(onSustain) == ADSRDisplay::DragTarget::SustainPoint);
}

TEST_CASE("Hit test detects End point within 12px radius", "[adsr_display][hittest]") {
    auto display = makeDisplayWithValues(50.0f, 100.0f, 0.5f, 200.0f);
    auto endPos = display.getControlPointPosition(ADSRDisplay::DragTarget::EndPoint);

    CPoint onEnd(endPos.x, endPos.y);
    REQUIRE(display.hitTest(onEnd) == ADSRDisplay::DragTarget::EndPoint);
}

TEST_CASE("Hit test returns None for empty area", "[adsr_display][hittest]") {
    auto display = makeDisplayWithValues(50.0f, 100.0f, 0.5f, 200.0f);

    // Point far from any control point (top-left corner)
    CPoint nowhere(0, 0);
    REQUIRE(display.hitTest(nowhere) == ADSRDisplay::DragTarget::None);
}

TEST_CASE("Control points take priority over curve segments", "[adsr_display][hittest]") {
    auto display = makeDisplayWithValues(50.0f, 100.0f, 0.5f, 200.0f);
    auto peakPos = display.getControlPointPosition(ADSRDisplay::DragTarget::PeakPoint);

    // Point exactly on Peak (which is also on the attack curve endpoint)
    CPoint onPeak(peakPos.x, peakPos.y);
    auto target = display.hitTest(onPeak);

    // Control points have priority over curves
    REQUIRE(target == ADSRDisplay::DragTarget::PeakPoint);
}
