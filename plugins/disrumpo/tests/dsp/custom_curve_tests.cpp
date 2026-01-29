// ==============================================================================
// Tests: CustomCurve
// ==============================================================================
// Tests for user-defined breakpoint curve for Custom morph link mode.
//
// Reference: specs/007-sweep-system/spec.md (FR-022)
// Reference: specs/007-sweep-system/data-model.md (CustomCurve entity)
// ==============================================================================

#include "dsp/custom_curve.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;
using namespace Disrumpo;

// ==============================================================================
// Default Construction Tests
// ==============================================================================

TEST_CASE("CustomCurve: default construction", "[sweep][curve][custom]") {
    CustomCurve curve;

    SECTION("has exactly 2 breakpoints") {
        REQUIRE(curve.getBreakpointCount() == 2);
    }

    SECTION("first breakpoint at (0, 0)") {
        auto bp = curve.getBreakpoint(0);
        REQUIRE(bp.x == Approx(0.0f));
        REQUIRE(bp.y == Approx(0.0f));
    }

    SECTION("last breakpoint at (1, 1)") {
        auto bp = curve.getBreakpoint(1);
        REQUIRE(bp.x == Approx(1.0f));
        REQUIRE(bp.y == Approx(1.0f));
    }

    SECTION("evaluates as linear by default") {
        REQUIRE(curve.evaluate(0.0f) == Approx(0.0f));
        REQUIRE(curve.evaluate(0.25f) == Approx(0.25f));
        REQUIRE(curve.evaluate(0.5f) == Approx(0.5f));
        REQUIRE(curve.evaluate(0.75f) == Approx(0.75f));
        REQUIRE(curve.evaluate(1.0f) == Approx(1.0f));
    }
}

// ==============================================================================
// Add Breakpoint Tests
// ==============================================================================

TEST_CASE("CustomCurve: add breakpoint", "[sweep][curve][custom]") {
    CustomCurve curve;

    SECTION("can add breakpoint in middle") {
        REQUIRE(curve.addBreakpoint(0.5f, 0.8f));
        REQUIRE(curve.getBreakpointCount() == 3);
    }

    SECTION("breakpoints are sorted by x") {
        curve.addBreakpoint(0.7f, 0.6f);
        curve.addBreakpoint(0.3f, 0.4f);

        REQUIRE(curve.getBreakpointCount() == 4);

        // Verify sorted order
        REQUIRE(curve.getBreakpoint(0).x == Approx(0.0f));
        REQUIRE(curve.getBreakpoint(1).x == Approx(0.3f));
        REQUIRE(curve.getBreakpoint(2).x == Approx(0.7f));
        REQUIRE(curve.getBreakpoint(3).x == Approx(1.0f));
    }

    SECTION("can add up to 8 breakpoints") {
        // Already has 2, so we can add 6 more
        REQUIRE(curve.addBreakpoint(0.1f, 0.1f));  // 3
        REQUIRE(curve.addBreakpoint(0.2f, 0.2f));  // 4
        REQUIRE(curve.addBreakpoint(0.3f, 0.3f));  // 5
        REQUIRE(curve.addBreakpoint(0.4f, 0.4f));  // 6
        REQUIRE(curve.addBreakpoint(0.5f, 0.5f));  // 7
        REQUIRE(curve.addBreakpoint(0.6f, 0.6f));  // 8

        REQUIRE(curve.getBreakpointCount() == 8);
    }

    SECTION("cannot add more than 8 breakpoints") {
        for (int i = 0; i < 6; ++i) {
            curve.addBreakpoint(static_cast<float>(i + 1) * 0.1f, 0.5f);
        }

        REQUIRE(curve.getBreakpointCount() == 8);

        // Should fail to add 9th
        REQUIRE_FALSE(curve.addBreakpoint(0.75f, 0.75f));
        REQUIRE(curve.getBreakpointCount() == 8);
    }

    SECTION("clamps coordinates to [0, 1]") {
        curve.addBreakpoint(-0.5f, 1.5f);

        // Find the added breakpoint (should be at x=0.0 or close to start)
        auto bp = curve.getBreakpoint(0);  // Might be inserted at front if x was clamped to 0
        // Coordinates should be clamped
        REQUIRE(bp.x >= 0.0f);
        REQUIRE(bp.x <= 1.0f);
    }
}

// ==============================================================================
// Remove Breakpoint Tests
// ==============================================================================

TEST_CASE("CustomCurve: remove breakpoint", "[sweep][curve][custom]") {
    CustomCurve curve;

    SECTION("can remove middle breakpoint") {
        curve.addBreakpoint(0.5f, 0.7f);
        REQUIRE(curve.getBreakpointCount() == 3);

        REQUIRE(curve.removeBreakpoint(1));  // Remove middle
        REQUIRE(curve.getBreakpointCount() == 2);
    }

    SECTION("cannot remove below minimum (2)") {
        REQUIRE(curve.getBreakpointCount() == 2);

        // Should fail to remove when at minimum
        REQUIRE_FALSE(curve.removeBreakpoint(0));
        REQUIRE_FALSE(curve.removeBreakpoint(1));
        REQUIRE(curve.getBreakpointCount() == 2);
    }

    SECTION("invalid index returns false") {
        REQUIRE_FALSE(curve.removeBreakpoint(-1));
        REQUIRE_FALSE(curve.removeBreakpoint(99));
    }
}

// ==============================================================================
// Set Breakpoint Tests
// ==============================================================================

TEST_CASE("CustomCurve: set breakpoint", "[sweep][curve][custom]") {
    CustomCurve curve;

    SECTION("can modify breakpoint position") {
        curve.addBreakpoint(0.5f, 0.5f);

        curve.setBreakpoint(1, 0.5f, 0.8f);

        auto bp = curve.getBreakpoint(1);
        REQUIRE(bp.x == Approx(0.5f));
        REQUIRE(bp.y == Approx(0.8f));
    }

    SECTION("maintains sorting after modification") {
        curve.addBreakpoint(0.3f, 0.3f);
        curve.addBreakpoint(0.7f, 0.7f);

        // Move the middle point to the right
        curve.setBreakpoint(2, 0.8f, 0.8f);

        // Should be re-sorted
        REQUIRE(curve.getBreakpoint(0).x == Approx(0.0f));
        REQUIRE(curve.getBreakpoint(1).x == Approx(0.3f));
        REQUIRE(curve.getBreakpoint(2).x == Approx(0.8f));
        REQUIRE(curve.getBreakpoint(3).x == Approx(1.0f));
    }

    SECTION("cannot move first breakpoint x from 0") {
        curve.setBreakpoint(0, 0.5f, 0.2f);
        auto bp = curve.getBreakpoint(0);
        REQUIRE(bp.x == Approx(0.0f));  // X should remain at 0
    }

    SECTION("cannot move last breakpoint x from 1") {
        curve.setBreakpoint(1, 0.5f, 0.8f);

        // Find the last breakpoint
        int lastIdx = curve.getBreakpointCount() - 1;
        auto bp = curve.getBreakpoint(lastIdx);
        REQUIRE(bp.x == Approx(1.0f));  // X should remain at 1
    }
}

// ==============================================================================
// Interpolation Tests
// ==============================================================================

TEST_CASE("CustomCurve: evaluate interpolation", "[sweep][curve][custom]") {
    CustomCurve curve;

    SECTION("linear interpolation between breakpoints") {
        curve.addBreakpoint(0.5f, 1.0f);  // Step up to 1 at 0.5

        // From (0,0) to (0.5,1): linear interpolation
        REQUIRE(curve.evaluate(0.25f) == Approx(0.5f));

        // From (0.5,1) to (1,1): stays at 1
        REQUIRE(curve.evaluate(0.75f) == Approx(1.0f));
    }

    SECTION("complex multi-segment curve") {
        // Create a curve: 0->0.3->0.8->1
        curve.addBreakpoint(0.25f, 0.3f);
        curve.addBreakpoint(0.75f, 0.8f);

        // Between first and second (0,0) -> (0.25,0.3)
        // At x=0.125 (midpoint), y should be 0.15
        REQUIRE(curve.evaluate(0.125f) == Approx(0.15f).margin(0.01f));

        // Between second and third (0.25,0.3) -> (0.75,0.8)
        // At x=0.5 (midpoint), y should be 0.55
        REQUIRE(curve.evaluate(0.5f) == Approx(0.55f).margin(0.01f));
    }

    SECTION("edge cases") {
        // Below range returns first y
        REQUIRE(curve.evaluate(-0.5f) == Approx(0.0f));

        // Above range returns last y
        REQUIRE(curve.evaluate(1.5f) == Approx(1.0f));
    }
}

// ==============================================================================
// Reset Tests
// ==============================================================================

TEST_CASE("CustomCurve: reset", "[sweep][curve][custom]") {
    CustomCurve curve;

    curve.addBreakpoint(0.3f, 0.5f);
    curve.addBreakpoint(0.7f, 0.9f);

    REQUIRE(curve.getBreakpointCount() == 4);

    curve.reset();

    SECTION("reset restores default state") {
        REQUIRE(curve.getBreakpointCount() == 2);
        REQUIRE(curve.getBreakpoint(0).x == Approx(0.0f));
        REQUIRE(curve.getBreakpoint(0).y == Approx(0.0f));
        REQUIRE(curve.getBreakpoint(1).x == Approx(1.0f));
        REQUIRE(curve.getBreakpoint(1).y == Approx(1.0f));
    }
}

// ==============================================================================
// Constraint Tests
// ==============================================================================

TEST_CASE("CustomCurve: constraints", "[sweep][curve][custom]") {
    CustomCurve curve;

    SECTION("minimum 2 breakpoints enforced") {
        // Start with default 2
        REQUIRE(curve.getBreakpointCount() == 2);

        // Try removing - should fail
        REQUIRE_FALSE(curve.removeBreakpoint(0));
        REQUIRE_FALSE(curve.removeBreakpoint(1));
    }

    SECTION("maximum 8 breakpoints enforced") {
        // Add until at limit
        for (int i = 0; i < 10; ++i) {
            curve.addBreakpoint(static_cast<float>(i + 1) * 0.08f, 0.5f);
        }

        REQUIRE(curve.getBreakpointCount() == 8);
    }

    SECTION("first x must be 0") {
        auto bp = curve.getBreakpoint(0);
        REQUIRE(bp.x == Approx(0.0f));
    }

    SECTION("last x must be 1") {
        auto bp = curve.getBreakpoint(curve.getBreakpointCount() - 1);
        REQUIRE(bp.x == Approx(1.0f));
    }
}
