// ==============================================================================
// Layer 3: System Tests - BassMorph aliasing
//                          (Profundum Phase 8, specs/profundum-phase8-bass-morph)
// ==============================================================================
// tasks.md T013 (SC-017; FR-086; plan S1 C-16; deviation 5). Anti-aliasing while morphing:
//   (a) GATED, frozen-position grid: maxNonlinearPair() at p in {0, 0.05, ..., 1} (21 static steady renders,
//       initialPosition = p, 1 s settle), plus the two arms of the algorithm crossing on algorithmCrossingPair()
//       (plan S1 C-16: both governing amounts non-zero, so the switch falls at kThetaUp between two live nonlinear
//       arms). StereoFieldTest::wideAliasedDbfs on characterL and characterR, each <= -80 dBFS.
//   (b) RECORDED, never gated: a 1 s 0 -> 1 ramp render (rampMs 1000) of each pair; non-harmonic in-band energy of
//       the character tap over a Hann window [g_sw, g_sw + one period + 10 ms] at the recorded switch grid sample,
//       excluding +/-(2 / T_window) Hz of every k*f0. Appended to evidence/aliasing-crossfade.txt.
// Shards BassMorph_Aliasing_<44k|48k|96k> carry [long] (E0, E1, E2 each); BassMorph_AliasingSmoke is the per-push
// sibling (48 kHz, E1, (a) at p in {0, 0.5, 1} plus both crossing arms).
// FTZ / DAZ: set by every case (they read the core and the character stage; tasks.md rule 9).
// ==============================================================================

#include "bass_morph_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <enable_ftz_daz.h>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace Bmt = Krate::DSP::BassMorphTest;
using Krate::DSP::BassCharacterStage;
using Krate::DSP::BassMorphEngine;
using Krate::DSP::MorphState;
using BmaAlgorithm = BassCharacterStage::Algorithm;
using BmaPair = std::pair<MorphState, MorphState>;

constexpr double kBmaBarDbfs = -80.0;   // SC-017: the ratified bar, every grid point and arm, per channel
constexpr double kBmaPi = 3.14159265358979323846264338327950288;
constexpr std::size_t kBmaGridPoints = 21;   // p in {0, 0.05, ..., 1}

struct BmaNote {
    const char* name;
    double f0;
};

BmaNote bmaE0() { return {"E0", static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(16))}; }
BmaNote bmaE1() { return {"E1", static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(28))}; }
BmaNote bmaE2() { return {"E2", static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(40))}; }

/// tasks.md rule 11: evidence goes under specs/profundum-phase8-bass-morph/evidence/.
std::string bmaEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase8-bass-morph/evidence/";
}

void bmaWriteEvidence(const std::string& name, const std::string& text, bool append) {
    const std::string dir = bmaEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream out(dir + name, append ? std::ios::app : std::ios::trunc);
    out << text;
}

const char* bmaAlgorithmName(BmaAlgorithm a) {
    switch (a) {
        case BmaAlgorithm::Tube: return "Tube";
        case BmaAlgorithm::Fold: return "Fold";
        case BmaAlgorithm::Rectify: return "Rectify";
        case BmaAlgorithm::Bite: return "Bite";
        case BmaAlgorithm::Crush: return "Crush";
        case BmaAlgorithm::Fracture: return "Fracture";
    }
    return "?";
}

bool bmaAllFinite(const std::vector<float>& x) {
    return std::all_of(x.begin(), x.end(), [](float v) { return Krate::DSP::detail::isFinite(v); });
}

/// Render length for a frozen point: the 1 s settle plus the longest alias frame either algorithm can need at f0
/// (Fracture reads at f0 / 2, StereoFieldTest::wideAliasedDbfs), plus 50 ms margin (the rectifier_test.cpp
/// precedent). The default descriptorSeconds is shorter than the E0 Fracture frame at 96 kHz.
double bmaFrozenSeconds(double fs, double f0) {
    const std::size_t L = Krate::DSP::HarmonicFoldTest::aliasFftLength(fs, 0.5 * f0);
    return Krate::DSP::MassDynamicsTest::kSettleSeconds + static_cast<double>(L) / fs + 0.05;
}

/// tasks.md rule 17: advance until p and p_a have landed, with the hard interval cap; hitting it fails loudly.
void bmaAdvanceUntilLanded(BassMorphEngine& e, double fs) {
    const auto cap = static_cast<std::size_t>(
        2.0 * std::ceil(static_cast<double>(e.positionRampMs()) * fs / 32000.0) + 8.0);
    std::size_t n = 0;
    auto landed = [&e] {
        return e.position() == e.targetPosition()
               && std::bit_cast<std::uint32_t>(e.anchorPosition())
                      == std::bit_cast<std::uint32_t>(BassMorphEngine::curve(e.anchorMorphMode(), e.position()));
    };
    while (!landed()) {
        if (n >= cap) {
            FAIL("engine did not land within " << cap << " intervals: position " << e.position() << ", target "
                                               << e.targetPosition() << ", anchorPosition " << e.anchorPosition());
        }
        static_cast<void>(e.advanceInterval());
        ++n;
    }
}

/// One gated frozen point (SC-017(a)).
struct BmaPoint {
    std::string kind;   // "grid" | "armA" | "armB"
    float p = 0.0f;
    BmaAlgorithm algorithm = BmaAlgorithm::Tube;
    double aliasedL = 0.0;
    double aliasedR = 0.0;
};

/// A frozen steady render of @p pair at @p p (initialPosition = p, snapped by prepare's reset(), no automation, no
/// ADSR), read with wideAliasedDbfs at the resolved algorithm on each channel of the character tap.
/// @p expected, when set, is the algorithm the render's first resolved interval must carry (crossing arms).
BmaPoint bmaFrozenPoint(const BmaPair& pair, float p, const BmaNote& note, double fs, const char* kind,
                        const BmaAlgorithm* expected) {
    Bmt::MorphChainConfig cfg;
    cfg.a = pair.first;
    cfg.b = pair.second;
    cfg.initialPosition = p;
    cfg.f0 = note.f0;
    cfg.fs = fs;
    cfg.seconds = bmaFrozenSeconds(fs, note.f0);
    cfg.ampEnvelope = false;   // the steady render
    const Bmt::MorphChainTaps t = Bmt::renderMorphChain(cfg);

    INFO("SC-017 " << kind << " " << note.name << " fs " << fs << " p " << p);
    REQUIRE(!t.resolvedPerInterval.empty());
    const BmaAlgorithm alg = t.resolvedPerInterval.front().state.characterAlgorithm;
    REQUIRE(t.resolvedPerInterval.back().state.characterAlgorithm == alg);   // frozen: no switch inside the render
    REQUIRE(t.discreteSetterCalls.empty());
    if (expected != nullptr) {
        REQUIRE(alg == *expected);   // plan S1 C-16: the render reads the arm the landed engine selected
    }
    REQUIRE(bmaAllFinite(t.characterL));
    REQUIRE(bmaAllFinite(t.characterR));

    BmaPoint pt;
    pt.kind = kind;
    pt.p = p;
    pt.algorithm = alg;
    pt.aliasedL = Krate::DSP::StereoFieldTest::wideAliasedDbfs(t.characterL, fs, note.f0, alg);
    pt.aliasedR = Krate::DSP::StereoFieldTest::wideAliasedDbfs(t.characterR, fs, note.f0, alg);
    std::cout << std::setprecision(6) << "SC017," << kind << ',' << note.name << ",fs," << fs << ",p," << p << ','
              << bmaAlgorithmName(alg) << ",aliased_L_dbfs," << pt.aliasedL << ",aliased_R_dbfs," << pt.aliasedR
              << '\n';
    CHECK(pt.aliasedL <= kBmaBarDbfs);
    CHECK(pt.aliasedR <= kBmaBarDbfs);
    return pt;
}

/// SC-017(a) grid on maxNonlinearPair() at @p positions. Every p in (0, 1) must resolve Fracture (FR-034 rule 2:
/// A's Character Amount is 0); p = 0 resolves A's Tube (rule 1).
void bmaGrid(const std::vector<float>& positions, const BmaNote& note, double fs, std::vector<BmaPoint>& out) {
    const BmaPair pair = Bmt::maxNonlinearPair();
    for (const float p : positions) {
        const BmaAlgorithm expected = (p <= 0.0f) ? pair.first.characterAlgorithm : pair.second.characterAlgorithm;
        out.push_back(bmaFrozenPoint(pair, p, note, fs, "grid", &expected));
    }
}

/// SC-017(a) crossing arms on algorithmCrossingPair() (plan S1 C-16): a fresh engine reset at 0, ramped to
/// kThetaUp - 0.005 (lands with A selected: below kThetaDown it is A, inside the band it is kept), then to kThetaUp
/// (lands with B). Each arm is then a frozen steady render at that p; a fresh engine's reset() keeps its initial A
/// inside the band and selects B at kThetaUp (FR-011), and the render REQUIREs it resolved the landed algorithm.
void bmaArms(const BmaNote& note, double fs, std::vector<BmaPoint>& out) {
    const BmaPair pair = Bmt::algorithmCrossingPair();
    REQUIRE(pair.first.characterAmount != 0.0f);    // both governing amounts non-zero: rule 3 switches at kThetaUp
    REQUIRE(pair.second.characterAmount != 0.0f);

    BassMorphEngine e;
    REQUIRE(e.setEndpoint(BassMorphEngine::Endpoint::A, pair.first));
    REQUIRE(e.setEndpoint(BassMorphEngine::Endpoint::B, pair.second));
    e.setPosition(0.0f);
    e.prepare(fs);   // ends with reset() at 0 (plan S1 C-5)
    REQUIRE(e.selection(BassMorphEngine::SelectionGroup::Character) == BassMorphEngine::Endpoint::A);

    const float pArmA = BassMorphEngine::kThetaUp - 0.005f;
    e.setPosition(pArmA);
    bmaAdvanceUntilLanded(e, fs);
    REQUIRE(e.selection(BassMorphEngine::SelectionGroup::Character) == BassMorphEngine::Endpoint::A);
    const BmaAlgorithm algA = e.resolved().state.characterAlgorithm;
    REQUIRE(algA == pair.first.characterAlgorithm);

    const float pArmB = BassMorphEngine::kThetaUp;
    e.setPosition(pArmB);
    bmaAdvanceUntilLanded(e, fs);
    REQUIRE(e.selection(BassMorphEngine::SelectionGroup::Character) == BassMorphEngine::Endpoint::B);
    const BmaAlgorithm algB = e.resolved().state.characterAlgorithm;
    REQUIRE(algB == pair.second.characterAlgorithm);

    out.push_back(bmaFrozenPoint(pair, pArmA, note, fs, "armA", &algA));
    out.push_back(bmaFrozenPoint(pair, pArmB, note, fs, "armB", &algB));
}

/// Non-harmonic in-band energy of x[start, start + W) under a Hann window: per-bin power from a direct DFT at the
/// window's own bins m / T (m = 0 .. W/2), normalised so the one-sided sum is the windowed mean square (a 0 dBFS
/// sine sums to 0.5); bins within +/-(2 / T) Hz of any k * spacing (k >= 0) are excluded. Returned in dB re 0.5.
struct BmaNonHarmonic {
    double dbfs = Krate::DSP::StereoFieldTest::kFloorDb;
    double excludedFraction = 0.0;
};

BmaNonHarmonic bmaNonHarmonic(const std::vector<float>& x, std::size_t start, std::size_t W, double fs,
                              double spacing) {
    REQUIRE(W >= 4);
    REQUIRE(start + W <= x.size());
    std::vector<double> w(W);
    double sumW2 = 0.0;
    for (std::size_t n = 0; n < W; ++n) {
        w[n] = 0.5 - 0.5 * std::cos(2.0 * kBmaPi * static_cast<double>(n) / static_cast<double>(W));
        sumW2 += w[n] * w[n];
    }
    const double T = static_cast<double>(W) / fs;
    const double halfWidthHz = 2.0 / T;
    const std::size_t half = W / 2;
    double kept = 0.0;
    std::size_t excluded = 0;
    for (std::size_t m = 1; m <= half; ++m) {   // m = 0 (DC) is within 2 / T of 0 * spacing: always excluded
        const double hz = static_cast<double>(m) / T;
        const double k = std::round(hz / spacing);
        if (std::abs(hz - k * spacing) <= halfWidthHz) {
            ++excluded;
            continue;
        }
        double re = 0.0;
        double im = 0.0;
        const double omega = 2.0 * kBmaPi * static_cast<double>(m) / static_cast<double>(W);
        for (std::size_t n = 0; n < W; ++n) {
            const double v = static_cast<double>(x[start + n]) * w[n];
            const double ph = omega * static_cast<double>(n);
            re += v * std::cos(ph);
            im -= v * std::sin(ph);
        }
        const double scale = (m == half && W % 2 == 0) ? 1.0 : 2.0;   // Nyquist bin is not doubled
        kept += scale * (re * re + im * im) / (static_cast<double>(W) * sumW2);
    }
    BmaNonHarmonic r;
    r.dbfs = Krate::DSP::ProfundumTest::safeDb10(kept / 0.5);
    r.excludedFraction = static_cast<double>(excluded) / static_cast<double>(half);
    return r;
}

/// SC-017(b) record (never gated): a 1 s 0 -> 1 ramp (rampMs 1000; the Position event at 0.25 s so the note has
/// sounded before the ramp starts) of @p pair; g_sw = the recorded character setAlgorithm call
/// (discreteSetterCalls); window [g_sw, g_sw + one period + 10 ms] (transitionEndSamples(CharacterAlgorithm)).
std::string bmaCrossfadeRecord(const BmaPair& pair, const char* pairName, bool switchAtFirstMove, const BmaNote& note,
                               double fs) {
    constexpr double kRampMs = 1000.0;
    Bmt::MorphChainConfig cfg;
    cfg.a = pair.first;
    cfg.b = pair.second;
    cfg.initialPosition = 0.0f;
    cfg.rampMs = static_cast<float>(kRampMs);
    cfg.f0 = note.f0;
    cfg.fs = fs;
    cfg.seconds = 1.5;
    cfg.ampEnvelope = false;
    const std::size_t eventSample = (static_cast<std::size_t>(std::floor(0.25 * fs)) / Bmt::kGrid) * Bmt::kGrid;
    cfg.automation.push_back(
        Bmt::MorphEvent{.sample = eventSample, .kind = Bmt::MorphEvent::Kind::Position, .value = 1.0f});
    const Bmt::MorphChainTaps t = Bmt::renderMorphChain(cfg);

    INFO("SC-017(b) " << pairName << " " << note.name << " fs " << fs);
    REQUIRE(bmaAllFinite(t.characterL));
    REQUIRE(bmaAllFinite(t.characterR));
    std::size_t gSw = 0;
    std::size_t calls = 0;
    for (const auto& c : t.discreteSetterCalls) {
        if (c.leaf == Bmt::kLeafCharacterAlgorithm) {
            if (calls == 0)
                gSw = c.sample;
            ++calls;
        }
    }
    REQUIRE(calls == 1);   // one switch, A -> B, on the way up
    if (switchAtFirstMove) {
        // maxNonlinearPair: FR-034 rule 2 switches at the first interval with p > 0.
        std::size_t firstMove = 0;
        bool found = false;
        for (std::size_t i = 0; i < t.resolvedPerInterval.size(); ++i) {
            if (t.resolvedPerInterval[i].position > 0.0f) {
                firstMove = t.advanceSamples[i];
                found = true;
                break;
            }
        }
        REQUIRE(found);
        REQUIRE(gSw == firstMove);
    }
    // Window: the pre-roll + crossfade (one period + 10 ms), but at least 8 periods of f0 (T015-4 reading refinement:
    // with the short window the +/-2/T exclusion removed every in-band bin for f0 <= 300 Hz).
    const std::size_t W = std::max(Bmt::transitionEndSamples(Bmt::TransitionKind::CharacterAlgorithm, note.f0, fs),
                                   static_cast<std::size_t>(std::ceil(8.0 * fs / note.f0)));
    REQUIRE(gSw + W <= t.characterL.size());

    const double T = static_cast<double>(W) / fs;
    const BmaNonHarmonic l = bmaNonHarmonic(t.characterL, gSw, W, fs, note.f0);
    const BmaNonHarmonic r = bmaNonHarmonic(t.characterR, gSw, W, fs, note.f0);
    // Supplementary column: the same measure excluding k * f0 / 2 (Fracture's output period is 2 / f0, the
    // spacing wideAliasedDbfs uses for it).
    const BmaNonHarmonic lh = bmaNonHarmonic(t.characterL, gSw, W, fs, 0.5 * note.f0);
    const BmaNonHarmonic rh = bmaNonHarmonic(t.characterR, gSw, W, fs, 0.5 * note.f0);

    std::ostringstream os;
    os << std::setprecision(6);
    os << "SC017B," << pairName << ',' << note.name << ",f0," << note.f0 << ",fs," << fs << ",g_sw," << gSw
       << ",window_samples," << W << ",T_window_s," << T << ",exclusion_hz," << (2.0 / T) << ",nonharm_L_dbfs,"
       << l.dbfs << ",nonharm_R_dbfs," << r.dbfs << ",excluded_fraction," << l.excludedFraction
       << ",nonharm_half_L_dbfs," << lh.dbfs << ",nonharm_half_R_dbfs," << rh.dbfs << ",excluded_fraction_half,"
       << lh.excludedFraction << '\n';
    std::cout << os.str();
    return os.str();
}

std::string bmaCsv(const std::vector<std::pair<BmaNote, BmaPoint>>& rows, double fs) {
    std::ostringstream os;
    os << std::setprecision(6);
    os << "pair,kind,note,f0,fs,p,algorithm,aliased_L_dbfs,aliased_R_dbfs,bar_dbfs,pass\n";
    for (const auto& [note, pt] : rows) {
        const bool pass = pt.aliasedL <= kBmaBarDbfs && pt.aliasedR <= kBmaBarDbfs;
        os << (pt.kind == "grid" ? "maxNonlinearPair" : "algorithmCrossingPair") << ',' << pt.kind << ','
           << note.name << ',' << note.f0 << ',' << fs << ',' << pt.p << ',' << bmaAlgorithmName(pt.algorithm) << ','
           << pt.aliasedL << ',' << pt.aliasedR << ',' << kBmaBarDbfs << ',' << (pass ? 1 : 0) << '\n';
    }
    return os.str();
}

std::vector<float> bmaFullGrid() {
    std::vector<float> g;
    g.reserve(kBmaGridPoints);
    for (std::size_t i = 0; i < kBmaGridPoints; ++i)
        g.push_back(static_cast<float>(static_cast<double>(i) / static_cast<double>(kBmaGridPoints - 1)));
    return g;
}

/// One [long] shard: E0, E1, E2 at @p fs; (a) the 21-point grid and both arms (gated); (b) both ramp records.
void bmaShard(double fs, const char* fsLabel) {
    enableFTZDAZ();
    const std::vector<float> grid = bmaFullGrid();
    REQUIRE(grid.size() == kBmaGridPoints);
    REQUIRE(grid.front() == 0.0f);
    REQUIRE(grid.back() == 1.0f);
    std::vector<std::pair<BmaNote, BmaPoint>> rows;
    std::ostringstream record;
    record << std::setprecision(6);
    record << "# SC-017(b) crossfade-window record (tasks.md T013, plan S1 C-16), fs " << fs
           << " Hz: Hann [g_sw, g_sw + one period + 10 ms] of the character tap, non-harmonic in-band energy "
              "excluding +/-(2 / T_window) of every k*f0 (and, supplementary, of every k*f0/2). Not gated.\n";
    double worst = -1000.0;
    for (const BmaNote& note : {bmaE0(), bmaE1(), bmaE2()}) {
        std::vector<BmaPoint> pts;
        bmaGrid(grid, note, fs, pts);
        bmaArms(note, fs, pts);
        for (const BmaPoint& pt : pts) {
            worst = std::max({worst, pt.aliasedL, pt.aliasedR});
            rows.emplace_back(note, pt);
        }
        record << bmaCrossfadeRecord(Bmt::algorithmCrossingPair(), "algorithmCrossingPair", false, note, fs);
        record << bmaCrossfadeRecord(Bmt::maxNonlinearPair(), "maxNonlinearPair", true, note, fs);
    }
    std::cout << std::setprecision(6) << "SC017 WORST " << fsLabel << ' ' << worst << " dBFS (bar " << kBmaBarDbfs
              << ")\n";
    bmaWriteEvidence(std::string("sc017-aliasing-") + fsLabel + ".csv", bmaCsv(rows, fs), false);
    bmaWriteEvidence("aliasing-crossfade.txt", record.str(), true);
}

}  // namespace

// -----------------------------------------------------------------------------
// SC-017 (FR-086; plan S1 C-16): anti-aliasing while morphing, one [long] shard per sample rate
// -----------------------------------------------------------------------------

// SC-017
TEST_CASE("BassMorph_Aliasing_44k", "[systems][profundum][bass-morph][sc-017][long]") {
    bmaShard(44100.0, "44k");
}

// SC-017
TEST_CASE("BassMorph_Aliasing_48k", "[systems][profundum][bass-morph][sc-017][long]") {
    bmaShard(48000.0, "48k");
}

// SC-017
TEST_CASE("BassMorph_Aliasing_96k", "[systems][profundum][bass-morph][sc-017][long]") {
    bmaShard(96000.0, "96k");
}

// SC-017 (deviation 5: the per-push sibling; same assertions on one small cell)
TEST_CASE("BassMorph_AliasingSmoke", "[systems][profundum][bass-morph][sc-017]") {
    enableFTZDAZ();
    constexpr double kFs = 48000.0;
    const BmaNote note = bmaE1();
    std::vector<BmaPoint> pts;
    bmaGrid({0.0f, 0.5f, 1.0f}, note, kFs, pts);
    bmaArms(note, kFs, pts);   // plan S1 C-16
    REQUIRE(pts.size() == 5);
    REQUIRE(pts[1].algorithm == BmaAlgorithm::Fracture);   // FR-034 rule 2 at p = 0.5 on maxNonlinearPair
    REQUIRE(pts[3].algorithm == BmaAlgorithm::Tube);       // arm A
    REQUIRE(pts[4].algorithm == BmaAlgorithm::Fracture);   // arm B
}
