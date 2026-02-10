// ==============================================================================
// XYMorphPad Coordinate Conversion Tests (047-xy-morph-pad T014a, T014b)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/xy_morph_pad.h"

using namespace Krate::Plugins;
using namespace VSTGUI;
using Catch::Approx;

// Helper: create a default 250x160 pad (matching editor.uidesc dimensions)
static XYMorphPad makePad() {
    return XYMorphPad(CRect(0, 0, 250, 160), nullptr, -1);
}

// ==============================================================================
// T014a: pixelToPosition out-of-bounds clamping
// ==============================================================================

TEST_CASE("pixelToPosition clamps negative X to 0.0", "[xy_morph_pad][coord]") {
    auto pad = makePad();
    float normX = 0.0f;
    float normY = 0.0f;
    pad.pixelToPosition(-50.0f, 80.0f, normX, normY);

    REQUIRE(normX == 0.0f);
}

TEST_CASE("pixelToPosition clamps X beyond right edge to 1.0", "[xy_morph_pad][coord]") {
    auto pad = makePad();
    float normX = 0.0f;
    float normY = 0.0f;
    pad.pixelToPosition(300.0f, 80.0f, normX, normY);

    REQUIRE(normX == 1.0f);
}

TEST_CASE("pixelToPosition clamps negative Y to 1.0 (top)", "[xy_morph_pad][coord]") {
    auto pad = makePad();
    float normX = 0.0f;
    float normY = 0.0f;
    // Negative pixel Y = above the pad = normY should clamp to 1.0 (Y-inverted)
    pad.pixelToPosition(125.0f, -50.0f, normX, normY);

    REQUIRE(normY == 1.0f);
}

TEST_CASE("pixelToPosition clamps Y beyond bottom edge to 0.0", "[xy_morph_pad][coord]") {
    auto pad = makePad();
    float normX = 0.0f;
    float normY = 0.0f;
    // Large pixel Y = below the pad = normY should clamp to 0.0 (Y-inverted)
    pad.pixelToPosition(125.0f, 300.0f, normX, normY);

    REQUIRE(normY == 0.0f);
}

TEST_CASE("pixelToPosition clamps both axes for far out-of-bounds", "[xy_morph_pad][coord]") {
    auto pad = makePad();
    float normX = 0.5f;
    float normY = 0.5f;
    // Way off bottom-right
    pad.pixelToPosition(1000.0f, 1000.0f, normX, normY);

    REQUIRE(normX == 1.0f);
    REQUIRE(normY == 0.0f);
}

// ==============================================================================
// T014b: Coordinate round-trip within 0.01 tolerance (SC-006)
// ==============================================================================

TEST_CASE("Coordinate round-trip at center (0.5, 0.5)", "[xy_morph_pad][coord][roundtrip]") {
    auto pad = makePad();
    float pixelX = 0.0f;
    float pixelY = 0.0f;
    pad.positionToPixel(0.5f, 0.5f, pixelX, pixelY);

    float normX = 0.0f;
    float normY = 0.0f;
    pad.pixelToPosition(pixelX, pixelY, normX, normY);

    REQUIRE(normX == Approx(0.5f).margin(0.01f));
    REQUIRE(normY == Approx(0.5f).margin(0.01f));
}

TEST_CASE("Coordinate round-trip at origin (0.0, 0.0)", "[xy_morph_pad][coord][roundtrip]") {
    auto pad = makePad();
    float pixelX = 0.0f;
    float pixelY = 0.0f;
    pad.positionToPixel(0.0f, 0.0f, pixelX, pixelY);

    float normX = 0.0f;
    float normY = 0.0f;
    pad.pixelToPosition(pixelX, pixelY, normX, normY);

    REQUIRE(normX == Approx(0.0f).margin(0.01f));
    REQUIRE(normY == Approx(0.0f).margin(0.01f));
}

TEST_CASE("Coordinate round-trip at (1.0, 1.0)", "[xy_morph_pad][coord][roundtrip]") {
    auto pad = makePad();
    float pixelX = 0.0f;
    float pixelY = 0.0f;
    pad.positionToPixel(1.0f, 1.0f, pixelX, pixelY);

    float normX = 0.0f;
    float normY = 0.0f;
    pad.pixelToPosition(pixelX, pixelY, normX, normY);

    REQUIRE(normX == Approx(1.0f).margin(0.01f));
    REQUIRE(normY == Approx(1.0f).margin(0.01f));
}

TEST_CASE("Coordinate round-trip at arbitrary position (0.3, 0.7)", "[xy_morph_pad][coord][roundtrip]") {
    auto pad = makePad();
    float pixelX = 0.0f;
    float pixelY = 0.0f;
    pad.positionToPixel(0.3f, 0.7f, pixelX, pixelY);

    float normX = 0.0f;
    float normY = 0.0f;
    pad.pixelToPosition(pixelX, pixelY, normX, normY);

    REQUIRE(normX == Approx(0.3f).margin(0.01f));
    REQUIRE(normY == Approx(0.7f).margin(0.01f));
}

TEST_CASE("Coordinate round-trip on large pad (500x400)", "[xy_morph_pad][coord][roundtrip]") {
    XYMorphPad pad(CRect(0, 0, 500, 400), nullptr, -1);
    float pixelX = 0.0f;
    float pixelY = 0.0f;
    pad.positionToPixel(0.75f, 0.25f, pixelX, pixelY);

    float normX = 0.0f;
    float normY = 0.0f;
    pad.pixelToPosition(pixelX, pixelY, normX, normY);

    REQUIRE(normX == Approx(0.75f).margin(0.01f));
    REQUIRE(normY == Approx(0.25f).margin(0.01f));
}

TEST_CASE("Coordinate round-trip on minimum size pad (80x80)", "[xy_morph_pad][coord][roundtrip]") {
    XYMorphPad pad(CRect(0, 0, 80, 80), nullptr, -1);
    float pixelX = 0.0f;
    float pixelY = 0.0f;
    pad.positionToPixel(0.5f, 0.5f, pixelX, pixelY);

    float normX = 0.0f;
    float normY = 0.0f;
    pad.pixelToPosition(pixelX, pixelY, normX, normY);

    REQUIRE(normX == Approx(0.5f).margin(0.01f));
    REQUIRE(normY == Approx(0.5f).margin(0.01f));
}

TEST_CASE("Coordinate round-trip with non-zero origin", "[xy_morph_pad][coord][roundtrip]") {
    // Pad positioned at (100, 50) to (350, 210) — matching real layout offset
    XYMorphPad pad(CRect(100, 50, 350, 210), nullptr, -1);
    float pixelX = 0.0f;
    float pixelY = 0.0f;
    pad.positionToPixel(0.6f, 0.4f, pixelX, pixelY);

    float normX = 0.0f;
    float normY = 0.0f;
    pad.pixelToPosition(pixelX, pixelY, normX, normY);

    REQUIRE(normX == Approx(0.6f).margin(0.01f));
    REQUIRE(normY == Approx(0.4f).margin(0.01f));
}
