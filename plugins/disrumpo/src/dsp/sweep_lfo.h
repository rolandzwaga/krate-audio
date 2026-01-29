// ==============================================================================
// SweepLFO - LFO Wrapper for Sweep Frequency Modulation
// ==============================================================================
// Wraps Krate::DSP::LFO with sweep-specific range mapping and parameters.
// Provides frequency modulation output for sweep center frequency.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, composition over inheritance)
// - Principle IX: Layer 3 (composes Layer 1 LFO primitive)
// - Principle XII: Test-First Development
//
// Reference: specs/007-sweep-system/spec.md (FR-024, FR-025, SC-015)
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/core/note_value.h>

#include <algorithm>
#include <cmath>

namespace Disrumpo {

// =============================================================================
// Constants
// =============================================================================

/// @brief Minimum sweep LFO rate in Hz (FR-024)
constexpr float kMinSweepLFORate = 0.01f;

/// @brief Maximum sweep LFO rate in Hz (FR-024)
constexpr float kMaxSweepLFORate = 20.0f;

/// @brief Default sweep LFO rate in Hz
constexpr float kDefaultSweepLFORate = 1.0f;

// =============================================================================
// SweepLFO Class
// =============================================================================

/// @brief LFO wrapper for sweep frequency modulation.
///
/// Wraps Krate::DSP::LFO with sweep-specific features:
/// - Rate range 0.01Hz - 20Hz (free mode) or tempo-synced
/// - All standard waveforms (Sine, Triangle, Saw, Square, S&H, Smooth Random)
/// - Depth control for modulation amount
/// - Output maps to sweep frequency range (20Hz - 20kHz)
///
/// Thread Safety:
/// - prepare()/reset(): Call from non-audio thread only
/// - Parameter setters: Thread-safe via underlying LFO
/// - process(): Audio thread only
///
/// @note Real-time safe: no allocations after prepare()
class SweepLFO {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    SweepLFO() noexcept = default;
    ~SweepLFO() = default;

    // Non-copyable (LFO is non-copyable)
    SweepLFO(const SweepLFO&) = delete;
    SweepLFO& operator=(const SweepLFO&) = delete;

    // Movable
    SweepLFO(SweepLFO&&) noexcept = default;
    SweepLFO& operator=(SweepLFO&&) noexcept = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    /// @brief Prepare the LFO for processing.
    /// @param sampleRate Sample rate in Hz
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        lfo_.prepare(sampleRate);
        lfo_.setFrequency(rate_);
        prepared_ = true;
    }

    /// @brief Reset the LFO to initial state.
    void reset() noexcept {
        lfo_.reset();
    }

    // =========================================================================
    // Parameter Setters
    // =========================================================================

    /// @brief Enable or disable the sweep LFO.
    /// @param enabled true to enable modulation
    void setEnabled(bool enabled) noexcept {
        enabled_ = enabled;
    }

    /// @brief Set LFO rate in Hz (free mode).
    ///
    /// Per FR-024: Range 0.01Hz - 20Hz.
    ///
    /// @param hz Rate in Hz, clamped to valid range
    void setRate(float hz) noexcept {
        rate_ = std::clamp(hz, kMinSweepLFORate, kMaxSweepLFORate);
        if (!tempoSync_) {
            lfo_.setFrequency(rate_);
        }
    }

    /// @brief Set LFO waveform shape.
    ///
    /// Per FR-025: Sine, Triangle, Saw, Square, Sample & Hold, Smooth Random.
    ///
    /// @param waveform Waveform type
    void setWaveform(Krate::DSP::Waveform waveform) noexcept {
        lfo_.setWaveform(waveform);
    }

    /// @brief Set modulation depth.
    /// @param depth Depth [0, 1] where 1 = full range modulation
    void setDepth(float depth) noexcept {
        depth_ = std::clamp(depth, 0.0f, 1.0f);
    }

    /// @brief Enable or disable tempo synchronization.
    /// @param enabled true for tempo sync mode
    void setTempoSync(bool enabled) noexcept {
        tempoSync_ = enabled;
        lfo_.setTempoSync(enabled);
        if (!enabled) {
            lfo_.setFrequency(rate_);
        }
    }

    /// @brief Set host tempo for tempo sync mode.
    /// @param bpm Tempo in BPM
    void setTempo(float bpm) noexcept {
        lfo_.setTempo(bpm);
    }

    /// @brief Set note value for tempo sync mode.
    /// @param value Note value (whole, half, quarter, etc.)
    /// @param modifier Note modifier (none, dotted, triplet)
    void setNoteValue(Krate::DSP::NoteValue value,
                      Krate::DSP::NoteModifier modifier = Krate::DSP::NoteModifier::None) noexcept {
        lfo_.setNoteValue(value, modifier);
    }

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Check if LFO is enabled.
    [[nodiscard]] bool isEnabled() const noexcept {
        return enabled_;
    }

    /// @brief Get current rate in Hz.
    [[nodiscard]] float getRate() const noexcept {
        return rate_;
    }

    /// @brief Get current depth.
    [[nodiscard]] float getDepth() const noexcept {
        return depth_;
    }

    /// @brief Check if tempo sync is enabled.
    [[nodiscard]] bool isTempoSynced() const noexcept {
        return tempoSync_;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process one sample of LFO output.
    /// @return LFO value [-1, 1] scaled by depth
    [[nodiscard]] float process() noexcept {
        if (!enabled_) {
            return 0.0f;
        }
        return lfo_.process() * depth_;
    }

    /// @brief Get modulated frequency from base frequency.
    ///
    /// Applies LFO modulation to base frequency in log space,
    /// clamped to sweep frequency range (20Hz - 20kHz).
    ///
    /// @param baseFreqHz Base sweep frequency in Hz
    /// @return Modulated frequency in Hz [20, 20000]
    [[nodiscard]] float getModulatedFrequency(float baseFreqHz) noexcept {
        if (!enabled_) {
            return baseFreqHz;
        }

        // Get current LFO value (already processed)
        float lfoValue = lfo_.process() * depth_;

        // Modulate in log2 space for musical frequency response
        // LFO value [-1, 1] maps to +/- 2 octaves
        constexpr float kMaxOctaveShift = 2.0f;
        float octaveShift = lfoValue * kMaxOctaveShift;

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

    Krate::DSP::LFO lfo_;
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
    bool enabled_ = false;
    bool tempoSync_ = false;
    float rate_ = kDefaultSweepLFORate;
    float depth_ = 0.5f;
};

} // namespace Disrumpo
