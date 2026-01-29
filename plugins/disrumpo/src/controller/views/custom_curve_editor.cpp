// ==============================================================================
// CustomCurveEditor Implementation
// ==============================================================================
// FR-039a, FR-039b, FR-039c: Interactive breakpoint curve editor

#include "custom_curve_editor.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>

namespace Disrumpo {

// ==============================================================================
// Constructor
// ==============================================================================

CustomCurveEditor::CustomCurveEditor(const VSTGUI::CRect& size,
                                     VSTGUI::IControlListener* listener,
                                     int32_t tag)
    : CControl(size, listener, tag)
{
    // Initialize with default linear curve (2 endpoints)
    breakpoints_[0] = {0.0f, 0.0f};
    breakpoints_[1] = {1.0f, 1.0f};
}

// ==============================================================================
// Data API
// ==============================================================================

void CustomCurveEditor::setBreakpoints(
    const std::array<std::pair<float, float>, 8>& points, int count) {
    breakpointCount_ = std::clamp(count, 2, 8);
    for (int i = 0; i < breakpointCount_; ++i) {
        breakpoints_[static_cast<size_t>(i)] = points[static_cast<size_t>(i)];
    }
    setDirty();
}

std::pair<float, float> CustomCurveEditor::getBreakpoint(int index) const {
    if (index >= 0 && index < breakpointCount_) {
        return breakpoints_[static_cast<size_t>(index)];
    }
    return {0.0f, 0.0f};
}

// ==============================================================================
// Drawing
// ==============================================================================

void CustomCurveEditor::draw(VSTGUI::CDrawContext* context) {
    drawBackground(context);
    drawGrid(context);
    drawCurve(context);
    drawControlPoints(context);
    setDirty(false);
}

void CustomCurveEditor::drawBackground(VSTGUI::CDrawContext* context) {
    auto rect = getViewSize();

    // Dark background
    context->setFillColor(VSTGUI::CColor(0x12, 0x12, 0x16, 0xFF));
    context->drawRect(rect, VSTGUI::kDrawFilled);

    // Border
    context->setFrameColor(VSTGUI::CColor(0x3A, 0x3A, 0x40, 0xFF));
    context->setLineWidth(1.0);
    context->drawRect(rect, VSTGUI::kDrawStroked);
}

void CustomCurveEditor::drawGrid(VSTGUI::CDrawContext* context) {
    auto rect = getViewSize();

    context->setFrameColor(VSTGUI::CColor(0x2A, 0x2A, 0x30, 0xFF));
    context->setLineWidth(0.5);

    // Draw grid lines at 0.25 intervals
    for (int i = 1; i < 4; ++i) {
        float t = static_cast<float>(i) / 4.0f;
        float px = 0.0f;
        float py = 0.0f;

        // Vertical line
        normalizedToPixel(t, 0.0f, px, py);
        float topY = 0.0f;
        normalizedToPixel(t, 1.0f, px, topY);
        context->drawLine(
            VSTGUI::CPoint(px, topY),
            VSTGUI::CPoint(px, py));

        // Horizontal line
        normalizedToPixel(0.0f, t, px, py);
        float rightX = 0.0f;
        float rightY = 0.0f;
        normalizedToPixel(1.0f, t, rightX, rightY);
        context->drawLine(
            VSTGUI::CPoint(px, py),
            VSTGUI::CPoint(rightX, py));
    }

    // Draw diagonal reference line (y = x)
    context->setFrameColor(VSTGUI::CColor(0x40, 0x40, 0x48, 0xFF));
    context->setLineWidth(1.0);
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    normalizedToPixel(0.0f, 0.0f, x0, y0);
    normalizedToPixel(1.0f, 1.0f, x1, y1);
    context->drawLine(VSTGUI::CPoint(x0, y0), VSTGUI::CPoint(x1, y1));
}

void CustomCurveEditor::drawCurve(VSTGUI::CDrawContext* context) {
    if (breakpointCount_ < 2) {
        return;
    }

    auto path = VSTGUI::owned(context->createGraphicsPath());
    if (!path) {
        return;
    }

    // Start at first point
    float startPx = 0.0f;
    float startPy = 0.0f;
    normalizedToPixel(breakpoints_[0].first, breakpoints_[0].second, startPx, startPy);
    path->beginSubpath(VSTGUI::CPoint(startPx, startPy));

    // Connect through all breakpoints
    for (int i = 1; i < breakpointCount_; ++i) {
        float px = 0.0f;
        float py = 0.0f;
        normalizedToPixel(breakpoints_[static_cast<size_t>(i)].first,
                          breakpoints_[static_cast<size_t>(i)].second,
                          px, py);
        path->addLine(VSTGUI::CPoint(px, py));
    }

    // Draw curve stroke
    context->setFrameColor(VSTGUI::CColor(0x4E, 0xCD, 0xC4, 0xFF));  // accent-secondary
    context->setLineWidth(2.0);
    context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
}

void CustomCurveEditor::drawControlPoints(VSTGUI::CDrawContext* context) {
    for (int i = 0; i < breakpointCount_; ++i) {
        float px = 0.0f;
        float py = 0.0f;
        normalizedToPixel(breakpoints_[static_cast<size_t>(i)].first,
                          breakpoints_[static_cast<size_t>(i)].second,
                          px, py);

        // Highlight dragged point
        bool isSelected = (isDragging_ && dragIndex_ == i);

        // Filled circle
        VSTGUI::CColor fillColor = isSelected
            ? VSTGUI::CColor(0xFF, 0xFF, 0xFF, 0xFF)       // White when dragging
            : VSTGUI::CColor(0x4E, 0xCD, 0xC4, 0xFF);      // accent-secondary

        VSTGUI::CRect pointRect(
            px - kPointRadius, py - kPointRadius,
            px + kPointRadius, py + kPointRadius);

        context->setFillColor(fillColor);
        context->drawEllipse(pointRect, VSTGUI::kDrawFilled);

        // Outline
        context->setFrameColor(VSTGUI::CColor(0xFF, 0xFF, 0xFF, 0xC0));
        context->setLineWidth(1.5);
        context->drawEllipse(pointRect, VSTGUI::kDrawStroked);
    }
}

// ==============================================================================
// Mouse Interaction
// ==============================================================================

void CustomCurveEditor::onMouseDownEvent(VSTGUI::MouseDownEvent& event) {
    auto viewSize = getViewSize();
    float localX = static_cast<float>(event.mousePosition.x);
    float localY = static_cast<float>(event.mousePosition.y);

    if (event.buttonState.isRight()) {
        // Right-click: Remove breakpoint (if not endpoint and count > 2)
        int hitIdx = hitTestPoint(localX, localY);
        if (hitIdx > 0 && hitIdx < breakpointCount_ - 1 && breakpointCount_ > 2) {
            if (onRemove_) {
                onRemove_(hitIdx);
            }
            event.consumed = true;
        }
        return;
    }

    if (event.buttonState.isLeft()) {
        int hitIdx = hitTestPoint(localX, localY);
        if (hitIdx >= 0) {
            // Start dragging existing point
            dragIndex_ = hitIdx;
            isDragging_ = true;
            beginEdit();
            event.consumed = true;
        } else if (breakpointCount_ < 8) {
            // Add new breakpoint at clicked position
            float nx = 0.0f;
            float ny = 0.0f;
            pixelToNormalized(localX, localY, nx, ny);
            nx = std::clamp(nx, 0.01f, 0.99f);
            ny = std::clamp(ny, 0.0f, 1.0f);

            if (onAdd_) {
                onAdd_(nx, ny);
            }
            event.consumed = true;
        }
    }
}

void CustomCurveEditor::onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) {
    if (!isDragging_ || dragIndex_ < 0) {
        return;
    }

    float localX = static_cast<float>(event.mousePosition.x);
    float localY = static_cast<float>(event.mousePosition.y);

    float nx = 0.0f;
    float ny = 0.0f;
    pixelToNormalized(localX, localY, nx, ny);

    // Clamp Y to [0, 1]
    ny = std::clamp(ny, 0.0f, 1.0f);

    // Endpoints: X is fixed at 0.0 or 1.0
    if (dragIndex_ == 0) {
        nx = 0.0f;
    } else if (dragIndex_ == breakpointCount_ - 1) {
        nx = 1.0f;
    } else {
        // Interior points: constrain X between neighbors
        float leftX = breakpoints_[static_cast<size_t>(dragIndex_) - 1].first + 0.01f;
        float rightX = breakpoints_[static_cast<size_t>(dragIndex_) + 1].first - 0.01f;
        nx = std::clamp(nx, leftX, rightX);
    }

    breakpoints_[static_cast<size_t>(dragIndex_)] = {nx, ny};

    if (onChange_) {
        onChange_(dragIndex_, nx, ny);
    }

    setDirty();
    event.consumed = true;
}

void CustomCurveEditor::onMouseUpEvent(VSTGUI::MouseUpEvent& event) {
    if (isDragging_) {
        isDragging_ = false;
        dragIndex_ = -1;
        endEdit();
        setDirty();
        event.consumed = true;
    }
}

// ==============================================================================
// Coordinate Conversion
// ==============================================================================

void CustomCurveEditor::normalizedToPixel(float nx, float ny,
                                           float& px, float& py) const {
    auto rect = getViewSize();
    float innerLeft = static_cast<float>(rect.left) + kPadding;
    float innerTop = static_cast<float>(rect.top) + kPadding;
    float innerWidth = static_cast<float>(rect.getWidth()) - 2 * kPadding;
    float innerHeight = static_cast<float>(rect.getHeight()) - 2 * kPadding;

    px = innerLeft + nx * innerWidth;
    py = innerTop + (1.0f - ny) * innerHeight;  // Y inverted (0 at bottom)
}

void CustomCurveEditor::pixelToNormalized(float px, float py,
                                           float& nx, float& ny) const {
    auto rect = getViewSize();
    float innerLeft = static_cast<float>(rect.left) + kPadding;
    float innerTop = static_cast<float>(rect.top) + kPadding;
    float innerWidth = static_cast<float>(rect.getWidth()) - 2 * kPadding;
    float innerHeight = static_cast<float>(rect.getHeight()) - 2 * kPadding;

    nx = (px - innerLeft) / innerWidth;
    ny = 1.0f - (py - innerTop) / innerHeight;  // Y inverted
}

int CustomCurveEditor::hitTestPoint(float pixelX, float pixelY) const {
    for (int i = 0; i < breakpointCount_; ++i) {
        float px = 0.0f;
        float py = 0.0f;
        normalizedToPixel(breakpoints_[static_cast<size_t>(i)].first,
                          breakpoints_[static_cast<size_t>(i)].second,
                          px, py);

        float dx = pixelX - px;
        float dy = pixelY - py;
        if (dx * dx + dy * dy <= kHitRadius * kHitRadius) {
            return i;
        }
    }
    return -1;
}

} // namespace Disrumpo
