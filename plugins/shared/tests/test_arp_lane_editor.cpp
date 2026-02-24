// ==============================================================================
// ArpLaneEditor Tests (079-layout-framework)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/arp_lane_editor.h"

using namespace Krate::Plugins;
using namespace VSTGUI;
using Catch::Approx;

// Helper: create a default 500x200 ArpLaneEditor at position (0,0)
static ArpLaneEditor makeArpLaneEditor(int numSteps = 16) {
    ArpLaneEditor editor(CRect(0, 0, 500, 200), nullptr, -1);
    editor.setNumSteps(numSteps);
    return editor;
}

// ==============================================================================
// Construction Tests (T006)
// ==============================================================================

TEST_CASE("ArpLaneEditor default laneType is kVelocity", "[arp_lane_editor][construction]") {
    auto editor = makeArpLaneEditor();
    REQUIRE(editor.getLaneType() == ArpLaneType::kVelocity);
}

TEST_CASE("ArpLaneEditor default isCollapsed returns false", "[arp_lane_editor][construction]") {
    auto editor = makeArpLaneEditor();
    REQUIRE_FALSE(editor.isCollapsed());
}

TEST_CASE("ArpLaneEditor default accent color is copper", "[arp_lane_editor][construction]") {
    auto editor = makeArpLaneEditor();
    CColor accent = editor.getAccentColor();
    REQUIRE(accent.red == 208);
    REQUIRE(accent.green == 132);
    REQUIRE(accent.blue == 92);
    REQUIRE(accent.alpha == 255);
}

// ==============================================================================
// setAccentColor / Color Derivation Tests (T007)
// ==============================================================================

TEST_CASE("setAccentColor derives normalColor_ via darken 0.6x", "[arp_lane_editor][color]") {
    auto editor = makeArpLaneEditor();
    editor.setAccentColor(CColor{208, 132, 92, 255});

    // normalColor_ = darkenColor(accent, 0.6f)
    // Expected: (208*0.6, 132*0.6, 92*0.6) = (124.8, 79.2, 55.2)
    // uint8_t truncation: (124, 79, 55) -- allow +/-1 for rounding
    CColor normal = editor.getBarColorNormal();
    REQUIRE(static_cast<int>(normal.red) == Approx(125).margin(1));
    REQUIRE(static_cast<int>(normal.green) == Approx(79).margin(1));
    REQUIRE(static_cast<int>(normal.blue) == Approx(55).margin(1));
    REQUIRE(normal.alpha == 255);
}

TEST_CASE("setAccentColor derives ghostColor_ via darken 0.35x", "[arp_lane_editor][color]") {
    auto editor = makeArpLaneEditor();
    editor.setAccentColor(CColor{208, 132, 92, 255});

    // ghostColor_ = darkenColor(accent, 0.35f)
    // Expected: (208*0.35, 132*0.35, 92*0.35) = (72.8, 46.2, 32.2)
    // uint8_t truncation: (72, 46, 32) -- allow +/-1 for rounding
    CColor ghost = editor.getBarColorGhost();
    REQUIRE(static_cast<int>(ghost.red) == Approx(73).margin(1));
    REQUIRE(static_cast<int>(ghost.green) == Approx(46).margin(1));
    REQUIRE(static_cast<int>(ghost.blue) == Approx(32).margin(1));
    REQUIRE(ghost.alpha == 255);
}

TEST_CASE("setAccentColor also sets barColorAccent on base class", "[arp_lane_editor][color]") {
    auto editor = makeArpLaneEditor();
    CColor copper{208, 132, 92, 255};
    editor.setAccentColor(copper);

    REQUIRE(editor.getBarColorAccent() == copper);
}

// ==============================================================================
// setDisplayRange Tests (T008)
// ==============================================================================

TEST_CASE("setDisplayRange for kVelocity sets correct labels", "[arp_lane_editor][display_range]") {
    auto editor = makeArpLaneEditor();
    editor.setLaneType(ArpLaneType::kVelocity);
    editor.setDisplayRange(0.0f, 1.0f, "1.0", "0.0");

    REQUIRE(editor.getTopLabel() == "1.0");
    REQUIRE(editor.getBottomLabel() == "0.0");
}

TEST_CASE("setDisplayRange for kGate sets correct labels", "[arp_lane_editor][display_range]") {
    auto editor = makeArpLaneEditor();
    editor.setLaneType(ArpLaneType::kGate);
    editor.setDisplayRange(0.0f, 2.0f, "200%", "0%");

    REQUIRE(editor.getTopLabel() == "200%");
    REQUIRE(editor.getBottomLabel() == "0%");
}

// ==============================================================================
// Collapse/Expand Tests (T009)
// ==============================================================================

TEST_CASE("getCollapsedHeight returns kHeaderHeight (16.0f)", "[arp_lane_editor][collapse]") {
    auto editor = makeArpLaneEditor();
    REQUIRE(editor.getCollapsedHeight() == Approx(ArpLaneEditor::kHeaderHeight).margin(0.01f));
}

TEST_CASE("getExpandedHeight returns view height", "[arp_lane_editor][collapse]") {
    // Editor is 200px tall, so expanded height should be 200.0f
    auto editor = makeArpLaneEditor();
    float expandedHeight = editor.getExpandedHeight();
    REQUIRE(expandedHeight == Approx(200.0f).margin(0.01f));
}

TEST_CASE("setCollapsed triggers collapseCallback", "[arp_lane_editor][collapse]") {
    auto editor = makeArpLaneEditor();
    bool callbackCalled = false;
    editor.setCollapseCallback([&callbackCalled]() {
        callbackCalled = true;
    });

    editor.setCollapsed(true);
    REQUIRE(callbackCalled);
    REQUIRE(editor.isCollapsed());

    callbackCalled = false;
    editor.setCollapsed(false);
    REQUIRE(callbackCalled);
    REQUIRE_FALSE(editor.isCollapsed());
}

// ==============================================================================
// barAreaTopOffset Inheritance Tests (T010)
// ==============================================================================

TEST_CASE("ArpLaneEditor constructor sets barAreaTopOffset to kHeaderHeight", "[arp_lane_editor][layout]") {
    auto editor = makeArpLaneEditor();

    // Create a plain StepPatternEditor for comparison
    StepPatternEditor plain(CRect(0, 0, 500, 200), nullptr, -1);
    plain.setNumSteps(16);

    CRect plainBarArea = plain.getBarArea();
    CRect arpBarArea = editor.getBarArea();

    // The ArpLaneEditor bar area top should be shifted down by kHeaderHeight (16px)
    float expectedShift = ArpLaneEditor::kHeaderHeight;
    REQUIRE(static_cast<float>(arpBarArea.top) ==
            Approx(static_cast<float>(plainBarArea.top) + expectedShift).margin(0.01f));
}

// ==============================================================================
// Length Parameter Binding Tests (T042)
// ==============================================================================

TEST_CASE("setLengthParamId stores and retrieves the param ID", "[arp_lane_editor][length]") {
    auto editor = makeArpLaneEditor();
    editor.setLengthParamId(3020);
    REQUIRE(editor.getLengthParamId() == 3020);
}

TEST_CASE("setLengthParamId default is zero", "[arp_lane_editor][length]") {
    auto editor = makeArpLaneEditor();
    REQUIRE(editor.getLengthParamId() == 0);
}

TEST_CASE("setNumSteps changes bar count and getNumSteps returns it", "[arp_lane_editor][length]") {
    auto editor = makeArpLaneEditor(16);
    REQUIRE(editor.getNumSteps() == 16);

    editor.setNumSteps(8);
    REQUIRE(editor.getNumSteps() == 8);
}

TEST_CASE("setNumSteps clamps to valid range [kMinSteps, kMaxSteps]", "[arp_lane_editor][length]") {
    auto editor = makeArpLaneEditor(16);

    editor.setNumSteps(1); // Below kMinSteps (2)
    REQUIRE(editor.getNumSteps() == StepPatternEditor::kMinSteps);

    editor.setNumSteps(64); // Above kMaxSteps (32)
    REQUIRE(editor.getNumSteps() == StepPatternEditor::kMaxSteps);
}

// ==============================================================================
// Miniature Preview Rendering Tests (T051)
// ==============================================================================

TEST_CASE("Collapsed ArpLaneEditor with high levels uses accent color via getColorForLevel",
          "[arp_lane_editor][collapse][preview]") {
    auto editor = makeArpLaneEditor(16);
    editor.setAccentColor(CColor{208, 132, 92, 255});

    // Set all 16 steps to 0.8 (at the accent threshold)
    for (int i = 0; i < 16; ++i) {
        editor.setStepLevel(i, 0.8f);
    }

    editor.setCollapsed(true);
    REQUIRE(editor.isCollapsed());

    // getColorForLevel(0.8f) should return the accent color (level >= 0.80f)
    CColor color = editor.getColorForLevel(0.8f);
    REQUIRE(color == editor.getBarColorAccent());
}

TEST_CASE("Collapsed ArpLaneEditor preserves step data for miniature preview",
          "[arp_lane_editor][collapse][preview]") {
    auto editor = makeArpLaneEditor(16);

    // Set specific step levels before collapsing
    for (int i = 0; i < 16; ++i) {
        editor.setStepLevel(i, 0.8f);
    }

    editor.setCollapsed(true);

    // Verify step data is still accessible after collapse (needed for miniature preview)
    for (int i = 0; i < 16; ++i) {
        REQUIRE(editor.getStepLevel(i) == Approx(0.8f).margin(0.001f));
    }
}

TEST_CASE("getColorForLevel returns correct color tiers for miniature preview",
          "[arp_lane_editor][collapse][preview]") {
    auto editor = makeArpLaneEditor(16);
    CColor accent{208, 132, 92, 255};
    editor.setAccentColor(accent);

    // Verify color tiers used in miniature preview rendering:
    // level >= 0.80 -> accent color
    CColor highColor = editor.getColorForLevel(0.8f);
    REQUIRE(highColor == editor.getBarColorAccent());

    // level >= 0.40 and < 0.80 -> normal color
    CColor midColor = editor.getColorForLevel(0.5f);
    REQUIRE(midColor == editor.getBarColorNormal());

    // level > 0 and < 0.40 -> ghost color
    CColor lowColor = editor.getColorForLevel(0.2f);
    REQUIRE(lowColor == editor.getBarColorGhost());
}
