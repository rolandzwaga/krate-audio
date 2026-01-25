// ==============================================================================
// Layer 2: DSP Processors
// multistage_env_filter.h - Multi-Stage Envelope Filter
// ==============================================================================
// API Contract for specs/100-multistage-env-filter
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (RAII, value semantics)
// - Principle IX: Layer 2 (depends only on Layer 0-1)
// - Principle X: DSP Constraints (flush denormals, handle edge cases)
// - Principle XII: Test-First Development
//
// CONGRATULATIONS! SPEC #100!
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/svf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// EnvelopeState Enumeration (FR-026, FR-027)
// =============================================================================

/// @brief Envelope generator state machine states
enum class EnvelopeState : uint8_t {
    Idle,      ///< Not triggered, sitting at baseFrequency
    Running,   ///< Transitioning through stages
    Releasing, ///< Decaying to baseFrequency after release()
    Complete   ///< Finished (non-looping) or waiting for retrigger
};

// =============================================================================
// EnvelopeStage Structure
// =============================================================================

/// @brief Configuration for a single envelope stage
///
/// Each stage defines a target frequency, transition time, and curve shape.
/// The envelope transitions from the previous stage's target (or baseFrequency
/// for stage 0) to this stage's target using the specified time and curve.
struct EnvelopeStage {
    float targetHz = 1000.0f; ///< Target cutoff frequency [1, sampleRate*0.45] Hz
    float timeMs = 100.0f;    ///< Transition time [0, 10000] ms (0 = instant)
    float curve = 0.0f;       ///< Curve shape [-1 (log), 0 (linear), +1 (exp)]
};

// =============================================================================
// MultiStageEnvelopeFilter Class (FR-001 through FR-031)
// =============================================================================

/// @brief Multi-stage envelope filter with programmable curve shapes
///
/// Provides complex envelope shapes beyond ADSR driving filter movement
/// for evolving pads and textures. Supports up to 8 stages with independent
/// target frequency, transition time, and curve shape.
///
/// @par Features
/// - Up to 8 programmable stages with target, time, and curve
/// - Logarithmic, linear, and exponential curve shapes
/// - Loopable envelope section for rhythmic patterns
/// - Velocity-sensitive modulation depth
/// - Independent release time
///
/// @par Layer
/// Layer 2 (DSP Processor) - depends on Layer 0/1 only
///
/// @par Thread Safety
/// Not thread-safe. Create separate instances for each audio thread.
///
/// @par Real-Time Safety
/// All processing methods are noexcept with zero allocations.
///
/// @example Basic Usage
/// @code
/// MultiStageEnvelopeFilter filter;
/// filter.prepare(44100.0);
///
/// // Configure 4-stage sweep
/// filter.setNumStages(4);
/// filter.setStageTarget(0, 200.0f);   // Stage 0: 200 Hz
/// filter.setStageTarget(1, 2000.0f);  // Stage 1: 2000 Hz
/// filter.setStageTarget(2, 500.0f);   // Stage 2: 500 Hz
/// filter.setStageTarget(3, 800.0f);   // Stage 3: 800 Hz
/// filter.setStageTime(0, 100.0f);     // 100ms each
/// filter.setStageTime(1, 200.0f);
/// filter.setStageTime(2, 150.0f);
/// filter.setStageTime(3, 100.0f);
/// filter.setStageCurve(1, 1.0f);      // Exponential for stage 1
///
/// filter.trigger();  // Start envelope
///
/// // Process audio
/// for (size_t i = 0; i < numSamples; ++i) {
///     buffer[i] = filter.process(buffer[i]);
/// }
/// @endcode
class MultiStageEnvelopeFilter {
public:
    // =========================================================================
    // Constants (FR-002)
    // =========================================================================

    /// Maximum number of envelope stages (FR-002)
    static constexpr int kMaxStages = 8;

    /// Minimum resonance/Q factor
    static constexpr float kMinResonance = 0.1f;

    /// Maximum resonance/Q factor
    static constexpr float kMaxResonance = 30.0f;

    /// Minimum frequency in Hz
    static constexpr float kMinFrequency = 1.0f;

    /// Maximum stage transition time in milliseconds (FR-005)
    static constexpr float kMaxStageTimeMs = 10000.0f;

    /// Maximum release time in milliseconds (FR-017a)
    static constexpr float kMaxReleaseTimeMs = 10000.0f;

    /// Default release time in milliseconds
    static constexpr float kDefaultReleaseTimeMs = 500.0f;

    // =========================================================================
    // Lifecycle (FR-001, FR-007)
    // =========================================================================

    /// @brief Default constructor
    MultiStageEnvelopeFilter() noexcept = default;

    /// @brief Prepare the processor for a given sample rate (FR-001)
    ///
    /// Must be called before processing. Initializes internal filter and
    /// envelope state. May be called multiple times if sample rate changes.
    ///
    /// @param sampleRate Sample rate in Hz (clamped to >= 1000)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate >= 1000.0) ? sampleRate : 1000.0;
        prepared_ = true;

        // Prepare internal components
        filter_.prepare(sampleRate_);
        releaseSmoother_.configure(kDefaultReleaseTimeMs, static_cast<float>(sampleRate_));

        // Re-validate frequency clamping with new sample rate
        baseFrequency_ = clampFrequency(baseFrequency_);
        for (int i = 0; i < kMaxStages; ++i) {
            stages_[i].targetHz = clampFrequency(stages_[i].targetHz);
        }

        // Initialize filter at base frequency
        filter_.setCutoff(baseFrequency_);
        filter_.setResonance(resonance_);
        filter_.setMode(filterType_);
        currentCutoff_ = baseFrequency_;
    }

    /// @brief Reset internal state without changing parameters (FR-007)
    ///
    /// Clears envelope state, returns to stage 0, and resets filter.
    /// Configuration (stages, loop settings, etc.) is preserved.
    void reset() noexcept {
        state_ = EnvelopeState::Idle;
        currentStage_ = 0;
        stagePhase_ = 0.0f;
        phaseIncrement_ = 0.0f;
        stageFromFreq_ = baseFrequency_;
        stageToFreq_ = baseFrequency_;
        stageCurve_ = 0.0f;
        currentVelocity_ = 1.0f;
        currentCutoff_ = baseFrequency_;

        // Reset internal filter state
        filter_.reset();
        filter_.setCutoff(baseFrequency_);

        // Reset release smoother
        releaseSmoother_.snapTo(baseFrequency_);
    }

    // =========================================================================
    // Stage Configuration (FR-003 to FR-006)
    // =========================================================================

    /// @brief Set the number of active envelope stages (FR-003)
    /// @param stages Number of stages (clamped to [1, kMaxStages])
    void setNumStages(int stages) noexcept {
        numStages_ = std::clamp(stages, 1, kMaxStages);

        // Re-validate loop bounds
        loopStart_ = std::clamp(loopStart_, 0, numStages_ - 1);
        loopEnd_ = std::clamp(loopEnd_, loopStart_, numStages_ - 1);

        // Clamp current stage if needed during playback
        if (currentStage_ >= numStages_) {
            currentStage_ = numStages_ - 1;
        }
    }

    /// @brief Set the target cutoff frequency for a stage (FR-004)
    /// @param stage Stage index (0 to kMaxStages-1, out of range ignored)
    /// @param cutoffHz Target frequency in Hz (clamped to [1, sampleRate*0.45])
    void setStageTarget(int stage, float cutoffHz) noexcept {
        if (stage >= 0 && stage < kMaxStages) {
            stages_[stage].targetHz = clampFrequency(cutoffHz);
        }
    }

    /// @brief Set the transition time for a stage (FR-005)
    /// @param stage Stage index (0 to kMaxStages-1, out of range ignored)
    /// @param ms Transition time in milliseconds (clamped to [0, 10000])
    void setStageTime(int stage, float ms) noexcept {
        if (stage >= 0 && stage < kMaxStages) {
            stages_[stage].timeMs = std::clamp(ms, 0.0f, kMaxStageTimeMs);
        }
    }

    /// @brief Set the curve shape for a stage transition (FR-006)
    /// @param stage Stage index (0 to kMaxStages-1, out of range ignored)
    /// @param curve Curve value (clamped to [-1, +1])
    void setStageCurve(int stage, float curve) noexcept {
        if (stage >= 0 && stage < kMaxStages) {
            stages_[stage].curve = std::clamp(curve, -1.0f, 1.0f);
        }
    }

    // =========================================================================
    // Loop Control (FR-008 to FR-010a)
    // =========================================================================

    /// @brief Enable or disable envelope looping (FR-008)
    /// @param enabled true to enable loop, false to disable
    void setLoop(bool enabled) noexcept { loopEnabled_ = enabled; }

    /// @brief Set the loop start point (FR-009)
    /// @param stage Stage index (clamped to [0, numStages-1])
    void setLoopStart(int stage) noexcept {
        loopStart_ = std::clamp(stage, 0, numStages_ - 1);
        // Ensure loopEnd >= loopStart
        if (loopEnd_ < loopStart_) {
            loopEnd_ = loopStart_;
        }
    }

    /// @brief Set the loop end point (FR-010)
    /// @param stage Stage index (clamped to [loopStart, numStages-1])
    void setLoopEnd(int stage) noexcept {
        loopEnd_ = std::clamp(stage, loopStart_, numStages_ - 1);
    }

    // =========================================================================
    // Filter Configuration (FR-011 to FR-014)
    // =========================================================================

    /// @brief Set the filter resonance/Q factor (FR-011)
    /// @param q Q value (clamped to [0.1, 30.0])
    void setResonance(float q) noexcept {
        resonance_ = std::clamp(q, kMinResonance, kMaxResonance);
        filter_.setResonance(resonance_);
    }

    /// @brief Set the filter type (FR-012)
    /// @param type SVFMode (Lowpass, Bandpass, or Highpass)
    void setFilterType(SVFMode type) noexcept {
        filterType_ = type;
        filter_.setMode(filterType_);
    }

    /// @brief Set the base (minimum) cutoff frequency (FR-013)
    /// @param hz Frequency in Hz (clamped to [1, sampleRate*0.45])
    void setBaseFrequency(float hz) noexcept {
        baseFrequency_ = clampFrequency(hz);

        // If idle, update current cutoff to match
        if (state_ == EnvelopeState::Idle || state_ == EnvelopeState::Complete) {
            currentCutoff_ = baseFrequency_;
            filter_.setCutoff(currentCutoff_);
        }
    }

    // =========================================================================
    // Trigger & Control (FR-015 to FR-018a)
    // =========================================================================

    /// @brief Start the envelope from stage 0 (FR-015)
    ///
    /// Triggers the envelope with velocity 1.0. Restarts from stage 0
    /// even if envelope is already running.
    void trigger() noexcept { trigger(1.0f); }

    /// @brief Start the envelope with velocity-sensitive triggering (FR-016)
    /// @param velocity Velocity value (clamped to [0.0, 1.0])
    void trigger(float velocity) noexcept {
        if (!prepared_) return;

        currentVelocity_ = std::clamp(velocity, 0.0f, 1.0f);

        // Calculate velocity-scaled effective targets
        calculateEffectiveTargets();

        // Initialize stage 0 transition
        currentStage_ = 0;
        stagePhase_ = 0.0f;
        stageFromFreq_ = baseFrequency_;
        stageToFreq_ = effectiveTargets_[0];
        stageCurve_ = stages_[0].curve;

        // Calculate phase increment for stage 0
        phaseIncrement_ = calculatePhaseIncrement(stages_[0].timeMs);

        // Set state to running
        state_ = EnvelopeState::Running;
    }

    /// @brief Exit loop and begin decay to base frequency (FR-017)
    ///
    /// Immediately begins release phase, decaying from current cutoff
    /// to baseFrequency using the configured release time.
    void release() noexcept {
        if (state_ == EnvelopeState::Idle || state_ == EnvelopeState::Complete) {
            return;
        }

        // Configure release smoother
        releaseSmoother_.configure(releaseTimeMs_, static_cast<float>(sampleRate_));
        releaseSmoother_.snapTo(currentCutoff_);
        releaseSmoother_.setTarget(baseFrequency_);

        // Exit loop and enter releasing state
        state_ = EnvelopeState::Releasing;
    }

    /// @brief Set the release decay time (FR-017a)
    /// @param ms Release time in milliseconds (clamped to [0, 10000])
    void setReleaseTime(float ms) noexcept {
        releaseTimeMs_ = std::clamp(ms, 0.0f, kMaxReleaseTimeMs);
    }

    /// @brief Set velocity sensitivity for modulation depth (FR-018, FR-018a)
    /// @param amount Sensitivity (clamped to [0.0, 1.0])
    void setVelocitySensitivity(float amount) noexcept {
        velocitySensitivity_ = std::clamp(amount, 0.0f, 1.0f);
    }

    // =========================================================================
    // Processing (FR-019 to FR-022)
    // =========================================================================

    /// @brief Process a single audio sample (FR-019)
    /// @param input Input audio sample
    /// @return Filtered output sample
    [[nodiscard]] float process(float input) noexcept {
        if (!prepared_) {
            return 0.0f;
        }

        // Handle NaN/Inf input (FR-022)
        if (detail::isNaN(input) || detail::isInf(input)) {
            filter_.reset();
            return 0.0f;
        }

        // Update envelope state machine
        updateEnvelope();

        // Apply filter at current cutoff
        filter_.setCutoff(currentCutoff_);
        float output = filter_.process(input);

        // Flush denormals (FR-022)
        output = detail::flushDenormal(output);

        return output;
    }

    /// @brief Process a block of audio samples in-place (FR-020)
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    void processBlock(float* buffer, size_t numSamples) noexcept {
        if (!prepared_ || buffer == nullptr) {
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    // =========================================================================
    // State Monitoring (FR-023 to FR-027)
    // =========================================================================

    /// @brief Get the current filter cutoff frequency (FR-023)
    /// @return Current cutoff in Hz
    [[nodiscard]] float getCurrentCutoff() const noexcept { return currentCutoff_; }

    /// @brief Get the current envelope stage index (FR-024)
    /// @return Stage index (0 to numStages-1)
    [[nodiscard]] int getCurrentStage() const noexcept { return currentStage_; }

    /// @brief Get the current envelope position within stage (FR-025)
    /// @return Normalized position [0.0, 1.0]
    [[nodiscard]] float getEnvelopeValue() const noexcept {
        if (state_ == EnvelopeState::Idle) return 0.0f;
        if (state_ == EnvelopeState::Complete) return 1.0f;
        return std::clamp(stagePhase_, 0.0f, 1.0f);
    }

    /// @brief Check if envelope has finished (FR-026)
    /// @return true when state is Complete or Idle
    [[nodiscard]] bool isComplete() const noexcept {
        return state_ == EnvelopeState::Complete || state_ == EnvelopeState::Idle;
    }

    /// @brief Check if envelope is actively transitioning (FR-027)
    /// @return true when state is Running or Releasing
    [[nodiscard]] bool isRunning() const noexcept {
        return state_ == EnvelopeState::Running || state_ == EnvelopeState::Releasing;
    }

    // =========================================================================
    // Getters
    // =========================================================================

    [[nodiscard]] int getNumStages() const noexcept { return numStages_; }

    [[nodiscard]] float getStageTarget(int stage) const noexcept {
        if (stage >= 0 && stage < kMaxStages) {
            return stages_[stage].targetHz;
        }
        return 1000.0f; // Default
    }

    [[nodiscard]] float getStageTime(int stage) const noexcept {
        if (stage >= 0 && stage < kMaxStages) {
            return stages_[stage].timeMs;
        }
        return 100.0f; // Default
    }

    [[nodiscard]] float getStageCurve(int stage) const noexcept {
        if (stage >= 0 && stage < kMaxStages) {
            return stages_[stage].curve;
        }
        return 0.0f; // Default
    }

    [[nodiscard]] bool getLoop() const noexcept { return loopEnabled_; }
    [[nodiscard]] int getLoopStart() const noexcept { return loopStart_; }
    [[nodiscard]] int getLoopEnd() const noexcept { return loopEnd_; }
    [[nodiscard]] float getResonance() const noexcept { return resonance_; }
    [[nodiscard]] SVFMode getFilterType() const noexcept { return filterType_; }
    [[nodiscard]] float getBaseFrequency() const noexcept { return baseFrequency_; }
    [[nodiscard]] float getReleaseTime() const noexcept { return releaseTimeMs_; }
    [[nodiscard]] float getVelocitySensitivity() const noexcept { return velocitySensitivity_; }
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Clamp frequency to valid range (FR-014)
    [[nodiscard]] float clampFrequency(float hz) const noexcept {
        const float maxFreq = static_cast<float>(sampleRate_) * 0.45f;
        return std::clamp(hz, kMinFrequency, maxFreq);
    }

    /// @brief Calculate phase increment from stage time (samples)
    [[nodiscard]] float calculatePhaseIncrement(float timeMs) const noexcept {
        if (timeMs <= 0.0f) {
            return 1.0f; // Instant transition
        }
        const float timeSamples = timeMs * 0.001f * static_cast<float>(sampleRate_);
        return 1.0f / timeSamples;
    }

    /// @brief Apply curve shaping to linear phase (FR-028 to FR-031)
    /// @param t Linear phase [0, 1]
    /// @param curve Curve parameter [-1, +1]
    /// @return Curved phase [0, 1]
    [[nodiscard]] float applyCurve(float t, float curve) const noexcept {
        // Clamp t to [0, 1]
        t = std::clamp(t, 0.0f, 1.0f);

        if (std::abs(curve) < 0.001f) {
            // Linear (FR-028)
            return t;
        }

        if (curve >= 0.0f) {
            // Exponential: slow start, fast finish (FR-029)
            // t^(1 + curve * 3)
            const float exponent = 1.0f + curve * 3.0f;
            return std::pow(t, exponent);
        } else {
            // Logarithmic: fast start, slow finish (FR-030)
            // 1 - (1-t)^(1 + |curve| * 3)
            const float exponent = 1.0f + (-curve) * 3.0f;
            return 1.0f - std::pow(1.0f - t, exponent);
        }
    }

    /// @brief Calculate velocity-scaled effective targets (FR-018a)
    void calculateEffectiveTargets() noexcept {
        // Find maximum target across all active stages
        float maxTarget = baseFrequency_;
        for (int i = 0; i < numStages_; ++i) {
            maxTarget = std::max(maxTarget, stages_[i].targetHz);
        }

        // Calculate full range
        const float fullRange = maxTarget - baseFrequency_;

        // Early exit if no range
        if (fullRange <= 0.0f) {
            for (int i = 0; i < kMaxStages; ++i) {
                effectiveTargets_[i] = stages_[i].targetHz;
            }
            return;
        }

        // Calculate depth scale factor
        // sensitivity=0 -> depthScale=1 (velocity ignored, full depth always)
        // sensitivity=1, velocity=0 -> depthScale=0 (no modulation)
        // sensitivity=1, velocity=1 -> depthScale=1 (full modulation)
        const float depthScale = 1.0f - velocitySensitivity_ * (1.0f - currentVelocity_);

        // Scale each target proportionally
        for (int i = 0; i < kMaxStages; ++i) {
            const float originalOffset = stages_[i].targetHz - baseFrequency_;
            const float scaledOffset = originalOffset * depthScale;
            effectiveTargets_[i] = baseFrequency_ + scaledOffset;
        }
    }

    /// @brief Start transition to a specific stage
    void startStageTransition(int stageIndex) noexcept {
        stageFromFreq_ = currentCutoff_;
        stageToFreq_ = effectiveTargets_[stageIndex];
        stageCurve_ = stages_[stageIndex].curve;
        phaseIncrement_ = calculatePhaseIncrement(stages_[stageIndex].timeMs);
        stagePhase_ = 0.0f;
    }

    /// @brief Update envelope state machine (called each sample)
    void updateEnvelope() noexcept {
        switch (state_) {
            case EnvelopeState::Idle:
            case EnvelopeState::Complete:
                // Filter at base frequency when not running
                currentCutoff_ = baseFrequency_;
                break;

            case EnvelopeState::Running:
                updateRunningState();
                break;

            case EnvelopeState::Releasing:
                updateReleasingState();
                break;
        }
    }

    /// @brief Update envelope during Running state
    void updateRunningState() noexcept {
        // Advance phase
        stagePhase_ += phaseIncrement_;

        // Check for stage completion
        if (stagePhase_ >= 1.0f) {
            // Stage complete
            stagePhase_ = 1.0f;

            // Calculate final cutoff for completed stage
            const float curvedPhase = applyCurve(stagePhase_, stageCurve_);
            currentCutoff_ = stageFromFreq_ + (stageToFreq_ - stageFromFreq_) * curvedPhase;
            currentCutoff_ = clampFrequency(currentCutoff_);

            // Check for loop wrap (FR-010a)
            if (loopEnabled_ && currentStage_ == loopEnd_) {
                // Loop back to loopStart
                currentStage_ = loopStart_;
                startStageTransition(loopStart_);
            } else if (currentStage_ < numStages_ - 1) {
                // Advance to next stage
                currentStage_++;
                startStageTransition(currentStage_);
            } else {
                // Last stage complete, no loop - envelope complete
                state_ = EnvelopeState::Complete;
            }
        } else {
            // Calculate current cutoff based on curved phase
            const float curvedPhase = applyCurve(stagePhase_, stageCurve_);
            currentCutoff_ = stageFromFreq_ + (stageToFreq_ - stageFromFreq_) * curvedPhase;
            currentCutoff_ = clampFrequency(currentCutoff_);
        }
    }

    /// @brief Update envelope during Releasing state
    void updateReleasingState() noexcept {
        currentCutoff_ = releaseSmoother_.process();
        currentCutoff_ = clampFrequency(currentCutoff_);

        // Use frequency-appropriate threshold (1 Hz) instead of smoother's
        // normalized threshold (0.0001) which is too tight for frequency values
        constexpr float kFrequencyThreshold = 1.0f;
        if (std::abs(currentCutoff_ - baseFrequency_) < kFrequencyThreshold) {
            state_ = EnvelopeState::Complete;
            currentCutoff_ = baseFrequency_;
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Sample rate
    double sampleRate_ = 44100.0;

    // Stage configuration
    std::array<EnvelopeStage, kMaxStages> stages_{};
    int numStages_ = 1;

    // Loop configuration
    bool loopEnabled_ = false;
    int loopStart_ = 0;
    int loopEnd_ = 0;

    // Filter configuration
    SVFMode filterType_ = SVFMode::Lowpass;
    float resonance_ = SVF::kButterworthQ;
    float baseFrequency_ = 100.0f;

    // Modulation configuration
    float velocitySensitivity_ = 0.0f;
    float releaseTimeMs_ = kDefaultReleaseTimeMs;

    // Envelope state
    EnvelopeState state_ = EnvelopeState::Idle;
    int currentStage_ = 0;
    float stagePhase_ = 0.0f;
    float phaseIncrement_ = 0.0f;

    // Transition state
    float stageFromFreq_ = 100.0f;
    float stageToFreq_ = 100.0f;
    float stageCurve_ = 0.0f;

    // Velocity state
    float currentVelocity_ = 1.0f;
    std::array<float, kMaxStages> effectiveTargets_{};

    // Output state
    float currentCutoff_ = 100.0f;

    // Components
    SVF filter_;
    OnePoleSmoother releaseSmoother_;

    // Prepared flag
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
