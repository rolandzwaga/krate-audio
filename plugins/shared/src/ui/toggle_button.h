#pragma once

// ==============================================================================
// ToggleButton - Vector-drawn Toggle Button with Configurable Icon
// ==============================================================================
// A CControl toggle button with selectable icon styles. Click toggles between
// on (value=1) and off (value=0).
//
// Icon styles:
// - "power": IEC 5009 power symbol (circle with vertical line). Default.
// - "chevron": Directional chevron arrow with configurable on/off orientations.
//
// Visual states:
// - On (value >= 0.5): icon/text drawn in configurable bright accent color
// - Off (value < 0.5): icon/text drawn in configurable dimmed/muted color
//
// When title is set: draws centered text label instead of any icon.
//
// All drawing uses CGraphicsPath (no bitmaps, cross-platform).
//
// Registered as "ToggleButton" via VSTGUI ViewCreator system.
// ==============================================================================

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
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

enum class IconStyle { kPower, kChevron };
enum class Orientation { kRight, kDown, kLeft, kUp };

// ==============================================================================
// ToggleButton Control
// ==============================================================================

class ToggleButton : public VSTGUI::CControl {
public:
    // =========================================================================
    // Construction
    // =========================================================================

    ToggleButton(const VSTGUI::CRect& size,
                 VSTGUI::IControlListener* listener,
                 int32_t tag)
        : CControl(size, listener, tag) {
        setMin(0.0f);
        setMax(1.0f);
    }

    ToggleButton(const ToggleButton& other)
        : CControl(other)
        , onColor_(other.onColor_)
        , offColor_(other.offColor_)
        , iconSize_(other.iconSize_)
        , strokeWidth_(other.strokeWidth_)
        , iconStyle_(other.iconStyle_)
        , onOrientation_(other.onOrientation_)
        , offOrientation_(other.offOrientation_)
        , title_(other.title_)
        , font_(other.font_) {}

    CLASS_METHODS(ToggleButton, CControl)

    // =========================================================================
    // Color/Geometry Attributes (ViewCreator)
    // =========================================================================

    void setOnColor(const VSTGUI::CColor& color) { onColor_ = color; setDirty(); }
    [[nodiscard]] VSTGUI::CColor getOnColor() const { return onColor_; }

    void setOffColor(const VSTGUI::CColor& color) { offColor_ = color; setDirty(); }
    [[nodiscard]] VSTGUI::CColor getOffColor() const { return offColor_; }

    void setIconSize(float size) { iconSize_ = size; setDirty(); }
    [[nodiscard]] float getIconSize() const { return iconSize_; }

    void setStrokeWidth(VSTGUI::CCoord width) { strokeWidth_ = width; setDirty(); }
    [[nodiscard]] VSTGUI::CCoord getStrokeWidth() const { return strokeWidth_; }

    // =========================================================================
    // Icon Style Attributes
    // =========================================================================

    void setIconStyle(IconStyle style) { iconStyle_ = style; setDirty(); }
    [[nodiscard]] IconStyle getIconStyle() const { return iconStyle_; }

    void setOnOrientation(Orientation o) { onOrientation_ = o; setDirty(); }
    [[nodiscard]] Orientation getOnOrientation() const { return onOrientation_; }

    void setOffOrientation(Orientation o) { offOrientation_ = o; setDirty(); }
    [[nodiscard]] Orientation getOffOrientation() const { return offOrientation_; }

    // =========================================================================
    // Title/Font Attributes
    // =========================================================================

    void setTitle(const std::string& title) { title_ = title; setDirty(); }
    [[nodiscard]] const std::string& getTitle() const { return title_; }

    void setFont(VSTGUI::CFontRef font) {
        if (font)
            font_ = font;
        setDirty();
    }
    [[nodiscard]] VSTGUI::CFontRef getFont() const { return font_; }

    // =========================================================================
    // Drawing
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        bool isOn = getValueNormalized() >= 0.5f;
        VSTGUI::CColor activeColor = isOn ? onColor_ : offColor_;

        if (!title_.empty()) {
            drawTitle(context, activeColor);
        } else if (iconStyle_ == IconStyle::kChevron) {
            drawChevronIcon(context, activeColor, isOn);
        } else {
            drawPowerIcon(context, activeColor);
        }

        setDirty(false);
    }

    // =========================================================================
    // Mouse Interaction (click to toggle)
    // =========================================================================

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {
        if (!(buttons & VSTGUI::kLButton))
            return VSTGUI::kMouseEventNotHandled;

        beginEdit();
        float newVal = getValueNormalized() < 0.5f ? 1.0f : 0.0f;
        setValueNormalized(newVal);
        valueChanged();
        endEdit();
        invalid();
        return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
    }

    // Re-entrancy guard: VST3Editor's ParameterChangeListener calls
    // c->valueChanged() from updateControlValue() for non-parameter controls,
    // which re-enters this method and causes infinite recursion / stack overflow.
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

    void drawPowerIcon(VSTGUI::CDrawContext* context,
                       const VSTGUI::CColor& color) const {
        VSTGUI::CRect vs = getViewSize();

        // Icon dimensions: square, centered in view
        double viewW = vs.getWidth();
        double viewH = vs.getHeight();
        double dim = std::min(viewW, viewH) * static_cast<double>(iconSize_);
        double radius = dim / 2.0;
        double cx = vs.left + viewW / 2.0;
        double cy = vs.top + viewH / 2.0;

        // Circle arc (300 degrees, 60-degree gap at 12 o'clock)
        // VSTGUI angles: 0=East, clockwise. Top = 270 degrees.
        // Gap: 30 degrees each side of 270 -> arc from 300 to 240 (clockwise)
        VSTGUI::CRect arcRect(cx - radius, cy - radius,
                               cx + radius, cy + radius);

        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (path) {
            path->addArc(arcRect, 300.0, 240.0, true);

            context->setFrameColor(color);
            context->setLineWidth(strokeWidth_);
            context->setLineStyle(VSTGUI::CLineStyle(
                VSTGUI::CLineStyle::kLineCapRound,
                VSTGUI::CLineStyle::kLineJoinRound));
            context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
        }

        // Vertical line from top of icon to center ("I" in the gap)
        double lineTopY = cy - radius;
        double lineBottomY = cy;
        context->setFrameColor(color);
        context->drawLine(VSTGUI::CPoint(cx, lineTopY),
                          VSTGUI::CPoint(cx, lineBottomY));
    }

    void drawChevronIcon(VSTGUI::CDrawContext* context,
                         const VSTGUI::CColor& color,
                         bool isOn) const {
        VSTGUI::CRect vs = getViewSize();

        double viewW = vs.getWidth();
        double viewH = vs.getHeight();
        double dim = std::min(viewW, viewH) * static_cast<double>(iconSize_);
        double half = dim / 2.0;
        double cx = vs.left + viewW / 2.0;
        double cy = vs.top + viewH / 2.0;

        // Base chevron points to the right: ">"
        // Tip at +half, arms at -half. Draws relative to center, then rotates.
        double tipX = half;
        double armX = -half;
        double armY = half;

        Orientation orient = isOn ? onOrientation_ : offOrientation_;
        double angleDeg = orientationToDegrees(orient);
        double angleRad = angleDeg * 3.14159265358979323846 / 180.0;
        double cosA = std::cos(angleRad);
        double sinA = std::sin(angleRad);

        // Rotate and translate the 3 points
        auto rotate = [&](double x, double y) -> VSTGUI::CPoint {
            return VSTGUI::CPoint(cx + x * cosA - y * sinA,
                                  cy + x * sinA + y * cosA);
        };

        VSTGUI::CPoint tip = rotate(tipX, 0.0);
        VSTGUI::CPoint topArm = rotate(armX, -armY);
        VSTGUI::CPoint bottomArm = rotate(armX, armY);

        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (path) {
            path->beginSubpath(topArm);
            path->addLine(tip);
            path->addLine(bottomArm);
            path->closeSubpath();

            context->setFillColor(color);
            context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
        }
    }

    void drawTitle(VSTGUI::CDrawContext* context,
                   const VSTGUI::CColor& color) const {
        context->setFont(font_);
        context->setFontColor(color);
        context->drawString(VSTGUI::UTF8String(title_), getViewSize(),
                            VSTGUI::kCenterText);
    }

    static double orientationToDegrees(Orientation o) {
        switch (o) {
            case Orientation::kRight: return 0.0;
            case Orientation::kDown:  return 90.0;
            case Orientation::kLeft:  return 180.0;
            case Orientation::kUp:    return 270.0;
        }
        return 0.0;
    }

    // =========================================================================
    // Members
    // =========================================================================

    VSTGUI::CColor onColor_{100, 180, 255, 255};   // #64B4FF blue
    VSTGUI::CColor offColor_{96, 96, 104, 255};     // #606068 gray
    float iconSize_ = 0.6f;
    VSTGUI::CCoord strokeWidth_ = 2.0;
    IconStyle iconStyle_ = IconStyle::kPower;
    Orientation onOrientation_ = Orientation::kDown;
    Orientation offOrientation_ = Orientation::kRight;
    std::string title_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_{VSTGUI::kNormalFontSmall};
    bool inValueChanged_ = false;
};

// =============================================================================
// String ↔ Enum Helpers
// =============================================================================

inline IconStyle iconStyleFromString(const std::string& s) {
    if (s == "chevron") return IconStyle::kChevron;
    return IconStyle::kPower;
}

inline std::string iconStyleToString(IconStyle style) {
    switch (style) {
        case IconStyle::kChevron: return "chevron";
        default: return "power";
    }
}

inline Orientation orientationFromString(const std::string& s) {
    if (s == "up")    return Orientation::kUp;
    if (s == "down")  return Orientation::kDown;
    if (s == "left")  return Orientation::kLeft;
    return Orientation::kRight;
}

inline std::string orientationToString(Orientation o) {
    switch (o) {
        case Orientation::kUp:    return "up";
        case Orientation::kDown:  return "down";
        case Orientation::kLeft:  return "left";
        default: return "right";
    }
}

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct ToggleButtonCreator : VSTGUI::ViewCreatorAdapter {
    ToggleButtonCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override {
        return "ToggleButton";
    }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Toggle Button";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new ToggleButton(VSTGUI::CRect(0, 0, 24, 24), nullptr, -1);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override {
        auto* btn = dynamic_cast<ToggleButton*>(view);
        if (!btn)
            return false;

        // Color attributes
        VSTGUI::CColor color;
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("on-color"), color, description))
            btn->setOnColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("off-color"), color, description))
            btn->setOffColor(color);

        // Numeric attributes
        double d;
        if (attributes.getDoubleAttribute("icon-size", d))
            btn->setIconSize(static_cast<float>(d));
        if (attributes.getDoubleAttribute("stroke-width", d))
            btn->setStrokeWidth(d);

        // Icon style
        if (auto val = attributes.getAttributeValue("icon-style"))
            btn->setIconStyle(iconStyleFromString(*val));

        // Orientation attributes
        if (auto val = attributes.getAttributeValue("on-orientation"))
            btn->setOnOrientation(orientationFromString(*val));
        if (auto val = attributes.getAttributeValue("off-orientation"))
            btn->setOffOrientation(orientationFromString(*val));

        // Title
        if (auto val = attributes.getAttributeValue("title"))
            btn->setTitle(*val);

        // Font (resolved from IUIDescription named fonts)
        if (auto val = attributes.getAttributeValue("font")) {
            if (description) {
                auto font = description->getFont(val->data());
                if (font)
                    btn->setFont(font);
            }
        }

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("on-color");
        attributeNames.emplace_back("off-color");
        attributeNames.emplace_back("icon-size");
        attributeNames.emplace_back("stroke-width");
        attributeNames.emplace_back("icon-style");
        attributeNames.emplace_back("on-orientation");
        attributeNames.emplace_back("off-orientation");
        attributeNames.emplace_back("title");
        attributeNames.emplace_back("font");
        return true;
    }

    AttrType getAttributeType(
        const std::string& attributeName) const override {
        if (attributeName == "on-color") return kColorType;
        if (attributeName == "off-color") return kColorType;
        if (attributeName == "icon-size") return kFloatType;
        if (attributeName == "stroke-width") return kFloatType;
        if (attributeName == "icon-style") return kListType;
        if (attributeName == "on-orientation") return kListType;
        if (attributeName == "off-orientation") return kListType;
        if (attributeName == "title") return kStringType;
        if (attributeName == "font") return kFontType;
        return kUnknownType;
    }

    bool getPossibleListValues(
        const std::string& attributeName,
        VSTGUI::IViewCreator::ConstStringPtrList& values) const override {
        if (attributeName == "icon-style") {
            static const std::string kPower = "power";
            static const std::string kChevron = "chevron";
            values.emplace_back(&kPower);
            values.emplace_back(&kChevron);
            return true;
        }
        if (attributeName == "on-orientation" ||
            attributeName == "off-orientation") {
            static const std::string kUp = "up";
            static const std::string kDown = "down";
            static const std::string kLeft = "left";
            static const std::string kRight = "right";
            values.emplace_back(&kUp);
            values.emplace_back(&kDown);
            values.emplace_back(&kLeft);
            values.emplace_back(&kRight);
            return true;
        }
        return false;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override {
        auto* btn = dynamic_cast<ToggleButton*>(view);
        if (!btn)
            return false;

        if (attributeName == "on-color") {
            VSTGUI::UIViewCreator::colorToString(
                btn->getOnColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "off-color") {
            VSTGUI::UIViewCreator::colorToString(
                btn->getOffColor(), stringValue, desc);
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
            stringValue = iconStyleToString(btn->getIconStyle());
            return true;
        }
        if (attributeName == "on-orientation") {
            stringValue = orientationToString(btn->getOnOrientation());
            return true;
        }
        if (attributeName == "off-orientation") {
            stringValue = orientationToString(btn->getOffOrientation());
            return true;
        }
        if (attributeName == "title") {
            stringValue = btn->getTitle();
            return true;
        }
        if (attributeName == "font") {
            if (desc) {
                auto fontName = desc->lookupFontName(btn->getFont());
                if (fontName)
                    stringValue = fontName;
            }
            return true;
        }
        return false;
    }
};

inline ToggleButtonCreator gToggleButtonCreator;

} // namespace Krate::Plugins
