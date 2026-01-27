// ==============================================================================
// Layer 0: Core Utilities
// filter_design.h - Filter Design Utilities
// ==============================================================================
// API Contract for specs/070-filter-foundations
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (no allocations, noexcept)
// - Principle III: Modern C++ (constexpr where feasible)
// - Principle IX: Layer 0 (depends only on math_constants.h, db_utils.h)
// ==============================================================================

#pragma once

#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>

#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

/// @brief Filter design utility functions.
///
/// Provides common calculations needed for digital filter design:
/// - Frequency prewarping for bilinear transform
/// - Comb filter feedback coefficient calculation
/// - Chebyshev Type I Q value calculation
/// - Bessel filter Q value lookup
/// - Butterworth pole angle calculation
namespace FilterDesign {

// =============================================================================
// Frequency Prewarping (FR-006)
// =============================================================================

/// @brief Prewarp frequency for bilinear transform compensation.
///
/// The bilinear transform causes frequency warping when converting analog
/// filter designs to digital. This function calculates the prewarped analog
/// frequency that, after bilinear transform, produces the desired digital
/// cutoff frequency.
///
/// @formula f_prewarped = (sampleRate / pi) * tan(pi * freq / sampleRate)
///
/// @param freq Desired digital cutoff frequency in Hz
/// @param sampleRate Sample rate in Hz
/// @return Prewarped analog prototype frequency to use in filter design
///
/// @note Returns freq unchanged if sampleRate <= 0 or freq <= 0
/// @note Clamps omega to avoid tan(pi/2) singularity near Nyquist
///
/// @example
/// ```cpp
/// float fc = 1000.0f;  // Desired 1kHz cutoff
/// float prewarped = FilterDesign::prewarpFrequency(fc, 44100.0);
/// // Use prewarped frequency in analog prototype, then apply bilinear transform
/// ```
[[nodiscard]] inline float prewarpFrequency(float freq, double sampleRate) noexcept;

// =============================================================================
// RT60 Feedback Calculation (FR-007)
// =============================================================================

/// @brief Calculate comb filter feedback coefficient for desired RT60.
///
/// Given a delay line length and desired reverberation time (T60), calculates
/// the feedback coefficient needed to achieve that decay. Based on Schroeder's
/// reverberator design (1962).
///
/// @formula g = 10^(-3 * delayMs / (1000 * rt60Seconds))
///
/// @param delayMs Delay time in milliseconds
/// @param rt60Seconds Desired reverb decay time (T60) in seconds
/// @return Feedback coefficient in range [0, 1)
///
/// @note Returns 0.0f if delayMs <= 0 or rt60Seconds <= 0
/// @note RT60 is the time for the signal to decay by 60dB
///
/// @example
/// ```cpp
/// // 50ms delay with 2 second reverb tail
/// float g = FilterDesign::combFeedbackForRT60(50.0f, 2.0f);
/// // g ~= 0.841 - after 40 round trips (2000ms), amplitude = 0.001 (-60dB)
/// ```
[[nodiscard]] inline float combFeedbackForRT60(float delayMs, float rt60Seconds) noexcept;

// =============================================================================
// Chebyshev Q Calculation (FR-008)
// =============================================================================

/// @brief Calculate Q value for Chebyshev Type I filter cascade stage.
///
/// Chebyshev Type I filters have equiripple passband response and monotonic
/// stopband. The Q values for each biquad stage are derived from the pole
/// locations on an ellipse in the s-plane.
///
/// @param stage 0-indexed stage number (0 = first biquad)
/// @param numStages Total number of biquad stages (even order / 2)
/// @param rippleDb Passband ripple in dB (e.g., 0.5, 1.0, 3.0)
/// @return Q value for the specified stage
///
/// @note Returns Butterworth Q if rippleDb <= 0 (Butterworth is Chebyshev with 0 ripple)
/// @note Stage 0 typically has highest Q, decreasing for subsequent stages
/// @note For odd-order filters, first stage is first-order (not returned here)
///
/// @example
/// ```cpp
/// // 4th order Chebyshev with 1dB ripple (2 biquad stages)
/// float q0 = FilterDesign::chebyshevQ(0, 2, 1.0f);  // ~1.303
/// float q1 = FilterDesign::chebyshevQ(1, 2, 1.0f);  // ~0.541
/// ```
[[nodiscard]] inline float chebyshevQ(size_t stage, size_t numStages, float rippleDb) noexcept;

// =============================================================================
// Bessel Q Lookup (FR-009)
// =============================================================================

/// @brief Get Q value for Bessel filter cascade stage.
///
/// Bessel filters have maximally flat group delay, providing excellent
/// transient response with no overshoot. Q values are pre-computed from
/// Bessel polynomial roots and stored in a lookup table.
///
/// @param stage 0-indexed stage number (0 = first biquad)
/// @param numStages Total filter order (2-8 supported)
/// @return Q value for the specified stage
///
/// @note Returns 0.7071f (Butterworth) for unsupported orders (< 2 or > 8)
/// @note For odd orders, the first stage is first-order; remaining are biquads
/// @note Stage 0 has highest Q; values decrease for subsequent stages
///
/// @example
/// ```cpp
/// // 4th order Bessel (2 biquad stages)
/// float q0 = FilterDesign::besselQ(0, 4);  // 0.80554
/// float q1 = FilterDesign::besselQ(1, 4);  // 0.52193
/// ```
[[nodiscard]] constexpr float besselQ(size_t stage, size_t numStages) noexcept;

// =============================================================================
// Butterworth Pole Angle (FR-010)
// =============================================================================

/// @brief Calculate pole angle for Butterworth filter.
///
/// Butterworth filter poles are evenly spaced on a circle in the s-plane.
/// This function returns the angle of the k-th pole for an N-th order filter.
///
/// @formula theta_k = pi * (2*k + 1) / (2*N)
///
/// @param k 0-indexed pole number
/// @param N Filter order
/// @return Pole angle in radians
///
/// @note For order N, there are N poles; only the left half-plane poles
///       (with negative real part) are stable and used in filter design.
/// @note Returns 0.0f if N == 0
///
/// @example
/// ```cpp
/// // 2nd order Butterworth pole angles
/// float theta0 = FilterDesign::butterworthPoleAngle(0, 2);  // 3*pi/4
/// float theta1 = FilterDesign::butterworthPoleAngle(1, 2);  // 5*pi/4
/// ```
[[nodiscard]] constexpr float butterworthPoleAngle(size_t k, size_t N) noexcept;

// =============================================================================
// Implementation Details (not part of public API)
// =============================================================================

namespace detail {

/// Bessel Q lookup table for orders 2-8.
/// Table layout: besselQTable[order-2][stage]
inline constexpr float besselQTable[7][4] = {
    {0.57735f,  0.0f,     0.0f,     0.0f},      // Order 2: 1 biquad
    {0.69105f,  0.0f,     0.0f,     0.0f},      // Order 3: 1st-order + 1 biquad
    {0.80554f,  0.52193f, 0.0f,     0.0f},      // Order 4: 2 biquads
    {0.91648f,  0.56354f, 0.0f,     0.0f},      // Order 5: 1st-order + 2 biquads
    {1.02331f,  0.61119f, 0.51032f, 0.0f},      // Order 6: 3 biquads
    {1.12626f,  0.66082f, 0.53236f, 0.0f},      // Order 7: 1st-order + 3 biquads
    {1.22567f,  0.71085f, 0.55961f, 0.50599f},  // Order 8: 4 biquads
};

/// Natural log of 10 for dB conversions.
inline constexpr float kLn10 = 2.302585093f;

} // namespace detail

// =============================================================================
// Inline Implementations
// =============================================================================

inline float prewarpFrequency(float freq, double sampleRate) noexcept {
    if (sampleRate <= 0.0 || freq <= 0.0f) {
        return freq;
    }

    const float omega = kPi * freq / static_cast<float>(sampleRate);
    // Clamp to 1.5 radians to avoid tan approaching infinity
    const float clampedOmega = (omega > 1.5f) ? 1.5f : omega;

    return (static_cast<float>(sampleRate) / kPi) * std::tan(clampedOmega);
}

inline float combFeedbackForRT60(float delayMs, float rt60Seconds) noexcept {
    if (delayMs <= 0.0f || rt60Seconds <= 0.0f) {
        return 0.0f;
    }

    const float rt60Ms = rt60Seconds * 1000.0f;
    const float exponent = -3.0f * delayMs / rt60Ms;

    // 10^x = e^(x * ln(10))
    return detail::constexprExp(exponent * detail::kLn10);
}

inline float chebyshevQ(size_t stage, size_t numStages, float rippleDb) noexcept {
    if (numStages == 0) {
        return 0.7071f;  // Butterworth default
    }

    // Fall back to Butterworth for zero or negative ripple
    if (rippleDb <= 0.0f) {
        // Butterworth Q: 1 / (2 * cos(pi * (2*k + 1) / (4*n)))
        const float n = static_cast<float>(numStages);
        const float k = static_cast<float>(stage);
        const float angle = kPi * (2.0f * k + 1.0f) / (4.0f * n);
        return 1.0f / (2.0f * std::cos(angle));
    }

    const float n = static_cast<float>(numStages);

    // epsilon = sqrt(10^(ripple/10) - 1)
    const float epsilon = std::sqrt(std::pow(10.0f, rippleDb / 10.0f) - 1.0f);

    // mu = (1/n) * asinh(1/epsilon)
    const float mu = (1.0f / n) * std::asinh(1.0f / epsilon);

    // Pole location for stage k
    const float k = static_cast<float>(stage);
    const float theta = kPi * (2.0f * k + 1.0f) / (2.0f * n);

    const float sigma = -std::sinh(mu) * std::sin(theta);  // Real part
    const float omega = std::cosh(mu) * std::cos(theta);   // Imaginary part

    // Q = |pole| / (2 * |sigma|)
    const float poleMag = std::sqrt(sigma * sigma + omega * omega);
    return poleMag / (2.0f * std::abs(sigma));
}

constexpr float besselQ(size_t stage, size_t numStages) noexcept {
    // Supported orders: 2-8
    if (numStages < 2 || numStages > 8) {
        return 0.7071f;  // Butterworth fallback
    }

    // Number of biquad stages
    const size_t numBiquads = numStages / 2;
    if (stage >= numBiquads) {
        return 0.7071f;  // Invalid stage
    }

    return detail::besselQTable[numStages - 2][stage];
}

constexpr float butterworthPoleAngle(size_t k, size_t N) noexcept {
    if (N == 0) {
        return 0.0f;
    }
    return kPi * (2.0f * static_cast<float>(k) + 1.0f) / (2.0f * static_cast<float>(N));
}

} // namespace FilterDesign
} // namespace DSP
} // namespace Krate
