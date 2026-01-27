// ==============================================================================
// VowelSequencer API Contract
// ==============================================================================
// Layer 3 System - Vowel step sequencer with tempo sync
//
// This file defines the public API contract for VowelSequencer. Implementation
// will be in dsp/include/krate/dsp/systems/vowel_sequencer.h
// ==============================================================================

#pragma once

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/filter_tables.h>  // Vowel enum
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/primitives/sequencer_core.h>  // SequencerCore, Direction
#include <krate/dsp/primitives/smoother.h>        // LinearRamp
#include <krate/dsp/processors/formant_filter.h>

#include <array>
#include <cstddef>

namespace Krate::DSP {

/// @brief Single step configuration for vowel sequencer
struct VowelStep {
    Vowel vowel = Vowel::A;           ///< Vowel sound (A, E, I, O, U)
    float formantShift = 0.0f;        ///< Formant shift in semitones [-24, +24]

    /// @brief Clamp formant shift to valid range
    void clamp() noexcept {
        formantShift = std::clamp(formantShift, -24.0f, 24.0f);
    }
};

/// @brief 8-step vowel sequencer with tempo sync (Layer 3 System)
///
/// Creates rhythmic "talking" vowel effects by sequencing through vowel sounds
/// synchronized to tempo. Composes SequencerCore (timing) + FormantFilter (sound)
/// + LinearRamp (morphing).
///
/// @par Default Pattern
/// Palindrome: A, E, I, O, U, O, I, E (8 steps)
///
/// @par Real-Time Safety
/// All process methods are noexcept and allocation-free.
///
/// @par Gate Behavior (Bypass Safe)
/// When gate is off, dry signal passes at unity while wet fades out.
/// Formula: output = wet * gateRamp + input
///
/// @par Usage Pattern
/// @code
/// VowelSequencer seq;
/// seq.prepare(44100.0);
/// seq.setNumSteps(5);
/// seq.setPreset("aeiou");
/// seq.setMorphTime(50.0f);
///
/// // In process loop:
/// for (size_t i = 0; i < numSamples; ++i) {
///     buffer[i] = seq.process(buffer[i]);
/// }
/// @endcode
///
/// @see SequencerCore, FormantFilter
class VowelSequencer {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Maximum number of steps (8 for vowel sequences)
    static constexpr size_t kMaxSteps = 8;

    /// Morph time range in milliseconds
    static constexpr float kMinMorphTimeMs = 0.0f;
    static constexpr float kMaxMorphTimeMs = 500.0f;

    /// Default morph time
    static constexpr float kDefaultMorphTimeMs = 50.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor with palindrome pattern
    VowelSequencer() noexcept;

    /// @brief Prepare for processing at given sample rate
    /// @param sampleRate Sample rate in Hz
    /// @post isPrepared() returns true
    void prepare(double sampleRate) noexcept;

    /// @brief Reset to initial state
    /// @post getCurrentStep() returns 0
    /// @post Pattern configuration preserved
    void reset() noexcept;

    /// @brief Check if sequencer is ready for processing
    /// @return true if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Step Configuration
    // =========================================================================

    /// @brief Set number of active steps
    /// @param numSteps Number of steps [1, kMaxSteps], clamped
    void setNumSteps(size_t numSteps) noexcept;

    /// @brief Get current number of active steps
    /// @return Number of steps [1, kMaxSteps]
    [[nodiscard]] size_t getNumSteps() const noexcept;

    /// @brief Set complete step configuration
    /// @param stepIndex Step index [0, kMaxSteps-1], ignored if out of range
    /// @param step Step configuration (vowel, formantShift)
    void setStep(size_t stepIndex, const VowelStep& step) noexcept;

    /// @brief Get step configuration
    /// @param stepIndex Step index [0, kMaxSteps-1]
    /// @return Step configuration, default values if out of range
    [[nodiscard]] const VowelStep& getStep(size_t stepIndex) const noexcept;

    /// @brief Set step vowel
    /// @param stepIndex Step index [0, kMaxSteps-1], ignored if out of range
    /// @param vowel Vowel sound
    void setStepVowel(size_t stepIndex, Vowel vowel) noexcept;

    /// @brief Set step formant shift
    /// @param stepIndex Step index [0, kMaxSteps-1], ignored if out of range
    /// @param semitones Formant shift [-24, +24], clamped
    void setStepFormantShift(size_t stepIndex, float semitones) noexcept;

    // =========================================================================
    // Preset Management
    // =========================================================================

    /// @brief Load a preset pattern
    /// @param name Preset name: "aeiou", "wow", "yeah"
    /// @return true if preset found, false if unknown
    ///
    /// @par Preset Patterns
    /// - "aeiou": A, E, I, O, U (5 steps)
    /// - "wow":   O, A, O (3 steps)
    /// - "yeah":  I, E, A (3 steps)
    ///
    /// @par Behavior
    /// When preset is loaded, numSteps updates to match preset length.
    /// Steps beyond preset length preserve previous values.
    [[nodiscard]] bool setPreset(const char* name) noexcept;

    // =========================================================================
    // Morph Configuration
    // =========================================================================

    /// @brief Set morph time between vowels
    /// @param ms Morph time [kMinMorphTimeMs, kMaxMorphTimeMs], clamped
    ///
    /// @par SC-002 Morph Accuracy
    /// Vowel morphing completes within specified time +/- 1ms.
    void setMorphTime(float ms) noexcept;

    /// @brief Get current morph time
    /// @return Morph time in milliseconds
    [[nodiscard]] float getMorphTime() const noexcept;

    // =========================================================================
    // Timing Configuration (Delegated to SequencerCore)
    // =========================================================================

    /// @brief Set tempo in beats per minute
    /// @param bpm Tempo [20, 300], clamped
    void setTempo(float bpm) noexcept;

    /// @brief Set note value for step timing
    /// @param value Note value (Whole, Half, Quarter, Eighth, etc.)
    /// @param modifier Optional modifier (None, Dotted, Triplet)
    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;

    /// @brief Set swing amount
    /// @param swing Swing [0.0, 1.0], clamped
    void setSwing(float swing) noexcept;

    /// @brief Set gate length as fraction of step duration
    /// @param gateLength Gate length [0.0, 1.0], clamped
    void setGateLength(float gateLength) noexcept;

    /// @brief Set playback direction
    /// @param direction Direction mode
    void setDirection(Direction direction) noexcept;

    /// @brief Get current playback direction
    /// @return Current direction mode
    [[nodiscard]] Direction getDirection() const noexcept;

    // =========================================================================
    // Transport Control (Delegated to SequencerCore)
    // =========================================================================

    /// @brief Sync to DAW transport position
    /// @param ppqPosition Position in quarter notes
    void sync(double ppqPosition) noexcept;

    /// @brief Manually advance to next step
    void trigger() noexcept;

    /// @brief Get current step index
    /// @return Step index [0, numSteps-1]
    [[nodiscard]] int getCurrentStep() const noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process single sample
    /// @param input Input sample
    /// @return Processed sample with vowel effect
    /// @pre isPrepared() returns true
    ///
    /// When not prepared, returns 0.0f.
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process block of samples (in-place)
    /// @param buffer Audio buffer to process in-place
    /// @param numSamples Number of samples
    /// @param ctx Optional block context for tempo sync
    ///
    /// If ctx is provided and ctx->isPlaying, tempo is updated from ctx->tempoBPM.
    void processBlock(float* buffer, size_t numSamples,
                      const BlockContext* ctx = nullptr) noexcept;

private:
    // State
    bool prepared_ = false;
    double sampleRate_ = 44100.0;

    // Step configuration
    std::array<VowelStep, kMaxSteps> steps_{};

    // Timing (delegated)
    SequencerCore sequencer_;

    // Morph time
    float morphTimeMs_ = kDefaultMorphTimeMs;

    // Processing components
    FormantFilter formantFilter_;
    LinearRamp morphRamp_;       // For vowel morph position
    LinearRamp gateRamp_;        // For gate crossfade (5ms)

    // Processing state
    Vowel previousVowel_ = Vowel::A;
    Vowel currentVowel_ = Vowel::A;
    float currentFormantShift_ = 0.0f;

    // Private helper
    static constexpr VowelStep kDefaultStep{};
    void initializeDefaultPattern() noexcept;
    void applyStepParameters(int step) noexcept;
};

} // namespace Krate::DSP
