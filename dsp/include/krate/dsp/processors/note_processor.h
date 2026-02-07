// ==============================================================================
// Layer 2: DSP Processor - Note Event Processor
// ==============================================================================
// MIDI note processing with pitch bend smoothing and velocity curve mapping.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (C++20, RAII, value semantics)
// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1)
// - Principle XII: Test-First Development
//
// Reference: specs/036-note-event-processor/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/midi_utils.h>    // midiNoteToFrequency, VelocityCurve, mapVelocity
#include <krate/dsp/core/pitch_utils.h>   // semitonesToRatio
#include <krate/dsp/core/db_utils.h>      // detail::isNaN, detail::isInf
#include <krate/dsp/primitives/smoother.h> // OnePoleSmoother

#include <algorithm>  // std::clamp
#include <cstdint>

namespace Krate {
namespace DSP {

/// Pre-computed velocity values for multiple modulation destinations.
/// Each field contains the velocity-curved value scaled by its destination depth.
struct VelocityOutput {
    float amplitude    = 0.0f;  ///< Velocity scaled for amplitude destination (FR-017)
    float filter       = 0.0f;  ///< Velocity scaled for filter cutoff destination (FR-017)
    float envelopeTime = 0.0f;  ///< Velocity scaled for envelope time destination (FR-017)
};

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
class NoteProcessor {
public:
    // =====================================================================
    // Construction
    // =====================================================================

    /// Default constructor. A4=440Hz, bend range=2 semitones, smoothing=5ms.
    NoteProcessor() noexcept
        : a4Reference_(kA4FrequencyHz)
        , pitchBendRange_(2.0f)
        , smoothingTimeMs_(5.0f)
        , currentBendSemitones_(0.0f)
        , currentBendRatio_(1.0f)
        , velocityCurve_(VelocityCurve::Linear)
        , ampVelocityDepth_(1.0f)
        , filterVelocityDepth_(0.0f)
        , envTimeVelocityDepth_(0.0f)
        , sampleRate_(44100.0f)
    {
        bendSmoother_.configure(smoothingTimeMs_, sampleRate_);
    }

    // =====================================================================
    // Initialization (FR-003)
    // =====================================================================

    /// Configure for given sample rate.
    /// Preserves current smoothed bend value if mid-transition.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);
        // setSampleRate preserves current & target, recalculates coefficient only
        bendSmoother_.setSampleRate(sampleRate_);
    }

    /// Reset all state: snap bend smoother to 0, clear cached values.
    /// @post getFrequency(69) == a4Reference_, currentBendRatio == 1.0
    void reset() noexcept {
        bendSmoother_.snapTo(0.0f);
        currentBendSemitones_ = 0.0f;
        currentBendRatio_ = 1.0f;
    }

    // =====================================================================
    // Pitch Bend (FR-004 through FR-009)
    // =====================================================================

    /// Set pitch bend target (bipolar input from MIDI controller).
    /// NaN/Inf inputs are silently ignored (FR-020).
    /// @param bipolar Pitch bend value [-1.0, +1.0]
    void setPitchBend(float bipolar) noexcept {
        // FR-020: Guard NaN/Inf BEFORE calling setTarget to preserve smoother state
        if (detail::isNaN(bipolar) || detail::isInf(bipolar)) {
            return;  // Ignore invalid input
        }
        bendSmoother_.setTarget(bipolar);
    }

    /// Advance the pitch bend smoother by one sample.
    /// Call once per audio block (shared state for all voices).
    /// Updates internal cached bend ratio.
    /// @return Current smoothed pitch bend (bipolar, before range scaling)
    [[nodiscard]] float processPitchBend() noexcept {
        float smoothedBend = bendSmoother_.process();
        currentBendSemitones_ = smoothedBend * pitchBendRange_;
        currentBendRatio_ = semitonesToRatio(currentBendSemitones_);
        return smoothedBend;
    }

    // =====================================================================
    // Pitch Bend Configuration (FR-005, FR-007)
    // =====================================================================

    /// Set pitch bend range in semitones.
    /// @param semitones Range [0, 24]. Default: 2.
    void setPitchBendRange(float semitones) noexcept {
        pitchBendRange_ = std::clamp(semitones, 0.0f, 24.0f);
    }

    /// Set pitch bend smoothing time.
    /// @param ms Smoothing time in milliseconds. 0 = instant. Default: 5.
    void setSmoothingTime(float ms) noexcept {
        smoothingTimeMs_ = ms;
        bendSmoother_.configure(ms, sampleRate_);
    }

    // =====================================================================
    // Tuning (FR-002)
    // =====================================================================

    /// Set A4 tuning reference frequency.
    /// Finite values clamped to [400, 480] Hz.
    /// NaN/Inf reset to 440 Hz (ISO standard default).
    /// @param hz A4 reference frequency
    void setTuningReference(float hz) noexcept {
        if (detail::isNaN(hz) || detail::isInf(hz)) {
            a4Reference_ = kA4FrequencyHz;  // Reset to 440 Hz
            return;
        }
        a4Reference_ = std::clamp(hz, 400.0f, 480.0f);
    }

    /// Get current A4 tuning reference.
    /// @return A4 reference in Hz
    [[nodiscard]] float getTuningReference() const noexcept {
        return a4Reference_;
    }

    // =====================================================================
    // Frequency (FR-001, FR-009)
    // =====================================================================

    /// Get frequency for a MIDI note with current pitch bend and tuning.
    /// @param note MIDI note number (0-127)
    /// @return Frequency in Hz (always positive and finite)
    [[nodiscard]] float getFrequency(uint8_t note) const noexcept {
        float baseFreq = midiNoteToFrequency(static_cast<int>(note), a4Reference_);
        return baseFreq * currentBendRatio_;
    }

    // =====================================================================
    // Velocity (FR-010 through FR-018)
    // =====================================================================

    /// Set the velocity curve type.
    /// @param curve One of: Linear, Soft, Hard, Fixed
    void setVelocityCurve(VelocityCurve curve) noexcept {
        velocityCurve_ = curve;
    }

    /// Map a MIDI velocity to multi-destination output.
    /// Applies the current curve and depth settings.
    /// @param velocity MIDI velocity (0-127, clamped)
    /// @return VelocityOutput with per-destination scaled values
    [[nodiscard]] VelocityOutput mapVelocity(int velocity) const noexcept {
        // Delegate to Layer 0 mapVelocity for curve application
        float curvedVel = Krate::DSP::mapVelocity(velocity, velocityCurve_);

        VelocityOutput out;
        out.amplitude    = curvedVel * ampVelocityDepth_;
        out.filter       = curvedVel * filterVelocityDepth_;
        out.envelopeTime = curvedVel * envTimeVelocityDepth_;
        return out;
    }

    /// Set velocity depth for amplitude destination.
    /// @param depth [0.0, 1.0]. Default: 1.0
    void setAmplitudeVelocityDepth(float depth) noexcept {
        ampVelocityDepth_ = std::clamp(depth, 0.0f, 1.0f);
    }

    /// Set velocity depth for filter destination.
    /// @param depth [0.0, 1.0]. Default: 0.0
    void setFilterVelocityDepth(float depth) noexcept {
        filterVelocityDepth_ = std::clamp(depth, 0.0f, 1.0f);
    }

    /// Set velocity depth for envelope time destination.
    /// @param depth [0.0, 1.0]. Default: 0.0
    void setEnvelopeTimeVelocityDepth(float depth) noexcept {
        envTimeVelocityDepth_ = std::clamp(depth, 0.0f, 1.0f);
    }

private:
    OnePoleSmoother bendSmoother_;       ///< Smooths bipolar pitch bend input
    float a4Reference_;                  ///< A4 tuning reference in Hz [400, 480]
    float pitchBendRange_;               ///< Pitch bend range in semitones [0, 24]
    float smoothingTimeMs_;              ///< Pitch bend smoothing time in ms
    float currentBendSemitones_;         ///< Cached: smoothedBend * pitchBendRange_
    float currentBendRatio_;             ///< Cached: semitonesToRatio(currentBendSemitones_)
    VelocityCurve velocityCurve_;        ///< Active velocity curve type
    float ampVelocityDepth_;             ///< Amplitude velocity depth [0, 1]
    float filterVelocityDepth_;          ///< Filter velocity depth [0, 1]
    float envTimeVelocityDepth_;         ///< Envelope time velocity depth [0, 1]
    float sampleRate_;                   ///< Current sample rate in Hz
};

}  // namespace DSP
}  // namespace Krate
