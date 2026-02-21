// ==============================================================================
// ArpLane<T, MaxSteps> - Fixed-capacity step lane for arpeggiator polymetric
// patterns (Layer 1 Primitive)
// ==============================================================================
// Spec: 072-independent-lanes
//
// Generic fixed-capacity step lane container. Each lane maintains an
// independent step position that advances and wraps at its own configured
// length, enabling polymetric patterns when multiple lanes have different
// lengths.
//
// Constitution Compliance:
// - Principle II:  Real-Time Safety (noexcept, zero allocation, no locks)
// - Principle III: Modern C++ (C++20, constexpr, std::array)
// - Principle IX:  Layer 1 (depends only on standard library)
// ==============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

/// @brief Fixed-capacity step lane for arpeggiator polymetric patterns.
///
/// Stores up to MaxSteps values of type T. Maintains an internal position
/// that advances independently. Designed for composition within
/// ArpeggiatorCore (Layer 2).
///
/// @tparam T         Step value type (float, int8_t, uint8_t)
/// @tparam MaxSteps  Maximum step count (default 32)
///
/// @par Real-Time Safety
/// All methods are noexcept. Zero heap allocation. No locks, exceptions, or I/O.
///
/// @par Usage
/// @code
/// ArpLane<float> velocityLane;
/// velocityLane.setLength(4);
/// velocityLane.setStep(0, 1.0f);
/// velocityLane.setStep(1, 0.3f);
/// velocityLane.setStep(2, 0.3f);
/// velocityLane.setStep(3, 0.7f);
///
/// float v0 = velocityLane.advance();  // returns 1.0, moves to step 1
/// float v1 = velocityLane.advance();  // returns 0.3, moves to step 2
/// @endcode
template <typename T, size_t MaxSteps = 32>
class ArpLane {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kMaxSteps = MaxSteps;

    // =========================================================================
    // Configuration (FR-002, FR-005, FR-008, FR-009)
    // =========================================================================

    /// @brief Set active step count, clamped to [1, MaxSteps].
    /// If current position >= new length, position wraps to 0.
    void setLength(size_t len) noexcept
    {
        length_ = std::clamp(len, size_t{1}, MaxSteps);
        if (position_ >= length_) {
            position_ = 0;
        }
    }

    /// @brief Get current active step count.
    [[nodiscard]] size_t length() const noexcept { return length_; }

    /// @brief Set value at step index. Index clamped to [0, length-1].
    void setStep(size_t index, T value) noexcept
    {
        const size_t clampedIndex = std::min(index, length_ - 1);
        steps_[clampedIndex] = value;
    }

    /// @brief Get value at step index. Index clamped to [0, length-1].
    /// Out-of-range returns T{} (default value).
    [[nodiscard]] T getStep(size_t index) const noexcept
    {
        if (index >= length_) {
            return T{};
        }
        return steps_[index];
    }

    // =========================================================================
    // Advancement (FR-003, FR-004, FR-006)
    // =========================================================================

    /// @brief Return current step value and advance position by one.
    /// Wraps to step 0 when end of lane is reached.
    T advance() noexcept
    {
        const T value = steps_[position_];
        position_ = (position_ + 1) % length_;
        return value;
    }

    /// @brief Reset position to step 0.
    void reset() noexcept { position_ = 0; }

    /// @brief Get current step position index (for UI playhead).
    [[nodiscard]] size_t currentStep() const noexcept { return position_; }

private:
    std::array<T, MaxSteps> steps_{}; ///< Step values (value-initialized)
    size_t length_{1};                ///< Active step count [1, MaxSteps]
    size_t position_{0};              ///< Current position [0, length-1]
};

} // namespace Krate::DSP
