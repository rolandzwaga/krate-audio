// ==============================================================================
// Test helper - Profundum Phase 4 (specs/profundum-phase4-drop-note-behaviour)
// ==============================================================================
// dropCheckC1: the Drop-specific C¹ checker (plan S11.2). Same rules as Phase 3's
// anchorCheckC1 (dsp/tests/unit/systems/anchor_oscillator_test_helpers.h:165), but
// with the per-segment analytic bound supplied by the caller (DropCurve::
// pieceD2Bound), because AnchorSegmentSpec has no bound field and cannot express
// the exponential pieces. Std + drop_curve.h only (no Catch2), so the systems TU
// can include it next to anchorCheckC1 for the cross-check (T020).
// ==============================================================================

#pragma once

#include <krate/dsp/core/drop_curve.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Krate::DSP::DropTest {

/// One analytic segment: samples [start, start + len] inclusive (its end sample is the next
/// segment's start sample), with its analytic bound on |Δ²g| per sample².
struct DropC1Segment {
    std::size_t start, len;
    double bound;
};

struct DropC1Report {
    double worstRatio;         // max in-segment |Δ²g| / that segment's bound (1e300 for a zero bound)
    double worstBoundaryJump;  // max |Δg[b+1] − Δg[b]| at a segment boundary b
    bool inRange;              // every sample in [lo, hi]
    bool pass;                 // inRange, worstRatio ≤ 1.05, every boundary jump ≤ 1.05 × its bound
};

/// Rules (anchorCheckC1, plan S11.2): interior |Δ²g| ≤ 1.05·bound; at each boundary b (a segment's
/// start, b ≥ 1) |g[b+1] − 2g[b] + g[b−1]| ≤ 1.05 × the larger adjacent bound; a zero bound with a
/// non-zero Δ² fails; every sample in [lo, hi]. Samples past the end of g are not visited.
inline DropC1Report dropCheckC1(std::span<const double> g, std::span<const DropC1Segment> segs, double lo,
                                double hi) {
    constexpr double kTolerance = 1.05;
    constexpr double kUnbounded = 1e300;
    DropC1Report rep{0.0, 0.0, true, false};
    for (const double v : g) {
        if (!(v >= lo && v <= hi))
            rep.inRange = false;
    }
    const auto ratioOf = [&](double d2, double b) {
        if (b > 0.0)
            return std::abs(d2) / b;
        return d2 == 0.0 ? 0.0 : kUnbounded;
    };
    const auto d2At = [&](std::size_t n) { return g[n + 1] - 2.0 * g[n] + g[n - 1]; };

    for (const DropC1Segment& s : segs) {
        for (std::size_t n = s.start + 1; n + 1 <= s.start + s.len && n + 1 < g.size(); ++n)
            rep.worstRatio = std::max(rep.worstRatio, ratioOf(d2At(n), s.bound));
    }

    bool boundariesOk = true;
    for (std::size_t i = 1; i < segs.size(); ++i) {
        const std::size_t b = segs[i].start;
        if (b < 1 || b + 1 >= g.size())
            continue;
        const double jump = std::abs(d2At(b));
        rep.worstBoundaryJump = std::max(rep.worstBoundaryJump, jump);
        if (jump > kTolerance * std::max(segs[i - 1].bound, segs[i].bound))
            boundariesOk = false;
    }

    rep.pass = rep.inRange && rep.worstRatio <= kTolerance && boundariesOk;
    return rep;
}

/// Peak |second difference| per sample² of a cubic Hermite from (g0, slope gp0 per sample) to
/// (g1, slope 0) over len samples: max(|6Δ − 4m₀|, |6Δ − 2m₀|)/L², Δ = g1 − g0, m₀ = gp0·L (the
/// anchorHermitePeakD2 formula, anchor_oscillator_test_helpers.h:121, restated for the processors TU).
inline double dropHermitePeakD2(double g0, double gp0, double g1, std::size_t len) {
    const double L = static_cast<double>(len);
    const double delta = g1 - g0;
    const double m0 = L * gp0;
    return std::max(std::abs(6.0 * delta - 4.0 * m0), std::abs(6.0 * delta - 2.0 * m0)) / (L * L);
}

/// Every field of two DropCurves equal bit for bit (std::bit_cast per field: no struct memcmp,
/// the struct has padding).
inline bool dropCurveBitEqual(const DropCurve& a, const DropCurve& b) {
    const auto eq = [](double x, double y) { return std::bit_cast<std::uint64_t>(x) == std::bit_cast<std::uint64_t>(y); };
    bool ok = eq(a.amount, b.amount) && eq(a.reboundDepth, b.reboundDepth) && eq(a.fallSamples, b.fallSamples) &&
              eq(a.fallStart, b.fallStart) && a.famA == b.famA && a.famB == b.famB && eq(a.blendW, b.blendW) &&
              eq(a.elasticW, b.elasticW) && a.onsetFillet == b.onsetFillet && eq(a.brakeLen, b.brakeLen) &&
              eq(a.brakeY0, b.brakeY0) && eq(a.brakeM0, b.brakeM0) && eq(a.riseLen, b.riseLen) &&
              eq(a.riseY0, b.riseY0) && a.hasTail == b.hasTail && eq(a.endSample, b.endSample) &&
              eq(a.expRate, b.expRate) && a.numPieces == b.numPieces;
    for (std::size_t i = 0; i < a.knotT.size(); ++i)
        ok = ok && eq(a.knotT[i], b.knotT[i]) && eq(a.knotY[i], b.knotY[i]) && eq(a.knotV[i], b.knotV[i]);
    for (std::size_t i = 0; i < a.pieces.size(); ++i) {
        const DropCurve::Piece& p = a.pieces[i];
        const DropCurve::Piece& q = b.pieces[i];
        ok = ok && eq(p.start, q.start) && eq(p.end, q.end) && eq(p.invLen, q.invLen) && eq(p.c0, q.c0) &&
             eq(p.c1, q.c1) && eq(p.c2, q.c2) && eq(p.c3, q.c3) && eq(p.eDown, q.eDown) && eq(p.eUp, q.eUp) &&
             p.kind == q.kind;
    }
    return ok;
}

}  // namespace Krate::DSP::DropTest
