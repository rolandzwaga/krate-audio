# Data Model: Spectral Tilt Filter

**Feature**: 082-spectral-tilt
**Date**: 2026-01-22
**Layer**: 2 (DSP Processors)

## Class Diagram

```
+--------------------------------------------------+
|                  SpectralTilt                     |
+--------------------------------------------------+
| <<constants>>                                     |
| + kMinTilt: float = -12.0f                       |
| + kMaxTilt: float = +12.0f                       |
| + kMinPivot: float = 20.0f                       |
| + kMaxPivot: float = 20000.0f                    |
| + kMinSmoothing: float = 1.0f                    |
| + kMaxSmoothing: float = 500.0f                  |
| + kDefaultSmoothing: float = 50.0f               |
| + kDefaultPivot: float = 1000.0f                 |
| + kDefaultTilt: float = 0.0f                     |
| + kMaxGainDb: float = +24.0f                     |
| + kMinGainDb: float = -48.0f                     |
| + kQ: float = 0.7071f                            |
+--------------------------------------------------+
| - filter_: Biquad                                |
| - tiltSmoother_: OnePoleSmoother                 |
| - pivotSmoother_: OnePoleSmoother                |
| - sampleRate_: double = 44100.0                  |
| - tilt_: float = 0.0f                            |
| - pivotFrequency_: float = 1000.0f               |
| - smoothingMs_: float = 50.0f                    |
| - lastSmoothedTilt_: float = 0.0f                |
| - lastSmoothedPivot_: float = 1000.0f            |
| - prepared_: bool = false                        |
+--------------------------------------------------+
| <<lifecycle>>                                     |
| + prepare(sampleRate: double): void              |
| + reset(): void noexcept                         |
+--------------------------------------------------+
| <<parameters>>                                    |
| + setTilt(dBPerOctave: float): void              |
| + setPivotFrequency(hz: float): void             |
| + setSmoothing(ms: float): void                  |
+--------------------------------------------------+
| <<processing>>                                    |
| + process(input: float): float noexcept          |
| + processBlock(buffer: float*, n: int): void     |
+--------------------------------------------------+
| <<query>>                                         |
| + getTilt(): float noexcept                      |
| + getPivotFrequency(): float noexcept            |
| + getSmoothing(): float noexcept                 |
| + isPrepared(): bool noexcept                    |
+--------------------------------------------------+
| <<internal>>                                      |
| - updateCoefficients(): void noexcept            |
| - calculateShelfGain(tilt: float): float noexcept|
+--------------------------------------------------+
```

## Dependency Diagram

```
+-------------------+
|   SpectralTilt    |  Layer 2 Processor
+-------------------+
         |
         | composes
         v
+-------------------+     +---------------------+
|      Biquad       |     |   OnePoleSmoother   |  Layer 1 Primitives
+-------------------+     +---------------------+
         |                          |
         | uses                     | uses
         v                          v
+-------------------+     +---------------------+
| BiquadCoefficients|     |      db_utils.h     |  Layer 0/1
+-------------------+     +---------------------+
         |
         | uses
         v
+-------------------+
|  math_constants.h |  Layer 0
|    db_utils.h     |
+-------------------+
```

## State Transitions

```
                    +----------------+
                    |  Uninitialized |
                    +----------------+
                           |
                           | prepare(sampleRate)
                           v
                    +----------------+
          +-------->|    Prepared    |<--------+
          |         +----------------+         |
          |                |                   |
          |                | process()         |
          |                v                   |
          |         +----------------+         |
          |         |   Processing   |---------+
          |         +----------------+  (loop)
          |                |
          |                | reset()
          |                v
          |         +----------------+
          +---------|     Reset      |
                    +----------------+
```

## Data Flow

```
Input Signal
     |
     v
+--------------------------------------------+
| SpectralTilt::process()                    |
|                                            |
|  if (!prepared_) return input  // FR-019   |
|                                            |
|  +--------------------------------------+  |
|  | Parameter Smoothing                   |  |
|  |  smoothedTilt = tiltSmoother_.process |  |
|  |  smoothedPivot = pivotSmoother_.proc  |  |
|  +--------------------------------------+  |
|                    |                       |
|                    v                       |
|  +--------------------------------------+  |
|  | Coefficient Calculation (if changed) |  |
|  |  gainDb = smoothedTilt               |  |
|  |  clampedGain = clamp(gain, min, max) |  |
|  |  coeffs = calculate(HighShelf, ...)  |  |
|  |  filter_.setCoefficients(coeffs)     |  |
|  +--------------------------------------+  |
|                    |                       |
|                    v                       |
|  +--------------------------------------+  |
|  | Filter Processing                     |  |
|  |  output = filter_.process(input)     |  |
|  +--------------------------------------+  |
|                    |                       |
+--------------------------------------------+
     |
     v
Output Signal
```

## Member Variables

### Configuration State

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| `sampleRate_` | `double` | 44100.0 | Sample rate in Hz |
| `tilt_` | `float` | 0.0f | Target tilt in dB/octave |
| `pivotFrequency_` | `float` | 1000.0f | Pivot frequency in Hz |
| `smoothingMs_` | `float` | 50.0f | Smoothing time in ms |

### Processing State

| Member | Type | Description |
|--------|------|-------------|
| `filter_` | `Biquad` | High-shelf filter for tilt |
| `tiltSmoother_` | `OnePoleSmoother` | Smooths tilt changes |
| `pivotSmoother_` | `OnePoleSmoother` | Smooths pivot changes |
| `lastSmoothedTilt_` | `float` | Last smoothed tilt (for change detection) |
| `lastSmoothedPivot_` | `float` | Last smoothed pivot (for change detection) |

### Flags

| Member | Type | Description |
|--------|------|-------------|
| `prepared_` | `bool` | True after prepare() called |

## Constants Reference

| Constant | Value | Spec Reference |
|----------|-------|----------------|
| `kMinTilt` | -12.0f | FR-002 |
| `kMaxTilt` | +12.0f | FR-002 |
| `kMinPivot` | 20.0f | FR-003, Edge Case |
| `kMaxPivot` | 20000.0f | FR-003, Edge Case |
| `kMinSmoothing` | 1.0f | FR-014 |
| `kMaxSmoothing` | 500.0f | FR-014 |
| `kDefaultSmoothing` | 50.0f | FR-014, Assumptions |
| `kDefaultPivot` | 1000.0f | Assumptions |
| `kDefaultTilt` | 0.0f | Assumptions |
| `kMaxGainDb` | +24.0f | FR-024 |
| `kMinGainDb` | -48.0f | FR-025 |
| `kQ` | 0.7071f | Research: Butterworth |
