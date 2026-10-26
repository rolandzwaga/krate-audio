// ==============================================================================
// Layer 3: System Tests - Profundum Anchor, perf TU
//                                    (Profundum Phase 3, specs/profundum-phase3-anchor)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/profundum-phase3-anchor/plan.md (S8.1, S8.3 SC-018 row, S9.8)
//            specs/profundum-phase3-anchor/tasks.md (T006 wires this TU,
//                                                    T016 fills it)
//
// SCOPE OF THIS TU (plan S8.1): SC-018 [.perf]. Deliberately NOT in the -fno-fast-math
//   block: its figures must reflect the shipping FP mode. Tags
//   [systems][profundum][anchor][.perf]. Hidden: it runs only when named, alone, on an
//   idle machine (node tools/run-cpu-tests.js dsp_systems_tests, or T017's pinned run).
//   FTZ/DAZ is on under the test main (FR-057; AnchorOscillator_FtzPrecondition).
// ==============================================================================

#include <krate/dsp/core/db_utils.h>   // detail::isFinite
#include <krate/dsp/systems/anchor_oscillator.h>

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

using Anchor = AnchorOscillator;

constexpr double kPerfSampleRate = 48000.0;
constexpr std::size_t kPerfBlock = 64;               // host block (SC-018)
constexpr double kPerfSeconds = 10.0;                // audio per run
constexpr int kPerfTimedRuns = 5;                    // median of 5, after one discarded warm-up
constexpr double kEventPeriodSeconds = 0.1;          // waveform switch + PUNCH re-arm every 100 ms
constexpr double kVibratoRateHz = 5.0;
constexpr double kVibratoDepthSemitones = 1.0;       // ±1 semitone
constexpr float kFollowRatio = 1.25f;                // FOLLOW cells: followRatioPerSample ≡ 1.25
constexpr double kBudgetFraction = 0.003;            // SC-018: 0.3 % of one core, never relaxed (FR-064)
constexpr double kPerfTwoPi = 6.283185307179586476925286766559;

constexpr std::size_t kNumWaveforms = 5;
constexpr std::size_t kNumModes = 4;
constexpr std::array<Anchor::Waveform, kNumWaveforms> kWaveforms{
    Anchor::Waveform::Sine, Anchor::Waveform::Triangle, Anchor::Waveform::RoundedSine,
    Anchor::Waveform::AsymmetricSine, Anchor::Waveform::OctaveDownSine};
constexpr std::array<Anchor::Mode, kNumModes> kModes{Anchor::Mode::Lock, Anchor::Mode::Follow, Anchor::Mode::Punch,
                                                     Anchor::Mode::Ghost};
constexpr std::array<const char*, kNumWaveforms> kWaveformNames{"sine", "triangle", "rounded", "asymmetric",
                                                                "octave-down"};
constexpr std::array<const char*, kNumModes> kModeNames{"LOCK", "FOLLOW", "PUNCH", "GHOST"};

/// E1 = MIDI 28 (the fast path).
double e1Hz() { return 440.0 * std::pow(2.0, (28.0 - 69.0) / 12.0); }

/// plan S8.3: 494 Hz with the ±1-semitone vibrato keeps f_T in the taper band at 48 kHz, so
/// refreshTaper takes its slow path at every grid sample (C-1).
constexpr double kTaperBandHz = 494.0;

/// The whole run's pitch and ratio input, built before timing so the timed loop is the Anchor only.
struct PerfLoad {
    std::vector<float> f0;      // 5 Hz ±1-semitone vibrato around the note
    std::vector<float> ratio;   // ≡ kFollowRatio (passed for FOLLOW cells only)
    float noteHz = 0.0f;
};

PerfLoad makeLoad(double noteHz, std::size_t totalSamples) {
    PerfLoad load;
    load.noteHz = static_cast<float>(noteHz);
    load.f0.resize(totalSamples);
    for (std::size_t i = 0; i < totalSamples; ++i) {
        const double t = static_cast<double>(i) / kPerfSampleRate;
        const double semis = kVibratoDepthSemitones * std::sin(kPerfTwoPi * kVibratoRateHz * t);
        load.f0[i] = static_cast<float>(noteHz * std::pow(2.0, semis / 12.0));
    }
    load.ratio.assign(totalSamples, kFollowRatio);
    return load;
}

Anchor::Controls cellControls(Anchor::Waveform w, Anchor::Mode m) {
    return Anchor::Controls{.waveform = w,
                            .mode = m,
                            .level = Anchor::kDefaultLevel,
                            .punchBoostDb = 6.0f,
                            .punchTimeMs = 40.0f,
                            .ghostTimeMs = 15.0f};
}

/// One run: FreeRunning policy, noteOn at the cell's note, then kPerfSeconds of audio in host blocks.
/// Every 100 ms (block boundaries, 75 blocks at 48 kHz / 64) the waveform toggles between the cell's
/// waveform and the next one (so every cell carries its own S4.8 crossfades) and a PUNCH re-arm
/// `noteOn(f, true)` is issued. Returns the wall-clock seconds; accumulates the output into `checksum`.
double timedRun(Anchor& anchor, std::size_t cellWaveform, Anchor::Mode mode, const PerfLoad& load,
                std::size_t totalSamples, std::array<float, kPerfBlock>& L, std::array<float, kPerfBlock>& R,
                double& checksum) {
    const auto eventBlocks = static_cast<std::size_t>(
        std::llround(kEventPeriodSeconds * kPerfSampleRate / static_cast<double>(kPerfBlock)));   // 75
    const Anchor::Controls primary = cellControls(kWaveforms[cellWaveform], mode);
    const Anchor::Controls alternate = cellControls(kWaveforms[(cellWaveform + 1) % kNumWaveforms], mode);
    const float* ratio = mode == Anchor::Mode::Follow ? load.ratio.data() : nullptr;

    anchor.reset();   // every run starts from the same state (not sounding)
    anchor.setRetriggerPhase(Anchor::RetriggerPhase::FreeRunning);
    anchor.setCoreH1Amplitude(Anchor::kMaxCoreH1Amplitude);
    anchor.setFollowRatio(kFollowRatio);
    anchor.setControls(primary);
    anchor.noteOn(load.noteHz, true);

    bool onPrimary = true;
    const auto start = std::chrono::steady_clock::now();
    std::size_t block = 0;
    for (std::size_t pos = 0; pos < totalSamples; pos += kPerfBlock, ++block) {
        const std::size_t n = std::min(kPerfBlock, totalSamples - pos);
        if (block != 0 && block % eventBlocks == 0) {
            onPrimary = !onPrimary;
            anchor.setControls(onPrimary ? primary : alternate);   // waveform switch (C-8: latched at offset 0)
            anchor.noteOn(load.f0[pos], true);                     // PUNCH re-arm, FreeRunning (bridged arm)
        }
        anchor.processBlock(L.data(), R.data(), n, load.f0.data() + pos, ratio == nullptr ? nullptr : ratio + pos);
        for (std::size_t k = 0; k < n; ++k)
            checksum += static_cast<double>(L[k]) + static_cast<double>(R[k]);
    }
    const auto stop = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(stop - start).count();
}

/// One warm-up (discarded) + kPerfTimedRuns timed runs; returns median(wall) / kPerfSeconds as a fraction.
double measureCell(std::size_t waveform, std::size_t mode, const PerfLoad& load, std::size_t totalSamples,
                   double& checksum) {
    Anchor anchor;
    anchor.prepare(kPerfSampleRate);

    std::array<float, kPerfBlock> L{};
    std::array<float, kPerfBlock> R{};

    (void)timedRun(anchor, waveform, kModes[mode], load, totalSamples, L, R, checksum);   // warm-up
    std::array<double, kPerfTimedRuns> walls{};
    for (auto& wall : walls)
        wall = timedRun(anchor, waveform, kModes[mode], load, totalSamples, L, R, checksum);
    REQUIRE(anchor.stateFinite());

    std::sort(walls.begin(), walls.end());
    return walls[walls.size() / 2] / kPerfSeconds;
}

}  // namespace

// SC-018 (plan S8.3; FR-064; C-1). Do NOT run concurrently with anything else (tasks.md T017).
TEST_CASE("AnchorOscillator_CpuBudget", "[systems][profundum][anchor][.perf]") {
    const auto totalSamples = static_cast<std::size_t>(std::llround(kPerfSeconds * kPerfSampleRate));
    // Buffers allocated before any timing.
    const PerfLoad e1 = makeLoad(e1Hz(), totalSamples);
    const PerfLoad taper = makeLoad(kTaperBandHz, totalSamples);
    double checksum = 0.0;

    std::array<std::array<double, kNumModes>, kNumWaveforms> costE1{};
    std::array<std::array<double, kNumModes>, kNumWaveforms> costTaper{};
    for (std::size_t w = 0; w < kNumWaveforms; ++w) {
        for (std::size_t m = 0; m < kNumModes; ++m) {
            costE1[w][m] = measureCell(w, m, e1, totalSamples, checksum);
            costTaper[w][m] = measureCell(w, m, taper, totalSamples, checksum);
        }
    }

    double worst = 0.0;
    const char* worstWaveform = "";
    const char* worstMode = "";
    const char* worstNote = "";
    std::printf("ANCHOR_PERF %-12s %-7s %10s %14s\n", "waveform", "mode", "E1 %", "494 Hz %");
    for (std::size_t w = 0; w < kNumWaveforms; ++w) {
        for (std::size_t m = 0; m < kNumModes; ++m) {
            std::printf("ANCHOR_PERF %-12s %-7s %10.4f %14.4f\n", kWaveformNames[w], kModeNames[m],
                        costE1[w][m] * 100.0, costTaper[w][m] * 100.0);
            if (costE1[w][m] > worst) {
                worst = costE1[w][m];
                worstWaveform = kWaveformNames[w];
                worstMode = kModeNames[m];
                worstNote = "E1";
            }
            if (costTaper[w][m] > worst) {
                worst = costTaper[w][m];
                worstWaveform = kWaveformNames[w];
                worstMode = kModeNames[m];
                worstNote = "494Hz";
            }
        }
    }
    std::printf("ANCHOR_PERF worst cell: %s %s %s = %.4f %% (budget %.1f %%)\n", worstWaveform, worstMode, worstNote,
                worst * 100.0, kBudgetFraction * 100.0);

    REQUIRE(detail::isFinite(checksum));   // keeps the render from being elided
    REQUIRE(worst <= kBudgetFraction);     // SC-018: never relaxed, never a shrunk workload (FR-064)
}
