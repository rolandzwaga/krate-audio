// ==============================================================================
// Layer 2: Processors
// markov_matrices.h — Hardcoded 7x7 transition matrices for Gradus Markov mode
// ==============================================================================
// Provides 5 preset transition matrices indexed by scale degree
// (I, ii, iii, IV, V, vi, vii°). Row-stochastic (each row sums to 1.0) so the
// normalized sampler consistently reproduces the intended bias.
//
// Constitution Principle III: Modern C++ Standards — constexpr, std::array.
// Constitution Principle IX: Layered DSP Architecture — Layer 2, depends on
// Layer 1 (held_note_buffer.h for kMarkovMatrixSize).
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/held_note_buffer.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

/// @brief Named presets for the Markov transition matrix.
enum class MarkovPreset : uint8_t {
    Uniform = 0,    ///< Flat baseline — every transition equally likely
    Jazz,           ///< ii–V–I voice leading bias
    Minimal,        ///< Strong self-loops + ±1 step motion
    Ambient,        ///< Favors wide jumps (3+ degree distance)
    Classical,      ///< I–IV–V–I circle-of-fifths bias
    Custom          ///< Sentinel — values come from user-edited cell params
};

inline constexpr int kNumMarkovPresets = 6;  // Uniform..Custom

namespace detail {

/// Uniform matrix: every cell = 1/7. Equivalent to Random arp mode.
inline constexpr std::array<float, kMarkovMatrixSize> kMarkovUniform = {{
    // I      ii     iii    IV     V      vi     vii°
    1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7,   // from I
    1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7,   // from ii
    1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7,   // from iii
    1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7,   // from IV
    1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7,   // from V
    1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7,   // from vi
    1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7, 1.f/7,   // from vii°
}};

/// Jazz matrix: biased toward ii→V→I voice leading plus common turnarounds.
/// - I (0): favors IV (3) and vi (5); some ii (1) and V (4).
/// - ii (1): strongly favors V (4).
/// - iii (2): resolves to vi (5) or IV (3).
/// - IV (3): favors V (4) or back to I (0).
/// - V (4): strongly resolves to I (0).
/// - vi (5): favors ii (1) (common ii-V-I setup).
/// - vii° (6): resolves to I (0).
inline constexpr std::array<float, kMarkovMatrixSize> kMarkovJazz = {{
    // I      ii     iii    IV     V      vi     vii°
    0.10f, 0.20f, 0.05f, 0.25f, 0.15f, 0.20f, 0.05f,   // from I   -> 1.00
    0.05f, 0.05f, 0.05f, 0.10f, 0.60f, 0.10f, 0.05f,   // from ii  -> 1.00
    0.05f, 0.10f, 0.05f, 0.30f, 0.10f, 0.35f, 0.05f,   // from iii -> 1.00
    0.20f, 0.05f, 0.05f, 0.05f, 0.50f, 0.10f, 0.05f,   // from IV  -> 1.00
    0.55f, 0.05f, 0.10f, 0.10f, 0.05f, 0.10f, 0.05f,   // from V   -> 1.00
    0.10f, 0.45f, 0.10f, 0.15f, 0.10f, 0.05f, 0.05f,   // from vi  -> 1.00
    0.55f, 0.05f, 0.10f, 0.10f, 0.10f, 0.05f, 0.05f,   // from vii°-> 1.00
}};

/// Minimal matrix: strong self-loops (stays on the current degree) plus ±1
/// neighbor motion. Creates static, meditative patterns.
inline constexpr std::array<float, kMarkovMatrixSize> kMarkovMinimal = {{
    // I      ii     iii    IV     V      vi     vii°
    0.60f, 0.30f, 0.02f, 0.02f, 0.02f, 0.02f, 0.02f,   // from I
    0.25f, 0.50f, 0.20f, 0.01f, 0.01f, 0.01f, 0.02f,   // from ii
    0.01f, 0.20f, 0.55f, 0.20f, 0.01f, 0.02f, 0.01f,   // from iii
    0.01f, 0.01f, 0.20f, 0.55f, 0.20f, 0.02f, 0.01f,   // from IV
    0.01f, 0.01f, 0.01f, 0.20f, 0.55f, 0.20f, 0.02f,   // from V
    0.02f, 0.01f, 0.01f, 0.01f, 0.20f, 0.55f, 0.20f,   // from vi
    0.05f, 0.02f, 0.02f, 0.02f, 0.02f, 0.25f, 0.62f,   // from vii°
}};

/// Ambient matrix: favors wide jumps (3–5 degrees distant), creating
/// non-stepwise, spacious motion.
inline constexpr std::array<float, kMarkovMatrixSize> kMarkovAmbient = {{
    // I      ii     iii    IV     V      vi     vii°
    0.05f, 0.05f, 0.15f, 0.30f, 0.25f, 0.15f, 0.05f,   // from I    -> 1.00
    0.15f, 0.05f, 0.05f, 0.10f, 0.25f, 0.25f, 0.15f,   // from ii   -> 1.00
    0.25f, 0.15f, 0.05f, 0.05f, 0.10f, 0.15f, 0.25f,   // from iii  -> 1.00
    0.30f, 0.20f, 0.15f, 0.05f, 0.05f, 0.10f, 0.15f,   // from IV   -> 1.00
    0.25f, 0.20f, 0.20f, 0.10f, 0.05f, 0.10f, 0.10f,   // from V    -> 1.00
    0.15f, 0.25f, 0.25f, 0.15f, 0.10f, 0.05f, 0.05f,   // from vi   -> 1.00
    0.05f, 0.15f, 0.25f, 0.25f, 0.15f, 0.10f, 0.05f,   // from vii° -> 1.00
}};

/// Classical matrix: I–IV–V–I circle-of-fifths progression bias.
/// - I (0): heavily favors IV (3) and V (4).
/// - IV (3): favors V (4).
/// - V (4): resolves strongly to I (0).
/// - Other degrees: moderate flow toward the I/IV/V axis.
inline constexpr std::array<float, kMarkovMatrixSize> kMarkovClassical = {{
    // I      ii     iii    IV     V      vi     vii°
    0.05f, 0.10f, 0.05f, 0.35f, 0.30f, 0.10f, 0.05f,   // from I    -> 1.00
    0.10f, 0.05f, 0.05f, 0.15f, 0.55f, 0.05f, 0.05f,   // from ii   -> 1.00
    0.10f, 0.05f, 0.05f, 0.35f, 0.10f, 0.30f, 0.05f,   // from iii  -> 1.00
    0.15f, 0.05f, 0.05f, 0.05f, 0.60f, 0.05f, 0.05f,   // from IV   -> 1.00
    0.60f, 0.05f, 0.05f, 0.10f, 0.05f, 0.10f, 0.05f,   // from V    -> 1.00
    0.10f, 0.15f, 0.05f, 0.30f, 0.25f, 0.05f, 0.10f,   // from vi   -> 1.00
    0.55f, 0.05f, 0.10f, 0.10f, 0.10f, 0.05f, 0.05f,   // from vii° -> 1.00
}};

} // namespace detail

/// Get the hardcoded preset matrix for a given MarkovPreset.
/// Returns Uniform for Custom (sentinel) — callers should treat Custom as
/// a UI-only signal and read matrix values from their own storage.
[[nodiscard]] inline constexpr const std::array<float, kMarkovMatrixSize>&
getMarkovPresetMatrix(MarkovPreset preset) noexcept {
    switch (preset) {
        case MarkovPreset::Uniform:   return detail::kMarkovUniform;
        case MarkovPreset::Jazz:      return detail::kMarkovJazz;
        case MarkovPreset::Minimal:   return detail::kMarkovMinimal;
        case MarkovPreset::Ambient:   return detail::kMarkovAmbient;
        case MarkovPreset::Classical: return detail::kMarkovClassical;
        case MarkovPreset::Custom:    return detail::kMarkovUniform;
    }
    return detail::kMarkovUniform;
}

} // namespace Krate::DSP
