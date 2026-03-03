# API Contract: ArpeggiatorCore Lane Extension

**Spec**: 072-independent-lanes
**File**: `dsp/include/krate/dsp/processors/arpeggiator_core.h` (modified)

---

## New Public API

### Lane Accessors

```cpp
/// @brief Access velocity lane for configuration.
ArpLane<float>& velocityLane() noexcept { return velocityLane_; }

/// @brief Access gate lane for configuration.
ArpLane<float>& gateLane() noexcept { return gateLane_; }

/// @brief Access pitch lane for configuration.
ArpLane<int8_t>& pitchLane() noexcept { return pitchLane_; }
```

### Const Lane Accessors (for queries)

```cpp
[[nodiscard]] const ArpLane<float>& velocityLane() const noexcept { return velocityLane_; }
[[nodiscard]] const ArpLane<float>& gateLane() const noexcept { return gateLane_; }
[[nodiscard]] const ArpLane<int8_t>& pitchLane() const noexcept { return pitchLane_; }
```

---

## Modified Existing Methods

### reset()

**Before**: Resets timing, selector, pending NoteOffs.
**After**: Same + calls `resetLanes()`.

### noteOn() (Retrigger Note mode)

**Before**: Resets selector and swing counter.
**After**: Same + calls `resetLanes()`.

### processBlock() (Bar Boundary handling)

**Before**: Resets selector and swing counter at bar boundary.
**After**: Same + calls `resetLanes()`.

### fireStep()

**Before**: Gets notes from NoteSelector, emits NoteOn, schedules NoteOff.
**After**: Same + advances all three lanes, applies velocity/gate/pitch modifiers.

### calculateGateDuration()

**Before**: `size_t calculateGateDuration() const noexcept`
**After**: `size_t calculateGateDuration(float gateLaneValue = 1.0f) const noexcept`

The gate lane multiplier is applied: `stepDuration * gateLengthPercent / 100 * gateLaneValue`.

---

## New Private Members

```cpp
// Lane containers (Layer 1)
ArpLane<float> velocityLane_;   // default: length=1, step[0]=1.0f
ArpLane<float> gateLane_;       // default: length=1, step[0]=1.0f
ArpLane<int8_t> pitchLane_;     // default: length=1, step[0]=0

/// @brief Reset all lane positions to step 0.
void resetLanes() noexcept {
    velocityLane_.reset();
    gateLane_.reset();
    pitchLane_.reset();
}
```

---

## Initialization

Lanes must be initialized with correct default values in the constructor or field initializers:

- `velocityLane_`: length 1, step[0] = 1.0f (full velocity passthrough)
- `gateLane_`: length 1, step[0] = 1.0f (pure global gate length)
- `pitchLane_`: length 1, step[0] = 0 (no transposition)

These defaults ensure SC-002 (bit-identical backward compatibility).
