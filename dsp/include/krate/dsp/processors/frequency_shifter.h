// ==============================================================================
// Layer 2: DSP Processor - Frequency Shifter
// ==============================================================================
// Frequency shifter using Hilbert transform for single-sideband modulation.
// Shifts all frequencies by a constant Hz amount (not pitch shifting).
// Creates inharmonic, metallic effects. Based on the Bode frequency shifter
// principle using single-sideband modulation.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1)
// - Principle X: DSP Constraints (feedback soft limiting, denormal flushing)
// - Principle XII: Test-First Development
//
// Reference: specs/097-frequency-shifter/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/primitives/hilbert_transform.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Enumerations
// =============================================================================

/// @brief Shift direction for single-sideband modulation
///
/// Determines which sideband(s) appear in the output:
/// - Up: Upper sideband only (input frequency + shift amount)
/// - Down: Lower sideband only (input frequency - shift amount)
/// - Both: Both sidebands (ring modulation effect)
///
/// @par Formulas
/// Given I (in-phase) and Q (quadrature) from Hilbert transform,
/// and carrier cos(wt), sin(wt):
/// - Up: output = I*cos(wt) - Q*sin(wt)
/// - Down: output = I*cos(wt) + Q*sin(wt)
/// - Both: output = 0.5*(up + down) = I*cos(wt)
enum class ShiftDirection : uint8_t {
    Up = 0,    ///< Upper sideband only (input + shift)
    Down,      ///< Lower sideband only (input - shift)
    Both       ///< Both sidebands (ring modulation)
};

// =============================================================================
// FrequencyShifter Class
// =============================================================================

/// @brief Frequency shifter using Hilbert transform for SSB modulation.
///
/// Shifts all frequencies by a constant Hz amount (not pitch shifting).
/// Unlike pitch shifting which preserves harmonic relationships, frequency
/// shifting adds/subtracts a fixed Hz value, creating inharmonic, metallic
/// textures. Based on the Bode frequency shifter principle.
///
/// @par Algorithm
/// 1. Generate analytic signal using Hilbert transform (I + jQ)
/// 2. Multiply by complex exponential carrier (cos(wt) + j*sin(wt))
/// 3. Take real part for desired sideband
///
/// @par Features
/// - Three direction modes: Up (upper sideband), Down (lower), Both (ring mod)
/// - LFO modulation of shift amount for evolving effects
/// - Feedback path with tanh saturation for spiraling (Shepard-tone) effects
/// - Stereo mode: left = +shift, right = -shift for width
/// - Dry/wet mix control
/// - Click-free parameter smoothing
///
/// @par Real-Time Safety
/// All processing methods are noexcept and allocation-free after prepare().
/// Safe for audio callbacks.
///
/// @par Thread Safety
/// Not thread-safe. Create separate instances per audio channel or use
/// processStereo() for stereo processing on the same thread.
///
/// @par Layer
/// Layer 2 (Processor) - depends on Layer 0 (core) and Layer 1 (primitives)
///
/// @par Latency
/// Fixed 5-sample latency from Hilbert transform. Not compensated in output.
///
/// @par Aliasing
/// Frequency shifting is linear; aliasing occurs only when shifted frequencies
/// exceed Nyquist. No oversampling at Layer 2 to maintain CPU budget.
class FrequencyShifter {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Maximum shift amount in Hz (positive or negative)
    static constexpr float kMaxShiftHz = 5000.0f;

    /// Maximum modulation depth in Hz
    static constexpr float kMaxModDepthHz = 500.0f;

    /// Maximum feedback amount (0.99 to prevent infinite sustain)
    static constexpr float kMaxFeedback = 0.99f;

    /// Minimum LFO modulation rate in Hz
    static constexpr float kMinModRate = 0.01f;

    /// Maximum LFO modulation rate in Hz
    static constexpr float kMaxModRate = 20.0f;

    /// Oscillator renormalization interval (samples)
    static constexpr int kRenormInterval = 1024;

    /// Default smoothing time in milliseconds
    static constexpr float kSmoothingTimeMs = 5.0f;

    // =========================================================================
    // Lifecycle (FR-001, FR-002)
    // =========================================================================

    /// @brief Default constructor
    ///
    /// Creates an unprepared processor. Call prepare() before processing.
    /// Processing before prepare() returns input unchanged.
    FrequencyShifter() noexcept = default;

    /// @brief Destructor
    ~FrequencyShifter() = default;

    // Non-copyable due to LFO containing vectors
    FrequencyShifter(const FrequencyShifter&) = delete;
    FrequencyShifter& operator=(const FrequencyShifter&) = delete;
    FrequencyShifter(FrequencyShifter&&) noexcept = default;
    FrequencyShifter& operator=(FrequencyShifter&&) noexcept = default;

    /// @brief Initialize for given sample rate (FR-001)
    ///
    /// Prepares the Hilbert transform, LFO, and smoothers.
    /// Must be called before processing. Call again if sample rate changes.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @note NOT real-time safe (LFO allocates wavetables)
    void prepare(double sampleRate) noexcept;

    /// @brief Clear all internal state (FR-002)
    ///
    /// Resets Hilbert transform, oscillator phase, and feedback sample.
    /// Does not change parameter values or sample rate.
    void reset() noexcept;

    // =========================================================================
    // Shift Control (FR-004, FR-005, FR-006)
    // =========================================================================

    /// @brief Set the base frequency shift amount (FR-004, FR-005)
    ///
    /// @param hz Shift amount in Hz, clamped to [-5000, +5000]
    ///           Typical musical range: -1000 to +1000
    void setShiftAmount(float hz) noexcept;

    /// @brief Get the current shift amount in Hz
    [[nodiscard]] float getShiftAmount() const noexcept;

    /// @brief Set the shift direction (FR-006)
    ///
    /// @param dir Shift direction mode:
    ///            - Up: Upper sideband (input + shift)
    ///            - Down: Lower sideband (input - shift)
    ///            - Both: Ring modulation (both sidebands)
    void setDirection(ShiftDirection dir) noexcept;

    /// @brief Get current shift direction
    [[nodiscard]] ShiftDirection getDirection() const noexcept;

    // =========================================================================
    // LFO Modulation (FR-010, FR-011, FR-012)
    // =========================================================================

    /// @brief Set LFO modulation rate (FR-011)
    ///
    /// @param hz LFO frequency in Hz, clamped to [0.01, 20]
    void setModRate(float hz) noexcept;

    /// @brief Get current LFO modulation rate in Hz
    [[nodiscard]] float getModRate() const noexcept;

    /// @brief Set LFO modulation depth (FR-012)
    ///
    /// Effective shift = baseShift + modDepth * lfoValue
    /// where lfoValue is in [-1, +1]
    ///
    /// @param hz Modulation range in Hz, clamped to [0, 500]
    void setModDepth(float hz) noexcept;

    /// @brief Get current LFO modulation depth in Hz
    [[nodiscard]] float getModDepth() const noexcept;

    // =========================================================================
    // Feedback (FR-014, FR-015, FR-016)
    // =========================================================================

    /// @brief Set feedback amount for spiraling effects (FR-014)
    ///
    /// Feedback creates Shepard-tone-like spiraling where frequencies
    /// continue shifting through successive passes.
    ///
    /// @param amount Feedback level, clamped to [0.0, 0.99]
    /// @note Feedback is soft-limited with tanh() to prevent runaway (FR-015)
    void setFeedback(float amount) noexcept;

    /// @brief Get current feedback amount
    [[nodiscard]] float getFeedback() const noexcept;

    // =========================================================================
    // Mix (FR-017, FR-018)
    // =========================================================================

    /// @brief Set dry/wet mix (FR-017)
    ///
    /// output = (1-mix)*dry + mix*wet
    ///
    /// @param dryWet Mix amount, clamped to [0.0, 1.0]
    ///               0.0 = dry only (bypass), 1.0 = wet only
    void setMix(float dryWet) noexcept;

    /// @brief Get current dry/wet mix
    [[nodiscard]] float getMix() const noexcept;

    // =========================================================================
    // Processing (FR-019, FR-020, FR-021, FR-022)
    // =========================================================================

    /// @brief Process a single mono sample (FR-019)
    ///
    /// @param input Input sample
    /// @return Frequency-shifted output sample (with mix applied)
    ///
    /// @note Returns input unchanged if prepare() not called
    /// @note Returns 0 and resets on NaN/Inf input (FR-023)
    /// @note noexcept, allocation-free (FR-022)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process stereo with opposite shifts per channel (FR-020, FR-021)
    ///
    /// Left channel receives positive shift (+shiftHz).
    /// Right channel receives negative shift (-shiftHz).
    ///
    /// @param left Left channel sample (in/out)
    /// @param right Right channel sample (in/out)
    ///
    /// @note noexcept, allocation-free (FR-022)
    void processStereo(float& left, float& right) noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if processor has been prepared
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Update oscillator coefficients when shift frequency changes
    void updateOscillator(float shiftHz) noexcept;

    /// @brief Advance oscillator by one sample and handle renormalization
    void advanceOscillator() noexcept;

    /// @brief Apply SSB modulation formula
    /// @param I In-phase component from Hilbert transform
    /// @param Q Quadrature component from Hilbert transform
    /// @param shiftSign +1.0 for positive shift, -1.0 for negative
    /// @return Processed sample based on direction mode
    [[nodiscard]] float applySSB(float I, float Q, float shiftSign) noexcept;

    /// @brief Internal single-channel processing
    /// @param input Input sample
    /// @param hilbert Hilbert transform instance for this channel
    /// @param feedbackState Feedback state for this channel
    /// @param shiftSign +1.0 for positive shift, -1.0 for negative
    /// @return Wet output sample (before mix)
    [[nodiscard]] float processInternal(float input, HilbertTransform& hilbert,
                                        float& feedbackState, float shiftSign) noexcept;

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Hilbert transform for analytic signal (separate for stereo)
    HilbertTransform hilbertL_;
    HilbertTransform hilbertR_;

    // Quadrature oscillator state (recurrence relation)
    float cosTheta_ = 1.0f;    ///< cos(current phase)
    float sinTheta_ = 0.0f;    ///< sin(current phase)
    float cosDelta_ = 1.0f;    ///< cos(phase increment)
    float sinDelta_ = 0.0f;    ///< sin(phase increment)
    int renormCounter_ = 0;    ///< Counter for renormalization

    // LFO for modulation
    LFO modLFO_;

    // Feedback state (separate for stereo)
    float feedbackSampleL_ = 0.0f;
    float feedbackSampleR_ = 0.0f;

    // Parameters (raw target values)
    float shiftHz_ = 0.0f;          ///< Base shift amount [-5000, +5000] Hz
    float modRate_ = 1.0f;          ///< LFO rate [0.01, 20] Hz
    float modDepth_ = 0.0f;         ///< LFO modulation depth [0, 500] Hz
    float feedback_ = 0.0f;         ///< Feedback amount [0, 0.99]
    float mix_ = 1.0f;              ///< Dry/wet mix [0, 1]
    ShiftDirection direction_ = ShiftDirection::Up;

    // Smoothers for click-free parameter changes
    OnePoleSmoother shiftSmoother_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother mixSmoother_;

    // State
    double sampleRate_ = 44100.0;
    bool prepared_ = false;

    // Track last shift for oscillator coefficient updates
    float lastEffectiveShift_ = 0.0f;
};

// =============================================================================
// Inline Implementations
// =============================================================================

inline void FrequencyShifter::prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate;

    // Initialize Hilbert transforms
    hilbertL_.prepare(sampleRate);
    hilbertR_.prepare(sampleRate);

    // Initialize LFO
    modLFO_.prepare(sampleRate);
    modLFO_.setWaveform(Waveform::Sine);
    modLFO_.setFrequency(modRate_);

    // Configure smoothers
    const auto sampleRateF = static_cast<float>(sampleRate);
    shiftSmoother_.configure(kSmoothingTimeMs, sampleRateF);
    feedbackSmoother_.configure(kSmoothingTimeMs, sampleRateF);
    mixSmoother_.configure(kSmoothingTimeMs, sampleRateF);

    // Initialize smoother targets
    shiftSmoother_.snapTo(shiftHz_);
    feedbackSmoother_.snapTo(feedback_);
    mixSmoother_.snapTo(mix_);

    // Initialize oscillator
    updateOscillator(shiftHz_);

    prepared_ = true;
}

inline void FrequencyShifter::reset() noexcept {
    hilbertL_.reset();
    hilbertR_.reset();
    modLFO_.reset();

    // Reset oscillator state
    cosTheta_ = 1.0f;
    sinTheta_ = 0.0f;
    renormCounter_ = 0;

    // Reset feedback state
    feedbackSampleL_ = 0.0f;
    feedbackSampleR_ = 0.0f;

    // Reset smoothers
    shiftSmoother_.reset();
    feedbackSmoother_.reset();
    mixSmoother_.reset();

    // Reinitialize smoother targets
    shiftSmoother_.snapTo(shiftHz_);
    feedbackSmoother_.snapTo(feedback_);
    mixSmoother_.snapTo(mix_);

    lastEffectiveShift_ = 0.0f;
}

inline void FrequencyShifter::setShiftAmount(float hz) noexcept {
    shiftHz_ = std::clamp(hz, -kMaxShiftHz, kMaxShiftHz);
    shiftSmoother_.setTarget(shiftHz_);
}

inline float FrequencyShifter::getShiftAmount() const noexcept {
    return shiftHz_;
}

inline void FrequencyShifter::setDirection(ShiftDirection dir) noexcept {
    direction_ = dir;
}

inline ShiftDirection FrequencyShifter::getDirection() const noexcept {
    return direction_;
}

inline void FrequencyShifter::setModRate(float hz) noexcept {
    modRate_ = std::clamp(hz, kMinModRate, kMaxModRate);
    modLFO_.setFrequency(modRate_);
}

inline float FrequencyShifter::getModRate() const noexcept {
    return modRate_;
}

inline void FrequencyShifter::setModDepth(float hz) noexcept {
    modDepth_ = std::clamp(hz, 0.0f, kMaxModDepthHz);
}

inline float FrequencyShifter::getModDepth() const noexcept {
    return modDepth_;
}

inline void FrequencyShifter::setFeedback(float amount) noexcept {
    feedback_ = std::clamp(amount, 0.0f, kMaxFeedback);
    feedbackSmoother_.setTarget(feedback_);
}

inline float FrequencyShifter::getFeedback() const noexcept {
    return feedback_;
}

inline void FrequencyShifter::setMix(float dryWet) noexcept {
    mix_ = std::clamp(dryWet, 0.0f, 1.0f);
    mixSmoother_.setTarget(mix_);
}

inline float FrequencyShifter::getMix() const noexcept {
    return mix_;
}

inline bool FrequencyShifter::isPrepared() const noexcept {
    return prepared_;
}

inline void FrequencyShifter::updateOscillator(float shiftHz) noexcept {
    // Calculate phase increment: delta = 2*pi*f/fs
    const double delta = static_cast<double>(kTwoPi) * static_cast<double>(shiftHz) / sampleRate_;
    cosDelta_ = static_cast<float>(std::cos(delta));
    sinDelta_ = static_cast<float>(std::sin(delta));
    lastEffectiveShift_ = shiftHz;
}

inline void FrequencyShifter::advanceOscillator() noexcept {
    // Recurrence relation: rotation matrix
    // [cos(theta + delta)]   [cos(delta) -sin(delta)] [cos(theta)]
    // [sin(theta + delta)] = [sin(delta)  cos(delta)] [sin(theta)]
    const float cosNext = cosTheta_ * cosDelta_ - sinTheta_ * sinDelta_;
    const float sinNext = sinTheta_ * cosDelta_ + cosTheta_ * sinDelta_;
    cosTheta_ = cosNext;
    sinTheta_ = sinNext;

    // Periodic renormalization to prevent drift (every kRenormInterval samples)
    if (++renormCounter_ >= kRenormInterval) {
        renormCounter_ = 0;
        const float r = std::sqrt(cosTheta_ * cosTheta_ + sinTheta_ * sinTheta_);
        if (r > 0.0f) {
            cosTheta_ /= r;
            sinTheta_ /= r;
        }
    }
}

inline float FrequencyShifter::applySSB(float I, float Q, float shiftSign) noexcept {
    // Adjust sin term sign for shift direction
    const float adjustedSin = sinTheta_ * shiftSign;

    switch (direction_) {
        case ShiftDirection::Up:
            // Upper sideband: I*cos - Q*sin
            return I * cosTheta_ - Q * adjustedSin;

        case ShiftDirection::Down:
            // Lower sideband: I*cos + Q*sin
            return I * cosTheta_ + Q * adjustedSin;

        case ShiftDirection::Both:
            // Ring mod: 0.5*(up + down) = I*cos
            // Q terms cancel out: (I*cos - Q*sin + I*cos + Q*sin) / 2 = I*cos
            return I * cosTheta_;
    }

    return I * cosTheta_;  // Fallback (should never reach)
}

inline float FrequencyShifter::processInternal(float input, HilbertTransform& hilbert,
                                                float& feedbackState, float shiftSign) noexcept {
    // Get smoothed feedback and apply saturation
    const float smoothedFeedback = feedbackSmoother_.process();
    const float feedbackIn = std::tanh(feedbackState) * smoothedFeedback;

    // Combine input with feedback
    const float inputWithFeedback = input + feedbackIn;

    // Generate analytic signal
    const HilbertOutput iq = hilbert.process(inputWithFeedback);

    // Apply SSB modulation
    const float wet = applySSB(iq.i, iq.q, shiftSign);

    // Store for next feedback iteration
    feedbackState = wet;

    return wet;
}

inline float FrequencyShifter::process(float input) noexcept {
    if (!prepared_) {
        return input;
    }

    // Handle NaN/Inf input (FR-023)
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();
        return 0.0f;
    }

    // Get LFO modulation value
    const float lfoValue = modLFO_.process();

    // Calculate effective shift with modulation (FR-013)
    const float smoothedShift = shiftSmoother_.process();
    const float effectiveShift = smoothedShift + modDepth_ * lfoValue;

    // Update oscillator if shift changed significantly
    if (std::abs(effectiveShift - lastEffectiveShift_) > 0.001f) {
        updateOscillator(effectiveShift);
    }

    // Process through Hilbert + SSB
    const float wet = processInternal(input, hilbertL_, feedbackSampleL_, 1.0f);

    // Advance oscillator
    advanceOscillator();

    // Apply mix (FR-018)
    const float smoothedMix = mixSmoother_.process();
    float output = (1.0f - smoothedMix) * input + smoothedMix * wet;

    // Flush denormals (FR-024)
    output = detail::flushDenormal(output);

    return output;
}

inline void FrequencyShifter::processStereo(float& left, float& right) noexcept {
    if (!prepared_) {
        return;
    }

    // Handle NaN/Inf input (FR-023)
    if (detail::isNaN(left) || detail::isInf(left) ||
        detail::isNaN(right) || detail::isInf(right)) {
        reset();
        left = 0.0f;
        right = 0.0f;
        return;
    }

    // Get LFO modulation value (shared between channels)
    const float lfoValue = modLFO_.process();

    // Calculate effective shift with modulation (FR-013)
    const float smoothedShift = shiftSmoother_.process();
    const float effectiveShift = smoothedShift + modDepth_ * lfoValue;

    // Update oscillator if shift changed significantly
    if (std::abs(effectiveShift - lastEffectiveShift_) > 0.001f) {
        updateOscillator(effectiveShift);
    }

    // Store dry signals
    const float dryL = left;
    const float dryR = right;

    // Process left channel with positive shift (+shiftHz) (FR-021)
    const float wetL = processInternal(left, hilbertL_, feedbackSampleL_, 1.0f);

    // Process right channel with negative shift (-shiftHz) (FR-021)
    const float wetR = processInternal(right, hilbertR_, feedbackSampleR_, -1.0f);

    // Advance oscillator once (shared state)
    advanceOscillator();

    // Apply mix (FR-018)
    const float smoothedMix = mixSmoother_.process();
    left = (1.0f - smoothedMix) * dryL + smoothedMix * wetL;
    right = (1.0f - smoothedMix) * dryR + smoothedMix * wetR;

    // Flush denormals (FR-024)
    left = detail::flushDenormal(left);
    right = detail::flushDenormal(right);
}

} // namespace DSP
} // namespace Krate
