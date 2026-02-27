// ==============================================================================
// End-to-End Arp Preset Playback Test (082-presets-polish)
// ==============================================================================
// Verifies SC-011: load deterministic "Basic Up 1/16" preset state, feed MIDI
// note-on events for C-E-G chord, run processBlock for 2+ arp cycles, and
// assert the emitted note event sequence matches the expected ascending pattern.
//
// Uses ArpeggiatorCore directly (not full Processor) for clean event capture.
// The preset parameters are applied via the core's setter API, matching the
// values that the preset generator writes for "Basic Up 1/16".
//
// Phase 9 (US7): T093
//
// Reference: specs/082-presets-polish/spec.md SC-011
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/arpeggiator_core.h>
#include <krate/dsp/core/note_value.h>

#include <algorithm>
#include <array>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Helper: Collect NoteOn events from ArpeggiatorCore over multiple blocks
// =============================================================================

namespace {

struct CollectedNote {
    uint8_t pitch;
    uint8_t velocity;
    int32_t sampleOffset;
};

/// Configure ArpeggiatorCore with "Basic Up 1/16" preset parameters.
///
/// Matches the factory preset definition:
///   mode=Up, tempoSync=1, noteValue=1/16 (index 7), gateLength=80%,
///   octaveRange=1, velocity lane = uniform 0.8, modifier lane = kStepActive,
///   8-step patterns for all lanes
void configureBasicUp116(ArpeggiatorCore& arp) {
    arp.prepare(44100.0, 512);

    // Base parameters
    arp.setMode(ArpMode::Up);
    arp.setOctaveRange(1);
    arp.setOctaveMode(OctaveMode::Sequential);
    arp.setTempoSync(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setFreeRate(4.0f);       // default (not used in tempo sync)
    arp.setGateLength(80.0f);    // 80%
    arp.setSwing(0.0f);
    arp.setLatchMode(LatchMode::Off);
    arp.setRetrigger(ArpRetriggerMode::Off);

    // Velocity lane: 8 steps, all 0.8
    arp.velocityLane().setLength(8);
    for (size_t i = 0; i < 8; ++i) {
        arp.velocityLane().setStep(i, 0.8f);
    }

    // Gate lane: 8 steps, all 1.0 (default, gateLength controls actual %)
    arp.gateLane().setLength(8);
    for (size_t i = 0; i < 8; ++i) {
        arp.gateLane().setStep(i, 1.0f);
    }

    // Pitch lane: 8 steps, all 0
    arp.pitchLane().setLength(8);
    for (size_t i = 0; i < 8; ++i) {
        arp.pitchLane().setStep(i, 0);
    }

    // Modifier lane: 8 steps, all kStepActive
    arp.modifierLane().setLength(8);
    for (size_t i = 0; i < 8; ++i) {
        arp.modifierLane().setStep(i, static_cast<uint8_t>(kStepActive));
    }

    // Ratchet lane: 8 steps, all 1 (no ratchet)
    arp.ratchetLane().setLength(8);
    for (size_t i = 0; i < 8; ++i) {
        arp.ratchetLane().setStep(i, static_cast<uint8_t>(1));
    }

    // Condition lane: 8 steps, all Always
    arp.conditionLane().setLength(8);
    for (size_t i = 0; i < 8; ++i) {
        arp.conditionLane().setStep(i, static_cast<uint8_t>(TrigCondition::Always));
    }

    // No Euclidean, no spice, no humanize
    arp.setEuclideanEnabled(false);
    arp.setSpice(0.0f);
    arp.setHumanize(0.0f);
    arp.setRatchetSwing(50.0f);  // neutral (no swing)

    // Accent/slide defaults
    arp.setAccentVelocity(30);   // default accent boost
    arp.setSlideTime(50.0f);     // default slide time

    // Enable arp LAST (mirrors processor's applyParamsToEngine order)
    arp.setEnabled(true);
}

/// Process multiple blocks and collect all NoteOn events.
std::vector<CollectedNote> processAndCollectNotes(
    ArpeggiatorCore& arp,
    int numBlocks,
    double tempoBPM = 120.0,
    size_t blockSize = 512)
{
    BlockContext ctx{
        .sampleRate = 44100.0,
        .blockSize = blockSize,
        .tempoBPM = tempoBPM,
        .isPlaying = true
    };

    std::array<ArpEvent, 256> events{};
    std::vector<CollectedNote> collected;

    for (int block = 0; block < numBlocks; ++block) {
        size_t numEvents = arp.processBlock(ctx, events);
        for (size_t i = 0; i < numEvents; ++i) {
            if (events[i].type == ArpEvent::Type::NoteOn) {
                collected.push_back({
                    events[i].note,
                    events[i].velocity,
                    events[i].sampleOffset
                });
            }
        }
    }

    return collected;
}

}  // namespace

// =============================================================================
// E2E Test: Load Basic Up 1/16 preset, play C-E-G, verify ascending sequence
// =============================================================================

TEST_CASE("E2E: Load Basic Up 1/16 state, feed C-E-G chord, verify ascending note sequence",
          "[arp][e2e]") {
    // -------------------------------------------------------------------------
    // (a) Set up ArpeggiatorCore with "Basic Up 1/16" preset parameters
    // -------------------------------------------------------------------------
    ArpeggiatorCore arp;
    configureBasicUp116(arp);

    // -------------------------------------------------------------------------
    // (c) Feed MIDI note-on for C4 (60), E4 (64), G4 (67)
    // -------------------------------------------------------------------------
    // Velocity 127 (max MIDI velocity, representing 1.0 in VST3 float scale)
    arp.noteOn(60, 127);  // C4
    arp.noteOn(64, 127);  // E4
    arp.noteOn(67, 127);  // G4

    // -------------------------------------------------------------------------
    // (d) Run process() for blocks covering 2+ full arp cycles
    // -------------------------------------------------------------------------
    // At 120 BPM, 1/16 note = 0.25 beats = 5512.5 samples.
    // With 8-step lane length, 1 cycle = 8 steps.
    // Note: The arp fires on every step (8 steps per cycle), but the held notes
    // cycle independently (3 notes: C4, E4, G4 repeat every 3 steps).
    // 2 cycles = 16 steps = 16 * 5512.5 = 88200 samples.
    // With 512-sample blocks: 88200 / 512 = 172.3 blocks.
    // Use 200 blocks to ensure we capture 2+ complete cycles.
    constexpr int kNumBlocks = 200;
    auto notes = processAndCollectNotes(arp, kNumBlocks);

    // -------------------------------------------------------------------------
    // (f) Verify note sequence
    // -------------------------------------------------------------------------
    // NoteSelector::advanceUp() with 3 sorted held notes [60, 64, 67] and
    // octaveRange=1 (Sequential mode) cycles through held notes with period 3:
    //   noteIndex 0 -> 60 (C4)
    //   noteIndex 1 -> 64 (E4)
    //   noteIndex 2 -> 67 (G4)
    //   noteIndex 0 -> 60 (C4)  [wraps, octaveRange=1 so octave stays 0]
    //   ... repeating every 3 steps
    //
    // The note selector is INDEPENDENT of lane length. The 8-step lane length
    // controls velocity/gate/pitch/modifier/ratchet/condition values, but
    // the note selection cycles through the 3 held notes continuously.
    //
    // Full sequence for 16 steps:
    //   Step  0: C4 (60)    Step  8: C4 (60)
    //   Step  1: E4 (64)    Step  9: E4 (64)
    //   Step  2: G4 (67)    Step 10: G4 (67)
    //   Step  3: C4 (60)    Step 11: C4 (60)
    //   Step  4: E4 (64)    Step 12: E4 (64)
    //   Step  5: G4 (67)    Step 13: G4 (67)
    //   Step  6: C4 (60)    Step 14: C4 (60)
    //   Step  7: E4 (64)    Step 15: E4 (64)
    //
    // Confirmed against NoteSelector::advanceUp() implementation in
    // dsp/include/krate/dsp/primitives/held_note_buffer.h lines 328-350.
    const std::array<uint8_t, 3> expectedNotesCycle = {60, 64, 67};

    // We should have at least 16 notes for 2 full 8-step lane cycles
    REQUIRE(notes.size() >= 16);

    INFO("Total NoteOn events collected: " << notes.size());

    // Verify the first 16 notes follow the ascending C-E-G cycle (period 3)
    for (size_t i = 0; i < 16; ++i) {
        size_t noteIdx = i % 3;
        INFO("Note index " << i << " (note cycle position " << noteIdx << ")");
        CHECK(notes[i].pitch == expectedNotesCycle[noteIdx]);
    }

    // -------------------------------------------------------------------------
    // (g) Verify velocities are approximately 0.8 * 127 = 101.6 -> 102
    // -------------------------------------------------------------------------
    // Velocity formula: round(heldVelocity * velScale) = round(127 * 0.8) = 102
    // Humanize is 0, so no random variation.
    for (size_t i = 0; i < 16; ++i) {
        INFO("Velocity at note index " << i);
        CHECK(notes[i].velocity == 102);
    }
}

// =============================================================================
// Additional E2E test: verify timing consistency
// =============================================================================

TEST_CASE("E2E: Basic Up 1/16 timing offsets are consistent with 1/16 note rate",
          "[arp][e2e]") {
    ArpeggiatorCore arp;
    configureBasicUp116(arp);

    arp.noteOn(60, 127);  // C4
    arp.noteOn(64, 127);  // E4
    arp.noteOn(67, 127);  // G4

    // Collect notes across many blocks
    constexpr int kNumBlocks = 200;

    BlockContext ctx{
        .sampleRate = 44100.0,
        .blockSize = 512,
        .tempoBPM = 120.0,
        .isPlaying = true
    };

    // Track absolute sample positions of NoteOn events
    std::array<ArpEvent, 256> events{};
    std::vector<int64_t> noteOnAbsPositions;
    int64_t blockStartSample = 0;

    for (int block = 0; block < kNumBlocks; ++block) {
        size_t numEvents = arp.processBlock(ctx, events);
        for (size_t i = 0; i < numEvents; ++i) {
            if (events[i].type == ArpEvent::Type::NoteOn) {
                noteOnAbsPositions.push_back(blockStartSample + events[i].sampleOffset);
            }
        }
        blockStartSample += static_cast<int64_t>(ctx.blockSize);
    }

    REQUIRE(noteOnAbsPositions.size() >= 8);

    // At 120 BPM, 1/16 note = 0.25 beats = 44100 * 0.25 / (120/60) = 5512.5 samples
    // Allow +/- 2 samples tolerance for integer rounding
    constexpr double kExpectedStepSamples = 44100.0 * 0.25 / (120.0 / 60.0);
    constexpr int64_t kTolerance = 2;

    // Check intervals between consecutive NoteOn events
    for (size_t i = 1; i < std::min(noteOnAbsPositions.size(), size_t{16}); ++i) {
        int64_t interval = noteOnAbsPositions[i] - noteOnAbsPositions[i - 1];
        int64_t expected = static_cast<int64_t>(std::round(kExpectedStepSamples));
        INFO("Interval between note " << (i - 1) << " and " << i
             << ": " << interval << " samples (expected ~" << expected << ")");
        CHECK(std::abs(interval - expected) <= kTolerance);
    }
}
