#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/ring_geometry.h"

using Catch::Approx;
using namespace Gradus;

// ==============================================================================
// Angle Calculations
// ==============================================================================

TEST_CASE("RingGeometry: stepArc basics", "[gradus][ring][geometry]")
{
    SECTION("16 steps - each step is 2*PI/16 radians")
    {
        auto arc = RingGeometry::stepArc(0, 16);
        float stepAngle = kTwoPi / 16.0f;
        CHECK(arc.startAngle == Approx(kStartAngle));
        CHECK(arc.endAngle == Approx(kStartAngle + stepAngle));
        CHECK(arc.centerAngle == Approx(kStartAngle + stepAngle * 0.5f));
    }

    SECTION("Step 8 of 16 is at the bottom (6 o'clock)")
    {
        auto arc = RingGeometry::stepArc(8, 16);
        // Step 8 center = -PI/2 + 8*(2PI/16) + half_step = -PI/2 + PI + PI/16
        CHECK(arc.centerAngle == Approx(kStartAngle + kPi + kPi / 16.0f));
    }

    SECTION("32 steps - each step is ~11.25 degrees")
    {
        auto arc = RingGeometry::stepArc(0, 32);
        float expectedAngle = kTwoPi / 32.0f;
        CHECK((arc.endAngle - arc.startAngle) == Approx(expectedAngle));
    }

    SECTION("1 step covers full circle")
    {
        auto arc = RingGeometry::stepArc(0, 1);
        CHECK((arc.endAngle - arc.startAngle) == Approx(kTwoPi));
    }
}

TEST_CASE("RingGeometry: angleToStep", "[gradus][ring][geometry]")
{
    SECTION("12 o'clock = step 0")
    {
        CHECK(RingGeometry::angleToStep(kStartAngle, 16) == 0);
        CHECK(RingGeometry::angleToStep(kStartAngle + 0.01f, 16) == 0);
    }

    SECTION("3 o'clock = step 4 of 16")
    {
        // 3 o'clock = angle 0 in standard math
        CHECK(RingGeometry::angleToStep(0.0f, 16) == 4);
    }

    SECTION("6 o'clock = step 8 of 16")
    {
        // 6 o'clock = PI/2
        CHECK(RingGeometry::angleToStep(kPi / 2.0f, 16) == 8);
    }

    SECTION("Just before full circle wraps to last step")
    {
        // Angle just before 12 o'clock from the left = step 15
        float justBefore = kStartAngle - 0.01f + kTwoPi;
        CHECK(RingGeometry::angleToStep(justBefore, 16) == 15);
    }

    SECTION("Negative angles wrap correctly")
    {
        // -PI = 9 o'clock = step 12 of 16
        CHECK(RingGeometry::angleToStep(-kPi, 16) == 12);
    }
}

// ==============================================================================
// Point to Angle/Radius
// ==============================================================================

TEST_CASE("RingGeometry: pointToAngle and pointToRadius",
          "[gradus][ring][geometry]")
{
    RingGeometry geo;
    geo.setCenter(330.0f, 330.0f);

    SECTION("Point directly above center = 12 o'clock")
    {
        float angle = geo.pointToAngle(330.0f, 30.0f); // 300px above
        CHECK(angle == Approx(-kPi / 2.0f));
    }

    SECTION("Point directly right = 3 o'clock")
    {
        float angle = geo.pointToAngle(630.0f, 330.0f);
        CHECK(angle == Approx(0.0f));
    }

    SECTION("Point directly below = 6 o'clock")
    {
        float angle = geo.pointToAngle(330.0f, 630.0f);
        CHECK(angle == Approx(kPi / 2.0f));
    }

    SECTION("Radius from center")
    {
        CHECK(geo.pointToRadius(330.0f, 330.0f) == Approx(0.0f));
        CHECK(geo.pointToRadius(430.0f, 330.0f) == Approx(100.0f));
        CHECK(geo.pointToRadius(330.0f, 230.0f) == Approx(100.0f));
    }
}

// ==============================================================================
// Hit Testing
// ==============================================================================

TEST_CASE("RingGeometry: hitTest center", "[gradus][ring][geometry]")
{
    RingGeometry geo;

    auto result = geo.hitTest(330.0f, 330.0f); // dead center
    CHECK(result.isCenter);
    CHECK(result.subZone == SubZone::kEuclidean);
    CHECK(result.ringIndex == -1);

    // Edge of center
    result = geo.hitTest(330.0f + 74.0f, 330.0f);
    CHECK(result.isCenter);
}

TEST_CASE("RingGeometry: hitTest Ring 0 (Velocity/Gate)",
          "[gradus][ring][geometry]")
{
    RingGeometry geo;
    geo.setLaneStepCount(0, 16); // velocity
    geo.setLaneStepCount(1, 16); // gate

    SECTION("Outer half = Velocity")
    {
        // Radius 300 (between split 282 and outer 320), at 12 o'clock
        auto result = geo.hitTest(330.0f, 330.0f - 300.0f);
        CHECK(result.ringIndex == 0);
        CHECK(result.subZone == SubZone::kVelocity);
        CHECK(result.stepIndex == 0); // 12 o'clock = step 0
    }

    SECTION("Inner half = Gate")
    {
        // Radius 260 (between inner 245 and split 282), at 12 o'clock
        auto result = geo.hitTest(330.0f, 330.0f - 260.0f);
        CHECK(result.ringIndex == 0);
        CHECK(result.subZone == SubZone::kGate);
        CHECK(result.stepIndex == 0);
    }

    SECTION("Different step counts on same ring")
    {
        geo.setLaneStepCount(0, 16); // velocity
        geo.setLaneStepCount(1, 8);  // gate has fewer steps

        // 3 o'clock, outer (velocity) = step 4 of 16
        auto result = geo.hitTest(330.0f + 300.0f, 330.0f);
        CHECK(result.subZone == SubZone::kVelocity);
        CHECK(result.stepIndex == 4);

        // 3 o'clock, inner (gate) = step 2 of 8
        result = geo.hitTest(330.0f + 260.0f, 330.0f);
        CHECK(result.subZone == SubZone::kGate);
        CHECK(result.stepIndex == 2);
    }
}

TEST_CASE("RingGeometry: hitTest Ring 1 (Pitch)",
          "[gradus][ring][geometry]")
{
    RingGeometry geo;
    geo.setLaneStepCount(2, 12); // pitch has 12 steps

    // Radius 210 (between 185 and 240), at 12 o'clock
    auto result = geo.hitTest(330.0f, 330.0f - 210.0f);
    CHECK(result.ringIndex == 1);
    CHECK(result.subZone == SubZone::kPitch);
    CHECK(result.stepIndex == 0);
}

TEST_CASE("RingGeometry: hitTest Ring 2 (Modifier/Condition)",
          "[gradus][ring][geometry]")
{
    RingGeometry geo;
    geo.setLaneStepCount(3, 16); // modifier
    geo.setLaneStepCount(4, 16); // condition

    SECTION("Outer = Modifier")
    {
        // Radius 170 (between split 157 and outer 180)
        auto result = geo.hitTest(330.0f, 330.0f - 170.0f);
        CHECK(result.ringIndex == 2);
        CHECK(result.subZone == SubZone::kModifier);
    }

    SECTION("Inner = Condition")
    {
        // Radius 145 (between inner 135 and split 157)
        auto result = geo.hitTest(330.0f, 330.0f - 145.0f);
        CHECK(result.ringIndex == 2);
        CHECK(result.subZone == SubZone::kCondition);
    }
}

TEST_CASE("RingGeometry: hitTest Ring 3 (Ratchet/Chord/Inversion)",
          "[gradus][ring][geometry]")
{
    RingGeometry geo;
    geo.setLaneStepCount(5, 16); // ratchet
    geo.setLaneStepCount(6, 4);  // chord
    geo.setLaneStepCount(7, 4);  // inversion

    SECTION("Outer = Ratchet (113-130)")
    {
        auto result = geo.hitTest(330.0f, 330.0f - 120.0f);
        CHECK(result.subZone == SubZone::kRatchet);
        CHECK(result.stepIndex == 0);
    }

    SECTION("Middle = Chord (97-113)")
    {
        auto result = geo.hitTest(330.0f, 330.0f - 105.0f);
        CHECK(result.subZone == SubZone::kChord);
        // 12 o'clock with 4 steps = step 0
        CHECK(result.stepIndex == 0);
    }

    SECTION("Inner = Inversion (80-97)")
    {
        auto result = geo.hitTest(330.0f, 330.0f - 88.0f);
        CHECK(result.subZone == SubZone::kInversion);
        CHECK(result.stepIndex == 0);
    }
}

TEST_CASE("RingGeometry: hitTest gap between rings returns miss",
          "[gradus][ring][geometry]")
{
    RingGeometry geo;

    // Gap between Ring 1 (outer=240) and Ring 0 (inner=245)
    auto result = geo.hitTest(330.0f, 330.0f - 242.0f);
    CHECK(result.ringIndex == -1);
    CHECK(result.subZone == SubZone::kNone);
    CHECK_FALSE(result.isCenter);
}

TEST_CASE("RingGeometry: hitTest outside all rings returns miss",
          "[gradus][ring][geometry]")
{
    RingGeometry geo;

    auto result = geo.hitTest(330.0f, 330.0f - 350.0f);
    CHECK(result.ringIndex == -1);
    CHECK(result.subZone == SubZone::kNone);
}

// ==============================================================================
// Radial Value Conversion
// ==============================================================================

TEST_CASE("RingGeometry: radiusToNormalizedValue",
          "[gradus][ring][geometry]")
{
    RingGeometry geo;

    SECTION("Velocity: inner edge = 0, outer edge = 1")
    {
        // Velocity sub-zone: splitRadius(282) to outerRadius(320)
        CHECK(geo.radiusToNormalizedValue(0, SubZone::kVelocity, 282.0f)
              == Approx(0.0f));
        CHECK(geo.radiusToNormalizedValue(0, SubZone::kVelocity, 320.0f)
              == Approx(1.0f));
        CHECK(geo.radiusToNormalizedValue(0, SubZone::kVelocity, 301.0f)
              == Approx(0.5f));
    }

    SECTION("Clamps to 0-1 range")
    {
        CHECK(geo.radiusToNormalizedValue(0, SubZone::kVelocity, 200.0f)
              == Approx(0.0f));
        CHECK(geo.radiusToNormalizedValue(0, SubZone::kVelocity, 400.0f)
              == Approx(1.0f));
    }

    SECTION("Pitch: full ring")
    {
        // Pitch: innerRadius(185) to outerRadius(240)
        CHECK(geo.radiusToNormalizedValue(1, SubZone::kPitch, 185.0f)
              == Approx(0.0f));
        CHECK(geo.radiusToNormalizedValue(1, SubZone::kPitch, 240.0f)
              == Approx(1.0f));
        // Midline = 212.5 → normalized 0.5
        CHECK(geo.radiusToNormalizedValue(1, SubZone::kPitch, 212.5f)
              == Approx(0.5f));
    }
}

TEST_CASE("RingGeometry: normalizedValueToRadius round-trips",
          "[gradus][ring][geometry]")
{
    RingGeometry geo;

    for (float v = 0.0f; v <= 1.0f; v += 0.1f) {
        float radius = geo.normalizedValueToRadius(
            0, SubZone::kVelocity, v);
        float roundTrip = geo.radiusToNormalizedValue(
            0, SubZone::kVelocity, radius);
        CHECK(roundTrip == Approx(v).margin(0.001f));
    }
}

// ==============================================================================
// SubZone to Lane Index Mapping
// ==============================================================================

TEST_CASE("RingGeometry: subZoneToLaneIndex",
          "[gradus][ring][geometry]")
{
    CHECK(subZoneToLaneIndex(SubZone::kVelocity) == 0);
    CHECK(subZoneToLaneIndex(SubZone::kGate) == 1);
    CHECK(subZoneToLaneIndex(SubZone::kPitch) == 2);
    CHECK(subZoneToLaneIndex(SubZone::kModifier) == 3);
    CHECK(subZoneToLaneIndex(SubZone::kCondition) == 4);
    CHECK(subZoneToLaneIndex(SubZone::kRatchet) == 5);
    CHECK(subZoneToLaneIndex(SubZone::kChord) == 6);
    CHECK(subZoneToLaneIndex(SubZone::kInversion) == 7);
    CHECK(subZoneToLaneIndex(SubZone::kNone) == -1);
    CHECK(subZoneToLaneIndex(SubZone::kEuclidean) == -1);
}

// ==============================================================================
// Polar/Cartesian Conversion
// ==============================================================================

TEST_CASE("RingGeometry: polarToCartesian",
          "[gradus][ring][geometry]")
{
    RingGeometry geo;
    geo.setCenter(330.0f, 330.0f);

    float x = 0.0f, y = 0.0f;

    SECTION("12 o'clock at radius 100")
    {
        geo.polarToCartesian(-kPi / 2.0f, 100.0f, x, y);
        CHECK(x == Approx(330.0f).margin(0.01f));
        CHECK(y == Approx(230.0f).margin(0.01f)); // 330 - 100
    }

    SECTION("3 o'clock at radius 100")
    {
        geo.polarToCartesian(0.0f, 100.0f, x, y);
        CHECK(x == Approx(430.0f).margin(0.01f)); // 330 + 100
        CHECK(y == Approx(330.0f).margin(0.01f));
    }
}
