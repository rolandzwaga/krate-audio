// ==============================================================================
// Color Utility Tests (046-step-pattern-editor)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ui/color_utils.h"

using namespace Krate::Plugins;
using namespace VSTGUI;

// ==============================================================================
// lerpColor Tests
// ==============================================================================

TEST_CASE("lerpColor returns start color at t=0", "[color_utils]") {
    CColor a{100, 150, 200, 255};
    CColor b{200, 50, 100, 128};
    CColor result = lerpColor(a, b, 0.0f);

    REQUIRE(result.red == 100);
    REQUIRE(result.green == 150);
    REQUIRE(result.blue == 200);
    REQUIRE(result.alpha == 255);
}

TEST_CASE("lerpColor returns end color at t=1", "[color_utils]") {
    CColor a{100, 150, 200, 255};
    CColor b{200, 50, 100, 128};
    CColor result = lerpColor(a, b, 1.0f);

    REQUIRE(result.red == 200);
    REQUIRE(result.green == 50);
    REQUIRE(result.blue == 100);
    REQUIRE(result.alpha == 128);
}

TEST_CASE("lerpColor interpolates at t=0.5", "[color_utils]") {
    CColor a{0, 0, 0, 0};
    CColor b{200, 100, 50, 200};
    CColor result = lerpColor(a, b, 0.5f);

    REQUIRE(result.red == 100);
    REQUIRE(result.green == 50);
    REQUIRE(result.blue == 25);
    REQUIRE(result.alpha == 100);
}

TEST_CASE("lerpColor handles same color", "[color_utils]") {
    CColor c{128, 128, 128, 255};
    CColor result = lerpColor(c, c, 0.5f);

    REQUIRE(result.red == 128);
    REQUIRE(result.green == 128);
    REQUIRE(result.blue == 128);
    REQUIRE(result.alpha == 255);
}

// ==============================================================================
// darkenColor Tests
// ==============================================================================

TEST_CASE("darkenColor returns black at factor=0", "[color_utils]") {
    CColor c{200, 150, 100, 255};
    CColor result = darkenColor(c, 0.0f);

    REQUIRE(result.red == 0);
    REQUIRE(result.green == 0);
    REQUIRE(result.blue == 0);
    REQUIRE(result.alpha == 255); // Alpha unchanged
}

TEST_CASE("darkenColor returns same color at factor=1", "[color_utils]") {
    CColor c{200, 150, 100, 255};
    CColor result = darkenColor(c, 1.0f);

    REQUIRE(result.red == 200);
    REQUIRE(result.green == 150);
    REQUIRE(result.blue == 100);
    REQUIRE(result.alpha == 255);
}

TEST_CASE("darkenColor halves at factor=0.5", "[color_utils]") {
    CColor c{200, 100, 50, 128};
    CColor result = darkenColor(c, 0.5f);

    REQUIRE(result.red == 100);
    REQUIRE(result.green == 50);
    REQUIRE(result.blue == 25);
    REQUIRE(result.alpha == 128); // Alpha unchanged
}

TEST_CASE("darkenColor preserves alpha", "[color_utils]") {
    CColor c{200, 150, 100, 42};
    CColor result = darkenColor(c, 0.3f);

    REQUIRE(result.alpha == 42);
}

// ==============================================================================
// brightenColor Tests
// ==============================================================================

TEST_CASE("brightenColor returns same color at factor=1", "[color_utils]") {
    CColor c{100, 80, 60, 255};
    CColor result = brightenColor(c, 1.0f);

    REQUIRE(result.red == 100);
    REQUIRE(result.green == 80);
    REQUIRE(result.blue == 60);
    REQUIRE(result.alpha == 255);
}

TEST_CASE("brightenColor doubles at factor=2", "[color_utils]") {
    CColor c{50, 40, 30, 255};
    CColor result = brightenColor(c, 2.0f);

    REQUIRE(result.red == 100);
    REQUIRE(result.green == 80);
    REQUIRE(result.blue == 60);
    REQUIRE(result.alpha == 255);
}

TEST_CASE("brightenColor clamps to 255", "[color_utils]") {
    CColor c{200, 200, 200, 255};
    CColor result = brightenColor(c, 2.0f);

    REQUIRE(result.red == 255);
    REQUIRE(result.green == 255);
    REQUIRE(result.blue == 255);
    REQUIRE(result.alpha == 255);
}

TEST_CASE("brightenColor preserves alpha", "[color_utils]") {
    CColor c{100, 80, 60, 42};
    CColor result = brightenColor(c, 1.5f);

    REQUIRE(result.alpha == 42);
}
