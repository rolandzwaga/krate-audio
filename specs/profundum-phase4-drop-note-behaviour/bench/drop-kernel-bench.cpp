// Profundum Phase 4, task T005 (C-16, R-1): CPU feasibility microbench of the planned Drop components, written BEFORE
// any header lands. Stdlib only, no repo header: every structure below is a bench-local prototype of the plan design.
//
// Reproduce (repo root, Git Bash):
//   "/c/Program Files/LLVM/bin/clang++.exe" -std=c++20 -O2 -ffast-math -march=x86-64-v2 -DNDEBUG \
//       specs/profundum-phase4-drop-note-behaviour/bench/drop-kernel-bench.cpp -o <scratchpad>/drop-bench.exe
//   pwsh -NoProfile -Command "& tools/pin-perf-cores.ps1 -Exe <scratchpad>/drop-bench.exe -ExeArgs @('<stage>')"
//   (a second argument, e.g. @('1','breakdown'), first prints a per-component split of (A); diagnostic only)
//
// <stage> applies plan S13 R-1's levers cumulatively, in order (lever (1), the idle fast path, cannot move (A): the
// envelope is never idle under a 100 ms retrigger of a 200 ms drop; lever (5), the lazy solve, is already in S5.4):
//   0 = plain design (Hermite coefficients rebuilt from the endpoints at every evaluation; every predictor pass
//       evaluates the curve afresh, std::exp per exponential term per node; Newton starts from gamma = 0)
//   1 = + lever (2): Hermite (Horner) coefficients hoisted to the piece entry / curve build
//   2 = + lever (3): the solve caches 2^{(d + carry)/12} and b per node once, each pass multiplies by 2^{gamma b / 12}
//   3 = + lever (4): linearised start, no bracket-end passes, 4 passes per candidate: forced worst case 9 evaluations
//
// What is timed (48 kHz, host block 64):
//   (A) per-sample load: DropEnvelope kernel (plan S3.4: counter-based u, one geometric recurrence per exponential
//       term seeded with std::exp at the piece entry, Horner elsewhere, float out), Drop elastic s = 1, R = 1, A = 24,
//       T = 200 ms, snap-retriggered every 100 ms (curve rebuilt each time); DropPhaseLock per-sample correction
//       (S5.2: Horner b(x), one add, float cast) + staircase mirror (S5.6: one exp2 per 16 samples, one add per sample);
//       Quantized glide (S4.2: counter-based, pow only on a step change), 24 st over 500 ms restarted every 1 s;
//       RetriggerDeclick (C-15, double rotation) armed every 100 ms.
//   (B) one worst-case trigger + solve: bridged trigger at T = 1 ms, A = 48, s = 1, R = 1, from mid-fall of a previous
//       T = 20 ms drop (brake 48 + rise 48 samples, 11 pieces), with a running correction carried (S5.5), exactly 17
//       predictor evaluations forced at stages 0-2 (1 shared + 2 x (2 bracket + 6 Newton/bisection); after convergence
//       a forced pass re-evaluates the converged point, same cost), S5.3 predictor with 4 exact
//       updates + GL8 on fixed panels (2 per piece, 4 on pieces > 0.5 N) + EM endpoint terms.
//   (C) a typical trigger + solve: snap, A = 24, T = 200 ms, s = 1/3, R = 0, early stop at 1e-9 cycles.
// Budget fraction F = (ns_A * 48000 + 10 * us_B * 1000) / 1e9 (one worst-case trigger per 100 ms, SC-019's rate).
// Decision bar (tasks.md T005): F <= 0.033 % plain solve; else levers in order; F > 0.05 % after all: stop.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <xmmintrin.h>
#include <pmmintrin.h>

namespace {

// ---- C-12 constants (plan S3.3) ----
constexpr double kExpRate = 8.0, kLandingFilletFraction = 0.05, kOnsetFilletFraction = 0.05;
constexpr double kElasticOvershootFraction = 0.15, kElasticPeakRatio = 0.45, kElasticLandingSlope = 1.5;
constexpr double kMaxReboundFraction = 0.25, kDropBridgeMs = 1.0, kBrakeSemitones = 0.5;
constexpr std::array<double, 7> kElasticLayout{1.0, 1.15, 1.30, 1.45, 1.60, 1.75, 2.0};
constexpr std::array<double, 7> kReboundLayout{1.0, 1.0, 1.5, 1.5, 1.5, 1.5, 1.5};
constexpr int kMaxPieces = 12;
constexpr double kLn2Over12 = 0.057762265046662105;   // ln 2 / 12
constexpr double kMaxCorrection = 48.0;               // gamma_lim = 48 (O-1 ruling, T015)
constexpr std::size_t kPitchUpdateInterval = 16;

// ---- GL8 nodes on [-1, 1] (C-2) ----
constexpr std::array<double, 8> kGlX{-0.9602898564975363, -0.7966664774136267, -0.5255324099163290, -0.1834346424956498,
                                     0.1834346424956498,  0.5255324099163290,  0.7966664774136267,  0.9602898564975363};
constexpr std::array<double, 8> kGlW{0.1012285362903763, 0.2223810344533745, 0.3137066458778873, 0.3626837833783620,
                                     0.3626837833783620, 0.3137066458778873, 0.2223810344533745, 0.1012285362903763};

// ---- DropCurve prototype (plan S3.1-S3.5, rulings: rise L_r = L_min, no sigma_up) ----
// A piece is a cubic Hermite in t = (n - x0) / len plus exponential-family terms in u = (n - fallStart) / N:
//   d(n) = H(t) + kConst + kLin u + kExp e^{-k u} + kMir e^{-k (1 - u)}
struct Piece {
    double x0 = 0.0, x1 = 0.0, invLen = 1.0;
    double y0 = 0.0, s0 = 0.0, y1 = 0.0, s1 = 0.0;   // Hermite endpoints (st, st per unit t)
    double c2 = 0.0, c3 = 0.0;                       // hoisted Horner coefficients (lever 2)
    double kConst = 0.0, kLin = 0.0, kExp = 0.0, kMir = 0.0;
    bool longPiece = false, herm = false, expo = false;   // piece kind: Hermite part / exponential-family part
};

template <bool kHoist>
inline void hermCoeffs(const Piece& p, double& c2, double& c3) {
    if constexpr (kHoist) { c2 = p.c2; c3 = p.c3; }
    else {
        c2 = 3.0 * (p.y1 - p.y0) - 2.0 * p.s0 - p.s1;
        c3 = 2.0 * (p.y0 - p.y1) + p.s0 + p.s1;
    }
}

struct Curve {
    std::array<Piece, kMaxPieces> p{};
    int count = 0;
    double fallStart = 0.0, N = 1.0, invN = 1.0, endSample = 0.0;

    template <bool kHoist>
    double pieceValue(const Piece& q, double n) const {
        double v = 0.0;
        if (q.herm) {
            double c2, c3;
            hermCoeffs<kHoist>(q, c2, c3);
            const double t = (n - q.x0) * q.invLen;
            v = q.y0 + t * (q.s0 + t * (c2 + t * c3));
        }
        if (q.expo) {
            const double u = (n - fallStart) * invN;
            v += q.kConst + q.kLin * u;
            if (q.kExp != 0.0) v += q.kExp * std::exp(-kExpRate * u);
            if (q.kMir != 0.0) v += q.kMir * std::exp(-kExpRate * (1.0 - u));
        }
        return v;
    }
    template <bool kHoist>
    double pieceSlope(const Piece& q, double n) const {   // st per sample
        double c2, c3;
        hermCoeffs<kHoist>(q, c2, c3);
        const double t = (n - q.x0) * q.invLen;
        double v = (q.s0 + t * (2.0 * c2 + 3.0 * t * c3)) * q.invLen;
        const double u = (n - fallStart) * invN;
        double du = q.kLin;
        if (q.kExp != 0.0) du -= kExpRate * q.kExp * std::exp(-kExpRate * u);
        if (q.kMir != 0.0) du += kExpRate * q.kMir * std::exp(-kExpRate * (1.0 - u));
        return v + du * invN;
    }
    int find(double n) const {
        int i = 0;
        while (i + 1 < count && n >= p[static_cast<std::size_t>(i)].x1) ++i;
        return i;
    }
    template <bool kHoist> double valueAt(double n) const {
        if (n >= endSample || count == 0) return 0.0;
        return pieceValue<kHoist>(p[static_cast<std::size_t>(find(n))], std::max(n, 0.0));
    }
    template <bool kHoist> double slopeAt(double n) const {
        if (n >= endSample || count == 0) return 0.0;
        return pieceSlope<kHoist>(p[static_cast<std::size_t>(find(n))], std::max(n, 0.0));
    }
    Piece& add(double x0, double x1) {
        Piece& q = p[static_cast<std::size_t>(count++)];
        q.x0 = x0; q.x1 = x1; q.invLen = 1.0 / (x1 - x0);
        return q;
    }
};

// family value / slope (per u) at the region joints
struct FamJoint { double v05, d05, v95, d95; };
inline FamJoint famJoint(int id) {
    const double e04 = std::exp(-0.4), e76 = std::exp(-7.6);
    switch (id) {
        case 0: return {e04, -kExpRate * e04, e76, -kExpRate * e76};               // exponential
        case 1: return {0.95, -1.0, 0.05, -1.0};                                   // linear
        default: return {1.0 - e76, -kExpRate * e76, 1.0 - e04, -kExpRate * e04};  // logarithmic / elastic fall
    }
}

inline Curve makeDropCurve(double A, double timeMs, double s, double R, double fs, bool bridged, double dNow,
                           double slopeNow) {
    Curve c;
    const double N = timeMs * fs / 1000.0;
    c.N = N; c.invN = 1.0 / N;
    const double rho = kMaxReboundFraction * R * A;
    const double we = std::max(0.0, 3.0 * s - 2.0);
    int fa = 0, fb = 1; double w = 0.0;
    if (s <= 1.0 / 3.0) { fa = 0; fb = 1; w = 3.0 * s; }
    else if (s <= 2.0 / 3.0) { fa = 1; fb = 2; w = 3.0 * s - 1.0; }
    else { fa = 2; fb = 3; w = 3.0 * s - 2.0; }
    const double scale = A + rho;
    const std::array<int, 2> fam{fa, fb};
    const std::array<double, 2> wt{1.0 - w, w};

    double x = 0.0;
    if (bridged) {
        const double lMin = std::max(1.0, std::round(std::min(kDropBridgeMs, timeMs) * fs / 1000.0));
        double yk = dNow;
        if (slopeNow != 0.0) {
            const double lBr = std::clamp(std::round(3.0 * kBrakeSemitones / std::abs(slopeNow)), 1.0, lMin);
            Piece& q = c.add(0.0, lBr);
            q.y0 = dNow; q.s0 = slopeNow * lBr; q.y1 = dNow + slopeNow * lBr / 3.0; q.s1 = 0.0;
            yk = q.y1; x = lBr;
        }
        Piece& r = c.add(x, x + lMin);
        r.y0 = yk; r.s0 = 0.0; r.y1 = A; r.s1 = 0.0;
        x += lMin;
    }
    c.fallStart = x;
    const double u05 = kOnsetFilletFraction, u95 = 1.0 - kLandingFilletFraction;
    // region [0, 0.05]
    {
        Piece& q = c.add(x, x + u05 * N);
        if (bridged) {   // onset fillet (C-8 step 3): (1, 0) -> (p(0.05), p'(0.05))
            double v = 0.0, d = 0.0;
            for (int i = 0; i < 2; ++i) { const FamJoint j = famJoint(fam[static_cast<std::size_t>(i)]); v += wt[static_cast<std::size_t>(i)] * j.v05; d += wt[static_cast<std::size_t>(i)] * j.d05; }
            q.y0 = A; q.s0 = 0.0; q.y1 = scale * v - rho; q.s1 = scale * d * u05;
        } else {
            q.kConst = -rho; q.y0 = 0.0; q.y1 = 0.0;
            for (int i = 0; i < 2; ++i) {
                const double ww = wt[static_cast<std::size_t>(i)] * scale;
                switch (fam[static_cast<std::size_t>(i)]) {
                    case 0: q.kExp += ww; break;
                    case 1: q.kConst += ww; q.kLin -= ww; break;
                    default: {   // mirrored exponential fillet: (1, 0) -> (1 - e^{-7.6}, -8 e^{-7.6})
                        const FamJoint j = famJoint(2);
                        q.y0 += ww; q.y1 += ww * j.v05; q.s1 += ww * j.d05 * u05;
                    }
                }
            }
        }
    }
    // region [0.05, 0.95]
    {
        Piece& q = c.add(x + u05 * N, x + u95 * N);
        q.kConst = -rho;
        for (int i = 0; i < 2; ++i) {
            const double ww = wt[static_cast<std::size_t>(i)] * scale;
            switch (fam[static_cast<std::size_t>(i)]) {
                case 0: q.kExp += ww; break;
                case 1: q.kConst += ww; q.kLin -= ww; break;
                default: q.kConst += ww; q.kMir -= ww;
            }
        }
    }
    // region [0.95, 1]: landing fillet (blend of the family fillets is one Hermite)
    {
        Piece& q = c.add(x + u95 * N, x + N);
        double v = 0.0, d = 0.0, dEnd = 0.0;
        for (int i = 0; i < 2; ++i) {
            const FamJoint j = famJoint(fam[static_cast<std::size_t>(i)]);
            v += wt[static_cast<std::size_t>(i)] * j.v95; d += wt[static_cast<std::size_t>(i)] * j.d95;
            if (fam[static_cast<std::size_t>(i)] == 3) dEnd -= wt[static_cast<std::size_t>(i)] * kElasticLandingSlope;
        }
        const double lu = kLandingFilletFraction;
        q.y0 = scale * v - rho; q.s0 = scale * d * lu; q.y1 = -rho; q.s1 = scale * dEnd * lu;
    }
    // tail (S3.2)
    if (rho > 0.0 || we > 0.0) {
        std::array<double, 7> t{}, y{}, v{};
        for (std::size_t i = 0; i < 7; ++i) t[i] = (1.0 - we) * kReboundLayout[i] + we * kElasticLayout[i];
        const double eA = kElasticOvershootFraction * A;
        const double d1 = rho + we * eA, d2 = kElasticPeakRatio * we * eA, d3 = kElasticPeakRatio * kElasticPeakRatio * we * eA;
        const auto safe = [](double num, double den) { return den > 0.0 ? num / den : 0.0; };
        y = {-rho, -d1, 0.0, d2, 0.0, -d3, 0.0};
        v = {-scale * we * kElasticLandingSlope, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        v[2] = std::min(1.5 * safe(d1, t[2] - t[1]), 1.5 * safe(d2, t[3] - t[2]));
        v[4] = -std::min(1.5 * safe(d2, t[4] - t[3]), 1.5 * safe(d3, t[5] - t[4]));
        for (std::size_t i = 0; i < 6; ++i) {
            const double lu = t[i + 1] - t[i];
            if (!(lu > 0.0)) continue;
            Piece& q = c.add(x + t[i] * N, x + t[i + 1] * N);
            q.y0 = y[i]; q.s0 = v[i] * lu; q.y1 = y[i + 1]; q.s1 = v[i + 1] * lu;
        }
        c.endSample = x + t[6] * N;
    } else {
        c.endSample = x + N;
    }
    for (int i = 0; i < c.count; ++i) {
        Piece& q = c.p[static_cast<std::size_t>(i)];
        q.c2 = 3.0 * (q.y1 - q.y0) - 2.0 * q.s0 - q.s1;
        q.c3 = 2.0 * (q.y0 - q.y1) + q.s0 + q.s1;
        q.longPiece = (q.x1 - q.x0) > 0.5 * N;
        q.herm = q.y0 != 0.0 || q.s0 != 0.0 || q.y1 != 0.0 || q.s1 != 0.0;
        q.expo = q.kConst != 0.0 || q.kLin != 0.0 || q.kExp != 0.0 || q.kMir != 0.0;
    }
    return c;
}

inline double bump(double x) { return (x > 0.0 && x < 1.0) ? x * x * (16.0 + x * (-32.0 + 16.0 * x)) : 0.0; }
inline double bumpSlope(double x) { return (x > 0.0 && x < 1.0) ? 32.0 * x * (1.0 - x) * (1.0 - 2.0 * x) : 0.0; }

// ---- DropEnvelope per-sample kernel (S3.4) ----
template <bool kHoist>
struct Envelope {
    Curve c;
    std::uint32_t n = 0;
    int pi = 0;
    bool active = false;
    double eE = 0.0, rE = 1.0, eM = 0.0, rM = 1.0;
    void enter(int i) {
        pi = i;
        const Piece& q = c.p[static_cast<std::size_t>(i)];
        const double u = (static_cast<double>(n) - c.fallStart) * c.invN;
        if (q.kExp != 0.0) { eE = std::exp(-kExpRate * u); rE = std::exp(-kExpRate * c.invN); }
        if (q.kMir != 0.0) { eM = std::exp(-kExpRate * (1.0 - u)); rM = std::exp(kExpRate * c.invN); }
    }
    void trigger(const Curve& curve) { c = curve; n = 0; active = c.count > 0; if (active) enter(0); }
    float process() {
        if (!active) return 0.0f;
        const double nn = static_cast<double>(n);
        if (nn >= c.endSample) { active = false; return 0.0f; }
        while (nn >= c.p[static_cast<std::size_t>(pi)].x1) enter(pi + 1);
        const Piece& q = c.p[static_cast<std::size_t>(pi)];
        double v = 0.0;
        if (q.herm) {   // Horner on Hermite pieces (S3.4 "elsewhere")
            double c2, c3;
            hermCoeffs<kHoist>(q, c2, c3);
            const double t = (nn - q.x0) * q.invLen;
            v = q.y0 + t * (q.s0 + t * (c2 + t * c3));
        }
        if (q.expo) {   // exponential-family region: one geometric recurrence per exponential term
            const double u = (nn - c.fallStart) * c.invN;
            v += q.kConst + q.kLin * u;
            if (q.kExp != 0.0) { v += q.kExp * eE; eE *= rE; }
            if (q.kMir != 0.0) { v += q.kMir * eM; eM *= rM; }
        }
        ++n;
        return static_cast<float>(v);
    }
    // S3.4 processBlock: the same kernel with the counter and recurrence states held in locals across the block
    void processBlock(float* out, int count) {
        int i = 0;
        if (active) {
            std::uint32_t nl = n;
            int pl = pi;
            double e = eE, m = eM, re = rE, rm = rM;
            for (; i < count; ++i) {
                const double nn = static_cast<double>(nl);
                if (nn >= c.endSample) { active = false; break; }
                while (nn >= c.p[static_cast<std::size_t>(pl)].x1) {
                    n = nl; enter(pl + 1); pl = pi; e = eE; m = eM; re = rE; rm = rM;
                }
                const Piece& q = c.p[static_cast<std::size_t>(pl)];
                double v = 0.0;
                if (q.herm) {
                    double c2, c3;
                    hermCoeffs<kHoist>(q, c2, c3);
                    const double t = (nn - q.x0) * q.invLen;
                    v = q.y0 + t * (q.s0 + t * (c2 + t * c3));
                }
                if (q.expo) {
                    const double u = (nn - c.fallStart) * c.invN;
                    v += q.kConst + q.kLin * u;
                    if (q.kExp != 0.0) { v += q.kExp * e; e *= re; }
                    if (q.kMir != 0.0) { v += q.kMir * m; m *= rm; }
                }
                ++nl;
                out[i] = static_cast<float>(v);
            }
            n = nl; pi = pl; eE = e; eM = m;
        }
        for (; i < count; ++i) out[i] = 0.0f;
    }
};

// ---- DropPhaseLock per-sample correction (S5.2) + staircase mirror (S5.6) ----
struct LockKernel {
    double gamma = 0.0, invD = 0.0, ds = 0.0, c0 = 0.0, c0s = 0.0, lc = 0.0, invLc = 0.0;
    double fRes = 41.2, minF = 8.0, capF = 19200.0, invFs = 1.0 / 48000.0, fh = 41.2, acc = 0.0;
    std::uint32_t n = 0, phase = 0;
    void plan(double g, double endSample, std::uint32_t phi) {
        gamma = g; ds = endSample; invD = 1.0 / endSample; n = 0; phase = phi; lc = 0.0;
    }
    float process(float planned) {
        double dc = static_cast<double>(planned);
        const double nn = static_cast<double>(n);
        if (nn < ds) {
            const double x = nn * invD;
            dc += gamma * (x * x * (16.0 + x * (-32.0 + 16.0 * x)));
            if (nn < lc) {
                const double t = nn * invLc;
                dc += c0 + t * (c0s + t * ((-3.0 * c0 - 2.0 * c0s) + t * (2.0 * c0 + c0s)));
            }
        }
        const float out = static_cast<float>(dc);
        if (((n + kPitchUpdateInterval - phase) & (kPitchUpdateInterval - 1)) == 0)
            fh = std::clamp(fRes * std::exp2(dc * (1.0 / 12.0)), minF, capF);
        acc += (fh - fRes) * invFs;
        ++n;
        return out;
    }
    // S5.1 processBlock: same per-sample law, counter / held frequency / accumulator in locals
    void processBlock(const float* in, float* out, int count) {
        std::uint32_t nl = n;
        double f = fh, a = acc;
        for (int i = 0; i < count; ++i) {
            double dc = static_cast<double>(in[i]);
            const double nn = static_cast<double>(nl);
            if (nn < ds) {
                const double x = nn * invD;
                dc += gamma * (x * x * (16.0 + x * (-32.0 + 16.0 * x)));
                if (nn < lc) {
                    const double t = nn * invLc;
                    dc += c0 + t * (c0s + t * ((-3.0 * c0 - 2.0 * c0s) + t * (2.0 * c0 + c0s)));
                }
            }
            out[i] = static_cast<float>(dc);
            if (((nl + kPitchUpdateInterval - phase) & (kPitchUpdateInterval - 1)) == 0)
                f = std::clamp(fRes * std::exp2(dc * (1.0 / 12.0)), minF, capF);
            a += (f - fRes) * invFs;
            ++nl;
        }
        n = nl; fh = f; acc = a;
    }
};

// ---- MonoHandler Quantized glide law (S4.2, T009: no sigma_up floor) ----
struct QuantizedGlide {
    double q0 = 0.0, absDelta = 0.0, sign = 1.0, lenSamples = 1.0;
    std::uint64_t n = 0;
    int k = -1;
    float freq = 0.0f;
    void start(double from, double to, double timeMs, double fs) {
        q0 = from; absDelta = std::abs(to - from); sign = to >= from ? 1.0 : -1.0;
        lenSamples = timeMs * fs / 1000.0; n = 0; k = -1;
    }
    float process() {
        const int kk = static_cast<int>(std::min(absDelta, std::floor(static_cast<double>(n) * absDelta / lenSamples)));
        if (kk != k) {
            k = kk;
            freq = static_cast<float>(440.0 * std::pow(2.0, (q0 + sign * kk - 69.0) / 12.0));
        }
        ++n;
        return freq;
    }
};

// ---- RetriggerDeclick (C-15, S7) ----
struct Declick {
    int len = 72, k = 0;
    double c = 1.0, s = 0.0, cd = 1.0, sd = 0.0;
    bool active = false;
    void prepare(double fs) {
        len = static_cast<int>(std::ceil(1.5 * fs / 1000.0));
        cd = std::cos(3.141592653589793 / len); sd = std::sin(3.141592653589793 / len);
    }
    void arm() { if (active) return; active = true; k = 0; c = cd; s = sd; }
    float process() {
        if (!active) return 1.0f;
        float g;
        if (k == len - 1) { g = 0.0f; active = false; }
        else {
            g = static_cast<float>(0.5 * (1.0 + c));
            const double nc = c * cd - s * sd; s = s * cd + c * sd; c = nc;
        }
        ++k;
        return g;
    }
    void processBlock(float* gain, int count) {
        int i = 0;
        if (active) {
            int kl = k;
            double cl = c, sl = s;
            for (; i < count; ++i) {
                if (kl == len - 1) { gain[i] = 0.0f; ++kl; ++i; active = false; break; }
                gain[i] = static_cast<float>(0.5 * (1.0 + cl));
                const double nc = cl * cd - sl * sd; sl = sl * cd + cl * sd; cl = nc;
                ++kl;
            }
            k = kl; c = cl; s = sl;
        }
        for (; i < count; ++i) gain[i] = 1.0f;
    }
};

// ---- Predictor (S5.3) and solve (S5.4) ----
struct SolveInputs {
    const Curve* c = nullptr;
    double fRes = 41.2, fs = 48000.0, minF = 8.0, capF = 19200.0;
    std::uint32_t phi = 0;
    double c0 = 0.0, c0s = 0.0, lc = 0.0;   // carry (S5.5): Hermite (c0, c0s per sample) -> (0, 0) over lc samples
};
struct SolveResult { double gamma = 0.0, phi0 = 0.0, residual = 0.0, target = 0.0; int evals = 0, panels = 0; bool feasible = false; };

constexpr int kMaxNodes = 4 * kMaxPieces * 8 + 8;
constexpr int kLinStartPasses = 4;   // lever (4) only

template <bool kHoist, bool kCache, bool kLinStart>
struct Solver {
    SolveInputs in;
    double ds = 0.0, invD = 0.0, h = 0.0, invFs = 0.0, t4 = 0.0;
    int evals = 0, panels = 0;
    // lever-3 cache: weighted nodes (hold term, 4 exact updates, GL nodes, EM half term at t4)
    struct Node { double e, b, w; };
    std::array<Node, kMaxNodes> nodes{};
    int nodeCount = 0;
    double t4dSlope = 0.0, t4b = 0.0, t4bSlope = 0.0, t4e = 0.0;

    double carry(double n) const {
        if (!(n < in.lc)) return 0.0;
        const double t = n / in.lc, s0 = in.c0s * in.lc;
        return in.c0 + t * (s0 + t * ((-3.0 * in.c0 - 2.0 * s0) + t * (2.0 * in.c0 + s0)));
    }
    double carrySlope(double n) const {
        if (!(n < in.lc)) return 0.0;
        const double t = n / in.lc, s0 = in.c0s * in.lc;
        return (s0 + t * (2.0 * (-3.0 * in.c0 - 2.0 * s0) + 3.0 * t * (2.0 * in.c0 + s0))) / in.lc;
    }
    // g = clamp(f_res 2^{dc/12}) - f_res ; dg/dgamma = (ln2/12) f_h b [unclamped]
    void g(double e2, double b, double gamma, double& v, double& dv) const {
        const double f = in.fRes * e2 * (gamma != 0.0 ? std::exp2(gamma * b * (1.0 / 12.0)) : 1.0);
        if (f < in.minF) { v = in.minF - in.fRes; dv = 0.0; }
        else if (f > in.capF) { v = in.capF - in.fRes; dv = 0.0; }
        else { v = f - in.fRes; dv = kLn2Over12 * f * b; }
    }
    void setup(const SolveInputs& s) {
        in = s; ds = in.c->endSample; invD = 1.0 / ds; invFs = 1.0 / in.fs;
        h = static_cast<double>(kPitchUpdateInterval) * invFs;
        t4 = static_cast<double>(in.phi) + 4.0 * static_cast<double>(kPitchUpdateInterval);
        nodeCount = 0;
        if constexpr (kCache) {
            const auto push = [&](double n, double w) {
                const double d = in.c->template valueAt<kHoist>(n) + carry(n);
                nodes[static_cast<std::size_t>(nodeCount++)] = Node{std::exp2(d * (1.0 / 12.0)), bump(n * invD), w};
            };
            push(0.0, static_cast<double>(in.phi) * invFs);
            for (int j = 0; j < 4; ++j) push(static_cast<double>(in.phi) + j * static_cast<double>(kPitchUpdateInterval), h);
            push(t4, 0.5 * h);
            forEachNode([&](double n, double w) { push(n, w); });
            t4e = nodes[5].e; t4b = nodes[5].b;
            t4dSlope = in.c->template slopeAt<kHoist>(t4) + carrySlope(t4);
            t4bSlope = bumpSlope(t4 * invD) * invD;
        }
    }
    template <class F> void forEachNode(F&& fn) {
        panels = 0;
        const Curve& c = *in.c;
        for (int i = 0; i < c.count; ++i) {
            const Piece& q = c.p[static_cast<std::size_t>(i)];
            const double a = std::max(q.x0, t4), b = std::min(q.x1, ds);
            if (!(b > a)) continue;
            const int np = q.longPiece ? 4 : 2;
            panels += np;
            const double hp = (b - a) / np;
            for (int k = 0; k < np; ++k) {
                const double half = 0.5 * hp, mid = a + (k + 0.5) * hp;
                for (std::size_t j = 0; j < 8; ++j) fn(mid + half * kGlX[j], kGlW[j] * half * invFs);
            }
        }
    }
    // Phi_hat(gamma) and dPhi_hat/dgamma
    double predict(double gamma, double& dPhi) {
        ++evals;
        double sum = 0.0, dsum = 0.0, v, dv;
        if constexpr (kCache) {
            for (int i = 0; i < nodeCount; ++i) {
                const Node& nd = nodes[static_cast<std::size_t>(i)];
                g(nd.e, nd.b, gamma, v, dv);
                sum += nd.w * v; dsum += nd.w * dv;
            }
            // EM slope term at t4: g' (per second) = (ln2/12) f_h dc' fs when unclamped
            const double f = in.fRes * t4e * std::exp2(gamma * t4b * (1.0 / 12.0));
            if (f > in.minF && f < in.capF) {
                const double dcs = t4dSlope + gamma * t4bSlope;
                sum -= h * h / 12.0 * kLn2Over12 * f * dcs * in.fs;
                dsum -= h * h / 12.0 * kLn2Over12 * f * (kLn2Over12 * t4b * dcs + t4bSlope) * in.fs;
            }
        } else {
            const Curve& c = *in.c;
            const auto at = [&](double n, double w) {
                const double d = c.template valueAt<kHoist>(n) + carry(n);
                const double b = bump(n * invD);
                // the plain design folds gamma into the node's one exp2
                const double f = in.fRes * std::exp2((d + gamma * b) * (1.0 / 12.0));
                if (f < in.minF) { v = in.minF - in.fRes; dv = 0.0; }
                else if (f > in.capF) { v = in.capF - in.fRes; dv = 0.0; }
                else { v = f - in.fRes; dv = kLn2Over12 * f * b; }
                sum += w * v; dsum += w * dv;
            };
            at(0.0, static_cast<double>(in.phi) * invFs);
            for (int j = 0; j < 4; ++j) at(static_cast<double>(in.phi) + j * static_cast<double>(kPitchUpdateInterval), h);
            at(t4, 0.5 * h);
            forEachNode(at);
            const double d4 = c.template valueAt<kHoist>(t4) + carry(t4), b4 = bump(t4 * invD);
            const double f = in.fRes * std::exp2((d4 + gamma * b4) * (1.0 / 12.0));
            if (f > in.minF && f < in.capF) {
                const double bs = bumpSlope(t4 * invD) * invD;
                const double dcs = c.template slopeAt<kHoist>(t4) + carrySlope(t4) + gamma * bs;
                sum -= h * h / 12.0 * kLn2Over12 * f * dcs * in.fs;
                dsum -= h * h / 12.0 * kLn2Over12 * f * (kLn2Over12 * b4 * dcs + bs) * in.fs;
            }
        }
        dPhi = dsum;
        return sum;
    }
    SolveResult solve(const SolveInputs& s, bool forceCap) {
        evals = 0;
        setup(s);
        double dp0 = 0.0;
        const double p0 = predict(0.0, dp0);
        SolveResult best{};
        best.phi0 = p0;
        bool haveBest = false;
        const double fl = std::floor(p0);
        for (int ci = 0; ci < 2; ++ci) {
            const double target = fl + ci;
            double gm = 0.0, f = p0 - target, fp = dp0, lo = -kMaxCorrection, hi = kMaxCorrection;
            bool feasible = true;
            int iters = 6;
            if constexpr (kLinStart) {
                // lever (4): Newton from the linearised start gamma_0 = (n* - Phi(0)) / Phi'(0); no bracket-end passes
                // (the shared Phi(0) seeds one side of the bracket, bisection falls back on [-48, 0] or [0, 48]);
                // kLinStartPasses = 4 passes per candidate, so the forced worst case is 1 + 2 x 4 = 9 evaluations.
                // The 4-pass cap is NOT proven on the SC-013 grid here: T015 must prove it (residual per cell).
                iters = kLinStartPasses - 1;
                if (f < 0.0) lo = 0.0; else hi = 0.0;
                if (fp > 0.0) {
                    const double g0 = -f / fp;
                    if (!(g0 > lo && g0 < hi)) gm = 0.5 * (lo + hi);
                    else gm = g0;
                } else {
                    gm = 0.5 * (lo + hi);
                }
                f = predict(gm, fp) - target;
                if (f < 0.0) lo = gm; else hi = gm;
            } else {
                double dd;
                const double fLo = predict(lo, dd) - target, fHi = predict(hi, dd) - target;
                if (fLo > 0.0 || fHi < 0.0) { feasible = false; gm = fLo > 0.0 ? lo : hi; f = fLo > 0.0 ? fLo : fHi; }
                if (f < 0.0) lo = 0.0; else hi = 0.0;
            }
            for (int it = 0; it < iters; ++it) {
                if (!forceCap && (!feasible || std::abs(f) < 1e-9)) break;
                double ng = (fp > 0.0) ? gm - f / fp : 0.5 * (lo + hi);
                if (!(ng > lo && ng < hi)) ng = 0.5 * (lo + hi);
                if (feasible && std::abs(f) < 1e-9) ng = gm;   // forced passes after convergence: same cost, stay put
                gm = ng;
                f = predict(gm, fp) - target;
                if (f < 0.0) lo = gm; else hi = gm;
            }
            if (feasible && (!haveBest || std::abs(gm) < std::abs(best.gamma))) {
                best.gamma = gm; best.residual = std::abs(f); best.target = target; best.feasible = true; haveBest = true;
            } else if (!haveBest && !best.feasible) {
                best.gamma = gm; best.residual = std::abs(f); best.target = target;
            }
        }
        best.evals = evals;
        best.panels = panels;
        return best;
    }
};

double gSink = 0.0;

template <class F>
double medianOf(int trials, F&& fn) {
    std::vector<double> v;
    for (int i = 0; i < trials; ++i) v.push_back(fn());
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

constexpr double kFs = 48000.0;
constexpr int kBlock = 64;

// (A) ns per sample
template <bool kHoist>
double runA() {
    Envelope<kHoist> env;
    LockKernel lock;
    QuantizedGlide glide;
    Declick dk;
    dk.prepare(kFs);
    constexpr int kSamples = 96000 * 2;   // 4 s of audio per trial
    const int dropPeriod = static_cast<int>(kFs * 0.1), glidePeriod = static_cast<int>(kFs);
    std::array<float, kBlock> out{}, d{}, dc{}, gn{};
    double sink = 0.0;
    int up = 1;
    const auto t0 = std::chrono::steady_clock::now();
    // Both event periods are whole blocks (4800 = 75 x 64, 48000 = 750 x 64): events land at block offset 0 and are
    // dispatched per block, as a host's event list is, so no per-sample event test pollutes the kernel timing.
    static_assert(4800 % kBlock == 0 && 48000 % kBlock == 0);
    for (int base = 0; base < kSamples; base += kBlock) {
        if (base % dropPeriod == 0) {
            const Curve c = makeDropCurve(24.0, 200.0, 1.0, 1.0, kFs, false, 0.0, 0.0);
            env.trigger(c);
            lock.plan(0.37 + 1e-3 * (base / dropPeriod % 7), c.endSample, static_cast<std::uint32_t>(base / dropPeriod) & 15u);
            dk.arm();
        }
        if (base % glidePeriod == 0) { glide.start(up ? 36.0 : 60.0, up ? 60.0 : 36.0, 500.0, kFs); up ^= 1; }
        env.processBlock(d.data(), kBlock);
        lock.processBlock(d.data(), dc.data(), kBlock);
        dk.processBlock(gn.data(), kBlock);
        for (int i = 0; i < kBlock; ++i) {   // MonoHandler is per sample (processPortamento)
            const std::size_t j = static_cast<std::size_t>(i);
            out[j] = dc[j] * gn[j] + glide.process() * 1e-6f;
        }
        sink += out[kBlock - 1];
    }
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    gSink += sink + lock.acc;
    return el * 1e9 / kSamples;
}

// (A) split by component, same event schedule (diagnostic only; not part of F). part: 0 envelope, 1 lock (planned
// input a constant 7.0f), 2 quantized glide, 3 declick, 4 empty harness loop.
template <int kPart>
double runAPart() {
    Envelope<true> env;
    LockKernel lock;
    QuantizedGlide glide;
    Declick dk;
    dk.prepare(kFs);
    constexpr int kSamples = 96000 * 2;
    std::array<float, kBlock> out{}, in{};
    in.fill(7.0f);
    double sink = 0.0;
    int up = 1;
    const auto t0 = std::chrono::steady_clock::now();
    for (int base = 0; base < kSamples; base += kBlock) {
        if (base % 4800 == 0) {
            const Curve c = makeDropCurve(24.0, 200.0, 1.0, 1.0, kFs, false, 0.0, 0.0);
            env.trigger(c);
            lock.plan(0.37, c.endSample, static_cast<std::uint32_t>(base / 4800) & 15u);
            dk.arm();
        }
        if (base % 48000 == 0) { glide.start(up ? 36.0 : 60.0, up ? 60.0 : 36.0, 500.0, kFs); up ^= 1; }
        if constexpr (kPart == 0) env.processBlock(out.data(), kBlock);
        else if constexpr (kPart == 1) lock.processBlock(in.data(), out.data(), kBlock);
        else if constexpr (kPart == 3) dk.processBlock(out.data(), kBlock);
        else {
            for (int i = 0; i < kBlock; ++i)
                out[static_cast<std::size_t>(i)] = kPart == 2 ? glide.process() : static_cast<float>(i);
        }
        sink += out[kBlock - 1];
    }
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    gSink += sink + lock.acc;
    return el * 1e9 / kSamples;
}

// (B) us per worst-case trigger + solve
template <bool kHoist, bool kCache, bool kLin>
double runB(SolveResult& last) {
    const Curve prev = makeDropCurve(48.0, 20.0, 1.0, 1.0, kFs, false, 0.0, 0.0);
    const double nMid = prev.fallStart + 0.5 * prev.N + 1.0;   // the next sample after mid-fall
    const double dNow = prev.valueAt<true>(nMid), sNow = prev.slopeAt<true>(nMid);
    const double gPrev = 2.5, invDPrev = 1.0 / prev.endSample;
    const double c0 = gPrev * bump(nMid * invDPrev), c0s = gPrev * bumpSlope(nMid * invDPrev) * invDPrev;
    Solver<kHoist, kCache, kLin> solver;
    constexpr int kSolves = 2000;
    double sink = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kSolves; ++i) {
        const Curve c = makeDropCurve(48.0, 1.0, 1.0, 1.0, kFs, true, dNow, sNow);
        SolveInputs si;
        si.c = &c; si.fRes = 41.203444614108741 * (1.0 + 1e-9 * (i & 7)); si.fs = kFs; si.minF = 8.0; si.capF = 0.8 * kFs * 0.5;
        si.phi = static_cast<std::uint32_t>(i) & 15u; si.c0 = c0; si.c0s = c0s; si.lc = c.fallStart;
        last = solver.solve(si, true);
        sink += last.gamma;
    }
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    gSink += sink;
    return el * 1e6 / kSolves;
}

// (C) us per typical trigger + solve
template <bool kHoist, bool kCache, bool kLin>
double runC(SolveResult& last) {
    Solver<kHoist, kCache, kLin> solver;
    constexpr int kSolves = 2000;
    double sink = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kSolves; ++i) {
        const Curve c = makeDropCurve(24.0, 200.0, 1.0 / 3.0, 0.0, kFs, false, 0.0, 0.0);
        SolveInputs si;
        si.c = &c; si.fRes = 41.203444614108741 * (1.0 + 1e-9 * (i & 7)); si.fs = kFs; si.minF = 8.0; si.capF = 0.8 * kFs * 0.5;
        si.phi = static_cast<std::uint32_t>(i) & 15u;
        last = solver.solve(si, false);
        sink += last.gamma;
    }
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    gSink += sink;
    return el * 1e6 / kSolves;
}

template <bool kHoist, bool kCache, bool kLin>
void runStage(int stage) {
    constexpr int kTrials = 9;
    SolveResult rb{}, rc{};
    // warm-up
    runA<kHoist>(); runB<kHoist, kCache, kLin>(rb); runC<kHoist, kCache, kLin>(rc);
    const double nsA = medianOf(kTrials, [] { return runA<kHoist>(); });
    const double usB = medianOf(kTrials, [&] { return runB<kHoist, kCache, kLin>(rb); });
    const double usC = medianOf(kTrials, [&] { return runC<kHoist, kCache, kLin>(rc); });
    const double F = (nsA * 48000.0 + 10.0 * usB * 1000.0) / 1e9;
    std::printf("stage %d | A %.3f ns/sample | B %.3f us/solve (evals %d, panels/eval %d, Phi0 %.6f, n* %.0f, gamma %.6f, "
                "resid %.2e, feasible %d) | C %.3f us/solve (evals %d, panels/eval %d, gamma %.6f, resid %.2e) | "
                "F = %.5f %% (A %.5f %% + B %.5f %%)\n",
                stage, nsA, usB, rb.evals, rb.panels, rb.phi0, rb.target, rb.gamma, rb.residual, rb.feasible ? 1 : 0, usC,
                rc.evals, rc.panels, rc.gamma, rc.residual, F * 100.0, nsA * 48000.0 / 1e9 * 100.0,
                10.0 * usB * 1000.0 / 1e9 * 100.0);
}

}   // namespace

int main(int argc, char** argv) {
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    const int stage = argc > 1 ? std::atoi(argv[1]) : 0;
    if (argc > 2) {   // "<stage> breakdown": per-component split of (A), hoisted kernel, diagnostic only
        const auto m = [](auto fn) { return medianOf(9, fn); };
        std::printf("A breakdown (ns/sample): envelope %.3f | lock+mirror %.3f | quantized glide %.3f | declick %.3f | "
                    "empty loop %.3f\n",
                    m([] { return runAPart<0>(); }), m([] { return runAPart<1>(); }), m([] { return runAPart<2>(); }),
                    m([] { return runAPart<3>(); }), m([] { return runAPart<4>(); }));
    }
    switch (stage) {
        case 0: runStage<false, false, false>(0); break;
        case 1: runStage<true, false, false>(1); break;
        case 2: runStage<true, true, false>(2); break;
        default: runStage<true, true, true>(3); break;
    }
    std::printf("(sink %g)\n", gSink);
    return 0;
}
