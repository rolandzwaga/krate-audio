// ==============================================================================
// Layer 2: Processor - Pattern Scheduler
// ==============================================================================
// Tempo-synced pattern sequencer for Pattern Freeze Mode.
//
// Advances through a pattern bitmask at a rate determined by tempo and note
// value, invoking a callback when pattern steps are hit. Used to trigger
// slice playback in various rhythmic patterns (Euclidean, random, etc.).
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept process, no RT allocations)
// - Principle III: Modern C++ (RAII, value semantics, C++20, std::function)
// - Principle IX: Layer 2 (depends on Layer 0-1)
// - Principle XII: Test-First Development
//
// Reference: specs/069-pattern-freeze/data-model.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/euclidean_pattern.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/core/pattern_freeze_types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace Krate::DSP {

/// @brief Tempo-synced pattern sequencer for triggering slices
///
/// Advances through a rhythmic pattern at tempo-synced intervals, invoking
/// a callback on each hit. Supports Euclidean patterns and arbitrary bitmasks.
///
/// @note All processing methods are noexcept for real-time safety.
/// @note Callback is stored by value (std::function), so capture carefully.
///
/// @example
/// @code
/// PatternScheduler scheduler;
/// scheduler.prepare(44100.0, 512);
/// scheduler.setEuclidean(3, 8, 0);  // Tresillo pattern
/// scheduler.setTempoSync(true, NoteValue::Sixteenth, NoteModifier::None);
/// scheduler.setTriggerCallback([this](int step) {
///     triggerSliceAtStep(step);
/// });
///
/// // In process callback:
/// scheduler.process(numSamples, ctx);
/// @endcode
class PatternScheduler {
public:
    /// @brief Callback type for step triggers
    using TriggerCallback = std::function<void(int step)>;

    /// @brief Default constructor
    PatternScheduler() noexcept = default;

    /// @brief Destructor
    ~PatternScheduler() = default;

    // Non-copyable, movable
    PatternScheduler(const PatternScheduler&) = delete;
    PatternScheduler& operator=(const PatternScheduler&) = delete;
    PatternScheduler(PatternScheduler&&) noexcept = default;
    PatternScheduler& operator=(PatternScheduler&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods
    // =========================================================================

    /// @brief Prepare scheduler for processing
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum samples per process call
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;

        // Initialize with default pattern
        setEuclidean(PatternFreezeConstants::kDefaultEuclideanHits,
                     PatternFreezeConstants::kDefaultEuclideanSteps,
                     PatternFreezeConstants::kDefaultEuclideanRotation);

        // Default step duration (1/8 note at 120 BPM = 250ms = 11025 samples at 44.1kHz)
        stepDurationSamples_ = static_cast<size_t>(sampleRate_ * 0.25);

        reset();
    }

    /// @brief Reset scheduler state
    void reset() noexcept {
        currentStep_ = 0;
        sampleCounter_ = 0;
        stepTriggered_ = false;
    }

    // =========================================================================
    // Pattern Configuration
    // =========================================================================

    /// @brief Set pattern from raw bitmask
    ///
    /// @param pattern Pattern bitmask (bit i = step i is hit)
    /// @param steps Total number of steps in pattern
    void setPattern(uint32_t pattern, int steps) noexcept {
        pattern_ = pattern;
        steps_ = std::clamp(steps, EuclideanPattern::kMinSteps,
                           EuclideanPattern::kMaxSteps);
        // Ensure current step is valid
        if (currentStep_ >= steps_) {
            currentStep_ = 0;
        }
    }

    /// @brief Set Euclidean pattern from parameters
    ///
    /// @param hits Number of hits/pulses
    /// @param steps Number of steps
    /// @param rotation Pattern rotation
    void setEuclidean(int hits, int steps, int rotation = 0) noexcept {
        steps_ = std::clamp(steps, EuclideanPattern::kMinSteps,
                           EuclideanPattern::kMaxSteps);
        hits = std::clamp(hits, 0, steps_);
        pattern_ = EuclideanPattern::generate(hits, steps_, rotation);

        if (currentStep_ >= steps_) {
            currentStep_ = 0;
        }
    }

    /// @brief Get current pattern bitmask
    [[nodiscard]] uint32_t getPattern() const noexcept { return pattern_; }

    /// @brief Get number of steps in pattern
    [[nodiscard]] int getSteps() const noexcept { return steps_; }

    /// @brief Get current step position
    [[nodiscard]] int getCurrentStep() const noexcept { return currentStep_; }

    // =========================================================================
    // Timing Configuration
    // =========================================================================

    /// @brief Set step duration directly in samples
    ///
    /// @param samples Number of samples per step
    void setStepDuration(size_t samples) noexcept {
        stepDurationSamples_ = std::max(size_t{1}, samples);
        tempoSync_ = false;
    }

    /// @brief Set tempo-synced step duration
    ///
    /// @param enabled Enable tempo sync
    /// @param noteValue Note value for step duration
    /// @param modifier Note modifier (dotted, triplet)
    void setTempoSync(bool enabled, NoteValue noteValue = NoteValue::Sixteenth,
                      NoteModifier modifier = NoteModifier::None) noexcept {
        tempoSync_ = enabled;
        noteValue_ = noteValue;
        noteModifier_ = modifier;
    }

    // =========================================================================
    // Callback Configuration
    // =========================================================================

    /// @brief Set callback invoked when a pattern hit occurs
    ///
    /// @param callback Function called with step index on each hit
    void setTriggerCallback(TriggerCallback callback) noexcept {
        triggerCallback_ = std::move(callback);
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a block of samples, advancing through pattern
    ///
    /// @param numSamples Number of samples to process
    /// @param ctx Block context with tempo information
    void process(size_t numSamples, const BlockContext& ctx) noexcept {
        if (numSamples == 0) {
            return;
        }

        // Update step duration if tempo-synced
        if (tempoSync_) {
            updateStepDurationFromTempo(ctx);
        }

        // Process samples, advancing through pattern
        size_t samplesRemaining = numSamples;
        while (samplesRemaining > 0) {
            // Check if we need to trigger at current step
            if (!stepTriggered_) {
                checkAndTrigger();
                stepTriggered_ = true;
            }

            // Calculate samples until next step
            size_t samplesToNextStep = stepDurationSamples_ - sampleCounter_;
            size_t samplesToProcess = std::min(samplesRemaining, samplesToNextStep);

            sampleCounter_ += samplesToProcess;
            samplesRemaining -= samplesToProcess;

            // Advance to next step if reached
            if (sampleCounter_ >= stepDurationSamples_) {
                advanceStep();
            }
        }
    }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Update step duration based on tempo
    void updateStepDurationFromTempo(const BlockContext& ctx) noexcept {
        const float beatsPerStep = getBeatsForNote(noteValue_, noteModifier_);
        const double msPerBeat = 60000.0 / std::max(ctx.tempoBPM, 20.0);
        const double msPerStep = msPerBeat * static_cast<double>(beatsPerStep);
        stepDurationSamples_ = static_cast<size_t>(
            sampleRate_ * msPerStep / 1000.0);
        stepDurationSamples_ = std::max(size_t{1}, stepDurationSamples_);
    }

    /// @brief Check current step and invoke callback if hit
    void checkAndTrigger() noexcept {
        if (EuclideanPattern::isHit(pattern_, currentStep_, steps_)) {
            if (triggerCallback_) {
                triggerCallback_(currentStep_);
            }
        }
    }

    /// @brief Advance to next step in pattern
    void advanceStep() noexcept {
        currentStep_ = (currentStep_ + 1) % steps_;
        sampleCounter_ = 0;
        stepTriggered_ = false;
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Configuration
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;

    // Pattern
    uint32_t pattern_ = 0;
    int steps_ = PatternFreezeConstants::kDefaultEuclideanSteps;

    // Timing
    bool tempoSync_ = false;
    NoteValue noteValue_ = NoteValue::Sixteenth;
    NoteModifier noteModifier_ = NoteModifier::None;
    size_t stepDurationSamples_ = 11025;  // Default ~250ms at 44.1kHz

    // State
    int currentStep_ = 0;
    size_t sampleCounter_ = 0;
    bool stepTriggered_ = false;

    // Callback
    TriggerCallback triggerCallback_;
};

}  // namespace Krate::DSP
