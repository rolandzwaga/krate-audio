// ==============================================================================
// Layer 1: DSP Primitive Tests - Spectral Utilities
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written before implementation.
//
// Tests for: dsp/include/krate/dsp/primitives/spectral_utils.h
// Specifically: parabolicInterpolation() (T021 - Innexus Phase 3)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/primitives/spectral_utils.h>

using namespace Krate::DSP;
using Catch::Approx;

// ==============================================================================
// parabolicInterpolation() Tests (T021 - Innexus Phase 3)
// ==============================================================================

TEST_CASE("parabolicInterpolation returns exact vertex on known parabola", "[spectral][parabolic][foundational]") {
    // A parabola y = -(x - 5.3)^2 + 10, sampled at x=5, x=6, x=7
    // The vertex is at x=5.3
    // y(5) = -(5 - 5.3)^2 + 10 = -0.09 + 10 = 9.91
    // y(6) = -(6 - 5.3)^2 + 10 = -0.49 + 10 = 9.51
    // y(7) = -(7 - 5.3)^2 + 10 = -2.89 + 10 = 7.11

    // Using bins centered at bin 6 (center), with left=bin 5, right=bin 7
    // parabolicInterpolation(left=9.91, center=9.51, right=7.11, centerBin=6)
    // The formula: delta = 0.5 * (left - right) / (left - 2*center + right)
    // delta = 0.5 * (9.91 - 7.11) / (9.91 - 2*9.51 + 7.11)
    // delta = 0.5 * 2.8 / (9.91 - 19.02 + 7.11)
    // delta = 0.5 * 2.8 / (-2.0)
    // delta = -0.7
    // interpolated position = 6 + (-0.7) = 5.3

    float result = parabolicInterpolation(9.91f, 9.51f, 7.11f, 6.0f);
    REQUIRE(result == Approx(5.3f).margin(0.01f));
}

TEST_CASE("parabolicInterpolation returns center bin for symmetric parabola", "[spectral][parabolic][foundational]") {
    // Symmetric parabola: left == right means the peak is exactly at center bin
    // y = -(x-10)^2 + 20, sampled at x=9, x=10, x=11
    // y(9) = -1 + 20 = 19
    // y(10) = 0 + 20 = 20
    // y(11) = -1 + 20 = 19

    float result = parabolicInterpolation(19.0f, 20.0f, 19.0f, 10.0f);
    REQUIRE(result == Approx(10.0f).margin(1e-6f));
}

TEST_CASE("parabolicInterpolation handles asymmetric peaks", "[spectral][parabolic]") {
    SECTION("peak offset to the left") {
        // When left > right, the true peak is to the left of center
        float result = parabolicInterpolation(8.0f, 10.0f, 6.0f, 5.0f);
        REQUIRE(result < 5.0f);  // Peak should be left of center bin
    }

    SECTION("peak offset to the right") {
        // When right > left, the true peak is to the right of center
        float result = parabolicInterpolation(6.0f, 10.0f, 8.0f, 5.0f);
        REQUIRE(result > 5.0f);  // Peak should be right of center bin
    }
}

TEST_CASE("parabolicInterpolation result stays within +/- 0.5 of center bin", "[spectral][parabolic]") {
    // The parabolic interpolation offset should never exceed +/- 0.5 bins
    // from the center when center is the local maximum
    float result1 = parabolicInterpolation(1.0f, 10.0f, 9.0f, 100.0f);
    REQUIRE(result1 >= 99.5f);
    REQUIRE(result1 <= 100.5f);

    float result2 = parabolicInterpolation(9.0f, 10.0f, 1.0f, 100.0f);
    REQUIRE(result2 >= 99.5f);
    REQUIRE(result2 <= 100.5f);
}
