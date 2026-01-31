#pragma once

// ==============================================================================
// CustomCurveEditor - Interactive breakpoint curve editor
// ==============================================================================
// FR-039a: Display custom curve with editable control points
// FR-039b: Mouse interaction for adding, moving, removing breakpoints
// FR-039c: Real-time curve update on parameter changes
//
// Constitution Compliance:
// - Principle V: VSTGUI cross-platform (no native code)
// - Principle VI: Thread safety (parameter access via controller)
//
// Reference: specs/007-sweep-system/spec.md
// ==============================================================================

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"

#include <array>
#include <utility>

namespace Disrumpo {

/// @brief Interactive breakpoint curve editor for Custom morph link mode.
/// @details Renders a graph with draggable control points. Users can:
///          - Left-click empty area: Add breakpoint (up to 8)
///          - Left-click + drag on point: Move breakpoint (endpoints X fixed)
///          - Right-click on point: Remove breakpoint (if count > 2, not endpoints)
class CustomCurveEditor : public VSTGUI::CControl {
public:
    /// @brief Callback for when breakpoints change.
    using ChangeCallback = std::function<void(int pointIndex, float x, float y)>;
    using AddCallback = std::function<void(float x, float y)>;
    using RemoveCallback = std::function<void(int pointIndex)>;

    /// @brief Construct a CustomCurveEditor control.
    /// @param size The size and position rectangle.
    /// @param listener Control listener (typically the controller)
    /// @param tag Control tag for parameter binding
    CustomCurveEditor(const VSTGUI::CRect& size,
                      VSTGUI::IControlListener* listener,
                      int32_t tag);
    ~CustomCurveEditor() override = default;

    // =========================================================================
    // Data API
    // =========================================================================

    /// @brief Set the breakpoint data for display.
    /// @param points Array of (x, y) pairs, sorted by x
    /// @param count Number of active points (2-8)
    void setBreakpoints(const std::array<std::pair<float, float>, 8>& points, int count);

    /// @brief Get the current breakpoint count.
    int getBreakpointCount() const { return breakpointCount_; }

    /// @brief Get a breakpoint position.
    /// @param index Point index (0 to count-1)
    /// @return (x, y) pair in [0, 1]
    std::pair<float, float> getBreakpoint(int index) const;

    /// @brief Set callbacks for breakpoint changes.
    void setOnChange(ChangeCallback cb) { onChange_ = std::move(cb); }
    void setOnAdd(AddCallback cb) { onAdd_ = std::move(cb); }
    void setOnRemove(RemoveCallback cb) { onRemove_ = std::move(cb); }

    /// @brief Enable high contrast mode (Spec 012 FR-025a)
    /// Increases borders, uses solid fills.
    void setHighContrastMode(bool enabled) { highContrastEnabled_ = enabled; }

    // =========================================================================
    // CControl Overrides
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& event) override;

    CLASS_METHODS(CustomCurveEditor, CControl)

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    void drawBackground(VSTGUI::CDrawContext* context);
    void drawGrid(VSTGUI::CDrawContext* context);
    void drawCurve(VSTGUI::CDrawContext* context);
    void drawControlPoints(VSTGUI::CDrawContext* context);

    /// @brief Hit test: find which control point is near pixel position
    /// @return Point index (0 to count-1) or -1 if none
    int hitTestPoint(float pixelX, float pixelY) const;

    /// @brief Convert normalized [0,1] to pixel coordinates within the padded area.
    void normalizedToPixel(float nx, float ny, float& px, float& py) const;

    /// @brief Convert pixel coordinates to normalized [0,1] within the padded area.
    void pixelToNormalized(float px, float py, float& nx, float& ny) const;

    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kPointRadius = 6.0f;
    static constexpr float kHitRadius = 10.0f;
    static constexpr int kPadding = 8;

    // =========================================================================
    // State
    // =========================================================================

    std::array<std::pair<float, float>, 8> breakpoints_{};
    int breakpointCount_ = 2;

    int dragIndex_ = -1;
    bool isDragging_ = false;

    ChangeCallback onChange_;
    AddCallback onAdd_;
    RemoveCallback onRemove_;

    // High contrast mode (Spec 012 FR-025a)
    bool highContrastEnabled_ = false;
};

} // namespace Disrumpo
