// ==============================================================================
// CONTRACT: ResidualAnalyzer
// ==============================================================================
// Location: dsp/include/krate/dsp/processors/residual_analyzer.h
// Layer: 2 (Processors)
// Namespace: Krate::DSP
// Dependencies: Layer 0-1 only (FFT, SpectralBuffer, SpectralTransientDetector)
//   Note: The STFT streaming class is NOT used. Windowing (Hann) and FFT are
//   performed manually per frame using fft_ + window_ for offline analysis.
//
// This is the API contract -- the actual implementation will match these
// signatures. Comments describe the intended behavior and requirements.
// ==============================================================================

#pragma once

#include <krate/dsp/processors/residual_types.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/primitives/spectral_transient_detector.h>

#include <vector>

namespace Krate {
namespace DSP {

/// @brief Extracts the residual (stochastic) component from an audio signal
///        by subtracting resynthesized harmonics and characterizing the residual
///        spectral envelope.
///
/// Operates on the background analysis thread (NOT real-time safe).
/// Used during offline sample analysis in the Innexus SampleAnalyzer pipeline.
///
/// Algorithm (per frame):
///   1. Resynthesize harmonic signal from tracked Partial data (FR-002, FR-003)
///   2. Subtract from original audio: residual = original - harmonics (FR-001)
///   3. Apply Hann window (window_ buffer) to residual, then forward FFT via fft_
///      into spectralBuffer_. The STFT streaming class is NOT used -- windowing
///      and FFT are performed manually for per-frame offline analysis.
///   4. Extract spectral envelope (16 log-spaced bands) (FR-004, FR-005)
///   5. Compute total residual energy: RMS of magnitude spectrum (FR-006)
///   6. Detect transients via spectral flux (FR-007)
///   7. Output ResidualFrame (FR-008)
///
/// @note NOT real-time safe. Allocates memory during prepare().
///       All analysis runs on background thread (FR-010).
class ResidualAnalyzer {
public:
    ResidualAnalyzer() noexcept = default;
    ~ResidualAnalyzer() noexcept = default;

    // Non-copyable, movable
    ResidualAnalyzer(const ResidualAnalyzer&) = delete;
    ResidualAnalyzer& operator=(const ResidualAnalyzer&) = delete;
    ResidualAnalyzer(ResidualAnalyzer&&) noexcept = default;
    ResidualAnalyzer& operator=(ResidualAnalyzer&&) noexcept = default;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Prepare the analyzer for a given FFT configuration.
    ///
    /// @param fftSize  FFT size for residual spectral analysis (must match
    ///                 the short-window STFT used for harmonic analysis)
    /// @param hopSize  Hop size in samples (must match short-window hop)
    /// @param sampleRate  Audio sample rate in Hz
    ///
    /// @note NOT real-time safe (allocates memory).
    void prepare(size_t fftSize, size_t hopSize, float sampleRate) noexcept;

    /// @brief Reset all internal state without reallocating.
    void reset() noexcept;

    // =========================================================================
    // Analysis (Background Thread Only)
    // =========================================================================

    /// @brief Analyze one frame of audio and produce a ResidualFrame.
    ///
    /// This is the main analysis function, called once per analysis frame
    /// in the SampleAnalyzer pipeline.
    ///
    /// @param originalAudio  Pointer to the original audio segment for this
    ///                       frame (fftSize samples, windowed or unwindowed)
    /// @param numSamples     Number of samples in originalAudio (must be >= fftSize)
    /// @param frame          The HarmonicFrame for this time position (provides
    ///                       tracked partials for harmonic subtraction)
    /// @return ResidualFrame containing spectral envelope, energy, and transient flag
    ///
    /// @pre prepare() has been called
    /// @note NOT real-time safe
    [[nodiscard]] ResidualFrame analyzeFrame(
        const float* originalAudio,
        size_t numSamples,
        const HarmonicFrame& frame) noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] size_t fftSize() const noexcept;
    [[nodiscard]] size_t hopSize() const noexcept;

private:
    // --- Internal methods ---

    /// @brief Resynthesize the harmonic signal from tracked partials (FR-002, FR-003).
    ///
    /// For each active partial in the HarmonicFrame, generates:
    ///   signal[n] += amplitude * sin(phase + 2*pi*frequency*n/sampleRate)
    ///
    /// Uses actual tracked frequencies (not idealized n*F0) for tight cancellation.
    void resynthesizeHarmonics(
        const HarmonicFrame& frame,
        float* output,
        size_t numSamples) noexcept;

    /// @brief Extract the 16-band spectral envelope from a magnitude spectrum (FR-004, FR-005).
    ///
    /// Computes the RMS energy in each of 16 log-spaced frequency bands.
    void extractSpectralEnvelope(
        const float* magnitudes,
        size_t numBins,
        std::array<float, kResidualBands>& bandEnergies) noexcept;

    /// @brief Compute total residual energy from a magnitude spectrum (FR-006).
    ///
    /// Returns sqrt(sum(magnitudes[k]^2) / numBins) -- RMS of the magnitude spectrum.
    [[nodiscard]] float computeTotalEnergy(
        const float* magnitudes,
        size_t numBins) noexcept;

    // --- Internal state ---
    FFT fft_;
    SpectralBuffer spectralBuffer_;
    SpectralTransientDetector transientDetector_;

    std::vector<float> harmonicBuffer_;    ///< Resynthesized harmonic signal
    std::vector<float> residualBuffer_;    ///< residual = original - harmonic
    std::vector<float> windowedBuffer_;    ///< Windowed residual for FFT
    std::vector<float> window_;            ///< Analysis window (Hann)
    std::vector<float> magnitudeBuffer_;   ///< Magnitude spectrum for envelope extraction

    float sampleRate_ = 0.0f;
    size_t fftSize_ = 0;
    size_t hopSize_ = 0;
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
