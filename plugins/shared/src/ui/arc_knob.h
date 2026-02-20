#pragma once

// ==============================================================================
// ArcKnob - Minimal arc-style knob with gradient trail and modulation ring
// ==============================================================================
// A sober, minimalist knob control for Krate Audio plugins.
//
// Visual elements (back to front):
// 1. Guide ring: faint 270-degree arc showing full travel path
// 2. Value arc: 1px gradient arc from start to current indicator position
//    (darker further from indicator, subtle effect)
// 3. Modulation ring: optional inner arc showing bidirectional mod range
// 4. Indicator: 4px radial tick pointing inward from the arc circle
//
// Overrides CKnobBase mouse interaction with vertical linear tracking
// (drag up = increase, drag down = decrease, shift for precision).
// Mouse wheel and keyboard still work. Default angles: 7 o'clock to 5 o'clock (270 deg).
//
// Registered as "ArcKnob" via VSTGUI ViewCreator system.
// ==============================================================================

#include "color_utils.h"

#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/detail/uiviewcreatorattributes.h"

#include "public.sdk/source/vst/vstguieditor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace Krate::Plugins {

// ==============================================================================
// ArcKnob Control
// ==============================================================================

class ArcKnob : public VSTGUI::CKnobBase {
public:
    ArcKnob(const VSTGUI::CRect& size, VSTGUI::IControlListener* listener,
            int32_t tag)
        : CKnobBase(size, listener, tag, nullptr) {}

    ArcKnob(const ArcKnob& other)
        : CKnobBase(other)
        , modRange_(other.modRange_)
        , arcColor_(other.arcColor_)
        , modColor_(other.modColor_)
        , guideColor_(other.guideColor_)
        , indicatorLength_(other.indicatorLength_)
        , arcLineWidth_(other.arcLineWidth_)
        , modArcLineWidth_(other.modArcLineWidth_) {}

    // =========================================================================
    // Modulation API
    // =========================================================================

    /// Set the current modulation range (bipolar, [-1, +1]).
    /// Positive values sweep above and below the knob value.
    void setModulationRange(float range) noexcept {
        if (std::abs(range - modRange_) > 0.0005f) {
            modRange_ = range;
            setDirty();
        }
    }

    [[nodiscard]] float getModulationRange() const noexcept { return modRange_; }

    // =========================================================================
    // Color Configuration
    // =========================================================================

    void setArcColor(VSTGUI::CColor color) noexcept { arcColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getArcColor() const noexcept { return arcColor_; }

    void setModColor(VSTGUI::CColor color) noexcept { modColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getModColor() const noexcept { return modColor_; }

    void setGuideColor(VSTGUI::CColor color) noexcept { guideColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getGuideColor() const noexcept { return guideColor_; }

    // =========================================================================
    // Geometry Configuration
    // =========================================================================

    void setIndicatorLength(VSTGUI::CCoord length) noexcept { indicatorLength_ = length; }
    [[nodiscard]] VSTGUI::CCoord getIndicatorLength() const noexcept { return indicatorLength_; }

    void setArcLineWidth(VSTGUI::CCoord width) noexcept { arcLineWidth_ = width; }
    [[nodiscard]] VSTGUI::CCoord getArcLineWidth() const noexcept { return arcLineWidth_; }

    void setModArcLineWidth(VSTGUI::CCoord width) noexcept { modArcLineWidth_ = width; }
    [[nodiscard]] VSTGUI::CCoord getModArcLineWidth() const noexcept { return modArcLineWidth_; }

    // =========================================================================
    // Drawing
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        drawGuideRing(context);
        drawValueArc(context);
        drawModulationArc(context);
        drawIndicator(context);

        setDirty(false);
    }

    // =========================================================================
    // Mouse Interaction (vertical linear tracking)
    // =========================================================================

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override {
        if (!buttons.isLeftButton())
            return VSTGUI::kMouseEventNotHandled;

        beginEdit();

        mouseState_.firstPoint = where;
        mouseState_.entryValue = value;
        mouseState_.range = knobRange;
        if (buttons & kZoomModifier)
            mouseState_.range *= zoomFactor;
        mouseState_.coef = (getMax() - getMin()) / mouseState_.range;
        mouseState_.oldButton = buttons();  // Use int32_t assignment to avoid deprecated implicit copy
        mouseState_.active = true;

        showValuePopup();

        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override {
        if (!buttons.isLeftButton() || !mouseState_.active)
            return VSTGUI::kMouseEventNotHandled;

        auto diff = mouseState_.firstPoint.y - where.y; // up = increase

        if (buttons != mouseState_.oldButton) {
            mouseState_.range = knobRange;
            if (buttons & kZoomModifier)
                mouseState_.range *= zoomFactor;
            float newCoef = (getMax() - getMin()) / mouseState_.range;
            mouseState_.entryValue +=
                static_cast<float>(diff) * (mouseState_.coef - newCoef);
            mouseState_.coef = newCoef;
            mouseState_.oldButton = buttons();  // Use int32_t assignment to avoid deprecated implicit copy
        }

        value = mouseState_.entryValue +
                static_cast<float>(diff) * mouseState_.coef;
        bounceValue();

        if (value != getOldValue())
            valueChanged();
        if (isDirty())
            invalid();

        updateValuePopup();

        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseUp(
        VSTGUI::CPoint& /*where*/,
        const VSTGUI::CButtonState& /*buttons*/) override {
        if (mouseState_.active) {
            hideValuePopup();
            mouseState_.active = false;
            endEdit();
        }
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseCancel() override {
        if (mouseState_.active) {
            hideValuePopup();
            value = mouseState_.entryValue;
            if (isDirty()) {
                valueChanged();
                invalid();
            }
            mouseState_.active = false;
            endEdit();
        }
        return VSTGUI::kMouseEventHandled;
    }

    bool removed(VSTGUI::CView* parent) override {
        hideValuePopup();
        return CKnobBase::removed(parent);
    }

    CLASS_METHODS(ArcKnob, CKnobBase)

private:
    // =========================================================================
    // Angle Helpers
    // =========================================================================

    /// Convert normalized value [0,1] to angle in degrees.
    /// Uses CKnobBase's startAngle and rangeAngle (radians internally).
    [[nodiscard]] double valueToAngleDeg(float value) const {
        double rad = static_cast<double>(startAngle) +
                     static_cast<double>(value) * static_cast<double>(rangeAngle);
        return rad * (180.0 / VSTGUI::Constants::pi);
    }

    /// Convert angle in degrees to a point at given radius from center.
    [[nodiscard]] VSTGUI::CPoint angleToPoint(double angleDeg, double radius) const {
        double angleRad = angleDeg * (VSTGUI::Constants::pi / 180.0);
        VSTGUI::CRect vs = getViewSize();
        double cx = vs.left + vs.getWidth() / 2.0;
        double cy = vs.top + vs.getHeight() / 2.0;
        return VSTGUI::CPoint(cx + std::cos(angleRad) * radius,
                              cy + std::sin(angleRad) * radius);
    }

    /// Get the arc rectangle (centered in view, inset for indicator).
    [[nodiscard]] VSTGUI::CRect getArcRect() const {
        VSTGUI::CRect vs = getViewSize();
        double dim = std::min(vs.getWidth(), vs.getHeight());
        double arcRadius = dim / 2.0 - indicatorLength_ / 2.0;
        double cx = vs.left + vs.getWidth() / 2.0;
        double cy = vs.top + vs.getHeight() / 2.0;
        return VSTGUI::CRect(cx - arcRadius, cy - arcRadius,
                              cx + arcRadius, cy + arcRadius);
    }

    /// Get the arc radius.
    [[nodiscard]] double getArcRadius() const {
        VSTGUI::CRect vs = getViewSize();
        double dim = std::min(vs.getWidth(), vs.getHeight());
        return dim / 2.0 - indicatorLength_ / 2.0;
    }

    // =========================================================================
    // Drawing Helpers
    // =========================================================================

    /// Step 1: Faint 270-degree arc showing the full travel path.
    void drawGuideRing(VSTGUI::CDrawContext* context) const {
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path)
            return;

        VSTGUI::CRect arcRect = getArcRect();
        double startDeg = valueToAngleDeg(0.0f);
        double endDeg = valueToAngleDeg(1.0f);

        path->addArc(arcRect, startDeg, endDeg, true);

        context->setFrameColor(guideColor_);
        context->setLineWidth(arcLineWidth_);
        context->setLineStyle(VSTGUI::kLineSolid);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
    }

    /// Step 2: Gradient arc from start to current value.
    /// Color interpolates from dark (at start) to bright (near indicator).
    void drawValueArc(VSTGUI::CDrawContext* context) const {
        float value = getValueNormalized();
        if (value < 0.005f)
            return; // Nothing to draw at minimum

        VSTGUI::CRect arcRect = getArcRect();

        // Gradient: divide into segments with interpolated colors
        // Dark end = arcColor * 0.4, Bright end = arcColor * 0.85
        VSTGUI::CColor darkEnd = darkenColor(arcColor_, 0.4f);
        VSTGUI::CColor brightEnd = darkenColor(arcColor_, 0.85f);

        constexpr int kSegments = 16;
        int segCount = std::max(1, static_cast<int>(kSegments * value));

        context->setLineWidth(arcLineWidth_);
        context->setLineStyle(VSTGUI::kLineSolid);

        for (int i = 0; i < segCount; ++i) {
            float segStart = static_cast<float>(i) / static_cast<float>(segCount) * value;
            float segEnd = static_cast<float>(i + 1) / static_cast<float>(segCount) * value;

            // t goes from 0 (dark) to 1 (bright) across the arc
            float t = static_cast<float>(i + 1) / static_cast<float>(segCount);
            VSTGUI::CColor segColor = lerpColor(darkEnd, brightEnd, t);

            double segStartDeg = valueToAngleDeg(segStart);
            double segEndDeg = valueToAngleDeg(segEnd);

            auto segPath = VSTGUI::owned(context->createGraphicsPath());
            if (!segPath)
                continue;

            segPath->addArc(arcRect, segStartDeg, segEndDeg, true);
            context->setFrameColor(segColor);
            context->drawGraphicsPath(segPath,
                                       VSTGUI::CDrawContext::kPathStroked);
        }
    }

    /// Step 3: Inner modulation arc (bidirectional from knob value).
    void drawModulationArc(VSTGUI::CDrawContext* context) const {
        if (std::abs(modRange_) < 0.001f)
            return;

        float value = getValueNormalized();
        float modLow = std::clamp(value - std::abs(modRange_), 0.0f, 1.0f);
        float modHigh = std::clamp(value + std::abs(modRange_), 0.0f, 1.0f);

        if (std::abs(modHigh - modLow) < 0.001f)
            return;

        // Inner ring: smaller radius
        double arcRadius = getArcRadius();
        double modRadius = arcRadius - arcLineWidth_ / 2.0 - modArcLineWidth_ / 2.0 - 2.0;
        if (modRadius < 1.0)
            return;

        VSTGUI::CRect vs = getViewSize();
        double cx = vs.left + vs.getWidth() / 2.0;
        double cy = vs.top + vs.getHeight() / 2.0;
        VSTGUI::CRect modRect(cx - modRadius, cy - modRadius,
                                cx + modRadius, cy + modRadius);

        double lowDeg = valueToAngleDeg(modLow);
        double highDeg = valueToAngleDeg(modHigh);

        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path)
            return;

        path->addArc(modRect, lowDeg, highDeg, true);
        context->setFrameColor(modColor_);
        context->setLineWidth(modArcLineWidth_);
        context->setLineStyle(VSTGUI::kLineSolid);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
    }

    /// Step 4: Radial tick mark at current value, pointing inward.
    void drawIndicator(VSTGUI::CDrawContext* context) const {
        float value = getValueNormalized();
        double angleDeg = valueToAngleDeg(value);
        double arcRadius = getArcRadius();

        // Outer point: on the arc circle
        VSTGUI::CPoint outer = angleToPoint(angleDeg, arcRadius);
        // Inner point: indicatorLength_ pixels toward center
        VSTGUI::CPoint inner = angleToPoint(angleDeg,
                                             arcRadius - indicatorLength_);

        context->setFrameColor(arcColor_);
        context->setLineWidth(2.0);
        context->setLineStyle(VSTGUI::CLineStyle(VSTGUI::CLineStyle::kLineCapRound));
        context->drawLine(outer, inner);
    }

    // =========================================================================
    // Value Popup Helpers
    // =========================================================================

    /// Navigate view hierarchy to get the VST3 EditController.
    [[nodiscard]] Steinberg::Vst::EditController* getEditController() const {
        auto* frame = getFrame();
        if (!frame)
            return nullptr;
        auto* editor = dynamic_cast<Steinberg::Vst::VSTGUIEditor*>(frame->getEditor());
        if (!editor)
            return nullptr;
        return editor->getController();
    }

    /// Get the formatted parameter value string via EditController.
    /// Falls back to percentage display if controller is unavailable.
    bool getFormattedValue(std::string& result) const {
        if (getTag() >= 0) {
            auto* controller = getEditController();
            if (controller) {
                Steinberg::Vst::String128 str128{};
                if (controller->getParamStringByValue(
                        static_cast<Steinberg::Vst::ParamID>(getTag()),
                        getValueNormalized(), str128) == Steinberg::kResultOk) {
                    result.clear();
                    for (int i = 0; i < 128 && str128[i] != 0; ++i)
                        result += static_cast<char>(str128[i]);
                    if (!result.empty())
                        return true;
                }
            }
        }
        // Fallback: display percentage
        int pct = static_cast<int>(getValueNormalized() * 100.0f + 0.5f);
        result = std::to_string(pct) + "%";
        return true;
    }

    /// Show the value popup below the knob.
    void showValuePopup() {
        auto* frame = getFrame();
        if (!frame || valuePopup_)
            return;

        std::string text;
        if (!getFormattedValue(text))
            return;

        // Estimate popup dimensions
        constexpr VSTGUI::CCoord kCharWidth = 7.0;
        constexpr VSTGUI::CCoord kPaddingH = 12.0;
        constexpr VSTGUI::CCoord kPopupHeight = 20.0;
        constexpr VSTGUI::CCoord kGap = 4.0;

        auto popupWidth = static_cast<VSTGUI::CCoord>(text.size()) * kCharWidth + kPaddingH * 2;
        popupWidth = std::max(popupWidth, 36.0);

        // Position below knob center in frame coordinates.
        // CView::localToFrame doesn't add the view's own offset, so start
        // in parent coordinates (using getViewSize() which is in parent coords).
        VSTGUI::CRect vs = getViewSize();
        VSTGUI::CPoint bottomCenter(
            vs.left + vs.getWidth() / 2.0, vs.bottom);
        localToFrame(bottomCenter);

        VSTGUI::CRect popupRect(
            bottomCenter.x - popupWidth / 2.0,
            bottomCenter.y + kGap,
            bottomCenter.x + popupWidth / 2.0,
            bottomCenter.y + kGap + kPopupHeight);

        // Create styled label
        int32_t style = VSTGUI::CParamDisplay::kRoundRectStyle
                      | VSTGUI::CParamDisplay::kNoFrame;
        valuePopup_ = new VSTGUI::CTextLabel(popupRect, text.c_str(), nullptr, style);

        auto* font = new VSTGUI::CFontDesc("", 11);
        valuePopup_->setFont(font);
        font->forget();

        valuePopup_->setFontColor(VSTGUI::CColor(240, 240, 240));
        valuePopup_->setBackColor(VSTGUI::CColor(30, 30, 30, 220));
        valuePopup_->setRoundRectRadius(4.0);
        valuePopup_->setHoriAlign(VSTGUI::kCenterText);
        valuePopup_->setMouseEnabled(false);

        frame->addView(valuePopup_);
    }

    /// Update the popup text with the current parameter value.
    void updateValuePopup() {
        if (!valuePopup_)
            return;

        std::string text;
        if (!getFormattedValue(text))
            return;

        valuePopup_->setText(text.c_str());

        // Resize to fit new text
        constexpr VSTGUI::CCoord kCharWidth = 7.0;
        constexpr VSTGUI::CCoord kPaddingH = 12.0;

        auto newWidth = static_cast<VSTGUI::CCoord>(text.size()) * kCharWidth + kPaddingH * 2;
        newWidth = std::max(newWidth, 36.0);

        VSTGUI::CRect r = valuePopup_->getViewSize();
        VSTGUI::CCoord centerX = (r.left + r.right) / 2.0;
        r.left = centerX - newWidth / 2.0;
        r.right = centerX + newWidth / 2.0;
        valuePopup_->setViewSize(r);
        valuePopup_->setMouseableArea(r);

        valuePopup_->invalid();
    }

    /// Remove the popup from the frame.
    void hideValuePopup() {
        if (!valuePopup_)
            return;
        auto* frame = getFrame();
        if (frame)
            frame->removeView(valuePopup_, true);
        valuePopup_ = nullptr;
    }

    // =========================================================================
    // State
    // =========================================================================

    // Linear mouse tracking state
    struct MouseState {
        VSTGUI::CPoint firstPoint;
        float entryValue = 0.0f;
        float range = 0.0f;
        float coef = 0.0f;
        VSTGUI::CButtonState oldButton;
        bool active = false;
    };
    MouseState mouseState_;

    VSTGUI::CTextLabel* valuePopup_ = nullptr;  // Owned by frame when visible

    float modRange_ = 0.0f;

    VSTGUI::CColor arcColor_{220, 180, 100, 255};      // Warm gold default
    VSTGUI::CColor modColor_{130, 215, 255, 210};      // Semi-transparent cyan
    VSTGUI::CColor guideColor_{255, 255, 255, 40};     // Very dim white

    VSTGUI::CCoord indicatorLength_ = 4.0;
    VSTGUI::CCoord arcLineWidth_ = 1.0;
    VSTGUI::CCoord modArcLineWidth_ = 1.0;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================
// Registers "ArcKnob" with the VSTGUI UIViewFactory.
// getBaseViewName() -> "CControl" ensures all CControl attributes
// (control-tag, default-value, min-value, max-value, etc.) are applied.

struct ArcKnobCreator : VSTGUI::ViewCreatorAdapter {
    ArcKnobCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override { return "ArcKnob"; }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override { return "Arc Knob"; }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new ArcKnob(VSTGUI::CRect(0, 0, 40, 40), nullptr, -1);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override {
        auto* knob = dynamic_cast<ArcKnob*>(view);
        if (!knob)
            return false;

        // Color attributes
        VSTGUI::CColor color;
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("arc-color"), color, description))
            knob->setArcColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("mod-color"), color, description))
            knob->setModColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("guide-color"), color, description))
            knob->setGuideColor(color);

        // Numeric attributes
        double d;
        if (attributes.getDoubleAttribute("indicator-length", d))
            knob->setIndicatorLength(d);
        if (attributes.getDoubleAttribute("arc-width", d))
            knob->setArcLineWidth(d);
        if (attributes.getDoubleAttribute("mod-arc-width", d))
            knob->setModArcLineWidth(d);

        // Also apply CKnobBase attributes (angle-start, angle-range, etc.)
        if (attributes.getDoubleAttribute("angle-start", d)) {
            d = d / 180.0 * VSTGUI::Constants::pi;
            knob->setStartAngle(static_cast<float>(d));
        }
        if (attributes.getDoubleAttribute("angle-range", d)) {
            d = d / 180.0 * VSTGUI::Constants::pi;
            knob->setRangeAngle(static_cast<float>(d));
        }
        if (attributes.getDoubleAttribute("zoom-factor", d))
            knob->setZoomFactor(static_cast<float>(d));

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("arc-color");
        attributeNames.emplace_back("mod-color");
        attributeNames.emplace_back("guide-color");
        attributeNames.emplace_back("indicator-length");
        attributeNames.emplace_back("arc-width");
        attributeNames.emplace_back("mod-arc-width");
        attributeNames.emplace_back("angle-start");
        attributeNames.emplace_back("angle-range");
        attributeNames.emplace_back("zoom-factor");
        return true;
    }

    AttrType getAttributeType(const std::string& attributeName) const override {
        if (attributeName == "arc-color") return kColorType;
        if (attributeName == "mod-color") return kColorType;
        if (attributeName == "guide-color") return kColorType;
        if (attributeName == "indicator-length") return kFloatType;
        if (attributeName == "arc-width") return kFloatType;
        if (attributeName == "mod-arc-width") return kFloatType;
        if (attributeName == "angle-start") return kFloatType;
        if (attributeName == "angle-range") return kFloatType;
        if (attributeName == "zoom-factor") return kFloatType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override {
        auto* knob = dynamic_cast<ArcKnob*>(view);
        if (!knob)
            return false;

        if (attributeName == "arc-color") {
            VSTGUI::UIViewCreator::colorToString(
                knob->getArcColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "mod-color") {
            VSTGUI::UIViewCreator::colorToString(
                knob->getModColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "guide-color") {
            VSTGUI::UIViewCreator::colorToString(
                knob->getGuideColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "indicator-length") {
            stringValue = VSTGUI::UIAttributes::doubleToString(
                knob->getIndicatorLength());
            return true;
        }
        if (attributeName == "arc-width") {
            stringValue = VSTGUI::UIAttributes::doubleToString(
                knob->getArcLineWidth());
            return true;
        }
        if (attributeName == "mod-arc-width") {
            stringValue = VSTGUI::UIAttributes::doubleToString(
                knob->getModArcLineWidth());
            return true;
        }
        if (attributeName == "angle-start") {
            stringValue = VSTGUI::UIAttributes::doubleToString(
                knob->getStartAngle() / VSTGUI::Constants::pi * 180.0, 5);
            return true;
        }
        if (attributeName == "angle-range") {
            stringValue = VSTGUI::UIAttributes::doubleToString(
                knob->getRangeAngle() / VSTGUI::Constants::pi * 180.0, 5);
            return true;
        }
        if (attributeName == "zoom-factor") {
            stringValue = VSTGUI::UIAttributes::doubleToString(
                knob->getZoomFactor());
            return true;
        }
        return false;
    }
};

// Inline variable (C++17) - safe for inclusion from multiple translation units.
// Include this header from each plugin's entry.cpp to register the view type.
inline ArcKnobCreator gArcKnobCreator;

} // namespace Krate::Plugins
