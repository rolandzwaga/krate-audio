// ==============================================================================
// Layer 1: Primitives - SequencerCore Tests
// ==============================================================================
// Tests for SequencerCore - reusable timing engine for step sequencers
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline
// - Principle XII: Test-First Development
//
// Reference: specs/099-vowel-sequencer/spec.md
// ==============================================================================

#include <krate/dsp/primitives/sequencer_core.h>

#include <catch2/catch_all.hpp>

#include <array>
#include <set>
#include <cmath>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Lifecycle Tests (T006)
// =============================================================================

TEST_CASE("SequencerCore lifecycle - prepare and reset", "[sequencer_core][lifecycle]") {
    SequencerCore core;

    SECTION("not prepared initially") {
        REQUIRE_FALSE(core.isPrepared());
    }

    SECTION("prepared after prepare()") {
        core.prepare(44100.0);
        REQUIRE(core.isPrepared());
    }

    SECTION("reset preserves prepared state") {
        core.prepare(44100.0);
        core.reset();
        REQUIRE(core.isPrepared());
    }

    SECTION("reset returns to step 0") {
        core.prepare(44100.0);
        core.setNumSteps(4);
        core.setTempo(120.0f);
        core.setNoteValue(NoteValue::Quarter);

        // Advance to a different step
        for (int i = 0; i < 50000; ++i) {
            (void)core.tick();
        }

        REQUIRE(core.getCurrentStep() > 0);

        core.reset();
        REQUIRE(core.getCurrentStep() == 0);
    }

    SECTION("minimum sample rate clamping") {
        core.prepare(500.0);  // Below minimum (1000)
        REQUIRE(core.isPrepared());
        // Should still function correctly with clamped sample rate
    }
}

// =============================================================================
// Timing Tests (T007) - SC-001 equivalent
// =============================================================================

TEST_CASE("SequencerCore timing accuracy - SC-001", "[sequencer_core][timing]") {
    SequencerCore core;
    core.prepare(44100.0);
    core.setNumSteps(4);

    SECTION("quarter note at 120 BPM = 500ms = 22050 samples") {
        core.setTempo(120.0f);
        core.setNoteValue(NoteValue::Quarter);

        const size_t expectedSamples = 22050;
        const float tolerance = 44.1f;  // 1ms @ 44.1kHz = 44 samples

        // Count samples until step change
        size_t count = 0;
        while (!core.tick() && count < 30000) {
            ++count;
        }

        REQUIRE(count == Approx(expectedSamples).margin(tolerance));
    }

    SECTION("eighth note at 120 BPM = 250ms = 11025 samples") {
        core.setTempo(120.0f);
        core.setNoteValue(NoteValue::Eighth);

        const size_t expectedSamples = 11025;
        const float tolerance = 44.1f;

        size_t count = 0;
        while (!core.tick() && count < 15000) {
            ++count;
        }

        REQUIRE(count == Approx(expectedSamples).margin(tolerance));
    }

    SECTION("dotted eighth note at 120 BPM = 375ms") {
        core.setTempo(120.0f);
        core.setNoteValue(NoteValue::Eighth, NoteModifier::Dotted);

        const float expectedMs = 375.0f;
        const size_t expectedSamples = static_cast<size_t>(expectedMs * 44.1f);
        const float tolerance = 44.1f;

        size_t count = 0;
        while (!core.tick() && count < 20000) {
            ++count;
        }

        REQUIRE(count == Approx(expectedSamples).margin(tolerance));
    }

    SECTION("triplet eighth note at 120 BPM") {
        core.setTempo(120.0f);
        core.setNoteValue(NoteValue::Eighth, NoteModifier::Triplet);

        // Triplet = 2/3 of normal, so 250ms * 2/3 = 166.67ms
        const float expectedMs = 250.0f * 0.6666666666667f;
        const size_t expectedSamples = static_cast<size_t>(expectedMs * 44.1f);
        const float tolerance = 44.1f;

        size_t count = 0;
        while (!core.tick() && count < 10000) {
            ++count;
        }

        REQUIRE(count == Approx(expectedSamples).margin(tolerance));
    }

    SECTION("tempo change updates step duration") {
        core.setTempo(120.0f);
        core.setNoteValue(NoteValue::Quarter);

        // Get first step duration at 120 BPM
        size_t count1 = 0;
        while (!core.tick() && count1 < 30000) {
            ++count1;
        }

        // Change tempo to 60 BPM (double the duration)
        core.setTempo(60.0f);

        size_t count2 = 0;
        while (!core.tick() && count2 < 60000) {
            ++count2;
        }

        // At 60 BPM, quarter note = 1000ms = 44100 samples
        REQUIRE(count2 == Approx(44100).margin(100));
    }
}

// =============================================================================
// Forward Direction Tests (T008)
// =============================================================================

TEST_CASE("SequencerCore Forward direction", "[sequencer_core][direction]") {
    SequencerCore core;
    core.prepare(44100.0);
    core.setNumSteps(4);
    core.setTempo(300.0f);  // Fast tempo for quicker testing
    core.setNoteValue(NoteValue::Sixteenth);
    core.setDirection(Direction::Forward);

    SECTION("advances 0,1,2,3,0,1,2,3") {
        std::vector<int> steps;
        steps.push_back(core.getCurrentStep());

        for (int cycle = 0; cycle < 8; ++cycle) {
            while (!core.tick()) {}
            steps.push_back(core.getCurrentStep());
        }

        REQUIRE(steps.size() == 9);
        REQUIRE(steps[0] == 0);
        REQUIRE(steps[1] == 1);
        REQUIRE(steps[2] == 2);
        REQUIRE(steps[3] == 3);
        REQUIRE(steps[4] == 0);
        REQUIRE(steps[5] == 1);
        REQUIRE(steps[6] == 2);
        REQUIRE(steps[7] == 3);
        REQUIRE(steps[8] == 0);
    }

    SECTION("single step loops correctly") {
        core.setNumSteps(1);
        core.reset();

        REQUIRE(core.getCurrentStep() == 0);

        // Advance through a step
        while (!core.tick()) {}
        REQUIRE(core.getCurrentStep() == 0);

        while (!core.tick()) {}
        REQUIRE(core.getCurrentStep() == 0);
    }
}

// =============================================================================
// Backward Direction Tests (T009)
// =============================================================================

TEST_CASE("SequencerCore Backward direction", "[sequencer_core][direction]") {
    SequencerCore core;
    core.prepare(44100.0);
    core.setNumSteps(4);
    core.setTempo(300.0f);
    core.setNoteValue(NoteValue::Sixteenth);
    core.setDirection(Direction::Backward);

    SECTION("advances 3,2,1,0,3,2,1,0") {
        // After reset with Backward, starts at step 3
        REQUIRE(core.getCurrentStep() == 3);

        std::vector<int> steps;
        steps.push_back(core.getCurrentStep());

        for (int cycle = 0; cycle < 8; ++cycle) {
            while (!core.tick()) {}
            steps.push_back(core.getCurrentStep());
        }

        REQUIRE(steps.size() == 9);
        REQUIRE(steps[0] == 3);
        REQUIRE(steps[1] == 2);
        REQUIRE(steps[2] == 1);
        REQUIRE(steps[3] == 0);
        REQUIRE(steps[4] == 3);
        REQUIRE(steps[5] == 2);
        REQUIRE(steps[6] == 1);
        REQUIRE(steps[7] == 0);
        REQUIRE(steps[8] == 3);
    }
}

// =============================================================================
// PingPong Direction Tests (T010)
// =============================================================================

TEST_CASE("SequencerCore PingPong direction", "[sequencer_core][direction]") {
    SequencerCore core;
    core.prepare(44100.0);
    core.setNumSteps(4);
    core.setTempo(300.0f);
    core.setNoteValue(NoteValue::Sixteenth);
    core.setDirection(Direction::PingPong);

    SECTION("bounces 0,1,2,3,2,1,0,1,2,3") {
        // PingPong starts at step 0
        REQUIRE(core.getCurrentStep() == 0);

        std::vector<int> steps;
        steps.push_back(core.getCurrentStep());

        for (int cycle = 0; cycle < 9; ++cycle) {
            while (!core.tick()) {}
            steps.push_back(core.getCurrentStep());
        }

        // Pattern: 0,1,2,3,2,1,0,1,2,3
        // Endpoints (0, 3) are visited once per cycle
        REQUIRE(steps.size() == 10);
        REQUIRE(steps[0] == 0);
        REQUIRE(steps[1] == 1);
        REQUIRE(steps[2] == 2);
        REQUIRE(steps[3] == 3);
        REQUIRE(steps[4] == 2);
        REQUIRE(steps[5] == 1);
        REQUIRE(steps[6] == 0);
        REQUIRE(steps[7] == 1);
        REQUIRE(steps[8] == 2);
        REQUIRE(steps[9] == 3);
    }

    SECTION("two steps pingpongs correctly") {
        core.setNumSteps(2);
        core.reset();

        std::vector<int> steps;
        steps.push_back(core.getCurrentStep());

        for (int cycle = 0; cycle < 5; ++cycle) {
            while (!core.tick()) {}
            steps.push_back(core.getCurrentStep());
        }

        // Pattern: 0,1,0,1,0,1
        REQUIRE(steps[0] == 0);
        REQUIRE(steps[1] == 1);
        REQUIRE(steps[2] == 0);
        REQUIRE(steps[3] == 1);
    }

    SECTION("single step stays at 0") {
        core.setNumSteps(1);
        core.reset();

        REQUIRE(core.getCurrentStep() == 0);

        while (!core.tick()) {}
        REQUIRE(core.getCurrentStep() == 0);

        while (!core.tick()) {}
        REQUIRE(core.getCurrentStep() == 0);
    }
}

// =============================================================================
// Random Direction Tests (T011)
// =============================================================================

TEST_CASE("SequencerCore Random direction", "[sequencer_core][direction]") {
    SequencerCore core;
    core.prepare(44100.0);
    core.setNumSteps(5);
    core.setTempo(300.0f);
    core.setNoteValue(NoteValue::Sixteenth);
    core.setDirection(Direction::Random);

    SECTION("all steps visited within 10*N iterations") {
        std::set<int> visitedSteps;
        visitedSteps.insert(core.getCurrentStep());

        const int maxIterations = 50;  // 10 * 5

        for (int i = 0; i < maxIterations && visitedSteps.size() < 5; ++i) {
            while (!core.tick()) {}
            visitedSteps.insert(core.getCurrentStep());
        }

        REQUIRE(visitedSteps.size() == 5);
    }

    SECTION("no immediate repetition") {
        const int numTests = 100;
        int previousStep = core.getCurrentStep();

        for (int i = 0; i < numTests; ++i) {
            while (!core.tick()) {}
            int currentStep = core.getCurrentStep();
            REQUIRE(currentStep != previousStep);
            previousStep = currentStep;
        }
    }

    SECTION("single step always stays at 0") {
        core.setNumSteps(1);
        core.reset();

        REQUIRE(core.getCurrentStep() == 0);

        // Even with random direction, single step stays at 0
        while (!core.tick()) {}
        REQUIRE(core.getCurrentStep() == 0);
    }
}

// =============================================================================
// Swing Tests (T012)
// =============================================================================

TEST_CASE("SequencerCore swing timing - SC-004", "[sequencer_core][swing]") {
    SequencerCore core;
    core.prepare(44100.0);
    core.setNumSteps(4);
    core.setTempo(120.0f);
    core.setNoteValue(NoteValue::Eighth);

    SECTION("50% swing produces 3:1 ratio") {
        core.setSwing(0.5f);

        // Measure step 0 (even, longer)
        size_t step0Samples = 0;
        while (!core.tick()) {
            ++step0Samples;
        }

        // Measure step 1 (odd, shorter)
        size_t step1Samples = 0;
        while (!core.tick()) {
            ++step1Samples;
        }

        float ratio = static_cast<float>(step0Samples) / static_cast<float>(step1Samples);

        // 50% swing: even = base * 1.5, odd = base * 0.5
        // Ratio = 1.5 / 0.5 = 3.0
        REQUIRE(ratio >= 2.9f);
        REQUIRE(ratio <= 3.1f);
    }

    SECTION("0% swing produces equal durations") {
        core.setSwing(0.0f);

        size_t step0Samples = 0;
        while (!core.tick()) {
            ++step0Samples;
        }

        size_t step1Samples = 0;
        while (!core.tick()) {
            ++step1Samples;
        }

        // Should be nearly equal
        REQUIRE(step0Samples == Approx(step1Samples).margin(10));
    }

    SECTION("swing clamped to valid range") {
        core.setSwing(-0.5f);  // Below min
        core.setSwing(1.5f);   // Above max
        // Should not crash, just clamp
    }
}

// =============================================================================
// PPQ Sync Tests (T013) - SC-008 equivalent
// =============================================================================

TEST_CASE("SequencerCore PPQ sync - SC-008", "[sequencer_core][sync]") {
    SequencerCore core;
    core.prepare(44100.0);
    core.setNumSteps(4);
    core.setTempo(120.0f);
    core.setNoteValue(NoteValue::Quarter);  // 1 beat per step
    core.setDirection(Direction::Forward);

    SECTION("sync to beginning of step 0") {
        core.sync(0.0);
        REQUIRE(core.getCurrentStep() == 0);
    }

    SECTION("sync to beginning of step 2") {
        core.sync(2.0);  // 2 beats into pattern = step 2
        REQUIRE(core.getCurrentStep() == 2);
    }

    SECTION("sync wraps around pattern") {
        core.sync(5.0);  // 5 beats = step 1 (5 % 4 = 1)
        REQUIRE(core.getCurrentStep() == 1);
    }

    SECTION("sync to mid-step position") {
        core.sync(0.5);  // Halfway through step 0
        REQUIRE(core.getCurrentStep() == 0);

        // After sync, should be positioned mid-step
        // Count samples until next step - should be about half
        size_t count = 0;
        while (!core.tick() && count < 20000) {
            ++count;
        }

        // Should be about 11025 samples (half of 22050)
        REQUIRE(count == Approx(11025).margin(500));
    }

    SECTION("sync with Backward direction") {
        core.setDirection(Direction::Backward);
        core.sync(2.0);  // With backward, 2 beats = step (N-1 - 2) = step 1
        // Actually for backward: step = N-1 - (pos % N) = 3 - 2 = 1
        REQUIRE(core.getCurrentStep() == 1);
    }

    SECTION("sync with PingPong direction") {
        core.setDirection(Direction::PingPong);
        // Cycle length = 2 * (4-1) = 6
        // Position 3 = step 3 (ascending part)
        core.sync(3.0);
        REQUIRE(core.getCurrentStep() == 3);

        // Position 4 = step 2 (descending part: 6 - 4 = 2)
        core.sync(4.0);
        REQUIRE(core.getCurrentStep() == 2);
    }

    SECTION("sync with Random direction keeps current step") {
        core.setDirection(Direction::Random);
        int beforeSync = core.getCurrentStep();
        core.sync(2.0);
        // Random mode cannot predict sequence, keeps current step
        REQUIRE(core.getCurrentStep() == beforeSync);
    }
}

// =============================================================================
// Gate Length Tests (T014)
// =============================================================================

TEST_CASE("SequencerCore gate length", "[sequencer_core][gate]") {
    SequencerCore core;
    core.prepare(44100.0);
    core.setNumSteps(4);
    core.setTempo(120.0f);
    core.setNoteValue(NoteValue::Quarter);

    SECTION("100% gate stays active entire step") {
        core.setGateLength(1.0f);

        // Gate should be active at start
        REQUIRE(core.isGateActive());

        // Process most of the step
        for (size_t i = 0; i < 20000; ++i) {
            (void)core.tick();
            REQUIRE(core.isGateActive());
        }
    }

    SECTION("50% gate active for first half") {
        core.setGateLength(0.5f);

        // Step duration at 120 BPM quarter = 22050 samples
        // Gate should be active for first 11025 samples

        REQUIRE(core.isGateActive());

        // Process first quarter of step - gate should be active
        for (size_t i = 0; i < 5000; ++i) {
            (void)core.tick();
        }
        REQUIRE(core.isGateActive());

        // Process past 50% mark - gate should be inactive
        for (size_t i = 0; i < 10000; ++i) {
            (void)core.tick();
        }
        REQUIRE_FALSE(core.isGateActive());
    }

    SECTION("getGateRampValue returns smooth 5ms crossfade") {
        core.setGateLength(0.5f);

        // Process to gate off point
        for (size_t i = 0; i < 12000; ++i) {
            (void)core.tick();
            (void)core.getGateRampValue();
        }

        // Gate should be transitioning - ramp value should be between 0 and 1
        float rampValue = core.getGateRampValue();
        // After gate turns off, ramp should be ramping down
        REQUIRE(rampValue <= 1.0f);
        REQUIRE(rampValue >= 0.0f);

        // Continue processing until ramp completes
        for (size_t i = 0; i < 500; ++i) {
            (void)core.tick();
            (void)core.getGateRampValue();
        }

        // After ~5ms (220 samples), ramp should be near 0
        rampValue = core.getGateRampValue();
        REQUIRE(rampValue < 0.1f);
    }

    SECTION("gate reactivates on step change") {
        core.setGateLength(0.5f);

        // Process through first step
        while (!core.tick()) {}

        // Gate should reactivate at step boundary
        REQUIRE(core.isGateActive());
    }
}

// =============================================================================
// Configuration Tests
// =============================================================================

TEST_CASE("SequencerCore configuration", "[sequencer_core][config]") {
    SequencerCore core;
    core.prepare(44100.0);

    SECTION("setNumSteps clamps to valid range") {
        core.setNumSteps(0);  // Below min
        REQUIRE(core.getNumSteps() >= 1);

        core.setNumSteps(100);  // Above max
        REQUIRE(core.getNumSteps() <= 16);
    }

    SECTION("setTempo clamps to valid range") {
        core.setTempo(10.0f);   // Below min (20)
        core.setTempo(400.0f);  // Above max (300)
        // Should not crash
    }

    SECTION("trigger advances step immediately") {
        core.setNumSteps(4);
        core.setDirection(Direction::Forward);
        core.reset();

        REQUIRE(core.getCurrentStep() == 0);

        core.trigger();
        REQUIRE(core.getCurrentStep() == 1);

        core.trigger();
        REQUIRE(core.getCurrentStep() == 2);
    }

    SECTION("getDirection returns current direction") {
        core.setDirection(Direction::Backward);
        REQUIRE(core.getDirection() == Direction::Backward);

        core.setDirection(Direction::PingPong);
        REQUIRE(core.getDirection() == Direction::PingPong);
    }
}
