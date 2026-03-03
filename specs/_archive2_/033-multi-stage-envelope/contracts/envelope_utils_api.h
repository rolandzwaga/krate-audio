// ==============================================================================
// API Contract: Shared Envelope Utilities
// ==============================================================================
// Layer 1 (Primitives) - depends on Layer 0 only
// Namespace: Krate::DSP
// Header: dsp/include/krate/dsp/primitives/envelope_utils.h
//
// Extracted from adsr_envelope.h to be shared between ADSREnvelope and
// MultiStageEnvelope. This is a refactoring -- no new functionality, just
// relocation of existing types and functions.
//
// After extraction, adsr_envelope.h will #include this header.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Constants (from adsr_envelope.h)
// =============================================================================

inline constexpr float kEnvelopeIdleThreshold = 1e-4f;
inline constexpr float kMinEnvelopeTimeMs = 0.1f;
inline constexpr float kMaxEnvelopeTimeMs = 10000.0f;
inline constexpr float kSustainSmoothTimeMs = 5.0f;
inline constexpr float kDefaultTargetRatioA = 0.3f;
inline constexpr float kDefaultTargetRatioDR = 0.0001f;
inline constexpr float kLinearTargetRatio = 100.0f;

// =============================================================================
// Enumerations (from adsr_envelope.h)
// =============================================================================

enum class EnvCurve : uint8_t {
    Exponential = 0,
    Linear,
    Logarithmic
};

enum class RetriggerMode : uint8_t {
    Hard = 0,
    Legato
};

// =============================================================================
// Coefficient Calculation (from ADSREnvelope::calcCoefficients)
// =============================================================================

struct StageCoefficients {
    float coef = 0.0f;
    float base = 0.0f;
};

/// Calculate one-pole coefficients for envelope stage transitions.
/// Uses the EarLevel Engineering method.
///
/// @param timeMs     Stage duration in milliseconds
/// @param sampleRate Sample rate in Hz
/// @param targetLevel Target level for the transition
/// @param targetRatio Controls curve shape (small = steep, large = linear)
/// @param rising     true for attack-like (rising), false for decay-like (falling)
/// @return StageCoefficients with coef and base values
[[nodiscard]] inline StageCoefficients calcEnvCoefficients(
    float timeMs, float sampleRate,
    float targetLevel, float targetRatio, bool rising) noexcept
{
    StageCoefficients result;
    float rate = timeMs * 0.001f * sampleRate;
    if (rate < 1.0f) rate = 1.0f;

    result.coef = std::exp(-std::log((1.0f + targetRatio) / targetRatio) / rate);

    if (rising) {
        result.base = (targetLevel + targetRatio) * (1.0f - result.coef);
    } else {
        result.base = (targetLevel - targetRatio) * (1.0f - result.coef);
    }

    return result;
}

/// Get the target ratio for attack-like (rising) curves.
[[nodiscard]] inline float getAttackTargetRatio(EnvCurve curve) noexcept {
    switch (curve) {
        case EnvCurve::Exponential:  return kDefaultTargetRatioA;
        case EnvCurve::Linear:       return kLinearTargetRatio;
        case EnvCurve::Logarithmic:  return kDefaultTargetRatioA; // Not used for log
    }
    return kDefaultTargetRatioA;
}

/// Get the target ratio for decay-like (falling) curves.
[[nodiscard]] inline float getDecayTargetRatio(EnvCurve curve) noexcept {
    switch (curve) {
        case EnvCurve::Exponential:  return kDefaultTargetRatioDR;
        case EnvCurve::Linear:       return kLinearTargetRatio;
        case EnvCurve::Logarithmic:  return kDefaultTargetRatioDR; // Not used for log
    }
    return kDefaultTargetRatioDR;
}

} // namespace DSP
} // namespace Krate
