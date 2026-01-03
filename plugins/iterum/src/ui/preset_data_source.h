#pragma once

// ==============================================================================
// PresetDataSource - CDataBrowser Delegate for Preset List
// ==============================================================================
// Spec 042: Preset Browser
// Implements IDataBrowserDelegate to provide data for the preset list.
// ==============================================================================

#include "vstgui/lib/idatabrowserdelegate.h"
#include "vstgui/lib/cdatabrowser.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "../preset/preset_info.h"
#include "preset_browser_logic.h"  // SelectionAction, determineSelectionAction
#include <vector>
#include <string>

namespace Iterum {

class PresetDataSource : public VSTGUI::DataBrowserDelegateAdapter {
public:
    PresetDataSource() = default;
    ~PresetDataSource() override = default;

    // Data management
    void setPresets(const std::vector<PresetInfo>& presets);
    void setModeFilter(int mode);  // -1 = All
    void setSearchFilter(const std::string& query);
    const PresetInfo* getPresetAtRow(int row) const;

    // IDataBrowserDelegate overrides
    int32_t dbGetNumRows(VSTGUI::CDataBrowser* browser) override;
    int32_t dbGetNumColumns(VSTGUI::CDataBrowser* browser) override;
    VSTGUI::CCoord dbGetRowHeight(VSTGUI::CDataBrowser* browser) override;
    VSTGUI::CCoord dbGetCurrentColumnWidth(int32_t index, VSTGUI::CDataBrowser* browser) override;

    void dbDrawCell(
        VSTGUI::CDrawContext* context,
        const VSTGUI::CRect& size,
        int32_t row,
        int32_t column,
        int32_t flags,
        VSTGUI::CDataBrowser* browser
    ) override;

    void dbSelectionChanged(VSTGUI::CDataBrowser* browser) override;

    VSTGUI::CMouseEventResult dbOnMouseDown(
        const VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons,
        int32_t row,
        int32_t column,
        VSTGUI::CDataBrowser* browser
    ) override;

    // Selection callback
    using SelectionCallback = std::function<void(int)>;
    using DoubleClickCallback = std::function<void(int)>;

    void setSelectionCallback(SelectionCallback cb) { selectionCallback_ = std::move(cb); }
    void setDoubleClickCallback(DoubleClickCallback cb) { doubleClickCallback_ = std::move(cb); }

    // Capture selection state BEFORE a click event starts
    // Must be called from PresetBrowserView::onMouseDown BEFORE forwarding to children
    // This is necessary because CDataBrowser updates selection BEFORE calling delegate
    void capturePreClickSelection(VSTGUI::CDataBrowser* browser) {
        preClickSelectedRow_ = browser->getSelectedRow();
    }

    // Clear ALL selection tracking state
    // Must be called when mode changes to prevent stale selection state
    void clearSelectionState() {
        preClickSelectedRow_ = -1;
        previousSelectedRow_ = -1;
    }

    // Test accessors
    int32_t getPreviousSelectedRow() const { return previousSelectedRow_; }
    int32_t getPreClickSelectedRow() const { return preClickSelectedRow_; }

    // For testing: manually set the pre-click selection state
    void capturePreClickSelection(int32_t row) { preClickSelectedRow_ = row; }

    // Test method - handles mouse down logic without CDataBrowser dependency
    // Returns: {shouldDeselect, mouseEventResult}
    struct MouseDownResult {
        bool shouldDeselect;
        bool handled;
    };
    MouseDownResult handleMouseDownForTesting(int32_t row, bool isDoubleClick) {
        if (isDoubleClick && doubleClickCallback_) {
            return {false, true};  // Handled, but not deselect
        }
        // Use preClickSelectedRow_ which was captured BEFORE CDataBrowser updated selection
        auto action = determineSelectionAction(row, preClickSelectedRow_);
        if (action == SelectionAction::Deselect) {
            return {true, true};  // Should deselect, handled
        }
        return {false, false};  // Not handled, let browser do default
    }

private:
    std::vector<PresetInfo> allPresets_;
    std::vector<PresetInfo> filteredPresets_;
    int modeFilter_ = -1;
    std::string searchFilter_;

    SelectionCallback selectionCallback_;
    DoubleClickCallback doubleClickCallback_;

    // Selection state captured BEFORE the current click event
    // This must be set by PresetBrowserView::onMouseDown BEFORE forwarding to children
    // because CDataBrowser calls setSelectedRow() BEFORE dbOnMouseDown()
    int32_t preClickSelectedRow_ = -1;

    // Track selection state after changes complete (for dbSelectionChanged callback)
    int32_t previousSelectedRow_ = -1;

    void applyFilters();
};

} // namespace Iterum
