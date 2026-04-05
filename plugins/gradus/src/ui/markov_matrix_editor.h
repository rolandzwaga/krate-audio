// =============================================================================
// MarkovMatrixEditor — 7x7 Matrix Editor (Markov Chain Mode)
// =============================================================================
// Spec 133 (Gradus v1.7): custom CControl that renders a 7x7 grid of cells
// (scale-degree transition probabilities) with click-drag editing.
//
// Humble object: all testable logic lives in markov_matrix_editor_logic.h.
// This file only wires drawing, mouse handling, and callback dispatch.
// =============================================================================

#pragma once

#include "plugin_ids.h"
#include "markov_matrix_editor_logic.h"

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"

#include "pluginterfaces/vst/vsttypes.h"

#include <array>
#include <cstdio>
#include <functional>

namespace Gradus {

class MarkovMatrixEditor : public VSTGUI::CControl {
public:
    static constexpr int kDim = MarkovMatrixEditorLogic::kDim;
    static constexpr int kNumCells = kDim * kDim;

    // Expanded view is the full 7x7 editor; collapsed view is a small
    // trigger button the user can click to re-expand.
    static constexpr VSTGUI::CCoord kExpandedSize  = 160.0;
    static constexpr VSTGUI::CCoord kCollapsedSize = 32.0;
    // Size of the "×" minimize button rendered in the top-right corner
    // of the expanded view.
    static constexpr VSTGUI::CCoord kMinimizeBtn   = 14.0;

    explicit MarkovMatrixEditor(const VSTGUI::CRect& anchor)
        : CControl(VSTGUI::CRect(anchor.left, anchor.top,
                                 anchor.left + kExpandedSize,
                                 anchor.top + kExpandedSize),
                   nullptr, -1, nullptr)
        , anchorTopLeft_(anchor.left, anchor.top)
    {
        // Initialize cells to Uniform (1/7) so the widget is usable before
        // any host-side sync arrives.
        constexpr float kUniform = 1.0f / 7.0f;
        for (auto& c : cellValues_) c = kUniform;
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

        const VSTGUI::CCoord size = expand ? kExpandedSize : kCollapsedSize;
        VSTGUI::CRect newFrame(anchorTopLeft_.x, anchorTopLeft_.y,
                               anchorTopLeft_.x + size,
                               anchorTopLeft_.y + size);
        // Invalidate the larger of old and new rect so any parent redraws both.
        if (auto* parent = getParentView()) {
            VSTGUI::CRect dirty = getViewSize();
            dirty.unite(newFrame);
            parent->invalidRect(dirty);
        }
        setViewSize(newFrame);
        setMouseableArea(newFrame);
        invalid();
    }

    // -------------------------------------------------------------------------
    // State accessors
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

    /// Flat-index accessor (for batch sync).
    void setCellValueFlat(int flatIdx, float value)
    {
        if (flatIdx < 0 || flatIdx >= kNumCells) return;
        setCellValue(flatIdx / kDim, flatIdx % kDim, value);
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
    // CControl overrides
    // -------------------------------------------------------------------------

    void draw(VSTGUI::CDrawContext* context) override
    {
        if (expanded_) {
            drawExpanded(context);
        } else {
            drawCollapsed(context);
        }
        setDirty(false);
    }

    void drawCollapsed(VSTGUI::CDrawContext* context)
    {
        const VSTGUI::CRect bounds = getViewSize();

        // Filled amber rounded-rect button with a tiny 3x3 dot-grid icon
        // suggesting "matrix".
        context->setFillColor({0x22, 0x22, 0x28, 0xF0});
        context->drawRect(bounds, VSTGUI::kDrawFilled);
        context->setFrameColor({0xE8, 0xC8, 0x4C, 0xFF});
        context->setLineWidth(1.5);
        context->drawRect(bounds, VSTGUI::kDrawStroked);

        // Mini 3x3 dot grid centered inside
        const VSTGUI::CCoord cx = (bounds.left + bounds.right) * 0.5;
        const VSTGUI::CCoord cy = (bounds.top + bounds.bottom) * 0.5;
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
        const VSTGUI::CRect bounds = getViewSize();
        const float w = static_cast<float>(bounds.getWidth());
        const float h = static_cast<float>(bounds.getHeight());
        if (w <= 0.0f || h <= 0.0f) return;

        const float bLeft = static_cast<float>(bounds.left);
        const float bTop  = static_cast<float>(bounds.top);

        // Background
        context->setFillColor({0x16, 0x16, 0x1C, 0xE0});
        context->drawRect(bounds, VSTGUI::kDrawFilled);

        // Border
        context->setFrameColor({0x44, 0x44, 0x4A, 0xFF});
        context->setLineWidth(1.0);
        context->drawRect(bounds, VSTGUI::kDrawStroked);

        // Draw the 7x7 grid of cells. Brightness encodes the cell value.
        const VSTGUI::CColor cellBgLow  {0x22, 0x22, 0x28, 0xFF};
        const VSTGUI::CColor cellBgHi   {0xE8, 0xC8, 0x4C, 0xFF};  // amber/gold
        const VSTGUI::CColor cellStroke {0x33, 0x33, 0x3A, 0xFF};

        for (int row = 0; row < kDim; ++row) {
            for (int col = 0; col < kDim; ++col) {
                auto cr = MarkovMatrixEditorLogic::rectForCell(row, col, w, h);
                VSTGUI::CRect cell(
                    bLeft + static_cast<VSTGUI::CCoord>(cr.left) + 1.0,
                    bTop  + static_cast<VSTGUI::CCoord>(cr.top) + 1.0,
                    bLeft + static_cast<VSTGUI::CCoord>(cr.right) - 1.0,
                    bTop  + static_cast<VSTGUI::CCoord>(cr.bottom) - 1.0);

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

        auto layout = MarkovMatrixEditorLogic::computeLayout(w, h);
        for (int i = 0; i < kDim; ++i) {
            // Column labels (top)
            VSTGUI::CRect colLabel(
                bLeft + static_cast<VSTGUI::CCoord>(layout.left + i * layout.cellW),
                bTop,
                bLeft + static_cast<VSTGUI::CCoord>(layout.left + (i + 1) * layout.cellW),
                bTop + static_cast<VSTGUI::CCoord>(layout.top));
            context->drawString(VSTGUI::UTF8String(kLabels[i]), colLabel,
                VSTGUI::kCenterText);
            // Row labels (left)
            VSTGUI::CRect rowLabel(
                bLeft,
                bTop + static_cast<VSTGUI::CCoord>(layout.top + i * layout.cellH),
                bLeft + static_cast<VSTGUI::CCoord>(layout.left),
                bTop + static_cast<VSTGUI::CCoord>(layout.top + (i + 1) * layout.cellH));
            context->drawString(VSTGUI::UTF8String(kLabels[i]), rowLabel,
                VSTGUI::kCenterText);
        }

        // Minimize button ("×") in top-right corner
        const VSTGUI::CRect btn = minimizeButtonRect();
        context->setFillColor({0x33, 0x33, 0x3A, 0xFF});
        context->drawRect(btn, VSTGUI::kDrawFilled);
        context->setFrameColor({0x88, 0x88, 0x8E, 0xFF});
        context->drawRect(btn, VSTGUI::kDrawStroked);
        context->setFontColor({0xE0, 0xE0, 0xE8, 0xFF});
        context->drawString(VSTGUI::UTF8String("-"), btn, VSTGUI::kCenterText);
    }

    [[nodiscard]] VSTGUI::CRect minimizeButtonRect() const
    {
        const VSTGUI::CRect bounds = getViewSize();
        return VSTGUI::CRect(
            bounds.right - kMinimizeBtn - 2.0,
            bounds.top + 2.0,
            bounds.right - 2.0,
            bounds.top + 2.0 + kMinimizeBtn);
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        if (!buttons.isLeftButton()) return VSTGUI::kMouseEventNotHandled;

        // Collapsed: any click expands the editor.
        if (!expanded_) {
            setExpanded(true);
            return VSTGUI::kMouseEventHandled;
        }

        // Expanded: check minimize button first.
        if (minimizeButtonRect().pointInside(where)) {
            setExpanded(false);
            return VSTGUI::kMouseEventHandled;
        }

        // Cell edit
        const VSTGUI::CRect bounds = getViewSize();
        const float localX = static_cast<float>(where.x - bounds.left);
        const float localY = static_cast<float>(where.y - bounds.top);
        const auto hit = MarkovMatrixEditorLogic::cellAtPoint(
            localX, localY,
            static_cast<float>(bounds.getWidth()),
            static_cast<float>(bounds.getHeight()));
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
        if (!expanded_) return VSTGUI::kMouseEventNotHandled;
        if (!buttons.isLeftButton() || !dragCell_.valid())
            return VSTGUI::kMouseEventNotHandled;

        const VSTGUI::CRect bounds = getViewSize();
        const float localY = static_cast<float>(where.y - bounds.top);
        updateCellFromDrag(localY);
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseUp(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& /*buttons*/) override
    {
        (void)where;
        if (dragCell_.valid() && dragEditStarted_ && endEditCallback_) {
            const auto paramId = static_cast<Steinberg::Vst::ParamID>(
                Gradus::kArpMarkovCell00Id + dragCell_.flatIndex());
            endEditCallback_(paramId);
        }
        dragCell_ = {-1, -1};
        dragEditStarted_ = false;
        return VSTGUI::kMouseEventHandled;
    }

    CLASS_METHODS(MarkovMatrixEditor, CControl)

private:
    void updateCellFromDrag(float localY)
    {
        if (!dragCell_.valid()) return;

        const VSTGUI::CRect bounds = getViewSize();
        const float w = static_cast<float>(bounds.getWidth());
        const float h = static_cast<float>(bounds.getHeight());
        const auto cr = MarkovMatrixEditorLogic::rectForCell(
            dragCell_.row, dragCell_.col, w, h);
        const float newValue = MarkovMatrixEditorLogic::valueFromDragY(
            localY, cr.top, cr.bottom);

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

    // Expand/collapse state. `anchorTopLeft_` is the top-left corner used
    // by both states — the view resizes from this anchor, not center.
    bool expanded_ = true;
    VSTGUI::CPoint anchorTopLeft_{0, 0};

    ParameterCallback paramCallback_;
    EditGateCallback  beginEditCallback_;
    EditGateCallback  endEditCallback_;
};

} // namespace Gradus
