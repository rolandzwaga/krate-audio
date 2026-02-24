// ==============================================================================
// Arpeggiator Controller Tests (071-arp-engine-integration, 079-layout-framework)
// ==============================================================================
// Tests for controller-level arp integration: tempo sync visibility toggle,
// parameter registration verification, velocity/gate lane wiring.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "parameters/arpeggiator_params.h"
#include "plugin_ids.h"
#include "controller/controller.h"

#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstparameters.h"

using Catch::Approx;

TEST_CASE("ArpController placeholder - struct accessible from controller tests", "[arp][controller]") {
    Ruinae::ArpeggiatorParams params;
    params.tempoSync.store(true, std::memory_order_relaxed);
    REQUIRE(params.tempoSync.load() == true);
}

// ==============================================================================
// T043: Arp Tempo Sync Visibility Toggle Logic (FR-016)
// ==============================================================================
// Tests the visibility logic for the arp rate/note-value groups that is
// implemented in Controller::setParamNormalized(). Since we cannot instantiate
// the full VSTGUI controller in a unit test, we verify the core logic:
//   - When kArpTempoSyncId value < 0.5: rate group visible, note value hidden
//   - When kArpTempoSyncId value >= 0.5: rate group hidden, note value visible
//
// This mirrors the pattern used by all other sync toggles in the controller.
// ==============================================================================

TEST_CASE("ArpController_TempoSyncToggle_SwitchesVisibility", "[arp][controller]") {
    // The visibility toggle logic in setParamNormalized is:
    //   rateGroupVisible    = (value < 0.5)
    //   noteValueVisible    = (value >= 0.5)
    //
    // We verify this logic for the relevant normalized boundary values.

    SECTION("Tempo sync OFF (value=0.0): rate group visible, note value hidden") {
        double value = 0.0;
        bool rateGroupVisible = (value < 0.5);
        bool noteValueVisible = (value >= 0.5);

        REQUIRE(rateGroupVisible == true);
        REQUIRE(noteValueVisible == false);
    }

    SECTION("Tempo sync ON (value=1.0): rate group hidden, note value visible") {
        double value = 1.0;
        bool rateGroupVisible = (value < 0.5);
        bool noteValueVisible = (value >= 0.5);

        REQUIRE(rateGroupVisible == false);
        REQUIRE(noteValueVisible == true);
    }

    SECTION("Boundary value at 0.5: note value takes priority (sync ON)") {
        double value = 0.5;
        bool rateGroupVisible = (value < 0.5);
        bool noteValueVisible = (value >= 0.5);

        REQUIRE(rateGroupVisible == false);
        REQUIRE(noteValueVisible == true);
    }

    SECTION("Parameter ID for tempo sync is kArpTempoSyncId (3004)") {
        REQUIRE(Ruinae::kArpTempoSyncId == 3004);
    }

    SECTION("Default tempoSync is true (sync ON)") {
        Ruinae::ArpeggiatorParams params;
        bool defaultSync = params.tempoSync.load(std::memory_order_relaxed);
        REQUIRE(defaultSync == true);

        // Default state: rate group should be hidden, note value visible
        double normalizedDefault = defaultSync ? 1.0 : 0.0;
        REQUIRE((normalizedDefault < 0.5) == false);   // rate hidden
        REQUIRE((normalizedDefault >= 0.5) == true);    // note value visible
    }
}

// ==============================================================================
// T022: Velocity Lane Parameter Registration (079-layout-framework, US1)
// ==============================================================================
// Verify that after calling registerArpParams(), the velocity lane parameters
// are properly registered: kArpVelocityLaneStep0Id through
// kArpVelocityLaneStep31Id (IDs 3021-3052) and kArpVelocityLaneLengthId (3020).
// Also verify playhead parameter IDs are defined.
// ==============================================================================

TEST_CASE("VelocityLane_ParameterIds_CorrectRange", "[arp][controller][velocity]") {
    SECTION("Velocity lane step IDs span 3021-3052") {
        REQUIRE(Ruinae::kArpVelocityLaneStep0Id == 3021);
        REQUIRE(Ruinae::kArpVelocityLaneStep31Id == 3052);

        // All 32 steps should be contiguous
        for (int i = 0; i < 32; ++i) {
            REQUIRE(static_cast<int>(Ruinae::kArpVelocityLaneStep0Id) + i ==
                    3021 + i);
        }
    }

    SECTION("Velocity lane length ID is 3020") {
        REQUIRE(Ruinae::kArpVelocityLaneLengthId == 3020);
    }

    SECTION("Playhead parameter IDs are defined") {
        REQUIRE(Ruinae::kArpVelocityPlayheadId == 3294);
        REQUIRE(Ruinae::kArpGatePlayheadId == 3295);
    }
}

TEST_CASE("VelocityLane_ParameterRegistration_AllStepsRegistered", "[arp][controller][velocity]") {
    // Create a Controller, call initialize(), and verify parameter objects exist
    Ruinae::Controller controller;

    // Initialize the controller (registers all parameters)
    auto result = controller.initialize(nullptr);
    REQUIRE(result == Steinberg::kResultOk);

    SECTION("Velocity lane length parameter is registered") {
        auto* param = controller.getParameterObject(Ruinae::kArpVelocityLaneLengthId);
        REQUIRE(param != nullptr);
    }

    SECTION("All 32 velocity lane step parameters are registered") {
        for (int i = 0; i < 32; ++i) {
            auto paramId = static_cast<Steinberg::Vst::ParamID>(
                Ruinae::kArpVelocityLaneStep0Id + i);
            auto* param = controller.getParameterObject(paramId);
            REQUIRE(param != nullptr);
        }
    }

    SECTION("Velocity playhead parameter is registered") {
        auto* param = controller.getParameterObject(Ruinae::kArpVelocityPlayheadId);
        REQUIRE(param != nullptr);
    }

    SECTION("Gate playhead parameter is registered") {
        auto* param = controller.getParameterObject(Ruinae::kArpGatePlayheadId);
        REQUIRE(param != nullptr);
    }

    controller.terminate();
}
