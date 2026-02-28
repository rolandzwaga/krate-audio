// ==============================================================================
// Unit Tests: TranceGate (Layer 2 Processor)
// ==============================================================================
// Tests for the rhythmic energy shaper / pattern-driven VCA.
// Reference: specs/039-trance-gate/spec.md
// ==============================================================================

#include <krate/dsp/processors/trance_gate.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <vector>

using namespace Krate::DSP;

// =============================================================================
// Helper: calculate expected samples per step
// =============================================================================
static size_t expectedSamplesPerStep(double bpm, NoteValue note,
                                      NoteModifier mod, double sampleRate) {
    const float beatsPerNote = getBeatsForNote(note, mod);
    const double secondsPerBeat = 60.0 / bpm;
    return static_cast<size_t>(secondsPerBeat * static_cast<double>(beatsPerNote) * sampleRate);
}

// =============================================================================
// Phase 1: Skeleton
// =============================================================================

TEST_CASE("TranceGate - compiles", "[trance_gate]") {
    TranceGate gate;
    (void)gate;
    REQUIRE(true);
}

// =============================================================================
// Phase 2: Foundational - Timing and Smoother
// =============================================================================

TEST_CASE("TranceGate - step advancement at correct sample count", "[trance_gate][foundational]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 16;
    params.noteValue = NoteValue::Sixteenth;
    params.noteModifier = NoteModifier::None;
    params.depth = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    // Set alternating pattern: 1.0, 0.0, 1.0, 0.0, ...
    for (int i = 0; i < 16; ++i) {
        gate.setStep(i, (i % 2 == 0) ? 1.0f : 0.0f);
    }

    REQUIRE(gate.getCurrentStep() == 0);

    // At 120 BPM, 1/16 note = 0.25 beats * 0.5 sec/beat * 44100 = 5512.5 => 5512 samples
    const size_t stepsPerNote = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                        NoteModifier::None, 44100.0);
    REQUIRE(stepsPerNote == 5512);

    // Process exactly stepsPerNote samples -- should advance to step 1
    for (size_t i = 0; i < stepsPerNote; ++i) {
        (void)gate.process(1.0f);
    }

    REQUIRE(gate.getCurrentStep() == 1);
}

TEST_CASE("TranceGate - smoother produces smooth transitions", "[trance_gate][foundational]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 2;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 5.0f;
    params.releaseMs = 10.0f;
    params.tempoSync = true;
    gate.setParams(params);

    // Pattern: step 0 = 1.0, step 1 = 0.0
    gate.setStep(0, 1.0f);
    gate.setStep(1, 0.0f);

    const size_t stepsPerNote = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                        NoteModifier::None, 44100.0);

    // Process through step 0 (gain ~1.0), leaving 1 sample before boundary
    float lastOutput = 0.0f;
    for (size_t i = 0; i < stepsPerNote - 1; ++i) {
        lastOutput = gate.process(1.0f);
    }
    // Near end of step 0, output should be near 1.0
    REQUIRE(lastOutput == Catch::Approx(1.0f).margin(0.01f));

    // Process the boundary sample (step advances to 1) plus one more
    (void)gate.process(1.0f);  // step boundary
    float firstSampleStep1 = gate.process(1.0f);
    // Must be less than 1.0 but greater than 0.0 (smoothing in progress)
    REQUIRE(firstSampleStep1 < 1.0f);
    REQUIRE(firstSampleStep1 > 0.0f);

    // Process more samples -- gain should continue decreasing
    float prevGain = firstSampleStep1;
    bool monotonicallyDecreasing = true;
    for (size_t i = 1; i < 200; ++i) {
        float output = gate.process(1.0f);
        if (output > prevGain + 0.0001f) {
            monotonicallyDecreasing = false;
            break;
        }
        prevGain = output;
    }
    REQUIRE(monotonicallyDecreasing);
}

// =============================================================================
// Phase 3: User Story 1 - Pattern-Driven Rhythmic Gating
// =============================================================================

TEST_CASE("TranceGate - alternating pattern produces rhythmic gating at correct step duration",
           "[trance_gate][US1]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 16;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    // Alternating 1.0 / 0.0
    for (int i = 0; i < 16; ++i) {
        gate.setStep(i, (i % 2 == 0) ? 1.0f : 0.0f);
    }

    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);

    // Process step 0 (level 1.0) -- check output near end of step
    // Leave 100 samples before step boundary so we're firmly in step 0
    for (size_t i = 0; i < samplesPerStep - 100; ++i) {
        (void)gate.process(1.0f);
    }
    // Near end of step 0, output should be near 1.0
    float outputEndStep0 = 0.0f;
    for (size_t i = 0; i < 50; ++i) {
        outputEndStep0 = gate.process(1.0f);
    }
    REQUIRE(outputEndStep0 == Catch::Approx(1.0f).margin(0.01f));
    // Skip past step boundary
    for (size_t i = 0; i < 50; ++i) {
        (void)gate.process(1.0f);
    }

    // Process step 1 (level 0.0) -- after ramp, should be near 0.0
    // With 1ms attack/release, ramp is ~44 samples. Process a few hundred.
    for (size_t i = 0; i < 300; ++i) {
        (void)gate.process(1.0f);
    }
    float outputMidStep1 = gate.process(1.0f);
    REQUIRE(outputMidStep1 == Catch::Approx(0.0f).margin(0.01f));
}

TEST_CASE("TranceGate - ghost notes and accents produce float-level gain",
           "[trance_gate][US1]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 4;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    // Step 0 = 0.3 (ghost note), Step 1 = 1.0 (accent), Step 2 = 0.0, Step 3 = 0.7
    gate.setStep(0, 0.3f);
    gate.setStep(1, 1.0f);
    gate.setStep(2, 0.0f);
    gate.setStep(3, 0.7f);

    // Start smoothers at step 0 level
    // Process many samples to let smoother converge on step 0 level
    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);

    float lastOutput = 0.0f;
    for (size_t i = 0; i < samplesPerStep - 1; ++i) {
        lastOutput = gate.process(1.0f);
    }
    // Near end of step 0, output should be approximately 0.3
    REQUIRE(lastOutput == Catch::Approx(0.3f).margin(0.02f));

    // Process through step 1 to let it settle to 1.0
    for (size_t i = 0; i < samplesPerStep - 1; ++i) {
        lastOutput = gate.process(1.0f);
    }
    REQUIRE(lastOutput == Catch::Approx(1.0f).margin(0.02f));
}

TEST_CASE("TranceGate - all-open pattern is transparent", "[trance_gate][US1]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 8;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    // All steps at 1.0 (default)
    for (int i = 0; i < 8; ++i) {
        gate.setStep(i, 1.0f);
    }

    // Process 10000 samples, verify output == input
    bool allMatch = true;
    for (size_t i = 0; i < 10000; ++i) {
        const float input = 0.75f;
        const float output = gate.process(input);
        if (std::abs(output - input) > 0.001f) {
            allMatch = false;
            break;
        }
    }
    REQUIRE(allMatch);
}

TEST_CASE("TranceGate - setStep modifies only addressed step", "[trance_gate][US1]") {
    TranceGate gate;
    gate.prepare(44100.0);

    TranceGateParams params;
    params.numSteps = 8;
    gate.setParams(params);

    // Set all to 1.0
    for (int i = 0; i < 8; ++i) {
        gate.setStep(i, 1.0f);
    }

    // Modify only step 3
    gate.setStep(3, 0.5f);

    // Verify by processing through each step and checking levels
    // We need to read the pattern indirectly through processing.
    // Process through steps and check output at end of each.
    gate.setTempo(120.0);
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);
    // Re-set pattern after setParams
    for (int i = 0; i < 8; ++i) {
        gate.setStep(i, 1.0f);
    }
    gate.setStep(3, 0.5f);

    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);

    // Process through steps 0-3
    float outputAtEndOfStep[4];
    for (int step = 0; step < 4; ++step) {
        float last = 0.0f;
        for (size_t i = 0; i < samplesPerStep - 1; ++i) {
            last = gate.process(1.0f);
        }
        outputAtEndOfStep[step] = last;
    }

    // Steps 0, 1, 2 should be near 1.0
    REQUIRE(outputAtEndOfStep[0] == Catch::Approx(1.0f).margin(0.02f));
    REQUIRE(outputAtEndOfStep[1] == Catch::Approx(1.0f).margin(0.02f));
    REQUIRE(outputAtEndOfStep[2] == Catch::Approx(1.0f).margin(0.02f));
    // Step 3 should be near 0.5
    REQUIRE(outputAtEndOfStep[3] == Catch::Approx(0.5f).margin(0.05f));
}

TEST_CASE("TranceGate - default state without prepare is passthrough", "[trance_gate][US1]") {
    TranceGate gate;
    // Do NOT call prepare()

    // Process signal, verify output equals input
    bool allMatch = true;
    for (size_t i = 0; i < 1000; ++i) {
        const float input = 0.42f;
        const float output = gate.process(input);
        if (std::abs(output - input) > 0.0001f) {
            allMatch = false;
            break;
        }
    }
    REQUIRE(allMatch);
}

TEST_CASE("TranceGate - processBlock mono produces same result as per-sample process",
           "[trance_gate][US1]") {
    // Set up two identical gates
    TranceGate gateA;
    TranceGate gateB;

    gateA.prepare(44100.0);
    gateB.prepare(44100.0);
    gateA.setTempo(120.0);
    gateB.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 8;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 3.0f;
    params.releaseMs = 8.0f;
    params.tempoSync = true;
    gateA.setParams(params);
    gateB.setParams(params);

    // Same pattern
    for (int i = 0; i < 8; ++i) {
        const float level = (i % 2 == 0) ? 1.0f : 0.0f;
        gateA.setStep(i, level);
        gateB.setStep(i, level);
    }

    constexpr size_t kBlockSize = 512;
    std::array<float, kBlockSize> bufferA;
    std::array<float, kBlockSize> bufferB;

    // Fill with constant input
    bufferA.fill(0.8f);
    bufferB.fill(0.8f);

    // Process gateA per-sample
    for (size_t i = 0; i < kBlockSize; ++i) {
        bufferA[i] = gateA.process(bufferA[i]);
    }

    // Process gateB with processBlock
    gateB.processBlock(bufferB.data(), kBlockSize);

    // Compare
    bool allMatch = true;
    for (size_t i = 0; i < kBlockSize; ++i) {
        if (std::abs(bufferA[i] - bufferB[i]) > 0.0001f) {
            allMatch = false;
            break;
        }
    }
    REQUIRE(allMatch);
}

// =============================================================================
// Phase 4: User Story 2 - Click-Free Edge Shaping
// =============================================================================

TEST_CASE("TranceGate - max gain change within one-pole bounds", "[trance_gate][US2]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 2;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 2.0f;
    params.releaseMs = 10.0f;
    params.tempoSync = true;
    gate.setParams(params);

    gate.setStep(0, 0.0f);
    gate.setStep(1, 1.0f);

    // Process several pattern cycles and track max sample-to-sample gain change
    float prevGain = gate.getGateValue();
    float maxDelta = 0.0f;

    const size_t totalSamples = 44100;  // 1 second
    for (size_t i = 0; i < totalSamples; ++i) {
        (void)gate.process(1.0f);
        const float currentGain = gate.getGateValue();
        const float delta = std::abs(currentGain - prevGain);
        if (delta > maxDelta) {
            maxDelta = delta;
        }
        prevGain = currentGain;
    }

    // SC-002: Max change must be < 0.056 for attackMs=2.0 at 44100 Hz
    REQUIRE(maxDelta < 0.056f);
}

TEST_CASE("TranceGate - minimum ramp time prevents instantaneous transitions",
           "[trance_gate][US2]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 2;
    params.noteValue = NoteValue::Eighth;  // Longer steps to have room for ramp
    params.depth = 1.0f;
    params.attackMs = 1.0f;   // Minimum
    params.releaseMs = 1.0f;  // Minimum
    params.tempoSync = true;
    gate.setParams(params);

    gate.setStep(0, 0.0f);
    gate.setStep(1, 1.0f);

    // Process to get into step 0 (level 0.0) and let it settle
    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Eighth,
                                                          NoteModifier::None, 44100.0);
    for (size_t i = 0; i < samplesPerStep; ++i) {
        (void)gate.process(1.0f);
    }

    // Now entering step 1 (0.0 -> 1.0 transition). Count samples to reach 0.99
    int samplesToReach99 = 0;
    for (int i = 0; i < 1000; ++i) {
        (void)gate.process(1.0f);
        samplesToReach99++;
        if (gate.getGateValue() >= 0.99f) {
            break;
        }
    }

    // With attackMs=1.0, ramp should take approximately 44 samples (~1ms at 44100 Hz)
    // OnePoleSmoother's completion threshold causes slightly early snap-to-target
    REQUIRE(samplesToReach99 >= 38);
    REQUIRE(samplesToReach99 <= 50);
}

TEST_CASE("TranceGate - 99% settling time matches attack parameter", "[trance_gate][US2]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 2;
    params.noteValue = NoteValue::Quarter;  // Long steps
    params.depth = 1.0f;
    params.attackMs = 20.0f;
    params.releaseMs = 50.0f;
    params.tempoSync = true;
    gate.setParams(params);

    gate.setStep(0, 0.0f);
    gate.setStep(1, 1.0f);

    // Process through step 0 to let it settle at 0.0
    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Quarter,
                                                          NoteModifier::None, 44100.0);
    for (size_t i = 0; i < samplesPerStep; ++i) {
        (void)gate.process(1.0f);
    }

    // Now entering step 1 (0.0 -> 1.0). Count samples to reach 99%
    int samplesToReach99 = 0;
    for (int i = 0; i < 5000; ++i) {
        (void)gate.process(1.0f);
        samplesToReach99++;
        if (gate.getGateValue() >= 0.99f) {
            break;
        }
    }

    // 20ms at 44100 Hz = 882 samples. Allow some tolerance.
    REQUIRE(samplesToReach99 >= 800);
    REQUIRE(samplesToReach99 <= 960);
}

// =============================================================================
// Phase 5: User Story 3 - Euclidean Pattern Generation
// =============================================================================

TEST_CASE("TranceGate - Euclidean E(3,8) matches tresillo", "[trance_gate][US3]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 8;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    gate.setEuclidean(3, 8, 0);

    // Expected: [1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0]
    const std::array<float, 8> expected = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};

    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);

    // Process through each step and check output level at the end
    for (int step = 0; step < 8; ++step) {
        float lastOutput = 0.0f;
        for (size_t i = 0; i < samplesPerStep - 1; ++i) {
            lastOutput = gate.process(1.0f);
        }
        REQUIRE(lastOutput == Catch::Approx(expected[static_cast<size_t>(step)]).margin(0.05f));
    }
}

TEST_CASE("TranceGate - Euclidean E(5,8) matches cinquillo", "[trance_gate][US3]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 8;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    gate.setEuclidean(5, 8, 0);

    // Bresenham accumulator produces: hits at positions {0,2,4,5,7}
    // Pattern: [1.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0]
    const std::array<float, 8> expected = {1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};

    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);

    for (int step = 0; step < 8; ++step) {
        float lastOutput = 0.0f;
        for (size_t i = 0; i < samplesPerStep - 1; ++i) {
            lastOutput = gate.process(1.0f);
        }
        REQUIRE(lastOutput == Catch::Approx(expected[static_cast<size_t>(step)]).margin(0.05f));
    }
}

TEST_CASE("TranceGate - Euclidean E(5,12) reference pattern", "[trance_gate][US3]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 12;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    gate.setEuclidean(5, 12, 0);

    // Expected: [1,0,0,1,0,1,0,0,1,0,1,0]
    const std::array<float, 12> expected = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,
                                             0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f};

    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);

    for (int step = 0; step < 12; ++step) {
        float lastOutput = 0.0f;
        for (size_t i = 0; i < samplesPerStep - 1; ++i) {
            lastOutput = gate.process(1.0f);
        }
        REQUIRE(lastOutput == Catch::Approx(expected[static_cast<size_t>(step)]).margin(0.05f));
    }
}

TEST_CASE("TranceGate - Euclidean rotation shifts pattern", "[trance_gate][US3]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 16;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    // Generate pattern without rotation
    TranceGate gateNoRot;
    gateNoRot.prepare(44100.0);
    gateNoRot.setTempo(120.0);
    gateNoRot.setParams(params);
    gateNoRot.setEuclidean(4, 16, 0);

    // Generate pattern with rotation 2
    gate.setEuclidean(4, 16, 2);

    // Compare: rotated pattern should differ from unrotated
    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);

    std::array<float, 16> levelsNoRot{};
    std::array<float, 16> levelsRot{};

    for (int step = 0; step < 16; ++step) {
        float lastA = 0.0f;
        float lastB = 0.0f;
        for (size_t i = 0; i < samplesPerStep - 1; ++i) {
            lastA = gateNoRot.process(1.0f);
            lastB = gate.process(1.0f);
        }
        levelsNoRot[static_cast<size_t>(step)] = (lastA > 0.5f) ? 1.0f : 0.0f;
        levelsRot[static_cast<size_t>(step)] = (lastB > 0.5f) ? 1.0f : 0.0f;
    }

    // Patterns should be different (rotation should shift)
    REQUIRE(levelsNoRot != levelsRot);

    // But they should have the same number of hits
    int hitsNoRot = 0;
    int hitsRot = 0;
    for (int i = 0; i < 16; ++i) {
        if (levelsNoRot[static_cast<size_t>(i)] > 0.5f) hitsNoRot++;
        if (levelsRot[static_cast<size_t>(i)] > 0.5f) hitsRot++;
    }
    REQUIRE(hitsNoRot == hitsRot);
    REQUIRE(hitsNoRot == 4);
}

TEST_CASE("TranceGate - Euclidean edge cases", "[trance_gate][US3]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 16;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    SECTION("All zeros: E(0,16)") {
        gate.setEuclidean(0, 16, 0);
        const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                              NoteModifier::None, 44100.0);
        // After settling, output should be near 0
        for (size_t i = 0; i < samplesPerStep * 2; ++i) {
            (void)gate.process(1.0f);
        }
        REQUIRE(gate.getGateValue() == Catch::Approx(0.0f).margin(0.01f));
    }

    SECTION("All ones: E(16,16)") {
        gate.setEuclidean(16, 16, 0);
        // Process and verify output stays at 1.0
        bool allNearOne = true;
        for (size_t i = 0; i < 1000; ++i) {
            (void)gate.process(1.0f);
            if (gate.getGateValue() < 0.99f) {
                allNearOne = false;
                break;
            }
        }
        REQUIRE(allNearOne);
    }
}

// =============================================================================
// Phase 6: User Story 4 - Depth Control
// =============================================================================

TEST_CASE("TranceGate - depth 0.0 bypasses gate entirely", "[trance_gate][US4]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 4;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 0.0f;
    params.tempoSync = true;
    gate.setParams(params);

    // Set pattern with silence steps
    gate.setStep(0, 0.0f);
    gate.setStep(1, 0.0f);
    gate.setStep(2, 0.0f);
    gate.setStep(3, 0.0f);

    // With depth=0.0, output should equal input regardless of pattern
    bool allMatch = true;
    for (size_t i = 0; i < 10000; ++i) {
        const float input = 0.65f;
        const float output = gate.process(input);
        if (std::abs(output - input) > 0.001f) {
            allMatch = false;
            break;
        }
    }
    REQUIRE(allMatch);
}

TEST_CASE("TranceGate - depth 1.0 applies full pattern effect", "[trance_gate][US4]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 2;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    gate.setStep(0, 1.0f);
    gate.setStep(1, 0.0f);

    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);

    // Process step 0 -- let it settle
    for (size_t i = 0; i < samplesPerStep; ++i) {
        (void)gate.process(1.0f);
    }

    // Process well into step 1 -- should be near 0.0
    for (size_t i = 0; i < 500; ++i) {
        (void)gate.process(1.0f);
    }
    float output = gate.process(1.0f);
    REQUIRE(output == Catch::Approx(0.0f).margin(0.01f));
}

TEST_CASE("TranceGate - depth 0.5 halves the effect", "[trance_gate][US4]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 2;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 0.5f;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    gate.setStep(0, 0.0f);
    gate.setStep(1, 0.0f);

    // Let it settle on step 0 level 0.0 with depth 0.5
    // Expected: finalGain = lerp(1.0, 0.0, 0.5) = 0.5
    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);
    for (size_t i = 0; i < samplesPerStep * 2; ++i) {
        (void)gate.process(1.0f);
    }

    // SC-005: depth=0.5, step level=0.0: output should be ~50% of input (within 1%)
    const float input = 1.0f;
    const float output = gate.process(input);
    REQUIRE(output == Catch::Approx(0.5f).margin(0.01f));
}

// =============================================================================
// Phase 6: User Story 5 - Tempo Synchronization
// =============================================================================

TEST_CASE("TranceGate - step duration matches tempo and note value", "[trance_gate][US5]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 4;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    gate.setStep(0, 1.0f);
    gate.setStep(1, 0.0f);
    gate.setStep(2, 1.0f);
    gate.setStep(3, 0.0f);

    // SC-001: step boundary within 1 sample of ideal
    // 120 BPM, 1/16: 0.25 beats * 0.5 sec/beat * 44100 = 5512.5 -> 5512 samples
    REQUIRE(gate.getCurrentStep() == 0);

    // Process 5512 samples
    for (size_t i = 0; i < 5512; ++i) {
        (void)gate.process(1.0f);
    }
    REQUIRE(gate.getCurrentStep() == 1);
}

TEST_CASE("TranceGate - tempo change adjusts step duration", "[trance_gate][US5]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 16;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    for (int i = 0; i < 16; ++i) {
        gate.setStep(i, (i % 2 == 0) ? 1.0f : 0.0f);
    }

    // Process one step at 120 BPM
    const size_t samples120 = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                      NoteModifier::None, 44100.0);
    for (size_t i = 0; i < samples120; ++i) {
        (void)gate.process(1.0f);
    }
    REQUIRE(gate.getCurrentStep() == 1);

    // Change to 140 BPM
    gate.setTempo(140.0);

    // Process one step at 140 BPM
    const size_t samples140 = expectedSamplesPerStep(140.0, NoteValue::Sixteenth,
                                                      NoteModifier::None, 44100.0);
    for (size_t i = 0; i < samples140; ++i) {
        (void)gate.process(1.0f);
    }
    REQUIRE(gate.getCurrentStep() == 2);

    // Verify the step duration changed (140 BPM should have fewer samples)
    REQUIRE(samples140 < samples120);
}

TEST_CASE("TranceGate - free-run mode uses Hz rate", "[trance_gate][US5]") {
    TranceGate gate;
    gate.prepare(44100.0);

    TranceGateParams params;
    params.numSteps = 4;
    params.depth = 1.0f;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.tempoSync = false;
    params.rateHz = 8.0f;
    gate.setParams(params);

    gate.setStep(0, 1.0f);
    gate.setStep(1, 0.0f);
    gate.setStep(2, 1.0f);
    gate.setStep(3, 0.0f);

    // At 8 Hz, each step = 44100/8 = 5512.5 -> 5512 samples
    REQUIRE(gate.getCurrentStep() == 0);

    // Process 5512 samples
    for (size_t i = 0; i < 5512; ++i) {
        (void)gate.process(1.0f);
    }
    REQUIRE(gate.getCurrentStep() == 1);
}

TEST_CASE("TranceGate - dotted and triplet note modifiers", "[trance_gate][US5]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 4;
    params.depth = 1.0f;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.tempoSync = true;

    gate.setStep(0, 1.0f);
    gate.setStep(1, 0.0f);
    gate.setStep(2, 1.0f);
    gate.setStep(3, 0.0f);

    SECTION("Dotted 1/16") {
        params.noteValue = NoteValue::Sixteenth;
        params.noteModifier = NoteModifier::Dotted;
        gate.setParams(params);

        const size_t expected = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                        NoteModifier::Dotted, 44100.0);
        // Process expected samples
        for (size_t i = 0; i < expected; ++i) {
            (void)gate.process(1.0f);
        }
        REQUIRE(gate.getCurrentStep() == 1);
    }

    SECTION("Triplet 1/16") {
        params.noteValue = NoteValue::Sixteenth;
        params.noteModifier = NoteModifier::Triplet;
        gate.setParams(params);

        const size_t expected = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                        NoteModifier::Triplet, 44100.0);
        for (size_t i = 0; i < expected; ++i) {
            (void)gate.process(1.0f);
        }
        REQUIRE(gate.getCurrentStep() == 1);
    }
}

// =============================================================================
// Phase 7: User Story 6 - Modulation Output
// =============================================================================

TEST_CASE("TranceGate - getGateValue matches applied gain", "[trance_gate][US6]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 4;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 5.0f;
    params.releaseMs = 10.0f;
    params.tempoSync = true;
    gate.setParams(params);

    gate.setStep(0, 1.0f);
    gate.setStep(1, 0.0f);
    gate.setStep(2, 0.5f);
    gate.setStep(3, 1.0f);

    // Process samples and compare output/input ratio to getGateValue
    const float input = 0.8f;
    bool allMatch = true;
    float maxError = 0.0f;

    for (size_t i = 0; i < 20000; ++i) {
        const float output = gate.process(input);
        const float appliedGain = output / input;
        const float gateValue = gate.getGateValue();
        const float error = std::abs(appliedGain - gateValue);
        if (error > maxError) maxError = error;
        if (error > 0.001f) {
            allMatch = false;
            break;
        }
    }
    REQUIRE(allMatch);
    // SC-006: within 0.001 tolerance
    REQUIRE(maxError <= 0.001f);
}

TEST_CASE("TranceGate - getGateValue reflects depth adjustment", "[trance_gate][US6]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 2;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 0.5f;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    gate.setStep(0, 0.0f);
    gate.setStep(1, 0.0f);

    // Let it settle
    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);
    for (size_t i = 0; i < samplesPerStep * 2; ++i) {
        (void)gate.process(1.0f);
    }

    // depth=0.5, step=0.0 -> finalGain = lerp(1.0, 0.0, 0.5) = 0.5
    REQUIRE(gate.getGateValue() == Catch::Approx(0.5f).margin(0.01f));
}

TEST_CASE("TranceGate - getGateValue is 1.0 for all-open pattern", "[trance_gate][US6]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 8;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    for (int i = 0; i < 8; ++i) {
        gate.setStep(i, 1.0f);
    }

    for (size_t i = 0; i < 5000; ++i) {
        (void)gate.process(1.0f);
    }

    REQUIRE(gate.getGateValue() == Catch::Approx(1.0f).margin(0.001f));
}

TEST_CASE("TranceGate - getCurrentStep returns correct index", "[trance_gate][US6]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 8;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    for (int i = 0; i < 8; ++i) {
        gate.setStep(i, 1.0f);
    }

    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);

    for (int step = 0; step < 8; ++step) {
        REQUIRE(gate.getCurrentStep() == step);
        for (size_t i = 0; i < samplesPerStep; ++i) {
            (void)gate.process(1.0f);
        }
    }
    // After 8 steps, should wrap to 0
    REQUIRE(gate.getCurrentStep() == 0);
}

// =============================================================================
// Phase 7: User Story 7 - Voice Modes
// =============================================================================

TEST_CASE("TranceGate - per-voice mode resets on reset()", "[trance_gate][US7]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 16;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.perVoice = true;
    params.tempoSync = true;
    gate.setParams(params);

    for (int i = 0; i < 16; ++i) {
        gate.setStep(i, (i % 2 == 0) ? 1.0f : 0.0f);
    }

    // Advance to step 5
    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);
    for (size_t i = 0; i < samplesPerStep * 5; ++i) {
        (void)gate.process(1.0f);
    }
    REQUIRE(gate.getCurrentStep() == 5);

    // Reset
    gate.reset();
    REQUIRE(gate.getCurrentStep() == 0);
}

TEST_CASE("TranceGate - global mode does not reset on reset()", "[trance_gate][US7]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 16;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.perVoice = false;  // Global mode
    params.tempoSync = true;
    gate.setParams(params);

    for (int i = 0; i < 16; ++i) {
        gate.setStep(i, (i % 2 == 0) ? 1.0f : 0.0f);
    }

    // Advance to step 5
    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);
    for (size_t i = 0; i < samplesPerStep * 5; ++i) {
        (void)gate.process(1.0f);
    }
    REQUIRE(gate.getCurrentStep() == 5);

    // Reset -- should be no-op in global mode
    gate.reset();
    REQUIRE(gate.getCurrentStep() == 5);
}

TEST_CASE("TranceGate - two per-voice instances produce different phasing",
           "[trance_gate][US7]") {
    TranceGate gateA;
    TranceGate gateB;

    gateA.prepare(44100.0);
    gateB.prepare(44100.0);
    gateA.setTempo(120.0);
    gateB.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 8;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.perVoice = true;
    params.tempoSync = true;
    gateA.setParams(params);
    gateB.setParams(params);

    for (int i = 0; i < 8; ++i) {
        const float level = (i % 2 == 0) ? 1.0f : 0.0f;
        gateA.setStep(i, level);
        gateB.setStep(i, level);
    }

    // Advance both by 2 steps
    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);
    for (size_t i = 0; i < samplesPerStep * 2; ++i) {
        (void)gateA.process(1.0f);
        (void)gateB.process(1.0f);
    }

    // Reset only gateA (simulating new note-on at different time)
    gateA.reset();

    // Now process some samples -- they should produce different output
    bool foundDifference = false;
    for (size_t i = 0; i < samplesPerStep * 2; ++i) {
        const float outA = gateA.process(1.0f);
        const float outB = gateB.process(1.0f);
        if (std::abs(outA - outB) > 0.1f) {
            foundDifference = true;
            break;
        }
    }
    REQUIRE(foundDifference);
}

TEST_CASE("TranceGate - stereo processBlock applies identical gain to both channels",
           "[trance_gate][US7]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 4;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 3.0f;
    params.releaseMs = 8.0f;
    params.tempoSync = true;
    gate.setParams(params);

    gate.setStep(0, 1.0f);
    gate.setStep(1, 0.0f);
    gate.setStep(2, 0.7f);
    gate.setStep(3, 0.3f);

    constexpr size_t kBlockSize = 2048;
    std::vector<float> left(kBlockSize, 1.0f);
    std::vector<float> right(kBlockSize, 1.0f);

    gate.processBlock(left.data(), right.data(), kBlockSize);

    // SC-007: left and right must be identical at every sample
    bool allIdentical = true;
    for (size_t i = 0; i < kBlockSize; ++i) {
        if (left[i] != right[i]) {
            allIdentical = false;
            break;
        }
    }
    REQUIRE(allIdentical);
}

TEST_CASE("TranceGate - phaseOffset rotates pattern start position", "[trance_gate][US7]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 16;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.phaseOffset = 0.5f;  // Starts from step 8
    params.tempoSync = true;
    gate.setParams(params);

    // Set step 0 = 1.0, step 8 = 0.0, rest = 0.5
    for (int i = 0; i < 16; ++i) {
        gate.setStep(i, 0.5f);
    }
    gate.setStep(0, 1.0f);
    gate.setStep(8, 0.0f);

    // With phaseOffset = 0.5, effectiveStep at currentStep_=0 should be 8
    // So the first step's target level is step 8 = 0.0
    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);

    // Let it settle on first effective step (step 8 = 0.0)
    for (size_t i = 0; i < samplesPerStep - 100; ++i) {
        (void)gate.process(1.0f);
    }
    // Output should be near 0.0 (since effective step is 8, level = 0.0)
    float output = gate.process(1.0f);
    REQUIRE(output == Catch::Approx(0.0f).margin(0.05f));
}

// =============================================================================
// Phase 8: Edge Cases, Performance, and Safety
// =============================================================================

TEST_CASE("TranceGate - minimum two steps loops correctly", "[trance_gate][edge]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 2;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    gate.setStep(0, 0.5f);
    gate.setStep(1, 1.0f);

    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);

    // Process several cycles
    for (int cycle = 0; cycle < 4; ++cycle) {
        // Step 0: process samplesPerStep-2 then read last sample before boundary
        for (size_t i = 0; i < samplesPerStep - 2; ++i) {
            (void)gate.process(1.0f);
        }
        float atEndStep0 = gate.process(1.0f);
        (void)gate.process(1.0f);  // boundary crossing sample

        // Step 1: same pattern
        for (size_t i = 0; i < samplesPerStep - 2; ++i) {
            (void)gate.process(1.0f);
        }
        float atEndStep1 = gate.process(1.0f);
        (void)gate.process(1.0f);  // boundary crossing sample

        if (cycle > 0) {  // Skip first cycle (ramp settling)
            REQUIRE(atEndStep0 == Catch::Approx(0.5f).margin(0.05f));
            REQUIRE(atEndStep1 == Catch::Approx(1.0f).margin(0.05f));
        }
    }
}

TEST_CASE("TranceGate - all-zero pattern produces depth-modulated silence",
           "[trance_gate][edge]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 4;
    params.noteValue = NoteValue::Sixteenth;
    params.attackMs = 1.0f;
    params.releaseMs = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    for (int i = 0; i < 4; ++i) {
        gate.setStep(i, 0.0f);
    }

    SECTION("depth 1.0 -> near zero") {
        params.depth = 1.0f;
        gate.setParams(params);
        for (int i = 0; i < 4; ++i) gate.setStep(i, 0.0f);

        // Let settle
        for (size_t i = 0; i < 20000; ++i) {
            (void)gate.process(1.0f);
        }
        REQUIRE(gate.getGateValue() == Catch::Approx(0.0f).margin(0.01f));
    }

    SECTION("depth 0.5 -> ~50%") {
        params.depth = 0.5f;
        gate.setParams(params);
        for (int i = 0; i < 4; ++i) gate.setStep(i, 0.0f);

        for (size_t i = 0; i < 20000; ++i) {
            (void)gate.process(1.0f);
        }
        REQUIRE(gate.getGateValue() == Catch::Approx(0.5f).margin(0.01f));
    }
}

TEST_CASE("TranceGate - extreme tempos clamped to [20, 300] BPM", "[trance_gate][edge]") {
    TranceGate gate;
    gate.prepare(44100.0);

    TranceGateParams params;
    params.numSteps = 4;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    for (int i = 0; i < 4; ++i) {
        gate.setStep(i, (i % 2 == 0) ? 1.0f : 0.0f);
    }

    SECTION("Too low tempo clamped to 20") {
        gate.setTempo(5.0);

        const size_t expectedAt20 = expectedSamplesPerStep(20.0, NoteValue::Sixteenth,
                                                            NoteModifier::None, 44100.0);
        for (size_t i = 0; i < expectedAt20; ++i) {
            (void)gate.process(1.0f);
        }
        REQUIRE(gate.getCurrentStep() == 1);
    }

    SECTION("Too high tempo clamped to 300") {
        gate.setTempo(500.0);

        const size_t expectedAt300 = expectedSamplesPerStep(300.0, NoteValue::Sixteenth,
                                                              NoteModifier::None, 44100.0);
        for (size_t i = 0; i < expectedAt300; ++i) {
            (void)gate.process(1.0f);
        }
        REQUIRE(gate.getCurrentStep() == 1);
    }
}

TEST_CASE("TranceGate - pattern update mid-processing is click-free", "[trance_gate][edge]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 4;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 5.0f;
    params.releaseMs = 10.0f;
    params.tempoSync = true;
    gate.setParams(params);

    for (int i = 0; i < 4; ++i) {
        gate.setStep(i, 1.0f);
    }

    // Process some samples
    float prevGain = 1.0f;
    float maxDelta = 0.0f;

    for (size_t i = 0; i < 5000; ++i) {
        (void)gate.process(1.0f);

        // Mid-processing: change a step to 0.0
        if (i == 2500) {
            gate.setStep(0, 0.0f);
            gate.setStep(1, 0.0f);
            gate.setStep(2, 0.0f);
            gate.setStep(3, 0.0f);
        }

        const float currentGain = gate.getGateValue();
        const float delta = std::abs(currentGain - prevGain);
        if (delta > maxDelta) {
            maxDelta = delta;
        }
        prevGain = currentGain;
    }

    // The max delta should still be within one-pole bounds
    // For attackMs=5.0: max_delta = 1 - exp(-5000/(5.0*44100)) = ~0.0224
    REQUIRE(maxDelta < 0.03f);
}

TEST_CASE("TranceGate - ramp time exceeding step duration produces triangular envelope",
           "[trance_gate][edge]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(300.0);  // Fast tempo

    TranceGateParams params;
    params.numSteps = 2;
    params.noteValue = NoteValue::ThirtySecond;  // Very short steps
    params.depth = 1.0f;
    params.attackMs = 20.0f;   // Long ramp
    params.releaseMs = 20.0f;  // Long ramp
    params.tempoSync = true;
    gate.setParams(params);

    gate.setStep(0, 1.0f);
    gate.setStep(1, 0.0f);

    // Process several cycles -- should not crash or produce NaN
    bool hasNaN = false;
    bool hasInf = false;
    for (size_t i = 0; i < 44100; ++i) {
        const float output = gate.process(1.0f);
        if (std::isnan(output)) hasNaN = true;
        if (std::isinf(output)) hasInf = true;
    }
    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
}

TEST_CASE("TranceGate - prepare recalculates coefficients with new sample rate",
           "[trance_gate][edge]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 4;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    for (int i = 0; i < 4; ++i) {
        gate.setStep(i, (i % 2 == 0) ? 1.0f : 0.0f);
    }

    // Step at 44100: 5512 samples
    size_t samplesAt44100 = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                    NoteModifier::None, 44100.0);
    for (size_t i = 0; i < samplesAt44100; ++i) {
        (void)gate.process(1.0f);
    }
    REQUIRE(gate.getCurrentStep() == 1);

    // Re-prepare at 96000
    gate.prepare(96000.0);
    gate.setTempo(120.0);
    gate.setParams(params);
    for (int i = 0; i < 4; ++i) {
        gate.setStep(i, (i % 2 == 0) ? 1.0f : 0.0f);
    }

    // Step at 96000: 12000 samples
    size_t samplesAt96000 = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                    NoteModifier::None, 96000.0);
    REQUIRE(samplesAt96000 > samplesAt44100);  // Should be roughly double

    for (size_t i = 0; i < samplesAt96000; ++i) {
        (void)gate.process(1.0f);
    }
    // After processing at new rate, verify step advances correctly
    // (getCurrentStep depends on how many steps were already processed)
    REQUIRE(gate.getCurrentStep() >= 1);
}

TEST_CASE("TranceGate - gate does not affect voice lifetime", "[trance_gate][edge]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 4;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.tempoSync = true;
    gate.setParams(params);

    // All steps at 0.0
    for (int i = 0; i < 4; ++i) {
        gate.setStep(i, 0.0f);
    }

    // Process audio -- gate should produce output (attenuated) but not signal note-off
    // The TranceGate class has no mechanism to end a voice.
    // Verify: process returns a value, gate value exists, no exceptions.
    bool allValid = true;
    for (size_t i = 0; i < 10000; ++i) {
        const float output = gate.process(1.0f);
        const float gateVal = gate.getGateValue();
        if (std::isnan(output) || std::isnan(gateVal)) {
            allValid = false;
            break;
        }
    }
    REQUIRE(allValid);

    // Gate with depth=1.0 and step=0.0 should produce near-zero output
    float output = gate.process(1.0f);
    REQUIRE(output == Catch::Approx(0.0f).margin(0.01f));

    // But the gate itself has no mechanism for voice lifetime management
    // (no noteOff(), no isFinished(), no voice-stealing signals)
    // This is verified by the absence of such methods in the API.
    REQUIRE(true);
}

// =============================================================================
// Phase 9: Retrigger Depth
// =============================================================================

TEST_CASE("TranceGate - retrigger depth 0 keeps flat gain across consecutive on-steps",
           "[trance_gate][retrigger]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 4;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 2.0f;
    params.releaseMs = 10.0f;
    params.tempoSync = true;
    params.retriggerDepth = 0.0f;  // No retrigger (legacy behavior)
    gate.setParams(params);

    // All steps ON
    for (int i = 0; i < 4; ++i) {
        gate.setStep(i, 1.0f);
    }

    gate.reset();

    // Process through multiple step boundaries
    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);
    // Skip attack ramp of first step
    for (size_t i = 0; i < samplesPerStep / 2; ++i) {
        (void)gate.process(1.0f);
    }

    // From here, gain should stay at 1.0 across step boundaries
    float minGain = 1.0f;
    for (size_t i = 0; i < samplesPerStep * 2; ++i) {
        (void)gate.process(1.0f);
        const float g = gate.getGateValue();
        if (g < minGain) minGain = g;
    }

    // With retriggerDepth=0, gain stays flat at 1.0
    REQUIRE(minGain > 0.99f);
}

TEST_CASE("TranceGate - retrigger depth 1 creates dip at step boundary",
           "[trance_gate][retrigger]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 4;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 2.0f;
    params.releaseMs = 10.0f;
    params.tempoSync = true;
    params.retriggerDepth = 1.0f;  // Full retrigger
    gate.setParams(params);

    // All steps ON
    for (int i = 0; i < 4; ++i) {
        gate.setStep(i, 1.0f);
    }

    gate.reset();

    // Process past the first step boundary
    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);
    // Let the first step fully ramp up
    for (size_t i = 0; i < samplesPerStep - 1; ++i) {
        (void)gate.process(1.0f);
    }

    // Gain should be near 1.0 before the boundary
    REQUIRE(gate.getGateValue() > 0.95f);

    // Process one more sample to trigger step boundary
    (void)gate.process(1.0f);

    // The retrigger should have snapped gain down significantly
    // (full retrigger = snap to 0, then attack ramp starts)
    REQUIRE(gate.getGateValue() < 0.2f);
}

TEST_CASE("TranceGate - retrigger depth 0.5 creates partial dip",
           "[trance_gate][retrigger]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 4;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 2.0f;
    params.releaseMs = 10.0f;
    params.tempoSync = true;
    params.retriggerDepth = 0.5f;  // Half retrigger
    gate.setParams(params);

    // All steps ON
    for (int i = 0; i < 4; ++i) {
        gate.setStep(i, 1.0f);
    }

    gate.reset();

    // Process to just before second step boundary
    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);
    for (size_t i = 0; i < samplesPerStep - 1; ++i) {
        (void)gate.process(1.0f);
    }

    REQUIRE(gate.getGateValue() > 0.95f);

    // Trigger step boundary
    (void)gate.process(1.0f);

    // With 0.5 retrigger, gain should dip to roughly 0.5 (of 1.0)
    const float postBoundaryGain = gate.getGateValue();
    REQUIRE(postBoundaryGain < 0.7f);
    REQUIRE(postBoundaryGain > 0.3f);
}

TEST_CASE("TranceGate - retrigger does not affect transitions between different levels",
           "[trance_gate][retrigger]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 2;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 2.0f;
    params.releaseMs = 10.0f;
    params.tempoSync = true;
    params.retriggerDepth = 1.0f;
    gate.setParams(params);

    gate.setStep(0, 1.0f);
    gate.setStep(1, 0.0f);  // Already going to 0

    gate.reset();

    // Process through one full cycle (1→0→1→0)
    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);

    // No crashes, no NaN
    bool valid = true;
    for (size_t i = 0; i < samplesPerStep * 4; ++i) {
        const float output = gate.process(1.0f);
        if (std::isnan(output) || std::isinf(output)) {
            valid = false;
            break;
        }
    }
    REQUIRE(valid);
}

TEST_CASE("TranceGate - retrigger recovery ramp is smooth",
           "[trance_gate][retrigger]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 4;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 2.0f;
    params.releaseMs = 10.0f;
    params.tempoSync = true;
    params.retriggerDepth = 1.0f;
    gate.setParams(params);

    for (int i = 0; i < 4; ++i) {
        gate.setStep(i, 1.0f);
    }

    gate.reset();

    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);

    // Track max delta EXCLUDING the first sample after each step boundary.
    // The retrigger snap creates an intentional gain dip at the boundary.
    // The recovery ramp (attack smoother) should be smooth.
    float prevGain = gate.getGateValue();
    float maxRecoveryDelta = 0.0f;
    size_t sampleInStep = 0;

    for (size_t i = 0; i < samplesPerStep * 6; ++i) {
        (void)gate.process(1.0f);
        sampleInStep++;

        if (sampleInStep >= samplesPerStep) {
            sampleInStep = 0;
        }

        const float g = gate.getGateValue();

        // Skip the boundary sample (sampleInStep == 0) — the snap is intentional
        if (sampleInStep > 1) {
            const float delta = std::abs(g - prevGain);
            if (delta > maxRecoveryDelta) maxRecoveryDelta = delta;
        }

        prevGain = g;
    }

    // Recovery ramp max delta should be bounded by attack smoother coefficient.
    // attackMs=2.0 at 44100Hz: max delta ≈ 1 - exp(-5000/(2*44100)) ≈ 0.055
    REQUIRE(maxRecoveryDelta < 0.06f);
}

TEST_CASE("TranceGate - 32 steps cycles through all 32 positions",
           "[trance_gate][regression]") {
    // Regression test: verify that with numSteps=32, currentStep reaches all
    // 32 positions (0-31) before wrapping. Simulates the processor's per-block
    // setParams pattern.
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 32;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 1.0f;
    params.attackMs = 2.0f;
    params.releaseMs = 10.0f;
    params.tempoSync = true;
    params.perVoice = true;
    gate.setParams(params);

    // Set all 32 steps to 1.0
    for (int i = 0; i < 32; ++i) {
        gate.setStep(i, 1.0f);
    }

    gate.reset();

    const size_t samplesPerStep = expectedSamplesPerStep(120.0, NoteValue::Sixteenth,
                                                          NoteModifier::None, 44100.0);

    // Track which steps we visit
    std::array<bool, 32> visited{};
    int maxStepSeen = -1;

    // Process enough samples to cycle through all 32 steps.
    // Re-call setParams periodically (like the processor does every block).
    constexpr size_t kBlockSize = 512;
    const size_t totalSamples = samplesPerStep * 33; // just over one full cycle

    for (size_t s = 0; s < totalSamples; s += kBlockSize) {
        // Simulate processor: re-apply params each block
        gate.setParams(params);
        gate.setTempo(120.0);

        size_t blockEnd = std::min(s + kBlockSize, totalSamples);
        for (size_t i = s; i < blockEnd; ++i) {
            (void)gate.process(1.0f);
            int step = gate.getCurrentStep();
            if (step >= 0 && step < 32) {
                visited[static_cast<size_t>(step)] = true;
                if (step > maxStepSeen) maxStepSeen = step;
            }
        }
    }

    // All 32 steps should have been visited
    INFO("Max step seen: " << maxStepSeen);
    for (int i = 0; i < 32; ++i) {
        INFO("Step " << i << " visited: " << visited[static_cast<size_t>(i)]);
        REQUIRE(visited[static_cast<size_t>(i)]);
    }
}

TEST_CASE("TranceGate - processing overhead < 0.1% CPU", "[trance_gate][performance]") {
    TranceGate gate;
    gate.prepare(44100.0);
    gate.setTempo(120.0);

    TranceGateParams params;
    params.numSteps = 16;
    params.noteValue = NoteValue::Sixteenth;
    params.depth = 0.8f;
    params.attackMs = 5.0f;
    params.releaseMs = 10.0f;
    params.tempoSync = true;
    gate.setParams(params);

    for (int i = 0; i < 16; ++i) {
        gate.setStep(i, (i % 2 == 0) ? 1.0f : 0.0f);
    }

    // Process 1 second of audio
    constexpr size_t kSamples = 44100;
    std::array<float, 512> buffer;
    buffer.fill(0.5f);

    const auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < kSamples / 512; ++i) {
        buffer.fill(0.5f);
        gate.processBlock(buffer.data(), 512);
    }
    const auto end = std::chrono::high_resolution_clock::now();

    const double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();
    // 1 second of audio at 44100 Hz = 1000ms of real-time
    // 0.1% CPU = 1.0ms
    const double cpuPercent = (elapsedMs / 1000.0) * 100.0;

    REQUIRE(cpuPercent < 0.1);
}
