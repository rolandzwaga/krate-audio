# Data Model: Spectral Test Utilities

**Date**: 2026-01-13
**Feature**: 054-spectral-test-utils

## Overview

This document defines the data structures and function signatures for the spectral analysis test utilities.

## Namespace

```cpp
namespace Krate::DSP::TestUtils {
    // All definitions below
}
```

## Data Structures

### AliasingMeasurement

Result of aliasing measurement, containing power levels in dB for each category.

```cpp
/// @brief Result of aliasing measurement
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
```

### AliasingTestConfig

Configuration parameters for aliasing measurement.

```cpp
/// @brief Configuration for aliasing measurement
struct AliasingTestConfig {
    float testFrequencyHz = 5000.0f;   ///< Fundamental frequency (Hz)
    float sampleRate = 44100.0f;       ///< Sample rate (Hz)
    float driveGain = 4.0f;            ///< Pre-gain to induce clipping
    size_t fftSize = 2048;             ///< FFT size (must be power of 2)
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
```

## Helper Functions

### Frequency to Bin Conversion

```cpp
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
```

### Aliased Frequency Calculation

```cpp
/// @brief Calculate the aliased frequency for a harmonic
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
```

### Aliasing Detection

```cpp
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
```

### Bin Collections

```cpp
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
```

## Main Functions

### measureAliasing

```cpp
/// @brief Measure aliasing in a waveshaper's output
/// @tparam Processor Callable with signature: float(float)
/// @param config Test configuration
/// @param processor The waveshaper to test (lambda, function, functor)
/// @return Aliasing measurement results
template<typename Processor>
[[nodiscard]] AliasingMeasurement measureAliasing(
    const AliasingTestConfig& config,
    Processor&& processor
);
```

### compareAliasing

```cpp
/// @brief Compare aliasing between two processors
/// @tparam ProcessorA First processor type
/// @tparam ProcessorB Second processor type (reference)
/// @param config Test configuration
/// @param test The processor being tested
/// @param reference The reference processor (typically naive)
/// @return Aliasing reduction in dB (positive = test has less aliasing)
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
```

## Internal Helpers

### Power Summation

```cpp
/// @brief Sum power from specified bins (internal helper)
/// @param spectrum FFT output buffer
/// @param bins Bin indices to sum
/// @return RMS of the summed power
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

/// @brief Convert linear amplitude to dB
/// @param amplitude Linear amplitude value
/// @return Value in dB (with -200 dB floor)
[[nodiscard]] inline float toDb(float amplitude) noexcept {
    constexpr float kEpsilon = 1e-10f;
    constexpr float kFloorDb = -200.0f;
    if (amplitude < kEpsilon) {
        return kFloorDb;
    }
    return 20.0f * std::log10(amplitude);
}
```

## Dependencies

The implementation requires these headers:

```cpp
// KrateDSP headers
#include <krate/dsp/primitives/fft.h>           // FFT, Complex
#include <krate/dsp/core/window_functions.h>    // Window::generateHann
#include <krate/dsp/core/math_constants.h>      // kTwoPi

// Standard library
#include <cmath>      // std::sin, std::sqrt, std::log10, std::fmod, std::round
#include <vector>     // std::vector
#include <cstddef>    // size_t
```

## Relationships

```
AliasingTestConfig
    |
    +---> frequencyToBin()
    +---> calculateAliasedFrequency()
    +---> willAlias()
    +---> getHarmonicBins()
    +---> getAliasedBins()
    |
    v
measureAliasing<Processor>()
    |
    +---> FFT::prepare()
    +---> FFT::forward()
    +---> Window::generateHann()
    +---> sumBinPower()
    +---> toDb()
    |
    v
AliasingMeasurement
    |
    +---> aliasingReductionVs()
    |
    v
compareAliasing()
```
