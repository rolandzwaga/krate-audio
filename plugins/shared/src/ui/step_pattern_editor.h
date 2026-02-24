#pragma once

// ==============================================================================
// StepPatternEditor - Visual Step Pattern Editor
// ==============================================================================
// A shared VSTGUI CControl subclass for editing step patterns visually.
// Renders a bar chart of step levels with click-and-drag editing, paint mode,
// color-coded bars, Euclidean dot indicators, playback position, phase offset.
//
// This component is plugin-agnostic: it communicates via ParameterCallback
// and configurable base parameter IDs. No dependency on any specific plugin.
//
// Registered as "StepPatternEditor" via VSTGUI ViewCreator system.
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

#include <krate/dsp/core/euclidean_pattern.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <random>
#include <string>

namespace Krate::Plugins {

// ==============================================================================
// StepPatternEditor Control
// ==============================================================================

class StepPatternEditor : public VSTGUI::CControl {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr int kMaxSteps = 32;
    static constexpr int kMinSteps = 2;

    // Layout zone heights
    static constexpr float kScrollIndicatorHeight = 6.0f;
    static constexpr float kPhaseOffsetHeight = 12.0f;
    static constexpr float kEuclideanDotHeight = 10.0f;
    static constexpr float kStepLabelHeight = 12.0f;
    static constexpr float kPlaybackIndicatorHeight = 8.0f;
    static constexpr float kBarPadding = 1.0f;
    static constexpr float kGridLabelWidth = 24.0f;

    // =========================================================================
    // Construction
    // =========================================================================

    StepPatternEditor(const VSTGUI::CRect& size,
                      VSTGUI::IControlListener* listener,
                      int32_t tag)
        : CControl(size, listener, tag) {
        stepLevels_.fill(1.0f);
        preDragLevels_.fill(1.0f);
        rng_.seed(static_cast<unsigned>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    }

    StepPatternEditor(const StepPatternEditor& other)
        : CControl(other)
        , stepLevels_(other.stepLevels_)
        , numSteps_(other.numSteps_)
        , playbackStep_(other.playbackStep_)
        , isPlaying_(other.isPlaying_)
        , phaseOffset_(other.phaseOffset_)
        , euclideanEnabled_(other.euclideanEnabled_)
        , euclideanHits_(other.euclideanHits_)
        , euclideanRotation_(other.euclideanRotation_)
        , euclideanPattern_(other.euclideanPattern_)
        , isModified_(other.isModified_)
        , zoomLevel_(other.zoomLevel_)
        , scrollOffset_(other.scrollOffset_)
        , visibleSteps_(other.visibleSteps_)
        , barColorAccent_(other.barColorAccent_)
        , barColorNormal_(other.barColorNormal_)
        , barColorGhost_(other.barColorGhost_)
        , silentOutlineColor_(other.silentOutlineColor_)
        , gridColor_(other.gridColor_)
        , backgroundColor_(other.backgroundColor_)
        , playbackColor_(other.playbackColor_)
        , textColor_(other.textColor_)
        , stepLevelBaseParamId_(other.stepLevelBaseParamId_) {
        rng_.seed(static_cast<unsigned>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    }

    // =========================================================================
    // Step Level API (FR-001, FR-005, FR-006, FR-012)
    // =========================================================================

    void setStepLevel(int index, float level) {
        if (index < 0 || index >= kMaxSteps) return;
        stepLevels_[static_cast<size_t>(index)] = std::clamp(level, 0.0f, 1.0f);
        setDirty();
    }

    [[nodiscard]] float getStepLevel(int index) const {
        if (index < 0 || index >= kMaxSteps) return 0.0f;
        return stepLevels_[static_cast<size_t>(index)];
    }

    // =========================================================================
    // Step Count API (FR-013, FR-015, FR-016)
    // =========================================================================

    void setNumSteps(int count) {
        count = std::clamp(count, kMinSteps, kMaxSteps);
        if (count == numSteps_) return;

        // Cancel any active drag (FR-017)
        if (isDragging_) {
            cancelDrag();
        }

        numSteps_ = count;

        // Clamp zoom/scroll to new step count
        clampZoomScroll();

        setDirty();
    }

    [[nodiscard]] int getNumSteps() const { return numSteps_; }

    // =========================================================================
    // Playback API (FR-024, FR-025, FR-026, FR-027)
    // =========================================================================

    void setPlaybackStep(int step) {
        if (step != playbackStep_) {
            playbackStep_ = step;
            setDirty();
        }
    }

    void setPlaying(bool playing) {
        if (playing == isPlaying_) return;
        isPlaying_ = playing;

        if (playing) {
            refreshTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
                [this](VSTGUI::CVSTGUITimer*) {
                    invalid();
                }, 33); // ~30fps
        } else {
            refreshTimer_ = nullptr;
        }
    }

    // =========================================================================
    // Phase Offset API (FR-028)
    // =========================================================================

    void setPhaseOffset(float offset) {
        phaseOffset_ = std::clamp(offset, 0.0f, 1.0f);
        setDirty();
    }

    [[nodiscard]] float getPhaseOffset() const { return phaseOffset_; }

    /// Right-click handler: set step to 0. Called from editor subclass since
    /// VST3Editor intercepts right-clicks at the frame level for context menus.
    void handleRightClick(const VSTGUI::CPoint& localPos) {
        int step = getStepFromPoint(localPos);
        if (step < 0) return;
        notifyBeginEdit(step);
        stepLevels_[static_cast<size_t>(step)] = 0.0f;
        notifyStepChange(step, 0.0f);
        notifyEndEdit(step);
        if (euclideanEnabled_) isModified_ = true;
        setDirty();
    }

    // =========================================================================
    // Euclidean Mode API (FR-018 through FR-023)
    // =========================================================================

    void setEuclideanEnabled(bool enabled) {
        if (enabled == euclideanEnabled_) return;
        euclideanEnabled_ = enabled;
        if (enabled) {
            regenerateEuclideanPattern();
            applyEuclideanPattern();
        }
        setDirty();
    }

    void setEuclideanHits(int hits) {
        hits = std::clamp(hits, 0, numSteps_);
        if (hits == euclideanHits_) return;
        euclideanHits_ = hits;
        if (euclideanEnabled_) {
            regenerateEuclideanPattern();
            applyEuclideanPattern();
        }
        setDirty();
    }

    void setEuclideanRotation(int rotation) {
        rotation = std::clamp(rotation, 0, std::max(0, numSteps_ - 1));
        if (rotation == euclideanRotation_) return;
        euclideanRotation_ = rotation;
        if (euclideanEnabled_) {
            regenerateEuclideanPattern();
            applyEuclideanPattern();
        }
        setDirty();
    }

    [[nodiscard]] bool isPatternModified() const { return isModified_; }

    /// Reset to pure Euclidean pattern (FR-023, called by external Regen button)
    void regenerateEuclidean() {
        if (!euclideanEnabled_) return;
        regenerateEuclideanPattern();
        // Reset ALL steps to pure Euclidean: hits=1.0, rests=0.0
        for (int i = 0; i < numSteps_; ++i) {
            float level = Krate::DSP::EuclideanPattern::isHit(
                euclideanPattern_, i, numSteps_) ? 1.0f : 0.0f;
            stepLevels_[static_cast<size_t>(i)] = level;
            notifyStepChange(i, level);
        }
        isModified_ = false;
        setDirty();
    }

    // =========================================================================
    // Parameter Callback (FR-012, FR-037)
    // =========================================================================

    using ParameterCallback = std::function<void(uint32_t paramId, float normalizedValue)>;
    using EditCallback = std::function<void(uint32_t paramId)>;

    void setParameterCallback(ParameterCallback cb) { paramCallback_ = std::move(cb); }
    void setBeginEditCallback(EditCallback cb) { beginEditCallback_ = std::move(cb); }
    void setEndEditCallback(EditCallback cb) { endEditCallback_ = std::move(cb); }
    void setStepLevelBaseParamId(uint32_t baseId) { stepLevelBaseParamId_ = baseId; }

    // =========================================================================
    // Preset / Transform API (FR-029, FR-030, FR-031)
    // =========================================================================

    void applyPresetAll() {
        applyPreset([](int /*step*/, int /*numSteps*/) { return 1.0f; });
    }

    void applyPresetOff() {
        applyPreset([](int /*step*/, int /*numSteps*/) { return 0.0f; });
    }

    void applyPresetAlternate() {
        applyPreset([](int step, int /*numSteps*/) {
            return (step % 2 == 0) ? 1.0f : 0.0f;
        });
    }

    void applyPresetRampUp() {
        applyPreset([](int step, int numSteps) {
            return (numSteps <= 1) ? 1.0f
                : static_cast<float>(step) / static_cast<float>(numSteps - 1);
        });
    }

    void applyPresetRampDown() {
        applyPreset([](int step, int numSteps) {
            return (numSteps <= 1) ? 1.0f
                : 1.0f - static_cast<float>(step) / static_cast<float>(numSteps - 1);
        });
    }

    void applyPresetRandom() {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        applyPreset([this, &dist](int /*step*/, int /*numSteps*/) {
            return dist(rng_);
        });
    }

    void applyTransformInvert() {
        for (int i = 0; i < numSteps_; ++i) {
            auto idx = static_cast<size_t>(i);
            float newLevel = 1.0f - stepLevels_[idx];
            notifyBeginEdit(i);
            stepLevels_[idx] = newLevel;
            notifyStepChange(i, newLevel);
            notifyEndEdit(i);
        }
        if (euclideanEnabled_) isModified_ = true;
        setDirty();
    }

    void applyTransformShiftRight() {
        if (numSteps_ < 2) return;
        float last = stepLevels_[static_cast<size_t>(numSteps_ - 1)];
        for (int i = numSteps_ - 1; i > 0; --i) {
            stepLevels_[static_cast<size_t>(i)] = stepLevels_[static_cast<size_t>(i - 1)];
        }
        stepLevels_[0] = last;

        // Notify all step changes
        for (int i = 0; i < numSteps_; ++i) {
            notifyBeginEdit(i);
            notifyStepChange(i, stepLevels_[static_cast<size_t>(i)]);
            notifyEndEdit(i);
        }
        if (euclideanEnabled_) isModified_ = true;
        setDirty();
    }

    void applyTransformShiftLeft() {
        if (numSteps_ < 2) return;
        float first = stepLevels_[0];
        for (int i = 0; i < numSteps_ - 1; ++i) {
            stepLevels_[static_cast<size_t>(i)] = stepLevels_[static_cast<size_t>(i + 1)];
        }
        stepLevels_[static_cast<size_t>(numSteps_ - 1)] = first;

        // Notify all step changes
        for (int i = 0; i < numSteps_; ++i) {
            notifyBeginEdit(i);
            notifyStepChange(i, stepLevels_[static_cast<size_t>(i)]);
            notifyEndEdit(i);
        }
        if (euclideanEnabled_) isModified_ = true;
        setDirty();
    }

    // =========================================================================
    // Color Configuration (FR-036)
    // =========================================================================

    void setBarColorAccent(VSTGUI::CColor color) { barColorAccent_ = color; }
    [[nodiscard]] VSTGUI::CColor getBarColorAccent() const { return barColorAccent_; }

    void setBarColorNormal(VSTGUI::CColor color) { barColorNormal_ = color; }
    [[nodiscard]] VSTGUI::CColor getBarColorNormal() const { return barColorNormal_; }

    void setBarColorGhost(VSTGUI::CColor color) { barColorGhost_ = color; }
    [[nodiscard]] VSTGUI::CColor getBarColorGhost() const { return barColorGhost_; }

    void setSilentOutlineColor(VSTGUI::CColor color) { silentOutlineColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getSilentOutlineColor() const { return silentOutlineColor_; }

    void setGridColor(VSTGUI::CColor color) { gridColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getGridColor() const { return gridColor_; }

    void setEditorBackgroundColor(VSTGUI::CColor color) { backgroundColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getEditorBackgroundColor() const { return backgroundColor_; }

    void setPlaybackColor(VSTGUI::CColor color) { playbackColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getPlaybackColor() const { return playbackColor_; }

    void setTextColor(VSTGUI::CColor color) { textColor_ = color; }
    [[nodiscard]] VSTGUI::CColor getTextColor() const { return textColor_; }

    // =========================================================================
    // Subclass Layout Support
    // =========================================================================

    /// Set a top offset for the bar area (e.g., for subclass headers)
    /// Default is 0.0f, which preserves existing behavior.
    void setBarAreaTopOffset(float offset) { barAreaTopOffset_ = offset; }

    // =========================================================================
    // Layout Computation (public for testability)
    // =========================================================================

    /// Get the bar area rectangle (the region where bars are drawn)
    [[nodiscard]] VSTGUI::CRect getBarArea() const {
        VSTGUI::CRect vs = getViewSize();
        float top = static_cast<float>(vs.top) + kPhaseOffsetHeight + barAreaTopOffset_;
        float bottom = static_cast<float>(vs.bottom) - kStepLabelHeight - kPlaybackIndicatorHeight;

        if (numSteps_ >= 24 && zoomLevel_ > 1.0f) {
            top += kScrollIndicatorHeight;
        }
        if (euclideanEnabled_) {
            bottom -= kEuclideanDotHeight;
        }

        float left = static_cast<float>(vs.left) + kGridLabelWidth;
        float right = static_cast<float>(vs.right);

        return VSTGUI::CRect(left, top, right, bottom);
    }

    /// Get the rectangle for a specific bar
    [[nodiscard]] VSTGUI::CRect getBarRect(int stepIndex) const {
        VSTGUI::CRect barArea = getBarArea();
        float barAreaWidth = static_cast<float>(barArea.getWidth());
        int steps = getVisibleStepCount();
        if (steps <= 0) return VSTGUI::CRect();

        float barWidth = barAreaWidth / static_cast<float>(steps);
        int visibleIndex = stepIndex - scrollOffset_;
        if (visibleIndex < 0 || visibleIndex >= steps)
            return VSTGUI::CRect();

        float level = stepLevels_[static_cast<size_t>(stepIndex)];
        float barHeight = static_cast<float>(barArea.getHeight());
        float barTop = static_cast<float>(barArea.top) + barHeight * (1.0f - level);
        float barLeft = static_cast<float>(barArea.left) + static_cast<float>(visibleIndex) * barWidth + kBarPadding;
        float barRight = barLeft + barWidth - 2.0f * kBarPadding;

        return VSTGUI::CRect(barLeft, barTop, barRight, static_cast<float>(barArea.bottom));
    }

    /// Get the step index from a point, or -1 if outside
    [[nodiscard]] int getStepFromPoint(const VSTGUI::CPoint& point) const {
        VSTGUI::CRect barArea = getBarArea();
        if (!barArea.pointInside(point)) return -1;

        float barAreaWidth = static_cast<float>(barArea.getWidth());
        int steps = getVisibleStepCount();
        if (steps <= 0) return -1;

        float barWidth = barAreaWidth / static_cast<float>(steps);
        float relX = static_cast<float>(point.x - barArea.left);
        int visibleIndex = static_cast<int>(relX / barWidth);

        if (visibleIndex < 0 || visibleIndex >= steps) return -1;

        int stepIndex = visibleIndex + scrollOffset_;
        if (stepIndex >= numSteps_) return -1;

        return stepIndex;
    }

    /// Get the level from a Y coordinate within the bar area
    [[nodiscard]] float getLevelFromY(float y) const {
        VSTGUI::CRect barArea = getBarArea();
        float barHeight = static_cast<float>(barArea.getHeight());
        if (barHeight <= 0.0f) return 0.0f;

        float relY = static_cast<float>(y - barArea.top);
        float level = 1.0f - (relY / barHeight);
        return std::clamp(level, 0.0f, 1.0f);
    }

    /// Get the color for a given level (FR-002)
    [[nodiscard]] VSTGUI::CColor getColorForLevel(float level) const {
        if (level <= 0.0f) return silentOutlineColor_;
        if (level < 0.40f) return barColorGhost_;
        if (level < 0.80f) return barColorNormal_;
        return barColorAccent_;
    }

    /// Get visible step count based on zoom
    [[nodiscard]] int getVisibleStepCount() const {
        if (zoomLevel_ <= 1.0f) return numSteps_;
        int visible = static_cast<int>(std::ceil(
            static_cast<float>(numSteps_) / zoomLevel_));
        return std::clamp(visible, 1, numSteps_);
    }

    /// Get the playback indicator rectangle
    [[nodiscard]] VSTGUI::CRect getPlaybackIndicatorRect() const {
        if (playbackStep_ < 0 || playbackStep_ >= numSteps_) return VSTGUI::CRect();

        VSTGUI::CRect barArea = getBarArea();
        float barAreaWidth = static_cast<float>(barArea.getWidth());
        int steps = getVisibleStepCount();
        if (steps <= 0) return VSTGUI::CRect();

        int visibleIndex = playbackStep_ - scrollOffset_;
        if (visibleIndex < 0 || visibleIndex >= steps) return VSTGUI::CRect();

        float barWidth = barAreaWidth / static_cast<float>(steps);
        float centerX = static_cast<float>(barArea.left) +
            (static_cast<float>(visibleIndex) + 0.5f) * barWidth;
        float top = static_cast<float>(barArea.bottom);
        if (euclideanEnabled_) top += kEuclideanDotHeight;
        top += kStepLabelHeight;

        float halfWidth = 4.0f;
        return VSTGUI::CRect(centerX - halfWidth, top,
                              centerX + halfWidth, top + kPlaybackIndicatorHeight);
    }

    /// Compute phase offset start step
    [[nodiscard]] int getPhaseStartStep() const {
        if (numSteps_ <= 0) return 0;
        return static_cast<int>(std::round(phaseOffset_ * static_cast<float>(numSteps_)))
            % numSteps_;
    }

    // =========================================================================
    // CControl Overrides
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        VSTGUI::CRect vs = getViewSize();

        // Background
        context->setFillColor(backgroundColor_);
        context->drawRect(vs, VSTGUI::kDrawFilled);

        // Draw zones in order per spec
        drawScrollIndicator(context);
        drawPhaseOffsetIndicator(context);
        drawGridLines(context);
        drawBars(context);
        drawEuclideanDots(context);
        drawStepLabels(context);
        drawPlaybackIndicator(context);

        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {

        if ((buttons & VSTGUI::kLButton) == 0) return VSTGUI::kMouseEventNotHandled;

        int step = getStepFromPoint(where);
        if (step < 0) return VSTGUI::kMouseEventNotHandled;

        // Double-click: reset to 1.0 (FR-007)
        if (buttons.isDoubleClick()) {
            notifyBeginEdit(step);
            stepLevels_[static_cast<size_t>(step)] = 1.0f;
            notifyStepChange(step, 1.0f);
            notifyEndEdit(step);
            if (euclideanEnabled_) isModified_ = true;
            setDirty();
            return VSTGUI::kMouseEventHandled;
        }

        // Alt+click: toggle between 0.0 and 1.0 (FR-008)
        if (buttons.getModifierState() & VSTGUI::kAlt) {
            notifyBeginEdit(step);
            float newLevel = (stepLevels_[static_cast<size_t>(step)] > 0.0f) ? 0.0f : 1.0f;
            stepLevels_[static_cast<size_t>(step)] = newLevel;
            notifyStepChange(step, newLevel);
            notifyEndEdit(step);
            if (euclideanEnabled_) isModified_ = true;
            setDirty();
            return VSTGUI::kMouseEventHandled;
        }

        // Start drag gesture
        isDragging_ = true;
        dirtySteps_.reset();
        preDragLevels_ = stepLevels_;
        fineMode_ = (buttons.getModifierState() & VSTGUI::kShift) != 0;
        dragStartY_ = static_cast<float>(where.y);
        lastDragStep_ = step;

        // Set level from vertical position
        float level = getLevelFromY(static_cast<float>(where.y));
        updateStepLevel(step, level);

        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {

        if (!isDragging_) return VSTGUI::kMouseEventNotHandled;

        // Update fine mode from current modifier state
        fineMode_ = (buttons.getModifierState() & VSTGUI::kShift) != 0;

        int step = getStepFromPoint(where);
        if (step < 0) return VSTGUI::kMouseEventHandled;

        float level = getLevelFromY(static_cast<float>(where.y));

        // Fine mode: 0.1x sensitivity (FR-009)
        if (fineMode_) {
            float rawLevel = getLevelFromY(static_cast<float>(where.y));
            float baseLevelFromDragStart = getLevelFromY(dragStartY_);
            float delta = (rawLevel - baseLevelFromDragStart) * 0.1f;
            level = std::clamp(
                preDragLevels_[static_cast<size_t>(step)] + delta, 0.0f, 1.0f);
        }

        // Paint mode: fill steps between last and current (FR-006)
        if (lastDragStep_ >= 0 && step != lastDragStep_) {
            int from = std::min(lastDragStep_, step);
            int to = std::max(lastDragStep_, step);
            for (int i = from; i <= to; ++i) {
                if (i < numSteps_) {
                    updateStepLevel(i, level);
                }
            }
        } else {
            updateStepLevel(step, level);
        }

        lastDragStep_ = step;
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseUp(
        VSTGUI::CPoint& /*where*/,
        const VSTGUI::CButtonState& /*buttons*/) override {

        if (!isDragging_) return VSTGUI::kMouseEventNotHandled;

        // End edit for all dirty steps (FR-011)
        for (int i = 0; i < kMaxSteps; ++i) {
            if (dirtySteps_.test(static_cast<size_t>(i))) {
                notifyEndEdit(i);
            }
        }

        isDragging_ = false;
        dirtySteps_.reset();
        lastDragStep_ = -1;
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseCancel() override {
        cancelDrag();
        return VSTGUI::kMouseEventHandled;
    }

    int32_t onKeyDown(VstKeyCode& keyCode) override {
        // Escape: cancel drag (FR-010)
        if (keyCode.virt == VKEY_ESCAPE && isDragging_) {
            cancelDrag();
            return 1; // handled
        }
        return -1; // not handled
    }

    bool onWheel([[maybe_unused]] const VSTGUI::CPoint& where,
                 [[maybe_unused]] const VSTGUI::CMouseWheelAxis& axis,
                 const float& distance,
                 const VSTGUI::CButtonState& buttons) override {

        if (numSteps_ < 24) return false;

        if (buttons.getModifierState() & VSTGUI::kControl) {
            // Ctrl+wheel: zoom (FR-034)
            float maxZoom = static_cast<float>(numSteps_) / 4.0f;
            zoomLevel_ = std::clamp(zoomLevel_ + distance * 0.25f, 1.0f, maxZoom);
            clampZoomScroll();
            setDirty();
            return true;
        }

        // Regular wheel: scroll (FR-033)
        scrollOffset_ = std::clamp(
            scrollOffset_ - static_cast<int>(distance * 2.0f),
            0, std::max(0, numSteps_ - getVisibleStepCount()));
        setDirty();
        return true;
    }

    CLASS_METHODS(StepPatternEditor, CControl)

private:
    // =========================================================================
    // Drawing Helpers
    // =========================================================================

    void drawScrollIndicator(VSTGUI::CDrawContext* context) const {
        if (numSteps_ < 24 || zoomLevel_ <= 1.0f) return;

        VSTGUI::CRect vs = getViewSize();
        float indicatorTop = static_cast<float>(vs.top);
        float indicatorLeft = static_cast<float>(vs.left) + kGridLabelWidth;
        float indicatorWidth = static_cast<float>(vs.getWidth()) - kGridLabelWidth;

        // Background track
        VSTGUI::CRect track(indicatorLeft, indicatorTop,
                             indicatorLeft + indicatorWidth,
                             indicatorTop + kScrollIndicatorHeight);
        context->setFillColor(VSTGUI::CColor(30, 30, 33, 255));
        context->drawRect(track, VSTGUI::kDrawFilled);

        // Thumb
        int visible = getVisibleStepCount();
        float thumbWidth = indicatorWidth * static_cast<float>(visible) /
            static_cast<float>(numSteps_);
        float thumbX = indicatorLeft + indicatorWidth *
            static_cast<float>(scrollOffset_) / static_cast<float>(numSteps_);

        VSTGUI::CRect thumb(thumbX, indicatorTop,
                             thumbX + thumbWidth,
                             indicatorTop + kScrollIndicatorHeight);
        context->setFillColor(VSTGUI::CColor(80, 80, 85, 255));
        context->drawRect(thumb, VSTGUI::kDrawFilled);
    }

    void drawPhaseOffsetIndicator(VSTGUI::CDrawContext* context) const {
        if (phaseOffset_ <= 0.0f && numSteps_ > 0) return;

        int startStep = getPhaseStartStep();
        if (startStep < scrollOffset_ || startStep >= scrollOffset_ + getVisibleStepCount())
            return;

        VSTGUI::CRect barArea = getBarArea();
        float barAreaWidth = static_cast<float>(barArea.getWidth());
        int steps = getVisibleStepCount();
        if (steps <= 0) return;

        float barWidth = barAreaWidth / static_cast<float>(steps);
        int visibleIndex = startStep - scrollOffset_;
        float centerX = static_cast<float>(barArea.left) +
            (static_cast<float>(visibleIndex) + 0.5f) * barWidth;

        float triTop = static_cast<float>(barArea.top) - kPhaseOffsetHeight;
        float triBottom = static_cast<float>(barArea.top) - 2.0f;
        float halfWidth = 5.0f;

        // Draw downward-pointing triangle
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path) return;

        path->beginSubpath(VSTGUI::CPoint(centerX - halfWidth, triTop));
        path->addLine(VSTGUI::CPoint(centerX + halfWidth, triTop));
        path->addLine(VSTGUI::CPoint(centerX, triBottom));
        path->closeSubpath();

        context->setFillColor(textColor_);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
    }

    void drawGridLines(VSTGUI::CDrawContext* context) const {
        VSTGUI::CRect barArea = getBarArea();
        float barHeight = static_cast<float>(barArea.getHeight());

        context->setFrameColor(gridColor_);
        context->setLineWidth(1.0);
        context->setLineStyle(VSTGUI::kLineSolid);

        // Grid lines at 0.0, 0.25, 0.50, 0.75, 1.0
        const float gridLevels[] = {0.0f, 0.25f, 0.50f, 0.75f, 1.0f};
        for (float gLevel : gridLevels) {
            float y = static_cast<float>(barArea.top) + barHeight * (1.0f - gLevel);
            context->drawLine(
                VSTGUI::CPoint(barArea.left, y),
                VSTGUI::CPoint(barArea.right, y));
        }

        // Labels "0.0" and "1.0" right-aligned in the label area
        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 9.0);
        context->setFont(font);
        context->setFontColor(textColor_);

        VSTGUI::CRect topLabelRect(
            getViewSize().left, static_cast<float>(barArea.top) - 6.0f,
            static_cast<float>(barArea.left) - 2.0f,
            static_cast<float>(barArea.top) + 6.0f);
        context->drawString(VSTGUI::UTF8String("1.0"), topLabelRect,
                             VSTGUI::kRightText, true);

        VSTGUI::CRect bottomLabelRect(
            getViewSize().left, static_cast<float>(barArea.bottom) - 6.0f,
            static_cast<float>(barArea.left) - 2.0f,
            static_cast<float>(barArea.bottom) + 6.0f);
        context->drawString(VSTGUI::UTF8String("0.0"), bottomLabelRect,
                             VSTGUI::kRightText, true);
    }

    void drawBars(VSTGUI::CDrawContext* context) const {
        int visibleStart = scrollOffset_;
        int visibleEnd = std::min(scrollOffset_ + getVisibleStepCount(), numSteps_);

        for (int i = visibleStart; i < visibleEnd; ++i) {
            float level = stepLevels_[static_cast<size_t>(i)];
            VSTGUI::CRect bar = getBarRect(i);

            if (bar.isEmpty()) continue;

            if (level <= 0.0f) {
                // Silent step: outline only
                VSTGUI::CRect fullBar = getBarRect(i);
                VSTGUI::CRect barArea = getBarArea();
                fullBar.top = barArea.top;
                context->setFrameColor(silentOutlineColor_);
                context->setLineWidth(1.0);
                context->drawRect(fullBar, VSTGUI::kDrawStroked);
            } else {
                // Filled bar with color based on level
                VSTGUI::CColor barColor = getColorForLevel(level);
                context->setFillColor(barColor);
                context->drawRect(bar, VSTGUI::kDrawFilled);
            }
        }
    }

    void drawEuclideanDots(VSTGUI::CDrawContext* context) const {
        if (!euclideanEnabled_) return;

        VSTGUI::CRect barArea = getBarArea();
        float dotTop = static_cast<float>(barArea.bottom) + 1.0f;
        float dotCenterY = dotTop + kEuclideanDotHeight / 2.0f;

        float barAreaWidth = static_cast<float>(barArea.getWidth());
        int steps = getVisibleStepCount();
        if (steps <= 0) return;

        float barWidth = barAreaWidth / static_cast<float>(steps);
        float dotRadius = 3.0f;

        int visibleStart = scrollOffset_;
        int visibleEnd = std::min(scrollOffset_ + steps, numSteps_);

        for (int i = visibleStart; i < visibleEnd; ++i) {
            int visibleIndex = i - scrollOffset_;
            float centerX = static_cast<float>(barArea.left) +
                (static_cast<float>(visibleIndex) + 0.5f) * barWidth;

            bool isHit = Krate::DSP::EuclideanPattern::isHit(
                euclideanPattern_, i, numSteps_);

            if (isHit) {
                // Filled dot for hit
                context->setFillColor(VSTGUI::CColor(220, 170, 60, 255));
                VSTGUI::CRect dotRect(centerX - dotRadius, dotCenterY - dotRadius,
                                       centerX + dotRadius, dotCenterY + dotRadius);
                context->drawEllipse(dotRect, VSTGUI::kDrawFilled);
            } else {
                // Empty dot for rest
                context->setFrameColor(VSTGUI::CColor(50, 50, 55, 255));
                context->setLineWidth(1.0);
                VSTGUI::CRect dotRect(centerX - dotRadius, dotCenterY - dotRadius,
                                       centerX + dotRadius, dotCenterY + dotRadius);
                context->drawEllipse(dotRect, VSTGUI::kDrawStroked);
            }
        }
    }

    void drawStepLabels(VSTGUI::CDrawContext* context) const {
        VSTGUI::CRect barArea = getBarArea();
        float labelTop = static_cast<float>(barArea.bottom);
        if (euclideanEnabled_) labelTop += kEuclideanDotHeight;

        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 9.0);
        context->setFont(font);
        context->setFontColor(textColor_);

        float barAreaWidth = static_cast<float>(barArea.getWidth());
        int steps = getVisibleStepCount();
        if (steps <= 0) return;

        float barWidth = barAreaWidth / static_cast<float>(steps);

        int visibleStart = scrollOffset_;
        int visibleEnd = std::min(scrollOffset_ + steps, numSteps_);

        for (int i = visibleStart; i < visibleEnd; ++i) {
            // Label every 4th step (1-indexed: 1, 5, 9, ...)
            if ((i % 4) != 0) continue;

            int visibleIndex = i - scrollOffset_;
            float centerX = static_cast<float>(barArea.left) +
                (static_cast<float>(visibleIndex) + 0.5f) * barWidth;

            char label[16];
            snprintf(label, sizeof(label), "%d", i + 1);

            VSTGUI::CRect labelRect(centerX - 10.0f, labelTop,
                                     centerX + 10.0f, labelTop + kStepLabelHeight);
            context->drawString(VSTGUI::UTF8String(label), labelRect,
                                 VSTGUI::kCenterText, true);
        }
    }

    void drawPlaybackIndicator(VSTGUI::CDrawContext* context) const {
        if (!isPlaying_ || playbackStep_ < 0 || playbackStep_ >= numSteps_) return;

        VSTGUI::CRect indRect = getPlaybackIndicatorRect();
        if (indRect.isEmpty()) return;

        float centerX = static_cast<float>(indRect.left + indRect.right) / 2.0f;
        float halfWidth = 4.0f;

        // Upward-pointing triangle
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path) return;

        path->beginSubpath(VSTGUI::CPoint(centerX - halfWidth, indRect.bottom));
        path->addLine(VSTGUI::CPoint(centerX + halfWidth, indRect.bottom));
        path->addLine(VSTGUI::CPoint(centerX, indRect.top));
        path->closeSubpath();

        context->setFillColor(playbackColor_);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
    }

    // =========================================================================
    // Interaction Helpers
    // =========================================================================

    void updateStepLevel(int step, float level) {
        if (step < 0 || step >= numSteps_) return;

        auto idx = static_cast<size_t>(step);

        // Begin edit on first touch of this step
        if (!dirtySteps_.test(idx)) {
            notifyBeginEdit(step);
            dirtySteps_.set(idx);
        }

        stepLevels_[idx] = std::clamp(level, 0.0f, 1.0f);
        notifyStepChange(step, stepLevels_[idx]);

        if (euclideanEnabled_) isModified_ = true;
        setDirty();
    }

    void cancelDrag() {
        if (!isDragging_) return;

        // Revert to pre-drag levels (FR-010)
        stepLevels_ = preDragLevels_;

        // Notify changes for reverted steps and end edit
        for (int i = 0; i < kMaxSteps; ++i) {
            if (dirtySteps_.test(static_cast<size_t>(i))) {
                notifyStepChange(i, stepLevels_[static_cast<size_t>(i)]);
                notifyEndEdit(i);
            }
        }

        isDragging_ = false;
        dirtySteps_.reset();
        lastDragStep_ = -1;
        setDirty();
    }

    // =========================================================================
    // Parameter Notification Helpers
    // =========================================================================

    void notifyBeginEdit(int step) {
        if (beginEditCallback_ && stepLevelBaseParamId_ > 0) {
            beginEditCallback_(stepLevelBaseParamId_ + static_cast<uint32_t>(step));
        }
    }

    void notifyEndEdit(int step) {
        if (endEditCallback_ && stepLevelBaseParamId_ > 0) {
            endEditCallback_(stepLevelBaseParamId_ + static_cast<uint32_t>(step));
        }
    }

    void notifyStepChange(int step, float level) {
        if (paramCallback_ && stepLevelBaseParamId_ > 0) {
            paramCallback_(stepLevelBaseParamId_ + static_cast<uint32_t>(step), level);
        }
    }

    // =========================================================================
    // Euclidean Helpers
    // =========================================================================

    void regenerateEuclideanPattern() {
        euclideanPattern_ = Krate::DSP::EuclideanPattern::generate(
            euclideanHits_, numSteps_, euclideanRotation_);
    }

    /// Apply Euclidean pattern with smart level preservation (FR-021)
    void applyEuclideanPattern() {
        for (int i = 0; i < numSteps_; ++i) {
            bool isHit = Krate::DSP::EuclideanPattern::isHit(
                euclideanPattern_, i, numSteps_);
            auto idx = static_cast<size_t>(i);
            if (isHit) {
                // Rest-to-hit: set 1.0 only if currently 0.0
                if (stepLevels_[idx] <= 0.0f) {
                    stepLevels_[idx] = 1.0f;
                    notifyStepChange(i, 1.0f);
                }
            } else {
                // Hit-to-rest: preserve level (bar still visible if > 0)
                // Do NOT zero out -- the level remains so the "ghost note"
                // concept (empty dot with bar) works per FR-020
            }
        }
        isModified_ = false;
    }

    // =========================================================================
    // Preset Helper
    // =========================================================================

    template<typename Func>
    void applyPreset(Func levelFunc) {
        for (int i = 0; i < numSteps_; ++i) {
            float newLevel = std::clamp(levelFunc(i, numSteps_), 0.0f, 1.0f);
            notifyBeginEdit(i);
            stepLevels_[static_cast<size_t>(i)] = newLevel;
            notifyStepChange(i, newLevel);
            notifyEndEdit(i);
        }
        if (euclideanEnabled_) isModified_ = true;
        setDirty();
    }

    // =========================================================================
    // Zoom/Scroll Helpers
    // =========================================================================

    void clampZoomScroll() {
        float maxZoom = static_cast<float>(numSteps_) / 4.0f;
        zoomLevel_ = std::clamp(zoomLevel_, 1.0f, std::max(1.0f, maxZoom));
        int visible = getVisibleStepCount();
        scrollOffset_ = std::clamp(scrollOffset_, 0,
            std::max(0, numSteps_ - visible));
    }

    // =========================================================================
    // State
    // =========================================================================

    // Step data
    std::array<float, kMaxSteps> stepLevels_{};
    int numSteps_ = 16;

    // Playback
    int playbackStep_ = -1;
    bool isPlaying_ = false;

    // Phase offset
    float phaseOffset_ = 0.0f;

    // Bar area top offset (for subclass headers, e.g., ArpLaneEditor)
    float barAreaTopOffset_ = 0.0f;

    // Euclidean mode
    bool euclideanEnabled_ = false;
    int euclideanHits_ = 4;
    int euclideanRotation_ = 0;
    uint32_t euclideanPattern_ = 0;
    bool isModified_ = false;

    // Drag state
    bool isDragging_ = false;
    std::bitset<kMaxSteps> dirtySteps_;
    std::array<float, kMaxSteps> preDragLevels_{};
    float dragStartY_ = 0.0f;
    bool fineMode_ = false;
    int lastDragStep_ = -1;

    // Zoom/scroll
    float zoomLevel_ = 1.0f;
    int scrollOffset_ = 0;
    int visibleSteps_ = kMaxSteps;

    // Colors
    VSTGUI::CColor barColorAccent_{220, 170, 60, 255};
    VSTGUI::CColor barColorNormal_{80, 140, 200, 255};
    VSTGUI::CColor barColorGhost_{60, 90, 120, 255};
    VSTGUI::CColor silentOutlineColor_{50, 50, 55, 255};
    VSTGUI::CColor gridColor_{255, 255, 255, 30};
    VSTGUI::CColor backgroundColor_{35, 35, 38, 255};
    VSTGUI::CColor playbackColor_{255, 200, 80, 255};
    VSTGUI::CColor textColor_{180, 180, 185, 255};

    // Callbacks
    ParameterCallback paramCallback_;
    EditCallback beginEditCallback_;
    EditCallback endEditCallback_;
    uint32_t stepLevelBaseParamId_ = 0;

    // Timer
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> refreshTimer_;

    // Random
    std::mt19937 rng_;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct StepPatternEditorCreator : VSTGUI::ViewCreatorAdapter {
    StepPatternEditorCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override { return "StepPatternEditor"; }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Step Pattern Editor";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new StepPatternEditor(VSTGUI::CRect(0, 0, 500, 200), nullptr, -1);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override {
        auto* editor = dynamic_cast<StepPatternEditor*>(view);
        if (!editor)
            return false;

        VSTGUI::CColor color;
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("bar-color-accent"), color, description))
            editor->setBarColorAccent(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("bar-color-normal"), color, description))
            editor->setBarColorNormal(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("bar-color-ghost"), color, description))
            editor->setBarColorGhost(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("silent-outline-color"), color, description))
            editor->setSilentOutlineColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("grid-color"), color, description))
            editor->setGridColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("background-color"), color, description))
            editor->setEditorBackgroundColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("playback-color"), color, description))
            editor->setPlaybackColor(color);
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("text-color"), color, description))
            editor->setTextColor(color);

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("bar-color-accent");
        attributeNames.emplace_back("bar-color-normal");
        attributeNames.emplace_back("bar-color-ghost");
        attributeNames.emplace_back("silent-outline-color");
        attributeNames.emplace_back("grid-color");
        attributeNames.emplace_back("background-color");
        attributeNames.emplace_back("playback-color");
        attributeNames.emplace_back("text-color");
        return true;
    }

    AttrType getAttributeType(const std::string& attributeName) const override {
        if (attributeName == "bar-color-accent") return kColorType;
        if (attributeName == "bar-color-normal") return kColorType;
        if (attributeName == "bar-color-ghost") return kColorType;
        if (attributeName == "silent-outline-color") return kColorType;
        if (attributeName == "grid-color") return kColorType;
        if (attributeName == "background-color") return kColorType;
        if (attributeName == "playback-color") return kColorType;
        if (attributeName == "text-color") return kColorType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override {
        auto* editor = dynamic_cast<StepPatternEditor*>(view);
        if (!editor)
            return false;

        if (attributeName == "bar-color-accent") {
            VSTGUI::UIViewCreator::colorToString(
                editor->getBarColorAccent(), stringValue, desc);
            return true;
        }
        if (attributeName == "bar-color-normal") {
            VSTGUI::UIViewCreator::colorToString(
                editor->getBarColorNormal(), stringValue, desc);
            return true;
        }
        if (attributeName == "bar-color-ghost") {
            VSTGUI::UIViewCreator::colorToString(
                editor->getBarColorGhost(), stringValue, desc);
            return true;
        }
        if (attributeName == "silent-outline-color") {
            VSTGUI::UIViewCreator::colorToString(
                editor->getSilentOutlineColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "grid-color") {
            VSTGUI::UIViewCreator::colorToString(
                editor->getGridColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "background-color") {
            VSTGUI::UIViewCreator::colorToString(
                editor->getEditorBackgroundColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "playback-color") {
            VSTGUI::UIViewCreator::colorToString(
                editor->getPlaybackColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "text-color") {
            VSTGUI::UIViewCreator::colorToString(
                editor->getTextColor(), stringValue, desc);
            return true;
        }
        return false;
    }
};

inline StepPatternEditorCreator gStepPatternEditorCreator;

} // namespace Krate::Plugins
