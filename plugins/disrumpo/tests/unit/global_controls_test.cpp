// ==============================================================================
// Global Controls and Band Count Tests
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Tests for CSegmentButton band count wiring (T036)
//
// Band Count parameter:
// - StringListParameter with 8 values ["1", "2", ..., "8"]
// - Segment index i maps to band count i+1
// - Normalized value at index i = i / 7.0f (for 8 values, indices 0-7)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "plugin_ids.h"

using namespace Disrumpo;
using Catch::Approx;

// ==============================================================================
// Test: Band Count Segment Index to Normalized Value Mapping
// ==============================================================================
TEST_CASE("Band count segment index maps to correct normalized value", "[global][bandcount]") {
    // StringListParameter with N items: normalized value for index i = i / (N-1)
    // For 8 items (indices 0-7): normalized = index / 7.0

    SECTION("Segment index 0 (band count 1) maps to normalized 0.0") {
        int segmentIndex = 0;
        float normalized = static_cast<float>(segmentIndex) / 7.0f;
        REQUIRE(normalized == Approx(0.0f).margin(0.001f));
    }

    SECTION("Segment index 1 (band count 2) maps to normalized 1/7") {
        int segmentIndex = 1;
        float normalized = static_cast<float>(segmentIndex) / 7.0f;
        REQUIRE(normalized == Approx(1.0f / 7.0f).margin(0.001f));
    }

    SECTION("Segment index 3 (band count 4) maps to normalized 3/7") {
        int segmentIndex = 3;
        float normalized = static_cast<float>(segmentIndex) / 7.0f;
        REQUIRE(normalized == Approx(3.0f / 7.0f).margin(0.001f));
        REQUIRE(normalized == Approx(0.4286f).margin(0.001f));
    }

    SECTION("Segment index 5 (band count 6) maps to normalized 5/7") {
        int segmentIndex = 5;
        float normalized = static_cast<float>(segmentIndex) / 7.0f;
        REQUIRE(normalized == Approx(5.0f / 7.0f).margin(0.001f));
    }

    SECTION("Segment index 7 (band count 8) maps to normalized 1.0") {
        int segmentIndex = 7;
        float normalized = static_cast<float>(segmentIndex) / 7.0f;
        REQUIRE(normalized == Approx(1.0f).margin(0.001f));
    }
}

// ==============================================================================
// Test: Normalized Value to Band Count Conversion
// ==============================================================================
TEST_CASE("Normalized value converts to correct band count", "[global][bandcount]") {
    // To convert normalized to band count: round(normalized * 7) + 1
    // Or: floor(normalized * 7 + 0.5) + 1

    auto normalizedToBandCount = [](float normalized) -> int {
        int index = static_cast<int>(std::round(normalized * 7.0f));
        return index + 1;  // Band count is 1-based
    };

    SECTION("Normalized 0.0 gives band count 1") {
        REQUIRE(normalizedToBandCount(0.0f) == 1);
    }

    SECTION("Normalized 3/7 gives band count 4") {
        REQUIRE(normalizedToBandCount(3.0f / 7.0f) == 4);
    }

    SECTION("Normalized 1.0 gives band count 8") {
        REQUIRE(normalizedToBandCount(1.0f) == 8);
    }

    SECTION("Normalized 0.5 gives band count 4 or 5 (depending on rounding)") {
        // 0.5 * 7 = 3.5, rounds to 4, +1 = 5
        REQUIRE(normalizedToBandCount(0.5f) == 5);
    }
}

// ==============================================================================
// Test: Band Count Parameter ID
// ==============================================================================
TEST_CASE("Band count parameter ID is correctly encoded", "[global][bandcount]") {
    SECTION("Band count parameter ID is in global range (0x0F00)") {
        auto paramId = makeGlobalParamId(GlobalParamType::kGlobalBandCount);
        REQUIRE(paramId == 0x0F03);
        REQUIRE(paramId == 3843);
    }

    SECTION("Band count parameter is identified as global") {
        auto paramId = makeGlobalParamId(GlobalParamType::kGlobalBandCount);
        REQUIRE(isGlobalParamId(paramId) == true);
        REQUIRE(isBandParamId(paramId) == false);
        REQUIRE(isNodeParamId(paramId) == false);
        REQUIRE(isSweepParamId(paramId) == false);
    }
}

// ==============================================================================
// Test: Band Visibility Threshold Calculation
// ==============================================================================
TEST_CASE("Band visibility threshold is correctly calculated", "[global][visibility]") {
    // Band b is visible when bandCount >= b+1
    // In normalized terms: normalized >= b/7
    // Threshold for band index b = b / 7.0

    auto bandVisibilityThreshold = [](int bandIndex) -> float {
        return static_cast<float>(bandIndex) / 7.0f;
    };

    SECTION("Band 0 threshold is 0.0 (always visible)") {
        REQUIRE(bandVisibilityThreshold(0) == Approx(0.0f).margin(0.001f));
    }

    SECTION("Band 1 threshold is 1/7") {
        REQUIRE(bandVisibilityThreshold(1) == Approx(1.0f / 7.0f).margin(0.001f));
    }

    SECTION("Band 3 threshold is 3/7") {
        REQUIRE(bandVisibilityThreshold(3) == Approx(3.0f / 7.0f).margin(0.001f));
    }

    SECTION("Band 4 threshold is 4/7") {
        REQUIRE(bandVisibilityThreshold(4) == Approx(4.0f / 7.0f).margin(0.001f));
    }

    SECTION("Band 7 threshold is 1.0") {
        REQUIRE(bandVisibilityThreshold(7) == Approx(1.0f).margin(0.001f));
    }
}

// ==============================================================================
// Test: Band Visibility at Different Band Counts
// ==============================================================================
TEST_CASE("Band visibility is correct for different band counts", "[global][visibility]") {
    // isVisible(bandIndex, normalizedBandCount) = normalizedBandCount >= bandIndex/7.0
    // But we need to use > for cleaner threshold semantics:
    // Band b is visible when bandCount > b (since bandCount is 1-indexed)
    // For StringListParameter with 8 items:
    // - Index 0 = "1" = 1 band visible (bands 0)
    // - Index 3 = "4" = 4 bands visible (bands 0-3)
    // So band b is visible when index >= b, i.e., normalized >= b/7.0

    auto isBandVisible = [](int bandIndex, float normalizedBandCount) -> bool {
        // Using a small epsilon for floating point comparison
        float threshold = static_cast<float>(bandIndex) / 7.0f;
        return normalizedBandCount >= threshold - 0.001f;
    };

    SECTION("Band count = 1 (normalized = 0.0): only band 0 visible") {
        float normalized = 0.0f;
        REQUIRE(isBandVisible(0, normalized) == true);
        REQUIRE(isBandVisible(1, normalized) == false);
        REQUIRE(isBandVisible(7, normalized) == false);
    }

    SECTION("Band count = 4 (normalized = 3/7): bands 0-3 visible") {
        float normalized = 3.0f / 7.0f;  // This is the index, not the count
        REQUIRE(isBandVisible(0, normalized) == true);
        REQUIRE(isBandVisible(1, normalized) == true);
        REQUIRE(isBandVisible(2, normalized) == true);
        REQUIRE(isBandVisible(3, normalized) == true);
        REQUIRE(isBandVisible(4, normalized) == false);
        REQUIRE(isBandVisible(5, normalized) == false);
    }

    SECTION("Band count = 6 (normalized = 5/7): bands 0-5 visible") {
        float normalized = 5.0f / 7.0f;
        REQUIRE(isBandVisible(0, normalized) == true);
        REQUIRE(isBandVisible(1, normalized) == true);
        REQUIRE(isBandVisible(4, normalized) == true);
        REQUIRE(isBandVisible(5, normalized) == true);
        REQUIRE(isBandVisible(6, normalized) == false);
        REQUIRE(isBandVisible(7, normalized) == false);
    }

    SECTION("Band count = 8 (normalized = 1.0): all bands visible") {
        float normalized = 1.0f;
        for (int i = 0; i < 8; ++i) {
            REQUIRE(isBandVisible(i, normalized) == true);
        }
    }
}

// ==============================================================================
// Test: Global Control Parameter IDs
// ==============================================================================
TEST_CASE("Global control parameter IDs are correctly encoded", "[global][encoding]") {
    SECTION("Input Gain ID") {
        auto paramId = makeGlobalParamId(GlobalParamType::kGlobalInputGain);
        REQUIRE(paramId == 0x0F00);
        REQUIRE(paramId == 3840);
    }

    SECTION("Output Gain ID") {
        auto paramId = makeGlobalParamId(GlobalParamType::kGlobalOutputGain);
        REQUIRE(paramId == 0x0F01);
        REQUIRE(paramId == 3841);
    }

    SECTION("Mix ID") {
        auto paramId = makeGlobalParamId(GlobalParamType::kGlobalMix);
        REQUIRE(paramId == 0x0F02);
        REQUIRE(paramId == 3842);
    }

    SECTION("Band Count ID") {
        auto paramId = makeGlobalParamId(GlobalParamType::kGlobalBandCount);
        REQUIRE(paramId == 0x0F03);
        REQUIRE(paramId == 3843);
    }

    SECTION("Oversample Max ID") {
        auto paramId = makeGlobalParamId(GlobalParamType::kGlobalOversample);
        REQUIRE(paramId == 0x0F04);
        REQUIRE(paramId == 3844);
    }
}
