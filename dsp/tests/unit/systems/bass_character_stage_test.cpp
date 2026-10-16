// ==============================================================================
// Layer 3: System Tests - BassCharacterStage
//                         (Profundum Phase 5, specs/profundum-phase5-shape-character)
// ==============================================================================
// Reference: specs/profundum-phase5-shape-character/plan.md (S2, S5.1-S5.7, C-16..C-19, C-21, C-23)
//            specs/profundum-phase5-shape-character/tasks.md (T002 wires this TU; T009, T014,
//                                                             T017 fill it)
//
// Will hold: SC-008, SC-009, SC-013, SC-021 [long], SC-026, SC-027, SC-029, C-19 Rectify law,
// Crush hold ring (plan S9.1).
//
// T009 (this block, tags [systems][profundum][shape-character][bass-character]):
//   BassCharacter_Lifecycle, BassCharacter_AmountZeroBitIdenticalRectify (SC-008 preview),
//   BassCharacter_RectifyLawMatchesRectifier (C-19), BassCharacter_SwitchMachine (S5.6, D-7,
//   C-21, FR-057), BassCharacter_ObserverBasics (FR-030, FR-036, C-18),
//   BassCharacter_F0FeedRekey (FR-052, S5.3 step 1), BassCharacter_MonoLink (S2, FR-027).
// T014: BassCharacter_CrushHoldRing (plan S5.4 unit case; FR-051, D-6).
// T017: BassCharacter_AmountZeroBitIdentical (SC-008), BassCharacter_ClickCountPositiveControl
//   (SC-009 / SC-021 controls), BassCharacter_SwitchClickFreeSmoke + _SwitchClickFree [long] (SC-009),
//   BassCharacter_MonoSum (SC-013), ShapeCharacter_ParameterStepsClickFreeSmoke + _ParameterStepsClickFree
//   [long] (SC-021), BassCharacter_RLawHysteresis (SC-026 (b)(c)(d)), BassCharacter_DryShareNoComb
//   (SC-027, C-14), BassCharacter_AlgorithmQueue (SC-029, C-21).
// ==============================================================================

#include "bass_character_stage_probe.h"
#include "shape_character_test_helpers.h"

#include <krate/dsp/core/closed_form_adaa2.h>
#include <krate/dsp/core/crossfade_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/keytracked_tilt.h>
#include <krate/dsp/processors/oversampled_residual_path.h>
#include <krate/dsp/processors/rectifier.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/profundum_core.h>
#include <krate/dsp/systems/shape_morph.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace Krate::DSP;
using BcsProbe = Krate::DSP::detail::BassCharacterStageProbe;
using BcsAlgo = BassCharacterStage::Algorithm;

namespace {

constexpr double kBcsFs = 48000.0;
constexpr float kBcsP = HarmonicFold::kCalibrationPeak;

/// tasks.md rule 8: 440·2^((m − 69)/12) in double, cast once.
float bcsNoteHz(int midi) {
    return static_cast<float>(440.0 * std::exp2((static_cast<double>(midi) - 69.0) / 12.0));
}

/// An 8-partial saw-like test signal at peak ≈ P, phase-offset by `phase` periods.
std::vector<float> bcsSaw(double f0, std::size_t n, double phase = 0.0) {
    std::vector<float> x(n);
    constexpr double kTwoPiD = 6.283185307179586;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kBcsFs;
        double v = 0.0;
        for (int k = 1; k <= 8; ++k)
            v += std::sin(kTwoPiD * static_cast<double>(k) * (f0 * t + phase)) / static_cast<double>(k);
        x[i] = static_cast<float>(0.55 * static_cast<double>(kBcsP) * v);
    }
    return x;
}

bool bcsSameBits(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

bool bcsSameBits(const float* a, const float* b, std::size_t n) {
    return std::memcmp(a, b, n * sizeof(float)) == 0;
}

/// Configure before prepare, so prepare()'s reset() leaves the stage steady (no crossfade).
void bcsSetup(BassCharacterStage& s, BcsAlgo a, float amount, float tone) {
    s.setAlgorithm(a);
    s.setAmount(amount);
    s.setTone(tone);
    s.prepare(kBcsFs, 512);
}

/// Process k samples one at a time (stereo, L == R sine at C2).
void bcsRun(BassCharacterStage& s, std::size_t k, std::size_t& clock) {
    for (std::size_t i = 0; i < k; ++i, ++clock) {
        const auto v = static_cast<float>(static_cast<double>(kBcsP)
                                          * std::sin(6.283185307179586 * 65.40639 * static_cast<double>(clock) / kBcsFs));
        float l = v;
        float r = v;
        s.processBlock(&l, &r, 1);
    }
}

float bcsRectPivot(const BassCharacterStage& s) {
    return BcsProbe::postTilt(s, BcsAlgo::Rectify, 0).pivotHz();
}

}  // namespace

// -----------------------------------------------------------------------------
// Lifecycle (Edge Cases: null / 0 / unprepared / right == left / long calls)
// -----------------------------------------------------------------------------
TEST_CASE("BassCharacter_Lifecycle", "[systems][profundum][shape-character][bass-character]") {
    static_assert(BassCharacterStage::latencySamples() == 0);
    constexpr std::size_t kN = 4096;
    const float f0 = bcsNoteHz(28);  // E1
    const std::vector<float> inL = bcsSaw(f0, kN);
    const std::vector<float> inR = bcsSaw(f0, kN, 0.37);

    SECTION("unprepared processBlock leaves the buffers unchanged") {
        BassCharacterStage s;
        s.setAlgorithm(BcsAlgo::Rectify);
        s.setAmount(0.7f);
        std::vector<float> l = inL;
        std::vector<float> r = inR;
        s.processBlock(l.data(), r.data(), kN);
        REQUIRE(bcsSameBits(l, inL));
        REQUIRE(bcsSameBits(r, inR));
    }

    SECTION("null pointer or zero samples is a no-op") {
        BassCharacterStage s;
        bcsSetup(s, BcsAlgo::Rectify, 0.7f, 0.5f);
        std::vector<float> l = inL;
        std::vector<float> r = inR;
        s.processBlock(nullptr, r.data(), kN);
        s.processBlock(l.data(), nullptr, kN);
        s.processBlock(l.data(), r.data(), 0);
        REQUIRE(bcsSameBits(l, inL));
        REQUIRE(bcsSameBits(r, inR));
    }

    SECTION("right == left equals channel 0 of an L == R stereo render") {
        BassCharacterStage mono;
        BassCharacterStage stereo;
        bcsSetup(mono, BcsAlgo::Rectify, 0.7f, 0.3f);
        bcsSetup(stereo, BcsAlgo::Rectify, 0.7f, 0.3f);
        std::vector<float> m = inL;
        std::vector<float> l = inL;
        std::vector<float> r = inL;
        mono.processBlock(m.data(), m.data(), kN);
        stereo.processBlock(l.data(), r.data(), kN);
        REQUIRE(bcsSameBits(m, l));
        REQUIRE_FALSE(bcsSameBits(m, inL));  // not inert
    }

    SECTION("one 4096-sample call equals eight 512-sample calls bit-identically") {
        for (const BcsAlgo a : {BcsAlgo::Rectify, BcsAlgo::Tube}) {
            BassCharacterStage one;
            BassCharacterStage eight;
            for (BassCharacterStage* s : {&one, &eight}) {
                s->setChannelOffsets(0.1f, -0.1f);
                bcsSetup(*s, a, 0.7f, 0.3f);
            }
            std::vector<float> l1 = inL;
            std::vector<float> r1 = inR;
            std::vector<float> l8 = inL;
            std::vector<float> r8 = inR;
            one.processBlock(l1.data(), r1.data(), kN);
            for (std::size_t b = 0; b < kN; b += 512)
                eight.processBlock(l8.data() + b, r8.data() + b, 512);
            REQUIRE(bcsSameBits(l1, l8));
            REQUIRE(bcsSameBits(r1, r8));
        }
    }
}

// -----------------------------------------------------------------------------
// SC-008 preview (FR-022, FR-054, C-16: settle = 100 ms)
// -----------------------------------------------------------------------------
TEST_CASE("BassCharacter_AmountZeroBitIdenticalRectify", "[systems][profundum][shape-character][bass-character]") {
    const float f0 = bcsNoteHz(28);
    constexpr auto kSettle = static_cast<std::size_t>(0.1 * kBcsFs);  // 100 ms (C-16)

    SECTION("Amount 0 from the start: bit-identical from sample 0") {
        constexpr std::size_t kN = 48000;
        const std::vector<float> inL = bcsSaw(f0, kN);
        const std::vector<float> inR = bcsSaw(f0, kN, 0.37);
        BassCharacterStage s;
        bcsSetup(s, BcsAlgo::Rectify, 0.0f, 0.5f);
        std::vector<float> l = inL;
        std::vector<float> r = inR;
        for (std::size_t b = 0; b < kN; b += 512) {
            const std::size_t n = std::min<std::size_t>(512, kN - b);
            s.processBlock(l.data() + b, r.data() + b, n);
        }
        REQUIRE(bcsSameBits(l, inL));
        REQUIRE(bcsSameBits(r, inR));
    }

    SECTION("Amount 0.7 for 0.5 s then 0: bit-identical from 100 ms after the change") {
        constexpr std::size_t kHalf = 24000;
        constexpr std::size_t kN = 2 * kHalf;
        const std::vector<float> inL = bcsSaw(f0, kN);
        const std::vector<float> inR = bcsSaw(f0, kN, 0.37);
        BassCharacterStage s;
        bcsSetup(s, BcsAlgo::Rectify, 0.7f, 0.5f);
        std::vector<float> l = inL;
        std::vector<float> r = inR;
        s.processBlock(l.data(), r.data(), kHalf);
        REQUIRE_FALSE(bcsSameBits(l.data(), inL.data(), kHalf));  // processed while Amount 0.7
        s.setAmount(0.0f);
        s.processBlock(l.data() + kHalf, r.data() + kHalf, kHalf);
        const std::size_t from = kHalf + kSettle;
        REQUIRE(bcsSameBits(l.data() + from, inL.data() + from, kN - from));
        REQUIRE(bcsSameBits(r.data() + from, inR.data() + from, kN - from));
        REQUIRE(BcsProbe::bypassed(s));
    }
}

// -----------------------------------------------------------------------------
// C-19: the shipped Rectify branch is Rectifier's law (SC-014(a) form)
// -----------------------------------------------------------------------------
TEST_CASE("BassCharacter_RectifyLawMatchesRectifier", "[systems][profundum][shape-character][bass-character]") {
    // plan S1 C-19; S5.4 Rectify. Smoothers snapped (seamUnsmoothed_), Tone 0.5 (post tilt
    // neutral; under C-23 the post tilt is not in process() at all).
    BassCharacterStage s;
    BcsProbe::setSeamUnsmoothed(s, true);
    bcsSetup(s, BcsAlgo::Rectify, 0.0f, 0.5f);

    const double delta = 1.0e-4 * static_cast<double>(kBcsP);
    const auto count = static_cast<std::size_t>(std::llround(8.0 * static_cast<double>(kBcsP) / delta)) + 1;
    std::vector<float> x(count);
    for (std::size_t n = 0; n < count; ++n)
        x[n] = static_cast<float>(-4.0 * static_cast<double>(kBcsP) + static_cast<double>(n) * delta);

    for (const float amount : {0.2f, 0.4f, 0.55f, 0.7f, 0.85f, 1.0f}) {
        INFO("Amount " << amount);
        s.setAmount(amount);
        REQUIRE(BcsProbe::smoothedAmount(s, 0) == amount);
        BcsProbe::resetBranch(s, BcsAlgo::Rectify, 0);

        // S5.4 mapping, computed here independently.
        const float wet = std::min(1.0f, amount / BassCharacterStage::kRectHalfEnd);
        const float sArg = std::clamp((amount - BassCharacterStage::kRectHalfEnd)
                                          / (1.0f - BassCharacterStage::kRectHalfEnd),
                                      0.0f, 1.0f);
        const float b = sArg * sArg * (3.0f - 2.0f * sArg);
        const float mk = Rectifier::sineMakeup(b);

        double maxErr = 0.0;
        std::size_t checked = 0;
        for (std::size_t n = 0; n < count; ++n) {
            const BassCharacterStage::BranchOut o = BcsProbe::runBranch(s, BcsAlgo::Rectify, 0, x[n]);
            if (n < 3)
                continue;  // the reset history (zeros) is still in the ADAA2 window
            const float lo = std::min({x[n - 2], x[n - 1], x[n]});
            const float hi = std::max({x[n - 2], x[n - 1], x[n]});
            if (lo < 0.0f && hi > 0.0f)
                continue;  // straddles the kink
            const double xp = static_cast<double>(x[n - 1]);
            const double expected = (1.0 - static_cast<double>(wet)) * xp
                                    + static_cast<double>(wet) * static_cast<double>(mk)
                                          * static_cast<double>(Rectifier::transfer(x[n - 1], b));
            maxErr = std::max(maxErr, std::abs(static_cast<double>(o.y) - expected));
            ++checked;
        }
        REQUIRE(checked > count - 10);
        REQUIRE(maxErr <= 1.0e-5);

        // The branch's derived values and its lin coefficient.
        const BcsProbe::RectifyMix mix = BcsProbe::rectifyMix(s, 0);
        REQUIRE(std::abs(mix.wet - wet) <= 1.0e-6f);
        REQUIRE(std::abs(mix.b - b) <= 1.0e-6f);
        REQUIRE(std::abs(mix.mk - mk) <= 1.0e-6f * mk);
        // c = (1 − wet) + wet·mk·linearCoefficient(b), from the branch's own wet / b / mk. A margin
        // of a few float ulps instead of == (fast-math contraction may differ between the two
        // expression sites on FMA targets; T009 run-log).
        const float expectedLinC = (1.0f - mix.wet) + mix.wet * mix.mk * Rectifier::linearCoefficient(mix.b);
        const float linC = BcsProbe::branchLinearCoefficient(s, BcsAlgo::Rectify, 0);
        REQUIRE(std::abs(linC - expectedLinC) <= 4.0f * 1.2e-7f * std::max(1.0f, std::abs(expectedLinC)));
    }
}

// -----------------------------------------------------------------------------
// S5.6 switch machine (D-7, C-21, FR-057; SC-029(b) preview)
// -----------------------------------------------------------------------------
TEST_CASE("BassCharacter_SwitchMachine", "[systems][profundum][shape-character][bass-character]") {
    BassCharacterStage s;
    bcsSetup(s, BcsAlgo::Tube, 0.5f, 0.5f);
    std::size_t clock = 0;
    const float inc = crossfadeIncrement(BassCharacterStage::kSwitchCrossfadeMs, kBcsFs);
    const auto len = static_cast<std::size_t>(std::ceil(1.0 / static_cast<double>(inc)));
    REQUIRE(BcsProbe::crossfadeLength(s) == len);
    REQUIRE(len > 400);  // ~10 ms at 48 kHz (the exact count depends on float rounding of the increment)
    // FR-026 pre-roll (user ruling 2026-10-16, round 8): a transition is one period of f0 (the incoming
    // branch warming up, unmixed) and then the crossfade; `tl` is the in-flight length.
    const std::size_t pre = BcsProbe::prerollLength(s);
    const auto prePeriod = static_cast<std::size_t>(std::llround(kBcsFs / BassCharacterStage::kDefaultFundamentalHz));
    REQUIRE(pre == prePeriod);
    const std::size_t tl = pre + len;

    // setAlgorithm(current) starts nothing.
    s.setAlgorithm(BcsAlgo::Tube);
    REQUIRE_FALSE(BcsProbe::crossfading(s));
    bcsRun(s, 10, clock);
    REQUIRE(BcsProbe::maxRunningBranchCount(s) == 1);

    SECTION("A -> B, then C, D during the crossfade: pending D only; B -> D starts at the end") {
        s.setAlgorithm(BcsAlgo::Fold);
        REQUIRE(BcsProbe::crossfading(s));
        REQUIRE(BcsProbe::current(s) == BcsAlgo::Tube);
        REQUIRE(BcsProbe::target(s) == BcsAlgo::Fold);
        bcsRun(s, 100, clock);
        REQUIRE(BcsProbe::runningBranchCount(s) == 2);
        s.setAlgorithm(BcsAlgo::Rectify);
        REQUIRE(BcsProbe::hasPending(s));
        REQUIRE(BcsProbe::pending(s) == BcsAlgo::Rectify);
        bcsRun(s, 100, clock);
        s.setAlgorithm(BcsAlgo::Bite);
        REQUIRE(BcsProbe::hasPending(s));
        REQUIRE(BcsProbe::pending(s) == BcsAlgo::Bite);
        REQUIRE(BcsProbe::target(s) == BcsAlgo::Fold);
        bcsRun(s, tl - 201, clock);
        REQUIRE(BcsProbe::crossfading(s));
        REQUIRE(BcsProbe::current(s) == BcsAlgo::Tube);
        bcsRun(s, 1, clock);
        REQUIRE(BcsProbe::current(s) == BcsAlgo::Fold);
        REQUIRE(BcsProbe::target(s) == BcsAlgo::Bite);
        REQUIRE(BcsProbe::crossfading(s));
        REQUIRE(BcsProbe::prerolling(s));  // the queued B -> D starts with its own pre-roll
        REQUIRE_FALSE(BcsProbe::hasPending(s));
        bcsRun(s, tl, clock);
        REQUIRE(BcsProbe::current(s) == BcsAlgo::Bite);
        REQUIRE_FALSE(BcsProbe::crossfading(s));
        REQUIRE(BcsProbe::maxRunningBranchCount(s) == 2);
    }

    SECTION("C-21: A -> B, then C, then B clears the queue and ends at B") {
        s.setAlgorithm(BcsAlgo::Crush);
        bcsRun(s, 50, clock);
        s.setAlgorithm(BcsAlgo::Fracture);
        REQUIRE(BcsProbe::hasPending(s));
        s.setAlgorithm(BcsAlgo::Crush);
        REQUIRE_FALSE(BcsProbe::hasPending(s));
        REQUIRE(BcsProbe::target(s) == BcsAlgo::Crush);
        bcsRun(s, tl - 50, clock);
        REQUIRE(BcsProbe::current(s) == BcsAlgo::Crush);
        REQUIRE_FALSE(BcsProbe::crossfading(s));
        bcsRun(s, tl, clock);
        REQUIRE(BcsProbe::current(s) == BcsAlgo::Crush);
        REQUIRE_FALSE(BcsProbe::crossfading(s));
        REQUIRE(BcsProbe::runningBranchCount(s) == 1);
    }

    SECTION("A -> B, then C, then A: B -> A starts at the end") {
        s.setAlgorithm(BcsAlgo::Rectify);
        bcsRun(s, 30, clock);
        s.setAlgorithm(BcsAlgo::Bite);
        s.setAlgorithm(BcsAlgo::Tube);
        REQUIRE(BcsProbe::hasPending(s));
        REQUIRE(BcsProbe::pending(s) == BcsAlgo::Tube);
        bcsRun(s, tl - 30, clock);
        REQUIRE(BcsProbe::current(s) == BcsAlgo::Rectify);
        REQUIRE(BcsProbe::target(s) == BcsAlgo::Tube);
        REQUIRE(BcsProbe::crossfading(s));
        bcsRun(s, tl, clock);
        REQUIRE(BcsProbe::current(s) == BcsAlgo::Tube);
        REQUIRE_FALSE(BcsProbe::crossfading(s));
    }

    REQUIRE(BcsProbe::maxRunningBranchCount(s) <= 2);
    REQUIRE(s.stateFinite());
}

// -----------------------------------------------------------------------------
// S5.7 observer basics (FR-030, FR-036, C-18, D-8)
// -----------------------------------------------------------------------------
TEST_CASE("BassCharacter_ObserverBasics", "[systems][profundum][shape-character][bass-character]") {
    BassCharacterStage s;
    bcsSetup(s, BcsAlgo::Tube, 0.0f, 0.5f);
    std::size_t clock = 0;
    const std::size_t len = BcsProbe::prerollLength(s) + BcsProbe::crossfadeLength(s);  // FR-026 in-flight length

    for (const BcsAlgo a : {BcsAlgo::Tube, BcsAlgo::Fold, BcsAlgo::Bite, BcsAlgo::Crush}) {
        s.setAlgorithm(a);
        for (const float amount : {0.0f, 0.5f, 1.0f}) {
            s.setAmount(amount);
            REQUIRE(s.perceivedPitchRatio() == 1.0f);
        }
        bcsRun(s, len, clock);
    }

    // Rectify with the default descriptor: 1 everywhere.
    s.setAlgorithm(BcsAlgo::Rectify);
    bcsRun(s, len, clock);
    REQUIRE(BcsProbe::current(s) == BcsAlgo::Rectify);
    for (int k = 0; k <= 200; ++k) {
        s.setAmount(static_cast<float>(k) * 0.005f);
        REQUIRE(s.perceivedPitchRatio() == 1.0f);
    }
    s.setAmount(0.0f);

    // Half-wave-symmetric input, Tone 0.5: one up-flip at the first Amount >= hi, one down-flip at
    // the first Amount <= lo.
    s.setInputHalfWaveSymmetric(true);
    s.setTone(0.5f);
    const BassCharacterStage::FlipEdges edges = BassCharacterStage::kRectifyFlipEdges[2];
    REQUIRE(s.perceivedPitchRatio() == 1.0f);
    int upFlips = 0;
    float prev = s.perceivedPitchRatio();
    for (int k = 0; k <= 200; ++k) {
        const float amount = static_cast<float>(k) * 0.005f;
        s.setAmount(amount);
        const float r = s.perceivedPitchRatio();
        REQUIRE(r == (amount >= edges.hi ? 2.0f : 1.0f));
        if (r != prev)
            ++upFlips;
        prev = r;
    }
    REQUIRE(upFlips == 1);
    int downFlips = 0;
    for (int k = 200; k >= 0; --k) {
        const float amount = static_cast<float>(k) * 0.005f;
        s.setAmount(amount);
        const float r = s.perceivedPitchRatio();
        REQUIRE(r == (amount <= edges.lo ? 1.0f : 2.0f));
        if (r != prev)
            ++downFlips;
        prev = r;
    }
    REQUIRE(downFlips == 1);

    // Offsets never change r.
    s.setAmount(1.0f);
    REQUIRE(s.perceivedPitchRatio() == 2.0f);
    s.setChannelOffsets(0.15f, 0.15f);
    REQUIRE(s.perceivedPitchRatio() == 2.0f);
    const float inside = 0.5f * (edges.lo + edges.hi);
    s.setAmount(inside);
    REQUIRE(s.perceivedPitchRatio() == 2.0f);  // hysteresis holds inside the band
    s.setChannelOffsets(-0.15f, -0.15f);
    REQUIRE(s.perceivedPitchRatio() == 2.0f);

    // reset() at an Amount inside the band reports the default side (SC-026(c)).
    s.reset();
    REQUIRE(s.perceivedPitchRatio() == 1.0f);
    s.setChannelOffsets(0.15f, 0.15f);
    REQUIRE(s.perceivedPitchRatio() == 1.0f);
    s.setChannelOffsets(0.0f, 0.0f);

    // During a Rectify -> Tube crossfade r == 1 from the setAlgorithm call on (D-8).
    s.setAmount(1.0f);
    REQUIRE(s.perceivedPitchRatio() == 2.0f);
    s.setAlgorithm(BcsAlgo::Tube);
    REQUIRE(s.perceivedPitchRatio() == 1.0f);
    bcsRun(s, len, clock);
    REQUIRE(BcsProbe::current(s) == BcsAlgo::Tube);

    // Tube -> Rectify reports 2 from the call; a queued request leaves r unchanged.
    s.setAlgorithm(BcsAlgo::Rectify);
    REQUIRE(s.perceivedPitchRatio() == 2.0f);
    s.setAlgorithm(BcsAlgo::Fold);
    REQUIRE(BcsProbe::hasPending(s));
    REQUIRE(s.perceivedPitchRatio() == 2.0f);
    bcsRun(s, len, clock);
    REQUIRE(BcsProbe::target(s) == BcsAlgo::Fold);  // the queued request started
    REQUIRE(s.perceivedPitchRatio() == 1.0f);
}

// -----------------------------------------------------------------------------
// FR-052, S5.3 step 1: the f0 feed and the re-key rule
// -----------------------------------------------------------------------------
TEST_CASE("BassCharacter_F0FeedRekey", "[systems][profundum][shape-character][bass-character]") {
    const float c2 = bcsNoteHz(36);
    const float c3 = bcsNoteHz(48);
    const float e1 = bcsNoteHz(28);
    const float e2 = bcsNoteHz(40);

    SECTION("start, leaving bypass and reset re-key; clamps") {
        BassCharacterStage s;
        bcsSetup(s, BcsAlgo::Tube, 0.5f, 0.5f);
        std::size_t clock = 0;
        const std::size_t len = BcsProbe::prerollLength(s) + BcsProbe::crossfadeLength(s);  // FR-026 in-flight length
        const float defaultPivot = BassCharacterStage::kPostTiltPivot * BassCharacterStage::kDefaultFundamentalHz;
        REQUIRE(bcsRectPivot(s) == defaultPivot);

        // A branch that is not running is not updated ...
        s.setFundamentalHz(c3);
        bcsRun(s, 4, clock);
        REQUIRE(bcsRectPivot(s) == defaultPivot);
        REQUIRE(BcsProbe::postTilt(s, BcsAlgo::Tube, 0).pivotHz() == BassCharacterStage::kPostTiltPivot * c3);
        REQUIRE(BcsProbe::preTilt(s, BcsAlgo::Tube, 0).pivotHz() == BassCharacterStage::kPreTiltPivot * c3);
        // ... and start() re-keys it before its first sample.
        s.setAlgorithm(BcsAlgo::Rectify);
        bcsRun(s, 1, clock);
        REQUIRE(bcsRectPivot(s) == BassCharacterStage::kPostTiltPivot * c3);
        REQUIRE(BcsProbe::postTilt(s, BcsAlgo::Rectify, 1).pivotHz() == BassCharacterStage::kPostTiltPivot * c3);
        bcsRun(s, len, clock);
        REQUIRE(BcsProbe::current(s) == BcsAlgo::Rectify);

        // Leaving bypass (Amount 0 -> 0.5) re-keys.
        s.setAmount(0.0f);
        for (int guard = 0; guard < 48000 && !BcsProbe::bypassed(s); ++guard)
            bcsRun(s, 1, clock);
        REQUIRE(BcsProbe::bypassed(s));
        s.setFundamentalHz(e1);
        REQUIRE(bcsRectPivot(s) == BassCharacterStage::kPostTiltPivot * c3);  // bypassed: not running
        s.setAmount(0.5f);
        bcsRun(s, 1, clock);
        REQUIRE_FALSE(BcsProbe::bypassed(s));
        REQUIRE(bcsRectPivot(s) == BassCharacterStage::kPostTiltPivot * e1);

        // reset() re-keys.
        s.setAmount(0.0f);
        for (int guard = 0; guard < 48000 && !BcsProbe::bypassed(s); ++guard)
            bcsRun(s, 1, clock);
        REQUIRE(BcsProbe::bypassed(s));
        s.setFundamentalHz(e2);
        s.reset();
        REQUIRE(bcsRectPivot(s) == BassCharacterStage::kPostTiltPivot * e2);
        bcsRun(s, 1, clock);
        REQUIRE(bcsRectPivot(s) == BassCharacterStage::kPostTiltPivot * e2);

        // Clamps to [8, 20000] (read back through the running branch's pivot).
        s.setAmount(0.5f);
        bcsRun(s, 8, clock);
        REQUIRE_FALSE(BcsProbe::bypassed(s));
        s.setFundamentalHz(5.0f);
        REQUIRE(s.fundamentalHz() == BassCharacterStage::kMinFundamentalHz);
        REQUIRE(bcsRectPivot(s) == BassCharacterStage::kPostTiltPivot * BassCharacterStage::kMinFundamentalHz);
        s.setFundamentalHz(30000.0f);
        REQUIRE(s.fundamentalHz() == BassCharacterStage::kMaxFundamentalHz);
        REQUIRE(bcsRectPivot(s) == BassCharacterStage::kPostTiltPivot * BassCharacterStage::kMaxFundamentalHz);
    }

    SECTION("a constant f0PerSample array equals the null pointer with setFundamentalHz (SC-025(e) preview)") {
        constexpr std::size_t kN = 8192;
        const std::vector<float> inL = bcsSaw(c2, kN);
        const std::vector<float> inR = bcsSaw(c2, kN, 0.37);
        const std::vector<float> f0s(kN, c2);
        BassCharacterStage withPtr;
        BassCharacterStage withNull;
        bcsSetup(withPtr, BcsAlgo::Rectify, 0.7f, 0.3f);
        bcsSetup(withNull, BcsAlgo::Rectify, 0.7f, 0.3f);
        withNull.setFundamentalHz(c2);
        std::vector<float> lp = inL;
        std::vector<float> rp = inR;
        std::vector<float> ln = inL;
        std::vector<float> rn = inR;
        for (std::size_t b = 0; b < kN; b += 512) {
            withPtr.processBlock(lp.data() + b, rp.data() + b, 512, f0s.data() + b);
            withNull.processBlock(ln.data() + b, rn.data() + b, 512);
        }
        REQUIRE(bcsSameBits(lp, ln));
        REQUIRE(bcsSameBits(rp, rn));
        REQUIRE(bcsRectPivot(withPtr) == BassCharacterStage::kPostTiltPivot * c2);
    }
}

// -----------------------------------------------------------------------------
// Mono link (plan S2; FR-027; SC-013(a) preview)
// -----------------------------------------------------------------------------
TEST_CASE("BassCharacter_MonoLink", "[systems][profundum][shape-character][bass-character]") {
    constexpr std::size_t kN = 4096;
    const float f0 = bcsNoteHz(28);
    const std::vector<float> in = bcsSaw(f0, kN);

    for (const BcsAlgo a : {BcsAlgo::Rectify, BcsAlgo::Tube}) {
        BassCharacterStage s;
        bcsSetup(s, a, 0.7f, 0.3f);
        std::vector<float> l = in;
        std::vector<float> r = in;
        s.processBlock(l.data(), r.data(), kN);
        REQUIRE(bcsSameBits(l, r));
        REQUIRE(BcsProbe::linked(s));

        s.setChannelOffsets(0.1f, 0.0f);
        REQUIRE_FALSE(BcsProbe::linked(s));
        if (a == BcsAlgo::Rectify) {  // the offset reaches the law: L and R now differ
            std::vector<float> l2 = in;
            std::vector<float> r2 = in;
            s.processBlock(l2.data(), r2.data(), kN);
            REQUIRE_FALSE(bcsSameBits(l2, r2));
        }
        s.setChannelOffsets(0.0f, 0.0f);
        REQUIRE_FALSE(BcsProbe::linked(s));  // stays unlinked until reset()
        s.reset();
        REQUIRE(BcsProbe::linked(s));
    }
}

// -----------------------------------------------------------------------------
// T014 Crush hold ring (plan S5.4 unit case; FR-051, D-6)
// -----------------------------------------------------------------------------

// The S5.4 occupancy bound: every engine's steps are >= 2.75 F-samples apart, so at most 2 of the
// 4-sample B-spline steps are active at once in the 3-slot ring and the ring-full policy never fires.
// An Amount ramp that carries m_eff across many integers (the parity-keyed engine handoff) is
// click-free against the steady references.
TEST_CASE("BassCharacter_CrushHoldRing", "[systems][profundum][shape-character][bass-character]") {
    namespace Sct = Krate::DSP::ShapeCharacterTest;
    constexpr double kTwoPiD = 6.283185307179586;

    // (a) Worst spacing: f0 = 20 kHz at 44.1 kHz, Amount 1 -> m(1) = 3 (m(A) = 64·(64/3)^-A, user ruling
    // 2026-10-16, round 7), m_eff = min(3, 4·44100/(4·20000) = 2.205) = 2.205: the cap binds, m_lo = 2,
    // m_hi = 3, frac = 0.205, the m_hi = 3 engine's steps 4·44100/(3·20000) = 2.94 F-samples apart. At this
    // f0 the cap binds everywhere under this law and f0 = 20 kHz at 44.1 kHz is the fastest m_hi·f0/(F·fs):
    // the worst spacing.
    {
        constexpr double fs = 44100.0;
        BassCharacterStage s;
        s.setAlgorithm(BcsAlgo::Crush);
        s.setAmount(1.0f);
        s.setFundamentalHz(20000.0f);
        s.prepare(fs, 512);
        REQUIRE(s.oversamplingFactor() == 4);  // premise of the 2.205 cap / 2.94 spacing figures (S5.4)
        const auto n = static_cast<std::size_t>(0.2 * fs);
        std::vector<float> L(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double ph = kTwoPiD * 20000.0 * static_cast<double>(i) / fs;
            L[i] = static_cast<float>(static_cast<double>(kBcsP) * std::sin(ph));
        }
        std::vector<float> R = L;
        for (std::size_t pos = 0; pos < n; pos += 512)
            s.processBlock(L.data() + pos, R.data() + pos, std::min<std::size_t>(512, n - pos));
        INFO("f0 20 kHz / 44.1 kHz, Amount 1: max occupancy " << BcsProbe::crushMaxOccupancy(s) << ", retirements "
                                                              << BcsProbe::crushRetirements(s));
        CHECK(BcsProbe::crushMaxOccupancy(s) <= 2);
        CHECK(BcsProbe::crushMaxOccupancy(s) >= 2);  // not vacuous: m_hi = 3 steps 2.94 apart, 4 long, overlap
        CHECK(BcsProbe::crushRetirements(s) == 0);
        CHECK(s.stateFinite());
    }

    // (b) E1, 48 kHz, kSawAnchor: 0.5 s at Amount 0.05 (the hold runs), a 0.5 s ramp to Amount 1
    // (m from 64·(64/3)^-0.05 = 54.7 down to 3, user ruling 2026-10-16, round 7), 0.25 s held at 1. Gate (long-ramp form, Group 6 gate
    // ruling 2026-10-16, spec "Differential click count"): the sample-delta bound in the SC-003(b)
    // form, every |dy| during the ramp and its settle <= 1.1 x the max steady-state |dy| over 21
    // Amounts held across [0.05, 1]; the differential click count (two-reference form) is recorded,
    // because a hold's staircase positions depend on the exact Amount and no held render is
    // phase-aligned with the ramp (the detector count is ill-posed for it).
    {
        constexpr double fs = kBcsFs;
        const double f0 = Sct::kE1Hz;
        constexpr float kStartAmount = 0.05f;
        constexpr double kPreSeconds = 0.5;
        constexpr double kRampSeconds = 0.5;
        constexpr double kTotalSeconds = 1.25;
        constexpr std::size_t kBlock = 64;

        // Premise: m_eff crosses >= 3 integers and the F·fs/(4·f0) cap does not bind at E1.
        const double mStart = static_cast<double>(BassCharacterStage::kCrushMultipleLo)
                              * std::pow(static_cast<double>(BassCharacterStage::kCrushMultipleHi
                                                             / BassCharacterStage::kCrushMultipleLo),
                                         static_cast<double>(kStartAmount));
        const double mEnd = static_cast<double>(BassCharacterStage::kCrushMultipleHi);
        REQUIRE(std::floor(mStart) - std::floor(mEnd) >= 3.0);
        REQUIRE(4.0 * fs / (4.0 * f0) > 64.0);

        const std::vector<float> in = Sct::fixture(SpectralShapeRecipe::kSawAnchor, f0, fs, kTotalSeconds);
        const std::size_t n = in.size();

        const auto render = [&](BassCharacterStage& st, const auto& amountAt) {
            st.setAlgorithm(BcsAlgo::Crush);
            st.setAmount(amountAt(0.0));
            st.setTone(0.5f);
            st.setFundamentalHz(static_cast<float>(f0));
            st.prepare(fs, 512);
            REQUIRE(st.oversamplingFactor() == 4);
            std::vector<float> l = in;
            std::vector<float> r = in;
            for (std::size_t pos = 0; pos < n; pos += kBlock) {
                st.setAmount(amountAt(static_cast<double>(pos) / fs));
                const std::size_t len = std::min(kBlock, n - pos);
                st.processBlock(l.data() + pos, r.data() + pos, len);
            }
            return l;
        };
        const auto ramp = [&](double t) {
            if (t < kPreSeconds)
                return kStartAmount;
            const double x = std::min(1.0, (t - kPreSeconds) / kRampSeconds);
            const auto a0 = static_cast<double>(kStartAmount);
            return static_cast<float>(a0 + (1.0 - a0) * x);
        };
        BassCharacterStage sTr;
        const std::vector<float> tr = render(sTr, ramp);

        constexpr std::size_t kGrid = 21;
        const double gridStep = (1.0 - static_cast<double>(kStartAmount)) / static_cast<double>(kGrid - 1);
        std::vector<std::vector<float>> held(kGrid);
        std::vector<const float*> refs(kGrid);
        std::vector<double> refValues(kGrid);
        for (std::size_t g = 0; g < kGrid; ++g) {
            const auto v = static_cast<float>(static_cast<double>(kStartAmount) + gridStep * static_cast<double>(g));
            BassCharacterStage sg;
            held[g] = render(sg, [v](double) { return v; });
            refs[g] = held[g].data();
            refValues[g] = static_cast<double>(v);
        }
        const std::vector<float>& refStart = held[0];
        const std::vector<float>& refEnd = held[kGrid - 1];

        INFO("E1 Amount ramp 0.05 -> 1: max occupancy " << BcsProbe::crushMaxOccupancy(sTr) << ", retirements "
                                                        << BcsProbe::crushRetirements(sTr));
        CHECK(BcsProbe::crushMaxOccupancy(sTr) <= 2);
        CHECK(BcsProbe::crushMaxOccupancy(sTr) >= 1);  // not vacuous: the hold ran
        CHECK(BcsProbe::crushRetirements(sTr) == 0);
        CHECK(sTr.stateFinite());

        // Sample-delta bound (gated): steady max |dy| per held Amount, bound = 1.1 x the grid max.
        double steadyMax = 0.0;
        std::size_t steadyArg = 0;
        for (std::size_t g = 0; g < kGrid; ++g) {
            const double d = Sct::steadyDeltaMax(refs[g], 1, n);
            if (d > steadyMax) {
                steadyMax = d;
                steadyArg = g;
            }
        }
        const auto rampBegin = static_cast<std::size_t>(kPreSeconds * fs);
        const auto rampEnd = static_cast<std::size_t>((kPreSeconds + kRampSeconds + 0.050) * fs);   // + 5 tau settle
        const double rampMax = Sct::steadyDeltaMax(tr.data(), rampBegin, rampEnd);
        INFO("sample-delta bound: ramp max |dy| " << rampMax << ", steady grid max " << steadyMax << " at Amount "
                                                 << refValues[steadyArg] << ", ratio " << rampMax / steadyMax << " (bar 1.1)");
        CHECK(rampMax <= 1.1 * steadyMax);
        // Differential click count, two-reference form: RECORDED (not gated) for a long ramp.
        const Sct::ClickCount cc = Sct::differentialClicks(tr.data(), refStart.data(), refEnd.data(), n, fs, f0);
        WARN("differential clicks on the 0.5 s Amount ramp (recorded, not gated; long-ramp ruling 2026-10-16): counted "
             << cc.counted << " (guarded out " << cc.guardedOut << ", transition detections " << cc.transitionDetections << ", reference detections "
             << cc.referenceDetections << ", N " << cc.windowN << ")");
        INFO("differential clicks (recorded): counted " << cc.counted << " (guarded out " << cc.guardedOut << ", transition detections " << cc.transitionDetections
                                             << ", reference detections " << cc.referenceDetections << ", N "
                                             << cc.windowN << ")");
        // The ramp moved the sound (the transition is not a copy of the start reference).
        CHECK(std::memcmp(tr.data(), refStart.data(), n * sizeof(float)) != 0);
    }
}

// =============================================================================
// T017: character behaviour gates (SC-008, SC-009, SC-013, SC-021, SC-026(b)(c)(d), SC-027, SC-029;
// plan S1 C-14, C-16, C-17, C-21)
// =============================================================================
// Differential click count (spec "Differential click count", Group 3 gate rulings 2026-10-16):
// ClickDetector at its documented defaults; a transition detection counts only if neither held
// reference (the stage held at the start setting and at the end setting, same input) has one within
// ±N, N = floor(fs / f0 / 8) (recorded per cell as "N"). The positive controls are gated per SC arm
// and per note (ruling 2026-10-16): at each note at least one fixture flags each control; every cell
// is recorded with WARN. SC-021's 20 ms ramps and steps keep the count as written (Group 6 ruling).
//
// Phase alignment of the held references: Crush and Fracture run a phasor that start() and leaving
// bypass reset to 0 (S5.3 step 3, S5.4), while a render held from prepare starts it at sample 0. Every
// event that can start a branch (an algorithm switch, leaving the Amount-0 bypass) is therefore placed
// at the sample S (on the cell's block grid) whose S·f0/fs lies nearest an even integer: there the
// reset phasor and the held render's phasor coincide, so the end reference is phase-aligned sample
// for sample, as the spec's method requires. The event sample is recorded per cell.

namespace {

namespace BcsSct = Krate::DSP::ShapeCharacterTest;
namespace BcsTu = Krate::DSP::TestUtils;

constexpr std::array<BcsAlgo, BassCharacterStage::kNumAlgorithms> kT17Algos{
    BcsAlgo::Tube, BcsAlgo::Fold, BcsAlgo::Rectify, BcsAlgo::Bite, BcsAlgo::Crush, BcsAlgo::Fracture};
constexpr float kT17Alpha = 1.493f;           // C-16 switch-law exponent (task T017 literal)
constexpr double kT17ClickSeconds = 1.5;      // events in [0.5 s, 1.0 s), >= 0.5 s rendered after
constexpr std::size_t kT17RampSamples = 960;  // 20 ms at 48 kHz (SC-021)
constexpr std::size_t kT17ParamBlock = 64;    // SC-021 host block: a 20 ms ramp is 15 setter calls
constexpr auto kT17EventLo = static_cast<std::size_t>(0.5 * kBcsFs);
constexpr auto kT17EventHi = static_cast<std::size_t>(1.0 * kBcsFs);
constexpr auto kT17ControlFrom = static_cast<std::size_t>(1.0 * kBcsFs);
constexpr auto kT17SteadyBegin = static_cast<std::size_t>(0.25 * kBcsFs);
constexpr float kT17Offset = BassCharacterStage::kMaxChannelOffset;  // ±0.15 (D-12)
constexpr std::size_t kT17None = static_cast<std::size_t>(-1);

const char* t17AlgoName(BcsAlgo a) {
    switch (a) {
        case BcsAlgo::Tube:
            return "Tube";
        case BcsAlgo::Fold:
            return "Fold";
        case BcsAlgo::Rectify:
            return "Rectify";
        case BcsAlgo::Bite:
            return "Bite";
        case BcsAlgo::Crush:
            return "Crush";
        case BcsAlgo::Fracture:
            return "Fracture";
    }
    return "?";
}

struct T17Note {
    const char* name;
    double hz;
};

/// One complete control state of the stage (centre Amount / Tone, channel offsets).
struct T17Setting {
    float amount;
    float tone;
    float amountOffset;
    float toneOffset;
};

constexpr T17Setting kT17SwitchSetting{.amount = 1.0f, .tone = 0.5f, .amountOffset = 0.0f, .toneOffset = 0.0f};

/// u + (v - u)·t per field, in double; t = 1 returns v exactly.
T17Setting t17Lerp(const T17Setting& a, const T17Setting& b, double t) {
    const auto mix = [t](float u, float v) {
        return static_cast<float>(static_cast<double>(u) + (static_cast<double>(v) - static_cast<double>(u)) * t);
    };
    return {.amount = mix(a.amount, b.amount),
            .tone = mix(a.tone, b.tone),
            .amountOffset = mix(a.amountOffset, b.amountOffset),
            .toneOffset = mix(a.toneOffset, b.toneOffset)};
}

void t17Apply(BassCharacterStage& s, const T17Setting& c) {
    s.setAmount(c.amount);
    s.setTone(c.tone);
    s.setChannelOffsets(c.amountOffset, c.toneOffset);
}

/// Every control set before prepare, so prepare()'s reset() leaves the stage steady at `c`.
void t17Prepare(BassCharacterStage& s, BcsAlgo a, const T17Setting& c, double f0) {
    s.setAlgorithm(a);
    t17Apply(s, c);
    s.setFundamentalHz(static_cast<float>(f0));
    s.prepare(kBcsFs, 512);
}

struct T17Stereo {
    std::vector<float> l;
    std::vector<float> r;
};

/// Host blocks of `block`, split at every event sample (strictly increasing); onEvent(i) runs right
/// before sample events[i]; after(stage) runs after every processBlock call.
template <class OnEvent, class After>
T17Stereo t17RenderEvents(BassCharacterStage& s, const std::vector<float>& inL, const std::vector<float>& inR,
                          std::size_t block, const std::vector<std::size_t>& events, const OnEvent& onEvent,
                          const After& after) {
    REQUIRE(inL.size() == inR.size());
    REQUIRE(block > 0);
    for (std::size_t i = 1; i < events.size(); ++i)
        REQUIRE(events[i] > events[i - 1]);
    T17Stereo y{inL, inR};
    const std::size_t n = inL.size();
    std::size_t e = 0;
    std::size_t pos = 0;
    while (pos < n) {
        while (e < events.size() && events[e] == pos) {
            onEvent(e);
            ++e;
        }
        std::size_t end = std::min(n, (pos / block + 1) * block);
        if (e < events.size() && events[e] < end)
            end = events[e];
        s.processBlock(y.l.data() + pos, y.r.data() + pos, end - pos);
        after(static_cast<const BassCharacterStage&>(s));
        pos = end;
    }
    return y;
}

template <class OnEvent>
T17Stereo t17RenderEvents(BassCharacterStage& s, const std::vector<float>& inL, const std::vector<float>& inR,
                          std::size_t block, const std::vector<std::size_t>& events, const OnEvent& onEvent) {
    return t17RenderEvents(s, inL, inR, block, events, onEvent, [](const BassCharacterStage&) {});
}

/// Host blocks of `block`; onBlock(pos) runs before the block that starts at pos.
template <class OnBlock>
T17Stereo t17RenderBlocks(BassCharacterStage& s, const std::vector<float>& inL, const std::vector<float>& inR,
                          std::size_t block, const OnBlock& onBlock) {
    REQUIRE(inL.size() == inR.size());
    T17Stereo y{inL, inR};
    const std::size_t n = inL.size();
    for (std::size_t pos = 0; pos < n; pos += block) {
        onBlock(pos);
        const std::size_t len = std::min(block, n - pos);
        s.processBlock(y.l.data() + pos, y.r.data() + pos, len);
    }
    return y;
}

/// The stage held at `c` (algorithm a) from prepare over the whole input. syncAtZero: syncPhase(0) right
/// after prepare (FR-072; the fixtures are sine-phase from sample 0, the core's phase origin).
T17Stereo t17Held(const std::vector<float>& inL, const std::vector<float>& inR, BcsAlgo a, const T17Setting& c,
                  double f0, bool syncAtZero = false) {
    BassCharacterStage s;
    t17Prepare(s, a, c, f0);
    if (syncAtZero)
        s.syncPhase(0.0f);
    return t17RenderBlocks(s, inL, inR, 512, [](std::size_t) {});
}

double t17MaxAbs(const std::vector<float>& y, std::size_t begin, std::size_t end) {
    double m = 0.0;
    for (std::size_t k = begin; k < end; ++k)
        m = std::max(m, std::abs(static_cast<double>(y[k])));
    return m;
}

double t17Db10(double x) { return 10.0 * std::log10(std::max(x, 1.0e-300)); }

/// Sorted ClickDetector detection indices (documented defaults, sampleRate = fs).
std::vector<std::size_t> t17Detections(const std::vector<float>& y) {
    BcsTu::ClickDetectorConfig cfg{};
    cfg.sampleRate = static_cast<float>(kBcsFs);
    BcsTu::ClickDetector detector(cfg);
    detector.prepare();
    const std::vector<BcsTu::ClickDetection> d = detector.detect(y.data(), y.size());
    std::vector<std::size_t> idx;
    idx.reserve(d.size());
    for (const BcsTu::ClickDetection& c : d)
        idx.push_back(c.sampleIndex);
    std::sort(idx.begin(), idx.end());
    return idx;
}

std::vector<std::size_t> t17Merged(const std::vector<float>& a, const std::vector<float>& b) {
    std::vector<std::size_t> d = t17Detections(a);
    const std::vector<std::size_t> e = t17Detections(b);
    d.insert(d.end(), e.begin(), e.end());
    std::sort(d.begin(), d.end());
    return d;
}

/// Distance from k to the nearest entry of a sorted index list (kT17None when empty).
std::size_t t17Nearest(const std::vector<std::size_t>& sorted, std::size_t k) {
    std::size_t best = kT17None;
    const auto it = std::lower_bound(sorted.begin(), sorted.end(), k);
    if (it != sorted.end())
        best = std::min(best, *it - k);
    if (it != sorted.begin())
        best = std::min(best, k - *(it - 1));
    return best;
}

/// Positive-control event: candidates every 8 samples over one period of f0 (+ 64) from
/// kT17ControlFrom. A candidate is valid when it lies at least 2·N from every detection of either
/// reference (a snap inside ±N of a reference detection is masked by the differential rule by
/// construction, see Rectifier_BlendStepClickFree); the valid candidate with the largest jump proxy
/// wins. With none valid, kT17ControlFrom is returned and `valid` is false (recorded by the caller).
template <class Jump>
std::size_t t17ControlEvent(double f0, const std::vector<std::size_t>& refDet, std::size_t n, const Jump& jump,
                            bool& valid) {
    const auto windowN = static_cast<std::size_t>(std::floor(kBcsFs / f0 / 8.0));
    const auto period = static_cast<std::size_t>(std::ceil(kBcsFs / f0));
    std::size_t best = kT17ControlFrom;
    double bestScore = -1.0;
    valid = false;
    for (std::size_t e = kT17ControlFrom; e < kT17ControlFrom + period + 64; e += 8) {
        if (e + 2 * windowN >= n || t17Nearest(refDet, e) < 2 * windowN)
            continue;
        const double score = jump(e);
        if (score > bestScore) {
            bestScore = score;
            best = e;
            valid = true;
        }
    }
    return best;
}

/// The sample S = grid·m + offset in [kT17EventLo, kT17EventHi) whose S·f0/fs (f0 as the stage's
/// float) lies nearest an even integer (see the method note above).
std::size_t t17AlignedEvent(double f0, std::size_t grid, std::size_t offset) {
    const auto f = static_cast<double>(static_cast<float>(f0));
    std::size_t best = kT17EventLo;
    double bestErr = 2.0;
    for (std::size_t m = kT17EventLo / grid; m * grid + offset < kT17EventHi; ++m) {
        const std::size_t s = m * grid + offset;
        if (s < kT17EventLo)
            continue;
        const double twoPeriods = static_cast<double>(s) * f / (2.0 * kBcsFs);
        const double err = std::abs(twoPeriods - std::round(twoPeriods));
        if (err < bestErr) {
            bestErr = err;
            best = s;
        }
    }
    return best;
}

/// The C-16 switch gains the stage applies, mirrored sample for sample: before `start` g_a = 1,
/// g_b = 0; at start + j (j = 0 .. len - 1) pos = min(1, (j + 1)·inc), inc = crossfadeIncrement(10, fs),
/// g_a = cos(πpos/2)^α, g_b = sin(πpos/2)^α; from start + len on g_a = 0, g_b = 1.
void t17SwitchGains(std::size_t n, std::size_t start, std::size_t len, std::vector<float>& ga,
                    std::vector<float>& gb) {
    const float inc = crossfadeIncrement(BassCharacterStage::kSwitchCrossfadeMs, kBcsFs);
    ga.assign(n, 1.0f);
    gb.assign(n, 0.0f);
    for (std::size_t k = start; k < n; ++k) {
        const std::size_t count = k - start + 1;
        if (count > len) {
            ga[k] = 0.0f;
            gb[k] = 1.0f;
            continue;
        }
        const float pos = std::min(1.0f, static_cast<float>(count) * inc);
        ga[k] = std::pow(std::max(0.0f, std::cos(pos * Krate::DSP::kHalfPi)), kT17Alpha);
        gb[k] = std::pow(std::max(0.0f, std::sin(pos * Krate::DSP::kHalfPi)), kT17Alpha);
    }
}

/// Spec "Transition level L_tr[k]": RMS over exactly one period of f0 (rounded to the nearest
/// sample) centred on k, in dBFS, for k in [begin, end).
std::vector<double> t17SlidingLevel(const std::vector<float>& y, double f0, std::size_t begin, std::size_t end) {
    const auto T = static_cast<std::size_t>(std::llround(kBcsFs / f0));
    REQUIRE(T >= 2);
    REQUIRE(begin >= T / 2);
    REQUIRE(end > begin);
    REQUIRE(end - T / 2 + T <= y.size());
    std::vector<double> prefix(y.size() + 1, 0.0);
    for (std::size_t i = 0; i < y.size(); ++i)
        prefix[i + 1] = prefix[i] + static_cast<double>(y[i]) * static_cast<double>(y[i]);
    std::vector<double> out;
    out.reserve(end - begin);
    for (std::size_t k = begin; k < end; ++k) {
        const std::size_t lo = k - T / 2;
        const std::size_t hi = lo + T;
        out.push_back(t17Db10((prefix[hi] - prefix[lo]) / static_cast<double>(T)));
    }
    return out;
}

std::string t17EvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase5-shape-character/evidence/";
}

// ---- SC-009 / SC-021 positive controls -------------------------------------------------------

/// Jump proxy for a control event: |a| + |b - a| at sample e of the two references.
double t17JumpProxy(const std::vector<float>& a, const std::vector<float>& b, std::size_t e) {
    const auto x = static_cast<double>(a[e]);
    return std::abs(x) + std::abs(static_cast<double>(b[e]) - x);
}

/// Jump proxy for an event that leaves the FR-054 bypass with seamHardBypass_: leaving bypass resets
/// the branch and its round trip (plan S5.3 step 3), so the output steps from the bypass value a[e] to
/// the cold path’s near-zero first sample and only then rises toward b over the halfband start-up.
/// The instantaneous jump is |a[e]|; |a| + |b - a| would favour a ≈ 0, where no step is made (T022
/// run log: C2 kSawAnchor control (ii) was placed at a = -0.0018, b = -0.498 and counted 0; the 0.0137 ≤ |a|
/// candidates of that cell counted 1 or 2).
double t17ColdStartJumpProxy(const std::vector<float>& a, std::size_t e) {
    return std::abs(static_cast<double>(a[e]));
}

/// Control (i): a hard algorithm switch Tube -> Fracture (seamHardSwitch_: the law switches at once,
/// no crossfade), Amount 1, Tone 0.5. References: Tube and Fracture held.
BcsSct::ClickCount t17HardSwitchControl(const std::vector<float>& in, double f0, std::size_t& at, bool& valid) {
    const std::vector<float> yA = t17Held(in, in, BcsAlgo::Tube, kT17SwitchSetting, f0).l;
    const std::vector<float> yB = t17Held(in, in, BcsAlgo::Fracture, kT17SwitchSetting, f0).l;
    const std::vector<std::size_t> det = t17Merged(yA, yB);
    at = t17ControlEvent(
        f0, det, in.size(), [&](std::size_t e) { return t17JumpProxy(yA, yB, e); }, valid);
    BassCharacterStage s;
    BcsProbe::setSeamHardSwitch(s, true);
    t17Prepare(s, BcsAlgo::Tube, kT17SwitchSetting, f0);
    const std::size_t event = at;
    const T17Stereo y =
        t17RenderEvents(s, in, in, 512, {event}, [&](std::size_t) { s.setAlgorithm(BcsAlgo::Fracture); });
    return BcsSct::differentialClicks(y.l.data(), yA.data(), yB.data(), in.size(), kBcsFs, f0);
}

/// Control (ii): an unsmoothed full-range Amount 0 -> 1 step (Rectify, Tone 0.5). Amount 0 is the
/// FR-054 bypass, whose 5 ms ramp would itself smooth the step, so "unsmoothed" snaps both the
/// parameter smoothers (seamUnsmoothed_) and the bypass crossfade (seamHardBypass_).
BcsSct::ClickCount t17UnsmoothedStepControl(const std::vector<float>& in, double f0, std::size_t& at, bool& valid) {
    constexpr T17Setting kFrom{.amount = 0.0f, .tone = 0.5f, .amountOffset = 0.0f, .toneOffset = 0.0f};
    constexpr T17Setting kTo{.amount = 1.0f, .tone = 0.5f, .amountOffset = 0.0f, .toneOffset = 0.0f};
    const std::vector<float> yA = t17Held(in, in, BcsAlgo::Rectify, kFrom, f0).l;
    const std::vector<float> yB = t17Held(in, in, BcsAlgo::Rectify, kTo, f0).l;
    const std::vector<std::size_t> det = t17Merged(yA, yB);
    at = t17ControlEvent(
        f0, det, in.size(), [&](std::size_t e) { return t17ColdStartJumpProxy(yA, e); }, valid);
    BassCharacterStage s;
    BcsProbe::setSeamUnsmoothed(s, true);
    BcsProbe::setSeamHardBypass(s, true);
    t17Prepare(s, BcsAlgo::Rectify, kFrom, f0);
    const std::size_t event = at;
    const T17Stereo y = t17RenderEvents(s, in, in, 512, {event}, [&](std::size_t) { t17Apply(s, kTo); });
    return BcsSct::differentialClicks(y.l.data(), yA.data(), yB.data(), in.size(), kBcsFs, f0);
}

/// FR-054 control: a hard, uncrossfaded bypass switch (seamHardBypass_), Amount 0 -> `small`, Tone 0.5.
BcsSct::ClickCount t17HardBypassControl(const std::vector<float>& in, double f0, BcsAlgo a, float small,
                                        std::size_t& at, bool& valid) {
    const T17Setting from{.amount = 0.0f, .tone = 0.5f, .amountOffset = 0.0f, .toneOffset = 0.0f};
    const T17Setting to{.amount = small, .tone = 0.5f, .amountOffset = 0.0f, .toneOffset = 0.0f};
    const std::vector<float> yA = t17Held(in, in, a, from, f0).l;
    const std::vector<float> yB = t17Held(in, in, a, to, f0).l;
    const std::vector<std::size_t> det = t17Merged(yA, yB);
    at = t17ControlEvent(
        f0, det, in.size(), [&](std::size_t e) { return t17ColdStartJumpProxy(yA, e); }, valid);
    BassCharacterStage s;
    BcsProbe::setSeamHardBypass(s, true);
    t17Prepare(s, a, from, f0);
    const std::size_t event = at;
    const T17Stereo y = t17RenderEvents(s, in, in, 512, {event}, [&](std::size_t) { t17Apply(s, to); });
    return BcsSct::differentialClicks(y.l.data(), yA.data(), yB.data(), in.size(), kBcsFs, f0);
}

/// WARNs one control cell; returns 1 when it flagged.
std::size_t t17RecordControl(const char* note, const char* fixture, const char* what, const BcsSct::ClickCount& c,
                             std::size_t at, bool valid) {
    WARN(note << ", " << fixture << " control " << what << " at sample " << at
              << (valid ? "" : " (no candidate clear of reference detections)") << ": counted " << c.counted
              << " (guarded out " << c.guardedOut << ", transition detections " << c.transitionDetections << ", reference detections "
              << c.referenceDetections << ", N " << c.windowN << ")"
              << (c.counted >= 1u ? " - FLAGGED" : " - not flagged (recorded)"));
    return c.counted >= 1u ? std::size_t{1} : std::size_t{0};
}

// -----------------------------------------------------------------------------
// FR-026 / FR-054 pre-roll (user ruling 2026-10-16, Clarifications "Group 9 rulings, round 8")
// -----------------------------------------------------------------------------
// (a) During the pre-roll the output is bit-identical to the outgoing algorithm held (the incoming
//     branch is running but unmixed; two branches run). (b) At the end of the pre-roll the incoming
//     slot's residual DC blocker is primed: Rectify at Amount 1 on kSawAnchor E1, whose cold start
//     carries a residual DC of ~0.43, has a one-period level within SC-009's +1.5 dB of its steady
//     level from the first full-gain period on (measured before the ruling: +5 dB, draining over
//     ~80 ms). (c) Leaving the Amount-0 bypass pre-rolls the same way: the output stays the copy for
//     one period, then e ramps; the primed Rectify branch meets the same level bound.
TEST_CASE("BassCharacter_SwitchPreroll", "[systems][profundum][shape-character][bass-character]") {
    namespace Sct = Krate::DSP::ShapeCharacterTest;
    const double f0 = Sct::kE1Hz;
    const std::vector<float> in = Sct::fixture(SpectralShapeRecipe::kSawAnchor, f0, kBcsFs, 1.5);
    const std::size_t n = in.size();
    const auto T = static_cast<std::size_t>(std::llround(kBcsFs / f0));
    const auto levelDb = [&](const std::vector<float>& y, std::size_t k) {
        double acc = 0.0;
        for (std::size_t i = k; i < k + T; ++i)
            acc += static_cast<double>(y[i]) * y[i];
        return 20.0 * std::log10(std::sqrt(acc / static_cast<double>(T)) + 1e-30);
    };
    const std::vector<float> yTube = t17Held(in, in, BcsAlgo::Tube, kT17SwitchSetting, f0).l;
    const std::vector<float> yRect = t17Held(in, in, BcsAlgo::Rectify, kT17SwitchSetting, f0).l;
    const double steadyRect = levelDb(yRect, kT17SteadyBegin);

    SECTION("(a), (b): switch Tube -> Rectify") {
        BassCharacterStage s;
        t17Prepare(s, BcsAlgo::Tube, kT17SwitchSetting, f0);
        const std::size_t pre = BcsProbe::prerollLength(s);
        const std::size_t len = BcsProbe::crossfadeLength(s);
        REQUIRE(pre == T);
        const std::size_t at = t17AlignedEvent(f0, 512, 200);
        std::size_t maxRun = 0;
        bool sawPreroll = false;
        const T17Stereo y = t17RenderEvents(
            s, in, in, 512, {at}, [&](std::size_t) { s.setAlgorithm(BcsAlgo::Rectify); },
            [&](const BassCharacterStage& st) {
                maxRun = std::max(maxRun, BcsProbe::runningBranchCount(st));
                sawPreroll = sawPreroll || BcsProbe::prerolling(st);
            });
        CHECK(sawPreroll);
        CHECK(maxRun == 2u);
        // (a) bit-identical to Tube held up to the last pre-roll sample.
        std::size_t firstDiff = n;
        for (std::size_t k = 0; k < n; ++k) {
            if (std::bit_cast<std::uint32_t>(y.l[k]) != std::bit_cast<std::uint32_t>(yTube[k])) {
                firstDiff = k;
                break;
            }
        }
        INFO("first sample differing from Tube held: " << firstDiff << "; request " << at << ", pre-roll " << pre);
        CHECK(firstDiff >= at + pre);
        CHECK(firstDiff < at + pre + 8);  // the crossfade does start right after the pre-roll
        // (b) one-period level from the first full-gain period on: within +1.5 dB of Rectify's steady level.
        double worst = -1e9;
        for (std::size_t k = at + pre + len; k + T <= std::min(n, at + pre + len + 8 * T); k += T / 4)
            worst = std::max(worst, levelDb(y.l, k) - steadyRect);
        INFO("worst one-period level over 8 periods after the crossfade: " << worst << " dB above steady (bar +1.5)");
        CHECK(worst <= 1.5);
        CHECK(BcsProbe::current(s) == BcsAlgo::Rectify);
        CHECK_FALSE(BcsProbe::crossfading(s));
        CHECK(s.stateFinite());
    }

    SECTION("(c): leaving the Amount-0 bypass into Rectify") {
        const T17Setting off{.amount = 0.0f, .tone = 0.5f, .amountOffset = 0.0f, .toneOffset = 0.0f};
        BassCharacterStage s;
        t17Prepare(s, BcsAlgo::Rectify, off, f0);
        REQUIRE(BcsProbe::bypassed(s));
        const std::size_t pre = BcsProbe::prerollLength(s);
        const std::size_t at = t17AlignedEvent(f0, 512, 200);
        bool sawPreroll = false;
        const T17Stereo y = t17RenderEvents(
            s, in, in, 512, {at}, [&](std::size_t) { s.setAmount(1.0f); },
            [&](const BassCharacterStage& st) { sawPreroll = sawPreroll || BcsProbe::engagePrerolling(st); });
        CHECK(sawPreroll);
        std::size_t firstDiff = n;
        for (std::size_t k = 0; k < n; ++k) {
            if (std::bit_cast<std::uint32_t>(y.l[k]) != std::bit_cast<std::uint32_t>(in[k])) {
                firstDiff = k;
                break;
            }
        }
        INFO("first sample differing from the copy: " << firstDiff << "; request " << at << ", pre-roll " << pre);
        CHECK(firstDiff >= at + pre);
        CHECK(firstDiff < at + pre + 8);
        // The Amount smoother and the 5 ms engage ramp settle well inside 4 periods; then the level bound.
        double worst = -1e9;
        for (std::size_t k = at + pre + 4 * T; k + T <= std::min(n, at + pre + 12 * T); k += T / 4)
            worst = std::max(worst, levelDb(y.l, k) - steadyRect);
        INFO("worst one-period level over periods 4..12 after the pre-roll: " << worst << " dB above steady (bar +1.5)");
        CHECK(worst <= 1.5);
        CHECK_FALSE(BcsProbe::bypassed(s));
        CHECK(s.stateFinite());
    }
}

// ---- SC-009 switch cell ----------------------------------------------------------------------

/// One SC-009 cell: A -> B requested mid host block (process 200 samples, setAlgorithm, process 312),
/// Amount 1, Tone 0.5, L == R input. yA, yB: A and B held at the same settings (the steady renders).
void t17CheckSwitch(const std::vector<float>& in, const T17Note& note, BcsAlgo a, BcsAlgo b,
                    const std::vector<float>& yA, const std::vector<float>& yB) {
    const double f0 = note.hz;
    const std::size_t n = in.size();
    const std::size_t at = t17AlignedEvent(f0, 512, 200);
    REQUIRE(at % 512 == 200);

    BassCharacterStage s;
    t17Prepare(s, a, kT17SwitchSetting, f0);
    const std::size_t len = BcsProbe::crossfadeLength(s);
    // FR-026 pre-roll (user ruling 2026-10-16, round 8): the crossfade starts one period after the request;
    // during the pre-roll the outgoing branch alone is heard (g_a = 1).
    const std::size_t pre = BcsProbe::prerollLength(s);
    const std::size_t xfStart = at + pre;
    BcsProbe::resetMaxRunning(s);
    const T17Stereo y = t17RenderEvents(s, in, in, 512, {at}, [&](std::size_t) { s.setAlgorithm(b); });

    // D-11 bound over the pre-roll and the crossfade (Phase 4 SC-010(b) pattern).
    std::vector<float> ga;
    std::vector<float> gb;
    t17SwitchGains(n, xfStart, len, ga, gb);
    const double Da = BcsSct::steadyDeltaMax(yA.data(), kT17SteadyBegin, n);
    const double Db = BcsSct::steadyDeltaMax(yB.data(), kT17SteadyBegin, n);
    const double maxXa = t17MaxAbs(yA, kT17SteadyBegin, n);
    const double maxXb = t17MaxAbs(yB, kT17SteadyBegin, n);
    const std::size_t xEnd = xfStart + len + 1;
    REQUIRE(xEnd < n);
    const double ratio = BcsSct::d11Ratio(y.l.data(), ga.data(), gb.data(), Da, Db, maxXa, maxXb, at, xEnd);

    // Differential click count (two-reference form).
    const BcsSct::ClickCount cc = BcsSct::differentialClicks(y.l.data(), yA.data(), yB.data(), n, kBcsFs, f0);

    // Level: L_tr over [switch - T, switch + crossfade + T] against the two steady one-period levels,
    // measured with the same sliding one-period RMS on the held renders over the same window (a
    // stationary signal's one-period RMS still moves with the window position, so the steady level of
    // each render is the span it takes over the window).
    const auto T = static_cast<std::size_t>(std::llround(kBcsFs / f0));
    const std::size_t wBegin = at - T;
    const std::size_t wEnd = xfStart + len + T + 1;
    const std::vector<double> lTr = t17SlidingLevel(y.l, f0, wBegin, wEnd);
    const std::vector<double> lA = t17SlidingLevel(yA, f0, wBegin, wEnd);
    const std::vector<double> lB = t17SlidingLevel(yB, f0, wBegin, wEnd);
    const double steadyMin =
        std::min(*std::min_element(lA.begin(), lA.end()), *std::min_element(lB.begin(), lB.end()));
    const double steadyMax =
        std::max(*std::max_element(lA.begin(), lA.end()), *std::max_element(lB.begin(), lB.end()));
    const double trMin = *std::min_element(lTr.begin(), lTr.end());
    const double trMax = *std::max_element(lTr.begin(), lTr.end());

    INFO(note.name << ", " << t17AlgoName(a) << " -> " << t17AlgoName(b) << ", switch at sample " << at
                   << " (pre-roll " << pre << ", crossfade " << len << " samples): d11Ratio " << ratio << " (Da " << Da << ", Db " << Db
                   << ", max|xa| " << maxXa << ", max|xb| " << maxXb << "); counted " << cc.counted
                   << " (guarded out " << cc.guardedOut << ", transition detections " << cc.transitionDetections << ", reference detections "
                   << cc.referenceDetections << ", N " << cc.windowN << "); L_tr [" << trMin << ", " << trMax
                   << "] dBFS vs steady [" << steadyMin << ", " << steadyMax << "] dBFS, window [-2.5, +1.5] dB");
    // Level window [min - 2.5 dB, max + 1.5 dB] of the two steady levels (spec SC-009, user ruling
    // 2026-10-16: the floor of any fixed-exponent crossfade law; no level follower on the switch path).
    // The midpoint excursion is recorded per pair: L_tr at the crossfade midpoint against the span of the
    // two steady levels at the same position (signed distance outside that span, 0 inside it).
    constexpr double kLevelUnderDb = 2.5;
    constexpr double kLevelOverDb = 1.5;
    const std::size_t midIdx = xfStart + len / 2 - wBegin;
    const double midLo = std::min(lA[midIdx], lB[midIdx]);
    const double midHi = std::max(lA[midIdx], lB[midIdx]);
    double midExcursion = 0.0;
    if (lTr[midIdx] > midHi)
        midExcursion = lTr[midIdx] - midHi;
    else if (lTr[midIdx] < midLo)
        midExcursion = lTr[midIdx] - midLo;
    WARN("SC-009 level " << note.name << ", " << t17AlgoName(a) << " -> " << t17AlgoName(b) << ": over "
                         << (trMax - steadyMax) << " dB, under " << (trMin - steadyMin)
                         << " dB; midpoint L_tr " << lTr[midIdx] << " vs steady [" << midLo << ", " << midHi
                         << "] dBFS, midpoint excursion " << midExcursion << " dB");
    CHECK(ratio <= 1.0);
    CHECK(cc.counted == 0u);
    CHECK(trMin >= steadyMin - kLevelUnderDb);
    CHECK(trMax <= steadyMax + kLevelOverDb);
    CHECK(BcsProbe::current(s) == b);
    CHECK_FALSE(BcsProbe::crossfading(s));
    CHECK(BcsProbe::maxRunningBranchCount(s) <= 2);
    CHECK(s.stateFinite());
}

// ---- SC-021 parameter cell -------------------------------------------------------------------

/// One SC-021 cell: s0 -> s1 at `event` (a multiple of kT17ParamBlock) as a single step or as a
/// 20 ms ramp (setters every 64-sample block, linear, reaching s1 exactly). Both channels counted
/// against their own held references.
void t17CheckParamCell(const std::vector<float>& in, const T17Note& note, const char* fixture, BcsAlgo a,
                       const char* what, const T17Setting& s0, const T17Setting& s1, bool ramp,
                       const T17Stereo& ref0, const T17Stereo& ref1, std::size_t event, bool syncAtZero = false) {
    REQUIRE(event % kT17ParamBlock == 0);
    BassCharacterStage s;
    t17Prepare(s, a, s0, note.hz);
    if (syncAtZero)  // FR-072 / SC-030: the same sync as the references (t17Held syncAtZero)
        s.syncPhase(0.0f);
    const T17Stereo y = t17RenderBlocks(s, in, in, kT17ParamBlock, [&](std::size_t pos) {
        if (pos < event)
            return;
        if (!ramp) {
            if (pos == event)
                t17Apply(s, s1);
            return;
        }
        const std::size_t done = pos - event + kT17ParamBlock;
        if (done > kT17RampSamples + kT17ParamBlock)
            return;
        const double t = std::min(1.0, static_cast<double>(done) / static_cast<double>(kT17RampSamples));
        t17Apply(s, t17Lerp(s0, s1, t));
    });
    const std::size_t n = in.size();
    const BcsSct::ClickCount ccL =
        BcsSct::differentialClicks(y.l.data(), ref0.l.data(), ref1.l.data(), n, kBcsFs, note.hz);
    const BcsSct::ClickCount ccR =
        BcsSct::differentialClicks(y.r.data(), ref0.r.data(), ref1.r.data(), n, kBcsFs, note.hz);
    INFO(note.name << ", " << fixture << ", " << t17AlgoName(a) << ", " << what << " (Amount " << s0.amount << " -> "
                   << s1.amount << ", Tone " << s0.tone << " -> " << s1.tone << ", offsets (" << s0.amountOffset
                   << ", " << s0.toneOffset << ") -> (" << s1.amountOffset << ", " << s1.toneOffset << ")), "
                   << (ramp ? "20 ms ramp" : "single step") << " at sample " << event << ": counted L "
                   << ccL.counted << " / R " << ccR.counted << " (guarded out " << ccL.guardedOut << " / "
                   << ccR.guardedOut << ", transition detections "
                   << ccL.transitionDetections << " / " << ccR.transitionDetections << ", reference detections "
                   << ccL.referenceDetections << " / " << ccR.referenceDetections << ", N " << ccL.windowN << ")");
    CHECK(ccL.counted == 0u);
    CHECK(ccR.counted == 0u);
    CHECK(s.stateFinite());
}

/// SC-021 Crush cell (round-2 ruling 2026-10-16, spec "Differential click count" bullet "Crush"): Crush's
/// staircase positions depend on the setting, so no held render matches a moving m. Gate: the sample-delta
/// bound in the SC-003(b) form, per channel: every |dy| over [event, event + ramp + 50 ms) <= 1.1 x
/// steadyMax, steadyMax = the max steady |dy| (from kT17SteadyBegin) over a grid of >= 21 held settings
/// across the traversed range (computed by the caller). The differential count is recorded (WARN).
/// Returns the larger of the two channels' ratios (transition max / steady max).
double t17CheckCrushDeltaCell(const std::vector<float>& in, const T17Note& note, const char* fixture, const char* what,
                              const T17Setting& s0, const T17Setting& s1, bool ramp, const T17Stereo& ref0,
                              const T17Stereo& ref1, std::size_t event, const std::array<double, 2>& steadyMax) {
    REQUIRE(event % kT17ParamBlock == 0);
    BassCharacterStage s;
    t17Prepare(s, BcsAlgo::Crush, s0, note.hz);
    s.syncPhase(0.0f);  // FR-072 / SC-030: the same sync as the references and the grid
    const T17Stereo y = t17RenderBlocks(s, in, in, kT17ParamBlock, [&](std::size_t pos) {
        if (pos < event)
            return;
        if (!ramp) {
            if (pos == event)
                t17Apply(s, s1);
            return;
        }
        const std::size_t done = pos - event + kT17ParamBlock;
        if (done > kT17RampSamples + kT17ParamBlock)
            return;
        const double t = std::min(1.0, static_cast<double>(done) / static_cast<double>(kT17RampSamples));
        t17Apply(s, t17Lerp(s0, s1, t));
    });
    const std::size_t n = in.size();
    const std::size_t windowEnd =
        std::min(n, event + (ramp ? kT17RampSamples : std::size_t{0}) + static_cast<std::size_t>(0.050 * kBcsFs));
    const double trL = BcsSct::steadyDeltaMax(y.l.data(), event, windowEnd);
    const double trR = BcsSct::steadyDeltaMax(y.r.data(), event, windowEnd);
    REQUIRE(steadyMax[0] > 0.0);
    REQUIRE(steadyMax[1] > 0.0);
    const double ratioL = trL / steadyMax[0];
    const double ratioR = trR / steadyMax[1];
    const BcsSct::ClickCount ccL =
        BcsSct::differentialClicks(y.l.data(), ref0.l.data(), ref1.l.data(), n, kBcsFs, note.hz);
    const BcsSct::ClickCount ccR =
        BcsSct::differentialClicks(y.r.data(), ref0.r.data(), ref1.r.data(), n, kBcsFs, note.hz);
    WARN("SC-021 Crush " << note.name << ", " << fixture << ", " << what << " (Amount " << s0.amount << " -> "
                         << s1.amount << ", Tone " << s0.tone << " -> " << s1.tone << ", offsets (" << s0.amountOffset
                         << ", " << s0.toneOffset << ") -> (" << s1.amountOffset << ", " << s1.toneOffset << ")), "
                         << (ramp ? "20 ms ramp" : "single step") << ": max |dy| L " << trL << " / R " << trR
                         << " vs grid steady max " << steadyMax[0] << " / " << steadyMax[1] << ", ratio " << ratioL
                         << " / " << ratioR << " (bar 1.1); differential clicks recorded: counted L " << ccL.counted
                         << " / R " << ccR.counted << " (guarded out " << ccL.guardedOut << " / " << ccR.guardedOut
                         << ")");
    INFO(note.name << ", " << fixture << ", Crush, " << what << ", " << (ramp ? "20 ms ramp" : "single step")
                   << ": ratio L " << ratioL << " / R " << ratioR << " (bar 1.1)");
    CHECK(ratioL <= 1.1);
    CHECK(ratioR <= 1.1);
    CHECK(s.stateFinite());
    return std::max(ratioL, ratioR);
}

// ---- SC-029 ----------------------------------------------------------------------------------

/// SC-029(e): true if T has a setAlgorithmBlend(float). A requires-expression on a non-dependent type
/// with an invalid requirement is ill-formed outside a template ([expr.prim.req]), so it lives in a
/// concept.
template <class T>
concept T17HasAlgorithmBlend = requires(T s) { s.setAlgorithmBlend(0.5f); };

struct T17Request {
    std::size_t at;
    BcsAlgo algo;
};

/// Renders with setAlgorithm(requests[i].algo) right before sample requests[i].at, in host blocks of
/// `block` (1 = sample by sample). maxRunning: the largest runningBranchCount after any call.
T17Stereo t17RenderRequests(BassCharacterStage& s, const std::vector<float>& inL, const std::vector<float>& inR,
                            std::size_t block, const std::vector<T17Request>& requests, std::size_t& maxRunning) {
    std::vector<std::size_t> at;
    at.reserve(requests.size());
    for (const T17Request& q : requests)
        at.push_back(q.at);
    maxRunning = 0;
    return t17RenderEvents(
        s, inL, inR, block, at, [&](std::size_t i) { s.setAlgorithm(requests[i].algo); },
        [&](const BassCharacterStage& st) { maxRunning = std::max(maxRunning, BcsProbe::runningBranchCount(st)); });
}

}  // namespace

// -----------------------------------------------------------------------------
// SC-008: Amount 0 is bit-identical to the input once settled (FR-022, FR-054, C-16)
// -----------------------------------------------------------------------------
TEST_CASE("BassCharacter_AmountZeroBitIdentical", "[systems][profundum][shape-character][bass-character]") {
    // C-16: settle = max(100 ms, 5 x the longest smoother time constant / crossfade length in the stage).
    constexpr double kLongestMs = std::max({static_cast<double>(BassCharacterStage::kParamSmoothMs),
                                            static_cast<double>(BassCharacterStage::kSwitchCrossfadeMs),
                                            static_cast<double>(BassCharacterStage::kBypassCrossfadeMs)});
    constexpr double kSettleMs = std::max(100.0, 5.0 * kLongestMs);
    const auto settle = static_cast<std::size_t>(std::llround(kSettleMs * 0.001 * kBcsFs));
    const auto change = static_cast<std::size_t>(0.3 * kBcsFs);  // Amount 0.8 for 0.3 s
    const double f0 = BcsSct::kE1Hz;

    const std::vector<float> inL = BcsSct::fixture(SpectralShapeRecipe::kSawAnchor, f0, kBcsFs, 0.8);
    const std::size_t n = inL.size();
    std::vector<float> inR(n);
    for (std::size_t i = 0; i < n; ++i)
        inR[i] = inL[(i + 397) % n];  // a different right channel (unlinks the stage)
    REQUIRE(change + settle < n);
    INFO("settle used: " << kSettleMs << " ms = " << settle << " samples (C-16: max(100 ms, 5 x " << kLongestMs
                         << " ms)), counted from setAmount(0) at sample " << change);

    constexpr T17Setting kOn{.amount = 0.8f, .tone = 0.5f, .amountOffset = 0.0f, .toneOffset = 0.0f};
    for (const BcsAlgo a : kT17Algos) {
        INFO(t17AlgoName(a));
        BassCharacterStage s;
        t17Prepare(s, a, kOn, f0);
        const T17Stereo y =
            t17RenderEvents(s, inL, inR, 512, {change}, [&](std::size_t) { s.setAmount(0.0f); });
        CHECK_FALSE(bcsSameBits(y.l.data(), inL.data(), change));  // processed while Amount 0.8 (not inert)
        CHECK_FALSE(bcsSameBits(y.r.data(), inR.data(), change));
        const std::size_t from = change + settle;
        CHECK(bcsSameBits(y.l.data() + from, inL.data() + from, n - from));
        CHECK(bcsSameBits(y.r.data() + from, inR.data() + from, n - from));
        CHECK(BcsProbe::bypassed(s));
    }
}

// -----------------------------------------------------------------------------
// SC-009 / SC-021 positive controls (per SC arm and per note, ruling 2026-10-16)
// -----------------------------------------------------------------------------
TEST_CASE("BassCharacter_ClickCountPositiveControl", "[systems][profundum][shape-character][bass-character]") {
    // (i) a hard algorithm switch Tube -> Fracture (seamHardSwitch_), (ii) an unsmoothed Amount 0 -> 1
    // step (seamUnsmoothed_ + seamHardBypass_), at the SC-009 / SC-021 notes E1 and C2 over the SC-021
    // fixtures kSawAnchor and kGrowl. At each note at least one fixture must flag each control.
    const std::array<T17Note, 2> notes{{{"E1", BcsSct::kE1Hz}, {"C2", BcsSct::kC2Hz}}};
    const std::array<BcsSct::Fixture, 2> fixtures{{BcsSct::kFixtures[2], BcsSct::kFixtures[3]}};  // saw, growl
    for (const T17Note& note : notes) {
        std::size_t hardFlagged = 0;
        std::size_t unsmoothedFlagged = 0;
        for (const BcsSct::Fixture& fx : fixtures) {
            const std::vector<float> in = BcsSct::fixture(fx.controls, note.hz, kBcsFs, kT17ClickSeconds);
            std::size_t at = 0;
            bool valid = false;
            const BcsSct::ClickCount c1 = t17HardSwitchControl(in, note.hz, at, valid);
            hardFlagged += t17RecordControl(note.name, fx.name, "(i) hard switch Tube -> Fracture", c1, at, valid);
            const BcsSct::ClickCount c2 = t17UnsmoothedStepControl(in, note.hz, at, valid);
            unsmoothedFlagged +=
                t17RecordControl(note.name, fx.name, "(ii) unsmoothed Amount 0 -> 1", c2, at, valid);
        }
        INFO(note.name << ": control (i) flagged over the fixtures = " << hardFlagged
                       << ", control (ii) flagged = " << unsmoothedFlagged);
        CHECK(hardFlagged >= 1u);
        CHECK(unsmoothedFlagged >= 1u);
    }
}

// -----------------------------------------------------------------------------
// SC-009: the algorithm switch (D-7, D-11, C-16)
// -----------------------------------------------------------------------------
TEST_CASE("BassCharacter_SwitchClickFreeSmoke", "[systems][profundum][shape-character][bass-character]") {
    // The per-push sibling of BassCharacter_SwitchClickFree: Tube -> Bite and Rectify -> Fracture at E1.
    REQUIRE(BassCharacterStage::kSwitchCrossfadeExponent == kT17Alpha);
    const T17Note note{"E1", BcsSct::kE1Hz};
    const std::vector<float> in =
        BcsSct::fixture(SpectralShapeRecipe::kSawAnchor, note.hz, kBcsFs, kT17ClickSeconds);
    const std::array<std::pair<BcsAlgo, BcsAlgo>, 2> pairs{
        {{BcsAlgo::Tube, BcsAlgo::Bite}, {BcsAlgo::Rectify, BcsAlgo::Fracture}}};
    for (const auto& [a, b] : pairs) {
        const std::vector<float> yA = t17Held(in, in, a, kT17SwitchSetting, note.hz).l;
        const std::vector<float> yB = t17Held(in, in, b, kT17SwitchSetting, note.hz).l;
        t17CheckSwitch(in, note, a, b, yA, yB);
    }
}

TEST_CASE("BassCharacter_SwitchClickFree", "[systems][profundum][shape-character][bass-character][long]") {
    // SC-009: all 30 ordered pairs x {E1, C2}, kSawAnchor, Amount 1, Tone 0.5, 48 kHz, the switch
    // requested mid host block. Positive controls: BassCharacter_ClickCountPositiveControl.
    REQUIRE(BassCharacterStage::kSwitchCrossfadeExponent == kT17Alpha);
    const std::array<T17Note, 2> notes{{{"E1", BcsSct::kE1Hz}, {"C2", BcsSct::kC2Hz}}};
    for (const T17Note& note : notes) {
        const std::vector<float> in =
            BcsSct::fixture(SpectralShapeRecipe::kSawAnchor, note.hz, kBcsFs, kT17ClickSeconds);
        std::array<std::vector<float>, BassCharacterStage::kNumAlgorithms> held;
        for (std::size_t i = 0; i < kT17Algos.size(); ++i)
            held[i] = t17Held(in, in, kT17Algos[i], kT17SwitchSetting, note.hz).l;
        std::size_t pairs = 0;
        for (std::size_t i = 0; i < kT17Algos.size(); ++i) {
            for (std::size_t j = 0; j < kT17Algos.size(); ++j) {
                if (i == j)
                    continue;
                t17CheckSwitch(in, note, kT17Algos[i], kT17Algos[j], held[i], held[j]);
                ++pairs;
            }
        }
        CHECK(pairs == 30u);
    }
}

// -----------------------------------------------------------------------------
// SC-013: mono sum (FR-027, FR-063, D-12)
// -----------------------------------------------------------------------------
TEST_CASE("BassCharacter_MonoSum", "[systems][profundum][shape-character][bass-character]") {
    SECTION("(a) zero offsets, L == R input: L == R bit-identical") {
        const std::vector<float> in =
            BcsSct::fixture(SpectralShapeRecipe::kSawAnchor, BcsSct::kE1Hz, kBcsFs, 0.5);
        for (const BcsAlgo a : kT17Algos) {
            for (const float amount : {0.5f, 1.0f}) {
                INFO(t17AlgoName(a) << ", Amount " << amount);
                BassCharacterStage s;
                t17Prepare(s, a,
                           T17Setting{.amount = amount, .tone = 0.3f, .amountOffset = 0.0f, .toneOffset = 0.0f},
                           BcsSct::kE1Hz);
                const T17Stereo y = t17RenderBlocks(s, in, in, 512, [](std::size_t) {});
                CHECK(bcsSameBits(y.l, y.r));
                CHECK_FALSE(bcsSameBits(y.l, in));  // not inert
                CHECK(BcsProbe::linked(s));
            }
        }
    }

    SECTION("(b) band energy of (L + R)/2 below 2·f0 at the gated offsets; (c) the offsets are not inert") {
        // Spec SC-013(b), user ruling 2026-10-16 (Clarifications "Group 8 gate rulings"): Tube, Fold, Bite and
        // Crush are gated at ±0.15 (D-12). Rectify and Fracture rotate h1 in opposite directions on L and R
        // under their Amount / Tone offsets, so they are gated at the largest offset on {0.05, 0.10} that
        // meets 1 dB (the per-algorithm offset ceiling, a Phase 7 handoff WIDE must not exceed); their ±0.15
        // reading is recorded. (c) is measured at the gated offset of each algorithm.
        const std::string dir = t17EvidenceDir();
        std::filesystem::create_directories(dir);
        std::ofstream csv(dir + "sc013.csv");
        REQUIRE(csv.is_open());
        csv << "algorithm,note,f0_hz,offset,gated,band_bins,E_mid_db,E_L_db,E_R_db,mid_vs_LR_mean_db,thd_L_db,"
               "thd_R_db,dthd_floored_db,thd_counts,C_L_oct,C_R_oct,dC_oct,pass_b,pass_c\n";
        std::ofstream ceilings(dir + "sc013-offset-ceilings.txt");
        REQUIRE(ceilings.is_open());
        ceilings << "# SC-013(b) per-algorithm offset ceilings (user ruling 2026-10-16; Phase 7 handoff).\n"
                    "# Tube, Fold, Bite and Crush are gated at the full +-0.15 (D-12) and carry no ceiling line.\n"
                    "# algorithm,note,ceiling_offset,b_at_ceiling_db,b_at_0.15_db (ceiling 0 = none on {0.05, "
                    "0.10})\n";

        constexpr std::array<float, 2> kCeilingCandidates{0.10f, 0.05f};  // largest first
        const std::array<T17Note, 3> notes{
            {{"E0", BcsSct::kE0Hz}, {"E1", BcsSct::kE1Hz}, {"E2", BcsSct::kE2Hz}}};
        for (const T17Note& note : notes) {
            const double f0 = note.hz;
            const std::size_t lf = ProfundumTest::aliasFftLength(kBcsFs, f0);
            const std::size_t frame = std::max(BcsTu::kLowFrequencyFftSize, lf);
            // rule 9: 1 s settle + the longest frame needed.
            const double seconds = 1.0 + static_cast<double>(frame) / kBcsFs;
            const std::vector<float> in = BcsSct::fixture(SpectralShapeRecipe::kSawAnchor, f0, kBcsFs, seconds);
            const std::size_t n = in.size();
            REQUIRE(n >= static_cast<std::size_t>(kBcsFs) + frame);

            const double binHz = kBcsFs / static_cast<double>(lf);
            const std::size_t binTop = static_cast<std::size_t>(std::floor(2.0 * f0 / binHz)) - 8;  // 2·f0 − 8 bins
            REQUIRE(binTop > 9);

            struct Reading {
                float offset = 0.0f;
                double midVsMean = 0.0;
                double eM = 0.0;
                double eL = 0.0;
                double eR = 0.0;
                double thdL = 0.0;
                double thdR = 0.0;
                double dThd = 0.0;
                double cL = 0.0;
                double cR = 0.0;
                double dC = 0.0;
                bool passB = false;
                bool thdCounts = false;
                bool passC = false;
                bool finite = false;
            };
            // One render of algorithm a at Amount 0.5 / Tone 0.5 with equal Amount and Tone offsets ±off.
            const auto measure = [&](BcsAlgo a, float off) {
                BassCharacterStage s;
                t17Prepare(s, a, T17Setting{.amount = 0.5f, .tone = 0.5f, .amountOffset = off, .toneOffset = off},
                           f0);
                const T17Stereo y = t17RenderBlocks(s, in, in, 512, [](std::size_t) {});
                REQUIRE_FALSE(BcsProbe::linked(s));

                // (b) BH7 bins 9 .. (2·f0 − 8 bins) on the aliasFftLength frame (the last lf samples).
                std::vector<float> mid(lf);
                for (std::size_t i = 0; i < lf; ++i) {
                    const std::size_t k = n - lf + i;
                    mid[i] = static_cast<float>(0.5 * (static_cast<double>(y.l[k]) + static_cast<double>(y.r[k])));
                }
                const std::vector<double> pL = ProfundumTest::bh7PowerSpectrum(y.l.data() + (n - lf), lf);
                const std::vector<double> pR = ProfundumTest::bh7PowerSpectrum(y.r.data() + (n - lf), lf);
                const std::vector<double> pM = ProfundumTest::bh7PowerSpectrum(mid.data(), lf);
                Reading r;
                r.offset = off;
                for (std::size_t k = 9; k <= binTop; ++k) {
                    r.eL += pL[k];
                    r.eR += pR[k];
                    r.eM += pM[k];
                }
                r.midVsMean = t17Db10(r.eM) - t17Db10(0.5 * (r.eL + r.eR));
                r.passB = std::abs(r.midVsMean) <= 1.0;

                // (c) |THD(L) − THD(R)| >= 1 dB under the OQ-S floor, or |C(L) − C(R)| >= 0.05 oct.
                const BcsSct::Descriptors dL = BcsSct::describe(y.l.data(), n, kBcsFs, f0);
                const BcsSct::Descriptors dR = BcsSct::describe(y.r.data(), n, kBcsFs, f0);
                r.thdL = dL.Thd;
                r.thdR = dR.Thd;
                r.thdCounts = BcsSct::floorCounts(dL.Thd, dR.Thd);
                r.dThd = r.thdCounts ? std::abs(BcsSct::floorClamp(dL.Thd) - BcsSct::floorClamp(dR.Thd)) : 0.0;
                r.cL = dL.C;
                r.cR = dR.C;
                r.dC = std::abs(dL.C - dR.C);
                r.passC = r.dThd >= 1.0 || r.dC >= 0.05;
                r.finite = s.stateFinite();
                return r;
            };
            const auto record = [&](BcsAlgo a, const Reading& r, bool gated) {
                csv << t17AlgoName(a) << ',' << note.name << ',' << f0 << ',' << r.offset << ',' << (gated ? 1 : 0)
                    << ",9-" << binTop << ',' << t17Db10(r.eM) << ',' << t17Db10(r.eL) << ',' << t17Db10(r.eR)
                    << ',' << r.midVsMean << ',' << r.thdL << ',' << r.thdR << ',' << r.dThd << ','
                    << (r.thdCounts ? 1 : 0) << ',' << r.cL << ',' << r.cR << ',' << r.dC << ','
                    << (r.passB ? 1 : 0) << ',' << (r.passC ? 1 : 0) << '\n';
            };

            for (const BcsAlgo a : kT17Algos) {
                const Reading full = measure(a, kT17Offset);
                const bool ceilingGated = (a == BcsAlgo::Rectify || a == BcsAlgo::Fracture);
                Reading gated = full;
                bool found = !ceilingGated;
                if (ceilingGated) {
                    WARN("SC-013(b) recorded (not gated): " << note.name << ", " << t17AlgoName(a)
                                                            << " at +-0.15: mid band energy re the L/R mean "
                                                            << full.midVsMean << " dB");
                    record(a, full, false);
                    std::array<Reading, kCeilingCandidates.size()> cand{};
                    for (std::size_t c = 0; c < kCeilingCandidates.size(); ++c)
                        cand[c] = measure(a, kCeilingCandidates[c]);
                    gated = cand.back();  // (c) falls back to the smallest offset when no ceiling exists
                    std::size_t gatedIdx = cand.size();
                    for (std::size_t c = 0; c < cand.size(); ++c) {
                        if (cand[c].passB) {
                            gated = cand[c];
                            gatedIdx = c;
                            found = true;
                            break;
                        }
                    }
                    for (std::size_t c = 0; c < cand.size(); ++c)
                        record(a, cand[c], c == gatedIdx);
                    const float ceilingOffset = found ? gated.offset : 0.0f;
                    WARN("SC-013(b) offset ceiling: " << note.name << ", " << t17AlgoName(a) << " ceiling +-"
                                                      << ceilingOffset << " ((b) " << gated.midVsMean
                                                      << " dB at it; +-0.10 " << cand[0].midVsMean << " dB, +-0.05 "
                                                      << cand[1].midVsMean << " dB; +-0.15 " << full.midVsMean
                                                      << " dB)");
                    ceilings << t17AlgoName(a) << ',' << note.name << ',' << ceilingOffset << ',' << gated.midVsMean
                             << ',' << full.midVsMean << '\n';
                } else {
                    record(a, full, true);
                }

                INFO(note.name << ", " << t17AlgoName(a) << ", gated offsets (+-" << gated.offset << ", +-"
                               << gated.offset << "), Amount 0.5, Tone 0.5: (b) mid band energy re the L/R mean "
                               << gated.midVsMean << " dB (bins 9.." << binTop << "); (c) THD L " << gated.thdL
                               << " / R " << gated.thdR << " dB (floored difference " << gated.dThd << ", counts "
                               << gated.thdCounts << "), C L " << gated.cL << " / R " << gated.cR << " oct (|dC| "
                               << gated.dC << ")");
                CHECK(found);
                CHECK(gated.passB);
                CHECK(gated.passC);
                CHECK(gated.finite);
                CHECK(full.finite);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// SC-021: parameters are zipper-free; the Amount-0 bypass edge (FR-054)
// -----------------------------------------------------------------------------
TEST_CASE("ShapeCharacter_ParameterStepsClickFreeSmoke", "[systems][profundum][shape-character][bass-character]") {
    // Bite, kSawAnchor E1: Amount 0.2 -> 1 step; the bypass edge Amount 0 <-> 0.1 both ways (steps);
    // the hard-bypass control (seamHardBypass_) must be flagged.
    const T17Note note{"E1", BcsSct::kE1Hz};
    const std::vector<float> in =
        BcsSct::fixture(SpectralShapeRecipe::kSawAnchor, note.hz, kBcsFs, kT17ClickSeconds);
    const std::size_t event = t17AlignedEvent(note.hz, kT17ParamBlock, 0);
    constexpr T17Setting k02{.amount = 0.2f, .tone = 0.5f, .amountOffset = 0.0f, .toneOffset = 0.0f};
    constexpr T17Setting k1{.amount = 1.0f, .tone = 0.5f, .amountOffset = 0.0f, .toneOffset = 0.0f};
    constexpr T17Setting k0{.amount = 0.0f, .tone = 0.5f, .amountOffset = 0.0f, .toneOffset = 0.0f};
    constexpr T17Setting k01{.amount = 0.1f, .tone = 0.5f, .amountOffset = 0.0f, .toneOffset = 0.0f};
    const T17Stereo r02 = t17Held(in, in, BcsAlgo::Bite, k02, note.hz);
    const T17Stereo r1 = t17Held(in, in, BcsAlgo::Bite, k1, note.hz);
    const T17Stereo r0 = t17Held(in, in, BcsAlgo::Bite, k0, note.hz);
    const T17Stereo r01 = t17Held(in, in, BcsAlgo::Bite, k01, note.hz);
    REQUIRE(bcsSameBits(r0.l, in));  // Amount 0 held is the input (SC-008)

    t17CheckParamCell(in, note, "saw", BcsAlgo::Bite, "Amount", k02, k1, false, r02, r1, event);
    t17CheckParamCell(in, note, "saw", BcsAlgo::Bite, "bypass edge", k0, k01, false, r0, r01, event);
    t17CheckParamCell(in, note, "saw", BcsAlgo::Bite, "bypass edge", k01, k0, false, r01, r0, event);

    std::size_t at = 0;
    bool valid = false;
    const BcsSct::ClickCount c = t17HardBypassControl(in, note.hz, BcsAlgo::Bite, 0.1f, at, valid);
    const std::size_t flagged =
        t17RecordControl(note.name, "saw", "hard bypass Amount 0 -> 0.1 (Bite)", c, at, valid);
    CHECK(flagged == 1u);
}

TEST_CASE("ShapeCharacter_ParameterStepsClickFree", "[systems][profundum][shape-character][bass-character][long]") {
    // SC-021: every algorithm x {Amount, Tone, amount offset, tone offset} x {full-range step, 20 ms
    // ramp}, each in both directions, x {E1, C2} x {kSawAnchor, kGrowl}; plus the bypass edge (Amount
    // 0 <-> 0.05, both directions, step and 20 ms ramp) for every algorithm. Differential count == 0 on
    // both channels everywhere; positive controls (i), (ii) flagged per note and the hard-bypass control
    // flagged per note and algorithm. The offsets' full range is the gated ±0.15 (D-12). The TubeStage
    // bypass-threshold crossing is unreachable in the stage (saturation pinned at 1, T011), so it has
    // no cell. Crush cells (round-2 ruling 2026-10-16) are gated by the sample-delta bound over a 21-point
    // grid of the traversed settings (t17CheckCrushDeltaCell), the count recorded; every other cell keeps the
    // differential count with the amplitude guard. SC-030 / FR-072 (user ruling 2026-10-16, Group 9): the transition render and both held
    // references get the same syncPhase(0) at sample 0 (the fixtures are sine-phase from sample 0), so the
    // Crush and Fracture phasors run from the start and a branch started at a bypass exit picks up the
    // running phase: the references are phase-aligned with it. For the other algorithms the sync is inert.
    constexpr T17Setting kA0{.amount = 0.0f, .tone = 0.5f, .amountOffset = 0.0f, .toneOffset = 0.0f};
    constexpr T17Setting kA1{.amount = 1.0f, .tone = 0.5f, .amountOffset = 0.0f, .toneOffset = 0.0f};
    constexpr T17Setting kT0{.amount = 0.5f, .tone = 0.0f, .amountOffset = 0.0f, .toneOffset = 0.0f};
    constexpr T17Setting kT1{.amount = 0.5f, .tone = 1.0f, .amountOffset = 0.0f, .toneOffset = 0.0f};
    constexpr T17Setting kAoLo{.amount = 0.5f, .tone = 0.5f, .amountOffset = -kT17Offset, .toneOffset = 0.0f};
    constexpr T17Setting kAoHi{.amount = 0.5f, .tone = 0.5f, .amountOffset = kT17Offset, .toneOffset = 0.0f};
    constexpr T17Setting kToLo{.amount = 0.5f, .tone = 0.5f, .amountOffset = 0.0f, .toneOffset = -kT17Offset};
    constexpr T17Setting kToHi{.amount = 0.5f, .tone = 0.5f, .amountOffset = 0.0f, .toneOffset = kT17Offset};
    constexpr T17Setting kEdge{.amount = 0.05f, .tone = 0.5f, .amountOffset = 0.0f, .toneOffset = 0.0f};
    const std::array<T17Setting, 9> settings{kA0, kA1, kT0, kT1, kAoLo, kAoHi, kToLo, kToHi, kEdge};
    struct Move {
        const char* what;
        std::size_t from;
        std::size_t to;
    };
    const std::array<Move, 5> moves{
        {{"Amount", 0, 1}, {"Tone", 2, 3}, {"amount offset", 4, 5}, {"tone offset", 6, 7}, {"bypass edge", 0, 8}}};

    const std::array<T17Note, 2> notes{{{"E1", BcsSct::kE1Hz}, {"C2", BcsSct::kC2Hz}}};
    const std::array<BcsSct::Fixture, 2> fixtures{{BcsSct::kFixtures[2], BcsSct::kFixtures[3]}};  // saw, growl
    double crushWorstRatio = 0.0;
    for (const T17Note& note : notes) {
        const std::size_t event = t17AlignedEvent(note.hz, kT17ParamBlock, 0);
        std::size_t hardFlagged = 0;
        std::size_t unsmoothedFlagged = 0;
        std::array<std::size_t, BassCharacterStage::kNumAlgorithms> bypassFlagged{};
        for (const BcsSct::Fixture& fx : fixtures) {
            const std::vector<float> in = BcsSct::fixture(fx.controls, note.hz, kBcsFs, kT17ClickSeconds);
            for (std::size_t ai = 0; ai < kT17Algos.size(); ++ai) {
                const BcsAlgo a = kT17Algos[ai];
                std::vector<T17Stereo> held;
                held.reserve(settings.size());
                for (const T17Setting& c : settings)
                    held.push_back(t17Held(in, in, a, c, note.hz, true));
                for (const Move& m : moves) {
                    if (a == BcsAlgo::Crush) {
                        // Round-2 ruling: the sample-delta bound over a 21-point grid of held settings across
                        // the traversed range (the bypass edge's Amount-0 end is the bypass copy).
                        constexpr std::size_t kGrid = 21;
                        std::array<double, 2> steadyMax{0.0, 0.0};
                        for (std::size_t g = 0; g < kGrid; ++g) {
                            const double t = static_cast<double>(g) / static_cast<double>(kGrid - 1);
                            const T17Stereo h =
                                t17Held(in, in, a, t17Lerp(settings[m.from], settings[m.to], t), note.hz, true);
                            steadyMax[0] =
                                std::max(steadyMax[0], BcsSct::steadyDeltaMax(h.l.data(), kT17SteadyBegin, h.l.size()));
                            steadyMax[1] =
                                std::max(steadyMax[1], BcsSct::steadyDeltaMax(h.r.data(), kT17SteadyBegin, h.r.size()));
                        }
                        for (const bool ramp : {false, true}) {
                            crushWorstRatio = std::max(
                                crushWorstRatio,
                                t17CheckCrushDeltaCell(in, note, fx.name, m.what, settings[m.from], settings[m.to],
                                                       ramp, held[m.from], held[m.to], event, steadyMax));
                            crushWorstRatio = std::max(
                                crushWorstRatio,
                                t17CheckCrushDeltaCell(in, note, fx.name, m.what, settings[m.to], settings[m.from],
                                                       ramp, held[m.to], held[m.from], event, steadyMax));
                        }
                        continue;
                    }
                    for (const bool ramp : {false, true}) {
                        t17CheckParamCell(in, note, fx.name, a, m.what, settings[m.from], settings[m.to], ramp,
                                          held[m.from], held[m.to], event, true);
                        t17CheckParamCell(in, note, fx.name, a, m.what, settings[m.to], settings[m.from], ramp,
                                          held[m.to], held[m.from], event, true);
                    }
                }
                std::size_t at = 0;
                bool valid = false;
                const BcsSct::ClickCount cb = t17HardBypassControl(in, note.hz, a, kEdge.amount, at, valid);
                bypassFlagged[ai] += t17RecordControl(note.name, fx.name, t17AlgoName(a), cb, at, valid);
            }
            std::size_t at = 0;
            bool valid = false;
            const BcsSct::ClickCount c1 = t17HardSwitchControl(in, note.hz, at, valid);
            hardFlagged += t17RecordControl(note.name, fx.name, "(i) hard switch Tube -> Fracture", c1, at, valid);
            const BcsSct::ClickCount c2 = t17UnsmoothedStepControl(in, note.hz, at, valid);
            unsmoothedFlagged +=
                t17RecordControl(note.name, fx.name, "(ii) unsmoothed Amount 0 -> 1", c2, at, valid);
        }
        INFO(note.name << ": control (i) flagged = " << hardFlagged << ", control (ii) flagged = "
                       << unsmoothedFlagged);
        CHECK(hardFlagged >= 1u);
        CHECK(unsmoothedFlagged >= 1u);
        for (std::size_t ai = 0; ai < kT17Algos.size(); ++ai) {
            INFO(note.name << ", " << t17AlgoName(kT17Algos[ai])
                           << ": hard-bypass control (Amount 0 -> 0.05) flagged over the fixtures = "
                           << bypassFlagged[ai]);
            CHECK(bypassFlagged[ai] >= 1u);
        }
    }
    WARN("SC-021 Crush sample-delta bound: worst ratio over every Crush cell " << crushWorstRatio << " (bar 1.1)");
}

// -----------------------------------------------------------------------------
// SC-030: onset invariance under phasor sync (FR-072)
// -----------------------------------------------------------------------------
namespace {

/// One SC-030 render: L, and for Crush the two hold engines' slot memories at the end (channel 0).
struct T30Out {
    std::vector<float> y;
    std::array<BcsProbe::CrushSlots, 2> slots{};
};

/// One SC-030 render: the stage set up before prepare (Tone 0.5), optionally synced right after prepare,
/// over in[0, len) in 512-sample host blocks. No Catch2 macros: it runs on worker threads.
T30Out t30Render(BcsAlgo a, float amount, double f0, const float* in, std::size_t len, bool sync,
                 float coreCycles) {
    BassCharacterStage s;
    s.setAlgorithm(a);
    s.setAmount(amount);
    s.setTone(0.5f);
    s.setFundamentalHz(static_cast<float>(f0));
    s.prepare(kBcsFs, 512);
    if (sync)
        s.syncPhase(coreCycles);
    std::vector<float> l(in, in + len);
    std::vector<float> r = l;
    for (std::size_t pos = 0; pos < len; pos += 512) {
        const std::size_t n = std::min<std::size_t>(512, len - pos);
        s.processBlock(l.data() + pos, r.data() + pos, n);
    }
    T30Out out;
    out.y = std::move(l);
    if (a == BcsAlgo::Crush) {
        out.slots[0] = BcsProbe::crushSlots(s, 0, 0);
        out.slots[1] = BcsProbe::crushSlots(s, 0, 1);
    }
    return out;
}

/// True when the descriptor frame (the last kLowFrequencyFftSize samples) is all zero: describe() would
/// have no harmonic power to normalise by.
bool t30Silent(const std::vector<float>& y) {
    const std::size_t frame = std::min<std::size_t>(BcsTu::kLowFrequencyFftSize, y.size());
    for (std::size_t k = y.size() - frame; k < y.size(); ++k) {
        if (y[k] != 0.0f)
            return false;
    }
    return true;
}

/// Spread (max − min) of the SC-030 descriptors over a set of renders. E_low, E_high, I, THD and R_oe
/// follow the OQ-S floor rule (spec "Descriptor definitions"): floored values, and a spread only when some
/// value lies above the count floor (otherwise 0). C and L are plain.
struct T30Spread {
    double c = 0.0;
    double l = 0.0;
    double elow = 0.0;
    double ehigh = 0.0;
    double i = 0.0;
    double thd = 0.0;
    double roe = 0.0;
};

T30Spread t30Spread(const std::vector<BcsSct::Descriptors>& d) {
    const auto span = [&](const auto& get) {
        double lo = 1.0e300;
        double hi = -1.0e300;
        for (const BcsSct::Descriptors& x : d) {
            lo = std::min(lo, get(x));
            hi = std::max(hi, get(x));
        }
        return hi - lo;
    };
    const auto counts = [&](const auto& get) {
        for (const BcsSct::Descriptors& x : d) {
            if (get(x) > BcsSct::kFloorCountDb)
                return true;
        }
        return false;
    };
    const auto floored = [&](const auto& get) {
        return counts(get) ? span([&](const BcsSct::Descriptors& x) { return BcsSct::floorClamp(get(x)); }) : 0.0;
    };
    T30Spread s;
    s.c = span([](const BcsSct::Descriptors& x) { return x.C; });
    s.l = span([](const BcsSct::Descriptors& x) { return x.L; });
    s.elow = floored([](const BcsSct::Descriptors& x) { return x.Elow; });
    s.ehigh = floored([](const BcsSct::Descriptors& x) { return x.Ehigh; });
    s.i = floored([](const BcsSct::Descriptors& x) { return x.I; });
    s.thd = floored([](const BcsSct::Descriptors& x) { return x.Thd; });
    if (counts([](const BcsSct::Descriptors& x) { return x.RoeNum; })
        || counts([](const BcsSct::Descriptors& x) { return x.RoeDen; }))
        s.roe = span([](const BcsSct::Descriptors& x) {
            return BcsSct::floorClamp(x.RoeNum) - BcsSct::floorClamp(x.RoeDen);
        });
    return s;
}

}  // namespace

TEST_CASE("BassCharacter_OnsetInvariance", "[systems][profundum][shape-character][bass-character]") {
    // SC-030 (FR-072): Crush and Fracture at Amount {0.5, 1}, Tone 0.5, kSawAnchor and kGrowl, E1 and C2,
    // 48 kHz. The same note is started at 8 sub-period offsets: the render input is the fixture from sample
    // s_k = round(k·T/8) on, and syncPhase gets the core's h1 phase at that sample, s_k·f0/fs cycles. The
    // fixture's partials are sine-phase from sample 0 (recipeAdditive), which is the core's phase origin
    // (edge at phase 0), so the constant added to the core phase is 0. Descriptors: describe() on the last
    // kLowFrequencyFftSize samples after the 1 s settle.
    // Bounds (main-loop harness ruling 2026-10-16): C spread ≤ 0.005 oct (the SC-006 cap, fixed: the SC-006
    // ε is window-phase noise, ~1e-8 oct here, not the onset question), 0.02 oct for Crush (round-5 harness
    // ruling 2026-10-16: a hysteresis quantiser is path-dependent by one quantiser step on a slot whose input
    // sits within ¼ step of a threshold, which the Amount-0.5 growl cells show as C 0.013 / 0.0116 oct with L
    // within 0.07 dB; the per-slot level differences across the 8 onsets are recorded per Crush cell); L,
    // E_low and E_high ≤ 0.1 dB; I ≤
    // 0.5 dB; THD and R_oe recorded, not gated (THD re h1 is ill-conditioned where Fracture at Amount 1
    // removes h1 by design). Fracture at Amount > 0 is described on the f0/2 lattice (describe() with f0/2 as the
    // lattice fundamental, the SC-011 Fracture convention; round-2 ruling). A cell whose output frame is silent (all zero) is recorded "silent" and skips
    // its gates. Positive control: the same 8 onsets without syncPhase exceed a bound in at least one cell.
    // Renders run on worker threads (no Catch2 macro off the main thread); descriptors on the main thread.
    constexpr std::size_t kOnsets = 8;
    constexpr double kCBoundOct = 0.005;
    constexpr double kCBoundCrushOct = 0.02;  // round-5 harness ruling 2026-10-16
    constexpr double kLevelBarDb = 0.1;
    constexpr double kBandBarDb = 0.1;
    constexpr double kIBarDb = 0.5;
    const std::array<T17Note, 2> notes{{{"E1", BcsSct::kE1Hz}, {"C2", BcsSct::kC2Hz}}};
    const std::array<std::pair<const char*, const SpectralShapeRecipe::Controls*>, 2> fixtures{
        {{"saw", &SpectralShapeRecipe::kSawAnchor}, {"growl", &SpectralShapeRecipe::kGrowl}}};
    constexpr std::array<BcsAlgo, 2> algos{BcsAlgo::Crush, BcsAlgo::Fracture};

    std::size_t controlCells = 0;
    std::string controlWhere;
    std::size_t silentCells = 0;
    for (const T17Note& note : notes) {
        const double f0 = note.hz;
        const double period = kBcsFs / f0;
        const auto spare = static_cast<std::size_t>(std::ceil(period)) + 1;
        const double seconds = 1.0 + static_cast<double>(BcsTu::kLowFrequencyFftSize) / kBcsFs + 0.05 + 2.0 / f0;
        for (const auto& [fxName, fxControls] : fixtures) {
            const std::vector<float> src = BcsSct::fixture(*fxControls, f0, kBcsFs, seconds);
            REQUIRE(src.size() > spare);
            const std::size_t len = src.size() - spare;
            REQUIRE(len >= static_cast<std::size_t>(kBcsFs) + BcsTu::kLowFrequencyFftSize);
            std::array<std::size_t, kOnsets> shift{};
            for (std::size_t k = 0; k < kOnsets; ++k)
                shift[k] = static_cast<std::size_t>(
                    std::llround(static_cast<double>(k) * period / static_cast<double>(kOnsets)));
            for (const BcsAlgo a : algos) {
                for (const float amount : {0.5f, 1.0f}) {
                    std::vector<std::future<T30Out>> synced;
                    std::vector<std::future<T30Out>> unsynced;
                    const double cBound = (a == BcsAlgo::Crush) ? kCBoundCrushOct : kCBoundOct;
                    std::vector<std::array<BcsProbe::CrushSlots, 2>> syncSlots;
                    for (std::size_t k = 0; k < kOnsets; ++k) {
                        const float* in = src.data() + shift[k];
                        // The fixture's actual starting phase: s_k / T = s_k·f0/fs cycles (s_k is rounded to a
                        // whole sample, so this is not exactly k/8; round-3 ruling 2026-10-16).
                        const auto cycles = static_cast<float>(static_cast<double>(shift[k]) * f0 / kBcsFs);
                        synced.push_back(
                            std::async(std::launch::async, t30Render, a, amount, f0, in, len, true, cycles));
                        unsynced.push_back(
                            std::async(std::launch::async, t30Render, a, amount, f0, in, len, false, 0.0f));
                    }

                    // Fracture at Amount > 0: the f0/2 lattice (its output period is 2/f0; the SC-011
                    // Fracture convention, bcsLatticeHz).
                    const double fLattice = (a == BcsAlgo::Fracture && amount > 0.0f) ? 0.5 * f0 : f0;
                    std::vector<BcsSct::Descriptors> dSync;
                    std::vector<BcsSct::Descriptors> dFree;
                    std::size_t silentSync = 0;
                    std::size_t silentFree = 0;
                    bool finite = true;
                    for (std::size_t k = 0; k < kOnsets; ++k) {
                        const T30Out os = synced[k].get();
                        const T30Out of = unsynced[k].get();
                        const std::vector<float>& ys = os.y;
                        const std::vector<float>& yf = of.y;
                        syncSlots.push_back(os.slots);
                        for (const float v : ys)
                            finite = finite && detail::isFinite(v);
                        if (t30Silent(ys))
                            ++silentSync;
                        else
                            dSync.push_back(BcsSct::describe(ys.data(), ys.size(), kBcsFs, fLattice));
                        if (t30Silent(yf))
                            ++silentFree;
                        else
                            dFree.push_back(BcsSct::describe(yf.data(), yf.size(), kBcsFs, fLattice));
                    }
                    const std::string cell = std::string(note.name) + ", " + fxName + ", " + t17AlgoName(a)
                                             + ", Amount " + (amount == 1.0f ? "1" : "0.5");
                    CHECK(finite);

                    // Control: unsynced onsets that differ in silence count as exceeding (one silent, one not).
                    const T30Spread u = t30Spread(dFree);
                    const bool controlFlags = (silentFree > 0 && silentFree < kOnsets) || u.c > cBound
                                              || u.l > kLevelBarDb || u.elow > kBandBarDb || u.ehigh > kBandBarDb
                                              || u.i > kIBarDb;
                    if (controlFlags) {
                        ++controlCells;
                        controlWhere += " [" + cell + "]";
                    }
                    if (silentSync > 0) {
                        ++silentCells;
                        WARN("SC-030 " << cell << ": silent (" << silentSync << " of " << kOnsets
                                       << " synced onsets have an all-zero frame; unsynced silent " << silentFree
                                       << "); gates skipped, recorded");
                        continue;
                    }
                    const T30Spread s = t30Spread(dSync);
                    if (a == BcsAlgo::Crush) {
                        // Recorded path-dependence (round-5 ruling): per engine, the slots whose stored level
                        // differs across the 8 synced onsets, and by how many quantiser steps.
                        std::ostringstream pd;
                        for (std::size_t e = 0; e < 2; ++e) {
                            const BcsProbe::CrushSlots& ref = syncSlots[0][e];
                            std::size_t differing = 0;
                            long long maxSteps = 0;
                            for (int j = 0; j < ref.m; ++j) {
                                const auto jj = static_cast<std::size_t>(j);
                                float lo = ref.level[jj];
                                float hi = ref.level[jj];
                                for (const auto& os : syncSlots) {
                                    lo = std::min(lo, os[e].level[jj]);
                                    hi = std::max(hi, os[e].level[jj]);
                                }
                                if (hi > lo) {
                                    ++differing;
                                    maxSteps = std::max(maxSteps, std::llround(static_cast<double>(hi - lo)
                                                                               / static_cast<double>(ref.step)));
                                }
                            }
                            pd << " engine " << e << " (m " << ref.m << "): " << differing << " of " << ref.m
                               << " slots differ, max " << maxSteps << " step(s);";
                        }
                        WARN("SC-030 " << cell << ": recorded path-dependence across the 8 synced onsets:" << pd.str());
                    }
                    WARN("SC-030 " << cell << ": synced spread C " << s.c << " oct, L " << s.l << ", E_low "
                                   << s.elow << ", E_high " << s.ehigh << ", I " << s.i << " dB (THD " << s.thd
                                   << ", R_oe " << s.roe << " dB recorded); unsynced spread C " << u.c << " oct, L "
                                   << u.l << ", E_low " << u.elow << ", E_high " << u.ehigh << ", I " << u.i
                                   << ", THD " << u.thd << ", R_oe " << u.roe << " dB, silent " << silentFree
                                   << " (control " << (controlFlags ? "flags" : "does not flag") << ")");
                    INFO(cell << ": synced spread C " << s.c << " oct (bound " << cBound << "), L " << s.l << ", E_low "
                              << s.elow << ", E_high " << s.ehigh << " dB (bar 0.1), I " << s.i << " dB (bar 0.5)");
                    CHECK(s.c <= cBound);
                    CHECK(s.l <= kLevelBarDb);
                    CHECK(s.elow <= kBandBarDb);
                    CHECK(s.ehigh <= kBandBarDb);
                    CHECK(s.i <= kIBarDb);
                }
            }
        }
    }
    WARN("SC-030 silent cells (recorded): " << silentCells);
    INFO("positive control (no syncPhase): cells exceeding a bound = " << controlCells << ":" << controlWhere);
    CHECK(controlCells >= 1u);
}

// -----------------------------------------------------------------------------
// SC-026 (b)(c)(d): observer hysteresis (FR-036, D-8, C-18); the crossfade rule (FR-030)
// -----------------------------------------------------------------------------
TEST_CASE("BassCharacter_RLawHysteresis", "[systems][profundum][shape-character][bass-character]") {
    // (d) one float.
    static_assert(
        std::is_same_v<decltype(std::declval<const BassCharacterStage&>().perceivedPitchRatio()), float>);

    const std::array<std::pair<float, std::size_t>, 3> tones{{{0.0f, 0}, {0.5f, 2}, {1.0f, 4}}};
    for (const auto& [tone, idx] : tones) {
        const BassCharacterStage::FlipEdges edges = BassCharacterStage::kRectifyFlipEdges[idx];
        const float band = edges.hi - edges.lo;
        REQUIRE(band > 0.0f);
        INFO("Tone " << tone << ": table band [" << edges.lo << ", " << edges.hi << "], width " << band);

        BassCharacterStage s;
        s.setInputHalfWaveSymmetric(true);
        bcsSetup(s, BcsAlgo::Rectify, 0.0f, tone);
        std::size_t clock = 0;

        // (b) Amount dithered by ±band/4 around each edge for 1000 steps. Approached from the side
        // the edge holds (below lo, above hi) r never changes; approached from the other side it steps
        // exactly once (the first crossing of the edge) and never toggles back.
        std::uint32_t rng = 0x6C8E9CF5u ^ static_cast<std::uint32_t>(idx + 1);
        for (const bool upper : {false, true}) {
            const float edge = upper ? edges.hi : edges.lo;
            for (const float approach : {0.0f, 1.0f}) {
                s.setAmount(approach);
                float prev = s.perceivedPitchRatio();
                REQUIRE(prev == (approach == 1.0f ? 2.0f : 1.0f));
                int changes = 0;
                for (int k = 0; k < 1000; ++k) {
                    rng ^= rng << 13;
                    rng ^= rng >> 17;
                    rng ^= rng << 5;
                    const auto u = static_cast<float>(static_cast<double>(rng >> 8) / 8388608.0 - 1.0);  // [-1, 1)
                    s.setAmount(edge + u * 0.25f * band);
                    const float r = s.perceivedPitchRatio();
                    if (r != prev)
                        ++changes;
                    prev = r;
                    if ((k & 15) == 0)
                        bcsRun(s, 4, clock);  // audio between control changes: the observer reads targets (C-18)
                }
                const bool heldSide = (upper && approach == 1.0f) || (!upper && approach == 0.0f);
                INFO((upper ? "upper" : "lower") << " edge " << edge << ", approached from Amount " << approach
                                                 << ": r changes over 1000 dithered steps = " << changes);
                CHECK(changes == (heldSide ? 0 : 1));
            }
        }

        // (c) reset() at an Amount inside the band reports the default side (r = 1).
        s.setAmount(1.0f);
        REQUIRE(s.perceivedPitchRatio() == 2.0f);
        const float inside = 0.5f * (edges.lo + edges.hi);
        s.setAmount(inside);
        REQUIRE(s.perceivedPitchRatio() == 2.0f);  // hysteresis holds inside the band
        s.reset();
        CHECK(s.perceivedPitchRatio() == 1.0f);
        CHECK_FALSE(BcsProbe::flippedRectify(s));
        bcsRun(s, 64, clock);
        CHECK(s.perceivedPitchRatio() == 1.0f);

        // (d) setChannelOffsets never changes r.
        const std::array<std::pair<float, float>, 4> offsets{
            {{kT17Offset, kT17Offset}, {-kT17Offset, -kT17Offset}, {kT17Offset, -kT17Offset}, {0.0f, 0.0f}}};
        for (const float amount : {0.0f, inside, 1.0f, inside}) {
            s.setAmount(amount);
            const float before = s.perceivedPitchRatio();
            for (const auto& [ao, to] : offsets) {
                s.setChannelOffsets(ao, to);
                CHECK(s.perceivedPitchRatio() == before);
                bcsRun(s, 8, clock);
                CHECK(s.perceivedPitchRatio() == before);
            }
        }

        // Crossfade (FR-030, D-8): Rectify -> Fracture with Amount past both flips; r == Fracture's r
        // from the setAlgorithm call on; a queued request (back to Rectify) leaves r unchanged until it
        // starts at the crossfade end.
        BassCharacterStage x;
        x.setInputHalfWaveSymmetric(true);
        bcsSetup(x, BcsAlgo::Rectify, 1.0f, tone);
        std::size_t xclock = 0;
        REQUIRE(x.perceivedPitchRatio() == 2.0f);
        REQUIRE(BcsProbe::flippedFracture(x));  // Amount 1 is past the Fracture flip too
        const std::size_t len = BcsProbe::prerollLength(x) + BcsProbe::crossfadeLength(x);  // FR-026 in-flight length
        x.setAlgorithm(BcsAlgo::Fracture);
        CHECK(x.perceivedPitchRatio() == 0.5f);
        std::size_t mismatches = 0;
        std::size_t queuedStartedAt = kT17None;
        for (std::size_t k = 0; k < 2 * len + 16; ++k) {
            if (k == 100) {
                x.setAlgorithm(BcsAlgo::Rectify);
                CHECK(BcsProbe::hasPending(x));
                CHECK(x.perceivedPitchRatio() == 0.5f);
            }
            bcsRun(x, 1, xclock);
            const bool rectifyTarget = BcsProbe::target(x) == BcsAlgo::Rectify;
            if (rectifyTarget && queuedStartedAt == kT17None) {
                queuedStartedAt = k;
                CHECK(BcsProbe::current(x) == BcsAlgo::Fracture);
            }
            if (x.perceivedPitchRatio() != (rectifyTarget ? 2.0f : 0.5f))
                ++mismatches;
        }
        INFO("crossfade " << len << " samples; the queued Rectify started after sample " << queuedStartedAt);
        CHECK(mismatches == 0u);
        CHECK(queuedStartedAt == len - 1);
        CHECK(BcsProbe::current(x) == BcsAlgo::Rectify);
    }
}

// -----------------------------------------------------------------------------
// SC-027: the dry share is phase-matched (FR-053, C-14)
// -----------------------------------------------------------------------------
TEST_CASE("BassCharacter_DryShareNoComb", "[systems][profundum][shape-character][bass-character]") {
    // seamIdentityLaw_ (y = lin = the branch's aligned input), Rectify, Tone 0.5, seeded white noise at
    // -12 dBFS RMS, 48 kHz. Reference (C-14): a standalone OversampledResidualPath fed lin = u[n-1]
    // (the ADAA2 alignment, one oversampled sample) and res = 0. Response: Welch, 2^14, Hann, 50 %
    // overlap, H = S_yx / S_xx; flat = max - min of 20·log10|H| over 20 Hz .. 0.45·fs.
    // Amount 0.2 is the spec cell (SC-027 row amended 2026-10-16, Clarifications "Group 9 rulings": dry
    // share 0.5, b = 0), where the identity-law residual, the flatness and the raw-dry control are gated.
    // At the drafted Amount 0.5 Rectify's dry share is 0 (wet = min(1, A / 0.4)), so the control cannot
    // comb there by construction: the 0.5 readings are recorded (WARN), not gated.
    constexpr std::size_t kSeg = 16384;
    constexpr std::size_t kHop = kSeg / 2;
    constexpr std::size_t kWarm = 4096;
    constexpr std::size_t kN = 16 * kSeg + kWarm;
    const std::vector<float> noise = BcsSct::whiteNoise(kN, 0x2545F491u, -12.0);

    const auto render = [&](float amount, bool rawDry, BassCharacterStage& s) {
        BcsProbe::setSeamIdentityLaw(s, true);
        BcsProbe::setSeamRawDry(s, rawDry);
        t17Prepare(s, BcsAlgo::Rectify,
                   T17Setting{.amount = amount, .tone = 0.5f, .amountOffset = 0.0f, .toneOffset = 0.0f},
                   BcsSct::kC2Hz);
        return t17RenderBlocks(s, noise, noise, 512, [](std::size_t) {}).l;
    };

    // Reference: the stage's linear path alone.
    std::vector<float> ref(kN);
    {
        OversampledResidualPath path;
        path.prepare(kBcsFs);
        REQUIRE(path.factor() == 4);
        std::array<float, OversampledResidualPath::kMaxFactor> u{};
        std::array<float, OversampledResidualPath::kMaxFactor> lin{};
        const std::array<float, OversampledResidualPath::kMaxFactor> res{};
        float prevU = 0.0f;
        for (std::size_t k = 0; k < kN; ++k) {
            path.upsample(noise[k], u.data());
            for (std::size_t i = 0; i < path.factor(); ++i) {
                lin[i] = prevU;  // u[n-1] at the oversampled rate
                prevU = u[i];
            }
            ref[k] = path.downsample(lin.data(), res.data());
        }
    }

    std::vector<double> window(kSeg);
    for (std::size_t i = 0; i < kSeg; ++i)
        window[i] = 0.5 - 0.5 * std::cos(ProfundumTest::kTwoPi * static_cast<double>(i) / static_cast<double>(kSeg));
    const auto response = [&](const std::vector<float>& y, double& ripple, double& maxDev) {
        std::vector<std::complex<double>> sxy(kSeg / 2 + 1);
        std::vector<double> sxx(kSeg / 2 + 1, 0.0);
        std::size_t segments = 0;
        for (std::size_t start = kWarm; start + kSeg <= kN; start += kHop) {
            std::vector<std::complex<double>> X(kSeg);
            std::vector<std::complex<double>> Y(kSeg);
            for (std::size_t i = 0; i < kSeg; ++i) {
                X[i] = std::complex<double>(static_cast<double>(noise[start + i]) * window[i], 0.0);
                Y[i] = std::complex<double>(static_cast<double>(y[start + i]) * window[i], 0.0);
            }
            ProfundumTest::fftDouble(X);
            ProfundumTest::fftDouble(Y);
            for (std::size_t k = 0; k <= kSeg / 2; ++k) {
                sxy[k] += Y[k] * std::conj(X[k]);
                sxx[k] += std::norm(X[k]);
            }
            ++segments;
        }
        REQUIRE(segments >= 30);
        const double binHz = kBcsFs / static_cast<double>(kSeg);
        const auto kLo = static_cast<std::size_t>(std::ceil(20.0 / binHz));
        const auto kHi = static_cast<std::size_t>(std::floor(0.45 * kBcsFs / binHz));
        double lo = 1.0e300;
        double hi = -1.0e300;
        maxDev = 0.0;
        for (std::size_t k = kLo; k <= kHi; ++k) {
            REQUIRE(sxx[k] > 0.0);
            const double db = 20.0 * std::log10(std::max(std::abs(sxy[k]) / sxx[k], 1.0e-300));
            lo = std::min(lo, db);
            hi = std::max(hi, db);
            maxDev = std::max(maxDev, std::abs(db));
        }
        ripple = hi - lo;
    };

    for (const float amount : {0.5f, 0.2f}) {
        BassCharacterStage s;
        const std::vector<float> y = render(amount, false, s);
        REQUIRE(s.oversamplingFactor() == 4);
        const BcsProbe::RectifyMix mix = BcsProbe::rectifyMix(s, 0);

        double sum = 0.0;
        for (std::size_t k = 0; k < kN; ++k) {
            const double d = static_cast<double>(y[k]) - static_cast<double>(ref[k]);
            sum += d * d;
        }
        const double residualDb = t17Db10(sum / static_cast<double>(kN));
        double ripple = 0.0;
        double maxDev = 0.0;
        response(y, ripple, maxDev);

        BassCharacterStage c;
        const std::vector<float> yc = render(amount, true, c);
        double rippleC = 0.0;
        double maxDevC = 0.0;
        response(yc, rippleC, maxDevC);
        const float dryShare = 1.0f - BcsProbe::rectifyMix(c, 0).wet;

        INFO("Amount " << amount << " (wet " << mix.wet << ", b " << mix.b << ", dry share " << dryShare
                       << "): residual vs the linear path " << residualDb << " dBFS (bar -100); response ripple "
                       << ripple << " dB, max |20 log10 H| " << maxDev << " dB (bar: ripple 0.1); raw-dry control "
                       << "ripple " << rippleC << " dB, max deviation " << maxDevC << " dB");
        if (amount == 0.2f) {
            REQUIRE(std::abs(mix.wet - 0.5f) <= 1.0e-6f);  // premise: a 0.5 dry share
            CHECK(residualDb <= -100.0);
            CHECK(ripple <= 0.1);
            CHECK(rippleC > 0.1);  // positive control: the raw-dry mix combs
        } else {
            WARN("SC-027 Amount 0.5 (recorded, not gated): Rectify dry share "
                 << dryShare << " (wet = min(1, A / 0.4)); residual " << residualDb << " dBFS, ripple " << ripple
                 << " dB; raw-dry control ripple " << rippleC << " dB");
        }
        CHECK(s.stateFinite());
    }
}

// -----------------------------------------------------------------------------
// SC-029: the algorithm-transition queue (FR-057, D-7, C-21)
// -----------------------------------------------------------------------------
TEST_CASE("BassCharacter_AlgorithmQueue", "[systems][profundum][shape-character][bass-character]") {
    // (e) API review: setAlgorithm is the only transition API.
    static_assert(!T17HasAlgorithmBlend<BassCharacterStage>);
    CHECK_FALSE(T17HasAlgorithmBlend<BassCharacterStage>);
    WARN("SC-029(e) API review: BassCharacterStage exposes no setAlgorithmBlend (the requires-expression is "
         "false); setAlgorithm is the only transition API");

    constexpr std::size_t kN = 8192;
    constexpr std::size_t kSwitch = 2 * 512 + 200;  // mid host block
    const double f0 = BcsSct::kE1Hz;
    const std::vector<float> inL = bcsSaw(f0, kN);
    const std::vector<float> inR = bcsSaw(f0, kN, 0.37);
    constexpr T17Setting kQ{.amount = 0.8f, .tone = 0.3f, .amountOffset = 0.1f, .toneOffset = -0.1f};

    std::size_t len = 0;
    {
        BassCharacterStage probe;
        t17Prepare(probe, BcsAlgo::Tube, kQ, f0);
        len = BcsProbe::prerollLength(probe) + BcsProbe::crossfadeLength(probe);  // FR-026 in-flight length
    }
    REQUIRE(len > 100);
    REQUIRE(kSwitch + 2 * len + 64 < kN);

    // (c) A request equal to the current algorithm while steady starts nothing (bit-identical to none).
    {
        BassCharacterStage s;
        BassCharacterStage r;
        t17Prepare(s, BcsAlgo::Bite, kQ, f0);
        t17Prepare(r, BcsAlgo::Bite, kQ, f0);
        std::size_t maxRun = 0;
        const T17Stereo ys = t17RenderRequests(s, inL, inR, 512, {{kSwitch, BcsAlgo::Bite}}, maxRun);
        CHECK(maxRun == 1u);
        std::size_t maxRef = 0;
        const T17Stereo yr = t17RenderRequests(r, inL, inR, 512, {}, maxRef);
        CHECK(bcsSameBits(ys.l, yr.l));
        CHECK(bcsSameBits(ys.r, yr.r));
        CHECK_FALSE(BcsProbe::crossfading(s));
    }

    std::uint32_t rng = 0x13579BDFu;
    const auto next = [&rng]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };
    const auto pick = [&]() { return kT17Algos[next() % kT17Algos.size()]; };

    constexpr std::size_t kTriples = 32;
    std::ostringstream printed;
    std::size_t recorded = 0;
    for (std::size_t t = 0; t < kTriples; ++t) {
        const BcsAlgo a = pick();
        BcsAlgo b = pick();
        while (b == a)
            b = pick();
        BcsAlgo c = pick();
        while (c == a || c == b)
            c = pick();
        BcsAlgo d = pick();
        while (d == b || d == c)
            d = pick();
        const std::size_t c1 = 1 + next() % (len / 2 - 1);        // C at kSwitch + c1
        const std::size_t d1 = c1 + 1 + next() % (len - c1 - 2);  // D at kSwitch + d1, c1 < d1 < len - 1
        REQUIRE(c1 < d1);
        REQUIRE(d1 < len);
        printed << "  " << t << ": " << t17AlgoName(a) << " -> " << t17AlgoName(b) << ", C " << t17AlgoName(c)
                << " at +" << c1 << ", D " << t17AlgoName(d) << " at +" << d1 << "\n";
        INFO("triple " << t << ": " << t17AlgoName(a) << " -> " << t17AlgoName(b) << ", C " << t17AlgoName(c)
                       << " at +" << c1 << ", D " << t17AlgoName(d) << " at +" << d1 << " (crossfade " << len
                       << ")");

        // (a)(b) C then D == "A -> B, then B -> D at the crossfade end"; <= 2 running at every sample.
        {
            BassCharacterStage s;
            t17Prepare(s, a, kQ, f0);
            BcsProbe::resetMaxRunning(s);
            std::size_t maxRun = 0;
            const T17Stereo y =
                t17RenderRequests(s, inL, inR, 1, {{kSwitch, b}, {kSwitch + c1, c}, {kSwitch + d1, d}}, maxRun);
            BassCharacterStage r;
            t17Prepare(r, a, kQ, f0);
            std::size_t maxRef = 0;
            const T17Stereo yr = t17RenderRequests(r, inL, inR, 512, {{kSwitch, b}, {kSwitch + len, d}}, maxRef);
            CHECK(bcsSameBits(y.l, yr.l));
            CHECK(bcsSameBits(y.r, yr.r));
            CHECK(maxRun <= 2u);  // at every sample (sample-by-sample render)
            CHECK(maxRun == 2u);  // not vacuous
            CHECK(BcsProbe::maxRunningBranchCount(s) <= 2u);
            CHECK(BcsProbe::current(s) == d);
            CHECK_FALSE(BcsProbe::crossfading(s));
            ++recorded;
        }

        // (c) C then B (the request returns to the current target): the queue clears, ends at B.
        {
            BassCharacterStage s;
            t17Prepare(s, a, kQ, f0);
            std::size_t maxRun = 0;
            const std::vector<T17Request> req{{kSwitch, b}, {kSwitch + c1, c}, {kSwitch + d1, b}};
            const std::vector<std::size_t> at{kSwitch, kSwitch + c1, kSwitch + d1};
            const T17Stereo y = t17RenderEvents(
                s, inL, inR, 1, at,
                [&](std::size_t i) {
                    s.setAlgorithm(req[i].algo);
                    if (i == 2) {
                        CHECK_FALSE(BcsProbe::hasPending(s));  // C-21
                        CHECK(BcsProbe::crossfading(s));
                        CHECK(BcsProbe::target(s) == b);
                    }
                },
                [&](const BassCharacterStage& st) {
                    maxRun = std::max(maxRun, BcsProbe::runningBranchCount(st));
                });
            BassCharacterStage r;
            t17Prepare(r, a, kQ, f0);
            std::size_t maxRef = 0;
            const T17Stereo yr = t17RenderRequests(r, inL, inR, 512, {{kSwitch, b}}, maxRef);
            CHECK(bcsSameBits(y.l, yr.l));
            CHECK(bcsSameBits(y.r, yr.r));
            CHECK(maxRun <= 2u);
            CHECK(BcsProbe::current(s) == b);
            CHECK_FALSE(BcsProbe::crossfading(s));
            // A request equal to the then-current target, while steady, starts no crossfade.
            s.setAlgorithm(b);
            CHECK_FALSE(BcsProbe::crossfading(s));
        }

        // (c) C then A (the request returns to the outgoing algorithm): B -> A at the crossfade end.
        {
            BassCharacterStage s;
            t17Prepare(s, a, kQ, f0);
            std::size_t maxRun = 0;
            const T17Stereo y =
                t17RenderRequests(s, inL, inR, 1, {{kSwitch, b}, {kSwitch + c1, c}, {kSwitch + d1, a}}, maxRun);
            BassCharacterStage r;
            t17Prepare(r, a, kQ, f0);
            std::size_t maxRef = 0;
            const T17Stereo yr = t17RenderRequests(r, inL, inR, 512, {{kSwitch, b}, {kSwitch + len, a}}, maxRef);
            CHECK(bcsSameBits(y.l, yr.l));
            CHECK(bcsSameBits(y.r, yr.r));
            CHECK(maxRun <= 2u);
            CHECK(BcsProbe::current(s) == a);
            CHECK_FALSE(BcsProbe::crossfading(s));
        }
    }
    WARN("SC-029 sampled ordered triples (xorshift32 seed 0x13579BDF), " << recorded << " recorded:\n"
                                                                         << printed.str());
    CHECK(recorded >= 30u);
}
