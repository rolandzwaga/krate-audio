#pragma once

// ==============================================================================
// RingRenderer — Single CView Drawing All 4 Concentric Rings
// ==============================================================================
// Draws the circular step sequencer display: 4 rings combining 8 lanes,
// Euclidean center, and per-step colored playhead highlights.
//
// This is a read-only visualization in Phase 2. Interaction added in Phase 5.
// ==============================================================================

#include "ring_geometry.h"
#include "ring_data_bridge.h"
#include "euclidean_center.h"

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cframe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace Gradus {

// ==============================================================================
// Lane Color Definitions
// ==============================================================================

struct LaneColors {
    VSTGUI::CColor accent;     ///< Ring fill color
    VSTGUI::CColor highlight;  ///< Playhead highlight color
};

inline const std::array<LaneColors, 8>& getLaneColors()
{
    static const std::array<LaneColors, 8> colors = {{
        // Velocity
        {{0xD0, 0x84, 0x5C, 0xFF}, {0x00, 0xE0, 0xE0, 0xFF}},
        // Gate
        {{0xC8, 0xA4, 0x64, 0xFF}, {0xE0, 0xA0, 0x00, 0xFF}},
        // Pitch
        {{0x6C, 0xA8, 0xA0, 0xFF}, {0x00, 0xD0, 0x70, 0xFF}},
        // Modifier
        {{0xC0, 0x70, 0x7C, 0xFF}, {0xE0, 0x60, 0x60, 0xFF}},
        // Condition
        {{0x7C, 0x90, 0xB0, 0xFF}, {0xB0, 0x70, 0xE0, 0xFF}},
        // Ratchet
        {{0x98, 0x80, 0xB0, 0xFF}, {0xE0, 0x80, 0x00, 0xFF}},
        // Chord
        {{0xA8, 0x8C, 0xC8, 0xFF}, {0x40, 0x80, 0xE0, 0xFF}},
        // Inversion
        {{0x88, 0xA8, 0xC8, 0xFF}, {0x40, 0xB0, 0xB0, 0xFF}},
    }};
    return colors;
}

// ==============================================================================
// RingRenderer
// ==============================================================================

class RingRenderer : public VSTGUI::CView {
public:
    explicit RingRenderer(const VSTGUI::CRect& size)
        : CView(size)
    {
        setWantsFocus(false);
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    void setDataSource(IRingDataSource* source) { dataSource_ = source; }
    void setGeometry(const RingGeometry& geo) { geometry_ = geo; }
    RingGeometry& geometry() { return geometry_; }

    /// Set which lane is selected (highlighted ring emphasis). -1 = none.
    void setSelectedLane(int laneIndex) { selectedLane_ = laneIndex; }

    // =========================================================================
    // Selection callback (ring click → detail strip)
    // =========================================================================

    using LaneSelectedCallback = std::function<void(int laneIndex)>;
    void setLaneSelectedCallback(LaneSelectedCallback cb)
    {
        laneSelectedCallback_ = std::move(cb);
    }

    // =========================================================================
    // Edit Callbacks (wired by controller to beginEdit/performEdit/endEdit)
    // =========================================================================

    using ValueChangeCallback = std::function<void(int laneIndex, int step, float normalizedValue)>;
    using BeginEndEditCallback = std::function<void(int laneIndex, int step)>;

    void setValueChangeCallback(ValueChangeCallback cb) { valueChangeCallback_ = std::move(cb); }
    void setBeginEditCallback(BeginEndEditCallback cb) { beginEditCallback_ = std::move(cb); }
    void setEndEditCallback(BeginEndEditCallback cb) { endEditCallback_ = std::move(cb); }

    // =========================================================================
    // Mouse Interaction
    // =========================================================================

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        if (!(buttons & VSTGUI::kLButton) || !dataSource_)
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;

        float localX = static_cast<float>(where.x - getViewSize().left);
        float localY = static_cast<float>(where.y - getViewSize().top);
        auto hit = geometry_.hitTest(localX, localY);

        if (hit.subZone == SubZone::kNone || hit.isCenter)
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;

        int laneIdx = subZoneToLaneIndex(hit.subZone);
        if (laneIdx < 0) return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;

        // Select this lane in the detail strip
        if (laneSelectedCallback_) laneSelectedCallback_(laneIdx);

        if (isBarTypeLane(laneIdx)) {
            // Start radial drag for bar-type lanes
            isDragging_ = true;
            dragLane_ = laneIdx;
            dragStep_ = hit.stepIndex;
            dragSubZone_ = hit.subZone;
            dragRingIndex_ = hit.ringIndex;

            if (beginEditCallback_) beginEditCallback_(laneIdx, hit.stepIndex);

            // Set value from current radius
            float radius = geometry_.pointToRadius(localX, localY);
            float value = geometry_.radiusToNormalizedValue(
                hit.ringIndex, hit.subZone, radius);
            if (valueChangeCallback_)
                valueChangeCallback_(laneIdx, hit.stepIndex, value);

            invalid();
            return VSTGUI::kMouseEventHandled;
        } else {
            // Click-to-cycle for discrete lanes
            cycleDiscreteValue(laneIdx, hit.stepIndex);
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
        }
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        [[maybe_unused]] const VSTGUI::CButtonState& buttons) override
    {
        float localX = static_cast<float>(where.x - getViewSize().left);
        float localY = static_cast<float>(where.y - getViewSize().top);

        if (isDragging_ && dataSource_) {
            // Continue radial drag
            float radius = geometry_.pointToRadius(localX, localY);
            float value = geometry_.radiusToNormalizedValue(
                dragRingIndex_, dragSubZone_, radius);

            // Also allow painting adjacent steps by angle
            int totalSteps = dataSource_->getStepCount(dragLane_);
            float angle = geometry_.pointToAngle(localX, localY);
            int currentStep = RingGeometry::angleToStep(angle, totalSteps);

            if (currentStep != dragStep_) {
                // End edit on old step, begin on new
                if (endEditCallback_) endEditCallback_(dragLane_, dragStep_);
                dragStep_ = currentStep;
                if (beginEditCallback_) beginEditCallback_(dragLane_, dragStep_);
            }

            if (valueChangeCallback_)
                valueChangeCallback_(dragLane_, dragStep_, value);

            invalid();
            return VSTGUI::kMouseEventHandled;
        }

        // Hover tracking
        if (dataSource_) {
            auto hit = geometry_.hitTest(localX, localY);
            int newLane = (hit.subZone != SubZone::kNone && !hit.isCenter)
                ? subZoneToLaneIndex(hit.subZone) : -1;
            int newStep = hit.stepIndex;

            if (newLane != hoveredLane_ || newStep != hoveredStep_) {
                hoveredLane_ = newLane;
                hoveredStep_ = newStep;
                hoveredSubZone_ = hit.subZone;
                hoveredRingIndex_ = hit.ringIndex;

                // Update cursor
                if (auto* frame = getFrame()) {
                    if (newLane >= 0 && isBarTypeLane(newLane))
                        frame->setCursor(VSTGUI::kCursorVSize);
                    else if (newLane >= 0)
                        frame->setCursor(VSTGUI::kCursorHand);
                    else
                        frame->setCursor(VSTGUI::kCursorDefault);
                }

                invalid();
            }
        }

        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseUp(
        [[maybe_unused]] VSTGUI::CPoint& where,
        [[maybe_unused]] const VSTGUI::CButtonState& buttons) override
    {
        if (isDragging_) {
            if (endEditCallback_) endEditCallback_(dragLane_, dragStep_);
            isDragging_ = false;
            dragLane_ = -1;
            dragStep_ = -1;
            invalid();
        }
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseExited(
        [[maybe_unused]] VSTGUI::CPoint& where,
        [[maybe_unused]] const VSTGUI::CButtonState& buttons) override
    {
        if (hoveredLane_ >= 0) {
            hoveredLane_ = -1;
            hoveredStep_ = -1;
            if (auto* frame = getFrame())
                frame->setCursor(VSTGUI::kCursorDefault);
            invalid();
        }
        return VSTGUI::kMouseEventHandled;
    }

    // =========================================================================
    // Drawing
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override
    {
        context->setDrawMode(
            VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        drawBackground(context);
        drawRingBackgrounds(context);

        if (dataSource_) {
            drawRing0(context);  // Velocity + Gate
            drawRing1(context);  // Pitch
            drawRing2(context);  // Modifier + Condition
            drawRing3(context);  // Ratchet + Chord + Inversion
            drawPlayheadHighlights(context);
        }

        drawEuclideanCenter(context);
        drawRingBorders(context);
        drawSelectedRingEmphasis(context);
        drawHoverIndicator(context);

        setDirty(false);
    }

private:
    // =========================================================================
    // Background
    // =========================================================================

    void drawBackground(VSTGUI::CDrawContext* context) const
    {
        context->setFillColor({0x1A, 0x1A, 0x1E, 0xFF});
        context->drawRect(getViewSize(), VSTGUI::kDrawFilled);
    }

    void drawRingBackgrounds(VSTGUI::CDrawContext* context) const
    {
        const VSTGUI::CColor bgColor{0x22, 0x22, 0x28, 0xFF};
        const float cx = geometry_.centerX();
        const float cy = geometry_.centerY();

        for (int r = 0; r < kRingCount; ++r) {
            const auto& ring = geometry_.ringConfig(r);
            drawAnnulus(context, cx, cy,
                        ring.innerRadius, ring.outerRadius, bgColor);
        }
    }

    void drawRingBorders(VSTGUI::CDrawContext* context) const
    {
        const VSTGUI::CColor borderColor{0x3A, 0x3A, 0x42, 0xFF};
        const float cx = geometry_.centerX();
        const float cy = geometry_.centerY();

        context->setFrameColor(borderColor);
        context->setLineWidth(1.0f);

        for (int r = 0; r < kRingCount; ++r) {
            const auto& ring = geometry_.ringConfig(r);
            drawCircle(context, cx, cy, ring.innerRadius);
            drawCircle(context, cx, cy, ring.outerRadius);

            // Split lines for combined rings
            if (ring.splitRadius > 0.0f)
                drawCircle(context, cx, cy, ring.splitRadius);
            if (ring.split2Radius > 0.0f)
                drawCircle(context, cx, cy, ring.split2Radius);
        }

        // Center border
        drawCircle(context, cx, cy, geometry_.centerRadius());
    }

    // =========================================================================
    // Ring 0: Velocity (outer) + Gate (inner)
    // =========================================================================

    void drawRing0(VSTGUI::CDrawContext* context) const
    {
        const auto& colors = getLaneColors();
        const int velSteps = dataSource_->getStepCount(0);
        const int gateSteps = dataSource_->getStepCount(1);

        // Velocity: bars from split outward, height = value
        drawBarRing(context, 0, SubZone::kVelocity, velSteps,
                    colors[0].accent, false);

        // Gate: bars from inner to split, height = value
        drawBarRing(context, 0, SubZone::kGate, gateSteps,
                    colors[1].accent, false);
    }

    // =========================================================================
    // Ring 1: Pitch (bipolar)
    // =========================================================================

    void drawRing1(VSTGUI::CDrawContext* context) const
    {
        const auto& colors = getLaneColors();
        const int pitchSteps = dataSource_->getStepCount(2);

        // Pitch is bipolar: 0.5 = center (zero), bars extend up or down
        drawBarRing(context, 1, SubZone::kPitch, pitchSteps,
                    colors[2].accent, true);
    }

    // =========================================================================
    // Ring 2: Modifier (outer) + Condition (inner)
    // =========================================================================

    void drawRing2(VSTGUI::CDrawContext* context) const
    {
        const auto& colors = getLaneColors();
        const int modSteps = dataSource_->getStepCount(3);
        const int condSteps = dataSource_->getStepCount(4);

        // Modifier: 4 flag dots per step segment
        drawModifierRing(context, modSteps, colors[3].accent);

        // Condition: colored blocks per step
        drawConditionRing(context, condSteps, colors[4].accent);
    }

    // =========================================================================
    // Ring 3: Ratchet (outer) + Chord (middle) + Inversion (inner)
    // =========================================================================

    void drawRing3(VSTGUI::CDrawContext* context) const
    {
        const auto& colors = getLaneColors();
        const int ratchetSteps = dataSource_->getStepCount(5);
        const int chordSteps = dataSource_->getStepCount(6);
        const int invSteps = dataSource_->getStepCount(7);

        // Ratchet: subdivision marks
        drawRatchetRing(context, ratchetSteps, colors[5].accent);

        // Chord: value blocks
        drawDiscreteRing(context, 3, SubZone::kChord, chordSteps,
                         colors[6].accent, 5); // 0-4

        // Inversion: value blocks
        drawDiscreteRing(context, 3, SubZone::kInversion, invSteps,
                         colors[7].accent, 4); // 0-3
    }

    // =========================================================================
    // Playhead Highlights
    // =========================================================================

    void drawPlayheadHighlights(VSTGUI::CDrawContext* context) const
    {
        const auto& colors = getLaneColors();
        const float cx = geometry_.centerX();
        const float cy = geometry_.centerY();

        // Map lane index to ring/subzone
        struct LaneMapping {
            int ringIndex;
            SubZone zone;
        };
        static constexpr LaneMapping mappings[8] = {
            {0, SubZone::kVelocity},  {0, SubZone::kGate},
            {1, SubZone::kPitch},     {2, SubZone::kModifier},
            {2, SubZone::kCondition}, {3, SubZone::kRatchet},
            {3, SubZone::kChord},     {3, SubZone::kInversion},
        };

        for (int lane = 0; lane < 8; ++lane) {
            int step = dataSource_->getPlayheadStep(lane);
            if (step < 0) continue;

            int totalSteps = dataSource_->getStepCount(lane);
            if (step >= totalSteps) continue;

            const auto& mapping = mappings[lane];
            float innerR = 0.0f, outerR = 0.0f;
            geometry_.getSubZoneRadii(
                mapping.ringIndex, mapping.zone, innerR, outerR);

            auto arc = RingGeometry::stepArc(step, totalSteps);

            // Draw highlight arc
            VSTGUI::CColor hlColor = colors[lane].highlight;
            hlColor.alpha = 200;

            drawArcSegment(context, cx, cy, innerR, outerR,
                           arc.startAngle, arc.endAngle, hlColor, true);

            // Trail: dimmer highlights on previous steps
            const auto& trail = dataSource_->getTrailState(lane);
            for (int t = 1; t < Krate::Plugins::PlayheadTrailState::kTrailLength; ++t) {
                int trailStep = trail.steps[t];
                if (trailStep < 0 || trailStep >= totalSteps) continue;

                auto trailArc = RingGeometry::stepArc(trailStep, totalSteps);
                VSTGUI::CColor trailColor = colors[lane].highlight;
                trailColor.alpha = static_cast<uint8_t>(
                    Krate::Plugins::PlayheadTrailState::kTrailAlphas[t]);

                drawArcSegment(context, cx, cy, innerR, outerR,
                               trailArc.startAngle, trailArc.endAngle,
                               trailColor, true);
            }
        }
    }

    // =========================================================================
    // Euclidean Center
    // =========================================================================

    void drawEuclideanCenter(VSTGUI::CDrawContext* context) const
    {
        if (!dataSource_) return;

        EuclideanCenter euclidean;
        euclidean.setEnabled(dataSource_->isEuclideanEnabled());
        euclidean.setHits(dataSource_->getEuclideanHits());
        euclidean.setSteps(dataSource_->getEuclideanSteps());
        euclidean.setRotation(dataSource_->getEuclideanRotation());

        euclidean.draw(context,
                       geometry_.centerX(), geometry_.centerY(),
                       geometry_.centerRadius() * 0.75f);
    }

    // =========================================================================
    // Drawing Helpers
    // =========================================================================

    /// Draw a unipolar or bipolar bar ring.
    void drawBarRing(VSTGUI::CDrawContext* context,
                     int ringIndex, SubZone zone, int totalSteps,
                     const VSTGUI::CColor& color, bool bipolar) const
    {
        const float cx = geometry_.centerX();
        const float cy = geometry_.centerY();
        const int laneIdx = subZoneToLaneIndex(zone);

        float innerR = 0.0f, outerR = 0.0f;
        geometry_.getSubZoneRadii(ringIndex, zone, innerR, outerR);

        const float gap = 1.0f; // px gap between segments

        for (int step = 0; step < totalSteps; ++step) {
            float value = dataSource_->getNormalizedValue(laneIdx, step);
            auto arc = RingGeometry::stepArc(step, totalSteps);

            // Shrink arc slightly for gap
            float halfGap = gap / ((innerR + outerR) * 0.5f);
            float a0 = arc.startAngle + halfGap;
            float a1 = arc.endAngle - halfGap;
            if (a0 >= a1) continue;

            if (bipolar) {
                // Pitch: 0.5 = zero. Draw from midline outward/inward.
                float midR = (innerR + outerR) * 0.5f;
                float deviation = value - 0.5f; // -0.5 to +0.5
                if (std::abs(deviation) < 0.01f) continue;

                float barInner, barOuter;
                if (deviation > 0.0f) {
                    barInner = midR;
                    barOuter = midR + deviation * (outerR - midR) * 2.0f;
                } else {
                    barInner = midR + deviation * (midR - innerR) * 2.0f;
                    barOuter = midR;
                }

                VSTGUI::CColor barColor = color;
                barColor.alpha = static_cast<uint8_t>(
                    100 + static_cast<int>(std::abs(deviation) * 2.0f * 155));

                drawArcSegment(context, cx, cy, barInner, barOuter,
                               a0, a1, barColor, false);
            } else {
                // Unipolar: bar from inner edge, height = value
                if (value < 0.01f) continue;
                float barOuter = innerR + value * (outerR - innerR);

                VSTGUI::CColor barColor = color;
                barColor.alpha = static_cast<uint8_t>(
                    80 + static_cast<int>(value * 175));

                drawArcSegment(context, cx, cy, innerR, barOuter,
                               a0, a1, barColor, false);
            }
        }
    }

    /// Draw modifier flags as tiny dots per step.
    void drawModifierRing(VSTGUI::CDrawContext* context,
                          int totalSteps,
                          const VSTGUI::CColor& color) const
    {
        const float cx = geometry_.centerX();
        const float cy = geometry_.centerY();
        float innerR = 0.0f, outerR = 0.0f;
        geometry_.getSubZoneRadii(2, SubZone::kModifier, innerR, outerR);

        // 4 flag dots stacked radially: Active, Tie, Slide, Accent
        const VSTGUI::CColor flagColors[4] = {
            {0x80, 0xE0, 0x80, 0xFF},  // Active: green
            {0xE0, 0xC0, 0x40, 0xFF},  // Tie: yellow
            {0x60, 0xA0, 0xE0, 0xFF},  // Slide: blue
            {0xE0, 0x60, 0x60, 0xFF},  // Accent: red
        };

        const float radialStep = (outerR - innerR) / 4.0f;
        const float dotRadius = std::min(radialStep * 0.35f, 2.5f);

        for (int step = 0; step < totalSteps; ++step) {
            float value = dataSource_->getNormalizedValue(3, step);
            int flags = static_cast<int>(std::round(value * 255.0f));

            auto arc = RingGeometry::stepArc(step, totalSteps);
            float midAngle = arc.centerAngle;

            for (int f = 0; f < 4; ++f) {
                float r = innerR + (static_cast<float>(f) + 0.5f) * radialStep;
                float dx = cx + r * std::cos(midAngle);
                float dy = cy + r * std::sin(midAngle);

                bool active = (flags & (1 << f)) != 0;
                VSTGUI::CRect dotRect(
                    dx - dotRadius, dy - dotRadius,
                    dx + dotRadius, dy + dotRadius);

                if (active) {
                    context->setFillColor(flagColors[f]);
                    context->drawEllipse(dotRect, VSTGUI::kDrawFilled);
                } else {
                    VSTGUI::CColor dim = flagColors[f];
                    dim.alpha = 30;
                    context->setFillColor(dim);
                    context->drawEllipse(dotRect, VSTGUI::kDrawFilled);
                }
            }
        }
    }

    /// Draw condition values as colored blocks.
    void drawConditionRing(VSTGUI::CDrawContext* context,
                           int totalSteps,
                           const VSTGUI::CColor& color) const
    {
        const float cx = geometry_.centerX();
        const float cy = geometry_.centerY();
        float innerR = 0.0f, outerR = 0.0f;
        geometry_.getSubZoneRadii(2, SubZone::kCondition, innerR, outerR);

        const float gap = 1.0f;

        for (int step = 0; step < totalSteps; ++step) {
            float value = dataSource_->getNormalizedValue(4, step);
            int condIndex = static_cast<int>(std::round(value * 17.0f));

            auto arc = RingGeometry::stepArc(step, totalSteps);
            float halfGap = gap / ((innerR + outerR) * 0.5f);
            float a0 = arc.startAngle + halfGap;
            float a1 = arc.endAngle - halfGap;
            if (a0 >= a1) continue;

            if (condIndex == 0) {
                // "Always" — full block, dim
                VSTGUI::CColor blockColor = color;
                blockColor.alpha = 40;
                drawArcSegment(context, cx, cy, innerR, outerR,
                               a0, a1, blockColor, false);
            } else {
                // Non-default condition — brighter block
                VSTGUI::CColor blockColor = color;
                blockColor.alpha = static_cast<uint8_t>(
                    60 + std::min(condIndex * 10, 150));
                drawArcSegment(context, cx, cy, innerR, outerR,
                               a0, a1, blockColor, false);
            }
        }
    }

    /// Draw ratchet subdivisions.
    void drawRatchetRing(VSTGUI::CDrawContext* context,
                         int totalSteps,
                         const VSTGUI::CColor& color) const
    {
        const float cx = geometry_.centerX();
        const float cy = geometry_.centerY();
        float innerR = 0.0f, outerR = 0.0f;
        geometry_.getSubZoneRadii(3, SubZone::kRatchet, innerR, outerR);

        const float gap = 1.0f;

        for (int step = 0; step < totalSteps; ++step) {
            float value = dataSource_->getNormalizedValue(5, step);
            int ratchetCount = std::clamp(
                1 + static_cast<int>(std::round(value * 3.0f)), 1, 4);

            auto arc = RingGeometry::stepArc(step, totalSteps);
            float halfGap = gap / ((innerR + outerR) * 0.5f);
            float a0 = arc.startAngle + halfGap;
            float a1 = arc.endAngle - halfGap;
            if (a0 >= a1) continue;

            // Draw the full segment
            VSTGUI::CColor segColor = color;
            segColor.alpha = static_cast<uint8_t>(
                60 + (ratchetCount - 1) * 50);
            drawArcSegment(context, cx, cy, innerR, outerR,
                           a0, a1, segColor, false);

            // Draw subdivision lines if ratchet > 1
            if (ratchetCount > 1) {
                context->setFrameColor({0xFF, 0xFF, 0xFF, 0x60});
                context->setLineWidth(0.8f);

                float subAngle = (a1 - a0) / static_cast<float>(ratchetCount);
                for (int s = 1; s < ratchetCount; ++s) {
                    float lineAngle = a0 + static_cast<float>(s) * subAngle;
                    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
                    geometry_.polarToCartesian(lineAngle, innerR, x0, y0);
                    geometry_.polarToCartesian(lineAngle, outerR, x1, y1);
                    context->drawLine(
                        VSTGUI::CPoint(x0, y0),
                        VSTGUI::CPoint(x1, y1));
                }
            }
        }
    }

    /// Draw discrete value blocks (for chord, inversion).
    void drawDiscreteRing(VSTGUI::CDrawContext* context,
                          int ringIndex, SubZone zone, int totalSteps,
                          const VSTGUI::CColor& color,
                          int maxValue) const
    {
        const float cx = geometry_.centerX();
        const float cy = geometry_.centerY();
        const int laneIdx = subZoneToLaneIndex(zone);
        float innerR = 0.0f, outerR = 0.0f;
        geometry_.getSubZoneRadii(ringIndex, zone, innerR, outerR);

        const float gap = 1.0f;

        for (int step = 0; step < totalSteps; ++step) {
            float value = dataSource_->getNormalizedValue(laneIdx, step);
            int discrete = static_cast<int>(
                std::round(value * static_cast<float>(maxValue - 1)));

            auto arc = RingGeometry::stepArc(step, totalSteps);
            float halfGap = gap / ((innerR + outerR) * 0.5f);
            float a0 = arc.startAngle + halfGap;
            float a1 = arc.endAngle - halfGap;
            if (a0 >= a1) continue;

            // Fill proportional to value
            float fillRatio = static_cast<float>(discrete)
                              / static_cast<float>(maxValue - 1);

            VSTGUI::CColor blockColor = color;
            blockColor.alpha = static_cast<uint8_t>(
                30 + static_cast<int>(fillRatio * 200));

            drawArcSegment(context, cx, cy, innerR, outerR,
                           a0, a1, blockColor, false);
        }
    }

    // =========================================================================
    // Low-Level Drawing Primitives
    // =========================================================================

    /// Draw an annulus (ring background).
    static void drawAnnulus(VSTGUI::CDrawContext* context,
                            float cx, float cy,
                            float innerR, float outerR,
                            const VSTGUI::CColor& color)
    {
        // Draw as two concentric circles with fill
        // Outer circle filled, inner circle as background cutout
        // For simplicity, draw outer filled then inner with bg color
        context->setFillColor(color);
        VSTGUI::CRect outerRect(cx - outerR, cy - outerR,
                                cx + outerR, cy + outerR);
        context->drawEllipse(outerRect, VSTGUI::kDrawFilled);

        context->setFillColor({0x1A, 0x1A, 0x1E, 0xFF});
        VSTGUI::CRect innerRect(cx - innerR, cy - innerR,
                                cx + innerR, cy + innerR);
        context->drawEllipse(innerRect, VSTGUI::kDrawFilled);
    }

    /// Draw a circle outline.
    static void drawCircle(VSTGUI::CDrawContext* context,
                            float cx, float cy, float r)
    {
        VSTGUI::CRect rect(cx - r, cy - r, cx + r, cy + r);
        context->drawEllipse(rect, VSTGUI::kDrawStroked);
    }

    /// Draw a filled arc segment (wedge between inner and outer radius).
    void drawArcSegment(VSTGUI::CDrawContext* context,
                        float cx, float cy,
                        float innerR, float outerR,
                        float startAngle, float endAngle,
                        const VSTGUI::CColor& color,
                        bool isHighlight) const
    {
        auto* path = context->createGraphicsPath();
        if (!path) return;

        // Build wedge path: outer arc → inner arc (reversed)
        const int segments = std::max(
            4, static_cast<int>((endAngle - startAngle) / 0.1f));

        // Outer arc (forward)
        for (int i = 0; i <= segments; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(segments);
            float angle = startAngle + t * (endAngle - startAngle);
            float x = cx + outerR * std::cos(angle);
            float y = cy + outerR * std::sin(angle);
            if (i == 0)
                path->beginSubpath(VSTGUI::CPoint(x, y));
            else
                path->addLine(VSTGUI::CPoint(x, y));
        }

        // Inner arc (reverse)
        for (int i = segments; i >= 0; --i) {
            float t = static_cast<float>(i) / static_cast<float>(segments);
            float angle = startAngle + t * (endAngle - startAngle);
            float x = cx + innerR * std::cos(angle);
            float y = cy + innerR * std::sin(angle);
            path->addLine(VSTGUI::CPoint(x, y));
        }

        path->closeSubpath();

        if (isHighlight) {
            // Highlight: stroke with thick border
            context->setFrameColor(color);
            context->setLineWidth(2.5f);
            context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
        } else {
            // Normal: filled
            context->setFillColor(color);
            context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
        }

        path->forget();
    }

    // =========================================================================
    // Selected Ring Emphasis
    // =========================================================================

    void drawSelectedRingEmphasis(VSTGUI::CDrawContext* context) const
    {
        if (selectedLane_ < 0) return;

        // Map lane to ring/subzone
        struct LaneMapping { int ringIndex; SubZone zone; };
        static constexpr LaneMapping mappings[8] = {
            {0, SubZone::kVelocity},  {0, SubZone::kGate},
            {1, SubZone::kPitch},     {2, SubZone::kModifier},
            {2, SubZone::kCondition}, {3, SubZone::kRatchet},
            {3, SubZone::kChord},     {3, SubZone::kInversion},
        };

        if (selectedLane_ >= 8) return;
        const auto& mapping = mappings[selectedLane_];
        const auto& colors = getLaneColors();

        float innerR = 0.0f, outerR = 0.0f;
        geometry_.getSubZoneRadii(mapping.ringIndex, mapping.zone,
                                  innerR, outerR);

        // Draw subtle glow around the selected sub-zone
        VSTGUI::CColor emphColor = colors[selectedLane_].accent;
        emphColor.alpha = 35;

        const float cx = geometry_.centerX();
        const float cy = geometry_.centerY();

        // Full annulus glow
        VSTGUI::CRect outerRect(cx - outerR - 1, cy - outerR - 1,
                                cx + outerR + 1, cy + outerR + 1);
        context->setFrameColor(emphColor);
        context->setLineWidth(2.5f);
        context->drawEllipse(outerRect, VSTGUI::kDrawStroked);
    }

    // =========================================================================
    // Hover Indicator
    // =========================================================================

    void drawHoverIndicator(VSTGUI::CDrawContext* context) const
    {
        if (hoveredLane_ < 0 || hoveredStep_ < 0 || !dataSource_) return;
        if (isDragging_) return; // Don't show hover during drag

        const auto& colors = getLaneColors();
        const float cx = geometry_.centerX();
        const float cy = geometry_.centerY();

        float innerR = 0.0f, outerR = 0.0f;
        geometry_.getSubZoneRadii(hoveredRingIndex_, hoveredSubZone_,
                                  innerR, outerR);

        int totalSteps = dataSource_->getStepCount(hoveredLane_);
        auto arc = RingGeometry::stepArc(hoveredStep_, totalSteps);

        // Draw hover outline in lane's highlight color
        VSTGUI::CColor hoverColor = colors[hoveredLane_].highlight;
        hoverColor.alpha = 120;

        drawArcSegment(context, cx, cy, innerR, outerR,
                       arc.startAngle, arc.endAngle, hoverColor, true);
    }

    // =========================================================================
    // Interaction Helpers
    // =========================================================================

    /// Returns true for lanes where drag editing changes a continuous value.
    [[nodiscard]] static bool isBarTypeLane(int laneIndex)
    {
        // 0=Vel, 1=Gate, 2=Pitch, 5=Ratchet are bar-type (draggable)
        // 3=Modifier, 4=Condition, 6=Chord, 7=Inversion are discrete (click)
        return laneIndex == 0 || laneIndex == 1 ||
               laneIndex == 2 || laneIndex == 5;
    }

    /// Cycle a discrete lane value on click.
    void cycleDiscreteValue(int laneIndex, int step)
    {
        if (!dataSource_) return;

        float current = dataSource_->getNormalizedValue(laneIndex, step);
        float newValue = 0.0f;

        switch (laneIndex) {
            case 3: // Modifier: toggle active flag (bit 0)
            {
                int flags = static_cast<int>(std::round(current * 255.0f));
                flags ^= 0x01; // Toggle active
                newValue = static_cast<float>(flags) / 255.0f;
                break;
            }
            case 4: // Condition: cycle 0-17
            {
                int cond = static_cast<int>(std::round(current * 17.0f));
                cond = (cond + 1) % 18;
                newValue = static_cast<float>(cond) / 17.0f;
                break;
            }
            case 6: // Chord: cycle 0-4
            {
                int chord = static_cast<int>(std::round(current * 4.0f));
                chord = (chord + 1) % 5;
                newValue = static_cast<float>(chord) / 4.0f;
                break;
            }
            case 7: // Inversion: cycle 0-3
            {
                int inv = static_cast<int>(std::round(current * 3.0f));
                inv = (inv + 1) % 4;
                newValue = static_cast<float>(inv) / 3.0f;
                break;
            }
            default:
                return;
        }

        if (beginEditCallback_) beginEditCallback_(laneIndex, step);
        if (valueChangeCallback_) valueChangeCallback_(laneIndex, step, newValue);
        if (endEditCallback_) endEditCallback_(laneIndex, step);
        invalid();
    }

    // =========================================================================
    // Members
    // =========================================================================

    IRingDataSource* dataSource_ = nullptr;
    RingGeometry geometry_;
    int selectedLane_ = -1;

    // Callbacks
    LaneSelectedCallback laneSelectedCallback_;
    ValueChangeCallback valueChangeCallback_;
    BeginEndEditCallback beginEditCallback_;
    BeginEndEditCallback endEditCallback_;

    // Hover state
    int hoveredLane_ = -1;
    int hoveredStep_ = -1;
    SubZone hoveredSubZone_ = SubZone::kNone;
    int hoveredRingIndex_ = -1;

    // Drag state
    bool isDragging_ = false;
    int dragLane_ = -1;
    int dragStep_ = -1;
    SubZone dragSubZone_ = SubZone::kNone;
    int dragRingIndex_ = -1;

    CLASS_METHODS(RingRenderer, CView)
};

} // namespace Gradus
