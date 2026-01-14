# Research: DiodeClipper Processor

**Feature**: 060-diode-clipper | **Date**: 2026-01-14

## Research Summary

This document consolidates research findings for implementing the DiodeClipper processor. All "NEEDS CLARIFICATION" items from the spec have been resolved through user clarification.

## Resolved Clarifications

### 1. Asymmetric Topology Implementation

**Question**: How should asymmetric topology handle positive vs negative half-cycles?

**Decision**: Use structurally different curves per polarity (Option A)

**Rationale**: This matches real diode physics where forward bias (exponential) and reverse bias (linear-ish) have fundamentally different I-V characteristics. The existing `Asymmetric::diode()` function already implements this pattern:
- Forward bias (x >= 0): `1.0f - std::exp(-x * 1.5f)` - soft exponential saturation
- Reverse bias (x < 0): `x / (1.0f - 0.5f * x)` - harder, more linear with soft limit

**Alternatives Considered**:
- Option B: Same curve with different parameters - Rejected as less physically accurate
- Option C: Bias offset approach - Rejected as not truly asymmetric

### 2. Diode Parameter Model

**Question**: Should diode parameters be fixed per type or configurable per instance?

**Decision**: Configurable per-instance with type presets (Option B)

**Rationale**: Provides flexibility for sound design while maintaining convenience of presets. Each DiodeType sets sensible defaults that can be overridden.

**Implementation**:
```cpp
// DiodeType presets provide default values
setDiodeType(DiodeType::Silicon);  // Sets forwardVoltage=0.6V, kneeSharpness=5.0

// User can then override
setForwardVoltage(0.65f);  // Custom voltage
setKneeSharpness(6.0f);    // Custom knee
```

### 3. Parameter Ranges

**Question**: What valid ranges for forwardVoltage and kneeSharpness?

**Decision**: Extended ranges for experimental sounds (Option C)

| Parameter | Range | Default (Silicon) | Units |
|-----------|-------|-------------------|-------|
| forwardVoltage | [0.05, 5.0] | 0.6 | Volts (normalized) |
| kneeSharpness | [0.5, 20.0] | 5.0 | Dimensionless |

**Rationale**: Extended ranges enable creative sound design beyond physical modeling. Values outside real-world diode specs can produce interesting distortion characters.

### 4. Output Level Management

**Question**: Automatic gain normalization or user-controlled output level?

**Decision**: User-controlled via setOutputLevel(dB) (Option B)

**Rationale**: Explicit gain staging control is preferred for professional audio workflows. Users can compensate for level changes from different diode types and drive settings.

| Parameter | Range | Default | Units |
|-----------|-------|---------|-------|
| outputLevel | [-24, +24] | 0.0 | dB |

### 5. Diode Type Change Behavior

**Question**: Instant or smoothed transition when changing DiodeType?

**Decision**: Smoothed transition over ~5ms (Option A)

**Rationale**: Prevents clicks during live parameter changes. When setDiodeType() is called, forwardVoltage and kneeSharpness targets are updated and smoothed to new values.

## Diode Type Specifications

Based on real-world diode characteristics and spec requirements:

| DiodeType | Forward Voltage | Knee Sharpness | Character |
|-----------|-----------------|----------------|-----------|
| Silicon | 0.6-0.7V | 5.0 (sharp) | Classic overdrive, balanced |
| Germanium | 0.25-0.3V | 2.0 (soft) | Warm, vintage, earliest clipping |
| LED | 1.6-2.0V | 15.0 (very hard) | Aggressive, late clipping |
| Schottky | 0.15-0.25V | 1.5 (softest) | Extremely early, subtle warmth |

### Default Values by Type

```cpp
// Silicon (default)
static constexpr float kSiliconVoltage = 0.6f;
static constexpr float kSiliconKnee = 5.0f;

// Germanium
static constexpr float kGermaniumVoltage = 0.3f;
static constexpr float kGermaniumKnee = 2.0f;

// LED
static constexpr float kLEDVoltage = 1.8f;
static constexpr float kLEDKnee = 15.0f;

// Schottky
static constexpr float kSchottkyVoltage = 0.2f;
static constexpr float kSchottkyKnee = 1.5f;
```

## Topology Implementation

### Symmetric Topology

Both polarities use identical clipping curves. Produces only odd harmonics.

```cpp
float symmetricClip(float x, float voltage, float knee) {
    // Same curve for positive and negative
    return diodeClipFunction(x, voltage, knee);
}
```

### Asymmetric Topology

Different transfer functions for positive and negative half-cycles. Produces even + odd harmonics.

Based on `Asymmetric::diode()` pattern but with configurable parameters:

```cpp
float asymmetricClip(float x, float voltage, float knee) {
    if (x >= 0.0f) {
        // Forward bias: exponential saturation
        // Scale by voltage, shape by knee
        return forwardBias(x, voltage, knee);
    } else {
        // Reverse bias: harder, more linear
        return reverseBias(x, voltage, knee);
    }
}
```

### SoftHard Topology

One polarity clips softly (low knee), other clips hard (high knee).

```cpp
float softHardClip(float x, float voltage, float softKnee, float hardKnee) {
    if (x >= 0.0f) {
        return diodeClipFunction(x, voltage, softKnee);
    } else {
        return diodeClipFunction(x, voltage, hardKnee);
    }
}
```

## Diode Transfer Function Design

### Requirements

1. Output bounded to reasonable range (not unbounded like raw Asymmetric::diode())
2. Configurable threshold (forward voltage)
3. Configurable knee sharpness
4. Smooth transition between linear and saturated regions

### Proposed Function

The transfer function should model the diode I-V characteristic:

```
For |x| < threshold: approximately linear with soft knee
For |x| >= threshold: soft saturation approaching limit
```

Using a modified tanh-based approach:

```cpp
float diodeClipFunction(float x, float voltage, float knee) {
    // Normalize input by threshold
    float normalized = x / voltage;

    // Apply knee sharpness to tanh for variable hardness
    // knee=1: very soft, knee=20: very hard
    float shaped = fastTanh(normalized * knee / 5.0f);

    // Scale output by voltage for consistent levels
    return shaped * voltage;
}
```

### Alternative: Asymmetric Exponential Model

For more accurate diode physics:

```cpp
float forwardBias(float x, float voltage, float knee) {
    // Exponential saturation: 1 - exp(-k*x/V)
    float k = knee * 0.3f;  // Scale knee to useful range
    return voltage * (1.0f - std::exp(-k * x / voltage));
}

float reverseBias(float x, float voltage, float knee) {
    // Linear with soft limit
    float k = knee * 0.1f;
    return x / (1.0f - k * x / voltage);
}
```

## Signal Flow

```
Input -> [Drive Gain] -> [Diode Clipping] -> [DC Blocker] -> [Output Level] -> [Mix Blend] -> Output
```

### Processing Steps

1. **Drive Gain**: Amplify input by drive amount (dB to linear)
2. **Diode Clipping**: Apply selected topology/type transfer function
3. **DC Blocking**: Remove DC offset (required for Asymmetric/SoftHard topologies)
4. **Output Level**: Apply output gain (dB to linear)
5. **Mix Blend**: Blend dry signal with processed signal

## Parameter Smoothing Strategy

All parameters that could cause clicks are smoothed:

| Parameter | Smoothing | Time |
|-----------|-----------|------|
| Drive | OnePoleSmoother | 5ms |
| Mix | OnePoleSmoother | 5ms |
| Output Level | OnePoleSmoother | 5ms |
| Forward Voltage | OnePoleSmoother | 5ms |
| Knee Sharpness | OnePoleSmoother | 5ms |

### Implementation

```cpp
// 6 smoothers total
OnePoleSmoother driveSmoother_;
OnePoleSmoother mixSmoother_;
OnePoleSmoother outputLevelSmoother_;
OnePoleSmoother voltageSmoother_;
OnePoleSmoother kneeSmoother_;
OnePoleSmoother kneeNegSmoother_;  // For SoftHard topology
```

## DC Blocking Requirements

| Topology | DC Blocking Required | Reason |
|----------|---------------------|--------|
| Symmetric | Optional | Symmetric clipping produces minimal DC |
| Asymmetric | Required | Different curves produce DC offset |
| SoftHard | Required | Asymmetric knee produces DC offset |

**Implementation**: Always apply DC blocker after clipping for safety. Can bypass in Symmetric mode if optimization needed, but spec requires DC blocking (FR-019).

## Performance Considerations

### Target Budget

- Layer 2: < 0.5% CPU per mono instance @ 44.1kHz

### Optimizations

1. **Bypass on mix=0**: Skip all processing when fully dry (FR-015)
2. **Inline transfer functions**: Keep clipping math inline for cache efficiency
3. **Batch smoother updates**: Process smoothers at block rate when stable
4. **Use fastTanh**: For transfer function (3x faster than std::tanh)

### Expected Cost

- 6 smoothers: ~0.1% CPU
- Transfer function per sample: ~0.1% CPU
- DC blocker: ~0.05% CPU
- Mix/gain: ~0.05% CPU
- **Total**: ~0.3% CPU (within budget)

## Test Strategy

### Unit Tests

1. **Lifecycle**: prepare(), reset(), process() sequence
2. **Diode Types**: Verify each type produces distinct harmonic spectra (SC-001)
3. **Topologies**:
   - Symmetric: odd harmonics only (SC-002)
   - Asymmetric: even harmonics present (SC-003)
4. **Parameter Smoothing**: No clicks on rapid changes (SC-004)
5. **DC Blocking**: Output DC < -60dBFS (SC-006)
6. **Sample Rate Support**: 44.1k, 48k, 88.2k, 96k, 192k (SC-007)
7. **Bypass Efficiency**: mix=0 returns input unchanged (FR-015)

### Spectral Tests

Use existing spectral_analysis.h test helpers:
- Measure harmonic content for each diode type
- Verify symmetric produces only odd harmonics
- Verify asymmetric produces even + odd harmonics

## References

### Existing Code Patterns

- `SaturationProcessor`: Layer 2 processor with DC blocking and smoothing
- `TubeStage`: Processor with waveshaper, DC blocker, parameter smoothing
- `Asymmetric::diode()`: Foundation transfer function

### Literature

- Diode I-V characteristics: Shockley diode equation
- Analog distortion modeling: David Yeh, "Digital Implementation of Musical Distortion Circuits"
- ADAA techniques: (not used here, oversampling is external)
