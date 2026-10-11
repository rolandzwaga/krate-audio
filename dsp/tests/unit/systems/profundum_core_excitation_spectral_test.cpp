// ==============================================================================
// Layer 3: System Tests - ProfundumCore excitation, spectral TU
//                                    (Profundum Phase 2, specs/profundum-phase2-edge-fold)
// ==============================================================================
// Reference: specs/profundum-phase2-edge-fold/plan.md (S5, S8.1, S8.2)
//            specs/profundum-phase2-edge-fold/tasks.md (T006 wires this TU,
//                                                       T014 fills it)
//
// SCOPE OF THIS TU (plan S8.2): excitation descriptor gates - SC-001,
//   SC-002 (rendered arms), SC-003, SC-011 (ExcitationDcFree), SC-012
//   (excitation arm), SC-020.
//
// T014 cases (spec names; full grids [long], each with a per-push ...Smoke sibling):
//   ProfundumCore_ExcitationDriveMonotonic / ...Smoke     SC-001 drive sweep
//   ProfundumCore_ExcitationMixMonotonic / ...Smoke       SC-001 mix sweep
//   ProfundumCore_EdgeDrivesExcitationRendered / ...Smoke SC-002(b)
//   ProfundumCore_EdgeZeroStillExcites                    SC-002(c) first clause
//   ProfundumCore_MixIndependentOfEdge                    SC-002(c) blend clause (plan S1 C-9),
//                                                         prints SC-022(g)
//   ProfundumCore_ExcitationEvenHarmonicsMonotonic / ...Smoke  SC-003(a)
//   ProfundumCore_ExcitationTriangleTail                  SC-003(b)
//   ProfundumCore_ExcitationDcFree                        SC-011 (excitation)
//   ProfundumCore_ExcitationFundamentalPreserved / ...Smoke    SC-012(a)-(c) (plan S1 C-2, C-6)
//   ProfundumCore_ExcitationLevelLaw / ...Smoke           SC-020
//
// Measurement conventions (tasks.md rules 8-10, spec "Descriptor definitions"):
// - every render is a fresh, prepared core driven by PT::renderExcited (policy, controls,
//   noteOn, 32-sample blocks); every render has an all-zero pan vector, so every render calls
//   PT::requireLREqual and the per-channel figures are those of L;
// - the "reference" is the twin mix-0 render (PT::renderMixZeroTwin: a second core, same call
//   sequence, f0, recipe state and ExcitationControls except mix = 0);
// - every steady window starts at t_settle = max(0.5 s, 8·τ) with τ = 1/(2π·kExcitationDcCornerHz)
//   (0.637 s), computed from the header constant;
// - THD, H, E_even and the odd/even ratio from PT::renderedHarmonicPowers on one
//   kLowFrequencyFftSize frame, n·f0 < fs/2; T₁ and E_gen from the BH7 Goertzel helpers over a
//   16-period window; DC and RMS over an integer number of f0 periods spanning >= 1 s.
//
// Rates: 44.1 / 48 / 96 kHz unless stated (tasks.md rule 10). A [long] case runs every rate; its
// Smoke sibling runs 48 kHz (and C2 where tasks.md T014 says "C2 only"), except that
// ...FundamentalPreservedSmoke runs SC-012(a) on its full grid (E0-E2 x 44.1/48/96 kHz: short renders)
// and (b)(c) at 48 kHz, E1 and C2. SC-012(c) names no input; it runs on (b)'s two inputs.
//
// No bar here carries a measurement allowance (tasks.md rule 12). A miss is a defect for
// tasks.md "Defects found" (rule 11); the headers are read-only for this task.
// ==============================================================================

#include "profundum_excitation_test_helpers.h"

#include "low_frequency_metrics.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/profundum_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <span>
#include <utility>
#include <vector>

using Krate::DSP::ProfundumCore;
using Krate::DSP::SpectralShapeRecipe;
namespace PT = Krate::DSP::ProfundumTest;

namespace {

using Controls = SpectralShapeRecipe::Controls;
using Excitation = ProfundumCore::ExcitationControls;

// Notes (tasks.md rule 10).
constexpr float kE0 = 20.6017f;   // MIDI 16
constexpr float kE1 = 41.2034f;   // MIDI 28
constexpr float kE2 = 82.4069f;   // MIDI 40
constexpr float kC2 = 65.4064f;   // MIDI 36

constexpr std::array<double, 3> kRates{44100.0, 48000.0, 96000.0};
constexpr std::array<double, 1> kSmokeRates{48000.0};
constexpr std::array<float, 4> kGateNotes{kE0, kE1, kE2, kC2};
constexpr std::array<float, 3> kLowNotes{kE0, kE1, kE2};
constexpr std::array<float, 2> kLeverNotes{kE1, kC2};
constexpr std::array<float, 1> kSmokeNotes{kC2};

/// Rendered-descriptor frame length (spec: kLowFrequencyFftSize = 262 144 samples).
constexpr std::size_t kFrame = Krate::DSP::TestUtils::kLowFrequencyFftSize;
/// BH7 Goertzel window for T₁, E_gen and the blend residual: 16 f0 periods (plan S8.2 "steady
/// 16-period window"); harmonics then sit 16 bins apart, outside the BH7 main lobe.
constexpr double kPhasorPeriods = 16.0;
constexpr double kRadToDeg = 57.295779513082320876798154814105;

struct NamedInput {
    const char* name;
    Controls controls;
};

/// SC-012(a) / SC-020 inputs (plan S1 C-6): kSineAnchor, the mid grid, kSawAnchor.
std::array<NamedInput, 3> sineMidSaw() {
    return {{{"kSineAnchor", SpectralShapeRecipe::kSineAnchor},
             {"midGrid", PT::midGrid()},
             {"kSawAnchor", SpectralShapeRecipe::kSawAnchor}}};
}

/// SC-011 / SC-012(b)(c) inputs: kSineAnchor and kSawAnchor.
std::array<NamedInput, 2> sineSaw() {
    return {{{"kSineAnchor", SpectralShapeRecipe::kSineAnchor}, {"kSawAnchor", SpectralShapeRecipe::kSawAnchor}}};
}

Controls withEdge(Controls c, float edge) {
    c.edge = edge;
    return c;
}

/// 33 evenly spaced points over [lo, hi], both ends included (spec sweep resolution).
std::vector<float> sweep33(float lo, float hi) {
    std::vector<float> xs(33);
    for (std::size_t i = 0; i < xs.size(); ++i)
        xs[i] = lo + (hi - lo) * static_cast<float>(i) / 32.0f;
    return xs;
}

/// tasks.md rule 9: t_settle = max(0.5 s, 8·τ) with τ the 2 Hz DC corner (header constant).
double settleSeconds() {
    const double tauDc = 1.0 / (PT::kTwoPi * static_cast<double>(ProfundumCore::kExcitationDcCornerHz));
    return std::max(0.5, 8.0 * tauDc);
}

std::size_t settleSamples(double fs) { return static_cast<std::size_t>(std::ceil(settleSeconds() * fs)); }

/// Render length holding t_settle plus a `window`-sample steady window (plus one control interval).
double secondsFor(std::size_t window, double fs) {
    return static_cast<double>(settleSamples(fs) + window + ProfundumCore::kControlInterval) / fs;
}

double steadyFrameSeconds(double fs) { return secondsFor(kFrame, fs); }

std::size_t phasorLength(double fs, float f0) {
    return static_cast<std::size_t>(std::llround(kPhasorPeriods * fs / static_cast<double>(f0)));
}

/// An integer number of f0 periods spanning >= 1 s (one spare period against rounding).
std::size_t periodWindowLength(double fs, float f0) {
    const double periods = std::ceil(static_cast<double>(f0)) + 1.0;
    return static_cast<std::size_t>(std::llround(periods * fs / static_cast<double>(f0)));
}

/// Fresh core at fs, setExcitation(ex) then the renderAnyCore sequence; returns L (L == R asserted).
std::vector<float> renderOut(double fs, const Controls& c, const Excitation& ex, float f0, double seconds) {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(fs);
    PT::Render r = PT::renderExcited(*core, c, ex, f0, seconds);
    PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
    return std::move(r.L);
}

/// The twin mix-0 reference (Descriptor definitions); returns L (L == R asserted).
std::vector<float> renderRef(double fs, const Controls& c, const Excitation& ex, float f0, double seconds) {
    PT::Render r = PT::renderMixZeroTwin(fs, c, ex, f0, seconds);
    PT::requireLREqual(r.L.data(), r.R.data(), r.L.size());
    return std::move(r.L);
}

/// Rendered descriptors of the steady frame (spec "Descriptor definitions").
struct Spectral {
    double thdLin = 0.0;     ///< Σₙ≥₂ wₙ / w₁
    double thdDb = 0.0;      ///< THD (dB)
    int count = 0;           ///< H: n >= 2, n·f0 < fs/2, wₙ >= w₁·10⁻⁶
    double evenDb = 0.0;     ///< E_even = Σ wₙ (n even) / w₁ (dB)
    double oddEvenDb = 0.0;  ///< Σ wₙ (odd n >= 3) / Σ wₙ (n even) (dB), Phase 1's ratio
};

Spectral describeSteady(const std::vector<float>& x, double fs, float f0) {
    const std::size_t start = settleSamples(fs);
    REQUIRE(x.size() >= start + kFrame);
    const double f = static_cast<double>(f0);
    int numHarmonics = 0;
    while (static_cast<double>(numHarmonics + 1) * f < 0.5 * fs)
        ++numHarmonics;
    const std::vector<double> w = PT::renderedHarmonicPowers(x.data() + start, fs, f, numHarmonics);
    REQUIRE(!w.empty());
    const double w1 = w[0];
    REQUIRE(w1 > 0.0);

    double rest = 0.0;
    double even = 0.0;
    double oddHigh = 0.0;
    int count = 0;
    for (std::size_t i = 1; i < w.size(); ++i) {
        const std::size_t n = i + 1;
        rest += w[i];
        if (n % 2 == 0)
            even += w[i];
        else
            oddHigh += w[i];
        if (w[i] >= w1 * 1.0e-6)
            ++count;
    }
    Spectral s;
    s.thdLin = rest / w1;
    s.thdDb = PT::safeDb10(s.thdLin);
    s.count = count;
    s.evenDb = PT::safeDb10(even / w1);
    s.oddEvenDb = PT::safeDb10(oddHigh) - PT::safeDb10(even);
    return s;
}

/// T₁ over the 16-period window starting at t_settle.
std::complex<double> steadyT1(const std::vector<float>& out, const std::vector<float>& ref, double fs, float f0) {
    const std::size_t start = settleSamples(fs);
    const std::size_t L = phasorLength(fs, f0);
    REQUIRE(out.size() >= start + L);
    REQUIRE(ref.size() >= start + L);
    return PT::t1Transfer(out.data() + start, ref.data() + start, L, fs, static_cast<double>(f0));
}

double magnitudeDb(std::complex<double> z) { return 20.0 * std::log10(std::max(std::abs(z), 1e-300)); }
double angleDeg(std::complex<double> z) { return std::arg(z) * kRadToDeg; }

void requireFinite(double x) { REQUIRE(Krate::DSP::detail::isFinite(x)); }

// =============================================================================
// SC-001 drive and mix sweeps
// =============================================================================

/// SC-001 drive sweep: mix 1, asymmetry 0, kSineAnchor with recipe edge ∈ {0, 1}.
void checkDriveMonotonic(std::span<const double> rates, std::span<const float> notes) {
    double minSpan = 1.0e300;
    for (const double fs : rates) {
        for (const float f0 : notes) {
            for (const float edge : {0.0f, 1.0f}) {
                const Controls c = withEdge(SpectralShapeRecipe::kSineAnchor, edge);
                std::vector<Spectral> s;
                for (const float d : sweep33(0.0f, 1.0f)) {
                    const Excitation ex{.mix = 1.0f, .drive = d, .asymmetry = 0.0f};
                    s.push_back(describeSteady(renderOut(fs, c, ex, f0, steadyFrameSeconds(fs)), fs, f0));
                }
                for (std::size_t i = 1; i < s.size(); ++i) {
                    INFO("SC-001 drive: fs " << fs << " f0 " << f0 << " edge " << edge << " point " << i
                                             << ": THD " << s[i - 1].thdDb << " -> " << s[i].thdDb << " dB, H "
                                             << s[i - 1].count << " -> " << s[i].count);
                    CHECK(s[i].thdLin > s[i - 1].thdLin);
                    CHECK(s[i].count >= s[i - 1].count);
                }
                const double span = s.back().thdDb - s.front().thdDb;
                INFO("SC-001 drive: fs " << fs << " f0 " << f0 << " edge " << edge << ": THD(drive 0) "
                                         << s.front().thdDb << " dB, THD(drive 1) " << s.back().thdDb
                                         << " dB, span " << span << " dB");
                CHECK(span >= 20.0);
                minSpan = std::min(minSpan, span);
            }
        }
    }
    WARN("SC-001 drive sweep: smallest THD(drive 1) - THD(drive 0) " << minSpan << " dB (bar 20; plan 44.4 / 31.4)");
}

/// SC-001 mix sweep: drive 1, asymmetry 0, kSineAnchor.
void checkMixMonotonic(std::span<const double> rates, std::span<const float> notes) {
    double minSpan = 1.0e300;
    for (const double fs : rates) {
        for (const float f0 : notes) {
            const Controls c = SpectralShapeRecipe::kSineAnchor;
            std::vector<Spectral> s;
            for (const float m : sweep33(0.0f, 1.0f)) {
                const Excitation ex{.mix = m, .drive = 1.0f, .asymmetry = 0.0f};
                s.push_back(describeSteady(renderOut(fs, c, ex, f0, steadyFrameSeconds(fs)), fs, f0));
            }
            for (std::size_t i = 1; i < s.size(); ++i) {
                INFO("SC-001 mix: fs " << fs << " f0 " << f0 << " point " << i << ": THD " << s[i - 1].thdDb
                                       << " -> " << s[i].thdDb << " dB");
                CHECK(s[i].thdLin > s[i - 1].thdLin);   // includes THD(1/32) > THD(0)
            }
            const double span = s.back().thdDb - s.front().thdDb;
            INFO("SC-001 mix: fs " << fs << " f0 " << f0 << ": THD(mix 0) " << s.front().thdDb << " dB, THD(mix 1) "
                                   << s.back().thdDb << " dB, span " << span << " dB");
            CHECK(span >= 20.0);
            minSpan = std::min(minSpan, span);
        }
    }
    WARN("SC-001 mix sweep: smallest THD(mix 1) - THD(mix 0) " << minSpan << " dB (bar 20)");
}

// =============================================================================
// SC-002(b) Edge drives the excitation (rendered)
// =============================================================================

/// drive 0.5, mix 1, asymmetry 0, kSineAnchor with edge swept: G = THD_out ⊖ THD_ref (linear power
/// difference, dB) strictly increasing, G(1) − G(0) >= 6 dB.
void checkEdgeDrives(std::span<const double> rates, std::span<const float> notes) {
    double minRise = 1.0e300;
    for (const double fs : rates) {
        for (const float f0 : notes) {
            const Excitation ex{.mix = 1.0f, .drive = 0.5f, .asymmetry = 0.0f};
            std::vector<double> g;
            for (const float e : sweep33(0.0f, 1.0f)) {
                const Controls c = withEdge(SpectralShapeRecipe::kSineAnchor, e);
                const Spectral so = describeSteady(renderOut(fs, c, ex, f0, steadyFrameSeconds(fs)), fs, f0);
                const Spectral sr = describeSteady(renderRef(fs, c, ex, f0, steadyFrameSeconds(fs)), fs, f0);
                g.push_back(PT::safeDb10(so.thdLin - sr.thdLin));
            }
            for (std::size_t i = 1; i < g.size(); ++i) {
                INFO("SC-002(b): fs " << fs << " f0 " << f0 << " edge point " << i << ": G " << g[i - 1] << " -> "
                                      << g[i] << " dB");
                CHECK(g[i] > g[i - 1]);
            }
            const double rise = g.back() - g.front();
            INFO("SC-002(b): fs " << fs << " f0 " << f0 << ": G(edge 0) " << g.front() << " dB, G(edge 1) "
                                  << g.back() << " dB, rise " << rise << " dB");
            CHECK(rise >= 6.0);
            minRise = std::min(minRise, rise);
        }
    }
    WARN("SC-002(b): smallest G(edge 1) - G(edge 0) " << minRise << " dB (bar 6; plan 20.7)");
}

// =============================================================================
// SC-003(a) even harmonics monotonic with asymmetry
// =============================================================================

/// drive 0.5, mix 1, kSineAnchor with edge 0.5: E_gen against the asymmetry-0 render strictly
/// increasing over the 32 non-zero points, E_gen(1) >= −30 dB re h1; symmetry clause
/// E_even(asymmetry 0) <= E_even(mix-0 reference) + 3 dB.
void checkEvenHarmonics(std::span<const double> rates, std::span<const float> notes) {
    double minAtOne = 1.0e300;
    double worstSymmetry = -1.0e300;
    for (const double fs : rates) {
        for (const float f0 : notes) {
            const Controls c = withEdge(SpectralShapeRecipe::kSineAnchor, 0.5f);
            const Excitation ex0{.mix = 1.0f, .drive = 0.5f, .asymmetry = 0.0f};
            const std::vector<float> a0 = renderOut(fs, c, ex0, f0, steadyFrameSeconds(fs));
            const std::vector<float> ref = renderRef(fs, c, ex0, f0, steadyFrameSeconds(fs));

            const double evenOut = describeSteady(a0, fs, f0).evenDb;
            const double evenRef = describeSteady(ref, fs, f0).evenDb;
            {
                INFO("SC-003(a) symmetry: fs " << fs << " f0 " << f0 << ": E_even(asymmetry 0) " << evenOut
                                               << " dB, E_even(mix-0 ref) " << evenRef << " dB");
                CHECK(evenOut <= evenRef + 3.0);
            }
            worstSymmetry = std::max(worstSymmetry, evenOut - evenRef);

            const std::size_t start = settleSamples(fs);
            const std::size_t L = phasorLength(fs, f0);
            REQUIRE(a0.size() >= start + L);
            const std::vector<float> xs = sweep33(0.0f, 1.0f);
            std::vector<double> eGen;
            for (std::size_t i = 1; i < xs.size(); ++i) {
                const Excitation ex{.mix = 1.0f, .drive = 0.5f, .asymmetry = xs[i]};
                const std::vector<float> out = renderOut(fs, c, ex, f0, secondsFor(L, fs));
                REQUIRE(out.size() >= start + L);
                eGen.push_back(PT::generatedEvenEnergyDb(out.data() + start, a0.data() + start, L, fs,
                                                         static_cast<double>(f0)));
            }
            for (std::size_t i = 1; i < eGen.size(); ++i) {
                INFO("SC-003(a): fs " << fs << " f0 " << f0 << " asymmetry " << xs[i + 1] << ": E_gen "
                                      << eGen[i - 1] << " -> " << eGen[i] << " dB");
                CHECK(eGen[i] > eGen[i - 1]);
            }
            INFO("SC-003(a): fs " << fs << " f0 " << f0 << ": E_gen(asymmetry 1) " << eGen.back() << " dB re h1");
            CHECK(eGen.back() >= -30.0);
            minAtOne = std::min(minAtOne, eGen.back());
        }
    }
    WARN("SC-003(a): lowest E_gen(asymmetry 1) " << minAtOne << " dB re h1 (bar -30; plan -17.6); worst symmetry "
                                                 << "E_even(out) - E_even(ref) " << worstSymmetry << " dB (bar 3)");
}

// =============================================================================
// SC-012(a)-(c) the fundamental survives the excitation (plan S1 C-2, C-6)
// =============================================================================

/// (a) drive 0, mix 1, asymmetry 0; kSineAnchor, mid grid, kSawAnchor (C-6); E0–E2 × 44.1/48/96 kHz:
/// ||T₁| (dB)| <= 0.1 dB and |∠T₁| <= 2°.
void checkFundamentalA() {
    double worstDb = 0.0;
    double worstDeg = 0.0;
    for (const double fs : kRates) {
        for (const float f0 : kLowNotes) {
            for (const NamedInput& in : sineMidSaw()) {
                const Excitation ex{.mix = 1.0f, .drive = 0.0f, .asymmetry = 0.0f};
                const double secs = secondsFor(phasorLength(fs, f0), fs);
                const std::vector<float> out = renderOut(fs, in.controls, ex, f0, secs);
                const std::vector<float> ref = renderRef(fs, in.controls, ex, f0, secs);
                const std::complex<double> t1 = steadyT1(out, ref, fs, f0);
                const double db = magnitudeDb(t1);
                const double deg = angleDeg(t1);
                INFO("SC-012(a): fs " << fs << " f0 " << f0 << " " << in.name << ": |T1| " << db << " dB, arg T1 "
                                      << deg << " deg");
                CHECK(std::abs(db) <= 0.1);
                CHECK(std::abs(deg) <= 2.0);
                worstDb = std::max(worstDb, std::abs(db));
                worstDeg = std::max(worstDeg, std::abs(deg));
            }
        }
    }
    WARN("SC-012(a): worst ||T1|| " << worstDb << " dB (bar 0.1; plan 0.06), worst |arg T1| " << worstDeg
                                    << " deg (bar 2; plan 0.13)");
}

/// (b) drive × asymmetry × mix ∈ {0, 0.5, 1}³ on kSineAnchor and kSawAnchor: |∠T₁| <= 2° at
/// asymmetry 0 on both inputs and at every setting on kSineAnchor; on kSawAnchor at asymmetry > 0,
/// |∠T₁| <= 20° (plan S1 C-2).
/// (c) mix 33-point sweep at drive 1, asymmetry 1: |T₁| never more than 0.5 dB below
/// min(|T₁(mix 0)|, |T₁(mix 1)|).
///
/// The mix-0 reference is rendered once per (input, f0, fs): the twin's ExcitationControls differ
/// only in drive and asymmetry across the grid, and a mix-0 core's output does not depend on them
/// (SC-004(a): bit-identical to the Phase 1 core, gated by ProfundumCore_ExcitationZeroMixBitIdentical).
void checkFundamentalBC(std::span<const double> rates, std::span<const float> notes) {
    constexpr std::array<float, 3> kLevels{0.0f, 0.5f, 1.0f};
    double worstSym = 0.0;      // asymmetry 0 (both inputs) and every kSineAnchor setting
    double worstSawAsym = 0.0;  // kSawAnchor at asymmetry > 0
    double worstDip = -1.0e300; // (c): largest dB below the lower endpoint
    for (const double fs : rates) {
        for (const float f0 : notes) {
            const double secs = secondsFor(phasorLength(fs, f0), fs);
            for (const NamedInput& in : sineSaw()) {
                const bool isSine = in.controls.depth == SpectralShapeRecipe::kSineAnchor.depth;
                const Excitation refEx{.mix = 1.0f, .drive = 1.0f, .asymmetry = 1.0f};
                const std::vector<float> ref = renderRef(fs, in.controls, refEx, f0, secs);

                // (b)
                for (const float drive : kLevels) {
                    for (const float asym : kLevels) {
                        for (const float mix : kLevels) {
                            const Excitation ex{.mix = mix, .drive = drive, .asymmetry = asym};
                            const std::vector<float> out = renderOut(fs, in.controls, ex, f0, secs);
                            const double deg = angleDeg(steadyT1(out, ref, fs, f0));
                            const bool tight = isSine || asym == 0.0f;   // plan S1 C-2
                            const double bar = tight ? 2.0 : 20.0;
                            INFO("SC-012(b): fs " << fs << " f0 " << f0 << " " << in.name << " drive " << drive
                                                  << " asymmetry " << asym << " mix " << mix << ": arg T1 " << deg
                                                  << " deg (bar " << bar << ")");
                            CHECK(std::abs(deg) <= bar);
                            if (tight)
                                worstSym = std::max(worstSym, std::abs(deg));
                            else
                                worstSawAsym = std::max(worstSawAsym, std::abs(deg));
                        }
                    }
                }

                // (c)
                std::vector<double> mags;
                for (const float m : sweep33(0.0f, 1.0f)) {
                    const Excitation ex{.mix = m, .drive = 1.0f, .asymmetry = 1.0f};
                    const std::vector<float> out = renderOut(fs, in.controls, ex, f0, secs);
                    mags.push_back(magnitudeDb(steadyT1(out, ref, fs, f0)));
                }
                const double lower = std::min(mags.front(), mags.back());
                for (std::size_t i = 0; i < mags.size(); ++i) {
                    INFO("SC-012(c): fs " << fs << " f0 " << f0 << " " << in.name << " mix point " << i << ": |T1| "
                                          << mags[i] << " dB, endpoints " << mags.front() << " / " << mags.back()
                                          << " dB");
                    CHECK(mags[i] >= lower - 0.5);
                    worstDip = std::max(worstDip, lower - mags[i]);
                }
            }
        }
    }
    WARN("SC-012(b): worst |arg T1| " << worstSym << " deg where the bar is 2; kSawAnchor at asymmetry > 0 "
                                      << worstSawAsym << " deg (bar 20; plan 15.04). SC-012(c): deepest dip below "
                                      << "the lower endpoint " << worstDip << " dB (bar 0.5)");
}

// =============================================================================
// SC-020 excitation level law
// =============================================================================

/// mix 1, drive 33-point sweep × asymmetry {0, 0.5, 1}; kSineAnchor, mid grid, kSawAnchor: output
/// RMS within ±1.5 dB of the reference RMS. The mix-0 reference is rendered once per (input, f0, fs)
/// (see checkFundamentalBC: a mix-0 core's output does not depend on drive or asymmetry, SC-004(a)).
void checkLevelLaw(std::span<const double> rates, std::span<const float> notes) {
    constexpr std::array<float, 3> kAsymmetries{0.0f, 0.5f, 1.0f};
    double worst = 0.0;
    double worstSigned = 0.0;
    for (const double fs : rates) {
        for (const float f0 : notes) {
            const std::size_t start = settleSamples(fs);
            const double secs = secondsFor(periodWindowLength(fs, f0), fs);
            const double f = static_cast<double>(f0);
            for (const NamedInput& in : sineMidSaw()) {
                const Excitation refEx{.mix = 1.0f, .drive = 0.0f, .asymmetry = 0.0f};
                const std::vector<float> ref = renderRef(fs, in.controls, refEx, f0, secs);
                REQUIRE(ref.size() > start);
                const double refDb = PT::rmsDbOverPeriods(ref.data() + start, ref.size() - start, fs, f, 1.0);
                for (const float asym : kAsymmetries) {
                    for (const float d : sweep33(0.0f, 1.0f)) {
                        const Excitation ex{.mix = 1.0f, .drive = d, .asymmetry = asym};
                        const std::vector<float> out = renderOut(fs, in.controls, ex, f0, secs);
                        REQUIRE(out.size() == ref.size());
                        const double outDb =
                            PT::rmsDbOverPeriods(out.data() + start, out.size() - start, fs, f, 1.0);
                        const double diff = outDb - refDb;
                        INFO("SC-020: fs " << fs << " f0 " << f0 << " " << in.name << " drive " << d << " asymmetry "
                                           << asym << ": level " << diff << " dB re reference");
                        CHECK(std::abs(diff) <= 1.5);
                        if (std::abs(diff) > worst) {
                            worst = std::abs(diff);
                            worstSigned = diff;
                        }
                    }
                }
            }
        }
    }
    WARN("SC-020: worst level " << worstSigned << " dB re reference (bar +-1.5; plan -0.95)");
}

}  // namespace

// =============================================================================
// SC-001: harmonic count and THD monotonic with drive and mix (audibility gate)
// =============================================================================

TEST_CASE("ProfundumCore_ExcitationDriveMonotonic", "[systems][profundum][edge-fold][long]") {
    checkDriveMonotonic(kRates, kGateNotes);
}

TEST_CASE("ProfundumCore_ExcitationDriveMonotonicSmoke", "[systems][profundum][edge-fold]") {
    checkDriveMonotonic(kSmokeRates, kSmokeNotes);
}

TEST_CASE("ProfundumCore_ExcitationMixMonotonic", "[systems][profundum][edge-fold][long]") {
    checkMixMonotonic(kRates, kGateNotes);
}

TEST_CASE("ProfundumCore_ExcitationMixMonotonicSmoke", "[systems][profundum][edge-fold]") {
    checkMixMonotonic(kSmokeRates, kSmokeNotes);
}

// =============================================================================
// SC-002(b)(c): Edge drives the excitation; Edge 0 still excites; mix is a plain blend
// =============================================================================

TEST_CASE("ProfundumCore_EdgeDrivesExcitationRendered", "[systems][profundum][edge-fold][long]") {
    checkEdgeDrives(kRates, kLeverNotes);
}

TEST_CASE("ProfundumCore_EdgeDrivesExcitationRenderedSmoke", "[systems][profundum][edge-fold]") {
    checkEdgeDrives(kSmokeRates, kSmokeNotes);
}

// SC-002(c) first clause: recipe edge 0 (kSineAnchor), mix 1, drive 1, asymmetry 0:
// THD(out) − THD(mix-0 reference) >= 20 dB at E1 and C2.
TEST_CASE("ProfundumCore_EdgeZeroStillExcites", "[systems][profundum][edge-fold]") {
    double minLift = 1.0e300;
    for (const double fs : kRates) {
        for (const float f0 : kLeverNotes) {
            const Controls c = withEdge(SpectralShapeRecipe::kSineAnchor, 0.0f);
            const Excitation ex{.mix = 1.0f, .drive = 1.0f, .asymmetry = 0.0f};
            const Spectral so = describeSteady(renderOut(fs, c, ex, f0, steadyFrameSeconds(fs)), fs, f0);
            const Spectral sr = describeSteady(renderRef(fs, c, ex, f0, steadyFrameSeconds(fs)), fs, f0);
            const double lift = so.thdDb - sr.thdDb;
            INFO("SC-002(c): fs " << fs << " f0 " << f0 << ": THD(out) " << so.thdDb << " dB, THD(ref) " << sr.thdDb
                                  << " dB, lift " << lift << " dB");
            CHECK(lift >= 20.0);
            minLift = std::min(minLift, lift);
        }
    }
    WARN("SC-002(c) Edge 0 still excites: smallest THD(out) - THD(ref) " << minLift << " dB (bar 20)");
}

// SC-002(c) blend clause, per the ratified plan S1 C-9: at Edge 0 and Edge 1 separately, drive 0.5,
// asymmetry 0, kSineAnchor, E1 and C2, 44.1/48/96 kHz, steady 16-period window: residual power of
// y(0.55) − ½(y(0.1) + y(1)) <= −60 dB re the output power y(0.55). The as-written residual
// y(0.5) − ½(y(0) + y(1)) is printed as SC-022(g) (recorded, not gated; plan −53.6 … −60.5 dB).
TEST_CASE("ProfundumCore_MixIndependentOfEdge", "[systems][profundum][edge-fold]") {
    double worstGated = -1.0e300;
    for (const double fs : kRates) {
        for (const float f0 : kLeverNotes) {
            for (const float edge : {0.0f, 1.0f}) {
                const Controls c = withEdge(SpectralShapeRecipe::kSineAnchor, edge);
                const std::size_t start = settleSamples(fs);
                const std::size_t L = phasorLength(fs, f0);
                const double secs = secondsFor(L, fs);
                const auto y = [&](float mix) {
                    const Excitation ex{.mix = mix, .drive = 0.5f, .asymmetry = 0.0f};
                    std::vector<float> v = renderOut(fs, c, ex, f0, secs);
                    REQUIRE(v.size() >= start + L);
                    return v;
                };
                // Residual power of a − ½(b + d) re the power of a, over the steady window (dB).
                const auto residualDb = [&](const std::vector<float>& a, const std::vector<float>& b,
                                            const std::vector<float>& d) {
                    double num = 0.0;
                    double den = 0.0;
                    for (std::size_t k = start; k < start + L; ++k) {
                        const double av = static_cast<double>(a[k]);
                        const double r = av - 0.5 * (static_cast<double>(b[k]) + static_cast<double>(d[k]));
                        num += r * r;
                        den += av * av;
                    }
                    REQUIRE(den > 0.0);
                    return PT::safeDb10(num / den);
                };

                const std::vector<float> y055 = y(0.55f);
                const std::vector<float> y010 = y(0.1f);
                const std::vector<float> y100 = y(1.0f);
                const double gated = residualDb(y055, y010, y100);
                {
                    INFO("SC-002(c) blend (C-9): fs " << fs << " f0 " << f0 << " edge " << edge
                                                      << ": residual y(0.55) - (y(0.1) + y(1))/2 " << gated
                                                      << " dB re output");
                    CHECK(gated <= -60.0);
                }
                worstGated = std::max(worstGated, gated);

                const std::vector<float> y050 = y(0.5f);
                const std::vector<float> y000 = y(0.0f);
                const double asWritten = residualDb(y050, y000, y100);
                requireFinite(asWritten);
                WARN("SC-022(g): fs " << fs << " f0 " << f0 << " edge " << edge
                                      << ": as-written residual y(0.5) - (y(0) + y(1))/2 " << asWritten
                                      << " dB re output (recorded; plan -53.6 ... -60.5)");
            }
        }
    }
    WARN("SC-002(c) blend (C-9): worst residual " << worstGated << " dB re output (bar -60; model <= -300.9)");
}

// =============================================================================
// SC-003: even harmonics with asymmetry; the asymmetric tail reached
// =============================================================================

TEST_CASE("ProfundumCore_ExcitationEvenHarmonicsMonotonic", "[systems][profundum][edge-fold][long]") {
    checkEvenHarmonics(kRates, kGateNotes);
}

TEST_CASE("ProfundumCore_ExcitationEvenHarmonicsMonotonicSmoke", "[systems][profundum][edge-fold]") {
    checkEvenHarmonics(kSmokeRates, kSmokeNotes);
}

// SC-003(b): kTriangleAnchor at max excitation (mix 1, drive 1, asymmetry 1): the odd/even ratio
// drops by >= 20 dB against the mix-0 reference and E_even >= −30 dB re h1. E0, E1, E2, C2.
TEST_CASE("ProfundumCore_ExcitationTriangleTail", "[systems][profundum][edge-fold]") {
    double minDrop = 1.0e300;
    double minEven = 1.0e300;
    for (const double fs : kRates) {
        for (const float f0 : kGateNotes) {
            const Controls c = SpectralShapeRecipe::kTriangleAnchor;
            const Excitation ex{.mix = 1.0f, .drive = 1.0f, .asymmetry = 1.0f};
            const Spectral so = describeSteady(renderOut(fs, c, ex, f0, steadyFrameSeconds(fs)), fs, f0);
            const Spectral sr = describeSteady(renderRef(fs, c, ex, f0, steadyFrameSeconds(fs)), fs, f0);
            const double drop = sr.oddEvenDb - so.oddEvenDb;
            INFO("SC-003(b): fs " << fs << " f0 " << f0 << ": odd/even " << sr.oddEvenDb << " -> " << so.oddEvenDb
                                  << " dB (drop " << drop << "), E_even(out) " << so.evenDb << " dB re h1");
            CHECK(drop >= 20.0);
            CHECK(so.evenDb >= -30.0);
            minDrop = std::min(minDrop, drop);
            minEven = std::min(minEven, so.evenDb);
        }
    }
    WARN("SC-003(b): smallest odd/even drop " << minDrop << " dB (bar 20); lowest E_even " << minEven
                                              << " dB re h1 (bar -30; plan -19.2)");
}

// =============================================================================
// SC-011 (excitation): DC <= −80 dBFS
// =============================================================================

// (drive, asymmetry) ∈ {0.5, 1}², mix 1, kSineAnchor and kSawAnchor, E0, E1, E2: |mean| over a
// >= 1 s integer-period window from t_settle <= −80 dBFS per channel (L == R asserted per render).
TEST_CASE("ProfundumCore_ExcitationDcFree", "[systems][profundum][edge-fold]") {
    double worstDc = -1.0e300;
    for (const double fs : kRates) {
        for (const float f0 : kLowNotes) {
            const std::size_t start = settleSamples(fs);
            const std::size_t len = periodWindowLength(fs, f0);
            REQUIRE(static_cast<double>(len) / fs >= 1.0);
            const double secs = secondsFor(len, fs);
            for (const NamedInput& in : sineSaw()) {
                for (const float drive : {0.5f, 1.0f}) {
                    for (const float asym : {0.5f, 1.0f}) {
                        const Excitation ex{.mix = 1.0f, .drive = drive, .asymmetry = asym};
                        const std::vector<float> out = renderOut(fs, in.controls, ex, f0, secs);
                        REQUIRE(out.size() >= start + len);
                        double sum = 0.0;
                        for (std::size_t k = start; k < start + len; ++k)
                            sum += static_cast<double>(out[k]);
                        const double mean = sum / static_cast<double>(len);
                        const double dcDb = 20.0 * std::log10(std::max(std::abs(mean), 1e-300));
                        INFO("SC-011: fs " << fs << " f0 " << f0 << " " << in.name << " drive " << drive
                                           << " asymmetry " << asym << ": DC " << dcDb << " dBFS");
                        CHECK(dcDb <= -80.0);
                        worstDc = std::max(worstDc, dcDb);
                    }
                }
            }
        }
    }
    WARN("SC-011 (excitation): worst DC " << worstDc << " dBFS (bar -80)");
}

// =============================================================================
// SC-012(a)-(c): the fundamental survives the excitation (plan S1 C-2, C-6)
// =============================================================================

TEST_CASE("ProfundumCore_ExcitationFundamentalPreserved", "[systems][profundum][edge-fold][long]") {
    checkFundamentalA();
    checkFundamentalBC(kRates, kLeverNotes);
}

TEST_CASE("ProfundumCore_ExcitationFundamentalPreservedSmoke", "[systems][profundum][edge-fold]") {
    checkFundamentalA();
    checkFundamentalBC(kSmokeRates, kLeverNotes);
}

// =============================================================================
// SC-020: excitation level law
// =============================================================================

TEST_CASE("ProfundumCore_ExcitationLevelLaw", "[systems][profundum][edge-fold][long]") {
    checkLevelLaw(kRates, kLeverNotes);
}

TEST_CASE("ProfundumCore_ExcitationLevelLawSmoke", "[systems][profundum][edge-fold]") {
    checkLevelLaw(kSmokeRates, kSmokeNotes);
}
