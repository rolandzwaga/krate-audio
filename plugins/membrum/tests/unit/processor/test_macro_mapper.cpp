// ==============================================================================
// Macro offset unit tests (Phase 6 T020, rewritten for fix plan T1.22)
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-022, FR-023, SC-003, SC-006)
// ==============================================================================
// The five macros are offsets on a pad's stored base fields
// (dsp/macro_offsets.h): applyMacroOffsets(base) is what the engine plays, the
// stored config is never modified, and baseFromEffective is its inverse.
// ==============================================================================

#include "dsp/macro_offsets.h"
#include "dsp/pad_config.h"
#include <allocation_detector.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdint>
#include <random>

// Global operator new/delete overrides required by AllocationDetector live in
// plugins/membrum/tests/unit/test_allocation_matrix.cpp. See the comment there.

using Catch::Matchers::WithinAbs;
using namespace Membrum;
using namespace Membrum::MacroCurves;

TEST_CASE("Macro offsets: macros at 0.5 leave every field bit-identical", "[macro_mapper]")
{
    PadConfig cfg{};
    cfg.material = 0.123f;
    cfg.decay = 1.7f;  // even an out-of-range stored value is played as stored
    cfg.tsPitchEnvTime = 0.25f;
    const PadConfig eff = applyMacroOffsets(cfg);
    CHECK(eff == cfg);
}

TEST_CASE("Macro offsets: Tightness=0.0 drives material down", "[macro_mapper]")
{
    PadConfig cfg{};
    cfg.macroTightness = 0.0f;
    const PadConfig eff = applyMacroOffsets(cfg);

    // material default 0.5 + (0.0-0.5)*0.3 = 0.5 - 0.15 = 0.35
    REQUIRE_THAT(eff.material, WithinAbs(0.5f - kTightnessMaterialSpan * 0.5f, 1e-6f));
    REQUIRE(cfg.material == 0.5f);  // the stored base is untouched
}

TEST_CASE("Macro offsets: Tightness=1.0 drives material up", "[macro_mapper]")
{
    PadConfig cfg{};
    cfg.macroTightness = 1.0f;
    const PadConfig eff = applyMacroOffsets(cfg);

    REQUIRE_THAT(eff.material, WithinAbs(0.5f + kTightnessMaterialSpan * 0.5f, 1e-6f));
}

TEST_CASE("Macro offsets: Brightness affects tsFilterCutoff and modeInject", "[macro_mapper]")
{
    PadConfig cfg{};
    cfg.macroBrightness = 0.0f;  // darker
    const PadConfig eff = applyMacroOffsets(cfg);
    // cutoff base 1.0 + (0.0-0.5)*0.4 = 0.8
    REQUIRE_THAT(eff.tsFilterCutoff, WithinAbs(1.0f - kBrightnessCutoffSpan * 0.5f, 1e-6f));
    // modeInject base 0.0 + (0.0-0.5)*0.3 = -0.15 -> clamped to 0
    REQUIRE_THAT(eff.modeInjectAmount, WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("Macro offsets: Punch scales an existing pitch envelope", "[macro_mapper][pitch]")
{
    PadConfig cfg{};
    cfg.tsPitchEnvStart = 0.2f;
    cfg.tsPitchEnvTime = 0.2f;
    cfg.macroPunch = 1.0f;
    const PadConfig eff = applyMacroOffsets(cfg);

    // tsPitchEnvStart: exp-delta at macro=1.0 = (exp2(1) - 1) * 0.5 = 0.5
    REQUIRE_THAT(eff.tsPitchEnvStart, WithinAbs(0.2f + kPunchPitchEnvDepthSpan, 1e-6f));
    // tsPitchEnvTime: Punch 1 halves the time (a faster drop).
    REQUIRE_THAT(eff.tsPitchEnvTime, WithinAbs(0.1f, 1e-6f));

    cfg.macroPunch = 0.0f;
    REQUIRE_THAT(applyMacroOffsets(cfg).tsPitchEnvTime, WithinAbs(0.4f, 1e-6f));
}

TEST_CASE("Macro offsets: Punch never switches a pitch envelope on", "[macro_mapper][pitch]")
{
    // MB-081: the default pad has no pitch envelope (start = end = time = 0).
    for (const float punch : {0.0f, 0.25f, 0.45f, 0.49f, 0.51f, 1.0f})
    {
        CAPTURE(punch);
        PadConfig cfg{};
        cfg.macroPunch = punch;
        const PadConfig eff = applyMacroOffsets(cfg);
        CHECK(eff.tsPitchEnvTime == 0.0f);
        CHECK(eff.tsPitchEnvStart == 0.0f);
        // Its exciter-attack target still moves.
        CHECK_THAT(eff.noiseBurstDuration,
                   WithinAbs(0.5f - (punch - 0.5f) * kPunchExciterAttackSpan, 1e-6f));
    }
}

TEST_CASE("Macro offsets: Complexity drives modeInjectAmount (proxy for mode count)",
          "[macro_mapper]")
{
    PadConfig cfg{};
    cfg.macroComplexity = 1.0f;
    const PadConfig eff = applyMacroOffsets(cfg);

    // modeInject default 0 + (1-0.5)*0.3 = 0.15
    REQUIRE(eff.modeInjectAmount > 0.1f);
    REQUIRE(eff.couplingAmount > 0.5f);
    REQUIRE(eff.nonlinearCoupling > 0.0f);
}

TEST_CASE("Macro offsets: Tightness and Body Size both offset decay", "[macro_mapper]")
{
    PadConfig cfg{};
    cfg.decay = 0.5f;
    cfg.macroTightness = 1.0f;
    cfg.macroBodySize = 1.0f;
    const float want = 0.5f - expDelta(1.0f, kTightnessDecaySpan) +
                       linDelta(1.0f, kBodySizeDecayScaleSpan);
    REQUIRE_THAT(applyMacroOffsets(cfg).decay, WithinAbs(want, 1e-6f));
}

TEST_CASE("Macro offsets: applyMacroOffsets is a pure function of the stored config",
          "[macro_mapper]")
{
    PadConfig cfg{};
    cfg.macroTightness = 0.75f;
    cfg.macroBrightness = 0.3f;
    const PadConfig before = cfg;
    const PadConfig first = applyMacroOffsets(cfg);
    for (int i = 0; i < 100; ++i)
        REQUIRE(applyMacroOffsets(cfg) == first);
    REQUIRE(cfg == before);
}

TEST_CASE("Macro offsets: output clamps to [0, 1] and the base keeps its value",
          "[macro_mapper]")
{
    PadConfig cfg{};
    cfg.material = 0.9f;
    cfg.macroTightness = 1.0f;
    const PadConfig eff = applyMacroOffsets(cfg);

    REQUIRE(eff.material == 1.0f);
    REQUIRE(cfg.material == 0.9f);
    // Back at neutral the pad plays its base again (MB-173).
    cfg.macroTightness = 0.5f;
    REQUIRE(applyMacroOffsets(cfg).material == 0.9f);
}

TEST_CASE("Macro offsets: applyMacroOffsets inverts baseFromEffective",
          "[macro_mapper][macro_offsets]")
{
    std::mt19937 rng(0x7122u);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    int checked = 0;
    int clampedConfigs = 0;
    for (int n = 0; n < 4000; ++n)
    {
        PadConfig x{};
        x.material = unit(rng);
        x.size = unit(rng);
        x.decay = unit(rng);
        x.decaySkew = unit(rng);
        x.tsFilterCutoff = unit(rng);
        x.modeInjectAmount = unit(rng);
        x.modeStretch = unit(rng);
        x.tsPitchEnvStart = unit(rng);
        x.tsPitchEnvTime = (n % 3 == 0) ? 0.0f : unit(rng);
        x.noiseBurstDuration = unit(rng);
        x.couplingAmount = unit(rng);
        x.nonlinearCoupling = unit(rng);
        x.macroTightness = unit(rng);
        x.macroBrightness = unit(rng);
        x.macroBodySize = unit(rng);
        x.macroPunch = unit(rng);
        x.macroComplexity = unit(rng);

        int clamped = -1;
        const PadConfig base = baseFromEffective(x, &clamped);
        REQUIRE(clamped >= 0);
        if (clamped > 0)
        {
            ++clampedConfigs;
            continue;
        }
        CAPTURE(n);
        const PadConfig y = applyMacroOffsets(base);
        CHECK_THAT(y.material, WithinAbs(x.material, 1e-6f));
        CHECK_THAT(y.size, WithinAbs(x.size, 1e-6f));
        CHECK_THAT(y.decay, WithinAbs(x.decay, 1e-6f));
        CHECK_THAT(y.decaySkew, WithinAbs(x.decaySkew, 1e-6f));
        CHECK_THAT(y.tsFilterCutoff, WithinAbs(x.tsFilterCutoff, 1e-6f));
        CHECK_THAT(y.modeInjectAmount, WithinAbs(x.modeInjectAmount, 1e-6f));
        CHECK_THAT(y.modeStretch, WithinAbs(x.modeStretch, 1e-6f));
        CHECK_THAT(y.tsPitchEnvStart, WithinAbs(x.tsPitchEnvStart, 1e-6f));
        CHECK_THAT(y.tsPitchEnvTime, WithinAbs(x.tsPitchEnvTime, 1e-6f));
        CHECK_THAT(y.noiseBurstDuration, WithinAbs(x.noiseBurstDuration, 1e-6f));
        CHECK_THAT(y.couplingAmount, WithinAbs(x.couplingAmount, 1e-6f));
        CHECK_THAT(y.nonlinearCoupling, WithinAbs(x.nonlinearCoupling, 1e-6f));
        // An envelope stays on or off.
        CHECK((y.tsPitchEnvTime > 0.0f) == (x.tsPitchEnvTime > 0.0f));
        ++checked;
    }
    INFO(checked << " configs checked, " << clampedConfigs << " skipped (a base clamped)");
    CHECK(checked >= 100);
}

// ==============================================================================
// T020 / SC-008 -- applyMacroOffsets() makes zero heap allocations.
// ==============================================================================
TEST_CASE("Macro offsets: applyMacroOffsets() makes zero allocations in 1000 iterations (SC-008)",
          "[macro_mapper][realtime]")
{
    PadConfig cfg{};
    cfg.tsPitchEnvTime = 0.2f;
    float sink = 0.0f;

    auto& detector = TestHelpers::AllocationDetector::instance();
    detector.startTracking();

    for (int i = 0; i < 1000; ++i)
    {
        cfg.macroTightness  = (i & 1) ? 0.4f : 0.6f;
        cfg.macroBrightness = (i & 2) ? 0.3f : 0.7f;
        cfg.macroBodySize   = (i & 4) ? 0.45f : 0.55f;
        cfg.macroPunch      = (i & 8) ? 0.35f : 0.65f;
        cfg.macroComplexity = (i & 16) ? 0.2f : 0.8f;
        sink += applyMacroOffsets(cfg).material;
    }

    const std::size_t allocations = detector.stopTracking();
    REQUIRE(allocations == 0);
    REQUIRE(sink > 0.0f);
}
