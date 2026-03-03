// ==============================================================================
// API Contract: NoteProcessor (Layer 2)
// ==============================================================================
// This file defines the public API contract for NoteProcessor.
// It is a design document, NOT compilable source code.
// The actual implementation will be in:
//   dsp/include/krate/dsp/processors/note_processor.h
//
// Dependencies:
//   Layer 0: <krate/dsp/core/midi_utils.h>   -- midiNoteToFrequency, VelocityCurve
//   Layer 0: <krate/dsp/core/pitch_utils.h>   -- semitonesToRatio
//   Layer 0: <krate/dsp/core/db_utils.h>      -- detail::isNaN, detail::isInf
//   Layer 1: <krate/dsp/primitives/smoother.h> -- OnePoleSmoother
// ==============================================================================

#pragma once

#include <cstdint>

namespace Krate::DSP {

// =========================================================================
// VelocityCurve (added to midi_utils.h, Layer 0)
// =========================================================================

/// Velocity-to-gain mapping curve types.
enum class VelocityCurve : uint8_t {
    Linear = 0,   ///< output = velocity / 127.0
    Soft   = 1,   ///< output = sqrt(velocity / 127.0)  -- concave
    Hard   = 2,   ///< output = (velocity / 127.0)^2    -- convex
    Fixed  = 3    ///< output = 1.0 for any velocity > 0
};

/// Map MIDI velocity through the specified curve.
/// @param velocity MIDI velocity (clamped to [0, 127])
/// @param curve Velocity curve type
/// @return Normalized gain [0.0, 1.0]. Always 0.0 for velocity 0.
[[nodiscard]] inline float mapVelocity(int velocity, VelocityCurve curve) noexcept;

// =========================================================================
// VelocityOutput (in note_processor.h, Layer 2)
// =========================================================================

/// Pre-computed velocity values for multiple modulation destinations.
struct VelocityOutput {
    float amplitude    = 0.0f;  ///< Velocity scaled for amplitude (FR-017)
    float filter       = 0.0f;  ///< Velocity scaled for filter cutoff (FR-017)
    float envelopeTime = 0.0f;  ///< Velocity scaled for envelope timing (FR-017)
};

// =========================================================================
// NoteProcessor (Layer 2)
// =========================================================================

/// MIDI note processing with pitch bend smoothing and velocity curve mapping.
///
/// Converts MIDI note numbers to frequencies with configurable A4 tuning,
/// applies smoothed pitch bend, and maps velocity through configurable curves
/// with multi-destination depth scaling.
///
/// Thread safety: Single audio thread only.
/// Real-time safety: All methods noexcept, zero allocations.
///
/// Usage pattern (polyphonic context):
///   1. prepare(sampleRate) -- once at init or sample rate change
///   2. setPitchBend(bipolar) -- when MIDI pitch bend received
///   3. processPitchBend() -- once per audio block (shared by all voices)
///   4. getFrequency(note) -- per voice per block
///   5. mapVelocity(velocity) -- per note-on event
///
class NoteProcessor {
public:
    // =====================================================================
    // Construction
    // =====================================================================

    /// Default constructor. A4=440Hz, bend range=2 semitones, smoothing=5ms.
    NoteProcessor() noexcept;

    // =====================================================================
    // Initialization (FR-003)
    // =====================================================================

    /// Configure for given sample rate.
    /// Preserves current smoothed bend value if mid-transition.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    void prepare(double sampleRate) noexcept;

    /// Reset all state: snap bend smoother to 0, clear cached values.
    /// @post getFrequency(69) == a4Reference_, currentBendRatio == 1.0
    void reset() noexcept;

    // =====================================================================
    // Pitch Bend (FR-004 through FR-009)
    // =====================================================================

    /// Set pitch bend target (bipolar input from MIDI controller).
    /// NaN/Inf inputs are silently ignored (FR-020).
    /// @param bipolar Pitch bend value [-1.0, +1.0]
    void setPitchBend(float bipolar) noexcept;

    /// Advance the pitch bend smoother by one sample.
    /// Call once per audio block (shared state for all voices).
    /// Updates internal cached bend ratio.
    /// @return Current smoothed pitch bend (bipolar, before range scaling)
    [[nodiscard]] float processPitchBend() noexcept;

    /// Get frequency for a MIDI note with current pitch bend and tuning.
    /// @param note MIDI note number (0-127)
    /// @return Frequency in Hz (always positive and finite)
    [[nodiscard]] float getFrequency(uint8_t note) const noexcept;

    // =====================================================================
    // Pitch Bend Configuration (FR-005, FR-007)
    // =====================================================================

    /// Set pitch bend range in semitones.
    /// @param semitones Range [0, 24]. Default: 2.
    void setPitchBendRange(float semitones) noexcept;

    /// Set pitch bend smoothing time.
    /// @param ms Smoothing time in milliseconds. 0 = instant. Default: 5.
    void setSmoothingTime(float ms) noexcept;

    // =====================================================================
    // Tuning (FR-002)
    // =====================================================================

    /// Set A4 tuning reference frequency.
    /// Finite values clamped to [400, 480] Hz.
    /// NaN/Inf reset to 440 Hz (ISO standard default).
    /// @param hz A4 reference frequency
    void setTuningReference(float hz) noexcept;

    /// Get current A4 tuning reference.
    /// @return A4 reference in Hz
    [[nodiscard]] float getTuningReference() const noexcept;

    // =====================================================================
    // Velocity (FR-010 through FR-018)
    // =====================================================================

    /// Set the velocity curve type.
    /// @param curve One of: Linear, Soft, Hard, Fixed
    void setVelocityCurve(VelocityCurve curve) noexcept;

    /// Map a MIDI velocity to multi-destination output.
    /// Applies the current curve and depth settings.
    /// @param velocity MIDI velocity (0-127, clamped)
    /// @return VelocityOutput with per-destination scaled values
    [[nodiscard]] VelocityOutput mapVelocity(int velocity) const noexcept;

    /// Set velocity depth for amplitude destination.
    /// @param depth [0.0, 1.0]. Default: 1.0
    void setAmplitudeVelocityDepth(float depth) noexcept;

    /// Set velocity depth for filter destination.
    /// @param depth [0.0, 1.0]. Default: 0.0
    void setFilterVelocityDepth(float depth) noexcept;

    /// Set velocity depth for envelope time destination.
    /// @param depth [0.0, 1.0]. Default: 0.0
    void setEnvelopeTimeVelocityDepth(float depth) noexcept;
};

}  // namespace Krate::DSP
