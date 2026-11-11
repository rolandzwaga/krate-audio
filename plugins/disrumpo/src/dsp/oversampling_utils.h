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

/// @brief Round a weighted average up to the nearest valid oversampling factor.
///
/// Maps a continuous weighted average to a discrete power-of-2 factor:
/// - (-inf, 1.0] -> 1
/// - (1.0, 2.0] -> 2
/// - (2.0, 4.0] -> 4
/// - (4.0, +inf) -> 8 (the 8x types, user ruling 2026-11-10)
///
/// Per spec FR-004: rounds UP to ensure quality is never compromised.
///
/// @param weightedAverage The weighted average of recommended factors
/// @return Rounded-up factor: 1, 2, 4 or 8
[[nodiscard]] constexpr int roundUpToPowerOf2Factor(float weightedAverage) noexcept {
    if (weightedAverage <= 1.0f) {
        return 1;
    }
    if (weightedAverage <= 2.0f) {
        return 2;
    }
    if (weightedAverage <= 4.0f) {
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
/// recommended oversampling factors using morph blend weights, then rounds up
/// to the nearest valid power of 2 (1, 2, 4 or 8), and clamps to the global limit.
///
/// Per spec FR-003: uses morph weights as weighting
/// Per spec FR-004: rounds up to nearest power of 2
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

    // Round up to nearest power of 2 and clamp to global limit. A weighted mean never
    // exceeds the largest weighted factor; the float weights sum to 1 only within a few
    // ULPs, so without the cap 2 x (w0 + w1) can read 2.0000002 and round up to 4
    // (a SoftClip -> Tube sweep switched factors 12 times in 2 s).
    const int rounded = std::min(roundUpToPowerOf2Factor(weightedSum), largestWeighted);
    return std::min(rounded, globalLimit);
}

} // namespace Disrumpo
