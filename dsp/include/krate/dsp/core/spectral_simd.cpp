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
#include <numbers>

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

// -----------------------------------------------------------------------------
// ComputePowerSpectrumPffftImpl: in-place |X(k)|^2 for pffft ordered format
// -----------------------------------------------------------------------------
// pffft ordered real output: [DC, Nyquist, Re(1), Im(1), Re(2), Im(2), ...]
// Computes power spectrum in-place: Re(k) = Re(k)^2 + Im(k)^2, Im(k) = 0

// NOLINTNEXTLINE(misc-use-internal-linkage) exported via HWY_EXPORT
void ComputePowerSpectrumPffftImpl(float* HWY_RESTRICT spectrum, size_t fftSize) {
    // DC and Nyquist are real-only (scalar)
    spectrum[0] = spectrum[0] * spectrum[0];
    spectrum[1] = spectrum[1] * spectrum[1];

    // Complex bins 1..fftSize/2-1 are interleaved [Re, Im] starting at index 2
    const size_t numComplexBins = fftSize / 2 - 1;
    float* complexStart = spectrum + 2;

    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto zero = hn::Zero(d);

    size_t k = 0;

    // SIMD loop: process N complex bins per iteration
    for (; k + N <= numComplexBins; k += N) {
        hn::Vec<decltype(d)> re;
        hn::Vec<decltype(d)> im;
        hn::LoadInterleaved2(d, complexStart + k * 2, re, im);

        // Power = Re^2 + Im^2
        const auto power = hn::MulAdd(im, im, hn::Mul(re, re));

        // Store [power, 0, power, 0, ...]
        hn::StoreInterleaved2(power, zero, d, complexStart + k * 2);
    }

    // Scalar tail for remaining bins
    for (; k < numComplexBins; ++k) {
        const float re = complexStart[k * 2];
        const float im = complexStart[k * 2 + 1];
        complexStart[k * 2] = re * re + im * im;
        complexStart[k * 2 + 1] = 0.0f;
    }
}

// -----------------------------------------------------------------------------
// BatchLog10Impl: compute log10(x) for array with SIMD (T019)
// -----------------------------------------------------------------------------

// NOLINTNEXTLINE(misc-use-internal-linkage) exported via HWY_EXPORT
void BatchLog10Impl(const float* HWY_RESTRICT input,
                    float* HWY_RESTRICT output, size_t count) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto minVal = hn::Set(d, 1e-10f);  // kMinLogInput

    size_t k = 0;
    for (; k + N <= count; k += N) {
        auto v = hn::LoadU(d, input + k);
        v = hn::Max(v, minVal);  // Branchless clamp
        hn::StoreU(hn::Log10(d, v), d, output + k);
    }
    // Scalar tail
    for (; k < count; ++k) {
        float val = std::max(input[k], 1e-10f);
        output[k] = std::log10(val);
    }
}

// -----------------------------------------------------------------------------
// BatchPow10Impl: compute 10^x for array with SIMD (T020)
// -----------------------------------------------------------------------------

// NOLINTNEXTLINE(misc-use-internal-linkage) exported via HWY_EXPORT
void BatchPow10Impl(const float* HWY_RESTRICT input,
                    float* HWY_RESTRICT output, size_t count) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto ln10 = hn::Set(d, std::numbers::ln10_v<float>);
    const auto minVal = hn::Set(d, 1e-10f);       // kMinLogInput
    const auto maxVal = hn::Set(d, 1e6f);          // kMaxPow10Output

    size_t k = 0;
    for (; k + N <= count; k += N) {
        const auto v = hn::LoadU(d, input + k);
        auto result = hn::Exp(d, hn::Mul(v, ln10));  // e^(x * ln(10)) = 10^x
        result = hn::Max(result, minVal);  // Clamp min
        result = hn::Min(result, maxVal);  // Clamp max
        hn::StoreU(result, d, output + k);
    }
    // Scalar tail
    for (; k < count; ++k) {
        float val = std::pow(10.0f, input[k]);
        val = std::max(1e-10f, std::min(val, 1e6f));
        output[k] = val;
    }
}

// -----------------------------------------------------------------------------
// BatchWrapPhaseImpl: wrap phase to [-pi,pi] out-of-place with SIMD (T021)
// -----------------------------------------------------------------------------

// NOLINTNEXTLINE(misc-use-internal-linkage) exported via HWY_EXPORT
void BatchWrapPhaseImpl(const float* HWY_RESTRICT input,
                        float* HWY_RESTRICT output, size_t count) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto twoPi = hn::Set(d, 6.283185307f);    // 2 * pi
    const auto invTwoPi = hn::Set(d, 0.159154943f);  // 1 / (2 * pi)

    size_t k = 0;
    for (; k + N <= count; k += N) {
        const auto v = hn::LoadU(d, input + k);
        const auto n = hn::Round(hn::Mul(v, invTwoPi));
        hn::StoreU(hn::NegMulAdd(n, twoPi, v), d, output + k);  // v - n * twoPi
    }
    // Scalar tail (using same branchless formula for consistency)
    constexpr float kTwoPiScalar = 6.283185307f;
    constexpr float kInvTwoPiScalar = 0.159154943f;
    for (; k < count; ++k) {
        float n = std::round(input[k] * kInvTwoPiScalar);
        output[k] = input[k] - n * kTwoPiScalar;
    }
}

// -----------------------------------------------------------------------------
// BatchWrapPhaseInPlaceImpl: wrap phase to [-pi,pi] in-place with SIMD (T022)
// -----------------------------------------------------------------------------

// NOLINTNEXTLINE(misc-use-internal-linkage) exported via HWY_EXPORT
void BatchWrapPhaseInPlaceImpl(float* HWY_RESTRICT data, size_t count) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto twoPi = hn::Set(d, 6.283185307f);
    const auto invTwoPi = hn::Set(d, 0.159154943f);

    size_t k = 0;
    for (; k + N <= count; k += N) {
        const auto v = hn::LoadU(d, data + k);
        const auto n = hn::Round(hn::Mul(v, invTwoPi));
        hn::StoreU(hn::NegMulAdd(n, twoPi, v), d, data + k);
    }
    constexpr float kTwoPiScalar = 6.283185307f;
    constexpr float kInvTwoPiScalar = 0.159154943f;
    for (; k < count; ++k) {
        float n = std::round(data[k] * kInvTwoPiScalar);
        data[k] = data[k] - n * kTwoPiScalar;
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
HWY_EXPORT(ComputePowerSpectrumPffftImpl);
HWY_EXPORT(BatchLog10Impl);
HWY_EXPORT(BatchPow10Impl);
HWY_EXPORT(BatchWrapPhaseImpl);
HWY_EXPORT(BatchWrapPhaseInPlaceImpl);

void computePolarBulk(const float* complexData, size_t numBins,
                      float* mags, float* phases) noexcept {
    HWY_DYNAMIC_DISPATCH(ComputePolarImpl)(complexData, numBins, mags, phases);
}

void reconstructCartesianBulk(const float* mags, const float* phases,
                               size_t numBins, float* complexData) noexcept {
    HWY_DYNAMIC_DISPATCH(ReconstructCartesianImpl)(mags, phases, numBins, complexData);
}

void computePowerSpectrumPffft(float* spectrum, size_t fftSize) noexcept {
    HWY_DYNAMIC_DISPATCH(ComputePowerSpectrumPffftImpl)(spectrum, fftSize);
}

void batchLog10(const float* input, float* output, std::size_t count) noexcept {
    HWY_DYNAMIC_DISPATCH(BatchLog10Impl)(input, output, count);
}

void batchPow10(const float* input, float* output, std::size_t count) noexcept {
    HWY_DYNAMIC_DISPATCH(BatchPow10Impl)(input, output, count);
}

void batchWrapPhase(const float* input, float* output, std::size_t count) noexcept {
    HWY_DYNAMIC_DISPATCH(BatchWrapPhaseImpl)(input, output, count);
}

void batchWrapPhase(float* data, std::size_t count) noexcept {
    HWY_DYNAMIC_DISPATCH(BatchWrapPhaseInPlaceImpl)(data, count);
}

}  // namespace DSP
}  // namespace Krate

#endif  // HWY_ONCE
