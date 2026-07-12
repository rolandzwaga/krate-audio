// arpeggiator_core_dawloop_test.cpp
// DAW loop regression + integration
// Split from the former 17k-line arpeggiator_core_test.cpp (D1). Shared helpers in
// arpeggiator_core_test_helpers.h.
#include "arpeggiator_core_test_helpers.h"



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
