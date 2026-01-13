// ==============================================================================
// Layer 0: Core Utility - Sigmoid Transfer Functions
// ==============================================================================
// Unified library of sigmoid (soft-clipping) transfer functions for audio
// distortion and saturation effects. Provides both symmetric functions
// (odd harmonics only) and asymmetric functions (even + odd harmonics).
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations)
// - Principle III: Modern C++ (constexpr, inline, C++20)
// - Principle IX: Layer 0 (no dependencies on higher layers)
// - Principle XIV: ODR Prevention (unique namespaces, reuses FastMath)
//
// Reference: specs/047-sigmoid-functions/spec.md
// ==============================================================================

#pragma once

#include <cmath>
#include <algorithm>

#include <krate/dsp/core/fast_math.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>

namespace Krate {
namespace DSP {

// =============================================================================
// Type Aliases
// =============================================================================

/// @brief Function pointer type for sigmoid transfer functions.
/// @details Used by Asymmetric::withBias() and other composition utilities.
///          All sigmoid functions have the signature: float(float) noexcept
using SigmoidFunc = float (*)(float);

// =============================================================================
// Sigmoid Namespace - Symmetric Transfer Functions
// =============================================================================
// All functions in this namespace are point-symmetric around the origin,
// meaning f(-x) = -f(x). This produces only odd harmonics (3rd, 5th, 7th...)
// when applied to audio signals.

namespace Sigmoid {

// -----------------------------------------------------------------------------
// tanh - Hyperbolic Tangent (FR-001)
// -----------------------------------------------------------------------------

/// @brief Fast hyperbolic tangent for saturation/waveshaping.
///
/// Wraps FastMath::fastTanh() which uses a Padé (5,4) approximant.
/// Produces warm, smooth saturation with only odd harmonics.
///
/// @param x Input value (unbounded)
/// @return Saturated output in range [-1, 1]
///
/// @performance ~3x faster than std::tanh (SC-002)
/// @harmonics Odd only (3rd, 5th, 7th...)
/// @note NaN propagates, +/-Inf returns +/-1
///
/// @example
/// @code
/// float y = Sigmoid::tanh(0.5f);  // ~0.462
/// float z = Sigmoid::tanh(3.0f);  // ~0.995
/// @endcode
[[nodiscard]] constexpr float tanh(float x) noexcept {
    return FastMath::fastTanh(x);
}

/// @brief Variable-drive tanh for "drive knob" control.
///
/// Applies drive scaling before tanh: tanh(drive * x).
/// At drive=1.0, matches tanh(x). Low drive is near-linear,
/// high drive approaches hard clipping.
///
/// @param x Input value
/// @param drive Drive amount (0.1 = soft, 1.0 = normal, 10.0 = hard)
/// @return Saturated output in range [-1, 1]
///
/// @note drive=0 returns 0, negative drive treated as positive
[[nodiscard]] constexpr float tanhVariable(float x, float drive) noexcept {
    // Use absolute value of drive (negative treated as positive)
    const float effectiveDrive = (drive < 0.0f) ? -drive : drive;
    if (effectiveDrive == 0.0f) {
        return 0.0f;
    }
    return FastMath::fastTanh(effectiveDrive * x);
}

// -----------------------------------------------------------------------------
// atan - Arctangent (FR-003)
// -----------------------------------------------------------------------------

/// @brief Normalized arctangent for soft saturation.
///
/// Returns (2/π) * atan(x), mapping output to [-1, 1].
/// Slightly brighter harmonic character than tanh.
///
/// @param x Input value (unbounded)
/// @return Saturated output in range [-1, 1]
///
/// @harmonics Odd only, slightly different rolloff than tanh
[[nodiscard]] inline float atan(float x) noexcept {
    constexpr float kTwoOverPi = 2.0f / kPi;
    return kTwoOverPi * std::atan(x);
}

/// @brief Variable-drive arctangent for "drive knob" control.
///
/// @param x Input value
/// @param drive Drive amount
/// @return Saturated output in range [-1, 1]
///
/// @note drive=0 returns 0, negative drive treated as positive
[[nodiscard]] inline float atanVariable(float x, float drive) noexcept {
    // Use absolute value of drive (negative treated as positive)
    const float effectiveDrive = (drive < 0.0f) ? -drive : drive;
    if (effectiveDrive == 0.0f) {
        return 0.0f;
    }
    constexpr float kTwoOverPi = 2.0f / kPi;
    return kTwoOverPi * std::atan(effectiveDrive * x);
}

// -----------------------------------------------------------------------------
// softClipCubic - Cubic Polynomial Soft Clipper (FR-005)
// -----------------------------------------------------------------------------

/// @brief Cubic polynomial soft clipper: 1.5x - 0.5x³
///
/// Classic waveshaping formula with smooth transition to clipping.
/// Very fast (no transcendentals). f'(±1) = 0 for smooth knee.
///
/// @param x Input value (unbounded, clipped outside [-1, 1])
/// @return Soft-clipped output in range [-1, 1]
///
/// @performance 8-10x faster than std::tanh
/// @harmonics Odd only (3rd harmonic dominant)
[[nodiscard]] inline float softClipCubic(float x) noexcept {
    // Handle NaN: propagate (NaN comparisons are false)
    if (detail::isNaN(x)) return x;
    if (x <= -1.0f) return -1.0f;
    if (x >= 1.0f) return 1.0f;
    return 1.5f * x - 0.5f * x * x * x;
}

// -----------------------------------------------------------------------------
// softClipQuintic - Quintic Polynomial Soft Clipper (FR-006)
// -----------------------------------------------------------------------------

/// @brief Quintic polynomial soft clipper: (15x - 10x³ + 3x⁵) / 8
///
/// 5th-order Legendre polynomial for smoother knee than cubic.
/// f'(±1) = 0 and f''(±1) = 0 for second-derivative continuity.
///
/// @param x Input value (unbounded, clipped outside [-1, 1])
/// @return Soft-clipped output in range [-1, 1]
///
/// @performance 6-8x faster than std::tanh
/// @harmonics Odd only (smoother spectral rolloff than cubic)
[[nodiscard]] inline float softClipQuintic(float x) noexcept {
    // Handle NaN: propagate (NaN comparisons are false)
    if (detail::isNaN(x)) return x;
    if (x <= -1.0f) return -1.0f;
    if (x >= 1.0f) return 1.0f;
    const float x2 = x * x;
    const float x3 = x2 * x;
    const float x5 = x3 * x2;
    return (15.0f * x - 10.0f * x3 + 3.0f * x5) * 0.125f;
}

// -----------------------------------------------------------------------------
// recipSqrt - Fast Reciprocal Square Root Sigmoid (FR-007)
// -----------------------------------------------------------------------------

/// @brief Ultra-fast tanh alternative: x / sqrt(x² + 1)
///
/// Algebraic formula that vectorizes well and has no transcendentals
/// except sqrt. Similar shape to tanh.
///
/// @param x Input value (unbounded)
/// @return Saturated output approaching [-1, 1]
///
/// @performance 10-13x faster than std::tanh (SC-003)
/// @harmonics Odd only, similar character to tanh
[[nodiscard]] inline float recipSqrt(float x) noexcept {
    // Handle infinity: return +/-1
    if (detail::isInf(x)) {
        return x > 0.0f ? 1.0f : -1.0f;
    }
    return x / std::sqrt(x * x + 1.0f);
}

// -----------------------------------------------------------------------------
// erf - Error Function (FR-008)
// -----------------------------------------------------------------------------

/// @brief Error function for tape-like saturation character.
///
/// Wraps std::erf. Unique spectral character with harmonic nulls
/// at specific frequencies, desirable for tape emulation.
///
/// @param x Input value
/// @return erf(x) in range (-1, 1)
///
/// @harmonics Odd only with characteristic spectral nulls
[[nodiscard]] inline float erf(float x) noexcept {
    return std::erf(x);
}

// -----------------------------------------------------------------------------
// erfApprox - Fast Error Function Approximation (FR-009)
// -----------------------------------------------------------------------------

/// @brief Fast approximation of erf for real-time use.
///
/// Uses Abramowitz and Stegun approximation (max error ~5×10⁻⁴).
/// Faster than std::erf while maintaining good accuracy.
///
/// @param x Input value
/// @return Approximate erf(x) within 0.1% of std::erf
///
/// @accuracy Max error 0.05% for |x| < 4
[[nodiscard]] inline float erfApprox(float x) noexcept {
    // Handle sign - erf is odd function
    const float sign = (x >= 0.0f) ? 1.0f : -1.0f;
    const float absX = (x >= 0.0f) ? x : -x;

    // Abramowitz and Stegun approximation 7.1.26
    // erf(x) ≈ 1 - (a1*t + a2*t² + a3*t³ + a4*t⁴ + a5*t⁵) * exp(-x²)
    // where t = 1 / (1 + p*x)
    constexpr float p = 0.3275911f;
    constexpr float a1 = 0.254829592f;
    constexpr float a2 = -0.284496736f;
    constexpr float a3 = 1.421413741f;
    constexpr float a4 = -1.453152027f;
    constexpr float a5 = 1.061405429f;

    const float t = 1.0f / (1.0f + p * absX);
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float t4 = t3 * t;
    const float t5 = t4 * t;

    const float poly = a1 * t + a2 * t2 + a3 * t3 + a4 * t4 + a5 * t5;
    const float result = 1.0f - poly * std::exp(-absX * absX);

    return sign * result;
}

// -----------------------------------------------------------------------------
// hardClip - Hard Clipper (FR-010)
// -----------------------------------------------------------------------------

/// @brief Hard clip to threshold (default ±1).
///
/// Simple clamp operation. Produces all harmonics (harsh character).
/// Provided for completeness and API consistency.
///
/// @param x Input value
/// @param threshold Clipping threshold (default 1.0)
/// @return Clipped output in range [-threshold, threshold]
///
/// @harmonics All harmonics (harsh, digital character)
[[nodiscard]] inline constexpr float hardClip(float x, float threshold = 1.0f) noexcept {
    return std::clamp(x, -threshold, threshold);
}

} // namespace Sigmoid

// =============================================================================
// Asymmetric Namespace - Asymmetric Transfer Functions
// =============================================================================
// Functions that create asymmetry in the transfer function, producing
// both even and odd harmonics. Even harmonics (2nd, 4th...) add warmth
// and are characteristic of tube amplifiers.

namespace Asymmetric {

// -----------------------------------------------------------------------------
// tube - Tube-Style Asymmetric Saturation (FR-012)
// -----------------------------------------------------------------------------

/// @brief Tube-style asymmetric saturation with even harmonics.
///
/// Uses a polynomial (x + 0.3x² - 0.15x³) with pre-limiting to create
/// asymmetric saturation that produces even harmonics (2nd, 4th...).
/// The x² term creates asymmetry; the x³ term adds odd harmonic content.
///
/// The polynomial has a turning point at x ≈ 2.3, so inputs are soft-limited
/// via tanh to stay in the stable operating range. This ensures correct
/// saturation behavior (compression, not inversion) at all drive levels.
///
/// @param x Input value (unbounded)
/// @return Asymmetrically saturated output in range approximately [-1, 1]
///
/// @harmonics Even + Odd (rich, warm, 2nd harmonic emphasis)
[[nodiscard]] inline float tube(float x) noexcept {
    // Pre-limit input to keep polynomial in stable range (|x| < ~2.3)
    // tanh(x * 0.5) * 2.0 soft-limits to approximately [-2, 2]
    const float limited = FastMath::fastTanh(x * 0.5f) * 2.0f;

    const float x2 = limited * limited;
    const float x3 = x2 * limited;
    const float asymmetric = limited + 0.3f * x2 - 0.15f * x3;
    return FastMath::fastTanh(asymmetric);
}

// -----------------------------------------------------------------------------
// diode - Diode-Style Asymmetric Saturation (FR-013)
// -----------------------------------------------------------------------------

/// @brief Diode-style asymmetric clipping.
///
/// Models diode conduction: soft exponential saturation in forward bias,
/// harder linear-ish curve in reverse bias. Creates even harmonics
/// through asymmetry. Extracted from SaturationProcessor.
///
/// @param x Input value
/// @return Asymmetrically clipped output
///
/// @harmonics Even + Odd (subtle warmth)
[[nodiscard]] inline float diode(float x) noexcept {
    if (x >= 0.0f) {
        // Forward bias: soft exponential saturation
        return 1.0f - std::exp(-x * 1.5f);
    } else {
        // Reverse bias: harder, more linear with soft limit
        return x / (1.0f - 0.5f * x);
    }
}

// -----------------------------------------------------------------------------
// withBias - Create Asymmetry via DC Bias (FR-011)
// -----------------------------------------------------------------------------

/// @brief Apply DC bias to symmetric function to create asymmetry.
///
/// Adds bias to input before applying symmetric sigmoid, creating
/// asymmetry that produces even harmonics. Caller MUST DC-block
/// the output to remove the offset.
///
/// @tparam SigmoidFunc Callable with signature float(float)
/// @param x Input value
/// @param bias DC bias to add before sigmoid
/// @param func Symmetric sigmoid function to apply
/// @return Biased sigmoid output (DC-block after!)
///
/// @note Always DC-block output when using non-zero bias
///
/// @example
/// @code
/// float out = Asymmetric::withBias(input, 0.2f, Sigmoid::tanh);
/// dcBlocker.process(out);  // Remove DC offset!
/// @endcode
template <typename Func>
[[nodiscard]] inline float withBias(float x, float bias, Func func) noexcept {
    return func(x + bias);
}

// -----------------------------------------------------------------------------
// dualCurve - Different Gains Per Polarity (FR-014)
// -----------------------------------------------------------------------------

/// @brief Apply different saturation gains to positive/negative half-waves.
///
/// Creates asymmetry by using different drive amounts for positive
/// and negative input. Useful for germanium fuzz modeling.
///
/// @param x Input value
/// @param posGain Drive gain for positive half-wave (clamped to >= 0)
/// @param negGain Drive gain for negative half-wave (clamped to >= 0)
/// @return Asymmetrically saturated output
///
/// @note Negative gains are clamped to zero to prevent polarity flips.
///       Zero gain produces zero output for that half-wave.
///
/// @harmonics Even + Odd when posGain != negGain
[[nodiscard]] inline float dualCurve(float x, float posGain, float negGain) noexcept {
    // FR-002: Clamp negative gains to zero to prevent polarity flips
    posGain = std::max(0.0f, posGain);
    negGain = std::max(0.0f, negGain);

    if (x >= 0.0f) {
        return FastMath::fastTanh(x * posGain);
    } else {
        return FastMath::fastTanh(x * negGain);
    }
}

} // namespace Asymmetric

} // namespace DSP
} // namespace Krate
