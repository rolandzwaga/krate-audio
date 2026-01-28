// ==============================================================================
// MorphEngine Weight Computation Tests
// ==============================================================================
// Unit tests for inverse distance weighting algorithm.
//
// Constitution Principle XII: Test-First Development
// Reference: specs/005-morph-system/spec.md FR-001, FR-014, FR-015, SC-001, SC-005
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "dsp/morph_engine.h"
#include "dsp/morph_node.h"
#include "dsp/distortion_types.h"

using Catch::Approx;

// =============================================================================
// Test Fixtures
// =============================================================================

namespace {

/// @brief Configure a MorphEngine for testing.
void prepareTestEngine(Disrumpo::MorphEngine& engine) {
    engine.prepare(44100.0, 512);
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

/// @brief Create standard 4-node setup at corners.
std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> createFourCornerNodes() {
    std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes;
    nodes[0] = Disrumpo::MorphNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip);   // Top-left
    nodes[1] = Disrumpo::MorphNode(1, 1.0f, 0.0f, Disrumpo::DistortionType::Tube);       // Top-right
    nodes[2] = Disrumpo::MorphNode(2, 0.0f, 1.0f, Disrumpo::DistortionType::Fuzz);       // Bottom-left
    nodes[3] = Disrumpo::MorphNode(3, 1.0f, 1.0f, Disrumpo::DistortionType::SineFold);   // Bottom-right
    return nodes;
}

} // anonymous namespace

// =============================================================================
// FR-001: Inverse Distance Weighting Tests
// =============================================================================

TEST_CASE("MorphEngine weight computation - cursor at node position gives 100% weight", "[morph][weight][FR-001]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);
    engine.setMode(Disrumpo::MorphMode::Linear1D);

    SECTION("cursor at node A (position 0.0)") {
        engine.calculateMorphWeights(0.0f, 0.0f);
        const auto& weights = engine.getWeights();

        REQUIRE(weights[0] == Approx(1.0f).margin(0.001f));
        REQUIRE(weights[1] == Approx(0.0f).margin(0.001f));
    }

    SECTION("cursor at node B (position 1.0)") {
        engine.calculateMorphWeights(1.0f, 0.0f);
        const auto& weights = engine.getWeights();

        REQUIRE(weights[0] == Approx(0.0f).margin(0.001f));
        REQUIRE(weights[1] == Approx(1.0f).margin(0.001f));
    }
}

TEST_CASE("MorphEngine weight computation - equidistant from 2 nodes gives 50/50 weights", "[morph][weight][FR-001]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);
    engine.setMode(Disrumpo::MorphMode::Linear1D);

    engine.calculateMorphWeights(0.5f, 0.0f);
    const auto& weights = engine.getWeights();

    REQUIRE(weights[0] == Approx(0.5f).margin(0.01f));
    REQUIRE(weights[1] == Approx(0.5f).margin(0.01f));
}

TEST_CASE("MorphEngine weight computation - 4 nodes at corners, cursor at center gives 25% each", "[morph][weight][FR-001]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createFourCornerNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Planar2D);

    // Center is at (0.5, 0.5) - equidistant from all corners
    engine.calculateMorphWeights(0.5f, 0.5f);
    const auto& weights = engine.getWeights();

    REQUIRE(weights[0] == Approx(0.25f).margin(0.01f));
    REQUIRE(weights[1] == Approx(0.25f).margin(0.01f));
    REQUIRE(weights[2] == Approx(0.25f).margin(0.01f));
    REQUIRE(weights[3] == Approx(0.25f).margin(0.01f));
}

// =============================================================================
// FR-014: Determinism Tests
// =============================================================================

TEST_CASE("MorphEngine weight computation - deterministic (same inputs always produce same weights)", "[morph][weight][FR-014]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createFourCornerNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Planar2D);

    // Calculate weights multiple times with same input
    engine.calculateMorphWeights(0.3f, 0.7f);
    const auto weights1 = engine.getWeights();

    engine.calculateMorphWeights(0.3f, 0.7f);
    const auto weights2 = engine.getWeights();

    engine.calculateMorphWeights(0.3f, 0.7f);
    const auto weights3 = engine.getWeights();

    // All calculations must produce identical results
    for (int i = 0; i < 4; ++i) {
        REQUIRE(weights1[i] == weights2[i]);
        REQUIRE(weights2[i] == weights3[i]);
    }
}

// =============================================================================
// FR-015: Weight Threshold Tests
// =============================================================================

TEST_CASE("MorphEngine weight computation - weights below threshold are skipped and renormalized", "[morph][weight][FR-015]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createFourCornerNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Planar2D);

    // Position very close to node A (0,0) - other nodes should have tiny weights
    engine.calculateMorphWeights(0.01f, 0.01f);
    const auto& weights = engine.getWeights();

    // Weights should sum to 1.0 (normalized)
    float sum = weights[0] + weights[1] + weights[2] + weights[3];
    REQUIRE(sum == Approx(1.0f).margin(0.001f));

    // Node A should have very high weight, far nodes should be thresholded to 0
    REQUIRE(weights[0] > 0.9f);  // Node A dominant
}

TEST_CASE("MorphEngine weight computation - weights sum to 1.0 (normalized)", "[morph][weight][SC-005]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createFourCornerNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Planar2D);

    SECTION("center position") {
        engine.calculateMorphWeights(0.5f, 0.5f);
        const auto& weights = engine.getWeights();
        float sum = weights[0] + weights[1] + weights[2] + weights[3];
        REQUIRE(sum == Approx(1.0f).margin(0.001f));
    }

    SECTION("corner position") {
        engine.calculateMorphWeights(0.0f, 0.0f);
        const auto& weights = engine.getWeights();
        float sum = weights[0] + weights[1] + weights[2] + weights[3];
        REQUIRE(sum == Approx(1.0f).margin(0.001f));
    }

    SECTION("random position") {
        engine.calculateMorphWeights(0.37f, 0.82f);
        const auto& weights = engine.getWeights();
        float sum = weights[0] + weights[1] + weights[2] + weights[3];
        REQUIRE(sum == Approx(1.0f).margin(0.001f));
    }
}

// =============================================================================
// SC-001: Performance Benchmark
// =============================================================================

TEST_CASE("MorphEngine weight computation - benchmark performance", "[morph][weight][SC-001][benchmark]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createFourCornerNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Planar2D);

    BENCHMARK("calculateMorphWeights for 4 nodes") {
        engine.calculateMorphWeights(0.5f, 0.5f);
        return engine.getWeights()[0];  // Prevent optimization
    };
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("MorphEngine weight computation - handles edge positions correctly", "[morph][weight][edge]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createFourCornerNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Planar2D);

    SECTION("cursor at exact corner") {
        engine.calculateMorphWeights(1.0f, 1.0f);
        const auto& weights = engine.getWeights();
        // Node D at (1,1) should have 100% weight
        REQUIRE(weights[3] == Approx(1.0f).margin(0.001f));
    }

    SECTION("cursor on edge") {
        engine.calculateMorphWeights(0.5f, 0.0f);
        const auto& weights = engine.getWeights();
        // Equidistant from A(0,0) and B(1,0), much farther from C and D
        REQUIRE(weights[0] == Approx(weights[1]).margin(0.01f));
        REQUIRE(weights[0] > weights[2]);
        REQUIRE(weights[0] > weights[3]);
    }
}

TEST_CASE("MorphEngine weight computation - 2-node linear interpolation", "[morph][weight][linear]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);
    engine.setMode(Disrumpo::MorphMode::Linear1D);

    SECTION("25% position") {
        engine.calculateMorphWeights(0.25f, 0.0f);
        const auto& weights = engine.getWeights();
        // At 0.25, distances are 0.25 (to A) and 0.75 (to B)
        // IDW with p=2: w_A = 1/0.25^2 = 16, w_B = 1/0.75^2 = 1.78
        // Normalized: w_A = 16/17.78 = 0.9, w_B = 1.78/17.78 = 0.1
        // With threshold, weights may be adjusted
        REQUIRE(weights[0] > weights[1]);  // Node A should have higher weight
    }

    SECTION("75% position") {
        engine.calculateMorphWeights(0.75f, 0.0f);
        const auto& weights = engine.getWeights();
        REQUIRE(weights[1] > weights[0]);  // Node B should have higher weight
    }
}

TEST_CASE("MorphEngine weight computation - nodes at same position", "[morph][weight][edge]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes;
    // All nodes at center
    nodes[0] = Disrumpo::MorphNode(0, 0.5f, 0.5f, Disrumpo::DistortionType::SoftClip);
    nodes[1] = Disrumpo::MorphNode(1, 0.5f, 0.5f, Disrumpo::DistortionType::Tube);
    nodes[2] = Disrumpo::MorphNode(2, 0.5f, 0.5f, Disrumpo::DistortionType::Fuzz);
    nodes[3] = Disrumpo::MorphNode(3, 0.5f, 0.5f, Disrumpo::DistortionType::SineFold);
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Planar2D);

    // Cursor at same position as all nodes
    engine.calculateMorphWeights(0.5f, 0.5f);
    const auto& weights = engine.getWeights();

    // Should handle gracefully (first node gets 100% weight since cursor is "on" it)
    float sum = weights[0] + weights[1] + weights[2] + weights[3];
    REQUIRE(sum == Approx(1.0f).margin(0.001f));
}
