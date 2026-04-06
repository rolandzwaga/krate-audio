// ==============================================================================
// Speed Curve Shape Presets
// ==============================================================================
// Pre-computed point sets for standard waveform shapes. Each function returns
// a vector of SpeedCurvePoints that, when baked, produce the named waveform.
// ==============================================================================

#pragma once

#include "speed_curve_data.h"

#include <cmath>
#include <vector>

namespace Gradus {

/// Shape preset identifiers (index into dropdown).
enum class SpeedCurvePreset {
    Flat = 0,
    Sine,
    Triangle,
    SawtoothUp,
    SawtoothDown,
    Square,
    Exponential,
    kCount  ///< sentinel — number of presets
};

inline constexpr int kSpeedCurvePresetCount = static_cast<int>(SpeedCurvePreset::kCount);

/// Human-readable names for the preset dropdown.
inline const char* speedCurvePresetName(SpeedCurvePreset p) {
    switch (p) {
        case SpeedCurvePreset::Flat:         return "Flat";
        case SpeedCurvePreset::Sine:         return "Sine";
        case SpeedCurvePreset::Triangle:     return "Triangle";
        case SpeedCurvePreset::SawtoothUp:   return "Saw Up";
        case SpeedCurvePreset::SawtoothDown: return "Saw Down";
        case SpeedCurvePreset::Square:       return "Square";
        case SpeedCurvePreset::Exponential:  return "Expo";
        default:                             return "Custom";
    }
}

// =============================================================================
// Preset Generators
// =============================================================================

/// Create a point with linear handles (no curvature).
inline SpeedCurvePoint makeLinearPoint(float x, float y,
                                        float handleSpan = 0.1f) {
    SpeedCurvePoint p;
    p.x = x; p.y = y;
    p.cpLeftX  = std::max(0.0f, x - handleSpan);
    p.cpLeftY  = y;
    p.cpRightX = std::min(1.0f, x + handleSpan);
    p.cpRightY = y;
    return p;
}

/// Flat: constant y=0.5 (no modulation).
inline std::vector<SpeedCurvePoint> generateFlatPreset() {
    SpeedCurvePoint p0;
    p0.x = 0.0f; p0.y = 0.5f;
    p0.cpLeftX = 0.0f; p0.cpLeftY = 0.5f;
    p0.cpRightX = 0.33f; p0.cpRightY = 0.5f;

    SpeedCurvePoint p1;
    p1.x = 1.0f; p1.y = 0.5f;
    p1.cpLeftX = 0.67f; p1.cpLeftY = 0.5f;
    p1.cpRightX = 1.0f; p1.cpRightY = 0.5f;

    return {p0, p1};
}

/// Sine: half-cycle sine wave from 0 to 1 and back to 0.
/// Uses bezier kappa ≈ 0.5524 for quarter-circle approximation.
inline std::vector<SpeedCurvePoint> generateSinePreset() {
    constexpr float kappa = 0.5524f;

    SpeedCurvePoint p0;
    p0.x = 0.0f; p0.y = 0.5f;
    p0.cpLeftX = 0.0f; p0.cpLeftY = 0.5f;
    p0.cpRightX = 0.25f * kappa; p0.cpRightY = 0.5f;

    SpeedCurvePoint p1;
    p1.x = 0.25f; p1.y = 1.0f;
    p1.cpLeftX = 0.25f - 0.25f * kappa; p1.cpLeftY = 1.0f;
    p1.cpRightX = 0.25f + 0.25f * kappa; p1.cpRightY = 1.0f;

    SpeedCurvePoint p2;
    p2.x = 0.5f; p2.y = 0.5f;
    p2.cpLeftX = 0.5f - 0.25f * kappa; p2.cpLeftY = 0.5f;
    p2.cpRightX = 0.5f + 0.25f * kappa; p2.cpRightY = 0.5f;

    SpeedCurvePoint p3;
    p3.x = 0.75f; p3.y = 0.0f;
    p3.cpLeftX = 0.75f - 0.25f * kappa; p3.cpLeftY = 0.0f;
    p3.cpRightX = 0.75f + 0.25f * kappa; p3.cpRightY = 0.0f;

    SpeedCurvePoint p4;
    p4.x = 1.0f; p4.y = 0.5f;
    p4.cpLeftX = 1.0f - 0.25f * kappa; p4.cpLeftY = 0.5f;
    p4.cpRightX = 1.0f; p4.cpRightY = 0.5f;

    return {p0, p1, p2, p3, p4};
}

/// Triangle: linear ramp up to peak at center, then back down.
inline std::vector<SpeedCurvePoint> generateTrianglePreset() {
    return {
        makeLinearPoint(0.0f, 0.0f, 0.17f),
        makeLinearPoint(0.5f, 1.0f, 0.17f),
        makeLinearPoint(1.0f, 0.0f, 0.17f),
    };
}

/// Sawtooth Up: linear ramp from bottom to top.
inline std::vector<SpeedCurvePoint> generateSawtoothUpPreset() {
    return {
        makeLinearPoint(0.0f, 0.0f, 0.33f),
        makeLinearPoint(1.0f, 1.0f, 0.33f),
    };
}

/// Sawtooth Down: linear ramp from top to bottom.
inline std::vector<SpeedCurvePoint> generateSawtoothDownPreset() {
    return {
        makeLinearPoint(0.0f, 1.0f, 0.33f),
        makeLinearPoint(1.0f, 0.0f, 0.33f),
    };
}

/// Square: step function with fast transitions.
inline std::vector<SpeedCurvePoint> generateSquarePreset() {
    constexpr float edge = 0.01f;  // near-vertical transition width
    return {
        makeLinearPoint(0.0f, 1.0f, edge),
        makeLinearPoint(0.5f - edge, 1.0f, edge),
        makeLinearPoint(0.5f, 0.0f, edge),
        makeLinearPoint(1.0f - edge, 0.0f, edge),
        makeLinearPoint(1.0f, 0.0f, edge),
    };
}

/// Exponential: accelerating curve (slow start, fast end).
inline std::vector<SpeedCurvePoint> generateExponentialPreset() {
    SpeedCurvePoint p0;
    p0.x = 0.0f; p0.y = 0.0f;
    p0.cpLeftX = 0.0f; p0.cpLeftY = 0.0f;
    p0.cpRightX = 0.6f; p0.cpRightY = 0.0f;

    SpeedCurvePoint p1;
    p1.x = 1.0f; p1.y = 1.0f;
    p1.cpLeftX = 0.9f; p1.cpLeftY = 1.0f;
    p1.cpRightX = 1.0f; p1.cpRightY = 1.0f;

    return {p0, p1};
}

/// Generate points for a given preset type.
inline std::vector<SpeedCurvePoint> generatePreset(SpeedCurvePreset preset) {
    switch (preset) {
        case SpeedCurvePreset::Flat:         return generateFlatPreset();
        case SpeedCurvePreset::Sine:         return generateSinePreset();
        case SpeedCurvePreset::Triangle:     return generateTrianglePreset();
        case SpeedCurvePreset::SawtoothUp:   return generateSawtoothUpPreset();
        case SpeedCurvePreset::SawtoothDown: return generateSawtoothDownPreset();
        case SpeedCurvePreset::Square:       return generateSquarePreset();
        case SpeedCurvePreset::Exponential:  return generateExponentialPreset();
        default:                             return generateFlatPreset();
    }
}

} // namespace Gradus
