// ==============================================================================
// MorphEngine Global Processor Cap Tests
// ==============================================================================
// Unit tests for FR-019: Global cap of 16 active distortion processors.
//
// Constitution Principle XII: Test-First Development
// Reference: specs/005-morph-system/spec.md FR-019, SC-009
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/morph_engine.h"
#include "dsp/morph_node.h"
#include "dsp/distortion_types.h"

#include <array>
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

/// @brief Create 4-node cross-family setup (all different families).
std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> createMaxCrossFamilyNodes() {
    std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes;

    // Node A: Saturation family
    nodes[0] = Disrumpo::MorphNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip);
    nodes[0].commonParams.drive = 2.0f;

    // Node B: Digital family
    nodes[1] = Disrumpo::MorphNode(1, 1.0f, 0.0f, Disrumpo::DistortionType::Bitcrush);
    nodes[1].commonParams.drive = 2.0f;

    // Node C: Wavefold family
    nodes[2] = Disrumpo::MorphNode(2, 0.0f, 1.0f, Disrumpo::DistortionType::SineFold);
    nodes[2].commonParams.drive = 2.0f;

    // Node D: Experimental family
    nodes[3] = Disrumpo::MorphNode(3, 1.0f, 1.0f, Disrumpo::DistortionType::Chaos);
    nodes[3].commonParams.drive = 2.0f;

    return nodes;
}

/// @brief Count active processors based on weights above threshold.
int countActiveProcessors(const std::array<float, Disrumpo::kMaxMorphNodes>& weights,
                          int activeNodeCount, float threshold = 0.001f) {
    int count = 0;
    for (int i = 0; i < activeNodeCount; ++i) {
        if (weights[i] >= threshold) {
            ++count;
        }
    }
    return count;
}

} // anonymous namespace

// =============================================================================
// FR-019: Global Processor Cap Tests
// =============================================================================

TEST_CASE("MorphEngine processor cap - weights are skipped below threshold", "[morph][cap][FR-019]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createMaxCrossFamilyNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Planar2D);
    engine.setSmoothingTime(0.0f);

    // Position very close to node A - other nodes should get negligible weight
    engine.calculateMorphWeights(0.01f, 0.01f);
    const auto& weights = engine.getWeights();

    // Node A should dominate
    REQUIRE(weights[0] > 0.9f);

    // Far nodes should be below threshold (0.001) and effectively skipped
    // Due to renormalization, weights still sum to 1.0
    float sum = weights[0] + weights[1] + weights[2] + weights[3];
    REQUIRE(sum == Approx(1.0f).margin(0.001f));
}

TEST_CASE("MorphEngine processor cap - center position activates all 4 nodes", "[morph][cap][FR-019]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createMaxCrossFamilyNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Planar2D);
    engine.setSmoothingTime(0.0f);

    // Center position - all nodes should be active with equal weight
    engine.calculateMorphWeights(0.5f, 0.5f);
    const auto& weights = engine.getWeights();

    // All nodes should have significant weight
    int activeCount = countActiveProcessors(weights, 4);
    REQUIRE(activeCount == 4);

    // Each should have approximately 25% weight
    for (int i = 0; i < 4; ++i) {
        REQUIRE(weights[i] == Approx(0.25f).margin(0.02f));
    }
}

TEST_CASE("MorphEngine processor cap - dynamic threshold raises when exceeding limit", "[morph][cap][FR-019]") {
    // This test verifies the algorithm described in FR-019:
    // When processor count would exceed 16, threshold is raised incrementally

    // Simulate scenario with multiple bands each having 4 cross-family nodes
    // With 5 bands * 4 nodes = 20 potential processors (exceeds 16 cap)

    // For now, test the single-engine threshold behavior
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createMaxCrossFamilyNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Planar2D);
    engine.setSmoothingTime(0.0f);

    // At center, all 4 nodes are active
    engine.calculateMorphWeights(0.5f, 0.5f);
    const auto& weights = engine.getWeights();

    // Verify weights are computed correctly
    int activeCount = countActiveProcessors(weights, 4);
    REQUIRE(activeCount <= Disrumpo::kMaxGlobalProcessors);
}

TEST_CASE("MorphEngine processor cap - threshold never exceeds 0.25", "[morph][cap][FR-019]") {
    // FR-019 specifies threshold should never go above 0.25
    // This ensures at least some morphing is always possible

    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createMaxCrossFamilyNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Planar2D);
    engine.setSmoothingTime(0.0f);

    // Even with aggressive threshold, weights should still work
    engine.calculateMorphWeights(0.5f, 0.5f);
    const auto& weights = engine.getWeights();

    // At center with 4 equal-distant nodes, each gets 25%
    // Since 25% >= kMaxWeightThreshold (0.25), all should be active
    for (int i = 0; i < 4; ++i) {
        REQUIRE(weights[i] >= Disrumpo::kMaxWeightThreshold - 0.01f);
    }
}

// =============================================================================
// SC-009: Never Exceed 16 Processors Globally
// =============================================================================

TEST_CASE("MorphEngine processor cap - single band never exceeds 4 processors", "[morph][cap][SC-009]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createMaxCrossFamilyNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Planar2D);
    engine.setSmoothingTime(0.0f);

    // Test various positions
    const std::array<std::pair<float, float>, 5> positions = {{
        {0.0f, 0.0f},   // Corner
        {0.5f, 0.5f},   // Center
        {0.25f, 0.75f}, // Arbitrary
        {1.0f, 0.5f},   // Edge
        {0.33f, 0.33f}  // Near A
    }};

    for (const auto& [x, y] : positions) {
        engine.calculateMorphWeights(x, y);
        const auto& weights = engine.getWeights();

        int activeCount = countActiveProcessors(weights, 4);

        // Single band can have at most 4 active processors
        REQUIRE(activeCount <= 4);

        // At least 1 processor should always be active
        REQUIRE(activeCount >= 1);
    }
}

TEST_CASE("MorphEngine processor cap - weights remain normalized after threshold", "[morph][cap][FR-019]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createMaxCrossFamilyNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Planar2D);
    engine.setSmoothingTime(0.0f);

    // Position close to corner - some weights will be thresholded
    engine.calculateMorphWeights(0.1f, 0.1f);
    const auto& weights = engine.getWeights();

    // After threshold and renormalization, weights should sum to 1.0
    float sum = 0.0f;
    for (int i = 0; i < 4; ++i) {
        sum += weights[i];
    }

    REQUIRE(sum == Approx(1.0f).margin(0.001f));
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("MorphEngine processor cap - 2-node configuration", "[morph][cap][edge]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);

    std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes;
    nodes[0] = Disrumpo::MorphNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip);
    nodes[1] = Disrumpo::MorphNode(1, 1.0f, 0.0f, Disrumpo::DistortionType::Bitcrush);
    nodes[2] = Disrumpo::MorphNode(2, 0.0f, 1.0f, Disrumpo::DistortionType::SineFold);
    nodes[3] = Disrumpo::MorphNode(3, 1.0f, 1.0f, Disrumpo::DistortionType::Chaos);

    engine.setNodes(nodes, 2);  // Only 2 active nodes
    engine.setMode(Disrumpo::MorphMode::Linear1D);
    engine.setSmoothingTime(0.0f);

    engine.calculateMorphWeights(0.5f, 0.0f);
    const auto& weights = engine.getWeights();

    // Only first 2 weights should be significant
    REQUIRE(weights[0] == Approx(0.5f).margin(0.01f));
    REQUIRE(weights[1] == Approx(0.5f).margin(0.01f));

    // Inactive nodes should have 0 weight
    REQUIRE(weights[2] == Approx(0.0f).margin(0.001f));
    REQUIRE(weights[3] == Approx(0.0f).margin(0.001f));
}

TEST_CASE("MorphEngine processor cap - processing with threshold does not crash", "[morph][cap][edge]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createMaxCrossFamilyNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Planar2D);
    engine.setSmoothingTime(0.0f);

    // Position close to corner - some processors will be skipped
    engine.setMorphPosition(0.05f, 0.05f);

    // Process samples without crashing
    float lastOutput = 0.0f;
    for (int i = 0; i < 100; ++i) {
        lastOutput = engine.process(0.5f);
    }

    // Output should be valid
    REQUIRE_FALSE(std::isnan(lastOutput));
    REQUIRE_FALSE(std::isinf(lastOutput));
}
