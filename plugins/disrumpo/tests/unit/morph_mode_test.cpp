// ==============================================================================
// MorphEngine Mode Tests
// ==============================================================================
// Unit tests for morph mode behaviors (1D Linear, 2D Planar, 2D Radial).
//
// Constitution Principle XII: Test-First Development
// Reference: specs/005-morph-system/spec.md FR-003, FR-004, FR-005, SC-005
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

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

/// @brief Create standard 2-node setup (A at 0, B at 1) for 1D Linear mode.
std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> createTwoNodes() {
    std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes;
    nodes[0] = Disrumpo::MorphNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip);
    nodes[1] = Disrumpo::MorphNode(1, 1.0f, 0.0f, Disrumpo::DistortionType::Tube);
    nodes[2] = Disrumpo::MorphNode(2, 0.0f, 1.0f, Disrumpo::DistortionType::Fuzz);
    nodes[3] = Disrumpo::MorphNode(3, 1.0f, 1.0f, Disrumpo::DistortionType::SineFold);
    return nodes;
}

/// @brief Create 3-node setup for 1D Linear mode (A at 0, B at 0.5, C at 1).
std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> createThreeNodesLinear() {
    std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes;
    nodes[0] = Disrumpo::MorphNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip);
    nodes[1] = Disrumpo::MorphNode(1, 0.5f, 0.0f, Disrumpo::DistortionType::Tube);
    nodes[2] = Disrumpo::MorphNode(2, 1.0f, 0.0f, Disrumpo::DistortionType::Fuzz);
    nodes[3] = Disrumpo::MorphNode(3, 1.0f, 1.0f, Disrumpo::DistortionType::SineFold);
    return nodes;
}

/// @brief Create standard 4-node setup at corners for 2D modes.
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
// FR-003: 1D Linear Mode Tests
// =============================================================================

TEST_CASE("MorphEngine 1D Linear mode - position 0.0 gives 100% node A", "[morph][mode][linear][FR-003]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);
    engine.setMode(Disrumpo::MorphMode::Linear1D);

    engine.calculateMorphWeights(0.0f, 0.0f);
    const auto& weights = engine.getWeights();

    REQUIRE(weights[0] == Approx(1.0f).margin(0.001f));
    REQUIRE(weights[1] == Approx(0.0f).margin(0.001f));
}

TEST_CASE("MorphEngine 1D Linear mode - position 1.0 gives 100% node B", "[morph][mode][linear][FR-003]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);
    engine.setMode(Disrumpo::MorphMode::Linear1D);

    engine.calculateMorphWeights(1.0f, 0.0f);
    const auto& weights = engine.getWeights();

    REQUIRE(weights[0] == Approx(0.0f).margin(0.001f));
    REQUIRE(weights[1] == Approx(1.0f).margin(0.001f));
}

TEST_CASE("MorphEngine 1D Linear mode - position 0.5 gives 50% A, 50% B", "[morph][mode][linear][FR-003]") {
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

TEST_CASE("MorphEngine 1D Linear mode - 3 nodes at 0, 0.5, 1.0 with position 0.25", "[morph][mode][linear][FR-003]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createThreeNodesLinear();
    engine.setNodes(nodes, 3);
    engine.setMode(Disrumpo::MorphMode::Linear1D);

    // Position 0.25: distances are 0.25 (to A at 0), 0.25 (to B at 0.5), 0.75 (to C at 1.0)
    engine.calculateMorphWeights(0.25f, 0.0f);
    const auto& weights = engine.getWeights();

    // A and B should have equal weights (both 0.25 distance), C should have lower weight
    REQUIRE(weights[0] == Approx(weights[1]).margin(0.01f));
    REQUIRE(weights[0] > weights[2]);
    // Sum should be 1.0
    float sum = weights[0] + weights[1] + weights[2];
    REQUIRE(sum == Approx(1.0f).margin(0.001f));
}

TEST_CASE("MorphEngine 1D Linear mode - ignores Y position", "[morph][mode][linear][FR-003]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createTwoNodes();
    engine.setNodes(nodes, 2);
    engine.setMode(Disrumpo::MorphMode::Linear1D);

    // Different Y values should produce same weights
    engine.calculateMorphWeights(0.5f, 0.0f);
    const auto weights1 = engine.getWeights();

    engine.calculateMorphWeights(0.5f, 0.5f);
    const auto weights2 = engine.getWeights();

    engine.calculateMorphWeights(0.5f, 1.0f);
    const auto weights3 = engine.getWeights();

    REQUIRE(weights1[0] == Approx(weights2[0]).margin(0.001f));
    REQUIRE(weights2[0] == Approx(weights3[0]).margin(0.001f));
}

// =============================================================================
// FR-004: 2D Planar Mode Tests
// =============================================================================

TEST_CASE("MorphEngine 2D Planar mode - cursor at (0,0) gives node A 100%", "[morph][mode][planar][FR-004]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createFourCornerNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Planar2D);

    engine.calculateMorphWeights(0.0f, 0.0f);
    const auto& weights = engine.getWeights();

    REQUIRE(weights[0] == Approx(1.0f).margin(0.001f));
    REQUIRE(weights[1] == Approx(0.0f).margin(0.001f));
    REQUIRE(weights[2] == Approx(0.0f).margin(0.001f));
    REQUIRE(weights[3] == Approx(0.0f).margin(0.001f));
}

TEST_CASE("MorphEngine 2D Planar mode - cursor at (0.5, 0.5) gives all 4 nodes 25% each", "[morph][mode][planar][FR-004]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createFourCornerNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Planar2D);

    engine.calculateMorphWeights(0.5f, 0.5f);
    const auto& weights = engine.getWeights();

    REQUIRE(weights[0] == Approx(0.25f).margin(0.01f));
    REQUIRE(weights[1] == Approx(0.25f).margin(0.01f));
    REQUIRE(weights[2] == Approx(0.25f).margin(0.01f));
    REQUIRE(weights[3] == Approx(0.25f).margin(0.01f));
}

TEST_CASE("MorphEngine 2D Planar mode - cursor at (0.25, 0.25) gives node A highest weight", "[morph][mode][planar][FR-004]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createFourCornerNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Planar2D);

    engine.calculateMorphWeights(0.25f, 0.25f);
    const auto& weights = engine.getWeights();

    // Node A at (0,0) should have highest weight (closest)
    REQUIRE(weights[0] > weights[1]);  // A > B
    REQUIRE(weights[0] > weights[2]);  // A > C
    REQUIRE(weights[0] > weights[3]);  // A > D

    // Node D at (1,1) should have lowest weight (farthest)
    REQUIRE(weights[3] < weights[1]);
    REQUIRE(weights[3] < weights[2]);
}

TEST_CASE("MorphEngine 2D Planar mode - cursor at each corner", "[morph][mode][planar][FR-004]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createFourCornerNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Planar2D);

    SECTION("top-left corner (0,0) - node A") {
        engine.calculateMorphWeights(0.0f, 0.0f);
        const auto& weights = engine.getWeights();
        REQUIRE(weights[0] == Approx(1.0f).margin(0.001f));
    }

    SECTION("top-right corner (1,0) - node B") {
        engine.calculateMorphWeights(1.0f, 0.0f);
        const auto& weights = engine.getWeights();
        REQUIRE(weights[1] == Approx(1.0f).margin(0.001f));
    }

    SECTION("bottom-left corner (0,1) - node C") {
        engine.calculateMorphWeights(0.0f, 1.0f);
        const auto& weights = engine.getWeights();
        REQUIRE(weights[2] == Approx(1.0f).margin(0.001f));
    }

    SECTION("bottom-right corner (1,1) - node D") {
        engine.calculateMorphWeights(1.0f, 1.0f);
        const auto& weights = engine.getWeights();
        REQUIRE(weights[3] == Approx(1.0f).margin(0.001f));
    }
}

// =============================================================================
// FR-005: 2D Radial Mode Tests
// =============================================================================

TEST_CASE("MorphEngine 2D Radial mode - center gives all nodes equal weight", "[morph][mode][radial][FR-005]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createFourCornerNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Radial2D);

    // Center is at (0.5, 0.5)
    engine.calculateMorphWeights(0.5f, 0.5f);
    const auto& weights = engine.getWeights();

    // All nodes should have equal weight at center
    REQUIRE(weights[0] == Approx(0.25f).margin(0.01f));
    REQUIRE(weights[1] == Approx(0.25f).margin(0.01f));
    REQUIRE(weights[2] == Approx(0.25f).margin(0.01f));
    REQUIRE(weights[3] == Approx(0.25f).margin(0.01f));
}

TEST_CASE("MorphEngine 2D Radial mode - edge toward corner gives that node highest weight", "[morph][mode][radial][FR-005]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createFourCornerNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Radial2D);

    // Position toward top-left corner (node A at 0,0)
    // Moving from center (0.5, 0.5) toward (0, 0)
    engine.calculateMorphWeights(0.1f, 0.1f);
    const auto& weights = engine.getWeights();

    // Node A should have highest weight (angle points toward it)
    REQUIRE(weights[0] > weights[1]);
    REQUIRE(weights[0] > weights[2]);
    REQUIRE(weights[0] > weights[3]);
}

TEST_CASE("MorphEngine 2D Radial mode - weights sum to 1.0", "[morph][mode][radial][FR-005]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createFourCornerNodes();
    engine.setNodes(nodes, 4);
    engine.setMode(Disrumpo::MorphMode::Radial2D);

    SECTION("center position") {
        engine.calculateMorphWeights(0.5f, 0.5f);
        const auto& weights = engine.getWeights();
        float sum = weights[0] + weights[1] + weights[2] + weights[3];
        REQUIRE(sum == Approx(1.0f).margin(0.001f));
    }

    SECTION("edge position") {
        engine.calculateMorphWeights(0.9f, 0.5f);
        const auto& weights = engine.getWeights();
        float sum = weights[0] + weights[1] + weights[2] + weights[3];
        REQUIRE(sum == Approx(1.0f).margin(0.001f));
    }

    SECTION("arbitrary position") {
        engine.calculateMorphWeights(0.3f, 0.7f);
        const auto& weights = engine.getWeights();
        float sum = weights[0] + weights[1] + weights[2] + weights[3];
        REQUIRE(sum == Approx(1.0f).margin(0.001f));
    }
}

// =============================================================================
// Mode Switching Tests
// =============================================================================

TEST_CASE("MorphEngine mode switching produces different weights", "[morph][mode]") {
    Disrumpo::MorphEngine engine;
    prepareTestEngine(engine);
    auto nodes = createFourCornerNodes();
    engine.setNodes(nodes, 4);

    // Position that should give different results in different modes
    constexpr float testX = 0.3f;
    constexpr float testY = 0.7f;

    engine.setMode(Disrumpo::MorphMode::Linear1D);
    engine.calculateMorphWeights(testX, testY);
    const auto linear1DWeights = engine.getWeights();

    engine.setMode(Disrumpo::MorphMode::Planar2D);
    engine.calculateMorphWeights(testX, testY);
    const auto planar2DWeights = engine.getWeights();

    engine.setMode(Disrumpo::MorphMode::Radial2D);
    engine.calculateMorphWeights(testX, testY);
    const auto radial2DWeights = engine.getWeights();

    // Linear1D ignores Y, so it should differ from 2D modes
    bool linearDifferent = false;
    for (int i = 0; i < 4; ++i) {
        if (std::abs(linear1DWeights[i] - planar2DWeights[i]) > 0.01f) {
            linearDifferent = true;
            break;
        }
    }
    REQUIRE(linearDifferent);

    // All modes should sum to 1.0
    float sum1D = linear1DWeights[0] + linear1DWeights[1] + linear1DWeights[2] + linear1DWeights[3];
    float sum2DP = planar2DWeights[0] + planar2DWeights[1] + planar2DWeights[2] + planar2DWeights[3];
    float sum2DR = radial2DWeights[0] + radial2DWeights[1] + radial2DWeights[2] + radial2DWeights[3];

    REQUIRE(sum1D == Approx(1.0f).margin(0.001f));
    REQUIRE(sum2DP == Approx(1.0f).margin(0.001f));
    REQUIRE(sum2DR == Approx(1.0f).margin(0.001f));
}
