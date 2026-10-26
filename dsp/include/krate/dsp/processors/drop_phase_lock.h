// ==============================================================================
// Layer 2: DSP Processor - Profundum Phase 4 (specs/profundum-phase4-drop-note-behaviour)
// ==============================================================================
// DropPhaseLock: the post-Drop landing correction that lands the core h1 phase
// on the grid. Includes Layer 0 only.
//   Spec:  specs/profundum-phase4-drop-note-behaviour/spec.md (FR-053-FR-059)
//   Plan:  specs/profundum-phase4-drop-note-behaviour/plan.md (S5, S11.1)
//   Carries: SC-013, SC-024(b), SC-025, SC-026, SC-017, SC-019.
//
// Per sample: d_c[n] = d[n] + gamma * b(n / D_s) (+ the bridged carry, S5.5), with
// b(x) = 16 x^2 (1 - x)^2 and D_s the planned curve's end (S5.2). From D_s on the
// output is exactly the planned offset (0.0f there).
//
// The solve (S5.3, S5.4) is lazy: trigger() latches its arguments into a pending
// slot (last call wins) and the first process()/processBlock() after it runs one
// solve: 1 shared prediction, then per candidate (floor / ceil of the prediction,
// C-4) 2 bracket evaluations at +-48 st (target-independent: computed once and shared
// by both candidates) and at most 6 safeguarded Newton steps.
// The prediction mirrors the core's 16-sample staircase and its [minF, cap]
// clamp (C-3): the first 4 pitch updates exactly, the remainder as fixed-panel
// Gauss-Legendre quadrature of the latched curve (the panel layout of
// DropCurve::integrate) plus Euler-Maclaurin endpoint terms. On a bridged curve
// the exact updates also cover every update inside the bridge (brake + rise,
// [0, fallStart)) and the 4 after it: the rise climbs up to A + rho_R + 2.7 st in
// L_min samples, far too steep for the two-term Euler-Maclaurin correction at t4
// (measured: up to 9.9 deg landing error at T = 1 ms, 1.09 deg at T = 200 ms,
// before this rule). The bridge's pieces then contribute no quadrature nodes.
//
// T005 lever (3): the quadrature nodes, 2^{(d + carry)/12} and b are cached once
// per solve; each evaluation costs one exp2(gamma*b/12) per node.
//
// Rulings 2026-10-12 (O-1, O-4): gamma_lim = 48 st constant, no slope cap;
// landingFeasible() = a root exists in [-48, 48] for at least one candidate.
//
// Trigger kinds (S5.5, FR-059): SnapReset zeroes the staircase mirror and lands
// Phi_hat alone; SnapFreeRunning and Bridged keep it and land (accumulated +
// Phi_hat) on an integer. Bridged also continues the running correction's
// (c0, c0') at the next sample by a Hermite carry to (0, 0) over the bridge's
// brake + rise (curve.fallStart samples), included in the predictor's integrand.
//
// Staircase mirror (S5.6): from the first trigger after prepare/reset on, every
// processed sample counts (f_h - f_res)/fs in double, with f_h = clamp(f_res *
// 2^{d_c/12}) recomputed at the trigger sample and at every sample with
// (n - gridPhase) % 16 == 0 (one exp2 per hold, T005 ruling). A non-finite
// input keeps f_h from the last finite update (deviation 7). The accumulation is
// per hold (T005 ruling (ii), applied by T023 epoch 6): a hold's samples are
// counted and folded in once, as count * (f_h - f_res)/fs, when the next hold
// starts; the per-sample loop never touches the mirror.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/drop_curve.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

class DropPhaseLock {
public:
    enum class TriggerKind : std::uint8_t { SnapReset, SnapFreeRunning, Bridged };

    static constexpr std::size_t kPitchUpdateInterval = 16;      // == ProfundumCore / AnchorOscillator (C-14)
    static constexpr int kExactLeadUpdates = 4;                   // C-3
    static constexpr double kMaxCorrectionSemitones = 48.0;       // gamma_lim (C-4; O-1 / O-4 rulings)
    static constexpr double kBumpSlopePeak = 3.0792014356780038;  // max b'(x), b = 16x^2(1-x)^2, at x = (3-sqrt3)/6
    static constexpr int kMaxSolveEvaluations = 8;                // per candidate, incl. 2 bracket ends (S5.4)
    static constexpr double kSolveTolerance = 1e-9;               // cycles: early stop (S5.4)
    /// Illinois false-position evaluations per candidate after the Newton budget, run only when it
    /// ends above kContinuationThresholdCycles (T023: E0, A 48, T 10 ms, R 1 stopped at 0.0098 /
    /// 0.066 cycles = 3.8 / 23.5 deg). Below the threshold (1e-6 cycles = 0.00036 deg) the residual is
    /// the float floor of the exact lead updates (~1e-8), which more passes cannot lower.
    static constexpr int kMaxContinuationEvaluations = 24;
    static constexpr double kContinuationThresholdCycles = 1e-6;
    static constexpr double kContinuationMinWidth = 1e-9;  // st: the bracket has collapsed
    static constexpr double kMinSampleRate = 22050.0;
    static constexpr double kMaxSampleRate = 192000.0;

    // Aliases of the Layer 0 definitions (plan S1 C-12); not second definitions.
    static constexpr double kExpRate = DropCurve::kExpRate;
    static constexpr double kLandingFilletFraction = DropCurve::kLandingFilletFraction;

    /// Clamp the rate to [22050, 192000], then reset(). A non-finite rate is a no-op
    /// (tasks.md deviation 7).
    void prepare(double sampleRate) noexcept {
        if (!detail::isFinite(sampleRate))
            return;
        sampleRate_ = std::clamp(sampleRate, kMinSampleRate, kMaxSampleRate);
        prepared_ = true;
        reset();
    }

    /// Accumulator 0, idle, no plan, no pending trigger.
    void reset() noexcept {
        pending_ = false;
        active_ = false;
        counter_ = 0;
        gamma_ = 0.0;
        residual_ = 0.0;
        predicted_ = 0.0;
        target_ = 0.0;
        feasible_ = true;
        clearCarry();
        clearMirror();
    }

    /// FR-058: FOLLOW. While bypassed no plan is made, c = 0 and process() returns its input
    /// bitwise. Entering bypass drops the running plan, any pending trigger and the staircase
    /// mirror (FOLLOW has no landing to keep; the next trigger after leaving bypass restarts it).
    void setBypass(bool follow) noexcept {
        bypass_ = follow;
        if (follow) {
            pending_ = false;
            active_ = false;
            gamma_ = 0.0;
            clearCarry();
            clearMirror();
        }
    }

    /// Latch the planned curve and the core's numbers; the solve runs at the next process call
    /// (S5.4). The last call before a process call wins. Ignored before prepare, in bypass, and
    /// for non-finite or non-positive fResHz / minF0Hz / capHz (deviation 7) and for capHz < minF0Hz
    /// (an empty clamp range).
    void trigger(const DropCurve& curve, float fResHz, std::size_t gridPhase, float minF0Hz, float capHz,
                 TriggerKind kind) noexcept {
        if (!prepared_ || bypass_)
            return;
        if (!validPositive(fResHz) || !validPositive(minF0Hz) || !validPositive(capHz) || capHz < minF0Hz)
            return;
        pendingCurve_ = curve;
        pendingFRes_ = fResHz;
        pendingGridPhase_ = gridPhase;
        pendingMinF_ = minF0Hz;
        pendingCap_ = capHz;
        pendingKind_ = kind;
        pending_ = true;
    }

    /// d_c for the planned offset d of this sample; runs a pending solve first.
    [[nodiscard]] float process(float plannedOffset) noexcept {
        if (!prepared_ || bypass_)
            return plannedOffset;
        if (pending_)
            solve();
        float out = 0.0f;
        run(&plannedOffset, &out, 1);
        return out;
    }

    /// n samples (in == out allowed). A null pointer or n = 0 is a no-op: a pending solve stays
    /// pending.
    void processBlock(const float* in, float* out, std::size_t n) noexcept {
        if (in == nullptr || out == nullptr || n == 0)
            return;
        if (!prepared_ || bypass_) {
            if (in != out) {
                for (std::size_t i = 0; i < n; ++i)
                    out[i] = in[i];
            }
            return;
        }
        if (pending_)
            solve();
        run(in, out, n);
    }

    /// gamma (st) of the last completed solve.
    [[nodiscard]] double correctionGamma() const noexcept { return gamma_; }
    /// |gamma| + the carry peak |c0| + |c0'| L 4/27 (S5.5); a diagnostic, NOT SC-013(c)'s gate (C-5).
    [[nodiscard]] double correctionPeakBound() const noexcept {
        return std::abs(gamma_) + std::abs(carryC0_) + std::abs(carrySlope_) * carryLen_ * (4.0 / 27.0);
    }
    /// |Phi_hat(gamma) - n*| at the returned gamma (S5.4).
    [[nodiscard]] double solveResidualCycles() const noexcept { return residual_; }
    /// Phi_hat before correction (gamma = 0).
    [[nodiscard]] double predictedExcessCycles() const noexcept { return predicted_; }
    /// The chosen landing integer (C-4); for SnapFreeRunning / Bridged it counts the accumulated
    /// excess before the trigger as well (S5.5).
    [[nodiscard]] double targetCycles() const noexcept { return target_; }
    /// A root exists in [-48, 48] for at least one candidate (O-1 / O-4 rulings).
    [[nodiscard]] bool landingFeasible() const noexcept { return feasible_; }
    /// Staircase mirror of the core's h1 excess in cycles (S5.6): zeroed by SnapReset, kept by
    /// SnapFreeRunning and Bridged.
    [[nodiscard]] double accumulatedExcessCycles() const noexcept {
        return accumulated_ + static_cast<double>(mirrorHoldCount_) * mirrorIncrement_;
    }

    /// Predicted excess phase in cycles of the core's h1 over the planned curve plus
    /// gamma * b(n / bumpLen) plus the Hermite carry from (carryAt0, carrySlope) to (0, 0) over
    /// carryLen samples (S5.3). updateInterval U (16 in the core, 1 for SC-024), gridPhase =
    /// samples until the core's next pitch update, [minF, capF] the core's clamp.
    [[nodiscard]] static double predictExcess(const DropCurve& curve, double fRes, double fs,
                                              std::size_t updateInterval, std::size_t gridPhase, double minF,
                                              double capF, double gamma, double bumpLen, double carryAt0,
                                              double carrySlope, double carryLen) noexcept {
        PredictorCache cache;
        buildCache(cache, curve, fRes, fs, updateInterval, gridPhase, minF, capF, bumpLen, carryAt0, carrySlope,
                   carryLen);
        return evaluate(cache, gamma).phi;
    }

private:
    static constexpr double kLn2Over12 = 0.057762265046662109;  // ln(2) / 12
    static constexpr std::size_t kMaxNodes =
        static_cast<std::size_t>(DropCurve::kMaxPieces) * 4 * DropCurve::kGLNodes.size();
    /// Exact lead updates at U = 1: the 4 of C-3 plus the longest bridge, brake + rise
    /// <= 2 L_min = 2 * round(kDropBridgeMs * kMaxSampleRate / 1000) samples.
    static constexpr std::size_t kMaxLeadUpdates =
        static_cast<std::size_t>(kExactLeadUpdates) +
        2 * static_cast<std::size_t>(DropCurve::kDropBridgeMs * kMaxSampleRate / 1000.0);

    /// Everything one evaluation needs that does not depend on gamma (T005 lever (3)).
    struct PredictorCache {
        double fRes = 0.0, minF = 0.0, capF = 0.0, invFs = 0.0, U = 1.0;
        double holdBefore = 0.0;  // phi * g(0): the noteOn f0 held until the first grid sample
        int leadCount = 0;
        std::array<double, kMaxLeadUpdates> leadBase{}, leadB{};  // double(float d) + carry, b
        double emRatio = 1.0, emB = 0.0, emSlope = 0.0, emBSlope = 0.0;  // at t4 (S5.3)
        std::size_t nodeCount = 0;
        std::array<double, kMaxNodes> nodeW{}, nodeRatio{}, nodeB{};
    };

    struct Evaluation {
        double phi = 0.0;    // cycles
        double dPhi = 0.0;   // cycles per semitone of gamma
    };

    struct CandidateResult {
        double gamma = 0.0;
        double residual = 0.0;
        bool feasible = true;
    };

    [[nodiscard]] static bool validPositive(float v) noexcept { return detail::isFinite(v) && v > 0.0f; }

    /// b(x) = 16 x^2 (1 - x)^2 on (0, 1), else 0 (Horner: x^2 (16 + x (-32 + 16 x))).
    [[nodiscard]] static double bump(double x) noexcept {
        if (!(x > 0.0 && x < 1.0))
            return 0.0;
        return x * x * (16.0 + x * (-32.0 + 16.0 * x));
    }
    /// b'(x) = 32 x (1 - x)(1 - 2x) on (0, 1), else 0.
    [[nodiscard]] static double bumpSlope(double x) noexcept {
        if (!(x > 0.0 && x < 1.0))
            return 0.0;
        return 32.0 * x * (1.0 - x) * (1.0 - 2.0 * x);
    }

    /// Hermite carry from (c0, c0') to (0, 0) over [0, len] samples (S5.5); 0 outside.
    [[nodiscard]] static double carryAt(double n, double c0, double c0Slope, double len) noexcept {
        if (!(len > 0.0) || !(n >= 0.0 && n < len))
            return 0.0;
        const double t = n / len;
        const double h00 = (2.0 * t - 3.0) * t * t + 1.0;
        const double h10 = ((t - 2.0) * t + 1.0) * t;
        return c0 * h00 + c0Slope * len * h10;
    }
    [[nodiscard]] static double carrySlopeAt(double n, double c0, double c0Slope, double len) noexcept {
        if (!(len > 0.0) || !(n >= 0.0 && n < len))
            return 0.0;
        const double t = n / len;
        return (c0 * (6.0 * t * t - 6.0 * t) + c0Slope * len * ((3.0 * t - 4.0) * t + 1.0)) / len;
    }

    /// The core's held f0 for a float semitone offset (mirrors static_cast<float> of f_core and the
    /// trajectoryF0 clamp).
    [[nodiscard]] static float heldF0(double fRes, float dc, double minF, double capF) noexcept {
        const auto f = static_cast<double>(static_cast<float>(fRes * std::exp2(static_cast<double>(dc) / 12.0)));
        return static_cast<float>(std::clamp(f, minF, capF));
    }

    static void buildCache(PredictorCache& c, const DropCurve& curve, double fRes, double fs,
                           std::size_t updateInterval, std::size_t gridPhase, double minF, double capF,
                           double bumpLen, double carryAt0, double carrySlope, double carryLen) noexcept {
        const std::size_t U = updateInterval == 0 ? 1 : updateInterval;
        const double phi = static_cast<double>(gridPhase % U);
        const double invD = bumpLen > 0.0 ? 1.0 / bumpLen : 0.0;
        const auto bAt = [&](double n) { return invD > 0.0 ? bump(n * invD) : 0.0; };
        const auto dPlusCarry = [&](double n) {
            return curve.valueAt(n) + carryAt(n, carryAt0, carrySlope, carryLen);
        };
        c.fRes = fRes;
        c.minF = minF;
        c.capF = capF;
        c.invFs = 1.0 / fs;
        c.U = static_cast<double>(U);

        // Samples [0, phi): the noteOn f0 (d_c[0]; b(0) = 0, so gamma-independent).
        {
            const auto d0 = static_cast<double>(static_cast<float>(curve.valueAt(0.0)));
            const auto dc0 = static_cast<float>(d0 + carryAt(0.0, carryAt0, carrySlope, carryLen));
            c.holdBefore = phi * (static_cast<double>(heldF0(fRes, dc0, minF, capF)) - fRes);
        }

        // The updates inside the bridge [0, fallStart) and the kExactLeadUpdates after it,
        // summed exactly (C-3; a snap curve has fallStart 0, so exactly kExactLeadUpdates).
        {
            const double bridgeUpdates = curve.fallStart > phi ? std::ceil((curve.fallStart - phi) / c.U) : 0.0;
            c.leadCount = static_cast<int>(std::min(static_cast<double>(kMaxLeadUpdates),
                                                    bridgeUpdates + static_cast<double>(kExactLeadUpdates)));
        }
        for (int j = 0; j < c.leadCount; ++j) {
            const double n = phi + static_cast<double>(j) * c.U;
            const auto dFloat = static_cast<double>(static_cast<float>(curve.valueAt(n)));
            c.leadBase[static_cast<std::size_t>(j)] = dFloat + carryAt(n, carryAt0, carrySlope, carryLen);
            c.leadB[static_cast<std::size_t>(j)] = bAt(n);
        }

        // Remainder from t4 (the first update not summed exactly): GL8 nodes in
        // DropCurve::integrate's panel layout (C-2), cached.
        const double t4 = phi + static_cast<double>(c.leadCount) * c.U;
        c.emRatio = std::exp2(dPlusCarry(t4) / 12.0);
        c.emB = bAt(t4);
        c.emSlope = curve.slopeAt(t4) + carrySlopeAt(t4, carryAt0, carrySlope, carryLen);
        c.emBSlope = invD > 0.0 ? bumpSlope(t4 * invD) * invD : 0.0;

        c.nodeCount = 0;
        for (int i = 0; i < curve.numPieces; ++i) {
            const DropCurve::Piece& p = curve.pieces[static_cast<std::size_t>(i)];
            const double a = std::max(p.start, t4);
            const double bEnd = p.end;
            if (!(bEnd > a))
                continue;
            const int panels = (bEnd - a) > 0.5 * curve.fallSamples ? 4 : 2;
            const double h = (bEnd - a) / static_cast<double>(panels);
            const double r = 0.5 * h;
            for (int k = 0; k < panels; ++k) {
                const double centre = a + (static_cast<double>(k) + 0.5) * h;
                for (std::size_t j = 0; j < DropCurve::kGLNodes.size(); ++j) {
                    if (c.nodeCount >= kMaxNodes)
                        return;
                    const double x = centre + r * DropCurve::kGLNodes[j];
                    const double d = curve.evalPiece(i, x) + carryAt(x, carryAt0, carrySlope, carryLen);
                    c.nodeW[c.nodeCount] = DropCurve::kGLWeights[j] * r;
                    c.nodeRatio[c.nodeCount] = std::exp2(d / 12.0);
                    c.nodeB[c.nodeCount] = bAt(x);
                    ++c.nodeCount;
                }
            }
        }
    }

    /// One S5.3 pass: Phi_hat(gamma) and dPhi_hat/dgamma (the integrand's unclamped part).
    [[nodiscard]] static Evaluation evaluate(const PredictorCache& c, double gamma) noexcept {
        double sum = c.holdBefore;
        double dSum = 0.0;
        // Exact lead updates (float path, as the core sees them).
        for (int j = 0; j < c.leadCount; ++j) {
            const double b = c.leadB[static_cast<std::size_t>(j)];
            const auto dc = static_cast<float>(c.leadBase[static_cast<std::size_t>(j)] + gamma * b);
            const auto f = static_cast<double>(static_cast<float>(c.fRes * std::exp2(static_cast<double>(dc) / 12.0)));
            const double fh = std::clamp(f, c.minF, c.capF);
            sum += c.U * (static_cast<double>(static_cast<float>(fh)) - c.fRes);
            if (f > c.minF && f < c.capF)
                dSum += c.U * kLn2Over12 * b * f;
        }
        // Quadrature of the clamped remainder.
        for (std::size_t k = 0; k < c.nodeCount; ++k) {
            const double b = c.nodeB[k];
            double f = c.fRes * c.nodeRatio[k];
            if (b != 0.0 && gamma != 0.0)
                f *= std::exp2(gamma * b / 12.0);
            const double fh = std::clamp(f, c.minF, c.capF);
            sum += c.nodeW[k] * (fh - c.fRes);
            if (f > c.minF && f < c.capF)
                dSum += c.nodeW[k] * kLn2Over12 * b * f;
        }
        // Euler-Maclaurin endpoint terms of the left-Riemann hold at t4 (the far end has g = g' = 0).
        {
            double f = c.fRes * c.emRatio;
            if (c.emB != 0.0 && gamma != 0.0)
                f *= std::exp2(gamma * c.emB / 12.0);
            const double fh = std::clamp(f, c.minF, c.capF);
            const bool unclamped = f > c.minF && f < c.capF;
            const double gPrime = unclamped ? kLn2Over12 * f * (c.emSlope + gamma * c.emBSlope) : 0.0;
            sum += 0.5 * c.U * (fh - c.fRes) - (c.U * c.U / 12.0) * gPrime;
            if (unclamped)
                dSum += 0.5 * c.U * kLn2Over12 * c.emB * f;
        }
        return {sum * c.invFs, dSum * c.invFs};
    }

    /// Phi_hat at the bracket ends -48 / +48 st: target-independent, so evaluated at most once per
    /// solve and shared by both candidates (SC-019; the values are those of the per-candidate passes).
    struct BracketEnds {
        double lo = 0.0, hi = 0.0;
        bool haveLo = false, haveHi = false;
    };

    /// Solve Phi_hat(gamma) = target on [-48, 48] (S5.4): 2 bracket evaluations, then at most
    /// kMaxSolveEvaluations - 2 safeguarded Newton steps from the linearised start. A candidate
    /// still above kContinuationThresholdCycles then continues by Illinois false position on the
    /// kept bracket (at most kMaxContinuationEvaluations), keeping the best point; any other
    /// candidate never reaches it, so its result is unchanged.
    [[nodiscard]] CandidateResult solveCandidate(double target, const Evaluation& at0,
                                                 BracketEnds& ends) const noexcept {
        constexpr double lim = kMaxCorrectionSemitones;
        const double r0 = at0.phi - target;
        if (std::abs(r0) < kSolveTolerance)
            return {0.0, std::abs(r0), true};
        if (!ends.haveLo) {
            ends.lo = evaluate(cache_, -lim).phi;
            ends.haveLo = true;
        }
        const double fLo = ends.lo - target;
        if (fLo > 0.0)
            return {-lim, fLo, false};  // the target lies below every reachable value
        if (!ends.haveHi) {
            ends.hi = evaluate(cache_, lim).phi;
            ends.haveHi = true;
        }
        const double fHi = ends.hi - target;
        if (fHi < 0.0)
            return {lim, -fHi, false};  // the target lies above every reachable value
        if (std::abs(fLo) < kSolveTolerance)
            return {-lim, std::abs(fLo), true};
        if (std::abs(fHi) < kSolveTolerance)
            return {lim, std::abs(fHi), true};

        double lo = -lim;
        double hi = lim;
        double fAtLo = fLo;  // F at the bracket ends: < 0 at lo, > 0 at hi
        double fAtHi = fHi;
        double g = at0.dPhi > 0.0 ? -r0 / at0.dPhi : 0.5 * (lo + hi);
        if (!(g > lo && g < hi))
            g = 0.5 * (lo + hi);
        double residual = std::abs(r0);
        double fLast = r0;
        for (int it = 0; it < kMaxSolveEvaluations - 2; ++it) {
            const Evaluation e = evaluate(cache_, g);
            const double F = e.phi - target;
            fLast = F;
            residual = std::abs(F);
            if (residual < kSolveTolerance || it == kMaxSolveEvaluations - 3)
                break;
            if (F < 0.0) {
                lo = g;
                fAtLo = F;
            } else {
                hi = g;
                fAtHi = F;
            }
            double next = e.dPhi > 0.0 ? g - F / e.dPhi : 0.5 * (lo + hi);
            if (!(next > lo && next < hi))
                next = 0.5 * (lo + hi);
            g = next;
        }
        if (!(residual > kContinuationThresholdCycles))
            return {g, residual, true};

        // Continuation (Illinois): fold the last Newton point into the bracket, then false position
        // with the stale end's F halved after two same-side steps. A flat stretch of the clamp
        // (f <= minF or >= cap) keeps it bracketed; it never leaves (lo, hi).
        if (fLast < 0.0) {
            lo = g;
            fAtLo = fLast;
        } else {
            hi = g;
            fAtHi = fLast;
        }
        int side = 0;
        for (int it = 0; it < kMaxContinuationEvaluations; ++it) {
            double next = (lo * fAtHi - hi * fAtLo) / (fAtHi - fAtLo);
            if (!(next > lo && next < hi))
                next = 0.5 * (lo + hi);
            const double F = evaluate(cache_, next).phi - target;
            if (std::abs(F) < residual) {
                g = next;
                residual = std::abs(F);
            }
            if (residual < kSolveTolerance || !(hi - lo > kContinuationMinWidth))
                break;
            if (F < 0.0) {
                lo = next;
                fAtLo = F;
                if (side < 0)
                    fAtHi *= 0.5;
                side = -1;
            } else {
                hi = next;
                fAtHi = F;
                if (side > 0)
                    fAtLo *= 0.5;
                side = 1;
            }
        }
        return {g, residual, true};
    }

    /// The running correction gamma * b(n / D_s) + carry(n) and its slope (st per sample) at the
    /// next sample: the bridged carry's start (S5.5); 0 once landed or with no plan.
    [[nodiscard]] double runningCorrection() const noexcept {
        if (!active_)
            return 0.0;
        const auto n = static_cast<double>(counter_);
        return gamma_ * bump(n * invD_) + carryAt(n, carryC0_, carrySlope_, carryLen_);
    }
    [[nodiscard]] double runningCorrectionSlope() const noexcept {
        if (!active_)
            return 0.0;
        const auto n = static_cast<double>(counter_);
        return gamma_ * bumpSlope(n * invD_) * invD_ + carrySlopeAt(n, carryC0_, carrySlope_, carryLen_);
    }

    void clearCarry() noexcept {
        carryC0_ = 0.0;
        carrySlope_ = 0.0;
        carryLen_ = 0.0;
    }

    void clearMirror() noexcept {
        mirrorActive_ = false;
        mirrorFresh_ = false;
        mirrorCountdown_ = 0;
        mirrorIncrement_ = 0.0;
        mirrorHoldCount_ = 0;
        accumulated_ = 0.0;
    }

    void solve() noexcept {
        pending_ = false;
        const TriggerKind kind = pendingKind_;

        // S5.5: the bridged carry starts from the running correction at this (the next) sample,
        // read before the old plan is replaced, and ends at (0, 0) after the bridge's brake + rise.
        // A curve without a bridge (fallStart 0) carries nothing.
        double c0 = 0.0;
        double c0Slope = 0.0;
        double carryLen = 0.0;
        if (kind == TriggerKind::Bridged && pendingCurve_.fallStart > 0.0) {
            c0 = runningCorrection();
            c0Slope = runningCorrectionSlope();
            if (c0 != 0.0 || c0Slope != 0.0)
                carryLen = pendingCurve_.fallStart;
        }
        carryC0_ = carryLen > 0.0 ? c0 : 0.0;
        carrySlope_ = carryLen > 0.0 ? c0Slope : 0.0;
        carryLen_ = carryLen;

        // S5.5 accumulator: only SnapReset zeroes it. S5.6: the mirror restarts its hold here (the
        // running hold is folded in first; its increment stays until a finite refresh).
        accumulated_ = kind == TriggerKind::SnapReset ? 0.0 : accumulatedExcessCycles();
        mirrorHoldCount_ = 0;
        const double offset = accumulated_;
        fRes_ = static_cast<double>(pendingFRes_);
        minF_ = static_cast<double>(pendingMinF_);
        capF_ = static_cast<double>(pendingCap_);
        mirrorActive_ = true;
        mirrorFresh_ = true;
        mirrorCountdown_ = pendingGridPhase_ % kPitchUpdateInterval;

        curve_ = pendingCurve_;
        const double D = curve_.endSample;
        invD_ = D > 0.0 ? 1.0 / D : 0.0;
        counter_ = 0;
        buildCache(cache_, curve_, fRes_, sampleRate_, kPitchUpdateInterval, pendingGridPhase_, minF_, capF_, D,
                   carryC0_, carrySlope_, carryLen_);
        const Evaluation at0 = evaluate(cache_, 0.0);
        predicted_ = at0.phi;

        // C-4 on the total (kept accumulator + this trajectory): its neighbouring integers.
        const double total0 = offset + at0.phi;
        const double nLo = std::floor(total0);
        const double nHi = std::ceil(total0);
        BracketEnds ends;
        const CandidateResult a = solveCandidate(nLo - offset, at0, ends);
        CandidateResult best = a;
        double bestTarget = nLo;
        if (nHi != nLo) {
            const CandidateResult b = solveCandidate(nHi - offset, at0, ends);
            const bool takeB = (a.feasible != b.feasible) ? b.feasible
                                                          : (a.feasible ? std::abs(b.gamma) < std::abs(a.gamma)
                                                                        : b.residual < a.residual);
            if (takeB) {
                best = b;
                bestTarget = nHi;
            }
        }
        gamma_ = best.gamma;
        residual_ = best.residual;
        target_ = bestTarget;
        feasible_ = best.feasible;
        active_ = invD_ > 0.0 && (gamma_ != 0.0 || carryLen_ > 0.0);
    }

    /// n samples of the correction and the staircase mirror (in == out allowed; prepared, not
    /// bypassed, no pending solve). The running state is held in locals for the loop (SC-019:
    /// stores to out cannot alias it) and written back once.
    ///   S5.2: d_c = d + gamma * b(n / D_s) + carry(n) while n < D_s; the input unchanged otherwise.
    ///   S5.6: the mirror runs first, over the hold starts only (the trigger sample and every sample
    ///   with (n - gridPhase) % 16 == 0), reading in[] before an in-place loop overwrites it; each
    ///   hold start recomputes that sample's d_c exactly as the loop below does. A non-finite input
    ///   keeps the previous hold (deviation 7). Idle until the first trigger after prepare/reset.
    void run(const float* in, float* out, std::size_t n) noexcept {
        const std::uint64_t counter0 = counter_;
        const bool active0 = active_;
        const double invD = invD_;
        const double gamma = gamma_;
        const double carryC0 = carryC0_;
        const double carrySlope = carrySlope_;
        const double carryLen = carryLen_;

        if (mirrorActive_) {
            bool fresh = mirrorFresh_;
            std::size_t countdown = mirrorCountdown_;
            double increment = mirrorIncrement_;
            std::uint64_t holdCount = mirrorHoldCount_;
            double accumulated = accumulated_;
            std::size_t i = 0;
            while (i < n) {
                if (fresh || countdown == 0) {
                    const float x = in[i];
                    if (detail::isFinite(x)) {
                        double c = 0.0;
                        if (active0) {
                            const auto k = static_cast<double>(static_cast<std::int64_t>(counter0 + i));
                            const double u = k * invD;
                            if (u < 1.0)
                                c = gamma * bump(u) + carryAt(k, carryC0, carrySlope, carryLen);
                        }
                        const float y = c != 0.0 ? static_cast<float>(static_cast<double>(x) + c) : x;
                        accumulated += static_cast<double>(holdCount) * increment;
                        holdCount = 0;
                        increment = (static_cast<double>(heldF0(fRes_, y, minF_, capF_)) - fRes_) / sampleRate_;
                    }
                    if (countdown == 0)
                        countdown = kPitchUpdateInterval;
                    fresh = false;
                }
                const std::size_t span = std::min(countdown, n - i);
                holdCount += span;
                countdown -= span;
                i += span;
            }
            mirrorFresh_ = fresh;
            mirrorCountdown_ = countdown;
            mirrorIncrement_ = increment;
            mirrorHoldCount_ = holdCount;
            accumulated_ = accumulated;
        }

        // SC-019 (T023 epoch 7): three segments, each with its own branch-free body -- the carry
        // window [0, carryLen), the bump alone up to D_s, then the planned offset unchanged. Same
        // arithmetic per sample as the single loop it replaces: past carryLen carryAt is exactly 0,
        // and on u in [0, 1) the bump's range test is the polynomial's own value at u = 0.
        bool active = active0;
        std::uint64_t counter = counter0;
        std::size_t i = 0;
        // counter < 2^63: the signed conversion is exact and cheaper than the unsigned one.
        for (; active && i < n; ++i) {
            const auto k = static_cast<double>(static_cast<std::int64_t>(counter));
            if (!(k < carryLen))
                break;
            const double u = k * invD;
            ++counter;
            double c = 0.0;
            if (u < 1.0)
                c = gamma * bump(u) + carryAt(k, carryC0, carrySlope, carryLen);
            else
                active = false;  // landed: exactly the planned offset from D_s on
            out[i] = correctedOffset(in[i], c);
        }
        for (; active && i < n; ++i) {
            const auto k = static_cast<double>(static_cast<std::int64_t>(counter));
            const double u = k * invD;
            ++counter;
            if (!(u < 1.0)) {
                active = false;  // landed: exactly the planned offset from D_s on
                out[i] = correctedOffset(in[i], 0.0);
                continue;        // ++i, then the loop ends
            }
            out[i] = correctedOffset(in[i], gamma * (u * u * (16.0 + u * (-32.0 + 16.0 * u))));
        }
        for (; i < n; ++i)  // inactive (or no samples left)
            out[i] = correctedOffset(in[i], 0.0);
        active_ = active;
        counter_ = counter;
    }

    /// One output sample: the input bitwise when c == 0 (b(0) = 0, after landing, no plan), 0 for a
    /// non-finite input (deviation 7), else float(x + c).
    [[nodiscard]] static float correctedOffset(float x, double c) noexcept {
        if (!detail::isFinite(x))
            return 0.0f;
        return c != 0.0 ? static_cast<float>(static_cast<double>(x) + c) : x;
    }

    PredictorCache cache_{};
    DropCurve curve_{};
    DropCurve pendingCurve_{};
    double sampleRate_ = 48000.0;
    double invD_ = 0.0;
    double gamma_ = 0.0;
    double residual_ = 0.0;
    double predicted_ = 0.0;
    double target_ = 0.0;
    double accumulated_ = 0.0;
    double carryC0_ = 0.0;  // S5.5 bridged carry: start value (st), slope (st per sample), length (samples)
    double carrySlope_ = 0.0;
    double carryLen_ = 0.0;
    double fRes_ = 0.0;  // the running plan's f_res and clamp (mirror, S5.6)
    double minF_ = 0.0;
    double capF_ = 0.0;
    double mirrorIncrement_ = 0.0;  // (f_h - f_res) / fs of the current hold
    std::uint64_t mirrorHoldCount_ = 0;  // samples counted in the current hold, not yet in accumulated_
    std::uint64_t counter_ = 0;
    std::size_t mirrorCountdown_ = 0;  // samples until the next hold refresh
    std::size_t pendingGridPhase_ = 0;
    float pendingFRes_ = 0.0f;
    float pendingMinF_ = 0.0f;
    float pendingCap_ = 0.0f;
    TriggerKind pendingKind_ = TriggerKind::SnapReset;
    bool prepared_ = false;
    bool bypass_ = false;
    bool pending_ = false;
    bool active_ = false;
    bool feasible_ = true;
    bool mirrorActive_ = false;
    bool mirrorFresh_ = false;
};

}  // namespace Krate::DSP
