// ==============================================================================
// Layer 1: DSP Primitive - Hilbert Transform
// ==============================================================================
// Hilbert transform using allpass filter cascade approximation.
// Creates an analytic signal with 90-degree phase-shifted quadrature output.
//
// Primary use case: Single-sideband modulation for frequency shifting.
//
// Implementation uses two parallel cascades of 4 second-order allpass filters
// with coefficients optimized by Olli Niemitalo for wideband 90-degree
// phase accuracy. The second-order allpass uses H(z) = (a² - z⁻²) / (1 - a²z⁻²).
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, constexpr, value semantics)
// - Principle IX: Layer 1 (depends only on Layer 0 and Layer 1)
// - Principle XII: Test-First Development
//
// Reference: specs/094-hilbert-transform/spec.md
// Coefficients: https://yehar.com/blog/?p=368 (Olli Niemitalo)
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cmath>

namespace Krate {
namespace DSP {

// =============================================================================
// Olli Niemitalo Hilbert Transform Coefficients
// =============================================================================
// Two parallel cascades of 4 second-order allpass filters.
// Optimized for wideband 90-degree phase accuracy (+/- 0.7 degrees over
// 0.002 to 0.998 of Nyquist frequency).
//
// IMPORTANT: These coefficients require SQUARING before use in the allpass.
// Reference: Olli Niemitalo - https://yehar.com/blog/?p=368
//   Transfer function: H(z) = (a² - z⁻²) / (1 - a²z⁻²)
//   Difference equation: y[n] = a²*(x[n] + y[n-2]) - x[n-2]
//
// Structure per Niemitalo:
// - Path 1 (odd coefficients): input → allpass cascade → 1-sample delay → output I
// - Path 2 (even coefficients): input → allpass cascade → output Q (negated)
//
// The delay on Path 1 aligns the outputs in time. Q is negated to match
// the textbook SSB modulation sign convention:
//   upper_sideband = I*cos(wt) - Q*sin(wt)
//   lower_sideband = I*cos(wt) + Q*sin(wt)
// =============================================================================

namespace {

/// Path 1 (In-phase): Odd-indexed coefficients from Niemitalo table (a1,a3,a5,a7)
/// "The ones with odd index go in one path" - 1-sample delay AFTER this cascade
constexpr float kHilbertPath1Coeffs[4] = {
    0.6923878f,           // a1
    0.9360654322959f,     // a3
    0.9882295226860f,     // a5
    0.9987488452737f      // a7
};

/// Path 2 (Quadrature): Even-indexed coefficients from Niemitalo table (a0,a2,a4,a6)
/// "The ones with even index go in the other" - output is negated for SSB convention
constexpr float kHilbertPath2Coeffs[4] = {
    0.4021921162426f,     // a0
    0.8561710882420f,     // a2
    0.9722909545651f,     // a4
    0.9952884791278f      // a6
};

/// Minimum supported sample rate (Hz)
constexpr double kHilbertMinSampleRate = 22050.0;

/// Maximum supported sample rate (Hz)
constexpr double kHilbertMaxSampleRate = 192000.0;

/// Fixed latency in samples
constexpr int kHilbertLatencySamples = 5;

/// Denormal threshold for flushing tiny values
/// This should be at or below the smallest normal float (~1.175e-38)
/// Using 1e-37 to catch subnormals while preserving small normal values
constexpr float kHilbertDenormalThreshold = 1e-37f;

} // anonymous namespace

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
/// Implementation uses two parallel cascades of 4 second-order allpass filters
/// with coefficients optimized by Olli Niemitalo for wideband 90-degree
/// phase accuracy. Uses transfer function H(z) = (a² - z⁻²) / (1 - a²z⁻²).
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
/// - Layer 1: Depends only on Layer 0 (db_utils.h)
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
    /// Sample rates outside [22050, 192000] Hz are clamped to this range.
    ///
    /// @param sampleRate Sample rate in Hz (clamped to 22050-192000)
    /// @note FR-001, FR-003
    void prepare(double sampleRate) noexcept {
        // Clamp sample rate to valid range (FR-003)
        sampleRate_ = std::clamp(sampleRate, kHilbertMinSampleRate, kHilbertMaxSampleRate);

        // Reset all filter states
        reset();
    }

    /// Clear all internal filter states.
    ///
    /// After reset(), 5 samples of settling time are required before
    /// the phase accuracy specification is met.
    ///
    /// @note FR-002
    void reset() noexcept {
        // Clear all second-order allpass filter states
        // Each stage needs x[n-1], x[n-2], y[n-1], y[n-2]
        for (int i = 0; i < 4; ++i) {
            ap1_x1_[i] = 0.0f;
            ap1_x2_[i] = 0.0f;
            ap1_y1_[i] = 0.0f;
            ap1_y2_[i] = 0.0f;
            ap2_x1_[i] = 0.0f;
            ap2_x2_[i] = 0.0f;
            ap2_y1_[i] = 0.0f;
            ap2_y2_[i] = 0.0f;
        }

        // Clear the delay element (applied AFTER Filter 1 allpass cascade)
        delay_ = 0.0f;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// Process a single sample.
    ///
    /// @param input Input sample
    /// @return HilbertOutput with in-phase (i) and quadrature (q) components
    /// @note FR-004, FR-006, FR-007
    /// @note Real-time safe: noexcept, no allocations
    [[nodiscard]] HilbertOutput process(float input) noexcept {
        // FR-019: NaN/Inf check
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return HilbertOutput{0.0f, 0.0f};
        }

        // Niemitalo Hilbert transform structure:
        // - Filter 1: input → allpass cascade → 1-sample delay → output I
        // - Filter 2: input → allpass cascade → output Q
        //
        // Second-order allpass transfer function: H(z) = (a² - z⁻²) / (1 - a²z⁻²)
        // Difference equation: y[n] = a²*(x[n] + y[n-2]) - x[n-2]
        //
        // Coefficients are SQUARED before use (a → a²).

        // Filter 1 (kHilbertPath1Coeffs): input → allpass cascade → delay → I
        float path1 = input;
        for (int i = 0; i < 4; ++i) {
            // Second-order allpass: y[n] = a²*(x[n] + y[n-2]) - x[n-2]
            const float a = kHilbertPath1Coeffs[i];
            const float a2 = a * a;  // Square the coefficient
            const float out = a2 * (path1 + ap1_y2_[i]) - ap1_x2_[i];

            // Shift state registers: [n-2] <- [n-1] <- [n]
            ap1_x2_[i] = ap1_x1_[i];
            ap1_x1_[i] = path1;
            ap1_y2_[i] = ap1_y1_[i];
            ap1_y1_[i] = out;

            // Denormal flushing (FR-018)
            if (std::abs(ap1_x1_[i]) < kHilbertDenormalThreshold) ap1_x1_[i] = 0.0f;
            if (std::abs(ap1_x2_[i]) < kHilbertDenormalThreshold) ap1_x2_[i] = 0.0f;
            if (std::abs(ap1_y1_[i]) < kHilbertDenormalThreshold) ap1_y1_[i] = 0.0f;
            if (std::abs(ap1_y2_[i]) < kHilbertDenormalThreshold) ap1_y2_[i] = 0.0f;

            path1 = out;
        }

        // Filter 2 (kHilbertPath2Coeffs): input → allpass cascade → Q
        float path2 = input;
        for (int i = 0; i < 4; ++i) {
            // Second-order allpass: y[n] = a²*(x[n] + y[n-2]) - x[n-2]
            const float a = kHilbertPath2Coeffs[i];
            const float a2 = a * a;  // Square the coefficient
            const float out = a2 * (path2 + ap2_y2_[i]) - ap2_x2_[i];

            // Shift state registers: [n-2] <- [n-1] <- [n]
            ap2_x2_[i] = ap2_x1_[i];
            ap2_x1_[i] = path2;
            ap2_y2_[i] = ap2_y1_[i];
            ap2_y1_[i] = out;

            // Denormal flushing (FR-018)
            if (std::abs(ap2_x1_[i]) < kHilbertDenormalThreshold) ap2_x1_[i] = 0.0f;
            if (std::abs(ap2_x2_[i]) < kHilbertDenormalThreshold) ap2_x2_[i] = 0.0f;
            if (std::abs(ap2_y1_[i]) < kHilbertDenormalThreshold) ap2_y1_[i] = 0.0f;
            if (std::abs(ap2_y2_[i]) < kHilbertDenormalThreshold) ap2_y2_[i] = 0.0f;

            path2 = out;
        }

        // Apply 1-sample delay AFTER Path 1's allpass cascade
        // This compensates for the group delay difference between the two paths.
        // Note: Q is negated to match the textbook SSB modulation convention where
        // Q leads I by 90 degrees. This ensures the standard formulas work correctly:
        //   upper_sideband = I*cos(wt) - Q*sin(wt)
        //   lower_sideband = I*cos(wt) + Q*sin(wt)
        float outI = delay_;
        delay_ = path1;
        float outQ = -path2;  // Negate for correct SSB convention

        // Denormal flushing for outputs and delay state (FR-018)
        if (std::abs(outI) < kHilbertDenormalThreshold) outI = 0.0f;
        if (std::abs(outQ) < kHilbertDenormalThreshold) outQ = 0.0f;
        if (std::abs(delay_) < kHilbertDenormalThreshold) delay_ = 0.0f;

        return HilbertOutput{outI, outQ};
    }

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
                      int numSamples) noexcept {
        if (input == nullptr || outI == nullptr || outQ == nullptr || numSamples <= 0) {
            return;
        }

        for (int i = 0; i < numSamples; ++i) {
            const HilbertOutput result = process(input[i]);
            outI[i] = result.i;
            outQ[i] = result.q;
        }
    }

    // =========================================================================
    // State Query
    // =========================================================================

    /// Get the configured sample rate.
    ///
    /// @return Sample rate in Hz (within [22050, 192000])
    /// @note FR-015
    [[nodiscard]] double getSampleRate() const noexcept {
        return sampleRate_;
    }

    /// Get the latency in samples (group delay).
    ///
    /// The Hilbert transform introduces a fixed 5-sample latency
    /// that should be compensated for in latency-sensitive applications.
    ///
    /// @return Fixed latency of 5 samples
    /// @note FR-016
    [[nodiscard]] int getLatencySamples() const noexcept {
        return kHilbertLatencySamples;
    }

private:
    // =========================================================================
    // Internal State
    // =========================================================================

    // Filter 1 second-order allpass filter states (4 stages)
    // Each stage needs x[n-1], x[n-2], y[n-1], y[n-2] for proper second-order operation
    float ap1_x1_[4] = {0.0f, 0.0f, 0.0f, 0.0f};  ///< x[n-1] for each stage
    float ap1_x2_[4] = {0.0f, 0.0f, 0.0f, 0.0f};  ///< x[n-2] for each stage
    float ap1_y1_[4] = {0.0f, 0.0f, 0.0f, 0.0f};  ///< y[n-1] for each stage
    float ap1_y2_[4] = {0.0f, 0.0f, 0.0f, 0.0f};  ///< y[n-2] for each stage

    // Filter 2 second-order allpass filter states (4 stages)
    float ap2_x1_[4] = {0.0f, 0.0f, 0.0f, 0.0f};  ///< x[n-1] for each stage
    float ap2_x2_[4] = {0.0f, 0.0f, 0.0f, 0.0f};  ///< x[n-2] for each stage
    float ap2_y1_[4] = {0.0f, 0.0f, 0.0f, 0.0f};  ///< y[n-1] for each stage
    float ap2_y2_[4] = {0.0f, 0.0f, 0.0f, 0.0f};  ///< y[n-2] for each stage

    /// One-sample delay applied AFTER Path 1 allpass cascade
    float delay_ = 0.0f;

    /// Configured sample rate
    double sampleRate_ = 44100.0;
};

} // namespace DSP
} // namespace Krate
