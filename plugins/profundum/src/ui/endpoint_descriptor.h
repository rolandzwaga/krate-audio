#pragma once

// ==============================================================================
// Profundum - endpoint descriptor words (Phase 12 T014; FR-048; plan 3.1 "Endpoint descriptor table", 4.11;
// design/advanced-page.md section 7)
// ==============================================================================
// Three words, one per axis (Shape, Colour, Force), computed from an endpoint's MorphState. Pure functions of
// the endpoint: never stored, never in a message, never supplied by a preset.
//
//   Shape  s = 0.5 (1 - recipe.depth) + 0.5 recipe.body                     Round / Full / Dense
//   Colour s = 0.5 recipe.edge + 0.25 (recipe.shift + 1)                    Dark / Warm / Bright
//   Force  s = max(foldAmount / 100, characterAmount, excitation.mix * drive) Clean / Driven / Aggressive
//
// Bands: s < 0.33 first word, s < 0.66 second, otherwise third. Each threshold belongs to the upper band.
//
// Fold term: D2 7.2 requires foldAmount / 100 (correctly rounded, so 66 / 100 == 0.66f), but the plugin builds
// with /fp:fast / -ffast-math, whose reciprocal math may turn the division into foldAmount * 0.01f
// (66 * 0.01f = 0.65999997f < 0.66f, which would read Driven for case 5). The band of a max is the max of the
// bands (the banding is monotone), so the fold term is banded against the exact thresholds 33 and 66 instead;
// the static_asserts below prove (in constant evaluation, strict IEEE) that these are the D2 thresholds x 100.
// ==============================================================================

#include <krate/dsp/systems/morph_state.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace Profundum::UI {

inline constexpr float kDescriptorLowerThreshold = 0.33f;
inline constexpr float kDescriptorUpperThreshold = 0.66f;

/// foldAmount ([0, 100]) values whose foldAmount / 100 equals the thresholds above.
inline constexpr float kDescriptorFoldLowerThreshold = 33.0f;
inline constexpr float kDescriptorFoldUpperThreshold = 66.0f;
// Compared as bit patterns: under MSVC /fp:fast a constant-evaluated float == of two bit-identical values
// (0x3EA8F5C3, 0x3F28F5C3) returns false, so the plain float comparison cannot carry this proof.
static_assert(std::bit_cast<std::uint32_t>(kDescriptorFoldLowerThreshold / 100.0f) ==
              std::bit_cast<std::uint32_t>(kDescriptorLowerThreshold));
static_assert(std::bit_cast<std::uint32_t>(kDescriptorFoldUpperThreshold / 100.0f) ==
              std::bit_cast<std::uint32_t>(kDescriptorUpperThreshold));

inline constexpr std::array<const char*, 3> kShapeWords{"Round", "Full", "Dense"};
inline constexpr std::array<const char*, 3> kColourWords{"Dark", "Warm", "Bright"};
inline constexpr std::array<const char*, 3> kForceWords{"Clean", "Driven", "Aggressive"};

namespace detail {

/// 0, 1 or 2: s < lower, s < upper, otherwise. A NaN score lands in band 2 (the comparisons are false);
/// a valid MorphState never produces one.
[[nodiscard]] constexpr int descriptorBand(float s, float lower, float upper) noexcept {
    if (s < lower) {
        return 0;
    }
    if (s < upper) {
        return 1;
    }
    return 2;
}

}  // namespace detail

/// D2 table (design/advanced-page.md 7.2). Word order: Shape, Colour, Force.
[[nodiscard]] inline std::array<const char*, 3> describeEndpoint(const Krate::DSP::MorphState& s) noexcept {
    const float shape = 0.5f * (1.0f - s.recipe.depth) + 0.5f * s.recipe.body;
    const float colour = 0.5f * s.recipe.edge + 0.25f * (s.recipe.shift + 1.0f);

    const int foldBand =
        detail::descriptorBand(s.foldAmount, kDescriptorFoldLowerThreshold, kDescriptorFoldUpperThreshold);
    const int otherBand = detail::descriptorBand(std::max(s.characterAmount, s.excitation.mix * s.excitation.drive),
                                                 kDescriptorLowerThreshold, kDescriptorUpperThreshold);
    const int forceBand = std::max(foldBand, otherBand);

    return {kShapeWords[static_cast<std::size_t>(
                detail::descriptorBand(shape, kDescriptorLowerThreshold, kDescriptorUpperThreshold))],
            kColourWords[static_cast<std::size_t>(
                detail::descriptorBand(colour, kDescriptorLowerThreshold, kDescriptorUpperThreshold))],
            kForceWords[static_cast<std::size_t>(forceBand)]};
}

}  // namespace Profundum::UI
