// ==============================================================================
// Layer 2: Processor Tests - Profundum Phase 4 (specs/profundum-phase4-drop-note-behaviour)
// ==============================================================================
// Reference: specs/profundum-phase4-drop-note-behaviour/plan.md (S5, S11.1)
//            specs/profundum-phase4-drop-note-behaviour/tasks.md (T006 wires this TU,
//                                                                 T015, T016 fill it)
//
// Holds: SC-024(b), SC-025 allocation, SC-026 bypass, DropPhaseLock_C1, DropPhaseLock_TriggerCoalescing.
// T015: SC-024(b) (predictor at U = 1 vs the per-sample double sum, plan S1 C-13), the landing on a
//   test-side staircase model (tasks.md deviation 11), the feasibility census under the 2026-10-12
//   O-1 / O-4 rulings (gamma_lim = 48, no gamma_cap), SC-026 component part (FOLLOW bypass),
//   trigger coalescing (S5.4) and the lifecycle (deviation 7).
// T016: the staircase mirror (S5.6), the trigger kinds (S5.5, FR-059), SC-013(c) component part
//   (C¹ of d_c with the S11.2 bounds, and the magnitude gate 1.05·Γ + 1 ulp(A) with the
//   solver-independent Γ = γ_an + measured carry, plan S1 C-5 under the 2026-10-12 O-1 ruling) and
//   SC-025's allocation check.
//
// Shape values: as in drop_envelope_test.cpp, each anchor is passed as the largest float at or below
// it (float(2/3) rounds up into the elastic segment).
// ==============================================================================

#include "drop_c1_checker.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/drop_envelope.h>
#include <krate/dsp/processors/drop_phase_lock.h>

#include <catch2/catch_test_macros.hpp>

#include <allocation_detector.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using Krate::DSP::DropCurve;
using Krate::DSP::DropEnvelope;
using Krate::DSP::DropPhaseLock;
using Krate::DSP::DropTest::DropC1Segment;
using Krate::DSP::DropTest::dropCheckC1;
using Krate::DSP::DropTest::dropHermitePeakD2;

namespace {

using Kind = DropPhaseLock::TriggerKind;

constexpr std::array<double, 3> kRates{44100.0, 48000.0, 96000.0};
constexpr float kMinF0 = 8.0f;  // == ProfundumCore::kMinF0Hz (profundum_core.h:99); systems TU static_asserts it (C-14)
constexpr std::size_t kU = 16;  // == ProfundumCore pitch-update interval (C-14)

double midiHz(int note) { return 440.0 * std::exp2((static_cast<double>(note) - 69.0) / 12.0); }
constexpr int kE0 = 16;
constexpr int kE1 = 28;
constexpr int kE2 = 40;
constexpr int kC3 = 48;

/// The largest float at or below s (see the file comment).
float shapeParam(double s) {
    float f = static_cast<float>(s);
    if (static_cast<double>(f) > s)
        f = std::nextafter(f, std::numeric_limits<float>::lowest());
    return f;
}

/// Prepared and snap-triggered envelope.
DropEnvelope makeTriggeredEnv(double fs, float amount, float timeMs, float shape, float rebound) {
    DropEnvelope env;
    env.prepare(fs);
    env.setAmount(amount);
    env.setTime(timeMs);
    env.setShape(shape);
    env.setRebound(rebound);
    env.trigger(true);
    return env;
}

/// The core's held f0 for one float d_c: clamp(float(f_res * 2^{d_c/12}), minF, cap), the exponent in
/// double. The d_c value goes through the opaqueDoubleBits barrier because MSVC /fp:fast otherwise
/// narrows the all-float-leaf expression to float precision (measured: 1 ulp of f_h off the IEEE
/// value at E1, d_c = 23.999773, against the header's double-sourced S5.6 mirror).
float heldHz(float dc, float fRes, float minF, float cap) {
    const double x = std::bit_cast<double>(Krate::DSP::detail::opaqueDoubleBits(static_cast<double>(dc)));
    const float f = static_cast<float>(static_cast<double>(fRes) * std::exp2(x / 12.0));
    return std::clamp(f, minF, cap);
}

/// Samples the planned curve is non-zero on: [0, ceil(D_s)).
std::size_t curveLength(const DropCurve& c) { return static_cast<std::size_t>(std::ceil(c.endSample)); }

/// The test's staircase model of the core's h1 excess (plan S5.3, tasks.md deviation 11): f_h is
/// sampled at n = gridPhase + j*16 as clamp(float(f_res * 2^{d_c/12}), minF, cap) and held; before
/// the first grid sample the core holds the noteOn f0 (d_c[0]). The sum runs over [0, ceil(D_s) + 16):
/// the hold that starts at the last grid sample before D_s keeps its full 16 samples in the core,
/// and from the next grid sample on d_c = 0, so f_h = f_res and every later term is exactly 0.
double staircaseExcess(const std::vector<float>& dc, float fRes, double fs, float minF, float cap,
                       std::size_t gridPhase, std::size_t count) {
    const auto held = [&](std::size_t n) { return heldHz(dc[n], fRes, minF, cap); };
    float h = held(0);
    double sum = 0.0;
    for (std::size_t n = 0; n < count; ++n) {
        if (n >= gridPhase && (n - gridPhase) % kU == 0)
            h = held(n);
        sum += (static_cast<double>(h) - static_cast<double>(fRes)) / fs;
    }
    return sum;
}

bool bitEqual(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

// ---- T016 helpers ------------------------------------------------------------

/// Staircase excess (cycles) of the trace samples [begin, end) for a trigger at `begin`: the hold is
/// refreshed at the trigger sample and at every n with (n - begin - gridPhase) % 16 == 0 (plan S5.3,
/// S5.6). For begin = 0 this is staircaseExcess.
double segmentExcess(const std::vector<float>& dc, std::size_t begin, std::size_t end, float fRes, double fs,
                     float minF, float cap, std::size_t gridPhase) {
    float h = heldHz(dc[begin], fRes, minF, cap);
    double sum = 0.0;
    for (std::size_t n = begin; n < end; ++n) {
        const std::size_t k = n - begin;
        if (k >= gridPhase && (k - gridPhase) % kU == 0)
            h = heldHz(dc[n], fRes, minF, cap);
        sum += (static_cast<double>(h) - static_cast<double>(fRes)) / fs;
    }
    return sum;
}

/// One float ulp at |x|.
double floatUlp(double x) {
    const auto f = static_cast<float>(std::abs(x));
    return static_cast<double>(std::nextafter(f, std::numeric_limits<float>::max())) - static_cast<double>(f);
}

/// b(x) = 16 x^2 (1 - x)^2 on (0, 1), else 0 (plan S1 C-5).
double bumpRef(double x) { return (x > 0.0 && x < 1.0) ? 16.0 * x * x * (1.0 - x) * (1.0 - x) : 0.0; }
/// b'(x) = 32 x (1 - x)(1 - 2x) on (0, 1), else 0.
double bumpSlopeRef(double x) { return (x > 0.0 && x < 1.0) ? 32.0 * x * (1.0 - x) * (1.0 - 2.0 * x) : 0.0; }

/// The S5.5 carry: cubic Hermite from (c0, c0' per sample) to (0, 0) over len samples; 0 outside.
double carryRef(double k, double c0, double c0Slope, double len) {
    if (!(len > 0.0) || !(k >= 0.0) || !(k < len))
        return 0.0;
    const double t = k / len;
    return c0 * ((2.0 * t - 3.0) * t * t + 1.0) + c0Slope * len * (((t - 2.0) * t + 1.0) * t);
}

/// plan S1 C-5 (O-1 ruling 2026-10-12: no gamma_cap): gamma_an = inf{g : L(g) >= |r|} on [0, 48] by
/// bisection; -1 when L(48) < |r| (unbounded; a sentinel, since this TU builds under fast-math). L(g)
/// is drop-model.js analyticPeak's lower bound on the phase a correction of size g moves, summed per
/// sample over the planned samples dRef (st) with b = b(n / D_s): unclamped at g, convexity (r > 0)
/// or concavity (r < 0) with beta in {1/4, 1/2, 3/4}; pushed through a clamp by g, the exact
/// saturated move; already clamped at g = 0, nothing. Built only from r, f_res, D_s, the planned
/// curve and b: it never reads the solver.
double gammaAnalytic(const std::vector<double>& dRef, double Ds, double fRes, double fs, double minF, double cap,
                     double r) {
    const double ar = std::abs(r);
    if (ar == 0.0)
        return 0.0;
    const double sgn = r > 0.0 ? 1.0 : -1.0;
    const double ln2Over12 = std::log(2.0) / 12.0;
    std::vector<double> f0s;
    std::vector<double> bs;
    f0s.reserve(dRef.size());
    bs.reserve(dRef.size());
    for (std::size_t n = 0; n < dRef.size(); ++n) {
        const double b = bumpRef(static_cast<double>(n) / Ds);
        const double f0 = fRes * std::exp2(dRef[n] / 12.0);
        if (b == 0.0 || f0 < minF || f0 > cap)
            continue;
        f0s.push_back(f0);
        bs.push_back(b);
    }
    constexpr std::array<double, 3> kBetas{0.25, 0.5, 0.75};
    const auto L = [&](double g) {
        std::array<double, 3> betaMove{};
        for (std::size_t i = 0; i < kBetas.size(); ++i)
            betaMove[i] = sgn > 0.0 ? std::exp2(g * kBetas[i] / 12.0) - 1.0 : 1.0 - std::exp2(-g * kBetas[i] / 12.0);
        const double full = std::exp2(sgn * g / 12.0);  // b <= 1: a sample whose f0 * full stays inside cannot clamp
        const double lin = sgn > 0.0 ? g * ln2Over12 : 1.0 - std::exp2(-g / 12.0);
        double sum = 0.0;
        for (std::size_t k = 0; k < f0s.size(); ++k) {
            const double f0 = f0s[k];
            const double b = bs[k];
            const double fFull = f0 * full;
            if (fFull > cap || fFull < minF) {
                const double fg = f0 * std::exp2(sgn * g * b / 12.0);
                if (fg > cap || fg < minF) {
                    sum += std::abs(std::clamp(fg, minF, cap) - f0);
                    continue;
                }
            }
            double m = b * lin;
            for (std::size_t i = 0; i < kBetas.size(); ++i) {
                if (b >= kBetas[i])
                    m = std::max(m, betaMove[i]);
            }
            sum += f0 * m;
        }
        return sum / fs;
    };
    if (L(48.0) < ar)
        return -1.0;
    double lo = 0.0;
    double hi = 48.0;
    for (int i = 0; i < 50; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (L(mid) >= ar)
            hi = mid;
        else
            lo = mid;
    }
    return hi;
}

/// One rendered trace: a SnapReset trigger at sample 0 and optionally a Bridged retrigger at nTrig
/// (grid phase continuing the first trigger's 16-sample grid). d is the planned (bypass) offset,
/// dc the lock's output.
struct DropRun {
    std::vector<float> d, dc;
    DropCurve first{}, second{};
    std::size_t nTrig = 0;  // 0: no retrigger
    std::size_t phase2 = 0;
    double gammaFirst = 0.0;  // correctionGamma() of each solve: C¹ segment bounds only (plan S11.2)
    double gammaLast = 0.0;
    double target = 0.0;
    bool feasible = true;
};

DropRun runDrop(double fs, float fRes, float cap, float A, float T, float sp, float R, std::size_t nTrig) {
    DropRun run;
    auto env = makeTriggeredEnv(fs, A, T, sp, R);
    run.first = env.plannedCurve();
    DropPhaseLock lock;
    lock.prepare(fs);
    lock.trigger(run.first, fRes, 0, kMinF0, cap, Kind::SnapReset);
    const auto step = [&] {
        const float d = env.process();
        run.d.push_back(d);
        run.dc.push_back(lock.process(d));
    };
    if (nTrig == 0) {
        const std::size_t count = curveLength(run.first) + kU;
        run.d.reserve(count);
        run.dc.reserve(count);
        for (std::size_t n = 0; n < count; ++n)
            step();
        run.gammaFirst = lock.correctionGamma();
    } else {
        for (std::size_t n = 0; n < nTrig; ++n)
            step();
        run.gammaFirst = lock.correctionGamma();
        env.trigger(false);
        run.second = env.plannedCurve();
        run.nTrig = nTrig;
        run.phase2 = (kU - nTrig % kU) % kU;
        lock.trigger(run.second, fRes, run.phase2, kMinF0, cap, Kind::Bridged);
        const std::size_t count = curveLength(run.second) + kU;
        for (std::size_t n = 0; n < count; ++n)
            step();
    }
    run.gammaLast = lock.correctionGamma();
    run.target = lock.targetCycles();
    run.feasible = lock.landingFeasible();
    return run;
}

/// Staircase excess of a whole DropRun trace (both triggers).
double runExcess(const DropRun& run, const std::vector<float>& trace, double fs, float fRes, float cap) {
    if (run.nTrig == 0)
        return segmentExcess(trace, 0, trace.size(), fRes, fs, kMinF0, cap, 0);
    return segmentExcess(trace, 0, run.nTrig, fRes, fs, kMinF0, cap, 0) +
           segmentExcess(trace, run.nTrig, trace.size(), fRes, fs, kMinF0, cap, run.phase2);
}

/// A latched curve that owns the samples [offset, stop] of a d_c trace, with its correction:
/// |gamma| over [offset, offset + D_s) and the carry Hermite's peak |Δ²| over [offset, offset + carryLen).
struct CorrectedSpan {
    const DropCurve* curve;
    std::size_t offset;
    std::size_t stop;
    double gammaAbs;
    double carryD2;
    double carryLen;
};

/// Float allowance on |Δ²d_c|: d is a float of the closed form (≤ ½ ulp(|d|) + 1e-9 per sample) and
/// d_c a float of double(d) + c (≤ ½ ulp(|d_c|)), so a second difference carries ≤ 2 ulp of each.
double correctedAllowance(const std::vector<double>& g, const std::vector<double>& d, std::size_t lo,
                          std::size_t hi) {
    const std::size_t a = lo > 0 ? lo - 1 : 0;
    const std::size_t b = std::min(hi + 1, g.size() - 1);
    double mg = 0.0;
    double md = 0.0;
    for (std::size_t n = a; n <= b; ++n) {
        mg = std::max(mg, std::abs(g[n]));
        md = std::max(md, std::abs(d[n]));
    }
    return 2.0 * floatUlp(mg) + 2.0 * floatUlp(md) + 4e-9;
}

/// S11.2 segments for d_c = d + c: breaks on the first integer sample of every piece, of D_s and of
/// the idle region; each bound is the largest overlapping pieceD2Bound + |gamma|·32/D_s² (max|b''| = 32,
/// C-5) where the bump is live + the carry Hermite's peak where the carry is live + the float allowance.
std::vector<DropC1Segment> correctedSegments(const std::vector<double>& g, const std::vector<double>& d,
                                             const std::vector<CorrectedSpan>& spans) {
    std::vector<DropC1Segment> segs;
    for (const CorrectedSpan& sp : spans) {
        const DropCurve& c = *sp.curve;
        const auto o = static_cast<double>(sp.offset);
        const auto clampBreak = [&](double x) {
            const double b = std::ceil(o + x);
            return static_cast<std::size_t>(
                std::clamp(b, static_cast<double>(sp.offset), static_cast<double>(sp.stop)));
        };
        std::vector<std::size_t> br{sp.offset, sp.stop, clampBreak(c.endSample)};
        if (sp.carryLen > 0.0)
            br.push_back(clampBreak(sp.carryLen));
        for (int j = 0; j < c.pieceCount(); ++j) {
            br.push_back(clampBreak(c.pieceStart(j)));
            br.push_back(clampBreak(c.pieceEnd(j)));
        }
        std::sort(br.begin(), br.end());
        br.erase(std::unique(br.begin(), br.end()), br.end());
        const double Ds = c.endSample;
        for (std::size_t k = 0; k + 1 < br.size(); ++k) {
            const std::size_t lo = br[k];
            const std::size_t hi = br[k + 1];
            double bound = 0.0;
            for (int j = 0; j < c.pieceCount(); ++j) {
                const double a = std::max(o + c.pieceStart(j), static_cast<double>(lo));
                const double b = std::min(o + c.pieceEnd(j), static_cast<double>(hi));
                if (a < b)
                    bound = std::max(bound, c.pieceD2Bound(j));
            }
            if (Ds > 0.0 && static_cast<double>(lo) < o + Ds)
                bound += sp.gammaAbs * 32.0 / (Ds * Ds);
            if (sp.carryLen > 0.0 && static_cast<double>(lo) < o + sp.carryLen)
                bound += sp.carryD2;
            segs.push_back(DropC1Segment{lo, hi - lo, bound + correctedAllowance(g, d, lo, hi)});
        }
    }
    return segs;
}

std::vector<double> toDouble(const std::vector<float>& v) { return {v.begin(), v.end()}; }

/// dropCheckC1 on a DropRun's d_c with the S11.2 bounds (range unchecked: SC-013(c)'s magnitude gate
/// owns the size of c).
Krate::DSP::DropTest::DropC1Report checkRunC1(const DropRun& run) {
    const auto g = toDouble(run.dc);
    const auto d = toDouble(run.d);
    std::vector<CorrectedSpan> spans;
    if (run.nTrig == 0) {
        spans.push_back(CorrectedSpan{&run.first, 0, g.size() - 1, std::abs(run.gammaFirst), 0.0, 0.0});
    } else {
        spans.push_back(CorrectedSpan{&run.first, 0, run.nTrig, std::abs(run.gammaFirst), 0.0, 0.0});
        // The carry's start from the first plan (gamma_1 b(n / D_1) at n = nTrig, plan S5.5).
        const double D1 = run.first.endSample;
        const double x = static_cast<double>(run.nTrig) / D1;
        const double c0 = run.gammaFirst * bumpRef(x);
        const double c0Slope = run.gammaFirst * bumpSlopeRef(x) / D1;
        const double L = run.second.fallStart;
        const double carryD2 = L > 0.0 ? dropHermitePeakD2(c0, c0Slope, 0.0, static_cast<std::size_t>(L)) : 0.0;
        spans.push_back(
            CorrectedSpan{&run.second, run.nTrig, g.size() - 1, std::abs(run.gammaLast), carryD2, L});
    }
    const auto segs = correctedSegments(g, d, spans);
    return dropCheckC1(g, segs, -1e6, 1e6);
}

/// Mid-fall / mid-rebound / mid-elastic retrigger positions on a snap curve (as
/// DropEnvelope_RetriggerContinuation).
enum class Mid : std::uint8_t { Fall, Rebound, Elastic };
std::size_t midPosition(const DropCurve& c, Mid where) {
    const double N = c.fallSamples;
    switch (where) {
        case Mid::Fall:
            return static_cast<std::size_t>(std::llround(0.3 * N));
        case Mid::Rebound: {
            double next = c.knotT[0];
            for (const double t : c.knotT) {
                if (t > c.knotT[0]) {
                    next = t;
                    break;
                }
            }
            return static_cast<std::size_t>(std::llround(c.fallStart + 0.5 * (c.knotT[0] + next)));
        }
        case Mid::Elastic:
            return static_cast<std::size_t>(std::llround(1.3 * N));
    }
    return 0;
}

}  // namespace

// ------------------------------------------------------------------------------
// SC-024(b): predictor at U = 1 vs the per-sample double sum (plan S1 C-13: 2e-5 cycles absolute)
// ------------------------------------------------------------------------------
TEST_CASE("DropPhaseLock_PredictedExcessPhase", "[processors][profundum][drop-lock]") {
    double worst = 0.0;
    double worstFs = 0.0;
    double worstF = 0.0;
    float worstA = 0.0f;
    float worstT = 0.0f;
    float worstS = 0.0f;
    float worstR = 0.0f;
    int cells = 0;
    for (const double fs : kRates) {
        for (const int note : {kE0, kC3}) {
            const auto fRes = static_cast<double>(static_cast<float>(midiHz(note)));
            for (const float A : {1.0f, 12.0f, 48.0f}) {
                for (const float T : {1.0f, 20.0f, 200.0f, 4000.0f}) {
                    for (const double s : {0.0, 1.0 / 3.0, 2.0 / 3.0, 5.0 / 6.0, 1.0}) {
                        for (const float R : {0.0f, 0.5f, 1.0f}) {
                            const float sp = shapeParam(s);
                            auto env = makeTriggeredEnv(fs, A, T, sp, R);
                            const DropCurve curve = env.plannedCurve();
                            const double Ds = curve.endSample;
                            const std::size_t count = curveLength(curve) + 3;  // n in [0, ceil(D_s) + 2]
                            double ref = 0.0;
                            for (std::size_t n = 0; n < count; ++n) {
                                const auto d = static_cast<double>(env.process());
                                ref += fRes * (std::exp2(d / 12.0) - 1.0) / fs;
                            }
                            const double got = DropPhaseLock::predictExcess(curve, fRes, fs, 1, 0, 0.0, 1e12, 0.0, Ds,
                                                                            0.0, 0.0, 0.0);
                            const double err = std::abs(got - ref);
                            CAPTURE(fs, note, A, T, sp, R, ref, got, err);
                            CHECK(err <= 2e-5);
                            if (err > worst) {
                                worst = err;
                                worstFs = fs;
                                worstF = fRes;
                                worstA = A;
                                worstT = T;
                                worstS = sp;
                                worstR = R;
                            }
                            ++cells;
                        }
                    }
                }
            }
        }
    }
    REQUIRE(cells == 1080);
    INFO("SC-024(b) worst |predicted - per-sample sum| = " << worst << " cycles at fs " << worstFs << ", f_res "
                                                           << worstF << ", A " << worstA << ", T " << worstT
                                                           << " ms, s " << worstS << ", R " << worstR
                                                           << " (model: ~1.41e-5 at C3, 44.1 kHz, A 48, T 1 ms)");
    CHECK(worst <= 2e-5);
}

// ------------------------------------------------------------------------------
// Deviation 11: landing gated on the test's staircase model of d_c (1/360 cycle = 1 degree)
// ------------------------------------------------------------------------------
TEST_CASE("DropPhaseLock_LandingOnStaircaseModel", "[processors][profundum][drop-lock]") {
    constexpr double fs = 48000.0;
    const float cap = static_cast<float>(0.8 * 24000.0);
    double worstTheta = 0.0;
    double worstResidual = 0.0;
    double maxGamma = 0.0;
    int cells = 0;
    for (const int note : {kE1, kC3}) {
        const float fRes = static_cast<float>(midiHz(note));
        for (const float A : {12.0f, 48.0f}) {
            for (const float T : {60.0f, 200.0f}) {
                for (const double s : {0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0}) {
                    for (const float R : {0.0f, 1.0f}) {
                        const float sp = shapeParam(s);
                        auto env = makeTriggeredEnv(fs, A, T, sp, R);
                        const DropCurve curve = env.plannedCurve();
                        DropPhaseLock lock;
                        lock.prepare(fs);
                        lock.trigger(curve, fRes, 0, kMinF0, cap, Kind::SnapReset);
                        const std::size_t len = curveLength(curve);
                        const std::size_t count = len + kU;
                        std::vector<float> dc(count);
                        for (std::size_t n = 0; n < count; ++n)
                            dc[n] = lock.process(env.process());
                        const double phi = staircaseExcess(dc, fRes, fs, kMinF0, cap, 0, count);
                        const double theta = std::abs(phi - lock.targetCycles());
                        CAPTURE(note, A, T, sp, R, phi, lock.targetCycles(), lock.predictedExcessCycles(),
                                lock.correctionGamma(), lock.solveResidualCycles());
                        CHECK(theta <= 1.0 / 360.0);
                        CHECK(lock.landingFeasible());
                        // O-1 / O-4 (2026-10-12): no slope cap; the only bound is the 48 st clamp.
                        CHECK(std::abs(lock.correctionGamma()) <= DropPhaseLock::kMaxCorrectionSemitones);
                        // C-4: the target is a neighbouring integer of the prediction (|r| < 1 cycle).
                        CHECK(lock.targetCycles() == std::floor(lock.targetCycles()));
                        CHECK(std::abs(lock.targetCycles() - lock.predictedExcessCycles()) < 1.0);
                        bool zeroAfter = true;
                        for (std::size_t n = len; n < count; ++n)
                            zeroAfter = zeroAfter && bitEqual(dc[n], 0.0f);
                        CHECK(zeroAfter);
                        worstTheta = std::max(worstTheta, theta);
                        worstResidual = std::max(worstResidual, lock.solveResidualCycles());
                        maxGamma = std::max(maxGamma, std::abs(lock.correctionGamma()));
                        ++cells;
                    }
                }
            }
        }
    }
    REQUIRE(cells == 64);
    INFO("deviation 11: worst |Phi_staircase - target| = " << worstTheta * 360.0 << " deg, worst solve residual "
                                                           << worstResidual << " cycles, max |gamma| " << maxGamma
                                                           << " st");
    CHECK(worstTheta <= 1.0 / 360.0);
}

// ------------------------------------------------------------------------------
// C-6 under O-1 / O-4 (2026-10-12): every SC-013 cell is feasible with gamma_lim = 48; the +-48 clamp
// branch is exercised by one synthetic trigger (no json, no gate on its feasibility).
// ------------------------------------------------------------------------------
TEST_CASE("DropPhaseLock_InfeasibleClamp", "[processors][profundum][drop-lock]") {
    int cells = 0;
    int infeasible = 0;
    double maxGamma = 0.0;
    for (const double fs : kRates) {
        const auto cap = static_cast<float>(0.4 * fs);  // SpectralShapeRecipe::capFrequency = 0.8 * fs / 2
        for (const int note : {kE0, kE1, kE2, kC3}) {
            const float fRes = static_cast<float>(midiHz(note));
            for (const float A : {6.0f, 12.0f, 24.0f, 48.0f}) {
                for (const float T : {10.0f, 60.0f, 200.0f, 1000.0f}) {
                    for (const double s : {0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0}) {
                        for (const float R : {0.0f, 1.0f}) {
                            const float sp = shapeParam(s);
                            auto env = makeTriggeredEnv(fs, A, T, sp, R);
                            DropPhaseLock lock;
                            lock.prepare(fs);
                            lock.trigger(env.plannedCurve(), fRes, 0, kMinF0, cap, Kind::SnapReset);
                            (void)lock.process(env.process());  // runs the lazy solve
                            CAPTURE(fs, note, A, T, sp, R, lock.correctionGamma(), lock.predictedExcessCycles(),
                                    lock.targetCycles(), lock.solveResidualCycles());
                            CHECK(lock.landingFeasible());
                            CHECK(std::abs(lock.correctionGamma()) <= DropPhaseLock::kMaxCorrectionSemitones);
                            if (!lock.landingFeasible())
                                ++infeasible;
                            maxGamma = std::max(maxGamma, std::abs(lock.correctionGamma()));
                            ++cells;
                        }
                    }
                }
            }
        }
    }
    REQUIRE(cells == 1536);
    CHECK(infeasible == 0);
    INFO("SC-013 grid feasibility census: " << cells << " cells, " << infeasible << " infeasible, max |gamma| "
                                            << maxGamma << " st (model lock2all: 47.9 st at T = 10 ms)");

    // Synthetic clamp-branch trigger: f_res = kMinF0Hz, A = 48, T = 1 ms, exponential, R = 1.
    constexpr double fs = 48000.0;
    auto env = makeTriggeredEnv(fs, 48.0f, 1.0f, 0.0f, 1.0f);
    DropPhaseLock lock;
    lock.prepare(fs);
    lock.trigger(env.plannedCurve(), kMinF0, 0, kMinF0, static_cast<float>(0.4 * fs), Kind::SnapReset);
    (void)lock.process(env.process());
    INFO("synthetic trigger: landingFeasible " << lock.landingFeasible() << ", gamma " << lock.correctionGamma()
                                               << ", predicted " << lock.predictedExcessCycles() << ", target "
                                               << lock.targetCycles() << ", residual "
                                               << lock.solveResidualCycles());
    CHECK(std::abs(lock.correctionGamma()) <= DropPhaseLock::kMaxCorrectionSemitones);
    if (!lock.landingFeasible())
        CHECK(std::abs(lock.correctionGamma()) == DropPhaseLock::kMaxCorrectionSemitones);
}

// ------------------------------------------------------------------------------
// SC-026 component part / FR-058: FOLLOW bypass returns the planned offset bitwise
// ------------------------------------------------------------------------------
TEST_CASE("DropPhaseLock_FollowBypass", "[processors][profundum][drop-lock]") {
    constexpr double fs = 48000.0;
    const float fRes = static_cast<float>(midiHz(kE1));
    const float cap = static_cast<float>(0.4 * fs);
    auto env = makeTriggeredEnv(fs, 24.0f, 20.0f, 1.0f, 1.0f);  // elastic, R = 1
    const DropCurve curve = env.plannedCurve();

    std::vector<float> values;
    values.reserve(10000);
    for (const float v : {48.0f, -48.0f, 0.0f, -0.0f, 1e-30f, -1e-30f})
        values.push_back(v);
    while (values.size() < 10000)
        values.push_back(env.process());  // the planned d (elastic tail and the exact 0.0f after it)

    DropPhaseLock lock;
    lock.prepare(fs);
    lock.setBypass(true);
    std::vector<float> out(values.size());
    std::size_t i = 0;
    for (const Kind kind : {Kind::SnapReset, Kind::SnapFreeRunning, Kind::Bridged}) {
        lock.trigger(curve, fRes, 0, kMinF0, cap, kind);
        for (std::size_t k = 0; k < values.size() / 3; ++k, ++i)
            out[i] = lock.process(values[i]);
    }
    for (; i < values.size(); ++i)
        out[i] = lock.process(values[i]);
    REQUIRE(std::memcmp(out.data(), values.data(), values.size() * sizeof(float)) == 0);
    CHECK(lock.correctionGamma() == 0.0);

    // processBlock likewise, after a trigger of every kind; in place as well.
    for (const Kind kind : {Kind::SnapReset, Kind::SnapFreeRunning, Kind::Bridged}) {
        lock.trigger(curve, fRes, 0, kMinF0, cap, kind);
        std::vector<float> blockOut(values.size());
        lock.processBlock(values.data(), blockOut.data(), values.size());
        CHECK(std::memcmp(blockOut.data(), values.data(), values.size() * sizeof(float)) == 0);
        std::vector<float> inPlace = values;
        lock.trigger(curve, fRes, 0, kMinF0, cap, kind);
        lock.processBlock(inPlace.data(), inPlace.data(), inPlace.size());
        CHECK(std::memcmp(inPlace.data(), values.data(), values.size() * sizeof(float)) == 0);
    }
}

// ------------------------------------------------------------------------------
// S5.4 / SC-019 functional half: N triggers at one offset cost one solve; the last one wins
// ------------------------------------------------------------------------------
TEST_CASE("DropPhaseLock_TriggerCoalescing", "[processors][profundum][drop-lock]") {
    constexpr double fs = 48000.0;
    const float fRes = static_cast<float>(midiHz(kE1));
    const float cap = static_cast<float>(0.4 * fs);
    constexpr std::size_t kN = 4096;

    std::vector<DropCurve> curves;
    for (int k = 0; k < 64; ++k) {
        auto env = makeTriggeredEnv(fs, 4.0f + 0.5f * static_cast<float>(k), 10.0f + static_cast<float>(k),
                                    shapeParam(static_cast<double>(k % 4) / 3.0), (k % 2) == 0 ? 0.0f : 1.0f);
        curves.push_back(env.plannedCurve());
    }
    std::vector<float> d(kN);
    {
        auto env = makeTriggeredEnv(fs, 4.0f + 0.5f * 63.0f, 10.0f + 63.0f, shapeParam(1.0), 1.0f);
        for (auto& v : d)
            v = env.process();
    }

    DropPhaseLock many;
    many.prepare(fs);
    for (const auto& c : curves)
        many.trigger(c, fRes, 0, kMinF0, cap, Kind::SnapReset);
    DropPhaseLock single;
    single.prepare(fs);
    single.trigger(curves.back(), fRes, 0, kMinF0, cap, Kind::SnapReset);

    std::vector<float> outMany(kN);
    std::vector<float> outSingle(kN);
    many.processBlock(d.data(), outMany.data(), kN);
    single.processBlock(d.data(), outSingle.data(), kN);
    REQUIRE(std::equal(outMany.begin(), outMany.end(), outSingle.begin(), bitEqual));
    CHECK(many.correctionGamma() == single.correctionGamma());
    CHECK(many.targetCycles() == single.targetCycles());
    CHECK(many.predictedExcessCycles() == single.predictedExcessCycles());
    CHECK(many.solveResidualCycles() == single.solveResidualCycles());
    // The correction is live (the case has teeth: the output differs from the planned d).
    CHECK(single.correctionGamma() != 0.0);
    CHECK_FALSE(std::equal(outSingle.begin(), outSingle.end(), d.begin(), bitEqual));

    // processBlock(nullptr, ...) and n = 0 are no-ops: a pending solve stays pending.
    DropPhaseLock a;
    a.prepare(fs);
    a.trigger(curves.back(), fRes, 0, kMinF0, cap, Kind::SnapReset);
    a.processBlock(nullptr, outMany.data(), kN);
    a.processBlock(d.data(), nullptr, kN);
    a.processBlock(d.data(), outMany.data(), 0);
    a.processBlock(d.data(), outMany.data(), kN);
    CHECK(std::equal(outMany.begin(), outMany.end(), outSingle.begin(), bitEqual));
}

// ------------------------------------------------------------------------------
// Lifecycle: before prepare, reset, deviation-7 inputs
// ------------------------------------------------------------------------------
TEST_CASE("DropPhaseLock_Lifecycle", "[processors][profundum][drop-lock]") {
    constexpr double fs = 48000.0;
    const float fRes = static_cast<float>(midiHz(kE1));
    const float cap = static_cast<float>(0.4 * fs);
    auto env = makeTriggeredEnv(fs, 24.0f, 60.0f, 0.0f, 1.0f);
    const DropCurve curve = env.plannedCurve();
    std::vector<float> d(curveLength(curve) + 32);
    for (auto& v : d)
        v = env.process();

    SECTION("before prepare: process is the identity, trigger is a no-op") {
        DropPhaseLock lock;
        lock.trigger(curve, fRes, 0, kMinF0, cap, Kind::SnapReset);
        for (const float v : d)
            REQUIRE(bitEqual(lock.process(v), v));
        CHECK(lock.correctionGamma() == 0.0);
        CHECK(lock.accumulatedExcessCycles() == 0.0);
        // prepare(NaN) / prepare(Inf) are no-ops (deviation 7): still unprepared.
        lock.prepare(std::numeric_limits<double>::quiet_NaN());
        lock.prepare(std::numeric_limits<double>::infinity());
        lock.trigger(curve, fRes, 0, kMinF0, cap, Kind::SnapReset);
        for (const float v : d)
            REQUIRE(bitEqual(lock.process(v), v));
    }

    SECTION("reset clears the plan, the pending flag and the accumulator") {
        DropPhaseLock lock;
        lock.prepare(fs);
        lock.trigger(curve, fRes, 0, kMinF0, cap, Kind::SnapReset);
        (void)lock.process(d[0]);
        REQUIRE(lock.correctionGamma() != 0.0);  // a live plan
        lock.trigger(curve, fRes, 0, kMinF0, cap, Kind::SnapReset);  // pending
        lock.reset();
        CHECK(lock.accumulatedExcessCycles() == 0.0);
        for (const float v : d)
            REQUIRE(bitEqual(lock.process(v), v));
        CHECK(lock.correctionGamma() == 0.0);
        CHECK(lock.accumulatedExcessCycles() == 0.0);
    }

    SECTION("deviation 7: non-finite or non-positive fRes / minF0 / cap triggers are ignored") {
        DropPhaseLock ref;
        ref.prepare(fs);
        ref.trigger(curve, fRes, 0, kMinF0, cap, Kind::SnapReset);
        DropPhaseLock lock;
        lock.prepare(fs);
        lock.trigger(curve, fRes, 0, kMinF0, cap, Kind::SnapReset);
        const std::size_t split = 100;
        for (std::size_t n = 0; n < split; ++n)
            REQUIRE(bitEqual(lock.process(d[n]), ref.process(d[n])));
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        for (const float bad : {nan, inf, -inf, 0.0f, -1.0f}) {
            lock.trigger(curve, bad, 0, kMinF0, cap, Kind::SnapReset);
            lock.trigger(curve, fRes, 0, bad, cap, Kind::SnapReset);
            lock.trigger(curve, fRes, 0, kMinF0, bad, Kind::SnapReset);
        }
        for (std::size_t n = split; n < d.size(); ++n)
            REQUIRE(bitEqual(lock.process(d[n]), ref.process(d[n])));
        CHECK(lock.correctionGamma() == ref.correctionGamma());
    }
}

// ------------------------------------------------------------------------------
// S5.6: the staircase mirror equals the test's double staircase sum at every sample
// ------------------------------------------------------------------------------
TEST_CASE("DropPhaseLock_Mirror", "[processors][profundum][drop-lock]") {
    constexpr double fs = 48000.0;
    constexpr std::size_t kGridPhase = 5;
    const float fRes = static_cast<float>(midiHz(kE1));
    const float cap = static_cast<float>(0.4 * fs);
    auto env = makeTriggeredEnv(fs, 24.0f, 200.0f, shapeParam(1.0), 1.0f);  // elastic + rebound
    const DropCurve curve = env.plannedCurve();
    DropPhaseLock lock;
    lock.prepare(fs);
    lock.trigger(curve, fRes, kGridPhase, kMinF0, cap, Kind::SnapReset);

    const std::size_t count = curveLength(curve) + static_cast<std::size_t>(2.0 * fs);  // 2 s past landing
    float h = 0.0f;
    double ref = 0.0;
    double worstRel = 0.0;
    std::size_t firstBad = count;
    for (std::size_t n = 0; n < count; ++n) {
        const float dc = lock.process(env.process());
        if (n == 0 || (n >= kGridPhase && (n - kGridPhase) % kU == 0))
            h = heldHz(dc, fRes, kMinF0, cap);
        ref += (static_cast<double>(h) - static_cast<double>(fRes)) / fs;
        const double got = lock.accumulatedExcessCycles();
        const double err = std::abs(got - ref);
        const bool ok = ref == 0.0 ? got == 0.0 : err <= 1e-12 * std::abs(ref);
        if (ref != 0.0)
            worstRel = std::max(worstRel, err / std::abs(ref));
        if (!ok && firstBad == count)
            firstBad = n;
    }
    INFO("worst relative |mirror - staircase| " << worstRel << ", first failing sample " << firstBad << " of "
                                                << count);
    CHECK(firstBad == count);
    CHECK(lock.correctionGamma() != 0.0);           // a live correction was mirrored
    CHECK(lock.accumulatedExcessCycles() != 0.0);
}

// ------------------------------------------------------------------------------
// FR-059 / S5.5: the trigger kinds' accumulator handling and the bridged plan
// ------------------------------------------------------------------------------
TEST_CASE("DropPhaseLock_TriggerKinds", "[processors][profundum][drop-lock]") {
    constexpr double fs = 48000.0;
    const float cap = static_cast<float>(0.4 * fs);

    SECTION("(i) SnapReset zeroes the accumulator") {
        const float fRes = static_cast<float>(midiHz(kE1));
        auto env = makeTriggeredEnv(fs, 48.0f, 4000.0f, 0.0f, 0.0f);
        DropPhaseLock lock;
        lock.prepare(fs);
        lock.trigger(env.plannedCurve(), fRes, 0, kMinF0, cap, Kind::SnapReset);
        for (std::size_t n = 0; n < static_cast<std::size_t>(fs); ++n)  // 1 s of the drop
            (void)lock.process(env.process());
        const double pre = lock.accumulatedExcessCycles();
        REQUIRE(pre != 0.0);  // teeth: there is something to zero
        env.setAmount(12.0f);
        env.setTime(200.0f);
        env.trigger(true);
        lock.trigger(env.plannedCurve(), fRes, 0, kMinF0, cap, Kind::SnapReset);  // 48000 % 16 == 0
        const float dc0 = lock.process(env.process());
        const double expected =
            (static_cast<double>(heldHz(dc0, fRes, kMinF0, cap)) - static_cast<double>(fRes)) / fs;
        CAPTURE(pre, expected, lock.accumulatedExcessCycles());
        // Zeroed, then exactly one sample's contribution (a 1e-14 relative tolerance only absorbs
        // fast-math turning the test's x / 48000.0 into a reciprocal multiply).
        CHECK(std::abs(lock.accumulatedExcessCycles() - expected) <= 1e-14 * std::abs(expected));
        // The target is Phi_hat's neighbour alone (no kept excess).
        CHECK(std::abs(lock.targetCycles() - lock.predictedExcessCycles()) < 1.0);
    }

    SECTION("(ii) SnapFreeRunning keeps the interrupted drop's excess and lands the total") {
        for (const int note : {kE1, kC3}) {
            const float fRes = static_cast<float>(midiHz(note));
            CAPTURE(note);
            auto env = makeTriggeredEnv(fs, 48.0f, 4000.0f, 0.0f, 0.0f);
            DropPhaseLock lock;
            lock.prepare(fs);
            lock.trigger(env.plannedCurve(), fRes, 0, kMinF0, cap, Kind::SnapReset);
            const auto nStop = static_cast<std::size_t>(0.3 * fs);  // interrupted at 300 ms
            std::vector<float> dc;
            for (std::size_t n = 0; n < nStop; ++n)
                dc.push_back(lock.process(env.process()));
            const double pre = lock.accumulatedExcessCycles();
            REQUIRE(pre != 0.0);
            REQUIRE(env.isActive());  // the first drop is still in flight

            env.setAmount(12.0f);
            env.setTime(200.0f);
            env.trigger(true);
            const DropCurve second = env.plannedCurve();
            const std::size_t phase = (kU - nStop % kU) % kU;
            lock.trigger(second, fRes, phase, kMinF0, cap, Kind::SnapFreeRunning);
            dc.push_back(lock.process(env.process()));
            const double first =
                (static_cast<double>(heldHz(dc.back(), fRes, kMinF0, cap)) - static_cast<double>(fRes)) / fs;
            CAPTURE(pre, first, lock.accumulatedExcessCycles(), lock.targetCycles(), lock.predictedExcessCycles());
            // Not zeroed: the pre-trigger value plus the first sample (tolerance as in (i)).
            CHECK(std::abs(lock.accumulatedExcessCycles() - (pre + first)) <=
                  1e-14 * (std::abs(pre) + std::abs(first)));
            CHECK(lock.targetCycles() == std::floor(lock.targetCycles()));
            CHECK(std::abs(lock.targetCycles() - (pre + lock.predictedExcessCycles())) < 1.0);
            CHECK(lock.landingFeasible());

            const std::size_t count = curveLength(second) + kU;
            for (std::size_t n = 1; n < count; ++n)
                dc.push_back(lock.process(env.process()));
            const double total = segmentExcess(dc, 0, nStop, fRes, fs, kMinF0, cap, 0) +
                                 segmentExcess(dc, nStop, dc.size(), fRes, fs, kMinF0, cap, phase);
            CAPTURE(total);
            CHECK(std::abs(total - lock.targetCycles()) <= 1.0 / 360.0);
        }
    }

    SECTION("(iii) Bridged mid-fall, mid-rebound, mid-elastic: no step, lands the whole trace") {
        struct Cell {
            Mid where;
            double s;
            float R;
            const char* name;
        };
        const std::array<Cell, 3> cells{Cell{Mid::Fall, 0.0, 0.0f, "mid-fall"},
                                        Cell{Mid::Rebound, 0.0, 1.0f, "mid-rebound"},
                                        Cell{Mid::Elastic, 1.0, 0.0f, "mid-elastic"}};
        constexpr float T = 200.0f;
        double worstTheta = 0.0;
        int count = 0;
        for (const Cell& cell : cells) {
            for (const int note : {kE1, kC3}) {
                for (const float A : {12.0f, 48.0f}) {
                    const float fRes = static_cast<float>(midiHz(note));
                    const float sp = shapeParam(cell.s);
                    const DropCurve snap = makeTriggeredEnv(fs, A, T, sp, cell.R).plannedCurve();
                    const std::size_t nTrig = midPosition(snap, cell.where);
                    REQUIRE(nTrig >= 2);
                    const DropRun run = runDrop(fs, fRes, cap, A, T, sp, cell.R, nTrig);
                    CAPTURE(cell.name, note, A, nTrig, run.gammaFirst, run.gammaLast, run.target);
                    REQUIRE(run.second.fallStart > 0.0);  // a bridged curve
                    // The first plan corrects (the carry has something to carry).
                    CHECK(run.gammaFirst != 0.0);
                    const auto rep = checkRunC1(run);
                    CAPTURE(rep.worstRatio, rep.worstBoundaryJump);
                    CHECK(rep.pass);
                    CHECK(run.feasible);
                    const double total = runExcess(run, run.dc, fs, fRes, cap);
                    const double theta = std::abs(total - run.target);
                    CAPTURE(total, theta * 360.0);
                    CHECK(theta <= 1.0 / 360.0);
                    worstTheta = std::max(worstTheta, theta);
                    ++count;
                }
            }
        }
        REQUIRE(count == 12);
        INFO("bridged landings: worst |Phi_staircase - target| " << worstTheta * 360.0 << " deg");
        CHECK(worstTheta <= 1.0 / 360.0);
    }
}

// ------------------------------------------------------------------------------
// SC-013(c) component part, FR-054: C¹ of d_c (S11.2 bounds) and the magnitude gate
// peak |d_c - d| <= 1.05 Γ + 1 ulp(A), Γ = γ_an + measured carry (plan S1 C-5; O-1: no γ_cap)
// ------------------------------------------------------------------------------
namespace {

struct Sc013cGate {
    double peak;   // measured max |d_c - d| over the gated span (st)
    double gan;    // γ_an (st); -1 when unbounded (L(48) < |r|): Γ then uses the 48 st clamp (O-1 / O-4)
    double carry;  // |c0| + |c0'| L 4/27 from the measured trace (0 on a snap)
    double bound;  // 1.05 Γ + 1 ulp(A)
    double r;      // n* - Φ_ref(0) (cycles)
};

/// Solver-independent: n* is the corrected render's own staircase total rounded; Φ_ref(0) the
/// staircase of the uncorrected render (with the measured carry on a bridged span); γ_an from r, f_res,
/// D_s and the planned curve. A bridged run gates the span after the retrigger (the span before it is
/// the snap cell's prefix, gated there).
Sc013cGate sc013cGate(const DropRun& run, double fs, float fRes, float cap, float A) {
    Sc013cGate gate{0.0, 0.0, 0.0, 0.0, 0.0};
    const double nStar = std::round(runExcess(run, run.dc, fs, fRes, cap));
    const std::size_t k = run.nTrig;
    const DropCurve& plan = k == 0 ? run.first : run.second;
    const std::size_t len = curveLength(plan);
    std::vector<double> dRef(len);
    double phiRef = 0.0;
    if (k == 0) {
        phiRef = runExcess(run, run.d, fs, fRes, cap);
        for (std::size_t j = 0; j < len; ++j)
            dRef[j] = static_cast<double>(run.d[j]);
    } else {
        const double c0 = static_cast<double>(run.dc[k]) - static_cast<double>(run.d[k]);
        const double cPrev = static_cast<double>(run.dc[k - 1]) - static_cast<double>(run.d[k - 1]);
        const double c0Slope = c0 - cPrev;
        const double L = plan.fallStart;
        gate.carry = std::abs(c0) + std::abs(c0Slope) * L * (4.0 / 27.0);
        std::vector<float> ref = run.dc;  // the corrected first span, then d + carry (no new bump)
        for (std::size_t n = k; n < ref.size(); ++n)
            ref[n] = static_cast<float>(static_cast<double>(run.d[n]) +
                                        carryRef(static_cast<double>(n - k), c0, c0Slope, L));
        phiRef = runExcess(run, ref, fs, fRes, cap);
        for (std::size_t j = 0; j < len; ++j)
            dRef[j] = static_cast<double>(run.d[k + j]) + carryRef(static_cast<double>(j), c0, c0Slope, L);
    }
    gate.r = nStar - phiRef;
    gate.gan = gammaAnalytic(dRef, plan.endSample, static_cast<double>(fRes), fs, static_cast<double>(kMinF0),
                             static_cast<double>(cap), gate.r);
    for (std::size_t n = k; n < run.dc.size(); ++n)
        gate.peak = std::max(gate.peak, std::abs(static_cast<double>(run.dc[n]) - static_cast<double>(run.d[n])));
    // O-1 removed γ_cap; where γ_an is unbounded the only solver-independent figure left is the
    // 48 st correction clamp (O-4), so every cell is still gated.
    const double gammaBound = gate.gan < 0.0 ? DropPhaseLock::kMaxCorrectionSemitones : gate.gan;
    gate.bound = 1.05 * (gammaBound + gate.carry) + floatUlp(static_cast<double>(A));
    return gate;
}

}  // namespace

TEST_CASE("DropPhaseLock_C1", "[processors][profundum][drop-lock]") {
    constexpr double fs = 48000.0;
    const float cap = static_cast<float>(0.4 * fs);
    constexpr std::array<float, 4> kTimes{10.0f, 60.0f, 200.0f, 1000.0f};
    std::array<double, 4> worstPeak{};
    std::array<double, 4> worstRatio{};  // peak / (1.05 Γ + 1 ulp(A))
    std::array<int, 4> unbounded{};
    double worstC1 = 0.0;
    int runs = 0;

    for (const int note : {kE0, kC3}) {
        const float fRes = static_cast<float>(midiHz(note));
        for (std::size_t ti = 0; ti < kTimes.size(); ++ti) {
            const float T = kTimes[ti];
            for (const float A : {12.0f, 48.0f}) {
                for (const double s : {0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0}) {
                    for (const float R : {0.0f, 1.0f}) {
                        const float sp = shapeParam(s);
                        const DropCurve snap = makeTriggeredEnv(fs, A, T, sp, R).plannedCurve();
                        for (const bool bridged : {false, true}) {
                            const std::size_t nTrig = bridged ? midPosition(snap, Mid::Fall) : 0;
                            const DropRun run = runDrop(fs, fRes, cap, A, T, sp, R, nTrig);
                            CAPTURE(note, T, A, sp, R, bridged, nTrig, run.gammaFirst, run.gammaLast);
                            const auto rep = checkRunC1(run);
                            CAPTURE(rep.worstRatio, rep.worstBoundaryJump);
                            CHECK(rep.pass);
                            worstC1 = std::max(worstC1, rep.worstRatio);

                            const Sc013cGate gate = sc013cGate(run, fs, fRes, cap, A);
                            CAPTURE(gate.peak, gate.gan, gate.carry, gate.bound, gate.r);
                            CHECK(std::abs(gate.r) < 1.0);  // C-4
                            CHECK(gate.peak <= gate.bound);
                            if (gate.gan < 0.0) {
                                ++unbounded[ti];
                            } else {
                                worstRatio[ti] = std::max(worstRatio[ti], gate.peak / gate.bound);
                            }
                            worstPeak[ti] = std::max(worstPeak[ti], gate.peak);
                            ++runs;
                        }
                    }
                }
            }
        }
    }
    REQUIRE(runs == 256);
    for (std::size_t ti = 0; ti < kTimes.size(); ++ti) {
        WARN("SC-013(c) component, T = " << kTimes[ti] << " ms: peak |d_c - d| " << worstPeak[ti]
                                         << " st, worst peak / (1.05 Gamma + 1 ulp(A)) " << worstRatio[ti]
                                         << ", cells with gamma_an unbounded (gated at 48 st) " << unbounded[ti]);
    }
    WARN("SC-013(c) component: worst C1 |d2 d_c| / bound " << worstC1);

    // Positive control: a cornered linear ramp (0 -> 1 over 100 samples, then held) fails.
    std::vector<double> ramp(200);
    for (std::size_t n = 0; n < ramp.size(); ++n)
        ramp[n] = std::min(1.0, static_cast<double>(n) / 100.0);
    const std::array<DropC1Segment, 2> rampSegs{DropC1Segment{0, 100, 1e-6}, DropC1Segment{100, 99, 1e-6}};
    const auto rampRep = dropCheckC1(ramp, rampSegs, -1e6, 1e6);
    CAPTURE(rampRep.worstRatio, rampRep.worstBoundaryJump);
    CHECK_FALSE(rampRep.pass);
}

// ------------------------------------------------------------------------------
// SC-025 (FR-057, FR-065): no allocation over triggers of every kind with solving process calls
// ------------------------------------------------------------------------------
TEST_CASE("DropPhaseLock_NoAllocation", "[processors][profundum][drop-lock]") {
    constexpr double fs = 48000.0;
    const float fRes = static_cast<float>(midiHz(kE1));
    const float cap = static_cast<float>(0.4 * fs);
    DropEnvelope env;
    env.prepare(fs);
    env.setAmount(24.0f);
    env.setShape(1.0f);
    env.setRebound(1.0f);
    DropPhaseLock lock;
    lock.prepare(fs);
    std::array<float, 64> in{};
    std::array<float, 64> out{};
    float sink = 0.0f;
    int solves = 0;

    std::size_t observed = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        lock.setBypass(true);
        lock.setBypass(false);
        for (int i = 0; i < 1000; ++i) {
            const auto kind = static_cast<Kind>(i % 3);
            env.setTime(1.0f + static_cast<float>(i % 50) * 20.0f);
            env.trigger(kind != Kind::Bridged);
            lock.trigger(env.plannedCurve(), fRes, static_cast<std::size_t>(i) % kU, kMinF0, cap, kind);
            if (i % 2 == 0) {
                for (int k = 0; k < 16; ++k)
                    sink += lock.process(env.process());
            } else {
                for (float& v : in)
                    v = env.process();
                lock.processBlock(in.data(), out.data(), in.size());
                sink += out[0];
            }
            if (lock.correctionGamma() != 0.0)
                ++solves;
        }
        // AllocationScope latches in its destructor (allocation_detector.h:117-119), so the live
        // figure is read while the scope is open.
        observed = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    INFO("allocations observed: " << observed << ", solves with gamma != 0: " << solves);
    CHECK(sink != 0.0f);
    CHECK(solves > 0);
    REQUIRE(observed == 0u);

    // Positive control: the detector sees an allocation inside an identical scope.
    std::size_t control = 0;
    int probeObserved = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        int* probe = new int(42);
        volatile int* probeSink = probe;  // defeat new/delete elision
        probeObserved = *probeSink;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        delete probe;
        control = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    CHECK(probeObserved == 42);
    CHECK(control >= 1u);
}
