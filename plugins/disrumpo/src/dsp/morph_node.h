// ==============================================================================
// MorphNode Data Structure for Disrumpo Plugin
// ==============================================================================
// A single point in the morph space containing distortion type, parameters,
// and XY position. Up to 4 nodes per band.
//
// Real-time safe: fixed-size, no allocations.
//
// Reference: specs/005-morph-system/spec.md FR-002, dsp-details.md Section 7.1
// ==============================================================================

#pragma once

#include "distortion_types.h"
#include "distortion_adapter.h"

namespace Disrumpo {

/// @brief A single morph node representing a distortion configuration at a 2D position.
///
/// Each frequency band contains 2-4 MorphNodes arranged in a morph space.
/// The morph cursor position relative to these nodes determines their weights.
///
/// @note Real-time safe: fixed-size struct with no allocations.
/// @note Per spec FR-002: Support 2 to 4 active morph nodes per band.
struct MorphNode {
    /// @brief Unique identifier for this node within the band (0-3).
    int id = 0;

    /// @brief The distortion type assigned to this node.
    DistortionType type = DistortionType::SoftClip;

    /// @brief Type-specific parameters for this node's distortion.
    DistortionParams params;

    /// @brief Common parameters (drive, mix, tone) for this node.
    DistortionCommonParams commonParams;

    /// @brief X position in morph space [0, 1].
    /// For 1D Linear mode: position along the single axis.
    /// For 2D modes: horizontal position.
    float posX = 0.0f;

    /// @brief Y position in morph space [0, 1].
    /// For 1D Linear mode: typically 0 (unused).
    /// For 2D modes: vertical position.
    float posY = 0.0f;

    /// @brief Default constructor with safe defaults.
    constexpr MorphNode() noexcept = default;

    /// @brief Construct with explicit id and position.
    /// @param nodeId Node identifier (0-3)
    /// @param x X position in morph space [0, 1]
    /// @param y Y position in morph space [0, 1]
    constexpr MorphNode(int nodeId, float x, float y) noexcept
        : id(nodeId)
        , type(DistortionType::SoftClip)
        , params()
        , commonParams()
        , posX(x)
        , posY(y) {}

    /// @brief Construct with id, position, and distortion type.
    /// @param nodeId Node identifier (0-3)
    /// @param x X position in morph space [0, 1]
    /// @param y Y position in morph space [0, 1]
    /// @param distType The distortion type for this node
    constexpr MorphNode(int nodeId, float x, float y, DistortionType distType) noexcept
        : id(nodeId)
        , type(distType)
        , params()
        , commonParams()
        , posX(x)
        , posY(y) {}
};

/// @brief Maximum number of morph nodes per band.
/// Per spec FR-002: Support 2 to 4 active morph nodes per band.
inline constexpr int kMaxMorphNodes = 4;

/// @brief Minimum number of active morph nodes per band.
inline constexpr int kMinActiveNodes = 2;

/// @brief Default number of active morph nodes (A-B morphing).
inline constexpr int kDefaultActiveNodes = 2;

/// @brief Weight threshold below which a node is skipped (FR-015).
/// Nodes with weight below this threshold are not processed to save CPU.
inline constexpr float kWeightThreshold = 0.001f;

/// @brief Maximum allowed weight threshold when enforcing global processor cap (FR-019).
inline constexpr float kMaxWeightThreshold = 0.25f;

/// @brief Weight threshold increment when enforcing global processor cap (FR-019).
inline constexpr float kWeightThresholdIncrement = 0.005f;

/// @brief Maximum total active distortion processors across all bands (FR-019).
inline constexpr int kMaxGlobalProcessors = 16;

} // namespace Disrumpo
