#pragma once

// ==============================================================================
// ModHeatmap - Source-by-Destination Grid Visualization
// ==============================================================================
// A read-only CView that displays a heatmap grid showing modulation routing
// intensity. Cell color = source color, brightness = |amount|.
// Supports click-to-select and hover tooltips.
//
// Global mode: 10 sources x 11 destinations grid
// Voice mode:   7 sources x  7 destinations grid
//
// Registered as "ModHeatmap" via VSTGUI ViewCreator system.
// Spec: 049-mod-matrix-grid (FR-031 to FR-038)
// ==============================================================================

#include "mod_source_colors.h"
#include "color_utils.h"

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/detail/uiviewcreatorattributes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <sstream>
#include <iomanip>

namespace Krate::Plugins {

// ==============================================================================
// ModHeatmap
// ==============================================================================

class ModHeatmap : public VSTGUI::CView {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr int kMaxSources = 10;      // Global mode max
    static constexpr int kMaxDestinations = 11;  // Global mode max
    static constexpr VSTGUI::CCoord kHeaderHeight = 16.0;
    static constexpr VSTGUI::CCoord kRowHeaderWidth = 30.0;

    // =========================================================================
    // Cell Data
    // =========================================================================

    struct CellData {
        float amount = 0.0f;   // [-1.0, +1.0]
        bool active = false;
    };

    // =========================================================================
    // Construction
    // =========================================================================

    explicit ModHeatmap(const VSTGUI::CRect& size)
        : CView(size) {
        setMouseEnabled(true);
    }

    ModHeatmap(const ModHeatmap& other)
        : CView(other)
        , mode_(other.mode_)
        , cellData_(other.cellData_) {}

    CLASS_METHODS(ModHeatmap, CView)

    // =========================================================================
    // Data Interface
    // =========================================================================

    void setCell(int sourceRow, int destCol, float amount, bool active) {
        if (sourceRow >= 0 && sourceRow < kMaxSources &&
            destCol >= 0 && destCol < kMaxDestinations) {
            cellData_[static_cast<size_t>(sourceRow)][static_cast<size_t>(destCol)] = {amount, active};
            setDirty();
        }
    }

    void setMode(int mode) {
        mode_ = std::clamp(mode, 0, 1);
        setDirty();
    }

    [[nodiscard]] int getMode() const { return mode_; }

    // =========================================================================
    // Callbacks
    // =========================================================================

    using CellClickCallback = std::function<void(int sourceIndex, int destIndex)>;
    void setCellClickCallback(CellClickCallback cb) { cellClickCallback_ = std::move(cb); }

    // =========================================================================
    // Drawing (FR-031 to FR-036)
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        VSTGUI::CRect vs = getViewSize();
        VSTGUI::CColor bgColor(30, 30, 33, 255);
        context->setFillColor(bgColor);
        context->drawRect(vs, VSTGUI::kDrawFilled);

        int numSources = (mode_ == 0) ? kNumGlobalSources : kNumVoiceSources;
        int numDests = (mode_ == 0) ? kNumGlobalDestinations : kNumVoiceDestinations;

        VSTGUI::CCoord availW = vs.getWidth() - kRowHeaderWidth;
        VSTGUI::CCoord availH = vs.getHeight() - kHeaderHeight;
        VSTGUI::CCoord cellW = (numDests > 0) ? availW / numDests : 0;
        VSTGUI::CCoord cellH = (numSources > 0) ? availH / numSources : 0;

        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 8.0);
        context->setFont(font);

        // Draw column headers (destination abbreviations, FR-035)
        bool isGlobal = (mode_ == 0);
        for (int d = 0; d < numDests; ++d) {
            VSTGUI::CRect headerRect(
                vs.left + kRowHeaderWidth + d * cellW, vs.top,
                vs.left + kRowHeaderWidth + (d + 1) * cellW, vs.top + kHeaderHeight);
            context->setFontColor(VSTGUI::CColor(140, 140, 150, 255));
            context->drawString(
                VSTGUI::UTF8String(destinationAbbrForIndex(d, isGlobal)),
                headerRect, VSTGUI::kCenterText, true);
        }

        // Draw row headers (source abbreviations, FR-036)
        for (int s = 0; s < numSources; ++s) {
            VSTGUI::CRect headerRect(
                vs.left, vs.top + kHeaderHeight + s * cellH,
                vs.left + kRowHeaderWidth, vs.top + kHeaderHeight + (s + 1) * cellH);
            context->setFontColor(sourceColorForIndex(s));
            context->drawString(
                VSTGUI::UTF8String(sourceAbbrForIndex(s)),
                headerRect, VSTGUI::kCenterText, true);
        }

        // Draw cells (FR-033, FR-034)
        for (int s = 0; s < numSources; ++s) {
            for (int d = 0; d < numDests; ++d) {
                VSTGUI::CRect cellRect(
                    vs.left + kRowHeaderWidth + d * cellW,
                    vs.top + kHeaderHeight + s * cellH,
                    vs.left + kRowHeaderWidth + (d + 1) * cellW,
                    vs.top + kHeaderHeight + (s + 1) * cellH);

                const auto& cell = cellData_[static_cast<size_t>(s)][static_cast<size_t>(d)];

                if (cell.active) {
                    // Active cell: source color * |amount| intensity (FR-033)
                    VSTGUI::CColor srcColor = sourceColorForIndex(s);
                    float intensity = std::abs(cell.amount);
                    VSTGUI::CColor cellColor = lerpColor(bgColor, srcColor, intensity);
                    context->setFillColor(cellColor);
                } else {
                    // Empty cell: dark background (FR-034)
                    context->setFillColor(VSTGUI::CColor(25, 25, 28, 255));
                }
                context->drawRect(cellRect, VSTGUI::kDrawFilled);

                // Cell border
                context->setFrameColor(VSTGUI::CColor(40, 40, 43, 255));
                context->setLineWidth(0.5);
                context->drawRect(cellRect, VSTGUI::kDrawStroked);
            }
        }

        setDirty(false);
    }

    // =========================================================================
    // Mouse Interaction (FR-037, FR-038)
    // =========================================================================

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {
        if (!(buttons & VSTGUI::kLButton))
            return VSTGUI::kMouseEventNotHandled;

        auto [s, d] = hitTestCell(where);
        if (s >= 0 && d >= 0) {
            const auto& cell = cellData_[static_cast<size_t>(s)][static_cast<size_t>(d)];
            if (cell.active && cellClickCallback_) {
                cellClickCallback_(s, d);
                return VSTGUI::kMouseEventHandled;
            }
        }
        return VSTGUI::kMouseEventNotHandled;
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& /*buttons*/) override {
        auto [s, d] = hitTestCell(where);
        if (s >= 0 && d >= 0) {
            const auto& cell = cellData_[static_cast<size_t>(s)][static_cast<size_t>(d)];
            if (cell.active) {
                bool isGlobal = (mode_ == 0);
                std::ostringstream tooltip;
                tooltip << sourceNameForIndex(s)
                        << " -> " << destinationNameForIndex(d)
                        << ": " << (cell.amount >= 0.0f ? "+" : "")
                        << std::fixed << std::setprecision(2) << cell.amount;
                setTooltipText(VSTGUI::UTF8String(tooltip.str()).data());
                (void)isGlobal; // Used for abbreviation selection in future
            } else {
                setTooltipText(nullptr);
            }
        } else {
            setTooltipText(nullptr);
        }
        return VSTGUI::kMouseEventHandled;
    }

private:
    // =========================================================================
    // Hit Testing
    // =========================================================================

    struct CellHit { int source; int dest; };

    [[nodiscard]] CellHit hitTestCell(const VSTGUI::CPoint& where) const {
        VSTGUI::CRect vs = getViewSize();
        int numSources = (mode_ == 0) ? kNumGlobalSources : kNumVoiceSources;
        int numDests = (mode_ == 0) ? kNumGlobalDestinations : kNumVoiceDestinations;

        VSTGUI::CCoord availW = vs.getWidth() - kRowHeaderWidth;
        VSTGUI::CCoord availH = vs.getHeight() - kHeaderHeight;
        VSTGUI::CCoord cellW = (numDests > 0) ? availW / numDests : 0;
        VSTGUI::CCoord cellH = (numSources > 0) ? availH / numSources : 0;

        VSTGUI::CCoord localX = where.x - vs.left - kRowHeaderWidth;
        VSTGUI::CCoord localY = where.y - vs.top - kHeaderHeight;

        if (localX < 0 || localY < 0 || cellW <= 0 || cellH <= 0)
            return {-1, -1};

        int d = static_cast<int>(localX / cellW);
        int s = static_cast<int>(localY / cellH);

        if (s >= 0 && s < numSources && d >= 0 && d < numDests)
            return {s, d};

        return {-1, -1};
    }

    // =========================================================================
    // State
    // =========================================================================

    int mode_ = 0; // 0=Global, 1=Voice
    std::array<std::array<CellData, kMaxDestinations>, kMaxSources> cellData_{};
    CellClickCallback cellClickCallback_;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct ModHeatmapCreator : VSTGUI::ViewCreatorAdapter {
    ModHeatmapCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override {
        return "ModHeatmap";
    }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCView;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Mod Heatmap";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new ModHeatmap(VSTGUI::CRect(0, 0, 300, 100));
    }

    bool apply(VSTGUI::CView* /*view*/,
               const VSTGUI::UIAttributes& /*attributes*/,
               const VSTGUI::IUIDescription* /*description*/) const override {
        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& /*attributeNames*/) const override {
        return true;
    }

    AttrType getAttributeType(
        const std::string& /*attributeName*/) const override {
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* /*view*/,
                           const std::string& /*attributeName*/,
                           std::string& /*stringValue*/,
                           const VSTGUI::IUIDescription* /*desc*/) const override {
        return false;
    }
};

inline ModHeatmapCreator gModHeatmapCreator;

} // namespace Krate::Plugins
