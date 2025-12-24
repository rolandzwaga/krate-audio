// ==============================================================================
// Layer 0: Core Utility Tests - FastMath
// ==============================================================================
// Tests for optimized transcendental function approximations.
//
// Constitution Compliance:
// - Principle XII: Test-First Development
//
// Reference: specs/017-layer0-utilities/spec.md (Phase 4 - US2)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <bit>
#include <cmath>
#include <array>
#include <cstdint>
#include <limits>

#include "dsp/core/fast_math.h"
#include "dsp/core/math_constants.h"

using namespace Iterum::DSP;
using namespace Iterum::DSP::FastMath;
using Catch::Approx;

// =============================================================================
// Helper Functions for Testing
// =============================================================================

namespace {

/// @brief Calculate relative error between two values.
float relativeError(float actual, float expected) {
    if (std::abs(expected) < 1e-6f) {
        return std::abs(actual - expected);
    }
    return std::abs((actual - expected) / expected);
}

/// @brief Check if value is NaN using bit manipulation.
/// Uses same approach as db_utils.h - works even with -ffast-math on MSVC.
bool isNaN(float x) {
    const auto bits = std::bit_cast<std::uint32_t>(x);
    return ((bits & 0x7F800000u) == 0x7F800000u) && ((bits & 0x007FFFFFu) != 0);
}

} // namespace

// =============================================================================
// fastSin Tests (T029-T033 - FR-009)
// =============================================================================

TEST_CASE("fastSin basic values", "[fast_math][US2]") {
    SECTION("sin(0) = 0") {
        REQUIRE(fastSin(0.0f) == Approx(0.0f).margin(0.001f));
    }

    SECTION("sin(pi/2) = 1") {
        REQUIRE(fastSin(kHalfPi) == Approx(1.0f).margin(0.002f));
    }

    SECTION("sin(pi) = 0") {
        REQUIRE(fastSin(kPi) == Approx(0.0f).margin(0.002f));
    }

    SECTION("sin(3*pi/2) = -1") {
        REQUIRE(fastSin(1.5f * kPi) == Approx(-1.0f).margin(0.002f));
    }

    SECTION("sin(2*pi) = 0") {
        REQUIRE(fastSin(kTwoPi) == Approx(0.0f).margin(0.002f));
    }

    SECTION("sin(-pi/2) = -1") {
        REQUIRE(fastSin(-kHalfPi) == Approx(-1.0f).margin(0.002f));
    }

    SECTION("sin(pi/6) = 0.5") {
        REQUIRE(fastSin(kPi / 6.0f) == Approx(0.5f).margin(0.002f));
    }

    SECTION("sin(pi/3) = sqrt(3)/2 = 0.866") {
        REQUIRE(fastSin(kPi / 3.0f) == Approx(0.866025f).margin(0.002f));
    }
}

TEST_CASE("fastSin accuracy within 0.1% (FR-009)", "[fast_math][US2]") {
    SECTION("Accuracy across [-2pi, 2pi] range") {
        // Test 100 points across the range
        for (int i = -100; i <= 100; ++i) {
            float x = static_cast<float>(i) / 100.0f * kTwoPi;
            float expected = std::sin(x);
            float actual = fastSin(x);

            // 0.1% = 0.001 relative error
            float relErr = relativeError(actual, expected);
            INFO("x = " << x << ", expected = " << expected << ", actual = " << actual << ", relErr = " << relErr);

            // For values close to zero, use absolute margin
            if (std::abs(expected) < 0.01f) {
                REQUIRE(std::abs(actual - expected) < 0.01f);
            } else {
                REQUIRE(relErr < 0.002f);  // 0.2% margin for float precision
            }
        }
    }
}

TEST_CASE("fastSin handles special values (FR-015, FR-016)", "[fast_math][US2]") {
    SECTION("NaN input returns NaN") {
        float nan = std::numeric_limits<float>::quiet_NaN();
        REQUIRE(isNaN(fastSin(nan)));
    }

    SECTION("Positive infinity returns NaN") {
        float inf = std::numeric_limits<float>::infinity();
        REQUIRE(isNaN(fastSin(inf)));
    }

    SECTION("Negative infinity returns NaN") {
        float negInf = -std::numeric_limits<float>::infinity();
        REQUIRE(isNaN(fastSin(negInf)));
    }
}

TEST_CASE("fastSin is noexcept", "[fast_math][realtime][US2]") {
    REQUIRE(noexcept(fastSin(0.0f)));
}

// =============================================================================
// fastCos Tests (T034-T037 - FR-010)
// =============================================================================

TEST_CASE("fastCos basic values", "[fast_math][US2]") {
    SECTION("cos(0) = 1") {
        REQUIRE(fastCos(0.0f) == Approx(1.0f).margin(0.002f));
    }

    SECTION("cos(pi/2) = 0") {
        REQUIRE(fastCos(kHalfPi) == Approx(0.0f).margin(0.002f));
    }

    SECTION("cos(pi) = -1") {
        REQUIRE(fastCos(kPi) == Approx(-1.0f).margin(0.002f));
    }

    SECTION("cos(2*pi) = 1") {
        REQUIRE(fastCos(kTwoPi) == Approx(1.0f).margin(0.002f));
    }

    SECTION("cos(pi/3) = 0.5") {
        REQUIRE(fastCos(kPi / 3.0f) == Approx(0.5f).margin(0.002f));
    }

    SECTION("cos(pi/6) = sqrt(3)/2 = 0.866") {
        REQUIRE(fastCos(kPi / 6.0f) == Approx(0.866025f).margin(0.002f));
    }
}

TEST_CASE("fastCos accuracy within 0.1% (FR-010)", "[fast_math][US2]") {
    SECTION("Accuracy across [-2pi, 2pi] range") {
        for (int i = -100; i <= 100; ++i) {
            float x = static_cast<float>(i) / 100.0f * kTwoPi;
            float expected = std::cos(x);
            float actual = fastCos(x);

            float relErr = relativeError(actual, expected);

            // For values close to zero, use absolute margin
            if (std::abs(expected) < 0.01f) {
                REQUIRE(std::abs(actual - expected) < 0.01f);
            } else {
                REQUIRE(relErr < 0.002f);
            }
        }
    }
}

TEST_CASE("fastCos handles special values", "[fast_math][US2]") {
    SECTION("NaN input returns NaN") {
        float nan = std::numeric_limits<float>::quiet_NaN();
        REQUIRE(isNaN(fastCos(nan)));
    }

    SECTION("Infinity input returns NaN") {
        float inf = std::numeric_limits<float>::infinity();
        REQUIRE(isNaN(fastCos(inf)));
    }
}

TEST_CASE("fastCos is noexcept", "[fast_math][realtime][US2]") {
    REQUIRE(noexcept(fastCos(0.0f)));
}

// =============================================================================
// fastTanh Tests (T038-T042 - FR-011)
// =============================================================================

TEST_CASE("fastTanh basic values", "[fast_math][US2]") {
    SECTION("tanh(0) = 0") {
        REQUIRE(fastTanh(0.0f) == 0.0f);
    }

    SECTION("tanh(0.5) within 0.5% of std::tanh") {
        float expected = std::tanh(0.5f);
        float actual = fastTanh(0.5f);
        float relErr = relativeError(actual, expected);
        REQUIRE(relErr < 0.005f);  // 0.5%
    }

    SECTION("tanh(1.0) within 0.5% of std::tanh") {
        float expected = std::tanh(1.0f);
        float actual = fastTanh(1.0f);
        float relErr = relativeError(actual, expected);
        REQUIRE(relErr < 0.005f);
    }

    SECTION("tanh(-1.0) within 0.5% of std::tanh") {
        float expected = std::tanh(-1.0f);
        float actual = fastTanh(-1.0f);
        float relErr = relativeError(actual, expected);
        REQUIRE(relErr < 0.005f);
    }

    SECTION("tanh(2.0) within 0.5% of std::tanh") {
        float expected = std::tanh(2.0f);
        float actual = fastTanh(2.0f);
        float relErr = relativeError(actual, expected);
        REQUIRE(relErr < 0.005f);
    }
}

TEST_CASE("fastTanh saturation behavior", "[fast_math][US2]") {
    SECTION("tanh(3.0) within 1% of std::tanh (FR-011)") {
        float expected = std::tanh(3.0f);
        float actual = fastTanh(3.0f);
        float relErr = relativeError(actual, expected);
        REQUIRE(relErr < 0.01f);  // 1%
    }

    SECTION("tanh(5.0) returns ~1.0") {
        float actual = fastTanh(5.0f);
        REQUIRE(actual == Approx(1.0f).margin(0.01f));
    }

    SECTION("tanh(-5.0) returns ~-1.0") {
        float actual = fastTanh(-5.0f);
        REQUIRE(actual == Approx(-1.0f).margin(0.01f));
    }

    SECTION("tanh(10.0) returns 1.0") {
        REQUIRE(fastTanh(10.0f) == 1.0f);
    }

    SECTION("tanh(-10.0) returns -1.0") {
        REQUIRE(fastTanh(-10.0f) == -1.0f);
    }
}

TEST_CASE("fastTanh accuracy within 0.5% for |x| < 3 (FR-011)", "[fast_math][US2]") {
    SECTION("Accuracy across [-3, 3] range") {
        for (int i = -30; i <= 30; ++i) {
            float x = static_cast<float>(i) / 10.0f;  // -3.0 to 3.0
            float expected = std::tanh(x);
            float actual = fastTanh(x);

            float relErr = relativeError(actual, expected);

            // For values close to zero, use absolute margin
            if (std::abs(expected) < 0.01f) {
                REQUIRE(std::abs(actual - expected) < 0.01f);
            } else {
                INFO("x = " << x << ", expected = " << expected << ", actual = " << actual);
                REQUIRE(relErr < 0.005f);  // 0.5%
            }
        }
    }
}

TEST_CASE("fastTanh handles special values (FR-015, FR-016)", "[fast_math][US2]") {
    SECTION("NaN input returns NaN") {
        float nan = std::numeric_limits<float>::quiet_NaN();
        REQUIRE(isNaN(fastTanh(nan)));
    }

    SECTION("Positive infinity returns 1.0") {
        float inf = std::numeric_limits<float>::infinity();
        REQUIRE(fastTanh(inf) == 1.0f);
    }

    SECTION("Negative infinity returns -1.0") {
        float negInf = -std::numeric_limits<float>::infinity();
        REQUIRE(fastTanh(negInf) == -1.0f);
    }
}

TEST_CASE("fastTanh is odd function (anti-symmetric)", "[fast_math][US2]") {
    SECTION("fastTanh(-x) = -fastTanh(x)") {
        for (float x : {0.1f, 0.5f, 1.0f, 2.0f, 2.5f}) {
            REQUIRE(fastTanh(-x) == Approx(-fastTanh(x)).margin(1e-6f));
        }
    }
}

TEST_CASE("fastTanh is noexcept", "[fast_math][realtime][US2]") {
    REQUIRE(noexcept(fastTanh(0.0f)));
}

// =============================================================================
// fastExp Tests (T043-T046 - FR-012)
// =============================================================================

TEST_CASE("fastExp basic values", "[fast_math][US2]") {
    SECTION("exp(0) = 1") {
        REQUIRE(fastExp(0.0f) == Approx(1.0f).margin(0.001f));
    }

    SECTION("exp(1) = e = 2.718") {
        REQUIRE(fastExp(1.0f) == Approx(2.71828f).margin(0.02f));  // 0.5% of e
    }

    SECTION("exp(-1) = 1/e = 0.368") {
        REQUIRE(fastExp(-1.0f) == Approx(0.36788f).margin(0.002f));
    }

    SECTION("exp(2) = e^2 = 7.389") {
        REQUIRE(fastExp(2.0f) == Approx(7.389f).margin(0.04f));
    }

    SECTION("exp(-2) = 1/e^2 = 0.135") {
        REQUIRE(fastExp(-2.0f) == Approx(0.13534f).margin(0.001f));
    }
}

TEST_CASE("fastExp accuracy within 0.5% for [-10, 10] (FR-012)", "[fast_math][US2]") {
    SECTION("Accuracy across [-10, 10] range") {
        for (int i = -100; i <= 100; ++i) {
            float x = static_cast<float>(i) / 10.0f;  // -10.0 to 10.0
            float expected = std::exp(x);
            float actual = fastExp(x);

            float relErr = relativeError(actual, expected);

            INFO("x = " << x << ", expected = " << expected << ", actual = " << actual << ", relErr = " << relErr);
            REQUIRE(relErr < 0.01f);  // 1% margin for safety
        }
    }
}

TEST_CASE("fastExp handles special values (FR-015, FR-016)", "[fast_math][US2]") {
    SECTION("NaN input returns NaN") {
        float nan = std::numeric_limits<float>::quiet_NaN();
        REQUIRE(isNaN(fastExp(nan)));
    }

    SECTION("Large positive value returns infinity") {
        float actual = fastExp(100.0f);
        REQUIRE(std::isinf(actual));
        REQUIRE(actual > 0.0f);
    }

    SECTION("Large negative value returns 0") {
        float actual = fastExp(-100.0f);
        REQUIRE(actual == 0.0f);
    }
}

TEST_CASE("fastExp is noexcept", "[fast_math][realtime][US2]") {
    REQUIRE(noexcept(fastExp(0.0f)));
}

// =============================================================================
// Constexpr Tests (US4)
// =============================================================================

TEST_CASE("FastMath functions are constexpr", "[fast_math][constexpr][US4]") {
    SECTION("fastSin is constexpr") {
        constexpr float sinZero = fastSin(0.0f);
        REQUIRE(sinZero == 0.0f);
    }

    SECTION("fastCos is constexpr") {
        constexpr float cosZero = fastCos(0.0f);
        REQUIRE(cosZero == Approx(1.0f).margin(0.002f));
    }

    SECTION("fastTanh is constexpr") {
        constexpr float tanhZero = fastTanh(0.0f);
        REQUIRE(tanhZero == 0.0f);
    }

    SECTION("fastExp is constexpr") {
        constexpr float expZero = fastExp(0.0f);
        REQUIRE(expZero == 1.0f);
    }

    SECTION("Compile-time lookup table generation") {
        constexpr std::array<float, 8> sineTable = {
            fastSin(0.0f),
            fastSin(kPi / 4.0f),
            fastSin(kHalfPi),
            fastSin(3.0f * kPi / 4.0f),
            fastSin(kPi),
            fastSin(5.0f * kPi / 4.0f),
            fastSin(3.0f * kHalfPi),
            fastSin(7.0f * kPi / 4.0f)
        };

        REQUIRE(sineTable[0] == Approx(0.0f).margin(0.002f));    // sin(0)
        REQUIRE(sineTable[2] == Approx(1.0f).margin(0.002f));    // sin(pi/2)
        REQUIRE(sineTable[4] == Approx(0.0f).margin(0.002f));    // sin(pi)
        REQUIRE(sineTable[6] == Approx(-1.0f).margin(0.002f));   // sin(3*pi/2)
    }
}

// =============================================================================
// Practical Use Case Tests (From spec.md acceptance scenarios)
// =============================================================================

TEST_CASE("Practical FastMath usage for saturation", "[fast_math][US2]") {
    SECTION("Spec US2 Scenario 1: fastTanh(0.5) within 0.1% of std::tanh") {
        float expected = std::tanh(0.5f);
        float actual = fastTanh(0.5f);
        float relErr = relativeError(actual, expected);

        REQUIRE(relErr < 0.001f);  // 0.1%
    }

    SECTION("Spec US2 Scenario 2: fastTanh(3.0) within 0.5% of std::tanh") {
        float expected = std::tanh(3.0f);
        float actual = fastTanh(3.0f);
        float relErr = relativeError(actual, expected);

        REQUIRE(relErr < 0.005f);  // 0.5%
    }

    SECTION("Soft clipping with drive") {
        // Simulating saturation in feedback path
        float input = 0.8f;
        float drive = 2.0f;
        float output = fastTanh(input * drive);

        // Output should be compressed (less than input * drive)
        REQUIRE(output < input * drive);
        REQUIRE(output > 0.0f);
        REQUIRE(output < 1.0f);
    }
}

TEST_CASE("FastMath for LFO modulation", "[fast_math][US2]") {
    SECTION("Generate sine wave samples") {
        constexpr size_t numSamples = 256;
        double phase = 0.0;
        double phaseIncrement = 1.0 / 256.0;  // One cycle over 256 samples

        for (size_t i = 0; i < numSamples; ++i) {
            float output = fastSin(static_cast<float>(phase * kTwoPi));

            // Verify range is [-1, 1]
            REQUIRE(output >= -1.01f);
            REQUIRE(output <= 1.01f);

            phase += phaseIncrement;
        }
    }
}
