// ==============================================================================
// Oversampling Integration Tests (User Story 4)
// ==============================================================================
// Multi-band integration tests verifying independent factor selection across
// bands and correct behavior under various combined conditions.
//
// Reference: specs/009-intelligent-oversampling/spec.md
// Tasks: T11.071, T11.072, T11.073, T11.072b
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/band_processor.h"
#include "dsp/distortion_types.h"
#include "dsp/morph_node.h"

#include <array>
#include <cmath>
#include <memory>
#include <vector>

using namespace Disrumpo;

// =============================================================================
// T11.072: 4 bands with different types and morph states
// =============================================================================

TEST_CASE("Integration: 4 bands with independent oversampling factors",
          "[oversampling][integration]") {

    constexpr int kNumBands = 4;

    // Use heap allocation to avoid stack overflow (each BandProcessor is large)
    std::vector<std::unique_ptr<BandProcessor>> bands;
    bands.reserve(kNumBands);
    for (int i = 0; i < kNumBands; ++i) {
        bands.push_back(std::make_unique<BandProcessor>());
        bands.back()->prepare(44100.0, 512);
    }

    // Assign different types to each band (covering all 3 oversample factors)
    bands[0]->setDistortionType(DistortionType::HardClip);     // 4x
    bands[1]->setDistortionType(DistortionType::SoftClip);     // 2x
    bands[2]->setDistortionType(DistortionType::Bitcrush);     // 1x
    bands[3]->setDistortionType(DistortionType::Fuzz);         // 4x

    // Verify each band has independent factor
    CHECK(bands[0]->getOversampleFactor() == 4);
    CHECK(bands[1]->getOversampleFactor() == 2);
    CHECK(bands[2]->getOversampleFactor() == 1);
    CHECK(bands[3]->getOversampleFactor() == 4);

    // Process all bands simultaneously - should not interfere
    constexpr size_t kBlockSize = 256;
    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};

    for (auto& bp : bands) {
        // Fill with signal
        for (size_t i = 0; i < kBlockSize; ++i) {
            left[i] = 0.3f * std::sin(2.0f * 3.14159f * 440.0f *
                      static_cast<float>(i) / 44100.0f);
            right[i] = left[i];
        }
        bp->processBlock(left.data(), right.data(), kBlockSize);
    }

    // Factors should be unchanged after processing
    CHECK(bands[0]->getOversampleFactor() == 4);
    CHECK(bands[1]->getOversampleFactor() == 2);
    CHECK(bands[2]->getOversampleFactor() == 1);
}

// =============================================================================
// T11.073: Rapid type automation across multiple bands
// =============================================================================

TEST_CASE("Integration: rapid type automation across multiple bands",
          "[oversampling][integration]") {

    constexpr int kNumBands = 4;
    constexpr size_t kBlockSize = 64;

    std::vector<std::unique_ptr<BandProcessor>> bands;
    bands.reserve(kNumBands);
    for (int i = 0; i < kNumBands; ++i) {
        bands.push_back(std::make_unique<BandProcessor>());
        bands.back()->prepare(44100.0, kBlockSize);
        DistortionCommonParams params;
        params.drive = 0.5f;
        params.mix = 1.0f;
        params.toneHz = 4000.0f;
        bands.back()->setDistortionCommonParams(params);
    }

    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};

    // Rapid type switching with processing
    DistortionType types[] = {
        DistortionType::HardClip, DistortionType::SoftClip,
        DistortionType::Bitcrush, DistortionType::Fuzz,
        DistortionType::Tube, DistortionType::Aliasing
    };

    for (int cycle = 0; cycle < 20; ++cycle) {
        for (int b = 0; b < kNumBands; ++b) {
            // Cycle through types
            bands[b]->setDistortionType(types[(cycle + b) % 6]);

            // Process a block
            for (size_t i = 0; i < kBlockSize; ++i) {
                left[i] = 0.3f;
                right[i] = 0.3f;
            }
            bands[b]->processBlock(left.data(), right.data(), kBlockSize);
        }
    }

    // Verify all bands are in a valid state (no crashes, valid factors)
    for (int b = 0; b < kNumBands; ++b) {
        int factor = bands[b]->getOversampleFactor();
        CHECK(factor >= 1);
        CHECK(factor <= 8);
    }
}

// =============================================================================
// T11.072b: FR-017 trigger verification
// =============================================================================

TEST_CASE("Integration: FR-017 triggers from all 4 conditions",
          "[oversampling][integration][FR-017]") {

    BandProcessor bp;
    bp.prepare(44100.0, 512);

    SECTION("trigger 1: type change") {
        bp.setDistortionType(DistortionType::Bitcrush);
        CHECK(bp.getOversampleFactor() == 1);

        bp.setDistortionType(DistortionType::HardClip);
        CHECK(bp.getOversampleFactor() == 4);
    }

    SECTION("trigger 2: morph position change") {
        std::array<MorphNode, kMaxMorphNodes> nodes = {{
            MorphNode(0, 0.0f, 0.0f, DistortionType::Bitcrush),
            MorphNode(1, 1.0f, 0.0f, DistortionType::HardClip),
            MorphNode(2, 0.0f, 1.0f, DistortionType::Bitcrush),
            MorphNode(3, 1.0f, 1.0f, DistortionType::HardClip)
        }};
        bp.setMorphNodes(nodes, 2);
        bp.setMorphMode(MorphMode::Linear1D);

        bp.setMorphPosition(0.0f, 0.0f);
        int factorAtA = bp.getOversampleFactor();

        bp.setMorphPosition(1.0f, 0.0f);
        int factorAtB = bp.getOversampleFactor();

        // Factor should change from 1x to 4x
        CHECK(factorAtA == 1);
        CHECK(factorAtB == 4);
    }

    SECTION("trigger 3: morph node change") {
        std::array<MorphNode, kMaxMorphNodes> nodesLow = {{
            MorphNode(0, 0.0f, 0.0f, DistortionType::Bitcrush),
            MorphNode(1, 1.0f, 0.0f, DistortionType::Aliasing),
            MorphNode(2, 0.0f, 1.0f, DistortionType::Bitcrush),
            MorphNode(3, 1.0f, 1.0f, DistortionType::Aliasing)
        }};
        bp.setMorphNodes(nodesLow, 2);
        bp.setMorphMode(MorphMode::Linear1D);
        bp.setMorphPosition(0.5f, 0.0f);
        CHECK(bp.getOversampleFactor() == 1);

        // Change nodes to high-OS types
        std::array<MorphNode, kMaxMorphNodes> nodesHigh = {{
            MorphNode(0, 0.0f, 0.0f, DistortionType::HardClip),
            MorphNode(1, 1.0f, 0.0f, DistortionType::Fuzz),
            MorphNode(2, 0.0f, 1.0f, DistortionType::HardClip),
            MorphNode(3, 1.0f, 1.0f, DistortionType::Fuzz)
        }};
        bp.setMorphNodes(nodesHigh, 2);
        CHECK(bp.getOversampleFactor() == 4);
    }

    SECTION("trigger 4: global limit change") {
        bp.setDistortionType(DistortionType::HardClip);
        CHECK(bp.getOversampleFactor() == 4);

        bp.setMaxOversampleFactor(2);
        CHECK(bp.getOversampleFactor() == 2);

        bp.setMaxOversampleFactor(8);
        CHECK(bp.getOversampleFactor() == 4);
    }
}
