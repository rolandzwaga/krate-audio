// ==============================================================================
// Layer 2: DSP Processor - Sidechain Filter
// ==============================================================================
// Dynamically controls filter cutoff frequency based on sidechain signal
// envelope for ducking/pumping effects.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, constexpr)
// - Principle IX: Layer 2 (depends only on Layer 0/1 and peer Layer 2)
// - Principle X: DSP Constraints (sample-accurate, denormal handling)
// - Principle XII: Test-First Development
//
// Reference: specs/090-sidechain-filter/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/processors/envelope_follower.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Enumerations
// =============================================================================

/// @brief State machine states for hold behavior
enum class SidechainFilterState : uint8_t {
    Idle = 0,     ///< Below threshold, filter at resting position
    Active = 1,   ///< Above threshold, envelope controlling filter
    Holding = 2   ///< Below threshold but in hold period
};

/// @brief Envelope-to-cutoff mapping direction for SidechainFilter
/// @note Declared locally to avoid EnvelopeFilter dependency
enum class SidechainDirection : uint8_t {
    Up = 0,    ///< Louder -> higher cutoff, rests at minCutoff when silent
    Down = 1   ///< Louder -> lower cutoff, rests at maxCutoff when silent
};

/// @brief Filter response type for SidechainFilter
/// @note Maps to SVFMode internally
enum class SidechainFilterMode : uint8_t {
    Lowpass = 0,   ///< 12 dB/oct lowpass
    Bandpass = 1,  ///< Constant 0 dB peak bandpass
    Highpass = 2   ///< 12 dB/oct highpass
};

// =============================================================================
// SidechainFilter Class
// =============================================================================

/// @brief Layer 2 DSP Processor - Sidechain-controlled dynamic filter
///
/// Dynamically controls a filter's cutoff frequency based on the amplitude
/// envelope of a sidechain signal. Supports external sidechain for ducking/pumping
/// effects and self-sidechain for auto-wah with optional lookahead.
///
/// @par Key Features
/// - External sidechain input for ducking/pumping (FR-001)
/// - Self-sidechain mode for auto-wah effects (FR-002)
/// - Configurable attack/release envelope times (FR-003, FR-004)
/// - Threshold triggering with dB domain comparison (FR-005)
/// - Hold time to prevent chattering (FR-014, FR-015, FR-016)
/// - Lookahead for transient anticipation (FR-013)
/// - Log-space cutoff mapping for perceptual linearity (FR-012)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, pre-allocated)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (composes EnvelopeFollower, SVF, DelayLine)
///
/// @par Usage
/// @code
/// SidechainFilter filter;
/// filter.prepare(48000.0, 512);
/// filter.setDirection(SidechainDirection::Down);
/// filter.setThreshold(-30.0f);
///
/// // External sidechain
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = filter.processSample(mainInput[i], sidechainInput[i]);
/// }
///
/// // Self-sidechain
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = filter.processSample(input[i]);
/// }
/// @endcode
class SidechainFilter {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinAttackMs = 0.1f;
    static constexpr float kMaxAttackMs = 500.0f;
    static constexpr float kMinReleaseMs = 1.0f;
    static constexpr float kMaxReleaseMs = 5000.0f;
    static constexpr float kMinThresholdDb = -60.0f;
    static constexpr float kMaxThresholdDb = 0.0f;
    static constexpr float kMinSensitivityDb = -24.0f;
    static constexpr float kMaxSensitivityDb = 24.0f;
    static constexpr float kMinCutoffHz = 20.0f;
    static constexpr float kMinResonance = 0.5f;
    static constexpr float kMaxResonance = 20.0f;
    static constexpr float kMinLookaheadMs = 0.0f;
    static constexpr float kMaxLookaheadMs = 50.0f;
    static constexpr float kMinHoldMs = 0.0f;
    static constexpr float kMaxHoldMs = 1000.0f;
    static constexpr float kMinSidechainHpHz = 20.0f;
    static constexpr float kMaxSidechainHpHz = 500.0f;

    // Defaults
    static constexpr float kDefaultAttackMs = 10.0f;
    static constexpr float kDefaultReleaseMs = 100.0f;
    static constexpr float kDefaultThresholdDb = -30.0f;
    static constexpr float kDefaultSensitivityDb = 0.0f;
    static constexpr float kDefaultMinCutoffHz = 200.0f;
    static constexpr float kDefaultMaxCutoffHz = 2000.0f;
    static constexpr float kDefaultResonance = 8.0f;
    static constexpr float kDefaultSidechainHpHz = 80.0f;

    // =========================================================================
    // Lifecycle (FR-024, FR-025, FR-026)
    // =========================================================================

    /// @brief Default constructor
    SidechainFilter() noexcept = default;

    /// @brief Destructor
    ~SidechainFilter() = default;

    // Non-copyable (contains filter state)
    SidechainFilter(const SidechainFilter&) = delete;
    SidechainFilter& operator=(const SidechainFilter&) = delete;

    // Movable
    SidechainFilter(SidechainFilter&&) noexcept = default;
    SidechainFilter& operator=(SidechainFilter&&) noexcept = default;

    /// @brief Prepare processor for given sample rate (FR-024)
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @pre sampleRate >= 1000.0
    /// @note NOT real-time safe (allocates DelayLine buffer)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        (void)maxBlockSize;  // Not needed for this processor
        sampleRate_ = sampleRate;

        // Calculate Nyquist-safe max cutoff
        maxCutoffLimit_ = static_cast<float>(sampleRate_) * 0.45f;

        // Prepare envelope follower
        envFollower_.prepare(sampleRate, maxBlockSize);
        envFollower_.setMode(DetectionMode::Amplitude);
        envFollower_.setAttackTime(attackMs_);
        envFollower_.setReleaseTime(releaseMs_);
        envFollower_.setSidechainEnabled(false);  // We handle filtering ourselves

        // Prepare main filter
        filter_.prepare(sampleRate);
        filter_.setMode(mapFilterType(filterType_));
        filter_.setResonance(resonance_);
        filter_.setCutoff(getRestingCutoff());

        // Prepare lookahead delay (allocate for max lookahead)
        lookaheadDelay_.prepare(sampleRate, kMaxLookaheadMs / 1000.0f);

        // Prepare sidechain HP filter
        sidechainHpFilter_.configure(
            Krate::DSP::FilterType::Highpass,
            sidechainHpCutoffHz_,
            kButterworthQ,
            0.0f,
            static_cast<float>(sampleRate_)
        );

        // Prepare cutoff smoother
        cutoffSmoother_.configure(5.0f, static_cast<float>(sampleRate_));  // 5ms smoothing
        cutoffSmoother_.snapTo(getRestingCutoff());

        // Initialize state
        updateLookaheadSamples();
        updateHoldSamples();
        currentCutoff_ = getRestingCutoff();

        prepared_ = true;
        reset();
    }

    /// @brief Reset internal state without reallocation (FR-025)
    /// @post Envelope cleared, filter reset, hold timer cleared
    /// @note Real-time safe
    void reset() noexcept {
        envFollower_.reset();
        filter_.reset();
        lookaheadDelay_.reset();
        sidechainHpFilter_.reset();

        state_ = SidechainFilterState::Idle;
        holdSamplesRemaining_ = 0;
        activeEnvelope_ = 0.0f;
        holdEnvelope_ = 0.0f;
        currentEnvelope_ = 0.0f;
        currentCutoff_ = getRestingCutoff();
        cutoffSmoother_.snapTo(currentCutoff_);
    }

    /// @brief Get processing latency in samples (FR-026)
    /// @return Latency (equals lookahead in samples, 0 if lookahead disabled)
    [[nodiscard]] size_t getLatency() const noexcept {
        return lookaheadSamples_;
    }

    // =========================================================================
    // Processing (FR-019, FR-020, FR-021)
    // =========================================================================

    /// @brief Process with external sidechain (FR-001, FR-019)
    /// @param mainInput Main audio sample to filter
    /// @param sidechainInput Sidechain sample for envelope detection
    /// @return Filtered output sample
    /// @pre prepare() has been called
    /// @note Real-time safe (noexcept, no allocations)
    [[nodiscard]] float processSample(float mainInput, float sidechainInput) noexcept {
        if (!prepared_) return mainInput;

        // Handle NaN/Inf in main input
        if (detail::isNaN(mainInput) || detail::isInf(mainInput)) {
            filter_.reset();
            return 0.0f;
        }

        // Handle NaN/Inf in sidechain (treat as silent)
        if (detail::isNaN(sidechainInput) || detail::isInf(sidechainInput)) {
            sidechainInput = 0.0f;
        }

        // 1. Process sidechain path
        float sidechainSignal = sidechainInput;

        // Apply sidechain HP filter if enabled
        if (sidechainHpEnabled_) {
            sidechainSignal = sidechainHpFilter_.process(sidechainInput);
        }

        // Apply sensitivity gain
        sidechainSignal *= sensitivityGain_;

        // Get envelope from sidechain
        const float envelope = envFollower_.processSample(sidechainSignal);
        currentEnvelope_ = envelope;

        // 2. Convert envelope to dB for threshold comparison
        const float envelopeDb = (envelope > 0.0f) ? gainToDb(envelope) : kSilenceFloorDb;

        // 3. Update state machine
        const float effectiveEnvelope = updateStateMachine(envelopeDb);

        // 4. Calculate cutoff
        float targetCutoff;
        if (state_ == SidechainFilterState::Idle) {
            targetCutoff = getRestingCutoff();
        } else {
            targetCutoff = mapEnvelopeToCutoff(effectiveEnvelope);
        }

        // 5. Smooth cutoff changes
        cutoffSmoother_.setTarget(targetCutoff);
        currentCutoff_ = cutoffSmoother_.process();

        // Apply cutoff to filter
        filter_.setCutoff(currentCutoff_);

        // 6. Process audio through lookahead delay and filter
        lookaheadDelay_.write(mainInput);
        const float delayedInput = (lookaheadSamples_ > 0)
            ? lookaheadDelay_.read(lookaheadSamples_)
            : mainInput;

        return filter_.process(delayedInput);
    }

    /// @brief Process with self-sidechain (FR-002)
    /// @param input Audio sample (used for both filtering and envelope)
    /// @return Filtered output sample
    /// @pre prepare() has been called
    /// @note In self-sidechain mode with lookahead, sidechain sees undelayed
    ///       signal while audio path is delayed (FR-013 clarification)
    [[nodiscard]] float processSample(float input) noexcept {
        // Self-sidechain: use same signal for both paths
        // The sidechain sees undelayed signal, audio path sees delayed signal
        return processSample(input, input);
    }

    /// @brief Block processing with external sidechain (FR-020)
    /// @param mainInput Main audio input buffer
    /// @param sidechainInput Sidechain input buffer
    /// @param output Output buffer (may alias mainInput)
    /// @param numSamples Number of samples to process
    void process(const float* mainInput, const float* sidechainInput,
                 float* output, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = processSample(mainInput[i], sidechainInput[i]);
        }
    }

    /// @brief Block processing in-place with external sidechain (FR-021)
    /// @param mainInOut Main audio buffer (modified in-place)
    /// @param sidechainInput Sidechain input buffer
    /// @param numSamples Number of samples to process
    void process(float* mainInOut, const float* sidechainInput,
                 size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            mainInOut[i] = processSample(mainInOut[i], sidechainInput[i]);
        }
    }

    /// @brief Block processing with self-sidechain
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    void process(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = processSample(buffer[i]);
        }
    }

    // =========================================================================
    // Sidechain Detection Parameters (FR-003 to FR-006)
    // =========================================================================

    /// @brief Set envelope attack time (FR-003)
    /// @param ms Attack time in milliseconds, clamped to [0.1, 500]
    void setAttackTime(float ms) noexcept {
        attackMs_ = std::clamp(ms, kMinAttackMs, kMaxAttackMs);
        envFollower_.setAttackTime(attackMs_);
    }

    /// @brief Set envelope release time (FR-004)
    /// @param ms Release time in milliseconds, clamped to [1, 5000]
    void setReleaseTime(float ms) noexcept {
        releaseMs_ = std::clamp(ms, kMinReleaseMs, kMaxReleaseMs);
        envFollower_.setReleaseTime(releaseMs_);
    }

    /// @brief Set trigger threshold (FR-005)
    /// @param dB Threshold in dB, clamped to [-60, 0]
    /// @note Comparison is: 20*log10(envelope) > threshold
    void setThreshold(float dB) noexcept {
        thresholdDb_ = std::clamp(dB, kMinThresholdDb, kMaxThresholdDb);
    }

    /// @brief Set sidechain sensitivity/pre-gain (FR-006)
    /// @param dB Gain in dB, clamped to [-24, +24]
    void setSensitivity(float dB) noexcept {
        sensitivityDb_ = std::clamp(dB, kMinSensitivityDb, kMaxSensitivityDb);
        sensitivityGain_ = dbToGain(sensitivityDb_);
    }

    [[nodiscard]] float getAttackTime() const noexcept { return attackMs_; }
    [[nodiscard]] float getReleaseTime() const noexcept { return releaseMs_; }
    [[nodiscard]] float getThreshold() const noexcept { return thresholdDb_; }
    [[nodiscard]] float getSensitivity() const noexcept { return sensitivityDb_; }

    // =========================================================================
    // Filter Response Parameters (FR-007 to FR-012)
    // =========================================================================

    /// @brief Set envelope-to-cutoff direction (FR-007)
    /// @param dir Up (louder=higher cutoff) or Down (louder=lower cutoff)
    void setDirection(SidechainDirection dir) noexcept {
        direction_ = dir;
        // Update current cutoff to new resting position if idle
        if (state_ == SidechainFilterState::Idle) {
            currentCutoff_ = getRestingCutoff();
            cutoffSmoother_.snapTo(currentCutoff_);
        }
    }

    /// @brief Set minimum cutoff frequency (FR-008)
    /// @param hz Frequency in Hz, clamped to [20, maxCutoff-1]
    void setMinCutoff(float hz) noexcept {
        minCutoffHz_ = std::clamp(hz, kMinCutoffHz, maxCutoffHz_ - 1.0f);
        // Update current cutoff if idle
        if (state_ == SidechainFilterState::Idle && prepared_) {
            currentCutoff_ = getRestingCutoff();
            cutoffSmoother_.snapTo(currentCutoff_);
        }
    }

    /// @brief Set maximum cutoff frequency (FR-009)
    /// @param hz Frequency in Hz, clamped to [minCutoff+1, sampleRate*0.45]
    void setMaxCutoff(float hz) noexcept {
        maxCutoffHz_ = std::clamp(hz, minCutoffHz_ + 1.0f, maxCutoffLimit_);
        // Update current cutoff if idle
        if (state_ == SidechainFilterState::Idle && prepared_) {
            currentCutoff_ = getRestingCutoff();
            cutoffSmoother_.snapTo(currentCutoff_);
        }
    }

    /// @brief Set filter resonance (FR-010)
    /// @param q Q value, clamped to [0.5, 20.0]
    void setResonance(float q) noexcept {
        resonance_ = std::clamp(q, kMinResonance, kMaxResonance);
        filter_.setResonance(resonance_);
    }

    /// @brief Set filter type (FR-011)
    /// @param type Lowpass, Bandpass, or Highpass
    void setFilterType(SidechainFilterMode type) noexcept {
        filterType_ = type;
        filter_.setMode(mapFilterType(filterType_));
    }

    [[nodiscard]] SidechainDirection getDirection() const noexcept { return direction_; }
    [[nodiscard]] float getMinCutoff() const noexcept { return minCutoffHz_; }
    [[nodiscard]] float getMaxCutoff() const noexcept { return maxCutoffHz_; }
    [[nodiscard]] float getResonance() const noexcept { return resonance_; }
    [[nodiscard]] SidechainFilterMode getFilterType() const noexcept { return filterType_; }

    // =========================================================================
    // Timing Parameters (FR-013 to FR-016)
    // =========================================================================

    /// @brief Set lookahead time (FR-013)
    /// @param ms Lookahead in milliseconds, clamped to [0, 50]
    /// @note Adds latency equal to lookahead time
    void setLookahead(float ms) noexcept {
        lookaheadMs_ = std::clamp(ms, kMinLookaheadMs, kMaxLookaheadMs);
        updateLookaheadSamples();
    }

    /// @brief Set hold time (FR-014)
    /// @param ms Hold time in milliseconds, clamped to [0, 1000]
    /// @note Hold delays release without affecting attack (FR-015)
    /// @note Re-triggering during hold resets the timer (FR-016)
    void setHoldTime(float ms) noexcept {
        holdMs_ = std::clamp(ms, kMinHoldMs, kMaxHoldMs);
        updateHoldSamples();
    }

    [[nodiscard]] float getLookahead() const noexcept { return lookaheadMs_; }
    [[nodiscard]] float getHoldTime() const noexcept { return holdMs_; }

    // =========================================================================
    // Sidechain Filter Parameters (FR-017, FR-018)
    // =========================================================================

    /// @brief Enable/disable sidechain highpass filter (FR-017)
    /// @param enabled true to enable filter
    void setSidechainFilterEnabled(bool enabled) noexcept {
        sidechainHpEnabled_ = enabled;
    }

    /// @brief Set sidechain filter cutoff (FR-018)
    /// @param hz Cutoff in Hz, clamped to [20, 500]
    void setSidechainFilterCutoff(float hz) noexcept {
        sidechainHpCutoffHz_ = std::clamp(hz, kMinSidechainHpHz, kMaxSidechainHpHz);
        sidechainHpFilter_.configure(
            Krate::DSP::FilterType::Highpass,
            sidechainHpCutoffHz_,
            kButterworthQ,
            0.0f,
            static_cast<float>(sampleRate_)
        );
    }

    [[nodiscard]] bool isSidechainFilterEnabled() const noexcept { return sidechainHpEnabled_; }
    [[nodiscard]] float getSidechainFilterCutoff() const noexcept { return sidechainHpCutoffHz_; }

    // =========================================================================
    // Monitoring (FR-027, FR-028)
    // =========================================================================

    /// @brief Get current filter cutoff frequency (FR-027)
    /// @return Cutoff in Hz
    [[nodiscard]] float getCurrentCutoff() const noexcept { return currentCutoff_; }

    /// @brief Get current envelope value (FR-028)
    /// @return Envelope value (linear, typically 0.0 to 1.0, may exceed 1.0)
    [[nodiscard]] float getCurrentEnvelope() const noexcept { return currentEnvelope_; }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if processor has been prepared
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Update state machine and return effective envelope for cutoff
    /// @param envelopeDb Envelope value in dB
    /// @return Effective envelope for cutoff mapping (0 if idle)
    float updateStateMachine(float envelopeDb) noexcept {
        const bool aboveThreshold = envelopeDb > thresholdDb_;

        switch (state_) {
            case SidechainFilterState::Idle:
                if (aboveThreshold) {
                    state_ = SidechainFilterState::Active;
                    activeEnvelope_ = envFollower_.getCurrentValue();
                    holdEnvelope_ = activeEnvelope_;  // Start tracking peak for this trigger
                }
                return 0.0f;  // Use resting cutoff

            case SidechainFilterState::Active: {
                activeEnvelope_ = envFollower_.getCurrentValue();
                // Track peak envelope during active phase for use in hold
                if (activeEnvelope_ > holdEnvelope_) {
                    holdEnvelope_ = activeEnvelope_;
                }
                if (!aboveThreshold) {
                    if (holdSamplesTotal_ > 0) {
                        state_ = SidechainFilterState::Holding;
                        holdSamplesRemaining_ = holdSamplesTotal_;
                        // holdEnvelope_ already contains the peak value
                        // Return the hold envelope, not the current (decayed) value
                        return holdEnvelope_;
                    } else {
                        state_ = SidechainFilterState::Idle;
                        return 0.0f;  // Immediate release
                    }
                }
                return activeEnvelope_;
            }

            case SidechainFilterState::Holding:
                // During hold, we maintain the envelope value from when hold started (FR-015)

                if (aboveThreshold) {
                    // Re-trigger: reset hold timer, go back to Active (FR-016)
                    state_ = SidechainFilterState::Active;
                    activeEnvelope_ = envFollower_.getCurrentValue();
                } else if (holdSamplesRemaining_ > 0) {
                    --holdSamplesRemaining_;
                } else {
                    // Hold expired: begin release
                    state_ = SidechainFilterState::Idle;
                    return 0.0f;
                }
                // Return the frozen envelope value from when hold started
                return holdEnvelope_;
        }

        return 0.0f;
    }

    /// @brief Map envelope [0,1] to cutoff using log-space interpolation (FR-012)
    /// @param envelope Envelope value (typically 0.0 to 1.0)
    /// @return Cutoff frequency in Hz
    [[nodiscard]] float mapEnvelopeToCutoff(float envelope) const noexcept {
        // Clamp envelope to [0, 1]
        envelope = std::clamp(envelope, 0.0f, 1.0f);

        // Log-space interpolation: exp(lerp(log(min), log(max), t))
        const float logMin = std::log(minCutoffHz_);
        const float logMax = std::log(maxCutoffHz_);

        float t = envelope;
        if (direction_ == SidechainDirection::Down) {
            t = 1.0f - t;  // Invert for down direction
        }

        const float logCutoff = logMin + t * (logMax - logMin);
        return std::exp(logCutoff);
    }

    /// @brief Get resting cutoff based on direction
    /// @return Resting cutoff frequency in Hz
    [[nodiscard]] float getRestingCutoff() const noexcept {
        // SidechainDirection::Up rests at minCutoff when silent (filter closed)
        // SidechainDirection::Down rests at maxCutoff when silent (filter open)
        return (direction_ == SidechainDirection::Up) ? minCutoffHz_ : maxCutoffHz_;
    }

    /// @brief Update lookahead delay samples from ms
    void updateLookaheadSamples() noexcept {
        // Use lround to avoid cross-platform truncation differences
        lookaheadSamples_ = static_cast<size_t>(
            std::lround((lookaheadMs_ / 1000.0f) * sampleRate_));
    }

    /// @brief Update hold time in samples
    void updateHoldSamples() noexcept {
        holdSamplesTotal_ = static_cast<size_t>((holdMs_ / 1000.0f) * sampleRate_);
    }

    /// @brief Map SidechainFilterMode to SVFMode
    /// @param type SidechainFilterMode enum
    /// @return Corresponding SVFMode
    [[nodiscard]] SVFMode mapFilterType(SidechainFilterMode type) const noexcept {
        switch (type) {
            case SidechainFilterMode::Lowpass:  return SVFMode::Lowpass;
            case SidechainFilterMode::Bandpass: return SVFMode::Bandpass;
            case SidechainFilterMode::Highpass: return SVFMode::Highpass;
            default: return SVFMode::Lowpass;
        }
    }

    // =========================================================================
    // Composed Components
    // =========================================================================

    EnvelopeFollower envFollower_;      ///< Sidechain envelope detection
    SVF filter_;                        ///< Main filter
    DelayLine lookaheadDelay_;          ///< Audio lookahead buffer
    Biquad sidechainHpFilter_;          ///< Sidechain highpass
    OnePoleSmoother cutoffSmoother_;    ///< Cutoff smoothing

    // =========================================================================
    // State Machine
    // =========================================================================

    SidechainFilterState state_ = SidechainFilterState::Idle;
    size_t holdSamplesRemaining_ = 0;
    size_t holdSamplesTotal_ = 0;
    float activeEnvelope_ = 0.0f;       ///< Envelope during active phase
    float holdEnvelope_ = 0.0f;         ///< Frozen envelope for hold phase

    // =========================================================================
    // Configuration
    // =========================================================================

    double sampleRate_ = 44100.0;
    float attackMs_ = kDefaultAttackMs;
    float releaseMs_ = kDefaultReleaseMs;
    float thresholdDb_ = kDefaultThresholdDb;
    float sensitivityDb_ = kDefaultSensitivityDb;
    float sensitivityGain_ = 1.0f;

    SidechainDirection direction_ = SidechainDirection::Down;
    SidechainFilterMode filterType_ = SidechainFilterMode::Lowpass;
    float minCutoffHz_ = kDefaultMinCutoffHz;
    float maxCutoffHz_ = kDefaultMaxCutoffHz;
    float resonance_ = kDefaultResonance;

    float lookaheadMs_ = 0.0f;
    size_t lookaheadSamples_ = 0;
    float holdMs_ = 0.0f;

    bool sidechainHpEnabled_ = false;
    float sidechainHpCutoffHz_ = kDefaultSidechainHpHz;

    // =========================================================================
    // Monitoring State
    // =========================================================================

    float currentCutoff_ = kDefaultMinCutoffHz;
    float currentEnvelope_ = 0.0f;

    // =========================================================================
    // Lifecycle State
    // =========================================================================

    bool prepared_ = false;
    float maxCutoffLimit_ = 20000.0f;   ///< Nyquist-safe limit
};

} // namespace DSP
} // namespace Krate
