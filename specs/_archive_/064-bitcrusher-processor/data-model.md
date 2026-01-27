# Data Model: BitcrusherProcessor

**Feature**: 064-bitcrusher-processor | **Date**: 2026-01-14

## Summary

Data model for BitcrusherProcessor Layer 2 DSP processor. Defines the class structure, enumerations, member variables, and their relationships.

---

## Enumerations

### ProcessingOrder

Controls the order of bit crushing and sample rate reduction.

```cpp
enum class ProcessingOrder : uint8_t {
    BitCrushFirst = 0,    // Bit crush -> Sample reduce (default)
    SampleReduceFirst = 1 // Sample reduce -> Bit crush
};
```

| Value | Order | Effect |
|-------|-------|--------|
| BitCrushFirst | BitCrusher -> SampleRateReducer | Quantization noise is aliased |
| SampleReduceFirst | SampleRateReducer -> BitCrusher | Stairstep is then quantized |

---

## Primary Entity: BitcrusherProcessor

### Class Overview

```cpp
class BitcrusherProcessor {
    // Layer 2 processor composing Layer 1 primitives
    // Provides gain staging, dither gating, configurable order
};
```

### Constants

| Constant | Type | Value | Description |
|----------|------|-------|-------------|
| kMinBitDepth | float | 4.0f | Minimum bit depth |
| kMaxBitDepth | float | 16.0f | Maximum bit depth |
| kMinReductionFactor | float | 1.0f | No reduction |
| kMaxReductionFactor | float | 8.0f | Maximum reduction |
| kMinGainDb | float | -24.0f | Minimum gain |
| kMaxGainDb | float | +24.0f | Maximum gain |
| kDefaultSmoothingMs | float | 5.0f | Parameter smoothing time |
| kDCBlockerCutoffHz | float | 10.0f | DC blocker cutoff |
| kDitherGateThresholdDb | float | -60.0f | Dither gate threshold |
| kDitherGateAttackMs | float | 1.0f | Fast attack for gate |
| kDitherGateReleaseMs | float | 20.0f | Slower release to prevent pumping |

### Parameter Members

| Member | Type | Default | Range | Smoothed | Description |
|--------|------|---------|-------|----------|-------------|
| bitDepth_ | float | 16.0f | [4, 16] | No | Bit depth for quantization |
| reductionFactor_ | float | 1.0f | [1, 8] | No | Sample rate reduction factor |
| ditherAmount_ | float | 0.0f | [0, 1] | No | TPDF dither amount |
| preGainDb_ | float | 0.0f | [-24, +24] | Yes | Pre-processing gain (drive) |
| postGainDb_ | float | 0.0f | [-24, +24] | Yes | Post-processing gain (makeup) |
| mix_ | float | 1.0f | [0, 1] | Yes | Dry/wet mix |
| processingOrder_ | ProcessingOrder | BitCrushFirst | enum | No | Processing chain order |
| ditherGateEnabled_ | bool | true | bool | No | Enable/disable dither gating |

### State Members

| Member | Type | Description |
|--------|------|-------------|
| sampleRate_ | double | Current sample rate |
| prepared_ | bool | Whether prepare() has been called |
| currentDitherAmount_ | float | Active dither amount (after gating) |

### Component Members

| Member | Type | Layer | Purpose |
|--------|------|-------|---------|
| bitCrusher_ | BitCrusher | 1 | Bit depth reduction + dither |
| sampleRateReducer_ | SampleRateReducer | 1 | Sample rate decimation |
| dcBlocker_ | DCBlocker | 1 | DC offset removal |
| preGainSmoother_ | OnePoleSmoother | 1 | Pre-gain smoothing |
| postGainSmoother_ | OnePoleSmoother | 1 | Post-gain smoothing |
| mixSmoother_ | OnePoleSmoother | 1 | Mix smoothing |
| ditherGateEnvelope_ | EnvelopeFollower | 2 | Signal level detection for dither gate |

### Buffer Members

| Member | Type | Purpose |
|--------|------|---------|
| dryBuffer_ | std::vector<float> | Store dry signal for mix blending |

---

## Relationships

```
BitcrusherProcessor (Layer 2)
    |
    +-- BitCrusher (Layer 1)
    |       - setBitDepth()
    |       - setDither()
    |       - process()
    |
    +-- SampleRateReducer (Layer 1)
    |       - setReductionFactor()
    |       - process()
    |
    +-- DCBlocker (Layer 1)
    |       - process()
    |
    +-- OnePoleSmoother x3 (Layer 1)
    |       - preGainSmoother_
    |       - postGainSmoother_
    |       - mixSmoother_
    |
    +-- EnvelopeFollower (Layer 2)
            - processSample()
            - Provides envelope for dither gating
```

---

## Processing Flow

### Signal Chain: BitCrushFirst (Default)

```
Input
  |
  v
[Store Dry] --> dryBuffer_
  |
  v
[Pre-Gain] (smoothed) --> preGainSmoother_.process()
  |
  v
[Dither Gate Check] --> envelope > threshold ? dither : 0
  |
  v
[BitCrusher] --> bitCrusher_.process(sample)
  |
  v
[SampleRateReducer] --> sampleRateReducer_.process(sample)
  |
  v
[Post-Gain] (smoothed) --> postGainSmoother_.process()
  |
  v
[DC Blocker] --> dcBlocker_.process(sample)
  |
  v
[Mix Blend] --> dry * (1-mix) + wet * mix
  |
  v
Output
```

### Signal Chain: SampleReduceFirst

```
Input
  |
  v
[Store Dry] --> dryBuffer_
  |
  v
[Pre-Gain] (smoothed)
  |
  v
[SampleRateReducer] --> sampleRateReducer_.process(sample)
  |
  v
[Dither Gate Check] --> envelope > threshold ? dither : 0
  |
  v
[BitCrusher] --> bitCrusher_.process(sample)
  |
  v
[Post-Gain] (smoothed)
  |
  v
[DC Blocker]
  |
  v
[Mix Blend]
  |
  v
Output
```

---

## Validation Rules

| Parameter | Validation | Clamp Method |
|-----------|------------|--------------|
| bitDepth | [4.0, 16.0] | std::clamp |
| reductionFactor | [1.0, 8.0] | std::clamp |
| ditherAmount | [0.0, 1.0] | std::clamp |
| preGainDb | [-24.0, +24.0] | std::clamp |
| postGainDb | [-24.0, +24.0] | std::clamp |
| mix | [0.0, 1.0] | std::clamp |

---

## State Transitions

### Lifecycle States

```
[Uninitialized]
      |
      | prepare(sampleRate, maxBlockSize)
      v
[Prepared] <------ reset()
      |                |
      | process()      |
      v                |
[Processing] ---------+
```

### Dither Gate States

```
[Dither Active] (envelope >= threshold)
      |
      | envelope drops below -60dB
      v
[Dither Gated] (dither = 0)
      |
      | envelope rises above -60dB
      v
[Dither Active]
```

---

## Default Configuration

```cpp
BitcrusherProcessor processor;
// After construction:
// - bitDepth_ = 16.0f (transparent)
// - reductionFactor_ = 1.0f (no reduction)
// - ditherAmount_ = 0.0f (no dither)
// - preGainDb_ = 0.0f (unity)
// - postGainDb_ = 0.0f (unity)
// - mix_ = 1.0f (100% wet)
// - processingOrder_ = BitCrushFirst
// - ditherGateEnabled_ = true
```
