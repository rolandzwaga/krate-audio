#pragma once

// ==============================================================================
// LaneTabBar — 8 Color-Coded Lane Selection Tabs
// ==============================================================================
// Horizontal tab bar for selecting which lane to edit in the DetailStrip.
// Each tab is color-coded to match the ring highlight colors.
// ==============================================================================

#include "ring_renderer.h" // for getLaneColors()

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"

#include <array>
#include <functional>
#include <string>

namespace Gradus {

class LaneTabBar : public VSTGUI::CView {
public:
    explicit LaneTabBar(const VSTGUI::CRect& size)
        : CView(size)
    {
    }

    using TabSelectedCallback = std::function<void(int laneIndex)>;
    void setTabSelectedCallback(TabSelectedCallback cb)
    {
        callback_ = std::move(cb);
    }

    void setSelectedTab(int index)
    {
        if (index >= 0 && index < kLaneCount && index != selectedTab_) {
            selectedTab_ = index;
            invalid();
        }
    }

    [[nodiscard]] int selectedTab() const { return selectedTab_; }

    /// Set a pulsing activity indicator on a tab (playhead active).
    void setTabActive(int index, bool active)
    {
        if (index >= 0 && index < kLaneCount) {
            tabActive_[index] = active;
            invalid();
        }
    }

    // =========================================================================
    // Drawing
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override
    {
        context->setDrawMode(
            VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        auto viewSize = getViewSize();
        float totalWidth = static_cast<float>(viewSize.getWidth());
        float tabWidth = totalWidth / static_cast<float>(kLaneCount);

        const auto& colors = getLaneColors();

        for (int i = 0; i < kLaneCount; ++i) {
            float x = viewSize.left + static_cast<float>(i) * tabWidth;
            VSTGUI::CRect tabRect(x, viewSize.top,
                                  x + tabWidth - 1.0, viewSize.bottom);

            bool isSelected = (i == selectedTab_);

            // Background
            if (isSelected) {
                context->setFillColor(colors[i].accent);
                context->drawRect(tabRect, VSTGUI::kDrawFilled);
            } else {
                VSTGUI::CColor bg = colors[i].accent;
                bg.alpha = 40;
                context->setFillColor(bg);
                context->drawRect(tabRect, VSTGUI::kDrawFilled);

                // Hover highlight
                if (i == hoveredTab_) {
                    VSTGUI::CColor hover = colors[i].accent;
                    hover.alpha = 80;
                    context->setFillColor(hover);
                    context->drawRect(tabRect, VSTGUI::kDrawFilled);
                }
            }

            // Activity dot (playhead active indicator)
            if (tabActive_[i]) {
                float dotX = x + tabWidth * 0.5f;
                float dotY = viewSize.top + 4.0f;
                VSTGUI::CRect dotRect(dotX - 2, dotY - 2, dotX + 2, dotY + 2);
                context->setFillColor(colors[i].highlight);
                context->drawEllipse(dotRect, VSTGUI::kDrawFilled);
            }

            // Label
            VSTGUI::CColor textColor = isSelected
                ? VSTGUI::CColor{0xFF, 0xFF, 0xFF, 0xFF}
                : VSTGUI::CColor{0xC0, 0xC0, 0xC8, 0xFF};

            // Draw abbreviated lane name
            VSTGUI::CRect textRect(x + 2, viewSize.top + 6,
                                   x + tabWidth - 3, viewSize.bottom - 2);
            context->setFontColor(textColor);
            context->setFont(VSTGUI::kNormalFontSmaller);
            context->drawString(kTabLabels[i], textRect,
                                VSTGUI::kCenterText);
        }

        setDirty(false);
    }

    // =========================================================================
    // Mouse Interaction
    // =========================================================================

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        if (!(buttons & VSTGUI::kLButton)) return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;

        int tab = hitTestTab(where);
        if (tab >= 0 && tab != selectedTab_) {
            selectedTab_ = tab;
            invalid();
            if (callback_) callback_(tab);
        }
        return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        [[maybe_unused]] const VSTGUI::CButtonState& buttons) override
    {
        int tab = hitTestTab(where);
        if (tab != hoveredTab_) {
            hoveredTab_ = tab;
            invalid();
        }
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseExited(
        [[maybe_unused]] VSTGUI::CPoint& where,
        [[maybe_unused]] const VSTGUI::CButtonState& buttons) override
    {
        if (hoveredTab_ >= 0) {
            hoveredTab_ = -1;
            invalid();
        }
        return VSTGUI::kMouseEventHandled;
    }

private:
    [[nodiscard]] int hitTestTab(const VSTGUI::CPoint& where) const
    {
        auto viewSize = getViewSize();
        if (!viewSize.pointInside(where)) return -1;

        float tabWidth = static_cast<float>(viewSize.getWidth()) / static_cast<float>(kLaneCount);
        float relX = static_cast<float>(where.x - viewSize.left);
        int tab = static_cast<int>(relX / tabWidth);
        return std::clamp(tab, 0, kLaneCount - 1);
    }

    static constexpr const char* kTabLabels[kLaneCount] = {
        "VEL", "GATE", "PITCH", "MOD", "COND", "RATCH", "CHORD", "INV", "DELAY"
    };

    int selectedTab_ = 0;
    int hoveredTab_ = -1;
    std::array<bool, kLaneCount> tabActive_{};
    TabSelectedCallback callback_;

    CLASS_METHODS(LaneTabBar, CView)
};

} // namespace Gradus
