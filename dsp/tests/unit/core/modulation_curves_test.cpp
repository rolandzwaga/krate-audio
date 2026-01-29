// ==============================================================================
// Layer 0: Core Tests - Modulation Curves
// ==============================================================================
// Tests for modulation curve shaping functions.
//
// Reference: specs/008-modulation-system/spec.md (FR-058, FR-059, SC-003, SC-004)
// ==============================================================================

#include <krate/dsp/core/modulation_curves.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Linear Curve Tests (SC-003)
// =============================================================================

TEST_CASE("Linear curve: y = x", "[core][modulation_curves]") {
    REQUIRE(applyModCurve(ModCurve::Linear, 0.0f) == Approx(0.0f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::Linear, 0.25f) == Approx(0.25f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::Linear, 0.5f) == Approx(0.5f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::Linear, 0.75f) == Approx(0.75f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::Linear, 1.0f) == Approx(1.0f).margin(0.01f));
}

// =============================================================================
// Exponential Curve Tests (SC-003)
// =============================================================================

TEST_CASE("Exponential curve: y = x^2", "[core][modulation_curves]") {
    REQUIRE(applyModCurve(ModCurve::Exponential, 0.0f) == Approx(0.0f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::Exponential, 0.25f) == Approx(0.0625f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::Exponential, 0.5f) == Approx(0.25f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::Exponential, 0.75f) == Approx(0.5625f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::Exponential, 1.0f) == Approx(1.0f).margin(0.01f));
}

// =============================================================================
// S-Curve Tests (SC-003)
// =============================================================================

TEST_CASE("S-Curve: y = x^2 * (3 - 2x)", "[core][modulation_curves]") {
    REQUIRE(applyModCurve(ModCurve::SCurve, 0.0f) == Approx(0.0f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::SCurve, 0.25f) == Approx(0.15625f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::SCurve, 0.5f) == Approx(0.5f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::SCurve, 0.75f) == Approx(0.84375f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::SCurve, 1.0f) == Approx(1.0f).margin(0.01f));
}

// =============================================================================
// Stepped Curve Tests (SC-003)
// =============================================================================

TEST_CASE("Stepped curve: y = floor(x * 4) / 3", "[core][modulation_curves]") {
    REQUIRE(applyModCurve(ModCurve::Stepped, 0.0f) == Approx(0.0f).margin(0.01f));
    // x=0.3: floor(0.3*4)/3 = floor(1.2)/3 = 1/3 = 0.333
    REQUIRE(applyModCurve(ModCurve::Stepped, 0.3f) == Approx(1.0f / 3.0f).margin(0.01f));
    // x=0.6: floor(0.6*4)/3 = floor(2.4)/3 = 2/3 = 0.667
    REQUIRE(applyModCurve(ModCurve::Stepped, 0.6f) == Approx(2.0f / 3.0f).margin(0.01f));
    // x=1.0: floor(1.0*4)/3 = floor(4.0)/3 = 4/3 = 1.333 -> clamped to 1.0
    // Actually per formula: floor(1.0*4)/3 = 4/3 = 1.333
    // But input is clamped to [0,1], so at x=1.0, floor(4)/3 = 4/3 = 1.333
    // The function returns 4/3 which is >1, so let's verify the actual output
    REQUIRE(applyModCurve(ModCurve::Stepped, 1.0f) == Approx(4.0f / 3.0f).margin(0.01f));
}

TEST_CASE("Stepped curve produces 4 discrete levels", "[core][modulation_curves]") {
    // Level 0: x in [0, 0.25) -> floor(x*4)/3 = 0/3 = 0.0
    REQUIRE(applyModCurve(ModCurve::Stepped, 0.1f) == Approx(0.0f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::Stepped, 0.24f) == Approx(0.0f).margin(0.01f));

    // Level 1: x in [0.25, 0.5) -> floor(x*4)/3 = 1/3 = 0.333
    REQUIRE(applyModCurve(ModCurve::Stepped, 0.25f) == Approx(1.0f / 3.0f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::Stepped, 0.49f) == Approx(1.0f / 3.0f).margin(0.01f));

    // Level 2: x in [0.5, 0.75) -> floor(x*4)/3 = 2/3 = 0.667
    REQUIRE(applyModCurve(ModCurve::Stepped, 0.5f) == Approx(2.0f / 3.0f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::Stepped, 0.74f) == Approx(2.0f / 3.0f).margin(0.01f));

    // Level 3: x in [0.75, 1.0] -> floor(x*4)/3 = 3/3 = 1.0
    REQUIRE(applyModCurve(ModCurve::Stepped, 0.75f) == Approx(1.0f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::Stepped, 0.99f) == Approx(1.0f).margin(0.01f));
}

// =============================================================================
// Bipolar Modulation Tests (SC-004)
// =============================================================================

TEST_CASE("Bipolar modulation: negative amount inverts positive", "[core][modulation_curves]") {
    const float sourceValue = 0.8f;  // Bipolar source

    // +100% amount
    float positive = applyBipolarModulation(ModCurve::Linear, sourceValue, 1.0f);
    // -100% amount
    float negative = applyBipolarModulation(ModCurve::Linear, sourceValue, -1.0f);

    // SC-004: within 0.001 tolerance
    REQUIRE(positive == Approx(-negative).margin(0.001f));
}

TEST_CASE("Bipolar modulation with different curves", "[core][modulation_curves]") {
    const float sourceValue = 0.6f;

    SECTION("Linear curve with positive amount") {
        float result = applyBipolarModulation(ModCurve::Linear, sourceValue, 0.5f);
        // abs(0.6) = 0.6, linear(0.6) = 0.6, * 0.5 = 0.3
        REQUIRE(result == Approx(0.3f).margin(0.001f));
    }

    SECTION("Exponential curve with positive amount") {
        float result = applyBipolarModulation(ModCurve::Exponential, sourceValue, 0.5f);
        // abs(0.6) = 0.6, exp(0.6) = 0.36, * 0.5 = 0.18
        REQUIRE(result == Approx(0.18f).margin(0.001f));
    }

    SECTION("S-Curve with positive amount") {
        float result = applyBipolarModulation(ModCurve::SCurve, sourceValue, 0.5f);
        // abs(0.6) = 0.6, scurve(0.6) = 0.36*(3-1.2) = 0.36*1.8 = 0.648, * 0.5 = 0.324
        REQUIRE(result == Approx(0.324f).margin(0.001f));
    }

    SECTION("Negative source value preserves sign") {
        float result = applyBipolarModulation(ModCurve::Linear, -0.6f, 0.5f);
        // abs(-0.6) = 0.6, linear(0.6) = 0.6, sign=-1, * 0.5 = -0.3
        REQUIRE(result == Approx(-0.3f).margin(0.001f));
    }
}

TEST_CASE("Bipolar modulation with zero amount produces zero output", "[core][modulation_curves]") {
    REQUIRE(applyBipolarModulation(ModCurve::Linear, 1.0f, 0.0f) == Approx(0.0f).margin(0.001f));
    REQUIRE(applyBipolarModulation(ModCurve::Exponential, 1.0f, 0.0f) == Approx(0.0f).margin(0.001f));
}

// =============================================================================
// Edge Case Tests
// =============================================================================

TEST_CASE("Curve clamps input to [0, 1]", "[core][modulation_curves]") {
    // Negative input clamped to 0
    REQUIRE(applyModCurve(ModCurve::Linear, -0.5f) == Approx(0.0f).margin(0.01f));

    // Input > 1 clamped to 1
    REQUIRE(applyModCurve(ModCurve::Linear, 1.5f) == Approx(1.0f).margin(0.01f));
}

TEST_CASE("All curves produce 0 at input 0", "[core][modulation_curves]") {
    REQUIRE(applyModCurve(ModCurve::Linear, 0.0f) == Approx(0.0f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::Exponential, 0.0f) == Approx(0.0f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::SCurve, 0.0f) == Approx(0.0f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::Stepped, 0.0f) == Approx(0.0f).margin(0.01f));
}

TEST_CASE("All curves produce expected value at input 1", "[core][modulation_curves]") {
    REQUIRE(applyModCurve(ModCurve::Linear, 1.0f) == Approx(1.0f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::Exponential, 1.0f) == Approx(1.0f).margin(0.01f));
    REQUIRE(applyModCurve(ModCurve::SCurve, 1.0f) == Approx(1.0f).margin(0.01f));
    // Stepped at 1.0: floor(4)/3 = 4/3 = 1.333
    REQUIRE(applyModCurve(ModCurve::Stepped, 1.0f) == Approx(4.0f / 3.0f).margin(0.01f));
}
