// ==============================================================================
// Layer 1: DSP Primitive - Wavetable Generator
// ==============================================================================
// Mipmapped wavetable generation via FFT/IFFT for standard waveforms,
// custom harmonic spectra, and raw waveform samples. Populates WavetableData
// with band-limited mipmap levels, each independently normalized with
// correct guard samples for branchless cubic Hermite interpolation.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (NOT real-time safe -- init-time only)
// - Principle III: Modern C++ (C++20, value semantics)
// - Principle IX: Layer 1 (depends on Layer 0: wavetable_data.h, math_constants.h;
//                  Layer 1: fft.h)
// - Principle XII: Test-First Development
//
// Reference: specs/016-wavetable-oscillator/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/wavetable_data.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/primitives/fft.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Krate {
namespace DSP {

// =============================================================================
// Internal Helpers (not part of public API)
// =============================================================================

namespace detail {

/// @brief Set guard samples for a single mipmap level.
/// Assumes levelData points to logical index 0 (getMutableLevel pointer).
/// @param levelData Pointer to logical index 0 of the level
/// @param tableSize Number of data samples (excluding guards)
inline void setGuardSamples(float* levelData, size_t tableSize) noexcept {
    // p[-1] = data[N-1] (prepend guard: wrap from end)
    levelData[-1] = levelData[tableSize - 1];
    // p[N] = data[0] (first append guard: wrap from start)
    levelData[tableSize] = levelData[0];
    // p[N+1] = data[1]
    levelData[tableSize + 1] = levelData[1];
    // p[N+2] = data[2]
    levelData[tableSize + 2] = levelData[2];
}

/// @brief Normalize samples so peak amplitude equals targetPeak.
/// @param data Pointer to sample data
/// @param count Number of samples
/// @param targetPeak Target peak amplitude (default 0.96)
inline void normalizeToPeak(float* data, size_t count, float targetPeak = 0.96f) noexcept {
    float peak = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        float absVal = std::abs(data[i]);
        if (absVal > peak) peak = absVal;
    }
    if (peak > 0.0f) {
        float scale = targetPeak / peak;
        for (size_t i = 0; i < count; ++i) {
            data[i] *= scale;
        }
    }
}

/// @brief Compute max harmonic number for a given mipmap level.
/// Level 0: tableSize/2, Level 1: tableSize/4, ..., Level 10: 1
inline size_t maxHarmonicForLevel(size_t level, size_t tableSize) noexcept {
    size_t maxH = tableSize / (static_cast<size_t>(1) << (level + 1));
    return (maxH < 1) ? 1 : maxH;
}

/// @brief Generate all mipmap levels from a spectrum-filling function.
/// The spectrumFiller is called for each level with the spectrum buffer and max harmonic.
/// @tparam SpectrumFiller Callable with signature void(Complex*, size_t maxHarmonic, size_t numBins)
template<typename SpectrumFiller>
inline void generateLevels(WavetableData& data, SpectrumFiller filler) {
    FFT fft;
    fft.prepare(kDefaultTableSize);
    const size_t numBins = fft.numBins();

    std::vector<Complex> spectrum(numBins);
    std::vector<float> buffer(kDefaultTableSize);

    for (size_t level = 0; level < kMaxMipmapLevels; ++level) {
        size_t maxH = maxHarmonicForLevel(level, kDefaultTableSize);

        // Clear spectrum
        std::fill(spectrum.begin(), spectrum.end(), Complex{0.0f, 0.0f});

        // Fill spectrum with harmonics
        filler(spectrum.data(), maxH, numBins);

        // IFFT to time domain
        fft.inverse(spectrum.data(), buffer.data());

        // Normalize to ~0.96 peak
        normalizeToPeak(buffer.data(), kDefaultTableSize);

        // Copy to WavetableData level
        float* levelData = data.getMutableLevel(level);
        for (size_t i = 0; i < kDefaultTableSize; ++i) {
            levelData[i] = buffer[i];
        }

        // Set guard samples
        setGuardSamples(levelData, kDefaultTableSize);
    }

    data.setNumLevels(kMaxMipmapLevels);
}

} // namespace detail

// =============================================================================
// Standard Waveform Generators (FR-016, FR-017, FR-018)
// =============================================================================

/// @brief Generate mipmapped sawtooth wavetable via FFT/IFFT.
///
/// Populates all kMaxMipmapLevels levels. Level 0 contains all harmonics
/// (1..tableSize/2) with amplitudes 1/n.
///
/// Frequency domain: spectrum[n] = {0.0f, -1.0f / n} for n = 1..maxHarmonic
///
/// @param data [out] WavetableData to populate. Previous contents are overwritten.
/// @post data.numLevels() == kMaxMipmapLevels
/// @post Each level is independently normalized to ~0.96 peak
/// @post Guard samples are set for branchless cubic Hermite
///
/// @note NOT real-time safe (allocates temporary buffers, performs FFT)
inline void generateMipmappedSaw(WavetableData& data) {
    detail::generateLevels(data, [](Complex* spectrum, size_t maxHarmonic, [[maybe_unused]] size_t numBins) {
        for (size_t n = 1; n <= maxHarmonic; ++n) {
            spectrum[n] = {0.0f, -1.0f / static_cast<float>(n)};
        }
    });
}

/// @brief Generate mipmapped square wave wavetable via FFT/IFFT.
///
/// Populates all kMaxMipmapLevels levels. Level 0 contains odd harmonics
/// only (1, 3, 5, ...) with amplitudes 1/n.
///
/// @param data [out] WavetableData to populate.
/// @note NOT real-time safe
inline void generateMipmappedSquare(WavetableData& data) {
    detail::generateLevels(data, [](Complex* spectrum, size_t maxHarmonic, [[maybe_unused]] size_t numBins) {
        for (size_t n = 1; n <= maxHarmonic; n += 2) {
            spectrum[n] = {0.0f, -1.0f / static_cast<float>(n)};
        }
    });
}

/// @brief Generate mipmapped triangle wave wavetable via FFT/IFFT.
///
/// Populates all kMaxMipmapLevels levels. Level 0 contains odd harmonics
/// only (1, 3, 5, ...) with amplitudes 1/n^2 and alternating sign.
///
/// @param data [out] WavetableData to populate.
/// @note NOT real-time safe
inline void generateMipmappedTriangle(WavetableData& data) {
    detail::generateLevels(data, [](Complex* spectrum, size_t maxHarmonic, [[maybe_unused]] size_t numBins) {
        for (size_t n = 1; n <= maxHarmonic; n += 2) {
            // Alternating sign: +1 for n=1, -1 for n=3, +1 for n=5, etc.
            float sign = (((n - 1) / 2) % 2 == 0) ? 1.0f : -1.0f;
            float amplitude = 1.0f / static_cast<float>(n * n);
            spectrum[n] = {0.0f, sign * amplitude};
        }
    });
}

// =============================================================================
// Custom Spectrum Generator (FR-019, FR-028)
// =============================================================================

/// @brief Generate mipmapped wavetable from a custom harmonic spectrum.
///
/// @param data [out] WavetableData to populate.
/// @param harmonicAmplitudes Array of harmonic amplitudes (index 0 = fundamental)
/// @param numHarmonics Number of elements in the array. If 0, all levels are silence.
/// @note NOT real-time safe
inline void generateMipmappedFromHarmonics(
    WavetableData& data,
    const float* harmonicAmplitudes,
    size_t numHarmonics
) {
    // FR-028: Handle 0 harmonics -- fill all levels with silence
    if (numHarmonics == 0 || harmonicAmplitudes == nullptr) {
        for (size_t level = 0; level < kMaxMipmapLevels; ++level) {
            float* levelData = data.getMutableLevel(level);
            for (size_t i = 0; i < kDefaultTableSize; ++i) {
                levelData[i] = 0.0f;
            }
            detail::setGuardSamples(levelData, kDefaultTableSize);
        }
        data.setNumLevels(kMaxMipmapLevels);
        return;
    }

    detail::generateLevels(data, [harmonicAmplitudes, numHarmonics](Complex* spectrum, size_t maxHarmonic, [[maybe_unused]] size_t numBins) {
        size_t limit = (numHarmonics < maxHarmonic) ? numHarmonics : maxHarmonic;
        for (size_t n = 1; n <= limit; ++n) {
            // harmonicAmplitudes[0] = fundamental (harmonic 1), etc.
            float amplitude = harmonicAmplitudes[n - 1];
            spectrum[n] = {0.0f, -amplitude};
        }
    });
}

// =============================================================================
// Raw Sample Generator (FR-020, FR-027)
// =============================================================================

/// @brief Generate mipmapped wavetable from raw single-cycle waveform samples.
///
/// @param data [out] WavetableData to populate.
/// @param samples Single-cycle waveform data
/// @param sampleCount Number of samples. If 0, data is left unchanged.
/// @note NOT real-time safe
inline void generateMipmappedFromSamples(
    WavetableData& data,
    const float* samples,
    size_t sampleCount
) {
    // FR-027: Handle zero-length input
    if (sampleCount == 0 || samples == nullptr) {
        return;
    }

    FFT fft;
    FFT fftTable;

    // Step 1: FFT the input to get its spectrum
    // If sampleCount != kDefaultTableSize, we need to resample via spectrum manipulation
    std::vector<Complex> inputSpectrum;

    if (sampleCount == kDefaultTableSize) {
        // Direct FFT at table size
        fft.prepare(kDefaultTableSize);
        inputSpectrum.resize(fft.numBins());
        fft.forward(samples, inputSpectrum.data());
    } else {
        // FFT at input size (must be power of 2 for our FFT, so zero-pad)
        size_t fftSize = 1;
        while (fftSize < sampleCount) {
            fftSize <<= 1;
        }
        // Clamp to valid FFT range
        if (fftSize < kMinFFTSize) fftSize = kMinFFTSize;
        if (fftSize > kMaxFFTSize) fftSize = kMaxFFTSize;

        fft.prepare(fftSize);
        std::vector<float> padded(fftSize, 0.0f);
        size_t copyCount = (sampleCount < fftSize) ? sampleCount : fftSize;
        for (size_t i = 0; i < copyCount; ++i) {
            padded[i] = samples[i];
        }

        std::vector<Complex> rawSpectrum(fft.numBins());
        fft.forward(padded.data(), rawSpectrum.data());

        // Resample spectrum to table size bins
        fftTable.prepare(kDefaultTableSize);
        size_t tableBins = fftTable.numBins();
        inputSpectrum.resize(tableBins, Complex{0.0f, 0.0f});

        size_t inputBins = fft.numBins();
        size_t copyBins = (inputBins < tableBins) ? inputBins : tableBins;

        // Scale factor for spectrum resampling
        float scaleFactor = static_cast<float>(kDefaultTableSize) / static_cast<float>(fftSize);
        for (size_t i = 0; i < copyBins; ++i) {
            inputSpectrum[i] = {rawSpectrum[i].real * scaleFactor, rawSpectrum[i].imag * scaleFactor};
        }
    }

    // Step 2: For each mipmap level, zero bins above max harmonic and IFFT
    FFT levelFft;
    levelFft.prepare(kDefaultTableSize);
    const size_t numBins = levelFft.numBins();
    std::vector<Complex> levelSpectrum(numBins);
    std::vector<float> buffer(kDefaultTableSize);

    for (size_t level = 0; level < kMaxMipmapLevels; ++level) {
        size_t maxH = detail::maxHarmonicForLevel(level, kDefaultTableSize);

        // Copy input spectrum up to maxHarmonic, zero the rest
        levelSpectrum[0] = inputSpectrum[0];  // DC
        for (size_t n = 1; n < numBins; ++n) {
            if (n <= maxH) {
                levelSpectrum[n] = inputSpectrum[n];
            } else {
                levelSpectrum[n] = {0.0f, 0.0f};
            }
        }

        // IFFT
        levelFft.inverse(levelSpectrum.data(), buffer.data());

        // Normalize
        detail::normalizeToPeak(buffer.data(), kDefaultTableSize);

        // Copy to level
        float* levelData = data.getMutableLevel(level);
        for (size_t i = 0; i < kDefaultTableSize; ++i) {
            levelData[i] = buffer[i];
        }

        // Set guard samples
        detail::setGuardSamples(levelData, kDefaultTableSize);
    }

    data.setNumLevels(kMaxMipmapLevels);
}

} // namespace DSP
} // namespace Krate
