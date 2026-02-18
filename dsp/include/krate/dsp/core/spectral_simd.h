// ==============================================================================
// Layer 0: Core Utility - SIMD-Accelerated Spectral Math
// ==============================================================================
// Bulk magnitude/phase computation and Cartesian reconstruction using
// Google Highway for runtime SIMD dispatch (SSE2/AVX2/AVX-512/NEON).
//
// These functions are the vectorized equivalents of per-bin sqrt/atan2/cos/sin.
// SpectralBuffer calls them at representation boundaries for O(1) amortized
// polar ↔ Cartesian conversion.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 0 (no DSP dependencies)
// ==============================================================================

#pragma once

#include <cstddef>

namespace Krate {
namespace DSP {

/// @brief Bulk compute magnitude and phase from interleaved Complex data
/// @param complexData Pointer to interleaved {real, imag} float pairs
/// @param numBins Number of complex bins (NOT number of floats)
/// @param mags Output magnitude array (must hold numBins floats)
/// @param phases Output phase array in radians (must hold numBins floats)
/// @note SIMD-accelerated with runtime ISA dispatch
void computePolarBulk(const float* complexData, size_t numBins,
                      float* mags, float* phases) noexcept;

/// @brief Bulk reconstruct interleaved Complex data from magnitude and phase
/// @param mags Input magnitude array
/// @param phases Input phase array in radians
/// @param numBins Number of complex bins (NOT number of floats)
/// @param complexData Output interleaved {real, imag} float pairs (must hold 2*numBins floats)
/// @note SIMD-accelerated with runtime ISA dispatch
void reconstructCartesianBulk(const float* mags, const float* phases,
                               size_t numBins, float* complexData) noexcept;

/// @brief In-place power spectrum for pffft ordered real-FFT output
///
/// Computes |X(k)|^2 for each bin in pffft's ordered format:
///   [DC, Nyquist, Re(1), Im(1), Re(2), Im(2), ...]
/// After: DC^2, Nyquist^2, and each complex bin becomes [Re^2+Im^2, 0].
///
/// @param spectrum pffft ordered spectrum buffer (modified in-place, must be SIMD-aligned)
/// @param fftSize  FFT size (number of floats in the buffer)
/// @note SIMD-accelerated with runtime ISA dispatch
void computePowerSpectrumPffft(float* spectrum, size_t fftSize) noexcept;

} // namespace DSP
} // namespace Krate
