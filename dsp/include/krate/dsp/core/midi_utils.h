// ==============================================================================
// Layer 0: Core Utilities
// midi_utils.h - MIDI Note and Velocity Conversion Functions
// ==============================================================================
// Constitution Principle II: Real-Time Audio Thread Safety
// - No allocation, no locks, no exceptions, no I/O
//
// Constitution Principle III: Modern C++ Standards
// - constexpr, const, value semantics
//
// Constitution Principle IX: Layered DSP Architecture
// - Layer 0: NO dependencies on higher layers
//
// Feature: 088-self-osc-filter
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>  // For detail::constexprExp

#include <algorithm>  // For std::clamp
#include <cmath>     // For std::sqrt, std::pow
#include <cstdint>

namespace Krate {
namespace DSP {

// ==============================================================================
// Constants
// ==============================================================================

/// Standard A4 reference frequency in Hz
inline constexpr float kA4FrequencyHz = 440.0f;

/// MIDI note number for A4
inline constexpr int kA4MidiNote = 69;

/// Minimum valid MIDI note number
inline constexpr int kMinMidiNote = 0;

/// Maximum valid MIDI note number
inline constexpr int kMaxMidiNote = 127;

/// Minimum valid MIDI velocity
inline constexpr int kMinMidiVelocity = 0;

/// Maximum valid MIDI velocity
inline constexpr int kMaxMidiVelocity = 127;

// ==============================================================================
// Functions
// ==============================================================================

/// Convert MIDI note number to frequency using 12-TET tuning.
///
/// Uses the standard 12-tone equal temperament formula:
///   frequency = a4Frequency * 2^((midiNote - 69) / 12)
///
/// @param midiNote MIDI note number (0-127, where 69 = A4)
/// @param a4Frequency Reference frequency for A4 (default 440 Hz)
/// @return Frequency in Hz
///
/// @note Real-time safe: no allocation, no exceptions
/// @note Constexpr: usable at compile time (C++20)
///
/// @example midiNoteToFrequency(69)       -> 440.0 Hz  (A4)
/// @example midiNoteToFrequency(60)       -> 261.63 Hz (C4, middle C)
/// @example midiNoteToFrequency(72)       -> 523.25 Hz (C5)
/// @example midiNoteToFrequency(69, 432)  -> 432.0 Hz  (A4 at alternate tuning)
///
[[nodiscard]] constexpr float midiNoteToFrequency(
    int midiNote,
    float a4Frequency = kA4FrequencyHz
) noexcept {
    // Formula: f = a4 * 2^((note - 69) / 12)
    // Rewrite: f = a4 * exp(ln(2) * (note - 69) / 12)
    constexpr float kLn2Over12 = 0.0577622650f;  // ln(2) / 12
    const float exponent = static_cast<float>(midiNote - kA4MidiNote) * kLn2Over12;
    return a4Frequency * detail::constexprExp(exponent);
}

/// Convert MIDI velocity to linear gain.
///
/// Uses a linear mapping where:
/// - velocity 127 = 1.0 (full level, 0 dB)
/// - velocity 64  = ~0.504 (approximately -6 dB)
/// - velocity 0   = 0.0 (silence)
///
/// This simple linear curve is commonly used and matches FR-007:
/// "velocity 127 = full level, velocity 64 = approximately -6 dB"
///
/// @param velocity MIDI velocity (0-127)
/// @return Linear gain multiplier (0.0 to 1.0)
///
/// @note Real-time safe: no allocation, no exceptions
/// @note Constexpr: usable at compile time (C++20)
/// @note Velocity is clamped to [0, 127]
///
/// @example velocityToGain(127) -> 1.0   (0 dB)
/// @example velocityToGain(64)  -> 0.504 (-5.95 dB, within 0.1 dB of -6 dB)
/// @example velocityToGain(0)   -> 0.0   (silence)
/// @example velocityToGain(1)   -> 0.008 (minimum non-zero gain)
///
[[nodiscard]] constexpr float velocityToGain(int velocity) noexcept {
    // Clamp velocity to valid range
    const int clampedVelocity = (velocity < kMinMidiVelocity) ? kMinMidiVelocity
                              : (velocity > kMaxMidiVelocity) ? kMaxMidiVelocity
                              : velocity;
    return static_cast<float>(clampedVelocity) / static_cast<float>(kMaxMidiVelocity);
}

// ==============================================================================
// Velocity Curve Types
// ==============================================================================

/// Velocity-to-gain mapping curve types for MIDI velocity processing.
///
/// These curves determine how MIDI velocity (0-127) is mapped to a
/// normalized gain value (0.0-1.0), shaping the dynamic response.
///
/// @note Velocity 0 always produces 0.0 regardless of curve type.
enum class VelocityCurve : uint8_t {
    Linear = 0,   ///< output = velocity / 127.0
    Soft   = 1,   ///< output = sqrt(velocity / 127.0) -- concave, easier dynamics
    Hard   = 2,   ///< output = (velocity / 127.0)^2   -- convex, emphasizes forte
    Fixed  = 3    ///< output = 1.0 for any velocity > 0
};

/// Map MIDI velocity through the specified curve type.
///
/// Applies the selected velocity curve to produce a normalized gain value.
/// All curves return 0.0 for velocity 0 (FR-015).
/// Velocity is clamped to [0, 127] (FR-016).
///
/// @param velocity MIDI velocity (clamped to [0, 127])
/// @param curve Velocity curve type
/// @return Normalized gain [0.0, 1.0]
///
/// @note Real-time safe: no allocation, no exceptions
///
/// @example mapVelocity(127, VelocityCurve::Linear) -> 1.0
/// @example mapVelocity(64, VelocityCurve::Soft)    -> ~0.710
/// @example mapVelocity(64, VelocityCurve::Hard)    -> ~0.254
/// @example mapVelocity(1, VelocityCurve::Fixed)    -> 1.0
///
[[nodiscard]] inline float mapVelocity(int velocity, VelocityCurve curve) noexcept {
    // Clamp velocity to valid range (FR-016)
    const int clamped = (velocity < kMinMidiVelocity) ? kMinMidiVelocity
                      : (velocity > kMaxMidiVelocity) ? kMaxMidiVelocity
                      : velocity;

    // Velocity 0 always returns 0.0 regardless of curve (FR-015)
    if (clamped == 0) {
        return 0.0f;
    }

    const float normalized = static_cast<float>(clamped) / static_cast<float>(kMaxMidiVelocity);

    switch (curve) {
        case VelocityCurve::Linear:
            return normalized;  // FR-011
        case VelocityCurve::Soft:
            return std::sqrt(normalized);  // FR-012
        case VelocityCurve::Hard:
            return normalized * normalized;  // FR-013
        case VelocityCurve::Fixed:
            return 1.0f;  // FR-014
        default:
            return normalized;  // Fallback to linear
    }
}

}  // namespace DSP
}  // namespace Krate
