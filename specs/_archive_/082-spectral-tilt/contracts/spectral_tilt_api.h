// ==============================================================================
// API Contract: Spectral Tilt Filter
// ==============================================================================
// This file defines the public API surface for SpectralTilt.
// Implementation will be in: dsp/include/krate/dsp/processors/spectral_tilt.h
//
// Feature: 082-spectral-tilt
// Layer: 2 (DSP Processors)
// Date: 2026-01-22
// ==============================================================================

#pragma once

#include <cstddef>

namespace Krate {
namespace DSP {

/// @brief Spectral Tilt Filter - Layer 2 Processor
///
/// Applies a linear dB/octave gain slope across the frequency spectrum
/// using an efficient IIR (high-shelf biquad) approximation.
///
/// @par Features
/// - Configurable tilt amount (-12 to +12 dB/octave)
/// - Configurable pivot frequency (20 Hz to 20 kHz)
/// - Parameter smoothing for click-free automation
/// - Zero latency (pure IIR implementation)
/// - Gain limiting for stability (+24 dB max, -48 dB min)
///
/// @par Real-Time Safety
/// All processing methods (process, processBlock) are noexcept and
/// allocation-free. Safe for audio thread use.
///
/// @par Thread Safety
/// Not thread-safe. Create separate instances for each audio thread.
///
/// @par Usage Example
/// @code
/// SpectralTilt tilt;
/// tilt.prepare(44100.0);
/// tilt.setTilt(6.0f);              // +6 dB/octave brightness
/// tilt.setPivotFrequency(1000.0f); // Pivot at 1 kHz
///
/// // In audio callback
/// for (int i = 0; i < numSamples; ++i) {
///     output[i] = tilt.process(input[i]);
/// }
/// @endcode
///
/// @see EnvelopeFilter, TiltEQ, SpectralMorphFilter (for FFT-based tilt)
class SpectralTilt {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// @name Parameter Ranges
    /// @{

    /// Minimum tilt amount in dB/octave (FR-002)
    static constexpr float kMinTilt = -12.0f;

    /// Maximum tilt amount in dB/octave (FR-002)
    static constexpr float kMaxTilt = +12.0f;

    /// Minimum pivot frequency in Hz (FR-003, Edge Case)
    static constexpr float kMinPivot = 20.0f;

    /// Maximum pivot frequency in Hz (FR-003, Edge Case)
    static constexpr float kMaxPivot = 20000.0f;

    /// Minimum smoothing time in milliseconds (FR-014)
    static constexpr float kMinSmoothing = 1.0f;

    /// Maximum smoothing time in milliseconds (FR-014)
    static constexpr float kMaxSmoothing = 500.0f;

    /// @}

    /// @name Default Values
    /// @{

    /// Default smoothing time in milliseconds (FR-014, Assumptions)
    static constexpr float kDefaultSmoothing = 50.0f;

    /// Default pivot frequency in Hz (Assumptions)
    static constexpr float kDefaultPivot = 1000.0f;

    /// Default tilt amount in dB/octave (Assumptions)
    static constexpr float kDefaultTilt = 0.0f;

    /// @}

    /// @name Gain Limits
    /// @{

    /// Maximum gain at any frequency in dB (FR-024)
    static constexpr float kMaxGainDb = +24.0f;

    /// Minimum gain at any frequency in dB (FR-025)
    static constexpr float kMinGainDb = -48.0f;

    /// @}

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor
    ///
    /// Creates an unprepared SpectralTilt with default parameters.
    /// Call prepare() before processing.
    SpectralTilt() noexcept;

    /// @brief Destructor
    ~SpectralTilt() noexcept;

    // Non-copyable, movable
    SpectralTilt(const SpectralTilt&) = delete;
    SpectralTilt& operator=(const SpectralTilt&) = delete;
    SpectralTilt(SpectralTilt&&) noexcept = default;
    SpectralTilt& operator=(SpectralTilt&&) noexcept = default;

    /// @brief Prepare for processing at given sample rate
    /// @param sampleRate Sample rate in Hz (typically 44100-192000)
    /// @pre None
    /// @post isPrepared() returns true
    /// @note NOT real-time safe (may allocate for smoothers)
    /// @note FR-015
    void prepare(double sampleRate);

    /// @brief Reset internal state without changing parameters
    /// @pre prepare() has been called
    /// @post Filter state cleared; parameters unchanged
    /// @note Real-time safe
    /// @note FR-016
    void reset() noexcept;

    // =========================================================================
    // Parameters
    // =========================================================================

    /// @brief Set tilt amount
    /// @param dBPerOctave Tilt in dB/octave, clamped to [-12, +12]
    /// @note Positive values boost frequencies above pivot
    /// @note Negative values cut frequencies above pivot
    /// @note Changes are smoothed to prevent clicks (FR-012)
    /// @note FR-002
    void setTilt(float dBPerOctave);

    /// @brief Set pivot frequency
    /// @param hz Pivot frequency in Hz, clamped to [20, 20000]
    /// @note Gain at pivot is always 0 dB regardless of tilt (FR-006)
    /// @note Changes are smoothed to prevent clicks (FR-013)
    /// @note FR-003
    void setPivotFrequency(float hz);

    /// @brief Set parameter smoothing time
    /// @param ms Smoothing time in milliseconds, clamped to [1, 500]
    /// @note Affects both tilt and pivot smoothing
    /// @note FR-014
    void setSmoothing(float ms);

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample
    /// @param input Input sample
    /// @return Processed output sample
    /// @pre None (returns input if not prepared - FR-019)
    /// @note Real-time safe: noexcept, no allocations (FR-021)
    /// @note Zero latency (FR-010)
    /// @note FR-017
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    /// @pre None (passthrough if not prepared)
    /// @note Real-time safe: noexcept, no allocations (FR-021)
    /// @note FR-018
    void processBlock(float* buffer, int numSamples) noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Get current tilt setting
    /// @return Tilt in dB/octave
    [[nodiscard]] float getTilt() const noexcept;

    /// @brief Get current pivot frequency
    /// @return Pivot frequency in Hz
    [[nodiscard]] float getPivotFrequency() const noexcept;

    /// @brief Get current smoothing time
    /// @return Smoothing time in milliseconds
    [[nodiscard]] float getSmoothing() const noexcept;

    /// @brief Check if processor is prepared
    /// @return true if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    // Implementation details hidden from API contract
    // See spectral_tilt.h for full implementation
};

} // namespace DSP
} // namespace Krate
