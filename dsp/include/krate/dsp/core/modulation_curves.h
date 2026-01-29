// ==============================================================================
// Layer 0: Core Utility - Modulation Curves
// ==============================================================================
// Pure math functions for modulation response curve shaping.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, stateless)
// - Principle III: Modern C++ (constexpr, inline)
// - Principle IX: Layer 0 (no dependencies on higher layers)
//
// Reference: specs/008-modulation-system/spec.md (FR-058, FR-059)
// ==============================================================================

#pragma once

#include <krate/dsp/core/modulation_types.h>

#include <algorithm>
#include <cmath>

namespace Krate {
namespace DSP {

/// @brief Apply modulation curve to a [0, 1] input value.
///
/// Per spec FR-058, curves shape the modulation response:
/// - Linear: y = x (transparent)
/// - Exponential: y = x^2 (slow start, fast end)
/// - S-Curve: y = x^2 * (3 - 2x) (smoothstep)
/// - Stepped: y = floor(x * 4) / 3 (4 discrete levels)
///
/// @param curve The curve shape to apply
/// @param x Input value, expected in [0, 1]
/// @return Shaped value in [0, 1]
///
/// @note For bipolar modulation (FR-059): pass abs(sourceValue),
///       then multiply result by routing amount (which carries the sign).
[[nodiscard]] inline float applyModCurve(ModCurve curve, float x) noexcept {
    x = std::clamp(x, 0.0f, 1.0f);

    switch (curve) {
        case ModCurve::Linear:
            return x;

        case ModCurve::Exponential:
            return x * x;

        case ModCurve::SCurve:
            return x * x * (3.0f - 2.0f * x);

        case ModCurve::Stepped:
            return std::floor(x * 4.0f) / 3.0f;
    }

    return x;  // Fallback for invalid enum
}

/// @brief Apply modulation curve with bipolar source handling.
///
/// Per spec FR-059: curve applied to abs(source) to shape the magnitude,
/// then source sign is preserved and amount is applied.
/// Formula: output = sign(source) * applyModCurve(curve, abs(source)) * amount
///
/// This preserves the bipolar nature of the source (e.g., LFO oscillation)
/// while allowing the curve to shape the response and amount to scale/invert.
///
/// @param curve Curve shape
/// @param sourceValue Raw source output (can be negative)
/// @param amount Routing amount [-1, +1] (carries sign/scale)
/// @return Modulation contribution for this routing
[[nodiscard]] inline float applyBipolarModulation(
    ModCurve curve,
    float sourceValue,
    float amount
) noexcept {
    const float absSource = std::abs(sourceValue);
    const float curved = applyModCurve(curve, absSource);
    // Preserve source sign: negative source produces negative output
    const float sign = (sourceValue >= 0.0f) ? 1.0f : -1.0f;
    return sign * curved * amount;
}

}  // namespace DSP
}  // namespace Krate
