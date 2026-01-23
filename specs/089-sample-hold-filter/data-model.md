# Data Model: Sample & Hold Filter

**Feature**: 089-sample-hold-filter | **Date**: 2026-01-23

## Entity Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                       SampleHoldFilter                               │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐  │
│  │   Trigger System  │  │  Sample Sources  │  │  Filter Core     │  │
│  │                   │  │                  │  │                  │  │
│  │  - Clock          │  │  - LFO           │  │  - SVF (L)       │  │
│  │  - Audio          │  │  - Random        │  │  - SVF (R)       │  │
│  │  - Random         │  │  - Envelope      │  │                  │  │
│  │                   │  │  - External      │  │                  │  │
│  └──────────────────┘  └──────────────────┘  └──────────────────┘  │
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │              Per-Parameter Configuration                       │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐           │   │
│  │  │   Cutoff    │  │      Q      │  │     Pan     │           │   │
│  │  │  - enabled  │  │  - enabled  │  │  - enabled  │           │   │
│  │  │  - source   │  │  - source   │  │  - source   │           │   │
│  │  │  - range    │  │  - range    │  │  - range    │           │   │
│  │  │  - smoother │  │  - smoother │  │  - smoother │           │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘           │   │
│  └──────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

## Enumerations

### TriggerSource (FR-001)

```cpp
/// @brief Trigger mode selection for S&H timing
enum class TriggerSource : uint8_t {
    Clock = 0,   ///< Regular intervals based on hold time (FR-003)
    Audio,       ///< Transient detection from input signal (FR-004)
    Random       ///< Probability-based at hold intervals (FR-005)
};
```

| Value | Behavior | Use Case |
|-------|----------|----------|
| Clock | Triggers at exact hold time intervals | Rhythmic stepped effects |
| Audio | Triggers on input transients | Reactive filter sweeps |
| Random | Evaluates probability at potential hold points | Chaotic modulation |

### SampleSource (FR-006)

```cpp
/// @brief Sample value source selection per parameter
enum class SampleSource : uint8_t {
    LFO = 0,     ///< Internal LFO output [-1, 1] (FR-007)
    Random,      ///< Xorshift32 random value [-1, 1] (FR-008)
    Envelope,    ///< EnvelopeFollower output [0, 1] -> normalized (FR-009)
    External     ///< User-provided value [0, 1] -> normalized (FR-010)
};
```

| Value | Input Range | Output Range | Conversion Formula | Source |
|-------|-------------|--------------|-------------------|--------|
| LFO | [-1, 1] | [-1, 1] | Direct (no conversion) | Internal LFO instance |
| Random | N/A | [-1, 1] | Direct generation | Xorshift32 PRNG |
| Envelope | [0, 1] | [-1, 1] | `(value * 2) - 1` | EnvelopeFollower.getCurrentValue() |
| External | [0, 1] | [-1, 1] | `(value * 2) - 1` | User setExternalValue() |

**Conversion Note**: Envelope and External sources output unipolar [0, 1] values. The formula `(value * 2) - 1` converts to bipolar [-1, 1] for consistent modulation behavior across all sources.

### SVFMode (existing, from svf.h)

```cpp
enum class SVFMode : uint8_t {
    Lowpass,   ///< 12 dB/oct lowpass
    Highpass,  ///< 12 dB/oct highpass
    Bandpass,  ///< Constant 0 dB peak gain
    Notch      ///< Band-reject filter
};
```

## Internal Structures

### ParameterState

```cpp
/// @brief Configuration and state for a single sampleable parameter
/// @note Internal structure, not part of public API
struct ParameterState {
    // Configuration (set by user)
    bool enabled = false;               ///< Whether sampling is active
    SampleSource source = SampleSource::LFO;  ///< Value source
    float modulationRange = 0.0f;       ///< Parameter-specific range

    // Runtime state (managed internally)
    float heldValue = 0.0f;             ///< Last sampled value [-1, 1]
    OnePoleSmoother smoother;           ///< Slew limiter for this parameter
};
```

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| enabled | bool | - | false | Enable/disable sampling for this parameter |
| source | SampleSource | enum | LFO | Which source provides sample values |
| modulationRange | float | varies | 0.0 | Amount of modulation (parameter-specific) |
| heldValue | float | [-1, 1] | 0.0 | Current held modulation value |
| smoother | OnePoleSmoother | - | - | Slew limiter instance |

### Modulation Range Semantics

| Parameter | modulationRange Meaning | Units | Range | Variable Name |
|-----------|------------------------|-------|-------|---------------|
| Cutoff | Octave offset from base | octaves | [0, 8] | cutoffOctaveRange |
| Q | Normalized Q variation | normalized | [0, 1] | qRange |
| Pan | Stereo cutoff offset amount | octaves | [0, 4] | panOctaveRange |

**Note**: Pan modulation uses octave-based formula:
- `leftCutoff = baseCutoff * pow(2, -panValue * panOctaveRange)`
- `rightCutoff = baseCutoff * pow(2, +panValue * panOctaveRange)`

## SampleHoldFilter Class

### Constants

```cpp
class SampleHoldFilter {
public:
    // Hold time limits (FR-002)
    static constexpr float kMinHoldTimeMs = 0.1f;    ///< Minimum 0.1ms
    static constexpr float kMaxHoldTimeMs = 10000.0f; ///< Maximum 10 seconds

    // Slew time limits (FR-015)
    static constexpr float kMinSlewTimeMs = 0.0f;    ///< Instant (no slew)
    static constexpr float kMaxSlewTimeMs = 500.0f;  ///< Maximum 500ms

    // LFO rate limits (FR-007)
    static constexpr float kMinLFORate = 0.01f;      ///< 0.01 Hz
    static constexpr float kMaxLFORate = 20.0f;      ///< 20 Hz

    // Cutoff modulation limits (FR-011)
    static constexpr float kMinCutoffOctaves = 0.0f;
    static constexpr float kMaxCutoffOctaves = 8.0f;

    // Q modulation limits (FR-012)
    static constexpr float kMinQRange = 0.0f;
    static constexpr float kMaxQRange = 1.0f;

    // Base cutoff limits (FR-019)
    static constexpr float kMinBaseCutoff = 20.0f;   ///< 20 Hz
    static constexpr float kMaxBaseCutoff = 20000.0f; ///< 20 kHz

    // Base Q limits (FR-020)
    static constexpr float kMinBaseQ = 0.1f;
    static constexpr float kMaxBaseQ = 30.0f;

    // Transient detection threshold
    static constexpr float kMinThreshold = 0.0f;
    static constexpr float kMaxThreshold = 1.0f;
    static constexpr float kDefaultThreshold = 0.5f;

    // Random trigger probability
    static constexpr float kMinProbability = 0.0f;
    static constexpr float kMaxProbability = 1.0f;
```

### Member Variables

```cpp
private:
    // === Composed DSP Components ===
    SVF filterL_;                    ///< Left channel filter
    SVF filterR_;                    ///< Right channel filter
    LFO lfo_;                        ///< Internal LFO source
    EnvelopeFollower envelopeFollower_; ///< For envelope source & audio trigger
    Xorshift32 rng_{1};              ///< Random number generator

    // === Per-Parameter State ===
    ParameterState cutoffState_;     ///< Cutoff modulation config & state
    ParameterState qState_;          ///< Q modulation config & state
    ParameterState panState_;        ///< Pan modulation config & state

    // === Trigger System State ===
    TriggerSource triggerSource_ = TriggerSource::Clock;
    double samplesUntilTrigger_ = 0.0;  ///< Double for precision (SC-001)
    double holdTimeSamples_ = 0.0;       ///< Hold time in samples
    float previousEnvelope_ = 0.0f;      ///< For audio trigger edge detection
    bool holdingAfterTransient_ = false; ///< Ignore transients during hold
    double transientHoldSamples_ = 0.0;  ///< Remaining samples in hold period
    bool pendingSourceSwitch_ = false;   ///< Flag for mode switch on next buffer

    // === Configuration ===
    double sampleRate_ = 44100.0;
    float holdTimeMs_ = 100.0f;          ///< Hold time in ms
    float slewTimeMs_ = 0.0f;            ///< Slew time in ms
    float baseCutoffHz_ = 1000.0f;       ///< Base cutoff frequency
    float baseQ_ = 0.707f;               ///< Base Q (resonance), default Butterworth
    SVFMode filterMode_ = SVFMode::Lowpass;  ///< Filter type
    float lfoRateHz_ = 1.0f;             ///< LFO frequency
    float transientThreshold_ = kDefaultThreshold;  ///< Audio trigger threshold
    float triggerProbability_ = 1.0f;    ///< Random trigger probability
    float externalValue_ = 0.5f;         ///< External source value [0, 1]
    uint32_t seed_ = 1;                  ///< RNG seed for reproducibility

    // === Per-Parameter Modulation Ranges ===
    float cutoffOctaveRange_ = 2.0f;     ///< Cutoff modulation range [0, 8] octaves
    float qRange_ = 0.5f;                ///< Q modulation range [0, 1] normalized
    float panOctaveRange_ = 1.0f;        ///< Pan modulation range [0, 4] octaves

    // === Lifecycle State ===
    bool prepared_ = false;
    float maxCutoff_ = 20000.0f;         ///< Cached max cutoff for sample rate
```

### State Transitions

```
┌─────────────┐    prepare()     ┌─────────────┐
│ Unprepared  │ ───────────────> │  Prepared   │
└─────────────┘                  └─────────────┘
                                       │
                                       │ reset()
                                       ▼
                                 ┌─────────────┐
                                 │   Ready     │
                                 │ (processing)│
                                 └─────────────┘
                                       │
                                       │ trigger event
                                       ▼
                                 ┌─────────────┐
                                 │  Sampling   │
                                 │ (new values)│
                                 └─────────────┘
                                       │
                                       │ hold period
                                       ▼
                                 ┌─────────────┐
                                 │   Holding   │
                                 │ (smoothing) │
                                 └─────────────┘
```

## Parameter Flow

### Cutoff Calculation

```
baseCutoffHz_ ─────────────────────────────────────────┐
                                                       │
cutoffState_.heldValue ──> smoother.process() ──┐      │
                                                │      │
cutoffState_.modulationRange ───────────────────┼──> octaveOffset ──> pow(2, offset) ──> modulatedCutoff
                                                │
                                                └──> (heldValue * modulationRange)

Final: modulatedCutoff = baseCutoffHz_ * pow(2, smoothedMod * modulationRange)
```

### Stereo Pan Flow

```
panState_.heldValue ──> smoother.process() ──┐
                                             │
panOctaveRange_ ─────────────────────────────┼──> panOffset
                                             │
                                             └──> (smoothedPan * panOctaveRange_)

leftCutoff  = modulatedCutoff * pow(2, -panOffset)
rightCutoff = modulatedCutoff * pow(2, +panOffset)
```

**Example**: With panValue = -1, panOctaveRange = 1:
- panOffset = -1 * 1 = -1
- leftCutoff = base * pow(2, -(-1)) = base * 2 (one octave higher)
- rightCutoff = base * pow(2, +(-1)) = base * 0.5 (one octave lower)

### Q Calculation

```
baseQ_ ─────────────────────────────────────────────┐
                                                    │
qState_.heldValue ──> smoother.process() ──┐        │
                                           │        │
qState_.modulationRange ───────────────────┼──> qOffset ──> modulatedQ
                                           │
                                           └──> (heldValue * modulationRange * (kMaxQ - kMinQ))

Final: modulatedQ = baseQ_ + qOffset
```

## Validation Rules

### Parameter Constraints

| Parameter | Constraint | Action |
|-----------|------------|--------|
| holdTimeMs | [0.1, 10000] | Clamp to range |
| slewTimeMs | [0, 500] | Clamp to range |
| baseCutoffHz | [20, 20000] | Clamp to [SVF::kMinCutoff, maxCutoff_] |
| baseQ | [0.1, 30] | Clamp to [SVF::kMinQ, SVF::kMaxQ] |
| lfoRateHz | [0.01, 20] | Clamp to range |
| cutoffOctaveRange | [0, 8] | Clamp to range |
| qRange | [0, 1] | Clamp to range |
| transientThreshold | [0, 1] | Clamp to range |
| triggerProbability | [0, 1] | Clamp to range |
| externalValue | [0, 1] | Clamp to range |

### Runtime Constraints

| Condition | Behavior |
|-----------|----------|
| !prepared_ | process() returns input unchanged |
| NaN/Inf input | Reset filter state, return 0 |
| cutoff > maxCutoff_ | Clamp to maxCutoff_ |
| q < kMinQ | Clamp to kMinQ |

## Thread Safety

| Component | Thread Safety |
|-----------|---------------|
| Parameter setters | Not thread-safe (UI thread only) |
| process() methods | Not thread-safe (audio thread only) |
| Configuration reads | Not thread-safe |

**Usage Pattern**: Configure from UI thread before audio starts, or use atomic messaging for runtime changes (not implemented in this class - responsibility of caller).

## Serialization

The class supports deterministic behavior through seed-based initialization. To serialize/restore state:

1. Store: seed, all configuration parameters
2. Restore: Call setSeed(), set all parameters, call reset()

Note: Internal filter state (ic1eq_, ic2eq_) is not serialized; reset() clears to known state.
