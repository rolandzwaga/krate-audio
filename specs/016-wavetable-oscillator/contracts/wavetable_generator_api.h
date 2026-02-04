// ==============================================================================
// API Contract: WavetableGenerator (Layer 1)
// ==============================================================================
// This file defines the public API for wavetable_generator.h.
// It is a design artifact, NOT compiled code.
//
// Location: dsp/include/krate/dsp/primitives/wavetable_generator.h
// Layer: 1 (depends on Layer 0: wavetable_data.h, math_constants.h;
//           and Layer 1: fft.h)
// Namespace: Krate::DSP
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
// Standard Waveform Generators (FR-016, FR-017, FR-018)
// =============================================================================

/// @brief Generate mipmapped sawtooth wavetable via FFT/IFFT.
///
/// Populates all kMaxMipmapLevels levels. Level 0 contains all harmonics
/// (1..tableSize/2) with amplitudes 1/n. Each successive level halves the
/// maximum harmonic number.
///
/// Frequency domain: spectrum[n] = {0.0f, -1.0f / n} for n = 1..maxHarmonic
///
/// @param data [out] WavetableData to populate. Previous contents are overwritten.
/// @post data.numLevels() == kMaxMipmapLevels
/// @post Each level is independently normalized to ~0.96 peak
/// @post Guard samples are set for branchless cubic Hermite
///
/// @note NOT real-time safe (allocates temporary buffers, performs FFT)
/// @note Call during initialization only (e.g., in prepare())
void generateMipmappedSaw(WavetableData& data);

/// @brief Generate mipmapped square wave wavetable via FFT/IFFT.
///
/// Populates all kMaxMipmapLevels levels. Level 0 contains odd harmonics
/// only (1, 3, 5, ...) with amplitudes 1/n.
///
/// Frequency domain: spectrum[n] = {0.0f, -1.0f / n} for odd n only
///
/// @param data [out] WavetableData to populate.
/// @post data.numLevels() == kMaxMipmapLevels
/// @post Even harmonic magnitudes below -60 dB relative to fundamental
///
/// @note NOT real-time safe
void generateMipmappedSquare(WavetableData& data);

/// @brief Generate mipmapped triangle wave wavetable via FFT/IFFT.
///
/// Populates all kMaxMipmapLevels levels. Level 0 contains odd harmonics
/// only (1, 3, 5, ...) with amplitudes 1/n^2 and alternating sign.
///
/// Frequency domain:
///   sign = ((n - 1) / 2) % 2 == 0 ? +1 : -1
///   spectrum[n] = {0.0f, sign / (n * n)} for odd n
///
/// @param data [out] WavetableData to populate.
/// @post data.numLevels() == kMaxMipmapLevels
///
/// @note NOT real-time safe
void generateMipmappedTriangle(WavetableData& data);

// =============================================================================
// Custom Spectrum Generator (FR-019, FR-028)
// =============================================================================

/// @brief Generate mipmapped wavetable from a custom harmonic spectrum.
///
/// Populates all kMaxMipmapLevels levels. Each level includes only those
/// harmonics from the input spectrum that fall below the level's Nyquist limit.
///
/// @param data [out] WavetableData to populate.
/// @param harmonicAmplitudes Array of harmonic amplitudes, where index 0 is the
///        fundamental (harmonic 1), index 1 is harmonic 2, etc.
///        Values represent relative amplitudes (1.0 = full).
/// @param numHarmonics Number of elements in the harmonicAmplitudes array.
///        If 0, all levels are filled with silence.
///
/// @pre harmonicAmplitudes is valid for indices [0, numHarmonics) or nullptr if numHarmonics == 0
/// @post data.numLevels() == kMaxMipmapLevels
/// @post Each level is independently normalized to ~0.96 peak
/// @post Guard samples are set for branchless cubic Hermite
///
/// @note NOT real-time safe
void generateMipmappedFromHarmonics(
    WavetableData& data,
    const float* harmonicAmplitudes,
    size_t numHarmonics
);

// =============================================================================
// Raw Sample Generator (FR-020, FR-027)
// =============================================================================

/// @brief Generate mipmapped wavetable from raw single-cycle waveform samples.
///
/// Performs FFT on the input, then for each mipmap level zeroes bins above
/// the level's Nyquist limit and performs IFFT. Input is resampled to
/// match the table size if sampleCount differs from kDefaultTableSize.
///
/// @param data [out] WavetableData to populate.
/// @param samples Single-cycle waveform data (one full cycle).
/// @param sampleCount Number of samples in the input buffer.
///        If 0 or samples is nullptr, data is left in default state.
///        If != kDefaultTableSize, input is resampled via FFT zero-padding
///        or truncation.
///
/// @pre samples is valid for indices [0, sampleCount) or nullptr if sampleCount == 0
/// @post data.numLevels() == kMaxMipmapLevels (or 0 if input was empty)
/// @post Each level is independently normalized to ~0.96 peak
/// @post Guard samples are set for branchless cubic Hermite
///
/// @note NOT real-time safe
void generateMipmappedFromSamples(
    WavetableData& data,
    const float* samples,
    size_t sampleCount
);

// =============================================================================
// Implementation Notes (not part of the API)
// =============================================================================
//
// Internal helper pattern used by all generators:
//
// void generateLevels(WavetableData& data, SpectrumFiller filler) {
//     FFT fft;
//     fft.prepare(kDefaultTableSize);
//     const size_t numBins = fft.numBins();  // = tableSize/2 + 1
//
//     std::vector<Complex> spectrum(numBins);
//     std::vector<float> buffer(kDefaultTableSize);
//
//     for (size_t level = 0; level < kMaxMipmapLevels; ++level) {
//         size_t maxHarmonic = kDefaultTableSize / (1u << (level + 1));
//
//         // Clear spectrum
//         std::fill(spectrum.begin(), spectrum.end(), Complex{0.0f, 0.0f});
//
//         // Fill spectrum (DC = 0, harmonics 1..maxHarmonic)
//         filler(spectrum.data(), maxHarmonic);
//
//         // IFFT to time domain
//         fft.inverse(spectrum.data(), buffer.data());
//
//         // Normalize to ~0.96 peak
//         float peak = 0.0f;
//         for (size_t i = 0; i < kDefaultTableSize; ++i) {
//             float absVal = std::abs(buffer[i]);
//             if (absVal > peak) peak = absVal;
//         }
//         if (peak > 0.0f) {
//             float scale = 0.96f / peak;
//             for (size_t i = 0; i < kDefaultTableSize; ++i) {
//                 buffer[i] *= scale;
//             }
//         }
//
//         // Copy to WavetableData level and set guard samples
//         float* levelData = data.getMutableLevel(level);
//         for (size_t i = 0; i < kDefaultTableSize; ++i) {
//             levelData[i] = buffer[i];
//         }
//         // Guard: levelData[-1] = levelData[N-1]
//         // Guard: levelData[N]   = levelData[0]
//         // Guard: levelData[N+1] = levelData[1]
//         // Guard: levelData[N+2] = levelData[2]
//         // (via physical array index manipulation)
//     }
//     data.setNumLevels(kMaxMipmapLevels);
// }
//

} // namespace DSP
} // namespace Krate
