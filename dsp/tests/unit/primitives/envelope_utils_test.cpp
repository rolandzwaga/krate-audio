// ==============================================================================
// Layer 1: DSP Primitive - Shared Envelope Utilities Tests
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests for extracted envelope_utils.h (constants, enums, coefficient calc)
// ==============================================================================

#include <krate/dsp/primitives/envelope_utils.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Constants Tests
// =============================================================================

TEST_CASE("envelope_utils: constants have correct values", "[primitives][envelope_utils]") {
    REQUIRE(kEnvelopeIdleThreshold == Approx(1e-4f));
    REQUIRE(kMinEnvelopeTimeMs == Approx(0.1f));
    REQUIRE(kMaxEnvelopeTimeMs == Approx(10000.0f));
    REQUIRE(kSustainSmoothTimeMs == Approx(5.0f));
    REQUIRE(kDefaultTargetRatioA == Approx(0.3f));
    REQUIRE(kDefaultTargetRatioDR == Approx(0.0001f));
    REQUIRE(kLinearTargetRatio == Approx(100.0f));
}

// =============================================================================
// Enumeration Tests
// =============================================================================

TEST_CASE("envelope_utils: EnvCurve enum values", "[primitives][envelope_utils]") {
    REQUIRE(static_cast<uint8_t>(EnvCurve::Exponential) == 0);
    REQUIRE(static_cast<uint8_t>(EnvCurve::Linear) == 1);
    REQUIRE(static_cast<uint8_t>(EnvCurve::Logarithmic) == 2);
}

TEST_CASE("envelope_utils: RetriggerMode enum values", "[primitives][envelope_utils]") {
    REQUIRE(static_cast<uint8_t>(RetriggerMode::Hard) == 0);
    REQUIRE(static_cast<uint8_t>(RetriggerMode::Legato) == 1);
}

// =============================================================================
// StageCoefficients Tests
// =============================================================================

TEST_CASE("envelope_utils: StageCoefficients default initialization", "[primitives][envelope_utils]") {
    StageCoefficients sc;
    REQUIRE(sc.coef == 0.0f);
    REQUIRE(sc.base == 0.0f);
}

// =============================================================================
// calcEnvCoefficients Tests
// =============================================================================

TEST_CASE("envelope_utils: calcEnvCoefficients produces valid coefficients for rising curve", "[primitives][envelope_utils]") {
    auto coeffs = calcEnvCoefficients(10.0f, 44100.0f, 1.0f, kDefaultTargetRatioA, true);

    // Coefficient must be in (0, 1) for convergent one-pole filter
    REQUIRE(coeffs.coef > 0.0f);
    REQUIRE(coeffs.coef < 1.0f);

    // Base must be positive for rising curve to target 1.0
    REQUIRE(coeffs.base > 0.0f);
}

TEST_CASE("envelope_utils: calcEnvCoefficients produces valid coefficients for falling curve", "[primitives][envelope_utils]") {
    auto coeffs = calcEnvCoefficients(50.0f, 44100.0f, 0.0f, kDefaultTargetRatioDR, false);

    REQUIRE(coeffs.coef > 0.0f);
    REQUIRE(coeffs.coef < 1.0f);

    // Base must be negative for falling curve to target 0.0
    REQUIRE(coeffs.base < 0.0f);
}

TEST_CASE("envelope_utils: calcEnvCoefficients clamps rate to minimum 1 sample", "[primitives][envelope_utils]") {
    // Very short time that would yield rate < 1
    auto coeffs = calcEnvCoefficients(0.001f, 44100.0f, 1.0f, kDefaultTargetRatioA, true);

    // Should still produce valid coefficients (no division by zero)
    REQUIRE(coeffs.coef > 0.0f);
    REQUIRE(coeffs.coef < 1.0f);
    REQUIRE(std::isfinite(coeffs.base));
}

TEST_CASE("envelope_utils: calcEnvCoefficients with linear target ratio", "[primitives][envelope_utils]") {
    // Linear curve uses a large target ratio (100.0) for near-linear behavior
    auto coeffs = calcEnvCoefficients(10.0f, 44100.0f, 1.0f, kLinearTargetRatio, true);

    REQUIRE(coeffs.coef > 0.0f);
    REQUIRE(coeffs.coef < 1.0f);
    REQUIRE(coeffs.base > 0.0f);
}

TEST_CASE("envelope_utils: calcEnvCoefficients one-pole converges to target", "[primitives][envelope_utils]") {
    // Simulate a rising curve from 0.0 to 1.0
    auto coeffs = calcEnvCoefficients(10.0f, 44100.0f, 1.0f, kDefaultTargetRatioA, true);

    float output = 0.0f;
    int numSamples = static_cast<int>(10.0f * 0.001f * 44100.0f); // 441 samples

    for (int i = 0; i < numSamples; ++i) {
        output = coeffs.base + output * coeffs.coef;
    }

    // After the configured time, the output should be close to the target
    REQUIRE(output > 0.8f);
    REQUIRE(output <= 1.0f + 0.01f); // May slightly overshoot with target ratio
}

// =============================================================================
// Target Ratio Helper Tests
// =============================================================================

TEST_CASE("envelope_utils: getAttackTargetRatio returns correct values", "[primitives][envelope_utils]") {
    REQUIRE(getAttackTargetRatio(EnvCurve::Exponential) == Approx(kDefaultTargetRatioA));
    REQUIRE(getAttackTargetRatio(EnvCurve::Linear) == Approx(kLinearTargetRatio));
    REQUIRE(getAttackTargetRatio(EnvCurve::Logarithmic) == Approx(kDefaultTargetRatioA));
}

TEST_CASE("envelope_utils: getDecayTargetRatio returns correct values", "[primitives][envelope_utils]") {
    REQUIRE(getDecayTargetRatio(EnvCurve::Exponential) == Approx(kDefaultTargetRatioDR));
    REQUIRE(getDecayTargetRatio(EnvCurve::Linear) == Approx(kLinearTargetRatio));
    REQUIRE(getDecayTargetRatio(EnvCurve::Logarithmic) == Approx(kDefaultTargetRatioDR));
}
