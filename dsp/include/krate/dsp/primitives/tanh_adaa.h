// ==============================================================================
// Layer 1: Primitives - Tanh with ADAA
// ==============================================================================
// Anti-aliased tanh saturation using Antiderivative Anti-Aliasing (ADAA).
// Provides first-order ADAA for tanh saturation with significantly reduced
// aliasing artifacts compared to naive tanh, without the CPU cost of oversampling.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 1 (depends only on Layer 0)
// - Principle X: DSP Constraints (no internal oversampling/DC blocking)
// - Principle XI: Performance Budget (< 10x naive tanh per sample)
// - Principle XII: Test-First Development
//
// Reference: specs/056-tanh-adaa/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/fast_math.h>
#include <krate/dsp/core/db_utils.h>

#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// TanhADAA Class (FR-001 to FR-025)
// =============================================================================

/// @brief Anti-aliased tanh saturation using first-order Antiderivative Anti-Aliasing.
///
/// ADAA is an analytical technique that reduces aliasing artifacts from nonlinear
/// waveshaping without the CPU cost of oversampling. Instead of computing tanh(x)
/// directly, ADAA computes the antiderivative F(x) at each sample and uses finite
/// differences to achieve band-limiting.
///
/// @par ADAA Theory
/// For tanh(x), the first antiderivative is F1(x) = ln(cosh(x)).
/// First-order ADAA: y[n] = (F1(x[n]*drive) - F1(x[n-1]*drive)) / (drive * (x[n] - x[n-1]))
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 1 (depends only on Layer 0)
/// - Principle X: DSP Constraints (no internal oversampling/DC blocking)
/// - Principle XI: Performance Budget (< 10x naive tanh per sample)
///
/// @par Usage Example
/// @code
/// TanhADAA saturator;
/// saturator.setDrive(4.0f);  // Heavy saturation
///
/// // Sample-by-sample
/// float output = saturator.process(input);
///
/// // Block processing
/// saturator.processBlock(buffer, numSamples);
/// @endcode
///
/// @see specs/056-tanh-adaa/spec.md
/// @see Sigmoid::tanh for naive tanh without anti-aliasing
class TanhADAA {
public:
    // =========================================================================
    // Construction (FR-001)
    // =========================================================================

    /// @brief Default constructor.
    ///
    /// Initializes with:
    /// - Drive: 1.0 (unity gain, standard tanh behavior)
    /// - State: No previous sample history
    TanhADAA() noexcept
        : x1_(0.0f)
        , drive_(1.0f)
        , hasPreviousSample_(false)
    {
    }

    // Default copy/move (trivially copyable - per-channel instances)
    TanhADAA(const TanhADAA&) = default;
    TanhADAA& operator=(const TanhADAA&) = default;
    TanhADAA(TanhADAA&&) noexcept = default;
    TanhADAA& operator=(TanhADAA&&) noexcept = default;
    ~TanhADAA() = default;

    // =========================================================================
    // Configuration (FR-002 to FR-005)
    // =========================================================================

    /// @brief Set the saturation drive level.
    ///
    /// @param drive Saturation intensity (negative values treated as positive)
    ///
    /// @note Drive of 0.0 results in output always being 0.0
    /// @note Does not reset state; takes effect on next process() call
    void setDrive(float drive) noexcept;

    /// @brief Clear all internal state.
    ///
    /// Resets x1_ and hasPreviousSample_ to initial values.
    /// Does not change drive_.
    ///
    /// @post First call to process() after reset() uses naive tanh
    void reset() noexcept;

    // =========================================================================
    // Getters (FR-014)
    // =========================================================================

    /// @brief Get the current drive level (always >= 0).
    [[nodiscard]] float getDrive() const noexcept;

    // =========================================================================
    // Processing (FR-009 to FR-013, FR-018 to FR-020)
    // =========================================================================

    /// @brief Process a single sample with anti-aliased tanh saturation.
    ///
    /// @param x Input sample
    /// @return Anti-aliased tanh-saturated output
    ///
    /// @note First sample after construction or reset() uses naive tanh
    /// @note NaN inputs are propagated
    /// @note Infinity inputs return +/-1.0
    /// @note Real-time safe: O(1) complexity, no allocations
    [[nodiscard]] float process(float x) noexcept;

    /// @brief Process a block of samples in-place.
    ///
    /// Equivalent to calling process() for each sample sequentially.
    /// Produces bit-identical output to N sequential process() calls.
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param n Number of samples in buffer
    ///
    /// @note No memory allocation during this call
    void processBlock(float* buffer, size_t n) noexcept;

    // =========================================================================
    // Static Antiderivative Function (FR-006 to FR-008)
    // =========================================================================

    /// @brief First antiderivative of tanh function.
    ///
    /// F1(x) = ln(cosh(x)) for |x| < 20.0
    /// F1(x) = |x| - ln(2) for |x| >= 20.0 (asymptotic approximation)
    ///
    /// The asymptotic approximation avoids overflow from cosh(x) for large inputs.
    ///
    /// @param x Input value (already scaled by drive if applicable)
    /// @return First antiderivative value
    [[nodiscard]] static float F1(float x) noexcept;

private:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Epsilon for near-identical sample detection (FR-013)
    static constexpr float kEpsilon = 1e-5f;

    /// Threshold for switching to asymptotic F1 approximation (FR-008)
    static constexpr float kOverflowThreshold = 20.0f;

    /// Natural log of 2, used in asymptotic approximation
    static constexpr float kLn2 = 0.693147180559945f;

    // =========================================================================
    // Member Variables
    // =========================================================================

    float x1_;                  ///< Previous input sample
    float drive_;               ///< Saturation intensity (>= 0)
    bool hasPreviousSample_;    ///< True after first sample processed
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline void TanhADAA::setDrive(float drive) noexcept {
    // FR-003: Negative drive treated as absolute value
    drive_ = (drive < 0.0f) ? -drive : drive;
}

inline void TanhADAA::reset() noexcept {
    // FR-005: Clear all internal state but preserve configuration
    x1_ = 0.0f;
    hasPreviousSample_ = false;
}

inline float TanhADAA::getDrive() const noexcept {
    return drive_;
}

inline float TanhADAA::F1(float x) noexcept {
    // FR-006, FR-007, FR-008: First antiderivative of tanh
    // F1(x) = ln(cosh(x))
    //
    // Using the identity: ln(cosh(x)) = |x| - ln(2) + ln(1 + exp(-2|x|))
    // This avoids computing cosh(x) which can overflow for large |x|
    //
    // For |x| >= 20, exp(-2|x|) is negligible (< 10^-17), use pure asymptotic
    // For smaller |x|, use the full identity which is numerically stable

    const float absX = (x >= 0.0f) ? x : -x;

    if (absX >= kOverflowThreshold) {
        // Pure asymptotic approximation to avoid any overflow risk
        return absX - kLn2;
    }

    // Use identity: ln(cosh(x)) = |x| - ln(2) + ln(1 + exp(-2|x|))
    // This is both accurate and avoids cosh overflow
    const float expTerm = std::exp(-2.0f * absX);
    return absX - kLn2 + std::log1p(expTerm);
}

inline float TanhADAA::process(float x) noexcept {
    // FR-004: Drive of 0.0 always returns 0.0
    if (drive_ == 0.0f) {
        return 0.0f;
    }

    // FR-019: NaN propagation
    if (detail::isNaN(x)) {
        return x;
    }

    // FR-020: Handle infinity by returning +/-1.0
    if (detail::isInf(x)) {
        const float result = (x > 0.0f) ? 1.0f : -1.0f;
        // Update state for next sample
        x1_ = x;
        hasPreviousSample_ = true;
        return result;
    }

    // FR-018: First sample after reset uses naive tanh
    if (!hasPreviousSample_) {
        hasPreviousSample_ = true;
        x1_ = x;
        return FastMath::fastTanh(x * drive_);
    }

    // FR-012, FR-013: First-order ADAA with epsilon fallback
    const float dx = x - x1_;
    const float absDx = (dx >= 0.0f) ? dx : -dx;

    float y;
    if (absDx < kEpsilon) {
        // FR-013: Epsilon fallback - use midpoint tanh
        const float midpoint = (x + x1_) * 0.5f;
        y = FastMath::fastTanh(midpoint * drive_);
    } else {
        // FR-012: First-order ADAA formula
        // y = (F1(x*drive) - F1(x1*drive)) / (drive * (x - x1))
        const float xScaled = x * drive_;
        const float x1Scaled = x1_ * drive_;
        y = (F1(xScaled) - F1(x1Scaled)) / (drive_ * dx);
    }

    // Update state for next sample
    x1_ = x;

    return y;
}

inline void TanhADAA::processBlock(float* buffer, size_t n) noexcept {
    // FR-010, FR-011: Block processing is equivalent to N sequential process() calls
    for (size_t i = 0; i < n; ++i) {
        buffer[i] = process(buffer[i]);
    }
}

} // namespace DSP
} // namespace Krate
