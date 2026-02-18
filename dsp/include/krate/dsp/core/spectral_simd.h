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

/// Minimum input value for log operations. Clamps zero/negative to avoid NaN/inf.
/// Shared constant: FormantPreserver and batchLog10 use this instead of separate equivalents.
inline constexpr float kMinLogInput = 1e-10f;

/// Maximum output value for pow10 operations. Prevents overflow to infinity.
inline constexpr float kMaxPow10Output = 1e6f;

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

/// @brief Batch compute log10(x) for an array of floats using SIMD
/// @param input Input array of float values
/// @param output Output array (must hold count floats)
/// @param count Number of elements
/// @note Non-positive inputs are clamped to kMinLogInput before log10
/// @note SIMD-accelerated with runtime ISA dispatch
void batchLog10(const float* input, float* output, std::size_t count) noexcept;

/// @brief Batch compute 10^x for an array of floats using SIMD
/// @param input Input array of float values (exponents)
/// @param output Output array (must hold count floats)
/// @param count Number of elements
/// @note Output clamped to [kMinLogInput, kMaxPow10Output]
/// @note SIMD-accelerated with runtime ISA dispatch
void batchPow10(const float* input, float* output, std::size_t count) noexcept;

/// @brief Batch wrap phase values to [-pi, +pi] range using SIMD (out-of-place)
/// @param input Input array of phase values in radians
/// @param output Output array (must hold count floats)
/// @param count Number of elements
/// @note Uses branchless round-and-subtract formula
/// @note SIMD-accelerated with runtime ISA dispatch
void batchWrapPhase(const float* input, float* output, std::size_t count) noexcept;

/// @brief Batch wrap phase values to [-pi, +pi] range using SIMD (in-place)
/// @param data Array of phase values in radians (modified in-place)
/// @param count Number of elements
/// @note Uses branchless round-and-subtract formula
/// @note SIMD-accelerated with runtime ISA dispatch
void batchWrapPhase(float* data, std::size_t count) noexcept;

} // namespace DSP
} // namespace Krate
