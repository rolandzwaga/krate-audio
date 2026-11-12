// ==============================================================================
// Layer 0: Core Utility - Ring-buffer splice search
// ==============================================================================
// The splice-point search of PitchSyncGranularShifter::beginSplice: among the candidate windows of a circular buffer
// that end `offset` samples older (or newer) than a reference window, the one whose samples best match the reference
// by normalised cross-correlation.
//
// Every candidate's dot product and energy accumulate in double, sample by sample, newest sample first: the same
// operations in the same order as the original search (`buffer[idx % n]` per sample), so each score, and so the chosen
// offset, is the same bit for bit. The speed comes from the indexing alone: no integer division per sample (the
// original spent ~93 % of a Harmonic Drones render in two 64-bit `div` per sample pair), and several candidates per
// pass, each with its own accumulators, so the core runs independent addition chains instead of waiting on one. On
// SSE2 eight adjacent candidates share one pass: their windows are adjacent in the buffer, so one 4-float load feeds
// four of them, and each packed-double lane is the scalar operation of its own candidate (IEEE per lane).
//
// Constitution Compliance:
// - Principle IX: Layer 0 (no dependencies except standard library)
// - Principle XIV: ODR Prevention (single definition for all consumers)
// ==============================================================================

#pragma once

#include <array>
#include <cmath>
#include <cstddef>

#if defined(__SSE2__) || defined(_M_X64) || defined(__x86_64__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define KRATE_RING_SPLICE_SSE2 1
#include <emmintrin.h>
#else
#define KRATE_RING_SPLICE_SSE2 0
#endif

namespace Krate::DSP::detail {

/// The longest reference window the search compares (PitchSyncGranularShifter clamps its match length to this).
inline constexpr std::size_t kRingSpliceMaxMatch = 256;

struct RingSpliceMatch {
    bool found = false;      ///< false: the reference window is silent (energy <= 1e-12); offset and score are unset
    std::size_t offset = 0;  ///< the best candidate offset in [minOffset, maxOffset] (the first one on a tie)
    double score = -2.0;     ///< its normalised cross-correlation, -1 when the candidate is silent
};

/// Searches the circular buffer @p buffer of @p n samples. The reference window is the @p matchLength samples ending
/// at index @p base (base, base - 1, ... modulo n); candidate @p offset is the window ending at base + offset (newer,
/// @p older false) or base - offset (older, @p older true), modulo n.
/// Preconditions: base < n, 1 <= matchLength <= kRingSpliceMaxMatch, matchLength <= n, minOffset <= maxOffset < n.
[[nodiscard]] inline RingSpliceMatch findRingSpliceMatch(const float* buffer, std::size_t n, std::size_t base,
                                                         bool older, std::size_t minOffset, std::size_t maxOffset,
                                                         std::size_t matchLength) noexcept {
    RingSpliceMatch result;
    std::array<double, kRingSpliceMaxMatch> x{};
    double referenceEnergy = 0.0;
    for (std::size_t j = 0; j < matchLength; ++j) {
        x[j] = static_cast<double>(buffer[base >= j ? base - j : base + n - j]);
        referenceEnergy += x[j] * x[j];
    }
    if (referenceEnergy <= 1e-12) {
        return result;
    }
    result.found = true;
    result.offset = maxOffset;

    // The index of the newest sample of candidate `offset`.
    const auto endOf = [&](std::size_t offset) {
        if (older) {
            return base >= offset ? base - offset : base + n - offset;
        }
        const std::size_t c = base + offset;
        return c < n ? c : c - n;
    };
    const auto consider = [&](std::size_t offset, double dot, double energy) {
        const double score = (energy > 1e-12) ? dot / std::sqrt(referenceEnergy * energy) : -1.0;
        if (score > result.score) {
            result.score = score;
            result.offset = offset;
        }
    };
    // One candidate, its window possibly wrapping past index 0.
    const auto single = [&](std::size_t offset) {
        const std::size_t end = endOf(offset);
        const std::size_t head = end + 1 < matchLength ? end + 1 : matchLength;
        double dot = 0.0;
        double energy = 0.0;
        std::size_t j = 0;
        for (const float* p = buffer + end; j < head; ++j) {
            const auto y = static_cast<double>(*(p - j));
            dot += x[j] * y;
            energy += y * y;
        }
        for (const float* p = buffer + end + n; j < matchLength; ++j) {
            const auto y = static_cast<double>(*(p - j));
            dot += x[j] * y;
            energy += y * y;
        }
        consider(offset, dot, energy);
    };

    // Four candidates, scalar.
    const auto block4 = [&](std::size_t offset) {
        const std::size_t e0 = endOf(offset);
        const std::size_t e1 = endOf(offset + 1);
        const std::size_t e2 = endOf(offset + 2);
        const std::size_t e3 = endOf(offset + 3);
        if (e0 + 1 < matchLength || e1 + 1 < matchLength || e2 + 1 < matchLength || e3 + 1 < matchLength) {
            for (std::size_t k = 0; k < 4; ++k) {
                single(offset + k);
            }
            return;
        }
        const float* p0 = buffer + e0;
        const float* p1 = buffer + e1;
        const float* p2 = buffer + e2;
        const float* p3 = buffer + e3;
        double d0 = 0.0;
        double d1 = 0.0;
        double d2 = 0.0;
        double d3 = 0.0;
        double g0 = 0.0;
        double g1 = 0.0;
        double g2 = 0.0;
        double g3 = 0.0;
        for (std::size_t j = 0; j < matchLength; ++j) {
            const double xj = x[j];
            const auto y0 = static_cast<double>(*(p0 - j));
            const auto y1 = static_cast<double>(*(p1 - j));
            const auto y2 = static_cast<double>(*(p2 - j));
            const auto y3 = static_cast<double>(*(p3 - j));
            d0 += xj * y0;
            g0 += y0 * y0;
            d1 += xj * y1;
            g1 += y1 * y1;
            d2 += xj * y2;
            g2 += y2 * y2;
            d3 += xj * y3;
            g3 += y3 * y3;
        }
        consider(offset, d0, g0);
        consider(offset + 1, d1, g1);
        consider(offset + 2, d2, g2);
        consider(offset + 3, d3, g3);
    };

#if KRATE_RING_SPLICE_SSE2
    // Eight candidates, packed: false when one of their windows wraps past index 0 or their ends straddle the buffer
    // end (the caller then takes them four at a time). Candidate k's newest sample is end0 - k (older) or end0 + k
    // (newer), so at step j the eight samples are buffer[low - j .. low - j + 7], lane l holding candidate
    // older ? 7 - l : l.
    const auto block8 = [&](std::size_t offset) {
        const std::size_t e0 = endOf(offset);
        std::size_t low = 0;
        if (older) {
            if (e0 < 7 || e0 - 7 + 1 < matchLength) {
                return false;
            }
            low = e0 - 7;
        } else {
            if (e0 + 7 >= n || e0 + 1 < matchLength) {
                return false;
            }
            low = e0;
        }
        __m128d d01 = _mm_setzero_pd();
        __m128d d23 = _mm_setzero_pd();
        __m128d d45 = _mm_setzero_pd();
        __m128d d67 = _mm_setzero_pd();
        __m128d g01 = _mm_setzero_pd();
        __m128d g23 = _mm_setzero_pd();
        __m128d g45 = _mm_setzero_pd();
        __m128d g67 = _mm_setzero_pd();
        const float* p = buffer + low;
        for (std::size_t j = 0; j < matchLength; ++j) {
            const __m128d xj = _mm_set1_pd(x[j]);
            const __m128 a = _mm_loadu_ps(p - j);
            const __m128 b = _mm_loadu_ps(p - j + 4);
            const __m128d y01 = _mm_cvtps_pd(a);
            const __m128d y23 = _mm_cvtps_pd(_mm_movehl_ps(a, a));
            const __m128d y45 = _mm_cvtps_pd(b);
            const __m128d y67 = _mm_cvtps_pd(_mm_movehl_ps(b, b));
            d01 = _mm_add_pd(d01, _mm_mul_pd(xj, y01));
            g01 = _mm_add_pd(g01, _mm_mul_pd(y01, y01));
            d23 = _mm_add_pd(d23, _mm_mul_pd(xj, y23));
            g23 = _mm_add_pd(g23, _mm_mul_pd(y23, y23));
            d45 = _mm_add_pd(d45, _mm_mul_pd(xj, y45));
            g45 = _mm_add_pd(g45, _mm_mul_pd(y45, y45));
            d67 = _mm_add_pd(d67, _mm_mul_pd(xj, y67));
            g67 = _mm_add_pd(g67, _mm_mul_pd(y67, y67));
        }
        std::array<double, 8> dot{};
        std::array<double, 8> energy{};
        _mm_storeu_pd(dot.data(), d01);
        _mm_storeu_pd(dot.data() + 2, d23);
        _mm_storeu_pd(dot.data() + 4, d45);
        _mm_storeu_pd(dot.data() + 6, d67);
        _mm_storeu_pd(energy.data(), g01);
        _mm_storeu_pd(energy.data() + 2, g23);
        _mm_storeu_pd(energy.data() + 4, g45);
        _mm_storeu_pd(energy.data() + 6, g67);
        for (std::size_t k = 0; k < 8; ++k) {
            const std::size_t lane = older ? 7 - k : k;
            consider(offset + k, dot[lane], energy[lane]);
        }
        return true;
    };
#endif

    std::size_t offset = minOffset;
#if KRATE_RING_SPLICE_SSE2
    for (; offset + 7 <= maxOffset; offset += 8) {
        if (!block8(offset)) {
            block4(offset);
            block4(offset + 4);
        }
    }
#endif
    for (; offset + 3 <= maxOffset; offset += 4) {
        block4(offset);
    }
    for (; offset <= maxOffset; ++offset) {
        single(offset);
    }
    return result;
}

}  // namespace Krate::DSP::detail
