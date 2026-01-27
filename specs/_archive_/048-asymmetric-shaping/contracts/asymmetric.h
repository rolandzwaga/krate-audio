// ==============================================================================
// API Contract: Asymmetric Shaping Functions
// ==============================================================================
// This file documents the expected API for spec 048-asymmetric-shaping.
// The actual implementation resides in dsp/include/krate/dsp/core/sigmoid.h
// within the Krate::DSP::Asymmetric namespace.
//
// Reference: specs/048-asymmetric-shaping/spec.md
// ==============================================================================

#pragma once

namespace Krate {
namespace DSP {
namespace Asymmetric {

// =============================================================================
// FR-001: WithBias Template Function
// =============================================================================

/// @brief Apply DC bias to symmetric function to create asymmetry.
///
/// Creates asymmetry by shifting the operating point on a symmetric sigmoid,
/// then removing the DC offset to maintain neutrality.
///
/// Formula: output = saturator(input + bias) - saturator(bias)
///
/// @tparam Func Callable with signature float(float) noexcept
/// @param x Input value
/// @param bias DC bias to add before sigmoid (typical range: -1.0 to 1.0)
/// @param func Symmetric sigmoid function to apply (e.g., Sigmoid::tanh)
/// @return Asymmetrically saturated output, DC neutral
///
/// @pre bias is finite
/// @post If x == 0 and bias is finite, output == 0 (DC neutral)
///
/// @example
/// @code
/// // Create subtle tube-like asymmetry from tanh
/// float out = Asymmetric::withBias(input, 0.2f, Sigmoid::tanh);
/// @endcode
template <typename Func>
[[nodiscard]] inline float withBias(float x, float bias, Func func) noexcept;

// =============================================================================
// FR-002: DualCurve Function
// =============================================================================

/// @brief Apply different saturation gains to positive/negative half-waves.
///
/// Uses tanh as the base curve. When gains differ, creates asymmetry
/// that produces even harmonics.
///
/// @param x Input value
/// @param posGain Drive gain for positive half-wave (x >= 0)
/// @param negGain Drive gain for negative half-wave (x < 0)
/// @return Saturated output in range [-1, 1]
///
/// @pre posGain >= 0 and negGain >= 0
/// @post When posGain == negGain, output matches symmetric tanh
/// @post When posGain != negGain, even harmonics are generated
///
/// @example
/// @code
/// // More saturation on positive half-wave
/// float out = Asymmetric::dualCurve(input, 2.0f, 1.0f);
/// @endcode
[[nodiscard]] inline float dualCurve(float x, float posGain, float negGain) noexcept;

// =============================================================================
// FR-003: Diode Clipping Function
// =============================================================================

/// @brief Diode-style asymmetric clipping.
///
/// Models diode conduction characteristics:
/// - Forward bias (x >= 0): Soft exponential saturation
/// - Reverse bias (x < 0): Harder rational function curve
///
/// Creates characteristic diode asymmetry with subtle even harmonics.
///
/// @param x Input value
/// @return Asymmetrically clipped output
///
/// @note Forward: 1 - exp(-1.5*x)
/// @note Reverse: x / (1 - 0.5*x)
///
/// @example
/// @code
/// float out = Asymmetric::diode(input);
/// @endcode
[[nodiscard]] inline float diode(float x) noexcept;

// =============================================================================
// FR-004: Tube Polynomial Function
// =============================================================================

/// @brief Tube-style asymmetric saturation with even harmonics.
///
/// Uses polynomial with both odd and even-order terms, then soft-limited
/// via tanh. The x^2 term creates 2nd harmonic emphasis characteristic
/// of vacuum tube amplifiers.
///
/// Formula: tanh(x + 0.3*x^2 - 0.15*x^3)
///
/// @param x Input value
/// @return Asymmetrically saturated output in range [-1, 1]
///
/// @note Produces rich harmonic content with 2nd harmonic emphasis
///
/// @example
/// @code
/// float out = Asymmetric::tube(input);
/// @endcode
[[nodiscard]] inline float tube(float x) noexcept;

} // namespace Asymmetric
} // namespace DSP
} // namespace Krate
