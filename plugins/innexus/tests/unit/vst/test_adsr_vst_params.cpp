// ==============================================================================
// ADSR VST3 Parameter Registration Tests (Spec 124: T026)
// ==============================================================================
// Tests that all 9 ADSR parameter IDs (720-728) are registered correctly in the
// Controller with proper ranges, defaults, and normalize/denormalize round-trip.
//
// Test-first: these tests should PASS since parameters were registered in Phase 2.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <cmath>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

// =============================================================================
// Test: All 9 ADSR parameter IDs (720-728) exist and are registered
// =============================================================================
TEST_CASE("ADSR VST: all 9 parameter IDs (720-728) are registered",
          "[adsr][vst][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    const Steinberg::Vst::ParamID adsrIds[] = {
        Innexus::kAdsrAttackId,
        Innexus::kAdsrDecayId,
        Innexus::kAdsrSustainId,
        Innexus::kAdsrReleaseId,
        Innexus::kAdsrAmountId,
        Innexus::kAdsrTimeScaleId,
        Innexus::kAdsrAttackCurveId,
        Innexus::kAdsrDecayCurveId,
        Innexus::kAdsrReleaseCurveId
    };

    for (auto id : adsrIds)
    {
        ParameterInfo info{};
        auto result = controller.getParameterInfoByTag(id, info);
        REQUIRE(result == kResultOk);
        REQUIRE(info.id == id);
        REQUIRE((info.flags & ParameterInfo::kCanAutomate) != 0);
    }

    REQUIRE(controller.terminate() == kResultOk);
}

// =============================================================================
// Test: Normalized range is [0.0, 1.0] for all parameters
// =============================================================================
TEST_CASE("ADSR VST: all parameters have normalized range [0.0, 1.0]",
          "[adsr][vst][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    const Steinberg::Vst::ParamID adsrIds[] = {
        Innexus::kAdsrAttackId,
        Innexus::kAdsrDecayId,
        Innexus::kAdsrSustainId,
        Innexus::kAdsrReleaseId,
        Innexus::kAdsrAmountId,
        Innexus::kAdsrTimeScaleId,
        Innexus::kAdsrAttackCurveId,
        Innexus::kAdsrDecayCurveId,
        Innexus::kAdsrReleaseCurveId
    };

    for (auto id : adsrIds)
    {
        ParameterInfo info{};
        REQUIRE(controller.getParameterInfoByTag(id, info) == kResultOk);

        // Default normalized value must be in [0.0, 1.0]
        REQUIRE(info.defaultNormalizedValue >= 0.0);
        REQUIRE(info.defaultNormalizedValue <= 1.0);
    }

    REQUIRE(controller.terminate() == kResultOk);
}

// =============================================================================
// Test: Normalize/denormalize round-trip for logarithmic time parameters
// =============================================================================
TEST_CASE("ADSR VST: log time parameter normalize/denormalize round-trip",
          "[adsr][vst][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Test Attack, Decay, Release (all log-mapped 1-5000ms via RangeParameter)
    const Steinberg::Vst::ParamID timeIds[] = {
        Innexus::kAdsrAttackId,
        Innexus::kAdsrDecayId,
        Innexus::kAdsrReleaseId
    };

    for (auto id : timeIds)
    {
        // Verify min/max plain values
        auto plainMin = controller.normalizedParamToPlain(id, 0.0);
        auto plainMax = controller.normalizedParamToPlain(id, 1.0);
        REQUIRE(plainMin == Approx(1.0).margin(0.01));
        REQUIRE(plainMax == Approx(5000.0).margin(0.1));

        // Round-trip: pick a test value, normalize, denormalize, compare
        double testPlain = 250.0;
        double normalized = controller.plainParamToNormalized(id, testPlain);
        REQUIRE(normalized >= 0.0);
        REQUIRE(normalized <= 1.0);
        double roundTripped = controller.normalizedParamToPlain(id, normalized);
        REQUIRE(roundTripped == Approx(testPlain).margin(0.5));
    }

    REQUIRE(controller.terminate() == kResultOk);
}

// =============================================================================
// Test: kAdsrAmountId defaults to normalized value corresponding to 0.0 plain
// =============================================================================
TEST_CASE("ADSR VST: Envelope Amount defaults to 0.0",
          "[adsr][vst][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    ParameterInfo info{};
    REQUIRE(controller.getParameterInfoByTag(Innexus::kAdsrAmountId, info) == kResultOk);

    // Amount is a RangeParameter with range [0.0, 1.0] and default 0.0
    // Normalized default for 0.0 in range [0.0, 1.0] = 0.0
    auto plainDefault = controller.normalizedParamToPlain(
        Innexus::kAdsrAmountId, info.defaultNormalizedValue);
    REQUIRE(plainDefault == Approx(0.0).margin(0.001));

    REQUIRE(controller.terminate() == kResultOk);
}

// =============================================================================
// Test: Sustain parameter range is [0.0, 1.0]
// =============================================================================
TEST_CASE("ADSR VST: Sustain parameter range is [0.0, 1.0]",
          "[adsr][vst][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    auto plainMin = controller.normalizedParamToPlain(Innexus::kAdsrSustainId, 0.0);
    auto plainMax = controller.normalizedParamToPlain(Innexus::kAdsrSustainId, 1.0);
    REQUIRE(plainMin == Approx(0.0).margin(0.001));
    REQUIRE(plainMax == Approx(1.0).margin(0.001));

    // Default sustain is 1.0
    ParameterInfo info{};
    REQUIRE(controller.getParameterInfoByTag(Innexus::kAdsrSustainId, info) == kResultOk);
    auto plainDefault = controller.normalizedParamToPlain(
        Innexus::kAdsrSustainId, info.defaultNormalizedValue);
    REQUIRE(plainDefault == Approx(1.0).margin(0.001));

    REQUIRE(controller.terminate() == kResultOk);
}

// =============================================================================
// Test: Time Scale parameter range is [0.25, 4.0], default 1.0
// =============================================================================
TEST_CASE("ADSR VST: Time Scale parameter range and default",
          "[adsr][vst][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    auto plainMin = controller.normalizedParamToPlain(Innexus::kAdsrTimeScaleId, 0.0);
    auto plainMax = controller.normalizedParamToPlain(Innexus::kAdsrTimeScaleId, 1.0);
    REQUIRE(plainMin == Approx(0.25).margin(0.01));
    REQUIRE(plainMax == Approx(4.0).margin(0.01));

    ParameterInfo info{};
    REQUIRE(controller.getParameterInfoByTag(Innexus::kAdsrTimeScaleId, info) == kResultOk);
    auto plainDefault = controller.normalizedParamToPlain(
        Innexus::kAdsrTimeScaleId, info.defaultNormalizedValue);
    REQUIRE(plainDefault == Approx(1.0).margin(0.01));

    REQUIRE(controller.terminate() == kResultOk);
}

// =============================================================================
// Test: Curve amount parameters range is [-1.0, +1.0], default 0.0
// =============================================================================
TEST_CASE("ADSR VST: Curve amount parameter ranges and defaults",
          "[adsr][vst][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    const Steinberg::Vst::ParamID curveIds[] = {
        Innexus::kAdsrAttackCurveId,
        Innexus::kAdsrDecayCurveId,
        Innexus::kAdsrReleaseCurveId
    };

    for (auto id : curveIds)
    {
        auto plainMin = controller.normalizedParamToPlain(id, 0.0);
        auto plainMax = controller.normalizedParamToPlain(id, 1.0);
        REQUIRE(plainMin == Approx(-1.0).margin(0.01));
        REQUIRE(plainMax == Approx(1.0).margin(0.01));

        ParameterInfo info{};
        REQUIRE(controller.getParameterInfoByTag(id, info) == kResultOk);
        auto plainDefault = controller.normalizedParamToPlain(id, info.defaultNormalizedValue);
        REQUIRE(plainDefault == Approx(0.0).margin(0.01));
    }

    REQUIRE(controller.terminate() == kResultOk);
}
