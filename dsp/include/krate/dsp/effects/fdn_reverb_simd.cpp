// ==============================================================================
// Layer 4: FDN Reverb - Highway SIMD Kernels
// ==============================================================================
// SIMD-accelerated kernels for the 8-channel FDN reverb (FR-015):
//   (a) One-pole filter bank (8-channel parallel filtering)
//   (b) Hadamard FWHT butterfly (3-stage for N=8)
//   (c) Householder feedback matrix (sum + broadcast + subtract)
//
// Uses Highway's self-inclusion pattern: foreach_target.h re-includes this
// file once per ISA target. The SIMD kernels compile for each target;
// HWY_EXPORT/HWY_DYNAMIC_DISPATCH select the best at runtime.
//
// Feature: 125-dual-reverb (FR-015)
// Reference: dsp/include/krate/dsp/core/spectral_simd.cpp (pattern)
// ==============================================================================

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "krate/dsp/effects/fdn_reverb_simd.cpp"
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
// ApplyFilterBankSIMD: 8-channel one-pole filter bank (FR-011, FR-015a)
// state[i] = coeff[i] * input[i] + (1 - coeff[i]) * state[i]
// output[i] = state[i]
// -----------------------------------------------------------------------------

// NOLINTNEXTLINE(misc-use-internal-linkage) exported via HWY_EXPORT
void ApplyFilterBankSIMDImpl(const float* HWY_RESTRICT inputs,
                             float* HWY_RESTRICT states,
                             const float* HWY_RESTRICT coeffs,
                             float* HWY_RESTRICT outputs,
                             size_t numChannels) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto one = hn::Set(d, 1.0f);

    size_t i = 0;
    for (; i + N <= numChannels; i += N) {
        const auto input = hn::LoadU(d, inputs + i);
        const auto state = hn::LoadU(d, states + i);
        const auto coeff = hn::LoadU(d, coeffs + i);

        // newState = coeff * input + (1 - coeff) * state
        const auto oneMinusCoeff = hn::Sub(one, coeff);
        const auto newState = hn::MulAdd(coeff, input, hn::Mul(oneMinusCoeff, state));

        hn::StoreU(newState, d, states + i);
        hn::StoreU(newState, d, outputs + i);
    }

    // Scalar tail
    for (; i < numChannels; ++i) {
        states[i] = coeffs[i] * inputs[i] + (1.0f - coeffs[i]) * states[i];
        outputs[i] = states[i];
    }
}

// -----------------------------------------------------------------------------
// ApplyHadamardSIMD: 8-point FWHT butterfly (FR-008, FR-015b)
// 3 stages (log2(8) = 3), each doing N/2 add/subtract pairs
// followed by 1/sqrt(8) normalization
// Uses FixedTag<float, 4> for guaranteed 4-wide SIMD on all x86
// -----------------------------------------------------------------------------

// NOLINTNEXTLINE(misc-use-internal-linkage) exported via HWY_EXPORT
void ApplyHadamardSIMDImpl(float* HWY_RESTRICT data,
                           [[maybe_unused]] size_t numChannels) {
    const hn::FixedTag<float, 4> d4;

    // Stage 1: stride = 4 (SIMD: lo[0:3] +/- hi[4:7])
    {
        auto lo = hn::LoadU(d4, data);
        auto hi = hn::LoadU(d4, data + 4);
        hn::StoreU(hn::Add(lo, hi), d4, data);
        hn::StoreU(hn::Sub(lo, hi), d4, data + 4);
    }

    // Stage 2: stride = 2 (scalar — requires interleaved pairs)
    for (size_t k = 0; k < 8; k += 4) {
        for (size_t i = 0; i < 2; ++i) {
            float a = data[k + i];
            float b = data[k + i + 2];
            data[k + i] = a + b;
            data[k + i + 2] = a - b;
        }
    }

    // Stage 3: stride = 1 (scalar — adjacent pairs)
    for (size_t k = 0; k < 8; k += 2) {
        float a = data[k];
        float b = data[k + 1];
        data[k] = a + b;
        data[k + 1] = a - b;
    }

    // SIMD normalization: 2x 4-wide multiply by 1/sqrt(8)
    constexpr float kNorm = 0.35355339059327373f;
    const auto norm = hn::Set(d4, kNorm);
    hn::StoreU(hn::Mul(hn::LoadU(d4, data), norm), d4, data);
    hn::StoreU(hn::Mul(hn::LoadU(d4, data + 4), norm), d4, data + 4);
}

// -----------------------------------------------------------------------------
// ApplyHouseholderSIMD: Householder feedback matrix (FR-010, FR-015c)
// y[i] = x[i] - (2/N) * sum(x) for N=8
// Uses 2x FixedTag<float, 4> for guaranteed SIMD on all x86
// -----------------------------------------------------------------------------

// NOLINTNEXTLINE(misc-use-internal-linkage) exported via HWY_EXPORT
void ApplyHouseholderSIMDImpl(float* HWY_RESTRICT data,
                              [[maybe_unused]] size_t numChannels) {
    const hn::FixedTag<float, 4> d4;

    // Load two halves
    const auto lo = hn::LoadU(d4, data);
    const auto hi = hn::LoadU(d4, data + 4);

    // Sum all 8 elements
    const float sumLo = hn::ReduceSum(d4, lo);
    const float sumHi = hn::ReduceSum(d4, hi);
    const float sum = sumLo + sumHi;

    // Broadcast scaled sum and subtract
    const auto scaled = hn::Set(d4, sum * 0.25f);  // 2/8 = 0.25
    hn::StoreU(hn::Sub(lo, scaled), d4, data);
    hn::StoreU(hn::Sub(hi, scaled), d4, data + 4);
}

// -----------------------------------------------------------------------------
// ApplyFeedbackSIMD: feedback gain + input injection (FR-015)
// data[i] = data[i] * gains[i] + input
// -----------------------------------------------------------------------------

// NOLINTNEXTLINE(misc-use-internal-linkage) exported via HWY_EXPORT
void ApplyFeedbackSIMDImpl(float* HWY_RESTRICT data,
                            const float* HWY_RESTRICT gains,
                            float input,
                            size_t numChannels) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto inputVec = hn::Set(d, input);

    size_t i = 0;
    for (; i + N <= numChannels; i += N) {
        const auto val = hn::LoadU(d, data + i);
        const auto gain = hn::LoadU(d, gains + i);
        hn::StoreU(hn::MulAdd(val, gain, inputVec), d, data + i);
    }

    // Scalar tail
    for (; i < numChannels; ++i) {
        data[i] = data[i] * gains[i] + input;
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace DSP
}  // namespace Krate

HWY_AFTER_NAMESPACE();

// =============================================================================
// Dispatch Table + Wrapper Functions (compiled once)
// =============================================================================

#if HWY_ONCE

namespace Krate::DSP {  // NOLINT(modernize-concat-nested-namespaces) already concatenated; false positive under HWY_ONCE guard

HWY_EXPORT(ApplyFilterBankSIMDImpl);
HWY_EXPORT(ApplyHadamardSIMDImpl);
HWY_EXPORT(ApplyHouseholderSIMDImpl);
HWY_EXPORT(ApplyFeedbackSIMDImpl);

void fdnApplyFilterBankSIMD(  // NOLINT(misc-use-internal-linkage) Highway HWY_DYNAMIC_DISPATCH requires external linkage
    const float* inputs, float* states,
    const float* coeffs, float* outputs,
    size_t numChannels) noexcept {
    HWY_DYNAMIC_DISPATCH(ApplyFilterBankSIMDImpl)(inputs, states, coeffs,
                                                   outputs, numChannels);
}

void fdnApplyHadamardSIMD(  // NOLINT(misc-use-internal-linkage) Highway HWY_DYNAMIC_DISPATCH requires external linkage
    float* data, size_t numChannels) noexcept {
    HWY_DYNAMIC_DISPATCH(ApplyHadamardSIMDImpl)(data, numChannels);
}

void fdnApplyHouseholderSIMD(  // NOLINT(misc-use-internal-linkage) Highway HWY_DYNAMIC_DISPATCH requires external linkage
    float* data, size_t numChannels) noexcept {
    HWY_DYNAMIC_DISPATCH(ApplyHouseholderSIMDImpl)(data, numChannels);
}

void fdnApplyFeedbackSIMD(  // NOLINT(misc-use-internal-linkage) Highway HWY_DYNAMIC_DISPATCH requires external linkage
    float* data, const float* gains,
    float input, size_t numChannels) noexcept {
    HWY_DYNAMIC_DISPATCH(ApplyFeedbackSIMDImpl)(data, gains, input, numChannels);
}

}  // namespace Krate::DSP

#endif  // HWY_ONCE
