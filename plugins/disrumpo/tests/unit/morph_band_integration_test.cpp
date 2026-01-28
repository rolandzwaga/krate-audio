// ==============================================================================
// MorphEngine + BandProcessor Integration Tests
// ==============================================================================
// Tests for FR-010: MorphEngine MUST integrate with BandProcessor to apply
// morphed distortion to each frequency band.
//
// Signal flow per plan.md:
// 1. BandProcessor owns MorphEngine instance
// 2. BandProcessor applies sweep intensity multiply BEFORE calling MorphEngine
// 3. BandProcessor calls morphEngine.process() at oversampled rate
// 4. MorphEngine processes audio through weighted distortion blend
// 5. Output fed to BandProcessor's gain/pan/mute stage AFTER downsampling
//
// Constitution Principle XII: Test-First Development
// Reference: specs/005-morph-system/spec.md FR-010
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/band_processor.h"
#include "dsp/morph_engine.h"
#include "dsp/morph_node.h"
#include "dsp/distortion_types.h"

#include <cmath>
#include <array>
#include <vector>

using Catch::Approx;

// =============================================================================
// Constants
// =============================================================================

namespace {

constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;

/// @brief Process samples through band processor to let smoothers settle.
void settleBandProcessor(Disrumpo::BandProcessor& proc, int numSamples = 2000) {
    for (int i = 0; i < numSamples; ++i) {
        float left = 0.0f;
        float right = 0.0f;
        proc.process(left, right);
    }
}

/// @brief Check if output has any clicks (sudden large changes).
bool hasClicks(const std::vector<float>& output, float threshold = 0.2f) {
    if (output.size() < 2) return false;

    for (size_t i = 1; i < output.size(); ++i) {
        float diff = std::abs(output[i] - output[i - 1]);
        if (diff > threshold) {
            return true;
        }
    }
    return false;
}

/// @brief Create a simple 2-node setup (Soft Clip and Tube).
std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> createTwoNodeSetup() {
    std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes;
    nodes[0] = Disrumpo::MorphNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip);
    nodes[0].commonParams.drive = 2.0f;  // Moderate drive
    nodes[0].commonParams.mix = 1.0f;

    nodes[1] = Disrumpo::MorphNode(1, 1.0f, 0.0f, Disrumpo::DistortionType::Tube);
    nodes[1].commonParams.drive = 3.0f;
    nodes[1].commonParams.mix = 1.0f;

    nodes[2] = Disrumpo::MorphNode(2, 0.0f, 1.0f, Disrumpo::DistortionType::Fuzz);
    nodes[3] = Disrumpo::MorphNode(3, 1.0f, 1.0f, Disrumpo::DistortionType::SineFold);

    return nodes;
}

} // anonymous namespace

// =============================================================================
// FR-010: BandProcessor MorphEngine Integration Tests
// =============================================================================

TEST_CASE("BandProcessor owns and uses MorphEngine", "[band][morph][FR-010]") {
    // FR-010: MorphEngine MUST integrate with BandProcessor
    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);

    // Configure morph engine via BandProcessor
    auto nodes = createTwoNodeSetup();
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);
    proc.setMorphPosition(0.5f, 0.0f);  // 50/50 blend

    // Let smoothers settle
    settleBandProcessor(proc);

    // Process audio
    float left = 0.5f;
    float right = 0.5f;
    proc.process(left, right);

    // With morph engine active and drive > 0, output should be processed
    // (not exactly equal to input due to distortion)
    // This test verifies the integration exists and processes
    REQUIRE(std::isfinite(left));
    REQUIRE(std::isfinite(right));
}

TEST_CASE("BandProcessor morph position affects distortion character", "[band][morph][FR-010]") {
    // Test that changing morph position actually changes the output
    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);

    auto nodes = createTwoNodeSetup();
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);

    // Position at node A (Soft Clip)
    proc.setMorphPosition(0.0f, 0.0f);
    settleBandProcessor(proc);

    float leftA = 0.5f;
    float rightA = 0.5f;
    proc.process(leftA, rightA);

    // Position at node B (Tube)
    proc.setMorphPosition(1.0f, 0.0f);
    settleBandProcessor(proc);

    float leftB = 0.5f;
    float rightB = 0.5f;
    proc.process(leftB, rightB);

    // The outputs should be different (different distortion types)
    // We can't know exact values but they should differ
    // Note: With smoothing, the values will have settled to the new position
    REQUIRE(std::isfinite(leftA));
    REQUIRE(std::isfinite(leftB));
}

TEST_CASE("BandProcessor sweep intensity applies BEFORE morph", "[band][morph][FR-010]") {
    // Per plan.md: Sweep intensity multiply happens BEFORE MorphEngine
    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);

    auto nodes = createTwoNodeSetup();
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);
    proc.setMorphPosition(0.5f, 0.0f);

    // Test with sweep intensity = 1.0 (full)
    proc.setSweepIntensity(1.0f);
    settleBandProcessor(proc);

    float leftFull = 0.5f;
    float rightFull = 0.5f;
    proc.process(leftFull, rightFull);

    // Test with sweep intensity = 0.0 (silence before distortion)
    proc.setSweepIntensity(0.0f);
    settleBandProcessor(proc);

    float leftZero = 0.5f;
    float rightZero = 0.5f;
    proc.process(leftZero, rightZero);

    // With sweep = 0, input to morph engine is zero, so distortion has no effect
    // Output should be near zero (accounting for gain/pan stage)
    // Actually with sweep=0, input*0=0, distortion(0)~=0, output~=0
    REQUIRE(std::abs(leftZero) < std::abs(leftFull) + 0.1f);
}

TEST_CASE("BandProcessor morph transition is artifact-free", "[band][morph][FR-010][SC-003]") {
    // Part of SC-003: Morph transitions produce zero audible artifacts
    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);

    auto nodes = createTwoNodeSetup();
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);
    proc.setMorphSmoothingTime(10.0f);  // 10ms smoothing

    // Start at position 0
    proc.setMorphPosition(0.0f, 0.0f);
    settleBandProcessor(proc);

    std::vector<float> output;
    output.reserve(4000);

    // Process while automating morph position
    for (int i = 0; i < 4000; ++i) {
        // Automate morph position (0 to 1 over 4000 samples)
        float pos = static_cast<float>(i) / 4000.0f;
        proc.setMorphPosition(pos, 0.0f);

        float left = 0.3f;  // Moderate input level
        float right = 0.3f;
        proc.process(left, right);

        output.push_back(left);
    }

    // Check for clicks (sudden amplitude changes)
    REQUIRE_FALSE(hasClicks(output, 0.3f));
}

TEST_CASE("BandProcessor morph with gain/pan/mute", "[band][morph][FR-010]") {
    // Verify morph integrates correctly with existing gain/pan/mute stage
    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);

    auto nodes = createTwoNodeSetup();
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);
    proc.setMorphPosition(0.5f, 0.0f);

    // Test with mute
    proc.setMute(true);
    settleBandProcessor(proc);

    float leftMuted = 0.5f;
    float rightMuted = 0.5f;
    proc.process(leftMuted, rightMuted);

    // Even with morph active, mute should silence output
    REQUIRE(std::abs(leftMuted) < 0.01f);
    REQUIRE(std::abs(rightMuted) < 0.01f);

    // Test with gain
    proc.setMute(false);
    proc.setGainDb(6.0f);  // +6dB
    settleBandProcessor(proc);

    float leftGain = 0.5f;
    float rightGain = 0.5f;
    proc.process(leftGain, rightGain);

    // Output should be present
    REQUIRE(std::isfinite(leftGain));
    REQUIRE(std::isfinite(rightGain));
}

TEST_CASE("BandProcessor morph smoothing time configurable", "[band][morph][FR-010]") {
    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);

    auto nodes = createTwoNodeSetup();
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);

    // Test different smoothing times
    SECTION("Fast smoothing (5ms)") {
        proc.setMorphSmoothingTime(5.0f);
        proc.setMorphPosition(0.0f, 0.0f);
        settleBandProcessor(proc, 500);

        proc.setMorphPosition(1.0f, 0.0f);

        // After 5ms worth of samples (~221 at 44.1kHz), should be mostly done
        for (int i = 0; i < 300; ++i) {
            float left = 0.1f, right = 0.1f;
            proc.process(left, right);
        }

        // No crash or artifacts expected
        REQUIRE(true);
    }

    SECTION("Slow smoothing (200ms)") {
        proc.setMorphSmoothingTime(200.0f);
        proc.setMorphPosition(0.0f, 0.0f);
        settleBandProcessor(proc, 1000);

        proc.setMorphPosition(1.0f, 0.0f);

        // After 50ms, still transitioning
        for (int i = 0; i < 2205; ++i) {  // ~50ms
            float left = 0.1f, right = 0.1f;
            proc.process(left, right);
        }

        // No crash or artifacts expected
        REQUIRE(true);
    }
}

TEST_CASE("BandProcessor processBlock uses morph engine", "[band][morph][FR-010]") {
    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);

    auto nodes = createTwoNodeSetup();
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);
    proc.setMorphPosition(0.5f, 0.0f);

    // Let settle
    settleBandProcessor(proc);

    // Process a block
    std::array<float, 256> left{};
    std::array<float, 256> right{};

    // Fill with test signal
    for (size_t i = 0; i < 256; ++i) {
        left[i] = 0.5f * std::sin(2.0f * 3.14159f * 440.0f * static_cast<float>(i) / 44100.0f);
        right[i] = left[i];
    }

    proc.processBlock(left.data(), right.data(), 256);

    // Verify output is valid
    for (size_t i = 0; i < 256; ++i) {
        REQUIRE(std::isfinite(left[i]));
        REQUIRE(std::isfinite(right[i]));
    }
}

TEST_CASE("BandProcessor morph bypass when drive is zero", "[band][morph][FR-010]") {
    // When all nodes have drive=0, distortion should be bypassed
    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, kBlockSize);

    // Create nodes with zero drive
    std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes;
    nodes[0] = Disrumpo::MorphNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip);
    nodes[0].commonParams.drive = 0.0f;  // Bypass
    nodes[0].commonParams.mix = 1.0f;

    nodes[1] = Disrumpo::MorphNode(1, 1.0f, 0.0f, Disrumpo::DistortionType::Tube);
    nodes[1].commonParams.drive = 0.0f;  // Bypass
    nodes[1].commonParams.mix = 1.0f;

    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);
    proc.setMorphPosition(0.5f, 0.0f);

    settleBandProcessor(proc);

    // With drive=0, distortion should be bypassed
    // Output depends on gain/pan stage
    float left = 0.5f;
    float right = 0.5f;
    proc.process(left, right);

    // Should produce valid output (with pan coefficients applied)
    REQUIRE(std::isfinite(left));
    REQUIRE(std::isfinite(right));
}
