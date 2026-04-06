#pragma once

// ==============================================================================
// ArpChordLane - Per-Step Chord Type Selection with Popup Menu
// ==============================================================================
// Custom CControl implementing IArpLane that renders per-step chord type cells.
// Left-click opens COptionMenu popup with 5 chord types, right-click resets to
// None (index 0). Follows ArpConditionLane pattern.
//
// Chord normalization: index / 4.0f (range 0-4, 5 values)
// Decode: clamp(round(normalized * 4.0f), 0, 4)
//
// Location: plugins/shared/src/ui/arp_chord_lane.h
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
// ArpChordLane
// ==============================================================================

class ArpChordLane : public VSTGUI::CControl, public IArpLane {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr int kMaxSteps = 32;
    static constexpr int kMinSteps = 2;
    static constexpr int kChordTypeCount = 5;
    static constexpr float kBodyHeight = 47.0f;
    static constexpr float kLeftMargin = 40.0f;
    static constexpr float kNormDivisor = 4.0f;  // kChordTypeCount - 1

    // Abbreviated labels for cell display
    static constexpr const char* kChordAbbrev[5] = {
        "--", "Dy", "Tri", "7th", "9th"
    };

    // Full names for COptionMenu popup entries
    static constexpr const char* kChordFullNames[5] = {
        "None", "Dyad", "Triad", "7th", "9th"
    };

    // =========================================================================
    // Construction
    // =========================================================================

    ArpChordLane(const VSTGUI::CRect& size,
                 VSTGUI::IControlListener* listener,
                 int32_t tag)
        : CControl(size, listener, tag) {
        // stepValues_ is zero-initialized = all None (0)
    }

    // =========================================================================
    // Step Value API
    // =========================================================================

    void setStepValue(int index, uint8_t value) {
        if (index >= 0 && index < kMaxSteps) {
            if (value >= kChordTypeCount) value = 0;
            stepValues_[static_cast<size_t>(index)] = value;
        }
    }

    [[nodiscard]] uint8_t getStepValue(int index) const {
        if (index >= 0 && index < kMaxSteps) {
            return stepValues_[static_cast<size_t>(index)];
        }
        return 0;
    }

    void setNumSteps(int count) {
        numSteps_ = std::clamp(count, kMinSteps, kMaxSteps);
        header_.setNumSteps(numSteps_);
    }

    [[nodiscard]] int getNumSteps() const { return numSteps_; }
    [[nodiscard]] int32_t getPlayheadStep() const { return playheadStep_; }

    // =========================================================================
    // Configuration
    // =========================================================================

    void setAccentColor(const VSTGUI::CColor& color) {
        accentColor_ = color;
        header_.setAccentColor(color);
    }

    [[nodiscard]] VSTGUI::CColor getAccentColor() const { return accentColor_; }

    void setLaneName(const std::string& name) { header_.setLaneName(name); }

    void setStepBaseParamId(uint32_t baseId) { stepBaseParamId_ = baseId; }
    void setLengthParamId(uint32_t paramId) { header_.setLengthParamId(paramId); }
    void setPlayheadParamId(uint32_t paramId) { playheadParamId_ = paramId; }

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

    [[nodiscard]] float getExpandedHeight() const override { return expandedHeight_; }

    [[nodiscard]] float getCollapsedHeight() const override {
        return ArpLaneHeader::kHeight;
    }

    [[nodiscard]] bool isCollapsed() const override { return header_.isCollapsed(); }

    void setCollapseVisible(bool visible) override {
        header_.setCollapseVisible(visible);
    }

    void setSpeedMultiplier(float speed) override {
        header_.setSpeedMultiplier(speed);
    }
    void setSpeedParamId(uint32_t id) override {
        header_.setSpeedParamId(id);
    }
    void setSpeedParamCallback(std::function<void(uint32_t, float)> cb) override {
        header_.setSpeedParamCallback(std::move(cb));
    }

    void setCollapsed(bool collapsed) override {
        bool wasCollapsed = header_.isCollapsed();
        header_.setCollapsed(collapsed);
        if (collapsed != wasCollapsed && collapseCallback_) collapseCallback_();
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
            return static_cast<float>(stepValues_[static_cast<size_t>(step)]) / kNormDivisor;
        }
        return 0.0f;
    }

    void setNormalizedStepValue(int32_t step, float value) override {
        if (step >= 0 && step < kMaxSteps) {
            auto idx = static_cast<uint8_t>(
                std::clamp(static_cast<int>(std::round(value * kNormDivisor)), 0,
                           kChordTypeCount - 1));
            stepValues_[static_cast<size_t>(step)] = idx;
        }
    }

    [[nodiscard]] int32_t getLaneTypeId() const override {
        return 6;  // ClipboardLaneType::kChord
    }

    void setTransformCallback(TransformCallback cb) override {
        transformCallback_ = cb;
        header_.setTransformCallback(
            [cb](TransformType type) {
                if (cb) cb(static_cast<int>(type));
            });
    }

    void setCopyPasteCallbacks(CopyCallback copy, PasteCallback paste) override {
        copyCallback_ = std::move(copy);
        pasteCallback_ = std::move(paste);
    }

    void setPasteEnabled(bool enabled) override { pasteEnabled_ = enabled; }

    void setEuclideanOverlay(int, int, int, bool) override {}

    void setDisabled(bool disabled, const std::string& message = {}) override {
        disabled_ = disabled;
        disabledMessage_ = message;
        setDirty();
    }

    [[nodiscard]] bool isDisabled() const override { return disabled_; }

    // =========================================================================
    // Transform Operations
    // =========================================================================

    [[nodiscard]] std::array<float, 32> computeTransform(TransformType type) const {
        int32_t len = getActiveLength();
        std::array<float, 32> result{};

        for (int32_t i = 0; i < len; ++i) {
            result[static_cast<size_t>(i)] = getNormalizedStepValue(i);
        }

        switch (type) {
            case TransformType::kInvert:
                // Invert: reverse the chord type index (None<->9th, Dyad<->7th, Triad stays)
                for (int32_t i = 0; i < len; ++i) {
                    auto idx = static_cast<int>(
                        std::round(result[static_cast<size_t>(i)] * kNormDivisor));
                    idx = (kChordTypeCount - 1) - idx;
                    result[static_cast<size_t>(i)] =
                        static_cast<float>(idx) / kNormDivisor;
                }
                break;

            case TransformType::kShiftLeft:
                if (len > 1) {
                    float first = result[0];
                    for (int32_t i = 0; i < len - 1; ++i)
                        result[static_cast<size_t>(i)] = result[static_cast<size_t>(i + 1)];
                    result[static_cast<size_t>(len - 1)] = first;
                }
                break;

            case TransformType::kShiftRight:
                if (len > 1) {
                    float last = result[static_cast<size_t>(len - 1)];
                    for (int32_t i = len - 1; i > 0; --i)
                        result[static_cast<size_t>(i)] = result[static_cast<size_t>(i - 1)];
                    result[0] = last;
                }
                break;

            case TransformType::kRandomize: {
                std::random_device rd;
                std::mt19937 rng(rd());
                std::uniform_int_distribution<int> dist(0, kChordTypeCount - 1);
                for (int32_t i = 0; i < len; ++i)
                    result[static_cast<size_t>(i)] =
                        static_cast<float>(dist(rng)) / kNormDivisor;
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
        VSTGUI::CRect headerRect(vs.left, vs.top, vs.right,
                                  vs.top + ArpLaneHeader::kHeight);

        header_.setNumSteps(numSteps_);

        if (isCollapsed()) {
            header_.draw(context, headerRect);
            drawMiniPreview(context, vs);
        } else {
            header_.draw(context, headerRect);
            drawBody(context, vs);
        }

        if (disabled_) drawDisabledOverlay(context, vs);

        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {

        if (disabled_) return VSTGUI::kMouseEventHandled;

        VSTGUI::CRect vs = getViewSize();
        VSTGUI::CRect headerRect(vs.left, vs.top, vs.right,
                                  vs.top + ArpLaneHeader::kHeight);

        if (buttons.isRightButton() && headerRect.pointInside(where)) {
            if (header_.handleRightClick(where, headerRect, getFrame()))
                return VSTGUI::kMouseEventHandled;
        }

        bool wasCollapsed = isCollapsed();
        VSTGUI::CPoint framePoint(where);
        localToFrame(framePoint);
        if (header_.handleMouseDown(where, framePoint, headerRect, getFrame())) {
            if (isCollapsed() != wasCollapsed && collapseCallback_) collapseCallback_();
            setDirty();
            return VSTGUI::kMouseEventHandled;
        }

        if (isCollapsed()) return VSTGUI::kMouseEventHandled;

        // Body interaction
        float bodyTop = static_cast<float>(vs.top) + ArpLaneHeader::kHeight;
        float bodyLeft = static_cast<float>(vs.left);
        float bodyWidth = static_cast<float>(vs.getWidth());

        float localX = static_cast<float>(where.x) - bodyLeft - kLeftMargin;
        float localY = static_cast<float>(where.y) - bodyTop;

        if (localX < 0.0f || localY < 0.0f || localY >= kBodyHeight)
            return VSTGUI::kMouseEventHandled;

        float cellWidth = (bodyWidth - kLeftMargin) / static_cast<float>(numSteps_);
        int step = static_cast<int>(localX / cellWidth);

        if (step < 0 || step >= numSteps_)
            return VSTGUI::kMouseEventHandled;

        // Right-click: reset to None (0)
        if (buttons.isRightButton()) {
            if (beginEditCallback_ && stepBaseParamId_ != 0)
                beginEditCallback_(stepBaseParamId_ + static_cast<uint32_t>(step));

            stepValues_[static_cast<size_t>(step)] = 0;

            if (paramCallback_ && stepBaseParamId_ != 0)
                paramCallback_(stepBaseParamId_ + static_cast<uint32_t>(step), 0.0f);

            if (endEditCallback_ && stepBaseParamId_ != 0)
                endEditCallback_(stepBaseParamId_ + static_cast<uint32_t>(step));

            setDirty(true);
            return VSTGUI::kMouseEventHandled;
        }

        // Left-click: open COptionMenu popup
        VSTGUI::CPoint frameWhere(where);
        localToFrame(frameWhere);
        VSTGUI::CRect menuRect(frameWhere.x, frameWhere.y,
                                frameWhere.x + 1, frameWhere.y + 1);
        auto* menu = new VSTGUI::COptionMenu(menuRect, nullptr, -1);

        for (int i = 0; i < kChordTypeCount; ++i)
            menu->addEntry(kChordFullNames[i]);

        uint8_t currentVal = stepValues_[static_cast<size_t>(step)];
        if (currentVal < kChordTypeCount)
            menu->setCurrent(static_cast<int32_t>(currentVal));

        menu->setListener(nullptr);
        menu->popup(getFrame(), frameWhere);

        int selectedIndex = menu->getCurrentIndex();
        if (selectedIndex >= 0 && selectedIndex < kChordTypeCount) {
            if (beginEditCallback_ && stepBaseParamId_ != 0)
                beginEditCallback_(stepBaseParamId_ + static_cast<uint32_t>(step));

            stepValues_[static_cast<size_t>(step)] = static_cast<uint8_t>(selectedIndex);

            if (paramCallback_ && stepBaseParamId_ != 0) {
                float normalized = static_cast<float>(selectedIndex) / kNormDivisor;
                paramCallback_(stepBaseParamId_ + static_cast<uint32_t>(step), normalized);
            }

            if (endEditCallback_ && stepBaseParamId_ != 0)
                endEditCallback_(stepBaseParamId_ + static_cast<uint32_t>(step));
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
        VSTGUI::CRect headerRect(vs.left, vs.top, vs.right,
                                  vs.top + ArpLaneHeader::kHeight);

        bool wasHovered = header_.isButtonHovered();
        if (header_.updateHover(where, headerRect, this)) {
            if (auto* frame = getFrame())
                frame->setCursor(VSTGUI::kCursorHand);
            if (!wasHovered) setDirty(true);
            return VSTGUI::kMouseEventHandled;
        }

        if (wasHovered) setDirty(true);

        float bodyTop = static_cast<float>(vs.top) + ArpLaneHeader::kHeight;
        float bodyBottom = bodyTop + kBodyHeight;
        bool inBody = where.y >= bodyTop && where.y < bodyBottom;
        if (auto* frame = getFrame())
            frame->setCursor(inBody ? VSTGUI::kCursorHand : VSTGUI::kCursorDefault);

        return VSTGUI::kMouseEventHandled;
    }

    CLASS_METHODS(ArpChordLane, CControl)

private:
    // =========================================================================
    // Drawing Helpers
    // =========================================================================

    void drawBody(VSTGUI::CDrawContext* context, const VSTGUI::CRect& vs) {
        float bodyTop = static_cast<float>(vs.top) + ArpLaneHeader::kHeight;
        float bodyLeft = static_cast<float>(vs.left);
        float bodyRight = static_cast<float>(vs.right);
        float bodyBottom = bodyTop + kBodyHeight;

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

        VSTGUI::CColor activeCellBg{35, 35, 40, 255};

        for (int i = 0; i < numSteps_; ++i) {
            uint8_t val = stepValues_[static_cast<size_t>(i)];
            if (val >= kChordTypeCount) val = 0;

            float cellLeft = contentLeft + static_cast<float>(i) * cellWidth;
            float cellRight = cellLeft + cellWidth;
            VSTGUI::CRect cellRect(cellLeft + 1.0, bodyTop + 1.0,
                                    cellRight - 1.0, bodyBottom - 1.0);

            if (val != 0) {
                context->setFillColor(activeCellBg);
                context->drawRect(cellRect, VSTGUI::kDrawFilled);
            }

            if (val != 0) {
                context->setFontColor(accentColor_);
            } else {
                VSTGUI::CColor dimColor = darkenColor(accentColor_, 0.8f);
                context->setFontColor(dimColor);
            }

            context->drawString(VSTGUI::UTF8String(kChordAbbrev[val]),
                               cellRect, VSTGUI::kCenterText);
        }

        // Trail overlay
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

        // Skip X overlays
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

        // Playhead overlay
        if (playheadStep_ >= 0 && playheadStep_ < numSteps_) {
            float overlayLeft = contentLeft +
                static_cast<float>(playheadStep_) * cellWidth;
            float overlayRight = overlayLeft + cellWidth;

            context->setFillColor(VSTGUI::CColor(255, 255, 255, 45));
            VSTGUI::CRect overlay(overlayLeft, bodyTop, overlayRight, bodyBottom);
            context->drawRect(overlay, VSTGUI::kDrawFilled);
        }
    }

    void drawMiniPreview(VSTGUI::CDrawContext* context, const VSTGUI::CRect& vs) {
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
            uint8_t val = stepValues_[static_cast<size_t>(i)];

            float cellLeft = previewLeft + static_cast<float>(i) * cellWidth;
            float cellRight = cellLeft + cellWidth;
            VSTGUI::CRect cellRect(cellLeft + 0.5, previewTop,
                                    cellRight - 0.5, previewBottom);

            if (val != 0) {
                context->setFillColor(filledColor);
                context->drawRect(cellRect, VSTGUI::kDrawFilled);
            } else {
                context->setFrameColor(dimColor);
                context->setLineWidth(1.0);
                context->drawRect(cellRect, VSTGUI::kDrawStroked);
            }
        }
    }

    // =========================================================================
    // Disabled Overlay
    // =========================================================================

    void drawDisabledOverlay(VSTGUI::CDrawContext* context,
                             const VSTGUI::CRect& vs) {
        // Semi-transparent dark overlay
        VSTGUI::CColor overlayColor{0, 0, 0, 160};
        context->setFillColor(overlayColor);
        context->drawRect(vs, VSTGUI::kDrawFilled);

        // Draw message text centered
        if (!disabledMessage_.empty()) {
            auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 11.0,
                VSTGUI::kItalicFace);
            context->setFont(font);
            context->setFontColor(VSTGUI::CColor{180, 180, 180, 200});
            context->drawString(VSTGUI::UTF8String(disabledMessage_),
                               vs, VSTGUI::kCenterText);
        }
    }

    // =========================================================================
    // State
    // =========================================================================

    ArpLaneHeader header_;
    std::array<uint8_t, kMaxSteps> stepValues_{};
    int numSteps_ = 8;
    int playheadStep_ = -1;
    VSTGUI::CColor accentColor_{124, 144, 176, 255};
    uint32_t stepBaseParamId_ = 0;
    uint32_t playheadParamId_ = 0;
    float expandedHeight_ = kBodyHeight + ArpLaneHeader::kHeight;
    ParameterCallback paramCallback_;
    EditCallback beginEditCallback_;
    EditCallback endEditCallback_;
    std::function<void()> collapseCallback_;

    TransformCallback transformCallback_;
    CopyCallback copyCallback_;
    PasteCallback pasteCallback_;
    bool pasteEnabled_ = false;
    PlayheadTrailState trailState_;
    float trailAlphas_[PlayheadTrailState::kTrailLength] = {160.0f, 100.0f, 55.0f, 25.0f};
    bool disabled_ = false;
    std::string disabledMessage_;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct ArpChordLaneCreator : VSTGUI::ViewCreatorAdapter {
    ArpChordLaneCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override { return "ArpChordLane"; }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Arp Chord Lane";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new ArpChordLane(VSTGUI::CRect(0, 0, 500, 44), nullptr, -1);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override {
        auto* lane = dynamic_cast<ArpChordLane*>(view);
        if (!lane) return false;

        VSTGUI::CColor color;
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("accent-color"), color, description))
            lane->setAccentColor(color);

        const auto* nameStr = attributes.getAttributeValue("lane-name");
        if (nameStr) lane->setLaneName(*nameStr);

        const auto* baseIdStr = attributes.getAttributeValue("step-base-param-id");
        if (baseIdStr)
            lane->setStepBaseParamId(static_cast<uint32_t>(std::stoul(*baseIdStr)));

        const auto* lengthIdStr = attributes.getAttributeValue("length-param-id");
        if (lengthIdStr)
            lane->setLengthParamId(static_cast<uint32_t>(std::stoul(*lengthIdStr)));

        const auto* playheadIdStr = attributes.getAttributeValue("playhead-param-id");
        if (playheadIdStr)
            lane->setPlayheadParamId(static_cast<uint32_t>(std::stoul(*playheadIdStr)));

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("accent-color");
        attributeNames.emplace_back("lane-name");
        attributeNames.emplace_back("step-base-param-id");
        attributeNames.emplace_back("length-param-id");
        attributeNames.emplace_back("playhead-param-id");
        return true;
    }

    AttrType getAttributeType(const std::string& attributeName) const override {
        if (attributeName == "accent-color") return kColorType;
        if (attributeName == "lane-name") return kStringType;
        if (attributeName == "step-base-param-id") return kStringType;
        if (attributeName == "length-param-id") return kStringType;
        if (attributeName == "playhead-param-id") return kStringType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override {
        auto* lane = dynamic_cast<ArpChordLane*>(view);
        if (!lane) return false;

        if (attributeName == "accent-color") {
            VSTGUI::UIViewCreator::colorToString(
                lane->getAccentColor(), stringValue, desc);
            return true;
        }
        return false;
    }
};

inline ArpChordLaneCreator gArpChordLaneCreator;

} // namespace Krate::Plugins
