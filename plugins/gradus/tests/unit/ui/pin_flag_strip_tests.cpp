// =============================================================================
// PinFlagStrip Logic Unit Tests
// =============================================================================
// Spec 133 (Gradus v1.6) — inline 32-cell pin toggle row.
//
// Humble-object pattern: all testable logic lives in pin_flag_strip_logic.h
// with no VSTGUI dependency. The CControl subclass (pin_flag_strip.h) is a
// thin wrapper that delegates to these free functions and is NOT instantiated
// in unit tests (matches Iterum's tap_pattern_editor_logic.h convention).
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/pin_flag_strip_logic.h"
#include "plugin_ids.h"

using Catch::Approx;
namespace Logic = Gradus::PinFlagStripLogic;

// -----------------------------------------------------------------------------
// cellIndexForX hit test
// -----------------------------------------------------------------------------

TEST_CASE("Gradus pin flag strip maps X to step index at full length",
          "[gradus][pin][ui]")
{
    // width 320, 32 cells → each cell is 10 wide → cell N covers [10N, 10N+10)
    CHECK(Logic::cellIndexForX(0.0f,   320.0f, 32) == 0);
    CHECK(Logic::cellIndexForX(5.0f,   320.0f, 32) == 0);
    CHECK(Logic::cellIndexForX(10.0f,  320.0f, 32) == 1);
    CHECK(Logic::cellIndexForX(160.0f, 320.0f, 32) == 16);
    CHECK(Logic::cellIndexForX(319.0f, 320.0f, 32) == 31);
}

TEST_CASE("Gradus pin flag strip cell width scales with active step count",
          "[gradus][pin][ui]")
{
    // Active length 16, same 320px wide → each cell is 20 wide
    CHECK(Logic::cellIndexForX(0.0f,   320.0f, 16) == 0);
    CHECK(Logic::cellIndexForX(19.0f,  320.0f, 16) == 0);
    CHECK(Logic::cellIndexForX(20.0f,  320.0f, 16) == 1);
    CHECK(Logic::cellIndexForX(160.0f, 320.0f, 16) == 8);
    CHECK(Logic::cellIndexForX(319.0f, 320.0f, 16) == 15);

    // Active length 8 → each cell is 40 wide
    CHECK(Logic::cellIndexForX(0.0f,   320.0f, 8) == 0);
    CHECK(Logic::cellIndexForX(39.0f,  320.0f, 8) == 0);
    CHECK(Logic::cellIndexForX(40.0f,  320.0f, 8) == 1);
    CHECK(Logic::cellIndexForX(319.0f, 320.0f, 8) == 7);

    // Active length 1 → single cell spans the whole width
    CHECK(Logic::cellIndexForX(0.0f,   320.0f, 1) == 0);
    CHECK(Logic::cellIndexForX(319.0f, 320.0f, 1) == 0);
}

TEST_CASE("Gradus pin flag strip rejects out-of-range X and invalid step counts",
          "[gradus][pin][ui]")
{
    CHECK(Logic::cellIndexForX(-1.0f,  320.0f, 32) == -1);
    CHECK(Logic::cellIndexForX(320.0f, 320.0f, 32) == -1);
    CHECK(Logic::cellIndexForX(500.0f, 320.0f, 32) == -1);
    CHECK(Logic::cellIndexForX(0.0f,   0.0f,   32) == -1);
    CHECK(Logic::cellIndexForX(0.0f,  -1.0f,   32) == -1);

    // Invalid numSteps
    CHECK(Logic::cellIndexForX(100.0f, 320.0f,  0) == -1);
    CHECK(Logic::cellIndexForX(100.0f, 320.0f, -1) == -1);
    CHECK(Logic::cellIndexForX(100.0f, 320.0f, 33) == -1);  // > kNumSteps
}

// -----------------------------------------------------------------------------
// toggleStep + state
// -----------------------------------------------------------------------------

TEST_CASE("Gradus pin flag strip toggles a cell value",
          "[gradus][pin][ui]")
{
    Logic::State state{};

    // Default: all zeros
    for (int i = 0; i < Logic::kNumSteps; ++i) {
        CHECK(Logic::getStepValue(state, i) == Approx(0.0f));
    }

    CHECK(Logic::toggleStep(state, 5) == Approx(1.0f));
    CHECK(Logic::getStepValue(state, 5) == Approx(1.0f));

    CHECK(Logic::toggleStep(state, 5) == Approx(0.0f));
    CHECK(Logic::getStepValue(state, 5) == Approx(0.0f));

    // Other cells unaffected
    Logic::toggleStep(state, 0);
    Logic::toggleStep(state, 31);
    CHECK(Logic::getStepValue(state, 0) == Approx(1.0f));
    CHECK(Logic::getStepValue(state, 31) == Approx(1.0f));
    CHECK(Logic::getStepValue(state, 15) == Approx(0.0f));
}

TEST_CASE("Gradus pin flag strip toggleStep is a no-op for out-of-range steps",
          "[gradus][pin][ui]")
{
    Logic::State state{};
    Logic::toggleStep(state, -1);
    Logic::toggleStep(state, 32);
    Logic::toggleStep(state, 999);
    for (int i = 0; i < Logic::kNumSteps; ++i) {
        CHECK(Logic::getStepValue(state, i) == Approx(0.0f));
    }
}

// -----------------------------------------------------------------------------
// setStepValue (host-side sync)
// -----------------------------------------------------------------------------

TEST_CASE("Gradus pin flag strip setStepValue reports state changes",
          "[gradus][pin][ui]")
{
    Logic::State state{};

    // First write: changed = true
    CHECK(Logic::setStepValue(state, 3, 1.0f) == true);
    CHECK(Logic::getStepValue(state, 3) == Approx(1.0f));

    // Idempotent write: changed = false
    CHECK(Logic::setStepValue(state, 3, 1.0f) == false);

    // Write back to 0: changed = true
    CHECK(Logic::setStepValue(state, 3, 0.0f) == true);
    CHECK(Logic::getStepValue(state, 3) == Approx(0.0f));
}

TEST_CASE("Gradus pin flag strip threshold matches backend at 0.5",
          "[gradus][pin][ui]")
{
    // The backend denormalizer in arpeggiator_params.h uses `value >= 0.5`.
    // The UI threshold must match, otherwise host automation passing exactly
    // 0.5 would desync the widget from the audio engine.
    Logic::State state{};

    Logic::setStepValue(state, 0, 0.3f);
    CHECK(Logic::getStepValue(state, 0) == Approx(0.0f));

    Logic::setStepValue(state, 0, 0.7f);
    CHECK(Logic::getStepValue(state, 0) == Approx(1.0f));

    // Reset and test exact 0.5 (should pin, matching backend)
    state = Logic::State{};
    Logic::setStepValue(state, 0, 0.5f);
    CHECK(Logic::getStepValue(state, 0) == Approx(1.0f));
}

TEST_CASE("Gradus pin flag strip setStepValue is a no-op for out-of-range steps",
          "[gradus][pin][ui]")
{
    Logic::State state{};
    CHECK(Logic::setStepValue(state, -1, 1.0f) == false);
    CHECK(Logic::setStepValue(state, 32, 1.0f) == false);
    CHECK(Logic::setStepValue(state, 999, 1.0f) == false);
    for (int i = 0; i < Logic::kNumSteps; ++i) {
        CHECK(Logic::getStepValue(state, i) == Approx(0.0f));
    }
}

// -----------------------------------------------------------------------------
// Echo loop invariant (click → host sync → no redraw thrash)
// -----------------------------------------------------------------------------

TEST_CASE("Gradus pin flag strip survives echo loop from click through host sync",
          "[gradus][pin][ui]")
{
    // Models the full user-click pipeline:
    //   1. User clicks cell → toggleStep returns new value
    //   2. Widget fires paramCallback → Controller::setParamNormalized()
    //   3. Controller's override forwards back to Widget::setStepValue()
    //
    // The echo at step 3 must be idempotent: the state already matches what
    // the click just set, so setStepValue must return false (no redraw).
    // This guarantees no recursive redraw thrash on every user interaction.

    Logic::State state{};
    int paramCallbackCount = 0;
    int redrawRequests = 0;

    auto simulateUserClick = [&](int step) {
        const float newValue = Logic::toggleStep(state, step);
        ++paramCallbackCount;
        // Host echoes back via setParamNormalized → setStepValue
        if (Logic::setStepValue(state, step, newValue)) {
            ++redrawRequests;  // Should never happen — state already matches
        }
    };

    simulateUserClick(5);
    CHECK(Logic::getStepValue(state, 5) == Approx(1.0f));
    CHECK(paramCallbackCount == 1);
    CHECK(redrawRequests == 0);

    simulateUserClick(5);
    CHECK(Logic::getStepValue(state, 5) == Approx(0.0f));
    CHECK(paramCallbackCount == 2);
    CHECK(redrawRequests == 0);

    // Stress: toggle every cell, echo back each time
    for (int step = 0; step < Logic::kNumSteps; ++step) {
        simulateUserClick(step);
    }
    CHECK(redrawRequests == 0);
}
