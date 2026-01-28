// ==============================================================================
// MorphNode Header Contract - 005-morph-system
// ==============================================================================
// Contract header defining MorphNode structure for morph system.
// This is a specification file - actual implementation will mirror this.
//
// Location: plugins/disrumpo/src/dsp/morph_node.h
// ==============================================================================

#pragma once

#include "distortion_types.h"
#include "distortion_adapter.h"
#include <array>
#include <cstdint>

namespace Disrumpo {

// =============================================================================
// MorphNode Structure (FR-002)
// =============================================================================

/// @brief A single morph node containing distortion type, parameters, and position.
///
/// Up to 4 nodes per band define the morph space. Each node represents a
/// distortion configuration that can be blended with others based on
/// cursor position.
///
/// Real-time safe: fixed-size, no allocations.
///
/// @par Memory Layout
/// - id: 4 bytes
/// - type: 1 byte (enum)
/// - padding: 3 bytes
/// - commonParams: 12 bytes (3 floats)
/// - typeParams: ~140 bytes (varies)
/// - posX, posY: 8 bytes (2 floats)
/// Total: ~168 bytes per node
struct MorphNode {
    /// @brief Unique identifier within band (0-3).
    int id = 0;

    /// @brief Distortion type for this node.
    DistortionType type = DistortionType::SoftClip;

    /// @brief Common distortion parameters (drive, mix, tone).
    DistortionCommonParams commonParams;

    /// @brief Type-specific distortion parameters.
    DistortionParams typeParams;

    /// @brief X position in morph space [0, 1].
    float posX = 0.0f;

    /// @brief Y position in morph space [0, 1].
    float posY = 0.0f;

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// @brief Check if this node is active (has meaningful contribution).
    ///
    /// A node is considered active if it has non-trivial drive or mix.
    /// Inactive nodes are skipped during weight computation.
    ///
    /// @return true if node has significant effect
    [[nodiscard]] bool isActive() const noexcept {
        return commonParams.drive > 0.0001f || commonParams.mix > 0.0001f;
    }

    /// @brief Get the family for this node's distortion type.
    ///
    /// Family determines interpolation strategy:
    /// - Same-family nodes can use parameter interpolation
    /// - Cross-family nodes require parallel processing
    ///
    /// @return DistortionFamily enum value
    [[nodiscard]] DistortionFamily getFamily() const noexcept {
        return Disrumpo::getFamily(type);
    }

    // =========================================================================
    // Comparison
    // =========================================================================

    /// @brief Check if two nodes have the same distortion configuration.
    ///
    /// Position is NOT compared - only type and parameters.
    ///
    /// @param other Node to compare with
    /// @return true if type and all parameters match
    [[nodiscard]] bool hasSameConfig(const MorphNode& other) const noexcept {
        return type == other.type &&
               commonParams.drive == other.commonParams.drive &&
               commonParams.mix == other.commonParams.mix &&
               commonParams.toneHz == other.commonParams.toneHz;
        // Note: Full typeParams comparison omitted for brevity
    }
};

// =============================================================================
// Constants
// =============================================================================

/// @brief Maximum number of morph nodes per band.
inline constexpr int kMaxMorphNodes = 4;

/// @brief Minimum number of morph nodes (A and B).
inline constexpr int kMinMorphNodes = 2;

// =============================================================================
// Default Node Positions
// =============================================================================

/// @brief Default node positions for 2-node configuration.
///
/// Nodes placed on horizontal axis for 1D linear morphing.
///
/// Layout:
/// ```
///   A (0.0, 0.5) -------- B (1.0, 0.5)
/// ```
inline constexpr float kDefaultNode2Positions[2][2] = {
    {0.0f, 0.5f},   // Node A: left center
    {1.0f, 0.5f}    // Node B: right center
};

/// @brief Default node positions for 3-node configuration.
///
/// Nodes placed in triangle for 2D morphing.
///
/// Layout:
/// ```
///        A (0.5, 0.0)
///       /          \
///      /            \
///   B (0.0, 1.0)--C (1.0, 1.0)
/// ```
inline constexpr float kDefaultNode3Positions[3][2] = {
    {0.5f, 0.0f},   // Node A: top center
    {0.0f, 1.0f},   // Node B: bottom left
    {1.0f, 1.0f}    // Node C: bottom right
};

/// @brief Default node positions for 4-node configuration.
///
/// Nodes placed at corners for full 2D morphing.
///
/// Layout:
/// ```
///   A (0.0, 0.0)--B (1.0, 0.0)
///        |              |
///        |              |
///   C (0.0, 1.0)--D (1.0, 1.0)
/// ```
inline constexpr float kDefaultNode4Positions[4][2] = {
    {0.0f, 0.0f},   // Node A: top-left
    {1.0f, 0.0f},   // Node B: top-right
    {0.0f, 1.0f},   // Node C: bottom-left
    {1.0f, 1.0f}    // Node D: bottom-right
};

// =============================================================================
// Utility Functions
// =============================================================================

/// @brief Get default position for a node in a given configuration.
///
/// @param nodeIndex Index of node (0-3)
/// @param totalNodes Total nodes in configuration (2-4)
/// @param outX Output X position [0, 1]
/// @param outY Output Y position [0, 1]
inline void getDefaultNodePosition(int nodeIndex, int totalNodes,
                                   float& outX, float& outY) noexcept {
    if (nodeIndex < 0 || nodeIndex >= totalNodes) {
        outX = 0.5f;
        outY = 0.5f;
        return;
    }

    switch (totalNodes) {
        case 2:
            outX = kDefaultNode2Positions[nodeIndex][0];
            outY = kDefaultNode2Positions[nodeIndex][1];
            break;
        case 3:
            outX = kDefaultNode3Positions[nodeIndex][0];
            outY = kDefaultNode3Positions[nodeIndex][1];
            break;
        case 4:
        default:
            outX = kDefaultNode4Positions[nodeIndex][0];
            outY = kDefaultNode4Positions[nodeIndex][1];
            break;
    }
}

/// @brief Initialize a MorphNode with default values for a given index.
///
/// @param node Node to initialize
/// @param index Node index (0-3)
/// @param totalNodes Total nodes in configuration (2-4)
inline void initializeNode(MorphNode& node, int index, int totalNodes) noexcept {
    node.id = index;
    node.type = DistortionType::SoftClip;
    node.commonParams = DistortionCommonParams{};
    node.typeParams = DistortionParams{};
    getDefaultNodePosition(index, totalNodes, node.posX, node.posY);
}

} // namespace Disrumpo
