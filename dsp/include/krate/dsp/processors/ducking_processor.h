// ==============================================================================
// Layer 2: DSP Processor - Ducking Processor
// ==============================================================================
// Sidechain-triggered gain reduction processor that attenuates a main audio
// signal based on the level of an external sidechain signal.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, constexpr, [[nodiscard]])
// - Principle IX: Layer 2 (depends on Layer 0-1 plus peer Layer 2 EnvelopeFollower)
// - Principle XII: Test-First Development
//
// Reference: specs/012-ducking-processor/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/envelope_follower.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Ducking State Enumeration
// =============================================================================

/// @brief Internal state machine states for hold time behavior
enum class DuckingState : uint8_t {
    Idle = 0,     ///< Sidechain below threshold, no gain reduction
    Ducking = 1,  ///< Sidechain above threshold, gain reduction active
    Holding = 2   ///< Sidechain dropped below threshold, holding before release
};

// =============================================================================
// DuckingProcessor Class
// =============================================================================

/// @brief Layer 2 DSP Processor - Sidechain-triggered gain reduction
///
/// Attenuates a main audio signal when an external sidechain signal exceeds
/// a threshold. Used for voiceover ducking, podcast mixing, and similar
/// applications where one audio source should automatically reduce the level
/// of another.
///
/// @par Key Features
/// - External sidechain input (FR-017)
/// - Threshold-triggered ducking (FR-001, FR-002, FR-003)
/// - Configurable depth (FR-004)
/// - Attack/release timing (FR-005, FR-006)
/// - Hold time to prevent chattering (FR-008, FR-009, FR-010)
/// - Range limit for maximum attenuation (FR-011, FR-012, FR-013)
/// - Optional sidechain highpass filter (FR-014, FR-015, FR-016)
/// - Gain reduction metering (FR-025)
/// - Zero latency (SC-008)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, pre-allocated)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (depends on Layer 0-1 and peer EnvelopeFollower)
/// - Principle XII: Test-First Development
///
/// @par Usage
/// @code
/// DuckingProcessor ducker;
/// ducker.prepare(44100.0, 512);
/// ducker.setThreshold(-30.0f);
/// ducker.setDepth(-12.0f);
/// ducker.setHoldTime(50.0f);
///
/// // In process callback
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = ducker.processSample(mainInput[i], sidechainInput[i]);
/// }
/// // Or block processing:
/// ducker.process(mainBuffer, sidechainBuffer, outputBuffer, numSamples);
/// @endcode
class DuckingProcessor {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinThreshold = -60.0f;      // dB
    static constexpr float kMaxThreshold = 0.0f;        // dB
    static constexpr float kDefaultThreshold = -30.0f;  // dB

    static constexpr float kMinDepth = -48.0f;          // dB
    static constexpr float kMaxDepth = 0.0f;            // dB
    static constexpr float kDefaultDepth = -12.0f;      // dB

    static constexpr float kMinAttackMs = 0.1f;         // ms
    static constexpr float kMaxAttackMs = 500.0f;       // ms
    static constexpr float kDefaultAttackMs = 10.0f;    // ms

    static constexpr float kMinReleaseMs = 1.0f;        // ms
    static constexpr float kMaxReleaseMs = 5000.0f;     // ms
    static constexpr float kDefaultReleaseMs = 100.0f;  // ms

    static constexpr float kMinHoldMs = 0.0f;           // ms
    static constexpr float kMaxHoldMs = 1000.0f;        // ms
    static constexpr float kDefaultHoldMs = 50.0f;      // ms

    static constexpr float kMinRange = -48.0f;          // dB
    static constexpr float kMaxRange = 0.0f;            // dB (0 = disabled)
    static constexpr float kDefaultRange = 0.0f;        // dB (disabled)

    static constexpr float kMinSidechainHz = 20.0f;     // Hz
    static constexpr float kMaxSidechainHz = 500.0f;    // Hz
    static constexpr float kDefaultSidechainHz = 80.0f; // Hz

    // =========================================================================
    // Lifecycle (FR-023, FR-024)
    // =========================================================================

    /// @brief Default constructor
    DuckingProcessor() noexcept = default;

    /// @brief Destructor
    ~DuckingProcessor() = default;

    // Non-copyable, movable
    DuckingProcessor(const DuckingProcessor&) = delete;
    DuckingProcessor& operator=(const DuckingProcessor&) = delete;
    DuckingProcessor(DuckingProcessor&&) noexcept = default;
    DuckingProcessor& operator=(DuckingProcessor&&) noexcept = default;

    /// @brief Prepare processor for given sample rate
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @note Call before audio processing begins
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        (void)maxBlockSize;  // Not needed - no buffer allocation
        sampleRate_ = static_cast<float>(sampleRate);

        // Configure envelope follower for sidechain detection
        envelopeFollower_.prepare(sampleRate, maxBlockSize);
        envelopeFollower_.setMode(DetectionMode::Peak);
        envelopeFollower_.setAttackTime(attackTimeMs_);
        envelopeFollower_.setReleaseTime(releaseTimeMs_);
        envelopeFollower_.setSidechainEnabled(false);  // We handle filtering ourselves

        // Configure gain smoother for click-free transitions
        gainSmoother_.configure(5.0f, sampleRate_);  // 5ms smoothing

        // Configure sidechain filter
        sidechainFilter_.configure(
            FilterType::Highpass,
            sidechainFilterCutoffHz_,
            kButterworthQ,
            0.0f,
            sampleRate_
        );

        // Recalculate hold time in samples
        updateHoldSamples();

        reset();
    }

    /// @brief Reset internal state without reallocation
    /// @note Clears envelope, gain state, and hold timer
    void reset() noexcept {
        envelopeFollower_.reset();
        gainSmoother_.reset();
        sidechainFilter_.reset();
        state_ = DuckingState::Idle;
        holdSamplesRemaining_ = 0;
        currentGainReduction_ = 0.0f;
        targetGainReduction_ = 0.0f;
        holdGainReduction_ = 0.0f;
        peakGainReduction_ = 0.0f;
    }

    // =========================================================================
    // Processing (FR-017, FR-018, FR-019, FR-020, FR-021, FR-022)
    // =========================================================================

    /// @brief Process a single sample pair
    /// @param main Main audio sample to process
    /// @param sidechain Sidechain sample for level detection
    /// @return Processed (ducked) main signal
    /// @pre prepare() has been called
    [[nodiscard]] float processSample(float main, float sidechain) noexcept {
        // FR-022: Handle NaN/Inf inputs
        if (detail::isNaN(main) || detail::isInf(main)) {
            main = 0.0f;
        }
        if (detail::isNaN(sidechain) || detail::isInf(sidechain)) {
            sidechain = 0.0f;
        }

        // Apply sidechain filter if enabled (FR-014, FR-015, FR-016)
        float filteredSidechain = sidechain;
        if (sidechainFilterEnabled_) {
            filteredSidechain = sidechainFilter_.process(sidechain);
        }

        // Get envelope from sidechain (FR-007)
        const float envelope = envelopeFollower_.processSample(filteredSidechain);

        // Convert envelope to dB for threshold comparison
        const float envelopeDb = gainToDb(envelope);

        // Update state machine and calculate target gain reduction
        updateStateMachine(envelopeDb);

        // Smooth gain reduction for click-free transitions (SC-004)
        gainSmoother_.setTarget(targetGainReduction_);
        const float smoothedGainReduction = gainSmoother_.process();

        // Store for metering (FR-025)
        currentGainReduction_ = smoothedGainReduction;

        // Apply gain reduction to main signal (FR-001, FR-002)
        const float gainLinear = dbToGain(smoothedGainReduction);
        return main * gainLinear;
    }

    /// @brief Process a block with separate main and sidechain buffers
    /// @param main Main audio input buffer
    /// @param sidechain Sidechain input buffer
    /// @param output Output buffer (may alias main for in-place)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    void process(const float* main, const float* sidechain,
                 float* output, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = processSample(main[i], sidechain[i]);
        }
    }

    /// @brief Process a block in-place on main buffer
    /// @param mainInOut Main audio buffer (overwritten with output)
    /// @param sidechain Sidechain input buffer
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    void process(float* mainInOut, const float* sidechain,
                 size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            mainInOut[i] = processSample(mainInOut[i], sidechain[i]);
        }
    }

    // =========================================================================
    // Parameter Setters
    // =========================================================================

    /// @brief Set threshold level (FR-003)
    /// @param dB Threshold in dB, clamped to [-60, 0]
    void setThreshold(float dB) noexcept {
        thresholdDb_ = std::clamp(dB, kMinThreshold, kMaxThreshold);
    }

    /// @brief Set ducking depth (FR-004)
    /// @param dB Depth in dB (negative value), clamped to [-48, 0]
    void setDepth(float dB) noexcept {
        depthDb_ = std::clamp(dB, kMinDepth, kMaxDepth);
    }

    /// @brief Set attack time (FR-005)
    /// @param ms Attack in milliseconds, clamped to [0.1, 500]
    void setAttackTime(float ms) noexcept {
        attackTimeMs_ = std::clamp(ms, kMinAttackMs, kMaxAttackMs);
        envelopeFollower_.setAttackTime(attackTimeMs_);
    }

    /// @brief Set release time (FR-006)
    /// @param ms Release in milliseconds, clamped to [1, 5000]
    void setReleaseTime(float ms) noexcept {
        releaseTimeMs_ = std::clamp(ms, kMinReleaseMs, kMaxReleaseMs);
        envelopeFollower_.setReleaseTime(releaseTimeMs_);
    }

    /// @brief Set hold time (FR-008)
    /// @param ms Hold in milliseconds, clamped to [0, 1000]
    void setHoldTime(float ms) noexcept {
        holdTimeMs_ = std::clamp(ms, kMinHoldMs, kMaxHoldMs);
        updateHoldSamples();
    }

    /// @brief Set range/maximum attenuation limit (FR-011)
    /// @param dB Range in dB (negative value), clamped to [-48, 0]
    /// @note 0 dB disables range limiting
    void setRange(float dB) noexcept {
        rangeDb_ = std::clamp(dB, kMinRange, kMaxRange);
    }

    /// @brief Enable or disable sidechain highpass filter (FR-015)
    /// @param enabled true to enable filter
    void setSidechainFilterEnabled(bool enabled) noexcept {
        sidechainFilterEnabled_ = enabled;
    }

    /// @brief Set sidechain filter cutoff (FR-014)
    /// @param hz Cutoff in Hz, clamped to [20, 500]
    void setSidechainFilterCutoff(float hz) noexcept {
        sidechainFilterCutoffHz_ = std::clamp(hz, kMinSidechainHz, kMaxSidechainHz);
        sidechainFilter_.configure(
            FilterType::Highpass,
            sidechainFilterCutoffHz_,
            kButterworthQ,
            0.0f,
            sampleRate_
        );
    }

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    [[nodiscard]] float getThreshold() const noexcept { return thresholdDb_; }
    [[nodiscard]] float getDepth() const noexcept { return depthDb_; }
    [[nodiscard]] float getAttackTime() const noexcept { return attackTimeMs_; }
    [[nodiscard]] float getReleaseTime() const noexcept { return releaseTimeMs_; }
    [[nodiscard]] float getHoldTime() const noexcept { return holdTimeMs_; }
    [[nodiscard]] float getRange() const noexcept { return rangeDb_; }
    [[nodiscard]] bool isSidechainFilterEnabled() const noexcept { return sidechainFilterEnabled_; }
    [[nodiscard]] float getSidechainFilterCutoff() const noexcept { return sidechainFilterCutoffHz_; }

    // =========================================================================
    // Metering (FR-025)
    // =========================================================================

    /// @brief Get current gain reduction in dB
    /// @return Gain reduction (negative when ducking, 0 when idle)
    [[nodiscard]] float getCurrentGainReduction() const noexcept {
        return currentGainReduction_;
    }

    // =========================================================================
    // Info
    // =========================================================================

    /// @brief Get processing latency in samples
    /// @return Latency (always 0 for DuckingProcessor per SC-008)
    [[nodiscard]] size_t getLatency() const noexcept {
        return 0;
    }

private:
    // =========================================================================
    // State Machine (FR-008, FR-009, FR-010)
    // =========================================================================

    void updateStateMachine(float envelopeDb) noexcept {
        const bool aboveThreshold = envelopeDb >= thresholdDb_;

        switch (state_) {
            case DuckingState::Idle:
                if (aboveThreshold) {
                    // IDLE -> DUCKING: sidechain exceeded threshold
                    state_ = DuckingState::Ducking;
                    targetGainReduction_ = calculateGainReduction(envelopeDb);
                    peakGainReduction_ = targetGainReduction_;  // Initialize peak tracking
                } else {
                    targetGainReduction_ = 0.0f;
                }
                break;

            case DuckingState::Ducking:
                if (aboveThreshold) {
                    // Still ducking - update gain reduction
                    targetGainReduction_ = calculateGainReduction(envelopeDb);
                    // Track the deepest gain reduction achieved during ducking
                    if (targetGainReduction_ < peakGainReduction_) {
                        peakGainReduction_ = targetGainReduction_;
                    }
                } else {
                    // DUCKING -> HOLDING: sidechain dropped below threshold
                    if (holdSamplesTotal_ > 0) {
                        state_ = DuckingState::Holding;
                        holdSamplesRemaining_ = holdSamplesTotal_;
                        // Use the peak gain reduction achieved during ducking
                        holdGainReduction_ = peakGainReduction_;
                        targetGainReduction_ = holdGainReduction_;
                    } else {
                        // No hold time - go directly to idle
                        state_ = DuckingState::Idle;
                        targetGainReduction_ = 0.0f;
                    }
                }
                break;

            case DuckingState::Holding:
                if (aboveThreshold) {
                    // HOLDING -> DUCKING: re-triggered during hold (FR-010)
                    state_ = DuckingState::Ducking;
                    holdSamplesRemaining_ = 0;
                    targetGainReduction_ = calculateGainReduction(envelopeDb);
                    // Reset peak tracking - start fresh for new ducking cycle
                    peakGainReduction_ = targetGainReduction_;
                } else {
                    // Continue holding - maintain gain reduction during hold
                    if (holdSamplesRemaining_ > 0) {
                        --holdSamplesRemaining_;
                        // Keep the gain reduction at the level when hold started
                        targetGainReduction_ = holdGainReduction_;
                    } else {
                        // Hold expired -> begin release (FR-009)
                        state_ = DuckingState::Idle;
                        targetGainReduction_ = 0.0f;
                    }
                }
                break;
        }
    }

    [[nodiscard]] float calculateGainReduction(float envelopeDb) const noexcept {
        // Calculate how far above threshold we are
        const float overshootDb = envelopeDb - thresholdDb_;

        // Proportional attenuation: full depth when 10+ dB above threshold
        constexpr float kFullDepthOvershoot = 10.0f;
        const float factor = std::clamp(overshootDb / kFullDepthOvershoot, 0.0f, 1.0f);

        // Target gain reduction (negative value)
        float gainReduction = depthDb_ * factor;

        // Apply range limit (FR-012, FR-013)
        // rangeDb_ is 0 or negative; more negative = more limiting
        // If rangeDb_ is 0, no limiting occurs
        if (rangeDb_ < 0.0f && gainReduction < rangeDb_) {
            gainReduction = rangeDb_;
        }

        return gainReduction;
    }

    void updateHoldSamples() noexcept {
        holdSamplesTotal_ = static_cast<size_t>(holdTimeMs_ * 0.001f * sampleRate_);
    }

    // =========================================================================
    // Parameters
    // =========================================================================

    float thresholdDb_ = kDefaultThreshold;
    float depthDb_ = kDefaultDepth;
    float attackTimeMs_ = kDefaultAttackMs;
    float releaseTimeMs_ = kDefaultReleaseMs;
    float holdTimeMs_ = kDefaultHoldMs;
    float rangeDb_ = kDefaultRange;
    bool sidechainFilterEnabled_ = false;
    float sidechainFilterCutoffHz_ = kDefaultSidechainHz;

    // =========================================================================
    // State
    // =========================================================================

    DuckingState state_ = DuckingState::Idle;
    size_t holdSamplesRemaining_ = 0;
    size_t holdSamplesTotal_ = 0;
    float currentGainReduction_ = 0.0f;
    float targetGainReduction_ = 0.0f;
    float holdGainReduction_ = 0.0f;  ///< GR level when entering hold
    float peakGainReduction_ = 0.0f; ///< Deepest GR achieved during ducking
    float sampleRate_ = 44100.0f;

    // =========================================================================
    // Composed Components
    // =========================================================================

    EnvelopeFollower envelopeFollower_;
    OnePoleSmoother gainSmoother_;
    Biquad sidechainFilter_;
};

}  // namespace DSP
}  // namespace Krate
