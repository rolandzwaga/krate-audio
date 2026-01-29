// ==============================================================================
// CustomCurve - User-Defined Breakpoint Curve
// ==============================================================================
// Allows users to define custom sweep-to-morph mapping curves using
// up to 8 breakpoints with linear interpolation between them.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (evaluate() is noexcept, no allocations)
// - Principle III: Modern C++ (C++20, RAII, value semantics)
// - Principle XII: Test-First Development
//
// Reference: specs/007-sweep-system/spec.md (FR-022)
// Reference: specs/007-sweep-system/data-model.md (CustomCurve entity)
// ==============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace Disrumpo {

/// @brief Minimum number of breakpoints allowed.
constexpr int kMinBreakpoints = 2;

/// @brief Maximum number of breakpoints allowed.
constexpr int kMaxBreakpoints = 8;

/// @brief A single breakpoint in the custom curve.
struct Breakpoint {
    float x = 0.0f;  ///< Normalized input position [0, 1]
    float y = 0.0f;  ///< Output value [0, 1]

    /// @brief Comparison for sorting by x coordinate.
    [[nodiscard]] bool operator<(const Breakpoint& other) const noexcept {
        return x < other.x;
    }
};

/// @brief User-defined breakpoint curve for Custom morph link mode.
///
/// Allows users to define arbitrary mapping curves using up to 8 breakpoints.
/// Linear interpolation is used between breakpoints.
///
/// Constraints:
/// - Minimum 2 breakpoints (endpoints)
/// - Maximum 8 breakpoints
/// - First breakpoint must have x = 0.0
/// - Last breakpoint must have x = 1.0
/// - Breakpoints are automatically sorted by x
///
/// @note Real-time safe: evaluate() has no allocations
class CustomCurve {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor - creates linear curve (0,0) to (1,1).
    CustomCurve() noexcept {
        reset();
    }

    /// @brief Reset to default linear curve (0,0) to (1,1).
    void reset() noexcept {
        breakpoints_[0] = Breakpoint{0.0f, 0.0f};
        breakpoints_[1] = Breakpoint{1.0f, 1.0f};
        count_ = 2;
    }

    // =========================================================================
    // Evaluation (Real-Time Safe)
    // =========================================================================

    /// @brief Evaluate the curve at a given x position.
    ///
    /// Uses linear interpolation between adjacent breakpoints.
    ///
    /// @param x Input position [0, 1]
    /// @return Interpolated output value [0, 1]
    [[nodiscard]] float evaluate(float x) const noexcept {
        // Clamp input to valid range
        if (x <= 0.0f) {
            return breakpoints_[0].y;
        }
        if (x >= 1.0f) {
            return breakpoints_[count_ - 1].y;
        }

        // Find the segment containing x
        for (int i = 0; i < count_ - 1; ++i) {
            const auto& p0 = breakpoints_[i];
            const auto& p1 = breakpoints_[i + 1];

            if (x >= p0.x && x <= p1.x) {
                // Linear interpolation
                float t = (p1.x > p0.x) ? (x - p0.x) / (p1.x - p0.x) : 0.0f;
                return p0.y + t * (p1.y - p0.y);
            }
        }

        // Fallback (shouldn't reach here with valid data)
        return x;
    }

    // =========================================================================
    // Breakpoint Management
    // =========================================================================

    /// @brief Add a new breakpoint to the curve.
    ///
    /// The breakpoint will be inserted in sorted order by x.
    /// Fails if already at maximum (8) breakpoints.
    ///
    /// @param x Input position [0, 1]
    /// @param y Output value [0, 1]
    /// @return true if added successfully, false if at limit
    bool addBreakpoint(float x, float y) noexcept {
        if (count_ >= kMaxBreakpoints) {
            return false;
        }

        // Clamp coordinates
        x = std::clamp(x, 0.0f, 1.0f);
        y = std::clamp(y, 0.0f, 1.0f);

        // Add the new breakpoint
        breakpoints_[count_] = Breakpoint{x, y};
        ++count_;

        // Re-sort
        sortBreakpoints();

        return true;
    }

    /// @brief Remove a breakpoint by index.
    ///
    /// Fails if at minimum (2) breakpoints or invalid index.
    /// Cannot remove endpoint breakpoints (x=0 or x=1).
    ///
    /// @param index Index of breakpoint to remove
    /// @return true if removed successfully, false if at minimum or invalid
    bool removeBreakpoint(int index) noexcept {
        if (count_ <= kMinBreakpoints) {
            return false;
        }

        if (index < 0 || index >= count_) {
            return false;
        }

        // Don't allow removing endpoints
        if (breakpoints_[index].x <= 0.001f || breakpoints_[index].x >= 0.999f) {
            return false;
        }

        // Shift remaining breakpoints down
        for (int i = index; i < count_ - 1; ++i) {
            breakpoints_[i] = breakpoints_[i + 1];
        }
        --count_;

        return true;
    }

    /// @brief Modify an existing breakpoint.
    ///
    /// The breakpoints will be re-sorted after modification.
    /// Endpoint x values (0 and 1) are protected and won't be modified.
    ///
    /// @param index Index of breakpoint to modify
    /// @param x New x position [0, 1]
    /// @param y New y value [0, 1]
    void setBreakpoint(int index, float x, float y) noexcept {
        if (index < 0 || index >= count_) {
            return;
        }

        // Clamp coordinates
        x = std::clamp(x, 0.0f, 1.0f);
        y = std::clamp(y, 0.0f, 1.0f);

        // Check if this is an endpoint
        bool isFirstEndpoint = (breakpoints_[index].x <= 0.001f);
        bool isLastEndpoint = (breakpoints_[index].x >= 0.999f);

        // Protect endpoint x values
        if (isFirstEndpoint) {
            x = 0.0f;
        } else if (isLastEndpoint) {
            x = 1.0f;
        }

        breakpoints_[index].x = x;
        breakpoints_[index].y = y;

        // Re-sort to maintain order
        sortBreakpoints();
    }

    /// @brief Get the number of breakpoints.
    [[nodiscard]] int getBreakpointCount() const noexcept {
        return count_;
    }

    /// @brief Get a breakpoint by index.
    ///
    /// @param index Index of breakpoint (0 to count-1)
    /// @return Breakpoint at index, or default if invalid
    [[nodiscard]] Breakpoint getBreakpoint(int index) const noexcept {
        if (index < 0 || index >= count_) {
            return Breakpoint{};
        }
        return breakpoints_[index];
    }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Sort breakpoints by x coordinate.
    void sortBreakpoints() noexcept {
        std::sort(breakpoints_.begin(), breakpoints_.begin() + count_);

        // Ensure endpoints are correct
        breakpoints_[0].x = 0.0f;
        breakpoints_[count_ - 1].x = 1.0f;
    }

    // =========================================================================
    // State
    // =========================================================================

    std::array<Breakpoint, kMaxBreakpoints> breakpoints_{};
    int count_ = 0;
};

} // namespace Disrumpo
