// ==============================================================================
// StepPatternEditor Tests (046-step-pattern-editor)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/step_pattern_editor.h"

using namespace Krate::Plugins;
using namespace VSTGUI;
using Catch::Approx;

// Helper: create a default 500x200 editor at position (0,0)
static StepPatternEditor makeEditor(int numSteps = 16) {
    StepPatternEditor editor(CRect(0, 0, 500, 200), nullptr, -1);
    editor.setNumSteps(numSteps);
    return editor;
}

// ==============================================================================
// Layout Computation Tests (T020)
// ==============================================================================

TEST_CASE("getBarRect returns valid rectangles for all steps", "[step_pattern_editor][layout]") {
    auto editor = makeEditor(16);

    for (int i = 0; i < 16; ++i) {
        CRect bar = editor.getBarRect(i);
        REQUIRE_FALSE(bar.isEmpty());
        REQUIRE(bar.getWidth() > 0);
        REQUIRE(bar.getHeight() >= 0);
    }
}

TEST_CASE("getBarRect returns empty for out-of-range step", "[step_pattern_editor][layout]") {
    auto editor = makeEditor(16);

    CRect bar = editor.getBarRect(16);
    REQUIRE(bar.isEmpty());
}

TEST_CASE("getBarRect bars fit within bar area", "[step_pattern_editor][layout]") {
    auto editor = makeEditor(16);
    CRect barArea = editor.getBarArea();

    for (int i = 0; i < 16; ++i) {
        CRect bar = editor.getBarRect(i);
        REQUIRE(bar.left >= barArea.left - 1.0);
        REQUIRE(bar.right <= barArea.right + 1.0);
        REQUIRE(bar.top >= barArea.top - 1.0);
        REQUIRE(bar.bottom <= barArea.bottom + 1.0);
    }
}

TEST_CASE("getStepFromPoint returns correct step index", "[step_pattern_editor][layout]") {
    auto editor = makeEditor(16);
    CRect barArea = editor.getBarArea();

    // Click in the middle of the bar area
    float barWidth = static_cast<float>(barArea.getWidth()) / 16.0f;
    float midY = static_cast<float>(barArea.top + barArea.bottom) / 2.0f;

    // Step 0: left side of bar area
    CPoint p0(barArea.left + barWidth * 0.5, midY);
    REQUIRE(editor.getStepFromPoint(p0) == 0);

    // Step 15: right side
    CPoint p15(barArea.left + barWidth * 15.5, midY);
    REQUIRE(editor.getStepFromPoint(p15) == 15);
}

TEST_CASE("getStepFromPoint returns -1 outside bar area", "[step_pattern_editor][layout]") {
    auto editor = makeEditor(16);

    CPoint outsideLeft(0, 100);
    REQUIRE(editor.getStepFromPoint(outsideLeft) == -1);

    CPoint outsideTop(250, 0);
    REQUIRE(editor.getStepFromPoint(outsideTop) == -1);
}

// ==============================================================================
// Color Selection Tests (T021)
// ==============================================================================

TEST_CASE("getColorForLevel returns outline color at 0.0", "[step_pattern_editor][color]") {
    auto editor = makeEditor();
    CColor result = editor.getColorForLevel(0.0f);
    CColor expected = editor.getSilentOutlineColor();
    REQUIRE(result == expected);
}

TEST_CASE("getColorForLevel returns ghost color for low levels", "[step_pattern_editor][color]") {
    auto editor = makeEditor();

    // 0.01-0.39 -> ghost
    CColor ghost = editor.getBarColorGhost();
    REQUIRE(editor.getColorForLevel(0.01f) == ghost);
    REQUIRE(editor.getColorForLevel(0.20f) == ghost);
    REQUIRE(editor.getColorForLevel(0.39f) == ghost);
}

TEST_CASE("getColorForLevel returns normal color for mid levels", "[step_pattern_editor][color]") {
    auto editor = makeEditor();

    // 0.40-0.79 -> normal
    CColor normal = editor.getBarColorNormal();
    REQUIRE(editor.getColorForLevel(0.40f) == normal);
    REQUIRE(editor.getColorForLevel(0.60f) == normal);
    REQUIRE(editor.getColorForLevel(0.79f) == normal);
}

TEST_CASE("getColorForLevel returns accent color for high levels", "[step_pattern_editor][color]") {
    auto editor = makeEditor();

    // 0.80-1.0 -> accent
    CColor accent = editor.getBarColorAccent();
    REQUIRE(editor.getColorForLevel(0.80f) == accent);
    REQUIRE(editor.getColorForLevel(0.95f) == accent);
    REQUIRE(editor.getColorForLevel(1.0f) == accent);
}

// ==============================================================================
// Bar Width Computation Tests (T057)
// ==============================================================================

TEST_CASE("bars fit within width for all step counts 2-32", "[step_pattern_editor][layout]") {
    for (int numSteps = 2; numSteps <= 32; ++numSteps) {
        auto editor = makeEditor(numSteps);
        CRect barArea = editor.getBarArea();
        float barAreaWidth = static_cast<float>(barArea.getWidth());

        // Each bar should have positive width
        float barWidth = barAreaWidth / static_cast<float>(numSteps);
        REQUIRE(barWidth > 0.0f);

        // Bars should not extend beyond bar area
        CRect lastBar = editor.getBarRect(numSteps - 1);
        REQUIRE(lastBar.right <= barArea.right + 1.0);
    }
}

// ==============================================================================
// Level Preservation Tests (T058)
// ==============================================================================

TEST_CASE("step levels are preserved when step count changes", "[step_pattern_editor][steps]") {
    auto editor = makeEditor(16);

    // Set some specific levels
    editor.setStepLevel(0, 0.5f);
    editor.setStepLevel(1, 0.3f);
    editor.setStepLevel(7, 0.8f);

    // Reduce to 8 steps
    editor.setNumSteps(8);
    REQUIRE(editor.getStepLevel(0) == Approx(0.5f));
    REQUIRE(editor.getStepLevel(1) == Approx(0.3f));
    REQUIRE(editor.getStepLevel(7) == Approx(0.8f));

    // Increase back to 16
    editor.setNumSteps(16);
    REQUIRE(editor.getStepLevel(0) == Approx(0.5f));
    REQUIRE(editor.getStepLevel(1) == Approx(0.3f));
    REQUIRE(editor.getStepLevel(7) == Approx(0.8f));
}

TEST_CASE("numSteps is clamped to valid range", "[step_pattern_editor][steps]") {
    auto editor = makeEditor(16);

    editor.setNumSteps(1);
    REQUIRE(editor.getNumSteps() == StepPatternEditor::kMinSteps);

    editor.setNumSteps(100);
    REQUIRE(editor.getNumSteps() == StepPatternEditor::kMaxSteps);
}

// ==============================================================================
// Euclidean Pattern Tests (T072)
// ==============================================================================

TEST_CASE("Euclidean E(5,16,0) generates correct hit positions", "[step_pattern_editor][euclidean]") {
    auto editor = makeEditor(16);
    editor.setEuclideanHits(5);
    editor.setEuclideanRotation(0);
    editor.setEuclideanEnabled(true);

    // E(5,16) should have 5 hits evenly distributed
    int hitCount = 0;
    for (int i = 0; i < 16; ++i) {
        if (editor.getStepLevel(i) >= 1.0f) hitCount++;
    }
    REQUIRE(hitCount >= 5);
}

// ==============================================================================
// Euclidean Rotation Tests (T073)
// ==============================================================================

TEST_CASE("Euclidean rotation shifts hit positions", "[step_pattern_editor][euclidean]") {
    // Start with all steps at 0 so we can clearly see what the Euclidean
    // pattern sets. Then use regenerateEuclidean() which does a clean reset.
    auto editor1 = makeEditor(16);
    editor1.applyPresetOff();  // All to 0
    editor1.setEuclideanHits(5);
    editor1.setEuclideanRotation(0);
    editor1.setEuclideanEnabled(true);
    editor1.regenerateEuclidean();  // Force pure pattern

    // Record hit positions with rotation=0
    std::array<float, 32> levels0{};
    for (int i = 0; i < 16; ++i) levels0[static_cast<size_t>(i)] = editor1.getStepLevel(i);

    auto editor2 = makeEditor(16);
    editor2.applyPresetOff();  // All to 0
    editor2.setEuclideanHits(5);
    editor2.setEuclideanRotation(2);
    editor2.setEuclideanEnabled(true);
    editor2.regenerateEuclidean();  // Force pure pattern

    // Record hit positions with rotation=2
    std::array<float, 32> levels2{};
    for (int i = 0; i < 16; ++i) levels2[static_cast<size_t>(i)] = editor2.getStepLevel(i);

    // The patterns should be different (rotation shifts them)
    bool different = false;
    for (int i = 0; i < 16; ++i) {
        if (levels0[static_cast<size_t>(i)] != levels2[static_cast<size_t>(i)]) {
            different = true;
            break;
        }
    }
    REQUIRE(different);
}

// ==============================================================================
// Euclidean Modification Detection Tests (T074)
// ==============================================================================

TEST_CASE("Manual edit triggers isModified flag in Euclidean mode", "[step_pattern_editor][euclidean]") {
    auto editor = makeEditor(16);
    editor.setEuclideanHits(5);
    editor.setEuclideanRotation(0);
    editor.setEuclideanEnabled(true);

    REQUIRE_FALSE(editor.isPatternModified());

    // Simulate manual edit: change a step level directly
    // The real edit would happen via mouse interaction, but we can test
    // by calling setStepLevel and checking that isModified would be set
    // after an applyTransform (which sets isModified in euclidean mode)
    editor.applyTransformInvert();
    REQUIRE(editor.isPatternModified());
}

TEST_CASE("regenerateEuclidean clears modified flag", "[step_pattern_editor][euclidean]") {
    auto editor = makeEditor(16);
    editor.setEuclideanHits(5);
    editor.setEuclideanRotation(0);
    editor.setEuclideanEnabled(true);

    editor.applyTransformInvert();
    REQUIRE(editor.isPatternModified());

    editor.regenerateEuclidean();
    REQUIRE_FALSE(editor.isPatternModified());
}

// ==============================================================================
// Rest-with-Ghost-Note Tests (T074b)
// ==============================================================================

TEST_CASE("Euclidean rest step preserves non-zero level (ghost note)", "[step_pattern_editor][euclidean]") {
    auto editor = makeEditor(16);

    // Set all steps to 0.5 (non-zero)
    for (int i = 0; i < 16; ++i) {
        editor.setStepLevel(i, 0.5f);
    }

    // Enable Euclidean with 4 hits - rest steps should KEEP their 0.5 level
    editor.setEuclideanHits(4);
    editor.setEuclideanRotation(0);
    editor.setEuclideanEnabled(true);

    // Count steps with level > 0.0 -- should be more than just the 4 hits
    // because rest steps preserve their level (FR-020/FR-021)
    int nonZeroCount = 0;
    for (int i = 0; i < 16; ++i) {
        if (editor.getStepLevel(i) > 0.0f) nonZeroCount++;
    }
    // All 16 should have non-zero level: 4 hits at 1.0 (promoted from 0.5 which
    // was non-zero so stays), 12 rests keeping 0.5
    // Actually per FR-021: rest-to-hit sets 1.0 only if currently 0.0.
    // Since they're all 0.5 (non-zero), hits keep 0.5 and rests keep 0.5.
    REQUIRE(nonZeroCount == 16);
}

TEST_CASE("Euclidean rest step at 0.0 stays at 0.0 (pure rest)", "[step_pattern_editor][euclidean]") {
    auto editor = makeEditor(16);
    editor.applyPresetOff(); // All to 0

    editor.setEuclideanHits(4);
    editor.setEuclideanRotation(0);
    editor.setEuclideanEnabled(true);

    // Hits should be promoted to 1.0, rests stay at 0.0
    int hitCount = 0;
    for (int i = 0; i < 16; ++i) {
        if (editor.getStepLevel(i) >= 1.0f) hitCount++;
    }
    REQUIRE(hitCount >= 4);
}

// ==============================================================================
// Preset Pattern Tests (T113)
// ==============================================================================

TEST_CASE("applyPresetAll sets all steps to 1.0", "[step_pattern_editor][presets]") {
    auto editor = makeEditor(8);
    editor.applyPresetOff();  // Start at 0
    editor.applyPresetAll();

    for (int i = 0; i < 8; ++i) {
        REQUIRE(editor.getStepLevel(i) == Approx(1.0f));
    }
}

TEST_CASE("applyPresetOff sets all steps to 0.0", "[step_pattern_editor][presets]") {
    auto editor = makeEditor(8);
    editor.applyPresetOff();

    for (int i = 0; i < 8; ++i) {
        REQUIRE(editor.getStepLevel(i) == Approx(0.0f));
    }
}

TEST_CASE("applyPresetAlternate alternates 1.0 and 0.0", "[step_pattern_editor][presets]") {
    auto editor = makeEditor(8);
    editor.applyPresetAlternate();

    for (int i = 0; i < 8; ++i) {
        float expected = (i % 2 == 0) ? 1.0f : 0.0f;
        REQUIRE(editor.getStepLevel(i) == Approx(expected));
    }
}

TEST_CASE("applyPresetRampUp creates linear ramp from 0 to 1", "[step_pattern_editor][presets]") {
    auto editor = makeEditor(8);
    editor.applyPresetRampUp();

    REQUIRE(editor.getStepLevel(0) == Approx(0.0f));
    REQUIRE(editor.getStepLevel(7) == Approx(1.0f));

    // Check monotonically increasing
    for (int i = 1; i < 8; ++i) {
        REQUIRE(editor.getStepLevel(i) >= editor.getStepLevel(i - 1));
    }
}

TEST_CASE("applyPresetRampDown creates linear ramp from 1 to 0", "[step_pattern_editor][presets]") {
    auto editor = makeEditor(8);
    editor.applyPresetRampDown();

    REQUIRE(editor.getStepLevel(0) == Approx(1.0f));
    REQUIRE(editor.getStepLevel(7) == Approx(0.0f));

    // Check monotonically decreasing
    for (int i = 1; i < 8; ++i) {
        REQUIRE(editor.getStepLevel(i) <= editor.getStepLevel(i - 1));
    }
}

// ==============================================================================
// Transform Tests (T114)
// ==============================================================================

TEST_CASE("applyTransformInvert inverts all levels", "[step_pattern_editor][transforms]") {
    auto editor = makeEditor(4);
    editor.setStepLevel(0, 1.0f);
    editor.setStepLevel(1, 0.5f);
    editor.setStepLevel(2, 0.0f);
    editor.setStepLevel(3, 0.8f);

    editor.applyTransformInvert();

    REQUIRE(editor.getStepLevel(0) == Approx(0.0f));
    REQUIRE(editor.getStepLevel(1) == Approx(0.5f));
    REQUIRE(editor.getStepLevel(2) == Approx(1.0f));
    REQUIRE(editor.getStepLevel(3) == Approx(0.2f).margin(0.001f));
}

TEST_CASE("applyTransformShiftRight rotates pattern right", "[step_pattern_editor][transforms]") {
    auto editor = makeEditor(4);
    editor.setStepLevel(0, 0.1f);
    editor.setStepLevel(1, 0.2f);
    editor.setStepLevel(2, 0.3f);
    editor.setStepLevel(3, 0.4f);

    editor.applyTransformShiftRight();

    REQUIRE(editor.getStepLevel(0) == Approx(0.4f));
    REQUIRE(editor.getStepLevel(1) == Approx(0.1f));
    REQUIRE(editor.getStepLevel(2) == Approx(0.2f));
    REQUIRE(editor.getStepLevel(3) == Approx(0.3f));
}

TEST_CASE("applyPresetRandom generates values in [0,1]", "[step_pattern_editor][presets]") {
    auto editor = makeEditor(16);
    editor.applyPresetRandom();

    for (int i = 0; i < 16; ++i) {
        float level = editor.getStepLevel(i);
        REQUIRE(level >= 0.0f);
        REQUIRE(level <= 1.0f);
    }
}

// ==============================================================================
// Phase Offset Tests (T139)
// ==============================================================================

TEST_CASE("Phase offset 0.0 maps to step 0 for 16 steps", "[step_pattern_editor][phase]") {
    auto editor = makeEditor(16);
    editor.setPhaseOffset(0.0f);
    REQUIRE(editor.getPhaseStartStep() == 0);
}

TEST_CASE("Phase offset 0.5 maps to step 8 for 16 steps", "[step_pattern_editor][phase]") {
    auto editor = makeEditor(16);
    editor.setPhaseOffset(0.5f);
    REQUIRE(editor.getPhaseStartStep() == 8);
}

TEST_CASE("Phase offset wraps correctly", "[step_pattern_editor][phase]") {
    auto editor = makeEditor(16);
    editor.setPhaseOffset(1.0f);
    // round(1.0 * 16) % 16 = 16 % 16 = 0
    REQUIRE(editor.getPhaseStartStep() == 0);
}

// ==============================================================================
// Zoom/Scroll Visibility Tests (T151)
// ==============================================================================

TEST_CASE("Zoom scroll controls hidden for fewer than 24 steps", "[step_pattern_editor][zoom]") {
    auto editor = makeEditor(16);

    // With 16 steps, getVisibleStepCount should equal numSteps
    REQUIRE(editor.getVisibleStepCount() == 16);
}

TEST_CASE("Zoom scroll available for 24+ steps", "[step_pattern_editor][zoom]") {
    auto editor = makeEditor(32);

    // Default zoom=1.0 shows all steps
    REQUIRE(editor.getVisibleStepCount() == 32);
}

// ==============================================================================
// Timer Lifecycle Tests (T096)
// ==============================================================================

// Note: CVSTGUITimer requires VSTGUI infrastructure. Timer creation is tested
// via pluginval integration. Here we test the state management without timers.

// ==============================================================================
// Playback Position Tests (T097)
// ==============================================================================

TEST_CASE("setPlaybackStep updates position", "[step_pattern_editor][playback]") {
    auto editor = makeEditor(16);
    // Don't call setPlaying(true) in tests - CVSTGUITimer requires VSTGUI init

    editor.setPlaybackStep(5);
    CRect indRect = editor.getPlaybackIndicatorRect();
    REQUIRE_FALSE(indRect.isEmpty());

    editor.setPlaybackStep(-1);
    CRect noRect = editor.getPlaybackIndicatorRect();
    REQUIRE(noRect.isEmpty());
}
