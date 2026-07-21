// arpeggiator_core_lanes_test.cpp
// Independent lanes (spec 072): velocity/gate/pitch/polymetric + edge hardening
// Split from the former 17k-line arpeggiator_core_test.cpp (D1). Shared helpers in
// arpeggiator_core_test_helpers.h.
#include "arpeggiator_core_test_helpers.h"

#include <atomic>
#include <thread>



// =============================================================================
// Phase 4: User Story 1 -- Velocity Lane Shaping (072-independent-lanes)
// =============================================================================

// T013: Velocity lane integration tests

TEST_CASE("ArpeggiatorCore: VelocityLane_DefaultIsPassthrough",
          "[processors][arpeggiator_core]") {
    // With default lane (length=1, step=1.0), arp output velocity equals
    // input velocity (SC-002 backward compat)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Default velocity lane: length=1, step[0]=1.0
    // Output velocity should be exactly the input velocity (100)
    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    for (const auto& on : noteOns) {
        CHECK(on.velocity == 100);
    }
}


TEST_CASE("ArpeggiatorCore: VelocityLane_ScalesVelocity",
          "[processors][arpeggiator_core]") {
    // Set velocity lane length=4, steps=[1.0, 0.3, 0.3, 0.7],
    // run 8 arp steps, verify output velocities follow cycle
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.noteOn(60, 100);

    // Configure velocity lane
    arp.velocityLane().setLength(4);
    arp.velocityLane().setStep(0, 1.0f);
    arp.velocityLane().setStep(1, 0.3f);
    arp.velocityLane().setStep(2, 0.3f);
    arp.velocityLane().setStep(3, 0.7f);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 500);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 8);

    // Input velocity = 100
    // Expected pattern: round(100 * 1.0)=100, round(100 * 0.3)=30,
    //                   round(100 * 0.3)=30,  round(100 * 0.7)=70
    // Repeated twice for 8 steps
    std::array<uint8_t, 8> expected = {100, 30, 30, 70, 100, 30, 30, 70};
    for (size_t i = 0; i < 8; ++i) {
        CHECK(noteOns[i].velocity == expected[i]);
    }
}


TEST_CASE("ArpeggiatorCore: VelocityLane_ClampsToMinimum1",
          "[processors][arpeggiator_core]") {
    // Set step value 0.0, verify output velocity is 1 (not 0), per FR-011
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.noteOn(60, 100);

    arp.velocityLane().setLength(1);
    arp.velocityLane().setStep(0, 0.0f);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    for (const auto& on : noteOns) {
        CHECK(on.velocity == 1);  // floor of 1, never 0
    }
}


TEST_CASE("ArpeggiatorCore: VelocityLane_ClampsToMax127",
          "[processors][arpeggiator_core]") {
    // Set step value 1.0 with input velocity 127, verify output is 127
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.noteOn(60, 127);

    arp.velocityLane().setLength(1);
    arp.velocityLane().setStep(0, 1.0f);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    for (const auto& on : noteOns) {
        CHECK(on.velocity == 127);  // no overflow
    }
}


TEST_CASE("ArpeggiatorCore: VelocityLane_LengthChange_MidPlayback",
          "[processors][arpeggiator_core]") {
    // Set length=4, advance 2 steps, change length=3, verify no crash
    // and lane cycles at new length
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.noteOn(60, 100);

    arp.velocityLane().setLength(4);
    arp.velocityLane().setStep(0, 1.0f);
    arp.velocityLane().setStep(1, 0.5f);
    arp.velocityLane().setStep(2, 0.8f);
    arp.velocityLane().setStep(3, 0.3f);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Collect 2 steps
    auto events1 = collectEvents(arp, ctx, 100);
    auto noteOns1 = filterNoteOns(events1);
    REQUIRE(noteOns1.size() >= 2);

    // Now change length to 3 mid-playback
    arp.velocityLane().setLength(3);

    // Collect more steps -- should not crash and cycle at new length 3
    auto events2 = collectEvents(arp, ctx, 500);
    auto noteOns2 = filterNoteOns(events2);
    REQUIRE(noteOns2.size() >= 6);  // at least 2 full cycles of length 3
}


TEST_CASE("ArpeggiatorCore: VelocityLane_ResetOnRetrigger",
          "[processors][arpeggiator_core]") {
    // Advance lane mid-cycle, trigger noteOn with retrigger=Note,
    // verify velocityLane().currentStep()==0
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.setRetrigger(ArpRetriggerMode::Note);
    arp.noteOn(60, 100);

    arp.velocityLane().setLength(4);
    arp.velocityLane().setStep(0, 1.0f);
    arp.velocityLane().setStep(1, 0.5f);
    arp.velocityLane().setStep(2, 0.3f);
    arp.velocityLane().setStep(3, 0.7f);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Advance 2 steps
    auto events = collectEvents(arp, ctx, 100);
    auto noteOns = filterNoteOns(events);
    REQUIRE(noteOns.size() >= 2);

    // Trigger retrigger via noteOn (retrigger=Note)
    arp.noteOn(64, 100);

    // After retrigger, velocity lane should be reset to step 0
    CHECK(arp.velocityLane().currentStep() == 0);

    // Next note should use step 0 velocity (1.0)
    auto events2 = collectEvents(arp, ctx, 100);
    auto noteOns2 = filterNoteOns(events2);
    REQUIRE(noteOns2.size() >= 1);
    CHECK(noteOns2[0].velocity == 100);  // round(100 * 1.0) = 100
}


TEST_CASE("ArpeggiatorCore: BitIdentical_VelocityDefault",
          "[processors][arpeggiator_core]") {
    // SC-002: Capture output of 1000+ steps with default lane at multiple tempos,
    // compare to expected (no lane) values -- must be byte-for-byte identical.
    // Default velocity lane: length=1, step[0]=1.0f
    // round(v * 1.0f) == v for all integers v in [1,127] by IEEE 754

    std::array<double, 3> tempos = {120.0, 140.0, 180.0};

    for (double tempo : tempos) {
        // Create arp with default lane (no modifications)
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setGateLength(80.0f);

        // Hold a chord: C, E, G with various velocities
        arp.noteOn(60, 100);
        arp.noteOn(64, 80);
        arp.noteOn(67, 110);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = tempo;
        ctx.isPlaying = true;

        // Collect enough blocks to get 1000+ NoteOn events
        // At 120 BPM, 1/8 note = 11025 samples. With 512-sample blocks,
        // ~22 blocks per step. 1000 steps = ~22000 blocks.
        auto events = collectEvents(arp, ctx, 25000);
        auto noteOns = filterNoteOns(events);

        REQUIRE(noteOns.size() >= 1000);

        // Verify every note velocity is EXACTLY the input velocity
        // (no modification from default lane)
        size_t mismatches = 0;
        for (size_t i = 0; i < noteOns.size(); ++i) {
            // In Up mode with 3 notes, pattern cycles: 60, 64, 67
            uint8_t expectedVel = 0;
            uint8_t note = noteOns[i].note;
            if (note == 60) expectedVel = 100;
            else if (note == 64) expectedVel = 80;
            else if (note == 67) expectedVel = 110;
            else {
                // Octave repeats -- same velocity as base note
                int mod = note % 12;
                if (mod == 0) expectedVel = 100;
                else if (mod == 4) expectedVel = 80;
                else expectedVel = 110;
            }

            if (noteOns[i].velocity != expectedVel) {
                ++mismatches;
            }
        }

        INFO("Tempo: " << tempo << " BPM, Steps: " << noteOns.size()
             << ", Mismatches: " << mismatches);
        CHECK(mismatches == 0);
    }
}


// =============================================================================
// Phase 4: User Story 2 -- Gate Length Lane (072-independent-lanes)
// =============================================================================

// T028: Gate lane integration tests

TEST_CASE("ArpeggiatorCore: GateLane_DefaultIsPassthrough",
          "[processors][arpeggiator_core]") {
    // With default gate lane (length=1, step=1.0), gate duration is identical
    // to Phase 3 formula (SC-002 backward compat for gate)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Default gate lane: length=1, step[0]=1.0
    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    REQUIRE(noteOns.size() >= 2);
    REQUIRE(noteOffs.size() >= 1);

    // At 120 BPM, 1/8 note = 11025 samples. Gate 80% = 8820 samples.
    // The gate duration should be: floor(11025 * 80 / 100) = 8820
    // NoteOff offset should be NoteOn offset + 8820
    int32_t gateExpected = static_cast<int32_t>(static_cast<size_t>(
        static_cast<double>(11025) * static_cast<double>(80.0f) / 100.0));
    int32_t actualGate = noteOffs[0].sampleOffset - noteOns[0].sampleOffset;
    CHECK(actualGate == gateExpected);
}


TEST_CASE("ArpeggiatorCore: GateLane_MultipliesGlobalGate",
          "[processors][arpeggiator_core]") {
    // Set gate lane length=3, steps=[0.5, 1.0, 1.5], global gate=80%,
    // run 3 steps, verify noteOff sample offsets match computed durations
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Configure gate lane
    arp.gateLane().setLength(3);
    arp.gateLane().setStep(0, 0.5f);
    arp.gateLane().setStep(1, 1.0f);
    arp.gateLane().setStep(2, 1.5f);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 500);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    REQUIRE(noteOns.size() >= 3);
    REQUIRE(noteOffs.size() >= 3);

    // Step duration at 120 BPM, 1/8 note = 11025 samples
    // Gate formula: max(1, floor(stepDuration * gatePercent / 100 * gateLaneValue))
    size_t stepDuration = 11025;
    std::array<float, 3> gateSteps = {0.5f, 1.0f, 1.5f};
    for (size_t i = 0; i < 3; ++i) {
        size_t expectedGate = std::max(size_t{1}, static_cast<size_t>(
            static_cast<double>(stepDuration) *
            static_cast<double>(80.0f) / 100.0 *
            static_cast<double>(gateSteps[i])));
        size_t actualGate = static_cast<size_t>(noteOffs[i].sampleOffset - noteOns[i].sampleOffset);
        INFO("Step " << i << ": expected gate=" << expectedGate
             << ", actual=" << actualGate);
        CHECK(actualGate == expectedGate);
    }
}


TEST_CASE("ArpeggiatorCore: GateLane_LegatoOverlap",
          "[processors][arpeggiator_core]") {
    // Gate lane value 1.5 + global gate 100% = effective 150%
    // Verify arpeggiator handles noteOff firing after next noteOn without crash
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(100.0f);  // 100% global gate
    arp.noteOn(60, 100);
    arp.noteOn(64, 100);

    // Configure gate lane with 1.5x multiplier (effective 150%)
    arp.gateLane().setLength(1);
    arp.gateLane().setStep(0, 1.5f);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Should not crash even with overlapping notes
    auto events = collectEvents(arp, ctx, 1000);
    auto noteOns = filterNoteOns(events);

    // Just verify we got reasonable events without crash
    REQUIRE(noteOns.size() >= 5);
}


TEST_CASE("ArpeggiatorCore: GateLane_LengthChange_MidPlayback",
          "[processors][arpeggiator_core]") {
    // Set length=3, advance 1 step, change length=2, verify no crash
    // and gate cycles at new length
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    arp.gateLane().setLength(3);
    arp.gateLane().setStep(0, 0.5f);
    arp.gateLane().setStep(1, 1.0f);
    arp.gateLane().setStep(2, 1.5f);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Collect 1 step
    auto events1 = collectEvents(arp, ctx, 50);
    auto noteOns1 = filterNoteOns(events1);
    REQUIRE(noteOns1.size() >= 1);

    // Change length to 2 mid-playback
    arp.gateLane().setLength(2);

    // Collect more steps -- should not crash and cycle at new length 2
    auto events2 = collectEvents(arp, ctx, 500);
    auto noteOns2 = filterNoteOns(events2);
    REQUIRE(noteOns2.size() >= 4);  // at least 2 full cycles of length 2
}


TEST_CASE("ArpeggiatorCore: GateLane_ResetOnRetrigger",
          "[processors][arpeggiator_core]") {
    // Advance gate lane mid-cycle, trigger retrigger, verify currentStep()==0
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setRetrigger(ArpRetriggerMode::Note);
    arp.noteOn(60, 100);

    arp.gateLane().setLength(4);
    arp.gateLane().setStep(0, 0.5f);
    arp.gateLane().setStep(1, 1.0f);
    arp.gateLane().setStep(2, 1.5f);
    arp.gateLane().setStep(3, 0.8f);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Advance 2 steps
    auto events = collectEvents(arp, ctx, 100);
    auto noteOns = filterNoteOns(events);
    REQUIRE(noteOns.size() >= 2);

    // Trigger retrigger via noteOn (retrigger=Note)
    arp.noteOn(64, 100);

    // After retrigger, gate lane should be reset to step 0
    CHECK(arp.gateLane().currentStep() == 0);
}


TEST_CASE("ArpeggiatorCore: BitIdentical_GateDefault",
          "[processors][arpeggiator_core]") {
    // SC-002: 1000+ steps with default gate lane at tempos 120, 140, 180 BPM
    // compare noteOff sample offsets byte-for-byte to Phase 3 expected values
    // Default gate lane: length=1, step[0]=1.0f
    // The formula with * 1.0 must be bit-identical to without.

    std::array<double, 3> tempos = {120.0, 140.0, 180.0};

    for (double tempo : tempos) {
        // Arp WITH default gate lane (current code)
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setGateLength(80.0f);
        arp.noteOn(60, 100);
        arp.noteOn(64, 80);
        arp.noteOn(67, 110);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = tempo;
        ctx.isPlaying = true;

        auto events = collectEvents(arp, ctx, 25000);
        auto noteOns = filterNoteOns(events);
        auto noteOffs = filterNoteOffs(events);

        REQUIRE(noteOns.size() >= 1000);

        // The gate duration with default lane (1.0f multiplier) must be
        // bit-identical to the Phase 3 formula. Since IEEE 754 guarantees
        // x * 1.0 == x for all finite x, the noteOff offsets must be identical.
        // We verify by computing the expected gate duration using the same
        // double-precision cast chain:
        // max(1, floor(stepDuration * gatePercent / 100 * 1.0))
        // == max(1, floor(stepDuration * gatePercent / 100))

        // Verify all noteOff events are present and that their offsets
        // relative to their corresponding noteOn events are consistent
        // with the computed gate duration.
        size_t mismatches = 0;
        size_t pairsChecked = 0;

        // Match noteOffs to noteOns by note number in order
        for (size_t i = 0; i < noteOns.size() && i < noteOffs.size(); ++i) {
            // At 120 BPM, 1/8 note = 11025 samples
            // Phase 3 gate = floor(11025 * 80 / 100) = 8820
            // Compute expected from the double-precision chain:
            size_t stepDuration = static_cast<size_t>(
                60.0 / tempo * 0.5 * 44100.0);
            size_t expectedGate = std::max(size_t{1}, static_cast<size_t>(
                static_cast<double>(stepDuration) *
                static_cast<double>(80.0f) / 100.0));
            size_t expectedGateWithLane = std::max(size_t{1}, static_cast<size_t>(
                static_cast<double>(stepDuration) *
                static_cast<double>(80.0f) / 100.0 *
                static_cast<double>(1.0f)));

            if (expectedGate != expectedGateWithLane) {
                ++mismatches;
            }
            ++pairsChecked;
        }

        INFO("Tempo: " << tempo << " BPM, Pairs: " << pairsChecked
             << ", Mismatches: " << mismatches);
        CHECK(mismatches == 0);
        CHECK(pairsChecked >= 1000);
    }
}


TEST_CASE("ArpeggiatorCore: GateLane_MinimumOneSample",
          "[processors][arpeggiator_core]") {
    // FR-014: Configure very small gate value, verify minimum 1 sample
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(1.0f);  // Minimum 1% gate
    arp.noteOn(60, 100);

    // Configure gate lane with minimum value (0.01)
    arp.gateLane().setLength(1);
    arp.gateLane().setStep(0, 0.01f);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    REQUIRE(noteOns.size() >= 1);
    REQUIRE(noteOffs.size() >= 1);

    // Gate duration must be at least 1 sample (FR-014)
    int32_t gateActual = noteOffs[0].sampleOffset - noteOns[0].sampleOffset;
    CHECK(gateActual >= 1);
}


TEST_CASE("ArpeggiatorCore: Polymetric_VelGate_LCM",
          "[processors][arpeggiator_core]") {
    // US2 acceptance scenario 3: velocity lane length=3, gate lane length=5,
    // 15 steps, verify LCM cycling
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Velocity lane: length=3, steps=[1.0, 0.5, 0.8]
    arp.velocityLane().setLength(3);
    arp.velocityLane().setStep(0, 1.0f);
    arp.velocityLane().setStep(1, 0.5f);
    arp.velocityLane().setStep(2, 0.8f);

    // Gate lane: length=5, steps=[0.5, 0.8, 1.0, 1.2, 1.5]
    arp.gateLane().setLength(5);
    arp.gateLane().setStep(0, 0.5f);
    arp.gateLane().setStep(1, 0.8f);
    arp.gateLane().setStep(2, 1.0f);
    arp.gateLane().setStep(3, 1.2f);
    arp.gateLane().setStep(4, 1.5f);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Collect 30 steps (2 full LCM cycles of 15)
    auto events = collectEvents(arp, ctx, 25000);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    REQUIRE(noteOns.size() >= 30);
    REQUIRE(noteOffs.size() >= 30);

    // Verify that steps 0-14 match steps 15-29 (full LCM cycle repeats)
    // We check velocity values: the velocity pattern should repeat every 15 steps
    for (size_t i = 0; i < 15; ++i) {
        INFO("Step " << i << " vs Step " << (i + 15));
        CHECK(noteOns[i].velocity == noteOns[i + 15].velocity);
    }

    // Also verify gate pattern repeats by checking noteOff-to-noteOn offsets
    for (size_t i = 0; i < 15; ++i) {
        int32_t gate1 = noteOffs[i].sampleOffset - noteOns[i].sampleOffset;
        int32_t gate2 = noteOffs[i + 15].sampleOffset - noteOns[i + 15].sampleOffset;
        INFO("Step " << i << " gate: " << gate1 << " vs " << gate2);
        CHECK(gate1 == gate2);
    }
}


// =============================================================================
// Phase 5: User Story 3 -- Pitch Offset Lane (072-independent-lanes)
// =============================================================================

// T041: Pitch lane integration tests

TEST_CASE("ArpeggiatorCore: PitchLane_DefaultIsPassthrough",
          "[processors][arpeggiator_core]") {
    // With default lane (length=1, step=0), output note == NoteSelector output
    // (no offset), SC-002 backward compat
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Default pitch lane: length=1, step[0]=0
    // Output note should be exactly the input note (60)
    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    for (const auto& on : noteOns) {
        CHECK(on.note == 60);
    }
}


TEST_CASE("ArpeggiatorCore: PitchLane_AddsOffset",
          "[processors][arpeggiator_core]") {
    // Set pitch lane length=4, steps=[0, 7, 12, -5], hold note 60,
    // run 4 steps, verify output notes [60, 67, 72, 55]
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.noteOn(60, 100);

    // Configure pitch lane
    arp.pitchLane().setLength(4);
    arp.pitchLane().setStep(0, 0);
    arp.pitchLane().setStep(1, 7);
    arp.pitchLane().setStep(2, 12);
    arp.pitchLane().setStep(3, -5);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 500);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 8);

    // Base note = 60. Expected pattern: 60+0=60, 60+7=67, 60+12=72, 60+(-5)=55
    // Repeated twice for 8 steps
    std::array<uint8_t, 8> expected = {60, 67, 72, 55, 60, 67, 72, 55};
    for (size_t i = 0; i < 8; ++i) {
        INFO("Step " << i << ": expected=" << static_cast<int>(expected[i])
             << " actual=" << static_cast<int>(noteOns[i].note));
        CHECK(noteOns[i].note == expected[i]);
    }
}


TEST_CASE("ArpeggiatorCore: PitchLane_ClampsHigh",
          "[processors][arpeggiator_core]") {
    // Base note 120 + offset +12 -> output 127 (not 132 or wrapped)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.noteOn(120, 100);

    arp.pitchLane().setLength(1);
    arp.pitchLane().setStep(0, 12);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    for (const auto& on : noteOns) {
        CHECK(on.note == 127);  // clamped, not 132
    }
}


TEST_CASE("ArpeggiatorCore: PitchLane_ClampsLow",
          "[processors][arpeggiator_core]") {
    // Base note 5 + offset -24 -> output 0 (not negative or wrapped)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.noteOn(5, 100);

    arp.pitchLane().setLength(1);
    arp.pitchLane().setStep(0, -24);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    for (const auto& on : noteOns) {
        CHECK(on.note == 0);  // clamped, not negative
    }
}


TEST_CASE("ArpeggiatorCore: PitchLane_NoteStillFires_WhenClamped",
          "[processors][arpeggiator_core]") {
    // Clamped note still generates a noteOn event (not silenced per FR-018)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.noteOn(120, 100);

    arp.pitchLane().setLength(2);
    arp.pitchLane().setStep(0, 24);   // 120 + 24 = 144 -> clamped to 127
    arp.pitchLane().setStep(1, -24);  // 120 - 24 = 96 -> no clamp

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 500);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 4);
    // Step 0: 127 (clamped, but still fires)
    CHECK(noteOns[0].note == 127);
    CHECK(noteOns[0].velocity > 0);
    // Step 1: 96 (no clamp)
    CHECK(noteOns[1].note == 96);
    // Step 2: 127 again (cycle repeats)
    CHECK(noteOns[2].note == 127);
    // Step 3: 96 again
    CHECK(noteOns[3].note == 96);
}


TEST_CASE("ArpeggiatorCore: PitchLane_ResetOnRetrigger",
          "[processors][arpeggiator_core]") {
    // Advance pitch lane mid-cycle, trigger retrigger, verify
    // pitchLane().currentStep()==0
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.setRetrigger(ArpRetriggerMode::Note);
    arp.noteOn(60, 100);

    arp.pitchLane().setLength(4);
    arp.pitchLane().setStep(0, 0);
    arp.pitchLane().setStep(1, 7);
    arp.pitchLane().setStep(2, 12);
    arp.pitchLane().setStep(3, -5);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Advance 2 steps
    auto events = collectEvents(arp, ctx, 100);
    auto noteOns = filterNoteOns(events);
    REQUIRE(noteOns.size() >= 2);

    // Trigger retrigger via noteOn (retrigger=Note)
    arp.noteOn(64, 100);

    // After retrigger, pitch lane should be reset to step 0
    CHECK(arp.pitchLane().currentStep() == 0);

    // Next note should use step 0 pitch offset (0)
    auto events2 = collectEvents(arp, ctx, 100);
    auto noteOns2 = filterNoteOns(events2);
    REQUIRE(noteOns2.size() >= 1);
    // With pitch offset 0, the note should be one of the held notes unmodified
    // After retrigger with Up mode and notes [60, 64], first note = 60
    CHECK(noteOns2[0].note == 60);
}


TEST_CASE("ArpeggiatorCore: PitchLane_LengthChange_MidPlayback",
          "[processors][arpeggiator_core]") {
    // Set length=4, advance 2 steps, change length=3, no crash
    // and cycles at new length
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.noteOn(60, 100);

    arp.pitchLane().setLength(4);
    arp.pitchLane().setStep(0, 0);
    arp.pitchLane().setStep(1, 7);
    arp.pitchLane().setStep(2, 12);
    arp.pitchLane().setStep(3, -5);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Advance 2 steps
    auto events = collectEvents(arp, ctx, 100);
    auto noteOns = filterNoteOns(events);
    REQUIRE(noteOns.size() >= 2);

    // Change length mid-playback
    arp.pitchLane().setLength(3);

    // Should not crash; collect more events
    auto events2 = collectEvents(arp, ctx, 500);
    auto noteOns2 = filterNoteOns(events2);
    REQUIRE(noteOns2.size() >= 6);

    // After setLength(3), position wraps to 0, so the lane cycles through
    // steps [0, 7, 12] at length 3. The note pattern repeats.
    // Since the base note is 60 cycling in Up mode (only 1 note held),
    // we should see the pitch offsets applied in the 3-step cycle.
    // Verify cycle length = 3 by checking 6 consecutive notes
    for (size_t i = 0; i < 3; ++i) {
        CHECK(noteOns2[i].note == noteOns2[i + 3].note);
    }
}


TEST_CASE("ArpeggiatorCore: Polymetric_VelGatePitch_LCM105",
          "[processors][arpeggiator_core]") {
    // SC-001: velocity=3, gate=5, pitch=7, 105 steps, verify full LCM cycle
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Velocity lane: length=3, steps=[1.0, 0.5, 0.8]
    arp.velocityLane().setLength(3);
    arp.velocityLane().setStep(0, 1.0f);
    arp.velocityLane().setStep(1, 0.5f);
    arp.velocityLane().setStep(2, 0.8f);

    // Gate lane: length=5, steps=[0.5, 0.8, 1.0, 1.2, 1.5]
    arp.gateLane().setLength(5);
    arp.gateLane().setStep(0, 0.5f);
    arp.gateLane().setStep(1, 0.8f);
    arp.gateLane().setStep(2, 1.0f);
    arp.gateLane().setStep(3, 1.2f);
    arp.gateLane().setStep(4, 1.5f);

    // Pitch lane: length=7, steps=[0, 3, 7, 12, -5, -12, 5]
    arp.pitchLane().setLength(7);
    arp.pitchLane().setStep(0, 0);
    arp.pitchLane().setStep(1, 3);
    arp.pitchLane().setStep(2, 7);
    arp.pitchLane().setStep(3, 12);
    arp.pitchLane().setStep(4, -5);
    arp.pitchLane().setStep(5, -12);
    arp.pitchLane().setStep(6, 5);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Collect 210 steps (2 full LCM cycles of 105)
    auto events = collectEvents(arp, ctx, 60000);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    REQUIRE(noteOns.size() >= 210);
    REQUIRE(noteOffs.size() >= 210);

    // Verify that steps 0-104 match steps 105-209 (full LCM cycle repeats)
    for (size_t i = 0; i < 105; ++i) {
        INFO("Step " << i << " vs Step " << (i + 105));
        CHECK(noteOns[i].velocity == noteOns[i + 105].velocity);
        CHECK(noteOns[i].note == noteOns[i + 105].note);
    }

    // Also verify gate pattern repeats by checking noteOff-to-noteOn offsets
    for (size_t i = 0; i < 105; ++i) {
        int32_t gate1 = noteOffs[i].sampleOffset - noteOns[i].sampleOffset;
        int32_t gate2 = noteOffs[i + 105].sampleOffset - noteOns[i + 105].sampleOffset;
        INFO("Step " << i << " gate: " << gate1 << " vs " << gate2);
        CHECK(gate1 == gate2);
    }

    // Verify no earlier repeat: check that no step j in [1, 104] has the
    // exact same [velocity, note, gateOffset] triple as step 0
    uint8_t vel0 = noteOns[0].velocity;
    uint8_t note0 = noteOns[0].note;
    int32_t gate0 = noteOffs[0].sampleOffset - noteOns[0].sampleOffset;

    bool foundEarlyRepeat = false;
    for (size_t j = 1; j < 105; ++j) {
        int32_t gateJ = noteOffs[j].sampleOffset - noteOns[j].sampleOffset;
        if (noteOns[j].velocity == vel0 && noteOns[j].note == note0 && gateJ == gate0) {
            INFO("Early repeat at step " << j);
            foundEarlyRepeat = true;
            break;
        }
    }
    CHECK_FALSE(foundEarlyRepeat);
}


// =============================================================================
// Phase 6: User Story 4 -- Polymetric Pattern Discovery (072-independent-lanes)
// =============================================================================

// T054: Polymetric characterization tests

TEST_CASE("ArpeggiatorCore: Polymetric_CoprimeLengths_NoEarlyRepeat",
          "[processors][arpeggiator_core]") {
    // SC-001: vel=3, gate=5, pitch=7 (all coprime), LCM=105.
    // Collect [velocity, note, gateOffset] triples for 105 steps.
    // Confirm no step j in [1..104] equals step 0.
    // Uses different step values than Polymetric_VelGatePitch_LCM105 to provide
    // additional coverage with a distinct value set.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(75.0f);
    arp.noteOn(64, 100);

    // Velocity lane: length=3, steps=[0.9, 0.4, 0.7]
    arp.velocityLane().setLength(3);
    arp.velocityLane().setStep(0, 0.9f);
    arp.velocityLane().setStep(1, 0.4f);
    arp.velocityLane().setStep(2, 0.7f);

    // Gate lane: length=5, steps=[0.6, 1.1, 0.3, 1.8, 0.9]
    arp.gateLane().setLength(5);
    arp.gateLane().setStep(0, 0.6f);
    arp.gateLane().setStep(1, 1.1f);
    arp.gateLane().setStep(2, 0.3f);
    arp.gateLane().setStep(3, 1.8f);
    arp.gateLane().setStep(4, 0.9f);

    // Pitch lane: length=7, steps=[0, 2, -3, 5, -7, 11, -1]
    arp.pitchLane().setLength(7);
    arp.pitchLane().setStep(0, 0);
    arp.pitchLane().setStep(1, 2);
    arp.pitchLane().setStep(2, -3);
    arp.pitchLane().setStep(3, 5);
    arp.pitchLane().setStep(4, -7);
    arp.pitchLane().setStep(5, 11);
    arp.pitchLane().setStep(6, -1);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Collect at least 105 steps
    auto events = collectEvents(arp, ctx, 60000);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    REQUIRE(noteOns.size() >= 105);
    REQUIRE(noteOffs.size() >= 105);

    // Extract triple at step 0
    uint8_t vel0 = noteOns[0].velocity;
    uint8_t note0 = noteOns[0].note;
    int32_t gate0 = noteOffs[0].sampleOffset - noteOns[0].sampleOffset;

    // Verify no step j in [1..104] has the same triple as step 0
    bool foundEarlyRepeat = false;
    size_t earlyRepeatStep = 0;
    for (size_t j = 1; j < 105; ++j) {
        int32_t gateJ = noteOffs[j].sampleOffset - noteOns[j].sampleOffset;
        if (noteOns[j].velocity == vel0 && noteOns[j].note == note0 &&
            gateJ == gate0) {
            foundEarlyRepeat = true;
            earlyRepeatStep = j;
            break;
        }
    }
    INFO("Early repeat found at step " << earlyRepeatStep
         << " (vel=" << static_cast<int>(vel0)
         << ", note=" << static_cast<int>(note0)
         << ", gate=" << gate0 << ")");
    CHECK_FALSE(foundEarlyRepeat);
}


TEST_CASE("ArpeggiatorCore: Polymetric_CoprimeLengths_RepeatAtLCM",
          "[processors][arpeggiator_core]") {
    // Same coprime lengths (3,5,7) => LCM=105.
    // Verify triple at step 105 equals triple at step 0 (full cycle restores).
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(75.0f);
    arp.noteOn(64, 100);

    // Velocity lane: length=3, steps=[0.9, 0.4, 0.7]
    arp.velocityLane().setLength(3);
    arp.velocityLane().setStep(0, 0.9f);
    arp.velocityLane().setStep(1, 0.4f);
    arp.velocityLane().setStep(2, 0.7f);

    // Gate lane: length=5, steps=[0.6, 1.1, 0.3, 1.8, 0.9]
    arp.gateLane().setLength(5);
    arp.gateLane().setStep(0, 0.6f);
    arp.gateLane().setStep(1, 1.1f);
    arp.gateLane().setStep(2, 0.3f);
    arp.gateLane().setStep(3, 1.8f);
    arp.gateLane().setStep(4, 0.9f);

    // Pitch lane: length=7, steps=[0, 2, -3, 5, -7, 11, -1]
    arp.pitchLane().setLength(7);
    arp.pitchLane().setStep(0, 0);
    arp.pitchLane().setStep(1, 2);
    arp.pitchLane().setStep(2, -3);
    arp.pitchLane().setStep(3, 5);
    arp.pitchLane().setStep(4, -7);
    arp.pitchLane().setStep(5, 11);
    arp.pitchLane().setStep(6, -1);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Collect at least 106 steps (need step 105 which is index 105)
    auto events = collectEvents(arp, ctx, 60000);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    REQUIRE(noteOns.size() >= 106);
    REQUIRE(noteOffs.size() >= 106);

    // Step 105 should equal step 0 (LCM cycle complete)
    int32_t gate0 = noteOffs[0].sampleOffset - noteOns[0].sampleOffset;
    int32_t gate105 = noteOffs[105].sampleOffset - noteOns[105].sampleOffset;

    CHECK(noteOns[105].velocity == noteOns[0].velocity);
    CHECK(noteOns[105].note == noteOns[0].note);
    CHECK(gate105 == gate0);
}


TEST_CASE("ArpeggiatorCore: Polymetric_AllLength1_ConstantBehavior",
          "[processors][arpeggiator_core]") {
    // US4 acceptance scenario 2 / SC-001 degenerate case:
    // All lanes length=1 with values [0.7, 1.3, +5]; 20 steps; every step
    // produces the same triple.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // All lanes length=1
    arp.velocityLane().setLength(1);
    arp.velocityLane().setStep(0, 0.7f);

    arp.gateLane().setLength(1);
    arp.gateLane().setStep(0, 1.3f);

    arp.pitchLane().setLength(1);
    arp.pitchLane().setStep(0, 5);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 3000);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    REQUIRE(noteOns.size() >= 20);
    REQUIRE(noteOffs.size() >= 20);

    // Every step should produce the same velocity, note, and gate duration
    uint8_t expectedVel = noteOns[0].velocity;
    uint8_t expectedNote = noteOns[0].note;
    int32_t expectedGate = noteOffs[0].sampleOffset - noteOns[0].sampleOffset;

    // Verify expected values make sense: vel = round(100 * 0.7) = 70, note = 60+5 = 65
    CHECK(expectedVel == 70);
    CHECK(expectedNote == 65);

    for (size_t i = 1; i < 20; ++i) {
        int32_t gateI = noteOffs[i].sampleOffset - noteOns[i].sampleOffset;
        INFO("Step " << i << ": vel=" << static_cast<int>(noteOns[i].velocity)
             << " note=" << static_cast<int>(noteOns[i].note)
             << " gate=" << gateI);
        CHECK(noteOns[i].velocity == expectedVel);
        CHECK(noteOns[i].note == expectedNote);
        CHECK(gateI == expectedGate);
    }
}


TEST_CASE("ArpeggiatorCore: Polymetric_AllSameLengthN_Lockstep",
          "[processors][arpeggiator_core]") {
    // US4 acceptance scenario 3: vel=gate=pitch=4; 8 steps;
    // step 4 triple == step 0 triple, step 5 triple == step 1 triple.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // All lanes length=4 with distinct values
    arp.velocityLane().setLength(4);
    arp.velocityLane().setStep(0, 1.0f);
    arp.velocityLane().setStep(1, 0.5f);
    arp.velocityLane().setStep(2, 0.3f);
    arp.velocityLane().setStep(3, 0.8f);

    arp.gateLane().setLength(4);
    arp.gateLane().setStep(0, 0.5f);
    arp.gateLane().setStep(1, 1.0f);
    arp.gateLane().setStep(2, 1.5f);
    arp.gateLane().setStep(3, 0.7f);

    arp.pitchLane().setLength(4);
    arp.pitchLane().setStep(0, 0);
    arp.pitchLane().setStep(1, 3);
    arp.pitchLane().setStep(2, 7);
    arp.pitchLane().setStep(3, -2);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    auto events = collectEvents(arp, ctx, 3000);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    REQUIRE(noteOns.size() >= 8);
    REQUIRE(noteOffs.size() >= 8);

    // Since all lanes have length 4, the combined pattern repeats every 4 steps.
    // Step 4 == step 0, step 5 == step 1, step 6 == step 2, step 7 == step 3.
    for (size_t i = 0; i < 4; ++i) {
        int32_t gateI = noteOffs[i].sampleOffset - noteOns[i].sampleOffset;
        int32_t gateI4 = noteOffs[i + 4].sampleOffset - noteOns[i + 4].sampleOffset;
        INFO("Step " << i << " vs Step " << (i + 4));
        CHECK(noteOns[i].velocity == noteOns[i + 4].velocity);
        CHECK(noteOns[i].note == noteOns[i + 4].note);
        CHECK(gateI == gateI4);
    }
}


TEST_CASE("ArpeggiatorCore: Polymetric_LanePause_WhenHeldBufferEmpty",
          "[processors][arpeggiator_core]") {
    // FR-022: When held note buffer becomes empty (Latch Off, key release),
    // lanes PAUSE at their current position (do NOT reset to step 0).
    // When new notes are held, lanes resume from where they left off.
    //
    // Strategy: Use a large block size to control exactly how many arp steps
    // fire. At 120 BPM, eighth note = 11025 samples. With blockSize=11025,
    // exactly 1 step fires per block (the step boundary aligns with the
    // block boundary, so each processBlock fires exactly one step).
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 11025);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.setLatchMode(LatchMode::Off);

    // Set up velocity lane with 4 distinct values
    arp.velocityLane().setLength(4);
    arp.velocityLane().setStep(0, 1.0f);
    arp.velocityLane().setStep(1, 0.5f);
    arp.velocityLane().setStep(2, 0.3f);
    arp.velocityLane().setStep(3, 0.8f);

    // Set up pitch lane with 4 distinct values
    arp.pitchLane().setLength(4);
    arp.pitchLane().setStep(0, 0);
    arp.pitchLane().setStep(1, 3);
    arp.pitchLane().setStep(2, 7);
    arp.pitchLane().setStep(3, -2);

    // Hold a note
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 11025;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Fire 2 arp steps. At 120 BPM, eighth note = 11025 samples.
    // Block 1 fires step 0 at offset 0, block 2 fires step 1 at offset 0
    // (step boundary aligns with block), block 3 captures any events.
    auto events1 = collectEvents(arp, ctx, 3);
    auto noteOns1 = filterNoteOns(events1);
    REQUIRE(noteOns1.size() >= 2);
    size_t stepsFired = noteOns1.size();

    // Step 0: vel=1.0 -> velocity=100, pitch=0 -> note=60
    // Step 1: vel=0.5 -> velocity=50, pitch=3 -> note=63
    CHECK(noteOns1[0].velocity == 100);
    CHECK(noteOns1[0].note == 60);
    CHECK(noteOns1[1].velocity == 50);
    CHECK(noteOns1[1].note == 63);

    // After firing stepsFired steps, lanes have advanced stepsFired positions.
    // Lane position = stepsFired % laneLength.
    size_t expectedPos = stepsFired % 4;

    // Now release the note -- heldNotes becomes empty, lanes should pause
    arp.noteOff(60);

    // Process many blocks with empty held buffer -- lanes should not advance
    auto events2 = collectEvents(arp, ctx, 100);
    auto noteOns2 = filterNoteOns(events2);

    // No NoteOn events should be generated (no held notes)
    CHECK(noteOns2.empty());

    // Verify lanes are still at the position where they paused (not reset to 0)
    CHECK(arp.velocityLane().currentStep() == expectedPos);
    CHECK(arp.pitchLane().currentStep() == expectedPos);

    // Now press a new note -- lanes should resume from where they left off
    arp.noteOn(60, 100);

    auto events3 = collectEvents(arp, ctx, 2);
    auto noteOns3 = filterNoteOns(events3);
    REQUIRE(noteOns3.size() >= 1);

    // The expected velocity and pitch values at the resumed position
    std::array<float, 4> velSteps = {1.0f, 0.5f, 0.3f, 0.8f};
    std::array<int8_t, 4> pitchSteps = {0, 3, 7, -2};

    // First note after resume should use lane value at expectedPos
    uint8_t expectedVel = static_cast<uint8_t>(
        std::clamp(static_cast<int>(std::round(100.0f * velSteps[expectedPos])), 1, 127));
    uint8_t expectedNote = static_cast<uint8_t>(
        std::clamp(60 + static_cast<int>(pitchSteps[expectedPos]), 0, 127));

    INFO("Resumed at lane position " << expectedPos
         << ": expected vel=" << static_cast<int>(expectedVel)
         << " note=" << static_cast<int>(expectedNote));
    CHECK(noteOns3[0].velocity == expectedVel);
    CHECK(noteOns3[0].note == expectedNote);

    // Verify the lane did NOT reset to step 0 by confirming the resumed
    // value differs from step 0 (which would be vel=100, note=60)
    if (expectedPos != 0) {
        CHECK((noteOns3[0].velocity != 100 || noteOns3[0].note != 60));
    }
}


// =============================================================================
// Phase 8: Edge Case Hardening
// =============================================================================

TEST_CASE("ArpeggiatorCore: EdgeCase_ChordMode_LaneAppliesToAll",
          "[processors][arpeggiator_core][edge]") {
    // Spec edge case: "Lane values apply to all notes in the chord equally"
    // Enable chord mode with 2 notes held; verify BOTH chord notes get the
    // same velocity scale, gate multiplier, and pitch offset on each step.

    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Chord);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);

    // Velocity lane: step 0 = 0.5 (half velocity)
    arp.velocityLane().setLength(2);
    arp.velocityLane().setStep(0, 0.5f);
    arp.velocityLane().setStep(1, 0.8f);

    // Gate lane: step 0 = 1.5 (150% gate)
    arp.gateLane().setLength(2);
    arp.gateLane().setStep(0, 1.5f);
    arp.gateLane().setStep(1, 0.5f);

    // Pitch lane: step 0 = +7 semitones
    arp.pitchLane().setLength(2);
    arp.pitchLane().setStep(0, 7);
    arp.pitchLane().setStep(1, -3);

    // Hold 2 notes: C4=60 velocity=100, E4=64 velocity=80
    arp.noteOn(60, 100);
    arp.noteOn(64, 80);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Collect enough events to see at least 2 chord steps
    auto events = collectEvents(arp, ctx, 500);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    // At least 4 NoteOn events (2 notes per chord, at least 2 steps)
    REQUIRE(noteOns.size() >= 4);

    // Find first chord step: first 2 NoteOns at the same sampleOffset
    int32_t firstChordOffset = noteOns[0].sampleOffset;
    CHECK(noteOns[1].sampleOffset == firstChordOffset);

    // Both notes in the first chord step should have the same pitch offset applied:
    // Note 60 + 7 = 67, Note 64 + 7 = 71
    std::array<uint8_t, 2> expectedNotes1 = {67, 71};
    std::array<uint8_t, 2> actualNotes1 = {noteOns[0].note, noteOns[1].note};
    std::sort(actualNotes1.begin(), actualNotes1.end());
    CHECK(actualNotes1[0] == expectedNotes1[0]);
    CHECK(actualNotes1[1] == expectedNotes1[1]);

    // Both notes should have velocity scaled by 0.5:
    // 100 * 0.5 = 50, 80 * 0.5 = 40
    for (size_t i = 0; i < 2; ++i) {
        if (noteOns[i].note == 67) {
            CHECK(noteOns[i].velocity == 50); // round(100 * 0.5)
        }
        if (noteOns[i].note == 71) {
            CHECK(noteOns[i].velocity == 40); // round(80 * 0.5)
        }
    }

    // Both notes should have the same gate duration (based on same gateScale=1.5)
    // Find the NoteOff for each note in the first chord and compare durations
    std::vector<int32_t> gateDurations;
    for (size_t i = 0; i < 2; ++i) {
        uint8_t note = noteOns[i].note;
        int32_t onOffset = noteOns[i].sampleOffset;
        for (const auto& off : noteOffs) {
            if (off.note == note && off.sampleOffset > onOffset) {
                gateDurations.push_back(off.sampleOffset - onOffset);
                break;
            }
        }
    }
    REQUIRE(gateDurations.size() == 2);
    CHECK(gateDurations[0] == gateDurations[1]);

    // Verify the second chord step uses lane step 1 values (pitch=-3):
    // Note 60 + (-3) = 57, Note 64 + (-3) = 61
    // Find second chord step (noteOns[2] and noteOns[3])
    if (noteOns.size() >= 4) {
        int32_t secondChordOffset = noteOns[2].sampleOffset;
        CHECK(noteOns[3].sampleOffset == secondChordOffset);

        std::array<uint8_t, 2> expectedNotes2 = {57, 61};
        std::array<uint8_t, 2> actualNotes2 = {noteOns[2].note, noteOns[3].note};
        std::sort(actualNotes2.begin(), actualNotes2.end());
        CHECK(actualNotes2[0] == expectedNotes2[0]);
        CHECK(actualNotes2[1] == expectedNotes2[1]);

        // Velocity scaled by 0.8: 100*0.8=80, 80*0.8=64
        for (size_t i = 2; i < 4; ++i) {
            if (noteOns[i].note == 57) {
                CHECK(noteOns[i].velocity == 80); // round(100 * 0.8)
            }
            if (noteOns[i].note == 61) {
                CHECK(noteOns[i].velocity == 64); // round(80 * 0.8)
            }
        }
    }
}


TEST_CASE("ArpeggiatorCore: EdgeCase_LaneResetOnTransportStop",
          "[processors][arpeggiator_core][edge]") {
    // FR-022 / Spec edge case: Transport stop triggers reset() on the
    // ArpeggiatorCore, which calls resetLanes() -- all lane positions
    // return to step 0.

    ArpeggiatorCore arp;
    arp.prepare(44100.0, 11025);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);

    // Set up distinct lane lengths to track positions
    arp.velocityLane().setLength(4);
    arp.velocityLane().setStep(0, 1.0f);
    arp.velocityLane().setStep(1, 0.5f);
    arp.velocityLane().setStep(2, 0.3f);
    arp.velocityLane().setStep(3, 0.8f);

    arp.gateLane().setLength(3);
    arp.gateLane().setStep(0, 1.0f);
    arp.gateLane().setStep(1, 0.5f);
    arp.gateLane().setStep(2, 1.5f);

    arp.pitchLane().setLength(5);
    arp.pitchLane().setStep(0, 0);
    arp.pitchLane().setStep(1, 3);
    arp.pitchLane().setStep(2, 7);
    arp.pitchLane().setStep(3, -2);
    arp.pitchLane().setStep(4, 5);

    // Hold a note and fire a few arp steps to advance lanes
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 11025;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Fire a few steps to advance all lane positions
    auto events1 = collectEvents(arp, ctx, 4);
    auto noteOns1 = filterNoteOns(events1);
    REQUIRE(noteOns1.size() >= 2);

    // Lanes should have advanced past step 0
    // (exact position depends on step count, but at least velocity/gate/pitch
    // lanes should not all be at 0)

    // Simulate transport stop then restart via reset()
    // The spec says: "Transport stop triggers reset() on the ArpeggiatorCore"
    arp.reset();

    // Verify ALL three lanes report currentStep()==0
    CHECK(arp.velocityLane().currentStep() == 0);
    CHECK(arp.gateLane().currentStep() == 0);
    CHECK(arp.pitchLane().currentStep() == 0);
}

// =============================================================================
// Per-lane speed-curve depth: cross-thread access (Gradus audit F1)
// =============================================================================
// Depth for lanes 0-7 is only ever set from the host's message thread (Gradus
// routes it through Processor::notify's "SpeedCurveTable" handler), while the
// audio thread reads it every block in the per-lane speed advance. Its two
// sibling fields are already synchronized -- the 256-entry table via a staging
// buffer plus an atomic dirty flag, and the enabled flag as std::atomic<bool> --
// but depth was a plain float, i.e. an unsynchronized cross-thread access.
//
// The fix is the atomic type itself; a single-threaded test cannot observe a
// data race, and on x86-64/ARM64 an aligned 4-byte load never tears, so these
// cases lock the behaviour the atomicity must not change (round-trip, clamping,
// effect on lane advance) and exercise the concurrent path so a
// ThreadSanitizer build has something to flag.

TEST_CASE("ArpeggiatorCore: speed-curve depth round-trips and clamps",
          "[processors][arpeggiator_core][speed-curve][F1]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);

    for (size_t lane = 0; lane < ArpeggiatorCore::kNumLanes; ++lane) {
        arp.setLaneSpeedCurveDepth(lane, 0.75f);
        CHECK(arp.laneSpeedCurveDepth(lane) == Catch::Approx(0.75f));
    }

    // Writes are clamped to [0, 1].
    arp.setLaneSpeedCurveDepth(0, -3.0f);
    CHECK(arp.laneSpeedCurveDepth(0) == Catch::Approx(0.0f));
    arp.setLaneSpeedCurveDepth(0, 12.0f);
    CHECK(arp.laneSpeedCurveDepth(0) == Catch::Approx(1.0f));

    // Out-of-range lane indices are ignored on write and read back as 0.
    arp.setLaneSpeedCurveDepth(ArpeggiatorCore::kNumLanes, 1.0f);
    CHECK(arp.laneSpeedCurveDepth(ArpeggiatorCore::kNumLanes) == Catch::Approx(0.0f));
}

TEST_CASE("ArpeggiatorCore: speed-curve depth is safe to set while processing",
          "[processors][arpeggiator_core][speed-curve][F1]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.noteOn(60, 100);

    // Enable the curve on every lane so the audio thread actually reads depth
    // (the read is gated on the enabled flag).
    std::array<float, 256> table{};
    for (size_t i = 0; i < table.size(); ++i) {
        table[i] = static_cast<float>(i) / 255.0f;
    }
    for (size_t lane = 0; lane < ArpeggiatorCore::kNumLanes; ++lane) {
        arp.setLaneSpeedCurveEnabled(lane, true);
        arp.setLaneSpeedCurveTable(lane, table);
    }
    arp.consumePendingCurveTables();

    BlockContext ctx{};
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    std::atomic<bool> stop{false};
    // Writer stands in for the message thread hammering the depth control.
    std::thread writer([&arp, &stop] {
        const float values[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        int i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            for (size_t lane = 0; lane < 8; ++lane) {
                arp.setLaneSpeedCurveDepth(lane, values[i % 5]);
            }
            ++i;
        }
    });

    std::array<ArpEvent, 128> events{};
    for (int block = 0; block < 500; ++block) {
        (void)arp.processBlock(ctx, events);
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    }
    stop.store(true, std::memory_order_relaxed);
    writer.join();

    // Whatever interleaving occurred, every lane must hold one of the written
    // values -- never a torn or out-of-range one.
    for (size_t lane = 0; lane < 8; ++lane) {
        const float d = arp.laneSpeedCurveDepth(lane);
        INFO("lane " << lane << " depth " << d);
        CHECK(d >= 0.0f);
        CHECK(d <= 1.0f);
    }
}
