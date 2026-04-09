// ==============================================================================
// Layer 2: SIMD-Accelerated Modal Resonator Bank Kernel
// ==============================================================================
// Vectorized coupled-form resonator processing across modes using Google Highway.
//
// Uses Highway's self-inclusion pattern: foreach_target.h re-includes this
// file once per ISA target. The SIMD kernels compile for each target;
// HWY_EXPORT/HWY_DYNAMIC_DISPATCH select the best at runtime.
//
// Reference: harmonic_oscillator_bank_simd.cpp (pattern)
// ==============================================================================

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "krate/dsp/processors/modal_resonator_bank_simd.cpp"
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
// ProcessModalBankSampleSIMDImpl: Coupled-form resonator loop vectorized across modes
//
// For each group of N modes (4 SSE / 8 AVX2):
//   1. Load sin/cos state, epsilon, radius, inputGain
//   2. Coupled-form advance: s_new = R*(s + eps*c) + gain*excitation
//                            c_new = R*(c - eps*s_new)
//   3. Accumulate output: sum += s_new
//   4. Write back sin/cos state
// -----------------------------------------------------------------------------

// NOLINTNEXTLINE(misc-use-internal-linkage) exported via HWY_EXPORT
void ProcessModalBankSampleSIMDImpl(
    float* HWY_RESTRICT sinState,
    float* HWY_RESTRICT cosState,
    const float* HWY_RESTRICT epsilon,
    const float* HWY_RESTRICT radius,
    const float* HWY_RESTRICT inputGain,
    float excitation,
    int numModes,
    float* HWY_RESTRICT outSum) {

    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto vExcitation = hn::Set(d, excitation);

    auto vSum = hn::Zero(d);

    size_t i = 0;
    const size_t count = static_cast<size_t>(numModes);

    // SIMD loop: process N modes per iteration
    for (; i + N <= count; i += N) {
        // Load per-mode state
        auto vSin = hn::LoadU(d, sinState + i);
        auto vCos = hn::LoadU(d, cosState + i);
        const auto vEps = hn::LoadU(d, epsilon + i);
        const auto vR = hn::LoadU(d, radius + i);
        const auto vGain = hn::LoadU(d, inputGain + i);

        // Coupled-form resonator (FR-003):
        // s_new = R * (s + eps * c) + gain * excitation
        // c_new = R * (c - eps * s_new)
        const auto vSPlusEpsC = hn::MulAdd(vEps, vCos, vSin);           // s + eps*c
        const auto vRSPlusEpsC = hn::Mul(vR, vSPlusEpsC);               // R*(s + eps*c)
        const auto vSNew = hn::MulAdd(vGain, vExcitation, vRSPlusEpsC); // R*(s+eps*c) + gain*ex

        const auto vCMinusEpsSNew = hn::NegMulAdd(vEps, vSNew, vCos);   // c - eps*s_new
        const auto vCNew = hn::Mul(vR, vCMinusEpsSNew);                 // R*(c - eps*s_new)

        // Accumulate output
        vSum = hn::Add(vSum, vSNew);

        // Write back state
        hn::StoreU(vSNew, d, sinState + i);
        hn::StoreU(vCNew, d, cosState + i);
    }

    // Horizontal sum of SIMD accumulator
    *outSum = hn::ReduceSum(d, vSum);

    // Scalar tail for remaining modes
    for (; i < count; ++i) {
        float s = sinState[i];
        float c = cosState[i];
        float eps = epsilon[i];
        float R = radius[i];
        float gain = inputGain[i];

        float s_new = R * (s + eps * c) + gain * excitation;
        float c_new = R * (c - eps * s_new);

        sinState[i] = s_new;
        cosState[i] = c_new;
        *outSum += s_new;
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

#include "krate/dsp/processors/modal_resonator_bank_simd.h"

// NOLINTNEXTLINE(modernize-concat-nested-namespaces) HWY_NAMESPACE dispatch section
namespace Krate {
namespace DSP {

HWY_EXPORT(ProcessModalBankSampleSIMDImpl);

float processModalBankSampleSIMD(
    float* sinState,
    float* cosState,
    const float* epsilon,
    const float* radius,
    const float* inputGain,
    float excitation,
    int numModes) noexcept {

    float outSum = 0.0f;

    HWY_DYNAMIC_DISPATCH(ProcessModalBankSampleSIMDImpl)(
        sinState, cosState, epsilon, radius, inputGain,
        excitation, numModes, &outSum);

    return outSum;
}

}  // namespace DSP
}  // namespace Krate

#endif  // HWY_ONCE
