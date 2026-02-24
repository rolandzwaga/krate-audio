#pragma once

// ==============================================================================
// ArpModifierLane - 4-Row Toggle Dot Grid for Step Modifiers
// ==============================================================================
// Custom CControl implementing IArpLane that renders a 4-row dot toggle grid
// (Rest/Tie/Slide/Accent). Each step has a bitmask encoding matching
// ArpStepFlags from arpeggiator_core.h:
//   - Row 0 (Rest): kStepActive (0x01) -- INVERTED: dot active = bit OFF
//   - Row 1 (Tie):  kStepTie   (0x02)
//   - Row 2 (Slide): kStepSlide (0x04)
//   - Row 3 (Accent): kStepAccent (0x08)
//
// Collapsible header via ArpLaneHeader composition, ViewCreator registration.
//
// Location: plugins/shared/src/ui/arp_modifier_lane.h
// ==============================================================================

#include "arp_lane.h"
#include "arp_lane_header.h"
#include "color_utils.h"

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
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
#include <string>

namespace Krate::Plugins {

// ==============================================================================
// ArpModifierLane
// ==============================================================================

class ArpModifierLane : public VSTGUI::CControl, public IArpLane {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr int kMaxSteps = 32;
    static constexpr int kMinSteps = 2;
    static constexpr int kRowCount = 4;
    static constexpr float kLeftMargin = 40.0f;
    static constexpr float kDotRadius = 4.0f;
    static constexpr float kBodyHeight = 44.0f;
    static constexpr float kRowHeight = 11.0f;  // kBodyHeight / kRowCount

    // Row definitions
    static constexpr const char* kRowLabels[4] = {"Rest", "Tie", "Slide", "Accent"};
    static constexpr uint8_t kRowBits[4] = {0x01, 0x02, 0x04, 0x08};

    // =========================================================================
    // Construction
    // =========================================================================

    ArpModifierLane(const VSTGUI::CRect& size,
                    VSTGUI::IControlListener* listener,
                    int32_t tag)
        : CControl(size, listener, tag) {
        // Initialize all step flags to kStepActive (0x01)
        stepFlags_.fill(0x01);
    }

    // =========================================================================
    // Step Flag API
    // =========================================================================

    void setStepFlags(int index, uint8_t flags) {
        if (index >= 0 && index < kMaxSteps) {
            stepFlags_[static_cast<size_t>(index)] = flags & 0x0F;
        }
    }

    [[nodiscard]] uint8_t getStepFlags(int index) const {
        if (index >= 0 && index < kMaxSteps) {
            return stepFlags_[static_cast<size_t>(index)] & 0x0F;
        }
        return 0x01;
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

    void setStepFlagBaseParamId(uint32_t baseId) {
        stepFlagBaseParamId_ = baseId;
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

        // Body interaction: determine step and row
        float bodyTop = static_cast<float>(vs.top) + ArpLaneHeader::kHeight;
        float bodyLeft = static_cast<float>(vs.left);
        float bodyWidth = static_cast<float>(vs.getWidth());

        float localX = static_cast<float>(where.x) - bodyLeft - kLeftMargin;
        float localY = static_cast<float>(where.y) - bodyTop;

        if (localX < 0.0f || localY < 0.0f || localY >= kBodyHeight) {
            return VSTGUI::kMouseEventHandled;
        }

        float stepWidth = (bodyWidth - kLeftMargin) / static_cast<float>(numSteps_);
        int step = static_cast<int>(localX / stepWidth);
        int row = static_cast<int>(localY / kRowHeight);

        if (step < 0 || step >= numSteps_ || row < 0 || row >= kRowCount) {
            return VSTGUI::kMouseEventHandled;
        }

        // Toggle the flag bit
        uint8_t flags = getStepFlags(step);

        // Fire begin edit
        if (beginEditCallback_ && stepFlagBaseParamId_ != 0) {
            beginEditCallback_(stepFlagBaseParamId_ + static_cast<uint32_t>(step));
        }

        // Row 0 (Rest): XOR kStepActive
        // Rows 1-3: XOR the row's bit
        flags ^= kRowBits[static_cast<size_t>(row)];
        flags &= 0x0F;
        setStepFlags(step, flags);

        // Fire parameter callback with normalized value
        if (paramCallback_ && stepFlagBaseParamId_ != 0) {
            float normalized = static_cast<float>(flags & 0x0F) / 15.0f;
            paramCallback_(stepFlagBaseParamId_ + static_cast<uint32_t>(step), normalized);
        }

        // Fire end edit
        if (endEditCallback_ && stepFlagBaseParamId_ != 0) {
            endEditCallback_(stepFlagBaseParamId_ + static_cast<uint32_t>(step));
        }

        setDirty(true);
        return VSTGUI::kMouseEventHandled;
    }

    CLASS_METHODS(ArpModifierLane, CControl)

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

        // Row labels in left margin (dimmed accent color)
        VSTGUI::CColor labelColor = darkenColor(accentColor_, 0.5f);
        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 8.0);
        context->setFont(font);
        context->setFontColor(labelColor);

        for (int r = 0; r < kRowCount; ++r) {
            float rowTop = bodyTop + static_cast<float>(r) * kRowHeight;
            VSTGUI::CRect labelRect(bodyLeft + 2.0, rowTop,
                                     bodyLeft + kLeftMargin - 2.0, rowTop + kRowHeight);
            context->drawString(VSTGUI::UTF8String(kRowLabels[r]), labelRect,
                               VSTGUI::kLeftText);
        }

        // Draw dots for each step and row
        float contentLeft = bodyLeft + kLeftMargin;
        float contentWidth = bodyRight - contentLeft;
        if (numSteps_ <= 0 || contentWidth <= 0.0f) return;

        float stepWidth = contentWidth / static_cast<float>(numSteps_);

        VSTGUI::CColor activeDotColor = accentColor_;
        VSTGUI::CColor inactiveDotColor = darkenColor(accentColor_, 0.25f);

        for (int i = 0; i < numSteps_; ++i) {
            uint8_t flags = getStepFlags(i);

            for (int r = 0; r < kRowCount; ++r) {
                float dotX = contentLeft + static_cast<float>(i) * stepWidth + stepWidth / 2.0f;
                float dotY = bodyTop + static_cast<float>(r) * kRowHeight + kRowHeight / 2.0f;

                // Determine if this dot is "active"
                bool active = false;
                if (r == 0) {
                    // Row 0 (Rest): active when kStepActive is OFF
                    active = (flags & 0x01) == 0;
                } else {
                    // Rows 1-3: active when the corresponding bit is ON
                    active = (flags & kRowBits[static_cast<size_t>(r)]) != 0;
                }

                if (active) {
                    // Filled circle
                    auto path = VSTGUI::owned(context->createGraphicsPath());
                    if (path) {
                        path->addEllipse(VSTGUI::CRect(
                            dotX - kDotRadius, dotY - kDotRadius,
                            dotX + kDotRadius, dotY + kDotRadius));
                        context->setFillColor(activeDotColor);
                        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
                    }
                } else {
                    // Outline circle
                    auto path = VSTGUI::owned(context->createGraphicsPath());
                    if (path) {
                        path->addEllipse(VSTGUI::CRect(
                            dotX - kDotRadius, dotY - kDotRadius,
                            dotX + kDotRadius, dotY + kDotRadius));
                        context->setFrameColor(inactiveDotColor);
                        context->setLineWidth(1.0);
                        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
                    }
                }
            }
        }

        // Draw playhead overlay
        if (playheadStep_ >= 0 && playheadStep_ < numSteps_) {
            float overlayLeft = contentLeft +
                static_cast<float>(playheadStep_) * stepWidth;
            float overlayRight = overlayLeft + stepWidth;

            VSTGUI::CColor overlayColor = accentColor_;
            overlayColor.alpha = 40;
            context->setFillColor(overlayColor);
            VSTGUI::CRect overlay(overlayLeft, bodyTop, overlayRight, bodyBottom);
            context->drawRect(overlay, VSTGUI::kDrawFilled);
        }
    }

    void drawMiniPreview(VSTGUI::CDrawContext* context, const VSTGUI::CRect& vs) {
        // Collapsed preview: tiny dots in the header area
        float previewLeft = static_cast<float>(vs.left) + 80.0f;
        float previewRight = static_cast<float>(vs.right) - 4.0f;
        float previewTop = static_cast<float>(vs.top) + 2.0f;
        float previewBottom = static_cast<float>(vs.top) + ArpLaneHeader::kHeight - 2.0f;

        float previewWidth = previewRight - previewLeft;
        float previewHeight = previewBottom - previewTop;

        if (previewWidth <= 0.0f || previewHeight <= 0.0f || numSteps_ <= 0) return;

        float stepWidth = previewWidth / static_cast<float>(numSteps_);
        float miniDotRadius = 2.0f;

        VSTGUI::CColor activeDotColor = accentColor_;
        VSTGUI::CColor dimDotColor = darkenColor(accentColor_, 0.25f);

        for (int i = 0; i < numSteps_; ++i) {
            uint8_t flags = getStepFlags(i);
            // Non-default = either kStepActive is cleared or any other flag is set
            bool nonDefault = (flags & 0x0F) != 0x01;

            float dotX = previewLeft + static_cast<float>(i) * stepWidth + stepWidth / 2.0f;
            float dotY = previewTop + previewHeight / 2.0f;

            auto path = VSTGUI::owned(context->createGraphicsPath());
            if (path) {
                path->addEllipse(VSTGUI::CRect(
                    dotX - miniDotRadius, dotY - miniDotRadius,
                    dotX + miniDotRadius, dotY + miniDotRadius));

                if (nonDefault) {
                    context->setFillColor(activeDotColor);
                    context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
                } else {
                    context->setFrameColor(dimDotColor);
                    context->setLineWidth(1.0);
                    context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
                }
            }
        }
    }

    // =========================================================================
    // State
    // =========================================================================

    ArpLaneHeader header_;
    std::array<uint8_t, kMaxSteps> stepFlags_{};
    int numSteps_ = 16;
    int playheadStep_ = -1;
    VSTGUI::CColor accentColor_{192, 112, 124, 255};
    uint32_t stepFlagBaseParamId_ = 0;
    uint32_t playheadParamId_ = 0;
    float expandedHeight_ = kBodyHeight + ArpLaneHeader::kHeight;  // 44.0f + 16.0f = 60.0f
    ParameterCallback paramCallback_;
    EditCallback beginEditCallback_;
    EditCallback endEditCallback_;
    std::function<void()> collapseCallback_;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct ArpModifierLaneCreator : VSTGUI::ViewCreatorAdapter {
    ArpModifierLaneCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override { return "ArpModifierLane"; }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Arp Modifier Lane";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new ArpModifierLane(VSTGUI::CRect(0, 0, 500, 60), nullptr, -1);
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override {
        auto* lane = dynamic_cast<ArpModifierLane*>(view);
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

        // Step flag base param ID
        const auto* baseIdStr = attributes.getAttributeValue("step-flag-base-param-id");
        if (baseIdStr) {
            auto id = static_cast<uint32_t>(std::stoul(*baseIdStr));
            lane->setStepFlagBaseParamId(id);
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
        attributeNames.emplace_back("step-flag-base-param-id");
        attributeNames.emplace_back("length-param-id");
        attributeNames.emplace_back("playhead-param-id");
        return true;
    }

    AttrType getAttributeType(const std::string& attributeName) const override {
        if (attributeName == "accent-color") return kColorType;
        if (attributeName == "lane-name") return kStringType;
        if (attributeName == "step-flag-base-param-id") return kStringType;
        if (attributeName == "length-param-id") return kStringType;
        if (attributeName == "playhead-param-id") return kStringType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override {
        auto* lane = dynamic_cast<ArpModifierLane*>(view);
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

inline ArpModifierLaneCreator gArpModifierLaneCreator;

} // namespace Krate::Plugins
