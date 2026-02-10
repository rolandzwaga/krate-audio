// ==============================================================================
// Layer 0: Core Utility - Curve Lookup Table Generation
// ==============================================================================
// Shared utility for generating 256-entry curve lookup tables used by both
// ADSREnvelope (Layer 1) and ADSRDisplay (UI).
//
// Provides:
//   - generatePowerCurveTable: Power curve (phase^exponent) lookup tables
//   - generateBezierCurveTable: Cubic Bezier curve lookup tables
//   - lookupCurveTable: Linear interpolation in lookup tables
//   - envCurveToCurveAmount: Convert EnvCurve enum to continuous float
//   - bezierToSimpleCurve: Derive curve amount from Bezier control points
//   - simpleCurveToBezier: Generate Bezier control points from curve amount
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations)
// - Principle III: Modern C++ (constexpr, value semantics, C++20)
// - Principle IX: Layer 0 (depends only on standard library)
// - Principle XIV: ODR Prevention (unique names, no duplication)
//
// Reference: specs/048-adsr-display/contracts/curve-table-api.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/envelope_utils.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace Krate {
namespace DSP {

// =============================================================================
// Constants
// =============================================================================

/// Number of entries in each curve lookup table.
inline constexpr size_t kCurveTableSize = 256;

/// Controls the curvature range. With k=3:
///   curve = -1.0 -> exponent = 2^(-3) = 0.125 (very logarithmic)
///   curve =  0.0 -> exponent = 2^(0)  = 1.0   (linear)
///   curve = +1.0 -> exponent = 2^(+3) = 8.0   (very exponential)
inline constexpr float kCurveRangeK = 3.0f;

// =============================================================================
// Power Curve Table Generation
// =============================================================================

/// Generate a power curve lookup table.
///
/// For each table entry i (0 to 255):
///   phase = i / 255.0
///   exponent = 2^(curveAmount * kCurveRangeK)
///   table[i] = startLevel + (endLevel - startLevel) * phase^exponent
///
/// Special case: curveAmount == 0.0 produces a linear ramp.
///
/// @param table        Output array (256 entries)
/// @param curveAmount  Curve shape [-1, +1]
/// @param startLevel   Level at phase=0 (default 0.0)
/// @param endLevel     Level at phase=1 (default 1.0)
inline void generatePowerCurveTable(
    std::array<float, kCurveTableSize>& table,
    float curveAmount,
    float startLevel = 0.0f,
    float endLevel = 1.0f) noexcept
{
    float exponent = std::pow(2.0f, curveAmount * kCurveRangeK);
    float range = endLevel - startLevel;

    for (size_t i = 0; i < kCurveTableSize; ++i) {
        float phase = static_cast<float>(i) / 255.0f;
        float shaped = std::pow(phase, exponent);
        table[i] = startLevel + range * shaped;
    }
}

// =============================================================================
// Bezier Curve Table Generation
// =============================================================================

/// Generate a cubic Bezier curve lookup table.
///
/// The Bezier curve is defined by 4 control points:
///   P0 = (0, startLevel)
///   P1 = (cp1x, lerp(startLevel, endLevel, cp1y))
///   P2 = (cp2x, lerp(startLevel, endLevel, cp2y))
///   P3 = (1, endLevel)
///
/// The table is indexed by uniform phase (x-coordinate).
/// Implementation: evaluate Bezier at 1024 uniform t values,
/// then resample to 256 uniform x values via linear interpolation.
///
/// @param table        Output array (256 entries)
/// @param cp1x, cp1y  Control point 1 (normalized [0,1] within segment)
/// @param cp2x, cp2y  Control point 2 (normalized [0,1] within segment)
/// @param startLevel   Level at phase=0 (default 0.0)
/// @param endLevel     Level at phase=1 (default 1.0)
inline void generateBezierCurveTable(
    std::array<float, kCurveTableSize>& table,
    float cp1x, float cp1y,
    float cp2x, float cp2y,
    float startLevel = 0.0f,
    float endLevel = 1.0f) noexcept
{
    // Map normalized cp Y values to actual levels
    float range = endLevel - startLevel;
    float p0y = startLevel;
    float p1y = startLevel + range * cp1y;
    float p2y = startLevel + range * cp2y;
    float p3y = endLevel;

    // P0.x = 0, P1.x = cp1x, P2.x = cp2x, P3.x = 1
    float p0x = 0.0f;
    float p1x = cp1x;
    float p2x = cp2x;
    float p3x = 1.0f;

    // Evaluate Bezier at 1024 uniform t values
    constexpr int kNumSamples = 1024;
    std::array<float, kNumSamples + 1> xValues{};
    std::array<float, kNumSamples + 1> yValues{};

    for (int i = 0; i <= kNumSamples; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kNumSamples);
        float oneMinusT = 1.0f - t;
        float oneMinusT2 = oneMinusT * oneMinusT;
        float oneMinusT3 = oneMinusT2 * oneMinusT;
        float t2 = t * t;
        float t3 = t2 * t;

        // Cubic Bezier: B(t) = (1-t)^3*P0 + 3(1-t)^2*t*P1 + 3(1-t)*t^2*P2 + t^3*P3
        xValues[i] = oneMinusT3 * p0x + 3.0f * oneMinusT2 * t * p1x
                     + 3.0f * oneMinusT * t2 * p2x + t3 * p3x;
        yValues[i] = oneMinusT3 * p0y + 3.0f * oneMinusT2 * t * p1y
                     + 3.0f * oneMinusT * t2 * p2y + t3 * p3y;
    }

    // Resample to 256 uniform x values via linear interpolation
    int searchStart = 0;
    for (size_t i = 0; i < kCurveTableSize; ++i) {
        float targetX = static_cast<float>(i) / 255.0f;

        // Find the segment in xValues that contains targetX
        int j = searchStart;
        while (j < kNumSamples && xValues[j + 1] < targetX) {
            ++j;
        }
        searchStart = j;

        if (j >= kNumSamples) {
            table[i] = yValues[kNumSamples];
        } else {
            float xSpan = xValues[j + 1] - xValues[j];
            if (xSpan < 1e-8f) {
                table[i] = yValues[j];
            } else {
                float frac = (targetX - xValues[j]) / xSpan;
                table[i] = yValues[j] + frac * (yValues[j + 1] - yValues[j]);
            }
        }
    }
}

// =============================================================================
// Table Lookup with Linear Interpolation
// =============================================================================

/// Linearly interpolate a value from the 256-entry curve table.
///
/// @param table  The lookup table
/// @param phase  Normalized phase [0, 1]
/// @return Interpolated output value
[[nodiscard]] inline float lookupCurveTable(
    const std::array<float, kCurveTableSize>& table,
    float phase) noexcept
{
    float index = phase * 255.0f;
    int i0 = static_cast<int>(index);
    i0 = std::clamp(i0, 0, 254);
    float frac = index - static_cast<float>(i0);
    return table[static_cast<size_t>(i0)]
           + frac * (table[static_cast<size_t>(i0) + 1] - table[static_cast<size_t>(i0)]);
}

// =============================================================================
// Conversion Functions
// =============================================================================

/// Convert the discrete EnvCurve enum to a continuous curve amount.
/// Preserves backward compatibility.
[[nodiscard]] inline float envCurveToCurveAmount(EnvCurve curve) noexcept {
    switch (curve) {
        case EnvCurve::Logarithmic:  return -0.7f;
        case EnvCurve::Linear:       return  0.0f;
        case EnvCurve::Exponential:  return +0.7f;
    }
    return 0.0f;
}

/// Derive the simple curve amount from a Bezier curve.
/// Samples the Bezier at phase=0.5 and finds the power curve
/// that matches: output_50 = 0.5^(2^(c * k))
/// Solving: c = log2(log(output_50) / log(0.5)) / k
///
/// Returns 0.0 if the Bezier produces exactly linear output at phase 0.5.
[[nodiscard]] inline float bezierToSimpleCurve(
    float cp1x, float cp1y,
    float cp2x, float cp2y,
    float startLevel = 0.0f,
    float endLevel = 1.0f) noexcept
{
    // Generate a Bezier table and sample at the midpoint
    std::array<float, kCurveTableSize> table{};
    generateBezierCurveTable(table, cp1x, cp1y, cp2x, cp2y, startLevel, endLevel);

    // Sample at phase 0.5 (index 127-128)
    float output50 = lookupCurveTable(table, 0.5f);

    // Normalize to [0,1] range
    float range = endLevel - startLevel;
    if (std::abs(range) < 1e-8f) return 0.0f;
    float normalizedOutput = (output50 - startLevel) / range;

    // For the power curve: normalizedOutput = 0.5^exponent
    // exponent = 2^(curveAmount * k)
    // So: log(normalizedOutput) / log(0.5) = exponent
    //     curveAmount = log2(exponent) / k

    // Guard against edge cases
    if (normalizedOutput <= 0.0f || normalizedOutput >= 1.0f) return 0.0f;

    float exponent = std::log(normalizedOutput) / std::log(0.5f);
    if (exponent <= 0.0f) return 0.0f;

    float curveAmount = std::log2(exponent) / kCurveRangeK;
    return std::clamp(curveAmount, -1.0f, 1.0f);
}

/// Generate Bezier control points that approximate a power curve.
/// Places cp1 at (1/3, powerCurve(1/3)) and cp2 at (2/3, powerCurve(2/3)).
inline void simpleCurveToBezier(
    float curveAmount,
    float& cp1x, float& cp1y,
    float& cp2x, float& cp2y) noexcept
{
    float exponent = std::pow(2.0f, curveAmount * kCurveRangeK);

    cp1x = 1.0f / 3.0f;
    cp1y = std::pow(1.0f / 3.0f, exponent);

    cp2x = 2.0f / 3.0f;
    cp2y = std::pow(2.0f / 3.0f, exponent);
}

} // namespace DSP
} // namespace Krate
