// ==============================================================================
// Layer 0: Core Utility - Chebyshev Polynomial Library
// ==============================================================================
// Chebyshev polynomials of the first kind for harmonic control in waveshaping.
// When a sine wave of amplitude 1.0 is passed through Tn(x), it produces the
// nth harmonic: Tn(cos(theta)) = cos(n*theta).
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations)
// - Principle III: Modern C++ (constexpr, C++20)
// - Principle IX: Layer 0 (no dependencies on higher layers)
// - Principle XIV: ODR Prevention (unique namespace)
//
// Reference: specs/049-chebyshev-polynomials/spec.md
// ==============================================================================

#pragma once

#include <algorithm>
#include <cstddef>

namespace Krate {
namespace DSP {
namespace Chebyshev {

// =============================================================================
// Constants
// =============================================================================

/// Maximum supported harmonics for harmonicMix function.
/// 32nd harmonic of 1kHz = 32kHz, exceeding Nyquist for standard sample rates.
constexpr int kMaxHarmonics = 32;

// =============================================================================
// Individual Chebyshev Polynomials T1-T8
// =============================================================================
// Implemented using Horner's method for numerical stability and efficiency.
// Each function evaluates T_n(x) where input x is typically in [-1, 1].
// =============================================================================

/// T1(x) = x (identity/fundamental)
/// Produces the fundamental frequency when applied to cos(theta).
/// @param x Input value, typically in [-1, 1] for pure harmonic generation
/// @return x (unchanged)
[[nodiscard]] constexpr float T1(float x) noexcept {
    return x;
}

/// T2(x) = 2x^2 - 1 (2nd harmonic)
/// Produces the 2nd harmonic when applied to cos(theta).
/// @param x Input value, typically in [-1, 1]
/// @return 2x^2 - 1
[[nodiscard]] constexpr float T2(float x) noexcept {
    // Horner form: 2x^2 - 1 = x*(2x) - 1
    return x * (2.0f * x) - 1.0f;
}

/// T3(x) = 4x^3 - 3x (3rd harmonic)
/// Produces the 3rd harmonic when applied to cos(theta).
/// @param x Input value, typically in [-1, 1]
/// @return 4x^3 - 3x
[[nodiscard]] constexpr float T3(float x) noexcept {
    // Horner form: x*(4x^2 - 3)
    return x * (4.0f * x * x - 3.0f);
}

/// T4(x) = 8x^4 - 8x^2 + 1 (4th harmonic)
/// Produces the 4th harmonic when applied to cos(theta).
/// @param x Input value, typically in [-1, 1]
/// @return 8x^4 - 8x^2 + 1
[[nodiscard]] constexpr float T4(float x) noexcept {
    // Horner form: ((8x^2 - 8)x^2) + 1 = 8x^2(x^2 - 1) + 1
    float x2 = x * x;
    return 8.0f * x2 * (x2 - 1.0f) + 1.0f;
}

/// T5(x) = 16x^5 - 20x^3 + 5x (5th harmonic)
/// Produces the 5th harmonic when applied to cos(theta).
/// @param x Input value, typically in [-1, 1]
/// @return 16x^5 - 20x^3 + 5x
[[nodiscard]] constexpr float T5(float x) noexcept {
    // Horner form: x*((16x^2 - 20)x^2 + 5)
    float x2 = x * x;
    return x * ((16.0f * x2 - 20.0f) * x2 + 5.0f);
}

/// T6(x) = 32x^6 - 48x^4 + 18x^2 - 1 (6th harmonic)
/// Produces the 6th harmonic when applied to cos(theta).
/// @param x Input value, typically in [-1, 1]
/// @return 32x^6 - 48x^4 + 18x^2 - 1
[[nodiscard]] constexpr float T6(float x) noexcept {
    // Horner form: (((32x^2 - 48)x^2 + 18)x^2) - 1
    float x2 = x * x;
    return ((32.0f * x2 - 48.0f) * x2 + 18.0f) * x2 - 1.0f;
}

/// T7(x) = 64x^7 - 112x^5 + 56x^3 - 7x (7th harmonic)
/// Produces the 7th harmonic when applied to cos(theta).
/// @param x Input value, typically in [-1, 1]
/// @return 64x^7 - 112x^5 + 56x^3 - 7x
[[nodiscard]] constexpr float T7(float x) noexcept {
    // Horner form: x*(((64x^2 - 112)x^2 + 56)x^2 - 7)
    float x2 = x * x;
    return x * (((64.0f * x2 - 112.0f) * x2 + 56.0f) * x2 - 7.0f);
}

/// T8(x) = 128x^8 - 256x^6 + 160x^4 - 32x^2 + 1 (8th harmonic)
/// Produces the 8th harmonic when applied to cos(theta).
/// @param x Input value, typically in [-1, 1]
/// @return 128x^8 - 256x^6 + 160x^4 - 32x^2 + 1
[[nodiscard]] constexpr float T8(float x) noexcept {
    // Horner form: ((((128x^2 - 256)x^2 + 160)x^2 - 32)x^2) + 1
    float x2 = x * x;
    return (((128.0f * x2 - 256.0f) * x2 + 160.0f) * x2 - 32.0f) * x2 + 1.0f;
}

// =============================================================================
// Generic Tn(x, n) - Arbitrary Order Chebyshev Polynomial
// =============================================================================
// Uses the recurrence relation: T_n(x) = 2x * T_{n-1}(x) - T_{n-2}(x)
// with base cases T_0(x) = 1 and T_1(x) = x.
// =============================================================================

/// Compute Chebyshev polynomial T_n(x) for arbitrary order n.
/// Uses the recurrence relation for numerical stability.
/// @param x Input value, typically in [-1, 1] for pure harmonic generation
/// @param n Order of the polynomial (0, 1, 2, ...). Negative values clamped to 0.
/// @return T_n(x)
[[nodiscard]] constexpr float Tn(float x, int n) noexcept {
    // Handle negative n by clamping to 0
    if (n <= 0) {
        return 1.0f;  // T_0(x) = 1
    }
    if (n == 1) {
        return x;  // T_1(x) = x
    }

    // Use recurrence relation: T_n = 2x * T_{n-1} - T_{n-2}
    float tPrev2 = 1.0f;  // T_0
    float tPrev1 = x;     // T_1
    float tCurrent = 0.0f;

    for (int i = 2; i <= n; ++i) {
        tCurrent = 2.0f * x * tPrev1 - tPrev2;
        tPrev2 = tPrev1;
        tPrev1 = tCurrent;
    }

    return tCurrent;
}

// =============================================================================
// Harmonic Mix Function
// =============================================================================
// Efficiently computes weighted sum of multiple Chebyshev polynomials
// using Clenshaw's recurrence algorithm for O(n) evaluation.
// =============================================================================

/// Compute weighted sum of Chebyshev polynomials using Clenshaw's algorithm.
///
/// Computes: sum(weights[i] * T_{i+1}(x)) for i = 0 to numHarmonics-1
///
/// Note: weights[0] corresponds to T_1 (fundamental), weights[1] to T_2, etc.
/// T_0 is not included in the sum (it's a DC offset).
///
/// @param x Input value, typically in [-1, 1]
/// @param weights Array of harmonic weights [w1, w2, ..., wN] where wN corresponds to T_N
/// @param numHarmonics Number of harmonics to sum (max kMaxHarmonics=32)
/// @return Weighted sum of Chebyshev polynomials
[[nodiscard]] inline float harmonicMix(float x, const float* weights,
                                       std::size_t numHarmonics) noexcept {
    // Handle null pointer
    if (weights == nullptr) {
        return 0.0f;
    }

    // Handle zero harmonics
    if (numHarmonics == 0) {
        return 0.0f;
    }

    // Clamp to maximum harmonics
    if (numHarmonics > static_cast<std::size_t>(kMaxHarmonics)) {
        numHarmonics = static_cast<std::size_t>(kMaxHarmonics);
    }

    // Clenshaw's algorithm for evaluating sum of Chebyshev polynomials
    // We compute sum(c[k] * T_k(x)) where c[k] = weights[k-1] for k = 1..numHarmonics
    //
    // Clenshaw recurrence for T_k:
    //   b_{n+2} = 0
    //   b_{n+1} = 0
    //   b_k = c_k + 2x * b_{k+1} - b_{k+2}, for k = n, n-1, ..., 1
    //   result = b_0 = c_0 + x*b_1 - b_2  (but c_0 = 0 since we don't include T_0)
    //
    // For sum starting at T_1 (no T_0), we have:
    //   result = x * b_1 - b_2

    float b1 = 0.0f;
    float b2 = 0.0f;

    // Process from highest order down to T_1
    // weights[numHarmonics-1] corresponds to T_{numHarmonics}
    for (std::size_t k = numHarmonics; k >= 1; --k) {
        float c_k = weights[k - 1];  // coefficient for T_k
        float b0 = c_k + 2.0f * x * b1 - b2;
        b2 = b1;
        b1 = b0;
    }

    // Final step: result = T_0 coefficient (0) + x*b_1 - b_2
    // Since we're summing T_1 to T_n only (no T_0), this is:
    return x * b1 - b2;
}

} // namespace Chebyshev
} // namespace DSP
} // namespace Krate
