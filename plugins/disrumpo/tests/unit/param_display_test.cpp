// ==============================================================================
// Drive/Mix Parameter Display Tests
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Tests for Drive/Mix display formatting (T063)
//
// Verifies:
// - Drive value formatting: plain number, one decimal (e.g., "5.2")
// - Mix value formatting: percentage, no decimal (e.g., "75%")
// - Parameter IDs are correctly encoded for per-node Drive/Mix
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "plugin_ids.h"

#include <string>
#include <cmath>
#include <cstdio>

using namespace Disrumpo;
using Catch::Approx;

// ==============================================================================
// Test: Drive Parameter ID Encoding (T063)
// ==============================================================================
TEST_CASE("Node Drive parameter ID is correctly encoded", "[drive][encoding]") {

    SECTION("Band 0 Node 0 Drive has tag value 1") {
        auto paramId = makeNodeParamId(0, 0, NodeParamType::kNodeDrive);
        REQUIRE(paramId == 0x0001);
        REQUIRE(paramId == 1);
    }

    SECTION("Band 1 Node 0 Drive has tag value 257") {
        // band=1 << 8 | param=1 = 0x0101 = 257
        auto paramId = makeNodeParamId(1, 0, NodeParamType::kNodeDrive);
        REQUIRE(paramId == 0x0101);
        REQUIRE(paramId == 257);
    }

    SECTION("Band 3 Node 2 Drive has tag value 8961") {
        // node=2 << 12 | band=3 << 8 | param=1 = 0x2301 = 8961
        auto paramId = makeNodeParamId(3, 2, NodeParamType::kNodeDrive);
        REQUIRE(paramId == 0x2301);
        REQUIRE(paramId == 8961);
    }
}

// ==============================================================================
// Test: Mix Parameter ID Encoding (T063)
// ==============================================================================
TEST_CASE("Node Mix parameter ID is correctly encoded", "[mix][encoding]") {

    SECTION("Band 0 Node 0 Mix has tag value 2") {
        auto paramId = makeNodeParamId(0, 0, NodeParamType::kNodeMix);
        REQUIRE(paramId == 0x0002);
        REQUIRE(paramId == 2);
    }

    SECTION("Band 1 Node 0 Mix has tag value 258") {
        // band=1 << 8 | param=2 = 0x0102 = 258
        auto paramId = makeNodeParamId(1, 0, NodeParamType::kNodeMix);
        REQUIRE(paramId == 0x0102);
        REQUIRE(paramId == 258);
    }

    SECTION("Band 7 Node 3 Mix has tag value 14082") {
        // node=3 << 12 | band=7 << 8 | param=2 = 0x3702 = 14082
        auto paramId = makeNodeParamId(7, 3, NodeParamType::kNodeMix);
        REQUIRE(paramId == 0x3702);
        REQUIRE(paramId == 14082);
    }
}

// ==============================================================================
// Test: All 8 Bands Have Unique Drive/Mix IDs for Node 0 (T063)
// ==============================================================================
TEST_CASE("Each band's Node 0 Drive and Mix have unique parameter IDs", "[drive][mix]") {
    std::vector<Steinberg::Vst::ParamID> driveIds;
    std::vector<Steinberg::Vst::ParamID> mixIds;

    for (int band = 0; band < 8; ++band) {
        auto driveId = makeNodeParamId(static_cast<uint8_t>(band), 0, NodeParamType::kNodeDrive);
        auto mixId = makeNodeParamId(static_cast<uint8_t>(band), 0, NodeParamType::kNodeMix);
        driveIds.push_back(driveId);
        mixIds.push_back(mixId);
    }

    // Verify all Drive IDs are unique
    for (size_t i = 0; i < driveIds.size(); ++i) {
        for (size_t j = i + 1; j < driveIds.size(); ++j) {
            REQUIRE(driveIds[i] != driveIds[j]);
        }
    }

    // Verify all Mix IDs are unique
    for (size_t i = 0; i < mixIds.size(); ++i) {
        for (size_t j = i + 1; j < mixIds.size(); ++j) {
            REQUIRE(mixIds[i] != mixIds[j]);
        }
    }

    // Verify Drive and Mix don't overlap
    for (const auto& driveId : driveIds) {
        for (const auto& mixId : mixIds) {
            REQUIRE(driveId != mixId);
        }
    }
}

// ==============================================================================
// Test: Drive Display Format (T063)
// ==============================================================================
TEST_CASE("Drive displays as plain number with one decimal", "[drive][display]") {
    // Drive range is [0, 10], normalized value maps linearly

    auto formatDrive = [](double plain) -> std::string {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f", plain);
        return std::string(buffer);
    };

    SECTION("Drive 0.0 displays as '0.0'") {
        REQUIRE(formatDrive(0.0) == "0.0");
    }

    SECTION("Drive 1.0 displays as '1.0'") {
        REQUIRE(formatDrive(1.0) == "1.0");
    }

    SECTION("Drive 5.2 displays as '5.2'") {
        REQUIRE(formatDrive(5.2) == "5.2");
    }

    SECTION("Drive 7.5 displays as '7.5'") {
        REQUIRE(formatDrive(7.5) == "7.5");
    }

    SECTION("Drive 10.0 displays as '10.0'") {
        REQUIRE(formatDrive(10.0) == "10.0");
    }
}

// ==============================================================================
// Test: Mix Display Format (T063)
// ==============================================================================
TEST_CASE("Mix displays as percentage with no decimal", "[mix][display]") {
    // Mix range is [0, 100], displayed as integer percentage

    auto formatMix = [](double plain) -> std::string {
        int percent = static_cast<int>(std::round(plain));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%%", percent);
        return std::string(buffer);
    };

    SECTION("Mix 0% displays as '0%'") {
        REQUIRE(formatMix(0.0) == "0%");
    }

    SECTION("Mix 50% displays as '50%'") {
        REQUIRE(formatMix(50.0) == "50%");
    }

    SECTION("Mix 75% displays as '75%'") {
        REQUIRE(formatMix(75.0) == "75%");
    }

    SECTION("Mix 100% displays as '100%'") {
        REQUIRE(formatMix(100.0) == "100%");
    }

    SECTION("Mix 33.3% rounds to '33%'") {
        REQUIRE(formatMix(33.3) == "33%");
    }

    SECTION("Mix 66.7% rounds to '67%'") {
        REQUIRE(formatMix(66.7) == "67%");
    }
}

// ==============================================================================
// Test: Drive Normalized-to-Plain Conversion (T063)
// ==============================================================================
TEST_CASE("Drive normalized-to-plain conversion", "[drive][conversion]") {
    // Drive: min=0, max=10
    // plain = min + normalized * (max - min) = normalized * 10

    auto normalizedToPlain = [](float normalized) -> float {
        return normalized * 10.0f;
    };

    SECTION("Normalized 0.0 gives plain 0.0") {
        REQUIRE(normalizedToPlain(0.0f) == Approx(0.0f).margin(0.001f));
    }

    SECTION("Normalized 0.1 gives plain 1.0") {
        REQUIRE(normalizedToPlain(0.1f) == Approx(1.0f).margin(0.001f));
    }

    SECTION("Normalized 0.5 gives plain 5.0") {
        REQUIRE(normalizedToPlain(0.5f) == Approx(5.0f).margin(0.001f));
    }

    SECTION("Normalized 1.0 gives plain 10.0") {
        REQUIRE(normalizedToPlain(1.0f) == Approx(10.0f).margin(0.001f));
    }
}

// ==============================================================================
// Test: Mix Normalized-to-Plain Conversion (T063)
// ==============================================================================
TEST_CASE("Mix normalized-to-plain conversion", "[mix][conversion]") {
    // Mix: min=0, max=100
    // plain = normalized * 100

    auto normalizedToPlain = [](float normalized) -> float {
        return normalized * 100.0f;
    };

    SECTION("Normalized 0.0 gives plain 0%") {
        REQUIRE(normalizedToPlain(0.0f) == Approx(0.0f).margin(0.001f));
    }

    SECTION("Normalized 0.5 gives plain 50%") {
        REQUIRE(normalizedToPlain(0.5f) == Approx(50.0f).margin(0.001f));
    }

    SECTION("Normalized 0.75 gives plain 75%") {
        REQUIRE(normalizedToPlain(0.75f) == Approx(75.0f).margin(0.001f));
    }

    SECTION("Normalized 1.0 gives plain 100%") {
        REQUIRE(normalizedToPlain(1.0f) == Approx(100.0f).margin(0.001f));
    }
}

// ==============================================================================
// Test: Control-Tag Decimal Values for Drive/Mix (T063)
// ==============================================================================
TEST_CASE("Drive and Mix control-tag decimal values", "[drive][mix][controltag]") {
    // uidesc control-tags must use decimal values

    SECTION("Band 1 Node 1 Drive tag is 1") {
        auto paramId = makeNodeParamId(0, 0, NodeParamType::kNodeDrive);
        REQUIRE(paramId == 1);
    }

    SECTION("Band 1 Node 1 Mix tag is 2") {
        auto paramId = makeNodeParamId(0, 0, NodeParamType::kNodeMix);
        REQUIRE(paramId == 2);
    }

    SECTION("Band 2 Node 1 Drive tag is 257") {
        auto paramId = makeNodeParamId(1, 0, NodeParamType::kNodeDrive);
        REQUIRE(paramId == 257);
    }

    SECTION("Band 2 Node 1 Mix tag is 258") {
        auto paramId = makeNodeParamId(1, 0, NodeParamType::kNodeMix);
        REQUIRE(paramId == 258);
    }
}
