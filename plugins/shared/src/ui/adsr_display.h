#pragma once

// ==============================================================================
// ADSRDisplay - Interactive ADSR Envelope Editor/Display
// ==============================================================================
// A shared VSTGUI CControl subclass for visualizing and editing ADSR envelope
// parameters. Renders envelope curve with filled gradient, grid lines, time
// labels, control points, and optional playback dot.
//
// Features:
// - Drag control points (Peak, Sustain, End) to adjust time/level parameters
// - Drag curve segments to adjust curve amount [-1, +1]
// - Shift+drag for 0.1x fine adjustment
// - Double-click to reset control points/curves to defaults
// - Escape to cancel drag and restore pre-drag values
// - Logarithmic time axis with 15% minimum segment width
// - Bezier mode with draggable control point handles
// - Real-time playback dot visualization
//
// This component is plugin-agnostic: it communicates via ParameterCallback
// and configurable parameter IDs. No dependency on any specific plugin.
//
// Registered as "ADSRDisplay" via VSTGUI ViewCreator system.
// ==============================================================================

#include "color_utils.h"

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/vstkeycode.h"
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
#include <functional>

namespace Krate::Plugins {

// ==============================================================================
// ADSRDisplay Control
// ==============================================================================

class ADSRDisplay : public VSTGUI::CControl {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kControlPointRadius = 12.0f;
    static constexpr float kControlPointDrawRadius = 4.0f;
    static constexpr float kMinSegmentWidthFraction = 0.15f;
    static constexpr float kSustainHoldFraction = 0.25f;
    static constexpr float kFineAdjustmentScale = 0.1f;
    static constexpr float kPadding = 4.0f;
    static constexpr float kMinTimeMs = 0.1f;
    static constexpr float kMaxTimeMs = 10000.0f;
    static constexpr float kCurveDragSensitivity = 0.005f;

    // Mode toggle button
    static constexpr float kModeToggleSize = 16.0f;

    // Bezier handle sizes
    static constexpr float kBezierHandleDrawSize = 3.0f;  // half-size for diamond
    static constexpr float kBezierHandleHitRadius = 8.0f;

    // Default ADSR values
    static constexpr float kDefaultAttackMs = 10.0f;
    static constexpr float kDefaultDecayMs = 50.0f;
    static constexpr float kDefaultSustainLevel = 0.5f;
    static constexpr float kDefaultReleaseMs = 100.0f;

    // =========================================================================
    // Types
    // =========================================================================

    enum class DragTarget {
        None,
        PeakPoint,       // Horizontal only (attack time)
        SustainPoint,    // Both axes (decay time + sustain level)
        EndPoint,        // Horizontal only (release time)
        AttackCurve,     // Curve amount [-1, +1]
        DecayCurve,      // Curve amount [-1, +1]
        ReleaseCurve,    // Curve amount [-1, +1]
        BezierHandle,    // Specific Bezier cp (identified by segment + handle index)
        ModeToggle       // [S]/[B] toggle button in top-right corner
    };

    struct SegmentLayout {
        float attackStartX = 0.0f;
        float attackEndX = 0.0f;
        float decayEndX = 0.0f;
        float sustainEndX = 0.0f;
        float releaseEndX = 0.0f;
        float topY = 0.0f;
        float bottomY = 0.0f;
    };

    struct PreDragValues {
        float attackMs = 0.0f;
        float decayMs = 0.0f;
        float sustainLevel = 0.0f;
        float releaseMs = 0.0f;
        float attackCurve = 0.0f;
        float decayCurve = 0.0f;
        float releaseCurve = 0.0f;
    };

    struct BezierHandles {
        float cp1x = 0.33f;
        float cp1y = 0.33f;
        float cp2x = 0.67f;
        float cp2y = 0.67f;
    };

    // =========================================================================
    // Callback Types
    // =========================================================================

    using ParameterCallback = std::function<void(uint32_t paramId, float normalizedValue)>;
    using EditCallback = std::function<void(uint32_t paramId)>;

    // =========================================================================
    // Construction
    // =========================================================================

    ADSRDisplay(const VSTGUI::CRect& size,
                VSTGUI::IControlListener* listener,
                int32_t tag)
        : CControl(size, listener, tag) {
        recalculateLayout();
    }

    ADSRDisplay(const ADSRDisplay& other)
        : CControl(other)
        , attackMs_(other.attackMs_)
        , decayMs_(other.decayMs_)
        , sustainLevel_(other.sustainLevel_)
        , releaseMs_(other.releaseMs_)
        , attackCurve_(other.attackCurve_)
        , decayCurve_(other.decayCurve_)
        , releaseCurve_(other.releaseCurve_)
        , bezierEnabled_(other.bezierEnabled_)
        , bezierHandles_(other.bezierHandles_)
        , layout_(other.layout_)
        , fillColor_(other.fillColor_)
        , strokeColor_(other.strokeColor_)
        , backgroundColor_(other.backgroundColor_)
        , gridColor_(other.gridColor_)
        , controlPointColor_(other.controlPointColor_)
        , textColor_(other.textColor_)
        , attackParamId_(other.attackParamId_)
        , decayParamId_(other.decayParamId_)
        , sustainParamId_(other.sustainParamId_)
        , releaseParamId_(other.releaseParamId_)
        , attackCurveParamId_(other.attackCurveParamId_)
        , decayCurveParamId_(other.decayCurveParamId_)
        , releaseCurveParamId_(other.releaseCurveParamId_)
        , bezierEnabledParamId_(other.bezierEnabledParamId_)
        , bezierBaseParamId_(other.bezierBaseParamId_) {}

    // =========================================================================
    // Parameter Value Setters (called by controller for sync)
    // =========================================================================

    void setAttackMs(float ms) {
        attackMs_ = std::clamp(ms, kMinTimeMs, kMaxTimeMs);
        recalculateLayout();
        setDirty();
    }

    void setDecayMs(float ms) {
        decayMs_ = std::clamp(ms, kMinTimeMs, kMaxTimeMs);
        recalculateLayout();
        setDirty();
    }

    void setSustainLevel(float level) {
        sustainLevel_ = std::clamp(level, 0.0f, 1.0f);
        recalculateLayout();
        setDirty();
    }

    void setReleaseMs(float ms) {
        releaseMs_ = std::clamp(ms, kMinTimeMs, kMaxTimeMs);
        recalculateLayout();
        setDirty();
    }

    void setAttackCurve(float curve) {
        attackCurve_ = std::clamp(curve, -1.0f, 1.0f);
        setDirty();
    }

    void setDecayCurve(float curve) {
        decayCurve_ = std::clamp(curve, -1.0f, 1.0f);
        setDirty();
    }

    void setReleaseCurve(float curve) {
        releaseCurve_ = std::clamp(curve, -1.0f, 1.0f);
        setDirty();
    }

    void setBezierEnabled(bool enabled) {
        bezierEnabled_ = enabled;
        setDirty();
    }

    void setBezierHandleValue(int segment, int handle, int axis, float value) {
        if (segment < 0 || segment > 2) return;
        if (handle < 0 || handle > 1) return;
        if (axis < 0 || axis > 1) return;

        value = std::clamp(value, 0.0f, 1.0f);
        auto& bh = bezierHandles_[static_cast<size_t>(segment)];

        if (handle == 0) {
            if (axis == 0) bh.cp1x = value;
            else bh.cp1y = value;
        } else {
            if (axis == 0) bh.cp2x = value;
            else bh.cp2y = value;
        }
        setDirty();
    }

    // =========================================================================
    // Parameter ID Configuration
    // =========================================================================

    void setAdsrBaseParamId(uint32_t baseId) {
        attackParamId_ = baseId;
        decayParamId_ = baseId + 1;
        sustainParamId_ = baseId + 2;
        releaseParamId_ = baseId + 3;
    }

    void setCurveBaseParamId(uint32_t baseId) {
        attackCurveParamId_ = baseId;
        decayCurveParamId_ = baseId + 1;
        releaseCurveParamId_ = baseId + 2;
    }

    void setBezierEnabledParamId(uint32_t paramId) {
        bezierEnabledParamId_ = paramId;
    }

    void setBezierBaseParamId(uint32_t baseId) {
        bezierBaseParamId_ = baseId;
    }

    // =========================================================================
    // Callback Configuration
    // =========================================================================

    void setParameterCallback(ParameterCallback cb) { paramCallback_ = std::move(cb); }
    void setBeginEditCallback(EditCallback cb) { beginEditCallback_ = std::move(cb); }
    void setEndEditCallback(EditCallback cb) { endEditCallback_ = std::move(cb); }

    // =========================================================================
    // Playback Visualization
    // =========================================================================

    void setPlaybackState(float outputLevel, int stage, bool voiceActive) {
        playbackOutput_ = outputLevel;
        playbackStage_ = stage;
        voiceActive_ = voiceActive;
        setDirty();
    }

    /// Set pointers to atomic playback state from processor (for timer-based polling)
    void setPlaybackStatePointers(std::atomic<float>* outputPtr,
                                   std::atomic<int>* stagePtr,
                                   std::atomic<bool>* activePtr) {
        playbackOutputPtr_ = outputPtr;
        playbackStagePtr_ = stagePtr;
        playbackActivePtr_ = activePtr;

        // Start playback poll timer if we have valid pointers
        if (outputPtr && stagePtr && activePtr && !playbackTimer_) {
            playbackTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
                [this](VSTGUI::CVSTGUITimer*) {
                    pollPlaybackState();
                }, 33); // ~30fps
        }
    }

    /// Check if the playback dot should be visible
    [[nodiscard]] bool isPlaybackDotVisible() const {
        return voiceActive_;
    }

    /// Calculate the pixel position of the playback dot based on current stage and output
    [[nodiscard]] VSTGUI::CPoint getPlaybackDotPosition() const {
        float output = playbackOutput_;
        int stage = playbackStage_;

        float dotX = 0.0f;
        float dotY = levelToPixelY(output);

        switch (stage) {
            case 1: { // Attack: output goes from 0 to 1
                // Interpolate X across the attack segment based on output level
                float progress = std::clamp(output, 0.0f, 1.0f);
                dotX = layout_.attackStartX + progress * (layout_.attackEndX - layout_.attackStartX);
                break;
            }
            case 2: { // Decay: output goes from 1.0 down to sustainLevel
                float range = 1.0f - sustainLevel_;
                float progress = (range > 0.001f)
                    ? std::clamp((1.0f - output) / range, 0.0f, 1.0f)
                    : 0.5f;
                dotX = layout_.attackEndX + progress * (layout_.decayEndX - layout_.attackEndX);
                break;
            }
            case 3: { // Sustain: hold at sustain level in the middle of sustain segment
                dotX = (layout_.decayEndX + layout_.sustainEndX) * 0.5f;
                break;
            }
            case 4: { // Release: output goes from sustainLevel down to 0
                float progress = (sustainLevel_ > 0.001f)
                    ? std::clamp(1.0f - output / sustainLevel_, 0.0f, 1.0f)
                    : 0.5f;
                dotX = layout_.sustainEndX + progress * (layout_.releaseEndX - layout_.sustainEndX);
                break;
            }
            default: // Idle or unknown
                dotX = layout_.attackStartX;
                dotY = layout_.bottomY;
                break;
        }

        return VSTGUI::CPoint(dotX, dotY);
    }

    // =========================================================================
    // Getters (for tests and readback)
    // =========================================================================

    [[nodiscard]] float getAttackMs() const { return attackMs_; }
    [[nodiscard]] float getDecayMs() const { return decayMs_; }
    [[nodiscard]] float getSustainLevel() const { return sustainLevel_; }
    [[nodiscard]] float getReleaseMs() const { return releaseMs_; }
    [[nodiscard]] float getAttackCurve() const { return attackCurve_; }
    [[nodiscard]] float getDecayCurve() const { return decayCurve_; }
    [[nodiscard]] float getReleaseCurve() const { return releaseCurve_; }
    [[nodiscard]] bool getBezierEnabled() const { return bezierEnabled_; }

    // =========================================================================
    // Layout and Coordinate Conversion (public for testability)
    // =========================================================================

    [[nodiscard]] SegmentLayout getLayout() const { return layout_; }

    [[nodiscard]] float levelToPixelY(float level) const {
        return layout_.bottomY - level * (layout_.bottomY - layout_.topY);
    }

    [[nodiscard]] float pixelYToLevel(float pixelY) const {
        float range = layout_.bottomY - layout_.topY;
        if (range <= 0.0f) return 0.0f;
        return std::clamp((layout_.bottomY - pixelY) / range, 0.0f, 1.0f);
    }

    /// Get the pixel position of a control point
    [[nodiscard]] VSTGUI::CPoint getControlPointPosition(DragTarget target) const {
        switch (target) {
            case DragTarget::PeakPoint:
                return VSTGUI::CPoint(layout_.attackEndX, layout_.topY);
            case DragTarget::SustainPoint:
                return VSTGUI::CPoint(layout_.decayEndX,
                                       levelToPixelY(sustainLevel_));
            case DragTarget::EndPoint:
                return VSTGUI::CPoint(layout_.releaseEndX, layout_.bottomY);
            default:
                return VSTGUI::CPoint(0, 0);
        }
    }

    /// Hit test: determine which element is at the given point
    [[nodiscard]] DragTarget hitTest(const VSTGUI::CPoint& point) const {
        // Mode toggle button in top-right corner (highest priority)
        if (hitTestModeToggle(point)) {
            return DragTarget::ModeToggle;
        }

        // Control points take priority over curve segments
        auto peakPos = getControlPointPosition(DragTarget::PeakPoint);
        if (distanceSquared(point, peakPos) <= kControlPointRadius * kControlPointRadius) {
            return DragTarget::PeakPoint;
        }

        auto sustainPos = getControlPointPosition(DragTarget::SustainPoint);
        if (distanceSquared(point, sustainPos) <= kControlPointRadius * kControlPointRadius) {
            return DragTarget::SustainPoint;
        }

        auto endPos = getControlPointPosition(DragTarget::EndPoint);
        if (distanceSquared(point, endPos) <= kControlPointRadius * kControlPointRadius) {
            return DragTarget::EndPoint;
        }

        // Bezier handle hit testing (when in Bezier mode)
        if (bezierEnabled_) {
            auto bezierTarget = hitTestBezierHandles(point);
            if (bezierTarget != DragTarget::None) {
                return bezierTarget;
            }
        }

        // Curve segment hit testing (middle third of each segment)
        auto curveTarget = hitTestCurveSegment(point);
        if (curveTarget != DragTarget::None) {
            return curveTarget;
        }

        return DragTarget::None;
    }

    // =========================================================================
    // Color Configuration (ViewCreator attributes)
    // =========================================================================

    void setFillColor(const VSTGUI::CColor& color) { fillColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getFillColor() const { return fillColor_; }

    void setStrokeColor(const VSTGUI::CColor& color) { strokeColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getStrokeColor() const { return strokeColor_; }

    void setBackgroundColor(const VSTGUI::CColor& color) { backgroundColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getBackgroundColor() const { return backgroundColor_; }

    void setGridColor(const VSTGUI::CColor& color) { gridColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getGridColor() const { return gridColor_; }

    void setControlPointColor(const VSTGUI::CColor& color) { controlPointColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getControlPointColor() const { return controlPointColor_; }

    void setTextColor(const VSTGUI::CColor& color) { textColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getTextColor() const { return textColor_; }

    // =========================================================================
    // CControl Overrides
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        drawBackground(context);
        drawGrid(context);
        drawEnvelopeCurve(context);
        drawSustainHoldLine(context);
        drawGateMarker(context);
        drawTimeLabels(context);
        drawControlPoints(context);
        if (bezierEnabled_) {
            drawBezierHandles(context);
        }
        drawModeToggle(context);
        drawCurveTooltip(context);
        drawPlaybackDot(context);

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

        // Mode toggle is a click action, not a drag
        if (target == DragTarget::ModeToggle) {
            handleModeToggle();
            return VSTGUI::kMouseEventHandled;
        }

        // Double-click: reset to defaults
        if (buttons.isDoubleClick()) {
            handleDoubleClick(target);
            return VSTGUI::kMouseEventHandled;
        }

        // Start drag gesture
        isDragging_ = true;
        dragTarget_ = target;
        lastDragPoint_ = where;

        // Store pre-drag values for Escape cancel
        preDragValues_.attackMs = attackMs_;
        preDragValues_.decayMs = decayMs_;
        preDragValues_.sustainLevel = sustainLevel_;
        preDragValues_.releaseMs = releaseMs_;
        preDragValues_.attackCurve = attackCurve_;
        preDragValues_.decayCurve = decayCurve_;
        preDragValues_.releaseCurve = releaseCurve_;

        // Begin edit for the relevant parameter(s)
        notifyBeginEdit(target);

        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {

        if (!isDragging_)
            return VSTGUI::kMouseEventNotHandled;

        float deltaX = static_cast<float>(where.x - lastDragPoint_.x);
        float deltaY = static_cast<float>(where.y - lastDragPoint_.y);

        // Fine adjustment with Shift
        if (buttons.getModifierState() & VSTGUI::kShift) {
            deltaX *= kFineAdjustmentScale;
            deltaY *= kFineAdjustmentScale;
        }

        lastDragPoint_ = where;

        handleDrag(dragTarget_, deltaX, deltaY);

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

        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseCancel() override {
        if (isDragging_) {
            cancelDrag();
        }
        return VSTGUI::kMouseEventHandled;
    }

    int32_t onKeyDown(VstKeyCode& keyCode) override {
        if (keyCode.virt == VKEY_ESCAPE && isDragging_) {
            cancelDrag();
            return 1; // handled
        }
        return -1; // not handled
    }

    CLASS_METHODS(ADSRDisplay, CControl)

private:
    // =========================================================================
    // Layout Computation
    // =========================================================================

    void recalculateLayout() {
        VSTGUI::CRect vs = getViewSize();
        float displayLeft = static_cast<float>(vs.left) + kPadding;
        float displayRight = static_cast<float>(vs.right) - kPadding;
        float displayTop = static_cast<float>(vs.top) + kPadding;
        float displayBottom = static_cast<float>(vs.bottom) - kPadding;

        layout_.topY = displayTop;
        layout_.bottomY = displayBottom;

        float totalWidth = displayRight - displayLeft;

        // Sustain hold occupies a fixed 25% of display width
        float sustainWidth = totalWidth * kSustainHoldFraction;
        float timeWidth = totalWidth - sustainWidth;

        // Use logarithmic time scaling for the three time segments
        float logAttack = std::log1p(attackMs_);
        float logDecay = std::log1p(decayMs_);
        float logRelease = std::log1p(releaseMs_);
        float logTotal = logAttack + logDecay + logRelease;

        float attackFrac = 0.0f;
        float decayFrac = 0.0f;
        float releaseFrac = 0.0f;

        if (logTotal > 0.0f) {
            attackFrac = logAttack / logTotal;
            decayFrac = logDecay / logTotal;
            releaseFrac = logRelease / logTotal;
        } else {
            attackFrac = decayFrac = releaseFrac = 1.0f / 3.0f;
        }

        // Enforce 15% minimum segment width relative to total display width.
        // Each time segment must be at least kMinSegmentWidthFraction of totalWidth.
        // Convert the totalWidth-based minimum to a timeWidth-based fraction.
        float minFracOfTimeWidth = (timeWidth > 0.0f)
            ? (kMinSegmentWidthFraction * totalWidth / timeWidth) : 0.0f;
        enforceMinimumFractions(attackFrac, decayFrac, releaseFrac, minFracOfTimeWidth);

        float attackWidth = attackFrac * timeWidth;
        float decayWidth = decayFrac * timeWidth;
        float releaseWidth = releaseFrac * timeWidth;

        layout_.attackStartX = displayLeft;
        layout_.attackEndX = displayLeft + attackWidth;
        layout_.decayEndX = layout_.attackEndX + decayWidth;
        layout_.sustainEndX = layout_.decayEndX + sustainWidth;
        layout_.releaseEndX = layout_.sustainEndX + releaseWidth;
    }

    /// Enforce minimum fraction for each segment, redistributing from larger segments
    static void enforceMinimumFractions(float& a, float& b, float& c, float minFrac) {
        // Iteratively adjust until all segments meet the minimum
        for (int iter = 0; iter < 3; ++iter) {
            float deficit = 0.0f;
            int overCount = 0;

            if (a < minFrac) deficit += minFrac - a;
            else overCount++;

            if (b < minFrac) deficit += minFrac - b;
            else overCount++;

            if (c < minFrac) deficit += minFrac - c;
            else overCount++;

            if (deficit <= 0.0f || overCount <= 0) break;

            float perOver = deficit / static_cast<float>(overCount);

            if (a < minFrac) a = minFrac;
            else a -= perOver;

            if (b < minFrac) b = minFrac;
            else b -= perOver;

            if (c < minFrac) c = minFrac;
            else c -= perOver;
        }

        // Normalize to sum to 1.0
        float sum = a + b + c;
        if (sum > 0.0f) {
            a /= sum;
            b /= sum;
            c /= sum;
        }
    }

    // =========================================================================
    // Distance Helpers
    // =========================================================================

    [[nodiscard]] static float distanceSquared(const VSTGUI::CPoint& a,
                                                const VSTGUI::CPoint& b) {
        float dx = static_cast<float>(a.x - b.x);
        float dy = static_cast<float>(a.y - b.y);
        return dx * dx + dy * dy;
    }

    // =========================================================================
    // Hit Testing
    // =========================================================================

    [[nodiscard]] DragTarget hitTestCurveSegment(const VSTGUI::CPoint& point) const {
        float px = static_cast<float>(point.x);
        float py = static_cast<float>(point.y);

        // Check if point is within the drawing area vertically
        if (py < layout_.topY - kControlPointRadius ||
            py > layout_.bottomY + kControlPointRadius) {
            return DragTarget::None;
        }

        // Attack segment (middle third to avoid overlap with control points)
        float attackMidStart = layout_.attackStartX +
            (layout_.attackEndX - layout_.attackStartX) / 3.0f;
        float attackMidEnd = layout_.attackEndX -
            (layout_.attackEndX - layout_.attackStartX) / 3.0f;
        if (px >= attackMidStart && px <= attackMidEnd) {
            return DragTarget::AttackCurve;
        }

        // Decay segment (middle third)
        float decayMidStart = layout_.attackEndX +
            (layout_.decayEndX - layout_.attackEndX) / 3.0f;
        float decayMidEnd = layout_.decayEndX -
            (layout_.decayEndX - layout_.attackEndX) / 3.0f;
        if (px >= decayMidStart && px <= decayMidEnd) {
            return DragTarget::DecayCurve;
        }

        // Release segment (middle third)
        float releaseMidStart = layout_.sustainEndX +
            (layout_.releaseEndX - layout_.sustainEndX) / 3.0f;
        float releaseMidEnd = layout_.releaseEndX -
            (layout_.releaseEndX - layout_.sustainEndX) / 3.0f;
        if (px >= releaseMidStart && px <= releaseMidEnd) {
            return DragTarget::ReleaseCurve;
        }

        return DragTarget::None;
    }

    /// Hit test the mode toggle button (16x16 in top-right corner)
    [[nodiscard]] bool hitTestModeToggle(const VSTGUI::CPoint& point) const {
        VSTGUI::CRect vs = getViewSize();
        float btnRight = static_cast<float>(vs.right) - kPadding;
        float btnLeft = btnRight - kModeToggleSize;
        float btnTop = static_cast<float>(vs.top) + kPadding;
        float btnBottom = btnTop + kModeToggleSize;

        float px = static_cast<float>(point.x);
        float py = static_cast<float>(point.y);
        return px >= btnLeft && px <= btnRight && py >= btnTop && py <= btnBottom;
    }

    /// Hit test Bezier handles (6 handles: 2 per segment x 3 segments)
    [[nodiscard]] DragTarget hitTestBezierHandles(const VSTGUI::CPoint& point) const {
        // Check each segment's two handles
        for (int seg = 0; seg < 3; ++seg) {
            for (int handle = 0; handle < 2; ++handle) {
                auto handlePos = getBezierHandlePixelPos(seg, handle);
                if (distanceSquared(point, handlePos) <=
                    kBezierHandleHitRadius * kBezierHandleHitRadius) {
                    // Store which specific handle was hit
                    activeBezierSegment_ = seg;
                    activeBezierHandle_ = handle;
                    return DragTarget::BezierHandle;
                }
            }
        }
        return DragTarget::None;
    }

    /// Get pixel position for a Bezier handle
    [[nodiscard]] VSTGUI::CPoint getBezierHandlePixelPos(int seg, int handle) const {
        const auto& bh = bezierHandles_[static_cast<size_t>(seg)];
        float normX = (handle == 0) ? bh.cp1x : bh.cp2x;
        float normY = (handle == 0) ? bh.cp1y : bh.cp2y;

        float segStartX = 0.0f, segEndX = 0.0f;
        float segStartY = 0.0f, segEndY = 0.0f;

        switch (seg) {
            case 0: // Attack: bottom to top
                segStartX = layout_.attackStartX;
                segEndX = layout_.attackEndX;
                segStartY = layout_.bottomY;
                segEndY = layout_.topY;
                break;
            case 1: // Decay: top to sustain
                segStartX = layout_.attackEndX;
                segEndX = layout_.decayEndX;
                segStartY = layout_.topY;
                segEndY = levelToPixelY(sustainLevel_);
                break;
            case 2: // Release: sustain to bottom
                segStartX = layout_.sustainEndX;
                segEndX = layout_.releaseEndX;
                segStartY = levelToPixelY(sustainLevel_);
                segEndY = layout_.bottomY;
                break;
            default:
                return VSTGUI::CPoint(0, 0);
        }

        float pixelX = segStartX + normX * (segEndX - segStartX);
        float pixelY = segStartY + normY * (segEndY - segStartY);
        return VSTGUI::CPoint(pixelX, pixelY);
    }

    // =========================================================================
    // Drag Handling
    // =========================================================================

    void handleDrag(DragTarget target, float deltaX, float deltaY) {
        switch (target) {
            case DragTarget::PeakPoint:
                handlePeakDrag(deltaX);
                break;
            case DragTarget::SustainPoint:
                handleSustainDrag(deltaX, deltaY);
                break;
            case DragTarget::EndPoint:
                handleEndPointDrag(deltaX);
                break;
            case DragTarget::AttackCurve:
                handleCurveDrag(attackCurve_, attackCurveParamId_, deltaY);
                break;
            case DragTarget::DecayCurve:
                handleCurveDrag(decayCurve_, decayCurveParamId_, deltaY);
                break;
            case DragTarget::ReleaseCurve:
                handleCurveDrag(releaseCurve_, releaseCurveParamId_, deltaY);
                break;
            case DragTarget::BezierHandle:
                handleBezierHandleDrag(deltaX, deltaY);
                break;
            default:
                break;
        }
    }

    void handlePeakDrag(float deltaX) {
        // Horizontal drag changes attack time
        float totalTimeWidth = layout_.releaseEndX - layout_.sustainEndX +
                               layout_.decayEndX - layout_.attackEndX +
                               layout_.attackEndX - layout_.attackStartX;
        if (totalTimeWidth <= 0.0f) return;

        float timeFraction = deltaX / totalTimeWidth;
        float logRange = std::log1p(kMaxTimeMs) - std::log1p(kMinTimeMs);
        float currentLogTime = std::log1p(attackMs_);
        float newLogTime = currentLogTime + timeFraction * logRange * 0.3f;
        float newAttackMs = std::clamp(std::expm1(newLogTime), kMinTimeMs, kMaxTimeMs);

        attackMs_ = newAttackMs;
        recalculateLayout();
        setDirty();

        // Notify parameter change (normalized: cubic mapping)
        if (paramCallback_ && attackParamId_ > 0) {
            float normalized = timeMsToNormalized(attackMs_);
            paramCallback_(attackParamId_, normalized);
        }
    }

    void handleSustainDrag(float deltaX, float deltaY) {
        // Horizontal: changes decay time
        float totalTimeWidth = layout_.releaseEndX - layout_.sustainEndX +
                               layout_.decayEndX - layout_.attackEndX +
                               layout_.attackEndX - layout_.attackStartX;
        if (totalTimeWidth > 0.0f) {
            float timeFraction = deltaX / totalTimeWidth;
            float logRange = std::log1p(kMaxTimeMs) - std::log1p(kMinTimeMs);
            float currentLogTime = std::log1p(decayMs_);
            float newLogTime = currentLogTime + timeFraction * logRange * 0.3f;
            float newDecayMs = std::clamp(std::expm1(newLogTime), kMinTimeMs, kMaxTimeMs);

            decayMs_ = newDecayMs;

            if (paramCallback_ && decayParamId_ > 0) {
                float normalized = timeMsToNormalized(decayMs_);
                paramCallback_(decayParamId_, normalized);
            }
        }

        // Vertical: changes sustain level
        float range = layout_.bottomY - layout_.topY;
        if (range > 0.0f) {
            float levelDelta = -deltaY / range;
            sustainLevel_ = std::clamp(sustainLevel_ + levelDelta, 0.0f, 1.0f);

            if (paramCallback_ && sustainParamId_ > 0) {
                paramCallback_(sustainParamId_, sustainLevel_);
            }
        }

        recalculateLayout();
        setDirty();
    }

    void handleEndPointDrag(float deltaX) {
        // Horizontal drag changes release time
        float totalTimeWidth = layout_.releaseEndX - layout_.sustainEndX +
                               layout_.decayEndX - layout_.attackEndX +
                               layout_.attackEndX - layout_.attackStartX;
        if (totalTimeWidth <= 0.0f) return;

        float timeFraction = deltaX / totalTimeWidth;
        float logRange = std::log1p(kMaxTimeMs) - std::log1p(kMinTimeMs);
        float currentLogTime = std::log1p(releaseMs_);
        float newLogTime = currentLogTime + timeFraction * logRange * 0.3f;
        float newReleaseMs = std::clamp(std::expm1(newLogTime), kMinTimeMs, kMaxTimeMs);

        releaseMs_ = newReleaseMs;
        recalculateLayout();
        setDirty();

        if (paramCallback_ && releaseParamId_ > 0) {
            float normalized = timeMsToNormalized(releaseMs_);
            paramCallback_(releaseParamId_, normalized);
        }
    }

    void handleCurveDrag(float& curveAmount, uint32_t paramId, float deltaY) {
        // Vertical drag: down = more exponential (+), up = more logarithmic (-)
        float delta = deltaY * kCurveDragSensitivity;
        curveAmount = std::clamp(curveAmount + delta, -1.0f, 1.0f);
        setDirty();

        if (paramCallback_ && paramId > 0) {
            // Normalize curve amount from [-1,+1] to [0,1]
            float normalized = (curveAmount + 1.0f) * 0.5f;
            paramCallback_(paramId, normalized);
        }
    }

    // =========================================================================
    // Bezier Handle Drag
    // =========================================================================

    void handleBezierHandleDrag(float deltaX, float deltaY) {
        int seg = activeBezierSegment_;
        int handle = activeBezierHandle_;
        if (seg < 0 || seg > 2 || handle < 0 || handle > 1) return;

        auto& bh = bezierHandles_[static_cast<size_t>(seg)];

        // Get segment bounds for pixel-to-normalized conversion
        float segStartX = 0.0f, segEndX = 0.0f;
        float segStartY = 0.0f, segEndY = 0.0f;
        getSegmentBounds(seg, segStartX, segEndX, segStartY, segEndY);

        float segWidth = segEndX - segStartX;
        float segHeight = segEndY - segStartY;

        // Convert pixel delta to normalized delta
        float normDeltaX = (segWidth > 0.0f) ? (deltaX / segWidth) : 0.0f;
        float normDeltaY = (std::abs(segHeight) > 0.0f) ? (deltaY / segHeight) : 0.0f;

        if (handle == 0) {
            bh.cp1x = std::clamp(bh.cp1x + normDeltaX, 0.0f, 1.0f);
            bh.cp1y = std::clamp(bh.cp1y + normDeltaY, 0.0f, 1.0f);
        } else {
            bh.cp2x = std::clamp(bh.cp2x + normDeltaX, 0.0f, 1.0f);
            bh.cp2y = std::clamp(bh.cp2y + normDeltaY, 0.0f, 1.0f);
        }
        setDirty();

        // Notify parameter changes for Bezier control points
        if (paramCallback_ && bezierBaseParamId_ > 0) {
            uint32_t offset = static_cast<uint32_t>(seg * 4);
            if (handle == 0) {
                paramCallback_(bezierBaseParamId_ + offset, bh.cp1x);
                paramCallback_(bezierBaseParamId_ + offset + 1, bh.cp1y);
            } else {
                paramCallback_(bezierBaseParamId_ + offset + 2, bh.cp2x);
                paramCallback_(bezierBaseParamId_ + offset + 3, bh.cp2y);
            }
        }
    }

    void getSegmentBounds(int seg, float& startX, float& endX,
                           float& startY, float& endY) const {
        switch (seg) {
            case 0: // Attack
                startX = layout_.attackStartX;
                endX = layout_.attackEndX;
                startY = layout_.bottomY;
                endY = layout_.topY;
                break;
            case 1: // Decay
                startX = layout_.attackEndX;
                endX = layout_.decayEndX;
                startY = layout_.topY;
                endY = levelToPixelY(sustainLevel_);
                break;
            case 2: // Release
                startX = layout_.sustainEndX;
                endX = layout_.releaseEndX;
                startY = levelToPixelY(sustainLevel_);
                endY = layout_.bottomY;
                break;
            default:
                startX = endX = startY = endY = 0.0f;
                break;
        }
    }

    // =========================================================================
    // Mode Toggle
    // =========================================================================

    void handleModeToggle() {
        if (bezierEnabled_) {
            // Bezier -> Simple: convert handle positions to curve amounts
            for (int seg = 0; seg < 3; ++seg) {
                const auto& bh = bezierHandles_[static_cast<size_t>(seg)];
                float curveAmount = Krate::DSP::bezierToSimpleCurve(
                    bh.cp1x, bh.cp1y, bh.cp2x, bh.cp2y);

                float* targetCurve = nullptr;
                uint32_t curveParamId = 0;
                switch (seg) {
                    case 0: targetCurve = &attackCurve_; curveParamId = attackCurveParamId_; break;
                    case 1: targetCurve = &decayCurve_; curveParamId = decayCurveParamId_; break;
                    case 2: targetCurve = &releaseCurve_; curveParamId = releaseCurveParamId_; break;
                    default: break;
                }
                if (targetCurve) {
                    *targetCurve = std::clamp(curveAmount, -1.0f, 1.0f);
                    if (paramCallback_ && curveParamId > 0) {
                        float normalized = (*targetCurve + 1.0f) * 0.5f;
                        paramCallback_(curveParamId, normalized);
                    }
                }
            }

            bezierEnabled_ = false;
        } else {
            // Simple -> Bezier: convert curve amounts to handle positions
            float curves[3] = {attackCurve_, decayCurve_, releaseCurve_};
            for (int seg = 0; seg < 3; ++seg) {
                auto& bh = bezierHandles_[static_cast<size_t>(seg)];
                Krate::DSP::simpleCurveToBezier(curves[seg],
                    bh.cp1x, bh.cp1y, bh.cp2x, bh.cp2y);

                // Notify parameter changes for all Bezier control points
                if (paramCallback_ && bezierBaseParamId_ > 0) {
                    uint32_t offset = static_cast<uint32_t>(seg * 4);
                    paramCallback_(bezierBaseParamId_ + offset, bh.cp1x);
                    paramCallback_(bezierBaseParamId_ + offset + 1, bh.cp1y);
                    paramCallback_(bezierBaseParamId_ + offset + 2, bh.cp2x);
                    paramCallback_(bezierBaseParamId_ + offset + 3, bh.cp2y);
                }
            }

            bezierEnabled_ = true;
        }

        // Notify Bezier enabled state change
        if (paramCallback_ && bezierEnabledParamId_ > 0) {
            paramCallback_(bezierEnabledParamId_, bezierEnabled_ ? 1.0f : 0.0f);
        }

        setDirty();
    }

    // =========================================================================
    // Double-click Reset
    // =========================================================================

    void handleDoubleClick(DragTarget target) {
        switch (target) {
            case DragTarget::PeakPoint:
                notifyBeginEdit(target);
                attackMs_ = kDefaultAttackMs;
                recalculateLayout();
                setDirty();
                if (paramCallback_ && attackParamId_ > 0) {
                    paramCallback_(attackParamId_, timeMsToNormalized(attackMs_));
                }
                notifyEndEdit(target);
                break;
            case DragTarget::SustainPoint:
                notifyBeginEdit(target);
                decayMs_ = kDefaultDecayMs;
                sustainLevel_ = kDefaultSustainLevel;
                recalculateLayout();
                setDirty();
                if (paramCallback_ && decayParamId_ > 0) {
                    paramCallback_(decayParamId_, timeMsToNormalized(decayMs_));
                }
                if (paramCallback_ && sustainParamId_ > 0) {
                    paramCallback_(sustainParamId_, sustainLevel_);
                }
                notifyEndEdit(target);
                break;
            case DragTarget::EndPoint:
                notifyBeginEdit(target);
                releaseMs_ = kDefaultReleaseMs;
                recalculateLayout();
                setDirty();
                if (paramCallback_ && releaseParamId_ > 0) {
                    paramCallback_(releaseParamId_, timeMsToNormalized(releaseMs_));
                }
                notifyEndEdit(target);
                break;
            case DragTarget::AttackCurve:
                notifyBeginEdit(target);
                attackCurve_ = 0.0f;
                setDirty();
                if (paramCallback_ && attackCurveParamId_ > 0) {
                    paramCallback_(attackCurveParamId_, 0.5f);
                }
                notifyEndEdit(target);
                break;
            case DragTarget::DecayCurve:
                notifyBeginEdit(target);
                decayCurve_ = 0.0f;
                setDirty();
                if (paramCallback_ && decayCurveParamId_ > 0) {
                    paramCallback_(decayCurveParamId_, 0.5f);
                }
                notifyEndEdit(target);
                break;
            case DragTarget::ReleaseCurve:
                notifyBeginEdit(target);
                releaseCurve_ = 0.0f;
                setDirty();
                if (paramCallback_ && releaseCurveParamId_ > 0) {
                    paramCallback_(releaseCurveParamId_, 0.5f);
                }
                notifyEndEdit(target);
                break;
            default:
                break;
        }
    }

    // =========================================================================
    // Cancel Drag (Escape)
    // =========================================================================

    void cancelDrag() {
        if (!isDragging_) return;

        // Restore pre-drag values
        attackMs_ = preDragValues_.attackMs;
        decayMs_ = preDragValues_.decayMs;
        sustainLevel_ = preDragValues_.sustainLevel;
        releaseMs_ = preDragValues_.releaseMs;
        attackCurve_ = preDragValues_.attackCurve;
        decayCurve_ = preDragValues_.decayCurve;
        releaseCurve_ = preDragValues_.releaseCurve;

        // Notify restored values via callbacks
        notifyRestoredValues();

        notifyEndEdit(dragTarget_);
        isDragging_ = false;
        dragTarget_ = DragTarget::None;

        recalculateLayout();
        setDirty();
    }

    void notifyRestoredValues() {
        if (paramCallback_) {
            if (attackParamId_ > 0)
                paramCallback_(attackParamId_, timeMsToNormalized(attackMs_));
            if (decayParamId_ > 0)
                paramCallback_(decayParamId_, timeMsToNormalized(decayMs_));
            if (sustainParamId_ > 0)
                paramCallback_(sustainParamId_, sustainLevel_);
            if (releaseParamId_ > 0)
                paramCallback_(releaseParamId_, timeMsToNormalized(releaseMs_));
            if (attackCurveParamId_ > 0)
                paramCallback_(attackCurveParamId_, (attackCurve_ + 1.0f) * 0.5f);
            if (decayCurveParamId_ > 0)
                paramCallback_(decayCurveParamId_, (decayCurve_ + 1.0f) * 0.5f);
            if (releaseCurveParamId_ > 0)
                paramCallback_(releaseCurveParamId_, (releaseCurve_ + 1.0f) * 0.5f);
        }
    }

    // =========================================================================
    // Parameter Notification Helpers
    // =========================================================================

    void notifyBeginEdit(DragTarget target) {
        if (!beginEditCallback_) return;

        switch (target) {
            case DragTarget::PeakPoint:
                if (attackParamId_ > 0) beginEditCallback_(attackParamId_);
                break;
            case DragTarget::SustainPoint:
                if (decayParamId_ > 0) beginEditCallback_(decayParamId_);
                if (sustainParamId_ > 0) beginEditCallback_(sustainParamId_);
                break;
            case DragTarget::EndPoint:
                if (releaseParamId_ > 0) beginEditCallback_(releaseParamId_);
                break;
            case DragTarget::AttackCurve:
                if (attackCurveParamId_ > 0) beginEditCallback_(attackCurveParamId_);
                break;
            case DragTarget::DecayCurve:
                if (decayCurveParamId_ > 0) beginEditCallback_(decayCurveParamId_);
                break;
            case DragTarget::ReleaseCurve:
                if (releaseCurveParamId_ > 0) beginEditCallback_(releaseCurveParamId_);
                break;
            case DragTarget::BezierHandle:
                if (bezierBaseParamId_ > 0) {
                    uint32_t offset = static_cast<uint32_t>(activeBezierSegment_ * 4 + activeBezierHandle_ * 2);
                    beginEditCallback_(bezierBaseParamId_ + offset);
                    beginEditCallback_(bezierBaseParamId_ + offset + 1);
                }
                break;
            default:
                break;
        }
    }

    void notifyEndEdit(DragTarget target) {
        if (!endEditCallback_) return;

        switch (target) {
            case DragTarget::PeakPoint:
                if (attackParamId_ > 0) endEditCallback_(attackParamId_);
                break;
            case DragTarget::SustainPoint:
                if (decayParamId_ > 0) endEditCallback_(decayParamId_);
                if (sustainParamId_ > 0) endEditCallback_(sustainParamId_);
                break;
            case DragTarget::EndPoint:
                if (releaseParamId_ > 0) endEditCallback_(releaseParamId_);
                break;
            case DragTarget::AttackCurve:
                if (attackCurveParamId_ > 0) endEditCallback_(attackCurveParamId_);
                break;
            case DragTarget::DecayCurve:
                if (decayCurveParamId_ > 0) endEditCallback_(decayCurveParamId_);
                break;
            case DragTarget::ReleaseCurve:
                if (releaseCurveParamId_ > 0) endEditCallback_(releaseCurveParamId_);
                break;
            case DragTarget::BezierHandle:
                if (bezierBaseParamId_ > 0) {
                    uint32_t offset = static_cast<uint32_t>(activeBezierSegment_ * 4 + activeBezierHandle_ * 2);
                    endEditCallback_(bezierBaseParamId_ + offset);
                    endEditCallback_(bezierBaseParamId_ + offset + 1);
                }
                break;
            default:
                break;
        }
    }

    // =========================================================================
    // Time <-> Normalized Conversion
    // =========================================================================

    /// Convert time in ms to normalized [0,1] using cubic mapping
    /// normalized^3 * 10000 = ms
    [[nodiscard]] static float timeMsToNormalized(float ms) {
        float clamped = std::clamp(ms, kMinTimeMs, kMaxTimeMs);
        return std::cbrt(clamped / kMaxTimeMs);
    }

    /// Convert normalized [0,1] to time in ms using cubic mapping
    [[nodiscard]] static float normalizedToTimeMs(float normalized) {
        float clamped = std::clamp(normalized, 0.0f, 1.0f);
        return std::clamp(clamped * clamped * clamped * kMaxTimeMs,
                          kMinTimeMs, kMaxTimeMs);
    }

    // =========================================================================
    // Drawing Helpers
    // =========================================================================

    void drawBackground(VSTGUI::CDrawContext* context) const {
        VSTGUI::CRect vs = getViewSize();
        context->setFillColor(backgroundColor_);
        context->drawRect(vs, VSTGUI::kDrawFilled);
    }

    void drawGrid(VSTGUI::CDrawContext* context) const {
        context->setFrameColor(gridColor_);
        context->setLineWidth(1.0);
        context->setLineStyle(VSTGUI::kLineSolid);

        // Horizontal grid lines at 25%, 50%, 75% level
        for (float level : {0.25f, 0.50f, 0.75f}) {
            float y = levelToPixelY(level);
            context->drawLine(
                VSTGUI::CPoint(layout_.attackStartX, y),
                VSTGUI::CPoint(layout_.releaseEndX, y));
        }
    }

    void drawEnvelopeCurve(VSTGUI::CDrawContext* context) const {
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path) return;

        constexpr int kCurveResolution = 32;

        // Start at bottom-left (attack start, level=0)
        path->beginSubpath(VSTGUI::CPoint(layout_.attackStartX, layout_.bottomY));

        float sustainY = levelToPixelY(sustainLevel_);

        if (bezierEnabled_) {
            // Bezier mode: use Bezier tables for curve segments
            drawBezierCurveSegment(path, layout_.attackStartX, layout_.bottomY,
                                    layout_.attackEndX, layout_.topY,
                                    bezierHandles_[0], kCurveResolution);
            drawBezierCurveSegment(path, layout_.attackEndX, layout_.topY,
                                    layout_.decayEndX, sustainY,
                                    bezierHandles_[1], kCurveResolution);
            path->addLine(VSTGUI::CPoint(layout_.sustainEndX, sustainY));
            drawBezierCurveSegment(path, layout_.sustainEndX, sustainY,
                                    layout_.releaseEndX, layout_.bottomY,
                                    bezierHandles_[2], kCurveResolution);
        } else {
            // Simple mode: use power curve tables
            drawCurveSegment(path, layout_.attackStartX, layout_.bottomY,
                             layout_.attackEndX, layout_.topY,
                             attackCurve_, kCurveResolution);
            drawCurveSegment(path, layout_.attackEndX, layout_.topY,
                             layout_.decayEndX, sustainY,
                             decayCurve_, kCurveResolution);
            path->addLine(VSTGUI::CPoint(layout_.sustainEndX, sustainY));
            drawCurveSegment(path, layout_.sustainEndX, sustainY,
                             layout_.releaseEndX, layout_.bottomY,
                             releaseCurve_, kCurveResolution);
        }

        // Close path to baseline
        path->addLine(VSTGUI::CPoint(layout_.releaseEndX, layout_.bottomY));
        path->closeSubpath();

        // Filled
        context->setFillColor(fillColor_);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);

        // Stroked
        context->setFrameColor(strokeColor_);
        context->setLineWidth(1.5);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
    }

    /// Draw a curved segment using power curve lookup
    static void drawCurveSegment(VSTGUI::SharedPointer<VSTGUI::CGraphicsPath>& path,
                                  float startX, float startY,
                                  float endX, float endY,
                                  float curveAmount, int resolution) {
        // Generate a temporary table for the curve shape
        std::array<float, Krate::DSP::kCurveTableSize> table{};
        Krate::DSP::generatePowerCurveTable(table, curveAmount);

        for (int i = 1; i <= resolution; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(resolution);
            float curveVal = Krate::DSP::lookupCurveTable(table, phase);

            float x = startX + phase * (endX - startX);
            float y = startY + curveVal * (endY - startY);
            path->addLine(VSTGUI::CPoint(x, y));
        }
    }

    /// Draw a curved segment using Bezier curve lookup
    static void drawBezierCurveSegment(
        VSTGUI::SharedPointer<VSTGUI::CGraphicsPath>& path,
        float startX, float startY,
        float endX, float endY,
        const BezierHandles& handles, int resolution)
    {
        std::array<float, Krate::DSP::kCurveTableSize> table{};
        Krate::DSP::generateBezierCurveTable(table,
            handles.cp1x, handles.cp1y, handles.cp2x, handles.cp2y);

        for (int i = 1; i <= resolution; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(resolution);
            float curveVal = Krate::DSP::lookupCurveTable(table, phase);

            float x = startX + phase * (endX - startX);
            float y = startY + curveVal * (endY - startY);
            path->addLine(VSTGUI::CPoint(x, y));
        }
    }

    void drawSustainHoldLine(VSTGUI::CDrawContext* context) const {
        float sustainY = levelToPixelY(sustainLevel_);

        // Dashed horizontal line from Sustain point to release start
        const VSTGUI::CCoord dashPattern[] = {4.0, 3.0};
        VSTGUI::CLineStyle dashStyle(
            VSTGUI::CLineStyle::kLineCapButt,
            VSTGUI::CLineStyle::kLineJoinMiter,
            0.0, 2, dashPattern);

        context->setFrameColor(strokeColor_);
        context->setLineWidth(1.0);
        context->setLineStyle(dashStyle);

        context->drawLine(
            VSTGUI::CPoint(layout_.decayEndX, sustainY),
            VSTGUI::CPoint(layout_.sustainEndX, sustainY));
    }

    void drawGateMarker(VSTGUI::CDrawContext* context) const {
        // Vertical dashed line at the gate-off boundary (sustain end / release start)
        float gateX = layout_.sustainEndX;

        const VSTGUI::CCoord dashPattern[] = {4.0, 3.0};
        VSTGUI::CLineStyle dashStyle(
            VSTGUI::CLineStyle::kLineCapButt,
            VSTGUI::CLineStyle::kLineJoinMiter,
            0.0, 2, dashPattern);

        VSTGUI::CColor gateColor = gridColor_;
        gateColor.alpha = std::min(static_cast<uint8_t>(gateColor.alpha + 20), static_cast<uint8_t>(255));

        context->setFrameColor(gateColor);
        context->setLineWidth(1.0);
        context->setLineStyle(dashStyle);

        context->drawLine(
            VSTGUI::CPoint(gateX, layout_.topY),
            VSTGUI::CPoint(gateX, layout_.bottomY));
    }

    void drawTimeLabels(VSTGUI::CDrawContext* context) const {
        VSTGUI::CRect vs = getViewSize();
        float displayHeight = static_cast<float>(vs.getHeight());

        // Skip labels if display is too small to fit them readably
        if (displayHeight < 60.0f) return;

        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 8.0);
        context->setFont(font);
        context->setFontColor(textColor_);

        constexpr float kLabelHeight = 10.0f;
        constexpr float kLabelWidth = 40.0f;
        constexpr float kLabelOffsetY = 2.0f;

        // Attack time label (below Peak point)
        {
            char buf[16];
            formatTimeLabel(buf, sizeof(buf), attackMs_);
            VSTGUI::CRect labelRect(
                layout_.attackEndX - kLabelWidth * 0.5,
                layout_.topY - kLabelHeight - kLabelOffsetY,
                layout_.attackEndX + kLabelWidth * 0.5,
                layout_.topY - kLabelOffsetY);
            // Clamp to display bounds
            if (labelRect.top < vs.top) {
                labelRect.offset(0, vs.top - labelRect.top + 1);
            }
            context->drawString(VSTGUI::UTF8String(buf), labelRect,
                                VSTGUI::kCenterText);
        }

        // Release time label (near End point)
        {
            char buf[16];
            formatTimeLabel(buf, sizeof(buf), releaseMs_);
            VSTGUI::CRect labelRect(
                layout_.releaseEndX - kLabelWidth,
                layout_.bottomY - kLabelHeight,
                layout_.releaseEndX,
                layout_.bottomY);
            context->drawString(VSTGUI::UTF8String(buf), labelRect,
                                VSTGUI::kRightText);
        }

        // Total duration label (bottom-right corner)
        {
            float totalMs = attackMs_ + decayMs_ + releaseMs_;
            char buf[24];
            formatTimeLabel(buf, sizeof(buf), totalMs);
            VSTGUI::CRect labelRect(
                vs.right - kLabelWidth - kPadding,
                vs.bottom - kLabelHeight - 1,
                vs.right - kPadding,
                vs.bottom - 1);
            context->drawString(VSTGUI::UTF8String(buf), labelRect,
                                VSTGUI::kRightText);
        }
    }

    /// Format a time value as a compact string (e.g., "10ms", "1.5s")
    static void formatTimeLabel(char* buf, size_t bufSize, float timeMs) {
        if (timeMs >= 1000.0f) {
            std::snprintf(buf, bufSize, "%.1fs", timeMs / 1000.0f);
        } else if (timeMs >= 100.0f) {
            std::snprintf(buf, bufSize, "%.0fms", timeMs);
        } else if (timeMs >= 10.0f) {
            std::snprintf(buf, bufSize, "%.0fms", timeMs);
        } else {
            std::snprintf(buf, bufSize, "%.1fms", timeMs);
        }
    }

    void drawCurveTooltip(VSTGUI::CDrawContext* context) const {
        // Only show during curve segment drags
        if (!isDragging_) return;

        float curveVal = 0.0f;
        switch (dragTarget_) {
            case DragTarget::AttackCurve:
                curveVal = attackCurve_;
                break;
            case DragTarget::DecayCurve:
                curveVal = decayCurve_;
                break;
            case DragTarget::ReleaseCurve:
                curveVal = releaseCurve_;
                break;
            default:
                return; // Not a curve drag
        }

        char buf[24];
        std::snprintf(buf, sizeof(buf), "Curve: %+.2f", curveVal);

        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 9.0);
        context->setFont(font);
        context->setFontColor(controlPointColor_);

        // Draw tooltip in upper-left area of the display
        VSTGUI::CRect vs = getViewSize();
        VSTGUI::CRect tooltipRect(
            vs.left + kPadding + 2,
            vs.top + kPadding,
            vs.left + kPadding + 80,
            vs.top + kPadding + 12);
        context->drawString(VSTGUI::UTF8String(buf), tooltipRect,
                            VSTGUI::kLeftText);
    }

    void drawControlPoints(VSTGUI::CDrawContext* context) const {
        context->setFillColor(controlPointColor_);

        // Peak point
        auto peakPos = getControlPointPosition(DragTarget::PeakPoint);
        drawCircle(context, peakPos, kControlPointDrawRadius);

        // Sustain point
        auto sustainPos = getControlPointPosition(DragTarget::SustainPoint);
        drawCircle(context, sustainPos, kControlPointDrawRadius);

        // End point
        auto endPos = getControlPointPosition(DragTarget::EndPoint);
        drawCircle(context, endPos, kControlPointDrawRadius);
    }

    static void drawCircle(VSTGUI::CDrawContext* context,
                            const VSTGUI::CPoint& center, float radius) {
        VSTGUI::CRect circleRect(
            center.x - radius, center.y - radius,
            center.x + radius, center.y + radius);
        context->drawEllipse(circleRect, VSTGUI::kDrawFilled);
    }

    /// Draw diamond shape for Bezier handles
    static void drawDiamond(VSTGUI::CDrawContext* context,
                             const VSTGUI::CPoint& center, float halfSize) {
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path) return;

        path->beginSubpath(VSTGUI::CPoint(center.x, center.y - halfSize));
        path->addLine(VSTGUI::CPoint(center.x + halfSize, center.y));
        path->addLine(VSTGUI::CPoint(center.x, center.y + halfSize));
        path->addLine(VSTGUI::CPoint(center.x - halfSize, center.y));
        path->closeSubpath();

        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
    }

    void drawModeToggle(VSTGUI::CDrawContext* context) const {
        VSTGUI::CRect vs = getViewSize();
        float btnRight = static_cast<float>(vs.right) - kPadding;
        float btnLeft = btnRight - kModeToggleSize;
        float btnTop = static_cast<float>(vs.top) + kPadding;
        float btnBottom = btnTop + kModeToggleSize;

        VSTGUI::CRect btnRect(btnLeft, btnTop, btnRight, btnBottom);

        // Draw button background
        VSTGUI::CColor btnBg = bezierEnabled_
            ? VSTGUI::CColor(80, 100, 160, 200)
            : VSTGUI::CColor(60, 60, 65, 200);
        context->setFillColor(btnBg);
        context->drawRect(btnRect, VSTGUI::kDrawFilled);

        // Draw border
        context->setFrameColor(VSTGUI::CColor(120, 120, 130, 200));
        context->setLineWidth(1.0);
        context->setLineStyle(VSTGUI::kLineSolid);
        context->drawRect(btnRect, VSTGUI::kDrawStroked);

        // Draw label
        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 9.0,
            VSTGUI::kBoldFace);
        context->setFont(font);
        context->setFontColor(VSTGUI::CColor(220, 220, 230, 255));
        context->drawString(
            VSTGUI::UTF8String(bezierEnabled_ ? "B" : "S"),
            btnRect, VSTGUI::kCenterText);
    }

    void pollPlaybackState() {
        if (!playbackOutputPtr_ || !playbackStagePtr_ || !playbackActivePtr_)
            return;

        float output = playbackOutputPtr_->load(std::memory_order_relaxed);
        int stage = playbackStagePtr_->load(std::memory_order_relaxed);
        bool active = playbackActivePtr_->load(std::memory_order_relaxed);

        // Only redraw if state actually changed
        if (output != playbackOutput_ || stage != playbackStage_ || active != voiceActive_) {
            playbackOutput_ = output;
            playbackStage_ = stage;
            voiceActive_ = active;
            setDirty();
        }
    }

    void drawPlaybackDot(VSTGUI::CDrawContext* context) const {
        if (!voiceActive_) return;

        auto dotPos = getPlaybackDotPosition();

        // Draw a bright 6px dot
        constexpr float kPlaybackDotRadius = 3.0f;

        // Glow effect: slightly larger semi-transparent circle behind the dot
        VSTGUI::CColor glowColor = strokeColor_;
        glowColor.alpha = 80;
        context->setFillColor(glowColor);
        drawCircle(context, dotPos, kPlaybackDotRadius + 2.0f);

        // Bright dot on top
        VSTGUI::CColor dotColor(255, 255, 255, 255);
        context->setFillColor(dotColor);
        drawCircle(context, dotPos, kPlaybackDotRadius);
    }

    void drawBezierHandles(VSTGUI::CDrawContext* context) const {
        VSTGUI::CColor handleColor(180, 180, 190, 255);
        VSTGUI::CColor activeColor(230, 230, 240, 255);
        VSTGUI::CColor lineColor(100, 100, 100, 200);

        context->setLineWidth(1.0);
        context->setLineStyle(VSTGUI::kLineSolid);

        for (int seg = 0; seg < 3; ++seg) {
            float segStartX = 0.0f, segEndX = 0.0f;
            float segStartY = 0.0f, segEndY = 0.0f;
            getSegmentBounds(seg, segStartX, segEndX, segStartY, segEndY);

            VSTGUI::CPoint segStart(segStartX, segStartY);
            VSTGUI::CPoint segEnd(segEndX, segEndY);

            for (int handle = 0; handle < 2; ++handle) {
                auto handlePos = getBezierHandlePixelPos(seg, handle);

                // Draw connection line to segment endpoint
                context->setFrameColor(lineColor);
                auto lineTarget = (handle == 0) ? segStart : segEnd;
                context->drawLine(lineTarget, handlePos);

                // Draw diamond handle
                bool isActive = isDragging_ &&
                    dragTarget_ == DragTarget::BezierHandle &&
                    activeBezierSegment_ == seg &&
                    activeBezierHandle_ == handle;
                context->setFillColor(isActive ? activeColor : handleColor);
                drawDiamond(context, handlePos, kBezierHandleDrawSize);
            }
        }
    }

    // =========================================================================
    // State
    // =========================================================================

    // ADSR parameters
    float attackMs_ = kDefaultAttackMs;
    float decayMs_ = kDefaultDecayMs;
    float sustainLevel_ = kDefaultSustainLevel;
    float releaseMs_ = kDefaultReleaseMs;

    // Curve amounts
    float attackCurve_ = 0.0f;
    float decayCurve_ = 0.0f;
    float releaseCurve_ = 0.0f;

    // Bezier mode
    bool bezierEnabled_ = false;
    std::array<BezierHandles, 3> bezierHandles_{
        BezierHandles{0.33f, 0.33f, 0.67f, 0.67f},  // Attack
        BezierHandles{0.33f, 0.67f, 0.67f, 0.33f},  // Decay
        BezierHandles{0.33f, 0.67f, 0.67f, 0.33f}   // Release
    };

    // Cached layout
    SegmentLayout layout_{};

    // Drag state
    bool isDragging_ = false;
    DragTarget dragTarget_ = DragTarget::None;
    VSTGUI::CPoint lastDragPoint_{0, 0};
    PreDragValues preDragValues_{};
    mutable int activeBezierSegment_ = -1;   // mutable: set in const hitTest
    mutable int activeBezierHandle_ = -1;    // mutable: set in const hitTest

    // Playback visualization
    float playbackOutput_ = 0.0f;
    int playbackStage_ = 0;
    bool voiceActive_ = false;

    // Colors
    VSTGUI::CColor fillColor_{80, 140, 200, 77};
    VSTGUI::CColor strokeColor_{80, 140, 200, 255};
    VSTGUI::CColor backgroundColor_{30, 30, 33, 255};
    VSTGUI::CColor gridColor_{255, 255, 255, 25};
    VSTGUI::CColor controlPointColor_{255, 255, 255, 255};
    VSTGUI::CColor textColor_{255, 255, 255, 180};

    // Parameter IDs
    uint32_t attackParamId_ = 0;
    uint32_t decayParamId_ = 0;
    uint32_t sustainParamId_ = 0;
    uint32_t releaseParamId_ = 0;
    uint32_t attackCurveParamId_ = 0;
    uint32_t decayCurveParamId_ = 0;
    uint32_t releaseCurveParamId_ = 0;
    uint32_t bezierEnabledParamId_ = 0;
    uint32_t bezierBaseParamId_ = 0;

    // Callbacks
    ParameterCallback paramCallback_;
    EditCallback beginEditCallback_;
    EditCallback endEditCallback_;

    // Playback state atomic pointers (from processor via IMessage)
    std::atomic<float>* playbackOutputPtr_ = nullptr;
    std::atomic<int>* playbackStagePtr_ = nullptr;
    std::atomic<bool>* playbackActivePtr_ = nullptr;

    // Timer for playback refresh (~30fps)
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> playbackTimer_;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================
// Registers "ADSRDisplay" with the VSTGUI UIViewFactory.
// getBaseViewName() -> "CControl" ensures all CControl attributes
// (control-tag, default-value, min-value, max-value, etc.) are applied.

struct ADSRDisplayCreator : VSTGUI::ViewCreatorAdapter {
    ADSRDisplayCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override { return "ADSRDisplay"; }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "ADSR Display";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new ADSRDisplay(VSTGUI::CRect(0, 0, 140, 90), nullptr, -1);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override {
        auto* display = dynamic_cast<ADSRDisplay*>(view);
        if (!display)
            return false;

        VSTGUI::CColor color;
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("fill-color"), color, description))
            display->setFillColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("stroke-color"), color, description))
            display->setStrokeColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("background-color"), color, description))
            display->setBackgroundColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("grid-color"), color, description))
            display->setGridColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("control-point-color"), color, description))
            display->setControlPointColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("text-color"), color, description))
            display->setTextColor(color);

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("fill-color");
        attributeNames.emplace_back("stroke-color");
        attributeNames.emplace_back("background-color");
        attributeNames.emplace_back("grid-color");
        attributeNames.emplace_back("control-point-color");
        attributeNames.emplace_back("text-color");
        return true;
    }

    AttrType getAttributeType(const std::string& attributeName) const override {
        if (attributeName == "fill-color") return kColorType;
        if (attributeName == "stroke-color") return kColorType;
        if (attributeName == "background-color") return kColorType;
        if (attributeName == "grid-color") return kColorType;
        if (attributeName == "control-point-color") return kColorType;
        if (attributeName == "text-color") return kColorType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override {
        auto* display = dynamic_cast<ADSRDisplay*>(view);
        if (!display)
            return false;

        if (attributeName == "fill-color") {
            VSTGUI::UIViewCreator::colorToString(
                display->getFillColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "stroke-color") {
            VSTGUI::UIViewCreator::colorToString(
                display->getStrokeColor(), stringValue, desc);
            return true;
        }
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
        if (attributeName == "control-point-color") {
            VSTGUI::UIViewCreator::colorToString(
                display->getControlPointColor(), stringValue, desc);
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
inline ADSRDisplayCreator gADSRDisplayCreator;

} // namespace Krate::Plugins
