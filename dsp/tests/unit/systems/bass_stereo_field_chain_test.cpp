// ==============================================================================
// Layer 3: System Tests - BassStereoField, chain TU
//                          (Profundum Phase 7, specs/profundum-phase7-stereo-field)
// ==============================================================================
// Reference: specs/profundum-phase7-stereo-field/spec.md (SC-004(a), SC-007 [long], SC-009(b), SC-018/019 rendered)
//            specs/profundum-phase7-stereo-field/plan.md (S10 chain, S13.2 conventions, S13.3 rows SC-004/007/009/018/019;
//            S1 C-14 phase skip rule, C-15 SOLID by slew)
//            specs/profundum-phase7-stereo-field/tasks.md T012 (deviations 6, 10)
//
// T012: the rendered full-chain cases (FR-051, FR-052, FR-061, FR-078).
//   BassStereoField_SolidBitIdentical_<Algorithm>   (six cases, Tube ... Fracture; T016 split by algorithm, the
//                                         single case read 173 s > 60 s in the Group 8 per-push log)
//                                         SC-004 (a1) from reset in SOLID, (a2) switched into SOLID at the stage
//                                         output, (a3) the limiter output converges (CSV sc004-a3-convergence.csv,
//                                         each case replaces its algorithm's rows). Per-push (deviation 10: never [long]).
//   BassStereoField_WideAliasing          SC-007 [long]: WIDE dirty arm, every algorithm, E0-E2, 44.1/48/96 kHz.
//   BassStereoField_WideAliasingSmoke     SC-007 smoke sibling (deviation 6): Rectify and Fracture, E1, 48 kHz.
//   BassStereoField_TransitionsClickFree  SC-009(b) [long] (T016: 104 s per push) long-ramp form (G7-1): sample-delta
//                                         bound on outL/R against steady renders along the traversed path,
//                                         differential count recorded on outL/R (sc009b-clicks.csv) and gated at 0
//                                         on coreL/R, with the positive controls (pan jump, hard L/R switch).
//   BassStereoField_TransitionsClickFreeSmoke  SC-009(b) smoke sibling: E2, block 512, G0>G1>G0 (sc009b-clicks-smoke.csv).
//   BassStereoField_H1Centred             SC-018 rendered [long] (T016: 16.4 s per push): h1 on L/R ==
//                                         deliveredGains()[0] * kCenterPanGain, M-h1 and Anchor power against
//                                         Spread 0, post-character sub-band M printed.
//   BassStereoField_H1CentredSmoke        SC-018 smoke sibling: E1, every mode, Spread 1.
//   BassStereoField_PanOnlyDirection      SC-019 rendered [long] (T016: 18.7 s per push): per-harmonic L - R phase
//                                         (C-14 skip rule) and the harmonic-resolved Q.
//   BassStereoField_PanOnlyDirectionSmoke SC-019 smoke sibling: E1, WIDE, Spread 1, MOTION 1 Cycle.
// ==============================================================================

#include "bass_stereo_field_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/profundum_core.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using Krate::DSP::BassStereoField;
using Krate::DSP::kMaxPartials;
using Krate::DSP::ProfundumCore;
using Krate::DSP::SpectralShapeRecipe;
using BsfAlgorithm = Krate::DSP::BassCharacterStage::Algorithm;
using BsfControls = Krate::DSP::BassStereoField::Controls;
using BsfMode = Krate::DSP::BassStereoField::ImageMode;
using BsfSource = Krate::DSP::BassStereoField::MotionSource;
namespace Sft = Krate::DSP::StereoFieldTest;
namespace Mdt = Krate::DSP::MassDynamicsTest;
namespace Sct = Krate::DSP::ShapeCharacterTest;
namespace Pt = Krate::DSP::ProfundumTest;
namespace Hft = Krate::DSP::HarmonicFoldTest;

constexpr double kBsfFs = 48000.0;
constexpr double kBsfLrBarLinear = 1e-6;   ///< SC-004(a3): -120 dBFS
constexpr double kBsfAliasBarDbfs = -80.0;  ///< SC-007
constexpr double kBsfH1DeliveredBarDb = 0.01;   ///< SC-018
constexpr double kBsfH1SpreadBarDb = 0.1;       ///< SC-018
constexpr double kBsfPhaseBarRad = 0.01;        ///< SC-019
constexpr double kBsfHarmonicQBarDb = -3.11;    ///< SC-019 (-3.01 dB - 0.1 dB)
constexpr double kBsfOneSidedPowerRatio = 1e-6;   ///< plan S1 C-14: weaker channel within 60 dB of the stronger
constexpr double kBsfHarmonicEnergyFloor = 1e-10; ///< "with energy": >= -100 dB re the strongest harmonic
constexpr std::size_t kBsfPhaseWindow = std::size_t{1} << 14;

struct BsfAlgorithmEntry {
    BsfAlgorithm algorithm;
    const char* name;
};

const std::array<BsfAlgorithmEntry, 6> kBsfAlgorithms{{
    {.algorithm = BsfAlgorithm::Tube, .name = "Tube"},
    {.algorithm = BsfAlgorithm::Fold, .name = "Fold"},
    {.algorithm = BsfAlgorithm::Rectify, .name = "Rectify"},
    {.algorithm = BsfAlgorithm::Bite, .name = "Bite"},
    {.algorithm = BsfAlgorithm::Crush, .name = "Crush"},
    {.algorithm = BsfAlgorithm::Fracture, .name = "Fracture"},
}};

const char* bsfModeName(BsfMode m) {
    switch (m) {
        case BsfMode::Solid: return "SOLID";
        case BsfMode::Spread: return "SPREAD";
        case BsfMode::Wide: return "WIDE";
    }
    return "?";
}

std::string bsfEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase7-stereo-field/evidence/";
}

double bsfSeconds(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
}

double bsfDb20(double x) { return 20.0 * std::log10(std::max(x, 1e-30)); }

/// The kSawAnchor chain fixture (tasks.md rule 10) at f0 / fs / seconds / host block; every arm switch off.
Sft::FieldChainConfig bsfChainConfig(double f0, double fs, double seconds, std::size_t hostBlock) {
    Sft::FieldChainConfig cfg{};
    cfg.chain.base = SpectralShapeRecipe::kSawAnchor;
    cfg.chain.f0 = f0;
    cfg.chain.fs = fs;
    cfg.chain.seconds = seconds;
    cfg.chain.hostBlock = hostBlock;
    cfg.chain.attachAnchor = false;
    cfg.shapeOn = false;
    cfg.foldAmount.reset();
    cfg.algorithm.reset();
    cfg.excitation = ProfundumCore::kDefaultExcitation;
    return cfg;
}

/// Dirty arm (rule 10): SHAPE 1 -> Fold 100 -> character at `a`, Amount 1, Tone 0.5.
void bsfSetDirty(Sft::FieldChainConfig& cfg, BsfAlgorithm a) {
    cfg.shapeOn = true;
    cfg.shape = 1.0f;
    cfg.foldAmount = 100.0f;
    cfg.algorithm = a;
    cfg.characterAmount = 1.0f;
    cfg.characterTone = 0.5f;
}

/// Anchor arm (rule 10): the dirty arm plus the Anchor (Sine / LOCK, the Phase 6 a1 feed).
void bsfSetAnchorArm(Sft::FieldChainConfig& cfg, BsfAlgorithm a) {
    bsfSetDirty(cfg, a);
    cfg.chain.attachAnchor = true;
}

BsfControls bsfControls(BsfMode mode, float spread, float gravity, float crossoverHz, float curve, float depth,
                        BsfSource source) {
    BsfControls c{};
    c.mode = mode;
    c.spread = spread;
    c.gravity = gravity;
    c.crossoverHz = crossoverHz;
    c.curve = curve;
    c.motionDepth = depth;
    c.motionSource = source;
    return c;
}

bool bsfRangeBitEqual(const std::vector<float>& a, const std::vector<float>& b, std::size_t begin, std::size_t end) {
    REQUIRE(a.size() >= end);
    REQUIRE(b.size() >= end);
    if (end <= begin)
        return true;
    return std::memcmp(a.data() + begin, b.data() + begin, (end - begin) * sizeof(float)) == 0;
}

/// M = (L + R) / 2, computed in double and rounded to float.
std::vector<float> bsfMid(const std::vector<float>& L, const std::vector<float>& R) {
    REQUIRE(L.size() == R.size());
    std::vector<float> m(L.size(), 0.0f);
    for (std::size_t i = 0; i < L.size(); ++i)
        m[i] = static_cast<float>(0.5 * (static_cast<double>(L[i]) + static_cast<double>(R[i])));
    return m;
}

/// The whole-period length (samples) of f0 closest to an integer, using at most maxLen samples and at least
/// 3/4 of the periods that fit (helpers self-test precedent).
std::size_t bsfWholePeriodLength(double fs, double f0, std::size_t maxLen) {
    const double period = fs / f0;
    const auto mMax = static_cast<std::size_t>(std::floor(static_cast<double>(maxLen) / period));
    REQUIRE(mMax >= 1);
    const std::size_t mMin = std::max<std::size_t>(1, (mMax * 3) / 4);
    std::size_t best = 0;
    double bestFrac = 2.0;
    for (std::size_t m = mMin; m <= mMax; ++m) {
        const double s = static_cast<double>(m) * period;
        const double frac = std::abs(s - std::round(s));
        if (frac < bestFrac && std::llround(s) <= static_cast<long long>(maxLen)) {
            bestFrac = frac;
            best = static_cast<std::size_t>(std::llround(s));
        }
    }
    REQUIRE(best > 0);
    return best;
}

double bsfMeanSquare(const float* x, std::size_t n) {
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        s += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    return n > 0 ? s / static_cast<double>(n) : 0.0;
}

/// A bare ProfundumCore render (kSawAnchor, Reset retrigger, note-on at 0) whose pan vector is handed by the
/// test at every grid point (the field bypassed): the SC-009(b) pan-jump positive control.
struct BsfCoreRender {
    std::vector<float> L, R;
};

BsfCoreRender bsfCoreRender(double f0, double fs, double seconds, std::size_t hostBlock,
                            const std::function<const std::array<float, kMaxPartials>&(std::size_t)>& vectorAt) {
    const std::size_t total = Mdt::renderLength(fs, seconds);
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(fs);
    core->setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
    core->setControls(SpectralShapeRecipe::kSawAnchor);
    BsfCoreRender r;
    r.L.assign(total, 0.0f);
    r.R.assign(total, 0.0f);
    Mdt::gridDriver(total, hostBlock, [&](std::size_t start, std::size_t len, bool atGrid) {
        if (atGrid) {
            core->setPartialPanOffsets(vectorAt(start));
            if (start == 0)
                core->noteOn(static_cast<float>(f0));
        }
        core->processBlock(r.L.data() + start, r.R.data() + start, len);
    });
    return r;
}

/// The grid point in [lo, hi) where the step a -> b is most visible to the differential click count: every
/// grid point with a reference detection (ClickDetector on aL, aR, bL or bR) within +-N = floor(fs / f0 / 8)
/// samples is skipped (there the count matches the step to the reference's own edge by definition), and of the
/// rest the one with the largest min over L and R of |a[g] - b[g]| / max(maxDeltaAround(a), maxDeltaAround(b))
/// is returned, read over +-max(N, frameSize) so the step also stands out of the detector frames around it
/// (ClickDetector thresholds each 512-sample frame at mean + 5 sigma of |dy|; over +-N alone it is the
/// amplitude guard's ratio). The positive controls put their unsmoothed step there.
/// (A step at the largest |a - b| lands on a waveform edge, where both references already have a detection:
/// the Group 7 gate measured every pan-jump control and the E0 hard-switch control MISSED that way.)
std::size_t bsfControlGridPoint(const std::vector<float>& aL, const std::vector<float>& aR,
                                const std::vector<float>& bL, const std::vector<float>& bR, std::size_t lo,
                                std::size_t hi, double fs, double f0) {
    const std::size_t n = aL.size();
    REQUIRE(hi <= n);
    REQUIRE(aR.size() == n);
    REQUIRE(bL.size() == n);
    REQUIRE(bR.size() == n);
    const auto windowN = static_cast<std::size_t>(std::floor(fs / f0 / 8.0));
    Krate::DSP::TestUtils::ClickDetectorConfig dc{};   // the documented defaults, as differentialClicks uses them
    dc.sampleRate = static_cast<float>(fs);
    REQUIRE(dc.isValid());
    const std::size_t localN = std::max(windowN, dc.frameSize);
    std::vector<std::size_t> refIdx;
    for (const std::vector<float>* x : {&aL, &aR, &bL, &bR}) {
        Krate::DSP::TestUtils::ClickDetector det(dc);
        det.prepare();
        for (const Krate::DSP::TestUtils::ClickDetection& d : det.detect(x->data(), n))
            refIdx.push_back(d.sampleIndex);
    }
    std::sort(refIdx.begin(), refIdx.end());
    std::size_t best = n;
    double bestRatio = -1.0;
    for (std::size_t g = ((lo + Sft::kGrid - 1) / Sft::kGrid) * Sft::kGrid; g < hi; g += Sft::kGrid) {
        const auto first = std::lower_bound(refIdx.begin(), refIdx.end(), g > windowN ? g - windowN : 0);
        if (first != refIdx.end() && *first <= g + windowN)
            continue;
        // max(), not infinity(): this TU is -ffast-math, where an infinity constant is undefined behaviour; on macOS
        // arm64 the seed left `ratio > bestRatio` false at every grid point (2026-10-25).
        double ratio = std::numeric_limits<double>::max();
        for (std::size_t ch = 0; ch < 2; ++ch) {
            const std::vector<float>& a = ch == 0 ? aL : aR;
            const std::vector<float>& b = ch == 0 ? bL : bR;
            const double step = std::abs(static_cast<double>(a[g]) - static_cast<double>(b[g]));
            const double local = std::max(Sct::maxDeltaAround(a.data(), n, g, localN),
                                          Sct::maxDeltaAround(b.data(), n, g, localN));
            ratio = std::min(ratio, step / std::max(local, 1e-30));
        }
        if (ratio > bestRatio) {
            bestRatio = ratio;
            best = g;
        }
    }
    INFO("no grid point in [" << lo << ", " << hi << ") is clear of reference detections within +-" << windowN);
    REQUIRE(best < n);
    return best;
}

struct BsfOut {
    std::vector<float> L, R;
};

/// SC-007 per cell: the WIDE dirty arm at `a`, Amount 1, offsets at the FR-041 ceilings (K_wide forced to 1 by
/// the probe), Fold 100, Spread 1; wideAliasedDbfs of characterL and characterR.
void bsfWideAliasingCell(const BsfAlgorithmEntry& alg, double f0, const char* noteName, double fs) {
    const double f = alg.algorithm == BsfAlgorithm::Fracture ? 0.5 * f0 : f0;
    const std::size_t fftLen = Hft::aliasFftLength(fs, f);
    const double seconds = Mdt::kSettleSeconds + static_cast<double>(fftLen) / fs + 0.05;
    Sft::FieldChainConfig cfg = bsfChainConfig(f0, fs, seconds, 512);
    bsfSetDirty(cfg, alg.algorithm);
    cfg.wideScaleOverride = 1.0f;   // the FR-041 ceilings, whatever K_wide the harvest commits
    cfg.field = bsfControls(BsfMode::Wide, 1.0f, 0.5f, BassStereoField::kDefaultCrossoverHz, 0.5f, 0.0f,
                            BsfSource::Drift);
    const Sft::FieldChainTaps t = Sft::renderFieldChain(cfg);

    INFO("SC-007 " << alg.name << " " << noteName << " fs " << fs);
    REQUIRE(!t.offsetsPerInterval.empty());
    const BassStereoField::ChannelOffsets o = t.offsetsPerInterval.back();
    const float ceiling = BassStereoField::wideCeiling(alg.algorithm);
    CHECK(std::bit_cast<std::uint32_t>(o.amount) == std::bit_cast<std::uint32_t>(ceiling));
    CHECK(std::bit_cast<std::uint32_t>(o.tone) == std::bit_cast<std::uint32_t>(-ceiling));

    const double aL = Sft::wideAliasedDbfs(t.characterL, fs, f0, alg.algorithm);
    const double aR = Sft::wideAliasedDbfs(t.characterR, fs, f0, alg.algorithm);
    std::cout << "SC007," << alg.name << ',' << noteName << ',' << fs << ",fft," << fftLen << ",offset," << o.amount
              << ",aliasL_dbfs," << aL << ",aliasR_dbfs," << aR << ",bar," << kBsfAliasBarDbfs << ','
              << ((aL <= kBsfAliasBarDbfs && aR <= kBsfAliasBarDbfs) ? "PASS" : "FAIL") << "\n";
    CHECK(aL <= kBsfAliasBarDbfs);
    CHECK(aR <= kBsfAliasBarDbfs);
}

/// The positions of the detections differentialClicks counts (the same ClickDetector defaults, the same
/// +-N match against both references and the same 1.1 x amplitude guard, shape_character_metric_helpers.h:76-133),
/// for the SC-009(b) record (evidence/sc009b-clicks.csv). The caller checks the size against `counted`.
std::vector<std::size_t> bsfCountedPositions(const float* transition, const float* refStart, const float* refEnd,
                                             std::size_t n, double fs, double f0) {
    Krate::DSP::TestUtils::ClickDetectorConfig dc{};
    dc.sampleRate = static_cast<float>(fs);
    REQUIRE(dc.isValid());
    const auto windowN = static_cast<std::size_t>(std::floor(fs / f0 / 8.0));
    std::vector<std::size_t> refIdx;
    for (const float* r : {refStart, refEnd}) {
        Krate::DSP::TestUtils::ClickDetector det(dc);
        det.prepare();
        for (const Krate::DSP::TestUtils::ClickDetection& d : det.detect(r, n))
            refIdx.push_back(d.sampleIndex);
    }
    std::sort(refIdx.begin(), refIdx.end());
    Krate::DSP::TestUtils::ClickDetector trDet(dc);
    trDet.prepare();
    std::vector<std::size_t> counted;
    for (const Krate::DSP::TestUtils::ClickDetection& d : trDet.detect(transition, n)) {
        const std::size_t i = d.sampleIndex;
        const auto first = std::lower_bound(refIdx.begin(), refIdx.end(), i > windowN ? i - windowN : 0);
        if (first != refIdx.end() && *first <= i + windowN)
            continue;
        const double refMax = std::max(Sct::maxDeltaAround(refStart, n, i, windowN),
                                       Sct::maxDeltaAround(refEnd, n, i, windowN));
        if (Sct::maxDeltaAround(transition, n, i, windowN) > 1.1 * refMax)
            counted.push_back(i);
    }
    return counted;
}

/// Positive-control amplitude arm (tasks.md T012): the transition's max |dy| within +-N of the step g exceeds
/// 1.1 x the larger of the two references' over the same window.
bool bsfStepStandsOut(const std::vector<float>& tr, const std::vector<float>& refA, const std::vector<float>& refB,
                      std::size_t g, double fs, double f0, double& trMax, double& refMax) {
    const std::size_t n = tr.size();
    REQUIRE(refA.size() == n);
    REQUIRE(refB.size() == n);
    const auto windowN = static_cast<std::size_t>(std::floor(fs / f0 / 8.0));
    trMax = Sct::maxDeltaAround(tr.data(), n, g, windowN);
    refMax = std::max(Sct::maxDeltaAround(refA.data(), n, g, windowN), Sct::maxDeltaAround(refB.data(), n, g, windowN));
    return trMax > 1.1 * refMax;
}

/// Transition-render taps SC-009(b) reads: the chain output (sample-delta gate, recorded count) and the core
/// (generator) tap (count gated at 0).
struct BsfTransitionTaps {
    BsfOut out;
    BsfOut core;
};

}  // namespace

// =============================================================================
// SC-004(a1)(a2)(a3): SOLID is L == R bit-identical (FR-051, FR-052; plan S1 C-15). Per-push (deviation 10).
// T016: split by algorithm into six per-push TEST_CASEs (the single case read SC004_A1 wall_s 173.094 > 60 s in
// evidence/run-log/close/harvest-step6-perpush-rerun.log:437); the grid per algorithm is unchanged.
// =============================================================================
namespace {

/// SC-004(a3) record: rewrites evidence/sc004-a3-convergence.csv with `rows` as `algName`'s rows, keeping the
/// other algorithms' rows from an earlier run (the six per-algorithm cases share the file). Rows are written in
/// kBsfAlgorithms order whatever order the cases ran in; a file with another header is replaced whole.
void bsfWriteA3Csv(const std::string& path, const std::string& header, const char* algName,
                   const std::vector<std::string>& rows) {
    constexpr std::size_t kAlgorithmField = 3;   // start_mode,gravity,crossover_hz,algorithm,...
    std::array<std::vector<std::string>, std::tuple_size_v<decltype(kBsfAlgorithms)>> byAlgorithm{};
    const auto algorithmIndex = [](const std::string& name) {
        for (std::size_t a = 0; a < kBsfAlgorithms.size(); ++a) {
            if (name == kBsfAlgorithms[a].name)
                return a;
        }
        return kBsfAlgorithms.size();
    };
    {
        std::ifstream in(path);
        std::string line;
        if (in.is_open() && std::getline(in, line) && line == header) {
            while (std::getline(in, line)) {
                std::stringstream ss(line);
                std::string field;
                for (std::size_t f = 0; f <= kAlgorithmField; ++f) {
                    if (!std::getline(ss, field, ',')) {
                        field.clear();
                        break;
                    }
                }
                const std::size_t a = algorithmIndex(field);
                if (a < kBsfAlgorithms.size() && field != algName)
                    byAlgorithm[a].push_back(line);
            }
        }
    }
    const std::size_t self = algorithmIndex(algName);
    REQUIRE(self < kBsfAlgorithms.size());
    byAlgorithm[self] = rows;
    std::ofstream csv(path, std::ios::trunc);
    REQUIRE(csv.is_open());
    csv << header << '\n';
    for (const std::vector<std::string>& group : byAlgorithm) {
        for (const std::string& row : group)
            csv << row << '\n';
    }
}

/// SC-004 for one algorithm: (a1) the full SC-001 control grid x host blocks from reset() in SOLID, then (a2)/(a3)
/// switched into SOLID from SPREAD and WIDE.
void bsfSolidBitIdentical(const BsfAlgorithmEntry& alg) {
    std::cout << std::setprecision(6);
    const double f0 = Sct::kE1Hz;
    const ProfundumCore::ExcitationControls excitation{.mix = 0.5f, .drive = 0.5f, .asymmetry = 0.0f};

    // ---- (a1) from reset() in SOLID: every SC-001 control setting x host block, 0.1 s at E1 ----
    {
        struct Motion {
            const char* name;
            float depth;
            BsfSource source;
        };
        const std::array<Motion, 3> motions{{{.name = "depth0", .depth = 0.0f, .source = BsfSource::Drift},
                                             {.name = "drift1", .depth = 1.0f, .source = BsfSource::Drift},
                                             {.name = "cycle1", .depth = 1.0f, .source = BsfSource::Cycle}}};
        const std::array<float, 3> spreads{0.0f, 0.5f, 1.0f};
        const std::array<float, 3> gravities{0.0f, 0.5f, 1.0f};
        const std::array<float, 3> crossovers{60.0f, 120.0f, 250.0f};
        const std::array<float, 2> curves{0.0f, 1.0f};
        const std::array<std::size_t, 4> blocks{1, 17, 32, 512};

        const auto t0 = std::chrono::steady_clock::now();
        std::size_t renders = 0;
        double peak = 0.0;
        for (const float spread : spreads) {
            for (const float gravity : gravities) {
                for (const float xover : crossovers) {
                    for (const float curve : curves) {
                        for (const Motion& m : motions) {
                            for (const std::size_t block : blocks) {
                                Sft::FieldChainConfig cfg = bsfChainConfig(f0, kBsfFs, 0.1, block);
                                bsfSetAnchorArm(cfg, alg.algorithm);
                                cfg.excitation = excitation;
                                cfg.field =
                                    bsfControls(BsfMode::Solid, spread, gravity, xover, curve, m.depth, m.source);
                                const Sft::FieldChainTaps t = Sft::renderFieldChain(cfg);
                                const std::size_t n = t.outL.size();
                                INFO("SC-004(a1) " << alg.name << " spread " << spread << " gravity " << gravity
                                                   << " fc " << xover << " curve " << curve << " " << m.name
                                                   << " block " << block);
                                REQUIRE(n == Mdt::renderLength(kBsfFs, 0.1));
                                Pt::requireLREqual(t.stageOutL.data(), t.stageOutR.data(), n);
                                Pt::requireLREqual(t.outL.data(), t.outR.data(), n);
                                for (const float v : t.outL)
                                    peak = std::max(peak, static_cast<double>(std::abs(v)));
                                ++renders;
                            }
                        }
                    }
                }
            }
        }
        INFO("SC-004(a1) non-vacuity " << alg.name);
        REQUIRE(peak > 1e-3);   // the chain sounds
        std::cout << "SC004_A1," << alg.name << ",out_peak," << peak << "\n";
        std::cout << "SC004_A1," << alg.name << ",renders," << renders << ",wall_s," << bsfSeconds(t0) << "\n";
    }

    // ---- (a2) switched into SOLID: stage output L == R from crossfadeEnd + 1;
    // ---- (a3) the same renders at the limiter output: max |L - R| <= -120 dBFS from crossfadeEnd + 500 ms ----
    {
        const std::size_t switchSample = 24000;   // 0.5 s at 48 kHz
        REQUIRE(switchSample % Sft::kGrid == 0);
        const auto settle500 = static_cast<std::size_t>(std::llround(0.5 * kBsfFs));
        const std::array<BsfMode, 2> startModes{BsfMode::Spread, BsfMode::Wide};
        const std::array<float, 2> gravities{0.0f, 1.0f};
        const std::array<float, 2> crossovers{60.0f, 250.0f};
        const std::array<std::size_t, 2> blocks{64, 512};

        const std::string dir = bsfEvidenceDir();
        std::filesystem::create_directories(dir);
        const std::string csvHeader =
            "start_mode,gravity,crossover_hz,algorithm,block,switch_sample,crossfade_end,crossfade_ms,"
            "first_below_ms,settled_below_ms,max_lr_dbfs_from_500ms,a3_pass";
        std::vector<std::string> csvRows;

        const auto t0 = std::chrono::steady_clock::now();
        std::size_t renders = 0;
        double worstDb = -400.0;
        double worstSettledMs = 0.0;
        for (const BsfMode startMode : startModes) {
            for (const float gravity : gravities) {
                for (const float xover : crossovers) {
                    for (const std::size_t block : blocks) {
                        // One 2.5 s render serves (a2) (gated over the whole post-crossfade span, which
                        // contains the 1.5 s render's) and (a3).
                        Sft::FieldChainConfig cfg = bsfChainConfig(f0, kBsfFs, 2.5, block);
                        bsfSetAnchorArm(cfg, alg.algorithm);
                        cfg.excitation = excitation;
                        cfg.wideScaleOverride = 1.0f;   // WIDE offsets at the FR-041 ceilings
                        cfg.perSampleStage = true;      // modeFadePerSample (T007)
                        const BsfControls before =
                            bsfControls(startMode, 1.0f, gravity, xover, 0.5f, 0.0f, BsfSource::Drift);
                        BsfControls after = before;
                        after.mode = BsfMode::Solid;
                        cfg.field = before;
                        cfg.fieldAt = [before, after, switchSample](std::size_t s) {
                            return s < switchSample ? before : after;
                        };
                        const Sft::FieldChainTaps t = Sft::renderFieldChain(cfg);
                        const std::size_t n = t.outL.size();
                        INFO("SC-004(a2/a3) start " << bsfModeName(startMode) << " gravity " << gravity << " fc "
                                                    << xover << " " << alg.name << " block " << block);
                        REQUIRE(t.modeFadePerSample.size() == n);

                        // Non-vacuity: every upstream stage ran L != R before the switch.
                        REQUIRE_FALSE(bsfRangeBitEqual(t.stageOutL, t.stageOutR, 0, switchSample));
                        REQUIRE_FALSE(bsfRangeBitEqual(t.characterL, t.characterR, 0, switchSample));

                        std::size_t fadeEnd = n;
                        for (std::size_t i = switchSample; i < n; ++i) {
                            if (t.modeFadePerSample[i] == 0.0f) {
                                fadeEnd = i;
                                break;
                            }
                        }
                        REQUIRE(fadeEnd < n);
                        REQUIRE(fadeEnd + 1 < n);
                        // (a2)
                        Pt::requireLREqual(t.stageOutL.data() + fadeEnd + 1, t.stageOutR.data() + fadeEnd + 1,
                                           n - fadeEnd - 1);

                        // (a3)
                        const std::size_t from = fadeEnd + settle500;
                        REQUIRE(from < n);
                        double maxDiff = 0.0;
                        for (std::size_t i = from; i < n; ++i)
                            maxDiff = std::max(maxDiff, std::abs(static_cast<double>(t.outL[i]) -
                                                                 static_cast<double>(t.outR[i])));
                        std::size_t firstBelow = n;
                        std::size_t settled = fadeEnd;
                        for (std::size_t i = fadeEnd; i < n; ++i) {
                            const double d = std::abs(static_cast<double>(t.outL[i]) - static_cast<double>(t.outR[i]));
                            if (d <= kBsfLrBarLinear) {
                                if (firstBelow == n)
                                    firstBelow = i;
                            } else {
                                settled = i + 1;
                            }
                        }
                        const double fadeMs = 1000.0 * static_cast<double>(fadeEnd - switchSample) / kBsfFs;
                        const double firstBelowMs =
                            firstBelow == n ? -1.0 : 1000.0 * static_cast<double>(firstBelow - fadeEnd) / kBsfFs;
                        const double settledMs = 1000.0 * static_cast<double>(settled - fadeEnd) / kBsfFs;
                        const double maxDb = bsfDb20(maxDiff);
                        const bool pass = maxDiff <= kBsfLrBarLinear;
                        worstDb = std::max(worstDb, maxDb);
                        worstSettledMs = std::max(worstSettledMs, settledMs);
                        std::ostringstream row;
                        row << std::setprecision(6) << bsfModeName(startMode) << ',' << gravity << ',' << xover << ','
                            << alg.name << ',' << block << ',' << switchSample << ',' << fadeEnd << ',' << fadeMs
                            << ',' << firstBelowMs << ',' << settledMs << ',' << maxDb << ',' << (pass ? 1 : 0);
                        csvRows.push_back(row.str());
                        INFO("crossfade end " << fadeEnd << " (" << fadeMs << " ms), |L - R| below -120 dBFS from "
                                              << settledMs << " ms after it, max from +500 ms " << maxDb << " dBFS");
                        CHECK(maxDiff <= kBsfLrBarLinear);
                        ++renders;
                    }
                }
            }
        }
        bsfWriteA3Csv(dir + "sc004-a3-convergence.csv", csvHeader, alg.name, csvRows);
        std::cout << "SC004_A2A3," << alg.name << ",renders," << renders << ",worst_max_lr_dbfs_from_500ms," << worstDb
                  << ",worst_settled_ms," << worstSettledMs << ",wall_s," << bsfSeconds(t0)
                  << ",csv,evidence/sc004-a3-convergence.csv\n";
    }
}

}  // namespace

TEST_CASE("BassStereoField_SolidBitIdentical_Tube", "[systems][profundum][stereo-field][sc-004]") {
    REQUIRE(kBsfAlgorithms[0].algorithm == BsfAlgorithm::Tube);
    bsfSolidBitIdentical(kBsfAlgorithms[0]);
}

TEST_CASE("BassStereoField_SolidBitIdentical_Fold", "[systems][profundum][stereo-field][sc-004]") {
    REQUIRE(kBsfAlgorithms[1].algorithm == BsfAlgorithm::Fold);
    bsfSolidBitIdentical(kBsfAlgorithms[1]);
}

TEST_CASE("BassStereoField_SolidBitIdentical_Rectify", "[systems][profundum][stereo-field][sc-004]") {
    REQUIRE(kBsfAlgorithms[2].algorithm == BsfAlgorithm::Rectify);
    bsfSolidBitIdentical(kBsfAlgorithms[2]);
}

TEST_CASE("BassStereoField_SolidBitIdentical_Bite", "[systems][profundum][stereo-field][sc-004]") {
    REQUIRE(kBsfAlgorithms[3].algorithm == BsfAlgorithm::Bite);
    bsfSolidBitIdentical(kBsfAlgorithms[3]);
}

TEST_CASE("BassStereoField_SolidBitIdentical_Crush", "[systems][profundum][stereo-field][sc-004]") {
    REQUIRE(kBsfAlgorithms[4].algorithm == BsfAlgorithm::Crush);
    bsfSolidBitIdentical(kBsfAlgorithms[4]);
}

TEST_CASE("BassStereoField_SolidBitIdentical_Fracture", "[systems][profundum][stereo-field][sc-004]") {
    REQUIRE(kBsfAlgorithms[5].algorithm == BsfAlgorithm::Fracture);
    bsfSolidBitIdentical(kBsfAlgorithms[5]);
}

// The six cases above cover every entry of kBsfAlgorithms, in its order.
static_assert(std::tuple_size_v<decltype(kBsfAlgorithms)> == 6);

// =============================================================================
// SC-007: anti-aliasing of the WIDE chain (FR-078). [long]; the smoke sibling runs per push (deviation 6).
// =============================================================================
TEST_CASE("BassStereoField_WideAliasing", "[systems][profundum][stereo-field][sc-007][long]") {
    std::cout << std::setprecision(6);
    const auto t0 = std::chrono::steady_clock::now();
    const std::array<std::pair<const char*, double>, 3> notes{
        {{"E0", Sct::kE0Hz}, {"E1", Sct::kE1Hz}, {"E2", Sct::kE2Hz}}};
    const std::array<double, 3> rates{44100.0, 48000.0, 96000.0};
    for (const double fs : rates) {
        for (const auto& [noteName, f0] : notes) {
            for (const BsfAlgorithmEntry& alg : kBsfAlgorithms)
                bsfWideAliasingCell(alg, f0, noteName, fs);
        }
    }
    std::cout << "SC007,cells," << rates.size() * notes.size() * kBsfAlgorithms.size() << ",wall_s," << bsfSeconds(t0)
              << "\n";
}

// SC-007 smoke (deviation 6): Rectify and Fracture at E1, 48 kHz.
TEST_CASE("BassStereoField_WideAliasingSmoke", "[systems][profundum][stereo-field][sc-007]") {
    std::cout << std::setprecision(6);
    for (const BsfAlgorithmEntry& alg : kBsfAlgorithms) {
        if (alg.algorithm == BsfAlgorithm::Rectify || alg.algorithm == BsfAlgorithm::Fracture)
            bsfWideAliasingCell(alg, Sct::kE1Hz, "E1", kBsfFs);
    }
}

// =============================================================================
// SC-009(b): mode, Gravity and crossover steps are click-free (FR-015, FR-027, FR-052), with positive controls.
// Long-ramp form (RULED 2026-10-18, G7-1; spec SC-009(b), Clarifications "Session 2026-10-18 (build rulings)"):
//   (i)   GATE outL/outR: every |dy| from the step to the end of its settle <= 1.1 x the steady max |dy| over
//         >= 21 settings along the traversed path, endpoints included (ratio printed per cell);
//   (ii)  RECORDED outL/outR: the two-reference differential count and its positions (evidence/sc009b-clicks.csv);
//   (iii) GATE coreL/coreR: the same differential count == 0 for every step and block size;
//   positive controls (pan jump on a bare core, hard L/R switch on the chain output): counted >= 1 AND the
//   transition's max |dy| within +-N > 1.1 x the references'.
// T016: the full grid read SC009B wall_s 103.791 > 15 s per push (evidence/run-log/close/
// harvest-step6-perpush-rerun.log:1290), so it is [long]; the smoke sibling runs one cell per push (deviation 6).
// =============================================================================
namespace {

using BsfNote = std::pair<const char*, double>;

/// SC-009(b) over notes x blocks x the sequences named by index (0 SOLID>SPREAD>WIDE, 1 WIDE>SPREAD>SOLID,
/// 2 G0>G1>G0, 3 FC60>FC250>FC60). Only the held settings and the steady paths those sequences read are rendered
/// (SPREAD always: the hard-switch control). The record goes to evidence/<csvName>.
void bsfTransitionsClickFree(const std::vector<BsfNote>& notes, const std::vector<std::size_t>& blocks,
                             const std::vector<std::size_t>& sequenceIdx, const std::string& csvName) {
    std::cout << std::setprecision(6);
    const auto t0 = std::chrono::steady_clock::now();
    constexpr double kSeconds = 3.0;
    constexpr double kSteadySeconds = 2.0;
    constexpr std::size_t kPathPoints = 21;   // >= 21 settings along the traversed path, endpoints included
    constexpr double kDeltaBar = 1.1;
    const auto step1 = static_cast<std::size_t>(std::llround(1.0 * kBsfFs));
    const auto step2 = static_cast<std::size_t>(std::llround(2.0 * kBsfFs));
    const auto segSplit = static_cast<std::size_t>(std::llround(1.5 * kBsfFs));
    REQUIRE(step1 % Sft::kGrid == 0);
    REQUIRE(step2 % Sft::kGrid == 0);

    // Steady renders: from reset(), settled for Mdt::kSettleSeconds (1 s >= 0.5 s), max |dy| over [1 s, 2 s).
    const auto steadyBegin = static_cast<std::size_t>(std::llround(Mdt::kSettleSeconds * kBsfFs));
    const std::size_t steadyEnd = Mdt::renderLength(kBsfFs, kSteadySeconds);
    REQUIRE(steadyBegin >= 1);
    REQUIRE(steadyEnd >= steadyBegin + static_cast<std::size_t>(std::llround(1.0 * kBsfFs)));
    REQUIRE(steadyEnd <= Mdt::renderLength(kBsfFs, kSeconds));

    // The settle after a step (before the one period of f0): the later of the FR-015 pan slew landing
    // (ceil(1 / kMaxPanStepPerInterval) = 334 intervals), the 20 ms f_c smoother, the 10 ms mode fade and the
    // 10 ms side-gain smoother.
    const auto msSamples = [](float ms) {
        return static_cast<std::size_t>(std::ceil(static_cast<double>(ms) * 1e-3 * kBsfFs));
    };
    const auto slewIntervals = static_cast<std::size_t>(
        std::ceil(1.0 / static_cast<double>(BassStereoField::kMaxPanStepPerInterval)));
    REQUIRE(slewIntervals == 334);
    const std::size_t smootherSamples =
        std::max({slewIntervals * Sft::kGrid, msSamples(BassStereoField::kCrossoverSmoothMs),
                  msSamples(BassStereoField::kModeCrossfadeMs), msSamples(BassStereoField::kSideGainSmoothMs)});

    // The held settings (Anchor arm, Tube; Spread 1 so every step moves the image).
    const BsfControls base =
        bsfControls(BsfMode::Spread, 1.0f, 0.5f, BassStereoField::kDefaultCrossoverHz, 0.5f, 0.0f, BsfSource::Drift);
    std::array<std::pair<const char*, BsfControls>, 7> settings{};
    settings[0] = {"SOLID", base};
    settings[0].second.mode = BsfMode::Solid;
    settings[1] = {"SPREAD", base};
    settings[2] = {"WIDE", base};
    settings[2].second.mode = BsfMode::Wide;
    settings[3] = {"G0", base};
    settings[3].second.gravity = 0.0f;
    settings[4] = {"G1", base};
    settings[4].second.gravity = 1.0f;
    settings[5] = {"FC60", base};
    settings[5].second.crossoverHz = BassStereoField::kMinCrossoverHz;
    settings[6] = {"FC250", base};
    settings[6].second.crossoverHz = BassStereoField::kMaxCrossoverHz;

    // The traversed paths and the held settings at their endpoints.
    //   0 SOLID <-> SPREAD: SPREAD at Spread s_i (the slewed vector is the SPREAD vector scaled);
    //   1 SPREAD <-> WIDE:  WIDE with the offset scale K_wide * t_i (probe; the vector is unchanged, FR-044);
    //   2 Gravity 0 <-> 1;  3 crossover 60 <-> 250 Hz.
    constexpr std::size_t kPaths = 4;
    const std::array<const char*, kPaths> pathNames{"spread", "wide_scale", "gravity", "crossover"};
    const std::array<std::array<std::size_t, 2>, kPaths> pathEndpoints{{{0, 1}, {1, 2}, {3, 4}, {5, 6}}};
    const auto pathPoint = [&base](std::size_t path, std::size_t i, BsfControls& c, std::optional<float>& wideScale,
                                   double& value) {
        const float t = static_cast<float>(i) / static_cast<float>(kPathPoints - 1);
        c = base;
        wideScale.reset();
        switch (path) {
            case 0:
                c.spread = t;
                value = t;
                break;
            case 1:
                c.mode = BsfMode::Wide;
                wideScale = BassStereoField::kWideOffsetScale * t;
                value = *wideScale;
                break;
            case 2:
                c.gravity = t;
                value = t;
                break;
            default:
                c.crossoverHz = BassStereoField::kMinCrossoverHz +
                                t * (BassStereoField::kMaxCrossoverHz - BassStereoField::kMinCrossoverHz);
                value = c.crossoverHz;
                break;
        }
    };

    struct Sequence {
        const char* name;
        std::array<std::size_t, 3> idx;    // the settings before step 1, between, after step 2
        std::array<std::size_t, 2> path;   // the path each step traverses
    };
    const std::array<Sequence, 4> sequences{{{.name = "SOLID>SPREAD>WIDE", .idx = {0, 1, 2}, .path = {0, 1}},
                                             {.name = "WIDE>SPREAD>SOLID", .idx = {2, 1, 0}, .path = {1, 0}},
                                             {.name = "G0>G1>G0", .idx = {3, 4, 3}, .path = {2, 2}},
                                             {.name = "FC60>FC250>FC60", .idx = {5, 6, 5}, .path = {3, 3}}}};

    // The held settings and the steady paths the selected sequences read.
    std::array<bool, 7> needHeld{};
    needHeld[1] = true;   // SPREAD: the hard-switch control
    std::array<bool, kPaths> needPath{};
    REQUIRE(!sequenceIdx.empty());
    for (const std::size_t q : sequenceIdx) {
        REQUIRE(q < sequences.size());
        for (const std::size_t k : sequences[q].idx)
            needHeld[k] = true;
        for (const std::size_t p : sequences[q].path) {
            needPath[p] = true;
            for (const std::size_t e : pathEndpoints[p])
                needHeld[e] = true;
        }
    }

    const std::string dir = bsfEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream csv(dir + csvName);
    REQUIRE(csv.is_open());
    csv << std::setprecision(6)
        << "note,block,sequence,step,from,to,tap,channel,gated,counted,tr_det,ref_det,guarded,N,"
           "counted_positions_ms_after_step\n";

    std::size_t deltaCells = 0;
    std::size_t coreCells = 0;
    std::size_t deltaFails = 0;
    double worstRatio = 0.0;
    for (const auto& [noteName, f0] : notes) {
        const auto period = static_cast<std::size_t>(std::ceil(kBsfFs / f0));
        const std::size_t settleSamples = smootherSamples + period;

        // The steady path maxima per channel, rendered at the first block size and reused for the next one
        // only when the held renders of both block sizes are bit-identical (the chain is then block-invariant).
        std::array<std::array<double, 2>, kPaths> pathMax{};
        std::array<std::array<double, 2>, kPaths> pathArg{};
        std::vector<BsfOut> gridHeld;
        bool gridReady = false;

        for (const std::size_t block : blocks) {
            const auto render = [&, f0 = f0](const std::function<BsfControls(std::size_t)>& at, double seconds,
                                             std::optional<float> wideScale) {
                Sft::FieldChainConfig cfg = bsfChainConfig(f0, kBsfFs, seconds, block);
                bsfSetAnchorArm(cfg, BsfAlgorithm::Tube);
                cfg.field = at(0);
                cfg.fieldAt = at;
                cfg.wideScaleOverride = wideScale;
                Sft::FieldChainTaps t = Sft::renderFieldChain(cfg);
                return BsfTransitionTaps{.out = BsfOut{.L = std::move(t.outL), .R = std::move(t.outR)},
                                         .core = BsfOut{.L = std::move(t.coreL), .R = std::move(t.coreR)}};
            };
            std::vector<BsfTransitionTaps> held;
            held.reserve(settings.size());
            for (std::size_t k = 0; k < settings.size(); ++k) {
                const BsfControls c = settings[k].second;
                held.push_back(needHeld[k] ? render([c](std::size_t) { return c; }, kSeconds, std::nullopt)
                                           : BsfTransitionTaps{});
            }
            const std::size_t n = held[1].out.L.size();

            // ---- the steady maxima along every path ----
            bool reuse = gridReady;
            for (std::size_t k = 0; reuse && k < held.size(); ++k)
                reuse = Sft::bitEqual(held[k].out.L, gridHeld[k].L) && Sft::bitEqual(held[k].out.R, gridHeld[k].R);
            if (!reuse) {
                for (std::size_t p = 0; p < kPaths; ++p) {
                    if (!needPath[p])
                        continue;
                    pathMax[p] = {0.0, 0.0};
                    // endpoints included: the held endpoint settings themselves ...
                    for (const std::size_t e : pathEndpoints[p]) {
                        for (std::size_t ch = 0; ch < 2; ++ch) {
                            const std::vector<float>& y = ch == 0 ? held[e].out.L : held[e].out.R;
                            const double m = Sct::steadyDeltaMax(y.data(), steadyBegin, steadyEnd);
                            if (m > pathMax[p][ch]) {
                                pathMax[p][ch] = m;
                                pathArg[p][ch] = -1.0 - static_cast<double>(e);   // < 0: held setting e
                            }
                        }
                    }
                    // ... and the 21 settings along the path (t = 0 and t = 1 included).
                    for (std::size_t i = 0; i < kPathPoints; ++i) {
                        BsfControls c{};
                        std::optional<float> wideScale;
                        double value = 0.0;
                        pathPoint(p, i, c, wideScale, value);
                        const BsfTransitionTaps st = render([c](std::size_t) { return c; }, kSteadySeconds, wideScale);
                        REQUIRE(st.out.L.size() == steadyEnd);
                        for (std::size_t ch = 0; ch < 2; ++ch) {
                            const std::vector<float>& y = ch == 0 ? st.out.L : st.out.R;
                            const double m = Sct::steadyDeltaMax(y.data(), steadyBegin, steadyEnd);
                            std::cout << "SC009B_STEADY," << noteName << ",block," << block << ',' << pathNames[p]
                                      << ',' << value << ',' << (ch == 0 ? "outL" : "outR") << ",max_dy," << m << "\n";
                            if (m > pathMax[p][ch]) {
                                pathMax[p][ch] = m;
                                pathArg[p][ch] = value;
                            }
                        }
                    }
                }
                gridHeld.clear();
                for (const BsfTransitionTaps& h : held)
                    gridHeld.push_back(h.out);
                gridReady = true;
            }
            for (std::size_t p = 0; p < kPaths; ++p) {
                if (!needPath[p])
                    continue;
                for (std::size_t ch = 0; ch < 2; ++ch) {
                    std::cout << "SC009B_PATHMAX," << noteName << ",block," << block << ',' << pathNames[p] << ','
                              << (ch == 0 ? "outL" : "outR") << ",max_dy," << pathMax[p][ch] << ",at,";
                    if (pathArg[p][ch] < 0.0)
                        std::cout << settings[static_cast<std::size_t>(-pathArg[p][ch] - 1.0)].first;
                    else
                        std::cout << pathArg[p][ch];
                    std::cout << ",reused," << (reuse ? 1 : 0) << "\n";
                    INFO("SC-009(b) steady path max " << noteName << " block " << block << " " << pathNames[p]);
                    REQUIRE(pathMax[p][ch] > 0.0);   // non-vacuity: the chain sounds on every path
                }
            }

            for (const std::size_t q : sequenceIdx) {
                const Sequence& seq = sequences[q];
                const BsfControls a = settings[seq.idx[0]].second;
                const BsfControls b = settings[seq.idx[1]].second;
                const BsfControls c = settings[seq.idx[2]].second;
                const BsfTransitionTaps tr = render(
                    [a, b, c, step1, step2](std::size_t s) {
                        if (s < step1)
                            return a;
                        if (s < step2)
                            return b;
                        return c;
                    },
                    kSeconds,
                    std::nullopt);
                REQUIRE(tr.out.L.size() == n);
                REQUIRE(tr.core.L.size() == n);
                for (std::size_t step = 0; step < 2; ++step) {
                    const std::size_t stepSample = step == 0 ? step1 : step2;
                    const std::size_t settleEnd = stepSample + settleSamples;
                    REQUIRE(settleEnd <= n);
                    // Differential counts per step on its half of the render ([0, 1.5 s) and [1.5 s, 3 s)),
                    // phase-aligned with the references held at the step's start and end settings.
                    const std::size_t begin = step == 0 ? 0 : segSplit;
                    const std::size_t end = step == 0 ? segSplit : n;
                    const std::size_t path = seq.path[step];
                    const BsfTransitionTaps& rs = held[seq.idx[step]];
                    const BsfTransitionTaps& re = held[seq.idx[step + 1]];
                    const char* fromName = settings[seq.idx[step]].first;
                    const char* toName = settings[seq.idx[step + 1]].first;
                    for (std::size_t ch = 0; ch < 2; ++ch) {
                        const char* chName = ch == 0 ? "L" : "R";
                        INFO("SC-009(b) " << noteName << " block " << block << " " << seq.name << " step " << (step + 1)
                                          << " " << fromName << ">" << toName << " ch " << chName);

                        // (i) GATE: the sample-delta bound on the chain output.
                        const std::vector<float>& x = ch == 0 ? tr.out.L : tr.out.R;
                        const double trMax = Sct::steadyDeltaMax(x.data(), stepSample, settleEnd);
                        const double bar = pathMax[path][ch];
                        const double ratio = trMax / bar;
                        const bool deltaPass = ratio <= kDeltaBar;
                        worstRatio = std::max(worstRatio, ratio);
                        std::cout << "SC009B_DELTA," << noteName << ",block," << block << ',' << seq.name << ",step,"
                                  << (step + 1) << ',' << fromName << '>' << toName << ",out" << chName << ",window,"
                                  << stepSample << ',' << settleEnd << ",tr_max_dy," << trMax << ",steady_max_dy,"
                                  << bar << ",path," << pathNames[path] << ",ratio," << ratio << ",bar," << kDeltaBar
                                  << ',' << (deltaPass ? "PASS" : "FAIL") << "\n";
                        CHECK(ratio <= kDeltaBar);
                        if (!deltaPass)
                            ++deltaFails;
                        ++deltaCells;

                        // (ii) RECORDED on outL/R and (iii) GATED at coreL/R: the two-reference differential count.
                        for (std::size_t tap = 0; tap < 2; ++tap) {
                            const BsfOut& tx = tap == 0 ? tr.out : tr.core;
                            const BsfOut& ts = tap == 0 ? rs.out : rs.core;
                            const BsfOut& te = tap == 0 ? re.out : re.core;
                            const std::vector<float>& xt = ch == 0 ? tx.L : tx.R;
                            const std::vector<float>& ys = ch == 0 ? ts.L : ts.R;
                            const std::vector<float>& ye = ch == 0 ? te.L : te.R;
                            const Sct::ClickCount cc = Sct::differentialClicks(
                                xt.data() + begin, ys.data() + begin, ye.data() + begin, end - begin, kBsfFs, f0);
                            const std::vector<std::size_t> pos = bsfCountedPositions(
                                xt.data() + begin, ys.data() + begin, ye.data() + begin, end - begin, kBsfFs, f0);
                            CHECK(pos.size() == cc.counted);   // the record mirrors differentialClicks
                            const char* tapName = tap == 0 ? "out" : "core";
                            csv << noteName << ',' << block << ',' << seq.name << ',' << (step + 1) << ',' << fromName
                                << ',' << toName << ',' << tapName << ',' << chName << ',' << (tap == 0 ? 0 : 1) << ','
                                << cc.counted << ',' << cc.transitionDetections << ',' << cc.referenceDetections << ','
                                << cc.guardedOut << ',' << cc.windowN << ',';
                            for (std::size_t k = 0; k < pos.size(); ++k) {
                                const double ms =
                                    1000.0 * (static_cast<double>(begin + pos[k]) - static_cast<double>(stepSample)) /
                                    kBsfFs;
                                csv << (k > 0 ? ";" : "") << ms;
                            }
                            csv << '\n';
                            std::cout << "SC009B_COUNT," << noteName << ",block," << block << ',' << seq.name
                                      << ",step," << (step + 1) << ',' << fromName << '>' << toName << ',' << tapName
                                      << chName << ",counted," << cc.counted << ",tr_det," << cc.transitionDetections
                                      << ",ref_det," << cc.referenceDetections << ",guarded," << cc.guardedOut << ",N,"
                                      << cc.windowN << ',' << (tap == 0 ? "RECORDED" : "GATED") << "\n";
                            if (tap == 1) {
                                CHECK(cc.counted == 0);
                                ++coreCells;
                            }
                        }
                    }
                }
            }

            // Positive control 2 (hard switch): L/R swapped at one sample of the held SPREAD chain render, at the
            // grid point in [1 s, 2 s) bsfControlGridPoint picks. References: the unswapped and the swapped render.
            {
                const BsfOut& s = held[1].out;
                const std::size_t g = bsfControlGridPoint(s.L, s.R, s.R, s.L, step1, step2, kBsfFs, f0);
                std::vector<float> swL(s.L);
                std::vector<float> swR(s.R);
                for (std::size_t i = g; i < n; ++i) {
                    swL[i] = s.R[i];
                    swR[i] = s.L[i];
                }
                const Sct::ClickCount cL = Sct::differentialClicks(swL.data(), s.L.data(), s.R.data(), n, kBsfFs, f0);
                const Sct::ClickCount cR = Sct::differentialClicks(swR.data(), s.R.data(), s.L.data(), n, kBsfFs, f0);
                double trL = 0.0;
                double refL = 0.0;
                double trR = 0.0;
                double refR = 0.0;
                const bool standsL = bsfStepStandsOut(swL, s.L, s.R, g, kBsfFs, f0, trL, refL);
                const bool standsR = bsfStepStandsOut(swR, s.R, s.L, g, kBsfFs, f0, trR, refR);
                const bool detected = cL.counted >= 1 && cR.counted >= 1 && standsL && standsR;
                std::cout << "SC009B_CONTROL,hard_switch,Tube," << noteName << ",block," << block << ",at," << g
                          << ",countedL," << cL.counted << ",countedR," << cR.counted << ",dyL," << trL << ",refL,"
                          << refL << ",dyR," << trR << ",refR," << refR << ',' << (detected ? "DETECTED" : "MISSED")
                          << "\n";
                INFO("SC-009(b) positive control hard switch " << noteName << " block " << block << " at " << g);
                CHECK(cL.counted >= 1);
                CHECK(cR.counted >= 1);
                CHECK(standsL);
                CHECK(standsR);
            }
        }

        // Positive control 1 (unsmoothed full-range step): a hand-made pan vector jump 0 -> +-1 (odd harmonics
        // -1 = left, even +1 = right, h1 included) handed to a bare core, the field bypassed, at the grid point
        // in [0.9 s, 1.1 s) bsfControlGridPoint picks. References: the core held at each vector.
        {
            const std::array<float, kMaxPartials> zero{};
            std::array<float, kMaxPartials> jump{};
            for (std::size_t k = 0; k < kMaxPartials; ++k)
                jump[k] = (k % 2 == 0) ? -1.0f : 1.0f;
            const BsfCoreRender a =
                bsfCoreRender(f0, kBsfFs, kSeconds, 512, [&zero](std::size_t) -> const std::array<float, kMaxPartials>& {
                    return zero;
                });
            const BsfCoreRender b =
                bsfCoreRender(f0, kBsfFs, kSeconds, 512, [&jump](std::size_t) -> const std::array<float, kMaxPartials>& {
                    return jump;
                });
            const std::size_t lo = static_cast<std::size_t>(std::llround(0.9 * kBsfFs));
            const std::size_t hi = static_cast<std::size_t>(std::llround(1.1 * kBsfFs));
            const std::size_t g = bsfControlGridPoint(a.L, a.R, b.L, b.R, lo, hi, kBsfFs, f0);
            const BsfCoreRender tr = bsfCoreRender(
                f0, kBsfFs, kSeconds, 512,
                [&zero, &jump, g](std::size_t s) -> const std::array<float, kMaxPartials>& {
                    return s < g ? zero : jump;
                });
            const std::size_t n = tr.L.size();
            const Sct::ClickCount cL = Sct::differentialClicks(tr.L.data(), a.L.data(), b.L.data(), n, kBsfFs, f0);
            const Sct::ClickCount cR = Sct::differentialClicks(tr.R.data(), a.R.data(), b.R.data(), n, kBsfFs, f0);
            double trL = 0.0;
            double refL = 0.0;
            double trR = 0.0;
            double refR = 0.0;
            const bool standsL = bsfStepStandsOut(tr.L, a.L, b.L, g, kBsfFs, f0, trL, refL);
            const bool standsR = bsfStepStandsOut(tr.R, a.R, b.R, g, kBsfFs, f0, trR, refR);
            const bool detected = cL.counted >= 1 && cR.counted >= 1 && standsL && standsR;
            std::cout << "SC009B_CONTROL,pan_jump,core," << noteName << ",at," << g << ",countedL," << cL.counted
                      << ",countedR," << cR.counted << ",dyL," << trL << ",refL," << refL << ",dyR," << trR
                      << ",refR," << refR << ',' << (detected ? "DETECTED" : "MISSED") << "\n";
            INFO("SC-009(b) positive control pan jump " << noteName << " at " << g);
            CHECK(cL.counted >= 1);
            CHECK(cR.counted >= 1);
            CHECK(standsL);
            CHECK(standsR);
        }
    }
    std::cout << "SC009B,delta_cells," << deltaCells << ",delta_fails," << deltaFails << ",worst_ratio," << worstRatio
              << ",bar," << kDeltaBar << ",core_count_cells," << coreCells << ",csv,evidence/" << csvName << ",wall_s,"
              << bsfSeconds(t0) << "\n";
}

}  // namespace

TEST_CASE("BassStereoField_TransitionsClickFree", "[systems][profundum][stereo-field][sc-009][long]") {
    bsfTransitionsClickFree({{"E0", Sct::kE0Hz}, {"E2", Sct::kE2Hz}, {"C3", Sct::kC3Hz}}, {64, 512}, {0, 1, 2, 3},
                            "sc009b-clicks.csv");
}

// SC-009(b) smoke (deviation 6): one cell, E2, block 512, G0>G1>G0 (one steady path), both positive controls.
TEST_CASE("BassStereoField_TransitionsClickFreeSmoke", "[systems][profundum][stereo-field][sc-009]") {
    bsfTransitionsClickFree({{"E2", Sct::kE2Hz}}, {512}, {2}, "sc009b-clicks-smoke.csv");
}

// =============================================================================
// SC-018 rendered: h1 centred and the a1 feed exact (FR-013, FR-061).
// T016: the full grid read SC018 wall_s 16.3986 > 15 s per push (evidence/run-log/close/
// harvest-step6-perpush-rerun.log:1327), so it is [long]; the smoke sibling runs per push (deviation 6).
// =============================================================================
namespace {

void bsfH1Centred(const std::vector<BsfNote>& notes, const std::vector<float>& spreads) {
    std::cout << std::setprecision(6);
    const auto t0 = std::chrono::steady_clock::now();
    constexpr double kSeconds = 3.0;
    const auto settle = static_cast<std::size_t>(std::llround(Mdt::kSettleSeconds * kBsfFs));
    const std::array<BsfMode, 3> modes{BsfMode::Solid, BsfMode::Spread, BsfMode::Wide};

    struct Reading {
        double h1L = 0.0, h1R = 0.0;     // goertzel amplitudes on coreL / coreR
        double expected = 0.0;           // mean deliveredGains()[0] * kCenterPanGain over the window
        double h1MPow = 0.0;             // h1 power in M at the core tap
        double anchorPow = 0.0;          // mean square of anchorL
        double subMPow = 0.0;            // post-character M power in [0, 2 f0)
    };

    for (const auto& [noteName, f0] : notes) {
        const auto measure = [&, f0 = f0](BsfMode mode, float spread) {
            Sft::FieldChainConfig cfg = bsfChainConfig(f0, kBsfFs, kSeconds, 512);
            bsfSetAnchorArm(cfg, BsfAlgorithm::Tube);
            cfg.field = bsfControls(mode, spread, 0.5f, BassStereoField::kDefaultCrossoverHz, 0.5f, 0.0f,
                                    BsfSource::Drift);
            const Sft::FieldChainTaps t = Sft::renderFieldChain(cfg);
            const std::size_t n = t.coreL.size();
            REQUIRE(n > settle + Sft::kGrid);
            const std::size_t len = bsfWholePeriodLength(kBsfFs, f0, n - settle - Sft::kGrid);
            Reading r;
            r.h1L = Sft::goertzelComplex(t.coreL.data() + settle, len, kBsfFs, f0).amplitude;
            r.h1R = Sft::goertzelComplex(t.coreR.data() + settle, len, kBsfFs, f0).amplitude;
            const std::size_t q0 = settle / Sft::kGrid;
            const std::size_t q1 = std::min((settle + len) / Sft::kGrid, t.h1DeliveredPerInterval.size());
            REQUIRE(q1 > q0);
            double sum = 0.0;
            for (std::size_t q = q0; q < q1; ++q)
                sum += static_cast<double>(t.h1DeliveredPerInterval[q]);
            r.expected = sum / static_cast<double>(q1 - q0) * static_cast<double>(SpectralShapeRecipe::kCenterPanGain);
            const std::vector<float> m = bsfMid(t.coreL, t.coreR);
            const double h1M = Sft::goertzelComplex(m.data() + settle, len, kBsfFs, f0).amplitude;
            r.h1MPow = h1M * h1M;
            r.anchorPow = bsfMeanSquare(t.anchorL.data() + settle, len);
            const std::vector<float> cm = bsfMid(t.characterL, t.characterR);
            r.subMPow = Sft::bandReading(cm.data() + settle, cm.data() + settle, n - settle, kBsfFs, 0.0, 2.0 * f0)
                            .meanEnergy;
            return r;
        };

        const Reading ref = measure(BsfMode::Spread, 0.0f);
        INFO("SC-018 " << noteName << " Spread 0 reference");
        REQUIRE(ref.expected > 0.0);
        REQUIRE(ref.h1MPow > 0.0);
        REQUIRE(ref.anchorPow > 1e-8);   // the Anchor arm sounds
        for (const BsfMode mode : modes) {
            for (const float spread : spreads) {
                const Reading r = measure(mode, spread);
                const double dL = bsfDb20(r.h1L / r.expected);
                const double dR = bsfDb20(r.h1R / r.expected);
                const double dM = Pt::safeDb10(r.h1MPow / ref.h1MPow);
                const double dA = Pt::safeDb10(r.anchorPow / ref.anchorPow);
                const double subDb = Pt::safeDb10(r.subMPow);
                const double subDelta = Pt::safeDb10(r.subMPow / std::max(ref.subMPow, 1e-300));
                std::cout << "SC018," << noteName << ',' << bsfModeName(mode) << ",spread," << spread
                          << ",h1L_vs_delivered_db," << dL << ",h1R_vs_delivered_db," << dR << ",h1M_vs_spread0_db,"
                          << dM << ",anchor_vs_spread0_db," << dA << ",post_character_subM_dbfs," << subDb
                          << ",subM_vs_spread0_db," << subDelta << " (recorded; WIDE vs Phase 5 -0.56..-0.61 dB)\n";
                INFO("SC-018 " << noteName << " " << bsfModeName(mode) << " spread " << spread << ": h1L " << dL
                               << " dB, h1R " << dR << " dB, M " << dM << " dB, Anchor " << dA << " dB");
                CHECK(std::abs(dL) <= kBsfH1DeliveredBarDb);
                CHECK(std::abs(dR) <= kBsfH1DeliveredBarDb);
                CHECK(std::abs(dM) <= kBsfH1SpreadBarDb);
                CHECK(std::abs(dA) <= kBsfH1SpreadBarDb);
            }
        }
    }
    std::cout << "SC018,wall_s," << bsfSeconds(t0) << "\n";
}

}  // namespace

TEST_CASE("BassStereoField_H1Centred", "[systems][profundum][stereo-field][sc-018][long]") {
    bsfH1Centred({{"E0", Sct::kE0Hz},
                  {"E1", Sct::kE1Hz},
                  {"C2", Sct::kC2Hz},
                  {"E2", Sct::kE2Hz},
                  {"C3", Sct::kC3Hz},
                  {"C4", Sct::noteHz(60)}},
                 {0.5f, 1.0f});
}

// SC-018 smoke (deviation 6): E1, every mode, Spread 1 (against the Spread 0 reference).
TEST_CASE("BassStereoField_H1CentredSmoke", "[systems][profundum][stereo-field][sc-018]") {
    bsfH1Centred({{"E1", Sct::kE1Hz}}, {1.0f});
}

// =============================================================================
// SC-019 rendered: pan only, no inter-channel phase offset, pan alone cannot cancel (FR-014; plan S1 C-14).
// T016: the full grid read SC019 wall_s 18.7473 > 15 s per push (evidence/run-log/close/
// harvest-step6-perpush-rerun.log:1352), so it is [long]; the smoke sibling runs per push (deviation 6).
// =============================================================================
namespace {

struct BsfMotion {
    const char* name;
    float depth;
    BsfSource source;
};

void bsfPanOnlyDirection(const std::vector<BsfNote>& notes, const std::vector<BsfMode>& modes,
                         const std::vector<float>& spreads, const std::vector<BsfMotion>& motions) {
    std::cout << std::setprecision(6);
    const auto t0 = std::chrono::steady_clock::now();
    constexpr std::size_t kFrame = Krate::DSP::TestUtils::kLowFrequencyFftSize;
    const auto settle = static_cast<std::size_t>(std::llround(Mdt::kSettleSeconds * kBsfFs));
    const int N = ProfundumCore::kDefaultPartials;

    for (const auto& [noteName, f0] : notes) {
        for (const BsfMode mode : modes) {
            for (const float spread : spreads) {
                for (const BsfMotion& m : motions) {
                    // The clean core (rule 10), 6.46 s (descriptorSeconds), read at coreL/R.
                    Sft::FieldChainConfig cfg = bsfChainConfig(f0, kBsfFs, 0.0, 512);
                    cfg.field = bsfControls(mode, spread, 0.5f, BassStereoField::kDefaultCrossoverHz, 0.5f, m.depth,
                                            m.source);
                    const Sft::FieldChainTaps t = Sft::renderFieldChain(cfg);
                    const std::size_t n = t.coreL.size();
                    REQUIRE(n >= settle + kFrame);
                    REQUIRE_FALSE(Sft::bitEqual(t.coreL, t.coreR));   // the field pans this render

                    const std::vector<float> mid = bsfMid(t.coreL, t.coreR);
                    const std::vector<double> wL = Pt::renderedHarmonicPowers(t.coreL.data() + settle, kBsfFs, f0, N);
                    const std::vector<double> wR = Pt::renderedHarmonicPowers(t.coreR.data() + settle, kBsfFs, f0, N);
                    const std::vector<double> wM = Pt::renderedHarmonicPowers(mid.data() + settle, kBsfFs, f0, N);
                    double strongest = 0.0;
                    for (std::size_t k = 0; k < wL.size(); ++k)
                        strongest = std::max({strongest, wL[k], wR[k]});
                    REQUIRE(strongest > 0.0);

                    // Two phase windows: the start and the end of the descriptor frame.
                    const std::array<std::size_t, 2> starts{settle, settle + kFrame - kBsfPhaseWindow};
                    std::size_t withEnergy = 0;
                    std::size_t phaseChecked = 0;
                    std::size_t oneSidedSkipped = 0;
                    double worstPhase = 0.0;
                    double worstQ = 0.0;
                    for (int h = 1; h <= N; ++h) {
                        const auto k = static_cast<std::size_t>(h - 1);
                        const double hz = static_cast<double>(h) * f0;
                        if (!(hz < 0.5 * kBsfFs))
                            continue;
                        const double stronger = std::max(wL[k], wR[k]);
                        const double weaker = std::min(wL[k], wR[k]);
                        if (!(stronger >= kBsfHarmonicEnergyFloor * strongest))
                            continue;
                        ++withEnergy;
                        const double q = Pt::safeDb10(wM[k] / (0.5 * (wL[k] + wR[k])));
                        worstQ = std::min(worstQ, q);
                        INFO("SC-019 " << noteName << " " << bsfModeName(mode) << " spread " << spread << " " << m.name
                                       << " harmonic " << h << ": Q " << q << " dB");
                        CHECK(q >= kBsfHarmonicQBarDb);
                        // plan S1 C-14: a one-sided harmonic (weaker channel > 60 dB down) has no meaningful phase.
                        if (!(weaker >= kBsfOneSidedPowerRatio * stronger)) {
                            ++oneSidedSkipped;
                            continue;
                        }
                        for (const std::size_t s : starts) {
                            const double pL = Pt::goertzelPhase(t.coreL.data() + s, kBsfPhaseWindow, kBsfFs, hz);
                            const double pR = Pt::goertzelPhase(t.coreR.data() + s, kBsfPhaseWindow, kBsfFs, hz);
                            const double d = std::abs(Sft::wrapPhase(pL - pR));
                            worstPhase = std::max(worstPhase, d);
                            INFO("window at " << s << ": |phase L - phase R| " << d << " rad");
                            CHECK(d <= kBsfPhaseBarRad);
                            ++phaseChecked;
                        }
                    }
                    std::cout << "SC019," << noteName << ',' << bsfModeName(mode) << ",spread," << spread << ','
                              << m.name << ",harmonics_with_energy," << withEnergy << ",phase_checks," << phaseChecked
                              << ",one_sided_skipped," << oneSidedSkipped << ",worst_phase_rad," << worstPhase
                              << ",worst_Q_db," << worstQ << "\n";
                    INFO("SC-019 non-vacuity " << noteName << " " << bsfModeName(mode) << " spread " << spread << " "
                                               << m.name);
                    REQUIRE(withEnergy > 0);
                    REQUIRE(phaseChecked > 0);
                }
            }
        }
    }
    std::cout << "SC019,wall_s," << bsfSeconds(t0) << "\n";
}

}  // namespace

TEST_CASE("BassStereoField_PanOnlyDirection", "[systems][profundum][stereo-field][sc-019][long]") {
    bsfPanOnlyDirection({{"E1", Sct::kE1Hz}, {"C2", Sct::kC2Hz}}, {BsfMode::Spread, BsfMode::Wide}, {0.5f, 1.0f},
                        {{.name = "depth0", .depth = 0.0f, .source = BsfSource::Drift},
                         {.name = "drift1", .depth = 1.0f, .source = BsfSource::Drift},
                         {.name = "cycle1", .depth = 1.0f, .source = BsfSource::Cycle}});
}

// SC-019 smoke (deviation 6): E1, WIDE, Spread 1, MOTION 1 Cycle.
TEST_CASE("BassStereoField_PanOnlyDirectionSmoke", "[systems][profundum][stereo-field][sc-019]") {
    bsfPanOnlyDirection({{"E1", Sct::kE1Hz}}, {BsfMode::Wide}, {1.0f},
                        {{.name = "cycle1", .depth = 1.0f, .source = BsfSource::Cycle}});
}
