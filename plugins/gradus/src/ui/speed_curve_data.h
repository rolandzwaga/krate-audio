// ==============================================================================
// Speed Curve Data Model
// ==============================================================================
// Per-lane speed curve represented as a chain of cubic Bezier segments.
// The curve modulates the lane's speed multiplier over one loop cycle:
//   - X-axis: normalized loop position [0, 1]
//   - Y-axis: speed offset factor [0, 1] where 0.5 = center (no offset)
//
// The curve is baked into a 256-entry lookup table for real-time-safe
// evaluation on the audio thread. Only the baked table crosses the
// controller→processor boundary (via IMessage).
// ==============================================================================

#pragma once

#include <krate/dsp/core/curve_table.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Gradus {

// =============================================================================
// Speed Curve Point
// =============================================================================

/// A single control point on the speed curve with Bezier handles.
/// Points are sorted by x-position; the first and last points are fixed
/// at x=0 and x=1 respectively.
struct SpeedCurvePoint {
    float x = 0.0f;         ///< Position along loop [0, 1]
    float y = 0.5f;          ///< Speed offset factor [0, 1] (0.5 = center)
    float cpLeftX  = 0.0f;   ///< Left Bezier handle X (absolute)
    float cpLeftY  = 0.5f;   ///< Left Bezier handle Y (absolute)
    float cpRightX = 0.0f;   ///< Right Bezier handle X (absolute)
    float cpRightY = 0.5f;   ///< Right Bezier handle Y (absolute)
};

// =============================================================================
// Speed Curve Data
// =============================================================================

/// Per-lane speed curve data: variable-count control points + bake-to-table.
struct SpeedCurveData {
    bool enabled = false;
    int presetIndex = 0;  ///< 0 = Flat (default), -1 = Custom
    std::vector<SpeedCurvePoint> points;

    /// Initialize with a flat curve (two endpoints at y=0.5).
    SpeedCurveData() { resetToFlat(); }

    /// Reset to a flat line (no speed modulation).
    void resetToFlat() {
        points.clear();
        SpeedCurvePoint p0;
        p0.x = 0.0f; p0.y = 0.5f;
        p0.cpLeftX = 0.0f; p0.cpLeftY = 0.5f;
        p0.cpRightX = 0.33f; p0.cpRightY = 0.5f;

        SpeedCurvePoint p1;
        p1.x = 1.0f; p1.y = 0.5f;
        p1.cpLeftX = 0.67f; p1.cpLeftY = 0.5f;
        p1.cpRightX = 1.0f; p1.cpRightY = 0.5f;

        points.push_back(p0);
        points.push_back(p1);
    }

    /// Sort points by x-position (maintaining first/last endpoint constraints).
    void sortPoints() {
        if (points.size() < 2) return;
        // Keep endpoints fixed, sort interior points
        std::sort(points.begin() + 1, points.end() - 1,
            [](const SpeedCurvePoint& a, const SpeedCurvePoint& b) {
                return a.x < b.x;
            });
        // Enforce endpoint constraints
        points.front().x = 0.0f;
        points.back().x = 1.0f;
    }

    /// Bake the curve into a 256-entry lookup table.
    ///
    /// Each consecutive pair of points forms a cubic Bezier segment:
    ///   P0 = current point position
    ///   P1 = current point's right handle
    ///   P2 = next point's left handle
    ///   P3 = next point position
    ///
    /// The per-segment tables are stitched into the final 256-entry table
    /// proportionally to each segment's x-span.
    void bakeToTable(std::array<float, Krate::DSP::kCurveTableSize>& table) const {
        if (points.size() < 2) {
            table.fill(0.5f);
            return;
        }

        // Generate a high-resolution intermediate curve, then resample to 256
        constexpr int kHiRes = 2048;
        std::array<float, kHiRes + 1> xSamples{};
        std::array<float, kHiRes + 1> ySamples{};

        int sampleIdx = 0;
        for (size_t seg = 0; seg + 1 < points.size(); ++seg) {
            const auto& p0 = points[seg];
            const auto& p3 = points[seg + 1];

            // Bezier control points (absolute coordinates)
            float bx0 = p0.x,       by0 = p0.y;
            float bx1 = p0.cpRightX, by1 = p0.cpRightY;
            float bx2 = p3.cpLeftX,  by2 = p3.cpLeftY;
            float bx3 = p3.x,       by3 = p3.y;

            // Number of samples proportional to segment x-span
            float segSpan = bx3 - bx0;
            int segSamples = std::max(4, static_cast<int>(segSpan * kHiRes));

            // Last segment must reach the end
            bool isLast = (seg + 2 == points.size());

            for (int i = 0; i < segSamples; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(segSamples);
                float u = 1.0f - t;
                float u2 = u * u;
                float u3 = u2 * u;
                float t2 = t * t;
                float t3 = t2 * t;

                float px = u3 * bx0 + 3.0f * u2 * t * bx1
                         + 3.0f * u * t2 * bx2 + t3 * bx3;
                float py = u3 * by0 + 3.0f * u2 * t * by1
                         + 3.0f * u * t2 * by2 + t3 * by3;

                if (sampleIdx <= kHiRes) {
                    xSamples[static_cast<size_t>(sampleIdx)] = px;
                    ySamples[static_cast<size_t>(sampleIdx)] = std::clamp(py, 0.0f, 1.0f);
                    ++sampleIdx;
                }
            }

            // Add final point of last segment
            if (isLast && sampleIdx <= kHiRes) {
                xSamples[static_cast<size_t>(sampleIdx)] = bx3;
                ySamples[static_cast<size_t>(sampleIdx)] = std::clamp(by3, 0.0f, 1.0f);
                ++sampleIdx;
            }
        }

        int totalSamples = std::min(sampleIdx, kHiRes + 1);

        // Resample to 256 uniform x values via linear interpolation
        int searchStart = 0;
        for (size_t i = 0; i < Krate::DSP::kCurveTableSize; ++i) {
            float targetX = static_cast<float>(i) / 255.0f;

            int j = searchStart;
            while (j < totalSamples - 1 &&
                   xSamples[static_cast<size_t>(j) + 1] < targetX) {
                ++j;
            }
            searchStart = j;

            if (j >= totalSamples - 1) {
                table[i] = ySamples[static_cast<size_t>(totalSamples - 1)];
            } else {
                float xSpan = xSamples[static_cast<size_t>(j) + 1]
                            - xSamples[static_cast<size_t>(j)];
                if (xSpan < 1e-8f) {
                    table[i] = ySamples[static_cast<size_t>(j)];
                } else {
                    float frac = (targetX - xSamples[static_cast<size_t>(j)]) / xSpan;
                    table[i] = ySamples[static_cast<size_t>(j)]
                             + frac * (ySamples[static_cast<size_t>(j) + 1]
                                     - ySamples[static_cast<size_t>(j)]);
                }
            }
        }
    }
};

} // namespace Gradus
