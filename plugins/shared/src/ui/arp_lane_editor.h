#pragma once

// ==============================================================================
// ArpLaneEditor - Arpeggiator Lane Step Editor
// ==============================================================================
// A StepPatternEditor subclass for arpeggiator lane editing with:
//   - Collapsible header with lane name and collapse triangle
//   - Lane type configuration (velocity, gate, pitch, ratchet)
//   - Accent color with derived normal/ghost colors
//   - Display range labels (top/bottom grid labels)
//   - Per-lane playhead parameter binding
//   - Miniature bar preview when collapsed
//   - IArpLane interface for polymorphic container management
//
// This component is plugin-agnostic: it communicates via callbacks and
// configurable parameter IDs. No dependency on any specific plugin.
//
// Registered as "ArpLaneEditor" via VSTGUI ViewCreator system.
// ==============================================================================

#include "arp_lane.h"
#include "arp_lane_header.h"
#include "step_pattern_editor.h"
#include "color_utils.h"

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/detail/uiviewcreatorattributes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>

namespace Krate::Plugins {

// ==============================================================================
// ArpLaneType Enum
// ==============================================================================

enum class ArpLaneType {
    kVelocity = 0,
    kGate = 1,
    kPitch = 2,
    kRatchet = 3
};

// ==============================================================================
// ArpLaneEditor Control
// ==============================================================================

class ArpLaneEditor : public StepPatternEditor, public IArpLane {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kHeaderHeight = ArpLaneHeader::kHeight;
    static constexpr float kMiniPreviewHeight = 12.0f;
    static constexpr float kMiniPreviewPaddingTop = 2.0f;
    static constexpr float kMiniPreviewPaddingBottom = 2.0f;

    /// Shared left margin for step content alignment across all arp lane types (FR-049).
    /// Must match ArpModifierLane::kLeftMargin and ArpConditionLane::kLeftMargin.
    static constexpr float kStepContentLeftMargin = 40.0f;

    // =========================================================================
    // Construction
    // =========================================================================

    ArpLaneEditor(const VSTGUI::CRect& size,
                  VSTGUI::IControlListener* listener,
                  int32_t tag)
        : StepPatternEditor(size, listener, tag) {
        // Set default accent color (copper) and derive normal/ghost
        setAccentColor(accentColor_);
        // Offset the bar area down by the header height
        setBarAreaTopOffset(kHeaderHeight);
        // Align bar area left offset with kStepContentLeftMargin (FR-049)
        setBarAreaLeftOffset(kStepContentLeftMargin);
    }

    ArpLaneEditor(const ArpLaneEditor& other)
        : StepPatternEditor(other)
        , laneType_(other.laneType_)
        , laneName_(other.laneName_)
        , accentColor_(other.accentColor_)
        , displayMin_(other.displayMin_)
        , displayMax_(other.displayMax_)
        , topLabel_(other.topLabel_)
        , bottomLabel_(other.bottomLabel_)
        , playheadParamId_(other.playheadParamId_)
        , expandedHeight_(other.expandedHeight_)
        , header_(other.header_) {
        // Re-derive colors from accent
        setAccentColor(accentColor_);
    }

    // =========================================================================
    // Lane Configuration
    // =========================================================================

    void setLaneType(ArpLaneType type) {
        laneType_ = type;
        // Pitch mode: right-click resets to 0.5 (0 semitones center line)
        if (type == ArpLaneType::kPitch) {
            setRightClickResetLevel(0.5f);
            setGridLabels("", "");   // Bipolar labels drawn separately
        } else if (type == ArpLaneType::kRatchet) {
            setRightClickResetLevel(0.0f);
            setGridLabels("4", "1"); // Ratchet count range
        } else {
            setRightClickResetLevel(0.0f);
            // kVelocity/kGate keep the default "1.0"/"0.0"
        }
    }
    [[nodiscard]] ArpLaneType getLaneType() const { return laneType_; }

    void setLaneName(const std::string& name) {
        laneName_ = name;
        header_.setLaneName(name);
    }
    [[nodiscard]] const std::string& getLaneName() const { return laneName_; }

    void setAccentColor(const VSTGUI::CColor& color) {
        accentColor_ = color;
        header_.setAccentColor(color);

        // Derive normal and ghost colors
        VSTGUI::CColor normal = darkenColor(color, 0.6f);
        VSTGUI::CColor ghost = darkenColor(color, 0.35f);

        // Apply to base class color slots
        setBarColorAccent(color);
        setBarColorNormal(normal);
        setBarColorGhost(ghost);
    }

    [[nodiscard]] VSTGUI::CColor getAccentColor() const { return accentColor_; }

    void setDisplayRange(float min, float max,
                         const std::string& topLabel,
                         const std::string& bottomLabel) {
        displayMin_ = min;
        displayMax_ = max;
        topLabel_ = topLabel;
        bottomLabel_ = bottomLabel;
    }

    [[nodiscard]] const std::string& getTopLabel() const { return topLabel_; }
    [[nodiscard]] const std::string& getBottomLabel() const { return bottomLabel_; }
    [[nodiscard]] float getDisplayMin() const { return displayMin_; }
    [[nodiscard]] float getDisplayMax() const { return displayMax_; }

    // =========================================================================
    // Discrete Mode Helpers (kRatchet)
    // =========================================================================

    /// Decode discrete count (1-4) from normalized step level (0.0-1.0).
    /// Formula: count = clamp(1 + round(normalized * 3.0), 1, 4)
    [[nodiscard]] int getDiscreteCount(int step) const {
        float normalized = getStepLevel(step);
        return std::clamp(
            static_cast<int>(1.0f + std::round(normalized * 3.0f)), 1, 4);
    }

    /// Encode discrete count (1-4) to normalized step level.
    /// Formula: normalized = (count - 1) / 3.0
    void setDiscreteCount(int step, int count) {
        count = std::clamp(count, 1, 4);
        float normalized = static_cast<float>(count - 1) / 3.0f;
        setStepLevel(step, normalized);
    }

    /// Click-cycle discrete value: 1->2->3->4->1
    void handleDiscreteClick(int step) {
        int count = getDiscreteCount(step);
        int nextCount = (count % 4) + 1;
        notifyBeginEdit(step);
        float newNormalized = static_cast<float>(nextCount - 1) / 3.0f;
        setStepLevel(step, newNormalized);
        notifyStepChange(step, newNormalized);
        notifyEndEdit(step);
        setDirty();
    }

    // =========================================================================
    // Parameter Binding
    // =========================================================================

    void setLengthParamId(uint32_t paramId) {
        header_.setLengthParamId(paramId);
    }
    [[nodiscard]] uint32_t getLengthParamId() const {
        return header_.getLengthParamId();
    }

    void setLengthParamCallback(std::function<void(uint32_t, float)> cb) {
        header_.setLengthParamCallback(std::move(cb));
    }

    void setPlayheadParamId(uint32_t paramId) { playheadParamId_ = paramId; }
    [[nodiscard]] uint32_t getPlayheadParamId() const { return playheadParamId_; }

    // =========================================================================
    // IArpLane Interface Implementation
    // =========================================================================

    VSTGUI::CView* getView() override { return this; }

    [[nodiscard]] float getExpandedHeight() const override {
        if (expandedHeight_ > 0.0f) {
            return expandedHeight_;
        }
        return static_cast<float>(getViewSize().getHeight());
    }

    [[nodiscard]] float getCollapsedHeight() const override {
        return ArpLaneHeader::kHeight;
    }

    [[nodiscard]] bool isCollapsed() const override {
        return header_.isCollapsed();
    }

    void setCollapsed(bool collapsed) override {
        if (!header_.isCollapsed() && collapsed) {
            // Transitioning from expanded to collapsed: save expanded height
            expandedHeight_ = static_cast<float>(getViewSize().getHeight());
        }
        header_.setCollapsed(collapsed);
        if (collapseCallback_) {
            collapseCallback_();
        }
        setDirty();
    }

    void setPlayheadStep(int32_t step) override {
        setPlaybackStep(step);
    }

    void setLength(int32_t length) override {
        setNumSteps(length);
        header_.setNumSteps(length);
    }

    void setCollapseCallback(std::function<void()> cb) override {
        collapseCallback_ = std::move(cb);
    }

    // =========================================================================
    // CControl Overrides
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        VSTGUI::CRect vs = getViewSize();
        VSTGUI::CRect headerRect(vs.left, vs.top, vs.right, vs.top + kHeaderHeight);

        // Keep header numSteps in sync
        header_.setNumSteps(getNumSteps());

        if (isCollapsed()) {
            header_.draw(context, headerRect);
            drawMiniaturePreview(context, vs);
        } else {
            // Draw body FIRST (base class fills entire view with bg)
            if (laneType_ == ArpLaneType::kPitch) {
                StepPatternEditor::draw(context);
                drawBipolarBars(context);
                drawBipolarGridLabels(context);
            } else if (laneType_ == ArpLaneType::kRatchet) {
                StepPatternEditor::draw(context);
                drawDiscreteBlocks(context);
            } else {
                StepPatternEditor::draw(context);
            }
            // Draw header LAST so it's on top (not erased by base bg fill)
            header_.draw(context, headerRect);
        }

        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {

        VSTGUI::CRect vs = getViewSize();
        VSTGUI::CRect headerRect(vs.left, vs.top, vs.right, vs.top + kHeaderHeight);

        // Track collapse state before header interaction
        bool wasCollapsed = isCollapsed();

        // Delegate header interaction to ArpLaneHeader
        if (header_.handleMouseDown(where, headerRect, getFrame())) {
            // If collapse state changed, fire the collapse callback
            if (isCollapsed() != wasCollapsed && collapseCallback_) {
                collapseCallback_();
            }
            setDirty();
            return VSTGUI::kMouseEventHandled;
        }

        // If collapsed, don't delegate to base class
        if (isCollapsed()) {
            return VSTGUI::kMouseEventHandled;
        }

        // Pitch mode: custom bipolar interaction
        if (laneType_ == ArpLaneType::kPitch) {
            return handleBipolarMouseDown(where, buttons);
        }

        // Ratchet mode: custom discrete interaction
        if (laneType_ == ArpLaneType::kRatchet) {
            return handleDiscreteMouseDown(where, buttons);
        }

        // Delegate to base class for bar interaction
        return StepPatternEditor::onMouseDown(where, buttons);
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {

        // Pitch mode: custom bipolar drag
        if (laneType_ == ArpLaneType::kPitch && isDragging_) {
            return handleBipolarMouseMoved(where, buttons);
        }

        // Ratchet mode: custom discrete drag
        if (laneType_ == ArpLaneType::kRatchet && discreteIsDragging_) {
            return handleDiscreteMouseMoved(where, buttons);
        }

        // Delegate to base class for standard drag
        return StepPatternEditor::onMouseMoved(where, buttons);
    }

    VSTGUI::CMouseEventResult onMouseUp(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {

        // Ratchet mode: handle click (click = <4px movement) vs drag
        if (laneType_ == ArpLaneType::kRatchet && discreteIsDragging_) {
            float deltaY = static_cast<float>(where.y) - discreteClickStartY_;
            if (std::abs(deltaY) < 4.0f) {
                // This was a click, not a drag
                handleDiscreteClick(discreteClickStep_);
            }
            // End edit for dirty steps
            for (int i = 0; i < kMaxSteps; ++i) {
                if (dirtySteps_.test(static_cast<size_t>(i))) {
                    notifyEndEdit(i);
                }
            }
            discreteIsDragging_ = false;
            isDragging_ = false;
            dirtySteps_.reset();
            lastDragStep_ = -1;
            return VSTGUI::kMouseEventHandled;
        }

        return StepPatternEditor::onMouseUp(where, buttons);
    }

    CLASS_METHODS(ArpLaneEditor, StepPatternEditor)

private:
    // =========================================================================
    // Drawing Helpers
    // =========================================================================

    void drawMiniaturePreview(VSTGUI::CDrawContext* context,
                              const VSTGUI::CRect& vs) {
        if (laneType_ == ArpLaneType::kPitch) {
            VSTGUI::CRect previewRect(
                vs.left + 80.0, vs.top + kMiniPreviewPaddingTop,
                vs.right - 4.0, vs.top + kHeaderHeight - kMiniPreviewPaddingBottom);
            drawBipolarMiniPreview(context, previewRect);
            return;
        }

        if (laneType_ == ArpLaneType::kRatchet) {
            VSTGUI::CRect previewRect(
                vs.left + 80.0, vs.top + kMiniPreviewPaddingTop,
                vs.right - 4.0, vs.top + kHeaderHeight - kMiniPreviewPaddingBottom);
            drawDiscreteMiniPreview(context, previewRect);
            return;
        }

        // Standard bar preview (velocity, gate)
        int steps = getNumSteps();
        if (steps <= 0) return;

        float previewLeft = static_cast<float>(vs.left) + 80.0f;
        float previewRight = static_cast<float>(vs.right) - 4.0f;
        float previewTop = static_cast<float>(vs.top) + kMiniPreviewPaddingTop;
        float previewBottom = static_cast<float>(vs.top) + kHeaderHeight - kMiniPreviewPaddingBottom;
        float previewWidth = previewRight - previewLeft;
        float previewHeight = previewBottom - previewTop;

        if (previewWidth <= 0.0f || previewHeight <= 0.0f) return;

        float barWidth = previewWidth / static_cast<float>(steps);

        for (int i = 0; i < steps; ++i) {
            float level = getStepLevel(i);
            if (level <= 0.0f) continue;

            VSTGUI::CColor barColor = getColorForLevel(level);
            float barLeft = previewLeft + static_cast<float>(i) * barWidth + 0.5f;
            float barRight = barLeft + barWidth - 1.0f;
            float barTop = previewTop + previewHeight * (1.0f - level);

            if (barRight <= barLeft) continue;

            VSTGUI::CRect barRect(barLeft, barTop, barRight, previewBottom);
            context->setFillColor(barColor);
            context->drawRect(barRect, VSTGUI::kDrawFilled);
        }
    }

    // =========================================================================
    // Bipolar Mode Drawing (FR-001, FR-002, FR-007, FR-008, FR-010)
    // =========================================================================

    /// Draw bipolar bars extending from center line (kPitch mode).
    /// Overlays on top of the base class draw. The base class draws standard
    /// bars from bottom, but for pitch we need bars from center. We overdraw
    /// the bar area background first, then draw bipolar bars.
    void drawBipolarBars(VSTGUI::CDrawContext* context) {
        VSTGUI::CRect barArea = getBarArea();
        float barHeight = static_cast<float>(barArea.getHeight());
        float centerY = static_cast<float>(barArea.top) + barHeight / 2.0f;

        // Overdraw bar area background to cover base class bars
        context->setFillColor(getEditorBackgroundColor());
        context->drawRect(barArea, VSTGUI::kDrawFilled);

        // Draw grid lines for bipolar mode
        context->setFrameColor(getGridColor());
        context->setLineWidth(1.0);
        context->setLineStyle(VSTGUI::kLineSolid);

        // Grid lines at 0.0, 0.25, 0.50, 0.75, 1.0 (in normalized space)
        const float gridLevels[] = {0.0f, 0.25f, 0.50f, 0.75f, 1.0f};
        for (float gLevel : gridLevels) {
            float y = static_cast<float>(barArea.top) + barHeight * (1.0f - gLevel);
            context->drawLine(
                VSTGUI::CPoint(barArea.left, y),
                VSTGUI::CPoint(barArea.right, y));
        }

        // Draw center line more prominently
        VSTGUI::CColor centerLineColor = getGridColor();
        centerLineColor.alpha = static_cast<uint8_t>(
            std::min(255, static_cast<int>(centerLineColor.alpha) + 40));
        context->setFrameColor(centerLineColor);
        context->drawLine(
            VSTGUI::CPoint(barArea.left, centerY),
            VSTGUI::CPoint(barArea.right, centerY));

        // Draw bipolar bars
        int visibleStart = 0;
        int visibleEnd = getNumSteps();
        float barAreaWidth = static_cast<float>(barArea.getWidth());
        if (visibleEnd <= 0) return;

        float stepWidth = barAreaWidth / static_cast<float>(visibleEnd);
        float padding = 1.5f;

        for (int i = visibleStart; i < visibleEnd; ++i) {
            float normalized = getStepLevel(i);
            float signedValue = (normalized - 0.5f) * 2.0f; // -1.0 to +1.0

            float barLeft = static_cast<float>(barArea.left) +
                static_cast<float>(i) * stepWidth + padding;
            float barRight = barLeft + stepWidth - 2.0f * padding;
            if (barRight <= barLeft) continue;

            if (std::abs(signedValue) < 0.001f) {
                // Zero: draw outline at center
                VSTGUI::CRect zeroRect(barLeft, centerY - 1.0f, barRight, centerY + 1.0f);
                context->setFrameColor(getSilentOutlineColor());
                context->setLineWidth(1.0);
                context->drawRect(zeroRect, VSTGUI::kDrawStroked);
                continue;
            }

            float barTop, barBottom;
            if (signedValue > 0.0f) {
                barTop = centerY - (signedValue * barHeight / 2.0f);
                barBottom = centerY;
            } else {
                barTop = centerY;
                barBottom = centerY + (std::abs(signedValue) * barHeight / 2.0f);
            }

            VSTGUI::CColor barColor = getColorForLevel(std::abs(signedValue));
            context->setFillColor(barColor);
            VSTGUI::CRect bar(barLeft, barTop, barRight, barBottom);
            context->drawRect(bar, VSTGUI::kDrawFilled);
        }

        // Redraw playback indicator (was covered by overdraw)
        drawBipolarPlaybackOverlay(context);
    }

    /// Draw bipolar grid labels: "+24" at top, "0" at center, "-24" at bottom
    void drawBipolarGridLabels(VSTGUI::CDrawContext* context) {
        VSTGUI::CRect barArea = getBarArea();
        float barHeight = static_cast<float>(barArea.getHeight());
        float centerY = static_cast<float>(barArea.top) + barHeight / 2.0f;

        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 9.0);
        context->setFont(font);
        context->setFontColor(getTextColor());

        // Top label: "+24"
        VSTGUI::CRect topLabelRect(
            getViewSize().left, static_cast<float>(barArea.top) - 6.0f,
            static_cast<float>(barArea.left) - 2.0f,
            static_cast<float>(barArea.top) + 6.0f);
        context->drawString(VSTGUI::UTF8String(topLabel_), topLabelRect,
                           VSTGUI::kRightText, true);

        // Center label: "0"
        VSTGUI::CRect centerLabelRect(
            getViewSize().left, centerY - 6.0f,
            static_cast<float>(barArea.left) - 2.0f,
            centerY + 6.0f);
        context->drawString(VSTGUI::UTF8String("0"), centerLabelRect,
                           VSTGUI::kRightText, true);

        // Bottom label: "-24"
        VSTGUI::CRect bottomLabelRect(
            getViewSize().left, static_cast<float>(barArea.bottom) - 6.0f,
            static_cast<float>(barArea.left) - 2.0f,
            static_cast<float>(barArea.bottom) + 6.0f);
        context->drawString(VSTGUI::UTF8String(bottomLabel_), bottomLabelRect,
                           VSTGUI::kRightText, true);
    }

    /// Draw a playback overlay in bipolar mode
    void drawBipolarPlaybackOverlay(VSTGUI::CDrawContext* context) {
        int step = getPlaybackStep();
        if (step < 0 || step >= getNumSteps()) return;

        VSTGUI::CRect barArea = getBarArea();
        float barAreaWidth = static_cast<float>(barArea.getWidth());
        int numSteps = getNumSteps();
        if (numSteps <= 0) return;

        float stepWidth = barAreaWidth / static_cast<float>(numSteps);
        float barLeft = static_cast<float>(barArea.left) + static_cast<float>(step) * stepWidth;
        float barRight = barLeft + stepWidth;

        VSTGUI::CColor overlayColor = accentColor_;
        overlayColor.alpha = 40;
        context->setFillColor(overlayColor);
        VSTGUI::CRect overlay(barLeft, barArea.top, barRight, barArea.bottom);
        context->drawRect(overlay, VSTGUI::kDrawFilled);
    }

    /// Draw bipolar mini preview for collapsed pitch lane (FR-010)
    void drawBipolarMiniPreview(VSTGUI::CDrawContext* context,
                                const VSTGUI::CRect& previewRect) {
        int steps = getNumSteps();
        if (steps <= 0) return;

        float previewWidth = static_cast<float>(previewRect.getWidth());
        float previewHeight = static_cast<float>(previewRect.getHeight());
        if (previewWidth <= 0.0f || previewHeight <= 0.0f) return;

        float centerY = static_cast<float>(previewRect.top) + previewHeight / 2.0f;
        float barWidth = previewWidth / static_cast<float>(steps);

        for (int i = 0; i < steps; ++i) {
            float normalized = getStepLevel(i);
            float signedValue = (normalized - 0.5f) * 2.0f;

            if (std::abs(signedValue) < 0.001f) continue;

            float barLeft = static_cast<float>(previewRect.left) +
                static_cast<float>(i) * barWidth + 0.5f;
            float barRight = barLeft + barWidth - 1.0f;
            if (barRight <= barLeft) continue;

            float barTop, barBottom;
            if (signedValue > 0.0f) {
                barTop = centerY - (signedValue * previewHeight / 2.0f);
                barBottom = centerY;
            } else {
                barTop = centerY;
                barBottom = centerY + (std::abs(signedValue) * previewHeight / 2.0f);
            }

            context->setFillColor(accentColor_);
            VSTGUI::CRect bar(barLeft, barTop, barRight, barBottom);
            context->drawRect(bar, VSTGUI::kDrawFilled);
        }
    }

    // =========================================================================
    // Bipolar Mode Interaction (FR-003, FR-004, FR-005, FR-006)
    // =========================================================================

    /// Snap a raw normalized level (0-1) to the nearest integer semitone.
    /// Returns the snapped normalized value.
    [[nodiscard]] static float snapBipolarToSemitone(float rawNormalized) {
        // Canonical formula from spec:
        // Decode: semitones = round((normalized - 0.5) * 48.0)
        // Encode: normalized = 0.5 + semitones / 48.0
        float semitones = std::round((rawNormalized - 0.5f) * 48.0f);
        semitones = std::clamp(semitones, -24.0f, 24.0f);
        return 0.5f + semitones / 48.0f;
    }

    /// Handle mouse down in kPitch mode: set step to snapped bipolar value
    VSTGUI::CMouseEventResult handleBipolarMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) {

        if ((buttons & VSTGUI::kLButton) == 0)
            return VSTGUI::kMouseEventNotHandled;

        int step = getStepFromPoint(where);
        if (step < 0) return VSTGUI::kMouseEventNotHandled;

        // Start drag gesture
        isDragging_ = true;
        dirtySteps_.reset();
        preDragLevels_[0] = 0.0f; // Just to initialize (base class stores all)
        lastDragStep_ = step;

        // Get raw level from Y and snap to semitone
        float rawLevel = getLevelFromY(static_cast<float>(where.y));
        float snappedLevel = snapBipolarToSemitone(rawLevel);
        updateStepLevel(step, snappedLevel);

        return VSTGUI::kMouseEventHandled;
    }

    /// Handle mouse moved in kPitch mode: paint across steps with snapping
    VSTGUI::CMouseEventResult handleBipolarMouseMoved(
        VSTGUI::CPoint& where,
        [[maybe_unused]] const VSTGUI::CButtonState& buttons) {

        int step = getStepFromPoint(where);
        if (step < 0) return VSTGUI::kMouseEventHandled;

        float rawLevel = getLevelFromY(static_cast<float>(where.y));
        float snappedLevel = snapBipolarToSemitone(rawLevel);

        // Paint mode: fill steps between last and current
        if (lastDragStep_ >= 0 && step != lastDragStep_) {
            int from = std::min(lastDragStep_, step);
            int to = std::max(lastDragStep_, step);
            for (int i = from; i <= to; ++i) {
                if (i < getNumSteps()) {
                    updateStepLevel(i, snappedLevel);
                }
            }
        } else {
            updateStepLevel(step, snappedLevel);
        }

        lastDragStep_ = step;
        return VSTGUI::kMouseEventHandled;
    }

    // =========================================================================
    // Discrete Mode Drawing (FR-011, FR-012, FR-016, FR-018, FR-019)
    // =========================================================================

    /// Draw stacked blocks for ratchet/discrete mode.
    /// Overlays on top of the base class draw. The base class draws standard
    /// bars from bottom, but for ratchet we need stacked blocks. We overdraw
    /// the bar area background first, then draw discrete blocks.
    void drawDiscreteBlocks(VSTGUI::CDrawContext* context) {
        VSTGUI::CRect barArea = getBarArea();
        float barHeight = static_cast<float>(barArea.getHeight());

        // Overdraw bar area background to cover base class bars
        context->setFillColor(getEditorBackgroundColor());
        context->drawRect(barArea, VSTGUI::kDrawFilled);

        // Redraw grid lines
        context->setFrameColor(getGridColor());
        context->setLineWidth(1.0);
        context->setLineStyle(VSTGUI::kLineSolid);

        // Grid lines at 25%, 50%, 75%
        const float gridLevels[] = {0.25f, 0.50f, 0.75f};
        for (float gLevel : gridLevels) {
            float y = static_cast<float>(barArea.top) + barHeight * (1.0f - gLevel);
            context->drawLine(
                VSTGUI::CPoint(barArea.left, y),
                VSTGUI::CPoint(barArea.right, y));
        }

        // Draw stacked blocks
        int numSteps = getNumSteps();
        if (numSteps <= 0) return;

        float barAreaWidth = static_cast<float>(barArea.getWidth());
        float stepWidth = barAreaWidth / static_cast<float>(numSteps);
        float blockGap = 2.0f;
        float blockHeight = (barHeight - 3.0f * blockGap) / 4.0f;

        for (int i = 0; i < numSteps; ++i) {
            int count = getDiscreteCount(i);

            float barLeft = static_cast<float>(barArea.left) +
                static_cast<float>(i) * stepWidth + kBarPadding;
            float barRight = barLeft + stepWidth - 2.0f * kBarPadding;
            if (barRight <= barLeft) continue;

            VSTGUI::CColor blockColor = getColorForLevel(
                static_cast<float>(count) / 4.0f);

            for (int b = 0; b < count; ++b) {
                float blockBottom = static_cast<float>(barArea.bottom) -
                    static_cast<float>(b) * (blockHeight + blockGap);
                float blockTop = blockBottom - blockHeight;

                VSTGUI::CRect block(barLeft, blockTop, barRight, blockBottom);
                context->setFillColor(blockColor);
                context->drawRect(block, VSTGUI::kDrawFilled);
            }
        }

        // Redraw playback indicator (was covered by overdraw)
        drawDiscretePlaybackOverlay(context);
    }

    /// Draw playback overlay for discrete mode
    void drawDiscretePlaybackOverlay(VSTGUI::CDrawContext* context) {
        int step = getPlaybackStep();
        if (step < 0 || step >= getNumSteps()) return;

        VSTGUI::CRect barArea = getBarArea();
        float barAreaWidth = static_cast<float>(barArea.getWidth());
        int numSteps = getNumSteps();
        if (numSteps <= 0) return;

        float stepWidth = barAreaWidth / static_cast<float>(numSteps);
        float barLeft = static_cast<float>(barArea.left) +
            static_cast<float>(step) * stepWidth;
        float barRight = barLeft + stepWidth;

        VSTGUI::CColor overlayColor = accentColor_;
        overlayColor.alpha = 40;
        context->setFillColor(overlayColor);
        VSTGUI::CRect overlay(barLeft, barArea.top, barRight, barArea.bottom);
        context->drawRect(overlay, VSTGUI::kDrawFilled);
    }

    /// Draw discrete mini preview for collapsed ratchet lane (FR-019)
    void drawDiscreteMiniPreview(VSTGUI::CDrawContext* context,
                                 const VSTGUI::CRect& previewRect) {
        int steps = getNumSteps();
        if (steps <= 0) return;

        float previewWidth = static_cast<float>(previewRect.getWidth());
        float previewHeight = static_cast<float>(previewRect.getHeight());
        if (previewWidth <= 0.0f || previewHeight <= 0.0f) return;

        float barWidth = previewWidth / static_cast<float>(steps);

        for (int i = 0; i < steps; ++i) {
            int count = getDiscreteCount(i);
            float fraction = static_cast<float>(count) / 4.0f;

            float barLeft = static_cast<float>(previewRect.left) +
                static_cast<float>(i) * barWidth + 0.5f;
            float barRight = barLeft + barWidth - 1.0f;
            if (barRight <= barLeft) continue;

            float barTop = static_cast<float>(previewRect.top) +
                previewHeight * (1.0f - fraction);
            float barBottom = static_cast<float>(previewRect.bottom);

            context->setFillColor(accentColor_);
            VSTGUI::CRect bar(barLeft, barTop, barRight, barBottom);
            context->drawRect(bar, VSTGUI::kDrawFilled);
        }
    }

    // =========================================================================
    // Discrete Mode Interaction (FR-013, FR-014, FR-015)
    // =========================================================================

    /// Handle mouse down in kRatchet mode
    VSTGUI::CMouseEventResult handleDiscreteMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) {

        if ((buttons & VSTGUI::kLButton) == 0)
            return VSTGUI::kMouseEventNotHandled;

        int step = getStepFromPoint(where);
        if (step < 0) return VSTGUI::kMouseEventNotHandled;

        // Start drag/click tracking
        discreteIsDragging_ = true;
        isDragging_ = true; // Keep base class aware
        dirtySteps_.reset();
        lastDragStep_ = step;

        // Record start position for click detection
        discreteClickStartY_ = static_cast<float>(where.y);
        discreteClickStep_ = step;
        discreteDragStartValue_ = getDiscreteCount(step);
        discreteDragAccumY_ = 0.0f;
        discreteHasEnteredDrag_ = false;

        return VSTGUI::kMouseEventHandled;
    }

    /// Handle mouse moved in kRatchet mode
    VSTGUI::CMouseEventResult handleDiscreteMouseMoved(
        VSTGUI::CPoint& where,
        [[maybe_unused]] const VSTGUI::CButtonState& buttons) {

        float deltaY = static_cast<float>(where.y) - discreteClickStartY_;

        // Check if we've entered drag mode (>= 4px threshold)
        if (!discreteHasEnteredDrag_) {
            if (std::abs(deltaY) < 4.0f) {
                return VSTGUI::kMouseEventHandled; // Still in click zone
            }
            discreteHasEnteredDrag_ = true;
        }

        // In drag mode: compute level change from total delta
        // Negative deltaY (up) = increase count, positive (down) = decrease
        int levelChange = static_cast<int>(-deltaY / 8.0f);
        int newCount = std::clamp(discreteDragStartValue_ + levelChange, 1, 4);

        int step = discreteClickStep_;
        if (step >= 0 && step < getNumSteps()) {
            float newNormalized = static_cast<float>(newCount - 1) / 3.0f;
            updateStepLevel(step, newNormalized);
        }

        return VSTGUI::kMouseEventHandled;
    }

    // =========================================================================
    // State
    // =========================================================================

    ArpLaneType laneType_ = ArpLaneType::kVelocity;
    std::string laneName_;
    VSTGUI::CColor accentColor_{208, 132, 92, 255};
    float displayMin_ = 0.0f;
    float displayMax_ = 1.0f;
    std::string topLabel_ = "1.0";
    std::string bottomLabel_ = "0.0";
    uint32_t playheadParamId_ = 0;
    float expandedHeight_ = 0.0f;
    std::function<void()> collapseCallback_;
    ArpLaneHeader header_;

    // Discrete mode drag state
    bool discreteIsDragging_ = false;
    float discreteClickStartY_ = 0.0f;
    int discreteClickStep_ = -1;
    int discreteDragStartValue_ = 1;
    float discreteDragAccumY_ = 0.0f;
    bool discreteHasEnteredDrag_ = false;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct ArpLaneEditorCreator : VSTGUI::ViewCreatorAdapter {
    ArpLaneEditorCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override { return "ArpLaneEditor"; }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return "StepPatternEditor";
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Arp Lane Editor";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new ArpLaneEditor(VSTGUI::CRect(0, 0, 500, 86), nullptr, -1);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override {
        auto* editor = dynamic_cast<ArpLaneEditor*>(view);
        if (!editor)
            return false;

        // Lane type
        const auto* laneTypeStr = attributes.getAttributeValue("lane-type");
        if (laneTypeStr) {
            if (*laneTypeStr == "velocity")
                editor->setLaneType(ArpLaneType::kVelocity);
            else if (*laneTypeStr == "gate")
                editor->setLaneType(ArpLaneType::kGate);
            else if (*laneTypeStr == "pitch")
                editor->setLaneType(ArpLaneType::kPitch);
            else if (*laneTypeStr == "ratchet")
                editor->setLaneType(ArpLaneType::kRatchet);
        }

        // Accent color
        VSTGUI::CColor color;
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("accent-color"), color, description))
            editor->setAccentColor(color);

        // Lane name
        const auto* nameStr = attributes.getAttributeValue("lane-name");
        if (nameStr)
            editor->setLaneName(*nameStr);

        // Step level base param ID
        const auto* baseIdStr = attributes.getAttributeValue("step-level-base-param-id");
        if (baseIdStr) {
            auto id = static_cast<uint32_t>(std::stoul(*baseIdStr));
            editor->setStepLevelBaseParamId(id);
        }

        // Length param ID
        const auto* lengthIdStr = attributes.getAttributeValue("length-param-id");
        if (lengthIdStr) {
            auto id = static_cast<uint32_t>(std::stoul(*lengthIdStr));
            editor->setLengthParamId(id);
        }

        // Playhead param ID
        const auto* playheadIdStr = attributes.getAttributeValue("playhead-param-id");
        if (playheadIdStr) {
            auto id = static_cast<uint32_t>(std::stoul(*playheadIdStr));
            editor->setPlayheadParamId(id);
        }

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("lane-type");
        attributeNames.emplace_back("accent-color");
        attributeNames.emplace_back("lane-name");
        attributeNames.emplace_back("step-level-base-param-id");
        attributeNames.emplace_back("length-param-id");
        attributeNames.emplace_back("playhead-param-id");
        return true;
    }

    AttrType getAttributeType(const std::string& attributeName) const override {
        if (attributeName == "lane-type") return kListType;
        if (attributeName == "accent-color") return kColorType;
        if (attributeName == "lane-name") return kStringType;
        if (attributeName == "step-level-base-param-id") return kStringType;
        if (attributeName == "length-param-id") return kStringType;
        if (attributeName == "playhead-param-id") return kStringType;
        return kUnknownType;
    }

    bool getPossibleListValues(const std::string& attributeName,
                               VSTGUI::IViewCreator::ConstStringPtrList& values) const override {
        if (attributeName == "lane-type") {
            static const std::string kVelocity = "velocity";
            static const std::string kGate = "gate";
            static const std::string kPitch = "pitch";
            static const std::string kRatchet = "ratchet";
            values.emplace_back(&kVelocity);
            values.emplace_back(&kGate);
            values.emplace_back(&kPitch);
            values.emplace_back(&kRatchet);
            return true;
        }
        return false;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override {
        auto* editor = dynamic_cast<ArpLaneEditor*>(view);
        if (!editor)
            return false;

        if (attributeName == "lane-type") {
            switch (editor->getLaneType()) {
                case ArpLaneType::kVelocity: stringValue = "velocity"; return true;
                case ArpLaneType::kGate:     stringValue = "gate";     return true;
                case ArpLaneType::kPitch:    stringValue = "pitch";    return true;
                case ArpLaneType::kRatchet:  stringValue = "ratchet";  return true;
            }
            return false;
        }
        if (attributeName == "accent-color") {
            VSTGUI::UIViewCreator::colorToString(
                editor->getAccentColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "lane-name") {
            stringValue = editor->getLaneName();
            return true;
        }
        return false;
    }
};

inline ArpLaneEditorCreator gArpLaneEditorCreator;

} // namespace Krate::Plugins
