// ==============================================================================
// Layer 3: System Tests - BassStereoField, mono-sum TU
//                          (Profundum Phase 7, specs/profundum-phase7-stereo-field)
// ==============================================================================
// Reference: specs/profundum-phase7-stereo-field/spec.md (SC-001, SC-002, SC-020 rendered; FR-021, FR-077)
//            specs/profundum-phase7-stereo-field/plan.md (SC-001 / SC-002 / SC-020 rows)
//            specs/profundum-phase7-stereo-field/tasks.md T009 (deviations 3, 6, 8, 11)
//
// T009:
//   BassStereoField_MonoSumPerBand_<Mode>_<Arm>_<Note>  [long][sc-001][sc-002]  168 shards, one macro line each
//       over bsfMsRunShard: Spread {0, .5, 1} x Gravity {0, .5, 1} x crossover {60, 120, 250} x curve {0, 1} x
//       MOTION (depth 0 | depth 1 Drift | depth 1 Cycle), rate 1 = 162 cells. Gated per cell by
//       StereoFieldTest::monoSumCellPasses (SC-001(a)(b) at core / stageOut / out, SC-002 at core / stageOut).
//       Every tap is recorded (FR-077). WIDE shards with a character stage also record the characterL/R
//       reading against the same cell re-rendered in SPREAD (it isolates the WIDE offsets).
//       The spec's BassStereoField_SubBandCorrelation is this shard set, tag [sc-002] (deviation 11).
//   BassStereoField_MonoSumPerBandSmoke                 per-push, two cells (harvest-dependent)
//   BassStereoField_GravityFloor_<Arm>                  [long][sc-020] 14 shards: Gravity 0, Spread 1, crossover
//       {60, 120, 250}, every note, SPREAD and WIDE: SC-001(a), SC-002 and A(B) > 0 dB
//   BassStereoField_GravityFloorSmoke                   per-push: DirtyRectify E1 crossover 120 WIDE
//   BassStereoField_PolyMonoSumRecord                   [long][sc-001] record only (Edge case POLY, Phase 9)
//
// Evidence: evidence/sc001-grid/<case>.csv (one line per cell and tap; WIDE dirty / Anchor shards also
// <case>-wide-vs-spread.csv), evidence/sc020-floor/<case>.csv, evidence/sc001-poly.csv.
// ==============================================================================

#include "bass_stereo_field_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/bass_stereo_field.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Krate::DSP::BassCharacterStage;
using Krate::DSP::BassStereoField;
namespace Sft = Krate::DSP::StereoFieldTest;
namespace Mdt = Krate::DSP::MassDynamicsTest;
namespace Sct = Krate::DSP::ShapeCharacterTest;

using BsfMsAlgorithm = BassCharacterStage::Algorithm;
using BsfMsMode = BassStereoField::ImageMode;

constexpr double kBsfMsFs = 48000.0;
constexpr double kBsfMsMotionSeconds = 61.0;   ///< depth 1 cells (tasks.md T009)

// ---- arms (spec descriptor block "Fixtures") ----

enum class BsfMsArmKind : std::uint8_t { Clean, Dirty, Anchor };

enum class BsfMsArmId : std::uint8_t {
    CleanSaw,
    CleanGrowl,
    DirtyTube,
    DirtyFold,
    DirtyRectify,
    DirtyBite,
    DirtyCrush,
    DirtyFracture,
    AnchorTube,
    AnchorFold,
    AnchorRectify,
    AnchorBite,
    AnchorCrush,
    AnchorFracture
};

struct BsfMsArm {
    const char* name = "";
    BsfMsArmKind kind = BsfMsArmKind::Clean;
    bool growl = false;                                ///< clean arm on kGrowl (else kSawAnchor)
    BsfMsAlgorithm algorithm = BsfMsAlgorithm::Tube;   ///< dirty / Anchor arms only
};

BsfMsArm bsfMsArm(BsfMsArmId id) {
    switch (id) {
        case BsfMsArmId::CleanSaw: return {"CleanSaw", BsfMsArmKind::Clean, false, BsfMsAlgorithm::Tube};
        case BsfMsArmId::CleanGrowl: return {"CleanGrowl", BsfMsArmKind::Clean, true, BsfMsAlgorithm::Tube};
        case BsfMsArmId::DirtyTube: return {"DirtyTube", BsfMsArmKind::Dirty, false, BsfMsAlgorithm::Tube};
        case BsfMsArmId::DirtyFold: return {"DirtyFold", BsfMsArmKind::Dirty, false, BsfMsAlgorithm::Fold};
        case BsfMsArmId::DirtyRectify:
            return {"DirtyRectify", BsfMsArmKind::Dirty, false, BsfMsAlgorithm::Rectify};
        case BsfMsArmId::DirtyBite: return {"DirtyBite", BsfMsArmKind::Dirty, false, BsfMsAlgorithm::Bite};
        case BsfMsArmId::DirtyCrush: return {"DirtyCrush", BsfMsArmKind::Dirty, false, BsfMsAlgorithm::Crush};
        case BsfMsArmId::DirtyFracture:
            return {"DirtyFracture", BsfMsArmKind::Dirty, false, BsfMsAlgorithm::Fracture};
        case BsfMsArmId::AnchorTube: return {"AnchorTube", BsfMsArmKind::Anchor, false, BsfMsAlgorithm::Tube};
        case BsfMsArmId::AnchorFold: return {"AnchorFold", BsfMsArmKind::Anchor, false, BsfMsAlgorithm::Fold};
        case BsfMsArmId::AnchorRectify:
            return {"AnchorRectify", BsfMsArmKind::Anchor, false, BsfMsAlgorithm::Rectify};
        case BsfMsArmId::AnchorBite: return {"AnchorBite", BsfMsArmKind::Anchor, false, BsfMsAlgorithm::Bite};
        case BsfMsArmId::AnchorCrush:
            return {"AnchorCrush", BsfMsArmKind::Anchor, false, BsfMsAlgorithm::Crush};
        case BsfMsArmId::AnchorFracture:
            return {"AnchorFracture", BsfMsArmKind::Anchor, false, BsfMsAlgorithm::Fracture};
    }
    return {"CleanSaw", BsfMsArmKind::Clean, false, BsfMsAlgorithm::Tube};
}

// ---- notes (spec descriptor block "Notes"; ShapeCharacterTest::noteHz, 440 * 2^((m - 69) / 12)) ----

enum class BsfMsNote : std::uint8_t { E0, E1, E2, C3 };

double bsfMsNoteHz(BsfMsNote n) {
    switch (n) {
        case BsfMsNote::E0: return Sct::kE0Hz;
        case BsfMsNote::E1: return Sct::kE1Hz;
        case BsfMsNote::E2: return Sct::kE2Hz;
        case BsfMsNote::C3: return Sct::kC3Hz;
    }
    return Sct::kE1Hz;
}

const char* bsfMsNoteName(BsfMsNote n) {
    switch (n) {
        case BsfMsNote::E0: return "E0";
        case BsfMsNote::E1: return "E1";
        case BsfMsNote::E2: return "E2";
        case BsfMsNote::C3: return "C3";
    }
    return "E1";
}

const char* bsfMsModeName(BsfMsMode m) {
    switch (m) {
        case BsfMsMode::Solid: return "SOLID";
        case BsfMsMode::Spread: return "SPREAD";
        case BsfMsMode::Wide: return "WIDE";
    }
    return "SPREAD";
}

// ---- one cell ----

/// The MOTION column of the grid: depth 0, depth 1 Drift, depth 1 Cycle (rate 1 throughout).
enum class BsfMsMotion : std::uint8_t { Off, Drift, Cycle };

struct BsfMsCellSpec {
    BsfMsMode mode = BsfMsMode::Spread;
    float spread = 1.0f;
    float gravity = 0.0f;
    float crossoverHz = 120.0f;
    float curve = 0.0f;
    BsfMsMotion motion = BsfMsMotion::Off;
};

BassStereoField::Controls bsfMsControls(const BsfMsCellSpec& s) {
    BassStereoField::Controls c{};
    c.mode = s.mode;
    c.spread = s.spread;
    c.gravity = s.gravity;
    c.crossoverHz = s.crossoverHz;
    c.curve = s.curve;
    c.motionRate = 1.0f;
    c.motionDepth = s.motion == BsfMsMotion::Off ? 0.0f : 1.0f;
    c.motionSource =
        s.motion == BsfMsMotion::Cycle ? BassStereoField::MotionSource::Cycle : BassStereoField::MotionSource::Drift;
    return c;
}

/// Clean: no ShapeMorph, no character stage, no Anchor, excitation off (kDefaultExcitation, mix 0).
/// Dirty: kSawAnchor -> SHAPE 1 -> Fold 100 -> the algorithm at Amount 1, Tone 0.5, no Anchor.
/// Anchor: Dirty + the Anchor. In WIDE the chain hands the field's own wideOffsets (renderFieldChain step 6).
/// Depth 0 renders MassDynamicsTest::descriptorSeconds(fs), depth 1 renders 61 s.
Sft::FieldChainConfig bsfMsConfig(const BsfMsArm& arm, double f0, const BsfMsCellSpec& s) {
    const double seconds = s.motion == BsfMsMotion::Off ? Mdt::descriptorSeconds(kBsfMsFs) : kBsfMsMotionSeconds;
    Sft::FieldChainConfig cfg{};
    cfg.chain = Mdt::ChainConfig{.base = arm.growl ? Krate::DSP::SpectralShapeRecipe::kGrowl
                                                   : Krate::DSP::SpectralShapeRecipe::kSawAnchor,
                                 .f0 = f0,
                                 .fs = kBsfMsFs,
                                 .seconds = seconds};
    cfg.chain.attachAnchor = arm.kind == BsfMsArmKind::Anchor;
    cfg.field = bsfMsControls(s);
    if (arm.kind == BsfMsArmKind::Clean) {
        cfg.shapeOn = false;
    } else {
        cfg.shapeOn = true;
        cfg.shape = 1.0f;
        cfg.foldAmount = 100.0f;
        cfg.algorithm = arm.algorithm;
        cfg.characterAmount = 1.0f;
        cfg.characterTone = 0.5f;
    }
    return cfg;
}

std::string bsfMsLabel(const BsfMsArm& arm, const char* note, const BsfMsCellSpec& s) {
    static constexpr std::array<const char*, 3> kMotionNames{"d0", "d1drift", "d1cycle"};
    std::ostringstream os;
    os << bsfMsModeName(s.mode) << '_' << arm.name << '_' << note << "_s" << s.spread << "_g" << s.gravity << "_fc"
       << s.crossoverHz << "_c" << s.curve << '_' << kMotionNames[static_cast<std::size_t>(s.motion)];
    return os.str();
}

std::string bsfMsEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase7-stereo-field/evidence/";
}

std::ofstream bsfMsOpenCsv(const std::string& subdir, const std::string& file, const std::string& header) {
    const std::string dir = bsfMsEvidenceDir() + subdir;
    std::filesystem::create_directories(dir);
    std::ofstream csv(dir + file);
    REQUIRE(csv.is_open());
    csv << header << "\n";
    return csv;
}

/// The worst valid octave Q of a tap (0 when no octave is valid); validCount = the valid octaves.
double bsfMsWorstOctaveQ(const Sft::TapReading& tr, std::size_t& validCount) {
    double worst = 0.0;
    validCount = 0;
    for (const Sft::BandReading& b : tr.octaves) {
        if (!b.valid)
            continue;
        worst = validCount == 0 ? b.q : std::min(worst, b.q);
        ++validCount;
    }
    return worst;
}

/// SC-001(a) and SC-002 only (SC-020: "SC-001(a) and SC-002 as above"): |Q_sub| <= 1 dB at core, stageOut and
/// out; rho_sub >= 0.99 at core and stageOut; an invalid sub band is not read (as monoSumCellPasses).
bool bsfMsSubPasses(const Sft::MonoSumCell& cell) {
    if (cell.antiPhaseExempt || !cell.subHarmonicInBand)   // G8-1 / G10-2: recorded, not gated
        return true;
    bool ok = true;
    // G8-1: the generator output and the safety-stage output are gated; the chain output is recorded.
    for (const Sft::MonoSumTap tap : {Sft::MonoSumTap::Core, Sft::MonoSumTap::StageOut}) {
        const Sft::TapReading& tr = cell.taps[static_cast<std::size_t>(tap)];
        if (!tr.sub.valid)
            continue;
        if (!(std::abs(tr.sub.q) <= Sft::kSubBandQBarDb))
            ok = false;
        if (!(tr.sub.rho >= Sft::kSubBandRhoBar))
            ok = false;
    }
    return ok;
}

// ---- shard bookkeeping: the worst-Q and worst-rho cell per shard ----

struct BsfMsWorst {
    double subAbsQ = -1.0;
    std::string subAbsQAt = "none";
    double octaveQ = 1.0;
    std::string octaveQAt = "none";
    double subRho = 2.0;
    std::string subRhoAt = "none";
    std::size_t cells = 0;
    std::size_t failingCells = 0;
    std::size_t cellsWithValidSub = 0;      ///< at stageOut
    std::size_t cellsWithValidOctave = 0;   ///< at stageOut
};

void bsfMsTrack(BsfMsWorst& w, const Sft::MonoSumCell& cell, const std::string& label, bool passed) {
    ++w.cells;
    if (!passed)
        ++w.failingCells;
    for (const Sft::MonoSumTap tap : {Sft::MonoSumTap::Core, Sft::MonoSumTap::StageOut, Sft::MonoSumTap::Out}) {
        const auto ti = static_cast<std::size_t>(tap);
        const Sft::TapReading& tr = cell.taps[ti];
        const std::string at = label + "@" + Sft::kMonoSumTapNames[ti];
        if (tr.sub.valid && std::abs(tr.sub.q) > w.subAbsQ) {
            w.subAbsQ = std::abs(tr.sub.q);
            w.subAbsQAt = at;
        }
        if (tap != Sft::MonoSumTap::Out && tr.sub.valid && tr.sub.rho < w.subRho) {
            w.subRho = tr.sub.rho;
            w.subRhoAt = at;
        }
        for (std::size_t b = 0; b < tr.octaves.size(); ++b) {
            if (tr.octaves[b].valid && tr.octaves[b].q < w.octaveQ) {
                w.octaveQ = tr.octaves[b].q;
                std::ostringstream os;
                os << at << "@oct" << cell.bands[b].lo;
                w.octaveQAt = os.str();
            }
        }
    }
    const Sft::TapReading& so = cell.taps[static_cast<std::size_t>(Sft::MonoSumTap::StageOut)];
    if (so.sub.valid)
        ++w.cellsWithValidSub;
    std::size_t validOct = 0;
    (void)bsfMsWorstOctaveQ(so, validOct);
    if (validOct > 0)
        ++w.cellsWithValidOctave;
}

void bsfMsPrintWorst(const std::string& name, const BsfMsWorst& w) {
    std::cout << std::setprecision(6) << "BSF_SC001_WORST," << name << ",cells," << w.cells << ",failing,"
              << w.failingCells << ",worst_abs_q_sub_db," << w.subAbsQ << ',' << w.subAbsQAt << ",worst_oct_q_db,"
              << w.octaveQ << ',' << w.octaveQAt << ",worst_rho_sub," << w.subRho << ',' << w.subRhoAt
              << ",cells_valid_sub," << w.cellsWithValidSub << ",cells_valid_oct," << w.cellsWithValidOctave << "\n";
}

std::string bsfMsWideDeltaHeader() {
    return "cell,tap,fc_hz,d_q_sub_db,d_rho_sub,d_s2m_sub_db,d_oct_worst_q_db,wide_q_sub_db,spread_q_sub_db,"
           "wide_oct_worst_q_db,spread_oct_worst_q_db";
}

/// FR-077 WIDE-offset record: the characterL/R reading of the WIDE cell minus the same cell in SPREAD.
std::string bsfMsWideDeltaLine(const std::string& label, const Sft::MonoSumCell& wide,
                               const Sft::MonoSumCell& spread) {
    const auto ci = static_cast<std::size_t>(Sft::MonoSumTap::Character);
    const Sft::TapReading& w = wide.taps[ci];
    const Sft::TapReading& s = spread.taps[ci];
    std::size_t wv = 0;
    std::size_t sv = 0;
    const double wOct = bsfMsWorstOctaveQ(w, wv);
    const double sOct = bsfMsWorstOctaveQ(s, sv);
    std::ostringstream os;
    os << std::setprecision(6) << label << ",character_wide_minus_spread," << wide.crossoverHz << ','
       << (w.sub.q - s.sub.q) << ',' << (w.sub.rho - s.sub.rho) << ',' << (w.sub.sideToMidDb - s.sub.sideToMidDb)
       << ',' << (wOct - sOct) << ',' << w.sub.q << ',' << s.sub.q << ',' << wOct << ',' << sOct;
    return os.str();
}

/// Render one cell, record every tap (CSV + stdout), gate it with monoSumCellPasses, track the worst. In WIDE
/// with a character stage and deltaCsv set, re-render the cell in SPREAD and record the characterL/R difference.
void bsfMsGateCell(const std::string& name, const BsfMsArm& arm, BsfMsNote note, const BsfMsCellSpec& s,
                   std::ofstream& csv, std::ofstream* deltaCsv, BsfMsWorst& worst) {
    const double f0 = bsfMsNoteHz(note);
    const std::string label = bsfMsLabel(arm, bsfMsNoteName(note), s);
    const Sft::FieldChainConfig cfg = bsfMsConfig(arm, f0, s);
    Sft::MonoSumCell cell;
    {
        const Sft::FieldChainTaps taps = Sft::renderFieldChain(cfg);   // scoped: a 61 s render's taps are large
        cell = Sft::evaluateMonoSumCell(taps, cfg);
    }
    const std::string line = Sft::monoSumCsvLine(cell, label);
    csv << line << "\n";
    std::cout << "BSF_SC001," << name << "\n" << line << "\n";

    if (deltaCsv != nullptr && s.mode == BsfMsMode::Wide && arm.kind != BsfMsArmKind::Clean) {
        BsfMsCellSpec sp = s;
        sp.mode = BsfMsMode::Spread;
        const Sft::FieldChainConfig cfgS = bsfMsConfig(arm, f0, sp);
        Sft::MonoSumCell cellS;
        {
            const Sft::FieldChainTaps tapsS = Sft::renderFieldChain(cfgS);
            cellS = Sft::evaluateMonoSumCell(tapsS, cfgS);
        }
        const std::string dl = bsfMsWideDeltaLine(label, cell, cellS);
        *deltaCsv << dl << "\n";
        std::cout << "BSF_SC001_WIDE_VS_SPREAD," << dl << "\n";
    }

    const bool passed = Sft::monoSumCellPasses(cell);
    if (cell.antiPhaseExempt)
        std::cout << "BSF_SC001_EXEMPT," << label << " (G8-1: character-tap sub band anti-correlated; recorded)\n";
    bsfMsTrack(worst, cell, label, passed);
    INFO(name << " cell " << label << "\n" << Sft::monoSumCsvHeader() << "\n" << line);
    CHECK(passed);
}

/// One SC-001 / SC-002 shard (tasks.md T009): 3 x 3 x 3 x 2 x 3 = 162 cells, no SECTIONs.
void bsfMsRunShard(BsfMsMode mode, BsfMsArmId armId, BsfMsNote note, const std::string& name) {
    const BsfMsArm arm = bsfMsArm(armId);
    std::ofstream csv = bsfMsOpenCsv("sc001-grid/", name + ".csv", Sft::monoSumCsvHeader());
    const bool recordDelta = mode == BsfMsMode::Wide && arm.kind != BsfMsArmKind::Clean;
    std::ofstream delta;
    if (recordDelta)
        delta = bsfMsOpenCsv("sc001-grid/", name + "-wide-vs-spread.csv", bsfMsWideDeltaHeader());
    BsfMsWorst worst;
    for (const float spread : {0.0f, 0.5f, 1.0f}) {
        for (const float gravity : {0.0f, 0.5f, 1.0f}) {
            for (const float fc : {60.0f, 120.0f, 250.0f}) {
                for (const float curve : {0.0f, 1.0f}) {
                    for (const BsfMsMotion motion : {BsfMsMotion::Off, BsfMsMotion::Drift, BsfMsMotion::Cycle}) {
                        const BsfMsCellSpec s{.mode = mode,
                                              .spread = spread,
                                              .gravity = gravity,
                                              .crossoverHz = fc,
                                              .curve = curve,
                                              .motion = motion};
                        bsfMsGateCell(name, arm, note, s, csv, recordDelta ? &delta : nullptr, worst);
                    }
                }
            }
        }
    }
    bsfMsPrintWorst(name, worst);
    REQUIRE(worst.cells == 162);
    // Non-vacuity: the 250 Hz cells hold a harmonic below f_c at every note (C3 = 130.8 Hz), and every cell
    // has octave bands above f_c with content.
    REQUIRE(worst.cellsWithValidSub > 0);
    REQUIRE(worst.cellsWithValidOctave > 0);
    REQUIRE(worst.failingCells == 0);
}

// ---- SC-020 rendered ----

/// SC-006's measure: the side energy in B = [20 Hz, f_c) at dynL/R (the stage input) minus at stageOutL/R, one
/// window over [kSettleSeconds, end) of the same render (deviation 8: Parseval over the masked bins).
struct BsfMsSideAttenuation {
    double inDbfs = Sft::kFloorDb;
    double outDbfs = Sft::kFloorDb;
    double aDb = 0.0;
    bool valid = false;   ///< the input side band >= -100 dBFS
};

double bsfMsSideMeanEnergy(const Sft::BandEnergies& e) {
    const double side4 = std::max(e.ll + e.rr - 2.0 * e.lr, 0.0);   // sum |L - R|^2 = 4 sum S^2
    return e.sumW2 > 0.0 ? 0.25 * side4 / e.sumW2 : 0.0;
}

BsfMsSideAttenuation bsfMsSideAttenuation(const Sft::FieldChainTaps& t, double fs, double fc) {
    const auto settle = static_cast<std::size_t>(std::llround(Mdt::kSettleSeconds * fs));
    REQUIRE(t.dynL.size() > settle);
    const std::size_t n = t.dynL.size() - settle;
    const double sIn =
        bsfMsSideMeanEnergy(Sft::bandEnergies(t.dynL.data() + settle, t.dynR.data() + settle, n, fs, 20.0, fc));
    const double sOut = bsfMsSideMeanEnergy(
        Sft::bandEnergies(t.stageOutL.data() + settle, t.stageOutR.data() + settle, n, fs, 20.0, fc));
    BsfMsSideAttenuation a;
    a.inDbfs = Krate::DSP::ProfundumTest::safeDb10(sIn);
    a.outDbfs = Krate::DSP::ProfundumTest::safeDb10(sOut);
    a.aDb = a.inDbfs - a.outDbfs;
    a.valid = sIn >= Sft::kValidMeanEnergy;
    return a;
}

/// Prints the committed floor and the harvest commit line(s) of evidence/harvest-wide-floor.txt (T013 / T015).
void bsfMsPrintHarvestProvenance(const std::string& name) {
    std::cout << std::setprecision(6) << "BSF_SC020," << name << ",committed_kSideFloorGain,"
              << BassStereoField::kSideFloorGain << ",committed_kWideOffsetScale,"
              << BassStereoField::kWideOffsetScale << ",sideGainForGravity(0),"
              << BassStereoField::sideGainForGravity(0.0f) << "\n";
    std::ifstream in(bsfMsEvidenceDir() + "harvest-wide-floor.txt");
    if (!in.is_open()) {
        std::cout << "BSF_SC020," << name << ",harvest,evidence/harvest-wide-floor.txt absent (T013 / T015 not run)\n";
        return;
    }
    std::string ln;
    std::size_t printed = 0;
    while (std::getline(in, ln)) {
        std::string lower = ln;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (lower.find("commit") != std::string::npos || lower.find("chosen") != std::string::npos) {
            std::cout << "BSF_SC020," << name << ",harvest," << ln << "\n";
            ++printed;
        }
    }
    if (printed == 0)
        std::cout << "BSF_SC020," << name << ",harvest,no commit / chosen line in evidence/harvest-wide-floor.txt\n";
}

/// SC-020 at Gravity 0 and Spread 1 (curve at Controls{}, depth 0): SC-001(a) and SC-002 (bsfMsSubPasses) and
/// A(B) > 0 dB on every cell whose input side band is valid.
void bsfMsRunGravityFloor(BsfMsArmId armId, std::span<const BsfMsMode> modes, std::span<const float> crossovers,
                          std::span<const BsfMsNote> notes, const std::string& name) {
    const BsfMsArm arm = bsfMsArm(armId);
    bsfMsPrintHarvestProvenance(name);
    std::ofstream csv = bsfMsOpenCsv("sc020-floor/", name + ".csv",
                                     Sft::monoSumCsvHeader() + ",side_in_dbfs,side_out_dbfs,a_b_db,a_b_valid,sub_passes");
    std::size_t cells = 0;
    std::size_t validA = 0;
    std::size_t failing = 0;
    double worstA = 1e300;
    std::string worstAAt = "none";
    for (const BsfMsMode mode : modes) {
        for (const BsfMsNote note : notes) {
            for (const float fc : crossovers) {
                BsfMsCellSpec s{};
                s.mode = mode;
                s.spread = 1.0f;
                s.gravity = 0.0f;
                s.crossoverHz = fc;
                s.curve = BassStereoField::Controls{}.curve;
                s.motion = BsfMsMotion::Off;
                const std::string label = bsfMsLabel(arm, bsfMsNoteName(note), s);
                const Sft::FieldChainConfig cfg = bsfMsConfig(arm, bsfMsNoteHz(note), s);
                const Sft::FieldChainTaps taps = Sft::renderFieldChain(cfg);
                const Sft::MonoSumCell cell = Sft::evaluateMonoSumCell(taps, cfg);
                const BsfMsSideAttenuation a = bsfMsSideAttenuation(taps, cfg.chain.fs, static_cast<double>(fc));
                const bool subOk = bsfMsSubPasses(cell);
                const bool aOk = !a.valid || a.aDb > 0.0;
                ++cells;
                if (a.valid) {
                    ++validA;
                    if (a.aDb < worstA) {
                        worstA = a.aDb;
                        worstAAt = label;
                    }
                }
                if (!subOk || !aOk)
                    ++failing;

                std::ostringstream extra;
                extra << std::setprecision(6) << ',' << a.inDbfs << ',' << a.outDbfs << ',' << a.aDb << ','
                      << (a.valid ? 1 : 0) << ',' << (subOk ? 1 : 0);
                const std::string line = Sft::monoSumCsvLine(cell, label);
                std::istringstream tapLines(line);   // one line per tap, each with the cell's A(B) columns
                std::string l;
                while (std::getline(tapLines, l))
                    csv << l << extra.str() << "\n";
                std::cout << "BSF_SC020," << name << "\n" << line << "\nBSF_SC020_A," << label << extra.str() << "\n";

                INFO(name << " cell " << label << "\n" << Sft::monoSumCsvHeader() << "\n" << line << "\nA(B) "
                          << a.aDb << " dB (in " << a.inDbfs << " dBFS, out " << a.outDbfs << " dBFS, valid "
                          << a.valid << ")");
                CHECK(subOk);
                CHECK(aOk);
            }
        }
    }
    std::cout << std::setprecision(6) << "BSF_SC020_WORST," << name << ",cells," << cells << ",cells_valid_a,"
              << validA << ",worst_a_db," << worstA << ',' << worstAAt << ",failing," << failing << "\n";
    REQUIRE(cells == modes.size() * notes.size() * crossovers.size());
    // Non-vacuity: a character stage makes side content below f_c at Spread 1. The clean arms' law keeps the
    // sub centred, so their A(B) may be unreadable in every cell (recorded above, not gated).
    if (arm.kind != BsfMsArmKind::Clean)
        REQUIRE(validA > 0);
    REQUIRE(failing == 0);
}

constexpr std::array<BsfMsMode, 2> kBsfMsFloorModes{BsfMsMode::Spread, BsfMsMode::Wide};
constexpr std::array<float, 3> kBsfMsCrossovers{60.0f, 120.0f, 250.0f};
constexpr std::array<BsfMsNote, 4> kBsfMsNotes{BsfMsNote::E0, BsfMsNote::E1, BsfMsNote::E2, BsfMsNote::C3};

bool bsfMsAllFinite(const std::vector<float>& x) {
    return std::all_of(x.begin(), x.end(), [](float v) { return Krate::DSP::detail::isFinite(v); });
}

}  // namespace

// ==============================================================================
// SC-001 / SC-002 per-push smoke (deviation 6). Harvest-dependent: it reads the committed kSideFloorGain /
// kWideOffsetScale and may be red on the seeds until T015.
// ==============================================================================

TEST_CASE("BassStereoField_MonoSumPerBandSmoke", "[systems][profundum][stereo-field][sc-001][sc-002]") {
    const std::string name = "BassStereoField_MonoSumPerBandSmoke";
    std::ofstream csv = bsfMsOpenCsv("sc001-grid/", name + ".csv", Sft::monoSumCsvHeader());
    BsfMsWorst worst;
    const BsfMsCellSpec a{.mode = BsfMsMode::Spread,
                          .spread = 1.0f,
                          .gravity = 0.0f,
                          .crossoverHz = 120.0f,
                          .curve = 0.0f,
                          .motion = BsfMsMotion::Off};
    bsfMsGateCell(name, bsfMsArm(BsfMsArmId::DirtyRectify), BsfMsNote::E1, a, csv, nullptr, worst);
    const BsfMsCellSpec b{.mode = BsfMsMode::Wide,
                          .spread = 1.0f,
                          .gravity = 0.0f,
                          .crossoverHz = 60.0f,
                          .curve = 1.0f,
                          .motion = BsfMsMotion::Off};
    bsfMsGateCell(name, bsfMsArm(BsfMsArmId::AnchorFracture), BsfMsNote::E0, b, csv, nullptr, worst);
    bsfMsPrintWorst(name, worst);
    REQUIRE(worst.cells == 2);
    REQUIRE(worst.cellsWithValidSub == 2);   // h1 below f_c in both cells (E1 < 120 Hz, E0 < 60 Hz)
    REQUIRE(worst.cellsWithValidOctave == 2);
    REQUIRE(worst.failingCells == 0);
}

// ==============================================================================
// SC-001 / SC-002 [long] shards: 3 modes x 14 arms x 4 notes = 168 (main-loop work, T019's lane)
// ==============================================================================

#define BSF_MONOSUM_SHARD(Mode, Arm, Note)                                          \
    TEST_CASE("BassStereoField_MonoSumPerBand_" #Mode "_" #Arm "_" #Note,           \
              "[systems][profundum][stereo-field][long][sc-001][sc-002]") {         \
        bsfMsRunShard(BsfMsMode::Mode, BsfMsArmId::Arm, BsfMsNote::Note,            \
                      "BassStereoField_MonoSumPerBand_" #Mode "_" #Arm "_" #Note);  \
    }

BSF_MONOSUM_SHARD(Solid, CleanSaw, E0)
BSF_MONOSUM_SHARD(Solid, CleanSaw, E1)
BSF_MONOSUM_SHARD(Solid, CleanSaw, E2)
BSF_MONOSUM_SHARD(Solid, CleanSaw, C3)
BSF_MONOSUM_SHARD(Solid, CleanGrowl, E0)
BSF_MONOSUM_SHARD(Solid, CleanGrowl, E1)
BSF_MONOSUM_SHARD(Solid, CleanGrowl, E2)
BSF_MONOSUM_SHARD(Solid, CleanGrowl, C3)
BSF_MONOSUM_SHARD(Solid, DirtyTube, E0)
BSF_MONOSUM_SHARD(Solid, DirtyTube, E1)
BSF_MONOSUM_SHARD(Solid, DirtyTube, E2)
BSF_MONOSUM_SHARD(Solid, DirtyTube, C3)
BSF_MONOSUM_SHARD(Solid, DirtyFold, E0)
BSF_MONOSUM_SHARD(Solid, DirtyFold, E1)
BSF_MONOSUM_SHARD(Solid, DirtyFold, E2)
BSF_MONOSUM_SHARD(Solid, DirtyFold, C3)
BSF_MONOSUM_SHARD(Solid, DirtyRectify, E0)
BSF_MONOSUM_SHARD(Solid, DirtyRectify, E1)
BSF_MONOSUM_SHARD(Solid, DirtyRectify, E2)
BSF_MONOSUM_SHARD(Solid, DirtyRectify, C3)
BSF_MONOSUM_SHARD(Solid, DirtyBite, E0)
BSF_MONOSUM_SHARD(Solid, DirtyBite, E1)
BSF_MONOSUM_SHARD(Solid, DirtyBite, E2)
BSF_MONOSUM_SHARD(Solid, DirtyBite, C3)
BSF_MONOSUM_SHARD(Solid, DirtyCrush, E0)
BSF_MONOSUM_SHARD(Solid, DirtyCrush, E1)
BSF_MONOSUM_SHARD(Solid, DirtyCrush, E2)
BSF_MONOSUM_SHARD(Solid, DirtyCrush, C3)
BSF_MONOSUM_SHARD(Solid, DirtyFracture, E0)
BSF_MONOSUM_SHARD(Solid, DirtyFracture, E1)
BSF_MONOSUM_SHARD(Solid, DirtyFracture, E2)
BSF_MONOSUM_SHARD(Solid, DirtyFracture, C3)
BSF_MONOSUM_SHARD(Solid, AnchorTube, E0)
BSF_MONOSUM_SHARD(Solid, AnchorTube, E1)
BSF_MONOSUM_SHARD(Solid, AnchorTube, E2)
BSF_MONOSUM_SHARD(Solid, AnchorTube, C3)
BSF_MONOSUM_SHARD(Solid, AnchorFold, E0)
BSF_MONOSUM_SHARD(Solid, AnchorFold, E1)
BSF_MONOSUM_SHARD(Solid, AnchorFold, E2)
BSF_MONOSUM_SHARD(Solid, AnchorFold, C3)
BSF_MONOSUM_SHARD(Solid, AnchorRectify, E0)
BSF_MONOSUM_SHARD(Solid, AnchorRectify, E1)
BSF_MONOSUM_SHARD(Solid, AnchorRectify, E2)
BSF_MONOSUM_SHARD(Solid, AnchorRectify, C3)
BSF_MONOSUM_SHARD(Solid, AnchorBite, E0)
BSF_MONOSUM_SHARD(Solid, AnchorBite, E1)
BSF_MONOSUM_SHARD(Solid, AnchorBite, E2)
BSF_MONOSUM_SHARD(Solid, AnchorBite, C3)
BSF_MONOSUM_SHARD(Solid, AnchorCrush, E0)
BSF_MONOSUM_SHARD(Solid, AnchorCrush, E1)
BSF_MONOSUM_SHARD(Solid, AnchorCrush, E2)
BSF_MONOSUM_SHARD(Solid, AnchorCrush, C3)
BSF_MONOSUM_SHARD(Solid, AnchorFracture, E0)
BSF_MONOSUM_SHARD(Solid, AnchorFracture, E1)
BSF_MONOSUM_SHARD(Solid, AnchorFracture, E2)
BSF_MONOSUM_SHARD(Solid, AnchorFracture, C3)
BSF_MONOSUM_SHARD(Spread, CleanSaw, E0)
BSF_MONOSUM_SHARD(Spread, CleanSaw, E1)
BSF_MONOSUM_SHARD(Spread, CleanSaw, E2)
BSF_MONOSUM_SHARD(Spread, CleanSaw, C3)
BSF_MONOSUM_SHARD(Spread, CleanGrowl, E0)
BSF_MONOSUM_SHARD(Spread, CleanGrowl, E1)
BSF_MONOSUM_SHARD(Spread, CleanGrowl, E2)
BSF_MONOSUM_SHARD(Spread, CleanGrowl, C3)
BSF_MONOSUM_SHARD(Spread, DirtyTube, E0)
BSF_MONOSUM_SHARD(Spread, DirtyTube, E1)
BSF_MONOSUM_SHARD(Spread, DirtyTube, E2)
BSF_MONOSUM_SHARD(Spread, DirtyTube, C3)
BSF_MONOSUM_SHARD(Spread, DirtyFold, E0)
BSF_MONOSUM_SHARD(Spread, DirtyFold, E1)
BSF_MONOSUM_SHARD(Spread, DirtyFold, E2)
BSF_MONOSUM_SHARD(Spread, DirtyFold, C3)
BSF_MONOSUM_SHARD(Spread, DirtyRectify, E0)
BSF_MONOSUM_SHARD(Spread, DirtyRectify, E1)
BSF_MONOSUM_SHARD(Spread, DirtyRectify, E2)
BSF_MONOSUM_SHARD(Spread, DirtyRectify, C3)
BSF_MONOSUM_SHARD(Spread, DirtyBite, E0)
BSF_MONOSUM_SHARD(Spread, DirtyBite, E1)
BSF_MONOSUM_SHARD(Spread, DirtyBite, E2)
BSF_MONOSUM_SHARD(Spread, DirtyBite, C3)
BSF_MONOSUM_SHARD(Spread, DirtyCrush, E0)
BSF_MONOSUM_SHARD(Spread, DirtyCrush, E1)
BSF_MONOSUM_SHARD(Spread, DirtyCrush, E2)
BSF_MONOSUM_SHARD(Spread, DirtyCrush, C3)
BSF_MONOSUM_SHARD(Spread, DirtyFracture, E0)
BSF_MONOSUM_SHARD(Spread, DirtyFracture, E1)
BSF_MONOSUM_SHARD(Spread, DirtyFracture, E2)
BSF_MONOSUM_SHARD(Spread, DirtyFracture, C3)
BSF_MONOSUM_SHARD(Spread, AnchorTube, E0)
BSF_MONOSUM_SHARD(Spread, AnchorTube, E1)
BSF_MONOSUM_SHARD(Spread, AnchorTube, E2)
BSF_MONOSUM_SHARD(Spread, AnchorTube, C3)
BSF_MONOSUM_SHARD(Spread, AnchorFold, E0)
BSF_MONOSUM_SHARD(Spread, AnchorFold, E1)
BSF_MONOSUM_SHARD(Spread, AnchorFold, E2)
BSF_MONOSUM_SHARD(Spread, AnchorFold, C3)
BSF_MONOSUM_SHARD(Spread, AnchorRectify, E0)
BSF_MONOSUM_SHARD(Spread, AnchorRectify, E1)
BSF_MONOSUM_SHARD(Spread, AnchorRectify, E2)
BSF_MONOSUM_SHARD(Spread, AnchorRectify, C3)
BSF_MONOSUM_SHARD(Spread, AnchorBite, E0)
BSF_MONOSUM_SHARD(Spread, AnchorBite, E1)
BSF_MONOSUM_SHARD(Spread, AnchorBite, E2)
BSF_MONOSUM_SHARD(Spread, AnchorBite, C3)
BSF_MONOSUM_SHARD(Spread, AnchorCrush, E0)
BSF_MONOSUM_SHARD(Spread, AnchorCrush, E1)
BSF_MONOSUM_SHARD(Spread, AnchorCrush, E2)
BSF_MONOSUM_SHARD(Spread, AnchorCrush, C3)
BSF_MONOSUM_SHARD(Spread, AnchorFracture, E0)
BSF_MONOSUM_SHARD(Spread, AnchorFracture, E1)
BSF_MONOSUM_SHARD(Spread, AnchorFracture, E2)
BSF_MONOSUM_SHARD(Spread, AnchorFracture, C3)
BSF_MONOSUM_SHARD(Wide, CleanSaw, E0)
BSF_MONOSUM_SHARD(Wide, CleanSaw, E1)
BSF_MONOSUM_SHARD(Wide, CleanSaw, E2)
BSF_MONOSUM_SHARD(Wide, CleanSaw, C3)
BSF_MONOSUM_SHARD(Wide, CleanGrowl, E0)
BSF_MONOSUM_SHARD(Wide, CleanGrowl, E1)
BSF_MONOSUM_SHARD(Wide, CleanGrowl, E2)
BSF_MONOSUM_SHARD(Wide, CleanGrowl, C3)
BSF_MONOSUM_SHARD(Wide, DirtyTube, E0)
BSF_MONOSUM_SHARD(Wide, DirtyTube, E1)
BSF_MONOSUM_SHARD(Wide, DirtyTube, E2)
BSF_MONOSUM_SHARD(Wide, DirtyTube, C3)
BSF_MONOSUM_SHARD(Wide, DirtyFold, E0)
BSF_MONOSUM_SHARD(Wide, DirtyFold, E1)
BSF_MONOSUM_SHARD(Wide, DirtyFold, E2)
BSF_MONOSUM_SHARD(Wide, DirtyFold, C3)
BSF_MONOSUM_SHARD(Wide, DirtyRectify, E0)
BSF_MONOSUM_SHARD(Wide, DirtyRectify, E1)
BSF_MONOSUM_SHARD(Wide, DirtyRectify, E2)
BSF_MONOSUM_SHARD(Wide, DirtyRectify, C3)
BSF_MONOSUM_SHARD(Wide, DirtyBite, E0)
BSF_MONOSUM_SHARD(Wide, DirtyBite, E1)
BSF_MONOSUM_SHARD(Wide, DirtyBite, E2)
BSF_MONOSUM_SHARD(Wide, DirtyBite, C3)
BSF_MONOSUM_SHARD(Wide, DirtyCrush, E0)
BSF_MONOSUM_SHARD(Wide, DirtyCrush, E1)
BSF_MONOSUM_SHARD(Wide, DirtyCrush, E2)
BSF_MONOSUM_SHARD(Wide, DirtyCrush, C3)
BSF_MONOSUM_SHARD(Wide, DirtyFracture, E0)
BSF_MONOSUM_SHARD(Wide, DirtyFracture, E1)
BSF_MONOSUM_SHARD(Wide, DirtyFracture, E2)
BSF_MONOSUM_SHARD(Wide, DirtyFracture, C3)
BSF_MONOSUM_SHARD(Wide, AnchorTube, E0)
BSF_MONOSUM_SHARD(Wide, AnchorTube, E1)
BSF_MONOSUM_SHARD(Wide, AnchorTube, E2)
BSF_MONOSUM_SHARD(Wide, AnchorTube, C3)
BSF_MONOSUM_SHARD(Wide, AnchorFold, E0)
BSF_MONOSUM_SHARD(Wide, AnchorFold, E1)
BSF_MONOSUM_SHARD(Wide, AnchorFold, E2)
BSF_MONOSUM_SHARD(Wide, AnchorFold, C3)
BSF_MONOSUM_SHARD(Wide, AnchorRectify, E0)
BSF_MONOSUM_SHARD(Wide, AnchorRectify, E1)
BSF_MONOSUM_SHARD(Wide, AnchorRectify, E2)
BSF_MONOSUM_SHARD(Wide, AnchorRectify, C3)
BSF_MONOSUM_SHARD(Wide, AnchorBite, E0)
BSF_MONOSUM_SHARD(Wide, AnchorBite, E1)
BSF_MONOSUM_SHARD(Wide, AnchorBite, E2)
BSF_MONOSUM_SHARD(Wide, AnchorBite, C3)
BSF_MONOSUM_SHARD(Wide, AnchorCrush, E0)
BSF_MONOSUM_SHARD(Wide, AnchorCrush, E1)
BSF_MONOSUM_SHARD(Wide, AnchorCrush, E2)
BSF_MONOSUM_SHARD(Wide, AnchorCrush, C3)
BSF_MONOSUM_SHARD(Wide, AnchorFracture, E0)
BSF_MONOSUM_SHARD(Wide, AnchorFracture, E1)
BSF_MONOSUM_SHARD(Wide, AnchorFracture, E2)
BSF_MONOSUM_SHARD(Wide, AnchorFracture, C3)
#undef BSF_MONOSUM_SHARD

// ==============================================================================
// SC-020 rendered: GravityFloor shards (14, SPREAD and WIDE inside each) and the per-push smoke
// ==============================================================================

#define BSF_GRAVITY_FLOOR_SHARD(Arm)                                                                   \
    TEST_CASE("BassStereoField_GravityFloor_" #Arm, "[systems][profundum][stereo-field][long][sc-020]") { \
        bsfMsRunGravityFloor(BsfMsArmId::Arm, kBsfMsFloorModes, kBsfMsCrossovers, kBsfMsNotes,         \
                             "BassStereoField_GravityFloor_" #Arm);                                    \
    }

BSF_GRAVITY_FLOOR_SHARD(CleanSaw)
BSF_GRAVITY_FLOOR_SHARD(CleanGrowl)
BSF_GRAVITY_FLOOR_SHARD(DirtyTube)
BSF_GRAVITY_FLOOR_SHARD(DirtyFold)
BSF_GRAVITY_FLOOR_SHARD(DirtyRectify)
BSF_GRAVITY_FLOOR_SHARD(DirtyBite)
BSF_GRAVITY_FLOOR_SHARD(DirtyCrush)
BSF_GRAVITY_FLOOR_SHARD(DirtyFracture)
BSF_GRAVITY_FLOOR_SHARD(AnchorTube)
BSF_GRAVITY_FLOOR_SHARD(AnchorFold)
BSF_GRAVITY_FLOOR_SHARD(AnchorRectify)
BSF_GRAVITY_FLOOR_SHARD(AnchorBite)
BSF_GRAVITY_FLOOR_SHARD(AnchorCrush)
BSF_GRAVITY_FLOOR_SHARD(AnchorFracture)
#undef BSF_GRAVITY_FLOOR_SHARD

// Harvest-dependent (reads the committed kSideFloorGain / kWideOffsetScale).
TEST_CASE("BassStereoField_GravityFloorSmoke", "[systems][profundum][stereo-field][sc-020]") {
    constexpr std::array<BsfMsMode, 1> modes{BsfMsMode::Wide};
    constexpr std::array<float, 1> crossovers{120.0f};
    constexpr std::array<BsfMsNote, 1> notes{BsfMsNote::E1};
    bsfMsRunGravityFloor(BsfMsArmId::DirtyRectify, modes, crossovers, notes, "BassStereoField_GravityFloorSmoke");
}

// ==============================================================================
// Edge case POLY (record only, for Phase 9): renderFieldPoly at {E1, E2, C3, A2}, SPREAD and WIDE, Tube (dirty
// arm with and without the Anchor), at Controls{} and at the worst case Spread 1 / Gravity 0. No REQUIRE
// beyond finiteness.
// ==============================================================================

TEST_CASE("BassStereoField_PolyMonoSumRecord", "[systems][profundum][stereo-field][long][sc-001]") {
    const std::array<double, 4> f0s{Sct::kE1Hz, Sct::kE2Hz, Sct::kC3Hz, Sct::noteHz(45)};   // A2 = 110 Hz
    std::ofstream csv = bsfMsOpenCsv("", "sc001-poly.csv", Sft::monoSumCsvHeader());
    const BassStereoField::Controls defaults{};
    for (const BsfMsMode mode : {BsfMsMode::Spread, BsfMsMode::Wide}) {
        for (const BsfMsArmId armId : {BsfMsArmId::DirtyTube, BsfMsArmId::AnchorTube}) {
            for (const bool worstCase : {false, true}) {
                const BsfMsArm arm = bsfMsArm(armId);
                BsfMsCellSpec s{};
                s.mode = mode;
                s.spread = worstCase ? 1.0f : defaults.spread;
                s.gravity = worstCase ? 0.0f : defaults.gravity;
                s.crossoverHz = defaults.crossoverHz;
                s.curve = defaults.curve;
                s.motion = BsfMsMotion::Off;
                const std::string label = std::string("POLY_E1E2C3A2_") + bsfMsLabel(arm, "poly", s);
                const Sft::FieldChainConfig cfg = bsfMsConfig(arm, f0s[0], s);
                const Sft::FieldChainTaps taps =
                    Sft::renderFieldPoly(std::span<const double>(f0s.data(), f0s.size()), cfg);
                INFO(label);
                REQUIRE(bsfMsAllFinite(taps.stageOutL));
                REQUIRE(bsfMsAllFinite(taps.stageOutR));
                REQUIRE(bsfMsAllFinite(taps.outL));
                REQUIRE(bsfMsAllFinite(taps.outR));
                const Sft::MonoSumCell cell = Sft::evaluateMonoSumCell(taps, cfg);
                const std::string line = Sft::monoSumCsvLine(cell, label);
                csv << line << "\n";
                const Sft::TapReading& so = cell.taps[static_cast<std::size_t>(Sft::MonoSumTap::StageOut)];
                std::size_t validOct = 0;
                const double worstOct = bsfMsWorstOctaveQ(so, validOct);
                std::cout << std::setprecision(6) << "BSF_SC001_POLY," << label << ",stageOut,sub_valid,"
                          << (so.sub.valid ? 1 : 0) << ",q_sub_db," << so.sub.q << ",rho_sub," << so.sub.rho
                          << ",s2m_sub_db," << so.sub.sideToMidDb << ",oct_valid," << validOct
                          << ",oct_worst_q_db," << worstOct << "\n"
                          << line << "\n";
            }
        }
    }
}
