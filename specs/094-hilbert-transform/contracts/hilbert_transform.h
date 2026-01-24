// ==============================================================================
// API Contract: Hilbert Transform
// ==============================================================================
// This file defines the public API for the HilbertTransform class.
// Implementation will be in: dsp/include/krate/dsp/primitives/hilbert_transform.h
//
// Spec: specs/094-hilbert-transform/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/allpass_1pole.h>

namespace Krate {
namespace DSP {

// =============================================================================
// Output Structure
// =============================================================================

/// Output structure containing both components of the analytic signal.
///
/// The in-phase (i) and quadrature (q) components can be used for
/// single-sideband modulation:
///   upper_sideband = i * cos(wt) - q * sin(wt)
///   lower_sideband = i * cos(wt) + q * sin(wt)
struct HilbertOutput {
    float i;  ///< In-phase component (original signal, delayed)
    float q;  ///< Quadrature component (90 degrees phase-shifted)
};

// =============================================================================
// HilbertTransform Class
// =============================================================================

/// @brief Hilbert transform using allpass filter cascade approximation.
///
/// Creates an analytic signal by producing a 90-degree phase-shifted
/// quadrature component alongside a delayed version of the input signal.
/// The two outputs can be used for single-sideband modulation (frequency
/// shifting) via:
///   shifted = i * cos(wt) - q * sin(wt)  // upper sideband
///   shifted = i * cos(wt) + q * sin(wt)  // lower sideband
///
/// Implementation uses two parallel cascades of 4 Allpass1Pole instances
/// with coefficients optimized by Olli Niemitalo for wideband 90-degree
/// phase accuracy.
///
/// @par Effective Bandwidth
/// At 44.1kHz: approximately 40Hz to 20kHz with +/- 1 degree accuracy.
/// Bandwidth scales with sample rate.
///
/// @par Latency
/// Fixed 5-sample latency (group delay) at all sample rates.
///
/// @par Constitution Compliance
/// - Real-time safe: noexcept, no allocations, no locks
/// - Layer 1: Depends only on Layer 0 (db_utils.h) and Layer 1 (Allpass1Pole)
///
/// @par Reference
/// Olli Niemitalo - Hilbert Transform: https://yehar.com/blog/?p=368
class HilbertTransform {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Default constructor - creates uninitialized transform.
    /// Call prepare() before processing.
    HilbertTransform() noexcept = default;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// Initialize for given sample rate.
    ///
    /// Configures all internal Allpass1Pole instances with optimized coefficients.
    /// Sample rates outside [22050, 192000] Hz are clamped to this range.
    ///
    /// @param sampleRate Sample rate in Hz (clamped to 22050-192000)
    /// @note FR-001, FR-003
    void prepare(double sampleRate) noexcept;

    /// Clear all internal filter states.
    ///
    /// After reset(), 5 samples of settling time are required before
    /// the phase accuracy specification is met.
    ///
    /// @note FR-002
    void reset() noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// Process a single sample.
    ///
    /// @param input Input sample
    /// @return HilbertOutput with in-phase (i) and quadrature (q) components
    /// @note FR-004, FR-006, FR-007
    /// @note Real-time safe: noexcept, no allocations
    [[nodiscard]] HilbertOutput process(float input) noexcept;

    /// Process a block of samples.
    ///
    /// Produces identical results to calling process() for each sample.
    ///
    /// @param input Input buffer (numSamples elements)
    /// @param outI Output buffer for in-phase component (numSamples elements)
    /// @param outQ Output buffer for quadrature component (numSamples elements)
    /// @param numSamples Number of samples to process
    /// @note FR-005
    /// @note Real-time safe: noexcept, no allocations
    void processBlock(const float* input, float* outI, float* outQ,
                      int numSamples) noexcept;

    // =========================================================================
    // State Query
    // =========================================================================

    /// Get the configured sample rate.
    ///
    /// @return Sample rate in Hz (within [22050, 192000])
    /// @note FR-015
    [[nodiscard]] double getSampleRate() const noexcept;

    /// Get the latency in samples (group delay).
    ///
    /// The Hilbert transform introduces a fixed 5-sample latency
    /// that should be compensated for in latency-sensitive applications.
    ///
    /// @return Fixed latency of 5 samples
    /// @note FR-016
    [[nodiscard]] int getLatencySamples() const noexcept;

private:
    // =========================================================================
    // Internal State
    // =========================================================================

    /// Path 1: 4 Allpass1Pole instances -> in-phase output (with 1-sample delay)
    /// Coefficients: 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737
    Allpass1Pole ap1_[4];

    /// One-sample delay for path alignment
    float delay1_ = 0.0f;

    /// Path 2: 4 Allpass1Pole instances -> quadrature output
    /// Coefficients: 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278
    Allpass1Pole ap2_[4];

    /// Configured sample rate
    double sampleRate_ = 44100.0;
};

} // namespace DSP
} // namespace Krate
