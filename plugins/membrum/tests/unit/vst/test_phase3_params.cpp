// ==============================================================================
// Phase 3.1 -- Controller parameter registration for kMaxPolyphony /
// kVoiceStealing / kChokeGroup
// ==============================================================================
// T3.1.5 -- satisfies FR-150, FR-151, SC-030.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "plugin_ids.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

constexpr int kPhase2ParameterCount = 34;
constexpr int kPhase3NewParameters  = 3;

} // namespace

TEST_CASE("Phase 3 params: controller exposes Phase 2 count + 3",
          "[membrum][vst][phase3_1][params]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Phase 4 adds 1 (kSelectedPadId) + 1152 (32 pads x 36 params) = 1153 more
    // Phase 5 adds 4 global coupling params (kGlobalCoupling..kCouplingDelay)
    // Phase 6 (US4) adds 32 per-pad coupling amount params (offset 36)
    // Phase 6 (US1, spec 141) adds 1 session-scoped global (kUiModeId)
    //   + 160 per-pad macros (32 pads x 5 macros)
    constexpr int kPhase5NewParameters = 4;
    constexpr int kPhase6US4Parameters = 32;
    constexpr int kPhase6US1Globals    = 1;
    constexpr int kPhase6US1MacroParams = 32 * 5;
    // Phase 8 (US7 / spec 141, T074) adds 1 global Output Bus selector
    // proxy (kOutputBusId).
    constexpr int kPhase6US7Globals    = 1;
    // Phase 7 adds 8 global noise/click proxies (kNoiseLayer*, kClickLayer*)
    // + 8 new per-pad offsets (42..49) across 32 pads.
    constexpr int kPhase7Globals       = 8;
    constexpr int kPhase7PerPadParams  = 32 * 8;
    // Phase 8A adds 2 global damping proxies (kBodyDampingB1/B3)
    // + 2 new per-pad offsets (50..51) across 32 pads.
    constexpr int kPhase8AGlobals       = 2;
    constexpr int kPhase8APerPadParams  = 32 * 2;
    // Phase 8C adds 2 global proxies (kAirLoadingId/kModeScatterId)
    // + 2 new per-pad offsets (52..53) across 32 pads.
    constexpr int kPhase8CGlobals       = 2;
    constexpr int kPhase8CPerPadParams  = 32 * 2;
    // Phase 8D adds 4 global proxies (coupling + 3 secondary) + 32*4 per-pad.
    constexpr int kPhase8DGlobals       = 4;
    constexpr int kPhase8DPerPadParams  = 32 * 4;
    // Phase 8E adds 1 tension-modulation proxy + 32*1 per-pad.
    constexpr int kPhase8EGlobals       = 1;
    constexpr int kPhase8EPerPadParams  = 32 * 1;
    constexpr int kPhase8FGlobals       = 1;
    constexpr int kPhase8FPerPadParams  = 32 * 1;
    // Phase 9 adds 1 master-gain global (true global; no per-pad tail).
    constexpr int kPhase9Globals        = 1;
    // Phase 10 adds 4 global pitch-env proxies (knee/midPitch/midFraction/curve2)
    // + 32 * 4 per-pad.
    constexpr int kPhase10Globals       = 4;
    constexpr int kPhase10PerPadParams  = 32 * 4;
    // M-9 adds 1 global pan proxy (kPadPanId) + 32*1 per-pad pan.
    constexpr int kM9Globals            = 1;
    constexpr int kM9PerPadParams       = 32 * 1;

    constexpr int kWireGlobals          = 1;
    constexpr int kWirePerPadParams     = 32 * 1;
    CHECK(controller.getParameterCount() ==
          kPhase2ParameterCount + kPhase3NewParameters + 1 + 32 * 36
          + kPhase5NewParameters + kPhase6US4Parameters
          + kPhase6US1Globals + kPhase6US1MacroParams
          + kPhase6US7Globals
          + kPhase7Globals + kPhase7PerPadParams
          + kPhase8AGlobals + kPhase8APerPadParams
          + kPhase8CGlobals + kPhase8CPerPadParams
          + kPhase8DGlobals + kPhase8DPerPadParams
          + kPhase8EGlobals + kPhase8EPerPadParams
          + kPhase8FGlobals + kPhase8FPerPadParams
          + kPhase9Globals
          + kPhase10Globals + kPhase10PerPadParams
          + kM9Globals + kM9PerPadParams
          + kWireGlobals + kWirePerPadParams);

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Phase 3 params: kMaxPolyphonyId is a stepped [4, 16] RangeParameter",
          "[membrum][vst][phase3_1][params]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    auto* param = controller.getParameterObject(Membrum::kMaxPolyphonyId);
    REQUIRE(param != nullptr);

    const auto& info = param->getInfo();
    CHECK(info.stepCount == 12);  // 13 integer values -> 12 steps
    CHECK(info.defaultNormalizedValue == Approx(
        (8.0 - 4.0) / (16.0 - 4.0)).margin(1e-12));

    // Round-trip the extremes via setParamNormalized.
    CHECK(controller.setParamNormalized(Membrum::kMaxPolyphonyId, 0.0) == kResultTrue);
    CHECK(param->toPlain(controller.getParamNormalized(Membrum::kMaxPolyphonyId))
          == Approx(4.0).margin(1e-9));

    CHECK(controller.setParamNormalized(Membrum::kMaxPolyphonyId, 1.0) == kResultTrue);
    CHECK(param->toPlain(controller.getParamNormalized(Membrum::kMaxPolyphonyId))
          == Approx(16.0).margin(1e-9));

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Phase 3 params: kVoiceStealingId is a 3-entry StringListParameter",
          "[membrum][vst][phase3_1][params]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    auto* param = controller.getParameterObject(Membrum::kVoiceStealingId);
    REQUIRE(param != nullptr);

    const auto& info = param->getInfo();
    // StringListParameter sets stepCount = (count - 1).
    CHECK(info.stepCount == 2);
    CHECK((info.flags & ParameterInfo::kIsList) != 0);
    CHECK(info.defaultNormalizedValue == Approx(0.0).margin(1e-12));

    // Each of the 3 list entries must map to a distinct integer 0..2.
    for (int idx = 0; idx < 3; ++idx)
    {
        const double norm = static_cast<double>(idx) / 2.0;
        const int plain = static_cast<int>(param->toPlain(norm));
        INFO("Voice stealing idx=" << idx << " plain=" << plain);
        CHECK(plain == idx);
    }

    REQUIRE(controller.terminate() == kResultOk);
}

// Audit finding 17: kChokeGroupId is now a 9-entry StringListParameter
// ("None"/"CG1".."CG8") so the bound COptionMenu shows friendly labels. The
// SDK ToNormalized/FromNormalized mapping is bit-identical to the previous
// RangeParameter(0..8, stepCount=8): stepCount stays 8, default stays 0.0, and
// norm 0.0/1.0 still map to plain index 0/8.
TEST_CASE("Phase 3 params: kChokeGroupId is a 9-entry StringListParameter",
          "[membrum][vst][phase3_1][params]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    auto* param = controller.getParameterObject(Membrum::kChokeGroupId);
    REQUIRE(param != nullptr);

    const auto& info = param->getInfo();
    CHECK((info.flags & ParameterInfo::kIsList) != 0);
    CHECK(info.stepCount == 8);  // 9 entries -> 8 steps
    CHECK(info.defaultNormalizedValue == Approx(0.0).margin(1e-12));

    // Mapping unchanged: each of the 9 entries maps to a distinct integer 0..8.
    for (int idx = 0; idx <= 8; ++idx)
    {
        const double norm = static_cast<double>(idx) / 8.0;
        const int plain = static_cast<int>(param->toPlain(norm));
        INFO("Choke group idx=" << idx << " plain=" << plain);
        CHECK(plain == idx);
    }

    REQUIRE(controller.terminate() == kResultOk);
}
