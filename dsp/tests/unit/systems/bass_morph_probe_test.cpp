// ==============================================================================
// Layer 3: System Tests - BassMorph probes (hidden)
//                          (Profundum Phase 8, specs/profundum-phase8-bass-morph)
// ==============================================================================
// tasks.md T008 (plan C-9, R-1, R-2; deviations 6, 7). Hidden cases only ([.]): the gate filter ends in ~[.],
// so neither case runs inside a gate. The main loop runs both through evidence/close/probe.sh at the T008
// checkpoint, before any rendered gate is trusted, and rules on the verdict lines:
//   BassMorph_SumTapCoherenceProbe   plan C-9: c, theta of the core's h1 at the character tap against the Anchor,
//                                    the predicted and measured SC-015 Anchor-clause and SC-008(d) margins.
//                                    Last line "C9 VERDICT GREEN" or "C9 VERDICT RED <cells>". No REQUIRE on a
//                                    margin (the main loop rules); REQUIRE only finite taps.
//   BassMorph_CpuEarlyProbe          plan R-2: early advanceInterval() timing on the SC-018 workload, against
//                                    333.3 ns per interval (0.05 % of one core at 48 kHz, plan C-15 D-10).
//                                    Last line "R2 EARLY <GREEN|RED> <ns> vs 333.3". Not the SC-018 gate.
// FTZ / DAZ: set by the render probe (it reads the core and the character stage); the engine-only CPU probe
// never sets it (tasks.md rule 9, plan R-7).
// ==============================================================================

#include "bass_morph_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <enable_ftz_daz.h>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace Bmt = Krate::DSP::BassMorphTest;
using Krate::DSP::BassMorphEngine;
using Krate::DSP::MorphState;
using BmMode = BassMorphEngine::AnchorMorphMode;

constexpr double kBmpFs = 48000.0;
constexpr double kBmpPi = 3.14159265358979323846264338327950288;

/// tasks.md rule 11: evidence goes under specs/profundum-phase8-bass-morph/evidence/.
std::string bmpEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase8-bass-morph/evidence/";
}

void bmpWriteEvidence(const std::string& name, const std::string& text) {
    const std::string dir = bmpEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream out(dir + name);
    out << text;
}

const char* bmpModeName(BmMode m) {
    switch (m) {
        case BmMode::Lock: return "Lock";
        case BmMode::Blend: return "Blend";
        case BmMode::Punch: return "Punch";
    }
    return "?";
}

bool bmpAllFinite(const std::vector<float>& x) {
    return std::all_of(x.begin(), x.end(), [](float v) { return Krate::DSP::detail::isFinite(v); });
}

/// The complex h1 of the mid (L + R) / 2 over the read window trimmed to whole periods: a single-bin DFT at f0,
/// phase referenced to absolute sample 0 (so two taps of one render share the reference).
std::complex<double> bmpH1(const std::vector<float>& L, const std::vector<float>& R, double f0, double fs) {
    REQUIRE(L.size() == R.size());
    const Bmt::ReadWindow w = Bmt::readWindow(L.size(), fs, f0);
    REQUIRE(w.wholePeriods > 0);
    const double omega = 2.0 * kBmpPi * f0 / fs;
    double re = 0.0;
    double im = 0.0;
    for (std::size_t n = w.start; n < w.start + w.wholePeriods; ++n) {
        const double x = 0.5 * (static_cast<double>(L[n]) + static_cast<double>(R[n]));
        const double ph = omega * static_cast<double>(n);
        re += x * std::cos(ph);
        im -= x * std::sin(ph);
    }
    const double scale = 2.0 / static_cast<double>(w.wholePeriods);
    return {re * scale, im * scale};
}

/// One (note, p, mode) cell of the coherence probe.
struct BmpCell {
    double f0 = 0.0;
    const char* note = "";
    float p = 0.0f;
    BmMode mode = BmMode::Blend;
    double level = 0.0;        ///< L: anchorLevelHanded of the last interval
    double hc = 0.0;           ///< |H_c| (character tap h1)
    double ha = 0.0;           ///< |H_a| (Anchor tap h1)
    std::complex<double> cz;   ///< c * e^{i theta} = (H_c / H_a) * L
    double c = 0.0;
    double thetaDeg = 0.0;
    double eSubSum = 0.0;      ///< E_sub(sum tap)
    double eSubAnchor = 0.0;   ///< E_sub(Anchor tap); F_lock(p) on the Lock cells
    double dcSumDb = 0.0;      ///< 20 log10 |mean of the sum-tap mid over the read window| (DC in the sub band)
    double sumH1Db = 0.0;      ///< 20 log10 (|H_sum| / sqrt 2): the f0-only RMS of the sum tap, for comparison with E_sub
};

/// DC of the mid over the whole-period read window, in dB (floored at -200).
double bmpDcDb(const std::vector<float>& L, const std::vector<float>& R, double f0, double fs) {
    const std::vector<float> mid = Bmt::midOf(L, R);
    const Bmt::ReadWindow w = Bmt::readWindow(mid.size(), fs, f0);
    double acc = 0.0;
    for (std::size_t i = w.start; i < w.start + w.wholePeriods; ++i) {
        acc += static_cast<double>(mid[i]);
    }
    const double mean = std::fabs(acc / static_cast<double>(w.wholePeriods));
    return 20.0 * std::log10(std::max(mean, 1e-10));
}

/// The Anchor level a mode resolves at p for the pair's levels (FR-051: Blend c = p, Punch c = p^3, Lock c = 0).
double bmpModeLevel(BmMode m, double p, double la, double lb) {
    const double c = static_cast<double>(BassMorphEngine::curve(m, static_cast<float>(p)));
    return la + (lb - la) * c;
}

double bmpDb20(double x) {
    return 20.0 * std::log10(std::max(x, 1e-30));
}

/// tasks.md T008 / spec SC-018: every leaf differs (enums and bools by value, floats by bits) and no governing
/// amount is 0 on either side.
bool bmpEveryLeafDiffers(const MorphState& a, const MorphState& b) {
    return Bmt::differingLeaves(a, b).size() == Krate::DSP::kMorphLeafFieldCount;
}

bool bmpNoGoverningZero(const MorphState& s) {
    return s.characterAmount != 0.0f && s.field.spread != 0.0f && s.field.motionDepth != 0.0f
           && s.motion.amount != 0.0f && s.anchor.level != 0.0f;
}

}  // namespace

// -----------------------------------------------------------------------------
// plan C-9 / R-1 (SC-008(d), SC-015): the sum-tap coherence probe
// -----------------------------------------------------------------------------

TEST_CASE("BassMorph_SumTapCoherenceProbe", "[systems][profundum][bass-morph][.]") {
    enableFTZDAZ();
    const auto pair = Bmt::showcasePair();
    const double la = static_cast<double>(pair.first.anchor.level);    // 1.0 (Lock holds A's level)
    const double lb = static_cast<double>(pair.second.anchor.level);   // harvested B (0.5 after B7-2)

    struct BmpNote {
        const char* name;
        double f0;
    };
    const std::array<BmpNote, 2> notes{{{"E1", static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(28))},
                                        {"C3", static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(48))}}};
    const std::array<float, 3> positions{0.0f, 0.5f, 1.0f};
    const std::array<BmMode, 3> modes{BmMode::Lock, BmMode::Blend, BmMode::Punch};

    // cells[note][p][mode]
    std::array<std::array<std::array<BmpCell, 3>, 3>, 2> cells{};

    std::ostringstream os;
    os << std::setprecision(6);
    os << "C-9 sum-tap coherence probe (tasks.md T008; plan C-9, R-1). showcasePair(), fs " << kBmpFs
       << " Hz, steady renders (ampEnvelope = false), host block 512.\n";
    os << "A anchor.level " << la << ", B anchor.level " << lb << "\n";
    os << "c = |H_c| / (|H_a| / L), theta = arg H_c - arg H_a (deg); H = single-bin DFT at f0 of the mid over the "
          "read window (whole periods).\n\n";
    os << "note\tp\tmode\tL\t|H_c|\t|H_a|\tc\ttheta_deg\tE_sub_sum_dB\tE_sub_anchor_dB\tsum_f0_rms_dB\tsum_dc_dB\n";

    for (std::size_t ni = 0; ni < notes.size(); ++ni) {
        for (std::size_t pi = 0; pi < positions.size(); ++pi) {
            for (std::size_t mi = 0; mi < modes.size(); ++mi) {
                Bmt::MorphChainConfig cfg;
                cfg.a = pair.first;
                cfg.b = pair.second;
                cfg.mode = modes[mi];
                cfg.initialPosition = positions[pi];   // snapped by prepare's reset() (steady render)
                cfg.f0 = notes[ni].f0;
                cfg.fs = kBmpFs;
                cfg.ampEnvelope = false;
                const Bmt::MorphChainTaps t = Bmt::renderMorphChain(cfg);

                INFO(notes[ni].name << " p " << positions[pi] << " " << bmpModeName(modes[mi]));
                REQUIRE(bmpAllFinite(t.characterL));
                REQUIRE(bmpAllFinite(t.characterR));
                REQUIRE(bmpAllFinite(t.anchorL));
                REQUIRE(bmpAllFinite(t.anchorR));
                REQUIRE(bmpAllFinite(t.sumL));
                REQUIRE(bmpAllFinite(t.sumR));
                REQUIRE(!t.anchorLevelHanded.empty());

                BmpCell& cell = cells[ni][pi][mi];
                cell.f0 = notes[ni].f0;
                cell.note = notes[ni].name;
                cell.p = positions[pi];
                cell.mode = modes[mi];
                cell.level = static_cast<double>(t.anchorLevelHanded.back());
                REQUIRE(cell.level > 0.0);
                const std::complex<double> hc = bmpH1(t.characterL, t.characterR, cell.f0, kBmpFs);
                const std::complex<double> ha = bmpH1(t.anchorL, t.anchorR, cell.f0, kBmpFs);
                cell.hc = std::abs(hc);
                cell.ha = std::abs(ha);
                REQUIRE(cell.ha > 0.0);
                cell.cz = (hc / ha) * cell.level;
                cell.c = std::abs(cell.cz);
                cell.thetaDeg = std::arg(hc * std::conj(ha)) * 180.0 / kBmpPi;
                cell.eSubSum = Bmt::eSubDb(t.sumL, t.sumR, cell.f0, kBmpFs);
                cell.eSubAnchor = Bmt::eSubDb(t.anchorL, t.anchorR, cell.f0, kBmpFs);
                cell.dcSumDb = bmpDcDb(t.sumL, t.sumR, cell.f0, kBmpFs);
                cell.sumH1Db = 20.0 * std::log10(std::max(std::abs(bmpH1(t.sumL, t.sumR, cell.f0, kBmpFs)) / std::sqrt(2.0), 1e-10));

                os << cell.note << '\t' << cell.p << '\t' << bmpModeName(cell.mode) << '\t' << cell.level << '\t'
                   << cell.hc << '\t' << cell.ha << '\t' << cell.c << '\t' << cell.thetaDeg << '\t' << cell.eSubSum
                   << '\t' << cell.eSubAnchor << '\t' << cell.sumH1Db << '\t' << cell.dcSumDb << '\n';
            }
        }
    }

    // Margins: every one must be >= 0 for GREEN.
    std::vector<std::string> red;
    // gated = part of the C9 verdict (measured margins). The in-phase predictions are recorded only: the measurement is
    // the truth, and the predictions assume a coherent core fundamental, which the chain does not deliver.
    auto margin = [&](const std::string& label, double m, bool gated = true) {
        const char* tag = "";
        if (!(m >= 0.0))
            tag = gated ? "\tRED" : "\tred(recorded)";
        os << label << '\t' << m << tag << '\n';
        if (gated && !(m >= 0.0))
            red.push_back(label);
    };
    constexpr std::size_t kLock = 0, kBlend = 1, kPunch = 2;
    constexpr std::size_t kP1 = 2;

    os << "\nPredicted margins (plan C-9; dB; in-phase arithmetic, recorded, not gated):\n";
    for (std::size_t ni = 0; ni < notes.size(); ++ni) {
        // SC-015 Anchor clause at p = 1, c and theta from the Blend render.
        const std::complex<double> czB = cells[ni][kP1][kBlend].cz;
        margin(std::string(notes[ni].name) + "/p=1/pred-sc015",
               bmpDb20(std::abs(la + czB)) - bmpDb20(std::abs(lb + czB)) - 2.0, false);
        // SC-008(d) at p in {0.5, 1} for Blend and Punch, c and theta from the cell's own render.
        for (std::size_t pi = 1; pi < positions.size(); ++pi) {
            for (const std::size_t mi : {kBlend, kPunch}) {
                const BmpCell& cell = cells[ni][pi][mi];
                const double lm = bmpModeLevel(cell.mode, static_cast<double>(cell.p), la, lb);
                std::ostringstream lab;
                lab << std::setprecision(6) << notes[ni].name << "/p=" << cell.p << '/' << bmpModeName(cell.mode)
                    << "/pred-sc008d";
                margin(lab.str(), bmpDb20(std::abs(lm + cell.cz)) - bmpDb20(lm) + 1.0, false);   // B7-1: own level
            }
        }
    }

    os << "\nMeasured margins (same renders; f0 reading; floor per B7-1 against the render's own Anchor tap; dB):\n";
    for (std::size_t ni = 0; ni < notes.size(); ++ni) {
        margin(std::string(notes[ni].name) + "/p=1/meas-sc015",
               cells[ni][kP1][kLock].eSubSum - cells[ni][kP1][kBlend].eSubSum - 2.0);
        for (std::size_t pi = 0; pi < positions.size(); ++pi) {
            for (std::size_t mi = 0; mi < modes.size(); ++mi) {
                // B7-1: Lock against F_lock(p) = its own Anchor tap; Blend / Punch against F_lock(p)·L_m(p)/L_A, which
                // is exactly that render's own Anchor tap (same a1, level scaled by the mode).
                std::ostringstream lab;
                lab << std::setprecision(6) << notes[ni].name << "/p=" << positions[pi] << '/'
                    << bmpModeName(modes[mi]) << "/meas-sc008d-floor";
                margin(lab.str(), cells[ni][pi][mi].eSubSum - cells[ni][pi][mi].eSubAnchor + 1.0);
            }
            std::ostringstream lab;
            lab << std::setprecision(6) << notes[ni].name << "/p=" << positions[pi] << "/meas-sc008d-punch-vs-blend";
            margin(lab.str(), cells[ni][pi][kPunch].eSubSum - cells[ni][pi][kBlend].eSubSum + 0.1);
        }
    }

    os << '\n';
    if (red.empty()) {
        os << "C9 VERDICT GREEN\n";
    } else {
        os << "C9 VERDICT RED";
        for (const std::string& r : red)
            os << ' ' << r;
        os << '\n';
    }
    std::cout << os.str();
    bmpWriteEvidence("coherence-probe.txt", os.str());
}

// -----------------------------------------------------------------------------
// plan R-2 (deviation 7): early advanceInterval() timing on the SC-018 workload
// -----------------------------------------------------------------------------

TEST_CASE("BassMorph_CpuEarlyProbe", "[systems][profundum][bass-morph][.]") {
    constexpr long kMaxDraws = 10'000'000;
    constexpr std::size_t kCalls = 200'000;
    constexpr int kReps = 3;
    constexpr double kBarNs = 333.3;                                         // plan C-15 D-10
    constexpr double kIntervalNs = 1e9 * 32.0 / 48000.0;                     // 666 667 ns at 48 kHz

    // Seed 0x8018, redrawn until every leaf differs and no governing amount is 0 (pEqual = 0: no leaf copied).
    std::mt19937 rng(0x8018u);
    MorphState a{};
    MorphState b{};
    long draws = 0;
    bool found = false;
    while (draws < kMaxDraws && !found) {
        const auto pr = Bmt::randomValidPair(rng, 0.0f);
        ++draws;
        if (bmpNoGoverningZero(pr.first) && bmpNoGoverningZero(pr.second) && bmpEveryLeafDiffers(pr.first, pr.second)) {
            a = pr.first;
            b = pr.second;
            found = true;
        }
    }
    INFO("draws " << draws);
    REQUIRE(found);

    BassMorphEngine e;
    REQUIRE(e.setEndpoint(BassMorphEngine::Endpoint::A, a));
    REQUIRE(e.setEndpoint(BassMorphEngine::Endpoint::B, b));
    e.setPositionRampMs(2000.0f);
    e.setPosition(0.0f);
    e.prepare(kBmpFs);
    e.setPosition(1.0f);

    // Re-target 0 <-> 1 on landing (tasks.md rule 17's landed condition), so every interval moves.
    auto step = [&e]() -> float {
        const BassMorphEngine::Resolved& r = e.advanceInterval();
        const float pos = e.position();
        if (std::bit_cast<std::uint32_t>(pos) == std::bit_cast<std::uint32_t>(e.targetPosition())
            && std::bit_cast<std::uint32_t>(e.anchorPosition())
                   == std::bit_cast<std::uint32_t>(BassMorphEngine::curve(e.anchorMorphMode(), pos))) {
            e.setPosition(e.targetPosition() >= 1.0f ? 0.0f : 1.0f);
        }
        return r.state.characterAmount + r.position;
    };

    // Untimed warm-up of the same length: counts intervals whose ramped p did not move (recorded, not gated).
    volatile float sink = 0.0f;
    std::size_t staticIntervals = 0;
    float prevPos = e.resolved().position;
    for (std::size_t i = 0; i < kCalls; ++i) {
        sink = sink + step();
        const float pos = e.resolved().position;
        if (std::bit_cast<std::uint32_t>(pos) == std::bit_cast<std::uint32_t>(prevPos))
            ++staticIntervals;
        prevPos = pos;
    }

    std::array<double, kReps> ns{};
    for (int rep = 0; rep < kReps; ++rep) {
        float acc = 0.0f;
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < kCalls; ++i)
            acc += step();
        const auto t1 = std::chrono::steady_clock::now();
        sink = sink + acc;
        ns[static_cast<std::size_t>(rep)] =
            static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count())
            / static_cast<double>(kCalls);
    }
    std::array<double, kReps> sorted = ns;
    std::sort(sorted.begin(), sorted.end());
    const double median = sorted[kReps / 2];
    const double pct = median / kIntervalNs * 100.0;
    const bool green = median <= kBarNs;

    std::ostringstream os;
    os << std::setprecision(6);
    os << "R-2 early CPU probe (tasks.md T008; plan R-2, deviation 7). Not the SC-018 gate (T014 / T020).\n";
    os << "workload: seed 0x8018, randomValidPair(rng, 0) redrawn until every leaf differs and no governing amount "
          "is 0; draws "
       << draws << "; fs " << kBmpFs << " Hz; ramp 2000 ms; mode " << bmpModeName(e.anchorMorphMode())
       << "; target re-set 0 <-> 1 on landing\n";
    os << "calls per repetition " << kCalls << ", repetitions " << kReps << ", warm-up static intervals "
       << staticIntervals << " of " << kCalls << "\n";
    os << "repetitions_ns_per_interval";
    for (const double v : ns)
        os << ' ' << v;
    os << '\n';
    os << "ns_per_interval " << median << '\n';
    os << "% of one core " << pct << '\n';
    os << "R2 EARLY " << (green ? "GREEN" : "RED") << ' ' << median << " vs 333.3\n";
    std::cout << os.str();
    bmpWriteEvidence("cpu-early.txt", os.str());
    static_cast<void>(sink);
}

// -----------------------------------------------------------------------------
// Main-loop harvest (2026-10-19, after the C-9 RED): the f0-only Lock-floor margin of showcase-B variants.
// The 1.5 f0 LP reading is h2 leakage at the monster end (sum_f0_rms is 3-5 dB below E_sub there), so the floor is
// read on the single-bin DFT at f0: margin = 20 log10 |H1(sum)| - 20 log10 |H1(anchor)| + 1 dB, Lock, p in {0.5, 1}.
// -----------------------------------------------------------------------------
TEST_CASE("BassMorph_ShowcaseHarvestProbe", "[systems][profundum][bass-morph][.]") {
    enableFTZDAZ();
    const auto base = Bmt::showcasePair();
    struct Variant {
        const char* label;
        void (*apply)(MorphState&);
    };
    const std::array<Variant, 14> variants{{
        {"baseline", [](MorphState&) {}},
        {"fold50", [](MorphState& b) { b.foldAmount = 50.0f; }},
        {"fold30", [](MorphState& b) { b.foldAmount = 30.0f; }},
        {"fold0", [](MorphState& b) { b.foldAmount = 0.0f; }},
        {"shape0.5", [](MorphState& b) { b.shape = 0.5f; }},
        {"shape0", [](MorphState& b) { b.shape = 0.0f; }},
        {"tone0.5", [](MorphState& b) { b.characterTone = 0.5f; }},
        {"charAmt0.5", [](MorphState& b) { b.characterAmount = 0.5f; }},
        {"asym0", [](MorphState& b) { b.excitation.asymmetry = 0.0f; }},
        {"excMix0.5", [](MorphState& b) { b.excitation.mix = 0.5f; }},
        {"edge0.7", [](MorphState& b) { b.recipe.edge = 0.7f; }},
        {"heavyEdge1", [](MorphState& b) { b.recipe = Krate::DSP::SpectralShapeRecipe::kHeavy; b.recipe.edge = 1.0f; }},
        {"charTube", [](MorphState& b) { b.characterAlgorithm = Krate::DSP::BassCharacterStage::Algorithm::Tube; }},
        {"fold50_shape0.5", [](MorphState& b) { b.foldAmount = 50.0f; b.shape = 0.5f; }},
    }};
    struct BmpNote {
        const char* name;
        double f0;
    };
    const std::array<BmpNote, 2> notes{{{"E1", static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(28))},
                                        {"C3", static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(48))}}};
    const std::array<float, 2> positions{0.5f, 1.0f};

    std::ostringstream os;
    os << std::setprecision(5);
    os << "Showcase-B harvest: Lock, f0-only readings (single-bin DFT), fs 48000, steady render.\n";
    os << "floor_f0_margin = 20log|H1 sum| - 20log|H1 anchor| + 1 dB (SC-008(d) on f0, Lock). D_cx / D_nl of B's end.\n\n";
    os << "variant\tnote\tp\tc\ttheta_deg\tanchor_f0_dB\tsum_f0_dB\tfloor_f0_margin_dB\tE_sub_LP_margin_dB\tD_cx_dB\tD_nl_dB\n";
    for (const Variant& v : variants) {
        MorphState b = base.second;
        v.apply(b);
        REQUIRE(Krate::DSP::isValidMorphState(b));
        double worst = 1e9;
        for (const BmpNote& note : notes) {
            for (const float p : positions) {
                Bmt::MorphChainConfig cfg;
                cfg.a = base.first;
                cfg.b = b;
                cfg.mode = BmMode::Lock;
                cfg.initialPosition = p;
                cfg.f0 = note.f0;
                cfg.fs = kBmpFs;
                cfg.ampEnvelope = false;
                const Bmt::MorphChainTaps t = Bmt::renderMorphChain(cfg);
                REQUIRE(bmpAllFinite(t.sumL));
                const std::complex<double> hc = bmpH1(t.characterL, t.characterR, note.f0, kBmpFs);
                const std::complex<double> ha = bmpH1(t.anchorL, t.anchorR, note.f0, kBmpFs);
                const std::complex<double> hs = bmpH1(t.sumL, t.sumR, note.f0, kBmpFs);
                const double level = static_cast<double>(t.anchorLevelHanded.back());
                const double c = std::abs(hc / ha) * level;
                const double theta = std::arg(hc * std::conj(ha)) * 180.0 / kBmpPi;
                const double anchorDb = bmpDb20(std::abs(ha));
                const double sumDb = bmpDb20(std::abs(hs));
                const double margin = sumDb - anchorDb + 1.0;
                const double lpMargin = Bmt::eSubDb(t.sumL, t.sumR, note.f0, kBmpFs)
                                        - Bmt::eSubDb(t.anchorL, t.anchorR, note.f0, kBmpFs) + 1.0;
                worst = std::min(worst, margin);
                os << v.label << '\t' << note.name << '\t' << p << '\t' << c << '\t' << theta << '\t' << anchorDb
                   << '\t' << sumDb << '\t' << margin << '\t' << lpMargin << '\t' << Bmt::dCxDb(t, note.f0, kBmpFs)
                   << '\t' << Bmt::dNlDb(t, note.f0, kBmpFs) << '\n';
            }
        }
        os << v.label << "\tWORST_FLOOR_F0_MARGIN\t" << worst << (worst >= 0.0 ? "\tGREEN" : "\tRED") << "\n";
    }
    bmpWriteEvidence("showcase-harvest.txt", os.str());
    std::cout << os.str();
    CHECK(true);
}

// -----------------------------------------------------------------------------
// B7-2 harvest (user ruling 2026-10-19): showcase B over a grid; f0 reading; floor at the resolved Anchor level (B7-1).
// Shards: BM_HARVEST_SHARD="i/N" runs the candidates with index % N == i; output evidence/harvest/showcase-<i>.txt.
// -----------------------------------------------------------------------------
TEST_CASE("BassMorph_ShowcaseHarvestGrid", "[systems][profundum][bass-morph][.]") {
    enableFTZDAZ();
    int shard = 0;
    int shards = 1;
    if (const char* env = std::getenv("BM_HARVEST_SHARD")) {
        REQUIRE(std::sscanf(env, "%d/%d", &shard, &shards) == 2);
        REQUIRE(shards > 0);
        REQUIRE(shard >= 0);
        REQUIRE(shard < shards);
    }
    const auto base = Bmt::showcasePair();
    const std::array<const char*, 2> recipeNames{"growl", "heavy"};
    const std::array<float, 3> folds{80.0f, 50.0f, 30.0f};
    const std::array<float, 2> shapes{1.0f, 0.5f};
    const std::array<float, 2> tones{0.8f, 0.5f};
    const std::array<float, 2> amounts{0.8f, 0.5f};
    const std::array<float, 2> levels{0.7f, 0.5f};
    struct BmpNote {
        const char* name;
        double f0;
    };
    const std::array<BmpNote, 2> notes{{{"E1", static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(28))},
                                        {"C3", static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(48))}}};
    const std::array<BmMode, 2> modes{BmMode::Lock, BmMode::Blend};
    constexpr int kPositions = 11;

    std::ostringstream os;
    os << std::setprecision(5);
    os << "# B7-2 showcase-B harvest, shard " << shard << "/" << shards << ". fs 48000, steady render, f0 reading.\n";
    os << "# ROW\tcand\tnote\tp\tmode\tc\ttheta_deg\tanchor_f0_dB\tsum_f0_dB\tfloor_margin_dB\n";
    os << "# CAND\tcand\trecipe\tfold\tshape\tone\tamount\tlevel\tworst_floor_lock\tworst_floor_blend\tsc015_anchor_E1\tsc015_anchor_C3\t"
          "dcx_travel_E1\tdnl_travel_E1\tdst_travel_E1\tdbr_travel_E1\tdcx_travel_C3\tdnl_travel_C3\tdst_travel_C3\tdbr_travel_C3\tanchor_f0_p1_E1\tverdict\n";
    const int kCandidates = static_cast<int>(recipeNames.size() * folds.size() * shapes.size() * tones.size()
                                             * amounts.size() * levels.size());
    for (int index = 0; index < kCandidates; ++index) {
        if (index % shards != shard) {
            continue;
        }
        // Decode the candidate index in the original nesting order (recipe, fold, shape, tone, amount, level).
        int rest = index;
        const float level = levels[static_cast<std::size_t>(rest % static_cast<int>(levels.size()))];
        rest /= static_cast<int>(levels.size());
        const float amount = amounts[static_cast<std::size_t>(rest % static_cast<int>(amounts.size()))];
        rest /= static_cast<int>(amounts.size());
        const float tone = tones[static_cast<std::size_t>(rest % static_cast<int>(tones.size()))];
        rest /= static_cast<int>(tones.size());
        const float shape = shapes[static_cast<std::size_t>(rest % static_cast<int>(shapes.size()))];
        rest /= static_cast<int>(shapes.size());
        const float fold = folds[static_cast<std::size_t>(rest % static_cast<int>(folds.size()))];
        rest /= static_cast<int>(folds.size());
        const std::size_t ri = static_cast<std::size_t>(rest);
        MorphState b = base.second;
        if (ri == 1) {
            b.recipe = Krate::DSP::SpectralShapeRecipe::kHeavy;
            b.recipe.edge = 1.0f;
        }
        b.foldAmount = fold;
        b.shape = shape;
        b.characterTone = tone;
        b.characterAmount = amount;
        b.anchor.level = level;
        REQUIRE(Krate::DSP::isValidMorphState(b));

        double worstLock = 1e9;
        double worstBlend = 1e9;
        std::array<double, 2> sc015{0.0, 0.0};
        std::array<std::array<double, 4>, 2> travel{};
        std::array<std::array<double, 2>, 2> sumF0AtOne{};   // [note][mode]
        double anchorF0P1E1 = 0.0;
        for (std::size_t ni = 0; ni < notes.size(); ++ni) {
            std::array<double, 4> d0{};
            std::array<double, 4> d1{};
            for (int pi = 0; pi < kPositions; ++pi) {
                const float p = static_cast<float>(pi) / static_cast<float>(kPositions - 1);
                for (std::size_t mi = 0; mi < modes.size(); ++mi) {
                    if (pi == 0 && mi == 1) {
                        continue;   // p = 0 is A in every mode
                    }
                    Bmt::MorphChainConfig cfg;
                    cfg.a = base.first;
                    cfg.b = b;
                    cfg.mode = modes[mi];
                    cfg.initialPosition = p;
                    cfg.f0 = notes[ni].f0;
                    cfg.fs = kBmpFs;
                    cfg.ampEnvelope = false;
                    const Bmt::MorphChainTaps t = Bmt::renderMorphChain(cfg);
                    REQUIRE(bmpAllFinite(t.sumL));
                    const std::complex<double> hc = bmpH1(t.characterL, t.characterR, notes[ni].f0, kBmpFs);
                    const std::complex<double> ha = bmpH1(t.anchorL, t.anchorR, notes[ni].f0, kBmpFs);
                    const std::complex<double> hs = bmpH1(t.sumL, t.sumR, notes[ni].f0, kBmpFs);
                    const double lv = static_cast<double>(t.anchorLevelHanded.back());
                    const double c = std::abs(hc / ha) * lv;
                    const double theta = std::arg(hc * std::conj(ha)) * 180.0 / kBmpPi;
                    const double anchorDb = bmpDb20(std::abs(ha));
                    const double sumDb = bmpDb20(std::abs(hs));
                    const double margin = sumDb - anchorDb + 1.0;   // B7-1: against the render's own Anchor tap
                    if (mi == 0) {
                        worstLock = std::min(worstLock, margin);
                    } else {
                        worstBlend = std::min(worstBlend, margin);
                    }
                    if (pi == kPositions - 1) {
                        sumF0AtOne[ni][mi] = sumDb;
                        if (mi == 0 && ni == 0) {
                            anchorF0P1E1 = anchorDb;
                        }
                    }
                    if (mi == 0 && (pi == 0 || pi == kPositions - 1)) {
                        std::array<double, 4>& d = (pi == 0) ? d0 : d1;
                        d[0] = Bmt::dCxDb(t, notes[ni].f0, kBmpFs);
                        d[1] = Bmt::dNlDb(t, notes[ni].f0, kBmpFs);
                        d[2] = Bmt::dStDb(t, static_cast<double>(b.field.crossoverHz), kBmpFs);
                        d[3] = Bmt::dBrOct(t, notes[ni].f0, kBmpFs);
                    }
                    os << "ROW\t" << index << '\t' << notes[ni].name << '\t' << p << '\t' << bmpModeName(modes[mi]) << '\t'
                       << c << '\t' << theta << '\t' << anchorDb << '\t' << sumDb << '\t' << margin << '\n';
                }
            }
            sc015[ni] = sumF0AtOne[ni][0] - sumF0AtOne[ni][1] - 2.0;
            for (int k = 0; k < 4; ++k) {
                travel[ni][k] = d1[k] - d0[k];
            }
        }
        bool ok = worstLock >= 0.0 && worstBlend >= 0.0;
        for (std::size_t ni = 0; ni < 2; ++ni) {
            ok = ok && sc015[ni] >= 0.0 && travel[ni][0] >= 3.0 && travel[ni][1] >= 3.0 && travel[ni][2] >= 3.0
                 && travel[ni][3] >= 0.25;
        }
        os << "CAND\t" << index << '\t' << recipeNames[ri] << '\t' << fold << '\t' << shape << '\t' << tone << '\t'
           << amount << '\t' << level << '\t' << worstLock << '\t' << worstBlend << '\t' << sc015[0] << '\t'
           << sc015[1];
        for (std::size_t ni = 0; ni < 2; ++ni) {
            for (int k = 0; k < 4; ++k) {
                os << '\t' << travel[ni][k];
            }
        }
        os << '\t' << anchorF0P1E1 << '\t' << (ok ? "OK" : "FAIL") << '\n';
        std::cout << "harvest cand " << index << " done\n";
    }
    bmpWriteEvidence("harvest/showcase-" + std::to_string(shard) + ".txt", os.str());
    CHECK(true);
}
