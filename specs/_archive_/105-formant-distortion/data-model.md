# Data Model: Formant Distortion Processor

**Spec**: 105-formant-distortion | **Layer**: 2 (Processor)

## Entities

### FormantDistortion

**Purpose**: Composite processor combining formant filtering with waveshaping for "talking distortion" effects.

**Location**: `dsp/include/krate/dsp/processors/formant_distortion.h`

| Field | Type | Default | Constraints | Description |
|-------|------|---------|-------------|-------------|
| formantFilter_ | FormantFilter | - | - | Vowel/formant filtering (composed) |
| waveshaper_ | Waveshaper | - | - | Saturation processing (composed) |
| envelopeFollower_ | EnvelopeFollower | - | - | Input envelope tracking (composed) |
| dcBlocker_ | DCBlocker | - | - | DC offset removal (composed) |
| mixSmoother_ | OnePoleSmoother | - | - | Mix parameter smoothing |
| vowel_ | Vowel | Vowel::A | A,E,I,O,U | Discrete vowel selection |
| vowelBlend_ | float | 0.0f | [0.0, 4.0] | Continuous vowel morph position |
| useBlendMode_ | bool | false | - | true=blend mode, false=discrete |
| staticFormantShift_ | float | 0.0f | [-24.0, 24.0] | Base formant shift (semitones) |
| envelopeFollowAmount_ | float | 0.0f | [0.0, 1.0] | Envelope modulation depth |
| envelopeModRange_ | float | 12.0f | [0.0, 24.0] | Max envelope shift (semitones) |
| mix_ | float | 1.0f | [0.0, 1.0] | Dry/wet mix |
| sampleRate_ | double | 44100.0 | >= 1000 | Current sample rate |
| prepared_ | bool | false | - | Initialization state |

### Relationships

```
FormantDistortion
    ├── has-a FormantFilter (1:1)
    ├── has-a Waveshaper (1:1)
    ├── has-a EnvelopeFollower (1:1)
    ├── has-a DCBlocker (1:1)
    └── has-a OnePoleSmoother (1:1)
```

## Enumerations

### Vowel (from filter_tables.h)

| Value | Numeric | Description |
|-------|---------|-------------|
| A | 0 | Open front unrounded [a] "father" |
| E | 1 | Close-mid front [e] "bed" |
| I | 2 | Close front [i] "see" |
| O | 3 | Close-mid back [o] "go" |
| U | 4 | Close back [u] "boot" |

### WaveshapeType (from waveshaper.h)

| Value | Numeric | Character |
|-------|---------|-----------|
| Tanh | 0 | Warm, smooth saturation |
| Atan | 1 | Slightly brighter than tanh |
| Cubic | 2 | 3rd harmonic dominant |
| Quintic | 3 | Smoother knee than cubic |
| ReciprocalSqrt | 4 | Fast tanh alternative |
| Erf | 5 | Tape-like with spectral nulls |
| HardClip | 6 | Harsh, all harmonics |
| Diode | 7 | Subtle even harmonics (unbounded) |
| Tube | 8 | Warm even harmonics (bounded) |

## State Transitions

### Vowel Mode State Machine

```
                setVowel(v)
  ┌─────────────────────────────────────────────┐
  │                                             │
  v                                             │
 DISCRETE ──────────────────────────────> BLEND
    ^         setVowelBlend(b)              │
    │                                       │
    └───────────────────────────────────────┘
                setVowel(v)
```

- Both `vowel_` and `vowelBlend_` retain their values regardless of mode
- `useBlendMode_` determines which value is used for processing
- Getters return stored values regardless of active mode

### Processing State

```
UNPREPARED ──────────────────────> PREPARED
               prepare()              │
                                      │
                                      v
                                   ACTIVE
                                      │
                                reset()
                                      │
                                      v
                                  PREPARED
                                 (states cleared)
```

## Validation Rules

| Parameter | Validation | Behavior |
|-----------|------------|----------|
| drive | [0.5, 20.0] | Clamp to range |
| formantShift | [-24.0, 24.0] | Clamp to range |
| vowelBlend | [0.0, 4.0] | Clamp to range |
| envelopeFollowAmount | [0.0, 1.0] | Clamp to range |
| envelopeModRange | [0.0, 24.0] | Clamp to range |
| mix | [0.0, 1.0] | Clamp to range |
| envelopeAttack | [0.1, 500.0] ms | Delegated to EnvelopeFollower |
| envelopeRelease | [1.0, 5000.0] ms | Delegated to EnvelopeFollower |

## Constants

```cpp
static constexpr float kMinDrive = 0.5f;
static constexpr float kMaxDrive = 20.0f;
static constexpr float kMinShift = -24.0f;
static constexpr float kMaxShift = 24.0f;
static constexpr float kMinEnvModRange = 0.0f;
static constexpr float kMaxEnvModRange = 24.0f;
static constexpr float kDefaultEnvModRange = 12.0f;
static constexpr float kDefaultSmoothingMs = 5.0f;
```
