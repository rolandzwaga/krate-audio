# Data Model: Ring Saturation Primitive

**Feature**: 108-ring-saturation
**Date**: 2026-01-26
**Layer**: 1 (Primitives)

## Entities

### RingSaturation

Main primitive class implementing self-modulation distortion.

**Purpose**: Creates metallic, bell-like character through self-modulation that generates signal-coherent inharmonic sidebands.

**State:**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `shaper_` | `Waveshaper` | Tanh, drive=1.0 | Active saturation shaper |
| `dcBlocker_` | `DCBlocker` | 10Hz cutoff | DC offset removal |
| `crossfade_` | `CrossfadeState` | inactive | Curve transition state |
| `drive_` | `float` | 1.0 | Drive parameter [0, unbounded) |
| `depth_` | `float` | 1.0 | Modulation depth [0, 1] |
| `stages_` | `int` | 1 | Number of stages [1, 4] |
| `sampleRate_` | `double` | 44100.0 | Stored sample rate |
| `prepared_` | `bool` | false | Preparation flag |

**Constants:**

| Constant | Value | Description |
|----------|-------|-------------|
| `kMinStages` | 1 | Minimum stage count |
| `kMaxStages` | 4 | Maximum stage count |
| `kDCBlockerCutoffHz` | 10.0f | DC blocker cutoff frequency |
| `kCrossfadeTimeMs` | 10.0f | Curve change crossfade duration |
| `kSoftLimitScale` | 2.0f | Soft limiter output bound |

**Relationships:**
- Composes: 1 Waveshaper (active), 1 DCBlocker
- During crossfade: temporarily holds 2nd Waveshaper (old curve)

---

### CrossfadeState (Internal)

Internal struct managing click-free curve transitions.

**Purpose**: Maintains state for crossfading between old and new saturation curves.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `oldShaper` | `Waveshaper` | - | Previous curve's shaper |
| `ramp` | `LinearRamp` | - | Crossfade position [0,1] |
| `active` | `bool` | false | Whether crossfade in progress |

**Lifecycle:**
1. Inactive by default
2. On `setSaturationCurve()`: copy current shaper to `oldShaper`, configure ramp, set `active=true`
3. During processing: blend old and new outputs by ramp position
4. When `ramp.isComplete()`: set `active=false`

---

## Parameters

### Drive (FR-008)

| Property | Value |
|----------|-------|
| Range | [0.0, unbounded), typical [0.1, 10.0] |
| Default | 1.0 |
| Setter | `setDrive(float)` |
| Getter | `getDrive()` |

**Validation:** Negative values are clamped to 0.0.

**Behavior:**
- Low drive (0.1): Nearly linear, subtle effect
- Unity drive (1.0): Standard ring saturation
- High drive (>5): Aggressive saturation, dense harmonics
- Zero drive: Output = input * (1 - depth)

### Modulation Depth (FR-009)

| Property | Value |
|----------|-------|
| Range | [0.0, 1.0] |
| Default | 1.0 |
| Setter | `setModulationDepth(float)` |
| Getter | `getModulationDepth()` |

**Behavior:**
- 0.0: Dry signal only (no ring modulation effect)
- 0.5: 50% ring modulation term added to input
- 1.0: Full ring modulation effect

**Important:** Depth scales only the ring modulation term, not a wet/dry blend:
```cpp
output = input + (ring_mod_term) * depth;  // NOT lerp(dry, wet, depth)
```

### Stages (FR-010, FR-011)

| Property | Value |
|----------|-------|
| Range | [1, 4] (clamped) |
| Default | 1 |
| Setter | `setStages(int)` |
| Getter | `getStages()` |

**Behavior:**
- 1: Single self-modulation pass
- 2-4: Multiple passes in series, increasing harmonic complexity

### Saturation Curve (FR-005, FR-006)

| Property | Value |
|----------|-------|
| Type | `WaveshapeType` enum |
| Default | `WaveshapeType::Tanh` |
| Setter | `setSaturationCurve(WaveshapeType)` |
| Getter | `getSaturationCurve()` |

**Available curves** (from Waveshaper):
- Tanh: Warm, smooth saturation
- Atan: Slightly brighter
- Cubic: 3rd harmonic dominant
- Quintic: Smoother knee
- ReciprocalSqrt: Fast tanh alternative
- Erf: Tape-like with spectral nulls
- HardClip: Harsh, all harmonics
- Diode: Subtle even harmonics (unbounded)
- Tube: Warm even harmonics

**Runtime switching:** 10ms crossfade prevents clicks

---

## Processing Flow

```
                    +-------------------------------------------+
                    |           RingSaturation.process()        |
                    +-------------------------------------------+
                                        |
                                        v
                    +-------------------------------------------+
                    |          Input Validation (NaN/Inf)       |
                    +-------------------------------------------+
                                        |
                                        v
                    +-------------------------------------------+
                    |          Stage 1: Self-Modulation         |
                    |  out = in + (in * sat(in*drive) - in)*d   |
                    +-------------------------------------------+
                                        |
                        +---------------+---------------+
                        |  if stages > 1                |
                        v                               |
                    +-------------------------------------------+
                    |          Stage 2-N: Repeat Formula        |
                    +-------------------------------------------+
                                        |
                                        v
                    +-------------------------------------------+
                    |      Soft Limiter: 2*tanh(x/2)            |
                    +-------------------------------------------+
                                        |
                                        v
                    +-------------------------------------------+
                    |      DC Blocker: 10Hz Highpass            |
                    +-------------------------------------------+
                                        |
                                        v
                    +-------------------------------------------+
                    |              Output                       |
                    +-------------------------------------------+
```

---

## State Transitions

### Preparation State

```
[Unprepared] --prepare()--> [Prepared] --reset()--> [Prepared, cleared]
     |                           |
     |                           +--prepare()--> [Prepared, reconfigured]
     |
     +--process()--> [Returns input unchanged]
```

### Crossfade State

```
[Inactive] --setSaturationCurve()--> [Active, ramp at 0.0]
                                            |
                                    process() each sample
                                            |
                                            v
                                    [Active, ramp advancing]
                                            |
                                    ramp.isComplete()
                                            |
                                            v
                                    [Inactive, new curve active]
```

---

## Memory Layout

```cpp
class RingSaturation {
    // Active processing state (24 bytes)
    Waveshaper shaper_;         // 24 bytes (type + drive + asymmetry)

    // DC Blocker (32 bytes)
    DCBlocker dcBlocker_;       // 32 bytes (R, x1, y1, prepared, sampleRate, cutoff)

    // Crossfade state (~64 bytes when inactive, more during crossfade)
    struct CrossfadeState {
        Waveshaper oldShaper;   // 24 bytes
        LinearRamp ramp;        // 20 bytes
        bool active;            // 1 byte + padding
    } crossfade_;

    // Parameters (16 bytes)
    float drive_;               // 4 bytes
    float depth_;               // 4 bytes
    int stages_;                // 4 bytes
    bool prepared_;             // 1 byte + padding

    // Configuration (8 bytes)
    double sampleRate_;         // 8 bytes

    // Total: ~144 bytes (within Layer 1 memory budget)
};
```

---

## Validation Rules

| Parameter | Validation | Behavior |
|-----------|------------|----------|
| drive | >= 0.0 | No clamping (unbounded positive) |
| depth | [0.0, 1.0] | Clamped to range |
| stages | [1, 4] | Clamped to range |
| saturationCurve | Valid WaveshapeType | Invalid values default to Tanh |
| sampleRate | >= 1000.0 | Clamped to minimum |
