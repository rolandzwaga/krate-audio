// ==============================================================================
// API Contract: Spectral Analysis Test Utilities
// ==============================================================================
// FFT-based aliasing measurement for quantitative verification of anti-aliasing
// success criteria (SC-001/SC-002 for ADAA specs).
//
// This is TEST INFRASTRUCTURE, not production DSP code.
//
// Location: tests/test_helpers/spectral_analysis.h
// Namespace: Krate::DSP::TestUtils
//
// Reference: specs/054-spectral-test-utils/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/core/math_constants.h>

#include <cmath>
#include <cstddef>
#include <vector>

namespace Krate {
namespace DSP {
namespace TestUtils {

// =============================================================================
// Data Structures
// =============================================================================

/// @brief Result of aliasing measurement
///
/// Contains power measurements in dB for fundamental, harmonics, and aliased
/// components. Higher signalToAliasingDb indicates better anti-aliasing.
///
/// @par Example Usage
/// @code
/// auto result = measureAliasing(config, [](float x) { return processor(x); });
/// INFO("Aliasing: " << result.aliasingPowerDb << " dB");
/// INFO("Signal-to-aliasing ratio: " << result.signalToAliasingDb << " dB");
/// @endcode
struct AliasingMeasurement {
    float fundamentalPowerDb;    ///< Power at fundamental frequency (dB)
    float harmonicPowerDb;       ///< Total power in intended harmonics below Nyquist (dB)
    float aliasingPowerDb;       ///< Total power in aliased components (dB)
    float signalToAliasingDb;    ///< Fundamental minus aliasing (dB), higher = better

    /// @brief Compare aliasing to a reference measurement
    /// @param reference The reference measurement (typically naive/unprocessed)
    /// @return Aliasing reduction in dB (positive = improvement)
    [[nodiscard]] float aliasingReductionVs(const AliasingMeasurement& reference) const noexcept {
        return reference.aliasingPowerDb - aliasingPowerDb;
    }

    /// @brief Check if measurement is valid (no NaN values)
    [[nodiscard]] bool isValid() const noexcept {
        return !std::isnan(fundamentalPowerDb) &&
               !std::isnan(harmonicPowerDb) &&
               !std::isnan(aliasingPowerDb) &&
               !std::isnan(signalToAliasingDb);
    }
};

/// @brief Configuration for aliasing measurement
///
/// @par Default Configuration (5kHz at 44.1kHz)
/// - Harmonics 2-4 (10-20kHz) are below Nyquist (intended)
/// - Harmonics 5+ alias back into spectrum (aliased)
///
/// @par Aliasing Example
/// | Harmonic | Frequency | Aliased To |
/// |----------|-----------|------------|
/// | 5 | 25,000 Hz | 19,100 Hz |
/// | 6 | 30,000 Hz | 14,100 Hz |
/// | 7 | 35,000 Hz | 9,100 Hz |
/// | 8 | 40,000 Hz | 4,100 Hz |
/// | 9 | 45,000 Hz | 900 Hz |
struct AliasingTestConfig {
    float testFrequencyHz = 5000.0f;   ///< Fundamental frequency (Hz)
    float sampleRate = 44100.0f;       ///< Sample rate (Hz)
    float driveGain = 4.0f;            ///< Pre-gain to induce clipping
    size_t fftSize = 2048;             ///< FFT size (must be power of 2, 256-8192)
    int maxHarmonic = 10;              ///< Highest harmonic to consider

    /// @brief Validate configuration
    [[nodiscard]] bool isValid() const noexcept {
        return testFrequencyHz > 0.0f &&
               sampleRate > 0.0f &&
               testFrequencyHz < sampleRate / 2.0f &&
               driveGain > 0.0f &&
               fftSize >= 256 &&
               fftSize <= 8192 &&
               (fftSize & (fftSize - 1)) == 0 &&  // Power of 2
               maxHarmonic >= 2;
    }

    /// @brief Get Nyquist frequency
    [[nodiscard]] float nyquist() const noexcept {
        return sampleRate / 2.0f;
    }

    /// @brief Get frequency resolution (Hz per bin)
    [[nodiscard]] float binResolution() const noexcept {
        return sampleRate / static_cast<float>(fftSize);
    }
};

// =============================================================================
// Helper Functions
// =============================================================================

/// @brief Convert frequency to FFT bin index
/// @param freqHz Frequency in Hz
/// @param sampleRate Sample rate in Hz
/// @param fftSize FFT size
/// @return Nearest bin index
[[nodiscard]] inline size_t frequencyToBin(
    float freqHz,
    float sampleRate,
    size_t fftSize
) noexcept {
    const float binFloat = freqHz * static_cast<float>(fftSize) / sampleRate;
    return static_cast<size_t>(std::round(binFloat));
}

/// @brief Calculate the aliased frequency for a harmonic
///
/// When a harmonic frequency exceeds Nyquist (fs/2), it "folds back" into the
/// representable range. This function computes where it lands.
///
/// @param fundamentalHz Fundamental frequency in Hz
/// @param harmonicNumber Harmonic number (2 = second harmonic, etc.)
/// @param sampleRate Sample rate in Hz
/// @return Aliased frequency in Hz (same as input if no aliasing)
[[nodiscard]] inline float calculateAliasedFrequency(
    float fundamentalHz,
    int harmonicNumber,
    float sampleRate
) noexcept {
    const float harmonicFreq = fundamentalHz * static_cast<float>(harmonicNumber);
    const float nyquist = sampleRate / 2.0f;

    // No aliasing if below Nyquist
    if (harmonicFreq <= nyquist) {
        return harmonicFreq;
    }

    // Fold back around Nyquist
    float aliased = std::fmod(harmonicFreq, sampleRate);
    if (aliased > nyquist) {
        aliased = sampleRate - aliased;
    }
    return aliased;
}

/// @brief Check if a harmonic will alias at given sample rate
/// @param fundamentalHz Fundamental frequency in Hz
/// @param harmonicNumber Harmonic number
/// @param sampleRate Sample rate in Hz
/// @return true if harmonic exceeds Nyquist
[[nodiscard]] inline bool willAlias(
    float fundamentalHz,
    int harmonicNumber,
    float sampleRate
) noexcept {
    return (fundamentalHz * static_cast<float>(harmonicNumber)) > (sampleRate / 2.0f);
}

/// @brief Get bin indices for intended harmonics (below Nyquist)
/// @param config Test configuration
/// @return Vector of bin indices for harmonics 2..maxHarmonic that don't alias
[[nodiscard]] inline std::vector<size_t> getHarmonicBins(
    const AliasingTestConfig& config
) {
    std::vector<size_t> bins;
    const float nyquist = config.nyquist();

    for (int n = 2; n <= config.maxHarmonic; ++n) {
        const float freq = config.testFrequencyHz * static_cast<float>(n);
        if (freq < nyquist) {
            bins.push_back(frequencyToBin(freq, config.sampleRate, config.fftSize));
        }
    }
    return bins;
}

/// @brief Get bin indices for aliased components
/// @param config Test configuration
/// @return Vector of bin indices where aliased harmonics appear
[[nodiscard]] inline std::vector<size_t> getAliasedBins(
    const AliasingTestConfig& config
) {
    std::vector<size_t> bins;
    const float nyquist = config.nyquist();

    for (int n = 2; n <= config.maxHarmonic; ++n) {
        const float freq = config.testFrequencyHz * static_cast<float>(n);
        if (freq >= nyquist) {
            const float aliasedFreq = calculateAliasedFrequency(
                config.testFrequencyHz, n, config.sampleRate);
            bins.push_back(frequencyToBin(aliasedFreq, config.sampleRate, config.fftSize));
        }
    }
    return bins;
}

// =============================================================================
// Internal Helpers
// =============================================================================

namespace detail {

/// @brief Convert linear amplitude to dB
[[nodiscard]] inline float toDb(float amplitude) noexcept {
    constexpr float kEpsilon = 1e-10f;
    constexpr float kFloorDb = -200.0f;
    if (amplitude < kEpsilon) {
        return kFloorDb;
    }
    return 20.0f * std::log10(amplitude);
}

/// @brief Sum power from specified bins
[[nodiscard]] inline float sumBinPower(
    const Complex* spectrum,
    const std::vector<size_t>& bins
) noexcept {
    float totalPower = 0.0f;
    for (size_t bin : bins) {
        const float mag = spectrum[bin].magnitude();
        totalPower += mag * mag;
    }
    return std::sqrt(totalPower);
}

} // namespace detail

// =============================================================================
// Main Functions
// =============================================================================

/// @brief Measure aliasing in a waveshaper's output
///
/// Generates a test signal, processes it through the provided waveshaper,
/// and measures the power distribution across fundamental, harmonics, and
/// aliased components using FFT analysis.
///
/// @tparam Processor Callable with signature: float(float)
/// @param config Test configuration
/// @param processor The waveshaper to test (lambda, function, functor)
/// @return Aliasing measurement results
///
/// @par Example
/// @code
/// AliasingTestConfig config{
///     .testFrequencyHz = 5000.0f,
///     .sampleRate = 44100.0f,
///     .driveGain = 4.0f,
///     .fftSize = 2048
/// };
///
/// auto result = measureAliasing(config, [](float x) {
///     return Sigmoid::hardClip(x);
/// });
/// @endcode
template<typename Processor>
[[nodiscard]] AliasingMeasurement measureAliasing(
    const AliasingTestConfig& config,
    Processor&& processor
) {
    // 1. Generate test signal (sine wave with drive)
    const size_t numSamples = config.fftSize;
    std::vector<float> input(numSamples);
    std::vector<float> output(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        const float phase = kTwoPi * config.testFrequencyHz *
                           static_cast<float>(i) / config.sampleRate;
        input[i] = config.driveGain * std::sin(phase);
    }

    // 2. Process through waveshaper
    for (size_t i = 0; i < numSamples; ++i) {
        output[i] = processor(input[i]);
    }

    // 3. Apply Hann window to reduce spectral leakage
    std::vector<float> window(numSamples);
    Window::generateHann(window.data(), numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        output[i] *= window[i];
    }

    // 4. Perform FFT
    FFT fft;
    fft.prepare(numSamples);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(output.data(), spectrum.data());

    // 5. Get bin indices
    const size_t fundamentalBin = frequencyToBin(
        config.testFrequencyHz, config.sampleRate, config.fftSize);
    const auto harmonicBins = getHarmonicBins(config);
    const auto aliasedBins = getAliasedBins(config);

    // 6. Measure power in each category
    const float fundamentalPower = spectrum[fundamentalBin].magnitude();
    const float harmonicPower = detail::sumBinPower(spectrum.data(), harmonicBins);
    const float aliasingPower = detail::sumBinPower(spectrum.data(), aliasedBins);

    // 7. Convert to dB
    return AliasingMeasurement{
        .fundamentalPowerDb = detail::toDb(fundamentalPower),
        .harmonicPowerDb = detail::toDb(harmonicPower),
        .aliasingPowerDb = detail::toDb(aliasingPower),
        .signalToAliasingDb = detail::toDb(fundamentalPower) - detail::toDb(aliasingPower)
    };
}

/// @brief Compare aliasing between two processors
///
/// Convenience function that measures aliasing for both processors and
/// computes the reduction in dB.
///
/// @tparam ProcessorA First processor type
/// @tparam ProcessorB Second processor type (reference)
/// @param config Test configuration
/// @param test The processor being tested
/// @param reference The reference processor (typically naive)
/// @return Aliasing reduction in dB (positive = test has less aliasing)
///
/// @par Example
/// @code
/// float reduction = compareAliasing(config,
///     [&](float x) { return adaa.process(x); },
///     [](float x) { return Sigmoid::hardClip(x); });
///
/// REQUIRE(reduction >= 12.0f);  // SC-001
/// @endcode
template<typename ProcessorA, typename ProcessorB>
[[nodiscard]] float compareAliasing(
    const AliasingTestConfig& config,
    ProcessorA&& test,
    ProcessorB&& reference
) {
    const auto testResult = measureAliasing(config, std::forward<ProcessorA>(test));
    const auto refResult = measureAliasing(config, std::forward<ProcessorB>(reference));
    return testResult.aliasingReductionVs(refResult);
}

} // namespace TestUtils
} // namespace DSP
} // namespace Krate
