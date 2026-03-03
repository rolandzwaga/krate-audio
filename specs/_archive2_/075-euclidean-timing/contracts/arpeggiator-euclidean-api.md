# API Contract: ArpeggiatorCore Euclidean Timing

**Feature**: 075-euclidean-timing | **Date**: 2026-02-22

## ArpeggiatorCore Public API Additions

### Setter Methods

```cpp
// Set the number of steps in the Euclidean pattern.
// Clamps to [EuclideanPattern::kMinSteps (2), EuclideanPattern::kMaxSteps (32)].
// Also re-clamps euclideanHits_ to [0, new step count].
// Regenerates the pattern bitmask.
// MUST be called BEFORE setEuclideanHits() when updating all parameters.
inline void setEuclideanSteps(int steps) noexcept;

// Set the number of hit pulses in the Euclidean pattern.
// Clamps to [0, euclideanSteps_].
// Regenerates the pattern bitmask.
inline void setEuclideanHits(int hits) noexcept;

// Set the rotation offset for the Euclidean pattern.
// Clamps to [0, EuclideanPattern::kMaxSteps - 1 (31)].
// Regenerates the pattern bitmask.
inline void setEuclideanRotation(int rotation) noexcept;

// Enable or disable Euclidean timing mode.
// When transitioning from disabled to enabled, resets euclideanPosition_ to 0.
// Does NOT clear ratchet sub-step state (in-flight sub-steps complete normally).
// MUST be called LAST when updating all parameters (after steps/hits/rotation).
inline void setEuclideanEnabled(bool enabled) noexcept;
```

### Getter Methods

```cpp
[[nodiscard]] inline bool euclideanEnabled() const noexcept;
[[nodiscard]] inline int euclideanHits() const noexcept;
[[nodiscard]] inline int euclideanSteps() const noexcept;
[[nodiscard]] inline int euclideanRotation() const noexcept;
```

### Prescribed Setter Call Order

When updating all Euclidean parameters from the processor:

```cpp
arpCore.setEuclideanSteps(steps);       // 1. Steps first (clamping base)
arpCore.setEuclideanHits(hits);         // 2. Hits clamped against new steps
arpCore.setEuclideanRotation(rotation); // 3. Rotation independent
arpCore.setEuclideanEnabled(enabled);   // 4. Enabled last (activates after full pattern computed)
```

## VST3 Parameter Contract

### Parameter Registration

| ID | Name | Type | Min | Max | Default | stepCount | Flags |
|----|------|------|-----|-----|---------|-----------|-------|
| 3230 | "Arp Euclidean" | Toggle | 0 | 1 | 0 | 1 | kCanAutomate |
| 3231 | "Arp Euclidean Hits" | RangeParameter | 0 | 32 | 4 | 32 | kCanAutomate |
| 3232 | "Arp Euclidean Steps" | RangeParameter | 2 | 32 | 8 | 30 | kCanAutomate |
| 3233 | "Arp Euclidean Rotation" | RangeParameter | 0 | 31 | 0 | 31 | kCanAutomate |

### Normalization Formulas

**Enabled (toggle):**
- Normalized -> Plain: `value >= 0.5 ? true : false`
- Plain -> Normalized: `enabled ? 1.0 : 0.0`

**Hits (discrete 0-32):**
- Normalized -> Plain: `clamp(round(value * 32), 0, 32)`
- Plain -> Normalized: `hits / 32.0`

**Steps (discrete 2-32):**
- Normalized -> Plain: `clamp(2 + round(value * 30), 2, 32)`
- Plain -> Normalized: `(steps - 2) / 30.0`

**Rotation (discrete 0-31):**
- Normalized -> Plain: `clamp(round(value * 31), 0, 31)`
- Plain -> Normalized: `rotation / 31.0`

### Display Formatting

| Parameter | Format | Examples |
|-----------|--------|---------|
| Enabled | "Off" / "On" | "Off", "On" |
| Hits | "%d hits" | "0 hits", "3 hits", "8 hits" |
| Steps | "%d steps" | "2 steps", "8 steps", "32 steps" |
| Rotation | "%d" | "0", "3", "15" |

## Serialization Contract

### Stream Format (appended after ratchet lane data)

```
[existing ratchet lane data]
int32: euclideanEnabled (0 or 1)
int32: euclideanHits (0-32)
int32: euclideanSteps (2-32)
int32: euclideanRotation (0-31)
```

### Backward Compatibility Rules

| EOF Point | Interpretation | Action |
|-----------|---------------|--------|
| Before euclideanEnabled | Phase 6 preset | return true, keep defaults (disabled, 4, 8, 0) |
| After euclideanEnabled but before euclideanHits | Corrupt stream | return false |
| After euclideanHits but before euclideanSteps | Corrupt stream | return false |
| After euclideanSteps but before euclideanRotation | Corrupt stream | return false |
| After all 4 fields | Complete load | return true |

### Value Clamping on Load

| Field | Clamp Range |
|-------|-------------|
| euclideanEnabled | bool (intVal != 0) |
| euclideanHits | [0, 32] |
| euclideanSteps | [2, 32] |
| euclideanRotation | [0, 31] |

## fireStep() Behavioral Contract

### Evaluation Order

```
1. selector_.advance() (NoteSelector -- always advances)
2. Lane advances: velocity, gate, pitch, modifier, ratchet (always advance)
3. Euclidean gating check (if enabled):
   a. Read isHit at current position
   b. Advance euclideanPosition_
   c. If rest: emit noteOffs, break tie chain, return
4. Modifier evaluation: Rest > Tie > Slide > Accent
5. Ratcheting
```

### Euclidean Rest Behavior

When `euclideanEnabled_ == true` and `isHit() == false`:
- Cancel pending noteOffs for current notes
- Emit noteOff for all currently sounding notes
- Set `currentArpNoteCount_ = 0`
- Set `tieActive_ = false` (break any tie chain)
- Increment `swingStepCounter_`
- Recalculate `currentStepDuration_`
- Return (skip modifier evaluation and ratcheting)

### Interaction Matrix

| Euclidean | Modifier | Result |
|-----------|----------|--------|
| Rest | Any | Silent (Euclidean rest overrides everything) |
| Hit | Active | Normal note emission |
| Hit | Rest | Silent (modifier Rest still works) |
| Hit | Tie | Sustain previous note (normal tie behavior) |
| Hit | Slide | Legato (normal slide behavior) |
| Hit | Accent | Velocity boost (normal accent behavior) |
| Disabled | Any | Phase 6 behavior (Euclidean state ignored) |
