// ==============================================================================
// Layer 1: Primitives
// held_note_buffer_test.cpp - Tests for HeldNoteBuffer & NoteSelector
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/primitives/held_note_buffer.h>

#include <algorithm>
#include <set>
#include <vector>

using Krate::DSP::HeldNoteBuffer;
using Krate::DSP::HeldNote;
using Krate::DSP::NoteSelector;
using Krate::DSP::ArpMode;
using Krate::DSP::OctaveMode;
using Krate::DSP::ArpNoteResult;

// =============================================================================
// HeldNoteBuffer Tests (User Story 1)
// =============================================================================

TEST_CASE("HeldNoteBuffer - noteOn adds notes", "[held_note_buffer]") {
    HeldNoteBuffer buffer;

    buffer.noteOn(60, 100);  // C3
    buffer.noteOn(64, 90);   // E3
    buffer.noteOn(67, 80);   // G3

    // Verify size
    REQUIRE(buffer.size() == 3);
    REQUIRE_FALSE(buffer.empty());

    // Verify byPitch() returns ascending order
    auto pitched = buffer.byPitch();
    REQUIRE(pitched.size() == 3);
    REQUIRE(pitched[0].note == 60);
    REQUIRE(pitched[1].note == 64);
    REQUIRE(pitched[2].note == 67);

    // Verify velocities are correct
    REQUIRE(pitched[0].velocity == 100);
    REQUIRE(pitched[1].velocity == 90);
    REQUIRE(pitched[2].velocity == 80);

    // Verify byInsertOrder() returns chronological order
    auto ordered = buffer.byInsertOrder();
    REQUIRE(ordered.size() == 3);
    REQUIRE(ordered[0].note == 60);
    REQUIRE(ordered[1].note == 64);
    REQUIRE(ordered[2].note == 67);

    // Verify insertion order counters are monotonically increasing
    REQUIRE(ordered[0].insertOrder < ordered[1].insertOrder);
    REQUIRE(ordered[1].insertOrder < ordered[2].insertOrder);
}

TEST_CASE("HeldNoteBuffer - noteOff removes notes", "[held_note_buffer]") {
    HeldNoteBuffer buffer;

    buffer.noteOn(60, 100);
    buffer.noteOn(64, 90);
    buffer.noteOn(67, 80);

    // Remove the middle note
    buffer.noteOff(64);

    // Verify size decrements
    REQUIRE(buffer.size() == 2);

    // Verify byPitch() excludes removed note, order preserved
    auto pitched = buffer.byPitch();
    REQUIRE(pitched.size() == 2);
    REQUIRE(pitched[0].note == 60);
    REQUIRE(pitched[1].note == 67);

    // Verify byInsertOrder() excludes removed note, relative order preserved
    auto ordered = buffer.byInsertOrder();
    REQUIRE(ordered.size() == 2);
    REQUIRE(ordered[0].note == 60);
    REQUIRE(ordered[1].note == 67);

    // Verify remaining insertion order is still monotonically increasing
    REQUIRE(ordered[0].insertOrder < ordered[1].insertOrder);
}

TEST_CASE("HeldNoteBuffer - noteOn updates existing velocity", "[held_note_buffer]") {
    HeldNoteBuffer buffer;

    buffer.noteOn(60, 100);

    // Send duplicate noteOn with different velocity
    buffer.noteOn(60, 120);

    // Size stays 1 -- no duplicate
    REQUIRE(buffer.size() == 1);

    // Velocity is updated
    auto pitched = buffer.byPitch();
    REQUIRE(pitched[0].note == 60);
    REQUIRE(pitched[0].velocity == 120);

    auto ordered = buffer.byInsertOrder();
    REQUIRE(ordered[0].note == 60);
    REQUIRE(ordered[0].velocity == 120);
}

TEST_CASE("HeldNoteBuffer - capacity limit 32 notes", "[held_note_buffer]") {
    HeldNoteBuffer buffer;

    // Fill buffer to capacity
    for (uint8_t i = 0; i < 32; ++i) {
        buffer.noteOn(i, static_cast<uint8_t>(100));
    }
    REQUIRE(buffer.size() == 32);

    // Attempt to add a 33rd note (new pitch)
    buffer.noteOn(99, 100);

    // Size remains 32
    REQUIRE(buffer.size() == 32);

    // Verify all original 32 notes are still intact
    auto pitched = buffer.byPitch();
    REQUIRE(pitched.size() == 32);
    for (uint8_t i = 0; i < 32; ++i) {
        REQUIRE(pitched[i].note == i);
        REQUIRE(pitched[i].velocity == 100);
    }
}

TEST_CASE("HeldNoteBuffer - noteOff unknown note ignored", "[held_note_buffer]") {
    HeldNoteBuffer buffer;

    // noteOff on empty buffer -- no crash
    buffer.noteOff(99);
    REQUIRE(buffer.size() == 0);
    REQUIRE(buffer.empty());

    // Add a note, then noteOff a different pitch
    buffer.noteOn(60, 100);
    buffer.noteOff(64);
    REQUIRE(buffer.size() == 1);

    // Original note is still there
    auto pitched = buffer.byPitch();
    REQUIRE(pitched[0].note == 60);
}

TEST_CASE("HeldNoteBuffer - clear resets all state", "[held_note_buffer]") {
    HeldNoteBuffer buffer;

    buffer.noteOn(60, 100);
    buffer.noteOn(64, 90);
    buffer.noteOn(67, 80);

    buffer.clear();

    REQUIRE(buffer.empty());
    REQUIRE(buffer.size() == 0);
    REQUIRE(buffer.byPitch().empty());
    REQUIRE(buffer.byInsertOrder().empty());

    // Subsequent noteOn gets insertOrder == 0 (counter reset)
    buffer.noteOn(72, 110);
    auto ordered = buffer.byInsertOrder();
    REQUIRE(ordered.size() == 1);
    REQUIRE(ordered[0].insertOrder == 0);
}

TEST_CASE("HeldNoteBuffer - stress test 1000 operations", "[held_note_buffer]") {
    HeldNoteBuffer buffer;

    // 1000 rapid interleaved noteOn/noteOff operations
    // Pitches cycle through [0-31]
    for (int i = 0; i < 1000; ++i) {
        auto pitch = static_cast<uint8_t>(i % 32);
        if (i % 3 == 0) {
            buffer.noteOff(pitch);
        } else {
            buffer.noteOn(pitch, static_cast<uint8_t>((i % 127) + 1));
        }

        // After every operation, verify buffer integrity
        REQUIRE(buffer.size() <= 32);

        auto pitched = buffer.byPitch();
        auto ordered = buffer.byInsertOrder();

        // Both views must have the same size
        REQUIRE(pitched.size() == buffer.size());
        REQUIRE(ordered.size() == buffer.size());

        // Collect pitches from both views into sorted sets
        std::set<uint8_t> pitchSet;
        std::set<uint8_t> orderSet;
        for (size_t j = 0; j < buffer.size(); ++j) {
            pitchSet.insert(pitched[j].note);
            orderSet.insert(ordered[j].note);
        }

        // Both views contain exactly the same set of note pitches
        REQUIRE(pitchSet == orderSet);

        // Verify pitch-sorted view is actually sorted ascending
        for (size_t j = 1; j < pitched.size(); ++j) {
            REQUIRE(pitched[j - 1].note < pitched[j].note);
        }
    }
}

// =============================================================================
// NoteSelector Directional Mode Tests (User Story 2)
// =============================================================================

TEST_CASE("NoteSelector - Up mode cycles ascending", "[note_selector_directional]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(64, 90);   // E3
    held.noteOn(67, 80);   // G3

    NoteSelector selector;
    selector.setMode(ArpMode::Up);
    selector.setOctaveRange(1);

    // Call advance() 6 times, verify sequence is 60, 64, 67, 60, 64, 67
    std::array<uint8_t, 6> expected = {60, 64, 67, 60, 64, 67};
    for (size_t i = 0; i < 6; ++i) {
        auto result = selector.advance(held);
        REQUIRE(result.count == 1);
        REQUIRE(result.notes[0] == expected[i]);
    }
}

TEST_CASE("NoteSelector - Down mode cycles descending", "[note_selector_directional]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);
    held.noteOn(64, 90);
    held.noteOn(67, 80);

    NoteSelector selector;
    selector.setMode(ArpMode::Down);
    selector.setOctaveRange(1);

    // Call advance() 6 times, verify sequence is 67, 64, 60, 67, 64, 60
    std::array<uint8_t, 6> expected = {67, 64, 60, 67, 64, 60};
    for (size_t i = 0; i < 6; ++i) {
        auto result = selector.advance(held);
        REQUIRE(result.count == 1);
        REQUIRE(result.notes[0] == expected[i]);
    }
}

TEST_CASE("NoteSelector - UpDown mode no endpoint repeat", "[note_selector_directional]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);
    held.noteOn(64, 90);
    held.noteOn(67, 80);

    NoteSelector selector;
    selector.setMode(ArpMode::UpDown);
    selector.setOctaveRange(1);

    // Call advance() 8 times, verify: 60, 64, 67, 64, 60, 64, 67, 64
    std::array<uint8_t, 8> expected = {60, 64, 67, 64, 60, 64, 67, 64};
    for (size_t i = 0; i < 8; ++i) {
        auto result = selector.advance(held);
        REQUIRE(result.count == 1);
        REQUIRE(result.notes[0] == expected[i]);
    }
}

TEST_CASE("NoteSelector - DownUp mode no endpoint repeat", "[note_selector_directional]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);
    held.noteOn(64, 90);
    held.noteOn(67, 80);

    NoteSelector selector;
    selector.setMode(ArpMode::DownUp);
    selector.setOctaveRange(1);

    // Call advance() 8 times, verify: 67, 64, 60, 64, 67, 64, 60, 64
    std::array<uint8_t, 8> expected = {67, 64, 60, 64, 67, 64, 60, 64};
    for (size_t i = 0; i < 8; ++i) {
        auto result = selector.advance(held);
        REQUIRE(result.count == 1);
        REQUIRE(result.notes[0] == expected[i]);
    }
}

TEST_CASE("NoteSelector - UpDown edge cases 1 and 2 notes", "[note_selector_directional]") {
    SECTION("single note always returns 60") {
        HeldNoteBuffer held;
        held.noteOn(60, 100);

        NoteSelector selector;
        selector.setMode(ArpMode::UpDown);
        selector.setOctaveRange(1);

        for (int i = 0; i < 6; ++i) {
            auto result = selector.advance(held);
            REQUIRE(result.count == 1);
            REQUIRE(result.notes[0] == 60);
        }
    }

    SECTION("two notes gives simple alternation") {
        HeldNoteBuffer held;
        held.noteOn(60, 100);
        held.noteOn(67, 80);

        NoteSelector selector;
        selector.setMode(ArpMode::UpDown);
        selector.setOctaveRange(1);

        // 2-note UpDown: 60, 67, 60, 67
        std::array<uint8_t, 4> expected = {60, 67, 60, 67};
        for (size_t i = 0; i < 4; ++i) {
            auto result = selector.advance(held);
            REQUIRE(result.count == 1);
            REQUIRE(result.notes[0] == expected[i]);
        }
    }
}

// =============================================================================
// NoteSelector Converge/Diverge Mode Tests (User Story 3)
// =============================================================================

TEST_CASE("NoteSelector - Converge mode even count", "[note_selector_converge_diverge]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(62, 90);   // D3
    held.noteOn(64, 80);   // E3
    held.noteOn(67, 70);   // G3

    NoteSelector selector;
    selector.setMode(ArpMode::Converge);
    selector.setOctaveRange(1);

    // Converge: lowest, highest, second-lowest, second-highest
    // Expected: 60, 67, 62, 64
    std::array<uint8_t, 4> expected = {60, 67, 62, 64};
    for (size_t i = 0; i < 4; ++i) {
        auto result = selector.advance(held);
        REQUIRE(result.count == 1);
        REQUIRE(result.notes[0] == expected[i]);
    }
}

TEST_CASE("NoteSelector - Converge mode odd count", "[note_selector_converge_diverge]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(62, 90);   // D3
    held.noteOn(64, 80);   // E3

    NoteSelector selector;
    selector.setMode(ArpMode::Converge);
    selector.setOctaveRange(1);

    // Converge with 3 notes: lowest, highest, middle
    // Expected: 60, 64, 62
    std::array<uint8_t, 3> expected = {60, 64, 62};
    for (size_t i = 0; i < 3; ++i) {
        auto result = selector.advance(held);
        REQUIRE(result.count == 1);
        REQUIRE(result.notes[0] == expected[i]);
    }
}

TEST_CASE("NoteSelector - Converge mode pure wrap", "[note_selector_converge_diverge]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(62, 90);   // D3
    held.noteOn(64, 80);   // E3
    held.noteOn(67, 70);   // G3

    NoteSelector selector;
    selector.setMode(ArpMode::Converge);
    selector.setOctaveRange(1);

    // Two full cycles: 60, 67, 62, 64, 60, 67, 62, 64
    std::array<uint8_t, 8> expected = {60, 67, 62, 64, 60, 67, 62, 64};
    for (size_t i = 0; i < 8; ++i) {
        auto result = selector.advance(held);
        REQUIRE(result.count == 1);
        REQUIRE(result.notes[0] == expected[i]);
    }
}

TEST_CASE("NoteSelector - Diverge mode even count", "[note_selector_converge_diverge]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(62, 90);   // D3
    held.noteOn(64, 80);   // E3
    held.noteOn(67, 70);   // G3

    NoteSelector selector;
    selector.setMode(ArpMode::Diverge);
    selector.setOctaveRange(1);

    // Diverge with 4 notes (even): two center notes first, then expanding outward
    // Pitch-sorted: [60, 62, 64, 67], center = index 2, so center-1=1, center=2
    // Expected: 62, 64, 60, 67
    std::array<uint8_t, 4> expected = {62, 64, 60, 67};
    for (size_t i = 0; i < 4; ++i) {
        auto result = selector.advance(held);
        REQUIRE(result.count == 1);
        REQUIRE(result.notes[0] == expected[i]);
    }
}

TEST_CASE("NoteSelector - Diverge mode odd count", "[note_selector_converge_diverge]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(62, 90);   // D3
    held.noteOn(64, 80);   // E3

    NoteSelector selector;
    selector.setMode(ArpMode::Diverge);
    selector.setOctaveRange(1);

    // Diverge with 3 notes (odd): center, then expanding outward
    // Pitch-sorted: [60, 62, 64], center = index 1
    // Expected: 62, 60, 64
    std::array<uint8_t, 3> expected = {62, 60, 64};
    for (size_t i = 0; i < 3; ++i) {
        auto result = selector.advance(held);
        REQUIRE(result.count == 1);
        REQUIRE(result.notes[0] == expected[i]);
    }
}

// =============================================================================
// NoteSelector Random/Walk Mode Tests (User Story 4)
// =============================================================================

TEST_CASE("NoteSelector - Random mode distribution", "[note_selector_random_walk]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(64, 90);   // E3
    held.noteOn(67, 80);   // G3

    NoteSelector selector(42);  // Fixed seed for determinism
    selector.setMode(ArpMode::Random);
    selector.setOctaveRange(1);

    // Call advance() 3000 times, count selections per note
    std::array<int, 3> counts = {0, 0, 0};
    const std::array<uint8_t, 3> noteValues = {60, 64, 67};

    for (int i = 0; i < 3000; ++i) {
        auto result = selector.advance(held);
        REQUIRE(result.count == 1);

        // Find which note was selected
        bool found = false;
        for (size_t n = 0; n < 3; ++n) {
            if (result.notes[0] == noteValues[n]) {
                counts[n]++;
                found = true;
                break;
            }
        }
        REQUIRE(found);  // Must be one of the held notes
    }

    // SC-005: Each count should be within 10% of 1000 (expected 33.3%)
    // 10% of 1000 = 100, so each count should be in [900, 1100]
    for (size_t n = 0; n < 3; ++n) {
        INFO("Note " << static_cast<int>(noteValues[n]) << " selected " << counts[n] << " times");
        REQUIRE(counts[n] >= 900);
        REQUIRE(counts[n] <= 1100);
    }
}

TEST_CASE("NoteSelector - Walk mode bounds", "[note_selector_random_walk]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(64, 90);   // E3
    held.noteOn(67, 80);   // G3
    held.noteOn(71, 70);   // B3

    NoteSelector selector(42);  // Fixed seed
    selector.setMode(ArpMode::Walk);
    selector.setOctaveRange(1);

    // SC-006: Call advance() 1000 times, verify every returned note
    // is within [0, 3] index range (i.e., is one of the held notes)
    const std::set<uint8_t> validNotes = {60, 64, 67, 71};

    for (int i = 0; i < 1000; ++i) {
        auto result = selector.advance(held);
        REQUIRE(result.count == 1);
        INFO("Iteration " << i << ": note = " << static_cast<int>(result.notes[0]));
        REQUIRE(validNotes.count(result.notes[0]) == 1);
    }
}

TEST_CASE("NoteSelector - Walk mode step size always 1", "[note_selector_random_walk]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(64, 90);   // E3
    held.noteOn(67, 80);   // G3

    NoteSelector selector(42);  // Fixed seed
    selector.setMode(ArpMode::Walk);
    selector.setOctaveRange(1);

    // Collect 100 results
    const auto pitched = held.byPitch();
    std::vector<size_t> indices;
    indices.reserve(100);

    for (int i = 0; i < 100; ++i) {
        auto result = selector.advance(held);
        REQUIRE(result.count == 1);

        // Find the index of the returned note in byPitch()
        size_t noteIdx = 0;
        for (size_t p = 0; p < pitched.size(); ++p) {
            if (pitched[p].note == result.notes[0]) {
                noteIdx = p;
                break;
            }
        }
        indices.push_back(noteIdx);
    }

    // Verify successive note index differences are 0 or 1 (never >= 2)
    // Difference of 0 can occur from boundary clamping (-1 at index 0 or +1 at max index)
    for (size_t i = 1; i < indices.size(); ++i) {
        size_t diff = (indices[i] > indices[i - 1])
                          ? (indices[i] - indices[i - 1])
                          : (indices[i - 1] - indices[i]);
        INFO("Step " << i << ": index " << indices[i - 1] << " -> " << indices[i]
                     << " (diff = " << diff << ")");
        REQUIRE(diff <= 1);
    }
}

// =============================================================================
// NoteSelector AsPlayed/Chord Mode Tests (User Story 5)
// =============================================================================

TEST_CASE("NoteSelector - AsPlayed mode insertion order", "[note_selector_asplayed_chord]") {
    HeldNoteBuffer held;
    // Press notes in non-pitch order: G3, C3, E3
    held.noteOn(67, 80);   // G3 first
    held.noteOn(60, 100);  // C3 second
    held.noteOn(64, 90);   // E3 third

    NoteSelector selector;
    selector.setMode(ArpMode::AsPlayed);
    selector.setOctaveRange(1);

    // Call advance() 3 times, verify sequence is 67, 60, 64 (insertion order, not pitch order)
    std::array<uint8_t, 3> expected = {67, 60, 64};
    for (size_t i = 0; i < 3; ++i) {
        auto result = selector.advance(held);
        REQUIRE(result.count == 1);
        REQUIRE(result.notes[0] == expected[i]);
    }

    // Verify wrapping: next 3 should repeat
    for (size_t i = 0; i < 3; ++i) {
        auto result = selector.advance(held);
        REQUIRE(result.count == 1);
        REQUIRE(result.notes[0] == expected[i]);
    }
}

TEST_CASE("NoteSelector - Chord mode returns all notes", "[note_selector_asplayed_chord]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(64, 90);   // E3
    held.noteOn(67, 80);   // G3

    NoteSelector selector;
    selector.setMode(ArpMode::Chord);
    selector.setOctaveRange(1);

    auto result = selector.advance(held);

    // FR-020: Chord mode returns all notes simultaneously
    REQUIRE(result.count == 3);

    // Notes should be from byPitch() -- verify pitches 60, 64, 67 are present
    REQUIRE(result.notes[0] == 60);
    REQUIRE(result.notes[1] == 64);
    REQUIRE(result.notes[2] == 67);

    // FR-024: Velocities must be correctly populated
    REQUIRE(result.velocities[0] == 100);
    REQUIRE(result.velocities[1] == 90);
    REQUIRE(result.velocities[2] == 80);
}

TEST_CASE("NoteSelector - Chord mode ignores octave range", "[note_selector_asplayed_chord]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(64, 90);   // E3

    NoteSelector selector;
    selector.setMode(ArpMode::Chord);
    selector.setOctaveRange(4);  // FR-020: Chord ignores octave range

    auto result = selector.advance(held);

    // Should still return exactly 2 notes at original pitch, no transposition
    REQUIRE(result.count == 2);
    REQUIRE(result.notes[0] == 60);
    REQUIRE(result.notes[1] == 64);
}

TEST_CASE("NoteSelector - Chord mode repeatable", "[note_selector_asplayed_chord]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(64, 90);   // E3
    held.noteOn(67, 80);   // G3

    NoteSelector selector;
    selector.setMode(ArpMode::Chord);
    selector.setOctaveRange(1);

    // Call advance() 5 times, verify all 5 results have count == 3 with same pitches
    for (int i = 0; i < 5; ++i) {
        auto result = selector.advance(held);
        REQUIRE(result.count == 3);
        REQUIRE(result.notes[0] == 60);
        REQUIRE(result.notes[1] == 64);
        REQUIRE(result.notes[2] == 67);
    }
}

// =============================================================================
// NoteSelector Octave Mode Tests (User Story 6)
// =============================================================================

TEST_CASE("NoteSelector - Sequential octave mode", "[note_selector_octave]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(64, 90);   // E3

    NoteSelector selector;
    selector.setMode(ArpMode::Up);
    selector.setOctaveRange(3);
    selector.setOctaveMode(OctaveMode::Sequential);

    // Sequential: full pattern at octave 0, then octave +1, then octave +2
    // Expected: 60, 64, 72, 76, 84, 88
    std::array<uint8_t, 6> expected = {60, 64, 72, 76, 84, 88};
    for (size_t i = 0; i < 6; ++i) {
        auto result = selector.advance(held);
        INFO("Step " << i << ": expected " << static_cast<int>(expected[i])
                     << ", got " << static_cast<int>(result.notes[0]));
        REQUIRE(result.count == 1);
        REQUIRE(result.notes[0] == expected[i]);
    }
}

TEST_CASE("NoteSelector - Interleaved octave mode", "[note_selector_octave]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(64, 90);   // E3

    NoteSelector selector;
    selector.setMode(ArpMode::Up);
    selector.setOctaveRange(3);
    selector.setOctaveMode(OctaveMode::Interleaved);

    // Interleaved: each note at all octave transpositions before next note
    // Expected: 60, 72, 84, 64, 76, 88
    std::array<uint8_t, 6> expected = {60, 72, 84, 64, 76, 88};
    for (size_t i = 0; i < 6; ++i) {
        auto result = selector.advance(held);
        INFO("Step " << i << ": expected " << static_cast<int>(expected[i])
                     << ", got " << static_cast<int>(result.notes[0]));
        REQUIRE(result.count == 1);
        REQUIRE(result.notes[0] == expected[i]);
    }
}

TEST_CASE("NoteSelector - octave range 1 no transposition", "[note_selector_octave]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(64, 90);   // E3
    held.noteOn(67, 80);   // G3

    NoteSelector selector;
    selector.setMode(ArpMode::Up);
    selector.setOctaveRange(1);  // Default -- no transposition
    selector.setOctaveMode(OctaveMode::Sequential);

    // With octave range 1, all returned notes should be in [60, 64, 67] with no +12 offset
    std::array<uint8_t, 6> expected = {60, 64, 67, 60, 64, 67};
    for (size_t i = 0; i < 6; ++i) {
        auto result = selector.advance(held);
        REQUIRE(result.count == 1);
        REQUIRE(result.notes[0] == expected[i]);
    }
}

TEST_CASE("NoteSelector - Down mode octave range 2 sequential", "[note_selector_octave]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(64, 90);   // E3
    held.noteOn(67, 80);   // G3

    NoteSelector selector;
    selector.setMode(ArpMode::Down);
    selector.setOctaveRange(2);
    selector.setOctaveMode(OctaveMode::Sequential);

    // Down mode with Sequential octave range 2:
    // Descending through upper octave first (octave 1), then lower (octave 0)
    // Upper octave: 67+12=79, 64+12=76, 60+12=72
    // Lower octave: 67, 64, 60
    // Expected: 79, 76, 72, 67, 64, 60
    std::array<uint8_t, 6> expected = {79, 76, 72, 67, 64, 60};
    for (size_t i = 0; i < 6; ++i) {
        auto result = selector.advance(held);
        INFO("Step " << i << ": expected " << static_cast<int>(expected[i])
                     << ", got " << static_cast<int>(result.notes[0]));
        REQUIRE(result.count == 1);
        REQUIRE(result.notes[0] == expected[i]);
    }
}

TEST_CASE("NoteSelector - MIDI note clamped to 127", "[note_selector_octave]") {
    HeldNoteBuffer held;
    held.noteOn(120, 100);

    NoteSelector selector;
    selector.setMode(ArpMode::Up);
    selector.setOctaveRange(4);
    selector.setOctaveMode(OctaveMode::Sequential);

    // FR-028: Note 120, range 4:
    // octave 0: 120
    // octave 1: min(127, 120+12) = 127
    // octave 2: min(127, 120+24) = 127
    // octave 3: min(127, 120+36) = 127
    // Expected: 120, 127, 127, 127
    std::array<uint8_t, 4> expected = {120, 127, 127, 127};
    for (size_t i = 0; i < 4; ++i) {
        auto result = selector.advance(held);
        INFO("Step " << i << ": expected " << static_cast<int>(expected[i])
                     << ", got " << static_cast<int>(result.notes[0]));
        REQUIRE(result.count == 1);
        REQUIRE(result.notes[0] == expected[i]);
    }
}

// =============================================================================
// NoteSelector Reset Tests (User Story 7 -- FR-025)
// =============================================================================

TEST_CASE("NoteSelector - reset returns Up to start", "[note_selector_reset]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(64, 90);   // E3
    held.noteOn(67, 80);   // G3

    NoteSelector selector;
    selector.setMode(ArpMode::Up);
    selector.setOctaveRange(1);

    // Advance to index 2 (at G3=67)
    auto r0 = selector.advance(held);
    REQUIRE(r0.notes[0] == 60);
    auto r1 = selector.advance(held);
    REQUIRE(r1.notes[0] == 64);
    auto r2 = selector.advance(held);
    REQUIRE(r2.notes[0] == 67);

    // Now noteIndex_ has wrapped to 0, but let's advance once more to be at index 1
    // Actually, after 3 advances in Up mode with 3 notes, noteIndex_ wraps to 0.
    // Let's advance 2 more to be at index 2 again.
    auto r3 = selector.advance(held);
    REQUIRE(r3.notes[0] == 60);
    auto r4 = selector.advance(held);
    REQUIRE(r4.notes[0] == 64);

    // Now we are past the start. Call reset().
    selector.reset();

    // Next advance() should return 60 (start of ascending pattern)
    auto result = selector.advance(held);
    REQUIRE(result.count == 1);
    REQUIRE(result.notes[0] == 60);
}

TEST_CASE("NoteSelector - reset restores UpDown direction", "[note_selector_reset]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(64, 90);   // E3
    held.noteOn(67, 80);   // G3

    NoteSelector selector;
    selector.setMode(ArpMode::UpDown);
    selector.setOctaveRange(1);

    // UpDown with 3 notes: 60, 64, 67, 64, 60, 64, 67, 64, ...
    // Advance 3 times to reach the descending phase
    auto r0 = selector.advance(held);
    REQUIRE(r0.notes[0] == 60);  // pos=0, ascending
    auto r1 = selector.advance(held);
    REQUIRE(r1.notes[0] == 64);  // pos=1, ascending
    auto r2 = selector.advance(held);
    REQUIRE(r2.notes[0] == 67);  // pos=2, at top
    auto r3 = selector.advance(held);
    REQUIRE(r3.notes[0] == 64);  // pos=3, descending

    // We are now in the descending phase. Call reset().
    selector.reset();

    // Next advance() should return 60 (ascending direction restored, start of pattern)
    auto result = selector.advance(held);
    REQUIRE(result.count == 1);
    REQUIRE(result.notes[0] == 60);
}

TEST_CASE("NoteSelector - reset restores Walk to index 0", "[note_selector_reset]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(64, 90);   // E3
    held.noteOn(67, 80);   // G3

    NoteSelector selector(42);  // Fixed seed
    selector.setMode(ArpMode::Walk);
    selector.setOctaveRange(1);

    // Advance walk several times to move away from index 0
    for (int i = 0; i < 20; ++i) {
        (void)selector.advance(held);
    }

    // Call reset()
    selector.reset();

    // After reset, walkIndex_ is 0, so the walk step (-1 or +1) is applied from 0.
    // If step is -1, clamped to 0, so note at index 0 = 60.
    // If step is +1, walkIndex becomes 1, so note at index 1 = 64.
    // Either way, the note should be 60 (index 0, clamped) or 64 (index 1).
    // The key point is that it starts from index 0, not wherever walk had wandered.
    auto result = selector.advance(held);
    REQUIRE(result.count == 1);
    // Walk starts at walkIndex_=0, applies +/-1 step, clamps.
    // Result must be note at index 0 or index 1 (i.e., 60 or 64)
    bool isAtStartRegion = (result.notes[0] == 60 || result.notes[0] == 64);
    INFO("After reset, Walk returned note " << static_cast<int>(result.notes[0])
         << " (expected 60 or 64, starting from walkIndex_=0)");
    REQUIRE(isAtStartRegion);
}

TEST_CASE("NoteSelector - reset resets octave offset", "[note_selector_reset]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(64, 90);   // E3

    NoteSelector selector;
    selector.setMode(ArpMode::Up);
    selector.setOctaveRange(2);
    selector.setOctaveMode(OctaveMode::Sequential);

    // Sequential Up with octave range 2 and 2 notes:
    // 60, 64 (octave 0), 72, 76 (octave 1), 60, 64, ...
    // Advance until we are in octave 1
    auto r0 = selector.advance(held);
    REQUIRE(r0.notes[0] == 60);  // octave 0
    auto r1 = selector.advance(held);
    REQUIRE(r1.notes[0] == 64);  // octave 0
    auto r2 = selector.advance(held);
    REQUIRE(r2.notes[0] == 72);  // octave 1 (60 + 12)

    // Now octaveOffset_ == 1. Call reset().
    selector.reset();

    // Next advance() should return 60 (octave 0, no transposition)
    auto result = selector.advance(held);
    REQUIRE(result.count == 1);
    REQUIRE(result.notes[0] == 60);
}

// =============================================================================
// NoteSelector Edge Case Tests (Phase 10 -- FR-024 through FR-027, FR-029)
// =============================================================================

TEST_CASE("NoteSelector - empty buffer returns count 0 all modes", "[note_selector_edge_cases]") {
    HeldNoteBuffer emptyBuffer;  // No notes added

    // FR-026: advance() with empty buffer must return count==0 for ALL 10 ArpMode values
    const std::array<ArpMode, 10> allModes = {
        ArpMode::Up,
        ArpMode::Down,
        ArpMode::UpDown,
        ArpMode::DownUp,
        ArpMode::Converge,
        ArpMode::Diverge,
        ArpMode::Random,
        ArpMode::Walk,
        ArpMode::AsPlayed,
        ArpMode::Chord
    };

    for (size_t m = 0; m < allModes.size(); ++m) {
        NoteSelector selector(42);
        selector.setMode(allModes[m]);
        auto result = selector.advance(emptyBuffer);
        INFO("Mode index " << m << ": expected count=0, got count=" << result.count);
        REQUIRE(result.count == 0);
    }
}

TEST_CASE("NoteSelector - index clamped on buffer shrink", "[note_selector_edge_cases]") {
    // FR-027: When notes are removed mid-pattern, index is clamped to min(index, newSize-1)
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3
    held.noteOn(64, 90);   // E3
    held.noteOn(67, 80);   // G3

    NoteSelector selector;
    selector.setMode(ArpMode::Up);
    selector.setOctaveRange(1);

    // Advance to index 2 (at G3=67)
    auto r0 = selector.advance(held);
    REQUIRE(r0.notes[0] == 60);
    auto r1 = selector.advance(held);
    REQUIRE(r1.notes[0] == 64);
    auto r2 = selector.advance(held);
    REQUIRE(r2.notes[0] == 67);

    // Now noteIndex_ has wrapped to 0. Advance once more to reach index 1.
    // Actually after 3 advances noteIndex_ wrapped to 0. Advance 2 more to reach index 2.
    auto r3 = selector.advance(held);
    REQUIRE(r3.notes[0] == 60);  // index 0
    auto r4 = selector.advance(held);
    REQUIRE(r4.notes[0] == 64);  // index 1, noteIndex_ will be 2 after this

    // noteIndex_ is now 2 (about to read G3). Remove G3 to shrink buffer to 2 notes.
    held.noteOff(67);
    REQUIRE(held.size() == 2);

    // Next advance() must return a valid note from [60, 64], not crash or go OOB
    auto result = selector.advance(held);
    REQUIRE(result.count == 1);
    bool validNote = (result.notes[0] == 60 || result.notes[0] == 64);
    INFO("After shrink, got note " << static_cast<int>(result.notes[0])
         << " (expected 60 or 64)");
    REQUIRE(validNote);
}

TEST_CASE("NoteSelector - single note all modes", "[note_selector_edge_cases]") {
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // Single note C3

    const std::array<ArpMode, 10> allModes = {
        ArpMode::Up,
        ArpMode::Down,
        ArpMode::UpDown,
        ArpMode::DownUp,
        ArpMode::Converge,
        ArpMode::Diverge,
        ArpMode::Random,
        ArpMode::Walk,
        ArpMode::AsPlayed,
        ArpMode::Chord
    };

    for (size_t m = 0; m < allModes.size(); ++m) {
        NoteSelector selector(42);
        selector.setMode(allModes[m]);
        selector.setOctaveRange(1);

        if (allModes[m] == ArpMode::Chord) {
            // Chord: count == 1, note is 60
            auto result = selector.advance(held);
            INFO("Chord mode: expected count=1, note=60");
            REQUIRE(result.count == 1);
            REQUIRE(result.notes[0] == 60);
        } else {
            // All other modes: advance() 10 times must always return note 60
            for (int i = 0; i < 10; ++i) {
                auto result = selector.advance(held);
                INFO("Mode " << m << ", step " << i
                     << ": expected count=1 note=60, got count="
                     << result.count << " note=" << static_cast<int>(result.notes[0]));
                REQUIRE(result.count == 1);
                REQUIRE(result.notes[0] == 60);
            }
        }
    }
}

TEST_CASE("NoteSelector - all modes with 2 notes", "[note_selector_edge_cases]") {
    // SC-001: Test all 10 modes with exactly 2 notes [60, 67]
    HeldNoteBuffer held;
    held.noteOn(60, 100);  // C3 (added first -- insertion order 0)
    held.noteOn(67, 80);   // G3 (added second -- insertion order 1)

    SECTION("Up: 60, 67, 60, 67, 60, 67") {
        NoteSelector selector;
        selector.setMode(ArpMode::Up);
        selector.setOctaveRange(1);

        std::array<uint8_t, 6> expected = {60, 67, 60, 67, 60, 67};
        for (size_t i = 0; i < 6; ++i) {
            auto result = selector.advance(held);
            REQUIRE(result.count == 1);
            REQUIRE(result.notes[0] == expected[i]);
        }
    }

    SECTION("Down: 67, 60, 67, 60, 67, 60") {
        NoteSelector selector;
        selector.setMode(ArpMode::Down);
        selector.setOctaveRange(1);

        std::array<uint8_t, 6> expected = {67, 60, 67, 60, 67, 60};
        for (size_t i = 0; i < 6; ++i) {
            auto result = selector.advance(held);
            REQUIRE(result.count == 1);
            REQUIRE(result.notes[0] == expected[i]);
        }
    }

    SECTION("UpDown: 60, 67, 60, 67, 60, 67 (2-note ping-pong)") {
        NoteSelector selector;
        selector.setMode(ArpMode::UpDown);
        selector.setOctaveRange(1);

        std::array<uint8_t, 6> expected = {60, 67, 60, 67, 60, 67};
        for (size_t i = 0; i < 6; ++i) {
            auto result = selector.advance(held);
            INFO("UpDown step " << i << ": expected " << static_cast<int>(expected[i])
                 << ", got " << static_cast<int>(result.notes[0]));
            REQUIRE(result.count == 1);
            REQUIRE(result.notes[0] == expected[i]);
        }
    }

    SECTION("DownUp: 67, 60, 67, 60, 67, 60 (2-note ping-pong)") {
        NoteSelector selector;
        selector.setMode(ArpMode::DownUp);
        selector.setOctaveRange(1);

        std::array<uint8_t, 6> expected = {67, 60, 67, 60, 67, 60};
        for (size_t i = 0; i < 6; ++i) {
            auto result = selector.advance(held);
            INFO("DownUp step " << i << ": expected " << static_cast<int>(expected[i])
                 << ", got " << static_cast<int>(result.notes[0]));
            REQUIRE(result.count == 1);
            REQUIRE(result.notes[0] == expected[i]);
        }
    }

    SECTION("Converge: 60, 67, 60, 67, 60, 67") {
        NoteSelector selector;
        selector.setMode(ArpMode::Converge);
        selector.setOctaveRange(1);

        std::array<uint8_t, 6> expected = {60, 67, 60, 67, 60, 67};
        for (size_t i = 0; i < 6; ++i) {
            auto result = selector.advance(held);
            REQUIRE(result.count == 1);
            REQUIRE(result.notes[0] == expected[i]);
        }
    }

    SECTION("Diverge: 60, 67, 60, 67, 60, 67") {
        NoteSelector selector;
        selector.setMode(ArpMode::Diverge);
        selector.setOctaveRange(1);

        std::array<uint8_t, 6> expected = {60, 67, 60, 67, 60, 67};
        for (size_t i = 0; i < 6; ++i) {
            auto result = selector.advance(held);
            INFO("Diverge step " << i << ": expected " << static_cast<int>(expected[i])
                 << ", got " << static_cast<int>(result.notes[0]));
            REQUIRE(result.count == 1);
            REQUIRE(result.notes[0] == expected[i]);
        }
    }

    SECTION("Walk: never returns outside [60, 67]") {
        NoteSelector selector(42);
        selector.setMode(ArpMode::Walk);
        selector.setOctaveRange(1);

        for (int i = 0; i < 100; ++i) {
            auto result = selector.advance(held);
            REQUIRE(result.count == 1);
            bool valid = (result.notes[0] == 60 || result.notes[0] == 67);
            INFO("Walk step " << i << ": got " << static_cast<int>(result.notes[0]));
            REQUIRE(valid);
        }
    }

    SECTION("Random: returns only 60 or 67") {
        NoteSelector selector(42);
        selector.setMode(ArpMode::Random);
        selector.setOctaveRange(1);

        for (int i = 0; i < 100; ++i) {
            auto result = selector.advance(held);
            REQUIRE(result.count == 1);
            bool valid = (result.notes[0] == 60 || result.notes[0] == 67);
            INFO("Random step " << i << ": got " << static_cast<int>(result.notes[0]));
            REQUIRE(valid);
        }
    }

    SECTION("AsPlayed: follows insertion order 60, 67, 60, 67, ...") {
        NoteSelector selector;
        selector.setMode(ArpMode::AsPlayed);
        selector.setOctaveRange(1);

        std::array<uint8_t, 6> expected = {60, 67, 60, 67, 60, 67};
        for (size_t i = 0; i < 6; ++i) {
            auto result = selector.advance(held);
            REQUIRE(result.count == 1);
            REQUIRE(result.notes[0] == expected[i]);
        }
    }

    SECTION("Chord: count == 2") {
        NoteSelector selector;
        selector.setMode(ArpMode::Chord);
        selector.setOctaveRange(1);

        auto result = selector.advance(held);
        REQUIRE(result.count == 2);
        REQUIRE(result.notes[0] == 60);
        REQUIRE(result.notes[1] == 67);
    }
}
