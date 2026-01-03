#pragma once

// =============================================================================
// PresetBrowserLogic - Pure Functions for Preset Browser Behavior
// =============================================================================
// Extracted from PresetBrowserView/PresetDataSource for testability (humble object pattern).
// These functions have no VSTGUI dependencies and can be unit tested.
// =============================================================================

#include <cstdint>

namespace Iterum {

/// Simplified key codes for testable logic (maps from VSTGUI::VirtualKey)
enum class KeyCode {
    Escape,
    Enter,  // Return or Enter key
    Other
};

/// Actions that can result from keyboard input
enum class KeyAction {
    None,                   // No action, pass to parent
    CloseBrowser,           // Close the entire preset browser
    ConfirmSaveDialog,      // Confirm the save dialog
    CancelSaveDialog,       // Cancel the save dialog
    ConfirmDeleteDialog,    // Confirm the delete dialog
    CancelDeleteDialog,     // Cancel the delete dialog
    ConfirmOverwriteDialog, // Confirm the overwrite dialog
    CancelOverwriteDialog   // Cancel the overwrite dialog
};

/// Determine the action to take based on keyboard input and dialog state.
/// This is a pure function with no side effects - easy to test.
///
/// @param key The key that was pressed
/// @param saveDialogVisible Whether the save dialog is currently visible
/// @param deleteDialogVisible Whether the delete dialog is currently visible
/// @param overwriteDialogVisible Whether the overwrite dialog is currently visible
/// @return The action to perform
inline KeyAction determineKeyAction(
    KeyCode key,
    bool saveDialogVisible,
    bool deleteDialogVisible,
    bool overwriteDialogVisible = false
) {
    switch (key) {
        case KeyCode::Escape:
            // Priority: save dialog > delete dialog > overwrite dialog > close browser
            if (saveDialogVisible) {
                return KeyAction::CancelSaveDialog;
            }
            if (deleteDialogVisible) {
                return KeyAction::CancelDeleteDialog;
            }
            if (overwriteDialogVisible) {
                return KeyAction::CancelOverwriteDialog;
            }
            return KeyAction::CloseBrowser;

        case KeyCode::Enter:
            // Priority: save dialog > delete dialog > overwrite dialog > nothing
            if (saveDialogVisible) {
                return KeyAction::ConfirmSaveDialog;
            }
            if (deleteDialogVisible) {
                return KeyAction::ConfirmDeleteDialog;
            }
            if (overwriteDialogVisible) {
                return KeyAction::ConfirmOverwriteDialog;
            }
            return KeyAction::None;

        case KeyCode::Other:
        default:
            return KeyAction::None;
    }
}

// =============================================================================
// Selection Actions
// =============================================================================

/// @brief Result of determining what selection action to take
enum class SelectionAction {
    AllowDefault,  ///< Let CDataBrowser handle selection normally
    Deselect       ///< Deselect the currently selected row (toggle off)
};

/// @brief Determine what selection action to take based on clicked row and previous selection
/// @param clickedRow The row that was clicked
/// @param previousSelectedRow The row that was selected BEFORE this click (-1 if none)
/// @return SelectionAction indicating what to do
///
/// This is a pure function extracted for testability (humble object pattern).
/// The key insight: CDataBrowser may update selection BEFORE calling the delegate,
/// so we must track the PREVIOUS selection state ourselves.
inline SelectionAction determineSelectionAction(int32_t clickedRow, int32_t previousSelectedRow) {
    // If clicking the same row that was already selected, toggle it off
    if (previousSelectedRow >= 0 && previousSelectedRow == clickedRow) {
        return SelectionAction::Deselect;
    }
    // Otherwise, let the browser handle selection normally
    return SelectionAction::AllowDefault;
}

// =============================================================================
// Testable Selection Behavior
// =============================================================================

/// @brief Result of mouse down handling
struct MouseDownResult {
    bool shouldDeselect;  ///< True if we should call unselectAll()
    bool handled;         ///< True if event was handled (don't pass to browser)
};

/// @brief Testable selection behavior without VSTGUI dependencies
///
/// This class encapsulates the selection toggle logic in a way that can be
/// unit tested without pulling in VSTGUI. PresetDataSource uses this internally.
class SelectionBehavior {
public:
    /// @brief Get the previously selected row
    int32_t getPreviousSelectedRow() const { return previousSelectedRow_; }

    /// @brief Set the previously selected row (called when selection changes)
    void setSelectedRow(int32_t row) { previousSelectedRow_ = row; }

    /// @brief Clear selection tracking
    void clearSelection() { previousSelectedRow_ = -1; }

    /// @brief Handle mouse down and determine what action to take
    /// @param clickedRow The row that was clicked
    /// @param isDoubleClick True if this is a double-click
    /// @param hasDoubleClickCallback True if a double-click callback is registered
    /// @return MouseDownResult indicating what action to take
    MouseDownResult handleMouseDown(int32_t clickedRow, bool isDoubleClick, bool hasDoubleClickCallback) {
        // Double-click with callback is handled separately
        if (isDoubleClick && hasDoubleClickCallback) {
            return {false, true};  // Handled, but not deselect
        }

        auto action = determineSelectionAction(clickedRow, previousSelectedRow_);
        if (action == SelectionAction::Deselect) {
            return {true, true};  // Should deselect, handled
        }

        return {false, false};  // Not handled, let browser do default
    }

private:
    int32_t previousSelectedRow_ = -1;
};

} // namespace Iterum
