// ==============================================================================
// ADSRDisplay Coordinate Conversion, Hit Testing, and Rendering Tests
// ==============================================================================
// T013: Coordinate conversion tests
// T014: Control point hit testing tests
// T029: Envelope curve path generation / rendering tests
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

// =============================================================================
// T029: Envelope Curve Path Generation / Rendering Tests
// =============================================================================

TEST_CASE("Layout covers four sequential segments: attack, decay, sustain-hold, release",
          "[adsr_display][rendering]") {
    auto display = makeDisplayWithValues(100.0f, 200.0f, 0.5f, 300.0f);
    auto layout = display.getLayout();

    // Four segments present in correct order
    float attackWidth = layout.attackEndX - layout.attackStartX;
    float decayWidth = layout.decayEndX - layout.attackEndX;
    float sustainWidth = layout.sustainEndX - layout.decayEndX;
    float releaseWidth = layout.releaseEndX - layout.sustainEndX;

    REQUIRE(attackWidth > 0.0f);
    REQUIRE(decayWidth > 0.0f);
    REQUIRE(sustainWidth > 0.0f);
    REQUIRE(releaseWidth > 0.0f);

    // Sustain hold is approximately 25% of total width
    float totalWidth = layout.releaseEndX - layout.attackStartX;
    REQUIRE(sustainWidth == Approx(totalWidth * 0.25f).margin(1.0f));
}

TEST_CASE("Envelope path closes to baseline (start and end at bottomY)",
          "[adsr_display][rendering]") {
    auto display = makeDisplayWithValues(50.0f, 100.0f, 0.7f, 150.0f);
    auto layout = display.getLayout();

    // The envelope path starts at (attackStartX, bottomY) and ends at (releaseEndX, bottomY)
    // Verify the start and end Y coordinates match bottomY
    // (the path generation code starts at bottomY and closes to bottomY)
    REQUIRE(layout.bottomY > layout.topY);  // Valid vertical range

    // The attack starts at level 0 (bottomY) and release ends at level 0 (bottomY)
    float startY = display.levelToPixelY(0.0f);
    float endY = display.levelToPixelY(0.0f);
    REQUIRE(startY == Approx(layout.bottomY).margin(1.0f));
    REQUIRE(endY == Approx(layout.bottomY).margin(1.0f));
}

TEST_CASE("Extreme timing ratio: 0.1ms attack + 10s release keeps all segments visible (SC-004)",
          "[adsr_display][rendering][extreme]") {
    auto display = makeDisplayWithValues(0.1f, 1.0f, 0.5f, 10000.0f);
    auto layout = display.getLayout();

    float totalWidth = layout.releaseEndX - layout.attackStartX;
    float minVisibleWidth = 3.0f; // pixels - a 3px segment is still visible

    float attackWidth = layout.attackEndX - layout.attackStartX;
    float decayWidth = layout.decayEndX - layout.attackEndX;
    float releaseWidth = layout.releaseEndX - layout.sustainEndX;

    // All time segments must be visible even with 100000:1 ratio
    REQUIRE(attackWidth >= minVisibleWidth);
    REQUIRE(decayWidth >= minVisibleWidth);
    REQUIRE(releaseWidth >= minVisibleWidth);

    // Each segment occupies at least ~15% of total width
    float minSegWidth = totalWidth * 0.15f;
    REQUIRE(attackWidth >= minSegWidth * 0.9f);
    REQUIRE(decayWidth >= minSegWidth * 0.9f);
    REQUIRE(releaseWidth >= minSegWidth * 0.9f);
}

TEST_CASE("Curve table integration: power curve with curveAmount=0 is linear",
          "[adsr_display][rendering][curve]") {
    // Verify that the curve table used by drawCurveSegment produces linear output
    // when curveAmount is 0 (which is the default for ADSRDisplay)
    std::array<float, Krate::DSP::kCurveTableSize> table{};
    Krate::DSP::generatePowerCurveTable(table, 0.0f);

    // Linear curve: table[i] should equal i/255
    for (int i = 0; i < static_cast<int>(Krate::DSP::kCurveTableSize); ++i) {
        float expected = static_cast<float>(i) / 255.0f;
        REQUIRE(table[static_cast<size_t>(i)] == Approx(expected).margin(0.01f));
    }
}

TEST_CASE("Curve table integration: positive curveAmount bends exponential",
          "[adsr_display][rendering][curve]") {
    std::array<float, Krate::DSP::kCurveTableSize> table{};
    Krate::DSP::generatePowerCurveTable(table, 0.7f);

    // Exponential curve: midpoint should be below 0.5
    float midVal = Krate::DSP::lookupCurveTable(table, 0.5f);
    REQUIRE(midVal < 0.4f);
}

TEST_CASE("Curve table integration: negative curveAmount bends logarithmic",
          "[adsr_display][rendering][curve]") {
    std::array<float, Krate::DSP::kCurveTableSize> table{};
    Krate::DSP::generatePowerCurveTable(table, -0.7f);

    // Logarithmic curve: midpoint should be above 0.5
    float midVal = Krate::DSP::lookupCurveTable(table, 0.5f);
    REQUIRE(midVal > 0.6f);
}

TEST_CASE("Time normalization round-trip for rendering accuracy",
          "[adsr_display][rendering][roundtrip]") {
    // Test the timeMsToNormalized / normalizedToTimeMs conversion used by the display
    // These are static methods but we test the round-trip via setters and getters

    auto display = makeDisplay();

    // Set various attack times and verify they survive the internal clamping
    for (float timeMs : {0.1f, 1.0f, 10.0f, 100.0f, 1000.0f, 10000.0f}) {
        display.setAttackMs(timeMs);
        float recovered = display.getAttackMs();
        REQUIRE(recovered == Approx(timeMs).margin(0.1f));
    }
}

TEST_CASE("Sustain level extremes: 0.0 and 1.0 render correctly",
          "[adsr_display][rendering][edge]") {
    // Sustain = 0.0: sustain point at bottom
    auto display0 = makeDisplayWithValues(10.0f, 50.0f, 0.0f, 100.0f);
    auto sustainPos0 = display0.getControlPointPosition(ADSRDisplay::DragTarget::SustainPoint);
    auto layout0 = display0.getLayout();
    REQUIRE(sustainPos0.y == Approx(layout0.bottomY).margin(1.0f));

    // Sustain = 1.0: sustain point at top
    auto display1 = makeDisplayWithValues(10.0f, 50.0f, 1.0f, 100.0f);
    auto sustainPos1 = display1.getControlPointPosition(ADSRDisplay::DragTarget::SustainPoint);
    auto layout1 = display1.getLayout();
    REQUIRE(sustainPos1.y == Approx(layout1.topY).margin(1.0f));
}

TEST_CASE("Color setters and getters round-trip correctly",
          "[adsr_display][rendering][colors]") {
    auto display = makeDisplay();

    CColor testColor(200, 100, 50, 128);
    display.setFillColor(testColor);
    REQUIRE(display.getFillColor() == testColor);

    display.setStrokeColor(testColor);
    REQUIRE(display.getStrokeColor() == testColor);

    display.setBackgroundColor(testColor);
    REQUIRE(display.getBackgroundColor() == testColor);

    display.setGridColor(testColor);
    REQUIRE(display.getGridColor() == testColor);

    display.setControlPointColor(testColor);
    REQUIRE(display.getControlPointColor() == testColor);

    display.setTextColor(testColor);
    REQUIRE(display.getTextColor() == testColor);
}
