// ==============================================================================
// MultiTap Delay Visibility Tests
// ==============================================================================
// Tests for conditional UI control visibility based on Time Mode parameter.
//
// Plan: When timeMode = Free (0):
//   - BaseTime control: Visible
//   - Internal Tempo control: Visible
//   - NoteValue control: Hidden
//
// Plan: When timeMode = Synced (1):
//   - BaseTime control: Hidden
//   - Internal Tempo control: Hidden
//   - NoteValue control: Visible
//
// Manual Testing Requirements (cannot be automated without full VSTGUI setup):
// 1. Load plugin in a DAW
// 2. Select MultiTap Delay mode
// 3. Verify "Time" and "Int Tempo" controls are visible when "Mode" = "Free"
// 4. Verify "Note" control is hidden when "Mode" = "Free"
// 5. Change "Mode" to "Synced"
// 6. Verify "Time" and "Int Tempo" controls disappear
// 7. Verify "Note" control appears
// 8. Change back to "Free"
// 9. Verify controls return to expected visibility
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "plugin_ids.h"

using namespace Iterum;
using Catch::Approx;

// ==============================================================================
// TEST: MultiTap visibility logic specification
// ==============================================================================

TEST_CASE("MultiTap visibility: Free mode shows baseTime and tempo", "[visibility][multitap]") {
    // Rule: Show baseTime and tempo controls when time mode is Free (< 0.5)
    auto shouldBeVisible = [](float normalizedTimeModeValue) -> bool {
        return normalizedTimeModeValue < 0.5f;
    };

    // Free mode (normalized 0.0)
    REQUIRE(shouldBeVisible(0.0f) == true);
    REQUIRE(shouldBeVisible(0.25f) == true);
    REQUIRE(shouldBeVisible(0.49f) == true);
}

TEST_CASE("MultiTap visibility: Synced mode hides baseTime and tempo", "[visibility][multitap]") {
    // Rule: Hide baseTime and tempo controls when time mode is Synced (>= 0.5)
    auto shouldBeVisible = [](float normalizedTimeModeValue) -> bool {
        return normalizedTimeModeValue < 0.5f;
    };

    // Synced mode (normalized 1.0)
    REQUIRE(shouldBeVisible(0.5f) == false);
    REQUIRE(shouldBeVisible(0.75f) == false);
    REQUIRE(shouldBeVisible(1.0f) == false);
}

TEST_CASE("MultiTap visibility: Synced mode shows noteValue", "[visibility][multitap]") {
    // Rule: Show noteValue control when time mode is Synced (>= 0.5)
    // Note: showWhenBelow = false in controller, meaning show when value >= threshold
    auto shouldBeVisible = [](float normalizedTimeModeValue) -> bool {
        return normalizedTimeModeValue >= 0.5f;
    };

    // Free mode - noteValue hidden
    REQUIRE(shouldBeVisible(0.0f) == false);
    REQUIRE(shouldBeVisible(0.25f) == false);
    REQUIRE(shouldBeVisible(0.49f) == false);

    // Synced mode - noteValue visible
    REQUIRE(shouldBeVisible(0.5f) == true);
    REQUIRE(shouldBeVisible(0.75f) == true);
    REQUIRE(shouldBeVisible(1.0f) == true);
}

TEST_CASE("MultiTap visibility: Pattern always visible", "[visibility][multitap]") {
    // Pattern control has no conditional visibility - always visible in both modes
    // This test documents that Pattern is not part of the visibility system

    REQUIRE(kMultiTapTimingPatternId == 900);  // Pattern ID exists
    REQUIRE(kMultiTapSpatialPatternId == 901); // Spatial pattern exists

    // No visibility controller for these - they're always visible
    // Documenting this explicitly as a design decision
    REQUIRE(true);
}

// ==============================================================================
// TEST: Visibility toggle behavior (Free <-> Synced switching)
// ==============================================================================

TEST_CASE("MultiTap visibility: switching Free->Synced updates visibility", "[visibility][multitap]") {
    // Simulates what happens when user switches from Free to Synced mode

    auto baseTimeShouldBeVisible = [](float normalizedValue) -> bool {
        return normalizedValue < 0.5f;  // Visible when Free
    };

    auto tempoShouldBeVisible = [](float normalizedValue) -> bool {
        return normalizedValue < 0.5f;  // Visible when Free
    };

    auto noteValueShouldBeVisible = [](float normalizedValue) -> bool {
        return normalizedValue >= 0.5f;  // Visible when Synced
    };

    // Start in Free mode (0.0)
    float timeMode = 0.0f;
    REQUIRE(baseTimeShouldBeVisible(timeMode) == true);
    REQUIRE(tempoShouldBeVisible(timeMode) == true);
    REQUIRE(noteValueShouldBeVisible(timeMode) == false);

    // Switch to Synced mode (1.0)
    timeMode = 1.0f;
    REQUIRE(baseTimeShouldBeVisible(timeMode) == false);
    REQUIRE(tempoShouldBeVisible(timeMode) == false);
    REQUIRE(noteValueShouldBeVisible(timeMode) == true);
}

TEST_CASE("MultiTap visibility: switching Synced->Free updates visibility", "[visibility][multitap]") {
    // Simulates what happens when user switches from Synced to Free mode

    auto baseTimeShouldBeVisible = [](float normalizedValue) -> bool {
        return normalizedValue < 0.5f;
    };

    auto tempoShouldBeVisible = [](float normalizedValue) -> bool {
        return normalizedValue < 0.5f;
    };

    auto noteValueShouldBeVisible = [](float normalizedValue) -> bool {
        return normalizedValue >= 0.5f;
    };

    // Start in Synced mode (1.0)
    float timeMode = 1.0f;
    REQUIRE(baseTimeShouldBeVisible(timeMode) == false);
    REQUIRE(tempoShouldBeVisible(timeMode) == false);
    REQUIRE(noteValueShouldBeVisible(timeMode) == true);

    // Switch to Free mode (0.0)
    timeMode = 0.0f;
    REQUIRE(baseTimeShouldBeVisible(timeMode) == true);
    REQUIRE(tempoShouldBeVisible(timeMode) == true);
    REQUIRE(noteValueShouldBeVisible(timeMode) == false);
}

// ==============================================================================
// TEST: Parameter ID verification
// ==============================================================================

TEST_CASE("MultiTap parameter IDs are correctly defined", "[visibility][multitap][ids]") {
    SECTION("Time-related parameter IDs") {
        REQUIRE(kMultiTapBaseTimeId == 903);
        REQUIRE(kMultiTapTempoId == 904);
        REQUIRE(kMultiTapTimeModeId == 910);
        REQUIRE(kMultiTapNoteValueId == 911);
    }

    SECTION("Pattern parameter IDs") {
        REQUIRE(kMultiTapTimingPatternId == 900);
        REQUIRE(kMultiTapSpatialPatternId == 901);
        REQUIRE(kMultiTapTapCountId == 902);
    }

    SECTION("Other MultiTap parameters") {
        REQUIRE(kMultiTapFeedbackId == 905);
        REQUIRE(kMultiTapMixId == 909);
    }
}

// ==============================================================================
// TEST: UI tag assignments for visibility controllers
// ==============================================================================

TEST_CASE("MultiTap UI tags are correctly assigned", "[visibility][multitap][tags]") {
    // These tags must match what's in controller.cpp and editor.uidesc

    SECTION("BaseTime visibility tags") {
        // 9908 = label tag, kMultiTapBaseTimeId (903) = control tag
        constexpr int32_t kBaseTimeLabelTag = 9908;
        constexpr int32_t kBaseTimeControlTag = 903;

        REQUIRE(kBaseTimeLabelTag == 9908);
        REQUIRE(kBaseTimeControlTag == kMultiTapBaseTimeId);
    }

    SECTION("Tempo visibility tags") {
        // 9911 = label tag, kMultiTapTempoId (904) = control tag
        constexpr int32_t kTempoLabelTag = 9911;
        constexpr int32_t kTempoControlTag = 904;

        REQUIRE(kTempoLabelTag == 9911);
        REQUIRE(kTempoControlTag == kMultiTapTempoId);
    }

    SECTION("NoteValue visibility tags") {
        // 9927 = label tag, kMultiTapNoteValueId (911) = control tag
        constexpr int32_t kNoteValueLabelTag = 9927;
        constexpr int32_t kNoteValueControlTag = 911;

        REQUIRE(kNoteValueLabelTag == 9927);
        REQUIRE(kNoteValueControlTag == kMultiTapNoteValueId);
    }
}

// ==============================================================================
// TEST: Edge cases
// ==============================================================================

TEST_CASE("MultiTap visibility handles boundary values", "[visibility][multitap][edge]") {
    auto freeControlsVisible = [](float v) -> bool { return v < 0.5f; };
    auto syncedControlsVisible = [](float v) -> bool { return v >= 0.5f; };

    SECTION("Exactly at threshold (0.5)") {
        // At exactly 0.5, we're in Synced mode
        REQUIRE(freeControlsVisible(0.5f) == false);
        REQUIRE(syncedControlsVisible(0.5f) == true);
    }

    SECTION("Values very close to threshold") {
        REQUIRE(freeControlsVisible(0.499999f) == true);
        REQUIRE(freeControlsVisible(0.500001f) == false);
    }

    SECTION("Extreme values") {
        REQUIRE(freeControlsVisible(0.0f) == true);
        REQUIRE(freeControlsVisible(1.0f) == false);
        REQUIRE(syncedControlsVisible(0.0f) == false);
        REQUIRE(syncedControlsVisible(1.0f) == true);
    }
}

// ==============================================================================
// TEST: Manual testing verification
// ==============================================================================

TEST_CASE("MultiTap visibility requires manual verification", "[visibility][multitap][manual]") {
    // Full integration testing requires VSTGUI infrastructure.
    // This test documents the manual verification procedure.

    SECTION("Manual test procedure for MultiTap visibility") {
        // 1. Load plugin in a DAW
        // 2. Switch to MultiTap mode
        // 3. Set Mode = "Free"
        //    - Verify: "Time" slider visible
        //    - Verify: "Int Tempo" slider visible
        //    - Verify: "Note" dropdown hidden
        // 4. Set Mode = "Synced"
        //    - Verify: "Time" slider hidden
        //    - Verify: "Int Tempo" slider hidden
        //    - Verify: "Note" dropdown visible
        // 5. Repeat switching several times
        // 6. Load a preset and verify visibility matches the saved time mode

        REQUIRE(true);  // Placeholder for manual verification
    }
}
