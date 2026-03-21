#pragma once

// ==============================================================================
// Layer 2: SIMD-Accelerated Modal Resonator Bank Kernel
// ==============================================================================
// Vectorized coupled-form resonator processing across modes using Google Highway.
// Called from ModalResonatorBank::processBlock() for the main resonator loop.
//
// The coupled-form recurrence has an intra-mode dependency (c_new uses s_new),
// so we cannot vectorize along time. Instead we vectorize across modes:
// process 4 (SSE) or 8 (AVX2) modes simultaneously per iteration.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocation)
// - Principle IV: SIMD & DSP Optimization (Highway runtime dispatch)
// - Principle IX: Layer 2 (depends on Layer 0)
// ==============================================================================

namespace Krate {
namespace DSP {

/// @brief SIMD-accelerated coupled-form resonator batch processing for one sample.
///
/// Processes `numModes` resonators for a single excitation sample:
/// 1. Coupled-form advance: s_new = R*(s + eps*c) + gain*excitation; c_new = R*(c - eps*s_new)
/// 2. Accumulate output: sum += s_new (across all modes)
///
/// @param sinState      In/out sin state array (aligned, kMaxModes)
/// @param cosState      In/out cos state array (aligned, kMaxModes)
/// @param epsilon       Frequency coefficients (aligned)
/// @param radius        Damping radius coefficients (aligned)
/// @param inputGain     Input gain per mode (aligned)
/// @param excitation    The excitation sample (after transient emphasis)
/// @param numModes      Number of modes to process
/// @return              Sum of all mode outputs (s_new values)
float processModalBankSampleSIMD(
    float* sinState,
    float* cosState,
    const float* epsilon,
    const float* radius,
    const float* inputGain,
    float excitation,
    int numModes) noexcept;

}  // namespace DSP
}  // namespace Krate
