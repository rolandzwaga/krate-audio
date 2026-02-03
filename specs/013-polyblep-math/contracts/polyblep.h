// ==============================================================================
// Layer 0: Core Utility - PolyBLEP/PolyBLAMP Correction Functions
// ==============================================================================
// Polynomial band-limited step (BLEP) and ramp (BLAMP) correction functions
// for anti-aliased waveform generation. Pure mathematical functions with no
// state, no initialization, and no memory allocation.
//
// These functions return a correction value to subtract from naive waveform
// output at discontinuities (BLEP for step, BLAMP for derivative).
//
// Usage:
//   // Sawtooth with PolyBLEP correction:
//   float saw = 2.0f * t - 1.0f;           // naive sawtooth
//   saw -= polyBlep(t, dt);                 // subtract BLEP correction at wrap
//
//   // Triangle with PolyBLAMP correction:
//   float tri = naive_triangle(t);
//   tri += slope_change * dt * polyBlamp(t, dt);  // add BLAMP at peaks
//
// Precondition: 0 < dt < 0.5 (below Nyquist). Behavior undefined for dt >= 0.5.
// NaN/Inf inputs are propagated per IEEE 754 without sanitization.
//
// References:
// - Valimaki & Pekonen, "Perceptually informed synthesis of bandlimited
//   classical waveforms using integrated polynomial interpolation" (2012)
// - Esqueda, Valimaki, Bilbao, "Rounding Corners with BLAMP" (DAFx-16, 2016)
// - ryukau filter_notes polyblep_residual
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (constexpr, noexcept, no allocations)
// - Principle III: Modern C++ (constexpr, [[nodiscard]], C++20)
// - Principle IX: Layer 0 (depends only on math_constants.h / stdlib)
// - Principle XII: Test-First Development
//
// Reference: specs/013-polyblep-math/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/math_constants.h>

namespace Krate {
namespace DSP {

// =============================================================================
// 2-Point PolyBLEP (FR-001, FR-005, FR-006, FR-007)
// =============================================================================

/// @brief 2-point polynomial band-limited step correction (C1 continuity).
///
/// Computes a correction value for step discontinuities (e.g., sawtooth wrap,
/// square wave edge). The correction is a 2nd-degree polynomial applied to
/// the 2 samples nearest the discontinuity.
///
/// @param t Normalized phase position [0, 1)
/// @param dt Normalized phase increment (frequency / sampleRate)
/// @return Correction value to subtract from naive waveform output.
///         Returns 0.0f outside the correction region [0, dt) and [1-dt, 1).
///
/// @pre 0 < dt < 0.5 (behavior undefined at/above Nyquist)
/// @note NaN/Inf inputs are propagated per IEEE 754 without sanitization.
///
/// @example
/// @code
/// // Anti-aliased sawtooth:
/// float saw = 2.0f * t - 1.0f;
/// saw -= polyBlep(t, dt);
/// @endcode
[[nodiscard]] constexpr float polyBlep(float t, float dt) noexcept;

// =============================================================================
// 4-Point PolyBLEP (FR-002, FR-005, FR-006, FR-008)
// =============================================================================

/// @brief 4-point polynomial band-limited step correction (C3 continuity).
///
/// Higher-quality variant using a 4th-degree polynomial over a 4-sample kernel.
/// Provides better alias suppression than the 2-point version at the cost of
/// a wider correction region (2*dt on each side of the discontinuity).
///
/// @param t Normalized phase position [0, 1)
/// @param dt Normalized phase increment (frequency / sampleRate)
/// @return Correction value to subtract from naive waveform output.
///         Returns 0.0f outside the correction region [0, 2*dt) and [1-2*dt, 1).
///
/// @pre 0 < dt < 0.5 (behavior undefined at/above Nyquist)
/// @note NaN/Inf inputs are propagated per IEEE 754 without sanitization.
[[nodiscard]] constexpr float polyBlep4(float t, float dt) noexcept;

// =============================================================================
// 2-Point PolyBLAMP (FR-003, FR-005, FR-006, FR-007)
// =============================================================================

/// @brief 2-point polynomial band-limited ramp correction (C1 continuity).
///
/// Computes a correction value for derivative discontinuities (e.g., triangle
/// wave peaks where the slope changes sign). The correction is the integral
/// of the 2-point polyBLEP, yielding a 3rd-degree polynomial.
///
/// @param t Normalized phase position [0, 1)
/// @param dt Normalized phase increment (frequency / sampleRate)
/// @return Raw correction value. Caller must scale by derivative discontinuity
///         magnitude and dt when applying.
///         Returns 0.0f outside the correction region [0, dt) and [1-dt, 1).
///
/// @pre 0 < dt < 0.5 (behavior undefined at/above Nyquist)
/// @note NaN/Inf inputs are propagated per IEEE 754 without sanitization.
///
/// @example
/// @code
/// // Anti-aliased triangle peak correction:
/// float slopeChange = 4.0f;  // derivative changes by 4 at peak
/// tri += slopeChange * dt * polyBlamp(t, dt);
/// @endcode
[[nodiscard]] constexpr float polyBlamp(float t, float dt) noexcept;

// =============================================================================
// 4-Point PolyBLAMP (FR-004, FR-005, FR-006, FR-008)
// =============================================================================

/// @brief 4-point polynomial band-limited ramp correction (C3 continuity).
///
/// Higher-quality variant using a 5th-degree polynomial over a 4-sample kernel.
/// Based on the DAFx-16 paper "Rounding Corners with BLAMP" by Esqueda,
/// Valimaki, and Bilbao. Provides better alias suppression for derivative
/// discontinuities than the 2-point version.
///
/// @param t Normalized phase position [0, 1)
/// @param dt Normalized phase increment (frequency / sampleRate)
/// @return Raw correction value. Caller must scale by derivative discontinuity
///         magnitude and dt when applying.
///         Returns 0.0f outside the correction region [0, 2*dt) and [1-2*dt, 1).
///
/// @pre 0 < dt < 0.5 (behavior undefined at/above Nyquist)
/// @note NaN/Inf inputs are propagated per IEEE 754 without sanitization.
[[nodiscard]] constexpr float polyBlamp4(float t, float dt) noexcept;

} // namespace DSP
} // namespace Krate
