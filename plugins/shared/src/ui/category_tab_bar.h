#pragma once

// ==============================================================================
// CategoryTabBar - Vertical Tab Bar for Category Filtering (Shared)
// ==============================================================================
// Generalized from Iterum's ModeTabBar. Accepts dynamic labels instead of
// hardcoded mode names.
// ==============================================================================

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include <functional>
#include <string>
#include <vector>

namespace Krate::Plugins {

class CategoryTabBar : public VSTGUI::CView {
public:
    CategoryTabBar(const VSTGUI::CRect& size, std::vector<std::string> labels);
    ~CategoryTabBar() override = default;

    // Tab selection
    int getSelectedTab() const { return selectedTab_; }
    void setSelectedTab(int tab);

    // Callback when selection changes
    using SelectionCallback = std::function<void(int)>;
    void setSelectionCallback(SelectionCallback cb) { selectionCallback_ = std::move(cb); }

    // CView overrides
    void draw(VSTGUI::CDrawContext* context) override;
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons
    ) override;

private:
    std::vector<std::string> labels_;
    int selectedTab_ = 0;
    SelectionCallback selectionCallback_;

    // Get tab bounds for a given index
    VSTGUI::CRect getTabRect(int index) const;
};

} // namespace Krate::Plugins
