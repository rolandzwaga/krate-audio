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
//
// ==============================================================================
// FR-015 Thread Safety Verification (by code inspection)
// ==============================================================================
// All query methods (calculate(), getScaleDegree(), quantizeToScale(),
// getSemitoneShift(), getScaleIntervals(), getKey(), getScale()) are safe
// for concurrent reads because:
//
//  1. All write operations are setKey() and setScale(), which the host
//     guarantees are NOT called during process() (parameter changes are
//     serialized before the audio callback).
//
//  2. All query methods are marked `const` and modify no shared state.
//     They read rootNote_ and scale_ but never write to them.
//
//  3. No `mutable` keyword, no lazy caches, no static locals, and no
//     computed fields exist in the ScaleHarmonizer class.  The only
//     mutable data is the constexpr lookup tables in namespace detail,
//     which are compile-time constants and inherently thread-safe.
//
// Therefore, after configuration via setKey()/setScale(), the object is
// effectively immutable and safe for concurrent reads from the audio
// thread without any synchronization.
//
// ==============================================================================
// FR-016 Layer 0 Dependency Rule Verification (by code inspection)
// ==============================================================================
// The #include directives in scale_harmonizer.h are:
//
//   Standard library (Layer 0 allowed):
//     <algorithm>   -- std::clamp
//     <array>       -- std::array for lookup tables
//     <cmath>       -- std::round (in getSemitoneShift)
//     <cstdint>     -- uint8_t for ScaleType underlying type
//
//   Layer 0 headers:
//     <krate/dsp/core/midi_utils.h>   -- kMinMidiNote, kMaxMidiNote constants
//     <krate/dsp/core/pitch_utils.h>  -- frequencyToMidiNote() for getSemitoneShift()
//
// No Layer 1+ headers (primitives/, processors/, systems/, effects/) are
// included.  This satisfies the Layer 0 dependency rule: only stdlib and
// other Layer 0 utilities.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <krate/dsp/core/scale_harmonizer.h>
#include <array>
#include <type_traits>

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

// =============================================================================
// US2: Non-Scale Note Handling (T022 / SC-003)
// =============================================================================

TEST_CASE("ScaleHarmonizer non-scale notes use nearest scale degree",
          "[scale-harmonizer][us2]") {
    ScaleHarmonizer harm;
    harm.setKey(0);  // C
    harm.setScale(ScaleType::Major);

    SECTION("C#4 (MIDI 61) 3rd above returns same shift as C4 (+4 semitones)") {
        // C#4 is not in C Major. Nearest degree = C (degree 0), round down on tie.
        // 3rd above from degree 0 => degree 2 (E), shift = 4 semitones
        auto resultC = harm.calculate(60, +2);   // C4 reference
        auto resultCs = harm.calculate(61, +2);  // C#4 non-scale
        CHECK(resultC.semitones == +4);
        CHECK(resultCs.semitones == +4);
    }

    SECTION("Eb4 (MIDI 63) 3rd above uses nearest degree's interval") {
        // Eb4 (offset 3 from C root). In Major: equidistant from D(2) and E(4).
        // Tie-break: round down to D (degree 1).
        // 3rd above from degree 1 => degree 3 (F), shift = 5-2 = 3 semitones
        auto resultEb = harm.calculate(63, +2);
        CHECK(resultEb.semitones == +3);
        CHECK(resultEb.scaleDegree == 3);  // target is F
    }

    SECTION("All 5 chromatic passing tones in C Major use nearest scale degree") {
        // The 5 chromatic passing tones in C Major (pitch classes not in scale):
        // C#/Db (pc 1), D#/Eb (pc 3), F#/Gb (pc 6), G#/Ab (pc 8), A#/Bb (pc 10)
        //
        // Each should resolve to the LOWER neighbor degree (round-down tie-break).
        // We verify 3rd above (+2 steps) produces the same shift as the resolved
        // scale degree's note.

        struct PassingToneCase {
            int midiNote;         // chromatic passing tone (octave 4)
            int resolvedDegree;   // nearest scale degree (round down on tie)
            int resolvedMidi;     // MIDI note of the resolved degree in same octave
            const char* name;
        };

        // C Major scale in octave 4: C=60, D=62, E=64, F=65, G=67, A=69, B=71
        const std::array<PassingToneCase, 5> cases = {{
            {61, 0, 60, "C#4 -> C (degree 0)"},   // offset 1: equidistant C/D, round down
            {63, 1, 62, "Eb4 -> D (degree 1)"},    // offset 3: equidistant D/E, round down
            {66, 3, 65, "F#4 -> F (degree 3)"},    // offset 6: equidistant F/G, round down
            {68, 4, 67, "G#4 -> G (degree 4)"},    // offset 8: equidistant G/A, round down
            {70, 5, 69, "A#4 -> A (degree 5)"},    // offset 10: equidistant A/B, round down
        }};

        for (const auto& tc : cases) {
            INFO("Passing tone: " << tc.name << " (MIDI " << tc.midiNote << ")");

            // Calculate 3rd above for the passing tone
            auto passingResult = harm.calculate(tc.midiNote, +2);

            // Calculate 3rd above for the resolved scale degree note
            auto degreeResult = harm.calculate(tc.resolvedMidi, +2);

            // Both must produce the same semitone shift
            CHECK(passingResult.semitones == degreeResult.semitones);

            // The target scale degree should match
            CHECK(passingResult.scaleDegree == degreeResult.scaleDegree);
        }
    }
}

// =============================================================================
// US2: Tie-Breaking Rule (T023 / FR-004)
// =============================================================================

TEST_CASE("ScaleHarmonizer tie-breaking: equidistant non-scale notes round DOWN",
          "[scale-harmonizer][us2]") {
    ScaleHarmonizer harm;
    harm.setKey(0);  // C
    harm.setScale(ScaleType::Major);

    SECTION("C# in C Major: equidistant from C and D, rounds to C") {
        // C# (MIDI 61): 1 semitone from C (degree 0), 1 semitone from D (degree 1)
        // FR-004: round DOWN = prefer lower degree = C (degree 0)
        // 3rd above from C (degree 0) = E (degree 2), shift = +4
        auto result = harm.calculate(61, +2);
        CHECK(result.semitones == +4);     // same as C -> E
        CHECK(result.scaleDegree == 2);    // target degree = E
    }

    SECTION("F# in C Major: equidistant from F and G, rounds to F") {
        // F# (MIDI 66): 1 semitone from F (degree 3), 1 semitone from G (degree 4)
        // FR-004: round DOWN = prefer lower degree = F (degree 3)
        // 3rd above from F (degree 3) = A (degree 5), shift = +4
        auto result = harm.calculate(66, +2);
        CHECK(result.semitones == +4);     // same as F -> A
        CHECK(result.scaleDegree == 5);    // target degree = A
    }

    SECTION("G# in C Major: equidistant from G and A, rounds to G") {
        // G# (MIDI 68): 1 semitone from G (degree 4), 1 semitone from A (degree 5)
        // FR-004: round DOWN = prefer lower degree = G (degree 4)
        // 3rd above from G (degree 4) = B (degree 6), shift = +4
        auto result = harm.calculate(68, +2);
        CHECK(result.semitones == +4);     // same as G -> B
        CHECK(result.scaleDegree == 6);    // target degree = B
    }

    SECTION("A# in C Major: equidistant from A and B, rounds to A") {
        // A# (MIDI 70): 1 semitone from A (degree 5), 1 semitone from B (degree 6)
        // FR-004: round DOWN = prefer lower degree = A (degree 5)
        // 3rd above from A (degree 5) = C (degree 0), shift = +3
        auto result = harm.calculate(70, +2);
        CHECK(result.semitones == +3);     // same as A -> C
        CHECK(result.scaleDegree == 0);    // target degree = C (wraps)
    }

    SECTION("Eb in C Major: equidistant from D and E, rounds to D") {
        // Eb (MIDI 63): 1 semitone from D (degree 1), 1 semitone from E (degree 2)
        // FR-004: round DOWN = prefer lower degree = D (degree 1)
        // 3rd above from D (degree 1) = F (degree 3), shift = +3
        auto result = harm.calculate(63, +2);
        CHECK(result.semitones == +3);     // same as D -> F
        CHECK(result.scaleDegree == 3);    // target degree = F
    }

    SECTION("Non-equidistant: Ab in C Major is closer to G than A") {
        // Ab (MIDI 68) offset=8: |8-7|=1 (G), |8-9|=1 (A) -> actually equidistant
        // This is already covered above. Let's test a truly non-equidistant case:
        // Bb (MIDI 70) offset=10: |10-9|=1 (A), |10-11|=1 (B) -> equidistant, also above
        // For a non-equidistant case, none exist in C Major with unique distance.
        // All 5 chromatic tones in C Major happen to be equidistant.
        // Instead, let's use a different scale where non-equidistant cases exist.

        // Phrygian: {0, 1, 3, 5, 7, 8, 10}
        // Offset 2 (from root): |2-1|=1 (degree 1), |2-3|=1 (degree 2) -> equidistant
        // Offset 4: |4-3|=1 (degree 2), |4-5|=1 (degree 3) -> equidistant
        // Offset 6: |6-5|=1 (degree 3), |6-7|=1 (degree 4) -> equidistant
        // Offset 9: |9-8|=1 (degree 5), |9-10|=1 (degree 6) -> equidistant
        // Offset 11: |11-10|=1 (degree 6), |11-0|=1 (degree 0) -> equidistant

        // Actually for Harmonic Minor: {0, 2, 3, 5, 7, 8, 11}
        // Offset 9: |9-8|=1 (degree 5), |9-11|=2 (degree 6) -> NOT equidistant, resolves to degree 5
        // Offset 10: |10-8|=2 (degree 5), |10-11|=1 (degree 6) -> NOT equidistant, resolves to degree 6
        harm.setScale(ScaleType::HarmonicMinor);

        // C Harmonic Minor: C=60, D=62, Eb=63, F=65, G=67, Ab=68, B=71
        // Test offset 9 from root (MIDI 69 = A): closer to Ab (degree 5) than B (degree 6)
        auto result = harm.calculate(69, +2);  // A4, 3rd above
        // Resolves to Ab (degree 5). 3rd above from degree 5 = degree 7 -> degree 0 + 1 octave
        // shift = intervals[0] - intervals[5] + 12 = 0 - 8 + 12 = +4
        CHECK(result.semitones == +4);
        CHECK(result.scaleDegree == 0);
    }
}

// =============================================================================
// US3: Chromatic (Fixed Shift) Mode (T030 / FR-003 / SC-005)
// =============================================================================

TEST_CASE("ScaleHarmonizer Chromatic mode returns diatonicSteps as raw semitones",
          "[scale-harmonizer][us3]") {
    ScaleHarmonizer harm;
    harm.setScale(ScaleType::Chromatic);

    SECTION("diatonicSteps=+7 always returns +7 semitones for any input note") {
        // Test across multiple octaves and notes
        const std::array<int, 7> testNotes = {36, 48, 60, 64, 69, 72, 100};
        for (int note : testNotes) {
            INFO("Input MIDI note: " << note);
            auto result = harm.calculate(note, +7);
            CHECK(result.semitones == +7);
            CHECK(result.targetNote == note + 7);
        }
    }

    SECTION("diatonicSteps=-5 always returns -5 semitones for any input note") {
        const std::array<int, 7> testNotes = {36, 48, 60, 64, 69, 72, 100};
        for (int note : testNotes) {
            INFO("Input MIDI note: " << note);
            auto result = harm.calculate(note, -5);
            CHECK(result.semitones == -5);
            CHECK(result.targetNote == note - 5);
        }
    }

    SECTION("Key setting has no effect on Chromatic mode result") {
        // Chromatic mode ignores key entirely
        for (int key = 0; key < 12; ++key) {
            harm.setKey(key);
            INFO("Key: " << key);

            auto resultUp = harm.calculate(60, +7);
            CHECK(resultUp.semitones == +7);
            CHECK(resultUp.targetNote == 67);

            auto resultDown = harm.calculate(60, -5);
            CHECK(resultDown.semitones == -5);
            CHECK(resultDown.targetNote == 55);
        }
    }

    SECTION("Various diatonic step values produce exact semitone match") {
        // In Chromatic mode, diatonicSteps IS the semitone shift
        harm.setKey(0);  // key should be irrelevant
        const int input = 60;  // C4

        for (int steps = -12; steps <= 12; ++steps) {
            INFO("diatonicSteps: " << steps);
            auto result = harm.calculate(input, steps);
            CHECK(result.semitones == steps);
            CHECK(result.targetNote == input + steps);
        }
    }
}

// =============================================================================
// US3: Chromatic mode scaleDegree is always -1 (T031 / FR-003)
// =============================================================================

TEST_CASE("ScaleHarmonizer Chromatic mode scaleDegree is always -1",
          "[scale-harmonizer][us3]") {
    ScaleHarmonizer harm;
    harm.setScale(ScaleType::Chromatic);

    SECTION("scaleDegree is -1 for positive intervals") {
        auto result = harm.calculate(60, +7);
        CHECK(result.scaleDegree == -1);
    }

    SECTION("scaleDegree is -1 for negative intervals") {
        auto result = harm.calculate(60, -5);
        CHECK(result.scaleDegree == -1);
    }

    SECTION("scaleDegree is -1 for unison") {
        auto result = harm.calculate(60, 0);
        CHECK(result.scaleDegree == -1);
    }

    SECTION("scaleDegree is -1 for all tested notes and intervals") {
        for (int note = 24; note <= 96; note += 12) {
            for (int steps = -7; steps <= 7; ++steps) {
                INFO("MIDI note: " << note << " steps: " << steps);
                auto result = harm.calculate(note, steps);
                CHECK(result.scaleDegree == -1);
            }
        }
    }

    SECTION("octaveOffset is always 0 in Chromatic mode") {
        // Chromatic mode is passthrough -- no diatonic octave concept
        for (int steps = -12; steps <= 12; ++steps) {
            INFO("diatonicSteps: " << steps);
            auto result = harm.calculate(60, steps);
            CHECK(result.octaveOffset == 0);
        }
    }
}

// =============================================================================
// US4: getScaleDegree() (T038 / FR-010)
// =============================================================================

TEST_CASE("ScaleHarmonizer getScaleDegree returns correct degree for scale notes",
          "[scale-harmonizer][us4]") {
    ScaleHarmonizer harm;
    harm.setKey(0);  // C
    harm.setScale(ScaleType::Major);

    SECTION("C4 (MIDI 60) is degree 0 (root)") {
        CHECK(harm.getScaleDegree(60) == 0);
    }

    SECTION("D4 (MIDI 62) is degree 1") {
        CHECK(harm.getScaleDegree(62) == 1);
    }

    SECTION("E4 (MIDI 64) is degree 2") {
        CHECK(harm.getScaleDegree(64) == 2);
    }

    SECTION("F4 (MIDI 65) is degree 3") {
        CHECK(harm.getScaleDegree(65) == 3);
    }

    SECTION("G4 (MIDI 67) is degree 4") {
        CHECK(harm.getScaleDegree(67) == 4);
    }

    SECTION("A4 (MIDI 69) is degree 5") {
        CHECK(harm.getScaleDegree(69) == 5);
    }

    SECTION("B4 (MIDI 71) is degree 6") {
        CHECK(harm.getScaleDegree(71) == 6);
    }

    SECTION("C#4 (MIDI 61) is NOT in C Major, returns -1") {
        CHECK(harm.getScaleDegree(61) == -1);
    }

    SECTION("All 12 pitch classes for C Major membership") {
        // C Major scale notes: C(0), D(2), E(4), F(5), G(7), A(9), B(11)
        // Expected degree:      0     1     2     3     4     5     6
        // Non-scale notes: C#(1), D#(3), F#(6), G#(8), A#(10) -> -1

        constexpr std::array<int, 12> expectedDegrees = {
            0,   // C  (pitch class 0) -> degree 0
            -1,  // C# (pitch class 1) -> not in scale
            1,   // D  (pitch class 2) -> degree 1
            -1,  // D# (pitch class 3) -> not in scale
            2,   // E  (pitch class 4) -> degree 2
            3,   // F  (pitch class 5) -> degree 3
            -1,  // F# (pitch class 6) -> not in scale
            4,   // G  (pitch class 7) -> degree 4
            -1,  // G# (pitch class 8) -> not in scale
            5,   // A  (pitch class 9) -> degree 5
            -1,  // A# (pitch class 10) -> not in scale
            6,   // B  (pitch class 11) -> degree 6
        };

        for (int pc = 0; pc < 12; ++pc) {
            int midiNote = 60 + pc;  // octave 4
            INFO("Pitch class " << pc << " (MIDI " << midiNote << ")");
            CHECK(harm.getScaleDegree(midiNote) == expectedDegrees[static_cast<size_t>(pc)]);
        }
    }

    SECTION("Same pitch class in different octaves returns same degree") {
        // C in octave 3 (MIDI 48), octave 4 (60), octave 5 (72)
        CHECK(harm.getScaleDegree(48) == 0);
        CHECK(harm.getScaleDegree(60) == 0);
        CHECK(harm.getScaleDegree(72) == 0);

        // D in different octaves
        CHECK(harm.getScaleDegree(50) == 1);
        CHECK(harm.getScaleDegree(62) == 1);
        CHECK(harm.getScaleDegree(74) == 1);
    }

    SECTION("Chromatic mode always returns -1") {
        harm.setScale(ScaleType::Chromatic);
        for (int note = 48; note <= 72; ++note) {
            INFO("MIDI note: " << note);
            CHECK(harm.getScaleDegree(note) == -1);
        }
    }

    SECTION("Non-C root key: G Major") {
        harm.setKey(7);  // G
        harm.setScale(ScaleType::Major);
        // G Major: G(7), A(9), B(11), C(0), D(2), E(4), F#(6)
        // G4=67, A4=69, B4=71, C5=72, D5=74, E5=76, F#5=78

        CHECK(harm.getScaleDegree(67) == 0);   // G -> degree 0
        CHECK(harm.getScaleDegree(69) == 1);   // A -> degree 1
        CHECK(harm.getScaleDegree(71) == 2);   // B -> degree 2
        CHECK(harm.getScaleDegree(72) == 3);   // C -> degree 3
        CHECK(harm.getScaleDegree(74) == 4);   // D -> degree 4
        CHECK(harm.getScaleDegree(76) == 5);   // E -> degree 5
        CHECK(harm.getScaleDegree(78) == 6);   // F# -> degree 6

        // F natural (MIDI 77) is NOT in G Major
        CHECK(harm.getScaleDegree(77) == -1);
    }
}

// =============================================================================
// US4: quantizeToScale() (T039 / FR-011)
// =============================================================================

TEST_CASE("ScaleHarmonizer quantizeToScale snaps to nearest scale note",
          "[scale-harmonizer][us4]") {
    ScaleHarmonizer harm;
    harm.setKey(0);  // C
    harm.setScale(ScaleType::Major);

    SECTION("C#4 (MIDI 61) quantizes to C4 (60) by round-down tie rule") {
        // C# is equidistant from C and D (1 semitone each).
        // Round-down tie rule -> snap to C (lower note).
        CHECK(harm.quantizeToScale(61) == 60);
    }

    SECTION("Notes already in the scale return unchanged") {
        // C Major scale in octave 4: C=60, D=62, E=64, F=65, G=67, A=69, B=71
        CHECK(harm.quantizeToScale(60) == 60);
        CHECK(harm.quantizeToScale(62) == 62);
        CHECK(harm.quantizeToScale(64) == 64);
        CHECK(harm.quantizeToScale(65) == 65);
        CHECK(harm.quantizeToScale(67) == 67);
        CHECK(harm.quantizeToScale(69) == 69);
        CHECK(harm.quantizeToScale(71) == 71);
    }

    SECTION("All chromatic passing tones in C Major quantize correctly") {
        // C Major: C(0), D(2), E(4), F(5), G(7), A(9), B(11)
        // All 5 non-scale chromatic notes are equidistant from two scale notes,
        // so they all round down.

        // C#4 (61): equidistant C/D -> C (60)
        CHECK(harm.quantizeToScale(61) == 60);

        // Eb4 (63): equidistant D/E -> D (62)
        CHECK(harm.quantizeToScale(63) == 62);

        // F#4 (66): equidistant F/G -> F (65)
        CHECK(harm.quantizeToScale(66) == 65);

        // G#4 (68): equidistant G/A -> G (67)
        CHECK(harm.quantizeToScale(68) == 67);

        // A#4 (70): equidistant A/B -> A (69)
        CHECK(harm.quantizeToScale(70) == 69);
    }

    SECTION("Chromatic mode returns input unchanged") {
        harm.setScale(ScaleType::Chromatic);
        for (int note = 48; note <= 72; ++note) {
            INFO("MIDI note: " << note);
            CHECK(harm.quantizeToScale(note) == note);
        }
    }

    SECTION("Quantization in different octaves") {
        // C#3 (49) should quantize to C3 (48)
        CHECK(harm.quantizeToScale(49) == 48);

        // C#5 (73) should quantize to C5 (72)
        CHECK(harm.quantizeToScale(73) == 72);

        // F#2 (42) should quantize to F2 (41)
        CHECK(harm.quantizeToScale(42) == 41);
    }

    SECTION("Quantization with non-C root key: D Major") {
        harm.setKey(2);  // D
        harm.setScale(ScaleType::Major);
        // D Major: D(2), E(4), F#(6), G(7), A(9), B(11), C#(1)
        // D4=62, E4=64, F#4=66, G4=67, A4=69, B4=71, C#5=73

        // Eb4 (63): offset from D = 1 semitone. C#(offset 11) or D(offset 0)?
        // offset 1: closest to D (offset 0, dist 1) vs E (offset 2, dist 1) -> tie, round down to D
        CHECK(harm.quantizeToScale(63) == 62);  // snaps to D4

        // F4 (65): offset from D = 3. Closest to E(offset 2, dist 1) vs F#(offset 4, dist 1) -> tie, round down to E
        CHECK(harm.quantizeToScale(65) == 64);  // snaps to E4

        // Notes in scale should be unchanged
        CHECK(harm.quantizeToScale(62) == 62);  // D4
        CHECK(harm.quantizeToScale(66) == 66);  // F#4
        CHECK(harm.quantizeToScale(67) == 67);  // G4
    }

    SECTION("Quantization with Harmonic Minor (non-equidistant gaps)") {
        harm.setKey(0);  // C
        harm.setScale(ScaleType::HarmonicMinor);
        // C Harmonic Minor: C(0), D(2), Eb(3), F(5), G(7), Ab(8), B(11)
        // Note the augmented 2nd gap between Ab(8) and B(11)

        // A4 (MIDI 69): offset 9 from C root.
        // |9-8|=1 (Ab, degree 5), |9-11|=2 (B, degree 6) -> closer to Ab
        CHECK(harm.quantizeToScale(69) == 68);  // snaps to Ab4 (68)

        // Bb4 (MIDI 70): offset 10 from C root.
        // |10-8|=2 (Ab, degree 5), |10-11|=1 (B, degree 6) -> closer to B
        CHECK(harm.quantizeToScale(70) == 71);  // snaps to B4 (71)
    }
}

// =============================================================================
// US5: Exhaustive Scale Interval Truth-Table Tests (T047 / FR-002 / FR-013)
// =============================================================================

TEST_CASE("ScaleHarmonizer getScaleIntervals exhaustive truth table for all 8 diatonic scales",
          "[scale-harmonizer][us5]") {
    // Verify the exact semitone offsets for every diatonic scale type per FR-002

    SECTION("Major (Ionian) = {0, 2, 4, 5, 7, 9, 11}") {
        constexpr auto intervals = ScaleHarmonizer::getScaleIntervals(ScaleType::Major);
        constexpr std::array<int, 7> expected = {0, 2, 4, 5, 7, 9, 11};
        REQUIRE(intervals == expected);
    }

    SECTION("NaturalMinor (Aeolian) = {0, 2, 3, 5, 7, 8, 10}") {
        constexpr auto intervals = ScaleHarmonizer::getScaleIntervals(ScaleType::NaturalMinor);
        constexpr std::array<int, 7> expected = {0, 2, 3, 5, 7, 8, 10};
        REQUIRE(intervals == expected);
    }

    SECTION("HarmonicMinor = {0, 2, 3, 5, 7, 8, 11}") {
        constexpr auto intervals = ScaleHarmonizer::getScaleIntervals(ScaleType::HarmonicMinor);
        constexpr std::array<int, 7> expected = {0, 2, 3, 5, 7, 8, 11};
        REQUIRE(intervals == expected);
    }

    SECTION("MelodicMinor (ascending) = {0, 2, 3, 5, 7, 9, 11}") {
        constexpr auto intervals = ScaleHarmonizer::getScaleIntervals(ScaleType::MelodicMinor);
        constexpr std::array<int, 7> expected = {0, 2, 3, 5, 7, 9, 11};
        REQUIRE(intervals == expected);
    }

    SECTION("Dorian = {0, 2, 3, 5, 7, 9, 10}") {
        constexpr auto intervals = ScaleHarmonizer::getScaleIntervals(ScaleType::Dorian);
        constexpr std::array<int, 7> expected = {0, 2, 3, 5, 7, 9, 10};
        REQUIRE(intervals == expected);
    }

    SECTION("Mixolydian = {0, 2, 4, 5, 7, 9, 10}") {
        constexpr auto intervals = ScaleHarmonizer::getScaleIntervals(ScaleType::Mixolydian);
        constexpr std::array<int, 7> expected = {0, 2, 4, 5, 7, 9, 10};
        REQUIRE(intervals == expected);
    }

    SECTION("Phrygian = {0, 1, 3, 5, 7, 8, 10}") {
        constexpr auto intervals = ScaleHarmonizer::getScaleIntervals(ScaleType::Phrygian);
        constexpr std::array<int, 7> expected = {0, 1, 3, 5, 7, 8, 10};
        REQUIRE(intervals == expected);
    }

    SECTION("Lydian = {0, 2, 4, 6, 7, 9, 11}") {
        constexpr auto intervals = ScaleHarmonizer::getScaleIntervals(ScaleType::Lydian);
        constexpr std::array<int, 7> expected = {0, 2, 4, 6, 7, 9, 11};
        REQUIRE(intervals == expected);
    }

    SECTION("Chromatic returns {0, 1, 2, 3, 4, 5, 6} per FR-013") {
        constexpr auto intervals = ScaleHarmonizer::getScaleIntervals(ScaleType::Chromatic);
        constexpr std::array<int, 7> expected = {0, 1, 2, 3, 4, 5, 6};
        REQUIRE(intervals == expected);
    }
}

// =============================================================================
// US5: Cross-Key Correctness Test - Dorian (T048)
// =============================================================================

TEST_CASE("ScaleHarmonizer Dorian 3rd-above for all 7 degrees in all 12 root keys",
          "[scale-harmonizer][us5]") {
    // Dorian scale: {0, 2, 3, 5, 7, 9, 10}
    // 3rd above = diatonicSteps = +2
    //
    // For each root key and each scale degree, compute the expected 3rd-above
    // interval from the Dorian interval table and verify calculate() matches.

    constexpr auto dorianIntervals = ScaleHarmonizer::getScaleIntervals(ScaleType::Dorian);

    for (int rootKey = 0; rootKey < 12; ++rootKey) {
        ScaleHarmonizer harm;
        harm.setKey(rootKey);
        harm.setScale(ScaleType::Dorian);

        for (int degree = 0; degree < 7; ++degree) {
            // Input MIDI note: root in octave 4 + scale offset for this degree
            int inputMidi = 60 + rootKey + dorianIntervals[static_cast<size_t>(degree)];

            // Target degree for 3rd above
            int targetDegree = (degree + 2) % 7;
            int octaves = (degree + 2) / 7;

            // Expected semitone shift
            int expectedSemitones =
                dorianIntervals[static_cast<size_t>(targetDegree)]
                - dorianIntervals[static_cast<size_t>(degree)]
                + octaves * 12;

            auto result = harm.calculate(inputMidi, +2);

            INFO("Root=" << rootKey
                 << " Degree=" << degree
                 << " Input=" << inputMidi
                 << " Expected shift=" << expectedSemitones);

            CHECK(result.semitones == expectedSemitones);
            CHECK(result.targetNote == inputMidi + expectedSemitones);
            CHECK(result.scaleDegree == targetDegree);
            CHECK(result.octaveOffset == octaves);
        }
    }

    // Spot-check D Dorian manually (D=2 as root key)
    // D Dorian: D(2), E(4), F(5), G(7), A(9), B(11), C(12/0)
    // Semitone offsets from D: {0, 2, 3, 5, 7, 9, 10}
    //
    // 3rd above for each degree:
    //   D -> F:  3 semitones (minor 3rd)    degree 0 -> degree 2
    //   E -> G:  3 semitones (minor 3rd)    degree 1 -> degree 3
    //   F -> A:  4 semitones (major 3rd)    degree 2 -> degree 4
    //   G -> B:  4 semitones (major 3rd)    degree 3 -> degree 5
    //   A -> C:  3 semitones (minor 3rd)    degree 4 -> degree 6
    //   B -> D:  3 semitones (minor 3rd)    degree 5 -> degree 0 (wraps, +1 octave)
    //   C -> E:  4 semitones (major 3rd)    degree 6 -> degree 1 (wraps, +1 octave)

    SECTION("D Dorian manual spot-check") {
        ScaleHarmonizer harm;
        harm.setKey(2);  // D
        harm.setScale(ScaleType::Dorian);

        // D4=62, E4=64, F4=65, G4=67, A4=69, B4=71, C5=72

        struct SpotCheck {
            int inputMidi;
            int expectedSemitones;
            int expectedTarget;
            int expectedDegree;
            int expectedOctave;
            const char* name;
        };

        const std::array<SpotCheck, 7> checks = {{
            {62, +3, 65, 2, 0, "D4->F4 (minor 3rd)"},
            {64, +3, 67, 3, 0, "E4->G4 (minor 3rd)"},
            {65, +4, 69, 4, 0, "F4->A4 (major 3rd)"},
            {67, +4, 71, 5, 0, "G4->B4 (major 3rd)"},
            {69, +3, 72, 6, 0, "A4->C5 (minor 3rd)"},
            {71, +3, 74, 0, 1, "B4->D5 (minor 3rd, wraps octave)"},
            {72, +4, 76, 1, 1, "C5->E5 (major 3rd, wraps octave)"},
        }};

        for (const auto& sc : checks) {
            INFO("Spot-check: " << sc.name << " (MIDI " << sc.inputMidi << ")");
            auto result = harm.calculate(sc.inputMidi, +2);
            CHECK(result.semitones == sc.expectedSemitones);
            CHECK(result.targetNote == sc.expectedTarget);
            CHECK(result.scaleDegree == sc.expectedDegree);
            CHECK(result.octaveOffset == sc.expectedOctave);
        }
    }
}

// =============================================================================
// US6: Negative Intervals - 3rd Below (T052 / FR-001 negative / SC-004)
// =============================================================================

TEST_CASE("ScaleHarmonizer negative intervals: 3rd below in C Major",
          "[scale-harmonizer][us6]") {
    ScaleHarmonizer harm;
    harm.setKey(0);  // C
    harm.setScale(ScaleType::Major);

    SECTION("E4 (MIDI 64) 3rd below -> C4 (MIDI 60), -4 semitones") {
        // E4 is degree 2 in C Major.
        // -2 diatonic steps: totalDegree = 2 + (-2) = 0
        // octaves = 0, targetDegree = 0 (C)
        // shift = intervals[0] - intervals[2] = 0 - 4 = -4
        // target = 64 + (-4) = 60 (C4)
        auto result = harm.calculate(64, -2);
        CHECK(result.semitones == -4);
        CHECK(result.targetNote == 60);
        CHECK(result.scaleDegree == 0);
        CHECK(result.octaveOffset == 0);
    }

    SECTION("C4 (MIDI 60) 3rd below -> A3 (MIDI 57), -3 semitones, wraps below octave") {
        // C4 is degree 0 in C Major.
        // -2 diatonic steps: totalDegree = 0 + (-2) = -2
        // Floor division: octaves = (-2 - 6) / 7 = -8 / 7 = -1 (C++ truncation)
        // targetDegree = ((-2 % 7) + 7) % 7 = ((-2) + 7) % 7 = 5
        // shift = intervals[5] - intervals[0] + (-1) * 12 = 9 - 0 - 12 = -3
        // target = 60 + (-3) = 57 (A3)
        auto result = harm.calculate(60, -2);
        CHECK(result.semitones == -3);
        CHECK(result.targetNote == 57);
        CHECK(result.scaleDegree == 5);
        CHECK(result.octaveOffset == -1);
    }

    SECTION("All 7 scale degrees with 3rd below (-2 steps)") {
        // For each degree, verify 3rd below produces correct results
        // C Major scale in octave 4: C=60, D=62, E=64, F=65, G=67, A=69, B=71

        struct TestCase {
            int inputMidi;
            int expectedSemitones;
            int expectedTargetNote;
            int expectedScaleDegree;
            int expectedOctaveOffset;
            const char* name;
        };

        // 3rd below for each C Major degree:
        // C(deg0) -2 -> A(deg5, octave-1): 9-0-12 = -3, target=57
        // D(deg1) -2 -> B(deg6, octave-1): 11-2-12 = -3, target=59
        // E(deg2) -2 -> C(deg0): 0-4 = -4, target=60
        // F(deg3) -2 -> D(deg1): 2-5 = -3, target=62
        // G(deg4) -2 -> E(deg2): 4-7 = -3, target=64
        // A(deg5) -2 -> F(deg3): 5-9 = -4, target=65
        // B(deg6) -2 -> G(deg4): 7-11 = -4, target=67
        const std::array<TestCase, 7> cases = {{
            {60, -3, 57, 5, -1, "C4->A3"},
            {62, -3, 59, 6, -1, "D4->B3"},
            {64, -4, 60, 0,  0, "E4->C4"},
            {65, -3, 62, 1,  0, "F4->D4"},
            {67, -3, 64, 2,  0, "G4->E4"},
            {69, -4, 65, 3,  0, "A4->F4"},
            {71, -4, 67, 4,  0, "B4->G4"},
        }};

        for (const auto& tc : cases) {
            INFO("Case: " << tc.name << " (MIDI " << tc.inputMidi << ")");
            auto result = harm.calculate(tc.inputMidi, -2);
            CHECK(result.semitones == tc.expectedSemitones);
            CHECK(result.targetNote == tc.expectedTargetNote);
            CHECK(result.scaleDegree == tc.expectedScaleDegree);
            CHECK(result.octaveOffset == tc.expectedOctaveOffset);
        }
    }
}

// =============================================================================
// US6: Octave-Exact Negative Intervals (T053 / FR-007)
// =============================================================================

TEST_CASE("ScaleHarmonizer octave-exact negative intervals",
          "[scale-harmonizer][us6]") {
    ScaleHarmonizer harm;
    harm.setKey(0);  // C
    harm.setScale(ScaleType::Major);

    SECTION("diatonicSteps=-7 (octave below) for C5 (MIDI 72) -> C4 (MIDI 60)") {
        // C5 is degree 0. -7 steps: totalDegree = 0 + (-7) = -7
        // Floor division: octaves = (-7-6)/7 = -13/7 = -1 (C++ truncation)
        // targetDegree = ((-7%7)+7)%7 = (0+7)%7 = 0
        // shift = intervals[0] - intervals[0] + (-1)*12 = -12
        // target = 72 - 12 = 60 (C4)
        auto result = harm.calculate(72, -7);
        CHECK(result.semitones == -12);
        CHECK(result.targetNote == 60);
        CHECK(result.scaleDegree == 0);
        CHECK(result.octaveOffset == -1);
    }

    SECTION("diatonicSteps=-7 for E4 (MIDI 64) -> E3 (MIDI 52)") {
        // E4 is degree 2. -7 steps: totalDegree = 2 + (-7) = -5
        // Floor division: octaves = (-5-6)/7 = -11/7 = -1 (C++ truncation)
        // targetDegree = ((-5%7)+7)%7 = ((-5)+7)%7 = 2
        // shift = intervals[2] - intervals[2] + (-1)*12 = -12
        // target = 64 - 12 = 52 (E3)
        auto result = harm.calculate(64, -7);
        CHECK(result.semitones == -12);
        CHECK(result.targetNote == 52);
        CHECK(result.scaleDegree == 2);
        CHECK(result.octaveOffset == -1);
    }

    SECTION("diatonicSteps=-7 for G4 (MIDI 67) -> G3 (MIDI 55)") {
        // G4 is degree 4. -7 steps: totalDegree = 4 + (-7) = -3
        // Floor division: octaves = (-3-6)/7 = -9/7 = -1 (C++ truncation)
        // targetDegree = ((-3%7)+7)%7 = ((-3)+7)%7 = 4
        // shift = intervals[4] - intervals[4] + (-1)*12 = -12
        // target = 67 - 12 = 55 (G3)
        auto result = harm.calculate(67, -7);
        CHECK(result.semitones == -12);
        CHECK(result.targetNote == 55);
        CHECK(result.scaleDegree == 4);
        CHECK(result.octaveOffset == -1);
    }
}

// =============================================================================
// US6: Multi-Octave Negative Intervals (T054 / FR-008 / SC-006)
// =============================================================================

TEST_CASE("ScaleHarmonizer multi-octave negative intervals",
          "[scale-harmonizer][us6]") {
    ScaleHarmonizer harm;
    harm.setKey(0);  // C
    harm.setScale(ScaleType::Major);

    SECTION("diatonicSteps=-9 from C4 (MIDI 60) -> A2 (MIDI 45)") {
        // C4 is degree 0. -9 steps: totalDegree = 0 + (-9) = -9
        // Floor division: octaves = (-9-6)/7 = -15/7 = -2 (C++ truncation)
        // targetDegree = ((-9%7)+7)%7 = ((-2)+7)%7 = 5
        // shift = intervals[5] - intervals[0] + (-2)*12 = 9 - 0 - 24 = -15
        // target = 60 - 15 = 45 (A2)
        auto result = harm.calculate(60, -9);
        CHECK(result.semitones == -15);
        CHECK(result.targetNote == 45);
        CHECK(result.scaleDegree == 5);
        CHECK(result.octaveOffset == -2);
    }

    SECTION("diatonicSteps=-14 from C4 (MIDI 60) -> C2 (MIDI 36)") {
        // C4 is degree 0. -14 steps: totalDegree = 0 + (-14) = -14
        // Floor division: octaves = (-14-6)/7 = -20/7 = -2 (C++ truncation)
        // targetDegree = ((-14%7)+7)%7 = (0+7)%7 = 0
        // shift = intervals[0] - intervals[0] + (-2)*12 = -24
        // target = 60 - 24 = 36 (C2)
        auto result = harm.calculate(60, -14);
        CHECK(result.semitones == -24);
        CHECK(result.targetNote == 36);
        CHECK(result.scaleDegree == 0);
        CHECK(result.octaveOffset == -2);
    }

    SECTION("diatonicSteps=-9 from E4 (MIDI 64) -> C3 (MIDI 48)") {
        // E4 is degree 2. -9 steps: totalDegree = 2 + (-9) = -7
        // Floor division: octaves = (-7-6)/7 = -13/7 = -1 (C++ truncation)
        // targetDegree = ((-7%7)+7)%7 = (0+7)%7 = 0
        // shift = intervals[0] - intervals[2] + (-1)*12 = 0 - 4 - 12 = -16
        // target = 64 - 16 = 48 (C3)
        auto result = harm.calculate(64, -9);
        CHECK(result.semitones == -16);
        CHECK(result.targetNote == 48);
        CHECK(result.scaleDegree == 0);
        CHECK(result.octaveOffset == -1);
    }

    SECTION("diatonicSteps=-14 from G4 (MIDI 67) -> G2 (MIDI 43)") {
        // G4 is degree 4. -14 steps: totalDegree = 4 + (-14) = -10
        // Floor division: octaves = (-10-6)/7 = -16/7 = -2 (C++ truncation)
        // targetDegree = ((-10%7)+7)%7 = ((-3)+7)%7 = 4
        // shift = intervals[4] - intervals[4] + (-2)*12 = 0 - 24 = -24
        // target = 67 - 24 = 43 (G2)
        auto result = harm.calculate(67, -14);
        CHECK(result.semitones == -24);
        CHECK(result.targetNote == 43);
        CHECK(result.scaleDegree == 4);
        CHECK(result.octaveOffset == -2);
    }

    SECTION("diatonicSteps=-14 from D4 (MIDI 62) -> D2 (MIDI 38)") {
        // D4 is degree 1. -14 steps: totalDegree = 1 + (-14) = -13
        // Floor division: octaves = (-13-6)/7 = -19/7 = -2 (C++ truncation)
        // targetDegree = ((-13%7)+7)%7 = ((-6)+7)%7 = 1
        // shift = intervals[1] - intervals[1] + (-2)*12 = 0 - 24 = -24
        // target = 62 - 24 = 38 (D2)
        auto result = harm.calculate(62, -14);
        CHECK(result.semitones == -24);
        CHECK(result.targetNote == 38);
        CHECK(result.scaleDegree == 1);
        CHECK(result.octaveOffset == -2);
    }

    SECTION("Multi-octave negative with non-C root: A Minor (-9 from E4)") {
        // A Natural Minor: A(0), B(2), C(3), D(5), E(7), F(8), G(10)
        // Key = 9 (A). E4 (MIDI 64): pitchClass = 4, offset from A = (4-9+12)%12 = 7
        // degree via reverse lookup = 4 (E is degree 4, intervals[4]=7)
        // -9 steps from degree 4: totalDegree = 4 + (-9) = -5
        // Floor division: octaves = (-5-6)/7 = -11/7 = -1 (C++ truncation)
        // targetDegree = ((-5%7)+7)%7 = ((-5)+7)%7 = 2
        // shift = intervals[2] - intervals[4] + (-1)*12 = 3 - 7 - 12 = -16
        // target = 64 - 16 = 48 (C3)
        // C is degree 2 in A Natural Minor
        harm.setKey(9);  // A
        harm.setScale(ScaleType::NaturalMinor);

        auto result = harm.calculate(64, -9);
        CHECK(result.semitones == -16);
        CHECK(result.targetNote == 48);
        CHECK(result.scaleDegree == 2);
        CHECK(result.octaveOffset == -1);
    }
}

// =============================================================================
// Phase 9: MIDI Boundary Clamping Tests (T061 / FR-009 / SC-007)
// =============================================================================

TEST_CASE("ScaleHarmonizer MIDI boundary clamping",
          "[scale-harmonizer][edge]") {
    ScaleHarmonizer harm;
    harm.setKey(0);  // C
    harm.setScale(ScaleType::Major);

    SECTION("Input MIDI 127 + large positive interval clamps targetNote to 127") {
        // MIDI 127 (G9). Diatonic steps +7 (octave above) would put target at 139.
        // Must clamp targetNote to 127 and recompute semitones.
        auto result = harm.calculate(127, +7);
        CHECK(result.targetNote <= 127);
        CHECK(result.semitones == result.targetNote - 127);
    }

    SECTION("Input MIDI 0 + large negative interval clamps targetNote to 0") {
        // MIDI 0. Diatonic steps -7 (octave below) would put target at -12.
        // Must clamp targetNote to 0 and recompute semitones.
        auto result = harm.calculate(0, -7);
        CHECK(result.targetNote >= 0);
        CHECK(result.semitones == result.targetNote - 0);
    }

    SECTION("Clamping at upper boundary: semitones reflects clamped shift") {
        // MIDI 120 + very large positive interval
        auto result = harm.calculate(120, +14);  // 2 octaves up = +24 semitones, 120+24=144 -> clamp to 127
        CHECK(result.targetNote == 127);
        CHECK(result.semitones == 7);  // 127 - 120
    }

    SECTION("Clamping at lower boundary: semitones reflects clamped shift") {
        // MIDI 5 + very large negative interval
        auto result = harm.calculate(5, -14);  // 2 octaves down = -24 semitones, 5-24=-19 -> clamp to 0
        CHECK(result.targetNote == 0);
        CHECK(result.semitones == -5);  // 0 - 5
    }

    SECTION("Chromatic mode also clamps correctly") {
        harm.setScale(ScaleType::Chromatic);

        // Upper boundary
        auto resultHigh = harm.calculate(127, +10);
        CHECK(resultHigh.targetNote == 127);
        CHECK(resultHigh.semitones == 0);  // 127 - 127

        // Lower boundary
        auto resultLow = harm.calculate(0, -10);
        CHECK(resultLow.targetNote == 0);
        CHECK(resultLow.semitones == 0);  // 0 - 0
    }

    SECTION("Exact boundary: MIDI 127 with diatonicSteps=0") {
        auto result = harm.calculate(127, 0);
        CHECK(result.targetNote == 127);
        CHECK(result.semitones == 0);
    }

    SECTION("Exact boundary: MIDI 0 with diatonicSteps=0") {
        auto result = harm.calculate(0, 0);
        CHECK(result.targetNote == 0);
        CHECK(result.semitones == 0);
    }
}

// =============================================================================
// Phase 9: Unison Tests (T062 / FR-006)
// =============================================================================

TEST_CASE("ScaleHarmonizer unison (diatonicSteps=0) behavior",
          "[scale-harmonizer][edge]") {
    ScaleHarmonizer harm;

    SECTION("Unison always returns semitones=0, octaveOffset=0, targetNote=inputMidiNote") {
        // Test across all 8 diatonic scales and all 12 keys
        constexpr std::array<ScaleType, 8> scales = {
            ScaleType::Major, ScaleType::NaturalMinor, ScaleType::HarmonicMinor,
            ScaleType::MelodicMinor, ScaleType::Dorian, ScaleType::Mixolydian,
            ScaleType::Phrygian, ScaleType::Lydian,
        };

        for (auto scaleType : scales) {
            for (int key = 0; key < 12; ++key) {
                harm.setKey(key);
                harm.setScale(scaleType);

                // Test a few representative MIDI notes
                for (int note : {0, 36, 60, 69, 96, 127}) {
                    INFO("Scale=" << static_cast<int>(scaleType)
                         << " Key=" << key << " Note=" << note);
                    auto result = harm.calculate(note, 0);
                    CHECK(result.semitones == 0);
                    CHECK(result.targetNote == note);
                    CHECK(result.octaveOffset == 0);
                }
            }
        }
    }

    SECTION("Unison scaleDegree matches getScaleDegree for scale notes") {
        harm.setKey(0);  // C
        harm.setScale(ScaleType::Major);

        // C Major scale notes in octave 4: C=60, D=62, E=64, F=65, G=67, A=69, B=71
        constexpr std::array<int, 7> scaleNotes = {60, 62, 64, 65, 67, 69, 71};
        constexpr std::array<int, 7> expectedDegrees = {0, 1, 2, 3, 4, 5, 6};

        for (int i = 0; i < 7; ++i) {
            INFO("Scale note: " << scaleNotes[static_cast<size_t>(i)]);
            auto result = harm.calculate(scaleNotes[static_cast<size_t>(i)], 0);
            CHECK(result.scaleDegree == expectedDegrees[static_cast<size_t>(i)]);
            CHECK(result.scaleDegree == harm.getScaleDegree(scaleNotes[static_cast<size_t>(i)]));
        }
    }

    SECTION("Unison for non-scale notes: scaleDegree is nearest scale degree (not -1)") {
        harm.setKey(0);  // C
        harm.setScale(ScaleType::Major);

        // C#4 (MIDI 61) is not in C Major.
        // Nearest degree via reverse lookup = 0 (C, round-down on tie).
        auto result = harm.calculate(61, 0);
        CHECK(result.semitones == 0);
        CHECK(result.targetNote == 61);
        // scaleDegree = nearest degree from reverse lookup, NOT -1
        // (For unison, target note = input note, resolved via nearest degree)
        CHECK(result.scaleDegree == 0);  // nearest to C# is C (degree 0)

        // Eb4 (MIDI 63) -> nearest degree is D (degree 1), round-down on tie
        auto result2 = harm.calculate(63, 0);
        CHECK(result2.scaleDegree == 1);
    }

    SECTION("Chromatic mode unison: scaleDegree is -1") {
        harm.setScale(ScaleType::Chromatic);
        auto result = harm.calculate(60, 0);
        CHECK(result.semitones == 0);
        CHECK(result.targetNote == 60);
        CHECK(result.scaleDegree == -1);
        CHECK(result.octaveOffset == 0);
    }
}

// =============================================================================
// Phase 9: getSemitoneShift() Frequency Convenience Method (T063 / FR-012)
// =============================================================================

TEST_CASE("ScaleHarmonizer getSemitoneShift frequency convenience method",
          "[scale-harmonizer][edge]") {
    ScaleHarmonizer harm;
    harm.setKey(0);  // C
    harm.setScale(ScaleType::Major);

    SECTION("440.0f Hz (A4=MIDI 69) with 3rd above returns same as calculate(69, +2)") {
        auto calcResult = harm.calculate(69, +2);
        float shiftFromFreq = harm.getSemitoneShift(440.0f, +2);
        CHECK(shiftFromFreq == static_cast<float>(calcResult.semitones));
    }

    SECTION("261.63f Hz (C4 ~= MIDI 60) with 3rd above returns same as calculate(60, +2)") {
        // Middle C is approximately 261.63 Hz -> MIDI 60
        auto calcResult = harm.calculate(60, +2);
        float shiftFromFreq = harm.getSemitoneShift(261.63f, +2);
        CHECK(shiftFromFreq == static_cast<float>(calcResult.semitones));
    }

    SECTION("Fractional MIDI note rounding: 440.5 Hz rounds to MIDI 69") {
        // 440.5 Hz -> frequencyToMidiNote = 12 * log2(440.5/440) + 69 = ~69.019
        // Rounds to MIDI 69
        auto calcResult = harm.calculate(69, +2);
        float shiftFromFreq = harm.getSemitoneShift(440.5f, +2);
        CHECK(shiftFromFreq == static_cast<float>(calcResult.semitones));
    }

    SECTION("Fractional MIDI note rounding: 453.08 Hz rounds to MIDI 70 (Bb4)") {
        // 453.08 Hz -> frequencyToMidiNote = 12 * log2(453.08/440) + 69 = ~69.508
        // Rounds to MIDI 70
        // But Bb4 (70) is not in C Major; nearest degree = A (degree 5)
        auto calcResult = harm.calculate(70, +2);
        float shiftFromFreq = harm.getSemitoneShift(453.08f, +2);
        CHECK(shiftFromFreq == static_cast<float>(calcResult.semitones));
    }

    SECTION("Negative interval via frequency") {
        // A4 = 440 Hz = MIDI 69, 3rd below (-2 steps)
        auto calcResult = harm.calculate(69, -2);
        float shiftFromFreq = harm.getSemitoneShift(440.0f, -2);
        CHECK(shiftFromFreq == static_cast<float>(calcResult.semitones));
    }

    SECTION("Chromatic mode via frequency") {
        harm.setScale(ScaleType::Chromatic);
        // 440 Hz = MIDI 69, +7 semitones
        float shiftFromFreq = harm.getSemitoneShift(440.0f, +7);
        CHECK(shiftFromFreq == 7.0f);
    }
}

// =============================================================================
// Phase 9: noexcept Verification (T064 / SC-008)
// =============================================================================

TEST_CASE("ScaleHarmonizer all methods are noexcept",
          "[scale-harmonizer][edge]") {
    // Verify at compile time that all public methods are noexcept
    // per FR-014 and SC-008.
    static_assert(noexcept(std::declval<ScaleHarmonizer>().setKey(0)),
                  "setKey must be noexcept");
    static_assert(noexcept(std::declval<ScaleHarmonizer>().setScale(ScaleType::Major)),
                  "setScale must be noexcept");
    static_assert(noexcept(std::declval<const ScaleHarmonizer>().getKey()),
                  "getKey must be noexcept");
    static_assert(noexcept(std::declval<const ScaleHarmonizer>().getScale()),
                  "getScale must be noexcept");
    static_assert(noexcept(std::declval<const ScaleHarmonizer>().calculate(60, 2)),
                  "calculate must be noexcept");
    static_assert(noexcept(std::declval<const ScaleHarmonizer>().getSemitoneShift(440.0f, 2)),
                  "getSemitoneShift must be noexcept");
    static_assert(noexcept(std::declval<const ScaleHarmonizer>().getScaleDegree(60)),
                  "getScaleDegree must be noexcept");
    static_assert(noexcept(std::declval<const ScaleHarmonizer>().quantizeToScale(60)),
                  "quantizeToScale must be noexcept");
    static_assert(noexcept(ScaleHarmonizer::getScaleIntervals(ScaleType::Major)),
                  "getScaleIntervals must be noexcept");

    // SC-008: Verify zero heap allocations by code inspection.
    // ScaleHarmonizer uses only:
    // - constexpr arrays (compile-time, no heap)
    // - int and enum members (stack/register)
    // - std::clamp, std::round (no allocations)
    // - std::log2 (in frequencyToMidiNote, no allocations)
    // No std::string, std::vector, new/delete, or malloc/free anywhere.
    SUCCEED("All methods verified noexcept via static_assert; zero allocations confirmed by inspection");
}
