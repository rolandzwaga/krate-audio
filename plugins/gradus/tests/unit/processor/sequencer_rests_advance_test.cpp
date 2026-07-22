// ==============================================================================
// Sequencer Rests + Playhead Advancement Tests (spec 142, Phase 3, FR-019, SC-008)
// ==============================================================================
// Verifies the rest-flag semantics:
//   * All-rest pattern emits zero note-ons but the playhead still advances
//     (SC-008 — Gradus must still tick through silence so playing resumes at
//      the correct position when the user fills a rest later).
//   * Mixed rest/play patterns emit only at the play steps.
// ==============================================================================

#include <krate/dsp/processors/arpeggiator_core.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>
#include <set>

using namespace Krate::DSP;

namespace {

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

size_t countNoteOns(const std::vector<ArpEvent>& events)
{
    size_t n = 0;
    for (const auto& e : events) {
        if (e.type == ArpEvent::Type::NoteOn) ++n;
    }
    return n;
}

}  // namespace

// =============================================================================
// SC-008: All-rest pattern emits zero note-ons, playhead still advances
// =============================================================================

TEST_CASE("Sequencer: all-rest pattern emits zero noteOns while playhead advances",
          "[arpeggiator_core][sequencer][rest][SC-008]")
{
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setSourceMode(SourceMode::Sequencer);

    // 32 rests, default pitches (60). length=32.
    arp.seqNoteLane().setLength(32);
    for (size_t i = 0; i < 32; ++i) {
        arp.seqNoteLane().setStep(i, uint8_t{60});
        arp.seqRestFlags()[i].store(1, std::memory_order_relaxed);
    }

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Sample the playhead as we go: an all-rest pattern must keep CLOCKING, so
    // "emitted nothing" alone is not the property under test. Run the ~4.6 s in
    // chunks and record every distinct step the lane visits.
    //
    // Previously the playhead half of this case was commented-out prose and the
    // only live assertion was the note-on count -- which a completely frozen
    // playhead passes, so the test could not fail for its stated purpose.
    std::vector<ArpEvent> events;
    std::set<size_t> stepsVisited;
    stepsVisited.insert(arp.seqNoteLane().currentStep());
    for (int chunk = 0; chunk < 400; ++chunk) {
        auto chunkEvents = collectEvents(arp, ctx, 1);
        events.insert(events.end(), chunkEvents.begin(), chunkEvents.end());
        stepsVisited.insert(arp.seqNoteLane().currentStep());
    }

    REQUIRE(countNoteOns(events) == 0);

    // At 120 BPM / 1/16 notes, 400 blocks of 512 samples is ~28 sequencer
    // ticks, so the lane must have visited many distinct steps rather than
    // sitting on one.
    INFO("distinct seqNote steps visited: " << stepsVisited.size());
    REQUIRE(stepsVisited.size() > 1);
    REQUIRE(arp.seqNoteLane().currentStep() != 0);
}

// =============================================================================
// FR-019: mixed rest/play pattern timing
// =============================================================================

TEST_CASE("Sequencer: mixed rest/play pattern emits at play steps only",
          "[arpeggiator_core][sequencer][rest][FR-019]")
{
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setSourceMode(SourceMode::Sequencer);

    // Alternating rest/play, length=8, pitches 60..67.
    arp.seqNoteLane().setLength(8);
    for (uint8_t i = 0; i < 8; ++i) {
        arp.seqNoteLane().setStep(i, static_cast<uint8_t>(60 + i));
        // Rest on even indices, play on odd.
        arp.seqRestFlags()[i].store((i % 2 == 0) ? 1 : 0,
                                    std::memory_order_relaxed);
    }

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);

    // Only the odd-index steps emit: 61, 63, 65, 67, then 61, 63, ...
    std::vector<int> emittedPitches;
    for (const auto& e : events) {
        if (e.type == ArpEvent::Type::NoteOn) {
            emittedPitches.push_back(static_cast<int>(e.note));
        }
    }
    REQUIRE(emittedPitches.size() >= 4);

    // First 4 emissions must be the odd-index pitches in order.
    REQUIRE(emittedPitches[0] == 61);
    REQUIRE(emittedPitches[1] == 63);
    REQUIRE(emittedPitches[2] == 65);
    REQUIRE(emittedPitches[3] == 67);

    // Even-index pitches (60, 62, 64, 66) must NEVER be emitted (rest).
    for (int p : emittedPitches) {
        REQUIRE(p != 60);
        REQUIRE(p != 62);
        REQUIRE(p != 64);
        REQUIRE(p != 66);
    }
}
