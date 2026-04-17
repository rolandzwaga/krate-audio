#pragma once

// ==============================================================================
// Shell Mode Constants -- Free-free Euler-Bernoulli beam modal ratios
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

/// Strike-position amplitude for mode k along a free-free beam.
/// For Phase 2 we use the simple sin(k * pi * x0 / L) approximation
/// (research.md §4.3 / spec 135 recommendation); x0/L == strikePos.
[[nodiscard]] inline float computeShellAmplitude(int modeIdx,
                                                 float strikePos) noexcept
{
    if (modeIdx < 0 || modeIdx >= kShellModeCount)
        return 0.0f;
    constexpr float kPi = 3.14159265358979f;
    const float k = static_cast<float>(modeIdx + 1);
    const float x = strikePos;  // x0/L
    return std::sin(k * kPi * x);
}

} // namespace Membrum::Bodies
