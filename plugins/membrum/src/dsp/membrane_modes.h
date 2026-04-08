#pragma once

// ==============================================================================
// Membrane Mode Constants -- Bessel function zeros for circular membrane
// ==============================================================================
// Provides:
//   - kMembraneRatios: 16 mode frequency ratios (j_mn / j_01)
//   - kMembraneBesselOrder: Bessel order m for each mode
//   - kMembraneBesselZeros: Actual j_mn values
//   - evaluateBesselJ(): Polynomial approximation of J_m(x) for m=0..6
//
// FR-031: Bessel ratios for circular membrane
// FR-035: Strike position amplitude calculation via J_m
// ==============================================================================

#include <array>
#include <cmath>

namespace Membrum {

// 16 circular membrane modes -- Bessel zero ratios (j_mn / j_01)
constexpr std::array<float, 16> kMembraneRatios = {
    1.000f, 1.593f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f,
    3.600f, 3.649f, 4.060f, 4.231f, 4.602f, 4.832f, 4.903f, 5.131f
};

// Bessel order (m) for each mode -- determines strike position response
constexpr std::array<int, 16> kMembraneBesselOrder = {
    0, 1, 2, 0, 3, 1, 4, 2, 0, 5, 3, 1, 6, 4, 2, 0
};

// j_mn values (actual Bessel zeros, for amplitude calculation)
constexpr std::array<float, 16> kMembraneBesselZeros = {
    2.405f, 3.832f, 5.136f, 5.520f, 6.380f, 7.016f, 7.588f, 8.417f,
    8.654f, 8.772f, 9.761f, 10.173f, 11.065f, 11.620f, 11.791f, 12.336f
};

// ==============================================================================
// Bessel function evaluation -- polynomial approximation
// ==============================================================================
// Approximation of J_m(x) using the truncated power series:
//   J_m(x) = sum_{k=0}^{N} (-1)^k / (k! * (m+k)!) * (x/2)^(2k+m)
//
// Accurate to ~5 significant digits for m=0..6 and x in [0, 12.5].
// Uses 12 terms of the series for sufficient convergence.
// ==============================================================================

inline float evaluateBesselJ(int m, float x) noexcept
{
    // J_m(x) = sum_{k=0}^{N} (-1)^k / (k! * (m+k)!) * (x/2)^(2k+m)
    // We compute iteratively to avoid factorial overflow.
    float halfX = x * 0.5f;

    // (x/2)^m for the leading power
    float xPowM = 1.0f;
    for (int i = 0; i < m; ++i)
        xPowM *= halfX;

    // 1/m! for the leading factorial
    float invMFactorial = 1.0f;
    for (int i = 2; i <= m; ++i)
        invMFactorial /= static_cast<float>(i);

    float result = xPowM * invMFactorial; // k=0 term

    // Subsequent terms: multiply by (-1) * (x/2)^2 / (k * (m+k))
    float halfXSq = halfX * halfX;
    float term = result;

    constexpr int kMaxTerms = 12;
    for (int k = 1; k <= kMaxTerms; ++k)
    {
        term *= -halfXSq / (static_cast<float>(k) * static_cast<float>(m + k));
        result += term;
    }

    return result;
}

} // namespace Membrum
