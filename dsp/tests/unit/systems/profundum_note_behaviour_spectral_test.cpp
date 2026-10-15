// ==============================================================================
// Layer 3: System Tests - Profundum Phase 4 (specs/profundum-phase4-drop-note-behaviour)
// ==============================================================================
// Reference: specs/profundum-phase4-drop-note-behaviour/plan.md (S5, S8, S11.1-S11.3)
//            specs/profundum-phase4-drop-note-behaviour/tasks.md (T006 wires this TU; T021, T022 fill it)
//
// Holds: SC-010, SC-013, SC-014, SC-015, SC-021, SC-026 (FOLLOW grid); full grids [long] with per-push smoke
// siblings. Includes all five Phase 4 headers with the core and the Anchor to prove they coexist.
//
// T021 (SC-013, SC-026 FOLLOW grid; FR-053-FR-059; C-4, C-5; tasks.md deviation 4):
//   - ProfundumNoteBehaviour_PostDropLandingSmoke (per push): E1, 48 kHz, A {12, 48} x T {60, 200} ms x four
//     Shape anchors x R {0, 1}, LOCK, host block 37, checks (a)-(c) + the 10 s drift; (d) bridged retriggers,
//     Reset zeroing the accumulator, the interrupted-Drop FreeRunning snap, the glide-in-flight record; the
//     bypass negative control; the cornered-ramp C1 positive control; the R-2 probe on E1 and C3 cells.
//   - ProfundumNoteBehaviour_PostDropLanding_E0 / _E1 / _E2 / _C3 / _PunchGhostE1 ([long]): the full SC-013
//     grid per note (A {6, 12, 24, 48} x T {10, 60, 200, 1000} ms x four anchors x R {0, 1} x 44.1 / 48 /
//     96 kHz), one TEST_CASE per shard so the shards run concurrently by name; per-cell CSV in evidence/.
//   - ProfundumNoteBehaviour_FollowAfterDrop ([long], 60 s drift window) + ..._FollowAfterDropSmoke (10 s).
//
// Rulings applied (tasks.md "Rulings applied", spec Clarifications 2026-10-12, O-1 / O-4):
//   - every SC-013 cell is gated at |theta| <= 1 deg; no infeasible set, no gamma_cap. landingFeasible() is
//     asserted true on every cell (the T015 census at component level, repeated on the rendered grid).
//   - SC-013(c): peak |d_c - d| <= 1.05 * Gamma + 1 ulp(A), Gamma = gamma_an + measured carry (0 on the snap
//     cells gated here). gamma_an is computed in this TU from r = n* - Phi_ref(0) (plan S1 C-5): Phi_ref(0) is
//     the double staircase sum over the BYPASS render's currentFrequency() trace, n* the C-4 neighbour (floor
//     or ceil of Phi_ref(0)) with the smaller gamma_an, L(g) the drop-model.js analyticPeak lower bound on
//     4000 midpoints of [0, D_s], bisection on [0, 48]. When L(48) < |r| (gamma_an unbounded) the gate uses
//     the constant gamma_lim = DropPhaseLock::kMaxCorrectionSemitones (48 st, a constant of the component, not
//     a solver output) -- recorded as such in the CSV (gamma_an = -1). correctionGamma() is read ONLY for the
//     C1 segment bounds (plan S11.2) and for the |gamma| record (O-4), never for the magnitude gate.
//   - theta = wrap(goertzelPhase(core L) - goertzelPhase(Anchor L)) over 4 periods of f_res from the landing
//     sample ceil(D_s). The 10 s drift (Phase 3 SC-003 bar 0.5 deg) uses Phase 3 SC-003's own estimator (whole-
//     period windows >= 1 s, as the FOLLOW cell): max_k |wrap(dTheta(t_L + k s) - dTheta(t_L + 1 s))|, k = 2..10.
//     (Fix-up 2026-10-13: the 4-period window read a +-0.45 deg h2-leakage bias as drift.)
//   - R-2 probe: the rendered theta against the staircase prediction 360 * wrap(Phi_lock), Phi_lock the
//     double sum of (currentFrequency() - f_res) / fs over the LOCK render (the C++ mirror of drop-model.js's
//     staircase-exact render; the model's own worst case is 0.218 deg). A gap > 0.3 deg is printed with the
//     tag "SC013-R2-DEFECT" and the cell, for tasks.md "Defects found"; it is not a gate here (plan R-2).
//
// T022 (SC-010, SC-014, SC-015, SC-016, SC-021; FR-060-FR-062, FR-064, FR-068): see the T022 block below for the
// method notes. Per push: ..._RetriggerClickFreeSmoke, _MonoIdentity, _GlideClickFree, _DeclickPositiveControl,
// _Audibility, _TrajectoryNyquistSmoke, _UseCaseRenders. [long] shards by note: _RetriggerClickFree_E0/_E1/_E2/_C3,
// _GlideClickFree_E0/_E1/_E2/_C3, and _TrajectoryNyquist (full SC-015 grid). Evidence: evidence/sc015-*.csv,
// evidence/usecase-*.{wav,csv}.
// ==============================================================================

#include "profundum_note_behaviour_test_helpers.h"

#include <krate/dsp/systems/profundum_core.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/drop_curve.h>
#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/primitives/retrigger_declick.h>
#include <krate/dsp/processors/drop_envelope.h>
#include <krate/dsp/processors/drop_phase_lock.h>
#include <krate/dsp/processors/note_memory.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

using Krate::DSP::AnchorOscillator;
using Krate::DSP::DropCurve;
using Krate::DSP::DropPhaseLock;
using Krate::DSP::GlideMode;
using Krate::DSP::NoteMemory;
using Krate::DSP::ProfundumCore;
namespace PT = Krate::DSP::ProfundumTest;
namespace DT = Krate::DSP::DropTest;

namespace {

// ---- grid ---------------------------------------------------------------------

constexpr int kSpE0 = 16;
constexpr int kSpE1 = 28;
constexpr int kSpE2 = 40;
constexpr int kSpC3 = 48;
constexpr std::array<double, 3> kSpRates{44100.0, 48000.0, 96000.0};
constexpr std::array<float, 4> kSpAmounts{6.0f, 12.0f, 24.0f, 48.0f};
constexpr std::array<float, 4> kSpTimes{10.0f, 60.0f, 200.0f, 1000.0f};
constexpr std::array<double, 4> kSpShapes{0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0};
constexpr std::array<float, 2> kSpRebounds{0.0f, 1.0f};
constexpr double kSpSmokeFs = 48000.0;
constexpr std::size_t kSpHostBlock = 37;
constexpr std::size_t kSpU = ProfundumCore::kPitchUpdateInterval;

// ---- bars ---------------------------------------------------------------------

constexpr double kSpThetaBarDeg = 1.0;         // SC-013(a)
constexpr double kSpDriftBarDeg = 0.5;         // Phase 3 SC-003 drift bar
constexpr double kSpGammaTolerance = 1.05;     // SC-013(c)
constexpr double kSpNegativeControlDeg = 90.0; // SC-013 negative control
constexpr double kSpModelThetaDeg = 0.22;      // drop-model.js lock3 4 worst case (plan C-3, R-2)
constexpr double kSpR2GapDeg = 0.3;            // plan R-2 investigation threshold
constexpr double kSpRadToDeg = 57.295779513082320876798154814105;

static_assert(ProfundumCore::kMinF0Hz == AnchorOscillator::kMinF0Hz);

/// The largest float at or below s (float(2/3) rounds up into the elastic segment; drop_envelope_test.cpp).
float spShapeParam(double s) {
    float f = static_cast<float>(s);
    if (static_cast<double>(f) > s)
        f = std::nextafter(f, std::numeric_limits<float>::lowest());
    return f;
}

const char* spModeName(AnchorOscillator::Mode m) {
    switch (m) {
        case AnchorOscillator::Mode::Lock:
            return "LOCK";
        case AnchorOscillator::Mode::Follow:
            return "FOLLOW";
        case AnchorOscillator::Mode::Punch:
            return "PUNCH";
        case AnchorOscillator::Mode::Ghost:
            return "GHOST";
    }
    return "?";
}

const char* spNoteName(int note) {
    switch (note) {
        case kSpE0:
            return "E0";
        case kSpE1:
            return "E1";
        case kSpE2:
            return "E2";
        case kSpC3:
            return "C3";
        default:
            return "note";
    }
}

AnchorOscillator::Controls spAnchor(AnchorOscillator::Mode mode) {
    return AnchorOscillator::Controls{.waveform = AnchorOscillator::Waveform::Sine, .mode = mode, .level = 1.0f};
}

std::unique_ptr<PT::NoteBehaviourHarness> spHarness(double fs) {
    auto h = std::make_unique<PT::NoteBehaviourHarness>();
    h->prepare(fs);
    return h;
}

std::size_t spSamples(double seconds, double fs) { return PT::noteBehaviourSeconds(seconds, fs); }

/// round(4 fs / f): the SC-013(a) Goertzel window.
std::size_t spFourPeriods(double fs, double f) { return static_cast<std::size_t>(std::llround(4.0 * fs / f)); }

double spWrapDeg(double d) {
    d = std::fmod(d, 360.0);
    if (d > 180.0)
        d -= 360.0;
    if (d <= -180.0)
        d += 360.0;
    return d;
}

/// theta = wrap(goertzelPhase(core) - goertzelPhase(Anchor)) in degrees over [start, start + L).
double spThetaDeg(const PT::NoteBehaviourTraces& tr, std::size_t start, std::size_t L, double fs, double hz) {
    REQUIRE(start + L <= tr.length);
    const double tc = PT::goertzelPhase(tr.coreL.data() + start, L, fs, hz);
    const double ta = PT::goertzelPhase(tr.anchorL.data() + start, L, fs, hz);
    return PT::noteBehaviourWrapRad(tc - ta) * kSpRadToDeg;
}

/// One float ulp at |x|.
double spFloatUlp(double x) {
    const auto f = static_cast<float>(std::abs(x));
    return static_cast<double>(std::nextafter(f, std::numeric_limits<float>::max())) - static_cast<double>(f);
}

/// The core's held f0 for one float d_c: clamp(float(f_res * 2^{d_c/12}), minF, cap), the exponent in double
/// behind the opaqueDoubleBits barrier (MSVC /fp:fast otherwise narrows the all-float-leaf expression;
/// drop_phase_lock_test.cpp heldHz).
float spHeldHz(float dc, float fRes, float minF, float cap) {
    const double x = std::bit_cast<double>(Krate::DSP::detail::opaqueDoubleBits(static_cast<double>(dc)));
    const float f = static_cast<float>(static_cast<double>(fRes) * std::exp2(x / 12.0));
    return std::clamp(f, minF, cap);
}

/// The S5.6 staircase mirror over trace samples [begin, end) for a trigger at `begin` with grid phase
/// `gridPhase`: the hold is refreshed at `begin` and at every n with (n - begin - gridPhase) % 16 == 0.
double spSegmentExcess(const std::vector<float>& dc, std::size_t begin, std::size_t end, float fRes, double fs,
                       float cap, std::size_t gridPhase) {
    const float minF = ProfundumCore::kMinF0Hz;
    float h = spHeldHz(dc[begin], fRes, minF, cap);
    double sum = 0.0;
    for (std::size_t n = begin; n < end; ++n) {
        const std::size_t k = n - begin;
        if (k >= gridPhase && (k - gridPhase) % kSpU == 0)
            h = spHeldHz(dc[n], fRes, minF, cap);
        sum += (static_cast<double>(h) - static_cast<double>(fRes)) / fs;
    }
    return sum;
}

/// Double staircase sum of the core's excess phase from its currentFrequency() trace over [begin, end).
double spCoreFreqExcess(const PT::NoteBehaviourTraces& tr, std::size_t begin, std::size_t end, double fRes,
                        double fs) {
    double sum = 0.0;
    for (std::size_t n = begin; n < end; ++n)
        sum += (static_cast<double>(tr.coreFreq[n]) - fRes) / fs;
    return sum;
}

/// b(x) = 16 x^2 (1 - x)^2 on (0, 1), else 0 (plan S1 C-5).
double spBump(double x) { return (x > 0.0 && x < 1.0) ? 16.0 * x * x * (1.0 - x) * (1.0 - x) : 0.0; }

/// plan S1 C-5 gamma_an (O-1 ruling: no gamma_cap): inf{g : L(g) >= |r|} by bisection on [0, 48]; -1 when
/// L(48) < |r| (unbounded; a sentinel, this TU builds under fast-math). L(g) is drop-model.js analyticPeak
/// (drop-model.js:483) on 4000 midpoints of [0, D_s]: unclamped at g, convexity (r > 0) or concavity (r < 0)
/// with beta in {1/4, 1/2, 3/4}; pushed through a clamp by g, the exact saturated move; already clamped at
/// g = 0, nothing. Built only from r, f_res, D_s, DropCurve::valueAt and b: it never reads the solver.
double spGammaAnalytic(const DropCurve& c, double fRes, double fs, double minF, double cap, double r) {
    const double ar = std::abs(r);
    if (ar == 0.0)
        return 0.0;
    const double D = c.endSample;
    if (!(D > 0.0))
        return -1.0;
    constexpr int kM = 4000;
    constexpr std::array<double, 3> kBetas{0.25, 0.5, 0.75};
    const double dt = D / static_cast<double>(kM);   // samples
    const double sgn = r > 0.0 ? 1.0 : -1.0;
    const double ln2Over12 = std::log(2.0) / 12.0;
    std::vector<double> bs(static_cast<std::size_t>(kM));
    std::vector<double> f0s(static_cast<std::size_t>(kM));
    for (std::size_t i = 0; i < bs.size(); ++i) {
        const double t = (static_cast<double>(i) + 0.5) * dt;
        bs[i] = spBump(t / D);
        f0s[i] = fRes * std::exp2(c.valueAt(t) / 12.0);
    }
    const auto L = [&](double g) {
        double sum = 0.0;
        for (std::size_t i = 0; i < bs.size(); ++i) {
            const double f0 = f0s[i];
            const double b = bs[i];
            if (f0 < minF || f0 > cap)
                continue;
            const double fg = f0 * std::exp2(sgn * g * b / 12.0);
            if (fg < minF || fg > cap) {
                sum += std::abs(std::clamp(fg, minF, cap) - f0) * dt;
                continue;
            }
            double m = 0.0;
            if (sgn > 0.0) {
                m = g * b * ln2Over12;
                for (const double be : kBetas) {
                    if (b >= be)
                        m = std::max(m, std::exp2(g * be / 12.0) - 1.0);
                }
            } else {
                m = b * (1.0 - std::exp2(-g / 12.0));
                for (const double be : kBetas) {
                    if (b >= be)
                        m = std::max(m, 1.0 - std::exp2(-g * be / 12.0));
                }
            }
            sum += f0 * m * dt;
        }
        return sum / fs;   // cycles
    };
    const double lim = DropPhaseLock::kMaxCorrectionSemitones;
    if (L(lim) < ar)
        return -1.0;
    double lo = 0.0;
    double hi = lim;
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (L(mid) >= ar)
            hi = mid;
        else
            lo = mid;
    }
    return hi;
}

/// Float allowance on |Δ²d_c| (drop_phase_lock_test.cpp correctedAllowance): d is a float of the closed form
/// and d_c a float of double(d) + c, so a second difference carries <= 2 ulp of each.
double spCorrectedAllowance(const std::vector<double>& g, const std::vector<double>& d, std::size_t lo,
                            std::size_t hi) {
    const std::size_t a = lo > 0 ? lo - 1 : 0;
    const std::size_t b = std::min(hi + 1, g.size() - 1);
    double mg = 0.0;
    double md = 0.0;
    for (std::size_t n = a; n <= b; ++n) {
        mg = std::max(mg, std::abs(g[n]));
        md = std::max(md, std::abs(d[n]));
    }
    return 2.0 * spFloatUlp(mg) + 2.0 * spFloatUlp(md) + 4e-9;
}

/// plan S11.2 segments for d_c = d + c of one snap plan starting at trace sample 0 (no carry): breaks on the
/// first integer sample of every piece and of D_s; each bound is the largest overlapping pieceD2Bound +
/// |gamma| 32 / D_s² where the bump is live (max|b''| = 32, C-5) + the float allowance.
std::vector<DT::DropC1Segment> spCorrectedSegments(const std::vector<double>& g, const std::vector<double>& d,
                                                   const DropCurve& c, double gammaAbs) {
    const std::size_t stop = g.size() - 1;
    const auto clampBreak = [&](double x) {
        return static_cast<std::size_t>(std::clamp(std::ceil(x), 0.0, static_cast<double>(stop)));
    };
    std::vector<std::size_t> br{0, stop, clampBreak(c.endSample)};
    for (int j = 0; j < c.pieceCount(); ++j) {
        br.push_back(clampBreak(c.pieceStart(j)));
        br.push_back(clampBreak(c.pieceEnd(j)));
    }
    std::sort(br.begin(), br.end());
    br.erase(std::unique(br.begin(), br.end()), br.end());
    const double Ds = c.endSample;
    std::vector<DT::DropC1Segment> segs;
    for (std::size_t k = 0; k + 1 < br.size(); ++k) {
        const std::size_t lo = br[k];
        const std::size_t hi = br[k + 1];
        double bound = 0.0;
        for (int j = 0; j < c.pieceCount(); ++j) {
            const double a = std::max(c.pieceStart(j), static_cast<double>(lo));
            const double b = std::min(c.pieceEnd(j), static_cast<double>(hi));
            if (a < b)
                bound = std::max(bound, c.pieceD2Bound(j));
        }
        if (Ds > 0.0 && static_cast<double>(lo) < Ds)
            bound += gammaAbs * 32.0 / (Ds * Ds);
        segs.push_back(DT::DropC1Segment{lo, hi - lo, bound + spCorrectedAllowance(g, d, lo, hi)});
    }
    return segs;
}

/// Mid-fall / mid-rebound / mid-elastic retrigger positions on a snap curve (drop_phase_lock_test.cpp
/// midPosition).
enum class SpMid : std::uint8_t { Fall, Rebound, Elastic };
std::size_t spMidPosition(const DropCurve& c, SpMid where) {
    const double N = c.fallSamples;
    switch (where) {
        case SpMid::Fall:
            return static_cast<std::size_t>(std::llround(0.3 * N));
        case SpMid::Rebound: {
            double next = c.knotT[0];
            for (const double t : c.knotT) {
                if (t > c.knotT[0]) {
                    next = t;
                    break;
                }
            }
            return static_cast<std::size_t>(std::llround(c.fallStart + 0.5 * (c.knotT[0] + next)));
        }
        case SpMid::Elastic:
            return static_cast<std::size_t>(std::llround(1.3 * N));
    }
    return 0;
}

/// The snap curve DropEnvelope latches for these (in-range) parameters (drop_envelope.h trigger()).
DropCurve spSnapCurve(float A, float T, float shape, float R, double fs) {
    return Krate::DSP::makeDropCurve(static_cast<double>(A), static_cast<double>(T), static_cast<double>(shape),
                                     static_cast<double>(R), fs, false, 0.0, 0.0);
}

// ---- one SC-013 cell ----------------------------------------------------------

struct SpCell {
    int note = kSpE1;
    double fs = kSpSmokeFs;
    float A = 12.0f;
    float T = 60.0f;
    double s = 0.0;
    float R = 0.0f;
    AnchorOscillator::Mode mode = AnchorOscillator::Mode::Lock;
};

struct SpCellResult {
    double theta = 0.0;        // deg, LOCK at landing
    double thetaBypass = 0.0;  // deg, lock bypassed (negative control)
    double drift = -1.0;       // deg, max over the 10 s; -1 when not measured
    double thetaStair = 0.0;   // deg, R-2 staircase prediction
    double r2Gap = 0.0;        // deg
    double gammaAbs = 0.0;     // |correctionGamma()| (recorded, O-4)
    double gammaAn = 0.0;      // st; -1 = unbounded
    double gate = 0.0;         // Gamma used by SC-013(c)
    double peak = 0.0;         // measured peak |d_c - d|
    double residual = 0.0;     // solveResidualCycles()
    double r = 0.0;            // n* - Phi_ref(0)
    bool feasible = true;
    std::size_t landing = 0;
    PT::NoteBehaviourAnchorDiff anchorDiff;   // SC-026 ruling (A): Anchor output, LOCK vs bypass (a1-scaled gated, raw recorded)
};

// Phase 3 SC-003 lock-window helpers (defined with the SC-026 FOLLOW cell below).
std::size_t spLockWindowLength(double f0, double fs);
std::size_t spCentredStart(double t, double f0, double fs);
double spDeltaThetaDeg(const std::vector<float>& anchor, const std::vector<float>& core, std::size_t start,
                       double hz, double fs);

/// Renders one SC-013 cell twice (lock bypassed, then LOCK) on fresh harnesses and applies checks (a)-(c).
/// driftSeconds = 0 skips the drift window (R-2 probe cells).
SpCellResult spRunLandingCell(const SpCell& c, int driftSeconds, PT::NoteBehaviourTraces& tr,
                              PT::NoteBehaviourTraces& by) {
    const float sp = spShapeParam(c.s);
    INFO(spModeName(c.mode) << " " << spNoteName(c.note) << " fs " << c.fs << " A " << c.A << " T " << c.T
                            << " ms s " << c.s << " R " << c.R);
    const DropCurve curve = spSnapCurve(c.A, c.T, sp, c.R, c.fs);
    REQUIRE(curve.endSample > 0.0);
    SpCellResult res;
    const auto landing = static_cast<std::size_t>(std::ceil(curve.endSample));
    res.landing = landing;
    const float fApprox = Krate::DSP::midiNoteToFrequency(c.note);   // render lengths only
    const std::size_t L4 = spFourPeriods(c.fs, static_cast<double>(fApprox));
    const std::size_t head = landing + 2 * kSpU + L4 + 1;
    // The drift windows are Phase 3 SC-003's (whole periods, >= 1 s), centred at landing + k s for k = 1..driftSeconds.
    std::size_t total = head;
    if (driftSeconds > 0) {
        const double tLast = static_cast<double>(landing) / c.fs + static_cast<double>(driftSeconds);
        total = std::max(total, spCentredStart(tLast, static_cast<double>(fApprox), c.fs)
                                    + spLockWindowLength(static_cast<double>(fApprox), c.fs) + 1);
    }

    const auto render = [&](bool bypass, PT::NoteBehaviourTraces& t, std::size_t n) {
        auto h = spHarness(c.fs);
        h->setAnchorControls(spAnchor(c.mode));
        h->setDrop(c.A, c.T, sp, c.R);
        h->setLockBypass(bypass);
        t.allocate(n);
        h->noteOn(c.note, 100, 0);
        h->render(n, kSpHostBlock, t);
        REQUIRE(h->contractViolations() == 0);
        REQUIRE(t.numOutcomes == 1);
        REQUIRE(t.recorded()[0].effectiveSample == 0);
        REQUIRE(t.recorded()[0].lockKind == DropPhaseLock::TriggerKind::SnapReset);
        REQUIRE(h->drop().plannedCurve().endSample == curve.endSample);
        REQUIRE(std::abs(t.fRes[0] / fApprox - 1.0f) <= 1e-5f);
        return h;
    };

    // Bypass render: Phi_ref(0), the Anchor reference, the negative control.
    (void)render(true, by, head);
    CHECK(PT::samplesBitEqual(by.dc.data(), by.d.data(), by.length));

    // LOCK render.
    auto h = render(false, tr, total);
    const float cap = h->capHz();
    const float fRes = tr.fRes[0];   // MonoHandler output (no glide: constant)
    REQUIRE(tr.fRes[landing] == fRes);
    const double f = static_cast<double>(fRes);
    const float fHeld = std::clamp(fRes, ProfundumCore::kMinF0Hz, cap);

    // (a) theta at landing, drift over the following seconds.
    res.theta = spThetaDeg(tr, landing, L4, c.fs, f);
    res.thetaBypass = spThetaDeg(by, landing, L4, c.fs, f);
    CHECK(std::abs(res.theta) <= kSpThetaBarDeg);
    if (driftSeconds > 0) {
        // Phase 3 SC-003's drift bound with Phase 3's estimator: |dTheta(t) - dTheta(t_L + 1 s)| on whole-period
        // windows of >= 1 s (spDeltaThetaDeg, as the FOLLOW cell). A 4-period BH7 window is NOT that estimator: the
        // core's h2 lies 4 bins from h1, inside the BH7 main lobe (+-7 bins), so its phase reads a waveform-phase-
        // dependent bias of about +-0.45 deg that moves with the window start (measured 2026-10-13: theta at landing
        // is constant per (T, R) across A and Shape, and the 4-period "drift" spans that bias).
        const double tL = static_cast<double>(landing) / c.fs;
        const double th1 = spDeltaThetaDeg(tr.anchorL, tr.coreL, spCentredStart(tL + 1.0, f, c.fs), f, c.fs);
        double worst = 0.0;
        for (int k = 2; k <= driftSeconds; ++k) {
            const double tk = spDeltaThetaDeg(tr.anchorL, tr.coreL,
                                              spCentredStart(tL + static_cast<double>(k), f, c.fs), f, c.fs);
            worst = std::max(worst, std::abs(spWrapDeg(tk - th1)));
        }
        res.drift = worst;
        CHECK(worst <= kSpDriftBarDeg);
    }

    // (b) no pitch motion after landing; the Anchor untouched by the correction.
    std::size_t dcNonZero = 0;
    for (std::size_t n = landing; n < tr.length; ++n) {
        if (std::bit_cast<std::uint32_t>(tr.dc[n]) != 0u)
            ++dcNonZero;
    }
    CHECK(dcNonZero == 0);
    const std::size_t firstUpdate = ((landing + kSpU - 1) / kSpU) * kSpU;   // the grid restarts at the noteOn (0)
    std::size_t freqOff = 0;
    for (std::size_t n = firstUpdate; n < tr.length; ++n) {
        if (!(tr.coreFreq[n] == fHeld))
            ++freqOff;
    }
    CHECK(freqOff == 0);
    // SC-026 ruling (A) (spec Clarifications 2026-10-13 "T023 epoch 6 readings"): pitch, phase() and mode gain
    // bit-identical; the output equals the bypassed output times a1_on / a1_off (the level feed divided out) within
    // float rounding (<= -120 dB re peak). The raw output difference is recorded per cell, not gated.
    REQUIRE(by.length == head);
    CHECK(std::memcmp(tr.fAnchor.data(), by.fAnchor.data(), head * sizeof(float)) == 0);
    CHECK(std::memcmp(tr.anchorInc.data(), by.anchorInc.data(), head * sizeof(double)) == 0);
    CHECK(std::memcmp(tr.anchorPhase.data(), by.anchorPhase.data(), head * sizeof(double)) == 0);
    CHECK(std::memcmp(tr.modeGain.data(), by.modeGain.data(), head * sizeof(double)) == 0);
    res.anchorDiff = PT::noteBehaviourAnchorOutputDiff(tr, by, head);
    INFO("Anchor output LOCK vs bypass, a1 divided out: max " << res.anchorDiff.scaledMaxDiff << " re peak "
                                                               << res.anchorDiff.peak << " = " << res.anchorDiff.scaledDb
                                                               << " dB (bar " << PT::kNoteBehaviourAnchorScaledBarDb
                                                               << "); raw " << res.anchorDiff.db << " dB (recorded)");
    CHECK(res.anchorDiff.peak > 0.0);   // non-vacuous: the Anchor sounded
    CHECK(res.anchorDiff.scaledDb <= PT::kNoteBehaviourAnchorScaledBarDb);

    // (c) the deviation against the solver-independent Gamma (plan S1 C-5, O-1 ruling).
    const double phiRef = spCoreFreqExcess(by, 0, head, f, c.fs);
    const double nLo = std::floor(phiRef);
    const double nHi = std::ceil(phiRef);
    const double minF = static_cast<double>(ProfundumCore::kMinF0Hz);
    const double capD = static_cast<double>(cap);
    const double gLo = spGammaAnalytic(curve, f, c.fs, minF, capD, nLo - phiRef);
    const double gHi = nHi != nLo ? spGammaAnalytic(curve, f, c.fs, minF, capD, nHi - phiRef) : gLo;
    // C-4 on the test side: the neighbour with the smaller (finite) gamma_an.
    const bool takeHi = ((gLo < 0.0) != (gHi < 0.0)) ? (gHi >= 0.0) : (gLo >= 0.0 && gHi < gLo);
    const double nStar = takeHi ? nHi : nLo;
    res.gammaAn = takeHi ? gHi : gLo;
    res.r = nStar - phiRef;
    CHECK(std::abs(res.r) < 1.0);
    const double lim = DropPhaseLock::kMaxCorrectionSemitones;
    constexpr double kCarry = 0.0;   // snap cells: no bridged carry (S5.5)
    res.gate = (res.gammaAn >= 0.0 ? std::min(res.gammaAn, lim) : lim) + kCarry;
    for (std::size_t n = 0; n < head; ++n)
        res.peak = std::max(res.peak, std::abs(static_cast<double>(tr.dc[n]) - static_cast<double>(tr.d[n])));
    const DropPhaseLock& lock = h->lock();
    res.gammaAbs = std::abs(lock.correctionGamma());
    res.residual = lock.solveResidualCycles();
    res.feasible = lock.landingFeasible();
    INFO("Phi_ref(0) " << phiRef << " n* " << nStar << " r " << res.r << " gamma_an " << res.gammaAn << " Gamma "
                       << res.gate << " peak " << res.peak << " |gamma| " << res.gammaAbs << " |gamma|/Gamma "
                       << (res.gate > 0.0 ? res.gammaAbs / res.gate : 0.0) << " residual " << res.residual
                       << " target " << lock.targetCycles());
    CHECK(res.peak <= kSpGammaTolerance * res.gate + spFloatUlp(static_cast<double>(c.A)));
    CHECK(res.feasible);   // O-1 / O-4: every SC-013 cell has a root in [-48, 48]

    // C1 of d_c with the S11.2 bounds (gamma from correctionGamma(): smoothness, not magnitude).
    {
        const std::vector<double> g(tr.dc.begin(), tr.dc.begin() + static_cast<std::ptrdiff_t>(head));
        const std::vector<double> d(tr.d.begin(), tr.d.begin() + static_cast<std::ptrdiff_t>(head));
        const auto segs = spCorrectedSegments(g, d, curve, res.gammaAbs);
        const DT::DropC1Report rep = DT::dropCheckC1(g, segs, -1e6, 1e6);
        INFO("C1 worst ratio " << rep.worstRatio << " worst boundary jump " << rep.worstBoundaryJump);
        CHECK(rep.pass);
    }

    // R-2 probe: rendered theta against the staircase prediction.
    const double phiLock = spCoreFreqExcess(tr, 0, head, f, c.fs);
    res.thetaStair = (phiLock - std::round(phiLock)) * 360.0;
    res.r2Gap = std::abs(spWrapDeg(res.theta - res.thetaStair));
    if (res.r2Gap > kSpR2GapDeg) {
        std::printf("SC013-R2-DEFECT %s %s fs %.0f A %.0f T %.0f s %.4f R %.0f: rendered theta %+.6f deg, staircase "
                    "prediction %+.6f deg, gap %.6f deg (> %.1f; model worst %.2f)\n",
                    spModeName(c.mode), spNoteName(c.note), c.fs, static_cast<double>(c.A), static_cast<double>(c.T),
                    c.s, static_cast<double>(c.R), res.theta, res.thetaStair, res.r2Gap, kSpR2GapDeg,
                    kSpModelThetaDeg);
    }
    return res;
}

void spPrintCell(const SpCell& c, const SpCellResult& r) {
    std::printf("SC-013 %s %s fs %.0f A %2.0f T %4.0f s %.4f R %.0f: theta %+.6f deg (bar %.1f), drift %.6f deg "
                "(bar %.1f), bypass theta %+.3f deg, |gamma| %.4f st, gamma_an %.4f, Gamma %.4f, peak |c| %.6f "
                "(bar %.6f), residual %.3e, R-2 gap %.4f deg, Anchor output a1-scaled diff %.2f dB "
                "(bar %.0f), raw diff %.2f dB (max %.3e re peak %.4f; recorded), unchecked %zu\n",
                spModeName(c.mode), spNoteName(c.note), c.fs, static_cast<double>(c.A), static_cast<double>(c.T), c.s,
                static_cast<double>(c.R), r.theta, kSpThetaBarDeg, r.drift, kSpDriftBarDeg, r.thetaBypass, r.gammaAbs,
                r.gammaAn, r.gate, r.peak, kSpGammaTolerance * r.gate + spFloatUlp(static_cast<double>(c.A)),
                r.residual, r.r2Gap, r.anchorDiff.scaledDb, PT::kNoteBehaviourAnchorScaledBarDb, r.anchorDiff.db,
                r.anchorDiff.maxDiff, r.anchorDiff.peak, r.anchorDiff.unchecked);
}

std::string spEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase4-drop-note-behaviour/evidence/";
}

double spElapsedSeconds(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

/// One [long] SC-013 shard: the full grid for `note` at three rates in each of `modes`; per-cell CSV.
void spRunLandingShard(int note, std::initializer_list<AnchorOscillator::Mode> modes, const char* csvName) {
    const auto t0 = std::chrono::steady_clock::now();
    PT::NoteBehaviourTraces tr;
    PT::NoteBehaviourTraces by;
    std::ofstream csv(spEvidenceDir() + csvName);
    CHECK(csv.is_open());
    csv << "mode,note,fs,A,T_ms,s,R,theta_deg,theta_bypass_deg,drift_deg,gamma_abs_st,gamma_an_st,Gamma_st,"
           "peak_c_st,gamma_over_Gamma,feasible,residual_cycles,r_cycles,theta_stair_deg,r2_gap_deg,anchor_diff_db,"
           "anchor_diff_max,anchor_peak,anchor_scaled_diff_db\n";
    std::array<double, kSpTimes.size()> worstGammaByT{};
    double worstTheta = 0.0;
    double worstDrift = 0.0;
    double worstBypass = 0.0;
    double worstGap = 0.0;
    double worstAnchorDiffDb = -400.0;
    double worstAnchorScaledDb = -400.0;
    std::size_t cells = 0;
    for (const AnchorOscillator::Mode mode : modes) {
        for (const double fs : kSpRates) {
            for (const float A : kSpAmounts) {
                for (std::size_t ti = 0; ti < kSpTimes.size(); ++ti) {
                    for (const double s : kSpShapes) {
                        for (const float R : kSpRebounds) {
                            const SpCell c{.note = note, .fs = fs, .A = A, .T = kSpTimes[ti], .s = s, .R = R,
                                           .mode = mode};
                            const SpCellResult r = spRunLandingCell(c, 10, tr, by);
                            spPrintCell(c, r);
                            ++cells;
                            worstGammaByT[ti] = std::max(worstGammaByT[ti], r.gammaAbs);
                            worstTheta = std::max(worstTheta, std::abs(r.theta));
                            worstDrift = std::max(worstDrift, r.drift);
                            worstBypass = std::max(worstBypass, std::abs(r.thetaBypass));
                            worstGap = std::max(worstGap, r.r2Gap);
                            worstAnchorDiffDb = std::max(worstAnchorDiffDb, r.anchorDiff.db);
                            worstAnchorScaledDb = std::max(worstAnchorScaledDb, r.anchorDiff.scaledDb);
                            char line[512];
                            std::snprintf(line, sizeof(line),
                                          "%s,%s,%.0f,%.0f,%.0f,%.6f,%.0f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.9f,%.6f,%d,"
                                          "%.3e,%.6f,%.6f,%.6f,%.2f,%.3e,%.6f,%.2f\n",
                                          spModeName(mode), spNoteName(note), fs, static_cast<double>(A),
                                          static_cast<double>(kSpTimes[ti]), s, static_cast<double>(R), r.theta,
                                          r.thetaBypass, r.drift, r.gammaAbs, r.gammaAn, r.gate, r.peak,
                                          r.gate > 0.0 ? r.gammaAbs / r.gate : 0.0, r.feasible ? 1 : 0, r.residual,
                                          r.r, r.thetaStair, r.r2Gap, r.anchorDiff.db, r.anchorDiff.maxDiff,
                                          r.anchorDiff.peak, r.anchorDiff.scaledDb);
                            csv << line;
                        }
                    }
                }
            }
        }
    }
    // Negative control: without the correction at least one cell misses by more than 90 deg.
    CHECK(worstBypass > kSpNegativeControlDeg);
    for (std::size_t ti = 0; ti < kSpTimes.size(); ++ti) {
        std::printf("SC-013 shard %s: T %4.0f ms max |gamma| %.4f st (O-4, recorded)\n", csvName,
                    static_cast<double>(kSpTimes[ti]), worstGammaByT[ti]);
    }
    std::printf("SC-013 shard %s: %zu cells, worst |theta| %.6f deg (bar %.1f), worst drift %.6f deg (bar %.1f), "
                "worst bypass |theta| %.3f deg (negative control > %.0f), worst R-2 gap %.4f deg, worst Anchor output raw diff "
                "%.2f dB (SC-026, recorded), worst a1-scaled diff %.2f dB (bar %.0f), wall clock %.1f s\n",
                csvName, cells, worstTheta, kSpThetaBarDeg, worstDrift, kSpDriftBarDeg, worstBypass,
                kSpNegativeControlDeg, worstGap, worstAnchorDiffDb, worstAnchorScaledDb,
                PT::kNoteBehaviourAnchorScaledBarDb,
                spElapsedSeconds(t0));
}

// ---- SC-026 FOLLOW ------------------------------------------------------------

/// Phase 3 lock window (profundum_anchor_chain_test.cpp:65-67): round(ceil(f0) fs / f0) samples (>= 1 s).
std::size_t spLockWindowLength(double f0, double fs) {
    return static_cast<std::size_t>(std::llround(std::ceil(f0) * fs / f0));
}

/// Start of the lock window centred at t seconds (profundum_anchor_chain_test.cpp:70-75).
std::size_t spCentredStart(double t, double f0, double fs) {
    const auto centre = static_cast<std::size_t>(std::llround(t * fs));
    const std::size_t half = spLockWindowLength(f0, fs) / 2;
    REQUIRE(centre >= half);
    return centre - half;
}

/// Phase 3 SC-003 delta-theta: wrap(theta_anchor - theta_core) in degrees over one lock window at `start`.
double spDeltaThetaDeg(const std::vector<float>& anchor, const std::vector<float>& core, std::size_t start,
                       double hz, double fs) {
    const std::size_t L = spLockWindowLength(hz, fs);
    REQUIRE(start + L <= anchor.size());
    REQUIRE(start + L <= core.size());
    const double ta = PT::goertzelPhase(anchor.data() + start, L, fs, hz);
    const double tc = PT::goertzelPhase(core.data() + start, L, fs, hz);
    return spWrapDeg((ta - tc) * kSpRadToDeg);
}

struct SpFollowResult {
    double dTheta1 = 0.0;
    double dThetaW = 0.0;
    double drift = 0.0;
};

/// SC-026 FOLLOW (r = 1) cell at E1, 48 kHz: d_c bitwise d at every sample; after t_s, |dTheta(t_s + 1 s)| <= 1
/// and |dTheta(t_s + W s) - dTheta(t_s + 1 s)| <= 0.5 deg. Rendered in 1 s chunks through one reused trace set.
SpFollowResult spRunFollowCell(float A, float T, double s, float R, double windowSeconds) {
    const double fs = kSpSmokeFs;
    const float sp = spShapeParam(s);
    INFO("FOLLOW E1 A " << A << " T " << T << " ms s " << s << " R " << R << " window " << windowSeconds << " s");
    const DropCurve curve = spSnapCurve(A, T, sp, R, fs);
    const double tS = std::ceil(curve.endSample) / fs;
    const double fApprox = static_cast<double>(Krate::DSP::midiNoteToFrequency(kSpE1));   // window placement
    const std::size_t start1 = spCentredStart(tS + 1.0, fApprox, fs);
    const std::size_t startW = spCentredStart(tS + windowSeconds, fApprox, fs);
    const std::size_t total = startW + spLockWindowLength(fApprox, fs) + 1;
    const std::size_t chunk = spSamples(1.0, fs);

    auto h = spHarness(fs);
    h->setAnchorControls(spAnchor(AnchorOscillator::Mode::Follow));
    h->setDrop(A, T, sp, R);
    h->noteOn(kSpE1, 100, 0);
    PT::NoteBehaviourTraces tr;
    tr.allocate(chunk);
    std::vector<float> core(total, 0.0f);
    std::vector<float> anchor(total, 0.0f);
    std::size_t dcMismatch = 0;
    double f = fApprox;
    for (std::size_t pos = 0; pos < total; pos += chunk) {
        const std::size_t n = std::min(chunk, total - pos);
        tr.length = 0;
        h->render(n, kSpHostBlock, tr);
        REQUIRE(tr.length == n);
        if (pos == 0)
            f = static_cast<double>(tr.fRes[0]);   // the MonoHandler f_res (no glide: constant)
        if (!PT::samplesBitEqual(tr.dc.data(), tr.d.data(), n))
            ++dcMismatch;
        const auto at = static_cast<std::ptrdiff_t>(pos);
        std::copy_n(tr.coreL.begin(), n, core.begin() + at);
        std::copy_n(tr.anchorL.begin(), n, anchor.begin() + at);
    }
    REQUIRE(h->contractViolations() == 0);
    REQUIRE(tr.numOutcomes == 1);
    REQUIRE(h->drop().plannedCurve().endSample == curve.endSample);
    CHECK(dcMismatch == 0);   // FR-058: c == 0 in FOLLOW

    SpFollowResult r;
    r.dTheta1 = spDeltaThetaDeg(anchor, core, start1, f, fs);
    r.dThetaW = spDeltaThetaDeg(anchor, core, startW, f, fs);
    r.drift = spWrapDeg(r.dThetaW - r.dTheta1);
    CHECK(std::abs(r.dTheta1) <= kSpThetaBarDeg);
    CHECK(std::abs(r.drift) <= kSpDriftBarDeg);
    std::printf("SC-026 FOLLOW E1 A %2.0f T %4.0f s %.4f R %.0f: dTheta(t_s + 1 s) %+.6f deg (bar %.1f), "
                "dTheta(t_s + %.0f s) %+.6f deg, drift %+.6f deg (bar %.1f)\n",
                static_cast<double>(A), static_cast<double>(T), s, static_cast<double>(R), r.dTheta1, kSpThetaBarDeg,
                windowSeconds, r.dThetaW, r.drift, kSpDriftBarDeg);
    return r;
}

}  // namespace

// =============================================================================
// T021: SC-013 post-Drop landing
// =============================================================================

TEST_CASE("ProfundumNoteBehaviour_PostDropLandingSmoke", "[systems][profundum][note-behaviour]") {
    const auto t0 = std::chrono::steady_clock::now();
    PT::NoteBehaviourTraces tr;
    PT::NoteBehaviourTraces by;
    constexpr std::array<float, 2> kSmokeAmounts{12.0f, 48.0f};
    constexpr std::array<float, 2> kSmokeTimes{60.0f, 200.0f};

    SECTION("(a)-(c) grid at E1, the negative control and the R-2 probe on E1 and C3") {
        double worstBypass = 0.0;
        double worstGap = 0.0;
        for (const int note : {kSpE1, kSpC3}) {
            // E1 carries the full (a)-(c) checks with the 10 s drift; C3 is the R-2 probe (landing only).
            const int drift = note == kSpE1 ? 10 : 0;
            for (const float A : kSmokeAmounts) {
                for (const float T : kSmokeTimes) {
                    for (const double s : kSpShapes) {
                        for (const float R : kSpRebounds) {
                            const SpCell c{.note = note, .fs = kSpSmokeFs, .A = A, .T = T, .s = s, .R = R,
                                           .mode = AnchorOscillator::Mode::Lock};
                            const SpCellResult r = spRunLandingCell(c, drift, tr, by);
                            spPrintCell(c, r);
                            INFO("R-2: rendered theta " << r.theta << " deg, staircase prediction " << r.thetaStair
                                                        << " deg, gap " << r.r2Gap << " deg (model worst "
                                                        << kSpModelThetaDeg << ")");
                            worstBypass = std::max(worstBypass, std::abs(r.thetaBypass));
                            worstGap = std::max(worstGap, r.r2Gap);
                        }
                    }
                }
            }
        }
        std::printf("SC-013 smoke: worst bypass |theta| %.3f deg (negative control > %.0f), worst R-2 gap %.4f deg "
                    "(investigate > %.1f)\n",
                    worstBypass, kSpNegativeControlDeg, worstGap, kSpR2GapDeg);
        CHECK(worstBypass > kSpNegativeControlDeg);
    }

    SECTION("C1 checker positive control: a cornered ramp fails") {
        std::vector<double> g(200, 1.0);
        for (std::size_t n = 0; n <= 100; ++n)
            g[n] = static_cast<double>(n) / 100.0;
        const std::array<DT::DropC1Segment, 1> segs{DT::DropC1Segment{0, 199, 0.0}};
        const DT::DropC1Report rep = DT::dropCheckC1(g, segs, -1e6, 1e6);
        CHECK_FALSE(rep.pass);
    }

    SECTION("(d) bridged retriggers mid-fall, mid-rebound and mid-elastic land within 1 deg") {
        constexpr float A = 24.0f;
        constexpr float T = 200.0f;
        struct Case {
            SpMid where;
            double s;
            float R;
            const char* name;
        };
        constexpr std::array<Case, 3> kCases{Case{SpMid::Fall, 0.0, 0.0f, "mid-fall"},
                                             Case{SpMid::Rebound, 0.0, 1.0f, "mid-rebound"},
                                             Case{SpMid::Elastic, 1.0, 0.0f, "mid-elastic"}};
        for (const Case& k : kCases) {
            INFO(k.name);
            const float sp = spShapeParam(k.s);
            const DropCurve c1 = spSnapCurve(A, T, sp, k.R, kSpSmokeFs);
            const std::size_t nTrig = spMidPosition(c1, k.where);
            REQUIRE(nTrig > 0);
            REQUIRE(static_cast<double>(nTrig) < c1.endSample);
            const auto n = static_cast<double>(nTrig);
            const DropCurve c2 = Krate::DSP::makeDropCurve(static_cast<double>(A), static_cast<double>(T),
                                                           static_cast<double>(sp), static_cast<double>(k.R),
                                                           kSpSmokeFs, true, c1.valueAt(n), c1.slopeAt(n));
            const std::size_t landing = nTrig + static_cast<std::size_t>(std::ceil(c2.endSample));
            const float f2Approx = Krate::DSP::midiNoteToFrequency(kSpE1 + 5);
            const std::size_t L4 = spFourPeriods(kSpSmokeFs, static_cast<double>(f2Approx));
            const std::size_t total = landing + 2 * kSpU + L4 + 1;

            auto h = spHarness(kSpSmokeFs);
            h->setNoteMemory(NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Continue);   // bridged
            h->setAnchorControls(spAnchor(AnchorOscillator::Mode::Lock));
            h->setDrop(A, T, sp, k.R);
            tr.allocate(total);
            h->noteOn(kSpE1, 100, 0);
            h->noteOn(kSpE1 + 5, 100, nTrig);
            h->render(total, kSpHostBlock, tr);
            REQUIRE(h->contractViolations() == 0);
            const std::array<PT::NoteOutcome, 2> expected{PT::NoteOutcome::FromSilence, PT::NoteOutcome::Overlap};
            REQUIRE(PT::noteOutcomesMatch(expected, tr.recorded()));
            const PT::NoteOutcomeRecord& rec = tr.recorded()[1];
            REQUIRE(rec.lockKind == DropPhaseLock::TriggerKind::Bridged);
            REQUIRE(rec.effectiveSample == nTrig);
            REQUIRE(h->drop().plannedCurve().endSample == c2.endSample);
            const float f2 = tr.fRes[landing];
            REQUIRE(std::abs(f2 / f2Approx - 1.0f) <= 1e-5f);

            const double theta = spThetaDeg(tr, landing, L4, kSpSmokeFs, static_cast<double>(f2));
            std::size_t dcNonZero = 0;
            for (std::size_t m = landing; m < tr.length; ++m) {
                if (std::bit_cast<std::uint32_t>(tr.dc[m]) != 0u)
                    ++dcNonZero;
            }
            std::printf("SC-013(d) bridged %s (E1 -> E1+5, A 24, T 200, retrigger at %zu): theta %+.6f deg at the final "
                        "landing %zu (bar %.1f), |gamma| %.4f st, residual %.3e\n",
                        k.name, nTrig, theta, landing, kSpThetaBarDeg, std::abs(h->lock().correctionGamma()),
                        h->lock().solveResidualCycles());
            CHECK(std::abs(theta) <= kSpThetaBarDeg);
            CHECK(dcNonZero == 0);
        }
    }

    SECTION("(d) a Reset noteOn zeroes the accumulator; theta = 0 at its effective sample") {
        const float sp = spShapeParam(0.0);
        const DropCurve c1 = spSnapCurve(24.0f, 200.0f, sp, 0.0f, kSpSmokeFs);
        const std::size_t nTrig = spMidPosition(c1, SpMid::Fall);
        const float f2Approx = Krate::DSP::midiNoteToFrequency(kSpE1 + 5);
        const std::size_t L4 = spFourPeriods(kSpSmokeFs, static_cast<double>(f2Approx));
        const std::size_t stage2 = 256;   // > the declick dip (kDeclickMs at 48 kHz = 72 samples)
        const std::size_t total = nTrig + stage2 + L4 + 1;

        auto h = spHarness(kSpSmokeFs);
        h->setNoteMemory(NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Reset);
        h->setAnchorControls(spAnchor(AnchorOscillator::Mode::Lock));
        h->setDrop(24.0f, 200.0f, sp, 0.0f);
        tr.allocate(total);
        h->noteOn(kSpE1, 100, 0);
        h->render(nTrig, kSpHostBlock, tr);
        const float cap = h->capHz();
        const float f1 = tr.fRes[0];
        const double accBefore = h->lock().accumulatedExcessCycles();
        const double mirrorBefore = spSegmentExcess(tr.dc, 0, nTrig, f1, kSpSmokeFs, cap, 0);
        INFO("accumulated before the Reset noteOn " << accBefore << " cycles, test mirror " << mirrorBefore);
        CHECK(std::abs(accBefore - mirrorBefore) <= 1e-9 * std::max(1.0, std::abs(mirrorBefore)));
        CHECK(std::abs(accBefore) > 1e-3);   // non-vacuous: there is an excess to zero

        // The second note's Drop is A = 0, so theta over 4 periods from the effective sample is the Reset phase.
        h->setDrop(0.0f, 200.0f, sp, 0.0f);
        h->noteOn(kSpE1 + 5, 100, 0);
        h->render(stage2, kSpHostBlock, tr);
        REQUIRE(tr.numOutcomes == 2);
        const PT::NoteOutcomeRecord& rec = tr.recorded()[1];
        REQUIRE(rec.outcome == PT::NoteOutcome::Overlap);
        REQUIRE(rec.lockKind == DropPhaseLock::TriggerKind::SnapReset);
        const std::size_t eff = rec.effectiveSample;
        REQUIRE(eff > nTrig);
        REQUIRE(eff < h->position());
        const float f2 = tr.fRes[eff];   // the f_res the lock latched at the trigger
        REQUIRE(std::abs(f2 / f2Approx - 1.0f) <= 1e-5f);
        const double accAfter = h->lock().accumulatedExcessCycles();
        const double mirrorAfter = spSegmentExcess(tr.dc, eff, h->position(), f2, kSpSmokeFs, cap, 0);
        INFO("accumulated after the Reset noteOn " << accAfter << " cycles, test mirror from the effective sample "
                                                   << mirrorAfter);
        CHECK(std::abs(accAfter - mirrorAfter) <= 1e-9);
        CHECK(tr.coreL[eff] == 0.0f);
        CHECK(PT::noteBehaviourAnchorPhaseAt(tr, eff) == 0.0);

        h->render(total - h->position(), kSpHostBlock, tr);
        REQUIRE(h->contractViolations() == 0);
        REQUIRE(eff + L4 <= tr.length);
        const double theta = spThetaDeg(tr, eff, L4, kSpSmokeFs, static_cast<double>(f2));
        std::printf("SC-013(d) Reset noteOn at %zu (effective %zu): accumulated %.6e -> %.6e cycles (mirror %.6e), "
                    "theta %+.6f deg (bar %.1f)\n",
                    nTrig, eff, accBefore, accAfter, mirrorAfter, theta, kSpThetaBarDeg);
        CHECK(std::abs(theta) <= kSpThetaBarDeg);
    }

    SECTION("(d) interrupted Drop, then a FreeRunning snap: the accumulator is kept and the second Drop lands") {
        for (const int note : {kSpE1, kSpC3}) {
            INFO("note " << spNoteName(note));
            const float sp = spShapeParam(0.0);
            const std::size_t tOff = spSamples(0.5, kSpSmokeFs);
            const std::size_t tSecond = spSamples(1.5, kSpSmokeFs);
            const DropCurve c2 = spSnapCurve(12.0f, 200.0f, sp, 0.0f, kSpSmokeFs);
            const std::size_t landing = tSecond + static_cast<std::size_t>(std::ceil(c2.endSample));
            const float fApprox = Krate::DSP::midiNoteToFrequency(note);
            const std::size_t L4 = spFourPeriods(kSpSmokeFs, static_cast<double>(fApprox));
            const std::size_t total = landing + 2 * kSpU + L4 + 1;

            auto h = spHarness(kSpSmokeFs);
            h->setNoteMemory(NoteMemory::Mode::OscillatorContinuous, NoteMemory::Retrigger::Continue);
            h->setAnchorControls(spAnchor(AnchorOscillator::Mode::Lock));
            h->setDrop(48.0f, 4000.0f, sp, 0.0f);
            tr.allocate(total);
            h->noteOn(note, 100, 0);
            h->noteOff(note, tOff);
            h->render(tSecond, kSpHostBlock, tr);
            REQUIRE_FALSE(h->envelope().isActive());   // the ADSR idles (300 ms release) ...
            REQUIRE(h->drop().isActive());              // ... while the 4 s Drop is still in flight
            const float cap = h->capHz();
            const float f = tr.fRes[0];   // no glide: constant
            REQUIRE(std::abs(f / fApprox - 1.0f) <= 1e-5f);
            const double accBefore = h->lock().accumulatedExcessCycles();
            const double mirror = spSegmentExcess(tr.dc, 0, tSecond, f, kSpSmokeFs, cap, 0);
            INFO("accumulated before the second trigger " << accBefore << " cycles, test mirror " << mirror);
            CHECK(std::abs(accBefore - mirror) <= 1e-9 * std::max(1.0, std::abs(mirror)));

            h->setDrop(12.0f, 200.0f, sp, 0.0f);
            h->noteOn(note, 100, 0);
            h->render(1, kSpHostBlock, tr);
            const std::array<PT::NoteOutcome, 3> expected{PT::NoteOutcome::FromSilence, PT::NoteOutcome::Release,
                                                          PT::NoteOutcome::FromSilence};
            REQUIRE(PT::noteOutcomesMatch(expected, tr.recorded()));
            const PT::NoteOutcomeRecord& rec = tr.recorded()[2];
            REQUIRE(rec.lockKind == DropPhaseLock::TriggerKind::SnapFreeRunning);
            REQUIRE(rec.effectiveSample == tSecond);
            // Not zeroed: one more staircase increment on top of the kept value.
            const double inc =
                (static_cast<double>(spHeldHz(tr.dc[tSecond], f, ProfundumCore::kMinF0Hz, cap)) - static_cast<double>(f))
                / kSpSmokeFs;
            const double accAfter = h->lock().accumulatedExcessCycles();
            CHECK(std::abs(accAfter - (accBefore + inc)) <= 1e-9 * std::max(1.0, std::abs(accBefore)));
            CHECK(std::abs(accAfter) > 1e-3);

            h->render(total - h->position(), kSpHostBlock, tr);
            REQUIRE(h->contractViolations() == 0);
            REQUIRE(h->drop().plannedCurve().endSample == c2.endSample);
            const double theta = spThetaDeg(tr, landing, L4, kSpSmokeFs, static_cast<double>(f));
            std::printf("SC-013(d) interrupted Drop %s: accumulated %.6f cycles kept at the FreeRunning snap (mirror "
                        "%.6f), theta %+.6f deg at the second landing %zu (bar %.1f)\n",
                        spNoteName(note), accBefore, mirror, theta, landing, kSpThetaBarDeg);
            CHECK(std::abs(theta) <= kSpThetaBarDeg);
        }
    }

    SECTION("(d) a glide in flight during a Drop: theta recorded, not gated (E-15)") {
        const float sp = spShapeParam(0.0);
        const DropCurve c1 = spSnapCurve(24.0f, 200.0f, sp, 0.0f, kSpSmokeFs);
        const std::size_t nTrig = spMidPosition(c1, SpMid::Fall);
        const auto n = static_cast<double>(nTrig);
        const DropCurve c2 = Krate::DSP::makeDropCurve(24.0, 200.0, static_cast<double>(sp), 0.0, kSpSmokeFs, true,
                                                       c1.valueAt(n), c1.slopeAt(n));
        const std::size_t landing = nTrig + static_cast<std::size_t>(std::ceil(c2.endSample));
        const std::size_t total = landing + spSamples(0.5, kSpSmokeFs);

        auto h = spHarness(kSpSmokeFs);
        h->setNoteMemory(NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Continue);
        h->setAnchorControls(spAnchor(AnchorOscillator::Mode::Lock));
        h->mono().setGlideMode(GlideMode::ConstantTime);
        h->mono().setPortamentoTime(200.0f);
        h->setDrop(24.0f, 200.0f, sp, 0.0f);
        tr.allocate(total);
        h->noteOn(kSpE1, 100, 0);
        h->noteOn(kSpE1 + 7, 100, nTrig);
        h->render(total, kSpHostBlock, tr);
        REQUIRE(h->contractViolations() == 0);
        REQUIRE(tr.numOutcomes == 2);
        REQUIRE(tr.recorded()[1].lockKind == DropPhaseLock::TriggerKind::Bridged);
        const double fLand = static_cast<double>(tr.fRes[landing]);
        const std::size_t L4 = spFourPeriods(kSpSmokeFs, fLand);
        const double theta = spThetaDeg(tr, landing, L4, kSpSmokeFs, fLand);
        std::printf("SC-013(d) glide in flight (E1 -> E1+7, 200 ms ConstantTime, from %zu) during a bridged Drop: "
                    "f_res at landing %.4f Hz (target %.4f), theta %+.6f deg (recorded, E-15)\n",
                    nTrig, fLand, static_cast<double>(Krate::DSP::midiNoteToFrequency(kSpE1 + 7)), theta);
        CHECK(Krate::DSP::detail::isFinite(theta));
    }

    std::printf("ProfundumNoteBehaviour_PostDropLandingSmoke wall clock %.1f s\n", spElapsedSeconds(t0));
}

// Full SC-013 grid, one shard per note (tasks.md deviation 4; plan R-10): run each by name, concurrently.
TEST_CASE("ProfundumNoteBehaviour_PostDropLanding_E0", "[systems][profundum][note-behaviour][long]") {
    spRunLandingShard(kSpE0, {AnchorOscillator::Mode::Lock}, "sc013-E0.csv");
}

TEST_CASE("ProfundumNoteBehaviour_PostDropLanding_E1", "[systems][profundum][note-behaviour][long]") {
    spRunLandingShard(kSpE1, {AnchorOscillator::Mode::Lock}, "sc013-E1.csv");
}

TEST_CASE("ProfundumNoteBehaviour_PostDropLanding_E2", "[systems][profundum][note-behaviour][long]") {
    spRunLandingShard(kSpE2, {AnchorOscillator::Mode::Lock}, "sc013-E2.csv");
}

TEST_CASE("ProfundumNoteBehaviour_PostDropLanding_C3", "[systems][profundum][note-behaviour][long]") {
    spRunLandingShard(kSpC3, {AnchorOscillator::Mode::Lock}, "sc013-C3.csv");
}

TEST_CASE("ProfundumNoteBehaviour_PostDropLanding_PunchGhostE1", "[systems][profundum][note-behaviour][long]") {
    spRunLandingShard(kSpE1, {AnchorOscillator::Mode::Punch, AnchorOscillator::Mode::Ghost}, "sc013-E1-punch-ghost.csv");
}

// =============================================================================
// T021: SC-026 FOLLOW after a Drop
// =============================================================================

TEST_CASE("ProfundumNoteBehaviour_FollowAfterDropSmoke", "[systems][profundum][note-behaviour]") {
    const auto t0 = std::chrono::steady_clock::now();
    for (const float A : {12.0f, 48.0f}) {
        for (const double s : kSpShapes)
            (void)spRunFollowCell(A, 200.0f, s, 1.0f, 10.0);
    }
    std::printf("ProfundumNoteBehaviour_FollowAfterDropSmoke wall clock %.1f s\n", spElapsedSeconds(t0));
}

TEST_CASE("ProfundumNoteBehaviour_FollowAfterDrop", "[systems][profundum][note-behaviour][long]") {
    const auto t0 = std::chrono::steady_clock::now();
    double worstD1 = 0.0;
    double worstDrift = 0.0;
    std::size_t cells = 0;
    for (const float A : kSpAmounts) {
        for (const float T : kSpTimes) {
            for (const double s : kSpShapes) {
                for (const float R : kSpRebounds) {
                    const SpFollowResult r = spRunFollowCell(A, T, s, R, 60.0);
                    worstD1 = std::max(worstD1, std::abs(r.dTheta1));
                    worstDrift = std::max(worstDrift, std::abs(r.drift));
                    ++cells;
                }
            }
        }
    }
    std::printf("SC-026 FOLLOW grid E1 48 kHz: %zu cells, worst |dTheta(t_s + 1 s)| %.6f deg (bar %.1f), worst "
                "60 s drift %.6f deg (bar %.1f), wall clock %.1f s\n",
                cells, worstD1, kSpThetaBarDeg, worstDrift, kSpDriftBarDeg, spElapsedSeconds(t0));
}

// =============================================================================
// T022: SC-010, SC-014, SC-015, SC-016, SC-021 (FR-060-FR-062, FR-064, FR-068; C-9, C-10, O-1 ruling)
// =============================================================================
// Method notes (surfaced; tasks.md T022, spec SC-010 / SC-014 / SC-015 / SC-021):
//   - SC-010 renders carry the SC-009(b) Drop pass (A = 12, T = 100 ms, s = 0) so the Drop's slope term is
//     exercised at every snap and bridged trigger; the scripts are the SC-009(b) matrix script per (Note Memory
//     mode, Retrigger) and the SC-009(c) VoiceReuse script per (mode, Retrigger).
//   - SC-010(b) bound, Phase 3 SC-006(c) form, per transition window [arm sample, effective + one period]:
//       1.1 * (G (Dc + Gm Da) + S_gain (Pc + Gm Pa) + Sm Pa)
//     G = 1 (declick gain x ADSR <= 1, velocity scaling off). S_gain = pi / (2 L_d) (C-15 raised cosine, analytic)
//     + the ADSR's largest one-pole step (envelope_utils.h calcEnvCoefficients, analytic). Dc/Pc, Da/Pa: max |dy| and
//     peak of a steady, prepared core (same recipe) and a LOCK level-1 Anchor at f_top, the highest COMMANDED
//     frequency (f_core / f_anchor control traces) in the window rounded up to a 1/8-semitone grid -- the Drop's
//     peak-slope term enters through f_top (the Drop starts the window at f_res 2^(A/12)). Gm / Sm: the peak and
//     the peak step of the Anchor's mode-gain program (PUNCH boost, GHOST emerge, Hermite re-arm bridges) read from
//     its control trace modeGain(): a control curve, never the measured output y. The glide steps (SC-010(c)): the
//     one-semitone frequency step on a phase-continuous oscillator is covered by D at f_top >= the upper step.
//   - SC-010(a) runs ClickDetector (documented defaults, sampleRate = fs) on the composition output L and counts
//     differentially (T023 epoch 6): a detection counts only if the phase-aligned uninterrupted reference render has
//     none within +-N samples, N = floor(fs / (8 f_max)) per cell (method at t22CountClicks). Each counted detection
//     is printed with tag SC010-CLICK and its nearest transition. RECORDED, NOT GATED (T023 epoch 7, spec
//     Clarifications 2026-10-13 "T023 epoch 6 readings" (B)): an undeclicked Reset restart lands at h1 phase 0, the
//     saw's own edge, so a position-only count cannot flag it. The click gates are (b) at every transition and (c)'s
//     bound at every glide step / switch; the positive controls are proven by (b) and (c) only. SC-021's "zero clicks
//     (SC-010 method)" is (b) at every transition of the use-case render; its (a) count is recorded in its CSV.
//   - SC-010(b) D_core (ruling (B')): the core's delivered-gain bound over the window (harness coreDeltaBound), not
//     the steady reference's; Pc, Da, Pa and the 1.1 factor as above.
//   - SC-014: the composition is rendered at host block 37; the harness renders the core in chunks that end on its
//     16-sample pitch grid, so the per-sample d_core trace is exact at any host block (helpers header, deviation 3).
//     f_zc-side settle time uses the 10-cent agreement bar (D-15) as its threshold: 1 cent is the estimator's own
//     self-test tolerance, not its in-render resolution. The Note Memory script carries the SC-009(b) Drop pass
//     (A = 12, T = 100 ms) so dropTrigger differences are audible.
//   - SC-015 (O-1 ruling): the rendered aliased power of the core output, Phase 1 SC-009's bend-smoke estimator
//     (aliasedPower over an aliasFftLength frame starting 2 x kControlInterval after the trajectory's last pitch
//     move, at the held f0), bar <= -96 dBFS. The index x currentFrequency() formula is neither computed nor gated.
//     "Smallest positive T" for the glides is one sample (1000 / fs ms); 1 ms is run as well. Upward rebound
//     recovery is covered by every Drop cell (R = 1).
// =============================================================================

namespace {

using Krate::DSP::SpectralShapeRecipe;
namespace TU = Krate::DSP::TestUtils;

constexpr float kT22DropA = 12.0f;      // SC-009(b) Drop pass
constexpr float kT22DropMs = 100.0f;
constexpr double kT22BoundFactor = 1.1;
constexpr int kT22C2 = 36;
constexpr std::array<std::size_t, 3> kT22Blocks{1, 37, 512};
constexpr std::array<AnchorOscillator::Mode, 4> kT22Modes{AnchorOscillator::Mode::Lock, AnchorOscillator::Mode::Follow,
                                                         AnchorOscillator::Mode::Punch, AnchorOscillator::Mode::Ghost};
constexpr std::array<NoteMemory::Mode, 4> kT22MemModes{NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Mode::Legato,
                                                       NoteMemory::Mode::EnvelopeRetriggerOnly,
                                                       NoteMemory::Mode::OscillatorContinuous};
constexpr std::array<NoteMemory::Retrigger, 2> kT22Retriggers{NoteMemory::Retrigger::Reset,
                                                              NoteMemory::Retrigger::Continue};

// Default: the harness's own controls (SpectralShapeRecipe::kDefaultControls), for the SC-021 use-case renders' (b).
enum class T22Recipe : std::uint8_t { Saw, Growl, Default };

const SpectralShapeRecipe::Controls& t22Controls(T22Recipe r) {
    if (r == T22Recipe::Default)
        return SpectralShapeRecipe::kDefaultControls;
    return r == T22Recipe::Saw ? SpectralShapeRecipe::kSawAnchor : SpectralShapeRecipe::kGrowl;
}

const char* t22RecipeName(T22Recipe r) {
    if (r == T22Recipe::Default)
        return "kDefaultControls";
    return r == T22Recipe::Saw ? "kSawAnchor" : "kGrowl";
}

const char* t22MemName(NoteMemory::Mode m) {
    switch (m) {
        case NoteMemory::Mode::AlwaysRetrigger:
            return "AlwaysRetrigger";
        case NoteMemory::Mode::Legato:
            return "Legato";
        case NoteMemory::Mode::EnvelopeRetriggerOnly:
            return "EnvelopeRetriggerOnly";
        case NoteMemory::Mode::OscillatorContinuous:
            return "OscillatorContinuous";
    }
    return "?";
}

const char* t22GlideName(GlideMode g) {
    switch (g) {
        case GlideMode::ConstantTime:
            return "ConstantTime";
        case GlideMode::ConstantRate:
            return "ConstantRate";
        case GlideMode::Quantized:
            return "Quantized";
    }
    return "?";
}

/// L_d = ceil(kDeclickMs fs / 1000) (retrigger_declick.h prepare()).
std::size_t t22DeclickLength(double fs) {
    return static_cast<std::size_t>(std::ceil(static_cast<double>(Krate::DSP::RetriggerDeclick::kDeclickMs) * fs / 1000.0));
}

/// round(fs / f): one period of f in samples (>= 1).
std::size_t t22Period(double fs, double f) {
    return std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(fs / f)));
}

/// The ADSR's largest per-sample step at the harness settings (envelope_utils.h:91-108): attack from 0
/// (base), decay and release from 1 (|base + coef - 1|). Analytic, from the harness constants.
double t22EnvSlopeMax(double fs) {
    using Krate::DSP::EnvCurve;
    const auto f = static_cast<float>(fs);
    const auto a = Krate::DSP::calcEnvCoefficients(PT::NoteBehaviourHarness::kAttackMs, f, 1.0f,
                                                   Krate::DSP::getAttackTargetRatio(EnvCurve::Exponential), true);
    const auto d = Krate::DSP::calcEnvCoefficients(PT::NoteBehaviourHarness::kDecayMs, f, 0.0f,
                                                   Krate::DSP::getDecayTargetRatio(EnvCurve::Exponential), false);
    const auto r = Krate::DSP::calcEnvCoefficients(PT::NoteBehaviourHarness::kReleaseMs, f, 0.0f,
                                                   Krate::DSP::getDecayTargetRatio(EnvCurve::Exponential), false);
    const double sa = std::abs(static_cast<double>(a.base));
    const double sd = std::abs(static_cast<double>(d.base) + static_cast<double>(d.coef) - 1.0);
    const double sr = std::abs(static_cast<double>(r.base) + static_cast<double>(r.coef) - 1.0);
    return std::max({sa, sd, sr});
}

/// pi / (2 L_d): the C-15 raised cosine's peak per-sample step.
double t22DeclickSlope(double fs) {
    return 3.14159265358979323846 / (2.0 * static_cast<double>(t22DeclickLength(fs)));
}

// ---- steady references for D_max / P_max ----------------------------------------

struct T22Steady {
    double dCore = 0.0, pCore = 0.0, dAnchor = 0.0, pAnchor = 0.0;
};

/// A prepared core (harness configuration, `recipe`) and a LOCK level-1 Anchor fed a1 as the harness does, both
/// Reset at f; max |dy| and peak over [0.5 s, 1 s) of a 512-block render.
T22Steady t22RenderSteady(double fs, T22Recipe recipe, float f) {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(fs, PT::NoteBehaviourHarness::kNumPartials);
    core->setExcitation(ProfundumCore::kDefaultExcitation);
    core->setControls(t22Controls(recipe));
    core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
    core->noteOn(f);
    auto anchor = std::make_unique<AnchorOscillator>();
    anchor->prepare(fs);
    anchor->setControls(spAnchor(AnchorOscillator::Mode::Lock));
    anchor->setRetriggerPhase(AnchorOscillator::RetriggerPhase::Reset);
    anchor->noteOn(f, true);
    const std::size_t total = spSamples(1.0, fs);
    const std::size_t from = spSamples(0.5, fs);
    std::vector<float> cl(total, 0.0f), cr(total, 0.0f), al(total, 0.0f), ar(total, 0.0f);
    constexpr std::size_t kBlock = 512;
    for (std::size_t pos = 0; pos < total; pos += kBlock) {
        const std::size_t n = std::min(kBlock, total - pos);
        core->processBlock(cl.data() + pos, cr.data() + pos, n);
        anchor->setCoreH1Amplitude(core->deliveredGains()[0] * SpectralShapeRecipe::kCenterPanGain);
        anchor->processBlock(al.data() + pos, ar.data() + pos, n);
    }
    T22Steady s;
    for (std::size_t n = from; n < total; ++n) {
        s.pCore = std::max(s.pCore, std::abs(static_cast<double>(cl[n])));
        s.pAnchor = std::max(s.pAnchor, std::abs(static_cast<double>(al[n])));
        s.dCore = std::max(s.dCore, std::abs(static_cast<double>(cl[n]) - static_cast<double>(cl[n - 1])));
        s.dAnchor = std::max(s.dAnchor, std::abs(static_cast<double>(al[n]) - static_cast<double>(al[n - 1])));
    }
    return s;
}

class T22SteadyCache {
public:
    T22Steady get(double fs, T22Recipe recipe, float f) {
        const auto bits = std::bit_cast<std::uint32_t>(f);
        for (const Entry& e : entries_) {
            if (e.fs == fs && e.recipe == recipe && e.fBits == bits)
                return e.value;
        }
        entries_.push_back(Entry{.fs = fs, .recipe = recipe, .fBits = bits, .value = t22RenderSteady(fs, recipe, f)});
        return entries_.back().value;
    }

private:
    struct Entry {
        double fs = 0.0;
        T22Recipe recipe = T22Recipe::Saw;
        std::uint32_t fBits = 0;
        T22Steady value{};
    };
    std::vector<Entry> entries_;
};

/// f rounded UP onto a 1/8-semitone grid, never above cap.
float t22SteadyKey(float f, float cap) {
    const double g = std::ceil(96.0 * std::log2(static_cast<double>(f)));
    const auto k = static_cast<float>(std::exp2(g / 96.0));
    return std::min(std::max(k, f), cap);
}

// ---- SC-010(a)(b) measurement ---------------------------------------------------

std::vector<TU::ClickDetection> t22Clicks(const float* y, std::size_t n, double fs) {
    TU::ClickDetectorConfig cfg{};   // documented defaults (artifact_detection.h:38-44)
    cfg.sampleRate = static_cast<float>(fs);
    TU::ClickDetector detector(cfg);
    detector.prepare();
    return detector.detect(y, n);
}

struct T22Bound {
    double worst = 0.0;   // |y[n] - y[n-1]| at the sample with the largest |dy| / bound ratio
    double bound = 0.0;   // the bound at that sample
    double fTop = 0.0;
    double gm = 1.0;
    double sm = 0.0;
    double dCore = 0.0;         // D_core used: max of the delivered-gain bound over the window (ruling (B'))
    double dCoreSteady = 0.0;   // the steady references' D_core over the window's keys (recorded)
    std::size_t at = 0;
};

/// SC-010(b) over samples [lo, hi) of y (default: the composition output L of tr), Phase 3 SC-006(c) form
/// 1.1 * (G * D + S * P) with G the total gain on the waveforms: y = (core + Anchor) * q, q = declick g * ADSR e,
/// so |dy[n]| <= q[n] * (Dc + Gm Da + Sm Pa) + |dq| * (Pc + Gm Pa). G is read per sample from the control traces
/// (gain, env; never the measured y) as max(q[n - 1], q[n]): the bound is evaluated at every sample and the
/// worst |dy| / bound ratio is returned. (Fix-up 2026-10-13: G was omitted, i.e. taken as 1, which let a
/// waveform edge at e = 1 mask an undeclicked restart at sustain 0.3, so the positive controls could not fail.)
/// Ruling (B') (T023 epoch 6, spec Clarifications 2026-10-13): Dc is the core's delivered-gain bound over the window
/// (tr.coreDeltaBound, the harness's analytic model of the actual trajectory, never the measured output), so the
/// core's shape slew after a Drop snap is inside D; Pc, Da, Pa and the 1.1 factor stay the steady references'.
T22Bound t22DeltaBound(const PT::NoteBehaviourTraces& tr, const std::vector<float>& y, std::size_t lo, std::size_t hi,
                       double fs, float cap, T22Recipe recipe, T22SteadyCache& cache) {
    lo = std::max<std::size_t>(lo, 1);
    hi = std::min(hi, tr.length);
    REQUIRE(lo < hi);
    T22Bound b;
    float fTop = ProfundumCore::kMinF0Hz;
    float fBottom = cap;
    for (std::size_t n = lo - 1; n < hi; ++n) {
        fTop = std::max({fTop, tr.fCore[n], tr.fAnchor[n]});
        fBottom = std::min({fBottom, tr.fCore[n], tr.fAnchor[n]});
        b.gm = std::max(b.gm, tr.modeGain[n]);
        if (n >= lo)
            b.sm = std::max(b.sm, std::abs(tr.modeGain[n] - tr.modeGain[n - 1]));
    }
    fTop = std::clamp(fTop, ProfundumCore::kMinF0Hz, cap);
    fBottom = std::clamp(fBottom, ProfundumCore::kMinF0Hz, fTop);
    // D and P are not monotone in f once the partials reach the cap (the band-limited edge's slope then
    // stays near pi * cap / fs while the mask reshapes the top partials), so every 1/8-semitone key the
    // window's trajectories cover is rendered and the per-term maximum taken (T023 epoch 5: the f_top
    // key alone read 0.4-3 % under the transient |dy| at C3 / E2 after the per-sample G fix).
    T22Steady st{};
    for (float f = fBottom;;) {
        const float key = t22SteadyKey(f, cap);
        const T22Steady k = cache.get(fs, recipe, key);
        if (k.dCore + k.dAnchor >= st.dCore + st.dAnchor)
            b.fTop = static_cast<double>(key);
        st.dCore = std::max(st.dCore, k.dCore);
        st.pCore = std::max(st.pCore, k.pCore);
        st.dAnchor = std::max(st.dAnchor, k.dAnchor);
        st.pAnchor = std::max(st.pAnchor, k.pAnchor);
        if (!(key < t22SteadyKey(fTop, cap)))
            break;
        f = std::nextafter(key, cap);   // the next grid key up
    }
    for (std::size_t n = lo; n < hi; ++n)
        b.dCore = std::max(b.dCore, static_cast<double>(tr.coreDeltaBound[n]));
    b.dCoreSteady = st.dCore;
    const double sGain = t22DeclickSlope(fs) + t22EnvSlopeMax(fs);
    const double dTerm = b.dCore + b.gm * st.dAnchor + b.sm * st.pAnchor;
    const double sTerm = sGain * (st.pCore + b.gm * st.pAnchor);
    double worstRatio = -1.0;
    for (std::size_t n = lo; n < hi; ++n) {
        const double q0 = std::abs(static_cast<double>(tr.gain[n - 1]) * static_cast<double>(tr.env[n - 1]));
        const double q1 = std::abs(static_cast<double>(tr.gain[n]) * static_cast<double>(tr.env[n]));
        const double bound = kT22BoundFactor * (std::max(q0, q1) * dTerm + sTerm);
        const double d = std::abs(static_cast<double>(y[n]) - static_cast<double>(y[n - 1]));
        if (d / bound > worstRatio) {
            worstRatio = d / bound;
            b.worst = d;
            b.bound = bound;
            b.at = n;
        }
    }
    return b;
}

/// SC-016: Anchor, core and composition L == R bitwise.
bool t22MonoIdentical(const PT::NoteBehaviourTraces& tr) {
    return PT::samplesBitEqual(tr.anchorL.data(), tr.anchorR.data(), tr.length)
           && PT::samplesBitEqual(tr.coreL.data(), tr.coreR.data(), tr.length)
           && PT::samplesBitEqual(tr.L.data(), tr.R.data(), tr.length);
}

bool t22IsTransition(const PT::NoteOutcomeRecord& r) {
    return r.outcome != PT::NoteOutcome::Release && r.outcome != PT::NoteOutcome::NoAction;
}

struct T22Totals {
    std::size_t renders = 0;
    std::size_t clicks = 0;      // counted (differential) detections, SC-010(a) / (c)
    std::size_t rawClicks = 0;   // raw ClickDetector detections (recorded)
    std::size_t references = 0;
    std::size_t nMin = std::numeric_limits<std::size_t>::max();
    std::size_t nMax = 0;
    std::array<std::size_t, 5> matchedDist{};   // matched detections by distance: <= 1, 2, 4, 8, > 8 samples
    std::size_t windows = 0;
    std::size_t overBound = 0;
    std::size_t monoBreaks = 0;
    double worstRatio = 0.0;
};

// ---- SC-010(a) / SC-021: the differential click count (T023 epoch 6; recorded, not gated since epoch 7) -----------
// Spec Clarifications 2026-10-13 "T023 epoch 5 readings" (B): ClickDetector with its documented defaults flags the
// band-limited saw / growl edge of the steady, transition-free core (12-115 detections per 0.5 s), so a detection
// counts only if the uninterrupted reference render of the cell has NO detection within +-N samples of it.
// "T023 epoch 6 readings" (B): the count is printed / recorded per cell for the compliance table, with no zero bar
// (an undeclicked restart at h1 phase 0 sits 0.36-0.50 samples from the reference's own saw edge, so position-only
// matching cannot flag it for any N >= 1); (b) and (c)'s bound gate clicks. Method:
//   - Segments: every applied transition (t22IsTransition, effective sample e_i) starts a segment that runs to the
//     next applied transition's effective sample. A detection belongs to the segment containing it, so the dip
//     before a declicked switch belongs to the old note's segment.
//   - Reference: the segment's note (record.note) rendered uninterrupted: a fresh harness with the cell's core
//     controls and Anchor mode, the note from silence at reference sample 0 and held, no other event, read from
//     kT22RefLeadSeconds on (attack, decay and the Anchor's mode-gain programs settled). Where the segment's transition
//     triggered the Drop, the reference's DropEnvelope alone is triggered (snap, the cell's Drop settings) at the lead
//     sample, so the reference carries the same pitch motion without a note event; otherwise its Drop Amount is 0. One
//     render per distinct (note, Drop) per cell, host block kT22RefBlock.
//   - Phase alignment: the core's h1 phase in cycles is the running sum of its held frequency (coreFreq / fs),
//     restarted at 0 at every Reset-branch effective sample (the re-seed, profundum_core.h:532-562) and continuous
//     across FreeRunning transitions. Detection n of segment i maps to m0 = lead + (n - e_i) and then moves by less
//     than half a reference period to m*, the reference sample whose h1 phase (mod 1) equals the transition render's
//     at n (linear interpolation of the reference's phase sum). Where a Reset re-seeds and the trajectory is the
//     reference's, the move is 0.
//   - N = floor(fs / (8 f_max)), f_max the highest core or Anchor frequency commanded anywhere in the render (cap
//     clamped): at most 1/8 of the shortest sounding period, fixed per cell; nMax (> 0) tightens it further.
constexpr double kT22RefLeadSeconds = 0.5;
constexpr std::size_t kT22RefBlock = 512;

struct T22RefSpec {
    const SpectralShapeRecipe::Controls* controls = nullptr;   // nullptr: the harness default controls
    AnchorOscillator::Mode mode = AnchorOscillator::Mode::Lock;
    float dropA = 0.0f;   // the cell's Drop (shape as the harness parameter)
    float dropT = kT22DropMs;
    float dropShape = 0.0f;
    float dropR = 0.0f;
};

struct T22ClickReport {
    std::size_t raw = 0;                // ClickDetector detections on the transition render
    std::vector<std::size_t> counted;   // detections with no reference detection within +-N (SC-010(a) / SC-021)
    std::size_t n = 0;                  // N in samples
    std::size_t references = 0;         // reference renders
    std::vector<std::pair<std::size_t, double>> nearest;   // every detection: |nearest reference detection - m*|
};

/// h1 phase sum in cycles at the START of each sample (size length + 1): + coreFreq[n] / fs per sample, restarted
/// at 0 at every Reset-branch effective sample.
std::vector<double> t22CorePhase(const PT::NoteBehaviourTraces& tr, double fs) {
    std::vector<std::uint8_t> reseed(tr.length, std::uint8_t{0});
    for (const PT::NoteOutcomeRecord& r : tr.recorded()) {
        if (r.coreResetBranch && r.effectiveSample < tr.length)
            reseed[r.effectiveSample] = 1;
    }
    std::vector<double> phi(tr.length + 1, 0.0);
    double acc = 0.0;
    for (std::size_t n = 0; n < tr.length; ++n) {
        if (reseed[n] != 0)
            acc = 0.0;
        phi[n] = acc;
        acc += static_cast<double>(tr.coreFreq[n]) / fs;
    }
    phi[tr.length] = acc;
    return phi;
}

/// The reference sample (fractional) whose phase sum equals target; phi is non-decreasing.
double t22PhaseToSample(const std::vector<double>& phi, double target) {
    const auto it = std::lower_bound(phi.begin(), phi.end(), target);
    if (it == phi.begin())
        return 0.0;
    if (it == phi.end())
        return static_cast<double>(phi.size() - 1);
    const auto j = static_cast<std::size_t>(it - phi.begin());
    const double a = phi[j - 1];
    const double b = phi[j];
    return static_cast<double>(j - 1) + (b > a ? (target - a) / (b - a) : 0.0);
}

/// The uninterrupted reference of `note` (see the method above), `length` samples; with drop, the Drop alone is
/// triggered at sample lead.
void t22RenderReference(double fs, const T22RefSpec& spec, int note, bool drop, std::size_t lead, std::size_t length,
                        PT::NoteBehaviourTraces& ref) {
    auto h = spHarness(fs);
    if (spec.controls != nullptr)
        h->core().setControls(*spec.controls);
    h->setAnchorControls(spAnchor(spec.mode));
    h->setDrop(0.0f, kT22DropMs, 0.0f, 0.0f);
    ref.allocate(length);
    h->noteOn(note, 100, 0);
    h->render(lead, kT22RefBlock, ref);
    if (drop) {
        h->setDrop(spec.dropA, spec.dropT, spec.dropShape, spec.dropR);
        h->drop().trigger(true);
    }
    h->render(length - lead, kT22RefBlock, ref);
    REQUIRE(h->contractViolations() == 0);
    REQUIRE(ref.length == length);
}

/// The SC-010(a) / SC-021 differential count over the whole render (method above). Prints the first five counted
/// detections with tag SC010-CLICK.
/// y: the signal to test (default tr.L; a positive control passes its own, sample-aligned with tr).
T22ClickReport t22CountClicks(const PT::NoteBehaviourTraces& tr, double fs, float cap, const T22RefSpec& spec,
                              const std::string& label, std::size_t nMax = 0, const float* y = nullptr) {
    T22ClickReport rep;
    const auto det = t22Clicks(y != nullptr ? y : tr.L.data(), tr.length, fs);
    rep.raw = det.size();

    float fMax = ProfundumCore::kMinF0Hz;
    for (std::size_t n = 0; n < tr.length; ++n)
        fMax = std::max({fMax, tr.fCore[n], tr.fAnchor[n]});
    fMax = std::min(fMax, cap);
    rep.n = static_cast<std::size_t>(std::floor(fs / (8.0 * static_cast<double>(fMax))));
    if (nMax > 0)
        rep.n = std::min(rep.n, nMax);
    if (det.empty())
        return rep;

    // Segments: applied transitions in time order.
    struct Segment {
        std::size_t start = 0;
        std::size_t end = 0;
        int note = -1;
        bool drop = false;
    };
    std::vector<Segment> segs;
    for (const PT::NoteOutcomeRecord& r : tr.recorded()) {
        if (!t22IsTransition(r) || r.effectiveSample >= tr.length || r.note < 0)
            continue;
        if (!segs.empty())
            segs.back().end = r.effectiveSample;
        segs.push_back({r.effectiveSample, tr.length, r.note, r.lockTriggered && spec.dropA != 0.0f});
    }

    // One reference per distinct note, long enough for its longest segment plus half a period and N.
    const std::size_t lead = spSamples(kT22RefLeadSeconds, fs);
    struct Reference {
        int note = -1;
        bool drop = false;
        std::vector<double> phi;
        std::vector<std::size_t> det;   // sorted detection indices
    };
    std::vector<Reference> refs;
    PT::NoteBehaviourTraces scratch;
    for (const Segment& s : segs) {
        bool have = false;
        for (const Reference& r : refs)
            have = have || (r.note == s.note && r.drop == s.drop);
        if (have)
            continue;
        std::size_t longest = 0;
        for (const Segment& o : segs) {
            if (o.note == s.note && o.drop == s.drop)
                longest = std::max(longest, o.end - o.start);
        }
        const double f = static_cast<double>(Krate::DSP::midiNoteToFrequency(s.note));
        const std::size_t len = lead + longest + t22Period(fs, f) + rep.n + 2;
        t22RenderReference(fs, spec, s.note, s.drop, lead, len, scratch);
        ++rep.references;
        Reference r;
        r.note = s.note;
        r.drop = s.drop;
        r.phi = t22CorePhase(scratch, fs);
        for (const TU::ClickDetection& d : t22Clicks(scratch.L.data(), scratch.length, fs))
            r.det.push_back(d.sampleIndex);
        std::sort(r.det.begin(), r.det.end());
        refs.push_back(std::move(r));
    }

    const std::vector<double> phiTr = t22CorePhase(tr, fs);
    for (const TU::ClickDetection& d : det) {
        const std::size_t n = d.sampleIndex;
        const Segment* seg = nullptr;
        for (const Segment& s : segs) {
            if (n >= s.start && n < s.end)
                seg = &s;
        }
        bool matched = false;
        double mStar = -1.0;
        if (seg != nullptr) {
            const Reference* ref = nullptr;
            for (const Reference& r : refs) {
                if (r.note == seg->note && r.drop == seg->drop)
                    ref = &r;
            }
            REQUIRE(ref != nullptr);
            const std::size_t m0 = lead + (n - seg->start);
            REQUIRE(m0 < ref->phi.size());
            const double w = phiTr[n] - std::floor(phiTr[n]);
            const double p0 = ref->phi[m0];
            double shift = w - (p0 - std::floor(p0));
            shift -= std::round(shift);   // [-0.5, 0.5]: the nearest sample with the same phase
            mStar = t22PhaseToSample(ref->phi, p0 + shift);
            const double lo = mStar - static_cast<double>(rep.n);
            const auto first = std::lower_bound(ref->det.begin(), ref->det.end(),
                                                static_cast<std::size_t>(std::max(0.0, std::ceil(lo))));
            matched = first != ref->det.end() && static_cast<double>(*first) <= mStar + static_cast<double>(rep.n);
            // Distance to the nearest reference detection (recorded: the choice of N).
            double best = std::numeric_limits<double>::infinity();
            const auto at = std::lower_bound(ref->det.begin(), ref->det.end(),
                                             static_cast<std::size_t>(std::max(0.0, std::floor(mStar))));
            if (at != ref->det.end())
                best = std::abs(static_cast<double>(*at) - mStar);
            if (at != ref->det.begin())
                best = std::min(best, std::abs(static_cast<double>(*(at - 1)) - mStar));
            rep.nearest.emplace_back(n, best);
        }
        if (matched)
            continue;
        rep.counted.push_back(n);
        if (rep.counted.size() <= 5) {
            std::size_t nearest = PT::kNoteBehaviourNoSample;
            std::size_t dist = std::numeric_limits<std::size_t>::max();
            for (const PT::NoteOutcomeRecord& r : tr.recorded()) {
                if (r.eventSample >= tr.length)
                    continue;
                const std::size_t dd = r.eventSample > n ? r.eventSample - n : n - r.eventSample;
                if (dd < dist) {
                    dist = dd;
                    nearest = r.eventSample;
                }
            }
            std::printf("SC010-CLICK %s: detection at sample %zu (amplitude %.6e) absent from the reference within "
                        "+-%zu (mapped to %.2f), nearest event at %zu (%zu samples away)\n",
                        label.c_str(), n, static_cast<double>(d.amplitude), rep.n, mStar, nearest, dist);
        }
    }
    return rep;
}

/// Prints every detection in [lo, hi] with its distance to the nearest reference detection (the choice of N).
void t22PrintNearest(const T22ClickReport& cr, std::size_t lo, std::size_t hi, const char* what) {
    for (const auto& [n, dist] : cr.nearest) {
        if (n >= lo && n <= hi)
            std::printf("SC-010 %s: detection at %zu, nearest reference detection %.2f samples from its mapped "
                        "position (N %zu)\n",
                        what, n, dist, cr.n);
    }
}

/// Counted detections in [lo, hi].
std::size_t t22CountedIn(const T22ClickReport& cr, std::size_t lo, std::size_t hi) {
    std::size_t k = 0;
    for (const std::size_t n : cr.counted) {
        if (n >= lo && n <= hi)
            ++k;
    }
    return k;
}

void t22AddClicks(T22Totals& tot, const T22ClickReport& cr) {
    tot.clicks += cr.counted.size();
    tot.rawClicks += cr.raw;
    tot.references += cr.references;
    tot.nMin = std::min(tot.nMin, cr.n);
    tot.nMax = std::max(tot.nMax, cr.n);
    for (const auto& [n, dist] : cr.nearest) {
        if (!(dist <= static_cast<double>(cr.n)))
            continue;
        std::size_t bin = 4;   // histogram bins: <= 1, <= 2, <= 4, <= 8, > 8 samples
        if (dist <= 1.0)
            bin = 0;
        else if (dist <= 2.0)
            bin = 1;
        else if (dist <= 4.0)
            bin = 2;
        else if (dist <= 8.0)
            bin = 3;
        ++tot.matchedDist[bin];
    }
}

void t22PrintMatchedDist(const char* label, const T22Totals& tot) {
    std::printf("%s: matched detections by distance to the reference's: <= 1: %zu, <= 2: %zu, <= 4: %zu, <= 8: %zu, "
                "> 8: %zu samples\n",
                label, tot.matchedDist[0], tot.matchedDist[1], tot.matchedDist[2], tot.matchedDist[3],
                tot.matchedDist[4]);
}

// ---- SC-010(a)(b) + SC-016 retrigger cells --------------------------------------

struct T22ClickCell {
    int note = kSpE1;
    double fs = kSpSmokeFs;
    T22Recipe recipe = T22Recipe::Saw;
    AnchorOscillator::Mode mode = AnchorOscillator::Mode::Lock;
    std::size_t block = kSpHostBlock;
    NoteMemory::Mode mem = NoteMemory::Mode::AlwaysRetrigger;
    NoteMemory::Retrigger rt = NoteMemory::Retrigger::Reset;
    bool reuse = false;   // false: the SC-009(b) matrix script; true: the SC-009(c) VoiceReuse script
};

std::string t22CellLabel(const T22ClickCell& c) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s fs %.0f %s %s block %zu %s/%s %s", spNoteName(c.note), c.fs,
                  t22RecipeName(c.recipe), spModeName(c.mode), c.block, t22MemName(c.mem),
                  c.rt == NoteMemory::Retrigger::Reset ? "Reset" : "Continue", c.reuse ? "VoiceReuse" : "matrix");
    return buf;
}

/// Renders one cell; SC-016 always; SC-010(a)(b) when gateClicks.
void t22RunClickCell(const T22ClickCell& c, bool gateClicks, T22SteadyCache& cache, PT::NoteBehaviourTraces& tr,
                     T22Totals& tot) {
    const std::string label = t22CellLabel(c);
    INFO(label);
    auto h = spHarness(c.fs);
    h->core().setControls(t22Controls(c.recipe));
    h->setAnchorControls(spAnchor(c.mode));
    h->setNoteMemory(c.mem, c.rt);
    h->setDrop(kT22DropA, kT22DropMs, 0.0f, 0.0f);
    if (c.reuse) {
        const std::size_t total = spSamples(1.0, c.fs);
        tr.allocate(total);
        h->noteOn(c.note, 100, 0);
        h->voiceReuse(c.note + 10, 100, spSamples(0.5, c.fs));
        h->render(total, c.block, tr);
        const std::array<PT::NoteOutcome, 2> expected{PT::NoteOutcome::FromSilence, PT::NoteOutcome::VoiceReuse};
        REQUIRE(PT::noteOutcomesMatch(expected, tr.recorded()));
    } else {
        const PT::NoteScript sc = PT::noteBehaviourMatrixScript(c.note, c.fs);
        PT::noteBehaviourRunScript(*h, sc, c.block, tr);
        REQUIRE(PT::noteOutcomesMatch(sc.expected, tr.recorded()));
    }
    REQUIRE(h->contractViolations() == 0);
    ++tot.renders;

    // SC-016.
    const bool mono = t22MonoIdentical(tr);
    CHECK(mono);
    if (!mono)
        ++tot.monoBreaks;
    if (!gateClicks)
        return;

    // SC-010(a), the differential count: recorded per cell, not gated (epoch-7 ruling (B)).
    const float cap = h->capHz();
    const T22ClickReport cr = t22CountClicks(tr, c.fs, cap, T22RefSpec{&t22Controls(c.recipe), c.mode, kT22DropA, kT22DropMs, 0.0f, 0.0f}, label);
    std::printf("SC010-A %s: %zu counted of %zu raw detections, N %zu (recorded, not gated)\n", label.c_str(),
                cr.counted.size(), cr.raw, cr.n);
    t22AddClicks(tot, cr);

    // SC-010(b) at every transition: the click gate.
    for (const PT::NoteOutcomeRecord& r : tr.recorded()) {
        if (!t22IsTransition(r) || r.effectiveSample >= tr.length)
            continue;
        const std::size_t eff = r.effectiveSample;
        const std::size_t hi = eff + t22Period(c.fs, static_cast<double>(tr.fRes[eff])) + 1;
        const T22Bound b = t22DeltaBound(tr, tr.L, r.eventSample, hi, c.fs, cap, c.recipe, cache);
        ++tot.windows;
        const double ratio = b.worst / b.bound;
        tot.worstRatio = std::max(tot.worstRatio, ratio);
        if (ratio > 1.0) {
            ++tot.overBound;
            std::printf("SC010-BOUND %s: %s at %zu (effective %zu): max|dy| %.6e at %zu > bound %.6e (f_top %.3f Hz, "
                        "Gm %.4f, Sm %.3e, Dc %.6e, steady Dc %.6e)\n",
                        label.c_str(), PT::noteOutcomeName(r.outcome), r.eventSample, eff, b.worst, b.at, b.bound,
                        b.fTop, b.gm, b.sm, b.dCore, b.dCoreSteady);
        }
        INFO(PT::noteOutcomeName(r.outcome) << " at " << r.eventSample << " effective " << eff << ": max|dy| "
                                            << b.worst << " at " << b.at << ", bound " << b.bound << " (f_top "
                                            << b.fTop << " Hz)");
        CHECK(b.worst <= b.bound);
    }
}

void t22RunRetriggerGrid(std::initializer_list<int> notes, std::initializer_list<double> rates,
                         std::initializer_list<T22Recipe> recipes, std::initializer_list<AnchorOscillator::Mode> modes,
                         std::initializer_list<std::size_t> blocks, bool gateClicks, const char* label) {
    const auto t0 = std::chrono::steady_clock::now();
    T22SteadyCache cache;
    PT::NoteBehaviourTraces tr;
    T22Totals tot;
    for (const int note : notes) {
        for (const double fs : rates) {
            for (const T22Recipe recipe : recipes) {
                for (const AnchorOscillator::Mode mode : modes) {
                    for (const std::size_t block : blocks) {
                        for (const NoteMemory::Mode mem : kT22MemModes) {
                            for (const NoteMemory::Retrigger rt : kT22Retriggers) {
                                for (const bool reuse : {false, true}) {
                                    const T22ClickCell c{.note = note, .fs = fs, .recipe = recipe, .mode = mode,
                                                         .block = block, .mem = mem, .rt = rt, .reuse = reuse};
                                    t22RunClickCell(c, gateClicks, cache, tr, tot);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    std::printf("%s: %zu renders, SC-016 L != R in %zu (bar 0); SC-010(a) %zu counted clicks (recorded, not gated) "
                "of %zu raw detections, N %zu-%zu, %zu reference renders; SC-010(b) %zu windows, %zu over the bound, "
                "worst ratio %.4f (bar 1, the click gate); wall clock %.1f s\n",
                label, tot.renders, tot.monoBreaks, tot.clicks, tot.rawClicks, tot.nMin == std::numeric_limits<std::size_t>::max() ? 0 : tot.nMin, tot.nMax,
                tot.references, tot.windows, tot.overBound, tot.worstRatio, spElapsedSeconds(t0));
    t22PrintMatchedDist(label, tot);
}

// ---- SC-010(c) glides --------------------------------------------------------------

struct T22GlideCell {
    GlideMode first = GlideMode::Quantized;
    GlideMode second = GlideMode::Quantized;
    int delta = 12;
    float timeMs = 500.0f;
    bool sw = false;   // switch first -> second at T/2
};

std::vector<T22GlideCell> t22GlideCells() {
    std::vector<T22GlideCell> out;
    for (const int d : {1, -1, 12, -12, 24, -24}) {
        for (const float T : {10.0f, 500.0f})
            out.push_back({.first = GlideMode::Quantized, .second = GlideMode::Quantized, .delta = d, .timeMs = T});
    }
    for (const int d : {24, -24})
        out.push_back({.first = GlideMode::ConstantRate, .second = GlideMode::ConstantRate, .delta = d, .timeMs = 10.0f});
    constexpr std::array<GlideMode, 3> kAll{GlideMode::ConstantTime, GlideMode::ConstantRate, GlideMode::Quantized};
    for (const GlideMode a : kAll) {
        for (const GlideMode b : kAll) {
            if (a != b)
                out.push_back({.first = a, .second = b, .delta = 12, .timeMs = 500.0f, .sw = true});
        }
    }
    return out;
}

/// Glide length in samples: T for ConstantTime / Quantized, T |delta| / 12 for ConstantRate (ms per octave).
std::size_t t22GlideSamples(GlideMode m, int delta, float timeMs, double fs) {
    double ms = static_cast<double>(timeMs);
    if (m == GlideMode::ConstantRate)
        ms *= std::abs(static_cast<double>(delta)) / 12.0;
    return static_cast<std::size_t>(std::ceil(ms * fs / 1000.0));
}

struct T22GlideRender {
    std::size_t t0 = 0;
    std::size_t switchAt = PT::kNoteBehaviourNoSample;
    std::size_t total = 0;
    float cap = 0.0f;
};

/// Note `low` (or low + |delta| for a downward glide) at 0, Overlap to its target at t0 = 0.3 s (Legato / Continue:
/// no restart, no declick), A = 0. With sw, the glide mode switches at t0 + T/2 between two render() calls.
/// `resetAt` (positive control (c)): a Reset noteOn of the target without the declick at that sample.
T22GlideRender t22RenderGlide(const T22GlideCell& g, int low, double fs, T22Recipe recipe, AnchorOscillator::Mode mode,
                              std::size_t block, PT::NoteBehaviourTraces& tr,
                              std::size_t resetAt = PT::kNoteBehaviourNoSample) {
    auto h = spHarness(fs);
    h->core().setControls(t22Controls(recipe));
    h->setAnchorControls(spAnchor(mode));
    h->setNoteMemory(NoteMemory::Mode::Legato, NoteMemory::Retrigger::Continue);
    h->setDrop(0.0f, kT22DropMs, 0.0f, 0.0f);
    h->mono().setGlideMode(g.first);
    h->mono().setPortamentoTime(g.timeMs);
    const int start = g.delta > 0 ? low : low - g.delta;
    const int target = start + g.delta;
    T22GlideRender out;
    out.t0 = spSamples(0.3, fs);
    const std::size_t len = std::max(t22GlideSamples(g.first, g.delta, g.timeMs, fs),
                                     t22GlideSamples(g.second, g.delta, g.timeMs, fs));
    out.total = out.t0 + len + spSamples(0.3, fs);
    tr.allocate(out.total);
    h->noteOn(start, 100, 0);
    h->noteOn(target, 100, out.t0);
    std::size_t split = out.total;
    if (g.sw) {
        out.switchAt = out.t0 + t22GlideSamples(GlideMode::ConstantTime, g.delta, 0.5f * g.timeMs, fs);
        split = out.switchAt;
    }
    if (resetAt < split)
        split = resetAt;
    h->render(split, block, tr);
    if (resetAt == split) {
        h->setNoteMemory(NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Reset);
        h->setUseDeclick(false);
        h->noteOn(target, 100, 0);
    }
    if (g.sw && out.switchAt == split)
        h->mono().setGlideMode(g.second);
    h->render(out.total - split, block, tr);
    REQUIRE(h->contractViolations() == 0);
    REQUIRE(tr.length == out.total);
    if (resetAt == PT::kNoteBehaviourNoSample) {
        const std::array<PT::NoteOutcome, 2> expected{PT::NoteOutcome::FromSilence, PT::NoteOutcome::Overlap};
        REQUIRE(PT::noteOutcomesMatch(expected, tr.recorded()));
    }
    out.cap = h->capHz();
    return out;
}

std::string t22GlideLabel(const T22GlideCell& g, int note, double fs, T22Recipe recipe, AnchorOscillator::Mode mode,
                          std::size_t block) {
    char buf[256];
    if (g.sw) {
        std::snprintf(buf, sizeof(buf), "%s fs %.0f %s %s block %zu switch %s -> %s (delta %+d, T %.0f ms)",
                      spNoteName(note), fs, t22RecipeName(recipe), spModeName(mode), block, t22GlideName(g.first),
                      t22GlideName(g.second), g.delta, static_cast<double>(g.timeMs));
    } else {
        std::snprintf(buf, sizeof(buf), "%s fs %.0f %s %s block %zu %s delta %+d T %.0f ms", spNoteName(note), fs,
                      t22RecipeName(recipe), spModeName(mode), block, t22GlideName(g.first), g.delta,
                      static_cast<double>(g.timeMs));
    }
    return buf;
}

/// SC-010(c) on one glide render: clicks; the bound at every Quantized step (every core pitch change while a
/// Quantized law runs) and at the switch (4 periods from it).
void t22CheckGlide(const T22GlideCell& g, int note, double fs, T22Recipe recipe, AnchorOscillator::Mode mode,
                   std::size_t block, T22SteadyCache& cache, PT::NoteBehaviourTraces& tr, T22Totals& tot) {
    const std::string label = t22GlideLabel(g, note, fs, recipe, mode, block);
    INFO(label);
    const T22GlideRender gr = t22RenderGlide(g, note, fs, recipe, mode, block, tr);
    ++tot.renders;
    const bool mono = t22MonoIdentical(tr);
    CHECK(mono);
    if (!mono)
        ++tot.monoBreaks;
    // SC-010(a) differential count: recorded per cell, not gated (epoch-7 ruling (B)); (c)'s bound below gates.
    const T22ClickReport cr = t22CountClicks(tr, fs, gr.cap, T22RefSpec{&t22Controls(recipe), mode}, label);
    std::printf("SC010-A %s: %zu counted of %zu raw detections, N %zu (recorded, not gated)\n", label.c_str(),
                cr.counted.size(), cr.raw, cr.n);
    t22AddClicks(tot, cr);

    const auto check = [&](std::size_t lo, std::size_t hi, const char* what) {
        const T22Bound b = t22DeltaBound(tr, tr.L, lo, hi, fs, gr.cap, recipe, cache);
        ++tot.windows;
        const double ratio = b.worst / b.bound;
        tot.worstRatio = std::max(tot.worstRatio, ratio);
        if (ratio > 1.0) {
            ++tot.overBound;
            std::printf("SC010-BOUND %s: %s at %zu: max|dy| %.6e at %zu > bound %.6e (f_top %.3f Hz, Dc %.6e, steady "
                        "Dc %.6e)\n",
                        label.c_str(), what, lo, b.worst, b.at, b.bound, b.fTop, b.dCore, b.dCoreSteady);
        }
        INFO(what << " at " << lo << ": max|dy| " << b.worst << " at " << b.at << ", bound " << b.bound);
        CHECK(b.worst <= b.bound);
    };
    std::size_t steps = 0;
    for (std::size_t n = gr.t0 + 1; n < tr.length; ++n) {
        const bool quantizedHere = (gr.switchAt == PT::kNoteBehaviourNoSample || n < gr.switchAt)
                                       ? g.first == GlideMode::Quantized
                                       : g.second == GlideMode::Quantized;
        if (!quantizedHere || tr.coreFreq[n] == tr.coreFreq[n - 1])
            continue;
        check(n, n + t22Period(fs, static_cast<double>(tr.coreFreq[n])) + 1, "Quantized step");
        ++steps;
    }
    // The core holds f0 for 16 samples, so Quantized steps closer than that merge into one rendered step.
    if (g.first == GlideMode::Quantized && !g.sw)
        CHECK(steps >= 1);
    if (g.sw)
        check(gr.switchAt, gr.switchAt + 4 * t22Period(fs, static_cast<double>(tr.fRes[gr.switchAt])) + 1, "switch");
}

void t22RunGlideGrid(std::initializer_list<int> notes, std::initializer_list<double> rates,
                     std::initializer_list<T22Recipe> recipes, std::initializer_list<AnchorOscillator::Mode> modes,
                     std::initializer_list<std::size_t> blocks, const char* label) {
    const auto t0 = std::chrono::steady_clock::now();
    T22SteadyCache cache;
    PT::NoteBehaviourTraces tr;
    T22Totals tot;
    const std::vector<T22GlideCell> cells = t22GlideCells();
    for (const int note : notes) {
        for (const double fs : rates) {
            for (const T22Recipe recipe : recipes) {
                for (const AnchorOscillator::Mode mode : modes) {
                    for (const std::size_t block : blocks) {
                        for (const T22GlideCell& g : cells)
                            t22CheckGlide(g, note, fs, recipe, mode, block, cache, tr, tot);
                    }
                }
            }
        }
    }
    std::printf("%s: %zu glide renders, SC-016 L != R in %zu (bar 0); SC-010(a) %zu counted clicks (recorded, not "
                "gated) of %zu raw detections, N %zu-%zu, %zu reference renders; %zu step/switch windows, %zu over the "
                "bound, worst ratio %.4f (bar 1, the click gate); wall clock %.1f s\n",
                label, tot.renders, tot.monoBreaks, tot.clicks, tot.rawClicks, tot.nMin == std::numeric_limits<std::size_t>::max() ? 0 : tot.nMin, tot.nMax,
                tot.references, tot.windows, tot.overBound, tot.worstRatio, spElapsedSeconds(t0));
    t22PrintMatchedDist(label, tot);
}

// ---- SC-014 / SC-021 pitch descriptors ---------------------------------------------

/// Mean of the per-sample frequency trace f over [a, b] (sample n holds f[n] over [n, n + 1)).
double t22MeanFreq(const std::vector<float>& f, double a, double b) {
    double sum = 0.0;
    const auto first = static_cast<std::size_t>(std::floor(a));
    for (std::size_t n = first; static_cast<double>(n) < b && n < f.size(); ++n) {
        const double lo = std::max(a, static_cast<double>(n));
        const double hi = std::min(b, static_cast<double>(n + 1));
        if (hi > lo)
            sum += static_cast<double>(f[n]) * (hi - lo);
    }
    return sum / (b - a);
}

struct T22Pitch {
    std::vector<double> dCore;                         // 12 log2(currentFrequency / f_res), per sample
    std::vector<PT::NoteBehaviourZcEstimate> zc;       // f_zc on the core output
    std::vector<double> zcSt;                          // 12 log2(f_zc / f_res) per estimate
    double fRes = 0.0;
    double worstAgreementCents = 0.0;                  // D-15 f_zc vs the half-period mean of f_res 2^(d_core/12)
};

/// d_core and f_zc (core L) of a trace; f_ref is the pitch the offsets are taken against.
T22Pitch t22Pitch(const PT::NoteBehaviourTraces& tr, double fs, double fRef) {
    T22Pitch p;
    p.fRes = fRef;
    p.dCore.resize(tr.length);
    for (std::size_t n = 0; n < tr.length; ++n)
        p.dCore[n] = 12.0 * std::log2(static_cast<double>(tr.coreFreq[n]) / fRef);
    p.zc = PT::noteBehaviourZeroCrossingPitch(std::span<const float>(tr.coreL.data(), tr.length), fs);
    p.zcSt.reserve(p.zc.size());
    for (const PT::NoteBehaviourZcEstimate& e : p.zc) {
        p.zcSt.push_back(12.0 * std::log2(e.hz / fRef));
        const double mean = t22MeanFreq(tr.coreFreq, e.tA, e.tB);
        p.worstAgreementCents = std::max(p.worstAgreementCents, std::abs(1200.0 * std::log2(e.hz / mean)));
    }
    return p;
}

struct T22Descriptors {
    double start = 0.0;   // st
    double ts = 0.0;      // samples
    double alpha = 0.0;
    double eps = 0.0;     // st
    double rho = 0.0;     // st
};

/// Descriptors on d_core (spec "Definitions"; t_s threshold 1 cent; alpha the sample mean over [0, round(N)] / A).
T22Descriptors t22CoreDescriptors(const std::vector<double>& d, double A, double N) {
    T22Descriptors out;
    out.start = d.empty() ? 0.0 : d[0];
    std::size_t landing = d.size();
    double minAll = 0.0;
    for (std::size_t n = 0; n < d.size(); ++n) {
        if (std::abs(d[n]) > 0.01)
            out.ts = static_cast<double>(n);
        if (landing == d.size() && d[n] <= 0.0)
            landing = n;
        minAll = std::min(minAll, d[n]);
    }
    double minAfter = 0.0;
    for (std::size_t n = landing; n < d.size(); ++n)
        minAfter = std::min(minAfter, d[n]);
    out.eps = -minAfter;
    out.rho = -minAll;
    if (A > 0.0) {
        const auto last = std::min(static_cast<std::size_t>(std::llround(N)), d.size() - 1);
        double sum = 0.0;
        for (std::size_t n = 0; n <= last; ++n)
            sum += d[n];
        out.alpha = sum / (static_cast<double>(last + 1) * A);
    }
    return out;
}

/// The same descriptors on f_zc (per half-period estimate; t_s threshold tsCents; alpha duration-weighted over the
/// estimates stamped in [0, N]). t_s is the stamp (tMid) of the first estimate from which every later estimate is
/// within tsCents: the earliest time the half-period estimator can certify as settled. (Fix-up 2026-10-13: "the
/// last unsettled stamp" read 0 whenever the whole fall fits inside the first half-period -- E1, T = 10 ms:
/// t_s 479 samples against the spec's 12.1 ms E1 resolution -- which is no time at all, not a settle time.)
T22Descriptors t22ZcDescriptors(const T22Pitch& p, double A, double N, double tsCents) {
    T22Descriptors out;
    if (p.zc.empty())
        return out;
    out.start = p.zcSt[0];
    std::size_t landing = p.zc.size();
    double minAll = 0.0;
    double num = 0.0;
    double den = 0.0;
    std::size_t settledFrom = 0;
    for (std::size_t k = 0; k < p.zc.size(); ++k) {
        const double st = p.zcSt[k];
        if (std::abs(st) > tsCents / 100.0)
            settledFrom = k + 1;
        if (landing == p.zc.size() && st <= 0.0)
            landing = k;
        minAll = std::min(minAll, st);
        if (p.zc[k].tMid <= N) {
            const double w = p.zc[k].tB - p.zc[k].tA;
            num += st * w;
            den += w;
        }
    }
    out.ts = p.zc[std::min(settledFrom, p.zc.size() - 1)].tMid;
    double minAfter = 0.0;
    for (std::size_t k = landing; k < p.zc.size(); ++k)
        minAfter = std::min(minAfter, p.zcSt[k]);
    out.eps = -minAfter;
    out.rho = -minAll;
    if (A > 0.0 && den > 0.0)
        out.alpha = num / (den * A);
    return out;
}

/// One SC-014 Drop render: the core at kSineAnchor, LOCK (lock active), 48 kHz, host block 37.
T22Pitch t22RenderDrop(int note, float A, float T, double s, float R, PT::NoteBehaviourTraces& tr) {
    const double fs = kSpSmokeFs;
    const float sp = spShapeParam(s);
    const DropCurve curve = spSnapCurve(A, T, sp, R, fs);
    const std::size_t total = static_cast<std::size_t>(std::ceil(curve.endSample)) + spSamples(0.6, fs);
    auto h = spHarness(fs);
    h->core().setControls(SpectralShapeRecipe::kSineAnchor);
    h->setAnchorControls(spAnchor(AnchorOscillator::Mode::Lock));
    h->setDrop(A, T, sp, R);
    tr.allocate(total);
    h->noteOn(note, 100, 0);
    h->render(total, kSpHostBlock, tr);
    REQUIRE(h->contractViolations() == 0);
    REQUIRE(tr.length == total);
    return t22Pitch(tr, fs, static_cast<double>(tr.fRes[0]));
}

/// Window RMS in dB over [a, b).
double t22RmsDb(const std::vector<float>& y, std::size_t a, std::size_t b) {
    double s = 0.0;
    for (std::size_t n = a; n < b; ++n)
        s += static_cast<double>(y[n]) * static_cast<double>(y[n]);
    return PT::safeDb10(s / static_cast<double>(std::max<std::size_t>(1, b - a)));
}

/// D = 10 log10(sum (a - b)^2 / sum b^2) over [lo, hi).
double t22DiffRatioDb(const std::vector<float>& a, const std::vector<float>& b, std::size_t lo, std::size_t hi) {
    double num = 0.0;
    double den = 0.0;
    for (std::size_t n = lo; n < hi; ++n) {
        const double d = static_cast<double>(a[n]) - static_cast<double>(b[n]);
        num += d * d;
        den += static_cast<double>(b[n]) * static_cast<double>(b[n]);
    }
    return PT::safeDb10(num) - PT::safeDb10(den);
}

/// Start of the w-sample window of [lo, hi) with the minimum RMS (the first on ties).
std::size_t t22MinRmsStart(const std::vector<float>& y, std::size_t lo, std::size_t hi, std::size_t w) {
    double best = std::numeric_limits<double>::max();
    std::size_t at = lo;
    for (std::size_t n = lo; n + w <= hi; ++n) {
        const double v = t22RmsDb(y, n, n + w);
        if (v < best) {
            best = v;
            at = n;
        }
    }
    return at;
}

/// Max |RMS-envelope difference| (dB) over consecutive w-sample frames of [lo, hi).
double t22EnvelopeDiffDb(const std::vector<float>& a, const std::vector<float>& b, std::size_t lo, std::size_t hi,
                         std::size_t w) {
    double worst = 0.0;
    for (std::size_t n = lo; n + w <= hi; n += w)
        worst = std::max(worst, std::abs(t22RmsDb(a, n, n + w) - t22RmsDb(b, n, n + w)));
    return worst;
}

/// f_zc offset (st, against fRef) at time t (samples), linear in the estimates' stamps.
double t22ZcAt(const T22Pitch& p, double t) {
    REQUIRE(!p.zc.empty());
    if (t <= p.zc.front().tMid)
        return p.zcSt.front();
    for (std::size_t k = 1; k < p.zc.size(); ++k) {
        if (t <= p.zc[k].tMid) {
            const double a = p.zc[k - 1].tMid;
            const double b = p.zc[k].tMid;
            const double x = (t - a) / (b - a);
            return p.zcSt[k - 1] + x * (p.zcSt[k] - p.zcSt[k - 1]);
        }
    }
    return p.zcSt.back();
}

// ---- SC-015 ----------------------------------------------------------------------

enum class T22TrajKind : std::uint8_t { Drop, Glide, BridgeMidFall, BridgeFromRest };

struct T22Traj {
    std::string name;
    T22TrajKind kind = T22TrajKind::Drop;
    double shape = 0.0;
    bool lock = true;
    GlideMode glide = GlideMode::Quantized;
    float glideMs = 1.0f;
};

std::vector<T22Traj> t22Trajectories(double fs) {
    std::vector<T22Traj> out;
    char buf[96];
    for (const double s : kSpShapes) {
        for (const bool lock : {false, true}) {
            std::snprintf(buf, sizeof(buf), "drop-A48-T1-s%.3f-R1-%s", s, lock ? "lock" : "bypass");
            out.push_back({.name = buf, .kind = T22TrajKind::Drop, .shape = s, .lock = lock});
        }
    }
    const auto oneSampleMs = static_cast<float>(1000.0 / fs);
    for (const GlideMode g : {GlideMode::Quantized, GlideMode::ConstantRate}) {
        for (const float ms : {oneSampleMs, 1.0f}) {
            std::snprintf(buf, sizeof(buf), "glide-%s-up24-T%.4fms", t22GlideName(g), static_cast<double>(ms));
            out.push_back({.name = buf, .kind = T22TrajKind::Glide, .glide = g, .glideMs = ms});
        }
    }
    for (const bool lock : {false, true}) {
        out.push_back({.name = std::string("bridge-midfall-up5-A48-T1-R1-") + (lock ? "lock" : "bypass"),
                       .kind = T22TrajKind::BridgeMidFall, .lock = lock});
        out.push_back({.name = std::string("bridge-fromrest-up5-A48-T1-R1-") + (lock ? "lock" : "bypass"),
                       .kind = T22TrajKind::BridgeFromRest, .lock = lock});
    }
    return out;
}

struct T22AliasResult {
    double aliasedDbfs = 0.0;
    double excluded = 0.0;
    std::size_t trajEnd = 0;
    std::size_t frameStart = 0;
    std::size_t frameLen = 0;
    double fFinal = 0.0;
};

/// One SC-015 cell at host block 1; writes its cell row and its trajectory rows to csv.
T22AliasResult t22RunAliasCell(const T22Traj& t, int note, double fs, T22Recipe recipe, PT::NoteBehaviourTraces& tr,
                               std::ofstream& csv) {
    INFO(t.name << " " << spNoteName(note) << " fs " << fs << " " << t22RecipeName(recipe));
    auto h = spHarness(fs);
    h->core().setControls(t22Controls(recipe));
    h->setAnchorControls(spAnchor(AnchorOscillator::Mode::Lock));
    h->setLockBypass(!t.lock);
    int finalNote = note;
    std::size_t second = PT::kNoteBehaviourNoSample;
    switch (t.kind) {
        case T22TrajKind::Drop:
            h->setDrop(48.0f, 1.0f, spShapeParam(t.shape), 1.0f);
            break;
        case T22TrajKind::Glide:
            h->setNoteMemory(NoteMemory::Mode::Legato, NoteMemory::Retrigger::Continue);
            h->setDrop(0.0f, kT22DropMs, 0.0f, 0.0f);
            h->mono().setGlideMode(t.glide);
            h->mono().setPortamentoTime(t.glideMs);
            second = spSamples(0.02, fs);
            finalNote = note + 24;
            break;
        case T22TrajKind::BridgeMidFall:
        case T22TrajKind::BridgeFromRest:
            h->setNoteMemory(NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Continue);
            h->setDrop(48.0f, 1.0f, 0.0f, 1.0f);
            second = t.kind == T22TrajKind::BridgeMidFall ? static_cast<std::size_t>(std::llround(0.3 * fs / 1000.0))
                                                          : spSamples(0.02, fs);
            finalNote = note + 5;
            break;
    }
    const std::size_t head = spSamples(0.1, fs);
    const auto fFinalApprox = static_cast<double>(Krate::DSP::midiNoteToFrequency(finalNote));
    const std::size_t len = PT::aliasFftLength(fs, fFinalApprox);
    tr.allocate(head + len);
    h->noteOn(note, 100, 0);
    if (second != PT::kNoteBehaviourNoSample)
        h->noteOn(finalNote, 100, second);
    h->render(head, 1, tr);
    if (t.kind == T22TrajKind::BridgeMidFall || t.kind == T22TrajKind::BridgeFromRest) {
        REQUIRE(tr.numOutcomes == 2);
        REQUIRE(tr.recorded()[1].lockKind == DropPhaseLock::TriggerKind::Bridged);
    }

    // The trajectory ends at the last pitch move (commanded or rendered) inside the head.
    T22AliasResult r;
    for (std::size_t n = 1; n < head; ++n) {
        if (tr.fCore[n] != tr.fCore[n - 1] || tr.coreFreq[n] != tr.coreFreq[n - 1])
            r.trajEnd = n + 1;
    }
    r.frameStart = r.trajEnd + 2 * ProfundumCore::kControlInterval;
    REQUIRE(r.frameStart <= head);
    r.frameLen = len;
    const std::size_t need = r.frameStart + len;
    if (need > head)
        h->render(need - head, 1, tr);
    REQUIRE(h->contractViolations() == 0);
    REQUIRE(tr.length >= need);
    r.fFinal = static_cast<double>(tr.coreFreq[r.frameStart]);
    CHECK(std::abs(r.fFinal / fFinalApprox - 1.0) <= 1e-5);
    const PT::AliasResult a = PT::aliasedPower(tr.coreL.data() + r.frameStart, len, fs, r.fFinal);
    r.aliasedDbfs = a.aliasedDbfs;
    r.excluded = a.excludedFraction;
    INFO("trajectory end " << r.trajEnd << ", frame " << r.frameStart << " + " << len << ", f " << r.fFinal
                           << " Hz: aliased " << a.aliasedDbfs << " dBFS, excluded " << a.excludedFraction);
    CHECK(a.excludedFraction <= 0.5);
    CHECK(a.aliasedDbfs <= -96.0);

    char line[512];
    std::snprintf(line, sizeof(line), "cell,%s,,,,,,,%.3f,%.4f,%zu,%zu,%.6f,%zu\n", t.name.c_str(), a.aliasedDbfs,
                  a.excludedFraction, r.frameStart, len, r.fFinal, r.trajEnd);
    csv << line;
    for (std::size_t n = 0; n < r.frameStart; ++n) {
        std::snprintf(line, sizeof(line), "traj,%s,%zu,%.6f,%.6f,%.6f,%.6f,%.6f,,,,,,\n", t.name.c_str(), n,
                      static_cast<double>(tr.fRes[n]), static_cast<double>(tr.d[n]), static_cast<double>(tr.dc[n]),
                      static_cast<double>(tr.fCore[n]), static_cast<double>(tr.coreFreq[n]));
        csv << line;
    }
    return r;
}

void t22RunAliasGrid(std::initializer_list<double> rates, std::initializer_list<int> notes,
                     std::initializer_list<T22Recipe> recipes, const char* label) {
    const auto t0 = std::chrono::steady_clock::now();
    std::filesystem::create_directories(spEvidenceDir());
    PT::NoteBehaviourTraces tr;
    double worst = -1.0e9;
    std::string worstCell;
    std::size_t cells = 0;
    for (const double fs : rates) {
        for (const int note : notes) {
            for (const T22Recipe recipe : recipes) {
                char name[128];
                std::snprintf(name, sizeof(name), "sc015-%.0f-%s-%s.csv", fs, spNoteName(note), t22RecipeName(recipe));
                std::ofstream csv(spEvidenceDir() + name);
                CHECK(csv.is_open());
                csv << "kind,cell,n,f_res_hz,d_st,dc_st,f_core_hz,core_freq_hz,aliased_dbfs,excluded_fraction,"
                       "frame_start,frame_len,f_final_hz,traj_end\n";
                for (const T22Traj& t : t22Trajectories(fs)) {
                    const T22AliasResult r = t22RunAliasCell(t, note, fs, recipe, tr, csv);
                    ++cells;
                    std::printf("SC-015 %s %s fs %.0f %s: aliased %.2f dBFS (bar -96), excluded %.3f, trajectory end %zu, "
                                "frame %zu + %zu at %.4f Hz\n",
                                t22RecipeName(recipe), spNoteName(note), fs, t.name.c_str(), r.aliasedDbfs, r.excluded,
                                r.trajEnd, r.frameStart, r.frameLen, r.fFinal);
                    if (r.aliasedDbfs > worst) {
                        worst = r.aliasedDbfs;
                        worstCell = std::string(name) + " " + t.name;
                    }
                }
            }
        }
    }
    std::printf("%s: %zu cells, worst aliased %.2f dBFS (%s; bar -96), wall clock %.1f s\n", label, cells, worst,
                worstCell.c_str(), spElapsedSeconds(t0));
}

// ---- SC-021 ----------------------------------------------------------------------

/// Stereo 32-bit float WAV (format tag 3), little-endian by construction.
bool noteBehaviourWriteWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R,
                           std::size_t n, double fs) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;
    const auto u32 = [&f](std::uint32_t v) {
        const std::array<char, 4> b{static_cast<char>(v & 0xFFu), static_cast<char>((v >> 8) & 0xFFu),
                                    static_cast<char>((v >> 16) & 0xFFu), static_cast<char>((v >> 24) & 0xFFu)};
        f.write(b.data(), 4);
    };
    const auto u16 = [&f](std::uint32_t v) {
        const std::array<char, 2> b{static_cast<char>(v & 0xFFu), static_cast<char>((v >> 8) & 0xFFu)};
        f.write(b.data(), 2);
    };
    const auto rate = static_cast<std::uint32_t>(std::llround(fs));
    const auto dataBytes = static_cast<std::uint32_t>(n * 2 * sizeof(float));
    f.write("RIFF", 4);
    u32(36u + dataBytes);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    u32(16u);
    u16(3u);   // IEEE float
    u16(2u);
    u32(rate);
    u32(rate * 8u);
    u16(8u);
    u16(32u);
    f.write("data", 4);
    u32(dataBytes);
    for (std::size_t i = 0; i < n; ++i) {
        u32(std::bit_cast<std::uint32_t>(L[i]));
        u32(std::bit_cast<std::uint32_t>(R[i]));
    }
    return f.good();
}

struct T22Preset {
    const char* slug;
    float A;
    float T;
    double s;
    float R;
    AnchorOscillator::Mode mode;
};

// plan S11.4 (FR-068).
// plan S11.4 (FR-068). Kick/bass hybrid re-authored 2026-10-15 under the SC-021 listening verdict
// (spec Clarifications 2026-10-14/15): A 48 from E1 started the fall at 659 Hz and read as a squeak;
// A 24 keeps the elastic snap and lands as a thump.
constexpr std::array<T22Preset, 4> kT22Presets{{
    {"808-attack", 12.0f, 120.0f, 0.0, 0.0f, AnchorOscillator::Mode::Lock},
    {"cinematic-impact", 36.0f, 900.0f, 2.0 / 3.0, 0.3f, AnchorOscillator::Mode::Ghost},
    {"techno-pluck", 7.0f, 25.0f, 1.0 / 3.0, 0.5f, AnchorOscillator::Mode::Punch},
    {"kick-bass-hybrid", 24.0f, 45.0f, 1.0, 1.0f, AnchorOscillator::Mode::Lock},
}};

}  // namespace

// =============================================================================
// T022: SC-010 + SC-016 -- retriggers click-free, L == R
// =============================================================================

// Per push: E1, 48 kHz, kSawAnchor, LOCK, host blocks {1, 512}, all 8 (mode, Retrigger) scripts (matrix + VoiceReuse).
TEST_CASE("ProfundumNoteBehaviour_RetriggerClickFreeSmoke", "[systems][profundum][note-behaviour]") {
    t22RunRetriggerGrid({kSpE1}, {kSpSmokeFs}, {T22Recipe::Saw}, {AnchorOscillator::Mode::Lock}, {1, 512}, true,
                        "SC-010(a)(b) smoke E1 48 kHz kSawAnchor LOCK");
}

// The full SC-010(a)(b) grid, one shard per note (run concurrently by name): 3 rates x kSawAnchor, kGrowl x four
// Anchor modes x host blocks {1, 37, 512} x 8 scripts x {matrix, VoiceReuse}; SC-016 rides along in every render.
TEST_CASE("ProfundumNoteBehaviour_RetriggerClickFree_E0", "[systems][profundum][note-behaviour][long]") {
    t22RunRetriggerGrid({kSpE0}, {44100.0, 48000.0, 96000.0}, {T22Recipe::Saw, T22Recipe::Growl},
                        {kT22Modes[0], kT22Modes[1], kT22Modes[2], kT22Modes[3]}, {kT22Blocks[0], kT22Blocks[1], kT22Blocks[2]},
                        true, "SC-010(a)(b) grid E0");
}

TEST_CASE("ProfundumNoteBehaviour_RetriggerClickFree_E1", "[systems][profundum][note-behaviour][long]") {
    t22RunRetriggerGrid({kSpE1}, {44100.0, 48000.0, 96000.0}, {T22Recipe::Saw, T22Recipe::Growl},
                        {kT22Modes[0], kT22Modes[1], kT22Modes[2], kT22Modes[3]}, {kT22Blocks[0], kT22Blocks[1], kT22Blocks[2]},
                        true, "SC-010(a)(b) grid E1");
}

TEST_CASE("ProfundumNoteBehaviour_RetriggerClickFree_E2", "[systems][profundum][note-behaviour][long]") {
    t22RunRetriggerGrid({kSpE2}, {44100.0, 48000.0, 96000.0}, {T22Recipe::Saw, T22Recipe::Growl},
                        {kT22Modes[0], kT22Modes[1], kT22Modes[2], kT22Modes[3]}, {kT22Blocks[0], kT22Blocks[1], kT22Blocks[2]},
                        true, "SC-010(a)(b) grid E2");
}

TEST_CASE("ProfundumNoteBehaviour_RetriggerClickFree_C3", "[systems][profundum][note-behaviour][long]") {
    t22RunRetriggerGrid({kSpC3}, {44100.0, 48000.0, 96000.0}, {T22Recipe::Saw, T22Recipe::Growl},
                        {kT22Modes[0], kT22Modes[1], kT22Modes[2], kT22Modes[3]}, {kT22Blocks[0], kT22Blocks[1], kT22Blocks[2]},
                        true, "SC-010(a)(b) grid C3");
}

// SC-016 per push (never [long], tasks.md rule 6): every script in all four Anchor modes and both recipes at E1,
// 48 kHz, host block 512 -- Anchor, core and output L == R bitwise. The [long] shards repeat it on every render.
TEST_CASE("ProfundumNoteBehaviour_MonoIdentity", "[systems][profundum][note-behaviour]") {
    t22RunRetriggerGrid({kSpE1}, {kSpSmokeFs}, {T22Recipe::Saw, T22Recipe::Growl},
                        {kT22Modes[0], kT22Modes[1], kT22Modes[2], kT22Modes[3]}, {512}, false,
                        "SC-016 mono identity E1 48 kHz");
}

// =============================================================================
// T022: SC-010(c) -- glides click-free
// =============================================================================

// Per push: E1, 48 kHz, kSawAnchor, LOCK, host block 37, every glide cell.
TEST_CASE("ProfundumNoteBehaviour_GlideClickFree", "[systems][profundum][note-behaviour]") {
    t22RunGlideGrid({kSpE1}, {kSpSmokeFs}, {T22Recipe::Saw}, {AnchorOscillator::Mode::Lock}, {kSpHostBlock},
                    "SC-010(c) glides E1 48 kHz kSawAnchor LOCK");
}

TEST_CASE("ProfundumNoteBehaviour_GlideClickFree_E0", "[systems][profundum][note-behaviour][long]") {
    t22RunGlideGrid({kSpE0}, {44100.0, 48000.0, 96000.0}, {T22Recipe::Saw, T22Recipe::Growl},
                    {kT22Modes[0], kT22Modes[1], kT22Modes[2], kT22Modes[3]}, {kT22Blocks[0], kT22Blocks[1], kT22Blocks[2]},
                    "SC-010(c) glide grid E0");
}

TEST_CASE("ProfundumNoteBehaviour_GlideClickFree_E1", "[systems][profundum][note-behaviour][long]") {
    t22RunGlideGrid({kSpE1}, {44100.0, 48000.0, 96000.0}, {T22Recipe::Saw, T22Recipe::Growl},
                    {kT22Modes[0], kT22Modes[1], kT22Modes[2], kT22Modes[3]}, {kT22Blocks[0], kT22Blocks[1], kT22Blocks[2]},
                    "SC-010(c) glide grid E1");
}

TEST_CASE("ProfundumNoteBehaviour_GlideClickFree_E2", "[systems][profundum][note-behaviour][long]") {
    t22RunGlideGrid({kSpE2}, {44100.0, 48000.0, 96000.0}, {T22Recipe::Saw, T22Recipe::Growl},
                    {kT22Modes[0], kT22Modes[1], kT22Modes[2], kT22Modes[3]}, {kT22Blocks[0], kT22Blocks[1], kT22Blocks[2]},
                    "SC-010(c) glide grid E2");
}

TEST_CASE("ProfundumNoteBehaviour_GlideClickFree_C3", "[systems][profundum][note-behaviour][long]") {
    t22RunGlideGrid({kSpC3}, {44100.0, 48000.0, 96000.0}, {T22Recipe::Saw, T22Recipe::Growl},
                    {kT22Modes[0], kT22Modes[1], kT22Modes[2], kT22Modes[3]}, {kT22Blocks[0], kT22Blocks[1], kT22Blocks[2]},
                    "SC-010(c) glide grid C3");
}

// =============================================================================
// T022: SC-010 positive controls
// =============================================================================

TEST_CASE("ProfundumNoteBehaviour_DeclickPositiveControl", "[systems][profundum][note-behaviour]") {
    const double fs = kSpSmokeFs;
    const T22Recipe recipe = T22Recipe::Saw;
    T22SteadyCache cache;
    PT::NoteBehaviourTraces tr;
    const std::size_t ld = t22DeclickLength(fs);
    const T22RefSpec controlRef{&t22Controls(recipe), AnchorOscillator::Mode::Lock};

    // A sounding E1 (AlwaysRetrigger, Reset, A = 0, LOCK level 1): the event lands where |core + Anchor| peaks in
    // [0.6 s, 1.0 s), so the phase restart has the largest jump to make.
    std::size_t ev = 0;
    {
        auto h = spHarness(fs);
        h->core().setControls(t22Controls(recipe));
        h->setAnchorControls(spAnchor(AnchorOscillator::Mode::Lock));
        h->setNoteMemory(NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Reset);
        h->setDrop(0.0f, kT22DropMs, 0.0f, 0.0f);
        const std::size_t total = spSamples(1.0, fs);
        tr.allocate(total);
        h->noteOn(kSpE1, 100, 0);
        h->render(total, kSpHostBlock, tr);
        double best = -1.0;
        for (std::size_t n = spSamples(0.6, fs); n < total; ++n) {
            const double v = std::abs(static_cast<double>(tr.coreL[n]) + static_cast<double>(tr.anchorL[n]));
            if (v > best) {
                best = v;
                ev = n;
            }
        }
        REQUIRE(best > 0.1);
    }
    const std::size_t total = ev + spSamples(0.3, fs);

    SECTION("a Reset noteOn on a sounding voice without the declick is flagged by (b)") {
        auto h = spHarness(fs);
        h->core().setControls(t22Controls(recipe));
        h->setAnchorControls(spAnchor(AnchorOscillator::Mode::Lock));
        h->setNoteMemory(NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Reset);
        h->setDrop(0.0f, kT22DropMs, 0.0f, 0.0f);
        h->setUseDeclick(false);
        tr.allocate(total);
        h->noteOn(kSpE1, 100, 0);
        h->noteOn(kSpE1 + 5, 100, ev);
        h->render(total, kSpHostBlock, tr);
        REQUIRE(h->contractViolations() == 0);
        REQUIRE(tr.numOutcomes == 2);
        REQUIRE(tr.recorded()[1].effectiveSample == ev);   // applied at once, no dip
        REQUIRE(tr.recorded()[1].coreResetBranch);
        // (a) under the differential count: recorded, not gated (epoch-7 ruling (B): the restart lands on the
        // reference's own saw edge, so a position-only count cannot flag it; (b) is the gate).
        const T22ClickReport cr = t22CountClicks(tr, fs, h->capHz(), controlRef, "positive control (a)");
        const std::size_t nearEvent = t22CountedIn(cr, ev - 8, ev + 8);
        t22PrintNearest(cr, ev - 8, ev + 8, "positive control (a)");
        std::printf("SC-010 positive control (a), undeclicked Reset at %zu: %zu counted detection(s) within 8 samples "
                    "(%zu counted, %zu raw in the render, N %zu; recorded, not gated)\n",
                    ev, nearEvent, cr.counted.size(), cr.raw, cr.n);
        const T22Bound b =
            t22DeltaBound(tr, tr.L, ev, ev + t22Period(fs, static_cast<double>(tr.fRes[ev])) + 1, fs, h->capHz(), recipe,
                          cache);
        std::printf("SC-010 positive control (b), undeclicked Reset at %zu: max|dy| %.6e vs bound %.6e (ratio %.2f, "
                    "must be > 1)\n",
                    ev, b.worst, b.bound, b.worst / b.bound);
        CHECK(b.worst > b.bound);
    }

    SECTION("a declick scheduled one block late (not split) is flagged by (b)") {
        // The undeclicked render's pre-gain sum, with the C-15 dip starting AT the event (the restart already
        // applied there) instead of finishing at it: y = (core + Anchor) * env * g_late.
        auto h = spHarness(fs);
        h->core().setControls(t22Controls(recipe));
        h->setAnchorControls(spAnchor(AnchorOscillator::Mode::Lock));
        h->setNoteMemory(NoteMemory::Mode::AlwaysRetrigger, NoteMemory::Retrigger::Reset);
        h->setDrop(0.0f, kT22DropMs, 0.0f, 0.0f);
        h->setUseDeclick(false);
        tr.allocate(total);
        h->noteOn(kSpE1, 100, 0);
        h->noteOn(kSpE1 + 5, 100, ev);
        h->render(total, kSpHostBlock, tr);
        REQUIRE(h->contractViolations() == 0);
        std::vector<float> y(tr.length, 0.0f);
        for (std::size_t n = 0; n < tr.length; ++n) {
            double g = 1.0;
            if (n >= ev && n - ev < ld) {
                const std::size_t k = n - ev;
                g = k + 1 == ld ? 0.0
                                : 0.5 * (1.0 + std::cos(3.14159265358979323846 * static_cast<double>(k + 1)
                                                        / static_cast<double>(ld)));
            }
            y[n] = static_cast<float>((static_cast<double>(tr.coreL[n]) + static_cast<double>(tr.anchorL[n]))
                                      * static_cast<double>(tr.env[n]) * g);
        }
        const T22Bound b = t22DeltaBound(tr, y, ev, ev + ld + t22Period(fs, static_cast<double>(tr.fRes[ev])) + 1, fs,
                                         h->capHz(), recipe, cache);
        std::printf("SC-010 positive control (b), late declick at %zu: max|dy| %.6e vs bound %.6e (ratio %.2f, must "
                    "be > 1)\n",
                    ev, b.worst, b.bound, b.worst / b.bound);
        CHECK(b.worst > b.bound);
        // (a) under the differential count: recorded, not gated (epoch-7 ruling (B)).
        const T22ClickReport cr =
            t22CountClicks(tr, fs, h->capHz(), controlRef, "positive control (b) late declick", 0, y.data());
        const std::size_t nearEvent = t22CountedIn(cr, ev - 8, ev + ld + 8);
        t22PrintNearest(cr, ev - 8, ev + ld + 8, "positive control (b) late declick");
        std::printf("SC-010 positive control (a) on the late declick at %zu: %zu counted detection(s) in [ev - 8, ev + "
                    "L_d + 8] (%zu counted, %zu raw in the render, N %zu; recorded, not gated)\n",
                    ev, nearEvent, cr.counted.size(), cr.raw, cr.n);
    }

    SECTION("a Quantized step applied with a phase reset is flagged by (c)") {
        const T22GlideCell g{.first = GlideMode::Quantized, .second = GlideMode::Quantized, .delta = 12,
                             .timeMs = 500.0f};
        const T22GlideRender ref = t22RenderGlide(g, kSpE1, fs, recipe, AnchorOscillator::Mode::Lock, kSpHostBlock, tr);
        std::size_t step = PT::kNoteBehaviourNoSample;
        for (std::size_t n = ref.t0 + spSamples(0.1, fs); n < tr.length; ++n) {
            if (tr.coreFreq[n] != tr.coreFreq[n - 1]) {
                step = n;
                break;
            }
        }
        REQUIRE(step != PT::kNoteBehaviourNoSample);
        const T22GlideRender ctl =
            t22RenderGlide(g, kSpE1, fs, recipe, AnchorOscillator::Mode::Lock, kSpHostBlock, tr, step);
        REQUIRE(tr.numOutcomes == 3);
        REQUIRE(tr.recorded()[2].effectiveSample == step);
        REQUIRE(tr.recorded()[2].coreResetBranch);
        const T22Bound b = t22DeltaBound(tr, tr.L, step, step + t22Period(fs, static_cast<double>(tr.coreFreq[step])) + 1,
                                         fs, ctl.cap, recipe, cache);
        std::printf("SC-010 positive control (c), Quantized step at %zu with a phase reset: max|dy| %.6e vs bound "
                    "%.6e (ratio %.2f, must be > 1)\n",
                    step, b.worst, b.bound, b.worst / b.bound);
        CHECK(b.worst > b.bound);
        // (a) under the differential count: recorded, not gated (epoch-7 ruling (B)).
        const T22ClickReport cr = t22CountClicks(tr, fs, ctl.cap, controlRef, "positive control (c) Quantized reset");
        const std::size_t nearStep = t22CountedIn(cr, step - 8, step + 8);
        t22PrintNearest(cr, step - 8, step + 8, "positive control (c) Quantized reset");
        std::printf("SC-010 positive control (a) on the Quantized step at %zu: %zu counted detection(s) within 8 "
                    "samples (%zu counted, %zu raw in the render, N %zu; recorded, not gated)\n",
                    step, nearStep, cr.counted.size(), cr.raw, cr.n);
    }
}

// =============================================================================
// T022: SC-014 audibility (FR-060)
// =============================================================================

TEST_CASE("ProfundumNoteBehaviour_Audibility", "[systems][profundum][note-behaviour]") {
    const auto t0 = std::chrono::steady_clock::now();
    const double fs = kSpSmokeFs;
    constexpr double kAgreementCents = 10.0;
    PT::NoteBehaviourTraces tr;

    const auto agree = [&](const T22Pitch& p, const char* what) {
        INFO(what << ": worst f_zc vs d_core half-period mean " << p.worstAgreementCents << " cents");
        CHECK(p.worstAgreementCents <= kAgreementCents);
    };

    for (const int note : {kSpE1, kT22C2}) {
        INFO("note " << note);

        DYNAMIC_SECTION("Amount 0 -> 48 moves the starting pitch, note " << note) {
            std::vector<T22Descriptors> core;
            std::vector<T22Descriptors> zc;
            for (int a = 0; a <= 48; a += 6) {
                const auto A = static_cast<float>(a);
                const T22Pitch p = t22RenderDrop(note, A, 1000.0f, 1.0 / 3.0, 0.0f, tr);
                agree(p, "Amount");
                const double N = 1000.0 * fs / 1000.0;
                core.push_back(t22CoreDescriptors(p.dCore, static_cast<double>(A), N));
                zc.push_back(t22ZcDescriptors(p, static_cast<double>(A), N, kAgreementCents));
                std::printf("SC-014 Amount %s A %2d: start %.4f st (d_core), %.4f st (f_zc)\n", spNoteName(note), a,
                            core.back().start, zc.back().start);
            }
            for (std::size_t i = 1; i < core.size(); ++i) {
                CHECK(core[i].start > core[i - 1].start);
                CHECK(zc[i].start > zc[i - 1].start);
            }
            CHECK(core.back().start - core.front().start >= 47.0);
            CHECK(zc.back().start - zc.front().start >= 47.0);
        }

        DYNAMIC_SECTION("Time 10 -> 1000 ms moves t_s, note " << note) {
            std::vector<double> tsCore;
            std::vector<double> tsZc;
            for (const float T : {10.0f, 30.0f, 100.0f, 300.0f, 1000.0f}) {
                const T22Pitch p = t22RenderDrop(note, 24.0f, T, 0.0, 0.0f, tr);
                agree(p, "Time");
                const double N = static_cast<double>(T) * fs / 1000.0;
                tsCore.push_back(t22CoreDescriptors(p.dCore, 24.0, N).ts);
                tsZc.push_back(t22ZcDescriptors(p, 24.0, N, kAgreementCents).ts);
                std::printf("SC-014 Time %s T %4.0f ms: t_s %.1f samples (d_core), %.1f (f_zc)\n", spNoteName(note),
                            static_cast<double>(T), tsCore.back(), tsZc.back());
            }
            for (std::size_t i = 1; i < tsCore.size(); ++i) {
                CHECK(tsCore[i] > tsCore[i - 1]);
                CHECK(tsZc[i] > tsZc[i - 1]);
            }
            REQUIRE(tsCore.front() > 0.0);
            REQUIRE(tsZc.front() > 0.0);
            CHECK(tsCore.back() / tsCore.front() >= 10.0);
            CHECK(tsZc.back() / tsZc.front() >= 10.0);
        }

        DYNAMIC_SECTION("Shape 0 -> 2/3 moves alpha; Shape 2/3 -> 1 moves epsilon, note " << note) {
            const double N = 1000.0 * fs / 1000.0;
            std::vector<double> aCore;
            std::vector<double> aZc;
            for (const double s : {0.0, 1.0 / 6.0, 1.0 / 3.0, 0.5, 2.0 / 3.0}) {
                const T22Pitch p = t22RenderDrop(note, 24.0f, 1000.0f, s, 0.0f, tr);
                agree(p, "Shape low");
                aCore.push_back(t22CoreDescriptors(p.dCore, 24.0, N).alpha);
                aZc.push_back(t22ZcDescriptors(p, 24.0, N, kAgreementCents).alpha);
                std::printf("SC-014 Shape %s s %.4f: alpha %.5f (d_core), %.5f (f_zc)\n", spNoteName(note), s,
                            aCore.back(), aZc.back());
            }
            for (std::size_t i = 1; i < aCore.size(); ++i) {
                CHECK(aCore[i] > aCore[i - 1]);
                CHECK(aZc[i] > aZc[i - 1]);
            }
            CHECK(aCore.back() - aCore.front() >= 0.6);
            CHECK(aZc.back() - aZc.front() >= 0.6);

            std::vector<double> eCore;
            std::vector<double> eZc;
            for (const double s : {2.0 / 3.0, 5.0 / 6.0, 1.0}) {
                const T22Pitch p = t22RenderDrop(note, 24.0f, 1000.0f, s, 0.0f, tr);
                agree(p, "Shape high");
                eCore.push_back(t22CoreDescriptors(p.dCore, 24.0, N).eps);
                eZc.push_back(t22ZcDescriptors(p, 24.0, N, kAgreementCents).eps);
                std::printf("SC-014 Shape %s s %.4f: epsilon %.5f st (d_core), %.5f st (f_zc)\n", spNoteName(note), s,
                            eCore.back(), eZc.back());
            }
            for (std::size_t i = 1; i < eCore.size(); ++i) {
                CHECK(eCore[i] >= eCore[i - 1]);
                CHECK(eZc[i] >= eZc[i - 1]);
            }
            CHECK(eCore.back() - eCore.front() >= 0.1 * 24.0);
            CHECK(eZc.back() - eZc.front() >= 0.1 * 24.0);
        }

        DYNAMIC_SECTION("Rebound 0 -> 1 moves rho, note " << note) {
            const double N = 1000.0 * fs / 1000.0;
            std::vector<double> rCore;
            std::vector<double> rZc;
            for (const float R : {0.0f, 0.25f, 0.5f, 1.0f}) {
                const T22Pitch p = t22RenderDrop(note, 24.0f, 1000.0f, 1.0 / 3.0, R, tr);
                agree(p, "Rebound");
                rCore.push_back(t22CoreDescriptors(p.dCore, 24.0, N).rho);
                rZc.push_back(t22ZcDescriptors(p, 24.0, N, kAgreementCents).rho);
                std::printf("SC-014 Rebound %s R %.2f: rho %.5f st (d_core), %.5f st (f_zc)\n", spNoteName(note),
                            static_cast<double>(R), rCore.back(), rZc.back());
            }
            for (std::size_t i = 1; i < rCore.size(); ++i) {
                CHECK(rCore[i] > rCore[i - 1]);
                CHECK(rZc[i] > rZc[i - 1]);
            }
            CHECK(rCore.back() - rCore.front() >= 0.2 * 24.0);
            CHECK(rZc.back() - rZc.front() >= 0.2 * 24.0);
        }
    }

    SECTION("Retrigger: Reset vs Continue at every audible onset") {
        const PT::NoteScript sc = PT::noteBehaviourStandardScript(kSpE1, fs);
        std::array<PT::NoteBehaviourTraces, 2> r;   // [0] Reset, [1] Continue
        for (std::size_t i = 0; i < 2; ++i) {
            auto h = spHarness(fs);
            h->core().setControls(SpectralShapeRecipe::kSawAnchor);
            h->setAnchorControls(spAnchor(AnchorOscillator::Mode::Lock));
            h->setNoteMemory(NoteMemory::Mode::AlwaysRetrigger,
                             i == 0 ? NoteMemory::Retrigger::Reset : NoteMemory::Retrigger::Continue);
            h->setDrop(0.0f, kT22DropMs, 0.0f, 0.0f);
            PT::noteBehaviourRunScript(*h, sc, kSpHostBlock, r[i]);
            REQUIRE(h->contractViolations() == 0);
            REQUIRE(PT::noteOutcomesMatch(sc.expected, r[i].recorded()));
        }
        REQUIRE(r[0].length == r[1].length);
        const std::size_t w = static_cast<std::size_t>(std::llround(0.25e-3 * fs));
        std::size_t onsets = 0;
        const auto recs = r[0].recorded();
        for (std::size_t k = 0; k < recs.size(); ++k) {
            const PT::NoteOutcomeRecord& rec = recs[k];
            if (!t22IsTransition(rec) || !rec.audible)
                continue;
            ++onsets;
            const std::size_t lo = rec.eventSample;
            const std::size_t eff = rec.effectiveSample;
            REQUIRE(eff < r[0].length);
            const std::size_t hi = std::min(r[0].length, eff + t22Period(fs, static_cast<double>(r[0].fRes[eff])) + 1);
            const double D = t22DiffRatioDb(r[0].L, r[1].L, lo, hi);
            // Ruling (C) (T023 epoch 7, spec Clarifications 2026-10-13 "T023 epoch 6 readings"): the reference is the
            // Continue render's RMS over ONE PERIOD of the new note centred on the (Continue render's) effective
            // sample -- phase-independent, unlike any 0.25 ms slice (epoch 5 read Continue's own minimum, epoch 6 the
            // Reset dip's window; both held a zero crossing). The Reset render's minimum 0.25 ms RMS in the onset
            // window must be >= 20 dB below it.
            const std::size_t dipAt = t22MinRmsStart(r[0].L, lo, hi, w);
            const double minReset = t22RmsDb(r[0].L, dipAt, dipAt + w);
            REQUIRE(k < r[1].recorded().size());
            const std::size_t effC = r[1].recorded()[k].effectiveSample;
            REQUIRE(effC < r[1].length);
            const std::size_t periodC = t22Period(fs, static_cast<double>(r[1].fRes[effC]));
            const std::size_t refLo = effC >= periodC / 2 ? effC - periodC / 2 : 0;
            const std::size_t refHi = std::min(r[1].length, refLo + periodC);
            const double refContinue = t22RmsDb(r[1].L, refLo, refHi);
            std::printf("SC-014 Retrigger onset %zu (%s at %zu, effective %zu): D %.2f dB (bar >= -10); Reset minimum "
                        "0.25 ms RMS %.2f dB at [%zu, %zu) vs Continue one-period RMS %.2f dB over [%zu, %zu) (centred "
                        "on Continue's effective %zu): gap %.2f dB (bar >= 20); same-window Continue %.2f dB "
                        "(recorded)\n",
                        k, PT::noteOutcomeName(rec.outcome), lo, eff, D, minReset, dipAt, dipAt + w, refContinue, refLo,
                        refHi, effC, refContinue - minReset, t22RmsDb(r[1].L, dipAt, dipAt + w));
            INFO("onset " << k);
            CHECK(D >= -10.0);
            CHECK(minReset <= refContinue - 20.0);
        }
        CHECK(onsets >= 3);
    }

    SECTION("Note Memory: every mode pair differs (Reset); the Continue collapse is bit-identical") {
        const PT::NoteScript sc = PT::noteBehaviourStandardScript(kSpE1, fs);
        const std::size_t ld = t22DeclickLength(fs);
        const std::size_t w = static_cast<std::size_t>(std::llround(0.25e-3 * fs));
        for (const NoteMemory::Retrigger rt : kT22Retriggers) {
            std::array<PT::NoteBehaviourTraces, 4> r;
            for (std::size_t m = 0; m < 4; ++m) {
                auto h = spHarness(fs);
                h->core().setControls(SpectralShapeRecipe::kSawAnchor);
                h->setAnchorControls(spAnchor(AnchorOscillator::Mode::Lock));
                h->setNoteMemory(kT22MemModes[m], rt);
                h->setDrop(kT22DropA, kT22DropMs, 0.0f, 0.0f);
                PT::noteBehaviourRunScript(*h, sc, kSpHostBlock, r[m]);
                REQUIRE(h->contractViolations() == 0);
                REQUIRE(PT::noteOutcomesMatch(sc.expected, r[m].recorded()));
            }
            if (rt == NoteMemory::Retrigger::Continue) {
                // FR-034 collapse: {AlwaysRetrigger, EnvelopeRetriggerOnly} and {Legato, OscillatorContinuous}.
                CHECK(PT::samplesBitEqual(r[0].L.data(), r[2].L.data(), r[0].length));
                CHECK(PT::samplesBitEqual(r[1].L.data(), r[3].L.data(), r[1].length));
                continue;
            }
            for (std::size_t a = 0; a < 4; ++a) {
                for (std::size_t b = a + 1; b < 4; ++b) {
                    double bestEnv = 0.0;
                    double bestD = -1.0e9;
                    for (std::size_t k = 0; k < sc.events.size(); ++k) {
                        if (sc.expected[k] == PT::NoteOutcome::Release || sc.expected[k] == PT::NoteOutcome::NoAction)
                            continue;
                        const std::size_t lo = sc.events[k].at;
                        const int newNote = r[a].recorded()[k].note;
                        const std::size_t hi = std::min(
                            r[a].length,
                            lo + ld
                                + t22Period(fs, static_cast<double>(Krate::DSP::midiNoteToFrequency(newNote))) + 1);
                        bestEnv = std::max(bestEnv, t22EnvelopeDiffDb(r[a].L, r[b].L, lo, hi, w));
                        bestD = std::max(bestD, t22DiffRatioDb(r[a].L, r[b].L, lo, hi));
                    }
                    std::printf("SC-014 Note Memory %s vs %s (Reset): max RMS-envelope difference %.2f dB (bar >= 3), "
                                "max D %.2f dB (bar >= -10)\n",
                                t22MemName(kT22MemModes[a]), t22MemName(kT22MemModes[b]), bestEnv, bestD);
                    INFO(t22MemName(kT22MemModes[a]) << " vs " << t22MemName(kT22MemModes[b]));
                    CHECK((bestEnv >= 3.0 || bestD >= -10.0));
                }
            }
        }
    }

    SECTION("Glide: C2 -> C4 over 500 ms, the three laws are distinct") {
        constexpr float kT = 500.0f;
        const std::size_t tStart = spSamples(0.3, fs);
        const std::size_t tLen = spSamples(static_cast<double>(kT) / 1000.0, fs);
        const std::size_t total = tStart + spSamples(1.2, fs);
        constexpr std::array<GlideMode, 3> kLaws{GlideMode::ConstantTime, GlideMode::ConstantRate, GlideMode::Quantized};
        std::array<PT::NoteBehaviourTraces, 3> r;
        std::vector<T22Pitch> p;
        for (std::size_t i = 0; i < 3; ++i) {
            auto h = spHarness(fs);
            h->core().setControls(SpectralShapeRecipe::kSineAnchor);
            h->setAnchorControls(spAnchor(AnchorOscillator::Mode::Lock));
            h->setNoteMemory(NoteMemory::Mode::Legato, NoteMemory::Retrigger::Continue);
            h->setDrop(0.0f, kT22DropMs, 0.0f, 0.0f);
            h->mono().setGlideMode(kLaws[i]);
            h->mono().setPortamentoTime(kT);
            r[i].allocate(total);
            h->noteOn(kT22C2, 100, 0);
            h->noteOn(kT22C2 + 24, 100, tStart);
            h->render(total, kSpHostBlock, r[i]);
            REQUIRE(h->contractViolations() == 0);
            const std::array<PT::NoteOutcome, 2> expected{PT::NoteOutcome::FromSilence, PT::NoteOutcome::Overlap};
            REQUIRE(PT::noteOutcomesMatch(expected, r[i].recorded()));
            p.push_back(t22Pitch(r[i], fs, static_cast<double>(r[i].fRes[0])));
            agree(p.back(), t22GlideName(kLaws[i]));
        }
        const std::size_t half = tStart + tLen / 2;
        const double ct = p[0].dCore[half];
        const double cr = p[1].dCore[half];
        const double q = p[2].dCore[half];
        double maxDev = 0.0;
        for (std::size_t n = tStart; n < tStart + tLen; ++n)
            maxDev = std::max(maxDev, std::abs(p[2].dCore[n] - p[0].dCore[n]));
        const auto hz = static_cast<double>(half);
        const double ctZ = t22ZcAt(p[0], hz);
        const double crZ = t22ZcAt(p[1], hz);
        const double qZ = t22ZcAt(p[2], hz);
        double maxDevZ = 0.0;
        const std::size_t step = spSamples(0.001, fs);
        const std::size_t from = tStart + t22Period(fs, static_cast<double>(r[0].fRes[0]));
        for (std::size_t n = from; n < tStart + tLen; n += step) {
            const auto t = static_cast<double>(n);
            maxDevZ = std::max(maxDevZ, std::abs(t22ZcAt(p[2], t) - t22ZcAt(p[0], t)));
        }
        // Quantized plateaus on d_core: every pitch change of the core is one semitone; 24 of them.
        std::size_t steps = 0;
        double worstStepErr = 0.0;
        for (std::size_t n = tStart + 1; n < total; ++n) {
            if (r[2].coreFreq[n] == r[2].coreFreq[n - 1])
                continue;
            ++steps;
            worstStepErr = std::max(worstStepErr, std::abs((p[2].dCore[n] - p[2].dCore[n - 1]) - 1.0));
        }
        std::printf("SC-014 Glide C2 -> C4 at T/2: CT %.3f, CR %.3f, Q %.3f st (d_core); CT %.3f, CR %.3f, Q %.3f st "
                    "(f_zc); max |Q - CT| %.3f st (d_core), %.3f st (f_zc); Quantized %zu steps, worst |step - 1 st| "
                    "%.5f st\n",
                    ct, cr, q, ctZ, crZ, qZ, maxDev, maxDevZ, steps, worstStepErr);
        CHECK(std::abs(ct - cr) >= 5.0);
        CHECK(std::abs(q - cr) >= 5.0);
        CHECK(maxDev >= 0.5);
        CHECK(std::abs(ctZ - crZ) >= 5.0);
        CHECK(std::abs(qZ - crZ) >= 5.0);
        CHECK(maxDevZ >= 0.5);
        CHECK(steps == 24);
        CHECK(worstStepErr <= 0.01);
    }
    std::printf("ProfundumNoteBehaviour_Audibility section wall clock %.1f s\n", spElapsedSeconds(t0));
}

// =============================================================================
// T022: SC-015 trajectory aliasing (O-1 ruling: rendered aliased power <= -96 dBFS)
// =============================================================================

// Per push: 48 kHz, E1 and C3, kSawAnchor, every trajectory cell.
TEST_CASE("ProfundumNoteBehaviour_TrajectoryNyquistSmoke", "[systems][profundum][note-behaviour]") {
    t22RunAliasGrid({kSpSmokeFs}, {kSpE1, kSpC3}, {T22Recipe::Saw}, "SC-015 smoke");
}

TEST_CASE("ProfundumNoteBehaviour_TrajectoryNyquist", "[systems][profundum][note-behaviour][long]") {
    t22RunAliasGrid({44100.0, 48000.0, 96000.0}, {kSpE0, kSpE1, kSpE2, kSpC3}, {T22Recipe::Saw, T22Recipe::Growl},
                    "SC-015 grid");
}

// =============================================================================
// T022: SC-021 use-case renders (FR-068)
// =============================================================================

TEST_CASE("ProfundumNoteBehaviour_UseCaseRenders", "[systems][profundum][note-behaviour]") {
    const double fs = kSpSmokeFs;
    std::filesystem::create_directories(spEvidenceDir());
    const double ceiling = static_cast<double>(Krate::DSP::HarmonicOscillatorBank::kOutputClamp)
                               * static_cast<double>(ProfundumCore::kBankHeadroom)
                           + static_cast<double>(AnchorOscillator::kOutputCeiling);
    PT::NoteBehaviourTraces tr;
    T22SteadyCache cache;
    for (const int note : {kSpE1, kT22C2}) {
        const char* noteName = note == kSpE1 ? "E1" : "C2";
        std::array<T22Descriptors, kT22Presets.size()> desc{};
        for (std::size_t i = 0; i < kT22Presets.size(); ++i) {
            const T22Preset& pr = kT22Presets[i];
            INFO(pr.slug << " " << noteName);
            const float sp = spShapeParam(pr.s);
            auto h = spHarness(fs);
            h->setAnchorControls(spAnchor(pr.mode));
            h->setDrop(pr.A, pr.T, sp, pr.R);
            const std::size_t hold = spSamples(1.5, fs);
            const std::size_t total = hold + spSamples(0.5, fs);
            tr.allocate(total);
            h->noteOn(note, 100, 0);
            h->noteOff(note, hold);
            h->render(total, 1, tr);
            REQUIRE(h->contractViolations() == 0);
            REQUIRE(tr.length == total);

            double peak = 0.0;
            bool finite = true;
            for (std::size_t n = 0; n < total; ++n) {
                finite = finite && Krate::DSP::detail::isFinite(tr.L[n]) && Krate::DSP::detail::isFinite(tr.R[n]);
                peak = std::max({peak, std::abs(static_cast<double>(tr.L[n])), std::abs(static_cast<double>(tr.R[n]))});
            }
            const T22ClickReport cr = t22CountClicks(tr, fs, h->capHz(), T22RefSpec{nullptr, pr.mode, pr.A, pr.T, sp, pr.R},
                                                     std::string(pr.slug) + " " + noteName);
            const std::size_t clicks = cr.counted.size();   // SC-010(a): recorded, not gated (epoch-7 ruling (B))
            // SC-021 "zero clicks (SC-010 method)" = (b) at every transition window (epoch-7 ruling (B)).
            std::size_t overBound = 0;
            std::size_t windows = 0;
            double worstRatio = 0.0;
            for (const PT::NoteOutcomeRecord& r : tr.recorded()) {
                if (!t22IsTransition(r) || r.effectiveSample >= tr.length)
                    continue;
                const std::size_t eff = r.effectiveSample;
                const std::size_t hi = eff + t22Period(fs, static_cast<double>(tr.fRes[eff])) + 1;
                const T22Bound b = t22DeltaBound(tr, tr.L, r.eventSample, hi, fs, h->capHz(), T22Recipe::Default, cache);
                ++windows;
                worstRatio = std::max(worstRatio, b.worst / b.bound);
                if (b.worst > b.bound)
                    ++overBound;
            }
            const T22Pitch p = t22Pitch(tr, fs, static_cast<double>(tr.fRes[0]));
            const double N =static_cast<double>(pr.T) * fs / 1000.0;
            desc[i] = t22CoreDescriptors(p.dCore, static_cast<double>(pr.A), N);

            const std::string base = spEvidenceDir() + "usecase-" + pr.slug + "-" + noteName;
            CHECK(noteBehaviourWriteWav(base + ".wav", tr.L, tr.R, total, fs));
            std::ofstream csv(base + ".csv");
            CHECK(csv.is_open());
            char line[512];
            csv << "preset,note,A_st,T_ms,s,R,anchor,start_st,t_s_ms,alpha,rho_st,epsilon_st,peak,ceiling,clicks_a,"
                   "b_windows,b_over,b_worst_ratio\n";
            std::snprintf(line, sizeof(line),
                          "%s,%s,%.1f,%.1f,%.6f,%.2f,%s,%.6f,%.3f,%.6f,%.6f,%.6f,%.6f,%.4f,%zu,%zu,%zu,%.4f\n",
                          pr.slug, noteName, static_cast<double>(pr.A), static_cast<double>(pr.T), pr.s,
                          static_cast<double>(pr.R), spModeName(pr.mode), desc[i].start, desc[i].ts * 1000.0 / fs,
                          desc[i].alpha, desc[i].rho, desc[i].eps, peak, ceiling, clicks, windows, overBound,
                          worstRatio);
            csv << line;
            std::printf("SC-021 %s %s: start %.3f st, t_s %.2f ms, alpha %.4f, rho %.4f st, epsilon %.4f st, peak %.4f "
                        "(ceiling %.2f), SC-010(b) %zu of %zu window(s) over the bound, worst ratio %.4f (bar 1, the "
                        "click gate); SC-010(a) %zu counted (recorded)\n",
                        pr.slug, noteName, desc[i].start, desc[i].ts * 1000.0 / fs, desc[i].alpha, desc[i].rho,
                        desc[i].eps, peak, ceiling, overBound, windows, worstRatio, clicks);
            CHECK(finite);
            CHECK(peak <= ceiling);
            CHECK(windows >= 1);   // non-vacuous: the onset window was checked
            CHECK(overBound == 0);
        }
        for (std::size_t a = 0; a < kT22Presets.size(); ++a) {
            for (std::size_t b = a + 1; b < kT22Presets.size(); ++b) {
                const T22Descriptors& x = desc[a];
                const T22Descriptors& y = desc[b];
                const double maxA = static_cast<double>(std::max(kT22Presets[a].A, kT22Presets[b].A));
                // A zero t_s on one side (no Drop motion) counts as an unbounded ratio; a finite sentinel, because
                // this TU builds under fast-math.
                const double tsRatio =
                    (std::min(x.ts, y.ts) > 0.0) ? std::max(x.ts, y.ts) / std::min(x.ts, y.ts) : 1.0e300;
                const bool distinct = std::abs(x.start - y.start) >= 6.0 || tsRatio >= 2.0
                                      || std::abs(x.alpha - y.alpha) >= 0.1 || std::abs(x.rho - y.rho) >= 0.05 * maxA;
                std::printf("SC-021 %s %s vs %s: |d start| %.3f st, t_s ratio %.3f, |d alpha| %.4f, |d rho| %.4f st "
                            "(bars 6, 2, 0.1, %.2f)\n",
                            noteName, kT22Presets[a].slug, kT22Presets[b].slug, std::abs(x.start - y.start), tsRatio,
                            std::abs(x.alpha - y.alpha), std::abs(x.rho - y.rho), 0.05 * maxA);
                INFO(kT22Presets[a].slug << " vs " << kT22Presets[b].slug << " " << noteName);
                CHECK(distinct);
            }
        }
    }
}
