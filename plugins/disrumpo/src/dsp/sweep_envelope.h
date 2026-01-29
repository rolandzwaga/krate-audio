// ==============================================================================
// SweepEnvelope - Envelope Follower for Sweep Frequency Modulation
// ==============================================================================
// Wraps Krate::DSP::EnvelopeFollower with sweep-specific parameters and output.
// Provides input-level-driven frequency modulation for sweep center frequency.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, composition over inheritance)
// - Principle IX: Layer 3 (composes Layer 2 EnvelopeFollower processor)
// - Principle XII: Test-First Development
//
// Reference: specs/007-sweep-system/spec.md (FR-026, FR-027, SC-016)
// ==============================================================================

#pragma once

#include <krate/dsp/processors/envelope_follower.h>

#include <algorithm>
#include <cmath>

namespace Disrumpo {

// =============================================================================
// Constants
// =============================================================================

/// @brief Minimum sweep envelope attack time in ms (FR-027)
constexpr float kMinSweepEnvAttackMs = 1.0f;

/// @brief Maximum sweep envelope attack time in ms (FR-027)
constexpr float kMaxSweepEnvAttackMs = 100.0f;

/// @brief Minimum sweep envelope release time in ms (FR-027)
constexpr float kMinSweepEnvReleaseMs = 10.0f;

/// @brief Maximum sweep envelope release time in ms (FR-027)
constexpr float kMaxSweepEnvReleaseMs = 500.0f;

/// @brief Default sweep envelope attack time in ms
constexpr float kDefaultSweepEnvAttackMs = 10.0f;

/// @brief Default sweep envelope release time in ms
constexpr float kDefaultSweepEnvReleaseMs = 100.0f;

// =============================================================================
// SweepEnvelope Class
// =============================================================================

/// @brief Envelope follower wrapper for sweep frequency modulation.
///
/// Wraps Krate::DSP::EnvelopeFollower with sweep-specific features:
/// - Attack time 1-100ms
/// - Release time 10-500ms
/// - Sensitivity 0-100% for modulation amount
/// - Output maps to sweep frequency range (20Hz - 20kHz)
///
/// Thread Safety:
/// - prepare()/reset(): Call from non-audio thread only
/// - Parameter setters: Thread-safe
/// - processSample(): Audio thread only
///
/// @note Real-time safe: no allocations after prepare()
class SweepEnvelope {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    SweepEnvelope() noexcept = default;
    ~SweepEnvelope() = default;

    // Non-copyable, movable
    SweepEnvelope(const SweepEnvelope&) = delete;
    SweepEnvelope& operator=(const SweepEnvelope&) = delete;
    SweepEnvelope(SweepEnvelope&&) noexcept = default;
    SweepEnvelope& operator=(SweepEnvelope&&) noexcept = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    /// @brief Prepare the envelope follower for processing.
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum block size
    void prepare(double sampleRate, size_t maxBlockSize = 512) noexcept {
        sampleRate_ = sampleRate;
        envelope_.prepare(sampleRate, maxBlockSize);
        envelope_.setMode(Krate::DSP::DetectionMode::RMS);
        envelope_.setAttackTime(attackMs_);
        envelope_.setReleaseTime(releaseMs_);
        prepared_ = true;
    }

    /// @brief Reset the envelope follower to initial state.
    void reset() noexcept {
        envelope_.reset();
        envelopeLevel_ = 0.0f;
    }

    // =========================================================================
    // Parameter Setters
    // =========================================================================

    /// @brief Enable or disable the envelope follower.
    /// @param enabled true to enable modulation
    void setEnabled(bool enabled) noexcept {
        enabled_ = enabled;
    }

    /// @brief Set attack time.
    ///
    /// Per FR-027: Range 1-100ms.
    ///
    /// @param ms Attack time in milliseconds
    void setAttackTime(float ms) noexcept {
        attackMs_ = std::clamp(ms, kMinSweepEnvAttackMs, kMaxSweepEnvAttackMs);
        if (prepared_) {
            envelope_.setAttackTime(attackMs_);
        }
    }

    /// @brief Set release time.
    ///
    /// Per FR-027: Range 10-500ms.
    ///
    /// @param ms Release time in milliseconds
    void setReleaseTime(float ms) noexcept {
        releaseMs_ = std::clamp(ms, kMinSweepEnvReleaseMs, kMaxSweepEnvReleaseMs);
        if (prepared_) {
            envelope_.setReleaseTime(releaseMs_);
        }
    }

    /// @brief Set sensitivity.
    ///
    /// Per FR-027: Range 0-100% (0-1).
    /// Controls how much the envelope affects the sweep frequency.
    ///
    /// @param sensitivity Sensitivity [0, 1]
    void setSensitivity(float sensitivity) noexcept {
        sensitivity_ = std::clamp(sensitivity, 0.0f, 1.0f);
    }

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Check if envelope follower is enabled.
    [[nodiscard]] bool isEnabled() const noexcept {
        return enabled_;
    }

    /// @brief Get attack time in ms.
    [[nodiscard]] float getAttackTime() const noexcept {
        return attackMs_;
    }

    /// @brief Get release time in ms.
    [[nodiscard]] float getReleaseTime() const noexcept {
        return releaseMs_;
    }

    /// @brief Get sensitivity.
    [[nodiscard]] float getSensitivity() const noexcept {
        return sensitivity_;
    }

    /// @brief Get current envelope level [0, 1].
    [[nodiscard]] float getEnvelopeLevel() const noexcept {
        return envelopeLevel_;
    }

    /// @brief Get current modulation amount (envelope * sensitivity).
    [[nodiscard]] float getModulationAmount() const noexcept {
        return envelopeLevel_ * sensitivity_;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process one sample of input and update envelope.
    /// @param input Input sample
    /// @return Current envelope level [0, 1]
    [[nodiscard]] float processSample(float input) noexcept {
        if (!enabled_) {
            return 0.0f;
        }

        envelopeLevel_ = envelope_.processSample(input);
        return envelopeLevel_;
    }

    /// @brief Get modulated frequency from base frequency.
    ///
    /// Applies envelope modulation to base frequency in log space,
    /// clamped to sweep frequency range (20Hz - 20kHz).
    /// Higher envelope = higher frequency.
    ///
    /// @param baseFreqHz Base sweep frequency in Hz
    /// @return Modulated frequency in Hz [20, 20000]
    [[nodiscard]] float getModulatedFrequency(float baseFreqHz) const noexcept {
        if (!enabled_) {
            return baseFreqHz;
        }

        // Get modulation amount (envelope * sensitivity)
        float modAmount = envelopeLevel_ * sensitivity_;

        // Modulate in log2 space for musical frequency response
        // Envelope [0, 1] maps to 0 to +2 octaves (upward only)
        constexpr float kMaxOctaveShift = 2.0f;
        float octaveShift = modAmount * kMaxOctaveShift;

        // Apply octave shift
        float log2Freq = std::log2(baseFreqHz) + octaveShift;

        // Clamp to valid frequency range
        constexpr float kLog2Min = 4.321928f;   // log2(20)
        constexpr float kLog2Max = 14.287712f;  // log2(20000)
        log2Freq = std::clamp(log2Freq, kLog2Min, kLog2Max);

        return std::pow(2.0f, log2Freq);
    }

private:
    // =========================================================================
    // State
    // =========================================================================

    Krate::DSP::EnvelopeFollower envelope_;
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
    bool enabled_ = false;
    float attackMs_ = kDefaultSweepEnvAttackMs;
    float releaseMs_ = kDefaultSweepEnvReleaseMs;
    float sensitivity_ = 0.5f;
    float envelopeLevel_ = 0.0f;
};

} // namespace Disrumpo
