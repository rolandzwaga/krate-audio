// ==============================================================================
// API Contract: SIMD-Accelerated Batch Math Functions
// ==============================================================================
// This file documents the exact public API contract for the new batch
// functions added to spectral_simd.h as part of spec 066-simd-optimization.
//
// This is a CONTRACT FILE -- it defines the expected signatures, behavior,
// and constraints. The actual implementation lives in:
//   dsp/include/krate/dsp/core/spectral_simd.h  (declarations)
//   dsp/include/krate/dsp/core/spectral_simd.cpp (definitions)
// ==============================================================================

#pragma once

#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// Constants
// =============================================================================

/// Minimum input value for log operations.
/// Non-positive inputs to batchLog10() are clamped to this value.
/// This is the single source of truth -- callers MUST NOT define separate equivalents.
/// Value matches FormantPreserver::kMinMagnitude (1e-10f).
inline constexpr float kMinLogInput = 1e-10f;

/// Maximum output value for pow10 operations.
/// Output of batchPow10() is clamped to this value to prevent infinity.
/// Value matches FormantPreserver::reconstructEnvelope() existing clamp.
inline constexpr float kMaxPow10Output = 1e6f;

// =============================================================================
// Batch Math Functions
// =============================================================================

/// @brief Batch compute log10(x) for an array of floats using SIMD
///
/// For each element: output[k] = log10(max(input[k], kMinLogInput))
///
/// @param input  Input array of float values (may be unaligned)
/// @param output Output array, must hold at least `count` floats (may be unaligned)
/// @param count  Number of elements to process
///
/// @pre input != nullptr OR count == 0
/// @pre output != nullptr OR count == 0
/// @post Every output[k] is finite (no NaN, no -inf)
/// @post For input[k] > 0: |output[k] - std::log10(input[k])| < 1e-5  (SC-004)
///
/// @note count == 0: returns immediately, no memory access
/// @note Non-positive inputs clamped to kMinLogInput (branchless)
/// @note SIMD hot loop: zero heap allocations, zero branches
/// @note Runtime ISA dispatch: SSE2 / AVX2 / AVX-512 / NEON
void batchLog10(const float* input, float* output, std::size_t count) noexcept;

/// @brief Batch compute 10^x for an array of floats using SIMD
///
/// For each element: output[k] = clamp(10^input[k], kMinLogInput, kMaxPow10Output)
/// Implementation: hn::Exp(d, hn::Mul(v, Set(d, ln(10))))
///
/// @param input  Input array of float exponents (may be unaligned)
/// @param output Output array, must hold at least `count` floats (may be unaligned)
/// @param count  Number of elements to process
///
/// @pre input != nullptr OR count == 0
/// @pre output != nullptr OR count == 0
/// @post Every output[k] is in [kMinLogInput, kMaxPow10Output] (no inf)
/// @post For input[k] in [-10, +6]: relative error vs std::pow(10,x) < 1e-5  (SC-005)
///
/// @note count == 0: returns immediately, no memory access
/// @note Output clamped to [kMinLogInput, kMaxPow10Output] (branchless)
/// @note SIMD hot loop: zero heap allocations, zero branches
/// @note Runtime ISA dispatch: SSE2 / AVX2 / AVX-512 / NEON
void batchPow10(const float* input, float* output, std::size_t count) noexcept;

/// @brief Batch wrap phase values to [-pi, +pi] range using SIMD (out-of-place)
///
/// For each element: output[k] = input[k] - 2*pi * round(input[k] / (2*pi))
/// Uses IEEE 754 round-to-nearest-even via hn::Round.
///
/// @param input  Input array of phase values in radians (may be unaligned)
/// @param output Output array, must hold at least `count` floats (may be unaligned)
/// @param count  Number of elements to process
///
/// @pre input != nullptr OR count == 0
/// @pre output != nullptr OR count == 0
/// @post Every output[k] is in [-pi, +pi]
/// @post |output[k] - wrapPhase(input[k])| < 1e-6  (SC-006)
///
/// @note count == 0: returns immediately, no memory access
/// @note Branchless: O(1) operations per element regardless of input magnitude
/// @note SIMD hot loop: zero heap allocations, zero branches
/// @note Runtime ISA dispatch: SSE2 / AVX2 / AVX-512 / NEON
void batchWrapPhase(const float* input, float* output, std::size_t count) noexcept;

/// @brief Batch wrap phase values to [-pi, +pi] range using SIMD (in-place)
///
/// For each element: data[k] = data[k] - 2*pi * round(data[k] / (2*pi))
///
/// @param data  Array of phase values in radians (modified in-place)
/// @param count Number of elements to process
///
/// @pre data != nullptr OR count == 0
/// @post Every data[k] is in [-pi, +pi]
///
/// @note Same behavior as out-of-place variant
void batchWrapPhase(float* data, std::size_t count) noexcept;

} // namespace DSP
} // namespace Krate
