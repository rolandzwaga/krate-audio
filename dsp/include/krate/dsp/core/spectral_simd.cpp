// ==============================================================================
// Layer 0: Core Utility - SIMD-Accelerated Spectral Math
// ==============================================================================
// Bulk magnitude/phase computation and Cartesian reconstruction using
// Google Highway for runtime SIMD dispatch (SSE2/AVX2/AVX-512/NEON).
//
// This file uses Highway's self-inclusion pattern: foreach_target.h re-includes
// this file once per ISA target. The SIMD kernels compile for each target;
// HWY_EXPORT/HWY_DYNAMIC_DISPATCH (inside #if HWY_ONCE) select the best at
// runtime.
// ==============================================================================

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "krate/dsp/core/spectral_simd.cpp"
#include "hwy/foreach_target.h"  // NOLINT(misc-header-include-cycle) Highway self-inclusion by design
#include "hwy/highway.h"
#include "hwy/contrib/math/math-inl.h"

#include <cmath>
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
// ComputePolarImpl: Complex[] -> mags[] + phases[]
// -----------------------------------------------------------------------------

// NOLINTNEXTLINE(misc-use-internal-linkage) exported via HWY_EXPORT
void ComputePolarImpl(const float* HWY_RESTRICT complexData, size_t numBins,
                      float* HWY_RESTRICT mags, float* HWY_RESTRICT phases) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);

    size_t k = 0;

    // SIMD loop: process N bins per iteration
    for (; k + N <= numBins; k += N) {
        // Load interleaved [real0, imag0, real1, imag1, ...] into separate vectors
        hn::Vec<decltype(d)> re;
        hn::Vec<decltype(d)> im;
        hn::LoadInterleaved2(d, complexData + k * 2, re, im);

        // Magnitude: sqrt(re^2 + im^2)
        const auto reSq = hn::Mul(re, re);
        const auto mag = hn::Sqrt(hn::MulAdd(im, im, reSq));

        // Phase: atan2(im, re)
        const auto phase = hn::Atan2(d, im, re);

        hn::Store(mag, d, mags + k);
        hn::Store(phase, d, phases + k);
    }

    // Scalar tail for remaining bins
    for (; k < numBins; ++k) {
        const float re = complexData[k * 2];
        const float im = complexData[k * 2 + 1];
        mags[k] = std::sqrt(re * re + im * im);
        phases[k] = std::atan2(im, re);
    }
}

// -----------------------------------------------------------------------------
// ReconstructCartesianImpl: mags[] + phases[] -> Complex[]
// -----------------------------------------------------------------------------

// NOLINTNEXTLINE(misc-use-internal-linkage) exported via HWY_EXPORT
void ReconstructCartesianImpl(const float* HWY_RESTRICT mags,
                               const float* HWY_RESTRICT phases,
                               size_t numBins,
                               float* HWY_RESTRICT complexData) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);

    size_t k = 0;

    // SIMD loop: process N bins per iteration
    for (; k + N <= numBins; k += N) {
        const auto mag = hn::Load(d, mags + k);
        const auto phase = hn::Load(d, phases + k);

        // Compute sin(phase) and cos(phase)
        const auto sinVal = hn::Sin(d, phase);
        const auto cosVal = hn::Cos(d, phase);

        // re = mag * cos(phase), im = mag * sin(phase)
        const auto re = hn::Mul(mag, cosVal);
        const auto im = hn::Mul(mag, sinVal);

        // Store as interleaved [real0, imag0, real1, imag1, ...]
        hn::StoreInterleaved2(re, im, d, complexData + k * 2);
    }

    // Scalar tail for remaining bins
    for (; k < numBins; ++k) {
        complexData[k * 2] = mags[k] * std::cos(phases[k]);
        complexData[k * 2 + 1] = mags[k] * std::sin(phases[k]);
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

#include "krate/dsp/core/spectral_simd.h"

// NOLINTNEXTLINE(modernize-concat-nested-namespaces) HWY_NAMESPACE dispatch section
namespace Krate {
namespace DSP {

HWY_EXPORT(ComputePolarImpl);
HWY_EXPORT(ReconstructCartesianImpl);

void computePolarBulk(const float* complexData, size_t numBins,
                      float* mags, float* phases) noexcept {
    HWY_DYNAMIC_DISPATCH(ComputePolarImpl)(complexData, numBins, mags, phases);
}

void reconstructCartesianBulk(const float* mags, const float* phases,
                               size_t numBins, float* complexData) noexcept {
    HWY_DYNAMIC_DISPATCH(ReconstructCartesianImpl)(mags, phases, numBins, complexData);
}

}  // namespace DSP
}  // namespace Krate

#endif  // HWY_ONCE
