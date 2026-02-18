// ==============================================================================
// Layer 3: System Component - Multi-Voice Harmonizer Engine
// ==============================================================================
// Orchestrates shared pitch analysis, per-voice pitch shifting, level/pan
// mixing, and mono-to-stereo constant-power panning. Composes existing
// Layer 0-2 components without introducing new DSP algorithms.
//
// Signal flow: mono input -> [PitchTracker] -> per-voice [DelayLine ->
// PitchShiftProcessor -> Level/Pan] -> stereo sum -> dry/wet mix -> stereo output.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (constexpr, std::array, C++20)
// - Principle IX: Layer 3 (depends on Layer 0, 1, 2 only)
// - Principle XII: Test-First Development
//
// Reference: specs/064-harmonizer-engine/spec.md
// ==============================================================================

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

// =============================================================================
// HarmonyMode Enum (T009)
// =============================================================================

/// @brief Harmony intelligence mode selector.
enum class HarmonyMode : uint8_t {
    Chromatic = 0,  ///< Fixed semitone shift, no pitch tracking or scale awareness
    Scalic = 1,     ///< Diatonic interval in a configured key/scale, with pitch tracking
};

// =============================================================================
// HarmonizerEngine Class (T010-T014)
// =============================================================================

/// @brief Multi-voice harmonizer engine (Layer 3 - Systems).
///
/// Orchestrates shared pitch analysis, per-voice pitch shifting, level/pan
/// mixing, and mono-to-stereo constant-power panning. Composes existing
/// Layer 0-2 components without introducing new DSP algorithms.
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
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;

        const auto sampleRateF = static_cast<float>(sampleRate);

        // Prepare all 4 PitchShiftProcessors
        for (auto& voice : voices_) {
            voice.pitchShifter.prepare(sampleRate, maxBlockSize);
        }

        // Prepare all 4 DelayLines (50ms max)
        for (auto& voice : voices_) {
            voice.delayLine.prepare(sampleRate, kMaxDelayMs / 1000.0f);
        }

        // Prepare PitchTracker
        pitchTracker_.prepare(sampleRate);

        // Configure per-voice smoothers
        for (auto& voice : voices_) {
            voice.levelSmoother.configure(kLevelSmoothTimeMs, sampleRateF);
            voice.panSmoother.configure(kPanSmoothTimeMs, sampleRateF);
            voice.pitchSmoother.configure(kPitchSmoothTimeMs, sampleRateF);
        }

        // Configure global dry/wet smoothers (10ms)
        dryLevelSmoother_.configure(kDryWetSmoothTimeMs, sampleRateF);
        wetLevelSmoother_.configure(kDryWetSmoothTimeMs, sampleRateF);

        // Allocate scratch buffers
        delayScratch_.resize(maxBlockSize, 0.0f);
        voiceScratch_.resize(maxBlockSize, 0.0f);

        prepared_ = true;
    }

    /// @brief Reset all processing state without changing configuration.
    void reset() noexcept {
        // Reset all 4 PitchShiftProcessors
        for (auto& voice : voices_) {
            voice.pitchShifter.reset();
        }

        // Reset all 4 DelayLines
        for (auto& voice : voices_) {
            voice.delayLine.reset();
        }

        // Reset PitchTracker
        pitchTracker_.reset();

        // Reset all smoothers
        for (auto& voice : voices_) {
            voice.levelSmoother.reset();
            voice.panSmoother.reset();
            voice.pitchSmoother.reset();
        }
        dryLevelSmoother_.reset();
        wetLevelSmoother_.reset();

        // Zero scratch buffers
        std::fill(delayScratch_.begin(), delayScratch_.end(), 0.0f);
        std::fill(voiceScratch_.begin(), voiceScratch_.end(), 0.0f);

        // Reset pitch tracking state
        lastDetectedNote_ = -1;
    }

    /// @brief Check whether prepare() has been called successfully.
    /// @return true if the engine is ready to process audio.
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    // =========================================================================
    // Audio Processing
    // =========================================================================

    /// @brief Process one block of audio: mono input to stereo output.
    /// @param input Pointer to mono input samples (numSamples)
    /// @param outputL Pointer to left output channel (numSamples, zeroed + written)
    /// @param outputR Pointer to right output channel (numSamples, zeroed + written)
    /// @param numSamples Number of samples to process (must be <= maxBlockSize)
    void process(const float* input, float* outputL, float* outputR,
                 std::size_t numSamples) noexcept {
        // FR-015: Pre-condition guard -- if not prepared, zero-fill and return
        if (!prepared_) {
            std::fill(outputL, outputL + numSamples, 0.0f);
            std::fill(outputR, outputR + numSamples, 0.0f);
            return;
        }

        // Stub: further processing will be implemented in Phase 3
        std::fill(outputL, outputL + numSamples, 0.0f);
        std::fill(outputR, outputR + numSamples, 0.0f);
    }

    // =========================================================================
    // Global Configuration (stubs for Phase 3)
    // =========================================================================

    void setHarmonyMode([[maybe_unused]] HarmonyMode mode) noexcept {}
    void setNumVoices([[maybe_unused]] int count) noexcept {}
    [[nodiscard]] int getNumVoices() const noexcept { return numActiveVoices_; }
    void setKey([[maybe_unused]] int rootNote) noexcept {}
    void setScale([[maybe_unused]] ScaleType type) noexcept {}
    void setPitchShiftMode([[maybe_unused]] PitchMode mode) noexcept {}
    void setFormantPreserve([[maybe_unused]] bool enable) noexcept {}
    void setDryLevel([[maybe_unused]] float dB) noexcept {}
    void setWetLevel([[maybe_unused]] float dB) noexcept {}

    // =========================================================================
    // Per-Voice Configuration (stubs for Phase 3)
    // =========================================================================

    void setVoiceInterval([[maybe_unused]] int voiceIndex,
                          [[maybe_unused]] int diatonicSteps) noexcept {}
    void setVoiceLevel([[maybe_unused]] int voiceIndex,
                       [[maybe_unused]] float dB) noexcept {}
    void setVoicePan([[maybe_unused]] int voiceIndex,
                     [[maybe_unused]] float pan) noexcept {}
    void setVoiceDelay([[maybe_unused]] int voiceIndex,
                       [[maybe_unused]] float ms) noexcept {}
    void setVoiceDetune([[maybe_unused]] int voiceIndex,
                        [[maybe_unused]] float cents) noexcept {}

    // =========================================================================
    // Query Methods (stubs for Phase 4)
    // =========================================================================

    [[nodiscard]] float getDetectedPitch() const noexcept { return 0.0f; }
    [[nodiscard]] int getDetectedNote() const noexcept { return -1; }
    [[nodiscard]] float getPitchConfidence() const noexcept { return 0.0f; }
    [[nodiscard]] std::size_t getLatencySamples() const noexcept { return 0; }

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
