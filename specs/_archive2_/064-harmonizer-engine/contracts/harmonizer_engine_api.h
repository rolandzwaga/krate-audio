// Contract: HarmonizerEngine API (064-harmonizer-engine)
// This file documents the exact public API contract for implementation.
// It is NOT compiled -- it serves as the binding specification.

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/scale_harmonizer.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/pitch_tracker.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/pitch_shift_processor.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Krate::DSP {

/// @brief Harmony intelligence mode selector.
enum class HarmonyMode : uint8_t {
    Chromatic = 0,  ///< Fixed semitone shift, no pitch tracking or scale awareness
    Scalic = 1,     ///< Diatonic interval in a configured key/scale, with pitch tracking
};

/// @brief Multi-voice harmonizer engine (Layer 3 - Systems).
///
/// Orchestrates shared pitch analysis, per-voice pitch shifting, level/pan
/// mixing, and mono-to-stereo constant-power panning. Composes existing
/// Layer 0-2 components without introducing new DSP algorithms.
///
/// Signal flow: mono input -> [PitchTracker] -> per-voice [DelayLine ->
/// PitchShiftProcessor -> Level/Pan] -> stereo sum -> dry/wet mix -> stereo output.
///
/// @par Real-Time Safety
/// All processing methods are noexcept. Zero heap allocations after prepare().
/// No locks, no I/O, no exceptions in the process path.
///
/// @par Thread Safety
/// Parameter setters are safe to call between process() calls from the same thread.
/// No cross-thread safety is provided -- the host must serialize parameter changes
/// with processing.
class HarmonizerEngine {
public:
    // =========================================================================
    // Constants
    // =========================================================================
    static constexpr int kMaxVoices = 4;
    static constexpr float kMinLevelDb = -60.0f;       ///< At or below = mute
    static constexpr float kMaxLevelDb = 6.0f;
    static constexpr int kMinInterval = -24;
    static constexpr int kMaxInterval = 24;
    static constexpr float kMinPan = -1.0f;
    static constexpr float kMaxPan = 1.0f;
    static constexpr float kMaxDelayMs = 50.0f;
    static constexpr float kMinDetuneCents = -50.0f;
    static constexpr float kMaxDetuneCents = 50.0f;

    // Smoothing time constants (milliseconds)
    static constexpr float kPitchSmoothTimeMs = 10.0f;
    static constexpr float kLevelSmoothTimeMs = 5.0f;
    static constexpr float kPanSmoothTimeMs = 5.0f;
    static constexpr float kDryWetSmoothTimeMs = 10.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================
    HarmonizerEngine() noexcept = default;

    /// @brief Initialize all internal components and pre-allocate buffers.
    /// @param sampleRate Audio sample rate in Hz (e.g. 44100.0)
    /// @param maxBlockSize Maximum number of samples per process() call
    /// @post All 4 PitchShiftProcessors prepared, all 4 DelayLines prepared,
    ///       PitchTracker prepared, all smoothers configured, scratch buffers
    ///       allocated. isPrepared() returns true.
    /// @note This method allocates. Call from setup thread, not audio thread.
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;

    /// @brief Reset all processing state without changing configuration.
    /// @post All PitchShiftProcessors reset, all DelayLines reset, PitchTracker
    ///       reset, all smoothers reset, scratch buffers zeroed. Configuration
    ///       (mode, intervals, levels, pans, etc.) is preserved.
    void reset() noexcept;

    /// @brief Check whether prepare() has been called successfully.
    /// @return true if the engine is ready to process audio.
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Audio Processing
    // =========================================================================

    /// @brief Process one block of audio: mono input to stereo output.
    /// @param input Pointer to mono input samples (numSamples)
    /// @param outputL Pointer to left output channel (numSamples, zeroed + written)
    /// @param outputR Pointer to right output channel (numSamples, zeroed + written)
    /// @param numSamples Number of samples to process (must be <= maxBlockSize)
    /// @pre outputL and outputR must NOT alias input or each other.
    /// @post outputL and outputR contain the mixed dry + wet stereo signal.
    /// @note If isPrepared() is false, zero-fills outputL and outputR and returns.
    void process(const float* input, float* outputL, float* outputR,
                 std::size_t numSamples) noexcept;

    // =========================================================================
    // Global Configuration
    // =========================================================================

    /// @brief Set the harmony mode (Chromatic or Scalic).
    /// @param mode The harmony mode to use.
    /// @post Takes effect on the next process() call. Voice configuration is
    ///       preserved across mode changes.
    void setHarmonyMode(HarmonyMode mode) noexcept;

    /// @brief Set the number of active harmony voices.
    /// @param count Number of voices, clamped to [0, kMaxVoices].
    ///              0 = dry signal only, no voice processing or pitch tracking.
    void setNumVoices(int count) noexcept;

    /// @brief Get the current number of active harmony voices.
    /// @return Current active voice count in [0, kMaxVoices].
    [[nodiscard]] int getNumVoices() const noexcept;

    /// @brief Set the root note for Scalic mode.
    /// @param rootNote Root note as integer (0=C, 1=C#, ..., 11=B). Wrapped mod 12.
    void setKey(int rootNote) noexcept;

    /// @brief Set the scale type for Scalic mode.
    /// @param type Scale type from ScaleType enum.
    void setScale(ScaleType type) noexcept;

    /// @brief Set the pitch shifting algorithm for all voices.
    /// @param mode Pitch shift mode (Simple, Granular, PitchSync, PhaseVocoder).
    /// @post All 4 PitchShiftProcessors are reconfigured and reset.
    ///       Latency reporting updates.
    void setPitchShiftMode(PitchMode mode) noexcept;

    /// @brief Enable or disable formant preservation for all voices.
    /// @param enable true to enable, false to disable.
    /// @note Only effective in Granular and PhaseVocoder modes.
    void setFormantPreserve(bool enable) noexcept;

    /// @brief Set the dry signal level in decibels.
    /// @param dB Dry level in dB. Smoothed at 10ms time constant.
    void setDryLevel(float dB) noexcept;

    /// @brief Set the wet (harmony) signal level in decibels.
    /// @param dB Wet level in dB. Smoothed at 10ms time constant.
    ///       Applied as master fader over the summed harmony bus.
    void setWetLevel(float dB) noexcept;

    // =========================================================================
    // Per-Voice Configuration
    // =========================================================================

    /// @brief Set the interval for a specific voice.
    /// @param voiceIndex Voice index [0, kMaxVoices-1]. Out-of-range is ignored.
    /// @param diatonicSteps Diatonic steps (Scalic) or raw semitones (Chromatic).
    ///        Clamped to [kMinInterval, kMaxInterval].
    void setVoiceInterval(int voiceIndex, int diatonicSteps) noexcept;

    /// @brief Set the output level for a specific voice.
    /// @param voiceIndex Voice index [0, kMaxVoices-1]. Out-of-range is ignored.
    /// @param dB Level in decibels. Clamped to [kMinLevelDb, kMaxLevelDb].
    ///        Values at or below kMinLevelDb are treated as mute (gain = 0).
    void setVoiceLevel(int voiceIndex, float dB) noexcept;

    /// @brief Set the stereo pan position for a specific voice.
    /// @param voiceIndex Voice index [0, kMaxVoices-1]. Out-of-range is ignored.
    /// @param pan Pan position. Clamped to [kMinPan, kMaxPan].
    ///        -1.0 = hard left, 0.0 = center, +1.0 = hard right.
    void setVoicePan(int voiceIndex, float pan) noexcept;

    /// @brief Set the onset delay for a specific voice.
    /// @param voiceIndex Voice index [0, kMaxVoices-1]. Out-of-range is ignored.
    /// @param ms Delay in milliseconds. Clamped to [0, kMaxDelayMs].
    ///        0 = bypass delay line (no processing overhead).
    void setVoiceDelay(int voiceIndex, float ms) noexcept;

    /// @brief Set the micro-detuning for a specific voice.
    /// @param voiceIndex Voice index [0, kMaxVoices-1]. Out-of-range is ignored.
    /// @param cents Detuning in cents. Clamped to [kMinDetuneCents, kMaxDetuneCents].
    ///        Added on top of the computed interval before pitch shifting.
    void setVoiceDetune(int voiceIndex, float cents) noexcept;

    // =========================================================================
    // Query Methods (read-only, UI feedback)
    // =========================================================================

    /// @brief Get the smoothed detected frequency from the PitchTracker.
    /// @return Frequency in Hz. Returns 0 if no pitch detected or in Chromatic mode.
    [[nodiscard]] float getDetectedPitch() const noexcept;

    /// @brief Get the committed MIDI note from the PitchTracker.
    /// @return MIDI note number (0-127). Returns -1 if no note committed.
    [[nodiscard]] int getDetectedNote() const noexcept;

    /// @brief Get the raw confidence value from the PitchTracker.
    /// @return Confidence in [0.0, 1.0]. Higher = more reliable pitch estimate.
    [[nodiscard]] float getPitchConfidence() const noexcept;

    /// @brief Get the engine's processing latency in samples.
    /// @return Latency matching the underlying PitchShiftProcessor for the
    ///         configured mode. Returns 0 if not prepared.
    [[nodiscard]] std::size_t getLatencySamples() const noexcept;

private:
    // =========================================================================
    // Internal Voice Structure
    // =========================================================================
    struct Voice {
        PitchShiftProcessor pitchShifter;   // L2: per-voice pitch shifting
        DelayLine           delayLine;       // L1: per-voice onset delay
        OnePoleSmoother     levelSmoother;   // L1: smooths gain changes (5ms)
        OnePoleSmoother     panSmoother;     // L1: smooths pan changes (5ms)
        OnePoleSmoother     pitchSmoother;   // L1: smooths semitone shift changes (10ms)

        // Configuration (set by public API, read in process)
        int   interval     = 0;       // diatonic steps (Scalic) or raw semitones (Chromatic)
        float levelDb      = 0.0f;    // output level in dB [-60, +6]
        float pan          = 0.0f;    // stereo position [-1.0, +1.0]
        float delayMs      = 0.0f;    // onset delay [0, 50] ms
        float detuneCents  = 0.0f;    // micro-detuning [-50, +50] cents

        // Computed (derived from configuration + pitch tracking)
        float targetSemitones = 0.0f; // total semitone shift (interval + detune)
        float linearGain      = 1.0f; // dbToGain(levelDb), 0 if muted
        float delaySamples    = 0.0f; // delayMs * sampleRate / 1000
    };

    // =========================================================================
    // Members
    // =========================================================================

    // Shared analysis components
    PitchTracker     pitchTracker_;      // Shared, Scalic mode only
    ScaleHarmonizer  scaleHarmonizer_;   // Shared, Scalic mode only

    // Voices (always 4 allocated, only numActiveVoices_ used)
    std::array<Voice, kMaxVoices> voices_;

    // Global configuration
    HarmonyMode harmonyMode_    = HarmonyMode::Chromatic;
    int         numActiveVoices_ = 0;
    PitchMode   pitchShiftMode_ = PitchMode::Simple;
    bool        formantPreserve_ = false;

    // Global level smoothers (independent, FR-007)
    OnePoleSmoother dryLevelSmoother_;
    OnePoleSmoother wetLevelSmoother_;

    // Scratch buffers (pre-allocated in prepare())
    std::vector<float> delayScratch_;   // Delayed input per voice
    std::vector<float> voiceScratch_;   // Pitch-shifted voice output

    // State
    double      sampleRate_       = 44100.0;
    std::size_t maxBlockSize_     = 0;
    bool        prepared_         = false;
    int         lastDetectedNote_ = -1; // Last valid MIDI note from PitchTracker
};

} // namespace Krate::DSP
