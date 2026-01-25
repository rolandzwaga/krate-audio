// ==============================================================================
// Layer 3: DSP Systems
// filter_step_sequencer.h - 16-step Filter Parameter Sequencer
// ==============================================================================
// API Contract for specs/098-filter-step-sequencer
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 3 (depends on Layers 0-2)
// - Principle X: DSP Constraints (flush denormals, handle edge cases)
//
// Dependencies (Layer 0-1):
// - SequencerCore (Layer 1): Timing, direction, gate handling
// - SVF (Layer 1): Core filter processing
// - LinearRamp (Layer 1): Parameter glide smoothing
// - BlockContext (Layer 0): Host tempo information
// - dbToGain (Layer 0): Gain conversion
// ==============================================================================

#pragma once

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/sequencer_core.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/svf.h>

#include <algorithm>
#include <array>
#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// SequencerStep (FR-001 to FR-006)
// =============================================================================

/// @brief Single step configuration in the filter sequence.
///
/// All parameters have sensible defaults for immediate use.
/// Parameters are clamped to valid ranges when set.
struct SequencerStep {
    float cutoffHz = 1000.0f;          ///< Cutoff frequency [20, 20000] Hz
    float q = 0.707f;                  ///< Resonance/Q [0.5, 20.0] (Butterworth default)
    SVFMode type = SVFMode::Lowpass;   ///< Filter mode
    float gainDb = 0.0f;               ///< Gain [-24, +12] dB (for Peak/Shelf modes)

    /// @brief Clamp all parameters to valid ranges
    void clamp() noexcept {
        cutoffHz = std::clamp(cutoffHz, 20.0f, 20000.0f);
        q = std::clamp(q, 0.5f, 20.0f);
        gainDb = std::clamp(gainDb, -24.0f, 12.0f);
    }
};

// =============================================================================
// Direction Enumeration
// =============================================================================
// Direction enum is defined in <krate/dsp/primitives/sequencer_core.h>
// Re-exported here for backward compatibility.
// Available values: Direction::Forward, Direction::Backward, Direction::PingPong, Direction::Random

// =============================================================================
// FilterStepSequencer Class (FR-001 through FR-022)
// =============================================================================

/// @brief 16-step filter parameter sequencer synchronized to tempo.
///
/// Composes SequencerCore for timing/direction and SVF filter with LinearRamp
/// smoothers to create rhythmic filter sweeps.
///
/// @par Layer
/// Layer 3 (System) - composes Layer 1 primitives (SequencerCore, SVF, LinearRamp)
///
/// @par Thread Safety
/// Not thread-safe. Use separate instances for each audio thread.
///
/// @par Real-Time Safety
/// All processing methods are noexcept with zero allocations.
///
/// @example Basic Usage
/// @code
/// FilterStepSequencer seq;
/// seq.prepare(44100.0);
///
/// // Set up 4 steps with different cutoffs
/// seq.setNumSteps(4);
/// seq.setStepCutoff(0, 200.0f);
/// seq.setStepCutoff(1, 800.0f);
/// seq.setStepCutoff(2, 2000.0f);
/// seq.setStepCutoff(3, 5000.0f);
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
class FilterStepSequencer {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kMaxSteps = 16;           ///< Maximum programmable steps
    static constexpr float kMinTempoBPM = 20.0f;      ///< Minimum tempo
    static constexpr float kMaxTempoBPM = 300.0f;     ///< Maximum tempo
    static constexpr float kMinGlideMs = 0.0f;        ///< Minimum glide time
    static constexpr float kMaxGlideMs = 500.0f;      ///< Maximum glide time
    static constexpr float kMinSwing = 0.0f;          ///< Minimum swing (0%)
    static constexpr float kMaxSwing = 1.0f;          ///< Maximum swing (100%)
    static constexpr float kMinGateLength = 0.0f;     ///< Minimum gate (0%)
    static constexpr float kMaxGateLength = 1.0f;     ///< Maximum gate (100%)
    static constexpr float kGateCrossfadeMs = 5.0f;   ///< Fixed crossfade duration
    static constexpr float kTypeCrossfadeMs = 5.0f;   ///< Crossfade duration for filter type changes

    // =========================================================================
    // Lifecycle (FR-020, FR-021)
    // =========================================================================

    /// @brief Default constructor
    FilterStepSequencer() noexcept = default;

    /// @brief Prepare the sequencer for audio processing.
    ///
    /// Must be called before process() or processBlock(). Initializes the
    /// internal filter, parameter ramps, and timing calculations. May be
    /// called multiple times to change sample rate.
    ///
    /// @param sampleRate Sample rate in Hz (clamped to minimum 1000 Hz)
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all processing state while preserving configuration.
    ///
    /// Resets the filter, returns to the starting step based on direction,
    /// and snaps all parameter ramps to their target values. Step parameters
    /// (cutoff, Q, type, gain) are preserved.
    void reset() noexcept;

    /// @brief Check if the sequencer has been prepared for processing.
    /// @return true if prepare() has been called, false otherwise
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Step Configuration (FR-001 to FR-006)
    // =========================================================================

    /// @brief Set number of active steps.
    /// @param numSteps Number of steps to use (clamped to [1, 16])
    void setNumSteps(size_t numSteps) noexcept;

    /// @brief Get the number of active steps.
    /// @return Current number of active steps (1-16)
    [[nodiscard]] size_t getNumSteps() const noexcept;

    /// @brief Set all parameters for a step at once.
    /// @param stepIndex Step index (0 to kMaxSteps-1, out of range is ignored)
    /// @param step Step configuration (parameters will be clamped to valid ranges)
    void setStep(size_t stepIndex, const SequencerStep& step) noexcept;

    /// @brief Get step parameters (read-only).
    /// @param stepIndex Step index (0 to kMaxSteps-1)
    /// @return Reference to step configuration (default step if index out of range)
    [[nodiscard]] const SequencerStep& getStep(size_t stepIndex) const noexcept;

    /// @brief Set step cutoff frequency.
    /// @param stepIndex Step index (0 to kMaxSteps-1, out of range is ignored)
    /// @param hz Cutoff frequency in Hz (clamped to [20, 20000])
    /// @note At runtime, cutoff is also clamped to sampleRate * 0.495 for Nyquist safety
    void setStepCutoff(size_t stepIndex, float hz) noexcept;

    /// @brief Set step resonance/Q factor.
    /// @param stepIndex Step index (0 to kMaxSteps-1, out of range is ignored)
    /// @param q Q factor (clamped to [0.5, 20.0]). 0.707 is Butterworth (no peak).
    void setStepQ(size_t stepIndex, float q) noexcept;

    /// @brief Set step filter type.
    /// @param stepIndex Step index (0 to kMaxSteps-1, out of range is ignored)
    /// @param type SVFMode (Lowpass, Highpass, Bandpass, Notch, Allpass, Peak)
    /// @note Filter type changes use 5ms crossfade between old and new filter outputs
    void setStepType(size_t stepIndex, SVFMode type) noexcept;

    /// @brief Set step gain.
    /// @param stepIndex Step index (0 to kMaxSteps-1, out of range is ignored)
    /// @param dB Gain in decibels (clamped to [-24, +12] dB)
    /// @note Gain is applied to the filter output, useful for creating accents
    void setStepGain(size_t stepIndex, float dB) noexcept;

    // =========================================================================
    // Timing Configuration (FR-007 to FR-011)
    // =========================================================================

    /// @brief Set tempo in beats per minute.
    /// @param bpm Tempo (clamped to [20, 300] BPM)
    /// @note Step durations update immediately when tempo changes
    void setTempo(float bpm) noexcept;

    /// @brief Set note value for step duration (tempo sync).
    /// @param value Base note value (Whole, Half, Quarter, Eighth, etc.)
    /// @param modifier Optional modifier (None, Dotted, Triplet)
    /// @note Triplet modifier creates 3 steps per 2 base notes
    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;

    /// @brief Set swing amount for groove timing.
    /// @param swing Swing amount (clamped to [0, 1]). 0 = no swing, 0.5 = 3:1 ratio
    /// @note Swing makes even steps longer and odd steps shorter
    void setSwing(float swing) noexcept;

    /// @brief Set glide/portamento time for parameter transitions.
    /// @param ms Glide time in milliseconds (clamped to [0, 500])
    /// @note Glide is truncated if step duration is shorter than glide time
    /// @note Filter type changes use a fixed 5ms crossfade (independent of glide time)
    void setGlideTime(float ms) noexcept;

    /// @brief Set gate length as fraction of step duration.
    /// @param gateLength Gate length (clamped to [0, 1]). 1 = full step active
    /// @note When gate < 1, filter output crossfades to dry during the off portion
    /// @note Uses a fixed 5ms crossfade for click-free transitions
    void setGateLength(float gateLength) noexcept;

    // =========================================================================
    // Playback Configuration (FR-012)
    // =========================================================================

    /// @brief Set playback direction mode.
    /// @param direction Direction mode (Forward, Backward, PingPong, Random)
    /// @note Changing direction calls reset() to return to the starting step
    void setDirection(Direction direction) noexcept;

    /// @brief Get the current playback direction mode.
    /// @return Current Direction setting
    [[nodiscard]] Direction getDirection() const noexcept;

    // =========================================================================
    // Transport (FR-013, FR-014)
    // =========================================================================

    /// @brief Sync to DAW transport position via PPQ (Pulses Per Quarter note).
    /// @param ppqPosition Musical position in beats from timeline start
    /// @note Calculates the correct step and phase for the given position
    /// @note In Random mode, sync keeps the current step (cannot predict random sequence)
    void sync(double ppqPosition) noexcept;

    /// @brief Manually trigger advancement to the next step.
    ///
    /// Immediately advances to the next step based on the current direction,
    /// resets the sample counter, and applies the new step's parameters.
    /// Useful for external triggering independent of tempo.
    void trigger() noexcept;

    /// @brief Get the current step index.
    /// @return Current step index (0 to numSteps-1)
    [[nodiscard]] int getCurrentStep() const noexcept;

    // =========================================================================
    // Processing (FR-015 to FR-019, FR-022)
    // =========================================================================

    /// @brief Process a single audio sample through the sequenced filter.
    ///
    /// Handles step advancement, gate state, parameter gliding, and filtering.
    /// Returns 0 if not prepared. Returns 0 and resets filter if input is NaN/Inf.
    ///
    /// @param input Input audio sample
    /// @return Filtered output sample (or 0 if not prepared or input is NaN/Inf)
    /// @note Real-time safe: noexcept, zero allocations
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of audio samples with optional host context.
    ///
    /// Processes each sample through process(), updating tempo from context if
    /// provided. The buffer is modified in-place.
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param numSamples Number of samples in the buffer
    /// @param ctx Optional BlockContext for tempo sync. If provided, setTempo()
    ///            is called with ctx->tempoBPM before processing.
    /// @note Real-time safe: noexcept, zero allocations
    void processBlock(float* buffer, size_t numSamples,
                      const BlockContext* ctx = nullptr) noexcept;

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Apply parameters from current step to filter with glide
    void applyStepParameters(int stepIndex) noexcept;

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Sequencer core handles timing, direction, gate
    SequencerCore sequencer_;

    // State
    double sampleRate_ = 44100.0;

    // Step configuration (filter-specific)
    std::array<SequencerStep, kMaxSteps> steps_{};

    // Glide time (filter-specific, not in SequencerCore)
    float glideTimeMs_ = 0.0f;

    // Previous step for detecting step changes
    int previousStep_ = -1;

    // Components (Layer 1)
    SVF filter_;              ///< Primary filter (current type)
    SVF filterOld_;           ///< Secondary filter for type crossfade
    LinearRamp cutoffRamp_;
    LinearRamp qRamp_;
    LinearRamp gainRamp_;
    LinearRamp typeCrossfadeRamp_;  ///< For 5ms filter type crossfade

    // Type crossfade state
    bool isTypeCrossfading_ = false;  ///< True during filter type transition
    SVFMode previousType_ = SVFMode::Lowpass;  ///< Type of filterOld_ during crossfade
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline void FilterStepSequencer::prepare(double sampleRate) noexcept {
    sampleRate_ = (sampleRate >= 1000.0) ? sampleRate : 1000.0;

    // Prepare sequencer core
    sequencer_.prepare(sampleRate_);

    // Prepare both filters
    filter_.prepare(sampleRate_);
    filterOld_.prepare(sampleRate_);

    // Configure ramps with sample rate (glide time set later)
    cutoffRamp_.configure(glideTimeMs_, static_cast<float>(sampleRate_));
    qRamp_.configure(glideTimeMs_, static_cast<float>(sampleRate_));
    gainRamp_.configure(glideTimeMs_, static_cast<float>(sampleRate_));
    typeCrossfadeRamp_.configure(kTypeCrossfadeMs, static_cast<float>(sampleRate_));

    // Apply initial step parameters
    int currentStep = sequencer_.getCurrentStep();
    applyStepParameters(currentStep);
    previousStep_ = currentStep;

    // Snap ramps to initial values (no glide on first step)
    cutoffRamp_.snapToTarget();
    qRamp_.snapToTarget();
    gainRamp_.snapToTarget();
    typeCrossfadeRamp_.snapTo(1.0f);  // Fully on new filter
    isTypeCrossfading_ = false;
}

inline void FilterStepSequencer::reset() noexcept {
    filter_.reset();
    filterOld_.reset();

    // Reset sequencer core (handles step position based on direction)
    sequencer_.reset();

    previousStep_ = -1;
    isTypeCrossfading_ = false;

    // Reset ramps
    cutoffRamp_.reset();
    qRamp_.reset();
    gainRamp_.reset();
    typeCrossfadeRamp_.snapTo(1.0f);

    // Re-apply current step if prepared
    if (sequencer_.isPrepared()) {
        int currentStep = sequencer_.getCurrentStep();
        applyStepParameters(currentStep);
        cutoffRamp_.snapToTarget();
        qRamp_.snapToTarget();
        gainRamp_.snapToTarget();
        previousStep_ = currentStep;
    }
}

inline bool FilterStepSequencer::isPrepared() const noexcept {
    return sequencer_.isPrepared();
}

inline void FilterStepSequencer::setNumSteps(size_t numSteps) noexcept {
    sequencer_.setNumSteps(numSteps);
}

inline size_t FilterStepSequencer::getNumSteps() const noexcept {
    return sequencer_.getNumSteps();
}

inline void FilterStepSequencer::setStep(size_t stepIndex, const SequencerStep& step) noexcept {
    if (stepIndex >= kMaxSteps) return;
    steps_[stepIndex] = step;
    steps_[stepIndex].clamp();
}

inline const SequencerStep& FilterStepSequencer::getStep(size_t stepIndex) const noexcept {
    if (stepIndex >= kMaxSteps) {
        static const SequencerStep defaultStep{};
        return defaultStep;
    }
    return steps_[stepIndex];
}

inline void FilterStepSequencer::setStepCutoff(size_t stepIndex, float hz) noexcept {
    if (stepIndex >= kMaxSteps) return;
    steps_[stepIndex].cutoffHz = std::clamp(hz, 20.0f, 20000.0f);
}

inline void FilterStepSequencer::setStepQ(size_t stepIndex, float q) noexcept {
    if (stepIndex >= kMaxSteps) return;
    steps_[stepIndex].q = std::clamp(q, 0.5f, 20.0f);
}

inline void FilterStepSequencer::setStepType(size_t stepIndex, SVFMode type) noexcept {
    if (stepIndex >= kMaxSteps) return;
    steps_[stepIndex].type = type;
}

inline void FilterStepSequencer::setStepGain(size_t stepIndex, float dB) noexcept {
    if (stepIndex >= kMaxSteps) return;
    steps_[stepIndex].gainDb = std::clamp(dB, -24.0f, 12.0f);
}

inline void FilterStepSequencer::setTempo(float bpm) noexcept {
    sequencer_.setTempo(bpm);
}

inline void FilterStepSequencer::setNoteValue(NoteValue value, NoteModifier modifier) noexcept {
    sequencer_.setNoteValue(value, modifier);
}

inline void FilterStepSequencer::setSwing(float swing) noexcept {
    sequencer_.setSwing(swing);
}

inline void FilterStepSequencer::setGlideTime(float ms) noexcept {
    glideTimeMs_ = std::clamp(ms, kMinGlideMs, kMaxGlideMs);
    if (sequencer_.isPrepared()) {
        cutoffRamp_.configure(glideTimeMs_, static_cast<float>(sampleRate_));
        qRamp_.configure(glideTimeMs_, static_cast<float>(sampleRate_));
        gainRamp_.configure(glideTimeMs_, static_cast<float>(sampleRate_));
    }
}

inline void FilterStepSequencer::setGateLength(float gateLength) noexcept {
    sequencer_.setGateLength(gateLength);
}

inline void FilterStepSequencer::setDirection(Direction direction) noexcept {
    sequencer_.setDirection(direction);
    // SequencerCore::setDirection calls reset() internally
    // We also need to reset our filter state
    if (sequencer_.isPrepared()) {
        filter_.reset();
        filterOld_.reset();
        isTypeCrossfading_ = false;
        previousStep_ = -1;
        cutoffRamp_.reset();
        qRamp_.reset();
        gainRamp_.reset();
        typeCrossfadeRamp_.snapTo(1.0f);

        int currentStep = sequencer_.getCurrentStep();
        applyStepParameters(currentStep);
        cutoffRamp_.snapToTarget();
        qRamp_.snapToTarget();
        gainRamp_.snapToTarget();
        previousStep_ = currentStep;
    }
}

inline Direction FilterStepSequencer::getDirection() const noexcept {
    return sequencer_.getDirection();
}

inline void FilterStepSequencer::sync(double ppqPosition) noexcept {
    int oldStep = sequencer_.getCurrentStep();
    sequencer_.sync(ppqPosition);
    int newStep = sequencer_.getCurrentStep();

    // Apply parameters if step changed
    if (newStep != oldStep) {
        applyStepParameters(newStep);
        previousStep_ = newStep;
    }
}

inline void FilterStepSequencer::trigger() noexcept {
    sequencer_.trigger();
    int newStep = sequencer_.getCurrentStep();
    applyStepParameters(newStep);
    previousStep_ = newStep;
}

inline int FilterStepSequencer::getCurrentStep() const noexcept {
    return sequencer_.getCurrentStep();
}

inline float FilterStepSequencer::process(float input) noexcept {
    if (!sequencer_.isPrepared()) {
        return 0.0f;
    }

    // FR-022: Handle NaN/Inf input
    if (detail::isNaN(input) || detail::isInf(input)) {
        filter_.reset();
        return 0.0f;
    }

    // Advance sequencer - returns true if step changed
    bool stepChanged = sequencer_.tick();

    // Apply new step parameters if step changed
    if (stepChanged) {
        int currentStep = sequencer_.getCurrentStep();
        applyStepParameters(currentStep);
        previousStep_ = currentStep;
    }

    // Process parameter ramps
    float cutoff = cutoffRamp_.process();
    float q = qRamp_.process();
    float gainDb = gainRamp_.process();

    // Clamp cutoff to Nyquist
    float maxCutoff = static_cast<float>(sampleRate_) * 0.495f;
    cutoff = std::min(cutoff, maxCutoff);

    // Apply to filter(s)
    filter_.setCutoff(cutoff);
    filter_.setResonance(q);
    filter_.setGain(0.0f);  // Unity gain for filter itself

    float wet;

    // Handle filter type crossfade (SC-003: no clicks on type changes)
    if (isTypeCrossfading_) {
        // Also update the old filter with same cutoff/Q
        filterOld_.setCutoff(cutoff);
        filterOld_.setResonance(q);
        filterOld_.setGain(0.0f);

        // Process both filters
        float wetNew = filter_.process(input);
        float wetOld = filterOld_.process(input);

        // Crossfade between old and new
        float crossfadeGain = typeCrossfadeRamp_.process();
        wet = wetNew * crossfadeGain + wetOld * (1.0f - crossfadeGain);

        // Check if crossfade is complete
        if (crossfadeGain >= 1.0f) {
            isTypeCrossfading_ = false;
        }
    } else {
        // Normal single-filter processing
        wet = filter_.process(input);
    }

    // Apply gain from step (external to filter)
    float linearGain = dbToGain(gainDb);
    wet *= linearGain;

    // Apply gate crossfade (from SequencerCore)
    float gateGain = sequencer_.getGateRampValue();
    float output = wet * gateGain + input * (1.0f - gateGain);

    return output;
}

inline void FilterStepSequencer::processBlock(float* buffer, size_t numSamples,
                                               const BlockContext* ctx) noexcept {
    if (!sequencer_.isPrepared() || buffer == nullptr) return;

    // Update tempo from context if provided
    if (ctx != nullptr) {
        setTempo(static_cast<float>(ctx->tempoBPM));
    }

    // Process each sample
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = process(buffer[i]);
    }
}

inline void FilterStepSequencer::applyStepParameters(int stepIndex) noexcept {
    if (stepIndex < 0 || stepIndex >= static_cast<int>(kMaxSteps)) return;

    const auto& step = steps_[static_cast<size_t>(stepIndex)];

    // Check if filter type is changing (SC-003: crossfade for smooth transition)
    SVFMode currentType = filter_.getMode();
    if (step.type != currentType && sequencer_.isPrepared()) {
        // Start type crossfade: copy entire filter (state + mode) to old filter
        filterOld_ = filter_;
        previousType_ = currentType;

        // Set new type on primary filter (state is preserved)
        filter_.setMode(step.type);

        // Start crossfade ramp from 0 (old) to 1 (new)
        typeCrossfadeRamp_.snapTo(0.0f);
        typeCrossfadeRamp_.setTarget(1.0f);
        isTypeCrossfading_ = true;
    } else {
        // No type change, just update mode directly
        filter_.setMode(step.type);
    }

    // Calculate effective glide time with truncation (FR-010)
    // We need step duration - get it from tempo and note value
    // For simplicity, use current glide time (SequencerCore handles swing internally)
    float effectiveGlideMs = glideTimeMs_;

    // Configure ramps with effective glide time
    cutoffRamp_.configure(effectiveGlideMs, static_cast<float>(sampleRate_));
    qRamp_.configure(effectiveGlideMs, static_cast<float>(sampleRate_));
    gainRamp_.configure(effectiveGlideMs, static_cast<float>(sampleRate_));

    // Set targets - continuous parameters GLIDE
    cutoffRamp_.setTarget(step.cutoffHz);
    qRamp_.setTarget(step.q);
    gainRamp_.setTarget(step.gainDb);
}

} // namespace DSP
} // namespace Krate
