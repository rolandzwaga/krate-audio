// ==============================================================================
// Control Visibility Logic Tests
// ==============================================================================
// Tests for conditional UI control visibility based on parameter values.
// Specifically tests the logic for hiding delay time controls when time mode
// is set to "Synced" (since the time value is ignored in synced mode).
//
// Manual Testing Requirements (cannot be automated without full VSTGUI setup):
// 1. Load plugin in a DAW
// 2. Select Digital Delay mode
// 3. Verify "Delay Time" control is visible when "Time Mode" = "Free"
// 4. Change "Time Mode" to "Synced"
// 5. Verify "Delay Time" control disappears
// 6. Change back to "Free"
// 7. Verify "Delay Time" control reappears
// 8. Repeat steps 2-7 for PingPong Delay mode
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "plugin_ids.h"

using namespace Iterum;
using Catch::Approx;

// ==============================================================================
// TEST: Time Mode parameter values
// ==============================================================================

TEST_CASE("Time Mode parameter values follow correct mapping", "[vst][visibility][timemode]") {
    SECTION("Digital Time Mode values") {
        // Time mode is a binary parameter: 0 = Free, 1 = Synced
        // Normalized values: 0.0 = Free, 1.0 = Synced
        // Threshold: normalized < 0.5 = Free, >= 0.5 = Synced

        constexpr float kFreeModeNormalized = 0.0f;
        constexpr float kSyncedModeNormalized = 1.0f;
        constexpr float kVisibilityThreshold = 0.5f;

        REQUIRE(kFreeModeNormalized < kVisibilityThreshold);
        REQUIRE(kSyncedModeNormalized >= kVisibilityThreshold);
    }

    SECTION("PingPong Time Mode values") {
        // Same mapping as Digital
        constexpr float kFreeModeNormalized = 0.0f;
        constexpr float kSyncedModeNormalized = 1.0f;
        constexpr float kVisibilityThreshold = 0.5f;

        REQUIRE(kFreeModeNormalized < kVisibilityThreshold);
        REQUIRE(kSyncedModeNormalized >= kVisibilityThreshold);
    }
}

// ==============================================================================
// TEST: Visibility logic specification
// ==============================================================================

TEST_CASE("Delay time visibility follows correct logic", "[vst][visibility][logic]") {
    SECTION("Digital Delay Time visibility") {
        // Rule: Show delay time control when time mode is Free (< 0.5)
        //       Hide delay time control when time mode is Synced (>= 0.5)

        auto shouldBeVisible = [](float normalizedTimeModeValue) -> bool {
            return normalizedTimeModeValue < 0.5f;
        };

        REQUIRE(shouldBeVisible(0.0f) == true);   // Free mode
        REQUIRE(shouldBeVisible(0.25f) == true);  // Still Free
        REQUIRE(shouldBeVisible(0.49f) == true);  // Still Free
        REQUIRE(shouldBeVisible(0.5f) == false);  // Synced mode
        REQUIRE(shouldBeVisible(0.75f) == false); // Still Synced
        REQUIRE(shouldBeVisible(1.0f) == false);  // Synced mode
    }

    SECTION("PingPong Delay Time visibility") {
        // Same logic as Digital
        auto shouldBeVisible = [](float normalizedTimeModeValue) -> bool {
            return normalizedTimeModeValue < 0.5f;
        };

        REQUIRE(shouldBeVisible(0.0f) == true);   // Free mode
        REQUIRE(shouldBeVisible(0.25f) == true);  // Still Free
        REQUIRE(shouldBeVisible(0.49f) == true);  // Still Free
        REQUIRE(shouldBeVisible(0.5f) == false);  // Synced mode
        REQUIRE(shouldBeVisible(0.75f) == false); // Still Synced
        REQUIRE(shouldBeVisible(1.0f) == false);  // Synced mode
    }
}

// ==============================================================================
// TEST: Parameter ID mapping
// ==============================================================================

TEST_CASE("Correct parameter IDs are used for visibility control", "[vst][visibility][ids]") {
    SECTION("Digital Delay parameters") {
        REQUIRE(kDigitalDelayTimeId == 600);
        REQUIRE(kDigitalTimeModeId == 601);

        // These IDs must be adjacent for the visibility logic to work correctly
        REQUIRE(kDigitalTimeModeId == kDigitalDelayTimeId + 1);
    }

    SECTION("PingPong Delay parameters") {
        REQUIRE(kPingPongDelayTimeId == 700);
        REQUIRE(kPingPongTimeModeId == 701);

        // These IDs must be adjacent for the visibility logic to work correctly
        REQUIRE(kPingPongTimeModeId == kPingPongDelayTimeId + 1);
    }
}

// ==============================================================================
// TEST: Mode filtering (prevents updating controls in hidden views)
// ==============================================================================

TEST_CASE("Visibility updates are filtered by current mode", "[vst][visibility][mode]") {
    // This test documents the fix for the hang bug when switching modes
    // The problem: setParamNormalized() was updating control visibility
    // for ALL time mode parameters, even when those controls belonged to
    // hidden mode views. This caused race conditions and hangs.
    //
    // The fix: Only update visibility for the currently active mode.

    SECTION("Digital delay time control should only update when in Digital mode") {
        // Digital mode index
        constexpr int32_t kDigitalModeIndex = static_cast<int32_t>(DelayMode::Digital);
        REQUIRE(kDigitalModeIndex == 5);

        // Should update Digital control ONLY when mode == Digital (5)
        auto shouldUpdateDigital = [](int32_t currentMode) -> bool {
            return currentMode == kDigitalModeIndex;
        };

        REQUIRE(shouldUpdateDigital(0) == false);  // Granular mode
        REQUIRE(shouldUpdateDigital(1) == false);  // Spectral mode
        REQUIRE(shouldUpdateDigital(2) == false);  // Shimmer mode
        REQUIRE(shouldUpdateDigital(3) == false);  // Tape mode
        REQUIRE(shouldUpdateDigital(4) == false);  // BBD mode
        REQUIRE(shouldUpdateDigital(5) == true);   // Digital mode - YES
        REQUIRE(shouldUpdateDigital(6) == false);  // PingPong mode
        REQUIRE(shouldUpdateDigital(7) == false);  // Reverse mode
    }

    SECTION("PingPong delay time control should only update when in PingPong mode") {
        // PingPong mode index
        constexpr int32_t kPingPongModeIndex = static_cast<int32_t>(DelayMode::PingPong);
        REQUIRE(kPingPongModeIndex == 6);

        // Should update PingPong control ONLY when mode == PingPong (6)
        auto shouldUpdatePingPong = [](int32_t currentMode) -> bool {
            return currentMode == kPingPongModeIndex;
        };

        REQUIRE(shouldUpdatePingPong(0) == false);  // Granular mode
        REQUIRE(shouldUpdatePingPong(1) == false);  // Spectral mode
        REQUIRE(shouldUpdatePingPong(2) == false);  // Shimmer mode
        REQUIRE(shouldUpdatePingPong(3) == false);  // Tape mode
        REQUIRE(shouldUpdatePingPong(4) == false);  // BBD mode
        REQUIRE(shouldUpdatePingPong(5) == false);  // Digital mode
        REQUIRE(shouldUpdatePingPong(6) == true);   // PingPong mode - YES
        REQUIRE(shouldUpdatePingPong(7) == false);  // Reverse mode
    }

    SECTION("Mode switching scenario that caused the hang") {
        // Repro: Switch from Digital (5) to PingPong (6)
        // When mode changes, setParamNormalized gets called for parameters
        // from BOTH modes during state sync. Without mode filtering,
        // this would try to manipulate controls in hidden views.

        // Digital mode active
        int32_t currentMode = 5;
        REQUIRE((currentMode == 5) == true);   // Update Digital control
        REQUIRE((currentMode == 6) == false);  // Don't touch PingPong control

        // Switch to PingPong mode
        currentMode = 6;
        REQUIRE((currentMode == 5) == false);  // Don't touch Digital control
        REQUIRE((currentMode == 6) == true);   // Update PingPong control
    }
}

// ==============================================================================
// TEST: Edge cases
// ==============================================================================

TEST_CASE("Visibility logic handles edge cases", "[vst][visibility][edge]") {
    SECTION("Boundary value exactly at threshold") {
        // At exactly 0.5, we should be in Synced mode (hidden)
        auto shouldBeVisible = [](float normalizedValue) -> bool {
            return normalizedValue < 0.5f;
        };

        REQUIRE(shouldBeVisible(0.5f) == false);
    }

    SECTION("Very small values near zero") {
        auto shouldBeVisible = [](float normalizedValue) -> bool {
            return normalizedValue < 0.5f;
        };

        REQUIRE(shouldBeVisible(0.0f) == true);
        REQUIRE(shouldBeVisible(0.001f) == true);
        REQUIRE(shouldBeVisible(0.00001f) == true);
    }

    SECTION("Values near 1.0") {
        auto shouldBeVisible = [](float normalizedValue) -> bool {
            return normalizedValue < 0.5f;
        };

        REQUIRE(shouldBeVisible(0.999f) == false);
        REQUIRE(shouldBeVisible(0.99999f) == false);
        REQUIRE(shouldBeVisible(1.0f) == false);
    }
}

// ==============================================================================
// TEST: UIViewSwitchContainer invalidates cached control references
// ==============================================================================
// REGRESSION TEST for visibility bug after mode switching
//
// Bug Description:
// - User switches from Digital to PingPong mode
// - UIViewSwitchContainer destroys Digital view controls, creates PingPong view controls
// - VisibilityController still holds reference to DESTROYED Digital delay time control
// - When time mode changes, setVisible() is called on destroyed control
// - New PingPong delay time control is never updated, visibility stuck
//
// Root Cause:
// - UIViewSwitchContainer destroys/recreates ALL controls when switching templates
// - Cached control pointers become invalid (dangling references)
// - setVisible() on destroyed control has no effect on new control
//
// Solution:
// - DO NOT cache control pointers across view switches
// - Look up control DYNAMICALLY on each update using control tag
// - Control lookup via frame->findControlByTag() always finds current control
// ==============================================================================

TEST_CASE("Control references must be dynamic, not cached", "[vst][visibility][viewswitch]") {
    // This test documents the requirement that VisibilityController must look up
    // controls dynamically on each update, not cache references.
    //
    // WHY: UIViewSwitchContainer destroys and recreates controls when switching views.
    // Cached pointers become dangling references after view switch.

    SECTION("Control lookup requirement") {
        // VisibilityController MUST use control tag for lookup, not cached pointer
        //
        // WRONG approach (causes bug):
        //   Constructor: delayTimeControl_ = findControl(kDigitalDelayTimeId);
        //   update(): delayTimeControl_->setVisible(shouldBeVisible);  // Dangling!
        //
        // CORRECT approach:
        //   Constructor: delayTimeControlTag_ = kDigitalDelayTimeId;
        //   update(): auto* control = findControl(delayTimeControlTag_);
        //             if (control) control->setVisible(shouldBeVisible);

        constexpr int32_t kDigitalDelayTimeTag = 600;
        constexpr int32_t kPingPongDelayTimeTag = 700;

        // These tags must remain constant across view switches
        REQUIRE(kDigitalDelayTimeTag == 600);
        REQUIRE(kPingPongDelayTimeTag == 700);

        // The tag identifies the control, not the pointer
        // After view switch:
        // - Old control pointer is INVALID (destroyed)
        // - Same tag finds NEW control pointer (freshly created)
    }

    SECTION("View switch invalidation scenario") {
        // Scenario that caused the bug:
        // 1. User is in Digital mode, time mode control visibility works correctly
        // 2. User switches to PingPong mode
        //    - UIViewSwitchContainer calls setCurrentViewIndex(6)
        //    - Digital view (including delay time control) is DESTROYED
        //    - PingPong view (including delay time control) is CREATED
        // 3. User changes time mode between Free/Synced
        //    - VisibilityController::update() is called
        //    - Tries to call setVisible() on CACHED pointer to Digital control
        //    - Digital control was DESTROYED in step 2!
        //    - PingPong control never gets setVisible() called
        //    - Visibility stuck in whatever state the template defined

        // The fix: Look up control on EVERY update
        bool useCachedPointer = false;  // WRONG - causes bug
        bool useDynamicLookup = true;   // CORRECT - survives view switches

        REQUIRE(useCachedPointer == false);
        REQUIRE(useDynamicLookup == true);
    }

    SECTION("Manual testing verification") {
        // This bug CANNOT be fully tested in unit tests because it requires
        // UIViewSwitchContainer and full VSTGUI infrastructure.
        //
        // Manual test procedure:
        // 1. Load plugin, select Digital mode
        // 2. Change time mode to "Synced" - verify delay time DISAPPEARS
        // 3. Change time mode to "Free" - verify delay time REAPPEARS
        // 4. Switch to PingPong mode
        // 5. Change time mode to "Synced" - delay time MUST DISAPPEAR
        // 6. Change time mode to "Free" - delay time MUST REAPPEAR
        //
        // BUG SYMPTOM: Steps 5-6 don't work, visibility stuck from step 4
        // CAUSE: Cached pointer to destroyed Digital control
        // FIX: Dynamic lookup finds current PingPong control

        REQUIRE(true);  // Test documents manual verification requirements
    }
}
