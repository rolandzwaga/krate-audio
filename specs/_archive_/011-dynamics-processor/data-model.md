# Data Model: DynamicsProcessor

**Feature**: 011-dynamics-processor
**Date**: 2025-12-23

## Class: DynamicsProcessor

Layer 2 DSP Processor for dynamics control (compression/limiting).

### Parameters

| Parameter | Type | Range | Default | Unit | Description |
|-----------|------|-------|---------|------|-------------|
| threshold | float | -60.0 to 0.0 | -20.0 | dB | Level above which compression begins |
| ratio | float | 1.0 to 100.0 | 4.0 | :1 | Compression ratio (100.0 = limiter) |
| kneeWidth | float | 0.0 to 24.0 | 0.0 | dB | Soft knee width (0 = hard knee) |
| attackTime | float | 0.1 to 500.0 | 10.0 | ms | Time to reach 63% of gain reduction |
| releaseTime | float | 1.0 to 5000.0 | 100.0 | ms | Time to return 63% from gain reduction |
| makeupGain | float | -24.0 to 24.0 | 0.0 | dB | Output gain compensation |
| autoMakeup | bool | false/true | false | - | Auto-calculate makeup from threshold/ratio |
| lookahead | float | 0.0 to 10.0 | 0.0 | ms | Lookahead time (0 = disabled) |
| sidechainEnabled | bool | false/true | false | - | Enable sidechain highpass filter |
| sidechainCutoff | float | 20.0 to 500.0 | 80.0 | Hz | Sidechain filter cutoff frequency |
| detectionMode | enum | RMS/Peak | RMS | - | Level detection algorithm |

### State Variables

| Variable | Type | Description |
|----------|------|-------------|
| currentGainReduction | float | Current gain reduction in dB (0 to -inf) |
| envelopeFollower | EnvelopeFollower | Level detection component |
| gainSmoother | OnePoleSmoother | Smooths gain changes to prevent clicks |
| lookaheadDelay | DelayLine | Audio delay for lookahead |
| sidechainFilter | Biquad | Highpass filter for sidechain |
| sampleRate | float | Current sample rate in Hz |
| lookaheadSamples | size_t | Lookahead in samples |

### Enumerations

```cpp
enum class DetectionMode : uint8_t {
    RMS = 0,    // RMS detection (average-responding)
    Peak = 1    // Peak detection (transient-responding)
};
```

### Derived Values

| Value | Formula | Description |
|-------|---------|-------------|
| autoMakeupGain | `-threshold * (1 - 1/ratio)` | Makeup for 0dB input |
| effectiveRatio | `ratio == 100.0f ? INFINITY : ratio` | Infinity for limiter mode |
| kneeStart | `threshold - kneeWidth/2` | Start of knee region |
| kneeEnd | `threshold + kneeWidth/2` | End of knee region |

### Relationships

```
DynamicsProcessor
├── has-a EnvelopeFollower (level detection)
├── has-a OnePoleSmoother (gain smoothing)
├── has-a DelayLine (lookahead buffer)
└── has-a Biquad (sidechain filter - optional)
```

### Gain Reduction Computation

```cpp
/// @brief Compute gain reduction in dB for a given input level
/// @param inputLevel_dB Input level in dB (from envelope follower)
/// @return Gain reduction in dB (always <= 0)
float computeGainReduction(float inputLevel_dB) const noexcept {
    // Below knee region: no compression
    if (inputLevel_dB <= kneeStart_) {
        return 0.0f;
    }

    // Above knee region: full compression
    if (inputLevel_dB >= kneeEnd_) {
        return (inputLevel_dB - threshold_) * (1.0f - 1.0f / ratio_);
    }

    // In knee region: quadratic interpolation
    const float x = inputLevel_dB - kneeStart_;
    return (1.0f - 1.0f / ratio_) * (x * x) / (2.0f * kneeWidth_);
}
```

### Processing Flow

1. **Input Sanitization**: Handle NaN/Inf inputs
2. **Sidechain Filter** (optional): Apply highpass to detection path
3. **Level Detection**: EnvelopeFollower processes (filtered) input
4. **dB Conversion**: Convert envelope to dB using gainToDb()
5. **Gain Computation**: Calculate gain reduction using threshold/ratio/knee
6. **Gain Smoothing**: OnePoleSmoother prevents abrupt changes
7. **Lookahead Delay**: DelayLine delays audio path (if enabled)
8. **Gain Application**: Multiply delayed audio by gain reduction
9. **Makeup Gain**: Apply makeup gain (manual or auto)
10. **Output**: Return processed sample, update metering

### Thread Safety

- All public methods are `noexcept`
- No memory allocation in process path
- Parameter changes are atomic or smoothed
- Safe for single-threaded audio callback use
