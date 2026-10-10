// ==============================================================================
// Layer 3: System Tests - ProfundumCore, perf TU
//                                    (specs/profundum-phase1-harmonic-core)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/profundum-phase1-harmonic-core/spec.md
//            specs/profundum-phase1-harmonic-core/plan.md  (S8.1, S8.3, S9.3)
//            specs/profundum-phase1-harmonic-core/tasks.md (T003 creates and
//                                                           wires this TU; T015
//                                                           fills it)
//
// SCOPE OF THIS TU (plan S8.1; tags [systems][profundum][.perf]): SC-013 and the
//   OQ-3 partial-count figures. Stays OUT of the -fno-fast-math block so its
//   figures reflect the shipping FP mode. Run alone:
//   node tools/run-cpu-tests.js dsp_systems_tests
// ==============================================================================

#include <krate/dsp/core/db_utils.h>                         // detail::isFinite
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

constexpr double kPerfSampleRate = 48000.0;
constexpr std::size_t kPerfBlock = 64;              // host block (SC-013)
constexpr double kPerfSeconds = 10.0;               // audio per run
constexpr int kPerfTimedRuns = 5;                   // median of 5, after one warm-up
constexpr double kSweepRateHz = 2.0;                // full-range D/B/E/S triangles
constexpr double kVibratoRateHz = 5.0;
constexpr double kVibratoDepthSemitones = 2.0;      // ±2 semitones
constexpr double kPerfTwoPi = 6.283185307179586476925286766559;

/// C2 = MIDI 36.
double c2Hz() { return 440.0 * std::pow(2.0, (36.0 - 69.0) / 12.0); }

/// Unipolar triangle in [0, 1]: 0 at phase 0, 1 at phase 1/2.
double triangle01(double phase) {
    const double p = phase - std::floor(phase);
    return p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p;
}

/// The whole run's control and pitch input, built before timing so the timed loop is the core only.
struct PerfLoad {
    std::vector<SpectralShapeRecipe::Controls> perBlock;   // set before every block
    std::vector<float> f0;                                 // empty -> held f0, nothing changing
};

PerfLoad makeSweepLoad(std::size_t totalSamples) {
    PerfLoad load;
    const std::size_t blocks = (totalSamples + kPerfBlock - 1) / kPerfBlock;
    load.perBlock.reserve(blocks);
    for (std::size_t b = 0; b < blocks; ++b) {
        const double t = static_cast<double>(b * kPerfBlock) / kPerfSampleRate;
        const double ph = kSweepRateHz * t;
        SpectralShapeRecipe::Controls c = SpectralShapeRecipe::kDefaultControls;
        c.depth = static_cast<float>(triangle01(ph + 0.0));
        c.body = static_cast<float>(triangle01(ph + 0.25));
        c.edge = static_cast<float>(triangle01(ph + 0.5));
        c.shift = static_cast<float>(-1.0 + 2.0 * triangle01(ph + 0.75));
        load.perBlock.push_back(c);
    }
    load.f0.resize(totalSamples);
    const double f0 = c2Hz();
    for (std::size_t i = 0; i < totalSamples; ++i) {
        const double t = static_cast<double>(i) / kPerfSampleRate;
        const double semis = kVibratoDepthSemitones * std::sin(kPerfTwoPi * kVibratoRateHz * t);
        load.f0[i] = static_cast<float>(f0 * std::pow(2.0, semis / 12.0));
    }
    return load;
}

PerfLoad makeStaticLoad(std::size_t totalSamples) {
    PerfLoad load;
    const std::size_t blocks = (totalSamples + kPerfBlock - 1) / kPerfBlock;
    load.perBlock.assign(blocks, SpectralShapeRecipe::kDefaultControls);
    return load;
}

/// One run: Reset-policy noteOn at C2, then kPerfSeconds of audio in host blocks. Returns the
/// wall-clock seconds and accumulates the output into `checksum`.
double timedRun(ProfundumCore& core, const PerfLoad& load, std::size_t totalSamples,
                std::array<float, kPerfBlock>& L, std::array<float, kPerfBlock>& R,
                double& checksum) {
    core.setRetriggerPhase(ProfundumCore::RetriggerPhase::Reset);
    core.setControls(load.perBlock.front());
    core.noteOn(static_cast<float>(c2Hz()));

    const auto start = std::chrono::steady_clock::now();
    std::size_t block = 0;
    for (std::size_t pos = 0; pos < totalSamples; pos += kPerfBlock, ++block) {
        const std::size_t n = std::min(kPerfBlock, totalSamples - pos);
        core.setControls(load.perBlock[block]);
        core.processBlock(L.data(), R.data(), n, load.f0.empty() ? nullptr : load.f0.data() + pos);
        for (std::size_t k = 0; k < n; ++k)
            checksum += static_cast<double>(L[k]) + static_cast<double>(R[k]);
    }
    const auto stop = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(stop - start).count();
}

/// Warm-up + kPerfTimedRuns timed runs; returns median(wall) / kPerfSeconds × 100 and prints it.
double measurePct(int partials, const char* loadName, const PerfLoad& load, std::size_t totalSamples,
                  double& checksum) {
    ProfundumCore core;
    core.prepare(kPerfSampleRate, partials);
    REQUIRE(core.isPrepared());
    REQUIRE(core.numPartials() == partials);

    std::array<float, kPerfBlock> L{};
    std::array<float, kPerfBlock> R{};

    (void)timedRun(core, load, totalSamples, L, R, checksum);   // warm-up
    std::vector<double> walls;
    walls.reserve(static_cast<std::size_t>(kPerfTimedRuns));
    for (int run = 0; run < kPerfTimedRuns; ++run)
        walls.push_back(timedRun(core, load, totalSamples, L, R, checksum));

    std::sort(walls.begin(), walls.end());
    const double median = walls[walls.size() / 2];
    const double pct = median / kPerfSeconds * 100.0;
    std::printf("PROFUNDUM_PERF partials=%d load=%s pct=%.3f\n", partials, loadName, pct);
    return pct;
}

}  // namespace

// SC-013 (plan S8.3), OQ-3 figures (plan S9.3). Do NOT run concurrently with anything else.
TEST_CASE("ProfundumCore_CpuBudget", "[systems][profundum][.perf]") {
    const auto totalSamples = static_cast<std::size_t>(std::llround(kPerfSeconds * kPerfSampleRate));
    const PerfLoad sweep = makeSweepLoad(totalSamples);
    const PerfLoad still = makeStaticLoad(totalSamples);
    double checksum = 0.0;

    // The gated figure: 64 partials, D/B/E/S 2 Hz triangles + 5 Hz ±2-semitone vibrato.
    const double pct64 = measurePct(64, "sweep", sweep, totalSamples, checksum);

    // OQ-3 figures, printed only (no assert).
    (void)measurePct(96, "sweep", sweep, totalSamples, checksum);
    (void)measurePct(64, "static", still, totalSamples, checksum);

    REQUIRE(detail::isFinite(checksum));   // keeps the render from being elided
    REQUIRE(pct64 <= 1.0);
}
