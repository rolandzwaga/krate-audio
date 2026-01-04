// =============================================================================
// TapPatternEditor Pure Logic Functions
// =============================================================================
// Extracted for testability (humble object pattern) - Constitution Principle XIII
// These pure functions can be tested without VSTGUI dependencies.
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Iterum {

/// Maximum number of taps supported by the pattern editor
static constexpr size_t kMaxPatternTaps = 16;

/// Minimum editor width in pixels (FR-007: handle narrow widths)
static constexpr float kMinEditorWidth = 200.0f;

/// Tap handle width for hit testing
static constexpr float kTapHandleWidth = 20.0f;

/// Tap bar width for visual representation
static constexpr float kTapBarWidth = 16.0f;

// =============================================================================
// Coordinate Conversion Functions (T016, T018)
// =============================================================================

/// Convert X pixel position to time ratio (0.0 - 1.0)
/// @param xPosition X coordinate in view-local space
/// @param viewWidth Width of the editor view
/// @return Time ratio clamped to [0.0, 1.0]
inline float positionToTimeRatio(float xPosition, float viewWidth) noexcept {
    if (viewWidth <= 0.0f) return 0.0f;
    float ratio = xPosition / viewWidth;
    return std::clamp(ratio, 0.0f, 1.0f);
}

/// Convert time ratio (0.0 - 1.0) to X pixel position
/// @param timeRatio Time ratio in [0.0, 1.0]
/// @param viewWidth Width of the editor view
/// @return X pixel position
inline float timeRatioToPosition(float timeRatio, float viewWidth) noexcept {
    return std::clamp(timeRatio, 0.0f, 1.0f) * viewWidth;
}

/// Convert Y pixel position to level ratio (0.0 - 1.0)
/// @param yPosition Y coordinate in view-local space (0 = top)
/// @param viewHeight Height of the editor view
/// @return Level ratio clamped to [0.0, 1.0]
/// @note Y is inverted: top of view = level 1.0, bottom = level 0.0
inline float levelFromYPosition(float yPosition, float viewHeight) noexcept {
    if (viewHeight <= 0.0f) return 0.0f;
    // Invert: top of view is level 1.0
    float ratio = 1.0f - (yPosition / viewHeight);
    return std::clamp(ratio, 0.0f, 1.0f);
}

/// Convert level ratio (0.0 - 1.0) to Y pixel position
/// @param levelRatio Level ratio in [0.0, 1.0]
/// @param viewHeight Height of the editor view
/// @return Y pixel position (inverted: top = 1.0)
inline float levelToYPosition(float levelRatio, float viewHeight) noexcept {
    // Invert: level 1.0 = top of view
    return (1.0f - std::clamp(levelRatio, 0.0f, 1.0f)) * viewHeight;
}

// =============================================================================
// Hit Testing Functions (T017)
// =============================================================================

/// Check if a point is within a tap's hit area
/// @param pointX X coordinate to test
/// @param tapCenterX Center X position of the tap
/// @param tapTop Top Y position of the tap bar
/// @param tapBottom Bottom Y position of the tap bar
/// @param handleWidth Width of the hit area around the tap
/// @param pointY Y coordinate to test
/// @return true if point is within tap hit area
inline bool isPointInTapHitArea(float pointX, float pointY,
                                 float tapCenterX, float tapTop, float tapBottom,
                                 float handleWidth = kTapHandleWidth) noexcept {
    float halfWidth = handleWidth / 2.0f;
    return (pointX >= tapCenterX - halfWidth) &&
           (pointX <= tapCenterX + halfWidth) &&
           (pointY >= tapTop) &&
           (pointY <= tapBottom);
}

/// Find which tap (if any) is at the given position
/// @param pointX X coordinate in view-local space
/// @param pointY Y coordinate in view-local space
/// @param tapTimeRatios Array of tap time ratios
/// @param tapLevels Array of tap levels
/// @param activeTapCount Number of active taps
/// @param viewWidth Editor view width
/// @param viewHeight Editor view height
/// @return Tap index (0-based) or -1 if no tap at position
inline int hitTestTap(float pointX, float pointY,
                      const float* tapTimeRatios,
                      const float* tapLevels,
                      size_t activeTapCount,
                      float viewWidth, float viewHeight) noexcept {
    if (!tapTimeRatios || !tapLevels || activeTapCount == 0) return -1;
    if (viewWidth <= 0.0f || viewHeight <= 0.0f) return -1;

    // Check taps in reverse order (front-to-back for overlapping taps)
    for (int i = static_cast<int>(activeTapCount) - 1; i >= 0; --i) {
        float tapCenterX = timeRatioToPosition(tapTimeRatios[i], viewWidth);
        float tapTop = levelToYPosition(tapLevels[i], viewHeight);
        float tapBottom = viewHeight;  // Bars extend to bottom

        if (isPointInTapHitArea(pointX, pointY, tapCenterX, tapTop, tapBottom)) {
            return i;
        }
    }
    return -1;
}

// =============================================================================
// Value Clamping Functions (T018.1)
// =============================================================================

/// Clamp a value to valid range [0.0, 1.0]
inline float clampRatio(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
}

// =============================================================================
// Axis Constraint Functions (T018.2)
// =============================================================================

/// Axis constraint mode for Shift+drag behavior
enum class ConstraintAxis {
    None,       ///< No constraint
    Horizontal, ///< Constrain to horizontal (time only)
    Vertical    ///< Constrain to vertical (level only)
};

/// Determine which axis to constrain based on movement delta
/// @param deltaX Horizontal movement from drag start
/// @param deltaY Vertical movement from drag start
/// @param threshold Minimum delta to trigger constraint
/// @return The axis to constrain movement to
inline ConstraintAxis determineConstraintAxis(float deltaX, float deltaY,
                                               float threshold = 5.0f) noexcept {
    float absDeltaX = std::abs(deltaX);
    float absDeltaY = std::abs(deltaY);

    if (absDeltaX < threshold && absDeltaY < threshold) {
        return ConstraintAxis::None;  // Not enough movement
    }

    return (absDeltaX > absDeltaY) ? ConstraintAxis::Horizontal : ConstraintAxis::Vertical;
}

/// Apply axis constraint to time/level values
/// @param currentTime Current time ratio
/// @param currentLevel Current level ratio
/// @param preDragTime Pre-drag time ratio (for constraint reference)
/// @param preDragLevel Pre-drag level ratio (for constraint reference)
/// @param axis The axis to constrain to
/// @return Pair of (constrainedTime, constrainedLevel)
inline std::pair<float, float> applyAxisConstraint(
    float currentTime, float currentLevel,
    float preDragTime, float preDragLevel,
    ConstraintAxis axis) noexcept {
    switch (axis) {
        case ConstraintAxis::Horizontal:
            return {currentTime, preDragLevel};  // Keep level fixed
        case ConstraintAxis::Vertical:
            return {preDragTime, currentLevel};  // Keep time fixed
        case ConstraintAxis::None:
        default:
            return {currentTime, currentLevel};  // No constraint
    }
}

// =============================================================================
// Double-Click Reset Functions (T018.3)
// =============================================================================

/// Calculate default time position for a tap (evenly spaced)
/// @param tapIndex 0-based tap index
/// @param totalTaps Total number of active taps
/// @return Default time ratio for evenly spaced pattern
inline float calculateDefaultTapTime(size_t tapIndex, size_t totalTaps) noexcept {
    if (totalTaps == 0) return 0.0f;
    // Evenly spaced: tap N at position (N+1)/(totalTaps+1)
    return static_cast<float>(tapIndex + 1) / static_cast<float>(totalTaps + 1);
}

/// Default level for a tap (100% / full level)
inline constexpr float kDefaultTapLevel = 1.0f;

// =============================================================================
// Grid Snapping Functions (Phase 5 - User Story 3)
// =============================================================================

/// Snap division options for grid snapping
enum class SnapDivision {
    Off,           ///< No snapping
    Quarter,       ///< 1/4 note (4 divisions)
    Eighth,        ///< 1/8 note (8 divisions)
    Sixteenth,     ///< 1/16 note (16 divisions)
    ThirtySecond,  ///< 1/32 note (32 divisions)
    Triplet        ///< Triplet grid (12 divisions for quarter-note triplets)
};

/// Get the number of grid divisions for a snap setting
inline int getSnapDivisions(SnapDivision division) noexcept {
    switch (division) {
        case SnapDivision::Quarter:      return 4;
        case SnapDivision::Eighth:       return 8;
        case SnapDivision::Sixteenth:    return 16;
        case SnapDivision::ThirtySecond: return 32;
        case SnapDivision::Triplet:      return 12;
        case SnapDivision::Off:
        default:                         return 0;
    }
}

/// Snap a time ratio to the nearest grid position
/// @param timeRatio The input time ratio (0.0 - 1.0)
/// @param division The snap division setting
/// @return The snapped time ratio, or original if snap is off
inline float snapToGrid(float timeRatio, SnapDivision division) noexcept {
    int divisions = getSnapDivisions(division);
    if (divisions == 0) {
        return timeRatio;  // No snapping
    }

    // Snap to nearest grid line: round(ratio * divisions) / divisions
    float scaled = timeRatio * static_cast<float>(divisions);
    float snapped = std::round(scaled) / static_cast<float>(divisions);
    return std::clamp(snapped, 0.0f, 1.0f);
}

// =============================================================================
// Mouse Button Handling Functions (T018.5)
// =============================================================================

/// Check if right button should be ignored
inline bool shouldIgnoreRightClick(bool isRightButton) noexcept {
    return isRightButton;  // Right-click is ignored in v1
}

// =============================================================================
// Editor Size Validation (T031.9)
// =============================================================================

/// Get effective editor width (enforces minimum)
inline float getEffectiveEditorWidth(float actualWidth) noexcept {
    return std::max(actualWidth, kMinEditorWidth);
}

} // namespace Iterum
