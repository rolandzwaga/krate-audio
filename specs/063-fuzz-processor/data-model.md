# Data Model: FuzzProcessor

**Feature**: 063-fuzz-processor | **Date**: 2026-01-14

## Entity Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                             FuzzProcessor                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│  Parameters (User-Facing)                                                   │
│  ─────────────────────────                                                  │
│  type_        : FuzzType        [Germanium, Silicon]     default: Germanium │
│  fuzz_        : float           [0.0, 1.0]               default: 0.5       │
│  volumeDb_    : float           [-24.0, +24.0] dB        default: 0.0       │
│  bias_        : float           [0.0, 1.0]               default: 0.7       │
│  tone_        : float           [0.0, 1.0]               default: 0.5       │
│  octaveUp_    : bool            [false, true]            default: false     │
├─────────────────────────────────────────────────────────────────────────────┤
│  Configuration                                                              │
│  ─────────────                                                              │
│  sampleRate_  : double          Sample rate in Hz        default: 44100.0   │
│  prepared_    : bool            Ready for processing     default: false     │
├─────────────────────────────────────────────────────────────────────────────┤
│  DSP Components (Layer 1)                                                   │
│  ────────────────────────                                                   │
│  germaniumShaper_ : Waveshaper   Type::Tube, drive varies                   │
│  siliconShaper_   : Waveshaper   Type::Tanh, drive varies                   │
│  toneFilter_      : Biquad       FilterType::Lowpass, Q=0.7071              │
│  dcBlocker_       : DCBlocker    10Hz cutoff                                │
├─────────────────────────────────────────────────────────────────────────────┤
│  Smoothers (Layer 1)                                                        │
│  ───────────────────                                                        │
│  fuzzSmoother_     : OnePoleSmoother   5ms smoothing                        │
│  volumeSmoother_   : OnePoleSmoother   5ms smoothing                        │
│  biasSmoother_     : OnePoleSmoother   5ms smoothing                        │
│  toneCtrlSmoother_ : OnePoleSmoother   5ms smoothing                        │
├─────────────────────────────────────────────────────────────────────────────┤
│  Germanium Sag State                                                        │
│  ───────────────────                                                        │
│  sagEnvelope_      : float       Current envelope value  default: 0.0       │
│  sagAttackCoeff_   : float       1ms attack coefficient  calc at prepare()  │
│  sagReleaseCoeff_  : float       100ms release coeff     calc at prepare()  │
├─────────────────────────────────────────────────────────────────────────────┤
│  Type Crossfade State                                                       │
│  ────────────────────                                                       │
│  crossfadeActive_     : bool     Crossfade in progress   default: false     │
│  crossfadePosition_   : float    [0.0, 1.0]              default: 0.0       │
│  crossfadeIncrement_  : float    Per-sample increment    calc at prepare()  │
│  previousType_        : FuzzType Type before crossfade   default: Germanium │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      │ composes
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                            FuzzType (enum)                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│  Germanium = 0   // Warm, saggy, even harmonics, soft clipping              │
│  Silicon   = 1   // Bright, tight, odd harmonics, hard clipping             │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Parameter Specifications

### FuzzType Enumeration (FR-001)

| Value | Underlying | Description |
|-------|------------|-------------|
| Germanium | 0 (uint8_t) | Warm, saggy response with softer clipping and even harmonics |
| Silicon | 1 (uint8_t) | Brighter, tighter response with harder clipping and odd harmonics |

### Fuzz Amount (FR-007, FR-012)

| Property | Value |
|----------|-------|
| Type | float |
| Range | [0.0, 1.0] |
| Default | 0.5 |
| Clamping | std::clamp on set |
| Smoothing | 5ms OnePoleSmoother |
| Mapping | Linear to drive gain: 1.0 + fuzz * 9.0 (1x to 10x drive) |

**Behavior**:
- fuzz=0.0: Minimal distortion, near-clean pass-through (THD < 1%)
- fuzz=0.5: Moderate saturation, balanced tone
- fuzz=1.0: Maximum saturation, heavily distorted

### Volume (FR-008, FR-013)

| Property | Value |
|----------|-------|
| Type | float |
| Unit | dB |
| Range | [-24.0, +24.0] |
| Default | 0.0 |
| Clamping | std::clamp on set |
| Smoothing | 5ms OnePoleSmoother |
| Mapping | dbToGain(volumeDb_) |

### Bias (FR-009, FR-014)

| Property | Value |
|----------|-------|
| Type | float |
| Range | [0.0, 1.0] |
| Default | 0.7 |
| Clamping | std::clamp on set |
| Smoothing | 5ms OnePoleSmoother |
| Mapping | Gate threshold = (1.0 - bias) * 0.2 |

**Behavior** (FR-022 to FR-025):
- bias=0.0: Maximum gating (dying battery), only loud signals pass
- bias=0.5: Moderate gating, some signal cutoff
- bias=1.0: No gating (fresh battery), full sustain

### Tone (FR-010, FR-015)

| Property | Value |
|----------|-------|
| Type | float |
| Range | [0.0, 1.0] |
| Default | 0.5 |
| Clamping | std::clamp on set |
| Smoothing | 5ms OnePoleSmoother |
| Mapping | Cutoff = 400 + tone * 7600 Hz |

**Behavior** (FR-026 to FR-028):
- tone=0.0: 400Hz cutoff (dark/muffled)
- tone=0.5: 4200Hz cutoff (neutral)
- tone=1.0: 8000Hz cutoff (bright/open)

### Octave-Up (FR-050, FR-051)

| Property | Value |
|----------|-------|
| Type | bool |
| Default | false |

**Behavior** (FR-052, FR-053):
- When enabled, applies self-modulation (input * |input|) before fuzz stage
- Creates octave-up effect through frequency doubling

## State Transitions

### Type Change State Machine

```
                 setFuzzType(newType)
                        │
                        ▼
              ┌─────────────────────┐
              │  newType == type_?  │
              └─────────────────────┘
                   │           │
                   │Yes        │No
                   ▼           ▼
              ┌────────┐  ┌────────────────────────────┐
              │ No-op  │  │ Start Crossfade            │
              └────────┘  │ previousType_ = type_      │
                          │ type_ = newType            │
                          │ crossfadeActive_ = true    │
                          │ crossfadePosition_ = 0.0   │
                          └────────────────────────────┘
```

### Crossfade Processing State Machine

```
              ┌─────────────────────────┐
              │   crossfadeActive_?     │
              └─────────────────────────┘
                   │              │
                   │No            │Yes
                   ▼              ▼
         ┌─────────────┐   ┌─────────────────────────────────────┐
         │ Single-type │   │ Dual Processing                     │
         │ processing  │   │ 1. Process previousType -> outA     │
         └─────────────┘   │ 2. Process type_ -> outB            │
                           │ 3. equalPowerGains(position)        │
                           │ 4. output = outA*fadeOut + outB*in  │
                           │ 5. position += increment            │
                           │ 6. if position >= 1.0:              │
                           │       crossfadeActive_ = false      │
                           └─────────────────────────────────────┘
```

### Lifecycle State Machine

```
    ┌───────────────┐
    │  CONSTRUCTED  │
    │  prepared_=F  │
    └───────┬───────┘
            │
            │ prepare(sampleRate, maxBlockSize)
            ▼
    ┌───────────────┐
    │   PREPARED    │
    │  prepared_=T  │
    └───────┬───────┘
            │
            │ process() / reset()
            │
            ├───────────────────┐
            │                   │
            │ process()         │ reset()
            ▼                   ▼
    ┌───────────────┐   ┌───────────────┐
    │  PROCESSING   │   │    RESET      │
    │  (returns to  │   │  (clears      │
    │   PREPARED)   │   │   state,      │
    └───────────────┘   │   snaps       │
                        │   smoothers)  │
                        └───────────────┘
```

## Validation Rules

| Parameter | Validation | Action on Invalid |
|-----------|------------|-------------------|
| fuzz | [0.0, 1.0] | std::clamp |
| volumeDb | [-24.0, +24.0] | std::clamp |
| bias | [0.0, 1.0] | std::clamp |
| tone | [0.0, 1.0] | std::clamp |
| type | FuzzType enum | No validation needed (type-safe) |
| octaveUp | bool | No validation needed (type-safe) |
| buffer (process) | nullptr | Return early (no-op) |
| numSamples (process) | 0 | Return early (no-op) |
| prepared | false | Return input unchanged (FR-004) |

## Constants

```cpp
// Default parameter values (FR-005)
static constexpr float kDefaultFuzz = 0.5f;
static constexpr float kDefaultVolumeDb = 0.0f;
static constexpr float kDefaultBias = 0.7f;
static constexpr float kDefaultTone = 0.5f;

// Parameter ranges
static constexpr float kMinVolumeDb = -24.0f;
static constexpr float kMaxVolumeDb = +24.0f;

// Timing
static constexpr float kSmoothingTimeMs = 5.0f;
static constexpr float kCrossfadeTimeMs = 5.0f;
static constexpr float kSagAttackMs = 1.0f;
static constexpr float kSagReleaseMs = 100.0f;

// Filter
static constexpr float kDCBlockerCutoffHz = 10.0f;
static constexpr float kToneMinHz = 400.0f;
static constexpr float kToneMaxHz = 8000.0f;
```

## Relationships

### Composition (FuzzProcessor "has-a")

| Component | Quantity | Lifetime | Purpose |
|-----------|----------|----------|---------|
| Waveshaper | 2 | Same as processor | Germanium + Silicon saturation |
| Biquad | 1 | Same as processor | Tone filter |
| DCBlocker | 1 | Same as processor | DC offset removal |
| OnePoleSmoother | 4 | Same as processor | Parameter smoothing |

### Dependencies (FuzzProcessor "uses")

| Dependency | Layer | Include Path |
|------------|-------|--------------|
| Waveshaper | 1 | `<krate/dsp/primitives/waveshaper.h>` |
| Biquad | 1 | `<krate/dsp/primitives/biquad.h>` |
| DCBlocker | 1 | `<krate/dsp/primitives/dc_blocker.h>` |
| OnePoleSmoother | 1 | `<krate/dsp/primitives/smoother.h>` |
| Sigmoid::tanh | 0 | `<krate/dsp/core/sigmoid.h>` |
| Asymmetric::tube | 0 | `<krate/dsp/core/sigmoid.h>` |
| dbToGain | 0 | `<krate/dsp/core/db_utils.h>` |
| flushDenormal | 0 | `<krate/dsp/core/db_utils.h>` |
| equalPowerGains | 0 | `<krate/dsp/core/crossfade_utils.h>` |
| crossfadeIncrement | 0 | `<krate/dsp/core/crossfade_utils.h>` |
