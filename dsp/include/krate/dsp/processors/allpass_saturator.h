// ==============================================================================
// Layer 2: DSP Processor - Allpass-Saturator Network
// ==============================================================================
// Resonant distortion processor using allpass filters with saturation in
// feedback loops. Creates pitched, self-oscillating resonances that can be
// excited by input audio.
//
// Feature: 109-allpass-saturator-network
// Layer: 2 (DSP Processors)
// Dependencies:
//   - Layer 1: Biquad, DelayLine, Waveshaper, DCBlocker, OnePoleSmoother, OnePoleLP
//   - Layer 0: math_constants.h, db_utils.h, sigmoid.h
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 2 (depends only on Layer 0/1)
// - Principle X: DSP Constraints (saturation, DC blocking, feedback limiting)
// - Principle XII: Test-First Development
//
// Reference: specs/109-allpass-saturator-network/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/sigmoid.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/one_pole.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/waveshaper.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// NetworkTopology Enumeration (FR-004 to FR-008)
// =============================================================================

/// @brief Available network topology configurations.
///
/// Each topology creates different resonant characteristics:
/// - SingleAllpass: Pitched resonance at specified frequency
/// - AllpassChain: Inharmonic, bell-like tones from cascaded stages
/// - KarplusStrong: Plucked string synthesis
/// - FeedbackMatrix: Dense, evolving textures with cross-channel interaction
enum class NetworkTopology : uint8_t {
    SingleAllpass = 0,   ///< Single allpass + saturator feedback loop
    AllpassChain = 1,    ///< 4 cascaded allpasses at prime frequency ratios
    KarplusStrong = 2,   ///< Delay + lowpass + saturator (string synthesis)
    FeedbackMatrix = 3   ///< 4x4 Householder matrix of cross-fed saturators
};

// =============================================================================
// AllpassSaturator Class (FR-001 to FR-030)
// =============================================================================

/// @brief Resonant distortion processor using allpass filters with saturation.
///
/// Creates pitched, self-oscillating resonances that can be excited by input.
/// Supports four topologies for different timbral characteristics.
///
/// @par Signal Flow (varies by topology)
/// @code
/// SingleAllpass:
///   input -> [+] -> [allpass] -> [saturator] -> [soft clip] -> output
///             ^                                      |
///             |_______ feedback * gain _____________|
///
/// KarplusStrong:
///   input -> [delay] -> [saturator] -> [1-pole LP] -> [soft clip] -> output
///              ^                                          |
///              |__________ feedback _____________________|
/// @endcode
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
/// - Principle IX: Layer 2 (depends only on Layer 0/1)
/// - Principle X: DSP Constraints (saturation, DC blocking, feedback < 100%)
///
/// @see specs/109-allpass-saturator-network/spec.md
class AllpassSaturator {
public:
    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor.
    AllpassSaturator() noexcept = default;

    /// @brief Destructor.
    ~AllpassSaturator() = default;

    // Non-copyable (contains delay buffers)
    AllpassSaturator(const AllpassSaturator&) = delete;
    AllpassSaturator& operator=(const AllpassSaturator&) = delete;

    // Movable
    AllpassSaturator(AllpassSaturator&&) noexcept = default;
    AllpassSaturator& operator=(AllpassSaturator&&) noexcept = default;

    /// @brief Prepare the processor for processing.
    ///
    /// Allocates internal buffers and initializes components.
    /// Must be called before process().
    ///
    /// @param sampleRate Sample rate in Hz (44100-192000)
    /// @param maxBlockSize Maximum samples per process() call (unused, for API consistency)
    ///
    /// @post prepared_ = true, ready for process() calls
    /// @note FR-001, FR-003: Supports 44100Hz to 192000Hz
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Clear all internal state without reallocation.
    ///
    /// Use when starting new audio to prevent artifacts from previous processing.
    /// Does not change parameter values.
    ///
    /// @note FR-002: Resets all delay buffers, filter states, and feedback values
    void reset() noexcept;

    // =========================================================================
    // Topology Selection (FR-004 to FR-009)
    // =========================================================================

    /// @brief Set the network topology configuration.
    ///
    /// Changes take effect immediately. State is reset to prevent artifacts.
    ///
    /// @param topology Network configuration to use
    ///
    /// @note FR-004: Immediate topology change
    /// @note FR-009: Resets state on topology change
    void setTopology(NetworkTopology topology) noexcept;

    /// @brief Get the current topology.
    /// @return Current network topology
    [[nodiscard]] NetworkTopology getTopology() const noexcept;

    // =========================================================================
    // Frequency Control (FR-010 to FR-012)
    // =========================================================================

    /// @brief Set the resonant frequency.
    ///
    /// For most topologies, this sets the pitch of the resonance.
    /// For AllpassChain, sets the base frequency (stages at f, 1.5f, 2.33f, 3.67f).
    ///
    /// @param hz Frequency in Hz (clamped to [20, sampleRate * 0.45])
    ///
    /// @note FR-010, FR-011: Clamped to valid range
    /// @note FR-012: 10ms smoothing for click-free changes
    void setFrequency(float hz) noexcept;

    /// @brief Get the current frequency setting.
    /// @return Target frequency in Hz
    [[nodiscard]] float getFrequency() const noexcept;

    // =========================================================================
    // Feedback Control (FR-013 to FR-016)
    // =========================================================================

    /// @brief Set the feedback amount.
    ///
    /// Controls resonance intensity and sustain:
    /// - 0.0 = no feedback (single pass through)
    /// - 0.5 = moderate resonance
    /// - 0.9+ = self-oscillation with input excitation
    ///
    /// @param feedback Feedback amount (clamped to [0.0, 0.999])
    ///
    /// @note FR-013: Valid range 0.0 to 1.0
    /// @note FR-014: Values > 0.9 enable self-oscillation
    /// @note FR-015: Soft clipping at +/-2.0 prevents unbounded growth
    /// @note FR-016: 10ms smoothing for click-free changes
    void setFeedback(float feedback) noexcept;

    /// @brief Get the current feedback setting.
    /// @return Target feedback amount
    [[nodiscard]] float getFeedback() const noexcept;

    // =========================================================================
    // Saturation Control (FR-017 to FR-020)
    // =========================================================================

    /// @brief Set the saturation transfer function.
    ///
    /// @param type Waveshape algorithm to use (from Waveshaper primitive)
    ///
    /// @note FR-017, FR-018: Supports all WaveshapeType values
    void setSaturationCurve(WaveshapeType type) noexcept;

    /// @brief Get the current saturation curve.
    /// @return Current waveshape type
    [[nodiscard]] WaveshapeType getSaturationCurve() const noexcept;

    /// @brief Set the saturation drive amount.
    ///
    /// Controls saturation intensity:
    /// - 0.1 = subtle warmth
    /// - 1.0 = moderate saturation
    /// - 10.0 = aggressive distortion
    ///
    /// @param drive Drive amount (clamped to [0.1, 10.0])
    ///
    /// @note FR-019: Valid range 0.1 to 10.0
    /// @note FR-020: 10ms smoothing for click-free changes
    void setDrive(float drive) noexcept;

    /// @brief Get the current drive setting.
    /// @return Target drive amount
    [[nodiscard]] float getDrive() const noexcept;

    // =========================================================================
    // Karplus-Strong Specific (FR-021 to FR-023)
    // =========================================================================

    /// @brief Set the decay time for KarplusStrong topology.
    ///
    /// Controls how long the string resonates after excitation.
    /// Only affects KarplusStrong topology; ignored for others.
    ///
    /// @param seconds Decay time in seconds (RT60)
    ///
    /// @note FR-021, FR-022: Only affects KarplusStrong
    /// @note FR-023: Converted to lowpass cutoff for string-like decay
    void setDecay(float seconds) noexcept;

    /// @brief Get the current decay setting.
    /// @return Target decay time in seconds
    [[nodiscard]] float getDecay() const noexcept;

    // =========================================================================
    // Processing (FR-024 to FR-030)
    // =========================================================================

    /// @brief Process a single sample.
    ///
    /// @param input Input sample
    /// @return Processed output sample
    ///
    /// @note FR-024: In-place block processing available
    /// @note FR-025: Real-time safe (no allocations)
    /// @note FR-026: Handles NaN/Inf by resetting and returning 0
    /// @note FR-027: Flushes denormals
    /// @note FR-028: DC blocking after saturation
    /// @note FR-029, FR-030: Bounded output via soft clipping
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place.
    ///
    /// @param buffer Audio buffer (modified in place)
    /// @param numSamples Number of samples to process
    ///
    /// @note Equivalent to calling process() for each sample
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// @brief Check if processor has been prepared.
    /// @return true if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept;

    /// @brief Get the current sample rate.
    /// @return Sample rate in Hz, or 0 if not prepared
    [[nodiscard]] double getSampleRate() const noexcept;

    // =========================================================================
    // Public Internal Types (exposed for testing)
    // =========================================================================

    /// @brief 4x4 unitary Householder feedback matrix for FeedbackMatrix topology.
    struct HouseholderMatrix {
        /// Apply Householder reflection to 4-element vector.
        /// H = I - 2vv^T where v = [0.5, 0.5, 0.5, 0.5]
        /// Matrix form:
        /// | -0.5  0.5  0.5  0.5 |
        /// |  0.5 -0.5  0.5  0.5 |
        /// |  0.5  0.5 -0.5  0.5 |
        /// |  0.5  0.5  0.5 -0.5 |
        static void multiply(const float in[4], float out[4]) noexcept {
            const float sum = in[0] + in[1] + in[2] + in[3];
            out[0] = 0.5f * (sum - 2.0f * in[0]);
            out[1] = 0.5f * (sum - 2.0f * in[1]);
            out[2] = 0.5f * (sum - 2.0f * in[2]);
            out[3] = 0.5f * (sum - 2.0f * in[3]);
        }
    };

private:
    // =========================================================================
    // Internal Types
    // =========================================================================

    /// @brief Single allpass filter with saturation in the feedback loop.
    class SaturatedAllpassStage {
    public:
        void prepare(double sampleRate) noexcept;
        void reset() noexcept;
        void setFrequency(float hz, float sampleRate) noexcept;
        void setDrive(float drive) noexcept;
        void setSaturationCurve(WaveshapeType type) noexcept;
        [[nodiscard]] float process(float input, float feedbackGain) noexcept;

    private:
        Biquad allpass_;
        Waveshaper waveshaper_;
        float lastOutput_ = 0.0f;
    };

    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Soft clip feedback signal to +/-2.0 range.
    /// Uses tanh(x * 0.5) * 2.0 for gradual compression.
    [[nodiscard]] static float softClipFeedback(float x) noexcept {
        return Sigmoid::tanh(x * 0.5f) * 2.0f;
    }

    /// @brief Clamp frequency to valid range [20Hz, sampleRate * 0.45].
    [[nodiscard]] float clampFrequency(float hz) const noexcept {
        const float maxFreq = static_cast<float>(sampleRate_) * 0.45f;
        return std::clamp(hz, 20.0f, maxFreq);
    }

    /// @brief Calculate lowpass cutoff and feedback for KarplusStrong decay.
    void decayToFeedbackAndCutoff(float decaySeconds, float frequency,
                                   float& outFeedback, float& outCutoff) const noexcept;

    /// @brief Process SingleAllpass topology.
    [[nodiscard]] float processSingleAllpass(float input) noexcept;

    /// @brief Process AllpassChain topology.
    [[nodiscard]] float processAllpassChain(float input) noexcept;

    /// @brief Process KarplusStrong topology.
    [[nodiscard]] float processKarplusStrong(float input) noexcept;

    /// @brief Process FeedbackMatrix topology.
    [[nodiscard]] float processFeedbackMatrix(float input) noexcept;

    /// @brief Update allpass chain frequencies based on base frequency.
    void updateChainFrequencies() noexcept;

    /// @brief Update matrix stage frequencies with slight detuning.
    void updateMatrixFrequencies() noexcept;

    // =========================================================================
    // Configuration
    // =========================================================================

    NetworkTopology topology_ = NetworkTopology::SingleAllpass;
    double sampleRate_ = 44100.0;
    float frequency_ = 440.0f;
    float feedback_ = 0.5f;
    float drive_ = 1.0f;
    float decay_ = 1.0f;
    WaveshapeType saturationCurve_ = WaveshapeType::Tanh;
    bool prepared_ = false;

    // =========================================================================
    // Parameter Smoothers (10ms time constant)
    // =========================================================================

    OnePoleSmoother frequencySmoother_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother driveSmoother_;

    // =========================================================================
    // Shared Components
    // =========================================================================

    DCBlocker dcBlocker_;

    // =========================================================================
    // SingleAllpass Components
    // =========================================================================

    SaturatedAllpassStage singleStage_;

    // =========================================================================
    // AllpassChain Components (4 stages at prime frequency ratios)
    // =========================================================================

    static constexpr std::array<float, 4> kChainFrequencyRatios = {1.0f, 1.5f, 2.33f, 3.67f};
    std::array<Biquad, 4> chainAllpasses_;
    Waveshaper chainWaveshaper_;
    float chainLastOutput_ = 0.0f;

    // =========================================================================
    // KarplusStrong Components
    // =========================================================================

    DelayLine ksDelay_;
    OnePoleLP ksLowpass_;
    Waveshaper ksWaveshaper_;
    float ksLastOutput_ = 0.0f;

    // =========================================================================
    // FeedbackMatrix Components (4x4 Householder)
    // =========================================================================

    static constexpr std::array<float, 4> kMatrixDetuneRatios = {1.0f, 1.003f, 0.997f, 1.005f};
    std::array<SaturatedAllpassStage, 4> matrixStages_;
    std::array<float, 4> matrixLastOutputs_ = {0.0f, 0.0f, 0.0f, 0.0f};
};

// =============================================================================
// Inline Implementation
// =============================================================================

// -----------------------------------------------------------------------------
// SaturatedAllpassStage
// -----------------------------------------------------------------------------

inline void AllpassSaturator::SaturatedAllpassStage::prepare(double sampleRate) noexcept {
    // Configure allpass with default Q
    allpass_.configure(FilterType::Allpass, 440.0f, 10.0f, 0.0f, static_cast<float>(sampleRate));
    waveshaper_.setType(WaveshapeType::Tanh);
    waveshaper_.setDrive(1.0f);
    lastOutput_ = 0.0f;
}

inline void AllpassSaturator::SaturatedAllpassStage::reset() noexcept {
    allpass_.reset();
    lastOutput_ = 0.0f;
}

inline void AllpassSaturator::SaturatedAllpassStage::setFrequency(float hz, float sampleRate) noexcept {
    // High Q for sharp resonance
    allpass_.configure(FilterType::Allpass, hz, 10.0f, 0.0f, sampleRate);
}

inline void AllpassSaturator::SaturatedAllpassStage::setDrive(float drive) noexcept {
    waveshaper_.setDrive(drive);
}

inline void AllpassSaturator::SaturatedAllpassStage::setSaturationCurve(WaveshapeType type) noexcept {
    waveshaper_.setType(type);
}

inline float AllpassSaturator::SaturatedAllpassStage::process(float input, float feedbackGain) noexcept {
    // Add feedback to input
    const float feedbackedInput = input + lastOutput_ * feedbackGain;

    // Process through allpass
    const float allpassed = allpass_.process(feedbackedInput);

    // Apply saturation
    const float saturated = waveshaper_.process(allpassed);

    // Soft clip to prevent runaway
    lastOutput_ = softClipFeedback(saturated);

    return lastOutput_;
}

// -----------------------------------------------------------------------------
// AllpassSaturator Lifecycle
// -----------------------------------------------------------------------------

inline void AllpassSaturator::prepare(double sampleRate, [[maybe_unused]] size_t maxBlockSize) noexcept {
    sampleRate_ = sampleRate;

    // Configure parameter smoothers (10ms time constant)
    frequencySmoother_.configure(10.0f, static_cast<float>(sampleRate));
    feedbackSmoother_.configure(10.0f, static_cast<float>(sampleRate));
    driveSmoother_.configure(10.0f, static_cast<float>(sampleRate));

    // Snap smoothers to initial values
    frequencySmoother_.snapTo(frequency_);
    feedbackSmoother_.snapTo(feedback_);
    driveSmoother_.snapTo(drive_);

    // Configure DC blocker (10Hz cutoff)
    dcBlocker_.prepare(sampleRate, 10.0f);

    // Prepare SingleAllpass stage
    singleStage_.prepare(sampleRate);
    singleStage_.setFrequency(frequency_, static_cast<float>(sampleRate));
    singleStage_.setDrive(drive_);
    singleStage_.setSaturationCurve(saturationCurve_);

    // Prepare AllpassChain stages
    for (size_t i = 0; i < 4; ++i) {
        chainAllpasses_[i].configure(FilterType::Allpass,
                                      frequency_ * kChainFrequencyRatios[i],
                                      10.0f, 0.0f, static_cast<float>(sampleRate));
    }
    chainWaveshaper_.setType(saturationCurve_);
    chainWaveshaper_.setDrive(drive_);

    // Prepare KarplusStrong delay (max delay for 20Hz minimum frequency)
    const float maxDelaySeconds = 1.0f / 20.0f;  // 50ms for 20Hz
    ksDelay_.prepare(sampleRate, maxDelaySeconds);
    ksLowpass_.prepare(sampleRate);
    ksLowpass_.setCutoff(5000.0f);  // Default cutoff
    ksWaveshaper_.setType(saturationCurve_);
    ksWaveshaper_.setDrive(drive_);

    // Prepare FeedbackMatrix stages
    for (size_t i = 0; i < 4; ++i) {
        matrixStages_[i].prepare(sampleRate);
        matrixStages_[i].setFrequency(frequency_ * kMatrixDetuneRatios[i],
                                       static_cast<float>(sampleRate));
        matrixStages_[i].setDrive(drive_);
        matrixStages_[i].setSaturationCurve(saturationCurve_);
    }

    prepared_ = true;
}

inline void AllpassSaturator::reset() noexcept {
    // Snap smoothers to current target values (don't change targets)
    frequencySmoother_.snapToTarget();
    feedbackSmoother_.snapToTarget();
    driveSmoother_.snapToTarget();

    // Reset DC blocker
    dcBlocker_.reset();

    // Reset SingleAllpass
    singleStage_.reset();

    // Reset AllpassChain
    for (auto& ap : chainAllpasses_) {
        ap.reset();
    }
    chainLastOutput_ = 0.0f;

    // Reset KarplusStrong
    ksDelay_.reset();
    ksLowpass_.reset();
    ksLastOutput_ = 0.0f;

    // Reset FeedbackMatrix
    for (auto& stage : matrixStages_) {
        stage.reset();
    }
    matrixLastOutputs_.fill(0.0f);
}

// -----------------------------------------------------------------------------
// Topology Selection
// -----------------------------------------------------------------------------

inline void AllpassSaturator::setTopology(NetworkTopology topology) noexcept {
    if (topology_ != topology) {
        topology_ = topology;
        // FR-009: Reset state on topology change
        reset();
    }
}

inline NetworkTopology AllpassSaturator::getTopology() const noexcept {
    return topology_;
}

// -----------------------------------------------------------------------------
// Frequency Control
// -----------------------------------------------------------------------------

inline void AllpassSaturator::setFrequency(float hz) noexcept {
    frequency_ = clampFrequency(hz);
    frequencySmoother_.setTarget(frequency_);
}

inline float AllpassSaturator::getFrequency() const noexcept {
    return frequency_;
}

// -----------------------------------------------------------------------------
// Feedback Control
// -----------------------------------------------------------------------------

inline void AllpassSaturator::setFeedback(float feedback) noexcept {
    feedback_ = std::clamp(feedback, 0.0f, 0.999f);
    feedbackSmoother_.setTarget(feedback_);
}

inline float AllpassSaturator::getFeedback() const noexcept {
    return feedback_;
}

// -----------------------------------------------------------------------------
// Saturation Control
// -----------------------------------------------------------------------------

inline void AllpassSaturator::setSaturationCurve(WaveshapeType type) noexcept {
    saturationCurve_ = type;
    singleStage_.setSaturationCurve(type);
    chainWaveshaper_.setType(type);
    ksWaveshaper_.setType(type);
    for (auto& stage : matrixStages_) {
        stage.setSaturationCurve(type);
    }
}

inline WaveshapeType AllpassSaturator::getSaturationCurve() const noexcept {
    return saturationCurve_;
}

inline void AllpassSaturator::setDrive(float drive) noexcept {
    drive_ = std::clamp(drive, 0.1f, 10.0f);
    driveSmoother_.setTarget(drive_);
}

inline float AllpassSaturator::getDrive() const noexcept {
    return drive_;
}

// -----------------------------------------------------------------------------
// KarplusStrong Specific
// -----------------------------------------------------------------------------

inline void AllpassSaturator::setDecay(float seconds) noexcept {
    decay_ = std::clamp(seconds, 0.001f, 60.0f);
}

inline float AllpassSaturator::getDecay() const noexcept {
    return decay_;
}

// -----------------------------------------------------------------------------
// Query Methods
// -----------------------------------------------------------------------------

inline bool AllpassSaturator::isPrepared() const noexcept {
    return prepared_;
}

inline double AllpassSaturator::getSampleRate() const noexcept {
    return prepared_ ? sampleRate_ : 0.0;
}

// -----------------------------------------------------------------------------
// Processing
// -----------------------------------------------------------------------------

inline float AllpassSaturator::process(float input) noexcept {
    // Return input unchanged if not prepared
    if (!prepared_) {
        return input;
    }

    // FR-026: Handle NaN/Inf input
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();
        return 0.0f;
    }

    // Smooth parameters
    const float smoothedFreq = frequencySmoother_.process();
    const float smoothedFeedback = feedbackSmoother_.process();
    const float smoothedDrive = driveSmoother_.process();

    // Update components with smoothed parameters
    const float sampleRateF = static_cast<float>(sampleRate_);
    singleStage_.setFrequency(smoothedFreq, sampleRateF);
    singleStage_.setDrive(smoothedDrive);

    // Update AllpassChain frequencies
    for (size_t i = 0; i < 4; ++i) {
        chainAllpasses_[i].configure(FilterType::Allpass,
                                      smoothedFreq * kChainFrequencyRatios[i],
                                      10.0f, 0.0f, sampleRateF);
    }
    chainWaveshaper_.setDrive(smoothedDrive);

    // Update KarplusStrong
    ksWaveshaper_.setDrive(smoothedDrive);

    // Update FeedbackMatrix
    for (size_t i = 0; i < 4; ++i) {
        matrixStages_[i].setFrequency(smoothedFreq * kMatrixDetuneRatios[i], sampleRateF);
        matrixStages_[i].setDrive(smoothedDrive);
    }

    // Process based on topology
    float output = 0.0f;
    switch (topology_) {
        case NetworkTopology::SingleAllpass:
            output = processSingleAllpass(input);
            break;
        case NetworkTopology::AllpassChain:
            output = processAllpassChain(input);
            break;
        case NetworkTopology::KarplusStrong:
            output = processKarplusStrong(input);
            break;
        case NetworkTopology::FeedbackMatrix:
            output = processFeedbackMatrix(input);
            break;
    }

    // FR-028: DC blocking after saturation
    output = dcBlocker_.process(output);

    // FR-027: Flush denormals
    output = detail::flushDenormal(output);

    return output;
}

inline void AllpassSaturator::processBlock(float* buffer, size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = process(buffer[i]);
    }
}

// -----------------------------------------------------------------------------
// Topology Processing Implementations
// -----------------------------------------------------------------------------

inline float AllpassSaturator::processSingleAllpass(float input) noexcept {
    const float feedback = feedbackSmoother_.getCurrentValue();
    return singleStage_.process(input, feedback);
}

inline float AllpassSaturator::processAllpassChain(float input) noexcept {
    const float feedback = feedbackSmoother_.getCurrentValue();

    // Add feedback to input
    float signal = input + chainLastOutput_ * feedback;

    // Process through chain of allpass filters
    for (auto& ap : chainAllpasses_) {
        signal = ap.process(signal);
    }

    // Apply saturation
    signal = chainWaveshaper_.process(signal);

    // Soft clip
    chainLastOutput_ = softClipFeedback(signal);

    return chainLastOutput_;
}

inline float AllpassSaturator::processKarplusStrong(float input) noexcept {
    const float smoothedFreq = frequencySmoother_.getCurrentValue();

    // Calculate delay time in samples from frequency
    const float delaySamples = static_cast<float>(sampleRate_) / smoothedFreq;

    // Calculate feedback and lowpass cutoff from decay time
    float ksFeedback, ksCutoff;
    decayToFeedbackAndCutoff(decay_, smoothedFreq, ksFeedback, ksCutoff);
    ksLowpass_.setCutoff(ksCutoff);

    // Read from delay with allpass interpolation
    const float delayed = ksDelay_.readAllpass(delaySamples);

    // Apply saturation
    const float saturated = ksWaveshaper_.process(delayed);

    // Apply lowpass for string timbre
    const float filtered = ksLowpass_.process(saturated);

    // Soft clip
    const float clipped = softClipFeedback(filtered);

    // Add input (excitation) to feedback
    const float feedbackSignal = input + clipped * ksFeedback;

    // Write to delay
    ksDelay_.write(feedbackSignal);

    ksLastOutput_ = clipped;
    return ksLastOutput_;
}

inline float AllpassSaturator::processFeedbackMatrix(float input) noexcept {
    const float feedback = feedbackSmoother_.getCurrentValue();

    // Apply Householder matrix to previous outputs for cross-coupling
    std::array<float, 4> feedbackSignals;
    HouseholderMatrix::multiply(matrixLastOutputs_.data(), feedbackSignals.data());

    // Scale by feedback amount
    for (auto& fb : feedbackSignals) {
        fb *= feedback;
    }

    // Process each stage: input goes to first channel, cross-feedback to all
    // Each stage also has internal feedback for self-oscillation
    const float internalFeedback = feedback * 0.7f;  // Scaled internal feedback
    std::array<float, 4> outputs;
    outputs[0] = matrixStages_[0].process(input + feedbackSignals[0], internalFeedback);
    outputs[1] = matrixStages_[1].process(feedbackSignals[1], internalFeedback);
    outputs[2] = matrixStages_[2].process(feedbackSignals[2], internalFeedback);
    outputs[3] = matrixStages_[3].process(feedbackSignals[3], internalFeedback);

    // Store outputs for next iteration
    matrixLastOutputs_ = outputs;

    // Sum all channels for mono output
    const float sum = outputs[0] + outputs[1] + outputs[2] + outputs[3];
    return sum * 0.25f;  // Normalize
}

// -----------------------------------------------------------------------------
// Internal Helpers
// -----------------------------------------------------------------------------

inline void AllpassSaturator::decayToFeedbackAndCutoff(
    float decaySeconds, float frequency,
    float& outFeedback, float& outCutoff) const noexcept {

    // RT60 decay: after decaySeconds, amplitude should be at -60dB
    // For a feedback loop, each period has gain = feedback
    // After N periods: gain^N = 10^(-60/20) = 0.001
    // N = decaySeconds * frequency
    // feedback^(decaySeconds * frequency) = 0.001
    // feedback = 0.001^(1 / (decaySeconds * frequency))

    const float periods = decaySeconds * frequency;
    if (periods > 0.0f) {
        // Use pow(0.001, 1/periods) but clamp to valid range
        outFeedback = std::pow(0.001f, 1.0f / periods);
        outFeedback = std::clamp(outFeedback, 0.0f, 0.999f);
    } else {
        outFeedback = 0.0f;
    }

    // Lowpass cutoff: lower cutoff = darker decay, higher cutoff = brighter
    // Use frequency-dependent cutoff for natural string behavior
    // Short decay = lower cutoff (muted), long decay = higher cutoff (bright)
    // Map decay 0.001-60s to cutoff range
    const float normalizedDecay = std::clamp(decaySeconds, 0.001f, 10.0f) / 10.0f;
    const float minCutoff = frequency * 2.0f;
    const float maxCutoff = static_cast<float>(sampleRate_) * 0.4f;
    outCutoff = minCutoff + normalizedDecay * (maxCutoff - minCutoff);
    outCutoff = std::clamp(outCutoff, 20.0f, maxCutoff);
}

} // namespace DSP
} // namespace Krate
