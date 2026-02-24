#pragma once

// ==============================================================================
// ArpLaneHeader - Shared Header Helper for Arpeggiator Lanes
// ==============================================================================
// Non-CView helper class owned by composition in each lane class.
// Encapsulates collapse toggle triangle, accent-colored name label,
// and length dropdown rendering/interaction.
//
// Location: plugins/shared/src/ui/arp_lane_header.h
// ==============================================================================

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Krate::Plugins {

class ArpLaneHeader {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kHeight = 16.0f;
    static constexpr float kCollapseTriangleSize = 8.0f;
    static constexpr float kLengthDropdownX = 80.0f;
    static constexpr float kLengthDropdownWidth = 36.0f;
    static constexpr int kMinSteps = 2;
    static constexpr int kMaxSteps = 32;

    // =========================================================================
    // Configuration
    // =========================================================================

    void setLaneName(const std::string& name) { laneName_ = name; }
    [[nodiscard]] const std::string& getLaneName() const { return laneName_; }

    void setAccentColor(const VSTGUI::CColor& color) { accentColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getAccentColor() const { return accentColor_; }

    void setNumSteps(int steps) { numSteps_ = steps; }
    [[nodiscard]] int getNumSteps() const { return numSteps_; }

    void setLengthParamId(uint32_t paramId) { lengthParamId_ = paramId; }
    [[nodiscard]] uint32_t getLengthParamId() const { return lengthParamId_; }

    // =========================================================================
    // Callbacks
    // =========================================================================

    void setCollapseCallback(std::function<void()> cb) {
        collapseCallback_ = std::move(cb);
    }

    void setLengthParamCallback(std::function<void(uint32_t, float)> cb) {
        lengthParamCallback_ = std::move(cb);
    }

    // =========================================================================
    // State
    // =========================================================================

    void setCollapsed(bool collapsed) { isCollapsed_ = collapsed; }
    [[nodiscard]] bool isCollapsed() const { return isCollapsed_; }

    [[nodiscard]] float getHeight() const { return kHeight; }

    // =========================================================================
    // Rendering: draws the header into the given rect
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context, const VSTGUI::CRect& headerRect) {
        // Header background
        VSTGUI::CColor headerBg{30, 30, 33, 255};
        context->setFillColor(headerBg);
        context->drawRect(headerRect, VSTGUI::kDrawFilled);

        // Collapse triangle
        drawCollapseTriangle(context, headerRect);

        // Lane name label
        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 9.0);
        context->setFont(font);
        context->setFontColor(accentColor_);

        VSTGUI::CRect nameRect(headerRect.left + 20.0, headerRect.top + 1.0,
                                headerRect.left + 80.0, headerRect.top + kHeight - 1.0);
        context->drawString(VSTGUI::UTF8String(laneName_), nameRect,
                           VSTGUI::kLeftText);

        // Length dropdown label (shows current step count)
        VSTGUI::CColor labelColor{160, 160, 165, 255};
        context->setFontColor(labelColor);

        std::string lengthText = std::to_string(numSteps_);
        VSTGUI::CRect lengthRect(headerRect.left + kLengthDropdownX, headerRect.top + 1.0,
                                  headerRect.left + kLengthDropdownX + kLengthDropdownWidth,
                                  headerRect.top + kHeight - 1.0);
        context->drawString(VSTGUI::UTF8String(lengthText), lengthRect,
                           VSTGUI::kCenterText);

        // Small dropdown indicator triangle
        float triX = static_cast<float>(headerRect.left) + kLengthDropdownX + kLengthDropdownWidth - 6.0f;
        float triY = static_cast<float>(headerRect.top) + kHeight / 2.0f;
        auto triPath = VSTGUI::owned(context->createGraphicsPath());
        if (triPath) {
            triPath->beginSubpath(VSTGUI::CPoint(triX - 2.5, triY - 1.5));
            triPath->addLine(VSTGUI::CPoint(triX + 2.5, triY - 1.5));
            triPath->addLine(VSTGUI::CPoint(triX, triY + 1.5));
            triPath->closeSubpath();
            context->setFillColor(labelColor);
            context->drawGraphicsPath(triPath, VSTGUI::CDrawContext::kPathFilled);
        }
    }

    // =========================================================================
    // Interaction: returns true if the click was handled (in header area)
    // =========================================================================

    bool handleMouseDown(const VSTGUI::CPoint& where,
                         const VSTGUI::CRect& headerRect,
                         VSTGUI::CFrame* frame) {
        if (!headerRect.pointInside(where)) {
            return false;
        }

        float localX = static_cast<float>(where.x - headerRect.left);

        // Toggle zone is the left ~24px (triangle + padding)
        if (localX < 24.0f) {
            isCollapsed_ = !isCollapsed_;
            if (collapseCallback_) {
                collapseCallback_();
            }
            return true;
        }

        // Length dropdown zone
        if (localX >= kLengthDropdownX &&
            localX < kLengthDropdownX + kLengthDropdownWidth) {
            openLengthDropdown(where, frame);
            return true;
        }

        return false;
    }

private:
    // =========================================================================
    // Drawing Helpers
    // =========================================================================

    void drawCollapseTriangle(VSTGUI::CDrawContext* context,
                              const VSTGUI::CRect& headerRect) {
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path) return;

        float cx = static_cast<float>(headerRect.left) + 10.0f;
        float cy = static_cast<float>(headerRect.top) + kHeight / 2.0f;
        float half = kCollapseTriangleSize / 2.0f;

        if (isCollapsed_) {
            // Right-pointing triangle (>)
            path->beginSubpath(VSTGUI::CPoint(cx - half * 0.5f, cy - half));
            path->addLine(VSTGUI::CPoint(cx + half * 0.5f, cy));
            path->addLine(VSTGUI::CPoint(cx - half * 0.5f, cy + half));
            path->closeSubpath();
        } else {
            // Down-pointing triangle (v)
            path->beginSubpath(VSTGUI::CPoint(cx - half, cy - half * 0.5f));
            path->addLine(VSTGUI::CPoint(cx + half, cy - half * 0.5f));
            path->addLine(VSTGUI::CPoint(cx, cy + half * 0.5f));
            path->closeSubpath();
        }

        context->setFillColor(VSTGUI::CColor{180, 180, 185, 255});
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
    }

    void openLengthDropdown(const VSTGUI::CPoint& where,
                            VSTGUI::CFrame* frame) {
        if (!frame) return;

        // Create option menu with values kMinSteps through kMaxSteps
        VSTGUI::CRect menuRect(where.x, where.y, where.x + 1, where.y + 1);
        auto* menu = new VSTGUI::COptionMenu(menuRect, nullptr, -1);

        for (int i = kMinSteps; i <= kMaxSteps; ++i) {
            menu->addEntry(std::to_string(i));
        }

        // Set current selection
        int currentIndex = numSteps_ - kMinSteps;
        menu->setCurrent(currentIndex);

        // Show popup and handle selection
        menu->setListener(nullptr);
        menu->popup(frame, where);

        int selectedIndex = menu->getCurrentIndex();
        if (selectedIndex >= 0) {
            int newSteps = selectedIndex + kMinSteps;
            if (newSteps != numSteps_) {
                numSteps_ = newSteps;

                // Notify via callback with normalized value
                if (lengthParamCallback_ && lengthParamId_ != 0) {
                    float normalized = static_cast<float>(newSteps - 1) / 31.0f;
                    lengthParamCallback_(lengthParamId_, normalized);
                }
            }
        }

        menu->forget();
    }

    // =========================================================================
    // State
    // =========================================================================

    std::string laneName_;
    VSTGUI::CColor accentColor_{208, 132, 92, 255};
    bool isCollapsed_ = false;
    int numSteps_ = 16;
    uint32_t lengthParamId_ = 0;
    std::function<void()> collapseCallback_;
    std::function<void(uint32_t, float)> lengthParamCallback_;
};

} // namespace Krate::Plugins
