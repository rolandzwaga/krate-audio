# Data Model: ADSR Envelope Generator

**Feature**: 032-adsr-envelope-generator | **Date**: 2026-02-06

---

## Entities

### ADSRStage (enum class)

Five-state finite state machine for the envelope lifecycle.

```cpp
enum class ADSRStage : uint8_t {
    Idle = 0,    // Not active, output = 0.0
    Attack,      // Rising toward peak
    Decay,       // Falling toward sustain
    Sustain,     // Holding at sustain level
    Release      // Falling toward 0.0
};
```

**State Transitions**:
```
                gate ON (hard retrigger)
           +-------------------------------+
           |                               |
           v                               |
  Idle --gate ON--> Attack --output>=peak--> Decay --output<=sustain--> Sustain
                      ^                                                    |
                      |                               gate OFF             |
                      +-- gate ON (hard retrigger) ---+                    |
                                                      |                    |
                                                      v                    |
                                                   Release <---gate OFF---+
                                                      |
                                            output<threshold
                                                      |
                                                      v
                                                    Idle
```

**Legato mode variations**:
- gate ON during Attack/Decay/Sustain: no action
- gate ON during Release: return to Sustain (or Decay if above sustain level)

---

### EnvCurve (enum class)

Curve shape options for time-based stages.

```cpp
enum class EnvCurve : uint8_t {
    Exponential = 0,  // Fast initial change, gradual approach (default)
    Linear,           // Constant rate of change
    Logarithmic       // Slow initial change, accelerating finish
};
```

**Mapping to target ratios** (EarLevel Engineering approach):

| EnvCurve | Attack targetRatio | Decay/Release targetRatio |
|----------|-------------------|--------------------------|
| Exponential | 0.3 | 0.0001 |
| Linear | 100.0 | 100.0 |
| Logarithmic | 0.0001 | 0.3 |

---

### RetriggerMode (enum class)

Controls behavior when gate-on occurs during an active envelope.

```cpp
enum class RetriggerMode : uint8_t {
    Hard = 0,   // Restart attack from current level (default)
    Legato      // Continue from current stage/level
};
```

---

### ADSREnvelope (class)

The main envelope generator class.

**Member Fields**:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `sampleRate_` | `float` | `44100.0f` | Sample rate in Hz |
| `output_` | `float` | `0.0f` | Current envelope output value |
| `stage_` | `ADSRStage` | `Idle` | Current stage |
| `attackTimeMs_` | `float` | `10.0f` | Attack time (0.1 - 10000 ms) |
| `decayTimeMs_` | `float` | `50.0f` | Decay time (0.1 - 10000 ms) |
| `sustainLevel_` | `float` | `0.5f` | Sustain level (0.0 - 1.0) |
| `releaseTimeMs_` | `float` | `100.0f` | Release time (0.1 - 10000 ms) |
| `attackCurve_` | `EnvCurve` | `Exponential` | Attack curve shape |
| `decayCurve_` | `EnvCurve` | `Exponential` | Decay curve shape |
| `releaseCurve_` | `EnvCurve` | `Exponential` | Release curve shape |
| `retriggerMode_` | `RetriggerMode` | `Hard` | Retrigger behavior |
| `velocityScalingEnabled_` | `bool` | `false` | Velocity scaling on/off |
| `velocity_` | `float` | `1.0f` | Current velocity (0.0 - 1.0) |
| `peakLevel_` | `float` | `1.0f` | Current peak level (velocity-scaled) |
| `attackCoef_` | `float` | `0.0f` | One-pole coefficient for attack |
| `attackBase_` | `float` | `0.0f` | One-pole base for attack |
| `decayCoef_` | `float` | `0.0f` | One-pole coefficient for decay |
| `decayBase_` | `float` | `0.0f` | One-pole base for decay |
| `releaseCoef_` | `float` | `0.0f` | One-pole coefficient for release |
| `releaseBase_` | `float` | `0.0f` | One-pole base for release |
| `sustainSmoothCoef_` | `float` | `0.0f` | One-pole coefficient for 5ms sustain smoothing |
| `gateOn_` | `bool` | `false` | Internal state tracking the current gate value (set by the public `gate(bool on)` method) |

**Validation Rules**:

| Parameter | Min | Max | Clamping |
|-----------|-----|-----|----------|
| Attack time | 0.1 ms | 10000 ms | Clamp to range |
| Decay time | 0.1 ms | 10000 ms | Clamp to range |
| Sustain level | 0.0 | 1.0 | Clamp to range |
| Release time | 0.1 ms | 10000 ms | Clamp to range |
| Velocity | 0.0 | 1.0 | Clamp to range |
| Sample rate | > 0 | - | Guard in prepare() |

**Computed Constants**:

| Constant | Value | Description |
|----------|-------|-------------|
| `kEnvelopeIdleThreshold` | `1e-4f` (0.0001) | Release-to-Idle transition threshold |
| `kMinEnvelopeTimeMs` | `0.1f` | Minimum time for A/D/R stages |
| `kMaxEnvelopeTimeMs` | `10000.0f` | Maximum time for A/D/R stages |
| `kSustainSmoothTimeMs` | `5.0f` | Smoothing time for sustain level changes |
| `kDefaultTargetRatioA` | `0.3f` | Exponential attack target ratio |
| `kDefaultTargetRatioDR` | `0.0001f` | Exponential decay/release target ratio |
| `kLinearTargetRatio` | `100.0f` | Linear approximation target ratio |

---

## Coefficient Calculation

The coefficient calculation is a private utility function:

```cpp
struct StageCoefficients {
    float coef;   // Multiplier for current output
    float base;   // Additive constant
};

static StageCoefficients calcCoefficients(
    float timeMs, float sampleRate,
    float startLevel, float endLevel,
    float targetRatio) noexcept;
```

**Formula**:
```
rate = timeMs * 0.001f * sampleRate  // Convert ms to samples
coef = exp(-log((1.0 + targetRatio) / targetRatio) / rate)
base = (endLevel + targetRatio) * (1.0 - coef)  // For rising stages
base = (endLevel - targetRatio) * (1.0 - coef)  // For falling stages
```

Note: The `+targetRatio` vs `-targetRatio` depends on direction:
- Attack (rising): target = peak + targetRatio (overshoot above peak)
- Decay (falling): target = sustain - targetRatio (undershoot below sustain)
- Release (falling): target = 0.0 - targetRatio (undershoot below zero)

---

## State Machine Detailed Transitions

### Idle -> Attack
- **Trigger**: `gate(true)` called (hard retrigger mode), or first gate-on
- **Action**: Calculate attack coefficients, set stage to Attack
- **Starting level**: Current output_ (0.0 for first gate, or current level for retrigger)

### Attack -> Decay
- **Trigger**: `output_ >= peakLevel_`
- **Action**: Clamp output to peakLevel_, calculate decay coefficients, set stage to Decay

### Decay -> Sustain
- **Trigger**: `output_ <= sustainLevel_ * peakLevel_`
- **Action**: Set stage to Sustain

### Sustain (steady state)
- **Trigger**: Gate remains on
- **Action**: Smooth output toward `sustainLevel_ * peakLevel_` using 5ms smoother

### Any -> Release
- **Trigger**: `gate(false)` called while in Attack, Decay, or Sustain
- **Action**: Calculate release coefficients from current output_, set stage to Release

### Release -> Idle
- **Trigger**: `output_ < kEnvelopeIdleThreshold`
- **Action**: Set output_ to 0.0, set stage to Idle

### Reset
- **Trigger**: `reset()` called
- **Action**: Set output_ to 0.0, set stage to Idle, clear gate state
