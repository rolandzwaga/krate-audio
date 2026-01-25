// ==============================================================================
// Layer 3: DSP Systems
// vowel_sequencer.h - 8-step Vowel Formant Sequencer
// ==============================================================================
// API Contract for specs/099-vowel-sequencer
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 3 (depends on Layers 0-2)
// - Principle X: DSP Constraints (flush denormals, handle edge cases)
//
// Dependencies:
// - SequencerCore (Layer 1): Timing engine
// - FormantFilter (Layer 2): Formant processing
// - NoteValue/NoteModifier (Layer 0): Tempo sync timing
// - BlockContext (Layer 0): Host tempo information
// ==============================================================================

#pragma once

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/filter_tables.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/primitives/sequencer_core.h>
#include <krate/dsp/processors/formant_filter.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Krate {
namespace DSP {

// =============================================================================
// VowelStep (FR-016, FR-017)
// =============================================================================

/// @brief Single step configuration for vowel sequencer.
///
/// Each step specifies a vowel sound and an optional per-step formant shift.
struct VowelStep {
    Vowel vowel = Vowel::A;           ///< Vowel sound (A, E, I, O, U)
    float formantShift = 0.0f;        ///< Formant shift in semitones [-24, +24]

    /// @brief Clamp formant shift to valid range
    void clamp() noexcept {
        formantShift = std::clamp(formantShift, -24.0f, 24.0f);
    }
};

// =============================================================================
// VowelSequencer Class (FR-015 through FR-025)
// =============================================================================

/// @brief 8-step vowel formant sequencer synchronized to tempo.
///
/// Composes SequencerCore for timing and FormantFilter for sound generation.
/// Each step specifies a vowel (A, E, I, O, U) and optional per-step formant
/// shift for pitch-varied talking effects.
///
/// @par Layer
/// Layer 3 (System) - composes Layer 1 primitives (SequencerCore)
/// and Layer 2 processors (FormantFilter)
///
/// @par Default Pattern (FR-015a)
/// Steps default to palindrome: A, E, I, O, U, O, I, E
///
/// @par Thread Safety
/// Not thread-safe. Use separate instances for each audio thread.
///
/// @par Real-Time Safety
/// All processing methods are noexcept with zero allocations.
///
/// @par Gate Behavior (FR-012a)
/// Bypass-safe design: dry signal always at unity, wet fades out.
/// Formula: output = wet * gateRamp + input
///
/// @example Basic Usage
/// @code
/// VowelSequencer seq;
/// seq.prepare(44100.0);
///
/// // Use preset pattern
/// seq.setPreset("wow");  // O, A, O
///
/// // Or configure manually
/// seq.setNumSteps(4);
/// seq.setStepVowel(0, Vowel::A);
/// seq.setStepVowel(1, Vowel::E);
/// seq.setStepFormantShift(1, 12.0f);  // +1 octave on step 1
///
/// // Configure timing
/// seq.setTempo(120.0f);
/// seq.setNoteValue(NoteValue::Quarter);
///
/// // Process audio
/// for (size_t i = 0; i < numSamples; ++i) {
///     buffer[i] = seq.process(buffer[i]);
/// }
/// @endcode
class VowelSequencer {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kMaxSteps = 8;            ///< Maximum programmable steps (FR-015)
    static constexpr float kMinMorphTimeMs = 0.0f;    ///< Minimum morph time
    static constexpr float kMaxMorphTimeMs = 500.0f;  ///< Maximum morph time (FR-020)

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor - initializes default pattern A,E,I,O,U,O,I,E
    VowelSequencer() noexcept {
        initializeDefaultPattern();
    }

    /// @brief Prepare the sequencer for audio processing.
    ///
    /// Must be called before process() or processBlock(). Initializes the
    /// internal filter and timing engine. May be called multiple times to
    /// change sample rate.
    ///
    /// @param sampleRate Sample rate in Hz (clamped to minimum 1000 Hz)
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all processing state while preserving configuration.
    ///
    /// Resets the filter and timing engine, returns to the starting step
    /// based on direction. Step configurations are preserved.
    void reset() noexcept;

    /// @brief Check if the sequencer has been prepared for processing.
    /// @return true if prepare() has been called, false otherwise
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Step Configuration (FR-016, FR-017)
    // =========================================================================

    /// @brief Set number of active steps.
    /// @param numSteps Number of steps to use (clamped to [1, 8])
    void setNumSteps(size_t numSteps) noexcept;

    /// @brief Get the number of active steps.
    /// @return Current number of active steps (1-8)
    [[nodiscard]] size_t getNumSteps() const noexcept;

    /// @brief Set all parameters for a step at once.
    /// @param stepIndex Step index (0 to kMaxSteps-1, out of range is ignored)
    /// @param step Step configuration (formantShift will be clamped)
    void setStep(size_t stepIndex, const VowelStep& step) noexcept;

    /// @brief Get step parameters (read-only).
    /// @param stepIndex Step index (0 to kMaxSteps-1)
    /// @return Reference to step configuration (default step if index out of range)
    [[nodiscard]] const VowelStep& getStep(size_t stepIndex) const noexcept;

    /// @brief Set step vowel.
    /// @param stepIndex Step index (0 to kMaxSteps-1, out of range is ignored)
    /// @param vowel Vowel selection (A, E, I, O, U)
    void setStepVowel(size_t stepIndex, Vowel vowel) noexcept;

    /// @brief Set step formant shift in semitones (FR-017).
    /// @param stepIndex Step index (0 to kMaxSteps-1, out of range is ignored)
    /// @param semitones Formant shift (clamped to [-24, +24])
    void setStepFormantShift(size_t stepIndex, float semitones) noexcept;

    // =========================================================================
    // Presets (FR-021)
    // =========================================================================

    /// @brief Load a preset vowel pattern.
    /// @param name Preset name: "aeiou", "wow", or "yeah"
    /// @return true if preset was found and loaded, false otherwise
    /// @note When preset loads, numSteps updates to match preset length.
    ///       Remaining steps (beyond preset) preserve previous values (FR-021a).
    bool setPreset(const char* name) noexcept;

    // =========================================================================
    // Timing Configuration (FR-020)
    // =========================================================================

    /// @brief Set tempo in beats per minute.
    /// @param bpm Tempo (clamped to [20, 300] BPM)
    void setTempo(float bpm) noexcept;

    /// @brief Set note value for step duration (tempo sync).
    /// @param value Base note value (Whole, Half, Quarter, Eighth, etc.)
    /// @param modifier Optional modifier (None, Dotted, Triplet)
    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;

    /// @brief Set swing amount for groove timing.
    /// @param swing Swing amount (clamped to [0, 1]). 0 = no swing, 0.5 = 3:1 ratio
    void setSwing(float swing) noexcept;

    /// @brief Set morph time for smooth vowel transitions (FR-020).
    /// @param ms Morph time in milliseconds (clamped to [0, 500])
    /// @note If step duration < morph time, morph is truncated (FR-020a)
    void setMorphTime(float ms) noexcept;

    /// @brief Set gate length as fraction of step duration.
    /// @param gateLength Gate length (clamped to [0, 1]). 1 = full step active
    /// @note When gate < 1, wet signal fades to zero while dry passes through
    void setGateLength(float gateLength) noexcept;

    // =========================================================================
    // Playback Configuration
    // =========================================================================

    /// @brief Set playback direction mode.
    /// @param direction Direction mode (Forward, Backward, PingPong, Random)
    void setDirection(Direction direction) noexcept;

    /// @brief Get the current playback direction mode.
    /// @return Current Direction setting
    [[nodiscard]] Direction getDirection() const noexcept;

    // =========================================================================
    // Transport
    // =========================================================================

    /// @brief Sync to DAW transport position via PPQ.
    /// @param ppqPosition Musical position in beats from timeline start
    void sync(double ppqPosition) noexcept;

    /// @brief Manually trigger advancement to the next step.
    void trigger() noexcept;

    /// @brief Get the current step index.
    /// @return Current step index (0 to numSteps-1)
    [[nodiscard]] int getCurrentStep() const noexcept;

    // =========================================================================
    // Processing (FR-022, FR-023)
    // =========================================================================

    /// @brief Process a single audio sample through the sequenced filter.
    ///
    /// Gate behavior (FR-012a): output = wet * gateRamp + input
    /// Dry signal always at unity, wet fades out when gate closes.
    ///
    /// @param input Input audio sample
    /// @return Filtered/sequenced output sample
    /// @note Real-time safe: noexcept, zero allocations (FR-024, FR-025)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of audio samples with optional host context.
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param numSamples Number of samples in the buffer
    /// @param ctx Optional BlockContext for tempo sync
    void processBlock(float* buffer, size_t numSamples,
                      const BlockContext* ctx = nullptr) noexcept;

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Initialize default palindrome pattern A,E,I,O,U,O,I,E
    void initializeDefaultPattern() noexcept;

    /// @brief Apply parameters from current step to formant filter
    void applyStepParameters(int stepIndex) noexcept;

    // =========================================================================
    // Member Variables
    // =========================================================================

    // State
    bool prepared_ = false;
    double sampleRate_ = 44100.0;

    // Step configuration (FR-015a: default pattern set in constructor)
    std::array<VowelStep, kMaxSteps> steps_{};

    // Morph time (FR-020)
    float morphTimeMs_ = 50.0f;

    // Components
    SequencerCore core_;          ///< Timing engine
    FormantFilter filter_;        ///< Formant filter

    // Previous step tracking for change detection
    int lastAppliedStep_ = -1;
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline void VowelSequencer::initializeDefaultPattern() noexcept {
    // FR-015a: Default pattern is palindrome A,E,I,O,U,O,I,E
    steps_[0] = VowelStep{Vowel::A, 0.0f};
    steps_[1] = VowelStep{Vowel::E, 0.0f};
    steps_[2] = VowelStep{Vowel::I, 0.0f};
    steps_[3] = VowelStep{Vowel::O, 0.0f};
    steps_[4] = VowelStep{Vowel::U, 0.0f};
    steps_[5] = VowelStep{Vowel::O, 0.0f};
    steps_[6] = VowelStep{Vowel::I, 0.0f};
    steps_[7] = VowelStep{Vowel::E, 0.0f};

    // Default to 8 steps
    core_.setNumSteps(kMaxSteps);
}

inline void VowelSequencer::prepare(double sampleRate) noexcept {
    sampleRate_ = (sampleRate >= 1000.0) ? sampleRate : 1000.0;
    prepared_ = true;

    // Prepare timing engine
    core_.prepare(sampleRate_);

    // Prepare formant filter
    filter_.prepare(sampleRate_);
    filter_.setSmoothingTime(morphTimeMs_ > 0.0f ? morphTimeMs_ : 5.0f);

    // Apply initial step parameters
    applyStepParameters(core_.getCurrentStep());
    lastAppliedStep_ = core_.getCurrentStep();
}

inline void VowelSequencer::reset() noexcept {
    core_.reset();
    filter_.reset();
    lastAppliedStep_ = -1;

    if (prepared_) {
        applyStepParameters(core_.getCurrentStep());
        lastAppliedStep_ = core_.getCurrentStep();
    }
}

inline bool VowelSequencer::isPrepared() const noexcept {
    return prepared_;
}

inline void VowelSequencer::setNumSteps(size_t numSteps) noexcept {
    // Clamp to VowelSequencer's max of 8, then pass to SequencerCore
    numSteps = std::clamp(numSteps, size_t{1}, kMaxSteps);
    core_.setNumSteps(numSteps);
}

inline size_t VowelSequencer::getNumSteps() const noexcept {
    return core_.getNumSteps();
}

inline void VowelSequencer::setStep(size_t stepIndex, const VowelStep& step) noexcept {
    if (stepIndex >= kMaxSteps) return;
    steps_[stepIndex] = step;
    steps_[stepIndex].clamp();
}

inline const VowelStep& VowelSequencer::getStep(size_t stepIndex) const noexcept {
    if (stepIndex >= kMaxSteps) {
        static const VowelStep defaultStep{};
        return defaultStep;
    }
    return steps_[stepIndex];
}

inline void VowelSequencer::setStepVowel(size_t stepIndex, Vowel vowel) noexcept {
    if (stepIndex >= kMaxSteps) return;
    steps_[stepIndex].vowel = vowel;
}

inline void VowelSequencer::setStepFormantShift(size_t stepIndex, float semitones) noexcept {
    if (stepIndex >= kMaxSteps) return;
    steps_[stepIndex].formantShift = std::clamp(semitones, -24.0f, 24.0f);
}

inline bool VowelSequencer::setPreset(const char* name) noexcept {
    if (name == nullptr) return false;

    // FR-021: Built-in presets
    if (std::strcmp(name, "aeiou") == 0) {
        steps_[0].vowel = Vowel::A;
        steps_[1].vowel = Vowel::E;
        steps_[2].vowel = Vowel::I;
        steps_[3].vowel = Vowel::O;
        steps_[4].vowel = Vowel::U;
        // FR-021a: numSteps updates to match preset length
        core_.setNumSteps(5);
        return true;
    }

    if (std::strcmp(name, "wow") == 0) {
        steps_[0].vowel = Vowel::O;
        steps_[1].vowel = Vowel::A;
        steps_[2].vowel = Vowel::O;
        core_.setNumSteps(3);
        return true;
    }

    if (std::strcmp(name, "yeah") == 0) {
        steps_[0].vowel = Vowel::I;
        steps_[1].vowel = Vowel::E;
        steps_[2].vowel = Vowel::A;
        core_.setNumSteps(3);
        return true;
    }

    // Unknown preset - return false, pattern unchanged
    return false;
}

inline void VowelSequencer::setTempo(float bpm) noexcept {
    core_.setTempo(bpm);
}

inline void VowelSequencer::setNoteValue(NoteValue value, NoteModifier modifier) noexcept {
    core_.setNoteValue(value, modifier);
}

inline void VowelSequencer::setSwing(float swing) noexcept {
    core_.setSwing(swing);
}

inline void VowelSequencer::setMorphTime(float ms) noexcept {
    morphTimeMs_ = std::clamp(ms, kMinMorphTimeMs, kMaxMorphTimeMs);
    if (prepared_) {
        filter_.setSmoothingTime(morphTimeMs_ > 0.0f ? morphTimeMs_ : 5.0f);
    }
}

inline void VowelSequencer::setGateLength(float gateLength) noexcept {
    core_.setGateLength(gateLength);
}

inline void VowelSequencer::setDirection(Direction direction) noexcept {
    core_.setDirection(direction);
}

inline Direction VowelSequencer::getDirection() const noexcept {
    return core_.getDirection();
}

inline void VowelSequencer::sync(double ppqPosition) noexcept {
    core_.sync(ppqPosition);
}

inline void VowelSequencer::trigger() noexcept {
    core_.trigger();
    applyStepParameters(core_.getCurrentStep());
}

inline int VowelSequencer::getCurrentStep() const noexcept {
    return core_.getCurrentStep();
}

inline float VowelSequencer::process(float input) noexcept {
    if (!prepared_) {
        return 0.0f;
    }

    // Handle NaN/Inf input
    if (detail::isNaN(input) || detail::isInf(input)) {
        filter_.reset();
        return 0.0f;
    }

    // Advance timing and check for step change
    bool stepChanged = core_.tick();

    // Apply new step parameters on step change
    int currentStep = core_.getCurrentStep();
    if (stepChanged || currentStep != lastAppliedStep_) {
        applyStepParameters(currentStep);
        lastAppliedStep_ = currentStep;
    }

    // Process through formant filter
    float wet = filter_.process(input);

    // FR-012a: Bypass-safe gate behavior
    // output = wet * gateRamp + input (dry always at unity, wet fades)
    float gateValue = core_.getGateRampValue();
    float output = wet * gateValue + input;

    return output;
}

inline void VowelSequencer::processBlock(float* buffer, size_t numSamples,
                                          const BlockContext* ctx) noexcept {
    if (!prepared_ || buffer == nullptr) return;

    // Update tempo from context if provided
    if (ctx != nullptr) {
        setTempo(static_cast<float>(ctx->tempoBPM));
    }

    // Process each sample
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = process(buffer[i]);
    }
}

inline void VowelSequencer::applyStepParameters(int stepIndex) noexcept {
    if (stepIndex < 0 || stepIndex >= static_cast<int>(kMaxSteps)) return;

    const auto& step = steps_[static_cast<size_t>(stepIndex)];

    // Apply vowel
    filter_.setVowel(step.vowel);

    // Apply per-step formant shift (FR-017)
    filter_.setFormantShift(step.formantShift);
}

} // namespace DSP
} // namespace Krate
