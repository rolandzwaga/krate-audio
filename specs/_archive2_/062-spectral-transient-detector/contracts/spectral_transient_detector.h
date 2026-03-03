// ==============================================================================
// API Contract: SpectralTransientDetector
// ==============================================================================
// This file defines the public API contract for the SpectralTransientDetector.
// It is NOT the implementation -- it is a reference for the implementation plan.
//
// Layer: 1 (Primitives)
// Location: dsp/include/krate/dsp/primitives/spectral_transient_detector.h
// Namespace: Krate::DSP
// Feature: 062-spectral-transient-detector
// ==============================================================================

#pragma once

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
/// @par Algorithm (Duxbury et al. 2002, Dixon 2006)
/// ```
/// SF(n) = sum(max(0, |X_n[k]| - |X_{n-1}[k]|))  for k = 0..numBins-1
/// runningAvg(n) = alpha * runningAvg(n-1) + (1 - alpha) * SF(n)
/// transient = SF(n) > threshold * runningAvg(n)
/// ```
///
/// @par Thread Safety
/// Not thread-safe. Must be called from a single thread.
///
/// @par Real-Time Safety
/// - `prepare()`: NOT real-time safe (allocates memory via std::vector). Declared
///   noexcept intentionally: OOM during prepare() is unrecoverable in a DSP context
///   and std::terminate() is the appropriate response (consistent with DSP plugin
///   lifecycle where host calls prepare() outside the audio thread before processing).
/// - `detect()`, `reset()`, getters, setters: Real-time safe (noexcept, no alloc)
///
/// @par Usage
/// @code
/// SpectralTransientDetector detector;
/// detector.prepare(2049);  // numBins for 4096-point FFT
///
/// // In processFrame():
/// bool isTransient = detector.detect(magnitudes, numBins);
/// if (isTransient) {
///     // Reset synthesis phases to analysis phases
/// }
/// @endcode
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
    /// @pre numBins > 0. If numBins == 0, the detector enters an invalid state
    ///      where subsequent detect() calls return false without processing.
    /// @post detect() can be called with up to numBins bins
    /// @note NOT real-time safe (allocates memory). noexcept is intentional:
    ///       OOM during prepare() causes std::terminate() by design.
    void prepare(std::size_t numBins) noexcept;

    /// @brief Reset all detection state without reallocating.
    ///
    /// Clears previous magnitudes, running average, lastFlux_, and detection flag.
    /// Configuration parameters (threshold_ and smoothingCoeff_) are preserved.
    /// The next detect() call will be treated as the first frame
    /// (detection suppressed, flux seeds the running average).
    ///
    /// @note Real-time safe (no allocations)
    void reset() noexcept;

    // =========================================================================
    // Detection
    // =========================================================================

    /// @brief Analyze a magnitude spectrum frame for transient onset.
    ///
    /// Computes half-wave rectified spectral flux between the current and
    /// previous magnitude frames. Compares flux against the adaptive
    /// threshold (multiplier * running average). Updates internal state
    /// (previous magnitudes, running average, detection flag).
    ///
    /// On the first call after prepare() or reset(), detection is
    /// suppressed (always returns false) but the running average is seeded.
    ///
    /// @param magnitudes Pointer to contiguous array of magnitude values
    /// @param numBins Number of elements in the magnitudes array
    /// @return true if a transient was detected on this frame
    ///
    /// @pre magnitudes != nullptr
    /// @pre numBins should match the value passed to prepare()
    ///      (debug assert on mismatch; release: clamp to min)
    /// @note Real-time safe (no allocations, no exceptions, no locks)
    [[nodiscard]] bool detect(const float* magnitudes, std::size_t numBins) noexcept;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Set the threshold multiplier for transient detection.
    ///
    /// A transient is detected when spectral flux exceeds
    /// (threshold * runningAverage). Higher values reduce sensitivity
    /// (fewer detections); lower values increase sensitivity.
    ///
    /// @param multiplier Threshold multiplier [1.0, 5.0]. Default: 1.5
    void setThreshold(float multiplier) noexcept;

    /// @brief Set the smoothing coefficient for the running average.
    ///
    /// Controls how quickly the running average adapts to flux changes.
    /// Higher values make it slower-moving (more historical context);
    /// lower values make it more responsive to recent changes.
    ///
    /// @param coeff Smoothing coefficient [0.8, 0.99]. Default: 0.95
    void setSmoothingCoeff(float coeff) noexcept;

    // =========================================================================
    // Query (most recent detect() call)
    // =========================================================================

    /// @brief Get the raw spectral flux from the most recent detect() call.
    /// @return Half-wave rectified spectral flux scalar (>= 0)
    [[nodiscard]] float getSpectralFlux() const noexcept;

    /// @brief Get the current running average of spectral flux.
    /// @return Exponential moving average of past flux values
    [[nodiscard]] float getRunningAverage() const noexcept;

    /// @brief Get the detection result from the most recent detect() call.
    /// @return true if the most recent frame was classified as a transient
    [[nodiscard]] bool isTransient() const noexcept;

private:
    std::vector<float> prevMagnitudes_;       // Previous frame magnitudes
    float runningAverage_ = 0.0f;             // EMA of spectral flux
    float threshold_ = 1.5f;                  // Detection threshold multiplier
    float smoothingCoeff_ = 0.95f;            // EMA coefficient (alpha)
    float lastFlux_ = 0.0f;                   // Most recent flux value
    bool transientDetected_ = false;          // Most recent detection result
    bool isFirstFrame_ = true;                // First-frame suppression flag
    std::size_t numBins_ = 0;                 // Prepared bin count
};

} // namespace DSP
} // namespace Krate
