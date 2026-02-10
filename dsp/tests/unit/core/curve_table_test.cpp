// ==============================================================================
// Tests for curve_table.h (Layer 0 Core Utility)
// ==============================================================================
// Test-first: These tests MUST be written before implementation.
// Verifies power curve generation, Bezier curve generation, table lookup,
// and conversion functions between EnvCurve enum and continuous curve amounts.
// ==============================================================================

#include <krate/dsp/core/curve_table.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Power Curve Table Generation
// =============================================================================

TEST_CASE("Power curve with curveAmount=0 produces linear ramp", "[curve_table][core]") {
    std::array<float, kCurveTableSize> table{};
    generatePowerCurveTable(table, 0.0f);

    // Linear ramp: table[i] should equal i/255.0
    float maxError = 0.0f;
    for (size_t i = 0; i < kCurveTableSize; ++i) {
        float expected = static_cast<float>(i) / 255.0f;
        float error = std::abs(table[i] - expected);
        if (error > maxError) maxError = error;
    }
    REQUIRE(maxError < 1e-6f);
}

TEST_CASE("Power curve with curveAmount=+1 produces exponential shape", "[curve_table][core]") {
    std::array<float, kCurveTableSize> table{};
    generatePowerCurveTable(table, 1.0f);

    // With curveAmount=+1, exponent = 2^(1*3) = 8
    // At midpoint (i=128), phase=0.502, output = 0.502^8 ~ 0.004
    // So table[128] should be small (< 0.1)
    REQUIRE(table[128] < 0.1f);
    // Endpoints should be correct
    REQUIRE(table[0] == Approx(0.0f).margin(1e-6f));
    REQUIRE(table[255] == Approx(1.0f).margin(1e-6f));
}

TEST_CASE("Power curve with curveAmount=-1 produces logarithmic shape", "[curve_table][core]") {
    std::array<float, kCurveTableSize> table{};
    generatePowerCurveTable(table, -1.0f);

    // With curveAmount=-1, exponent = 2^(-3) = 0.125
    // At midpoint (i=128), phase=0.502, output = 0.502^0.125 ~ 0.916
    // So table[128] should be large (> 0.9)
    REQUIRE(table[128] > 0.9f);
    // Endpoints should be correct
    REQUIRE(table[0] == Approx(0.0f).margin(1e-6f));
    REQUIRE(table[255] == Approx(1.0f).margin(1e-6f));
}

TEST_CASE("Power curve with custom start/end levels", "[curve_table][core]") {
    std::array<float, kCurveTableSize> table{};
    generatePowerCurveTable(table, 0.0f, 0.2f, 0.8f);

    // Linear ramp from 0.2 to 0.8
    REQUIRE(table[0] == Approx(0.2f).margin(1e-6f));
    REQUIRE(table[255] == Approx(0.8f).margin(1e-6f));
    // Midpoint should be 0.5 for linear
    REQUIRE(table[128] == Approx(0.2f + 0.6f * (128.0f / 255.0f)).margin(1e-4f));
}

// =============================================================================
// Bezier Curve Table Generation
// =============================================================================

TEST_CASE("Bezier with handles at (1/3, 1/3) and (2/3, 2/3) produces near-linear table",
          "[curve_table][core]") {
    std::array<float, kCurveTableSize> table{};
    generateBezierCurveTable(table, 1.0f / 3.0f, 1.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f);

    // Should be approximately linear
    float maxError = 0.0f;
    for (size_t i = 0; i < kCurveTableSize; ++i) {
        float expected = static_cast<float>(i) / 255.0f;
        float error = std::abs(table[i] - expected);
        if (error > maxError) maxError = error;
    }
    // The Bezier with control points at (1/3,1/3) and (2/3,2/3) is exactly linear
    REQUIRE(maxError < 0.01f);
}

TEST_CASE("Bezier table endpoints are correct", "[curve_table][core]") {
    std::array<float, kCurveTableSize> table{};
    generateBezierCurveTable(table, 0.2f, 0.8f, 0.8f, 0.2f);

    REQUIRE(table[0] == Approx(0.0f).margin(0.01f));
    REQUIRE(table[255] == Approx(1.0f).margin(0.01f));
}

TEST_CASE("Bezier with custom start/end levels", "[curve_table][core]") {
    std::array<float, kCurveTableSize> table{};
    generateBezierCurveTable(table, 1.0f / 3.0f, 1.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f,
                             0.5f, 1.0f);

    REQUIRE(table[0] == Approx(0.5f).margin(0.01f));
    REQUIRE(table[255] == Approx(1.0f).margin(0.01f));
}

// =============================================================================
// Lookup Table Interpolation
// =============================================================================

TEST_CASE("lookupCurveTable with phase=0 returns table[0]", "[curve_table][core]") {
    std::array<float, kCurveTableSize> table{};
    generatePowerCurveTable(table, 0.5f);

    REQUIRE(lookupCurveTable(table, 0.0f) == Approx(table[0]).margin(1e-6f));
}

TEST_CASE("lookupCurveTable with phase=1 returns table[255]", "[curve_table][core]") {
    std::array<float, kCurveTableSize> table{};
    generatePowerCurveTable(table, 0.5f);

    REQUIRE(lookupCurveTable(table, 1.0f) == Approx(table[255]).margin(1e-6f));
}

TEST_CASE("lookupCurveTable interpolation is monotonic for monotonic tables", "[curve_table][core]") {
    std::array<float, kCurveTableSize> table{};
    generatePowerCurveTable(table, 0.5f);

    // Test monotonicity over many phase values
    float prev = lookupCurveTable(table, 0.0f);
    bool monotonic = true;
    for (int i = 1; i <= 1000; ++i) {
        float phase = static_cast<float>(i) / 1000.0f;
        float val = lookupCurveTable(table, phase);
        if (val < prev - 1e-7f) {
            monotonic = false;
            break;
        }
        prev = val;
    }
    REQUIRE(monotonic);
}

TEST_CASE("lookupCurveTable interpolates between table entries", "[curve_table][core]") {
    std::array<float, kCurveTableSize> table{};
    // Use linear table for predictable interpolation
    generatePowerCurveTable(table, 0.0f);

    // Phase 0.5 should yield approximately 0.5 for a linear table
    REQUIRE(lookupCurveTable(table, 0.5f) == Approx(0.5f).margin(0.005f));
}

// =============================================================================
// EnvCurve Conversion
// =============================================================================

TEST_CASE("envCurveToCurveAmount conversion", "[curve_table][core]") {
    REQUIRE(envCurveToCurveAmount(EnvCurve::Exponential) == Approx(0.7f).margin(0.01f));
    REQUIRE(envCurveToCurveAmount(EnvCurve::Linear) == Approx(0.0f).margin(0.01f));
    REQUIRE(envCurveToCurveAmount(EnvCurve::Logarithmic) == Approx(-0.7f).margin(0.01f));
}

// =============================================================================
// Bezier-to-Simple and Simple-to-Bezier Conversion
// =============================================================================

TEST_CASE("simpleCurveToBezier round-trip", "[curve_table][core]") {
    // Generate Bezier from a curve amount, then convert back
    float cp1x = 0.0f, cp1y = 0.0f, cp2x = 0.0f, cp2y = 0.0f;
    simpleCurveToBezier(0.5f, cp1x, cp1y, cp2x, cp2y);

    // Convert back to simple - Bezier approximation of power curve is approximate
    // The round-trip should preserve the general direction and magnitude
    float recovered = bezierToSimpleCurve(cp1x, cp1y, cp2x, cp2y);

    REQUIRE(recovered == Approx(0.5f).margin(0.2f));
    REQUIRE(recovered > 0.0f);  // Same sign preserved
}

TEST_CASE("simpleCurveToBezier with curveAmount=0 produces near-linear handles", "[curve_table][core]") {
    float cp1x = 0.0f, cp1y = 0.0f, cp2x = 0.0f, cp2y = 0.0f;
    simpleCurveToBezier(0.0f, cp1x, cp1y, cp2x, cp2y);

    // For linear, handles should be at (1/3, 1/3) and (2/3, 2/3) approximately
    REQUIRE(cp1x == Approx(1.0f / 3.0f).margin(0.01f));
    REQUIRE(cp1y == Approx(1.0f / 3.0f).margin(0.01f));
    REQUIRE(cp2x == Approx(2.0f / 3.0f).margin(0.01f));
    REQUIRE(cp2y == Approx(2.0f / 3.0f).margin(0.01f));
}

TEST_CASE("bezierToSimpleCurve with linear handles returns 0", "[curve_table][core]") {
    float curve = bezierToSimpleCurve(1.0f / 3.0f, 1.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f);
    REQUIRE(curve == Approx(0.0f).margin(0.05f));
}

TEST_CASE("Round-trip for negative curve amount", "[curve_table][core]") {
    float cp1x = 0.0f, cp1y = 0.0f, cp2x = 0.0f, cp2y = 0.0f;
    simpleCurveToBezier(-0.5f, cp1x, cp1y, cp2x, cp2y);

    float recovered = bezierToSimpleCurve(cp1x, cp1y, cp2x, cp2y);
    REQUIRE(recovered == Approx(-0.5f).margin(0.2f));
    REQUIRE(recovered < 0.0f);  // Same sign preserved
}
