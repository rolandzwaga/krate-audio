// ==============================================================================
// Layer 1: DSP Primitive - Shared Envelope Utilities
// ==============================================================================
// Shared constants, enumerations, and coefficient calculation for envelope
// generators (ADSREnvelope, MultiStageEnvelope).
//
// Extracted from adsr_envelope.h to eliminate code duplication (DRY).
// Uses the EarLevel Engineering one-pole iterative method for coefficient
// calculation.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations)
// - Principle III: Modern C++ (constexpr, value semantics, C++20)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle XIV: ODR Prevention (shared header, no duplication)
//
// Reference: specs/033-multi-stage-envelope/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Compiler Compatibility Macros
// =============================================================================

#ifndef ITERUM_NOINLINE
#if defined(_MSC_VER)
#define ITERUM_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define ITERUM_NOINLINE __attribute__((noinline))
#else
#define ITERUM_NOINLINE
#endif
#endif

// =============================================================================
// Constants
// =============================================================================

inline constexpr float kEnvelopeIdleThreshold = 1e-4f;
inline constexpr float kMinEnvelopeTimeMs = 0.1f;
inline constexpr float kMaxEnvelopeTimeMs = 10000.0f;
inline constexpr float kSustainSmoothTimeMs = 5.0f;
inline constexpr float kDefaultTargetRatioA = 0.3f;
inline constexpr float kDefaultTargetRatioDR = 0.0001f;
inline constexpr float kLinearTargetRatio = 100.0f;

// =============================================================================
// Enumerations
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
// Coefficient Calculation
// =============================================================================

/// Coefficients for one-pole envelope stage transitions.
struct StageCoefficients {
    float coef = 0.0f;
    float base = 0.0f;
};

/// Calculate one-pole coefficients for envelope stage transitions.
/// Uses the EarLevel Engineering method:
///   rate = timeMs * 0.001 * sampleRate
///   coef = exp(-log((1 + targetRatio) / targetRatio) / rate)
///   base = (target +/- targetRatio) * (1 - coef)
///
/// @param timeMs      Stage duration in milliseconds
/// @param sampleRate  Sample rate in Hz
/// @param targetLevel Target level for the transition
/// @param targetRatio Controls curve shape (small = steep exponential, large = linear)
/// @param rising      true for attack-like (rising), false for decay-like (falling)
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
/// @param curve The curve shape
/// @return Target ratio value
[[nodiscard]] inline float getAttackTargetRatio(EnvCurve curve) noexcept {
    switch (curve) {
        case EnvCurve::Exponential:  return kDefaultTargetRatioA;
        case EnvCurve::Linear:       return kLinearTargetRatio;
        case EnvCurve::Logarithmic:  return kDefaultTargetRatioA; // Not used for log
    }
    return kDefaultTargetRatioA;
}

/// Get the target ratio for decay-like (falling) curves.
/// @param curve The curve shape
/// @return Target ratio value
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
