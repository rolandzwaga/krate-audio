// ==============================================================================
// SequencerCore API Contract
// ==============================================================================
// Layer 1 Primitive - Reusable timing engine for step sequencers
//
// This file defines the public API contract for SequencerCore. Implementation
// will be in dsp/include/krate/dsp/primitives/sequencer_core.h
// ==============================================================================

#pragma once

#include <krate/dsp/core/note_value.h>
#include <krate/dsp/primitives/smoother.h>  // LinearRamp

#include <array>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

/// @brief Playback direction for step sequencers
///
/// Defines how the sequencer advances through steps.
enum class Direction : uint8_t {
    Forward = 0,    ///< Sequential: 0, 1, 2, ..., N-1, 0, 1, ...
    Backward,       ///< Reverse: N-1, N-2, ..., 0, N-1, ...
    PingPong,       ///< Bounce: 0, 1, ..., N-1, N-2, ..., 1, 0, 1, ...
    Random          ///< Random with no immediate repeat
};

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
    /// @post getCurrentStep() returns 0
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

    // Private methods
    void updateStepDuration() noexcept;
    void advanceStep() noexcept;
    [[nodiscard]] int calculateNextStep() noexcept;
    [[nodiscard]] float applySwingToStep(int stepIndex, float baseDuration) const noexcept;
    [[nodiscard]] int calculatePingPongStep(double stepsIntoPattern) const noexcept;
};

} // namespace Krate::DSP
