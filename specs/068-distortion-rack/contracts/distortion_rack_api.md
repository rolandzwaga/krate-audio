# API Contract: DistortionRack

**Feature**: 068-distortion-rack | **Date**: 2026-01-15

## Overview

DistortionRack provides a 4-slot chainable distortion processor rack. Each slot can hold any supported distortion processor type with independent enable, mix, and gain controls.

---

## Constants

```cpp
static constexpr size_t kNumSlots = 4;
static constexpr float kDefaultSmoothingMs = 5.0f;
static constexpr float kDCBlockerCutoffHz = 10.0f;
static constexpr float kMinGainDb = -24.0f;
static constexpr float kMaxGainDb = +24.0f;
```

---

## Lifecycle Methods

### prepare

```cpp
void prepare(double sampleRate, size_t maxBlockSize) noexcept;
```

**Purpose**: Initialize the rack for audio processing.

**Parameters**:
- `sampleRate`: Sample rate in Hz (e.g., 44100.0, 48000.0)
- `maxBlockSize`: Maximum samples per channel per process() call

**Behavior**:
- Allocates internal buffers for oversampling
- Prepares oversamplers at effective sample rate (base * factor)
- Configures DC blockers at 10 Hz cutoff
- Configures smoothers with 5ms smoothing time
- Prepares all currently-configured slot processors
- Sets `prepared_` flag to true

**Pre-conditions**: None (can call multiple times)

**Post-conditions**:
- Ready to call `process()`
- `getLatency()` returns valid value

**Thread Safety**: NOT real-time safe (allocates memory)

---

### reset

```cpp
void reset() noexcept;
```

**Purpose**: Clear all internal state without deallocating.

**Behavior**:
- Resets all slot processors via their `reset()` methods
- Resets DC blockers
- Resets oversamplers
- Snaps smoothers to current targets (no ramp on resume)

**Pre-conditions**: `prepare()` should have been called

**Post-conditions**: Ready for fresh audio processing

**Thread Safety**: Real-time safe (no allocation)

---

### process

```cpp
void process(float* left, float* right, size_t numSamples) noexcept;
```

**Purpose**: Process stereo audio through the rack.

**Parameters**:
- `left`: Left channel buffer (in-place processing)
- `right`: Right channel buffer (in-place processing)
- `numSamples`: Number of samples per channel (must be <= maxBlockSize)

**Signal Flow**:
```
Input L/R
    |
    v (if oversamplingFactor > 1)
[Upsample L/R]
    |
    v
[Slot 0: Process -> DC Block -> Mix -> Gain]
    |
    v
[Slot 1: Process -> DC Block -> Mix -> Gain]
    |
    v
[Slot 2: Process -> DC Block -> Mix -> Gain]
    |
    v
[Slot 3: Process -> DC Block -> Mix -> Gain]
    |
    v (if oversamplingFactor > 1)
[Downsample L/R]
    |
    v
Output L/R
```

**Pre-conditions**:
- `prepare()` must have been called
- `numSamples <= maxBlockSize`
- Buffers must be valid and non-null

**Post-conditions**: Buffers contain processed audio

**Thread Safety**: Real-time safe (no allocation)

---

## Slot Configuration Methods

### setSlotType

```cpp
void setSlotType(size_t slot, SlotType type) noexcept;
```

**Purpose**: Set the processor type for a slot.

**Parameters**:
- `slot`: Slot index [0, 3]
- `type`: Processor type to assign

**Behavior**:
- Creates new processor instance(s) for the slot
- Calls `prepare()` on new processor with stored sample rate/block size
- Resets DC blockers for the slot
- If `type == Empty`, clears the slot (bypass)

**Pre-conditions**: `prepare()` should have been called for processor preparation

**Post-conditions**: Slot contains new processor, ready to process

**Thread Safety**: NOT real-time safe (may allocate)

---

### setSlotEnabled

```cpp
void setSlotEnabled(size_t slot, bool enabled) noexcept;
```

**Purpose**: Enable or disable a slot.

**Parameters**:
- `slot`: Slot index [0, 3]
- `enabled`: true to enable, false to bypass

**Behavior**:
- Sets enable smoother target to 1.0 (enabled) or 0.0 (disabled)
- Actual transition happens over 5ms during `process()`
- When disabled (0.0), slot passes audio through unchanged

**Pre-conditions**: None

**Post-conditions**: Slot enable state updated (smoothed transition pending)

**Thread Safety**: Real-time safe

---

### setSlotMix

```cpp
void setSlotMix(size_t slot, float mix) noexcept;
```

**Purpose**: Set the dry/wet mix for a slot.

**Parameters**:
- `slot`: Slot index [0, 3]
- `mix`: Mix value [0.0 = dry, 1.0 = wet]

**Behavior**:
- Clamps mix to [0.0, 1.0]
- Sets mix smoother target
- Mix formula: `output = dry * (1 - mix) + wet * mix`

**Pre-conditions**: None

**Post-conditions**: Slot mix updated (smoothed transition pending)

**Thread Safety**: Real-time safe

---

### setSlotGain

```cpp
void setSlotGain(size_t slot, float dB) noexcept;
```

**Purpose**: Set the output gain for a slot.

**Parameters**:
- `slot`: Slot index [0, 3]
- `dB`: Gain in decibels [-24.0, +24.0]

**Behavior**:
- Clamps dB to [-24.0, +24.0]
- Converts to linear gain via `dbToGain()`
- Sets gain smoother target
- Gain applied after mix blend

**Pre-conditions**: None

**Post-conditions**: Slot gain updated (smoothed transition pending)

**Thread Safety**: Real-time safe

---

## Slot Query Methods

### getSlotType

```cpp
[[nodiscard]] SlotType getSlotType(size_t slot) const noexcept;
```

**Returns**: Current `SlotType` for the slot, or `Empty` if slot index invalid.

---

### getSlotEnabled

```cpp
[[nodiscard]] bool getSlotEnabled(size_t slot) const noexcept;
```

**Returns**: Current enable state for the slot, or `false` if slot index invalid.

---

### getSlotMix

```cpp
[[nodiscard]] float getSlotMix(size_t slot) const noexcept;
```

**Returns**: Current mix value for the slot [0.0, 1.0], or 1.0 if slot index invalid.

---

### getSlotGain

```cpp
[[nodiscard]] float getSlotGain(size_t slot) const noexcept;
```

**Returns**: Current gain in dB for the slot, or 0.0 if slot index invalid.

---

## Processor Access Methods

### getProcessor

```cpp
template<typename T>
[[nodiscard]] T* getProcessor(size_t slot, size_t channel = 0) noexcept;

template<typename T>
[[nodiscard]] const T* getProcessor(size_t slot, size_t channel = 0) const noexcept;
```

**Purpose**: Access the underlying processor for fine-grained control.

**Parameters**:
- `slot`: Slot index [0, 3]
- `channel`: 0 = left, 1 = right

**Returns**:
- Pointer to processor if slot contains type `T`
- `nullptr` if slot index invalid, channel invalid, or type mismatch

**Usage Example**:
```cpp
rack.setSlotType(0, SlotType::TubeStage);
if (auto* tube = rack.getProcessor<TubeStage>(0)) {
    tube->setBias(0.3f);
    tube->setSaturationAmount(0.8f);
}
```

**Thread Safety**: NOT real-time safe to modify processor parameters during process()

---

## Global Configuration Methods

### setOversamplingFactor

```cpp
void setOversamplingFactor(int factor) noexcept;
```

**Purpose**: Set the global oversampling factor.

**Parameters**:
- `factor`: 1 (none), 2 (2x), or 4 (4x)

**Behavior**:
- Invalid factors are ignored
- For full effect, call `prepare()` after changing factor

**Pre-conditions**: None

**Post-conditions**: Oversampling factor updated

**Thread Safety**: NOT real-time safe (requires prepare())

---

### getOversamplingFactor

```cpp
[[nodiscard]] int getOversamplingFactor() const noexcept;
```

**Returns**: Current oversampling factor (1, 2, or 4).

---

### getLatency

```cpp
[[nodiscard]] size_t getLatency() const noexcept;
```

**Returns**: Processing latency in samples.

**Calculation**:
- Factor 1: 0 samples
- Factor 2: `oversampler2x_.getLatency()` (depends on quality)
- Factor 4: `oversampler4x_.getLatency()` (depends on quality)

---

### setDCBlockingEnabled

```cpp
void setDCBlockingEnabled(bool enabled) noexcept;
```

**Purpose**: Enable or disable per-slot DC blocking globally.

**Parameters**:
- `enabled`: true to enable DC blocking, false to bypass

**Pre-conditions**: None

**Post-conditions**: DC blocking state updated

**Thread Safety**: Real-time safe

---

### getDCBlockingEnabled

```cpp
[[nodiscard]] bool getDCBlockingEnabled() const noexcept;
```

**Returns**: Current DC blocking enabled state.

---

## Error Handling

All methods are `noexcept` and fail silently for invalid inputs:

| Invalid Input | Behavior |
|---------------|----------|
| slot index >= 4 | No-op for setters; default value for getters |
| channel > 1 | `nullptr` for getProcessor |
| type mismatch | `nullptr` for getProcessor |
| mix out of range | Clamped to [0.0, 1.0] |
| gain out of range | Clamped to [-24.0, +24.0] dB |
| invalid factor | Ignored (factor unchanged) |

---

## Thread Safety Summary

| Method | RT-Safe | Notes |
|--------|---------|-------|
| prepare | No | Allocates memory |
| reset | Yes | - |
| process | Yes | - |
| setSlotType | No | May allocate |
| setSlotEnabled | Yes | - |
| setSlotMix | Yes | - |
| setSlotGain | Yes | - |
| getSlot* | Yes | - |
| getProcessor | Yes* | *Modifying processor is not RT-safe |
| setOversamplingFactor | No | Requires prepare() |
| setDCBlockingEnabled | Yes | - |
