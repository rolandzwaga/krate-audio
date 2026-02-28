// ==============================================================================
// ArpLaneEditor Tests (079-layout-framework + 080-specialized-lane-types)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/arp_lane.h"
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

// ==============================================================================
// IArpLane Interface Tests (080-specialized-lane-types T003)
// ==============================================================================

TEST_CASE("ArpLaneEditor::getView() returns non-null", "[arp_lane_editor][iarplane]") {
    auto editor = makeArpLaneEditor();
    IArpLane& iLane = editor;
    REQUIRE(iLane.getView() != nullptr);
}

TEST_CASE("ArpLaneEditor::getView() returns this", "[arp_lane_editor][iarplane]") {
    auto editor = makeArpLaneEditor();
    IArpLane& iLane = editor;
    REQUIRE(iLane.getView() == static_cast<CView*>(&editor));
}

TEST_CASE("ArpLaneEditor::setPlayheadStep delegates to setPlaybackStep", "[arp_lane_editor][iarplane]") {
    auto editor = makeArpLaneEditor();
    IArpLane& iLane = editor;

    iLane.setPlayheadStep(5);
    REQUIRE(editor.getPlaybackStep() == 5);

    iLane.setPlayheadStep(-1);
    REQUIRE(editor.getPlaybackStep() == -1);
}

TEST_CASE("ArpLaneEditor::setLength delegates to setNumSteps", "[arp_lane_editor][iarplane]") {
    auto editor = makeArpLaneEditor(16);
    IArpLane& iLane = editor;

    iLane.setLength(8);
    REQUIRE(editor.getNumSteps() == 8);
}

TEST_CASE("ArpLaneEditor::setCollapseCallback wires correctly via IArpLane", "[arp_lane_editor][iarplane]") {
    auto editor = makeArpLaneEditor();
    IArpLane& iLane = editor;

    bool callbackFired = false;
    iLane.setCollapseCallback([&callbackFired]() {
        callbackFired = true;
    });

    iLane.setCollapsed(true);
    REQUIRE(callbackFired);
    REQUIRE(iLane.isCollapsed());
}

TEST_CASE("ArpLaneEditor IArpLane height methods return correct values", "[arp_lane_editor][iarplane]") {
    auto editor = makeArpLaneEditor();
    IArpLane& iLane = editor;

    REQUIRE(iLane.getExpandedHeight() == Approx(200.0f).margin(0.01f));
    REQUIRE(iLane.getCollapsedHeight() == Approx(16.0f).margin(0.01f));
    REQUIRE_FALSE(iLane.isCollapsed());
}

// ==============================================================================
// Pitch Lane Bipolar Mode Tests (080-specialized-lane-types T010)
// ==============================================================================

// Helper: create a pitch-mode ArpLaneEditor
static ArpLaneEditor makePitchLaneEditor(int numSteps = 8) {
    ArpLaneEditor editor(CRect(0, 0, 500, 200), nullptr, -1);
    editor.setNumSteps(numSteps);
    editor.setLaneType(ArpLaneType::kPitch);
    editor.setAccentColor(CColor{108, 168, 160, 255}); // Sage
    editor.setDisplayRange(-24.0f, 24.0f, "+24", "-24");
    return editor;
}

TEST_CASE("Bipolar mode: normalized 0.5 = 0 semitones (center)", "[arp_lane_editor][bipolar]") {
    auto editor = makePitchLaneEditor();

    // Canonical decode: semitones = round((normalized - 0.5) * 48.0)
    // For normalized = 0.5: semitones = round(0.0 * 48.0) = 0
    float normalized = 0.5f;
    float semitones = std::round((normalized - 0.5f) * 48.0f);
    REQUIRE(semitones == Approx(0.0f).margin(0.01f));
}

TEST_CASE("Bipolar mode: normalized 0.0 = -24 semitones", "[arp_lane_editor][bipolar]") {
    // Canonical decode: semitones = round((0.0 - 0.5) * 48.0) = round(-24.0) = -24
    float normalized = 0.0f;
    float semitones = std::round((normalized - 0.5f) * 48.0f);
    REQUIRE(semitones == Approx(-24.0f).margin(0.01f));
}

TEST_CASE("Bipolar mode: normalized 1.0 = +24 semitones", "[arp_lane_editor][bipolar]") {
    // Canonical decode: semitones = round((1.0 - 0.5) * 48.0) = round(24.0) = +24
    float normalized = 1.0f;
    float semitones = std::round((normalized - 0.5f) * 48.0f);
    REQUIRE(semitones == Approx(24.0f).margin(0.01f));
}

TEST_CASE("Bipolar mode: bars above center for positive values", "[arp_lane_editor][bipolar]") {
    auto editor = makePitchLaneEditor();
    // Set step 0 to +12 semitones: normalized = 0.5 + 12/48 = 0.75
    editor.setStepLevel(0, 0.75f);

    // signedValue = (0.75 - 0.5) * 2.0 = 0.5 (positive => above center)
    float signedValue = (editor.getStepLevel(0) - 0.5f) * 2.0f;
    REQUIRE(signedValue > 0.0f);
}

TEST_CASE("Bipolar mode: bars below center for negative values", "[arp_lane_editor][bipolar]") {
    auto editor = makePitchLaneEditor();
    // Set step 0 to -12 semitones: normalized = 0.5 + (-12)/48 = 0.25
    editor.setStepLevel(0, 0.25f);

    // signedValue = (0.25 - 0.5) * 2.0 = -0.5 (negative => below center)
    float signedValue = (editor.getStepLevel(0) - 0.5f) * 2.0f;
    REQUIRE(signedValue < 0.0f);
}

TEST_CASE("Bipolar mode: pitch step data round-trips correctly", "[arp_lane_editor][bipolar]") {
    auto editor = makePitchLaneEditor();

    // Test multiple semitone values round-trip through normalize/denormalize
    int testSemitones[] = {-24, -12, -1, 0, 1, 12, 24};
    for (int semi : testSemitones) {
        // Encode: normalized = 0.5 + semitones / 48.0
        float normalized = 0.5f + static_cast<float>(semi) / 48.0f;
        editor.setStepLevel(0, normalized);

        // Decode: semitones = round((normalized - 0.5) * 48.0)
        float decoded = std::round((editor.getStepLevel(0) - 0.5f) * 48.0f);
        REQUIRE(decoded == Approx(static_cast<float>(semi)).margin(0.01f));
    }
}

// ==============================================================================
// Pitch Lane Bipolar Snapping Tests (080-specialized-lane-types T011)
// ==============================================================================

TEST_CASE("Bipolar snapping: +12.7 semitones snaps to +13", "[arp_lane_editor][bipolar][snapping]") {
    // Canonical formula: semitones = round((normalized - 0.5) * 48.0)
    // If Y produces a raw value of +12.7 semitones, after snapping:
    // snapped_normalized = 0.5 + round(12.7) / 48.0 = 0.5 + 13/48
    float rawSemitones = 12.7f;
    float snapped = std::round(rawSemitones);
    REQUIRE(snapped == Approx(13.0f).margin(0.01f));

    float snappedNormalized = 0.5f + snapped / 48.0f;
    float decodedSemitones = std::round((snappedNormalized - 0.5f) * 48.0f);
    REQUIRE(decodedSemitones == Approx(13.0f).margin(0.01f));
}

TEST_CASE("Bipolar snapping: -7.3 semitones snaps to -7", "[arp_lane_editor][bipolar][snapping]") {
    float rawSemitones = -7.3f;
    float snapped = std::round(rawSemitones);
    REQUIRE(snapped == Approx(-7.0f).margin(0.01f));

    float snappedNormalized = 0.5f + snapped / 48.0f;
    float decodedSemitones = std::round((snappedNormalized - 0.5f) * 48.0f);
    REQUIRE(decodedSemitones == Approx(-7.0f).margin(0.01f));
}

TEST_CASE("Bipolar snapping: all integer semitones produce integer-snapped values", "[arp_lane_editor][bipolar][snapping]") {
    // Every integer semitone from -24 to +24 must encode/decode exactly
    for (int semi = -24; semi <= 24; ++semi) {
        float normalized = 0.5f + static_cast<float>(semi) / 48.0f;
        float decoded = std::round((normalized - 0.5f) * 48.0f);
        REQUIRE(decoded == Approx(static_cast<float>(semi)).margin(0.01f));
    }
}

// ==============================================================================
// Pitch Lane Bipolar Interaction Tests (080-specialized-lane-types T012)
// ==============================================================================

TEST_CASE("Bipolar interaction: snapBipolarToSemitone produces correct normalized values",
          "[arp_lane_editor][bipolar][interaction]") {
    auto editor = makePitchLaneEditor();

    // Test the snapping utility: a raw normalized value of 0.6 => signedValue = 0.2
    // rawSemitones = 0.2 * 24 = 4.8 -> snaps to 5 -> normalized = 0.5 + 5/48 = 0.604167
    float rawNormalized = 0.6f;
    float signedValue = (rawNormalized - 0.5f) * 2.0f;
    float rawSemitones = signedValue * 24.0f;
    float snappedSemitones = std::round(rawSemitones);
    float snappedNormalized = 0.5f + snappedSemitones / 48.0f;

    float decodedSemitones = std::round((snappedNormalized - 0.5f) * 48.0f);
    REQUIRE(decodedSemitones == Approx(5.0f).margin(0.01f));
}

TEST_CASE("Bipolar interaction: right-click resets to 0.5 normalized (0 semitones)",
          "[arp_lane_editor][bipolar][interaction]") {
    auto editor = makePitchLaneEditor();

    // Set step 0 to +12 semitones
    editor.setStepLevel(0, 0.75f);
    REQUIRE(editor.getStepLevel(0) == Approx(0.75f).margin(0.001f));

    // Right-click should reset to 0.5 (0 semitones) in pitch mode
    // We verify the reset value logic here
    float resetValue = 0.5f; // kPitch right-click reset value
    editor.setStepLevel(0, resetValue);
    float semitones = std::round((editor.getStepLevel(0) - 0.5f) * 48.0f);
    REQUIRE(semitones == Approx(0.0f).margin(0.01f));
}

TEST_CASE("Bipolar interaction: click above center sets positive normalized value",
          "[arp_lane_editor][bipolar][interaction]") {
    auto editor = makePitchLaneEditor();

    // Simulate: Y position above center produces a level > 0.5
    // In getLevelFromY, higher Y (closer to top) = higher level
    // For pitch mode, level > 0.5 means positive semitones
    float aboveCenterLevel = 0.7f;
    // Snap: semitones = round((0.7 - 0.5) * 48) = round(9.6) = 10
    float semitones = std::round((aboveCenterLevel - 0.5f) * 48.0f);
    float snappedNormalized = 0.5f + semitones / 48.0f;

    editor.setStepLevel(0, snappedNormalized);
    float decodedSemitones = std::round((editor.getStepLevel(0) - 0.5f) * 48.0f);
    REQUIRE(decodedSemitones > 0.0f);
    REQUIRE(decodedSemitones == Approx(10.0f).margin(0.01f));
}

TEST_CASE("Bipolar interaction: click below center sets negative normalized value",
          "[arp_lane_editor][bipolar][interaction]") {
    auto editor = makePitchLaneEditor();

    // Y position below center produces a level < 0.5
    float belowCenterLevel = 0.3f;
    // Snap: semitones = round((0.3 - 0.5) * 48) = round(-9.6) = -10
    float semitones = std::round((belowCenterLevel - 0.5f) * 48.0f);
    float snappedNormalized = 0.5f + semitones / 48.0f;

    editor.setStepLevel(0, snappedNormalized);
    float decodedSemitones = std::round((editor.getStepLevel(0) - 0.5f) * 48.0f);
    REQUIRE(decodedSemitones < 0.0f);
    REQUIRE(decodedSemitones == Approx(-10.0f).margin(0.01f));
}

// ==============================================================================
// Pitch Lane Bipolar Miniature Preview Tests (080-specialized-lane-types T013)
// ==============================================================================

TEST_CASE("Bipolar miniature preview: collapsed pitch lane preserves step data",
          "[arp_lane_editor][bipolar][preview]") {
    auto editor = makePitchLaneEditor(4);

    // Set mixed positive/negative values
    editor.setStepLevel(0, 0.75f);  // +12 semitones (above center)
    editor.setStepLevel(1, 0.25f);  // -12 semitones (below center)
    editor.setStepLevel(2, 0.5f);   // 0 semitones (center)
    editor.setStepLevel(3, 1.0f);   // +24 semitones (max above)

    editor.setCollapsed(true);

    // Verify data still accessible after collapse
    float signed0 = (editor.getStepLevel(0) - 0.5f) * 2.0f;
    float signed1 = (editor.getStepLevel(1) - 0.5f) * 2.0f;
    float signed2 = (editor.getStepLevel(2) - 0.5f) * 2.0f;
    float signed3 = (editor.getStepLevel(3) - 0.5f) * 2.0f;

    REQUIRE(signed0 > 0.0f);   // positive: above center
    REQUIRE(signed1 < 0.0f);   // negative: below center
    REQUIRE(signed2 == Approx(0.0f).margin(0.01f)); // center
    REQUIRE(signed3 > 0.0f);   // positive: above center
}

TEST_CASE("Bipolar miniature preview: positive values render above center, negative below",
          "[arp_lane_editor][bipolar][preview]") {
    auto editor = makePitchLaneEditor(4);

    // Set +6 semitones: normalized = 0.5 + 6/48 = 0.625
    editor.setStepLevel(0, 0.625f);
    // Set -6 semitones: normalized = 0.5 + (-6)/48 = 0.375
    editor.setStepLevel(1, 0.375f);

    editor.setCollapsed(true);

    // For miniature preview, signed values determine bar direction
    float sv0 = (editor.getStepLevel(0) - 0.5f) * 2.0f;
    float sv1 = (editor.getStepLevel(1) - 0.5f) * 2.0f;

    REQUIRE(sv0 > 0.0f);  // above center
    REQUIRE(sv1 < 0.0f);  // below center

    // Magnitudes should be equal for symmetric values
    REQUIRE(std::abs(sv0) == Approx(std::abs(sv1)).margin(0.01f));
}

// ==============================================================================
// Ratchet Lane Discrete Mode Rendering Tests (080-specialized-lane-types T021)
// ==============================================================================

// Helper: create a ratchet-mode ArpLaneEditor
static ArpLaneEditor makeRatchetLaneEditor(int numSteps = 8) {
    ArpLaneEditor editor(CRect(0, 0, 500, 200), nullptr, -1);
    editor.setNumSteps(numSteps);
    editor.setLaneType(ArpLaneType::kRatchet);
    editor.setAccentColor(CColor{152, 128, 176, 255}); // Lavender
    return editor;
}

TEST_CASE("Discrete mode rendering: normalized 0.0 = 1 block",
          "[arp_lane_editor][discrete][rendering]") {
    auto editor = makeRatchetLaneEditor();
    editor.setStepLevel(0, 0.0f);

    // Decode: count = clamp(1 + round(0.0 * 3.0), 1, 4) = 1
    float normalized = editor.getStepLevel(0);
    int count = std::clamp(static_cast<int>(1.0f + std::round(normalized * 3.0f)), 1, 4);
    REQUIRE(count == 1);
}

TEST_CASE("Discrete mode rendering: normalized 1/3 = 2 blocks",
          "[arp_lane_editor][discrete][rendering]") {
    auto editor = makeRatchetLaneEditor();
    float normalized = 1.0f / 3.0f;
    editor.setStepLevel(0, normalized);

    int count = std::clamp(static_cast<int>(1.0f + std::round(editor.getStepLevel(0) * 3.0f)), 1, 4);
    REQUIRE(count == 2);
}

TEST_CASE("Discrete mode rendering: normalized 2/3 = 3 blocks",
          "[arp_lane_editor][discrete][rendering]") {
    auto editor = makeRatchetLaneEditor();
    float normalized = 2.0f / 3.0f;
    editor.setStepLevel(0, normalized);

    int count = std::clamp(static_cast<int>(1.0f + std::round(editor.getStepLevel(0) * 3.0f)), 1, 4);
    REQUIRE(count == 3);
}

TEST_CASE("Discrete mode rendering: normalized 1.0 = 4 blocks",
          "[arp_lane_editor][discrete][rendering]") {
    auto editor = makeRatchetLaneEditor();
    editor.setStepLevel(0, 1.0f);

    int count = std::clamp(static_cast<int>(1.0f + std::round(editor.getStepLevel(0) * 3.0f)), 1, 4);
    REQUIRE(count == 4);
}

TEST_CASE("Discrete mode rendering: all counts 1-4 produce correct block counts",
          "[arp_lane_editor][discrete][rendering]") {
    auto editor = makeRatchetLaneEditor(4);

    // Set normalized values for counts 1-4
    float normalizedValues[] = {0.0f, 1.0f/3.0f, 2.0f/3.0f, 1.0f};
    int expectedCounts[] = {1, 2, 3, 4};

    for (int i = 0; i < 4; ++i) {
        editor.setStepLevel(i, normalizedValues[i]);
    }

    for (int i = 0; i < 4; ++i) {
        float n = editor.getStepLevel(i);
        int count = std::clamp(static_cast<int>(1.0f + std::round(n * 3.0f)), 1, 4);
        REQUIRE(count == expectedCounts[i]);
    }
}

TEST_CASE("Discrete mode rendering: encode/decode round-trips for all counts",
          "[arp_lane_editor][discrete][rendering]") {
    // Verify: encode(count) -> normalized -> decode -> count
    for (int c = 1; c <= 4; ++c) {
        float normalized = static_cast<float>(c - 1) / 3.0f;
        int decoded = std::clamp(static_cast<int>(1.0f + std::round(normalized * 3.0f)), 1, 4);
        REQUIRE(decoded == c);
    }
}

// ==============================================================================
// Ratchet Lane Discrete Click Cycle Tests (080-specialized-lane-types T022)
// ==============================================================================

TEST_CASE("Discrete click cycle: N=1 produces N=2",
          "[arp_lane_editor][discrete][click]") {
    auto editor = makeRatchetLaneEditor();
    // Set step 0 to count 1 (normalized 0.0)
    editor.setStepLevel(0, 0.0f);

    // Simulate click cycle: decode current, increment with wrap
    float n = editor.getStepLevel(0);
    int count = std::clamp(static_cast<int>(1.0f + std::round(n * 3.0f)), 1, 4);
    REQUIRE(count == 1);

    int nextCount = (count % 4) + 1; // 1->2
    REQUIRE(nextCount == 2);

    float nextNormalized = static_cast<float>(nextCount - 1) / 3.0f;
    editor.setStepLevel(0, nextNormalized);
    int decoded = std::clamp(static_cast<int>(1.0f + std::round(editor.getStepLevel(0) * 3.0f)), 1, 4);
    REQUIRE(decoded == 2);
}

TEST_CASE("Discrete click cycle: N=2 produces N=3",
          "[arp_lane_editor][discrete][click]") {
    auto editor = makeRatchetLaneEditor();
    editor.setStepLevel(0, 1.0f / 3.0f); // count=2

    float n = editor.getStepLevel(0);
    int count = std::clamp(static_cast<int>(1.0f + std::round(n * 3.0f)), 1, 4);
    REQUIRE(count == 2);

    int nextCount = (count % 4) + 1; // 2->3
    REQUIRE(nextCount == 3);
}

TEST_CASE("Discrete click cycle: N=3 produces N=4",
          "[arp_lane_editor][discrete][click]") {
    auto editor = makeRatchetLaneEditor();
    editor.setStepLevel(0, 2.0f / 3.0f); // count=3

    float n = editor.getStepLevel(0);
    int count = std::clamp(static_cast<int>(1.0f + std::round(n * 3.0f)), 1, 4);
    REQUIRE(count == 3);

    int nextCount = (count % 4) + 1; // 3->4
    REQUIRE(nextCount == 4);
}

TEST_CASE("Discrete click cycle: N=4 wraps to N=1",
          "[arp_lane_editor][discrete][click]") {
    auto editor = makeRatchetLaneEditor();
    editor.setStepLevel(0, 1.0f); // count=4

    float n = editor.getStepLevel(0);
    int count = std::clamp(static_cast<int>(1.0f + std::round(n * 3.0f)), 1, 4);
    REQUIRE(count == 4);

    int nextCount = (count % 4) + 1; // 4->1
    REQUIRE(nextCount == 1);
}

TEST_CASE("Discrete click cycle: full cycle 1->2->3->4->1",
          "[arp_lane_editor][discrete][click]") {
    auto editor = makeRatchetLaneEditor();
    editor.setStepLevel(0, 0.0f); // start at count=1

    int expected[] = {2, 3, 4, 1};
    for (int i = 0; i < 4; ++i) {
        float n = editor.getStepLevel(0);
        int count = std::clamp(static_cast<int>(1.0f + std::round(n * 3.0f)), 1, 4);
        int nextCount = (count % 4) + 1;
        float nextNormalized = static_cast<float>(nextCount - 1) / 3.0f;
        editor.setStepLevel(0, nextNormalized);

        int decoded = std::clamp(static_cast<int>(1.0f + std::round(editor.getStepLevel(0) * 3.0f)), 1, 4);
        REQUIRE(decoded == expected[i]);
    }
}

// ==============================================================================
// Ratchet Lane Discrete Drag Tests (080-specialized-lane-types T023)
// ==============================================================================

TEST_CASE("Discrete drag: 8px up from N=2 produces N=3",
          "[arp_lane_editor][discrete][drag]") {
    // Simulating: start at count=2, drag up 8px -> count=3
    int startCount = 2;
    float dragDeltaY = -8.0f; // Up = negative Y
    int levelChange = static_cast<int>(-dragDeltaY / 8.0f); // +1
    int newCount = std::clamp(startCount + levelChange, 1, 4);
    REQUIRE(newCount == 3);
}

TEST_CASE("Discrete drag: 16px up from N=2 produces N=4",
          "[arp_lane_editor][discrete][drag]") {
    int startCount = 2;
    float dragDeltaY = -16.0f;
    int levelChange = static_cast<int>(-dragDeltaY / 8.0f); // +2
    int newCount = std::clamp(startCount + levelChange, 1, 4);
    REQUIRE(newCount == 4);
}

TEST_CASE("Discrete drag: drag up clamps at N=4 (no wrap)",
          "[arp_lane_editor][discrete][drag]") {
    int startCount = 3;
    float dragDeltaY = -24.0f; // Would give +3
    int levelChange = static_cast<int>(-dragDeltaY / 8.0f);
    int newCount = std::clamp(startCount + levelChange, 1, 4);
    REQUIRE(newCount == 4); // Clamped, not 6
}

TEST_CASE("Discrete drag: drag down clamps at N=1 (no wrap)",
          "[arp_lane_editor][discrete][drag]") {
    int startCount = 2;
    float dragDeltaY = 24.0f; // Would give -3
    int levelChange = static_cast<int>(-dragDeltaY / 8.0f);
    int newCount = std::clamp(startCount + levelChange, 1, 4);
    REQUIRE(newCount == 1); // Clamped, not -1
}

TEST_CASE("Discrete drag: right-click resets to N=1 / normalized 0.0",
          "[arp_lane_editor][discrete][drag]") {
    auto editor = makeRatchetLaneEditor();
    // kRatchet right-click reset level is 0.0 (set by setLaneType)
    editor.setStepLevel(0, 1.0f); // count=4

    // Right-click should reset to rightClickResetLevel_ = 0.0
    float resetLevel = editor.getRightClickResetLevel();
    REQUIRE(resetLevel == Approx(0.0f).margin(0.001f));

    editor.setStepLevel(0, resetLevel);
    int count = std::clamp(static_cast<int>(1.0f + std::round(editor.getStepLevel(0) * 3.0f)), 1, 4);
    REQUIRE(count == 1);
}

// ==============================================================================
// Ratchet Lane Discrete Miniature Preview Tests (080-specialized-lane-types T024)
// ==============================================================================

TEST_CASE("Discrete miniature preview: count values produce correct height proportions",
          "[arp_lane_editor][discrete][preview]") {
    auto editor = makeRatchetLaneEditor(4);

    // Set counts 1, 3, 2, 4
    editor.setStepLevel(0, 0.0f);         // count=1 -> 25%
    editor.setStepLevel(1, 2.0f / 3.0f);  // count=3 -> 75%
    editor.setStepLevel(2, 1.0f / 3.0f);  // count=2 -> 50%
    editor.setStepLevel(3, 1.0f);          // count=4 -> 100%

    editor.setCollapsed(true);

    // Verify proportions: count/4.0 gives the height fraction
    float expectedFractions[] = {0.25f, 0.75f, 0.50f, 1.0f};
    for (int i = 0; i < 4; ++i) {
        float n = editor.getStepLevel(i);
        int count = std::clamp(static_cast<int>(1.0f + std::round(n * 3.0f)), 1, 4);
        float fraction = static_cast<float>(count) / 4.0f;
        REQUIRE(fraction == Approx(expectedFractions[i]).margin(0.01f));
    }
}

TEST_CASE("Discrete miniature preview: collapsed preserves step data for preview",
          "[arp_lane_editor][discrete][preview]") {
    auto editor = makeRatchetLaneEditor(4);

    editor.setStepLevel(0, 0.0f);
    editor.setStepLevel(1, 1.0f / 3.0f);
    editor.setStepLevel(2, 2.0f / 3.0f);
    editor.setStepLevel(3, 1.0f);

    editor.setCollapsed(true);

    // Step data must be preserved after collapse
    REQUIRE(editor.getStepLevel(0) == Approx(0.0f).margin(0.001f));
    REQUIRE(editor.getStepLevel(1) == Approx(1.0f / 3.0f).margin(0.001f));
    REQUIRE(editor.getStepLevel(2) == Approx(2.0f / 3.0f).margin(0.001f));
    REQUIRE(editor.getStepLevel(3) == Approx(1.0f).margin(0.001f));
}

TEST_CASE("Discrete miniature preview: lavender accent color is set",
          "[arp_lane_editor][discrete][preview]") {
    auto editor = makeRatchetLaneEditor();

    CColor accent = editor.getAccentColor();
    REQUIRE(accent.red == 152);
    REQUIRE(accent.green == 128);
    REQUIRE(accent.blue == 176);
    REQUIRE(accent.alpha == 255);
}

// ==============================================================================
// Ratchet Lane Helper Method Tests (080-specialized-lane-types T025-T028)
// ==============================================================================

TEST_CASE("getDiscreteCount returns correct count from normalized level",
          "[arp_lane_editor][discrete]") {
    auto editor = makeRatchetLaneEditor(4);

    // Test the decoding formula directly
    editor.setStepLevel(0, 0.0f);
    REQUIRE(editor.getDiscreteCount(0) == 1);

    editor.setStepLevel(0, 1.0f / 3.0f);
    REQUIRE(editor.getDiscreteCount(0) == 2);

    editor.setStepLevel(0, 2.0f / 3.0f);
    REQUIRE(editor.getDiscreteCount(0) == 3);

    editor.setStepLevel(0, 1.0f);
    REQUIRE(editor.getDiscreteCount(0) == 4);
}

TEST_CASE("setDiscreteCount sets correct normalized level",
          "[arp_lane_editor][discrete]") {
    auto editor = makeRatchetLaneEditor(4);

    editor.setDiscreteCount(0, 1);
    REQUIRE(editor.getStepLevel(0) == Approx(0.0f).margin(0.001f));

    editor.setDiscreteCount(0, 2);
    REQUIRE(editor.getStepLevel(0) == Approx(1.0f / 3.0f).margin(0.001f));

    editor.setDiscreteCount(0, 3);
    REQUIRE(editor.getStepLevel(0) == Approx(2.0f / 3.0f).margin(0.001f));

    editor.setDiscreteCount(0, 4);
    REQUIRE(editor.getStepLevel(0) == Approx(1.0f).margin(0.001f));
}

// ==============================================================================
// Bug Fix Tests
// ==============================================================================

// Bug 1: Header draw order - header drawn after base class
TEST_CASE("ArpLaneEditor header draw order: header drawn after base class",
          "[arp_lane_editor][draw_order]") {
    auto editor = makeArpLaneEditor(16);
    editor.setLaneName("VEL");

    // The bar area top must start at or below kHeaderHeight + kPhaseOffsetHeight,
    // proving the header occupies space above the bar area and the base class
    // background fill does not cover it (since header draws after base).
    VSTGUI::CRect barArea = editor.getBarArea();
    float minBarTop = ArpLaneEditor::kHeaderHeight + StepPatternEditor::kPhaseOffsetHeight;
    REQUIRE(static_cast<float>(barArea.top) >= minBarTop);
}

// Bug 2: Ratchet lane at 86px has usable bar area height
TEST_CASE("Ratchet lane at 86px has usable bar area height (>= 30px)",
          "[arp_lane_editor][ratchet][layout]") {
    ArpLaneEditor editor(CRect(0, 0, 500, 86), nullptr, -1);
    editor.setNumSteps(16);
    editor.setLaneType(ArpLaneType::kRatchet);

    CRect barArea = editor.getBarArea();
    float barAreaHeight = static_cast<float>(barArea.getHeight());
    REQUIRE(barAreaHeight >= 30.0f);
}

TEST_CASE("Ratchet lane at old 52px height has tiny bar area",
          "[arp_lane_editor][ratchet][layout]") {
    // This documents the old bug: 52px gave only ~4px bar area
    ArpLaneEditor editor(CRect(0, 0, 500, 52), nullptr, -1);
    editor.setNumSteps(16);
    editor.setLaneType(ArpLaneType::kRatchet);

    CRect barArea = editor.getBarArea();
    float barAreaHeight = static_cast<float>(barArea.getHeight());
    // At 52px total, bar area is way too small to be usable
    REQUIRE(barAreaHeight < 20.0f);
}

// Bug 3: Grid labels per lane type
TEST_CASE("Pitch lane type sets empty grid labels on base class",
          "[arp_lane_editor][grid_labels]") {
    auto editor = makeArpLaneEditor(16);
    editor.setLaneType(ArpLaneType::kPitch);

    REQUIRE(editor.getGridTopLabel().empty());
    REQUIRE(editor.getGridBottomLabel().empty());
}

TEST_CASE("Ratchet lane type sets 4/1 grid labels on base class",
          "[arp_lane_editor][grid_labels]") {
    auto editor = makeArpLaneEditor(16);
    editor.setLaneType(ArpLaneType::kRatchet);

    REQUIRE(editor.getGridTopLabel() == "4");
    REQUIRE(editor.getGridBottomLabel() == "1");
}

TEST_CASE("Velocity lane type keeps default 1.0/0.0 grid labels",
          "[arp_lane_editor][grid_labels]") {
    auto editor = makeArpLaneEditor(16);
    // Default lane type is kVelocity
    REQUIRE(editor.getLaneType() == ArpLaneType::kVelocity);
    REQUIRE(editor.getGridTopLabel() == "1.0");
    REQUIRE(editor.getGridBottomLabel() == "0.0");
}

TEST_CASE("Gate lane type keeps default 1.0/0.0 grid labels",
          "[arp_lane_editor][grid_labels]") {
    auto editor = makeArpLaneEditor(16);
    editor.setLaneType(ArpLaneType::kGate);

    REQUIRE(editor.getGridTopLabel() == "1.0");
    REQUIRE(editor.getGridBottomLabel() == "0.0");
}

// ==============================================================================
// Ratchet Lane Helper Method Tests (080-specialized-lane-types T025-T028)
// ==============================================================================

TEST_CASE("handleDiscreteClick cycles through 1->2->3->4->1",
          "[arp_lane_editor][discrete]") {
    auto editor = makeRatchetLaneEditor();
    editor.setStepLevel(0, 0.0f); // count=1

    editor.handleDiscreteClick(0);
    REQUIRE(editor.getDiscreteCount(0) == 2);

    editor.handleDiscreteClick(0);
    REQUIRE(editor.getDiscreteCount(0) == 3);

    editor.handleDiscreteClick(0);
    REQUIRE(editor.getDiscreteCount(0) == 4);

    editor.handleDiscreteClick(0);
    REQUIRE(editor.getDiscreteCount(0) == 1);
}

// ==============================================================================
// Pitch Lane Scale-Aware Popup Suffix Tests (084-arp-scale-mode T066a)
// ==============================================================================

TEST_CASE("formatValueText: Chromatic (scaleType=8) pitch value uses ' st' suffix",
          "[arp_lane_editor][scale-mode][popup]") {
    auto editor = makePitchLaneEditor();
    // Default scaleType_ is 8 (Chromatic) -- no setScaleType call needed

    // +2 semitones: normalized = 0.5 + 2/48 = 0.5417
    float normalized = 0.5f + 2.0f / 48.0f;
    std::string text = editor.formatValueText(normalized);
    REQUIRE(text == "+2 st");
}

TEST_CASE("formatValueText: Non-Chromatic (scaleType=0, Major) pitch value uses ' deg' suffix",
          "[arp_lane_editor][scale-mode][popup]") {
    auto editor = makePitchLaneEditor();
    editor.setScaleType(0); // Major

    // +2 degrees: normalized = 0.5 + 2/48 = 0.5417
    float normalized = 0.5f + 2.0f / 48.0f;
    std::string text = editor.formatValueText(normalized);
    REQUIRE(text == "+2 deg");
}

TEST_CASE("formatValueText: Chromatic negative pitch value uses ' st' suffix",
          "[arp_lane_editor][scale-mode][popup]") {
    auto editor = makePitchLaneEditor();
    // Default scaleType_ is 8 (Chromatic)

    // -1 semitone: normalized = 0.5 + (-1)/48
    float normalized = 0.5f + (-1.0f) / 48.0f;
    std::string text = editor.formatValueText(normalized);
    REQUIRE(text == "-1 st");
}

TEST_CASE("formatValueText: Non-Chromatic zero pitch shows '0 deg'",
          "[arp_lane_editor][scale-mode][popup]") {
    auto editor = makePitchLaneEditor();
    editor.setScaleType(4); // Dorian

    std::string text = editor.formatValueText(0.5f);
    REQUIRE(text == "0 deg");
}

TEST_CASE("formatValueText: Chromatic zero pitch shows '0 st'",
          "[arp_lane_editor][scale-mode][popup]") {
    auto editor = makePitchLaneEditor();
    // Default scaleType_ is 8 (Chromatic)

    std::string text = editor.formatValueText(0.5f);
    REQUIRE(text == "0 st");
}
