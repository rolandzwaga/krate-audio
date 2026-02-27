#pragma once

// ==============================================================================
// ArpConditionLane - Per-Step Condition Enum Selection with Popup Menu
// ==============================================================================
// Custom CControl implementing IArpLane that renders per-step condition
// abbreviation cells. Left-click opens COptionMenu popup with 18 conditions,
// right-click resets to Always (index 0), hover tooltip shows full description.
// Collapsible header via ArpLaneHeader composition, ViewCreator registration.
//
// Condition normalization: index / 17.0f (range 0-17, 18 values)
// Decode: clamp(round(normalized * 17.0f), 0, 17)
//
// Location: plugins/shared/src/ui/arp_condition_lane.h
// ==============================================================================

#include "arp_lane.h"
#include "arp_lane_header.h"
#include "color_utils.h"

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/detail/uiviewcreatorattributes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <random>
#include <string>

namespace Krate::Plugins {

// ==============================================================================
// ArpConditionLane
// ==============================================================================

class ArpConditionLane : public VSTGUI::CControl, public IArpLane {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr int kMaxSteps = 32;
    static constexpr int kMinSteps = 2;
    static constexpr int kConditionCount = 18;
    static constexpr float kBodyHeight = 47.0f;

    /// Left margin for step content alignment across all arp lane types (FR-049).
    /// Must match ArpLaneEditor::kStepContentLeftMargin and ArpModifierLane::kLeftMargin.
    static constexpr float kLeftMargin = 40.0f;

    // Abbreviated labels for cell display
    static constexpr const char* kConditionAbbrev[18] = {
        "Alw", "10%", "25%", "50%", "75%", "90%",
        "Ev2", "2:2", "Ev3", "2:3", "3:3",
        "Ev4", "2:4", "3:4", "4:4",
        "1st", "Fill", "!F"
    };

    // Full names for COptionMenu popup entries
    static constexpr const char* kConditionFullNames[18] = {
        "Always", "10%", "25%", "50%", "75%", "90%",
        "Every 2", "2nd of 2", "Every 3", "2nd of 3", "3rd of 3",
        "Every 4", "2nd of 4", "3rd of 4", "4th of 4",
        "First", "Fill", "Not Fill"
    };

    // Descriptive strings for setTooltipText() on hover
    static constexpr const char* kConditionTooltips[18] = {
        "Always -- Step fires unconditionally",
        "10% -- ~10% probability of firing",
        "25% -- ~25% probability of firing",
        "50% -- ~50% probability of firing",
        "75% -- ~75% probability of firing",
        "90% -- ~90% probability of firing",
        "Every 2 -- Fires on 1st of every 2 loops",
        "2nd of 2 -- Fires on 2nd of every 2 loops",
        "Every 3 -- Fires on 1st of every 3 loops",
        "2nd of 3 -- Fires on 2nd of every 3 loops",
        "3rd of 3 -- Fires on 3rd of every 3 loops",
        "Every 4 -- Fires on 1st of every 4 loops",
        "2nd of 4 -- Fires on 2nd of every 4 loops",
        "3rd of 4 -- Fires on 3rd of every 4 loops",
        "4th of 4 -- Fires on 4th of every 4 loops",
        "First -- Fires only on first loop",
        "Fill -- Fires only when fill mode is active",
        "Not Fill -- Fires only when fill mode is NOT active"
    };

    // =========================================================================
    // Construction
    // =========================================================================

    ArpConditionLane(const VSTGUI::CRect& size,
                     VSTGUI::IControlListener* listener,
                     int32_t tag)
        : CControl(size, listener, tag) {
        // stepConditions_ is zero-initialized via {} = all Always (0)
    }

    // =========================================================================
    // Step Condition API
    // =========================================================================

    void setStepCondition(int index, uint8_t conditionIndex) {
        if (index >= 0 && index < kMaxSteps) {
            if (conditionIndex >= kConditionCount) {
                conditionIndex = 0; // clamp out-of-range to Always
            }
            stepConditions_[static_cast<size_t>(index)] = conditionIndex;
        }
    }

    [[nodiscard]] uint8_t getStepCondition(int index) const {
        if (index >= 0 && index < kMaxSteps) {
            return stepConditions_[static_cast<size_t>(index)];
        }
        return 0; // default: Always
    }

    void setNumSteps(int count) {
        numSteps_ = std::clamp(count, kMinSteps, kMaxSteps);
        header_.setNumSteps(numSteps_);
    }

    [[nodiscard]] int getNumSteps() const { return numSteps_; }

    /// Get the current playhead step (-1 = no playhead).
    [[nodiscard]] int32_t getPlayheadStep() const { return playheadStep_; }

    // =========================================================================
    // Configuration
    // =========================================================================

    void setAccentColor(const VSTGUI::CColor& color) {
        accentColor_ = color;
        header_.setAccentColor(color);
    }

    [[nodiscard]] VSTGUI::CColor getAccentColor() const { return accentColor_; }

    void setLaneName(const std::string& name) {
        header_.setLaneName(name);
    }

    void setStepConditionBaseParamId(uint32_t baseId) {
        stepConditionBaseParamId_ = baseId;
    }

    void setLengthParamId(uint32_t paramId) {
        header_.setLengthParamId(paramId);
    }

    void setPlayheadParamId(uint32_t paramId) {
        playheadParamId_ = paramId;
    }

    // =========================================================================
    // Parameter Callbacks
    // =========================================================================

    using ParameterCallback = std::function<void(uint32_t paramId, float normalizedValue)>;
    using EditCallback = std::function<void(uint32_t paramId)>;

    void setParameterCallback(ParameterCallback cb) { paramCallback_ = std::move(cb); }
    void setBeginEditCallback(EditCallback cb) { beginEditCallback_ = std::move(cb); }
    void setEndEditCallback(EditCallback cb) { endEditCallback_ = std::move(cb); }

    void setLengthParamCallback(std::function<void(uint32_t, float)> cb) {
        header_.setLengthParamCallback(std::move(cb));
    }

    // =========================================================================
    // IArpLane Interface Implementation
    // =========================================================================

    VSTGUI::CView* getView() override { return this; }

    [[nodiscard]] float getExpandedHeight() const override {
        return expandedHeight_;
    }

    [[nodiscard]] float getCollapsedHeight() const override {
        return ArpLaneHeader::kHeight;
    }

    [[nodiscard]] bool isCollapsed() const override {
        return header_.isCollapsed();
    }

    void setCollapsed(bool collapsed) override {
        bool wasCollapsed = header_.isCollapsed();
        header_.setCollapsed(collapsed);
        if (collapsed != wasCollapsed && collapseCallback_) {
            collapseCallback_();
        }
        setDirty();
    }

    void setPlayheadStep(int32_t step) override {
        playheadStep_ = step;
        setDirty();
    }

    void setLength(int32_t length) override {
        setNumSteps(static_cast<int>(length));
        setDirty();
    }

    void setCollapseCallback(std::function<void()> cb) override {
        collapseCallback_ = std::move(cb);
    }

    // =========================================================================
    // IArpLane Phase 11c Stubs
    // =========================================================================

    void setTrailSteps(const int32_t steps[4], const float alphas[4]) override {
        for (int i = 0; i < PlayheadTrailState::kTrailLength; ++i) {
            trailState_.steps[i] = steps[i];
            trailAlphas_[i] = alphas[i];
        }
    }

    void setSkippedStep(int32_t step) override {
        trailState_.markSkipped(step);
        setDirty();
    }

    void clearOverlays() override {
        trailState_.clear();
        setDirty();
    }

    [[nodiscard]] int32_t getActiveLength() const override {
        return static_cast<int32_t>(numSteps_);
    }

    [[nodiscard]] float getNormalizedStepValue(int32_t step) const override {
        if (step >= 0 && step < kMaxSteps) {
            return static_cast<float>(stepConditions_[static_cast<size_t>(step)]) / 17.0f;
        }
        return 0.0f;
    }

    void setNormalizedStepValue(int32_t step, float value) override {
        if (step >= 0 && step < kMaxSteps) {
            auto condIdx = static_cast<uint8_t>(
                std::clamp(static_cast<int>(std::round(value * 17.0f)), 0, 17));
            stepConditions_[static_cast<size_t>(step)] = condIdx;
        }
    }

    [[nodiscard]] int32_t getLaneTypeId() const override {
        return 5;  // ClipboardLaneType::kCondition
    }

    void setTransformCallback(TransformCallback cb) override {
        transformCallback_ = cb;
        // Forward to header with type conversion
        header_.setTransformCallback(
            [cb](TransformType type) {
                if (cb) cb(static_cast<int>(type));
            });
    }

    void setCopyPasteCallbacks(CopyCallback copy, PasteCallback paste) override {
        copyCallback_ = std::move(copy);
        pasteCallback_ = std::move(paste);
    }

    void setPasteEnabled(bool enabled) override {
        pasteEnabled_ = enabled;
    }

    void setEuclideanOverlay(int /*hits*/, int /*steps*/, int /*rotation*/,
                             bool /*enabled*/) override {
        // Euclidean linear overlay not shown on condition lanes
    }

    // =========================================================================
    // Transform Operations (Phase 5, T048)
    // =========================================================================

    /// Condition inversion table: maps condition index to its inverse.
    /// From transform-operations.md:
    /// 0->0 (Always stays), 1<->5, 2<->4, 3->3, 6-14 unchanged,
    /// 15->15 (First stays), 16<->17 (Fill<->Not Fill)
    static constexpr uint8_t kConditionInvertTable[18] = {
        0, 5, 4, 3, 2, 1,              // probabilities: 0, 10%<->90%, 25%<->75%, 50% stays
        6, 7, 8, 9, 10, 11, 12, 13, 14, // ratios: unchanged
        15,                              // First: unchanged
        17, 16                           // Fill <-> Not Fill
    };

    /// Compute the result of applying a transform to this lane's step data.
    /// Returns an array of new normalized values (conditionIndex/17.0f).
    [[nodiscard]] std::array<float, 32> computeTransform(TransformType type) const {
        int32_t len = getActiveLength();
        std::array<float, 32> result{};

        // Read current condition values
        for (int32_t i = 0; i < len; ++i) {
            result[static_cast<size_t>(i)] = getNormalizedStepValue(i);
        }

        switch (type) {
            case TransformType::kInvert:
                for (int32_t i = 0; i < len; ++i) {
                    auto condIdx = static_cast<uint8_t>(
                        std::clamp(static_cast<int>(
                            std::round(result[static_cast<size_t>(i)] * 17.0f)), 0, 17));
                    uint8_t inverted = kConditionInvertTable[condIdx];
                    result[static_cast<size_t>(i)] =
                        static_cast<float>(inverted) / 17.0f;
                }
                break;

            case TransformType::kShiftLeft:
                if (len > 1) {
                    float first = result[0];
                    for (int32_t i = 0; i < len - 1; ++i) {
                        result[static_cast<size_t>(i)] = result[static_cast<size_t>(i + 1)];
                    }
                    result[static_cast<size_t>(len - 1)] = first;
                }
                break;

            case TransformType::kShiftRight:
                if (len > 1) {
                    float last = result[static_cast<size_t>(len - 1)];
                    for (int32_t i = len - 1; i > 0; --i) {
                        result[static_cast<size_t>(i)] = result[static_cast<size_t>(i - 1)];
                    }
                    result[0] = last;
                }
                break;

            case TransformType::kRandomize: {
                std::random_device rd;
                std::mt19937 rng(rd());
                std::uniform_int_distribution<int> dist(0, 17);
                for (int32_t i = 0; i < len; ++i) {
                    result[static_cast<size_t>(i)] =
                        static_cast<float>(dist(rng)) / 17.0f;
                }
                break;
            }
        }

        return result;
    }

    // =========================================================================
    // CControl Overrides
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        VSTGUI::CRect vs = getViewSize();
        VSTGUI::CRect headerRect(vs.left, vs.top, vs.right, vs.top + ArpLaneHeader::kHeight);

        // Keep header numSteps in sync
        header_.setNumSteps(numSteps_);

        if (isCollapsed()) {
            header_.draw(context, headerRect);
            drawMiniPreview(context, vs);
        } else {
            header_.draw(context, headerRect);
            drawBody(context, vs);
        }

        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {

        VSTGUI::CRect vs = getViewSize();
        VSTGUI::CRect headerRect(vs.left, vs.top, vs.right, vs.top + ArpLaneHeader::kHeight);

        // Right-click in header area: open copy/paste context menu
        if (buttons.isRightButton() && headerRect.pointInside(where)) {
            if (header_.handleRightClick(where, headerRect, getFrame())) {
                return VSTGUI::kMouseEventHandled;
            }
        }

        // Track collapse state before header interaction
        bool wasCollapsed = isCollapsed();

        // Delegate header interaction
        if (header_.handleMouseDown(where, headerRect, getFrame())) {
            if (isCollapsed() != wasCollapsed && collapseCallback_) {
                collapseCallback_();
            }
            setDirty();
            return VSTGUI::kMouseEventHandled;
        }

        // If collapsed, no body interaction
        if (isCollapsed()) {
            return VSTGUI::kMouseEventHandled;
        }

        // Body interaction: determine step
        float bodyTop = static_cast<float>(vs.top) + ArpLaneHeader::kHeight;
        float bodyLeft = static_cast<float>(vs.left);
        float bodyWidth = static_cast<float>(vs.getWidth());

        float localX = static_cast<float>(where.x) - bodyLeft - kLeftMargin;
        float localY = static_cast<float>(where.y) - bodyTop;

        if (localX < 0.0f || localY < 0.0f || localY >= kBodyHeight) {
            return VSTGUI::kMouseEventHandled;
        }

        float cellWidth = (bodyWidth - kLeftMargin) / static_cast<float>(numSteps_);
        int step = static_cast<int>(localX / cellWidth);

        if (step < 0 || step >= numSteps_) {
            return VSTGUI::kMouseEventHandled;
        }

        // Right-click on body: reset to Always (0)
        if (buttons.isRightButton()) {
            if (beginEditCallback_ && stepConditionBaseParamId_ != 0) {
                beginEditCallback_(stepConditionBaseParamId_ + static_cast<uint32_t>(step));
            }

            stepConditions_[static_cast<size_t>(step)] = 0;

            if (paramCallback_ && stepConditionBaseParamId_ != 0) {
                paramCallback_(stepConditionBaseParamId_ + static_cast<uint32_t>(step), 0.0f);
            }

            if (endEditCallback_ && stepConditionBaseParamId_ != 0) {
                endEditCallback_(stepConditionBaseParamId_ + static_cast<uint32_t>(step));
            }

            setDirty(true);
            return VSTGUI::kMouseEventHandled;
        }

        // Left-click: open COptionMenu popup
        VSTGUI::CRect menuRect(where.x, where.y, where.x + 1, where.y + 1);
        auto* menu = new VSTGUI::COptionMenu(menuRect, nullptr, -1);

        for (int i = 0; i < kConditionCount; ++i) {
            menu->addEntry(kConditionFullNames[i]);
        }

        // Set current selection
        uint8_t currentCond = stepConditions_[static_cast<size_t>(step)];
        if (currentCond < kConditionCount) {
            menu->setCurrent(static_cast<int32_t>(currentCond));
        }

        // Show popup
        menu->setListener(nullptr);
        menu->popup(getFrame(), where);

        int selectedIndex = menu->getCurrentIndex();
        if (selectedIndex >= 0 && selectedIndex < kConditionCount) {
            if (beginEditCallback_ && stepConditionBaseParamId_ != 0) {
                beginEditCallback_(stepConditionBaseParamId_ + static_cast<uint32_t>(step));
            }

            stepConditions_[static_cast<size_t>(step)] = static_cast<uint8_t>(selectedIndex);

            if (paramCallback_ && stepConditionBaseParamId_ != 0) {
                float normalized = static_cast<float>(selectedIndex) / 17.0f;
                paramCallback_(stepConditionBaseParamId_ + static_cast<uint32_t>(step), normalized);
            }

            if (endEditCallback_ && stepConditionBaseParamId_ != 0) {
                endEditCallback_(stepConditionBaseParamId_ + static_cast<uint32_t>(step));
            }
        }

        menu->forget();
        setDirty(true);
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseExited(
        VSTGUI::CPoint& /*where*/,
        const VSTGUI::CButtonState& /*buttons*/) override {
        if (auto* frame = getFrame())
            frame->setCursor(VSTGUI::kCursorDefault);
        if (header_.isButtonHovered()) {
            header_.clearHover(this);
            setDirty(true);
        }
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& /*buttons*/) override {

        VSTGUI::CRect vs = getViewSize();

        // Transform button hover: tooltip, cursor, highlight
        VSTGUI::CRect headerRect(vs.left, vs.top, vs.right,
                                  vs.top + ArpLaneHeader::kHeight);
        bool wasHovered = header_.isButtonHovered();
        if (header_.updateHover(where, headerRect, this)) {
            if (auto* frame = getFrame())
                frame->setCursor(VSTGUI::kCursorHand);
            if (!wasHovered)
                setDirty(true);
            return VSTGUI::kMouseEventHandled;
        }

        // Clear button hover if we moved off buttons
        if (wasHovered)
            setDirty(true);

        // Body area (dots) — show pointer cursor
        float bodyTop = static_cast<float>(vs.top) + ArpLaneHeader::kHeight;
        float bodyBottom = bodyTop + kBodyHeight;
        bool inBody = where.y >= bodyTop && where.y < bodyBottom;
        if (auto* frame = getFrame())
            frame->setCursor(inBody ? VSTGUI::kCursorHand : VSTGUI::kCursorDefault);

        return VSTGUI::kMouseEventHandled;
    }

    CLASS_METHODS(ArpConditionLane, CControl)

private:
    // =========================================================================
    // Drawing Helpers
    // =========================================================================

    void drawBody(VSTGUI::CDrawContext* context, const VSTGUI::CRect& vs) {
        float bodyTop = static_cast<float>(vs.top) + ArpLaneHeader::kHeight;
        float bodyLeft = static_cast<float>(vs.left);
        float bodyRight = static_cast<float>(vs.right);
        float bodyBottom = bodyTop + kBodyHeight;

        // Body background
        VSTGUI::CColor bodyBg{25, 25, 28, 255};
        context->setFillColor(bodyBg);
        VSTGUI::CRect bodyRect(bodyLeft, bodyTop, bodyRight, bodyBottom);
        context->drawRect(bodyRect, VSTGUI::kDrawFilled);

        if (numSteps_ <= 0) return;

        float contentLeft = bodyLeft + kLeftMargin;
        float contentWidth = bodyRight - contentLeft;
        float cellWidth = contentWidth / static_cast<float>(numSteps_);

        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 10.0);
        context->setFont(font);

        // Cell background colors
        VSTGUI::CColor defaultCellBg{25, 25, 28, 255};      // Same as body for Always
        VSTGUI::CColor activeCellBg{35, 35, 40, 255};        // Slightly lighter for non-Always

        for (int i = 0; i < numSteps_; ++i) {
            uint8_t condIdx = stepConditions_[static_cast<size_t>(i)];
            if (condIdx >= kConditionCount) condIdx = 0;

            float cellLeft = contentLeft + static_cast<float>(i) * cellWidth;
            float cellRight = cellLeft + cellWidth;
            VSTGUI::CRect cellRect(cellLeft + 1.0, bodyTop + 1.0,
                                    cellRight - 1.0, bodyBottom - 1.0);

            // Draw cell background (slightly lighter for non-Always)
            if (condIdx != 0) {
                context->setFillColor(activeCellBg);
                context->drawRect(cellRect, VSTGUI::kDrawFilled);
            }

            // Draw abbreviated label centered in cell
            if (condIdx != 0) {
                context->setFontColor(accentColor_);
            } else {
                VSTGUI::CColor dimColor = darkenColor(accentColor_, 0.8f);
                context->setFontColor(dimColor);
            }

            context->drawString(VSTGUI::UTF8String(kConditionAbbrev[condIdx]),
                               cellRect, VSTGUI::kCenterText);
        }

        // Draw trail overlay (semi-transparent accent rects for trail steps)
        for (int t = 0; t < PlayheadTrailState::kTrailLength; ++t) {
            int32_t trailStep = trailState_.steps[t];
            if (trailStep < 0 || trailStep >= numSteps_) continue;

            float overlayLeft = contentLeft +
                static_cast<float>(trailStep) * cellWidth;
            float overlayRight = overlayLeft + cellWidth;

            VSTGUI::CColor overlayColor = accentColor_;
            overlayColor.alpha = static_cast<uint8_t>(
                std::clamp(trailAlphas_[t], 0.0f, 255.0f));
            context->setFillColor(overlayColor);
            VSTGUI::CRect overlay(overlayLeft, bodyTop, overlayRight, bodyBottom);
            context->drawRect(overlay, VSTGUI::kDrawFilled);
        }

        // Draw skip X overlays (081-interaction-polish, FR-007, FR-011)
        {
            VSTGUI::CColor xColor = brightenColor(accentColor_, 1.3f);
            xColor.alpha = 204;
            constexpr float kXSize = 3.0f;
            constexpr float kXStroke = 1.5f;

            for (int i = 0; i < numSteps_ && i < 32; ++i) {
                if (!trailState_.skipped[i]) continue;

                float cellCenterX = contentLeft +
                    (static_cast<float>(i) + 0.5f) * cellWidth;
                float cellCenterY = bodyTop + kBodyHeight * 0.5f;

                context->setFrameColor(xColor);
                context->setLineWidth(kXStroke);
                context->drawLine(
                    VSTGUI::CPoint(cellCenterX - kXSize, cellCenterY - kXSize),
                    VSTGUI::CPoint(cellCenterX + kXSize, cellCenterY + kXSize));
                context->drawLine(
                    VSTGUI::CPoint(cellCenterX + kXSize, cellCenterY - kXSize),
                    VSTGUI::CPoint(cellCenterX - kXSize, cellCenterY + kXSize));
            }
        }

        // Draw playhead overlay
        if (playheadStep_ >= 0 && playheadStep_ < numSteps_) {
            float overlayLeft = contentLeft +
                static_cast<float>(playheadStep_) * cellWidth;
            float overlayRight = overlayLeft + cellWidth;

            VSTGUI::CColor overlayColor = accentColor_;
            overlayColor.alpha = 40;
            context->setFillColor(overlayColor);
            VSTGUI::CRect overlay(overlayLeft, bodyTop, overlayRight, bodyBottom);
            context->drawRect(overlay, VSTGUI::kDrawFilled);
        }
    }

    void drawMiniPreview(VSTGUI::CDrawContext* context, const VSTGUI::CRect& vs) {
        // Collapsed preview: small colored indicator cells in the header area
        float previewLeft = static_cast<float>(vs.left) + 80.0f;
        float previewRight = static_cast<float>(vs.right) - 4.0f;
        float previewTop = static_cast<float>(vs.top) + 3.0f;
        float previewBottom = static_cast<float>(vs.top) + ArpLaneHeader::kHeight - 3.0f;

        float previewWidth = previewRight - previewLeft;
        float previewHeight = previewBottom - previewTop;

        if (previewWidth <= 0.0f || previewHeight <= 0.0f || numSteps_ <= 0) return;

        float cellWidth = previewWidth / static_cast<float>(numSteps_);

        VSTGUI::CColor filledColor = accentColor_;
        VSTGUI::CColor dimColor = darkenColor(accentColor_, 0.5f);

        for (int i = 0; i < numSteps_; ++i) {
            uint8_t condIdx = stepConditions_[static_cast<size_t>(i)];

            float cellLeft = previewLeft + static_cast<float>(i) * cellWidth;
            float cellRight = cellLeft + cellWidth;
            VSTGUI::CRect cellRect(cellLeft + 0.5, previewTop,
                                    cellRight - 0.5, previewBottom);

            if (condIdx != 0) {
                // Non-Always: filled cell in accent color
                context->setFillColor(filledColor);
                context->drawRect(cellRect, VSTGUI::kDrawFilled);
            } else {
                // Always: outline only (dimmed)
                context->setFrameColor(dimColor);
                context->setLineWidth(1.0);
                context->drawRect(cellRect, VSTGUI::kDrawStroked);
            }
        }
    }

    // =========================================================================
    // State
    // =========================================================================

    ArpLaneHeader header_;
    std::array<uint8_t, kMaxSteps> stepConditions_{};
    int numSteps_ = 8;
    int playheadStep_ = -1;
    VSTGUI::CColor accentColor_{124, 144, 176, 255};
    uint32_t stepConditionBaseParamId_ = 0;
    uint32_t playheadParamId_ = 0;
    float expandedHeight_ = kBodyHeight + ArpLaneHeader::kHeight;  // 47.0f + 16.0f = 63.0f
    ParameterCallback paramCallback_;
    EditCallback beginEditCallback_;
    EditCallback endEditCallback_;
    std::function<void()> collapseCallback_;

    // Phase 11c callbacks and state
    TransformCallback transformCallback_;
    CopyCallback copyCallback_;
    PasteCallback pasteCallback_;
    bool pasteEnabled_ = false;
    PlayheadTrailState trailState_;
    float trailAlphas_[PlayheadTrailState::kTrailLength] = {160.0f, 100.0f, 55.0f, 25.0f};
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct ArpConditionLaneCreator : VSTGUI::ViewCreatorAdapter {
    ArpConditionLaneCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override { return "ArpConditionLane"; }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Arp Condition Lane";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new ArpConditionLane(VSTGUI::CRect(0, 0, 500, 44), nullptr, -1);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override {
        auto* lane = dynamic_cast<ArpConditionLane*>(view);
        if (!lane)
            return false;

        // Accent color
        VSTGUI::CColor color;
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("accent-color"), color, description))
            lane->setAccentColor(color);

        // Lane name
        const auto* nameStr = attributes.getAttributeValue("lane-name");
        if (nameStr)
            lane->setLaneName(*nameStr);

        // Step condition base param ID
        const auto* baseIdStr = attributes.getAttributeValue("step-condition-base-param-id");
        if (baseIdStr) {
            auto id = static_cast<uint32_t>(std::stoul(*baseIdStr));
            lane->setStepConditionBaseParamId(id);
        }

        // Length param ID
        const auto* lengthIdStr = attributes.getAttributeValue("length-param-id");
        if (lengthIdStr) {
            auto id = static_cast<uint32_t>(std::stoul(*lengthIdStr));
            lane->setLengthParamId(id);
        }

        // Playhead param ID
        const auto* playheadIdStr = attributes.getAttributeValue("playhead-param-id");
        if (playheadIdStr) {
            auto id = static_cast<uint32_t>(std::stoul(*playheadIdStr));
            lane->setPlayheadParamId(id);
        }

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("accent-color");
        attributeNames.emplace_back("lane-name");
        attributeNames.emplace_back("step-condition-base-param-id");
        attributeNames.emplace_back("length-param-id");
        attributeNames.emplace_back("playhead-param-id");
        return true;
    }

    AttrType getAttributeType(const std::string& attributeName) const override {
        if (attributeName == "accent-color") return kColorType;
        if (attributeName == "lane-name") return kStringType;
        if (attributeName == "step-condition-base-param-id") return kStringType;
        if (attributeName == "length-param-id") return kStringType;
        if (attributeName == "playhead-param-id") return kStringType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override {
        auto* lane = dynamic_cast<ArpConditionLane*>(view);
        if (!lane)
            return false;

        if (attributeName == "accent-color") {
            VSTGUI::UIViewCreator::colorToString(
                lane->getAccentColor(), stringValue, desc);
            return true;
        }
        return false;
    }
};

inline ArpConditionLaneCreator gArpConditionLaneCreator;

} // namespace Krate::Plugins
