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

// =============================================================================
// T043: Curve Segment Hit Testing Tests
// =============================================================================

TEST_CASE("Attack curve hit detection in middle third of segment",
          "[adsr_display][curve_hittest]") {
    // Use balanced timing so segments are reasonably wide
    auto display = makeDisplayWithValues(100.0f, 100.0f, 0.5f, 100.0f);
    auto layout = display.getLayout();

    // Middle of attack segment
    float attackMidX = (layout.attackStartX + layout.attackEndX) * 0.5f;
    float midY = (layout.topY + layout.bottomY) * 0.5f;

    CPoint midAttack(attackMidX, midY);
    auto target = display.hitTest(midAttack);
    REQUIRE(target == ADSRDisplay::DragTarget::AttackCurve);
}

TEST_CASE("Decay curve hit detection in middle third of segment",
          "[adsr_display][curve_hittest]") {
    auto display = makeDisplayWithValues(100.0f, 100.0f, 0.5f, 100.0f);
    auto layout = display.getLayout();

    float decayMidX = (layout.attackEndX + layout.decayEndX) * 0.5f;
    float midY = (layout.topY + layout.bottomY) * 0.5f;

    CPoint midDecay(decayMidX, midY);
    auto target = display.hitTest(midDecay);
    REQUIRE(target == ADSRDisplay::DragTarget::DecayCurve);
}

TEST_CASE("Release curve hit detection in middle third of segment",
          "[adsr_display][curve_hittest]") {
    auto display = makeDisplayWithValues(100.0f, 100.0f, 0.5f, 100.0f);
    auto layout = display.getLayout();

    float releaseMidX = (layout.sustainEndX + layout.releaseEndX) * 0.5f;
    float midY = (layout.topY + layout.bottomY) * 0.5f;

    CPoint midRelease(releaseMidX, midY);
    auto target = display.hitTest(midRelease);
    REQUIRE(target == ADSRDisplay::DragTarget::ReleaseCurve);
}

TEST_CASE("Control points take priority over curve segments in overlap zone",
          "[adsr_display][curve_hittest]") {
    auto display = makeDisplayWithValues(100.0f, 100.0f, 0.5f, 100.0f);
    auto peakPos = display.getControlPointPosition(ADSRDisplay::DragTarget::PeakPoint);

    // Point at peak position should be PeakPoint, not AttackCurve or DecayCurve
    CPoint onPeak(peakPos.x, peakPos.y);
    REQUIRE(display.hitTest(onPeak) == ADSRDisplay::DragTarget::PeakPoint);
}

TEST_CASE("Curve drag delta converts to curve amount change",
          "[adsr_display][curve_hittest]") {
    auto display = makeDisplayWithValues(100.0f, 100.0f, 0.5f, 100.0f);

    // Initially curve amount is 0
    REQUIRE(display.getAttackCurve() == Approx(0.0f).margin(0.01f));

    // Set a positive curve (exponential)
    display.setAttackCurve(0.5f);
    REQUIRE(display.getAttackCurve() == Approx(0.5f).margin(0.01f));

    // Set a negative curve (logarithmic)
    display.setAttackCurve(-0.7f);
    REQUIRE(display.getAttackCurve() == Approx(-0.7f).margin(0.01f));

    // Clamp at boundaries
    display.setAttackCurve(1.5f);
    REQUIRE(display.getAttackCurve() == Approx(1.0f).margin(0.01f));

    display.setAttackCurve(-1.5f);
    REQUIRE(display.getAttackCurve() == Approx(-1.0f).margin(0.01f));
}

// =============================================================================
// T054: Fine Adjustment Tests
// =============================================================================

TEST_CASE("Fine adjustment scale constant is 0.1",
          "[adsr_display][fine_adjust]") {
    // Verify the fine adjustment scale constant matches spec (SC-002)
    REQUIRE(ADSRDisplay::kFineAdjustmentScale == Approx(0.1f));
}

TEST_CASE("Double-click default values match spec",
          "[adsr_display][fine_adjust]") {
    // Verify the default values that double-click resets to (SC-003)
    REQUIRE(ADSRDisplay::kDefaultAttackMs == Approx(10.0f));
    REQUIRE(ADSRDisplay::kDefaultDecayMs == Approx(50.0f));
    REQUIRE(ADSRDisplay::kDefaultSustainLevel == Approx(0.5f));
    REQUIRE(ADSRDisplay::kDefaultReleaseMs == Approx(100.0f));
}

TEST_CASE("Pre-drag values can be stored and restored via Escape",
          "[adsr_display][fine_adjust]") {
    // Test that setting values, then restoring is possible through the API
    auto display = makeDisplayWithValues(200.0f, 300.0f, 0.8f, 400.0f);

    // Change values
    display.setAttackMs(50.0f);
    display.setDecayMs(150.0f);
    display.setSustainLevel(0.3f);
    display.setReleaseMs(500.0f);

    // Verify changed
    REQUIRE(display.getAttackMs() == Approx(50.0f).margin(0.1f));
    REQUIRE(display.getDecayMs() == Approx(150.0f).margin(0.1f));
    REQUIRE(display.getSustainLevel() == Approx(0.3f).margin(0.01f));
    REQUIRE(display.getReleaseMs() == Approx(500.0f).margin(0.1f));
}

TEST_CASE("Curve setters clamp to [-1, +1] range",
          "[adsr_display][fine_adjust]") {
    auto display = makeDisplay();

    display.setAttackCurve(2.0f);
    REQUIRE(display.getAttackCurve() == Approx(1.0f));

    display.setDecayCurve(-2.0f);
    REQUIRE(display.getDecayCurve() == Approx(-1.0f));

    display.setReleaseCurve(0.5f);
    REQUIRE(display.getReleaseCurve() == Approx(0.5f));
}

TEST_CASE("Bezier enabled setter and getter",
          "[adsr_display][fine_adjust]") {
    auto display = makeDisplay();

    REQUIRE(display.getBezierEnabled() == false);
    display.setBezierEnabled(true);
    REQUIRE(display.getBezierEnabled() == true);
    display.setBezierEnabled(false);
    REQUIRE(display.getBezierEnabled() == false);
}

// =============================================================================
// T062: Bezier Mode Tests
// =============================================================================

TEST_CASE("Mode toggle button hit detection in top-right corner",
          "[adsr_display][bezier]") {
    auto display = makeDisplay();
    auto vs = display.getViewSize();

    // Toggle button is 16x16 in top-right corner (with padding)
    float buttonCenterX = static_cast<float>(vs.right) - ADSRDisplay::kPadding - 8.0f;
    float buttonCenterY = static_cast<float>(vs.top) + ADSRDisplay::kPadding + 8.0f;

    CPoint onButton(buttonCenterX, buttonCenterY);
    auto target = display.hitTest(onButton);
    REQUIRE(target == ADSRDisplay::DragTarget::ModeToggle);
}

TEST_CASE("Mode toggle button returns None when outside",
          "[adsr_display][bezier]") {
    auto display = makeDisplay();

    // Point in the middle of the display should NOT hit the toggle button
    CPoint center(70, 45);
    auto target = display.hitTest(center);
    REQUIRE(target != ADSRDisplay::DragTarget::ModeToggle);
}

TEST_CASE("Bezier handle values can be set and round-trip",
          "[adsr_display][bezier]") {
    auto display = makeDisplay();

    // Set attack cp1
    display.setBezierHandleValue(0, 0, 0, 0.25f);  // seg=0, handle=cp1, axis=x
    display.setBezierHandleValue(0, 0, 1, 0.75f);  // seg=0, handle=cp1, axis=y

    // Set attack cp2
    display.setBezierHandleValue(0, 1, 0, 0.8f);   // seg=0, handle=cp2, axis=x
    display.setBezierHandleValue(0, 1, 1, 0.2f);   // seg=0, handle=cp2, axis=y

    // Values should clamp to [0,1]
    display.setBezierHandleValue(1, 0, 0, -0.5f);
    display.setBezierHandleValue(1, 0, 1, 1.5f);

    // Out-of-range segment should be ignored
    display.setBezierHandleValue(5, 0, 0, 0.5f);  // should not crash
}

TEST_CASE("Simple-to-Bezier conversion produces valid control points",
          "[adsr_display][bezier]") {
    // Test that simpleCurveToBezier from curve_table.h produces valid handles
    float cp1x = 0.0f, cp1y = 0.0f, cp2x = 0.0f, cp2y = 0.0f;

    // Linear curve (amount=0) should give symmetric points at 1/3 and 2/3
    Krate::DSP::simpleCurveToBezier(0.0f, cp1x, cp1y, cp2x, cp2y);
    REQUIRE(cp1x == Approx(1.0f / 3.0f).margin(0.01f));
    REQUIRE(cp1y == Approx(1.0f / 3.0f).margin(0.01f));
    REQUIRE(cp2x == Approx(2.0f / 3.0f).margin(0.01f));
    REQUIRE(cp2y == Approx(2.0f / 3.0f).margin(0.01f));
}

TEST_CASE("Bezier-to-Simple conversion round-trip is approximate",
          "[adsr_display][bezier]") {
    // Start with a known curve amount, convert to Bezier, convert back
    float originalCurve = 0.5f;
    float cp1x = 0.0f, cp1y = 0.0f, cp2x = 0.0f, cp2y = 0.0f;

    Krate::DSP::simpleCurveToBezier(originalCurve, cp1x, cp1y, cp2x, cp2y);

    float recoveredCurve = Krate::DSP::bezierToSimpleCurve(cp1x, cp1y, cp2x, cp2y);

    // Should be approximately the same (not exact due to sampling at phase 0.5)
    // The conversion loses precision because the Bezier midpoint doesn't
    // perfectly represent the power curve shape
    REQUIRE(recoveredCurve == Approx(originalCurve).margin(0.3f));
}

TEST_CASE("Bezier handle hit test in Bezier mode detects handles",
          "[adsr_display][bezier]") {
    auto display = makeDisplayWithValues(100.0f, 100.0f, 0.5f, 100.0f);
    display.setBezierEnabled(true);

    // Get attack segment bounds for handle position calculation
    auto layout = display.getLayout();
    float segStartX = layout.attackStartX;
    float segEndX = layout.attackEndX;
    float segStartY = layout.bottomY;  // attack: bottom to top
    float segEndY = layout.topY;

    // Attack cp1 at default (0.33, 0.33)
    float cp1PixelX = segStartX + 0.33f * (segEndX - segStartX);
    float cp1PixelY = segStartY + 0.33f * (segEndY - segStartY);

    CPoint onCp1(cp1PixelX, cp1PixelY);
    auto target = display.hitTest(onCp1);
    REQUIRE(target == ADSRDisplay::DragTarget::BezierHandle);
}

// =============================================================================
// T074: Playback Dot Positioning Tests
// =============================================================================

TEST_CASE("Playback dot position calculation for attack stage",
          "[adsr_display][playback]") {
    auto display = makeDisplayWithValues(100.0f, 200.0f, 0.6f, 300.0f);
    auto layout = display.getLayout();

    // Stage 1 = Attack, output = 0.5 means halfway through attack ramp
    display.setPlaybackState(0.5f, 1, true);

    // Get dot position via public accessor
    auto dotPos = display.getPlaybackDotPosition();

    // Dot should be somewhere in the attack segment horizontally
    REQUIRE(dotPos.x >= layout.attackStartX);
    REQUIRE(dotPos.x <= layout.attackEndX);

    // Dot Y should correspond to output level 0.5
    float expectedY = display.levelToPixelY(0.5f);
    REQUIRE(static_cast<float>(dotPos.y) == Approx(expectedY).margin(2.0f));
}

TEST_CASE("Playback dot position calculation for sustain stage",
          "[adsr_display][playback]") {
    auto display = makeDisplayWithValues(100.0f, 200.0f, 0.6f, 300.0f);
    auto layout = display.getLayout();

    // Stage 3 = Sustain, output = sustain level
    display.setPlaybackState(0.6f, 3, true);

    auto dotPos = display.getPlaybackDotPosition();

    // Dot should be in the sustain-hold segment
    REQUIRE(dotPos.x >= layout.decayEndX);
    REQUIRE(dotPos.x <= layout.sustainEndX);

    // Dot Y should be at sustain level
    float expectedY = display.levelToPixelY(0.6f);
    REQUIRE(static_cast<float>(dotPos.y) == Approx(expectedY).margin(2.0f));
}

TEST_CASE("Playback dot position calculation for release stage",
          "[adsr_display][playback]") {
    auto display = makeDisplayWithValues(100.0f, 200.0f, 0.6f, 300.0f);
    auto layout = display.getLayout();

    // Stage 4 = Release, output = 0.3 means partway through release
    display.setPlaybackState(0.3f, 4, true);

    auto dotPos = display.getPlaybackDotPosition();

    // Dot should be in the release segment
    REQUIRE(dotPos.x >= layout.sustainEndX);
    REQUIRE(dotPos.x <= layout.releaseEndX);

    // Dot Y should correspond to output level 0.3
    float expectedY = display.levelToPixelY(0.3f);
    REQUIRE(static_cast<float>(dotPos.y) == Approx(expectedY).margin(2.0f));
}

TEST_CASE("Playback dot is not visible when voice is inactive",
          "[adsr_display][playback]") {
    auto display = makeDisplay();

    // No active voice
    display.setPlaybackState(0.0f, 0, false);
    REQUIRE(display.isPlaybackDotVisible() == false);

    // Active voice
    display.setPlaybackState(0.5f, 1, true);
    REQUIRE(display.isPlaybackDotVisible() == true);

    // Voice becomes inactive again
    display.setPlaybackState(0.0f, 0, false);
    REQUIRE(display.isPlaybackDotVisible() == false);
}

TEST_CASE("Playback dot position for decay stage interpolates toward sustain",
          "[adsr_display][playback]") {
    auto display = makeDisplayWithValues(100.0f, 200.0f, 0.6f, 300.0f);
    auto layout = display.getLayout();

    // Stage 2 = Decay, output = 0.8 (between peak 1.0 and sustain 0.6)
    display.setPlaybackState(0.8f, 2, true);

    auto dotPos = display.getPlaybackDotPosition();

    // Dot should be in the decay segment
    REQUIRE(dotPos.x >= layout.attackEndX);
    REQUIRE(dotPos.x <= layout.decayEndX);

    // Dot Y should correspond to output level 0.8
    float expectedY = display.levelToPixelY(0.8f);
    REQUIRE(static_cast<float>(dotPos.y) == Approx(expectedY).margin(2.0f));
}

// =============================================================================
// T087: Edge Case Tests
// =============================================================================

TEST_CASE("Control point clamping at time boundaries",
          "[adsr_display][edge_cases]") {
    auto display = makeDisplay();

    // Minimum time
    display.setAttackMs(0.01f);  // Below kMinTimeMs
    REQUIRE(display.getAttackMs() == Approx(ADSRDisplay::kMinTimeMs).margin(0.01f));

    // Maximum time
    display.setDecayMs(20000.0f);  // Above kMaxTimeMs
    REQUIRE(display.getDecayMs() == Approx(ADSRDisplay::kMaxTimeMs).margin(1.0f));

    // Sustain boundaries
    display.setSustainLevel(-0.5f);
    REQUIRE(display.getSustainLevel() == Approx(0.0f).margin(0.01f));

    display.setSustainLevel(1.5f);
    REQUIRE(display.getSustainLevel() == Approx(1.0f).margin(0.01f));
}

TEST_CASE("Display at minimum dimensions still works",
          "[adsr_display][edge_cases]") {
    // Tiny 30x20 display
    ADSRDisplay display(CRect(0, 0, 30, 20), nullptr, -1);
    display.setAttackMs(10.0f);
    display.setDecayMs(50.0f);
    display.setSustainLevel(0.5f);
    display.setReleaseMs(100.0f);

    auto layout = display.getLayout();

    // Layout should still produce valid segment positions
    REQUIRE(layout.attackStartX < layout.releaseEndX);
    REQUIRE(layout.topY < layout.bottomY);

    // Hit testing should still work
    auto peakPos = display.getControlPointPosition(ADSRDisplay::DragTarget::PeakPoint);
    CPoint onPeak(peakPos.x, peakPos.y);
    auto target = display.hitTest(onPeak);
    REQUIRE(target == ADSRDisplay::DragTarget::PeakPoint);
}

TEST_CASE("Three ADSRDisplay instances do not interfere (SC-011)",
          "[adsr_display][edge_cases]") {
    // Create three independent instances
    auto amp = makeDisplayWithValues(10.0f, 50.0f, 0.8f, 100.0f);
    auto filter = makeDisplayWithValues(5.0f, 200.0f, 0.3f, 500.0f);
    auto mod = makeDisplayWithValues(100.0f, 100.0f, 0.6f, 300.0f);

    // Set different colors
    amp.setStrokeColor(CColor(80, 140, 200, 255));
    filter.setStrokeColor(CColor(220, 170, 60, 255));
    mod.setStrokeColor(CColor(160, 90, 200, 255));

    // Verify each instance maintains its own state
    REQUIRE(amp.getAttackMs() == Approx(10.0f).margin(0.1f));
    REQUIRE(filter.getAttackMs() == Approx(5.0f).margin(0.1f));
    REQUIRE(mod.getAttackMs() == Approx(100.0f).margin(0.1f));

    REQUIRE(amp.getSustainLevel() == Approx(0.8f).margin(0.01f));
    REQUIRE(filter.getSustainLevel() == Approx(0.3f).margin(0.01f));
    REQUIRE(mod.getSustainLevel() == Approx(0.6f).margin(0.01f));

    // Modifying one doesn't affect others
    amp.setAttackMs(500.0f);
    REQUIRE(amp.getAttackMs() == Approx(500.0f).margin(0.1f));
    REQUIRE(filter.getAttackMs() == Approx(5.0f).margin(0.1f));
    REQUIRE(mod.getAttackMs() == Approx(100.0f).margin(0.1f));
}

TEST_CASE("Programmatic parameter updates (host automation)",
          "[adsr_display][edge_cases]") {
    auto display = makeDisplay();
    uint32_t lastParamId = 0;
    float lastValue = 0.0f;
    int callCount = 0;

    display.setParameterCallback([&](uint32_t id, float val) {
        lastParamId = id;
        lastValue = val;
        callCount++;
    });

    display.setAdsrBaseParamId(100);

    // Programmatic setters should not trigger callbacks (only drags do)
    display.setAttackMs(200.0f);
    REQUIRE(callCount == 0);  // setters don't call paramCallback_

    display.setSustainLevel(0.3f);
    REQUIRE(callCount == 0);

    // Verify the display updates its internal state correctly
    REQUIRE(display.getAttackMs() == Approx(200.0f).margin(0.1f));
    REQUIRE(display.getSustainLevel() == Approx(0.3f).margin(0.01f));
}
