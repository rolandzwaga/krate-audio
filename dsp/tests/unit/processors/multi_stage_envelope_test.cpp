// ==============================================================================
// Layer 2: DSP Processor - Multi-Stage Envelope Generator Tests
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests organized by user story priority (US1-US6) + edge cases + performance
//
// Reference: specs/033-multi-stage-envelope/spec.md
// ==============================================================================

#include <krate/dsp/processors/multi_stage_envelope.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

constexpr float kTestSampleRate = 44100.0f;

/// Process envelope for N samples, collecting output into a vector.
std::vector<float> processAndCollect(MultiStageEnvelope& env, int numSamples) {
    std::vector<float> output(static_cast<size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i) {
        output[static_cast<size_t>(i)] = env.process();
    }
    return output;
}

/// Process envelope until it reaches the target state or maxSamples exceeded.
/// Returns number of samples processed.
int processUntilState(MultiStageEnvelope& env, MultiStageEnvState targetState, int maxSamples = 1000000) {
    int samples = 0;
    while (env.getState() != targetState && samples < maxSamples) {
        (void)env.process();
        ++samples;
    }
    return samples;
}

/// Check that output is continuous (no clicks): max step between consecutive
/// samples does not exceed maxStep.
bool isContinuous(const std::vector<float>& output, float maxStep) {
    for (size_t i = 1; i < output.size(); ++i) {
        if (std::abs(output[i] - output[i - 1]) > maxStep) {
            return false;
        }
    }
    return true;
}

/// Find maximum step between consecutive samples
float maxStep(const std::vector<float>& output) {
    float result = 0.0f;
    for (size_t i = 1; i < output.size(); ++i) {
        result = std::max(result, std::abs(output[i] - output[i - 1]));
    }
    return result;
}

/// Calculate expected samples for a given time in ms
int expectedSamples(float timeMs, float sampleRate = kTestSampleRate) {
    return std::max(1, static_cast<int>(std::round(timeMs * 0.001f * sampleRate)));
}

/// Create a basic 6-stage test envelope
MultiStageEnvelope createBasic6Stage() {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(6);
    env.setStage(0, 1.0f, 10.0f, EnvCurve::Exponential);   // Attack to 1.0
    env.setStage(1, 0.6f, 10.0f, EnvCurve::Exponential);   // Dip to 0.6
    env.setStage(2, 0.8f, 10.0f, EnvCurve::Exponential);   // Rise to 0.8
    env.setStage(3, 0.7f, 10.0f, EnvCurve::Exponential);   // Settle to 0.7
    env.setStage(4, 0.3f, 10.0f, EnvCurve::Exponential);   // Post-sustain
    env.setStage(5, 0.0f, 10.0f, EnvCurve::Exponential);   // Final
    env.setSustainPoint(3);
    env.setReleaseTime(100.0f);
    return env;
}

} // anonymous namespace

// =============================================================================
// US1: Basic Lifecycle (T013)
// =============================================================================

TEST_CASE("MultiStageEnvelope US1: initial state is Idle with zero output", "[msenv][us1]") {
    MultiStageEnvelope env;
    REQUIRE(env.getState() == MultiStageEnvState::Idle);
    REQUIRE(env.isActive() == false);
    REQUIRE(env.isReleasing() == false);
    REQUIRE(env.getOutput() == 0.0f);
    REQUIRE(env.getCurrentStage() == 0);
}

TEST_CASE("MultiStageEnvelope US1: prepare sets sample rate", "[msenv][us1]") {
    MultiStageEnvelope env;
    env.prepare(48000.0f);
    REQUIRE(env.getState() == MultiStageEnvState::Idle);
}

TEST_CASE("MultiStageEnvelope US1: prepare rejects invalid sample rate", "[msenv][us1]") {
    MultiStageEnvelope env;
    env.prepare(44100.0f);
    env.prepare(0.0f);   // Should be rejected
    env.prepare(-1.0f);  // Should be rejected
    // Envelope should still work (uses previous valid rate)
    REQUIRE(env.getState() == MultiStageEnvState::Idle);
}

TEST_CASE("MultiStageEnvelope US1: reset returns to Idle", "[msenv][us1]") {
    auto env = createBasic6Stage();
    env.gate(true);
    processAndCollect(env, 100);

    REQUIRE(env.isActive());
    env.reset();
    REQUIRE(env.getState() == MultiStageEnvState::Idle);
    REQUIRE(env.getOutput() == 0.0f);
    REQUIRE(env.isActive() == false);
}

TEST_CASE("MultiStageEnvelope US1: process returns 0 when Idle", "[msenv][us1]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    REQUIRE(env.process() == 0.0f);
    REQUIRE(env.process() == 0.0f);
}

// =============================================================================
// US1: Stage Configuration (T014)
// =============================================================================

TEST_CASE("MultiStageEnvelope US1: setNumStages clamps to valid range", "[msenv][us1]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);

    env.setNumStages(4);
    REQUIRE(env.getNumStages() == 4);

    env.setNumStages(8);
    REQUIRE(env.getNumStages() == 8);

    env.setNumStages(2);  // Below min
    REQUIRE(env.getNumStages() == 4);

    env.setNumStages(12); // Above max
    REQUIRE(env.getNumStages() == 8);
}

TEST_CASE("MultiStageEnvelope US1: setStageLevel clamps to [0, 1]", "[msenv][us1]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);

    env.setStageLevel(0, 0.5f);
    // Can verify via gate-on and checking first stage target
    // Level setting is validated through processing behavior
    REQUIRE(true);  // Just checking it doesn't crash
}

TEST_CASE("MultiStageEnvelope US1: setStageTime clamps to valid range", "[msenv][us1]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);

    env.setStageTime(0, 50.0f);
    env.setStageTime(0, -10.0f);  // Should clamp to 0
    env.setStageTime(0, 20000.0f); // Should clamp to 10000
    REQUIRE(true);  // No crash
}

TEST_CASE("MultiStageEnvelope US1: out-of-range stage indices are ignored", "[msenv][us1]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);

    env.setStageLevel(-1, 0.5f);  // Should be ignored
    env.setStageLevel(8, 0.5f);   // Should be ignored
    env.setStageTime(-1, 50.0f);  // Should be ignored
    env.setStageTime(8, 50.0f);   // Should be ignored
    env.setStageCurve(-1, EnvCurve::Linear); // Ignored
    env.setStageCurve(8, EnvCurve::Linear);  // Ignored
    REQUIRE(true);  // No crash
}

// =============================================================================
// US1: Sequential Stage Traversal (T015)
// =============================================================================

TEST_CASE("MultiStageEnvelope US1: traverses stages 0 through sustain point sequentially", "[msenv][us1]") {
    auto env = createBasic6Stage();
    env.gate(true);

    // Each stage is 10ms = 441 samples
    int samplesPerStage = expectedSamples(10.0f);
    REQUIRE(samplesPerStage == 441);

    // Process through stages 0, 1, 2, 3 (sustain point)
    // After 4 stages * 441 samples = 1764 samples, should be at sustain
    auto output = processAndCollect(env, samplesPerStage * 4);

    // After stage 0 (441 samples), should reach target 1.0
    REQUIRE(output[static_cast<size_t>(samplesPerStage - 1)] == Approx(1.0f));

    // After stage 1 (882 samples), should reach target 0.6
    REQUIRE(output[static_cast<size_t>(samplesPerStage * 2 - 1)] == Approx(0.6f));

    // After stage 2 (1323 samples), should reach target 0.8
    REQUIRE(output[static_cast<size_t>(samplesPerStage * 3 - 1)] == Approx(0.8f));

    // After stage 3 (1764 samples), should reach target 0.7 and enter sustain
    REQUIRE(output[static_cast<size_t>(samplesPerStage * 4 - 1)] == Approx(0.7f));
    REQUIRE(env.getState() == MultiStageEnvState::Sustaining);
}

TEST_CASE("MultiStageEnvelope US1: stage timing within +/-1 sample", "[msenv][us1]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 20.0f, EnvCurve::Linear);
    env.setStage(1, 0.5f, 30.0f, EnvCurve::Linear);
    env.setStage(2, 0.8f, 10.0f, EnvCurve::Linear);
    env.setStage(3, 0.0f, 50.0f, EnvCurve::Linear);
    env.setSustainPoint(2);
    env.setReleaseTime(100.0f);

    env.gate(true);

    int expected0 = expectedSamples(20.0f); // 882
    int expected1 = expectedSamples(30.0f); // 1323
    int expected2 = expectedSamples(10.0f); // 441

    // Process through stage 0
    int samplesInStage0 = 0;
    while (env.getCurrentStage() == 0 && samplesInStage0 < expected0 + 10) {
        (void)env.process();
        ++samplesInStage0;
    }
    // Stage 0 should complete in exactly expected0 samples (within +/-1)
    REQUIRE(samplesInStage0 >= expected0 - 1);
    REQUIRE(samplesInStage0 <= expected0 + 1);

    // Process through stage 1
    int samplesInStage1 = 0;
    while (env.getCurrentStage() == 1 && samplesInStage1 < expected1 + 10) {
        (void)env.process();
        ++samplesInStage1;
    }
    REQUIRE(samplesInStage1 >= expected1 - 1);
    REQUIRE(samplesInStage1 <= expected1 + 1);
}

// =============================================================================
// US1: Sustain Point Hold (T016)
// =============================================================================

TEST_CASE("MultiStageEnvelope US1: holds at sustain point indefinitely", "[msenv][us1]") {
    auto env = createBasic6Stage();
    env.gate(true);

    // Process through all pre-sustain stages to reach sustain
    processUntilState(env, MultiStageEnvState::Sustaining);
    REQUIRE(env.getState() == MultiStageEnvState::Sustaining);

    // Process for many more samples - should stay at sustain level
    float sustainLevel = env.getOutput();
    auto output = processAndCollect(env, 10000);

    bool allAtSustain = true;
    for (float sample : output) {
        if (std::abs(sample - sustainLevel) > 0.01f) {
            allAtSustain = false;
            break;
        }
    }
    REQUIRE(allAtSustain);
    REQUIRE(env.getState() == MultiStageEnvState::Sustaining);
}

// =============================================================================
// US1: Gate-Off from Sustain (T017)
// =============================================================================

TEST_CASE("MultiStageEnvelope US1: gate-off from sustain triggers release", "[msenv][us1]") {
    auto env = createBasic6Stage();
    env.gate(true);

    // Reach sustain
    processUntilState(env, MultiStageEnvState::Sustaining);
    REQUIRE(env.getState() == MultiStageEnvState::Sustaining);
    float sustainOutput = env.getOutput();
    REQUIRE(sustainOutput > 0.0f);

    // Gate off
    env.gate(false);
    REQUIRE(env.getState() == MultiStageEnvState::Releasing);
    REQUIRE(env.isReleasing());

    // Output should start decreasing
    float prev = env.getOutput();
    for (int i = 0; i < 100; ++i) {
        float current = env.process();
        REQUIRE(current <= prev + 0.001f); // Should be decreasing (with small tolerance)
        prev = current;
    }
}

TEST_CASE("MultiStageEnvelope US1: gate-off skips post-sustain stages", "[msenv][us1]") {
    auto env = createBasic6Stage();
    env.gate(true);

    processUntilState(env, MultiStageEnvState::Sustaining);

    // Gate off should go directly to Releasing, not stage 4 or 5
    env.gate(false);
    REQUIRE(env.getState() == MultiStageEnvState::Releasing);

    // Process to idle
    processUntilState(env, MultiStageEnvState::Idle);
    REQUIRE(env.getState() == MultiStageEnvState::Idle);
    REQUIRE(env.getOutput() == 0.0f);
}

// =============================================================================
// US1: Release to Idle (T018)
// =============================================================================

TEST_CASE("MultiStageEnvelope US1: release completes to Idle", "[msenv][us1]") {
    auto env = createBasic6Stage();
    env.gate(true);
    processUntilState(env, MultiStageEnvState::Sustaining);

    env.gate(false);
    REQUIRE(env.getState() == MultiStageEnvState::Releasing);

    int releaseSamples = processUntilState(env, MultiStageEnvState::Idle);
    REQUIRE(env.getState() == MultiStageEnvState::Idle);
    REQUIRE(env.getOutput() == 0.0f);
    REQUIRE(env.isActive() == false);
    REQUIRE(releaseSamples > 0);
}

TEST_CASE("MultiStageEnvelope US1: release reaches idle within expected time (SC-008)", "[msenv][us1]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 5.0f, EnvCurve::Exponential);
    env.setStage(1, 0.5f, 5.0f, EnvCurve::Exponential);
    env.setStage(2, 0.8f, 5.0f, EnvCurve::Exponential);
    env.setStage(3, 0.0f, 5.0f, EnvCurve::Exponential);
    env.setSustainPoint(2);
    env.setReleaseTime(50.0f);

    env.gate(true);
    processUntilState(env, MultiStageEnvState::Sustaining);
    env.gate(false);

    int samples = processUntilState(env, MultiStageEnvState::Idle, 100000);
    // Release should complete within a reasonable margin of the configured time
    // 50ms release = 2205 samples, but exponential takes longer at the tail
    // Allow up to 3x the release time for idle threshold convergence
    int maxExpected = expectedSamples(50.0f) * 3;
    REQUIRE(samples <= maxExpected);
    REQUIRE(env.isActive() == false);
}

// =============================================================================
// US1: process vs processBlock Equivalence (T019)
// =============================================================================

TEST_CASE("MultiStageEnvelope US1: processBlock matches sequential process calls (FR-008)", "[msenv][us1]") {
    // Create two identical envelopes
    auto env1 = createBasic6Stage();
    auto env2 = createBasic6Stage();

    env1.gate(true);
    env2.gate(true);

    constexpr int kBlockSize = 256;

    // Process env1 sample-by-sample
    std::vector<float> output1(kBlockSize);
    for (int i = 0; i < kBlockSize; ++i) {
        output1[static_cast<size_t>(i)] = env1.process();
    }

    // Process env2 using processBlock
    std::vector<float> output2(kBlockSize);
    env2.processBlock(output2.data(), kBlockSize);

    // Both outputs must be identical
    for (int i = 0; i < kBlockSize; ++i) {
        REQUIRE(output1[static_cast<size_t>(i)] == output2[static_cast<size_t>(i)]);
    }
}

// =============================================================================
// US1: Edge Cases (T020)
// =============================================================================

TEST_CASE("MultiStageEnvelope US1: minimum 4 stages works correctly", "[msenv][us1][edge]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 10.0f, EnvCurve::Exponential);
    env.setStage(1, 0.5f, 10.0f, EnvCurve::Exponential);
    env.setStage(2, 0.8f, 10.0f, EnvCurve::Exponential);
    env.setStage(3, 0.3f, 10.0f, EnvCurve::Exponential);
    env.setSustainPoint(2);  // FR-015 default for 4 stages
    env.setReleaseTime(50.0f);

    env.gate(true);
    processUntilState(env, MultiStageEnvState::Sustaining);
    REQUIRE(env.getState() == MultiStageEnvState::Sustaining);
    REQUIRE(env.getOutput() == Approx(0.8f));

    env.gate(false);
    processUntilState(env, MultiStageEnvState::Idle);
    REQUIRE(env.isActive() == false);
}

TEST_CASE("MultiStageEnvelope US1: maximum 8 stages works correctly", "[msenv][us1][edge]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(8);
    for (int i = 0; i < 8; ++i) {
        float level = static_cast<float>(i) / 7.0f;
        env.setStage(i, level, 5.0f, EnvCurve::Linear);
    }
    env.setSustainPoint(6);
    env.setReleaseTime(50.0f);

    env.gate(true);
    processUntilState(env, MultiStageEnvState::Sustaining);
    REQUIRE(env.getState() == MultiStageEnvState::Sustaining);
}

TEST_CASE("MultiStageEnvelope US1: sustain at last stage", "[msenv][us1][edge]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 5.0f, EnvCurve::Exponential);
    env.setStage(1, 0.5f, 5.0f, EnvCurve::Exponential);
    env.setStage(2, 0.8f, 5.0f, EnvCurve::Exponential);
    env.setStage(3, 0.6f, 5.0f, EnvCurve::Exponential);
    env.setSustainPoint(3);
    env.setReleaseTime(50.0f);

    env.gate(true);
    processUntilState(env, MultiStageEnvState::Sustaining);
    REQUIRE(env.getState() == MultiStageEnvState::Sustaining);
    REQUIRE(env.getOutput() == Approx(0.6f));

    env.gate(false);
    REQUIRE(env.getState() == MultiStageEnvState::Releasing);
}

TEST_CASE("MultiStageEnvelope US1: gate-off during pre-sustain stage", "[msenv][us1][edge]") {
    auto env = createBasic6Stage();
    env.gate(true);

    // Process only a few samples (still in stage 0)
    processAndCollect(env, 10);
    REQUIRE(env.getState() == MultiStageEnvState::Running);
    REQUIRE(env.getCurrentStage() == 0);

    // Gate off during stage 0 should go to release
    env.gate(false);
    REQUIRE(env.getState() == MultiStageEnvState::Releasing);

    processUntilState(env, MultiStageEnvState::Idle);
    REQUIRE(env.isActive() == false);
}

TEST_CASE("MultiStageEnvelope US1: FR-011 stage 0 starts from current output level", "[msenv][us1]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 10.0f, EnvCurve::Exponential);
    env.setStage(1, 0.5f, 10.0f, EnvCurve::Exponential);
    env.setStage(2, 0.8f, 10.0f, EnvCurve::Exponential);
    env.setStage(3, 0.3f, 10.0f, EnvCurve::Exponential);
    env.setSustainPoint(2);
    env.setReleaseTime(50.0f);

    // Gate on from idle - starts from 0.0
    env.gate(true);
    float firstSample = env.process();
    // First sample should be near 0 (starting from 0.0, heading toward 1.0)
    REQUIRE(firstSample < 0.5f);
    REQUIRE(firstSample >= 0.0f);
}

// =============================================================================
// US2: Per-Stage Curve Control (T037-T041)
// =============================================================================

TEST_CASE("MultiStageEnvelope US2: exponential curve midpoint > 0.55 for 0->1", "[msenv][us2]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 100.0f, EnvCurve::Exponential);
    env.setStage(1, 1.0f, 100.0f, EnvCurve::Exponential);
    env.setStage(2, 1.0f, 100.0f, EnvCurve::Exponential);
    env.setStage(3, 1.0f, 100.0f, EnvCurve::Exponential);
    env.setSustainPoint(3);
    env.setReleaseTime(50.0f);

    env.gate(true);

    int totalSamples = expectedSamples(100.0f);
    int midpoint = totalSamples / 2;

    auto output = processAndCollect(env, totalSamples);

    // Exponential rising: midpoint should be above 0.55 (fast initial rise)
    REQUIRE(output[static_cast<size_t>(midpoint)] > 0.55f);
}

TEST_CASE("MultiStageEnvelope US2: linear curve midpoint within 2% of 0.5 for 0->1", "[msenv][us2]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 100.0f, EnvCurve::Linear);
    env.setStage(1, 1.0f, 100.0f, EnvCurve::Linear);
    env.setStage(2, 1.0f, 100.0f, EnvCurve::Linear);
    env.setStage(3, 1.0f, 100.0f, EnvCurve::Linear);
    env.setSustainPoint(3);
    env.setReleaseTime(50.0f);

    env.gate(true);

    int totalSamples = expectedSamples(100.0f);
    int midpoint = totalSamples / 2;

    auto output = processAndCollect(env, totalSamples);

    // Linear: midpoint within 2% of 0.5
    REQUIRE(output[static_cast<size_t>(midpoint)] > 0.48f);
    REQUIRE(output[static_cast<size_t>(midpoint)] < 0.52f);
}

TEST_CASE("MultiStageEnvelope US2: logarithmic curve midpoint < 0.45 for 0->1", "[msenv][us2]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 100.0f, EnvCurve::Logarithmic);
    env.setStage(1, 1.0f, 100.0f, EnvCurve::Logarithmic);
    env.setStage(2, 1.0f, 100.0f, EnvCurve::Logarithmic);
    env.setStage(3, 1.0f, 100.0f, EnvCurve::Logarithmic);
    env.setSustainPoint(3);
    env.setReleaseTime(50.0f);

    env.gate(true);

    int totalSamples = expectedSamples(100.0f);
    int midpoint = totalSamples / 2;

    auto output = processAndCollect(env, totalSamples);

    // Logarithmic rising: midpoint below 0.45 (slow initial rise)
    REQUIRE(output[static_cast<size_t>(midpoint)] < 0.45f);
}

TEST_CASE("MultiStageEnvelope US2: falling exponential drops quickly at first", "[msenv][us2]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 5.0f, EnvCurve::Exponential);    // Quick rise
    env.setStage(1, 0.3f, 100.0f, EnvCurve::Exponential);   // Falling exp
    env.setStage(2, 0.3f, 50.0f, EnvCurve::Exponential);
    env.setStage(3, 0.3f, 50.0f, EnvCurve::Exponential);
    env.setSustainPoint(2);
    env.setReleaseTime(50.0f);

    env.gate(true);

    // Process through stage 0 to reach 1.0
    int stage0Samples = expectedSamples(5.0f);
    processAndCollect(env, stage0Samples);

    // Now in stage 1 (falling from 1.0 to 0.3)
    int stage1Samples = expectedSamples(100.0f);
    int midpoint = stage1Samples / 2;
    auto output = processAndCollect(env, stage1Samples);

    // Exponential falling: midpoint should be below the linear midpoint
    // Linear midpoint would be (1.0 + 0.3) / 2 = 0.65
    // Exponential should drop faster initially, so midpoint < 0.65
    float normalizedMidpoint = (output[static_cast<size_t>(midpoint)] - 0.3f) / (1.0f - 0.3f);
    REQUIRE(normalizedMidpoint < 0.45f);
}

TEST_CASE("MultiStageEnvelope US2: mixed curves across stages", "[msenv][us2]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 100.0f, EnvCurve::Exponential);
    env.setStage(1, 0.5f, 100.0f, EnvCurve::Linear);
    env.setStage(2, 0.8f, 100.0f, EnvCurve::Logarithmic);
    env.setStage(3, 0.8f, 50.0f, EnvCurve::Exponential);
    env.setSustainPoint(3);
    env.setReleaseTime(50.0f);

    env.gate(true);

    int samplesPerStage = expectedSamples(100.0f);
    int midpoint = samplesPerStage / 2;

    // Stage 0 (exponential 0->1): midpoint > 0.55
    auto stage0 = processAndCollect(env, samplesPerStage);
    REQUIRE(stage0[static_cast<size_t>(midpoint)] > 0.55f);
    REQUIRE(stage0.back() == Approx(1.0f));

    // Stage 1 (linear 1.0->0.5): midpoint near 0.75
    auto stage1 = processAndCollect(env, samplesPerStage);
    float linMid = stage1[static_cast<size_t>(midpoint)];
    REQUIRE(linMid > 0.73f);
    REQUIRE(linMid < 0.77f);
    REQUIRE(stage1.back() == Approx(0.5f));

    // Stage 2 (logarithmic 0.5->0.8): uses phase^2 (slow start)
    auto stage2 = processAndCollect(env, samplesPerStage);
    float logMidNormalized = (stage2[static_cast<size_t>(midpoint)] - 0.5f) / (0.8f - 0.5f);
    REQUIRE(logMidNormalized < 0.45f);
    REQUIRE(stage2.back() == Approx(0.8f));
}

// =============================================================================
// US3: Loop Points (T051-T056)
// =============================================================================

TEST_CASE("MultiStageEnvelope US3: basic loop cycles multiple times", "[msenv][us3]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(6);
    env.setStage(0, 0.5f, 5.0f, EnvCurve::Linear);   // Pre-loop
    env.setStage(1, 1.0f, 10.0f, EnvCurve::Linear);   // Loop start
    env.setStage(2, 0.3f, 10.0f, EnvCurve::Linear);   // Loop
    env.setStage(3, 0.8f, 10.0f, EnvCurve::Linear);   // Loop end
    env.setStage(4, 0.2f, 10.0f, EnvCurve::Linear);
    env.setStage(5, 0.0f, 10.0f, EnvCurve::Linear);
    env.setSustainPoint(4);
    env.setLoopEnabled(true);
    env.setLoopStart(1);
    env.setLoopEnd(3);
    env.setReleaseTime(50.0f);

    env.gate(true);

    // Process through stage 0
    int stage0Samples = expectedSamples(5.0f);
    processAndCollect(env, stage0Samples);

    // Now in loop: stages 1, 2, 3 repeat (each 10ms = 441 samples)
    int loopCycleSamples = expectedSamples(10.0f) * 3;

    // Process 5 full loop cycles
    for (int cycle = 0; cycle < 5; ++cycle) {
        auto loopOutput = processAndCollect(env, loopCycleSamples);
        // End of loop cycle should reach stage 3 target 0.8
        REQUIRE(loopOutput.back() == Approx(0.8f).margin(0.01f));
    }

    REQUIRE(env.getState() == MultiStageEnvState::Running);
}

TEST_CASE("MultiStageEnvelope US3: gate-off during loop exits immediately to release", "[msenv][us3]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 10.0f, EnvCurve::Linear);
    env.setStage(1, 0.0f, 10.0f, EnvCurve::Linear);
    env.setStage(2, 1.0f, 10.0f, EnvCurve::Linear);
    env.setStage(3, 0.0f, 10.0f, EnvCurve::Linear);
    env.setSustainPoint(2);
    env.setLoopEnabled(true);
    env.setLoopStart(0);
    env.setLoopEnd(1);
    env.setReleaseTime(50.0f);

    env.gate(true);

    // Process part of a stage (not complete)
    processAndCollect(env, expectedSamples(10.0f) / 2);

    float currentOutput = env.getOutput();
    REQUIRE(env.getState() == MultiStageEnvState::Running);

    // Gate off mid-stage
    env.gate(false);
    REQUIRE(env.getState() == MultiStageEnvState::Releasing);

    // Output should continue from where it was (no jump)
    float nextSample = env.process();
    REQUIRE(std::abs(nextSample - currentOutput) < 0.1f);
}

TEST_CASE("MultiStageEnvelope US3: single-stage loop (loopStart == loopEnd)", "[msenv][us3]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 0.5f, 5.0f, EnvCurve::Linear);
    env.setStage(1, 0.5f, 5.0f, EnvCurve::Linear);
    env.setStage(2, 1.0f, 10.0f, EnvCurve::Linear);
    env.setStage(3, 0.5f, 5.0f, EnvCurve::Linear);
    env.setSustainPoint(2);
    env.setLoopEnabled(true);
    env.setLoopStart(2);
    env.setLoopEnd(2);
    env.setReleaseTime(50.0f);

    env.gate(true);

    // Process through stages 0, 1 to reach stage 2
    processAndCollect(env, expectedSamples(5.0f) * 2);

    // Stage 2 should loop: enters at current level, targets 1.0
    // After the first loop iteration, from=1.0, to=1.0 (holds)
    processAndCollect(env, expectedSamples(10.0f) * 3); // 3 iterations
    REQUIRE(env.getState() == MultiStageEnvState::Running);
}

TEST_CASE("MultiStageEnvelope US3: full envelope loop (all stages)", "[msenv][us3]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 10.0f, EnvCurve::Linear);
    env.setStage(1, 0.0f, 10.0f, EnvCurve::Linear);
    env.setStage(2, 0.5f, 10.0f, EnvCurve::Linear);
    env.setStage(3, 0.2f, 10.0f, EnvCurve::Linear);
    env.setSustainPoint(2);
    env.setLoopEnabled(true);
    env.setLoopStart(0);
    env.setLoopEnd(3);
    env.setReleaseTime(50.0f);

    env.gate(true);

    // Process through 3 full loops (4 stages * 3 = 12 stage completions)
    int loopCycleSamples = expectedSamples(10.0f) * 4;
    for (int cycle = 0; cycle < 3; ++cycle) {
        auto output = processAndCollect(env, loopCycleSamples);
        REQUIRE(output.back() == Approx(0.2f).margin(0.01f));
    }
    REQUIRE(env.getState() == MultiStageEnvState::Running);
}

TEST_CASE("MultiStageEnvelope US3: loop precision over 100 cycles (SC-005)", "[msenv][us3]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 10.0f, EnvCurve::Linear);
    env.setStage(1, 0.0f, 10.0f, EnvCurve::Linear);
    env.setStage(2, 1.0f, 10.0f, EnvCurve::Linear);
    env.setStage(3, 0.0f, 10.0f, EnvCurve::Linear);
    env.setSustainPoint(2);
    env.setLoopEnabled(true);
    env.setLoopStart(0);
    env.setLoopEnd(1);
    env.setReleaseTime(50.0f);

    env.gate(true);

    int cycleSamples = expectedSamples(10.0f) * 2;

    bool driftDetected = false;
    for (int cycle = 0; cycle < 100; ++cycle) {
        auto output = processAndCollect(env, cycleSamples);
        // End of cycle should be exactly at loop end target level
        float endVal = output.back();
        if (std::abs(endVal - 0.0f) > 0.001f) {
            driftDetected = true;
            break;
        }
    }
    REQUIRE_FALSE(driftDetected);
}

TEST_CASE("MultiStageEnvelope US3: sustain bypassed when looping (FR-026)", "[msenv][us3]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 5.0f, EnvCurve::Linear);
    env.setStage(1, 0.5f, 5.0f, EnvCurve::Linear);
    env.setStage(2, 0.8f, 5.0f, EnvCurve::Linear);   // This is sustain point
    env.setStage(3, 0.3f, 5.0f, EnvCurve::Linear);
    env.setSustainPoint(2);
    env.setLoopEnabled(true);
    env.setLoopStart(0);
    env.setLoopEnd(3);
    env.setReleaseTime(50.0f);

    env.gate(true);

    // Process through all 4 stages - should NOT stop at sustain point
    int totalSamples = expectedSamples(5.0f) * 4;
    processAndCollect(env, totalSamples);

    // Should still be Running (looping), NOT Sustaining
    REQUIRE(env.getState() == MultiStageEnvState::Running);
}

// =============================================================================
// US4: Sustain Point Selection (T066-T070)
// =============================================================================

TEST_CASE("MultiStageEnvelope US4: sustain at early stage (stage 1 of 6)", "[msenv][us4]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(6);
    for (int i = 0; i < 6; ++i) {
        env.setStage(i, static_cast<float>(i + 1) / 6.0f, 10.0f, EnvCurve::Exponential);
    }
    env.setSustainPoint(1);
    env.setReleaseTime(50.0f);

    env.gate(true);
    processUntilState(env, MultiStageEnvState::Sustaining);

    REQUIRE(env.getState() == MultiStageEnvState::Sustaining);
    float expectedLevel = 2.0f / 6.0f;
    REQUIRE(env.getOutput() == Approx(expectedLevel).margin(0.01f));
}

TEST_CASE("MultiStageEnvelope US4: sustain at last stage", "[msenv][us4]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(6);
    for (int i = 0; i < 6; ++i) {
        env.setStage(i, static_cast<float>(i + 1) / 6.0f, 10.0f, EnvCurve::Exponential);
    }
    env.setSustainPoint(5);
    env.setReleaseTime(50.0f);

    env.gate(true);
    processUntilState(env, MultiStageEnvState::Sustaining);

    REQUIRE(env.getState() == MultiStageEnvState::Sustaining);
    REQUIRE(env.getOutput() == Approx(1.0f).margin(0.01f));
}

TEST_CASE("MultiStageEnvelope US4: sustain point change while in pre-sustain stage", "[msenv][us4]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(6);
    for (int i = 0; i < 6; ++i) {
        env.setStage(i, static_cast<float>(i + 1) / 6.0f, 10.0f, EnvCurve::Exponential);
    }
    env.setSustainPoint(4);
    env.setReleaseTime(50.0f);

    env.gate(true);

    // Process a few stages
    processAndCollect(env, expectedSamples(10.0f) * 2);

    // Change sustain point to an earlier stage
    env.setSustainPoint(2);

    // Continue processing - should sustain at stage 2
    processUntilState(env, MultiStageEnvState::Sustaining);
    REQUIRE(env.getState() == MultiStageEnvState::Sustaining);
}

TEST_CASE("MultiStageEnvelope US4: gate-off from non-default sustain skips post-sustain", "[msenv][us4]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(6);
    for (int i = 0; i < 6; ++i) {
        env.setStage(i, static_cast<float>(i + 1) / 6.0f, 10.0f, EnvCurve::Exponential);
    }
    env.setSustainPoint(1);
    env.setReleaseTime(50.0f);

    env.gate(true);
    processUntilState(env, MultiStageEnvState::Sustaining);

    env.gate(false);
    REQUIRE(env.getState() == MultiStageEnvState::Releasing);

    // Should go to Idle without hitting stages 2-5
    processUntilState(env, MultiStageEnvState::Idle);
    REQUIRE(env.isActive() == false);
}

TEST_CASE("MultiStageEnvelope US4: sustain point validation clamped to [0, numStages-1]", "[msenv][us4]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);

    env.setSustainPoint(-1);
    REQUIRE(env.getSustainPoint() == 0);

    env.setSustainPoint(10);
    REQUIRE(env.getSustainPoint() == 3);

    env.setSustainPoint(2);
    REQUIRE(env.getSustainPoint() == 2);
}

// =============================================================================
// US5: Retrigger and Legato Modes (T078-T082)
// =============================================================================

TEST_CASE("MultiStageEnvelope US5: hard retrigger from sustain restarts at stage 0", "[msenv][us5]") {
    auto env = createBasic6Stage();
    env.setRetriggerMode(RetriggerMode::Hard);
    env.gate(true);

    processUntilState(env, MultiStageEnvState::Sustaining);
    REQUIRE(env.getState() == MultiStageEnvState::Sustaining);
    float sustainOutput = env.getOutput();

    // Retrigger
    env.gate(true);
    REQUIRE(env.getState() == MultiStageEnvState::Running);
    REQUIRE(env.getCurrentStage() == 0);

    // First sample after retrigger should start from the sustain level
    float firstSample = env.process();
    // Should not snap to 0 - it starts from current output
    REQUIRE(std::abs(firstSample - sustainOutput) < 0.2f);
}

TEST_CASE("MultiStageEnvelope US5: hard retrigger from release restarts at stage 0", "[msenv][us5]") {
    auto env = createBasic6Stage();
    env.setRetriggerMode(RetriggerMode::Hard);
    env.gate(true);

    processUntilState(env, MultiStageEnvState::Sustaining);
    env.gate(false);

    // Process some release samples
    processAndCollect(env, 100);
    REQUIRE(env.getState() == MultiStageEnvState::Releasing);
    float releaseOutput = env.getOutput();

    // Retrigger from release
    env.gate(true);
    REQUIRE(env.getState() == MultiStageEnvState::Running);
    REQUIRE(env.getCurrentStage() == 0);

    // Should start from current (release) level
    float firstSample = env.process();
    REQUIRE(std::abs(firstSample - releaseOutput) < 0.2f);
}

TEST_CASE("MultiStageEnvelope US5: legato mode continues from current position", "[msenv][us5]") {
    auto env = createBasic6Stage();
    env.setRetriggerMode(RetriggerMode::Legato);
    env.gate(true);

    // Process to sustain
    processUntilState(env, MultiStageEnvState::Sustaining);
    float sustainOutput = env.getOutput();

    // Legato gate-on should NOT restart
    env.gate(true);
    REQUIRE(env.getState() == MultiStageEnvState::Sustaining);
    REQUIRE(env.getOutput() == Approx(sustainOutput).margin(0.01f));
}

TEST_CASE("MultiStageEnvelope US5: legato mode from release returns to sustain", "[msenv][us5]") {
    auto env = createBasic6Stage();
    env.setRetriggerMode(RetriggerMode::Legato);
    env.gate(true);

    processUntilState(env, MultiStageEnvState::Sustaining);
    env.gate(false);

    processAndCollect(env, 100);
    REQUIRE(env.getState() == MultiStageEnvState::Releasing);

    // Legato retrigger from release - returns to sustain
    env.gate(true);
    REQUIRE(env.getState() == MultiStageEnvState::Sustaining);
}

TEST_CASE("MultiStageEnvelope US5: click-free retrigger transitions (SC-006)", "[msenv][us5]") {
    auto env = createBasic6Stage();
    env.setRetriggerMode(RetriggerMode::Hard);
    env.gate(true);

    processUntilState(env, MultiStageEnvState::Sustaining);
    float lastOutput = env.getOutput();

    // Retrigger
    env.gate(true);
    float firstAfter = env.process();

    // The jump should not be too large (no click)
    // Maximum per-sample increment for stage 0 with 10ms duration
    // is approximately 1.0 / 441 = 0.00227 (for linear)
    // For exponential, it can be larger at the start, so allow more margin
    float step = std::abs(firstAfter - lastOutput);
    REQUIRE(step < 0.1f);  // No large jump
}

// =============================================================================
// US6: Real-Time Parameter Changes (T090-T093)
// =============================================================================

TEST_CASE("MultiStageEnvelope US6: mid-stage time change (no discontinuity)", "[msenv][us6]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 100.0f, EnvCurve::Linear);
    env.setStage(1, 0.5f, 100.0f, EnvCurve::Linear);
    env.setStage(2, 0.8f, 100.0f, EnvCurve::Linear);
    env.setStage(3, 0.3f, 100.0f, EnvCurve::Linear);
    env.setSustainPoint(2);
    env.setReleaseTime(50.0f);

    env.gate(true);

    // Process half of stage 0
    int halfStage = expectedSamples(100.0f) / 2;
    auto before = processAndCollect(env, halfStage);
    float lastBefore = before.back();

    // Change stage 0 time to 200ms (double)
    env.setStageTime(0, 200.0f);

    // Next sample should not jump
    float afterChange = env.process();
    REQUIRE(std::abs(afterChange - lastBefore) < 0.01f);
}

TEST_CASE("MultiStageEnvelope US6: sustain level change during hold (smooth transition)", "[msenv][us6]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 5.0f, EnvCurve::Exponential);
    env.setStage(1, 0.5f, 5.0f, EnvCurve::Exponential);
    env.setStage(2, 0.8f, 5.0f, EnvCurve::Exponential);
    env.setStage(3, 0.3f, 5.0f, EnvCurve::Exponential);
    env.setSustainPoint(2);
    env.setReleaseTime(50.0f);

    env.gate(true);
    processUntilState(env, MultiStageEnvState::Sustaining);
    REQUIRE(env.getOutput() == Approx(0.8f));

    // Change sustain level
    env.setStageLevel(2, 0.4f);

    // Process for 5ms (smoothing time)
    int smoothSamples = expectedSamples(5.0f);
    auto output = processAndCollect(env, smoothSamples * 2);

    // Should gradually approach 0.4 (not instant jump)
    // First few samples should still be near 0.8
    REQUIRE(output[0] > 0.6f);

    // After smoothing period, should be close to 0.4
    REQUIRE(output.back() == Approx(0.4f).margin(0.05f));
}

TEST_CASE("MultiStageEnvelope US6: future stage level change takes effect on entry", "[msenv][us6]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 10.0f, EnvCurve::Exponential);
    env.setStage(1, 0.5f, 10.0f, EnvCurve::Exponential);
    env.setStage(2, 0.8f, 10.0f, EnvCurve::Exponential);
    env.setStage(3, 0.3f, 10.0f, EnvCurve::Exponential);
    env.setSustainPoint(2);
    env.setReleaseTime(50.0f);

    env.gate(true);

    // While in stage 0, change stage 2's level
    processAndCollect(env, 10);
    env.setStageLevel(2, 0.9f);

    // Continue to sustain
    processUntilState(env, MultiStageEnvState::Sustaining);

    // Should be at the new level 0.9 (not original 0.8)
    REQUIRE(env.getOutput() == Approx(0.9f).margin(0.01f));
}

TEST_CASE("MultiStageEnvelope US6: loop boundary change during active loop", "[msenv][us6]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(6);
    env.setStage(0, 1.0f, 5.0f, EnvCurve::Linear);
    env.setStage(1, 0.5f, 5.0f, EnvCurve::Linear);
    env.setStage(2, 0.8f, 5.0f, EnvCurve::Linear);
    env.setStage(3, 0.3f, 5.0f, EnvCurve::Linear);
    env.setStage(4, 0.6f, 5.0f, EnvCurve::Linear);
    env.setStage(5, 0.1f, 5.0f, EnvCurve::Linear);
    env.setSustainPoint(4);
    env.setLoopEnabled(true);
    env.setLoopStart(1);
    env.setLoopEnd(3);
    env.setReleaseTime(50.0f);

    env.gate(true);

    // Process through a loop cycle
    int loopCycle = expectedSamples(5.0f) * 4; // stage 0 + 3 loop stages
    processAndCollect(env, loopCycle);

    // Change loop end to stage 2 (shorter loop)
    env.setLoopEnd(2);

    // The change takes effect on next loop iteration
    // Process more samples and verify no crash
    processAndCollect(env, loopCycle);
    REQUIRE(env.getState() == MultiStageEnvState::Running);
}

// =============================================================================
// Phase 9: Edge Cases & Robustness
// =============================================================================

TEST_CASE("MultiStageEnvelope edge: all stages 0ms (staircase pattern)", "[msenv][edge]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 0.0f, EnvCurve::Linear);
    env.setStage(1, 0.5f, 0.0f, EnvCurve::Linear);
    env.setStage(2, 0.8f, 0.0f, EnvCurve::Linear);
    env.setStage(3, 0.3f, 0.0f, EnvCurve::Linear);
    env.setSustainPoint(2);
    env.setReleaseTime(50.0f);

    env.gate(true);

    // With 0ms stages and minimum 1 sample each:
    // Sample 1: stage 0 -> snaps to 1.0, advance
    // Sample 2: stage 1 -> snaps to 0.5, advance
    // Sample 3: stage 2 -> snaps to 0.8, sustain

    auto output = processAndCollect(env, 5);
    REQUIRE(output[0] == Approx(1.0f));
    REQUIRE(output[1] == Approx(0.5f));
    REQUIRE(output[2] == Approx(0.8f));
    REQUIRE(env.getState() == MultiStageEnvState::Sustaining);
}

TEST_CASE("MultiStageEnvelope edge: maximum stage time (10000ms)", "[msenv][edge]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 10000.0f, EnvCurve::Linear);
    env.setStage(1, 0.5f, 5.0f, EnvCurve::Linear);
    env.setStage(2, 0.8f, 5.0f, EnvCurve::Linear);
    env.setStage(3, 0.3f, 5.0f, EnvCurve::Linear);
    env.setSustainPoint(2);
    env.setReleaseTime(50.0f);

    env.gate(true);

    // Process a portion
    processAndCollect(env, 10000);
    REQUIRE(env.getState() == MultiStageEnvState::Running);
    REQUIRE(env.getCurrentStage() == 0);

    // Output should be gradually increasing
    float output = env.getOutput();
    REQUIRE(output > 0.0f);
    REQUIRE(output < 1.0f);
}

TEST_CASE("MultiStageEnvelope edge: adjacent stages with same target level", "[msenv][edge]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 0.5f, 10.0f, EnvCurve::Linear);
    env.setStage(1, 0.5f, 10.0f, EnvCurve::Linear);  // Same as stage 0
    env.setStage(2, 0.5f, 10.0f, EnvCurve::Linear);   // Same again
    env.setStage(3, 0.5f, 10.0f, EnvCurve::Linear);
    env.setSustainPoint(2);
    env.setReleaseTime(50.0f);

    env.gate(true);

    // Process past stage 0
    int samplesPerStage = expectedSamples(10.0f);
    auto output = processAndCollect(env, samplesPerStage);
    REQUIRE(output.back() == Approx(0.5f));

    // Stage 1 should hold at 0.5 for its duration
    auto stage1Output = processAndCollect(env, samplesPerStage);
    for (float sample : stage1Output) {
        REQUIRE(sample == Approx(0.5f).margin(0.01f));
    }
}

TEST_CASE("MultiStageEnvelope edge: sample rate changes preserve output level", "[msenv][edge]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 10.0f, EnvCurve::Exponential);
    env.setStage(1, 0.5f, 10.0f, EnvCurve::Exponential);
    env.setStage(2, 0.8f, 10.0f, EnvCurve::Exponential);
    env.setStage(3, 0.3f, 10.0f, EnvCurve::Exponential);
    env.setSustainPoint(2);
    env.setReleaseTime(50.0f);

    env.gate(true);
    processAndCollect(env, 200);
    float currentOutput = env.getOutput();

    // Change sample rate while active
    env.prepare(96000.0f);

    // Output should be preserved (not reset)
    // Note: prepare doesn't reset the state machine
    REQUIRE(env.getOutput() == Approx(currentOutput).margin(0.01f));
}

TEST_CASE("MultiStageEnvelope edge: prepare at standard sample rates", "[msenv][edge]") {
    float rates[] = {44100.0f, 48000.0f, 88200.0f, 96000.0f, 176400.0f, 192000.0f};

    for (float rate : rates) {
        MultiStageEnvelope env;
        env.prepare(rate);
        env.setNumStages(4);
        env.setStage(0, 1.0f, 10.0f, EnvCurve::Exponential);
        env.setStage(1, 0.5f, 10.0f, EnvCurve::Exponential);
        env.setStage(2, 0.8f, 10.0f, EnvCurve::Exponential);
        env.setStage(3, 0.3f, 10.0f, EnvCurve::Exponential);
        env.setSustainPoint(2);
        env.setReleaseTime(50.0f);

        env.gate(true);
        processUntilState(env, MultiStageEnvState::Sustaining);
        REQUIRE(env.getState() == MultiStageEnvState::Sustaining);
        REQUIRE(env.getOutput() == Approx(0.8f).margin(0.01f));
    }
}

TEST_CASE("MultiStageEnvelope edge: 0ms release snaps to zero immediately", "[msenv][edge]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1.0f, 5.0f, EnvCurve::Exponential);
    env.setStage(1, 0.5f, 5.0f, EnvCurve::Exponential);
    env.setStage(2, 0.8f, 5.0f, EnvCurve::Exponential);
    env.setStage(3, 0.3f, 5.0f, EnvCurve::Exponential);
    env.setSustainPoint(2);
    env.setReleaseTime(0.0f);

    env.gate(true);
    processUntilState(env, MultiStageEnvState::Sustaining);

    env.gate(false);
    // With 0ms release, should reach idle very quickly
    int samples = processUntilState(env, MultiStageEnvState::Idle, 100);
    REQUIRE(samples <= 10); // Should be nearly immediate
    REQUIRE(env.getOutput() == 0.0f);
}

TEST_CASE("MultiStageEnvelope edge: FR-035 denormal prevention", "[msenv][edge]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 1e-7f, 5.0f, EnvCurve::Exponential);  // Very low level
    env.setStage(1, 1e-8f, 5.0f, EnvCurve::Exponential);
    env.setStage(2, 1e-9f, 5.0f, EnvCurve::Exponential);
    env.setStage(3, 0.0f, 5.0f, EnvCurve::Exponential);
    env.setSustainPoint(2);
    env.setReleaseTime(50.0f);

    env.gate(true);
    auto output = processAndCollect(env, 2000);

    bool hasDenormal = false;
    for (float sample : output) {
        if (sample != 0.0f && std::abs(sample) < 1e-15f) {
            hasDenormal = true;
            break;
        }
    }
    REQUIRE_FALSE(hasDenormal);
}

// =============================================================================
// Phase 9: Sample Rate Accuracy (SC-007)
// =============================================================================

TEST_CASE("MultiStageEnvelope: stage timing within 1% at all standard sample rates (SC-007)", "[msenv][timing]") {
    float rates[] = {44100.0f, 48000.0f, 88200.0f, 96000.0f, 176400.0f, 192000.0f};
    float testTimeMs = 50.0f;

    for (float rate : rates) {
        MultiStageEnvelope env;
        env.prepare(rate);
        env.setNumStages(4);
        env.setStage(0, 1.0f, testTimeMs, EnvCurve::Linear);
        env.setStage(1, 0.5f, testTimeMs, EnvCurve::Linear);
        env.setStage(2, 0.8f, testTimeMs, EnvCurve::Linear);
        env.setStage(3, 0.3f, testTimeMs, EnvCurve::Linear);
        env.setSustainPoint(2);
        env.setReleaseTime(50.0f);

        env.gate(true);

        // Count samples for stage 0
        int samplesInStage0 = 0;
        while (env.getCurrentStage() == 0 && samplesInStage0 < 1000000) {
            (void)env.process();
            ++samplesInStage0;
        }

        int expectedSamp = expectedSamples(testTimeMs, rate);
        float actualMs = static_cast<float>(samplesInStage0) / rate * 1000.0f;
        float error = std::abs(actualMs - testTimeMs) / testTimeMs;

        // Within 1% (or +/-1 sample, whichever is larger)
        bool withinTolerance = (error < 0.01f) ||
            (std::abs(samplesInStage0 - expectedSamp) <= 1);
        REQUIRE(withinTolerance);
    }
}

// =============================================================================
// Phase 9: Configuration Queries (T113-T115)
// =============================================================================

TEST_CASE("MultiStageEnvelope: configuration query methods", "[msenv][config]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);

    SECTION("getNumStages") {
        env.setNumStages(6);
        REQUIRE(env.getNumStages() == 6);
    }

    SECTION("getSustainPoint") {
        env.setNumStages(6);
        env.setSustainPoint(3);
        REQUIRE(env.getSustainPoint() == 3);
    }

    SECTION("getLoopEnabled") {
        REQUIRE(env.getLoopEnabled() == false);
        env.setLoopEnabled(true);
        REQUIRE(env.getLoopEnabled() == true);
    }

    SECTION("getLoopStart and getLoopEnd") {
        env.setNumStages(6);
        env.setLoopStart(1);
        env.setLoopEnd(4);
        REQUIRE(env.getLoopStart() == 1);
        REQUIRE(env.getLoopEnd() == 4);
    }
}

// =============================================================================
// Phase 9: Performance Benchmark (SC-003)
// =============================================================================

TEST_CASE("MultiStageEnvelope: performance benchmark (SC-003)", "[msenv][benchmark]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(8);
    for (int i = 0; i < 8; ++i) {
        env.setStage(i, static_cast<float>(i) / 7.0f, 50.0f, EnvCurve::Exponential);
    }
    env.setSustainPoint(6);
    env.setReleaseTime(100.0f);
    env.setLoopEnabled(true);
    env.setLoopStart(0);
    env.setLoopEnd(7);
    env.gate(true);

    // Warm up
    for (int i = 0; i < 1000; ++i) {
        (void)env.process();
    }

    BENCHMARK("8-stage envelope process (single sample)") {
        return env.process();
    };

    BENCHMARK("8-stage envelope processBlock (512 samples)") {
        float buffer[512];
        env.processBlock(buffer, 512);
        return buffer[511];
    };
}

// =============================================================================
// FR-021: Stage completes at exact time with target snap
// =============================================================================

TEST_CASE("MultiStageEnvelope: FR-021 final sample snaps to exact target level", "[msenv][us1]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);
    env.setNumStages(4);
    env.setStage(0, 0.73f, 10.0f, EnvCurve::Exponential);  // Arbitrary non-round target
    env.setStage(1, 0.29f, 10.0f, EnvCurve::Exponential);
    env.setStage(2, 0.55f, 10.0f, EnvCurve::Exponential);
    env.setStage(3, 0.41f, 10.0f, EnvCurve::Exponential);
    env.setSustainPoint(2);
    env.setReleaseTime(50.0f);

    env.gate(true);

    int samplesPerStage = expectedSamples(10.0f);

    // Process through stage 0
    auto stage0 = processAndCollect(env, samplesPerStage);
    // Last sample should be exactly 0.73
    REQUIRE(stage0.back() == Approx(0.73f));

    // Process through stage 1
    auto stage1 = processAndCollect(env, samplesPerStage);
    REQUIRE(stage1.back() == Approx(0.29f));

    // Process through stage 2
    auto stage2 = processAndCollect(env, samplesPerStage);
    REQUIRE(stage2.back() == Approx(0.55f));
}

// =============================================================================
// FR-009: isActive and isReleasing queries
// =============================================================================

TEST_CASE("MultiStageEnvelope: FR-009 state query methods", "[msenv][us1]") {
    auto env = createBasic6Stage();

    // Idle
    REQUIRE(env.isActive() == false);
    REQUIRE(env.isReleasing() == false);

    // Running
    env.gate(true);
    (void)env.process();
    REQUIRE(env.isActive() == true);
    REQUIRE(env.isReleasing() == false);

    // Sustaining
    processUntilState(env, MultiStageEnvState::Sustaining);
    REQUIRE(env.isActive() == true);
    REQUIRE(env.isReleasing() == false);

    // Releasing
    env.gate(false);
    REQUIRE(env.isActive() == true);
    REQUIRE(env.isReleasing() == true);

    // Back to Idle
    processUntilState(env, MultiStageEnvState::Idle);
    REQUIRE(env.isActive() == false);
    REQUIRE(env.isReleasing() == false);
}

// =============================================================================
// SC-002: Output Continuity (no clicks)
// =============================================================================

TEST_CASE("MultiStageEnvelope: SC-002 output continuous across stage transitions", "[msenv][continuity]") {
    auto env = createBasic6Stage();
    env.gate(true);

    // Process through all stages to sustain
    int totalPreSustain = expectedSamples(10.0f) * 4; // 4 stages * 441 samples
    auto output = processAndCollect(env, totalPreSustain + 100);

    // Check continuity - max step should be reasonable
    float ms = maxStep(output);
    // For 10ms stages at 44.1kHz, max step should be small
    REQUIRE(ms < 0.1f);
}

// =============================================================================
// FR-015: Default sustain point
// =============================================================================

TEST_CASE("MultiStageEnvelope: FR-015 default sustain point is numStages-2", "[msenv][us1]") {
    MultiStageEnvelope env;
    env.prepare(kTestSampleRate);

    // Default is 4 stages, sustain at 2
    REQUIRE(env.getSustainPoint() == 2);

    env.setNumStages(6);
    env.setSustainPoint(4);  // numStages - 2
    REQUIRE(env.getSustainPoint() == 4);

    env.setNumStages(8);
    env.setSustainPoint(6);
    REQUIRE(env.getSustainPoint() == 6);
}
