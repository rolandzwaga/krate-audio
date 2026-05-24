// ==============================================================================
// Source-Mode Held-Note Transposition Semantics Tests (spec 142, Phase 4, US2)
// ==============================================================================
// Comprehensive held-note transposition test coverage for the Sequencer Note
// lane path in `ArpeggiatorCore::fireStep`. The transposition formula itself
// (programmedPitch + (heldRoot-60) + kArpTranspose + pitchLaneOffset, clamped
// per stage) was wired in Phase 3 (T030c); this phase pins down the held-note
// semantics independently.
//
// Coverage:
//   * SC-003: single held note transposes by (heldNote - 60) for 12 root notes
//             sweeping the keyboard.
//   * FR-017: last-played still-held note wins as transposition root.
//   * FR-018: release of last-held note falls back to next-most-recent;
//             empty held-notes buffer = no transposition.
//   * FR-025a: empty held-notes buffer = base velocity 100; held-note velocity
//              passes through as the emitted base velocity.
//   * FR-021 / FR-021a: pitch lane + held-note transpose + kArpTranspose all
//             stack additively.
//   * FR-024: out-of-range emitted pitch is clamped to [0, 127] (per-stage
//             clamping per the Phase 3 compliance note).
//   * SC-010: transposition root updates within one audio block of note-on.
//
// Reference: specs/142-gradus-piano-roll-sequencer/{spec,plan,tasks}.md
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

// Run the arp for `numBlocks` blocks and collect every event with absolute
// (per-transport) sample offsets. Mirrors collectEvents in the Phase 3 test.
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

// Configure a vanilla Seq-mode arp with the given pattern.
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

// Standard test context: 44.1 kHz, 512-sample blocks, 120 BPM, playing.
BlockContext makeContext()
{
    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    return ctx;
}

// Default prepare + Seq-mode setup. Retrigger=Off so noteOn does NOT reset the
// playhead — we want to observe transposition root changes without playhead
// resets perturbing the test.
void prepareSeqMode(ArpeggiatorCore& arp)
{
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setRetrigger(ArpRetriggerMode::Off);
    arp.setSourceMode(SourceMode::Sequencer);
}

}  // namespace

// =============================================================================
// SC-003: single held note transposes by (heldNote - 60) for 12 root notes
// =============================================================================

TEST_CASE("Seq transpose: SC-003 single held note transposes by heldNote-60 for 12 root notes",
          "[arpeggiator_core][sequencer][transpose][SC-003]")
{
    // Sweep heldNote across 12 chromatic values spanning the keyboard. For
    // each, assert every emitted programmed pitch == programmedPitch +
    // (heldNote - 60) exactly. The pattern uses pitches well inside the
    // valid range so clamping never kicks in.
    constexpr std::array<uint8_t, 12> kHeldNotes{
        48, 50, 52, 55, 57, 59, 60, 62, 64, 67, 69, 71};
    constexpr std::array<uint8_t, 4> kPattern{60, 64, 67, 60};

    for (uint8_t heldNote : kHeldNotes) {
        ArpeggiatorCore arp;
        prepareSeqMode(arp);
        primeSeqLane(arp,
                     {kPattern[0], kPattern[1], kPattern[2], kPattern[3]},
                     {0, 0, 0, 0}, 4);

        arp.noteOn(heldNote, 100);

        auto ctx = makeContext();
        auto noteOns = filterNoteOns(collectEvents(arp, ctx, 80));
        REQUIRE(noteOns.size() >= 4);

        const int expectedOffset =
            static_cast<int>(heldNote) - 60;
        for (size_t step = 0; step < 4; ++step) {
            INFO("heldNote=" << static_cast<int>(heldNote)
                             << " step=" << step
                             << " programmed=" << static_cast<int>(kPattern[step]));
            const int expectedPitch = std::clamp(
                static_cast<int>(kPattern[step]) + expectedOffset, 0, 127);
            REQUIRE(static_cast<int>(noteOns[step].note) == expectedPitch);
        }
    }
}

// =============================================================================
// FR-017: last-played still-held note wins as transposition root
// =============================================================================

TEST_CASE("Seq transpose: FR-017 last-played note wins as transposition root",
          "[arpeggiator_core][sequencer][transpose][FR-017]")
{
    ArpeggiatorCore arp;
    prepareSeqMode(arp);
    primeSeqLane(arp, {60, 60, 60, 60}, {0, 0, 0, 0}, 4);

    // Hold 60 (no transpose), then add 65 (transpose +5) while 60 still held.
    arp.noteOn(60, 100);
    arp.noteOn(65, 100);

    auto ctx = makeContext();
    auto noteOns = filterNoteOns(collectEvents(arp, ctx, 80));
    REQUIRE(noteOns.size() >= 4);

    // Most-recently-pressed note (65) is the transposition root.
    // 60 (programmed) + (65 - 60) = 65 per step.
    for (size_t i = 0; i < 4; ++i) {
        INFO("step=" << i);
        REQUIRE(static_cast<int>(noteOns[i].note) == 65);
    }
}

// =============================================================================
// FR-018: release of last-held note falls back to next-most-recent
// =============================================================================

TEST_CASE("Seq transpose: FR-018 release of last-held note falls back to next-most-recent",
          "[arpeggiator_core][sequencer][transpose][FR-018]")
{
    ArpeggiatorCore arp;
    prepareSeqMode(arp);
    primeSeqLane(arp, {60, 60, 60, 60}, {0, 0, 0, 0}, 4);

    // Hold 60, then add 65 -> root should be 65. Release 65 -> root reverts
    // to 60 (the only remaining held note).
    arp.noteOn(60, 100);
    arp.noteOn(65, 100);
    arp.noteOff(65);

    // 60 should still be in the held buffer (no Latch interactions in Seq
    // mode per T030e).
    REQUIRE_FALSE(arp.heldNotes().empty());

    auto ctx = makeContext();
    auto noteOns = filterNoteOns(collectEvents(arp, ctx, 80));
    REQUIRE(noteOns.size() >= 4);

    // Programmed 60 + (60 - 60) = 60.
    for (size_t i = 0; i < 4; ++i) {
        INFO("step=" << i);
        REQUIRE(static_cast<int>(noteOns[i].note) == 60);
    }
}

// =============================================================================
// FR-015 / FR-025a: no held notes = no transposition, base velocity = 100
// =============================================================================

TEST_CASE("Seq transpose: no held notes = no transposition and base velocity = 100",
          "[arpeggiator_core][sequencer][transpose][FR-015][FR-025a]")
{
    ArpeggiatorCore arp;
    prepareSeqMode(arp);
    primeSeqLane(arp, {60, 64, 67, 60}, {0, 0, 0, 0}, 4);

    REQUIRE(arp.heldNotes().empty());

    auto ctx = makeContext();
    auto noteOns = filterNoteOns(collectEvents(arp, ctx, 80));
    REQUIRE(noteOns.size() >= 4);

    // Programmed pitches pass through unchanged.
    REQUIRE(static_cast<int>(noteOns[0].note) == 60);
    REQUIRE(static_cast<int>(noteOns[1].note) == 64);
    REQUIRE(static_cast<int>(noteOns[2].note) == 67);
    REQUIRE(static_cast<int>(noteOns[3].note) == 60);

    // Base velocity falls back to 100 with no held note (FR-025a). The
    // velocity lane defaults to a 1.0 multiplier so the emitted velocity
    // equals the base velocity.
    for (size_t i = 0; i < 4; ++i) {
        INFO("step=" << i);
        REQUIRE(static_cast<int>(noteOns[i].velocity) == 100);
    }
}

// =============================================================================
// FR-025a: held note velocity used as base velocity
// =============================================================================

TEST_CASE("Seq transpose: held note velocity used as base velocity",
          "[arpeggiator_core][sequencer][transpose][FR-025a]")
{
    ArpeggiatorCore arp;
    prepareSeqMode(arp);
    primeSeqLane(arp, {60, 64, 67, 60}, {0, 0, 0, 0}, 4);

    // Hold note 60 (no transposition) with velocity 80. The velocity lane
    // defaults to 1.0 passthrough, so the emitted velocity equals 80 exactly.
    arp.noteOn(60, 80);

    auto ctx = makeContext();
    auto noteOns = filterNoteOns(collectEvents(arp, ctx, 80));
    REQUIRE(noteOns.size() >= 4);

    for (size_t i = 0; i < 4; ++i) {
        INFO("step=" << i);
        REQUIRE(static_cast<int>(noteOns[i].velocity) == 80);
    }

    // Sanity: no transposition because heldRoot=60.
    REQUIRE(static_cast<int>(noteOns[0].note) == 60);
    REQUIRE(static_cast<int>(noteOns[1].note) == 64);
}

// =============================================================================
// FR-021 / FR-021a: pitch lane + held-note transpose + kArpTranspose stack
// =============================================================================

TEST_CASE("Seq transpose: FR-021 pitch lane + held-note + kArpTranspose stack additively",
          "[arpeggiator_core][sequencer][transpose][FR-021][FR-021a]")
{
    ArpeggiatorCore arp;
    prepareSeqMode(arp);

    // Length=1, programmedPitch=60, single step.
    primeSeqLane(arp, {60}, {0}, 1);

    // Pitch lane: length=1, step[0] = +1 semitone. Default scale is Chromatic
    // so the offset is interpreted as raw semitones.
    arp.pitchLane().setLength(1);
    arp.pitchLane().setStep(0, static_cast<int8_t>(1));

    // Global kArpTranspose = +2 semitones.
    arp.setTranspose(2);

    // Hold note 62 -> heldRoot-60 = +2.
    arp.noteOn(62, 100);

    auto ctx = makeContext();
    auto noteOns = filterNoteOns(collectEvents(arp, ctx, 80));
    REQUIRE(noteOns.size() >= 1);

    // finalPitch = 60 (programmed) + 2 (heldRoot-60) + 2 (kArpTranspose)
    //                              + 1 (pitchLane) = 65.
    for (size_t i = 0; i < noteOns.size(); ++i) {
        INFO("step=" << i);
        REQUIRE(static_cast<int>(noteOns[i].note) == 65);
    }
}

// =============================================================================
// SC-010: transposition root updates within one audio block of note-on
// =============================================================================

TEST_CASE("Seq transpose: SC-010 transposition root updates within one audio block of note-on",
          "[arpeggiator_core][sequencer][transpose][SC-010]")
{
    ArpeggiatorCore arp;
    prepareSeqMode(arp);

    // Repeating single-step pattern at pitch 60 — easy to observe transposed
    // emissions every step.
    primeSeqLane(arp, {60}, {0}, 1);

    auto ctx = makeContext();

    // Phase 1: drive several blocks at heldRoot=60 (default fallback). All
    // emitted notes are exactly 60.
    auto phase1 = filterNoteOns(collectEvents(arp, ctx, 40));
    REQUIRE(phase1.size() >= 1);
    for (const auto& on : phase1) {
        REQUIRE(static_cast<int>(on.note) == 60);
    }

    // Fire a noteOn for the new root BETWEEN blocks (boundary granularity).
    // SC-010 says the transposition root must take effect within one audio
    // block of the note-on event.
    arp.noteOn(65, 100);

    // Phase 2: drive at least one more block. Every emitted note MUST already
    // reflect the new root (60 + (65-60) = 65). The FIRST emitted note-on in
    // this phase proves the within-one-block guarantee.
    auto phase2 = filterNoteOns(collectEvents(arp, ctx, 40));
    REQUIRE(phase2.size() >= 1);
    REQUIRE(static_cast<int>(phase2.front().note) == 65);
    for (const auto& on : phase2) {
        REQUIRE(static_cast<int>(on.note) == 65);
    }
}

// =============================================================================
// FR-024: out-of-range emitted pitch is clamped to [0, 127]
// =============================================================================

TEST_CASE("Seq transpose: FR-024 out-of-range pitch is clamped to [0,127]",
          "[arpeggiator_core][sequencer][transpose][FR-024]")
{
    SECTION("downward transpose: programmed 110 + heldRoot 30 -> -30 -> 80") {
        ArpeggiatorCore arp;
        prepareSeqMode(arp);
        primeSeqLane(arp, {110}, {0}, 1);

        // heldRoot=30 -> offset = 30 - 60 = -30. 110 + (-30) = 80 (in range).
        arp.noteOn(30, 100);

        auto ctx = makeContext();
        auto noteOns = filterNoteOns(collectEvents(arp, ctx, 40));
        REQUIRE(noteOns.size() >= 1);
        REQUIRE(static_cast<int>(noteOns.front().note) == 80);
    }

    SECTION("upward pitch-lane overflow: programmed 110 + heldRoot 60 + pitchLane +24 -> 127 (clamped)") {
        ArpeggiatorCore arp;
        prepareSeqMode(arp);
        primeSeqLane(arp, {110}, {0}, 1);

        // heldRoot=60 -> no held-note transpose.
        arp.noteOn(60, 100);

        // Pitch lane +24 semitones at length=1, step[0].
        arp.pitchLane().setLength(1);
        arp.pitchLane().setStep(0, static_cast<int8_t>(24));

        auto ctx = makeContext();
        auto noteOns = filterNoteOns(collectEvents(arp, ctx, 40));
        REQUIRE(noteOns.size() >= 1);

        // Phase 3 per-stage clamping: the Seq-mode clamp keeps the intermediate
        // pitch <=127 (110 + 0 = 110), then the pitch-lane stage clamps
        // 110 + 24 = 134 down to 127. Final emitted pitch must be 127.
        REQUIRE(static_cast<int>(noteOns.front().note) == 127);
    }

    SECTION("downward underflow: programmed 5 + heldRoot 40 -> -20 -> clamp(-15,0,127) = 0") {
        ArpeggiatorCore arp;
        prepareSeqMode(arp);
        primeSeqLane(arp, {5}, {0}, 1);

        // heldRoot=40 -> offset = 40 - 60 = -20. 5 + (-20) = -15 -> clamp to 0.
        arp.noteOn(40, 100);

        auto ctx = makeContext();
        auto noteOns = filterNoteOns(collectEvents(arp, ctx, 40));
        REQUIRE(noteOns.size() >= 1);
        REQUIRE(static_cast<int>(noteOns.front().note) == 0);
    }
}

// =============================================================================
// T039 sanity: kArpTransposeId path participates in Sequencer mode
// =============================================================================
//
// T039 audit: confirms `transpose_` is read unconditionally in `fireStep`
// (the global Transpose stage at ~line 2227) — NOT gated by Live mode. The
// FR-021 stacking test above proves this end-to-end with all three offsets
// active, but this isolated test pins down the kArpTranspose path on its own
// so a regression that re-introduces a Live-mode gate is caught immediately.

TEST_CASE("Seq transpose: kArpTransposeId active in Sequencer mode (T039)",
          "[arpeggiator_core][sequencer][transpose][FR-021a]")
{
    ArpeggiatorCore arp;
    prepareSeqMode(arp);
    primeSeqLane(arp, {60}, {0}, 1);

    // No held note (heldRoot=60 fallback), no pitch lane offset (default).
    // Only kArpTranspose contributes.
    arp.setTranspose(7);

    auto ctx = makeContext();
    auto noteOns = filterNoteOns(collectEvents(arp, ctx, 40));
    REQUIRE(noteOns.size() >= 1);

    // 60 (programmed) + 0 (heldRoot-60) + 7 (kArpTranspose) + 0 (pitch lane) = 67.
    for (const auto& on : noteOns) {
        REQUIRE(static_cast<int>(on.note) == 67);
    }
}
