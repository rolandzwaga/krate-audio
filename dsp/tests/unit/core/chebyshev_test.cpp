// ==============================================================================
// Unit Tests: Chebyshev Polynomial Library
// ==============================================================================
// Tests for core/chebyshev.h - Chebyshev polynomials of the first kind for
// harmonic control in waveshaping.
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline (pure functions, independently testable)
// - Principle XII: Test-First Development
//
// Reference: specs/049-chebyshev-polynomials/spec.md
// ==============================================================================

#include <krate/dsp/core/chebyshev.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <limits>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Mathematical Constants
// =============================================================================

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

// =============================================================================
// US1: Individual Chebyshev Polynomials T1-T8
// =============================================================================

// -----------------------------------------------------------------------------
// T1(x) = x (FR-001)
// -----------------------------------------------------------------------------

TEST_CASE("Chebyshev::T1(x) returns x (identity/fundamental)", "[chebyshev][core][US1][T1]") {
    // FR-001: T1(x) = x

    SECTION("T1 is identity function") {
        REQUIRE(Chebyshev::T1(0.0f) == 0.0f);
        REQUIRE(Chebyshev::T1(0.5f) == 0.5f);
        REQUIRE(Chebyshev::T1(-0.5f) == -0.5f);
        REQUIRE(Chebyshev::T1(1.0f) == 1.0f);
        REQUIRE(Chebyshev::T1(-1.0f) == -1.0f);
    }

    SECTION("T1 returns exact input") {
        std::vector<float> testValues = {-0.9f, -0.7f, -0.3f, 0.0f, 0.2f, 0.6f, 0.8f};
        for (float x : testValues) {
            REQUIRE(Chebyshev::T1(x) == x);
        }
    }
}

// -----------------------------------------------------------------------------
// T2(x) = 2x^2 - 1 (FR-002)
// -----------------------------------------------------------------------------

TEST_CASE("Chebyshev::T2(x) returns 2x^2 - 1", "[chebyshev][core][US1][T2]") {
    // FR-002: T2(x) = 2x^2 - 1

    SECTION("T2 at key points") {
        // T2(0) = 2*0 - 1 = -1
        REQUIRE(Chebyshev::T2(0.0f) == Approx(-1.0f));
        // T2(1) = 2*1 - 1 = 1
        REQUIRE(Chebyshev::T2(1.0f) == Approx(1.0f));
        // T2(-1) = 2*1 - 1 = 1
        REQUIRE(Chebyshev::T2(-1.0f) == Approx(1.0f));
        // T2(0.5) = 2*0.25 - 1 = -0.5
        REQUIRE(Chebyshev::T2(0.5f) == Approx(-0.5f));
        // T2(-0.5) = 2*0.25 - 1 = -0.5
        REQUIRE(Chebyshev::T2(-0.5f) == Approx(-0.5f));
    }

    SECTION("T2 matches formula 2x^2 - 1") {
        std::vector<float> testValues = {-0.9f, -0.6f, -0.3f, 0.0f, 0.2f, 0.4f, 0.7f, 0.95f};
        for (float x : testValues) {
            float expected = 2.0f * x * x - 1.0f;
            REQUIRE(Chebyshev::T2(x) == Approx(expected).margin(1e-6f));
        }
    }
}

// -----------------------------------------------------------------------------
// T3(x) = 4x^3 - 3x (FR-003)
// -----------------------------------------------------------------------------

TEST_CASE("Chebyshev::T3(x) returns 4x^3 - 3x", "[chebyshev][core][US1][T3]") {
    // FR-003: T3(x) = 4x^3 - 3x

    SECTION("T3 at key points") {
        // T3(0) = 0
        REQUIRE(Chebyshev::T3(0.0f) == Approx(0.0f).margin(1e-6f));
        // T3(1) = 4 - 3 = 1
        REQUIRE(Chebyshev::T3(1.0f) == Approx(1.0f));
        // T3(-1) = -4 + 3 = -1
        REQUIRE(Chebyshev::T3(-1.0f) == Approx(-1.0f));
        // T3(0.5) = 4*0.125 - 1.5 = 0.5 - 1.5 = -1
        REQUIRE(Chebyshev::T3(0.5f) == Approx(-1.0f));
    }

    SECTION("T3 matches formula 4x^3 - 3x") {
        std::vector<float> testValues = {-0.9f, -0.6f, -0.3f, 0.0f, 0.2f, 0.4f, 0.7f, 0.95f};
        for (float x : testValues) {
            float expected = 4.0f * x * x * x - 3.0f * x;
            REQUIRE(Chebyshev::T3(x) == Approx(expected).margin(1e-6f));
        }
    }
}

// -----------------------------------------------------------------------------
// T4(x) = 8x^4 - 8x^2 + 1 (FR-004)
// -----------------------------------------------------------------------------

TEST_CASE("Chebyshev::T4(x) returns 8x^4 - 8x^2 + 1", "[chebyshev][core][US1][T4]") {
    // FR-004: T4(x) = 8x^4 - 8x^2 + 1

    SECTION("T4 at key points") {
        // T4(0) = 0 - 0 + 1 = 1
        REQUIRE(Chebyshev::T4(0.0f) == Approx(1.0f));
        // T4(1) = 8 - 8 + 1 = 1
        REQUIRE(Chebyshev::T4(1.0f) == Approx(1.0f));
        // T4(-1) = 8 - 8 + 1 = 1
        REQUIRE(Chebyshev::T4(-1.0f) == Approx(1.0f));
    }

    SECTION("T4 matches formula 8x^4 - 8x^2 + 1") {
        std::vector<float> testValues = {-0.9f, -0.6f, -0.3f, 0.0f, 0.2f, 0.4f, 0.7f, 0.95f};
        for (float x : testValues) {
            float x2 = x * x;
            float x4 = x2 * x2;
            float expected = 8.0f * x4 - 8.0f * x2 + 1.0f;
            REQUIRE(Chebyshev::T4(x) == Approx(expected).margin(1e-6f));
        }
    }
}

// -----------------------------------------------------------------------------
// T5(x) = 16x^5 - 20x^3 + 5x (FR-005)
// -----------------------------------------------------------------------------

TEST_CASE("Chebyshev::T5(x) returns 16x^5 - 20x^3 + 5x", "[chebyshev][core][US1][T5]") {
    // FR-005: T5(x) = 16x^5 - 20x^3 + 5x

    SECTION("T5 at key points") {
        // T5(0) = 0
        REQUIRE(Chebyshev::T5(0.0f) == Approx(0.0f).margin(1e-6f));
        // T5(1) = 16 - 20 + 5 = 1
        REQUIRE(Chebyshev::T5(1.0f) == Approx(1.0f));
        // T5(-1) = -16 + 20 - 5 = -1
        REQUIRE(Chebyshev::T5(-1.0f) == Approx(-1.0f));
    }

    SECTION("T5 matches formula 16x^5 - 20x^3 + 5x") {
        std::vector<float> testValues = {-0.9f, -0.6f, -0.3f, 0.0f, 0.2f, 0.4f, 0.7f, 0.95f};
        for (float x : testValues) {
            float x2 = x * x;
            float x3 = x2 * x;
            float x5 = x3 * x2;
            float expected = 16.0f * x5 - 20.0f * x3 + 5.0f * x;
            REQUIRE(Chebyshev::T5(x) == Approx(expected).margin(1e-6f));
        }
    }
}

// -----------------------------------------------------------------------------
// T6(x) = 32x^6 - 48x^4 + 18x^2 - 1 (FR-006)
// -----------------------------------------------------------------------------

TEST_CASE("Chebyshev::T6(x) returns 32x^6 - 48x^4 + 18x^2 - 1", "[chebyshev][core][US1][T6]") {
    // FR-006: T6(x) = 32x^6 - 48x^4 + 18x^2 - 1

    SECTION("T6 at key points") {
        // T6(0) = -1
        REQUIRE(Chebyshev::T6(0.0f) == Approx(-1.0f));
        // T6(1) = 32 - 48 + 18 - 1 = 1
        REQUIRE(Chebyshev::T6(1.0f) == Approx(1.0f));
        // T6(-1) = 32 - 48 + 18 - 1 = 1
        REQUIRE(Chebyshev::T6(-1.0f) == Approx(1.0f));
    }

    SECTION("T6 matches formula 32x^6 - 48x^4 + 18x^2 - 1") {
        std::vector<float> testValues = {-0.9f, -0.6f, -0.3f, 0.0f, 0.2f, 0.4f, 0.7f, 0.95f};
        for (float x : testValues) {
            float x2 = x * x;
            float x4 = x2 * x2;
            float x6 = x4 * x2;
            float expected = 32.0f * x6 - 48.0f * x4 + 18.0f * x2 - 1.0f;
            REQUIRE(Chebyshev::T6(x) == Approx(expected).margin(1e-5f));
        }
    }
}

// -----------------------------------------------------------------------------
// T7(x) = 64x^7 - 112x^5 + 56x^3 - 7x (FR-007)
// -----------------------------------------------------------------------------

TEST_CASE("Chebyshev::T7(x) returns 64x^7 - 112x^5 + 56x^3 - 7x", "[chebyshev][core][US1][T7]") {
    // FR-007: T7(x) = 64x^7 - 112x^5 + 56x^3 - 7x

    SECTION("T7 at key points") {
        // T7(0) = 0
        REQUIRE(Chebyshev::T7(0.0f) == Approx(0.0f).margin(1e-6f));
        // T7(1) = 64 - 112 + 56 - 7 = 1
        REQUIRE(Chebyshev::T7(1.0f) == Approx(1.0f));
        // T7(-1) = -64 + 112 - 56 + 7 = -1
        REQUIRE(Chebyshev::T7(-1.0f) == Approx(-1.0f));
    }

    SECTION("T7 matches formula 64x^7 - 112x^5 + 56x^3 - 7x") {
        std::vector<float> testValues = {-0.9f, -0.6f, -0.3f, 0.0f, 0.2f, 0.4f, 0.7f, 0.95f};
        for (float x : testValues) {
            float x2 = x * x;
            float x3 = x2 * x;
            float x5 = x3 * x2;
            float x7 = x5 * x2;
            float expected = 64.0f * x7 - 112.0f * x5 + 56.0f * x3 - 7.0f * x;
            REQUIRE(Chebyshev::T7(x) == Approx(expected).margin(1e-5f));
        }
    }
}

// -----------------------------------------------------------------------------
// T8(x) = 128x^8 - 256x^6 + 160x^4 - 32x^2 + 1 (FR-008)
// -----------------------------------------------------------------------------

TEST_CASE("Chebyshev::T8(x) returns 128x^8 - 256x^6 + 160x^4 - 32x^2 + 1", "[chebyshev][core][US1][T8]") {
    // FR-008: T8(x) = 128x^8 - 256x^6 + 160x^4 - 32x^2 + 1

    SECTION("T8 at key points") {
        // T8(0) = 1
        REQUIRE(Chebyshev::T8(0.0f) == Approx(1.0f));
        // T8(1) = 128 - 256 + 160 - 32 + 1 = 1
        REQUIRE(Chebyshev::T8(1.0f) == Approx(1.0f));
        // T8(-1) = 128 - 256 + 160 - 32 + 1 = 1
        REQUIRE(Chebyshev::T8(-1.0f) == Approx(1.0f));
    }

    SECTION("T8 matches formula 128x^8 - 256x^6 + 160x^4 - 32x^2 + 1") {
        std::vector<float> testValues = {-0.9f, -0.6f, -0.3f, 0.0f, 0.2f, 0.4f, 0.7f, 0.95f};
        for (float x : testValues) {
            float x2 = x * x;
            float x4 = x2 * x2;
            float x6 = x4 * x2;
            float x8 = x4 * x4;
            float expected = 128.0f * x8 - 256.0f * x6 + 160.0f * x4 - 32.0f * x2 + 1.0f;
            REQUIRE(Chebyshev::T8(x) == Approx(expected).margin(1e-4f));
        }
    }
}

// -----------------------------------------------------------------------------
// T_n(1) = 1 for all n (Property test)
// -----------------------------------------------------------------------------

TEST_CASE("All Chebyshev T_n(1) = 1", "[chebyshev][core][US1][property]") {
    // Property: T_n(1) = 1 for all n

    REQUIRE(Chebyshev::T1(1.0f) == Approx(1.0f));
    REQUIRE(Chebyshev::T2(1.0f) == Approx(1.0f));
    REQUIRE(Chebyshev::T3(1.0f) == Approx(1.0f));
    REQUIRE(Chebyshev::T4(1.0f) == Approx(1.0f));
    REQUIRE(Chebyshev::T5(1.0f) == Approx(1.0f));
    REQUIRE(Chebyshev::T6(1.0f) == Approx(1.0f));
    REQUIRE(Chebyshev::T7(1.0f) == Approx(1.0f));
    REQUIRE(Chebyshev::T8(1.0f) == Approx(1.0f));
}

// -----------------------------------------------------------------------------
// T_n(cos(theta)) = cos(n*theta) - Harmonic property (SC-003)
// -----------------------------------------------------------------------------

TEST_CASE("Chebyshev T_n(cos(theta)) = cos(n*theta) harmonic property", "[chebyshev][core][US1][SC-003]") {
    // SC-003: For sine wave input cos(theta), T_n produces cos(n*theta) within 1e-5 tolerance

    // Test at multiple angles
    std::vector<float> thetas = {0.0f, kPi / 6, kPi / 4, kPi / 3, kPi / 2, kPi, 3 * kPi / 2, kTwoPi};

    SECTION("T1(cos(theta)) = cos(1*theta)") {
        for (float theta : thetas) {
            float x = std::cos(theta);
            float expected = std::cos(1.0f * theta);
            REQUIRE(Chebyshev::T1(x) == Approx(expected).margin(1e-5f));
        }
    }

    SECTION("T2(cos(theta)) = cos(2*theta)") {
        for (float theta : thetas) {
            float x = std::cos(theta);
            float expected = std::cos(2.0f * theta);
            REQUIRE(Chebyshev::T2(x) == Approx(expected).margin(1e-5f));
        }
    }

    SECTION("T3(cos(theta)) = cos(3*theta)") {
        for (float theta : thetas) {
            float x = std::cos(theta);
            float expected = std::cos(3.0f * theta);
            REQUIRE(Chebyshev::T3(x) == Approx(expected).margin(1e-5f));
        }
    }

    SECTION("T4(cos(theta)) = cos(4*theta)") {
        for (float theta : thetas) {
            float x = std::cos(theta);
            float expected = std::cos(4.0f * theta);
            REQUIRE(Chebyshev::T4(x) == Approx(expected).margin(1e-5f));
        }
    }

    SECTION("T5(cos(theta)) = cos(5*theta)") {
        for (float theta : thetas) {
            float x = std::cos(theta);
            float expected = std::cos(5.0f * theta);
            REQUIRE(Chebyshev::T5(x) == Approx(expected).margin(1e-5f));
        }
    }

    SECTION("T6(cos(theta)) = cos(6*theta)") {
        for (float theta : thetas) {
            float x = std::cos(theta);
            float expected = std::cos(6.0f * theta);
            REQUIRE(Chebyshev::T6(x) == Approx(expected).margin(1e-5f));
        }
    }

    SECTION("T7(cos(theta)) = cos(7*theta)") {
        for (float theta : thetas) {
            float x = std::cos(theta);
            float expected = std::cos(7.0f * theta);
            REQUIRE(Chebyshev::T7(x) == Approx(expected).margin(1e-5f));
        }
    }

    SECTION("T8(cos(theta)) = cos(8*theta)") {
        for (float theta : thetas) {
            float x = std::cos(theta);
            float expected = std::cos(8.0f * theta);
            REQUIRE(Chebyshev::T8(x) == Approx(expected).margin(1e-5f));
        }
    }
}

// =============================================================================
// US2: Recursive Tn(x, n) Function
// =============================================================================

TEST_CASE("Chebyshev::Tn(x, n) matches T1-T8 for n=1..8", "[chebyshev][core][US2][SC-002]") {
    // SC-002: Tn(x, n) matches individual T1-T8 functions within 1e-7 relative tolerance

    std::vector<float> testValues = {-0.9f, -0.5f, 0.0f, 0.5f, 0.9f};

    SECTION("Tn(x, 1) matches T1(x)") {
        for (float x : testValues) {
            REQUIRE(Chebyshev::Tn(x, 1) == Approx(Chebyshev::T1(x)).margin(1e-7f));
        }
    }

    SECTION("Tn(x, 2) matches T2(x)") {
        for (float x : testValues) {
            REQUIRE(Chebyshev::Tn(x, 2) == Approx(Chebyshev::T2(x)).margin(1e-7f));
        }
    }

    SECTION("Tn(x, 3) matches T3(x)") {
        for (float x : testValues) {
            REQUIRE(Chebyshev::Tn(x, 3) == Approx(Chebyshev::T3(x)).margin(1e-7f));
        }
    }

    SECTION("Tn(x, 4) matches T4(x)") {
        for (float x : testValues) {
            REQUIRE(Chebyshev::Tn(x, 4) == Approx(Chebyshev::T4(x)).margin(1e-7f));
        }
    }

    SECTION("Tn(x, 5) matches T5(x)") {
        for (float x : testValues) {
            REQUIRE(Chebyshev::Tn(x, 5) == Approx(Chebyshev::T5(x)).margin(1e-7f));
        }
    }

    SECTION("Tn(x, 6) matches T6(x)") {
        for (float x : testValues) {
            REQUIRE(Chebyshev::Tn(x, 6) == Approx(Chebyshev::T6(x)).margin(1e-6f));
        }
    }

    SECTION("Tn(x, 7) matches T7(x)") {
        for (float x : testValues) {
            REQUIRE(Chebyshev::Tn(x, 7) == Approx(Chebyshev::T7(x)).margin(1e-6f));
        }
    }

    SECTION("Tn(x, 8) matches T8(x)") {
        for (float x : testValues) {
            REQUIRE(Chebyshev::Tn(x, 8) == Approx(Chebyshev::T8(x)).margin(1e-5f));
        }
    }
}

TEST_CASE("Chebyshev::Tn(x, 0) returns T0 = 1.0", "[chebyshev][core][US2][FR-010]") {
    // FR-010: Tn(x, 0) returns 1.0 (T0 = 1)

    REQUIRE(Chebyshev::Tn(0.0f, 0) == Approx(1.0f));
    REQUIRE(Chebyshev::Tn(0.5f, 0) == Approx(1.0f));
    REQUIRE(Chebyshev::Tn(-0.5f, 0) == Approx(1.0f));
    REQUIRE(Chebyshev::Tn(1.0f, 0) == Approx(1.0f));
    REQUIRE(Chebyshev::Tn(-1.0f, 0) == Approx(1.0f));
}

TEST_CASE("Chebyshev::Tn(x, 1) returns T1 = x", "[chebyshev][core][US2][FR-011]") {
    // FR-011: Tn(x, 1) returns x (T1 = x)

    REQUIRE(Chebyshev::Tn(0.0f, 1) == Approx(0.0f));
    REQUIRE(Chebyshev::Tn(0.5f, 1) == Approx(0.5f));
    REQUIRE(Chebyshev::Tn(-0.5f, 1) == Approx(-0.5f));
    REQUIRE(Chebyshev::Tn(1.0f, 1) == Approx(1.0f));
    REQUIRE(Chebyshev::Tn(-1.0f, 1) == Approx(-1.0f));
}

TEST_CASE("Chebyshev::Tn(x, n) with negative n returns T0 = 1.0", "[chebyshev][core][US2][FR-012]") {
    // FR-012: Negative n values clamped to 0, returning T0 = 1.0

    REQUIRE(Chebyshev::Tn(0.5f, -1) == Approx(1.0f));
    REQUIRE(Chebyshev::Tn(0.5f, -5) == Approx(1.0f));
    REQUIRE(Chebyshev::Tn(0.5f, -100) == Approx(1.0f));
    REQUIRE(Chebyshev::Tn(-0.3f, -1) == Approx(1.0f));
}

TEST_CASE("Chebyshev::Tn(cos(theta), 10) produces cos(10*theta)", "[chebyshev][core][US2]") {
    // Test arbitrary high order n=10

    std::vector<float> thetas = {0.0f, kPi / 6, kPi / 4, kPi / 3, kPi / 2, kPi};

    for (float theta : thetas) {
        float x = std::cos(theta);
        float expected = std::cos(10.0f * theta);
        REQUIRE(Chebyshev::Tn(x, 10) == Approx(expected).margin(1e-4f));
    }
}

TEST_CASE("Chebyshev::Tn(x, 20) arbitrary high order", "[chebyshev][core][US2]") {
    // Test n=20 - higher order polynomial

    float x = std::cos(kPi / 4);  // cos(45 degrees)
    float expected = std::cos(20.0f * kPi / 4);  // cos(20 * 45 degrees) = cos(900 degrees) = cos(180 degrees) = -1
    REQUIRE(Chebyshev::Tn(x, 20) == Approx(expected).margin(1e-3f));

    // At x=1, Tn(1, 20) should be 1
    REQUIRE(Chebyshev::Tn(1.0f, 20) == Approx(1.0f).margin(1e-5f));
}

// =============================================================================
// US3: Harmonic Mix Function
// =============================================================================

TEST_CASE("Chebyshev::harmonicMix with single non-zero weight matches Tn", "[chebyshev][core][US3][SC-006]") {
    // SC-006: harmonicMix with single non-zero weight matches corresponding Tn

    std::vector<float> testValues = {-0.9f, -0.5f, 0.0f, 0.5f, 0.9f};

    SECTION("weights[0]=1 (T1 only) matches T1") {
        float weights[8] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        for (float x : testValues) {
            REQUIRE(Chebyshev::harmonicMix(x, weights, 8) == Approx(Chebyshev::T1(x)).margin(1e-6f));
        }
    }

    SECTION("weights[1]=1 (T2 only) matches T2") {
        float weights[8] = {0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        for (float x : testValues) {
            REQUIRE(Chebyshev::harmonicMix(x, weights, 8) == Approx(Chebyshev::T2(x)).margin(1e-6f));
        }
    }

    SECTION("weights[2]=1 (T3 only) matches T3") {
        float weights[8] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        for (float x : testValues) {
            REQUIRE(Chebyshev::harmonicMix(x, weights, 8) == Approx(Chebyshev::T3(x)).margin(1e-6f));
        }
    }

    SECTION("weights[7]=1 (T8 only) matches T8") {
        float weights[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        for (float x : testValues) {
            REQUIRE(Chebyshev::harmonicMix(x, weights, 8) == Approx(Chebyshev::T8(x)).margin(1e-5f));
        }
    }
}

TEST_CASE("Chebyshev::harmonicMix with multiple weights produces weighted sum", "[chebyshev][core][US3]") {
    std::vector<float> testValues = {-0.9f, -0.5f, 0.0f, 0.5f, 0.9f};

    SECTION("0.5*T1 + 0.3*T2 + 0.2*T3") {
        float weights[8] = {0.5f, 0.3f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        for (float x : testValues) {
            float expected = 0.5f * Chebyshev::T1(x) + 0.3f * Chebyshev::T2(x) + 0.2f * Chebyshev::T3(x);
            REQUIRE(Chebyshev::harmonicMix(x, weights, 8) == Approx(expected).margin(1e-5f));
        }
    }

    SECTION("equal weights 1.0 each for T1-T4") {
        float weights[8] = {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        for (float x : testValues) {
            float expected = Chebyshev::T1(x) + Chebyshev::T2(x) + Chebyshev::T3(x) + Chebyshev::T4(x);
            REQUIRE(Chebyshev::harmonicMix(x, weights, 4) == Approx(expected).margin(1e-5f));
        }
    }
}

TEST_CASE("Chebyshev::harmonicMix with all zero weights returns 0.0", "[chebyshev][core][US3]") {
    float weights[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    REQUIRE(Chebyshev::harmonicMix(0.5f, weights, 8) == Approx(0.0f).margin(1e-7f));
    REQUIRE(Chebyshev::harmonicMix(-0.5f, weights, 8) == Approx(0.0f).margin(1e-7f));
    REQUIRE(Chebyshev::harmonicMix(1.0f, weights, 8) == Approx(0.0f).margin(1e-7f));
}

TEST_CASE("Chebyshev::harmonicMix with null weights returns 0.0", "[chebyshev][core][US3][FR-016]") {
    // FR-016: null weights pointer returns 0.0

    REQUIRE(Chebyshev::harmonicMix(0.5f, nullptr, 8) == 0.0f);
    REQUIRE(Chebyshev::harmonicMix(-0.5f, nullptr, 4) == 0.0f);
    REQUIRE(Chebyshev::harmonicMix(1.0f, nullptr, 0) == 0.0f);
}

TEST_CASE("Chebyshev::harmonicMix with numHarmonics=0 returns 0.0", "[chebyshev][core][US3][FR-015]") {
    // FR-015: numHarmonics=0 returns 0.0

    float weights[8] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

    REQUIRE(Chebyshev::harmonicMix(0.5f, weights, 0) == 0.0f);
    REQUIRE(Chebyshev::harmonicMix(-0.5f, weights, 0) == 0.0f);
}

TEST_CASE("Chebyshev::harmonicMix clamps numHarmonics>32 to 32", "[chebyshev][core][US3][FR-013]") {
    // FR-013: numHarmonics > kMaxHarmonics clamped to kMaxHarmonics (32)

    // Create weights array with 64 elements
    float weights[64];
    for (int i = 0; i < 64; ++i) {
        weights[i] = (i < 32) ? 0.1f : 1.0f;  // Higher weights after 32 should be ignored
    }

    // With numHarmonics=64, it should clamp to 32 and ignore weights[32..63]
    float result64 = Chebyshev::harmonicMix(0.5f, weights, 64);
    float result32 = Chebyshev::harmonicMix(0.5f, weights, 32);

    REQUIRE(result64 == Approx(result32).margin(1e-6f));
}

TEST_CASE("Chebyshev::harmonicMix weights[0]=T1, weights[1]=T2 mapping", "[chebyshev][core][US3][FR-017]") {
    // FR-017: weights[0] = T1, weights[1] = T2, ..., weights[n-1] = Tn

    float x = 0.7f;

    // Single T3 weight
    float weightsT3[3] = {0.0f, 0.0f, 1.0f};
    REQUIRE(Chebyshev::harmonicMix(x, weightsT3, 3) == Approx(Chebyshev::T3(x)).margin(1e-6f));

    // Single T5 weight
    float weightsT5[5] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    REQUIRE(Chebyshev::harmonicMix(x, weightsT5, 5) == Approx(Chebyshev::T5(x)).margin(1e-6f));
}

// =============================================================================
// US4: Performance and Attributes
// =============================================================================

TEST_CASE("Chebyshev functions are noexcept", "[chebyshev][core][US4][FR-019]") {
    // FR-019: All functions must be noexcept

    static_assert(noexcept(Chebyshev::T1(0.0f)), "T1 must be noexcept");
    static_assert(noexcept(Chebyshev::T2(0.0f)), "T2 must be noexcept");
    static_assert(noexcept(Chebyshev::T3(0.0f)), "T3 must be noexcept");
    static_assert(noexcept(Chebyshev::T4(0.0f)), "T4 must be noexcept");
    static_assert(noexcept(Chebyshev::T5(0.0f)), "T5 must be noexcept");
    static_assert(noexcept(Chebyshev::T6(0.0f)), "T6 must be noexcept");
    static_assert(noexcept(Chebyshev::T7(0.0f)), "T7 must be noexcept");
    static_assert(noexcept(Chebyshev::T8(0.0f)), "T8 must be noexcept");
    static_assert(noexcept(Chebyshev::Tn(0.0f, 1)), "Tn must be noexcept");

    float weights[4] = {0.0f};
    static_assert(noexcept(Chebyshev::harmonicMix(0.0f, weights, 4)), "harmonicMix must be noexcept");

    REQUIRE(true);  // Passes if compilation succeeds
}

TEST_CASE("Chebyshev functions are constexpr", "[chebyshev][core][US4][FR-018]") {
    // FR-018: All individual polynomial functions must be constexpr

    constexpr float t1 = Chebyshev::T1(0.5f);
    constexpr float t2 = Chebyshev::T2(0.5f);
    constexpr float t3 = Chebyshev::T3(0.5f);
    constexpr float t4 = Chebyshev::T4(0.5f);
    constexpr float t5 = Chebyshev::T5(0.5f);
    constexpr float t6 = Chebyshev::T6(0.5f);
    constexpr float t7 = Chebyshev::T7(0.5f);
    constexpr float t8 = Chebyshev::T8(0.5f);
    constexpr float tn = Chebyshev::Tn(0.5f, 4);

    // Verify values are correct (compile-time evaluation)
    REQUIRE(t1 == Approx(0.5f));
    REQUIRE(t2 == Approx(-0.5f));
    REQUIRE(tn == Approx(Chebyshev::T4(0.5f)));
}

TEST_CASE("Chebyshev 1M sample stability test", "[chebyshev][core][US4][SC-004]") {
    // SC-004: Process 1 million samples without unexpected NaN/Inf

    constexpr size_t numSamples = 1000000;
    size_t nanCount = 0;
    size_t infCount = 0;

    for (size_t i = 0; i < numSamples; ++i) {
        // Generate input in [-1, 1] range
        float x = -1.0f + 2.0f * static_cast<float>(i) / static_cast<float>(numSamples);

        // Test all functions
        float out = Chebyshev::T1(x);
        if (std::isnan(out)) nanCount++;
        if (std::isinf(out)) infCount++;

        out = Chebyshev::T2(x);
        if (std::isnan(out)) nanCount++;
        if (std::isinf(out)) infCount++;

        out = Chebyshev::T3(x);
        if (std::isnan(out)) nanCount++;
        if (std::isinf(out)) infCount++;

        out = Chebyshev::T4(x);
        if (std::isnan(out)) nanCount++;
        if (std::isinf(out)) infCount++;

        out = Chebyshev::T5(x);
        if (std::isnan(out)) nanCount++;
        if (std::isinf(out)) infCount++;

        out = Chebyshev::T6(x);
        if (std::isnan(out)) nanCount++;
        if (std::isinf(out)) infCount++;

        out = Chebyshev::T7(x);
        if (std::isnan(out)) nanCount++;
        if (std::isinf(out)) infCount++;

        out = Chebyshev::T8(x);
        if (std::isnan(out)) nanCount++;
        if (std::isinf(out)) infCount++;
    }

    REQUIRE(nanCount == 0);
    REQUIRE(infCount == 0);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("Chebyshev functions handle NaN input", "[chebyshev][core][edge][FR-020]") {
    // FR-020: NaN input propagates to output

    float nan = std::numeric_limits<float>::quiet_NaN();

    REQUIRE(std::isnan(Chebyshev::T1(nan)));
    REQUIRE(std::isnan(Chebyshev::T2(nan)));
    REQUIRE(std::isnan(Chebyshev::T3(nan)));
    REQUIRE(std::isnan(Chebyshev::T4(nan)));
    REQUIRE(std::isnan(Chebyshev::T5(nan)));
    REQUIRE(std::isnan(Chebyshev::T6(nan)));
    REQUIRE(std::isnan(Chebyshev::T7(nan)));
    REQUIRE(std::isnan(Chebyshev::T8(nan)));
    REQUIRE(std::isnan(Chebyshev::Tn(nan, 5)));
}

TEST_CASE("Chebyshev functions handle infinity input", "[chebyshev][core][edge]") {
    float posInf = std::numeric_limits<float>::infinity();
    float negInf = -std::numeric_limits<float>::infinity();

    // Infinity in polynomial produces infinity (but we don't crash)
    // T1(inf) = inf
    REQUIRE(std::isinf(Chebyshev::T1(posInf)));
    REQUIRE(std::isinf(Chebyshev::T1(negInf)));

    // Higher order polynomials with inf also produce inf or -inf
    // The exact sign depends on leading coefficient and input sign
    float t2PosInf = Chebyshev::T2(posInf);
    float t2NegInf = Chebyshev::T2(negInf);
    REQUIRE((std::isinf(t2PosInf) || std::isnan(t2PosInf)));
    REQUIRE((std::isinf(t2NegInf) || std::isnan(t2NegInf)));
}

TEST_CASE("Chebyshev functions handle out-of-range input |x| > 1", "[chebyshev][core][edge]") {
    // Out-of-range inputs produce valid results but not pure harmonics

    // T1(2) = 2 (just returns input)
    REQUIRE(Chebyshev::T1(2.0f) == 2.0f);

    // T2(2) = 2*4 - 1 = 7
    REQUIRE(Chebyshev::T2(2.0f) == Approx(7.0f));

    // T3(2) = 4*8 - 6 = 26
    REQUIRE(Chebyshev::T3(2.0f) == Approx(26.0f));

    // Functions should produce finite results
    REQUIRE(std::isfinite(Chebyshev::T4(1.5f)));
    REQUIRE(std::isfinite(Chebyshev::T5(-1.5f)));
    REQUIRE(std::isfinite(Chebyshev::T6(1.2f)));
    REQUIRE(std::isfinite(Chebyshev::T7(-1.1f)));
    REQUIRE(std::isfinite(Chebyshev::T8(1.3f)));
}

TEST_CASE("Chebyshev functions handle denormal input", "[chebyshev][core][edge]") {
    float denormal = 1e-40f;

    // Denormal inputs should produce finite results close to the limit
    REQUIRE(std::isfinite(Chebyshev::T1(denormal)));
    REQUIRE(std::isfinite(Chebyshev::T2(denormal)));
    REQUIRE(std::isfinite(Chebyshev::T3(denormal)));
    REQUIRE(std::isfinite(Chebyshev::T4(denormal)));
    REQUIRE(std::isfinite(Chebyshev::T5(denormal)));
    REQUIRE(std::isfinite(Chebyshev::T6(denormal)));
    REQUIRE(std::isfinite(Chebyshev::T7(denormal)));
    REQUIRE(std::isfinite(Chebyshev::T8(denormal)));

    // T1(denormal) = denormal (identity)
    REQUIRE(Chebyshev::T1(denormal) == denormal);

    // T2(denormal) ~ -1 (since 2*denormal^2 - 1 ~ -1)
    REQUIRE(Chebyshev::T2(denormal) == Approx(-1.0f).margin(1e-6f));
}
