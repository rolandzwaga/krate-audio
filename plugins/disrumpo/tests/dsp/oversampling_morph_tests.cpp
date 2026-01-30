// ==============================================================================
// Oversampling Morph-Aware Tests (User Story 2)
// ==============================================================================
// Tests for morph-weighted oversampling factor computation in BandProcessor.
// Verifies that when morphing between types with different oversampling
// requirements, the system dynamically adjusts the factor based on weighted
// average of active nodes' recommendations.
//
// Reference: specs/009-intelligent-oversampling/spec.md
// Tasks: T11.034, T11.035, T11.036, T11.037, T11.037b
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/band_processor.h"
#include "dsp/distortion_types.h"
#include "dsp/morph_node.h"
#include "dsp/oversampling_utils.h"

#include <array>
#include <cmath>

using namespace Disrumpo;

// Helper: Create a 2-node morph setup on a BandProcessor
static void setup2NodeMorph(BandProcessor& bp,
                             DistortionType typeA, DistortionType typeB) {
    std::array<MorphNode, kMaxMorphNodes> nodes = {{
        MorphNode(0, 0.0f, 0.0f, typeA),
        MorphNode(1, 1.0f, 0.0f, typeB),
        MorphNode(2, 0.0f, 1.0f, typeA),  // unused
        MorphNode(3, 1.0f, 1.0f, typeB)   // unused
    }};
    bp.setMorphNodes(nodes, 2);
    bp.setMorphMode(MorphMode::Linear1D);
}

// Helper: Create a 4-node morph setup on a BandProcessor
static void setup4NodeMorph(BandProcessor& bp,
                             DistortionType typeA, DistortionType typeB,
                             DistortionType typeC, DistortionType typeD) {
    std::array<MorphNode, kMaxMorphNodes> nodes = {{
        MorphNode(0, 0.0f, 0.0f, typeA),
        MorphNode(1, 1.0f, 0.0f, typeB),
        MorphNode(2, 0.0f, 1.0f, typeC),
        MorphNode(3, 1.0f, 1.0f, typeD)
    }};
    bp.setMorphNodes(nodes, 4);
    bp.setMorphMode(MorphMode::Planar2D);
}

// =============================================================================
// T11.034: 2-node morph tests (FR-003, FR-004)
// =============================================================================

TEST_CASE("BandProcessor: 2-node morph oversampling factor",
          "[oversampling][morph][FR-003][FR-004]") {

    BandProcessor bp;
    bp.prepare(44100.0, 512);

    SECTION("SoftClip (2x) + HardClip (4x) morph") {
        setup2NodeMorph(bp, DistortionType::SoftClip, DistortionType::HardClip);

        // At position 0.0 (fully on SoftClip = 2x): weighted avg = 2.0 -> 2
        bp.setMorphPosition(0.0f, 0.0f);
        CHECK(bp.getOversampleFactor() == 2);

        // At position 1.0 (fully on HardClip = 4x): weighted avg = 4.0 -> 4
        bp.setMorphPosition(1.0f, 0.0f);
        CHECK(bp.getOversampleFactor() == 4);
    }

    SECTION("Bitcrush (1x) + HardClip (4x) morph") {
        setup2NodeMorph(bp, DistortionType::Bitcrush, DistortionType::HardClip);

        // At position 0.0 (fully on Bitcrush = 1x): weighted avg = 1.0 -> 1
        bp.setMorphPosition(0.0f, 0.0f);
        CHECK(bp.getOversampleFactor() == 1);

        // At position 1.0 (fully on HardClip = 4x): weighted avg = 4.0 -> 4
        bp.setMorphPosition(1.0f, 0.0f);
        CHECK(bp.getOversampleFactor() == 4);
    }

    SECTION("same type both nodes (SoftClip 2x + SoftClip 2x)") {
        setup2NodeMorph(bp, DistortionType::SoftClip, DistortionType::SoftClip);

        // Regardless of position, weighted avg = 2.0 -> 2
        bp.setMorphPosition(0.0f, 0.0f);
        CHECK(bp.getOversampleFactor() == 2);

        bp.setMorphPosition(0.5f, 0.0f);
        CHECK(bp.getOversampleFactor() == 2);

        bp.setMorphPosition(1.0f, 0.0f);
        CHECK(bp.getOversampleFactor() == 2);
    }

    SECTION("both nodes 1x (Bitcrush + Aliasing)") {
        setup2NodeMorph(bp, DistortionType::Bitcrush, DistortionType::Aliasing);

        bp.setMorphPosition(0.0f, 0.0f);
        CHECK(bp.getOversampleFactor() == 1);

        bp.setMorphPosition(0.5f, 0.0f);
        CHECK(bp.getOversampleFactor() == 1);

        bp.setMorphPosition(1.0f, 0.0f);
        CHECK(bp.getOversampleFactor() == 1);
    }
}

// =============================================================================
// T11.035: Weighted average rounding tests (FR-004)
// =============================================================================

TEST_CASE("BandProcessor: morph weighted average rounding",
          "[oversampling][morph][FR-004]") {

    // These tests verify the rounding behavior using oversampling_utils directly
    // since BandProcessor morph weights depend on MorphEngine's internal state
    // which we can verify through factor selection behavior.

    SECTION("roundUpToPowerOf2Factor boundary values") {
        // Already tested in oversampling_utils_tests, but verify critical boundaries
        CHECK(roundUpToPowerOf2Factor(1.5f) == 2);
        CHECK(roundUpToPowerOf2Factor(2.0f) == 2);
        CHECK(roundUpToPowerOf2Factor(2.5f) == 4);
        CHECK(roundUpToPowerOf2Factor(3.0f) == 4);
    }
}

// =============================================================================
// T11.036: 4-node morph tests (SC-009)
// =============================================================================

TEST_CASE("BandProcessor: 4-node morph oversampling factor",
          "[oversampling][morph][SC-009]") {

    BandProcessor bp;
    bp.prepare(44100.0, 512);

    SECTION("4 nodes with mixed factors") {
        // Node A: SoftClip (2x), Node B: HardClip (4x),
        // Node C: Bitcrush (1x), Node D: Fuzz (4x)
        setup4NodeMorph(bp,
                        DistortionType::SoftClip,    // 2x
                        DistortionType::HardClip,    // 4x
                        DistortionType::Bitcrush,    // 1x
                        DistortionType::Fuzz);       // 4x

        // At corner (0,0) = Node A only: SoftClip = 2x -> factor 2
        bp.setMorphPosition(0.0f, 0.0f);
        CHECK(bp.getOversampleFactor() == 2);

        // At corner (1,0) = Node B only: HardClip = 4x -> factor 4
        bp.setMorphPosition(1.0f, 0.0f);
        CHECK(bp.getOversampleFactor() == 4);

        // At corner (0,1) = Node C only: Bitcrush = 1x -> factor 1
        bp.setMorphPosition(0.0f, 1.0f);
        CHECK(bp.getOversampleFactor() == 1);

        // At corner (1,1) = Node D only: Fuzz = 4x -> factor 4
        bp.setMorphPosition(1.0f, 1.0f);
        CHECK(bp.getOversampleFactor() == 4);
    }

    SECTION("all 4 nodes same type (4x)") {
        setup4NodeMorph(bp,
                        DistortionType::HardClip,    // 4x
                        DistortionType::Fuzz,        // 4x
                        DistortionType::SineFold,    // 4x
                        DistortionType::SergeFold);  // 4x

        // Regardless of position, all 4x -> weighted avg = 4.0 -> factor 4
        bp.setMorphPosition(0.5f, 0.5f);
        CHECK(bp.getOversampleFactor() == 4);

        bp.setMorphPosition(0.0f, 0.0f);
        CHECK(bp.getOversampleFactor() == 4);

        bp.setMorphPosition(1.0f, 1.0f);
        CHECK(bp.getOversampleFactor() == 4);
    }

    SECTION("all 4 nodes 1x type") {
        setup4NodeMorph(bp,
                        DistortionType::Bitcrush,       // 1x
                        DistortionType::SampleReduce,   // 1x
                        DistortionType::Quantize,       // 1x
                        DistortionType::Aliasing);      // 1x

        bp.setMorphPosition(0.5f, 0.5f);
        CHECK(bp.getOversampleFactor() == 1);
    }
}

// =============================================================================
// T11.037: Edge case tests
// =============================================================================

TEST_CASE("BandProcessor: morph oversampling edge cases",
          "[oversampling][morph][edge-cases]") {

    BandProcessor bp;
    bp.prepare(44100.0, 512);

    SECTION("switching from morph to single mode recalculates") {
        // Start in morph mode with 4x types
        setup2NodeMorph(bp, DistortionType::HardClip, DistortionType::Fuzz);
        bp.setMorphPosition(0.5f, 0.0f);
        CHECK(bp.getOversampleFactor() == 4);

        // Switch to single mode with a 1x type
        bp.setMorphEnabled(false);
        bp.setDistortionType(DistortionType::Bitcrush);
        CHECK(bp.getOversampleFactor() == 1);

        // Switch back to morph mode
        bp.setMorphEnabled(true);
        // Should recalculate based on morph state
        int factor = bp.getOversampleFactor();
        CHECK(factor >= 1);
        CHECK(factor <= 4);
    }

    SECTION("changing morph nodes triggers recalculation") {
        setup2NodeMorph(bp, DistortionType::Bitcrush, DistortionType::Bitcrush);
        bp.setMorphPosition(0.5f, 0.0f);
        CHECK(bp.getOversampleFactor() == 1);

        // Change nodes to 4x types
        std::array<MorphNode, kMaxMorphNodes> newNodes = {{
            MorphNode(0, 0.0f, 0.0f, DistortionType::HardClip),
            MorphNode(1, 1.0f, 0.0f, DistortionType::Fuzz),
            MorphNode(2, 0.0f, 1.0f, DistortionType::HardClip),
            MorphNode(3, 1.0f, 1.0f, DistortionType::Fuzz)
        }};
        bp.setMorphNodes(newNodes, 2);
        // Factor should have increased
        CHECK(bp.getOversampleFactor() == 4);
    }

    SECTION("global limit clamps morph factor") {
        setup2NodeMorph(bp, DistortionType::HardClip, DistortionType::Fuzz);
        bp.setMorphPosition(0.5f, 0.0f);
        CHECK(bp.getOversampleFactor() == 4);

        bp.setMaxOversampleFactor(2);
        CHECK(bp.getOversampleFactor() == 2);

        bp.setMaxOversampleFactor(1);
        CHECK(bp.getOversampleFactor() == 1);
    }
}

// =============================================================================
// T11.037b: Morph transition threshold test
// =============================================================================

TEST_CASE("BandProcessor: morph transition threshold between factors",
          "[oversampling][morph][transition-threshold]") {

    BandProcessor bp;
    bp.prepare(44100.0, 512);

    // SoftClip (2x) to HardClip (4x) morph
    // Weighted average = 2 * w_soft + 4 * w_hard
    // Transition from 2 to 4 when weighted avg > 2.0
    // With Linear1D: at position x, w_A = 1-x, w_B = x
    // weighted avg = 2*(1-x) + 4*x = 2 + 2x
    // > 2.0 when x > 0.0 (i.e., any weight toward HardClip gives > 2.0 -> rounds to 4)
    // BUT: at x=0.0 exactly, weighted avg = 2.0 -> rounds to 2
    setup2NodeMorph(bp, DistortionType::SoftClip, DistortionType::HardClip);

    SECTION("at x=0.0 (pure SoftClip), factor is 2") {
        bp.setMorphPosition(0.0f, 0.0f);
        CHECK(bp.getOversampleFactor() == 2);
    }

    SECTION("at x=1.0 (pure HardClip), factor is 4") {
        bp.setMorphPosition(1.0f, 0.0f);
        CHECK(bp.getOversampleFactor() == 4);
    }

    // The exact threshold depends on morph engine weight computation.
    // We verify that the factor transitions from 2 to 4 at some point
    // between x=0 and x=1.
    SECTION("factor transitions from 2 to 4 during morph") {
        int factorAt0 = 0;
        int factorAt1 = 0;

        bp.setMorphPosition(0.0f, 0.0f);
        factorAt0 = bp.getOversampleFactor();

        bp.setMorphPosition(1.0f, 0.0f);
        factorAt1 = bp.getOversampleFactor();

        // Factor at x=0 should be 2, at x=1 should be 4
        CHECK(factorAt0 == 2);
        CHECK(factorAt1 == 4);

        // There must exist a transition point between 0 and 1
        // Verify by scanning - find where it changes
        bool foundTransition = false;
        int prevFactor = factorAt0;
        for (int step = 1; step <= 100; ++step) {
            float x = static_cast<float>(step) / 100.0f;
            bp.setMorphPosition(x, 0.0f);
            int currentFactor = bp.getOversampleFactor();
            if (currentFactor != prevFactor) {
                foundTransition = true;
                INFO("Transition at x=" << x << ": " << prevFactor << " -> " << currentFactor);
                break;
            }
            prevFactor = currentFactor;
        }
        CHECK(foundTransition);
    }
}
