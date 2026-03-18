#pragma once

// ==============================================================================
// Mod Display Utilities - Shared drawing helpers & types for mod visualizers
// ==============================================================================
// Free functions for common drawing operations (backgrounds, gridlines, dots,
// scrolling time-series, XY trail plots) plus shared utility types (Xorshift32
// PRNG, RingBufferView).
// ==============================================================================

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/ccolor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Krate::Plugins {

// ==============================================================================
// Xorshift32 PRNG (shared by Rungler, S&H, Random displays)
// ==============================================================================

struct Xorshift32 {
    uint32_t state = 1;

    void seed(uint32_t s) noexcept { state = s ? s : 1; }

    uint32_t next() noexcept {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /// Return bipolar float in [-1, +1]
    float nextFloat() noexcept {
        return static_cast<float>(static_cast<int32_t>(next())) / 2147483648.0f;
    }
};

// ==============================================================================
// Ring Buffer View (non-owning read access for drawing helpers)
// ==============================================================================

struct RingBufferView {
    const float* data;
    int head;
    int count;
    int capacity;

    /// Access element i (0 = oldest, count-1 = newest)
    [[nodiscard]] float at(int i) const noexcept {
        int idx = (head - count + i + capacity) % capacity;
        return data[idx];
    }
};

// ==============================================================================
// Drawing Helpers
// ==============================================================================

/// Draw the standard rounded-rect background with border used by all mod displays.
/// Colors: fill (22,22,26), border (60,60,65), radius 6.0
inline void drawRoundedBackground(VSTGUI::CDrawContext* context,
                                   const VSTGUI::CRect& rect) {
    constexpr double kBorderRadius = 6.0;
    auto* path = context->createGraphicsPath();
    if (!path) return;
    path->addRoundRect(rect, kBorderRadius);
    context->setFillColor(VSTGUI::CColor(22, 22, 26, 255));
    context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
    context->setFrameColor(VSTGUI::CColor(60, 60, 65, 255));
    context->setLineWidth(1.0);
    context->setLineStyle(VSTGUI::kLineSolid);
    context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
    path->forget();
}

/// Draw a sub-panel background (darker inset area within a display).
/// Color: fill (16,16,20), configurable corner radius.
inline void drawSubPanel(VSTGUI::CDrawContext* context,
                          const VSTGUI::CRect& rect,
                          double cornerRadius = 4.0) {
    auto* path = context->createGraphicsPath();
    if (!path) return;
    path->addRoundRect(rect, cornerRadius);
    context->setFillColor(VSTGUI::CColor(16, 16, 20, 255));
    context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
    path->forget();
}

/// Draw a dashed reference line (used for zero lines, gridlines).
inline void drawDashedLine(VSTGUI::CDrawContext* context,
                            const VSTGUI::CPoint& p1,
                            const VSTGUI::CPoint& p2,
                            const VSTGUI::CColor& color,
                            double lineWidth = 1.0) {
    const VSTGUI::CLineStyle::CoordVector dashes = {4.0, 4.0};
    VSTGUI::CLineStyle dashedStyle(
        VSTGUI::CLineStyle::kLineCapButt,
        VSTGUI::CLineStyle::kLineJoinMiter,
        0.0, dashes);
    context->setFrameColor(color);
    context->setLineWidth(lineWidth);
    context->setLineStyle(dashedStyle);
    context->drawLine(p1, p2);
}

/// Draw crosshair gridlines in a sub-panel (used by XY plots).
inline void drawCrosshairGrid(VSTGUI::CDrawContext* context,
                               double left, double top, double w, double h) {
    VSTGUI::CColor gridColor(160, 160, 165, 30);
    double cx = left + w * 0.5;
    double cy = top + h * 0.5;
    drawDashedLine(context, {left + 4, cy}, {left + w - 4, cy}, gridColor);
    drawDashedLine(context, {cx, top + 4}, {cx, top + h - 4}, gridColor);
}

/// Draw a 3-layer glow dot (outer glow + colored dot + bright center).
/// Used at current position in XY plots and LFO cursor.
inline void drawGlowDot(VSTGUI::CDrawContext* context,
                          double x, double y,
                          const VSTGUI::CColor& color,
                          double glowRadius = 6.0,
                          double dotRadius = 3.5,
                          double centerRadius = 1.5,
                          uint8_t glowAlpha = 50,
                          uint8_t centerAlpha = 200) {
    VSTGUI::CRect glowRect(x - glowRadius, y - glowRadius,
                             x + glowRadius, y + glowRadius);
    context->setFillColor(VSTGUI::CColor(color.red, color.green, color.blue, glowAlpha));
    context->drawEllipse(glowRect, VSTGUI::kDrawFilled);

    VSTGUI::CRect dotRect(x - dotRadius, y - dotRadius,
                            x + dotRadius, y + dotRadius);
    context->setFillColor(color);
    context->drawEllipse(dotRect, VSTGUI::kDrawFilled);

    VSTGUI::CRect centerRect(x - centerRadius, y - centerRadius,
                               x + centerRadius, y + centerRadius);
    context->setFillColor(VSTGUI::CColor(255, 255, 255, centerAlpha));
    context->drawEllipse(centerRect, VSTGUI::kDrawFilled);
}

/// Draw a 2-layer value dot (colored dot + white center).
/// Used at the newest point in scrolling time series.
inline void drawValueDot(VSTGUI::CDrawContext* context,
                          double x, double y,
                          const VSTGUI::CColor& color,
                          double dotRadius = 4.0,
                          double centerRadius = 2.0) {
    VSTGUI::CRect dotRect(x - dotRadius, y - dotRadius,
                            x + dotRadius, y + dotRadius);
    context->setFillColor(color);
    context->drawEllipse(dotRect, VSTGUI::kDrawFilled);

    VSTGUI::CRect centerRect(x - centerRadius, y - centerRadius,
                               x + centerRadius, y + centerRadius);
    context->setFillColor(VSTGUI::CColor(255, 255, 255, 220));
    context->drawEllipse(centerRect, VSTGUI::kDrawFilled);
}

/// Draw a scrolling time-series curve with filled area and value dot.
/// Maps values from bipolar [-1,+1] to the plot area. Draws fill from
/// zeroY baseline, stroke line, and a value dot at the newest point.
inline void drawTimeSeriesCurve(
    VSTGUI::CDrawContext* context,
    double plotLeft, double plotTop, double plotW, double plotH,
    const RingBufferView& history,
    const VSTGUI::CColor& color,
    double zeroY,
    uint8_t fillAlpha = 25) {

    if (history.count < 2) return;
    int count = history.count;

    auto mapX = [&](int i) -> double {
        return plotLeft + (static_cast<double>(i) / (count - 1)) * plotW;
    };
    auto mapY = [&](float v) -> double {
        return plotTop + (1.0 - (static_cast<double>(v) + 1.0) * 0.5) * plotH;
    };

    auto* fillPath = context->createGraphicsPath();
    auto* strokePath = context->createGraphicsPath();
    if (!fillPath || !strokePath) {
        if (fillPath) fillPath->forget();
        if (strokePath) strokePath->forget();
        return;
    }

    float firstVal = history.at(0);
    fillPath->beginSubpath(VSTGUI::CPoint(mapX(0), zeroY));
    fillPath->addLine(VSTGUI::CPoint(mapX(0), mapY(firstVal)));
    strokePath->beginSubpath(VSTGUI::CPoint(mapX(0), mapY(firstVal)));

    for (int i = 1; i < count; ++i) {
        VSTGUI::CPoint pt(mapX(i), mapY(history.at(i)));
        fillPath->addLine(pt);
        strokePath->addLine(pt);
    }

    fillPath->addLine(VSTGUI::CPoint(mapX(count - 1), zeroY));
    fillPath->closeSubpath();

    context->setFillColor(VSTGUI::CColor(color.red, color.green, color.blue, fillAlpha));
    context->drawGraphicsPath(fillPath, VSTGUI::CDrawContext::kPathFilled);
    fillPath->forget();

    context->setFrameColor(color);
    context->setLineWidth(1.5);
    context->setLineStyle(VSTGUI::CLineStyle(
        VSTGUI::CLineStyle::kLineCapRound,
        VSTGUI::CLineStyle::kLineJoinRound));
    context->drawGraphicsPath(strokePath, VSTGUI::CDrawContext::kPathStroked);
    strokePath->forget();

    float newestVal = history.at(count - 1);
    drawValueDot(context, mapX(count - 1), mapY(newestVal), color);
}

/// Draw an XY trail plot with fading opacity segments and a glow dot
/// at the current position. Both trailX and trailY must share the same
/// head/count/capacity values.
inline void drawXYTrail(
    VSTGUI::CDrawContext* context,
    double plotLeft, double plotTop, double plotW, double plotH,
    const RingBufferView& trailX,
    const RingBufferView& trailY,
    const VSTGUI::CColor& color) {

    int count = trailX.count;
    if (count < 2) return;

    auto mapX = [&](float v) -> double {
        return plotLeft + (static_cast<double>(v) + 1.0) * 0.5 * plotW;
    };
    auto mapY = [&](float v) -> double {
        return plotTop + (1.0 - (static_cast<double>(v) + 1.0) * 0.5) * plotH;
    };

    context->setLineWidth(1.5);
    context->setLineStyle(VSTGUI::CLineStyle(
        VSTGUI::CLineStyle::kLineCapRound,
        VSTGUI::CLineStyle::kLineJoinRound));

    for (int i = 1; i < count; ++i) {
        float age = static_cast<float>(i) / static_cast<float>(count);
        auto alpha = static_cast<uint8_t>(age * age * 200); // quadratic fade-in

        VSTGUI::CColor segColor(color.red, color.green, color.blue, alpha);
        context->setFrameColor(segColor);
        context->drawLine(
            VSTGUI::CPoint(mapX(trailX.at(i - 1)), mapY(trailY.at(i - 1))),
            VSTGUI::CPoint(mapX(trailX.at(i)), mapY(trailY.at(i))));
    }

    float newestX = trailX.at(count - 1);
    float newestY = trailY.at(count - 1);
    drawGlowDot(context, mapX(newestX), mapY(newestY), color);
}

} // namespace Krate::Plugins
