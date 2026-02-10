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

// ==============================================================================
// bilinearColor Tests (047-xy-morph-pad T073, T073a, T073b)
// ==============================================================================

TEST_CASE("bilinearColor returns bottom-left at (0,0)", "[color_utils][bilinear]") {
    CColor bl{48, 84, 120, 255};
    CColor br{132, 102, 36, 255};
    CColor tl{80, 140, 200, 255};
    CColor tr{220, 170, 60, 255};

    CColor result = bilinearColor(bl, br, tl, tr, 0.0f, 0.0f);

    REQUIRE(result.red == 48);
    REQUIRE(result.green == 84);
    REQUIRE(result.blue == 120);
    REQUIRE(result.alpha == 255);
}

TEST_CASE("bilinearColor returns top-right at (1,1)", "[color_utils][bilinear]") {
    CColor bl{48, 84, 120, 255};
    CColor br{132, 102, 36, 255};
    CColor tl{80, 140, 200, 255};
    CColor tr{220, 170, 60, 255};

    CColor result = bilinearColor(bl, br, tl, tr, 1.0f, 1.0f);

    REQUIRE(result.red == 220);
    REQUIRE(result.green == 170);
    REQUIRE(result.blue == 60);
    REQUIRE(result.alpha == 255);
}

TEST_CASE("bilinearColor returns bottom-right at (1,0)", "[color_utils][bilinear]") {
    CColor bl{48, 84, 120, 255};
    CColor br{132, 102, 36, 255};
    CColor tl{80, 140, 200, 255};
    CColor tr{220, 170, 60, 255};

    CColor result = bilinearColor(bl, br, tl, tr, 1.0f, 0.0f);

    REQUIRE(result.red == 132);
    REQUIRE(result.green == 102);
    REQUIRE(result.blue == 36);
    REQUIRE(result.alpha == 255);
}

TEST_CASE("bilinearColor returns top-left at (0,1)", "[color_utils][bilinear]") {
    CColor bl{48, 84, 120, 255};
    CColor br{132, 102, 36, 255};
    CColor tl{80, 140, 200, 255};
    CColor tr{220, 170, 60, 255};

    CColor result = bilinearColor(bl, br, tl, tr, 0.0f, 1.0f);

    REQUIRE(result.red == 80);
    REQUIRE(result.green == 140);
    REQUIRE(result.blue == 200);
    REQUIRE(result.alpha == 255);
}

TEST_CASE("bilinearColor returns center blend at (0.5,0.5)", "[color_utils][bilinear]") {
    CColor bl{0, 0, 0, 255};
    CColor br{200, 0, 0, 255};
    CColor tl{0, 200, 0, 255};
    CColor tr{200, 200, 0, 255};

    CColor result = bilinearColor(bl, br, tl, tr, 0.5f, 0.5f);

    // At center: bottom lerp = (100, 0, 0), top lerp = (100, 200, 0)
    // vertical lerp at 0.5 = (100, 100, 0)
    REQUIRE(result.red == 100);
    REQUIRE(result.green == 100);
    REQUIRE(result.blue == 0);
    REQUIRE(result.alpha == 255);
}

TEST_CASE("bilinearColor preserves alpha interpolation", "[color_utils][bilinear]") {
    CColor bl{100, 100, 100, 0};
    CColor br{100, 100, 100, 0};
    CColor tl{100, 100, 100, 200};
    CColor tr{100, 100, 100, 200};

    CColor result = bilinearColor(bl, br, tl, tr, 0.5f, 0.5f);

    REQUIRE(result.alpha == 100);
}
