// ==============================================================================
// Layer 1: DSP Primitive - Spectral Utilities
// ==============================================================================
// Common spectral processing utilities shared across spectral processors.
// Prevents duplication of bin mapping, interpolation, and phase handling logic.
//
// Used by:
// - spectral_morph_filter.h
// - spectral_gate.h
// - spectral_tilt.h
// - spectral_delay.h
// - Any future spectral processor
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in inline functions)
// - Principle III: Modern C++ (constexpr where possible, value semantics)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle XV: ODR Prevention (inline constexpr, header-only)
//
// Reference: specs/FLT-ROADMAP.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/math_constants.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// Bin-Frequency Conversion
// =============================================================================

/// Convert FFT bin index to frequency in Hz
/// @param bin Bin index (0 to fftSize/2)
/// @param fftSize FFT size (e.g., 2048)
/// @param sampleRate Sample rate in Hz
/// @return Frequency in Hz
[[nodiscard]] inline constexpr float binToFrequency(
    size_t bin,
    size_t fftSize,
    float sampleRate) noexcept
{
    return static_cast<float>(bin) * sampleRate / static_cast<float>(fftSize);
}

/// Convert frequency in Hz to FFT bin index (fractional)
/// @param frequency Frequency in Hz
/// @param fftSize FFT size
/// @param sampleRate Sample rate in Hz
/// @return Fractional bin index
[[nodiscard]] inline constexpr float frequencyToBin(
    float frequency,
    size_t fftSize,
    float sampleRate) noexcept
{
    return frequency * static_cast<float>(fftSize) / sampleRate;
}

/// Convert frequency in Hz to nearest integer FFT bin index
/// @param frequency Frequency in Hz
/// @param fftSize FFT size
/// @param sampleRate Sample rate in Hz
/// @return Nearest integer bin index, clamped to [0, fftSize/2]
[[nodiscard]] inline size_t frequencyToBinNearest(
    float frequency,
    size_t fftSize,
    float sampleRate) noexcept
{
    const float fractionalBin = frequencyToBin(frequency, fftSize, sampleRate);
    const size_t maxBin = fftSize / 2;
    if (fractionalBin < 0.0f) return 0;
    if (fractionalBin >= static_cast<float>(maxBin)) return maxBin;
    return static_cast<size_t>(fractionalBin + 0.5f);
}

/// Get bin spacing (frequency resolution) in Hz
/// @param fftSize FFT size
/// @param sampleRate Sample rate in Hz
/// @return Frequency spacing between bins in Hz
[[nodiscard]] inline constexpr float getBinSpacing(
    size_t fftSize,
    float sampleRate) noexcept
{
    return sampleRate / static_cast<float>(fftSize);
}

// =============================================================================
// Magnitude Interpolation
// =============================================================================

/// Linear interpolation between two values
/// @param a First value
/// @param b Second value
/// @param t Interpolation factor [0, 1]
/// @return Interpolated value
[[nodiscard]] inline constexpr float lerp(float a, float b, float t) noexcept {
    return a + t * (b - a);
}

/// Interpolate magnitude at fractional bin index using linear interpolation
/// @param magnitudes Array of magnitude values (size >= numBins)
/// @param numBins Number of valid bins
/// @param fractionalBin Fractional bin index
/// @return Interpolated magnitude
[[nodiscard]] inline float interpolateMagnitudeLinear(
    const float* magnitudes,
    size_t numBins,
    float fractionalBin) noexcept
{
    if (fractionalBin <= 0.0f) return magnitudes[0];
    if (fractionalBin >= static_cast<float>(numBins - 1)) {
        return magnitudes[numBins - 1];
    }

    const size_t bin0 = static_cast<size_t>(fractionalBin);
    const size_t bin1 = bin0 + 1;
    const float frac = fractionalBin - static_cast<float>(bin0);

    return lerp(magnitudes[bin0], magnitudes[bin1], frac);
}

/// Interpolate magnitude at fractional bin index using cubic interpolation
/// (Catmull-Rom spline for smoother results)
/// @param magnitudes Array of magnitude values (size >= numBins)
/// @param numBins Number of valid bins
/// @param fractionalBin Fractional bin index
/// @return Interpolated magnitude
[[nodiscard]] inline float interpolateMagnitudeCubic(
    const float* magnitudes,
    size_t numBins,
    float fractionalBin) noexcept
{
    if (fractionalBin <= 0.0f) return magnitudes[0];
    if (fractionalBin >= static_cast<float>(numBins - 1)) {
        return magnitudes[numBins - 1];
    }

    const size_t bin1 = static_cast<size_t>(fractionalBin);
    const size_t bin0 = (bin1 > 0) ? bin1 - 1 : 0;
    const size_t bin2 = std::min(bin1 + 1, numBins - 1);
    const size_t bin3 = std::min(bin1 + 2, numBins - 1);

    const float t = fractionalBin - static_cast<float>(bin1);
    const float t2 = t * t;
    const float t3 = t2 * t;

    const float m0 = magnitudes[bin0];
    const float m1 = magnitudes[bin1];
    const float m2 = magnitudes[bin2];
    const float m3 = magnitudes[bin3];

    // Catmull-Rom coefficients
    const float a = -0.5f * m0 + 1.5f * m1 - 1.5f * m2 + 0.5f * m3;
    const float b = m0 - 2.5f * m1 + 2.0f * m2 - 0.5f * m3;
    const float c = -0.5f * m0 + 0.5f * m2;
    const float d = m1;

    return a * t3 + b * t2 + c * t + d;
}

// =============================================================================
// Phase Utilities
// =============================================================================

/// Wrap phase to [-pi, pi] range
/// @param phase Phase in radians
/// @return Wrapped phase in [-pi, pi]
[[nodiscard]] inline float wrapPhase(float phase) noexcept {
    while (phase > kPi) phase -= kTwoPi;
    while (phase < -kPi) phase += kTwoPi;
    return phase;
}

/// Wrap phase to [-pi, pi] using faster fmod-based method
/// @param phase Phase in radians
/// @return Wrapped phase in [-pi, pi]
[[nodiscard]] inline float wrapPhaseFast(float phase) noexcept {
    phase = std::fmod(phase + kPi, kTwoPi);
    if (phase < 0.0f) phase += kTwoPi;
    return phase - kPi;
}

/// Calculate phase difference (unwrapped)
/// @param currentPhase Current phase in radians
/// @param previousPhase Previous phase in radians
/// @return Unwrapped phase difference
[[nodiscard]] inline float phaseDifference(
    float currentPhase,
    float previousPhase) noexcept
{
    return wrapPhase(currentPhase - previousPhase);
}

/// Calculate instantaneous frequency from phase difference
/// @param phaseDiff Phase difference (already unwrapped)
/// @param hopSize STFT hop size in samples
/// @param sampleRate Sample rate in Hz
/// @return Instantaneous frequency in Hz
[[nodiscard]] inline float phaseToFrequency(
    float phaseDiff,
    size_t hopSize,
    float sampleRate) noexcept
{
    return phaseDiff * sampleRate / (kTwoPi * static_cast<float>(hopSize));
}

/// Calculate expected phase increment for a bin
/// @param binIndex Bin index
/// @param hopSize STFT hop size in samples
/// @param fftSize FFT size
/// @return Expected phase increment in radians
[[nodiscard]] inline float expectedPhaseIncrement(
    size_t binIndex,
    size_t hopSize,
    size_t fftSize) noexcept
{
    return kTwoPi * static_cast<float>(binIndex) *
           static_cast<float>(hopSize) / static_cast<float>(fftSize);
}

// =============================================================================
// Spectral Smoothing
// =============================================================================

/// Apply simple 3-point moving average smoothing to magnitude spectrum
/// @param magnitudes Array of magnitudes (modified in place)
/// @param numBins Number of bins
/// @param scratch Scratch buffer of same size (for temp storage)
/// @note Real-time safe if scratch buffer is pre-allocated
inline void smoothMagnitudes3Point(
    float* magnitudes,
    size_t numBins,
    float* scratch) noexcept
{
    if (numBins < 3) return;

    // Copy to scratch
    for (size_t i = 0; i < numBins; ++i) {
        scratch[i] = magnitudes[i];
    }

    // Apply 3-point average (handle edges)
    magnitudes[0] = (scratch[0] + scratch[1]) * 0.5f;
    for (size_t i = 1; i < numBins - 1; ++i) {
        magnitudes[i] = (scratch[i - 1] + scratch[i] + scratch[i + 1]) / 3.0f;
    }
    magnitudes[numBins - 1] = (scratch[numBins - 2] + scratch[numBins - 1]) * 0.5f;
}

/// Apply exponential smoothing to magnitude spectrum (per-bin one-pole filter)
/// @param magnitudes Current magnitude array
/// @param previousMagnitudes Previous frame magnitudes (updated to current after call)
/// @param numBins Number of bins
/// @param coefficient Smoothing coefficient [0, 1], higher = more smoothing
inline void smoothMagnitudesExponential(
    float* magnitudes,
    float* previousMagnitudes,
    size_t numBins,
    float coefficient) noexcept
{
    const float oneMinusCoeff = 1.0f - coefficient;
    for (size_t i = 0; i < numBins; ++i) {
        magnitudes[i] = coefficient * previousMagnitudes[i] + oneMinusCoeff * magnitudes[i];
        previousMagnitudes[i] = magnitudes[i];
    }
}

// =============================================================================
// Spectral Analysis Helpers
// =============================================================================

/// Calculate spectral centroid (center of mass of spectrum)
/// @param magnitudes Array of magnitude values
/// @param numBins Number of bins
/// @param sampleRate Sample rate in Hz
/// @param fftSize FFT size
/// @return Spectral centroid frequency in Hz
[[nodiscard]] inline float calculateSpectralCentroid(
    const float* magnitudes,
    size_t numBins,
    float sampleRate,
    size_t fftSize) noexcept
{
    float weightedSum = 0.0f;
    float magnitudeSum = 0.0f;

    for (size_t i = 0; i < numBins; ++i) {
        const float freq = binToFrequency(i, fftSize, sampleRate);
        weightedSum += freq * magnitudes[i];
        magnitudeSum += magnitudes[i];
    }

    if (magnitudeSum < 1e-10f) return 0.0f;
    return weightedSum / magnitudeSum;
}

/// Calculate spectral flatness (ratio of geometric to arithmetic mean)
/// @param magnitudes Array of magnitude values
/// @param numBins Number of bins
/// @return Spectral flatness [0, 1], higher = more noise-like
[[nodiscard]] inline float calculateSpectralFlatness(
    const float* magnitudes,
    size_t numBins) noexcept
{
    if (numBins == 0) return 0.0f;

    float logSum = 0.0f;
    float arithmeticSum = 0.0f;
    size_t validBins = 0;

    for (size_t i = 0; i < numBins; ++i) {
        if (magnitudes[i] > 1e-10f) {
            logSum += std::log(magnitudes[i]);
            arithmeticSum += magnitudes[i];
            ++validBins;
        }
    }

    if (validBins == 0 || arithmeticSum < 1e-10f) return 0.0f;

    const float geometricMean = std::exp(logSum / static_cast<float>(validBins));
    const float arithmeticMean = arithmeticSum / static_cast<float>(validBins);

    return geometricMean / arithmeticMean;
}

} // namespace DSP
} // namespace Krate
