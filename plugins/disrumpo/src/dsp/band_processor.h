// ==============================================================================
// Band Processor for Per-Band Gain/Pan/Mute Processing
// ==============================================================================
// Per-band gain/pan/mute processor with smoothing.
// Real-time safe: no allocations in process().
//
// References:
// - specs/002-band-management/contracts/band_processor_api.md
// - specs/002-band-management/spec.md FR-019 to FR-027
// - Constitution Principle XIV: Reuse Krate::DSP::OnePoleSmoother
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/smoother.h>
#include "band_state.h"

#include <cmath>
#include <algorithm>

namespace Disrumpo {

/// @brief Per-band gain/pan/mute processor with smoothing.
/// Real-time safe: no allocations in process().
class BandProcessor {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kPi = 3.14159265358979323846f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    BandProcessor() noexcept = default;
    ~BandProcessor() noexcept = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    /// @brief Initialize for given sample rate.
    /// @param sampleRate Sample rate in Hz
    void prepare(double sampleRate) noexcept {
        // Configure smoothers with default 10ms smoothing time
        const float sr = static_cast<float>(sampleRate);
        gainSmoother_.configure(kDefaultSmoothingMs, sr);
        panSmoother_.configure(kDefaultSmoothingMs, sr);
        muteSmoother_.configure(kDefaultSmoothingMs, sr);

        // Initialize to defaults: 0dB gain (1.0 linear), center pan, unmuted
        targetGainLinear_ = 1.0f;
        targetPan_ = 0.0f;
        targetMute_ = 0.0f; // 0 = unmuted

        gainSmoother_.snapTo(targetGainLinear_);
        panSmoother_.snapTo(targetPan_);
        muteSmoother_.snapTo(targetMute_);
    }

    /// @brief Reset all smoother states.
    void reset() noexcept {
        gainSmoother_.reset();
        panSmoother_.reset();
        muteSmoother_.reset();

        // Re-snap to current targets
        gainSmoother_.snapTo(targetGainLinear_);
        panSmoother_.snapTo(targetPan_);
        muteSmoother_.snapTo(targetMute_);
    }

    // =========================================================================
    // Parameter Setters (Thread-Safe)
    // =========================================================================

    /// @brief Set band gain in dB.
    /// FR-019: Each band MUST apply gain scaling based on BandState::gainDb
    /// FR-020: Gain MUST be converted from dB to linear
    /// @param db Gain in dB, clamped to [-24, +24]
    void setGainDb(float db) noexcept {
        // Clamp to valid range
        const float clampedDb = std::clamp(db, kMinBandGainDb, kMaxBandGainDb);

        // Convert dB to linear: gain = 10^(dB/20)
        targetGainLinear_ = std::pow(10.0f, clampedDb / 20.0f);
        gainSmoother_.setTarget(targetGainLinear_);
    }

    /// @brief Set pan position [-1, +1].
    /// FR-021: Range -1.0 to +1.0, where -1.0 = full left, +1.0 = full right
    /// @param pan Pan position, clamped to [-1, +1]
    void setPan(float pan) noexcept {
        targetPan_ = std::clamp(pan, -1.0f, 1.0f);
        panSmoother_.setTarget(targetPan_);
    }

    /// @brief Set mute state.
    /// FR-023: When muted, band output MUST be zero
    /// @param muted true to mute, false to unmute
    void setMute(bool muted) noexcept {
        targetMute_ = muted ? 1.0f : 0.0f;
        muteSmoother_.setTarget(targetMute_);
    }

    // =========================================================================
    // Processing (FR-022)
    // =========================================================================

    /// @brief Process stereo sample pair in-place.
    /// Applies gain, pan, and mute with smoothing.
    /// FR-022: Equal-power pan law
    /// @param left Left channel sample (modified in-place)
    /// @param right Right channel sample (modified in-place)
    void process(float& left, float& right) noexcept {
        // Get smoothed values
        const float gain = gainSmoother_.process();
        const float pan = panSmoother_.process();
        const float mute = muteSmoother_.process();

        // FR-022: Equal-power pan law
        // leftGain = cos(pan * PI/4 + PI/4)
        // rightGain = sin(pan * PI/4 + PI/4)
        const float panAngle = pan * kPi / 4.0f + kPi / 4.0f;
        const float leftCoeff = std::cos(panAngle);
        const float rightCoeff = std::sin(panAngle);

        // Calculate mute multiplier (0 = muted, 1 = unmuted)
        const float muteMultiplier = 1.0f - mute;

        // Apply all in one pass
        left = left * gain * leftCoeff * muteMultiplier;
        right = right * gain * rightCoeff * muteMultiplier;
    }

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Check if all smoothers have settled.
    /// @return true if any smoother is still transitioning
    [[nodiscard]] bool isSmoothing() const noexcept {
        return !gainSmoother_.isComplete() ||
               !panSmoother_.isComplete() ||
               !muteSmoother_.isComplete();
    }

private:
    // =========================================================================
    // Members
    // =========================================================================

    Krate::DSP::OnePoleSmoother gainSmoother_;
    Krate::DSP::OnePoleSmoother panSmoother_;
    Krate::DSP::OnePoleSmoother muteSmoother_;

    // Target values (for reference)
    float targetGainLinear_ = 1.0f;
    float targetPan_ = 0.0f;
    float targetMute_ = 0.0f;
};

} // namespace Disrumpo
