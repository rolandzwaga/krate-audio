// ==============================================================================
// Layer 3: System Tests - BassMorph trajectory
//                          (Profundum Phase 8, specs/profundum-phase8-bass-morph)
// ==============================================================================
// tasks.md T011 (SC-008; FR-040, FR-033, FR-051; plan C-9, C-15, R-13; deviations 5, 8).
//   BassMorph_ShowcaseTrajectory_<Lock|Blend|Punch>_<E1|C3>  [sc-008][long]: showcasePair(), 48 kHz, 21 static p
//       (initialPosition = p, no automation). Steady render per p -> D_cx, D_nl, D_st (f_c = the resolved
//       field.crossoverHz), D_br, L_K, E_sub (sum tap), E_sub,out (out tap); gate render per p -> D_t.
//       (a) D(p_j) >= max_{i<j} D(p_i) - eps (deviation 8; eps 0.25 dB, 0.02 oct for D_br, D_t >= 0.98 x the
//           running max); no gate render reaches 12 s.
//       (b) net travel D(1) - D(0) >= 3 dB for D_cx, D_nl, D_st; >= 0.25 oct for D_br; D_t(1) / D_t(0) >= 2.
//       (c) L_K(p) >= min(L_K(0), L_K(1)) - 1.5 dB.
//       (d) the floor at the resolved Anchor level (spec SC-008(d), ruling B7-1, 2026-10-19):
//           E_sub(p) >= F_lock(p) + 20 log10(L_m(p) / L_A) - 1.0 dB, L_m(p) the render's resolved anchor.level
//           (Mass 50 at both ends, so the handed level is the resolved level), L_A = A's anchor.level; in Lock
//           L_m = L_A, so the Lock clause is E_sub >= F_lock - 1.0 dB. The Punch shards also gate
//           E_sub_Punch(p) >= E_sub_Blend(p) - 0.1 dB.
//       A red in (c) or (d) is never fixed in the engine's output (spec Q7): T015 records it as DEFECT: with the
//       per-p table this case prints.
//       Writes evidence/sc008-trajectory-<mode>-<note>.csv.
//   BassMorph_ShowcaseTrajectorySmoke  [sc-008]: Blend, E1, p in {0, 0.5, 1}, (a) - (d) on those three points.
// F_lock(p) and Blend's E_sub(p) are cached per (note, p) in function-local statics: the Lock shard fills F_lock,
// the Blend shard fills Blend's E_sub; any case that finds its entry empty renders it itself.
// ==============================================================================

#include "bass_morph_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <enable_ftz_daz.h>

#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/morph_state.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace Bmt = Krate::DSP::BassMorphTest;
using Krate::DSP::BassMorphEngine;
using Krate::DSP::MorphState;
using BmtMode = BassMorphEngine::AnchorMorphMode;

constexpr double kBmtFs = 48000.0;
constexpr double kBmtEpsDb = 0.25;          // SC-008(a), plan C-15
constexpr double kBmtEpsOct = 0.02;         // SC-008(a) D_br
constexpr double kBmtDtRatioEps = 0.98;     // SC-008(a) D_t: 2 %
constexpr double kBmtTravelDb = 3.0;        // SC-015 bar (SC-008(b))
constexpr double kBmtTravelOct = 0.25;      // SC-015 bar D_br
constexpr double kBmtTravelDtRatio = 2.0;   // SC-015 bar D_t
constexpr double kBmtDipDb = 1.5;           // SC-008(c), ruled Q7
constexpr double kBmtFloorDb = 1.0;         // SC-008(d)
constexpr double kBmtPunchVsBlendDb = 0.1;  // SC-008(d) Punch vs Blend
constexpr double kBmtMaxGateSeconds = 12.0;

std::string bmtEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase8-bass-morph/evidence/";
}

void bmtWriteEvidence(const std::string& name, const std::string& text) {
    const std::string dir = bmtEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream out(dir + name);
    out << text;
}

const char* bmtModeName(BmtMode m) {
    switch (m) {
        case BmtMode::Lock: return "Lock";
        case BmtMode::Blend: return "Blend";
        case BmtMode::Punch: return "Punch";
    }
    return "?";
}

struct BmtNote {
    const char* name;
    double f0;
};

BmtNote bmtNoteE1() {
    return {"E1", static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(28))};
}

BmtNote bmtNoteC3() {
    return {"C3", static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(48))};
}

std::vector<float> bmtGrid21() {
    std::vector<float> p(21);
    for (std::size_t i = 0; i < p.size(); ++i)
        p[i] = static_cast<float>(static_cast<double>(i) / 20.0);
    p.back() = 1.0f;
    return p;
}

/// The steady render (spec descriptor block): amp envelope bypassed, the engine reset at p, no automation.
Bmt::MorphChainTaps bmtSteady(const std::pair<MorphState, MorphState>& pair, BmtMode mode, float p, double f0) {
    Bmt::MorphChainConfig cfg;
    cfg.a = pair.first;
    cfg.b = pair.second;
    cfg.mode = mode;
    cfg.initialPosition = p;
    cfg.f0 = f0;
    cfg.fs = kBmtFs;
    cfg.ampEnvelope = false;
    Bmt::MorphChainTaps t = Bmt::renderMorphChain(cfg);
    REQUIRE(!t.resolvedPerInterval.empty());
    return t;
}

/// Function-local caches keyed by (note, p bits): F_lock(p) (Lock render's Anchor tap) and Blend's E_sub(p).
using BmtCacheKey = std::pair<std::string, std::uint32_t>;

std::map<BmtCacheKey, double>& bmtFlockCache() {
    static std::map<BmtCacheKey, double> cache;
    return cache;
}

std::map<BmtCacheKey, double>& bmtBlendESubCache() {
    static std::map<BmtCacheKey, double> cache;
    return cache;
}

BmtCacheKey bmtKey(const BmtNote& note, float p) {
    return {std::string(note.name), std::bit_cast<std::uint32_t>(p)};
}

double bmtFlock(const std::pair<MorphState, MorphState>& pair, const BmtNote& note, float p) {
    auto& cache = bmtFlockCache();
    const auto it = cache.find(bmtKey(note, p));
    if (it != cache.end())
        return it->second;
    const Bmt::MorphChainTaps t = bmtSteady(pair, BmtMode::Lock, p, note.f0);
    const double f = Bmt::eSubDb(t.anchorL, t.anchorR, note.f0, kBmtFs);
    cache[bmtKey(note, p)] = f;
    return f;
}

double bmtBlendESub(const std::pair<MorphState, MorphState>& pair, const BmtNote& note, float p) {
    auto& cache = bmtBlendESubCache();
    const auto it = cache.find(bmtKey(note, p));
    if (it != cache.end())
        return it->second;
    const Bmt::MorphChainTaps t = bmtSteady(pair, BmtMode::Blend, p, note.f0);
    const double e = Bmt::eSubDb(t.sumL, t.sumR, note.f0, kBmtFs);
    cache[bmtKey(note, p)] = e;
    return e;
}

struct BmtRow {
    float p = 0.0f;
    double dCx = 0.0, dNl = 0.0, dSt = 0.0, dBr = 0.0, dT = 0.0;
    double dStOut = 0.0, dTOut = 0.0;   // B10-4: the output readings, recorded
    bool dStValid = false;              // Phase 7 band validity of the core tap above 2 f_c (G8-1)
    double lK = 0.0;
    double eSub = 0.0;       ///< pre-dynamics sum tap, f0 reading (gated)
    double eSubOut = 0.0;    ///< output tap (recorded)
    double dynGain = 0.0;    ///< eSubOut - eSub (recorded)
    double fLock = 0.0;      ///< F_lock(p)
    double levelM = 0.0;     ///< L_m(p): the render's resolved anchor.level
    double floorDb = 0.0;    ///< F_lock(p) + 20 log10(L_m / L_A) - 1 dB
    double eSubAnchor = 0.0; ///< this render's own Anchor tap (recorded)
    double blendESub = 0.0;  ///< Punch only: Blend's E_sub(p)
    double gateSeconds = 0.0;
};

/// SC-008(a) against the running maximum (deviation 8): additive eps for dB / oct descriptors.
void bmtCheckMonotoneAdditive(const std::vector<BmtRow>& rows, double BmtRow::*field, const char* name, double eps) {
    double runMax = rows.front().*field;
    for (std::size_t j = 1; j < rows.size(); ++j) {
        const double d = rows[j].*field;
        INFO("SC-008(a) " << name << " at p " << rows[j].p << ": " << d << " vs running max " << runMax << " - "
                          << eps);
        CHECK(d >= runMax - eps);
        runMax = std::max(runMax, d);
    }
}

/// SC-008(a) D_t: D_t(p_j) >= 0.98 x the running maximum.
void bmtCheckMonotoneRatio(const std::vector<BmtRow>& rows, double BmtRow::*field, const char* name, double ratio) {
    double runMax = rows.front().*field;
    for (std::size_t j = 1; j < rows.size(); ++j) {
        const double d = rows[j].*field;
        INFO("SC-008(a) " << name << " at p " << rows[j].p << ": " << d << " vs " << ratio << " x running max "
                          << runMax);
        CHECK(d >= ratio * runMax);
        runMax = std::max(runMax, d);
    }
}

/// One trajectory: renders, assertions (a) - (d), the per-p table (stdout) and, when csvName is non-empty, the CSV.
void bmtRunTrajectory(BmtMode mode, const BmtNote& note, const std::vector<float>& positions,
                      const std::string& csvName) {
    enableFTZDAZ();
    REQUIRE(positions.size() >= 2);
    REQUIRE(positions.front() == 0.0f);
    REQUIRE(positions.back() == 1.0f);
    const auto pair = Bmt::showcasePair();
    const double la = static_cast<double>(pair.first.anchor.level);
    REQUIRE(la > 0.0);

    std::vector<BmtRow> rows;
    rows.reserve(positions.size());
    for (const float p : positions) {
        INFO(bmtModeName(mode) << " " << note.name << " p " << p);
        BmtRow r;
        r.p = p;

        // ---- steady render ----
        const Bmt::MorphChainTaps t = bmtSteady(pair, mode, p, note.f0);
        const BassMorphEngine::Resolved& res = t.resolvedPerInterval.back();
        const double fc = static_cast<double>(res.state.field.crossoverHz);
        r.dCx = Bmt::dCxDb(t, note.f0, kBmtFs);
        r.dNl = Bmt::dNlDb(t, note.f0, kBmtFs);
        const Bmt::DstCoreReading dstCore = Bmt::dStCore(t, fc, kBmtFs);   // B10-4: the clean core tap
        r.dSt = dstCore.db;
        r.dStValid = dstCore.valid;
        r.dStOut = Bmt::dStDb(t, fc, kBmtFs);
        r.dBr = Bmt::dBrOct(t, note.f0, kBmtFs);
        r.lK = Bmt::lKDb(t, kBmtFs);
        r.eSub = Bmt::eSubDb(t.sumL, t.sumR, note.f0, kBmtFs);
        r.eSubOut = Bmt::eSubDb(t.outL, t.outR, note.f0, kBmtFs);
        r.dynGain = r.eSubOut - r.eSub;
        r.eSubAnchor = Bmt::eSubDb(t.anchorL, t.anchorR, note.f0, kBmtFs);
        r.levelM = static_cast<double>(res.state.anchor.level);
        REQUIRE(r.levelM > 0.0);

        if (mode == BmtMode::Lock) {   // the Lock shard fills the F_lock cache from its own Anchor tap
            r.fLock = r.eSubAnchor;
            bmtFlockCache()[bmtKey(note, p)] = r.fLock;
        } else {
            r.fLock = bmtFlock(pair, note, p);
        }
        if (mode == BmtMode::Blend)
            bmtBlendESubCache()[bmtKey(note, p)] = r.eSub;
        if (mode == BmtMode::Punch)
            r.blendESub = bmtBlendESub(pair, note, p);
        r.floorDb = r.fLock + 20.0 * std::log10(r.levelM / la) - kBmtFloorDb;

        // ---- gate render (D_t) ----
        Bmt::MorphChainConfig gcfg;
        gcfg.a = pair.first;
        gcfg.b = pair.second;
        gcfg.mode = mode;
        gcfg.initialPosition = p;
        gcfg.f0 = note.f0;
        gcfg.fs = kBmtFs;
        const Bmt::MorphChainTaps g = Bmt::gateRender(gcfg);
        r.gateSeconds = static_cast<double>(g.outL.size()) / kBmtFs;
        r.dT = Bmt::dTMsSum(g, note.f0, kBmtFs);   // B10-4: the pre-dynamics sum
        r.dTOut = Bmt::dTMs(g, note.f0, kBmtFs);
        rows.push_back(r);
    }

    // ---- the per-p table (stdout) and the CSV ----
    std::ostringstream table;
    table << std::setprecision(6);
    table << "SC-008 showcase trajectory " << bmtModeName(mode) << " " << note.name << " (f0 " << note.f0
          << " Hz, fs " << kBmtFs << ")\n";
    table << "p\tD_cx\tD_nl\tD_st\tD_br\tD_t\tL_K\tE_sub\tE_sub_out\tdyn_gain\tF_lock\tL_m\tfloor\tE_sub_anchor"
             "\tE_sub_blend\tgate_s\tD_st_out\tD_t_out\tD_st_valid\n";
    std::ostringstream csv;
    csv << std::setprecision(8);
    csv << "p,D_cx,D_nl,D_st,D_br,D_t,L_K,E_sub,E_sub_out,dyn_gain,F_lock,D_st_out,D_t_out,D_st_valid\n";
    for (const BmtRow& r : rows) {
        table << r.p << '\t' << r.dCx << '\t' << r.dNl << '\t' << r.dSt << '\t' << r.dBr << '\t' << r.dT << '\t'
              << r.lK << '\t' << r.eSub << '\t' << r.eSubOut << '\t' << r.dynGain << '\t' << r.fLock << '\t'
              << r.levelM << '\t' << r.floorDb << '\t' << r.eSubAnchor << '\t'
              << (mode == BmtMode::Punch ? r.blendESub : 0.0) << '\t' << r.gateSeconds << '\t' << r.dStOut << '\t'
              << r.dTOut << '\t' << (r.dStValid ? 1 : 0) << '\n';
        csv << r.p << ',' << r.dCx << ',' << r.dNl << ',' << r.dSt << ',' << r.dBr << ',' << r.dT << ',' << r.lK
            << ',' << r.eSub << ',' << r.eSubOut << ',' << r.dynGain << ',' << r.fLock << ',' << r.dStOut << ','
            << r.dTOut << ',' << (r.dStValid ? 1 : 0) << '\n';
    }
    std::cout << table.str();
    if (!csvName.empty())
        bmtWriteEvidence(csvName, csv.str());

    INFO(table.str());

    // ---- (a) monotone within eps against the running maximum; no gate render at 12 s ----
    bmtCheckMonotoneAdditive(rows, &BmtRow::dCx, "D_cx", kBmtEpsDb);
    bmtCheckMonotoneAdditive(rows, &BmtRow::dNl, "D_nl", kBmtEpsDb);
    {   // D_st on the core tap, over the positions whose band above 2 f_c is valid (G8-1); the rest are recorded.
        std::vector<BmtRow> validRows;
        for (const BmtRow& r : rows) {
            if (r.dStValid)
                validRows.push_back(r);
            else
                std::cout << "SC-008(a) D_st at p " << r.p << ": core band above 2 f_c below -60 dBFS, recorded "
                          << r.dSt << " dB, skipped\n";
        }
        REQUIRE(validRows.size() >= 2);
        bmtCheckMonotoneAdditive(validRows, &BmtRow::dSt, "D_st", kBmtEpsDb);
    }
    bmtCheckMonotoneAdditive(rows, &BmtRow::dBr, "D_br", kBmtEpsOct);
    bmtCheckMonotoneRatio(rows, &BmtRow::dT, "D_t", kBmtDtRatioEps);
    for (const BmtRow& r : rows) {
        INFO("SC-008(a) gate render length at p " << r.p << ": " << r.gateSeconds << " s");
        CHECK(r.gateSeconds < kBmtMaxGateSeconds - 1e-9);
    }

    // ---- (b) net travel ----
    const BmtRow& r0 = rows.front();
    const BmtRow& r1 = rows.back();
    CHECK(r1.dCx - r0.dCx >= kBmtTravelDb);
    CHECK(r1.dNl - r0.dNl >= kBmtTravelDb);
    CHECK(r1.dStOut - r0.dStOut >= kBmtTravelDb);   // (b) reads the output, as SC-015 does
    CHECK(r1.dBr - r0.dBr >= kBmtTravelOct);
    REQUIRE(r0.dT > 0.0);
    CHECK(r1.dT / r0.dT >= kBmtTravelDtRatio);

    // ---- (c) no loudness dip ----
    const double lkMin = std::min(r0.lK, r1.lK);
    for (const BmtRow& r : rows) {
        INFO("SC-008(c) L_K at p " << r.p << ": " << r.lK << " vs min(L_K(0), L_K(1)) " << lkMin << " - "
                                   << kBmtDipDb);
        CHECK(r.lK >= lkMin - kBmtDipDb);
    }

    // ---- (d) floor at the resolved Anchor level; Punch vs Blend ----
    for (const BmtRow& r : rows) {
        INFO("SC-008(d) E_sub at p " << r.p << ": " << r.eSub << " vs floor " << r.floorDb << " (F_lock " << r.fLock
                                     << ", L_m " << r.levelM << ", L_A " << la << ")");
        CHECK(r.eSub >= r.floorDb);
        if (mode == BmtMode::Punch) {
            INFO("SC-008(d) Punch E_sub " << r.eSub << " vs Blend E_sub " << r.blendESub << " - "
                                          << kBmtPunchVsBlendDb);
            CHECK(r.eSub >= r.blendESub - kBmtPunchVsBlendDb);
        }
    }
}

std::string bmtCsvName(BmtMode mode, const BmtNote& note) {
    std::string m = bmtModeName(mode);
    for (char& c : m)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return "sc008-trajectory-" + m + "-" + note.name + ".csv";
}

void bmtShard(BmtMode mode, const BmtNote& note) {
    bmtRunTrajectory(mode, note, bmtGrid21(), bmtCsvName(mode, note));
}

}  // namespace

// -----------------------------------------------------------------------------
// SC-008 shards ([long]; deviation 5: the Smoke sibling below runs per push)
// -----------------------------------------------------------------------------

TEST_CASE("BassMorph_ShowcaseTrajectory_Lock_E1", "[systems][profundum][bass-morph][sc-008][long]") {
    bmtShard(BmtMode::Lock, bmtNoteE1());
}

TEST_CASE("BassMorph_ShowcaseTrajectory_Lock_C3", "[systems][profundum][bass-morph][sc-008][long]") {
    bmtShard(BmtMode::Lock, bmtNoteC3());
}

TEST_CASE("BassMorph_ShowcaseTrajectory_Blend_E1", "[systems][profundum][bass-morph][sc-008][long]") {
    bmtShard(BmtMode::Blend, bmtNoteE1());
}

TEST_CASE("BassMorph_ShowcaseTrajectory_Blend_C3", "[systems][profundum][bass-morph][sc-008][long]") {
    bmtShard(BmtMode::Blend, bmtNoteC3());
}

TEST_CASE("BassMorph_ShowcaseTrajectory_Punch_E1", "[systems][profundum][bass-morph][sc-008][long]") {
    bmtShard(BmtMode::Punch, bmtNoteE1());
}

TEST_CASE("BassMorph_ShowcaseTrajectory_Punch_C3", "[systems][profundum][bass-morph][sc-008][long]") {
    bmtShard(BmtMode::Punch, bmtNoteC3());
}

// -----------------------------------------------------------------------------
// SC-008 Smoke (per push): Blend, E1, p in {0, 0.5, 1}; (a) - (d) on those three points. No CSV.
// -----------------------------------------------------------------------------

TEST_CASE("BassMorph_ShowcaseTrajectorySmoke", "[systems][profundum][bass-morph][sc-008]") {
    bmtRunTrajectory(BmtMode::Blend, bmtNoteE1(), std::vector<float>{0.0f, 0.5f, 1.0f}, std::string{});
}

// Hidden diagnostic (main loop, 2026-10-19): SC-008(a) D_st collapse at p = 1 at C3 -- side-to-mid above 2 f_c on every
// tap (sum, dyn, dc = limiter input, stage out, out), per-channel RMS of the output, and the compressor's gain, for
// p in {0.85, 0.9, 0.95, 1} in Lock at C3 and E1.
TEST_CASE("BassMorph_TrajectoryTapProbe", "[systems][profundum][bass-morph][.]") {
    enableFTZDAZ();
    const auto pair = Bmt::showcasePair();
    struct Note { const char* name; int midi; };
    for (const Note note : {Note{"C3", 48}, Note{"E1", 28}}) {
        const double f0 = static_cast<double>(Krate::DSP::ProfundumTest::anchorMidiHz(note.midi));
        for (const float p : {0.85f, 0.9f, 0.95f, 1.0f}) {
            Bmt::MorphChainConfig cfg;
            cfg.a = pair.first;
            cfg.b = pair.second;
            cfg.mode = BmtMode::Lock;
            cfg.initialPosition = p;
            cfg.f0 = f0;
            cfg.fs = kBmtFs;
            cfg.ampEnvelope = false;
            cfg.tail.chain.perSampleDynamicsTaps = true;
            const Bmt::MorphChainTaps t = Bmt::renderMorphChain(cfg);
            const auto& res = t.resolvedPerInterval.back();
            const double fc = static_cast<double>(res.state.field.crossoverHz);
            const Bmt::ReadWindow w = Bmt::readWindow(t.outL.size(), kBmtFs);
            auto s2m = [&](const std::vector<float>& L, const std::vector<float>& R) {
                return Krate::DSP::StereoFieldTest::bandReading(L.data() + w.start, R.data() + w.start, w.length, kBmtFs, 2.0 * fc,
                                                    0.5 * kBmtFs).sideToMidDb;
            };
            auto rms = [&](const std::vector<float>& x) {
                double acc = 0.0;
                for (std::size_t i = w.start; i < w.start + w.length; ++i)
                    acc += static_cast<double>(x[i]) * static_cast<double>(x[i]);
                return 10.0 * std::log10(std::max(acc / static_cast<double>(w.length), 1e-30));
            };
            double grMin = 0.0;
            for (std::size_t i = w.start; i < std::min(w.start + w.length, t.compGrDb.size()); ++i)
                grMin = std::min(grMin, static_cast<double>(t.compGrDb[i]));
            std::cout << std::setprecision(5) << "SC008 TAP " << note.name << " p " << p << " mode "
                      << static_cast<int>(res.state.field.mode) << " spread " << res.state.field.spread << " fc " << fc
                      << " | s2m sum " << s2m(t.sumL, t.sumR) << " dyn " << s2m(t.dynL, t.dynR) << " dc "
                      << s2m(t.dcL, t.dcR) << " stage " << s2m(t.stageOutL, t.stageOutR) << " out "
                      << s2m(t.outL, t.outR) << " | out rms L " << rms(t.outL) << " R " << rms(t.outR)
                      << " | sum rms L " << rms(t.sumL) << " R " << rms(t.sumR) << " | comp GR min " << grMin
                      << " | offsets amt " << t.offsetsPerInterval.back().amount << " tone "
                      << t.offsetsPerInterval.back().tone << "\n";
        }
    }
    CHECK(true);
}
