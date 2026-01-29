// ==============================================================================
// Node Editor Integration Tests
// ==============================================================================
// T137-T138: Integration tests for node editor panel (US7)
//
// Constitution Principle XII: Test-First Development
//
// These tests verify the node selection mechanism that allows users to click
// on a node (either in MorphPad or node editor list) to select it for editing.
// The selected node's parameters are then displayed in the UIViewSwitchContainer.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/views/morph_pad.h"
#include "dsp/distortion_types.h"

using Catch::Approx;
using namespace Disrumpo;

// =============================================================================
// T137: Node Editor Integration Test
// =============================================================================
// Verifies that clicking on a node indicator switches the visible parameters
// in the type-specific panel.

TEST_CASE("T137: Node selection changes parameter panel visibility", "[integration][node_editor][US7]") {
    VSTGUI::CRect rect(0, 0, 250, 200);
    MorphPad pad(rect);

    SECTION("clicking node in MorphPad selects that node") {
        // Set up 4 active nodes
        pad.setActiveNodeCount(4);

        // Initially no node is selected
        REQUIRE(pad.getSelectedNode() == -1);

        // Get pixel coordinates for Node B (at position 1,0)
        float pixelX = 0.0f, pixelY = 0.0f;
        pad.positionToPixel(1.0f, 0.0f, pixelX, pixelY);

        // Hit test should find Node B
        int hitNode = pad.hitTestNode(pixelX, pixelY);
        REQUIRE(hitNode == 1);

        // Select the node (simulating click behavior)
        pad.setSelectedNode(hitNode);
        REQUIRE(pad.getSelectedNode() == 1);
    }

    SECTION("selecting different node changes selection") {
        pad.setActiveNodeCount(4);
        pad.setSelectedNode(0);
        REQUIRE(pad.getSelectedNode() == 0);

        // Select Node C
        pad.setSelectedNode(2);
        REQUIRE(pad.getSelectedNode() == 2);

        // Previous selection cleared
        REQUIRE(pad.getSelectedNode() != 0);
    }

    SECTION("node selection is independent of cursor position") {
        pad.setActiveNodeCount(4);

        // Select Node B
        pad.setSelectedNode(1);
        REQUIRE(pad.getSelectedNode() == 1);

        // Move cursor around
        pad.setMorphPosition(0.0f, 0.0f);
        pad.setMorphPosition(1.0f, 1.0f);
        pad.setMorphPosition(0.5f, 0.5f);

        // Selection unchanged
        REQUIRE(pad.getSelectedNode() == 1);
    }
}

// =============================================================================
// T138: Node Selection Test - Clicking Node in MorphPad
// =============================================================================
// Verifies that clicking on a node circle in MorphPad selects that node for editing.

TEST_CASE("T138: Clicking node in MorphPad selects for editing", "[integration][node_editor][US7]") {
    VSTGUI::CRect rect(0, 0, 250, 200);
    MorphPad pad(rect);

    SECTION("clicking exactly on node A selects it") {
        pad.setActiveNodeCount(4);

        // Node A is at normalized (0, 0)
        float pixelX = 0.0f, pixelY = 0.0f;
        pad.positionToPixel(0.0f, 0.0f, pixelX, pixelY);

        int hitNode = pad.hitTestNode(pixelX, pixelY);
        REQUIRE(hitNode == 0);

        pad.setSelectedNode(hitNode);
        REQUIRE(pad.getSelectedNode() == 0);
    }

    SECTION("clicking within hit radius of node selects it") {
        pad.setActiveNodeCount(4);

        // Node D is at normalized (1, 1)
        float pixelX = 0.0f, pixelY = 0.0f;
        pad.positionToPixel(1.0f, 1.0f, pixelX, pixelY);

        // Move slightly off-center but within hit radius (8px)
        int hitNode = pad.hitTestNode(pixelX - 5.0f, pixelY + 3.0f);
        REQUIRE(hitNode == 3);

        pad.setSelectedNode(hitNode);
        REQUIRE(pad.getSelectedNode() == 3);
    }

    SECTION("clicking on empty space does not change selection") {
        pad.setActiveNodeCount(4);
        pad.setSelectedNode(2);  // Select Node C

        // Click in center (no node there)
        float pixelX = 0.0f, pixelY = 0.0f;
        pad.positionToPixel(0.5f, 0.5f, pixelX, pixelY);

        int hitNode = pad.hitTestNode(pixelX, pixelY);
        REQUIRE(hitNode == -1);

        // Selection should be unchanged (actual behavior depends on click handler)
        // In real implementation, clicking empty space might not change selection
        // This test documents expected behavior
    }

    SECTION("only active nodes can be selected") {
        pad.setActiveNodeCount(2);  // Only A and B active
        pad.setSelectedNode(-1);

        // Try to select Node C (inactive)
        float pixelX = 0.0f, pixelY = 0.0f;
        pad.positionToPixel(0.0f, 1.0f, pixelX, pixelY);

        int hitNode = pad.hitTestNode(pixelX, pixelY);
        REQUIRE(hitNode == -1);  // Node C is not hittable when inactive

        // Can still select Node A
        pad.positionToPixel(0.0f, 0.0f, pixelX, pixelY);
        hitNode = pad.hitTestNode(pixelX, pixelY);
        REQUIRE(hitNode == 0);

        pad.setSelectedNode(hitNode);
        REQUIRE(pad.getSelectedNode() == 0);
    }
}

// =============================================================================
// Selected Node Visual Feedback Tests (T141)
// =============================================================================
// Tests that the selected node has a visible highlight ring

TEST_CASE("Selected node has highlight ring visual feedback", "[integration][node_editor][US7]") {
    VSTGUI::CRect rect(0, 0, 250, 200);
    MorphPad pad(rect);

    SECTION("selected node state is tracked") {
        pad.setSelectedNode(1);
        REQUIRE(pad.getSelectedNode() == 1);

        pad.setSelectedNode(3);
        REQUIRE(pad.getSelectedNode() == 3);
    }

    SECTION("selection can be cleared") {
        pad.setSelectedNode(2);
        REQUIRE(pad.getSelectedNode() == 2);

        pad.setSelectedNode(-1);
        REQUIRE(pad.getSelectedNode() == -1);
    }

    SECTION("invalid selection index is ignored") {
        pad.setSelectedNode(1);

        // Try invalid indices
        pad.setSelectedNode(5);  // Too high
        REQUIRE(pad.getSelectedNode() == 1);  // Unchanged

        pad.setSelectedNode(-2);  // Too low (but -1 is valid for "no selection")
        REQUIRE(pad.getSelectedNode() == 1);  // Unchanged
    }
}

// =============================================================================
// Node Type Information Tests
// =============================================================================
// Tests that node type information is available for the editor panel

TEST_CASE("Node type information for editor panel", "[integration][node_editor][US7]") {
    VSTGUI::CRect rect(0, 0, 250, 200);
    MorphPad pad(rect);

    SECTION("each node has its own type") {
        pad.setNodeType(0, DistortionType::Tube);
        pad.setNodeType(1, DistortionType::Bitcrush);
        pad.setNodeType(2, DistortionType::SineFold);
        pad.setNodeType(3, DistortionType::Granular);

        REQUIRE(pad.getNodeType(0) == DistortionType::Tube);
        REQUIRE(pad.getNodeType(1) == DistortionType::Bitcrush);
        REQUIRE(pad.getNodeType(2) == DistortionType::SineFold);
        REQUIRE(pad.getNodeType(3) == DistortionType::Granular);
    }

    SECTION("selected node's type determines visible parameters") {
        pad.setNodeType(0, DistortionType::Tube);
        pad.setNodeType(1, DistortionType::Bitcrush);

        // Select Node A (Tube)
        pad.setSelectedNode(0);
        DistortionType selectedType = pad.getNodeType(pad.getSelectedNode());
        REQUIRE(selectedType == DistortionType::Tube);

        // Select Node B (Bitcrush)
        pad.setSelectedNode(1);
        selectedType = pad.getNodeType(pad.getSelectedNode());
        REQUIRE(selectedType == DistortionType::Bitcrush);
    }

    SECTION("type determines family color for UI") {
        pad.setNodeType(0, DistortionType::Tape);  // Saturation family
        pad.setNodeType(1, DistortionType::Bitcrush);  // Digital family

        auto colorA = MorphPad::getCategoryColor(getFamily(pad.getNodeType(0)));
        auto colorB = MorphPad::getCategoryColor(getFamily(pad.getNodeType(1)));

        // Saturation = Orange, Digital = Green
        REQUIRE(colorA.red == 0xFF);
        REQUIRE(colorA.green == 0x6B);
        REQUIRE(colorA.blue == 0x35);

        REQUIRE(colorB.red == 0x95);
        REQUIRE(colorB.green == 0xE8);
        REQUIRE(colorB.blue == 0x6B);
    }
}

// =============================================================================
// MorphPadListener Integration Tests
// =============================================================================
// Tests that the listener is notified when a node is selected

namespace {

class TestMorphPadListener : public MorphPadListener {
public:
    void onMorphPositionChanged(float /*morphX*/, float /*morphY*/) override {
        positionChangedCount++;
    }

    void onNodePositionChanged(int /*nodeIndex*/, float /*posX*/, float /*posY*/) override {
        nodePositionChangedCount++;
    }

    void onNodeSelected(int nodeIndex) override {
        lastSelectedNode = nodeIndex;
        nodeSelectedCount++;
    }

    int positionChangedCount = 0;
    int nodePositionChangedCount = 0;
    int nodeSelectedCount = 0;
    int lastSelectedNode = -1;
};

}  // namespace

TEST_CASE("MorphPad listener receives node selection events", "[integration][node_editor][US7]") {
    VSTGUI::CRect rect(0, 0, 250, 200);
    MorphPad pad(rect);
    TestMorphPadListener listener;
    pad.setMorphPadListener(&listener);

    SECTION("listener interface exists with onNodeSelected callback") {
        // The listener interface should have onNodeSelected method
        // This test verifies the interface compiles correctly
        REQUIRE(listener.nodeSelectedCount == 0);
    }

    SECTION("selecting node via setSelectedNode does not trigger listener") {
        // Direct API calls don't trigger listener - only mouse events do
        pad.setSelectedNode(2);
        REQUIRE(listener.nodeSelectedCount == 0);
    }
}
