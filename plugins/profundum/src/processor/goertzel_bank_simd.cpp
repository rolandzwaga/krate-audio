// ==============================================================================
// Profundum Phase 12 - Goertzel bank kernel (FR-075; T040, ruled 2026-10-24)
// ==============================================================================
// Highway self-inclusion pattern (as krate/dsp/core/spectral_simd.cpp): foreach_target.h
// re-includes this file once per ISA target, and HWY_EXPORT / HWY_DYNAMIC_DISPATCH
// (inside #if HWY_ONCE) pick the best one at runtime. Unaligned LoadU / StoreU only
// (tools/lint-simd-aligned-loadstore.js).
//
// Loop order: samples outer in groups of four (the inputs broadcast once per group),
// partials inner by vector, so each partial's state is loaded and stored once per four
// samples and the vector iterations are independent. Partials past the last full
// vector run in a scalar tail over the whole segment.
// ==============================================================================

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "processor/goertzel_bank_simd.cpp"
#include "hwy/foreach_target.h"  // NOLINT(misc-header-include-cycle) Highway self-inclusion by design
#include "hwy/highway.h"

#include <cstddef>

HWY_BEFORE_NAMESPACE();

// NOLINTNEXTLINE(modernize-concat-nested-namespaces) HWY_NAMESPACE is a macro
namespace Profundum {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// NOLINTNEXTLINE(misc-use-internal-linkage) exported via HWY_EXPORT
void GoertzelBankRunImpl(const double* HWY_RESTRICT xl, const double* HWY_RESTRICT xr, size_t m,
                         const double* HWY_RESTRICT c, double* HWY_RESTRICT s1L, double* HWY_RESTRICT s2L,
                         double* HWY_RESTRICT s1R, double* HWY_RESTRICT s2R, size_t count) {
    const hn::ScalableTag<double> d;
    const size_t lanes = hn::Lanes(d);
    const size_t vecCount = count - (count % lanes);

    size_t k = 0;
    for (; k + 4 <= m; k += 4) {
        const auto inL0 = hn::Set(d, xl[k]);
        const auto inL1 = hn::Set(d, xl[k + 1]);
        const auto inL2 = hn::Set(d, xl[k + 2]);
        const auto inL3 = hn::Set(d, xl[k + 3]);
        const auto inR0 = hn::Set(d, xr[k]);
        const auto inR1 = hn::Set(d, xr[k + 1]);
        const auto inR2 = hn::Set(d, xr[k + 2]);
        const auto inR3 = hn::Set(d, xr[k + 3]);
        for (size_t q = 0; q < vecCount; q += lanes) {
            const auto cq = hn::LoadU(d, c + q);
            auto l1 = hn::LoadU(d, s1L + q);
            auto l2 = hn::LoadU(d, s2L + q);
            auto l0 = hn::Sub(hn::MulAdd(cq, l1, inL0), l2);
            l2 = hn::Sub(hn::MulAdd(cq, l0, inL1), l1);
            l1 = hn::Sub(hn::MulAdd(cq, l2, inL2), l0);
            l0 = hn::Sub(hn::MulAdd(cq, l1, inL3), l2);
            hn::StoreU(l0, d, s1L + q);
            hn::StoreU(l1, d, s2L + q);
            auto r1 = hn::LoadU(d, s1R + q);
            auto r2 = hn::LoadU(d, s2R + q);
            auto r0 = hn::Sub(hn::MulAdd(cq, r1, inR0), r2);
            r2 = hn::Sub(hn::MulAdd(cq, r0, inR1), r1);
            r1 = hn::Sub(hn::MulAdd(cq, r2, inR2), r0);
            r0 = hn::Sub(hn::MulAdd(cq, r1, inR3), r2);
            hn::StoreU(r0, d, s1R + q);
            hn::StoreU(r1, d, s2R + q);
        }
    }
    for (; k < m; ++k) {
        const auto x = hn::Set(d, xl[k]);
        const auto y = hn::Set(d, xr[k]);
        for (size_t q = 0; q < vecCount; q += lanes) {
            const auto cq = hn::LoadU(d, c + q);
            const auto l1 = hn::LoadU(d, s1L + q);
            const auto l0 = hn::Sub(hn::MulAdd(cq, l1, x), hn::LoadU(d, s2L + q));
            hn::StoreU(l1, d, s2L + q);
            hn::StoreU(l0, d, s1L + q);
            const auto r1 = hn::LoadU(d, s1R + q);
            const auto r0 = hn::Sub(hn::MulAdd(cq, r1, y), hn::LoadU(d, s2R + q));
            hn::StoreU(r1, d, s2R + q);
            hn::StoreU(r0, d, s1R + q);
        }
    }

    // Scalar tail: the partials past the last full vector, over the whole segment.
    for (size_t q = vecCount; q < count; ++q) {
        const double cq = c[q];
        double l1 = s1L[q];
        double l2 = s2L[q];
        double r1 = s1R[q];
        double r2 = s2R[q];
        for (size_t j = 0; j < m; ++j) {
            const double l0 = xl[j] + cq * l1 - l2;
            l2 = l1;
            l1 = l0;
            const double r0 = xr[j] + cq * r1 - r2;
            r2 = r1;
            r1 = r0;
        }
        s1L[q] = l1;
        s2L[q] = l2;
        s1R[q] = r1;
        s2R[q] = r2;
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace Profundum

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

#include "processor/goertzel_bank_simd.h"

namespace Profundum {

HWY_EXPORT(GoertzelBankRunImpl);

void goertzelBankRun(const double* xl, const double* xr, std::size_t m, const double* c, double* s1L, double* s2L,
                     double* s1R, double* s2R, std::size_t count) noexcept {
    HWY_DYNAMIC_DISPATCH(GoertzelBankRunImpl)(xl, xr, m, c, s1L, s2L, s1R, s2R, count);
}

}  // namespace Profundum

#endif  // HWY_ONCE
