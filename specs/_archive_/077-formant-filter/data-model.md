# Data Model: Formant Filter

**Feature**: 077-formant-filter
**Date**: 2026-01-21

## Overview

This document defines the data model for the FormantFilter class, a Layer 2 DSP processor.

---

## Entity: FormantFilter

### Description
A parallel bandpass filter bank that models vocal formants (F1, F2, F3) for creating "talking" effects on non-vocal audio sources.

### Class Structure

```cpp
namespace Krate {
namespace DSP {

class FormantFilter {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr int kNumFormants = 3;
    static constexpr float kMinFrequency = 20.0f;
    static constexpr float kMaxFrequencyRatio = 0.45f;
    static constexpr float kMinQ = 0.5f;
    static constexpr float kMaxQ = 20.0f;
    static constexpr float kMinShift = -24.0f;
    static constexpr float kMaxShift = 24.0f;
    static constexpr float kMinGender = -1.0f;
    static constexpr float kMaxGender = 1.0f;
    static constexpr float kDefaultSmoothingMs = 5.0f;

    // ... public methods ...

private:
    // =========================================================================
    // Filter Stages
    // =========================================================================

    /// 3 parallel bandpass filters for F1, F2, F3
    std::array<Biquad, kNumFormants> formants_;

    // =========================================================================
    // Parameter Smoothers
    // =========================================================================

    /// Smoothers for formant frequencies (click-free transitions)
    std::array<OnePoleSmoother, kNumFormants> freqSmoothers_;

    /// Smoothers for formant bandwidths (maintains Q stability)
    std::array<OnePoleSmoother, kNumFormants> bwSmoothers_;

    // =========================================================================
    // Parameters
    // =========================================================================

    /// Current discrete vowel selection (when not morphing)
    Vowel currentVowel_ = Vowel::A;

    /// Continuous vowel morph position (0-4, A-E-I-O-U)
    float vowelMorphPosition_ = 0.0f;

    /// Formant frequency shift in semitones (-24 to +24)
    float formantShift_ = 0.0f;

    /// Gender parameter (-1 = male, 0 = neutral, +1 = female)
    float gender_ = 0.0f;

    /// Parameter smoothing time in milliseconds
    float smoothingTime_ = kDefaultSmoothingMs;

    // =========================================================================
    // State
    // =========================================================================

    /// Current sample rate
    double sampleRate_ = 44100.0;

    /// Whether prepare() has been called
    bool prepared_ = false;

    /// Mode flag: true = use vowelMorphPosition_, false = use currentVowel_
    bool useMorphMode_ = false;
};

} // namespace DSP
} // namespace Krate
```

---

## Member Details

### Constants

| Constant | Type | Value | Description |
|----------|------|-------|-------------|
| kNumFormants | int | 3 | Number of formant filters (F1, F2, F3) |
| kMinFrequency | float | 20.0f | Minimum formant frequency (Hz) |
| kMaxFrequencyRatio | float | 0.45f | Max frequency as ratio of sample rate |
| kMinQ | float | 0.5f | Minimum Q value (filter stability) |
| kMaxQ | float | 20.0f | Maximum Q value (filter stability) |
| kMinShift | float | -24.0f | Minimum shift in semitones |
| kMaxShift | float | 24.0f | Maximum shift in semitones |
| kMinGender | float | -1.0f | Male extreme |
| kMaxGender | float | 1.0f | Female extreme |
| kDefaultSmoothingMs | float | 5.0f | Default smoothing time |

### Filter Stages

| Member | Type | Description |
|--------|------|-------------|
| formants_ | std::array<Biquad, 3> | Three parallel bandpass filters configured as F1, F2, F3 |

### Parameter Smoothers

| Member | Type | Description |
|--------|------|-------------|
| freqSmoothers_ | std::array<OnePoleSmoother, 3> | Smoothers for F1, F2, F3 frequencies |
| bwSmoothers_ | std::array<OnePoleSmoother, 3> | Smoothers for BW1, BW2, BW3 bandwidths |

### Parameters

| Member | Type | Range | Default | Description |
|--------|------|-------|---------|-------------|
| currentVowel_ | Vowel | A, E, I, O, U | A | Discrete vowel selection |
| vowelMorphPosition_ | float | [0, 4] | 0.0 | Continuous morph position |
| formantShift_ | float | [-24, +24] | 0.0 | Semitone shift for all formants |
| gender_ | float | [-1, +1] | 0.0 | Gender scaling (-1=male, +1=female) |
| smoothingTime_ | float | [0.1, 1000] | 5.0 | Smoothing time in ms |

### State

| Member | Type | Description |
|--------|------|-------------|
| sampleRate_ | double | Current sample rate (set by prepare()) |
| prepared_ | bool | true if prepare() has been called |
| useMorphMode_ | bool | true = morph mode, false = discrete vowel |

---

## Relationships

### Dependencies (Layer 1)

```
FormantFilter (Layer 2)
    |
    +-- Biquad (Layer 1)
    |       Used as bandpass filter for each formant
    |
    +-- OnePoleSmoother (Layer 1)
            Used for parameter smoothing
```

### Dependencies (Layer 0)

```
FormantFilter (Layer 2)
    |
    +-- Vowel (Layer 0)
    |       Enum for type-safe vowel selection
    |
    +-- FormantData (Layer 0)
    |       Struct with formant frequencies and bandwidths
    |
    +-- kVowelFormants (Layer 0)
            Constexpr array of FormantData
```

---

## State Transitions

```
                     +-------------+
                     |   Initial   |
                     +------+------+
                            |
                            | prepare(sampleRate)
                            v
                     +------+------+
          +--------->|    Ready    |<---------+
          |          +------+------+          |
          |                 |                 |
          |    setVowel()   |   setVowelMorph()
          |                 |                 |
          |    +------------+------------+    |
          |    |                         |    |
          v    v                         v    v
    +-----+----+----+           +--------+----+-----+
    | Discrete Mode |           |    Morph Mode     |
    | useMorphMode_ |           |   useMorphMode_   |
    |    = false    |           |      = true       |
    +-------+-------+           +---------+---------+
            |                             |
            +----------+   +--------------+
                       |   |
                       v   v
               +-------+---+-------+
               |    Processing     |
               | process() called  |
               +-------+-----------+
                       |
                       | reset()
                       v
               +-------+-----------+
               |  Ready (cleared)  |
               +-------------------+
```

---

## Validation Rules

### Parameter Validation

| Parameter | Validation | Action on Invalid |
|-----------|------------|-------------------|
| vowelMorphPosition | [0.0, 4.0] | Clamp to range |
| formantShift | [-24.0, +24.0] | Clamp to range |
| gender | [-1.0, +1.0] | Clamp to range |
| smoothingTime | [0.1, 1000.0] | Clamp to range |
| formant frequency | [20.0, 0.45*sr] | Clamp after calculation |
| Q value | [0.5, 20.0] | Clamp after calculation |

### Invariants

1. `prepared_` must be true before processing
2. All smoothers must be configured for current sample rate
3. Formant frequencies must be clamped after shift/gender applied
4. Q values must be clamped to prevent instability

---

## Memory Layout

```
FormantFilter object (~720 bytes estimated)
+------------------------------------------+
| formants_[0]: Biquad (~40 bytes)         |
| formants_[1]: Biquad (~40 bytes)         |
| formants_[2]: Biquad (~40 bytes)         |
+------------------------------------------+
| freqSmoothers_[0]: OnePoleSmoother       |
| freqSmoothers_[1]: OnePoleSmoother       |
| freqSmoothers_[2]: OnePoleSmoother       |
+------------------------------------------+
| bwSmoothers_[0]: OnePoleSmoother         |
| bwSmoothers_[1]: OnePoleSmoother         |
| bwSmoothers_[2]: OnePoleSmoother         |
+------------------------------------------+
| currentVowel_: Vowel (1 byte)            |
| [padding]                                |
| vowelMorphPosition_: float (4 bytes)     |
| formantShift_: float (4 bytes)           |
| gender_: float (4 bytes)                 |
| smoothingTime_: float (4 bytes)          |
+------------------------------------------+
| sampleRate_: double (8 bytes)            |
| prepared_: bool (1 byte)                 |
| useMorphMode_: bool (1 byte)             |
| [padding]                                |
+------------------------------------------+
```

Note: All allocations are stack-based (std::array). No heap allocations during processing.
