# Data Model: Intelligent Per-Band Oversampling

**Feature**: 009-intelligent-oversampling
**Date**: 2026-01-30

## Entities

### 1. Oversampling Profile (EXISTING -- no changes)

**Location**: `plugins/disrumpo/src/dsp/distortion_types.h`
**Type**: `constexpr` free function

Maps each of the 26 `DistortionType` enum values to a recommended oversampling factor.

| Field | Type | Description |
|-------|------|-------------|
| input | `DistortionType` | One of 26 distortion types (enum, uint8_t) |
| output | `int` | Recommended factor: 1, 2, or 4 |

**Existing API**:
```cpp
constexpr int getRecommendedOversample(DistortionType type) noexcept;
```

**Validation**: Output is always 1, 2, or 4 (enforced by switch statement). Input validated by enum type. `constexpr` ensures compile-time evaluation.

### 2. Oversampling Crossfade State (NEW -- added to BandProcessor)

**Location**: `plugins/disrumpo/src/dsp/band_processor.h` (new members)

Per-band runtime state tracking the current oversampling factor and managing smooth transitions.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `currentOversampleFactor_` | `int` | 2 | **EXISTING**: The target factor being faded INTO during a transition, or the steady-state factor when no transition is active. The old factor being faded OUT is tracked separately in `crossfadeOldFactor_`. |
| `maxOversampleFactor_` | `int` | 8 | **EXISTING**: Global limit |
| `targetOversampleFactor_` | `int` | 2 | **NEW**: Desired factor after transition completes |
| `crossfadeOldFactor_` | `int` | 2 | **NEW**: Factor being faded out during transition |
| `crossfadeProgress_` | `float` | 0.0f | **NEW**: Transition progress [0.0 = start, 1.0 = done] |
| `crossfadeIncrement_` | `float` | 0.0f | **NEW**: Per-sample progress step (from crossfadeIncrement()) |
| `crossfadeActive_` | `bool` | false | **NEW**: True during an active transition |
| `crossfadeOldLeft_` | `std::array<float, 2048>` | {} | **NEW**: Pre-allocated buffer for old path left channel |
| `crossfadeOldRight_` | `std::array<float, 2048>` | {} | **NEW**: Pre-allocated buffer for old path right channel |

**State Transitions**:

```
IDLE (crossfadeActive_ = false)
  |
  |-- Factor change detected (new target != current)
  |   Sets: crossfadeOldFactor_ = currentOversampleFactor_
  |          crossfadeProgress_ = 0.0
  |          crossfadeIncrement_ = crossfadeIncrement(8.0, sampleRate_)
  |          crossfadeActive_ = true
  |          currentOversampleFactor_ = targetOversampleFactor_
  v
TRANSITIONING (crossfadeActive_ = true)
  |
  |-- Progress reaches 1.0
  |   Sets: crossfadeActive_ = false
  |          crossfadeOldFactor_ = currentOversampleFactor_
  |
  |-- New factor change mid-transition (abort-and-restart)
  |   Sets: crossfadeOldFactor_ = currentOversampleFactor_ (was being faded in)
  |          crossfadeProgress_ = 0.0
  |          currentOversampleFactor_ = new target
  |   (stays in TRANSITIONING)
  v
IDLE
```

**Validation Rules**:
- `crossfadeProgress_` clamped to [0.0, 1.0]
- `currentOversampleFactor_` must be in {1, 2, 4, 8}
- `targetOversampleFactor_` must be in {1, 2, 4, 8}
- `crossfadeOldFactor_` must be in {1, 2, 4, 8}
- `crossfadeIncrement_` must be positive (negative or zero indicates instant transition)

### 3. Global Oversampling Limit Parameter (EXISTING -- needs wiring)

**Location**: `plugins/disrumpo/src/plugin_ids.h`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| Parameter ID | `0x0F04` | -- | `kOversampleMaxId` |
| Normalized value | `float [0, 1]` | (maps to 4x) | VST3 normalized parameter |
| Discrete values | `{1, 2, 4, 8}` | 4 | Mapped from normalized via 4-step list |

**Denormalization mapping** (StringListParameter with 4 options):
| Normalized Range | Discrete Value | Display |
|------------------|---------------|---------|
| [0.0, 0.25) | 1 | "1x" |
| [0.25, 0.5) | 2 | "2x" |
| [0.5, 0.75) | 4 | "4x" |
| [0.75, 1.0] | 8 | "8x" |

**Wiring needed**:
- Processor: Handle in `processParameterChanges()`, call `setMaxOversampleFactor()` on all band processors with crossfade trigger
- Controller: Already registered (assumption from spec)

### 4. Morph Weight Set (EXISTING -- read-only for this feature)

**Location**: `plugins/disrumpo/src/dsp/morph_engine.h`

| Field | Type | Description |
|-------|------|-------------|
| `weights_` | `std::array<float, 4>` | Per-node weights, sum to 1.0 |
| `activeNodeCount_` | `int` | Number of active nodes (2-4) |

**Accessed via**: `MorphEngine::getWeights()` returns `const std::array<float, kMaxMorphNodes>&`

**Note**: Weights are computed by `MorphEngine::calculateMorphWeights()` based on morph position and mode. The oversampling system reads these weights but never modifies them.

## Relationships

```
+-------------------+     reads       +------------------+
| BandProcessor     |<--------------->| MorphEngine      |
| (oversampling     |                 | (weights, nodes) |
| crossfade state)  |                 +------------------+
+-------------------+
       |
       | calls
       v
+----------------------------+     reads     +-----------------------+
| calculateMorphOversample() |<------------->| getRecommendedOver-   |
| (oversampling_utils.h)     |               | sample() (per-type)   |
+----------------------------+               +-----------------------+
       |
       | limited by
       v
+----------------------------+
| Global Oversampling Limit  |
| (kOversampleMaxId = 0x0F04)|
+----------------------------+
       |
       | propagated by
       v
+----------------------------+
| Processor                  |
| (processParameterChanges) |
+----------------------------+
```

## New Utility Function

### `calculateMorphOversampleFactor()`

**Location**: `plugins/disrumpo/src/dsp/oversampling_utils.h` (NEW file)

```cpp
/// @brief Calculate morph-weighted oversampling factor, rounded up to power of 2.
///
/// Computes weighted average of per-node recommended factors using morph weights,
/// then rounds up to the nearest power of 2 (1, 2, or 4).
///
/// @param nodes Array of morph nodes (type info needed for profile lookup)
/// @param weights Array of morph weights (must sum to ~1.0 for active nodes)
/// @param activeNodeCount Number of active nodes (2-4)
/// @param globalLimit Global oversampling limit (1, 2, 4, or 8)
/// @return Effective oversampling factor: 1, 2, or 4 (clamped to globalLimit)
constexpr int calculateMorphOversampleFactor(
    const std::array<MorphNode, kMaxMorphNodes>& nodes,
    const std::array<float, kMaxMorphNodes>& weights,
    int activeNodeCount,
    int globalLimit
) noexcept;
```

**Algorithm**:
1. `weightedSum = 0.0`
2. For i in [0, activeNodeCount): `weightedSum += weights[i] * getRecommendedOversample(nodes[i].type)`
3. Round up to nearest power of 2:
   - `weightedSum <= 1.0` -> 1
   - `weightedSum <= 2.0` -> 2
   - else -> 4
4. Clamp to `min(result, globalLimit)`
5. Return result

**Why round up (FR-004)**: Rounding down would risk aliasing from the higher-factor type that still has significant weight. Rounding up ensures quality is never compromised.
