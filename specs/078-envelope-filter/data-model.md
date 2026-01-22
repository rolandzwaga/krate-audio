# Data Model: Envelope Filter / Auto-Wah

**Feature**: 078-envelope-filter | **Date**: 2026-01-22

## Overview

This document defines the data structures and entities for the EnvelopeFilter processor.

---

## Entity: EnvelopeFilter

**Location**: `dsp/include/krate/dsp/processors/envelope_filter.h`
**Layer**: 2 (Processor)
**Namespace**: `Krate::DSP`

### Description

A Layer 2 processor that combines an EnvelopeFollower with an SVF (State Variable Filter) to create classic auto-wah and envelope filter effects. The input signal's amplitude envelope modulates the filter cutoff frequency.

### Composed Components

| Component | Type | Purpose |
|-----------|------|---------|
| envFollower_ | EnvelopeFollower | Tracks amplitude envelope of input signal |
| filter_ | SVF | Resonant filter with modulated cutoff |

### Enumerations

#### Direction

```cpp
enum class Direction : uint8_t {
    Up = 0,    ///< Envelope opens filter (louder = higher cutoff)
    Down = 1   ///< Envelope closes filter (louder = lower cutoff)
};
```

#### FilterType

```cpp
enum class FilterType : uint8_t {
    Lowpass = 0,   ///< 12 dB/oct lowpass, classic wah
    Bandpass = 1,  ///< Constant 0 dB peak, vowel-like
    Highpass = 2   ///< 12 dB/oct highpass, "backwards" effect
};
```

### Constants

| Constant | Type | Value | Description |
|----------|------|-------|-------------|
| kMinSensitivity | float | -24.0f | Minimum sensitivity in dB |
| kMaxSensitivity | float | +24.0f | Maximum sensitivity in dB |
| kMinAttackMs | float | 0.1f | Minimum attack time (from EnvelopeFollower) |
| kMaxAttackMs | float | 500.0f | Maximum attack time (from EnvelopeFollower) |
| kMinReleaseMs | float | 1.0f | Minimum release time (from EnvelopeFollower) |
| kMaxReleaseMs | float | 5000.0f | Maximum release time (from EnvelopeFollower) |
| kMinFrequency | float | 20.0f | Minimum frequency for sweep range |
| kMinResonance | float | 0.5f | Minimum Q factor |
| kMaxResonance | float | 20.0f | Maximum Q factor |
| kDefaultMinFrequency | float | 200.0f | Default low sweep limit |
| kDefaultMaxFrequency | float | 2000.0f | Default high sweep limit |
| kDefaultResonance | float | 8.0f | Default Q (moderate resonance) |
| kDefaultAttackMs | float | 10.0f | Default attack time |
| kDefaultReleaseMs | float | 100.0f | Default release time |

### Member Variables

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| envFollower_ | EnvelopeFollower | - | Composed envelope tracker |
| filter_ | SVF | - | Composed resonant filter |
| sampleRate_ | double | 44100.0 | Current sample rate |
| sensitivityDb_ | float | 0.0f | Input gain for envelope detection |
| sensitivityGain_ | float | 1.0f | Linear gain (cached from dB) |
| direction_ | Direction | Up | Envelope-to-cutoff direction |
| filterType_ | FilterType | Lowpass | Current filter mode |
| minFrequency_ | float | 200.0f | Low end of frequency sweep |
| maxFrequency_ | float | 2000.0f | High end of frequency sweep |
| resonance_ | float | 8.0f | Filter Q factor |
| depth_ | float | 1.0f | Envelope modulation depth [0, 1] |
| mix_ | float | 1.0f | Dry/wet mix [0, 1] |
| currentCutoff_ | float | 200.0f | Last calculated cutoff (for monitoring) |
| currentEnvelope_ | float | 0.0f | Last envelope value (for monitoring) |
| prepared_ | bool | false | Preparation state flag |

### State Diagram

```
                    +-----------+
                    |  Created  |
                    +-----------+
                          |
                          v prepare(sampleRate)
                    +-----------+
              +---->|  Prepared |<----+
              |     +-----------+     |
              |           |           |
reset()       |           v process() |
              |     +-----------+     |
              +-----|Processing |-----+
                    +-----------+
```

### Invariants

1. `minFrequency_ < maxFrequency_` (enforced by setters)
2. `minFrequency_ >= kMinFrequency` (20 Hz)
3. `maxFrequency_ <= sampleRate_ * 0.45` (Nyquist safety)
4. `depth_ in [0.0, 1.0]`
5. `mix_ in [0.0, 1.0]`
6. `resonance_ in [0.5, 20.0]`
7. `sensitivityDb_ in [-24, +24]`

### Processing Pipeline

```
Input ----+---> [Sensitivity Gain] ---> [EnvelopeFollower] ---> envelope
          |                                                        |
          |                                    +-------------------+
          |                                    v
          |                           [Clamp to 0-1]
          |                                    |
          |                                    v
          |                           [Apply Depth]
          |                                    |
          |                                    v
          |                           [Exponential Map]
          |                                    |
          |                                    v cutoff
          +---> [SVF (setCutoff, process)] <---+
                           |
                           v
                    [Dry/Wet Mix]
                           |
                           v
                        Output
```

---

## Relationships

```
EnvelopeFilter (Layer 2)
    |
    +-- composes --> EnvelopeFollower (Layer 2)
    |                    |
    |                    +-- uses --> Biquad (Layer 1) [sidechain filter]
    |
    +-- composes --> SVF (Layer 1)
    |
    +-- uses --> dbToGain (Layer 0)
```

---

## Validation Rules

### Parameter Validation

| Parameter | Validation Rule | Action on Invalid |
|-----------|-----------------|-------------------|
| sensitivity | [-24, +24] dB | Clamp to range |
| attack | [0.1, 500] ms | Delegate to EnvelopeFollower (clamps) |
| release | [1, 5000] ms | Delegate to EnvelopeFollower (clamps) |
| minFrequency | [20, maxFreq-1] | Clamp, ensure < maxFreq |
| maxFrequency | [minFreq+1, 0.45*sr] | Clamp, ensure > minFreq |
| resonance | [0.5, 20] | Clamp to range |
| depth | [0, 1] | Clamp to range |
| mix | [0, 1] | Clamp to range |

### Input Validation

| Input | Handling |
|-------|----------|
| NaN | SVF returns 0, resets (per SVF FR-022) |
| Inf | SVF returns 0, resets (per SVF FR-022) |
| Unprepared | Return input unchanged (safe default) |

---

## Memory Layout

```cpp
class EnvelopeFilter {
    // Composed components (largest first for alignment)
    EnvelopeFollower envFollower_;  // ~88 bytes (estimate)
    SVF filter_;                    // ~64 bytes (estimate)

    // Configuration
    double sampleRate_;             // 8 bytes
    float sensitivityDb_;           // 4 bytes
    float sensitivityGain_;         // 4 bytes
    float minFrequency_;            // 4 bytes
    float maxFrequency_;            // 4 bytes
    float resonance_;               // 4 bytes
    float depth_;                   // 4 bytes
    float mix_;                     // 4 bytes
    float currentCutoff_;           // 4 bytes
    float currentEnvelope_;         // 4 bytes

    // Enums and flags
    Direction direction_;           // 1 byte
    FilterType filterType_;         // 1 byte
    bool prepared_;                 // 1 byte
    // Padding: 5 bytes

    // Total: ~200 bytes (estimate)
};
```

No dynamic allocation. All state preallocated for real-time safety.
