// ==============================================================================
// MorphEngine Transition Tests
// ==============================================================================
// Unit tests for morph smoothing and transition behavior.
//
// Constitution Principle XII: Test-First Development
// Reference: specs/005-morph-system/spec.md FR-009, SC-006, SC-007
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/morph_engine.h"
#include "dsp/morph_node.h"
#include "dsp/distortion_types.h"

#include <cmath>
#include <vector>

using Catch::Approx;

// =============================================================================
// Test Fixtures
// =============================================================================

namespace {

/// @brief Configure a MorphEngine for testing.
void prepareTestEngine(Disrumpo::MorphEngine& engine, double sampleRate = 44100.0) {
    engine.prepare(sampleRate, 512);
}

/// @brief Create standard 2-node setup (A at 0, B at 1).
std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> createTwoNodes() {
    std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes;
    nodes[0] = Disrumpo::MorphNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip);
    nodes[1] = Disrumpo::MorphNode(1, 1.0f, 0.0f, Disrumpo::DistortionType::Tube);
    nodes[2] = Disrumpo::MorphNode(2, 0.0f, 1.0f, Disrumpo::DistortionType::Fuzz);
    nodes[3] = Disrumpo::MorphNode(3, 1.0f, 1.0f, Disrumpo::DistortionType::SineFold);
    return nodes;
}

/// @brief Process N samples through engine to advance smoothers.
void advanceSamples(Disrumpo::MorphEngine& engine, int numSamples) {
    for (int i = 0; i < numSamples; ++i) {
        [[maybe_unused]] float out = engine.process(0.0f);  // Input doesn't matter for position smoothing test
    }
}

/// @brief Calculate number of samples for a given time in ms at sample rate.
int msToSamples(float ms, double sampleRate) {
    return static_cast<int>(ms * 0.001f * sampleRate);
}

/// @brief Check if output has any clicks (sudden large changes).
bool hasClicks(const std::vector<float>& output, float threshold = 0.1f) {
    if (output.size() < 2) return false;

    for (size_t i = 1; i < output.size(); ++i) {
        float diff = std::abs(output[i] - output[i - 1]);
        if (diff > threshold) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

// =============================================================================
// FR-009: Morph Smoothing Tests
// =============================================================================

TEST_CASE("MorphEngine smoothing - 0ms smoothing gives fast transition", "[morph][transition][FR-009]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);
    engine.setMode(Disrumpo::MorphMode::Linear1D);
    engine.setSmoothingTime(0.0f);  // 0ms gets clamped to minimum (0.1ms)

    // Start at position 0 and let it settle
    engine.setMorphPosition(0.0f, 0.0f);
    advanceSamples(engine, 50);  // Let it fully settle

    // Should be at 0
    REQUIRE(engine.getSmoothedX() == Approx(0.0f).margin(0.01f));

    // Jump to position 1
    engine.setMorphPosition(1.0f, 0.0f);

    // After minimum smoothing time samples, should reach target
    // 0.1ms at 44.1kHz is about 5 samples, so 50 samples should be enough
    advanceSamples(engine, 50);
    REQUIRE(engine.getSmoothedX() == Approx(1.0f).margin(0.01f));
}

TEST_CASE("MorphEngine smoothing - 100ms smoothing completes in approximately 100ms", "[morph][transition][FR-009][SC-006]") {
    constexpr double sampleRate = 44100.0;
    constexpr float smoothingMs = 100.0f;

    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine, sampleRate);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);
    engine.setMode(Disrumpo::MorphMode::Linear1D);
    engine.setSmoothingTime(smoothingMs);

    // Start at position 0
    engine.setMorphPosition(0.0f, 0.0f);
    advanceSamples(engine, 100);  // Let it settle

    // Jump to position 1
    engine.setMorphPosition(1.0f, 0.0f);

    // Process for 95ms (should not be complete yet)
    int samplesAt95ms = msToSamples(95.0f, sampleRate);
    advanceSamples(engine, samplesAt95ms);
    float posAt95ms = engine.getSmoothedX();

    // OnePoleSmoother reaches ~99% at the configured time
    // At 95ms of 100ms, should be close but not quite at target
    // Allow some margin since OnePoleSmoother is exponential
    REQUIRE(posAt95ms > 0.9f);  // Should be at least 90% of the way
    REQUIRE(posAt95ms < 1.0f);  // But not quite there

    // Process for additional 10ms (total 105ms - should be complete)
    int additionalSamples = msToSamples(10.0f, sampleRate);
    advanceSamples(engine, additionalSamples);
    float posAt105ms = engine.getSmoothedX();

    // Should be at or very close to target
    REQUIRE(posAt105ms == Approx(1.0f).margin(0.01f));
}

TEST_CASE("MorphEngine smoothing - rapid automation produces limited sample-to-sample changes", "[morph][transition][FR-009][SC-007]") {
    constexpr double sampleRate = 44100.0;
    constexpr float smoothingMs = 10.0f;  // Fast but with some smoothing

    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine, sampleRate);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);
    engine.setMode(Disrumpo::MorphMode::Linear1D);
    engine.setSmoothingTime(smoothingMs);

    // Simulate 20Hz square wave modulation (toggling every 25ms = 1103 samples at 44.1kHz)
    constexpr int samplesPerHalfCycle = 1103;  // ~25ms at 44.1kHz
    constexpr int numCycles = 10;

    std::vector<float> output;
    output.reserve(samplesPerHalfCycle * 2 * numCycles);

    for (int cycle = 0; cycle < numCycles; ++cycle) {
        // First half: position 0
        engine.setMorphPosition(0.0f, 0.0f);
        for (int s = 0; s < samplesPerHalfCycle; ++s) {
            float out = engine.process(0.5f);  // Use constant input
            output.push_back(out);
        }

        // Second half: position 1
        engine.setMorphPosition(1.0f, 0.0f);
        for (int s = 0; s < samplesPerHalfCycle; ++s) {
            float out = engine.process(0.5f);
            output.push_back(out);
        }
    }

    // Check that sample-to-sample changes are limited (no extreme clicks)
    // With processing through distortion, the output will vary, but shouldn't have
    // sudden large jumps indicative of clicks. Use a generous threshold.
    REQUIRE_FALSE(hasClicks(output, 0.5f));  // Allow larger changes due to distortion processing
}

// =============================================================================
// SC-006: Smoothing Timing Accuracy Tests
// =============================================================================

TEST_CASE("MorphEngine smoothing - timing accuracy reaches target", "[morph][transition][SC-006]") {
    constexpr double sampleRate = 44100.0;

    // The OnePoleSmoother reaches ~99% at the configured time (5 time constants).
    // We test that at the configured time, the value is very close to target.

    SECTION("50ms smoothing") {
        constexpr float smoothingMs = 50.0f;

        Disrumpo::MorphEngine engine;
        prepareTestEngine(engine, sampleRate);
        auto nodes = createTwoNodes();
        engine.setNodes(nodes, 2);
        engine.setSmoothingTime(smoothingMs);

        engine.setMorphPosition(0.0f, 0.0f);
        advanceSamples(engine, 500);  // Let it settle

        engine.setMorphPosition(1.0f, 0.0f);

        // At 50% of target time (25ms), should be partway
        int samplesAt50Percent = msToSamples(smoothingMs * 0.5f, sampleRate);
        advanceSamples(engine, samplesAt50Percent);
        float posAt50Percent = engine.getSmoothedX();
        REQUIRE(posAt50Percent > 0.3f);  // Should have moved significantly
        REQUIRE(posAt50Percent < 0.95f);  // But not complete

        // At 100% of target time, should be at or very near target
        int additionalSamples = msToSamples(smoothingMs * 0.5f, sampleRate);
        advanceSamples(engine, additionalSamples);
        float posAt100Percent = engine.getSmoothedX();
        REQUIRE(posAt100Percent > 0.95f);
    }

    SECTION("200ms smoothing") {
        constexpr float smoothingMs = 200.0f;

        Disrumpo::MorphEngine engine;
        prepareTestEngine(engine, sampleRate);
        auto nodes = createTwoNodes();
        engine.setNodes(nodes, 2);
        engine.setSmoothingTime(smoothingMs);

        engine.setMorphPosition(0.0f, 0.0f);
        advanceSamples(engine, 1000);

        engine.setMorphPosition(1.0f, 0.0f);

        // At 50% of target time, should be partway
        int samplesAt50Percent = msToSamples(smoothingMs * 0.5f, sampleRate);
        advanceSamples(engine, samplesAt50Percent);
        float posAt50Percent = engine.getSmoothedX();
        REQUIRE(posAt50Percent > 0.3f);
        REQUIRE(posAt50Percent < 0.95f);

        // At 100% of target time, should be at or very near target
        int additionalSamples = msToSamples(smoothingMs * 0.5f, sampleRate);
        advanceSamples(engine, additionalSamples);
        float posAt100Percent = engine.getSmoothedX();
        REQUIRE(posAt100Percent > 0.95f);
    }
}

// =============================================================================
// SC-007: Rapid Automation Tests
// =============================================================================

TEST_CASE("MorphEngine handles 20Hz LFO modulation without artifacts", "[morph][transition][SC-007]") {
    constexpr double sampleRate = 44100.0;
    constexpr float smoothingMs = 5.0f;  // Fast smoothing for rapid modulation

    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine, sampleRate);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);
    engine.setMode(Disrumpo::MorphMode::Linear1D);
    engine.setSmoothingTime(smoothingMs);

    // 20Hz = 50ms period = 2205 samples at 44.1kHz
    constexpr int samplesPerPeriod = 2205;
    constexpr int numPeriods = 5;

    std::vector<float> output;
    output.reserve(samplesPerPeriod * numPeriods);

    // Simulate sine wave LFO at 20Hz
    for (int i = 0; i < samplesPerPeriod * numPeriods; ++i) {
        // LFO position oscillates between 0 and 1
        float lfoPhase = static_cast<float>(i) / static_cast<float>(samplesPerPeriod);
        float lfoValue = (std::sin(lfoPhase * 2.0f * 3.14159265f) + 1.0f) * 0.5f;

        engine.setMorphPosition(lfoValue, 0.0f);
        float out = engine.process(0.5f);
        output.push_back(out);
    }

    // Check for clicks
    REQUIRE_FALSE(hasClicks(output, 0.15f));
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("MorphEngine smoothing - handles sample rate changes", "[morph][transition][edge]") {
    // Start at 44.1kHz
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine, 44100.0);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);
    engine.setSmoothingTime(100.0f);

    engine.setMorphPosition(0.0f, 0.0f);
    advanceSamples(engine, 100);

    // Change to 96kHz (simulating sample rate change)
    engine.prepare(96000.0, 512);
    engine.setNodes(nodes, 2);
    engine.setSmoothingTime(100.0f);

    engine.setMorphPosition(1.0f, 0.0f);

    // Process for 100ms at 96kHz
    int samples100ms = msToSamples(100.0f, 96000.0);
    advanceSamples(engine, samples100ms);

    // Should be at or near target
    REQUIRE(engine.getSmoothedX() > 0.95f);
}

TEST_CASE("MorphEngine smoothing - handles extreme smoothing times", "[morph][transition][edge]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);

    SECTION("minimum smoothing (0ms)") {
        engine.setSmoothingTime(0.0f);
        engine.setMorphPosition(0.0f, 0.0f);
        advanceSamples(engine, 10);
        engine.setMorphPosition(1.0f, 0.0f);
        advanceSamples(engine, 10);
        REQUIRE(engine.getSmoothedX() == Approx(1.0f).margin(0.01f));
    }

    SECTION("maximum smoothing (500ms)") {
        engine.setSmoothingTime(500.0f);
        engine.setMorphPosition(0.0f, 0.0f);
        advanceSamples(engine, 2000);  // Let it fully settle
        engine.setMorphPosition(1.0f, 0.0f);

        // After 100ms (20% of 500ms), should be partway through
        advanceSamples(engine, msToSamples(100.0f, 44100.0));
        float posAt100ms = engine.getSmoothedX();
        REQUIRE(posAt100ms < 0.9f);  // Should not be complete yet
        REQUIRE(posAt100ms > 0.0f);  // But should have started moving
    }
}

TEST_CASE("MorphEngine smoothing - reset clears smoother state", "[morph][transition][edge]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);
    engine.setSmoothingTime(100.0f);

    // Move to position 1
    engine.setMorphPosition(1.0f, 0.0f);
    advanceSamples(engine, msToSamples(50.0f, 44100.0));

    // Should be partway through transition
    float midTransition = engine.getSmoothedX();
    REQUIRE(midTransition > 0.0f);
    REQUIRE(midTransition < 1.0f);

    // Reset should clear state
    engine.reset();

    // Position should snap to target
    REQUIRE(engine.getSmoothedX() == Approx(1.0f).margin(0.01f));
}
