// ==============================================================================
// API Contract: First-Order Allpass Filter (Allpass1Pole)
// ==============================================================================
// Layer 1: DSP Primitive
// Location: dsp/include/krate/dsp/primitives/allpass_1pole.h
//
// This contract defines the public API for the Allpass1Pole class.
// Implementation must satisfy all requirements in spec.md.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, constexpr, value semantics)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle XII: Test-First Development
//
// Reference: specs/073-allpass-1pole/spec.md
// ==============================================================================

#pragma once

#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// Constants
// =============================================================================

/// Minimum coefficient boundary (exclusive of -1.0)
inline constexpr float kMinAllpass1PoleCoeff = -0.9999f;

/// Maximum coefficient boundary (exclusive of +1.0)
inline constexpr float kMaxAllpass1PoleCoeff = 0.9999f;

/// Minimum break frequency in Hz
inline constexpr float kMinAllpass1PoleFrequency = 1.0f;

// =============================================================================
// Allpass1Pole Class
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
/// Allpass1Pole filter;
/// filter.prepare(44100.0);
/// filter.setFrequency(1000.0f);  // Break frequency at 1kHz
///
/// // Process audio
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = filter.process(input[i]);
/// }
/// @endcode
class Allpass1Pole {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Default constructor - creates filter with coefficient 0 (break at fs/4)
    Allpass1Pole() noexcept = default;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// Initialize filter for a given sample rate.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    /// @post Filter is ready for processing with current coefficient
    /// @note FR-005
    void prepare(double sampleRate) noexcept;

    /// Set the break frequency (frequency at -90 degrees phase shift).
    /// @param hz Break frequency in Hz, clamped to [1 Hz, Nyquist * 0.99]
    /// @note FR-006, FR-009: Frequency clamping applied
    void setFrequency(float hz) noexcept;

    /// Set the filter coefficient directly.
    /// @param a Coefficient value, clamped to [-0.9999, +0.9999]
    /// @note FR-007, FR-008: Coefficient clamping applied
    void setCoefficient(float a) noexcept;

    /// Get the current filter coefficient.
    /// @return Current coefficient value in range [-0.9999, +0.9999]
    [[nodiscard]] float getCoefficient() const noexcept;

    /// Get the current break frequency.
    /// @return Break frequency in Hz corresponding to current coefficient
    [[nodiscard]] float getFrequency() const noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// Process a single sample.
    /// @param input Input sample
    /// @return Filtered output sample
    /// @note FR-010, FR-014: NaN/Inf input causes reset and returns 0.0f
    /// @note FR-015: Denormal flushing after each call
    /// @note FR-019, FR-020, FR-021: Real-time safe (noexcept, no alloc, no I/O)
    [[nodiscard]] float process(float input) noexcept;

    /// Process a block of samples in-place.
    /// @param buffer Sample buffer (modified in place)
    /// @param numSamples Number of samples to process
    /// @note FR-011, FR-012: Block processing, identical to N x process()
    /// @note FR-014: First sample NaN/Inf check - fills with zeros on invalid
    /// @note FR-015: Denormal flushing once at block end
    /// @note FR-019, FR-020, FR-021: Real-time safe
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // State Management
    // =========================================================================

    /// Clear filter state to zero.
    /// @post z1_ = 0, y1_ = 0
    /// @note FR-013
    void reset() noexcept;

    // =========================================================================
    // Static Utility Functions
    // =========================================================================

    /// Calculate coefficient from break frequency.
    /// @param hz Break frequency in Hz (clamped to valid range)
    /// @param sampleRate Sample rate in Hz
    /// @return Coefficient value in range [-0.9999, +0.9999]
    /// @note FR-016, FR-018: Formula a = (1 - tan(pi*f/fs)) / (1 + tan(pi*f/fs))
    [[nodiscard]] static float coeffFromFrequency(float hz, double sampleRate) noexcept;

    /// Calculate break frequency from coefficient.
    /// @param a Coefficient value (clamped to valid range)
    /// @param sampleRate Sample rate in Hz
    /// @return Break frequency in Hz
    /// @note FR-017
    [[nodiscard]] static float frequencyFromCoeff(float a, double sampleRate) noexcept;

private:
    // =========================================================================
    // State Variables
    // =========================================================================

    float a_ = 0.0f;              ///< Filter coefficient [-0.9999, +0.9999]
    float z1_ = 0.0f;             ///< Input delay state (x[n-1])
    float y1_ = 0.0f;             ///< Output feedback state (y[n-1])
    double sampleRate_ = 44100.0; ///< Sample rate in Hz
};

} // namespace DSP
} // namespace Krate
