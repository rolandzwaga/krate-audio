#pragma once

// ==============================================================================
// PowerButton - Vector-drawn IEC 5009 Power Toggle Button
// ==============================================================================
// A CControl toggle button rendering the standard power symbol (circle with
// vertical line at top). Click toggles between on (value=1) and off (value=0).
//
// Visual states:
// - On (value >= 0.5): icon/text drawn in configurable bright accent color
// - Off (value < 0.5): icon/text drawn in configurable dimmed/muted color
//
// When title is empty: draws the IEC 5009 power icon (arc + vertical line).
// When title is set: draws centered text label instead of the icon.
//
// All drawing uses CGraphicsPath (no bitmaps, cross-platform).
//
// Registered as "PowerButton" via VSTGUI ViewCreator system.
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
// PowerButton Control
// ==============================================================================

class PowerButton : public VSTGUI::CControl {
public:
    // =========================================================================
    // Construction
    // =========================================================================

    PowerButton(const VSTGUI::CRect& size,
                VSTGUI::IControlListener* listener,
                int32_t tag)
        : CControl(size, listener, tag) {
        setMin(0.0f);
        setMax(1.0f);
    }

    PowerButton(const PowerButton& other)
        : CControl(other)
        , onColor_(other.onColor_)
        , offColor_(other.offColor_)
        , iconSize_(other.iconSize_)
        , strokeWidth_(other.strokeWidth_)
        , title_(other.title_)
        , font_(other.font_) {}

    CLASS_METHODS(PowerButton, CControl)

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

        if (title_.empty()) {
            drawPowerIcon(context, activeColor);
        } else {
            drawTitle(context, activeColor);
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

    void drawTitle(VSTGUI::CDrawContext* context,
                   const VSTGUI::CColor& color) const {
        context->setFont(font_);
        context->setFontColor(color);
        context->drawString(VSTGUI::UTF8String(title_), getViewSize(),
                            VSTGUI::kCenterText);
    }

    // =========================================================================
    // Members
    // =========================================================================

    VSTGUI::CColor onColor_{100, 180, 255, 255};   // #64B4FF blue
    VSTGUI::CColor offColor_{96, 96, 104, 255};    // #606068 gray
    float iconSize_ = 0.6f;
    VSTGUI::CCoord strokeWidth_ = 2.0;
    std::string title_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_{VSTGUI::kNormalFontSmall};
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct PowerButtonCreator : VSTGUI::ViewCreatorAdapter {
    PowerButtonCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override {
        return "PowerButton";
    }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Power Button";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new PowerButton(VSTGUI::CRect(0, 0, 24, 24), nullptr, -1);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override {
        auto* btn = dynamic_cast<PowerButton*>(view);
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

        // Title
        if (auto val = attributes.getAttributeValue("title"))
            btn->setTitle(*val);

        // Font (resolved from IUIDescription named fonts, e.g. "~ NormalFontSmaller")
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
        if (attributeName == "title") return kStringType;
        if (attributeName == "font") return kFontType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override {
        auto* btn = dynamic_cast<PowerButton*>(view);
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

inline PowerButtonCreator gPowerButtonCreator;

} // namespace Krate::Plugins
