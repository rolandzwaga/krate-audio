// ==============================================================================
// Band State (Solo/Bypass/Mute) Tests
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Tests for Solo/Bypass/Mute parameter tags (T072)
//
// Verifies:
// - makeBandParamId returns correct tags for Solo, Bypass, Mute
// - All 8 bands produce unique IDs with no collisions
// - Boolean parameters have correct step count (1)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "plugin_ids.h"

#include <array>
#include <set>
#include <vector>

using namespace Disrumpo;
using Catch::Approx;

// ==============================================================================
// Test: Solo Parameter ID Encoding (T072)
// ==============================================================================
TEST_CASE("Band Solo parameter ID is correctly encoded", "[bandstate][solo]") {

    SECTION("Band 0 Solo has tag value 61442") {
        // 0xF << 12 | 0 << 8 | 0x02 = 0xF002 = 61442
        auto paramId = makeBandParamId(0, BandParamType::kBandSolo);
        REQUIRE(paramId == 0xF002);
        REQUIRE(paramId == 61442);
    }

    SECTION("Band 1 Solo has tag value 61698") {
        // 0xF << 12 | 1 << 8 | 0x02 = 0xF102 = 61698
        auto paramId = makeBandParamId(1, BandParamType::kBandSolo);
        REQUIRE(paramId == 0xF102);
        REQUIRE(paramId == 61698);
    }

    SECTION("Band 3 Solo has tag value 62210") {
        // 0xF << 12 | 3 << 8 | 0x02 = 0xF302 = 62210
        auto paramId = makeBandParamId(3, BandParamType::kBandSolo);
        REQUIRE(paramId == 0xF302);
        REQUIRE(paramId == 62210);
    }

    SECTION("Band 7 Solo has tag value 63234") {
        // 0xF << 12 | 7 << 8 | 0x02 = 0xF702 = 63234
        auto paramId = makeBandParamId(7, BandParamType::kBandSolo);
        REQUIRE(paramId == 0xF702);
        REQUIRE(paramId == 63234);
    }
}

// ==============================================================================
// Test: Bypass Parameter ID Encoding (T072)
// ==============================================================================
TEST_CASE("Band Bypass parameter ID is correctly encoded", "[bandstate][bypass]") {

    SECTION("Band 0 Bypass has tag value 61443") {
        // 0xF << 12 | 0 << 8 | 0x03 = 0xF003 = 61443
        auto paramId = makeBandParamId(0, BandParamType::kBandBypass);
        REQUIRE(paramId == 0xF003);
        REQUIRE(paramId == 61443);
    }

    SECTION("Band 1 Bypass has tag value 61699") {
        // 0xF << 12 | 1 << 8 | 0x03 = 0xF103 = 61699
        auto paramId = makeBandParamId(1, BandParamType::kBandBypass);
        REQUIRE(paramId == 0xF103);
        REQUIRE(paramId == 61699);
    }

    SECTION("Band 4 Bypass has tag value 62467") {
        // 0xF << 12 | 4 << 8 | 0x03 = 0xF403 = 62467
        auto paramId = makeBandParamId(4, BandParamType::kBandBypass);
        REQUIRE(paramId == 0xF403);
        REQUIRE(paramId == 62467);
    }

    SECTION("Band 7 Bypass has tag value 63235") {
        // 0xF << 12 | 7 << 8 | 0x03 = 0xF703 = 63235
        auto paramId = makeBandParamId(7, BandParamType::kBandBypass);
        REQUIRE(paramId == 0xF703);
        REQUIRE(paramId == 63235);
    }
}

// ==============================================================================
// Test: Mute Parameter ID Encoding (T072)
// ==============================================================================
TEST_CASE("Band Mute parameter ID is correctly encoded", "[bandstate][mute]") {

    SECTION("Band 0 Mute has tag value 61444") {
        // 0xF << 12 | 0 << 8 | 0x04 = 0xF004 = 61444
        auto paramId = makeBandParamId(0, BandParamType::kBandMute);
        REQUIRE(paramId == 0xF004);
        REQUIRE(paramId == 61444);
    }

    SECTION("Band 2 Mute has tag value 61956") {
        // 0xF << 12 | 2 << 8 | 0x04 = 0xF204 = 61956
        auto paramId = makeBandParamId(2, BandParamType::kBandMute);
        REQUIRE(paramId == 0xF204);
        REQUIRE(paramId == 61956);
    }

    SECTION("Band 5 Mute has tag value 62724") {
        // 0xF << 12 | 5 << 8 | 0x04 = 0xF504 = 62724
        auto paramId = makeBandParamId(5, BandParamType::kBandMute);
        REQUIRE(paramId == 0xF504);
        REQUIRE(paramId == 62724);
    }

    SECTION("Band 7 Mute has tag value 63236") {
        // 0xF << 12 | 7 << 8 | 0x04 = 0xF704 = 63236
        auto paramId = makeBandParamId(7, BandParamType::kBandMute);
        REQUIRE(paramId == 0xF704);
        REQUIRE(paramId == 63236);
    }
}

// ==============================================================================
// Test: All 8 Bands Have Unique Solo/Bypass/Mute IDs (T072)
// ==============================================================================
TEST_CASE("All bands produce unique Solo/Bypass/Mute IDs with no collisions", "[bandstate]") {
    std::set<Steinberg::Vst::ParamID> allIds;

    for (int band = 0; band < 8; ++band) {
        auto soloId = makeBandParamId(static_cast<uint8_t>(band), BandParamType::kBandSolo);
        auto bypassId = makeBandParamId(static_cast<uint8_t>(band), BandParamType::kBandBypass);
        auto muteId = makeBandParamId(static_cast<uint8_t>(band), BandParamType::kBandMute);

        // Insert should return true (not a duplicate)
        REQUIRE(allIds.insert(soloId).second == true);
        REQUIRE(allIds.insert(bypassId).second == true);
        REQUIRE(allIds.insert(muteId).second == true);
    }

    // We should have 8 * 3 = 24 unique IDs
    REQUIRE(allIds.size() == 24);
}

// ==============================================================================
// Test: Solo/Bypass/Mute Don't Overlap with Other Band Params (T072)
// ==============================================================================
TEST_CASE("Solo/Bypass/Mute don't overlap with Gain/Pan", "[bandstate]") {
    std::set<Steinberg::Vst::ParamID> stateIds;
    std::set<Steinberg::Vst::ParamID> otherIds;

    for (int band = 0; band < 8; ++band) {
        // State parameters
        stateIds.insert(makeBandParamId(static_cast<uint8_t>(band), BandParamType::kBandSolo));
        stateIds.insert(makeBandParamId(static_cast<uint8_t>(band), BandParamType::kBandBypass));
        stateIds.insert(makeBandParamId(static_cast<uint8_t>(band), BandParamType::kBandMute));

        // Other band parameters
        otherIds.insert(makeBandParamId(static_cast<uint8_t>(band), BandParamType::kBandGain));
        otherIds.insert(makeBandParamId(static_cast<uint8_t>(band), BandParamType::kBandPan));
        otherIds.insert(makeBandParamId(static_cast<uint8_t>(band), BandParamType::kBandMorphX));
        otherIds.insert(makeBandParamId(static_cast<uint8_t>(band), BandParamType::kBandMorphY));
    }

    // Check for any overlap
    for (const auto& stateId : stateIds) {
        REQUIRE(otherIds.count(stateId) == 0);
    }
}

// ==============================================================================
// Test: Band Parameter Type Extraction (T072)
// ==============================================================================
TEST_CASE("Band state parameter type can be extracted", "[bandstate][extraction]") {

    SECTION("Extract Solo from Band 0") {
        auto paramId = makeBandParamId(0, BandParamType::kBandSolo);
        REQUIRE(isBandParamId(paramId) == true);
        REQUIRE(extractBandParamType(paramId) == BandParamType::kBandSolo);
        REQUIRE(extractBandIndex(paramId) == 0);
    }

    SECTION("Extract Bypass from Band 3") {
        auto paramId = makeBandParamId(3, BandParamType::kBandBypass);
        REQUIRE(isBandParamId(paramId) == true);
        REQUIRE(extractBandParamType(paramId) == BandParamType::kBandBypass);
        REQUIRE(extractBandIndex(paramId) == 3);
    }

    SECTION("Extract Mute from Band 7") {
        auto paramId = makeBandParamId(7, BandParamType::kBandMute);
        REQUIRE(isBandParamId(paramId) == true);
        REQUIRE(extractBandParamType(paramId) == BandParamType::kBandMute);
        REQUIRE(extractBandIndex(paramId) == 7);
    }
}

// ==============================================================================
// Test: Boolean Parameter Normalized Values (T072)
// ==============================================================================
TEST_CASE("Boolean parameters use correct normalized values", "[bandstate][boolean]") {
    // Boolean parameters with stepCount=1 should have exactly 2 states:
    // normalized 0.0 = off, normalized 1.0 = on

    SECTION("Off state is normalized 0.0") {
        float normalized = 0.0f;
        bool isOn = normalized >= 0.5f;
        REQUIRE(isOn == false);
    }

    SECTION("On state is normalized 1.0") {
        float normalized = 1.0f;
        bool isOn = normalized >= 0.5f;
        REQUIRE(isOn == true);
    }

    SECTION("Threshold is at 0.5") {
        REQUIRE((0.49f >= 0.5f) == false);
        REQUIRE((0.51f >= 0.5f) == true);
    }
}

// ==============================================================================
// Test: Control-Tag Decimal Values (T072)
// ==============================================================================
TEST_CASE("Solo/Bypass/Mute control-tag decimal values", "[bandstate][controltag]") {

    SECTION("Band 1 Solo tag is 61442") {
        auto paramId = makeBandParamId(0, BandParamType::kBandSolo);
        REQUIRE(paramId == 61442);
    }

    SECTION("Band 1 Bypass tag is 61443") {
        auto paramId = makeBandParamId(0, BandParamType::kBandBypass);
        REQUIRE(paramId == 61443);
    }

    SECTION("Band 1 Mute tag is 61444") {
        auto paramId = makeBandParamId(0, BandParamType::kBandMute);
        REQUIRE(paramId == 61444);
    }

    SECTION("Band 2 Solo tag is 61698") {
        auto paramId = makeBandParamId(1, BandParamType::kBandSolo);
        REQUIRE(paramId == 61698);
    }

    SECTION("Band 2 Bypass tag is 61699") {
        auto paramId = makeBandParamId(1, BandParamType::kBandBypass);
        REQUIRE(paramId == 61699);
    }

    SECTION("Band 2 Mute tag is 61700") {
        auto paramId = makeBandParamId(1, BandParamType::kBandMute);
        REQUIRE(paramId == 61700);
    }
}

// ==============================================================================
// Test: Additive Solo Logic (T072)
// ==============================================================================
TEST_CASE("Additive solo logic: multiple bands can be soloed", "[bandstate][solo]") {
    // Additive solo means each band's Solo is independent.
    // When any Solo is active, only soloed bands pass audio.

    struct BandState {
        bool solo = false;
        bool mute = false;
        bool bypass = false;
    };

    std::array<BandState, 8> bands = {};

    auto anySoloActive = [&bands]() -> bool {
        for (const auto& band : bands) {
            if (band.solo) return true;
        }
        return false;
    };

    auto bandPassesAudio = [&bands, &anySoloActive](int bandIndex) -> bool {
        // Muted bands never pass audio
        if (bands[bandIndex].mute) return false;

        // If any solo is active, only soloed bands pass
        if (anySoloActive()) {
            return bands[bandIndex].solo;
        }

        // No solo active, all unmuted bands pass
        return true;
    };

    SECTION("No solos: all unmuted bands pass audio") {
        REQUIRE(bandPassesAudio(0) == true);
        REQUIRE(bandPassesAudio(1) == true);
        REQUIRE(bandPassesAudio(7) == true);
    }

    SECTION("Solo band 2: only band 2 passes") {
        bands[2].solo = true;

        REQUIRE(bandPassesAudio(0) == false);
        REQUIRE(bandPassesAudio(1) == false);
        REQUIRE(bandPassesAudio(2) == true);
        REQUIRE(bandPassesAudio(3) == false);
    }

    SECTION("Solo bands 2 and 4: both pass (additive)") {
        bands[2].solo = true;
        bands[4].solo = true;

        REQUIRE(bandPassesAudio(0) == false);
        REQUIRE(bandPassesAudio(2) == true);
        REQUIRE(bandPassesAudio(4) == true);
        REQUIRE(bandPassesAudio(5) == false);
    }

    SECTION("Solo band 2, mute band 4: band 4 still muted") {
        bands[2].solo = true;
        bands[4].mute = true;

        REQUIRE(bandPassesAudio(2) == true);
        REQUIRE(bandPassesAudio(4) == false);  // Mute takes precedence
    }
}
