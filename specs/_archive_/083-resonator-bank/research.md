# Research: Resonator Bank

**Feature**: 083-resonator-bank | **Date**: 2026-01-22

## Research Summary

All technical questions from the spec have been resolved through clarifications. This document consolidates the research findings and implementation decisions.

---

## Topic 1: RT60-to-Q Conversion Formula

### Question
What formula should convert decay time (RT60 in seconds) to filter Q factor?

### Research
The RT60 time is defined as the time for a signal to decay by 60dB (factor of 1/1000 in amplitude). For a resonant bandpass filter, the decay envelope is related to the filter's Q factor and center frequency.

**Derivation:**
- A resonant filter's impulse response decays exponentially: `A(t) = A0 * exp(-pi * f * t / Q)`
- At RT60, the amplitude has decayed by 60dB: `A(RT60) = A0 / 1000`
- Solving: `exp(-pi * f * RT60 / Q) = 1/1000`
- Taking natural log: `-pi * f * RT60 / Q = ln(1/1000) = -ln(1000)`
- Rearranging: `Q = (pi * f * RT60) / ln(1000)`

### Decision
**Formula**: `Q = (pi * frequency * RT60) / ln(1000)`

Where:
- `frequency` is the resonator center frequency in Hz
- `RT60` is the decay time in seconds
- `ln(1000) = 6.907755...`

### Rationale
This is the standard acoustic RT60 formula used in room acoustics and physical modeling. It provides perceptually accurate decay times.

### Implementation
```cpp
[[nodiscard]] inline constexpr float rt60ToQ(float frequency, float rt60Seconds) noexcept {
    constexpr float kLn1000 = 6.907755278982137f;  // ln(1000)
    if (rt60Seconds <= 0.0f || frequency <= 0.0f) {
        return 0.1f;  // Minimum Q for instant decay
    }
    return (kPi * frequency * rt60Seconds) / kLn1000;
}
```

---

## Topic 2: Trigger Implementation (Impulse Excitation)

### Question
How should the resonator state be initialized when trigger() is called?

### Research
For physical modeling, exciting a resonant filter mimics striking a physical object. The standard approach is impulse excitation - feeding a single non-zero sample followed by zeros.

**Two approaches considered:**
1. **Input Impulse**: Feed `amplitude * delta[n]` as input to filters
2. **State Injection**: Directly set filter z1/z2 states

The input impulse approach is cleaner and doesn't require knowledge of internal filter topology.

### Decision
**Approach**: Impulse excitation via internal flag

When `trigger(velocity)` is called:
1. Store velocity amplitude (0.0-1.0)
2. Set internal `triggerPending_` flag
3. On next `process()` call, add velocity to input for one sample
4. Clear flag after processing

### Rationale
- Doesn't require modifying Biquad internals
- Works naturally with the filtering topology
- Produces immediate output (within 1 sample, as per SC-004)
- Velocity scales excitation amplitude naturally

### Implementation
```cpp
void trigger(float velocity = 1.0f) noexcept {
    triggerVelocity_ = std::clamp(velocity, 0.0f, 1.0f);
    triggerPending_ = true;
}

[[nodiscard]] float process(float input) noexcept {
    float excitation = input;
    if (triggerPending_) {
        excitation += triggerVelocity_;
        triggerPending_ = false;
    }
    // ... rest of processing
}
```

---

## Topic 3: Spectral Tilt Implementation

### Question
Should spectral tilt apply gain adjustment per-resonator or as post-processing?

### Research
Spectral tilt shapes the brightness/darkness of the overall sound by attenuating high frequencies relative to low frequencies (or vice versa).

**Two approaches:**
1. **Per-resonator gain**: Adjust each resonator's output gain based on its frequency
2. **Post-processing filter**: Apply a tilt EQ after summing all resonators

Per-resonator adjustment is more accurate to physical modeling (higher partials naturally decay faster) and allows different tilt characteristics than a simple filter.

### Decision
**Approach**: Per-resonator gain adjustment based on frequency

**Formula**: `tiltGain = pow(frequency / referenceFrequency, tiltAmount / 6.0)`

Where:
- `referenceFrequency` = 1000 Hz (pivot point)
- `tiltAmount` = dB/octave (positive = boost highs, negative = cut highs)

### Rationale
- Per-resonator approach matches spec clarification
- Reference at 1000 Hz is standard for tilt EQs
- Division by 6.0 converts dB/octave to gain exponent (6dB = 1 octave = 2x frequency)

### Implementation
```cpp
[[nodiscard]] inline float calculateTiltGain(float frequency, float tiltDbPerOctave) noexcept {
    constexpr float kReferenceFrequency = 1000.0f;
    if (tiltDbPerOctave == 0.0f) return 1.0f;

    // octaves = log2(frequency / reference)
    // gain = 10^(tiltDbPerOctave * octaves / 20)
    // Simplified: gain = (frequency / reference)^(tiltDbPerOctave / 6.0206)
    const float octaves = std::log2(frequency / kReferenceFrequency);
    return dbToGain(tiltDbPerOctave * octaves);
}
```

---

## Topic 4: Parameter Smoothing

### Question
What smoothing time constant should OnePoleSmoother use?

### Research
Parameter smoothing prevents zipper noise when parameters change. The time constant should be:
- Long enough to prevent audible clicks/zips
- Short enough to feel responsive
- Consistent with other processors in the codebase

Existing processors use 5ms (default) to 50ms depending on parameter type.

### Decision
**Time Constant**: 20ms for all parameters

### Rationale
- 20ms is long enough to smooth any audible discontinuity
- Short enough for responsive automation
- Matches the spec clarification exactly
- Used for: frequency, decay, gain, Q, damping, exciter mix, spectral tilt

---

## Topic 5: Reset Behavior

### Question
Should reset() clear only filter states or also reset parameters?

### Research
Reset semantics vary across components:
- Some reset() functions only clear state (for seamless continuation)
- Some reset() functions restore defaults (for re-initialization)

### Decision
**Behavior**: Clear filter states AND reset parameters to defaults

Per spec clarification, reset() will:
1. Clear all Biquad filter states (z1, z2)
2. Reset all smoothers
3. Reset all per-resonator parameters to defaults
4. Reset all global parameters to defaults
5. Clear trigger state

### Rationale
- Matches spec clarification exactly
- User must reconfigure tuning after reset
- Provides clean slate for new sound design

---

## Topic 6: Inharmonicity Formula

### Question
What formula should be used for inharmonic series?

### Research
The inharmonicity formula models stiff string behavior where partials are progressively sharper than harmonic:

`f_n = f_0 * n * sqrt(1 + B * n^2)`

Where:
- `f_0` = fundamental frequency
- `n` = partial number (1, 2, 3, ...)
- `B` = inharmonicity coefficient (0 = harmonic, higher = more stretched)

Typical B values:
- Piano bass strings: B = 0.0001 to 0.001
- Piano treble strings: B = 0.001 to 0.01
- Bells: B = 0.01 to 0.1
- Bars/marimbas: B can be higher

### Decision
**Formula**: `f_n = f_0 * n * sqrt(1 + B * n^2)`

**Parameter range**: B = 0.0 to 1.0 (user-controlled)

### Rationale
This is the standard Railsback curve / stiff-string formula used in physical modeling. The range 0-1 covers all practical use cases from nearly harmonic to heavily stretched.

### Implementation
```cpp
[[nodiscard]] inline float calculateInharmonicFrequency(
    float fundamental, int partial, float inharmonicity
) noexcept {
    const float n = static_cast<float>(partial);
    const float stretch = std::sqrt(1.0f + inharmonicity * n * n);
    return fundamental * n * stretch;
}
```

---

## Topic 7: Q Clamping Range

### Question
The spec says Q should be clamped to max 100, but Biquad's kMaxQ is 30. What range should be used?

### Research
Very high Q values (>30) can cause:
- Numerical instability in IIR filters
- Self-oscillation
- Very narrow bandwidth that's difficult to control

However, for resonator banks modeling bells and bars, high Q values are needed for long sustain.

### Decision
**Q Range**: 0.1 to 100

The spec explicitly states Q should be clamped to 100 (FR-015 edge case). We'll define our own constant rather than use biquad's kMaxQ.

### Rationale
- Spec requirements take precedence
- High Q is needed for physical modeling
- The Biquad class will still handle stability internally
- We'll document that very high Q may require careful tuning

### Implementation
```cpp
// ResonatorBank-specific constants
inline constexpr float kMinResonatorQ = 0.1f;
inline constexpr float kMaxResonatorQ = 100.0f;
inline constexpr float kMaxDecayTime = 30.0f;   // seconds
inline constexpr float kMinDecayTime = 0.001f;  // 1ms minimum
```

---

## Topic 8: Frequency Clamping

### Question
What frequency range should resonators support?

### Research
Per spec edge case: frequencies should be clamped to 20Hz to sampleRate * 0.45 (Nyquist/2 safety margin).

The Biquad class has kMinFilterFrequency = 1.0f, but 20Hz is more practical for audible resonators.

### Decision
**Frequency Range**: 20 Hz to sampleRate * 0.45

### Rationale
- Matches spec edge case exactly
- 20Hz is lowest audible frequency
- 0.45 * sampleRate provides margin before Nyquist artifacts

---

## Summary of Decisions

| Topic | Decision |
|-------|----------|
| RT60-to-Q | `Q = (pi * f * RT60) / ln(1000)` |
| Trigger | Impulse excitation via input addition |
| Spectral Tilt | Per-resonator gain: `pow(f/1000, tilt/6)` |
| Smoothing | 20ms time constant |
| Reset | Clear states AND parameters |
| Inharmonicity | `f_n = f_0 * n * sqrt(1 + B * n^2)` |
| Q Range | 0.1 to 100 |
| Frequency Range | 20 Hz to sampleRate * 0.45 |

All clarifications have been addressed. Ready for Phase 1 design.
