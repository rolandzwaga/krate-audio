// ==============================================================================
// Scale Harmonizer - Unit Tests
// ==============================================================================
// Layer 0: Core Utilities
// Constitution Principle VIII: Testing Discipline
// Constitution Principle XIII: Test-First Development
//
// Tests for: dsp/include/krate/dsp/core/scale_harmonizer.h
// Purpose: Verify diatonic interval computation for harmonizer effects
// Tags: [scale-harmonizer]
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <krate/dsp/core/scale_harmonizer.h>
#include <array>

using namespace Krate::DSP;

// =============================================================================
// US1: Construction and Getters (T006)
// =============================================================================

TEST_CASE("ScaleHarmonizer default construction is C Major",
          "[scale-harmonizer][us1]") {
    ScaleHarmonizer harm;
    REQUIRE(harm.getKey() == 0);
    REQUIRE(harm.getScale() == ScaleType::Major);
}

TEST_CASE("ScaleHarmonizer setKey/getKey round-trips correctly",
          "[scale-harmonizer][us1]") {
    ScaleHarmonizer harm;

    SECTION("valid keys 0-11") {
        for (int k = 0; k < 12; ++k) {
            harm.setKey(k);
            REQUIRE(harm.getKey() == k);
        }
    }

    SECTION("out-of-range keys are wrapped via modulo 12") {
        harm.setKey(12);
        REQUIRE(harm.getKey() == 0);

        harm.setKey(14);
        REQUIRE(harm.getKey() == 2);

        harm.setKey(23);
        REQUIRE(harm.getKey() == 11);
    }
}

TEST_CASE("ScaleHarmonizer setScale/getScale round-trips correctly",
          "[scale-harmonizer][us1]") {
    ScaleHarmonizer harm;

    harm.setScale(ScaleType::NaturalMinor);
    REQUIRE(harm.getScale() == ScaleType::NaturalMinor);

    harm.setScale(ScaleType::Lydian);
    REQUIRE(harm.getScale() == ScaleType::Lydian);

    harm.setScale(ScaleType::Chromatic);
    REQUIRE(harm.getScale() == ScaleType::Chromatic);
}

// =============================================================================
// US1: getScaleIntervals() static method (T007)
// =============================================================================

TEST_CASE("ScaleHarmonizer::getScaleIntervals returns correct values",
          "[scale-harmonizer][us1]") {
    // Spot-check Major
    {
        constexpr auto intervals = ScaleHarmonizer::getScaleIntervals(ScaleType::Major);
        constexpr std::array<int, 7> expected = {0, 2, 4, 5, 7, 9, 11};
        REQUIRE(intervals == expected);
    }

    // Spot-check NaturalMinor
    {
        constexpr auto intervals = ScaleHarmonizer::getScaleIntervals(ScaleType::NaturalMinor);
        constexpr std::array<int, 7> expected = {0, 2, 3, 5, 7, 8, 10};
        REQUIRE(intervals == expected);
    }

    // Verify it is callable as static constexpr
    {
        constexpr auto majorIntervals = ScaleHarmonizer::getScaleIntervals(ScaleType::Major);
        static_assert(majorIntervals[0] == 0, "Root must be 0");
        static_assert(majorIntervals[2] == 4, "Major 3rd must be 4 semitones");
    }
}

// =============================================================================
// US1: C Major 3rd above reference table (T008 / SC-001)
// =============================================================================

TEST_CASE("ScaleHarmonizer C Major 3rd above for all 7 scale degrees",
          "[scale-harmonizer][us1]") {
    ScaleHarmonizer harm;
    harm.setKey(0);  // C
    harm.setScale(ScaleType::Major);

    // Reference table from SC-001:
    // C->E(+4), D->F(+3), E->G(+3), F->A(+4), G->B(+4), A->C(+3), B->D(+3)

    // C Major scale notes in octave 4 (MIDI 60-71):
    // C=60, D=62, E=64, F=65, G=67, A=69, B=71

    struct TestCase {
        int inputMidi;
        int expectedSemitones;
        int expectedTargetNote;
        int expectedScaleDegree;
        const char* name;
    };

    const std::array<TestCase, 7> cases = {{
        {60, +4, 64, 2, "C4->E4"},   // C -> E (major 3rd)
        {62, +3, 65, 3, "D4->F4"},   // D -> F (minor 3rd)
        {64, +3, 67, 4, "E4->G4"},   // E -> G (minor 3rd)
        {65, +4, 69, 5, "F4->A4"},   // F -> A (major 3rd)
        {67, +4, 71, 6, "G4->B4"},   // G -> B (major 3rd)
        {69, +3, 72, 0, "A4->C5"},   // A -> C (minor 3rd, crosses octave)
        {71, +3, 74, 1, "B4->D5"},   // B -> D (minor 3rd, crosses octave)
    }};

    for (const auto& tc : cases) {
        INFO("Case: " << tc.name << " (MIDI " << tc.inputMidi << ")");
        auto result = harm.calculate(tc.inputMidi, +2);  // 3rd above = +2 diatonic steps
        CHECK(result.semitones == tc.expectedSemitones);
        CHECK(result.targetNote == tc.expectedTargetNote);
        CHECK(result.scaleDegree == tc.expectedScaleDegree);
    }
}

// =============================================================================
// US1: Multi-scale exhaustive tests (T009 / SC-002)
// =============================================================================

TEST_CASE("ScaleHarmonizer exhaustive multi-scale/multi-key correctness",
          "[scale-harmonizer][us1]") {
    // 8 scales x 12 keys x 4 intervals x 7 degrees = 2688 test cases
    //
    // For each scale, we get the interval table, then for each root key and
    // each input degree, we compute the expected target for:
    //   2nd (+1 step), 3rd (+2 steps), 5th (+4 steps), octave (+7 steps)
    //
    // Expected semitone shift =
    //   scaleIntervals[(degree + steps) % 7] - scaleIntervals[degree]
    //   + octaveAdjustment
    // where octaveAdjustment = 12 if (degree + steps) >= 7, etc.

    constexpr std::array<ScaleType, 8> scales = {
        ScaleType::Major,
        ScaleType::NaturalMinor,
        ScaleType::HarmonicMinor,
        ScaleType::MelodicMinor,
        ScaleType::Dorian,
        ScaleType::Mixolydian,
        ScaleType::Phrygian,
        ScaleType::Lydian,
    };

    constexpr std::array<int, 4> diatonicSteps = {1, 2, 4, 7};  // 2nd, 3rd, 5th, octave

    int totalCases = 0;

    for (auto scaleType : scales) {
        auto intervals = ScaleHarmonizer::getScaleIntervals(scaleType);

        for (int rootKey = 0; rootKey < 12; ++rootKey) {
            ScaleHarmonizer harm;
            harm.setKey(rootKey);
            harm.setScale(scaleType);

            for (int steps : diatonicSteps) {
                for (int degree = 0; degree < 7; ++degree) {
                    // Input MIDI note = root in octave 4 + scale offset for this degree
                    int inputMidi = 60 + rootKey + intervals[static_cast<size_t>(degree)];

                    // Compute expected target
                    int totalSteps = degree + steps;
                    int octaves = totalSteps / 7;
                    int targetDegree = totalSteps % 7;

                    int expectedSemitones =
                        intervals[static_cast<size_t>(targetDegree)]
                        - intervals[static_cast<size_t>(degree)]
                        + octaves * 12;

                    int expectedTargetNote = inputMidi + expectedSemitones;

                    auto result = harm.calculate(inputMidi, steps);

                    INFO("Scale=" << static_cast<int>(scaleType)
                         << " Key=" << rootKey
                         << " Steps=" << steps
                         << " Degree=" << degree
                         << " Input=" << inputMidi);

                    CHECK(result.semitones == expectedSemitones);
                    CHECK(result.targetNote == expectedTargetNote);
                    CHECK(result.scaleDegree == targetDegree);
                    CHECK(result.octaveOffset == octaves);

                    ++totalCases;
                }
            }
        }
    }

    // Verify we actually tested 2688 cases
    REQUIRE(totalCases == 2688);
}

// =============================================================================
// US1: Octave wrapping tests (T010)
// =============================================================================

TEST_CASE("ScaleHarmonizer octave wrapping: 7th above and octave crossing",
          "[scale-harmonizer][us1]") {
    ScaleHarmonizer harm;
    harm.setKey(0);  // C
    harm.setScale(ScaleType::Major);

    SECTION("7th above (diatonicSteps=+6) from C4") {
        // C4 (60) + 7th above = B4 (71) = +11 semitones
        auto result = harm.calculate(60, +6);
        CHECK(result.semitones == 11);
        CHECK(result.targetNote == 71);
        CHECK(result.scaleDegree == 6);
        CHECK(result.octaveOffset == 0);  // stays within same octave
    }

    SECTION("Octave (diatonicSteps=+7) from C4") {
        // C4 (60) + octave = C5 (72) = +12 semitones
        auto result = harm.calculate(60, +7);
        CHECK(result.semitones == 12);
        CHECK(result.targetNote == 72);
        CHECK(result.scaleDegree == 0);
        CHECK(result.octaveOffset == 1);
    }

    SECTION("9th (diatonicSteps=+8) from C4") {
        // C4 (60) + 9th = D5 (74) = +14 semitones
        auto result = harm.calculate(60, +8);
        CHECK(result.semitones == 14);
        CHECK(result.targetNote == 74);
        CHECK(result.scaleDegree == 1);
        CHECK(result.octaveOffset == 1);
    }

    SECTION("10th (diatonicSteps=+9) from C4") {
        // C4 (60) + 10th = E5 (76) = +16 semitones
        auto result = harm.calculate(60, +9);
        CHECK(result.semitones == 16);
        CHECK(result.targetNote == 76);
        CHECK(result.scaleDegree == 2);
        CHECK(result.octaveOffset == 1);
    }

    SECTION("Two octaves (diatonicSteps=+14) from C4") {
        // C4 (60) + two octaves = C6 (84) = +24 semitones
        auto result = harm.calculate(60, +14);
        CHECK(result.semitones == 24);
        CHECK(result.targetNote == 84);
        CHECK(result.scaleDegree == 0);
        CHECK(result.octaveOffset == 2);
    }

    SECTION("7th above from A4 crosses octave boundary") {
        // A4 (69) + 7th = G5 (79) = +10 semitones
        // A is degree 5, +6 steps = degree 11 = 7+4 => octave 1, degree 4 (G)
        // shift = intervals[4] - intervals[5] + 12 = 7 - 9 + 12 = 10
        auto result = harm.calculate(69, +6);
        CHECK(result.semitones == 10);
        CHECK(result.targetNote == 79);
        CHECK(result.scaleDegree == 4);
        CHECK(result.octaveOffset == 1);
    }

    SECTION("Unison (diatonicSteps=0)") {
        auto result = harm.calculate(60, 0);
        CHECK(result.semitones == 0);
        CHECK(result.targetNote == 60);
        CHECK(result.octaveOffset == 0);
    }
}
