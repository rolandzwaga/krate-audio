// ==============================================================================
// Layer 1: DSP Primitives
// one_pole.h - One-Pole Audio Filters
// ==============================================================================
// API Contract for specs/070-filter-foundations
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (RAII, value semantics)
// - Principle IX: Layer 1 (depends only on Layer 0)
// - Principle X: DSP Constraints (flush denormals, handle edge cases)
//
// Reference: specs/070-filter-foundations/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>

#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// OnePoleLP - First-Order Lowpass Filter (FR-013, FR-016, FR-017, FR-018-FR-025)
// =============================================================================

/// @brief First-order lowpass filter for audio signal processing.
///
/// Implements a 6dB/octave lowpass filter using the standard one-pole topology.
/// Unlike OnePoleSmoother (designed for parameter smoothing), this class is
/// optimized for audio signal processing with proper frequency response.
///
/// @formula y[n] = (1 - a) * x[n] + a * y[n-1]
///          where a = exp(-2 * pi * cutoff / sampleRate)
///
/// @note Call prepare() before processing; filter returns input unchanged if unprepared
/// @note NaN/Inf inputs are handled by returning 0 and resetting state
/// @note All processing methods are noexcept and flush denormals
///
/// @example
/// ```cpp
/// OnePoleLP filter;
/// filter.prepare(44100.0);
/// filter.setCutoff(1000.0f);
///
/// // Process samples
/// for (auto& sample : buffer) {
///     sample = filter.process(sample);
/// }
/// ```
class OnePoleLP {
public:
    /// @brief Default constructor.
    /// Filter starts in unprepared state.
    OnePoleLP() noexcept = default;

    /// @brief Prepare the filter for processing.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    /// @note Clamps sampleRate to minimum 1000.0 if invalid
    void prepare(double sampleRate) noexcept;

    /// @brief Set the cutoff frequency.
    /// @param hz Cutoff frequency in Hz
    /// @note Clamps to [1.0, Nyquist * 0.99] range
    void setCutoff(float hz) noexcept;

    /// @brief Get the current cutoff frequency.
    /// @return Cutoff frequency in Hz
    [[nodiscard]] float getCutoff() const noexcept { return cutoffHz_; }

    /// @brief Process a single sample.
    /// @param input Input sample
    /// @return Filtered output sample
    /// @note Returns input unchanged if prepare() not called (FR-027)
    /// @note Returns 0 and resets state on NaN/Inf input (FR-034)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place.
    /// @param buffer Pointer to sample buffer (modified in-place)
    /// @param numSamples Number of samples to process
    /// @note Produces bit-identical output to equivalent process() calls (SC-009)
    void processBlock(float* buffer, size_t numSamples) noexcept;

    /// @brief Reset filter state.
    /// Clears internal state without changing cutoff or sample rate.
    void reset() noexcept;

private:
    void updateCoefficient() noexcept;
    float clampCutoff(float hz) const noexcept;

    float coefficient_ = 0.0f;    ///< Filter coefficient 'a'
    float state_ = 0.0f;          ///< y[n-1]
    float cutoffHz_ = 1000.0f;    ///< Current cutoff frequency
    double sampleRate_ = 44100.0; ///< Current sample rate
    bool prepared_ = false;       ///< True after prepare() called
};

// =============================================================================
// OnePoleHP - First-Order Highpass Filter (FR-014, FR-016-FR-025)
// =============================================================================

/// @brief First-order highpass filter for audio signal processing.
///
/// Implements a 6dB/octave highpass filter using the differentiator topology.
/// Useful for DC blocking, bass reduction, and crossover networks.
///
/// @formula y[n] = ((1 + a) / 2) * (x[n] - x[n-1]) + a * y[n-1]
///          where a = exp(-2 * pi * cutoff / sampleRate)
///
/// @note Same usage pattern as OnePoleLP
///
/// @example
/// ```cpp
/// OnePoleHP dcBlocker;
/// dcBlocker.prepare(44100.0);
/// dcBlocker.setCutoff(20.0f);  // Block below 20Hz
///
/// for (auto& sample : buffer) {
///     sample = dcBlocker.process(sample);
/// }
/// ```
class OnePoleHP {
public:
    OnePoleHP() noexcept = default;

    /// @brief Prepare the filter for processing.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    void prepare(double sampleRate) noexcept;

    /// @brief Set the cutoff frequency.
    /// @param hz Cutoff frequency in Hz
    void setCutoff(float hz) noexcept;

    /// @brief Get the current cutoff frequency.
    [[nodiscard]] float getCutoff() const noexcept { return cutoffHz_; }

    /// @brief Process a single sample.
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place.
    void processBlock(float* buffer, size_t numSamples) noexcept;

    /// @brief Reset filter state.
    void reset() noexcept;

private:
    void updateCoefficient() noexcept;
    float clampCutoff(float hz) const noexcept;

    float coefficient_ = 0.0f;    ///< Filter coefficient 'a'
    float inputState_ = 0.0f;     ///< x[n-1]
    float outputState_ = 0.0f;    ///< y[n-1]
    float cutoffHz_ = 100.0f;     ///< Current cutoff frequency
    double sampleRate_ = 44100.0; ///< Current sample rate
    bool prepared_ = false;       ///< True after prepare() called
};

// =============================================================================
// LeakyIntegrator - Envelope Detection (FR-015, FR-018-FR-021, FR-025)
// =============================================================================

/// @brief Simple leaky integrator for envelope detection and smoothing.
///
/// Implements y[n] = x[n] + leak * y[n-1] where leak is typically 0.99-0.9999.
/// The leak coefficient controls the decay rate of the accumulated value.
///
/// Unlike OnePoleLP/OnePoleHP, LeakyIntegrator is sample-rate independent
/// and does not require a prepare() method. The time constant in seconds is
/// approximately: tau = -1 / (sampleRate * ln(leak))
///
/// @note For leak = 0.999 at 44100 Hz: tau ~= 22.68ms (within SC-005 tolerance)
///
/// @example
/// ```cpp
/// LeakyIntegrator envelope;
/// envelope.setLeak(0.999f);
///
/// for (auto& sample : buffer) {
///     float rectified = std::abs(sample);
///     float env = envelope.process(rectified);
///     // env smoothly follows the amplitude envelope
/// }
/// ```
class LeakyIntegrator {
public:
    LeakyIntegrator() noexcept = default;

    /// @brief Construct with specific leak coefficient.
    /// @param leak Leak coefficient in range [0, 1)
    explicit LeakyIntegrator(float leak) noexcept;

    /// @brief Set the leak coefficient.
    /// @param a Leak coefficient (clamped to [0, 0.9999999])
    void setLeak(float a) noexcept;

    /// @brief Get the current leak coefficient.
    [[nodiscard]] float getLeak() const noexcept { return leak_; }

    /// @brief Process a single sample.
    /// @param input Input sample (typically rectified)
    /// @return Accumulated output
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place.
    /// @param buffer Pointer to sample buffer
    /// @param numSamples Number of samples to process
    void processBlock(float* buffer, size_t numSamples) noexcept;

    /// @brief Reset state to zero.
    void reset() noexcept;

    /// @brief Get current accumulated state (read-only).
    [[nodiscard]] float getState() const noexcept { return state_; }

private:
    float leak_ = 0.999f;  ///< Leak coefficient [0, 1)
    float state_ = 0.0f;   ///< Accumulated state y[n-1]
};

// =============================================================================
// Inline Implementations - OnePoleLP
// =============================================================================

inline void OnePoleLP::prepare(double sampleRate) noexcept {
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 44100.0;
    if (sampleRate_ < 1000.0) sampleRate_ = 1000.0;
    prepared_ = true;
    updateCoefficient();
}

inline void OnePoleLP::setCutoff(float hz) noexcept {
    cutoffHz_ = clampCutoff(hz);
    updateCoefficient();
}

inline float OnePoleLP::process(float input) noexcept {
    // Return input unchanged if not prepared (FR-027)
    if (!prepared_) {
        return input;
    }

    // Handle NaN/Inf (FR-034)
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();
        return 0.0f;
    }

    // y[n] = (1 - a) * x[n] + a * y[n-1]
    state_ = (1.0f - coefficient_) * input + coefficient_ * state_;
    state_ = detail::flushDenormal(state_);
    return state_;
}

inline void OnePoleLP::processBlock(float* buffer, size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = process(buffer[i]);
    }
}

inline void OnePoleLP::reset() noexcept {
    state_ = 0.0f;
}

inline void OnePoleLP::updateCoefficient() noexcept {
    // a = exp(-2 * pi * fc / fs)
    coefficient_ = std::exp(-kTwoPi * cutoffHz_ / static_cast<float>(sampleRate_));
}

inline float OnePoleLP::clampCutoff(float hz) const noexcept {
    if (hz <= 0.0f) return 1.0f;
    const float nyquist = static_cast<float>(sampleRate_) * 0.495f;
    return (hz > nyquist) ? nyquist : hz;
}

// =============================================================================
// Inline Implementations - OnePoleHP
// =============================================================================

inline void OnePoleHP::prepare(double sampleRate) noexcept {
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 44100.0;
    if (sampleRate_ < 1000.0) sampleRate_ = 1000.0;
    prepared_ = true;
    updateCoefficient();
}

inline void OnePoleHP::setCutoff(float hz) noexcept {
    cutoffHz_ = clampCutoff(hz);
    updateCoefficient();
}

inline float OnePoleHP::process(float input) noexcept {
    // Return input unchanged if not prepared (FR-027)
    if (!prepared_) {
        return input;
    }

    // Handle NaN/Inf (FR-034)
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();
        return 0.0f;
    }

    // y[n] = ((1 + a) / 2) * (x[n] - x[n-1]) + a * y[n-1]
    const float diff = input - inputState_;
    const float output = ((1.0f + coefficient_) * 0.5f) * diff + coefficient_ * outputState_;

    inputState_ = input;
    outputState_ = detail::flushDenormal(output);
    return outputState_;
}

inline void OnePoleHP::processBlock(float* buffer, size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = process(buffer[i]);
    }
}

inline void OnePoleHP::reset() noexcept {
    inputState_ = 0.0f;
    outputState_ = 0.0f;
}

inline void OnePoleHP::updateCoefficient() noexcept {
    // Same coefficient formula as lowpass
    coefficient_ = std::exp(-kTwoPi * cutoffHz_ / static_cast<float>(sampleRate_));
}

inline float OnePoleHP::clampCutoff(float hz) const noexcept {
    if (hz <= 0.0f) return 1.0f;
    const float nyquist = static_cast<float>(sampleRate_) * 0.495f;
    return (hz > nyquist) ? nyquist : hz;
}

// =============================================================================
// Inline Implementations - LeakyIntegrator
// =============================================================================

inline LeakyIntegrator::LeakyIntegrator(float leak) noexcept {
    setLeak(leak);
}

inline void LeakyIntegrator::setLeak(float a) noexcept {
    // Clamp to valid range [0, 1)
    if (a < 0.0f) a = 0.0f;
    if (a >= 1.0f) a = 0.9999999f;
    leak_ = a;
}

inline float LeakyIntegrator::process(float input) noexcept {
    // Handle NaN/Inf (FR-034)
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();
        return 0.0f;
    }

    // y[n] = x[n] + leak * y[n-1]
    state_ = input + leak_ * state_;
    state_ = detail::flushDenormal(state_);
    return state_;
}

inline void LeakyIntegrator::processBlock(float* buffer, size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = process(buffer[i]);
    }
}

inline void LeakyIntegrator::reset() noexcept {
    state_ = 0.0f;
}

} // namespace DSP
} // namespace Krate
