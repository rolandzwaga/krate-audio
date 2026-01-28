#pragma once

// ==============================================================================
// Band State Structure
// ==============================================================================
// Per-band configuration and state for the Disrumpo multiband processor.
// Real-time safe: fixed-size, no allocations.
//
// References:
// - specs/002-band-management/data-model.md
// - specs/002-band-management/spec.md FR-015 to FR-018
// - specs/005-morph-system/spec.md FR-002 (MorphNode array)
// ==============================================================================

#include <array>
#include <cstdint>
#include "distortion_types.h"
#include "morph_node.h"

namespace Disrumpo {

/// @brief State for a single frequency band.
/// Real-time safe: fixed-size, no allocations.
/// Per spec.md FR-015 to FR-018.
struct BandState {
    // Frequency bounds (informational, set by CrossoverNetwork)
    float lowFreqHz = 20.0f;     ///< Lower frequency bound (Hz)
    float highFreqHz = 20000.0f; ///< Upper frequency bound (Hz)

    // Output controls
    float gainDb = 0.0f;         ///< Band gain [-24, +24] dB (FR-019)
    float pan = 0.0f;            ///< Stereo pan [-1, +1] (FR-021)

    // Control flags
    bool solo = false;           ///< Solo flag (FR-025)
    bool bypass = false;         ///< Bypass flag (FR-024, for future distortion)
    bool mute = false;           ///< Mute flag (FR-023)

    // Morph fields (FR-018, 005-morph-system spec)
    float morphX = 0.5f;         ///< Morph X position [0, 1]
    float morphY = 0.5f;         ///< Morph Y position [0, 1]
    MorphMode morphMode = MorphMode::Linear1D;  ///< Current morph mode (FR-003, FR-004, FR-005)
    int activeNodeCount = 2;     ///< Number of active nodes (2-4) (FR-002)

    /// @brief Array of morph nodes for this band.
    /// Per spec FR-002: Support 2 to 4 active morph nodes per band.
    /// Fixed-size array for real-time safety (no allocations).
    std::array<MorphNode, kMaxMorphNodes> nodes = {{
        MorphNode(0, 0.0f, 0.0f, DistortionType::SoftClip),   // Node A at top-left
        MorphNode(1, 1.0f, 0.0f, DistortionType::Tube),       // Node B at top-right
        MorphNode(2, 0.0f, 1.0f, DistortionType::Fuzz),       // Node C at bottom-left
        MorphNode(3, 1.0f, 1.0f, DistortionType::SineFold)    // Node D at bottom-right
    }};
};

// =============================================================================
// Constants per dsp-details.md
// =============================================================================

inline constexpr int kMinBands = 1;
inline constexpr int kMaxBands = 8;
inline constexpr int kDefaultBands = 4;

inline constexpr float kMinBandGainDb = -24.0f;
inline constexpr float kMaxBandGainDb = +24.0f;

inline constexpr float kMinCrossoverHz = 20.0f;
inline constexpr float kMaxCrossoverHz = 20000.0f;
inline constexpr float kMinCrossoverSpacingOctaves = 0.5f;

/// @brief Default smoothing time for band parameter transitions (FR-027a)
inline constexpr float kDefaultSmoothingMs = 10.0f;

} // namespace Disrumpo
