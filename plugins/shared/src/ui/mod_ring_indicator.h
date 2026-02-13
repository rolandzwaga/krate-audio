#pragma once

// ==============================================================================
// ModRingIndicator - Colored Arc Overlay for Destination Knobs
// ==============================================================================
// A CView overlay placed on destination knobs that renders colored arcs
// showing modulation ranges from active routes. Supports up to 4 individual
// arcs stacked by creation order, with a composite gray arc for 5+ sources.
//
// Observes modulation parameters via IDependent (no timer, FR-030).
// Supports click-to-select (controller mediation, FR-027) and hover tooltips.
//
// Registered as "ModRingIndicator" via VSTGUI ViewCreator system.
// Spec: 049-mod-matrix-grid (FR-020 to FR-030)
// ==============================================================================

#include "mod_source_colors.h"
#include "color_utils.h"

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/detail/uiviewcreatorattributes.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace Krate::Plugins {

// ==============================================================================
// ModRingIndicator
// ==============================================================================

class ModRingIndicator : public VSTGUI::CView {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr int kMaxVisibleArcs = 4;
    static constexpr double kStartAngleDeg = 135.0;   // Bottom-left (matches ArcKnob)
    static constexpr double kEndAngleDeg = 405.0;     // Bottom-right (135 + 270)
    static constexpr double kSweepDeg = 270.0;

    // =========================================================================
    // ArcInfo
    // =========================================================================

    struct ArcInfo {
        float amount = 0.0f;           // [-1.0, +1.0]
        VSTGUI::CColor color{220, 170, 60, 255};
        int sourceIndex = 0;
        int destIndex = 0;
        bool bypassed = false;
    };

    // =========================================================================
    // Construction
    // =========================================================================

    explicit ModRingIndicator(const VSTGUI::CRect& size)
        : CView(size) {
        setMouseEnabled(true);
        setTransparency(true);
    }

    ModRingIndicator(const ModRingIndicator& other)
        : CView(other)
        , baseValue_(other.baseValue_)
        , arcs_(other.arcs_)
        , strokeWidth_(other.strokeWidth_)
        , destinationIndex_(other.destinationIndex_) {}

    CLASS_METHODS(ModRingIndicator, CView)

    // =========================================================================
    // Configuration
    // =========================================================================

    void setBaseValue(float normalizedValue) {
        baseValue_ = std::clamp(normalizedValue, 0.0f, 1.0f);
        setDirty();
    }

    [[nodiscard]] float getBaseValue() const { return baseValue_; }

    void setArcs(const std::vector<ArcInfo>& arcs) {
        arcs_ = arcs;
        // Remove bypassed arcs (FR-019)
        arcs_.erase(
            std::remove_if(arcs_.begin(), arcs_.end(),
                           [](const ArcInfo& a) { return a.bypassed; }),
            arcs_.end());
        setDirty();
    }

    [[nodiscard]] const std::vector<ArcInfo>& getArcs() const { return arcs_; }

    void setController(Steinberg::Vst::EditController* controller) {
        controller_ = controller;
    }

    /// Destination index this indicator is associated with (ModDestination enum value).
    /// Used by the Controller to identify which destination knob this overlays.
    void setDestinationIndex(int index) { destinationIndex_ = index; }
    [[nodiscard]] int getDestinationIndex() const { return destinationIndex_; }

    void setStrokeWidth(float width) { strokeWidth_ = width; setDirty(); }
    [[nodiscard]] float getStrokeWidth() const { return strokeWidth_; }

    // =========================================================================
    // Drawing (FR-020 to FR-026)
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        if (arcs_.empty()) {
            setDirty(false);
            return;
        }

        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        VSTGUI::CRect vs = getViewSize();
        double dim = std::min(vs.getWidth(), vs.getHeight());
        double radius = (dim / 2.0) - strokeWidth_ - 1.0;
        double cx = vs.left + vs.getWidth() / 2.0;
        double cy = vs.top + vs.getHeight() / 2.0;

        if (radius < 2.0) {
            setDirty(false);
            return;
        }

        VSTGUI::CRect arcRect(cx - radius, cy - radius,
                               cx + radius, cy + radius);

        // Determine which arcs to draw
        if (arcs_.size() <= static_cast<size_t>(kMaxVisibleArcs)) {
            // Draw all individual arcs (most recent on top, FR-025)
            for (const auto& arc : arcs_) {
                drawSingleArc(context, arcRect, arc);
            }
        } else {
            // Composite mode: merge oldest into gray arc, show 4 newest (FR-026)
            // Draw composite gray arc first (underneath)
            float compositeAmount = 0.0f;
            for (size_t i = 0; i < arcs_.size() - kMaxVisibleArcs; ++i) {
                compositeAmount += arcs_[i].amount;
            }
            compositeAmount = std::clamp(compositeAmount, -1.0f, 1.0f);

            ArcInfo compositeArc;
            compositeArc.amount = compositeAmount;
            compositeArc.color = VSTGUI::CColor(140, 140, 145, 200);
            drawSingleArc(context, arcRect, compositeArc);

            // Draw "+" label at composite arc midpoint (FR-026)
            drawCompositeLabel(context, arcRect, compositeAmount, cx, cy, radius);

            // Draw 4 most recent individual arcs on top
            for (size_t i = arcs_.size() - kMaxVisibleArcs; i < arcs_.size(); ++i) {
                drawSingleArc(context, arcRect, arcs_[i]);
            }
        }

        setDirty(false);
    }

    // =========================================================================
    // Mouse Interaction (FR-027, FR-028)
    // =========================================================================

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {
        if (!(buttons & VSTGUI::kLButton))
            return VSTGUI::kMouseEventNotHandled;

        // Hit test against arcs - find the nearest one
        int hitIndex = hitTestArc(where);
        if (hitIndex >= 0 && hitIndex < static_cast<int>(arcs_.size())) {
            // Notify controller for cross-component selection (FR-027)
            if (selectCallback_) {
                selectCallback_(arcs_[static_cast<size_t>(hitIndex)].sourceIndex,
                                arcs_[static_cast<size_t>(hitIndex)].destIndex);
            }
            return VSTGUI::kMouseEventHandled;
        }
        return VSTGUI::kMouseEventNotHandled;
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& /*buttons*/) override {
        // Dynamic tooltip updates (FR-028)
        int hitIndex = hitTestArc(where);
        if (hitIndex >= 0 && hitIndex < static_cast<int>(arcs_.size())) {
            const auto& arc = arcs_[static_cast<size_t>(hitIndex)];
            std::ostringstream tooltip;
            tooltip << sourceNameForTab(0, arc.sourceIndex)
                    << " -> " << destinationNameForTab(0, arc.destIndex)
                    << ": " << (arc.amount >= 0.0f ? "+" : "")
                    << std::fixed << std::setprecision(2) << arc.amount;
            setTooltipText(VSTGUI::UTF8String(tooltip.str()).data());
        } else {
            setTooltipText(nullptr);
        }
        return VSTGUI::kMouseEventHandled;
    }

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Called when this view is removed from its parent (e.g. by UIViewSwitchContainer).
    /// Fires the removed callback so the controller can null out cached pointers.
    bool removed(VSTGUI::CView* parent) override {
        if (removedCallback_) removedCallback_();
        return CView::removed(parent);
    }

    // =========================================================================
    // Callbacks
    // =========================================================================

    using SelectCallback = std::function<void(int sourceIndex, int destIndex)>;
    void setSelectCallback(SelectCallback cb) { selectCallback_ = std::move(cb); }

    using RemovedCallback = std::function<void()>;
    void setRemovedCallback(RemovedCallback cb) { removedCallback_ = std::move(cb); }

private:
    // =========================================================================
    // Drawing Helpers
    // =========================================================================

    /// Convert normalized [0,1] to angle in degrees (135 to 405)
    [[nodiscard]] static double valueToAngleDeg(float normalizedValue) {
        return kStartAngleDeg + static_cast<double>(normalizedValue) * kSweepDeg;
    }

    void drawSingleArc(VSTGUI::CDrawContext* context,
                       const VSTGUI::CRect& arcRect,
                       const ArcInfo& arc) const {
        if (std::abs(arc.amount) < 0.001f)
            return;

        // Compute arc start and end based on base value and amount
        float arcStart = baseValue_;
        float arcEnd = baseValue_ + arc.amount;

        // Clamp at 0.0 and 1.0 (FR-022)
        arcEnd = std::clamp(arcEnd, 0.0f, 1.0f);

        if (std::abs(arcEnd - arcStart) < 0.001f)
            return;

        double startDeg = valueToAngleDeg(std::min(arcStart, arcEnd));
        double endDeg = valueToAngleDeg(std::max(arcStart, arcEnd));

        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path)
            return;

        path->addArc(arcRect, startDeg, endDeg, true);

        context->setFrameColor(arc.color);
        context->setLineWidth(strokeWidth_);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
    }

    void drawCompositeLabel(VSTGUI::CDrawContext* context,
                            [[maybe_unused]] const VSTGUI::CRect& arcRect,
                            float compositeAmount,
                            double cx, double cy, double radius) const {
        // Position at midpoint of composite arc
        float midValue = baseValue_ + compositeAmount / 2.0f;
        midValue = std::clamp(midValue, 0.0f, 1.0f);
        double angleDeg = valueToAngleDeg(midValue);
        double angleRad = angleDeg * M_PI / 180.0;

        double labelX = cx + (radius + 8.0) * std::cos(angleRad);
        double labelY = cy + (radius + 8.0) * std::sin(angleRad);

        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 10.0);
        context->setFont(font);
        context->setFontColor(VSTGUI::CColor(180, 180, 185, 255));

        VSTGUI::CRect labelRect(labelX - 6.0, labelY - 6.0,
                                 labelX + 6.0, labelY + 6.0);
        context->drawString(VSTGUI::UTF8String("+"), labelRect,
                            VSTGUI::kCenterText, true);
    }

    /// Hit test: find the arc index at the given point.
    /// Returns -1 if no arc is hit.
    [[nodiscard]] int hitTestArc(const VSTGUI::CPoint& where) const {
        if (arcs_.empty())
            return -1;

        VSTGUI::CRect vs = getViewSize();
        double cx = vs.left + vs.getWidth() / 2.0;
        double cy = vs.top + vs.getHeight() / 2.0;
        double dim = std::min(vs.getWidth(), vs.getHeight());
        double radius = (dim / 2.0) - strokeWidth_ - 1.0;

        // Check if point is near the arc ring
        double dx = where.x - cx;
        double dy = where.y - cy;
        double dist = std::sqrt(dx * dx + dy * dy);
        double hitTolerance = strokeWidth_ * 2.0;

        if (std::abs(dist - radius) > hitTolerance)
            return -1;

        // Calculate angle of the click point
        double angleDeg = std::atan2(dy, dx) * 180.0 / M_PI;
        if (angleDeg < 0) angleDeg += 360.0;

        // Check each arc (most recent first for top-level priority)
        for (int i = static_cast<int>(arcs_.size()) - 1; i >= 0; --i) {
            const auto& arc = arcs_[static_cast<size_t>(i)];
            float arcStart = baseValue_;
            float arcEnd = std::clamp(baseValue_ + arc.amount, 0.0f, 1.0f);

            double startDeg = valueToAngleDeg(std::min(arcStart, arcEnd));
            double endDeg = valueToAngleDeg(std::max(arcStart, arcEnd));

            // Normalize angle for comparison
            if (angleDeg >= startDeg && angleDeg <= endDeg)
                return i;
        }
        return -1;
    }

    // =========================================================================
    // State
    // =========================================================================

    float baseValue_ = 0.5f;
    std::vector<ArcInfo> arcs_;
    Steinberg::Vst::EditController* controller_ = nullptr;
    float strokeWidth_ = 3.0f;
    int destinationIndex_ = -1;     // ModDestination enum value (-1 = unset)
    SelectCallback selectCallback_;
    RemovedCallback removedCallback_;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct ModRingIndicatorCreator : VSTGUI::ViewCreatorAdapter {
    ModRingIndicatorCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override {
        return "ModRingIndicator";
    }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCView;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Mod Ring Indicator";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new ModRingIndicator(VSTGUI::CRect(0, 0, 50, 50));
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* /*description*/) const override {
        auto* indicator = dynamic_cast<ModRingIndicator*>(view);
        if (!indicator)
            return false;

        double d;
        if (attributes.getDoubleAttribute("stroke-width", d))
            indicator->setStrokeWidth(static_cast<float>(d));

        double destIdx;
        if (attributes.getDoubleAttribute("dest-index", destIdx))
            indicator->setDestinationIndex(static_cast<int>(destIdx));

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("stroke-width");
        attributeNames.emplace_back("dest-index");
        return true;
    }

    AttrType getAttributeType(
        const std::string& attributeName) const override {
        if (attributeName == "stroke-width") return kFloatType;
        if (attributeName == "dest-index") return kIntegerType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* /*desc*/) const override {
        auto* indicator = dynamic_cast<ModRingIndicator*>(view);
        if (!indicator)
            return false;

        if (attributeName == "stroke-width") {
            stringValue = VSTGUI::UIAttributes::doubleToString(
                indicator->getStrokeWidth());
            return true;
        }
        if (attributeName == "dest-index") {
            stringValue = std::to_string(indicator->getDestinationIndex());
            return true;
        }
        return false;
    }
};

inline ModRingIndicatorCreator gModRingIndicatorCreator;

} // namespace Krate::Plugins
