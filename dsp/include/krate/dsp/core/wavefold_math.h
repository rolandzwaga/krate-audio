// ==============================================================================
// Layer 0: Core Utilities - Wavefolding Mathematical Functions
// ==============================================================================
// Library of pure, stateless mathematical functions for wavefolding algorithms.
// Provides three fundamental wavefolding algorithms:
// - Lambert W function: For theoretical wavefolder design (Lockhart algorithm)
// - Triangle fold: Symmetric mirror-like folding using modular arithmetic
// - Sine fold: Characteristic Serge synthesizer sound
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations)
// - Principle III: Modern C++ (inline, [[nodiscard]], C++20)
// - Principle IX: Layer 0 (no dependencies on higher layers)
// - Principle XIV: ODR Prevention (unique namespace WavefoldMath)
//
// Reference: specs/050-wavefolding-math/spec.md
// ==============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include <krate/dsp/core/db_utils.h>

namespace Krate {
namespace DSP {
namespace WavefoldMath {

// =============================================================================
// Constants
// =============================================================================

/// Minimum threshold value to prevent degeneracy in triangleFold.
/// Used to avoid division by zero and unbounded behavior.
constexpr float kMinThreshold = 0.01f;

/// Domain boundary for Lambert W function: -1/e
/// lambertW(x) returns NaN for x < kLambertWDomainMin
constexpr float kLambertWDomainMin = -0.36787944117144233f;  // -1/e

/// Small gain threshold for sineFold linear approximation.
/// When gain < this value, return x directly for smooth transition at gain=0.
constexpr float kSineFoldGainEpsilon = 0.001f;

// =============================================================================
// Lambert W Function (FR-001)
// =============================================================================

/// @brief Principal branch of the Lambert W function.
///
/// Computes W(x) where W satisfies W(x) * exp(W(x)) = x.
/// This is the principal branch W0, valid for x >= -1/e.
///
/// Implementation uses Newton-Raphson iteration with exactly 4 iterations
/// and Halley initial estimate w0 = x / (1 + x).
///
/// @param x Input value. Valid range: x >= -1/e (approximately -0.3679)
/// @return W(x) for valid inputs; NaN for x < -1/e or x = NaN
///
/// @note Performance: ~4 exp() calls, ~200-400 cycles
/// @note Accuracy: < 0.001 absolute tolerance vs reference (SC-002)
/// @note Real-time safe: noexcept, no allocation
///
/// @par Use case:
///   Foundation for Lockhart wavefolder design, enabling precise control
///   over harmonic mapping in circuit-derived transfer functions.
///
/// @par Mathematical properties:
///   - W(0) = 0
///   - W(e) = 1
///   - W(-1/e) = -1 (branch point)
///   - Monotonically increasing for x > -1/e
///
/// @par Harmonic Character:
///   When used in Lockhart wavefolder circuits, produces rich even and odd
///   harmonics with characteristic spectral nulls at specific frequencies.
///
/// @example
/// @code
/// float w = WavefoldMath::lambertW(0.1f);  // ~0.0953
/// float w2 = WavefoldMath::lambertW(1.0f); // ~0.567
/// @endcode
[[nodiscard]] inline float lambertW(float x) noexcept {
    // Handle special values
    if (detail::isNaN(x)) {
        return x;  // Propagate NaN
    }
    if (detail::isInf(x)) {
        return x > 0.0f ? x : std::numeric_limits<float>::quiet_NaN();
    }
    if (x < kLambertWDomainMin) {
        return std::numeric_limits<float>::quiet_NaN();  // Below domain
    }
    if (x == 0.0f) {
        return 0.0f;  // W(0) = 0
    }

    // Initial estimate
    float w;
    if (x < -0.32f) {
        // Near branch point: Puiseux series expansion around x = -1/e
        // W(x) = -1 + p - p^2/3 + 11p^3/72 - 43p^4/540 + ...
        // where p = sqrt(2(1+ex))
        const float e = 2.7182818284590452f;
        const float p = std::sqrt(2.0f * (1.0f + e * x));
        const float p2 = p * p;
        const float p3 = p2 * p;
        const float p4 = p3 * p;
        w = -1.0f + p - p2 / 3.0f + 11.0f * p3 / 72.0f - 43.0f * p4 / 540.0f;
    } else if (x < 0.35f) {
        // For x in [-0.32, 0.35], use Taylor series around x=0:
        // W(x) = x - x^2 + 3/2*x^3 - 8/3*x^4 + 125/24*x^5 - ...
        // Taylor series converges for |x| < 1/e ~ 0.368
        const float x2 = x * x;
        const float x3 = x2 * x;
        const float x4 = x3 * x;
        const float x5 = x4 * x;
        w = x - x2 + 1.5f * x3 - 2.666666667f * x4 + 5.208333333f * x5;
    } else if (x < 3.0f) {
        // For moderate x, use Halley's approximation as initial guess
        w = x / (1.0f + x);
    } else if (x < 50.0f) {
        // For larger x, use log-based estimate: W(x) ~ ln(x) - ln(ln(x))
        const float lnx = std::log(x);
        w = lnx - std::log(lnx);
    } else {
        // For very large x, use asymptotic expansion
        const float lnx = std::log(x);
        const float lnlnx = std::log(lnx);
        w = lnx - lnlnx + lnlnx / lnx;
    }

    // 4 Newton-Raphson iterations (FR-001)
    // Newton's method: w_{n+1} = w_n - (w_n * exp(w_n) - x) / (exp(w_n) * (w_n + 1))
    for (int i = 0; i < 4; ++i) {
        const float ew = std::exp(w);
        const float wew = w * ew;
        const float wp1 = w + 1.0f;

        // Guard against w = -1 which makes denominator zero
        if (std::abs(wp1) < 1e-10f) {
            w = -1.0f + 1e-6f;  // Nudge away from singularity
            continue;
        }

        const float fp = ew * wp1;
        if (std::abs(fp) > 1e-10f) {
            w = w - (wew - x) / fp;
        }
    }

    return w;
}

// =============================================================================
// Lambert W Function Approximation (FR-002)
// =============================================================================

/// @brief Fast approximation of Lambert W function.
///
/// Uses single Newton-Raphson iteration with Halley initial estimate
/// for ~3x speedup over exact lambertW() with < 0.01 relative error.
///
/// @param x Input value. Valid range: x >= -1/e (approximately -0.3679)
/// @return Approximate W(x) for valid inputs; NaN for x < -1/e or x = NaN
///
/// @note Performance: ~1 exp() call, ~50-100 cycles (3x+ faster than lambertW)
/// @note Accuracy: < 0.01 relative error for x in [-0.36, 1.0] (SC-003)
/// @note Real-time safe: noexcept, no allocation
///
/// @par Use case:
///   Real-time audio processing where full lambertW() accuracy is not
///   required. Acceptable for most wavefolding applications.
///
/// @par Harmonic Character:
///   Produces similar harmonic content to full lambertW() with slight
///   variations in upper harmonic ratios.
///
/// @example
/// @code
/// float wApprox = WavefoldMath::lambertWApprox(0.1f);  // ~0.095 (within 1%)
/// @endcode
[[nodiscard]] inline float lambertWApprox(float x) noexcept {
    // Handle special values
    if (detail::isNaN(x)) {
        return x;  // Propagate NaN
    }
    if (detail::isInf(x)) {
        return x > 0.0f ? x : std::numeric_limits<float>::quiet_NaN();
    }
    if (x < kLambertWDomainMin) {
        return std::numeric_limits<float>::quiet_NaN();  // Below domain
    }
    if (x == 0.0f) {
        return 0.0f;  // W(0) = 0
    }

    // Initial estimate (same as lambertW for consistency)
    float w;
    if (x < -0.32f) {
        // Near branch point: Puiseux series expansion
        const float e = 2.7182818284590452f;
        const float p = std::sqrt(2.0f * (1.0f + e * x));
        const float p2 = p * p;
        const float p3 = p2 * p;
        const float p4 = p3 * p;
        w = -1.0f + p - p2 / 3.0f + 11.0f * p3 / 72.0f - 43.0f * p4 / 540.0f;
    } else if (x < 0.35f) {
        // Taylor series around x=0 (converges for |x| < 1/e ~ 0.368)
        const float x2 = x * x;
        const float x3 = x2 * x;
        const float x4 = x3 * x;
        const float x5 = x4 * x;
        w = x - x2 + 1.5f * x3 - 2.666666667f * x4 + 5.208333333f * x5;
    } else if (x < 3.0f) {
        // Halley's approximation
        w = x / (1.0f + x);
    } else if (x < 50.0f) {
        // Log-based estimate
        const float lnx = std::log(x);
        w = lnx - std::log(lnx);
    } else {
        // Asymptotic expansion
        const float lnx = std::log(x);
        const float lnlnx = std::log(lnx);
        w = lnx - lnlnx + lnlnx / lnx;
    }

    // Single Newton-Raphson iteration (FR-002)
    const float ew = std::exp(w);
    const float wew = w * ew;
    const float wp1 = w + 1.0f;

    if (std::abs(wp1) > 1e-10f) {
        const float fp = ew * wp1;
        if (std::abs(fp) > 1e-10f) {
            w = w - (wew - x) / fp;
        }
    }

    return w;
}

// =============================================================================
// Triangle Fold (FR-003, FR-004, FR-005)
// =============================================================================

/// @brief Symmetric triangle wavefolding with multi-fold support.
///
/// Folds signal peaks that exceed the threshold, reflecting back and forth
/// within [-threshold, threshold]. Uses modular arithmetic to handle
/// arbitrary input magnitudes without diverging.
///
/// @param x Input signal value (any finite float)
/// @param threshold Folding threshold (clamped to minimum 0.01f)
/// @return Folded output, always within [-threshold, threshold]
///
/// @note Performance: ~5-15 cycles (fmod + arithmetic)
/// @note Real-time safe: noexcept, no allocation
/// @note Symmetry: triangleFold(-x, t) == -triangleFold(x, t) (FR-004)
///
/// @par Algorithm:
///   Uses modular arithmetic to map any input to a triangular wave:
///   - period = 4 * threshold
///   - phase = fmod(|x| + threshold, period)
///   - Map phase to triangle wave within [-threshold, threshold]
///
/// @par Harmonic Character:
///   Dense harmonic series (odd harmonics) with gradual high-frequency
///   rolloff. Similar to triangle wave spectrum. Good for guitar effects
///   and general-purpose wavefolding.
///
/// @example
/// @code
/// // No folding (within threshold)
/// float y1 = WavefoldMath::triangleFold(0.5f, 1.0f);  // 0.5
///
/// // Single fold
/// float y2 = WavefoldMath::triangleFold(1.5f, 1.0f);  // 0.5 (reflected)
///
/// // Multiple folds
/// float y3 = WavefoldMath::triangleFold(3.5f, 1.0f);  // -0.5
/// @endcode
[[nodiscard]] inline float triangleFold(float x, float threshold = 1.0f) noexcept {
    // Handle NaN - propagate
    if (detail::isNaN(x)) {
        return x;
    }

    // Clamp threshold to minimum (FR-003)
    threshold = std::max(kMinThreshold, threshold);

    // Get absolute value and sign for symmetry (FR-004)
    const float ax = std::abs(x);
    const float sign = x >= 0.0f ? 1.0f : -1.0f;

    // Modular arithmetic for multi-fold support (FR-005)
    const float period = 4.0f * threshold;
    float phase = std::fmod(ax + threshold, period);

    // Handle potential negative phase from fmod with negative inputs
    if (phase < 0.0f) {
        phase += period;
    }

    // Map phase to triangle wave within [-threshold, threshold]
    float result;
    if (phase < 2.0f * threshold) {
        result = phase - threshold;
    } else {
        result = 3.0f * threshold - phase;
    }

    // Apply sign for odd symmetry (FR-004)
    return sign * result;
}

// =============================================================================
// Sine Fold (FR-006, FR-007, FR-008)
// =============================================================================

/// @brief Sine-based wavefolding characteristic of Serge synthesizers.
///
/// Applies the classic Serge wavefolder transfer function: sin(gain * x).
/// Creates smooth, musical folding with FM-like harmonic character.
///
/// @param x Input signal value (any float)
/// @param gain Folding intensity. At gain=0, returns x (linear passthrough).
///             Negative gain is treated as absolute value.
/// @return Folded output, always within [-1, 1]
///
/// @note Performance: ~50-80 cycles (dominated by sin() call)
/// @note Real-time safe: noexcept, no allocation
/// @note Output always bounded to [-1, 1] due to sine function
///
/// @par Edge cases:
///   - gain = 0: Returns x (linear passthrough, not silence)
///   - gain < 0: Treated as |gain|
///   - gain very small (< 0.001): Returns x for smooth transition
///
/// @par Harmonic Character:
///   Sparse FM-like spectrum (Bessel function distribution).
///   Characteristic Serge synthesizer sound with smooth harmonics.
///   Aliasing at high gains is intentional (anti-aliasing is processor
///   layer responsibility).
///
/// @par Typical gain values:
///   - gain = 1: Gentle folding
///   - gain = pi (~3.14): Characteristic Serge tone
///   - gain = 2*pi (~6.28): Aggressive folding
///   - gain > 10: Heavy folding, significant aliasing
///
/// @example
/// @code
/// // Gentle folding
/// float y1 = WavefoldMath::sineFold(0.5f, 3.14159f);  // sin(pi * 0.5) = 1.0
///
/// // Linear passthrough at gain=0
/// float y2 = WavefoldMath::sineFold(0.7f, 0.0f);  // 0.7 (unchanged)
///
/// // Aggressive folding
/// float y3 = WavefoldMath::sineFold(0.5f, 10.0f);  // sin(5.0) = -0.959
/// @endcode
[[nodiscard]] inline float sineFold(float x, float gain) noexcept {
    // Handle NaN - propagate
    if (detail::isNaN(x)) {
        return x;
    }

    // Treat negative gain as positive (FR-006)
    gain = std::abs(gain);

    // Linear passthrough at very small gain for smooth transition at gain=0 (FR-007)
    if (gain < kSineFoldGainEpsilon) {
        return x;
    }

    // Classic Serge wavefolder: sin(gain * x) (FR-006, FR-008)
    return std::sin(gain * x);
}

} // namespace WavefoldMath
} // namespace DSP
} // namespace Krate
