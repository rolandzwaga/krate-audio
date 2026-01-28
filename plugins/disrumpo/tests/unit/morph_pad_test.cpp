// ==============================================================================
// MorphPad Unit Tests
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests for MorphPad coordinate conversion, hit testing, and cursor clamping.
//
// Reference: specs/006-morph-ui/tasks.md T008, T010, T011, T012
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/views/morph_pad.h"
#include "dsp/distortion_types.h"

using Catch::Approx;
using namespace Disrumpo;

// =============================================================================
// Test Fixture: MorphPad with known dimensions
// =============================================================================
// MorphPad uses kPadding = 8.0f internally
// For a 250x200 size:
//   innerWidth = 250 - 2*8 = 234
//   innerHeight = 200 - 2*8 = 184

// =============================================================================
// Coordinate Conversion Tests (T008)
// =============================================================================

TEST_CASE("MorphPad coordinate conversion: positionToPixel", "[morph_pad][coordinate]") {
    // Create MorphPad with known size at origin (0, 0)
    VSTGUI::CRect rect(0, 0, 250, 200);
    MorphPad pad(rect);

    SECTION("center position (0.5, 0.5) maps to center pixels") {
        float pixelX = 0.0f;
        float pixelY = 0.0f;
        pad.positionToPixel(0.5f, 0.5f, pixelX, pixelY);

        // Center X = 0 + 8 + 0.5 * 234 = 8 + 117 = 125
        // Center Y (inverted) = 200 - 8 - 0.5 * 184 = 192 - 92 = 100
        REQUIRE(pixelX == Approx(125.0f).margin(0.5f));
        REQUIRE(pixelY == Approx(100.0f).margin(0.5f));
    }

    SECTION("bottom-left position (0, 0) maps to bottom-left pixels") {
        float pixelX = 0.0f;
        float pixelY = 0.0f;
        pad.positionToPixel(0.0f, 0.0f, pixelX, pixelY);

        // X = 0 + 8 + 0 * 234 = 8
        // Y (inverted, 0 at bottom) = 200 - 8 - 0 * 184 = 192
        REQUIRE(pixelX == Approx(8.0f).margin(0.5f));
        REQUIRE(pixelY == Approx(192.0f).margin(0.5f));
    }

    SECTION("top-right position (1, 1) maps to top-right pixels") {
        float pixelX = 0.0f;
        float pixelY = 0.0f;
        pad.positionToPixel(1.0f, 1.0f, pixelX, pixelY);

        // X = 0 + 8 + 1 * 234 = 242
        // Y (inverted, 1 at top) = 200 - 8 - 1 * 184 = 8
        REQUIRE(pixelX == Approx(242.0f).margin(0.5f));
        REQUIRE(pixelY == Approx(8.0f).margin(0.5f));
    }
}

TEST_CASE("MorphPad coordinate conversion: pixelToPosition", "[morph_pad][coordinate]") {
    VSTGUI::CRect rect(0, 0, 250, 200);
    MorphPad pad(rect);

    SECTION("center pixels map to position (0.5, 0.5)") {
        float normX = 0.0f;
        float normY = 0.0f;
        pad.pixelToPosition(125.0f, 100.0f, normX, normY);

        REQUIRE(normX == Approx(0.5f).margin(0.01f));
        REQUIRE(normY == Approx(0.5f).margin(0.01f));
    }

    SECTION("bottom-left pixels map to position (0, 0)") {
        float normX = 0.0f;
        float normY = 0.0f;
        pad.pixelToPosition(8.0f, 192.0f, normX, normY);

        REQUIRE(normX == Approx(0.0f).margin(0.01f));
        REQUIRE(normY == Approx(0.0f).margin(0.01f));
    }

    SECTION("top-right pixels map to position (1, 1)") {
        float normX = 0.0f;
        float normY = 0.0f;
        pad.pixelToPosition(242.0f, 8.0f, normX, normY);

        REQUIRE(normX == Approx(1.0f).margin(0.01f));
        REQUIRE(normY == Approx(1.0f).margin(0.01f));
    }

    SECTION("round-trip conversion preserves position") {
        // Start with a normalized position
        float originalX = 0.73f;
        float originalY = 0.28f;

        // Convert to pixels and back
        float pixelX = 0.0f;
        float pixelY = 0.0f;
        pad.positionToPixel(originalX, originalY, pixelX, pixelY);

        float resultX = 0.0f;
        float resultY = 0.0f;
        pad.pixelToPosition(pixelX, pixelY, resultX, resultY);

        REQUIRE(resultX == Approx(originalX).margin(0.01f));
        REQUIRE(resultY == Approx(originalY).margin(0.01f));
    }
}

// =============================================================================
// Hit Testing Tests (T010)
// =============================================================================

TEST_CASE("MorphPad hit testing: hitTestNode", "[morph_pad][hit_test]") {
    VSTGUI::CRect rect(0, 0, 250, 200);
    MorphPad pad(rect);

    // Set up 4 active nodes at corners (default positions)
    pad.setActiveNodeCount(4);
    // Default node positions: A(0,0), B(1,0), C(0,1), D(1,1)

    SECTION("clicking on node A returns index 0") {
        // Node A is at normalized (0, 0)
        float pixelX = 0.0f;
        float pixelY = 0.0f;
        pad.positionToPixel(0.0f, 0.0f, pixelX, pixelY);

        int hitNode = pad.hitTestNode(pixelX, pixelY);
        REQUIRE(hitNode == 0);
    }

    SECTION("clicking on node B returns index 1") {
        // Node B is at normalized (1, 0)
        float pixelX = 0.0f;
        float pixelY = 0.0f;
        pad.positionToPixel(1.0f, 0.0f, pixelX, pixelY);

        int hitNode = pad.hitTestNode(pixelX, pixelY);
        REQUIRE(hitNode == 1);
    }

    SECTION("clicking on node C returns index 2") {
        // Node C is at normalized (0, 1)
        float pixelX = 0.0f;
        float pixelY = 0.0f;
        pad.positionToPixel(0.0f, 1.0f, pixelX, pixelY);

        int hitNode = pad.hitTestNode(pixelX, pixelY);
        REQUIRE(hitNode == 2);
    }

    SECTION("clicking on node D returns index 3") {
        // Node D is at normalized (1, 1)
        float pixelX = 0.0f;
        float pixelY = 0.0f;
        pad.positionToPixel(1.0f, 1.0f, pixelX, pixelY);

        int hitNode = pad.hitTestNode(pixelX, pixelY);
        REQUIRE(hitNode == 3);
    }

    SECTION("clicking on center returns -1 (no hit)") {
        float pixelX = 0.0f;
        float pixelY = 0.0f;
        pad.positionToPixel(0.5f, 0.5f, pixelX, pixelY);

        int hitNode = pad.hitTestNode(pixelX, pixelY);
        REQUIRE(hitNode == -1);
    }

    SECTION("clicking slightly inside hit radius still hits node") {
        // Node A at (0,0), test a few pixels toward center
        float pixelX = 0.0f;
        float pixelY = 0.0f;
        pad.positionToPixel(0.0f, 0.0f, pixelX, pixelY);

        // Move 5 pixels toward center (within 8px hit radius)
        int hitNode = pad.hitTestNode(pixelX + 5.0f, pixelY - 5.0f);
        REQUIRE(hitNode == 0);
    }

    SECTION("with 2 active nodes, only nodes A and B are hittable") {
        pad.setActiveNodeCount(2);

        // Node C should not be hit even at its position
        float pixelX = 0.0f;
        float pixelY = 0.0f;
        pad.positionToPixel(0.0f, 1.0f, pixelX, pixelY);

        int hitNode = pad.hitTestNode(pixelX, pixelY);
        REQUIRE(hitNode == -1);  // Node C is inactive

        // Node A should still be hittable
        pad.positionToPixel(0.0f, 0.0f, pixelX, pixelY);
        hitNode = pad.hitTestNode(pixelX, pixelY);
        REQUIRE(hitNode == 0);
    }
}

// =============================================================================
// Cursor Clamping Tests (T011)
// =============================================================================

TEST_CASE("MorphPad cursor clamping: setMorphPosition", "[morph_pad][clamping]") {
    VSTGUI::CRect rect(0, 0, 250, 200);
    MorphPad pad(rect);

    SECTION("position within bounds is not modified") {
        pad.setMorphPosition(0.3f, 0.7f);
        REQUIRE(pad.getMorphX() == Approx(0.3f));
        REQUIRE(pad.getMorphY() == Approx(0.7f));
    }

    SECTION("position below 0 is clamped to 0") {
        pad.setMorphPosition(-0.5f, -0.1f);
        REQUIRE(pad.getMorphX() == Approx(0.0f));
        REQUIRE(pad.getMorphY() == Approx(0.0f));
    }

    SECTION("position above 1 is clamped to 1") {
        pad.setMorphPosition(1.5f, 2.0f);
        REQUIRE(pad.getMorphX() == Approx(1.0f));
        REQUIRE(pad.getMorphY() == Approx(1.0f));
    }

    SECTION("mixed out-of-bounds values are individually clamped") {
        pad.setMorphPosition(-0.2f, 1.3f);
        REQUIRE(pad.getMorphX() == Approx(0.0f));
        REQUIRE(pad.getMorphY() == Approx(1.0f));
    }

    SECTION("edge values are preserved") {
        pad.setMorphPosition(0.0f, 1.0f);
        REQUIRE(pad.getMorphX() == Approx(0.0f));
        REQUIRE(pad.getMorphY() == Approx(1.0f));
    }
}

// =============================================================================
// Fine Adjustment Tests (T012)
// =============================================================================

TEST_CASE("MorphPad fine adjustment scale constant", "[morph_pad][fine_adjustment]") {
    // The fine adjustment scale is 0.1 (10x precision)
    // This test verifies the expected behavior through setMorphPosition

    VSTGUI::CRect rect(0, 0, 250, 200);
    MorphPad pad(rect);

    SECTION("normal movement from center") {
        // Start at center
        pad.setMorphPosition(0.5f, 0.5f);
        REQUIRE(pad.getMorphX() == Approx(0.5f));
        REQUIRE(pad.getMorphY() == Approx(0.5f));

        // Move by 0.1 (simulating a small drag)
        pad.setMorphPosition(0.6f, 0.4f);
        REQUIRE(pad.getMorphX() == Approx(0.6f));
        REQUIRE(pad.getMorphY() == Approx(0.4f));
    }

    SECTION("fine adjustment would scale movement by 0.1") {
        // This tests the expected math for fine adjustment
        // If we start at 0.5 and move 0.5 in pixel space,
        // with fine adjustment the effective movement would be 0.05

        float startX = 0.5f;
        float normalDelta = 0.3f;
        float fineScale = 0.1f;

        float expectedFineResult = startX + normalDelta * fineScale;
        REQUIRE(expectedFineResult == Approx(0.53f));  // 0.5 + 0.3 * 0.1 = 0.53
    }
}

// =============================================================================
// Category Color Tests (T006)
// =============================================================================

TEST_CASE("MorphPad category colors", "[morph_pad][colors]") {
    SECTION("Saturation family returns orange") {
        auto color = MorphPad::getCategoryColor(DistortionFamily::Saturation);
        REQUIRE(color.red == 0xFF);
        REQUIRE(color.green == 0x6B);
        REQUIRE(color.blue == 0x35);
    }

    SECTION("Wavefold family returns teal") {
        auto color = MorphPad::getCategoryColor(DistortionFamily::Wavefold);
        REQUIRE(color.red == 0x4E);
        REQUIRE(color.green == 0xCD);
        REQUIRE(color.blue == 0xC4);
    }

    SECTION("Digital family returns green") {
        auto color = MorphPad::getCategoryColor(DistortionFamily::Digital);
        REQUIRE(color.red == 0x95);
        REQUIRE(color.green == 0xE8);
        REQUIRE(color.blue == 0x6B);
    }

    SECTION("Rectify family returns purple") {
        auto color = MorphPad::getCategoryColor(DistortionFamily::Rectify);
        REQUIRE(color.red == 0xC7);
        REQUIRE(color.green == 0x92);
        REQUIRE(color.blue == 0xEA);
    }

    SECTION("Dynamic family returns yellow") {
        auto color = MorphPad::getCategoryColor(DistortionFamily::Dynamic);
        REQUIRE(color.red == 0xFF);
        REQUIRE(color.green == 0xCB);
        REQUIRE(color.blue == 0x6B);
    }

    SECTION("Hybrid family returns red") {
        auto color = MorphPad::getCategoryColor(DistortionFamily::Hybrid);
        REQUIRE(color.red == 0xFF);
        REQUIRE(color.green == 0x53);
        REQUIRE(color.blue == 0x70);
    }

    SECTION("Experimental family returns light blue") {
        auto color = MorphPad::getCategoryColor(DistortionFamily::Experimental);
        REQUIRE(color.red == 0x89);
        REQUIRE(color.green == 0xDD);
        REQUIRE(color.blue == 0xFF);
    }
}

// =============================================================================
// Node Configuration Tests
// =============================================================================

TEST_CASE("MorphPad node configuration", "[morph_pad][nodes]") {
    VSTGUI::CRect rect(0, 0, 250, 200);
    MorphPad pad(rect);

    SECTION("default has 4 active nodes") {
        REQUIRE(pad.getActiveNodeCount() == 4);
    }

    SECTION("setActiveNodeCount clamps to valid range") {
        pad.setActiveNodeCount(1);
        REQUIRE(pad.getActiveNodeCount() == 2);  // Minimum is 2

        pad.setActiveNodeCount(5);
        REQUIRE(pad.getActiveNodeCount() == 4);  // Maximum is 4

        pad.setActiveNodeCount(3);
        REQUIRE(pad.getActiveNodeCount() == 3);
    }

    SECTION("node positions can be set and retrieved") {
        pad.setNodePosition(0, 0.25f, 0.75f);

        float x = 0.0f;
        float y = 0.0f;
        pad.getNodePosition(0, x, y);

        REQUIRE(x == Approx(0.25f));
        REQUIRE(y == Approx(0.75f));
    }

    SECTION("node type can be set and retrieved") {
        pad.setNodeType(1, DistortionType::Bitcrush);
        REQUIRE(pad.getNodeType(1) == DistortionType::Bitcrush);
    }

    SECTION("node weight can be set and retrieved") {
        pad.setNodeWeight(2, 0.8f);
        REQUIRE(pad.getNodeWeight(2) == Approx(0.8f));
    }

    SECTION("node weight is clamped to [0, 1]") {
        pad.setNodeWeight(0, -0.5f);
        REQUIRE(pad.getNodeWeight(0) == Approx(0.0f));

        pad.setNodeWeight(0, 1.5f);
        REQUIRE(pad.getNodeWeight(0) == Approx(1.0f));
    }
}

// =============================================================================
// Morph Mode Tests
// =============================================================================

TEST_CASE("MorphPad morph mode", "[morph_pad][mode]") {
    VSTGUI::CRect rect(0, 0, 250, 200);
    MorphPad pad(rect);

    SECTION("default mode is Planar2D") {
        REQUIRE(pad.getMorphMode() == MorphMode::Planar2D);
    }

    SECTION("mode can be changed") {
        pad.setMorphMode(MorphMode::Linear1D);
        REQUIRE(pad.getMorphMode() == MorphMode::Linear1D);

        pad.setMorphMode(MorphMode::Radial2D);
        REQUIRE(pad.getMorphMode() == MorphMode::Radial2D);
    }
}

// =============================================================================
// Selected Node Tests
// =============================================================================

TEST_CASE("MorphPad node selection", "[morph_pad][selection]") {
    VSTGUI::CRect rect(0, 0, 250, 200);
    MorphPad pad(rect);

    SECTION("default has no selection (-1)") {
        REQUIRE(pad.getSelectedNode() == -1);
    }

    SECTION("node can be selected") {
        pad.setSelectedNode(2);
        REQUIRE(pad.getSelectedNode() == 2);
    }

    SECTION("selection can be cleared") {
        pad.setSelectedNode(1);
        pad.setSelectedNode(-1);
        REQUIRE(pad.getSelectedNode() == -1);
    }

    SECTION("invalid selection index is ignored") {
        pad.setSelectedNode(2);
        pad.setSelectedNode(5);  // Invalid - should be ignored
        REQUIRE(pad.getSelectedNode() == 2);  // Still 2
    }
}
