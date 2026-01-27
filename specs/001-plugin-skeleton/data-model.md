# Data Model: Disrumpo Plugin Skeleton

**Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)
**Date**: 2026-01-27

## Overview

This document defines the data model for the Disrumpo plugin skeleton. The skeleton establishes:
- Parameter ID encoding scheme (bit-encoded)
- State serialization format with versioning
- Core entities (Processor, Controller)

---

## 1. Parameter ID Encoding

### Encoding Scheme

Per [dsp-details.md](../Disrumpo/dsp-details.md), Disrumpo uses a bit-encoded parameter ID system:

```
Bit Layout (16-bit ParamID):
+--------+--------+--------+--------+
| 15..12 | 11..8  |  7..0  |
|  node  |  band  | param  |
+--------+--------+--------+

Special Bands:
- 0xF = Global parameters
- 0xE = Sweep parameters
- 0x0-0x7 = Per-band parameters (8 bands)

Node Index:
- 0xF = Band-level parameter (no node)
- 0x0-0x3 = Per-node parameters (4 nodes per band)
```

### Global Parameters (Skeleton)

| Parameter | ID (Hex) | ID (Decimal) | Description |
|-----------|----------|--------------|-------------|
| kInputGainId | 0x0F00 | 3840 | Input gain control |
| kOutputGainId | 0x0F01 | 3841 | Output gain control |
| kGlobalMixId | 0x0F02 | 3842 | Global dry/wet mix |

### Reserved ID Ranges (Future)

| Range | Purpose | Notes |
|-------|---------|-------|
| 0x0F00-0x0FFF | Global parameters | 256 available |
| 0x0E00-0x0EFF | Sweep parameters | 256 available |
| 0xF000-0xFFFF | Band-level (node=0xF) | Per-band params |
| 0x0000-0x7FFF | Per-node params | 8 bands x 4 nodes |

---

## 2. Parameter Definitions

### kInputGainId (0x0F00)

| Property | Value |
|----------|-------|
| Name | Input Gain |
| Range | 0.0 - 1.0 (normalized) |
| Default | 0.5 |
| Unit | dB |
| Display | Linear to dB mapping: `20 * log10(value * 2)` |
| Min Display | -inf dB (when 0.0) |
| Max Display | +6 dB (when 1.0) |

**Behavior**: Scales input signal before processing. At default (0.5), gain is unity (0 dB).

### kOutputGainId (0x0F01)

| Property | Value |
|----------|-------|
| Name | Output Gain |
| Range | 0.0 - 1.0 (normalized) |
| Default | 0.5 |
| Unit | dB |
| Display | Linear to dB mapping: `20 * log10(value * 2)` |
| Min Display | -inf dB (when 0.0) |
| Max Display | +6 dB (when 1.0) |

**Behavior**: Scales output signal after processing. At default (0.5), gain is unity (0 dB).

### kGlobalMixId (0x0F02)

| Property | Value |
|----------|-------|
| Name | Mix |
| Range | 0.0 - 1.0 (normalized) |
| Default | 1.0 |
| Unit | % |
| Display | `value * 100` percent |
| 0.0 | 100% Dry |
| 1.0 | 100% Wet |

**Behavior**: Blends between dry (input) and wet (processed) signal. At default (1.0), output is 100% wet.

**Note**: In the skeleton, audio passes through unchanged. The Mix parameter is registered but has no effect until DSP processing is added.

---

## 3. State Serialization Format

### Version Strategy

| Version | Description |
|---------|-------------|
| 1 | Initial release (skeleton) |
| 2+ | Future versions add parameters at end |

### Binary Format (v1)

```
Offset  Size   Type    Field
------  ----   ----    -----
0       4      int32   version (= 1)
4       4      float   inputGain
8       4      float   outputGain
12      4      float   globalMix
------
Total: 16 bytes
```

### Serialization Code Pattern

```cpp
// In Processor::getState()
tresult PLUGIN_API Processor::getState(IBStream* state) {
    IBStreamer streamer(state, kLittleEndian);

    // Version (MUST be first)
    if (!streamer.writeInt32(kPresetVersion))
        return kResultFalse;

    // Parameters
    if (!streamer.writeFloat(inputGain_.load()))
        return kResultFalse;
    if (!streamer.writeFloat(outputGain_.load()))
        return kResultFalse;
    if (!streamer.writeFloat(globalMix_.load()))
        return kResultFalse;

    return kResultOk;
}
```

### Deserialization with Version Handling

```cpp
// In Processor::setState()
tresult PLUGIN_API Processor::setState(IBStream* state) {
    IBStreamer streamer(state, kLittleEndian);

    int32 version;
    if (!streamer.readInt32(version))
        return kResultFalse;  // Corrupted state, use defaults

    if (version > kPresetVersion) {
        // Future version: read what we understand
        // Unknown fields at end will be skipped
    }

    // Read parameters
    float inputGain, outputGain, globalMix;

    if (!streamer.readFloat(inputGain))
        return kResultFalse;
    if (!streamer.readFloat(outputGain))
        return kResultFalse;
    if (!streamer.readFloat(globalMix))
        return kResultFalse;

    // Apply to atomics
    inputGain_.store(inputGain);
    outputGain_.store(outputGain);
    globalMix_.store(globalMix);

    return kResultOk;
}
```

---

## 4. Entity Definitions

### Processor Entity

```cpp
namespace Disrumpo {

class Processor : public Steinberg::Vst::AudioEffect {
public:
    // Lifecycle
    tresult initialize(FUnknown* context) override;
    tresult terminate() override;

    // Audio Processing
    tresult setBusArrangements(...) override;
    tresult setupProcessing(ProcessSetup& setup) override;
    tresult setActive(TBool state) override;
    tresult process(ProcessData& data) override;

    // State
    tresult getState(IBStream* state) override;
    tresult setState(IBStream* state) override;

    // Factory
    static FUnknown* createInstance(void*);

private:
    // Audio state
    double sampleRate_ = 44100.0;

    // Parameters (atomic for thread safety)
    std::atomic<float> inputGain_{0.5f};
    std::atomic<float> outputGain_{0.5f};
    std::atomic<float> globalMix_{1.0f};
};

} // namespace Disrumpo
```

### Controller Entity

```cpp
namespace Disrumpo {

class Controller : public Steinberg::Vst::EditControllerEx1 {
public:
    // Lifecycle
    tresult initialize(FUnknown* context) override;
    tresult terminate() override;

    // State sync
    tresult setComponentState(IBStream* state) override;
    tresult getState(IBStream* state) override;
    tresult setState(IBStream* state) override;

    // UI (returns nullptr for skeleton)
    IPlugView* createView(FIDString name) override;

    // Factory
    static FUnknown* createInstance(void*);
};

} // namespace Disrumpo
```

---

## 5. Validation Rules

### Parameter Validation

| Parameter | Validation |
|-----------|------------|
| inputGain | Clamp to [0.0, 1.0] |
| outputGain | Clamp to [0.0, 1.0] |
| globalMix | Clamp to [0.0, 1.0] |

### State Validation

| Condition | Action |
|-----------|--------|
| version > kPresetVersion | Load known fields, ignore unknown |
| version < 1 | Invalid, return kResultFalse |
| Read fails mid-stream | Return kResultFalse, use defaults |
| NaN or Inf values | Clamp to valid range |

---

## 6. Thread Safety Model

### Parameter Access

```
Host Thread                 Audio Thread
-----------                 ------------
    |                            |
    |  performEdit(id, value)    |
    |  --------------------->    |
    |                            |
    |         atomic store       |
    |         inputGain_.store() |
    |                            |
    |                       atomic load
    |                       inputGain_.load()
    |                            |
```

**Guarantee**: Parameters use `std::atomic<float>` for lock-free read/write across threads.

### State Serialization

- `getState()`: Called from any thread, reads atomics
- `setState()`: Called from any thread, writes atomics
- Both operations are thread-safe via atomics

---

## 7. Default Values

| Parameter | Default (Normalized) | Default (Display) | Rationale |
|-----------|---------------------|-------------------|-----------|
| inputGain | 0.5 | 0 dB | Unity gain |
| outputGain | 0.5 | 0 dB | Unity gain |
| globalMix | 1.0 | 100% | Full wet (effect audible) |

---

## 8. Future Extensions

### Version 2 (Planned for Band Management)

```
Offset  Size   Type    Field
------  ----   ----    -----
0       4      int32   version (= 2)
4       4      float   inputGain
8       4      float   outputGain
12      4      float   globalMix
16      4      int32   bandCount         // NEW
20+     ...    ...     per-band params   // NEW
```

### Migration Strategy

When `version == 1`:
- Read the 3 skeleton parameters
- Apply default bandCount = 4
- Initialize per-band params to defaults
