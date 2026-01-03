// ==============================================================================
// Layer 2: DSP Processor - Dynamics Processor (Compressor/Limiter)
// ==============================================================================
// A dynamics processing unit that uses EnvelopeFollower for level detection
// and applies gain reduction based on threshold, ratio, and knee settings.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, constexpr)
// - Principle IX: Layer 2 (depends on Layer 0-1 plus peer Layer 2 EnvelopeFollower)
// - Principle X: DSP Constraints (sample-accurate, denormal handling)
// - Principle XII: Test-First Development
//
// Reference: specs/011-dynamics-processor/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/processors/envelope_follower.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Detection Mode Enumeration
// =============================================================================

/// @brief Detection algorithm type selection for level measurement
enum class DynamicsDetectionMode : uint8_t {
    RMS = 0,    ///< RMS detection - average-responding, suits program material
    Peak = 1    ///< Peak detection - transient-responding, suits limiting
};

// =============================================================================
// DynamicsProcessor Class
// =============================================================================

/// @brief Layer 2 DSP Processor - Dynamics control (compressor/limiter)
///
/// Provides flexible dynamics processing with:
/// - Configurable threshold, ratio, and knee
/// - Attack/release timing via EnvelopeFollower
/// - Optional soft knee for transparent compression
/// - Manual or auto makeup gain
/// - RMS or Peak detection modes
/// - Optional sidechain highpass filter
/// - Optional lookahead for transparent limiting
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, pre-allocated buffers)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (depends on Layer 0-1 and peer EnvelopeFollower)
/// - Principle XII: Test-First Development
class DynamicsProcessor {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinThreshold = -60.0f;      // dB
    static constexpr float kMaxThreshold = 0.0f;        // dB
    static constexpr float kDefaultThreshold = -20.0f;  // dB

    static constexpr float kMinRatio = 1.0f;            // 1:1 (no compression)
    static constexpr float kMaxRatio = 100.0f;          // 100:1 (limiter)
    static constexpr float kDefaultRatio = 4.0f;        // 4:1

    static constexpr float kMinKnee = 0.0f;             // dB (hard knee)
    static constexpr float kMaxKnee = 24.0f;            // dB
    static constexpr float kDefaultKnee = 0.0f;         // dB (hard knee)

    static constexpr float kMinAttackMs = 0.1f;         // ms
    static constexpr float kMaxAttackMs = 500.0f;       // ms
    static constexpr float kDefaultAttackMs = 10.0f;    // ms

    static constexpr float kMinReleaseMs = 1.0f;        // ms
    static constexpr float kMaxReleaseMs = 5000.0f;     // ms
    static constexpr float kDefaultReleaseMs = 100.0f;  // ms

    static constexpr float kMinMakeupGain = -24.0f;     // dB
    static constexpr float kMaxMakeupGain = 24.0f;      // dB
    static constexpr float kDefaultMakeupGain = 0.0f;   // dB

    static constexpr float kMinLookaheadMs = 0.0f;      // ms (disabled)
    static constexpr float kMaxLookaheadMs = 10.0f;     // ms
    static constexpr float kDefaultLookaheadMs = 0.0f;  // ms (disabled)

    static constexpr float kMinSidechainHz = 20.0f;     // Hz
    static constexpr float kMaxSidechainHz = 500.0f;    // Hz
    static constexpr float kDefaultSidechainHz = 80.0f; // Hz

    // =========================================================================
    // Lifecycle (FR-024, FR-025)
    // =========================================================================

    /// @brief Default constructor
    DynamicsProcessor() noexcept = default;

    /// @brief Destructor
    ~DynamicsProcessor() = default;

    // Non-copyable, movable
    DynamicsProcessor(const DynamicsProcessor&) = delete;
    DynamicsProcessor& operator=(const DynamicsProcessor&) = delete;
    DynamicsProcessor(DynamicsProcessor&&) noexcept = default;
    DynamicsProcessor& operator=(DynamicsProcessor&&) noexcept = default;

    /// @brief Prepare processor for given sample rate
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @note Allocates lookahead buffer; call before setActive(true)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);

        // Configure envelope follower
        envelopeFollower_.prepare(sampleRate, maxBlockSize);
        envelopeFollower_.setAttackTime(attackTimeMs_);
        envelopeFollower_.setReleaseTime(releaseTimeMs_);
        updateDetectionMode();

        // Configure sidechain filter
        sidechainFilter_.configure(
            FilterType::Highpass,
            sidechainCutoffHz_,
            kButterworthQ,
            0.0f,
            sampleRate_
        );

        // Configure gain smoother (5ms smoothing time for click-free changes)
        gainSmoother_.configure(5.0f, sampleRate_);

        // Configure lookahead delay
        updateLookahead();

        reset();
    }

    /// @brief Reset internal state without reallocation
    /// @note Clears envelope, gain state, and delay line
    void reset() noexcept {
        envelopeFollower_.reset();
        sidechainFilter_.reset();
        gainSmoother_.reset();
        lookaheadDelay_.reset();
        currentGainReduction_ = 0.0f;
    }

    // =========================================================================
    // Processing (FR-001, FR-002, FR-021, FR-022, FR-023)
    // =========================================================================

    /// @brief Process a single sample
    /// @param input Input sample
    /// @return Processed (compressed) sample
    /// @pre prepare() has been called
    [[nodiscard]] float processSample(float input) noexcept {
        // Handle NaN/Inf input (FR-023)
        if (detail::isNaN(input)) {
            input = 0.0f;
        }
        if (detail::isInf(input)) {
            input = (input > 0.0f) ? 1e10f : -1e10f;
        }

        // Sidechain path: optionally filter for detection
        float detectionSample = input;
        if (sidechainEnabled_) {
            detectionSample = sidechainFilter_.process(input);
        }

        // Level detection via EnvelopeFollower
        float envelope = envelopeFollower_.processSample(detectionSample);

        // Convert envelope to dB
        float inputLevel_dB = gainToDb(envelope);

        // Compute gain reduction
        float gainReduction_dB = computeGainReduction(inputLevel_dB);

        // Smooth gain reduction to prevent clicks
        // gainReduction_dB is positive (amount to reduce by)
        gainSmoother_.setTarget(gainReduction_dB);
        float smoothedGR = gainSmoother_.process();

        // Store for metering (FR-016, FR-017) - negative value for display
        currentGainReduction_ = detail::flushDenormal(-smoothedGR);

        // Audio path: apply lookahead delay if enabled
        float audioSample = input;
        if (lookaheadSamples_ > 0) {
            audioSample = lookaheadDelay_.read(lookaheadSamples_);
            lookaheadDelay_.write(input);
        }

        // Apply gain reduction (negative dB = attenuation)
        float gainLinear = dbToGain(-smoothedGR);
        float output = audioSample * gainLinear;

        // Apply makeup gain
        float effectiveMakeup = autoMakeupEnabled_ ? calculateAutoMakeup() : makeupGain_dB_;
        output *= dbToGain(effectiveMakeup);

        return output;
    }

    /// @brief Process a block of samples in-place
    /// @param buffer Audio buffer (overwritten with processed audio)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    void process(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = processSample(buffer[i]);
        }
    }

    /// @brief Process a block with separate input/output buffers
    /// @param input Input audio buffer
    /// @param output Output audio buffer
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    void process(const float* input, float* output, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = processSample(input[i]);
        }
    }

    // =========================================================================
    // Parameter Setters
    // =========================================================================

    /// @brief Set compression threshold (FR-004)
    /// @param dB Threshold in dB, clamped to [-60, 0]
    void setThreshold(float dB) noexcept {
        threshold_dB_ = std::clamp(dB, kMinThreshold, kMaxThreshold);
        updateKneeRegion();
    }

    /// @brief Set compression ratio (FR-003)
    /// @param ratio Ratio (e.g., 4.0 for 4:1), clamped to [1, 100]
    /// @note ratio >= 100 is treated as infinity (limiter mode)
    void setRatio(float ratio) noexcept {
        ratio_ = std::clamp(ratio, kMinRatio, kMaxRatio);
    }

    /// @brief Set soft knee width (FR-008)
    /// @param dB Knee width in dB, clamped to [0, 24]
    /// @note 0 dB = hard knee
    void setKneeWidth(float dB) noexcept {
        kneeWidth_dB_ = std::clamp(dB, kMinKnee, kMaxKnee);
        updateKneeRegion();
    }

    /// @brief Set attack time (FR-005)
    /// @param ms Attack time in milliseconds, clamped to [0.1, 500]
    void setAttackTime(float ms) noexcept {
        attackTimeMs_ = std::clamp(ms, kMinAttackMs, kMaxAttackMs);
        envelopeFollower_.setAttackTime(attackTimeMs_);
    }

    /// @brief Set release time (FR-006)
    /// @param ms Release time in milliseconds, clamped to [1, 5000]
    void setReleaseTime(float ms) noexcept {
        releaseTimeMs_ = std::clamp(ms, kMinReleaseMs, kMaxReleaseMs);
        envelopeFollower_.setReleaseTime(releaseTimeMs_);
    }

    /// @brief Set manual makeup gain (FR-010)
    /// @param dB Makeup gain in dB, clamped to [-24, 24]
    void setMakeupGain(float dB) noexcept {
        makeupGain_dB_ = std::clamp(dB, kMinMakeupGain, kMaxMakeupGain);
    }

    /// @brief Enable or disable auto makeup gain (FR-011)
    /// @param enabled true to auto-calculate makeup from threshold/ratio
    void setAutoMakeup(bool enabled) noexcept {
        autoMakeupEnabled_ = enabled;
    }

    /// @brief Set detection mode (FR-012)
    /// @param mode Detection algorithm (RMS or Peak)
    void setDetectionMode(DynamicsDetectionMode mode) noexcept {
        detectionMode_ = mode;
        updateDetectionMode();
    }

    /// @brief Set lookahead time (FR-018)
    /// @param ms Lookahead in milliseconds, clamped to [0, 10]
    /// @note 0 ms = disabled (no latency)
    void setLookahead(float ms) noexcept {
        lookaheadMs_ = std::clamp(ms, kMinLookaheadMs, kMaxLookaheadMs);
        updateLookahead();
    }

    /// @brief Enable or disable sidechain highpass filter (FR-014)
    /// @param enabled true to enable sidechain filter
    void setSidechainEnabled(bool enabled) noexcept {
        sidechainEnabled_ = enabled;
    }

    /// @brief Set sidechain filter cutoff frequency (FR-014)
    /// @param hz Cutoff in Hz, clamped to [20, 500]
    void setSidechainCutoff(float hz) noexcept {
        sidechainCutoffHz_ = std::clamp(hz, kMinSidechainHz, kMaxSidechainHz);
        sidechainFilter_.configure(
            FilterType::Highpass,
            sidechainCutoffHz_,
            kButterworthQ,
            0.0f,
            sampleRate_
        );
    }

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    [[nodiscard]] float getThreshold() const noexcept { return threshold_dB_; }
    [[nodiscard]] float getRatio() const noexcept { return ratio_; }
    [[nodiscard]] float getKneeWidth() const noexcept { return kneeWidth_dB_; }
    [[nodiscard]] float getAttackTime() const noexcept { return attackTimeMs_; }
    [[nodiscard]] float getReleaseTime() const noexcept { return releaseTimeMs_; }
    [[nodiscard]] float getMakeupGain() const noexcept { return makeupGain_dB_; }
    [[nodiscard]] bool isAutoMakeupEnabled() const noexcept { return autoMakeupEnabled_; }
    [[nodiscard]] DynamicsDetectionMode getDetectionMode() const noexcept { return detectionMode_; }
    [[nodiscard]] float getLookahead() const noexcept { return lookaheadMs_; }
    [[nodiscard]] bool isSidechainEnabled() const noexcept { return sidechainEnabled_; }
    [[nodiscard]] float getSidechainCutoff() const noexcept { return sidechainCutoffHz_; }

    // =========================================================================
    // Metering (FR-016, FR-017)
    // =========================================================================

    /// @brief Get current gain reduction in dB
    /// @return Gain reduction (0 = no reduction, negative = reduction applied)
    [[nodiscard]] float getCurrentGainReduction() const noexcept {
        return currentGainReduction_;
    }

    // =========================================================================
    // Info (FR-020)
    // =========================================================================

    /// @brief Get processing latency in samples (FR-020)
    /// @return Latency (equals lookahead in samples, 0 if disabled)
    [[nodiscard]] size_t getLatency() const noexcept {
        return lookaheadSamples_;
    }

private:
    // =========================================================================
    // Gain Reduction Computation (FR-002, FR-009)
    // =========================================================================

    /// @brief Compute gain reduction in dB for a given input level
    /// @param inputLevel_dB Input level in dB (from envelope follower)
    /// @return Gain reduction in dB (always <= 0)
    [[nodiscard]] float computeGainReduction(float inputLevel_dB) const noexcept {
        // Handle ratio 1:1 (bypass) - no compression
        if (ratio_ <= 1.0f) {
            return 0.0f;
        }

        // Calculate compression slope
        const float slope = 1.0f - (1.0f / ratio_);

        // Hard knee (kneeWidth == 0)
        if (kneeWidth_dB_ <= 0.0f) {
            if (inputLevel_dB <= threshold_dB_) {
                return 0.0f;
            }
            return (inputLevel_dB - threshold_dB_) * slope;
        }

        // Soft knee with quadratic interpolation (FR-009)
        if (inputLevel_dB < kneeStart_dB_) {
            // Below knee region - no compression
            return 0.0f;
        }

        if (inputLevel_dB > kneeEnd_dB_) {
            // Above knee region - full compression
            return (inputLevel_dB - threshold_dB_) * slope;
        }

        // In knee region - quadratic interpolation
        const float x = inputLevel_dB - kneeStart_dB_;
        return slope * (x * x) / (2.0f * kneeWidth_dB_);
    }

    /// @brief Calculate auto-makeup gain (FR-011)
    /// @return Makeup gain in dB to compensate for compression at 0 dB input
    [[nodiscard]] float calculateAutoMakeup() const noexcept {
        if (ratio_ <= 1.0f) {
            return 0.0f;
        }
        // Compensate for gain reduction that would occur at 0 dB input
        return -threshold_dB_ * (1.0f - 1.0f / ratio_);
    }

    /// @brief Update knee region boundaries
    void updateKneeRegion() noexcept {
        kneeStart_dB_ = threshold_dB_ - kneeWidth_dB_ * 0.5f;
        kneeEnd_dB_ = threshold_dB_ + kneeWidth_dB_ * 0.5f;
    }

    /// @brief Update detection mode in envelope follower
    void updateDetectionMode() noexcept {
        switch (detectionMode_) {
            case DynamicsDetectionMode::RMS:
                envelopeFollower_.setMode(DetectionMode::RMS);
                break;
            case DynamicsDetectionMode::Peak:
                envelopeFollower_.setMode(DetectionMode::Peak);
                break;
        }
    }

    /// @brief Update lookahead delay configuration
    void updateLookahead() noexcept {
        if (sampleRate_ > 0.0f) {
            lookaheadSamples_ = static_cast<size_t>(lookaheadMs_ * 0.001f * sampleRate_);
            if (lookaheadSamples_ > 0) {
                // Allocate delay line for lookahead (max 10ms)
                lookaheadDelay_.prepare(static_cast<double>(sampleRate_), kMaxLookaheadMs * 0.001f);
            }
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Parameters
    float threshold_dB_ = kDefaultThreshold;
    float ratio_ = kDefaultRatio;
    float kneeWidth_dB_ = kDefaultKnee;
    float attackTimeMs_ = kDefaultAttackMs;
    float releaseTimeMs_ = kDefaultReleaseMs;
    float makeupGain_dB_ = kDefaultMakeupGain;
    bool autoMakeupEnabled_ = false;
    DynamicsDetectionMode detectionMode_ = DynamicsDetectionMode::RMS;
    float lookaheadMs_ = kDefaultLookaheadMs;
    bool sidechainEnabled_ = false;
    float sidechainCutoffHz_ = kDefaultSidechainHz;

    // Derived values
    float kneeStart_dB_ = kDefaultThreshold - kDefaultKnee * 0.5f;
    float kneeEnd_dB_ = kDefaultThreshold + kDefaultKnee * 0.5f;
    size_t lookaheadSamples_ = 0;

    // State
    float currentGainReduction_ = 0.0f;
    float sampleRate_ = 44100.0f;

    // Components
    EnvelopeFollower envelopeFollower_;
    OnePoleSmoother gainSmoother_;
    DelayLine lookaheadDelay_;
    Biquad sidechainFilter_;
};

}  // namespace DSP
}  // namespace Krate
