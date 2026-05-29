// ==============================================================================
// Source-Mode FR-022 Inertness + Source Toggle Hygiene Tests
// (spec 142, Phase 7, User Story 5)
// ==============================================================================
// Verifies the audio-thread inertness of FR-022 controls when Source = Sequencer
// (T067) and the cleanliness of source-mode toggles mid-playback (T068).
//
// Inertness coverage (T067):
//   * FR-022: ArpMode changes do not alter MIDI output in Sequencer mode.
//   * FR-022: OctaveRange / OctaveMode changes are inert in Sequencer mode.
//   * FR-022: Markov enabled (ArpMode::Markov + matrix) has no effect.
//   * FR-022: Euclidean enabled (setEuclideanEnabled(true) + steps/hits) has no effect.
//   * FR-022: LatchMode ignored — transposition root reverts on physical release.
//   * FR-022a: Retrigger Note / Beat still active (resets lane 9 playhead).
//   * FR-022b: Spice / Humanize still active (perturbs emitted notes).
//
// Source toggle hygiene coverage (T068):
//   * SC-007: 100 source toggles mid-playback produce zero stuck notes.
//   * FR-025: note-off emitted on source toggle for a sounding programmed note.
//   * FR-025: pending MIDI delay echoes survive source toggle — natural tail-out.
//             (Processor-level test; the MidiNoteDelay pending queue lives in the
//             Processor, not in ArpeggiatorCore.)
//   * velocity=0 noteOn treated as note-off in Sequencer mode (held-note buffer
//     update follows note-off semantics; previous held note becomes the new root).
//   * lane playheads unchanged after source toggle (Q5-A).
//
// T070 verification:
//   * Latch=Hold + Seq mode end-to-end: transposition root reverts to "no
//     transposition" (root = 60) on physical release. Emitted pitch equals
//     programmed pitch — proves the noteOff bypass in ArpeggiatorCore::noteOff
//     (Phase 3 T030e) correctly drops the latched-released note from the
//     transposition-root computation in fireStep.
//
// Reference: specs/142-gradus-piano-roll-sequencer/{spec,plan,tasks}.md
// ==============================================================================

#include <krate/dsp/processors/arpeggiator_core.h>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include "vst_param_changes.h"
#include "vst_event_list.h"

using namespace Krate::DSP;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// =============================================================================
// Core-level helpers (shared across the T067/T068/T070 ArpeggiatorCore tests)
// =============================================================================

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

std::vector<ArpEvent> filterNoteOffs(const std::vector<ArpEvent>& events)
{
    std::vector<ArpEvent> out;
    for (const auto& e : events) {
        if (e.type == ArpEvent::Type::NoteOff) out.push_back(e);
    }
    return out;
}

// Extract just the pitch sequence from a list of note-on events.
std::vector<int> noteOnPitches(const std::vector<ArpEvent>& events)
{
    std::vector<int> out;
    for (const auto& e : events) {
        if (e.type == ArpEvent::Type::NoteOn) {
            out.push_back(static_cast<int>(e.note));
        }
    }
    return out;
}

// Configure the Sequencer Note lane with the given pattern.
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

BlockContext makeContext()
{
    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    return ctx;
}

// Vanilla Seq-mode setup: enabled, 1/16, retrigger Off (so noteOn doesn't reset
// the playhead and confuse "after toggle, playhead is unchanged" assertions),
// latch Off.
void prepareSeqMode(ArpeggiatorCore& arp)
{
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setRetrigger(ArpRetriggerMode::Off);
    arp.setLatchMode(LatchMode::Off);
    arp.setSourceMode(SourceMode::Sequencer);
}

}  // namespace

// =============================================================================
// T067: FR-022 audio-thread inertness in Sequencer mode
// =============================================================================

TEST_CASE("Seq toggle: FR-022 ArpMode change has no effect on MIDI output in Sequencer mode",
          "[arpeggiator_core][sequencer][FR-022][inert][US5]")
{
    // Baseline: run with ArpMode::Up. Compare every other ArpMode value's output
    // pitch sequence — they must all be identical because lane 9 (Sequencer
    // Note) bypasses the selector entirely in Seq mode.
    auto runWithMode = [](ArpMode mode) -> std::vector<int> {
        ArpeggiatorCore arp;
        prepareSeqMode(arp);
        arp.setMode(mode);  // Setting ArpMode also resets the selector internally.
        primeSeqLane(arp, {60, 64, 67, 60}, {0, 0, 0, 0}, 4);

        BlockContext ctx = makeContext();
        auto events = collectEvents(arp, ctx, 80);
        return noteOnPitches(events);
    };

    const auto baseline = runWithMode(ArpMode::Up);
    REQUIRE(baseline.size() >= 4);

    // Cycle every ArpMode value and assert the emitted pitch sequence is
    // identical. The sequencer-note path must NOT consult the selector.
    const std::array<ArpMode, 12> allModes = {
        ArpMode::Up, ArpMode::Down, ArpMode::UpDown, ArpMode::DownUp,
        ArpMode::Converge, ArpMode::Diverge, ArpMode::Random, ArpMode::Walk,
        ArpMode::AsPlayed, ArpMode::Chord, ArpMode::Gravity, ArpMode::Markov
    };
    for (auto m : allModes) {
        const auto out = runWithMode(m);
        INFO("ArpMode " << static_cast<int>(m));
        REQUIRE(out == baseline);
    }
}

TEST_CASE("Seq toggle: FR-022 OctaveRange / OctaveMode changes inert in Sequencer mode",
          "[arpeggiator_core][sequencer][FR-022][inert][US5]")
{
    auto runWith = [](int range, OctaveMode mode) -> std::vector<int> {
        ArpeggiatorCore arp;
        prepareSeqMode(arp);
        arp.setOctaveRange(range);
        arp.setOctaveMode(mode);
        primeSeqLane(arp, {60, 64, 67, 60}, {0, 0, 0, 0}, 4);
        BlockContext ctx = makeContext();
        auto events = collectEvents(arp, ctx, 80);
        return noteOnPitches(events);
    };

    const auto baseline = runWith(1, OctaveMode::Sequential);
    REQUIRE(baseline.size() >= 4);

    // 12 combinations of range x mode — every result must equal baseline.
    for (int r = 1; r <= 4; ++r) {
        for (auto m : {OctaveMode::Sequential, OctaveMode::Interleaved}) {
            const auto out = runWith(r, m);
            INFO("OctaveRange=" << r << " mode=" << static_cast<int>(m));
            REQUIRE(out == baseline);
        }
    }
}

TEST_CASE("Seq toggle: FR-022 Markov enabled has no effect in Sequencer mode",
          "[arpeggiator_core][sequencer][FR-022][inert][US5]")
{
    // Baseline: vanilla Seq mode, ArpMode::Up, no Markov.
    auto baseline = [] {
        ArpeggiatorCore arp;
        prepareSeqMode(arp);
        primeSeqLane(arp, {60, 64, 67, 60}, {0, 0, 0, 0}, 4);
        BlockContext ctx = makeContext();
        return noteOnPitches(collectEvents(arp, ctx, 80));
    }();

    // With Markov enabled (mode + non-trivial transition matrix), the live-mode
    // path would generate noise; the seq-mode path must ignore the matrix.
    ArpeggiatorCore arp;
    prepareSeqMode(arp);
    arp.setMode(ArpMode::Markov);

    // Build a non-identity Markov matrix that would obviously perturb the
    // selector output in Live mode (uniform 1/7 transitions).
    std::array<float, kMarkovMatrixSize> matrix{};
    for (auto& v : matrix) v = 1.0f / static_cast<float>(kMarkovMatrixDim);
    arp.setMarkovMatrix(matrix);

    primeSeqLane(arp, {60, 64, 67, 60}, {0, 0, 0, 0}, 4);
    BlockContext ctx = makeContext();
    const auto out = noteOnPitches(collectEvents(arp, ctx, 80));

    REQUIRE(out == baseline);
}

TEST_CASE("Seq toggle: FR-022 Euclidean enabled has no effect in Sequencer mode",
          "[arpeggiator_core][sequencer][FR-022][inert][US5]")
{
    // Baseline: vanilla Seq mode, no Euclidean gating.
    auto baseline = [] {
        ArpeggiatorCore arp;
        prepareSeqMode(arp);
        primeSeqLane(arp, {60, 64, 67, 60}, {0, 0, 0, 0}, 4);
        BlockContext ctx = makeContext();
        return noteOnPitches(collectEvents(arp, ctx, 80));
    }();

    // With Euclidean enabled at 3/8 (sparse) the Live-mode path would gate
    // notes off; the Seq-mode path must NOT consult the Euclidean state.
    ArpeggiatorCore arp;
    prepareSeqMode(arp);
    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(3);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    primeSeqLane(arp, {60, 64, 67, 60}, {0, 0, 0, 0}, 4);
    BlockContext ctx = makeContext();
    const auto out = noteOnPitches(collectEvents(arp, ctx, 80));

    REQUIRE(out == baseline);
}

TEST_CASE("Seq toggle: FR-022 LatchMode ignored in Sequencer mode — transposition root reverts on physical release",
          "[arpeggiator_core][sequencer][FR-022][latch][US5]")
{
    // For every LatchMode value, in Seq mode the transposition root MUST
    // revert on the physical release of the last held note. We hold note 65
    // (transposition +5), release it, then drive the sequencer; emitted pitch
    // must equal the programmed pitch (no transposition).
    for (auto mode : {LatchMode::Off, LatchMode::Hold, LatchMode::Add}) {
        ArpeggiatorCore arp;
        prepareSeqMode(arp);
        arp.setLatchMode(mode);
        primeSeqLane(arp, {60, 60, 60, 60}, {0, 0, 0, 0}, 4);

        arp.noteOn(65, 100);
        arp.noteOff(65);

        BlockContext ctx = makeContext();
        auto events = collectEvents(arp, ctx, 80);
        auto noteOns = filterNoteOns(events);
        REQUIRE(noteOns.size() >= 4);

        INFO("LatchMode " << static_cast<int>(mode));
        // The first emitted note-on must be at the programmed pitch (60) — no
        // transposition residue from the released note 65.
        REQUIRE(static_cast<int>(noteOns[0].note) == 60);
    }
}

TEST_CASE("Seq toggle: FR-022a Retrigger Note still active in Sequencer mode",
          "[arpeggiator_core][sequencer][FR-022a][retrigger][US5]")
{
    // With Retrigger=Note, a new held note-on must reset lane 9 playhead.
    // (Mirrors the Phase 3 retrigger test, but explicitly framed as an FR-022a
    // inertness-counterexample: Retrigger is NOT in the FR-022 disabled bucket.)
    ArpeggiatorCore arp;
    prepareSeqMode(arp);
    arp.setRetrigger(ArpRetriggerMode::Note);
    std::vector<uint8_t> pitches(32);
    std::vector<int>     rests(32, 0);
    for (size_t i = 0; i < 32; ++i) pitches[i] = static_cast<uint8_t>(60 + (i % 12));
    primeSeqLane(arp, pitches, rests, 32);

    BlockContext ctx = makeContext();
    arp.noteOn(60, 100);
    (void)collectEvents(arp, ctx, 80);
    REQUIRE(arp.seqNoteLane().currentStep() != 0);

    arp.noteOn(67, 100);
    REQUIRE(arp.seqNoteLane().currentStep() == 0);
}

TEST_CASE("Seq toggle: FR-022a Retrigger Beat still active in Sequencer mode",
          "[arpeggiator_core][sequencer][FR-022a][retrigger][US5]")
{
    // Beat retrigger must still reset lane 9 on a bar boundary in Seq mode.
    ArpeggiatorCore arp;
    prepareSeqMode(arp);
    arp.setRetrigger(ArpRetriggerMode::Beat);
    std::vector<uint8_t> pitches(32);
    std::vector<int>     rests(32, 0);
    for (size_t i = 0; i < 32; ++i) pitches[i] = static_cast<uint8_t>(60 + (i % 12));
    primeSeqLane(arp, pitches, rests, 32);

    BlockContext ctx = makeContext();
    ctx.timeSignatureNumerator = 4;
    ctx.timeSignatureDenominator = 4;

    const int64_t barSamples = static_cast<int64_t>(ctx.samplesPerBar());
    REQUIRE(barSamples > 0);

    // Start mid-bar so the playhead can advance off step 0 before the next
    // boundary fires.
    ctx.transportPositionSamples = barSamples / 2;
    const size_t blocksToAdvance = static_cast<size_t>(
        (barSamples - ctx.transportPositionSamples)
        / static_cast<int64_t>(ctx.blockSize));
    REQUIRE(blocksToAdvance > 0);
    (void)collectEvents(arp, ctx, blocksToAdvance > 1 ? blocksToAdvance - 1 : 1);
    REQUIRE(arp.seqNoteLane().currentStep() != 0);

    // Straddling the bar boundary triggers resetLanes(), which resets lane 9.
    (void)collectEvents(arp, ctx, 2);
    REQUIRE(arp.seqNoteLane().currentStep() == 0);
}

TEST_CASE("Seq toggle: FR-022b Spice still active in Sequencer mode",
          "[arpeggiator_core][sequencer][FR-022b][spice][US5]")
{
    // Spice blends a per-step overlay onto the velocity/gate/ratchet/condition
    // lanes (077-spice-dice-humanize). Even though we cannot construct an
    // identity-conflict with the sequencer's pitch path, we CAN show that
    // setting Spice > 0 changes the emitted velocities relative to Spice = 0
    // (which proves Spice is still applied in Seq mode — FR-022b).

    auto runWith = [](float spice) -> std::vector<int> {
        ArpeggiatorCore arp;
        prepareSeqMode(arp);
        // Spice blends per-step random overlays onto the lane values; the
        // overlays default to the identity (1.0) so we must roll the dice
        // first to get non-trivial overlay values that can drive a visible
        // velocity change when Spice > 0.
        arp.triggerDice();
        arp.setSpice(spice);
        // Program a play-all pattern (rest=0) so every step emits.
        primeSeqLane(arp, {60, 60, 60, 60}, {0, 0, 0, 0}, 4);
        BlockContext ctx = makeContext();
        auto events = collectEvents(arp, ctx, 80);
        std::vector<int> vels;
        for (const auto& e : events) {
            if (e.type == ArpEvent::Type::NoteOn) {
                vels.push_back(static_cast<int>(e.velocity));
            }
        }
        return vels;
    };

    auto vNo = runWith(0.0f);
    auto vFull = runWith(1.0f);
    REQUIRE(vNo.size() >= 4);
    REQUIRE(vFull.size() >= 4);

    // With Spice = 1.0 and the velocity overlay PRNG-seeded (default Gradus
    // behavior), at least ONE of the first ~10 emitted velocities must differ
    // from the spice=0 baseline — proving Spice is still applied. If every
    // velocity matched, Spice would have been silently bypassed in Seq mode.
    bool anyDiffer = false;
    const size_t cmpCount = std::min<size_t>(10, std::min(vNo.size(), vFull.size()));
    for (size_t i = 0; i < cmpCount; ++i) {
        if (vNo[i] != vFull[i]) { anyDiffer = true; break; }
    }
    REQUIRE(anyDiffer);
}

TEST_CASE("Seq toggle: FR-022b Humanize still active in Sequencer mode",
          "[arpeggiator_core][sequencer][FR-022b][humanize][US5]")
{
    // Humanize jitters velocity/timing/gate via PRNG (077-spice-dice-humanize).
    // With humanize=0 emitted velocities are deterministic; with humanize=1
    // they vary. As above, demonstrate that AT LEAST ONE emitted velocity
    // differs between humanize=0 and humanize=1 baselines in Seq mode.

    auto runVelocities = [](float humanize) -> std::vector<int> {
        ArpeggiatorCore arp;
        prepareSeqMode(arp);
        arp.setHumanize(humanize);
        primeSeqLane(arp, {60, 60, 60, 60}, {0, 0, 0, 0}, 4);
        BlockContext ctx = makeContext();
        auto events = collectEvents(arp, ctx, 80);
        std::vector<int> vels;
        for (const auto& e : events) {
            if (e.type == ArpEvent::Type::NoteOn) {
                vels.push_back(static_cast<int>(e.velocity));
            }
        }
        return vels;
    };

    auto vNo = runVelocities(0.0f);
    auto vFull = runVelocities(1.0f);
    REQUIRE(vNo.size() >= 4);
    REQUIRE(vFull.size() >= 4);

    bool anyDiffer = false;
    const size_t cmpCount = std::min<size_t>(10, std::min(vNo.size(), vFull.size()));
    for (size_t i = 0; i < cmpCount; ++i) {
        if (vNo[i] != vFull[i]) { anyDiffer = true; break; }
    }
    REQUIRE(anyDiffer);
}

// =============================================================================
// T070: end-to-end Latch=Hold + Seq mode emission check
// =============================================================================

TEST_CASE("Seq toggle: Latch=Hold + Seq mode emits programmed pitch after physical release (FR-022 end-to-end)",
          "[arpeggiator_core][sequencer][FR-022][latch][US5]")
{
    // Spec 142 T070 verification: with LatchMode=Hold and sourceMode=Sequencer,
    // a noteOn(60) immediately followed by noteOff(60) MUST leave heldNotes_
    // empty (T030e noteOff bypass), so the transposition root reverts to 60.
    // Driving fireStep must then emit the programmed pitch unchanged.

    ArpeggiatorCore arp;
    prepareSeqMode(arp);
    arp.setLatchMode(LatchMode::Hold);

    // Pattern of programmed pitch 60 — if transposition is leaking, we'd see a
    // value other than 60 in the emitted sequence.
    primeSeqLane(arp, {60, 60, 60, 60}, {0, 0, 0, 0}, 4);

    arp.noteOn(60, 100);
    arp.noteOff(60);
    REQUIRE(arp.heldNotes().empty());  // Phase 3 T030e gate

    BlockContext ctx = makeContext();
    auto events = collectEvents(arp, ctx, 80);
    auto noteOns = filterNoteOns(events);
    REQUIRE(noteOns.size() >= 4);

    // Every emitted note-on must equal the programmed pitch — no transposition.
    for (size_t i = 0; i < 4; ++i) {
        INFO("noteOn index " << i << " emitted pitch " << static_cast<int>(noteOns[i].note));
        REQUIRE(static_cast<int>(noteOns[i].note) == 60);
    }
}

// =============================================================================
// T068: Source-toggle hygiene at the ArpeggiatorCore level
// =============================================================================

TEST_CASE("Seq toggle: FR-025 note-off emitted on source toggle for sounding note",
          "[arpeggiator_core][sequencer][FR-025][toggle][US5]")
{
    // Drive the Sequencer until a note is sounding (currentArpNotes_ > 0), then
    // simulate the Processor's source-toggle path: requestPanicNoteOff() +
    // setSourceMode(Live). The next processBlock MUST emit a note-off for the
    // sounding programmed note at sampleOffset 0.
    //
    // Timing detail: a 1/16 note @ 120 BPM ≈ 7350 samples. At gate=1.0 the note
    // would natural-off after ~5880 samples (80% of step). To guarantee a note
    // is still sounding when we toggle, we use a long note value (1/2) so the
    // gate does NOT expire before our toggle: at 1/2 = ~58k samples, the first
    // note-on fires within block 1 and stays sounding for the entire test.

    ArpeggiatorCore arp;
    prepareSeqMode(arp);
    arp.setNoteValue(NoteValue::Half, NoteModifier::None);  // long note for clear sounding window
    primeSeqLane(arp, {72, 72, 72, 72}, {0, 0, 0, 0}, 4);

    BlockContext ctx = makeContext();

    // Run a few blocks until at least one note-on has been emitted and is
    // still sounding. With note value 1/2 the note-on fires almost immediately
    // and remains sounding well past our toggle point.
    auto preEvents = collectEvents(arp, ctx, 5);
    auto preOns = filterNoteOns(preEvents);
    REQUIRE(!preOns.empty());
    auto preOffs = filterNoteOffs(preEvents);
    // Defensive: the note must still be sounding (more on-events than offs).
    REQUIRE(preOns.size() > preOffs.size());

    // Toggle source (mirrors Processor::applyParams panic-on-toggle path).
    arp.requestPanicNoteOff();
    arp.setSourceMode(SourceMode::Live);

    // Next processBlock must emit a note-off at sample 0 for the sounding note.
    // In Live mode with no held notes, the empty-buffer-fallback inside
    // processBlock consumes the panic flag and emits the noteOff for every
    // entry in currentArpNotes_ at sampleOffset 0 (arpeggiator_core.h ~1099).
    std::array<ArpEvent, 128> blockEvents{};
    size_t count = arp.processBlock(ctx, blockEvents);
    bool foundPanicNoteOff = false;
    for (size_t i = 0; i < count; ++i) {
        const auto& e = blockEvents[i];
        if (e.type == ArpEvent::Type::NoteOff
            && e.sampleOffset == 0
            && static_cast<int>(e.note) == 72) {
            foundPanicNoteOff = true;
            break;
        }
    }
    REQUIRE(foundPanicNoteOff);
}

TEST_CASE("Seq toggle: lane playheads unchanged after source toggle (Q5-A)",
          "[arpeggiator_core][sequencer][toggle][Q5-A][US5]")
{
    // Toggle source while transport is running. Every lane's playhead position
    // captured immediately BEFORE the toggle must equal the playhead position
    // immediately AFTER setSourceMode() returns (no reset).

    ArpeggiatorCore arp;
    prepareSeqMode(arp);
    // Long patterns so playheads have room to advance off step 0 without
    // wrapping back during the lead-in.
    std::vector<uint8_t> pitches(32);
    std::vector<int>     rests(32, 0);
    for (size_t i = 0; i < 32; ++i) pitches[i] = static_cast<uint8_t>(60);
    primeSeqLane(arp, pitches, rests, 32);
    arp.seqNoteLane().setLength(32);
    arp.velocityLane().setLength(32);
    arp.gateLane().setLength(32);
    arp.pitchLane().setLength(32);
    arp.modifierLane().setLength(32);
    arp.ratchetLane().setLength(32);
    arp.conditionLane().setLength(32);
    arp.chordLane().setLength(32);
    arp.inversionLane().setLength(32);
    arp.midiDelayLane().setLength(32);

    BlockContext ctx = makeContext();
    (void)collectEvents(arp, ctx, 40);  // let playheads advance

    const auto before = std::array<size_t, 10>{
        arp.velocityLane().currentStep(),
        arp.gateLane().currentStep(),
        arp.pitchLane().currentStep(),
        arp.modifierLane().currentStep(),
        arp.ratchetLane().currentStep(),
        arp.conditionLane().currentStep(),
        arp.chordLane().currentStep(),
        arp.inversionLane().currentStep(),
        arp.midiDelayLane().currentStep(),
        arp.seqNoteLane().currentStep()
    };

    // Toggle without resetting any lane state (panic only affects sounding
    // note buffer, NOT playheads — Q5-A).
    arp.requestPanicNoteOff();
    arp.setSourceMode(SourceMode::Live);

    const auto after = std::array<size_t, 10>{
        arp.velocityLane().currentStep(),
        arp.gateLane().currentStep(),
        arp.pitchLane().currentStep(),
        arp.modifierLane().currentStep(),
        arp.ratchetLane().currentStep(),
        arp.conditionLane().currentStep(),
        arp.chordLane().currentStep(),
        arp.inversionLane().currentStep(),
        arp.midiDelayLane().currentStep(),
        arp.seqNoteLane().currentStep()
    };

    REQUIRE(before == after);

    // Also toggle back to Seq — still no playhead reset.
    arp.requestPanicNoteOff();
    arp.setSourceMode(SourceMode::Sequencer);

    const auto after2 = std::array<size_t, 10>{
        arp.velocityLane().currentStep(),
        arp.gateLane().currentStep(),
        arp.pitchLane().currentStep(),
        arp.modifierLane().currentStep(),
        arp.ratchetLane().currentStep(),
        arp.conditionLane().currentStep(),
        arp.chordLane().currentStep(),
        arp.inversionLane().currentStep(),
        arp.midiDelayLane().currentStep(),
        arp.seqNoteLane().currentStep()
    };
    REQUIRE(after2 == after);
}

TEST_CASE("Seq toggle: velocity=0 noteOn treated as note-off in Sequencer mode (FR-018 transposition root)",
          "[arpeggiator_core][sequencer][velocity-zero][edge-case][US5]")
{
    // Per the spec Edge Cases: a noteOn with velocity=0 is the canonical
    // "note-off alias" convention. In Sequencer mode this must be treated as
    // a note-off: the previous held note becomes the transposition root
    // (FR-018), and emitted pitch is offset accordingly.
    //
    // Note: ArpeggiatorCore::noteOn is the unconditional entry for all incoming
    // events; the velocity-zero conversion lives in the Processor layer
    // (standard VST3 MIDI handling) before noteOn() is called. We verify the
    // FR-018 fallback chain works by directly exercising the noteOn/noteOff
    // API the Processor uses: noteOn(60, 100), noteOn(65, 100) -> root=65;
    // then "vel=0" -> noteOff(65), expected root reverts to 60.

    ArpeggiatorCore arp;
    prepareSeqMode(arp);
    primeSeqLane(arp, {72, 72, 72, 72}, {0, 0, 0, 0}, 4);

    arp.noteOn(60, 100);
    arp.noteOn(65, 100);

    // Sanity: root is now 65 (last-played wins).
    REQUIRE(!arp.heldNotes().empty());
    REQUIRE(static_cast<int>(arp.heldNotes().byInsertOrder().back().note) == 65);

    // Simulate vel=0 noteOn for note 65 — the Processor's MIDI handler converts
    // this into a noteOff call before passing to the core.
    arp.noteOff(65);

    // Root reverts to 60 (FR-018: next-most-recent still-held note).
    REQUIRE(!arp.heldNotes().empty());
    REQUIRE(static_cast<int>(arp.heldNotes().byInsertOrder().back().note) == 60);

    // Drive the sequencer: programmed pitch 72 + (60-60) = 72 emitted (NOT
    // 72 + (65-60) = 77, which would indicate the root didn't revert).
    BlockContext ctx = makeContext();
    auto events = collectEvents(arp, ctx, 80);
    auto noteOns = filterNoteOns(events);
    REQUIRE(noteOns.size() >= 1);
    REQUIRE(static_cast<int>(noteOns[0].note) == 72);
}

TEST_CASE("Seq toggle: SC-007 100 source toggles mid-playback produce zero stuck notes",
          "[arpeggiator_core][sequencer][SC-007][toggle][US5]")
{
    // Toggle Source Live<->Sequencer 100 times across a continuously-running
    // transport. Hold a MIDI note throughout so Live mode emits notes too.
    // Track every NoteOn/NoteOff event; at the end, assert every note-on has
    // a matching note-off (no stuck notes).

    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setRetrigger(ArpRetriggerMode::Off);
    arp.setLatchMode(LatchMode::Off);
    arp.setSourceMode(SourceMode::Live);
    primeSeqLane(arp, {60, 64, 67, 60}, {0, 0, 0, 0}, 4);

    // Hold a note so both modes are actively producing output.
    arp.noteOn(60, 100);

    BlockContext ctx = makeContext();

    SourceMode current = SourceMode::Live;
    std::vector<ArpEvent> allEvents;
    std::array<ArpEvent, 128> blockEvents{};

    for (int toggle = 0; toggle < 100; ++toggle) {
        // Toggle source and request panic note-off (mirrors Processor wiring).
        current = (current == SourceMode::Live) ? SourceMode::Sequencer
                                                : SourceMode::Live;
        arp.requestPanicNoteOff();
        arp.setSourceMode(current);

        // Run several blocks between toggles so any sounding note has time to
        // complete its noteOff before the next toggle (and we sample more
        // event traffic).
        for (size_t b = 0; b < 5; ++b) {
            size_t count = arp.processBlock(ctx, blockEvents);
            for (size_t i = 0; i < count; ++i) {
                ArpEvent e = blockEvents[i];
                allEvents.push_back(e);
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }
    }

    // Flush: toggle back to Live + panic + drain a few more blocks so any
    // straggler noteOff can fire.
    arp.requestPanicNoteOff();
    arp.setSourceMode(SourceMode::Live);
    arp.noteOff(60);  // release held note so Live mode also stops emitting
    for (size_t b = 0; b < 50; ++b) {
        size_t count = arp.processBlock(ctx, blockEvents);
        for (size_t i = 0; i < count; ++i) {
            allEvents.push_back(blockEvents[i]);
        }
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    }

    // Tally per-pitch on/off counts. Every pitch must have offCount >= onCount
    // (no stuck notes — final state has all notes released).
    std::array<int, 128> onCount{};
    std::array<int, 128> offCount{};
    for (const auto& e : allEvents) {
        if (e.type == ArpEvent::Type::NoteOn) {
            ++onCount[e.note];
        } else if (e.type == ArpEvent::Type::NoteOff) {
            ++offCount[e.note];
        }
    }
    for (int pitch = 0; pitch < 128; ++pitch) {
        if (onCount[pitch] > 0) {
            INFO("pitch=" << pitch << " on=" << onCount[pitch]
                          << " off=" << offCount[pitch]);
            REQUIRE(offCount[pitch] >= onCount[pitch]);
        }
    }
}

// =============================================================================
// T068: MIDI delay echo survives source toggle (Processor-level test)
// =============================================================================
//
// The MidiNoteDelay's pending-echo queue lives in Gradus::Processor::midiDelay_,
// not inside ArpeggiatorCore. FR-025 mandates that pending echoes survive the
// source toggle (natural tail-out). This test asserts:
//   1. A note-on in Seq mode that would generate an echo via the MIDI delay
//      lane's active step actually DOES enqueue a pending echo.
//   2. Toggling Source mid-playback (before the echo's scheduled time) does NOT
//      flush the pending-echo queue — `midiDelay_.pendingCount()` is preserved.
//   3. The echo's NoteOn fires at the originally scheduled sample time with the
//      expected pitch and velocity.
//   4. After the toggle, NO new echoes are generated from the new source until
//      that source emits a new note.

namespace {

// Minimal IParameterChanges shim that lets us drive Source toggle through
// processParameterChanges (the Processor's source-mode toggle wiring fires on
// param edge detection in applyParams).
// Parameter-change mocks consolidated into tests/test_helpers/vst_param_changes.h
using SingleParamChange = Krate::Test::ParameterChanges;
using MultiParamChange = Krate::Test::ParameterChanges;


// IEventList mock consolidated into tests/test_helpers/vst_event_list.h
using TestEventList = Krate::Test::EventList;


struct CapturedMidi {
    int64_t absoluteSample;
    bool    isNoteOn;
    int16_t pitch;
    int     velocity;
};

}  // namespace

TEST_CASE("Seq toggle: FR-025 pending MIDI delay echoes survive source toggle — natural tail-out",
          "[processor][sequencer][FR-025][midi-delay][US5]")
{
    // Configuration: Gradus Processor in Sequencer mode with the MIDI delay
    // lane's step 0 active (feedback=3 echoes, fixed delay time). Pattern:
    // single play step at pitch=72 with the rest of the lane resting so the
    // sequencer fires exactly ONE note-on at the very first step, which then
    // schedules echoes via MidiNoteDelay.

    constexpr double kSampleRate = 44100.0;
    constexpr int32  kBlockSize  = 512;

    auto proc = std::make_unique<Gradus::Processor>();
    proc->initialize(nullptr);

    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.sampleRate = kSampleRate;
    setup.maxSamplesPerBlock = kBlockSize;
    proc->setupProcessing(setup);
    proc->setActive(true);

    using namespace Gradus;

    // Multi-param change shim: deliver a batch of param changes in ONE
    // processParameterChanges call so applyParams sees them all before the
    // first sequencer fire.
    

    // Build the configuration block: all params delivered in one process().
    // Note value: 1/16 (Sixteenth dropdown index 13). Each click is ~22050
    // samples at 1/8; 1/16 = ~11025 samples ≈ ~22 blocks. Use a fast value
    // so the first note-on fires soon after we enter Seq mode.
    // - Source = Sequencer (3741 = 1.0 normalized -> 1)
    // - Lane length = 1 (3742 = 0.0 normalized -> length 1)
    // - Step 0 pitch = 72 (3743 = 72/127 normalized)
    // - Step 0 rest = 0 (3775 = 0.0 normalized -> play)
    // - MIDI delay step 0 active = 1 (3708 normalized 1.0)
    // - MIDI delay step 0 feedback = 4 (3575 = 4/16 = 0.25)
    // - MIDI delay step 0 time mode = Free (3511 = 0.0)
    // - MIDI delay step 0 delay time = ~200ms (3543: normalized in 10..2000ms
    //   range = (200-10)/(2000-10) ~= 0.0955, ~200ms => 8820 samples ≈ 17 blocks)

    MultiParamChange setup_params;
    // Force the MIDI delay lane to length=1 so its playhead always points to
    // step 0 — otherwise lane 8 advances on each fire and `currentDelayStep`
    // would jump to step 1 (which has active=false) by the time the post-arp
    // MidiNoteDelay processes the noteOn.
    setup_params.add(kArpMidiDelayLaneLengthId, 0.0);
    setup_params.add(kArpMidiDelayActiveStep0Id, 1.0);
    setup_params.add(kArpMidiDelayFeedbackStep0Id, 0.25);
    setup_params.add(kArpMidiDelayTimeModeStep0Id, 0.0);
    setup_params.add(kArpMidiDelayTimeStep0Id, 0.0955);
    setup_params.add(kArpSequencerNoteLaneStep0Id, 72.0 / 127.0);
    setup_params.add(kArpSequencerNoteLaneRestStep0Id, 0.0);
    setup_params.add(kArpSequencerNoteLaneLengthId, 0.0);
    setup_params.add(kArpSourceModeId, 1.0);

    // Drive loop: shared block context, captures emitted MIDI events with
    // absolute sample times.
    std::vector<float> outL(static_cast<size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<size_t>(kBlockSize), 0.0f);
    float* outChannelBuffers[2] = { outL.data(), outR.data() };
    AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = outChannelBuffers;

    MultiParamChange noParamChange;
    SingleParamChange singleToggle;
    TestEventList inEvents;
    TestEventList outEvents;

    ProcessContext processContext{};
    processContext.state = ProcessContext::kPlaying
                         | ProcessContext::kTempoValid
                         | ProcessContext::kTimeSigValid;
    processContext.tempo = 120.0;
    processContext.timeSigNumerator = 4;
    processContext.timeSigDenominator = 4;
    processContext.sampleRate = kSampleRate;
    processContext.projectTimeMusic = 0.0;
    processContext.projectTimeSamples = 0;

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kBlockSize;
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.outputParameterChanges = nullptr;
    data.inputEvents = &inEvents;
    data.outputEvents = &outEvents;
    data.processContext = &processContext;

    std::vector<CapturedMidi> capturedBeforeToggle;
    std::vector<CapturedMidi> capturedAfterToggle;

    auto drainEvents = [](TestEventList& evList, int64_t blockStartSample,
                          std::vector<CapturedMidi>& dest) {
        int32 evCount = evList.getEventCount();
        for (int32 i = 0; i < evCount; ++i) {
            Event e{};
            if (evList.getEvent(i, e) != kResultTrue) continue;
            CapturedMidi c{};
            c.absoluteSample = blockStartSample + e.sampleOffset;
            if (e.type == Event::kNoteOnEvent) {
                c.isNoteOn = true;
                c.pitch = e.noteOn.pitch;
                c.velocity = std::clamp(
                    static_cast<int>(e.noteOn.velocity * 127.0f + 0.5f), 0, 127);
            } else if (e.type == Event::kNoteOffEvent) {
                c.isNoteOn = false;
                c.pitch = e.noteOff.pitch;
                c.velocity = 0;
            } else {
                continue;
            }
            dest.push_back(c);
        }
    };

    auto runBlock = [&](IParameterChanges* params,
                        int64_t blockStart,
                        std::vector<CapturedMidi>& dest) {
        inEvents.clear();
        outEvents.clear();
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processContext.projectTimeSamples = blockStart;
        data.inputParameterChanges = params;
        proc->process(data);
        drainEvents(outEvents, blockStart, dest);
    };

    int64_t blockStart = 0;

    // Block 0: deliver the full setup config (Source flips to Sequencer here;
    // the sequencer's firstStepPending_ guarantees the first note-on emits at
    // sample 0 of this block).
    runBlock(&setup_params, blockStart, capturedBeforeToggle);
    blockStart += kBlockSize;

    // Phase 1: drive 5 blocks (~58ms @ 44.1k/512). The echo is scheduled at
    // ~200ms (8820 samples ≈ 17 blocks), so the toggle in the next block
    // happens BEFORE the echo's scheduled time.
    for (int b = 0; b < 5; ++b) {
        runBlock(&noParamChange, blockStart, capturedBeforeToggle);
        blockStart += kBlockSize;
    }

    bool sawProgrammedNote = false;
    int echoBaseVelocity = 0;
    int64_t programmedNoteOnSample = -1;
    for (const auto& c : capturedBeforeToggle) {
        if (c.isNoteOn && c.pitch == 72) {
            sawProgrammedNote = true;
            echoBaseVelocity = c.velocity;
            programmedNoteOnSample = c.absoluteSample;
            break;
        }
    }
    INFO("Phase 1 events captured: " << capturedBeforeToggle.size());
    REQUIRE(sawProgrammedNote);
    REQUIRE(echoBaseVelocity > 0);

    // The echo will fire at programmedNoteOnSample + delaySamples (~200ms ≈
    // 8820 samples ≈ 18 blocks). We toggle BEFORE the echo's scheduled time
    // so we can prove the toggle didn't flush the pending queue.
    const int64_t expectedDelaySamples = static_cast<int64_t>(0.20 * kSampleRate);
    INFO("programmedNoteOnSample=" << programmedNoteOnSample
         << " expectedEchoSample=" << (programmedNoteOnSample + expectedDelaySamples)
         << " currentBlockStart=" << blockStart);
    REQUIRE(blockStart < programmedNoteOnSample + expectedDelaySamples);

    // Phase 2: toggle Source -> Live in a dedicated block. Then drain a
    // generous tail (60 blocks ~ 696ms) so any pending echoes fire.
    singleToggle.setChange(kArpSourceModeId, 0.0);
    runBlock(&singleToggle, blockStart, capturedAfterToggle);
    blockStart += kBlockSize;

    for (int b = 0; b < 60; ++b) {
        runBlock(&noParamChange, blockStart, capturedAfterToggle);
        blockStart += kBlockSize;
    }

    proc->setActive(false);
    proc->terminate();

    // ---- Assertion 1: pending echo NoteOn at pitch 72 fires after toggle. --
    bool sawEchoNoteOn = false;
    int64_t echoSample = -1;
    int echoVelocity = -1;
    for (const auto& c : capturedAfterToggle) {
        if (c.isNoteOn && c.pitch == 72) {
            sawEchoNoteOn = true;
            echoSample = c.absoluteSample;
            echoVelocity = c.velocity;
            break;
        }
    }
    INFO("programmedNoteOnSample=" << programmedNoteOnSample
         << "  echoSample=" << echoSample
         << "  echoVelocity=" << echoVelocity);
    REQUIRE(sawEchoNoteOn);

    // ---- Assertion 2: echo arrives at the originally scheduled time. ------
    const int64_t expectedEchoSample = programmedNoteOnSample + expectedDelaySamples;
    const int64_t tolerance = static_cast<int64_t>(2 * kBlockSize);  // ~2 blocks slop
    REQUIRE(echoSample >= expectedEchoSample - tolerance);
    REQUIRE(echoSample <= expectedEchoSample + tolerance);

    // ---- Assertion 3: echo velocity reflects the per-echo decay. ----------
    // Default velocityDecay = 0.5 (echo #1 retains 50% of the source velocity).
    REQUIRE(echoVelocity > 0);
    REQUIRE(echoVelocity < echoBaseVelocity);

    // ---- Assertion 4: no NEW noteOns from the new source until it emits. --
    // After the toggle, Source=Live. There are no held MIDI notes, so the
    // Live-mode arp must emit nothing new — every additional noteOn we see at
    // pitch 72 is an echo. Echos at OTHER pitches would still be at pitch 72
    // (default pitchShiftPerRepeat=0 means all echoes are at the source pitch).
    //
    // Simplest correctness check: the post-toggle captured stream must not
    // contain a noteOn at a pitch OTHER than 72.
    for (const auto& c : capturedAfterToggle) {
        if (c.isNoteOn) {
            INFO("unexpected post-toggle noteOn pitch=" << c.pitch);
            REQUIRE(c.pitch == 72);
        }
    }
}
