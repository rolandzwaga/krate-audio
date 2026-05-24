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

// =============================================================================
// T030d (Beat retrigger): bar-boundary retrigger resets lane 10 playhead
// =============================================================================

TEST_CASE("ArpeggiatorCore: retrigger Beat resets lane 10 playhead on bar boundary",
          "[arpeggiator_core][sequencer][retrigger][FR-022a]")
{
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setSourceMode(SourceMode::Sequencer);
    arp.setRetrigger(ArpRetriggerMode::Beat);

    // Long pattern so the playhead does not naturally wrap during the lead-in.
    std::vector<uint8_t> pitches(32);
    std::vector<int>     rests(32, 0);
    for (size_t i = 0; i < 32; ++i) {
        pitches[i] = static_cast<uint8_t>(60 + (i % 12));
    }
    primeSeqLane(arp, pitches, rests, 32);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.timeSignatureNumerator = 4;
    ctx.timeSignatureDenominator = 4;
    ctx.isPlaying = true;

    // At 120 BPM, 4/4, 44.1kHz: bar = 4 * 22050 = 88200 samples.
    // Start the transport in the middle of a bar so the playhead advances a
    // few steps before the next bar boundary fires.
    const int64_t barSamples = static_cast<int64_t>(ctx.samplesPerBar());
    REQUIRE(barSamples > 0);

    // Run starting at sample 0 for half a bar's worth of blocks (no boundary
    // because the first block starts exactly at a bar boundary which resets
    // immediately to step 0). Use a non-zero start position instead so the
    // playhead can advance before the next bar boundary.
    ctx.transportPositionSamples = barSamples / 2;  // mid-bar

    // Advance until just BEFORE the next bar boundary so the playhead is
    // non-zero.
    const size_t blocksToAdvance = static_cast<size_t>(
        (barSamples - ctx.transportPositionSamples) / static_cast<int64_t>(ctx.blockSize));
    REQUIRE(blocksToAdvance > 0);
    (void)collectEvents(arp, ctx, blocksToAdvance > 1 ? blocksToAdvance - 1 : 1);

    // Sanity: the playhead should have advanced off step 0 after running
    // through several sixteenth-note steps (one 1/16 @120BPM = 5512 samples;
    // half a bar is 44100 samples → ~8 sixteenth steps).
    REQUIRE(arp.seqNoteLane().currentStep() != 0);

    // The next processBlock call will straddle the bar boundary at
    // ctx.transportPositionSamples == barSamples, triggering the Beat retrigger
    // which calls resetLanes() — and resetLanes() resets seqNoteLane_ (see
    // arpeggiator_core.h:2671).
    (void)collectEvents(arp, ctx, 2);

    REQUIRE(arp.seqNoteLane().currentStep() == 0);
}

// =============================================================================
// SC-002: side-by-side parity — Sequencer output threads through downstream
//          lanes the same way Live mode does
// =============================================================================

TEST_CASE("ArpeggiatorCore: SC-002 side-by-side sequencer output matches equivalent live-mode output",
          "[arpeggiator_core][sequencer][SC-002]")
{
    // Configure two cores with identical downstream-lane settings. Instance A
    // runs in Live mode with one held note (64); instance B runs in Sequencer
    // mode with a 1-step pattern of pitch=64. Both should emit the same pitch
    // sequence and velocity through the downstream lane pipeline. We hold the
    // same root note in B (heldRoot=64) so that the Seq mode's transposition
    // formula (programmedPitch + heldRoot - 60) = 64 + (64-60) = 68 — to make
    // the two paths emit identical absolute pitches we instead pin Live to no
    // transposition by holding note 64 and Seq to programmedPitch=64 with no
    // held note (heldRoot defaults to 60 → transposition = 0 → emitted = 64).
    //
    // The downstream lane pipeline (velocity, gate, modifier, ratchet,
    // condition, chord, inversion, MIDI delay) operates AFTER the source
    // selection branch, so equivalent inputs must produce equivalent outputs.

    auto configureCore = [](ArpeggiatorCore& arp) {
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);  // Live mode uses Up; Seq mode bypasses selector
        arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
        arp.setLatchMode(LatchMode::Off);
        arp.setRetrigger(ArpRetriggerMode::Off);
        // Leave all lane defaults (length=1, value=identity) so downstream
        // lanes are pass-through and the comparison is meaningful.
    };

    ArpeggiatorCore arpLive;
    ArpeggiatorCore arpSeq;
    configureCore(arpLive);
    configureCore(arpSeq);

    // Live: hold a single note 64 with velocity 100.
    arpLive.setSourceMode(SourceMode::Live);
    arpLive.noteOn(64, 100);

    // Seq: program a 1-step pattern with pitch 64; no held note → heldRoot=60,
    // transposition = 64 - 60 = ... wait, the formula adds (heldRoot - 60) so
    // with no held note heldRoot=60 → offset=0 → emitted = 64. Velocity falls
    // back to 100 per FR-025a. So Seq and Live emit (64, vel=100) repeatedly.
    arpSeq.setSourceMode(SourceMode::Sequencer);
    arpSeq.seqNoteLane().setLength(1);
    arpSeq.seqNoteLane().setStep(0, 64);
    arpSeq.seqRestFlags()[0].store(0, std::memory_order_relaxed);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    BlockContext ctxLive = ctx;
    BlockContext ctxSeq = ctx;

    constexpr size_t kBlocks = 80;  // ~1 second of audio at 44.1k/512
    auto liveEvents = collectEvents(arpLive, ctxLive, kBlocks);
    auto seqEvents  = collectEvents(arpSeq,  ctxSeq,  kBlocks);

    auto liveOns = filterNoteOns(liveEvents);
    auto seqOns  = filterNoteOns(seqEvents);

    // Both pipelines must emit the same NUMBER of note-ons over the same
    // transport window (lane timing is identical because lane modulators are
    // at defaults).
    REQUIRE(liveOns.size() == seqOns.size());
    REQUIRE(liveOns.size() >= 4);  // sanity floor

    // Pitch and velocity equivalence at each emitted note. Floating-point
    // values inside the lane chain stay deterministic for identical inputs,
    // so an exact equality check on the discrete MIDI ints is appropriate.
    for (size_t i = 0; i < liveOns.size(); ++i) {
        INFO("note index " << i);
        REQUIRE(static_cast<int>(seqOns[i].note) ==
                static_cast<int>(liveOns[i].note));
        REQUIRE(static_cast<int>(seqOns[i].velocity) ==
                static_cast<int>(liveOns[i].velocity));
    }
}

// =============================================================================
// Phase 8 (US6): FR-023 — output-side scale quantize applies in Sequencer mode.
//
// Background: Gradus does NOT have an unconditional "snap-to-scale" stage that
// runs on every emitted note (such a stage would break SC-004 byte-identical
// Live MIDI for pre-feature presets where the default scale is Chromatic and
// no transpose / pitch lane offset is applied). The "output-side scale
// quantize" referred to by FR-023 is the existing pair of scale-aware stages
// in fireStep:
//
//   1. Pitch lane offset stage (arpeggiator_core.h:2199-2218): when scale is
//      non-Chromatic AND pitch lane offset != 0, the offset is interpreted as
//      scale degrees and ScaleHarmonizer::calculate() produces an in-scale
//      result.
//   2. Global transpose stage (arpeggiator_core.h:2226-2241): when scale is
//      non-Chromatic AND transpose != 0, the transpose is interpreted as scale
//      degrees and ScaleHarmonizer::calculate() produces an in-scale result.
//
// Both stages run unconditionally regardless of sourceMode — they sit AFTER
// the Sequencer-mode early-branch (which only replaces the source pitch +
// velocity). So FR-023 is satisfied structurally: Seq-emitted notes pass
// through the same downstream stages as Live-emitted notes.
//
// The tests below verify this by exercising the scale-aware stages in Seq
// mode and comparing the output against the equivalent Live-mode pipeline
// (parity assertion — proves FR-023 without depending on the exact numerical
// output of ScaleHarmonizer::calculate(), which has its own dedicated
// coverage in dsp/tests/unit/core/scale_harmonizer_test.cpp).
// =============================================================================

TEST_CASE("ArpeggiatorCore: FR-023 output scale-aware transpose applies in Sequencer mode",
          "[arpeggiator_core][sequencer][scale][FR-023]")
{
    // Drive two cores: one in Live mode (held note 60 → emits 60 per step in
    // ArpMode::Up with single held note), one in Seq mode (programmed step
    // pitch = 60, no held note → emits 60 per step). Both with transpose=+2
    // and scale = C Natural Minor. The transpose stage runs in both code
    // paths AFTER the source pitch is resolved — outputs MUST match (FR-023).
    auto configureCommon = [](ArpeggiatorCore& arp) {
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
        arp.setLatchMode(LatchMode::Off);
        arp.setRetrigger(ArpRetriggerMode::Off);
        arp.setRootNote(0);  // C
        arp.setScaleType(ScaleType::NaturalMinor);
        arp.setTranspose(2);  // +2 scale degrees
    };

    ArpeggiatorCore arpLive;
    ArpeggiatorCore arpSeq;
    configureCommon(arpLive);
    configureCommon(arpSeq);

    arpLive.setSourceMode(SourceMode::Live);
    arpLive.noteOn(60, 100);  // Live: hold C, emit C repeatedly

    arpSeq.setSourceMode(SourceMode::Sequencer);
    arpSeq.seqNoteLane().setLength(1);
    arpSeq.seqNoteLane().setStep(0, 60);                          // programmed pitch = C
    arpSeq.seqRestFlags()[0].store(0, std::memory_order_relaxed); // not a rest

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    BlockContext ctxLive = ctx;
    BlockContext ctxSeq = ctx;

    constexpr size_t kBlocks = 80;
    auto liveOns = filterNoteOns(collectEvents(arpLive, ctxLive, kBlocks));
    auto seqOns  = filterNoteOns(collectEvents(arpSeq,  ctxSeq,  kBlocks));

    REQUIRE(liveOns.size() == seqOns.size());
    REQUIRE(liveOns.size() >= 4);  // sanity floor

    // Parity: the transpose stage produced identical emitted pitches in both
    // pipelines. If the scale-aware transpose stage were bypassed in Seq mode
    // (regression), Seq emissions would be Live + 0 (no transpose) while
    // Live emissions would be the scale-transposed value — they would differ.
    for (size_t i = 0; i < liveOns.size(); ++i) {
        INFO("note index " << i);
        REQUIRE(static_cast<int>(seqOns[i].note) ==
                static_cast<int>(liveOns[i].note));
    }

    // Additional sanity: the transposed result is in C minor (the scale stage
    // ran). Natural Minor degrees indexed from 0:
    //   d0=C(0), d1=D(2), d2=Eb(3), d3=F(5), d4=G(7), d5=Ab(8), d6=Bb(10).
    // C (degree 0) + 2 scale degrees = Eb (degree 2, semitone 3) = MIDI 63.
    // (Direct Chromatic +2 would give 62 = D natural — which is NOT in C minor.)
    REQUIRE(static_cast<int>(seqOns[0].note) == 63);
}

TEST_CASE("ArpeggiatorCore: FR-023 output scale Chromatic passes pattern through unquantized",
          "[arpeggiator_core][sequencer][scale][FR-023]")
{
    // With scale=Chromatic both the pitch lane and global transpose stages
    // perform direct semitone math (no scale snap). A Seq pattern with no
    // transpose / no pitch offset must emit programmed pitches unchanged.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setSourceMode(SourceMode::Sequencer);
    arp.setScaleType(ScaleType::Chromatic);  // explicit — default is already Chromatic
    arp.setTranspose(0);

    primeSeqLane(arp, {60, 64, 67, 60}, {0, 0, 0, 0}, 4);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto noteOns = filterNoteOns(collectEvents(arp, ctx, 80));
    REQUIRE(noteOns.size() >= 4);

    // No scale quantization applied — programmed pitches emitted exactly.
    REQUIRE(static_cast<int>(noteOns[0].note) == 60);
    REQUIRE(static_cast<int>(noteOns[1].note) == 64);
    REQUIRE(static_cast<int>(noteOns[2].note) == 67);
    REQUIRE(static_cast<int>(noteOns[3].note) == 60);
}

TEST_CASE("ArpeggiatorCore: FR-021 pitch formula stacks before scale-aware stages",
          "[arpeggiator_core][sequencer][transpose][FR-021]")
{
    // FR-021 / FR-021a: finalPitch = programmedPitch + (heldRoot - 60)
    //                              + kArpTranspose + pitchLaneOffset,
    // evaluated BEFORE the output scale-aware transpose stage.
    //
    // We use scale=Chromatic so each stage is a direct semitone add, making
    // the additive contract observable as integer arithmetic. Setup:
    //   programmedPitch       = 60   (Seq lane step 0)
    //   heldRoot              = 62   (hold note 62 → +(62-60) = +2)
    //   kArpTranspose         = +1
    //   pitch lane offset     = +1   (set via pitchLane().setStep(0, 1))
    // Expected emit = 60 + 2 + 1 + 1 = 64.
    //
    // Verifying the FULL stacked value (64) reaches the emission proves all
    // four stages ran in order. If transpose or the pitch lane stage were
    // bypassed in Seq mode, the result would be < 64.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setSourceMode(SourceMode::Sequencer);
    arp.setScaleType(ScaleType::Chromatic);

    // Programmed pitch = 60 at step 0; length=1 so step 0 fires every time.
    primeSeqLane(arp, {60}, {0}, 1);

    // Pitch lane: length=1, step 0 value = +1.
    arp.pitchLane().setLength(1);
    arp.pitchLane().setStep(0, static_cast<int8_t>(1));

    // Global transpose = +1.
    arp.setTranspose(1);

    // Hold note 62 → heldRoot=62 → +(62-60)=+2.
    arp.noteOn(62, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto noteOns = filterNoteOns(collectEvents(arp, ctx, 80));
    REQUIRE(noteOns.size() >= 4);

    // Stacked: 60 + 2 + 1 + 1 = 64. Each emit must equal 64 (length=1 pattern,
    // held note unchanged, all stages additive in Chromatic mode).
    for (size_t i = 0; i < noteOns.size(); ++i) {
        INFO("note index " << i << " (FR-021 stacking)");
        REQUIRE(static_cast<int>(noteOns[i].note) == 64);
    }
}
