#pragma once

// ==============================================================================
// ActionButton - Momentary Push Button with Configurable Icon
// ==============================================================================
// A CControl momentary button for triggering one-shot actions.
// On mouse down: shows pressed visual state.
// On mouse up (inside button): fires valueChanged(1.0), then resets to 0.0.
//
// Icon styles:
// - "invert": Two opposing vertical arrows (swap/invert pattern).
// - "shift-left": Left-pointing arrow (shift pattern left).
// - "shift-right": Right-pointing arrow (shift pattern right).
// - "regen": Circular refresh arrow (regenerate pattern).
//
// All drawing uses CGraphicsPath (no bitmaps, cross-platform).
//
// Registered as "ActionButton" via VSTGUI ViewCreator system.
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

namespace Krate::Plugins {

// ==============================================================================
// Enums
// ==============================================================================

enum class ActionIconStyle { kInvert, kShiftLeft, kShiftRight, kRegen };

// ==============================================================================
// ActionButton Control
// ==============================================================================

class ActionButton : public VSTGUI::CControl {
public:
    // =========================================================================
    // Construction
    // =========================================================================

    ActionButton(const VSTGUI::CRect& size,
                 VSTGUI::IControlListener* listener,
                 int32_t tag)
        : CControl(size, listener, tag) {
        setMin(0.0f);
        setMax(1.0f);
    }

    ActionButton(const ActionButton& other)
        : CControl(other)
        , color_(other.color_)
        , pressedColor_(other.pressedColor_)
        , iconSize_(other.iconSize_)
        , strokeWidth_(other.strokeWidth_)
        , iconStyle_(other.iconStyle_) {}

    CLASS_METHODS(ActionButton, CControl)

    // =========================================================================
    // Attributes (ViewCreator)
    // =========================================================================

    void setColor(const VSTGUI::CColor& color) { color_ = color; setDirty(); }
    [[nodiscard]] VSTGUI::CColor getColor() const { return color_; }

    void setPressedColor(const VSTGUI::CColor& color) { pressedColor_ = color; setDirty(); }
    [[nodiscard]] VSTGUI::CColor getPressedColor() const { return pressedColor_; }

    void setIconSize(float size) { iconSize_ = size; setDirty(); }
    [[nodiscard]] float getIconSize() const { return iconSize_; }

    void setStrokeWidth(VSTGUI::CCoord width) { strokeWidth_ = width; setDirty(); }
    [[nodiscard]] VSTGUI::CCoord getStrokeWidth() const { return strokeWidth_; }

    void setIconStyle(ActionIconStyle style) { iconStyle_ = style; setDirty(); }
    [[nodiscard]] ActionIconStyle getIconStyle() const { return iconStyle_; }

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

    // =========================================================================
    // Drawing
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        VSTGUI::CColor activeColor = pressed_ ? pressedColor_ : color_;

        switch (iconStyle_) {
            case ActionIconStyle::kInvert:
                drawInvertIcon(context, activeColor);
                break;
            case ActionIconStyle::kShiftLeft:
                drawShiftIcon(context, activeColor, -1.0);
                break;
            case ActionIconStyle::kShiftRight:
                drawShiftIcon(context, activeColor, 1.0);
                break;
            case ActionIconStyle::kRegen:
                drawRegenIcon(context, activeColor);
                break;
        }

        setDirty(false);
    }

    // =========================================================================
    // Mouse Interaction (momentary)
    // =========================================================================

    VSTGUI::CMouseEventResult onMouseEntered(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {
        if (auto* frame = getFrame())
            frame->setCursor(VSTGUI::kCursorHand);
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseExited(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {
        if (auto* frame = getFrame())
            frame->setCursor(VSTGUI::kCursorDefault);
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {
        if (!(buttons & VSTGUI::kLButton))
            return VSTGUI::kMouseEventNotHandled;

        pressed_ = true;
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& /*buttons*/) override {
        bool inside = getViewSize().pointInside(where);
        if (inside != pressed_) {
            pressed_ = inside;
            invalid();
        }
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseUp(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& /*buttons*/) override {
        if (pressed_) {
            pressed_ = false;
            beginEdit();
            setValueNormalized(1.0f);
            valueChanged();
            setValueNormalized(0.0f);
            endEdit();
            invalid();
        }
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseCancel() override {
        pressed_ = false;
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

    // Re-entrancy guard (same as ToggleButton)
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

    void drawInvertIcon(VSTGUI::CDrawContext* context,
                        const VSTGUI::CColor& color) const {
        VSTGUI::CRect vs = getViewSize();

        double viewW = vs.getWidth();
        double viewH = vs.getHeight();
        double dim = std::min(viewW, viewH) * static_cast<double>(iconSize_);
        double cx = vs.left + viewW / 2.0;
        double cy = vs.top + viewH / 2.0;

        // Two vertical arrows side by side: ↑ on left, ↓ on right
        // Arrow spacing: arrows are separated by ~40% of icon width
        double halfSpacing = dim * 0.2;
        double arrowHeight = dim * 0.45;
        double headSize = dim * 0.18;

        context->setFrameColor(color);
        context->setFillColor(color);
        context->setLineWidth(strokeWidth_);
        context->setLineStyle(VSTGUI::CLineStyle(
            VSTGUI::CLineStyle::kLineCapRound,
            VSTGUI::CLineStyle::kLineJoinRound));

        // Left arrow: pointing UP
        double leftX = cx - halfSpacing;
        double upTop = cy - arrowHeight;
        double upBottom = cy + arrowHeight;

        // Shaft
        context->drawLine(VSTGUI::CPoint(leftX, upTop),
                          VSTGUI::CPoint(leftX, upBottom));

        // Arrowhead (filled triangle pointing up)
        auto upHead = VSTGUI::owned(context->createGraphicsPath());
        if (upHead) {
            upHead->beginSubpath(VSTGUI::CPoint(leftX, upTop - headSize * 0.3));
            upHead->addLine(VSTGUI::CPoint(leftX - headSize, upTop + headSize));
            upHead->addLine(VSTGUI::CPoint(leftX + headSize, upTop + headSize));
            upHead->closeSubpath();
            context->drawGraphicsPath(upHead, VSTGUI::CDrawContext::kPathFilled);
        }

        // Right arrow: pointing DOWN
        double rightX = cx + halfSpacing;
        double downTop = cy - arrowHeight;
        double downBottom = cy + arrowHeight;

        // Shaft
        context->drawLine(VSTGUI::CPoint(rightX, downTop),
                          VSTGUI::CPoint(rightX, downBottom));

        // Arrowhead (filled triangle pointing down)
        auto downHead = VSTGUI::owned(context->createGraphicsPath());
        if (downHead) {
            downHead->beginSubpath(VSTGUI::CPoint(rightX, downBottom + headSize * 0.3));
            downHead->addLine(VSTGUI::CPoint(rightX - headSize, downBottom - headSize));
            downHead->addLine(VSTGUI::CPoint(rightX + headSize, downBottom - headSize));
            downHead->closeSubpath();
            context->drawGraphicsPath(downHead, VSTGUI::CDrawContext::kPathFilled);
        }
    }

    void drawShiftIcon(VSTGUI::CDrawContext* context,
                       const VSTGUI::CColor& color,
                       double direction) const {
        VSTGUI::CRect vs = getViewSize();

        double viewW = vs.getWidth();
        double viewH = vs.getHeight();
        double dim = std::min(viewW, viewH) * static_cast<double>(iconSize_);
        double cx = vs.left + viewW / 2.0;
        double cy = vs.top + viewH / 2.0;

        double halfLen = dim * 0.4;
        double headSize = dim * 0.22;

        context->setFrameColor(color);
        context->setFillColor(color);
        context->setLineWidth(strokeWidth_);
        context->setLineStyle(VSTGUI::CLineStyle(
            VSTGUI::CLineStyle::kLineCapRound,
            VSTGUI::CLineStyle::kLineJoinRound));

        // Horizontal shaft
        double x1 = cx - halfLen * direction;
        double x2 = cx + halfLen * direction;
        context->drawLine(VSTGUI::CPoint(x1, cy),
                          VSTGUI::CPoint(x2, cy));

        // Arrowhead at the tip (x2 end)
        auto head = VSTGUI::owned(context->createGraphicsPath());
        if (head) {
            double tipX = x2 + headSize * 0.3 * direction;
            head->beginSubpath(VSTGUI::CPoint(tipX, cy));
            head->addLine(VSTGUI::CPoint(x2 - headSize * direction, cy - headSize));
            head->addLine(VSTGUI::CPoint(x2 - headSize * direction, cy + headSize));
            head->closeSubpath();
            context->drawGraphicsPath(head, VSTGUI::CDrawContext::kPathFilled);
        }
    }

    void drawRegenIcon(VSTGUI::CDrawContext* context,
                       const VSTGUI::CColor& color) const {
        VSTGUI::CRect vs = getViewSize();

        double viewW = vs.getWidth();
        double viewH = vs.getHeight();
        double dim = std::min(viewW, viewH) * static_cast<double>(iconSize_);
        double cx = vs.left + viewW / 2.0;
        double cy = vs.top + viewH / 2.0;
        double radius = dim * 0.4;
        double headSize = dim * 0.18;

        context->setFrameColor(color);
        context->setFillColor(color);
        context->setLineWidth(strokeWidth_);
        context->setLineStyle(VSTGUI::CLineStyle(
            VSTGUI::CLineStyle::kLineCapRound,
            VSTGUI::CLineStyle::kLineJoinRound));

        // Circular arc (~300 degrees, gap at top-right)
        VSTGUI::CRect arcRect(cx - radius, cy - radius,
                               cx + radius, cy + radius);

        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (path) {
            // Arc from 30 degrees to 330 degrees (300-degree sweep, gap at top-right)
            path->addArc(arcRect, 30.0, 330.0, true);
            context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
        }

        // Arrowhead at the end of the arc (at 330 degrees = upper-right)
        // 330 degrees in VSTGUI = 30 degrees above East = upper-right
        double endAngleRad = 330.0 * 3.14159265358979323846 / 180.0;
        double endX = cx + radius * std::cos(endAngleRad);
        double endY = cy + radius * std::sin(endAngleRad);

        // Arrow points clockwise along the arc tangent
        auto head = VSTGUI::owned(context->createGraphicsPath());
        if (head) {
            // Tangent at 330 deg points roughly toward 60 deg (perpendicular, clockwise)
            double tangentAngle = endAngleRad + 3.14159265358979323846 / 2.0;
            double cosT = std::cos(tangentAngle);
            double sinT = std::sin(tangentAngle);
            double perpX = -sinT;
            double perpY = cosT;

            head->beginSubpath(VSTGUI::CPoint(
                endX + cosT * headSize * 0.8, endY + sinT * headSize * 0.8));
            head->addLine(VSTGUI::CPoint(
                endX - perpX * headSize, endY - perpY * headSize));
            head->addLine(VSTGUI::CPoint(
                endX + perpX * headSize, endY + perpY * headSize));
            head->closeSubpath();
            context->drawGraphicsPath(head, VSTGUI::CDrawContext::kPathFilled);
        }
    }

    // =========================================================================
    // Members
    // =========================================================================

    VSTGUI::CColor color_{96, 96, 104, 255};          // #606068 gray (normal)
    VSTGUI::CColor pressedColor_{100, 180, 255, 255}; // #64B4FF blue (pressed)
    float iconSize_ = 0.6f;
    VSTGUI::CCoord strokeWidth_ = 2.0;
    ActionIconStyle iconStyle_ = ActionIconStyle::kInvert;
    bool pressed_ = false;
    bool inValueChanged_ = false;
};

// =============================================================================
// String <-> Enum Helpers
// =============================================================================

inline ActionIconStyle actionIconStyleFromString(const std::string& s) {
    if (s == "invert") return ActionIconStyle::kInvert;
    if (s == "shift-left") return ActionIconStyle::kShiftLeft;
    if (s == "shift-right") return ActionIconStyle::kShiftRight;
    if (s == "regen") return ActionIconStyle::kRegen;
    return ActionIconStyle::kInvert; // default
}

inline std::string actionIconStyleToString(ActionIconStyle style) {
    switch (style) {
        case ActionIconStyle::kInvert: return "invert";
        case ActionIconStyle::kShiftLeft: return "shift-left";
        case ActionIconStyle::kShiftRight: return "shift-right";
        case ActionIconStyle::kRegen: return "regen";
    }
    return "invert";
}

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct ActionButtonCreator : VSTGUI::ViewCreatorAdapter {
    ActionButtonCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override {
        return "ActionButton";
    }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Action Button";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new ActionButton(VSTGUI::CRect(0, 0, 26, 26), nullptr, -1);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override {
        auto* btn = dynamic_cast<ActionButton*>(view);
        if (!btn)
            return false;

        // Color attributes
        VSTGUI::CColor color;
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("color"), color, description))
            btn->setColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("pressed-color"), color, description))
            btn->setPressedColor(color);

        // Numeric attributes
        double d;
        if (attributes.getDoubleAttribute("icon-size", d))
            btn->setIconSize(static_cast<float>(d));
        if (attributes.getDoubleAttribute("stroke-width", d))
            btn->setStrokeWidth(d);

        // Icon style
        if (auto val = attributes.getAttributeValue("icon-style"))
            btn->setIconStyle(actionIconStyleFromString(*val));

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("color");
        attributeNames.emplace_back("pressed-color");
        attributeNames.emplace_back("icon-size");
        attributeNames.emplace_back("stroke-width");
        attributeNames.emplace_back("icon-style");
        return true;
    }

    AttrType getAttributeType(
        const std::string& attributeName) const override {
        if (attributeName == "color") return kColorType;
        if (attributeName == "pressed-color") return kColorType;
        if (attributeName == "icon-size") return kFloatType;
        if (attributeName == "stroke-width") return kFloatType;
        if (attributeName == "icon-style") return kListType;
        return kUnknownType;
    }

    bool getPossibleListValues(
        const std::string& attributeName,
        VSTGUI::IViewCreator::ConstStringPtrList& values) const override {
        if (attributeName == "icon-style") {
            static const std::string kInvert = "invert";
            static const std::string kShiftLeft = "shift-left";
            static const std::string kShiftRight = "shift-right";
            static const std::string kRegen = "regen";
            values.emplace_back(&kInvert);
            values.emplace_back(&kShiftLeft);
            values.emplace_back(&kShiftRight);
            values.emplace_back(&kRegen);
            return true;
        }
        return false;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override {
        auto* btn = dynamic_cast<ActionButton*>(view);
        if (!btn)
            return false;

        if (attributeName == "color") {
            VSTGUI::UIViewCreator::colorToString(
                btn->getColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "pressed-color") {
            VSTGUI::UIViewCreator::colorToString(
                btn->getPressedColor(), stringValue, desc);
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
        if (attributeName == "icon-style") {
            stringValue = actionIconStyleToString(btn->getIconStyle());
            return true;
        }
        return false;
    }
};

inline ActionButtonCreator gActionButtonCreator;

} // namespace Krate::Plugins
