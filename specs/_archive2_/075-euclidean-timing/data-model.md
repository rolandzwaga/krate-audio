# Data Model: Euclidean Timing Mode

**Feature**: 075-euclidean-timing | **Date**: 2026-02-22

## Entities

### 1. Euclidean Timing State (ArpeggiatorCore members)

Added to `Krate::DSP::ArpeggiatorCore` at `dsp/include/krate/dsp/processors/arpeggiator_core.h`.

| Field | Type | Default | Range | Description |
|-------|------|---------|-------|-------------|
| `euclideanEnabled_` | `bool` | `false` | true/false | Whether Euclidean timing gating is active |
| `euclideanHits_` | `int` | `4` | [0, 32] | Number of hit pulses (k) in the pattern |
| `euclideanSteps_` | `int` | `8` | [2, 32] | Total number of steps (n) in the pattern |
| `euclideanRotation_` | `int` | `0` | [0, 31] | Cyclic rotation offset applied to pattern |
| `euclideanPosition_` | `size_t` | `0` | [0, euclideanSteps_-1] | Current step position in the Euclidean pattern |
| `euclideanPattern_` | `uint32_t` | `0` | 32-bit bitmask | Pre-computed pattern from `EuclideanPattern::generate()` |

**Relationships:**
- `euclideanPattern_` is derived from `euclideanHits_`, `euclideanSteps_`, `euclideanRotation_` via `regenerateEuclideanPattern()`.
- `euclideanPosition_` cycles modulo `euclideanSteps_`.
- `euclideanPosition_` is reset to 0 by `resetLanes()`, `setEuclideanEnabled(true)`, and `reset()`.

**Validation Rules:**
- `euclideanSteps_` is clamped to [EuclideanPattern::kMinSteps (2), EuclideanPattern::kMaxSteps (32)] in `setEuclideanSteps()`.
- `euclideanHits_` is clamped to [0, euclideanSteps_] in both `setEuclideanHits()` and `setEuclideanSteps()`.
- `euclideanRotation_` is clamped to [0, EuclideanPattern::kMaxSteps - 1 (31)] in `setEuclideanRotation()`.
- Pattern is automatically regenerated after any setter changes a parameter value.

**State Transitions:**
- **Disabled -> Enabled**: `euclideanPosition_` resets to 0. Pattern starts gating from step 0.
- **Enabled -> Disabled**: No cleanup needed. Euclidean state is simply ignored.
- **Parameter change while enabled**: Pattern is regenerated immediately. New pattern takes effect from the next step boundary.
- **Retrigger (Note/Beat)**: `resetLanes()` resets `euclideanPosition_` to 0.
- **Transport stop/restart**: `reset()` resets position and regenerates pattern.

### 2. Euclidean Parameter Storage (ArpeggiatorParams members)

Added to `Ruinae::ArpeggiatorParams` at `plugins/ruinae/src/parameters/arpeggiator_params.h`.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `euclideanEnabled` | `std::atomic<bool>` | `false` | Euclidean mode on/off |
| `euclideanHits` | `std::atomic<int>` | `4` | Number of hits (k) |
| `euclideanSteps` | `std::atomic<int>` | `8` | Number of steps (n) |
| `euclideanRotation` | `std::atomic<int>` | `0` | Rotation offset |

**Thread Safety**: All fields are `std::atomic` for lock-free access between the processor thread (reads) and parameter change callbacks (writes).

### 3. VST3 Parameter IDs

Added to `Ruinae::ParameterIDs` enum at `plugins/ruinae/src/plugin_ids.h`.

| Parameter | ID | Type | Range | Default | stepCount |
|-----------|----|------|-------|---------|-----------|
| `kArpEuclideanEnabledId` | 3230 | Toggle | 0-1 | 0 (off) | 1 |
| `kArpEuclideanHitsId` | 3231 | RangeParameter | 0-32 | 4 | 32 |
| `kArpEuclideanStepsId` | 3232 | RangeParameter | 2-32 | 8 | 30 |
| `kArpEuclideanRotationId` | 3233 | RangeParameter | 0-31 | 0 | 31 |

All parameters have `kCanAutomate` flag. None are hidden (`kIsHidden`).

### 4. Serialization Format

**Binary stream order** (appended after ratchet lane data):

| Offset (relative) | Type | Field | Backward Compat |
|--------------------|------|-------|-----------------|
| +0 | int32 | euclideanEnabled (0 or 1) | EOF here = Phase 6 compat (return true) |
| +4 | int32 | euclideanHits (0-32) | EOF here = corrupt (return false) |
| +8 | int32 | euclideanSteps (2-32) | EOF here = corrupt (return false) |
| +12 | int32 | euclideanRotation (0-31) | EOF here = corrupt (return false) |

Total: 16 bytes appended to the existing stream.

### 5. EuclideanPattern (Existing, Layer 0 -- No Changes)

`Krate::DSP::EuclideanPattern` at `dsp/include/krate/dsp/core/euclidean_pattern.h`.

| Method | Signature | Used For |
|--------|-----------|----------|
| `generate` | `static constexpr uint32_t generate(int pulses, int steps, int rotation = 0) noexcept` | Bitmask generation on parameter change |
| `isHit` | `static constexpr bool isHit(uint32_t pattern, int position, int steps) noexcept` | Per-step gating check in fireStep() |
| `countHits` | `static constexpr int countHits(uint32_t pattern) noexcept` | Test validation only |

## Entity Relationships

```
EuclideanPattern (Layer 0, static utility)
    |
    +-- generate() called by --> regenerateEuclideanPattern() in ArpeggiatorCore
    +-- isHit() called by -----> fireStep() in ArpeggiatorCore
    +-- countHits() used by ----> Unit tests only

ArpeggiatorCore (Layer 2)
    |
    +-- euclideanEnabled_ ---> gates fireStep() behavior
    +-- euclideanPattern_ ---> queried by isHit() each step
    +-- euclideanPosition_ --> advances each step, wraps at euclideanSteps_
    +-- euclideanHits_/Steps_/Rotation_ --> parameters for generate()

ArpeggiatorParams (Plugin layer)
    |
    +-- euclideanEnabled/Hits/Steps/Rotation --> transferred to ArpeggiatorCore via setters
    +-- serialized/deserialized in state save/load
    +-- synced to controller via loadArpParamsToController()
```
