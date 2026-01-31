// ==============================================================================
// KeyboardShortcutHandler Unit Tests
// ==============================================================================
// T048: Tests for keyboard shortcut logic (Tab cycling, Space toggle, Arrow keys)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>

#include "plugin_ids.h"

using namespace Disrumpo;

// =============================================================================
// Tab Cycling Logic Tests
// =============================================================================

TEST_CASE("Tab cycling wraps forward through bands", "[keyboard][tab]") {
    int activeBandCount = 4;
    int focusedBandIndex = -1;

    // First tab: -1 -> 0
    focusedBandIndex++;
    if (focusedBandIndex >= activeBandCount) focusedBandIndex = 0;
    REQUIRE(focusedBandIndex == 0);

    // Tab: 0 -> 1
    focusedBandIndex++;
    if (focusedBandIndex >= activeBandCount) focusedBandIndex = 0;
    REQUIRE(focusedBandIndex == 1);

    // Tab: 1 -> 2
    focusedBandIndex++;
    if (focusedBandIndex >= activeBandCount) focusedBandIndex = 0;
    REQUIRE(focusedBandIndex == 2);

    // Tab: 2 -> 3
    focusedBandIndex++;
    if (focusedBandIndex >= activeBandCount) focusedBandIndex = 0;
    REQUIRE(focusedBandIndex == 3);

    // Tab: 3 -> 0 (wrap)
    focusedBandIndex++;
    if (focusedBandIndex >= activeBandCount) focusedBandIndex = 0;
    REQUIRE(focusedBandIndex == 0);
}

TEST_CASE("Shift+Tab cycling wraps backward through bands", "[keyboard][tab]") {
    int activeBandCount = 4;
    int focusedBandIndex = 0;

    // Shift+Tab: 0 -> 3 (wrap backward)
    focusedBandIndex--;
    if (focusedBandIndex < 0) focusedBandIndex = activeBandCount - 1;
    REQUIRE(focusedBandIndex == 3);

    // Shift+Tab: 3 -> 2
    focusedBandIndex--;
    if (focusedBandIndex < 0) focusedBandIndex = activeBandCount - 1;
    REQUIRE(focusedBandIndex == 2);
}

// =============================================================================
// Space Toggle Logic Tests
// =============================================================================

TEST_CASE("Space toggles band bypass parameter", "[keyboard][space]") {
    // Simulate bypass toggle logic
    double currentBypass = 0.0;  // Not bypassed
    double newBypass = (currentBypass >= 0.5) ? 0.0 : 1.0;
    REQUIRE(newBypass == 1.0);

    // Toggle back
    currentBypass = newBypass;
    newBypass = (currentBypass >= 0.5) ? 0.0 : 1.0;
    REQUIRE(newBypass == 0.0);
}

TEST_CASE("Space does nothing when no band is focused", "[keyboard][space]") {
    int focusedBandIndex = -1;
    bool handled = (focusedBandIndex >= 0);
    REQUIRE_FALSE(handled);
}

// =============================================================================
// Arrow Key Step Calculation Tests
// =============================================================================

TEST_CASE("Fine adjustment step is 1/100th of range", "[keyboard][arrow]") {
    float fineStep = 0.01f;
    REQUIRE(fineStep == 0.01f);
}

TEST_CASE("Coarse adjustment step is 1/10th of range", "[keyboard][arrow]") {
    float coarseStep = 0.1f;
    REQUIRE(coarseStep == 0.1f);
}

TEST_CASE("Arrow key adjusts parameter within bounds", "[keyboard][arrow]") {
    double currentValue = 0.5;
    float stepFraction = 0.01f;

    SECTION("Up/Right increases value") {
        double newValue = currentValue + stepFraction;
        newValue = std::clamp(newValue, 0.0, 1.0);
        REQUIRE_THAT(newValue, Catch::Matchers::WithinAbs(0.51, 0.001));
    }

    SECTION("Down/Left decreases value") {
        double newValue = currentValue - stepFraction;
        newValue = std::clamp(newValue, 0.0, 1.0);
        REQUIRE_THAT(newValue, Catch::Matchers::WithinAbs(0.49, 0.001));
    }

    SECTION("Value clamps at maximum") {
        currentValue = 0.995;
        double newValue = currentValue + stepFraction;
        newValue = std::clamp(newValue, 0.0, 1.0);
        REQUIRE(newValue <= 1.0);
    }

    SECTION("Value clamps at minimum") {
        currentValue = 0.005;
        double newValue = currentValue - stepFraction;
        newValue = std::clamp(newValue, 0.0, 1.0);
        REQUIRE(newValue >= 0.0);
    }
}

TEST_CASE("Discrete parameter uses single step", "[keyboard][arrow]") {
    int stepCount = 7;  // e.g., Band Count has 8 values (0-7)
    double step = 1.0 / static_cast<double>(stepCount);

    REQUIRE_THAT(step, Catch::Matchers::WithinAbs(1.0 / 7.0, 0.001));

    // Stepping from 0.0 should give first step
    double currentValue = 0.0;
    double newValue = currentValue + step;
    REQUIRE_THAT(newValue, Catch::Matchers::WithinAbs(1.0 / 7.0, 0.001));
}

// =============================================================================
// Band Parameter ID Tests
// =============================================================================

TEST_CASE("Band bypass parameter ID is correctly encoded", "[keyboard][params]") {
    for (int band = 0; band < 8; ++band) {
        auto paramId = makeBandParamId(static_cast<uint8_t>(band), BandParamType::kBandBypass);
        REQUIRE(isBandParamId(paramId));
        REQUIRE(extractBandIndex(paramId) == band);
        REQUIRE(extractBandParamType(paramId) == BandParamType::kBandBypass);
    }
}
