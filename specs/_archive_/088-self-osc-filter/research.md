# Research: Self-Oscillating Filter

**Feature**: 088-self-osc-filter | **Date**: 2026-01-23

## Research Summary

This document captures research findings for the Self-Oscillating Filter processor.

---

## 1. LadderFilter Self-Oscillation Behavior

### Decision
Use LadderFilter with resonance >= 3.95 for reliable self-oscillation, with Nonlinear model for authentic analog character.

### Rationale
The existing LadderFilter primitive (Layer 1) already supports self-oscillation through its resonance control:
- Resonance range: 0.0 to 4.0 (kMaxResonance)
- Self-oscillation threshold: approximately 3.9
- For reliable, stable oscillation: use 3.95 or higher
- The Nonlinear model (Huovilainen) provides more authentic analog oscillation character with per-stage tanh saturation

### Alternatives Considered

| Approach | Pros | Cons | Decision |
|----------|------|------|----------|
| Use LadderFilter | Already implemented, tested, analog character | Oscillation tied to resonance parameter | **Selected** |
| Build dedicated oscillator | Full control over oscillation | Duplicates filter work, no filter ping mode | Rejected |
| Use SVF filter | Modulation stable | Less classic character, no existing self-osc | Rejected |

### Key Parameters from LadderFilter
```cpp
static constexpr float kMinCutoff = 20.0f;
static constexpr float kMaxCutoffRatio = 0.45f;  // Max freq = sampleRate * 0.45
static constexpr float kMinResonance = 0.0f;
static constexpr float kMaxResonance = 4.0f;
```

---

## 2. Envelope Design for Oscillator

### Decision
Use a 4-state envelope (Idle, Attack, Sustain, Release) with OnePoleSmoother for exponential curves.

### Rationale
The spec requires:
- Attack time: 0-20ms (FR-006b)
- Release time: 10-2000ms (FR-006)
- Retrigger: restart attack from current level (FR-008b)

An exponential envelope is natural for audio and matches the behavior of OnePoleSmoother, which is already available in Layer 1.

### State Machine Design
```
States: IDLE, ATTACK, SUSTAIN, RELEASE

Transitions:
- IDLE -> ATTACK: noteOn() called
- ATTACK -> SUSTAIN: envelope level reaches target (99%)
- SUSTAIN -> RELEASE: noteOff() called
- RELEASE -> IDLE: envelope level drops below threshold (-60dB)
- ANY -> ATTACK: noteOn() during active state (retrigger from current level)
```

### Implementation Approach
Use separate OnePoleSmoother instances configured for attack and release times:
- Attack smoother: configured with attack time, targets velocity gain
- Release smoother: configured with release time, targets 0.0

During processing:
- In ATTACK: use attack smoother moving toward target
- In RELEASE: use release smoother moving toward 0
- Track current level for retrigger support

### Alternatives Considered

| Approach | Pros | Cons | Decision |
|----------|------|------|----------|
| OnePoleSmoother with reconfiguration | Simple, existing component | Need to reconfigure on state change | Rejected |
| Two dedicated smoothers | Clean separation, no reconfiguration | Slightly more memory | **Selected** |
| ADSR envelope primitive | More features | Overkill, doesn't exist in codebase | Rejected |
| Linear ramp envelope | Simple | Less natural sound | Rejected |

---

## 3. Glide Implementation

### Decision
Use LinearRamp for frequency (Hz) interpolation per spec FR-010.

### Rationale
The spec explicitly states:
> "Glide MUST use smooth interpolation (linear frequency ramp, perceived as constant-rate pitch change)"

This means:
- LinearRamp operates on Hz values
- A glide from 440Hz to 880Hz over 100ms would traverse 4400 Hz/sec
- This creates a constant frequency change rate, which is perceived as accelerating pitch (since pitch perception is logarithmic)

The alternative (logarithmic frequency) would give constant semitones/sec, but the spec explicitly requests linear frequency.

### Implementation
```cpp
// On noteOn with glide > 0:
frequencyRamp_.configure(glideMs_, sampleRate_);
frequencyRamp_.setTarget(newFrequency);

// In process():
float currentFreq = frequencyRamp_.process();
filter_.setCutoff(currentFreq);
```

### Alternatives Considered

| Approach | Pros | Cons | Decision |
|----------|------|------|----------|
| Linear frequency ramp | Matches spec, simple | Accelerating pitch perception | **Selected** |
| Logarithmic (pitch) ramp | Constant semitones/sec | Doesn't match spec | Rejected |
| Portamento with time stretch | Musical | More complex, spec is clear | Rejected |

---

## 4. MIDI Note to Frequency Conversion

### Decision
Create `midiNoteToFrequency()` utility in new Layer 0 file `core/midi_utils.h`.

### Rationale
Standard 12-TET (twelve-tone equal temperament) formula:
```cpp
frequency = 440.0 * pow(2.0, (midiNote - 69) / 12.0)
```

This is a standard conversion that will be reused by future melodic DSP components (ring modulator, FM synthesis, etc.), so it belongs in Layer 0.

### Formula Verification

| MIDI Note | Note Name | Expected Frequency | Formula Result |
|-----------|-----------|-------------------|----------------|
| 21 | A0 | 27.5 Hz | 27.5 Hz |
| 60 | C4 (Middle C) | 261.63 Hz | 261.63 Hz |
| 69 | A4 | 440.0 Hz | 440.0 Hz |
| 81 | A5 | 880.0 Hz | 880.0 Hz |
| 108 | C8 | 4186.01 Hz | 4186.01 Hz |

### Implementation
```cpp
// In core/midi_utils.h
namespace Krate::DSP {

/// Convert MIDI note number to frequency using 12-TET tuning.
/// @param midiNote MIDI note number (0-127, where 69 = A4)
/// @param a4Frequency Reference frequency for A4 (default 440 Hz)
/// @return Frequency in Hz
[[nodiscard]] constexpr float midiNoteToFrequency(
    int midiNote,
    float a4Frequency = 440.0f
) noexcept {
    // Use constexprExp from db_utils.h for constexpr compatibility
    // pow(2, x) = exp(x * ln(2))
    constexpr float kLn2 = 0.693147181f;
    const float semitones = static_cast<float>(midiNote - 69) / 12.0f;
    return a4Frequency * detail::constexprExp(semitones * kLn2);
}

} // namespace Krate::DSP
```

---

## 5. Velocity to Gain Mapping

### Decision
Use linear velocity-to-gain mapping: `gain = velocity / 127.0f`

### Rationale
The spec states (FR-007):
> "Velocity parameter in noteOn() MUST scale the output level proportionally (velocity 127 = full level, velocity 64 = approximately -6 dB)"

Verification:
- velocity 127: gain = 127/127 = 1.0 (0 dB)
- velocity 64: gain = 64/127 = 0.504 (-5.95 dB, approximately -6 dB)

This linear mapping satisfies the spec requirement.

### Implementation
```cpp
// In core/midi_utils.h
namespace Krate::DSP {

/// Convert MIDI velocity to linear gain.
/// Uses linear mapping where velocity 127 = 1.0 and velocity 64 ≈ -6 dB.
/// @param velocity MIDI velocity (0-127)
/// @return Linear gain multiplier (0.0 to 1.0)
[[nodiscard]] constexpr float velocityToGain(int velocity) noexcept {
    // Clamp to valid MIDI range
    const int clamped = (velocity < 0) ? 0 : (velocity > 127) ? 127 : velocity;
    return static_cast<float>(clamped) / 127.0f;
}

} // namespace Krate::DSP
```

### Alternatives Considered

| Curve | Formula | v=64 Result | Decision |
|-------|---------|-------------|----------|
| Linear | v/127 | -5.95 dB | **Selected** (matches spec) |
| Quadratic | (v/127)^2 | -12 dB | Rejected |
| dB-linear | dbToGain(-48 + 48*v/127) | -24 dB | Rejected |

---

## 6. Wave Shaping Implementation

### Decision
Use `FastMath::fastTanh()` with input gain scaling from 1x to 3x.

### Rationale
The spec states (FR-015):
> "The amount parameter (0.0 to 1.0) scales the input gain before tanh: 0.0 applies unity gain (clean), 1.0 applies 3x gain (moderate saturation)."

Implementation:
```cpp
float applyWaveShaping(float input) const noexcept {
    if (waveShapeAmount_ <= 0.0f) return input;
    // Map 0-1 to 1x-3x gain
    float gain = 1.0f + waveShapeAmount_ * 2.0f;
    return FastMath::fastTanh(input * gain);
}
```

This provides:
- At amount 0.0: gain = 1.0, tanh(x) ≈ x for small x (clean)
- At amount 0.5: gain = 2.0, moderate saturation
- At amount 1.0: gain = 3.0, visible soft clipping

The output is bounded to [-1, 1] by tanh, and DCBlocker2 removes any DC offset.

---

## 7. External Input Mixing

### Decision
Mix external input with filter input before processing, with 0 = oscillation only, 1 = external only.

### Rationale
Per FR-012 and FR-013:
- `setExternalInput(mix)` where 0.0 = oscillation only, 1.0 = external input only
- The filter processes the external signal with current cutoff and resonance

Signal flow:
```cpp
// In process(float externalInput):
float mix = mixSmoother_.process();
float filterInput = externalInput * mix;  // External portion
float output = filter_.process(filterInput);
// The self-oscillation is inherent in the filter when resonance is high
```

When mix = 0, no external signal enters the filter, but self-oscillation continues.
When mix = 1, external signal fully enters, and the filter processes it with its resonant character.

---

## References

- LadderFilter header: `dsp/include/krate/dsp/primitives/ladder_filter.h`
- DCBlocker2 header: `dsp/include/krate/dsp/primitives/dc_blocker.h`
- Smoother header: `dsp/include/krate/dsp/primitives/smoother.h`
- MIDI specification: https://www.midi.org/specifications
- 12-TET: https://en.wikipedia.org/wiki/Equal_temperament
