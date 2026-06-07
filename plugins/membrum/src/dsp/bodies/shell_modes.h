#pragma once

// ==============================================================================
// Shell Mode Constants -- Free-free Euler-Bernoulli beam modal ratios + shapes
// ==============================================================================
// Sources:
//   - Fletcher & Rossing (1991/1998): "The Physics of Musical Instruments",
//     Chapter 2, Section 2.14 "Transverse Vibrations of a Free-Free Bar".
//   - Rossing & Fletcher (2008): "Principles of Vibration and Sound",
//     Section 2.19 "Free Bars".
//   - Spec 135 "Shell" body reference (untuned metal bar / glockenspiel-style
//     resonator).
//
// For a free-free Euler-Bernoulli beam the natural frequencies satisfy
//   cos(beta_k * L) * cosh(beta_k * L) = 1
// whose first six roots give the canonical ratios (relative to the 1st mode):
//   1.000, 2.757, 5.404, 8.933, 13.344, 18.637
// Modes 7..12 follow the asymptotic approximation:
//   beta_k * L ~= (2k + 1) * pi / 2
//
// Phase 8B: 12 -> 32 modes (AVX2 8-lane aligned, 4 clean kernel iters).
// Modes beyond Nyquist at typical fundamentals are culled by the modal
// bank, so the extended table is harmless for small-shell presets and
// useful for large-shell / low-fundamental presets.
// ==============================================================================

#include <cmath>

namespace Membrum::Bodies {

inline constexpr int kShellModeCount = 32;

// First 6 values are the exact roots of cos(bL)*cosh(bL)=1 (spec-135
// verified); the remaining 26 follow the asymptotic (2k+1)*pi/2 formula
// for the free-free Euler-Bernoulli beam: ratio_k ~ ((2k+1)pi/2 / 4.73)^2.
inline constexpr float kShellRatios[kShellModeCount] = {
    1.000f,   2.757f,   5.404f,   8.933f,  13.344f,  18.637f,
    24.812f,  31.870f,  39.810f,  48.632f,  58.336f,  68.922f,
    80.430f,  92.720f, 105.970f, 120.130f, 135.120f, 150.990f,
    167.750f, 185.420f, 203.950f, 223.390f, 243.680f, 264.880f,
    286.950f, 309.880f, 333.720f, 358.420f, 384.020f, 410.500f,
    437.900f, 466.130f
};

/// Exact roots beta_k*L of cos(bL)cosh(bL)=1 for the first six modes; higher
/// modes use the asymptotic (2k+1)*pi/2. Used by the free-free mode shape below.
[[nodiscard]] inline double shellBetaL(int modeIdx) noexcept
{
    constexpr double kBetaL[6] = {
        4.730040745, 7.853204624, 10.995607838,
        14.137165491, 17.278759657, 20.420352245
    };
    if (modeIdx >= 0 && modeIdx < 6)
        return kBetaL[modeIdx];
    constexpr double kPi = 3.14159265358979;
    // 1-based mode number n = modeIdx + 1 -> (2n + 1) * pi / 2.
    return (2.0 * static_cast<double>(modeIdx + 1) + 1.0) * kPi * 0.5;
}

/// Strike-position amplitude for mode k along a FREE-FREE beam.
///
/// PHYSICS CHANGE (signal-path audit §3-B, "Shell strike shape is simply-
/// supported sin(k*pi*x), not free-free"): the previous shape sin(k*pi*x/L) is
/// the SIMPLY-SUPPORTED (pinned-end) beam mode shape, which is identically ZERO
/// at both ends (x=0, x=L). But the ends of a free-free bar are exactly where it
/// is normally struck, and the true free-free shape has a MAXIMUM antinode there.
/// So an edge strike collapsed every mode to the 0.05 floor -> one dull spectrum
/// regardless of Strike Position.
///
/// The correct free-free Euler-Bernoulli mode shape is
///   phi_k(xi) = cosh(a) + cos(a) - sigma_k * (sinh(a) + sin(a)),  a = bL*xi
///   sigma_k   = (cosh(bL) - cos(bL)) / (sinh(bL) - sin(bL))
/// with xi = x/L = strikePos in [0,1]. It is +/-1 (antinode, scaled to +/-2 in
/// this normalization, returned at half so the range is ~[-1,1]) at both free
/// ends and has k-1 interior nodes.
///
/// Numerical note: cosh(a) - sigma*sinh(a) is a difference of huge numbers for
/// high modes (catastrophic cancellation even in double). We instead form the
/// tiny (1 - sigma) directly -- 1 - sigma = (cos(bL) - sin(bL) - e^{-bL}) /
/// (sinh(bL) - sin(bL)), all O(1)/huge and accurate -- and use
///   cosh(a) - sigma*sinh(a) = 0.5*(1-sigma)*e^{a} + 0.5*(1+sigma)*e^{-a},
/// which is bounded (the (1-sigma)*e^{a} product stays O(1)) and stable for all
/// 32 modes.
[[nodiscard]] inline float computeShellAmplitude(int modeIdx,
                                                 float strikePos) noexcept
{
    if (modeIdx < 0 || modeIdx >= kShellModeCount)
        return 0.0f;
    const double bL  = shellBetaL(modeIdx);
    const double xi  = static_cast<double>(strikePos);  // x / L
    const double a   = bL * xi;

    const double sinhBL = std::sinh(bL);
    const double denom  = sinhBL - std::sin(bL);
    // 1 - sigma computed without cancellation (numerator is O(1)).
    const double oneMinusSigma =
        (std::cos(bL) - std::sin(bL) - std::exp(-bL)) / denom;
    const double sigma = 1.0 - oneMinusSigma;

    const double expA  = std::exp(a);
    const double expmA = std::exp(-a);
    // cosh(a) - sigma*sinh(a), stable decomposition.
    const double coshMinusSigmaSinh =
        0.5 * oneMinusSigma * expA + 0.5 * (1.0 + sigma) * expmA;
    const double phi =
        coshMinusSigmaSinh + std::cos(a) - sigma * std::sin(a);

    return static_cast<float>(0.5 * phi);  // ~[-1, 1]
}

} // namespace Membrum::Bodies
