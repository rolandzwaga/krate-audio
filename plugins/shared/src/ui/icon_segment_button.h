#pragma once

// ==============================================================================
// IconSegmentButton - Compact Segment Button with Vector Icons and Tooltips
// ==============================================================================
// A CControl segment button that displays named vector icons instead of text.
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
        , selectedColor_(other.selectedColor_)
        , unselectedColor_(other.unselectedColor_)
        , frameColor_(other.frameColor_)
        , highlightColor_(other.highlightColor_)
        , roundRadius_(other.roundRadius_)
        , iconSize_(other.iconSize_)
        , strokeWidth_(other.strokeWidth_)
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

        // Icons
        for (uint32_t i = 0; i < static_cast<uint32_t>(segments_.size()); ++i) {
            bool isSel = (i == selected);
            VSTGUI::CColor iconColor = isSel ? selectedColor_ : unselectedColor_;
            drawIcon(context, segments_[i].rect, segments_[i].iconName, iconColor);
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

    void drawIcon(VSTGUI::CDrawContext* context,
                  const VSTGUI::CRect& segRect,
                  const std::string& iconName,
                  const VSTGUI::CColor& color) const {
        if (iconName == "gear")
            drawGearIcon(context, segRect, color);
        else if (iconName == "funnel")
            drawFunnelIcon(context, segRect, color);
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
    VSTGUI::CColor selectedColor_{100, 200, 220, 255};
    VSTGUI::CColor unselectedColor_{120, 120, 130, 255};
    VSTGUI::CColor frameColor_{60, 60, 68, 255};
    VSTGUI::CColor highlightColor_{50, 50, 58, 255};
    double roundRadius_ = 3.0;
    float iconSize_ = 0.55f;
    double strokeWidth_ = 1.5;
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

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("segment-names");
        attributeNames.emplace_back("segment-icons");
        attributeNames.emplace_back("selected-color");
        attributeNames.emplace_back("unselected-color");
        attributeNames.emplace_back("frame-color");
        attributeNames.emplace_back("highlight-color");
        attributeNames.emplace_back("round-radius");
        attributeNames.emplace_back("icon-size");
        attributeNames.emplace_back("stroke-width");
        return true;
    }

    AttrType getAttributeType(
        const std::string& attributeName) const override {
        if (attributeName == "segment-names") return kStringType;
        if (attributeName == "segment-icons") return kStringType;
        if (attributeName == "selected-color") return kColorType;
        if (attributeName == "unselected-color") return kColorType;
        if (attributeName == "frame-color") return kColorType;
        if (attributeName == "highlight-color") return kColorType;
        if (attributeName == "round-radius") return kFloatType;
        if (attributeName == "icon-size") return kFloatType;
        if (attributeName == "stroke-width") return kFloatType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override {
        auto* btn = dynamic_cast<IconSegmentButton*>(view);
        if (!btn)
            return false;

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
        return false;
    }
};

inline IconSegmentButtonCreator gIconSegmentButtonCreator;

} // namespace Krate::Plugins
