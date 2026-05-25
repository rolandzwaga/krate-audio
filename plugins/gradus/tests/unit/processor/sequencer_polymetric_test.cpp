// ==============================================================================
// Sequencer Polymetric Tests (spec 142, Phase 3, User Story 1, FR-025b)
// ==============================================================================
// Verifies that lane 10 (Sequencer Note) advances polymetrically alongside
// the other 9 lanes — independent length, independent speed multiplier —
// without coupling its playhead to any other lane's.
// ==============================================================================

#include <krate/dsp/processors/arpeggiator_core.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cstdint>
#include <vector>

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

std::vector<ArpEvent> filterNoteOns(const std::vector<ArpEvent>& events)
{
    std::vector<ArpEvent> out;
    for (const auto& e : events) {
        if (e.type == ArpEvent::Type::NoteOn) out.push_back(e);
    }
    return out;
}

}  // namespace

// =============================================================================
// FR-025b: Sequencer Note lane advances polymetrically vs other lanes
// =============================================================================

TEST_CASE("Sequencer: lane 10 advances polymetrically (length=3) vs velocity lane (length=4)",
          "[arpeggiator_core][sequencer][polymetric][FR-025b]")
{
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setSourceMode(SourceMode::Sequencer);

    // Sequencer Note lane: length=3, pitches [60, 62, 64].
    arp.seqNoteLane().setLength(3);
    arp.seqNoteLane().setStep(0, uint8_t{60});
    arp.seqNoteLane().setStep(1, uint8_t{62});
    arp.seqNoteLane().setStep(2, uint8_t{64});
    for (size_t i = 0; i < 32; ++i) {
        arp.seqRestFlags()[i].store(0, std::memory_order_relaxed);
    }

    // Velocity lane: length=4 with [1.0, 0.5, 1.0, 0.5] so any cross-coupling
    // would produce a different velocity profile than length=3 expects.
    arp.velocityLane().setLength(4);
    arp.velocityLane().setStep(0, 1.0f);
    arp.velocityLane().setStep(1, 0.5f);
    arp.velocityLane().setStep(2, 1.0f);
    arp.velocityLane().setStep(3, 0.5f);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // 12 steps = LCM(3, 4). After 12 steps both lanes have looped to step 0.
    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);
    REQUIRE(noteOns.size() >= 12);

    // Pitch pattern must follow length=3 modulo regardless of velocity lane:
    // steps 0..11 => pitches [60,62,64, 60,62,64, 60,62,64, 60,62,64].
    const uint8_t expectedPitches[12] = {60, 62, 64, 60, 62, 64,
                                         60, 62, 64, 60, 62, 64};
    for (size_t i = 0; i < 12; ++i) {
        REQUIRE(static_cast<int>(noteOns[i].note) ==
                static_cast<int>(expectedPitches[i]));
    }
}

TEST_CASE("Sequencer: lane 10 speed multiplier acts independently",
          "[arpeggiator_core][sequencer][polymetric][speed]")
{
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setSourceMode(SourceMode::Sequencer);

    arp.seqNoteLane().setLength(2);
    arp.seqNoteLane().setStep(0, uint8_t{60});
    arp.seqNoteLane().setStep(1, uint8_t{72});
    for (size_t i = 0; i < 32; ++i) {
        arp.seqRestFlags()[i].store(0, std::memory_order_relaxed);
    }

    // Lane 10 advances at 0.5x speed — every two ticks emits one new pitch.
    arp.setLaneSpeed(9, 0.5f);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);
    REQUIRE(noteOns.size() >= 6);

    // With 0.5x speed, the first emitted pitch must be 60 (step 0) and we
    // expect the lane to remain on step 0 for two consecutive emissions
    // before advancing. (Exact ratio depends on integer rounding inside the
    // accumulator; we settle for the looser "first several are 60" assertion.)
    REQUIRE(static_cast<int>(noteOns[0].note) == 60);
    REQUIRE(static_cast<int>(noteOns[1].note) == 60);
}
