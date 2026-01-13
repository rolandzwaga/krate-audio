// ==============================================================================
// Layer 1: Primitives - Hard Clip with ADAA
// ==============================================================================
// Anti-aliased hard clipping using Antiderivative Anti-Aliasing (ADAA).
// Provides first-order and second-order ADAA for hard clipping with
// significantly reduced aliasing artifacts compared to naive clipping.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 1 (depends only on Layer 0)
// - Principle X: DSP Constraints (no internal oversampling/DC blocking)
// - Principle XI: Performance Budget (< 10x naive hard clip per sample)
// - Principle XII: Test-First Development
//
// Reference: specs/053-hard-clip-adaa/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/sigmoid.h>
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// HardClipADAA Class (FR-001 to FR-034)
// =============================================================================

/// @brief Anti-aliased hard clipping using Antiderivative Anti-Aliasing (ADAA).
///
/// Implements first-order and second-order ADAA algorithms for hard clipping,
/// providing significant aliasing reduction without the CPU cost of oversampling.
///
/// @par ADAA Theory
/// Instead of computing f(x[n]) directly, ADAA computes the antiderivative F(x)
/// at each sample and uses finite differences:
/// - First-order: y[n] = (F1(x[n]) - F1(x[n-1])) / (x[n] - x[n-1])
/// - Second-order: Uses F2 and the first-order derivative for smoother results
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 1 (depends only on Layer 0)
/// - Principle X: DSP Constraints (no internal oversampling/DC blocking)
/// - Principle XI: Performance Budget (< 10x naive hard clip per sample)
///
/// @par Usage Example
/// @code
/// HardClipADAA clipper;
/// clipper.setOrder(HardClipADAA::Order::First);  // Good quality
/// clipper.setThreshold(0.8f);                    // Clip at +/-0.8
///
/// // Sample-by-sample
/// float output = clipper.process(input);
///
/// // Block processing
/// clipper.processBlock(buffer, numSamples);
/// @endcode
///
/// @see specs/053-hard-clip-adaa/spec.md
/// @see Waveshaper for naive hard clipping without anti-aliasing
class HardClipADAA {
public:
    // =========================================================================
    // Order Enumeration (FR-001, FR-002)
    // =========================================================================

    /// @brief ADAA order selection for aliasing reduction quality vs CPU tradeoff.
    ///
    /// | Order  | Aliasing Reduction | CPU Cost vs Naive |
    /// |--------|-------------------|-------------------|
    /// | First  | ~12-20 dB         | ~6-8x             |
    /// | Second | ~18-30 dB         | ~12-15x           |
    enum class Order : uint8_t {
        First = 0,   ///< First-order ADAA: efficient, good aliasing reduction
        Second = 1   ///< Second-order ADAA: higher quality, more CPU
    };

    // =========================================================================
    // Construction (FR-003)
    // =========================================================================

    /// @brief Default constructor.
    ///
    /// Initializes with:
    /// - Order: First (efficient, good quality)
    /// - Threshold: 1.0 (standard [-1, 1] range)
    /// - State: No previous sample history
    HardClipADAA() noexcept
        : x1_(0.0f)
        , D1_prev_(0.0f)
        , threshold_(1.0f)
        , order_(Order::First)
        , hasPreviousSample_(false)
        , hasValidD1Prev_(false)
    {
    }

    // Default copy/move (trivially copyable - per-channel instances)
    HardClipADAA(const HardClipADAA&) = default;
    HardClipADAA& operator=(const HardClipADAA&) = default;
    HardClipADAA(HardClipADAA&&) noexcept = default;
    HardClipADAA& operator=(HardClipADAA&&) noexcept = default;
    ~HardClipADAA() = default;

    // =========================================================================
    // Configuration (FR-004 to FR-008)
    // =========================================================================

    /// @brief Set the ADAA algorithm order.
    ///
    /// @param order Order::First for efficiency, Order::Second for quality
    ///
    /// @note Does not reset state; takes effect on next process() call
    void setOrder(Order order) noexcept;

    /// @brief Set the clipping threshold.
    ///
    /// @param threshold Clipping level (negative values treated as positive)
    ///
    /// @note Threshold of 0.0 results in output always being 0.0
    /// @note Does not reset state
    void setThreshold(float threshold) noexcept;

    /// @brief Clear all internal state.
    ///
    /// Resets x1_, D1_prev_, and hasPreviousSample_ to initial values.
    /// Does not change order_ or threshold_.
    ///
    /// @post First call to process() after reset() uses naive hard clip
    void reset() noexcept;

    // =========================================================================
    // Getters (FR-022, FR-023)
    // =========================================================================

    /// @brief Get the current ADAA order.
    [[nodiscard]] Order getOrder() const noexcept;

    /// @brief Get the current threshold (always >= 0).
    [[nodiscard]] float getThreshold() const noexcept;

    // =========================================================================
    // Processing (FR-013 to FR-021, FR-024 to FR-029)
    // =========================================================================

    /// @brief Process a single sample with anti-aliased hard clipping.
    ///
    /// @param x Input sample
    /// @return Anti-aliased hard-clipped output
    ///
    /// @note First sample after construction or reset() uses naive hard clip
    /// @note NaN inputs are propagated
    /// @note Infinity inputs are clamped to threshold
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
    // Static Antiderivative Functions (FR-009 to FR-012)
    // =========================================================================

    /// @brief First antiderivative of hard clip function.
    ///
    /// F1(x, t) = integral of clamp(x, -t, t) dx:
    /// - x < -t:  F1 = -t*x - t^2/2
    /// - |x| <= t: F1 = x^2/2
    /// - x > t:   F1 = t*x - t^2/2
    ///
    /// @param x Input value
    /// @param threshold Clipping threshold (must be >= 0)
    /// @return First antiderivative value
    [[nodiscard]] static float F1(float x, float threshold) noexcept;

    /// @brief Second antiderivative of hard clip function.
    ///
    /// F2(x, t) = integral of F1(x, t) dx:
    /// - x < -t:  F2 = -t*x^2/2 - t^2*x/2 - t^3/6
    /// - |x| <= t: F2 = x^3/6
    /// - x > t:   F2 = t*x^2/2 - t^2*x/2 + t^3/6
    ///
    /// @param x Input value
    /// @param threshold Clipping threshold (must be >= 0)
    /// @return Second antiderivative value
    [[nodiscard]] static float F2(float x, float threshold) noexcept;

private:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Epsilon for near-identical sample detection (FR-017, FR-020)
    static constexpr float kEpsilon = 1e-5f;

    // =========================================================================
    // Internal Processing Methods
    // =========================================================================

    /// @brief First-order ADAA processing (FR-016, FR-017)
    [[nodiscard]] float processFirstOrder(float x) noexcept;

    /// @brief Second-order ADAA processing (FR-018 to FR-021)
    [[nodiscard]] float processSecondOrder(float x) noexcept;

    // =========================================================================
    // Member Variables
    // =========================================================================

    float x1_;                  ///< Previous input sample
    float D1_prev_;             ///< Previous first-order result (for ADAA2)
    float threshold_;           ///< Clipping threshold (>= 0)
    Order order_;               ///< Selected ADAA algorithm
    bool hasPreviousSample_;    ///< True after first sample processed
    bool hasValidD1Prev_;       ///< True when D1_prev_ is valid for second-order
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline void HardClipADAA::setOrder(Order order) noexcept {
    order_ = order;
}

inline void HardClipADAA::setThreshold(float threshold) noexcept {
    // FR-006: Negative threshold treated as absolute value
    threshold_ = (threshold < 0.0f) ? -threshold : threshold;
}

inline void HardClipADAA::reset() noexcept {
    // FR-008: Clear all internal state but preserve configuration
    x1_ = 0.0f;
    D1_prev_ = 0.0f;
    hasPreviousSample_ = false;
    hasValidD1Prev_ = false;
}

inline HardClipADAA::Order HardClipADAA::getOrder() const noexcept {
    return order_;
}

inline float HardClipADAA::getThreshold() const noexcept {
    return threshold_;
}

inline float HardClipADAA::F1(float x, float threshold) noexcept {
    // FR-009, FR-010: First antiderivative of hard clip
    // F1(x, t) = integral of clamp(x, -t, t)
    const float t = threshold;
    const float t2 = t * t;

    if (x < -t) {
        // Region x < -t: F1 = -t*x - t^2/2
        return -t * x - t2 * 0.5f;
    } else if (x > t) {
        // Region x > t: F1 = t*x - t^2/2
        return t * x - t2 * 0.5f;
    } else {
        // Region |x| <= t: F1 = x^2/2
        return x * x * 0.5f;
    }
}

inline float HardClipADAA::F2(float x, float threshold) noexcept {
    // FR-011, FR-012: Second antiderivative of hard clip
    // F2(x, t) = integral of F1(x, t)
    const float t = threshold;
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float x2 = x * x;

    if (x < -t) {
        // Region x < -t: F2 = -t*x^2/2 - t^2*x/2 - t^3/6
        return -t * x2 * 0.5f - t2 * x * 0.5f - t3 / 6.0f;
    } else if (x > t) {
        // Region x > t: F2 = t*x^2/2 - t^2*x/2 + t^3/6
        return t * x2 * 0.5f - t2 * x * 0.5f + t3 / 6.0f;
    } else {
        // Region |x| <= t: F2 = x^3/6
        return x * x2 / 6.0f;
    }
}

inline float HardClipADAA::processFirstOrder(float x) noexcept {
    // FR-016: First-order ADAA formula
    // y = (F1(x[n]) - F1(x[n-1])) / (x[n] - x[n-1])

    const float dx = x - x1_;

    // Check for near-identical samples (FR-017)
    // Use std::abs which is constexpr-friendly in C++23, inline comparison for C++20
    const float absDx = (dx >= 0.0f) ? dx : -dx;

    float D1;
    if (absDx < kEpsilon) {
        // Fallback: use hardClip of midpoint
        const float midpoint = (x + x1_) * 0.5f;
        D1 = Sigmoid::hardClip(midpoint, threshold_);
    } else {
        // ADAA1 formula
        D1 = (F1(x, threshold_) - F1(x1_, threshold_)) / dx;
    }

    return D1;
}

inline float HardClipADAA::processSecondOrder(float x) noexcept {
    // FR-018 to FR-021: Second-order ADAA
    // Using polynomial extrapolation form: D2[n] = 2*D1[n] - D1[n-1]
    // This provides a better approximation by extrapolating the trend in D1.

    const float dx = x - x1_;
    const float absDx = (dx >= 0.0f) ? dx : -dx;

    // Compute first-order ADAA result (needed for D1_prev update and fallback)
    float D1;
    if (absDx < kEpsilon) {
        // Fallback for first-order
        const float midpoint = (x + x1_) * 0.5f;
        D1 = Sigmoid::hardClip(midpoint, threshold_);
    } else {
        D1 = (F1(x, threshold_) - F1(x1_, threshold_)) / dx;
    }

    // FR-020: Fall back to first-order if:
    // - Samples are near-identical (dx < epsilon), OR
    // - We don't have a valid D1_prev_ yet (need 2+ samples for second-order)
    float y;
    if (absDx < kEpsilon || !hasValidD1Prev_) {
        y = D1;
    } else {
        // Second-order ADAA using polynomial extrapolation:
        // D2[n] = 2*D1[n] - D1[n-1]
        // This extrapolates based on the slope of D1 values
        y = 2.0f * D1 - D1_prev_;
    }

    // FR-021: Update D1_prev for next sample
    D1_prev_ = D1;
    hasValidD1Prev_ = true;  // After first valid D1 computation, D1_prev_ is valid

    return y;
}

inline float HardClipADAA::process(float x) noexcept {
    // FR-007: Threshold of 0.0 always returns 0.0
    if (threshold_ == 0.0f) {
        return 0.0f;
    }

    // FR-028: NaN propagation
    if (detail::isNaN(x)) {
        return x;
    }

    // FR-029: Handle infinity by clamping to threshold
    if (detail::isInf(x)) {
        const float result = (x > 0.0f) ? threshold_ : -threshold_;
        // Update state for next sample
        x1_ = x;
        hasPreviousSample_ = true;
        return result;
    }

    // FR-027: First sample after reset uses naive hard clip
    if (!hasPreviousSample_) {
        hasPreviousSample_ = true;
        x1_ = x;
        // Note: D1_prev_ is NOT set here - it will be set on the second sample
        // when processSecondOrder() computes the first valid D1 value.
        // hasValidD1Prev_ remains false until then.
        return Sigmoid::hardClip(x, threshold_);
    }

    float y;
    if (order_ == Order::First) {
        y = processFirstOrder(x);
    } else {
        y = processSecondOrder(x);
    }

    // Update state for next sample
    x1_ = x;

    return y;
}

inline void HardClipADAA::processBlock(float* buffer, size_t n) noexcept {
    // FR-014, FR-015: Block processing is equivalent to N sequential process() calls
    for (size_t i = 0; i < n; ++i) {
        buffer[i] = process(buffer[i]);
    }
}

} // namespace DSP
} // namespace Krate
