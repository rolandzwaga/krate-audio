// =============================================================================
// PianoRollView Playhead Mapping Tests
// =============================================================================
// Spec 142 Phase 6 — focused tests for FR-034a (playhead param drives step
// highlight). Uses the humble-object logic header.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/piano_roll_view_logic.h"

using Catch::Approx;
namespace Logic = Gradus::PianoRollViewLogic;

TEST_CASE("PianoRollView: FR-034a playhead param maps to step index across length values",
          "[gradus][piano_roll][playhead][fr034a]")
{
    // Length = 16: param/0.5 lands at raw step 16 → clamped to 15.
    CHECK(Logic::playheadStepFromParam(0.0,  16) == 0);
    CHECK(Logic::playheadStepFromParam(0.25, 16) == 8);
    CHECK(Logic::playheadStepFromParam(0.5,  16) == 15);
    CHECK(Logic::playheadStepFromParam(1.0,  16) == 15);

    // Length = 32: full range reachable.
    CHECK(Logic::playheadStepFromParam(0.0,     32) == 0);
    CHECK(Logic::playheadStepFromParam(0.25,    32) == 8);
    CHECK(Logic::playheadStepFromParam(0.5,     32) == 16);
    CHECK(Logic::playheadStepFromParam(0.96875, 32) == 31);
    CHECK(Logic::playheadStepFromParam(1.0,     32) == 31);

    // Length = 8: param/0.25 lands at raw step 8 → clamped to 7.
    CHECK(Logic::playheadStepFromParam(0.0,  8) == 0);
    CHECK(Logic::playheadStepFromParam(0.25, 8) == 7);
    CHECK(Logic::playheadStepFromParam(0.5,  8) == 7);
    CHECK(Logic::playheadStepFromParam(1.0,  8) == 7);

    // Length = 1: any non-zero param value lands on step 0.
    CHECK(Logic::playheadStepFromParam(0.0, 1) == 0);
    CHECK(Logic::playheadStepFromParam(0.5, 1) == 0);
    CHECK(Logic::playheadStepFromParam(1.0, 1) == 0);
}

TEST_CASE("PianoRollView: FR-034a playhead clamps to active length when length shrinks",
          "[gradus][piano_roll][playhead][fr034a]")
{
    // Audio thread reports the lane is at raw step 24 (param value 0.75 * 32 = 24).
    // UI's active length is currently 12 (user shrunk the lane mid-playback).
    // The cursor must clamp into the visible area.
    CHECK(Logic::playheadStepFromParam(0.75, 12) == 11);

    // Same param value, length=32 → cursor sits at step 24.
    CHECK(Logic::playheadStepFromParam(0.75, 32) == 24);

    // Param value just barely under the new max — still inside range.
    CHECK(Logic::playheadStepFromParam(0.21875, 12) == 7); // 0.21875 * 32 = 7
}

TEST_CASE("PianoRollView: FR-034a playhead rejects invalid lengths",
          "[gradus][piano_roll][playhead][fr034a]")
{
    CHECK(Logic::playheadStepFromParam(0.5, 0)  == -1);
    CHECK(Logic::playheadStepFromParam(0.5, -1) == -1);
}

TEST_CASE("PianoRollView: FR-034a playhead clamps negative / >1 normalized values",
          "[gradus][piano_roll][playhead][fr034a]")
{
    CHECK(Logic::playheadStepFromParam(-0.5, 16) == 0);
    CHECK(Logic::playheadStepFromParam(2.0,  16) == 15);
}
