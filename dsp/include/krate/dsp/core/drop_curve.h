// ==============================================================================
// Layer 0: Core Utility - Profundum Phase 4 (specs/profundum-phase4-drop-note-behaviour)
// ==============================================================================
// DropCurve / makeDropCurve: the Drop's latched curve math and every
// curve-geometry constant (plan S1 C-1, C-12). Includes stdlib and
// core/db_utils.h only.
//   Spec:  specs/profundum-phase4-drop-note-behaviour/spec.md
//   Plan:  specs/profundum-phase4-drop-note-behaviour/plan.md (S3, S11.1)
//   Carries: SC-001-SC-005, SC-024(a) (through DropEnvelope), SC-013 (through
//            DropPhaseLock).
//
// Per the 2026-10-12 O-1 ruling no kMaxUpwardSemitonesPerSample is defined and
// the bridge rise is always L_min samples (spec FR-007).
//
// Representation: the planned curve d(n) (semitones, n in samples from the
// trigger's effective sample) is a chain of at most kMaxPieces pieces. Every
// piece is
//     d(n) = c0 + c1*tau + c2*tau^2 + c3*tau^3 + eDown*E_down(n) + eUp*E_up(n),
//     tau  = (n - start) / (end - start),
//     E_down(n) = exp(-k*u),  E_up(n) = exp(-k*(1 - u)),  u = (n - fallStart)/N.
// A cubic is fixed by its end values and slopes, so every Hermite fillet,
// the linear family and the blend of any of them is a cubic; the exponential
// family and its mirror contribute the two exponential terms (plan S3.1-S3.5).
// The Hermite coefficients are hoisted to the piece (T005 lever (2)).
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

/// Latched Drop curve (plain data, FR-003, D-4). Built by makeDropCurve.
struct DropCurve {
    static constexpr int kMaxPieces = 12;

    // Curve-geometry constants: the ONLY definitions (plan S1 C-12). DropEnvelope and
    // DropPhaseLock alias them.
    static constexpr double kExpRate = 8.0;                 // k: exponential family e^{-k u}
    static constexpr double kLandingFilletFraction = 0.05;  // delta: landing fillet (last 5 % of T)
    static constexpr double kOnsetFilletFraction = 0.05;    // bridged triggers only (C-8)
    static constexpr double kElasticOvershootFraction = 0.15;  // epsilon_e / A (D-2)
    static constexpr double kElasticDecay = 0.5;               // spec bound on the peak ratio (D-2)
    static constexpr double kElasticPeakRatio = 0.45;          // q, the realised peak ratio
    static constexpr double kElasticSettleFactor = 2.0;        // ringing ends at 2 T
    static constexpr double kElasticLandingSlope = 1.5;        // 1.5 * eps_e / (ELAY[1] - ELAY[0]), per T in A
    static constexpr double kMaxReboundFraction = 0.25;        // rho_R = R * 0.25 * A (D-3)
    static constexpr double kReboundSettleFactor = 0.5;        // rebound return over 0.5 T
    static constexpr double kDropBridgeMs = 1.0;               // L_min = round(min(1 ms, T) * fs / 1000)
    static constexpr double kBrakeSemitones = 0.5;             // brake excursion when L_br > 1 (C-8)
    /// Elastic knot layout in units of T after the fall start (C-12).
    static constexpr std::array<double, 7> kElasticKnotLayout{1.00, 1.15, 1.30, 1.45, 1.60, 1.75, 2.00};

    enum class PieceKind : std::uint8_t { Brake, Rise, OnsetFillet, ExpBlend, Fillet, Tail };

    /// One piece over [start, end) in samples (see the file comment for the form).
    struct Piece {
        double start = 0.0, end = 0.0, invLen = 0.0;
        double c0 = 0.0, c1 = 0.0, c2 = 0.0, c3 = 0.0;
        double eDown = 0.0, eUp = 0.0;
        PieceKind kind = PieceKind::Tail;
    };

    double amount = 0.0, reboundDepth = 0.0;  // A, rho_R (st)
    double fallSamples = 1.0;                 // N = T*fs/1000 (double, not rounded)
    double fallStart = 0.0;                   // samples of brake + rise before the fall (0 on snap)
    std::uint8_t famA = 0, famB = 1;
    double blendW = 0.0, elasticW = 0.0;
    bool onsetFillet = false;                                                   // bridged triggers (C-8)
    double brakeLen = 0.0, brakeY0 = 0.0, brakeM0 = 0.0, riseLen = 0.0, riseY0 = 0.0;  // samples / st / st per sample
    std::array<double, 7> knotT{}, knotY{}, knotV{};  // tail: samples from the fall start / st / st per sample
    bool hasTail = false;
    double endSample = 0.0;     // fallStart + t_end in samples; 0 for the zero curve
    double expRate = 0.0;       // k / N (per sample)
    std::array<Piece, kMaxPieces> pieces{};
    int numPieces = 0;

    [[nodiscard]] int pieceCount() const noexcept { return numPieces; }
    [[nodiscard]] double pieceStart(int i) const noexcept { return pieces[static_cast<std::size_t>(i)].start; }
    [[nodiscard]] double pieceEnd(int i) const noexcept { return pieces[static_cast<std::size_t>(i)].end; }
    [[nodiscard]] PieceKind pieceKind(int i) const noexcept { return pieces[static_cast<std::size_t>(i)].kind; }

    /// e^{-k u} at sample position n.
    [[nodiscard]] double expDownAt(double n) const noexcept { return std::exp(-expRate * (n - fallStart)); }
    /// e^{-k (1 - u)} at sample position n.
    [[nodiscard]] double expUpAt(double n) const noexcept {
        return std::exp(expRate * (n - fallStart) - kExpRate);
    }

    /// Index of the piece holding n (first piece with n < end), or -1 past the end.
    [[nodiscard]] int pieceIndexAt(double n) const noexcept {
        for (int i = 0; i < numPieces; ++i) {
            if (n < pieces[static_cast<std::size_t>(i)].end)
                return i;
        }
        return -1;
    }

    /// d of piece i at sample position n (no range check).
    [[nodiscard]] double evalPiece(int i, double n) const noexcept {
        const Piece& p = pieces[static_cast<std::size_t>(i)];
        const double tau = (n - p.start) * p.invLen;
        double d = ((p.c3 * tau + p.c2) * tau + p.c1) * tau + p.c0;
        if (p.eDown != 0.0)
            d += p.eDown * expDownAt(n);
        if (p.eUp != 0.0)
            d += p.eUp * expUpAt(n);
        return d;
    }

    /// dd/dn of piece i at sample position n (st per sample).
    [[nodiscard]] double evalPieceSlope(int i, double n) const noexcept {
        const Piece& p = pieces[static_cast<std::size_t>(i)];
        const double tau = (n - p.start) * p.invLen;
        double s = ((3.0 * p.c3 * tau + 2.0 * p.c2) * tau + p.c1) * p.invLen;
        if (p.eDown != 0.0)
            s -= p.eDown * expRate * expDownAt(n);
        if (p.eUp != 0.0)
            s += p.eUp * expRate * expUpAt(n);
        return s;
    }

    /// d at sample position n (closed form, double). 0 at and after endSample; n < 0 reads n = 0.
    [[nodiscard]] double valueAt(double n) const noexcept {
        if (!(n < endSample))
            return 0.0;
        const double m = std::max(n, 0.0);
        const int i = pieceIndexAt(m);
        return i < 0 ? 0.0 : evalPiece(i, m);
    }

    /// dd/dn at sample position n (st per sample). 0 at and after endSample.
    [[nodiscard]] double slopeAt(double n) const noexcept {
        if (!(n < endSample))
            return 0.0;
        const double m = std::max(n, 0.0);
        const int i = pieceIndexAt(m);
        return i < 0 ? 0.0 : evalPieceSlope(i, m);
    }

    /// Analytic bound on |d''| (st per sample^2) over piece i, the SC-004 checker's per-piece bound:
    /// the cubic's peak |second derivative| (linear in tau, so at an end) plus each exponential
    /// term's peak, e^{-k u} at the piece start and e^{-k(1-u)} at its end (plan S3.4).
    [[nodiscard]] double pieceD2Bound(int i) const noexcept {
        const Piece& p = pieces[static_cast<std::size_t>(i)];
        double b = std::max(std::abs(2.0 * p.c2), std::abs(2.0 * p.c2 + 6.0 * p.c3)) * p.invLen * p.invLen;
        const double k2 = expRate * expRate;
        if (p.eDown != 0.0)
            b += std::abs(p.eDown) * k2 * expDownAt(p.start);
        if (p.eUp != 0.0)
            b += std::abs(p.eUp) * k2 * expUpAt(p.end);
        return b;
    }

    /// Gauss-Legendre 8 nodes and weights on [-1, 1] (plan S1 C-2).
    static constexpr std::array<double, 8> kGLNodes{-0.9602898564975363, -0.7966664774136267, -0.5255324099163290,
                                                    -0.1834346424956498, 0.1834346424956498,  0.5255324099163290,
                                                    0.7966664774136267,  0.9602898564975363};
    static constexpr std::array<double, 8> kGLWeights{0.1012285362903763, 0.2223810344533745, 0.3137066458778873,
                                                      0.3626837833783620, 0.3626837833783620, 0.3137066458778873,
                                                      0.2223810344533745, 0.1012285362903763};

    /// Integral of g(n, d(n)) dn over [from, endSample] in samples (plan S1 C-2, S3.3): GL8 on
    /// fixed panels, 2 per piece, 4 on pieces longer than 0.5*N. O(1) in T, allocation-free.
    template <class F>
    [[nodiscard]] double integrate(F&& g, double from = 0.0) const noexcept {
        double sum = 0.0;
        for (int i = 0; i < numPieces; ++i) {
            const Piece& p = pieces[static_cast<std::size_t>(i)];
            const double a = std::max(p.start, from);
            const double b = p.end;
            if (!(b > a))
                continue;
            const int panels = (b - a) > 0.5 * fallSamples ? 4 : 2;
            const double h = (b - a) / static_cast<double>(panels);
            const double r = 0.5 * h;
            for (int k = 0; k < panels; ++k) {
                const double c = a + (static_cast<double>(k) + 0.5) * h;
                for (std::size_t j = 0; j < kGLNodes.size(); ++j) {
                    const double x = c + r * kGLNodes[j];
                    sum += kGLWeights[j] * r * g(x, evalPiece(i, x));
                }
            }
        }
        return sum;
    }
};

static_assert(DropCurve::kElasticPeakRatio < DropCurve::kElasticDecay);
static_assert(DropCurve::kElasticKnotLayout[6] == DropCurve::kElasticSettleFactor);
// The bridged onset fillet replaces exactly the first fall region [0, delta] (S3.5 step 3).
static_assert(DropCurve::kOnsetFilletFraction == DropCurve::kLandingFilletFraction);
// kElasticLandingSlope = 1.5 * eps_e / (ELAY[1] - ELAY[0]) (C-12).
static_assert(DropCurve::kElasticLandingSlope - 1.5 * DropCurve::kElasticOvershootFraction /
                                                    (DropCurve::kElasticKnotLayout[1] -
                                                     DropCurve::kElasticKnotLayout[0]) <
                  1e-12 &&
              DropCurve::kElasticLandingSlope - 1.5 * DropCurve::kElasticOvershootFraction /
                                                    (DropCurve::kElasticKnotLayout[1] -
                                                     DropCurve::kElasticKnotLayout[0]) >
                  -1e-12);

namespace detail {

/// Polynomial part of one family over one fall region, as end values and slopes (per u) in p
/// units, plus the exponential coefficients (plan S3.1).
struct DropFamilyRegion {
    double p0 = 0.0, m0 = 0.0, p1 = 0.0, m1 = 0.0, eDown = 0.0, eUp = 0.0;
};

/// Family `id` (0 exponential, 1 linear, 2 logarithmic, 3 elastic fall) over fall region
/// `region` (0: [0, delta], 1: [delta, 1 - delta], 2: [1 - delta, 1]).
[[nodiscard]] inline DropFamilyRegion dropFamilyRegion(int id, int region) noexcept {
    constexpr double k = DropCurve::kExpRate;
    constexpr double df = DropCurve::kLandingFilletFraction;
    const double eA = std::exp(-k * (1.0 - df));  // e^{-k(1-delta)} = e^{-7.6}
    const double eD = std::exp(-k * df);          // e^{-k delta}     = e^{-0.4}
    DropFamilyRegion r{};
    if (id == 0) {  // exponential: e^{-k u}, then the landing fillet
        if (region < 2)
            r.eDown = 1.0;
        else
            r = {eA, -k * eA, 0.0, 0.0, 0.0, 0.0};
    } else if (id == 1) {  // linear: 1 - u, then the landing fillet
        if (region == 0)
            r = {1.0, -1.0, 1.0 - df, -1.0, 0.0, 0.0};
        else if (region == 1)
            r = {1.0 - df, -1.0, df, -1.0, 0.0, 0.0};
        else
            r = {df, -1.0, 0.0, 0.0, 0.0, 0.0};
    } else {  // logarithmic / elastic fall: 1 - p_exp(1 - u), landing fillet
        if (region == 0)  // the mirrored exponential fillet: p'(0) = 0, "holds, then falls"
            r = {1.0, 0.0, 1.0 - eA, -k * eA, 0.0, 0.0};
        else if (region == 1)
            r = {1.0, 0.0, 1.0, 0.0, 0.0, -1.0};
        else
            r = {1.0 - eD, -k * eD, 0.0, id == 3 ? -DropCurve::kElasticLandingSlope : 0.0, 0.0, 0.0};
    }
    return r;
}

/// Append a cubic Hermite piece over [start, end] in samples from value y0 / slope M0 (per tau)
/// to y1 / M1, plus the exponential coefficients.
inline void dropAppendPiece(DropCurve& c, DropCurve::PieceKind kind, double start, double end, double y0,
                            double m0Tau, double y1, double m1Tau, double eDown, double eUp) noexcept {
    if (!(end > start) || c.numPieces >= DropCurve::kMaxPieces)
        return;
    DropCurve::Piece& p = c.pieces[static_cast<std::size_t>(c.numPieces++)];
    p.start = start;
    p.end = end;
    p.invLen = 1.0 / (end - start);
    p.c0 = y0;
    p.c1 = m0Tau;
    p.c2 = 3.0 * (y1 - y0) - 2.0 * m0Tau - m1Tau;
    p.c3 = 2.0 * (y0 - y1) + m0Tau + m1Tau;
    p.eDown = eDown;
    p.eUp = eUp;
    p.kind = kind;
}

}  // namespace detail

/// Build the latched curve (plan S3.1-S3.5). A, rebound in [0, 1], shape in [0, 1], timeMs > 0 and
/// sampleRate > 0 are the caller's (clamped) values. bridged: brake -> rise -> onset fillet from
/// (dNow, slopeNow) (st, st per sample) at position 0; otherwise a snap (d(0) = A).
[[nodiscard]] inline DropCurve makeDropCurve(double A, double timeMs, double shape, double rebound, double sampleRate,
                                             bool bridged, double dNow, double slopeNow) noexcept {
    using Kind = DropCurve::PieceKind;
    constexpr double k = DropCurve::kExpRate;
    constexpr double df = DropCurve::kLandingFilletFraction;

    DropCurve c{};
    const double N = timeMs * sampleRate / 1000.0;
    c.amount = A;
    c.fallSamples = N;
    c.expRate = k / N;
    const double rhoR = DropCurve::kMaxReboundFraction * rebound * A;
    c.reboundDepth = rhoR;

    // Blend (FR-004): [0, 1/3] -> (0, 1), [1/3, 2/3] -> (1, 2), [2/3, 1] -> (2, 3).
    if (shape <= 1.0 / 3.0) {
        c.famA = 0;
        c.famB = 1;
        c.blendW = 3.0 * shape;
    } else if (shape <= 2.0 / 3.0) {
        c.famA = 1;
        c.famB = 2;
        c.blendW = 3.0 * shape - 1.0;
    } else {
        c.famA = 2;
        c.famB = 3;
        c.blendW = 3.0 * shape - 2.0;
    }
    const double we = shape > 2.0 / 3.0 ? 3.0 * shape - 2.0 : 0.0;
    c.elasticW = we;

    // Bridge (S3.5, C-8; O-1: L_r = L_min always).
    double pos = 0.0;
    c.onsetFillet = bridged;
    if (bridged) {
        const double lMin =
            std::max(1.0, std::round(std::min(DropCurve::kDropBridgeMs, timeMs) * sampleRate / 1000.0));
        double y = dNow;
        if (slopeNow != 0.0) {
            const double lBr =
                std::min(lMin, std::max(1.0, std::round(3.0 * DropCurve::kBrakeSemitones / std::abs(slopeNow))));
            const double y1 = y + slopeNow * lBr / 3.0;
            c.brakeLen = lBr;
            c.brakeY0 = y;
            c.brakeM0 = slopeNow;
            detail::dropAppendPiece(c, Kind::Brake, 0.0, lBr, y, slopeNow * lBr, y1, 0.0, 0.0, 0.0);
            y = y1;
            pos = lBr;
        }
        c.riseLen = lMin;
        c.riseY0 = y;
        detail::dropAppendPiece(c, Kind::Rise, pos, pos + lMin, y, 0.0, A, 0.0, 0.0, 0.0);
        pos += lMin;
    }
    c.fallStart = pos;

    // Fall, u in [0, 1]: d = (A + rho_R) * p(u; s) - rho_R (C-7).
    const double scale = A + rhoR;
    const double wA = 1.0 - c.blendW;
    const double wB = c.blendW;
    const std::array<double, 4> ub{0.0, df, 1.0 - df, 1.0};
    for (int region = 0; region < 3; ++region) {
        const auto fa = detail::dropFamilyRegion(c.famA, region);
        const auto fb = detail::dropFamilyRegion(c.famB, region);
        const double u0 = ub[static_cast<std::size_t>(region)];
        const double u1 = ub[static_cast<std::size_t>(region) + 1];
        const double du = u1 - u0;
        const double eDown = wA * fa.eDown + wB * fb.eDown;
        const double eUp = wA * fa.eUp + wB * fb.eUp;
        double p0 = wA * fa.p0 + wB * fb.p0;
        double m0 = wA * fa.m0 + wB * fb.m0;
        double p1 = wA * fa.p1 + wB * fb.p1;
        double m1 = wA * fa.m1 + wB * fb.m1;
        double pieceDown = eDown;
        double pieceUp = eUp;
        Kind kind = region == 2 ? Kind::Fillet : Kind::ExpBlend;
        if (bridged && region == 0) {
            // Onset fillet: zero-slope start at p = 1 to the blended family's value and slope at
            // u = delta, exponential terms folded into the cubic's end conditions.
            const double eDd = std::exp(-k * u1);
            const double eUd = std::exp(-k * (1.0 - u1));
            p1 += eDown * eDd + eUp * eUd;
            m1 += -k * eDown * eDd + k * eUp * eUd;
            p0 = 1.0;
            m0 = 0.0;
            pieceDown = 0.0;
            pieceUp = 0.0;
            kind = Kind::OnsetFillet;
        }
        // d = scale * p - rho_R (p >= 0 at the ends, so A = 0 gives +0, never -0).
        const double y0 = scale * p0 - rhoR;
        const double y1 = scale * p1 - rhoR;
        detail::dropAppendPiece(c, kind, pos + u0 * N, pos + u1 * N, y0, scale * m0 * du, y1, scale * m1 * du,
                                scale * pieceDown, scale * pieceUp);
    }

    // Tail (S3.2): present iff rho_R > 0 or w_e > 0.
    double tEnd = 1.0;
    if (rhoR > 0.0 || we > 0.0) {
        c.hasTail = true;
        const double epsA = DropCurve::kElasticOvershootFraction * A;
        const double q = DropCurve::kElasticPeakRatio;
        std::array<double, 7> t{};
        for (std::size_t i = 0; i < 7; ++i) {
            const double rlay = i < 2 ? 1.0 : 1.0 + DropCurve::kReboundSettleFactor;
            t[i] = (1.0 - we) * rlay + we * DropCurve::kElasticKnotLayout[i];
        }
        const double d1 = rhoR + we * epsA;
        const double d2 = q * we * epsA;
        const double d3 = q * q * we * epsA;
        const auto safe = [](double num, double den) { return den > 0.0 ? num / den : 0.0; };
        std::array<double, 7> v{};  // st per u
        v[0] = -scale * we * DropCurve::kElasticLandingSlope;
        v[2] = std::min(1.5 * safe(d1, t[2] - t[1]), 1.5 * safe(d2, t[3] - t[2]));
        v[4] = -std::min(1.5 * safe(d2, t[4] - t[3]), 1.5 * safe(d3, t[5] - t[4]));
        const std::array<double, 7> y{0.0 - rhoR, 0.0 - d1, 0.0, d2, 0.0, 0.0 - d3, 0.0};
        for (std::size_t i = 0; i < 7; ++i) {
            c.knotT[i] = t[i] * N;
            c.knotY[i] = y[i];
            c.knotV[i] = v[i] / N;
        }
        for (std::size_t i = 0; i + 1 < 7; ++i) {
            const double len = t[i + 1] - t[i];
            if (!(len > 0.0))
                continue;
            detail::dropAppendPiece(c, Kind::Tail, pos + t[i] * N, pos + t[i + 1] * N, y[i], v[i] * len, y[i + 1],
                                    v[i + 1] * len, 0.0, 0.0);
        }
        tEnd = t[6];
    }
    // The last piece's end is the analytic end of the planned curve (FR-053, SC-003).
    c.endSample = c.numPieces > 0 ? c.pieces[static_cast<std::size_t>(c.numPieces - 1)].end : pos + tEnd * N;
    return c;
}

}  // namespace Krate::DSP
