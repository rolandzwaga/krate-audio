// ==============================================================================
// Layer 3: System Tests - Shape & Character, perf TU
//                       (Profundum Phase 5, specs/profundum-phase5-shape-character)
// ==============================================================================
// Reference: specs/profundum-phase5-shape-character/spec.md SC-015 (FR-065; user ruling
//            2026-10-15, Clarifications "T001 CPU bench ruling"), plan.md S8, S9.1, C-20,
//            C-22; tasks.md T021 (deviation 7).
//
// SCOPE: SC-015 (ShapeCharacter_CpuBudget) [performance][.perf]. Deliberately NOT in the
//   -fno-fast-math block: its figures must reflect the shipping FP mode. FTZ/DAZ is on under
//   the test main (dsp_test_main.cpp). Run alone and pinned, nothing else executing:
//   bash specs/profundum-phase5-shape-character/evidence/close/cpu-lane.sh
//   (= pwsh -NoProfile -File tools/pin-perf-cores.ps1
//        -Exe build/windows-x64-release/bin/Release/dsp_systems_tests.exe
//        -ExeArgs "ShapeCharacter_CpuBudget")
//
// GATED: the MONO-LINKED chain ShapeMorph -> BassCharacterStage (kSawAnchor E1 on L and R,
//   L == R, zero channel offsets; both probes assert linked == true before and after timing),
//   all six algorithms x SHAPE {0, 0.25, 0.5, 0.75, 1}, Amount 1, Tone 0.5, no crossfade:
//   max over the 30 cells <= 2.0 % of one core (median of 3 repetitions of 5 s of audio).
// RECORDED (printed and written to evidence/sc015-cpu.txt, not gated): Shape alone and
//   Character alone per cell; the worst crossfade cell (alternating switches every 20 ms);
//   a 48 st / 60 ms Drop through f0PerSample at the argmax cell; a 10 ms Amount + Tone +
//   SHAPE ramp repeated every 50 ms at the argmax cell; ShapeMorph disabled (warm, C-22);
//   and the UNLINKED cells (R = the same fixture offset by 0.37 period,
//   setChannelOffsets(0.15, 0.15); both probes assert linked == false) for Phases 7 / 9.
// ==============================================================================

#include "shape_character_test_helpers.h"
#include "bass_character_stage_probe.h"
#include "shape_morph_probe.h"

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/shape_morph.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace Krate::DSP;
using ShapeMorphProbe = Krate::DSP::detail::ShapeMorphProbe;
using CharacterProbe = Krate::DSP::detail::BassCharacterStageProbe;
using ScpAlgorithm = BassCharacterStage::Algorithm;
namespace SCT = Krate::DSP::ShapeCharacterTest;

namespace {

constexpr double kScpFs = 48000.0;
constexpr std::size_t kScpBlock = 64;               // host block
constexpr double kScpSeconds = 5.0;                 // audio per timed repetition
constexpr int kScpReps = 3;                         // median of 3
constexpr std::size_t kScpWarmupBlocks = 750;       // 1 s of audio, untimed (smoothers, DC, ADAA state)
constexpr std::size_t kScpInputLen = 48000;         // 1 s input loop (= 750 blocks)
constexpr double kScpROffsetPeriods = 0.37;         // unlinked R offset (SC-015)
constexpr float kScpUnlinkedOffset = 0.15f;         // setChannelOffsets(0.15, 0.15) (SC-015)
constexpr double kScpBarPct = 2.0;                  // SC-015 bar, % of one core
constexpr std::size_t kScpSwitchPeriod = 960;       // crossfade cell: switch every 20 ms (15 blocks)
constexpr std::size_t kScpRampPeriod = 2400;        // ramp cell: new targets every 50 ms
constexpr std::size_t kScpDropPeriod = 12000;       // Drop re-triggered every 250 ms (T001 bench)
constexpr double kScpDropSemitones = 48.0;
constexpr double kScpDropSeconds = 0.060;
constexpr std::size_t kScpPitchGrid = 16;           // ProfundumCore::kPitchUpdateInterval (profundum_core.h:98)

constexpr std::array<float, 5> kScpShapes{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
constexpr std::array<const char*, BassCharacterStage::kNumAlgorithms> kScpAlgoNames{
    "Tube", "Fold", "Rectify", "Bite", "Crush", "Fracture"};

ScpAlgorithm scpAlgo(std::size_t i) { return static_cast<ScpAlgorithm>(i); }

enum class ScpKind : std::uint8_t { Chain, ShapeOnly, CharOnly };
enum class ScpLoad : std::uint8_t { Steady, Crossfade, Drop, Ramp };

struct ScpCell {
    ScpKind kind = ScpKind::Chain;
    ScpLoad load = ScpLoad::Steady;
    std::size_t algo = 0;
    std::size_t algoB = 0;        // crossfade partner
    float shape = 0.0f;
    bool linked = true;
    bool shapeEnabled = true;     // false: the C-22 disabled (warm) cell
};

/// The input loops: L = kSawAnchor at E1; R = the same fixture offset by 0.37 period.
struct ScpInput {
    std::vector<float> left;
    std::vector<float> right;
    std::vector<float> dropF0;    // one Drop period, per sample, held on the 16-sample grid
};

ScpInput scpMakeInput() {
    ScpInput in;
    const auto shift = static_cast<std::size_t>(std::llround(kScpROffsetPeriods * kScpFs / SCT::kE1Hz));
    const double seconds = static_cast<double>(kScpInputLen + shift + 1) / kScpFs;
    const std::vector<float> fx =
        SCT::fixture(SpectralShapeRecipe::kSawAnchor, SCT::kE1Hz, kScpFs, seconds);
    REQUIRE(fx.size() >= kScpInputLen + shift);
    in.left.assign(fx.begin(), fx.begin() + static_cast<std::ptrdiff_t>(kScpInputLen));
    in.right.assign(fx.begin() + static_cast<std::ptrdiff_t>(shift),
                    fx.begin() + static_cast<std::ptrdiff_t>(shift + kScpInputLen));

    // 48 st -> 0 linear over 60 ms, then held, re-triggered every 250 ms (T001 bench drop cell).
    in.dropF0.assign(kScpDropPeriod, 0.0f);
    for (std::size_t n = 0; n < kScpDropPeriod; ++n) {
        const std::size_t g = n - (n % kScpPitchGrid);
        const double t = static_cast<double>(g) / kScpFs;
        const double st = t < kScpDropSeconds ? kScpDropSemitones * (1.0 - t / kScpDropSeconds) : 0.0;
        in.dropF0[n] = static_cast<float>(SCT::kE1Hz * std::exp2(st / 12.0));
    }
    return in;
}

/// Runs one cell: prepare, warm-up, probe check, kScpReps timed repetitions of kScpSeconds.
/// Returns median(wall) / audio x 100 (% of one core).
double scpMeasure(const ScpCell& cell, const ScpInput& in, double& checksum) {
    const bool useShape = cell.kind != ScpKind::CharOnly;
    const bool useChar = cell.kind != ScpKind::ShapeOnly;
    const auto e1 = static_cast<float>(SCT::kE1Hz);

    auto morph = std::make_unique<ShapeMorph>();
    auto stage = std::make_unique<BassCharacterStage>();
    morph->prepare(kScpFs, kScpBlock);
    morph->setFundamentalHz(e1);
    morph->setShape(cell.shape);
    morph->setEnabled(cell.shapeEnabled);
    (*morph).reset();   // snaps SHAPE and the engage ramp to their targets

    stage->prepare(kScpFs, kScpBlock);
    stage->setFundamentalHz(e1);
    stage->setAlgorithm(scpAlgo(cell.algo));
    stage->setAmount(1.0f);
    stage->setTone(0.5f);
    if (!cell.linked)
        stage->setChannelOffsets(kScpUnlinkedOffset, kScpUnlinkedOffset);
    (*stage).reset();   // current = target (no crossfade), smoothers snapped; links only with zero offsets

    std::array<float, kScpBlock> bl{};
    std::array<float, kScpBlock> br{};
    std::array<float, kScpBlock> f0Block{};
    const std::vector<float>& srcR = cell.linked ? in.left : in.right;
    std::size_t pos = 0;
    std::size_t absN = 0;
    bool onB = false;
    bool rampAlt = false;
    std::size_t rampIndex = 0;

    const auto block = [&]() {
        std::copy_n(in.left.data() + pos, kScpBlock, bl.data());
        std::copy_n(srcR.data() + pos, kScpBlock, br.data());
        const float* f0p = nullptr;
        if (cell.load == ScpLoad::Drop) {
            for (std::size_t i = 0; i < kScpBlock; ++i)
                f0Block[i] = in.dropF0[(absN + i) % kScpDropPeriod];
            f0p = f0Block.data();
        } else if (cell.load == ScpLoad::Crossfade) {
            if (absN % kScpSwitchPeriod == 0) {   // 960 = 15 x 64: block-aligned
                onB = !onB;
                stage->setAlgorithm(scpAlgo(onB ? cell.algoB : cell.algo));
            }
        } else if (cell.load == ScpLoad::Ramp) {
            const std::size_t k = absN / kScpRampPeriod;
            if (k != rampIndex || absN == 0) {   // new targets: each 10 ms smoother ramps
                rampIndex = k;
                rampAlt = !rampAlt;
                const float shapeB = cell.shape >= 0.5f ? cell.shape - 0.25f : cell.shape + 0.25f;
                stage->setAmount(rampAlt ? 0.8f : 1.0f);
                stage->setTone(rampAlt ? 0.3f : 0.5f);
                morph->setShape(rampAlt ? shapeB : cell.shape);
            }
        }
        if (useShape)
            morph->processBlock(bl.data(), br.data(), kScpBlock, f0p);
        if (useChar)
            stage->processBlock(bl.data(), br.data(), kScpBlock, f0p);
        checksum += static_cast<double>(bl[kScpBlock - 1]) + static_cast<double>(br[kScpBlock - 1]);
        absN += kScpBlock;
        pos += kScpBlock;
        if (pos >= kScpInputLen)
            pos = 0;
    };

    const auto checkLink = [&]() {
        if (useShape)
            REQUIRE(ShapeMorphProbe::linked(*morph) == cell.linked);
        if (useChar)
            REQUIRE(CharacterProbe::linked(*stage) == cell.linked);
    };

    for (std::size_t b = 0; b < kScpWarmupBlocks; ++b)
        block();
    checkLink();   // before timing (SC-015)

    const auto blocksPerRep = static_cast<std::size_t>(std::llround(kScpSeconds * kScpFs)) / kScpBlock;
    const double audioSeconds = static_cast<double>(blocksPerRep * kScpBlock) / kScpFs;
    std::array<double, kScpReps> pct{};
    for (int r = 0; r < kScpReps; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t b = 0; b < blocksPerRep; ++b)
            block();
        const auto t1 = std::chrono::steady_clock::now();
        pct[static_cast<std::size_t>(r)] = std::chrono::duration<double>(t1 - t0).count() / audioSeconds * 100.0;
    }
    checkLink();   // the link never re-forms during processing, so this proves every rep ran linked / unlinked
    if (useShape)
        REQUIRE(morph->stateFinite());
    if (useChar)
        REQUIRE(stage->stateFinite());

    std::sort(pct.begin(), pct.end());
    return pct[static_cast<std::size_t>(kScpReps / 2)];
}

std::string scpEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase5-shape-character/evidence/";
}

/// Prints a line and appends it to the evidence log.
void scpEmit(std::ostringstream& log, const char* line) {
    std::printf("%s\n", line);
    log << line << '\n';
}

}  // namespace

// SC-015 (FR-065; plan S8, C-20 superseded by the 2026-10-15 ruling, C-22). Do NOT run concurrently
// with anything else; run pinned via evidence/close/cpu-lane.sh.
TEST_CASE("ShapeCharacter_CpuBudget",
          "[systems][profundum][shape-character][performance][.perf]") {
    const ScpInput in = scpMakeInput();
    double checksum = 0.0;
    std::ostringstream log;
    char line[256];

    std::snprintf(line, sizeof(line),
                  "SC015 setup fs=%.0f block=%zu note=E1 (%.5f Hz) fixture=kSawAnchor reps=%d x %.1f s "
                  "median warmup=1 s amount=1 tone=0.5 bar=%.1f%% (mono-linked)",
                  kScpFs, kScpBlock, SCT::kE1Hz, kScpReps, kScpSeconds, kScpBarPct);
    scpEmit(log, line);

    // ---- Components alone (linked) ----
    std::array<double, kScpShapes.size()> shapeAlone{};
    for (std::size_t s = 0; s < kScpShapes.size(); ++s) {
        shapeAlone[s] = scpMeasure(ScpCell{.kind = ScpKind::ShapeOnly, .shape = kScpShapes[s]}, in, checksum);
        std::snprintf(line, sizeof(line), "SC015 shape_alone shape=%.2f pct=%.4f", static_cast<double>(kScpShapes[s]),
                      shapeAlone[s]);
        scpEmit(log, line);
    }
    std::array<double, BassCharacterStage::kNumAlgorithms> charAlone{};
    for (std::size_t a = 0; a < BassCharacterStage::kNumAlgorithms; ++a) {
        charAlone[a] = scpMeasure(ScpCell{.kind = ScpKind::CharOnly, .algo = a}, in, checksum);
        std::snprintf(line, sizeof(line), "SC015 char_alone algo=%s pct=%.4f", kScpAlgoNames[a], charAlone[a]);
        scpEmit(log, line);
    }

    // ---- Gated sweep: linked chain, 6 algorithms x 5 SHAPE positions ----
    double maxPct = -1.0;
    std::size_t argAlgo = 0;
    std::size_t argShape = 0;
    for (std::size_t a = 0; a < BassCharacterStage::kNumAlgorithms; ++a) {
        for (std::size_t s = 0; s < kScpShapes.size(); ++s) {
            const double p =
                scpMeasure(ScpCell{.kind = ScpKind::Chain, .algo = a, .shape = kScpShapes[s]}, in, checksum);
            std::snprintf(line, sizeof(line),
                          "SC015 cell linked algo=%s shape=%.2f pct=%.4f shape_alone=%.4f char_alone=%.4f",
                          kScpAlgoNames[a], static_cast<double>(kScpShapes[s]), p, shapeAlone[s], charAlone[a]);
            scpEmit(log, line);
            if (p > maxPct) {
                maxPct = p;
                argAlgo = a;
                argShape = s;
            }
        }
    }
    const float argShapePos = kScpShapes[argShape];
    std::snprintf(line, sizeof(line),
                  "SC015 GATED max_linked pct=%.4f argmax algo=%s shape=%.2f shape_alone=%.4f char_alone=%.4f bar=%.1f",
                  maxPct, kScpAlgoNames[argAlgo], static_cast<double>(argShapePos), shapeAlone[argShape],
                  charAlone[argAlgo], kScpBarPct);
    scpEmit(log, line);

    // ---- Recorded: worst crossfade cell (linked, argmax SHAPE, every algorithm pair) ----
    double xfMax = -1.0;
    std::size_t xfA = 0;
    std::size_t xfB = 0;
    for (std::size_t a = 0; a < BassCharacterStage::kNumAlgorithms; ++a) {
        for (std::size_t b = a + 1; b < BassCharacterStage::kNumAlgorithms; ++b) {
            const double p = scpMeasure(ScpCell{.kind = ScpKind::Chain, .load = ScpLoad::Crossfade, .algo = a,
                                                .algoB = b, .shape = argShapePos},
                                        in, checksum);
            std::snprintf(line, sizeof(line), "SC015 record crossfade algo=%s<->%s shape=%.2f pct=%.4f",
                          kScpAlgoNames[a], kScpAlgoNames[b], static_cast<double>(argShapePos), p);
            scpEmit(log, line);
            if (p > xfMax) {
                xfMax = p;
                xfA = a;
                xfB = b;
            }
        }
    }
    std::snprintf(line, sizeof(line), "SC015 RECORD worst_crossfade algo=%s<->%s shape=%.2f pct=%.4f (not gated)",
                  kScpAlgoNames[xfA], kScpAlgoNames[xfB], static_cast<double>(argShapePos), xfMax);
    scpEmit(log, line);

    // ---- Recorded: Drop, ramp, ShapeMorph disabled (all at the argmax cell, linked) ----
    const double dropPct = scpMeasure(
        ScpCell{.kind = ScpKind::Chain, .load = ScpLoad::Drop, .algo = argAlgo, .shape = argShapePos}, in, checksum);
    std::snprintf(line, sizeof(line),
                  "SC015 RECORD drop 48st/60ms every 250 ms via f0PerSample (16-sample grid) algo=%s shape=%.2f "
                  "pct=%.4f (not gated)",
                  kScpAlgoNames[argAlgo], static_cast<double>(argShapePos), dropPct);
    scpEmit(log, line);

    const double rampPct = scpMeasure(
        ScpCell{.kind = ScpKind::Chain, .load = ScpLoad::Ramp, .algo = argAlgo, .shape = argShapePos}, in, checksum);
    std::snprintf(line, sizeof(line),
                  "SC015 RECORD ramp amount 1<->0.8 tone 0.5<->0.3 shape +-0.25 every 50 ms algo=%s shape=%.2f "
                  "pct=%.4f (not gated)",
                  kScpAlgoNames[argAlgo], static_cast<double>(argShapePos), rampPct);
    scpEmit(log, line);

    const double disabledPct = scpMeasure(
        ScpCell{.kind = ScpKind::Chain, .algo = argAlgo, .shape = argShapePos, .shapeEnabled = false}, in, checksum);
    std::snprintf(line, sizeof(line),
                  "SC015 RECORD shape_disabled_warm (C-22) algo=%s shape=%.2f pct=%.4f (not gated)",
                  kScpAlgoNames[argAlgo], static_cast<double>(argShapePos), disabledPct);
    scpEmit(log, line);

    // ---- Recorded: unlinked sweep (Phase 7 / Phase 9 handoff) ----
    double unMax = -1.0;
    std::size_t unAlgo = 0;
    std::size_t unShape = 0;
    double unAtLinkedArgmax = 0.0;
    for (std::size_t a = 0; a < BassCharacterStage::kNumAlgorithms; ++a) {
        for (std::size_t s = 0; s < kScpShapes.size(); ++s) {
            const double p = scpMeasure(
                ScpCell{.kind = ScpKind::Chain, .algo = a, .shape = kScpShapes[s], .linked = false}, in, checksum);
            std::snprintf(line, sizeof(line), "SC015 record unlinked algo=%s shape=%.2f pct=%.4f", kScpAlgoNames[a],
                          static_cast<double>(kScpShapes[s]), p);
            scpEmit(log, line);
            if (a == argAlgo && s == argShape)
                unAtLinkedArgmax = p;
            if (p > unMax) {
                unMax = p;
                unAlgo = a;
                unShape = s;
            }
        }
    }
    std::snprintf(line, sizeof(line),
                  "SC015 RECORD max_unlinked pct=%.4f argmax algo=%s shape=%.2f; unlinked at linked argmax pct=%.4f "
                  "(not gated)",
                  unMax, kScpAlgoNames[unAlgo], static_cast<double>(kScpShapes[unShape]), unAtLinkedArgmax);
    scpEmit(log, line);

    std::snprintf(line, sizeof(line), "SC015 VERDICT %s max_linked=%.4f bar=%.1f",
                  maxPct <= kScpBarPct ? "PASS" : "FAIL", maxPct, kScpBarPct);
    scpEmit(log, line);

    const std::string dir = scpEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream out(dir + "sc015-cpu.txt");
    REQUIRE(out.is_open());
    out << log.str();
    out.close();

    REQUIRE(detail::isFinite(checksum));   // keeps the render from being elided
    CHECK(maxPct <= kScpBarPct);           // SC-015: max over the 30 linked cells
}
