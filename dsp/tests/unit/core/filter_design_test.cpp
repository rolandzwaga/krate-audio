// ==============================================================================
// Layer 0: Core Utilities - Filter Design Tests
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Constitution Principle XII: Test-First Development
//
// Tests for: dsp/include/krate/dsp/core/filter_design.h
// Contract: specs/070-filter-foundations/contracts/filter_design.h
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/core/filter_design.h>

#include <cmath>
#include <limits>

using namespace Krate::DSP;
using Catch::Approx;

// ==============================================================================
// SC-012: Constexpr Verification (static_assert tests)
// ==============================================================================
// These static_asserts verify that besselQ and butterworthPoleAngle are constexpr

static_assert(FilterDesign::besselQ(0, 2) > 0.5f, "besselQ must be constexpr");
static_assert(FilterDesign::butterworthPoleAngle(0, 2) > 0.0f, "butterworthPoleAngle must be constexpr");

// ==============================================================================
// prewarpFrequency Tests (FR-006, SC-006)
// ==============================================================================

TEST_CASE("prewarpFrequency compensates for bilinear transform warping", "[filter_design][prewarp]") {

    SECTION("SC-006: 1kHz at 44100Hz within 1% of theoretical value") {
        // Theoretical: f_prewarped = (fs / pi) * tan(pi * f / fs)
        // For 1kHz at 44100Hz: (44100 / pi) * tan(pi * 1000 / 44100)
        // Expected: approximately 1001.5 Hz (slight increase due to warping)
        const float result = FilterDesign::prewarpFrequency(1000.0f, 44100.0);
        const float theoretical = (44100.0f / 3.14159265f) * std::tan(3.14159265f * 1000.0f / 44100.0f);

        REQUIRE(result == Approx(theoretical).epsilon(0.01f));
    }

    SECTION("Low frequencies show minimal warping") {
        // At very low frequencies, prewarping should be close to the original
        const float result = FilterDesign::prewarpFrequency(100.0f, 44100.0);
        REQUIRE(result == Approx(100.0f).epsilon(0.01f));
    }

    SECTION("Higher frequencies show more significant warping") {
        // At 10kHz, prewarping should show noticeable difference
        const float result = FilterDesign::prewarpFrequency(10000.0f, 44100.0);
        const float theoretical = (44100.0f / 3.14159265f) * std::tan(3.14159265f * 10000.0f / 44100.0f);

        REQUIRE(result == Approx(theoretical).epsilon(0.01f));
        REQUIRE(result > 10000.0f); // Warping increases frequency
    }

    SECTION("Edge case: zero sample rate returns input unchanged") {
        REQUIRE(FilterDesign::prewarpFrequency(1000.0f, 0.0) == 1000.0f);
    }

    SECTION("Edge case: negative sample rate returns input unchanged") {
        REQUIRE(FilterDesign::prewarpFrequency(1000.0f, -44100.0) == 1000.0f);
    }

    SECTION("Edge case: zero frequency returns zero (or input unchanged)") {
        REQUIRE(FilterDesign::prewarpFrequency(0.0f, 44100.0) == 0.0f);
    }

    SECTION("Edge case: negative frequency returns input unchanged") {
        REQUIRE(FilterDesign::prewarpFrequency(-1000.0f, 44100.0) == -1000.0f);
    }
}

// ==============================================================================
// combFeedbackForRT60 Tests (FR-007, SC-007)
// ==============================================================================

TEST_CASE("combFeedbackForRT60 calculates feedback for desired decay time", "[filter_design][comb]") {

    SECTION("SC-007: 50ms delay, 2.0s RT60 within 1% of theoretical") {
        // Formula: g = 10^(-3 * delayMs / (1000 * rt60Seconds))
        // g = 10^(-3 * 50 / 2000) = 10^(-0.075) = approximately 0.841
        const float result = FilterDesign::combFeedbackForRT60(50.0f, 2.0f);
        const float theoretical = std::pow(10.0f, -3.0f * 50.0f / 2000.0f);

        REQUIRE(result == Approx(theoretical).epsilon(0.01f));
    }

    SECTION("Longer RT60 produces higher feedback coefficient") {
        const float shortDecay = FilterDesign::combFeedbackForRT60(50.0f, 1.0f);
        const float longDecay = FilterDesign::combFeedbackForRT60(50.0f, 3.0f);

        REQUIRE(longDecay > shortDecay);
    }

    SECTION("Shorter delay requires higher feedback for same RT60") {
        const float shortDelay = FilterDesign::combFeedbackForRT60(25.0f, 2.0f);
        const float longDelay = FilterDesign::combFeedbackForRT60(100.0f, 2.0f);

        REQUIRE(shortDelay > longDelay);
    }

    SECTION("Result is in valid range [0, 1)") {
        const float result = FilterDesign::combFeedbackForRT60(50.0f, 2.0f);
        REQUIRE(result >= 0.0f);
        REQUIRE(result < 1.0f);
    }

    SECTION("Edge case: zero delay returns 0") {
        REQUIRE(FilterDesign::combFeedbackForRT60(0.0f, 2.0f) == 0.0f);
    }

    SECTION("Edge case: negative delay returns 0") {
        REQUIRE(FilterDesign::combFeedbackForRT60(-50.0f, 2.0f) == 0.0f);
    }

    SECTION("Edge case: zero RT60 returns 0") {
        REQUIRE(FilterDesign::combFeedbackForRT60(50.0f, 0.0f) == 0.0f);
    }

    SECTION("Edge case: negative RT60 returns 0") {
        REQUIRE(FilterDesign::combFeedbackForRT60(50.0f, -2.0f) == 0.0f);
    }
}

// ==============================================================================
// besselQ Tests (FR-009)
// ==============================================================================

TEST_CASE("besselQ returns correct Q values for Bessel filter stages", "[filter_design][bessel]") {

    SECTION("Order 2: Q = 0.57735") {
        REQUIRE(FilterDesign::besselQ(0, 2) == Approx(0.57735f).margin(0.0001f));
    }

    SECTION("Order 3: Q = 0.69105 for stage 0") {
        REQUIRE(FilterDesign::besselQ(0, 3) == Approx(0.69105f).margin(0.0001f));
    }

    SECTION("Order 4: stage 0 = 0.80554, stage 1 = 0.52193") {
        REQUIRE(FilterDesign::besselQ(0, 4) == Approx(0.80554f).margin(0.0001f));
        REQUIRE(FilterDesign::besselQ(1, 4) == Approx(0.52193f).margin(0.0001f));
    }

    SECTION("Order 5: stage 0 = 0.91648, stage 1 = 0.56354") {
        REQUIRE(FilterDesign::besselQ(0, 5) == Approx(0.91648f).margin(0.0001f));
        REQUIRE(FilterDesign::besselQ(1, 5) == Approx(0.56354f).margin(0.0001f));
    }

    SECTION("Order 6: stages 0, 1, 2") {
        REQUIRE(FilterDesign::besselQ(0, 6) == Approx(1.02331f).margin(0.0001f));
        REQUIRE(FilterDesign::besselQ(1, 6) == Approx(0.61119f).margin(0.0001f));
        REQUIRE(FilterDesign::besselQ(2, 6) == Approx(0.51032f).margin(0.0001f));
    }

    SECTION("Order 7: stages 0, 1, 2") {
        REQUIRE(FilterDesign::besselQ(0, 7) == Approx(1.12626f).margin(0.0001f));
        REQUIRE(FilterDesign::besselQ(1, 7) == Approx(0.66082f).margin(0.0001f));
        REQUIRE(FilterDesign::besselQ(2, 7) == Approx(0.53236f).margin(0.0001f));
    }

    SECTION("Order 8: stages 0, 1, 2, 3") {
        REQUIRE(FilterDesign::besselQ(0, 8) == Approx(1.22567f).margin(0.0001f));
        REQUIRE(FilterDesign::besselQ(1, 8) == Approx(0.71085f).margin(0.0001f));
        REQUIRE(FilterDesign::besselQ(2, 8) == Approx(0.55961f).margin(0.0001f));
        REQUIRE(FilterDesign::besselQ(3, 8) == Approx(0.50599f).margin(0.0001f));
    }

    SECTION("Edge case: order < 2 returns Butterworth fallback (0.7071)") {
        REQUIRE(FilterDesign::besselQ(0, 1) == Approx(0.7071f).margin(0.0001f));
        REQUIRE(FilterDesign::besselQ(0, 0) == Approx(0.7071f).margin(0.0001f));
    }

    SECTION("Edge case: order > 8 returns Butterworth fallback") {
        REQUIRE(FilterDesign::besselQ(0, 9) == Approx(0.7071f).margin(0.0001f));
        REQUIRE(FilterDesign::besselQ(0, 10) == Approx(0.7071f).margin(0.0001f));
    }

    SECTION("Edge case: stage out of range returns Butterworth fallback") {
        REQUIRE(FilterDesign::besselQ(5, 4) == Approx(0.7071f).margin(0.0001f));
    }
}

// ==============================================================================
// chebyshevQ Tests (FR-008)
// ==============================================================================

TEST_CASE("chebyshevQ calculates Q values for Chebyshev Type I filters", "[filter_design][chebyshev]") {

    SECTION("4th order 1dB ripple: stage 0 has highest Q") {
        // For 4th order (2 biquads) with 1dB ripple
        // The Q values depend on pole assignment convention.
        // Using direct pole-to-Q formula: Q = |pole| / (2 * |sigma|)
        // Stage 0 uses the pole closest to real axis (smallest theta), giving highest Q
        const float q0 = FilterDesign::chebyshevQ(0, 2, 1.0f);
        REQUIRE(q0 > 1.0f);  // Should be notably higher than Butterworth (~0.5-0.7)
        REQUIRE(q0 == Approx(3.56f).margin(0.1f));  // Direct formula result
    }

    SECTION("4th order 1dB ripple: stage 1 has lower Q than stage 0") {
        const float q1 = FilterDesign::chebyshevQ(1, 2, 1.0f);
        REQUIRE(q1 > 0.5f);  // Should be higher than basic Butterworth
        REQUIRE(q1 == Approx(0.785f).margin(0.05f));  // Direct formula result
    }

    SECTION("Stage 0 has higher Q than subsequent stages") {
        const float q0 = FilterDesign::chebyshevQ(0, 2, 1.0f);
        const float q1 = FilterDesign::chebyshevQ(1, 2, 1.0f);
        REQUIRE(q0 > q1);
    }

    SECTION("Higher ripple produces higher Q values") {
        const float q_1dB = FilterDesign::chebyshevQ(0, 2, 1.0f);
        const float q_3dB = FilterDesign::chebyshevQ(0, 2, 3.0f);
        REQUIRE(q_3dB > q_1dB);
    }

    SECTION("Zero ripple falls back to Butterworth") {
        const float q_cheby = FilterDesign::chebyshevQ(0, 2, 0.0f);
        // Butterworth Q for order 4 (2 stages), stage 0:
        // Q = 1 / (2 * cos(pi * (2*0 + 1) / (4*2))) = 1 / (2 * cos(pi/8))
        REQUIRE(q_cheby == Approx(0.5412f).margin(0.01f));
    }

    SECTION("Negative ripple falls back to Butterworth") {
        const float q_cheby = FilterDesign::chebyshevQ(0, 2, -1.0f);
        REQUIRE(q_cheby == Approx(0.5412f).margin(0.01f));
    }

    SECTION("Edge case: zero stages returns Butterworth default") {
        REQUIRE(FilterDesign::chebyshevQ(0, 0, 1.0f) == Approx(0.7071f).margin(0.0001f));
    }
}

// ==============================================================================
// butterworthPoleAngle Tests (FR-010)
// ==============================================================================

TEST_CASE("butterworthPoleAngle calculates Butterworth filter pole angles", "[filter_design][butterworth]") {

    SECTION("Order 2, k=0: theta = pi * (2*0+1) / (2*2) = pi/4") {
        // Formula: theta_k = pi * (2*k + 1) / (2*N)
        // For k=0, N=2: theta = pi * 1 / 4 = pi/4
        const float theta = FilterDesign::butterworthPoleAngle(0, 2);
        const float expected = 3.14159265f / 4.0f;  // pi/4
        REQUIRE(theta == Approx(expected).margin(0.0001f));
    }

    SECTION("Order 2, k=1: theta = pi * (2*1+1) / (2*2) = 3*pi/4") {
        // For k=1, N=2: theta = pi * 3 / 4 = 3*pi/4
        const float theta = FilterDesign::butterworthPoleAngle(1, 2);
        const float expected = 3.0f * 3.14159265f / 4.0f;  // 3*pi/4
        REQUIRE(theta == Approx(expected).margin(0.0001f));
    }

    SECTION("Order 4, k=0: theta = pi*(2*0+1)/(2*4) = pi/8") {
        // Wait, the formula in spec is theta_k = pi * (2*k + 1) / (2*N)
        // For k=0, N=4: theta = pi * 1 / 8 = pi/8
        // But that gives left half plane poles starting at pi/8...
        // Let me recalculate: for N=2, k=0: theta = pi * 1 / 4 = pi/4
        // Hmm, the contract says k=0, N=2 returns 3*pi/4. Let me check.
        // Actually looking at contract: theta_k = pi * (2*k + 1) / (2*N)
        // k=0, N=2: theta = pi * 1 / 4 = pi/4... but that's not 3*pi/4.
        // Let me check the actual pole positions. Butterworth poles are at:
        // s_k = exp(j * pi * (2k + N + 1) / (2N)) for k = 0, 1, ..., N-1
        // For stability we use the left half plane poles.
        // Contract implementation shows: pi * (2*k + 1) / (2*N)
        // For k=0, N=2: pi * 1 / 4 = 0.785 rad
        // But comment says "3*pi/4"...
        // I'll test with what the contract actually implements.
        const float theta = FilterDesign::butterworthPoleAngle(0, 4);
        const float expected = 3.14159265f * (2.0f * 0.0f + 1.0f) / (2.0f * 4.0f);
        REQUIRE(theta == Approx(expected).margin(0.0001f));
    }

    SECTION("Poles are evenly spaced") {
        const float theta0 = FilterDesign::butterworthPoleAngle(0, 4);
        const float theta1 = FilterDesign::butterworthPoleAngle(1, 4);
        const float theta2 = FilterDesign::butterworthPoleAngle(2, 4);
        const float theta3 = FilterDesign::butterworthPoleAngle(3, 4);

        const float spacing = theta1 - theta0;
        REQUIRE((theta2 - theta1) == Approx(spacing).margin(0.0001f));
        REQUIRE((theta3 - theta2) == Approx(spacing).margin(0.0001f));
    }

    SECTION("Edge case: N=0 returns 0") {
        REQUIRE(FilterDesign::butterworthPoleAngle(0, 0) == 0.0f);
    }
}

// ==============================================================================
// Integration: Verify all functions are noexcept
// ==============================================================================

TEST_CASE("FilterDesign functions are noexcept", "[filter_design][safety]") {
    STATIC_REQUIRE(noexcept(FilterDesign::prewarpFrequency(1000.0f, 44100.0)));
    STATIC_REQUIRE(noexcept(FilterDesign::combFeedbackForRT60(50.0f, 2.0f)));
    STATIC_REQUIRE(noexcept(FilterDesign::chebyshevQ(0, 2, 1.0f)));
    STATIC_REQUIRE(noexcept(FilterDesign::besselQ(0, 2)));
    STATIC_REQUIRE(noexcept(FilterDesign::butterworthPoleAngle(0, 2)));
}
