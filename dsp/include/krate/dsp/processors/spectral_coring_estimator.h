// ==============================================================================
// SpectralCoringEstimator - Lightweight Residual Estimation via Spectral Coring
// ==============================================================================
// Layer 2: Processors
// Spec: specs/117-live-sidechain-mode/spec.md
// Covers: FR-007 (spectral coring residual estimation)
//
// Estimates the stochastic (noise) component by identifying harmonic bins in the
// STFT spectrum and measuring energy in the remaining inter-harmonic bins.
// Zero additional analysis latency compared to full subtraction-based residual
// extraction (ResidualAnalyzer).
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept estimateResidual, no allocations)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 2 (depends on Layer 0 core, Layer 1 spectral_buffer)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_types.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Krate::DSP {

/// @brief Lightweight residual estimation via spectral coring.
///
/// Estimates the stochastic (noise) component by zeroing bins at harmonic
/// frequencies and measuring energy in the remaining inter-harmonic bins.
/// Produces ResidualFrame output compatible with ResidualSynthesizer.
///
/// Used in live sidechain mode where full subtraction-based residual
/// extraction (ResidualAnalyzer) would add one frame of latency.
class SpectralCoringEstimator {
public:
    SpectralCoringEstimator() = default;
    ~SpectralCoringEstimator() = default;

    // Non-copyable, movable
    SpectralCoringEstimator(const SpectralCoringEstimator&) = delete;
    SpectralCoringEstimator& operator=(const SpectralCoringEstimator&) = delete;
    SpectralCoringEstimator(SpectralCoringEstimator&&) noexcept = default;
    SpectralCoringEstimator& operator=(SpectralCoringEstimator&&) noexcept = default;

    /// Configure for the given FFT parameters.
    /// @param fftSize FFT size (must be power of 2)
    /// @param sampleRate Sample rate in Hz
    void prepare(size_t fftSize, float sampleRate)
    {
        fftSize_ = fftSize;
        sampleRate_ = sampleRate;
        numBins_ = fftSize / 2 + 1;
        prepared_ = true;
    }

    /// Reset internal state.
    void reset()
    {
        fftSize_ = 0;
        sampleRate_ = 0.0f;
        numBins_ = 0;
        prepared_ = false;
    }

    /// Estimate residual by zeroing harmonic bins in the spectrum.
    /// @param spectrum STFT spectral buffer with magnitude data
    /// @param frame Harmonic frame identifying partial frequencies
    /// @return ResidualFrame with band energies from non-harmonic bins
    [[nodiscard]] ResidualFrame estimateResidual(
        const SpectralBuffer& spectrum,
        const HarmonicFrame& frame) noexcept
    {
        ResidualFrame result{};

        if (!prepared_ || numBins_ == 0 || !spectrum.isPrepared())
            return result;

        const float binSpacing = sampleRate_ / static_cast<float>(fftSize_);
        if (binSpacing <= 0.0f)
            return result;

        const auto& bandEdges = getResidualBandEdges();
        const float nyquist = sampleRate_ * 0.5f;

        // For each bin, check if it's near a harmonic. If not, accumulate
        // its energy into the appropriate residual band.
        for (size_t b = 1; b < numBins_; ++b)
        {
            const float binFreq = static_cast<float>(b) * binSpacing;
            const float mag = spectrum.getMagnitude(b);
            const float binEnergy = mag * mag;

            // Check if this bin is near any harmonic partial
            bool isHarmonic = false;
            for (int p = 0; p < frame.numPartials; ++p)
            {
                const float partialFreq = frame.partials[static_cast<size_t>(p)].frequency;
                if (partialFreq <= 0.0f)
                    continue;

                const float distBins = std::abs(binFreq - partialFreq) / binSpacing;
                if (distBins < coringBandwidthBins_)
                {
                    isHarmonic = true;
                    break;
                }
            }

            if (isHarmonic)
                continue;

            // Accumulate into the correct residual band
            // Band edges are normalized to Nyquist
            const float normFreq = binFreq / nyquist;

            for (size_t band = 0; band < kResidualBands; ++band)
            {
                if (normFreq >= bandEdges[band] && normFreq < bandEdges[band + 1])
                {
                    result.bandEnergies[band] += binEnergy;
                    break;
                }
            }

            result.totalEnergy += binEnergy;
        }

        // Convert accumulated squared magnitudes to RMS-like values
        // Take sqrt to match the ResidualFrame convention (RMS energy per band)
        for (size_t band = 0; band < kResidualBands; ++band)
        {
            result.bandEnergies[band] = std::sqrt(result.bandEnergies[band]);
        }
        result.totalEnergy = std::sqrt(result.totalEnergy);

        // Spectral coring does not detect transients
        result.transientFlag = false;

        return result;
    }

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] size_t fftSize() const noexcept { return fftSize_; }

private:
    size_t fftSize_ = 0;
    float sampleRate_ = 0.0f;
    size_t numBins_ = 0;
    float coringBandwidthBins_ = 1.5f;
    bool prepared_ = false;
};

} // namespace Krate::DSP
