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
[[nodiscard]] constexpr float polyBlep(float t, float dt) noexcept {
    // After-wrap region [0, dt): just past the discontinuity
    if (t < dt) {
        float x = t / dt; // normalize to [0, 1)
        // Correction = -(x^2 - 2x + 1) = -(x-1)^2
        return -(x * x - 2.0f * x + 1.0f);
    }
    // Before-wrap region [1-dt, 1): approaching the discontinuity
    if (t > 1.0f - dt) {
        float x = (t - 1.0f) / dt; // normalize to (-1, 0]
        // Correction = (x^2 + 2x + 1) = (x+1)^2
        return x * x + 2.0f * x + 1.0f;
    }
    return 0.0f;
}

// =============================================================================
// 4-Point PolyBLEP (FR-002, FR-005, FR-006, FR-008)
// =============================================================================

/// @brief 4-point polynomial band-limited step correction (C3 continuity).
///
/// Higher-quality variant using a 4th-degree polynomial over a 4-sample kernel.
/// Provides better alias suppression than the 2-point version at the cost of
/// a wider correction region (2*dt on each side of the discontinuity).
///
/// Uses integrated 3rd-order B-spline basis functions (JB4,0 through JB4,3)
/// from ryukau filter_notes polyblep_residual.
///
/// @param t Normalized phase position [0, 1)
/// @param dt Normalized phase increment (frequency / sampleRate)
/// @return Correction value to subtract from naive waveform output.
///         Returns 0.0f outside the correction region [0, 2*dt) and [1-2*dt, 1).
///
/// @pre 0 < dt < 0.5 (behavior undefined at/above Nyquist)
/// @note NaN/Inf inputs are propagated per IEEE 754 without sanitization.
[[nodiscard]] constexpr float polyBlep4(float t, float dt) noexcept {
    const float dt2 = 2.0f * dt;

    // After-wrap region [0, 2*dt): samples after the discontinuity.
    // The correction uses the smoothed unit step S(u) where u = t/dt.
    // polyBlep4 = S(u) - 1 for the after-wrap side (subtract from naive).
    //
    // S(u) for u in [0,1): I_4(u+2) = 1/2 + (3(u+2)^4 - 32(u+2)^3 + 120(u+2)^2 - 176(u+2) + 80)/24
    // S(u) for u in [1,2): I_4(u+2) = 1 - (2-u)^4/24
    //
    // Simplification for u in [0,1):
    //   polyBlep4 = S(u) - 1 = -1/2 + P(u)/24
    //   where P(u) = 3(u+2)^4 - 32(u+2)^3 + 120(u+2)^2 - 176(u+2) + 80
    //
    //   Expanding in terms of u:
    //   (u+2)^4 = u^4 + 8u^3 + 24u^2 + 32u + 16
    //   (u+2)^3 = u^3 + 6u^2 + 12u + 8
    //   (u+2)^2 = u^2 + 4u + 4
    //   (u+2) = u + 2
    //   P(u) = 3(u^4+8u^3+24u^2+32u+16) - 32(u^3+6u^2+12u+8) + 120(u^2+4u+4) - 176(u+2) + 80
    //        = 3u^4+24u^3+72u^2+96u+48 - 32u^3-192u^2-384u-256 + 120u^2+480u+480 - 176u-352+80
    //        = 3u^4 + (24-32)u^3 + (72-192+120)u^2 + (96-384+480-176)u + (48-256+480-352+80)
    //        = 3u^4 - 8u^3 + 0u^2 + 16u + 0
    //        = 3u^4 - 8u^3 + 16u
    //
    //   polyBlep4 = -1/2 + (3u^4 - 8u^3 + 16u) / 24
    //
    // For u in [1,2):
    //   polyBlep4 = S(u) - 1 = -(2-u)^4/24
    if (t < dt2) {
        float u = t / dt;

        if (u < 1.0f) {
            float u2 = u * u;
            float u3 = u2 * u;
            float u4 = u3 * u;
            return -0.5f + (3.0f * u4 - 8.0f * u3 + 16.0f * u) / 24.0f;
        }
        float v = 2.0f - u;
        float v2 = v * v;
        float v4 = v2 * v2;
        return -(v4 / 24.0f);
    }

    // Before-wrap region [1-2*dt, 1): samples before the discontinuity.
    // polyBlep4 = S(u) for the before-wrap side, where u = -(1-t)/dt maps to [-2, 0).
    // By antisymmetry: polyBlep4_before(offset) = -polyBlep4_after(offset)
    // where offset is the distance from the discontinuity.
    if (t > 1.0f - dt2) {
        float u = (1.0f - t) / dt; // distance from discontinuity in sample units [0, 2)

        if (u < 1.0f) {
            // Mirror of the after-wrap [0,1) segment, with sign flip
            float u2 = u * u;
            float u3 = u2 * u;
            float u4 = u3 * u;
            return 0.5f - (3.0f * u4 - 8.0f * u3 + 16.0f * u) / 24.0f;
        }
        // Mirror of the after-wrap [1,2) segment, with sign flip
        float v = 2.0f - u;
        float v2 = v * v;
        float v4 = v2 * v2;
        return v4 / 24.0f;
    }

    return 0.0f;
}

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
[[nodiscard]] constexpr float polyBlamp(float t, float dt) noexcept {
    // After-wrap region [0, dt)
    if (t < dt) {
        float x = t / dt - 1.0f; // normalize to [-1, 0)
        // Correction = -(1/3) * x^3
        return -(1.0f / 3.0f) * x * x * x;
    }
    // Before-wrap region [1-dt, 1)
    if (t > 1.0f - dt) {
        float x = (t - 1.0f) / dt + 1.0f; // normalize to (0, 1]
        // Correction = (1/3) * x^3
        return (1.0f / 3.0f) * x * x * x;
    }
    return 0.0f;
}

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
/// The correction is computed by evaluating the 4-point BLAMP residual from
/// DAFx-16 Table 1 at the appropriate fractional position, summing all four
/// sample contributions into a single returned value.
///
/// @param t Normalized phase position [0, 1)
/// @param dt Normalized phase increment (frequency / sampleRate)
/// @return Raw correction value. Caller must scale by derivative discontinuity
///         magnitude and dt when applying.
///         Returns 0.0f outside the correction region [0, 2*dt) and [1-2*dt, 1).
///
/// @pre 0 < dt < 0.5 (behavior undefined at/above Nyquist)
/// @note NaN/Inf inputs are propagated per IEEE 754 without sanitization.
[[nodiscard]] constexpr float polyBlamp4(float t, float dt) noexcept {
    const float dt2 = 2.0f * dt;

    // After-wrap region [0, 2*dt): correction for samples after the ramp discontinuity.
    // Uses DAFx-16 Table 1 per-sample BLAMP residuals.
    // d = fractional position of discontinuity [0, 1).
    if (t < dt2) {
        if (t < dt) {
            // n=0 sample (first after discontinuity): d = t/dt
            float d = t / dt;
            float d2 = d * d;
            float d3 = d2 * d;
            float d4 = d3 * d;
            float d5 = d4 * d;
            // DAFx-16 Table 1, n=0: d^5/40 - d^4/12 + d^2/3 - d/2 + 7/30
            return d5 / 40.0f - d4 / 12.0f + d2 / 3.0f - d / 2.0f + 7.0f / 30.0f;
        }
        // n=+1 sample (second after discontinuity): d = t/dt - 1
        float d = t / dt - 1.0f;
        float d2 = d * d;
        float d3 = d2 * d;
        float d4 = d3 * d;
        float d5 = d4 * d;
        // DAFx-16 Table 1, n=+1: -d^5/120 + d^4/24 - d^3/12 + d^2/12 - d/24 + 1/120
        return -d5 / 120.0f + d4 / 24.0f - d3 / 12.0f + d2 / 12.0f - d / 24.0f + 1.0f / 120.0f;
    }

    // Before-wrap region [1-2*dt, 1): correction for samples before the ramp
    // discontinuity. BLAMP is symmetric (same sign on both sides).
    if (t > 1.0f - dt2) {
        if (t > 1.0f - dt) {
            // n=-1 sample (last before discontinuity): d = (1-t)/dt
            float d = (1.0f - t) / dt;
            float d2 = d * d;
            float d3 = d2 * d;
            float d4 = d3 * d;
            float d5 = d4 * d;
            // DAFx-16 Table 1, n=-1: -d^5/40 + d^4/24 + d^3/12 + d^2/12 + d/24 + 1/120
            return -d5 / 40.0f + d4 / 24.0f + d3 / 12.0f + d2 / 12.0f + d / 24.0f + 1.0f / 120.0f;
        }
        // n=-2 sample (second-last before discontinuity): d = (1-t)/dt - 1
        float d = (1.0f - t) / dt - 1.0f;
        float d2 = d * d;
        float d4 = d2 * d2;
        float d5 = d4 * d;
        // DAFx-16 Table 1, n=-2: d^5/120
        return d5 / 120.0f;
    }

    return 0.0f;
}

} // namespace DSP
} // namespace Krate
