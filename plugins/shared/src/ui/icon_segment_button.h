#pragma once

// ==============================================================================
// IconSegmentButton - Compact Segment Button with Vector Icons/Text and Tooltips
// ==============================================================================
// A CControl segment button that displays named vector icons or text labels.
// When a segment has no icon name, its name is drawn as centered text.
// Segment names appear as tooltips on hover.
//
// Value mapping (same as CSegmentButton):
//   N segments: value = selectedIndex / (N - 1)
//   2 segments: 0.0 = first, 1.0 = second
//   3 segments: 0.0, 0.5, 1.0
//
// Built-in icons: "gear", "funnel"
// Additional icons can be added by extending drawIcon().
//
// All drawing uses CGraphicsPath (no bitmaps, cross-platform).
//
// Registered as "IconSegmentButton" via VSTGUI ViewCreator system.
// ==============================================================================

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/iuidescription.h"
#include "vstgui/uidescription/detail/uiviewcreatorattributes.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <sstream>

namespace Krate::Plugins {

// ==============================================================================
// IconSegmentButton Control
// ==============================================================================

class IconSegmentButton : public VSTGUI::CControl {
public:
    // =========================================================================
    // Layout Mode
    // =========================================================================

    enum LayoutMode {
        kIconOrText,   // Default: icon if available, else text label
        kIconAndText   // Stacked: icon above, text below
    };

    // =========================================================================
    // Segment Data
    // =========================================================================

    struct Segment {
        std::string name;     // Tooltip text
        std::string iconName; // Named icon key (e.g. "gear", "funnel")
        VSTGUI::CRect rect;   // Computed per-segment hit rect
    };

    // =========================================================================
    // Construction
    // =========================================================================

    IconSegmentButton(const VSTGUI::CRect& size,
                      VSTGUI::IControlListener* listener,
                      int32_t tag)
        : CControl(size, listener, tag) {
        setMin(0.0f);
        setMax(1.0f);
    }

    IconSegmentButton(const IconSegmentButton& other)
        : CControl(other)
        , segments_(other.segments_)
        , layoutMode_(other.layoutMode_)
        , selectedColor_(other.selectedColor_)
        , unselectedColor_(other.unselectedColor_)
        , frameColor_(other.frameColor_)
        , highlightColor_(other.highlightColor_)
        , roundRadius_(other.roundRadius_)
        , iconSize_(other.iconSize_)
        , strokeWidth_(other.strokeWidth_)
        , textFontSize_(other.textFontSize_)
        , hoverSegment_(kNoSegment) {}

    CLASS_METHODS(IconSegmentButton, CControl)

    // =========================================================================
    // Segment Configuration
    // =========================================================================

    void setSegmentNames(const std::string& commaList) {
        auto names = splitComma(commaList);
        // Resize segments, preserve icon names if already set
        while (segments_.size() < names.size())
            segments_.push_back({});
        segments_.resize(names.size());
        for (size_t i = 0; i < names.size(); ++i)
            segments_[i].name = names[i];
        computeSegmentRects();
        setDirty();
    }

    [[nodiscard]] std::string getSegmentNames() const {
        return joinComma(segments_, [](const Segment& s) { return s.name; });
    }

    void setSegmentIcons(const std::string& commaList) {
        auto icons = splitComma(commaList);
        while (segments_.size() < icons.size())
            segments_.push_back({});
        for (size_t i = 0; i < icons.size(); ++i)
            segments_[i].iconName = icons[i];
        computeSegmentRects();
        setDirty();
    }

    [[nodiscard]] std::string getSegmentIcons() const {
        return joinComma(segments_, [](const Segment& s) { return s.iconName; });
    }

    // =========================================================================
    // Visual Attributes
    // =========================================================================

    void setSelectedColor(const VSTGUI::CColor& c) { selectedColor_ = c; setDirty(); }
    [[nodiscard]] VSTGUI::CColor getSelectedColor() const { return selectedColor_; }

    void setUnselectedColor(const VSTGUI::CColor& c) { unselectedColor_ = c; setDirty(); }
    [[nodiscard]] VSTGUI::CColor getUnselectedColor() const { return unselectedColor_; }

    void setFrameColor(const VSTGUI::CColor& c) { frameColor_ = c; setDirty(); }
    [[nodiscard]] VSTGUI::CColor getFrameColor() const { return frameColor_; }

    void setHighlightColor(const VSTGUI::CColor& c) { highlightColor_ = c; setDirty(); }
    [[nodiscard]] VSTGUI::CColor getHighlightColor() const { return highlightColor_; }

    void setRoundRadius(double r) { roundRadius_ = r; setDirty(); }
    [[nodiscard]] double getRoundRadius() const { return roundRadius_; }

    void setIconSize(float s) { iconSize_ = s; setDirty(); }
    [[nodiscard]] float getIconSize() const { return iconSize_; }

    void setStrokeWidth(double w) { strokeWidth_ = w; setDirty(); }
    [[nodiscard]] double getStrokeWidth() const { return strokeWidth_; }

    void setTextFontSize(double s) { textFontSize_ = s; setDirty(); }
    [[nodiscard]] double getTextFontSize() const { return textFontSize_; }

    void setLayoutMode(LayoutMode m) { layoutMode_ = m; setDirty(); }
    [[nodiscard]] LayoutMode getLayoutMode() const { return layoutMode_; }

    // =========================================================================
    // Selected Segment
    // =========================================================================

    [[nodiscard]] uint32_t getSelectedSegment() const {
        if (segments_.size() <= 1) return 0;
        float val = getValueNormalized();
        auto idx = static_cast<uint32_t>(
            std::round(val * static_cast<float>(segments_.size() - 1)));
        return std::min(idx, static_cast<uint32_t>(segments_.size() - 1));
    }

    void setSelectedSegment(uint32_t idx) {
        if (segments_.empty()) return;
        idx = std::min(idx, static_cast<uint32_t>(segments_.size() - 1));
        float val = (segments_.size() <= 1)
            ? 0.0f
            : static_cast<float>(idx) / static_cast<float>(segments_.size() - 1);
        setValueNormalized(val);
    }

    // =========================================================================
    // Drawing
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        if (segments_.empty()) {
            setDirty(false);
            return;
        }

        computeSegmentRects();
        uint32_t selected = getSelectedSegment();
        VSTGUI::CRect vs = getViewSize();

        // Background
        drawBackground(context, vs);

        // Selected segment highlight
        if (selected < segments_.size())
            drawSelectedHighlight(context, segments_[selected].rect, selected);

        // Icons / Text
        for (uint32_t i = 0; i < static_cast<uint32_t>(segments_.size()); ++i) {
            bool isSel = (i == selected);
            VSTGUI::CColor iconColor = isSel ? selectedColor_ : unselectedColor_;
            drawIcon(context, segments_[i].rect, segments_[i].iconName,
                     iconColor, segments_[i].name);
        }

        // Frame
        drawFrame(context, vs);

        // Segment dividers
        drawDividers(context, vs);

        setDirty(false);
    }

    // =========================================================================
    // Lifecycle
    // =========================================================================

    bool attached(VSTGUI::CView* parent) override {
        if (CControl::attached(parent)) {
            if (auto* frame = getFrame())
                frame->enableTooltips(true, 500);
            return true;
        }
        return false;
    }

    void setViewSize(const VSTGUI::CRect& rect,
                     bool invalid = true) override {
        CControl::setViewSize(rect, invalid);
        computeSegmentRects();
    }

    // =========================================================================
    // Mouse Interaction
    // =========================================================================

    VSTGUI::CMouseEventResult onMouseEntered(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {
        if (auto* frame = getFrame())
            frame->setCursor(VSTGUI::kCursorHand);
        updateHoverTooltip(where);
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseExited(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {
        if (auto* frame = getFrame())
            frame->setCursor(VSTGUI::kCursorDefault);
        hoverSegment_ = kNoSegment;
        setTooltipText(nullptr);
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {
        updateHoverTooltip(where);
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {
        if (!(buttons & VSTGUI::kLButton))
            return VSTGUI::kMouseEventNotHandled;

        uint32_t hit = hitTestSegment(where);
        if (hit == kNoSegment || hit == getSelectedSegment())
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;

        beginEdit();
        setSelectedSegment(hit);
        valueChanged();
        endEdit();
        invalid();
        return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
    }

    void valueChanged() override {
        if (inValueChanged_) return;
        inValueChanged_ = true;
        CControl::valueChanged();
        inValueChanged_ = false;
    }

private:
    // =========================================================================
    // Drawing Helpers
    // =========================================================================

    void drawBackground(VSTGUI::CDrawContext* context,
                        const VSTGUI::CRect& bounds) const {
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path) return;
        path->addRoundRect(bounds, roundRadius_);
        context->setFillColor(VSTGUI::CColor(30, 30, 34, 255));
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
    }

    void drawSelectedHighlight(VSTGUI::CDrawContext* context,
                               const VSTGUI::CRect& segRect,
                               uint32_t segIndex) const {
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path) return;

        // For edge segments, use rounded corners on the outer side only
        // For middle segments, use a plain rect
        bool isFirst = (segIndex == 0);
        bool isLast = (segIndex == segments_.size() - 1);

        if (isFirst || isLast) {
            // Rounded on the outer corners, square on the inner
            VSTGUI::CRect r = segRect;
            if (isFirst && isLast) {
                path->addRoundRect(r, roundRadius_);
            } else if (isFirst) {
                // Round top-left, bottom-left only
                addPartialRoundRect(path, r, roundRadius_, true, false, false, true);
            } else {
                // Round top-right, bottom-right only
                addPartialRoundRect(path, r, roundRadius_, false, true, true, false);
            }
        } else {
            path->addRect(segRect);
        }

        context->setFillColor(highlightColor_);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
    }

    void drawFrame(VSTGUI::CDrawContext* context,
                   const VSTGUI::CRect& bounds) const {
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path) return;
        path->addRoundRect(bounds, roundRadius_);
        context->setFrameColor(frameColor_);
        context->setLineWidth(1.0);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
    }

    void drawDividers(VSTGUI::CDrawContext* context,
                      const VSTGUI::CRect& bounds) const {
        if (segments_.size() <= 1) return;
        context->setFrameColor(frameColor_);
        context->setLineWidth(1.0);
        for (size_t i = 1; i < segments_.size(); ++i) {
            double x = segments_[i].rect.left;
            context->drawLine(
                VSTGUI::CPoint(x, bounds.top + 1),
                VSTGUI::CPoint(x, bounds.bottom - 1));
        }
    }

    // =========================================================================
    // Icon Drawing
    // =========================================================================

    void drawIconOnly(VSTGUI::CDrawContext* context,
                      const VSTGUI::CRect& rect,
                      const std::string& iconName,
                      const VSTGUI::CColor& color) const {
        if (iconName == "gear")          drawGearIcon(context, rect, color);
        else if (iconName == "funnel")   drawFunnelIcon(context, rect, color);
        else if (iconName == "granular") drawGranularIcon(context, rect, color);
        else if (iconName == "spectral") drawSpectralIcon(context, rect, color);
        else if (iconName == "shimmer")  drawShimmerIcon(context, rect, color);
        else if (iconName == "tape")     drawTapeIcon(context, rect, color);
        else if (iconName == "bbd")      drawBBDIcon(context, rect, color);
        else if (iconName == "digital")  drawDigitalIcon(context, rect, color);
        else if (iconName == "pingpong") drawPingPongIcon(context, rect, color);
        else if (iconName == "reverse")  drawReverseIcon(context, rect, color);
        else if (iconName == "multitap") drawMultiTapIcon(context, rect, color);
        else if (iconName == "freeze")   drawFreezeIcon(context, rect, color);
        else                             drawFallbackDot(context, rect, color);
    }

    void drawIconAndText(VSTGUI::CDrawContext* context,
                         const VSTGUI::CRect& segRect,
                         const std::string& iconName,
                         const VSTGUI::CColor& color,
                         const std::string& segName) const {
        // Split segment: ~60% icon (top), ~40% text (bottom)
        double splitY = segRect.top + segRect.getHeight() * 0.6;
        VSTGUI::CRect iconRect(segRect.left, segRect.top + 2, segRect.right, splitY);
        VSTGUI::CRect textRect(segRect.left, splitY, segRect.right, segRect.bottom - 2);

        if (!iconName.empty())
            drawIconOnly(context, iconRect, iconName, color);
        if (!segName.empty()) {
            auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>(
                "Helvetica", static_cast<VSTGUI::CCoord>(textFontSize_));
            context->setFont(font);
            context->setFontColor(color);
            context->drawString(
                VSTGUI::UTF8String(segName).getPlatformString(), textRect,
                VSTGUI::kCenterText);
        }
    }

    void drawIcon(VSTGUI::CDrawContext* context,
                  const VSTGUI::CRect& segRect,
                  const std::string& iconName,
                  const VSTGUI::CColor& color,
                  const std::string& segName = {}) const {
        if (layoutMode_ == kIconAndText) {
            drawIconAndText(context, segRect, iconName, color, segName);
            return;
        }
        // kIconOrText: icon if available, else text
        if (!iconName.empty())
            drawIconOnly(context, segRect, iconName, color);
        else if (!segName.empty())
            drawTextLabel(context, segRect, segName, color);
        else
            drawFallbackDot(context, segRect, color);
    }

    void drawGearIcon(VSTGUI::CDrawContext* context,
                      const VSTGUI::CRect& segRect,
                      const VSTGUI::CColor& color) const {
        double cx = segRect.left + segRect.getWidth() / 2.0;
        double cy = segRect.top + segRect.getHeight() / 2.0;
        double dim = std::min(segRect.getWidth(), segRect.getHeight())
                     * static_cast<double>(iconSize_);
        double outerR = dim / 2.0;
        double innerR = outerR * 0.55;
        double toothW = outerR * 0.35;

        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path) return;

        constexpr int kTeeth = 6;
        constexpr double kPi = 3.14159265358979323846;

        // Build gear outline: alternating inner/outer arcs
        for (int i = 0; i < kTeeth; ++i) {
            double angle = (static_cast<double>(i) / kTeeth) * 2.0 * kPi - kPi / 2.0;
            double halfTooth = toothW / outerR * 0.5;

            // Outer tooth corners
            double a1 = angle - halfTooth;
            double a2 = angle + halfTooth;
            double ox1 = cx + outerR * std::cos(a1);
            double oy1 = cy + outerR * std::sin(a1);
            double ox2 = cx + outerR * std::cos(a2);
            double oy2 = cy + outerR * std::sin(a2);

            // Inner valley corners (midpoint between teeth)
            double midAngle = angle + kPi / kTeeth;
            double halfValley = halfTooth * 0.8;
            double v1 = midAngle - halfValley;
            double v2 = midAngle + halfValley;
            double ix1 = cx + innerR * std::cos(v1);
            double iy1 = cy + innerR * std::sin(v1);
            double ix2 = cx + innerR * std::cos(v2);
            double iy2 = cy + innerR * std::sin(v2);

            if (i == 0)
                path->beginSubpath(VSTGUI::CPoint(ox1, oy1));
            else
                path->addLine(VSTGUI::CPoint(ox1, oy1));

            path->addLine(VSTGUI::CPoint(ox2, oy2));
            path->addLine(VSTGUI::CPoint(ix1, iy1));
            path->addLine(VSTGUI::CPoint(ix2, iy2));
        }
        path->closeSubpath();

        // Center hole (drawn as separate circle that gets subtracted visually)
        double holeR = innerR * 0.45;
        VSTGUI::CRect holeRect(cx - holeR, cy - holeR, cx + holeR, cy + holeR);
        path->addEllipse(holeRect);

        context->setFillColor(color);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilledEvenOdd);
    }

    void drawFunnelIcon(VSTGUI::CDrawContext* context,
                        const VSTGUI::CRect& segRect,
                        const VSTGUI::CColor& color) const {
        double cx = segRect.left + segRect.getWidth() / 2.0;
        double cy = segRect.top + segRect.getHeight() / 2.0;
        double dim = std::min(segRect.getWidth(), segRect.getHeight())
                     * static_cast<double>(iconSize_);
        double halfW = dim / 2.0;
        double halfH = dim / 2.0;

        // Funnel shape: wide top, narrow stem at bottom
        double topY = cy - halfH;
        double midY = cy + halfH * 0.1;
        double bottomY = cy + halfH;
        double stemHalfW = halfW * 0.15;

        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path) return;

        path->beginSubpath(VSTGUI::CPoint(cx - halfW, topY));
        path->addLine(VSTGUI::CPoint(cx + halfW, topY));
        path->addLine(VSTGUI::CPoint(cx + stemHalfW, midY));
        path->addLine(VSTGUI::CPoint(cx + stemHalfW, bottomY));
        path->addLine(VSTGUI::CPoint(cx - stemHalfW, bottomY));
        path->addLine(VSTGUI::CPoint(cx - stemHalfW, midY));
        path->closeSubpath();

        context->setFillColor(color);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
    }

    // ---- Delay mode icons (thin stroke line art) ----

    void drawGranularIcon(VSTGUI::CDrawContext* context,
                          const VSTGUI::CRect& segRect,
                          const VSTGUI::CColor& color) const {
        double cx = segRect.left + segRect.getWidth() / 2.0;
        double cy = segRect.top + segRect.getHeight() / 2.0;
        double dim = std::min(segRect.getWidth(), segRect.getHeight())
                     * static_cast<double>(iconSize_);
        double r = dim / 2.0;
        // Scattered particles at varied positions
        constexpr double kOffsets[][2] = {
            {-0.5, -0.6}, {0.4, -0.4}, {-0.2, -0.1}, {0.6, 0.1},
            {-0.6, 0.4}, {0.1, 0.5}, {0.5, 0.6}
        };
        constexpr double kRadii[] = {2.0, 1.5, 2.5, 1.8, 2.0, 1.5, 2.2};
        for (int i = 0; i < 7; ++i) {
            double px = cx + kOffsets[i][0] * r;
            double py = cy + kOffsets[i][1] * r;
            double pr = kRadii[i] * (dim / 20.0);
            auto path = VSTGUI::owned(context->createGraphicsPath());
            if (!path) continue;
            path->addEllipse(VSTGUI::CRect(px - pr, py - pr, px + pr, py + pr));
            context->setFillColor(color);
            context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
        }
    }

    void drawSpectralIcon(VSTGUI::CDrawContext* context,
                          const VSTGUI::CRect& segRect,
                          const VSTGUI::CColor& color) const {
        double cx = segRect.left + segRect.getWidth() / 2.0;
        double cy = segRect.top + segRect.getHeight() / 2.0;
        double dim = std::min(segRect.getWidth(), segRect.getHeight())
                     * static_cast<double>(iconSize_);
        double r = dim / 2.0;
        // 5 vertical bars of varying height
        constexpr double kHeights[] = {0.4, 0.7, 1.0, 0.8, 0.5};
        constexpr int kBars = 5;
        double barW = dim / (kBars * 2.0);
        double totalW = barW * kBars + barW * (kBars - 1) * 0.5;
        double startX = cx - totalW / 2.0;
        double baseY = cy + r * 0.5;
        for (int i = 0; i < kBars; ++i) {
            double bx = startX + i * barW * 1.5;
            double bh = kHeights[i] * dim * 0.8;
            VSTGUI::CRect barRect(bx, baseY - bh, bx + barW, baseY);
            context->setFillColor(color);
            context->drawRect(barRect, VSTGUI::kDrawFilled);
        }
    }

    void drawShimmerIcon(VSTGUI::CDrawContext* context,
                         const VSTGUI::CRect& segRect,
                         const VSTGUI::CColor& color) const {
        double cx = segRect.left + segRect.getWidth() / 2.0;
        double cy = segRect.top + segRect.getHeight() / 2.0;
        double dim = std::min(segRect.getWidth(), segRect.getHeight())
                     * static_cast<double>(iconSize_);
        double r = dim / 2.0;
        // Upward chevron
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path) return;
        path->beginSubpath(VSTGUI::CPoint(cx - r * 0.5, cy + r * 0.2));
        path->addLine(VSTGUI::CPoint(cx, cy - r * 0.5));
        path->addLine(VSTGUI::CPoint(cx + r * 0.5, cy + r * 0.2));
        context->setFrameColor(color);
        context->setLineWidth(strokeWidth_);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
        // Sparkle dots
        constexpr double kSparkles[][2] = {{0.3, -0.6}, {-0.4, -0.3}, {0.5, 0.0}};
        for (auto& sp : kSparkles) {
            double sx = cx + sp[0] * r;
            double sy = cy + sp[1] * r;
            double sr = 1.2 * (dim / 20.0);
            auto dot = VSTGUI::owned(context->createGraphicsPath());
            if (!dot) continue;
            dot->addEllipse(VSTGUI::CRect(sx - sr, sy - sr, sx + sr, sy + sr));
            context->setFillColor(color);
            context->drawGraphicsPath(dot, VSTGUI::CDrawContext::kPathFilled);
        }
    }

    void drawTapeIcon(VSTGUI::CDrawContext* context,
                      const VSTGUI::CRect& segRect,
                      const VSTGUI::CColor& color) const {
        double cx = segRect.left + segRect.getWidth() / 2.0;
        double cy = segRect.top + segRect.getHeight() / 2.0;
        double dim = std::min(segRect.getWidth(), segRect.getHeight())
                     * static_cast<double>(iconSize_);
        double r = dim / 2.0;
        double reelR = r * 0.35;
        double leftX = cx - r * 0.45;
        double rightX = cx + r * 0.45;
        // Two reels
        for (double rx : {leftX, rightX}) {
            auto circle = VSTGUI::owned(context->createGraphicsPath());
            if (!circle) continue;
            circle->addEllipse(VSTGUI::CRect(
                rx - reelR, cy - reelR, rx + reelR, cy + reelR));
            context->setFrameColor(color);
            context->setLineWidth(strokeWidth_);
            context->drawGraphicsPath(circle, VSTGUI::CDrawContext::kPathStroked);
            // Hub dot
            double hubR = reelR * 0.25;
            auto hub = VSTGUI::owned(context->createGraphicsPath());
            if (!hub) continue;
            hub->addEllipse(VSTGUI::CRect(
                rx - hubR, cy - hubR, rx + hubR, cy + hubR));
            context->setFillColor(color);
            context->drawGraphicsPath(hub, VSTGUI::CDrawContext::kPathFilled);
        }
        // Tape path line connecting tops
        auto tape = VSTGUI::owned(context->createGraphicsPath());
        if (!tape) return;
        tape->beginSubpath(VSTGUI::CPoint(leftX + reelR, cy - reelR * 0.3));
        tape->addLine(VSTGUI::CPoint(rightX - reelR, cy - reelR * 0.3));
        context->setFrameColor(color);
        context->setLineWidth(strokeWidth_ * 0.8);
        context->drawGraphicsPath(tape, VSTGUI::CDrawContext::kPathStroked);
    }

    void drawBBDIcon(VSTGUI::CDrawContext* context,
                     const VSTGUI::CRect& segRect,
                     const VSTGUI::CColor& color) const {
        double cx = segRect.left + segRect.getWidth() / 2.0;
        double cy = segRect.top + segRect.getHeight() / 2.0;
        double dim = std::min(segRect.getWidth(), segRect.getHeight())
                     * static_cast<double>(iconSize_);
        double r = dim / 2.0;
        // 5 small squares in a row connected by lines
        constexpr int kBuckets = 5;
        double boxSize = dim / 8.0;
        double totalW = r * 1.6;
        double spacing = totalW / (kBuckets - 1);
        double startX = cx - totalW / 2.0;
        // Connecting line
        auto line = VSTGUI::owned(context->createGraphicsPath());
        if (line) {
            line->beginSubpath(VSTGUI::CPoint(startX, cy));
            line->addLine(VSTGUI::CPoint(startX + totalW, cy));
            context->setFrameColor(color);
            context->setLineWidth(strokeWidth_ * 0.7);
            context->drawGraphicsPath(line, VSTGUI::CDrawContext::kPathStroked);
        }
        // Buckets
        for (int i = 0; i < kBuckets; ++i) {
            double bx = startX + i * spacing;
            VSTGUI::CRect box(bx - boxSize, cy - boxSize,
                              bx + boxSize, cy + boxSize);
            context->setFrameColor(color);
            context->setLineWidth(strokeWidth_);
            context->drawRect(box, VSTGUI::kDrawStroked);
        }
    }

    void drawDigitalIcon(VSTGUI::CDrawContext* context,
                         const VSTGUI::CRect& segRect,
                         const VSTGUI::CColor& color) const {
        double cx = segRect.left + segRect.getWidth() / 2.0;
        double cy = segRect.top + segRect.getHeight() / 2.0;
        double dim = std::min(segRect.getWidth(), segRect.getHeight())
                     * static_cast<double>(iconSize_);
        double r = dim / 2.0;
        // Square waveform
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path) return;
        double x0 = cx - r * 0.7;
        double hi = cy - r * 0.4;
        double lo = cy + r * 0.4;
        double step = r * 0.35;
        path->beginSubpath(VSTGUI::CPoint(x0, lo));
        path->addLine(VSTGUI::CPoint(x0, hi));
        path->addLine(VSTGUI::CPoint(x0 + step, hi));
        path->addLine(VSTGUI::CPoint(x0 + step, lo));
        path->addLine(VSTGUI::CPoint(x0 + 2 * step, lo));
        path->addLine(VSTGUI::CPoint(x0 + 2 * step, hi));
        path->addLine(VSTGUI::CPoint(x0 + 3 * step, hi));
        path->addLine(VSTGUI::CPoint(x0 + 3 * step, lo));
        path->addLine(VSTGUI::CPoint(x0 + 4 * step, lo));
        context->setFrameColor(color);
        context->setLineWidth(strokeWidth_);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
    }

    void drawPingPongIcon(VSTGUI::CDrawContext* context,
                          const VSTGUI::CRect& segRect,
                          const VSTGUI::CColor& color) const {
        double cx = segRect.left + segRect.getWidth() / 2.0;
        double cy = segRect.top + segRect.getHeight() / 2.0;
        double dim = std::min(segRect.getWidth(), segRect.getHeight())
                     * static_cast<double>(iconSize_);
        double r = dim / 2.0;
        double wallL = cx - r * 0.6;
        double wallR = cx + r * 0.6;
        // Two walls
        context->setFrameColor(color);
        context->setLineWidth(strokeWidth_);
        context->drawLine(VSTGUI::CPoint(wallL, cy - r * 0.6),
                          VSTGUI::CPoint(wallL, cy + r * 0.6));
        context->drawLine(VSTGUI::CPoint(wallR, cy - r * 0.6),
                          VSTGUI::CPoint(wallR, cy + r * 0.6));
        // Zigzag bounce
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path) return;
        path->beginSubpath(VSTGUI::CPoint(wallL, cy - r * 0.4));
        path->addLine(VSTGUI::CPoint(wallR, cy - r * 0.1));
        path->addLine(VSTGUI::CPoint(wallL, cy + r * 0.2));
        path->addLine(VSTGUI::CPoint(wallR, cy + r * 0.5));
        context->setLineWidth(strokeWidth_ * 0.8);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
    }

    void drawReverseIcon(VSTGUI::CDrawContext* context,
                         const VSTGUI::CRect& segRect,
                         const VSTGUI::CColor& color) const {
        double cx = segRect.left + segRect.getWidth() / 2.0;
        double cy = segRect.top + segRect.getHeight() / 2.0;
        double dim = std::min(segRect.getWidth(), segRect.getHeight())
                     * static_cast<double>(iconSize_);
        double r = dim / 2.0;
        // Left-pointing play triangle
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path) return;
        path->beginSubpath(VSTGUI::CPoint(cx + r * 0.4, cy - r * 0.5));
        path->addLine(VSTGUI::CPoint(cx - r * 0.5, cy));
        path->addLine(VSTGUI::CPoint(cx + r * 0.4, cy + r * 0.5));
        path->closeSubpath();
        context->setFillColor(color);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
    }

    void drawMultiTapIcon(VSTGUI::CDrawContext* context,
                          const VSTGUI::CRect& segRect,
                          const VSTGUI::CColor& color) const {
        double cx = segRect.left + segRect.getWidth() / 2.0;
        double cy = segRect.top + segRect.getHeight() / 2.0;
        double dim = std::min(segRect.getWidth(), segRect.getHeight())
                     * static_cast<double>(iconSize_);
        double r = dim / 2.0;
        // 4 vertical lines at staggered positions, decreasing height
        constexpr double kXOff[] = {-0.5, -0.15, 0.2, 0.55};
        constexpr double kH[] = {1.0, 0.75, 0.55, 0.35};
        double baseY = cy + r * 0.5;
        context->setFrameColor(color);
        context->setLineWidth(strokeWidth_ * 1.2);
        for (int i = 0; i < 4; ++i) {
            double lx = cx + kXOff[i] * r;
            double h = kH[i] * r;
            context->drawLine(VSTGUI::CPoint(lx, baseY),
                              VSTGUI::CPoint(lx, baseY - h));
        }
    }

    void drawFreezeIcon(VSTGUI::CDrawContext* context,
                        const VSTGUI::CRect& segRect,
                        const VSTGUI::CColor& color) const {
        double cx = segRect.left + segRect.getWidth() / 2.0;
        double cy = segRect.top + segRect.getHeight() / 2.0;
        double dim = std::min(segRect.getWidth(), segRect.getHeight())
                     * static_cast<double>(iconSize_);
        double r = dim / 2.0 * 0.7;
        // Snowflake: 3 lines crossing at center (60° intervals) with end ticks
        constexpr double kPi = 3.14159265358979323846;
        context->setFrameColor(color);
        context->setLineWidth(strokeWidth_);
        for (int i = 0; i < 3; ++i) {
            double angle = i * kPi / 3.0;
            double cosA = std::cos(angle);
            double sinA = std::sin(angle);
            double x1 = cx + r * cosA;
            double y1 = cy + r * sinA;
            double x2 = cx - r * cosA;
            double y2 = cy - r * sinA;
            context->drawLine(VSTGUI::CPoint(x1, y1), VSTGUI::CPoint(x2, y2));
            // End ticks (perpendicular short lines)
            double tickLen = r * 0.25;
            double perpX = -sinA * tickLen;
            double perpY = cosA * tickLen;
            context->drawLine(VSTGUI::CPoint(x1 - perpX, y1 - perpY),
                              VSTGUI::CPoint(x1 + perpX, y1 + perpY));
            context->drawLine(VSTGUI::CPoint(x2 - perpX, y2 - perpY),
                              VSTGUI::CPoint(x2 + perpX, y2 + perpY));
        }
    }

    void drawTextLabel(VSTGUI::CDrawContext* context,
                       const VSTGUI::CRect& segRect,
                       const std::string& text,
                       const VSTGUI::CColor& color) const {
        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>(
            "Helvetica", static_cast<VSTGUI::CCoord>(textFontSize_));
        context->setFont(font);
        context->setFontColor(color);
        context->drawString(
            VSTGUI::UTF8String(text).getPlatformString(), segRect,
            VSTGUI::kCenterText);
    }

    void drawFallbackDot(VSTGUI::CDrawContext* context,
                         const VSTGUI::CRect& segRect,
                         const VSTGUI::CColor& color) const {
        double cx = segRect.left + segRect.getWidth() / 2.0;
        double cy = segRect.top + segRect.getHeight() / 2.0;
        double r = 3.0;
        VSTGUI::CRect dotRect(cx - r, cy - r, cx + r, cy + r);
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path) return;
        path->addEllipse(dotRect);
        context->setFillColor(color);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
    }

    // =========================================================================
    // Geometry Helpers
    // =========================================================================

    static void addPartialRoundRect(VSTGUI::CGraphicsPath* path,
                                    const VSTGUI::CRect& r, double radius,
                                    bool tl, bool tr, bool br, bool bl) {
        // Build a rect with selective rounded corners
        double x1 = r.left, y1 = r.top, x2 = r.right, y2 = r.bottom;

        path->beginSubpath(VSTGUI::CPoint(x1 + (tl ? radius : 0), y1));

        // Top edge -> top-right corner
        path->addLine(VSTGUI::CPoint(x2 - (tr ? radius : 0), y1));
        if (tr) {
            VSTGUI::CRect arcRect(x2 - 2 * radius, y1, x2, y1 + 2 * radius);
            path->addArc(arcRect, 270.0, 360.0, true);
        }

        // Right edge -> bottom-right corner
        path->addLine(VSTGUI::CPoint(x2, y2 - (br ? radius : 0)));
        if (br) {
            VSTGUI::CRect arcRect(x2 - 2 * radius, y2 - 2 * radius, x2, y2);
            path->addArc(arcRect, 0.0, 90.0, true);
        }

        // Bottom edge -> bottom-left corner
        path->addLine(VSTGUI::CPoint(x1 + (bl ? radius : 0), y2));
        if (bl) {
            VSTGUI::CRect arcRect(x1, y2 - 2 * radius, x1 + 2 * radius, y2);
            path->addArc(arcRect, 90.0, 180.0, true);
        }

        // Left edge -> top-left corner
        path->addLine(VSTGUI::CPoint(x1, y1 + (tl ? radius : 0)));
        if (tl) {
            VSTGUI::CRect arcRect(x1, y1, x1 + 2 * radius, y1 + 2 * radius);
            path->addArc(arcRect, 180.0, 270.0, true);
        }

        path->closeSubpath();
    }

    void computeSegmentRects() {
        if (segments_.empty()) return;
        VSTGUI::CRect vs = getViewSize();
        double segW = vs.getWidth() / static_cast<double>(segments_.size());
        for (size_t i = 0; i < segments_.size(); ++i) {
            segments_[i].rect = VSTGUI::CRect(
                vs.left + segW * static_cast<double>(i), vs.top,
                vs.left + segW * static_cast<double>(i + 1), vs.bottom);
        }
    }

    uint32_t hitTestSegment(const VSTGUI::CPoint& where) const {
        for (uint32_t i = 0; i < static_cast<uint32_t>(segments_.size()); ++i) {
            if (segments_[i].rect.pointInside(where))
                return i;
        }
        return kNoSegment;
    }

    void updateHoverTooltip(const VSTGUI::CPoint& where) {
        uint32_t seg = hitTestSegment(where);
        if (seg == hoverSegment_) return;
        hoverSegment_ = seg;
        if (seg != kNoSegment && seg < segments_.size())
            setTooltipText(VSTGUI::UTF8String(segments_[seg].name).data());
        else
            setTooltipText(nullptr);
    }

    // =========================================================================
    // String Helpers
    // =========================================================================

    static std::vector<std::string> splitComma(const std::string& s) {
        std::vector<std::string> result;
        std::istringstream stream(s);
        std::string token;
        while (std::getline(stream, token, ',')) {
            // Trim whitespace
            auto start = token.find_first_not_of(' ');
            auto end = token.find_last_not_of(' ');
            if (start != std::string::npos)
                result.push_back(token.substr(start, end - start + 1));
            else
                result.emplace_back();
        }
        return result;
    }

    template <typename Func>
    static std::string joinComma(const std::vector<Segment>& segs, Func f) {
        std::string result;
        for (size_t i = 0; i < segs.size(); ++i) {
            if (i > 0) result += ',';
            result += f(segs[i]);
        }
        return result;
    }

    // =========================================================================
    // Members
    // =========================================================================

    static constexpr uint32_t kNoSegment = 0xFFFFFFFF;

    std::vector<Segment> segments_;
    LayoutMode layoutMode_ = kIconOrText;
    VSTGUI::CColor selectedColor_{100, 200, 220, 255};
    VSTGUI::CColor unselectedColor_{120, 120, 130, 255};
    VSTGUI::CColor frameColor_{60, 60, 68, 255};
    VSTGUI::CColor highlightColor_{50, 50, 58, 255};
    double roundRadius_ = 3.0;
    float iconSize_ = 0.55f;
    double strokeWidth_ = 1.5;
    double textFontSize_ = 9.0;
    uint32_t hoverSegment_ = kNoSegment;
    bool inValueChanged_ = false;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct IconSegmentButtonCreator : VSTGUI::ViewCreatorAdapter {
    IconSegmentButtonCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override {
        return "IconSegmentButton";
    }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Icon Segment Button";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new IconSegmentButton(VSTGUI::CRect(0, 0, 40, 18), nullptr, -1);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override {
        auto* btn = dynamic_cast<IconSegmentButton*>(view);
        if (!btn)
            return false;

        // Layout mode
        if (auto val = attributes.getAttributeValue("layout-mode")) {
            if (*val == "icon-and-text")
                btn->setLayoutMode(IconSegmentButton::kIconAndText);
            else
                btn->setLayoutMode(IconSegmentButton::kIconOrText);
        }

        // Segment configuration
        if (auto val = attributes.getAttributeValue("segment-names"))
            btn->setSegmentNames(*val);
        if (auto val = attributes.getAttributeValue("segment-icons"))
            btn->setSegmentIcons(*val);

        // Color attributes
        VSTGUI::CColor color;
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("selected-color"), color, description))
            btn->setSelectedColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("unselected-color"), color, description))
            btn->setUnselectedColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("frame-color"), color, description))
            btn->setFrameColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("highlight-color"), color, description))
            btn->setHighlightColor(color);

        // Numeric attributes
        double d;
        if (attributes.getDoubleAttribute("round-radius", d))
            btn->setRoundRadius(d);
        if (attributes.getDoubleAttribute("icon-size", d))
            btn->setIconSize(static_cast<float>(d));
        if (attributes.getDoubleAttribute("stroke-width", d))
            btn->setStrokeWidth(d);
        if (attributes.getDoubleAttribute("font-size", d))
            btn->setTextFontSize(d);

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("layout-mode");
        attributeNames.emplace_back("segment-names");
        attributeNames.emplace_back("segment-icons");
        attributeNames.emplace_back("selected-color");
        attributeNames.emplace_back("unselected-color");
        attributeNames.emplace_back("frame-color");
        attributeNames.emplace_back("highlight-color");
        attributeNames.emplace_back("round-radius");
        attributeNames.emplace_back("icon-size");
        attributeNames.emplace_back("stroke-width");
        attributeNames.emplace_back("font-size");
        return true;
    }

    AttrType getAttributeType(
        const std::string& attributeName) const override {
        if (attributeName == "layout-mode") return kStringType;
        if (attributeName == "segment-names") return kStringType;
        if (attributeName == "segment-icons") return kStringType;
        if (attributeName == "selected-color") return kColorType;
        if (attributeName == "unselected-color") return kColorType;
        if (attributeName == "frame-color") return kColorType;
        if (attributeName == "highlight-color") return kColorType;
        if (attributeName == "round-radius") return kFloatType;
        if (attributeName == "icon-size") return kFloatType;
        if (attributeName == "stroke-width") return kFloatType;
        if (attributeName == "font-size") return kFloatType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override {
        auto* btn = dynamic_cast<IconSegmentButton*>(view);
        if (!btn)
            return false;

        if (attributeName == "layout-mode") {
            stringValue = (btn->getLayoutMode() == IconSegmentButton::kIconAndText)
                ? "icon-and-text" : "icon-or-text";
            return true;
        }
        if (attributeName == "segment-names") {
            stringValue = btn->getSegmentNames();
            return true;
        }
        if (attributeName == "segment-icons") {
            stringValue = btn->getSegmentIcons();
            return true;
        }
        if (attributeName == "selected-color") {
            VSTGUI::UIViewCreator::colorToString(
                btn->getSelectedColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "unselected-color") {
            VSTGUI::UIViewCreator::colorToString(
                btn->getUnselectedColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "frame-color") {
            VSTGUI::UIViewCreator::colorToString(
                btn->getFrameColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "highlight-color") {
            VSTGUI::UIViewCreator::colorToString(
                btn->getHighlightColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "round-radius") {
            stringValue = VSTGUI::UIAttributes::doubleToString(
                btn->getRoundRadius());
            return true;
        }
        if (attributeName == "icon-size") {
            stringValue = VSTGUI::UIAttributes::doubleToString(
                btn->getIconSize());
            return true;
        }
        if (attributeName == "stroke-width") {
            stringValue = VSTGUI::UIAttributes::doubleToString(
                btn->getStrokeWidth());
            return true;
        }
        if (attributeName == "font-size") {
            stringValue = VSTGUI::UIAttributes::doubleToString(
                btn->getTextFontSize());
            return true;
        }
        return false;
    }
};

inline IconSegmentButtonCreator gIconSegmentButtonCreator;

} // namespace Krate::Plugins
