#pragma once

// ==============================================================================
// Bell Mode Constants -- Chladni church-bell partial ratios
// ==============================================================================
// Sources:
//   - Rossing, T.D. (2000): "Science of Percussion Instruments", Chapter 5
//     "Bells", World Scientific.
//   - Fletcher & Rossing (1991/1998): "The Physics of Musical Instruments",
//     Chapter 21 "Bells".
//   - Hibberts, B. (2014): "The Five Main Partials of a Church Bell"
//     http://www.hibberts.co.uk/ (the "big bell" parameter set).
//   - Spec 135 "Bell" body reference.
//
// A traditional church bell is tuned around the 5 lowest partials:
//   hum, prime (fundamental), tierce, quint, nominal
// with ratios (relative to nominal = 1.0):
//   0.250, 0.500, 0.600, 0.750, 1.000
//
// Higher partials extend via the Chladni-style formula
//   f_{m,n} = C * (m + b*n)^p
// using Hibberts' "big bell" parameters. Phase 2 uses 16 modes (FR-024).
//
// Phase 8B: bell mode count stays at 16. Extending requires empirical
// tuning against real bell recordings (Hibberts' data covers only the
// first ~16 partials accurately), which is beyond the scope of this
// phase. Documented limit.
//
// Note the ratio table is expressed with "nominal" (index 4) = 1.000. The
// Size->fundamental formula in bell_mapper.h assigns f0 to the nominal
// partial, so the hum note sits at 0.25*f0 (two octaves below nominal) and
// the tierce at 0.6*f0 (the minor-third character of the bell's prime note).
// ==============================================================================

#include <cmath>

namespace Membrum::Bodies {

inline constexpr int kBellModeCount = 16;

// Chladni partial ratios normalized to nominal = 1.000. First 5 entries are
// the classical hum/prime/tierce/quint/nominal series; entries 5..15 extend
// the upper partials via Hibberts' big-bell Chladni formula.
inline constexpr float kBellRatios[kBellModeCount] = {
    0.250f, 0.500f, 0.600f, 0.750f, 1.000f,
    1.500f, 2.000f, 2.600f, 3.200f, 4.000f,
    5.333f, 6.400f, 7.333f, 8.667f, 10.000f, 12.000f
};

/// Strike-position amplitude for a bell mode, using the Chladni radial
/// approximation per research.md §4.5:
///   r/R = strikePos
///   A_k = | cos((k+1) * pi * r/R) |   (simple radial standing wave)
/// This reproduces the empirical "near-lip vs near-crown" tonal difference
/// without a full Chladni eigenfunction library.
[[nodiscard]] inline float computeBellAmplitude(int modeIdx,
                                                float strikePos) noexcept
{
    if (modeIdx < 0 || modeIdx >= kBellModeCount)
        return 0.0f;
    constexpr float kPi = 3.14159265358979f;
    const float k = static_cast<float>(modeIdx + 1);
    const float r = strikePos;  // r/R
    return std::abs(std::cos(k * kPi * r));
}

} // namespace Membrum::Bodies
