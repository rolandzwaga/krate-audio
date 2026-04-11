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

    CHECK(controller.getParameterCount() ==
          kPhase2ParameterCount + kPhase3NewParameters);

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

TEST_CASE("Phase 3 params: kChokeGroupId is a stepped [0, 8] RangeParameter",
          "[membrum][vst][phase3_1][params]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    auto* param = controller.getParameterObject(Membrum::kChokeGroupId);
    REQUIRE(param != nullptr);

    const auto& info = param->getInfo();
    CHECK(info.stepCount == 8);
    CHECK(info.defaultNormalizedValue == Approx(0.0).margin(1e-12));

    CHECK(controller.setParamNormalized(Membrum::kChokeGroupId, 0.0) == kResultTrue);
    CHECK(param->toPlain(controller.getParamNormalized(Membrum::kChokeGroupId))
          == Approx(0.0).margin(1e-9));

    CHECK(controller.setParamNormalized(Membrum::kChokeGroupId, 1.0) == kResultTrue);
    CHECK(param->toPlain(controller.getParamNormalized(Membrum::kChokeGroupId))
          == Approx(8.0).margin(1e-9));

    REQUIRE(controller.terminate() == kResultOk);
}
