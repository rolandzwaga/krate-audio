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

// ==============================================================================
// TransformType - Enum for Lane Transform Operations (Phase 11c)
// ==============================================================================

enum class TransformType {
    kInvert = 0,
    kShiftLeft = 1,
    kShiftRight = 2,
    kRandomize = 3
};

class ArpLaneHeader {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kHeight = 16.0f;
    static constexpr float kCollapseTriangleSize = 8.0f;
    static constexpr float kLengthDropdownX = 80.0f;
    static constexpr float kLengthDropdownWidth = 36.0f;
    static constexpr int kMinSteps = 1;
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
    // Transform Button Constants (Phase 11c)
    // =========================================================================

    static constexpr float kButtonSize = 12.0f;
    static constexpr float kButtonGap = 2.0f;
    static constexpr float kButtonsRightMargin = 4.0f;

    // =========================================================================
    // Transform Callbacks (Phase 11c)
    // =========================================================================

    using TransformCallback = std::function<void(TransformType)>;
    void setTransformCallback(TransformCallback cb) {
        transformCallback_ = std::move(cb);
    }

    // =========================================================================
    // Copy/Paste Callbacks (Phase 11c)
    // =========================================================================

    using CopyCallback = std::function<void()>;
    using PasteCallback = std::function<void()>;

    void setCopyPasteCallbacks(CopyCallback copy, PasteCallback paste) {
        copyCallback_ = std::move(copy);
        pasteCallback_ = std::move(paste);
    }

    void setPasteEnabled(bool enabled) { pasteEnabled_ = enabled; }
    [[nodiscard]] bool isPasteEnabled() const { return pasteEnabled_; }

    // =========================================================================
    // State
    // =========================================================================

    void setCollapsed(bool collapsed) { isCollapsed_ = collapsed; }
    [[nodiscard]] bool isCollapsed() const { return isCollapsed_; }

    [[nodiscard]] float getHeight() const { return kHeight; }

    // =========================================================================
    // Transform Button Rendering (Phase 11c - Phase 5, T044)
    // =========================================================================

    /// Draw 4 transform icon glyphs (12x12px each, 2px gap, right-aligned at
    /// headerRight - 4px). Layout from right: Randomize, ShiftRight, ShiftLeft, Invert.
    /// Uses CGraphicsPath for cross-platform icon rendering.
    void drawTransformButtons(VSTGUI::CDrawContext* context,
                              const VSTGUI::CRect& headerRect) {
        // Tint color for button icons
        VSTGUI::CColor tint{
            static_cast<uint8_t>(accentColor_.red * 0.6f),
            static_cast<uint8_t>(accentColor_.green * 0.6f),
            static_cast<uint8_t>(accentColor_.blue * 0.6f),
            accentColor_.alpha
        };

        context->setFrameColor(tint);
        context->setFillColor(tint);
        context->setLineWidth(1.0);
        context->setLineStyle(VSTGUI::CLineStyle(
            VSTGUI::CLineStyle::kLineCapRound,
            VSTGUI::CLineStyle::kLineJoinRound));

        for (int i = 0; i < 4; ++i) {
            VSTGUI::CRect btnRect = getButtonRect(headerRect, i);
            float cx = static_cast<float>(btnRect.left + btnRect.right) / 2.0f;
            float cy = static_cast<float>(btnRect.top + btnRect.bottom) / 2.0f;
            float half = kButtonSize * 0.35f;

            switch (static_cast<TransformType>(i)) {
                case TransformType::kInvert:
                    drawMiniInvertIcon(context, cx, cy, half, tint);
                    break;
                case TransformType::kShiftLeft:
                    drawMiniShiftIcon(context, cx, cy, half, tint, -1.0f);
                    break;
                case TransformType::kShiftRight:
                    drawMiniShiftIcon(context, cx, cy, half, tint, 1.0f);
                    break;
                case TransformType::kRandomize:
                    drawMiniRegenIcon(context, cx, cy, half, tint);
                    break;
            }
        }
    }

    // =========================================================================
    // Transform Button Hit Detection (Phase 11c - Phase 5, T045)
    // =========================================================================

    /// Test click against the 4 button rects. Returns true if a button was hit
    /// and fires the transform callback.
    bool handleTransformClick(const VSTGUI::CPoint& where,
                              const VSTGUI::CRect& headerRect) {
        if (!transformCallback_) return false;

        for (int i = 0; i < 4; ++i) {
            VSTGUI::CRect btnRect = getButtonRect(headerRect, i);
            if (btnRect.pointInside(where)) {
                transformCallback_(static_cast<TransformType>(i));
                return true;
            }
        }
        return false;
    }

    // =========================================================================
    // Right-Click Context Menu (Phase 11c - stub)
    // =========================================================================

    bool handleRightClick(const VSTGUI::CPoint& where,
                          const VSTGUI::CRect& headerRect,
                          VSTGUI::CFrame* frame) {
        if (!frame) return false;
        if (!copyCallback_ && !pasteCallback_) return false;
        if (!headerRect.pointInside(where)) return false;

        // Create context menu with Copy and Paste entries
        VSTGUI::CRect menuRect(where.x, where.y, where.x + 1, where.y + 1);
        auto* menu = new VSTGUI::COptionMenu(menuRect, nullptr, -1);

        // Entry 0: Copy (always enabled)
        menu->addEntry("Copy");

        // Entry 1: Paste (grayed out if clipboard is empty)
        auto* pasteItem = new VSTGUI::CMenuItem("Paste");
        if (!pasteEnabled_) {
            pasteItem->setEnabled(false);
        }
        menu->addEntry(pasteItem);

        // Show popup (synchronous - blocks until menu dismissed)
        menu->setListener(nullptr);
        menu->popup(frame, where);

        int selectedIndex = menu->getCurrentIndex();
        menu->forget();

        if (selectedIndex == 0 && copyCallback_) {
            copyCallback_();
            return true;
        }
        if (selectedIndex == 1 && pasteEnabled_ && pasteCallback_) {
            pasteCallback_();
            return true;
        }

        return selectedIndex >= 0;
    }

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

        // Transform buttons (right-aligned)
        drawTransformButtons(context, headerRect);
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

        // Transform button zone (right-aligned buttons)
        if (handleTransformClick(where, headerRect)) {
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
    // Transform Button Rect Computation (Phase 5, T045)
    // =========================================================================

    /// Compute the CRect for transform button at index (0=Invert, 1=ShiftLeft,
    /// 2=ShiftRight, 3=Randomize). Buttons are laid out right-to-left:
    /// [Invert][gap][ShiftLeft][gap][ShiftRight][gap][Randomize][margin]
    /// Starting from headerRect.right - kButtonsRightMargin.
    [[nodiscard]] VSTGUI::CRect getButtonRect(const VSTGUI::CRect& headerRect,
                                               int buttonIndex) const {
        // Rightmost button (index 3 = Randomize) is at the far right
        float rightEdge = static_cast<float>(headerRect.right) - kButtonsRightMargin;
        float btnY = static_cast<float>(headerRect.top) +
            (kHeight - kButtonSize) / 2.0f;

        // Button order from right: Randomize(3), ShiftRight(2), ShiftLeft(1), Invert(0)
        int fromRight = 3 - buttonIndex;
        float btnRight = rightEdge - static_cast<float>(fromRight) *
            (kButtonSize + kButtonGap);
        float btnLeft = btnRight - kButtonSize;

        return VSTGUI::CRect(btnLeft, btnY, btnRight, btnY + kButtonSize);
    }

    // =========================================================================
    // Mini Icon Drawing (Phase 5, T044)
    // =========================================================================

    /// Draw a miniature Invert icon (two opposing vertical arrows)
    void drawMiniInvertIcon(VSTGUI::CDrawContext* context,
                            float cx, float cy, float half,
                            const VSTGUI::CColor& color) const {
        float spacing = half * 0.5f;
        float arrowLen = half * 0.8f;
        float headSize = half * 0.35f;

        context->setFrameColor(color);
        context->setFillColor(color);

        // Up arrow on left
        float leftX = cx - spacing;
        context->drawLine(
            VSTGUI::CPoint(leftX, cy - arrowLen),
            VSTGUI::CPoint(leftX, cy + arrowLen));
        auto upHead = VSTGUI::owned(context->createGraphicsPath());
        if (upHead) {
            upHead->beginSubpath(VSTGUI::CPoint(leftX, cy - arrowLen - headSize * 0.2));
            upHead->addLine(VSTGUI::CPoint(leftX - headSize, cy - arrowLen + headSize));
            upHead->addLine(VSTGUI::CPoint(leftX + headSize, cy - arrowLen + headSize));
            upHead->closeSubpath();
            context->drawGraphicsPath(upHead, VSTGUI::CDrawContext::kPathFilled);
        }

        // Down arrow on right
        float rightX = cx + spacing;
        context->drawLine(
            VSTGUI::CPoint(rightX, cy - arrowLen),
            VSTGUI::CPoint(rightX, cy + arrowLen));
        auto downHead = VSTGUI::owned(context->createGraphicsPath());
        if (downHead) {
            downHead->beginSubpath(VSTGUI::CPoint(rightX, cy + arrowLen + headSize * 0.2));
            downHead->addLine(VSTGUI::CPoint(rightX - headSize, cy + arrowLen - headSize));
            downHead->addLine(VSTGUI::CPoint(rightX + headSize, cy + arrowLen - headSize));
            downHead->closeSubpath();
            context->drawGraphicsPath(downHead, VSTGUI::CDrawContext::kPathFilled);
        }
    }

    /// Draw a miniature Shift icon (horizontal arrow left or right)
    void drawMiniShiftIcon(VSTGUI::CDrawContext* context,
                           float cx, float cy, float half,
                           const VSTGUI::CColor& color,
                           float direction) const {
        float shaftLen = half * 0.7f;
        float headSize = half * 0.4f;

        context->setFrameColor(color);
        context->setFillColor(color);

        float x1 = cx - shaftLen * direction;
        float x2 = cx + shaftLen * direction;
        context->drawLine(VSTGUI::CPoint(x1, cy), VSTGUI::CPoint(x2, cy));

        auto head = VSTGUI::owned(context->createGraphicsPath());
        if (head) {
            float tipX = x2 + headSize * 0.2f * direction;
            head->beginSubpath(VSTGUI::CPoint(tipX, cy));
            head->addLine(VSTGUI::CPoint(x2 - headSize * direction, cy - headSize));
            head->addLine(VSTGUI::CPoint(x2 - headSize * direction, cy + headSize));
            head->closeSubpath();
            context->drawGraphicsPath(head, VSTGUI::CDrawContext::kPathFilled);
        }
    }

    /// Draw a miniature Regen/Randomize icon (circular arc with arrowhead)
    void drawMiniRegenIcon(VSTGUI::CDrawContext* context,
                           float cx, float cy, float half,
                           const VSTGUI::CColor& color) const {
        float radius = half * 0.7f;

        context->setFrameColor(color);
        context->setFillColor(color);

        VSTGUI::CRect arcRect(cx - radius, cy - radius,
                               cx + radius, cy + radius);

        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (path) {
            path->addArc(arcRect, 30.0, 330.0, true);
            context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
        }

        // Small arrowhead at ~330 degrees
        float headSize = half * 0.3f;
        constexpr float kPi = 3.14159265f;
        float angle330 = 330.0f * kPi / 180.0f;
        float tipX = cx + radius * std::cos(angle330);
        float tipY = cy + radius * std::sin(angle330);

        auto head = VSTGUI::owned(context->createGraphicsPath());
        if (head) {
            head->beginSubpath(VSTGUI::CPoint(tipX + headSize, tipY - headSize * 0.5f));
            head->addLine(VSTGUI::CPoint(tipX - headSize * 0.5f, tipY - headSize));
            head->addLine(VSTGUI::CPoint(tipX, tipY + headSize * 0.3f));
            head->closeSubpath();
            context->drawGraphicsPath(head, VSTGUI::CDrawContext::kPathFilled);
        }
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

    // Phase 11c: transform + copy/paste callbacks and state
    TransformCallback transformCallback_;
    CopyCallback copyCallback_;
    PasteCallback pasteCallback_;
    bool pasteEnabled_ = false;
};

} // namespace Krate::Plugins
