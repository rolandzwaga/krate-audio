// ==============================================================================
// Profundum Phase 6 (specs/profundum-phase6-mass-dynamics)
// Mass law spectral grids: SC-001(b)(c), SC-003; the law harvest (plan S8.2)
// ==============================================================================
// tasks.md T014 (FR-024, FR-028; plan S1 C-1, C-7, S8.2, S13.2, S13.3, R-6; deviations 6, 10). Tags
// [systems][profundum][mass-law]; the grids add [long] (each with a non-long ...Smoke sibling), the
// harvest is hidden ([.calibration]) and run only by the main loop in T019.
//
//   SC-001(b) MassLaw_RenderedTargetsMonotonic_<Target>_<Note> [long]  Target x {E1, C3} (12 shards)
//             MassLaw_RenderedTargetsMonotonicSmoke                    seven targets at E1, Mass {0, 50, 100}
//   SC-001(c) MassLaw_FundamentalStrengthJointSweep_<Fixture>_<Note> [long]  {Ref, Saw, Sine} x 4 notes
//             MassLaw_FundamentalStrengthJointSweepSmoke              Ref at E1 (expected red until T019)
//   SC-003    MassLaw_LevelSlopeBounded_<Fixture>_<Note> [long]       {Ref, Saw, Sine} x 4 notes
//             MassLaw_LevelSlopeBoundedSmoke                          Ref at E2 and C3 (expected red until T019)
//   plan S8.2 MassLaw_TableHarvest [.calibration]                     writes evidence/mass-law-table.txt
//
// Conventions (tasks.md rules 8, 9; plan S13.2): fs 48 kHz, host block 512, the validation chain of
// mass_dynamics_test_helpers.h (renderChain), renders of descriptorSeconds(fs) = 1 s + 262144 samples,
// spectral descriptors and P / L on the last TestUtils::kLowFrequencyFftSize samples. Fixtures: Ref =
// the Mass reference voice (ProfundumCore at kDefaultControls, the stored base = Mass 50), Saw =
// kSawAnchor, Sine = kSineAnchor; the non-recipe law fields are the MassLaw::Targets defaults. Every
// value is printed with std::setprecision(6); the [long] shards also write one CSV each under
// specs/profundum-phase6-mass-dynamics/evidence/.
//
// Repeat noise (plan S13.2): epsilon per cell and descriptor = max |delta| (max - min) over 8 note-on
// offsets x host blocks {64, 512}, capped at 0.1 dB. The offset k * round(P / 8) is rounded to the
// nearest multiple of 32 because renderChain REQUIREs noteOnSample on the 32-sample grid
// (mass_dynamics_test_helpers.h:35 of renderChain; T011 decision 1). No SC-001(b) descriptor is a
// centroid, so the 0.005 oct cap has no row in this TU.
//
// Descriptors in dB: w1 = 10 log10 w_1, w1 / sum(w), E_low and THD with the describe() formulas
// (shape_character_test_helpers.h:107-150) on ProfundumTest::renderedHarmonicPowers of the frame, so
// the I / C / L FFT passes describe() also runs are skipped; D_tr by TransientShaperTest::dTrDb with
// the window dTrWindow(fs, 250) from note-on; Delta L_step by MassDynamicsTest::deltaLStepDb on the
// two-level step (kSawAnchor, preDynamicsGainDb -12 dB then 0 dB at 1 s + 131072 samples, joined by
// the twoLevelStep 0.5 ms raised cosine); P = TestUtils::kWeightedLevelDb, L = whole-period RMS
// (TransientShaperTest::levelDbPeriods) of the same frame.
// ==============================================================================

#include "mass_dynamics_test_helpers.h"

#include <k_weighting.h>

#include <krate/dsp/core/modulation_types.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/processors/transient_shaper.h>
#include <krate/dsp/systems/mass_law.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace Krate::DSP;

namespace {

namespace mlsMd = Krate::DSP::MassDynamicsTest;
namespace mlsTs = Krate::DSP::TransientShaperTest;
namespace mlsSc = Krate::DSP::ShapeCharacterTest;
namespace mlsPt = Krate::DSP::ProfundumTest;
namespace mlsTu = Krate::DSP::TestUtils;

constexpr double kMlsFs = 48000.0;
constexpr std::size_t kMlsHostBlock = 512;
constexpr std::size_t kMlsFrame = mlsTu::kLowFrequencyFftSize;
constexpr std::array<std::size_t, 2> kMlsRepeatBlocks{64, 512};
constexpr std::size_t kMlsRepeatOffsets = 8;
constexpr double kMlsEpsCapDb = 0.1;   // plan S13.2 repeat-noise cap (dB descriptors)
constexpr double kMlsBig = 1.0e300;

// SC-003 gate constants. RULED 2026-10-17, plan S1 C-1 (a): the K-weighted span and per-unit slope are
// gated at E2 and C3; at E0 and E1 they are recorded (the RLB high-pass under-reads the sub there).
// The unweighted 6 dB span is gated at every note. Indexed {E0, E1, E2, C3}.
constexpr std::array<bool, 4> kGateKWeightAt{false, false, true, true};
constexpr double kMlsSpanPMaxDb = 3.0;
constexpr double kMlsSlopePMaxDbPerUnit = 0.2;
constexpr double kMlsSpanLMaxDb = 6.0;
constexpr double kMlsW1FallMaxDb = 6.0;   // SC-001(c) bounded fall of the sum's w1 from Mass 0 (G8-4 ruling 2026-10-17)

// -----------------------------------------------------------------------------
// Notes and fixtures (tasks.md rules 8, 9)
// -----------------------------------------------------------------------------

struct MlsNote {
    const char* name;
    int midi;
    std::size_t index;   // into kGateKWeightAt
};

constexpr std::array<MlsNote, 4> kMlsNotes{{
    {.name = "E0", .midi = 16, .index = 0},
    {.name = "E1", .midi = 28, .index = 1},
    {.name = "E2", .midi = 40, .index = 2},
    {.name = "C3", .midi = 48, .index = 3},
}};
constexpr std::size_t kMlsE0 = 0;
constexpr std::size_t kMlsE1 = 1;
constexpr std::size_t kMlsE2 = 2;
constexpr std::size_t kMlsC3 = 3;

/// 440 * 2^((m - 69) / 12) in double (ShapeCharacterTest::noteHz).
double mlsHz(const MlsNote& note) { return mlsSc::noteHz(note.midi); }

struct MlsFixture {
    const char* name;
    SpectralShapeRecipe::Controls controls;
    bool sc003Gated = true;   ///< false: SC-003 recorded, not gated (G8-2 ruling 2026-10-17, the pure-sine fixture)
};

const std::array<MlsFixture, 3> kMlsFixtures{{
    {.name = "Ref", .controls = SpectralShapeRecipe::kDefaultControls},
    {.name = "Saw", .controls = SpectralShapeRecipe::kSawAnchor},
    // SC-003 recorded (spec SC-003 row, user ruling 2026-10-17 G8-2): the 2 f0 sidechain cannot see a
    // sub-only voice at any note, so the FR-016 makeup alone spans about 5.7 dB on this fixture.
    {.name = "Sine", .controls = SpectralShapeRecipe::kSineAnchor, .sc003Gated = false},
}};
constexpr std::size_t kMlsRef = 0;
constexpr std::size_t kMlsSaw = 1;
constexpr std::size_t kMlsSine = 2;

// -----------------------------------------------------------------------------
// Evidence and printing
// -----------------------------------------------------------------------------

std::string mlsEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase6-mass-dynamics/evidence/";
}

std::ofstream mlsOpenEvidence(const std::string& file) {
    const std::string dir = mlsEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream out(dir + file);
    REQUIRE(out.is_open());
    out << std::setprecision(6);
    return out;
}

void mlsPrint(const std::ostringstream& line) { std::cout << line.str() << '\n'; }

// -----------------------------------------------------------------------------
// Chain configuration
// -----------------------------------------------------------------------------

/// The validation chain at a static Mass (the law's slew is snapped by prepareChainLaw).
mlsMd::ChainConfig mlsConfig(const SpectralShapeRecipe::Controls& base, double f0, float mass,
                             std::size_t noteOnSample, std::size_t hostBlock) {
    mlsMd::ChainConfig cfg;
    cfg.base = base;
    cfg.f0 = f0;
    cfg.fs = kMlsFs;
    cfg.hostBlock = hostBlock;
    cfg.noteOnSample = noteOnSample;
    cfg.massAt = [mass](std::size_t) { return mass; };
    return cfg;
}

/// Repeat-noise note-on offset k: k * round(P / 8), rounded to the nearest multiple of the 32 grid.
std::size_t mlsRepeatOffset(double f0, std::size_t k) {
    const auto step = static_cast<std::size_t>(std::llround(kMlsFs / f0 / 8.0));
    return (k * step + mlsMd::kGrid / 2) / mlsMd::kGrid * mlsMd::kGrid;
}

/// max - min of measure(renderChain(make(offset, block)), cfg) over the 8 offsets x blocks {64, 512}.
template <class Make, class Measure>
double mlsRepeatSpread(const Make& make, const Measure& measure, double f0) {
    double lo = kMlsBig;
    double hi = -kMlsBig;
    for (const std::size_t block : kMlsRepeatBlocks) {
        for (std::size_t k = 0; k < kMlsRepeatOffsets; ++k) {
            const mlsMd::ChainConfig cfg = make(mlsRepeatOffset(f0, k), block);
            const mlsMd::ChainTaps taps = mlsMd::renderChain(cfg);
            const double v = measure(taps, cfg);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }
    return hi - lo;
}

// -----------------------------------------------------------------------------
// Descriptors
// -----------------------------------------------------------------------------

struct MlsSpectrum {
    double w1Db;       ///< 10 log10 w_1
    double w1FracDb;   ///< 10 log10 (w_1 / sum w)
    double elowDb;     ///< sum_{n=2..8} w / sum w
    double thdDb;      ///< sum_{n>=2} w / w_1
};

/// The describe() power formulas on the last kLowFrequencyFftSize samples of y.
MlsSpectrum mlsSpectrum(const std::vector<float>& y, double f0) {
    REQUIRE(y.size() >= kMlsFrame);
    const int N = static_cast<int>(std::ceil(0.5 * kMlsFs / f0));
    const std::vector<double> w = mlsPt::renderedHarmonicPowers(y.data() + (y.size() - kMlsFrame), kMlsFs, f0, N);
    double total = 0.0;
    double lo = 0.0;
    double hi = 0.0;
    for (std::size_t i = 0; i < w.size(); ++i) {
        total += w[i];
        const std::size_t h = i + 1;
        if (h == 1)
            continue;
        if (h <= 8)
            lo += w[i];
        else
            hi += w[i];
    }
    REQUIRE(total > 0.0);
    const double w1 = w.empty() ? 0.0 : w[0];
    return MlsSpectrum{.w1Db = mlsPt::safeDb10(w1),
                       .w1FracDb = mlsPt::safeDb10(w1 / total),
                       .elowDb = mlsPt::safeDb10(lo / total),
                       .thdDb = mlsPt::safeDb10((lo + hi) / std::max(w1, 1e-300))};
}

struct MlsLevels {
    double pDb;   ///< K-weighted level (LKFS) of the frame
    double lDb;   ///< whole-period RMS dBFS of the frame
};

MlsLevels mlsLevels(const std::vector<float>& y, double f0) {
    REQUIRE(y.size() >= kMlsFrame);
    const std::size_t n = y.size();
    return MlsLevels{.pDb = mlsTu::kWeightedLevelDb(y.data(), n, kMlsFs, n - kMlsFrame),
                     .lDb = mlsTs::levelDbPeriods(y.data() + (n - kMlsFrame), kMlsFrame, kMlsFs, f0)};
}

/// The two-level step (plan S13.3 SC-001(b)): -12 dB, then 0 dB from 1 s + 131072 samples (absolute).
constexpr std::size_t kMlsStepOffset = 131072;
constexpr std::size_t kMlsStepWindow = 65536;   // steady window at each level

std::size_t mlsStepSample() { return mlsTs::secondsToSamples(kMlsFs, 1.0) + kMlsStepOffset; }

std::function<float(std::size_t)> mlsStepGainDb() {
    const std::size_t s0 = mlsStepSample();
    const std::size_t R = mlsTs::rampSamples(kMlsFs);
    const double gLow = std::pow(10.0, -12.0 / 20.0);
    return [s0, R, gLow](std::size_t n) -> float {
        if (n < s0)
            return -12.0f;
        if (n >= s0 + R)
            return 0.0f;
        const double g = gLow + (1.0 - gLow) * mlsTs::rampWeight(n - s0, R);
        return static_cast<float>(20.0 * std::log10(g));
    };
}

/// L(high) - L(low): the last 65536 samples before the step vs the last 65536 samples of the render.
double mlsDeltaLStep(const std::vector<float>& y, double f0) {
    const std::size_t s0 = mlsStepSample();
    REQUIRE(s0 >= kMlsStepWindow);
    REQUIRE(y.size() >= s0 + kMlsStepWindow);
    return mlsMd::deltaLStepDb(std::span<const float>(y), kMlsFs, f0, s0 - kMlsStepWindow, s0,
                               y.size() - kMlsStepWindow, y.size());
}

/// D_tr from note-on, window dTrWindow(fs, 250 ms) = 375 ms, steady = the last window of the render.
double mlsDtr(const std::vector<float>& y, std::size_t onset) {
    const std::size_t W = mlsTs::dTrWindow(kMlsFs, static_cast<double>(TransientShaper::kMaxDurationMs));
    REQUIRE(y.size() >= W);
    return mlsTs::dTrDb(y.data(), y.size(), onset, y.size() - W, W);
}

// -----------------------------------------------------------------------------
// Monotonicity
// -----------------------------------------------------------------------------

struct MlsMonotone {
    std::size_t violations;   ///< steps with delta < -tol in the documented direction
    double worstDelta;        ///< smallest signed step in the documented direction
    std::size_t worstAt;      ///< index i of the worst step (i - 1 -> i)
};

MlsMonotone mlsMonotone(const std::vector<double>& v, bool rising, double tol) {
    MlsMonotone r{.violations = 0, .worstDelta = kMlsBig, .worstAt = 0};
    for (std::size_t i = 1; i < v.size(); ++i) {
        const double d = rising ? v[i] - v[i - 1] : v[i - 1] - v[i];
        if (d < r.worstDelta) {
            r.worstDelta = d;
            r.worstAt = i;
        }
        if (d < -tol)
            ++r.violations;
    }
    return r;
}

std::vector<float> mlsMassRange(std::size_t step) {
    std::vector<float> m;
    for (std::size_t i = 0; i <= 100; i += step)
        m.push_back(static_cast<float>(i));
    return m;
}

// -----------------------------------------------------------------------------
// SC-001(b): one target driven, the others held at their Mass 0 values
// -----------------------------------------------------------------------------

struct MlsTargetSpec {
    MassLaw::Target target;
    const char* name;
    const char* descriptor;
    bool rising;   ///< documented direction: true = non-decreasing in Mass
};

constexpr std::array<MlsTargetSpec, MassLaw::kNumTargets> kMlsTargets{{
    {.target = MassLaw::Target::Depth, .name = "Depth", .descriptor = "w1/sum(w) dB of outL", .rising = false},
    {.target = MassLaw::Target::Body, .name = "Body", .descriptor = "E_low dB of outL", .rising = true},
    {.target = MassLaw::Target::TransientDuration, .name = "Duration",
     .descriptor = "D_tr dB of outL (375 ms window, shaper +6 / 0 dB)", .rising = true},
    {.target = MassLaw::Target::FoldAmount, .name = "Fold", .descriptor = "THD dB of outL", .rising = true},
    {.target = MassLaw::Target::CompressionAmount, .name = "Compression",
     .descriptor = "Delta L_step dB of outL (kSawAnchor step)", .rising = false},
    {.target = MassLaw::Target::AnchorLevel, .name = "AnchorLevel", .descriptor = "w1 dB of sumL", .rising = true},
    // FR-021 output trim (G8-5 ruling 2026-10-17): non-increasing level.
    {.target = MassLaw::Target::OutputTrim, .name = "OutputTrim", .descriptor = "L dBFS of outL", .rising = false},
}};
static_assert([] {
    for (std::size_t i = 0; i < kMlsTargets.size(); ++i)
        if (static_cast<std::size_t>(kMlsTargets[i].target) != i)
            return false;
    return true;
}(), "kMlsTargets is in MassLaw::Target order");

const MlsTargetSpec& mlsTarget(MassLaw::Target t) { return kMlsTargets[static_cast<std::size_t>(t)]; }

/// massAt == 0 and all seven targets overridden: the driven target at evaluateRow(kRows[k], m, base_k),
/// every other target j at evaluateRow(kRows[j], 0, base_j). The compression row runs on the step
/// fixture (base kSawAnchor) with the per-sample dynamics taps (steady GR recorded) and the other
/// targets at their Mass 50 base values (G8-7 refinement 2026-10-17, spec SC-001(b) row): at Mass 0 the
/// Depth bias purifies the saw to a voice the 2 f0 sidechain cannot see (GR 0.03 dB at amount 1).
mlsMd::ChainConfig mlsDrivenConfig(MassLaw::Target driven, double f0, float m, std::size_t noteOn,
                                   std::size_t block) {
    const bool step = driven == MassLaw::Target::CompressionAmount;
    mlsMd::ChainConfig cfg = mlsConfig(step ? SpectralShapeRecipe::kSawAnchor : SpectralShapeRecipe::kDefaultControls,
                                       f0, MassLaw::kMinMass, noteOn, block);
    const MassLaw::Targets base = mlsMd::lawBaseFor(cfg);
    const float othersMass = step ? MassLaw::kNeutralMass : MassLaw::kMinMass;   // G8-7
    for (const MassLaw::Row& row : MassLaw::kRows) {
        const float v = MassLaw::evaluateRow(row, row.target == driven ? m : othersMass,
                                             mlsMd::targetField(base, row.target));
        cfg.targetOverride[static_cast<std::size_t>(row.target)] = [v](std::size_t) -> std::optional<float> {
            return v;
        };
    }
    if (step) {
        cfg.preDynamicsGainDb = mlsStepGainDb();
        cfg.perSampleDynamicsTaps = true;
    }
    return cfg;
}

double mlsDrivenDescriptor(MassLaw::Target driven, const mlsMd::ChainTaps& taps, const mlsMd::ChainConfig& cfg) {
    switch (driven) {
        case MassLaw::Target::Depth: return mlsSpectrum(taps.outL, cfg.f0).w1FracDb;
        case MassLaw::Target::Body: return mlsSpectrum(taps.outL, cfg.f0).elowDb;
        case MassLaw::Target::TransientDuration: return mlsDtr(taps.outL, cfg.noteOnSample);
        case MassLaw::Target::FoldAmount: return mlsSpectrum(taps.outL, cfg.f0).thdDb;
        case MassLaw::Target::CompressionAmount: return mlsDeltaLStep(taps.outL, cfg.f0);
        case MassLaw::Target::AnchorLevel: return mlsSpectrum(taps.sumL, cfg.f0).w1Db;
        case MassLaw::Target::OutputTrim: return mlsLevels(taps.outL, cfg.f0).lDb;
    }
    return 0.0;
}

/// SC-001(b) over `masses` at one note. measureEps: the repeat noise at Mass 0 and Mass 100 (capped);
/// otherwise epsilon = the cap (the Smoke rule). csvFile empty = no CSV.
void mlsRenderedTargetsRun(MassLaw::Target driven, const MlsNote& note, const std::vector<float>& masses,
                           bool measureEps, const std::string& csvFile) {
    const MlsTargetSpec& spec = mlsTarget(driven);
    const double f0 = mlsHz(note);
    const auto measure = [driven](const mlsMd::ChainTaps& t, const mlsMd::ChainConfig& c) {
        return mlsDrivenDescriptor(driven, t, c);
    };

    double rawEps = 0.0;
    if (measureEps) {
        for (const float m : {MassLaw::kMinMass, MassLaw::kMaxMass}) {
            const double s = mlsRepeatSpread(
                [&](std::size_t off, std::size_t block) { return mlsDrivenConfig(driven, f0, m, off, block); },
                measure, f0);
            std::ostringstream line;
            line << std::setprecision(6) << "SC-001(b) " << spec.name << ' ' << note.name << " repeat noise at Mass "
                 << m << ": " << s << " dB";
            mlsPrint(line);
            rawEps = std::max(rawEps, s);
        }
    }
    const double eps = measureEps ? std::min(rawEps, kMlsEpsCapDb) : kMlsEpsCapDb;

    std::optional<std::ofstream> csv;
    if (!csvFile.empty()) {
        csv.emplace(mlsOpenEvidence(csvFile));
        *csv << "target,note,mass,descriptor_db,target_value,steady_gr_low_db,steady_gr_high_db\n";
    }
    std::vector<double> v;
    v.reserve(masses.size());
    for (const float m : masses) {
        const mlsMd::ChainConfig cfg = mlsDrivenConfig(driven, f0, m, 0, kMlsHostBlock);
        const mlsMd::ChainTaps taps = mlsMd::renderChain(cfg);
        const double d = mlsDrivenDescriptor(driven, taps, cfg);
        v.push_back(d);
        const MassLaw::Targets base = mlsMd::lawBaseFor(cfg);
        const float targetValue =
            MassLaw::evaluateRow(MassLaw::kRows[static_cast<std::size_t>(driven)], m, mlsMd::targetField(base, driven));
        double grLow = 0.0;
        double grHigh = 0.0;
        if (driven == MassLaw::Target::CompressionAmount) {   // steady GR: recorded only (SC-001(b))
            const std::size_t s0 = mlsStepSample();
            grLow = mlsTs::meanOf(std::span<const float>(taps.compGrDb), s0 - kMlsStepWindow, s0);
            grHigh = mlsTs::meanOf(std::span<const float>(taps.compGrDb), taps.compGrDb.size() - kMlsStepWindow,
                                   taps.compGrDb.size());
        }
        std::ostringstream line;
        line << std::setprecision(6) << "SC-001(b) " << spec.name << ' ' << note.name << " Mass " << m << ": "
             << spec.descriptor << " = " << d << " (target " << targetValue << ")";
        if (driven == MassLaw::Target::CompressionAmount)
            line << ", steady GR low " << grLow << " dB / high " << grHigh << " dB (recorded)";
        mlsPrint(line);
        if (csv)
            *csv << spec.name << ',' << note.name << ',' << m << ',' << d << ',' << targetValue << ',' << grLow << ','
                 << grHigh << '\n';
    }

    const MlsMonotone mono = mlsMonotone(v, spec.rising, eps);
    std::ostringstream line;
    line << std::setprecision(6) << "SC-001(b) " << spec.name << ' ' << note.name << ": epsilon " << eps
         << " dB (measured " << (measureEps ? rawEps : -1.0) << ", cap " << kMlsEpsCapDb << "), direction "
         << (spec.rising ? "non-decreasing" : "non-increasing") << ", worst step " << mono.worstDelta << " dB at Mass "
         << masses[mono.worstAt] << ", violations " << mono.violations;
    mlsPrint(line);
    INFO(line.str());
    CHECK(mono.violations == 0);
}

// -----------------------------------------------------------------------------
// SC-001(c): all six targets from the law, w1 of the fold + anchor sum tap non-decreasing (spec
// SC-001(c) row, user ruling 2026-10-17 G8-2: the compressor's gain is common to every harmonic and
// SC-002's GR(100) >= 3 dB would make an output-tap w1 fall on any voice richer than the calibration one)
// -----------------------------------------------------------------------------

double mlsW1SumDb(const mlsMd::ChainTaps& taps, const mlsMd::ChainConfig& cfg) {
    return mlsSpectrum(taps.sumL, cfg.f0).w1Db;
}

void mlsJointSweepRun(const MlsFixture& fx, const MlsNote& note, const std::vector<float>& masses, bool measureEps,
                      const std::string& csvFile) {
    const double f0 = mlsHz(note);
    double rawEps = 0.0;
    if (measureEps) {   // epsilon for w1 measured at Mass 50 of this cell
        rawEps = mlsRepeatSpread(
            [&](std::size_t off, std::size_t block) {
                return mlsConfig(fx.controls, f0, MassLaw::kNeutralMass, off, block);
            },
            mlsW1SumDb, f0);
    }
    const double eps = measureEps ? std::min(rawEps, kMlsEpsCapDb) : kMlsEpsCapDb;

    std::optional<std::ofstream> csv;
    if (!csvFile.empty()) {
        csv.emplace(mlsOpenEvidence(csvFile));
        *csv << "fixture,note,mass,w1_out_db\n";
    }
    std::vector<double> v;
    v.reserve(masses.size());
    for (const float m : masses) {
        const mlsMd::ChainConfig cfg = mlsConfig(fx.controls, f0, m, 0, kMlsHostBlock);
        const mlsMd::ChainTaps taps = mlsMd::renderChain(cfg);
        const double w1 = mlsW1SumDb(taps, cfg);
        v.push_back(w1);
        std::ostringstream line;
        line << std::setprecision(6) << "SC-001(c) " << fx.name << ' ' << note.name << " Mass " << m
             << ": w1(outL) = " << w1 << " dB";
        mlsPrint(line);
        if (csv)
            *csv << fx.name << ',' << note.name << ',' << m << ',' << w1 << '\n';
    }
    // SC-001(c) as re-ruled (user ruling 2026-10-17, Clarifications "Group 8 harvest ruling" G8-4): a
    // bounded fall, w1(m) >= w1(Mass 0) - 6 dB - eps at every point; the adjacent steps are recorded.
    REQUIRE(masses.front() == 0.0f);
    const MlsMonotone mono = mlsMonotone(v, true, eps);
    double worstFall = 0.0;
    std::size_t worstFallAt = 0;
    for (std::size_t i = 1; i < v.size(); ++i) {
        const double fall = v[i] - v[0];
        if (fall < worstFall) {
            worstFall = fall;
            worstFallAt = i;
        }
    }
    std::ostringstream line;
    line << std::setprecision(6) << "SC-001(c) " << fx.name << ' ' << note.name << ": epsilon " << eps
         << " dB (measured at Mass 50 " << (measureEps ? rawEps : -1.0) << ", cap " << kMlsEpsCapDb
         << "), worst fall from Mass 0 " << worstFall << " dB at Mass " << masses[worstFallAt] << " (bar -"
         << kMlsW1FallMaxDb << " - eps), worst step " << mono.worstDelta << " dB at Mass " << masses[mono.worstAt]
         << " (recorded, G8-4; non-decreasing violations " << mono.violations << ")";
    mlsPrint(line);
    INFO(line.str());
    CHECK(worstFall >= -(kMlsW1FallMaxDb + eps));
}

// -----------------------------------------------------------------------------
// SC-003: K-weighted P and unweighted L over the Mass sweep
// -----------------------------------------------------------------------------

struct MlsSlope {
    double spanP;
    double maxStepPPerUnit;   ///< max |P(i) - P(i - 1)| / (m(i) - m(i - 1))
    double spanL;
};

MlsSlope mlsSlope(const std::vector<float>& masses, const std::vector<double>& P, const std::vector<double>& L) {
    REQUIRE(masses.size() == P.size());
    REQUIRE(masses.size() == L.size());
    REQUIRE(masses.size() >= 2);
    MlsSlope s{.spanP = *std::max_element(P.begin(), P.end()) - *std::min_element(P.begin(), P.end()),
               .maxStepPPerUnit = 0.0,
               .spanL = *std::max_element(L.begin(), L.end()) - *std::min_element(L.begin(), L.end())};
    for (std::size_t i = 1; i < P.size(); ++i) {
        const double dm = static_cast<double>(masses[i] - masses[i - 1]);
        REQUIRE(dm > 0.0);
        s.maxStepPPerUnit = std::max(s.maxStepPPerUnit, std::abs(P[i] - P[i - 1]) / dm);
    }
    return s;
}

/// The SC-003 K-weighted verdict label (C-1 (a); G8-2 fixture gating).
const char* mlsKWeightedVerdict(bool gated, bool fixtureGated) {
    if (gated)
        return "GATED";
    return fixtureGated ? "RECORDED (C-1 (a))" : "RECORDED (G8-2, sub-only fixture)";
}

/// The SC-003 span(L) verdict label of a harvest check line (G8-2 fixture gating).
const char* mlsSpanLVerdict(bool fixtureGated, bool pass) {
    if (!fixtureGated)
        return "recorded, G8-2";
    return pass ? "pass" : "FAIL";
}

void mlsLevelSlopeRun(const MlsFixture& fx, const MlsNote& note, const std::vector<float>& masses,
                      const std::string& csvFile) {
    const double f0 = mlsHz(note);
    std::optional<std::ofstream> csv;
    if (!csvFile.empty()) {
        csv.emplace(mlsOpenEvidence(csvFile));
        *csv << "fixture,note,mass,p_lkfs,l_dbfs\n";
    }
    std::vector<double> P;
    std::vector<double> L;
    for (const float m : masses) {
        const mlsMd::ChainConfig cfg = mlsConfig(fx.controls, f0, m, 0, kMlsHostBlock);
        const mlsMd::ChainTaps taps = mlsMd::renderChain(cfg);
        const MlsLevels lv = mlsLevels(taps.outL, f0);
        P.push_back(lv.pDb);
        L.push_back(lv.lDb);
        std::ostringstream line;
        line << std::setprecision(6) << "SC-003 " << fx.name << ' ' << note.name << " Mass " << m << ": P = " << lv.pDb
             << " LKFS, L = " << lv.lDb << " dBFS";
        mlsPrint(line);
        if (csv)
            *csv << fx.name << ',' << note.name << ',' << m << ',' << lv.pDb << ',' << lv.lDb << '\n';
    }
    const MlsSlope s = mlsSlope(masses, P, L);
    // K-weighted gated per note (plan S1 C-1 (a), RULED 2026-10-17); the whole fixture recorded when
    // sc003Gated is false (G8-2 ruling 2026-10-17, the pure-sine fixture).
    const bool gated = kGateKWeightAt[note.index] && fx.sc003Gated;
    std::ostringstream line;
    line << std::setprecision(6) << "SC-003 " << fx.name << ' ' << note.name << ": span(P) " << s.spanP
         << " dB (bar " << kMlsSpanPMaxDb << "), max |dP| " << s.maxStepPPerUnit << " dB/unit (bar "
         << kMlsSlopePMaxDbPerUnit << "), K-weighted "
         << mlsKWeightedVerdict(gated, fx.sc003Gated)
         << "; span(L) " << s.spanL << " dB (bar " << kMlsSpanLMaxDb << (fx.sc003Gated ? ", gated)" : ", recorded)");
    mlsPrint(line);
    INFO(line.str());
    if (gated) {
        CHECK(s.spanP <= kMlsSpanPMaxDb);
        CHECK(s.maxStepPPerUnit <= kMlsSlopePMaxDbPerUnit);
    } else {
        WARN("SC-003 " << fx.name << ' ' << note.name << " K-weighted recorded (FR-048 measurement): span(P) "
                       << s.spanP << " dB, max |dP| " << s.maxStepPPerUnit << " dB/unit");
    }
    if (fx.sc003Gated)
        CHECK(s.spanL <= kMlsSpanLMaxDb);
    else
        WARN("SC-003 " << fx.name << ' ' << note.name << " unweighted span(L) recorded (G8-2): " << s.spanL << " dB");
}

// -----------------------------------------------------------------------------
// Shard entry points
// -----------------------------------------------------------------------------

void mlsRenderedTargetsShard(MassLaw::Target t, std::size_t note) {
    const MlsTargetSpec& spec = mlsTarget(t);
    mlsRenderedTargetsRun(t, kMlsNotes[note], mlsMassRange(1), true,
                          std::string("sc001b-") + spec.name + "-" + kMlsNotes[note].name + ".csv");
}

void mlsJointSweepShard(std::size_t fixture, std::size_t note) {
    mlsJointSweepRun(kMlsFixtures[fixture], kMlsNotes[note], mlsMassRange(1), true,
                     std::string("sc001c-") + kMlsFixtures[fixture].name + "-" + kMlsNotes[note].name + ".csv");
}

void mlsLevelSlopeShard(std::size_t fixture, std::size_t note) {
    mlsLevelSlopeRun(kMlsFixtures[fixture], kMlsNotes[note], mlsMassRange(1),
                     std::string("sc003-") + kMlsFixtures[fixture].name + "-" + kMlsNotes[note].name + ".csv");
}

// -----------------------------------------------------------------------------
// Plan S8.2 law harvest (MassLaw_TableHarvest)
// -----------------------------------------------------------------------------

using MlsRows = std::array<MassLaw::Row, MassLaw::kNumTargets>;

constexpr std::size_t kMlsLatticePoints = 11;   // Mass 0, 10, ..., 100
constexpr std::size_t kMlsFinePoints = 101;     // M-1 (2026-10-18): the SC-003 |dP| gate reads every Mass unit
constexpr std::size_t kMlsMid = 5;              // Mass 50
constexpr std::size_t kMlsTop = 10;             // Mass 100
constexpr double kMlsHarvestW1TolDb = 0.1;      // adjacent w1 steps (recorded since G8-4)
constexpr double kMlsHarvestW1FallDb = 5.5;     // SC-001(c) bounded fall: 6 dB less a 0.5 dB harvest margin (G8-4)
constexpr double kMlsHarvestThdStepDb = 6.5;    // SC-002 6 dB + 0.5 dB margin
constexpr double kMlsHarvestElowStepDb = 3.5;   // SC-002 3 dB + 0.5 dB margin
constexpr double kMlsSc002Thd0MaxDb = -30.0;    // SC-002 Mass 0 THD bar (recorded beside the margins)
constexpr int kMlsBisectSteps = 12;
constexpr int kMlsMaxRounds = 3;

float mlsLatticeMass(std::size_t i) { return 10.0f * static_cast<float>(i); }

struct MlsCell {
    std::size_t fixture;
    std::size_t note;
};

struct MlsReading {
    double w1Db = 0.0;
    double thdDb = 0.0;
    double elowDb = 0.0;
    double pDb = 0.0;
    double lDb = 0.0;
    bool rendered = false;
};

using MlsCellSweep = std::array<MlsReading, kMlsLatticePoints>;

std::vector<MlsCell> mlsAllCells() {
    std::vector<MlsCell> cells;
    for (std::size_t f = 0; f < kMlsFixtures.size(); ++f)
        for (std::size_t n = 0; n < kMlsNotes.size(); ++n)
            cells.push_back(MlsCell{.fixture = f, .note = n});
    return cells;
}

/// The two taps a harvest reading needs: w1 from the fold + anchor sum (G8-2), the rest from the output.
struct MlsHarvestTaps {
    std::vector<float> sumL;
    std::vector<float> outL;
};

/// Worker body: the render only. The descriptor FFT runs on the calling thread, because
/// lowFreq::magnitudeSpectrum keeps function-local static scratch (low_frequency_metrics.h:94-134).
MlsHarvestTaps mlsHarvestRender(const MlsRows* rows, MlsCell cell, float mass) {
    mlsMd::ChainConfig cfg =
        mlsConfig(kMlsFixtures[cell.fixture].controls, mlsHz(kMlsNotes[cell.note]), mass, 0, kMlsHostBlock);
    cfg.rowsOverride = rows;
    mlsMd::ChainTaps taps = mlsMd::renderChain(cfg);
    return MlsHarvestTaps{.sumL = std::move(taps.sumL), .outL = std::move(taps.outL)};
}

MlsReading mlsReadingOf(const MlsHarvestTaps& y, double f0) {
    const MlsSpectrum sum = mlsSpectrum(y.sumL, f0);
    const MlsSpectrum s = mlsSpectrum(y.outL, f0);
    const MlsLevels lv = mlsLevels(y.outL, f0);
    return MlsReading{.w1Db = sum.w1Db, .thdDb = s.thdDb, .elowDb = s.elowDb, .pDb = lv.pDb, .lDb = lv.lDb,
                      .rendered = true};
}

/// Renders every (cell, point) of `points` with `rows` into sweeps[cell][point]: the jobs run as
/// std::async waves of hardware_concurrency() renders; each finished render is measured here.
void mlsSweepInto(const MlsRows& rows, const std::vector<MlsCell>& cells, const std::vector<std::size_t>& points,
                  std::vector<MlsCellSweep>& sweeps, std::size_t& renders) {
    REQUIRE(sweeps.size() == cells.size());
    struct Job {
        std::size_t cell;
        std::size_t point;
    };
    std::vector<Job> jobs;
    for (std::size_t c = 0; c < cells.size(); ++c)
        for (const std::size_t p : points)
            jobs.push_back(Job{.cell = c, .point = p});
    const std::size_t wave = std::max<std::size_t>(1, std::thread::hardware_concurrency());
    for (std::size_t b = 0; b < jobs.size(); b += wave) {
        const std::size_t e = std::min(jobs.size(), b + wave);
        std::vector<std::future<MlsHarvestTaps>> futures;
        futures.reserve(e - b);
        for (std::size_t i = b; i < e; ++i)
            futures.push_back(std::async(std::launch::async, mlsHarvestRender, &rows, cells[jobs[i].cell],
                                         mlsLatticeMass(jobs[i].point)));
        for (std::size_t i = b; i < e; ++i) {
            const MlsHarvestTaps y = futures[i - b].get();
            const MlsCell& c = cells[jobs[i].cell];
            sweeps[jobs[i].cell][jobs[i].point] = mlsReadingOf(y, mlsHz(kMlsNotes[c.note]));
        }
        renders += e - b;
    }
}

struct MlsDip {
    double worst;   ///< smallest w1 step (dB) over the range
    std::size_t cell;
    std::size_t at;   ///< lattice index i of the step (i - 1 -> i)
};

/// SC-001(c) as re-ruled (G8-4): the worst w1(i) − w1(0) over i in (from, to] of any cell (point 0 rendered).
MlsDip mlsWorstFallFromZero(const std::vector<MlsCellSweep>& s, std::size_t from, std::size_t to) {
    MlsDip d{.worst = 0.0, .cell = 0, .at = 0};
    for (std::size_t c = 0; c < s.size(); ++c) {
        REQUIRE(s[c][0].rendered);
        for (std::size_t i = from + 1; i <= to; ++i) {
            REQUIRE(s[c][i].rendered);
            const double fall = s[c][i].w1Db - s[c][0].w1Db;
            if (fall < d.worst)
                d = MlsDip{.worst = fall, .cell = c, .at = i};
        }
    }
    return d;
}

/// The C-7 cap on max(|amountUp|, |amountDown|) for `curve` (static_assert of mass_law.h:248-260).
float mlsCapAmount(const MassLaw::Row& r, ModCurve curve) {
    return (r.hi - r.lo) * MassLaw::kMassSweepMs / (2.0f * MassLaw::kSlewBoundRefMs) / MassLaw::maxSlope(curve);
}

bool mlsWithinCap(const MassLaw::Row& r) {
    const float a = std::max(std::abs(r.amountUp), std::abs(r.amountDown));
    return a * MassLaw::maxSlope(r.curve) <= (r.hi - r.lo) * MassLaw::kMassSweepMs / (2.0f * MassLaw::kSlewBoundRefMs);
}

bool mlsRowsEqual(const MlsRows& a, const MlsRows& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].target != b[i].target || a[i].lo != b[i].lo || a[i].hi != b[i].hi ||
            a[i].amountDown != b[i].amountDown || a[i].amountUp != b[i].amountUp || a[i].curve != b[i].curve)
            return false;
    }
    return true;
}

/// Smallest x in [lo, hi] with pass(x) (pass assumed monotone in x); nullopt when pass(hi) fails.
template <class Pass>
std::optional<float> mlsBisectSmallest(float lo, float hi, const Pass& pass) {
    if (!pass(hi))
        return std::nullopt;
    if (pass(lo))
        return lo;
    for (int s = 0; s < kMlsBisectSteps; ++s) {
        const float mid = 0.5f * (lo + hi);
        if (pass(mid))
            hi = mid;
        else
            lo = mid;
    }
    return hi;
}

/// Largest x in [lo, hi] with pass(x) (pass assumed monotone, true at small x); nullopt when pass(lo) fails.
template <class Pass>
std::optional<float> mlsBisectLargest(float lo, float hi, const Pass& pass) {
    if (pass(hi))
        return hi;
    if (!pass(lo))
        return std::nullopt;
    for (int s = 0; s < kMlsBisectSteps; ++s) {
        const float mid = 0.5f * (lo + hi);
        if (pass(mid))
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

/// The largest span(L) = max L − min L (dB) over the rendered points of any SC-003-gated cell
/// (the pure-sine fixture is recorded, G8-2).
double mlsWorstSpanL(const std::vector<MlsCellSweep>& s, const std::vector<MlsCell>& cells) {
    double worst = 0.0;
    for (std::size_t c = 0; c < s.size(); ++c) {
        if (!kMlsFixtures[cells[c].fixture].sc003Gated)
            continue;
        const MlsCellSweep& cell = s[c];
        double lo = kMlsBig;
        double hi = -kMlsBig;
        for (const MlsReading& r : cell) {
            if (!r.rendered)
                continue;
            lo = std::min(lo, r.lDb);
            hi = std::max(hi, r.lDb);
        }
        worst = std::max(worst, hi - lo);
    }
    return worst;
}

/// A float literal for the header: 9 significant digits, always a decimal point, `f` suffix.
std::string mlsFloatLiteral(float v) {
    std::ostringstream os;
    os << std::setprecision(9) << v;
    std::string s = os.str();
    if (s.find_first_of(".eE") == std::string::npos)
        s += ".0";
    return s + "f";
}

const char* mlsTargetEnumName(MassLaw::Target t) {
    switch (t) {
        case MassLaw::Target::Depth: return "Target::Depth";
        case MassLaw::Target::Body: return "Target::Body";
        case MassLaw::Target::TransientDuration: return "Target::TransientDuration";
        case MassLaw::Target::FoldAmount: return "Target::FoldAmount";
        case MassLaw::Target::CompressionAmount: return "Target::CompressionAmount";
        case MassLaw::Target::AnchorLevel: return "Target::AnchorLevel";
        case MassLaw::Target::OutputTrim: return "Target::OutputTrim";
    }
    return "Target::Depth";
}

const char* mlsCurveName(ModCurve c) {
    switch (c) {
        case ModCurve::Linear: return "ModCurve::Linear";
        case ModCurve::Exponential: return "ModCurve::Exponential";
        case ModCurve::SCurve: return "ModCurve::SCurve";
        case ModCurve::Stepped: return "ModCurve::Stepped";
    }
    return "ModCurve::Linear";
}

/// One row in the exact form of mass_law.h:98-103 (8-space indent).
std::string mlsRowLine(const MassLaw::Row& r) {
    // Designated initialisers (FR-046).
    return std::string("        {.target = ") + mlsTargetEnumName(r.target) + ", .lo = " + mlsFloatLiteral(r.lo) +
           ", .hi = " + mlsFloatLiteral(r.hi) + ", .amountDown = " + mlsFloatLiteral(r.amountDown) +
           ", .amountUp = " + mlsFloatLiteral(r.amountUp) + ", .curve = " + mlsCurveName(r.curve) + "},";
}

std::string mlsRowsSummary(const MlsRows& rows) {
    std::ostringstream os;
    os << std::setprecision(6);
    for (const MassLaw::Row& r : rows)
        os << mlsTarget(r.target).name << " down " << r.amountDown << " up " << r.amountUp << ' '
           << mlsCurveName(r.curve) << "; ";
    return os.str();
}

/// M-1 (main loop, 2026-10-18; plan S8.2 step 3): the SC-003 |dP| per Mass unit of one gated cell at
/// every Mass unit, as the MassLaw_LevelSlopeBounded shard measures it. On the 11-point lattice a
/// 1.0 dB rise over Mass 50 -> 58 read 0.10 dB per unit and the bend step never triggered, while the
/// shard read 0.2178 against 0.2.
double mlsFineSlope(const MlsRows& rows, const MlsCell& cell, std::size_t& renders) {
    const double f0 = mlsHz(kMlsNotes[cell.note]);
    std::vector<double> P(kMlsFinePoints, 0.0);
    const std::size_t wave = std::max<std::size_t>(1, std::thread::hardware_concurrency());
    for (std::size_t b = 0; b < kMlsFinePoints; b += wave) {
        const std::size_t e = std::min(kMlsFinePoints, b + wave);
        std::vector<std::future<MlsHarvestTaps>> futures;
        futures.reserve(e - b);
        for (std::size_t i = b; i < e; ++i)
            futures.push_back(std::async(std::launch::async, mlsHarvestRender, &rows, cell, static_cast<float>(i)));
        for (std::size_t i = b; i < e; ++i)
            P[i] = mlsLevels(futures[i - b].get().outL, f0).pDb;
        renders += e - b;
    }
    double slope = 0.0;
    for (std::size_t i = 1; i < kMlsFinePoints; ++i)
        slope = std::max(slope, std::abs(P[i] - P[i - 1]));
    return slope;
}

/// The full-lattice verdict of one candidate table (plan S8.2 step 3 and the checks per cell).
struct MlsVerdict {
    bool sc001c = true;        ///< every cell's w1 non-decreasing within 0.1 dB
    bool sc003L = true;        ///< every cell's span(L) <= 6 dB
    bool sc003KGated = true;   ///< span(P) <= 3 dB at the kGateKWeightAt notes
    bool slopeGated = true;    ///< |dP| <= 0.2 dB / unit at the kGateKWeightAt notes, at every Mass unit (M-1)
    bool sc002 = true;         ///< the SC-002 + 0.5 dB margins on Ref at E1 and C3
    double worstW1Step = kMlsBig;   ///< recorded (G8-4)
    double worstW1Fall = 0.0;       ///< SC-001(c): min over cells and points of w1(i) − w1(0)
    double worstSpanL = 0.0;
    double worstGatedSpanP = 0.0;
    double worstGatedSlope = 0.0;
    std::array<double, 4> spanPByNote{};   ///< max span(P) over the fixtures, per note
    std::string detail;
};

MlsVerdict mlsFullEval(const MlsRows& rows, const std::vector<MlsCell>& cells, const std::string& label,
                       std::size_t& renders) {
    std::vector<MlsCellSweep> s(cells.size());
    std::vector<std::size_t> points;
    for (std::size_t i = 0; i < kMlsLatticePoints; ++i)
        points.push_back(i);
    mlsSweepInto(rows, cells, points, s, renders);

    MlsVerdict v;
    std::ostringstream os;
    os << std::setprecision(6);
    os << "== full lattice: " << label << " (" << mlsRowsSummary(rows) << ")\n";
    for (std::size_t c = 0; c < cells.size(); ++c) {
        const MlsFixture& fx = kMlsFixtures[cells[c].fixture];
        const MlsNote& note = kMlsNotes[cells[c].note];
        double minP = kMlsBig;
        double maxP = -kMlsBig;
        double minL = kMlsBig;
        double maxL = -kMlsBig;
        double slope = 0.0;
        double dip = kMlsBig;
        double fall = 0.0;
        for (std::size_t i = 0; i < kMlsLatticePoints; ++i) {
            const MlsReading& r = s[c][i];
            os << "cell " << fx.name << ' ' << note.name << " Mass " << mlsLatticeMass(i) << ": w1 " << r.w1Db
               << " dB, THD " << r.thdDb << " dB, E_low " << r.elowDb << " dB, P " << r.pDb << " LKFS, L " << r.lDb
               << " dBFS\n";
            minP = std::min(minP, r.pDb);
            maxP = std::max(maxP, r.pDb);
            minL = std::min(minL, r.lDb);
            maxL = std::max(maxL, r.lDb);
            if (i > 0) {
                slope = std::max(slope, std::abs(r.pDb - s[c][i - 1].pDb) / 10.0);
                dip = std::min(dip, r.w1Db - s[c][i - 1].w1Db);
                fall = std::min(fall, r.w1Db - s[c][0].w1Db);
            }
        }
        const double spanP = maxP - minP;
        const double spanL = maxL - minL;
        const bool gated = kGateKWeightAt[note.index] && fx.sc003Gated;   // C-1 (a); G8-2 fixture gating
        const double fine = gated ? mlsFineSlope(rows, cells[c], renders) : slope;   // M-1
        os << "check " << fx.name << ' ' << note.name << ": SC-001(c) worst fall from Mass 0 (sum tap) " << fall << " dB ("
           << (fall >= -kMlsHarvestW1FallDb ? "pass" : "FAIL") << "; worst step " << dip << " dB recorded), SC-003 span(L) "
           << spanL << " dB ("
           << mlsSpanLVerdict(fx.sc003Gated, spanL <= kMlsSpanLMaxDb) << "), span(P) "
           << spanP << " dB, |dP| " << slope << " dB/unit on the lattice, " << fine << " dB/unit per Mass unit ("
           << (gated ? "gated, M-1" : "recorded") << ", "
           << ((spanP <= kMlsSpanPMaxDb && fine <= kMlsSlopePMaxDbPerUnit) ? "pass" : "FAIL") << ")\n";
        v.worstW1Step = std::min(v.worstW1Step, dip);
        v.worstW1Fall = std::min(v.worstW1Fall, fall);
        if (fx.sc003Gated)
            v.worstSpanL = std::max(v.worstSpanL, spanL);
        if (fx.sc003Gated)
            v.spanPByNote[note.index] = std::max(v.spanPByNote[note.index], spanP);
        if (fall < -kMlsHarvestW1FallDb)
            v.sc001c = false;
        if (fx.sc003Gated && spanL > kMlsSpanLMaxDb)
            v.sc003L = false;
        if (gated) {
            v.worstGatedSpanP = std::max(v.worstGatedSpanP, spanP);
            v.worstGatedSlope = std::max(v.worstGatedSlope, fine);
            if (spanP > kMlsSpanPMaxDb)
                v.sc003KGated = false;
            if (fine > kMlsSlopePMaxDbPerUnit)
                v.slopeGated = false;
        }
        if (cells[c].fixture == kMlsRef && (cells[c].note == kMlsE1 || cells[c].note == kMlsC3)) {
            const MlsReading& r0 = s[c][0];
            const MlsReading& r50 = s[c][kMlsMid];
            const MlsReading& r100 = s[c][kMlsTop];
            const bool a = r100.thdDb >= r50.thdDb + kMlsHarvestThdStepDb;
            const bool b = r50.thdDb >= r0.thdDb + kMlsHarvestThdStepDb;
            const bool e = r50.elowDb >= r0.elowDb + kMlsHarvestElowStepDb;
            os << "check SC-002 margins Ref " << note.name << ": THD(100) - THD(50) " << r100.thdDb - r50.thdDb
               << " dB (>= " << kMlsHarvestThdStepDb << " " << (a ? "pass" : "FAIL") << "), THD(50) - THD(0) "
               << r50.thdDb - r0.thdDb << " dB (>= " << kMlsHarvestThdStepDb << " " << (b ? "pass" : "FAIL")
               << "), E_low(50) - E_low(0) " << r50.elowDb - r0.elowDb << " dB (>= " << kMlsHarvestElowStepDb << " "
               << (e ? "pass" : "FAIL") << "), THD(0) " << r0.thdDb << " dB (SC-002 bar <= " << kMlsSc002Thd0MaxDb
               << ", recorded)\n";
            if (!(a && b && e))
                v.sc002 = false;
        }
    }
    os << "verdict " << label << ": SC-001(c) " << (v.sc001c ? "pass" : "FAIL") << " (worst fall from Mass 0 " << v.worstW1Fall << " dB; worst step " << v.worstW1Step
       << " dB), SC-003 L " << (v.sc003L ? "pass" : "FAIL") << " (worst " << v.worstSpanL << " dB), SC-003 P gated "
       << (v.sc003KGated && v.slopeGated ? "pass" : "FAIL") << " (span " << v.worstGatedSpanP << " dB, |dP| "
       << v.worstGatedSlope << " dB/unit), SC-002 margins " << (v.sc002 ? "pass" : "FAIL") << "; span(P) E0 "
       << v.spanPByNote[kMlsE0] << " dB, E1 " << v.spanPByNote[kMlsE1] << " dB (recorded, C-1 (a))\n";
    v.detail = os.str();
    return v;
}

}  // namespace

// =============================================================================
// SC-001(b): each target alone moves its descriptor monotonically
// =============================================================================

// SC-001(b) smoke (deviation 10): the six targets at E1, Mass {0, 50, 100}, epsilon = the 0.1 dB cap.
TEST_CASE("MassLaw_RenderedTargetsMonotonicSmoke", "[systems][profundum][mass-law]") {
    const std::vector<float> masses{MassLaw::kMinMass, MassLaw::kNeutralMass, MassLaw::kMaxMass};
    for (const MlsTargetSpec& spec : kMlsTargets) {
        INFO("target " << spec.name);
        mlsRenderedTargetsRun(spec.target, kMlsNotes[kMlsE1], masses, false, std::string());
    }
}

// SC-001(b): 12 shards, Target x {E1, C3}, Mass 0..100 step 1.
TEST_CASE("MassLaw_RenderedTargetsMonotonic_Depth_E1", "[systems][profundum][mass-law][long]") {
    mlsRenderedTargetsShard(MassLaw::Target::Depth, kMlsE1);
}
TEST_CASE("MassLaw_RenderedTargetsMonotonic_Depth_C3", "[systems][profundum][mass-law][long]") {
    mlsRenderedTargetsShard(MassLaw::Target::Depth, kMlsC3);
}
TEST_CASE("MassLaw_RenderedTargetsMonotonic_Body_E1", "[systems][profundum][mass-law][long]") {
    mlsRenderedTargetsShard(MassLaw::Target::Body, kMlsE1);
}
TEST_CASE("MassLaw_RenderedTargetsMonotonic_Body_C3", "[systems][profundum][mass-law][long]") {
    mlsRenderedTargetsShard(MassLaw::Target::Body, kMlsC3);
}
TEST_CASE("MassLaw_RenderedTargetsMonotonic_Duration_E1", "[systems][profundum][mass-law][long]") {
    mlsRenderedTargetsShard(MassLaw::Target::TransientDuration, kMlsE1);
}
TEST_CASE("MassLaw_RenderedTargetsMonotonic_Duration_C3", "[systems][profundum][mass-law][long]") {
    mlsRenderedTargetsShard(MassLaw::Target::TransientDuration, kMlsC3);
}
TEST_CASE("MassLaw_RenderedTargetsMonotonic_Fold_E1", "[systems][profundum][mass-law][long]") {
    mlsRenderedTargetsShard(MassLaw::Target::FoldAmount, kMlsE1);
}
TEST_CASE("MassLaw_RenderedTargetsMonotonic_Fold_C3", "[systems][profundum][mass-law][long]") {
    mlsRenderedTargetsShard(MassLaw::Target::FoldAmount, kMlsC3);
}
TEST_CASE("MassLaw_RenderedTargetsMonotonic_Compression_E1", "[systems][profundum][mass-law][long]") {
    mlsRenderedTargetsShard(MassLaw::Target::CompressionAmount, kMlsE1);
}
TEST_CASE("MassLaw_RenderedTargetsMonotonic_Compression_C3", "[systems][profundum][mass-law][long]") {
    mlsRenderedTargetsShard(MassLaw::Target::CompressionAmount, kMlsC3);
}
TEST_CASE("MassLaw_RenderedTargetsMonotonic_AnchorLevel_E1", "[systems][profundum][mass-law][long]") {
    mlsRenderedTargetsShard(MassLaw::Target::AnchorLevel, kMlsE1);
}
TEST_CASE("MassLaw_RenderedTargetsMonotonic_AnchorLevel_C3", "[systems][profundum][mass-law][long]") {
    mlsRenderedTargetsShard(MassLaw::Target::AnchorLevel, kMlsC3);
}
TEST_CASE("MassLaw_RenderedTargetsMonotonic_OutputTrim_E1", "[systems][profundum][mass-law][long]") {
    mlsRenderedTargetsShard(MassLaw::Target::OutputTrim, kMlsE1);
}
TEST_CASE("MassLaw_RenderedTargetsMonotonic_OutputTrim_C3", "[systems][profundum][mass-law][long]") {
    mlsRenderedTargetsShard(MassLaw::Target::OutputTrim, kMlsC3);
}

// =============================================================================
// SC-001(c): joint sweep, fundamental strength (FR-024)
// =============================================================================

// SC-001(c) smoke (deviation 10): Ref at E1, Mass {0, 25, 50, 75, 100}, epsilon = the cap.
// Expected red until T019 (the S8.2 seed rows; the law harvest calibrates them).
TEST_CASE("MassLaw_FundamentalStrengthJointSweepSmoke", "[systems][profundum][mass-law]") {
    mlsJointSweepRun(kMlsFixtures[kMlsRef], kMlsNotes[kMlsE1], mlsMassRange(25), false, std::string());
}

// SC-001(c): 12 shards, {Ref, Saw, Sine} x {E0, E1, E2, C3}, Mass 0..100 step 1.
TEST_CASE("MassLaw_FundamentalStrengthJointSweep_Ref_E0", "[systems][profundum][mass-law][long]") {
    mlsJointSweepShard(kMlsRef, kMlsE0);
}
TEST_CASE("MassLaw_FundamentalStrengthJointSweep_Ref_E1", "[systems][profundum][mass-law][long]") {
    mlsJointSweepShard(kMlsRef, kMlsE1);
}
TEST_CASE("MassLaw_FundamentalStrengthJointSweep_Ref_E2", "[systems][profundum][mass-law][long]") {
    mlsJointSweepShard(kMlsRef, kMlsE2);
}
TEST_CASE("MassLaw_FundamentalStrengthJointSweep_Ref_C3", "[systems][profundum][mass-law][long]") {
    mlsJointSweepShard(kMlsRef, kMlsC3);
}
TEST_CASE("MassLaw_FundamentalStrengthJointSweep_Saw_E0", "[systems][profundum][mass-law][long]") {
    mlsJointSweepShard(kMlsSaw, kMlsE0);
}
TEST_CASE("MassLaw_FundamentalStrengthJointSweep_Saw_E1", "[systems][profundum][mass-law][long]") {
    mlsJointSweepShard(kMlsSaw, kMlsE1);
}
TEST_CASE("MassLaw_FundamentalStrengthJointSweep_Saw_E2", "[systems][profundum][mass-law][long]") {
    mlsJointSweepShard(kMlsSaw, kMlsE2);
}
TEST_CASE("MassLaw_FundamentalStrengthJointSweep_Saw_C3", "[systems][profundum][mass-law][long]") {
    mlsJointSweepShard(kMlsSaw, kMlsC3);
}
TEST_CASE("MassLaw_FundamentalStrengthJointSweep_Sine_E0", "[systems][profundum][mass-law][long]") {
    mlsJointSweepShard(kMlsSine, kMlsE0);
}
TEST_CASE("MassLaw_FundamentalStrengthJointSweep_Sine_E1", "[systems][profundum][mass-law][long]") {
    mlsJointSweepShard(kMlsSine, kMlsE1);
}
TEST_CASE("MassLaw_FundamentalStrengthJointSweep_Sine_E2", "[systems][profundum][mass-law][long]") {
    mlsJointSweepShard(kMlsSine, kMlsE2);
}
TEST_CASE("MassLaw_FundamentalStrengthJointSweep_Sine_C3", "[systems][profundum][mass-law][long]") {
    mlsJointSweepShard(kMlsSine, kMlsC3);
}

// =============================================================================
// SC-003: the perceived-level slope is bounded (FR-028; plan S1 C-1 (a))
// =============================================================================

// SC-003 smoke (deviation 10): Ref at E2 and C3, Mass {0, 25, 50, 75, 100}: span(P) <= 3 dB,
// span(L) <= 6 dB, adjacent |dP| / 25 <= 0.2 dB. Expected red until T019.
TEST_CASE("MassLaw_LevelSlopeBoundedSmoke", "[systems][profundum][mass-law]") {
    for (const std::size_t note : {kMlsE2, kMlsC3}) {
        INFO("note " << kMlsNotes[note].name);
        mlsLevelSlopeRun(kMlsFixtures[kMlsRef], kMlsNotes[note], mlsMassRange(25), std::string());
    }
}

// SC-003: 12 shards, {Ref, Saw, Sine} x {E0, E1, E2, C3}, Mass 0..100 step 1.
TEST_CASE("MassLaw_LevelSlopeBounded_Ref_E0", "[systems][profundum][mass-law][long]") {
    mlsLevelSlopeShard(kMlsRef, kMlsE0);
}
TEST_CASE("MassLaw_LevelSlopeBounded_Ref_E1", "[systems][profundum][mass-law][long]") {
    mlsLevelSlopeShard(kMlsRef, kMlsE1);
}
TEST_CASE("MassLaw_LevelSlopeBounded_Ref_E2", "[systems][profundum][mass-law][long]") {
    mlsLevelSlopeShard(kMlsRef, kMlsE2);
}
TEST_CASE("MassLaw_LevelSlopeBounded_Ref_C3", "[systems][profundum][mass-law][long]") {
    mlsLevelSlopeShard(kMlsRef, kMlsC3);
}
TEST_CASE("MassLaw_LevelSlopeBounded_Saw_E0", "[systems][profundum][mass-law][long]") {
    mlsLevelSlopeShard(kMlsSaw, kMlsE0);
}
TEST_CASE("MassLaw_LevelSlopeBounded_Saw_E1", "[systems][profundum][mass-law][long]") {
    mlsLevelSlopeShard(kMlsSaw, kMlsE1);
}
TEST_CASE("MassLaw_LevelSlopeBounded_Saw_E2", "[systems][profundum][mass-law][long]") {
    mlsLevelSlopeShard(kMlsSaw, kMlsE2);
}
TEST_CASE("MassLaw_LevelSlopeBounded_Saw_C3", "[systems][profundum][mass-law][long]") {
    mlsLevelSlopeShard(kMlsSaw, kMlsC3);
}
TEST_CASE("MassLaw_LevelSlopeBounded_Sine_E0", "[systems][profundum][mass-law][long]") {
    mlsLevelSlopeShard(kMlsSine, kMlsE0);
}
TEST_CASE("MassLaw_LevelSlopeBounded_Sine_E1", "[systems][profundum][mass-law][long]") {
    mlsLevelSlopeShard(kMlsSine, kMlsE1);
}
TEST_CASE("MassLaw_LevelSlopeBounded_Sine_E2", "[systems][profundum][mass-law][long]") {
    mlsLevelSlopeShard(kMlsSine, kMlsE2);
}
TEST_CASE("MassLaw_LevelSlopeBounded_Sine_C3", "[systems][profundum][mass-law][long]") {
    mlsLevelSlopeShard(kMlsSine, kMlsC3);
}

// =============================================================================
// Plan S8.2: the Mass law harvest (hidden; run only by the main loop in T019, after the amount
// table is pasted). Writes evidence/mass-law-table.txt for paste-harvest.js mass-law-rows.
// =============================================================================

TEST_CASE("MassLaw_TableHarvest", "[systems][profundum][mass-law][.calibration]") {
    const auto wallStart = std::chrono::steady_clock::now();
    MlsRows rows = MassLaw::kRows;   // the candidate copy (via ChainConfig::rowsOverride)
    const std::vector<MlsCell> all = mlsAllCells();
    const std::vector<MlsCell> refCells{MlsCell{.fixture = kMlsRef, .note = kMlsE1},
                                        MlsCell{.fixture = kMlsRef, .note = kMlsC3}};
    std::size_t renders = 0;
    std::ostringstream log;
    log << std::setprecision(6);
    std::vector<std::string> stops;
    const auto ix = [](MassLaw::Target t) { return static_cast<std::size_t>(t); };
    const std::size_t kAnchor = ix(MassLaw::Target::AnchorLevel);
    const std::size_t kFold = ix(MassLaw::Target::FoldAmount);
    const std::size_t kBody = ix(MassLaw::Target::Body);
    const std::size_t kDepth = ix(MassLaw::Target::Depth);
    std::vector<std::size_t> allPoints;
    for (std::size_t i = 0; i < kMlsLatticePoints; ++i)
        allPoints.push_back(i);
    log << "seed: " << mlsRowsSummary(rows) << '\n';

    // Group 8 ruling (user, 2026-10-17; R-6 / FR-021; spec Clarifications "Group 8 harvest ruling"): the
    // Anchor's level multiplies the core's h1 (anchor_oscillator.h:340), so it cannot carry a fundamental
    // the Depth bias removes. The Depth and Anchor travel are therefore harvested: step (0) the Anchor
    // travel SC-003's unweighted span allows, step (1) the Depth travel SC-001(c) allows, step (2) Fold /
    // Body for SC-002. Shortfalls are not stops here; the final full-lattice verdict (step 3) decides.

    // Step (0): the largest Anchor travel a in [0, 0.5] (amountDown = amountUp = a) whose full-lattice
    // unweighted span(L) is <= 6 dB on every cell. The pure-sine E0 cell bounds it: the sidechain
    // high-pass at 2 f0 cannot see the sine the Anchor doubles.
    const auto stepAnchorTravel = [&](int round) {
        MassLaw::Row& anchor = rows[kAnchor];
        const float hi = std::min(0.5f, mlsCapAmount(anchor, anchor.curve));
        const auto pass = [&](float a) {
            MlsRows cand = rows;
            cand[kAnchor].amountDown = a;
            cand[kAnchor].amountUp = a;
            std::vector<MlsCellSweep> s(all.size());
            mlsSweepInto(cand, all, allPoints, s, renders);
            const double spanL = mlsWorstSpanL(s, all);
            log << "round " << round << " (0) Anchor travel " << a << ": worst span(L) " << spanL << " dB\n";
            return spanL <= kMlsSpanLMaxDb;
        };
        const std::optional<float> a = mlsBisectLargest(0.0f, hi, pass);
        const float travel = a.has_value() ? *a : 0.0f;
        anchor.amountDown = travel;
        anchor.amountUp = travel;
        if (!a.has_value())
            log << "round " << round << " (0) span(L) > " << kMlsSpanLMaxDb << " dB even with no Anchor travel\n";
    };

    // Step (1): the Depth travel, direction unchanged (falling, D-2), against SC-001(c) as re-ruled
    // (G8-4): the sum's w1 at every point >= w1(Mass 0) - kMlsHarvestW1FallDb. The lower half first,
    // because Mass 0 depends on amountDown: the largest |amountDown| in [0, 0.5] over Mass 0..50, then
    // the largest |amountUp| over 50..100 with points 0 and 50 rendered once (Mass 50 is independent
    // of every amount, mass_law.h:128-130).
    const auto stepDepth = [&](int round) {
        MassLaw::Row& depth = rows[kDepth];
        const float cap = std::min(0.5f, mlsCapAmount(depth, depth.curve));
        const auto logDip = [&](int rnd, const char* which, float magnitude, const MlsDip& d) {
            log << "round " << rnd << " (1) Depth " << which << magnitude << ": worst fall from Mass 0 " << d.worst
                << " dB at " << kMlsFixtures[all[d.cell].fixture].name << ' ' << kMlsNotes[all[d.cell].note].name
                << " Mass " << mlsLatticeMass(d.at) << '\n';
        };
        const auto lowerPass = [&](float magnitude) {
            MlsRows cand = rows;
            cand[kDepth].amountDown = -magnitude;
            std::vector<MlsCellSweep> s(all.size());
            mlsSweepInto(cand, all, {0, 1, 2, 3, 4, kMlsMid}, s, renders);
            const MlsDip d = mlsWorstFallFromZero(s, 0, kMlsMid);
            logDip(round, "amountDown -", magnitude, d);
            return d.worst >= -kMlsHarvestW1FallDb;
        };
        const std::optional<float> down = mlsBisectLargest(0.0f, cap, lowerPass);
        depth.amountDown = -(down.has_value() ? *down : 0.0f);
        if (!down.has_value())
            log << "round " << round << " (1) SC-001(c) Mass 0..50 not met even with no Depth travel\n";

        std::vector<MlsCellSweep> base(all.size());
        mlsSweepInto(rows, all, {0, kMlsMid}, base, renders);
        const auto upperPass = [&](float magnitude) {
            MlsRows cand = rows;
            cand[kDepth].amountUp = -magnitude;
            std::vector<MlsCellSweep> s = base;
            mlsSweepInto(cand, all, {6, 7, 8, 9, 10}, s, renders);
            const MlsDip d = mlsWorstFallFromZero(s, kMlsMid, kMlsTop);
            logDip(round, "amountUp -", magnitude, d);
            return d.worst >= -kMlsHarvestW1FallDb;
        };
        const std::optional<float> up = mlsBisectLargest(0.0f, cap, upperPass);
        depth.amountUp = -(up.has_value() ? *up : 0.0f);
        if (!up.has_value())
            log << "round " << round << " (1) SC-001(c) Mass 50..100 not met even with no Depth travel\n";
    };

    // Step (2): the smallest Fold amountUp meeting THD(100) >= THD(50) + 6.5 dB on Ref at E1 and C3;
    // Body amountUp in [0, 0.5] is the second lever, searched with the Fold at its interval end only
    // when the Fold alone cannot meet it. The Mass 0 -> 50 margins (THD(50) >= THD(0) + 6.5 dB,
    // E_low(50) >= E_low(0) + 3.5 dB) do not depend on either amountUp; they are checked on the final
    // table (step 3). Fold interval: [0, the C-7 (i) cap] = [0, 100] (the task text's [0, 50] is the
    // C-7 (ii) cap; the ruled seed is already +60; evidence/run-log/T014.txt decision 4).
    const auto stepFoldBody = [&](int round) {
        const MlsRows before = rows;
        const auto thdPass = [&](const MlsRows& cand, const char* what, float value) {
            std::vector<MlsCellSweep> s(refCells.size());
            mlsSweepInto(cand, refCells, {kMlsMid, kMlsTop}, s, renders);
            bool ok = true;
            for (std::size_t c = 0; c < refCells.size(); ++c) {
                const double step = s[c][kMlsTop].thdDb - s[c][kMlsMid].thdDb;
                log << "round " << round << " (2) " << what << ' ' << value << ": Ref "
                    << kMlsNotes[refCells[c].note].name << " THD(100) - THD(50) " << step << " dB\n";
                if (step < kMlsHarvestThdStepDb)
                    ok = false;
            }
            return ok;
        };
        MassLaw::Row& fold = rows[kFold];
        const float foldHi = mlsCapAmount(fold, fold.curve);
        const std::optional<float> foldUp = mlsBisectSmallest(0.0f, foldHi, [&](float a) {
            MlsRows cand = rows;
            cand[kFold].amountUp = a;
            return thdPass(cand, "Fold amountUp", a);
        });
        if (foldUp.has_value()) {
            fold.amountUp = *foldUp;
        } else {
            fold.amountUp = foldHi;
            MassLaw::Row& body = rows[kBody];
            const float bodyHi = std::min(0.5f, mlsCapAmount(body, body.curve));
            const std::optional<float> bodyUp = mlsBisectSmallest(0.0f, bodyHi, [&](float b) {
                MlsRows cand = rows;
                cand[kBody].amountUp = b;
                return thdPass(cand, "Body amountUp (Fold at its cap)", b);
            });
            if (bodyUp.has_value()) {
                body.amountUp = *bodyUp;
            } else {
                body.amountUp = bodyHi;
                std::ostringstream os;
                os << std::setprecision(6) << "SC-002 THD(100) >= THD(50) + 6.5 dB not met with Fold amountUp "
                   << foldHi << " and Body amountUp " << bodyHi;
                stops.push_back(os.str());
            }
        }
        return !mlsRowsEqual(before, rows);
    };

    int roundsRun = 0;
    for (int round = 0; round < kMlsMaxRounds; ++round) {
        ++roundsRun;
        const MlsRows before = rows;
        stepAnchorTravel(round);
        stepDepth(round);
        stepFoldBody(round);
        const bool changed = !mlsRowsEqual(before, rows);
        log << "after round " << round << ": " << mlsRowsSummary(rows) << (changed ? "(changed)" : "(stable)") << '\n';
        if (!changed)
            break;
    }

    // Step (2c): the FR-021 output trim (user ruling 2026-10-17, Clarifications "Group 8 harvest ruling"
    // G8-5): one Linear row, 0 dB at Mass 50, monotone non-increasing (FR-024), harvested so the
    // reference voice's K-weighted level at the gated notes (E2, C3) is flat from Mass 0 to 50 and does
    // not rise above 50. Only rises are cancelled: amountDown = -max(0, mean P(50) - P(0)),
    // amountUp = -max(0, mean P(100) - P(50)). The trim sits after the dynamics, so SC-001(c) (sum tap)
    // and SC-002 (ratios) do not see it; it runs once, after the rounds.
    {
        const std::size_t kTrim = ix(MassLaw::Target::OutputTrim);
        rows[kTrim].amountDown = 0.0f;
        rows[kTrim].amountUp = 0.0f;
        std::vector<MlsCell> gatedRef;
        for (std::size_t n = 0; n < kMlsNotes.size(); ++n)
            if (kGateKWeightAt[kMlsNotes[n].index])
                gatedRef.push_back(MlsCell{.fixture = kMlsRef, .note = n});
        REQUIRE_FALSE(gatedRef.empty());
        std::vector<MlsCellSweep> s(gatedRef.size());
        mlsSweepInto(rows, gatedRef, {0, kMlsMid, kMlsTop}, s, renders);
        double d50 = 0.0;
        double d100 = 0.0;
        for (std::size_t c = 0; c < s.size(); ++c) {
            d50 += s[c][kMlsMid].pDb - s[c][0].pDb;
            d100 += s[c][kMlsTop].pDb - s[c][kMlsMid].pDb;
        }
        d50 /= static_cast<double>(s.size());
        d100 /= static_cast<double>(s.size());
        rows[kTrim].amountDown = -static_cast<float>(std::max(0.0, d50));
        rows[kTrim].amountUp = -static_cast<float>(std::max(0.0, d100));
        log << "(2c) output trim (G8-5): Ref K-weighted P(50) - P(0) " << d50 << " dB, P(100) - P(50) " << d100
            << " dB (mean over the gated notes) -> amountDown " << rows[kTrim].amountDown << ", amountUp "
            << rows[kTrim].amountUp << '\n';
    }

    // Step (3): SC-003 against the bar on all four notes (K-weighted gated per kGateKWeightAt, plan
    // S1 C-1 (a); the unweighted span everywhere), on the full lattice. Fold or Anchor bends to
    // SCurve only if the gated |dP| needs it, and only within the C-7 cap.
    MlsVerdict verdict = mlsFullEval(rows, all, "harvested (Linear)", renders);
    std::string evals = verdict.detail;
    std::array<double, 2> bestE0E1{verdict.spanPByNote[kMlsE0], verdict.spanPByNote[kMlsE1]};
    if (!verdict.slopeGated) {
        std::vector<std::pair<std::string, MlsRows>> bends;
        MlsRows foldS = rows;
        foldS[kFold].curve = ModCurve::SCurve;
        MlsRows anchorS = rows;
        anchorS[kAnchor].curve = ModCurve::SCurve;
        MlsRows bothS = foldS;
        bothS[kAnchor].curve = ModCurve::SCurve;
        bends.emplace_back("Fold SCurve", foldS);
        bends.emplace_back("Anchor SCurve", anchorS);
        bends.emplace_back("Fold + Anchor SCurve", bothS);
        for (const auto& [label, cand] : bends) {
            if (!mlsWithinCap(cand[kFold]) || !mlsWithinCap(cand[kAnchor])) {
                log << "bend " << label << ": skipped (C-7 cap)\n";
                continue;
            }
            const MlsVerdict vb = mlsFullEval(cand, all, label, renders);
            evals += vb.detail;
            bestE0E1[0] = std::min(bestE0E1[0], vb.spanPByNote[kMlsE0]);
            bestE0E1[1] = std::min(bestE0E1[1], vb.spanPByNote[kMlsE1]);
            if (vb.sc001c && vb.sc003L && vb.slopeGated) {
                log << "bend " << label << ": adopted\n";
                rows = cand;
                verdict = vb;
                break;
            }
            log << "bend " << label << ": rejected\n";
        }
    }

    bool capsOk = true;
    for (const MassLaw::Row& r : rows)
        capsOk = capsOk && mlsWithinCap(r);
    if (!capsOk)
        stops.emplace_back("a harvested row violates the C-7 cap (mass_law.h static_assert)");
    if (!(verdict.sc001c && verdict.sc003L)) {
        std::ostringstream os;
        os << std::setprecision(6) << "FR-021 ruling needed (R-6) SC-001(c) worst fall from Mass 0 " << verdict.worstW1Fall
           << " dB (harvest bar " << kMlsHarvestW1FallDb << "), SC-003 worst span(L) " << verdict.worstSpanL
           << " dB (bar " << kMlsSpanLMaxDb << ")";
        stops.push_back(os.str());
    }
    if (!(verdict.sc003KGated && verdict.slopeGated)) {
        std::ostringstream os;
        os << std::setprecision(6) << "SC-003 K-weighted at E2 / C3: span(P) " << verdict.worstGatedSpanP
           << " dB (bar " << kMlsSpanPMaxDb << "), |dP| " << verdict.worstGatedSlope << " dB/unit (bar "
           << kMlsSlopePMaxDbPerUnit << ")";
        stops.push_back(os.str());
    }
    if (!verdict.sc002)
        stops.emplace_back("SC-002 + 0.5 dB margins not met on Ref at E1 / C3 (see the check lines)");

    const double wallSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count();
    std::ostringstream table;
    table << "BEGIN-INITIALIZER mass-law-rows\n";
    for (const MassLaw::Row& r : rows)
        table << mlsRowLine(r) << '\n';
    table << "END-INITIALIZER mass-law-rows\n";
    std::cout << table.str();

    std::ofstream out = mlsOpenEvidence("mass-law-table.txt");
    out << table.str() << '\n';
    out << "MassLaw_TableHarvest (plan S8.2; tasks.md T014): rounds " << roundsRun << ", renders " << renders
        << ", wall clock " << wallSeconds << " s, hardware_concurrency " << std::thread::hardware_concurrency()
        << "\n";
    out << "best K-weighted span(P) reached (FR-048 measurement, recorded per C-1 (a)): E0 " << bestE0E1[0]
        << " dB, E1 " << bestE0E1[1] << " dB\n\n";
    out << log.str() << '\n' << evals;
    for (const std::string& s : stops) {
        out << "STOP AND SURFACE: " << s << '\n';
        std::cout << "STOP AND SURFACE: " << s << '\n';
    }
    out.close();

    std::string joined;
    for (const std::string& s : stops)
        joined += s + "; ";
    if (!stops.empty())
        FAIL("STOP AND SURFACE: " << joined);
    SUCCEED();
}
