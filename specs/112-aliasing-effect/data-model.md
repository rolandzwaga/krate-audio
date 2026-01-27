# Data Model: AliasingEffect

**Feature**: 112-aliasing-effect | **Date**: 2026-01-27

## Entities

### AliasingEffect

The main processor class for intentional aliasing with band isolation and frequency shifting.

**Layer**: 2 (DSP Processor)
**Location**: `dsp/include/krate/dsp/processors/aliasing_effect.h`

#### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `sampleRate_` | `double` | `44100.0` | Current sample rate in Hz |
| `prepared_` | `bool` | `false` | Whether prepare() has been called |
| `downsampleFactor_` | `float` | `2.0f` | Target downsample factor [2, 32] |
| `frequencyShiftHz_` | `float` | `0.0f` | Target frequency shift [-5000, +5000] Hz |
| `bandLowHz_` | `float` | `20.0f` | Target aliasing band low frequency [20, sampleRate*0.45] Hz |
| `bandHighHz_` | `float` | `20000.0f` | Target aliasing band high frequency [20, sampleRate*0.45] Hz |
| `mix_` | `float` | `1.0f` | Target dry/wet mix [0, 1] |

#### Internal Components

| Component | Type | Purpose |
|-----------|------|---------|
| `bandHighpass_` | `BiquadCascade<2>` | 24dB/oct highpass for band isolation low edge |
| `bandLowpass_` | `BiquadCascade<2>` | 24dB/oct lowpass for band isolation high edge |
| `frequencyShifter_` | `FrequencyShifter` | SSB frequency shifting (fixed: Dir=Up, FB=0, Mod=0, Mix=1) |
| `sampleRateReducer_` | `SampleRateReducer` | Sample-and-hold downsampling |
| `downsampleSmoother_` | `OnePoleSmoother` | 10ms smoothing for downsample factor |
| `shiftSmoother_` | `OnePoleSmoother` | 10ms smoothing for frequency shift |
| `bandLowSmoother_` | `OnePoleSmoother` | 10ms smoothing for band low frequency |
| `bandHighSmoother_` | `OnePoleSmoother` | 10ms smoothing for band high frequency |
| `mixSmoother_` | `OnePoleSmoother` | 10ms smoothing for mix |

#### Constants

| Constant | Type | Value | Description |
|----------|------|-------|-------------|
| `kMinDownsampleFactor` | `float` | `2.0f` | Minimum downsample factor |
| `kMaxDownsampleFactor` | `float` | `32.0f` | Maximum downsample factor |
| `kMinFrequencyShiftHz` | `float` | `-5000.0f` | Minimum frequency shift |
| `kMaxFrequencyShiftHz` | `float` | `5000.0f` | Maximum frequency shift |
| `kMinBandFrequencyHz` | `float` | `20.0f` | Minimum band boundary frequency |
| `kSmoothingTimeMs` | `float` | `10.0f` | Parameter smoothing time constant |
| `kDefaultDownsampleFactor` | `float` | `2.0f` | Default downsample factor |
| `kDefaultMix` | `float` | `1.0f` | Default mix (full wet) |

#### Validation Rules

1. **Downsample factor**: Clamped to [2.0, 32.0]
2. **Frequency shift**: Clamped to [-5000, +5000] Hz
3. **Band low frequency**: Clamped to [20, sampleRate * 0.45] Hz
4. **Band high frequency**: Clamped to [20, sampleRate * 0.45] Hz
5. **Band relationship**: Low frequency MUST be <= high frequency
6. **Mix**: Clamped to [0.0, 1.0]

#### State Transitions

```
┌─────────────────────────────────────────────────────────────┐
│                      UNPREPARED                              │
│  (prepared_ = false, all methods return input unchanged)    │
└─────────────────┬───────────────────────────────────────────┘
                  │ prepare(sampleRate, maxBlockSize)
                  ▼
┌─────────────────────────────────────────────────────────────┐
│                        PREPARED                              │
│  (prepared_ = true, processing active)                      │
│                                                             │
│  Transitions:                                               │
│  - setDownsampleFactor() → updates target, smoother active  │
│  - setFrequencyShift() → updates target, smoother active    │
│  - setAliasingBand() → updates targets, smoothers active    │
│  - setMix() → updates target, smoother active               │
│  - reset() → clears all state, remains PREPARED             │
│  - prepare(newRate) → reinitializes for new sample rate     │
└─────────────────────────────────────────────────────────────┘
```

### SampleRateReducer (MODIFIED)

Existing primitive with extended range.

**Layer**: 1 (DSP Primitive)
**Location**: `dsp/include/krate/dsp/primitives/sample_rate_reducer.h`

#### Modified Constants

| Constant | Old Value | New Value | Description |
|----------|-----------|-----------|-------------|
| `kMaxReductionFactor` | `8.0f` | `32.0f` | Maximum reduction factor |

No other changes to this class.

## Relationships

```
┌──────────────────────────────────────────────────────────────────┐
│                        AliasingEffect                             │
│                         (Layer 2)                                 │
└────────────┬─────────────────┬────────────────┬──────────────────┘
             │                 │                │
    ┌────────▼────────┐  ┌────▼────┐   ┌───────▼───────┐
    │ FrequencyShifter │  │ Biquad  │   │SampleRateReducer│
    │   (Layer 2)      │  │Cascade  │   │   (Layer 1)    │
    └────────┬─────────┘  │(Layer 1)│   └────────────────┘
             │            └─────────┘
    ┌────────▼────────┐
    │HilbertTransform │
    │   (Layer 1)     │
    └─────────────────┘
```

Note: FrequencyShifter is also Layer 2 but is used as a composed component within AliasingEffect. This is valid - Layer 2 processors can compose other Layer 2 processors as long as there are no circular dependencies.

## Memory Layout

All components are value types (stack-allocated within AliasingEffect):

```cpp
class AliasingEffect {
    // Filters (no heap allocation)
    BiquadCascade<2> bandHighpass_;   // ~128 bytes (4 Biquads * ~32 bytes)
    BiquadCascade<2> bandLowpass_;    // ~128 bytes

    // Processor (heap allocation in prepare() for LFO wavetables)
    FrequencyShifter frequencyShifter_;  // ~2KB (includes HilbertTransform, LFO)

    // Primitive (no heap allocation)
    SampleRateReducer sampleRateReducer_;  // ~16 bytes

    // Smoothers (no heap allocation)
    OnePoleSmoother downsampleSmoother_;   // ~20 bytes
    OnePoleSmoother shiftSmoother_;        // ~20 bytes
    OnePoleSmoother bandLowSmoother_;      // ~20 bytes
    OnePoleSmoother bandHighSmoother_;     // ~20 bytes
    OnePoleSmoother mixSmoother_;          // ~20 bytes

    // Parameters
    double sampleRate_;                    // 8 bytes
    float downsampleFactor_;               // 4 bytes
    float frequencyShiftHz_;               // 4 bytes
    float bandLowHz_;                      // 4 bytes
    float bandHighHz_;                     // 4 bytes
    float mix_;                            // 4 bytes
    bool prepared_;                        // 1 byte (+ padding)
};

// Total: ~2.5KB (dominated by FrequencyShifter)
```

## Processing Data Flow

```
Input Sample
     │
     ▼
┌────────────────────────────────────────────────────────────────┐
│                    NaN/Inf Check                               │
│  if (isNaN || isInf) { reset(); return 0.0f; }                │
└────────────────────────────────────────────────────────────────┘
     │ (valid input)
     ├────────────────────────────────────────────────────────────┐
     │                                                            │
     ▼                                                            │
┌─────────────────────────────────────────────────────┐          │
│              Band Isolation                          │          │
│  bandSignal = LP(HP(input))                         │          │
│  nonBandSignal = input - bandSignal                 │          │
└─────────────────────────────────────────────────────┘          │
     │                                                            │
     ▼ (bandSignal)                                               │
┌─────────────────────────────────────────────────────┐          │
│           Frequency Shift                            │          │
│  shiftedBand = shifter.process(bandSignal)          │          │
└─────────────────────────────────────────────────────┘          │
     │                                                            │
     ▼                                                            │
┌─────────────────────────────────────────────────────┐          │
│           Downsample (Sample-and-Hold)               │          │
│  aliasedBand = reducer.process(shiftedBand)         │          │
└─────────────────────────────────────────────────────┘          │
     │                                                            │
     ▼                                                            │
┌─────────────────────────────────────────────────────┐          │
│              Recombine                               │          │
│  wetSignal = nonBandSignal + aliasedBand            │◄─────────┘
└─────────────────────────────────────────────────────┘  (nonBandSignal)
     │
     ▼
┌─────────────────────────────────────────────────────┐
│                Mix with Dry                          │
│  output = (1-mix)*input + mix*wetSignal             │
└─────────────────────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────────────────────┐
│              Denormal Flush                          │
│  output = flushDenormal(output)                     │
└─────────────────────────────────────────────────────┘
     │
     ▼
  Output Sample
```

## Test Scenarios Mapping

| Requirement | Entity | Field/Method | Test Strategy |
|-------------|--------|--------------|---------------|
| FR-001 | AliasingEffect | prepare() | Verify initialization at multiple sample rates |
| FR-002 | AliasingEffect | reset() | Verify state cleared, no reallocation |
| FR-004 | AliasingEffect | setDownsampleFactor() | Verify clamping and smoothing |
| FR-008 | AliasingEffect | setFrequencyShift() | Verify clamping and smoothing |
| FR-013 | AliasingEffect | setAliasingBand() | Verify clamping and low <= high |
| FR-019 | AliasingEffect | setMix() | Verify clamping and smoothing |
| FR-023 | AliasingEffect | process() | Block processing, in-place |
| FR-025 | AliasingEffect | process() | NaN/Inf input handling |
| SC-001 | AliasingEffect | process() | Aliased frequency detection |
| SC-002 | AliasingEffect | process() | Band isolation attenuation measurement |
| SC-009 | AliasingEffect | bandHighpass_/bandLowpass_ | 24dB/oct rolloff verification |
