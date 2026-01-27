# Data Model: MidSideProcessor

**Feature**: 014-midside-processor
**Date**: 2025-12-24

## Entity: MidSideProcessor

A Layer 2 DSP processor for stereo Mid/Side encoding, decoding, and manipulation.

### State Variables

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| sampleRate_ | float | > 0 | 44100.0f | Current sample rate (Hz) |
| midGainDb_ | float | [-96, +24] | 0.0f | Target mid channel gain (dB) |
| sideGainDb_ | float | [-96, +24] | 0.0f | Target side channel gain (dB) |
| width_ | float | [0.0, 2.0] | 1.0f | Width factor (0=mono, 1=normal, 2=wide) |
| soloMid_ | bool | true/false | false | Solo mid channel flag |
| soloSide_ | bool | true/false | false | Solo side channel flag |

### Smoothers (OnePoleSmoother instances)

| Field | Purpose | Smooth Time |
|-------|---------|-------------|
| midGainSmoother_ | Smooth mid gain changes | 5-10ms |
| sideGainSmoother_ | Smooth side gain changes | 5-10ms |
| widthSmoother_ | Smooth width changes | 5-10ms |
| soloMidSmoother_ | Smooth solo mid transitions | 5-10ms |
| soloSideSmoother_ | Smooth solo side transitions | 5-10ms |

### Constants

| Constant | Type | Value | Description |
|----------|------|-------|-------------|
| kDefaultSmoothTimeMs | float | 5.0f | Default parameter smoothing time |
| kMinGainDb | float | -96.0f | Minimum gain value |
| kMaxGainDb | float | +24.0f | Maximum gain value |
| kMinWidth | float | 0.0f | Mono (no side channel) |
| kMaxWidth | float | 2.0f | Maximum stereo width |
| kDefaultWidth | float | 1.0f | Unity width (unmodified) |

## Formulas

### Encoding (L/R to M/S)

```
Mid  = (Left + Right) / 2
Side = (Left - Right) / 2
```

### Decoding (M/S to L/R)

```
Left  = Mid + Side
Right = Mid - Side
```

### Width Scaling

```
Side_scaled = Side * width
```

Where:
- width = 0.0: Mono (Side completely removed)
- width = 1.0: Unity (original stereo image)
- width = 2.0: Maximum width (Side doubled)

### Gain Application

```
Mid_processed  = Mid * linearGain(midGainDb)
Side_processed = Side * linearGain(sideGainDb) * width
```

### Solo Logic

```
if soloMid:  Side = 0  (output Mid to both L/R)
if soloSide: Mid = 0   (output Side to both L/R, phase inverted R)
```

## State Transitions

### Initialization Flow

```
Constructor → default state (unity gain, unity width, no solo)
    │
    ▼
prepare(sampleRate, maxBlockSize) → smoothers initialized
    │
    ▼
Ready for process()
```

### Parameter Update Flow

```
setWidth/setMidGain/setSideGain/setSoloMid/setSoloSide
    │
    ▼
Store target value
    │
    ▼
Smoother interpolates toward target during process()
```

### Reset Flow

```
reset()
    │
    ▼
Smoothers snap to target values (no interpolation)
Parameters retain their targets
```

## Validation Rules

| Parameter | Validation | Behavior on Invalid |
|-----------|------------|---------------------|
| width | 0.0 ≤ width ≤ 2.0 | Clamp to valid range |
| midGainDb | -96.0 ≤ gain ≤ +24.0 | Clamp to valid range |
| sideGainDb | -96.0 ≤ gain ≤ +24.0 | Clamp to valid range |
| sampleRate | > 0 | Undefined behavior |
| numSamples | > 0 | No processing |

## Thread Safety

| Operation | Thread | Safety |
|-----------|--------|--------|
| prepare() | Any | Call before processing |
| reset() | Any | Call during inactive state |
| setWidth/setGain/setSolo | Any | Thread-safe (stores target) |
| process() | Audio | Real-time safe, noexcept |
| getWidth/getGain/isSolo | Any | Thread-safe (reads current) |
