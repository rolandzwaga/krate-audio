# Data Model: Resonator Bank

**Feature**: 083-resonator-bank | **Date**: 2026-01-22

## Entity Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           ResonatorBank                                  │
│  (Layer 2 Processor)                                                     │
├─────────────────────────────────────────────────────────────────────────┤
│  Global State:                                                           │
│  - sampleRate_: double                                                   │
│  - prepared_: bool                                                       │
│  - triggerPending_: bool                                                 │
│  - triggerVelocity_: float                                               │
├─────────────────────────────────────────────────────────────────────────┤
│  Per-Resonator (x16):                                                    │
│  - filters_[16]: Biquad                                                  │
│  - frequencies_[16]: float                                               │
│  - decays_[16]: float (RT60 seconds)                                     │
│  - gains_[16]: float (linear)                                            │
│  - qValues_[16]: float                                                   │
│  - enabled_[16]: bool                                                    │
├─────────────────────────────────────────────────────────────────────────┤
│  Parameter Smoothers:                                                    │
│  - dampingSmoother_: OnePoleSmoother                                     │
│  - exciterMixSmoother_: OnePoleSmoother                                  │
│  - spectralTiltSmoother_: OnePoleSmoother                                │
├─────────────────────────────────────────────────────────────────────────┤
│  Global Parameters (Targets):                                            │
│  - damping_: float [0.0, 1.0]                                           │
│  - exciterMix_: float [0.0, 1.0]                                        │
│  - spectralTilt_: float [-12.0, +12.0] dB/octave                        │
├─────────────────────────────────────────────────────────────────────────┤
│  Tuning State:                                                           │
│  - tuningMode_: TuningMode                                               │
│  - numActiveResonators_: size_t                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

## Enumerations

### TuningMode

```cpp
enum class TuningMode : uint8_t {
    Harmonic,    ///< Integer multiples of fundamental: f, 2f, 3f, 4f...
    Inharmonic,  ///< Stretched partials: f_n = f * n * sqrt(1 + B*n^2)
    Custom       ///< User-specified frequencies
};
```

## Constants

```cpp
/// Maximum number of resonators in the bank
inline constexpr size_t kMaxResonators = 16;

/// Minimum resonator frequency in Hz
inline constexpr float kMinResonatorFrequency = 20.0f;

/// Maximum resonator frequency ratio (relative to sample rate)
inline constexpr float kMaxResonatorFrequencyRatio = 0.45f;

/// Minimum Q value
inline constexpr float kMinResonatorQ = 0.1f;

/// Maximum Q value (higher than Biquad default for physical modeling)
inline constexpr float kMaxResonatorQ = 100.0f;

/// Minimum decay time in seconds
inline constexpr float kMinDecayTime = 0.001f;  // 1ms

/// Maximum decay time in seconds
inline constexpr float kMaxDecayTime = 30.0f;

/// Default decay time in seconds
inline constexpr float kDefaultDecayTime = 1.0f;

/// Default Q value
inline constexpr float kDefaultResonatorQ = 10.0f;

/// Default gain in dB
inline constexpr float kDefaultGainDb = 0.0f;

/// Parameter smoothing time in milliseconds
inline constexpr float kSmoothingTimeMs = 20.0f;

/// Spectral tilt reference frequency
inline constexpr float kTiltReferenceFrequency = 1000.0f;

/// Minimum spectral tilt in dB/octave
inline constexpr float kMinSpectralTilt = -12.0f;

/// Maximum spectral tilt in dB/octave
inline constexpr float kMaxSpectralTilt = 12.0f;

/// ln(1000) for RT60-to-Q conversion
inline constexpr float kLn1000 = 6.907755278982137f;
```

## Field Definitions

### Per-Resonator Fields

| Field | Type | Default | Range | Description |
|-------|------|---------|-------|-------------|
| frequency | float | 440.0f | [20, sampleRate*0.45] Hz | Center frequency |
| decay | float | 1.0f | [0.001, 30] seconds | RT60 decay time |
| gain | float | 1.0f | [0, 10] linear | Output gain (from dB) |
| qValue | float | 10.0f | [0.1, 100] | Quality factor |
| enabled | bool | false | - | Active state |

### Global Parameter Fields

| Field | Type | Default | Range | Description |
|-------|------|---------|-------|-------------|
| damping | float | 0.0f | [0, 1] | Decay scaling (0=full, 1=silent) |
| exciterMix | float | 0.0f | [0, 1] | Dry input blend (0=wet, 1=dry) |
| spectralTilt | float | 0.0f | [-12, +12] dB/oct | High frequency rolloff |

### State Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| sampleRate | double | 44100.0 | Current sample rate |
| prepared | bool | false | Initialization state |
| triggerPending | bool | false | Pending impulse excitation |
| triggerVelocity | float | 0.0f | Velocity for pending trigger |
| tuningMode | TuningMode | Custom | Current tuning mode |
| numActiveResonators | size_t | 0 | Count of enabled resonators |

## Validation Rules

### Frequency Validation
```cpp
float clampedFreq = std::clamp(freq, kMinResonatorFrequency,
                               static_cast<float>(sampleRate_) * kMaxResonatorFrequencyRatio);
```

### Decay Validation
```cpp
float clampedDecay = std::clamp(decay, kMinDecayTime, kMaxDecayTime);
// Note: decay = 0 is clamped to kMinDecayTime, not allowed to be zero
```

### Q Validation
```cpp
float clampedQ = std::clamp(q, kMinResonatorQ, kMaxResonatorQ);
```

### Index Validation
```cpp
if (index >= kMaxResonators) return;  // Silently ignore out-of-range
```

### Custom Frequencies Validation
```cpp
// When setCustomFrequencies called with count > 16, only first 16 are used
size_t usedCount = std::min(count, kMaxResonators);
```

## State Transitions

### Initialization Flow
```
┌─────────────┐
│  Created    │
│  (default)  │
└──────┬──────┘
       │ prepare(sampleRate)
       v
┌─────────────┐
│  Prepared   │
│  (ready)    │
└──────┬──────┘
       │ setHarmonicSeries() / setInharmonicSeries() / setCustomFrequencies()
       v
┌─────────────┐
│  Configured │
│  (active)   │
└─────────────┘
```

### Processing States
```
                    trigger(velocity)
     ┌──────────────────────────────────────┐
     │                                      v
┌────┴─────┐                        ┌──────────────┐
│   Idle   │◄───────────────────────│ Trigger      │
│ (silent) │    after 1 sample      │ (excitation) │
└────┬─────┘                        └──────────────┘
     │ process(input)
     v
┌──────────────┐
│  Resonating  │
│  (decaying)  │
└──────────────┘
```

### Reset Behavior
```cpp
void reset() noexcept {
    // 1. Clear all filter states
    for (auto& filter : filters_) {
        filter.reset();
    }

    // 2. Reset smoother states
    dampingSmoother_.reset();
    exciterMixSmoother_.reset();
    spectralTiltSmoother_.reset();

    // 3. Reset per-resonator parameters to defaults
    for (size_t i = 0; i < kMaxResonators; ++i) {
        frequencies_[i] = 440.0f;  // A4
        decays_[i] = kDefaultDecayTime;
        gains_[i] = 1.0f;
        qValues_[i] = kDefaultResonatorQ;
        enabled_[i] = false;
    }

    // 4. Reset global parameters to defaults
    damping_ = 0.0f;
    exciterMix_ = 0.0f;
    spectralTilt_ = 0.0f;

    // 5. Reset tuning state
    tuningMode_ = TuningMode::Custom;
    numActiveResonators_ = 0;

    // 6. Clear trigger state
    triggerPending_ = false;
    triggerVelocity_ = 0.0f;
}
```

## Relationships

```
ResonatorBank (1) ────────────────────────── (*) Biquad
                                             [16 instances, bandpass configured]

ResonatorBank (1) ────────────────────────── (3) OnePoleSmoother
                                             [damping, exciterMix, spectralTilt]
```

## Memory Layout

```cpp
class ResonatorBank {
    // Filter bank (16 * ~24 bytes = ~384 bytes)
    std::array<Biquad, kMaxResonators> filters_;

    // Per-resonator parameters (16 * 4 * 4 = 256 bytes)
    std::array<float, kMaxResonators> frequencies_;
    std::array<float, kMaxResonators> decays_;
    std::array<float, kMaxResonators> gains_;
    std::array<float, kMaxResonators> qValues_;

    // Per-resonator state (16 * 1 = 16 bytes, padded)
    std::array<bool, kMaxResonators> enabled_;

    // Smoothers (3 * ~28 bytes = ~84 bytes)
    OnePoleSmoother dampingSmoother_;
    OnePoleSmoother exciterMixSmoother_;
    OnePoleSmoother spectralTiltSmoother_;

    // Global parameters (3 * 4 = 12 bytes)
    float damping_ = 0.0f;
    float exciterMix_ = 0.0f;
    float spectralTilt_ = 0.0f;

    // State (8 + 4 + 1 + 1 + 4 + 8 = 26 bytes)
    double sampleRate_ = 44100.0;
    TuningMode tuningMode_ = TuningMode::Custom;
    bool prepared_ = false;
    bool triggerPending_ = false;
    float triggerVelocity_ = 0.0f;
    size_t numActiveResonators_ = 0;

    // Total: ~800 bytes (no dynamic allocation)
};
```

## Notes

1. **No Dynamic Allocation**: All storage is fixed-size arrays, meeting real-time safety requirements.

2. **Gain Stored as Linear**: User-facing API accepts dB, but internal storage is linear gain for efficiency in processing loop.

3. **Q Computed from Decay**: When decay time is set, Q is recalculated using the RT60 formula. Users can also set Q directly to override.

4. **Tilt Applied Per-Sample**: Spectral tilt is computed once per process() call (smoothed value) and applied as gain scaling to each resonator's output.
