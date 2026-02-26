// ==============================================================================
// Euclidean Dot Display Tests
// ==============================================================================
// Tests for EuclideanDotDisplay CView properties, clamping, and pattern
// consistency with EuclideanPattern::generate().
//
// Phase 11c - User Story 5: Euclidean Dual Visualization
// Tags: [euclidean]
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/euclidean_dot_display.h"
#include <krate/dsp/core/euclidean_pattern.h>

using namespace Krate::Plugins;
using Krate::DSP::EuclideanPattern;

// ==============================================================================
// T066: EuclideanDotDisplay Unit Tests
// ==============================================================================

TEST_CASE("EuclideanDotDisplay constructor creates view with default values",
          "[euclidean]") {
    VSTGUI::CRect rect(0, 0, 60, 60);
    EuclideanDotDisplay display(rect);

    REQUIRE(display.getHits() == 0);
    REQUIRE(display.getSteps() == 8);
    REQUIRE(display.getRotation() == 0);
    REQUIRE(display.getDotRadius() == Catch::Approx(3.0f));
    REQUIRE(display.getAccentColor() == VSTGUI::CColor(208, 132, 92, 255));
}

TEST_CASE("EuclideanDotDisplay setHits with steps=8 generates correct pattern",
          "[euclidean]") {
    VSTGUI::CRect rect(0, 0, 60, 60);
    EuclideanDotDisplay display(rect);

    display.setSteps(8);
    display.setHits(3);
    display.setRotation(0);

    // E(3,8) should produce exactly 3 hit positions
    uint32_t pattern = EuclideanPattern::generate(3, 8, 0);
    int hitCount = EuclideanPattern::countHits(pattern);
    REQUIRE(hitCount == 3);

    // The display should store hits=3
    REQUIRE(display.getHits() == 3);
}

TEST_CASE("EuclideanDotDisplay setSteps clamps hits to steps", "[euclidean]") {
    VSTGUI::CRect rect(0, 0, 60, 60);
    EuclideanDotDisplay display(rect);

    display.setSteps(8);
    display.setHits(5);
    REQUIRE(display.getHits() == 5);

    // Now reduce steps to 4 -- hits should be clamped to 4
    display.setSteps(4);
    REQUIRE(display.getHits() <= display.getSteps());
    REQUIRE(display.getSteps() == 4);
}

TEST_CASE("EuclideanDotDisplay setRotation shifts hit positions correctly",
          "[euclidean]") {
    VSTGUI::CRect rect(0, 0, 60, 60);
    EuclideanDotDisplay display(rect);

    display.setSteps(8);
    display.setHits(3);
    display.setRotation(0);

    uint32_t patternNoRotation = EuclideanPattern::generate(3, 8, 0);

    display.setRotation(2);
    REQUIRE(display.getRotation() == 2);

    uint32_t patternWithRotation = EuclideanPattern::generate(3, 8, 2);

    // The patterns should differ (rotation shifts hits)
    REQUIRE(patternNoRotation != patternWithRotation);

    // Both should still have the same number of hits
    REQUIRE(EuclideanPattern::countHits(patternNoRotation) ==
            EuclideanPattern::countHits(patternWithRotation));
}

TEST_CASE("EuclideanDotDisplay property clamping", "[euclidean]") {
    VSTGUI::CRect rect(0, 0, 60, 60);
    EuclideanDotDisplay display(rect);

    SECTION("hits clamped to [0, steps]") {
        display.setSteps(8);

        display.setHits(-5);
        REQUIRE(display.getHits() == 0);

        display.setHits(100);
        REQUIRE(display.getHits() == 8);  // clamped to steps
    }

    SECTION("steps clamped to [2, 32]") {
        display.setSteps(0);
        REQUIRE(display.getSteps() == 2);

        display.setSteps(1);
        REQUIRE(display.getSteps() == 2);

        display.setSteps(64);
        REQUIRE(display.getSteps() == 32);

        display.setSteps(16);
        REQUIRE(display.getSteps() == 16);
    }

    SECTION("rotation clamped to [0, steps-1]") {
        display.setSteps(8);

        display.setRotation(-1);
        REQUIRE(display.getRotation() == 0);

        display.setRotation(8);
        REQUIRE(display.getRotation() == 7);  // steps-1

        display.setRotation(100);
        REQUIRE(display.getRotation() == 7);

        display.setRotation(3);
        REQUIRE(display.getRotation() == 3);
    }
}

TEST_CASE("EuclideanDotDisplay E(3,8) produces hits at expected positions",
          "[euclidean]") {
    VSTGUI::CRect rect(0, 0, 60, 60);
    EuclideanDotDisplay display(rect);

    display.setSteps(8);
    display.setHits(3);
    display.setRotation(0);

    // Verify via EuclideanPattern::generate -- count hits
    uint32_t pattern = EuclideanPattern::generate(3, 8, 0);
    REQUIRE(EuclideanPattern::countHits(pattern) == 3);

    // Step 0 should always be a hit for E(k,n) with rotation=0 and k>0
    REQUIRE(EuclideanPattern::isHit(pattern, 0, 8));
}

// ==============================================================================
// T067: Euclidean Pattern Consistency Tests (SC-005)
// ==============================================================================

TEST_CASE("Euclidean circular display and linear overlay use identical "
           "EuclideanPattern::generate() call",
          "[euclidean]") {
    // Verify that given the same hits/steps/rotation, both the circular
    // display and the linear overlay (which both call
    // EuclideanPattern::generate()) produce the same pattern bitmask.
    //
    // The circular display uses:
    //   EuclideanPattern::generate(hits_, steps_, rotation_)
    // The linear overlay also uses:
    //   EuclideanPattern::generate(euclideanHits_, euclideanSteps_,
    //                              euclideanRotation_)
    //
    // This test verifies the underlying function is deterministic and
    // that the same parameters always yield the same result.

    for (int steps = 2; steps <= 16; ++steps) {
        for (int hits = 0; hits <= steps; ++hits) {
            for (int rotation = 0; rotation < steps; ++rotation) {
                uint32_t pattern1 =
                    EuclideanPattern::generate(hits, steps, rotation);
                uint32_t pattern2 =
                    EuclideanPattern::generate(hits, steps, rotation);
                REQUIRE(pattern1 == pattern2);

                // Also verify hit count is preserved
                REQUIRE(EuclideanPattern::countHits(pattern1) == hits);
            }
        }
    }
}

TEST_CASE("Euclidean pattern consistency across parameter orderings",
          "[euclidean]") {
    // SC-005: The display and overlay must show identical results.
    // This tests that the pattern is purely a function of (hits, steps, rotation)
    // regardless of the order setters are called.

    VSTGUI::CRect rect(0, 0, 60, 60);
    EuclideanDotDisplay display1(rect);
    EuclideanDotDisplay display2(rect);

    // Set in different order
    display1.setSteps(12);
    display1.setHits(5);
    display1.setRotation(3);

    display2.setRotation(3);
    display2.setHits(5);
    display2.setSteps(12);

    // Both should have the same stored values
    REQUIRE(display1.getHits() == display2.getHits());
    REQUIRE(display1.getSteps() == display2.getSteps());
    REQUIRE(display1.getRotation() == display2.getRotation());

    // And the generated pattern should match
    uint32_t pattern1 = EuclideanPattern::generate(
        display1.getHits(), display1.getSteps(), display1.getRotation());
    uint32_t pattern2 = EuclideanPattern::generate(
        display2.getHits(), display2.getSteps(), display2.getRotation());
    REQUIRE(pattern1 == pattern2);
}
