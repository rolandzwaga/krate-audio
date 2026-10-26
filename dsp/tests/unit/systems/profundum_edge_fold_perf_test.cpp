// ==============================================================================
// Layer 3: System Tests - Profundum excitation + HarmonicFold, perf TU
//                                    (Profundum Phase 2, specs/profundum-phase2-edge-fold)
// ==============================================================================
// Reference: specs/profundum-phase2-edge-fold/plan.md (S8.1, S8.2, S9.5, S11 R-1)
//            specs/profundum-phase2-edge-fold/tasks.md (T006 wires this TU,
//                                                       T013 fills it)
//
// SCOPE OF THIS TU (plan S8.2): SC-018 (ProfundumEdgeFold_CpuBudget) [.perf].
//   Deliberately NOT in the -fno-fast-math block: its figures must reflect
//   the shipping FP mode. Run alone and pinned, nothing else executing:
//   pwsh -NoProfile -File tools/pin-perf-cores.ps1 \
//        -Exe build/windows-x64-release/bin/Release/dsp_systems_tests.exe \
//        -ExeArgs "ProfundumEdgeFold_CpuBudget"
// ==============================================================================

#include <krate/dsp/core/db_utils.h>                         // detail::isFinite
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/harmonic_types.h>             // kMaxPartials
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/profundum_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

using namespace Krate::DSP;

namespace {

// ---- Phase 1 SC-013 harness constants (copied; profundum_core_perf_test.cpp is frozen) ----
constexpr double kEfPerfSampleRate = 48000.0;
constexpr std::size_t kEfPerfBlock = 64;            // host block
constexpr int kEfPerfPartials = 64;
constexpr double kEfPerfSeconds = 10.0;             // audio per run
constexpr int kEfPerfTimedRuns = 5;                 // median of 5, after one warm-up
constexpr double kEfSweepRateHz = 2.0;              // full-range D/B/E/S triangles
constexpr double kEfVibratoRateHz = 5.0;
constexpr double kEfVibratoDepthSemitones = 2.0;    // ±2 semitones
constexpr double kEfTwoPi = 6.283185307179586476925286766559;

// ---- SC-018 stage automation ----
constexpr double kEfDriveAutomationHz = 0.5;        // excitation drive: 0.5 Hz sine, 0…1
constexpr double kEfFoldAutomationHz = 0.5;         // Fold amount: 0.5 Hz sine, 0…100

/// C2 = MIDI 36.
inline double efC2Hz() { return 440.0 * std::pow(2.0, (36.0 - 69.0) / 12.0); }

/// Unipolar triangle in [0, 1]: 0 at phase 0, 1 at phase 1/2.
inline double efTriangle01(double phase) {
    const double p = phase - std::floor(phase);
    return p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p;
}

/// The whole run's control, pitch and stage automation, built before timing so the timed loop
/// is the DSP only.
struct EfPerfLoad {
    std::vector<SpectralShapeRecipe::Controls> perBlock;   // core controls, set before every block
    std::vector<float> f0;                                 // per-sample vibrato trajectory
    std::vector<float> drive;                              // per-block excitation drive, 0…1
    std::vector<float> foldAmount;                         // per-block Fold amount, 0…100
};

/// Phase 1 SC-013's load (D/B/E/S 2 Hz triangles + 5 Hz ±2-semitone vibrato), copied from
/// profundum_core_perf_test.cpp makeSweepLoad, plus the SC-018 stage automation.
inline EfPerfLoad makeEdgeFoldLoad(std::size_t totalSamples) {
    EfPerfLoad load;
    const std::size_t blocks = (totalSamples + kEfPerfBlock - 1) / kEfPerfBlock;
    load.perBlock.reserve(blocks);
    load.drive.reserve(blocks);
    load.foldAmount.reserve(blocks);
    for (std::size_t b = 0; b < blocks; ++b) {
        const double t = static_cast<double>(b * kEfPerfBlock) / kEfPerfSampleRate;
        const double ph = kEfSweepRateHz * t;
        SpectralShapeRecipe::Controls c = SpectralShapeRecipe::kDefaultControls;
        c.depth = static_cast<float>(efTriangle01(ph + 0.0));
        c.body = static_cast<float>(efTriangle01(ph + 0.25));
        c.edge = static_cast<float>(efTriangle01(ph + 0.5));
        c.shift = static_cast<float>(-1.0 + 2.0 * efTriangle01(ph + 0.75));
        load.perBlock.push_back(c);
        load.drive.push_back(static_cast<float>(0.5 + 0.5 * std::sin(kEfTwoPi * kEfDriveAutomationHz * t)));
        load.foldAmount.push_back(
            static_cast<float>(50.0 + 50.0 * std::sin(kEfTwoPi * kEfFoldAutomationHz * t)));
    }
    load.f0.resize(totalSamples);
    const double f0 = efC2Hz();
    for (std::size_t i = 0; i < totalSamples; ++i) {
        const double t = static_cast<double>(i) / kEfPerfSampleRate;
        const double semis = kEfVibratoDepthSemitones * std::sin(kEfTwoPi * kEfVibratoRateHz * t);
        load.f0[i] = static_cast<float>(f0 * std::pow(2.0, semis / 12.0));
    }
    return load;
}

/// Which stages a configuration runs (SC-018 A…D, plus the stereo-forced record variant).
struct EfConfig {
    const char* name;
    bool excitation;   // mix 1, asymmetry 1, drive automated
    bool fold;         // HarmonicFold on the core output, amount automated
    bool stereo;       // non-zero pan vector: L != R through excitation and Fold
};

/// SC-013(b)-style pan vector: 0 on h1, +0.5 on even n, -0.5 on odd n >= 3.
inline std::array<float, kMaxPartials> efStereoPan() {
    std::array<float, kMaxPartials> pan{};
    for (std::size_t k = 1; k < kMaxPartials; ++k) {
        const std::size_t n = k + 1;   // harmonic number
        pan[k] = (n % 2 == 0) ? 0.5f : -0.5f;
    }
    return pan;
}

/// One run: Reset-policy noteOn at C2, then kEfPerfSeconds of audio in host blocks. Returns the
/// wall-clock seconds and accumulates the output into `checksum`.
inline double efTimedRun(ProfundumCore& core, HarmonicFold& fold, const EfConfig& cfg,
                         const EfPerfLoad& load, std::size_t totalSamples,
                         std::array<float, kEfPerfBlock>& L, std::array<float, kEfPerfBlock>& R,
                         double& checksum) {
    core.setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
    core.setControls(load.perBlock.front());
    if (cfg.excitation)
        core.setExcitation(ProfundumCore::ExcitationControls{
            .mix = 1.0f, .drive = load.drive.front(), .asymmetry = 1.0f});
    core.noteOn(static_cast<float>(efC2Hz()));
    if (cfg.fold) {
        fold.setAmount(load.foldAmount.front());
        fold.reset();
    }

    const auto start = std::chrono::steady_clock::now();
    std::size_t block = 0;
    for (std::size_t pos = 0; pos < totalSamples; pos += kEfPerfBlock, ++block) {
        const std::size_t n = std::min(kEfPerfBlock, totalSamples - pos);
        core.setControls(load.perBlock[block]);
        if (cfg.excitation)   // one setExcitation per block
            core.setExcitation(ProfundumCore::ExcitationControls{
                .mix = 1.0f, .drive = load.drive[block], .asymmetry = 1.0f});
        core.processBlock(L.data(), R.data(), n, load.f0.data() + pos);
        if (cfg.fold) {
            fold.setAmount(load.foldAmount[block]);
            fold.processBlock(L.data(), R.data(), n);
        }
        for (std::size_t k = 0; k < n; ++k)
            checksum += static_cast<double>(L[k]) + static_cast<double>(R[k]);
    }
    const auto stop = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(stop - start).count();
}

/// Warm-up + kEfPerfTimedRuns timed runs; returns median(wall) / kEfPerfSeconds × 100 and prints it.
inline double efMeasurePct(const EfConfig& cfg, const EfPerfLoad& load, std::size_t totalSamples,
                           double& checksum) {
    ProfundumCore core;
    core.prepare(kEfPerfSampleRate, kEfPerfPartials);
    REQUIRE(core.isPrepared());
    REQUIRE(core.numPartials() == kEfPerfPartials);
    if (cfg.stereo)
        core.setPartialPanOffsets(efStereoPan());

    HarmonicFold fold;
    fold.setFundamentalHz(static_cast<float>(efC2Hz()));   // the played f0
    fold.prepare(kEfPerfSampleRate, kEfPerfBlock);

    std::array<float, kEfPerfBlock> L{};
    std::array<float, kEfPerfBlock> R{};

    (void)efTimedRun(core, fold, cfg, load, totalSamples, L, R, checksum);   // warm-up
    std::vector<double> walls;
    walls.reserve(static_cast<std::size_t>(kEfPerfTimedRuns));
    for (int run = 0; run < kEfPerfTimedRuns; ++run)
        walls.push_back(efTimedRun(core, fold, cfg, load, totalSamples, L, R, checksum));

    REQUIRE(core.stateFinite());
    if (cfg.fold)
        REQUIRE(fold.stateFinite());

    std::sort(walls.begin(), walls.end());
    const double median = walls[walls.size() / 2];
    const double pct = median / kEfPerfSeconds * 100.0;
    std::printf("PROFUNDUM_EDGEFOLD_PERF config=%s pct=%.3f\n", cfg.name, pct);
    return pct;
}

}  // namespace

// SC-018 (plan S8.2, S9.5; FR-054). Do NOT run concurrently with anything else; run pinned.
TEST_CASE("ProfundumEdgeFold_CpuBudget", "[systems][profundum][edge-fold][.perf]") {
    const auto totalSamples = static_cast<std::size_t>(std::llround(kEfPerfSeconds * kEfPerfSampleRate));
    const EfPerfLoad load = makeEdgeFoldLoad(totalSamples);
    double checksum = 0.0;

    const EfConfig cfgA{"A_core_mix0", false, false, false};
    const EfConfig cfgB{"B_core_excitation", true, false, false};
    const EfConfig cfgC{"C_core_fold", false, true, false};
    const EfConfig cfgD{"D_core_excitation_fold", true, true, false};
    const EfConfig cfgDs{"D_stereo_forced", true, true, true};

    const double pctA = efMeasurePct(cfgA, load, totalSamples, checksum);
    const double pctB = efMeasurePct(cfgB, load, totalSamples, checksum);
    const double pctC = efMeasurePct(cfgC, load, totalSamples, checksum);
    const double pctD = efMeasurePct(cfgD, load, totalSamples, checksum);
    const double pctDs = efMeasurePct(cfgDs, load, totalSamples, checksum);   // record only

    const double stageCost = pctD - pctA;
    std::printf("PROFUNDUM_EDGEFOLD_PERF excitation_alone=%.3f fold_alone=%.3f stage_cost=%.3f "
                "total=%.3f stereo_forced_total=%.3f stereo_forced_stage_cost=%.3f\n",
                pctB - pctA, pctC - pctA, stageCost, pctD, pctDs, pctDs - pctA);

    REQUIRE(detail::isFinite(checksum));   // keeps the render from being elided
    CHECK(pctA <= 1.0);        // core alone (mix 0)
    CHECK(stageCost <= 1.5);   // [(D) − (A)] / audio
    CHECK(pctD <= 2.5);        // whole chain / audio
}
