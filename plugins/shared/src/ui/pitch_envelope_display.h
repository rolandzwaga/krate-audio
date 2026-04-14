#pragma once

// ==============================================================================
// PitchEnvelopeDisplay - Interactive Pitch-Envelope Editor/Display
// ==============================================================================
// A shared VSTGUI CControl subclass for visualising and editing a one-segment
// pitch-envelope (drum-synth style: start pitch -> end pitch over Time, shaped
// by Curve). Patterned on ADSRDisplay.
//
// Parameters (all normalised [0, 1] at the VST boundary):
//   - Start pitch (0 = low, 1 = high) at t=0 (left edge)
//   - End pitch   (0 = low, 1 = high) at t=Time (right edge)
//   - Time        (horizontal position of the End handle)
//   - Curve       (maps to power-curve amount via 2*curveN - 1)
//
// Interaction:
//   - Drag Start handle vertically -> Start param
//   - Drag End   handle vertically -> End param
//   - Drag Time  handle horizontally -> Time param
//   - Drag curve mid-segment (vertical delta) -> Curve param
//
// This component is plugin-agnostic: it communicates via ParameterCallback
// and configurable parameter IDs. Registered as "PitchEnvelopeDisplay" via
// VSTGUI ViewCreator.
// ==============================================================================

#include "color_utils.h"

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/detail/uiviewcreatorattributes.h"

#include <krate/dsp/core/curve_table.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace Krate::Plugins {

// ==============================================================================
// PitchEnvelopeDisplay Control
// ==============================================================================

class PitchEnvelopeDisplay : public VSTGUI::CControl {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kControlPointRadius = 10.0f;       // hit radius
    static constexpr float kControlPointDrawRadius = 4.0f;    // visual radius
    static constexpr float kHitRadius = 10.0f;                // hit test radius
    static constexpr float kPadding = 6.0f;
    static constexpr float kCurveDragSensitivity = 0.005f;
    static constexpr float kLabelStripHeight = 14.0f;

    static constexpr int kCurveResolution = 64;

    static constexpr float kMinViewHeightForGrid = 40.0f;
    static constexpr float kMinViewHeightForLabels = 60.0f;

    // =========================================================================
    // Types
    // =========================================================================

    enum class DragTarget {
        None,
        Start,      // vertical only
        End,        // vertical only
        Time,       // horizontal only
        Curve       // vertical delta on the segment body
    };

    using ParameterCallback = std::function<void(uint32_t paramId, float normalizedValue)>;
    using EditCallback = std::function<void(uint32_t paramId)>;

    // =========================================================================
    // Construction
    // =========================================================================

    PitchEnvelopeDisplay(const VSTGUI::CRect& size,
                         VSTGUI::IControlListener* listener,
                         int32_t tag)
        : CControl(size, listener, tag) {}

    PitchEnvelopeDisplay(const PitchEnvelopeDisplay& other)
        : CControl(other)
        , startN_(other.startN_)
        , endN_(other.endN_)
        , timeN_(other.timeN_)
        , curveN_(other.curveN_)
        , startParamId_(other.startParamId_)
        , endParamId_(other.endParamId_)
        , timeParamId_(other.timeParamId_)
        , curveParamId_(other.curveParamId_)
        , backgroundColor_(other.backgroundColor_)
        , gridColor_(other.gridColor_)
        , strokeColor_(other.strokeColor_)
        , fillColor_(other.fillColor_)
        , handleColor_(other.handleColor_)
        , handleActiveColor_(other.handleActiveColor_)
        , textColor_(other.textColor_) {}

    // =========================================================================
    // Parameter Value Setters (called by controller for sync)
    // =========================================================================

    void setStartNormalized(float n) {
        if (isDragging_) return;
        startN_ = std::clamp(n, 0.0f, 1.0f);
        setDirty();
    }

    void setEndNormalized(float n) {
        if (isDragging_) return;
        endN_ = std::clamp(n, 0.0f, 1.0f);
        setDirty();
    }

    void setTimeNormalized(float n) {
        if (isDragging_) return;
        timeN_ = std::clamp(n, 0.0f, 1.0f);
        setDirty();
    }

    void setCurveNormalized(float n) {
        if (isDragging_) return;
        curveN_ = std::clamp(n, 0.0f, 1.0f);
        setDirty();
    }

    [[nodiscard]] float getStartNormalized() const { return startN_; }
    [[nodiscard]] float getEndNormalized() const { return endN_; }
    [[nodiscard]] float getTimeNormalized() const { return timeN_; }
    [[nodiscard]] float getCurveNormalized() const { return curveN_; }

    // =========================================================================
    // Parameter ID Configuration
    // =========================================================================

    void setStartParamId(uint32_t paramId) { startParamId_ = paramId; }
    void setEndParamId(uint32_t paramId) { endParamId_ = paramId; }
    void setTimeParamId(uint32_t paramId) { timeParamId_ = paramId; }
    void setCurveParamId(uint32_t paramId) { curveParamId_ = paramId; }

    [[nodiscard]] uint32_t getStartParamId() const { return startParamId_; }
    [[nodiscard]] uint32_t getEndParamId() const { return endParamId_; }
    [[nodiscard]] uint32_t getTimeParamId() const { return timeParamId_; }
    [[nodiscard]] uint32_t getCurveParamId() const { return curveParamId_; }

    // =========================================================================
    // Callback Configuration
    // =========================================================================

    void setParameterCallback(ParameterCallback cb) { paramCallback_ = std::move(cb); }
    void setBeginEditCallback(EditCallback cb) { beginEditCallback_ = std::move(cb); }
    void setEndEditCallback(EditCallback cb) { endEditCallback_ = std::move(cb); }

    // =========================================================================
    // Color Configuration (ViewCreator attributes)
    // =========================================================================

    void setBackgroundColor(const VSTGUI::CColor& color) { backgroundColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getBackgroundColor() const { return backgroundColor_; }

    void setGridColor(const VSTGUI::CColor& color) { gridColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getGridColor() const { return gridColor_; }

    void setStrokeColor(const VSTGUI::CColor& color) { strokeColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getStrokeColor() const { return strokeColor_; }

    void setFillColor(const VSTGUI::CColor& color) { fillColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getFillColor() const { return fillColor_; }

    void setHandleColor(const VSTGUI::CColor& color) { handleColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getHandleColor() const { return handleColor_; }

    void setHandleActiveColor(const VSTGUI::CColor& color) { handleActiveColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getHandleActiveColor() const { return handleActiveColor_; }

    void setTextColor(const VSTGUI::CColor& color) { textColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getTextColor() const { return textColor_; }

    // =========================================================================
    // Geometry (public for testability)
    // =========================================================================

    /// Convert normalised time [0,1] to local pixel X.
    [[nodiscard]] float timeToPixelX(float n) const {
        VSTGUI::CRect vs = getViewSize();
        float left = static_cast<float>(vs.left) + kPadding;
        float right = static_cast<float>(vs.right) - kPadding;
        return left + std::clamp(n, 0.0f, 1.0f) * (right - left);
    }

    /// Convert normalised pitch [0,1] (1 = high) to local pixel Y.
    [[nodiscard]] float pitchToPixelY(float n) const {
        VSTGUI::CRect vs = getViewSize();
        float top = static_cast<float>(vs.top) + kPadding;
        float bottom = static_cast<float>(vs.bottom) - kPadding;
        // Reserve label strip at the bottom if tall enough.
        if (vs.getHeight() >= kMinViewHeightForLabels) {
            bottom -= kLabelStripHeight;
        }
        return bottom - std::clamp(n, 0.0f, 1.0f) * (bottom - top);
    }

    [[nodiscard]] float pixelXToTime(float pixelX) const {
        VSTGUI::CRect vs = getViewSize();
        float left = static_cast<float>(vs.left) + kPadding;
        float right = static_cast<float>(vs.right) - kPadding;
        float span = std::max(1.0f, right - left);
        return std::clamp((pixelX - left) / span, 0.0f, 1.0f);
    }

    [[nodiscard]] float pixelYToPitch(float pixelY) const {
        VSTGUI::CRect vs = getViewSize();
        float top = static_cast<float>(vs.top) + kPadding;
        float bottom = static_cast<float>(vs.bottom) - kPadding;
        if (vs.getHeight() >= kMinViewHeightForLabels) {
            bottom -= kLabelStripHeight;
        }
        float span = std::max(1.0f, bottom - top);
        return std::clamp((bottom - pixelY) / span, 0.0f, 1.0f);
    }

    /// Y coordinate on the drawn curve at given normalised time.
    [[nodiscard]] float curveYAtTime(float timeN) const {
        std::array<float, Krate::DSP::kCurveTableSize> table{};
        float curveAmount = 2.0f * curveN_ - 1.0f;
        Krate::DSP::generatePowerCurveTable(table, curveAmount);
        float shaped = Krate::DSP::lookupCurveTable(table, std::clamp(timeN, 0.0f, 1.0f));
        float startY = pitchToPixelY(startN_);
        float endY = pitchToPixelY(endN_);
        return startY + shaped * (endY - startY);
    }

    [[nodiscard]] VSTGUI::CPoint getHandleCenter(DragTarget target) const {
        VSTGUI::CRect vs = getViewSize();
        float leftX = static_cast<float>(vs.left) + kPadding;
        float rightX = static_cast<float>(vs.right) - kPadding;
        switch (target) {
            case DragTarget::Start:
                return VSTGUI::CPoint(leftX, pitchToPixelY(startN_));
            case DragTarget::End:
                return VSTGUI::CPoint(rightX, pitchToPixelY(endN_));
            case DragTarget::Time:
                return VSTGUI::CPoint(timeToPixelX(timeN_), curveYAtTime(timeN_));
            default:
                return VSTGUI::CPoint(0, 0);
        }
    }

    [[nodiscard]] DragTarget hitTest(const VSTGUI::CPoint& point) const {
        // Start and End handles take priority over Time (which can coincide
        // with Start when timeN==0 or with End when timeN==1).
        const float r2 = kHitRadius * kHitRadius;
        auto hits = [&](DragTarget t) {
            auto c = getHandleCenter(t);
            float dx = static_cast<float>(point.x - c.x);
            float dy = static_cast<float>(point.y - c.y);
            return (dx * dx + dy * dy) <= r2;
        };

        if (hits(DragTarget::Start)) return DragTarget::Start;
        if (hits(DragTarget::End))   return DragTarget::End;
        if (hits(DragTarget::Time))  return DragTarget::Time;

        // Curve drag: inside the envelope rect (excluding handles which were
        // tested above), we interpret a press as a curve-amount adjustment.
        VSTGUI::CRect vs = getViewSize();
        float leftX = static_cast<float>(vs.left) + kPadding;
        float rightX = static_cast<float>(vs.right) - kPadding;
        if (point.x >= leftX && point.x <= rightX) {
            float yCurve = curveYAtTime(pixelXToTime(static_cast<float>(point.x)));
            float dy = static_cast<float>(point.y) - yCurve;
            if (std::abs(dy) <= kHitRadius * 2.0f) {
                return DragTarget::Curve;
            }
        }

        return DragTarget::None;
    }

    // =========================================================================
    // CControl Overrides
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        drawBackground(context);
        drawGrid(context);
        drawEnvelopeCurve(context);
        drawHandles(context);
        drawLabels(context);

        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {

        if ((buttons & VSTGUI::kLButton) == 0)
            return VSTGUI::kMouseEventNotHandled;

        DragTarget target = hitTest(where);
        if (target == DragTarget::None)
            return VSTGUI::kMouseEventNotHandled;

        isDragging_ = true;
        dragTarget_ = target;
        lastDragPoint_ = where;
        dragAnchorCurveN_ = curveN_;

        notifyBeginEdit(target);
        setDirty();
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& /*buttons*/) override {

        if (!isDragging_)
            return VSTGUI::kMouseEventNotHandled;

        handleDrag(dragTarget_, where);
        lastDragPoint_ = where;
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseUp(
        VSTGUI::CPoint& /*where*/,
        const VSTGUI::CButtonState& /*buttons*/) override {
        if (!isDragging_)
            return VSTGUI::kMouseEventNotHandled;

        notifyEndEdit(dragTarget_);
        isDragging_ = false;
        dragTarget_ = DragTarget::None;
        setDirty();
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseCancel() override {
        if (isDragging_) {
            notifyEndEdit(dragTarget_);
            isDragging_ = false;
            dragTarget_ = DragTarget::None;
            setDirty();
        }
        return VSTGUI::kMouseEventHandled;
    }

    bool removed(VSTGUI::CView* parent) override {
        // Terminate any in-flight drag cleanly so beginEdit/endEdit pairs
        // stay balanced when the editor closes mid-drag.
        if (isDragging_) {
            notifyEndEdit(dragTarget_);
            isDragging_ = false;
            dragTarget_ = DragTarget::None;
        }
        return CControl::removed(parent);
    }

    CLASS_METHODS(PitchEnvelopeDisplay, CControl)

private:
    // =========================================================================
    // Drag Handling
    // =========================================================================

    void handleDrag(DragTarget target, const VSTGUI::CPoint& where) {
        switch (target) {
            case DragTarget::Start: {
                startN_ = pixelYToPitch(static_cast<float>(where.y));
                fireParam(startParamId_, startN_);
                setDirty();
                break;
            }
            case DragTarget::End: {
                endN_ = pixelYToPitch(static_cast<float>(where.y));
                fireParam(endParamId_, endN_);
                setDirty();
                break;
            }
            case DragTarget::Time: {
                timeN_ = pixelXToTime(static_cast<float>(where.x));
                fireParam(timeParamId_, timeN_);
                setDirty();
                break;
            }
            case DragTarget::Curve: {
                // Vertical delta from the press anchor adjusts curve amount.
                float deltaY = static_cast<float>(where.y - lastDragPoint_.y);
                // Down (positive deltaY) = more exponential (curveN -> 1).
                // Up   (negative deltaY) = more logarithmic  (curveN -> 0).
                float curveDelta = deltaY * kCurveDragSensitivity;
                curveN_ = std::clamp(curveN_ + curveDelta, 0.0f, 1.0f);
                fireParam(curveParamId_, curveN_);
                setDirty();
                break;
            }
            default:
                break;
        }
    }

    void fireParam(uint32_t paramId, float value) {
        if (paramCallback_ && paramId != 0) {
            paramCallback_(paramId, std::clamp(value, 0.0f, 1.0f));
        }
    }

    void notifyBeginEdit(DragTarget target) {
        if (!beginEditCallback_) return;
        uint32_t id = paramIdForTarget(target);
        if (id != 0) beginEditCallback_(id);
    }

    void notifyEndEdit(DragTarget target) {
        if (!endEditCallback_) return;
        uint32_t id = paramIdForTarget(target);
        if (id != 0) endEditCallback_(id);
    }

    [[nodiscard]] uint32_t paramIdForTarget(DragTarget target) const {
        switch (target) {
            case DragTarget::Start: return startParamId_;
            case DragTarget::End:   return endParamId_;
            case DragTarget::Time:  return timeParamId_;
            case DragTarget::Curve: return curveParamId_;
            default: return 0;
        }
    }

    // =========================================================================
    // Drawing Helpers
    // =========================================================================

    void drawBackground(VSTGUI::CDrawContext* context) const {
        VSTGUI::CRect vs = getViewSize();
        context->setFillColor(backgroundColor_);
        context->drawRect(vs, VSTGUI::kDrawFilled);

        context->setFrameColor(gridColor_);
        context->setLineWidth(1.0);
        context->setLineStyle(VSTGUI::kLineSolid);
        context->drawRect(vs, VSTGUI::kDrawStroked);
    }

    void drawGrid(VSTGUI::CDrawContext* context) const {
        VSTGUI::CRect vs = getViewSize();
        if (vs.getHeight() < kMinViewHeightForGrid) return;

        context->setFrameColor(gridColor_);
        context->setLineWidth(1.0);
        context->setLineStyle(VSTGUI::kLineSolid);

        float leftX = static_cast<float>(vs.left) + kPadding;
        float rightX = static_cast<float>(vs.right) - kPadding;

        // Horizontal lines at 25/50/75% pitch.
        for (float level : {0.25f, 0.5f, 0.75f}) {
            float y = pitchToPixelY(level);
            context->drawLine(
                VSTGUI::CPoint(leftX, y),
                VSTGUI::CPoint(rightX, y));
        }

        float topY = pitchToPixelY(1.0f);
        float botY = pitchToPixelY(0.0f);

        // Vertical lines at 25/50/75% time.
        for (float t : {0.25f, 0.5f, 0.75f}) {
            float x = timeToPixelX(t);
            context->drawLine(
                VSTGUI::CPoint(x, topY),
                VSTGUI::CPoint(x, botY));
        }
    }

    void drawEnvelopeCurve(VSTGUI::CDrawContext* context) const {
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path) return;

        VSTGUI::CRect vs = getViewSize();
        float leftX = static_cast<float>(vs.left) + kPadding;
        float rightX = static_cast<float>(vs.right) - kPadding;
        float bottomY = pitchToPixelY(0.0f);

        std::array<float, Krate::DSP::kCurveTableSize> table{};
        float curveAmount = 2.0f * curveN_ - 1.0f;
        Krate::DSP::generatePowerCurveTable(table, curveAmount);

        float startY = pitchToPixelY(startN_);
        float endY = pitchToPixelY(endN_);

        // Build the curve path from Start -> End.
        path->beginSubpath(VSTGUI::CPoint(leftX, startY));
        for (int i = 1; i <= kCurveResolution; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(kCurveResolution);
            float shaped = Krate::DSP::lookupCurveTable(table, phase);
            float x = leftX + phase * (rightX - leftX);
            float y = startY + shaped * (endY - startY);
            path->addLine(VSTGUI::CPoint(x, y));
        }

        // Close path to baseline for a translucent fill underneath the curve.
        path->addLine(VSTGUI::CPoint(rightX, bottomY));
        path->addLine(VSTGUI::CPoint(leftX, bottomY));
        path->closeSubpath();

        context->setFillColor(fillColor_);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);

        context->setFrameColor(strokeColor_);
        context->setLineWidth(1.5);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
    }

    void drawHandles(VSTGUI::CDrawContext* context) const {
        auto drawOne = [&](DragTarget t) {
            bool active = isDragging_ && dragTarget_ == t;
            auto center = getHandleCenter(t);
            VSTGUI::CRect r(
                center.x - kControlPointDrawRadius,
                center.y - kControlPointDrawRadius,
                center.x + kControlPointDrawRadius,
                center.y + kControlPointDrawRadius);
            context->setFillColor(active ? handleActiveColor_ : handleColor_);
            context->drawEllipse(r, VSTGUI::kDrawFilled);
            context->setFrameColor(gridColor_);
            context->setLineWidth(1.0);
            context->drawEllipse(r, VSTGUI::kDrawStroked);
        };
        drawOne(DragTarget::Start);
        drawOne(DragTarget::End);
        drawOne(DragTarget::Time);
    }

    void drawLabels(VSTGUI::CDrawContext* context) const {
        VSTGUI::CRect vs = getViewSize();
        if (vs.getHeight() < kMinViewHeightForLabels) return;

        context->setFont(VSTGUI::kNormalFontSmall);
        context->setFontColor(textColor_);

        float leftX = static_cast<float>(vs.left) + kPadding;
        float rightX = static_cast<float>(vs.right) - kPadding;
        float labelTop = static_cast<float>(vs.bottom) - kPadding - kLabelStripHeight;
        float labelBot = static_cast<float>(vs.bottom) - kPadding;

        char buf[32];

        // Start pitch (left)
        std::snprintf(buf, sizeof(buf), "S:%.0f%%", startN_ * 100.0f);
        VSTGUI::CRect leftR(leftX, labelTop, leftX + 60.0f, labelBot);
        context->drawString(VSTGUI::UTF8String(buf), leftR, VSTGUI::kLeftText);

        // Time (centre)
        std::snprintf(buf, sizeof(buf), "T:%.0f%%", timeN_ * 100.0f);
        VSTGUI::CRect midR((leftX + rightX) * 0.5f - 30.0f, labelTop,
                           (leftX + rightX) * 0.5f + 30.0f, labelBot);
        context->drawString(VSTGUI::UTF8String(buf), midR, VSTGUI::kCenterText);

        // End pitch (right)
        std::snprintf(buf, sizeof(buf), "E:%.0f%%", endN_ * 100.0f);
        VSTGUI::CRect rightR(rightX - 60.0f, labelTop, rightX, labelBot);
        context->drawString(VSTGUI::UTF8String(buf), rightR, VSTGUI::kRightText);
    }

    // =========================================================================
    // State
    // =========================================================================

    // Normalised parameter values (all [0, 1]).
    float startN_ = 0.5f;
    float endN_   = 0.25f;
    float timeN_  = 0.5f;
    float curveN_ = 0.5f;  // 0.5 -> curve amount 0 (linear)

    // Parameter IDs (0 means unbound).
    uint32_t startParamId_ = 0;
    uint32_t endParamId_   = 0;
    uint32_t timeParamId_  = 0;
    uint32_t curveParamId_ = 0;

    // Drag state.
    bool isDragging_ = false;
    DragTarget dragTarget_ = DragTarget::None;
    VSTGUI::CPoint lastDragPoint_{0, 0};
    float dragAnchorCurveN_ = 0.5f;

    // Colors (defaults match ADSRDisplay palette).
    VSTGUI::CColor backgroundColor_{30, 30, 33, 255};
    VSTGUI::CColor gridColor_{255, 255, 255, 25};
    VSTGUI::CColor strokeColor_{180, 140, 90, 255};
    VSTGUI::CColor fillColor_{180, 140, 90, 60};
    VSTGUI::CColor handleColor_{255, 200, 110, 255};
    VSTGUI::CColor handleActiveColor_{255, 230, 150, 255};
    VSTGUI::CColor textColor_{255, 255, 255, 180};

    // Callbacks.
    ParameterCallback paramCallback_;
    EditCallback beginEditCallback_;
    EditCallback endEditCallback_;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================
// Registers "PitchEnvelopeDisplay" with the VSTGUI UIViewFactory.
// getBaseViewName() -> "CControl" ensures standard CControl attributes
// (default-value, min-value, max-value, etc.) are applied.

struct PitchEnvelopeDisplayCreator : VSTGUI::ViewCreatorAdapter {
    PitchEnvelopeDisplayCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override { return "PitchEnvelopeDisplay"; }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Pitch Envelope Display";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new PitchEnvelopeDisplay(
            VSTGUI::CRect(0, 0, 240, 80), nullptr, -1);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override {
        auto* display = dynamic_cast<PitchEnvelopeDisplay*>(view);
        if (!display)
            return false;

        auto applyTag = [&](const char* attrName,
                            void (PitchEnvelopeDisplay::*setter)(uint32_t)) {
            const auto* attr = attributes.getAttributeValue(attrName);
            if (!attr || attr->empty()) return;

            // First try to resolve via the UI description's control-tag table.
            int32_t tag = description->getTagForName(attr->c_str());
            if (tag != -1) {
                (display->*setter)(static_cast<uint32_t>(tag));
                return;
            }
            // Fall back to a numeric literal.
            char* endPtr = nullptr;
            long numTag = std::strtol(attr->c_str(), &endPtr, 10);
            if (endPtr != attr->c_str()) {
                (display->*setter)(static_cast<uint32_t>(numTag));
            }
        };

        applyTag("start-tag", &PitchEnvelopeDisplay::setStartParamId);
        applyTag("end-tag",   &PitchEnvelopeDisplay::setEndParamId);
        applyTag("time-tag",  &PitchEnvelopeDisplay::setTimeParamId);
        applyTag("curve-tag", &PitchEnvelopeDisplay::setCurveParamId);

        VSTGUI::CColor color;
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("background-color"), color, description))
            display->setBackgroundColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("grid-color"), color, description))
            display->setGridColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("stroke-color"), color, description))
            display->setStrokeColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("fill-color"), color, description))
            display->setFillColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("handle-color"), color, description))
            display->setHandleColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("handle-active-color"), color, description))
            display->setHandleActiveColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("text-color"), color, description))
            display->setTextColor(color);

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("start-tag");
        attributeNames.emplace_back("end-tag");
        attributeNames.emplace_back("time-tag");
        attributeNames.emplace_back("curve-tag");
        attributeNames.emplace_back("background-color");
        attributeNames.emplace_back("grid-color");
        attributeNames.emplace_back("stroke-color");
        attributeNames.emplace_back("fill-color");
        attributeNames.emplace_back("handle-color");
        attributeNames.emplace_back("handle-active-color");
        attributeNames.emplace_back("text-color");
        return true;
    }

    AttrType getAttributeType(const std::string& attributeName) const override {
        if (attributeName == "start-tag") return kTagType;
        if (attributeName == "end-tag")   return kTagType;
        if (attributeName == "time-tag")  return kTagType;
        if (attributeName == "curve-tag") return kTagType;
        if (attributeName == "background-color") return kColorType;
        if (attributeName == "grid-color") return kColorType;
        if (attributeName == "stroke-color") return kColorType;
        if (attributeName == "fill-color") return kColorType;
        if (attributeName == "handle-color") return kColorType;
        if (attributeName == "handle-active-color") return kColorType;
        if (attributeName == "text-color") return kColorType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override {
        auto* display = dynamic_cast<PitchEnvelopeDisplay*>(view);
        if (!display)
            return false;

        auto tagToString = [&](uint32_t tag) {
            // Look up the name registered for this tag in the description; if
            // none, fall back to the numeric literal.
            VSTGUI::UTF8StringPtr name =
                desc->lookupControlTagName(static_cast<int32_t>(tag));
            if (name != nullptr) {
                stringValue = name;
            } else {
                stringValue = std::to_string(tag);
            }
        };

        if (attributeName == "start-tag") { tagToString(display->getStartParamId()); return true; }
        if (attributeName == "end-tag")   { tagToString(display->getEndParamId());   return true; }
        if (attributeName == "time-tag")  { tagToString(display->getTimeParamId());  return true; }
        if (attributeName == "curve-tag") { tagToString(display->getCurveParamId()); return true; }

        if (attributeName == "background-color") {
            VSTGUI::UIViewCreator::colorToString(
                display->getBackgroundColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "grid-color") {
            VSTGUI::UIViewCreator::colorToString(
                display->getGridColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "stroke-color") {
            VSTGUI::UIViewCreator::colorToString(
                display->getStrokeColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "fill-color") {
            VSTGUI::UIViewCreator::colorToString(
                display->getFillColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "handle-color") {
            VSTGUI::UIViewCreator::colorToString(
                display->getHandleColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "handle-active-color") {
            VSTGUI::UIViewCreator::colorToString(
                display->getHandleActiveColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "text-color") {
            VSTGUI::UIViewCreator::colorToString(
                display->getTextColor(), stringValue, desc);
            return true;
        }
        return false;
    }
};

// Inline variable (C++17) - safe for inclusion from multiple translation units.
// Include this header from each plugin's entry.cpp to register the view type.
inline PitchEnvelopeDisplayCreator gPitchEnvelopeDisplayCreator;

} // namespace Krate::Plugins
