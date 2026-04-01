// ==============================================================================
// Layer 2: DSP Processor - Residual Synthesizer
// ==============================================================================
// Resynthesizes the noise (stochastic) component in real time from stored
// ResidualFrame data using FFT-domain spectral envelope shaping.
//
// Algorithm (per frame):
//   1. Generate white noise (fftSize samples) from deterministic PRNG (FR-013, FR-030)
//   2. Forward FFT the noise via fft_ into spectralBuffer_
//   3. Multiply noise spectrum by interpolated spectral envelope (FR-014)
//   4. Apply brightness tilt to envelope (FR-022)
//   5. Scale by frame energy * transient emphasis (FR-016, FR-023)
//   6. Feed shaped spectrum to OverlapAdd for overlap-add reconstruction
//
// Spec: specs/116-residual-noise-model/spec.md
// Covers: FR-013 to FR-020, FR-029, FR-030
// ==============================================================================

#pragma once

#include <krate/dsp/processors/residual_types.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/primitives/stft.h>           // OverlapAdd
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/core/random.h>                // Xorshift32

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Krate::DSP {

class ResidualSynthesizer {
public:
    /// PRNG seed constant -- reset on every prepare() for deterministic output (FR-030)
    static constexpr uint32_t kPrngSeed = 12345;

    ResidualSynthesizer() noexcept = default;
    ~ResidualSynthesizer() noexcept = default;

    // Non-copyable, movable
    ResidualSynthesizer(const ResidualSynthesizer&) = delete;
    ResidualSynthesizer& operator=(const ResidualSynthesizer&) = delete;
    ResidualSynthesizer(ResidualSynthesizer&&) noexcept = default;
    ResidualSynthesizer& operator=(ResidualSynthesizer&&) noexcept = default;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    void prepare(size_t fftSize, size_t hopSize, float sampleRate) noexcept
    {
        fftSize_ = fftSize;
        hopSize_ = hopSize;
        sampleRate_ = sampleRate;
        numBins_ = fftSize / 2 + 1;

        // Pre-allocate all buffers (FR-020)
        noiseBuffer_.resize(fftSize, 0.0f);
        envelopeBuffer_.resize(numBins_, 0.0f);
        outputBuffer_.resize(hopSize, 0.0f);

        fft_.prepare(fftSize);
        overlapAdd_.prepare(fftSize, hopSize, WindowType::Hann, 0.0f, true);
        spectralBuffer_.prepare(fftSize);

        rng_.seed(kPrngSeed);

        prepared_ = true;
        frameLoaded_ = false;
        cursor_ = 0;
    }

    void reset() noexcept
    {
        overlapAdd_.reset();
        rng_.seed(kPrngSeed);
        frameLoaded_ = false;
        cursor_ = 0;
        std::fill(outputBuffer_.begin(), outputBuffer_.end(), 0.0f);
    }

    // =========================================================================
    // Frame Loading (Real-Time Safe)
    // =========================================================================

    void loadFrame(
        const ResidualFrame& frame,
        float brightness = 0.0f,
        float transientEmphasis = 0.0f) noexcept
    {
        if (!prepared_)
            return;

        // Step 1: Generate white noise (FR-013)
        for (size_t i = 0; i < fftSize_; ++i)
        {
            noiseBuffer_[i] = rng_.nextFloat();
        }

        // Step 2: Forward FFT noise into spectralBuffer_
        fft_.forward(noiseBuffer_.data(), spectralBuffer_.data());

        // Step 3: Interpolate spectral envelope from band energies
        interpolateEnvelope(frame.bandEnergies);

        // Step 4: Apply brightness tilt (FR-022)
        if (brightness != 0.0f)
        {
            applyBrightnessTilt(brightness);
        }

        // Step 4b: Normalize envelope to unit RMS (shape only, no amplitude).
        // Band energies are FFT-domain magnitudes. Normalizing ensures the
        // envelope only controls spectral shape while energyScale controls level.
        float envSumSq = 0.0f;
        for (size_t k = 0; k < numBins_; ++k)
            envSumSq += envelopeBuffer_[k] * envelopeBuffer_[k];
        if (envSumSq > 1e-20f)
        {
            float invEnvRms = std::sqrt(static_cast<float>(numBins_) / envSumSq);
            for (size_t k = 0; k < numBins_; ++k)
                envelopeBuffer_[k] *= invEnvRms;
        }

        // Step 5: Compute energy scale (FR-016, FR-023)
        // totalEnergy is RMS of residual FFT magnitudes (N-scaled from pffft).
        // Divide by sqrt(fftSize) to convert to a scale compatible with the
        // harmonic oscillator bank output. The sqrt accounts for the noise
        // FFT's per-bin magnitude being ~sqrt(N) (white noise property).
        float energyScale = frame.totalEnergy
            / std::sqrt(static_cast<float>(fftSize_));
        if (frame.transientFlag && transientEmphasis > 0.0f)
        {
            energyScale *= (1.0f + transientEmphasis);
        }

        // Step 6: Multiply spectral bins by envelope * energyScale
        Complex* data = spectralBuffer_.data();
        for (size_t k = 0; k < numBins_; ++k)
        {
            float scale = envelopeBuffer_[k] * energyScale;
            data[k].real *= scale;
            data[k].imag *= scale;
        }

        // Step 7: Overlap-add synthesis (FR-015, FR-017)
        overlapAdd_.synthesize(spectralBuffer_);

        if (overlapAdd_.samplesAvailable() >= hopSize_)
        {
            overlapAdd_.pullSamples(outputBuffer_.data(), hopSize_);
        }
        else
        {
            std::fill(outputBuffer_.begin(), outputBuffer_.end(), 0.0f);
        }

        frameLoaded_ = true;
        cursor_ = 0;
    }

    // =========================================================================
    // Audio Output (Real-Time Safe)
    // =========================================================================
    // SC-007 Audit (2026-03-04, implementation agent):
    // loadFrame(), process(), processBlock() use ONLY pre-allocated buffers:
    //   - noiseBuffer_ (vector, sized in prepare())
    //   - envelopeBuffer_ (vector, sized in prepare())
    //   - outputBuffer_ (vector, sized in prepare())
    //   - spectralBuffer_ (SpectralBuffer, prepared in prepare())
    //   - fft_ (FFT, prepared in prepare())
    //   - overlapAdd_ (OverlapAdd, prepared in prepare())
    //   - rng_ (Xorshift32, no alloc)
    // No std::vector::push_back(), new, malloc, or container resize in these paths.
    // No locks, exceptions, or I/O. All real-time safe.

    /// @param feedbackVelocity Resonator feedback velocity (unused by residual model,
    ///        accepted for unified exciter interface FR-015).
    [[nodiscard]] float process(float /*feedbackVelocity*/) noexcept
    {
        if (!frameLoaded_ || cursor_ >= hopSize_)
            return 0.0f;

        return outputBuffer_[cursor_++];
    }

    void processBlock(float* output, size_t numSamples) noexcept
    {
        if (!frameLoaded_)
        {
            std::fill_n(output, numSamples, 0.0f);
            return;
        }

        for (size_t i = 0; i < numSamples; ++i)
        {
            if (cursor_ < hopSize_)
            {
                output[i] = outputBuffer_[cursor_++];
            }
            else
            {
                output[i] = 0.0f;
            }
        }
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

    void interpolateEnvelope(
        const std::array<float, kResidualBands>& bandEnergies) noexcept
    {
        const auto& edges = getResidualBandEdges();
        const auto& centers = getResidualBandCenters();

        // For each FFT bin, find the surrounding band centers and interpolate
        for (size_t k = 0; k < numBins_; ++k)
        {
            // Frequency ratio of this bin (0.0 to 1.0 of Nyquist)
            float freqRatio = static_cast<float>(k) / static_cast<float>(numBins_ - 1);

            // Find which band this bin falls into
            size_t bandIdx = 0;
            for (size_t b = 0; b < kResidualBands; ++b)
            {
                if (freqRatio >= edges[b] && freqRatio < edges[b + 1])
                {
                    bandIdx = b;
                    break;
                }
                if (b == kResidualBands - 1)
                    bandIdx = kResidualBands - 1;
            }

            // Linear interpolation between this band and adjacent
            float center = centers[bandIdx];
            float energy = bandEnergies[bandIdx];

            if (freqRatio <= center && bandIdx > 0)
            {
                // Interpolate between previous band and this band
                float prevCenter = centers[bandIdx - 1];
                float prevEnergy = bandEnergies[bandIdx - 1];
                float t = (center > prevCenter)
                    ? (freqRatio - prevCenter) / (center - prevCenter)
                    : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                envelopeBuffer_[k] = prevEnergy + t * (energy - prevEnergy);
            }
            else if (freqRatio > center && bandIdx < kResidualBands - 1)
            {
                // Interpolate between this band and next band
                float nextCenter = centers[bandIdx + 1];
                float nextEnergy = bandEnergies[bandIdx + 1];
                float t = (nextCenter > center)
                    ? (freqRatio - center) / (nextCenter - center)
                    : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                envelopeBuffer_[k] = energy + t * (nextEnergy - energy);
            }
            else
            {
                envelopeBuffer_[k] = energy;
            }
        }
    }

    void applyBrightnessTilt(float brightness) noexcept
    {
        for (size_t k = 0; k < numBins_; ++k)
        {
            float normalizedBin = static_cast<float>(k)
                / static_cast<float>(numBins_ - 1);
            float tilt = 1.0f + brightness * (2.0f * normalizedBin - 1.0f);
            tilt = std::max(tilt, 0.0f);
            envelopeBuffer_[k] *= tilt;
        }
    }

    // =========================================================================
    // Internal state
    // =========================================================================
    FFT fft_;
    OverlapAdd overlapAdd_;
    SpectralBuffer spectralBuffer_;
    Xorshift32 rng_{kPrngSeed};

    std::vector<float> noiseBuffer_;
    std::vector<float> envelopeBuffer_;
    std::vector<float> outputBuffer_;

    float sampleRate_ = 0.0f;
    size_t fftSize_ = 0;
    size_t hopSize_ = 0;
    size_t numBins_ = 0;
    size_t cursor_ = 0;
    bool prepared_ = false;
    bool frameLoaded_ = false;
};

} // namespace Krate::DSP
