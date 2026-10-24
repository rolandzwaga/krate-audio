#pragma once

// ==============================================================================
// Profundum Phase 12 - Goertzel bank kernel (FR-075; T040, ruled 2026-10-24)
// ==============================================================================
// The spectrum producer's per-partial Goertzel recurrence over one segment, in
// double, both channels, with Highway runtime dispatch (AVX2 + FMA on x86, NEON on
// ARM, scalar fallback). Plugin-local: goertzel_bank_simd.cpp.
//
// For every partial n < count and every sample k < m:
//   s[k] = x[k] + c[n] * s[k-1] - s[k-2],   s1 = s[k-1], s2 = s[k-2] (updated in place)
// Partials at or above count are not touched. Real-time safe: no allocation, no
// locks, no exceptions.
// ==============================================================================

#include <cstddef>

namespace Profundum {

void goertzelBankRun(const double* xl, const double* xr, std::size_t m, const double* c, double* s1L, double* s2L,
                     double* s1R, double* s2R, std::size_t count) noexcept;

}  // namespace Profundum
