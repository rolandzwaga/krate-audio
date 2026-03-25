// ==============================================================================
// Layer 3: SIMD-Accelerated Sympathetic Resonance Bank Kernel
// ==============================================================================
// Vectorized second-order driven resonator processing across resonators using
// Google Highway. Called from SympatheticResonance::process() for the main
// resonator loop.
//
// Each resonator is independent (no cross-resonator feedback), so we vectorize
// across resonators: process 4 (SSE/NEON) or 8 (AVX2) simultaneously.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocation)
// - Principle IV: SIMD & DSP Optimization (Highway runtime dispatch)
// - Principle IX: Layer 3 (depends on Layer 0)
// ==============================================================================

#pragma once

namespace Krate {
namespace DSP {

/// @brief SIMD-accelerated driven resonator batch processing for one sample.
///
/// Processes `count` resonators for a single input sample:
/// 1. Second-order recurrence: y[n] = coeff * y[n-1] - rSquared * y[n-2] + scaledInput * gain
/// 2. Envelope follower: peak detect with instant attack, exponential release
/// 3. Accumulate output: sum += y[n] (across all resonators)
///
/// @param y1s            In/out y[n-1] state array
/// @param y2s            In/out y[n-2] state array
/// @param coeffs         2*r*cos(omega) coefficient per resonator
/// @param rSquareds      r^2 per resonator
/// @param gains          Input gain per resonator (1/sqrt(partialNumber))
/// @param count          Number of resonators to process
/// @param scaledInput    Input sample scaled by smoothed coupling gain
/// @param sums           Out: pointer to accumulated output sum
/// @param releaseCoeff   Envelope follower release coefficient
/// @param envelopes      In/out envelope follower state per resonator
void processSympatheticBankSIMD(
    float* y1s,
    float* y2s,
    const float* coeffs,
    const float* rSquareds,
    const float* gains,
    int count,
    float scaledInput,
    float* sums,
    float releaseCoeff,
    float* envelopes) noexcept;

}  // namespace DSP
}  // namespace Krate
