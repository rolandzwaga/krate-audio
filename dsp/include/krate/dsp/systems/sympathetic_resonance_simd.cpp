// ==============================================================================
// Layer 3: SIMD-Accelerated Sympathetic Resonance Bank Kernel
// ==============================================================================
// Vectorized second-order driven resonator processing across resonators using
// Google Highway.
//
// Uses Highway's self-inclusion pattern: foreach_target.h re-includes this
// file once per ISA target. The SIMD kernels compile for each target;
// HWY_EXPORT/HWY_DYNAMIC_DISPATCH select the best at runtime.
//
// Reference: modal_resonator_bank_simd.cpp (pattern)
// ==============================================================================

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "krate/dsp/systems/sympathetic_resonance_simd.cpp"
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
// ProcessSympatheticBankSIMDImpl: Second-order resonator loop vectorized across
// resonators.
//
// For each group of N resonators (4 SSE / 8 AVX2):
//   1. Load y1, y2, coeff, rSquared, gain, envelope
//   2. Second-order recurrence: y = coeff * y1 - rSquared * y2 + scaledInput * gain
//   3. Update state: y2 = y1, y1 = y
//   4. Envelope follower: env = max(|y|, env * releaseCoeff)
//   5. Accumulate output: sum += y
//   6. Write back y1, y2, envelope
// -----------------------------------------------------------------------------

// NOLINTNEXTLINE(misc-use-internal-linkage) exported via HWY_EXPORT
void ProcessSympatheticBankSIMDImpl(
    float* HWY_RESTRICT y1s,
    float* HWY_RESTRICT y2s,
    const float* HWY_RESTRICT coeffs,
    const float* HWY_RESTRICT rSquareds,
    const float* HWY_RESTRICT gains,
    int count,
    float scaledInput,
    float* HWY_RESTRICT outSum,
    float releaseCoeff,
    float* HWY_RESTRICT envelopes) {

    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto vScaledInput = hn::Set(d, scaledInput);
    const auto vReleaseCoeff = hn::Set(d, releaseCoeff);

    auto vSum = hn::Zero(d);

    size_t i = 0;
    const size_t total = static_cast<size_t>(count);

    // SIMD loop: process N resonators per iteration
    for (; i + N <= total; i += N) {
        // Load per-resonator state
        auto vY1 = hn::Load(d, y1s + i);
        auto vY2 = hn::Load(d, y2s + i);
        const auto vCoeff = hn::Load(d, coeffs + i);
        const auto vRSq = hn::Load(d, rSquareds + i);
        const auto vGain = hn::Load(d, gains + i);
        auto vEnv = hn::Load(d, envelopes + i);

        // Second-order recurrence:
        // y = coeff * y1 - rSquared * y2 + scaledInput * gain
        const auto vCoeffY1 = hn::Mul(vCoeff, vY1);                          // coeff * y1
        const auto vCoeffY1MinusRSqY2 = hn::NegMulAdd(vRSq, vY2, vCoeffY1); // coeff*y1 - rSq*y2
        const auto vY = hn::MulAdd(vGain, vScaledInput, vCoeffY1MinusRSqY2); // + gain*scaledInput

        // Update state: y2 = y1, y1 = y
        hn::Store(vY1, d, y2s + i);    // y2 = old y1
        hn::Store(vY, d, y1s + i);     // y1 = new y

        // Envelope follower: env = max(|y|, env * releaseCoeff)
        const auto vAbsY = hn::Abs(vY);
        const auto vEnvDecayed = hn::Mul(vEnv, vReleaseCoeff);
        const auto vEnvNew = hn::Max(vAbsY, vEnvDecayed);
        hn::Store(vEnvNew, d, envelopes + i);

        // Accumulate output
        vSum = hn::Add(vSum, vY);
    }

    // Horizontal sum of SIMD accumulator
    *outSum = hn::ReduceSum(d, vSum);

    // Scalar tail for remaining resonators
    for (; i < total; ++i) {
        float y1 = y1s[i];
        float y2 = y2s[i];
        float coeff = coeffs[i];
        float rSq = rSquareds[i];
        float gain = gains[i];

        float y = coeff * y1 - rSq * y2 + scaledInput * gain;

        y2s[i] = y1;
        y1s[i] = y;

        // Envelope follower
        float absY = (y >= 0.0f) ? y : -y;
        float envDecayed = envelopes[i] * releaseCoeff;
        envelopes[i] = (absY > envDecayed) ? absY : envDecayed;

        *outSum += y;
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

#include "krate/dsp/systems/sympathetic_resonance_simd.h"

// NOLINTNEXTLINE(modernize-concat-nested-namespaces) HWY_NAMESPACE dispatch section
namespace Krate {
namespace DSP {

HWY_EXPORT(ProcessSympatheticBankSIMDImpl);

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
    float* envelopes) noexcept {

    HWY_DYNAMIC_DISPATCH(ProcessSympatheticBankSIMDImpl)(
        y1s, y2s, coeffs, rSquareds, gains,
        count, scaledInput, sums, releaseCoeff, envelopes);
}

}  // namespace DSP
}  // namespace Krate

#endif  // HWY_ONCE
