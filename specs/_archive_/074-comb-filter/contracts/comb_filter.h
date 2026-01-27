// ==============================================================================
// Layer 1: DSP Primitive - Comb Filters (API Contract)
// ==============================================================================
// This file defines the PUBLIC API for the comb filter primitives.
// Implementation details should match this contract exactly.
//
// Contains three filter types:
// - FeedforwardComb: FIR comb filter (notches)
// - FeedbackComb: IIR comb filter (peaks) with optional damping
// - SchroederAllpass: Allpass filter (flat magnitude, phase dispersion)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, value semantics)
// - Principle IX: Layer 1 (depends only on Layer 0 and stdlib)
// - Principle XII: Test-First Development
//
// Reference: specs/074-comb-filter/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/delay_line.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// FeedforwardComb - FIR Comb Filter
// =============================================================================

/// @brief Feedforward (FIR) comb filter for creating spectral notches.
///
/// Implements the difference equation: y[n] = x[n] + g * x[n-D]
///
/// Creates notches at frequencies: f = (2k-1) / (2 * D * T) where k=1,2,3...
/// Use for: flanger, chorus, doubling effects.
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle IX: Layer 1 (depends only on Layer 0 and DelayLine)
///
/// @see FeedbackComb for resonant (IIR) filtering
/// @see SchroederAllpass for unity magnitude filtering
class FeedforwardComb {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor. Creates unprepared filter.
    FeedforwardComb() noexcept = default;

    /// @brief Destructor.
    ~FeedforwardComb() = default;

    // Non-copyable due to DelayLine, movable
    FeedforwardComb(const FeedforwardComb&) = delete;
    FeedforwardComb& operator=(const FeedforwardComb&) = delete;
    FeedforwardComb(FeedforwardComb&&) noexcept = default;
    FeedforwardComb& operator=(FeedforwardComb&&) noexcept = default;

    /// @brief Prepare the filter for processing.
    /// @param sampleRate Sample rate in Hz [8000, 192000]
    /// @param maxDelaySeconds Maximum delay time in seconds
    void prepare(double sampleRate, float maxDelaySeconds) noexcept;

    /// @brief Reset all internal state to zero.
    void reset() noexcept;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Set the feedforward gain coefficient.
    /// @param g Gain [0.0, 1.0], clamped at boundaries
    /// @note 0.0 = no effect (dry only), 1.0 = maximum notch depth
    void setGain(float g) noexcept;

    /// @brief Set delay time in samples.
    /// @param samples Delay [1.0, maxDelaySamples], clamped at boundaries
    void setDelaySamples(float samples) noexcept;

    /// @brief Set delay time in milliseconds.
    /// @param ms Delay time, converted to samples internally
    void setDelayMs(float ms) noexcept;

    // =========================================================================
    // Processing (Real-Time Safe)
    // =========================================================================

    /// @brief Process a single sample.
    /// @param input Input sample
    /// @return Filtered output sample
    /// @note Returns input unchanged if not prepared
    /// @note Handles NaN/Inf by resetting and returning 0.0f
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place.
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples
    /// @note Bit-identical to sequential process() calls
    void processBlock(float* buffer, size_t numSamples) noexcept;

private:
    DelayLine delay_;
    float gain_ = 0.5f;
    float delaySamples_ = 1.0f;
    double sampleRate_ = 0.0;
};

// =============================================================================
// FeedbackComb - IIR Comb Filter
// =============================================================================

/// @brief Feedback (IIR) comb filter for creating spectral peaks/resonances.
///
/// Implements the difference equation:
/// - Without damping: y[n] = x[n] + g * y[n-D]
/// - With damping:    y[n] = x[n] + g * LP(y[n-D])
///
/// Where LP is one-pole lowpass: LP(x) = (1-d)*x + d*LP_prev
///
/// Creates peaks at frequencies: f = k / (D * T) where k=0,1,2...
/// Use for: Karplus-Strong synthesis, reverb comb banks, physical modeling.
///
/// @par Stability
/// Feedback coefficient is clamped to [-0.9999, 0.9999] for DC stability.
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle IX: Layer 1 (depends only on Layer 0 and DelayLine)
///
/// @see FeedforwardComb for notch filtering
/// @see SchroederAllpass for unity magnitude filtering
class FeedbackComb {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor. Creates unprepared filter.
    FeedbackComb() noexcept = default;

    /// @brief Destructor.
    ~FeedbackComb() = default;

    // Non-copyable due to DelayLine, movable
    FeedbackComb(const FeedbackComb&) = delete;
    FeedbackComb& operator=(const FeedbackComb&) = delete;
    FeedbackComb(FeedbackComb&&) noexcept = default;
    FeedbackComb& operator=(FeedbackComb&&) noexcept = default;

    /// @brief Prepare the filter for processing.
    /// @param sampleRate Sample rate in Hz [8000, 192000]
    /// @param maxDelaySeconds Maximum delay time in seconds
    void prepare(double sampleRate, float maxDelaySeconds) noexcept;

    /// @brief Reset all internal state to zero.
    void reset() noexcept;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Set the feedback gain coefficient.
    /// @param g Feedback [-0.9999, 0.9999], clamped at boundaries for stability
    /// @note Positive: in-phase feedback (standard)
    /// @note Negative: phase-inverted feedback
    void setFeedback(float g) noexcept;

    /// @brief Set the damping coefficient for the feedback lowpass filter.
    /// @param d Damping [0.0, 1.0], clamped at boundaries
    /// @note 0.0 = no damping (bright, all frequencies)
    /// @note 1.0 = maximum damping (dark, DC only)
    void setDamping(float d) noexcept;

    /// @brief Set delay time in samples.
    /// @param samples Delay [1.0, maxDelaySamples], clamped at boundaries
    void setDelaySamples(float samples) noexcept;

    /// @brief Set delay time in milliseconds.
    /// @param ms Delay time, converted to samples internally
    void setDelayMs(float ms) noexcept;

    // =========================================================================
    // Processing (Real-Time Safe)
    // =========================================================================

    /// @brief Process a single sample.
    /// @param input Input sample
    /// @return Filtered output sample
    /// @note Returns input unchanged if not prepared
    /// @note Handles NaN/Inf by resetting and returning 0.0f
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place.
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples
    /// @note Bit-identical to sequential process() calls
    void processBlock(float* buffer, size_t numSamples) noexcept;

private:
    DelayLine delay_;
    float feedback_ = 0.5f;
    float damping_ = 0.0f;
    float dampingState_ = 0.0f;  // One-pole LP state (flushed for denormals)
    float delaySamples_ = 1.0f;
    double sampleRate_ = 0.0;
};

// =============================================================================
// SchroederAllpass
// =============================================================================

/// @brief Schroeder allpass filter for reverb diffusion.
///
/// Implements the difference equation: y[n] = -g*x[n] + x[n-D] + g*y[n-D]
///
/// Maintains unity magnitude response at all frequencies while dispersing
/// the phase, spreading transients in time without altering tonal balance.
///
/// Use for: reverb diffusion networks, impulse spreading, decorrelation.
///
/// @par Magnitude Response
/// Unity (1.0) at all frequencies within 0.01 dB tolerance.
///
/// @par Stability
/// Coefficient is clamped to [-0.9999, 0.9999] for stability.
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle IX: Layer 1 (depends only on Layer 0 and DelayLine)
///
/// @see FeedforwardComb for notch filtering
/// @see FeedbackComb for resonant filtering
class SchroederAllpass {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor. Creates unprepared filter.
    SchroederAllpass() noexcept = default;

    /// @brief Destructor.
    ~SchroederAllpass() = default;

    // Non-copyable due to DelayLine, movable
    SchroederAllpass(const SchroederAllpass&) = delete;
    SchroederAllpass& operator=(const SchroederAllpass&) = delete;
    SchroederAllpass(SchroederAllpass&&) noexcept = default;
    SchroederAllpass& operator=(SchroederAllpass&&) noexcept = default;

    /// @brief Prepare the filter for processing.
    /// @param sampleRate Sample rate in Hz [8000, 192000]
    /// @param maxDelaySeconds Maximum delay time in seconds
    void prepare(double sampleRate, float maxDelaySeconds) noexcept;

    /// @brief Reset all internal state to zero.
    void reset() noexcept;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Set the allpass coefficient.
    /// @param g Coefficient [-0.9999, 0.9999], clamped at boundaries
    /// @note Typical value: 0.7 (golden ratio inverse approximation)
    /// @note Higher values = more diffusion, longer impulse response
    void setCoefficient(float g) noexcept;

    /// @brief Set delay time in samples.
    /// @param samples Delay [1.0, maxDelaySamples], clamped at boundaries
    void setDelaySamples(float samples) noexcept;

    /// @brief Set delay time in milliseconds.
    /// @param ms Delay time, converted to samples internally
    void setDelayMs(float ms) noexcept;

    // =========================================================================
    // Processing (Real-Time Safe)
    // =========================================================================

    /// @brief Process a single sample.
    /// @param input Input sample
    /// @return Filtered output sample
    /// @note Returns input unchanged if not prepared
    /// @note Handles NaN/Inf by resetting and returning 0.0f
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place.
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples
    /// @note Bit-identical to sequential process() calls
    void processBlock(float* buffer, size_t numSamples) noexcept;

private:
    DelayLine delay_;
    float coefficient_ = 0.7f;
    float feedbackState_ = 0.0f;  // y[n-D] state (flushed for denormals)
    float delaySamples_ = 1.0f;
    double sampleRate_ = 0.0;
};

} // namespace DSP
} // namespace Krate
