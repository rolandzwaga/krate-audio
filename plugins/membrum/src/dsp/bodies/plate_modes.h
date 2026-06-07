#pragma once

// ==============================================================================
// Plate Mode Constants -- Kirchhoff square-plate modal ratios
// ==============================================================================
// Sources:
//   - Leissa, A.W. (1969/1993): "Vibration of Plates", NASA SP-160 / Acoustical
//     Society of America. Simply-supported square-plate eigenvalues:
//       f_{m,n} / f_{1,1} = (m^2 + n^2) / 2
//   - Fletcher & Rossing (1991/1998): "The Physics of Musical Instruments",
//     Chapter 3, Section 3.5 "Rectangular Plates".
//   - Spec 135 "Plate" body reference (Kirchhoff-Love thin-plate theory).
//
// For a simply-supported square plate (a = b) the natural frequencies are:
//   f_{m,n} = (pi/2) * sqrt(D / (rho * h)) * (m^2 + n^2) / a^2
// Normalized to the (1,1) fundamental: ratio = (m^2 + n^2) / 2.
//
// Phase 2 uses 16 modes by default (FR-021). Noise Body extends this up to
// 40 entries (FR-025 / FR-062). The (m,n) / (n,m) degenerate pairs of a square
// plate are kept explicitly: the spec 135 recommendation is to use both with
// slightly decorrelated amplitudes to avoid perfect phase cancellation.
// ==============================================================================

#include <cmath>

namespace Membrum::Bodies {

// Phase 8B: plate mode count 16 -> 48 (6 clean AVX2 kernel iters).
inline constexpr int kPlateModeCount    = 48;
inline constexpr int kPlateMaxModeCount = 48;

struct PlateModeIndices
{
    int m;
    int n;
};

// (m,n) grid-pair indices for the first 40 modes, sorted by (m^2 + n^2).
// Generated from the simply-supported square-plate eigenvalue table.
// Ties (degenerate pairs m != n) are listed as (m,n) then (n,m) so the
// amplitude table can deliberately decorrelate them.
inline constexpr PlateModeIndices kPlateIndices[kPlateMaxModeCount] = {
    {1, 1},  // 2  -> 1.000 (fundamental)
    {1, 2},  // 5  -> 2.500
    {2, 2},  // 8  -> 4.000
    {1, 3},  // 10 -> 5.000
    {2, 3},  // 13 -> 6.500
    {1, 4},  // 17 -> 8.500
    {3, 3},  // 18 -> 9.000
    {2, 4},  // 20 -> 10.000
    {3, 4},  // 25 -> 12.500
    {1, 5},  // 26 -> 13.000
    {2, 5},  // 29 -> 14.500
    {4, 4},  // 32 -> 16.000
    {1, 6},  // 37 -> 18.500
    {3, 5},  // 34 -> 17.000
    {4, 5},  // 41 -> 20.500
    {2, 6},  // 40 -> 20.000
    // --- Modes 17..40 (Noise Body extension) ---
    {5, 5},  // 50 -> 25.000
    {3, 6},  // 45 -> 22.500
    {1, 7},  // 50 -> 25.000
    {4, 6},  // 52 -> 26.000
    {2, 7},  // 53 -> 26.500
    {5, 6},  // 61 -> 30.500
    {3, 7},  // 58 -> 29.000
    {1, 8},  // 65 -> 32.500
    {6, 6},  // 72 -> 36.000
    {4, 7},  // 65 -> 32.500
    {2, 8},  // 68 -> 34.000
    {5, 7},  // 74 -> 37.000
    {3, 8},  // 73 -> 36.500
    {6, 7},  // 85 -> 42.500
    {1, 9},  // 82 -> 41.000
    {4, 8},  // 80 -> 40.000
    {2, 9},  // 85 -> 42.500
    {5, 8},  // 89 -> 44.500
    {7, 7},  // 98 -> 49.000
    {3, 9},  // 90 -> 45.000
    {6, 8},  // 100 -> 50.000
    {1, 10}, // 101 -> 50.500
    {4, 9},  // 97 -> 48.500
    {2, 10}, // 104 -> 52.000
    // --- Phase 8B extension: modes 40..47 ---
    {5, 9},  // 106 -> 53.000
    {3, 10}, // 109 -> 54.500
    {7, 8},  // 113 -> 56.500
    {4, 10}, // 116 -> 58.000
    {6, 9},  // 117 -> 58.500
    {1, 11}, // 122 -> 61.000
    {8, 8},  // 128 -> 64.000
    {7, 9},  // 130 -> 65.000
};

// Mode ratios relative to (1,1) fundamental. Every entry is (m^2+n^2)/2 for
// the (m,n) pair stored at the SAME index in kPlateIndices above — the two
// arrays MUST stay in lock-step, otherwise the resonator bank receives a
// frequency for one mode and a strike-amplitude for another (the consumers in
// plate_mapper.h / noise_body_mapper.h read frequency from kPlateRatios[k] and
// amplitude from kPlateIndices[k]). Indices 8..15 were previously desynced
// (built from a different sort order) and are now regenerated to match.
inline constexpr float kPlateRatios[kPlateMaxModeCount] = {
    1.000f, 2.500f, 4.000f, 5.000f, 6.500f, 8.500f, 9.000f, 10.000f,
    12.500f, 13.000f, 14.500f, 16.000f, 18.500f, 17.000f, 20.500f, 20.000f,
    // 17..40 extended table for Noise Body (FR-062):
    25.000f, 22.500f, 25.000f, 26.000f, 26.500f, 30.500f, 29.000f, 32.500f,
    36.000f, 32.500f, 34.000f, 37.000f, 36.500f, 42.500f, 41.000f, 40.000f,
    42.500f, 44.500f, 49.000f, 45.000f, 50.000f, 50.500f, 48.500f, 52.000f,
    // Phase 8B extension: modes 40..47.
    53.000f, 54.500f, 56.500f, 58.000f, 58.500f, 61.000f, 64.000f, 65.000f
};

/// Strike-position amplitude for rectangular-plate mode (m,n):
///   A_{m,n} = sin(m * pi * x0 / a) * sin(n * pi * y0 / b)
///
/// The single user-facing Strike Position scalar is mapped to a 2-D point that
/// sweeps a diagonal of the unit square (research.md §4.7, the documented
/// "diagonal" mapping):
///   x0/a = 0.5  + 0.3 * (strikePos - 0.5)    -> [0.35, 0.65]
///   y0/b = 0.43 + 0.3 * (strikePos - 0.5)    -> [0.28, 0.58]
///
/// BOTH coordinates move, so every (m,n) mode — including even-n modes — is
/// excited as a function of the knob. The earlier mapping pinned y0 = 0.5,
/// which parked every even-n mode on its horizontal nodal line (sin(n*pi/2)=0)
/// for ALL strike positions, decoupling half the modal palette from the knob
/// (correctness-audit Finding 8). The y axis is centred at 0.43 (off the
/// y=0.5 nodal line) so the default, centred-knob strike excites even-n modes
/// rather than nulling them. The x axis keeps its documented horizontal range,
/// so even-m modes still node at the centred knob (a genuine centre-strike
/// property of the horizontal axis) but respond elsewhere in the sweep.
[[nodiscard]] inline float computePlateAmplitude(int modeIdx,
                                                 float strikePos) noexcept
{
    if (modeIdx < 0 || modeIdx >= kPlateMaxModeCount)
        return 0.0f;
    const PlateModeIndices idx = kPlateIndices[modeIdx];
    constexpr float kPi = 3.14159265358979f;
    const float d  = strikePos - 0.5f;
    const float x0 = 0.5f  + 0.3f * d;  // research.md §4.7 (horizontal sweep)
    const float y0 = 0.43f + 0.3f * d;  // vertical sweep, off the y=0.5 node
    const float fm = static_cast<float>(idx.m);
    const float fn = static_cast<float>(idx.n);
    return std::sin(fm * kPi * x0) * std::sin(fn * kPi * y0);
}

} // namespace Membrum::Bodies
