// ==============================================================================
// Oversampling Global Limit Tests (User Story 3)
// ==============================================================================
// Tests for the global oversampling limit parameter that caps all bands to a
// maximum factor regardless of their computed recommendation.
//
// Reference: specs/009-intelligent-oversampling/spec.md
// Tasks: T11.045, T11.046, T11.047, T11.048, T11.049, T11.049b
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/band_processor.h"
#include "dsp/distortion_types.h"
#include "dsp/morph_node.h"

#include <array>

using namespace Disrumpo;

// =============================================================================
// T11.045/T11.046: Global limit 1x forces all bands to 1x
// =============================================================================

TEST_CASE("BandProcessor: global limit 1x forces all types to 1x",
          "[oversampling][limit][FR-005][FR-006][FR-007][FR-008]") {

    BandProcessor bp;
    bp.prepare(44100.0, 512);
    bp.setMaxOversampleFactor(1);

    SECTION("4x types clamped to 1x") {
        bp.setDistortionType(DistortionType::HardClip);
        CHECK(bp.getOversampleFactor() == 1);

        bp.setDistortionType(DistortionType::Fuzz);
        CHECK(bp.getOversampleFactor() == 1);

        bp.setDistortionType(DistortionType::SineFold);
        CHECK(bp.getOversampleFactor() == 1);
    }

    SECTION("2x types clamped to 1x") {
        bp.setDistortionType(DistortionType::SoftClip);
        CHECK(bp.getOversampleFactor() == 1);

        bp.setDistortionType(DistortionType::Tube);
        CHECK(bp.getOversampleFactor() == 1);

        bp.setDistortionType(DistortionType::Tape);
        CHECK(bp.getOversampleFactor() == 1);
    }

    SECTION("1x types unaffected") {
        bp.setDistortionType(DistortionType::Bitcrush);
        CHECK(bp.getOversampleFactor() == 1);

        bp.setDistortionType(DistortionType::Aliasing);
        CHECK(bp.getOversampleFactor() == 1);
    }
}

// =============================================================================
// T11.047: Global limit 2x clamps 4x types to 2x
// =============================================================================

TEST_CASE("BandProcessor: global limit 2x clamps 4x types to 2x",
          "[oversampling][limit][FR-007]") {

    BandProcessor bp;
    bp.prepare(44100.0, 512);
    bp.setMaxOversampleFactor(2);

    SECTION("4x types clamped to 2x") {
        bp.setDistortionType(DistortionType::HardClip);
        CHECK(bp.getOversampleFactor() == 2);

        bp.setDistortionType(DistortionType::Fuzz);
        CHECK(bp.getOversampleFactor() == 2);

        bp.setDistortionType(DistortionType::SergeFold);
        CHECK(bp.getOversampleFactor() == 2);
    }

    SECTION("2x types unaffected") {
        bp.setDistortionType(DistortionType::SoftClip);
        CHECK(bp.getOversampleFactor() == 2);

        bp.setDistortionType(DistortionType::Tube);
        CHECK(bp.getOversampleFactor() == 2);
    }

    SECTION("1x types unaffected") {
        bp.setDistortionType(DistortionType::Bitcrush);
        CHECK(bp.getOversampleFactor() == 1);
    }
}

// =============================================================================
// T11.048: Global limit 4x (default) does not affect types <= 4x
// =============================================================================

TEST_CASE("BandProcessor: global limit 4x (default) allows full range",
          "[oversampling][limit][FR-008]") {

    BandProcessor bp;
    bp.prepare(44100.0, 512);
    // Default limit is 8 (kMaxOversampleFactor), but setting to 4 explicitly
    bp.setMaxOversampleFactor(4);

    bp.setDistortionType(DistortionType::HardClip);
    CHECK(bp.getOversampleFactor() == 4);

    bp.setDistortionType(DistortionType::SoftClip);
    CHECK(bp.getOversampleFactor() == 2);

    bp.setDistortionType(DistortionType::Bitcrush);
    CHECK(bp.getOversampleFactor() == 1);
}

// =============================================================================
// T11.049: Limit changes during processing re-clamp all bands
// =============================================================================

TEST_CASE("BandProcessor: changing limit during processing re-clamps factor",
          "[oversampling][limit][FR-015][FR-016]") {

    BandProcessor bp;
    bp.prepare(44100.0, 512);

    SECTION("lowering limit re-clamps active factor") {
        bp.setDistortionType(DistortionType::HardClip);
        CHECK(bp.getOversampleFactor() == 4);

        // Lower limit to 2x
        bp.setMaxOversampleFactor(2);
        CHECK(bp.getOversampleFactor() == 2);

        // Lower limit to 1x
        bp.setMaxOversampleFactor(1);
        CHECK(bp.getOversampleFactor() == 1);
    }

    SECTION("raising limit restores recommended factor") {
        bp.setMaxOversampleFactor(1);
        bp.setDistortionType(DistortionType::HardClip);
        CHECK(bp.getOversampleFactor() == 1);

        // Raise limit to 4x
        bp.setMaxOversampleFactor(4);
        CHECK(bp.getOversampleFactor() == 4);
    }

    SECTION("limit change respects current type") {
        bp.setDistortionType(DistortionType::SoftClip);
        CHECK(bp.getOversampleFactor() == 2);

        // Setting limit to 4x should not increase SoftClip beyond 2x
        bp.setMaxOversampleFactor(4);
        CHECK(bp.getOversampleFactor() == 2);

        // Setting limit to 1x should clamp
        bp.setMaxOversampleFactor(1);
        CHECK(bp.getOversampleFactor() == 1);
    }

    SECTION("limit change with morph active") {
        // Setup morph between 2x and 4x types
        std::array<MorphNode, kMaxMorphNodes> nodes = {{
            MorphNode(0, 0.0f, 0.0f, DistortionType::SoftClip),    // 2x
            MorphNode(1, 1.0f, 0.0f, DistortionType::HardClip),    // 4x
            MorphNode(2, 0.0f, 1.0f, DistortionType::SoftClip),
            MorphNode(3, 1.0f, 1.0f, DistortionType::HardClip)
        }};
        bp.setMorphNodes(nodes, 2);
        bp.setMorphMode(MorphMode::Linear1D);

        // Position toward HardClip (4x)
        bp.setMorphPosition(1.0f, 0.0f);
        CHECK(bp.getOversampleFactor() == 4);

        // Clamp to 2x
        bp.setMaxOversampleFactor(2);
        CHECK(bp.getOversampleFactor() == 2);

        // Restore
        bp.setMaxOversampleFactor(8);
        CHECK(bp.getOversampleFactor() == 4);
    }
}

// =============================================================================
// T11.049b: Rapid limit automation test
// =============================================================================

TEST_CASE("BandProcessor: rapid limit parameter changes",
          "[oversampling][limit][FR-015]") {

    BandProcessor bp;
    bp.prepare(44100.0, 512);
    bp.setDistortionType(DistortionType::HardClip);

    SECTION("rapid toggling between limits does not crash") {
        std::array<float, 64> left{};
        std::array<float, 64> right{};
        for (size_t i = 0; i < 64; ++i) {
            left[i] = 0.5f;
            right[i] = 0.5f;
        }

        // Simulate rapid automation: 4x -> 2x -> 4x -> 1x -> 4x
        for (int cycle = 0; cycle < 10; ++cycle) {
            bp.setMaxOversampleFactor(4);
            bp.processBlock(left.data(), right.data(), 64);

            bp.setMaxOversampleFactor(2);
            bp.processBlock(left.data(), right.data(), 64);

            bp.setMaxOversampleFactor(1);
            bp.processBlock(left.data(), right.data(), 64);
        }

        // Should not crash; verify factor is correct after settling
        bp.setMaxOversampleFactor(4);
        // Process enough blocks for any crossfade to complete
        for (int i = 0; i < 20; ++i) {
            bp.processBlock(left.data(), right.data(), 64);
        }
        CHECK(bp.getOversampleFactor() == 4);
    }
}
