// ==============================================================================
// Layer 3: System Tests - BassStereoField, harvest TU
//                          (Profundum Phase 7, specs/profundum-phase7-stereo-field)
// ==============================================================================
// Reference: specs/profundum-phase7-stereo-field/spec.md (FR-021, FR-041; SC-001, SC-002, SC-007, SC-010, SC-020)
//            specs/profundum-phase7-stereo-field/plan.md (S9.1 harvest, plan S1 C-13 escalation)
//            specs/profundum-phase7-stereo-field/tasks.md T013 (deviations 4, 7)
//
// The calibration harvest (hidden; run only by the main loop through evidence/close/harvest.sh, T015). Tags
// [stereo-field-harvest][.calibration] ONLY, never [profundum] (deviation 4: a tag filter that matches a hidden
// case runs it, so a [profundum]-tagged harvest would run inside every gate). Precedent:
// mass_compressor_chain_test.cpp "MassCompressor_AmountTableHarvest" ([mass-dynamics-harvest][.calibration]).
//
//   BassStereoField_WideScaleHarvest            step 1: K_wide in {1.0, 0.75, 0.5} at g_floor 0.5, WIDE dirty arm
//   BassStereoField_SideFloorHarvest_<Arm>      step 2 (14 shards): g_floor in {0.71 ... 0.1} at the committed
//                                               K_wide, the SC-001 grid at Gravity 0, depth 0; one chain render per
//                                               cell, then StereoFieldTest::restage per candidate (deviation 7)
//   BassStereoField_SideFloorLeverHarvest       step 2: SC-010's Gravity lever per candidate (restage)
//
// Lines parsed by evidence/close/aggregate-harvest.js (T015):
//   HARVEST wide-scale <K> PASS|FAIL
//   HARVEST wide-scale-chosen <K>                         (absent when no candidate passes)
//   STOP AND SURFACE: K_wide < 0.5 needed (plan C-13)     (no candidate passes)
//   HARVEST side-floor <arm> <g> PASS|FAIL <worst Q_sub dB> <worst rho_sub>
//   HARVEST side-floor-lever <g> PASS|FAIL
// Every other printed line is a record (BSF_HARVEST_..., HARVEST-CELL ...). Nothing here REQUIREs a candidate to
// pass: the harvest measures, the aggregator decides (plan S9.1 step 3: never below the escalation floor silently).
//
// Shipping FP mode (not -fno-fast-math): nothing here builds or compares a non-finite value.
// No bit-exact float goldens. TU-local names carry a `bsf` / `kBsf` prefix (tasks.md rule 6).
// ==============================================================================

#include "bass_stereo_field_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/bass_stereo_field.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using Krate::DSP::BassCharacterStage;
using Krate::DSP::BassStereoField;
using Krate::DSP::SpectralShapeRecipe;
namespace Sft = Krate::DSP::StereoFieldTest;
namespace Mdt = Krate::DSP::MassDynamicsTest;
namespace Sct = Krate::DSP::ShapeCharacterTest;
namespace Hft = Krate::DSP::HarmonicFoldTest;
using BsfAlg = BassCharacterStage::Algorithm;
using BsfMode = BassStereoField::ImageMode;
using BsfClock = std::chrono::steady_clock;

constexpr double kBsfFs = 48000.0;   // tasks.md rule 9

// plan S9.1 / plan S1 C-13: the candidate sets, in search order (largest first).
// G8-1 (2026-10-18): K_wide in {1, 0.75} (the SC-010 WIDE lever fails at 0.5); the floor candidates extend to 0.03.
constexpr std::array<float, 2> kBsfWideScaleCandidates{1.0f, 0.75f};
constexpr float kBsfStep1SideFloor = 0.5f;   // step 1 runs at the g_floor seed
constexpr std::array<float, 11> kBsfSideFloorCandidates{0.71f,  0.5f,  0.35f, 0.25f,  0.18f, 0.125f,
                                                        0.1f,   0.071f, 0.05f, 0.035f, 0.03f};

constexpr std::array<float, 3> kBsfCrossovers{60.0f, 120.0f, 250.0f};
constexpr std::array<float, 3> kBsfSpreads{0.0f, 0.5f, 1.0f};        // SC-001 grid
constexpr std::array<float, 2> kBsfWideSpreads{0.5f, 1.0f};          // step 1
constexpr std::array<float, 2> kBsfCurves{0.0f, 1.0f};               // SC-001 grid
constexpr std::array<BsfMode, 3> kBsfModes{BsfMode::Solid, BsfMode::Spread, BsfMode::Wide};
constexpr std::array<BsfAlg, 6> kBsfAlgorithms{BsfAlg::Tube, BsfAlg::Fold,  BsfAlg::Rectify,
                                               BsfAlg::Bite, BsfAlg::Crush, BsfAlg::Fracture};

constexpr double kBsfLeverSideToMidDb = 3.0;   // SC-010 static lever bar
constexpr double kBsfLeverRho = 0.1;
constexpr double kBsfAliasBarDbfs = -80.0;     // SC-007

struct BsfNote {
    const char* name;
    double hz;
};

/// tasks.md rule 9 (ShapeCharacterTest::kE0Hz ... are inline const double, not constexpr).
std::array<BsfNote, 4> bsfNotes() {
    return {{{.name = "E0", .hz = Sct::kE0Hz},
             {.name = "E1", .hz = Sct::kE1Hz},
             {.name = "E2", .hz = Sct::kE2Hz},
             {.name = "C3", .hz = Sct::kC3Hz}}};
}

/// SC-010 showcase notes (spec SC-010: E1 and C3).
std::array<BsfNote, 2> bsfLeverNotes() {
    return {{{.name = "E1", .hz = Sct::kE1Hz}, {.name = "C3", .hz = Sct::kC3Hz}}};
}

const char* bsfAlgName(BsfAlg a) {
    switch (a) {
        case BsfAlg::Tube: return "Tube";
        case BsfAlg::Fold: return "Fold";
        case BsfAlg::Rectify: return "Rectify";
        case BsfAlg::Bite: return "Bite";
        case BsfAlg::Crush: return "Crush";
        case BsfAlg::Fracture: return "Fracture";
    }
    return "?";
}

const char* bsfModeName(BsfMode m) {
    switch (m) {
        case BsfMode::Solid: return "Solid";
        case BsfMode::Spread: return "Spread";
        case BsfMode::Wide: return "Wide";
    }
    return "?";
}

// -----------------------------------------------------------------------------
// Arms (tasks.md rule 10; T009's arm list)
// -----------------------------------------------------------------------------

enum class BsfArmKind : std::uint8_t { Clean, Dirty, Anchor };

struct BsfArm {
    const char* name;
    BsfArmKind kind;
    SpectralShapeRecipe::Controls base;
    std::optional<BsfAlg> algorithm;
};

const std::array<BsfArm, 14>& bsfArms() {
    static const std::array<BsfArm, 14> arms{{
        {.name = "CleanSaw", .kind = BsfArmKind::Clean, .base = SpectralShapeRecipe::kSawAnchor, .algorithm = std::nullopt},
        {.name = "CleanGrowl", .kind = BsfArmKind::Clean, .base = SpectralShapeRecipe::kGrowl, .algorithm = std::nullopt},
        {.name = "DirtyTube", .kind = BsfArmKind::Dirty, .base = SpectralShapeRecipe::kSawAnchor, .algorithm = BsfAlg::Tube},
        {.name = "DirtyFold", .kind = BsfArmKind::Dirty, .base = SpectralShapeRecipe::kSawAnchor, .algorithm = BsfAlg::Fold},
        {.name = "DirtyRectify", .kind = BsfArmKind::Dirty, .base = SpectralShapeRecipe::kSawAnchor,
         .algorithm = BsfAlg::Rectify},
        {.name = "DirtyBite", .kind = BsfArmKind::Dirty, .base = SpectralShapeRecipe::kSawAnchor, .algorithm = BsfAlg::Bite},
        {.name = "DirtyCrush", .kind = BsfArmKind::Dirty, .base = SpectralShapeRecipe::kSawAnchor, .algorithm = BsfAlg::Crush},
        {.name = "DirtyFracture", .kind = BsfArmKind::Dirty, .base = SpectralShapeRecipe::kSawAnchor,
         .algorithm = BsfAlg::Fracture},
        {.name = "AnchorTube", .kind = BsfArmKind::Anchor, .base = SpectralShapeRecipe::kSawAnchor, .algorithm = BsfAlg::Tube},
        {.name = "AnchorFold", .kind = BsfArmKind::Anchor, .base = SpectralShapeRecipe::kSawAnchor, .algorithm = BsfAlg::Fold},
        {.name = "AnchorRectify", .kind = BsfArmKind::Anchor, .base = SpectralShapeRecipe::kSawAnchor,
         .algorithm = BsfAlg::Rectify},
        {.name = "AnchorBite", .kind = BsfArmKind::Anchor, .base = SpectralShapeRecipe::kSawAnchor, .algorithm = BsfAlg::Bite},
        {.name = "AnchorCrush", .kind = BsfArmKind::Anchor, .base = SpectralShapeRecipe::kSawAnchor,
         .algorithm = BsfAlg::Crush},
        {.name = "AnchorFracture", .kind = BsfArmKind::Anchor, .base = SpectralShapeRecipe::kSawAnchor,
         .algorithm = BsfAlg::Fracture},
    }};
    return arms;
}

BsfArm bsfArmByName(std::string_view name) {
    const std::array<BsfArm, 14>& arms = bsfArms();
    std::size_t idx = arms.size();
    for (std::size_t i = 0; i < arms.size(); ++i) {
        if (name == arms[i].name) {
            idx = i;
            break;
        }
    }
    INFO("harvest arm " << name);
    REQUIRE(idx < arms.size());
    return arms[idx];
}

BsfArm bsfDirtyArm(BsfAlg a) {
    return BsfArm{.name = bsfAlgName(a), .kind = BsfArmKind::Dirty, .base = SpectralShapeRecipe::kSawAnchor,
                  .algorithm = a};
}

/// Clean: shapeOn false, no algorithm, no Anchor, excitation off (the default). Dirty: SHAPE 1 -> Fold 100 ->
/// the algorithm at Amount 1, Tone 0.5, no Anchor. Anchor: Dirty + the Anchor (the Phase 6 feed in the chain).
/// fs 48 kHz, host block 512 (tasks.md rule 9). seconds <= 0 -> MassDynamicsTest::descriptorSeconds(fs).
Sft::FieldChainConfig bsfArmConfig(const BsfArm& arm, double f0, double seconds) {
    Sft::FieldChainConfig cfg{};
    cfg.chain = Mdt::ChainConfig{.base = arm.base, .f0 = f0, .fs = kBsfFs, .seconds = seconds};
    cfg.chain.attachAnchor = arm.kind == BsfArmKind::Anchor;
    if (arm.kind == BsfArmKind::Clean) {
        cfg.shapeOn = false;
        cfg.algorithm = std::nullopt;
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

BassStereoField::Controls bsfControls(BsfMode mode, float spread, float gravity, float crossoverHz, float curve) {
    BassStereoField::Controls c{};
    c.mode = mode;
    c.spread = spread;
    c.gravity = gravity;
    c.crossoverHz = crossoverHz;
    c.curve = curve;
    c.motionDepth = 0.0f;   // deviation 7: depth 0 (MOTION only pulls width inward, FR-030)
    return c;
}

/// SC-007's frame: the render must hold the settle plus HarmonicFoldTest::aliasFftLength(fs, f) samples
/// (f = f0 / 2 for Fracture), which exceeds descriptorSeconds at E0 (2^19 samples for Fracture).
double bsfAliasSeconds(double f0, BsfAlg a) {
    const double f = a == BsfAlg::Fracture ? 0.5 * f0 : f0;
    const std::size_t L = Hft::aliasFftLength(kBsfFs, f);
    return std::max(Mdt::descriptorSeconds(kBsfFs), Mdt::kSettleSeconds + static_cast<double>(L) / kBsfFs);
}

double bsfMs(BsfClock::time_point a, BsfClock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// -----------------------------------------------------------------------------
// Worst-so-far readings over the gated taps (monoSumCellPasses's taps)
// -----------------------------------------------------------------------------

struct BsfWorst {
    double subQ = 0.0;      ///< min Q_sub (Q <= 0 dB always, so the largest |Q_sub|) at core, stageOut, out
    double rho = 1.0;       ///< min rho_sub at core and stageOut (SC-002)
    double octQ = 0.0;      ///< min valid octave Q at core, stageOut, out (SC-001(b))
    std::size_t validSub = 0;
    std::size_t exempt = 0; ///< G8-1 anti-phase cells (recorded)
};

void bsfFoldWorst(BsfWorst& w, const Sft::MonoSumCell& cell) {
    if (cell.antiPhaseExempt)
        ++w.exempt;
    for (const Sft::MonoSumTap tap : {Sft::MonoSumTap::Core, Sft::MonoSumTap::StageOut, Sft::MonoSumTap::Out}) {
        const Sft::TapReading& tr = cell.taps[static_cast<std::size_t>(tap)];
        if (tr.sub.valid) {
            ++w.validSub;
            w.subQ = std::min(w.subQ, tr.sub.q);
            if (tap != Sft::MonoSumTap::Out)
                w.rho = std::min(w.rho, tr.sub.rho);
        }
        for (const Sft::BandReading& b : tr.octaves) {
            if (b.valid)
                w.octQ = std::min(w.octQ, b.q);
        }
    }
}

bool bsfLeverPasses(const Sft::LeverMove& m) {
    return m.sideToMidDb >= kBsfLeverSideToMidDb || m.rho >= kBsfLeverRho;
}

std::string bsfEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase7-stereo-field/evidence/harvest/";
}

/// Shards run concurrently (T015 step 4): the error_code overload never throws on a lost creation race.
std::string bsfMakeEvidenceDir() {
    const std::string dir = bsfEvidenceDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// -----------------------------------------------------------------------------
// Step 2 shard body (one per arm; deviation 7)
// -----------------------------------------------------------------------------

void bsfSideFloorShard(const char* armName) {
    std::cout << std::setprecision(6);
    const BsfArm arm = bsfArmByName(armName);
    const std::string dir = bsfMakeEvidenceDir();
    std::ofstream csv(dir + "side-floor-" + armName + ".csv");
    csv << std::setprecision(6);
    csv << "g_floor,mode,note,spread,crossover_hz,curve,pass,worst_sub_q_db,worst_rho_sub,worst_oct_q_db\n";
    std::ofstream detail(dir + "side-floor-" + armName + "-cells.csv");
    detail << std::setprecision(6);
    detail << "g_floor," << Sft::monoSumCsvHeader() << "\n";

    // K_wide is the committed constant (pasted after step 1); the render runs at the committed floor and every
    // candidate is evaluated by restage (deviation 7: the stage is the last component that depends on g_floor).
    std::cout << "HARVEST side-floor-config " << armName << " K_wide " << BassStereoField::kWideOffsetScale
              << " committed_floor " << BassStereoField::kSideFloorGain << "\n";

    std::array<std::size_t, kBsfSideFloorCandidates.size()> passCount{};
    std::array<BsfWorst, kBsfSideFloorCandidates.size()> worst{};
    std::size_t cells = 0;
    double totalMs = 0.0;
    const auto shardStart = BsfClock::now();

    for (const BsfMode mode : kBsfModes) {
        for (const BsfNote& note : bsfNotes()) {
            for (const float spread : kBsfSpreads) {
                for (const float fc : kBsfCrossovers) {
                    for (const float curve : kBsfCurves) {
                        Sft::FieldChainConfig cfg = bsfArmConfig(arm, note.hz, 0.0);
                        cfg.field = bsfControls(mode, spread, 0.0f, fc, curve);   // Gravity 0 (deviation 7)
                        std::ostringstream label;
                        label << std::setprecision(6) << armName << '_' << bsfModeName(mode) << '_' << note.name << "_s"
                              << spread << "_fc" << fc << "_c" << curve;

                        const auto t0 = BsfClock::now();
                        const Sft::FieldChainTaps taps = Sft::renderFieldChain(cfg);
                        const auto t1 = BsfClock::now();
                        for (std::size_t gi = 0; gi < kBsfSideFloorCandidates.size(); ++gi) {
                            const float g = kBsfSideFloorCandidates[gi];
                            const Sft::FieldChainTaps r = Sft::restage(taps, cfg, g);
                            const Sft::MonoSumCell cell = Sft::evaluateMonoSumCell(r, cfg);
                            const bool ok = Sft::monoSumCellPasses(cell);
                            if (ok)
                                ++passCount[gi];
                            BsfWorst cw{};
                            bsfFoldWorst(cw, cell);
                            bsfFoldWorst(worst[gi], cell);
                            csv << g << ',' << bsfModeName(mode) << ',' << note.name << ',' << spread << ',' << fc << ','
                                << curve << ',' << (ok ? 1 : 0) << ',' << cw.subQ << ',' << cw.rho << ',' << cw.octQ
                                << '\n';
                            std::istringstream lines(Sft::monoSumCsvLine(cell, label.str()));
                            std::string line;
                            while (std::getline(lines, line))
                                detail << g << ',' << line << '\n';
                        }
                        const auto t2 = BsfClock::now();
                        ++cells;
                        totalMs += bsfMs(t0, t2);
                        std::cout << "HARVEST-CELL side-floor " << label.str() << " render_ms " << bsfMs(t0, t1)
                                  << " restage_ms " << bsfMs(t1, t2) << " total_ms " << bsfMs(t0, t2) << "\n";
                    }
                }
            }
        }
    }

    std::cout << "BSF_HARVEST_FLOOR_SHARD," << armName << ",cells," << cells << ",mean_cell_ms,"
              << (cells > 0 ? totalMs / static_cast<double>(cells) : 0.0) << ",shard_ms,"
              << bsfMs(shardStart, BsfClock::now()) << "\n";
    for (std::size_t gi = 0; gi < kBsfSideFloorCandidates.size(); ++gi) {
        const float g = kBsfSideFloorCandidates[gi];
        const bool ok = passCount[gi] == cells;
        std::cout << "BSF_HARVEST_FLOOR_ROW," << armName << ",g," << g << ",pass," << passCount[gi] << ",cells," << cells
                  << ",worst_sub_q_db," << worst[gi].subQ << ",worst_rho_sub," << worst[gi].rho << ",worst_oct_q_db,"
                  << worst[gi].octQ << ",valid_sub_readings," << worst[gi].validSub << ",exempt," << worst[gi].exempt << "\n";
        std::cout << "HARVEST side-floor " << armName << ' ' << g << ' ' << (ok ? "PASS" : "FAIL") << ' '
                  << worst[gi].subQ << ' ' << worst[gi].rho << "\n";
    }
    // 3 modes x 4 notes x 3 Spread x 3 crossovers x 2 curves (the SC-001 grid at Gravity 0, depth 0).
    CHECK(cells == kBsfModes.size() * 4 * kBsfSpreads.size() * kBsfCrossovers.size() * kBsfCurves.size());
}

}  // namespace

// =============================================================================
// plan S9.1 step 1 (K_wide), plan S1 C-13. FR-041; SC-001(a), SC-002, SC-007, SC-010 (WIDE vs SPREAD).
// The WIDE dirty arm, every algorithm x Gravity 0 x crossover {60, 120, 250} x {E0, E1, E2, C3} x Spread
// {0.5, 1}, depth 0, g_floor 0.5 (sideFloorOverride), K_wide via wideScaleOverride.
// =============================================================================
TEST_CASE("BassStereoField_WideScaleHarvest", "[stereo-field-harvest][.calibration]") {
    std::cout << std::setprecision(6);
    const std::string dir = bsfMakeEvidenceDir();
    std::ofstream csv(dir + "wide-scale.csv");
    csv << std::setprecision(6);
    csv << "k_wide,algorithm,note,spread,crossover_hz,pass,worst_sub_q_db,worst_rho_sub,worst_oct_q_db,alias_l_dbfs,"
           "alias_r_dbfs,cell_ms\n";
    std::ofstream detail(dir + "wide-scale-cells.csv");
    detail << std::setprecision(6);
    detail << "k_wide," << Sft::monoSumCsvHeader() << "\n";

    // SC-010 WIDE vs SPREAD (dirty arm, Tube) on the showcase notes from Controls{}: the SPREAD reference does
    // not depend on K_wide (wideOffsets is {+0, +0} outside WIDE), so it renders once per note.
    const BsfArm leverArm = bsfDirtyArm(BsfAlg::Tube);
    const auto leverNotes = bsfLeverNotes();
    auto leverConfig = [&](double f0, BsfMode mode, float k) {
        Sft::FieldChainConfig cfg = bsfArmConfig(leverArm, f0, 0.0);
        cfg.field = BassStereoField::Controls{};
        cfg.field.mode = mode;
        cfg.sideFloorOverride = kBsfStep1SideFloor;
        cfg.wideScaleOverride = k;
        return cfg;
    };
    std::vector<Sft::FieldChainTaps> spreadRefs;
    spreadRefs.reserve(leverNotes.size());
    for (const BsfNote& note : leverNotes)
        spreadRefs.push_back(Sft::renderFieldChain(leverConfig(note.hz, BsfMode::Spread, 1.0f)));

    std::optional<float> chosen;
    for (const float k : kBsfWideScaleCandidates) {
        std::size_t pass = 0;
        std::size_t cells = 0;
        std::size_t aliasCells = 0;
        BsfWorst worst{};
        double worstAlias = -400.0;
        const auto kStart = BsfClock::now();

        for (const BsfAlg alg : kBsfAlgorithms) {
            const BsfArm arm = bsfDirtyArm(alg);
            for (const float fc : kBsfCrossovers) {
                for (const BsfNote& note : bsfNotes()) {
                    for (const float spread : kBsfWideSpreads) {
                        // SC-007: offsets at the ceilings (Spread 1) at E0 - E2, 48 kHz.
                        const bool aliasCell = spread == 1.0f && note.hz < Sct::kC3Hz;
                        Sft::FieldChainConfig cfg =
                            bsfArmConfig(arm, note.hz, aliasCell ? bsfAliasSeconds(note.hz, alg) : 0.0);
                        cfg.field = bsfControls(BsfMode::Wide, spread, 0.0f, fc, BassStereoField::Controls{}.curve);
                        cfg.sideFloorOverride = kBsfStep1SideFloor;
                        cfg.wideScaleOverride = k;

                        const auto t0 = BsfClock::now();
                        const Sft::FieldChainTaps taps = Sft::renderFieldChain(cfg);
                        const Sft::MonoSumCell cell = Sft::evaluateMonoSumCell(taps, cfg);
                        const bool ok = Sft::monoSumCellPasses(cell);
                        ++cells;
                        if (ok)
                            ++pass;
                        BsfWorst cw{};
                        bsfFoldWorst(cw, cell);
                        bsfFoldWorst(worst, cell);
                        double aliasL = 0.0;
                        double aliasR = 0.0;
                        if (aliasCell) {
                            aliasL = Sft::wideAliasedDbfs(taps.characterL, kBsfFs, note.hz, alg);
                            aliasR = Sft::wideAliasedDbfs(taps.characterR, kBsfFs, note.hz, alg);
                            worstAlias = std::max({worstAlias, aliasL, aliasR});
                            ++aliasCells;
                        }
                        const double ms = bsfMs(t0, BsfClock::now());

                        std::ostringstream label;
                        label << std::setprecision(6) << "Wide_" << bsfAlgName(alg) << '_' << note.name << "_s" << spread
                              << "_fc" << fc << "_k" << k;
                        csv << k << ',' << bsfAlgName(alg) << ',' << note.name << ',' << spread << ',' << fc << ','
                            << (ok ? 1 : 0) << ',' << cw.subQ << ',' << cw.rho << ',' << cw.octQ << ',';
                        if (aliasCell)
                            csv << aliasL << ',' << aliasR;
                        else
                            csv << "na,na";
                        csv << ',' << ms << '\n';
                        std::istringstream lines(Sft::monoSumCsvLine(cell, label.str()));
                        std::string line;
                        while (std::getline(lines, line))
                            detail << k << ',' << line << '\n';
                        std::cout << "HARVEST-CELL wide-scale " << label.str() << " pass " << (ok ? 1 : 0) << " ms " << ms
                                  << "\n";
                    }
                }
            }
        }

        // SC-010 lever: WIDE vs SPREAD at E1 and C3, staticLeverMove >= 3 dB or >= 0.1.
        bool leverOk = true;
        std::ostringstream leverText;
        leverText << std::setprecision(6);
        for (std::size_t i = 0; i < leverNotes.size(); ++i) {
            const Sft::FieldChainConfig wcfg = leverConfig(leverNotes[i].hz, BsfMode::Wide, k);
            const Sft::FieldChainTaps wide = Sft::renderFieldChain(wcfg);
            const Sft::LeverMove m =
                Sft::staticLeverMove(wide, spreadRefs[i], kBsfFs, static_cast<double>(wcfg.field.crossoverHz));
            const bool ok = bsfLeverPasses(m);
            leverOk = leverOk && ok;
            leverText << ",lever_" << leverNotes[i].name << "_s2m_db," << m.sideToMidDb << ",lever_" << leverNotes[i].name
                      << "_rho," << m.rho << ",lever_" << leverNotes[i].name << "_pass," << (ok ? 1 : 0);
        }

        const bool aliasOk = aliasCells > 0 && worstAlias <= kBsfAliasBarDbfs;
        // G8-1: the mono-sum pass count is recorded here (the floor step owns the sub band); K is chosen on SC-007
        // and the SC-010 WIDE-versus-SPREAD lever.
        const bool candidateOk = aliasOk && leverOk;
        std::cout << "BSF_HARVEST_WIDE_ROW,k," << k << ",pass," << pass << ",cells," << cells << ",worst_sub_q_db,"
                  << worst.subQ << ",worst_rho_sub," << worst.rho << ",worst_oct_q_db," << worst.octQ
                  << ",worst_alias_dbfs," << worstAlias << ",alias_cells," << aliasCells << ",exempt," << worst.exempt
                  << leverText.str() << ",candidate_ms," << bsfMs(kStart, BsfClock::now()) << "\n";
        std::cout << "HARVEST wide-scale " << k << ' ' << (candidateOk ? "PASS" : "FAIL") << "\n";
        if (candidateOk && !chosen.has_value())
            chosen = k;   // candidates run largest first: the first PASS is the largest passing K
        // 6 algorithms x 3 crossovers x 4 notes x 2 Spread.
        CHECK(cells == kBsfAlgorithms.size() * kBsfCrossovers.size() * 4 * kBsfWideSpreads.size());
    }

    if (chosen.has_value()) {
        std::cout << "HARVEST wide-scale-chosen " << *chosen << "\n";
    } else {
        // plan S9.1 step 3 / plan S1 C-13: never below the escalation floor silently.
        std::cout << "STOP AND SURFACE: K_wide < 0.75 needed (plan C-13)\n";
    }
}

// =============================================================================
// plan S9.1 step 2 (g_floor), plan S1 C-13, deviation 7. FR-021; SC-001(a)(b), SC-002, SC-020.
// 14 shards (one per arm), run concurrently by evidence/close/harvest.sh step 4.
// =============================================================================
#define BSF_SIDE_FLOOR_SHARD(Arm)                                                                         \
    TEST_CASE("BassStereoField_SideFloorHarvest_" #Arm, "[stereo-field-harvest][.calibration]") { \
        bsfSideFloorShard(#Arm);                                                                          \
    }

BSF_SIDE_FLOOR_SHARD(CleanSaw)
BSF_SIDE_FLOOR_SHARD(CleanGrowl)
BSF_SIDE_FLOOR_SHARD(DirtyTube)
BSF_SIDE_FLOOR_SHARD(DirtyFold)
BSF_SIDE_FLOOR_SHARD(DirtyRectify)
BSF_SIDE_FLOOR_SHARD(DirtyBite)
BSF_SIDE_FLOOR_SHARD(DirtyCrush)
BSF_SIDE_FLOOR_SHARD(DirtyFracture)
BSF_SIDE_FLOOR_SHARD(AnchorTube)
BSF_SIDE_FLOOR_SHARD(AnchorFold)
BSF_SIDE_FLOOR_SHARD(AnchorRectify)
BSF_SIDE_FLOOR_SHARD(AnchorBite)
BSF_SIDE_FLOOR_SHARD(AnchorCrush)
BSF_SIDE_FLOOR_SHARD(AnchorFracture)

#undef BSF_SIDE_FLOOR_SHARD

// =============================================================================
// plan S9.1 step 2, SC-010's Gravity lever per g_floor candidate (restage). Showcase: SPREAD, kSawAnchor clean
// core, E1 and C3, Controls{} with Gravity 0 and 1; staticLeverMove >= 3 dB side-to-mid or >= 0.1 rho.
// =============================================================================
TEST_CASE("BassStereoField_SideFloorLeverHarvest", "[stereo-field-harvest][.calibration]") {
    std::cout << std::setprecision(6);
    const std::string dir = bsfMakeEvidenceDir();
    std::ofstream csv(dir + "side-floor-lever.csv");
    csv << std::setprecision(6);
    csv << "g_floor,note,s2m_move_db,rho_move,pass\n";

    const BsfArm arm = bsfArmByName("CleanSaw");
    const auto notes = bsfLeverNotes();
    auto gravityConfig = [&](double f0, float gravity) {
        Sft::FieldChainConfig cfg = bsfArmConfig(arm, f0, 0.0);
        cfg.field = BassStereoField::Controls{};
        cfg.field.mode = BsfMode::Spread;
        cfg.field.gravity = gravity;
        return cfg;
    };
    std::vector<Sft::FieldChainConfig> cfg0;
    std::vector<Sft::FieldChainConfig> cfg1;
    std::vector<Sft::FieldChainTaps> g0;
    std::vector<Sft::FieldChainTaps> g1;
    for (const BsfNote& note : notes) {
        cfg0.push_back(gravityConfig(note.hz, 0.0f));
        cfg1.push_back(gravityConfig(note.hz, 1.0f));
        g0.push_back(Sft::renderFieldChain(cfg0.back()));
        g1.push_back(Sft::renderFieldChain(cfg1.back()));
    }

    for (const float g : kBsfSideFloorCandidates) {
        bool allOk = true;
        std::ostringstream row;
        row << std::setprecision(6) << "BSF_HARVEST_FLOOR_LEVER,g," << g;
        for (std::size_t i = 0; i < notes.size(); ++i) {
            const Sft::FieldChainTaps a = Sft::restage(g0[i], cfg0[i], g);
            const Sft::FieldChainTaps b = Sft::restage(g1[i], cfg1[i], g);
            const Sft::LeverMove m = Sft::staticLeverMove(a, b, kBsfFs, static_cast<double>(cfg0[i].field.crossoverHz));
            const bool ok = bsfLeverPasses(m);
            // G8-1 refinement: the Gravity lever is gated at E1 and recorded at C3 (no harmonic of C3 lies below
            // f_s = 240 Hz except the centred h1, so Gravity has no band to act on there).
            if (std::string(notes[i].name) == "E1")
                allOk = allOk && ok;
            csv << g << ',' << notes[i].name << ',' << m.sideToMidDb << ',' << m.rho << ',' << (ok ? 1 : 0) << '\n';
            row << ',' << notes[i].name << "_s2m_db," << m.sideToMidDb << ',' << notes[i].name << "_rho," << m.rho;
        }
        std::cout << row.str() << "\n";
        std::cout << "HARVEST side-floor-lever " << g << ' ' << (allOk ? "PASS" : "FAIL") << "\n";
    }
}
