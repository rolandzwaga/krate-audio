// =============================================================================
// PresetDataSource Selection Logic Tests
// =============================================================================
// Tests for selection toggle behavior in preset browser
//
// CRITICAL: CDataBrowser calls setSelectedRow() BEFORE dbOnMouseDown()!
// This means dbSelectionChanged fires BEFORE our mouse handler runs.
// Tests MUST simulate this real call order to catch bugs.
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include "ui/preset_browser_logic.h"
#include "ui/preset_data_source.h"
#include "vstgui/lib/cdatabrowser.h"

using namespace Krate::Plugins;

// =============================================================================
// Mock CDataBrowser that simulates REAL behavior
// =============================================================================
// CRITICAL: CDataBrowser updates selection BEFORE calling delegate!
// See vstgui4/vstgui/lib/cdatabrowser.cpp lines 930-932:
//   browser->setSelectedRow(cell.row);  // Selection updated FIRST
//   return db->dbOnMouseDown(...);       // THEN delegate called
// =============================================================================

class MockCDataBrowserBehavior {
public:
    // Simulates clicking a row with REAL CDataBrowser call order
    static VSTGUI::CMouseEventResult simulateRowClick(
        PresetDataSource& dataSource,
        VSTGUI::CDataBrowser* browser,
        int32_t clickedRow,
        bool isDoubleClick = false
    ) {
        // Step 1: CDataBrowser updates selection FIRST (this triggers dbSelectionChanged)
        browser->setSelectedRow(clickedRow);

        // Step 2: THEN CDataBrowser calls delegate's dbOnMouseDown
        VSTGUI::CPoint where(0, 0);
        VSTGUI::CButtonState buttons;
        if (isDoubleClick) {
            // Simulate double-click button state
            buttons = VSTGUI::CButtonState(VSTGUI::kLButton | VSTGUI::kDoubleClick);
        } else {
            buttons = VSTGUI::CButtonState(VSTGUI::kLButton);
        }

        return dataSource.dbOnMouseDown(where, buttons, clickedRow, 0, browser);
    }
};

// =============================================================================
// Unit Tests: Pure Function
// =============================================================================

TEST_CASE("determineSelectionAction pure function", "[ui][preset-browser]") {

    SECTION("no prior selection - allow default selection behavior") {
        // When nothing was selected before, clicking any row should allow
        // the browser to handle selection normally
        REQUIRE(determineSelectionAction(0, -1) == SelectionAction::AllowDefault);
        REQUIRE(determineSelectionAction(5, -1) == SelectionAction::AllowDefault);
        REQUIRE(determineSelectionAction(99, -1) == SelectionAction::AllowDefault);
    }

    SECTION("clicking the already-selected row - should deselect") {
        // When clicking on the same row that was already selected,
        // we should deselect it (toggle behavior)
        REQUIRE(determineSelectionAction(0, 0) == SelectionAction::Deselect);
        REQUIRE(determineSelectionAction(5, 5) == SelectionAction::Deselect);
        REQUIRE(determineSelectionAction(99, 99) == SelectionAction::Deselect);
    }

    SECTION("clicking a different row - allow default selection behavior") {
        // When clicking on a different row than was selected,
        // let the browser handle it (will select the new row)
        REQUIRE(determineSelectionAction(0, 5) == SelectionAction::AllowDefault);
        REQUIRE(determineSelectionAction(5, 0) == SelectionAction::AllowDefault);
        REQUIRE(determineSelectionAction(10, 20) == SelectionAction::AllowDefault);
    }
}

// =============================================================================
// Integration Tests: PresetDataSource with REAL CDataBrowser Call Order
// =============================================================================
// These tests simulate the ACTUAL behavior of CDataBrowser:
// 1. setSelectedRow(clickedRow) is called FIRST - triggers dbSelectionChanged
// 2. dbOnMouseDown is called AFTER - by which time previousSelectedRow_ is wrong!
//
// The current implementation is BROKEN because it relies on previousSelectedRow_
// being set from the PREVIOUS click, but dbSelectionChanged updates it during
// the CURRENT click before dbOnMouseDown runs.
// =============================================================================

TEST_CASE("PresetDataSource selection toggle with REAL call order", "[ui][preset-browser][integration]") {
    PresetDataSource dataSource;

    SECTION("first click should select, not immediately deselect") {
        // Initial state: nothing selected
        REQUIRE(dataSource.getPreviousSelectedRow() == -1);

        // Simulate REAL CDataBrowser behavior:
        // 1. User clicks row 0
        // 2. CDataBrowser calls setSelectedRow(0) -> triggers dbSelectionChanged
        //    -> previousSelectedRow_ becomes 0
        // 3. CDataBrowser calls dbOnMouseDown(row=0)
        //    -> determineSelectionAction(0, 0) returns Deselect!  BUG!

        // Step 1: CDataBrowser updates selection FIRST
        // This simulates what happens when setSelectedRow triggers dbSelectionChanged
        dataSource.capturePreClickSelection(-1);  // Capture selection BEFORE click

        // Step 2: dbOnMouseDown is called
        auto result = dataSource.handleMouseDownForTesting(0, false);

        // First click should allow selection, NOT deselect!
        REQUIRE_FALSE(result.shouldDeselect);
        REQUIRE_FALSE(result.handled);
    }

    SECTION("second click on same row should deselect") {
        // Setup: row 0 is already selected from previous interaction
        dataSource.capturePreClickSelection(0);

        // Click row 0 again - should deselect
        auto result = dataSource.handleMouseDownForTesting(0, false);

        REQUIRE(result.shouldDeselect);
        REQUIRE(result.handled);
    }

    SECTION("clicking different row should allow selection change") {
        // Setup: row 2 is selected
        dataSource.capturePreClickSelection(2);

        // Click row 5 - should allow browser to select it
        auto result = dataSource.handleMouseDownForTesting(5, false);

        REQUIRE_FALSE(result.shouldDeselect);
        REQUIRE_FALSE(result.handled);
    }

    SECTION("full realistic click sequence") {
        // Simulates actual user interaction with correct call ordering

        // Step 1: Nothing selected, click row 0 -> should select
        dataSource.capturePreClickSelection(-1);
        auto r1 = dataSource.handleMouseDownForTesting(0, false);
        REQUIRE_FALSE(r1.handled);  // Browser will select row 0

        // Step 2: Row 0 selected, click row 0 again -> should deselect
        dataSource.capturePreClickSelection(0);
        auto r2 = dataSource.handleMouseDownForTesting(0, false);
        REQUIRE(r2.shouldDeselect);
        REQUIRE(r2.handled);

        // Step 3: Nothing selected, click row 3 -> should select
        dataSource.capturePreClickSelection(-1);
        auto r3 = dataSource.handleMouseDownForTesting(3, false);
        REQUIRE_FALSE(r3.handled);

        // Step 4: Row 3 selected, click row 5 -> should select row 5
        dataSource.capturePreClickSelection(3);
        auto r4 = dataSource.handleMouseDownForTesting(5, false);
        REQUIRE_FALSE(r4.handled);

        // Step 5: Row 5 selected, click row 5 again -> should deselect
        dataSource.capturePreClickSelection(5);
        auto r5 = dataSource.handleMouseDownForTesting(5, false);
        REQUIRE(r5.shouldDeselect);
        REQUIRE(r5.handled);
    }
}

// =============================================================================
// Regression Test: Mode Change Must Clear ALL Selection State
// =============================================================================
// BUG: When switching modes and back, the previously selected cell retained
// visual selection state because preClickSelectedRow_ was not reset.
// =============================================================================

TEST_CASE("PresetDataSource clears all selection state on mode change", "[ui][preset-browser][regression]") {
    PresetDataSource dataSource;

    SECTION("clearSelectionState resets preClickSelectedRow") {
        // Simulate: user selected row 2
        dataSource.capturePreClickSelection(2);
        REQUIRE(dataSource.getPreClickSelectedRow() == 2);

        // Mode change should clear ALL selection state
        dataSource.clearSelectionState();

        // preClickSelectedRow_ must be reset to -1
        REQUIRE(dataSource.getPreClickSelectedRow() == -1);
    }

    SECTION("after mode change, clicking previously selected row should SELECT not deselect") {
        // Setup: row 2 was selected before mode change
        dataSource.capturePreClickSelection(2);

        // Mode change clears selection state
        dataSource.clearSelectionState();

        // User clicks row 2 after mode change
        // Since selection was cleared, this should be treated as a NEW selection
        dataSource.capturePreClickSelection(-1);  // Nothing selected now
        auto result = dataSource.handleMouseDownForTesting(2, false);

        // Should allow selection, NOT deselect!
        REQUIRE_FALSE(result.shouldDeselect);
        REQUIRE_FALSE(result.handled);
    }

    SECTION("mode switch sequence: select -> change mode -> change back -> click same row") {
        // Step 1: Select row 2 in mode A
        dataSource.capturePreClickSelection(-1);
        auto r1 = dataSource.handleMouseDownForTesting(2, false);
        REQUIRE_FALSE(r1.handled);  // Browser selects row 2

        // Step 2: Mode changes (e.g., to mode B) - clears selection
        dataSource.clearSelectionState();
        REQUIRE(dataSource.getPreClickSelectedRow() == -1);

        // Step 3: Mode changes back to mode A
        // Selection is still cleared, preClickSelectedRow_ should still be -1

        // Step 4: Click row 2 again - should SELECT it (not deselect!)
        dataSource.capturePreClickSelection(-1);  // Nothing selected
        auto r2 = dataSource.handleMouseDownForTesting(2, false);

        REQUIRE_FALSE(r2.shouldDeselect);
        REQUIRE_FALSE(r2.handled);  // Let browser select
    }
}
