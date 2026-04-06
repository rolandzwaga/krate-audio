// =============================================================================
// MarkovMatrixEditor — Pure Logic (Humble Object Pattern)
// =============================================================================
// Spec 133 (Gradus v1.6).
// VSTGUI-free logic for the 7x7 Markov transition matrix editor. The CControl
// subclass in markov_matrix_editor.h delegates to these functions and only
// adds drawing, mouse handling, and callback dispatch on top.
//
// Mirrors the humble-object pattern used by pin_flag_strip_logic.h and
// tap_pattern_editor_logic.h.
// =============================================================================

#pragma once

#include <krate/dsp/primitives/held_note_buffer.h>  // kMarkovMatrixDim, kMarkovMatrixSize

#include <algorithm>

namespace Gradus::MarkovMatrixEditorLogic {

inline constexpr int kDim = 7;  // Matches Krate::DSP::kMarkovMatrixDim

/// A cell position in the 7x7 grid (row, col) — row is "from degree",
/// col is "to degree".
struct CellIndex {
    int row;  ///< 0..6, or -1 if invalid
    int col;  ///< 0..6, or -1 if invalid

    [[nodiscard]] constexpr bool valid() const noexcept {
        return row >= 0 && row < kDim && col >= 0 && col < kDim;
    }

    [[nodiscard]] constexpr int flatIndex() const noexcept {
        return row * kDim + col;
    }
};

/// Geometry description for the matrix editor. All positions are in the
/// widget's local coordinate space (origin 0,0 top-left).
struct Layout {
    float left;    ///< x-coordinate of the grid (after row-label margin)
    float top;     ///< y-coordinate of the grid (after column-label margin)
    float cellW;   ///< Width of one cell
    float cellH;   ///< Height of one cell
};

/// Compute the grid layout from widget bounds.
///
/// Layout: top ~12px reserved for column labels, left ~12px reserved for row
/// labels. The remaining area is divided into a 7x7 cell grid.
[[nodiscard]] inline Layout computeLayout(float width, float height) noexcept
{
    constexpr float kLabelMargin = 12.0f;
    Layout l{};
    l.left = kLabelMargin;
    l.top = kLabelMargin;
    const float gridW = std::max(0.0f, width - kLabelMargin);
    const float gridH = std::max(0.0f, height - kLabelMargin);
    l.cellW = gridW / static_cast<float>(kDim);
    l.cellH = gridH / static_cast<float>(kDim);
    return l;
}

/// Hit-test a local (x, y) against the 7x7 grid. Returns an invalid
/// CellIndex if the point is outside the grid area.
[[nodiscard]] inline CellIndex cellAtPoint(float x, float y,
                                           float width, float height) noexcept
{
    const Layout l = computeLayout(width, height);
    if (l.cellW <= 0.0f || l.cellH <= 0.0f) return {-1, -1};
    if (x < l.left || y < l.top) return {-1, -1};
    const float relX = x - l.left;
    const float relY = y - l.top;
    const int col = static_cast<int>(relX / l.cellW);
    const int row = static_cast<int>(relY / l.cellH);
    if (col < 0 || col >= kDim || row < 0 || row >= kDim) return {-1, -1};
    return {row, col};
}

/// Compute the rectangle (in local coords) for a specific cell. Returns
/// left, top, right, bottom. Caller is responsible for validity.
struct CellRect {
    float left;
    float top;
    float right;
    float bottom;
};

[[nodiscard]] inline CellRect rectForCell(int row, int col,
                                          float width, float height) noexcept
{
    const Layout l = computeLayout(width, height);
    return CellRect{
        l.left + static_cast<float>(col) * l.cellW,
        l.top  + static_cast<float>(row) * l.cellH,
        l.left + static_cast<float>(col + 1) * l.cellW,
        l.top  + static_cast<float>(row + 1) * l.cellH
    };
}

/// Compute a cell value from a vertical drag gesture.
///
/// Users click-drag on a cell like a mini slider: dragging up increases the
/// value (toward 1.0), dragging down decreases it (toward 0.0). The drag
/// anchor is the cell's top-bottom range.
///
/// @param localY Mouse y in widget-local coords
/// @param cellTop Cell's top y coordinate
/// @param cellBottom Cell's bottom y coordinate
/// @return Clamped [0, 1] value
[[nodiscard]] inline float valueFromDragY(float localY,
                                          float cellTop,
                                          float cellBottom) noexcept
{
    const float span = cellBottom - cellTop;
    if (span <= 0.0f) return 0.0f;
    const float rel = 1.0f - (localY - cellTop) / span;
    if (rel < 0.0f) return 0.0f;
    if (rel > 1.0f) return 1.0f;
    return rel;
}

} // namespace Gradus::MarkovMatrixEditorLogic
