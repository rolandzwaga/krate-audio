// ==============================================================================
// Speed Curve Editor — Semi-transparent overlay for per-lane speed curves
// ==============================================================================
// Displays and edits a free-form Bezier curve that modulates lane speed over
// one loop cycle. Rendered as a semi-transparent overlay on top of the lane
// editor's step bars.
//
// Interaction:
//   - Drag control points to reposition
//   - Drag Bezier handles (diamonds) to adjust curve shape
//   - Click empty area to add a new control point
//   - Right-click a control point to remove it (endpoints are fixed)
//   - Shift+drag for fine adjustment
//   - Escape cancels current drag
//
// The editor communicates changes via a callback that provides the updated
// SpeedCurveData. The controller rebakes the lookup table and sends it to
// the processor via IMessage.
// ==============================================================================

#pragma once

#include "speed_curve_data.h"
#include "speed_curve_presets.h"

#include "vstgui/lib/cview.h"
#include "vstgui/lib/vstkeycode.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <algorithm>
#include <functional>

namespace Gradus {

class SpeedCurveEditor : public VSTGUI::CView {
public:
    /// Called on every edit (mouse move) — for display updates only.
    using CurveChangedCallback = std::function<void(const SpeedCurveData&)>;
    /// Called on mouse-up — for IMessage sending to processor.
    using CurveCommittedCallback = std::function<void(const SpeedCurveData&)>;

    explicit SpeedCurveEditor(const VSTGUI::CRect& size)
        : CView(size)
    {
        setTransparency(true);
        setWantsFocus(true);
    }

    ~SpeedCurveEditor() override = default;

    // =========================================================================
    // Data Access
    // =========================================================================

    void setCurveData(const SpeedCurveData& data) {
        data_ = data;
        invalid();
    }

    [[nodiscard]] const SpeedCurveData& curveData() const { return data_; }

    void setCurveChangedCallback(CurveChangedCallback cb) {
        curveChangedCallback_ = std::move(cb);
    }

    void setCurveCommittedCallback(CurveCommittedCallback cb) {
        curveCommittedCallback_ = std::move(cb);
    }

    /// Apply a preset shape and notify.
    void applyPreset(SpeedCurvePreset preset) {
        data_.points = generatePreset(preset);
        data_.presetIndex = static_cast<int>(preset);
        notifyCurveChanged();
        notifyCurveCommitted();
        invalid();
    }

    // =========================================================================
    // CView Overrides
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        auto r = getViewSize();

        // Semi-transparent background
        context->setFillColor(VSTGUI::CColor(0, 0, 0, 100));
        context->drawRect(r, VSTGUI::kDrawFilled);

        if (data_.points.size() < 2) return;

        float contentLeft = r.left + kPadding;
        float contentRight = r.right - kPadding;
        float contentTop = r.top + kPadding;
        float contentBottom = r.bottom - kPadding;
        float contentW = contentRight - contentLeft;
        float contentH = contentBottom - contentTop;

        // Grid lines at y = 0.0, 0.25, 0.5, 0.75, 1.0
        context->setLineWidth(1.0);
        context->setFrameColor(VSTGUI::CColor(255, 255, 255, 30));
        for (float yVal : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            float py = contentBottom - yVal * contentH;
            context->drawLine(VSTGUI::CPoint(contentLeft, py),
                              VSTGUI::CPoint(contentRight, py));
        }
        // Center line (y=0.5) slightly brighter
        context->setFrameColor(VSTGUI::CColor(255, 255, 255, 60));
        float centerY = contentBottom - 0.5f * contentH;
        context->drawLine(VSTGUI::CPoint(contentLeft, centerY),
                          VSTGUI::CPoint(contentRight, centerY));

        // Bake table once for both fill and stroke
        std::array<float, 256> bakedTable{};
        data_.bakeToTable(bakedTable);

        // Draw filled area between curve and center
        drawCurveFill(context, bakedTable, contentLeft, contentTop, contentW, contentH, contentBottom);

        // Draw the curve stroke
        drawCurveStroke(context, bakedTable, contentLeft, contentTop, contentW, contentH, contentBottom);

        // Draw Bezier handles
        if (data_.points.size() >= 2) {
            context->setFrameColor(VSTGUI::CColor(200, 200, 100, 150));
            context->setLineWidth(1.0);
            for (size_t i = 0; i < data_.points.size(); ++i) {
                const auto& pt = data_.points[i];
                // Right handle (not on last point)
                if (i < data_.points.size() - 1) {
                    auto ptPos = curveToPixel(pt.x, pt.y,
                        contentLeft, contentBottom, contentW, contentH);
                    auto hPos = curveToPixel(pt.cpRightX, pt.cpRightY,
                        contentLeft, contentBottom, contentW, contentH);
                    context->drawLine(ptPos, hPos);
                    drawDiamond(context, hPos, kHandleDrawSize);
                }
                // Left handle (not on first point)
                if (i > 0) {
                    auto ptPos = curveToPixel(pt.x, pt.y,
                        contentLeft, contentBottom, contentW, contentH);
                    auto hPos = curveToPixel(pt.cpLeftX, pt.cpLeftY,
                        contentLeft, contentBottom, contentW, contentH);
                    context->drawLine(ptPos, hPos);
                    drawDiamond(context, hPos, kHandleDrawSize);
                }
            }
        }

        // Draw control points
        for (size_t i = 0; i < data_.points.size(); ++i) {
            const auto& pt = data_.points[i];
            auto pos = curveToPixel(pt.x, pt.y,
                contentLeft, contentBottom, contentW, contentH);
            bool isSelected = (selectedPointIndex_ == static_cast<int>(i));
            bool isDragged = (isDragging_ && dragTarget_ == DragTarget::Point &&
                              dragIndex_ == static_cast<int>(i));

            // Selection ring (outer glow)
            if (isSelected) {
                context->setFillColor(VSTGUI::CColor(255, 220, 120, 60));
                context->setFrameColor(VSTGUI::CColor(255, 220, 120, 180));
                context->setLineWidth(1.5);
                float selR = kPointDrawRadius + 3.0f;
                VSTGUI::CRect selCircle(pos.x - selR, pos.y - selR,
                                         pos.x + selR, pos.y + selR);
                context->drawEllipse(selCircle, VSTGUI::kDrawFilledAndStroked);
            }

            VSTGUI::CColor pointColor(220, 180, 100, 230);
            if (isSelected || isDragged)
                pointColor = VSTGUI::CColor(255, 220, 120, 255);

            context->setFillColor(pointColor);
            context->setFrameColor(VSTGUI::CColor(255, 255, 255, 180));
            context->setLineWidth(1.0);
            VSTGUI::CRect circle(pos.x - kPointDrawRadius, pos.y - kPointDrawRadius,
                                  pos.x + kPointDrawRadius, pos.y + kPointDrawRadius);
            context->drawEllipse(circle, VSTGUI::kDrawFilledAndStroked);
        }
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        if (!(buttons & VSTGUI::kLButton))
            return VSTGUI::kMouseEventNotHandled;

        auto r = getViewSize();
        float cL = r.left + kPadding, cR = r.right - kPadding;
        float cT = r.top + kPadding, cB = r.bottom - kPadding;
        float cW = cR - cL, cH = cB - cT;

        // Left-click: check hit targets
        // Priority: handle > point > empty (double-click = add point)
        int handleSeg = -1, handleSide = -1;
        if (hitTestHandle(where, cL, cB, cW, cH, handleSeg, handleSide)) {
            isDragging_ = true;
            dragTarget_ = DragTarget::Handle;
            dragIndex_ = handleSeg;
            dragHandleSide_ = handleSide;
            lastDragPoint_ = where;
            return VSTGUI::kMouseEventHandled;
        }

        int ptIdx = hitTestPoint(where, cL, cB, cW, cH);
        if (ptIdx >= 0) {
            selectedPointIndex_ = ptIdx;
            isDragging_ = true;
            dragTarget_ = DragTarget::Point;
            dragIndex_ = ptIdx;
            lastDragPoint_ = where;
            // Take keyboard focus so Delete/Backspace reaches onKeyDown
            if (auto* frame = getFrame())
                frame->setFocusView(this);
            invalid();
            return VSTGUI::kMouseEventHandled;
        }

        // Single click on empty area: deselect
        if (!(buttons & VSTGUI::kDoubleClick)) {
            selectedPointIndex_ = -1;
            invalid();
            return VSTGUI::kMouseEventHandled;
        }

        // Double-click on empty area: add a new control point

        float cx = pixelToCurveX(static_cast<float>(where.x), cL, cW);
        float cy = pixelToCurveY(static_cast<float>(where.y), cB, cH);
        cx = std::clamp(cx, 0.01f, 0.99f);
        cy = std::clamp(cy, 0.0f, 1.0f);

        SpeedCurvePoint newPt;
        newPt.x = cx; newPt.y = cy;
        newPt.cpLeftX = std::max(0.0f, cx - 0.1f);
        newPt.cpLeftY = cy;
        newPt.cpRightX = std::min(1.0f, cx + 0.1f);
        newPt.cpRightY = cy;

        // Insert in sorted position
        auto it = std::lower_bound(data_.points.begin() + 1,
            data_.points.end() - 1, newPt,
            [](const SpeedCurvePoint& a, const SpeedCurvePoint& b) {
                return a.x < b.x;
            });
        auto insertedIt = data_.points.insert(it, newPt);

        data_.presetIndex = -1;
        int newIdx = static_cast<int>(std::distance(data_.points.begin(), insertedIt));
        selectedPointIndex_ = newIdx;
        isDragging_ = true;
        dragTarget_ = DragTarget::Point;
        dragIndex_ = newIdx;
        lastDragPoint_ = where;
        if (auto* frame = getFrame())
            frame->setFocusView(this);

        notifyCurveChanged();
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        if (!isDragging_)
            return VSTGUI::kMouseEventNotHandled;

        auto r = getViewSize();
        float cL = r.left + kPadding, cR = r.right - kPadding;
        float cT [[maybe_unused]] = r.top + kPadding, cB = r.bottom - kPadding;
        float cW = cR - cL, cH = cB - cT;

        float sensitivity = (buttons & VSTGUI::kShift) ? 0.1f : 1.0f;
        float dx = static_cast<float>(where.x - lastDragPoint_.x) * sensitivity;
        float dy = static_cast<float>(where.y - lastDragPoint_.y) * sensitivity;
        float dxCurve = dx / cW;
        float dyCurve = -dy / cH;  // Y is inverted (pixel Y increases downward)

        lastDragPoint_ = where;

        if (dragTarget_ == DragTarget::Point && dragIndex_ >= 0 &&
            dragIndex_ < static_cast<int>(data_.points.size())) {
            auto& pt = data_.points[static_cast<size_t>(dragIndex_)];

            // Endpoints: only allow Y movement
            if (dragIndex_ == 0 || dragIndex_ == static_cast<int>(data_.points.size()) - 1) {
                pt.y = std::clamp(pt.y + dyCurve, 0.0f, 1.0f);
                // Move handles with the point
                pt.cpLeftY = std::clamp(pt.cpLeftY + dyCurve, 0.0f, 1.0f);
                pt.cpRightY = std::clamp(pt.cpRightY + dyCurve, 0.0f, 1.0f);
            } else {
                float oldX = pt.x;
                pt.x = std::clamp(pt.x + dxCurve, 0.01f, 0.99f);
                pt.y = std::clamp(pt.y + dyCurve, 0.0f, 1.0f);
                float xShift = pt.x - oldX;
                pt.cpLeftX = std::clamp(pt.cpLeftX + xShift, 0.0f, 1.0f);
                pt.cpLeftY = std::clamp(pt.cpLeftY + dyCurve, 0.0f, 1.0f);
                pt.cpRightX = std::clamp(pt.cpRightX + xShift, 0.0f, 1.0f);
                pt.cpRightY = std::clamp(pt.cpRightY + dyCurve, 0.0f, 1.0f);
            }

            data_.presetIndex = -1;
            notifyCurveChanged();
            invalid();
        }
        else if (dragTarget_ == DragTarget::Handle && dragIndex_ >= 0 &&
                 dragIndex_ < static_cast<int>(data_.points.size())) {
            auto& pt = data_.points[static_cast<size_t>(dragIndex_)];
            if (dragHandleSide_ == 0) {
                // Left handle
                pt.cpLeftX = std::clamp(pt.cpLeftX + dxCurve, 0.0f, pt.x);
                pt.cpLeftY = std::clamp(pt.cpLeftY + dyCurve, 0.0f, 1.0f);
            } else {
                // Right handle
                pt.cpRightX = std::clamp(pt.cpRightX + dxCurve, pt.x, 1.0f);
                pt.cpRightY = std::clamp(pt.cpRightY + dyCurve, 0.0f, 1.0f);
            }

            data_.presetIndex = -1;
            notifyCurveChanged();
            invalid();
        }

        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseUp(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        (void)where; (void)buttons;
        if (isDragging_) {
            isDragging_ = false;
            dragTarget_ = DragTarget::None;
            data_.sortPoints();
            notifyCurveChanged();
            notifyCurveCommitted();
            invalid();
            return VSTGUI::kMouseEventHandled;
        }
        return VSTGUI::kMouseEventNotHandled;
    }

    VSTGUI::CMouseEventResult onMouseCancel() override {
        isDragging_ = false;
        dragTarget_ = DragTarget::None;
        return VSTGUI::kMouseEventHandled;
    }

    int32_t onKeyDown(VstKeyCode& keyCode) override {
        // Delete or Backspace: remove selected point (endpoints are protected)
        if (keyCode.virt == VKEY_DELETE ||
            keyCode.virt == VKEY_BACK) {
            if (selectedPointIndex_ > 0 &&
                selectedPointIndex_ < static_cast<int>(data_.points.size()) - 1) {
                data_.points.erase(data_.points.begin() + selectedPointIndex_);
                data_.presetIndex = -1;
                selectedPointIndex_ = -1;
                notifyCurveChanged();
                notifyCurveCommitted();
                invalid();
                return 1;  // handled
            }
        }
        return -1;  // not handled
    }

    CLASS_METHODS(SpeedCurveEditor, CView)

private:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kPadding = 4.0f;
    static constexpr float kPointDrawRadius = 4.0f;
    static constexpr float kPointHitRadius = 10.0f;
    static constexpr float kHandleDrawSize = 3.0f;
    static constexpr float kHandleHitRadius = 8.0f;

    // =========================================================================
    // Drag State
    // =========================================================================

    enum class DragTarget { None, Point, Handle };

    bool isDragging_ = false;
    DragTarget dragTarget_ = DragTarget::None;
    int dragIndex_ = -1;
    int dragHandleSide_ = 0;  // 0 = left, 1 = right
    int selectedPointIndex_ = -1;  ///< Currently selected point (-1 = none)
    VSTGUI::CPoint lastDragPoint_;

    // =========================================================================
    // Data
    // =========================================================================

    SpeedCurveData data_;
    CurveChangedCallback curveChangedCallback_;
    CurveCommittedCallback curveCommittedCallback_;

    // =========================================================================
    // Coordinate Conversion
    // =========================================================================

    static VSTGUI::CPoint curveToPixel(float cx, float cy,
                                        float left, float bottom,
                                        float width, float height) {
        return {left + cx * width, bottom - cy * height};
    }

    static float pixelToCurveX(float px, float left, float width) {
        return (px - left) / width;
    }

    static float pixelToCurveY(float py, float bottom, float height) {
        return (bottom - py) / height;
    }

    // =========================================================================
    // Hit Testing
    // =========================================================================

    /// Returns index of hit control point, or -1.
    int hitTestPoint(const VSTGUI::CPoint& where,
                     float cL, float cB, float cW, float cH) const {
        for (size_t i = 0; i < data_.points.size(); ++i) {
            auto pos = curveToPixel(data_.points[i].x, data_.points[i].y,
                                    cL, cB, cW, cH);
            float dx = static_cast<float>(where.x) - static_cast<float>(pos.x);
            float dy = static_cast<float>(where.y) - static_cast<float>(pos.y);
            if (dx * dx + dy * dy <= kPointHitRadius * kPointHitRadius)
                return static_cast<int>(i);
        }
        return -1;
    }

    /// Returns true if a bezier handle was hit. Sets seg and side (0=left, 1=right).
    bool hitTestHandle(const VSTGUI::CPoint& where,
                       float cL, float cB, float cW, float cH,
                       int& seg, int& side) const {
        for (size_t i = 0; i < data_.points.size(); ++i) {
            const auto& pt = data_.points[i];
            // Right handle
            if (i < data_.points.size() - 1) {
                auto hPos = curveToPixel(pt.cpRightX, pt.cpRightY, cL, cB, cW, cH);
                float dx = static_cast<float>(where.x - hPos.x);
                float dy = static_cast<float>(where.y - hPos.y);
                if (dx * dx + dy * dy <= kHandleHitRadius * kHandleHitRadius) {
                    seg = static_cast<int>(i);
                    side = 1;
                    return true;
                }
            }
            // Left handle
            if (i > 0) {
                auto hPos = curveToPixel(pt.cpLeftX, pt.cpLeftY, cL, cB, cW, cH);
                float dx = static_cast<float>(where.x - hPos.x);
                float dy = static_cast<float>(where.y - hPos.y);
                if (dx * dx + dy * dy <= kHandleHitRadius * kHandleHitRadius) {
                    seg = static_cast<int>(i);
                    side = 0;
                    return true;
                }
            }
        }
        return false;
    }

    // =========================================================================
    // Drawing Helpers
    // =========================================================================

    void drawDiamond(VSTGUI::CDrawContext* ctx, VSTGUI::CPoint center, float size) {
        ctx->setFillColor(VSTGUI::CColor(200, 200, 100, 200));
        auto path = VSTGUI::owned(ctx->createGraphicsPath());
        if (!path) return;
        path->beginSubpath(VSTGUI::CPoint(center.x, center.y - size));
        path->addLine(VSTGUI::CPoint(center.x + size, center.y));
        path->addLine(VSTGUI::CPoint(center.x, center.y + size));
        path->addLine(VSTGUI::CPoint(center.x - size, center.y));
        path->closeSubpath();
        ctx->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
        ctx->setFrameColor(VSTGUI::CColor(255, 255, 255, 150));
        ctx->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
    }

    void drawCurveFill(VSTGUI::CDrawContext* ctx,
                       const std::array<float, 256>& table,
                       float cL, float cT, float cW, float cH, float cB) {
        if (data_.points.size() < 2) return;

        auto path = VSTGUI::owned(ctx->createGraphicsPath());
        if (!path) return;

        float centerPixelY = cB - 0.5f * cH;

        // Start at center line
        path->beginSubpath(VSTGUI::CPoint(cL, centerPixelY));

        // Trace the curve
        for (size_t i = 0; i < 256; ++i) {
            float x = cL + (static_cast<float>(i) / 255.0f) * cW;
            float y = cB - table[i] * cH;
            path->addLine(VSTGUI::CPoint(x, y));
        }

        // Close back to center
        path->addLine(VSTGUI::CPoint(cL + cW, centerPixelY));
        path->closeSubpath();

        ctx->setFillColor(VSTGUI::CColor(220, 180, 100, 35));
        ctx->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
    }

    void drawCurveStroke(VSTGUI::CDrawContext* ctx,
                         const std::array<float, 256>& table,
                         float cL, float cT, float cW, float cH, float cB) {
        (void)cT;
        if (data_.points.size() < 2) return;

        auto path = VSTGUI::owned(ctx->createGraphicsPath());
        if (!path) return;

        float x0 = cL;
        float y0 = cB - table[0] * cH;
        path->beginSubpath(VSTGUI::CPoint(x0, y0));

        for (size_t i = 1; i < 256; ++i) {
            float x = cL + (static_cast<float>(i) / 255.0f) * cW;
            float y = cB - table[i] * cH;
            path->addLine(VSTGUI::CPoint(x, y));
        }

        ctx->setLineWidth(2.0);
        ctx->setFrameColor(VSTGUI::CColor(220, 180, 100, 200));
        ctx->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
    }

    // =========================================================================
    // Notification
    // =========================================================================

    void notifyCurveChanged() {
        if (curveChangedCallback_)
            curveChangedCallback_(data_);
    }

    void notifyCurveCommitted() {
        if (curveCommittedCallback_)
            curveCommittedCallback_(data_);
    }
};

} // namespace Gradus
