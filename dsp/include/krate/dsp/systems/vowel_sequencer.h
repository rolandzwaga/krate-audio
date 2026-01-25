// ==============================================================================
// Layer 3: DSP Systems
// vowel_sequencer.h - 16-step Vowel Formant Sequencer
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

namespace Krate {
namespace DSP {

// =============================================================================
// VowelStep
// =============================================================================

/// @brief Single step configuration for vowel sequencer.
///
/// Contains both discrete vowel selection and continuous morph position.
/// Which is used depends on the VowelSequencer's morphMode setting.
struct VowelStep {
    Vowel vowel = Vowel::A;     ///< Discrete vowel selection (when morphMode=false)
    float morph = 0.0f;         ///< Continuous morph position [0,4] (when morphMode=true)

    /// @brief Clamp morph to valid range
    void clamp() noexcept {
        morph = std::clamp(morph, 0.0f, 4.0f);
    }
};

// =============================================================================
// VowelSequencer Class
// =============================================================================

/// @brief 16-step vowel formant sequencer synchronized to tempo.
///
/// Composes SequencerCore for timing and FormantFilter for sound generation.
/// Each step can specify a discrete vowel (A, E, I, O, U) or a continuous
/// morph position (0.0-4.0) for interpolated vowels.
///
/// @par Layer
/// Layer 3 (System) - composes Layer 1 primitives (SequencerCore)
/// and Layer 2 processors (FormantFilter)
///
/// @par Thread Safety
/// Not thread-safe. Use separate instances for each audio thread.
///
/// @par Real-Time Safety
/// All processing methods are noexcept with zero allocations.
///
/// @par Gate Behavior
/// When gate is off, output = input (dry signal at unity).
/// Formula: output = wet * gateRamp + input * (1.0 - gateRamp)
///
/// @example Basic Usage
/// @code
/// VowelSequencer seq;
/// seq.prepare(44100.0);
///
/// // Set up 4 steps with different vowels
/// seq.setNumSteps(4);
/// seq.setStepVowel(0, Vowel::A);
/// seq.setStepVowel(1, Vowel::E);
/// seq.setStepVowel(2, Vowel::I);
/// seq.setStepVowel(3, Vowel::U);
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

    static constexpr size_t kMaxSteps = 16;           ///< Maximum programmable steps
    static constexpr float kMinGlideMs = 0.0f;        ///< Minimum glide time
    static constexpr float kMaxGlideMs = 500.0f;      ///< Maximum glide time

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor
    VowelSequencer() noexcept = default;

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
    // Step Configuration
    // =========================================================================

    /// @brief Set number of active steps.
    /// @param numSteps Number of steps to use (clamped to [1, 16])
    void setNumSteps(size_t numSteps) noexcept;

    /// @brief Get the number of active steps.
    /// @return Current number of active steps (1-16)
    [[nodiscard]] size_t getNumSteps() const noexcept;

    /// @brief Set all parameters for a step at once.
    /// @param stepIndex Step index (0 to kMaxSteps-1, out of range is ignored)
    /// @param step Step configuration
    void setStep(size_t stepIndex, const VowelStep& step) noexcept;

    /// @brief Get step parameters (read-only).
    /// @param stepIndex Step index (0 to kMaxSteps-1)
    /// @return Reference to step configuration (default step if index out of range)
    [[nodiscard]] const VowelStep& getStep(size_t stepIndex) const noexcept;

    /// @brief Set step vowel (discrete mode).
    /// @param stepIndex Step index (0 to kMaxSteps-1, out of range is ignored)
    /// @param vowel Vowel selection (A, E, I, O, U)
    void setStepVowel(size_t stepIndex, Vowel vowel) noexcept;

    /// @brief Set step morph position (continuous mode).
    /// @param stepIndex Step index (0 to kMaxSteps-1, out of range is ignored)
    /// @param morph Morph position (clamped to [0, 4])
    void setStepMorph(size_t stepIndex, float morph) noexcept;

    // =========================================================================
    // Mode Configuration
    // =========================================================================

    /// @brief Set morph mode (discrete vs continuous).
    /// @param enabled true for continuous morph, false for discrete vowels
    void setMorphMode(bool enabled) noexcept;

    /// @brief Check if morph mode is enabled.
    /// @return true if using continuous morph positions
    [[nodiscard]] bool isMorphMode() const noexcept;

    // =========================================================================
    // Global Formant Modification
    // =========================================================================

    /// @brief Shift all formant frequencies by semitones.
    /// @param semitones Shift amount (clamped to [-24, +24])
    void setFormantShift(float semitones) noexcept;

    /// @brief Set gender scaling parameter.
    /// @param amount Gender value (clamped to [-1, +1])
    ///               -1.0 = male (formants down), +1.0 = female (formants up)
    void setGender(float amount) noexcept;

    // =========================================================================
    // Timing Configuration
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

    /// @brief Set glide/portamento time for parameter transitions.
    /// @param ms Glide time in milliseconds (clamped to [0, 500])
    void setGlideTime(float ms) noexcept;

    /// @brief Set gate length as fraction of step duration.
    /// @param gateLength Gate length (clamped to [0, 1]). 1 = full step active
    /// @note When gate < 1, output crossfades to dry input during the off portion
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
    // Processing
    // =========================================================================

    /// @brief Process a single audio sample through the sequenced filter.
    ///
    /// Gate behavior: output = wet * gateRamp + input * (1.0 - gateRamp)
    /// When gate is off, output approaches input (dry at unity).
    ///
    /// @param input Input audio sample
    /// @return Filtered/sequenced output sample
    /// @note Real-time safe: noexcept, zero allocations
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

    /// @brief Apply parameters from current step to formant filter
    void applyStepParameters(int stepIndex) noexcept;

    // =========================================================================
    // Member Variables
    // =========================================================================

    // State
    bool prepared_ = false;
    double sampleRate_ = 44100.0;

    // Step configuration
    std::array<VowelStep, kMaxSteps> steps_{};

    // Mode
    bool morphMode_ = false;

    // Global formant modification
    float formantShift_ = 0.0f;
    float gender_ = 0.0f;

    // Glide time
    float glideTimeMs_ = 0.0f;

    // Components
    SequencerCore core_;          ///< Timing engine
    FormantFilter filter_;        ///< Formant filter

    // Previous step tracking for change detection
    int lastAppliedStep_ = -1;
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline void VowelSequencer::prepare(double sampleRate) noexcept {
    sampleRate_ = (sampleRate >= 1000.0) ? sampleRate : 1000.0;
    prepared_ = true;

    // Prepare timing engine
    core_.prepare(sampleRate_);

    // Prepare formant filter
    filter_.prepare(sampleRate_);
    filter_.setSmoothingTime(glideTimeMs_ > 0.0f ? glideTimeMs_ : 5.0f);

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
    // Also set morph to match vowel for consistency
    steps_[stepIndex].morph = static_cast<float>(static_cast<int>(vowel));
}

inline void VowelSequencer::setStepMorph(size_t stepIndex, float morph) noexcept {
    if (stepIndex >= kMaxSteps) return;
    steps_[stepIndex].morph = std::clamp(morph, 0.0f, 4.0f);
    // Also update vowel to nearest discrete value for consistency
    int nearestVowel = static_cast<int>(std::round(steps_[stepIndex].morph));
    nearestVowel = std::clamp(nearestVowel, 0, 4);
    steps_[stepIndex].vowel = static_cast<Vowel>(nearestVowel);
}

inline void VowelSequencer::setMorphMode(bool enabled) noexcept {
    morphMode_ = enabled;
}

inline bool VowelSequencer::isMorphMode() const noexcept {
    return morphMode_;
}

inline void VowelSequencer::setFormantShift(float semitones) noexcept {
    formantShift_ = std::clamp(semitones, -24.0f, 24.0f);
    filter_.setFormantShift(formantShift_);
}

inline void VowelSequencer::setGender(float amount) noexcept {
    gender_ = std::clamp(amount, -1.0f, 1.0f);
    filter_.setGender(gender_);
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

inline void VowelSequencer::setGlideTime(float ms) noexcept {
    glideTimeMs_ = std::clamp(ms, kMinGlideMs, kMaxGlideMs);
    if (prepared_) {
        filter_.setSmoothingTime(glideTimeMs_ > 0.0f ? glideTimeMs_ : 5.0f);
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

    // Apply gate with crossfade to dry signal
    // Gate behavior: output = wet * gateRamp + input * (1.0 - gateRamp)
    float gateValue = core_.getGateRampValue();
    float output = wet * gateValue + input * (1.0f - gateValue);

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

    // Apply vowel or morph position based on mode
    if (morphMode_) {
        filter_.setVowelMorph(step.morph);
    } else {
        filter_.setVowel(step.vowel);
    }
}

} // namespace DSP
} // namespace Krate
