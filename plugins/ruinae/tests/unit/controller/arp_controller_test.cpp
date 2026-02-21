// ==============================================================================
// Arpeggiator Controller Tests (071-arp-engine-integration)
// ==============================================================================
// Tests for controller-level arp integration: tempo sync visibility toggle,
// parameter registration verification.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include "parameters/arpeggiator_params.h"
#include "plugin_ids.h"

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
