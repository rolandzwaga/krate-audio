#pragma once

// ==============================================================================
// EuclideanCenter — Dot Ring Display for Center of Circular UI
// ==============================================================================
// Draws a Euclidean rhythm pattern as a ring of dots in the center of the
// concentric ring display. Hits are filled, rests are hollow.
// ==============================================================================

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"

#include <cmath>
#include <vector>

namespace Gradus {

class EuclideanCenter {
public:
    void setEnabled(bool enabled) { enabled_ = enabled; }
    void setHits(int hits) { hits_ = hits; }
    void setSteps(int steps) { steps_ = steps; }
    void setRotation(int rotation) { rotation_ = rotation; }

    /// Draw the Euclidean dot ring centered at (cx, cy) with given radius.
    void draw(VSTGUI::CDrawContext* context,
              float cx, float cy, float radius) const
    {
        if (!enabled_ || steps_ <= 0) return;

        // Generate Euclidean pattern using Bjorklund algorithm
        auto pattern = generateEuclidean(hits_, steps_);

        // Apply rotation
        if (rotation_ > 0 && rotation_ < steps_) {
            std::vector<bool> rotated(steps_);
            for (int i = 0; i < steps_; ++i) {
                rotated[i] = pattern[(i + rotation_) % steps_];
            }
            pattern = std::move(rotated);
        }

        const float dotRadius = 4.0f;
        const float angleStep = 6.28318530718f / static_cast<float>(steps_);
        const float startAngle = -3.14159265359f / 2.0f; // 12 o'clock

        for (int i = 0; i < steps_; ++i) {
            float angle = startAngle + static_cast<float>(i) * angleStep;
            float dx = cx + radius * std::cos(angle);
            float dy = cy + radius * std::sin(angle);

            VSTGUI::CRect dotRect(
                dx - dotRadius, dy - dotRadius,
                dx + dotRadius, dy + dotRadius);

            if (pattern[i]) {
                // Hit: filled dot
                context->setFillColor(hitColor_);
                context->drawEllipse(dotRect, VSTGUI::kDrawFilled);
            } else {
                // Rest: hollow dot
                context->setFrameColor(restColor_);
                context->setLineWidth(1.5f);
                context->drawEllipse(dotRect, VSTGUI::kDrawStroked);
            }
        }

        // Draw connecting lines between hits for visual flow
        if (hits_ >= 2) {
            context->setFrameColor(lineColor_);
            context->setLineWidth(1.0f);

            float prevX = 0.0f, prevY = 0.0f;
            bool havePrev = false;
            float firstX = 0.0f, firstY = 0.0f;
            bool haveFirst = false;

            for (int i = 0; i < steps_; ++i) {
                if (!pattern[i]) continue;

                float angle = startAngle + static_cast<float>(i) * angleStep;
                float dx = cx + radius * std::cos(angle);
                float dy = cy + radius * std::sin(angle);

                if (!haveFirst) {
                    firstX = dx;
                    firstY = dy;
                    haveFirst = true;
                }

                if (havePrev) {
                    context->drawLine(
                        VSTGUI::CPoint(prevX, prevY),
                        VSTGUI::CPoint(dx, dy));
                }

                prevX = dx;
                prevY = dy;
                havePrev = true;
            }

            // Close the polygon
            if (haveFirst && havePrev) {
                context->drawLine(
                    VSTGUI::CPoint(prevX, prevY),
                    VSTGUI::CPoint(firstX, firstY));
            }
        }
    }

private:
    /// Bjorklund algorithm for Euclidean rhythm generation.
    [[nodiscard]] static std::vector<bool> generateEuclidean(int hits, int steps)
    {
        if (steps <= 0) return {};
        if (hits <= 0) return std::vector<bool>(steps, false);
        if (hits >= steps) return std::vector<bool>(steps, true);

        // Bjorklund algorithm
        std::vector<std::vector<bool>> groups;
        groups.reserve(steps);
        for (int i = 0; i < steps; ++i) {
            groups.push_back({i < hits});
        }

        int remainder = steps - hits;
        int divisor = hits;

        while (remainder > 1) {
            int count = std::min(divisor, remainder);
            for (int i = 0; i < count; ++i) {
                auto& back = groups[divisor + i];
                groups[i].insert(groups[i].end(), back.begin(), back.end());
            }
            groups.erase(groups.begin() + divisor + count, groups.end());
            groups.erase(groups.begin() + divisor,
                         groups.begin() + divisor + count);
            // Recalculate
            int newSize = static_cast<int>(groups.size());
            remainder = newSize - count;
            if (remainder <= 0) break;
            divisor = count;
            remainder = newSize - divisor;
        }

        // Flatten
        std::vector<bool> result;
        result.reserve(steps);
        for (const auto& g : groups) {
            result.insert(result.end(), g.begin(), g.end());
        }

        return result;
    }

    bool enabled_ = false;
    int hits_ = 0;
    int steps_ = 16;
    int rotation_ = 0;

    VSTGUI::CColor hitColor_{0xD0, 0xA0, 0x50, 0xFF};   // warm gold
    VSTGUI::CColor restColor_{0x60, 0x60, 0x60, 0xA0};   // dim gray
    VSTGUI::CColor lineColor_{0xD0, 0xA0, 0x50, 0x40};   // faint gold
};

} // namespace Gradus
