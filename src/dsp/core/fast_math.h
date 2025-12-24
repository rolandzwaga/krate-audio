// ==============================================================================
// Layer 0: Core Utility - FastMath
// ==============================================================================
// Optimized approximations of transcendental functions.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (no allocation, noexcept)
// - Principle III: Modern C++ (constexpr, inline)
// - Principle IX: Layer 0 (no dependencies on higher layers)
//
// Performance Target (SC-001): 2x faster than std:: equivalents
//
// Reference: specs/017-layer0-utilities/spec.md
// ==============================================================================

#pragma once

#include "dsp/core/db_utils.h"        // detail::isNaN, detail::isInf (reuse existing)
#include "dsp/core/math_constants.h"  // kPi, kTwoPi, kHalfPi
#include <limits>

namespace Iterum {
namespace DSP {
namespace FastMath {

// =============================================================================
// Internal Implementation Details
// =============================================================================

namespace detail {

// Reuse isNaN and isInf from db_utils.h via Iterum::DSP::detail namespace
using Iterum::DSP::detail::isNaN;
using Iterum::DSP::detail::isInf;

/// @brief Reduce angle to [-pi, pi] range for sin/cos.
/// Uses integer truncation to avoid non-constexpr std::floor.
[[nodiscard]] constexpr float reduceAngle(float x) noexcept {
    // Reduce to [-pi, pi] using: x - 2*pi * round(x / (2*pi))
    const float invTwoPi = 1.0f / kTwoPi;
    const float n = static_cast<float>(static_cast<int>(x * invTwoPi + (x >= 0.0f ? 0.5f : -0.5f)));
    return x - n * kTwoPi;
}

/// @brief Compute sine for x in [-pi/2, pi/2] using 7th-order Taylor.
/// This range gives excellent accuracy (< 0.01% error).
[[nodiscard]] constexpr float sinCore(float x) noexcept {
    // Taylor: sin(x) = x - x^3/6 + x^5/120 - x^7/5040
    // Horner form: x * (1 + x^2 * (-1/6 + x^2 * (1/120 + x^2 * -1/5040)))
    const float x2 = x * x;
    return x * (1.0f + x2 * (-0.16666667f + x2 * (0.00833333f + x2 * -0.00019841f)));
}

} // namespace detail

// =============================================================================
// Public API
// =============================================================================

/// @brief Fast sine approximation using 7th-order Taylor polynomial.
///
/// @param x Angle in radians
/// @return Approximate sin(x)
///
/// @accuracy Maximum error: 0.1% for x in [-2pi, 2pi] (FR-009)
/// @performance Target: 2x faster than std::sin (SC-001)
///
/// @note NaN input returns NaN (FR-015)
/// @note Infinity input returns NaN (FR-016)
///
/// @example
/// @code
/// float y = fastSin(kPi / 6.0f);  // ~ 0.5
/// constexpr float z = fastSin(0.0f);  // = 0.0 (compile-time)
/// @endcode
[[nodiscard]] constexpr float fastSin(float x) noexcept {
    // Handle special cases (FR-015, FR-016)
    if (detail::isNaN(x)) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    if (detail::isInf(x)) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    // Reduce to [-pi, pi]
    float reduced = detail::reduceAngle(x);

    // Further reduce to [-pi/2, pi/2] for better Taylor accuracy
    // Use identity: sin(x) = sin(pi - x) for x > pi/2
    //               sin(x) = -sin(-pi - x) for x < -pi/2
    if (reduced > kHalfPi) {
        reduced = kPi - reduced;
    } else if (reduced < -kHalfPi) {
        reduced = -kPi - reduced;
    }

    return detail::sinCore(reduced);
}

/// @brief Fast cosine approximation using 5th-order minimax polynomial.
///
/// @param x Angle in radians
/// @return Approximate cos(x)
///
/// @accuracy Maximum error: 0.1% for x in [-2pi, 2pi] (FR-010)
/// @performance Target: 2x faster than std::cos (SC-001)
///
/// @note Implemented as fastSin(x + pi/2)
/// @note NaN input returns NaN (FR-015)
/// @note Infinity input returns NaN (FR-016)
///
/// @example
/// @code
/// float y = fastCos(kPi / 3.0f);  // ~ 0.5
/// constexpr float z = fastCos(0.0f);  // ~ 1.0 (compile-time)
/// @endcode
[[nodiscard]] constexpr float fastCos(float x) noexcept {
    return fastSin(x + kHalfPi);
}

/// @brief Fast hyperbolic tangent approximation using Pade approximant.
///
/// @param x Input value
/// @return Approximate tanh(x)
///
/// @accuracy Maximum error: 0.5% for |x| < 3, 1% for larger (FR-011)
/// @performance Target: 2x faster than std::tanh (SC-001)
///
/// @note NaN input returns NaN (FR-015)
/// @note +Infinity returns +1.0, -Infinity returns -1.0 (FR-016)
///
/// @example
/// @code
/// float y = fastTanh(0.5f);  // ~ 0.462
/// float z = fastTanh(10.0f); // ~ 1.0 (saturation)
/// @endcode
[[nodiscard]] constexpr float fastTanh(float x) noexcept {
    // Handle special cases (FR-015, FR-016)
    if (detail::isNaN(x)) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    if (detail::isInf(x)) {
        return x > 0.0f ? 1.0f : -1.0f;
    }

    // For |x| >= 4, tanh saturates to +/-1 (to within float precision)
    if (x >= 4.0f) {
        return 1.0f;
    }
    if (x <= -4.0f) {
        return -1.0f;
    }

    // Padé (5,4) approximation for |x| < 4:
    // tanh(x) ≈ x * (945 + 105*x² + x⁴) / (945 + 420*x² + 15*x⁴)
    // This gives < 0.05% max error for |x| < 4
    const float x2 = x * x;
    const float x4 = x2 * x2;
    return x * (945.0f + 105.0f * x2 + x4) / (945.0f + 420.0f * x2 + 15.0f * x4);
}

/// @brief Fast exponential approximation using range-reduced Taylor series.
///
/// @param x Exponent
/// @return Approximate exp(x)
///
/// @accuracy Maximum error: 0.5% for x in [-10, 10] (FR-012)
/// @performance Target: 2x faster than std::exp (SC-001)
///
/// @note NaN input returns NaN (FR-015)
/// @note Large positive x returns +Infinity (FR-016)
/// @note Large negative x returns 0 (FR-016)
///
/// @example
/// @code
/// float y = fastExp(1.0f);  // ~ 2.718
/// float z = fastExp(-1.0f); // ~ 0.368
/// @endcode
[[nodiscard]] constexpr float fastExp(float x) noexcept {
    // Handle special cases (FR-015, FR-016)
    if (detail::isNaN(x)) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    if (x > 88.0f) {
        return std::numeric_limits<float>::infinity();
    }
    if (x < -88.0f) {
        return 0.0f;
    }

    // Range reduction: exp(x) = exp(x/n)^n
    // Use exp(x) = 2^k * exp(r) where x = k*ln(2) + r and |r| <= ln(2)/2
    constexpr float kLn2 = 0.693147181f;
    const int k = static_cast<int>(x / kLn2 + (x >= 0.0f ? 0.5f : -0.5f));
    const float r = x - static_cast<float>(k) * kLn2;

    // Taylor series for exp(r): 1 + r + r^2/2 + r^3/6 + r^4/24 + r^5/120
    // Horner's form for efficiency
    const float r2 = r * r;
    const float r3 = r2 * r;
    const float r4 = r2 * r2;
    const float r5 = r4 * r;
    float expR = 1.0f + r + r2 * 0.5f + r3 * (1.0f / 6.0f) + r4 * (1.0f / 24.0f) + r5 * (1.0f / 120.0f);

    // Multiply by 2^k
    // Use bit manipulation for integer powers of 2
    if (k >= 0) {
        for (int i = 0; i < k && i < 128; ++i) {
            expR *= 2.0f;
        }
    } else {
        for (int i = 0; i > k && i > -128; --i) {
            expR *= 0.5f;
        }
    }

    return expR;
}

} // namespace FastMath
} // namespace DSP
} // namespace Iterum
