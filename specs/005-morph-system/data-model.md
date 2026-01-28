# Data Model: MorphEngine DSP System

**Feature**: 005-morph-system | **Date**: 2026-01-28

---

## Overview

This document defines the data structures and relationships for the MorphEngine system.

---

## 1. New Types

### 1.1 MorphMode (Enumeration)

Defines how morph cursor position maps to node weights.

```cpp
// Location: plugins/disrumpo/src/dsp/morph_types.h

namespace Disrumpo {

/// @brief Morph interpolation mode.
/// Determines how cursor position maps to node weights.
enum class MorphMode : uint8_t {
    Linear1D = 0,   ///< Single axis A-B-C-D using morphX only
    Planar2D,       ///< XY position with inverse distance weighting
    Radial2D        ///< Angle + distance from center
};

constexpr int kMorphModeCount = 3;

/// @brief Get display name for a morph mode.
constexpr const char* getMorphModeName(MorphMode mode) noexcept {
    switch (mode) {
        case MorphMode::Linear1D: return "1D Linear";
        case MorphMode::Planar2D: return "2D Planar";
        case MorphMode::Radial2D: return "2D Radial";
        default: return "Unknown";
    }
}

} // namespace Disrumpo
```

### 1.2 DistortionFamily (Enumeration)

Groups distortion types by interpolation compatibility.

```cpp
// Location: plugins/disrumpo/src/dsp/distortion_types.h (ADD to existing file)

/// @brief Distortion family for interpolation strategy.
///
/// Families group distortion types by their interpolation compatibility:
/// - Same-family morphs use parameter interpolation (efficient, single processor)
/// - Cross-family morphs use parallel processing with output crossfade
enum class DistortionFamily : uint8_t {
    Saturation = 0,     ///< D01-D06: Transfer function interpolation
    Wavefold,           ///< D07-D09: Parameter interpolation
    Digital,            ///< D12-D14, D18-D19: Parameter interpolation
    Rectify,            ///< D10-D11: Parameter interpolation
    Dynamic,            ///< D15, D17: Parameter interpolation + envelope coupling
    Hybrid,             ///< D16, D26: Parallel blend with output crossfade
    Experimental        ///< D20-D25: Parallel blend with output crossfade
};

/// @brief Get the family for a distortion type.
///
/// Maps each of the 26 distortion types to its interpolation family.
/// Used by MorphEngine to determine same-family vs cross-family processing.
///
/// @param type The distortion type
/// @return Family enum value
constexpr DistortionFamily getFamily(DistortionType type) noexcept {
    switch (type) {
        // Saturation (D01-D06) - transfer function interpolation
        case DistortionType::SoftClip:
        case DistortionType::HardClip:
        case DistortionType::Tube:
        case DistortionType::Tape:
        case DistortionType::Fuzz:
        case DistortionType::AsymmetricFuzz:
            return DistortionFamily::Saturation;

        // Wavefold (D07-D09) - parameter interpolation
        case DistortionType::SineFold:
        case DistortionType::TriangleFold:
        case DistortionType::SergeFold:
            return DistortionFamily::Wavefold;

        // Digital (D12-D14, D18-D19) - parameter interpolation
        case DistortionType::Bitcrush:
        case DistortionType::SampleReduce:
        case DistortionType::Quantize:
        case DistortionType::Aliasing:
        case DistortionType::BitwiseMangler:
            return DistortionFamily::Digital;

        // Rectify (D10-D11) - parameter interpolation
        case DistortionType::FullRectify:
        case DistortionType::HalfRectify:
            return DistortionFamily::Rectify;

        // Dynamic (D15) + FeedbackDist (D17) - parameter + envelope coupling
        case DistortionType::Temporal:
        case DistortionType::FeedbackDist:
            return DistortionFamily::Dynamic;

        // Hybrid (D16, D26) - parallel blend with output crossfade
        case DistortionType::RingSaturation:
        case DistortionType::AllpassResonant:
            return DistortionFamily::Hybrid;

        // Experimental (D20-D25) - parallel blend with output crossfade
        case DistortionType::Chaos:
        case DistortionType::Formant:
        case DistortionType::Granular:
        case DistortionType::Spectral:
        case DistortionType::Fractal:
        case DistortionType::Stochastic:
            return DistortionFamily::Experimental;

        default:
            return DistortionFamily::Saturation;
    }
}

/// @brief Check if two families are the same (can use parameter interpolation).
constexpr bool isSameFamily(DistortionType a, DistortionType b) noexcept {
    return getFamily(a) == getFamily(b);
}

/// @brief Check if family requires parallel processing for interpolation.
constexpr bool requiresParallelProcessing(DistortionFamily family) noexcept {
    return family == DistortionFamily::Hybrid ||
           family == DistortionFamily::Experimental;
}
```

### 1.3 MorphNode (Structure)

Represents a single point in the morph space.

```cpp
// Location: plugins/disrumpo/src/dsp/morph_node.h

#pragma once

#include "distortion_types.h"
#include "distortion_adapter.h"
#include <cstdint>

namespace Disrumpo {

/// @brief A single morph node containing distortion type, parameters, and position.
///
/// Up to 4 nodes per band define the morph space. Each node represents a
/// distortion configuration that can be blended with others based on
/// cursor position.
///
/// Real-time safe: fixed-size, no allocations.
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

    /// @brief Check if this node is active (has meaningful contribution).
    [[nodiscard]] bool isActive() const noexcept {
        return commonParams.drive > 0.0001f || commonParams.mix > 0.0001f;
    }

    /// @brief Get the family for this node's distortion type.
    [[nodiscard]] DistortionFamily getFamily() const noexcept {
        return Disrumpo::getFamily(type);
    }
};

/// @brief Maximum number of morph nodes per band.
inline constexpr int kMaxMorphNodes = 4;

/// @brief Minimum number of morph nodes (A and B).
inline constexpr int kMinMorphNodes = 2;

/// @brief Default node positions for 2-node configuration.
inline constexpr float kDefaultNode2Positions[2][2] = {
    {0.0f, 0.5f},   // Node A: left
    {1.0f, 0.5f}    // Node B: right
};

/// @brief Default node positions for 4-node configuration.
inline constexpr float kDefaultNode4Positions[4][2] = {
    {0.0f, 0.0f},   // Node A: top-left
    {1.0f, 0.0f},   // Node B: top-right
    {0.0f, 1.0f},   // Node C: bottom-left
    {1.0f, 1.0f}    // Node D: bottom-right
};

} // namespace Disrumpo
```

### 1.4 MorphDriverType (Enumeration)

Defines the source controlling morph position.

```cpp
// Location: plugins/disrumpo/src/dsp/morph_types.h

/// @brief Source that controls morph position automatically.
///
/// Advanced drivers enable creative sound design possibilities:
/// - Chaos: Lorenz/Rossler attractor creates organic movement
/// - Envelope: Input loudness drives morph position
/// - Pitch: Detected pitch maps to morph position
/// - Transient: Attack detection triggers morph jumps
enum class MorphDriverType : uint8_t {
    Manual = 0,     ///< Direct user control via XY pad/automation
    LFO,            ///< Standard oscillator (provided by modulation system)
    Chaos,          ///< Lorenz/Rossler attractor
    Envelope,       ///< Input loudness
    Pitch,          ///< Detected pitch mapping
    Transient       ///< Attack detection triggers
};

constexpr int kMorphDriverCount = 6;

/// @brief Get display name for a morph driver.
constexpr const char* getMorphDriverName(MorphDriverType driver) noexcept {
    switch (driver) {
        case MorphDriverType::Manual:    return "Manual";
        case MorphDriverType::LFO:       return "LFO";
        case MorphDriverType::Chaos:     return "Chaos";
        case MorphDriverType::Envelope:  return "Envelope";
        case MorphDriverType::Pitch:     return "Pitch";
        case MorphDriverType::Transient: return "Transient";
        default: return "Unknown";
    }
}
```

---

## 2. Extended Types

### 2.1 BandState Extension

Add MorphNode array to existing BandState.

```cpp
// Location: plugins/disrumpo/src/dsp/band_state.h (MODIFY existing file)

#pragma once

#include "morph_node.h"
#include "morph_types.h"
#include <array>
#include <cstdint>

namespace Disrumpo {

/// @brief State for a single frequency band.
/// Real-time safe: fixed-size, no allocations.
struct BandState {
    // === Frequency bounds ===
    float lowFreqHz = 20.0f;
    float highFreqHz = 20000.0f;

    // === Output controls ===
    float gainDb = 0.0f;
    float pan = 0.0f;

    // === Control flags ===
    bool solo = false;
    bool bypass = false;
    bool mute = false;

    // === Morph configuration (FR-002 to FR-005) ===
    std::array<MorphNode, kMaxMorphNodes> nodes;  ///< Up to 4 morph nodes
    int activeNodeCount = 2;                       ///< How many nodes active (2-4)
    MorphMode morphMode = MorphMode::Linear1D;     ///< Morph interpolation mode
    float morphX = 0.5f;                           ///< Morph cursor X [0, 1]
    float morphY = 0.5f;                           ///< Morph cursor Y [0, 1]

    // === Morph driver configuration (FR-017) ===
    MorphDriverType driverType = MorphDriverType::Manual;
    float morphSmoothingMs = 50.0f;                ///< Smoothing time [0, 500] ms

    /// @brief Initialize nodes to default positions.
    void initializeNodes() noexcept {
        // Default: 2 nodes (A and B) on horizontal axis
        activeNodeCount = 2;

        for (int i = 0; i < kMaxMorphNodes; ++i) {
            nodes[i].id = i;
            nodes[i].type = DistortionType::SoftClip;
            nodes[i].commonParams = DistortionCommonParams{};
            nodes[i].typeParams = DistortionParams{};

            if (i < 2) {
                // Use 2-node default positions
                nodes[i].posX = kDefaultNode2Positions[i][0];
                nodes[i].posY = kDefaultNode2Positions[i][1];
            } else {
                // Use 4-node positions for inactive nodes
                nodes[i].posX = kDefaultNode4Positions[i][0];
                nodes[i].posY = kDefaultNode4Positions[i][1];
            }
        }
    }
};

// ... existing constants ...

} // namespace Disrumpo
```

---

## 3. Computed Types

### 3.1 MorphWeights

Computed weights for each node based on cursor position.

```cpp
// Location: plugins/disrumpo/src/dsp/morph_engine.h (part of MorphEngine)

/// @brief Computed morph weights for up to 4 nodes.
///
/// Weights are normalized to sum to 1.0 (or 0.0 if all nodes inactive).
/// Nodes with weight below threshold (0.001) are zeroed.
struct MorphWeights {
    std::array<float, kMaxMorphNodes> values = {0.0f, 0.0f, 0.0f, 0.0f};
    int activeCount = 0;  ///< Number of nodes with non-zero weight

    /// @brief Get weight for a specific node.
    [[nodiscard]] float operator[](int index) const noexcept {
        return (index >= 0 && index < kMaxMorphNodes) ? values[index] : 0.0f;
    }

    /// @brief Check if weights are valid (sum to ~1.0).
    [[nodiscard]] bool isValid() const noexcept {
        float sum = 0.0f;
        for (int i = 0; i < kMaxMorphNodes; ++i) {
            sum += values[i];
        }
        return std::abs(sum - 1.0f) < 0.001f || sum < 0.001f;
    }
};
```

### 3.2 MorphProcessingState

Internal state for morph processing.

```cpp
// Location: plugins/disrumpo/src/dsp/morph_engine.h (part of MorphEngine)

/// @brief Internal processing state for cross-family transitions.
struct MorphProcessingState {
    // Smoothed position
    float smoothedX = 0.5f;
    float smoothedY = 0.5f;

    // Current weights (potentially smoothed)
    MorphWeights currentWeights;

    // Transition zone state
    bool inTransitionZone = false;
    float transitionPosition = 0.0f;  // [0, 1] within 40-60% zone
    float fadeInProgress = 1.0f;      // For zone entry fade-in

    // Family tracking for cross-family detection
    DistortionFamily dominantFamily = DistortionFamily::Saturation;
    bool isCrossFamily = false;

    // Active processor tracking
    int activeProcessorCount = 0;
};
```

---

## 4. Constants

```cpp
// Location: plugins/disrumpo/src/dsp/morph_constants.h

#pragma once

namespace Disrumpo {

// === Weight computation ===
inline constexpr float kIDWExponent = 2.0f;           ///< Inverse distance weighting exponent
inline constexpr float kMinDistance = 0.001f;         ///< Minimum distance (cursor on node)
inline constexpr float kWeightThreshold = 0.001f;     ///< Skip nodes below this weight

// === Transition zone ===
inline constexpr float kTransitionZoneStart = 0.4f;   ///< Cross-family transition start
inline constexpr float kTransitionZoneEnd = 0.6f;     ///< Cross-family transition end
inline constexpr float kZoneFadeInMs = 7.0f;          ///< Fade-in time when entering zone

// === Smoothing ===
inline constexpr float kMinSmoothingMs = 0.0f;        ///< Instant transitions allowed
inline constexpr float kMaxSmoothingMs = 500.0f;      ///< Maximum smoothing time
inline constexpr float kDefaultSmoothingMs = 50.0f;   ///< Default smoothing time

// === Global limits ===
inline constexpr int kMaxGlobalProcessors = 16;       ///< Max active distortion instances

} // namespace Disrumpo
```

---

## 5. Entity Relationships

```
BandState (1) ----contains---- (4) MorphNode
    |
    +-- morphMode : MorphMode
    +-- driverType : MorphDriverType
    +-- activeNodeCount : int
    +-- morphX, morphY : float

MorphNode (1) ----has---- (1) DistortionType
    |
    +-- type : DistortionType ----maps-to---- (1) DistortionFamily
    +-- commonParams : DistortionCommonParams
    +-- typeParams : DistortionParams
    +-- posX, posY : float

MorphEngine (1) ----processes---- (1) BandState
    |
    +-- computes : MorphWeights
    +-- tracks : MorphProcessingState
    +-- owns : DistortionAdapter[] (for parallel processing)
```

---

## 6. State Transitions

### 6.1 Morph Mode Transitions

```
[Linear1D] --setMode(Planar2D)--> [Planar2D]
    |                                  |
    +-- Uses morphX only              +-- Uses morphX and morphY
    +-- 2-4 nodes on axis             +-- 2-4 nodes in 2D space

[Planar2D] --setMode(Radial2D)--> [Radial2D]
    |                                  |
    +-- IDW by distance               +-- Angle selects nodes
                                      +-- Distance controls intensity
```

### 6.2 Cross-Family Transition

```
[Single Processor (< 40%)]
    |
    +-- morphPosition increases past 40%
    |
[Transition Zone Entry]
    |
    +-- Activate secondary processor
    +-- Start fade-in (7ms)
    |
[Both Processors Active (40-60%)]
    |
    +-- Equal-power crossfade
    +-- Both process in parallel
    |
[Transition Zone Exit (> 60%)]
    |
    +-- Deactivate first processor
    |
[Single Processor (> 60%)]
```

---

## 7. Validation Rules

| Field | Rule | Error |
|-------|------|-------|
| activeNodeCount | 2 <= value <= 4 | Clamp to range |
| morphX, morphY | 0.0 <= value <= 1.0 | Clamp to range |
| morphSmoothingMs | 0.0 <= value <= 500.0 | Clamp to range |
| MorphNode.posX/Y | 0.0 <= value <= 1.0 | Clamp to range |
| MorphWeights.values[i] | 0.0 <= value <= 1.0 | Clamp, renormalize |
| Sum of weights | ~1.0 (within 0.001) | Renormalize if off |
