# Data Model: TapeSaturator Processor

**Feature Branch**: `062-tape-saturator`
**Date**: 2026-01-14
**Layer**: 2 (Processors)

## Entity Diagram

```
TapeSaturator
├── Enumerations
│   ├── TapeModel { Simple, Hysteresis }
│   └── HysteresisSolver { RK2, RK4, NR4, NR8 }
├── Parameters (user-facing)
│   ├── model_ : TapeModel
│   ├── solver_ : HysteresisSolver
│   ├── driveDb_ : float [-24, +24]
│   ├── saturation_ : float [0, 1]
│   ├── bias_ : float [-1, +1]
│   └── mix_ : float [0, 1]
├── Expert J-A Parameters
│   ├── ja_a_ : float (default: 22.0)
│   ├── ja_alpha_ : float (default: 1.6e-11)
│   ├── ja_c_ : float (default: 1.7)
│   ├── ja_k_ : float (default: 27.0)
│   └── ja_Ms_ : float (default: 350000.0)
├── Internal State
│   ├── M_ : float (magnetization state)
│   ├── H_prev_ : float (previous field)
│   └── TScale_ : float (sample rate compensation)
├── Smoothers (Layer 1: OnePoleSmoother)
│   ├── driveSmoother_
│   ├── saturationSmoother_
│   ├── biasSmoother_
│   └── mixSmoother_
├── Crossfade State
│   ├── crossfadeActive_ : bool
│   ├── crossfadePosition_ : float
│   ├── crossfadeIncrement_ : float
│   └── previousModel_ : TapeModel
├── Filters (Layer 1: Biquad)
│   ├── preEmphasis_ : Biquad (HighShelf +9dB @ 3kHz)
│   └── deEmphasis_ : Biquad (HighShelf -9dB @ 3kHz)
├── DC Blocker (Layer 1: DCBlocker)
│   └── dcBlocker_ : DCBlocker (10Hz cutoff)
└── Configuration
    ├── sampleRate_ : double
    ├── prepared_ : bool
    └── maxBlockSize_ : size_t
```

## Enumerations

### TapeModel

```cpp
enum class TapeModel : uint8_t {
    Simple = 0,     ///< tanh saturation + pre/de-emphasis
    Hysteresis = 1  ///< Jiles-Atherton magnetic model
};
```

### HysteresisSolver

```cpp
enum class HysteresisSolver : uint8_t {
    RK2 = 0,  ///< Runge-Kutta 2nd order (fastest)
    RK4 = 1,  ///< Runge-Kutta 4th order (default)
    NR4 = 2,  ///< Newton-Raphson 4 iterations
    NR8 = 3   ///< Newton-Raphson 8 iterations (most accurate)
};
```

## Parameter Ranges and Defaults

| Parameter | Type | Range | Default | Unit |
|-----------|------|-------|---------|------|
| model | TapeModel | {Simple, Hysteresis} | Simple | - |
| solver | HysteresisSolver | {RK2, RK4, NR4, NR8} | RK4 | - |
| drive | float | [-24, +24] | 0.0 | dB |
| saturation | float | [0, 1] | 0.5 | - |
| bias | float | [-1, +1] | 0.0 | - |
| mix | float | [0, 1] | 1.0 | - |

## Expert J-A Parameters

| Parameter | Symbol | Default | Physical Meaning |
|-----------|--------|---------|------------------|
| ja_a | a | 22.0 | Langevin function shape |
| ja_alpha | alpha | 1.6e-11 | Inter-domain coupling |
| ja_c | c | 1.7 | Reversibility coefficient |
| ja_k | k | 27.0 | Pinning coefficient (coercivity) |
| ja_Ms | Ms | 350000.0 | Saturation magnetization |

## Dependencies (Layer 0 and 1)

| Component | Layer | Header | Usage |
|-----------|-------|--------|-------|
| dbToGain() | 0 | core/db_utils.h | Convert drive dB to linear |
| flushDenormal() | 0 | core/db_utils.h | Prevent denormal CPU spikes |
| equalPowerGains() | 0 | core/crossfade_utils.h | Model crossfade |
| crossfadeIncrement() | 0 | core/crossfade_utils.h | Calculate crossfade rate |
| Sigmoid::tanh() | 0 | core/sigmoid.h | Simple model saturation |
| FastMath::fastTanh() | 0 | core/fast_math.h | Langevin function |
| Biquad | 1 | primitives/biquad.h | Pre/de-emphasis filters |
| DCBlocker | 1 | primitives/dc_blocker.h | DC offset removal |
| OnePoleSmoother | 1 | primitives/smoother.h | Parameter smoothing |

## Signal Flow

### Simple Model

```
Input
  ↓
[Drive Gain (smoothed)]
  ↓
[Pre-Emphasis: HighShelf +9dB @ 3kHz]
  ↓
[tanh saturation blended with linear by saturation param]
  ↓
[De-Emphasis: HighShelf -9dB @ 3kHz]
  ↓
[DC Blocker @ 10Hz]
  ↓
[Mix Blend (smoothed)]
  ↓
Output
```

### Hysteresis Model

```
Input
  ↓
[Drive Gain (smoothed)]
  ↓
[Bias Offset (smoothed)]
  ↓
[Jiles-Atherton Hysteresis (selected solver)]
  │  ├── M state variable maintained
  │  └── T-scaling applied for sample rate
  ↓
[Saturation blends J-A intensity via Ms scaling]
  ↓
[DC Blocker @ 10Hz]
  ↓
[Mix Blend (smoothed)]
  ↓
Output
```

### Model Crossfade (when model changes)

```
[Old Model Output] ──┐
                     ├── [Equal Power Crossfade (~10ms)]
[New Model Output] ──┘
                           ↓
                       Output
```

## State Transitions

### Prepared State

```
Unprepared ──[prepare(sampleRate, maxBlockSize)]──> Prepared
     ↑                                                   │
     └──────────────────[reset()]────────────────────────┘
                    (optional, clears filters)
```

### Model Crossfade State

```
Inactive ──[setModel(newModel) where newModel != currentModel]──> Active
    ↑                                                                │
    └─────────[crossfadePosition >= 1.0]─────────────────────────────┘
```

## Validation Rules

1. **Drive**: Clamped to [-24, +24] dB
2. **Saturation**: Clamped to [0, 1]
3. **Bias**: Clamped to [-1, +1]
4. **Mix**: Clamped to [0, 1]
5. **Sample Rate**: Must be >= 1000 Hz (enforced by Layer 1 components)
6. **J-A Parameters**: No explicit clamping (expert mode), but validated for sanity

## Memory Layout Estimation

| Component | Size (bytes) | Notes |
|-----------|--------------|-------|
| Parameters (8 floats) | 32 | 4 bytes each |
| J-A params (5 floats) | 20 | Expert mode |
| State (M, H_prev, TScale) | 12 | |
| Smoothers (4x OnePoleSmoother) | ~80 | 20 bytes each estimate |
| Crossfade state | 16 | bool + 3 floats |
| Biquad x2 | ~64 | 32 bytes each estimate |
| DCBlocker | ~24 | |
| Config | 24 | double + bool + size_t |
| **Total** | **~272 bytes** | Per instance |

No dynamic allocation required. All memory is stack/member allocated.
