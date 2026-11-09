// ==============================================================================
// MorphEngine Weight Computation Tests
// ==============================================================================
// Unit tests for inverse distance weighting algorithm.
//
// Constitution Principle XII: Test-First Development
// Reference: specs/005-morph-system/spec.md FR-001, FR-014, FR-015, SC-001, SC-005
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "dsp/morph_engine.h"
#include "dsp/morph_node.h"
#include "dsp/distortion_types.h"
#include "dsp/band_processor.h"
#include "controller/views/morph_pad.h"

#include <cmath>
#include <cstdio>
#include <utility>

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
    // MorphEngine is ~936 KB (five DistortionAdapters); on the stack it overflows Windows'
    // 1 MB thread stack, so it lives on the heap here as it does in BandProcessor.
    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
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
    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
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
    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
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
    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
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
    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
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
    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
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
    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
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
    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
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
    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
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
    auto engineOwner = std::make_unique<Disrumpo::MorphEngine>();
    auto& engine = *engineOwner;
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

// =============================================================================
// D-089 (fix plan T4.02): one IDW weight function
// =============================================================================
// The weights the MorphEngine applies are the ground truth. The band's
// oversampling choice (BandProcessor::computeRawMorphWeights) and the morph pad's
// node brightness (MorphPad::getNodeWeight) must show the same weights for every
// mode, node count and cursor position. Separately compiled call sites: compared
// within 1e-6, not bit for bit.

TEST_CASE("Morph engine weights match the band oversampling and morph pad weights", "[disrumpo][morph][weight][D-089]") {
    using namespace Disrumpo;
    using Layout = std::array<std::pair<float, float>, kMaxMorphNodes>;
    const std::array<Layout, 2> layouts{{
        {{{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}}},     // the default corners
        {{{0.1f, 0.2f}, {0.8f, 0.15f}, {0.3f, 0.9f}, {0.65f, 0.6f}}},  // moved nodes
    }};
    const std::array<MorphMode, 3> modes{MorphMode::Linear1D, MorphMode::Planar2D, MorphMode::Radial2D};
    const std::array<const char*, 3> modeNames{"Linear1D", "Planar2D", "Radial2D"};

    auto engine = std::make_unique<MorphEngine>();
    engine->prepare(48000.0, 512);
    auto band = std::make_unique<BandProcessor>();
    band->prepare(48000.0, 512);
    MorphPad pad(VSTGUI::CRect(0, 0, 250, 200));

    int compared = 0;
    int bandOff = 0;
    int padOff = 0;
    for (size_t l = 0; l < layouts.size(); ++l) {
        std::array<MorphNode, kMaxMorphNodes> nodes{};
        for (int i = 0; i < kMaxMorphNodes; ++i) {
            const auto& pos = layouts[l][static_cast<size_t>(i)];
            nodes[static_cast<size_t>(i)] = MorphNode(i, pos.first, pos.second, DistortionType::SoftClip);
        }
        for (int count = 2; count <= kMaxMorphNodes; ++count) {
            for (size_t m = 0; m < modes.size(); ++m) {
                engine->setNodes(nodes, count);
                engine->setMode(modes[m]);
                band->setMorphNodes(nodes, count);
                band->setMorphMode(modes[m]);
                pad.setActiveNodeCount(count);
                for (int i = 0; i < kMaxMorphNodes; ++i) {
                    pad.setNodePosition(i, nodes[static_cast<size_t>(i)].posX, nodes[static_cast<size_t>(i)].posY);
                }
                pad.setMorphMode(modes[m]);
                for (int xi = 0; xi < 5; ++xi) {
                    for (int yi = 0; yi < 5; ++yi) {
                        const float x = static_cast<float>(xi) / 4.0f;
                        const float y = static_cast<float>(yi) / 4.0f;
                        engine->calculateMorphWeights(x, y);
                        const auto eng = engine->getWeights();
                        band->setMorphPosition(x, y);
                        const auto bw = band->computeRawMorphWeights();
                        pad.setMorphPosition(x, y);
                        const auto ref = MorphEngine::computeWeights(modes[m], nodes, count, x, y);
                        bool bandBad = false;
                        bool padBad = false;
                        for (int i = 0; i < kMaxMorphNodes; ++i) {
                            const auto k = static_cast<size_t>(i);
                            CHECK(eng[k] == ref[k]);
                            bandBad = bandBad || std::fabs(bw[k] - eng[k]) >= 1e-6f;
                            padBad = padBad || std::fabs(pad.getNodeWeight(i) - eng[k]) >= 1e-6f;
                        }
                        ++compared;
                        bandOff += bandBad ? 1 : 0;
                        padOff += padBad ? 1 : 0;
                        if ((bandBad || padBad) && (bandOff + padOff) <= 12) {
                            std::printf("T4.02 layout %zu, %d nodes, %s, cursor (%.2f, %.2f): engine %.4f %.4f %.4f %.4f"
                                        " | band %.4f %.4f %.4f %.4f | pad %.4f %.4f %.4f %.4f\n",
                                        l, count, modeNames[m], x, y, eng[0], eng[1], eng[2], eng[3], bw[0], bw[1],
                                        bw[2], bw[3], pad.getNodeWeight(0), pad.getNodeWeight(1),
                                        pad.getNodeWeight(2), pad.getNodeWeight(3));
                        }
                    }
                }
            }
        }
    }
    std::printf("T4.02 %d (layout, count, mode, cursor) cases: band differs in %d, pad differs in %d\n", compared,
                bandOff, padOff);
    CHECK(bandOff == 0);
    CHECK(padOff == 0);
}
