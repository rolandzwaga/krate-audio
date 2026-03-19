// ==============================================================================
// Layer 2: SIMD-Accelerated Harmonic Oscillator Bank Kernels
// ==============================================================================
// Vectorized MCF oscillator processing across partials using Google Highway.
//
// Uses Highway's self-inclusion pattern: foreach_target.h re-includes this
// file once per ISA target. The SIMD kernels compile for each target;
// HWY_EXPORT/HWY_DYNAMIC_DISPATCH select the best at runtime.
//
// Feature: MPE polyphonic support (Phase 6)
// Reference: dsp/include/krate/dsp/core/spectral_simd.cpp (pattern)
// ==============================================================================

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "krate/dsp/processors/harmonic_oscillator_bank_simd.cpp"
#include "hwy/foreach_target.h"  // NOLINT(misc-header-include-cycle) Highway self-inclusion by design
#include "hwy/highway.h"

#include <cstddef>

// =============================================================================
// Per-Target SIMD Kernels (compiled once per ISA target)
// =============================================================================

HWY_BEFORE_NAMESPACE();

// NOLINTNEXTLINE(modernize-concat-nested-namespaces) HWY_NAMESPACE is a macro
namespace Krate {
namespace DSP {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// -----------------------------------------------------------------------------
// ProcessMcfBatchSIMDImpl: Core MCF oscillator loop vectorized across partials
//
// For each group of N partials (4 SSE / 8 AVX2):
//   1. Amplitude smoothing: amp += coeff * (target*aa - amp)
//   2. Compute output: ampSample = sin * amp (no bandwidth in SIMD path)
//   3. Accumulate stereo: sumL += ampSample * panL; sumR += ampSample * panR
//   4. MCF advance: sNew = s + eps*c; cNew = c - eps*sNew
//   5. Write back sin/cos/amp
// -----------------------------------------------------------------------------

// NOLINTNEXTLINE(misc-use-internal-linkage) exported via HWY_EXPORT
void ProcessMcfBatchSIMDImpl(
    float* HWY_RESTRICT sinState,
    float* HWY_RESTRICT cosState,
    const float* HWY_RESTRICT epsilon,
    const float* HWY_RESTRICT detuneMultiplier,
    float* HWY_RESTRICT currentAmplitude,
    const float* HWY_RESTRICT targetAmplitude,
    const float* HWY_RESTRICT antiAliasGain,
    const float* HWY_RESTRICT panLeft,
    const float* HWY_RESTRICT panRight,
    float ampSmoothCoeff,
    int numPartials,
    float* HWY_RESTRICT outSumL,
    float* HWY_RESTRICT outSumR) {

    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto vCoeff = hn::Set(d, ampSmoothCoeff);

    auto vSumL = hn::Zero(d);
    auto vSumR = hn::Zero(d);

    size_t i = 0;
    const size_t count = static_cast<size_t>(numPartials);

    // SIMD loop: process N partials per iteration
    for (; i + N <= count; i += N) {
        // Load per-partial state
        auto vSin = hn::Load(d, sinState + i);
        auto vCos = hn::Load(d, cosState + i);
        auto vEps = hn::Load(d, epsilon + i);
        auto vDetune = hn::Load(d, detuneMultiplier + i);
        auto vAmp = hn::Load(d, currentAmplitude + i);
        const auto vTarget = hn::Load(d, targetAmplitude + i);
        const auto vAA = hn::Load(d, antiAliasGain + i);
        const auto vPanL = hn::Load(d, panLeft + i);
        const auto vPanR = hn::Load(d, panRight + i);

        // 1. Amplitude smoothing: amp += coeff * (target*aa - amp)
        const auto vTargetAA = hn::Mul(vTarget, vAA);
        const auto vDiff = hn::Sub(vTargetAA, vAmp);
        vAmp = hn::MulAdd(vCoeff, vDiff, vAmp);

        // 2. Output sample: ampSample = sin * amp
        const auto vAmpSample = hn::Mul(vSin, vAmp);

        // 3. Stereo accumulation
        vSumL = hn::MulAdd(vAmpSample, vPanL, vSumL);
        vSumR = hn::MulAdd(vAmpSample, vPanR, vSumR);

        // 4. MCF advance with detune: eps_eff = eps * detune
        const auto vEpsEff = hn::Mul(vEps, vDetune);
        const auto vSinNew = hn::MulAdd(vEpsEff, vCos, vSin);
        const auto vCosNew = hn::NegMulAdd(vEpsEff, vSinNew, vCos);

        // 5. Write back state
        hn::Store(vSinNew, d, sinState + i);
        hn::Store(vCosNew, d, cosState + i);
        hn::Store(vAmp, d, currentAmplitude + i);
    }

    // Horizontal sum of SIMD accumulators
    *outSumL = hn::ReduceSum(d, vSumL);
    *outSumR = hn::ReduceSum(d, vSumR);

    // Scalar tail for remaining partials
    for (; i < count; ++i) {
        float target = targetAmplitude[i] * antiAliasGain[i];
        currentAmplitude[i] += ampSmoothCoeff * (target - currentAmplitude[i]);

        float s = sinState[i];
        float c = cosState[i];
        float eps = epsilon[i] * detuneMultiplier[i];

        float ampSample = s * currentAmplitude[i];
        *outSumL += ampSample * panLeft[i];
        *outSumR += ampSample * panRight[i];

        float sNew = s + eps * c;
        float cNew = c - eps * sNew;

        sinState[i] = sNew;
        cosState[i] = cNew;
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace DSP
}  // namespace Krate

HWY_AFTER_NAMESPACE();

// =============================================================================
// HWY_ONCE: Export table + public API (compiled exactly once)
// =============================================================================

#if HWY_ONCE

#include "krate/dsp/processors/harmonic_oscillator_bank_simd.h"

// NOLINTNEXTLINE(modernize-concat-nested-namespaces) HWY_NAMESPACE dispatch section
namespace Krate {
namespace DSP {

HWY_EXPORT(ProcessMcfBatchSIMDImpl);

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
    float& sumR) noexcept {

    float outSumL = 0.0f;
    float outSumR = 0.0f;

    HWY_DYNAMIC_DISPATCH(ProcessMcfBatchSIMDImpl)(
        sinState, cosState, epsilon, detuneMultiplier,
        currentAmplitude, targetAmplitude, antiAliasGain,
        panLeft, panRight, ampSmoothCoeff, numPartials,
        &outSumL, &outSumR);

    sumL += outSumL;
    sumR += outSumR;
}

}  // namespace DSP
}  // namespace Krate

#endif  // HWY_ONCE
