#include "category_tab_bar.h"

namespace Krate::Plugins {

CategoryTabBar::CategoryTabBar(const VSTGUI::CRect& size, std::vector<std::string> labels)
    : CView(size)
    , labels_(std::move(labels))
{
}

void CategoryTabBar::setSelectedTab(int tab) {
    int numTabs = static_cast<int>(labels_.size());
    if (tab >= 0 && tab < numTabs && tab != selectedTab_) {
        selectedTab_ = tab;
        invalid(); // Request redraw

        if (selectionCallback_) {
            // Convert to subcategory filter: 0 = All (-1), 1+ = subcategory (0-based)
            int filterIndex = (tab == 0) ? -1 : (tab - 1);
            selectionCallback_(filterIndex);
        }
    }
}

VSTGUI::CRect CategoryTabBar::getTabRect(int index) const {
    auto viewSize = getViewSize();
    int numTabs = static_cast<int>(labels_.size());
    auto tabHeight = viewSize.getHeight() / numTabs;

    return {
        viewSize.left,
        viewSize.top + tabHeight * index,
        viewSize.right,
        viewSize.top + tabHeight * (index + 1)
    };
}

void CategoryTabBar::draw(VSTGUI::CDrawContext* context) {
    int numTabs = static_cast<int>(labels_.size());

    // Set font for text rendering
    auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 11);
    context->setFont(font);

    for (int i = 0; i < numTabs; ++i) {
        auto tabRect = getTabRect(i);

        // Background
        if (i == selectedTab_) {
            context->setFillColor(VSTGUI::CColor(60, 100, 160)); // Selected
        } else {
            context->setFillColor(VSTGUI::CColor(50, 50, 50)); // Normal
        }
        context->drawRect(tabRect, VSTGUI::kDrawFilled);

        // Border
        context->setFrameColor(VSTGUI::CColor(80, 80, 80));
        context->drawRect(tabRect, VSTGUI::kDrawStroked);

        // Text
        context->setFontColor(VSTGUI::CColor(255, 255, 255));
        auto textRect = tabRect;
        textRect.inset(8, 0);
        context->drawString(labels_[static_cast<size_t>(i)].c_str(), textRect, VSTGUI::kLeftText);
    }
}

VSTGUI::CMouseEventResult CategoryTabBar::onMouseDown(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& /*buttons*/
) {
    int numTabs = static_cast<int>(labels_.size());
    // Find which tab was clicked
    for (int i = 0; i < numTabs; ++i) {
        auto tabRect = getTabRect(i);
        if (tabRect.pointInside(where)) {
            setSelectedTab(i);
            return VSTGUI::kMouseEventHandled;
        }
    }

    return VSTGUI::kMouseEventNotHandled;
}

} // namespace Krate::Plugins
