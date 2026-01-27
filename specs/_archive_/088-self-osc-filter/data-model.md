# Data Model: Self-Oscillating Filter

**Feature**: 088-self-osc-filter | **Date**: 2026-01-23

## Entities

### SelfOscillatingFilter (Layer 2 Processor)

A melodically playable self-oscillating filter that wraps LadderFilter with MIDI control, envelope, and glide.

#### Fields

| Field | Type | Default | Description | Validation |
|-------|------|---------|-------------|------------|
| `frequency_` | float | 440.0f | Target oscillation frequency (Hz) | [20, 20000] or sampleRate*0.45 |
| `resonance_` | float | 1.0f | Normalized resonance (0=low, 1=self-osc) | [0.0, 1.0] |
| `glideMs_` | float | 0.0f | Glide/portamento time | [0, 5000] |
| `attackMs_` | float | 0.0f | Envelope attack time | [0, 20] |
| `releaseMs_` | float | 500.0f | Envelope release time | [10, 2000] |
| `externalMix_` | float | 0.0f | External input mix (0=osc, 1=ext) | [0.0, 1.0] |
| `waveShapeAmount_` | float | 0.0f | Saturation amount (0=clean, 1=sat) | [0.0, 1.0] |
| `levelDb_` | float | 0.0f | Output level in dB | [-60, +6] |

#### State Fields

| Field | Type | Description |
|-------|------|-------------|
| `envelopeState_` | EnvelopeState | Current envelope state (Idle/Attack/Sustain/Release) |
| `currentEnvelopeLevel_` | float | Current envelope amplitude (0.0 to 1.0) |
| `targetVelocityGain_` | float | Target gain from last noteOn velocity |
| `sampleRate_` | double | Current sample rate |
| `prepared_` | bool | Whether prepare() has been called |

#### Composed Components

| Component | Type | Purpose |
|-----------|------|---------|
| `filter_` | LadderFilter | Core self-oscillating filter |
| `dcBlocker_` | DCBlocker2 | DC offset removal |
| `frequencyRamp_` | LinearRamp | Glide interpolation |
| `levelSmoother_` | OnePoleSmoother | Output level smoothing |
| `mixSmoother_` | OnePoleSmoother | External mix smoothing |
| `attackEnvelope_` | OnePoleSmoother | Attack phase envelope |
| `releaseEnvelope_` | OnePoleSmoother | Release phase envelope |

#### Constants

```cpp
static constexpr float kMinFrequency = 20.0f;
static constexpr float kMaxFrequency = 20000.0f;
static constexpr float kMinAttackMs = 0.0f;
static constexpr float kMaxAttackMs = 20.0f;
static constexpr float kMinReleaseMs = 10.0f;
static constexpr float kMaxReleaseMs = 2000.0f;
static constexpr float kMinGlideMs = 0.0f;
static constexpr float kMaxGlideMs = 5000.0f;
static constexpr float kMinLevelDb = -60.0f;
static constexpr float kMaxLevelDb = 6.0f;
static constexpr float kSelfOscResonance = 3.95f;  // LadderFilter resonance for self-osc
static constexpr float kReleaseThresholdDb = -60.0f;  // Envelope release completion threshold
static constexpr float kDefaultSmoothingMs = 5.0f;  // Parameter smoothing time
```

---

### EnvelopeState (Enum)

Internal envelope state machine states.

```cpp
enum class EnvelopeState : uint8_t {
    Idle,     // No note active, envelope at zero
    Attack,   // Note triggered, ramping up to target
    Sustain,  // At target level, held until noteOff
    Release   // noteOff received, ramping down to zero
};
```

#### State Transitions

| From | To | Trigger | Action |
|------|-----|---------|--------|
| Idle | Attack | noteOn() | Set target = velocityGain, start attack |
| Attack | Sustain | Envelope >= 99% of target | Hold at target |
| Attack | Attack | noteOn() (retrigger) | Update target, continue from current level |
| Sustain | Attack | noteOn() (retrigger) | Update target, restart attack from current |
| Sustain | Release | noteOff() | Start release toward zero |
| Release | Idle | Envelope < threshold (-60dB) | Stop processing |
| Release | Attack | noteOn() (retrigger) | Set new target, restart attack from current |

---

### MIDI Utilities (Layer 0)

New utility functions in `core/midi_utils.h`:

#### midiNoteToFrequency

```cpp
/// Convert MIDI note number to frequency using 12-TET tuning.
/// @param midiNote MIDI note number (0-127, where 69 = A4)
/// @param a4Frequency Reference frequency for A4 (default 440 Hz)
/// @return Frequency in Hz
[[nodiscard]] constexpr float midiNoteToFrequency(
    int midiNote,
    float a4Frequency = 440.0f
) noexcept;
```

| Input | Output |
|-------|--------|
| 60 (C4) | 261.63 Hz |
| 69 (A4) | 440.0 Hz |
| 72 (C5) | 523.25 Hz |

#### velocityToGain

```cpp
/// Convert MIDI velocity to linear gain.
/// Uses linear mapping where velocity 127 = 1.0 and velocity 64 ≈ -6 dB.
/// @param velocity MIDI velocity (0-127)
/// @return Linear gain multiplier (0.0 to 1.0)
[[nodiscard]] constexpr float velocityToGain(int velocity) noexcept;
```

| Input | Output | dB |
|-------|--------|----|
| 127 | 1.0 | 0 dB |
| 64 | 0.504 | -5.95 dB |
| 1 | 0.008 | -42 dB |
| 0 | 0.0 | -inf |

---

## Relationships

```
SelfOscillatingFilter
    ├── has-a LadderFilter (1:1, composition)
    ├── has-a DCBlocker2 (1:1, composition)
    ├── has-a LinearRamp (1:1, for frequency glide)
    ├── has-a OnePoleSmoother (4:1, for level, mix, attack, release)
    └── uses EnvelopeState (enum)

Layer Dependencies:
    Layer 2 (SelfOscillatingFilter)
        └── Layer 1 (LadderFilter, DCBlocker2, LinearRamp, OnePoleSmoother)
            └── Layer 0 (dbToGain, fastTanh, midiNoteToFrequency, velocityToGain)
```

---

## Signal Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                    SelfOscillatingFilter                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  External Input ─────┬─────────────────────────────┐            │
│                      │                             │            │
│                      ▼                             │            │
│               ┌─────────────┐                      │            │
│               │ Mix Control │ ← mixSmoother_       │            │
│               │ (0=osc,1=ext)│                     │            │
│               └──────┬──────┘                      │            │
│                      │                             │            │
│                      ▼                             │            │
│               ┌─────────────┐                      │            │
│               │ LadderFilter│ ← setCutoff(freqRamp_.process())  │
│               │ (res=3.95)  │ ← setResonance(mapped resonance)  │
│               └──────┬──────┘                      │            │
│                      │                             │            │
│                      ▼                             │            │
│               ┌─────────────┐                      │            │
│               │ DCBlocker2  │ (10Hz cutoff)        │            │
│               └──────┬──────┘                      │            │
│                      │                             │            │
│                      ▼                             │            │
│               ┌─────────────┐                      │            │
│               │ Wave Shape  │ ← waveShapeAmount_   │            │
│               │ (tanh sat)  │                      │            │
│               └──────┬──────┘                      │            │
│                      │                             │            │
│                      ▼                             │            │
│               ┌─────────────┐                      │            │
│               │ Envelope    │ ← attack/release envelope         │
│               │ (* gain)    │                      │            │
│               └──────┬──────┘                      │            │
│                      │                             │            │
│                      ▼                             │            │
│               ┌─────────────┐                      │            │
│               │ Level Gain  │ ← levelSmoother_     │            │
│               │ (dB→linear) │                      │            │
│               └──────┬──────┘                      │            │
│                      │                             │            │
│                      ▼                             │            │
│                   Output                           │            │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Validation Rules

### Parameter Validation

All parameters are clamped to their valid ranges in setter methods:

```cpp
void setFrequency(float hz) noexcept {
    float maxFreq = std::min(kMaxFrequency, static_cast<float>(sampleRate_ * 0.45));
    frequency_ = std::clamp(hz, kMinFrequency, maxFreq);
    frequencyRamp_.setTarget(frequency_);
}

void setResonance(float amount) noexcept {
    resonance_ = std::clamp(amount, 0.0f, 1.0f);
    // Map to filter range: 0->0, 0.95->3.9, 1.0->3.95
    float filterRes = mapResonanceToFilter(resonance_);
    filter_.setResonance(filterRes);
}

void setAttack(float ms) noexcept {
    attackMs_ = std::clamp(ms, kMinAttackMs, kMaxAttackMs);
    attackEnvelope_.configure(attackMs_, static_cast<float>(sampleRate_));
}
```

### MIDI Validation

```cpp
void noteOn(int midiNote, int velocity) noexcept {
    // FR-008: velocity 0 treated as noteOff
    if (velocity <= 0) {
        noteOff();
        return;
    }

    // Clamp to valid ranges
    midiNote = std::clamp(midiNote, 0, 127);
    velocity = std::clamp(velocity, 1, 127);

    // Convert and apply
    float freq = midiNoteToFrequency(midiNote);
    targetVelocityGain_ = velocityToGain(velocity);

    // Handle glide and envelope...
}
```

---

## Invariants

1. **Filter resonance**: When `resonance_ >= 0.95` (user value), internal filter resonance must be >= 3.9 for self-oscillation
2. **Envelope bounds**: `currentEnvelopeLevel_` always in [0.0, 1.0]
3. **DC blocking**: Output always passes through DCBlocker2
4. **Prepared state**: All processing methods return early if `prepared_ == false`
5. **Real-time safety**: All processing methods are noexcept with zero allocations
