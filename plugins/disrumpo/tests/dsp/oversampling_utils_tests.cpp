// ==============================================================================
// Oversampling Utilities Tests
// ==============================================================================
// Tests for oversampling factor computation: roundUpToPowerOf2Factor(),
// getSingleTypeOversampleFactor(), and calculateMorphOversampleFactor().
//
// Reference: specs/009-intelligent-oversampling/spec.md
// Tasks: T11.006, T11.007, T11.008, T11.009
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/oversampling_utils.h"
#include "dsp/distortion_types.h"
#include "dsp/morph_node.h"

using namespace Disrumpo;

// =============================================================================
// T11.006: roundUpToPowerOf2Factor() tests (FR-004)
// =============================================================================

TEST_CASE("roundUpToPowerOf2Factor: maps weighted average to power-of-2 factor",
          "[oversampling][utils][FR-004]") {

    SECTION("exact values map correctly") {
        CHECK(roundUpToPowerOf2Factor(1.0f) == 1);
        CHECK(roundUpToPowerOf2Factor(2.0f) == 2);
        CHECK(roundUpToPowerOf2Factor(4.0f) == 4);
    }

    SECTION("values between 1 and 2 round up to 2") {
        CHECK(roundUpToPowerOf2Factor(1.1f) == 2);
        CHECK(roundUpToPowerOf2Factor(1.5f) == 2);
        CHECK(roundUpToPowerOf2Factor(1.9f) == 2);
    }

    SECTION("values between 2 and 4 round up to 4") {
        CHECK(roundUpToPowerOf2Factor(2.1f) == 4);
        CHECK(roundUpToPowerOf2Factor(2.5f) == 4);
        CHECK(roundUpToPowerOf2Factor(3.0f) == 4);
        CHECK(roundUpToPowerOf2Factor(3.5f) == 4);
        CHECK(roundUpToPowerOf2Factor(3.9f) == 4);
    }

    SECTION("zero or sub-1 values return 1") {
        CHECK(roundUpToPowerOf2Factor(0.0f) == 1);
        CHECK(roundUpToPowerOf2Factor(0.5f) == 1);
        CHECK(roundUpToPowerOf2Factor(-1.0f) == 1);
    }

    SECTION("values above 4 return 4") {
        CHECK(roundUpToPowerOf2Factor(5.0f) == 4);
        CHECK(roundUpToPowerOf2Factor(8.0f) == 4);
    }
}

// =============================================================================
// T11.007: getSingleTypeOversampleFactor() tests (FR-007, FR-008)
// =============================================================================

TEST_CASE("getSingleTypeOversampleFactor: returns recommended factor clamped to limit",
          "[oversampling][utils][FR-007][FR-008]") {

    SECTION("no clamping when limit >= recommended") {
        // SoftClip is 2x, limit is 4 -> returns 2
        CHECK(getSingleTypeOversampleFactor(DistortionType::SoftClip, 4) == 2);
        // HardClip is 4x, limit is 4 -> returns 4
        CHECK(getSingleTypeOversampleFactor(DistortionType::HardClip, 4) == 4);
        // Bitcrush is 1x, limit is 4 -> returns 1
        CHECK(getSingleTypeOversampleFactor(DistortionType::Bitcrush, 4) == 1);
    }

    SECTION("clamping when limit < recommended") {
        // HardClip is 4x, limit is 2 -> returns 2
        CHECK(getSingleTypeOversampleFactor(DistortionType::HardClip, 2) == 2);
        // HardClip is 4x, limit is 1 -> returns 1
        CHECK(getSingleTypeOversampleFactor(DistortionType::HardClip, 1) == 1);
        // SoftClip is 2x, limit is 1 -> returns 1
        CHECK(getSingleTypeOversampleFactor(DistortionType::SoftClip, 1) == 1);
    }

    SECTION("limit of 8 does not force higher factors") {
        // HardClip is 4x, limit is 8 -> returns 4 (not 8)
        CHECK(getSingleTypeOversampleFactor(DistortionType::HardClip, 8) == 4);
        // SoftClip is 2x, limit is 8 -> returns 2 (not 8)
        CHECK(getSingleTypeOversampleFactor(DistortionType::SoftClip, 8) == 2);
    }
}

// =============================================================================
// T11.008: calculateMorphOversampleFactor() - all 26 types individually (FR-001, FR-002, SC-008)
// =============================================================================

TEST_CASE("calculateMorphOversampleFactor: single node (all 26 types individually)",
          "[oversampling][utils][FR-001][FR-002][SC-008]") {

    // For a single-node scenario, weight[0] = 1.0 and activeNodeCount = 1
    // (In practice morph always has >= 2 nodes, but this tests the per-type mapping)

    auto testSingleNode = [](DistortionType type, int expectedFactor) {
        std::array<MorphNode, kMaxMorphNodes> nodes{};
        nodes[0].type = type;
        std::array<float, kMaxMorphNodes> weights = {1.0f, 0.0f, 0.0f, 0.0f};
        int result = calculateMorphOversampleFactor(nodes, weights, 1, 8);
        INFO("Type: " << static_cast<int>(type) << " expected: " << expectedFactor);
        CHECK(result == expectedFactor);
    };

    SECTION("4x types") {
        testSingleNode(DistortionType::HardClip, 4);
        testSingleNode(DistortionType::Fuzz, 4);
        testSingleNode(DistortionType::AsymmetricFuzz, 4);
        testSingleNode(DistortionType::SineFold, 4);
        testSingleNode(DistortionType::TriangleFold, 4);
        testSingleNode(DistortionType::SergeFold, 4);
        testSingleNode(DistortionType::FullRectify, 4);
        testSingleNode(DistortionType::HalfRectify, 4);
        testSingleNode(DistortionType::RingSaturation, 4);
        testSingleNode(DistortionType::AllpassResonant, 4);
    }

    SECTION("1x types") {
        testSingleNode(DistortionType::Bitcrush, 1);
        testSingleNode(DistortionType::SampleReduce, 1);
        testSingleNode(DistortionType::Quantize, 1);
        testSingleNode(DistortionType::Aliasing, 1);
        testSingleNode(DistortionType::BitwiseMangler, 1);
        testSingleNode(DistortionType::Spectral, 1);
    }

    SECTION("2x types") {
        testSingleNode(DistortionType::SoftClip, 2);
        testSingleNode(DistortionType::Tube, 2);
        testSingleNode(DistortionType::Tape, 2);
        testSingleNode(DistortionType::Temporal, 2);
        testSingleNode(DistortionType::FeedbackDist, 2);
        testSingleNode(DistortionType::Chaos, 2);
        testSingleNode(DistortionType::Formant, 2);
        testSingleNode(DistortionType::Granular, 2);
        testSingleNode(DistortionType::Fractal, 2);
        testSingleNode(DistortionType::Stochastic, 2);
    }
}

// =============================================================================
// T11.009: calculateMorphOversampleFactor() - morph-weighted computation (FR-003, FR-004, SC-009)
// 20+ weight combinations
// =============================================================================

TEST_CASE("calculateMorphOversampleFactor: morph-weighted computation",
          "[oversampling][utils][FR-003][FR-004][SC-009]") {

    // Helper to create a 2-node morph
    auto twoNodeResult = [](DistortionType a, DistortionType b,
                            float wA, float wB, int limit = 8) -> int {
        std::array<MorphNode, kMaxMorphNodes> nodes{};
        nodes[0].type = a;
        nodes[1].type = b;
        std::array<float, kMaxMorphNodes> weights = {wA, wB, 0.0f, 0.0f};
        return calculateMorphOversampleFactor(nodes, weights, 2, limit);
    };

    SECTION("all nodes same type") {
        // SoftClip (2x) + SoftClip (2x) = avg 2.0 -> 2
        CHECK(twoNodeResult(DistortionType::SoftClip, DistortionType::SoftClip,
                            0.5f, 0.5f) == 2);
        // HardClip (4x) + HardClip (4x) = avg 4.0 -> 4
        CHECK(twoNodeResult(DistortionType::HardClip, DistortionType::HardClip,
                            0.5f, 0.5f) == 4);
        // Bitcrush (1x) + Bitcrush (1x) = avg 1.0 -> 1
        CHECK(twoNodeResult(DistortionType::Bitcrush, DistortionType::Bitcrush,
                            0.5f, 0.5f) == 1);
    }

    SECTION("equidistant weights between different factor types") {
        // SoftClip (2x) + HardClip (4x) at 0.5/0.5 = avg 3.0 -> 4
        CHECK(twoNodeResult(DistortionType::SoftClip, DistortionType::HardClip,
                            0.5f, 0.5f) == 4);
        // Bitcrush (1x) + SoftClip (2x) at 0.5/0.5 = avg 1.5 -> 2
        CHECK(twoNodeResult(DistortionType::Bitcrush, DistortionType::SoftClip,
                            0.5f, 0.5f) == 2);
        // Bitcrush (1x) + HardClip (4x) at 0.5/0.5 = avg 2.5 -> 4
        CHECK(twoNodeResult(DistortionType::Bitcrush, DistortionType::HardClip,
                            0.5f, 0.5f) == 4);
    }

    SECTION("single dominant node (0.9/0.1 split)") {
        // SoftClip (2x) dominant + HardClip (4x) = 0.9*2 + 0.1*4 = 2.2 -> 4
        CHECK(twoNodeResult(DistortionType::SoftClip, DistortionType::HardClip,
                            0.9f, 0.1f) == 4);
        // HardClip (4x) dominant + SoftClip (2x) = 0.9*4 + 0.1*2 = 3.8 -> 4
        CHECK(twoNodeResult(DistortionType::HardClip, DistortionType::SoftClip,
                            0.9f, 0.1f) == 4);
        // HardClip (4x) dominant + Bitcrush (1x) = 0.9*4 + 0.1*1 = 3.7 -> 4
        CHECK(twoNodeResult(DistortionType::HardClip, DistortionType::Bitcrush,
                            0.9f, 0.1f) == 4);
        // Bitcrush (1x) dominant + SoftClip (2x) = 0.9*1 + 0.1*2 = 1.1 -> 2
        CHECK(twoNodeResult(DistortionType::Bitcrush, DistortionType::SoftClip,
                            0.9f, 0.1f) == 2);
    }

    SECTION("gradual transitions (0.7/0.3, 0.6/0.4)") {
        // SoftClip (2x) 0.7 + HardClip (4x) 0.3 = 0.7*2 + 0.3*4 = 2.6 -> 4
        CHECK(twoNodeResult(DistortionType::SoftClip, DistortionType::HardClip,
                            0.7f, 0.3f) == 4);
        // SoftClip (2x) 0.6 + HardClip (4x) 0.4 = 0.6*2 + 0.4*4 = 2.8 -> 4
        CHECK(twoNodeResult(DistortionType::SoftClip, DistortionType::HardClip,
                            0.6f, 0.4f) == 4);
        // Bitcrush (1x) 0.7 + SoftClip (2x) 0.3 = 0.7*1 + 0.3*2 = 1.3 -> 2
        CHECK(twoNodeResult(DistortionType::Bitcrush, DistortionType::SoftClip,
                            0.7f, 0.3f) == 2);
        // Bitcrush (1x) 0.6 + SoftClip (2x) 0.4 = 0.6*1 + 0.4*2 = 1.4 -> 2
        CHECK(twoNodeResult(DistortionType::Bitcrush, DistortionType::SoftClip,
                            0.6f, 0.4f) == 2);
    }

    SECTION("3-node morph with varied distributions") {
        std::array<MorphNode, kMaxMorphNodes> nodes{};
        nodes[0].type = DistortionType::Bitcrush;    // 1x
        nodes[1].type = DistortionType::SoftClip;    // 2x
        nodes[2].type = DistortionType::HardClip;    // 4x

        // Equal weights: (1+2+4)/3 = 2.33 -> 4
        std::array<float, kMaxMorphNodes> w1 = {1.0f/3.0f, 1.0f/3.0f, 1.0f/3.0f, 0.0f};
        CHECK(calculateMorphOversampleFactor(nodes, w1, 3, 8) == 4);

        // Heavy on 1x: 0.8*1 + 0.1*2 + 0.1*4 = 1.4 -> 2
        std::array<float, kMaxMorphNodes> w2 = {0.8f, 0.1f, 0.1f, 0.0f};
        CHECK(calculateMorphOversampleFactor(nodes, w2, 3, 8) == 2);

        // Heavy on 4x: 0.1*1 + 0.1*2 + 0.8*4 = 3.5 -> 4
        std::array<float, kMaxMorphNodes> w3 = {0.1f, 0.1f, 0.8f, 0.0f};
        CHECK(calculateMorphOversampleFactor(nodes, w3, 3, 8) == 4);
    }

    SECTION("4-node morph with varied distributions") {
        std::array<MorphNode, kMaxMorphNodes> nodes{};
        nodes[0].type = DistortionType::Bitcrush;     // 1x
        nodes[1].type = DistortionType::SoftClip;     // 2x
        nodes[2].type = DistortionType::Tube;          // 2x
        nodes[3].type = DistortionType::HardClip;     // 4x

        // Equal weights: (1+2+2+4)/4 = 2.25 -> 4
        std::array<float, kMaxMorphNodes> w1 = {0.25f, 0.25f, 0.25f, 0.25f};
        CHECK(calculateMorphOversampleFactor(nodes, w1, 4, 8) == 4);

        // Mostly 2x types: 0.1*1 + 0.4*2 + 0.4*2 + 0.1*4 = 2.1 -> 4
        std::array<float, kMaxMorphNodes> w2 = {0.1f, 0.4f, 0.4f, 0.1f};
        CHECK(calculateMorphOversampleFactor(nodes, w2, 4, 8) == 4);

        // Mostly 1x type: 0.7*1 + 0.1*2 + 0.1*2 + 0.1*4 = 1.5 -> 2
        std::array<float, kMaxMorphNodes> w3 = {0.7f, 0.1f, 0.1f, 0.1f};
        CHECK(calculateMorphOversampleFactor(nodes, w3, 4, 8) == 2);
    }

    SECTION("boundary cases (1.0/0.0/0.0/0.0)") {
        std::array<MorphNode, kMaxMorphNodes> nodes{};
        nodes[0].type = DistortionType::HardClip;  // 4x
        nodes[1].type = DistortionType::Bitcrush;  // 1x
        nodes[2].type = DistortionType::Bitcrush;  // 1x
        nodes[3].type = DistortionType::Bitcrush;  // 1x

        std::array<float, kMaxMorphNodes> w = {1.0f, 0.0f, 0.0f, 0.0f};
        CHECK(calculateMorphOversampleFactor(nodes, w, 4, 8) == 4);
    }

    SECTION("rounding thresholds - weighted averages near boundaries") {
        // avg = 1.0 exactly -> 1
        CHECK(twoNodeResult(DistortionType::Bitcrush, DistortionType::Bitcrush,
                            0.5f, 0.5f) == 1);
        // avg = 2.0 exactly -> 2
        CHECK(twoNodeResult(DistortionType::SoftClip, DistortionType::SoftClip,
                            0.5f, 0.5f) == 2);
        // avg just above 1.0 -> 2 (Bitcrush 1x weight=0.99, SoftClip 2x weight=0.01)
        // 0.99*1 + 0.01*2 = 1.01 -> 2
        CHECK(twoNodeResult(DistortionType::Bitcrush, DistortionType::SoftClip,
                            0.99f, 0.01f) == 2);
        // avg just above 2.0 -> 4 (SoftClip 2x weight=0.99, HardClip 4x weight=0.01)
        // 0.99*2 + 0.01*4 = 2.02 -> 4
        CHECK(twoNodeResult(DistortionType::SoftClip, DistortionType::HardClip,
                            0.99f, 0.01f) == 4);
    }

    SECTION("global limit clamping") {
        // HardClip (4x) + HardClip (4x), limit 2 -> clamped to 2
        CHECK(twoNodeResult(DistortionType::HardClip, DistortionType::HardClip,
                            0.5f, 0.5f, 2) == 2);
        // HardClip (4x) + HardClip (4x), limit 1 -> clamped to 1
        CHECK(twoNodeResult(DistortionType::HardClip, DistortionType::HardClip,
                            0.5f, 0.5f, 1) == 1);
        // SoftClip (2x) + HardClip (4x), limit 2 -> clamped to 2
        CHECK(twoNodeResult(DistortionType::SoftClip, DistortionType::HardClip,
                            0.5f, 0.5f, 2) == 2);
    }

    SECTION("edge case: activeNodeCount = 0 returns 1") {
        std::array<MorphNode, kMaxMorphNodes> nodes{};
        std::array<float, kMaxMorphNodes> weights = {0.0f, 0.0f, 0.0f, 0.0f};
        CHECK(calculateMorphOversampleFactor(nodes, weights, 0, 8) == 1);
    }

    SECTION("edge case: activeNodeCount clamped to kMaxMorphNodes") {
        std::array<MorphNode, kMaxMorphNodes> nodes{};
        nodes[0].type = DistortionType::HardClip;  // 4x
        nodes[1].type = DistortionType::HardClip;  // 4x
        nodes[2].type = DistortionType::HardClip;  // 4x
        nodes[3].type = DistortionType::HardClip;  // 4x
        std::array<float, kMaxMorphNodes> weights = {0.25f, 0.25f, 0.25f, 0.25f};
        // activeNodeCount = 10 but should be clamped to 4
        CHECK(calculateMorphOversampleFactor(nodes, weights, 10, 8) == 4);
    }
}
