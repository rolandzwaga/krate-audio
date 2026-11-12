// ==============================================================================
// Oversampling Utilities for Disrumpo Plugin
// ==============================================================================
// Utility functions for intelligent oversampling factor computation.
// Provides morph-weighted factor calculation that combines per-type
// oversampling profiles with morph blend weights.
//
// Layer: Plugin DSP (Disrumpo-specific, NOT shared DSP)
// Namespace: Disrumpo
//
// Reference: specs/009-intelligent-oversampling/spec.md
// ==============================================================================

#pragma once

#include "distortion_types.h"
#include "morph_node.h"

#include <algorithm>
#include <array>

namespace Disrumpo {

/// @brief Round a weighted average to the nearest valid oversampling factor.
///
/// Maps a continuous weighted average to the discrete power-of-2 factor nearest to it
/// (a tie, at a midpoint, takes the higher factor):
/// - (-inf, 1.5) -> 1
/// - [1.5, 3.0) -> 2
/// - [3.0, 6.0) -> 4
/// - [6.0, +inf) -> 8 (the 8x types, user ruling 2026-11-10)
///
/// Nearest, not up (user ruling 2026-11-11, CPU round "Faster math + lower OS"): rounding up
/// ran a Formant/Chaos/Granular/Fractal band (mean 2.5) at 4x and a HardClip/SineFold/
/// FullRectify/RingSaturation band (mean 5) at 8x, over the 15 % morph budget; the user
/// accepted the extra aliasing of such mixes. FR-004 rounded up before.
///
/// @param weightedAverage The weighted average of recommended factors
/// @return Nearest factor: 1, 2, 4 or 8
[[nodiscard]] constexpr int roundToNearestPowerOf2Factor(float weightedAverage) noexcept {
    if (weightedAverage < 1.5f) {
        return 1;
    }
    if (weightedAverage < 3.0f) {
        return 2;
    }
    if (weightedAverage < 6.0f) {
        return 4;
    }
    return 8;
}

/// @brief Get oversampling factor for a single distortion type with global limit.
///
/// Convenience function for non-morph mode: looks up the type's recommended
/// factor and clamps to the global limit.
///
/// @param type The distortion type
/// @param globalLimit Global oversampling limit (1, 2, 4, or 8)
/// @return Effective oversampling factor: 1, 2, 4 or 8 (never exceeds globalLimit)
[[nodiscard]] constexpr int getSingleTypeOversampleFactor(
    DistortionType type,
    int globalLimit
) noexcept {
    const int recommended = getRecommendedOversample(type);
    return std::min(recommended, globalLimit);
}

/// @brief Calculate morph-weighted oversampling factor.
///
/// For a band in morph mode, computes the weighted average of all active nodes'
/// recommended oversampling factors using morph blend weights, then rounds it
/// to the nearest valid power of 2 (1, 2, 4 or 8), and clamps to the global limit.
///
/// Per spec FR-003: uses morph weights as weighting
/// Per spec FR-004 as ruled 2026-11-11: rounds to the nearest power of 2 (was: up)
/// Per spec FR-007, FR-008: clamps to global limit
/// Per spec FR-013: constant time (max 4 iterations)
///
/// @param nodes Array of morph nodes (only first activeNodeCount are read)
/// @param weights Array of morph weights (normalized, sum to ~1.0 for active nodes)
/// @param activeNodeCount Number of active nodes (2-4), clamped internally
/// @param globalLimit Global oversampling limit (1, 2, 4, or 8)
/// @return Effective oversampling factor: 1, 2, 4 or 8 (never exceeds globalLimit)
[[nodiscard]] constexpr int calculateMorphOversampleFactor(
    const std::array<MorphNode, kMaxMorphNodes>& nodes,
    const std::array<float, kMaxMorphNodes>& weights,
    int activeNodeCount,
    int globalLimit
) noexcept {
    // Defensive: clamp active node count
    const int count = std::clamp(activeNodeCount, 0, kMaxMorphNodes);

    if (count == 0) {
        return 1;
    }

    // Compute weighted average of recommended factors
    float weightedSum = 0.0f;
    int largestWeighted = 1;
    for (int i = 0; i < count; ++i) {
        const float w = weights[static_cast<size_t>(i)];
        const int factor = getRecommendedOversample(nodes[static_cast<size_t>(i)].type);
        weightedSum += w * static_cast<float>(factor);
        if (w > 0.0f) {
            largestWeighted = std::max(largestWeighted, factor);
        }
    }

    // Round to the nearest power of 2 and clamp to global limit. A weighted mean never
    // exceeds the largest weighted factor; the float weights sum to 1 only within a few
    // ULPs (under round-up, 2 x (w0 + w1) read 2.0000002 and went to 4: a SoftClip -> Tube
    // sweep switched factors 12 times in 2 s), so the cap stays.
    const int rounded = std::min(roundToNearestPowerOf2Factor(weightedSum), largestWeighted);
    return std::min(rounded, globalLimit);
}

} // namespace Disrumpo
