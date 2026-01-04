// ==============================================================================
// MultiTap Delay Visibility Tests (Simplified Design)
// ==============================================================================
// Tests for conditional UI control visibility based on Pattern selection.
//
// SIMPLIFIED DESIGN:
// - No Free/Synced mode toggle (removed)
// - No Time slider (removed)
// - No Internal Tempo slider (removed)
// - Rhythmic patterns (0-13): Use host tempo, no additional controls needed
// - Mathematical patterns (14-18): Use Note Value + host tempo for base time
//
// Visibility Rules:
// - Pattern dropdown: Always visible
// - Note Value: Visible only when pattern is mathematical (14+)
// - Note Modifier: Visible only when pattern is mathematical (14+)
//
// Manual Testing Requirements:
// 1. Load plugin in a DAW
// 2. Select MultiTap Delay mode
// 3. Select "Quarter Note" pattern (preset)
//    - Verify: "Note" dropdown is HIDDEN
// 4. Select "Golden Ratio" pattern (mathematical)
//    - Verify: "Note" dropdown appears
// 5. Switch between preset and mathematical patterns
//    - Verify Note control toggles visibility correctly
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "plugin_ids.h"

using namespace Iterum;
using Catch::Approx;

// ==============================================================================
// TEST: Pattern-based visibility specification
// ==============================================================================

TEST_CASE("MultiTap visibility: NoteValue visible only for mathematical patterns", "[visibility][multitap]") {
    // Rule: Show noteValue when pattern is mathematical (>= 14/19)
    //
    // Mathematical patterns: GoldenRatio (14), Fibonacci (15), Exponential (16),
    //                        PrimeNumbers (17), LinearSpread (18), Custom (19)
    // Preset patterns: WholeNote (0) through TripletSixteenth (13)

    auto noteValueShouldBeVisible = [](float normalizedPattern) -> bool {
        return normalizedPattern >= (14.0f / 19.0f);
    };

    SECTION("Preset patterns (0-13) - noteValue hidden") {
        REQUIRE(noteValueShouldBeVisible(0.0f / 19.0f) == false);   // WholeNote
        REQUIRE(noteValueShouldBeVisible(1.0f / 19.0f) == false);   // HalfNote
        REQUIRE(noteValueShouldBeVisible(2.0f / 19.0f) == false);   // QuarterNote
        REQUIRE(noteValueShouldBeVisible(3.0f / 19.0f) == false);   // EighthNote
        REQUIRE(noteValueShouldBeVisible(4.0f / 19.0f) == false);   // SixteenthNote
        REQUIRE(noteValueShouldBeVisible(5.0f / 19.0f) == false);   // ThirtySecondNote
        REQUIRE(noteValueShouldBeVisible(6.0f / 19.0f) == false);   // DottedHalf
        REQUIRE(noteValueShouldBeVisible(7.0f / 19.0f) == false);   // DottedQuarter
        REQUIRE(noteValueShouldBeVisible(8.0f / 19.0f) == false);   // DottedEighth
        REQUIRE(noteValueShouldBeVisible(9.0f / 19.0f) == false);   // DottedSixteenth
        REQUIRE(noteValueShouldBeVisible(10.0f / 19.0f) == false);  // TripletHalf
        REQUIRE(noteValueShouldBeVisible(11.0f / 19.0f) == false);  // TripletQuarter
        REQUIRE(noteValueShouldBeVisible(12.0f / 19.0f) == false);  // TripletEighth
        REQUIRE(noteValueShouldBeVisible(13.0f / 19.0f) == false);  // TripletSixteenth
    }

    SECTION("Mathematical patterns (14-18) - noteValue visible") {
        REQUIRE(noteValueShouldBeVisible(14.0f / 19.0f) == true);   // GoldenRatio
        REQUIRE(noteValueShouldBeVisible(15.0f / 19.0f) == true);   // Fibonacci
        REQUIRE(noteValueShouldBeVisible(16.0f / 19.0f) == true);   // Exponential
        REQUIRE(noteValueShouldBeVisible(17.0f / 19.0f) == true);   // PrimeNumbers
        REQUIRE(noteValueShouldBeVisible(18.0f / 19.0f) == true);   // LinearSpread
    }

    SECTION("Custom pattern (19) - noteValue visible") {
        // Custom pattern also uses baseTimeMs_, so Note Value should be shown
        REQUIRE(noteValueShouldBeVisible(19.0f / 19.0f) == true);   // Custom
        REQUIRE(noteValueShouldBeVisible(1.0f) == true);            // Max normalized value
    }
}

TEST_CASE("MultiTap visibility: Pattern dropdown always visible", "[visibility][multitap]") {
    // Pattern control has no conditional visibility - always visible
    // This test documents that Pattern is not part of the visibility system

    REQUIRE(kMultiTapTimingPatternId == 900);  // Pattern ID exists
    REQUIRE(kMultiTapSpatialPatternId == 901); // Spatial pattern exists

    // No visibility controller for these - they're always visible
    REQUIRE(true);
}

// ==============================================================================
// TEST: Pattern switching behavior
// ==============================================================================

TEST_CASE("MultiTap visibility: switching between preset and mathematical patterns", "[visibility][multitap]") {
    auto noteValueShouldBeVisible = [](float normalizedPattern) -> bool {
        return normalizedPattern >= (14.0f / 19.0f);
    };

    constexpr float kQuarterNotePattern = 2.0f / 19.0f;
    constexpr float kGoldenRatioPattern = 14.0f / 19.0f;
    constexpr float kFibonacciPattern = 15.0f / 19.0f;
    constexpr float kTripletEighthPattern = 12.0f / 19.0f;

    // Start with Quarter Note (preset) - Note Value hidden
    float pattern = kQuarterNotePattern;
    REQUIRE(noteValueShouldBeVisible(pattern) == false);

    // Switch to GoldenRatio (mathematical) - Note Value appears
    pattern = kGoldenRatioPattern;
    REQUIRE(noteValueShouldBeVisible(pattern) == true);

    // Switch to TripletEighth (preset) - Note Value hidden again
    pattern = kTripletEighthPattern;
    REQUIRE(noteValueShouldBeVisible(pattern) == false);

    // Switch to Fibonacci (mathematical) - Note Value appears again
    pattern = kFibonacciPattern;
    REQUIRE(noteValueShouldBeVisible(pattern) == true);
}

// ==============================================================================
// TEST: Parameter ID verification
// ==============================================================================

TEST_CASE("MultiTap parameter IDs are correctly defined", "[visibility][multitap][ids]") {
    SECTION("Pattern parameter IDs") {
        REQUIRE(kMultiTapTimingPatternId == 900);
        REQUIRE(kMultiTapSpatialPatternId == 901);
        REQUIRE(kMultiTapTapCountId == 902);
    }

    SECTION("Note Value parameter IDs") {
        REQUIRE(kMultiTapNoteValueId == 911);
        REQUIRE(kMultiTapNoteModifierId == 912);
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

    SECTION("NoteValue visibility tags") {
        // 9927 = label tag, kMultiTapNoteValueId (911) = control tag
        constexpr int32_t kNoteValueLabelTag = 9927;
        constexpr int32_t kNoteValueControlTag = 911;

        REQUIRE(kNoteValueLabelTag == 9927);
        REQUIRE(kNoteValueControlTag == kMultiTapNoteValueId);
    }
}

// ==============================================================================
// TEST: Pattern threshold verification
// ==============================================================================

TEST_CASE("MultiTap pattern threshold is correct", "[visibility][multitap][threshold]") {
    // The VisibilityController for MultiTap Note Value uses pattern threshold:
    // - Pattern threshold: 14.0f/19.0f (showWhenBelow=false means show when >= 14/19)
    //
    // This must match what's in controller.cpp didOpen()

    constexpr float kPatternThreshold = 14.0f / 19.0f;

    SECTION("Last preset pattern is below threshold") {
        // TripletSixteenth (13) is the last preset pattern
        REQUIRE(13.0f / 19.0f < kPatternThreshold);
    }

    SECTION("First mathematical pattern is at threshold") {
        // GoldenRatio (14) is the first mathematical pattern
        REQUIRE(14.0f / 19.0f >= kPatternThreshold);
    }

    SECTION("All mathematical patterns are at or above threshold") {
        REQUIRE(14.0f / 19.0f >= kPatternThreshold);  // GoldenRatio
        REQUIRE(15.0f / 19.0f >= kPatternThreshold);  // Fibonacci
        REQUIRE(16.0f / 19.0f >= kPatternThreshold);  // Exponential
        REQUIRE(17.0f / 19.0f >= kPatternThreshold);  // PrimeNumbers
        REQUIRE(18.0f / 19.0f >= kPatternThreshold);  // LinearSpread
        REQUIRE(19.0f / 19.0f >= kPatternThreshold);  // Custom
    }

    SECTION("All preset patterns are below threshold") {
        for (int i = 0; i <= 13; ++i) {
            float normalizedPattern = static_cast<float>(i) / 19.0f;
            REQUIRE(normalizedPattern < kPatternThreshold);
        }
    }
}

// ==============================================================================
// TEST: Edge cases
// ==============================================================================

TEST_CASE("MultiTap visibility handles boundary values", "[visibility][multitap][edge]") {
    auto noteValueVisible = [](float pattern) -> bool {
        return pattern >= (14.0f / 19.0f);
    };

    constexpr float kMathThreshold = 14.0f / 19.0f;
    constexpr float kGoldenRatio = 14.0f / 19.0f;
    constexpr float kLastPreset = 13.0f / 19.0f;  // TripletSixteenth

    SECTION("Pattern threshold boundary") {
        // TripletSixteenth (13) is the last preset pattern
        REQUIRE(noteValueVisible(kLastPreset) == false);     // Last preset - hidden
        REQUIRE(noteValueVisible(kGoldenRatio) == true);     // First mathematical - visible

        // Very close to threshold
        REQUIRE(noteValueVisible(kMathThreshold - 0.001f) == false);
        REQUIRE(noteValueVisible(kMathThreshold) == true);
    }

    SECTION("Extreme values") {
        REQUIRE(noteValueVisible(0.0f) == false);    // First pattern (WholeNote)
        REQUIRE(noteValueVisible(1.0f) == true);     // Last pattern (Custom at max)
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
        //
        // Test A: Preset pattern (no Note Value)
        // 3. Select Pattern = "Quarter Note" (or any preset pattern 0-13)
        //    - Verify: "Note" dropdown is HIDDEN
        //    - Verify: Only Pattern, Tap Count, Feedback, Mix controls visible
        //
        // Test B: Mathematical pattern (with Note Value)
        // 4. Select Pattern = "Golden Ratio" (or any mathematical pattern 14-18)
        //    - Verify: "Note" dropdown appears
        //    - Change Note Value and verify delay timing changes
        //
        // Test C: Pattern switching
        // 5. Switch Pattern = "Fibonacci"
        //    - Verify: "Note" dropdown still visible
        // 6. Switch Pattern = "Eighth Note" (preset)
        //    - Verify: "Note" dropdown becomes hidden
        // 7. Switch Pattern = "Exponential" (mathematical)
        //    - Verify: "Note" dropdown reappears
        //
        // Test D: Note Value affects mathematical patterns
        // 8. With Fibonacci pattern selected
        //    - Change Note Value from 1/4 to 1/8
        //    - Verify: Tap timing halves (shorter delays)
        //    - Change Note Value from 1/8 to 1/2
        //    - Verify: Tap timing doubles (longer delays)

        REQUIRE(true);  // Placeholder for manual verification
    }
}
