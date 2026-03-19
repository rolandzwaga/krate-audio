#pragma once

// ==============================================================================
// Layer 2: SIMD-Accelerated Harmonic Oscillator Bank Kernels
// ==============================================================================
// Vectorized MCF oscillator processing across partials using Google Highway.
// Called from HarmonicOscillatorBank::processStereo() for the main active loop.
//
// The MCF recurrence has an intra-partial dependency (cosNew uses sinNew),
// so we cannot vectorize along time. Instead we vectorize across partials:
// process 4 (SSE) or 8 (AVX2) partials simultaneously per iteration.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocation)
// - Principle IV: SIMD & DSP Optimization (Highway runtime dispatch)
// - Principle IX: Layer 2 (depends on Layer 0)
// ==============================================================================

#include <cstddef>

namespace Krate {
namespace DSP {

/// @brief SIMD-accelerated MCF oscillator batch processing.
///
/// Processes `numPartials` oscillators in one call:
/// 1. Amplitude smoothing: currentAmp += coeff * (targetAmp * aaGain - currentAmp)
/// 2. MCF advance: sNew = s + eps*c; cNew = c - eps*sNew
/// 3. Stereo accumulation: sumL += amp*s*panL; sumR += amp*s*panR
///
/// @note Does NOT handle bandwidth modulation (noise) or renormalization.
///       Those remain scalar. This covers the core MCF + amplitude + pan loop.
void processMcfBatchSIMD(
    float* sinState,
    float* cosState,
    const float* epsilon,
    const float* detuneMultiplier,
    float* currentAmplitude,
    const float* targetAmplitude,
    const float* antiAliasGain,
    const float* panLeft,
    const float* panRight,
    float ampSmoothCoeff,
    int numPartials,
    float& sumL,
    float& sumR) noexcept;

}  // namespace DSP
}  // namespace Krate
