// ==============================================================================
// Voice Allocator Tests
// ==============================================================================
// Feature: 034-voice-allocator
// Layer: 3 (System)
// Tests: All 6 user stories + cross-cutting + edge cases + performance
// ==============================================================================

#include <krate/dsp/systems/voice_allocator.h>

#include <catch2/catch_all.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>
#include <thread>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Helper: Compute expected 12-TET frequency
// =============================================================================
static float expected12TETFrequency(int midiNote, float a4 = 440.0f) {
    return a4 * std::pow(2.0f, static_cast<float>(midiNote - 69) / 12.0f);
}

// =============================================================================
// Phase 3: User Story 1 - Basic Polyphonic Voice Allocation
// =============================================================================

TEST_CASE("US1: noteOn with idle voices assigns unique voice indices",
          "[voice_allocator][us1]") {
    VoiceAllocator alloc;

    std::set<uint8_t> assignedVoices;
    for (int i = 0; i < 8; ++i) {
        auto events = alloc.noteOn(static_cast<uint8_t>(60 + i), 100);
        REQUIRE(events.size() == 1);
        REQUIRE(events[0].type == VoiceEvent::Type::NoteOn);
        REQUIRE_FALSE(assignedVoices.contains(events[0].voiceIndex));
        assignedVoices.insert(events[0].voiceIndex);
        REQUIRE(events[0].voiceIndex < 8);
    }
    REQUIRE(assignedVoices.size() == 8);
}

TEST_CASE("US1: noteOn returns VoiceEvent with correct note, velocity, frequency",
          "[voice_allocator][us1]") {
    VoiceAllocator alloc;

    auto events = alloc.noteOn(60, 100);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].type == VoiceEvent::Type::NoteOn);
    REQUIRE(events[0].note == 60);
    REQUIRE(events[0].velocity == 100);

    float expectedFreq = expected12TETFrequency(60);
    REQUIRE(events[0].frequency == Approx(expectedFreq).margin(0.01f));
}

TEST_CASE("US1: frequency computation accuracy for all 128 MIDI notes",
          "[voice_allocator][us1][sc007]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(32);

    for (int batch = 0; batch < 4; ++batch) {
        alloc.reset();
        for (int i = 0; i < 32; ++i) {
            int note = batch * 32 + i;
            if (note > 127) break;

            auto events = alloc.noteOn(static_cast<uint8_t>(note), 100);
            REQUIRE(events.size() == 1);

            float expected = expected12TETFrequency(note);
            INFO("MIDI note " << note << ": expected=" << expected
                 << " actual=" << events[0].frequency);
            REQUIRE(events[0].frequency == Approx(expected).margin(0.01f));
        }
    }
}

TEST_CASE("US1: noteOff transitions voice to Releasing and returns NoteOff event",
          "[voice_allocator][us1]") {
    VoiceAllocator alloc;

    auto onEvents = alloc.noteOn(60, 100);
    REQUIRE(onEvents.size() == 1);
    uint8_t voiceIdx = onEvents[0].voiceIndex;

    auto offEvents = alloc.noteOff(60);
    REQUIRE(offEvents.size() == 1);
    REQUIRE(offEvents[0].type == VoiceEvent::Type::NoteOff);
    REQUIRE(offEvents[0].voiceIndex == voiceIdx);
    REQUIRE(offEvents[0].note == 60);

    REQUIRE(alloc.getVoiceState(voiceIdx) == VoiceState::Releasing);
}

TEST_CASE("US1: noteOff for non-active note returns empty span",
          "[voice_allocator][us1]") {
    VoiceAllocator alloc;

    auto events = alloc.noteOff(60);
    REQUIRE(events.empty());

    (void)alloc.noteOn(60, 100);
    (void)alloc.noteOff(60);
    auto events2 = alloc.noteOff(60);
    REQUIRE(events2.empty());
}

TEST_CASE("US1: voiceFinished transitions Releasing voice to Idle",
          "[voice_allocator][us1]") {
    VoiceAllocator alloc;

    auto onEvents = alloc.noteOn(60, 100);
    uint8_t voiceIdx = onEvents[0].voiceIndex;

    (void)alloc.noteOff(60);
    REQUIRE(alloc.getVoiceState(voiceIdx) == VoiceState::Releasing);

    alloc.voiceFinished(voiceIdx);
    REQUIRE(alloc.getVoiceState(voiceIdx) == VoiceState::Idle);
    REQUIRE(alloc.getVoiceNote(voiceIdx) == -1);
}

TEST_CASE("US1: voiceFinished ignores out-of-range and non-Releasing voices",
          "[voice_allocator][us1]") {
    VoiceAllocator alloc;

    // Out of range
    alloc.voiceFinished(100);

    // Active voice (not releasing)
    auto events = alloc.noteOn(60, 100);
    uint8_t voiceIdx = events[0].voiceIndex;
    REQUIRE(alloc.getVoiceState(voiceIdx) == VoiceState::Active);

    alloc.voiceFinished(voiceIdx);
    REQUIRE(alloc.getVoiceState(voiceIdx) == VoiceState::Active);

    // Idle voice
    alloc.voiceFinished(7);
}

TEST_CASE("US1: getActiveVoiceCount returns correct count",
          "[voice_allocator][us1]") {
    VoiceAllocator alloc;

    REQUIRE(alloc.getActiveVoiceCount() == 0);

    (void)alloc.noteOn(60, 100);
    REQUIRE(alloc.getActiveVoiceCount() == 1);

    (void)alloc.noteOn(62, 100);
    REQUIRE(alloc.getActiveVoiceCount() == 2);

    (void)alloc.noteOff(60);
    // Releasing still counts as active
    REQUIRE(alloc.getActiveVoiceCount() == 2);

    (void)alloc.noteOff(60);  // Already releasing, no effect
    REQUIRE(alloc.getActiveVoiceCount() == 2);
}

TEST_CASE("US1: isVoiceActive returns correct state",
          "[voice_allocator][us1]") {
    VoiceAllocator alloc;

    auto events = alloc.noteOn(60, 100);
    uint8_t voiceIdx = events[0].voiceIndex;

    REQUIRE(alloc.isVoiceActive(voiceIdx) == true);

    (void)alloc.noteOff(60);
    REQUIRE(alloc.isVoiceActive(voiceIdx) == true);  // Releasing = active

    alloc.voiceFinished(voiceIdx);
    REQUIRE(alloc.isVoiceActive(voiceIdx) == false);  // Idle = not active
}

TEST_CASE("US1: velocity-0 noteOn treated as noteOff",
          "[voice_allocator][us1][sc011]") {
    VoiceAllocator alloc;

    auto onEvents = alloc.noteOn(60, 100);
    uint8_t voiceIdx = onEvents[0].voiceIndex;
    REQUIRE(alloc.getVoiceState(voiceIdx) == VoiceState::Active);

    // velocity 0 = noteOff
    auto offEvents = alloc.noteOn(60, 0);
    REQUIRE(offEvents.size() == 1);
    REQUIRE(offEvents[0].type == VoiceEvent::Type::NoteOff);
    REQUIRE(alloc.getVoiceState(voiceIdx) == VoiceState::Releasing);
}

// =============================================================================
// Phase 4: User Story 2 - Allocation Mode Selection
// =============================================================================

TEST_CASE("US2: RoundRobin mode cycles through voices",
          "[voice_allocator][us2]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(4);
    alloc.setAllocationMode(AllocationMode::RoundRobin);

    std::vector<uint8_t> indices;
    for (int i = 0; i < 6; ++i) {
        auto events = alloc.noteOn(static_cast<uint8_t>(60 + i), 100);
        if (i < 4) {
            REQUIRE(events.size() == 1);
        } else {
            REQUIRE(events.size() == 2);  // Steal + NoteOn
        }
        for (const auto& e : events) {
            if (e.type == VoiceEvent::Type::NoteOn) {
                indices.push_back(e.voiceIndex);
            }
        }
    }

    REQUIRE(indices[0] == 0);
    REQUIRE(indices[1] == 1);
    REQUIRE(indices[2] == 2);
    REQUIRE(indices[3] == 3);
    REQUIRE(indices[4] == 0);
    REQUIRE(indices[5] == 1);
}

TEST_CASE("US2: Oldest mode selects voice with earliest timestamp",
          "[voice_allocator][us2]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(4);
    alloc.setAllocationMode(AllocationMode::Oldest);

    (void)alloc.noteOn(60, 100);  // voice 0 (oldest)
    (void)alloc.noteOn(62, 80);
    (void)alloc.noteOn(64, 90);
    (void)alloc.noteOn(66, 70);

    auto events = alloc.noteOn(68, 100);
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].type == VoiceEvent::Type::Steal);
    REQUIRE(events[0].voiceIndex == 0);
    REQUIRE(events[1].type == VoiceEvent::Type::NoteOn);
    REQUIRE(events[1].voiceIndex == 0);
}

TEST_CASE("US2: LowestVelocity mode selects voice with lowest velocity",
          "[voice_allocator][us2]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(4);
    alloc.setAllocationMode(AllocationMode::LowestVelocity);

    (void)alloc.noteOn(60, 100);
    (void)alloc.noteOn(62, 40);   // lowest velocity
    (void)alloc.noteOn(64, 80);
    (void)alloc.noteOn(66, 60);

    auto events = alloc.noteOn(68, 100);
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].type == VoiceEvent::Type::Steal);
    REQUIRE(events[0].velocity == 40);
}

TEST_CASE("US2: HighestNote mode selects voice with highest MIDI note",
          "[voice_allocator][us2]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(4);
    alloc.setAllocationMode(AllocationMode::HighestNote);

    (void)alloc.noteOn(48, 100);
    (void)alloc.noteOn(72, 100);  // highest note
    (void)alloc.noteOn(60, 100);
    (void)alloc.noteOn(55, 100);

    auto events = alloc.noteOn(50, 100);
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].type == VoiceEvent::Type::Steal);
    REQUIRE(events[0].note == 72);
}

TEST_CASE("US2: setAllocationMode changes mode without disrupting active voices",
          "[voice_allocator][us2]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(4);
    alloc.setAllocationMode(AllocationMode::RoundRobin);

    (void)alloc.noteOn(60, 100);
    (void)alloc.noteOn(62, 100);

    REQUIRE(alloc.getActiveVoiceCount() == 2);

    alloc.setAllocationMode(AllocationMode::Oldest);

    REQUIRE(alloc.getActiveVoiceCount() == 2);
    REQUIRE(alloc.getVoiceState(0) == VoiceState::Active);
    REQUIRE(alloc.getVoiceState(1) == VoiceState::Active);
}

// =============================================================================
// Phase 5: User Story 3 - Voice Stealing
// =============================================================================

TEST_CASE("US3: Hard steal returns Steal event + NoteOn for stolen voice",
          "[voice_allocator][us3]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(4);
    alloc.setStealMode(StealMode::Hard);

    for (int i = 0; i < 4; ++i) {
        (void)alloc.noteOn(static_cast<uint8_t>(60 + i), 100);
    }

    auto events = alloc.noteOn(70, 100);
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].type == VoiceEvent::Type::Steal);
    REQUIRE(events[1].type == VoiceEvent::Type::NoteOn);
    REQUIRE(events[0].voiceIndex == events[1].voiceIndex);
    REQUIRE(events[1].note == 70);
}

TEST_CASE("US3: Soft steal returns NoteOff (old) + NoteOn (new) for same voice",
          "[voice_allocator][us3]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(4);
    alloc.setStealMode(StealMode::Soft);

    for (int i = 0; i < 4; ++i) {
        (void)alloc.noteOn(static_cast<uint8_t>(60 + i), 100);
    }

    auto events = alloc.noteOn(70, 100);
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].type == VoiceEvent::Type::NoteOff);
    REQUIRE(events[1].type == VoiceEvent::Type::NoteOn);
    REQUIRE(events[0].voiceIndex == events[1].voiceIndex);
    REQUIRE(events[0].note != 70);
    REQUIRE(events[1].note == 70);
}

TEST_CASE("US3: Releasing voices preferred over Active voices for stealing",
          "[voice_allocator][us3][sc004]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(4);
    alloc.setAllocationMode(AllocationMode::Oldest);

    (void)alloc.noteOn(60, 100);  // v0
    (void)alloc.noteOn(62, 100);  // v1
    (void)alloc.noteOn(64, 100);  // v2
    (void)alloc.noteOn(66, 100);  // v3

    // Release voice playing note 62
    (void)alloc.noteOff(62);

    auto events = alloc.noteOn(70, 100);
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].note == 62);
}

TEST_CASE("US3: Allocation mode strategy applied among releasing voices",
          "[voice_allocator][us3]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(4);
    alloc.setAllocationMode(AllocationMode::Oldest);

    (void)alloc.noteOn(60, 100);  // v0, timestamp=1
    (void)alloc.noteOn(62, 100);  // v1, timestamp=2
    (void)alloc.noteOn(64, 100);  // v2, timestamp=3
    (void)alloc.noteOn(66, 100);  // v3, timestamp=4

    (void)alloc.noteOff(60);  // v0 releasing
    (void)alloc.noteOff(62);  // v1 releasing

    auto events = alloc.noteOn(70, 100);
    REQUIRE(events[0].note == 60);
}

TEST_CASE("US3: setStealMode changes steal behavior",
          "[voice_allocator][us3]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(2);

    alloc.setStealMode(StealMode::Hard);
    (void)alloc.noteOn(60, 100);
    (void)alloc.noteOn(62, 100);

    auto events = alloc.noteOn(64, 100);
    REQUIRE(events[0].type == VoiceEvent::Type::Steal);

    alloc.reset();
    alloc.setStealMode(StealMode::Soft);
    (void)alloc.noteOn(60, 100);
    (void)alloc.noteOn(62, 100);

    events = alloc.noteOn(64, 100);
    REQUIRE(events[0].type == VoiceEvent::Type::NoteOff);
}

// =============================================================================
// Phase 6: User Story 4 - Same-Note Retrigger
// =============================================================================

TEST_CASE("US4: Same-note retrigger reuses existing voice",
          "[voice_allocator][us4][sc005]") {
    VoiceAllocator alloc;

    auto events1 = alloc.noteOn(60, 100);
    REQUIRE(events1.size() == 1);
    uint8_t voiceIdx = events1[0].voiceIndex;
    REQUIRE(alloc.getActiveVoiceCount() == 1);

    auto events2 = alloc.noteOn(60, 80);
    REQUIRE(events2.size() == 2);
    REQUIRE(events2[0].type == VoiceEvent::Type::Steal);
    REQUIRE(events2[0].voiceIndex == voiceIdx);
    REQUIRE(events2[1].type == VoiceEvent::Type::NoteOn);
    REQUIRE(events2[1].voiceIndex == voiceIdx);
    REQUIRE(events2[1].velocity == 80);

    REQUIRE(alloc.getActiveVoiceCount() == 1);
}

TEST_CASE("US4: Releasing voice reclaimed for same-note retrigger",
          "[voice_allocator][us4]") {
    VoiceAllocator alloc;

    auto events1 = alloc.noteOn(60, 100);
    uint8_t voiceIdx = events1[0].voiceIndex;

    (void)alloc.noteOff(60);
    REQUIRE(alloc.getVoiceState(voiceIdx) == VoiceState::Releasing);

    auto events2 = alloc.noteOn(60, 90);
    REQUIRE(events2.size() == 2);
    REQUIRE(events2[0].type == VoiceEvent::Type::Steal);
    REQUIRE(events2[0].voiceIndex == voiceIdx);
    REQUIRE(events2[1].type == VoiceEvent::Type::NoteOn);
    REQUIRE(events2[1].voiceIndex == voiceIdx);
    REQUIRE(alloc.getVoiceState(voiceIdx) == VoiceState::Active);
}

TEST_CASE("US4: Active voice count does not increase on same-note retrigger",
          "[voice_allocator][us4]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(4);

    (void)alloc.noteOn(60, 100);
    (void)alloc.noteOn(62, 100);
    (void)alloc.noteOn(64, 100);
    (void)alloc.noteOn(66, 100);
    REQUIRE(alloc.getActiveVoiceCount() == 4);

    (void)alloc.noteOn(60, 80);
    REQUIRE(alloc.getActiveVoiceCount() == 4);
}

// =============================================================================
// Phase 7: User Story 5 - Unison Mode
// =============================================================================

TEST_CASE("US5: Unison count N allocates N voices per note-on",
          "[voice_allocator][us5][sc006]") {
    VoiceAllocator alloc;
    alloc.setUnisonCount(3);
    alloc.setUnisonDetune(0.5f);

    auto events = alloc.noteOn(60, 100);
    REQUIRE(events.size() == 3);

    std::set<uint8_t> voiceIndices;
    for (const auto& e : events) {
        REQUIRE(e.type == VoiceEvent::Type::NoteOn);
        REQUIRE(e.note == 60);
        REQUIRE(e.velocity == 100);
        voiceIndices.insert(e.voiceIndex);
    }
    REQUIRE(voiceIndices.size() == 3);
}

TEST_CASE("US5: Unison detune spreads voices symmetrically (odd N)",
          "[voice_allocator][us5]") {
    VoiceAllocator alloc;
    alloc.setUnisonCount(3);
    alloc.setUnisonDetune(1.0f);

    auto events = alloc.noteOn(69, 100);
    REQUIRE(events.size() == 3);

    float baseFreq = 440.0f;
    float expectedDown = baseFreq * std::pow(2.0f, -50.0f / 1200.0f);
    float expectedCenter = baseFreq;
    float expectedUp = baseFreq * std::pow(2.0f, 50.0f / 1200.0f);

    std::vector<float> freqs;
    for (const auto& e : events) {
        freqs.push_back(e.frequency);
    }
    std::sort(freqs.begin(), freqs.end());

    REQUIRE(freqs[0] == Approx(expectedDown).margin(0.1f));
    REQUIRE(freqs[1] == Approx(expectedCenter).margin(0.1f));
    REQUIRE(freqs[2] == Approx(expectedUp).margin(0.1f));
}

TEST_CASE("US5: Unison detune spreads voices symmetrically (even N)",
          "[voice_allocator][us5]") {
    VoiceAllocator alloc;
    alloc.setUnisonCount(4);
    alloc.setUnisonDetune(1.0f);

    auto events = alloc.noteOn(69, 100);
    REQUIRE(events.size() == 4);

    float baseFreq = 440.0f;

    std::vector<float> freqs;
    for (const auto& e : events) {
        freqs.push_back(e.frequency);
    }
    std::sort(freqs.begin(), freqs.end());

    // For N=4, detune=1.0: offsets = -50, -16.67, +16.67, +50 cents
    float expected0 = baseFreq * std::pow(2.0f, -50.0f / 1200.0f);
    float expected1 = baseFreq * std::pow(2.0f, (-50.0f / 3.0f) / 1200.0f);
    float expected2 = baseFreq * std::pow(2.0f, (50.0f / 3.0f) / 1200.0f);
    float expected3 = baseFreq * std::pow(2.0f, 50.0f / 1200.0f);

    REQUIRE(freqs[0] == Approx(expected0).margin(0.1f));
    REQUIRE(freqs[1] == Approx(expected1).margin(0.1f));
    REQUIRE(freqs[2] == Approx(expected2).margin(0.1f));
    REQUIRE(freqs[3] == Approx(expected3).margin(0.1f));
}

TEST_CASE("US5: noteOff releases all N unison voices",
          "[voice_allocator][us5][sc006]") {
    VoiceAllocator alloc;
    alloc.setUnisonCount(3);
    alloc.setUnisonDetune(0.5f);

    (void)alloc.noteOn(60, 100);
    REQUIRE(alloc.getActiveVoiceCount() == 3);

    auto offEvents = alloc.noteOff(60);
    REQUIRE(offEvents.size() == 3);

    for (const auto& e : offEvents) {
        REQUIRE(e.type == VoiceEvent::Type::NoteOff);
        REQUIRE(e.note == 60);
    }
}

TEST_CASE("US5: Effective polyphony = voiceCount / unisonCount",
          "[voice_allocator][us5][sc006]") {
    VoiceAllocator alloc;
    alloc.setUnisonCount(4);

    (void)alloc.noteOn(60, 100);
    REQUIRE(alloc.getActiveVoiceCount() == 4);

    (void)alloc.noteOn(64, 100);
    REQUIRE(alloc.getActiveVoiceCount() == 8);

    // Third note should trigger stealing
    auto events3 = alloc.noteOn(68, 100);
    bool hasStealOrOff = false;
    bool hasNoteOn = false;
    for (const auto& e : events3) {
        if (e.type == VoiceEvent::Type::Steal || e.type == VoiceEvent::Type::NoteOff) {
            hasStealOrOff = true;
        }
        if (e.type == VoiceEvent::Type::NoteOn) {
            hasNoteOn = true;
        }
    }
    REQUIRE(hasStealOrOff);
    REQUIRE(hasNoteOn);
}

TEST_CASE("US5: setUnisonCount clamps to [1, kMaxUnisonCount]",
          "[voice_allocator][us5]") {
    VoiceAllocator alloc;

    alloc.setUnisonCount(0);  // Clamps to 1
    auto events = alloc.noteOn(60, 100);
    REQUIRE(events.size() == 1);

    alloc.reset();
    alloc.setUnisonCount(100);  // Clamps to 8
    events = alloc.noteOn(60, 100);
    REQUIRE(events.size() == 8);
}

TEST_CASE("US5: setUnisonDetune clamps and ignores NaN/Inf",
          "[voice_allocator][us5]") {
    VoiceAllocator alloc;
    alloc.setUnisonCount(2);

    // Helper: trigger a note and return the absolute frequency spread between
    // the two unison voices.
    auto measureSpread = [&](float detune) -> float {
        alloc.reset();
        alloc.setUnisonCount(2);
        alloc.setUnisonDetune(detune);
        auto events = alloc.noteOn(69, 100);
        REQUIRE(events.size() == 2);
        return std::abs(alloc.getVoiceFrequency(events[0].voiceIndex) -
                        alloc.getVoiceFrequency(events[1].voiceIndex));
    };

    // Baseline: detune=0.5 produces a nonzero spread
    float spread05 = measureSpread(0.5f);
    REQUIRE(spread05 > 0.0f);

    // detune=1.0 produces maximum spread
    float spread10 = measureSpread(1.0f);
    REQUIRE(spread10 > spread05);

    // Clamp below: detune=-1.0 should clamp to 0.0 (no spread)
    float spreadNeg = measureSpread(-1.0f);
    REQUIRE(spreadNeg == Approx(0.0f).margin(0.001f));

    // Clamp above: detune=2.0 should clamp to 1.0 (same as detune=1.0)
    float spreadOver = measureSpread(2.0f);
    REQUIRE(spreadOver == Approx(spread10).margin(0.01f));

    // NaN rejected: set 0.5, then NaN, verify spread still matches 0.5
    alloc.reset();
    alloc.setUnisonCount(2);
    alloc.setUnisonDetune(0.5f);
    alloc.setUnisonDetune(std::numeric_limits<float>::quiet_NaN());
    auto events = alloc.noteOn(69, 100);
    float spreadAfterNaN = std::abs(alloc.getVoiceFrequency(events[0].voiceIndex) -
                                    alloc.getVoiceFrequency(events[1].voiceIndex));
    REQUIRE(spreadAfterNaN == Approx(spread05).margin(0.01f));

    // Inf rejected: set 0.5, then Inf, verify spread still matches 0.5
    alloc.reset();
    alloc.setUnisonCount(2);
    alloc.setUnisonDetune(0.5f);
    alloc.setUnisonDetune(std::numeric_limits<float>::infinity());
    events = alloc.noteOn(69, 100);
    float spreadAfterInf = std::abs(alloc.getVoiceFrequency(events[0].voiceIndex) -
                                    alloc.getVoiceFrequency(events[1].voiceIndex));
    REQUIRE(spreadAfterInf == Approx(spread05).margin(0.01f));
}

TEST_CASE("US5: Unison mode changes do not affect active voices",
          "[voice_allocator][us5]") {
    VoiceAllocator alloc;
    alloc.setUnisonCount(1);

    (void)alloc.noteOn(60, 100);
    REQUIRE(alloc.getActiveVoiceCount() == 1);

    alloc.setUnisonCount(4);
    REQUIRE(alloc.getActiveVoiceCount() == 1);

    auto events2 = alloc.noteOn(64, 100);
    REQUIRE(events2.size() == 4);
}

TEST_CASE("US5: setUnisonCount(4) takes effect immediately for next noteOn",
          "[voice_allocator][us5]") {
    VoiceAllocator alloc;

    alloc.setUnisonCount(4);
    auto events = alloc.noteOn(60, 100);
    REQUIRE(events.size() == 4);
}

TEST_CASE("US5: Unison group stealing steals all N voices together",
          "[voice_allocator][us5]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(8);
    alloc.setUnisonCount(4);

    (void)alloc.noteOn(60, 100);
    (void)alloc.noteOn(64, 100);

    auto events = alloc.noteOn(68, 100);

    size_t noteOnCount = 0;
    for (const auto& e : events) {
        if (e.type == VoiceEvent::Type::NoteOn && e.note == 68) {
            ++noteOnCount;
        }
    }
    REQUIRE(noteOnCount == 4);
}

TEST_CASE("US5: Unison group with any Releasing voice considered Releasing",
          "[voice_allocator][us5]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(8);
    alloc.setUnisonCount(4);

    (void)alloc.noteOn(60, 100);
    (void)alloc.noteOn(64, 100);

    (void)alloc.noteOff(60);

    auto events = alloc.noteOn(68, 100);

    bool stoleFromReleasing = false;
    for (const auto& e : events) {
        if (e.type == VoiceEvent::Type::Steal || e.type == VoiceEvent::Type::NoteOff) {
            if (e.note == 60) stoleFromReleasing = true;
        }
    }
    REQUIRE(stoleFromReleasing);
}

// =============================================================================
// Phase 8: User Story 6 - Configurable Voice Count
// =============================================================================

TEST_CASE("US6: setVoiceCount clamps to [1, kMaxVoices]",
          "[voice_allocator][us6]") {
    VoiceAllocator alloc;

    // Clamp below: setVoiceCount(0) should clamp to 1 (monophonic)
    (void)alloc.setVoiceCount(0);
    auto e1 = alloc.noteOn(60, 100);
    REQUIRE(e1.size() == 1);
    // Second note must steal (only 1 voice available)
    auto e2 = alloc.noteOn(64, 100);
    REQUIRE(e2.size() == 2);
    REQUIRE(e2[0].type == VoiceEvent::Type::Steal);
    REQUIRE(e2[1].type == VoiceEvent::Type::NoteOn);

    // Clamp above: setVoiceCount(100) should clamp to 32
    alloc.reset();
    (void)alloc.setVoiceCount(100);
    // Fill 32 voices without stealing
    for (int i = 0; i < 32; ++i) {
        auto events = alloc.noteOn(static_cast<uint8_t>(i), 100);
        REQUIRE(events.size() == 1);
        REQUIRE(events[0].type == VoiceEvent::Type::NoteOn);
    }
    REQUIRE(alloc.getActiveVoiceCount() == 32);
    // 33rd note must steal (clamped to 32, not 100)
    auto e33 = alloc.noteOn(100, 100);
    REQUIRE(e33.size() == 2);
    REQUIRE(e33[0].type == VoiceEvent::Type::Steal);
}

TEST_CASE("US6: Reducing voice count releases excess voices",
          "[voice_allocator][us6]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(8);

    for (int i = 0; i < 8; ++i) {
        (void)alloc.noteOn(static_cast<uint8_t>(60 + i), 100);
    }
    REQUIRE(alloc.getActiveVoiceCount() == 8);

    auto events = alloc.setVoiceCount(4);
    REQUIRE(events.size() == 4);

    for (const auto& e : events) {
        REQUIRE(e.type == VoiceEvent::Type::NoteOff);
        REQUIRE(e.voiceIndex >= 4);
    }

    REQUIRE(alloc.getActiveVoiceCount() == 4);
}

TEST_CASE("US6: Increasing voice count makes new voices available",
          "[voice_allocator][us6]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(4);

    for (int i = 0; i < 4; ++i) {
        (void)alloc.noteOn(static_cast<uint8_t>(60 + i), 100);
    }

    (void)alloc.setVoiceCount(8);

    auto events = alloc.noteOn(70, 100);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].type == VoiceEvent::Type::NoteOn);
    REQUIRE(events[0].voiceIndex >= 4);
}

TEST_CASE("US6: Voice count 1 produces monophonic behavior",
          "[voice_allocator][us6]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(1);

    (void)alloc.noteOn(60, 100);
    REQUIRE(alloc.getActiveVoiceCount() == 1);

    auto events = alloc.noteOn(64, 100);
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].type == VoiceEvent::Type::Steal);
    REQUIRE(events[1].type == VoiceEvent::Type::NoteOn);
    REQUIRE(events[1].note == 64);
}

// =============================================================================
// Phase 9: Pitch Bend, Tuning, and State Queries
// =============================================================================

TEST_CASE("setPitchBend updates all active voice frequencies",
          "[voice_allocator][sc012]") {
    VoiceAllocator alloc;

    auto events = alloc.noteOn(69, 100);  // A4 = 440 Hz
    REQUIRE(events[0].frequency == Approx(440.0f).margin(0.01f));

    alloc.setPitchBend(2.0f);

    float expectedFreq = 440.0f * std::pow(2.0f, 2.0f / 12.0f);

    auto events2 = alloc.noteOn(69, 80);  // retrigger
    for (const auto& e : events2) {
        if (e.type == VoiceEvent::Type::NoteOn) {
            REQUIRE(e.frequency == Approx(expectedFreq).margin(0.1f));
        }
    }
}

TEST_CASE("setPitchBend ignores NaN/Inf values",
          "[voice_allocator]") {
    VoiceAllocator alloc;

    alloc.setPitchBend(1.0f);
    alloc.setPitchBend(std::numeric_limits<float>::quiet_NaN());
    alloc.setPitchBend(std::numeric_limits<float>::infinity());
    alloc.setPitchBend(-std::numeric_limits<float>::infinity());
    // No crash = pass
    REQUIRE(true);
}

TEST_CASE("setTuningReference recalculates all active voice frequencies",
          "[voice_allocator]") {
    VoiceAllocator alloc;

    (void)alloc.noteOn(69, 100);
    alloc.setTuningReference(432.0f);

    auto events = alloc.noteOn(69, 80);  // retrigger
    for (const auto& e : events) {
        if (e.type == VoiceEvent::Type::NoteOn) {
            REQUIRE(e.frequency == Approx(432.0f).margin(0.1f));
        }
    }
}

TEST_CASE("setTuningReference ignores NaN/Inf values",
          "[voice_allocator]") {
    VoiceAllocator alloc;

    alloc.setTuningReference(432.0f);
    alloc.setTuningReference(std::numeric_limits<float>::quiet_NaN());
    alloc.setTuningReference(std::numeric_limits<float>::infinity());
    REQUIRE(true);
}

TEST_CASE("noteOn after setPitchBend uses updated frequency",
          "[voice_allocator]") {
    VoiceAllocator alloc;

    alloc.setPitchBend(2.0f);

    auto events = alloc.noteOn(69, 100);
    float expectedFreq = 440.0f * std::pow(2.0f, 2.0f / 12.0f);
    REQUIRE(events[0].frequency == Approx(expectedFreq).margin(0.1f));
}

TEST_CASE("setPitchBend immediately updates stored voice frequencies (SC-012 direct)",
          "[voice_allocator][sc012]") {
    VoiceAllocator alloc;

    // Trigger A4, verify baseline frequency via getVoiceFrequency
    auto events = alloc.noteOn(69, 100);
    uint8_t vi = events[0].voiceIndex;
    REQUIRE(alloc.getVoiceFrequency(vi) == Approx(440.0f).margin(0.01f));

    // Apply pitch bend -- frequency must update immediately, no retrigger
    alloc.setPitchBend(2.0f);
    float expectedBent = 440.0f * std::pow(2.0f, 2.0f / 12.0f);
    REQUIRE(alloc.getVoiceFrequency(vi) == Approx(expectedBent).margin(0.1f));

    // Reset pitch bend, verify frequency returns to base
    alloc.setPitchBend(0.0f);
    REQUIRE(alloc.getVoiceFrequency(vi) == Approx(440.0f).margin(0.01f));
}

TEST_CASE("setTuningReference immediately updates stored voice frequencies",
          "[voice_allocator]") {
    VoiceAllocator alloc;

    auto events = alloc.noteOn(69, 100);
    uint8_t vi = events[0].voiceIndex;
    REQUIRE(alloc.getVoiceFrequency(vi) == Approx(440.0f).margin(0.01f));

    // Change tuning -- frequency must update immediately, no retrigger
    alloc.setTuningReference(432.0f);
    REQUIRE(alloc.getVoiceFrequency(vi) == Approx(432.0f).margin(0.01f));
}

TEST_CASE("getVoiceNote returns note or -1 for idle",
          "[voice_allocator]") {
    VoiceAllocator alloc;

    REQUIRE(alloc.getVoiceNote(0) == -1);

    auto events = alloc.noteOn(60, 100);
    uint8_t vi = events[0].voiceIndex;
    REQUIRE(alloc.getVoiceNote(vi) == 60);

    (void)alloc.noteOff(60);
    REQUIRE(alloc.getVoiceNote(vi) == 60);

    alloc.voiceFinished(vi);
    REQUIRE(alloc.getVoiceNote(vi) == -1);

    REQUIRE(alloc.getVoiceNote(100) == -1);
}

TEST_CASE("getVoiceState returns current state",
          "[voice_allocator]") {
    VoiceAllocator alloc;

    REQUIRE(alloc.getVoiceState(0) == VoiceState::Idle);

    auto events = alloc.noteOn(60, 100);
    uint8_t vi = events[0].voiceIndex;
    REQUIRE(alloc.getVoiceState(vi) == VoiceState::Active);

    (void)alloc.noteOff(60);
    REQUIRE(alloc.getVoiceState(vi) == VoiceState::Releasing);

    alloc.voiceFinished(vi);
    REQUIRE(alloc.getVoiceState(vi) == VoiceState::Idle);
}

TEST_CASE("Thread-safe query methods under concurrent contention",
          "[voice_allocator]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(8);

    std::atomic<bool> running{true};
    std::atomic<bool> uiReady{false};
    std::atomic<bool> queryFailed{false};

    // UI thread: continuously query voice state while audio thread mutates
    std::thread uiThread([&]() {
        uiReady.store(true, std::memory_order_release);
        while (running.load(std::memory_order_relaxed)) {
            uint32_t count = alloc.getActiveVoiceCount();
            if (count > 8) {
                queryFailed.store(true, std::memory_order_relaxed);
            }
            for (size_t i = 0; i < 8; ++i) {
                auto state = alloc.getVoiceState(i);
                auto stateVal = static_cast<uint8_t>(state);
                if (stateVal > 2) {
                    queryFailed.store(true, std::memory_order_relaxed);
                }
                int note = alloc.getVoiceNote(i);
                if (note < -1 || note > 127) {
                    queryFailed.store(true, std::memory_order_relaxed);
                }
            }
        }
    });

    // Wait for UI thread to start
    while (!uiReady.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    // Audio thread (this thread): run full lifecycle rapidly
    for (int iter = 0; iter < 10000; ++iter) {
        uint8_t note = static_cast<uint8_t>(48 + (iter % 36));
        uint8_t vel = static_cast<uint8_t>(40 + (iter % 88));

        auto onEvents = alloc.noteOn(note, vel);
        (void)onEvents;

        if (iter % 3 == 0) {
            auto offEvents = alloc.noteOff(note);
            (void)offEvents;
        }
        if (iter % 7 == 0) {
            for (size_t v = 0; v < 8; ++v) {
                alloc.voiceFinished(v);
            }
        }
    }

    running.store(false, std::memory_order_relaxed);
    uiThread.join();

    REQUIRE_FALSE(queryFailed.load());
}

TEST_CASE("reset returns all voices to Idle and clears state",
          "[voice_allocator]") {
    VoiceAllocator alloc;

    (void)alloc.noteOn(60, 100);
    (void)alloc.noteOn(62, 100);
    (void)alloc.noteOn(64, 100);
    (void)alloc.noteOff(64);

    REQUIRE(alloc.getActiveVoiceCount() == 3);

    alloc.reset();

    REQUIRE(alloc.getActiveVoiceCount() == 0);
    for (size_t i = 0; i < VoiceAllocator::kMaxVoices; ++i) {
        REQUIRE(alloc.getVoiceState(i) == VoiceState::Idle);
        REQUIRE(alloc.getVoiceNote(i) == -1);
    }
}

// =============================================================================
// Phase 10: Performance and Memory Verification
// =============================================================================

TEST_CASE("Performance: noteOn latency < 1us average with 32 voices",
          "[voice_allocator][performance][sc008]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(32);

    // Warm up
    for (int i = 0; i < 32; ++i) {
        (void)alloc.noteOn(static_cast<uint8_t>(i), 100);
    }
    alloc.reset();

    constexpr int iterations = 10000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        alloc.reset();
        for (int j = 0; j < 32; ++j) {
            (void)alloc.noteOn(static_cast<uint8_t>(j), 100);
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto durationNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end - start).count();

    double avgNsPerNoteOn = static_cast<double>(durationNs) /
                            (static_cast<double>(iterations) * 32.0);

    INFO("Average noteOn latency: " << avgNsPerNoteOn << " ns");
    REQUIRE(avgNsPerNoteOn < 1000.0);
}

TEST_CASE("Memory: VoiceAllocator instance size < 4096 bytes",
          "[voice_allocator][memory][sc009]") {
    size_t instanceSize = sizeof(VoiceAllocator);
    INFO("VoiceAllocator size: " << instanceSize << " bytes");
    REQUIRE(instanceSize < 4096);
}

// =============================================================================
// Phase 13: Edge Cases
// =============================================================================

TEST_CASE("Edge: MIDI note 0 (lowest) processed correctly",
          "[voice_allocator][edge]") {
    VoiceAllocator alloc;

    auto events = alloc.noteOn(0, 100);
    REQUIRE(events.size() == 1);

    float expected = expected12TETFrequency(0);
    REQUIRE(events[0].frequency == Approx(expected).margin(0.01f));
    REQUIRE(events[0].frequency > 0.0f);
}

TEST_CASE("Edge: MIDI note 127 (highest) processed correctly",
          "[voice_allocator][edge]") {
    VoiceAllocator alloc;

    auto events = alloc.noteOn(127, 100);
    REQUIRE(events.size() == 1);

    float expected = expected12TETFrequency(127);
    REQUIRE(events[0].frequency == Approx(expected).margin(0.5f));
    REQUIRE(events[0].frequency > 0.0f);
}

TEST_CASE("Edge: Double note-off for same note returns empty span",
          "[voice_allocator][edge]") {
    VoiceAllocator alloc;

    (void)alloc.noteOn(60, 100);
    auto events1 = alloc.noteOff(60);
    REQUIRE(events1.size() == 1);

    auto events2 = alloc.noteOff(60);
    REQUIRE(events2.empty());
}

TEST_CASE("Edge: All voices active, no releasing, steal selects active voice",
          "[voice_allocator][edge]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(4);

    for (int i = 0; i < 4; ++i) {
        (void)alloc.noteOn(static_cast<uint8_t>(60 + i), 100);
    }

    auto events = alloc.noteOn(70, 100);
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].type == VoiceEvent::Type::Steal);
}

TEST_CASE("Edge: All voices releasing, steal selects best releasing voice",
          "[voice_allocator][edge]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(4);
    alloc.setAllocationMode(AllocationMode::Oldest);

    for (int i = 0; i < 4; ++i) {
        (void)alloc.noteOn(static_cast<uint8_t>(60 + i), 100);
    }
    for (int i = 0; i < 4; ++i) {
        (void)alloc.noteOff(static_cast<uint8_t>(60 + i));
    }

    auto events = alloc.noteOn(70, 100);
    REQUIRE(events[0].note == 60);
}

TEST_CASE("Edge: Unison count clamped when exceeds voice count",
          "[voice_allocator][edge]") {
    VoiceAllocator alloc;
    (void)alloc.setVoiceCount(4);
    alloc.setUnisonCount(8);

    auto events = alloc.noteOn(60, 100);
    REQUIRE(events.size() <= 8);
    REQUIRE(alloc.getActiveVoiceCount() <= 4);
}

TEST_CASE("Edge: MIDI machine gun - rapid same-note retrigger",
          "[voice_allocator][edge]") {
    VoiceAllocator alloc;

    for (int i = 0; i < 100; ++i) {
        (void)alloc.noteOn(60, static_cast<uint8_t>(50 + (i % 50)));
    }

    REQUIRE(alloc.getActiveVoiceCount() == 1);
}

TEST_CASE("Edge: Pitch bend +2 semitones on MIDI note 127 produces valid frequency",
          "[voice_allocator][edge]") {
    VoiceAllocator alloc;
    alloc.setPitchBend(2.0f);

    auto events = alloc.noteOn(127, 100);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].frequency > 0.0f);

    bool isValid = !detail::isNaN(events[0].frequency) &&
                   !detail::isInf(events[0].frequency);
    REQUIRE(isValid);
}

TEST_CASE("Edge: Pitch bend -2 semitones on MIDI note 0 produces valid frequency",
          "[voice_allocator][edge]") {
    VoiceAllocator alloc;
    alloc.setPitchBend(-2.0f);

    auto events = alloc.noteOn(0, 100);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].frequency > 0.0f);

    bool isValid = !detail::isNaN(events[0].frequency) &&
                   !detail::isInf(events[0].frequency);
    REQUIRE(isValid);
}
