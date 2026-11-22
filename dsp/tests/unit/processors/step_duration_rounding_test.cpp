// ==============================================================================
// Tempo-derived step lengths round to the nearest sample (Ruinae audit R-106, R-015)
// ==============================================================================
// TranceGate::updateStepDuration and ArpeggiatorCore::stepDurationFor converted a tempo product to samples with a
// truncating cast. Those products are often an exact integer that the double lands an ulp below (50 BPM, dotted half,
// 44.1 kHz evaluates to 79379.99999999999), so the step was a sample short, and which products miss depends on the
// floating-point mode. Each swing half was truncated on its own too, so a 36 % pair at 1/8, 120 BPM summed to 22049,
// not 2 * 11025: the free-running arp drifted a sample per pair against the tempo.
//
// This TU is built twice: in dsp_processors_tests (the executable's fast-math flags, the macOS leg's mode) and in
// dsp_ieee_tests (IEEE). Both must read the rounded values.
// ==============================================================================

#include <krate/dsp/processors/arpeggiator_core.h>
#include <krate/dsp/processors/trance_gate.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

using namespace Krate::DSP;

TEST_CASE("Step durations round to the nearest sample", "[processors][arp][trance-gate][timing]") {
    SECTION("TranceGate: 50 BPM dotted values at 44.1 kHz are exact") {
        // 60 / 50 * 1.5 beats * 44100 = 79380 and 60 / 50 * 3 beats * 44100 = 158760 exactly; both double products
        // land an ulp below (79379.99999999999, 158759.99999999997).
        auto stepAt50Bpm = [](NoteValue value) {
            TranceGate gate;
            gate.prepare(44100.0);
            TranceGateParams params;
            params.tempoSync = true;
            params.noteValue = value;
            params.noteModifier = NoteModifier::Dotted;
            gate.setParams(params);
            gate.setTempo(50.0);
            return gate.getSamplesPerStep();
        };
        CHECK(stepAt50Bpm(NoteValue::Quarter) == 79380);
        CHECK(stepAt50Bpm(NoteValue::Half) == 158760);
    }

    SECTION("ArpeggiatorCore: a 36 % swing pair at 1/8, 120 BPM spans exactly 2 * 11025 samples") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setSwing(36.0f);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);
        arp.noteOn(52, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;

        std::vector<int64_t> onsets;
        std::array<ArpEvent, 128> events{};
        for (size_t b = 0; b < 400; ++b) {
            const size_t count = arp.processBlock(ctx, events);
            for (size_t i = 0; i < count; ++i) {
                if (events[i].type == ArpEvent::Type::NoteOn)
                    onsets.push_back(static_cast<int64_t>(b * ctx.blockSize) + events[i].sampleOffset);
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }
        REQUIRE(onsets.size() >= 16);

        // Step k lasts dur(k) and the even step is the long one (R-199), so gap 2j is the long half and gap 2j + 1
        // the short one: long = round(11025 * 1.36) = 14994, short = 2 * 11025 - long = 7056.
        for (size_t i = 0; i + 2 < onsets.size(); i += 2) {
            const int64_t longGap = onsets[i + 1] - onsets[i];
            const int64_t shortGap = onsets[i + 2] - onsets[i + 1];
            INFO("pair at step " << i << ": long " << longGap << ", short " << shortGap);
            CHECK(longGap == 14994);
            CHECK(longGap + shortGap == 2 * 11025);
        }
    }
}
