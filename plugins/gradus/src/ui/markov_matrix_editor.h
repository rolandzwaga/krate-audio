// =============================================================================
// MarkovMatrixEditor — Self-contained Markov Chain editing panel
// =============================================================================
// Spec 133 (Gradus v1.7): CViewContainer hosting a 7x7 transition matrix
// grid (scale-degree probabilities) PLUS an embedded preset dropdown
// (Uniform / Jazz / Minimal / Ambient / Classical / Custom) at the top.
//
// Two visual states:
//  - Expanded: 160x180 (top 20px = preset dropdown, bottom 160x160 = grid,
//              minimize button in top-right corner)
//  - Collapsed: 32x32 trigger button the user clicks to re-expand
//
// Humble object: the grid/hit-test geometry lives in
// markov_matrix_editor_logic.h. This file wires drawing, mouse handling,
// callback dispatch, and the child dropdown.
// =============================================================================

#pragma once

#include "plugin_ids.h"
#include "markov_matrix_editor_logic.h"

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"

#include "pluginterfaces/vst/vsttypes.h"

#include <array>
#include <cstdio>
#include <functional>

namespace Gradus {

class MarkovMatrixEditor : public VSTGUI::CViewContainer,
                           public VSTGUI::IControlListener {
public:
    static constexpr int kDim = MarkovMatrixEditorLogic::kDim;
    static constexpr int kNumCells = kDim * kDim;
    static constexpr int kNumPresets = 6;

    // Expanded view is the full editor; collapsed view is a small
    // trigger button the user can click to re-expand.
    static constexpr VSTGUI::CCoord kExpandedWidth  = 160.0;
    static constexpr VSTGUI::CCoord kExpandedHeight = 180.0;  // includes 20px dropdown strip
    static constexpr VSTGUI::CCoord kCollapsedSize  = 32.0;
    static constexpr VSTGUI::CCoord kDropdownStrip  = 20.0;   // top strip for preset dropdown
    static constexpr VSTGUI::CCoord kMinimizeBtn    = 14.0;

    explicit MarkovMatrixEditor(const VSTGUI::CRect& anchor)
        : CViewContainer(VSTGUI::CRect(anchor.left, anchor.top,
                                       anchor.left + kExpandedWidth,
                                       anchor.top + kExpandedHeight))
        , anchorTopLeft_(anchor.left, anchor.top)
    {
        setBackgroundColor({0, 0, 0, 0});  // our draw() paints the background
        setTransparency(true);

        // Initialize cells to Uniform (1/7) so the widget is usable before
        // any host-side sync arrives.
        constexpr float kUniform = 1.0f / 7.0f;
        for (auto& c : cellValues_) c = kUniform;

        // Create the preset dropdown child. It lives in the top 18px strip
        // (y=2..20) with a small right-side gap for the minimize button.
        const VSTGUI::CCoord dropLeft   = 4.0;
        const VSTGUI::CCoord dropRight  = kExpandedWidth - kMinimizeBtn - 6.0;
        const VSTGUI::CCoord dropTop    = 2.0;
        const VSTGUI::CCoord dropBottom = kDropdownStrip;
        presetDropdown_ = new VSTGUI::COptionMenu(
            VSTGUI::CRect(dropLeft, dropTop, dropRight, dropBottom),
            this,  // IControlListener
            static_cast<int32_t>(Gradus::kArpMarkovPresetId));
        presetDropdown_->addEntry("Uniform");
        presetDropdown_->addEntry("Jazz");
        presetDropdown_->addEntry("Minimal");
        presetDropdown_->addEntry("Ambient");
        presetDropdown_->addEntry("Classical");
        presetDropdown_->addEntry("Custom");
        presetDropdown_->setMin(0.0f);
        presetDropdown_->setMax(static_cast<float>(kNumPresets - 1));
        presetDropdown_->setValue(0.0f);
        presetDropdown_->setBackColor({0x22, 0x22, 0x28, 0xFF});
        presetDropdown_->setFrameColor({0x50, 0x50, 0x60, 0xFF});
        presetDropdown_->setFontColor({0xE0, 0xE0, 0xE8, 0xFF});
        presetDropdown_->setFont(VSTGUI::kNormalFontSmaller);
        addView(presetDropdown_);
    }

    ~MarkovMatrixEditor() override = default;

    // -------------------------------------------------------------------------
    // Expand / collapse
    // -------------------------------------------------------------------------

    [[nodiscard]] bool isExpanded() const { return expanded_; }

    void setExpanded(bool expand)
    {
        if (expand == expanded_) return;
        expanded_ = expand;

        const VSTGUI::CCoord w = expand ? kExpandedWidth  : kCollapsedSize;
        const VSTGUI::CCoord h = expand ? kExpandedHeight : kCollapsedSize;
        VSTGUI::CRect newFrame(anchorTopLeft_.x, anchorTopLeft_.y,
                               anchorTopLeft_.x + w,
                               anchorTopLeft_.y + h);
        if (auto* parent = getParentView()) {
            VSTGUI::CRect dirty = getViewSize();
            dirty.unite(newFrame);
            parent->invalidRect(dirty);
        }
        setViewSize(newFrame);
        setMouseableArea(newFrame);

        // Hide the child dropdown entirely when collapsed (otherwise it
        // would still capture clicks inside its own rect).
        if (presetDropdown_) presetDropdown_->setVisible(expand);

        invalid();
    }

    // -------------------------------------------------------------------------
    // Cell state accessors
    // -------------------------------------------------------------------------

    [[nodiscard]] float getCellValue(int row, int col) const
    {
        if (row < 0 || row >= kDim || col < 0 || col >= kDim) return 0.0f;
        return cellValues_[static_cast<size_t>(row * kDim + col)];
    }

    /// Host-driven update (preset load, automation). Does NOT fire callbacks.
    /// Uses setDirty(true) because this may be called from a non-UI thread via
    /// Controller::setParamNormalized.
    void setCellValue(int row, int col, float value)
    {
        if (row < 0 || row >= kDim || col < 0 || col >= kDim) return;
        const size_t idx = static_cast<size_t>(row * kDim + col);
        const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        if (cellValues_[idx] != clamped) {
            cellValues_[idx] = clamped;
            setDirty(true);
        }
    }

    void setCellValueFlat(int flatIdx, float value)
    {
        if (flatIdx < 0 || flatIdx >= kNumCells) return;
        setCellValue(flatIdx / kDim, flatIdx % kDim, value);
    }

    /// Host-driven preset dropdown sync. Does NOT fire valueChanged.
    void setPresetValue(int presetIndex)
    {
        if (!presetDropdown_) return;
        if (presetIndex < 0 || presetIndex >= kNumPresets) return;
        const float v = static_cast<float>(presetIndex);
        if (presetDropdown_->getValue() != v) {
            // setValueNormalized directly so valueChanged doesn't fire
            presetDropdown_->setValue(v);
            presetDropdown_->invalid();
        }
    }

    // -------------------------------------------------------------------------
    // Parameter callbacks (same pattern as PinFlagStrip / ArpLaneEditor)
    // -------------------------------------------------------------------------

    using ParameterCallback =
        std::function<void(Steinberg::Vst::ParamID paramId, float normalizedValue)>;
    using EditGateCallback =
        std::function<void(Steinberg::Vst::ParamID paramId)>;

    void setParameterCallback(ParameterCallback cb) { paramCallback_ = std::move(cb); }
    void setBeginEditCallback(EditGateCallback cb)  { beginEditCallback_ = std::move(cb); }
    void setEndEditCallback  (EditGateCallback cb)  { endEditCallback_ = std::move(cb); }

    // -------------------------------------------------------------------------
    // IControlListener (for the embedded preset dropdown)
    // -------------------------------------------------------------------------

    void valueChanged(VSTGUI::CControl* control) override
    {
        if (control != presetDropdown_) return;
        const int presetIdx = static_cast<int>(
            presetDropdown_->getValue() + 0.5f);
        if (presetIdx < 0 || presetIdx >= kNumPresets) return;

        // Normalize index 0..5 onto [0, 1] for VST3 (stepCount=5 → 6 entries)
        const float normalized =
            static_cast<float>(presetIdx) / static_cast<float>(kNumPresets - 1);

        const auto paramId = static_cast<Steinberg::Vst::ParamID>(
            Gradus::kArpMarkovPresetId);
        if (beginEditCallback_) beginEditCallback_(paramId);
        if (paramCallback_)     paramCallback_(paramId, normalized);
        if (endEditCallback_)   endEditCallback_(paramId);
    }

    // -------------------------------------------------------------------------
    // CViewContainer overrides
    // -------------------------------------------------------------------------

    void drawBackgroundRect(VSTGUI::CDrawContext* context,
                            const VSTGUI::CRect& /*updateRect*/) override
    {
        if (expanded_) {
            drawExpanded(context);
        } else {
            drawCollapsed(context);
        }
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        // Let children (the preset dropdown) handle clicks inside their bounds
        // first. CViewContainer::onMouseDown dispatches to children and returns
        // handled if one of them consumed the event.
        auto childResult = CViewContainer::onMouseDown(where, buttons);
        if (childResult == VSTGUI::kMouseEventHandled) {
            return childResult;
        }

        if (!buttons.isLeftButton()) return VSTGUI::kMouseEventNotHandled;

        // Collapsed: any click expands the editor.
        if (!expanded_) {
            setExpanded(true);
            return VSTGUI::kMouseEventHandled;
        }

        // Expanded: check minimize button first (frame coords — `where` is
        // in parent's coordinate space).
        if (minimizeButtonFrameRect().pointInside(where)) {
            setExpanded(false);
            return VSTGUI::kMouseEventHandled;
        }

        // Cell edit (grid lives below the dropdown strip)
        const VSTGUI::CRect bounds = getViewSize();
        const float localX = static_cast<float>(where.x - bounds.left);
        const float localY = static_cast<float>(where.y - bounds.top - kDropdownStrip);
        const float gridW = static_cast<float>(bounds.getWidth());
        const float gridH = static_cast<float>(bounds.getHeight() - kDropdownStrip);
        const auto hit = MarkovMatrixEditorLogic::cellAtPoint(localX, localY, gridW, gridH);
        if (!hit.valid()) return VSTGUI::kMouseEventNotHandled;

        dragCell_ = hit;
        dragEditStarted_ = false;
        updateCellFromDrag(localY);
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        // Dropdown popup consumes its own moves while open
        auto childResult = CViewContainer::onMouseMoved(where, buttons);
        if (childResult == VSTGUI::kMouseEventHandled) return childResult;

        if (!expanded_) return VSTGUI::kMouseEventNotHandled;
        if (!buttons.isLeftButton() || !dragCell_.valid())
            return VSTGUI::kMouseEventNotHandled;

        const VSTGUI::CRect bounds = getViewSize();
        const float localY = static_cast<float>(where.y - bounds.top - kDropdownStrip);
        updateCellFromDrag(localY);
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseUp(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        auto childResult = CViewContainer::onMouseUp(where, buttons);
        if (childResult == VSTGUI::kMouseEventHandled) return childResult;

        if (dragCell_.valid() && dragEditStarted_ && endEditCallback_) {
            const auto paramId = static_cast<Steinberg::Vst::ParamID>(
                Gradus::kArpMarkovCell00Id + dragCell_.flatIndex());
            endEditCallback_(paramId);
        }
        dragCell_ = {-1, -1};
        dragEditStarted_ = false;
        return VSTGUI::kMouseEventHandled;
    }

    CLASS_METHODS(MarkovMatrixEditor, CViewContainer)

private:
    // Drawing note: inside drawBackgroundRect, the CDrawContext is already
    // translated so that (0, 0) is the container's top-left. All drawing
    // here uses container-LOCAL coordinates, not frame-relative getViewSize().
    // (See vstgui/lib/cviewcontainer.cpp drawRect: CDrawContext::Transform
    // offsetTransform is installed before drawBackgroundRect is called.)

    void drawCollapsed(VSTGUI::CDrawContext* context)
    {
        const VSTGUI::CCoord w = getWidth();
        const VSTGUI::CCoord h = getHeight();
        const VSTGUI::CRect bounds(0, 0, w, h);

        context->setFillColor({0x22, 0x22, 0x28, 0xF0});
        context->drawRect(bounds, VSTGUI::kDrawFilled);
        context->setFrameColor({0xE8, 0xC8, 0x4C, 0xFF});
        context->setLineWidth(1.5);
        context->drawRect(bounds, VSTGUI::kDrawStroked);

        // Mini 3x3 dot grid centered inside
        const VSTGUI::CCoord cx = w * 0.5;
        const VSTGUI::CCoord cy = h * 0.5;
        context->setFillColor({0xE8, 0xC8, 0x4C, 0xFF});
        for (int r = -1; r <= 1; ++r) {
            for (int c = -1; c <= 1; ++c) {
                VSTGUI::CRect dot(
                    cx + c * 6.0 - 1.5, cy + r * 6.0 - 1.5,
                    cx + c * 6.0 + 1.5, cy + r * 6.0 + 1.5);
                context->drawRect(dot, VSTGUI::kDrawFilled);
            }
        }
    }

    void drawExpanded(VSTGUI::CDrawContext* context)
    {
        const VSTGUI::CCoord w = getWidth();
        const VSTGUI::CCoord h = getHeight();
        const VSTGUI::CRect bounds(0, 0, w, h);

        // Panel background
        context->setFillColor({0x16, 0x16, 0x1C, 0xE0});
        context->drawRect(bounds, VSTGUI::kDrawFilled);

        // Panel border
        context->setFrameColor({0x44, 0x44, 0x4A, 0xFF});
        context->setLineWidth(1.0);
        context->drawRect(bounds, VSTGUI::kDrawStroked);

        // Grid area (local coords, below the dropdown strip)
        const VSTGUI::CCoord gridTop    = kDropdownStrip;
        const VSTGUI::CCoord gridLeft   = 0.0;
        const VSTGUI::CCoord gridRight  = w;
        const VSTGUI::CCoord gridBottom = h;
        const float gridW = static_cast<float>(gridRight - gridLeft);
        const float gridH = static_cast<float>(gridBottom - gridTop);

        const VSTGUI::CColor cellBgLow  {0x22, 0x22, 0x28, 0xFF};
        const VSTGUI::CColor cellBgHi   {0xE8, 0xC8, 0x4C, 0xFF};
        const VSTGUI::CColor cellStroke {0x33, 0x33, 0x3A, 0xFF};

        for (int row = 0; row < kDim; ++row) {
            for (int col = 0; col < kDim; ++col) {
                auto cr = MarkovMatrixEditorLogic::rectForCell(row, col, gridW, gridH);
                VSTGUI::CRect cell(
                    gridLeft + static_cast<VSTGUI::CCoord>(cr.left) + 1.0,
                    gridTop  + static_cast<VSTGUI::CCoord>(cr.top) + 1.0,
                    gridLeft + static_cast<VSTGUI::CCoord>(cr.right) - 1.0,
                    gridTop  + static_cast<VSTGUI::CCoord>(cr.bottom) - 1.0);

                const float v = cellValues_[static_cast<size_t>(row * kDim + col)];
                VSTGUI::CColor blended;
                blended.red   = static_cast<uint8_t>(cellBgLow.red   + (cellBgHi.red   - cellBgLow.red)   * v);
                blended.green = static_cast<uint8_t>(cellBgLow.green + (cellBgHi.green - cellBgLow.green) * v);
                blended.blue  = static_cast<uint8_t>(cellBgLow.blue  + (cellBgHi.blue  - cellBgLow.blue)  * v);
                blended.alpha = 0xFF;

                context->setFillColor(blended);
                context->drawRect(cell, VSTGUI::kDrawFilled);
                context->setFrameColor(cellStroke);
                context->drawRect(cell, VSTGUI::kDrawStroked);
            }
        }

        // Row/column labels (Roman numerals for scale degrees)
        static constexpr const char* kLabels[kDim] = {"I","ii","iii","IV","V","vi","vii"};
        context->setFontColor({0xC0, 0xC0, 0xC8, 0xFF});
        context->setFont(VSTGUI::kNormalFontSmaller);

        auto layout = MarkovMatrixEditorLogic::computeLayout(gridW, gridH);
        for (int i = 0; i < kDim; ++i) {
            VSTGUI::CRect colLabel(
                gridLeft + static_cast<VSTGUI::CCoord>(layout.left + i * layout.cellW),
                gridTop,
                gridLeft + static_cast<VSTGUI::CCoord>(layout.left + (i + 1) * layout.cellW),
                gridTop + static_cast<VSTGUI::CCoord>(layout.top));
            context->drawString(VSTGUI::UTF8String(kLabels[i]), colLabel,
                VSTGUI::kCenterText);
            VSTGUI::CRect rowLabel(
                gridLeft,
                gridTop + static_cast<VSTGUI::CCoord>(layout.top + i * layout.cellH),
                gridLeft + static_cast<VSTGUI::CCoord>(layout.left),
                gridTop + static_cast<VSTGUI::CCoord>(layout.top + (i + 1) * layout.cellH));
            context->drawString(VSTGUI::UTF8String(kLabels[i]), rowLabel,
                VSTGUI::kCenterText);
        }

        // Minimize button ("-") in local coords, top-right corner
        const VSTGUI::CRect btn = minimizeButtonLocalRect();
        context->setFillColor({0x33, 0x33, 0x3A, 0xFF});
        context->drawRect(btn, VSTGUI::kDrawFilled);
        context->setFrameColor({0x88, 0x88, 0x8E, 0xFF});
        context->drawRect(btn, VSTGUI::kDrawStroked);
        context->setFontColor({0xE0, 0xE0, 0xE8, 0xFF});
        context->drawString(VSTGUI::UTF8String("-"), btn, VSTGUI::kCenterText);
    }

    // Local (container 0-based) coordinates — used by draw().
    [[nodiscard]] VSTGUI::CRect minimizeButtonLocalRect() const
    {
        const VSTGUI::CCoord w = getWidth();
        return VSTGUI::CRect(
            w - kMinimizeBtn - 3.0,
            3.0,
            w - 3.0,
            3.0 + kMinimizeBtn);
    }

    // Frame (parent-relative) coordinates — used by onMouseDown which
    // receives `where` in parent coordinates.
    [[nodiscard]] VSTGUI::CRect minimizeButtonFrameRect() const
    {
        const VSTGUI::CRect bounds = getViewSize();
        return VSTGUI::CRect(
            bounds.right - kMinimizeBtn - 3.0,
            bounds.top + 3.0,
            bounds.right - 3.0,
            bounds.top + 3.0 + kMinimizeBtn);
    }

    void updateCellFromDrag(float localYInGrid)
    {
        if (!dragCell_.valid()) return;

        const VSTGUI::CRect bounds = getViewSize();
        const float gridW = static_cast<float>(bounds.getWidth());
        const float gridH = static_cast<float>(bounds.getHeight() - kDropdownStrip);
        const auto cr = MarkovMatrixEditorLogic::rectForCell(
            dragCell_.row, dragCell_.col, gridW, gridH);
        const float newValue = MarkovMatrixEditorLogic::valueFromDragY(
            localYInGrid, cr.top, cr.bottom);

        const size_t idx = static_cast<size_t>(dragCell_.flatIndex());
        if (cellValues_[idx] == newValue) return;
        cellValues_[idx] = newValue;

        const auto paramId = static_cast<Steinberg::Vst::ParamID>(
            Gradus::kArpMarkovCell00Id + dragCell_.flatIndex());
        if (beginEditCallback_ && !dragEditStarted_) {
            beginEditCallback_(paramId);
            dragEditStarted_ = true;
        }
        if (paramCallback_) paramCallback_(paramId, newValue);
        invalid();
    }

    std::array<float, kNumCells> cellValues_{};

    MarkovMatrixEditorLogic::CellIndex dragCell_{-1, -1};
    bool dragEditStarted_ = false;

    bool expanded_ = true;
    VSTGUI::CPoint anchorTopLeft_{0, 0};

    VSTGUI::COptionMenu* presetDropdown_ = nullptr;  // VSTGUI owns after addView

    ParameterCallback paramCallback_;
    EditGateCallback  beginEditCallback_;
    EditGateCallback  endEditCallback_;
};

} // namespace Gradus
