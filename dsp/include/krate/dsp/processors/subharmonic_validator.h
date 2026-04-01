// ==============================================================================
// Layer 2: DSP Processor - Subharmonic Validator
// ==============================================================================
// Validates and corrects YIN F0 estimates using subharmonic summation
// (Hermes 1988).  YIN is prone to octave errors — it sometimes locks onto
// a subharmonic (f0/2, f0/3) instead of the true fundamental.
//
// The validator sums the spectral magnitudes at each harmonic of a candidate
// F0 (weighted by 1/h).  It compares the score for the original F0 against
// octave-up (f0*2) and octave-down (f0/2) candidates and corrects if a
// different octave scores significantly higher.
//
// This uses data already available from the STFT — no additional FFTs needed.
//
// Reference: Hermes, D.J. (1988). "Measurement of pitch by subharmonic
// summation." JASA 83(1), pp. 257-264.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 2 (depends on Layer 1 spectral_buffer)
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/processors/harmonic_types.h>

#include <algorithm>
#include <cmath>

namespace Krate::DSP {

/// @brief Validates and corrects YIN F0 estimates using subharmonic summation.
///
/// Usage:
/// @code
///   SubharmonicValidator validator;
///   validator.prepare(fftSize, sampleRate);
///   auto corrected = validator.validate(yinEstimate, spectrum);
/// @endcode
class SubharmonicValidator {
public:
    /// Maximum number of harmonics to sum (higher = more accurate but slower)
    static constexpr int kMaxHarmonics = 12;

    /// Minimum ratio by which an octave correction must exceed the original
    /// score to trigger correction.  Values above 1.0 prevent spurious jumps.
    static constexpr float kCorrectionThreshold = 1.4f;

    SubharmonicValidator() noexcept = default;

    /// @brief Prepare the validator for a given FFT size and sample rate.
    void prepare(size_t fftSize, double sampleRate) noexcept {
        fftSize_ = fftSize;
        sampleRate_ = static_cast<float>(sampleRate);
        nyquist_ = sampleRate_ * 0.5f;
        binSpacing_ = sampleRate_ / static_cast<float>(fftSize);
    }

    /// @brief Validate and potentially correct an F0 estimate.
    ///
    /// Compares the harmonic support for f0, f0*2, and f0/2 in the spectrum.
    /// Returns a corrected F0Estimate if an octave correction scores
    /// significantly better than the original.
    ///
    /// @param estimate The YIN F0 estimate to validate
    /// @param spectrum The STFT magnitude spectrum for this frame
    /// @return Corrected F0Estimate (may be unchanged if original was correct)
    /// @note Real-time safe
    [[nodiscard]] F0Estimate validate(
        const F0Estimate& estimate,
        const SpectralBuffer& spectrum) const noexcept
    {
        if (!estimate.voiced || estimate.frequency <= 0.0f ||
            fftSize_ == 0 || binSpacing_ <= 0.0f)
        {
            return estimate;
        }

        const float f0 = estimate.frequency;

        // Score the original F0 and octave alternatives
        float scoreOriginal = harmonicScore(f0, spectrum);
        float scoreOctaveUp = (f0 * 2.0f < nyquist_)
            ? harmonicScore(f0 * 2.0f, spectrum) : 0.0f;
        float scoreOctaveDown = (f0 * 0.5f >= 30.0f)
            ? harmonicScore(f0 * 0.5f, spectrum) : 0.0f;

        // Also check double-octave up (f0*4) for severe subharmonic errors
        // where YIN found f0/4 instead of f0
        float scoreDblOctaveUp = (f0 * 4.0f < nyquist_)
            ? harmonicScore(f0 * 4.0f, spectrum) : 0.0f;

        // Find the best scoring candidate
        float bestScore = scoreOriginal;
        float bestF0 = f0;

        if (scoreOctaveUp > bestScore * kCorrectionThreshold) {
            bestScore = scoreOctaveUp;
            bestF0 = f0 * 2.0f;
        }
        if (scoreOctaveDown > bestScore * kCorrectionThreshold) {
            bestScore = scoreOctaveDown;
            bestF0 = f0 * 0.5f;
        }
        if (scoreDblOctaveUp > bestScore * kCorrectionThreshold) {
            bestScore = scoreDblOctaveUp;
            bestF0 = f0 * 4.0f;
        }

        if (bestF0 == f0) {
            return estimate; // No correction needed
        }

        // Return corrected estimate
        F0Estimate corrected = estimate;
        corrected.frequency = bestF0;
        // Confidence stays the same — the pitch is more correct,
        // not less confident
        return corrected;
    }

    /// Compute harmonic support score for a candidate F0.
    /// Public so callers can compare scores between different F0 candidates.
    /// Sums weighted spectral magnitudes at f0, 2*f0, 3*f0, ...
    /// Weight = 1/h (fundamental counts most, upper harmonics less).
    [[nodiscard]] float harmonicScore(
        float f0, const SpectralBuffer& spectrum) const noexcept
    {
        float score = 0.0f;
        const size_t numBins = spectrum.numBins();

        for (int h = 1; h <= kMaxHarmonics; ++h) {
            float harmonicFreq = f0 * static_cast<float>(h);
            if (harmonicFreq >= nyquist_) break;

            // Find the closest bin
            float binF = harmonicFreq / binSpacing_;
            auto bin = static_cast<size_t>(std::round(binF));
            if (bin >= numBins) break;

            // Use parabolic interpolation across 3 bins for a better
            // magnitude estimate (same as the partial tracker does)
            float mag = spectrum.getMagnitude(bin);
            if (bin > 0 && bin < numBins - 1) {
                float magL = spectrum.getMagnitude(bin - 1);
                float magR = spectrum.getMagnitude(bin + 1);
                // Take the max of center and interpolated peak
                float interpMag = mag;
                float denom = magL - 2.0f * mag + magR;
                if (denom != 0.0f) {
                    float delta = 0.5f * (magL - magR) / denom;
                    interpMag = mag - 0.25f * (magL - magR) * delta;
                }
                mag = std::max(mag, interpMag);
            }

            // Weight by 1/h — fundamental is most important
            float weight = 1.0f / static_cast<float>(h);
            score += mag * weight;
        }

        return score;
    }

    size_t fftSize_ = 0;
    float sampleRate_ = 44100.0f;
    float nyquist_ = 22050.0f;
    float binSpacing_ = 43.07f;
};

} // namespace Krate::DSP
