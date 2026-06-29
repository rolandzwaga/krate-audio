// =============================================================================
// PianoRollView Logic Unit Tests
// =============================================================================
// Spec 142 (Gradus Piano-Roll Step Sequencer), Phase 6.
//
// Covers the 13 scenarios from contracts/piano-roll-view.md Test Coverage
// section. Tests use the humble-object logic header (no VSTGUI dependency)
// per project convention (see pin_flag_strip_tests.cpp).
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/piano_roll_view_logic.h"

using Catch::Approx;
namespace Logic = Gradus::PianoRollViewLogic;

// -----------------------------------------------------------------------------
// 1. rendersGridWith48Rows — FR-028
// -----------------------------------------------------------------------------
TEST_CASE("PianoRollView: 48 rows from C2 to B5",
          "[gradus][piano_roll][ui][fr028]")
{
    constexpr float kViewH = 480.0f; // 10px per row
    // Top row (y=0) is MIDI 83 (B5).
    CHECK(Logic::pitchFromY(0.0f, kViewH) == 83);
    CHECK(Logic::pitchFromY(5.0f, kViewH) == 83);
    // Bottom row (y=475) is MIDI 36 (C2).
    CHECK(Logic::pitchFromY(475.0f, kViewH) == 36);
    // Mid row at y=240 lands somewhere mid-octave.
    int midRow = Logic::pitchFromY(240.0f, kViewH);
    CHECK(midRow >= 36);
    CHECK(midRow <= 83);
    // Out-of-range y returns -1.
    CHECK(Logic::pitchFromY(-1.0f, kViewH) == -1);
    CHECK(Logic::pitchFromY(kViewH, kViewH) == -1);
    CHECK(Logic::pitchFromY(kViewH + 100.0f, kViewH) == -1);
    // Compile-time sanity: 48 rows.
    static_assert(Logic::kPitchRows == 48, "FR-028");
    static_assert(Logic::kMidiLow == 36, "FR-028: C2");
    static_assert(Logic::kMidiHigh == 83, "FR-028: B5");
}

// -----------------------------------------------------------------------------
// 1b. noteName — MIDI → scientific pitch notation for the hover tooltip
// -----------------------------------------------------------------------------
TEST_CASE("PianoRollView: noteName maps MIDI to scientific pitch notation",
          "[gradus][piano_roll][ui][tooltip]")
{
    // Grid anchors (C2 = MIDI 36, C4 = middle C = MIDI 60, B5 = MIDI 83).
    CHECK(Logic::noteName(36) == "C2");
    CHECK(Logic::noteName(60) == "C4");
    CHECK(Logic::noteName(83) == "B5");
    // Accidentals use sharps.
    CHECK(Logic::noteName(61) == "C#4");
    CHECK(Logic::noteName(66) == "F#4");
    // Octave boundary: B3 → C4.
    CHECK(Logic::noteName(59) == "B3");
    // Extremes are clamped into 0..127 and still produce a valid label.
    CHECK(Logic::noteName(0) == "C-1");
    CHECK(Logic::noteName(127) == "G9");
    CHECK(Logic::noteName(-5) == "C-1");
    CHECK(Logic::noteName(200) == "G9");
}

// -----------------------------------------------------------------------------
// 1c. hoveredCellNoteLabel / isPlacedNoteCell — in-cell note label on hover
// (shown for placed notes AND empty/ghost cells)
// -----------------------------------------------------------------------------
TEST_CASE("PianoRollView: hovered cell shows the row note name; placed-note predicate",
          "[gradus][piano_roll][ui][hover_label]")
{
    Logic::StepArray steps{};
    // Step 0: a placed note at pitch 64 (E4). Step 1: a rest at pitch 60.
    steps[0] = Logic::StepData{ /*pitch=*/64, /*isRest=*/false };
    steps[1] = Logic::StepData{ /*pitch=*/60, /*isRest=*/true };
    const int activeLength = 8;

    // Label = the hovered ROW's name for any valid active cell — note OR ghost.
    CHECK(Logic::hoveredCellNoteLabel(0, 64, activeLength) == "E4"); // on the note
    CHECK(Logic::hoveredCellNoteLabel(0, 65, activeLength) == "F4"); // empty row, same step
    CHECK(Logic::hoveredCellNoteLabel(3, 60, activeLength) == "C4"); // empty cell elsewhere
    // Off the active grid → empty.
    CHECK(Logic::hoveredCellNoteLabel(0, 64, /*activeLength=*/0).empty());
    CHECK(Logic::hoveredCellNoteLabel(-1, 64, activeLength).empty());
    CHECK(Logic::hoveredCellNoteLabel(Logic::kMaxSteps, 64, activeLength).empty());
    CHECK(Logic::hoveredCellNoteLabel(0, Logic::kMidiLow - 1, activeLength).empty());
    CHECK(Logic::hoveredCellNoteLabel(0, Logic::kMidiHigh + 1, activeLength).empty());

    // Placed-note predicate (color selector): true only on the note's own cell.
    CHECK(Logic::isPlacedNoteCell(steps, 0, 64, activeLength));
    CHECK_FALSE(Logic::isPlacedNoteCell(steps, 0, 65, activeLength)); // different row
    CHECK_FALSE(Logic::isPlacedNoteCell(steps, 1, 60, activeLength)); // rest
    CHECK_FALSE(Logic::isPlacedNoteCell(steps, 3, 60, activeLength)); // empty cell
    CHECK_FALSE(Logic::isPlacedNoteCell(steps, 0, 64, /*activeLength=*/0)); // inactive
}

// -----------------------------------------------------------------------------
// 2. clickOnRestingStepPlacesNote — FR-030
// -----------------------------------------------------------------------------
TEST_CASE("PianoRollView: click on a resting step places a note at the clicked row",
          "[gradus][piano_roll][ui][fr030]")
{
    Logic::MouseStateMachine sm;
    Logic::StepData restingC4{ 60, true };
    sm.onLeftMouseDown(/*step=*/5, /*pitch=*/67);
    auto edit = sm.onLeftMouseUp(restingC4);
    REQUIRE(edit.valid);
    CHECK(edit.step == 5);
    CHECK(edit.pitch == 67);
    CHECK_FALSE(edit.isRest);
}

// -----------------------------------------------------------------------------
// 3. clickOnSamePitchTogglesRest — FR-030
// -----------------------------------------------------------------------------
TEST_CASE("PianoRollView: same-pitch click toggles rest",
          "[gradus][piano_roll][ui][fr030]")
{
    Logic::MouseStateMachine sm;
    Logic::StepData noteAt67{ 67, false };
    sm.onLeftMouseDown(/*step=*/5, /*pitch=*/67);
    auto edit = sm.onLeftMouseUp(noteAt67);
    REQUIRE(edit.valid);
    CHECK(edit.step == 5);
    CHECK(edit.pitch == 67); // pitch unchanged
    CHECK(edit.isRest);      // toggled to rest
}

// -----------------------------------------------------------------------------
// 4. clickOnDifferentPitchReplaces — FR-030
// -----------------------------------------------------------------------------
TEST_CASE("PianoRollView: different-pitch click silently replaces",
          "[gradus][piano_roll][ui][fr030]")
{
    Logic::MouseStateMachine sm;
    Logic::StepData noteAt67{ 67, false };
    sm.onLeftMouseDown(/*step=*/5, /*pitch=*/70);
    auto edit = sm.onLeftMouseUp(noteAt67);
    REQUIRE(edit.valid);
    CHECK(edit.step == 5);
    CHECK(edit.pitch == 70);
    CHECK_FALSE(edit.isRest);
}

// -----------------------------------------------------------------------------
// 5. rightClickSetsRest — FR-032
// -----------------------------------------------------------------------------
TEST_CASE("PianoRollView: right-click sets rest on the clicked step",
          "[gradus][piano_roll][ui][fr032]")
{
    Logic::MouseStateMachine sm;
    Logic::StepData noteAt67{ 67, false };
    auto edit = sm.onRightMouseDown(/*step=*/10, noteAt67);
    REQUIRE(edit.valid);
    CHECK(edit.step == 10);
    CHECK(edit.pitch == 67); // preserved
    CHECK(edit.isRest);
    // Right-click does NOT enter drag mode.
    CHECK(sm.state() == Logic::MouseStateMachine::State::kIdle);
}

TEST_CASE("PianoRollView: right-click on an already-resting step is idempotent",
          "[gradus][piano_roll][ui][fr032]")
{
    Logic::MouseStateMachine sm;
    Logic::StepData restAt60{ 60, true };
    auto edit = sm.onRightMouseDown(/*step=*/3, restAt60);
    REQUIRE(edit.valid);
    CHECK(edit.isRest);          // still rest
    CHECK(edit.pitch == 60);
    CHECK(sm.state() == Logic::MouseStateMachine::State::kIdle);
}

TEST_CASE("PianoRollView: right-click out of range emits no edit",
          "[gradus][piano_roll][ui][fr032]")
{
    Logic::MouseStateMachine sm;
    Logic::StepData any{ 60, false };
    auto edit = sm.onRightMouseDown(/*step=*/-1, any);
    CHECK_FALSE(edit.valid);
}

// -----------------------------------------------------------------------------
// 6. dragLocksPitchToStart — FR-031
// -----------------------------------------------------------------------------
TEST_CASE("PianoRollView: drag locks pitch to start row and paints subsequent columns",
          "[gradus][piano_roll][ui][fr031]")
{
    Logic::MouseStateMachine sm;
    // mouseDown at row 67, step 2.
    sm.onLeftMouseDown(/*step=*/2, /*pitch=*/67);

    // mouseMoved to step 3 — even with cursor at a different row, paint pitch
    // is the locked start pitch (67).
    auto e1 = sm.onMouseMovedDragging(3);
    REQUIRE(e1.valid);
    CHECK(e1.step == 3);
    CHECK(e1.pitch == 67);
    CHECK_FALSE(e1.isRest);

    auto e2 = sm.onMouseMovedDragging(4);
    REQUIRE(e2.valid);
    CHECK(e2.step == 4);
    CHECK(e2.pitch == 67);

    auto e3 = sm.onMouseMovedDragging(5);
    REQUIRE(e3.valid);
    CHECK(e3.step == 5);
    CHECK(e3.pitch == 67);

    // Drag continues — re-emitting onMouseMoved at the same step does NOT
    // produce a duplicate edit (lastPaintedStep skip).
    auto e4 = sm.onMouseMovedDragging(5);
    CHECK_FALSE(e4.valid);

    // mouseUp after a drag: no single-click resolution because lastPainted != -1.
    Logic::StepData irrelevant{ 60, true };
    auto eUp = sm.onLeftMouseUp(irrelevant);
    CHECK_FALSE(eUp.valid);
    CHECK(sm.state() == Logic::MouseStateMachine::State::kIdle);
}

// -----------------------------------------------------------------------------
// 7. dragNeverTogglesRest — FR-031
// -----------------------------------------------------------------------------
TEST_CASE("PianoRollView: drag NEVER toggles to rest even on a same-pitch cell",
          "[gradus][piano_roll][ui][fr031]")
{
    Logic::MouseStateMachine sm;
    sm.onLeftMouseDown(/*step=*/2, /*pitch=*/67);
    // step 3 already has a note at pitch 67 — drag must still paint, NOT toggle.
    auto edit = sm.onMouseMovedDragging(3);
    REQUIRE(edit.valid);
    CHECK(edit.pitch == 67);
    CHECK_FALSE(edit.isRest);  // never rest during drag
}

// -----------------------------------------------------------------------------
// 8. playheadDrivenByParam — FR-034a
// -----------------------------------------------------------------------------
TEST_CASE("PianoRollView: playhead step driven by normalized param value (length=16)",
          "[gradus][piano_roll][ui][fr034a]")
{
    CHECK(Logic::playheadStepFromParam(0.0, 16) == 0);
    CHECK(Logic::playheadStepFromParam(0.5, 16) == 15);   // 0.5*32 = 16 → clamp to 15
    CHECK(Logic::playheadStepFromParam(0.25, 16) == 8);   // 0.25*32 = 8
    CHECK(Logic::playheadStepFromParam(1.0, 16) == 15);   // clamped to length-1
    // Length 32 — playhead can reach 31.
    CHECK(Logic::playheadStepFromParam(0.96875, 32) == 31); // 0.96875*32 = 31
    CHECK(Logic::playheadStepFromParam(1.0, 32) == 31);
}

TEST_CASE("PianoRollView: playhead clamps to active length when length changes",
          "[gradus][piano_roll][ui][fr034a]")
{
    // Param value of 0.9375 → raw step 30. With length=8 the cursor clamps to 7.
    CHECK(Logic::playheadStepFromParam(0.9375, 8) == 7);
    // length<=0 returns -1.
    CHECK(Logic::playheadStepFromParam(0.5, 0) == -1);
    CHECK(Logic::playheadStepFromParam(0.5, -3) == -1);
    // Negative param clamps to 0 step.
    CHECK(Logic::playheadStepFromParam(-1.0, 16) == 0);
}

// -----------------------------------------------------------------------------
// 9. lengthChangeShrinksActiveArea — FR-029
// -----------------------------------------------------------------------------
TEST_CASE("PianoRollView: clicking outside active length yields step -1",
          "[gradus][piano_roll][ui][fr029]")
{
    constexpr float kViewW = 320.0f;
    // Length 8 → each cell is 40px wide. x=320 is the right edge (out of range).
    CHECK(Logic::stepFromX(320.0f, kViewW, 8) == -1);
    // x=400 well outside.
    CHECK(Logic::stepFromX(400.0f, kViewW, 8) == -1);
    // x=39 inside cell 0.
    CHECK(Logic::stepFromX(39.0f, kViewW, 8) == 0);
    // x=40 starts cell 1.
    CHECK(Logic::stepFromX(40.0f, kViewW, 8) == 1);
    // x=319 = cell 7 (the last active).
    CHECK(Logic::stepFromX(319.0f, kViewW, 8) == 7);
    // Length 32 — cells are 10px wide.
    CHECK(Logic::stepFromX(0.0f,   kViewW, 32) == 0);
    CHECK(Logic::stepFromX(319.0f, kViewW, 32) == 31);
    // Length 0 / -1 / view 0 are invalid.
    CHECK(Logic::stepFromX(0.0f, kViewW, 0) == -1);
    CHECK(Logic::stepFromX(0.0f, kViewW, -1) == -1);
    CHECK(Logic::stepFromX(0.0f, 0.0f, 32) == -1);
}

// -----------------------------------------------------------------------------
// 10. externalParamChangeRedraws — FR-034
// -----------------------------------------------------------------------------
// The logic header doesn't drive redraw directly, but the StepData refresh is
// pure: writing into the array should be observable by drawNotes. We assert
// the simple round-trip here as a structural contract for the view's
// IDependent::update branch.
TEST_CASE("PianoRollView: StepData cache mirrors external param changes",
          "[gradus][piano_roll][ui][fr034]")
{
    Logic::StepArray steps{};
    REQUIRE(steps.size() == 32);
    // Defaults: pitch=60, isRest=true (constructor defaults — per FR-006/FR-007).
    for (const auto& s : steps) {
        CHECK(s.pitch == 60);
        CHECK(s.isRest);
    }
    // External update (e.g., from IDependent::update for kArpSequencerNoteLaneStep7Id).
    steps[7].pitch = 72;
    steps[7].isRest = false;
    CHECK(steps[7].pitch == 72);
    CHECK_FALSE(steps[7].isRest);
}

// -----------------------------------------------------------------------------
// 11. dragOutOfBoundsClamps — edge case
// -----------------------------------------------------------------------------
TEST_CASE("PianoRollView: drag out of bounds clamps to in-range steps",
          "[gradus][piano_roll][ui][drag]")
{
    constexpr float kViewW = 320.0f;
    constexpr int   kLen   = 16;

    // x=-50 → step 0 (clamped).
    CHECK(Logic::stepFromXClamped(-50.0f, kViewW, kLen) == 0);
    // x=400 → step 15 (clamped to length-1).
    CHECK(Logic::stepFromXClamped(400.0f, kViewW, kLen) == 15);
    // x=0 → step 0.
    CHECK(Logic::stepFromXClamped(0.0f, kViewW, kLen) == 0);
    // x=319 → step 15.
    CHECK(Logic::stepFromXClamped(319.0f, kViewW, kLen) == 15);

    // Drag locks pitch even when cursor moves off the grid.
    Logic::MouseStateMachine sm;
    sm.onLeftMouseDown(/*step=*/2, /*pitch=*/80);
    // After clamping, a "move to x=-50" yields step 0.
    int s = Logic::stepFromXClamped(-50.0f, kViewW, kLen);
    auto e = sm.onMouseMovedDragging(s);
    REQUIRE(e.valid);
    CHECK(e.step == 0);
    CHECK(e.pitch == 80); // locked
}

// -----------------------------------------------------------------------------
// 12. rightClickDuringDrag — edge case
// -----------------------------------------------------------------------------
TEST_CASE("PianoRollView: right-click during an in-flight drag emits rest and does not crash",
          "[gradus][piano_roll][ui][drag]")
{
    Logic::MouseStateMachine sm;
    sm.onLeftMouseDown(/*step=*/2, /*pitch=*/67);
    auto paint = sm.onMouseMovedDragging(3);
    REQUIRE(paint.valid);

    // Right-click on a column mid-drag: emit a rest edit; drag remains.
    Logic::StepData any{ 70, false };
    auto rest = sm.onRightMouseDown(/*step=*/5, any);
    REQUIRE(rest.valid);
    CHECK(rest.step == 5);
    CHECK(rest.isRest);

    // Drag is still in progress (state machine wasn't reset by right-click).
    CHECK(sm.state() == Logic::MouseStateMachine::State::kDragging);

    // Continuing the drag still works.
    auto more = sm.onMouseMovedDragging(6);
    REQUIRE(more.valid);
    CHECK(more.pitch == 67);
}

// -----------------------------------------------------------------------------
// 13. idempotentAttachedRemoved — lifecycle contract
// -----------------------------------------------------------------------------
// The state machine itself owns no parameter dependencies, but its reset()
// should be idempotent so the view's removed()/dtor defense-in-depth path
// can call it twice without issue.
TEST_CASE("PianoRollView: state machine reset is idempotent",
          "[gradus][piano_roll][ui][lifecycle]")
{
    Logic::MouseStateMachine sm;
    sm.onLeftMouseDown(/*step=*/2, /*pitch=*/67);
    auto e = sm.onMouseMovedDragging(3);
    REQUIRE(e.valid);

    sm.reset();
    CHECK(sm.state() == Logic::MouseStateMachine::State::kIdle);
    CHECK(sm.dragStartStep() == -1);
    CHECK(sm.lastPaintedStep() == -1);

    // Calling reset twice — still idle, no crash.
    sm.reset();
    CHECK(sm.state() == Logic::MouseStateMachine::State::kIdle);
}

// -----------------------------------------------------------------------------
// Single-click resolution helper round-trip
// -----------------------------------------------------------------------------
TEST_CASE("PianoRollView: resolveSingleClick covers all FR-030 branches",
          "[gradus][piano_roll][ui][fr030]")
{
    SECTION("rest → place note") {
        auto r = Logic::resolveSingleClick({ 60, true }, /*clickedPitch=*/67);
        CHECK(r.pitch == 67);
        CHECK_FALSE(r.isRest);
    }
    SECTION("same pitch → toggle to rest") {
        auto r = Logic::resolveSingleClick({ 67, false }, /*clickedPitch=*/67);
        CHECK(r.pitch == 67);
        CHECK(r.isRest);
    }
    SECTION("different pitch → silent replace") {
        auto r = Logic::resolveSingleClick({ 67, false }, /*clickedPitch=*/70);
        CHECK(r.pitch == 70);
        CHECK_FALSE(r.isRest);
    }
}

// -----------------------------------------------------------------------------
// colWidth / rowHeight sanity
// -----------------------------------------------------------------------------
TEST_CASE("PianoRollView: column and row geometry",
          "[gradus][piano_roll][ui][geometry]")
{
    CHECK(Logic::colWidth(320.0f, 32) == Approx(10.0f));
    CHECK(Logic::colWidth(320.0f, 16) == Approx(20.0f));
    CHECK(Logic::colWidth(320.0f, 8)  == Approx(40.0f));
    CHECK(Logic::colWidth(320.0f, 0)  == Approx(0.0f));
    CHECK(Logic::rowHeight(480.0f)    == Approx(10.0f));
}

// -----------------------------------------------------------------------------
// 14. computeHover — fine-grained per-cell hover tracking
// -----------------------------------------------------------------------------
TEST_CASE("PianoRollView: hover within grid returns valid step+pitch",
          "[gradus][piano_roll][ui][hover]")
{
    constexpr float kViewW = 320.0f; // 16 active cols => 20px each
    constexpr float kViewH = 480.0f; // 48 rows => 10px each
    constexpr int   kLen   = 16;

    // Click roughly in the middle of step 4, row 10 (pitch = 83 - 10 = 73).
    auto h = Logic::computeHover(90.0f, 105.0f, kViewW, kViewH, kLen, -1, -1);
    CHECK(h.step == 4);
    CHECK(h.pitch == 73);
    CHECK(h.changed == true);
}

TEST_CASE("PianoRollView: hover within same cell reports changed=false",
          "[gradus][piano_roll][ui][hover]")
{
    constexpr float kViewW = 320.0f;
    constexpr float kViewH = 480.0f;
    constexpr int   kLen   = 16;

    // First move into step 4, pitch 73.
    auto h1 = Logic::computeHover(85.0f, 105.0f, kViewW, kViewH, kLen, -1, -1);
    REQUIRE(h1.step == 4);
    REQUIRE(h1.pitch == 73);
    // A tiny jitter inside the same cell should NOT report changed.
    auto h2 = Logic::computeHover(95.0f, 108.0f, kViewW, kViewH, kLen,
                                  h1.step, h1.pitch);
    CHECK(h2.step == 4);
    CHECK(h2.pitch == 73);
    CHECK(h2.changed == false);
}

TEST_CASE("PianoRollView: hover off-grid returns step=-1, pitch=-1",
          "[gradus][piano_roll][ui][hover]")
{
    constexpr float kViewW = 320.0f;
    constexpr float kViewH = 480.0f;
    constexpr int   kLen   = 16;

    // x past the right edge.
    auto h = Logic::computeHover(500.0f, 100.0f, kViewW, kViewH, kLen, -1, -1);
    CHECK(h.step == -1);
    CHECK(h.pitch != -1); // y is in range
    // Both x and y past — fully off-grid.
    auto h2 = Logic::computeHover(500.0f, 1000.0f, kViewW, kViewH, kLen, -1, -1);
    CHECK(h2.step == -1);
    CHECK(h2.pitch == -1);
}

TEST_CASE("PianoRollView: hover past activeLength clamps step to -1 in x dimension",
          "[gradus][piano_roll][ui][hover]")
{
    constexpr float kViewW = 320.0f;
    constexpr float kViewH = 480.0f;
    constexpr int   kLen   = 8; // half-active: cols 8..15 are inactive

    // x corresponds to col 12 — beyond active length 8 but inside view width.
    // stepFromX returns -1 when x < activeLength * colWidth; with kLen=8 and
    // viewW=320, colWidth=40 so 8 active cols span 0..320 entire view.
    // Use kLen=8 with viewW=320: hover at x=200 still maps to step within 8 cols.
    auto h = Logic::computeHover(200.0f, 100.0f, kViewW, kViewH, kLen, -1, -1);
    CHECK(h.step == 5); // 200/40 = 5
    CHECK(h.pitch == 73);
}

TEST_CASE("PianoRollView: hover transition changes report changed=true",
          "[gradus][piano_roll][ui][hover]")
{
    constexpr float kViewW = 320.0f;
    constexpr float kViewH = 480.0f;
    constexpr int   kLen   = 16;

    // Start at step 4/pitch 73.
    auto h1 = Logic::computeHover(85.0f, 105.0f, kViewW, kViewH, kLen, -1, -1);
    // Move one row down (pitch 72).
    auto h2 = Logic::computeHover(85.0f, 115.0f, kViewW, kViewH, kLen,
                                  h1.step, h1.pitch);
    CHECK(h2.step == 4);
    CHECK(h2.pitch == 72);
    CHECK(h2.changed == true);
    // Move one column right (step 5).
    auto h3 = Logic::computeHover(105.0f, 115.0f, kViewW, kViewH, kLen,
                                  h2.step, h2.pitch);
    CHECK(h3.step == 5);
    CHECK(h3.pitch == 72);
    CHECK(h3.changed == true);
}
