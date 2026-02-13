#pragma once

// ==============================================================================
// BipolarSlider - Centered Fill Slider Control for Modulation Amounts
// ==============================================================================
// A CControl that renders a bipolar slider with centered fill.
// Fill extends left from center for negative values, right for positive.
// Supports fine adjustment (Shift 0.1x) and Escape to cancel.
//
// Internal value: normalized [0.0, 1.0] (VST boundary requirement)
// Display value: bipolar [-1.0, +1.0] where 0.5 normalized = 0.0 bipolar
//
// Registered as "BipolarSlider" via VSTGUI ViewCreator system.
// Spec: 049-mod-matrix-grid (FR-007 to FR-010)
// ==============================================================================

#include "color_utils.h"

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/detail/uiviewcreatorattributes.h"

#include <algorithm>
#include <cmath>

namespace Krate::Plugins {

// ==============================================================================
// BipolarSlider Control
// ==============================================================================

class BipolarSlider : public VSTGUI::CControl {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kFineScale = 0.1f;
    static constexpr float kDefaultSensitivity = 1.0f / 200.0f; // 200px full range

    // =========================================================================
    // Construction
    // =========================================================================

    BipolarSlider(const VSTGUI::CRect& size,
                  VSTGUI::IControlListener* listener,
                  int32_t tag)
        : CControl(size, listener, tag) {
        setMin(0.0f);
        setMax(1.0f);
        setValue(0.5f); // center = 0 bipolar
    }

    BipolarSlider(const BipolarSlider& other)
        : CControl(other)
        , fillColor_(other.fillColor_)
        , trackColor_(other.trackColor_)
        , centerTickColor_(other.centerTickColor_)
        , dragging_(false)
        , preDragValue_(other.preDragValue_) {}

    CLASS_METHODS(BipolarSlider, CControl)

    // =========================================================================
    // Color Attributes (ViewCreator)
    // =========================================================================

    void setFillColor(const VSTGUI::CColor& color) { fillColor_ = color; setDirty(); }
    [[nodiscard]] VSTGUI::CColor getFillColor() const { return fillColor_; }

    void setTrackColor(const VSTGUI::CColor& color) { trackColor_ = color; setDirty(); }
    [[nodiscard]] VSTGUI::CColor getTrackColor() const { return trackColor_; }

    void setCenterTickColor(const VSTGUI::CColor& color) { centerTickColor_ = color; setDirty(); }
    [[nodiscard]] VSTGUI::CColor getCenterTickColor() const { return centerTickColor_; }

    // =========================================================================
    // Value Helpers
    // =========================================================================

    /// Convert normalized [0,1] to bipolar [-1,+1]
    [[nodiscard]] static float normalizedToBipolar(float normalized) {
        return normalized * 2.0f - 1.0f;
    }

    /// Convert bipolar [-1,+1] to normalized [0,1]
    [[nodiscard]] static float bipolarToNormalized(float bipolar) {
        return (bipolar + 1.0f) / 2.0f;
    }

    /// Get the current value as bipolar [-1,+1]
    [[nodiscard]] float getBipolarValue() const {
        return normalizedToBipolar(getValueNormalized());
    }

    // =========================================================================
    // Drawing (FR-007, FR-008)
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        VSTGUI::CRect r = getViewSize();
        VSTGUI::CCoord padding = 2.0;
        VSTGUI::CCoord trackHeight = 4.0;
        VSTGUI::CCoord centerX = r.left + r.getWidth() / 2.0;
        VSTGUI::CCoord trackTop = r.top + (r.getHeight() - trackHeight) / 2.0;
        VSTGUI::CCoord trackBottom = trackTop + trackHeight;
        VSTGUI::CCoord trackLeft = r.left + padding;
        VSTGUI::CCoord trackRight = r.right - padding;

        // Draw track background
        VSTGUI::CRect trackRect(trackLeft, trackTop, trackRight, trackBottom);
        context->setFillColor(trackColor_);
        context->drawRect(trackRect, VSTGUI::kDrawFilled);

        // Draw centered fill (from center to current value position)
        float normalized = getValueNormalized();
        VSTGUI::CCoord valueX = trackLeft + static_cast<VSTGUI::CCoord>(
            normalized * (trackRight - trackLeft));

        VSTGUI::CRect fillRect;
        if (normalized < 0.5f) {
            // Negative: fill from valueX to center
            fillRect = VSTGUI::CRect(valueX, trackTop, centerX, trackBottom);
        } else {
            // Positive: fill from center to valueX
            fillRect = VSTGUI::CRect(centerX, trackTop, valueX, trackBottom);
        }
        VSTGUI::CColor activeFill = (normalized < 0.5f)
            ? darkenColor(fillColor_, 0.55f) : fillColor_;
        context->setFillColor(activeFill);
        context->drawRect(fillRect, VSTGUI::kDrawFilled);

        // Draw center tick mark
        VSTGUI::CCoord tickTop = r.top + r.getHeight() * 0.2;
        VSTGUI::CCoord tickBottom = r.top + r.getHeight() * 0.8;
        context->setFrameColor(centerTickColor_);
        context->setLineWidth(1.0);
        context->drawLine(VSTGUI::CPoint(centerX, tickTop),
                          VSTGUI::CPoint(centerX, tickBottom));

        // Draw value indicator (small circle at current position)
        VSTGUI::CCoord indicatorRadius = 5.0;
        VSTGUI::CCoord indicatorY = r.top + r.getHeight() / 2.0;
        VSTGUI::CRect indicatorRect(
            valueX - indicatorRadius, indicatorY - indicatorRadius,
            valueX + indicatorRadius, indicatorY + indicatorRadius);
        context->setFillColor(activeFill);
        context->drawEllipse(indicatorRect, VSTGUI::kDrawFilled);

        setDirty(false);
    }

    // =========================================================================
    // Mouse Interaction (FR-009, FR-010)
    // =========================================================================

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {
        if (!(buttons & VSTGUI::kLButton))
            return VSTGUI::kMouseEventNotHandled;

        beginEdit();
        dragging_ = true;
        preDragValue_ = getValueNormalized();
        lastMouseX_ = where.x;
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {
        if (!dragging_)
            return VSTGUI::kMouseEventNotHandled;

        float sensitivity = kDefaultSensitivity;

        // Fine adjustment: Shift key = 0.1x sensitivity (FR-009)
        if (buttons.isShiftSet()) {
            sensitivity *= kFineScale;
        }

        // Horizontal drag: right = increase, left = decrease
        float delta = static_cast<float>(where.x - lastMouseX_) * sensitivity;
        lastMouseX_ = where.x;

        float newValue = std::clamp(getValueNormalized() + delta, 0.0f, 1.0f);
        setValueNormalized(newValue);
        valueChanged();
        invalid();

        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseUp(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {
        if (!dragging_)
            return VSTGUI::kMouseEventNotHandled;

        dragging_ = false;
        endEdit();
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseCancel() override {
        if (dragging_) {
            // Restore pre-drag value on Escape (FR-010)
            setValueNormalized(preDragValue_);
            valueChanged();
            invalid();
            dragging_ = false;
            endEdit();
        }
        return VSTGUI::kMouseEventHandled;
    }

private:
    VSTGUI::CColor fillColor_{220, 170, 60, 255};       // Gold accent
    VSTGUI::CColor trackColor_{50, 50, 55, 255};         // Dark track
    VSTGUI::CColor centerTickColor_{120, 120, 125, 255}; // Subtle center tick

    bool dragging_ = false;
    float preDragValue_ = 0.5f;
    VSTGUI::CCoord lastMouseX_ = 0.0;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct BipolarSliderCreator : VSTGUI::ViewCreatorAdapter {
    BipolarSliderCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override {
        return "BipolarSlider";
    }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Bipolar Slider";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new BipolarSlider(VSTGUI::CRect(0, 0, 120, 20), nullptr, -1);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override {
        auto* slider = dynamic_cast<BipolarSlider*>(view);
        if (!slider)
            return false;

        VSTGUI::CColor color;
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("fill-color"), color, description))
            slider->setFillColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("track-color"), color, description))
            slider->setTrackColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("center-tick-color"), color, description))
            slider->setCenterTickColor(color);

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("fill-color");
        attributeNames.emplace_back("track-color");
        attributeNames.emplace_back("center-tick-color");
        return true;
    }

    AttrType getAttributeType(
        const std::string& attributeName) const override {
        if (attributeName == "fill-color") return kColorType;
        if (attributeName == "track-color") return kColorType;
        if (attributeName == "center-tick-color") return kColorType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override {
        auto* slider = dynamic_cast<BipolarSlider*>(view);
        if (!slider)
            return false;

        if (attributeName == "fill-color") {
            VSTGUI::UIViewCreator::colorToString(
                slider->getFillColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "track-color") {
            VSTGUI::UIViewCreator::colorToString(
                slider->getTrackColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "center-tick-color") {
            VSTGUI::UIViewCreator::colorToString(
                slider->getCenterTickColor(), stringValue, desc);
            return true;
        }
        return false;
    }
};

inline BipolarSliderCreator gBipolarSliderCreator;

} // namespace Krate::Plugins
