// ==============================================================================
// Parameter ID Encoding Tests
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Tests for hex bit-field parameter ID encoding per dsp-details.md
//
// Bit Layout (16-bit ParamID):
// +--------+--------+--------+
// | 15..12 | 11..8  |  7..0  |
// |  node  |  band  | param  |
// +--------+--------+--------+
//
// Special Bands:
// - 0xF = Global parameters (node nibble = 0x0)
// - 0xE = Sweep parameters (node nibble = 0x0)
// - 0x0-0x7 = Per-band parameters (node nibble = 0xF for band-level, 0-3 for node-level)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "plugin_ids.h"

#include <set>

using namespace Disrumpo;

// ==============================================================================
// Test: Global Parameter IDs (0x0Fxx range)
// ==============================================================================
TEST_CASE("makeGlobalParamId returns 0x0F00 range values", "[parameter][encoding]") {
    SECTION("Global parameters are in 0x0F00 range") {
        REQUIRE(makeGlobalParamId(GlobalParamType::kGlobalInputGain) == 0x0F00);
        REQUIRE(makeGlobalParamId(GlobalParamType::kGlobalOutputGain) == 0x0F01);
        REQUIRE(makeGlobalParamId(GlobalParamType::kGlobalMix) == 0x0F02);
        REQUIRE(makeGlobalParamId(GlobalParamType::kGlobalBandCount) == 0x0F03);
        REQUIRE(makeGlobalParamId(GlobalParamType::kGlobalOversample) == 0x0F04);
    }

    SECTION("Global parameter decimal values match expected") {
        // 0x0F00 = 3840, 0x0F01 = 3841, etc.
        REQUIRE(makeGlobalParamId(GlobalParamType::kGlobalInputGain) == 3840);
        REQUIRE(makeGlobalParamId(GlobalParamType::kGlobalOutputGain) == 3841);
        REQUIRE(makeGlobalParamId(GlobalParamType::kGlobalMix) == 3842);
        REQUIRE(makeGlobalParamId(GlobalParamType::kGlobalBandCount) == 3843);
        REQUIRE(makeGlobalParamId(GlobalParamType::kGlobalOversample) == 3844);
    }
}

// ==============================================================================
// Test: Sweep Parameter IDs (0x0Exx range)
// ==============================================================================
TEST_CASE("makeSweepParamId returns 0x0E00 range values", "[parameter][encoding]") {
    SECTION("Sweep parameters are in 0x0E00 range") {
        REQUIRE(makeSweepParamId(SweepParamType::kSweepEnable) == 0x0E00);
        REQUIRE(makeSweepParamId(SweepParamType::kSweepFrequency) == 0x0E01);
        REQUIRE(makeSweepParamId(SweepParamType::kSweepWidth) == 0x0E02);
        REQUIRE(makeSweepParamId(SweepParamType::kSweepIntensity) == 0x0E03);
        REQUIRE(makeSweepParamId(SweepParamType::kSweepMorphLink) == 0x0E04);
        REQUIRE(makeSweepParamId(SweepParamType::kSweepFalloff) == 0x0E05);
    }

    SECTION("Sweep parameter decimal values match expected") {
        // 0x0E00 = 3584, 0x0E01 = 3585, etc.
        REQUIRE(makeSweepParamId(SweepParamType::kSweepEnable) == 3584);
        REQUIRE(makeSweepParamId(SweepParamType::kSweepFrequency) == 3585);
        REQUIRE(makeSweepParamId(SweepParamType::kSweepWidth) == 3586);
        REQUIRE(makeSweepParamId(SweepParamType::kSweepIntensity) == 3587);
        REQUIRE(makeSweepParamId(SweepParamType::kSweepMorphLink) == 3588);
        REQUIRE(makeSweepParamId(SweepParamType::kSweepFalloff) == 3589);
    }
}

// ==============================================================================
// Test: Band Parameter IDs (0xFbpp encoding)
// ==============================================================================
TEST_CASE("makeBandParamId encodes band and param correctly", "[parameter][encoding]") {
    SECTION("Band 0 parameters") {
        // 0xF000 = (0xF << 12) | (0 << 8) | 0 = 61440
        REQUIRE(makeBandParamId(0, BandParamType::kBandGain) == 0xF000);
        REQUIRE(makeBandParamId(0, BandParamType::kBandPan) == 0xF001);
        REQUIRE(makeBandParamId(0, BandParamType::kBandSolo) == 0xF002);
        REQUIRE(makeBandParamId(0, BandParamType::kBandBypass) == 0xF003);
        REQUIRE(makeBandParamId(0, BandParamType::kBandMute) == 0xF004);
        // MorphX = 0x08, MorphY = 0x09, MorphMode = 0x0A per dsp-details.md
        REQUIRE(makeBandParamId(0, BandParamType::kBandMorphX) == 0xF008);
        REQUIRE(makeBandParamId(0, BandParamType::kBandMorphY) == 0xF009);
        REQUIRE(makeBandParamId(0, BandParamType::kBandMorphMode) == 0xF00A);
    }

    SECTION("Band 3 Gain returns 0xF300") {
        // 0xF300 = (0xF << 12) | (3 << 8) | 0 = 62208
        REQUIRE(makeBandParamId(3, BandParamType::kBandGain) == 0xF300);
        REQUIRE(makeBandParamId(3, BandParamType::kBandGain) == 62208);
    }

    SECTION("Band 7 parameters") {
        // 0xF700 = (0xF << 12) | (7 << 8) | 0 = 63232
        REQUIRE(makeBandParamId(7, BandParamType::kBandGain) == 0xF700);
        REQUIRE(makeBandParamId(7, BandParamType::kBandMute) == 0xF704);
    }

    SECTION("Decimal values for Band 0 parameters") {
        REQUIRE(makeBandParamId(0, BandParamType::kBandGain) == 61440);
        REQUIRE(makeBandParamId(0, BandParamType::kBandPan) == 61441);
        REQUIRE(makeBandParamId(0, BandParamType::kBandSolo) == 61442);
        REQUIRE(makeBandParamId(0, BandParamType::kBandBypass) == 61443);
        REQUIRE(makeBandParamId(0, BandParamType::kBandMute) == 61444);
        REQUIRE(makeBandParamId(0, BandParamType::kBandMorphX) == 61448);
        REQUIRE(makeBandParamId(0, BandParamType::kBandMorphY) == 61449);
        REQUIRE(makeBandParamId(0, BandParamType::kBandMorphMode) == 61450);
    }
}

// ==============================================================================
// Test: Node Parameter IDs (0xNbpp encoding)
// ==============================================================================
TEST_CASE("makeNodeParamId encodes band, node, and param correctly", "[parameter][encoding]") {
    SECTION("Band 0, Node 0 parameters (0x00pp)") {
        // makeNodeParamId(band=0, node=0, param) = (0 << 12) | (0 << 8) | param
        REQUIRE(makeNodeParamId(0, 0, NodeParamType::kNodeType) == 0x0000);
        REQUIRE(makeNodeParamId(0, 0, NodeParamType::kNodeDrive) == 0x0001);
        REQUIRE(makeNodeParamId(0, 0, NodeParamType::kNodeMix) == 0x0002);
        REQUIRE(makeNodeParamId(0, 0, NodeParamType::kNodeTone) == 0x0003);
        REQUIRE(makeNodeParamId(0, 0, NodeParamType::kNodeBias) == 0x0004);
        REQUIRE(makeNodeParamId(0, 0, NodeParamType::kNodeFolds) == 0x0005);
        REQUIRE(makeNodeParamId(0, 0, NodeParamType::kNodeBitDepth) == 0x0006);
    }

    SECTION("Band 1, Node 2, Drive returns 0x2101") {
        // makeNodeParamId(band=1, node=2, kNodeDrive) = (2 << 12) | (1 << 8) | 1 = 0x2101 = 8449
        REQUIRE(makeNodeParamId(1, 2, NodeParamType::kNodeDrive) == 0x2101);
        REQUIRE(makeNodeParamId(1, 2, NodeParamType::kNodeDrive) == 8449);
    }

    SECTION("Band 7, Node 3 parameters") {
        // (3 << 12) | (7 << 8) | param = 0x37pp
        REQUIRE(makeNodeParamId(7, 3, NodeParamType::kNodeType) == 0x3700);
        REQUIRE(makeNodeParamId(7, 3, NodeParamType::kNodeDrive) == 0x3701);
    }

    SECTION("Decimal values for common node parameters") {
        // Band 0, Node 0: (0 << 12) | (0 << 8) | param
        REQUIRE(makeNodeParamId(0, 0, NodeParamType::kNodeType) == 0);
        REQUIRE(makeNodeParamId(0, 0, NodeParamType::kNodeDrive) == 1);
        REQUIRE(makeNodeParamId(0, 0, NodeParamType::kNodeMix) == 2);
        REQUIRE(makeNodeParamId(0, 0, NodeParamType::kNodeTone) == 3);
        REQUIRE(makeNodeParamId(0, 0, NodeParamType::kNodeBias) == 4);
        REQUIRE(makeNodeParamId(0, 0, NodeParamType::kNodeFolds) == 5);
        REQUIRE(makeNodeParamId(0, 0, NodeParamType::kNodeBitDepth) == 6);
    }
}

// ==============================================================================
// Test: Extraction Functions
// ==============================================================================
TEST_CASE("Extraction functions recover original values", "[parameter][encoding]") {
    SECTION("Extract band from band parameter ID") {
        for (uint8_t band = 0; band < 8; ++band) {
            auto paramId = makeBandParamId(band, BandParamType::kBandGain);
            REQUIRE(extractBandIndex(paramId) == band);
        }
    }

    SECTION("Extract band from node parameter ID") {
        for (uint8_t band = 0; band < 8; ++band) {
            for (uint8_t node = 0; node < 4; ++node) {
                auto paramId = makeNodeParamId(band, node, NodeParamType::kNodeDrive);
                REQUIRE(extractBandFromNodeParam(paramId) == band);
            }
        }
    }

    SECTION("Extract node from node parameter ID") {
        for (uint8_t band = 0; band < 8; ++band) {
            for (uint8_t node = 0; node < 4; ++node) {
                auto paramId = makeNodeParamId(band, node, NodeParamType::kNodeType);
                REQUIRE(extractNode(paramId) == node);
            }
        }
    }

    SECTION("Extract param type from band parameter ID") {
        auto gainId = makeBandParamId(3, BandParamType::kBandGain);
        auto muteId = makeBandParamId(5, BandParamType::kBandMute);
        auto morphXId = makeBandParamId(2, BandParamType::kBandMorphX);

        REQUIRE(extractBandParamType(gainId) == BandParamType::kBandGain);
        REQUIRE(extractBandParamType(muteId) == BandParamType::kBandMute);
        REQUIRE(extractBandParamType(morphXId) == BandParamType::kBandMorphX);
    }
}

// ==============================================================================
// Test: Type Detection Functions
// ==============================================================================
TEST_CASE("Type detection functions work correctly", "[parameter][encoding]") {
    SECTION("isBandParamId identifies band-level parameters") {
        // Band parameters have node nibble = 0xF
        REQUIRE(isBandParamId(makeBandParamId(0, BandParamType::kBandGain)) == true);
        REQUIRE(isBandParamId(makeBandParamId(7, BandParamType::kBandMute)) == true);
        REQUIRE(isBandParamId(makeBandParamId(3, BandParamType::kBandMorphX)) == true);
    }

    SECTION("isBandParamId returns false for node parameters") {
        // Node parameters have node nibble = 0-3
        REQUIRE(isBandParamId(makeNodeParamId(0, 0, NodeParamType::kNodeType)) == false);
        REQUIRE(isBandParamId(makeNodeParamId(1, 2, NodeParamType::kNodeDrive)) == false);
    }

    SECTION("isNodeParamId identifies node-level parameters") {
        REQUIRE(isNodeParamId(makeNodeParamId(0, 0, NodeParamType::kNodeType)) == true);
        REQUIRE(isNodeParamId(makeNodeParamId(7, 3, NodeParamType::kNodeBitDepth)) == true);
    }

    SECTION("isNodeParamId returns false for band parameters") {
        REQUIRE(isNodeParamId(makeBandParamId(0, BandParamType::kBandGain)) == false);
        REQUIRE(isNodeParamId(makeBandParamId(5, BandParamType::kBandSolo)) == false);
    }

    SECTION("isGlobalParamId identifies global parameters") {
        REQUIRE(isGlobalParamId(makeGlobalParamId(GlobalParamType::kGlobalInputGain)) == true);
        REQUIRE(isGlobalParamId(makeGlobalParamId(GlobalParamType::kGlobalBandCount)) == true);
        REQUIRE(isGlobalParamId(kInputGainId) == true);
    }

    SECTION("isGlobalParamId returns false for non-global parameters") {
        REQUIRE(isGlobalParamId(makeBandParamId(0, BandParamType::kBandGain)) == false);
        REQUIRE(isGlobalParamId(makeNodeParamId(0, 0, NodeParamType::kNodeType)) == false);
        REQUIRE(isGlobalParamId(makeSweepParamId(SweepParamType::kSweepEnable)) == false);
    }

    SECTION("isSweepParamId identifies sweep parameters") {
        REQUIRE(isSweepParamId(makeSweepParamId(SweepParamType::kSweepEnable)) == true);
        REQUIRE(isSweepParamId(makeSweepParamId(SweepParamType::kSweepFrequency)) == true);
    }

    SECTION("isSweepParamId returns false for non-sweep parameters") {
        REQUIRE(isSweepParamId(makeGlobalParamId(GlobalParamType::kGlobalInputGain)) == false);
        REQUIRE(isSweepParamId(makeBandParamId(0, BandParamType::kBandGain)) == false);
    }
}

// ==============================================================================
// Test: No Collisions - All Parameter Combinations Produce Unique IDs
// ==============================================================================
TEST_CASE("No parameter ID collisions exist", "[parameter][encoding]") {
    std::set<Steinberg::Vst::ParamID> allIds;

    SECTION("All parameter IDs are unique") {
        // Add global parameters
        for (uint8_t p = 0; p <= static_cast<uint8_t>(GlobalParamType::kGlobalOversample); ++p) {
            auto id = makeGlobalParamId(static_cast<GlobalParamType>(p));
            REQUIRE(allIds.insert(id).second); // Insert returns false if duplicate
        }

        // Add sweep parameters
        for (uint8_t p = 0; p <= static_cast<uint8_t>(SweepParamType::kSweepFalloff); ++p) {
            auto id = makeSweepParamId(static_cast<SweepParamType>(p));
            REQUIRE(allIds.insert(id).second);
        }

        // Add band parameters for all 8 bands
        for (uint8_t band = 0; band < 8; ++band) {
            // Basic band params (0x00-0x04)
            for (uint8_t p = 0; p <= 4; ++p) {
                auto id = makeBandParamId(band, static_cast<BandParamType>(p));
                REQUIRE(allIds.insert(id).second);
            }
            // Morph params (0x08-0x0A)
            auto morphXId = makeBandParamId(band, BandParamType::kBandMorphX);
            auto morphYId = makeBandParamId(band, BandParamType::kBandMorphY);
            auto morphModeId = makeBandParamId(band, BandParamType::kBandMorphMode);
            REQUIRE(allIds.insert(morphXId).second);
            REQUIRE(allIds.insert(morphYId).second);
            REQUIRE(allIds.insert(morphModeId).second);
        }

        // Add node parameters for all 8 bands x 4 nodes
        for (uint8_t band = 0; band < 8; ++band) {
            for (uint8_t node = 0; node < 4; ++node) {
                for (uint8_t p = 0; p <= static_cast<uint8_t>(NodeParamType::kNodeBitDepth); ++p) {
                    auto id = makeNodeParamId(band, node, static_cast<NodeParamType>(p));
                    REQUIRE(allIds.insert(id).second);
                }
            }
        }

        // Add crossover parameters
        for (uint8_t i = 0; i < 7; ++i) {
            auto id = makeCrossoverParamId(i);
            REQUIRE(allIds.insert(id).second);
        }

        // Verify we have a substantial number of unique IDs
        // Global: 5, Sweep: 6, Band: 8 * 8 = 64, Node: 8 * 4 * 7 = 224, Crossover: 7
        // Total: 5 + 6 + 64 + 224 + 7 = 306
        REQUIRE(allIds.size() >= 300);
    }
}
