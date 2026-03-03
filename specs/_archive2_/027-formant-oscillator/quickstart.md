# Quickstart: FOF Formant Oscillator

**Date**: 2026-02-05 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Overview

The `FormantOscillator` generates vowel-like sounds directly using FOF (Fonction d'Onde Formantique) synthesis. Unlike `FormantFilter` which applies resonances to an input signal, this oscillator generates audio without any input.

---

## Basic Usage

### Minimal Setup

```cpp
#include <krate/dsp/processors/formant_oscillator.h>

using namespace Krate::DSP;

// Create and prepare
FormantOscillator osc;
osc.prepare(44100.0);  // Must call before processing

// Configure
osc.setFundamental(110.0f);  // A2, typical male voice range
osc.setVowel(Vowel::A);      // "ah" sound

// Process single sample
float sample = osc.process();
```

### Block Processing

```cpp
// Process a block of samples (more efficient)
std::array<float, 512> buffer;
osc.processBlock(buffer.data(), buffer.size());
```

---

## Vowel Selection

### Discrete Vowels

```cpp
// Select predefined vowels (matches FormantFilter API)
osc.setVowel(Vowel::A);  // "ah" as in "father"
osc.setVowel(Vowel::E);  // "eh" as in "bed"
osc.setVowel(Vowel::I);  // "ee" as in "see"
osc.setVowel(Vowel::O);  // "oh" as in "go"
osc.setVowel(Vowel::U);  // "oo" as in "boot"
```

### Vowel Morphing

```cpp
// Blend between two vowels
osc.morphVowels(Vowel::A, Vowel::O, 0.0f);   // Pure A
osc.morphVowels(Vowel::A, Vowel::O, 0.5f);   // 50% blend
osc.morphVowels(Vowel::A, Vowel::O, 1.0f);   // Pure O

// Continuous position across all vowels
// 0=A, 1=E, 2=I, 3=O, 4=U (fractional = interpolation)
osc.setMorphPosition(0.0f);   // Pure A
osc.setMorphPosition(1.5f);   // Between E and I
osc.setMorphPosition(4.0f);   // Pure U
```

---

## Pitch Control

```cpp
// Set fundamental frequency (pitch)
osc.setFundamental(110.0f);   // A2 - male voice range
osc.setFundamental(220.0f);   // A3 - alto range
osc.setFundamental(440.0f);   // A4 - soprano range

// Formant frequencies remain fixed (source-filter model)
// Only the pitch changes, not the vowel character
```

---

## Per-Formant Control

For creative sound design, you can override individual formant parameters:

```cpp
// Index 0=F1, 1=F2, 2=F3, 3=F4, 4=F5

// Set custom frequencies
osc.setFormantFrequency(0, 800.0f);   // F1 at 800 Hz
osc.setFormantFrequency(1, 1200.0f);  // F2 at 1200 Hz

// Adjust bandwidths
osc.setFormantBandwidth(0, 40.0f);    // Narrow F1
osc.setFormantBandwidth(1, 150.0f);   // Wide F2

// Control amplitudes
osc.setFormantAmplitude(2, 0.0f);     // Disable F3
osc.setFormantAmplitude(3, 0.7f);     // Boost F4

// Query current values
float f1 = osc.getFormantFrequency(0);
float bw1 = osc.getFormantBandwidth(0);
float amp1 = osc.getFormantAmplitude(0);
```

---

## Common Patterns

### Voice Animation (Talking Effect)

```cpp
// Animate morph position over time for "talking" effect
float time = 0.0f;
float morphSpeed = 0.5f;  // Hz

for (size_t i = 0; i < numSamples; ++i) {
    // Oscillate between vowels
    float position = 2.0f + 2.0f * std::sin(kTwoPi * morphSpeed * time);
    osc.setMorphPosition(position);

    output[i] = osc.process();
    time += 1.0f / sampleRate;
}
```

### Pitch Sweep with Fixed Formants

```cpp
// Formants stay fixed while pitch sweeps
// Creates characteristic "source-filter" vocal quality
osc.setVowel(Vowel::A);

float pitch = 110.0f;
for (size_t i = 0; i < numSamples; ++i) {
    osc.setFundamental(pitch);
    output[i] = osc.process();

    pitch *= 1.0001f;  // Slow upward glide
    if (pitch > 440.0f) pitch = 110.0f;
}
```

### Alien Voice Design

```cpp
// Non-standard formant positions for otherworldly sounds
osc.setFormantFrequency(0, 200.0f);   // Very low F1
osc.setFormantFrequency(1, 3000.0f);  // Very high F2
osc.setFormantFrequency(2, 3500.0f);
osc.setFormantFrequency(3, 4000.0f);
osc.setFormantFrequency(4, 4500.0f);

// Wide bandwidths for breathy character
for (size_t i = 0; i < 5; ++i) {
    osc.setFormantBandwidth(i, 200.0f);
}
```

---

## Integration Example

### Stereo Voice with Slight Detuning

```cpp
class StereoFormantVoice {
    FormantOscillator oscL_;
    FormantOscillator oscR_;

public:
    void prepare(double sampleRate) {
        oscL_.prepare(sampleRate);
        oscR_.prepare(sampleRate);
    }

    void setNote(float fundamental) {
        oscL_.setFundamental(fundamental);
        oscR_.setFundamental(fundamental * 1.002f);  // 3 cent detune
    }

    void setVowel(Vowel vowel) {
        oscL_.setVowel(vowel);
        oscR_.setVowel(vowel);
    }

    std::pair<float, float> process() {
        return {oscL_.process(), oscR_.process()};
    }
};
```

---

## Performance Notes

- The oscillator generates 5 formants, each with up to 8 overlapping grains
- Processing cost scales with fundamental frequency (higher = more grains active)
- CPU usage typically < 0.5% per instance at 44.1kHz
- No allocations during processing (all grain pools are fixed-size)

---

## Difference from FormantFilter

| Aspect | FormantFilter | FormantOscillator |
|--------|---------------|-------------------|
| Input | Requires audio input | Generates audio directly |
| Method | 3 parallel bandpass filters | 5 FOF grain generators |
| Formants | F1, F2, F3 only | F1, F2, F3, F4, F5 |
| Use case | Apply vowel character to existing sounds | Create vocal sounds from scratch |
| CPU | Lower (3 biquads) | Higher (5 x 8 grains) |
