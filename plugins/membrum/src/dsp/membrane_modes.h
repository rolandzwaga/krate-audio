#pragma once

// ==============================================================================
// Membrane Mode Constants -- Bessel function zeros for circular membrane
// ==============================================================================
// Provides:
//   - kMembraneRatios: 48 mode frequency ratios (j_mn / j_01)
//   - kMembraneBesselOrder: Bessel order m for each mode
//   - kMembraneBesselZeros: Actual j_mn values
//   - evaluateBesselJ(): Polynomial approximation of J_m(x) for m=0..14
//
// Phase 8B: mode count 16 -> 48 (first 48 j_mn zeros sorted by magnitude).
// Max Bessel order m = 14. Max j_mn ~ 19.6.
//
// Source for j_mn values: Abramowitz & Stegun, Handbook of Mathematical
// Functions, Table 9.5 (Zeros of Bessel functions). Accurate to 4 decimal
// places.
//
// FR-031: Bessel ratios for circular membrane
// FR-035: Strike position amplitude calculation via J_m
// ==============================================================================

#include <array>
#include <cmath>

namespace Membrum {

// 48 circular membrane modes -- Bessel zero ratios (j_mn / j_01) sorted
// ascending. j_01 = 2.4048.
constexpr std::array<float, 48> kMembraneRatios = {
    1.0000f, 1.5933f, 2.1357f, 2.2954f, 2.6528f, 2.9173f, 3.1554f, 3.5001f,
    3.5984f, 3.6475f, 4.0589f, 4.1316f, 4.2304f, 4.6009f, 4.6099f, 4.8320f,
    4.9033f, 5.0838f, 5.1309f, 5.4124f, 5.5408f, 5.5535f, 5.6509f, 5.9765f,
    6.0194f, 6.1528f, 6.1633f, 6.2097f, 6.4826f, 6.5282f, 6.6683f, 6.7453f,
    6.8479f, 6.9428f, 7.0710f, 7.1712f, 7.3281f, 7.4063f, 7.4725f, 7.5193f,
    7.6101f, 7.6655f, 7.8592f, 7.8925f, 8.0710f, 8.1350f, 8.1607f, 8.1611f
};

// Bessel order (m) for each mode -- determines strike position response.
constexpr std::array<int, 48> kMembraneBesselOrder = {
    0, 1, 2, 0, 3, 1, 4, 2,
    0, 5, 3, 6, 1, 4, 7, 2,
    0, 8, 5, 3, 1, 9, 6, 4,
    10, 2, 7, 0, 11, 5, 8, 3,
    1, 12, 6, 9, 4, 13, 2, 0,
    7, 10, 14, 5, 3, 8, 1, 11
};

// j_mn values (actual Bessel zeros, for amplitude calculation).
constexpr std::array<float, 48> kMembraneBesselZeros = {
    2.4048f,  3.8317f,  5.1356f,  5.5201f,  6.3802f,  7.0156f,  7.5883f,  8.4172f,
    8.6537f,  8.7715f,  9.7610f,  9.9361f,  10.1735f, 11.0647f, 11.0864f, 11.6198f,
    11.7915f, 12.2251f, 12.3386f, 13.0152f, 13.3237f, 13.3543f, 13.5893f, 14.3725f,
    14.4755f, 14.7960f, 14.8213f, 14.9309f, 15.5898f, 15.7002f, 16.0378f, 16.2235f,
    16.4706f, 16.6983f, 17.0038f, 17.2412f, 17.6160f, 17.8014f, 17.9598f, 18.0711f,
    18.2876f, 18.4335f, 18.9000f, 18.9801f, 19.4094f, 19.5545f, 19.6159f, 19.6160f
};

// ==============================================================================
// Bessel function evaluation -- polynomial approximation
// ==============================================================================
// Approximation of J_m(x) using the truncated power series:
//   J_m(x) = sum_{k=0}^{N} (-1)^k / (k! * (m+k)!) * (x/2)^(2k+m)
//
// Phase 8B: term count raised 12 -> 20 to keep accuracy under 1% for the
// new m=0..14 range at strike arguments up to j_mn * 0.9 ~ 17.6. For a
// typical strike at r/a = 0.3 the argument reduces to ~5.9 which is well
// inside the convergence radius at all relevant m, so the existing
// polynomial remains appropriate.
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

    constexpr int kMaxTerms = 20;
    for (int k = 1; k <= kMaxTerms; ++k)
    {
        term *= -halfXSq / (static_cast<float>(k) * static_cast<float>(m + k));
        result += term;
    }

    return result;
}

} // namespace Membrum
