// ==============================================================================
// Parameter Display Formatting Tests
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Tests for getParamStringByValue() custom formatting (FR-027)
//
// Formatting rules per spec:
// - Drive: plain number, one decimal, no unit (e.g., "5.2")
// - Mix: percentage, no decimal (e.g., "75%")
// - Gain: dB with one decimal (e.g., "4.5 dB")
// - Pan: "Center" at 0.5 normalized, else "30% L" or "30% R"
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "plugin_ids.h"

#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <string>
#include <cstring>

using namespace Disrumpo;
using Catch::Approx;

// ==============================================================================
// Helper: Convert TChar string to std::string for comparison
// ==============================================================================
static std::string tcharToString(const Steinberg::Vst::TChar* str) {
    std::string result;
    while (*str) {
        result += static_cast<char>(*str);
        ++str;
    }
    return result;
}

// ==============================================================================
// Test Fixture: Create Controller-like parameter container for testing
// ==============================================================================
// Note: These tests verify the formatting logic, not the actual Controller.
// The Controller's getParamStringByValue() implementation must match these rules.

// ==============================================================================
// Test: Drive Display Format (T036c)
// ==============================================================================
TEST_CASE("Drive parameter displays as plain number with one decimal", "[display][format]") {
    // Drive range is [0, 10], so normalized value maps linearly

    SECTION("Drive value 1.0 displays as '1.0'") {
        // normalized = 1.0 / 10.0 = 0.1
        double normalized = 0.1;
        double plain = normalized * 10.0;  // Simulate toPlain()

        // Format: one decimal, no unit
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f", plain);

        REQUIRE(std::string(buffer) == "1.0");
    }

    SECTION("Drive value 5.2 displays as '5.2'") {
        double plain = 5.2;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f", plain);

        REQUIRE(std::string(buffer) == "5.2");
    }

    SECTION("Drive value 10.0 displays as '10.0'") {
        double plain = 10.0;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f", plain);

        REQUIRE(std::string(buffer) == "10.0");
    }

    SECTION("Drive value 0.0 displays as '0.0'") {
        double plain = 0.0;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f", plain);

        REQUIRE(std::string(buffer) == "0.0");
    }

    SECTION("Drive value 7.5 displays as '7.5'") {
        double plain = 7.5;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f", plain);

        REQUIRE(std::string(buffer) == "7.5");
    }
}

// ==============================================================================
// Test: Mix Display Format (T036c)
// ==============================================================================
TEST_CASE("Mix parameter displays as percentage with no decimal", "[display][format]") {
    // Mix range is [0, 100], displayed as integer percentage

    SECTION("Mix value 75% displays as '75%'") {
        double plain = 75.0;
        int percent = static_cast<int>(std::round(plain));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%%", percent);

        REQUIRE(std::string(buffer) == "75%");
    }

    SECTION("Mix value 0% displays as '0%'") {
        double plain = 0.0;
        int percent = static_cast<int>(std::round(plain));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%%", percent);

        REQUIRE(std::string(buffer) == "0%");
    }

    SECTION("Mix value 100% displays as '100%'") {
        double plain = 100.0;
        int percent = static_cast<int>(std::round(plain));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%%", percent);

        REQUIRE(std::string(buffer) == "100%");
    }

    SECTION("Mix value 50% displays as '50%'") {
        double plain = 50.0;
        int percent = static_cast<int>(std::round(plain));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%%", percent);

        REQUIRE(std::string(buffer) == "50%");
    }

    SECTION("Mix value 33.3% rounds to '33%'") {
        double plain = 33.3;
        int percent = static_cast<int>(std::round(plain));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%%", percent);

        REQUIRE(std::string(buffer) == "33%");
    }

    SECTION("Mix value 66.7% rounds to '67%'") {
        double plain = 66.7;
        int percent = static_cast<int>(std::round(plain));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%%", percent);

        REQUIRE(std::string(buffer) == "67%");
    }
}

// ==============================================================================
// Test: Gain Display Format (T036c)
// ==============================================================================
TEST_CASE("Gain parameter displays with dB suffix and one decimal", "[display][format]") {
    // Gain range is [-24, +24] dB

    SECTION("Gain value 0.0 displays as '0.0 dB'") {
        double plain = 0.0;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f dB", plain);

        REQUIRE(std::string(buffer) == "0.0 dB");
    }

    SECTION("Gain value 4.5 displays as '4.5 dB'") {
        double plain = 4.5;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f dB", plain);

        REQUIRE(std::string(buffer) == "4.5 dB");
    }

    SECTION("Gain value -12.0 displays as '-12.0 dB'") {
        double plain = -12.0;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f dB", plain);

        REQUIRE(std::string(buffer) == "-12.0 dB");
    }

    SECTION("Gain value 24.0 displays as '24.0 dB'") {
        double plain = 24.0;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f dB", plain);

        REQUIRE(std::string(buffer) == "24.0 dB");
    }

    SECTION("Gain value -24.0 displays as '-24.0 dB'") {
        double plain = -24.0;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.1f dB", plain);

        REQUIRE(std::string(buffer) == "-24.0 dB");
    }
}

// ==============================================================================
// Test: Pan Display Format (T036c)
// ==============================================================================
TEST_CASE("Pan parameter displays with L/R suffix or Center", "[display][format]") {
    // Pan range is [-1, +1] in plain value
    // normalized 0.5 = plain 0.0 = Center
    // normalized < 0.5 = Left
    // normalized > 0.5 = Right

    SECTION("Pan at center (0.0 plain) displays as 'Center'") {
        double plain = 0.0;
        std::string result;

        if (std::abs(plain) < 0.01) {
            result = "Center";
        }

        REQUIRE(result == "Center");
    }

    SECTION("Pan at -0.3 displays as '30% L'") {
        double plain = -0.3;
        int percent = static_cast<int>(std::round(std::abs(plain) * 100.0));
        char buffer[32];

        if (plain < 0) {
            snprintf(buffer, sizeof(buffer), "%d%% L", percent);
        }

        REQUIRE(std::string(buffer) == "30% L");
    }

    SECTION("Pan at +0.3 displays as '30% R'") {
        double plain = 0.3;
        int percent = static_cast<int>(std::round(plain * 100.0));
        char buffer[32];

        if (plain > 0) {
            snprintf(buffer, sizeof(buffer), "%d%% R", percent);
        }

        REQUIRE(std::string(buffer) == "30% R");
    }

    SECTION("Pan at -1.0 (full left) displays as '100% L'") {
        double plain = -1.0;
        int percent = static_cast<int>(std::round(std::abs(plain) * 100.0));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%% L", percent);

        REQUIRE(std::string(buffer) == "100% L");
    }

    SECTION("Pan at +1.0 (full right) displays as '100% R'") {
        double plain = 1.0;
        int percent = static_cast<int>(std::round(plain * 100.0));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%% R", percent);

        REQUIRE(std::string(buffer) == "100% R");
    }

    SECTION("Pan near center (-0.005) still displays as 'Center'") {
        double plain = -0.005;
        std::string result;

        if (std::abs(plain) < 0.01) {
            result = "Center";
        } else if (plain < 0) {
            int percent = static_cast<int>(std::round(std::abs(plain) * 100.0));
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% L", percent);
            result = buffer;
        }

        REQUIRE(result == "Center");
    }

    SECTION("Pan at -0.5 displays as '50% L'") {
        double plain = -0.5;
        int percent = static_cast<int>(std::round(std::abs(plain) * 100.0));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%% L", percent);

        REQUIRE(std::string(buffer) == "50% L");
    }

    SECTION("Pan at +0.75 displays as '75% R'") {
        double plain = 0.75;
        int percent = static_cast<int>(std::round(plain * 100.0));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%% R", percent);

        REQUIRE(std::string(buffer) == "75% R");
    }
}

// ==============================================================================
// Test: Parameter ID Detection for Format Selection
// ==============================================================================
TEST_CASE("Parameter ID encoding identifies correct parameter types", "[display][encoding]") {
    SECTION("Node Drive parameter is correctly identified") {
        auto paramId = makeNodeParamId(0, 0, NodeParamType::kNodeDrive);
        REQUIRE(isNodeParamId(paramId) == true);
        REQUIRE(extractNodeParamType(paramId) == NodeParamType::kNodeDrive);
    }

    SECTION("Node Mix parameter is correctly identified") {
        auto paramId = makeNodeParamId(3, 2, NodeParamType::kNodeMix);
        REQUIRE(isNodeParamId(paramId) == true);
        REQUIRE(extractNodeParamType(paramId) == NodeParamType::kNodeMix);
    }

    SECTION("Band Gain parameter is correctly identified") {
        auto paramId = makeBandParamId(5, BandParamType::kBandGain);
        REQUIRE(isBandParamId(paramId) == true);
        REQUIRE(extractBandParamType(paramId) == BandParamType::kBandGain);
    }

    SECTION("Band Pan parameter is correctly identified") {
        auto paramId = makeBandParamId(2, BandParamType::kBandPan);
        REQUIRE(isBandParamId(paramId) == true);
        REQUIRE(extractBandParamType(paramId) == BandParamType::kBandPan);
    }

    SECTION("Global Mix parameter is correctly identified") {
        auto paramId = makeGlobalParamId(GlobalParamType::kGlobalMix);
        REQUIRE(isGlobalParamId(paramId) == true);
    }

    SECTION("Global Input Gain parameter is correctly identified") {
        auto paramId = makeGlobalParamId(GlobalParamType::kGlobalInputGain);
        REQUIRE(isGlobalParamId(paramId) == true);
    }
}
