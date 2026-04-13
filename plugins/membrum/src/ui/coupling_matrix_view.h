#pragma once

// ==============================================================================
// CouplingMatrixView -- Phase 6 (Spec 141, US6) -- 32x32 coupling-matrix editor
// ==============================================================================
// FR-050..FR-054
//
// The view renders the 32x32 coupling matrix as a colour-intensity heat map.
// Each cell maps (src, dst) -> effectiveGain in [0, kMaxCoefficient=0.05]; a
// brighter cell means a higher gain. Cells whose (src, dst) pair is currently
// active in the live engine are outlined using the MatrixActivityPublisher
// bitmask (32 bits per source pad, one bit per destination).
//
// Interaction (FR-051 / FR-053):
//   - Left click on a cell: cycles the override through four steps
//     (none -> 0.01 -> 0.025 -> 0.05 -> cleared) and calls
//     CouplingMatrix::setOverride()/clearOverride().
//   - Right click on a cell: immediately clears the override via
//     clearOverride().
//   - Shift+left click on a cell: engages Solo on that pair
//     (setSoloPath(src, dst)). A second shift+click anywhere, or closing /
//     removing the view, clears Solo.
//
// Lifecycle (FR-053 edge case):
//   - The destructor and removed() call clearSolo() unconditionally so Solo
//     never outlives the editor.
//   - A 30 Hz CVSTGUITimer snapshots the activity publisher and invalidates
//     the view only when activity bits changed (dirty-rect mitigation per
//     research.md R2 / section 4).
// ==============================================================================

#include "dsp/coupling_matrix.h"

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/crect.h"
#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/events.h"

#include <array>
#include <cstdint>

namespace Membrum {
class MatrixActivityPublisher;
} // namespace Membrum

namespace Membrum::UI {

inline constexpr int kMatrixCells = 32;

class CouplingMatrixView : public VSTGUI::CView
{
public:
    CouplingMatrixView(const VSTGUI::CRect&      size,
                       CouplingMatrix*           matrix,
                       MatrixActivityPublisher*  activityPublisher) noexcept;

    ~CouplingMatrixView() override;

    CouplingMatrixView(const CouplingMatrixView&)            = delete;
    CouplingMatrixView& operator=(const CouplingMatrixView&) = delete;

    // --- testable helpers --------------------------------------------------

    /// Compute the screen rect of cell (src, dst), relative to the view
    /// origin (i.e. in the same coordinate space as getViewSize()).
    [[nodiscard]] VSTGUI::CRect cellRect(int src, int dst) const noexcept;

    /// Map a mouse point (in the same coordinate space as getViewSize()) into
    /// a (src, dst) cell. Returns {-1, -1} if the point is outside the grid.
    struct Cell { int src = -1; int dst = -1; };
    [[nodiscard]] Cell cellFromPoint(VSTGUI::CPoint p) const noexcept;

    /// Dispatches a mouse-down at `localPoint`. Returns the (src, dst) cell
    /// that was hit (or {-1,-1}). Mutates the underlying CouplingMatrix.
    Cell handleMouseDown(VSTGUI::CPoint localPoint,
                         bool           isShift,
                         bool           isRight) noexcept;

    /// Snapshot activity publisher into the internal cache. Normally called
    /// by the 30 Hz timer; tests call it directly.
    void pollActivity() noexcept;

    [[nodiscard]] std::uint32_t activityMaskForSrc(int src) const noexcept
    {
        if (src < 0 || src >= kMatrixCells) return 0u;
        return activityMask_[static_cast<std::size_t>(src)];
    }

    /// Engage/disengage Solo. FR-053: clearSolo() runs in the destructor and
    /// in removed() regardless of user action.
    void setSoloPath(int src, int dst) noexcept;
    void clearSolo() noexcept;

    [[nodiscard]] bool hasSolo() const noexcept
    {
        return matrix_ != nullptr && matrix_->hasSolo();
    }

    // --- VSTGUI hooks ------------------------------------------------------
    void draw(VSTGUI::CDrawContext* ctx) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;
    bool attached(VSTGUI::CView* parent) override;
    bool removed(VSTGUI::CView* parent) override;

    // --- override click step table (exposed for tests) ---------------------
    /// Returns the next override value in the click cycle.
    /// (hasOverride, currentValue) -> (nextHasOverride, nextValue).
    struct OverrideStep { bool hasOverride = false; float value = 0.0f; };
    [[nodiscard]] static OverrideStep nextOverrideStep(bool hasOverride,
                                                       float currentValue) noexcept;

private:
    struct CellGeom
    {
        float cellW = 0.0f;
        float cellH = 0.0f;
    };
    [[nodiscard]] CellGeom cellGeom() const noexcept;

    CouplingMatrix*          matrix_            = nullptr;
    MatrixActivityPublisher* activityPublisher_ = nullptr;

    std::array<std::uint32_t, kMatrixCells> activityMask_{};

    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> pollTimer_;
};

} // namespace Membrum::UI
