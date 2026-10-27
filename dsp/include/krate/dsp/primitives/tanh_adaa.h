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
#include <krate/dsp/core/log_cosh_table.h>

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

    /// @brief process() with F1Fast in place of F1 (Profundum Phase 14 R11, opt-in).
    ///
    /// The same ADAA, epsilon fallback, NaN / infinity handling and state as process(); not
    /// bit-identical to it. Do not mix the two on one instance between reset() calls.
    [[nodiscard]] float processFast(float x) noexcept;

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

    /// @brief F1 in double with ln(1 + exp(-2|x|)) from the kLogCoshTable polynomials
    /// (core/log_cosh_table.h, |error| <= 2.4e-9) instead of std::exp + std::log1p: about 3.6x
    /// cheaper (Profundum Phase 14 R11). processFast keeps it and the ADAA quotient in double.
    /// @p x must not be NaN.
    [[nodiscard]] static double F1Fast(double x) noexcept;

private:
    template <bool Fast>
    [[nodiscard]] float processImpl(float x) noexcept;

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
    // Profundum Phase 9 T029 (bit-identical): F1(x1_ * drive_) is F1(x * drive_) of the previous
    // ADAA sample while drive_ is unchanged, so it is kept instead of being recomputed (one exp
    // and one log1p per sample fewer). Invalid after setDrive(), reset() and every branch that
    // updates x1_ without evaluating F1.
    float f1x1_ = 0.0f;         ///< F1(x1_ * drive_) when f1Valid_
    double f1x1Fast_ = 0.0;     ///< F1Fast(x1_ * drive_) when f1Valid_ (processFast)
    bool f1Valid_ = false;      ///< f1x1_ matches the current x1_ and drive_
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline void TanhADAA::setDrive(float drive) noexcept {
    // FR-003: Negative drive treated as absolute value
    drive_ = (drive < 0.0f) ? -drive : drive;
    f1Valid_ = false;
}

inline void TanhADAA::reset() noexcept {
    // FR-005: Clear all internal state but preserve configuration
    x1_ = 0.0f;
    hasPreviousSample_ = false;
    f1Valid_ = false;
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

inline double TanhADAA::F1Fast(double x) noexcept {
    constexpr double kLn2d = 0.69314718055994531;
    const double absX = (x >= 0.0) ? x : -x;
    if (absX >= static_cast<double>(kOverflowThreshold)) {
        return absX - kLn2d;
    }
    // absX < 20, so seg = floor(4 absX) <= 79 and u = absX - seg / 4 is in [0, 0.25).
    const auto seg = static_cast<int>(absX * static_cast<double>(detail::kLogCoshSegmentsPerUnit));
    const double u = absX - static_cast<double>(detail::kLogCoshSegmentWidth) * static_cast<double>(seg);
    const auto& c = detail::kLogCoshTable[static_cast<std::size_t>(seg)];
    const double g = ((((c[5] * u + c[4]) * u + c[3]) * u + c[2]) * u + c[1]) * u + c[0];
    return absX - kLn2d + g;
}

inline float TanhADAA::process(float x) noexcept {
    return processImpl<false>(x);
}

inline float TanhADAA::processFast(float x) noexcept {
    return processImpl<true>(x);
}

template <bool Fast>
inline float TanhADAA::processImpl(float x) noexcept {
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
        f1Valid_ = false;
        return result;
    }

    // FR-018: First sample after reset uses naive tanh
    if (!hasPreviousSample_) {
        hasPreviousSample_ = true;
        x1_ = x;
        f1Valid_ = false;
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
        f1Valid_ = false;
    } else if constexpr (Fast) {
        // R11: the same quotient with F1Fast, the antiderivatives and the quotient in double.
        const auto d = static_cast<double>(drive_);
        const double xd = static_cast<double>(x);
        const double x1d = static_cast<double>(x1_);
        const double f1x = F1Fast(xd * d);
        const double f1x1 = f1Valid_ ? f1x1Fast_ : F1Fast(x1d * d);
        y = static_cast<float>((f1x - f1x1) / (d * (xd - x1d)));
        f1x1Fast_ = f1x;
        f1Valid_ = true;
    } else {
        // FR-012: First-order ADAA formula
        // y = (F1(x*drive) - F1(x1*drive)) / (drive * (x - x1))
        const float xScaled = x * drive_;
        const float f1x = F1(xScaled);
        const float f1x1 = f1Valid_ ? f1x1_ : F1(x1_ * drive_);
        y = (f1x - f1x1) / (drive_ * dx);
        f1x1_ = f1x;   // == F1(x1_ * drive_) of the next sample (same product, same drive_)
        f1Valid_ = true;
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
