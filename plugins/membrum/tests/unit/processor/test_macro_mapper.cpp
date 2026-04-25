// ==============================================================================
// MacroMapper unit tests (Phase 6, T020)
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-022, FR-023, SC-003, SC-006)
// Contract: specs/141-membrum-phase6-ui/contracts/macro_mapper.h
// Data model: specs/141-membrum-phase6-ui/data-model.md section 3
// ==============================================================================

#include "processor/macro_mapper.h"
#include "dsp/pad_config.h"
#include <allocation_detector.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// Global operator new/delete overrides required by AllocationDetector live in
// plugins/membrum/tests/unit/test_allocation_matrix.cpp. See the comment there.

using Catch::Matchers::WithinAbs;
using namespace Membrum;
using namespace Membrum::MacroCurves;

namespace {

RegisteredDefaultsTable makeNeutralDefaults()
{
    RegisteredDefaultsTable t{};
    // Seed values mirror PadConfig defaults and the registered-defaults table
    // the Controller will build in T025.
    t.byOffset[kPadMaterial]           = 0.5f;
    t.byOffset[kPadSize]               = 0.5f;
    t.byOffset[kPadDecay]              = 0.3f;
    t.byOffset[kPadDecaySkew]          = 0.5f;
    t.byOffset[kPadModeInjectAmount]   = 0.0f;
    t.byOffset[kPadModeStretch]        = 0.333333f;
    t.byOffset[kPadNonlinearCoupling]  = 0.0f;
    t.byOffset[kPadCouplingAmount]     = 0.5f;
    t.byOffset[kPadTSFilterCutoff]     = 1.0f;
    t.byOffset[kPadTSPitchEnvStart]    = 0.0f;
    t.byOffset[kPadTSPitchEnvEnd]      = 0.0f;
    t.byOffset[kPadTSPitchEnvTime]     = 0.0f;
    t.byOffset[kPadNoiseBurstDuration] = 0.5f;
    return t;
}

} // namespace

TEST_CASE("MacroMapper::prepare() caches defaults and resets cache", "[macro_mapper]")
{
    MacroMapper m;
    REQUIRE_FALSE(m.isPrepared());
    m.prepare(makeNeutralDefaults());
    REQUIRE(m.isPrepared());
}

TEST_CASE("MacroMapper: macro=0.5 produces zero delta against defaults", "[macro_mapper]")
{
    MacroMapper m;
    m.prepare(makeNeutralDefaults());

    PadConfig cfg{};
    // All five macros already 0.5 by construction; force dirty with sentinel
    // by invalidating cache (prepare already does that).
    m.apply(0, cfg);

    REQUIRE_THAT(cfg.material,         WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(cfg.decay,            WithinAbs(0.3f, 1e-6f));
    REQUIRE_THAT(cfg.decaySkew,        WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(cfg.size,             WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(cfg.modeStretch,      WithinAbs(0.333333f, 1e-5f));
    REQUIRE_THAT(cfg.tsFilterCutoff,   WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(cfg.tsPitchEnvStart,  WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(cfg.tsPitchEnvTime,   WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(cfg.noiseBurstDuration, WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(cfg.couplingAmount,   WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(cfg.nonlinearCoupling, WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(cfg.modeInjectAmount, WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("MacroMapper: Tightness=0.0 drives material down", "[macro_mapper]")
{
    MacroMapper m;
    m.prepare(makeNeutralDefaults());

    PadConfig cfg{};
    cfg.macroTightness = 0.0f;
    m.apply(0, cfg);

    // material default 0.5 + (0.0-0.5)*0.3 = 0.5 - 0.15 = 0.35
    REQUIRE_THAT(cfg.material,
                 WithinAbs(0.5f - kTightnessMaterialSpan * 0.5f, 1e-6f));
}

TEST_CASE("MacroMapper: Tightness=1.0 drives material up", "[macro_mapper]")
{
    MacroMapper m;
    m.prepare(makeNeutralDefaults());

    PadConfig cfg{};
    cfg.macroTightness = 1.0f;
    m.apply(0, cfg);

    REQUIRE_THAT(cfg.material,
                 WithinAbs(0.5f + kTightnessMaterialSpan * 0.5f, 1e-6f));
}

TEST_CASE("MacroMapper: Brightness affects tsFilterCutoff and modeInject", "[macro_mapper]")
{
    MacroMapper m;
    m.prepare(makeNeutralDefaults());

    PadConfig cfg{};
    cfg.macroBrightness = 0.0f;  // darker
    m.apply(0, cfg);
    // cutoff base 1.0 + (0.0-0.5)*0.4 = 0.8
    REQUIRE_THAT(cfg.tsFilterCutoff,
                 WithinAbs(1.0f - kBrightnessCutoffSpan * 0.5f, 1e-6f));
    // modeInject base 0.0 + (0.0-0.5)*0.3 = -0.15 -> clamped to 0
    REQUIRE_THAT(cfg.modeInjectAmount, WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("MacroMapper: Punch drives tsPitchEnvStart/Time", "[macro_mapper]")
{
    MacroMapper m;
    m.prepare(makeNeutralDefaults());

    PadConfig cfg{};
    cfg.macroPunch = 1.0f;
    m.apply(0, cfg);

    // tsPitchEnvStart: exp-delta at macro=1.0 = (exp2(1) - 1) * 0.5 = 0.5
    // + default 0.0 = 0.5
    REQUIRE(cfg.tsPitchEnvStart > 0.2f);
    // tsPitchEnvTime: default 0.0 - linDelta(1.0, 0.4) = -0.2 clamped to 0
    REQUIRE_THAT(cfg.tsPitchEnvTime, WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("MacroMapper: Complexity drives modeInjectAmount (proxy for mode count)",
          "[macro_mapper]")
{
    MacroMapper m;
    m.prepare(makeNeutralDefaults());

    PadConfig cfg{};
    cfg.macroComplexity = 1.0f;
    m.apply(0, cfg);

    // modeInject default 0 + (1-0.5)*0.3 = 0.15
    REQUIRE(cfg.modeInjectAmount > 0.1f);
    REQUIRE(cfg.couplingAmount > 0.5f);
    REQUIRE(cfg.nonlinearCoupling > 0.0f);
}

TEST_CASE("MacroMapper::isDirty() reflects cache state", "[macro_mapper]")
{
    MacroMapper m;
    m.prepare(makeNeutralDefaults());

    // Phase 8G: cache initialises to 0.5 (neutral) so a fresh PadConfig --
    // whose macroX fields also default to 0.5 -- is NOT dirty. This is the
    // fix for the 808-toms preset-load bug: arriving macro values that
    // match neutral leave per-pad cfg fields untouched, instead of the
    // pre-fix sentinel-driven first-apply that overwrote cfg from
    // `defaults_ + delta` (collapsing per-pad preset sizes to 0.5).
    PadConfig cfg{};
    REQUIRE_FALSE(m.isDirty(0, cfg));

    cfg.macroTightness = 0.75f;
    REQUIRE(m.isDirty(0, cfg));

    m.apply(0, cfg);
    REQUIRE_FALSE(m.isDirty(0, cfg));

    cfg.macroTightness = 0.30f;
    REQUIRE(m.isDirty(0, cfg));
}

TEST_CASE("MacroMapper::reapplyAll() refreshes every pad", "[macro_mapper]")
{
    MacroMapper m;
    m.prepare(makeNeutralDefaults());

    std::array<PadConfig, kNumPads> pads{};
    for (auto& p : pads)
        p.macroTightness = 0.0f;

    m.reapplyAll(pads);

    const float expectedMaterial = 0.5f - kTightnessMaterialSpan * 0.5f;
    for (const auto& p : pads)
        REQUIRE_THAT(p.material, WithinAbs(expectedMaterial, 1e-6f));
}

TEST_CASE("MacroMapper: output clamps to [0, 1]", "[macro_mapper]")
{
    MacroMapper m;

    // Pick defaults near the edges so large macros would overshoot.
    RegisteredDefaultsTable t = makeNeutralDefaults();
    t.byOffset[kPadMaterial] = 0.9f;
    m.prepare(t);

    PadConfig cfg{};
    cfg.macroTightness = 1.0f;
    m.apply(0, cfg);

    REQUIRE(cfg.material >= 0.0f);
    REQUIRE(cfg.material <= 1.0f);
}

TEST_CASE("MacroMapper::apply() early-outs when macros unchanged (no re-write)",
          "[macro_mapper]")
{
    MacroMapper m;
    m.prepare(makeNeutralDefaults());

    PadConfig cfg{};
    cfg.macroTightness = 1.0f;
    m.apply(0, cfg);
    const float firstMaterial = cfg.material;

    // Stomp the computed field then re-apply with unchanged macros; because
    // the cache matches, apply() must NOT touch cfg.
    cfg.material = 0.12345f;
    m.apply(0, cfg);
    REQUIRE_THAT(cfg.material, WithinAbs(0.12345f, 1e-7f));

    // Phase 8G: invalidate forces "next apply re-layers macro deltas onto
    // the *current* cfg" (the new incremental contract). The first apply
    // started from cfg.material = 0.5 and ended at firstMaterial; after
    // stomping cfg.material to 0.12345 and invalidating, the next apply
    // re-layers the same delta (macroTightness=1.0 vs cached 0.5 yields
    // +linDelta(1.0, span)) on top of 0.12345 -- not back to firstMaterial.
    // The invariant we now check is "delta layered correctly", i.e. the
    // new value equals 0.12345 + (firstMaterial - 0.5) within rounding.
    m.invalidateCache();
    m.apply(0, cfg);
    const float expectedAfterInvalidate = 0.12345f + (firstMaterial - 0.5f);
    REQUIRE_THAT(cfg.material, WithinAbs(expectedAfterInvalidate, 1e-6f));
}

TEST_CASE("MacroMapper without prepare() is a no-op", "[macro_mapper]")
{
    MacroMapper m;
    PadConfig cfg{};
    cfg.material = 0.25f;
    m.apply(0, cfg);
    REQUIRE_THAT(cfg.material, WithinAbs(0.25f, 1e-7f));
}

// ==============================================================================
// T020 / SC-008 -- MacroMapper::apply() makes zero heap allocations.
// Uses TestHelpers::AllocationDetector with the global operator new overrides
// at the top of this TU to count allocations across 1000 apply() calls.
// ==============================================================================
TEST_CASE("MacroMapper::apply() makes zero allocations in 1000 iterations (SC-008)",
          "[macro_mapper][realtime]")
{
    MacroMapper m;
    m.prepare(makeNeutralDefaults());

    PadConfig cfg{};
    // Warm-up: run a few iterations so any lazy state settles.
    for (int i = 0; i < 8; ++i)
    {
        cfg.macroTightness  = 0.25f + 0.01f * static_cast<float>(i);
        cfg.macroBrightness = 0.75f - 0.01f * static_cast<float>(i);
        m.apply(0, cfg);
    }

    auto& detector = TestHelpers::AllocationDetector::instance();
    detector.startTracking();

    for (int i = 0; i < 1000; ++i)
    {
        // Perturb macros each iteration so apply() does not early-out.
        cfg.macroTightness  = (i & 1) ? 0.4f : 0.6f;
        cfg.macroBrightness = (i & 2) ? 0.3f : 0.7f;
        cfg.macroBodySize   = (i & 4) ? 0.45f : 0.55f;
        cfg.macroPunch      = (i & 8) ? 0.35f : 0.65f;
        cfg.macroComplexity = (i & 16) ? 0.2f : 0.8f;
        m.apply(0, cfg);
    }

    const std::size_t allocations = detector.stopTracking();
    REQUIRE(allocations == 0);
}
