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
// Phase 2 uses 12 modes (FR-022). Fewer modes than the other bodies because
// the free-free beam ratios grow rapidly, so higher modes are already above
// Nyquist for sensible fundamentals.
// ==============================================================================

#include <cmath>

namespace Membrum::Bodies {

inline constexpr int kShellModeCount = 12;

// First 6 values are the exact roots of cos(bL)*cosh(bL)=1 (spec-135
// verified); the remaining 6 follow the asymptotic (2k+1)*pi/2 formula.
inline constexpr float kShellRatios[kShellModeCount] = {
    1.000f,  2.757f,  5.404f,  8.933f, 13.344f, 18.637f,
    24.812f, 31.870f, 39.810f, 48.632f, 58.336f, 68.922f
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
