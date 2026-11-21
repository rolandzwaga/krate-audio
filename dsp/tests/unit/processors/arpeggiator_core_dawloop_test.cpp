// arpeggiator_core_dawloop_test.cpp
// DAW loop regression + integration
// Split from the former 17k-line arpeggiator_core_test.cpp (D1). Shared helpers in
// arpeggiator_core_test_helpers.h.
#include "arpeggiator_core_test_helpers.h"

#include <cstdlib>
#include <string>



TEST_CASE("ArpeggiatorCore: ratchet on step 0 stays aligned after DAW loop",
          "[processors][arpeggiator_core][regression]") {
    // Setup: 120 BPM, 4/4, quarter notes, 44100 Hz
    // Step duration = 22050 samples, bar = 88200 samples
    constexpr double kSampleRate = 44100.0;
    constexpr double kTempo = 120.0;
    constexpr size_t kBlockSize = 512;
    constexpr int32_t kStepDuration = 22050;  // 60/120 * 44100
    constexpr int32_t kBarDuration = 4 * kStepDuration;  // 88200

    ArpeggiatorCore arp;
    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setTempoSync(true);
    arp.setNoteValue(NoteValue::Quarter, NoteModifier::None);

    // Ratchet lane: 4 steps. Step 0 = 2 (ratchet x2), steps 1-3 = 1 (normal)
    // Must set length FIRST since setStep clamps index to [0, length-1].
    arp.ratchetLane().setLength(4);
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(2));
    arp.ratchetLane().setStep(1, static_cast<uint8_t>(1));
    arp.ratchetLane().setStep(2, static_cast<uint8_t>(1));
    arp.ratchetLane().setStep(3, static_cast<uint8_t>(1));

    // Hold 4 notes
    arp.noteOn(60, 100);
    arp.noteOn(64, 100);
    arp.noteOn(67, 100);
    arp.noteOn(71, 100);

    SECTION("first step fires immediately at transport start") {
        BlockContext ctx;
        ctx.sampleRate = kSampleRate;
        ctx.blockSize = kBlockSize;
        ctx.tempoBPM = kTempo;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;

        auto events = collectEventsForSamples(arp, ctx, kBarDuration);
        auto noteOns = filterNoteOns(events);

        // The first NoteOn should fire at sample 0 (immediately at transport start),
        // not delayed by a full step duration.
        REQUIRE(noteOns.size() >= 1);
        INFO("First NoteOn at sample offset " << noteOns[0].sampleOffset
             << " (expected 0, not " << kStepDuration << ")");
        CHECK(noteOns[0].sampleOffset == 0);
    }

    SECTION("ratchet aligns across DAW loop (transport stop+restart)") {
        BlockContext ctx;
        ctx.sampleRate = kSampleRate;
        ctx.blockSize = kBlockSize;
        ctx.tempoBPM = kTempo;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;

        // --- Bar 1: play one full bar ---
        auto bar1Events = collectEventsForSamples(arp, ctx, kBarDuration);
        auto bar1NoteOns = filterNoteOns(bar1Events);
        auto bar1Ratchets = findRatchetStepOffsets(bar1Events, kStepDuration);

        INFO("Bar 1: " << bar1NoteOns.size() << " NoteOns, "
             << bar1Ratchets.size() << " ratcheted steps");
        REQUIRE(bar1Ratchets.size() >= 1);
        int32_t bar1RatchetOffset = bar1Ratchets[0];
        INFO("Bar 1 first ratchet at sample " << bar1RatchetOffset);

        // --- Simulate DAW loop: transport stops for 1 block ---
        ctx.isPlaying = false;
        {
            std::array<ArpEvent, 128> stopEvents;
            arp.processBlock(ctx, stopEvents);
        }

        // --- Bar 2: transport restarts at position 0 ---
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;

        auto bar2Events = collectEventsForSamples(arp, ctx, kBarDuration);
        auto bar2NoteOns = filterNoteOns(bar2Events);
        auto bar2Ratchets = findRatchetStepOffsets(bar2Events, kStepDuration);

        INFO("Bar 2: " << bar2NoteOns.size() << " NoteOns, "
             << bar2Ratchets.size() << " ratcheted steps");
        REQUIRE(bar2Ratchets.size() >= 1);
        int32_t bar2RatchetOffset = bar2Ratchets[0];
        INFO("Bar 2 first ratchet at sample " << bar2RatchetOffset);

        // The ratchet should appear at the same relative position in both bars.
        // With the bug, bar 1 has ratchet at ~0 (step 1) but bar 2 has it at
        // ~kStepDuration (step 2) due to the first-step delay.
        CHECK(bar1RatchetOffset == bar2RatchetOffset);
    }

    SECTION("ratchet aligns with retrigger=Beat across bar boundary") {
        arp.setRetrigger(ArpRetriggerMode::Beat);

        BlockContext ctx;
        ctx.sampleRate = kSampleRate;
        ctx.blockSize = kBlockSize;
        ctx.tempoBPM = kTempo;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;

        // Play 2 full bars without transport stop
        auto events = collectEventsForSamples(arp, ctx, 2 * kBarDuration);
        auto noteOns = filterNoteOns(events);

        // Split events into bar 1 and bar 2
        std::vector<ArpEvent> bar1Events, bar2Events;
        for (const auto& e : events) {
            if (e.sampleOffset < kBarDuration) {
                bar1Events.push_back(e);
            } else {
                ArpEvent shifted = e;
                shifted.sampleOffset -= kBarDuration;
                bar2Events.push_back(shifted);
            }
        }

        auto bar1Ratchets = findRatchetStepOffsets(bar1Events, kStepDuration);
        auto bar2Ratchets = findRatchetStepOffsets(bar2Events, kStepDuration);

        INFO("Bar 1 ratchets: " << bar1Ratchets.size()
             << ", Bar 2 ratchets: " << bar2Ratchets.size());
        REQUIRE(bar1Ratchets.size() >= 1);
        REQUIRE(bar2Ratchets.size() >= 1);

        INFO("Bar 1 ratchet at " << bar1Ratchets[0]
             << ", Bar 2 ratchet at " << bar2Ratchets[0]);
        // Both bars should have the ratchet at the same relative position
        CHECK(bar1Ratchets[0] == bar2Ratchets[0]);
    }
}


TEST_CASE("ArpeggiatorCore: DAW loop fires all steps every iteration",
          "[processors][arpeggiator_core][daw_loop]") {
    // 8-step pattern at 1/8 note, 120 BPM, 4/4 = exactly 1 bar per cycle.
    // At 120 BPM, 1/8 = 0.5 beats = 0.25s = 11025 samples (integer!).
    // 8 steps = 88200 samples = 1 bar at 4/4.
    constexpr double kSampleRate = 44100.0;
    constexpr double kTempo = 120.0;
    constexpr size_t kBlockSize = 512;
    constexpr size_t kStepDuration = 11025;
    constexpr size_t kBarSamples = 8 * kStepDuration;  // 88200 = 1 bar
    constexpr size_t kExpectedSteps = 8;

    ArpeggiatorCore arp;
    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setTempoSync(true);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);

    // Hold 4 notes (C E G B)
    arp.noteOn(60, 100);
    arp.noteOn(64, 100);
    arp.noteOn(67, 100);
    arp.noteOn(71, 100);

    // Set velocity lane to 8 steps so pattern = 8 steps = 1 bar
    arp.velocityLane().setLength(kExpectedSteps);
    arp.gateLane().setLength(kExpectedSteps);

    SECTION("sync mode: all 8 steps fire in each of 3 loop iterations") {
        BlockContext ctx;
        ctx.sampleRate = kSampleRate;
        ctx.blockSize = kBlockSize;
        ctx.tempoBPM = kTempo;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;
        ctx.projectTimeMusic = 0.0;
        ctx.projectTimeMusicValid = true;

        for (int loop = 0; loop < 3; ++loop) {
            INFO("Loop iteration " << loop);

            auto events = processBarWithTransport(arp, ctx, kBarSamples);
            auto noteOns = filterNoteOns(events);

            INFO("NoteOns in loop " << loop << ": " << noteOns.size());
            REQUIRE(noteOns.size() == kExpectedSteps);

            // First NoteOn should be at or very near sample 0
            CHECK(noteOns[0].sampleOffset <= 1);

            // Simulate DAW loop: PPQ jumps back to 0
            arp.notifyTransportLoop();
            ctx.transportPositionSamples = 0;
            ctx.projectTimeMusic = 0.0;
            // syncToMusicalPosition is called by the processor before processBlock
            arp.syncToMusicalPosition(0.0);
        }
    }

    SECTION("free-rate mode: notifyTransportLoop resets pattern") {
        arp.setTempoSync(false);
        // 44100 / 11025 = 4 Hz gives exactly 11025 samples/step = same as 1/8
        arp.setFreeRate(4.0f);

        BlockContext ctx;
        ctx.sampleRate = kSampleRate;
        ctx.blockSize = kBlockSize;
        ctx.tempoBPM = kTempo;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;
        ctx.projectTimeMusic = 0.0;
        ctx.projectTimeMusicValid = false;  // free rate: no musical position

        for (int loop = 0; loop < 3; ++loop) {
            INFO("Loop iteration " << loop);

            auto events = processBarWithTransport(arp, ctx, kBarSamples);
            auto noteOns = filterNoteOns(events);

            INFO("NoteOns in loop " << loop << ": " << noteOns.size());
            REQUIRE(noteOns.size() == kExpectedSteps);

            CHECK(noteOns[0].sampleOffset <= 1);

            // Simulate DAW loop restart
            arp.notifyTransportLoop();
            ctx.transportPositionSamples = 0;
            ctx.projectTimeMusic = 0.0;
        }
    }

    SECTION("loop restart emits NoteOffs before new pattern") {
        // Use 150% gate so the note is still sounding when we restart.
        // Gate duration = 1.5 * 11025 = 16537 samples.
        arp.setGateLength(150.0f);

        BlockContext ctx;
        ctx.sampleRate = kSampleRate;
        ctx.blockSize = kBlockSize;
        ctx.tempoBPM = kTempo;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;
        ctx.projectTimeMusic = 0.0;
        ctx.projectTimeMusicValid = true;

        // Process just 2 blocks (1024 samples) — step 0 fires immediately
        // at sample 0, its NoteOff is at 16537, so the note is still active.
        // This avoids any block-boundary edge cases.
        auto bar1Events = processBarWithTransport(arp, ctx, 2 * kBlockSize);
        auto bar1NoteOns = filterNoteOns(bar1Events);
        INFO("NoteOns after 2 blocks: " << bar1NoteOns.size());
        REQUIRE(bar1NoteOns.size() >= 1);

        // Collect event types for diagnostics
        size_t bar1NoteOffs = 0;
        for (const auto& e : bar1Events) {
            if (e.type == ArpEvent::Type::NoteOff) ++bar1NoteOffs;
        }
        INFO("NoteOffs after 2 blocks: " << bar1NoteOffs);
        INFO("Total events after 2 blocks: " << bar1Events.size());

        // Trigger loop restart while the note is still sounding
        arp.notifyTransportLoop();
        ctx.transportPositionSamples = 0;
        ctx.projectTimeMusic = 0.0;
        arp.syncToMusicalPosition(0.0);

        // Process the restart block — should emit NoteOffs then new NoteOn
        std::array<ArpEvent, 128> restartEvents;
        ctx.blockSize = kBlockSize;
        size_t count = arp.processBlock(ctx, restartEvents);

        // Diagnostics: dump all restart events
        INFO("Restart block event count: " << count);
        for (size_t i = 0; i < count; ++i) {
            const char* typeName = "Unknown";
            switch (restartEvents[i].type) {
                case ArpEvent::Type::NoteOn: typeName = "NoteOn"; break;
                case ArpEvent::Type::NoteOff: typeName = "NoteOff"; break;
                case ArpEvent::Type::kSkip: typeName = "Skip"; break;
            }
            INFO("  Event[" << i << "]: " << typeName
                 << " note=" << (int)restartEvents[i].note
                 << " offset=" << restartEvents[i].sampleOffset);
        }

        bool hasNoteOff = false;
        bool hasNoteOn = false;
        int32_t lastNoteOffOffset = -1;
        int32_t firstNoteOnOffset = -1;
        for (size_t i = 0; i < count; ++i) {
            if (restartEvents[i].type == ArpEvent::Type::NoteOff) {
                hasNoteOff = true;
                lastNoteOffOffset = restartEvents[i].sampleOffset;
            }
            if (restartEvents[i].type == ArpEvent::Type::NoteOn && firstNoteOnOffset < 0) {
                hasNoteOn = true;
                firstNoteOnOffset = restartEvents[i].sampleOffset;
            }
        }

        // NoteOffs must be emitted for cleanup
        CHECK(hasNoteOff);
        // New step 0 must fire in the same block
        CHECK(hasNoteOn);
        // NoteOff cleanup happens at offset 0, NoteOn at offset 0
        CHECK(lastNoteOffOffset == 0);
        CHECK(firstNoteOnOffset == 0);
    }

    SECTION("step timing is consistent across loop boundaries") {
        BlockContext ctx;
        ctx.sampleRate = kSampleRate;
        ctx.blockSize = kBlockSize;
        ctx.tempoBPM = kTempo;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;
        ctx.projectTimeMusic = 0.0;
        ctx.projectTimeMusicValid = true;

        // Collect step offsets for 3 consecutive loops
        std::vector<std::vector<int32_t>> loopStepOffsets;

        for (int loop = 0; loop < 3; ++loop) {
            auto events = processBarWithTransport(arp, ctx, kBarSamples);
            auto noteOns = filterNoteOns(events);

            std::vector<int32_t> offsets;
            for (const auto& e : noteOns) {
                offsets.push_back(e.sampleOffset);
            }
            loopStepOffsets.push_back(offsets);

            // DAW loop
            arp.notifyTransportLoop();
            ctx.transportPositionSamples = 0;
            ctx.projectTimeMusic = 0.0;
            arp.syncToMusicalPosition(0.0);
        }

        // All loops should have the same number of steps
        REQUIRE(loopStepOffsets[0].size() == kExpectedSteps);
        REQUIRE(loopStepOffsets[1].size() == kExpectedSteps);
        REQUIRE(loopStepOffsets[2].size() == kExpectedSteps);

        // Step offsets should match across loops (within 2 samples for rounding)
        for (size_t step = 0; step < kExpectedSteps; ++step) {
            INFO("Step " << step << ": loop0=" << loopStepOffsets[0][step]
                 << " loop1=" << loopStepOffsets[1][step]
                 << " loop2=" << loopStepOffsets[2][step]);
            CHECK(std::abs(loopStepOffsets[0][step] - loopStepOffsets[1][step]) <= 2);
            CHECK(std::abs(loopStepOffsets[0][step] - loopStepOffsets[2][step]) <= 2);
        }
    }
}

// syncToMusicalPosition sets the step counter from the host's PPQ at each block start. A 1/8 step at 120 BPM and
// 44.1 kHz is exactly 11025 samples, so the counter is an exact integer; truncating stepFraction * duration made it
// one short whenever the product landed an ulp below that integer (at 208 of the 703 64-sample block starts in this
// render, at 5 of the 21 2048-sample ones), and a step fired one sample late depending on the buffer size. The
// macOS -ffast-math leg moved which block starts miss and failed Ruinae's 2048-vs-64 arp render (0.76 at sample
// 44951). Every step must fire on its grid sample whatever the buffer size.
TEST_CASE("ArpeggiatorCore: PPQ resync puts every step on its grid sample at any block size",
          "[processors][arpeggiator_core][transport][regression]") {
    constexpr double kSampleRate = 44100.0;
    constexpr double kTempo = 120.0;
    constexpr int32_t kStep = 11025;  // 1/8 at 120 BPM
    constexpr size_t kTotal = 44 * 2048;  // ~2 s: steps 0..8

    auto stepOnsets = [&](size_t blockSize) {
        ArpeggiatorCore arp;
        arp.prepare(kSampleRate, blockSize);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setTempoSync(true);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.noteOn(60, 100);
        arp.noteOn(64, 100);
        std::vector<int32_t> onsets;
        std::array<ArpEvent, 128> events{};
        for (size_t pos = 0; pos < kTotal; pos += blockSize) {
            BlockContext ctx;
            ctx.sampleRate = kSampleRate;
            ctx.blockSize = std::min(blockSize, kTotal - pos);
            ctx.tempoBPM = kTempo;
            ctx.isPlaying = true;
            ctx.transportPositionSamples = static_cast<int64_t>(pos);
            ctx.projectTimeMusic = static_cast<double>(pos) / kSampleRate * (kTempo / 60.0);
            ctx.projectTimeMusicValid = true;
            arp.syncToMusicalPosition(ctx.projectTimeMusic);  // as the plugins call it, every block
            const size_t count = arp.processBlock(ctx, events);
            for (size_t i = 0; i < count; ++i) {
                if (events[i].type == ArpEvent::Type::NoteOn) {
                    onsets.push_back(events[i].sampleOffset + static_cast<int32_t>(pos));
                }
            }
        }
        return onsets;
    };

    // A step moves only when the resync just before it misses, so the sizes spread the resync points over the step.
    for (const size_t blockSize : {size_t{64}, size_t{2048}, size_t{512}, size_t{61}, size_t{97}, size_t{100},
                                   size_t{127}, size_t{333}, size_t{441}, size_t{1000}, size_t{1023}}) {
        const auto onsets = stepOnsets(blockSize);
        REQUIRE(onsets.size() >= 8);  // not vacuous: steps 0..8 in ~2 s
        for (size_t k = 0; k < onsets.size(); ++k) {
            INFO("block " << blockSize << ", step " << k << " at sample " << onsets[k]);
            CHECK(onsets[k] == static_cast<int32_t>(k) * kStep);
        }
    }
}

// R-066: the per-block PPQ resync re-anchored the counter on a straight one-step grid and scaled the fraction onto the
// SWUNG duration, so a swung arp in a playing DAW lost its long/short alternation and played flams 290-443 samples
// apart (1/16, 120 BPM, swing 50 %, 512-sample blocks: intervals 5060, 6101, 5044, 409, ...). With swing the grid
// period is a pair of steps; the onsets must alternate long/short (long first, as free-run plays them), keep every
// pair on the host's 1/8 grid, and not depend on the buffer size.
TEST_CASE("syncToMusicalPosition keeps swing timing at every block size",
          "[processors][arp][swing]") {
    constexpr double kSampleRate = 44100.0;
    constexpr double kTempo = 120.0;
    constexpr size_t kTotal = 4 * 4 * 22050;  // 4 bars of 4/4 at 120 BPM

    struct SwingRate {
        const char* name;
        NoteValue value;
        NoteModifier modifier;
        double beats;  // quarter notes per step
        double swing;
        std::vector<size_t> blockSizes;  // the first one is the reference
    };
    // The plan's case (1/16, swing 50 %, 64/512/4096), then Tape_Shuffle's rate (1/8T, swing 22 %) at uneven sizes so
    // block starts fall on and beside the swung onsets.
    const std::vector<SwingRate> rates = {
        {"1/16 swing 50%", NoteValue::Sixteenth, NoteModifier::None, 0.25, 0.5, {64, 512, 4096}},
        {"1/8T swing 22%", NoteValue::Eighth, NoteModifier::Triplet, 1.0 / 3.0, 0.22, {64, 61, 97, 333, 1000, 2048}},
    };

    for (const auto& rate : rates) {
        const double base = 60.0 / kTempo * rate.beats * kSampleRate;  // 5512.5 / 7350 samples
        const double longStep = base * (1.0 + rate.swing);
        const double shortStep = base * (1.0 - rate.swing);

        auto stepOnsets = [&](size_t blockSize, bool sync = true) {
            ArpeggiatorCore arp;
            arp.prepare(kSampleRate, blockSize);
            arp.setEnabled(true);
            arp.setMode(ArpMode::Up);
            arp.setTempoSync(true);
            arp.setNoteValue(rate.value, rate.modifier);
            arp.setSwing(static_cast<float>(rate.swing * 100.0));
            arp.noteOn(60, 100);
            arp.noteOn(64, 100);
            arp.noteOn(67, 100);
            std::vector<int64_t> onsets;
            std::array<ArpEvent, 128> events{};
            for (size_t pos = 0; pos < kTotal; pos += blockSize) {
                BlockContext ctx;
                ctx.sampleRate = kSampleRate;
                ctx.blockSize = std::min(blockSize, kTotal - pos);
                ctx.tempoBPM = kTempo;
                ctx.isPlaying = true;
                ctx.transportPositionSamples = static_cast<int64_t>(pos);
                ctx.projectTimeMusic = static_cast<double>(pos) / kSampleRate * (kTempo / 60.0);
                ctx.projectTimeMusicValid = true;
                if (sync) arp.syncToMusicalPosition(ctx.projectTimeMusic);  // as Ruinae and Gradus call it, every block
                const size_t count = arp.processBlock(ctx, events);
                for (size_t i = 0; i < count; ++i) {
                    if (events[i].type == ArpEvent::Type::NoteOn) {
                        onsets.push_back(events[i].sampleOffset + static_cast<int64_t>(pos));
                    }
                }
            }
            return onsets;
        };

        // Intervals alternate long/short, LONG first (R-199 ruling B: step k lasts dur(k), and step 0 of a pair is
        // the even, lengthened one).
        auto countBadIntervals = [&](const std::vector<int64_t>& onsets, std::string& firstBad) {
            size_t badIntervals = 0;
            for (size_t k = 1; k < onsets.size(); ++k) {
                const double interval = static_cast<double>(onsets[k] - onsets[k - 1]);
                const double expected = ((k - 1) % 2 == 0) ? longStep : shortStep;
                if (std::abs(interval - expected) > 2.0) {
                    if (badIntervals++ == 0) {
                        firstBad = "interval " + std::to_string(k - 1) + " (" + std::to_string(onsets[k - 1])
                                 + " -> " + std::to_string(onsets[k]) + ") = " + std::to_string(interval)
                                 + ", expected " + std::to_string(expected);
                    }
                }
            }
            return badIntervals;
        };

        const auto minOnsets = static_cast<size_t>(static_cast<double>(kTotal) / base) - 2;

        // Free-run (no PPQ sync) plays the same long-first order as the DAW sync.
        {
            const auto freeRun = stepOnsets(512, false);
            std::string firstBad;
            INFO(rate.name << ", free-run, " << freeRun.size() << " onsets");
            REQUIRE(freeRun.size() >= minOnsets);
            const size_t bad = countBadIntervals(freeRun, firstBad);
            INFO("first bad: " << firstBad);
            REQUIRE(bad == 0);
        }

        const auto reference = stepOnsets(rate.blockSizes.front());
        for (const size_t blockSize : rate.blockSizes) {
            const auto onsets = stepOnsets(blockSize);
            INFO(rate.name << ", block size " << blockSize << ", " << onsets.size() << " onsets");
            REQUIRE(onsets.size() >= minOnsets);  // not vacuous: every step of 4 bars

            std::string firstBad;
            const size_t badIntervals = countBadIntervals(onsets, firstBad);
            INFO("first bad: " << firstBad);
            REQUIRE(badIntervals == 0);

            // Every pair starts on the host's two-step grid.
            size_t offGrid = 0;
            for (size_t k = 0; k < onsets.size(); k += 2) {
                const double grid = static_cast<double>(k / 2) * 2.0 * base;
                if (std::abs(static_cast<double>(onsets[k]) - grid) > 2.0) ++offGrid;
            }
            REQUIRE(offGrid == 0);

            // The onset list does not depend on the buffer size.
            REQUIRE(onsets.size() == reference.size());
            size_t moved = 0;
            for (size_t k = 0; k < onsets.size(); ++k) {
                if (std::abs(onsets[k] - reference[k]) > 2) ++moved;
            }
            REQUIRE(moved == 0);
        }
    }
}
