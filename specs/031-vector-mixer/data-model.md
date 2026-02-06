# Data Model: Vector Mixer

**Feature**: 031-vector-mixer | **Date**: 2026-02-06

## Entities

### Topology (enum class)

Spatial arrangement of the four sources in the XY plane.

| Value | Integer | Description |
|-------|---------|-------------|
| `Square` | 0 | Sources at corners (bilinear interpolation). A=top-left, B=top-right, C=bottom-left, D=bottom-right. Default. |
| `Diamond` | 1 | Sources at cardinal points (Prophet VS style). A=left, B=right, C=top, D=bottom. |

**Backing type**: `uint8_t`

---

### MixingLaw (enum class)

Weight transformation applied after topology computation.

| Value | Integer | Description |
|-------|---------|-------------|
| `Linear` | 0 | Weights used directly from topology. Sum = 1.0 (amplitude-preserving). Default. |
| `EqualPower` | 1 | `sqrt()` applied to topology weights. Sum-of-squares = 1.0 (power-preserving). |
| `SquareRoot` | 2 | `sqrt()` applied to topology weights (no additional normalization). Mathematically identical to EqualPower for unit-sum topology weights — retained as named option for semantic clarity. |

**Backing type**: `uint8_t`

---

### Weights (struct)

Current mixing weights for the four sources. Returned by `getWeights()`.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `a` | `float` | `0.25f` | Weight for source A |
| `b` | `float` | `0.25f` | Weight for source B |
| `c` | `float` | `0.25f` | Weight for source C |
| `d` | `float` | `0.25f` | Weight for source D |

**Invariants**:
- All weights are non-negative: `a >= 0 && b >= 0 && c >= 0 && d >= 0`
- For Linear and SquareRoot laws: weights sum to approximately 1.0 (within floating-point precision)
- For EqualPower law: sum of squared weights approximately equals 1.0

---

### StereoOutput (struct) -- EXISTING, to be extracted to Layer 0

Stereo sample pair returned by stereo `process()` methods.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `left` | `float` | `0.0f` | Left channel output sample |
| `right` | `float` | `0.0f` | Right channel output sample |

**Current location**: `dsp/include/krate/dsp/systems/unison_engine.h`
**Target location**: `dsp/include/krate/dsp/core/stereo_output.h`

---

### VectorMixer (class)

Main system component. Layer 3.

#### Public State (via getters/setters)

| Parameter | Type | Range | Default | Thread-Safe | Description |
|-----------|------|-------|---------|-------------|-------------|
| vectorX | `std::atomic<float>` | [-1, 1] | 0.0 | Yes (atomic) | Horizontal position. -1=left/A, +1=right/B |
| vectorY | `std::atomic<float>` | [-1, 1] | 0.0 | Yes (atomic) | Vertical position. -1=top/A, +1=bottom/D |
| smoothingTimeMs | `std::atomic<float>` | [0, inf) | 5.0 | Yes (atomic) | One-pole smoothing time in ms. 0 = instant. |
| topology | `Topology` | enum | Square | No | Spatial arrangement. Set only when not processing. |
| mixingLaw | `MixingLaw` | enum | Linear | No | Weight transformation. Set only when not processing. |

#### Private State

| Member | Type | Description |
|--------|------|-------------|
| `sampleRate_` | `double` | Sample rate in Hz. Set by `prepare()`. |
| `smoothCoeff_` | `float` | One-pole coefficient computed from smoothing time and sample rate. |
| `smoothedX_` | `float` | Current smoothed X position (advances per sample). |
| `smoothedY_` | `float` | Current smoothed Y position (advances per sample). |
| `currentWeights_` | `Weights` | Most recently computed weights (after topology + mixing law). Updated per-sample inside `process()`/`processBlock()` before computing output. `getWeights()` returns a copy of this cached value, reflecting the weights used for the most recent sample. Not updated outside of process calls. |
| `prepared_` | `bool` | Whether `prepare()` has been called. |

#### State Transitions

```
                    prepare(sampleRate)
    [Unprepared] -----------------------> [Prepared]
         |                                    |
         |  process() returns 0.0             |  process() computes output
         |                                    |
         |                  reset()           |
         |              <-----------          |
         |              ----------->          |
         |                                    |
         |            prepare(newRate)         |
         |              <-----------          |
         |              ----------->          |
```

- **Unprepared**: Default state after construction. `process()` returns 0.0.
- **Prepared**: After `prepare()`. All processing methods functional.
- **Reset**: `reset()` snaps smoothed X/Y to current targets. Stays in Prepared state.

## Relationships

```
VectorMixer (Layer 3)
    |
    +-- uses Topology (enum) for source arrangement selection
    +-- uses MixingLaw (enum) for weight transformation selection
    +-- returns Weights (struct) from getWeights()
    +-- returns StereoOutput (struct) from stereo process()
    |
    +-- depends on: math_constants.h (Layer 0) for kTwoPi
    +-- depends on: db_utils.h (Layer 0) for constexprExp, isNaN, isInf
    +-- depends on: stereo_output.h (Layer 0) for StereoOutput
    |
    +-- no Layer 1/2 dependencies
```

## Validation Rules

| Rule | When Applied | Action on Violation |
|------|-------------|---------------------|
| X clamped to [-1, 1] | `setVectorX()`, `setVectorPosition()` (at setter, before atomic store) | `std::clamp(x, -1.0f, 1.0f)` |
| Y clamped to [-1, 1] | `setVectorY()`, `setVectorPosition()` (at setter, before atomic store) | `std::clamp(y, -1.0f, 1.0f)` |
| Smoothing time clamped to [0, inf) | `setSmoothingTimeMs()` | Negative values clamped to 0 |
| Topology enum range | `setTopology()` | Out-of-range values ignored (preserve previous) |
| MixingLaw enum range | `setMixingLaw()` | Out-of-range values ignored (preserve previous) |
| NaN/Inf in audio inputs | `process()`, `processBlock()` | Propagated through (no sanitization). Debug assertion in debug builds. |
| sampleRate > 0 | `prepare()` | Debug assertion failure; system remains unprepared |
| process() before prepare() | Runtime check | Returns 0.0f (silence) |

## Memory Layout

VectorMixer has no heap allocations. Estimated instance size:

| Member | Size (bytes) |
|--------|-------------|
| `targetX_` (atomic<float>) | 4 |
| `targetY_` (atomic<float>) | 4 |
| `smoothingTimeMs_` (atomic<float>) | 4 |
| `smoothedX_` (float) | 4 |
| `smoothedY_` (float) | 4 |
| `smoothCoeff_` (float) | 4 |
| `sampleRate_` (double) | 8 |
| `currentWeights_` (Weights) | 16 |
| `topology_` (Topology enum) | 1 |
| `mixingLaw_` (MixingLaw enum) | 1 |
| `prepared_` (bool) | 1 |
| Padding | ~2 |
| **Total** | **~52 bytes** |
