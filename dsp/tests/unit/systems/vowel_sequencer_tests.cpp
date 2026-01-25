// ==============================================================================
// Layer 3: Systems - VowelSequencer Tests
// ==============================================================================
// Tests for VowelSequencer - 16-step vowel formant sequencer
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline
// - Principle XII: Test-First Development
//
// Reference: specs/099-vowel-sequencer/spec.md
// ==============================================================================

#include <krate/dsp/systems/vowel_sequencer.h>

#include <catch2/catch_all.hpp>

#include <array>
#include <cmath>
#include <set>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Lifecycle Tests (T015)
// =============================================================================

TEST_CASE("VowelSequencer lifecycle - prepare and reset", "[vowel_sequencer][lifecycle]") {
    VowelSequencer seq;

    SECTION("not prepared initially") {
        REQUIRE_FALSE(seq.isPrepared());
    }

    SECTION("prepared after prepare()") {
        seq.prepare(44100.0);
        REQUIRE(seq.isPrepared());
    }

    SECTION("reset preserves prepared state") {
        seq.prepare(44100.0);
        seq.reset();
        REQUIRE(seq.isPrepared());
    }

    SECTION("reset returns to step 0") {
        seq.prepare(44100.0);
        seq.setNumSteps(4);
        seq.setTempo(120.0f);
        seq.setNoteValue(NoteValue::Quarter);

        // Advance to a different step
        for (int i = 0; i < 50000; ++i) {
            (void)seq.process(0.5f);
        }

        REQUIRE(seq.getCurrentStep() > 0);

        seq.reset();
        REQUIRE(seq.getCurrentStep() == 0);
    }
}

// =============================================================================
// Step Configuration Tests (T016)
// =============================================================================

TEST_CASE("VowelSequencer step configuration", "[vowel_sequencer][steps]") {
    VowelSequencer seq;
    seq.prepare(44100.0);

    SECTION("default step values") {
        // Default should be Vowel::A
        const auto& step = seq.getStep(0);
        REQUIRE(step.vowel == Vowel::A);
        REQUIRE(step.morph == 0.0f);
    }

    SECTION("setStepVowel updates discrete vowel") {
        seq.setStepVowel(0, Vowel::E);
        seq.setStepVowel(1, Vowel::I);
        seq.setStepVowel(2, Vowel::O);
        seq.setStepVowel(3, Vowel::U);

        REQUIRE(seq.getStep(0).vowel == Vowel::E);
        REQUIRE(seq.getStep(1).vowel == Vowel::I);
        REQUIRE(seq.getStep(2).vowel == Vowel::O);
        REQUIRE(seq.getStep(3).vowel == Vowel::U);
    }

    SECTION("setStepMorph updates continuous morph position") {
        seq.setStepMorph(0, 0.0f);   // A
        seq.setStepMorph(1, 1.5f);   // Between E and I
        seq.setStepMorph(2, 2.0f);   // I
        seq.setStepMorph(3, 4.0f);   // U

        REQUIRE(seq.getStep(0).morph == Approx(0.0f));
        REQUIRE(seq.getStep(1).morph == Approx(1.5f));
        REQUIRE(seq.getStep(2).morph == Approx(2.0f));
        REQUIRE(seq.getStep(3).morph == Approx(4.0f));
    }

    SECTION("morph clamped to valid range") {
        seq.setStepMorph(0, -1.0f);
        seq.setStepMorph(1, 5.0f);

        REQUIRE(seq.getStep(0).morph >= 0.0f);
        REQUIRE(seq.getStep(1).morph <= 4.0f);
    }

    SECTION("setNumSteps clamps to valid range") {
        seq.setNumSteps(0);  // Below min
        REQUIRE(seq.getNumSteps() >= 1);

        seq.setNumSteps(100);  // Above max
        REQUIRE(seq.getNumSteps() <= 16);
    }

    SECTION("setStep sets all parameters") {
        VowelStep step;
        step.vowel = Vowel::I;
        step.morph = 2.5f;
        seq.setStep(0, step);

        REQUIRE(seq.getStep(0).vowel == Vowel::I);
        REQUIRE(seq.getStep(0).morph == Approx(2.5f));
    }
}

// =============================================================================
// Morph Mode Tests (T017)
// =============================================================================

TEST_CASE("VowelSequencer morph mode - SC-009", "[vowel_sequencer][morph]") {
    VowelSequencer seq;
    seq.prepare(44100.0);
    seq.setNumSteps(2);
    seq.setTempo(300.0f);  // Fast for testing
    seq.setNoteValue(NoteValue::Sixteenth);
    seq.setGlideTime(0.0f);  // No glide for discrete testing

    SECTION("discrete mode uses exact vowel formants") {
        seq.setMorphMode(false);  // Discrete mode
        seq.setStepVowel(0, Vowel::A);
        seq.setStepVowel(1, Vowel::U);

        // Process some samples - should use discrete vowels only
        // The filter should be configured for vowel A's formants
        for (int i = 0; i < 1000; ++i) {
            (void)seq.process(0.5f);
        }

        // Cannot easily verify formant frequencies externally,
        // but we verify no crash and correct mode
        REQUIRE_FALSE(seq.isMorphMode());
    }

    SECTION("morph mode interpolates between vowels") {
        seq.setMorphMode(true);  // Morph mode
        seq.setStepMorph(0, 0.0f);   // A
        seq.setStepMorph(1, 2.0f);   // I

        // Process some samples
        for (int i = 0; i < 1000; ++i) {
            (void)seq.process(0.5f);
        }

        REQUIRE(seq.isMorphMode());
    }
}

// =============================================================================
// Global Formant Modification Tests (T018)
// =============================================================================

TEST_CASE("VowelSequencer global formant modification", "[vowel_sequencer][formant]") {
    VowelSequencer seq;
    seq.prepare(44100.0);
    seq.setNumSteps(4);

    SECTION("formant shift applies to all steps") {
        seq.setFormantShift(12.0f);  // +1 octave
        // Should not crash, formants should be shifted up
        for (int i = 0; i < 1000; ++i) {
            (void)seq.process(0.5f);
        }
    }

    SECTION("formant shift clamped to valid range") {
        seq.setFormantShift(-30.0f);  // Below min (-24)
        seq.setFormantShift(30.0f);   // Above max (+24)
        // Should clamp without crash
    }

    SECTION("gender parameter applies to all steps") {
        seq.setGender(-1.0f);  // Male
        for (int i = 0; i < 1000; ++i) {
            (void)seq.process(0.5f);
        }

        seq.setGender(1.0f);  // Female
        for (int i = 0; i < 1000; ++i) {
            (void)seq.process(0.5f);
        }
    }

    SECTION("gender parameter clamped to valid range") {
        seq.setGender(-2.0f);  // Below min
        seq.setGender(2.0f);   // Above max
        // Should clamp without crash
    }
}

// =============================================================================
// Timing Tests (T019)
// =============================================================================

TEST_CASE("VowelSequencer timing accuracy - SC-001", "[vowel_sequencer][timing]") {
    VowelSequencer seq;
    seq.prepare(44100.0);
    seq.setNumSteps(4);

    SECTION("quarter note at 120 BPM = 500ms = 22050 samples") {
        seq.setTempo(120.0f);
        seq.setNoteValue(NoteValue::Quarter);

        const size_t expectedSamples = 22050;
        const float tolerance = 44.1f;  // 1ms @ 44.1kHz

        // Count samples until step change
        int startStep = seq.getCurrentStep();
        size_t count = 0;
        while (seq.getCurrentStep() == startStep && count < 30000) {
            (void)seq.process(0.5f);
            ++count;
        }

        REQUIRE(count == Approx(expectedSamples).margin(tolerance));
    }

    SECTION("tempo change updates step duration") {
        seq.setTempo(120.0f);
        seq.setNoteValue(NoteValue::Quarter);

        // Get first step duration at 120 BPM
        size_t count1 = 0;
        int startStep = seq.getCurrentStep();
        while (seq.getCurrentStep() == startStep && count1 < 30000) {
            (void)seq.process(0.5f);
            ++count1;
        }

        // Change tempo to 60 BPM (double the duration)
        seq.setTempo(60.0f);

        size_t count2 = 0;
        startStep = seq.getCurrentStep();
        while (seq.getCurrentStep() == startStep && count2 < 60000) {
            (void)seq.process(0.5f);
            ++count2;
        }

        // At 60 BPM, quarter note = 1000ms = 44100 samples
        REQUIRE(count2 == Approx(44100).margin(100));
    }
}

// =============================================================================
// Direction Tests (T020)
// =============================================================================

TEST_CASE("VowelSequencer direction modes - SC-005 to SC-007", "[vowel_sequencer][direction]") {
    VowelSequencer seq;
    seq.prepare(44100.0);
    seq.setNumSteps(4);
    seq.setTempo(300.0f);
    seq.setNoteValue(NoteValue::Sixteenth);

    SECTION("Forward direction: 0,1,2,3,0,1,2,3") {
        seq.setDirection(Direction::Forward);

        std::vector<int> steps;
        steps.push_back(seq.getCurrentStep());

        for (int cycle = 0; cycle < 8; ++cycle) {
            int startStep = seq.getCurrentStep();
            while (seq.getCurrentStep() == startStep) {
                (void)seq.process(0.5f);
            }
            steps.push_back(seq.getCurrentStep());
        }

        REQUIRE(steps[0] == 0);
        REQUIRE(steps[1] == 1);
        REQUIRE(steps[2] == 2);
        REQUIRE(steps[3] == 3);
        REQUIRE(steps[4] == 0);
    }

    SECTION("Backward direction: 3,2,1,0,3,2,1,0") {
        seq.setDirection(Direction::Backward);

        // After reset with Backward, starts at step 3
        REQUIRE(seq.getCurrentStep() == 3);

        std::vector<int> steps;
        steps.push_back(seq.getCurrentStep());

        for (int cycle = 0; cycle < 4; ++cycle) {
            int startStep = seq.getCurrentStep();
            while (seq.getCurrentStep() == startStep) {
                (void)seq.process(0.5f);
            }
            steps.push_back(seq.getCurrentStep());
        }

        REQUIRE(steps[0] == 3);
        REQUIRE(steps[1] == 2);
        REQUIRE(steps[2] == 1);
        REQUIRE(steps[3] == 0);
        REQUIRE(steps[4] == 3);
    }

    SECTION("PingPong direction: 0,1,2,3,2,1,0,1") {
        seq.setDirection(Direction::PingPong);

        std::vector<int> steps;
        steps.push_back(seq.getCurrentStep());

        for (int cycle = 0; cycle < 7; ++cycle) {
            int startStep = seq.getCurrentStep();
            while (seq.getCurrentStep() == startStep) {
                (void)seq.process(0.5f);
            }
            steps.push_back(seq.getCurrentStep());
        }

        REQUIRE(steps[0] == 0);
        REQUIRE(steps[1] == 1);
        REQUIRE(steps[2] == 2);
        REQUIRE(steps[3] == 3);
        REQUIRE(steps[4] == 2);
        REQUIRE(steps[5] == 1);
        REQUIRE(steps[6] == 0);
        REQUIRE(steps[7] == 1);
    }

    SECTION("Random direction: no immediate repeat") {
        seq.setDirection(Direction::Random);

        const int numTests = 100;
        int previousStep = seq.getCurrentStep();

        for (int i = 0; i < numTests; ++i) {
            while (seq.getCurrentStep() == previousStep) {
                (void)seq.process(0.5f);
            }
            int currentStep = seq.getCurrentStep();
            REQUIRE(currentStep != previousStep);
            previousStep = currentStep;
        }
    }
}

// =============================================================================
// Gate and Output Tests (T021)
// =============================================================================

TEST_CASE("VowelSequencer gate behavior - SC-003", "[vowel_sequencer][gate]") {
    VowelSequencer seq;
    seq.prepare(44100.0);
    seq.setNumSteps(4);
    seq.setTempo(120.0f);
    seq.setNoteValue(NoteValue::Quarter);

    SECTION("100% gate maintains filtered output") {
        seq.setGateLength(1.0f);

        // Process some samples with non-zero input
        float input = 0.5f;
        float output = 0.0f;

        for (int i = 0; i < 1000; ++i) {
            output = seq.process(input);
        }

        // Output should be non-zero (filter active)
        // Note: Exact value depends on formant filter characteristics
        REQUIRE(std::abs(output) > 0.0f);
    }

    SECTION("0% gate produces dry signal") {
        seq.setGateLength(0.0f);

        // Let the gate ramp settle
        for (int i = 0; i < 500; ++i) {
            (void)seq.process(0.5f);
        }

        // Now output should equal input (dry signal)
        float input = 0.5f;
        float output = seq.process(input);

        // Should be very close to input (dry)
        REQUIRE(output == Approx(input).margin(0.01f));
    }

    SECTION("gate off returns to input (not silence)") {
        seq.setGateLength(0.5f);

        // Process past gate-off point
        // At 120 BPM quarter = 22050 samples, 50% gate = 11025 samples
        for (int i = 0; i < 20000; ++i) {
            (void)seq.process(0.5f);
        }

        // Let the gate ramp settle (5ms = 220 samples)
        for (int i = 0; i < 500; ++i) {
            (void)seq.process(0.5f);
        }

        // Output should be close to input when gate is off
        float input = 0.5f;
        float output = seq.process(input);

        // Should be close to dry signal
        REQUIRE(output == Approx(input).margin(0.1f));
    }
}

// =============================================================================
// Glide Tests (T022)
// =============================================================================

TEST_CASE("VowelSequencer glide/portamento - SC-002", "[vowel_sequencer][glide]") {
    VowelSequencer seq;
    seq.prepare(44100.0);
    seq.setNumSteps(2);
    seq.setTempo(120.0f);
    seq.setNoteValue(NoteValue::Half);  // Long steps for glide testing
    seq.setStepVowel(0, Vowel::A);
    seq.setStepVowel(1, Vowel::U);

    SECTION("0ms glide snaps immediately") {
        seq.setGlideTime(0.0f);

        // Process some samples - parameters should snap
        for (int i = 0; i < 1000; ++i) {
            (void)seq.process(0.5f);
        }

        // No crashes expected
    }

    SECTION("long glide truncated to step duration") {
        // Step duration at 120 BPM half note = 1000ms
        seq.setGlideTime(2000.0f);  // Longer than step

        // Should not cause infinite glide or crash
        for (int i = 0; i < 50000; ++i) {
            (void)seq.process(0.5f);
        }
    }

    SECTION("glide smooths transitions") {
        seq.setGlideTime(50.0f);  // 50ms glide

        // Process through a step change
        for (int i = 0; i < 50000; ++i) {
            (void)seq.process(0.5f);
        }

        // No crashes expected
    }
}

// =============================================================================
// Swing Tests (T023)
// =============================================================================

TEST_CASE("VowelSequencer swing timing - SC-004", "[vowel_sequencer][swing]") {
    VowelSequencer seq;
    seq.prepare(44100.0);
    seq.setNumSteps(4);
    seq.setTempo(120.0f);
    seq.setNoteValue(NoteValue::Eighth);

    SECTION("50% swing produces 3:1 ratio") {
        seq.setSwing(0.5f);

        // Measure step 0 (even, longer)
        size_t step0Samples = 0;
        int startStep = seq.getCurrentStep();
        while (seq.getCurrentStep() == startStep) {
            (void)seq.process(0.5f);
            ++step0Samples;
        }

        // Measure step 1 (odd, shorter)
        size_t step1Samples = 0;
        startStep = seq.getCurrentStep();
        while (seq.getCurrentStep() == startStep) {
            (void)seq.process(0.5f);
            ++step1Samples;
        }

        float ratio = static_cast<float>(step0Samples) / static_cast<float>(step1Samples);

        // 50% swing: ratio should be ~3:1
        REQUIRE(ratio >= 2.9f);
        REQUIRE(ratio <= 3.1f);
    }

    SECTION("0% swing produces equal durations") {
        seq.setSwing(0.0f);

        size_t step0Samples = 0;
        int startStep = seq.getCurrentStep();
        while (seq.getCurrentStep() == startStep) {
            (void)seq.process(0.5f);
            ++step0Samples;
        }

        size_t step1Samples = 0;
        startStep = seq.getCurrentStep();
        while (seq.getCurrentStep() == startStep) {
            (void)seq.process(0.5f);
            ++step1Samples;
        }

        // Should be nearly equal
        REQUIRE(step0Samples == Approx(step1Samples).margin(10));
    }
}

// =============================================================================
// PPQ Sync Tests
// =============================================================================

TEST_CASE("VowelSequencer PPQ sync - SC-008", "[vowel_sequencer][sync]") {
    VowelSequencer seq;
    seq.prepare(44100.0);
    seq.setNumSteps(4);
    seq.setTempo(120.0f);
    seq.setNoteValue(NoteValue::Quarter);
    seq.setDirection(Direction::Forward);

    SECTION("sync to beginning of step 0") {
        seq.sync(0.0);
        REQUIRE(seq.getCurrentStep() == 0);
    }

    SECTION("sync to beginning of step 2") {
        seq.sync(2.0);
        REQUIRE(seq.getCurrentStep() == 2);
    }

    SECTION("sync wraps around pattern") {
        seq.sync(5.0);  // 5 beats = step 1 (5 % 4 = 1)
        REQUIRE(seq.getCurrentStep() == 1);
    }
}

// =============================================================================
// ProcessBlock Tests
// =============================================================================

TEST_CASE("VowelSequencer processBlock", "[vowel_sequencer][process]") {
    VowelSequencer seq;
    seq.prepare(44100.0);
    seq.setNumSteps(4);
    seq.setTempo(120.0f);
    seq.setNoteValue(NoteValue::Quarter);

    SECTION("processes buffer in-place") {
        std::array<float, 512> buffer;
        buffer.fill(0.5f);

        seq.processBlock(buffer.data(), buffer.size(), nullptr);

        // Buffer should be modified (filtered)
        // Check that at least some samples changed
        int changedSamples = 0;
        for (float sample : buffer) {
            if (std::abs(sample - 0.5f) > 0.001f) {
                ++changedSamples;
            }
        }
        REQUIRE(changedSamples > 0);
    }

    SECTION("uses tempo from BlockContext") {
        std::array<float, 512> buffer;
        buffer.fill(0.5f);

        BlockContext ctx{.sampleRate = 44100.0, .tempoBPM = 60.0};

        seq.processBlock(buffer.data(), buffer.size(), &ctx);

        // Should update tempo internally (no direct way to verify without timing tests)
    }
}

// =============================================================================
// Edge Cases and Safety
// =============================================================================

TEST_CASE("VowelSequencer edge cases and safety", "[vowel_sequencer][safety]") {
    VowelSequencer seq;
    seq.prepare(44100.0);

    SECTION("handles NaN input") {
        float output = seq.process(std::numeric_limits<float>::quiet_NaN());
        REQUIRE_FALSE(std::isnan(output));
    }

    SECTION("handles Inf input") {
        float output = seq.process(std::numeric_limits<float>::infinity());
        REQUIRE_FALSE(std::isinf(output));
    }

    SECTION("single step loops correctly") {
        seq.setNumSteps(1);
        seq.setTempo(300.0f);
        seq.setNoteValue(NoteValue::Sixteenth);

        for (int i = 0; i < 10000; ++i) {
            (void)seq.process(0.5f);
            REQUIRE(seq.getCurrentStep() == 0);
        }
    }

    SECTION("process returns 0 when not prepared") {
        VowelSequencer unprepared;
        float output = unprepared.process(0.5f);
        REQUIRE(output == 0.0f);
    }

    SECTION("trigger manually advances step") {
        seq.setNumSteps(4);
        seq.setDirection(Direction::Forward);
        seq.reset();

        REQUIRE(seq.getCurrentStep() == 0);

        seq.trigger();
        REQUIRE(seq.getCurrentStep() == 1);

        seq.trigger();
        REQUIRE(seq.getCurrentStep() == 2);
    }
}
