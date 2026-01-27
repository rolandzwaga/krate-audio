// ==============================================================================
// API Contract: Wavefolding Math Library
// ==============================================================================
// This file defines the public API contract for the wavefolding math library.
// Implementation will be in: dsp/include/krate/dsp/core/wavefold_math.h
//
// Spec: 050-wavefolding-math
// Date: 2026-01-12
// ==============================================================================

#pragma once

#include <cmath>
#include <limits>

namespace Krate {
namespace DSP {
namespace WavefoldMath {

// =============================================================================
// Constants
// =============================================================================

/// Minimum threshold value to prevent degeneracy
/// Used in triangleFold() to avoid division by zero and unbounded behavior
constexpr float kMinThreshold = 0.01f;

/// Domain boundary for Lambert W function: -1/e
/// lambertW(x) returns NaN for x < kLambertWDomainMin
constexpr float kLambertWDomainMin = -0.36787944117144233f;  // -1/e

/// Small gain threshold for sineFold linear approximation
/// When gain < this value, return x directly for smooth transition at gain=0
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
/// @example
/// @code
/// float w = WavefoldMath::lambertW(0.1f);  // ~0.0953
/// float w2 = WavefoldMath::lambertW(1.0f); // ~0.567
/// @endcode
[[nodiscard]] inline float lambertW(float x) noexcept;

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
/// @example
/// @code
/// float wApprox = WavefoldMath::lambertWApprox(0.1f);  // ~0.095 (within 1%)
/// @endcode
[[nodiscard]] inline float lambertWApprox(float x) noexcept;

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
/// @par Harmonic character:
///   Dense harmonic series (odd harmonics) with gradual high-frequency
///   rolloff. Similar to triangle wave spectrum.
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
[[nodiscard]] inline float triangleFold(float x, float threshold = 1.0f) noexcept;

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
///   - gain very small (< 0.001): Uses linear approximation
///
/// @par Harmonic character:
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
[[nodiscard]] inline float sineFold(float x, float gain) noexcept;

} // namespace WavefoldMath
} // namespace DSP
} // namespace Krate
