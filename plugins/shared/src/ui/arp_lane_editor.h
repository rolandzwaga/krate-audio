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
//
// This component is plugin-agnostic: it communicates via callbacks and
// configurable parameter IDs. No dependency on any specific plugin.
//
// Registered as "ArpLaneEditor" via VSTGUI ViewCreator system.
// ==============================================================================

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
    kPitch = 2,    // Phase 11b placeholder
    kRatchet = 3   // Phase 11b placeholder
};

// ==============================================================================
// ArpLaneEditor Control
// ==============================================================================

class ArpLaneEditor : public StepPatternEditor {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kHeaderHeight = 16.0f;
    static constexpr float kMiniPreviewHeight = 12.0f;
    static constexpr float kMiniPreviewPaddingTop = 2.0f;
    static constexpr float kMiniPreviewPaddingBottom = 2.0f;
    static constexpr float kCollapseTriangleSize = 8.0f;
    static constexpr float kLengthDropdownX = 80.0f;
    static constexpr float kLengthDropdownWidth = 36.0f;

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
        , lengthParamId_(other.lengthParamId_)
        , playheadParamId_(other.playheadParamId_)
        , isCollapsed_(other.isCollapsed_) {
        // Re-derive colors from accent
        setAccentColor(accentColor_);
    }

    // =========================================================================
    // Lane Configuration
    // =========================================================================

    void setLaneType(ArpLaneType type) { laneType_ = type; }
    [[nodiscard]] ArpLaneType getLaneType() const { return laneType_; }

    void setLaneName(const std::string& name) { laneName_ = name; }
    [[nodiscard]] const std::string& getLaneName() const { return laneName_; }

    void setAccentColor(const VSTGUI::CColor& color) {
        accentColor_ = color;
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
    // Parameter Binding
    // =========================================================================

    void setLengthParamId(uint32_t paramId) { lengthParamId_ = paramId; }
    [[nodiscard]] uint32_t getLengthParamId() const { return lengthParamId_; }

    void setLengthParamCallback(std::function<void(uint32_t, float)> cb) {
        lengthParamCallback_ = std::move(cb);
    }

    void setPlayheadParamId(uint32_t paramId) { playheadParamId_ = paramId; }
    [[nodiscard]] uint32_t getPlayheadParamId() const { return playheadParamId_; }

    // =========================================================================
    // Collapse/Expand
    // =========================================================================

    void setCollapsed(bool collapsed) {
        isCollapsed_ = collapsed;
        if (collapseCallback_) {
            collapseCallback_();
        }
        setDirty();
    }

    [[nodiscard]] bool isCollapsed() const { return isCollapsed_; }

    void setCollapseCallback(std::function<void()> cb) {
        collapseCallback_ = std::move(cb);
    }

    // =========================================================================
    // Height Queries
    // =========================================================================

    /// Get expanded height: total view height (header + body)
    [[nodiscard]] float getExpandedHeight() const {
        return static_cast<float>(getViewSize().getHeight());
    }

    /// Get collapsed height: just the header
    [[nodiscard]] float getCollapsedHeight() const {
        return kHeaderHeight;
    }

    // =========================================================================
    // CControl Overrides
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        VSTGUI::CRect vs = getViewSize();

        if (isCollapsed_) {
            // Draw header with miniature preview
            drawHeader(context, vs);
            drawMiniaturePreview(context, vs);
        } else {
            // Draw header, then delegate body to base class
            drawHeader(context, vs);
            StepPatternEditor::draw(context);
        }

        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {

        VSTGUI::CRect vs = getViewSize();
        VSTGUI::CRect headerRect(vs.left, vs.top, vs.right, vs.top + kHeaderHeight);

        // Check if click is in the header area
        if (headerRect.pointInside(where)) {
            float localX = static_cast<float>(where.x - vs.left);

            // Toggle zone is the left ~24px (triangle + padding)
            if (localX < 24.0f) {
                setCollapsed(!isCollapsed_);
                return VSTGUI::kMouseEventHandled;
            }

            // Length dropdown zone
            if (localX >= kLengthDropdownX &&
                localX < kLengthDropdownX + kLengthDropdownWidth) {
                openLengthDropdown(where);
                return VSTGUI::kMouseEventHandled;
            }
        }

        // If collapsed, don't delegate to base class
        if (isCollapsed_) {
            return VSTGUI::kMouseEventHandled;
        }

        // Delegate to base class for bar interaction
        return StepPatternEditor::onMouseDown(where, buttons);
    }

    CLASS_METHODS(ArpLaneEditor, StepPatternEditor)

private:
    // =========================================================================
    // Drawing Helpers
    // =========================================================================

    void drawHeader(VSTGUI::CDrawContext* context, const VSTGUI::CRect& vs) {
        // Header background
        VSTGUI::CRect headerRect(vs.left, vs.top, vs.right, vs.top + kHeaderHeight);
        VSTGUI::CColor headerBg{30, 30, 33, 255};
        context->setFillColor(headerBg);
        context->drawRect(headerRect, VSTGUI::kDrawFilled);

        // Collapse triangle
        drawCollapseTriangle(context, vs);

        // Lane name label
        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 9.0);
        context->setFont(font);
        context->setFontColor(accentColor_);

        VSTGUI::CRect nameRect(vs.left + 20.0, vs.top + 1.0,
                                vs.left + 80.0, vs.top + kHeaderHeight - 1.0);
        context->drawString(VSTGUI::UTF8String(laneName_), nameRect,
                           VSTGUI::kLeftText);

        // Length dropdown label (shows current step count)
        VSTGUI::CColor labelColor{160, 160, 165, 255};
        context->setFontColor(labelColor);

        std::string lengthText = std::to_string(getNumSteps());
        VSTGUI::CRect lengthRect(vs.left + kLengthDropdownX, vs.top + 1.0,
                                  vs.left + kLengthDropdownX + kLengthDropdownWidth,
                                  vs.top + kHeaderHeight - 1.0);
        context->drawString(VSTGUI::UTF8String(lengthText), lengthRect,
                           VSTGUI::kCenterText);

        // Small dropdown indicator triangle
        float triX = static_cast<float>(vs.left) + kLengthDropdownX + kLengthDropdownWidth - 6.0f;
        float triY = static_cast<float>(vs.top) + kHeaderHeight / 2.0f;
        auto triPath = VSTGUI::owned(context->createGraphicsPath());
        if (triPath) {
            triPath->beginSubpath(VSTGUI::CPoint(triX - 2.5, triY - 1.5));
            triPath->addLine(VSTGUI::CPoint(triX + 2.5, triY - 1.5));
            triPath->addLine(VSTGUI::CPoint(triX, triY + 1.5));
            triPath->closeSubpath();
            context->setFillColor(labelColor);
            context->drawGraphicsPath(triPath, VSTGUI::CDrawContext::kPathFilled);
        }
    }

    void openLengthDropdown(const VSTGUI::CPoint& where) {
        auto* frame = getFrame();
        if (!frame) return;

        // Create option menu with values kMinSteps through kMaxSteps
        VSTGUI::CRect menuRect(where.x, where.y, where.x + 1, where.y + 1);
        auto* menu = new VSTGUI::COptionMenu(menuRect, nullptr, -1);

        for (int i = kMinSteps; i <= kMaxSteps; ++i) {
            menu->addEntry(std::to_string(i));
        }

        // Set current selection
        int currentIndex = getNumSteps() - kMinSteps;
        menu->setCurrent(currentIndex);

        // Show popup and handle selection
        menu->setListener(nullptr);
        menu->popup(frame, where);

        int selectedIndex = menu->getCurrentIndex();
        if (selectedIndex >= 0) {
            int newSteps = selectedIndex + kMinSteps;
            if (newSteps != getNumSteps()) {
                setNumSteps(newSteps);
                setDirty(true);

                // Notify via callback with normalized value
                if (lengthParamCallback_ && lengthParamId_ != 0) {
                    float normalized = static_cast<float>(newSteps - 1) / 31.0f;
                    lengthParamCallback_(lengthParamId_, normalized);
                }
            }
        }

        menu->forget();
    }

    void drawCollapseTriangle(VSTGUI::CDrawContext* context,
                              const VSTGUI::CRect& vs) {
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path) return;

        float cx = static_cast<float>(vs.left) + 10.0f;
        float cy = static_cast<float>(vs.top) + kHeaderHeight / 2.0f;
        float half = kCollapseTriangleSize / 2.0f;

        if (isCollapsed_) {
            // Right-pointing triangle (>)
            path->beginSubpath(VSTGUI::CPoint(cx - half * 0.5f, cy - half));
            path->addLine(VSTGUI::CPoint(cx + half * 0.5f, cy));
            path->addLine(VSTGUI::CPoint(cx - half * 0.5f, cy + half));
            path->closeSubpath();
        } else {
            // Down-pointing triangle (v)
            path->beginSubpath(VSTGUI::CPoint(cx - half, cy - half * 0.5f));
            path->addLine(VSTGUI::CPoint(cx + half, cy - half * 0.5f));
            path->addLine(VSTGUI::CPoint(cx, cy + half * 0.5f));
            path->closeSubpath();
        }

        context->setFillColor(VSTGUI::CColor{180, 180, 185, 255});
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
    }

    void drawMiniaturePreview(VSTGUI::CDrawContext* context,
                              const VSTGUI::CRect& vs) {
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
    // State
    // =========================================================================

    ArpLaneType laneType_ = ArpLaneType::kVelocity;
    std::string laneName_;
    VSTGUI::CColor accentColor_{208, 132, 92, 255};
    float displayMin_ = 0.0f;
    float displayMax_ = 1.0f;
    std::string topLabel_ = "1.0";
    std::string bottomLabel_ = "0.0";
    uint32_t lengthParamId_ = 0;
    uint32_t playheadParamId_ = 0;
    bool isCollapsed_ = false;
    std::function<void()> collapseCallback_;
    std::function<void(uint32_t, float)> lengthParamCallback_;
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
