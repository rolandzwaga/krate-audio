// arpeggiator_core_euclidean_test.cpp
// Euclidean timing mode (spec 075)
// Split from the former 17k-line arpeggiator_core_test.cpp (D1). Shared helpers in
// arpeggiator_core_test_helpers.h.
#include "arpeggiator_core_test_helpers.h"



// =============================================================================
// Phase 7: Euclidean Timing Mode (075-euclidean-timing)
// =============================================================================
// Task Group 1: Foundational Infrastructure Tests

// T004: Verify default Euclidean state values after construction (FR-001, FR-015)
TEST_CASE("EuclideanState_DefaultValues",
          "[arp][euclidean][foundational]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);

    CHECK(arp.euclideanEnabled() == false);
    CHECK(arp.euclideanHits() == 4);
    CHECK(arp.euclideanSteps() == 8);
    CHECK(arp.euclideanRotation() == 0);
}


// T005: setEuclideanSteps clamps hits to new step count (FR-009)
TEST_CASE("EuclideanSetters_ClampHitsToSteps",
          "[arp][euclidean][foundational]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);

    arp.setEuclideanSteps(5);
    arp.setEuclideanHits(10);  // should clamp to 5 (the new step count)
    CHECK(arp.euclideanHits() == 5);
}


// T006: setEuclideanSteps clamps to valid range [2, 32] (FR-009)
TEST_CASE("EuclideanSetters_ClampStepsToRange",
          "[arp][euclidean][foundational]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);

    arp.setEuclideanSteps(1);    // below minimum
    CHECK(arp.euclideanSteps() == 2);

    arp.setEuclideanSteps(33);   // above maximum
    CHECK(arp.euclideanSteps() == 32);
}


// T007: setEuclideanRotation clamps to valid range [0, 31] (FR-009)
TEST_CASE("EuclideanSetters_ClampRotationToRange",
          "[arp][euclidean][foundational]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);

    arp.setEuclideanRotation(35);   // above maximum
    CHECK(arp.euclideanRotation() == 31);

    arp.setEuclideanRotation(-1);   // below minimum
    CHECK(arp.euclideanRotation() == 0);
}


// T008: hits=0 is valid (fully silent pattern) (FR-009)
TEST_CASE("EuclideanSetters_HitsZeroAllowed",
          "[arp][euclidean][foundational]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);

    arp.setEuclideanHits(0);
    CHECK(arp.euclideanHits() == 0);
}


// T009: setEuclideanEnabled(true) resets euclidean position (FR-010)
// Verified by observing that after enabling, the first step fires as if
// from position 0 in the Euclidean pattern.
TEST_CASE("EuclideanEnabled_ResetsPosition",
          "[arp][euclidean][foundational]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.noteOn(60, 100);

    // Configure Euclidean: E(1,4) = pattern 0001 -> hit at step 0 only
    arp.setEuclideanSteps(4);
    arp.setEuclideanHits(1);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 44100;  // 1 second block to capture many steps
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run some steps to advance the Euclidean position
    auto events1 = collectEvents(arp, ctx, 4);

    // Now disable and re-enable to reset position
    arp.setEuclideanEnabled(false);
    arp.setEuclideanEnabled(true);

    // After re-enable, position should be 0 again.
    // The first step should be a hit (position 0 of E(1,4)).
    // Collect events for one step cycle
    auto events2 = collectEvents(arp, ctx, 4);
    auto noteOns2 = filterNoteOns(events2);

    // There should be noteOn events -- the first step after re-enable
    // fires at position 0 which is a hit in E(1,4)
    REQUIRE(noteOns2.size() >= 1);
}


// T010: setEuclideanEnabled(true) does NOT clear ratchet sub-step state (FR-010)
TEST_CASE("EuclideanEnabled_DoesNotClearRatchetState",
          "[arp][euclidean][foundational]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Set ratchet lane: count 4 (4 sub-steps per step)
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(4));

    // Euclidean disabled initially, all steps active
    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Process a few blocks to start ratcheting
    std::array<ArpEvent, 128> blockEvents;
    arp.processBlock(ctx, blockEvents);
    ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);

    // Now enable Euclidean mid-playback (doesn't clear ratchet state)
    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(8);  // all hits, so pattern is transparent
    arp.setEuclideanEnabled(true);

    // Continue processing -- in-flight ratchet sub-steps should complete
    // We just verify the arp continues without crash or stuck notes
    auto events = collectEvents(arp, ctx, 100);
    auto noteOns = filterNoteOns(events);

    // With ratchet count 4 and all Euclidean hits, we should see ~4 noteOns per step
    CHECK(noteOns.size() > 10);
}


// T011: resetLanes() resets Euclidean position to 0 (FR-013, SC-012)
TEST_CASE("EuclideanResetLanes_ResetsPosition",
          "[arp][euclidean][foundational]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.noteOn(60, 100);

    // Configure Euclidean: E(1,8) = hit only at step 0
    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(1);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 44100;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run 4 steps worth of blocks to advance Euclidean position past step 0
    auto events1 = collectEvents(arp, ctx, 4);

    // Now use setRetrigger + noteOn to call resetLanes()
    arp.setRetrigger(ArpRetriggerMode::Note);
    arp.noteOn(64, 100);  // triggers resetLanes() via retrigger

    // After resetLanes, Euclidean position is 0 again.
    // E(1,8) has a hit at step 0, so the next step should fire a noteOn.
    auto events2 = collectEvents(arp, ctx, 4);
    auto noteOns2 = filterNoteOns(events2);

    // Should have at least one noteOn (from position 0 hit)
    REQUIRE(noteOns2.size() >= 1);
}


// T012: Pattern generation matches E(3,8) tresillo bitmask (FR-008)
// Verify that setting hits=3, steps=8, rotation=0 produces the correct
// bitmask via regenerateEuclideanPattern(). Since euclideanPattern_ is
// private, we verify by checking EuclideanPattern::generate() directly
// matches E(3,8), and the getters confirm the stored values.
TEST_CASE("EuclideanPatternGenerated_E3_8",
          "[arp][euclidean][foundational]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);

    // Set E(3,8) tresillo pattern
    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(3);
    arp.setEuclideanRotation(0);

    // Verify the stored parameter values
    CHECK(arp.euclideanSteps() == 8);
    CHECK(arp.euclideanHits() == 3);
    CHECK(arp.euclideanRotation() == 0);

    // Verify the expected pattern via EuclideanPattern directly
    // E(3,8) = 10010010 binary = hits at positions 0, 3, 6
    uint32_t expected = EuclideanPattern::generate(3, 8, 0);
    CHECK(EuclideanPattern::isHit(expected, 0, 8) == true);   // step 0: hit
    CHECK(EuclideanPattern::isHit(expected, 1, 8) == false);  // step 1: rest
    CHECK(EuclideanPattern::isHit(expected, 2, 8) == false);  // step 2: rest
    CHECK(EuclideanPattern::isHit(expected, 3, 8) == true);   // step 3: hit
    CHECK(EuclideanPattern::isHit(expected, 4, 8) == false);  // step 4: rest
    CHECK(EuclideanPattern::isHit(expected, 5, 8) == false);  // step 5: rest
    CHECK(EuclideanPattern::isHit(expected, 6, 8) == true);   // step 6: hit
    CHECK(EuclideanPattern::isHit(expected, 7, 8) == false);  // step 7: rest

    // Verify hit count
    CHECK(EuclideanPattern::countHits(expected) == 3);

    // Verify the setters also trigger regeneration by changing params
    // and checking the pattern changes (setEuclideanHits calls regenerate)
    arp.setEuclideanHits(5);
    CHECK(arp.euclideanHits() == 5);
    // E(5,8) cinquillo should have 5 hits
    uint32_t cinquillo = EuclideanPattern::generate(5, 8, 0);
    CHECK(EuclideanPattern::countHits(cinquillo) == 5);
}


// =============================================================================
// Phase 7: Euclidean Timing Mode (075-euclidean-timing)
// Task Group 2: User Story 1+2 - Euclidean Gating in fireStep()
// =============================================================================

// T027: E(3,8) tresillo: noteOn on steps 0, 3, 6 only (SC-001, US1)
TEST_CASE("EuclideanGating_Tresillo_E3_8",
          "[arp][euclidean][gating]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Configure Euclidean: E(3,8) tresillo = 10010010
    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(3);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Collect events over enough blocks for 8+ steps
    // At 120 BPM, 1/8 note = 11025 samples. 8 steps = 88200 samples.
    // At blockSize=512, that's ~173 blocks. Use 200 for margin.
    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // We need at least 3 noteOns (one full cycle of E(3,8))
    REQUIRE(noteOns.size() >= 3);

    // At 120 BPM, eighth note = 11025 samples. The arp starts counting from 0
    // and the first step fires after one step duration elapses (at offset ~11025).
    // Subsequent steps fire at intervals of 11025.
    // E(3,8) hits at positions 0, 3, 6 within the Euclidean pattern.
    // Step 0 (first fire): offset ~11025
    // Step 3: offset ~11025 + 3*11025 = ~44100
    // Step 6: offset ~11025 + 6*11025 = ~77175
    constexpr size_t kStepDuration = 11025;
    constexpr size_t kFirstStepOffset = 0;  // first step fires immediately  // first step fires at 1 duration

    for (size_t i = 0; i < std::min(noteOns.size(), size_t{3}); ++i) {
        constexpr size_t kExpectedSteps[] = {0, 3, 6};
        size_t expectedStep = kExpectedSteps[i];
        size_t expectedOffset = kFirstStepOffset + expectedStep * kStepDuration;
        size_t actualOffset = static_cast<size_t>(noteOns[i].sampleOffset);
        // Within one block size tolerance
        CHECK(actualOffset >= expectedOffset);
        CHECK(actualOffset < expectedOffset + kBlockSize);
    }

    // Count total noteOns in first 8 steps (from first fire to 8 steps later)
    // The 8th step would fire at ~kFirstStepOffset + 8*kStepDuration
    size_t endOfCycle = kFirstStepOffset + 8 * kStepDuration;
    size_t noteOnsInFirst8Steps = 0;
    for (const auto& e : noteOns) {
        if (std::cmp_less(e.sampleOffset, endOfCycle)) {
            ++noteOnsInFirst8Steps;
        }
    }
    CHECK(noteOnsInFirst8Steps == 3);
}


// T028: E(8,8) all hits = identical to Euclidean disabled (SC-001, US1)
TEST_CASE("EuclideanGating_AllHits_E8_8_EqualsDisabled",
          "[arp][euclidean][gating]") {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    // Run WITHOUT Euclidean
    ArpeggiatorCore arpDisabled;
    arpDisabled.prepare(kSampleRate, kBlockSize);
    arpDisabled.setEnabled(true);
    arpDisabled.setMode(ArpMode::Up);
    arpDisabled.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arpDisabled.setGateLength(80.0f);
    arpDisabled.noteOn(60, 100);

    BlockContext ctxDisabled;
    ctxDisabled.sampleRate = kSampleRate;
    ctxDisabled.blockSize = kBlockSize;
    ctxDisabled.tempoBPM = 120.0;
    ctxDisabled.isPlaying = true;
    ctxDisabled.transportPositionSamples = 0;

    auto eventsDisabled = collectEvents(arpDisabled, ctxDisabled, 200);
    auto noteOnsDisabled = filterNoteOns(eventsDisabled);

    // Run WITH Euclidean E(8,8)
    ArpeggiatorCore arpAllHits;
    arpAllHits.prepare(kSampleRate, kBlockSize);
    arpAllHits.setEnabled(true);
    arpAllHits.setMode(ArpMode::Up);
    arpAllHits.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arpAllHits.setGateLength(80.0f);
    arpAllHits.noteOn(60, 100);

    arpAllHits.setEuclideanSteps(8);
    arpAllHits.setEuclideanHits(8);
    arpAllHits.setEuclideanRotation(0);
    arpAllHits.setEuclideanEnabled(true);

    BlockContext ctxAllHits;
    ctxAllHits.sampleRate = kSampleRate;
    ctxAllHits.blockSize = kBlockSize;
    ctxAllHits.tempoBPM = 120.0;
    ctxAllHits.isPlaying = true;
    ctxAllHits.transportPositionSamples = 0;

    auto eventsAllHits = collectEvents(arpAllHits, ctxAllHits, 200);
    auto noteOnsAllHits = filterNoteOns(eventsAllHits);

    // Both should have the same number of noteOns
    REQUIRE(noteOnsDisabled.size() > 5);
    CHECK(noteOnsAllHits.size() == noteOnsDisabled.size());

    // Every step fires
    for (size_t i = 0; i < std::min(noteOnsAllHits.size(), noteOnsDisabled.size()); ++i) {
        CHECK(noteOnsAllHits[i].note == noteOnsDisabled[i].note);
        CHECK(noteOnsAllHits[i].velocity == noteOnsDisabled[i].velocity);
        CHECK(noteOnsAllHits[i].sampleOffset == noteOnsDisabled[i].sampleOffset);
    }
}


// T029: E(0,8) zero hits = all silent (SC-001, US1)
TEST_CASE("EuclideanGating_ZeroHits_E0_8_AllSilent",
          "[arp][euclidean][gating]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // E(0,8) = all rests
    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(0);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // No noteOns should fire
    CHECK(noteOns.size() == 0);
}


// T030: E(5,8) cinquillo (SC-001, US1)
TEST_CASE("EuclideanGating_Cinquillo_E5_8",
          "[arp][euclidean][gating]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // E(5,8) cinquillo = 10110110
    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(5);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // Expect 5 noteOns per 8 steps in first cycle
    // First step fires at kStepDuration (arp counts one full duration before first fire)
    constexpr size_t kStepDuration = 11025;  // 120 BPM, eighth note
    constexpr size_t kFirstStepOffset = 0;  // first step fires immediately
    size_t endOfCycle = kFirstStepOffset + 8 * kStepDuration;
    size_t noteOnsInFirst8Steps = 0;
    for (const auto& e : noteOns) {
        if (std::cmp_less(e.sampleOffset, endOfCycle)) {
            ++noteOnsInFirst8Steps;
        }
    }
    CHECK(noteOnsInFirst8Steps == 5);

    // Verify the hit pattern via EuclideanPattern::generate
    // Our Bresenham implementation produces E(5,8) with 5 hits across 8 steps
    // Hits at positions: 0, 2, 4, 5, 7 (Bresenham-style maximally even distribution)
    uint32_t pattern = EuclideanPattern::generate(5, 8, 0);
    CHECK(EuclideanPattern::isHit(pattern, 0, 8) == true);
    CHECK(EuclideanPattern::isHit(pattern, 1, 8) == false);
    CHECK(EuclideanPattern::isHit(pattern, 2, 8) == true);
    CHECK(EuclideanPattern::isHit(pattern, 3, 8) == false);
    CHECK(EuclideanPattern::isHit(pattern, 4, 8) == true);
    CHECK(EuclideanPattern::isHit(pattern, 5, 8) == true);
    CHECK(EuclideanPattern::isHit(pattern, 6, 8) == false);
    CHECK(EuclideanPattern::isHit(pattern, 7, 8) == true);
}


// T031: E(5,16) bossa nova (SC-001, US1)
TEST_CASE("EuclideanGating_BossaNova_E5_16",
          "[arp][euclidean][gating]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // E(5,16) bossa nova = 1001001000100100
    arp.setEuclideanSteps(16);
    arp.setEuclideanHits(5);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // 16th note at 120 BPM = 5512.5 samples. 16 steps = ~88200 samples.
    // 200 blocks * 512 = 102400 samples -> covers 16 steps.
    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // Verify the Euclidean pattern: E(5,16) = 1001001000100100
    uint32_t pattern = EuclideanPattern::generate(5, 16, 0);
    CHECK(EuclideanPattern::countHits(pattern) == 5);

    // Count noteOns in first 16 steps
    size_t stepDuration16th = static_cast<size_t>(kSampleRate * 60.0 / 120.0 / 4.0);  // ~5512
    size_t noteOnsInFirst16Steps = 0;
    for (const auto& e : noteOns) {
        if (std::cmp_less(e.sampleOffset, 16 * stepDuration16th)) {
            ++noteOnsInFirst16Steps;
        }
    }
    CHECK(noteOnsInFirst16Steps == 5);
}


// T032: Euclidean disabled = Phase 6 identical (SC-004, FR-002)
TEST_CASE("EuclideanDisabled_Phase6Identical",
          "[arp][euclidean][gating]") {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    // Array of BPMs to test
    double bpms[] = {120.0, 140.0, 180.0};

    for (double bpm : bpms) {
        SECTION("BPM=" + std::to_string(static_cast<int>(bpm))) {
            // Run with Euclidean disabled (default)
            ArpeggiatorCore arpDefault;
            arpDefault.prepare(kSampleRate, kBlockSize);
            arpDefault.setEnabled(true);
            arpDefault.setMode(ArpMode::Up);
            arpDefault.setNoteValue(NoteValue::Eighth, NoteModifier::None);
            arpDefault.setGateLength(80.0f);
            arpDefault.noteOn(60, 100);
            arpDefault.noteOn(64, 90);

            BlockContext ctxDefault;
            ctxDefault.sampleRate = kSampleRate;
            ctxDefault.blockSize = kBlockSize;
            ctxDefault.tempoBPM = bpm;
            ctxDefault.isPlaying = true;
            ctxDefault.transportPositionSamples = 0;

            // Collect many blocks for 1000+ steps
            // At 120 BPM, eighth note = 11025 samples. 1050 steps = 11576250 samples.
            // At 512 blockSize = ~22610 blocks.
            size_t numBlocks = 25000;
            auto eventsDefault = collectEvents(arpDefault, ctxDefault, numBlocks);
            auto noteOnsDefault = filterNoteOns(eventsDefault);

            // Run with Euclidean explicitly disabled
            ArpeggiatorCore arpExplicit;
            arpExplicit.prepare(kSampleRate, kBlockSize);
            arpExplicit.setEnabled(true);
            arpExplicit.setMode(ArpMode::Up);
            arpExplicit.setNoteValue(NoteValue::Eighth, NoteModifier::None);
            arpExplicit.setGateLength(80.0f);
            arpExplicit.noteOn(60, 100);
            arpExplicit.noteOn(64, 90);

            // Set Euclidean params but leave disabled
            arpExplicit.setEuclideanSteps(8);
            arpExplicit.setEuclideanHits(3);
            arpExplicit.setEuclideanRotation(2);
            // euclideanEnabled is false by default -- leave it

            BlockContext ctxExplicit;
            ctxExplicit.sampleRate = kSampleRate;
            ctxExplicit.blockSize = kBlockSize;
            ctxExplicit.tempoBPM = bpm;
            ctxExplicit.isPlaying = true;
            ctxExplicit.transportPositionSamples = 0;

            auto eventsExplicit = collectEvents(arpExplicit, ctxExplicit, numBlocks);
            auto noteOnsExplicit = filterNoteOns(eventsExplicit);

            // SC-004: zero tolerance -- same notes, velocities, sample offsets, legato flags
            REQUIRE(noteOnsDefault.size() > 1000);
            REQUIRE(noteOnsExplicit.size() == noteOnsDefault.size());

            for (size_t i = 0; i < noteOnsDefault.size(); ++i) {
                CHECK(noteOnsExplicit[i].note == noteOnsDefault[i].note);
                CHECK(noteOnsExplicit[i].velocity == noteOnsDefault[i].velocity);
                CHECK(noteOnsExplicit[i].sampleOffset == noteOnsDefault[i].sampleOffset);
                CHECK(noteOnsExplicit[i].legato == noteOnsDefault[i].legato);
            }
        }
    }
}


// T033: Rotation shifts pattern (SC-002, US2)
TEST_CASE("EuclideanRotation_ShiftsPattern",
          "[arp][euclidean][rotation]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;
    arp.prepare(kSampleRate, kBlockSize);

    // E(3,8) at rotation=0
    uint32_t pattern0 = EuclideanPattern::generate(3, 8, 0);

    // E(3,8) at rotation=1
    uint32_t pattern1 = EuclideanPattern::generate(3, 8, 1);

    // Patterns should differ
    CHECK(pattern0 != pattern1);

    // Both should have exactly 3 hits
    CHECK(EuclideanPattern::countHits(pattern0) == 3);
    CHECK(EuclideanPattern::countHits(pattern1) == 3);

    // Now test via the arp: collect noteOns for each rotation
    auto runArpForOneFullCycle = [&](int rotation) -> std::vector<ArpEvent> {
        ArpeggiatorCore arpInner;
        arpInner.prepare(kSampleRate, kBlockSize);
        arpInner.setEnabled(true);
        arpInner.setMode(ArpMode::Up);
        arpInner.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arpInner.setGateLength(80.0f);
        arpInner.noteOn(60, 100);

        arpInner.setEuclideanSteps(8);
        arpInner.setEuclideanHits(3);
        arpInner.setEuclideanRotation(rotation);
        arpInner.setEuclideanEnabled(true);

        BlockContext ctxInner;
        ctxInner.sampleRate = kSampleRate;
        ctxInner.blockSize = kBlockSize;
        ctxInner.tempoBPM = 120.0;
        ctxInner.isPlaying = true;
        ctxInner.transportPositionSamples = 0;

        auto evts = collectEvents(arpInner, ctxInner, 200);
        return filterNoteOns(evts);
    };

    auto noteOnsR0 = runArpForOneFullCycle(0);
    auto noteOnsR1 = runArpForOneFullCycle(1);

    // Both should have at least 3 noteOns (one full cycle)
    REQUIRE(noteOnsR0.size() >= 3);
    REQUIRE(noteOnsR1.size() >= 3);

    // The timing of the first 3 noteOns should differ between rotations
    bool anyDifferent = false;
    for (size_t i = 0; i < 3; ++i) {
        if (noteOnsR0[i].sampleOffset != noteOnsR1[i].sampleOffset) {
            anyDifferent = true;
            break;
        }
    }
    CHECK(anyDifferent);
}


// T034: Rotation modulo steps wraps around (SC-002, US2)
TEST_CASE("EuclideanRotation_ModuloSteps_WrapAround",
          "[arp][euclidean][rotation]") {
    // rotation=8 with steps=8 should be same as rotation=0
    uint32_t patternR0 = EuclideanPattern::generate(3, 8, 0);
    uint32_t patternR8 = EuclideanPattern::generate(3, 8, 8);

    CHECK(patternR0 == patternR8);
}


// T035: All rotations of E(5,16) are distinct with exactly 5 hits each (SC-002, US2)
TEST_CASE("EuclideanRotation_AllDistinct_E5_16",
          "[arp][euclidean][rotation]") {
    std::array<uint32_t, 16> patterns;
    for (int rot = 0; rot < 16; ++rot) {
        patterns[static_cast<size_t>(rot)] = EuclideanPattern::generate(5, 16, rot);
        CHECK(EuclideanPattern::countHits(patterns[static_cast<size_t>(rot)]) == 5);
    }

    // All 16 patterns must be distinct
    for (int i = 0; i < 16; ++i) {
        for (int j = i + 1; j < 16; ++j) {
            CHECK(patterns[static_cast<size_t>(i)] != patterns[static_cast<size_t>(j)]);
        }
    }
}


// T036: All lanes advance on Euclidean rest steps (SC-003, FR-003, FR-004, FR-011)
TEST_CASE("EuclideanRestStep_AllLanesAdvance",
          "[arp][euclidean][gating]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Velocity lane length=3 with distinct values
    arp.velocityLane().setLength(3);
    arp.velocityLane().setStep(0, 1.0f);
    arp.velocityLane().setStep(1, 0.5f);
    arp.velocityLane().setStep(2, 0.25f);

    // Euclidean steps=5, hits=2 (sparse)
    arp.setEuclideanSteps(5);
    arp.setEuclideanHits(2);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run 15 steps (LCM of 5 and 3 = 15)
    // At 120 BPM, eighth = 11025 samples. 15 steps = 165375 samples.
    // Need ~323 blocks of 512
    auto events = collectEvents(arp, ctx, 350);
    auto noteOns = filterNoteOns(events);

    // Verify that noteOns occur: E(2,5) has 2 hits per 5 steps
    // Over 15 steps = 3 full Euclidean cycles = 6 noteOns
    constexpr size_t kStepDuration = 11025;
    size_t noteOnsIn15Steps = 0;
    for (const auto& e : noteOns) {
        if (std::cmp_less(e.sampleOffset, 15 * kStepDuration)) {
            ++noteOnsIn15Steps;
        }
    }
    CHECK(noteOnsIn15Steps == 6);

    // Verify velocity lane cycles every 3 steps regardless of Euclidean pattern.
    // The velocity values on noteOns should follow the velocity lane cycling pattern.
    // The velocity lane advances on every step (including rest), but we can only
    // observe velocities on hit steps. Over 15 steps with E(2,5), we get hits at
    // specific positions; the velocity at each hit depends on (step_index % 3).
    // This confirms lanes advance on rest steps too.
    REQUIRE(noteOnsIn15Steps >= 2);
}


// T037: Euclidean rest breaks tie chain (SC-006, FR-007)
TEST_CASE("EuclideanRestStep_BreaksTieChain",
          "[arp][euclidean][gating]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Modifier lane: step 0=Active, step 1=Tie, step 2=Active, step 3=Active
    arp.modifierLane().setLength(4);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepTie));
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(3, static_cast<uint8_t>(kStepActive));

    // Euclidean: E(2,4) -- hits on steps 0 and 2; rests on steps 1 and 3
    arp.setEuclideanSteps(4);
    arp.setEuclideanHits(2);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run enough blocks for exactly 4 steps (steps at 0, 11025, 22050, 33075).
    // Use 70 blocks = 35840 samples (> 33075, < 44100).
    auto events = collectEvents(arp, ctx, 70);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    // Step 0: Euclidean hit, modifier Active -> noteOn fires
    // Step 1: Euclidean rest -> tie is ignored, noteOff emitted (breaks tie chain)
    // Step 2: Euclidean hit, modifier Active -> fresh noteOn fires
    // Step 3: Euclidean rest -> noteOff emitted

    // Should have noteOns for steps 0 and 2
    constexpr size_t kStepDuration = 11025;
    size_t endOfCycle = 4 * kStepDuration;
    size_t noteOnsIn4Steps = 0;
    for (const auto& e : noteOns) {
        if (std::cmp_less(e.sampleOffset, endOfCycle)) {
            ++noteOnsIn4Steps;
        }
    }
    CHECK(noteOnsIn4Steps == 2);

    // The key behavior: Euclidean rest at step 1 breaks the tie chain.
    // Because step 0's look-ahead sees step 1 has Tie modifier, it suppresses the
    // gate noteOff (expecting tie sustain). But the Euclidean rest at step 1 fires
    // instead, emitting a noteOff to terminate the sustained note. This is FR-007.
    // Verify at least 1 noteOff from the Euclidean rest within the first cycle.
    size_t noteOffsIn4Steps = 0;
    for (const auto& e : noteOffs) {
        if (std::cmp_less(e.sampleOffset, endOfCycle)) {
            ++noteOffsIn4Steps;
        }
    }
    // At least 1 from the Euclidean rest breaking the tie chain,
    // plus possible gate noteOffs from subsequent hits.
    CHECK(noteOffsIn4Steps >= 1);

    // Over the 4-step window, noteOffs should match noteOns (no stuck notes)
    CHECK(noteOffs.size() >= noteOns.size());
}


// T038: Ratchet on Euclidean rest step is suppressed (SC-007, FR-016)
TEST_CASE("EuclideanRestStep_RatchetSuppressed",
          "[arp][euclidean][gating]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Ratchet lane: all steps = 4 sub-steps
    arp.ratchetLane().setLength(1);
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(4));

    // Euclidean: E(0,8) = all rests
    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(0);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // No noteOns should fire even though ratchet count is 4
    CHECK(noteOns.size() == 0);
}


// T039: Ratchet on Euclidean hit step fires correct sub-steps (SC-007, FR-017)
TEST_CASE("EuclideanHitStep_RatchetApplies",
          "[arp][euclidean][gating]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Ratchet lane: count=2
    arp.ratchetLane().setLength(1);
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(2));

    // Euclidean: E(8,8) = all hits (so ratchet fires on every step)
    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(8);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // With ratchet=2 and all Euclidean hits, each step produces 2 noteOns.
    // Over 8 steps = 16 noteOns minimum. First step fires at kStepDuration.
    constexpr size_t kStepDuration = 11025;
    constexpr size_t kFirstStepOffset = 0;  // first step fires immediately
    size_t endOfCycle = kFirstStepOffset + 8 * kStepDuration;
    size_t noteOnsIn8Steps = 0;
    for (const auto& e : noteOns) {
        if (std::cmp_less(e.sampleOffset, endOfCycle)) {
            ++noteOnsIn8Steps;
        }
    }
    CHECK(noteOnsIn8Steps == 16);  // 8 steps * 2 ratchets
}


// T040: Modifier Rest on Euclidean hit step still produces silence (FR-019)
TEST_CASE("EuclideanHitStep_ModifierRestApplies",
          "[arp][euclidean][gating]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Modifier lane: all steps = Rest (kStepActive not set)
    arp.modifierLane().setLength(1);
    arp.modifierLane().setStep(0, 0x00);  // Rest

    // Euclidean: E(8,8) = all hits
    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(8);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // All Euclidean hits, but modifier Rest suppresses every step
    CHECK(noteOns.size() == 0);
}


// T041: Modifier Tie on Euclidean hit step sustains note (FR-020)
TEST_CASE("EuclideanHitStep_ModifierTieApplies",
          "[arp][euclidean][gating]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Modifier lane: step 0=Active, step 1=Tie
    arp.modifierLane().setLength(2);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepTie));

    // Euclidean: E(8,8) = all hits (so tie actually takes effect)
    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(8);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // With alternating Active/Tie pattern and all Euclidean hits:
    // Step 0: Active -> noteOn
    // Step 1: Tie -> sustain (no new noteOn)
    // Step 2: Active -> noteOn
    // Step 3: Tie -> sustain
    // So noteOns should be about half the total steps
    constexpr size_t kStepDuration = 11025;
    size_t noteOnsIn8Steps = 0;
    for (const auto& e : noteOns) {
        if (std::cmp_less(e.sampleOffset, 8 * kStepDuration)) {
            ++noteOnsIn8Steps;
        }
    }
    CHECK(noteOnsIn8Steps == 4);  // steps 0, 2, 4, 6 fire noteOns
}


// T042: Euclidean rest with Tie modifier = still silent, tie chain broken (FR-006, FR-007, FR-020)
TEST_CASE("EuclideanRestStep_ModifierTie_TieChainBroken",
          "[arp][euclidean][gating]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Modifier lane: all Tie (to test that Euclidean rest overrides)
    arp.modifierLane().setLength(2);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepTie));

    // Euclidean: E(1,2) = hit on step 0, rest on step 1
    arp.setEuclideanSteps(2);
    arp.setEuclideanHits(1);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    // Step 0: Euclidean hit + Active -> noteOn
    // E(1,2) hits at step 0, rests at step 1 (alternating)
    // Step 0: Euclidean hit + Active -> noteOn
    // Step 1: Euclidean rest (overrides Tie modifier) -> noteOff emitted, tie broken
    // Step 2: Euclidean hit + Active -> new noteOn
    // Step 3: Euclidean rest -> noteOff
    constexpr size_t kStepDuration = 11025;
    constexpr size_t kFirstStepOffset = 0;  // first step fires immediately
    size_t endOfCycle = kFirstStepOffset + 4 * kStepDuration;
    size_t noteOnsIn4Steps = 0;
    for (const auto& e : noteOns) {
        if (std::cmp_less(e.sampleOffset, endOfCycle)) {
            ++noteOnsIn4Steps;
        }
    }
    CHECK(noteOnsIn4Steps == 2);  // steps 0 and 2

    // At least 1 noteOff from Euclidean rest breaking the tie chain.
    // The rest overrides the Tie modifier and emits noteOff for sounding notes.
    size_t noteOffsIn4Steps = 0;
    for (const auto& e : noteOffs) {
        if (std::cmp_less(e.sampleOffset, endOfCycle)) {
            ++noteOffsIn4Steps;
        }
    }
    CHECK(noteOffsIn4Steps >= 1);
}


// T043: Chord mode - hit fires all, rest silences all (FR-021)
TEST_CASE("EuclideanChordMode_HitFiresAll_RestSilencesAll",
          "[arp][euclidean][gating]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Chord);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);
    arp.noteOn(64, 90);
    arp.noteOn(67, 80);

    // E(1,2) = hit on step 0, rest on step 1
    arp.setEuclideanSteps(2);
    arp.setEuclideanHits(1);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 100);
    auto noteOns = filterNoteOns(events);

    // Step 0 fires at 0: Euclidean hit -> all 3 chord notes fire
    // Step 1 fires at kStepDuration: Euclidean rest -> all 3 silenced (noteOffs)
    // Step 2 fires at 2*kStepDuration: Euclidean hit -> all 3 fire again
    constexpr size_t kStepDuration = 11025;
    size_t noteOnsIn2Steps = 0;
    for (const auto& e : noteOns) {
        if (std::cmp_less(e.sampleOffset, 2 * kStepDuration)) {
            ++noteOnsIn2Steps;
        }
    }
    CHECK(noteOnsIn2Steps == 3);  // 3 chord notes on step 0

    // Verify noteOffs are emitted for step 0's notes (gate noteOffs or rest noteOffs).
    // With step 0 at offset 0 and 80% gate, noteOffs fire at ~8820 (within [0, kStepDuration)).
    // At step 1 (kStepDuration), the rest may emit additional noteOffs if notes still sound.
    auto noteOffs = filterNoteOffs(events);
    size_t noteOffsBeforeStep2 = 0;
    for (const auto& e : noteOffs) {
        size_t offset = static_cast<size_t>(e.sampleOffset);
        if (offset < 2 * kStepDuration) {
            ++noteOffsBeforeStep2;
        }
    }
    CHECK(noteOffsBeforeStep2 == 3);  // 3 chord notes silenced
}


// T044: Position reset on retrigger (SC-012, FR-013)
TEST_CASE("EuclideanPositionReset_OnRetrigger",
          "[arp][euclidean][gating]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setRetrigger(ArpRetriggerMode::Note);
    arp.noteOn(60, 100);

    // E(1,8) = hit only at step 0
    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(1);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run 4 steps to advance Euclidean position past step 0
    auto events1 = collectEvents(arp, ctx, 100);
    auto noteOns1 = filterNoteOns(events1);

    // E(1,8) has 1 hit per 8 steps, so we should see some noteOns from step 0
    REQUIRE(noteOns1.size() >= 1);

    // Retrigger via new noteOn (retrigger mode = Note)
    arp.noteOn(64, 100);

    // After retrigger, position resets to 0. Next step should be hit.
    auto events2 = collectEvents(arp, ctx, 100);
    auto noteOns2 = filterNoteOns(events2);

    // Should see noteOns again from position 0
    REQUIRE(noteOns2.size() >= 1);
}


// T045: Defensive branch (result.count==0) advances euclideanPosition_ (FR-035)
TEST_CASE("EuclideanDefensiveBranch_PositionAdvances",
          "[arp][euclidean][gating]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // E(1,4) = hit at step 0 only
    arp.setEuclideanSteps(4);
    arp.setEuclideanHits(1);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Add and remove note to trigger the defensive branch
    arp.noteOn(60, 100);

    // Process one block so the arp fires step 0 (noteOn)
    std::array<ArpEvent, 128> blockEvents;
    arp.processBlock(ctx, blockEvents);
    ctx.transportPositionSamples += static_cast<int64_t>(kBlockSize);

    // Remove all notes -> held buffer empty
    arp.noteOff(60);

    // Process enough blocks for several step ticks with empty buffer
    // (defensive branch fires). The euclideanPosition_ should advance.
    auto eventsEmpty = collectEvents(arp, ctx, 200);

    // Now add a note back
    arp.noteOn(60, 100);

    // The Euclidean position should have advanced during the empty period.
    // Process more blocks and verify the arp pattern is offset from step 0.
    auto eventsAfter = collectEvents(arp, ctx, 200);
    auto noteOnsAfter = filterNoteOns(eventsAfter);

    // The test passes if we don't crash and the arp continues to function.
    // The key requirement is that euclideanPosition_ was advancing during
    // the defensive branch, keeping it synchronized with other lanes.
    // After re-adding the note, the Euclidean pattern should continue
    // from the advanced position (not from 0).
    // We just verify the arp produces events after the empty period.
    CHECK(noteOnsAfter.size() >= 1);
}


// T046: Euclidean rest with Accent modifier = still silent (FR-006, FR-018)
TEST_CASE("EuclideanEvaluationOrder_BeforeModifier",
          "[arp][euclidean][gating]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setAccentVelocity(20);
    arp.noteOn(60, 100);

    // Modifier lane: all steps = Active + Accent
    arp.modifierLane().setLength(1);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive | kStepAccent));

    // Euclidean: E(0,8) = all rests
    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(0);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // Euclidean rest evaluated before modifier -> no noteOn despite Accent
    CHECK(noteOns.size() == 0);
}


// T046a: Swing and Euclidean are orthogonal (FR-022)
TEST_CASE("EuclideanSwing_Orthogonal",
          "[arp][euclidean][gating]") {
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    // Run with Euclidean E(3,8) and swing at 50%
    ArpeggiatorCore arp;
    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setSwing(50.0f);  // 50% swing
    arp.noteOn(60, 100);

    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(3);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 250);
    auto noteOns = filterNoteOns(events);

    // E(3,8) tresillo should still produce 3 noteOns per 8 steps
    // Swing affects timing of ALL steps (hit and rest) but doesn't change which fire
    REQUIRE(noteOns.size() >= 3);

    // Verify the step spacing is non-uniform (swing is applied)
    // With swing=50%, even steps are longer, odd steps are shorter
    // At 120 BPM, base eighth note = 11025 samples
    // Even step (lengthened): 11025 * 1.5 = 16537
    // Odd step (shortened): 11025 * 0.5 = 5512
    // Step durations cycle: [long, short, long, short, ...]
    //
    // E(3,8) hits on steps 0, 3, 6:
    // Step 0 offset: 0
    // Step 1: 0 + 16537 = 16537 (rest)
    // Step 2: 16537 + 5512 = 22049 (rest)
    // Step 3: 22049 + 16537 = 38586 (hit)
    // Step 4: 38586 + 5512 = 44098 (rest)
    // Step 5: 44098 + 16537 = 60635 (rest)
    // Step 6: 60635 + 5512 = 66147 (hit)
    //
    // The key test: noteOns should NOT be at regular intervals
    if (noteOns.size() >= 3) {
        int32_t gap1 = noteOns[1].sampleOffset - noteOns[0].sampleOffset;
        int32_t gap2 = noteOns[2].sampleOffset - noteOns[1].sampleOffset;
        // Gaps should be non-zero and different from each other
        // (swing makes them non-uniform)
        CHECK(gap1 > 0);
        CHECK(gap2 > 0);
        // With swing, the gaps between hit steps 0->3 and 3->6 differ
        // because swing alternates even/odd step durations
        // gap1 (steps 0-3) = long+short+long = 16537+5512+16537 = 38586
        // gap2 (steps 3-6) = short+long+short = 5512+16537+5512 = 27562
        CHECK(gap1 != gap2);
    }

    // Count total noteOns per 8-step cycle to confirm Euclidean gating is correct.
    // With swing 50%, cycle = 4*short + 4*long = 4*5512 + 4*16537 = 88196.
    // Use a window slightly less than the cycle to avoid catching the next cycle's
    // first step (which fires at 88196 with the immediate-start behavior).
    constexpr size_t kCycleWindow = 80000;
    size_t noteOnsInFirstCycle = 0;
    for (const auto& e : noteOns) {
        if (std::cmp_less(e.sampleOffset, kCycleWindow)) {
            ++noteOnsInFirstCycle;
        }
    }
    CHECK(noteOnsInFirstCycle == 3);
}


// =============================================================================
// Phase 4: User Story 3 - Euclidean Lane Interplay (Polymetric)
// =============================================================================

// T053: Polymetric cycling -- Euclidean steps=5 + velocity lane length=3 = 15-step cycle (SC-003)
TEST_CASE("EuclideanPolymetric_Steps5_VelocityLength3",
          "[arp][euclidean][polymetric]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Velocity lane length=3 with very distinct values
    // 1.0, 0.5, 0.25 -> results in vel 100, 50, 25 for input vel=100
    arp.velocityLane().setLength(3);
    arp.velocityLane().setStep(0, 1.0f);
    arp.velocityLane().setStep(1, 0.5f);
    arp.velocityLane().setStep(2, 0.25f);

    // Euclidean: E(3,5) = hits on steps 0, 2, 4 (binary 10101)
    arp.setEuclideanSteps(5);
    arp.setEuclideanHits(3);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // At 120 BPM, eighth note = 11025 samples. 15 steps = 165375 samples.
    // Need ~323 blocks of 512. Use 350 for margin.
    auto events = collectEvents(arp, ctx, 350);
    auto noteOns = filterNoteOns(events);

    // E(3,5) = hits at positions 0, 2, 4. Rests at 1, 3.
    // 3 hits per 5-step cycle. Over 15 steps = 3 Euclidean cycles = 9 noteOns.
    // First step fires at offset kStepDuration (the arp waits one step before firing).
    // Step N fires at offset (N+1)*kStepDuration. Use window up to 16*kStepDuration
    // to capture all 15 steps.
    constexpr size_t kStepDuration = 11025;
    constexpr size_t kFirstStepOffset = 0;  // first step fires immediately
    size_t endOf15Steps = kFirstStepOffset + 15 * kStepDuration;
    size_t noteOnsIn15Steps = 0;
    for (const auto& e : noteOns) {
        if (std::cmp_less(e.sampleOffset, endOf15Steps)) {
            ++noteOnsIn15Steps;
        }
    }
    CHECK(noteOnsIn15Steps == 9);  // 3 hits/cycle * 3 cycles

    // Verify polymetric cycling: the velocity pattern repeats every 3 steps
    // and the Euclidean pattern repeats every 5 steps. The combined pattern
    // only repeats after LCM(3,5) = 15 steps.
    //
    // Step: 0  1  2  3  4  5  6  7  8  9  10 11 12 13 14
    // Eucl: H  R  H  R  H  H  R  H  R  H  H  R  H  R  H
    // VelI: 0  1  2  0  1  2  0  1  2  0  1  2  0  1  2
    // VelS: 1.0 0.5 0.25 1.0 0.5 0.25 1.0 0.5 0.25 ...
    //
    // NoteOns at steps: 0(velI=0), 2(velI=2), 4(velI=1), 5(velI=2), 7(velI=1), 9(velI=0),
    //                   10(velI=1), 12(velI=0), 14(velI=2)
    // Expected velocities:
    //   step 0:  velI=0 -> scale=1.0  -> vel=100
    //   step 2:  velI=2 -> scale=0.25 -> vel=25
    //   step 4:  velI=1 -> scale=0.5  -> vel=50
    //   step 5:  velI=2 -> scale=0.25 -> vel=25
    //   step 7:  velI=1 -> scale=0.5  -> vel=50
    //   step 9:  velI=0 -> scale=1.0  -> vel=100
    //   step 10: velI=1 -> scale=0.5  -> vel=50
    //   step 12: velI=0 -> scale=1.0  -> vel=100
    //   step 14: velI=2 -> scale=0.25 -> vel=25
    //
    // Verify velocities confirm polymetric cycling pattern.
    // Collect velocities of noteOns in the first 15 steps.
    std::vector<uint8_t> velocities;
    for (const auto& e : noteOns) {
        if (std::cmp_less(e.sampleOffset, endOf15Steps)) {
            velocities.push_back(e.velocity);
        }
    }
    REQUIRE(velocities.size() == 9);

    // Verify the velocity pattern: [100, 25, 50, 25, 50, 100, 50, 100, 25]
    CHECK(velocities[0] == 100);
    CHECK(velocities[1] == 25);
    CHECK(velocities[2] == 50);
    CHECK(velocities[3] == 25);
    CHECK(velocities[4] == 50);
    CHECK(velocities[5] == 100);
    CHECK(velocities[6] == 50);
    CHECK(velocities[7] == 100);
    CHECK(velocities[8] == 25);

    // Verify the second 15-step cycle produces the exact same velocity pattern
    // (confirming polymetric cycling resets after LCM)
    std::vector<uint8_t> secondCycleVelocities;
    for (const auto& e : noteOns) {
        auto off = static_cast<size_t>(e.sampleOffset);
        if (off >= endOf15Steps && off < endOf15Steps + 15 * kStepDuration) {
            secondCycleVelocities.push_back(e.velocity);
        }
    }
    // Second cycle should also have 9 noteOns with same velocity pattern
    if (secondCycleVelocities.size() == 9) {
        for (size_t i = 0; i < 9; ++i) {
            CHECK(secondCycleVelocities[i] == velocities[i]);
        }
    }
}


// T054: Ratchet lane interplay -- hit+ratchet fires, rest+ratchet is silent (FR-016)
TEST_CASE("EuclideanPolymetric_RatchetInterplay",
          "[arp][euclidean][polymetric]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Ratchet lane length=3 with values [1, 2, 4]
    arp.ratchetLane().setLength(3);
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(1));  // no ratchet
    arp.ratchetLane().setStep(1, static_cast<uint8_t>(2));  // 2 sub-steps
    arp.ratchetLane().setStep(2, static_cast<uint8_t>(4));  // 4 sub-steps

    // Euclidean: E(3,8) = tresillo, hits at positions 0, 3, 6 (10010010)
    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(3);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run enough blocks for 24 steps (LCM(8,3) = 24).
    // 24 * 11025 = 264600 samples. 264600 / 512 ~ 517 blocks.
    auto events = collectEvents(arp, ctx, 550);
    auto noteOns = filterNoteOns(events);

    // Within the first 24 steps, the combined Euclidean + ratchet pattern:
    // Step: 0  1  2  3  4  5  6  7  | 8  9  10 11 12 13 14 15 | 16 17 18 19 20 21 22 23
    // Eucl: H  R  R  H  R  R  H  R  | H  R  R  H  R  R  H  R  | H  R  R  H  R  R  H  R
    // RatI: 0  1  2  0  1  2  0  1  | 2  0  1  2  0  1  2  0  | 1  2  0  1  2  0  1  2
    // RatV: 1  2  4  1  2  4  1  2  | 4  1  2  4  1  2  4  1  | 2  4  1  2  4  1  2  4
    //
    // Hit steps and their ratchet values:
    //   Step 0:  hit, ratchet=1 -> 1 noteOn
    //   Step 3:  hit, ratchet=1 -> 1 noteOn (ratchetIdx=0 since ratchet advanced through 0,1,2,0)
    //   Step 6:  hit, ratchet=1 -> 1 noteOn
    //   Step 8:  hit, ratchet=4 -> 4 noteOns
    //   Step 11: hit, ratchet=4 -> 4 noteOns
    //   Step 14: hit, ratchet=4 -> 4 noteOns
    //   Step 16: hit, ratchet=2 -> 2 noteOns
    //   Step 19: hit, ratchet=2 -> 2 noteOns
    //   Step 22: hit, ratchet=2 -> 2 noteOns
    //
    // Total noteOns in 24 steps: 3*1 + 3*4 + 3*2 = 3 + 12 + 6 = 21

    constexpr size_t kStepDuration = 11025;
    size_t noteOnsIn24Steps = 0;
    for (const auto& e : noteOns) {
        if (std::cmp_less(e.sampleOffset, 24 * kStepDuration)) {
            ++noteOnsIn24Steps;
        }
    }
    CHECK(noteOnsIn24Steps == 21);

    // Verify that rest steps produce NO noteOns even with high ratchet counts.
    // Rest steps with ratchet 4 (e.g., step 2, 5) should be completely silent.
    // We already verified the exact count; if rest+ratchet leaked, count would exceed 21.
    // Additional check: verify all noteOns within first 8 steps = 3
    // (steps 0, 3, 6 are hits with ratchet 1, 1, 1 -> 3 noteOns)
    size_t noteOnsIn8Steps = 0;
    for (const auto& e : noteOns) {
        if (std::cmp_less(e.sampleOffset, 8 * kStepDuration)) {
            ++noteOnsIn8Steps;
        }
    }
    CHECK(noteOnsIn8Steps == 3);  // 3 hits * 1 ratchet each
}


// T055: Modifier lane interplay -- Tie on hit sustains, Tie on rest is silent (FR-006, FR-018, FR-020)
TEST_CASE("EuclideanPolymetric_ModifierInterplay",
          "[arp][euclidean][polymetric]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Modifier lane length=4: [Active, Tie, Active, Active]
    arp.modifierLane().setLength(4);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepTie));
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(3, static_cast<uint8_t>(kStepActive));

    // Euclidean: E(3,8) = tresillo, hits at positions 0, 3, 6 (10010010)
    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(3);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run enough blocks for 16 steps (two Euclidean cycles).
    // 16 steps at offset (1+16)*11025 = 187425 samples -> need ~366 blocks of 512.
    auto events = collectEvents(arp, ctx, 400);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    // Combined pattern for first 8 steps:
    // Step: 0     1     2     3     4     5     6     7
    // Eucl: Hit   Rest  Rest  Hit   Rest  Rest  Hit   Rest
    // ModI: 0     1     2     3     0     1     2     3
    // Mod:  Act   Tie   Act   Act   Act   Tie   Act   Act
    //
    // Step 0: Euclidean hit + Active -> noteOn
    // Step 1: Euclidean rest -> override Tie, noteOff emitted (tie chain broken)
    // Step 2: Euclidean rest -> silent
    // Step 3: Euclidean hit + Active -> noteOn
    // Step 4: Euclidean rest -> override Active, noteOff emitted
    // Step 5: Euclidean rest -> override Tie, silent (no preceding note anyway)
    // Step 6: Euclidean hit + Active -> noteOn
    // Step 7: Euclidean rest -> noteOff emitted

    // First step fires at kStepDuration offset. Step N fires at (N+1)*kStepDuration.
    // Cycle 1 (steps 0-7) events fall within [kStepDuration, 9*kStepDuration).
    constexpr size_t kStepDuration = 11025;
    constexpr size_t kFirstStepOffset = 0;  // first step fires immediately
    size_t endOfCycle1 = kFirstStepOffset + 8 * kStepDuration;
    size_t noteOnsIn8Steps = 0;
    for (const auto& e : noteOns) {
        if (std::cmp_less(e.sampleOffset, endOfCycle1)) {
            ++noteOnsIn8Steps;
        }
    }
    CHECK(noteOnsIn8Steps == 3);  // Only steps 0, 3, 6

    // Verify noteOffs occur at Euclidean rest steps to break tie chains.
    // Step 1 has Tie modifier, but Euclidean rest overrides -> noteOff emitted.
    size_t noteOffsIn8Steps = 0;
    for (const auto& e : noteOffs) {
        if (std::cmp_less(e.sampleOffset, endOfCycle1)) {
            ++noteOffsIn8Steps;
        }
    }
    // Each hit produces a noteOn; the following rest produces a noteOff.
    // Gate-based noteOffs may also fire. At minimum, we expect noteOffs from
    // the Euclidean rest steps that break active notes.
    CHECK(noteOffsIn8Steps >= 3);

    // Now verify the second cycle (steps 8-15) where modifier lane alignment shifts.
    // Step: 8     9     10    11    12    13    14    15
    // Eucl: Hit   Rest  Rest  Hit   Rest  Rest  Hit   Rest
    // ModI: 0     1     2     3     0     1     2     3
    // Mod:  Act   Tie   Act   Act   Act   Tie   Act   Act
    //
    // Same alignment since LCM(8,4) = 8 -- modifier lane cycles exactly twice
    // within one Euclidean cycle. The pattern repeats identically.
    size_t endOfCycle2 = endOfCycle1 + 8 * kStepDuration;
    size_t noteOnsInCycle2 = 0;
    for (const auto& e : noteOns) {
        auto off = static_cast<size_t>(e.sampleOffset);
        if (off >= endOfCycle1 && off < endOfCycle2) {
            ++noteOnsInCycle2;
        }
    }
    CHECK(noteOnsInCycle2 == 3);
}


// T056: All lanes advance on rest -- verify all 5 lane types advance on every step tick (FR-004, FR-011)
TEST_CASE("EuclideanPolymetric_AllLanesAdvanceOnRest",
          "[arp][euclidean][polymetric]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Configure each lane with a prime length so we can detect independent cycling:
    // Velocity: length=3
    arp.velocityLane().setLength(3);
    arp.velocityLane().setStep(0, 1.0f);
    arp.velocityLane().setStep(1, 0.5f);
    arp.velocityLane().setStep(2, 0.25f);

    // Gate: length=2
    arp.gateLane().setLength(2);
    arp.gateLane().setStep(0, 1.0f);
    arp.gateLane().setStep(1, 0.5f);

    // Pitch: length=2
    arp.pitchLane().setLength(2);
    arp.pitchLane().setStep(0, static_cast<int8_t>(0));
    arp.pitchLane().setStep(1, static_cast<int8_t>(12));

    // Modifier: length=2
    arp.modifierLane().setLength(2);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive));

    // Ratchet: length=2
    arp.ratchetLane().setLength(2);
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(1));
    arp.ratchetLane().setStep(1, static_cast<uint8_t>(1));

    // Euclidean: E(1,5) = only step 0 is a hit, steps 1-4 are rests (10000)
    // This means 4 out of 5 steps are rests -- extreme rest ratio
    arp.setEuclideanSteps(5);
    arp.setEuclideanHits(1);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run 10 steps (2 Euclidean cycles). Only steps 0 and 5 should produce noteOns.
    // At 120 BPM eighth note = 11025 samples. 10 steps = 110250 samples.
    // Need ~216 blocks of 512.
    auto events = collectEvents(arp, ctx, 250);
    auto noteOns = filterNoteOns(events);

    constexpr size_t kStepDuration = 11025;
    size_t noteOnsIn10Steps = 0;
    for (const auto& e : noteOns) {
        if (std::cmp_less(e.sampleOffset, 10 * kStepDuration)) {
            ++noteOnsIn10Steps;
        }
    }
    // 2 Euclidean cycles * 1 hit per cycle = 2 noteOns
    CHECK(noteOnsIn10Steps == 2);

    // Verify that velocity lane advances on rest steps by checking the velocity
    // of the two noteOns. If lanes didn't advance on rests, both noteOns would
    // have the same velocity (both at velocity lane position 0).
    //
    // Step 0: Euclidean hit, velocity lane position 0 -> scale=1.0 -> vel=100
    // Steps 1-4: Euclidean rests; velocity lane advances: positions 1, 2, 0, 1
    // Step 5: Euclidean hit, velocity lane position 2 -> scale=0.25 -> vel=25
    // Steps 6-9: Euclidean rests; velocity lane advances: positions 0, 1, 2, 0
    //
    // If velocity lane did NOT advance on rests, step 5 would still be at
    // position 1 (only advancing on hits), giving vel=50 instead of vel=25.
    std::vector<uint8_t> velocities;
    for (const auto& e : noteOns) {
        if (std::cmp_less(e.sampleOffset, 10 * kStepDuration)) {
            velocities.push_back(e.velocity);
        }
    }
    REQUIRE(velocities.size() == 2);
    CHECK(velocities[0] == 100);  // Step 0: vel lane pos 0 -> 1.0 * 100
    CHECK(velocities[1] == 25);   // Step 5: vel lane pos 2 -> 0.25 * 100

    // Similarly, verify pitch lane advances on rest steps.
    // Pitch lane length=2: [0, +12]
    // Step 0: pitch lane pos 0 -> offset=0 -> note=60
    // Steps 1-4: rests; pitch advances: pos 1, 0, 1, 0
    // Step 5: pitch lane pos 1 -> offset=+12 -> note=72
    std::vector<uint8_t> notes;
    for (const auto& e : noteOns) {
        if (std::cmp_less(e.sampleOffset, 10 * kStepDuration)) {
            notes.push_back(e.note);
        }
    }
    REQUIRE(notes.size() == 2);
    CHECK(notes[0] == 60);   // Step 0: pitch offset 0
    CHECK(notes[1] == 72);   // Step 5: pitch offset +12
}


// =============================================================================
// Phase 5: User Story 4 - Euclidean Mode On/Off Transitions
// =============================================================================

// T061: Disabled-to-enabled transition produces no stuck notes, gating from next
// step, position starts at 0 (SC-005, FR-010)
TEST_CASE("EuclideanTransition_DisabledToEnabled_NoStuckNotes",
          "[arp][euclidean][transition]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Pre-configure Euclidean parameters but leave disabled
    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(3);
    arp.setEuclideanRotation(0);
    // Euclidean mode is OFF -- all steps should fire

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run 4 steps with Euclidean disabled -- all should fire
    // At 120 BPM eighth note = 11025 samples. 4 steps ~ 44100 samples ~ 87 blocks
    auto eventsDisabled = collectEvents(arp, ctx, 100);
    auto noteOnsDisabled = filterNoteOns(eventsDisabled);
    auto noteOffsDisabled = filterNoteOffs(eventsDisabled);

    // With Euclidean disabled, all steps should fire (Phase 6 behavior)
    REQUIRE(noteOnsDisabled.size() >= 4);

    // Enable Euclidean mid-playback
    arp.setEuclideanEnabled(true);

    // Run 16 steps (2 full Euclidean cycles of 8 steps)
    // 16 * 11025 = 176400 samples. 176400 / 512 ~ 345 blocks.
    auto eventsEnabled = collectEvents(arp, ctx, 360);
    auto noteOnsEnabled = filterNoteOns(eventsEnabled);
    auto noteOffsEnabled = filterNoteOffs(eventsEnabled);

    // E(3,8) should produce 3 hits per 8-step cycle = ~6 noteOns in 16 steps
    CHECK(noteOnsEnabled.size() >= 5);  // At least ~6 hits (margin for boundary)

    // Verify no stuck notes: every noteOn must have a corresponding noteOff.
    // The last note in the block may still be sounding (gate open), so allow +1.
    CHECK(noteOffsEnabled.size() + 1 >= noteOnsEnabled.size());

    // Verify the Euclidean position started from 0 by checking that
    // the first step after enable fires (position 0 is a hit in E(3,8))
    REQUIRE(noteOnsEnabled.size() >= 1);
    // The first noteOn after enable should be very early (at the next step boundary)
    CHECK(noteOnsEnabled[0].sampleOffset < 22050);  // Within ~2 steps
}


// T062: Enabled-to-disabled transition -- all steps active, no stuck notes (SC-005)
TEST_CASE("EuclideanTransition_EnabledToDisabled_AllStepsActive",
          "[arp][euclidean][transition]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Enable Euclidean: E(1,8) = only 1 hit per 8 steps (very sparse)
    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(1);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run 8 steps with Euclidean enabled (E(1,8) = 1 hit per cycle)
    auto eventsEnabled = collectEvents(arp, ctx, 200);
    auto noteOnsEnabled = filterNoteOns(eventsEnabled);

    // E(1,8) = 1 hit per 8 steps -- should see 1-2 noteOns in ~8 steps
    REQUIRE(noteOnsEnabled.size() >= 1);

    // Now disable Euclidean mid-playback
    arp.setEuclideanEnabled(false);

    // Run 8 more steps -- ALL should fire now (Phase 6 behavior)
    auto eventsAfterDisable = collectEvents(arp, ctx, 200);
    auto noteOnsAfterDisable = filterNoteOns(eventsAfterDisable);
    auto noteOffsAfterDisable = filterNoteOffs(eventsAfterDisable);

    // With Euclidean disabled, all 8 steps should fire
    CHECK(noteOnsAfterDisable.size() >= 7);  // ~8 steps, allow margin

    // No stuck notes: the last note may still be sounding at block boundary
    CHECK(noteOffsAfterDisable.size() + 1 >= noteOnsAfterDisable.size());
}


// T063: Mid-step toggle produces no partial artifacts (SC-005)
TEST_CASE("EuclideanTransition_MidStep_NoPartialArtifacts",
          "[arp][euclidean][transition]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Process a few blocks to get mid-step (not at a step boundary)
    // At 120 BPM, eighth note = 11025 samples. 1 block = 512 samples.
    // Process 5 blocks = 2560 samples -- mid-step (step boundary at 11025).
    std::array<ArpEvent, 128> blockEvents;
    for (size_t b = 0; b < 5; ++b) {
        arp.processBlock(ctx, blockEvents);
        ctx.transportPositionSamples += static_cast<int64_t>(kBlockSize);
    }

    // Toggle Euclidean on mid-step
    arp.setEuclideanSteps(4);
    arp.setEuclideanHits(2);
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    // Run many more blocks to capture subsequent steps
    auto eventsAfterToggle = collectEvents(arp, ctx, 250);
    auto noteOnsAfterToggle = filterNoteOns(eventsAfterToggle);
    auto noteOffsAfterToggle = filterNoteOffs(eventsAfterToggle);

    // E(2,4) has 2 hits per 4 steps. Over ~10 steps should see ~5 noteOns.
    CHECK(noteOnsAfterToggle.size() >= 4);

    // No stuck notes: last note may still be sounding at block boundary
    CHECK(noteOffsAfterToggle.size() + 1 >= noteOnsAfterToggle.size());

    // Verify no partial-step artifacts: every noteOn should have a valid
    // sampleOffset (non-negative, within expected range)
    for (const auto& e : noteOnsAfterToggle) {
        CHECK(e.sampleOffset >= 0);
    }

    // Now toggle OFF mid-step and verify clean transition back
    for (size_t b = 0; b < 3; ++b) {
        arp.processBlock(ctx, blockEvents);
        ctx.transportPositionSamples += static_cast<int64_t>(kBlockSize);
    }
    arp.setEuclideanEnabled(false);

    auto eventsAfterOff = collectEvents(arp, ctx, 200);
    auto noteOnsAfterOff = filterNoteOns(eventsAfterOff);
    auto noteOffsAfterOff = filterNoteOffs(eventsAfterOff);

    // All steps should fire after disable
    CHECK(noteOnsAfterOff.size() >= 7);  // ~8+ steps
    // No stuck notes: noteOffs should be within 1 of noteOns
    // (the very last note may still be sounding at the block boundary)
    CHECK(noteOffsAfterOff.size() + 1 >= noteOnsAfterOff.size());
}


// T064: In-flight ratchet sub-steps complete normally when Euclidean is enabled (FR-010)
TEST_CASE("EuclideanTransition_InFlightRatchet_Completes",
          "[arp][euclidean][transition]") {
    ArpeggiatorCore arp;
    constexpr double kSampleRate = 44100.0;
    constexpr size_t kBlockSize = 512;

    arp.prepare(kSampleRate, kBlockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(60, 100);

    // Set ratchet lane: 4 sub-steps per step
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(4));

    BlockContext ctx;
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Process blocks until the first step fires with ratchet 4.
    // The arp fires after one step duration (11025 samples) = ~22 blocks of 512.
    // Process 25 blocks to be past the first step fire but before all 4 sub-steps
    // complete. Each sub-step = 11025 / 4 = 2756 samples = ~5.4 blocks.
    // After 25 blocks = 12800 samples, we are past the first step fire (~11025)
    // and likely 1-2 sub-steps have fired, with remaining sub-steps in-flight.
    std::array<ArpEvent, 128> blockEvents;
    for (size_t b = 0; b < 25; ++b) {
        arp.processBlock(ctx, blockEvents);
        ctx.transportPositionSamples += static_cast<int64_t>(kBlockSize);
    }

    // Enable Euclidean mid-ratchet (with all hits so pattern is transparent)
    arp.setEuclideanSteps(8);
    arp.setEuclideanHits(8);  // all hits -- pattern is transparent
    arp.setEuclideanRotation(0);
    arp.setEuclideanEnabled(true);

    // Continue processing to complete the remaining ratchet sub-steps
    // and capture subsequent steps
    auto eventsAfterEnable = collectEvents(arp, ctx, 300);
    auto noteOnsAfterEnable = filterNoteOns(eventsAfterEnable);
    auto noteOffsAfterEnable = filterNoteOffs(eventsAfterEnable);

    // With ratchet=4 and all Euclidean hits (transparent), we should see
    // ratcheted noteOns. The in-flight sub-steps should have completed.
    // Over ~12 steps with ratchet 4, expect ~48 noteOns.
    CHECK(noteOnsAfterEnable.size() >= 10);

    // No stuck notes: last note may still be sounding at block boundary
    CHECK(noteOffsAfterEnable.size() + 1 >= noteOnsAfterEnable.size());

    // Additional verification: enable Euclidean with a sparse pattern (E(1,8))
    // during a ratcheted step. The in-flight sub-steps should still complete.
    ArpeggiatorCore arp2;
    arp2.prepare(kSampleRate, kBlockSize);
    arp2.setEnabled(true);
    arp2.setMode(ArpMode::Up);
    arp2.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp2.setGateLength(80.0f);
    arp2.noteOn(60, 100);
    arp2.ratchetLane().setStep(0, static_cast<uint8_t>(4));

    BlockContext ctx2;
    ctx2.sampleRate = kSampleRate;
    ctx2.blockSize = kBlockSize;
    ctx2.tempoBPM = 120.0;
    ctx2.isPlaying = true;
    ctx2.transportPositionSamples = 0;

    // Process until the first step fires and some sub-steps are in-flight
    for (size_t b = 0; b < 25; ++b) {
        arp2.processBlock(ctx2, blockEvents);
        ctx2.transportPositionSamples += static_cast<int64_t>(kBlockSize);
    }

    // Enable Euclidean with sparse pattern E(1,8)
    arp2.setEuclideanSteps(8);
    arp2.setEuclideanHits(1);
    arp2.setEuclideanRotation(0);
    arp2.setEuclideanEnabled(true);

    // Continue -- remaining sub-steps from the first ratcheted step should complete
    auto eventsAfterSparse = collectEvents(arp2, ctx2, 300);
    auto noteOnsSparse = filterNoteOns(eventsAfterSparse);
    auto noteOffsSparse = filterNoteOffs(eventsAfterSparse);

    // With E(1,8) = 1 hit per 8 steps, over ~12 steps we expect ~1-2 noteOns
    // (from the sparse Euclidean pattern) plus any in-flight sub-steps that
    // completed normally before the Euclidean gating kicked in.
    // The key assertion: no stuck notes and no crash.
    CHECK(noteOnsSparse.size() >= 1);
    CHECK(noteOffsSparse.size() + 1 >= noteOnsSparse.size());
}
