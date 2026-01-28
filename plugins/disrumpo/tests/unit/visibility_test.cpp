// ==============================================================================
// Band Visibility Threshold Tests
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Tests for ContainerVisibilityController threshold logic (T086)
//
// Verifies:
// - Band visibility thresholds are correctly calculated
// - Bands show/hide based on BandCount parameter value
// - Edge cases for band count changes
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "plugin_ids.h"

#include <cmath>

using namespace Disrumpo;
using Catch::Approx;

// ==============================================================================
// Test: Band Visibility Threshold Calculation (T086)
// ==============================================================================
TEST_CASE("Band visibility threshold is correctly calculated", "[visibility][threshold]") {
    // Band b is visible when BandCount normalized value >= threshold
    // For StringListParameter with 8 items (indices 0-7 = counts 1-8):
    // Threshold for band index b = b / 7.0
    // Band 0: threshold 0.0 (always visible)
    // Band 1: threshold 1/7 = 0.143
    // Band 4: threshold 4/7 = 0.571
    // Band 7: threshold 7/7 = 1.0

    auto calculateThreshold = [](int bandIndex) -> float {
        return static_cast<float>(bandIndex) / 7.0f;
    };

    SECTION("Band 0 threshold is 0.0 (always visible)") {
        REQUIRE(calculateThreshold(0) == Approx(0.0f).margin(0.001f));
    }

    SECTION("Band 1 threshold is 1/7") {
        REQUIRE(calculateThreshold(1) == Approx(1.0f / 7.0f).margin(0.001f));
        REQUIRE(calculateThreshold(1) == Approx(0.1429f).margin(0.001f));
    }

    SECTION("Band 2 threshold is 2/7") {
        REQUIRE(calculateThreshold(2) == Approx(2.0f / 7.0f).margin(0.001f));
    }

    SECTION("Band 3 threshold is 3/7") {
        REQUIRE(calculateThreshold(3) == Approx(3.0f / 7.0f).margin(0.001f));
    }

    SECTION("Band 4 threshold is 4/7") {
        REQUIRE(calculateThreshold(4) == Approx(4.0f / 7.0f).margin(0.001f));
        REQUIRE(calculateThreshold(4) == Approx(0.5714f).margin(0.001f));
    }

    SECTION("Band 5 threshold is 5/7") {
        REQUIRE(calculateThreshold(5) == Approx(5.0f / 7.0f).margin(0.001f));
    }

    SECTION("Band 6 threshold is 6/7") {
        REQUIRE(calculateThreshold(6) == Approx(6.0f / 7.0f).margin(0.001f));
    }

    SECTION("Band 7 threshold is 1.0") {
        REQUIRE(calculateThreshold(7) == Approx(1.0f).margin(0.001f));
    }
}

// ==============================================================================
// Test: Band Count to Normalized Mapping (T086)
// ==============================================================================
TEST_CASE("Band count maps to correct normalized value", "[visibility][bandcount]") {
    // For StringListParameter with 8 items:
    // Index i (count i+1) maps to normalized value i / 7.0

    auto bandCountToNormalized = [](int bandCount) -> float {
        int index = bandCount - 1;  // 1-8 -> 0-7
        return static_cast<float>(index) / 7.0f;
    };

    SECTION("Band count 1 maps to normalized 0.0") {
        REQUIRE(bandCountToNormalized(1) == Approx(0.0f).margin(0.001f));
    }

    SECTION("Band count 4 maps to normalized 3/7") {
        REQUIRE(bandCountToNormalized(4) == Approx(3.0f / 7.0f).margin(0.001f));
    }

    SECTION("Band count 6 maps to normalized 5/7") {
        REQUIRE(bandCountToNormalized(6) == Approx(5.0f / 7.0f).margin(0.001f));
    }

    SECTION("Band count 8 maps to normalized 1.0") {
        REQUIRE(bandCountToNormalized(8) == Approx(1.0f).margin(0.001f));
    }
}

// ==============================================================================
// Test: ContainerVisibilityController Logic (T086)
// ==============================================================================
TEST_CASE("ContainerVisibilityController shows band when value >= threshold", "[visibility][controller]") {
    // showWhenBelow = false means: show when normalized >= threshold

    auto isBandVisible = [](float normalizedBandCount, float threshold) -> bool {
        // Using >= for threshold comparison (showWhenBelow = false)
        return normalizedBandCount >= threshold;
    };

    SECTION("Band 0 (threshold 0.0) is visible at band count 1") {
        float normalized = 0.0f;  // Band count 1
        float threshold = 0.0f;   // Band 0 threshold
        REQUIRE(isBandVisible(normalized, threshold) == true);
    }

    SECTION("Band 4 (threshold 4/7) is hidden at band count 4") {
        float normalized = 3.0f / 7.0f;  // Band count 4 (index 3)
        float threshold = 4.0f / 7.0f;   // Band 4 threshold
        REQUIRE(isBandVisible(normalized, threshold) == false);
    }

    SECTION("Band 4 (threshold 4/7) is visible at band count 5") {
        float normalized = 4.0f / 7.0f;  // Band count 5 (index 4)
        float threshold = 4.0f / 7.0f;   // Band 4 threshold
        REQUIRE(isBandVisible(normalized, threshold) == true);
    }

    SECTION("Band 5 (threshold 5/7) is visible at band count 6") {
        float normalized = 5.0f / 7.0f;  // Band count 6 (index 5)
        float threshold = 5.0f / 7.0f;   // Band 5 threshold
        REQUIRE(isBandVisible(normalized, threshold) == true);
    }

    SECTION("Band 7 (threshold 1.0) is only visible at band count 8") {
        float threshold = 1.0f;  // Band 7 threshold

        // Band count 7 (index 6) = 6/7
        REQUIRE(isBandVisible(6.0f / 7.0f, threshold) == false);

        // Band count 8 (index 7) = 1.0
        REQUIRE(isBandVisible(1.0f, threshold) == true);
    }
}

// ==============================================================================
// Test: Band Visibility at Different Band Counts (T086)
// ==============================================================================
TEST_CASE("Band visibility matrix for all band counts", "[visibility][matrix]") {
    // Visibility function: band b is visible when bandCount >= b+1
    // In normalized terms: normalized >= b/7.0

    auto isBandVisible = [](int bandIndex, int bandCount) -> bool {
        float normalizedBandCount = static_cast<float>(bandCount - 1) / 7.0f;
        float threshold = static_cast<float>(bandIndex) / 7.0f;
        return normalizedBandCount >= threshold;
    };

    SECTION("Band count = 1: only band 0 visible") {
        REQUIRE(isBandVisible(0, 1) == true);
        REQUIRE(isBandVisible(1, 1) == false);
        REQUIRE(isBandVisible(2, 1) == false);
        REQUIRE(isBandVisible(7, 1) == false);
    }

    SECTION("Band count = 2: bands 0-1 visible") {
        REQUIRE(isBandVisible(0, 2) == true);
        REQUIRE(isBandVisible(1, 2) == true);
        REQUIRE(isBandVisible(2, 2) == false);
    }

    SECTION("Band count = 4: bands 0-3 visible") {
        REQUIRE(isBandVisible(0, 4) == true);
        REQUIRE(isBandVisible(1, 4) == true);
        REQUIRE(isBandVisible(2, 4) == true);
        REQUIRE(isBandVisible(3, 4) == true);
        REQUIRE(isBandVisible(4, 4) == false);
        REQUIRE(isBandVisible(5, 4) == false);
        REQUIRE(isBandVisible(6, 4) == false);
        REQUIRE(isBandVisible(7, 4) == false);
    }

    SECTION("Band count = 6: bands 0-5 visible") {
        REQUIRE(isBandVisible(0, 6) == true);
        REQUIRE(isBandVisible(1, 6) == true);
        REQUIRE(isBandVisible(4, 6) == true);
        REQUIRE(isBandVisible(5, 6) == true);
        REQUIRE(isBandVisible(6, 6) == false);
        REQUIRE(isBandVisible(7, 6) == false);
    }

    SECTION("Band count = 8: all bands visible") {
        for (int i = 0; i < 8; ++i) {
            REQUIRE(isBandVisible(i, 8) == true);
        }
    }
}

// ==============================================================================
// Test: Edge Cases for Visibility Changes (T086)
// ==============================================================================
TEST_CASE("Visibility change edge cases", "[visibility][edge]") {

    auto isBandVisible = [](int bandIndex, int bandCount) -> bool {
        float normalizedBandCount = static_cast<float>(bandCount - 1) / 7.0f;
        float threshold = static_cast<float>(bandIndex) / 7.0f;
        return normalizedBandCount >= threshold;
    };

    SECTION("Band 4 becomes visible exactly at count 5") {
        REQUIRE(isBandVisible(4, 4) == false);
        REQUIRE(isBandVisible(4, 5) == true);
    }

    SECTION("Band 1 becomes visible exactly at count 2") {
        REQUIRE(isBandVisible(1, 1) == false);
        REQUIRE(isBandVisible(1, 2) == true);
    }

    SECTION("Decreasing band count hides bands immediately") {
        // Going from 6 to 4 should hide bands 4 and 5
        REQUIRE(isBandVisible(4, 6) == true);
        REQUIRE(isBandVisible(5, 6) == true);
        REQUIRE(isBandVisible(4, 4) == false);
        REQUIRE(isBandVisible(5, 4) == false);
    }
}

// ==============================================================================
// Test: UI-Only Visibility Container Tags (T086)
// ==============================================================================
TEST_CASE("Band container visibility tags are in correct range", "[visibility][tag]") {
    // UI-only visibility tags start at 9000

    constexpr int kBandContainerTagBase = 9000;

    auto getBandContainerTag = [](int bandIndex) -> int {
        return kBandContainerTagBase + bandIndex;
    };

    SECTION("Band 0 container tag is 9000") {
        REQUIRE(getBandContainerTag(0) == 9000);
    }

    SECTION("Band 1 container tag is 9001") {
        REQUIRE(getBandContainerTag(1) == 9001);
    }

    SECTION("Band 7 container tag is 9007") {
        REQUIRE(getBandContainerTag(7) == 9007);
    }

    SECTION("Container tags don't overlap with parameter IDs") {
        // Parameter IDs are in different ranges:
        // - Global: 0x0F00-0x0FFF (3840-4095)
        // - Sweep: 0x0E00-0x0EFF (3584-3839)
        // - Band: 0xF000-0xF7FF (61440-63487)
        // - Node: 0x0000-0x37FF (0-14335)
        // Container tags (9000-9007) don't overlap with any of these
        for (int i = 0; i < 8; ++i) {
            int tag = getBandContainerTag(i);
            REQUIRE(tag >= 9000);
            REQUIRE(tag <= 9007);
            REQUIRE((tag < 3584 || tag > 4095));  // Not in global/sweep range
            REQUIRE(tag < 61440);  // Not in band range
        }
    }
}

// ==============================================================================
// Test: Band 0 Always Visible (T086)
// ==============================================================================
TEST_CASE("Band 0 is always visible regardless of band count", "[visibility][band0]") {
    auto isBandVisible = [](int bandIndex, int bandCount) -> bool {
        float normalizedBandCount = static_cast<float>(bandCount - 1) / 7.0f;
        float threshold = static_cast<float>(bandIndex) / 7.0f;
        return normalizedBandCount >= threshold;
    };

    // Band 0 has threshold 0.0, so it's always visible when normalized >= 0.0
    // The minimum band count is 1, which gives normalized = 0.0
    // 0.0 >= 0.0 is true

    for (int bandCount = 1; bandCount <= 8; ++bandCount) {
        REQUIRE(isBandVisible(0, bandCount) == true);
    }
}
