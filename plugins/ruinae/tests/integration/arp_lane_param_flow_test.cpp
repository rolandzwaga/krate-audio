// ==============================================================================
// Integration Test: Arp Lane Parameter Flow (079-layout-framework)
// ==============================================================================
// Verifies velocity (and later gate) lane parameter round-trip:
//   Set a parameter via setParamNormalized(), read it back, verify value.
//
// This file is dedicated to arp lane parameter flow tests and will be
// extended in subsequent phases (US2-US6).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/vsttypes.h"

using Catch::Approx;

// ==============================================================================
// T023: Velocity Lane Parameter Round-Trip (SC-007)
// ==============================================================================

TEST_CASE("VelocityLane_ParameterRoundTrip_ValuePreserved", "[arp][integration][velocity]") {
    Ruinae::Controller controller;
    auto result = controller.initialize(nullptr);
    REQUIRE(result == Steinberg::kResultOk);

    SECTION("Set velocity step 0 to 0.75, read back matches within 1e-6") {
        auto paramId = static_cast<Steinberg::Vst::ParamID>(
            Ruinae::kArpVelocityLaneStep0Id);
        controller.setParamNormalized(paramId, 0.75);

        auto* param = controller.getParameterObject(paramId);
        REQUIRE(param != nullptr);

        double readBack = param->getNormalized();
        REQUIRE(readBack == Approx(0.75).margin(1e-6));
    }

    SECTION("Set velocity step 15 to 0.0, read back matches") {
        auto paramId = static_cast<Steinberg::Vst::ParamID>(
            Ruinae::kArpVelocityLaneStep0Id + 15);
        controller.setParamNormalized(paramId, 0.0);

        auto* param = controller.getParameterObject(paramId);
        REQUIRE(param != nullptr);

        double readBack = param->getNormalized();
        REQUIRE(readBack == Approx(0.0).margin(1e-6));
    }

    SECTION("Set velocity step 31 to 1.0, read back matches") {
        auto paramId = static_cast<Steinberg::Vst::ParamID>(
            Ruinae::kArpVelocityLaneStep0Id + 31);
        controller.setParamNormalized(paramId, 1.0);

        auto* param = controller.getParameterObject(paramId);
        REQUIRE(param != nullptr);

        double readBack = param->getNormalized();
        REQUIRE(readBack == Approx(1.0).margin(1e-6));
    }

    SECTION("Set velocity lane length to 0.5 (midpoint), read back matches") {
        auto paramId = static_cast<Steinberg::Vst::ParamID>(
            Ruinae::kArpVelocityLaneLengthId);
        controller.setParamNormalized(paramId, 0.5);

        auto* param = controller.getParameterObject(paramId);
        REQUIRE(param != nullptr);

        double readBack = param->getNormalized();
        REQUIRE(readBack == Approx(0.5).margin(1e-6));
    }

    controller.terminate();
}
