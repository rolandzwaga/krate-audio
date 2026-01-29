// ==============================================================================
// API Contract: Sweep-Morph Link Curves
// ==============================================================================
// Pure functions for mapping normalized sweep frequency to morph position.
// These curves define how sweep position drives morph position when linked.
//
// Layer: Layer 0 (core) - pure math functions, no state
//
// Reference: specs/007-sweep-system/spec.md (FR-014 to FR-022)
// Reference: specs/Disrumpo/dsp-details.md Section 8
// ==============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Disrumpo {

/// @brief Morph link mode enumeration.
///
/// Defines how sweep frequency position maps to morph XY position.
/// Extended from original 7 modes to include Custom (8 modes total).
enum class MorphLinkMode : uint8_t {
    None = 0,       ///< Manual control only, no link to sweep
    SweepFreq,      ///< Linear mapping: y = x
    InverseSweep,   ///< Inverted: y = 1 - x
    EaseIn,         ///< Quadratic curve: y = x^2
    EaseOut,        ///< Inverse quadratic: y = 1 - (1-x)^2
    HoldRise,       ///< Hold at 0 until 60%, then rise: y = 0 if x < 0.6, else (x-0.6)/0.4
    Stepped,        ///< Quantize to 4 levels: y = floor(x*4)/3
    Custom,         ///< User-defined breakpoint curve
    COUNT           ///< Sentinel for iteration (8 modes)
};

/// @brief Total number of morph link modes.
constexpr int kMorphLinkModeCount = static_cast<int>(MorphLinkMode::COUNT);

/// @brief Get display name for a morph link mode.
/// @param mode The morph link mode
/// @return C-string display name
constexpr const char* getMorphLinkModeName(MorphLinkMode mode) noexcept {
    switch (mode) {
        case MorphLinkMode::None:         return "None";
        case MorphLinkMode::SweepFreq:    return "Sweep Freq";
        case MorphLinkMode::InverseSweep: return "Inverse Sweep";
        case MorphLinkMode::EaseIn:       return "Ease In";
        case MorphLinkMode::EaseOut:      return "Ease Out";
        case MorphLinkMode::HoldRise:     return "Hold-Rise";
        case MorphLinkMode::Stepped:      return "Stepped";
        case MorphLinkMode::Custom:       return "Custom";
        default:                          return "Unknown";
    }
}

// =============================================================================
// Morph Link Curve Functions
// =============================================================================

/// @brief Apply morph link curve to normalized sweep frequency.
///
/// Converts a normalized sweep frequency position [0, 1] to a morph position [0, 1]
/// using the specified curve. For Custom mode, use CustomCurve::evaluate() instead.
///
/// @param mode Morph link curve type
/// @param x Normalized sweep frequency [0, 1] where 0 = 20Hz, 1 = 20kHz
/// @return Morph position [0, 1]
/// @note For Custom mode, returns x (linear) - use CustomCurve::evaluate() for custom curves
[[nodiscard]] inline float applyMorphLinkCurve(MorphLinkMode mode, float x) noexcept {
    // Clamp input to valid range
    x = std::clamp(x, 0.0f, 1.0f);

    switch (mode) {
        case MorphLinkMode::None:
            // Manual control - return center position
            return 0.5f;

        case MorphLinkMode::SweepFreq:
            // Linear: y = x
            return x;

        case MorphLinkMode::InverseSweep:
            // Inverse: y = 1 - x
            return 1.0f - x;

        case MorphLinkMode::EaseIn:
            // Quadratic (slow start, fast end): y = x^2
            return x * x;

        case MorphLinkMode::EaseOut:
            // Inverse quadratic (fast start, slow end): y = 1 - (1-x)^2
            return 1.0f - (1.0f - x) * (1.0f - x);

        case MorphLinkMode::HoldRise:
            // Hold at 0 until 60%, then rise to 1
            // y = 0 if x < 0.6, else (x - 0.6) / 0.4
            if (x < 0.6f) {
                return 0.0f;
            }
            return (x - 0.6f) / 0.4f;

        case MorphLinkMode::Stepped:
            // Quantize to 4 discrete levels: 0, 0.333, 0.667, 1.0
            // y = floor(x * 4) / 3
            return std::floor(x * 4.0f) / 3.0f;

        case MorphLinkMode::Custom:
            // Custom mode should use CustomCurve::evaluate()
            // Fall back to linear if called directly
            return x;

        default:
            return x;
    }
}

// =============================================================================
// Frequency Normalization
// =============================================================================

/// @brief Minimum frequency for normalization (Hz).
constexpr float kNormMinFreqHz = 20.0f;

/// @brief Maximum frequency for normalization (Hz).
constexpr float kNormMaxFreqHz = 20000.0f;

/// @brief Pre-computed log2 of minimum frequency.
constexpr float kLog2MinFreq = 4.321928f;  // log2(20)

/// @brief Pre-computed log2 of maximum frequency.
constexpr float kLog2MaxFreq = 14.287712f; // log2(20000)

/// @brief Pre-computed range for normalization.
constexpr float kLog2FreqRange = kLog2MaxFreq - kLog2MinFreq;  // ~9.966

/// @brief Normalize sweep frequency to [0, 1] range.
///
/// Uses logarithmic mapping: x = (log2(freq) - log2(20)) / (log2(20000) - log2(20))
///
/// @param freqHz Frequency in Hz [20, 20000]
/// @return Normalized position [0, 1]
[[nodiscard]] inline float normalizeSweepFrequency(float freqHz) noexcept {
    freqHz = std::clamp(freqHz, kNormMinFreqHz, kNormMaxFreqHz);
    float log2Freq = std::log2(freqHz);
    return (log2Freq - kLog2MinFreq) / kLog2FreqRange;
}

/// @brief Denormalize [0, 1] to sweep frequency in Hz.
///
/// @param normalized Normalized position [0, 1]
/// @return Frequency in Hz [20, 20000]
[[nodiscard]] inline float denormalizeSweepFrequency(float normalized) noexcept {
    normalized = std::clamp(normalized, 0.0f, 1.0f);
    float log2Freq = kLog2MinFreq + normalized * kLog2FreqRange;
    return std::pow(2.0f, log2Freq);
}

// =============================================================================
// Intensity Calculation Functions
// =============================================================================

/// @brief Calculate Gaussian (Smooth) intensity for a band.
///
/// Per spec FR-008: intensity = intensityParam * exp(-0.5 * (distanceOctaves / sigma)^2)
/// Per spec FR-009: distanceOctaves = abs(log2(bandFreq) - log2(sweepCenterFreq))
/// Per spec FR-010: Multiplicative scaling preserves shape
///
/// @param bandFreqHz Band center frequency in Hz
/// @param sweepCenterHz Sweep center frequency in Hz
/// @param widthOctaves Sweep width in octaves (sigma = width / 2)
/// @param intensityParam Intensity parameter [0, 2] where 1.0 = 100%
/// @return Intensity multiplier [0, 2]
[[nodiscard]] inline float calculateGaussianIntensity(
    float bandFreqHz,
    float sweepCenterHz,
    float widthOctaves,
    float intensityParam
) noexcept {
    // Distance in octave space (FR-009)
    float distanceOctaves = std::abs(std::log2(bandFreqHz) - std::log2(sweepCenterHz));

    // Sigma = width / 2 (per spec FR-006)
    float sigma = widthOctaves / 2.0f;

    // Avoid division by zero
    if (sigma < 0.001f) {
        sigma = 0.001f;
    }

    // Gaussian falloff (FR-008)
    float normalizedDist = distanceOctaves / sigma;
    float exponent = -0.5f * normalizedDist * normalizedDist;
    float falloff = std::exp(exponent);

    // Scale by intensity (FR-010) - multiplicative scaling
    return intensityParam * falloff;
}

/// @brief Calculate Sharp (linear) intensity for a band.
///
/// Per spec FR-006a: intensity = intensityParam * max(0, 1 - abs(distanceOctaves) / (width / 2))
/// Produces exactly 0.0 at the edge (distance = width/2) and beyond.
///
/// @param bandFreqHz Band center frequency in Hz
/// @param sweepCenterHz Sweep center frequency in Hz
/// @param widthOctaves Sweep width in octaves
/// @param intensityParam Intensity parameter [0, 2] where 1.0 = 100%
/// @return Intensity multiplier [0, 2]
[[nodiscard]] inline float calculateLinearFalloff(
    float bandFreqHz,
    float sweepCenterHz,
    float widthOctaves,
    float intensityParam
) noexcept {
    // Distance in octave space
    float distanceOctaves = std::abs(std::log2(bandFreqHz) - std::log2(sweepCenterHz));

    // Half width is the edge
    float halfWidth = widthOctaves / 2.0f;

    // Avoid division by zero
    if (halfWidth < 0.001f) {
        halfWidth = 0.001f;
    }

    // Linear falloff, exactly 0.0 at edge and beyond
    float falloff = std::max(0.0f, 1.0f - distanceOctaves / halfWidth);

    // Scale by intensity - multiplicative scaling
    return intensityParam * falloff;
}

} // namespace Disrumpo
