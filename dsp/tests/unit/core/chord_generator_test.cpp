// ==============================================================================
// ChordGenerator Unit Tests
// ==============================================================================
// Tests for chord generation, inversion, and voicing utilities.
// Feature: arp-chord-lane, Phase 1
// ==============================================================================

#include <krate/dsp/core/chord_generator.h>
#include <krate/dsp/core/scale_harmonizer.h>

#include <catch2/catch_test_macros.hpp>

using namespace Krate::DSP;

// =============================================================================
// generateChordNotes Tests
// =============================================================================

TEST_CASE("ChordGenerator: None returns single root note", "[chord-generator]") {
    ScaleHarmonizer harm;
    harm.setKey(0);  // C
    harm.setScale(ScaleType::Major);

    auto result = generateChordNotes(60, ChordType::None, harm);
    REQUIRE(result.count == 1);
    CHECK(result.notes[0] == 60);
}

TEST_CASE("ChordGenerator: Diatonic triad C Major from C4", "[chord-generator]") {
    ScaleHarmonizer harm;
    harm.setKey(0);  // C
    harm.setScale(ScaleType::Major);

    // C Major triad from C4=60: C(60), E(64), G(67)
    auto result = generateChordNotes(60, ChordType::Triad, harm);
    REQUIRE(result.count == 3);
    CHECK(result.notes[0] == 60);  // C4
    CHECK(result.notes[1] == 64);  // E4
    CHECK(result.notes[2] == 67);  // G4
}

TEST_CASE("ChordGenerator: Diatonic triad C Major from D4 produces minor quality",
          "[chord-generator]") {
    ScaleHarmonizer harm;
    harm.setKey(0);  // C
    harm.setScale(ScaleType::Major);

    // D minor triad from D4=62: D(62), F(65), A(69)
    auto result = generateChordNotes(62, ChordType::Triad, harm);
    REQUIRE(result.count == 3);
    CHECK(result.notes[0] == 62);  // D4
    CHECK(result.notes[1] == 65);  // F4
    CHECK(result.notes[2] == 69);  // A4
}

TEST_CASE("ChordGenerator: Diatonic dyad C Major from C4", "[chord-generator]") {
    ScaleHarmonizer harm;
    harm.setKey(0);
    harm.setScale(ScaleType::Major);

    // Dyad = root + 5th: C(60), G(67)
    auto result = generateChordNotes(60, ChordType::Dyad, harm);
    REQUIRE(result.count == 2);
    CHECK(result.notes[0] == 60);  // C4
    CHECK(result.notes[1] == 67);  // G4
}

TEST_CASE("ChordGenerator: Diatonic 7th C Major from C4", "[chord-generator]") {
    ScaleHarmonizer harm;
    harm.setKey(0);
    harm.setScale(ScaleType::Major);

    // Cmaj7: C(60), E(64), G(67), B(71)
    auto result = generateChordNotes(60, ChordType::Seventh, harm);
    REQUIRE(result.count == 4);
    CHECK(result.notes[0] == 60);  // C4
    CHECK(result.notes[1] == 64);  // E4
    CHECK(result.notes[2] == 67);  // G4
    CHECK(result.notes[3] == 71);  // B4
}

TEST_CASE("ChordGenerator: Diatonic 9th C Major from C4", "[chord-generator]") {
    ScaleHarmonizer harm;
    harm.setKey(0);
    harm.setScale(ScaleType::Major);

    // Cmaj9: C(60), E(64), G(67), B(71), D(74)
    auto result = generateChordNotes(60, ChordType::Ninth, harm);
    REQUIRE(result.count == 5);
    CHECK(result.notes[0] == 60);  // C4
    CHECK(result.notes[1] == 64);  // E4
    CHECK(result.notes[2] == 67);  // G4
    CHECK(result.notes[3] == 71);  // B4
    CHECK(result.notes[4] == 74);  // D5
}

TEST_CASE("ChordGenerator: Chromatic triad", "[chord-generator]") {
    ScaleHarmonizer harm;
    harm.setScale(ScaleType::Chromatic);

    // Chromatic triad from C4: C(60), E(64), G(67)
    auto result = generateChordNotes(60, ChordType::Triad, harm);
    REQUIRE(result.count == 3);
    CHECK(result.notes[0] == 60);
    CHECK(result.notes[1] == 64);  // +4
    CHECK(result.notes[2] == 67);  // +7
}

TEST_CASE("ChordGenerator: Chromatic dyad", "[chord-generator]") {
    ScaleHarmonizer harm;
    harm.setScale(ScaleType::Chromatic);

    auto result = generateChordNotes(60, ChordType::Dyad, harm);
    REQUIRE(result.count == 2);
    CHECK(result.notes[0] == 60);
    CHECK(result.notes[1] == 67);  // +7
}

TEST_CASE("ChordGenerator: Chromatic 7th", "[chord-generator]") {
    ScaleHarmonizer harm;
    harm.setScale(ScaleType::Chromatic);

    auto result = generateChordNotes(60, ChordType::Seventh, harm);
    REQUIRE(result.count == 4);
    CHECK(result.notes[0] == 60);
    CHECK(result.notes[1] == 64);  // +4
    CHECK(result.notes[2] == 67);  // +7
    CHECK(result.notes[3] == 70);  // +10
}

TEST_CASE("ChordGenerator: Chromatic 9th", "[chord-generator]") {
    ScaleHarmonizer harm;
    harm.setScale(ScaleType::Chromatic);

    auto result = generateChordNotes(60, ChordType::Ninth, harm);
    REQUIRE(result.count == 5);
    CHECK(result.notes[0] == 60);
    CHECK(result.notes[1] == 64);  // +4
    CHECK(result.notes[2] == 67);  // +7
    CHECK(result.notes[3] == 70);  // +10
    CHECK(result.notes[4] == 74);  // +14
}

TEST_CASE("ChordGenerator: Pentatonic chord stacking wraps degrees",
          "[chord-generator]") {
    ScaleHarmonizer harm;
    harm.setKey(0);  // C
    harm.setScale(ScaleType::MajorPentatonic);

    // Major pentatonic: {C, D, E, G, A} = 5 degrees
    // Triad from C: root + degree 2 + degree 4
    // Degree 0=C, degree 2=E, degree 4=A
    auto result = generateChordNotes(60, ChordType::Triad, harm);
    REQUIRE(result.count == 3);
    CHECK(result.notes[0] == 60);  // C4
    // Degree +2 from C = E (scale degree 2, semitone 4)
    CHECK(result.notes[1] == 64);  // E4
    // Degree +4 from C = A (scale degree 4, semitone 9)
    CHECK(result.notes[2] == 69);  // A4
}

TEST_CASE("ChordGenerator: MIDI note clamping at 127", "[chord-generator]") {
    ScaleHarmonizer harm;
    harm.setScale(ScaleType::Chromatic);

    // Root at 120, 9th chord: 120, 124, 127 (clamped), 127, 127
    auto result = generateChordNotes(120, ChordType::Ninth, harm);
    REQUIRE(result.count == 5);
    CHECK(result.notes[0] == 120);
    CHECK(result.notes[1] == 124);  // +4
    CHECK(result.notes[2] == 127);  // +7 = 127
    CHECK(result.notes[3] == 127);  // +10 clamped to 127
    CHECK(result.notes[4] == 127);  // +14 clamped to 127
}

// =============================================================================
// applyInversion Tests
// =============================================================================

TEST_CASE("ChordGenerator: Root inversion is no-op", "[chord-generator]") {
    ChordResult chord;
    chord.notes = {60, 64, 67, 0, 0};
    chord.count = 3;

    applyInversion(chord, InversionType::Root);
    CHECK(chord.notes[0] == 60);
    CHECK(chord.notes[1] == 64);
    CHECK(chord.notes[2] == 67);
}

TEST_CASE("ChordGenerator: 1st inversion on triad", "[chord-generator]") {
    ChordResult chord;
    chord.notes = {60, 64, 67, 0, 0};
    chord.count = 3;

    // 1st inversion: bottom note (60) goes up an octave (72)
    // Result: {64, 67, 72}
    applyInversion(chord, InversionType::First);
    CHECK(chord.notes[0] == 64);
    CHECK(chord.notes[1] == 67);
    CHECK(chord.notes[2] == 72);
}

TEST_CASE("ChordGenerator: 2nd inversion on triad", "[chord-generator]") {
    ChordResult chord;
    chord.notes = {60, 64, 67, 0, 0};
    chord.count = 3;

    // 2nd inversion: bottom 2 notes go up
    // After 1st rotation: {64, 67, 72}
    // After 2nd rotation: {67, 72, 76}
    applyInversion(chord, InversionType::Second);
    CHECK(chord.notes[0] == 67);
    CHECK(chord.notes[1] == 72);
    CHECK(chord.notes[2] == 76);
}

TEST_CASE("ChordGenerator: 3rd inversion on 7th chord", "[chord-generator]") {
    ChordResult chord;
    chord.notes = {60, 64, 67, 71, 0};
    chord.count = 4;

    // 3rd inversion on 7th: bottom 3 notes rotated up
    // After 1st: {64, 67, 71, 72}
    // After 2nd: {67, 71, 72, 76}
    // After 3rd: {71, 72, 76, 79}
    applyInversion(chord, InversionType::Third);
    CHECK(chord.notes[0] == 71);
    CHECK(chord.notes[1] == 72);
    CHECK(chord.notes[2] == 76);
    CHECK(chord.notes[3] == 79);
}

TEST_CASE("ChordGenerator: Inversion clamped for small chords",
          "[chord-generator]") {
    // 3rd inversion on triad: clamped to 2nd inversion (max rotations = count-1 = 2)
    ChordResult chord;
    chord.notes = {60, 64, 67, 0, 0};
    chord.count = 3;

    applyInversion(chord, InversionType::Third);
    // Same as 2nd inversion: {67, 72, 76}
    CHECK(chord.notes[0] == 67);
    CHECK(chord.notes[1] == 72);
    CHECK(chord.notes[2] == 76);
}

TEST_CASE("ChordGenerator: Inversion clamps notes at 127", "[chord-generator]") {
    ChordResult chord;
    chord.notes = {120, 124, 127, 0, 0};
    chord.count = 3;

    // 1st inversion: 120 + 12 = 127 (clamped)
    applyInversion(chord, InversionType::First);
    CHECK(chord.notes[0] == 124);
    CHECK(chord.notes[1] == 127);
    CHECK(chord.notes[2] == 127);  // clamped
}

TEST_CASE("ChordGenerator: Inversion on single note is no-op",
          "[chord-generator]") {
    ChordResult chord;
    chord.notes = {60, 0, 0, 0, 0};
    chord.count = 1;

    applyInversion(chord, InversionType::First);
    CHECK(chord.notes[0] == 60);
    CHECK(chord.count == 1);
}

// =============================================================================
// applyVoicing Tests
// =============================================================================

TEST_CASE("ChordGenerator: Close voicing is no-op", "[chord-generator]") {
    ChordResult chord;
    chord.notes = {60, 64, 67, 0, 0};
    chord.count = 3;

    applyVoicing(chord, VoicingMode::Close, 12345);
    CHECK(chord.notes[0] == 60);
    CHECK(chord.notes[1] == 64);
    CHECK(chord.notes[2] == 67);
}

TEST_CASE("ChordGenerator: Drop-2 voicing", "[chord-generator]") {
    ChordResult chord;
    chord.notes = {60, 64, 67, 71, 0};
    chord.count = 4;

    // Drop-2: second-from-top (index 2, G=67) drops an octave (55)
    applyVoicing(chord, VoicingMode::Drop2, 0);
    CHECK(chord.notes[0] == 60);
    CHECK(chord.notes[1] == 64);
    CHECK(chord.notes[2] == 55);  // G dropped
    CHECK(chord.notes[3] == 71);
}

TEST_CASE("ChordGenerator: Drop-2 clamps at 0", "[chord-generator]") {
    ChordResult chord;
    chord.notes = {5, 9, 0, 0};
    chord.count = 2;

    // Drop-2 on dyad: second-from-top is index 0 (5), drops 12 -> clamped to 0
    applyVoicing(chord, VoicingMode::Drop2, 0);
    CHECK(chord.notes[0] == 0);  // 5-12 = clamped to 0
    CHECK(chord.notes[1] == 9);
}

TEST_CASE("ChordGenerator: Spread voicing raises odd-indexed notes",
          "[chord-generator]") {
    ChordResult chord;
    chord.notes = {60, 64, 67, 71, 0};
    chord.count = 4;

    // Spread: index 1 (64->76), index 3 (71->83)
    applyVoicing(chord, VoicingMode::Spread, 0);
    CHECK(chord.notes[0] == 60);
    CHECK(chord.notes[1] == 76);  // +12
    CHECK(chord.notes[2] == 67);
    CHECK(chord.notes[3] == 83);  // +12
}

TEST_CASE("ChordGenerator: Random voicing is deterministic for same seed",
          "[chord-generator]") {
    ChordResult chord1;
    chord1.notes = {60, 64, 67, 71, 0};
    chord1.count = 4;

    ChordResult chord2;
    chord2.notes = {60, 64, 67, 71, 0};
    chord2.count = 4;

    applyVoicing(chord1, VoicingMode::Random, 42);
    applyVoicing(chord2, VoicingMode::Random, 42);

    CHECK(chord1.notes[0] == chord2.notes[0]);
    CHECK(chord1.notes[1] == chord2.notes[1]);
    CHECK(chord1.notes[2] == chord2.notes[2]);
    CHECK(chord1.notes[3] == chord2.notes[3]);
}

TEST_CASE("ChordGenerator: Random voicing differs for different seeds",
          "[chord-generator]") {
    ChordResult chord1;
    chord1.notes = {60, 64, 67, 71, 74};
    chord1.count = 5;

    ChordResult chord2;
    chord2.notes = {60, 64, 67, 71, 74};
    chord2.count = 5;

    applyVoicing(chord1, VoicingMode::Random, 42);
    applyVoicing(chord2, VoicingMode::Random, 99999);

    // Root is never modified
    CHECK(chord1.notes[0] == chord2.notes[0]);
    // At least one other note should differ with high probability
    bool anyDifferent = false;
    for (size_t i = 1; i < 5; ++i) {
        if (chord1.notes[i] != chord2.notes[i]) {
            anyDifferent = true;
            break;
        }
    }
    CHECK(anyDifferent);
}

TEST_CASE("ChordGenerator: Voicing on single note is no-op",
          "[chord-generator]") {
    ChordResult chord;
    chord.notes = {60, 0, 0, 0, 0};
    chord.count = 1;

    applyVoicing(chord, VoicingMode::Spread, 42);
    CHECK(chord.notes[0] == 60);
    CHECK(chord.count == 1);
}
