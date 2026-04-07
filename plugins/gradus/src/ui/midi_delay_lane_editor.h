#pragma once

// ==============================================================================
// MidiDelayLaneEditor — Multi-Knob Grid Lane Editor (IArpLane)
// ==============================================================================
// Per-step grid editor for the MIDI Delay lane. Uses CViewContainer to host
// actual ArcKnob and ToggleButton controls for each step's 6 parameters.
//
// Follows the ArpChordLane pattern for IArpLane: CControl + ArpLaneHeader
// composition. The body area contains a scrollable grid of real VSTGUI controls.
//
// Scrolling: when numSteps > kMaxVisibleSteps (10), column width is frozen and
// the grid scrolls horizontally via mouse wheel or scrollbar drag.
// ==============================================================================

#include "../plugin_ids.h"
#include "ui/arp_lane.h"
#include "ui/arp_lane_header.h"
#include "ui/arc_knob.h"
#include "ui/toggle_button.h"

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/controls/ccontrol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

namespace Gradus {

/// Transparent overlay that draws the playhead/trail highlight on top of knobs.
/// Added as the last child of MidiDelayLaneEditor so it renders after all controls.
class PlayheadOverlayView : public VSTGUI::CView {
public:
    explicit PlayheadOverlayView(const VSTGUI::CRect& size) : CView(size) {
        setMouseEnabled(false);  // Click-through
    }

    void setPlayheadStep(int step) { playheadStep_ = step; invalid(); }
    void setTrailState(const int32_t steps[4], const float alphas[4]) {
        for (int i = 0; i < 4; ++i) { trailSteps_[i] = steps[i]; trailAlphas_[i] = alphas[i]; }
        invalid();
    }
    void setScrollOffset(float px) { scrollOffsetPx_ = px; invalid(); }
    void setGridParams(int numSteps, float colW, float bodyTop, float knobAreaH, float bodyLeft) {
        numSteps_ = numSteps; colW_ = colW; bodyTop_ = bodyTop;
        knobAreaH_ = knobAreaH; bodyLeft_ = bodyLeft;
    }
    void setAccentColor(VSTGUI::CColor c) { accentColor_ = c; }

    void draw(VSTGUI::CDrawContext* context) override {
        if (playheadStep_ < 0 && trailSteps_[0] < 0) { setDirty(false); return; }

        context->setDrawMode(VSTGUI::kAntiAliasing);

        auto drawCol = [&](int step, uint8_t alpha) {
            if (step < 0 || step >= numSteps_) return;
            float x0 = bodyLeft_ + static_cast<float>(step) * colW_ - scrollOffsetPx_;
            float xEnd = x0 + colW_;
            if (xEnd < bodyLeft_ || x0 > static_cast<float>(getViewSize().getWidth())) return;
            VSTGUI::CRect r(std::max(x0, bodyLeft_), bodyTop_, xEnd, bodyTop_ + knobAreaH_);
            VSTGUI::CColor c = accentColor_; c.alpha = alpha;
            context->setFillColor(c);
            context->drawRect(r, VSTGUI::kDrawFilled);
        };

        drawCol(playheadStep_, 0x30);
        for (int t = 0; t < 4; ++t)
            drawCol(trailSteps_[t], static_cast<uint8_t>(trailAlphas_[t] * 40.0f));

        setDirty(false);
    }

    CLASS_METHODS(PlayheadOverlayView, CView)

private:
    int playheadStep_ = -1;
    std::array<int, 4> trailSteps_ = {-1, -1, -1, -1};
    std::array<float, 4> trailAlphas_ = {0, 0, 0, 0};
    float scrollOffsetPx_ = 0.0f;
    int numSteps_ = 16;
    float colW_ = 37.0f;
    float bodyTop_ = 16.0f;
    float knobAreaH_ = 400.0f;
    float bodyLeft_ = 40.0f;
    VSTGUI::CColor accentColor_{0xD4, 0xA8, 0x56, 0xFF};
};

class MidiDelayLaneEditor : public VSTGUI::CViewContainer,
                            public Krate::Plugins::IArpLane,
                            public VSTGUI::IControlListener {
public:
    // =========================================================================
    // Types & Constants
    // =========================================================================

    enum class KnobRow : int {
        kActive = 0,     // Power toggle: on/off per step
        kTimeMode,       // Sync toggle: Free / Synced
        kDelayTime,
        kFeedback,
        kVelDecay,
        kPitchShift,
        kGateScale,
        kCount
    };

    static constexpr int kRowCount = static_cast<int>(KnobRow::kCount);
    static constexpr int kMaxSteps = 32;
    static constexpr float kLeftMargin = 40.0f;
    static constexpr int kMaxVisibleSteps = 10;
    static constexpr float kStepNumberBarH = 14.0f;
    static constexpr float kScrollBarH = 8.0f;

    using ParameterCallback = std::function<void(uint32_t paramId, float normalizedValue)>;
    using EditCallback = std::function<void(uint32_t paramId)>;

    // =========================================================================
    // Construction
    // =========================================================================

    explicit MidiDelayLaneEditor(const VSTGUI::CRect& size)
        : CViewContainer(size)
    {
        setBackgroundColor({0x16, 0x16, 0x1C, 0xC0});
        header_.setLaneName("DELAY");
        header_.setAccentColor(accentColor_);
        // Hide Invert (bit 0) — doesn't apply to multi-param grid.
        // Keep ShiftLeft(1), ShiftRight(2), Randomize(3).
        header_.setTransformMask(0x0E);

        for (int s = 0; s < kMaxSteps; ++s) {
            stepValues_[s][static_cast<int>(KnobRow::kActive)] = 0.0f;
            stepValues_[s][static_cast<int>(KnobRow::kTimeMode)] = 1.0f;
            stepValues_[s][static_cast<int>(KnobRow::kDelayTime)] = 10.0f / 29.0f;
            stepValues_[s][static_cast<int>(KnobRow::kFeedback)] = 3.0f / 16.0f;
            stepValues_[s][static_cast<int>(KnobRow::kVelDecay)] = 0.5f;
            stepValues_[s][static_cast<int>(KnobRow::kPitchShift)] = 0.5f;
            stepValues_[s][static_cast<int>(KnobRow::kGateScale)] = (1.0f - 0.1f) / 1.9f;
        }
    }

    // =========================================================================
    // Step Value API
    // =========================================================================

    void setStepValue(int step, KnobRow row, float normalized) {
        if (step >= 0 && step < kMaxSteps && static_cast<int>(row) < kRowCount) {
            stepValues_[step][static_cast<int>(row)] = std::clamp(normalized, 0.0f, 1.0f);
            // Update the corresponding control if it exists
            updateControlValue(step, row);
            // Show/hide column controls when Active toggle changes programmatically
            if (row == KnobRow::kActive)
                updateColumnVisibility(step, normalized >= 0.5f);
        }
    }

    [[nodiscard]] float getStepValue(int step, KnobRow row) const {
        if (step >= 0 && step < kMaxSteps && static_cast<int>(row) < kRowCount)
            return stepValues_[step][static_cast<int>(row)];
        return 0.0f;
    }

    void setNumSteps(int count) {
        int newCount = std::clamp(count, 1, kMaxSteps);
        numSteps_ = newCount;
        header_.setNumSteps(numSteps_);
        // Only rebuild if we have a valid size (attached to parent)
        if (getViewSize().getWidth() > 1 && getViewSize().getHeight() > 1) {
            rebuildControls();
        }
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    void setAccentColor(const VSTGUI::CColor& color) {
        accentColor_ = color;
        header_.setAccentColor(color);
    }

    void setLengthParamId(uint32_t paramId) { header_.setLengthParamId(paramId); }
    void setPlayheadParamId(uint32_t paramId) { playheadParamId_ = paramId; }

    void setParameterCallback(ParameterCallback cb) { paramCallback_ = std::move(cb); }
    void setBeginEditCallback(EditCallback cb) { beginEditCallback_ = std::move(cb); }
    void setEndEditCallback(EditCallback cb) { endEditCallback_ = std::move(cb); }

    void setLengthParamCallback(std::function<void(uint32_t, float)> cb) {
        header_.setLengthParamCallback(std::move(cb));
    }

    // =========================================================================
    // IControlListener (receives events from child ArcKnobs/ToggleButtons)
    // =========================================================================

    void valueChanged(VSTGUI::CControl* control) override {
        auto tag = static_cast<uint32_t>(control->getTag());
        float value = control->getValueNormalized();

        // Map tag back to step/row and update our cache
        auto [step, row] = tagToStepRow(tag);
        if (step >= 0 && step < kMaxSteps && static_cast<int>(row) < kRowCount) {
            // Snap TIME knob to note value grid when SYNC is on
            if (row == KnobRow::kDelayTime) {
                bool synced = stepValues_[step][static_cast<int>(KnobRow::kTimeMode)] >= 0.5f;
                if (synced) {
                    value = std::round(value * 29.0f) / 29.0f;
                    control->setValue(value);
                }
            }
            // Single path for cache + visibility (setStepValue handles Active toggle)
            setStepValue(step, row, value);
        }

        if (paramCallback_) paramCallback_(tag, value);
    }

    void controlBeginEdit(VSTGUI::CControl* control) override {
        if (beginEditCallback_)
            beginEditCallback_(static_cast<uint32_t>(control->getTag()));
    }

    void controlEndEdit(VSTGUI::CControl* control) override {
        if (endEditCallback_)
            endEditCallback_(static_cast<uint32_t>(control->getTag()));
    }

    // =========================================================================
    // IArpLane Interface
    // =========================================================================

    VSTGUI::CView* getView() override { return this; }

    [[nodiscard]] float getExpandedHeight() const override { return expandedHeight_; }
    [[nodiscard]] float getCollapsedHeight() const override {
        return Krate::Plugins::ArpLaneHeader::kHeight;
    }
    [[nodiscard]] bool isCollapsed() const override { return header_.isCollapsed(); }
    void setCollapseVisible(bool visible) override { header_.setCollapseVisible(visible); }

    void setSpeedMultiplier(float speed) override { header_.setSpeedMultiplier(speed); }
    void setSpeedParamId(uint32_t id) override { header_.setSpeedParamId(id); }
    void setSpeedParamCallback(std::function<void(uint32_t, float)> cb) override {
        header_.setSpeedParamCallback(std::move(cb));
    }

    void setCollapsed(bool collapsed) override {
        bool wasCollapsed = header_.isCollapsed();
        header_.setCollapsed(collapsed);
        if (collapsed != wasCollapsed && collapseCallback_) collapseCallback_();
        setDirty(true);
    }

    void setPlayheadStep(int32_t step) override {
        playheadStep_ = step;
        if (playheadOverlay_) playheadOverlay_->setPlayheadStep(step);
    }

    void setLength(int32_t length) override { setNumSteps(static_cast<int>(length)); }

    void setCollapseCallback(std::function<void()> cb) override {
        collapseCallback_ = std::move(cb);
    }

    void setTrailSteps(const int32_t steps[4], const float alphas[4]) override {
        for (int i = 0; i < Krate::Plugins::PlayheadTrailState::kTrailLength; ++i) {
            trailState_.steps[i] = steps[i];
            trailAlphas_[i] = alphas[i];
        }
        if (playheadOverlay_) playheadOverlay_->setTrailState(steps, alphas);
    }

    void setSkippedStep(int32_t step) override {
        trailState_.markSkipped(step);
        if (playheadOverlay_) playheadOverlay_->setPlayheadStep(playheadStep_);
    }
    void clearOverlays() override {
        trailState_.clear();
        if (playheadOverlay_) {
            playheadOverlay_->setPlayheadStep(-1);
            int32_t empty[4] = {-1,-1,-1,-1};
            float zeros[4] = {0,0,0,0};
            playheadOverlay_->setTrailState(empty, zeros);
        }
    }

    [[nodiscard]] int32_t getActiveLength() const override { return numSteps_; }

    [[nodiscard]] float getNormalizedStepValue(int32_t step) const override {
        return getStepValue(step, KnobRow::kFeedback);
    }

    void setNormalizedStepValue(int32_t step, float value) override {
        setStepValue(step, KnobRow::kFeedback, value);
    }

    [[nodiscard]] int32_t getLaneTypeId() const override { return 8; }

    void setTransformCallback(TransformCallback cb) override {
        transformCallback_ = cb;
        header_.setTransformCallback(
            [cb](Krate::Plugins::TransformType type) {
                if (cb) cb(static_cast<int>(type));
            });
    }

    void setCopyPasteCallbacks(CopyCallback copy, PasteCallback paste) override {
        copyCallback_ = std::move(copy);
        pasteCallback_ = std::move(paste);
    }

    void setPasteEnabled(bool enabled) override { pasteEnabled_ = enabled; }
    void setEuclideanOverlay(int, int, int, bool) override {}

    // =========================================================================
    // Drawing
    // =========================================================================

    void drawBackgroundRect(VSTGUI::CDrawContext* context,
                            const VSTGUI::CRect& /*updateRect*/) override
    {
        // NOTE: CViewContainer::drawRect applies a transform so that
        // drawBackgroundRect operates in container-local coords (0,0 origin).
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        float w = static_cast<float>(getViewSize().getWidth());
        float h = static_cast<float>(getViewSize().getHeight());

        // Background
        VSTGUI::CRect localRect(0, 0, w, h);
        context->setFillColor(getBackgroundColor());
        context->drawRect(localRect, VSTGUI::kDrawFilled);

        // Header (local coords)
        float headerH = Krate::Plugins::ArpLaneHeader::kHeight;
        VSTGUI::CRect headerRect(0, 0, w, headerH);
        header_.setNumSteps(numSteps_);
        header_.draw(context, headerRect);

        if (header_.isCollapsed()) return;

        float bodyTop = headerH;
        float bodyLeft = kLeftMargin;
        float bodyW = w - kLeftMargin;
        bool needsScroll = numSteps_ > kMaxVisibleSteps;
        float bottomReserved = kStepNumberBarH + (needsScroll ? kScrollBarH : 0.0f);
        float knobAreaH = h - bodyTop - bottomReserved;
        float colW = bodyW / static_cast<float>(std::min(numSteps_, kMaxVisibleSteps));
        float rowH = knobAreaH / static_cast<float>(kRowCount);

        // Row labels in left margin
        static constexpr const char* kRowLabels[kRowCount] = {
            "ACTIVE", "SYNC", "TIME", "FB", "DECAY", "PITCH", "GATE"
        };
        context->setFont(VSTGUI::kNormalFontVerySmall);
        context->setFontColor({0x80, 0x80, 0x88, 0xA0});
        for (int row = 0; row < kRowCount; ++row) {
            float yMid = bodyTop + (static_cast<float>(row) + 0.5f) * rowH;
            VSTGUI::CRect labelRect(2, yMid - 6, kLeftMargin - 2, yMid + 6);
            context->drawString(kRowLabels[row], labelRect, VSTGUI::kRightText);
        }

        // Column separators
        for (int step = 0; step < numSteps_; ++step) {
            float xEnd = bodyLeft + static_cast<float>(step + 1) * colW - scrollOffsetPx_;
            if (xEnd < bodyLeft || xEnd > w) continue;
            context->setFrameColor({0x30, 0x30, 0x38, 0xFF});
            context->setLineWidth(0.5);
            context->drawLine({xEnd, bodyTop}, {xEnd, bodyTop + knobAreaH});
        }

        // Row separators
        context->setFrameColor({0x28, 0x28, 0x30, 0xFF});
        for (int row = 1; row < kRowCount; ++row) {
            float y = bodyTop + static_cast<float>(row) * rowH;
            context->drawLine({bodyLeft, y}, {w, y});
        }

        // Step number bar
        float numBarTop = h - bottomReserved;
        VSTGUI::CRect numBarBg(bodyLeft, numBarTop, w, numBarTop + kStepNumberBarH);
        context->setFillColor({0x12, 0x12, 0x16, 0xFF});
        context->drawRect(numBarBg, VSTGUI::kDrawFilled);

        context->setFont(VSTGUI::kNormalFontVerySmall);
        for (int step = 0; step < numSteps_; ++step) {
            float x0 = bodyLeft + static_cast<float>(step) * colW - scrollOffsetPx_;
            if (x0 + colW < bodyLeft || x0 > w) continue;
            context->setFontColor(step == playheadStep_
                ? accentColor_ : VSTGUI::CColor{0x70, 0x70, 0x78, 0xFF});
            char numBuf[4];
            snprintf(numBuf, sizeof(numBuf), "%d", step + 1);
            VSTGUI::CRect numRect(x0, numBarTop, x0 + colW, numBarTop + kStepNumberBarH);
            context->drawString(numBuf, numRect, VSTGUI::kCenterText);
        }

        // Scrollbar
        if (needsScroll) {
            float totalW = colW * static_cast<float>(numSteps_);
            float maxScroll = std::max(0.0f, totalW - bodyW);
            float sbTop = numBarTop + kStepNumberBarH;
            VSTGUI::CRect trackRect(bodyLeft, sbTop, w, sbTop + kScrollBarH);
            context->setFillColor({0x20, 0x20, 0x28, 0xFF});
            context->drawRect(trackRect, VSTGUI::kDrawFilled);

            float thumbRatio = bodyW / totalW;
            float thumbW = std::max(20.0f, bodyW * thumbRatio);
            float thumbTrackW = bodyW - thumbW;
            float thumbX = bodyLeft + (maxScroll > 0 ? (scrollOffsetPx_ / maxScroll) * thumbTrackW : 0);
            VSTGUI::CRect thumbRect(thumbX, sbTop + 1, thumbX + thumbW, sbTop + kScrollBarH - 1);
            context->setFillColor(scrollbarDragging_
                ? accentColor_ : VSTGUI::CColor{0x50, 0x50, 0x58, 0xFF});
            context->drawRect(thumbRect, VSTGUI::kDrawFilled);
        }
    }

    // =========================================================================
    // Mouse (header + scrollbar — knob interaction handled by child controls)
    // =========================================================================

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        // CViewContainer::onMouseDown receives coords in parent-frame space.
        auto vs = getViewSize();
        float headerH = Krate::Plugins::ArpLaneHeader::kHeight;
        VSTGUI::CRect headerRect(vs.left, vs.top, vs.right, vs.top + headerH);

        // Convert to local coords for header hit test (header expects local)
        VSTGUI::CPoint localWhere(where.x - vs.left, where.y - vs.top);
        VSTGUI::CRect localHeaderRect(0, 0, vs.getWidth(), headerH);

        // Header right-click (context menu)
        if (buttons.isRightButton() && headerRect.pointInside(where)) {
            if (header_.handleRightClick(localWhere, localHeaderRect, getFrame()))
                return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
        }

        // Header left-click (step count, speed dropdown, collapse)
        bool wasCollapsed = isCollapsed();
        // framePoint needs to be in frame coords for popup positioning
        VSTGUI::CPoint framePoint(where);
        if (getFrame()) {
            // where is in parent coords; convert to frame coords
            if (auto* parent = getParentView()) {
                parent->localToFrame(framePoint);
            }
        }
        if (header_.handleMouseDown(localWhere, framePoint, localHeaderRect, getFrame())) {
            if (isCollapsed() != wasCollapsed && collapseCallback_) collapseCallback_();
            setDirty(true);
            return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
        }

        // Scrollbar drag (local coords)
        if (isInScrollbar(localWhere)) {
            scrollbarDragging_ = true;
            scrollDragStartX_ = static_cast<float>(where.x);  // parent coords for drag delta
            scrollDragStartOffset_ = scrollOffsetPx_;
            return VSTGUI::kMouseEventHandled;
        }

        // Let CViewContainer route to child ArcKnob/ToggleButton controls
        return CViewContainer::onMouseDown(where, buttons);
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        if (scrollbarDragging_ && (buttons & VSTGUI::kLButton)) {
            float dx = static_cast<float>(where.x) - scrollDragStartX_;
            float bodyW = static_cast<float>(getViewSize().getWidth()) - kLeftMargin;
            float colW = bodyW / static_cast<float>(std::min(numSteps_, kMaxVisibleSteps));
            float totalW = colW * static_cast<float>(numSteps_);
            float maxScroll = std::max(0.0f, totalW - bodyW);
            float thumbRatio = bodyW / totalW;
            float thumbW = std::max(20.0f, bodyW * thumbRatio);
            float thumbTrackW = bodyW - thumbW;
            if (thumbTrackW > 0) {
                scrollOffsetPx_ = std::clamp(
                    scrollDragStartOffset_ + dx * (maxScroll / thumbTrackW), 0.0f, maxScroll);
                repositionControls();
                if (playheadOverlay_) playheadOverlay_->setScrollOffset(scrollOffsetPx_);
                setDirty(true);
            }
            return VSTGUI::kMouseEventHandled;
        }
        return CViewContainer::onMouseMoved(where, buttons);
    }

    VSTGUI::CMouseEventResult onMouseUp(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        if (scrollbarDragging_) {
            scrollbarDragging_ = false;
            setDirty(true);
            return VSTGUI::kMouseEventHandled;
        }
        return CViewContainer::onMouseUp(where, buttons);
    }

    void onMouseWheelEvent(VSTGUI::MouseWheelEvent& event) override
    {
        if (numSteps_ <= kMaxVisibleSteps) {
            CViewContainer::onMouseWheelEvent(event);
            return;
        }

        float bodyW = static_cast<float>(getViewSize().getWidth()) - kLeftMargin;
        float colW = bodyW / static_cast<float>(kMaxVisibleSteps);
        float totalW = colW * static_cast<float>(numSteps_);
        float maxScroll = std::max(0.0f, totalW - bodyW);

        // Use deltaX if available, otherwise deltaY for vertical scroll wheels
        float delta = (event.deltaX != 0.0f) ? event.deltaX : event.deltaY;
        scrollOffsetPx_ = std::clamp(scrollOffsetPx_ - delta * colW, 0.0f, maxScroll);
        repositionControls();
        if (playheadOverlay_) playheadOverlay_->setScrollOffset(scrollOffsetPx_);
        setDirty(true);
        event.consumed = true;
    }

    CLASS_METHODS(MidiDelayLaneEditor, CViewContainer)

private:
    // =========================================================================
    // Control Management
    // =========================================================================

    /// Destroy all child controls and recreate for current numSteps_.
    void rebuildControls()
    {
        playheadOverlay_ = nullptr;  // Will be destroyed by removeAll
        removeAll();
        controlPtrs_.clear();

        auto vs = getViewSize();
        float bodyW = static_cast<float>(vs.getWidth()) - kLeftMargin;
        float bodyTop = Krate::Plugins::ArpLaneHeader::kHeight;
        bool needsScroll = numSteps_ > kMaxVisibleSteps;
        float bottomReserved = kStepNumberBarH + (needsScroll ? kScrollBarH : 0.0f);
        float knobAreaH = static_cast<float>(vs.getHeight()) - bodyTop - bottomReserved;
        float colW = bodyW / static_cast<float>(std::min(numSteps_, kMaxVisibleSteps));
        float rowH = knobAreaH / static_cast<float>(kRowCount);
        float knobSize = std::max(std::min(colW, rowH) * 0.7f, 14.0f);

        scrollOffsetPx_ = 0.0f;
        controlPtrs_.reserve(static_cast<size_t>(numSteps_ * kRowCount));

        // Child rects in container-local coordinates (0-based).
        float ox = 0.0f;
        float oy = 0.0f;

        for (int step = 0; step < numSteps_; ++step) {
            for (int row = 0; row < kRowCount; ++row) {
                auto knobRow = static_cast<KnobRow>(row);
                uint32_t tag = getParamId(step, knobRow);

                float x0 = kLeftMargin + static_cast<float>(step) * colW;
                float yTop = bodyTop + static_cast<float>(row) * rowH;
                float xMid = x0 + colW * 0.5f;
                float yMid = yTop + rowH * 0.5f;

                VSTGUI::CRect knobRect(
                    ox + xMid - knobSize * 0.5f, oy + yMid - knobSize * 0.5f,
                    ox + xMid + knobSize * 0.5f, oy + yMid + knobSize * 0.5f);

                float value = stepValues_[step][row];

                if (knobRow == KnobRow::kActive || knobRow == KnobRow::kTimeMode) {
                    auto* toggle = new Krate::Plugins::ToggleButton(
                        knobRect, this, static_cast<int32_t>(tag));
                    toggle->setValue(value);
                    toggle->setDefaultValue(getDefaultValue(knobRow));
                    addView(toggle);
                    controlPtrs_.push_back(toggle);
                } else {
                    auto* knob = new Krate::Plugins::ArcKnob(
                        knobRect, this, static_cast<int32_t>(tag));
                    knob->setValue(value);
                    knob->setDefaultValue(getDefaultValue(knobRow));
                    knob->setArcColor(accentColor_);
                    knob->setPopupPosition(Krate::Plugins::ArcKnob::kPopupTop);
                    addView(knob);
                    controlPtrs_.push_back(knob);
                }
            }
        }

        // Set initial column visibility based on active state
        for (int step = 0; step < numSteps_; ++step) {
            bool active = stepValues_[step][static_cast<int>(KnobRow::kActive)] >= 0.5f;
            updateColumnVisibility(step, active);
        }

        // Add playhead overlay as last child (draws on top of everything)
        playheadOverlay_ = new PlayheadOverlayView(
            VSTGUI::CRect(0, 0, static_cast<float>(vs.getWidth()),
                                 static_cast<float>(vs.getHeight())));
        playheadOverlay_->setAccentColor(accentColor_);
        playheadOverlay_->setGridParams(numSteps_, colW, bodyTop, knobAreaH, kLeftMargin);
        playheadOverlay_->setPlayheadStep(playheadStep_);
        addView(playheadOverlay_);
    }

    /// Reposition all child controls based on current scroll offset.
    void repositionControls()
    {
        auto vs = getViewSize();
        float ox = 0.0f;
        float oy = 0.0f;
        float bodyW = static_cast<float>(vs.getWidth()) - kLeftMargin;
        float bodyTop = Krate::Plugins::ArpLaneHeader::kHeight;
        bool needsScroll = numSteps_ > kMaxVisibleSteps;
        float bottomReserved = kStepNumberBarH + (needsScroll ? kScrollBarH : 0.0f);
        float knobAreaH = static_cast<float>(vs.getHeight()) - bodyTop - bottomReserved;
        float colW = bodyW / static_cast<float>(std::min(numSteps_, kMaxVisibleSteps));
        float rowH = knobAreaH / static_cast<float>(kRowCount);
        float knobSize = std::min(colW, rowH) * 0.7f;
        knobSize = std::max(knobSize, 14.0f);

        for (int step = 0; step < numSteps_; ++step) {
            for (int row = 0; row < kRowCount; ++row) {
                int idx = step * kRowCount + row;
                if (idx >= static_cast<int>(controlPtrs_.size())) break;

                float x0 = kLeftMargin + static_cast<float>(step) * colW - scrollOffsetPx_;
                float yTop = bodyTop + static_cast<float>(row) * rowH;
                float xMid = x0 + colW * 0.5f;
                float yMid = yTop + rowH * 0.5f;

                // Frame coordinates (offset by container origin)
                VSTGUI::CRect newRect(
                    ox + xMid - knobSize * 0.5f, oy + yMid - knobSize * 0.5f,
                    ox + xMid + knobSize * 0.5f, oy + yMid + knobSize * 0.5f);

                auto* child = controlPtrs_[static_cast<size_t>(idx)];
                if (child) {
                    child->setViewSize(newRect);
                    child->setMouseableArea(newRect);
                    bool inViewport = (xMid + knobSize * 0.5f > kLeftMargin) &&
                                      (xMid - knobSize * 0.5f < static_cast<float>(vs.getWidth()));
                    // Active row is always visible (if in viewport).
                    // Other rows are only visible if the step is active.
                    auto knobRow = static_cast<KnobRow>(row);
                    bool activeState = stepValues_[step][static_cast<int>(KnobRow::kActive)] >= 0.5f;
                    bool visible = inViewport && (knobRow == KnobRow::kActive || activeState);
                    child->setVisible(visible);
                }
            }
        }
    }

    /// Show/hide the 6 parameter controls below the ACTIVE toggle for a step.
    void updateColumnVisibility(int step, bool active)
    {
        for (int row = static_cast<int>(KnobRow::kTimeMode);
             row < kRowCount; ++row) {
            int idx = step * kRowCount + row;
            if (idx >= 0 && idx < static_cast<int>(controlPtrs_.size())) {
                controlPtrs_[static_cast<size_t>(idx)]->setVisible(active);
            }
        }
        setDirty(true);
    }

    /// Update a single control's value from our cache.
    void updateControlValue(int step, KnobRow row)
    {
        int idx = step * kRowCount + static_cast<int>(row);
        if (idx >= 0 && idx < static_cast<int>(controlPtrs_.size())) {
            auto* ctrl = dynamic_cast<VSTGUI::CControl*>(controlPtrs_[static_cast<size_t>(idx)]);
            if (ctrl) {
                ctrl->setValue(stepValues_[step][static_cast<int>(row)]);
                ctrl->invalid();
            }
        }
    }

    // =========================================================================
    // Hit Testing
    // =========================================================================

    [[nodiscard]] bool isInScrollbar(const VSTGUI::CPoint& where) const
    {
        if (numSteps_ <= kMaxVisibleSteps) return false;
        float h = static_cast<float>(getViewSize().getHeight());
        float sbTop = h - kScrollBarH;
        return where.y >= sbTop && where.y <= h && where.x >= kLeftMargin;
    }

    // =========================================================================
    // Parameter Mapping
    // =========================================================================

    [[nodiscard]] static uint32_t getParamId(int step, KnobRow row)
    {
        switch (row) {
            case KnobRow::kActive:     return kArpMidiDelayActiveStep0Id + static_cast<uint32_t>(step);
            case KnobRow::kTimeMode:   return kArpMidiDelayTimeModeStep0Id + static_cast<uint32_t>(step);
            case KnobRow::kDelayTime:  return kArpMidiDelayTimeStep0Id + static_cast<uint32_t>(step);
            case KnobRow::kFeedback:   return kArpMidiDelayFeedbackStep0Id + static_cast<uint32_t>(step);
            case KnobRow::kVelDecay:   return kArpMidiDelayVelDecayStep0Id + static_cast<uint32_t>(step);
            case KnobRow::kPitchShift: return kArpMidiDelayPitchShiftStep0Id + static_cast<uint32_t>(step);
            case KnobRow::kGateScale:  return kArpMidiDelayGateScaleStep0Id + static_cast<uint32_t>(step);
            default: return 0;
        }
    }

    struct StepRowResult { int step; KnobRow row; };

    [[nodiscard]] static StepRowResult tagToStepRow(uint32_t tag)
    {
        if (tag >= kArpMidiDelayActiveStep0Id && tag <= kArpMidiDelayActiveStep0Id + 31)
            return {static_cast<int>(tag - kArpMidiDelayActiveStep0Id), KnobRow::kActive};
        if (tag >= kArpMidiDelayTimeModeStep0Id && tag <= kArpMidiDelayTimeModeStep0Id + 31)
            return {static_cast<int>(tag - kArpMidiDelayTimeModeStep0Id), KnobRow::kTimeMode};
        if (tag >= kArpMidiDelayTimeStep0Id && tag <= kArpMidiDelayTimeStep0Id + 31)
            return {static_cast<int>(tag - kArpMidiDelayTimeStep0Id), KnobRow::kDelayTime};
        if (tag >= kArpMidiDelayFeedbackStep0Id && tag <= kArpMidiDelayFeedbackStep0Id + 31)
            return {static_cast<int>(tag - kArpMidiDelayFeedbackStep0Id), KnobRow::kFeedback};
        if (tag >= kArpMidiDelayVelDecayStep0Id && tag <= kArpMidiDelayVelDecayStep0Id + 31)
            return {static_cast<int>(tag - kArpMidiDelayVelDecayStep0Id), KnobRow::kVelDecay};
        if (tag >= kArpMidiDelayPitchShiftStep0Id && tag <= kArpMidiDelayPitchShiftStep0Id + 31)
            return {static_cast<int>(tag - kArpMidiDelayPitchShiftStep0Id), KnobRow::kPitchShift};
        if (tag >= kArpMidiDelayGateScaleStep0Id && tag <= kArpMidiDelayGateScaleStep0Id + 31)
            return {static_cast<int>(tag - kArpMidiDelayGateScaleStep0Id), KnobRow::kGateScale};
        return {-1, KnobRow::kCount};
    }

    [[nodiscard]] static float getDefaultValue(KnobRow row)
    {
        switch (row) {
            case KnobRow::kActive:     return 0.0f;
            case KnobRow::kTimeMode:   return 1.0f;
            case KnobRow::kDelayTime:  return 10.0f / 29.0f;
            case KnobRow::kFeedback:   return 3.0f / 16.0f;
            case KnobRow::kVelDecay:   return 0.5f;
            case KnobRow::kPitchShift: return 0.5f;
            case KnobRow::kGateScale:  return (1.0f - 0.1f) / 1.9f;
            default: return 0.0f;
        }
    }

    // =========================================================================
    // State
    // =========================================================================

    Krate::Plugins::ArpLaneHeader header_;
    VSTGUI::CColor accentColor_{0xD4, 0xA8, 0x56, 0xFF};
    float expandedHeight_ = 400.0f;

    std::array<std::array<float, kRowCount>, kMaxSteps> stepValues_{};
    int numSteps_ = 16;

    int32_t playheadStep_ = -1;
    Krate::Plugins::PlayheadTrailState trailState_;
    std::array<float, 4> trailAlphas_ = {0, 0, 0, 0};

    uint32_t playheadParamId_ = 0;
    bool pasteEnabled_ = false;

    // Child control pointers (indexed by step * kRowCount + row)
    std::vector<VSTGUI::CView*> controlPtrs_;
    PlayheadOverlayView* playheadOverlay_ = nullptr;  // Owned by CViewContainer

    // Scroll state
    float scrollOffsetPx_ = 0.0f;
    bool scrollbarDragging_ = false;
    float scrollDragStartX_ = 0.0f;
    float scrollDragStartOffset_ = 0.0f;

    // Callbacks
    ParameterCallback paramCallback_;
    EditCallback beginEditCallback_;
    EditCallback endEditCallback_;
    std::function<void()> collapseCallback_;
    TransformCallback transformCallback_;
    CopyCallback copyCallback_;
    PasteCallback pasteCallback_;
};

} // namespace Gradus
