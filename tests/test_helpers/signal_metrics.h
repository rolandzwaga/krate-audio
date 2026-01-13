// ==============================================================================
// Test Helper: Signal Quality Metrics
// ==============================================================================
// SNR, THD, crest factor, kurtosis, ZCR, and spectral flatness calculations
// for quantitative verification of DSP signal quality.
//
// This is TEST INFRASTRUCTURE, not production DSP code.
//
// Location: tests/test_helpers/signal_metrics.h
// Namespace: Krate::DSP::TestUtils::SignalMetrics
//
// Reference: specs/055-artifact-detection/spec.md
// Requirements: FR-005, FR-006, FR-007, FR-008, FR-010, FR-011
// ==============================================================================

#pragma once

#include "statistical_utils.h"

#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/core/math_constants.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace Krate {
namespace DSP {
namespace TestUtils {

// =============================================================================
// SignalQualityMetrics (FR-005)
// =============================================================================

/// @brief Aggregate signal quality metrics
struct SignalQualityMetrics {
    float snrDb = 0.0f;             ///< Signal-to-Noise Ratio in dB
    float thdPercent = 0.0f;        ///< Total Harmonic Distortion in percent
    float thdDb = 0.0f;             ///< THD in dB
    float crestFactorDb = 0.0f;     ///< Crest factor (peak/RMS) in dB
    float kurtosis = 0.0f;          ///< Excess kurtosis

    /// @brief Check if all metrics are valid (no NaN or Inf)
    [[nodiscard]] bool isValid() const noexcept {
        return std::isfinite(snrDb) &&
               std::isfinite(thdPercent) &&
               std::isfinite(thdDb) &&
               std::isfinite(crestFactorDb) &&
               std::isfinite(kurtosis);
    }
};

// =============================================================================
// SignalMetrics Namespace
// =============================================================================

namespace SignalMetrics {

// -----------------------------------------------------------------------------
// SNR Calculation (FR-005)
// -----------------------------------------------------------------------------

/// @brief Calculate Signal-to-Noise Ratio
/// @param signal Processed signal
/// @param reference Original/reference signal
/// @param n Number of samples
/// @return SNR in dB
/// @note SNR = 10 * log10(signal_power / noise_power)
///       where noise = signal - reference
[[nodiscard]] inline float calculateSNR(
    const float* signal,
    const float* reference,
    size_t n
) noexcept {
    if (signal == nullptr || reference == nullptr || n == 0) {
        return 0.0f;
    }

    float signalPower = 0.0f;
    float noisePower = 0.0f;

    for (size_t i = 0; i < n; ++i) {
        signalPower += reference[i] * reference[i];
        const float noise = signal[i] - reference[i];
        noisePower += noise * noise;
    }

    // Avoid division by zero
    if (noisePower < 1e-20f) {
        return 200.0f;  // Return very high SNR for near-identical signals
    }

    return 10.0f * std::log10(signalPower / noisePower);
}

// -----------------------------------------------------------------------------
// THD Calculation (FR-006)
// -----------------------------------------------------------------------------

/// @brief Calculate Total Harmonic Distortion
/// @param signal Input signal
/// @param n Number of samples
/// @param fundamentalHz Fundamental frequency in Hz
/// @param sampleRate Sample rate in Hz
/// @param maxHarmonic Maximum harmonic to consider (default: 10)
/// @return THD in percent (0-100)
/// @note THD = sqrt(H2^2 + H3^2 + ... + Hn^2) / H1 * 100
[[nodiscard]] inline float calculateTHD(
    const float* signal,
    size_t n,
    float fundamentalHz,
    float sampleRate,
    int maxHarmonic = 10
) noexcept {
    if (signal == nullptr || n < 256) {
        return 0.0f;
    }

    // Find suitable FFT size (power of 2, at least n)
    size_t fftSize = 256;
    while (fftSize < n && fftSize < 8192) {
        fftSize *= 2;
    }
    if (fftSize > n) {
        fftSize = n;  // Use actual size if smaller
        // Ensure power of 2
        size_t p2 = 256;
        while (p2 * 2 <= fftSize) {
            p2 *= 2;
        }
        fftSize = p2;
    }

    // Prepare FFT
    FFT fft;
    fft.prepare(fftSize);

    // Apply Hann window to avoid spectral leakage
    std::vector<float> windowed(fftSize);
    std::vector<float> window(fftSize);
    Window::generateHann(window.data(), fftSize);

    for (size_t i = 0; i < fftSize; ++i) {
        windowed[i] = (i < n) ? signal[i] * window[i] : 0.0f;
    }

    // Perform FFT
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(windowed.data(), spectrum.data());

    // Calculate bin resolution
    const float binResolution = sampleRate / static_cast<float>(fftSize);

    // Find fundamental bin and magnitude
    const size_t fundamentalBin = static_cast<size_t>(fundamentalHz / binResolution + 0.5f);
    if (fundamentalBin >= spectrum.size()) {
        return 0.0f;
    }

    // Peak-picking around fundamental bin for better accuracy
    float fundamentalMag = 0.0f;
    for (int offset = -2; offset <= 2; ++offset) {
        const size_t bin = fundamentalBin + offset;
        if (bin < spectrum.size()) {
            const float mag = spectrum[bin].magnitude();
            if (mag > fundamentalMag) {
                fundamentalMag = mag;
            }
        }
    }

    if (fundamentalMag < 1e-10f) {
        return 0.0f;  // No fundamental found
    }

    // Sum harmonic power
    float harmonicPowerSum = 0.0f;
    for (int h = 2; h <= maxHarmonic; ++h) {
        const float harmonicFreq = fundamentalHz * static_cast<float>(h);
        const float nyquist = sampleRate / 2.0f;

        if (harmonicFreq >= nyquist) {
            break;  // Above Nyquist
        }

        const size_t harmonicBin = static_cast<size_t>(harmonicFreq / binResolution + 0.5f);
        if (harmonicBin >= spectrum.size()) {
            break;
        }

        // Peak-picking around harmonic bin
        float harmonicMag = 0.0f;
        for (int offset = -2; offset <= 2; ++offset) {
            const size_t bin = harmonicBin + offset;
            if (bin < spectrum.size()) {
                const float mag = spectrum[bin].magnitude();
                if (mag > harmonicMag) {
                    harmonicMag = mag;
                }
            }
        }

        harmonicPowerSum += harmonicMag * harmonicMag;
    }

    // THD = sqrt(sum of harmonic powers) / fundamental
    const float thd = std::sqrt(harmonicPowerSum) / fundamentalMag;
    return thd * 100.0f;  // Convert to percent
}

// -----------------------------------------------------------------------------
// Crest Factor Calculation (FR-007)
// -----------------------------------------------------------------------------

/// @brief Calculate crest factor (peak-to-RMS ratio) in dB
/// @param signal Input signal
/// @param n Number of samples
/// @return Crest factor in dB
[[nodiscard]] inline float calculateCrestFactorDb(
    const float* signal,
    size_t n
) noexcept {
    if (signal == nullptr || n == 0) {
        return 0.0f;
    }

    // Find peak
    float peak = 0.0f;
    float rmsSquared = 0.0f;

    for (size_t i = 0; i < n; ++i) {
        const float absVal = std::abs(signal[i]);
        if (absVal > peak) {
            peak = absVal;
        }
        rmsSquared += signal[i] * signal[i];
    }

    rmsSquared /= static_cast<float>(n);
    const float rms = std::sqrt(rmsSquared);

    if (rms < 1e-10f) {
        return 0.0f;
    }

    // Crest factor in dB = 20 * log10(peak / RMS)
    return 20.0f * std::log10(peak / rms);
}

// -----------------------------------------------------------------------------
// Kurtosis Calculation (FR-008)
// -----------------------------------------------------------------------------

/// @brief Calculate excess kurtosis
/// @param signal Input signal
/// @param n Number of samples
/// @return Excess kurtosis (0 for normal distribution)
/// @note Kurtosis = E[(X-mu)^4] / sigma^4 - 3
///       Positive kurtosis = heavy tails (impulsive)
///       Negative kurtosis = light tails (uniform-like)
[[nodiscard]] inline float calculateKurtosis(
    const float* signal,
    size_t n
) noexcept {
    if (signal == nullptr || n < 4) {
        return 0.0f;
    }

    // Compute mean
    const float mean = StatisticalUtils::computeMean(signal, n);

    // Compute second and fourth central moments
    const float m2 = StatisticalUtils::computeMoment(signal, n, mean, 2);
    const float m4 = StatisticalUtils::computeMoment(signal, n, mean, 4);

    if (m2 < 1e-10f) {
        return 0.0f;
    }

    // Excess kurtosis = m4 / m2^2 - 3
    return m4 / (m2 * m2) - 3.0f;
}

// -----------------------------------------------------------------------------
// Zero-Crossing Rate (FR-011)
// -----------------------------------------------------------------------------

/// @brief Calculate zero-crossing rate
/// @param signal Input signal
/// @param n Number of samples
/// @return ZCR (crossings per sample)
[[nodiscard]] inline float calculateZCR(
    const float* signal,
    size_t n
) noexcept {
    if (signal == nullptr || n < 2) {
        return 0.0f;
    }

    size_t crossings = 0;

    for (size_t i = 1; i < n; ++i) {
        // Check if sign changed (crossed zero)
        if ((signal[i] >= 0.0f && signal[i - 1] < 0.0f) ||
            (signal[i] < 0.0f && signal[i - 1] >= 0.0f)) {
            ++crossings;
        }
    }

    return static_cast<float>(crossings) / static_cast<float>(n);
}

// -----------------------------------------------------------------------------
// Spectral Flatness (FR-010)
// -----------------------------------------------------------------------------

/// @brief Calculate spectral flatness (Wiener entropy)
/// @param signal Input signal
/// @param n Number of samples
/// @param sampleRate Sample rate in Hz
/// @return Spectral flatness (0 = tonal, 1 = noise-like)
/// @note Flatness = geometric_mean(spectrum) / arithmetic_mean(spectrum)
[[nodiscard]] inline float calculateSpectralFlatness(
    const float* signal,
    size_t n,
    [[maybe_unused]] float sampleRate
) noexcept {
    if (signal == nullptr || n < 64) {
        return 0.0f;
    }

    // Find suitable FFT size (power of 2)
    size_t fftSize = 64;
    while (fftSize * 2 <= n && fftSize < 4096) {
        fftSize *= 2;
    }

    // Prepare FFT
    FFT fft;
    fft.prepare(fftSize);

    // Apply Hann window
    std::vector<float> windowed(fftSize);
    std::vector<float> window(fftSize);
    Window::generateHann(window.data(), fftSize);

    for (size_t i = 0; i < fftSize; ++i) {
        windowed[i] = (i < n) ? signal[i] * window[i] : 0.0f;
    }

    // Perform FFT
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(windowed.data(), spectrum.data());

    // Compute magnitude spectrum (skip DC bin)
    const size_t numBins = spectrum.size() - 1;  // Exclude DC
    if (numBins == 0) {
        return 0.0f;
    }

    std::vector<float> magnitudes(numBins);
    for (size_t i = 0; i < numBins; ++i) {
        magnitudes[i] = spectrum[i + 1].magnitude();  // Skip DC
    }

    // Compute arithmetic mean
    float arithMean = 0.0f;
    for (float m : magnitudes) {
        arithMean += m;
    }
    arithMean /= static_cast<float>(numBins);

    if (arithMean < 1e-10f) {
        return 0.0f;
    }

    // Compute geometric mean using log sum
    float logSum = 0.0f;
    size_t validBins = 0;
    for (float m : magnitudes) {
        if (m > 1e-10f) {
            logSum += std::log(m);
            ++validBins;
        }
    }

    if (validBins == 0) {
        return 0.0f;
    }

    const float geomMean = std::exp(logSum / static_cast<float>(validBins));

    // Spectral flatness = geometric mean / arithmetic mean
    return geomMean / arithMean;
}

// -----------------------------------------------------------------------------
// Aggregate Quality Measurement
// -----------------------------------------------------------------------------

/// @brief Measure comprehensive signal quality metrics
/// @param signal Processed signal
/// @param reference Reference signal (for SNR)
/// @param n Number of samples
/// @param fundamentalHz Fundamental frequency for THD calculation
/// @param sampleRate Sample rate in Hz
/// @return SignalQualityMetrics structure
[[nodiscard]] inline SignalQualityMetrics measureQuality(
    const float* signal,
    const float* reference,
    size_t n,
    float fundamentalHz,
    float sampleRate
) noexcept {
    SignalQualityMetrics metrics;

    // Calculate SNR
    metrics.snrDb = calculateSNR(signal, reference, n);

    // Calculate THD
    metrics.thdPercent = calculateTHD(signal, n, fundamentalHz, sampleRate);
    metrics.thdDb = (metrics.thdPercent > 0.0f)
        ? 20.0f * std::log10(metrics.thdPercent / 100.0f)
        : -200.0f;

    // Calculate crest factor
    metrics.crestFactorDb = calculateCrestFactorDb(signal, n);

    // Calculate kurtosis
    metrics.kurtosis = calculateKurtosis(signal, n);

    return metrics;
}

} // namespace SignalMetrics

} // namespace TestUtils
} // namespace DSP
} // namespace Krate
