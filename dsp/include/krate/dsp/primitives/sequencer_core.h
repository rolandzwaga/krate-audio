// ==============================================================================
// Layer 1: DSP Primitives
// sequencer_core.h - Reusable Timing Engine for Step Sequencers
// ==============================================================================
// API Contract for specs/099-vowel-sequencer
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle X: DSP Constraints (flush denormals, handle edge cases)
// - Principle XII: Test-First Development
//
// Dependencies (Layer 0):
// - NoteValue/NoteModifier (Layer 0): Tempo sync timing
// - LinearRamp (Layer 1): Gate crossfade smoothing
// ==============================================================================

#pragma once

#include <krate/dsp/core/note_value.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Direction Enumeration
// =============================================================================

/// @brief Playback direction for step sequencers
///
/// Defines how the sequencer advances through steps.
enum class Direction : uint8_t {
    Forward = 0,    ///< Sequential: 0, 1, 2, ..., N-1, 0, 1, ...
    Backward,       ///< Reverse: N-1, N-2, ..., 0, N-1, ...
    PingPong,       ///< Bounce: 0, 1, ..., N-1, N-2, ..., 1, 0, 1, ...
    Random          ///< Random with no immediate repeat
};

// =============================================================================
// SequencerCore Class
// =============================================================================

/// @brief Reusable timing engine for step sequencers (Layer 1 Primitive)
///
/// Provides tempo-synchronized step timing, direction control, swing, and
/// gate length for rhythmic effects. Consumers (FilterStepSequencer,
/// VowelSequencer) compose this class and handle their own parameter
/// interpolation based on step change events.
///
/// @par Real-Time Safety
/// All tick() and state query methods are noexcept and allocation-free.
///
/// @par Usage Pattern
/// @code
/// SequencerCore core;
/// core.prepare(44100.0);
/// core.setNumSteps(8);
/// core.setTempo(120.0f);
/// core.setNoteValue(NoteValue::Eighth);
///
/// // In process loop:
/// if (core.tick()) {
///     // Step changed! Query new step and update parameters
///     int step = core.getCurrentStep();
///     applyStepParameters(step);
/// }
/// bool gateOn = core.isGateActive();
/// float gateValue = core.getGateRampValue();
/// @endcode
///
/// @see FilterStepSequencer, VowelSequencer
class SequencerCore {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Maximum number of steps supported
    static constexpr size_t kMaxSteps = 16;

    /// Tempo range in BPM
    static constexpr float kMinTempoBPM = 20.0f;
    static constexpr float kMaxTempoBPM = 300.0f;

    /// Swing range [0.0 = no swing, 1.0 = max swing]
    static constexpr float kMinSwing = 0.0f;
    static constexpr float kMaxSwing = 1.0f;

    /// Gate length range [0.0 = no gate, 1.0 = full step]
    static constexpr float kMinGateLength = 0.0f;
    static constexpr float kMaxGateLength = 1.0f;

    /// Gate crossfade time in milliseconds
    static constexpr float kGateCrossfadeMs = 5.0f;

    /// Minimum sample rate (Hz)
    static constexpr double kMinSampleRate = 1000.0;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor
    SequencerCore() noexcept = default;

    /// @brief Prepare for processing at given sample rate
    /// @param sampleRate Sample rate in Hz (clamped to >= 1000)
    /// @post isPrepared() returns true
    void prepare(double sampleRate) noexcept;

    /// @brief Reset playback state to initial position
    /// @pre isPrepared() returns true
    /// @post getCurrentStep() returns 0 (or N-1 for Backward)
    /// @post Internal counters reset, configuration preserved
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

    // =========================================================================
    // Timing Configuration
    // =========================================================================

    /// @brief Set tempo in beats per minute
    /// @param bpm Tempo [kMinTempoBPM, kMaxTempoBPM], clamped
    void setTempo(float bpm) noexcept;

    /// @brief Set note value for step timing
    /// @param value Note value (Whole, Half, Quarter, Eighth, etc.)
    /// @param modifier Optional modifier (None, Dotted, Triplet)
    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;

    /// @brief Set swing amount
    /// @param swing Swing [0.0, 1.0], clamped
    ///              0.0 = no swing (equal timing)
    ///              0.5 = 3:1 ratio (even steps 1.5x, odd steps 0.5x)
    ///              1.0 = maximum swing
    /// @note Swing applies to step indices (even=long, odd=short) regardless
    ///       of playback direction.
    void setSwing(float swing) noexcept;

    /// @brief Set gate length as fraction of step duration
    /// @param gateLength Gate length [0.0, 1.0], clamped
    ///                   0.0 = instant gate off
    ///                   1.0 = gate on for entire step
    void setGateLength(float gateLength) noexcept;

    // =========================================================================
    // Direction Configuration
    // =========================================================================

    /// @brief Set playback direction
    /// @param direction Direction mode
    void setDirection(Direction direction) noexcept;

    /// @brief Get current playback direction
    /// @return Current direction mode
    [[nodiscard]] Direction getDirection() const noexcept;

    // =========================================================================
    // Transport Control
    // =========================================================================

    /// @brief Sync to DAW transport position
    /// @param ppqPosition Position in quarter notes (PPQ)
    ///
    /// Calculates correct step based on position, accounting for note value
    /// and direction. For PingPong, correctly handles bounce position.
    ///
    /// @par SC-008 Sync Accuracy
    /// Step position matches PPQ within 1 sample after sync() returns.
    void sync(double ppqPosition) noexcept;

    /// @brief Manually advance to next step
    ///
    /// Advances step using current direction rules. Useful for external
    /// triggering (MIDI notes, etc.).
    void trigger() noexcept;

    /// @brief Get current step index
    /// @return Step index [0, numSteps-1]
    [[nodiscard]] int getCurrentStep() const noexcept;

    // =========================================================================
    // Per-Sample Processing
    // =========================================================================

    /// @brief Advance sequencer by one sample
    /// @return true if step changed this sample
    /// @pre isPrepared() returns true
    ///
    /// Call once per sample. When true is returned, query getCurrentStep()
    /// to get the new step index and update consumer parameters.
    ///
    /// @par SC-001 Timing Accuracy
    /// Step changes occur within 1ms (44 samples @ 44.1kHz) of expected time.
    [[nodiscard]] bool tick() noexcept;

    /// @brief Check if gate is currently active
    /// @return true if gate is on
    ///
    /// Gate is active for (gateLength * stepDuration) samples at the start
    /// of each step, then off for remaining samples.
    [[nodiscard]] bool isGateActive() const noexcept;

    /// @brief Get gate ramp value for crossfade
    /// @return Gate value [0.0, 1.0] with 5ms ramp
    ///
    /// Use for click-free gate transitions:
    /// @code
    /// float gateValue = core.getGateRampValue();
    /// output = wetSignal * gateValue + drySignal * (1.0f - gateValue);
    /// @endcode
    [[nodiscard]] float getGateRampValue() noexcept;

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Update step duration based on tempo and note value
    void updateStepDuration() noexcept;

    /// @brief Advance to next step based on direction
    void advanceStep() noexcept;

    /// @brief Calculate next step index based on direction
    [[nodiscard]] int calculateNextStep() noexcept;

    /// @brief Apply swing timing to step
    [[nodiscard]] float applySwingToStep(int stepIndex, float baseDuration) const noexcept;

    /// @brief Calculate PingPong step from pattern position
    [[nodiscard]] int calculatePingPongStep(double stepsIntoPattern) const noexcept;

    // =========================================================================
    // Member Variables
    // =========================================================================

    // State
    bool prepared_ = false;
    double sampleRate_ = 44100.0;

    // Step configuration
    size_t numSteps_ = 4;

    // Timing
    float tempoBPM_ = 120.0f;
    NoteValue noteValue_ = NoteValue::Eighth;
    NoteModifier noteModifier_ = NoteModifier::None;
    float swing_ = 0.0f;
    float gateLength_ = 1.0f;

    // Direction
    Direction direction_ = Direction::Forward;
    bool pingPongForward_ = true;
    uint32_t rngState_ = 12345;  // xorshift PRNG

    // Processing state
    int currentStep_ = 0;
    size_t sampleCounter_ = 0;
    size_t stepDurationSamples_ = 0;
    size_t gateDurationSamples_ = 0;
    bool gateActive_ = true;

    // Gate ramp (5ms crossfade)
    LinearRamp gateRamp_;
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline void SequencerCore::prepare(double sampleRate) noexcept {
    sampleRate_ = (sampleRate >= kMinSampleRate) ? sampleRate : kMinSampleRate;
    prepared_ = true;

    // Configure gate ramp
    gateRamp_.configure(kGateCrossfadeMs, static_cast<float>(sampleRate_));
    gateRamp_.snapTo(1.0f);  // Gate starts active

    // Initialize step duration
    updateStepDuration();
}

inline void SequencerCore::reset() noexcept {
    // Set initial step based on direction
    switch (direction_) {
        case Direction::Forward:
        case Direction::PingPong:
        case Direction::Random:
            currentStep_ = 0;
            pingPongForward_ = true;
            break;
        case Direction::Backward:
            currentStep_ = static_cast<int>(numSteps_) - 1;
            break;
    }

    sampleCounter_ = 0;
    gateActive_ = true;

    // Reset gate ramp
    gateRamp_.snapTo(1.0f);

    // Recalculate step duration
    if (prepared_) {
        updateStepDuration();
    }
}

inline bool SequencerCore::isPrepared() const noexcept {
    return prepared_;
}

inline void SequencerCore::setNumSteps(size_t numSteps) noexcept {
    numSteps_ = std::clamp(numSteps, size_t{1}, kMaxSteps);
    // Ensure current step is within bounds
    if (currentStep_ >= static_cast<int>(numSteps_)) {
        currentStep_ = 0;
    }
}

inline size_t SequencerCore::getNumSteps() const noexcept {
    return numSteps_;
}

inline void SequencerCore::setTempo(float bpm) noexcept {
    tempoBPM_ = std::clamp(bpm, kMinTempoBPM, kMaxTempoBPM);
    if (prepared_) {
        updateStepDuration();
    }
}

inline void SequencerCore::setNoteValue(NoteValue value, NoteModifier modifier) noexcept {
    noteValue_ = value;
    noteModifier_ = modifier;
    if (prepared_) {
        updateStepDuration();
    }
}

inline void SequencerCore::setSwing(float swing) noexcept {
    swing_ = std::clamp(swing, kMinSwing, kMaxSwing);
}

inline void SequencerCore::setGateLength(float gateLength) noexcept {
    gateLength_ = std::clamp(gateLength, kMinGateLength, kMaxGateLength);
}

inline void SequencerCore::setDirection(Direction direction) noexcept {
    direction_ = direction;
    // Reset to appropriate starting step when direction changes
    if (prepared_) {
        reset();
    }
}

inline Direction SequencerCore::getDirection() const noexcept {
    return direction_;
}

inline void SequencerCore::sync(double ppqPosition) noexcept {
    if (!prepared_ || numSteps_ == 0) return;

    // Calculate beats per step based on note value
    float beatsPerStep = getBeatsForNote(noteValue_, noteModifier_);

    // Calculate which step we should be at
    double stepsIntoPattern = ppqPosition / static_cast<double>(beatsPerStep);

    // Handle direction
    int effectiveStep = 0;
    switch (direction_) {
        case Direction::Forward:
            effectiveStep = static_cast<int>(std::fmod(stepsIntoPattern, static_cast<double>(numSteps_)));
            if (effectiveStep < 0) effectiveStep += static_cast<int>(numSteps_);
            break;
        case Direction::Backward:
            effectiveStep = static_cast<int>(numSteps_) - 1 -
                (static_cast<int>(std::fmod(stepsIntoPattern, static_cast<double>(numSteps_))));
            if (effectiveStep < 0) effectiveStep += static_cast<int>(numSteps_);
            break;
        case Direction::PingPong:
            effectiveStep = calculatePingPongStep(stepsIntoPattern);
            break;
        case Direction::Random:
            // Can't sync random - keep current step
            effectiveStep = currentStep_;
            break;
    }

    // Calculate phase within current step
    double fractionalStep = std::fmod(stepsIntoPattern, 1.0);
    if (fractionalStep < 0.0) fractionalStep += 1.0;

    // Update sample counter based on phase
    float swungDuration = applySwingToStep(effectiveStep, static_cast<float>(stepDurationSamples_));
    sampleCounter_ = static_cast<size_t>(fractionalStep * static_cast<double>(swungDuration));

    // Update current step
    currentStep_ = effectiveStep;
}

inline void SequencerCore::trigger() noexcept {
    advanceStep();
}

inline int SequencerCore::getCurrentStep() const noexcept {
    return currentStep_;
}

inline bool SequencerCore::tick() noexcept {
    if (!prepared_) return false;

    bool stepChanged = false;

    // Calculate step duration with swing
    float swungStepDuration = applySwingToStep(currentStep_, static_cast<float>(stepDurationSamples_));
    size_t actualStepDuration = static_cast<size_t>(swungStepDuration);

    // Check for step boundary
    if (sampleCounter_ >= actualStepDuration) {
        advanceStep();
        sampleCounter_ = 0;
        stepChanged = true;
    }

    // Update gate state
    gateDurationSamples_ = static_cast<size_t>(static_cast<float>(actualStepDuration) * gateLength_);
    bool shouldBeActive = (sampleCounter_ < gateDurationSamples_);

    // Handle gate transitions with crossfade
    if (shouldBeActive && !gateActive_) {
        gateActive_ = true;
        gateRamp_.setTarget(1.0f);
    } else if (!shouldBeActive && gateActive_) {
        gateActive_ = false;
        gateRamp_.setTarget(0.0f);
    }

    // Increment counter
    ++sampleCounter_;

    return stepChanged;
}

inline bool SequencerCore::isGateActive() const noexcept {
    return gateActive_;
}

inline float SequencerCore::getGateRampValue() noexcept {
    return gateRamp_.process();
}

inline void SequencerCore::updateStepDuration() noexcept {
    // Calculate milliseconds per beat
    float msPerBeat = 60000.0f / tempoBPM_;

    // Get beats per step from note value
    float beatsPerStep = getBeatsForNote(noteValue_, noteModifier_);

    // Calculate base step duration in samples
    float stepMs = msPerBeat * beatsPerStep;
    stepDurationSamples_ = static_cast<size_t>(stepMs * 0.001f * sampleRate_);

    // Ensure at least 1 sample
    if (stepDurationSamples_ == 0) {
        stepDurationSamples_ = 1;
    }
}

inline void SequencerCore::advanceStep() noexcept {
    currentStep_ = calculateNextStep();
    gateActive_ = true;
    gateRamp_.setTarget(1.0f);
}

inline int SequencerCore::calculateNextStep() noexcept {
    int next = 0;

    switch (direction_) {
        case Direction::Forward:
            next = (currentStep_ + 1) % static_cast<int>(numSteps_);
            break;

        case Direction::Backward:
            next = currentStep_ - 1;
            if (next < 0) {
                next = static_cast<int>(numSteps_) - 1;
            }
            break;

        case Direction::PingPong:
            if (numSteps_ <= 1) {
                next = 0;
            } else if (pingPongForward_) {
                next = currentStep_ + 1;
                if (next >= static_cast<int>(numSteps_) - 1) {
                    next = static_cast<int>(numSteps_) - 1;
                    pingPongForward_ = false;
                }
            } else {
                next = currentStep_ - 1;
                if (next <= 0) {
                    next = 0;
                    pingPongForward_ = true;
                }
            }
            break;

        case Direction::Random:
            if (numSteps_ <= 1) {
                next = 0;
            } else {
                // Rejection sampling with xorshift PRNG
                do {
                    rngState_ ^= rngState_ << 13;
                    rngState_ ^= rngState_ >> 17;
                    rngState_ ^= rngState_ << 5;
                    next = static_cast<int>(rngState_ % static_cast<uint32_t>(numSteps_));
                } while (next == currentStep_);
            }
            break;
    }

    return next;
}

inline float SequencerCore::applySwingToStep(int stepIndex, float baseDuration) const noexcept {
    if (swing_ <= 0.0f) return baseDuration;

    // Swing affects step pairs: even steps get longer, odd steps get shorter
    // Formula: ratio = (1+swing)/(1-swing) at swing=0.5 gives 3:1
    bool isOddStep = (stepIndex % 2 == 1);

    if (isOddStep) {
        // Odd step gets shorter: multiply by (1 - swing)
        return baseDuration * (1.0f - swing_);
    } else {
        // Even step gets longer: multiply by (1 + swing)
        return baseDuration * (1.0f + swing_);
    }
}

inline int SequencerCore::calculatePingPongStep(double stepsIntoPattern) const noexcept {
    if (numSteps_ <= 1) return 0;

    // PingPong cycle length: 2 * (N - 1) for N steps
    // Pattern: 0,1,2,3,2,1,0,1,2,3,2,1...
    int cycleLength = 2 * (static_cast<int>(numSteps_) - 1);
    int posInCycle = static_cast<int>(std::fmod(stepsIntoPattern, static_cast<double>(cycleLength)));
    if (posInCycle < 0) posInCycle += cycleLength;

    // First half: ascending (0 to N-1)
    // Second half: descending (N-2 to 1)
    if (posInCycle < static_cast<int>(numSteps_)) {
        return posInCycle;
    } else {
        // Mirror back: position N maps to N-2, N+1 maps to N-3, etc.
        return cycleLength - posInCycle;
    }
}

} // namespace DSP
} // namespace Krate
