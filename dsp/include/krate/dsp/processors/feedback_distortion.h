// ==============================================================================
// Layer 2: DSP Processor - Feedback Distortion
// ==============================================================================
// Controlled feedback runaway distortion processor with limiting for sustained,
// singing distortion effects. Implements a feedback delay loop with saturation
// and soft limiting.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, value semantics)
// - Principle IX: Layer 2 (depends on Layer 0-1 plus peer EnvelopeFollower)
// - Principle X: DSP Constraints (soft limiting for feedback > 100%, DC blocking)
// - Principle XII: Test-First Development
//
// Reference: specs/110-feedback-distortion/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/waveshaper.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/envelope_follower.h>
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

/// @brief Layer 2 DSP Processor - Feedback distortion with controlled runaway.
///
/// Creates sustained, singing distortion by running audio through a feedback
/// delay loop with saturation. When feedback >= 1.0, the signal grows unbounded;
/// a soft limiter catches this runaway to create "controlled chaos" - indefinite
/// sustain at a bounded level.
///
/// @par Features
/// - Feedback delay time 1-100ms (controls resonance pitch)
/// - Feedback 0-150% (>100% causes runaway behavior)
/// - Selectable saturation curves (Tanh, Tube, Diode, etc.)
/// - Soft limiter with configurable threshold
/// - Tone filter (lowpass) in feedback path
/// - DC blocking after asymmetric saturation
/// - All parameters smoothed for click-free changes
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (depends on Layers 0-1 + EnvelopeFollower)
/// - Principle X: DSP Constraints (soft limiting, DC blocking)
///
/// @par Signal Flow
/// @code
/// Input -> [+] -> DelayLine -> Waveshaper -> Biquad -> DCBlocker -> SoftLimiter -> Output
///           ^                                                            |
///           +------------------------ * feedback ------------------------+
/// @endcode
///
/// @par Usage Example
/// @code
/// FeedbackDistortion distortion;
/// distortion.prepare(44100.0, 512);
///
/// // Singing distortion with natural decay
/// distortion.setDelayTime(10.0f);      // 100Hz resonance
/// distortion.setFeedback(0.8f);        // Decays naturally
/// distortion.setDrive(2.0f);
/// distortion.setSaturationCurve(WaveshapeType::Tanh);
///
/// // Controlled runaway (drone mode)
/// distortion.setFeedback(1.2f);        // Self-sustaining
/// distortion.setLimiterThreshold(-6.0f);
/// @endcode
///
/// @see specs/110-feedback-distortion/spec.md
class FeedbackDistortion {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// @name Delay Time Parameters (FR-004, FR-005)
    /// @{
    static constexpr float kMinDelayMs = 1.0f;      ///< Minimum delay time
    static constexpr float kMaxDelayMs = 100.0f;    ///< Maximum delay time
    static constexpr float kDefaultDelayMs = 10.0f; ///< Default: 100Hz resonance
    /// @}

    /// @name Feedback Parameters (FR-007, FR-008)
    /// @{
    static constexpr float kMinFeedback = 0.0f;     ///< Minimum feedback (no resonance)
    static constexpr float kMaxFeedback = 1.5f;     ///< Maximum feedback (aggressive runaway)
    static constexpr float kDefaultFeedback = 0.8f; ///< Default: natural decay
    /// @}

    /// @name Drive Parameters (FR-013, FR-014)
    /// @{
    static constexpr float kMinDrive = 0.1f;        ///< Minimum drive
    static constexpr float kMaxDrive = 10.0f;       ///< Maximum drive
    static constexpr float kDefaultDrive = 1.0f;    ///< Default: unity
    /// @}

    /// @name Limiter Parameters (FR-016, FR-017)
    /// @{
    static constexpr float kMinThresholdDb = -24.0f;   ///< Minimum threshold
    static constexpr float kMaxThresholdDb = 0.0f;     ///< Maximum threshold
    static constexpr float kDefaultThresholdDb = -6.0f;///< Default threshold
    /// @}

    /// @name Tone Filter Parameters (FR-020, FR-022)
    /// @{
    static constexpr float kMinToneHz = 20.0f;         ///< Minimum tone frequency
    static constexpr float kMaxToneHz = 20000.0f;      ///< Maximum tone frequency
    static constexpr float kDefaultToneHz = 5000.0f;   ///< Default: mild filtering
    /// @}

    /// @name Internal Constants
    /// @{
    static constexpr float kLimiterAttackMs = 0.5f;    ///< Fast attack (FR-019a)
    static constexpr float kLimiterReleaseMs = 50.0f;  ///< Natural release (FR-019b)
    static constexpr float kSmoothingTimeMs = 10.0f;   ///< Parameter smoothing (FR-006)
    static constexpr float kDCBlockerCutoffHz = 10.0f; ///< DC blocker cutoff
    /// @}

    // =========================================================================
    // Construction
    // =========================================================================

    /// @brief Default constructor.
    /// Initializes with default parameters. prepare() must be called before processing.
    FeedbackDistortion() noexcept = default;

    // Non-copyable (contains component state)
    FeedbackDistortion(const FeedbackDistortion&) = delete;
    FeedbackDistortion& operator=(const FeedbackDistortion&) = delete;

    // Movable
    FeedbackDistortion(FeedbackDistortion&&) noexcept = default;
    FeedbackDistortion& operator=(FeedbackDistortion&&) noexcept = default;

    ~FeedbackDistortion() = default;

    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Prepare processor for given sample rate. (FR-001)
    ///
    /// Initializes all components (delay line, filters, smoothers, envelope).
    /// Must be called before any processing and when sample rate changes.
    ///
    /// @param sampleRate Sample rate in Hz (44100-192000 supported per FR-003)
    /// @param maxBlockSize Maximum samples per process() call (unused, for API consistency)
    /// @pre sampleRate > 0
    /// @post ready for processing via process()
    /// @note NOT real-time safe (may allocate)
    void prepare(double sampleRate, [[maybe_unused]] size_t maxBlockSize) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);

        // Initialize delay line: 0.1 seconds max at any sample rate
        delayLine_.prepare(sampleRate, 0.1f);

        // Configure smoothers with 10ms time constant (FR-006, FR-010, FR-015, FR-023)
        delayTimeSmoother_.configure(kSmoothingTimeMs, sampleRate_);
        feedbackSmoother_.configure(kSmoothingTimeMs, sampleRate_);
        driveSmoother_.configure(kSmoothingTimeMs, sampleRate_);
        thresholdSmoother_.configure(kSmoothingTimeMs, sampleRate_);
        toneFreqSmoother_.configure(kSmoothingTimeMs, sampleRate_);

        // Snap smoothers to initial values
        delayTimeSmoother_.snapTo(delayTimeMs_);
        feedbackSmoother_.snapTo(feedback_);
        driveSmoother_.snapTo(drive_);
        thresholdSmoother_.snapTo(limiterThresholdLinear_);
        toneFreqSmoother_.snapTo(toneFrequencyHz_);

        // Configure tone filter as lowpass with Butterworth Q (FR-021, FR-021a)
        toneFilter_.configure(
            FilterType::Lowpass,
            toneFrequencyHz_,
            kButterworthQ,
            0.0f,
            sampleRate_
        );

        // Configure DC blocker (FR-028)
        dcBlocker_.prepare(sampleRate, kDCBlockerCutoffHz);

        // Configure envelope follower for limiter (FR-019a, FR-019b)
        limiterEnvelope_.prepare(sampleRate, 1);  // maxBlockSize=1 for sample-by-sample
        limiterEnvelope_.setMode(DetectionMode::Peak);
        limiterEnvelope_.setAttackTime(kLimiterAttackMs);
        limiterEnvelope_.setReleaseTime(kLimiterReleaseMs);

        // Initialize limiter threshold linear value
        limiterThresholdLinear_ = dbToGain(limiterThresholdDb_);

        prepared_ = true;
        reset();
    }

    /// @brief Reset all internal state without reallocation. (FR-002)
    ///
    /// Clears delay line, filters, envelope, and feedback state.
    /// Call when starting new audio stream or after discontinuity.
    ///
    /// @note Real-time safe (no allocation)
    void reset() noexcept {
        delayLine_.reset();
        toneFilter_.reset();
        dcBlocker_.reset();
        limiterEnvelope_.reset();
        feedbackSample_ = 0.0f;
    }

    // =========================================================================
    // Processing (FR-024, FR-025, FR-026, FR-027, FR-028, FR-029)
    // =========================================================================

    /// @brief Process a single sample. (FR-024)
    ///
    /// @param x Input sample
    /// @return Processed output sample
    /// @pre prepare() has been called
    /// @note Returns 0.0 if input is NaN/Inf (FR-026)
    /// @note Real-time safe: noexcept, no allocation (FR-025)
    [[nodiscard]] float process(float x) noexcept {
        // FR-026: NaN/Inf check - reset state and return 0.0
        if (detail::isNaN(x) || detail::isInf(x)) {
            reset();
            return 0.0f;
        }

        // Get smoothed parameter values
        const float smoothedDelayMs = delayTimeSmoother_.process();
        const float smoothedFeedback = feedbackSmoother_.process();
        const float smoothedDrive = driveSmoother_.process();
        const float smoothedThreshold = thresholdSmoother_.process();
        const float smoothedToneFreq = toneFreqSmoother_.process();

        // Convert delay time from ms to samples
        const float delaySamples = smoothedDelayMs * sampleRate_ * 0.001f;

        // Update tone filter frequency if it has changed significantly
        if (std::abs(smoothedToneFreq - lastToneFreq_) > 0.1f) {
            toneFilter_.configure(
                FilterType::Lowpass,
                smoothedToneFreq,
                kButterworthQ,
                0.0f,
                sampleRate_
            );
            lastToneFreq_ = smoothedToneFreq;
        }

        // Feedback comb filter topology for resonance at f = 1000/delayMs Hz:
        // 1. Read delayed output (feedback signal)
        // 2. Process the feedback through saturation/filter/limiter
        // 3. Write input + processed feedback to delay line
        // 4. Return processed output

        // Read from delay line - this is the feedback signal
        float delayed = delayLine_.readLinear(delaySamples);

        // Apply saturation with smoothed drive (FR-013)
        saturation_.setDrive(smoothedDrive);
        float saturated = saturation_.process(delayed);

        // Apply tone filter (lowpass) - FR-020
        float filtered = toneFilter_.process(saturated);

        // Apply DC blocker to remove asymmetric saturation DC (FR-028)
        float dcBlocked = dcBlocker_.process(filtered);

        // Soft limiter (FR-019, FR-019c, FR-030)
        // Uses tanh-based soft clipping to keep output within threshold + 3dB
        float processed = dcBlocked;
        float absLevel = std::abs(dcBlocked);

        if (absLevel > smoothedThreshold && smoothedThreshold > 0.0f) {
            // Calculate how much we're over the threshold
            float overAmount = absLevel - smoothedThreshold;
            float normalizedOver = overAmount / smoothedThreshold;

            // Soft knee compression using tanh
            // This maps values above threshold to a compressed range
            // tanh(x) approaches 1 as x increases, giving us soft limiting
            float compressedOver = smoothedThreshold * 0.41f * std::tanh(normalizedOver * 2.0f);

            // New level is threshold + compressed overshoot
            // The 0.41f factor ensures we stay within +3dB of threshold (1.41x)
            float newLevel = smoothedThreshold + compressedOver;

            // Apply gain reduction
            processed = dcBlocked * (newLevel / absLevel);
        }

        // FR-027: Flush denormals to prevent CPU spikes
        processed = detail::flushDenormal(processed);

        // Write input + feedback to delay line
        float feedbackSignal = processed * smoothedFeedback;
        feedbackSignal = detail::flushDenormal(feedbackSignal);
        delayLine_.write(x + feedbackSignal);

        // Output is the processed feedback signal mixed with input
        // For this effect, we want to hear the resonating feedback, not the dry input
        return processed;
    }

    /// @brief Process a block of samples in-place. (FR-024)
    ///
    /// Equivalent to calling process() for each sample sequentially.
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param n Number of samples in buffer
    /// @pre prepare() has been called
    /// @note Real-time safe: noexcept, no allocation
    void process(float* buffer, size_t n) noexcept {
        for (size_t i = 0; i < n; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    // =========================================================================
    // Delay Time (FR-004, FR-005, FR-006)
    // =========================================================================

    /// @brief Set feedback delay time. (FR-004)
    ///
    /// Controls the fundamental frequency of the resonance: f = 1000 / delayMs Hz.
    ///
    /// @param ms Delay time in milliseconds, clamped to [1.0, 100.0] (FR-005)
    /// @note Changes are smoothed over 10ms (FR-006)
    void setDelayTime(float ms) noexcept {
        delayTimeMs_ = std::clamp(ms, kMinDelayMs, kMaxDelayMs);
        delayTimeSmoother_.setTarget(delayTimeMs_);
    }

    /// @brief Get current delay time setting.
    [[nodiscard]] float getDelayTime() const noexcept {
        return delayTimeMs_;
    }

    // =========================================================================
    // Feedback (FR-007, FR-008, FR-009, FR-010)
    // =========================================================================

    /// @brief Set feedback amount. (FR-007)
    ///
    /// - Below 1.0: Signal decays naturally
    /// - At 1.0: Signal sustains indefinitely (FR-009)
    /// - Above 1.0: Signal grows (runaway behavior, caught by limiter)
    ///
    /// @param amount Feedback amount, clamped to [0.0, 1.5] (FR-008)
    /// @note Changes are smoothed over 10ms (FR-010)
    void setFeedback(float amount) noexcept {
        feedback_ = std::clamp(amount, kMinFeedback, kMaxFeedback);
        feedbackSmoother_.setTarget(feedback_);
    }

    /// @brief Get current feedback setting.
    [[nodiscard]] float getFeedback() const noexcept {
        return feedback_;
    }

    // =========================================================================
    // Saturation (FR-011, FR-012, FR-013, FR-014, FR-015)
    // =========================================================================

    /// @brief Set saturation curve type. (FR-011)
    ///
    /// @param type Waveshape type (all WaveshapeType values supported per FR-012)
    void setSaturationCurve(WaveshapeType type) noexcept {
        saturationCurve_ = type;
        saturation_.setType(type);
    }

    /// @brief Get current saturation curve.
    [[nodiscard]] WaveshapeType getSaturationCurve() const noexcept {
        return saturationCurve_;
    }

    /// @brief Set saturation drive amount. (FR-013)
    ///
    /// @param drive Drive amount, clamped to [0.1, 10.0] (FR-014)
    /// @note Changes are smoothed over 10ms (FR-015)
    void setDrive(float drive) noexcept {
        drive_ = std::clamp(drive, kMinDrive, kMaxDrive);
        driveSmoother_.setTarget(drive_);
    }

    /// @brief Get current drive setting.
    [[nodiscard]] float getDrive() const noexcept {
        return drive_;
    }

    // =========================================================================
    // Limiter (FR-016, FR-017, FR-018, FR-019)
    // =========================================================================

    /// @brief Set limiter threshold. (FR-016)
    ///
    /// The limiter catches feedback runaway. Output peaks stay within
    /// threshold + 3dB (FR-030).
    ///
    /// @param dB Threshold in dB, clamped to [-24.0, 0.0] (FR-017)
    void setLimiterThreshold(float dB) noexcept {
        limiterThresholdDb_ = std::clamp(dB, kMinThresholdDb, kMaxThresholdDb);
        limiterThresholdLinear_ = dbToGain(limiterThresholdDb_);
        thresholdSmoother_.setTarget(limiterThresholdLinear_);
    }

    /// @brief Get current limiter threshold.
    [[nodiscard]] float getLimiterThreshold() const noexcept {
        return limiterThresholdDb_;
    }

    // =========================================================================
    // Tone Filter (FR-020, FR-021, FR-022, FR-023)
    // =========================================================================

    /// @brief Set tone filter frequency. (FR-020)
    ///
    /// Lowpass filter in feedback path shapes sustained tone character.
    /// Uses Butterworth Q (0.707) for neutral response (FR-021a).
    ///
    /// @param hz Cutoff frequency, clamped to [20.0, min(20000.0, sampleRate*0.45)] (FR-022)
    /// @note Changes are smoothed over 10ms (FR-023)
    void setToneFrequency(float hz) noexcept {
        // Clamp to valid range considering Nyquist (FR-022)
        const float maxFreq = std::min(kMaxToneHz, sampleRate_ * 0.45f);
        toneFrequencyHz_ = std::clamp(hz, kMinToneHz, maxFreq);
        toneFreqSmoother_.setTarget(toneFrequencyHz_);
    }

    /// @brief Get current tone filter frequency.
    [[nodiscard]] float getToneFrequency() const noexcept {
        return toneFrequencyHz_;
    }

    // =========================================================================
    // Info (SC-007)
    // =========================================================================

    /// @brief Get processing latency in samples. (SC-007)
    /// @return Always 0 (no lookahead required)
    [[nodiscard]] constexpr size_t getLatency() const noexcept { return 0; }

private:
    // =========================================================================
    // Components (Layer 1 + Layer 2)
    // =========================================================================

    DelayLine delayLine_;               ///< Feedback delay path
    Waveshaper saturation_;             ///< Saturation in feedback
    Biquad toneFilter_;                 ///< Lowpass tone control
    DCBlocker dcBlocker_;               ///< DC offset removal
    EnvelopeFollower limiterEnvelope_;  ///< Level tracking for soft limiter

    // =========================================================================
    // Parameter Smoothers (10ms time constant per FR-006, FR-010, FR-015, FR-023)
    // =========================================================================

    OnePoleSmoother delayTimeSmoother_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother driveSmoother_;
    OnePoleSmoother thresholdSmoother_;
    OnePoleSmoother toneFreqSmoother_;

    // =========================================================================
    // Parameters (target values)
    // =========================================================================

    float delayTimeMs_ = kDefaultDelayMs;
    float feedback_ = kDefaultFeedback;
    float drive_ = kDefaultDrive;
    float limiterThresholdDb_ = kDefaultThresholdDb;
    float toneFrequencyHz_ = kDefaultToneHz;
    WaveshapeType saturationCurve_ = WaveshapeType::Tanh;

    // =========================================================================
    // Cached / Derived Values
    // =========================================================================

    float limiterThresholdLinear_ = 0.5f;  ///< dbToGain(kDefaultThresholdDb)
    float sampleRate_ = 44100.0f;

    // =========================================================================
    // State
    // =========================================================================

    float feedbackSample_ = 0.0f;  ///< Previous output for feedback (unused after refactor)
    float lastToneFreq_ = 0.0f;   ///< Last configured tone filter frequency
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
