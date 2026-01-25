// ==============================================================================
// Layer 1: DSP Primitive - First-Order Allpass Filter
// ==============================================================================
// First-order allpass filter for phase shifting applications.
// Primary use case: Phaser effects with cascaded stages and LFO modulation.
//
// Implements the difference equation: y[n] = a*x[n] + x[n-1] - a*y[n-1]
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, constexpr, value semantics)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle XII: Test-First Development
//
// Naming: Follows one_pole.h convention (OnePoleLP, OnePoleHP -> OnePoleAllpass)
// Reference: specs/073-allpass-1pole/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// Constants
// =============================================================================

/// Minimum coefficient boundary (exclusive of -1.0)
inline constexpr float kOnePoleAllpassMinCoeff = -0.9999f;

/// Maximum coefficient boundary (exclusive of +1.0)
inline constexpr float kOnePoleAllpassMaxCoeff = 0.9999f;

/// Minimum break frequency in Hz
inline constexpr float kOnePoleAllpassMinFrequency = 1.0f;

// Backwards compatibility aliases
inline constexpr float kMinAllpass1PoleCoeff = kOnePoleAllpassMinCoeff;
inline constexpr float kMaxAllpass1PoleCoeff = kOnePoleAllpassMaxCoeff;
inline constexpr float kMinAllpass1PoleFrequency = kOnePoleAllpassMinFrequency;

// =============================================================================
// OnePoleAllpass Class
// =============================================================================

/// @brief First-order allpass filter for phase shifting applications.
///
/// Implements the first-order allpass difference equation:
/// @code
/// y[n] = a*x[n] + x[n-1] - a*y[n-1]
/// @endcode
///
/// The filter provides:
/// - Unity magnitude response at all frequencies
/// - Phase shift from 0 degrees (DC) to -180 degrees (Nyquist)
/// - -90 degrees phase shift at the break frequency
///
/// Primary use case: Phaser effects (cascaded stages with LFO modulation)
///
/// @par Constitution Compliance
/// - Real-time safe: noexcept, no allocations, no locks
/// - Layer 1: Depends only on Layer 0 (math_constants.h, db_utils.h)
///
/// @par Example Usage
/// @code
/// OnePoleAllpass filter;
/// filter.prepare(44100.0);
/// filter.setFrequency(1000.0f);  // Break frequency at 1kHz
///
/// // Process audio
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = filter.process(input[i]);
/// }
/// @endcode
class OnePoleAllpass {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Default constructor - creates filter with coefficient 0 (break at fs/4)
    OnePoleAllpass() noexcept = default;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// Initialize filter for a given sample rate.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    /// @post Filter is ready for processing with current coefficient
    /// @note FR-005
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    }

    /// Set the break frequency (frequency at -90 degrees phase shift).
    /// @param hz Break frequency in Hz, clamped to [1 Hz, Nyquist * 0.99]
    /// @note FR-006, FR-009: Frequency clamping applied
    void setFrequency(float hz) noexcept {
        a_ = coeffFromFrequency(hz, sampleRate_);
    }

    /// Set the filter coefficient directly.
    /// @param a Coefficient value, clamped to [-0.9999, +0.9999]
    /// @note FR-007, FR-008: Coefficient clamping applied
    void setCoefficient(float a) noexcept {
        a_ = std::clamp(a, kOnePoleAllpassMinCoeff, kOnePoleAllpassMaxCoeff);
    }

    /// Get the current filter coefficient.
    /// @return Current coefficient value in range [-0.9999, +0.9999]
    [[nodiscard]] float getCoefficient() const noexcept {
        return a_;
    }

    /// Get the current break frequency.
    /// @return Break frequency in Hz corresponding to current coefficient
    [[nodiscard]] float getFrequency() const noexcept {
        return frequencyFromCoeff(a_, sampleRate_);
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// Process a single sample.
    /// @param input Input sample
    /// @return Filtered output sample
    /// @note FR-010, FR-014: NaN/Inf input causes reset and returns 0.0f
    /// @note FR-015: Denormal flushing after each call
    /// @note FR-019, FR-020, FR-021: Real-time safe (noexcept, no alloc, no I/O)
    [[nodiscard]] float process(float input) noexcept {
        // FR-014: NaN/Inf check for every process() call
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return 0.0f;
        }

        // FR-001: y[n] = a*x[n] + x[n-1] - a*y[n-1]
        const float output = a_ * input + z1_ - a_ * y1_;

        // Update state
        z1_ = input;
        y1_ = output;

        // FR-015: Denormal flushing after each call
        z1_ = detail::flushDenormal(z1_);
        y1_ = detail::flushDenormal(y1_);

        return output;
    }

    /// Process a block of samples in-place.
    /// @param buffer Sample buffer (modified in place)
    /// @param numSamples Number of samples to process
    /// @note FR-011, FR-012: Block processing, identical to N x process()
    /// @note FR-014: First sample NaN/Inf check - fills with zeros on invalid
    /// @note FR-015: Denormal flushing per-sample (required for SC-007 bit-identical output)
    /// @note FR-019, FR-020, FR-021: Real-time safe
    void processBlock(float* buffer, size_t numSamples) noexcept {
        if (buffer == nullptr || numSamples == 0) {
            return;
        }

        // FR-014: Check first sample, abort entire block if invalid
        if (detail::isNaN(buffer[0]) || detail::isInf(buffer[0])) {
            reset();
            for (size_t i = 0; i < numSamples; ++i) {
                buffer[i] = 0.0f;
            }
            return;
        }

        // Process block with per-sample denormal flushing (SC-007: bit-identical to process())
        for (size_t i = 0; i < numSamples; ++i) {
            // FR-001: y[n] = a*x[n] + x[n-1] - a*y[n-1]
            const float input = buffer[i];
            const float output = a_ * input + z1_ - a_ * y1_;

            // Update state
            z1_ = input;
            y1_ = output;

            // FR-015: Flush denormals per-sample to match process()
            z1_ = detail::flushDenormal(z1_);
            y1_ = detail::flushDenormal(y1_);

            buffer[i] = output;
        }
    }

    // =========================================================================
    // State Management
    // =========================================================================

    /// Clear filter state to zero.
    /// @post z1_ = 0, y1_ = 0
    /// @note FR-013
    void reset() noexcept {
        z1_ = 0.0f;
        y1_ = 0.0f;
    }

    // =========================================================================
    // Static Utility Functions
    // =========================================================================

    /// Calculate coefficient from break frequency.
    /// @param hz Break frequency in Hz (clamped to valid range)
    /// @param sampleRate Sample rate in Hz
    /// @return Coefficient value in range [-0.9999, +0.9999]
    /// @note FR-016, FR-018: Formula a = (1 - tan(pi*f/fs)) / (1 + tan(pi*f/fs))
    [[nodiscard]] static float coeffFromFrequency(float hz, double sampleRate) noexcept {
        // FR-009: Clamp frequency to [1 Hz, Nyquist * 0.99]
        const float sr = static_cast<float>(sampleRate > 0.0 ? sampleRate : 44100.0);
        const float maxFreq = sr * 0.5f * 0.99f;
        const float clampedHz = std::clamp(hz, kOnePoleAllpassMinFrequency, maxFreq);

        // FR-018: a = (1 - tan(pi * f / fs)) / (1 + tan(pi * f / fs))
        const float t = std::tan(kPi * clampedHz / sr);
        const float a = (1.0f - t) / (1.0f + t);

        // FR-008: Clamp to valid coefficient range
        return std::clamp(a, kOnePoleAllpassMinCoeff, kOnePoleAllpassMaxCoeff);
    }

    /// Calculate break frequency from coefficient.
    /// @param a Coefficient value (clamped to valid range)
    /// @param sampleRate Sample rate in Hz
    /// @return Break frequency in Hz
    /// @note FR-017
    [[nodiscard]] static float frequencyFromCoeff(float a, double sampleRate) noexcept {
        // Clamp coefficient to valid range
        const float clampedA = std::clamp(a, kOnePoleAllpassMinCoeff, kOnePoleAllpassMaxCoeff);
        const float sr = static_cast<float>(sampleRate > 0.0 ? sampleRate : 44100.0);

        // FR-017: Inverse formula freq = sr * atan((1 - a) / (1 + a)) / pi
        // Handle edge case where 1 + a could be near zero
        const float denom = 1.0f + clampedA;
        if (denom < 0.0001f) {
            return sr * 0.5f * 0.99f;  // Near Nyquist
        }

        const float freq = sr * std::atan((1.0f - clampedA) / denom) / kPi;

        // Clamp result to valid frequency range
        const float maxFreq = sr * 0.5f * 0.99f;
        return std::clamp(freq, kOnePoleAllpassMinFrequency, maxFreq);
    }

private:
    // =========================================================================
    // State Variables
    // =========================================================================

    float a_ = 0.0f;              ///< Filter coefficient [-0.9999, +0.9999]
    float z1_ = 0.0f;             ///< Input delay state (x[n-1])
    float y1_ = 0.0f;             ///< Output feedback state (y[n-1])
    double sampleRate_ = 44100.0; ///< Sample rate in Hz
};

// =============================================================================
// Backwards Compatibility
// =============================================================================

/// @deprecated Use OnePoleAllpass instead. Will be removed in future version.
using Allpass1Pole = OnePoleAllpass;

} // namespace DSP
} // namespace Krate
