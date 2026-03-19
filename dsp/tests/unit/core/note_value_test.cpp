// ==============================================================================
// Layer 0: Core Utility Tests - NoteValue
// ==============================================================================
// Tests for musical note value enums and helper functions.
//
// Constitution Compliance:
// - Principle XII: Test-First Development
//
// Reference: specs/017-layer0-utilities/spec.md (Phase 2)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>

#include <krate/dsp/core/note_value.h>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// NoteValue Enum Tests (T005)
// =============================================================================

TEST_CASE("NoteValue enum has correct values", "[note_value][US1]") {
    SECTION("Enum values are sequential from 0") {
        REQUIRE(static_cast<uint8_t>(NoteValue::QuadrupleWhole) == 0);
        REQUIRE(static_cast<uint8_t>(NoteValue::TripleWhole) == 1);
        REQUIRE(static_cast<uint8_t>(NoteValue::DoubleWhole) == 2);
        REQUIRE(static_cast<uint8_t>(NoteValue::Whole) == 3);
        REQUIRE(static_cast<uint8_t>(NoteValue::Half) == 4);
        REQUIRE(static_cast<uint8_t>(NoteValue::Quarter) == 5);
        REQUIRE(static_cast<uint8_t>(NoteValue::Eighth) == 6);
        REQUIRE(static_cast<uint8_t>(NoteValue::Sixteenth) == 7);
        REQUIRE(static_cast<uint8_t>(NoteValue::ThirtySecond) == 8);
        REQUIRE(static_cast<uint8_t>(NoteValue::SixtyFourth) == 9);
    }

    SECTION("All 10 note values are defined") {
        // Verify array indexing works for all values
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::QuadrupleWhole)] > 0.0f);
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::TripleWhole)] > 0.0f);
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::DoubleWhole)] > 0.0f);
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::Whole)] > 0.0f);
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::Half)] > 0.0f);
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::Quarter)] > 0.0f);
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::Eighth)] > 0.0f);
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::Sixteenth)] > 0.0f);
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::ThirtySecond)] > 0.0f);
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::SixtyFourth)] > 0.0f);
    }
}

// =============================================================================
// NoteModifier Enum Tests (T006)
// =============================================================================

TEST_CASE("NoteModifier enum has correct values", "[note_value][US1]") {
    SECTION("Enum values are sequential from 0") {
        REQUIRE(static_cast<uint8_t>(NoteModifier::None) == 0);
        REQUIRE(static_cast<uint8_t>(NoteModifier::Dotted) == 1);
        REQUIRE(static_cast<uint8_t>(NoteModifier::Triplet) == 2);
    }

    SECTION("All 3 modifiers are defined") {
        // Verify array indexing works for all values
        REQUIRE(kModifierMultiplier[static_cast<size_t>(NoteModifier::None)] > 0.0f);
        REQUIRE(kModifierMultiplier[static_cast<size_t>(NoteModifier::Dotted)] > 0.0f);
        REQUIRE(kModifierMultiplier[static_cast<size_t>(NoteModifier::Triplet)] > 0.0f);
    }
}

// =============================================================================
// kBeatsPerNote Array Tests (T005)
// =============================================================================

TEST_CASE("kBeatsPerNote has correct beat values for 4/4 time", "[note_value][US1]") {
    SECTION("Double whole note = 8 beats") {
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::DoubleWhole)] == 8.0f);
    }

    SECTION("Whole note = 4 beats") {
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::Whole)] == 4.0f);
    }

    SECTION("Half note = 2 beats") {
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::Half)] == 2.0f);
    }

    SECTION("Quarter note = 1 beat") {
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::Quarter)] == 1.0f);
    }

    SECTION("Eighth note = 0.5 beats") {
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::Eighth)] == 0.5f);
    }

    SECTION("Sixteenth note = 0.25 beats") {
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::Sixteenth)] == 0.25f);
    }

    SECTION("Thirty-second note = 0.125 beats") {
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::ThirtySecond)] == 0.125f);
    }

    SECTION("Sixty-fourth note = 0.0625 beats") {
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::SixtyFourth)] == 0.0625f);
    }

    SECTION("Standard values decrease by factor of 2 (DoubleWhole through SixtyFourth)") {
        // DoubleWhole through SixtyFourth follow strict power-of-2 progression
        constexpr size_t dw = static_cast<size_t>(NoteValue::DoubleWhole);
        REQUIRE(kBeatsPerNote[dw + 0] / kBeatsPerNote[dw + 1] == 2.0f);  // DoubleWhole/Whole
        REQUIRE(kBeatsPerNote[dw + 1] / kBeatsPerNote[dw + 2] == 2.0f);  // Whole/Half
        REQUIRE(kBeatsPerNote[dw + 2] / kBeatsPerNote[dw + 3] == 2.0f);  // Half/Quarter
        REQUIRE(kBeatsPerNote[dw + 3] / kBeatsPerNote[dw + 4] == 2.0f);  // Quarter/Eighth
        REQUIRE(kBeatsPerNote[dw + 4] / kBeatsPerNote[dw + 5] == 2.0f);  // Eighth/Sixteenth
        REQUIRE(kBeatsPerNote[dw + 5] / kBeatsPerNote[dw + 6] == 2.0f);  // Sixteenth/ThirtySecond
        REQUIRE(kBeatsPerNote[dw + 6] / kBeatsPerNote[dw + 7] == 2.0f);  // ThirtySecond/SixtyFourth
    }

    SECTION("Extended note values have correct beats") {
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::QuadrupleWhole)] == 16.0f);
        REQUIRE(kBeatsPerNote[static_cast<size_t>(NoteValue::TripleWhole)] == 12.0f);
    }
}

// =============================================================================
// kModifierMultiplier Array Tests (T006)
// =============================================================================

TEST_CASE("kModifierMultiplier has correct multiplier values", "[note_value][US1]") {
    SECTION("None modifier = 1.0x (no change)") {
        REQUIRE(kModifierMultiplier[static_cast<size_t>(NoteModifier::None)] == 1.0f);
    }

    SECTION("Dotted modifier = 1.5x duration") {
        REQUIRE(kModifierMultiplier[static_cast<size_t>(NoteModifier::Dotted)] == 1.5f);
    }

    SECTION("Triplet modifier = 2/3x duration (~0.667)") {
        // Use Approx for floating-point comparison
        REQUIRE(kModifierMultiplier[static_cast<size_t>(NoteModifier::Triplet)] ==
                Approx(0.6666666666667f).margin(1e-10f));
    }

    SECTION("Triplet is exactly 2/3") {
        float triplet = kModifierMultiplier[static_cast<size_t>(NoteModifier::Triplet)];
        // 2/3 as float
        float expected = 2.0f / 3.0f;
        REQUIRE(triplet == Approx(expected).margin(1e-6f));
    }
}

// =============================================================================
// getBeatsForNote() Function Tests (T007)
// =============================================================================

TEST_CASE("getBeatsForNote basic note values without modifier", "[note_value][US1]") {
    SECTION("Double whole note = 8 beats") {
        REQUIRE(getBeatsForNote(NoteValue::DoubleWhole) == 8.0f);
        REQUIRE(getBeatsForNote(NoteValue::DoubleWhole, NoteModifier::None) == 8.0f);
    }

    SECTION("Quarter note = 1 beat") {
        REQUIRE(getBeatsForNote(NoteValue::Quarter) == 1.0f);
        REQUIRE(getBeatsForNote(NoteValue::Quarter, NoteModifier::None) == 1.0f);
    }

    SECTION("Whole note = 4 beats") {
        REQUIRE(getBeatsForNote(NoteValue::Whole) == 4.0f);
    }

    SECTION("Half note = 2 beats") {
        REQUIRE(getBeatsForNote(NoteValue::Half) == 2.0f);
    }

    SECTION("Eighth note = 0.5 beats") {
        REQUIRE(getBeatsForNote(NoteValue::Eighth) == 0.5f);
    }

    SECTION("Sixteenth note = 0.25 beats") {
        REQUIRE(getBeatsForNote(NoteValue::Sixteenth) == 0.25f);
    }

    SECTION("Thirty-second note = 0.125 beats") {
        REQUIRE(getBeatsForNote(NoteValue::ThirtySecond) == 0.125f);
    }

    SECTION("Sixty-fourth note = 0.0625 beats") {
        REQUIRE(getBeatsForNote(NoteValue::SixtyFourth) == 0.0625f);
    }
}

TEST_CASE("getBeatsForNote with Dotted modifier", "[note_value][US1]") {
    SECTION("Dotted quarter = 1.5 beats") {
        REQUIRE(getBeatsForNote(NoteValue::Quarter, NoteModifier::Dotted) == 1.5f);
    }

    SECTION("Dotted half = 3 beats") {
        REQUIRE(getBeatsForNote(NoteValue::Half, NoteModifier::Dotted) == 3.0f);
    }

    SECTION("Dotted eighth = 0.75 beats") {
        REQUIRE(getBeatsForNote(NoteValue::Eighth, NoteModifier::Dotted) == 0.75f);
    }

    SECTION("Dotted whole = 6 beats") {
        REQUIRE(getBeatsForNote(NoteValue::Whole, NoteModifier::Dotted) == 6.0f);
    }

    SECTION("Dotted sixteenth = 0.375 beats") {
        REQUIRE(getBeatsForNote(NoteValue::Sixteenth, NoteModifier::Dotted) == 0.375f);
    }
}

TEST_CASE("getBeatsForNote with Triplet modifier", "[note_value][US1]") {
    SECTION("Triplet quarter = 2/3 beat") {
        float expected = 1.0f * (2.0f / 3.0f);
        REQUIRE(getBeatsForNote(NoteValue::Quarter, NoteModifier::Triplet) ==
                Approx(expected).margin(1e-6f));
    }

    SECTION("Triplet eighth = 1/3 beat") {
        float expected = 0.5f * (2.0f / 3.0f);
        REQUIRE(getBeatsForNote(NoteValue::Eighth, NoteModifier::Triplet) ==
                Approx(expected).margin(1e-6f));
    }

    SECTION("Triplet half = 4/3 beats") {
        float expected = 2.0f * (2.0f / 3.0f);
        REQUIRE(getBeatsForNote(NoteValue::Half, NoteModifier::Triplet) ==
                Approx(expected).margin(1e-6f));
    }

    SECTION("3 triplet quarters = 2 regular quarters") {
        float tripletQuarter = getBeatsForNote(NoteValue::Quarter, NoteModifier::Triplet);
        float threeTripletsTotal = tripletQuarter * 3.0f;
        REQUIRE(threeTripletsTotal == Approx(2.0f).margin(1e-6f));
    }

    SECTION("3 triplet eighths = 1 regular quarter") {
        float tripletEighth = getBeatsForNote(NoteValue::Eighth, NoteModifier::Triplet);
        float threeTripletsTotal = tripletEighth * 3.0f;
        REQUIRE(threeTripletsTotal == Approx(1.0f).margin(1e-6f));
    }
}

// =============================================================================
// Constexpr Tests (T007) - Verify compile-time evaluation
// =============================================================================

TEST_CASE("getBeatsForNote is constexpr", "[note_value][constexpr][US4]") {
    SECTION("Can be used in constexpr context") {
        constexpr float quarterBeats = getBeatsForNote(NoteValue::Quarter);
        REQUIRE(quarterBeats == 1.0f);
    }

    SECTION("Constexpr with modifier") {
        constexpr float dottedQuarter = getBeatsForNote(NoteValue::Quarter, NoteModifier::Dotted);
        REQUIRE(dottedQuarter == 1.5f);
    }

    SECTION("Constexpr array initialization") {
        constexpr std::array<float, 8> beats = {
            getBeatsForNote(NoteValue::DoubleWhole),
            getBeatsForNote(NoteValue::Whole),
            getBeatsForNote(NoteValue::Half),
            getBeatsForNote(NoteValue::Quarter),
            getBeatsForNote(NoteValue::Eighth),
            getBeatsForNote(NoteValue::Sixteenth),
            getBeatsForNote(NoteValue::ThirtySecond),
            getBeatsForNote(NoteValue::SixtyFourth)
        };

        REQUIRE(beats[0] == 8.0f);
        REQUIRE(beats[1] == 4.0f);
        REQUIRE(beats[2] == 2.0f);
        REQUIRE(beats[3] == 1.0f);
        REQUIRE(beats[4] == 0.5f);
        REQUIRE(beats[5] == 0.25f);
        REQUIRE(beats[6] == 0.125f);
        REQUIRE(beats[7] == 0.0625f);
    }

    SECTION("static_assert validation") {
        // These would fail at compile time if getBeatsForNote is not constexpr
        static_assert(getBeatsForNote(NoteValue::Quarter) == 1.0f);
        static_assert(getBeatsForNote(NoteValue::Half) == 2.0f);
        static_assert(getBeatsForNote(NoteValue::Quarter, NoteModifier::Dotted) == 1.5f);
        REQUIRE(true);  // If we got here, static_asserts passed
    }
}

TEST_CASE("kBeatsPerNote and kModifierMultiplier are constexpr", "[note_value][constexpr][US4]") {
    SECTION("kBeatsPerNote can be used at compile time") {
        constexpr float quarter = kBeatsPerNote[static_cast<size_t>(NoteValue::Quarter)];
        REQUIRE(quarter == 1.0f);
    }

    SECTION("kModifierMultiplier can be used at compile time") {
        constexpr float dotted = kModifierMultiplier[1];
        REQUIRE(dotted == 1.5f);
    }

    SECTION("Compile-time lookup table") {
        constexpr std::array<float, 3> modifiers = {
            kModifierMultiplier[0],
            kModifierMultiplier[1],
            kModifierMultiplier[2]
        };

        REQUIRE(modifiers[0] == 1.0f);
        REQUIRE(modifiers[1] == 1.5f);
        REQUIRE(modifiers[2] == Approx(2.0f / 3.0f).margin(1e-6f));
    }
}

// =============================================================================
// noexcept Tests - Verify real-time safety
// =============================================================================

TEST_CASE("getBeatsForNote is noexcept", "[note_value][realtime][US1]") {
    SECTION("Function is noexcept") {
        REQUIRE(noexcept(getBeatsForNote(NoteValue::Quarter)));
        REQUIRE(noexcept(getBeatsForNote(NoteValue::Quarter, NoteModifier::Dotted)));
        REQUIRE(noexcept(getBeatsForNote(NoteValue::Eighth, NoteModifier::Triplet)));
    }
}

// =============================================================================
// Practical Use Case Tests (From spec.md US1 acceptance scenarios)
// =============================================================================

TEST_CASE("Practical tempo sync calculations", "[note_value][US1]") {
    // These tests validate the NoteValue system works correctly for
    // the intended use case of tempo-synced delay times

    SECTION("Quarter note at 120 BPM") {
        // At 120 BPM, one beat = 0.5 seconds
        // Quarter note = 1 beat = 0.5 seconds
        float beatsPerSecond = 120.0f / 60.0f;  // 2 beats/sec
        float noteBeats = getBeatsForNote(NoteValue::Quarter);
        float durationSeconds = noteBeats / beatsPerSecond;

        REQUIRE(durationSeconds == Approx(0.5f).margin(1e-6f));
    }

    SECTION("Dotted eighth at 90 BPM") {
        // From spec.md US1 acceptance scenario 2:
        // dotted eighth = 0.75 beats
        // At 90 BPM: 0.75 beats * (60/90 sec/beat) = 0.5 seconds
        float beatsPerSecond = 90.0f / 60.0f;  // 1.5 beats/sec
        float noteBeats = getBeatsForNote(NoteValue::Eighth, NoteModifier::Dotted);
        float durationSeconds = noteBeats / beatsPerSecond;

        REQUIRE(noteBeats == 0.75f);
        REQUIRE(durationSeconds == Approx(0.5f).margin(1e-6f));
    }

    SECTION("Triplet quarter at 120 BPM") {
        // Triplet quarter = 2/3 beat
        // At 120 BPM: (2/3) beat * (0.5 sec/beat) = 1/3 second
        float beatsPerSecond = 120.0f / 60.0f;  // 2 beats/sec
        float noteBeats = getBeatsForNote(NoteValue::Quarter, NoteModifier::Triplet);
        float durationSeconds = noteBeats / beatsPerSecond;

        float expectedSeconds = (2.0f / 3.0f) / 2.0f;  // (2/3 beat) / (2 beats/sec)
        REQUIRE(durationSeconds == Approx(expectedSeconds).margin(1e-6f));
    }
}
