#pragma once

// ==============================================================================
// Bell Mode Constants -- Church-bell partials with 2-D (m,n) mode shapes
// ==============================================================================
// Sources:
//   - Rossing, T.D. (2000): "Science of Percussion Instruments", Chapter 5
//     "Bells", World Scientific.
//   - Perrin, R., Charnley, T. & DePont, J. (1983): "Normal modes of the
//     modern English church bell", Journal of Sound and Vibration 90(1).
//   - Fletcher & Rossing (1991/1998): "The Physics of Musical Instruments",
//     Chapter 21 "Bells".
//   - Hibberts, B. (2014): "The Five Main Partials of a Church Bell".
//   - Spec 135 "Bell" body reference.
//
// A traditional church bell is tuned around the 5 lowest partials:
//   hum, prime (fundamental), tierce, quint, nominal
// with ratios (relative to nominal = 1.0):
//   0.250, 0.500, 0.600, 0.750, 1.000
// Higher partials extend via Hibberts' "big bell" Chladni fit.
//
// PHYSICS CHANGE (signal-path audit §3-B, "Bell strike shape |cos((k+1)*pi*r)|
// is not a bell mode shape"): bell modes are genuinely 2-D, classified by
// (m nodal meridians, n nodal circles). Binding the strike amplitude to a single
// radial index (k+1) had NO physical correspondence to the partial it scaled,
// and sweeping r is unphysical anyway -- a clapper always strikes the SAME spot
// (the soundbow, r ~ R). The correct mode shape factorizes as
//   phi(theta, z) = cos(m * theta) * Z_n(z)
// so what actually varies the timbre is WHERE around the rim (azimuth theta) the
// bell is struck relative to each mode's m nodal meridians. We therefore map the
// Strike Position scalar to the strike AZIMUTH and weight each partial by its
// meridional factor |cos(m * theta)| (the through-thickness factor Z_n at the
// fixed soundbow is approximated as ~1, mildly tapered with n).
//
// Honesty note: the meridional numbers m_k follow Perrin/Rossing's increasing-
// with-frequency grouping of bell partials; the exact (m,n) group assignments
// ABOVE the principal five are approximate (Hibberts' data resolves the partial
// FREQUENCIES, not a full (m,n) census). The correctness win is the mode-shape
// FORM (meridional cos(m*theta) at a fixed strike point), not a claim of exact
// per-partial (m,n) identification.
//
// Phase 8B: bell mode count stays at 16 (documented limit -- extending needs
// empirical tuning against real bell recordings).
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

struct BellModeIndices
{
    int m;  // nodal meridians (circumferential mode number)
    int n;  // nodal circles
};

// (m,n) per partial. First five are the canonical principal partials
// (hum/prime/tierce/quint/nominal); higher entries follow the increasing-m
// grouping of Perrin/Rossing (approximate above the principal five -- see
// header note). m drives the meridional strike factor; n a mild taper.
inline constexpr BellModeIndices kBellIndices[kBellModeCount] = {
    {2, 0}, {2, 1}, {3, 1}, {3, 1}, {4, 1},
    {4, 1}, {5, 1}, {5, 1}, {6, 1}, {6, 2},
    {7, 2}, {7, 2}, {8, 2}, {8, 2}, {9, 2}, {9, 3}
};

/// Strike-position amplitude for bell mode (m,n).
///
/// The Strike Position scalar maps to the strike AZIMUTH around the rim
/// (a clapper strikes a fixed soundbow, so the meridional position -- not the
/// radius -- is what varies the partial balance):
///   theta = strikePos * (pi / 2)
/// Amplitude = |cos(m * theta)| * throughThicknessTaper(n). At strikePos = 0 the
/// strike sits on a meridional antinode (all modes excited); higher m partials
/// pass through nodes as the azimuth sweeps, giving each partial a distinct,
/// physically-shaped response to the knob -- replacing the non-physical radial
/// (k+1) standing wave.
[[nodiscard]] inline float computeBellAmplitude(int modeIdx,
                                                float strikePos) noexcept
{
    if (modeIdx < 0 || modeIdx >= kBellModeCount)
        return 0.0f;
    const BellModeIndices idx = kBellIndices[modeIdx];
    constexpr float kPi = 3.14159265358979f;
    const float theta = strikePos * (kPi * 0.5f);
    const float meridional = std::abs(std::cos(static_cast<float>(idx.m) * theta));
    // Mild through-thickness taper: higher circular modes couple slightly less
    // to a soundbow strike. Bounded so no partial is fully killed.
    const float taper = 1.0f / (1.0f + 0.12f * static_cast<float>(idx.n));
    return meridional * taper;
}

} // namespace Membrum::Bodies
