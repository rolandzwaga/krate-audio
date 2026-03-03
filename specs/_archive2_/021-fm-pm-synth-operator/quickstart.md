# Quickstart: FM/PM Synthesis Operator

**Feature**: 021-fm-pm-synth-operator
**Date**: 2026-02-05

---

## Overview

FMOperator is a single FM synthesis building block. It produces a sine wave whose phase can be modulated by:
- **External input** (from another operator or modulation source)
- **Self-feedback** (the operator's own previous output)

The output can serve as:
- **Carrier**: Audible output (set level > 0)
- **Modulator**: Feed `lastRawOutput()` to another operator's phase input

---

## Basic Usage

### Single Operator (Carrier Only)

```cpp
#include <krate/dsp/processors/fm_operator.h>

using namespace Krate::DSP;

// Create and initialize
FMOperator op;
op.prepare(44100.0);  // Sample rate

// Configure
op.setFrequency(440.0f);  // Base frequency: 440 Hz
op.setRatio(1.0f);        // Effective freq = 440 * 1.0 = 440 Hz
op.setFeedback(0.0f);     // Pure sine (no self-modulation)
op.setLevel(1.0f);        // Full amplitude

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = op.process();
}
```

### Single Operator with Feedback

Feedback adds harmonics, transitioning from sine to sawtooth-like:

```cpp
FMOperator op;
op.prepare(44100.0);
op.setFrequency(110.0f);
op.setRatio(1.0f);
op.setFeedback(0.5f);  // Moderate harmonics
op.setLevel(1.0f);

// Output is a harmonically rich waveform
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = op.process();
}
```

**Feedback values:**
| Feedback | Character |
|----------|-----------|
| 0.0 | Pure sine |
| 0.2 | Slightly asymmetric |
| 0.5 | Saw-like harmonics |
| 1.0 | Maximum richness |

---

## Two-Operator FM

The classic FM setup: one modulator feeding into one carrier.

```cpp
FMOperator modulator, carrier;

// Initialize both
modulator.prepare(44100.0);
carrier.prepare(44100.0);

// Modulator: 2x carrier frequency
modulator.setFrequency(440.0f);
modulator.setRatio(2.0f);     // 880 Hz modulator
modulator.setFeedback(0.0f);  // Clean modulator
modulator.setLevel(0.5f);     // Controls modulation depth

// Carrier: fundamental frequency
carrier.setFrequency(440.0f);
carrier.setRatio(1.0f);       // 440 Hz carrier
carrier.setFeedback(0.0f);
carrier.setLevel(1.0f);

// Process: modulator -> carrier
for (size_t i = 0; i < numSamples; ++i) {
    // Process modulator first
    modulator.process();

    // Get raw modulator output for PM
    // Scale by modulator level for modulation index control
    float modulationIndex = 0.5f;  // Adjust for timbre
    float pm = modulator.lastRawOutput() * modulationIndex;

    // Process carrier with modulation
    output[i] = carrier.process(pm);
}
```

**Modulation Index Effect:**
| Index | Spectrum |
|-------|----------|
| 0.0 | Pure carrier fundamental |
| 0.5 | Fundamental + sidebands |
| 1.0 | Strong sidebands |
| 2.0+ | Complex metallic tones |

---

## Common FM Ratios

### Harmonic Ratios (Musical)

| Ratio | Interval | Use Case |
|-------|----------|----------|
| 1:1 | Unison | Thickening, chorus |
| 2:1 | Octave | Bright, organ-like |
| 3:1 | Octave + fifth | Clarinet-like |
| 4:1 | Two octaves | Flute-like |

```cpp
// Organ-like timbre (2:1 ratio)
modulator.setRatio(2.0f);
carrier.setRatio(1.0f);
```

### Inharmonic Ratios (Metallic)

| Ratio | Character |
|-------|-----------|
| 1.41 | Bell, chime |
| 2.76 | Metallic ping |
| 7.0 | Harsh, aggressive |

```cpp
// Bell-like timbre
modulator.setRatio(1.41f);  // sqrt(2)
carrier.setRatio(1.0f);
modulator.setLevel(1.0f);   // High mod index for bell
```

---

## Lifecycle Management

### For Polyphonic Synthesis

```cpp
class Voice {
    FMOperator op1_, op2_;

public:
    void prepare(double sampleRate) {
        op1_.prepare(sampleRate);
        op2_.prepare(sampleRate);
    }

    void noteOn(float frequency) {
        // Reset phase for clean attack
        op1_.reset();
        op2_.reset();

        // Set frequency (ratio preserved)
        op1_.setFrequency(frequency);
        op2_.setFrequency(frequency);
    }
};
```

### Key Points

- `prepare()`: Call once at initialization or sample rate change
- `reset()`: Call on note-on for clean phase reset
- Configuration (frequency, ratio, feedback, level) preserved across `reset()`

---

## Advanced: Combined Feedback and External PM

An operator can receive both self-feedback AND external modulation:

```cpp
FMOperator modulator, carrier;
modulator.prepare(44100.0);
carrier.prepare(44100.0);

// Modulator
modulator.setFrequency(220.0f);
modulator.setRatio(3.0f);
modulator.setLevel(0.3f);

// Carrier with both feedback and external PM
carrier.setFrequency(220.0f);
carrier.setRatio(1.0f);
carrier.setFeedback(0.3f);  // Self-feedback enabled
carrier.setLevel(1.0f);

for (size_t i = 0; i < numSamples; ++i) {
    modulator.process();
    float pm = modulator.lastRawOutput() * 0.3f;
    // Carrier combines external PM with internal feedback
    output[i] = carrier.process(pm);
}
```

---

## Edge Cases

### Before prepare()

```cpp
FMOperator op;
// NOT prepared
float sample = op.process();  // Returns 0.0, no crash
```

### Zero Frequency

```cpp
op.setFrequency(0.0f);
float sample = op.process();  // Returns 0.0 (silence)
```

### NaN/Infinity Input

```cpp
op.setFrequency(std::numeric_limits<float>::quiet_NaN());
// Treated as 0.0 Hz

float sample = op.process(std::numeric_limits<float>::infinity());
// PM sanitized, output bounded
```

---

## Performance Notes

| Aspect | Value |
|--------|-------|
| Memory per operator | ~90 KB (wavetable) |
| CPU per operator | < 0.5% at 44.1 kHz |
| Real-time safe | process(), setters |
| NOT real-time safe | prepare() |

For FM Voice with 6 operators:
- Memory: ~540 KB (or ~90 KB with shared table optimization)
- CPU: Well under 3% total

---

## Related Components

| Component | Use Case |
|-----------|----------|
| FM Voice (Phase 9) | 4-6 operator FM with algorithm routing |
| WavetableOscillator | Underlying oscillator engine |
| LFO | Vibrato, tremolo modulation |
| Envelope | Modulation index control over time |
