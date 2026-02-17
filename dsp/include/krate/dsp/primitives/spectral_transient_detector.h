// ==============================================================================
// Layer 1: DSP Primitive - Spectral Transient Detector
// ==============================================================================
// Spectral flux-based transient detector for onset detection in magnitude
// spectra (Layer 1 primitive).
//
// Algorithm: Half-wave rectified spectral flux (Duxbury et al. 2002, Dixon 2006)
//   SF(n) = sum(max(0, |X_n[k]| - |X_{n-1}[k]|))  for k = 0..numBins-1
//   runningAvg(n) = alpha * runningAvg(n-1) + (1 - alpha) * SF(n)
//   transient = SF(n) > threshold * runningAvg(n)
//
// Performance: O(numBins) per frame, single linear pass, no transcendental math.
// At 44.1kHz / 4096-point FFT / 1024-hop: ~43 frames/sec * 2049 bins * 3 ops
// = ~258K FLOPs/sec. Negligible overhead (< 0.01% CPU).
//
// Feature: 062-spectral-transient-detector
// Spec: specs/062-spectral-transient-detector/spec.md
// ==============================================================================

#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Krate {
namespace DSP {

/// @brief Spectral flux-based transient detector for onset detection in
///        magnitude spectra (Layer 1 primitive).
///
/// Computes half-wave rectified spectral flux per frame and compares against
/// an adaptive threshold derived from an exponentially-weighted moving average
/// of past flux values. Designed for integration with the PhaseVocoderPitchShifter
/// for transient-aware phase reset.
///
/// @par Thread Safety
/// Not thread-safe. Must be called from a single thread.
///
/// @par Real-Time Safety
/// - `prepare()`: NOT real-time safe (allocates memory via std::vector). Declared
///   noexcept intentionally: OOM during prepare() is unrecoverable in a DSP context
///   and std::terminate() is the appropriate response.
/// - `detect()`, `reset()`, getters, setters: Real-time safe (noexcept, no alloc)
class SpectralTransientDetector {
public:
    // =========================================================================
    // Construction
    // =========================================================================

    SpectralTransientDetector() noexcept = default;
    ~SpectralTransientDetector() noexcept = default;

    // Movable
    SpectralTransientDetector(SpectralTransientDetector&&) noexcept = default;
    SpectralTransientDetector& operator=(SpectralTransientDetector&&) noexcept = default;

    // Non-copyable (owns internal buffer)
    SpectralTransientDetector(const SpectralTransientDetector&) = delete;
    SpectralTransientDetector& operator=(const SpectralTransientDetector&) = delete;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Prepare the detector for a given number of frequency bins.
    ///
    /// Allocates internal storage for previous magnitudes. If called again
    /// with a different bin count, reallocates and fully resets all state.
    /// If called with the same bin count, still resets all state.
    ///
    /// @param numBins Number of magnitude bins (typically fftSize/2 + 1)
    /// @note NOT real-time safe (allocates memory). noexcept is intentional:
    ///       OOM during prepare() causes std::terminate() by design.
    void prepare(std::size_t numBins) noexcept
    {
        numBins_ = numBins;
        prevMagnitudes_.assign(numBins, 0.0f);
        runningAverage_ = 0.0f;
        lastFlux_ = 0.0f;
        transientDetected_ = false;
        isFirstFrame_ = true;
    }

    /// @brief Reset all detection state without reallocating.
    ///
    /// Clears previous magnitudes, running average, lastFlux_, and detection flag.
    /// Configuration parameters (threshold_ and smoothingCoeff_) are preserved.
    /// The next detect() call will be treated as the first frame.
    ///
    /// @note Real-time safe (no allocations)
    void reset() noexcept
    {
        std::fill(prevMagnitudes_.begin(), prevMagnitudes_.end(), 0.0f);
        runningAverage_ = 0.0f;
        lastFlux_ = 0.0f;
        transientDetected_ = false;
        isFirstFrame_ = true;
    }

    // =========================================================================
    // Detection
    // =========================================================================

    /// @brief Analyze a magnitude spectrum frame for transient onset.
    ///
    /// Computes half-wave rectified spectral flux between the current and
    /// previous magnitude frames. Compares flux against the adaptive
    /// threshold (multiplier * running average). Updates internal state.
    ///
    /// On the first call after prepare() or reset(), detection is
    /// suppressed (always returns false) but the running average is seeded.
    ///
    /// @param magnitudes Pointer to contiguous array of magnitude values
    /// @param numBins Number of elements in the magnitudes array
    /// @return true if a transient was detected on this frame
    [[nodiscard]] bool detect(const float* magnitudes, std::size_t numBins) noexcept
    {
        // FR-016: Debug assert on bin-count mismatch; release clamp
        assert(numBins == numBins_ &&
               "SpectralTransientDetector::detect() bin count mismatch with prepare()");

        const std::size_t effectiveBins = std::min(numBins, numBins_);

        // Edge case: zero effective bins
        if (effectiveBins == 0) {
            lastFlux_ = 0.0f;
            transientDetected_ = false;
            // Update running average with flux=0
            runningAverage_ = smoothingCoeff_ * runningAverage_;
            // Enforce floor (FR-011)
            runningAverage_ = std::max(runningAverage_, kRunningAverageFloor);
            isFirstFrame_ = false;
            return false;
        }

        // FR-001: Compute half-wave rectified spectral flux
        // SF(n) = sum(max(0, |X_n[k]| - |X_{n-1}[k]|))
        float flux = 0.0f;
        for (std::size_t k = 0; k < effectiveBins; ++k) {
            const float diff = magnitudes[k] - prevMagnitudes_[k];
            if (diff > 0.0f) {
                flux += diff;
            }
        }

        lastFlux_ = flux;

        // FR-002: Update EMA running average
        // runningAvg(n) = alpha * runningAvg(n-1) + (1 - alpha) * SF(n)
        runningAverage_ = smoothingCoeff_ * runningAverage_ +
                          (1.0f - smoothingCoeff_) * flux;

        // FR-011: Enforce minimum floor on running average
        runningAverage_ = std::max(runningAverage_, kRunningAverageFloor);

        // FR-010: First-frame suppression
        if (isFirstFrame_) {
            isFirstFrame_ = false;
            transientDetected_ = false;
            // FR-006: Store current magnitudes for next frame
            std::copy(magnitudes, magnitudes + effectiveBins, prevMagnitudes_.begin());
            return false;
        }

        // FR-002: Compare flux against adaptive threshold
        transientDetected_ = flux > threshold_ * runningAverage_;

        // FR-006: Store current magnitudes for next frame
        std::copy(magnitudes, magnitudes + effectiveBins, prevMagnitudes_.begin());

        return transientDetected_;
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Set the threshold multiplier for transient detection.
    /// @param multiplier Threshold multiplier [1.0, 5.0]. Default: 1.5
    void setThreshold(float multiplier) noexcept
    {
        threshold_ = std::clamp(multiplier, 1.0f, 5.0f);
    }

    /// @brief Set the smoothing coefficient for the running average.
    /// @param coeff Smoothing coefficient [0.8, 0.99]. Default: 0.95
    void setSmoothingCoeff(float coeff) noexcept
    {
        smoothingCoeff_ = std::clamp(coeff, 0.8f, 0.99f);
    }

    // =========================================================================
    // Query (most recent detect() call)
    // =========================================================================

    /// @brief Get the raw spectral flux from the most recent detect() call.
    [[nodiscard]] float getSpectralFlux() const noexcept { return lastFlux_; }

    /// @brief Get the current running average of spectral flux.
    [[nodiscard]] float getRunningAverage() const noexcept { return runningAverage_; }

    /// @brief Get the detection result from the most recent detect() call.
    [[nodiscard]] bool isTransient() const noexcept { return transientDetected_; }

private:
    /// Minimum floor for running average to prevent division-by-zero
    /// or ultra-sensitive detection after prolonged silence (FR-011)
    static constexpr float kRunningAverageFloor = 1e-10f;

    std::vector<float> prevMagnitudes_;       ///< Previous frame magnitudes
    float runningAverage_ = 0.0f;             ///< EMA of spectral flux
    float threshold_ = 1.5f;                  ///< Detection threshold multiplier
    float smoothingCoeff_ = 0.95f;            ///< EMA coefficient (alpha)
    float lastFlux_ = 0.0f;                   ///< Most recent flux value
    bool transientDetected_ = false;          ///< Most recent detection result
    bool isFirstFrame_ = true;                ///< First-frame suppression flag
    std::size_t numBins_ = 0;                 ///< Prepared bin count
};

} // namespace DSP
} // namespace Krate
