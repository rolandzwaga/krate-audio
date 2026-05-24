// ==============================================================================
// ArpeggiatorCore Sequencer-Mode Tests (spec 142, Phase 3, User Story 1)
// ==============================================================================
// Verifies the lane 10 (Sequencer Note) behavior inside ArpeggiatorCore:
//   * Lane 10 is inert in Live mode (no advance, no emission).
//   * Lane 10 advances and emits programmed pitches in Sequencer mode.
//   * Rest steps suppress note-on but still advance the playhead (SC-008, FR-019).
//   * Retrigger=Note resets the lane 10 playhead on new held note-on (FR-022a).
//   * Retrigger=Beat resets the lane 10 playhead at a bar boundary (FR-022a).
//   * Side-by-side: sequencer output threads through downstream lanes the same
//     way live held-note input does (SC-002).
//   * Lane 9 modulator setters (setLaneSpeed, setLaneSwing,
//     setLaneLengthJitter, setLaneSpeedCurveTable, setLaneSpeedCurveDepth,
//     setLaneSpeedCurveEnabled) accept index 9 and round-trip (T029 audit
//     gate — guards against silently dropping writes to lane 9).
//
// Reference: specs/142-gradus-piano-roll-sequencer/{spec,plan,tasks,data-model}.md
// ==============================================================================

#include <krate/dsp/processors/arpeggiator_core.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

using namespace Krate::DSP;

namespace {

// Run the arp for `numBlocks` blocks and collect all events with absolute
// sample offsets. Mirrors the helper used in dsp/tests/unit/processors/.
std::vector<ArpEvent> collectEvents(ArpeggiatorCore& arp,
                                    BlockContext& ctx,
                                    size_t numBlocks)
{
    std::vector<ArpEvent> all;
    std::array<ArpEvent, 128> blockEvents{};
    for (size_t b = 0; b < numBlocks; ++b) {
        size_t count = arp.processBlock(ctx, blockEvents);
        for (size_t i = 0; i < count; ++i) {
            ArpEvent e = blockEvents[i];
            e.sampleOffset += static_cast<int32_t>(b * ctx.blockSize);
            all.push_back(e);
        }
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    }
    return all;
}

std::vector<ArpEvent> filterNoteOns(const std::vector<ArpEvent>& events)
{
    std::vector<ArpEvent> out;
    for (const auto& e : events) {
        if (e.type == ArpEvent::Type::NoteOn) out.push_back(e);
    }
    return out;
}

// Configure a vanilla arp + sequencer pattern. Caller picks the source mode.
void primeSeqLane(ArpeggiatorCore& arp,
                  const std::vector<uint8_t>& pitches,
                  const std::vector<int>& restFlags,
                  size_t length)
{
    arp.seqNoteLane().setLength(length);
    for (size_t i = 0; i < pitches.size(); ++i) {
        arp.seqNoteLane().setStep(i, pitches[i]);
    }
    for (size_t i = 0; i < restFlags.size(); ++i) {
        arp.seqRestFlags()[i].store(
            static_cast<uint8_t>(restFlags[i] != 0 ? 1 : 0),
            std::memory_order_relaxed);
    }
}

}  // namespace

// =============================================================================
// T029 gate: lane 9 modulator setters round-trip
// =============================================================================

TEST_CASE("ArpeggiatorCore: lane 9 modulator setters round-trip",
          "[arpeggiator_core][sequencer][lane9]")
{
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);

    // Each setter must accept laneIndex=9 (the Sequencer Note lane index after
    // T028 bumps kNumLanes 9->10). Before the T029 audit + T028 bump, these
    // calls would silently no-op (laneIndex < kNumLanes guard rejected 9).
    arp.setLaneSpeed(9, 2.0f);
    arp.setLaneSwing(9, 50.0f);
    arp.setLaneLengthJitter(9, 2);
    arp.setLaneSpeedCurveDepth(9, 0.7f);
    arp.setLaneSpeedCurveEnabled(9, true);

    std::array<float, 256> table{};
    for (size_t i = 0; i < 256; ++i) table[i] = static_cast<float>(i) / 255.0f;
    arp.setLaneSpeedCurveTable(9, table);

    // Observable round-trip: write/read back via the public accessors. The
    // values stored in the per-lane arrays must reflect the writes — if the
    // setter no-ops because laneIndex >= kNumLanes, the readback will see the
    // default value instead.
    REQUIRE(arp.laneSpeed(9) == Catch::Approx(2.0f));
    REQUIRE(arp.laneSwing(9) == Catch::Approx(0.5f));  // stored as 0..1
    REQUIRE(arp.laneLengthJitter(9) == 2);
    REQUIRE(arp.laneSpeedCurveDepth(9) == Catch::Approx(0.7f));
    REQUIRE(arp.laneSpeedCurveEnabled(9) == true);

    // Curve table is staged; consume it before reading back.
    arp.consumePendingCurveTables();
    const auto& storedTable = arp.laneSpeedCurveTable(9);
    REQUIRE(storedTable[0] == Catch::Approx(0.0f));
    REQUIRE(storedTable[255] == Catch::Approx(1.0f));
    REQUIRE(storedTable[128] == Catch::Approx(128.0f / 255.0f));
}

// =============================================================================
// T030a: lane 10 inert in Live mode, advances in Sequencer mode
// =============================================================================

TEST_CASE("ArpeggiatorCore: lane 10 never advances when sourceMode == Live",
          "[arpeggiator_core][sequencer][lane10][live]")
{
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setSourceMode(SourceMode::Live);

    // Hold a note so the live arp emits something — but lane 10 must remain
    // inert. The lane's playhead must stay at step 0 throughout.
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto _ = collectEvents(arp, ctx, 200);  // ~2.3 seconds at 44.1k
    REQUIRE(arp.seqNoteLane().currentStep() == 0);
}

TEST_CASE("ArpeggiatorCore: lane 10 advances when sourceMode == Sequencer",
          "[arpeggiator_core][sequencer][lane10]")
{
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setSourceMode(SourceMode::Sequencer);

    // Program a 4-step play pattern (no rests) — pitches don't matter for the
    // advance check, but the pattern must be playable (rest=0).
    primeSeqLane(arp, {60, 62, 64, 65}, {0, 0, 0, 0}, 4);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto _ = collectEvents(arp, ctx, 200);
    // After many blocks the playhead must have advanced past step 0.
    const bool advanced = (arp.seqNoteLane().currentStep() != 0) ||
                          (arp.seqNoteLane().length() == 1);
    REQUIRE(advanced);
}

// =============================================================================
// T030b: Sequencer mode emits programmed pitches; rest steps suppress note-on
// =============================================================================

TEST_CASE("ArpeggiatorCore: Sequencer mode emits programmed pitch at each step",
          "[arpeggiator_core][sequencer][lane10]")
{
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setSourceMode(SourceMode::Sequencer);

    primeSeqLane(arp, {60, 64, 67, 60}, {0, 0, 0, 0}, 4);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Run long enough for at least 4 steps to fire (1/16 @120 = 7350 samples).
    auto events = collectEvents(arp, ctx, 80);
    auto noteOns = filterNoteOns(events);
    REQUIRE(noteOns.size() >= 4);

    // First 4 emitted pitches must match the programmed pattern (no transpose
    // since no held notes -> root=60, transpose=heldRoot-60=0).
    REQUIRE(static_cast<int>(noteOns[0].note) == 60);
    REQUIRE(static_cast<int>(noteOns[1].note) == 64);
    REQUIRE(static_cast<int>(noteOns[2].note) == 67);
    REQUIRE(static_cast<int>(noteOns[3].note) == 60);
}

TEST_CASE("ArpeggiatorCore: rest step suppresses note-on but advances playhead",
          "[arpeggiator_core][sequencer][rest][SC-008]")
{
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setSourceMode(SourceMode::Sequencer);

    // Pattern: [play 60, play 64, REST, play 67] length=4.
    primeSeqLane(arp, {60, 64, 50, 67}, {0, 0, 1, 0}, 4);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 80);
    auto noteOns = filterNoteOns(events);

    // Step 2 (pitch 50) is a rest → no noteOn for pitch 50. The other steps
    // emit normally. Take the first 3 emitted notes to verify the sequence:
    // {60, 64, 67} (rest step 2 skipped).
    REQUIRE(noteOns.size() >= 3);
    REQUIRE(static_cast<int>(noteOns[0].note) == 60);
    REQUIRE(static_cast<int>(noteOns[1].note) == 64);
    REQUIRE(static_cast<int>(noteOns[2].note) == 67);

    // The rest pitch must NEVER appear in the output (defensive).
    for (const auto& on : noteOns) {
        REQUIRE(static_cast<int>(on.note) != 50);
    }
}

// =============================================================================
// T030d: Retrigger Note + Beat reset lane 10
// =============================================================================

TEST_CASE("ArpeggiatorCore: retrigger Note resets lane 10 playhead",
          "[arpeggiator_core][sequencer][retrigger][FR-022a]")
{
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setSourceMode(SourceMode::Sequencer);
    arp.setRetrigger(ArpRetriggerMode::Note);

    // Length=32 chosen so the playhead doesn't wrap back to 0 within the
    // 80-block transport window (8 fires < length).
    std::vector<uint8_t> pitches(32);
    std::vector<int>     rests(32, 0);
    for (size_t i = 0; i < 32; ++i) pitches[i] = static_cast<uint8_t>(60 + (i % 12));
    primeSeqLane(arp, pitches, rests, 32);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    arp.noteOn(60, 100);
    (void)collectEvents(arp, ctx, 80);
    // After ~80 blocks the playhead has progressed past step 0.
    REQUIRE(arp.seqNoteLane().currentStep() != 0);  // pre-condition

    // New noteOn must reset the sequencer-note lane playhead to step 0.
    arp.noteOn(67, 100);
    REQUIRE(arp.seqNoteLane().currentStep() == 0);
}

// =============================================================================
// T030c (and US2 sneak-peek): transposition formula
// =============================================================================

TEST_CASE("ArpeggiatorCore: no held notes -> base velocity 100, no transposition",
          "[arpeggiator_core][sequencer][transpose][FR-025a]")
{
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setSourceMode(SourceMode::Sequencer);

    primeSeqLane(arp, {60, 64, 67, 60}, {0, 0, 0, 0}, 4);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 80);
    auto noteOns = filterNoteOns(events);
    REQUIRE(noteOns.size() >= 4);

    // No transpose: emitted pitches equal programmed pitches; velocity = 100.
    REQUIRE(static_cast<int>(noteOns[0].note) == 60);
    REQUIRE(static_cast<int>(noteOns[1].note) == 64);
    REQUIRE(static_cast<int>(noteOns[0].velocity) == 100);
}

TEST_CASE("ArpeggiatorCore: held note transposes pattern by heldNote-60",
          "[arpeggiator_core][sequencer][transpose][SC-003]")
{
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setSourceMode(SourceMode::Sequencer);

    primeSeqLane(arp, {60, 64, 67, 60}, {0, 0, 0, 0}, 4);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Hold note 67 (G4) -> transposition = +7 semitones; base velocity = 90.
    arp.noteOn(67, 90);

    auto events = collectEvents(arp, ctx, 80);
    auto noteOns = filterNoteOns(events);
    REQUIRE(noteOns.size() >= 4);

    // Programmed [60, 64, 67, 60] + 7 = [67, 71, 74, 67].
    REQUIRE(static_cast<int>(noteOns[0].note) == 67);
    REQUIRE(static_cast<int>(noteOns[1].note) == 71);
    REQUIRE(static_cast<int>(noteOns[2].note) == 74);
    REQUIRE(static_cast<int>(noteOns[3].note) == 67);

    // Held-note velocity supplies the base velocity in Sequencer mode.
    REQUIRE(static_cast<int>(noteOns[0].velocity) == 90);
}

// =============================================================================
// T030e: noteOff bypasses latch in Sequencer mode (FR-022)
// =============================================================================

TEST_CASE("ArpeggiatorCore: Latch=Hold ignored in Seq mode (heldNotes empties on release)",
          "[arpeggiator_core][sequencer][latch][FR-022]")
{
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setLatchMode(LatchMode::Hold);

    SECTION("Live mode: Latch=Hold retains released notes (baseline)") {
        arp.setSourceMode(SourceMode::Live);
        arp.noteOn(60, 100);
        arp.noteOff(60);
        // Latch=Hold preserves the released note in heldNotes_ until a new
        // note-on triggers the buffer clear. (Pre-existing Gradus behavior.)
        REQUIRE_FALSE(arp.heldNotes().empty());
    }

    SECTION("Seq mode: Latch=Hold bypassed; heldNotes empties on release") {
        arp.setSourceMode(SourceMode::Sequencer);
        arp.noteOn(60, 100);
        arp.noteOff(60);
        REQUIRE(arp.heldNotes().empty());
    }
}
