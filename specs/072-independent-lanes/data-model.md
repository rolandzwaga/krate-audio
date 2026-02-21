# Data Model: Independent Lane Architecture (072)

**Date**: 2026-02-21
**Phase 1 Output**: Entity definitions extracted from spec

---

## Entity: ArpLane<T, MaxSteps>

**Location**: `dsp/include/krate/dsp/primitives/arp_lane.h`
**Layer**: 1 (primitives)
**Namespace**: `Krate::DSP`

### Template Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| T | typename | (required) | Step value type. Supported: `float`, `int8_t`, `uint8_t` |
| MaxSteps | size_t | 32 | Maximum number of steps. Must be >= 1. |

### Member Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| steps_ | `std::array<T, MaxSteps>` | value-initialized (0) | Step values |
| length_ | `size_t` | 1 | Active step count [1, MaxSteps] |
| position_ | `size_t` | 0 | Current step index [0, length_-1] |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| setLength | `void setLength(size_t len) noexcept` | Set active length, clamped to [1, MaxSteps]. If position_ >= new length, wraps to 0. |
| length | `size_t length() const noexcept` | Returns current active length. |
| setStep | `void setStep(size_t index, T value) noexcept` | Set step value. Index clamped to [0, length_-1]. |
| getStep | `T getStep(size_t index) const noexcept` | Get step value. Index clamped to [0, length_-1]. Out-of-range returns T{}. |
| advance | `T advance() noexcept` | Return current step value, then increment position (wrapping at length). |
| reset | `void reset() noexcept` | Set position to 0. |
| currentStep | `size_t currentStep() const noexcept` | Return current position index. |

### Validation Rules

| Rule | Enforcement |
|------|------------|
| Length in [1, MaxSteps] | `setLength()` clamps silently |
| Step index in [0, length-1] | `setStep()`/`getStep()` clamp; out-of-range `getStep()` returns T{} |
| Position in [0, length-1] | `advance()` wraps with modulo; `setLength()` resets if position >= new length |

### State Transitions

```
[Created] --prepare()--> [Ready: position=0, length=1, all steps=default]
[Ready]   --advance()--> [Advancing: position increments mod length]
[Any]     --reset()---> [Ready: position=0]
[Any]     --setLength(n)--> [length=clamp(n,1,32); position=position<length ? position : 0]
```

---

## Entity Extension: ArpeggiatorCore (Existing, Modified)

**Location**: `dsp/include/krate/dsp/processors/arpeggiator_core.h`
**Layer**: 2 (processors)

### New Member Fields

| Field | Type | Default Value | Description |
|-------|------|---------------|-------------|
| velocityLane_ | `ArpLane<float>` | length=1, step[0]=1.0f | Velocity multiplier per step (0.0-1.0) |
| gateLane_ | `ArpLane<float>` | length=1, step[0]=1.0f | Gate duration multiplier per step (0.01-2.0) |
| pitchLane_ | `ArpLane<int8_t>` | length=1, step[0]=0 | Semitone offset per step (-24 to +24) |

### New Public Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| velocityLane | `ArpLane<float>& velocityLane() noexcept` | Access velocity lane for configuration |
| gateLane | `ArpLane<float>& gateLane() noexcept` | Access gate lane for configuration |
| pitchLane | `ArpLane<int8_t>& pitchLane() noexcept` | Access pitch lane for configuration |

### New Private Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| resetLanes | `void resetLanes() noexcept` | Reset all lane positions to step 0. Called from reset(), retrigger, transport restart. |

### Modified Methods

| Method | Change |
|--------|--------|
| reset() | Adds `resetLanes()` call |
| noteOn() | When retrigger=Note, adds `resetLanes()` call alongside existing `selector_.reset()` |
| fireStep() | After NoteSelector::advance(), calls lane advance() and applies velocity/gate/pitch |
| calculateGateDuration() | Extended to accept gateLaneValue parameter, multiplied into the formula |

### Lane Application in fireStep() (Pseudocode)

```cpp
void fireStep(...) {
    ArpNoteResult result = selector_.advance(heldNotes_);

    if (result.count > 0) {
        // Advance all lanes (once per step, regardless of chord size)
        float velScale = velocityLane_.advance();
        float gateScale = gateLane_.advance();
        int8_t pitchOffset = pitchLane_.advance();

        // Calculate gate with lane multiplier
        size_t gateDuration = calculateGateDuration(gateScale);

        // Apply velocity and pitch to all notes in this step
        for (size_t i = 0; i < result.count; ++i) {
            // Velocity: scale and clamp to [1, 127]
            int scaledVel = static_cast<int>(
                std::round(result.velocities[i] * velScale));
            result.velocities[i] = static_cast<uint8_t>(
                std::clamp(scaledVel, 1, 127));

            // Pitch: offset and clamp to [0, 127]
            int offsetNote = static_cast<int>(result.notes[i]) + pitchOffset;
            result.notes[i] = static_cast<uint8_t>(
                std::clamp(offsetNote, 0, 127));
        }

        // ... emit events with modified velocities/notes and gateDuration
    }

    ++swingStepCounter_;
    currentStepDuration_ = calculateStepDuration(ctx);
}
```

---

## Entity Extension: ArpeggiatorParams (Existing, Modified)

**Location**: `plugins/ruinae/src/parameters/arpeggiator_params.h`
**Namespace**: `Ruinae`

### New Atomic Fields

| Field | Type | Default | Parameter ID Range |
|-------|------|---------|-------------------|
| velocityLaneLength | `std::atomic<int>` | 1 | kArpVelocityLaneLengthId (3020) |
| velocityLaneSteps | `std::array<std::atomic<float>, 32>` | 1.0f each | 3021-3052 |
| gateLaneLength | `std::atomic<int>` | 1 | kArpGateLaneLengthId (3060) |
| gateLaneSteps | `std::array<std::atomic<float>, 32>` | 1.0f each | 3061-3092 |
| pitchLaneLength | `std::atomic<int>` | 1 | kArpPitchLaneLengthId (3100) |
| pitchLaneSteps | `std::array<std::atomic<int>, 32>` | 0 each | 3101-3132 |

### Constructor Changes

```cpp
ArpeggiatorParams() {
    for (auto& step : velocityLaneSteps) {
        step.store(1.0f, std::memory_order_relaxed);
    }
    for (auto& step : gateLaneSteps) {
        step.store(1.0f, std::memory_order_relaxed);
    }
    // pitchLaneSteps default to 0 via value-initialization
}
```

---

## Entity: Parameter IDs (Existing, Extended)

**Location**: `plugins/ruinae/src/plugin_ids.h`

### New Parameter IDs

```cpp
// Velocity Lane (3020-3052)
kArpVelocityLaneLengthId = 3020,    // discrete: 1-32
kArpVelocityLaneStep0Id  = 3021,    // continuous: 0.0-1.0
// ... through
kArpVelocityLaneStep31Id = 3052,

// Gate Lane (3060-3092)
kArpGateLaneLengthId     = 3060,    // discrete: 1-32
kArpGateLaneStep0Id      = 3061,    // continuous: 0.01-2.0
// ... through
kArpGateLaneStep31Id     = 3092,

// Pitch Lane (3100-3132)
// NOTE: 3100 was previously kNumParameters sentinel; it is now kArpPitchLaneLengthId.
// kNumParameters is simultaneously updated to 3200, so there is no collision.
kArpPitchLaneLengthId    = 3100,    // discrete: 1-32 (step count, NOT semitone range)
kArpPitchLaneStep0Id     = 3101,    // discrete: -24 to +24
// ... through
kArpPitchLaneStep31Id    = 3132,
```

### Modified Constants

| Constant | Old Value | New Value |
|----------|-----------|-----------|
| kArpEndId | 3099 | 3199 |
| kNumParameters | 3100 | 3200 |

---

## Relationships

```
ArpLane<float> ----composed-by----> ArpeggiatorCore (velocityLane_, gateLane_)
ArpLane<int8_t> ---composed-by----> ArpeggiatorCore (pitchLane_)
ArpeggiatorCore ---reads-from-----> ArpeggiatorParams (via processor bridge)
ArpeggiatorParams --registered-by-> Controller (registerArpParams)
ArpeggiatorParams --dispatched-by-> Processor (handleArpParamChange)
ArpeggiatorParams --serialized-by-> Processor (saveArpParams/loadArpParams)
```
