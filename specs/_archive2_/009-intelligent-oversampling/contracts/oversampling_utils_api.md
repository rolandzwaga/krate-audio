# API Contract: oversampling_utils.h

**Location**: `plugins/disrumpo/src/dsp/oversampling_utils.h`
**Layer**: Plugin DSP (Disrumpo-specific)
**Namespace**: `Disrumpo`

## Overview

Utility functions for intelligent oversampling factor computation. Provides the morph-weighted factor calculation that combines per-type oversampling profiles with morph blend weights to determine the optimal oversampling factor per band.

## Dependencies

| Dependency | Header | What is used |
|------------|--------|-------------|
| `MorphNode` | `morph_node.h` | `MorphNode::type`, `kMaxMorphNodes` |
| `DistortionType` | `distortion_types.h` | Enum values |
| `getRecommendedOversample()` | `distortion_types.h` | Per-type factor lookup |

## API

### `calculateMorphOversampleFactor()`

```cpp
/// @brief Calculate morph-weighted oversampling factor.
///
/// For a band in morph mode, computes the weighted average of all active nodes'
/// recommended oversampling factors using morph blend weights, then rounds up
/// to the nearest valid power of 2 (1, 2, or 4), and clamps to the global limit.
///
/// Per spec FR-003: uses morph weights as weighting
/// Per spec FR-004: rounds up to nearest power of 2
/// Per spec FR-007, FR-008: clamps to global limit
/// Per spec FR-013: constant time (no loops beyond active node count)
///
/// @param nodes Array of morph nodes (only first activeNodeCount are read)
/// @param weights Array of morph weights (normalized, sum to ~1.0 for active nodes)
/// @param activeNodeCount Number of active nodes (2-4), clamped internally
/// @param globalLimit Global oversampling limit (1, 2, 4, or 8)
/// @return Effective oversampling factor: 1, 2, or 4 (never exceeds globalLimit)
[[nodiscard]] constexpr int calculateMorphOversampleFactor(
    const std::array<MorphNode, kMaxMorphNodes>& nodes,
    const std::array<float, kMaxMorphNodes>& weights,
    int activeNodeCount,
    int globalLimit
) noexcept;
```

### `roundUpToPowerOf2Factor()`

```cpp
/// @brief Round a weighted average up to the nearest valid oversampling factor.
///
/// Maps a continuous weighted average to a discrete power-of-2 factor:
/// - [0.0, 1.0] -> 1
/// - (1.0, 2.0] -> 2
/// - (2.0, 4.0] -> 4
///
/// @param weightedAverage The weighted average of recommended factors
/// @return Rounded-up factor: 1, 2, or 4
[[nodiscard]] constexpr int roundUpToPowerOf2Factor(float weightedAverage) noexcept;
```

### `getSingleTypeOversampleFactor()`

```cpp
/// @brief Get oversampling factor for a single distortion type with global limit.
///
/// Convenience function for non-morph mode: looks up the type's recommended
/// factor and clamps to the global limit.
///
/// @param type The distortion type
/// @param globalLimit Global oversampling limit (1, 2, 4, or 8)
/// @return Effective oversampling factor: 1, 2, or 4 (never exceeds globalLimit)
[[nodiscard]] constexpr int getSingleTypeOversampleFactor(
    DistortionType type,
    int globalLimit
) noexcept;
```

## Behavior Specifications

### Weighted Average Computation

```
Given nodes = [SoftClip(2x), HardClip(4x)]
And   weights = [0.5, 0.5, 0.0, 0.0]
And   activeNodeCount = 2
Then  weightedAverage = 0.5 * 2 + 0.5 * 4 = 3.0
Then  roundUpToPowerOf2Factor(3.0) = 4
Then  result = min(4, globalLimit)
```

### Rounding Behavior

| Weighted Average | Rounded Factor |
|------------------|---------------|
| 1.0 | 1 |
| 1.5 | 2 |
| 2.0 | 2 |
| 2.5 | 4 |
| 3.0 | 4 |
| 3.5 | 4 |
| 4.0 | 4 |

### Edge Cases

| Case | Behavior |
|------|----------|
| All nodes same type | Returns that type's factor (no rounding) |
| All nodes 1x types | Returns 1 |
| `activeNodeCount = 0` | Returns 1 (defensive) |
| `activeNodeCount > kMaxMorphNodes` | Clamped to kMaxMorphNodes |
| `globalLimit = 1` | Always returns 1 |
| Weights do not sum to 1.0 | Still computes weighted sum (garbage in, garbage out -- caller's responsibility) |
