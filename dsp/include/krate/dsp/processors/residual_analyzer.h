// ==============================================================================
// Layer 2: DSP Processor - Residual Analyzer
// ==============================================================================
// Extracts the residual (stochastic) component from an audio signal by
// subtracting resynthesized harmonics and characterizing the residual
// spectral envelope.
//
// Operates on the background analysis thread (NOT real-time safe).
// Used during offline sample analysis in the Innexus SampleAnalyzer pipeline.
//
// Algorithm (per frame):
//   1. Resynthesize harmonic signal from tracked Partial data (FR-002, FR-003)
//   2. Subtract from original audio: residual = original - harmonics (FR-001)
//   3. Apply Hann window to residual, then forward FFT (FR-004)
//   4. Extract spectral envelope (16 log-spaced bands) (FR-004, FR-005)
//   5. Compute total residual energy: RMS of magnitude spectrum (FR-006)
//   6. Detect transients via spectral flux (FR-007)
//   7. Output ResidualFrame (FR-008)
//
// Spec: specs/116-residual-noise-model/spec.md
// Covers: FR-001 to FR-012
// ==============================================================================

#pragma once

#include <krate/dsp/processors/residual_types.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/primitives/spectral_transient_detector.h>
#include <krate/dsp/core/math_constants.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Krate::DSP {

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

    void prepare(size_t fftSize, size_t hopSize, float sampleRate) noexcept
    {
        fftSize_ = fftSize;
        hopSize_ = hopSize;
        sampleRate_ = sampleRate;

        fft_.prepare(fftSize);
        spectralBuffer_.prepare(fftSize);
        transientDetector_.prepare(fftSize / 2 + 1);

        harmonicBuffer_.resize(fftSize, 0.0f);
        residualBuffer_.resize(fftSize, 0.0f);
        windowedBuffer_.resize(fftSize, 0.0f);
        magnitudeBuffer_.resize(fftSize / 2 + 1, 0.0f);

        // Generate Hann window
        window_.resize(fftSize);
        for (size_t i = 0; i < fftSize; ++i)
        {
            window_[i] = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(i)
                                                    / static_cast<float>(fftSize)));
        }

        prepared_ = true;
    }

    void reset() noexcept
    {
        transientDetector_.reset();
        std::fill(harmonicBuffer_.begin(), harmonicBuffer_.end(), 0.0f);
        std::fill(residualBuffer_.begin(), residualBuffer_.end(), 0.0f);
        std::fill(windowedBuffer_.begin(), windowedBuffer_.end(), 0.0f);
        std::fill(magnitudeBuffer_.begin(), magnitudeBuffer_.end(), 0.0f);
    }

    // =========================================================================
    // Analysis (Background Thread Only)
    // =========================================================================

    [[nodiscard]] ResidualFrame analyzeFrame(
        const float* originalAudio,
        size_t numSamples,
        const HarmonicFrame& frame) noexcept
    {
        ResidualFrame result;

        if (!prepared_ || originalAudio == nullptr || numSamples < fftSize_)
            return result;

        const size_t numBins = fftSize_ / 2 + 1;

        // Step 1: Resynthesize harmonics from tracked partials (FR-002, FR-003)
        resynthesizeHarmonics(frame, harmonicBuffer_.data(), fftSize_);

        // Step 2: Subtract harmonics from original (FR-001)
        for (size_t i = 0; i < fftSize_; ++i)
        {
            residualBuffer_[i] = originalAudio[i] - harmonicBuffer_[i];
        }

        // Step 3: Apply Hann window and FFT
        for (size_t i = 0; i < fftSize_; ++i)
        {
            windowedBuffer_[i] = residualBuffer_[i] * window_[i];
        }

        fft_.forward(windowedBuffer_.data(), spectralBuffer_.data());

        // Step 4: Extract magnitudes
        const Complex* spectrum = spectralBuffer_.data();
        for (size_t k = 0; k < numBins; ++k)
        {
            magnitudeBuffer_[k] = spectrum[k].magnitude();
        }

        // Step 5: Extract spectral envelope (FR-004, FR-005)
        extractSpectralEnvelope(
            magnitudeBuffer_.data(), numBins, result.bandEnergies);

        // Step 6: Compute total energy (FR-006)
        result.totalEnergy = computeTotalEnergy(magnitudeBuffer_.data(), numBins);

        // Step 7: Detect transients (FR-007)
        result.transientFlag = transientDetector_.detect(
            magnitudeBuffer_.data(), numBins);

        // Clamp all band energies to >= 0 (FR-011)
        for (size_t i = 0; i < kResidualBands; ++i)
        {
            result.bandEnergies[i] = std::max(result.bandEnergies[i], 0.0f);
        }
        result.totalEnergy = std::max(result.totalEnergy, 0.0f);

        return result;
    }

    // =========================================================================
    // Query
    // =========================================================================

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] size_t fftSize() const noexcept { return fftSize_; }
    [[nodiscard]] size_t hopSize() const noexcept { return hopSize_; }

private:
    // =========================================================================
    // Internal methods
    // =========================================================================

    void resynthesizeHarmonics(
        const HarmonicFrame& frame,
        float* output,
        size_t numSamples) noexcept
    {
        std::fill_n(output, numSamples, 0.0f);

        for (int p = 0; p < frame.numPartials; ++p)
        {
            const auto& partial = frame.partials[static_cast<size_t>(p)];
            if (partial.amplitude <= 0.0f)
                continue;

            const float freq = partial.frequency;
            const float amp = partial.amplitude;
            const float phi = partial.phase;
            const float omega = kTwoPi * freq / sampleRate_;

            for (size_t n = 0; n < numSamples; ++n)
            {
                output[n] += amp * std::sin(phi + omega * static_cast<float>(n));
            }
        }
    }

    void extractSpectralEnvelope(
        const float* magnitudes,
        size_t numBins,
        std::array<float, kResidualBands>& bandEnergies) noexcept
    {
        const auto& edges = getResidualBandEdges();

        for (size_t b = 0; b < kResidualBands; ++b)
        {
            // Map edge frequencies to bin indices
            // edges are normalized to [0, 1] of Nyquist
            // bin k corresponds to frequency k / (numBins - 1) of Nyquist
            float lowEdge = edges[b];
            float highEdge = edges[b + 1];

            size_t lowBin = static_cast<size_t>(
                lowEdge * static_cast<float>(numBins - 1) + 0.5f);
            size_t highBin = static_cast<size_t>(
                highEdge * static_cast<float>(numBins - 1) + 0.5f);

            lowBin = std::min(lowBin, numBins - 1);
            highBin = std::min(highBin, numBins - 1);

            if (highBin <= lowBin)
            {
                // At least include one bin
                highBin = lowBin + 1;
                if (highBin >= numBins)
                    highBin = numBins - 1;
            }

            // Compute RMS of magnitudes in this band
            float sumSq = 0.0f;
            size_t count = 0;
            for (size_t k = lowBin; k <= highBin && k < numBins; ++k)
            {
                sumSq += magnitudes[k] * magnitudes[k];
                ++count;
            }

            if (count > 0)
            {
                bandEnergies[b] = std::sqrt(sumSq / static_cast<float>(count));
            }
            else
            {
                bandEnergies[b] = 0.0f;
            }
        }
    }

    [[nodiscard]] float computeTotalEnergy(
        const float* magnitudes,
        size_t numBins) noexcept
    {
        float sumSq = 0.0f;
        for (size_t k = 0; k < numBins; ++k)
        {
            sumSq += magnitudes[k] * magnitudes[k];
        }
        return std::sqrt(sumSq / static_cast<float>(numBins));
    }

    // =========================================================================
    // Internal state
    // =========================================================================
    FFT fft_;
    SpectralBuffer spectralBuffer_;
    SpectralTransientDetector transientDetector_;

    std::vector<float> harmonicBuffer_;
    std::vector<float> residualBuffer_;
    std::vector<float> windowedBuffer_;
    std::vector<float> window_;
    std::vector<float> magnitudeBuffer_;

    float sampleRate_ = 0.0f;
    size_t fftSize_ = 0;
    size_t hopSize_ = 0;
    bool prepared_ = false;
};

} // namespace Krate::DSP
