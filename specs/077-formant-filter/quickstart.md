# Quickstart: Formant Filter

**Feature**: 077-formant-filter
**Date**: 2026-01-21

## Overview

The FormantFilter is a Layer 2 DSP processor that creates vocal "talking" effects by applying parallel bandpass filters at formant frequencies (F1, F2, F3).

---

## Basic Usage

### Include and Namespace

```cpp
#include <krate/dsp/processors/formant_filter.h>

using namespace Krate::DSP;
```

### Simple Discrete Vowel

```cpp
FormantFilter filter;

// Initialize for your sample rate
filter.prepare(44100.0);

// Select a vowel
filter.setVowel(Vowel::A);

// In your audio callback
void processAudio(float* buffer, int numSamples) {
    filter.processBlock(buffer, numSamples);
}
```

### Sample-by-Sample Processing

```cpp
FormantFilter filter;
filter.prepare(44100.0);
filter.setVowel(Vowel::E);

// Process single samples
for (int i = 0; i < numSamples; ++i) {
    output[i] = filter.process(input[i]);
}
```

---

## Vowel Morphing

### Continuous Morphing Between Vowels

```cpp
FormantFilter filter;
filter.prepare(44100.0);

// Morph position: 0=A, 1=E, 2=I, 3=O, 4=U
filter.setVowelMorph(0.5f);  // Halfway between A and E

// Animate the morph with an LFO
void processWithLFO(float* buffer, int numSamples, float lfoPhase) {
    for (int i = 0; i < numSamples; ++i) {
        // Sweep through all vowels
        float morphPos = 4.0f * (0.5f + 0.5f * std::sin(lfoPhase));
        filter.setVowelMorph(morphPos);
        buffer[i] = filter.process(buffer[i]);
        lfoPhase += 0.0001f;  // LFO increment
    }
}
```

### Morph Position Mapping

| Position | Vowel(s) | Sound |
|----------|----------|-------|
| 0.0 | A | "ah" (father) |
| 0.5 | A-E | between "ah" and "eh" |
| 1.0 | E | "eh" (bed) |
| 1.5 | E-I | between "eh" and "ee" |
| 2.0 | I | "ee" (see) |
| 2.5 | I-O | between "ee" and "oh" |
| 3.0 | O | "oh" (go) |
| 3.5 | O-U | between "oh" and "oo" |
| 4.0 | U | "oo" (boot) |

---

## Formant Shifting

### Pitch-Independent Character Change

```cpp
FormantFilter filter;
filter.prepare(44100.0);
filter.setVowel(Vowel::A);

// Shift formants up one octave (chipmunk effect)
filter.setFormantShift(12.0f);

// Shift formants down one octave (giant voice effect)
filter.setFormantShift(-12.0f);

// Shift by a fifth up
filter.setFormantShift(7.0f);
```

### Shift Values Reference

| Semitones | Effect |
|-----------|--------|
| -24 | Two octaves down (very deep) |
| -12 | One octave down (bass) |
| -7 | Fifth down |
| 0 | No change |
| +7 | Fifth up |
| +12 | One octave up (chipmunk) |
| +24 | Two octaves up (very high) |

---

## Gender Parameter

### Adjust Perceived Gender

```cpp
FormantFilter filter;
filter.prepare(44100.0);
filter.setVowel(Vowel::E);

// Male character (formants ~17% lower)
filter.setGender(-1.0f);

// Neutral
filter.setGender(0.0f);

// Female character (formants ~19% higher)
filter.setGender(1.0f);
```

### Combining Gender with Shift

```cpp
// Create a specific character
filter.setVowel(Vowel::I);
filter.setFormantShift(-6.0f);   // Lower by half octave
filter.setGender(0.5f);          // Slightly feminine

// The transformations combine multiplicatively:
// finalFreq = baseFreq * shiftMultiplier * genderMultiplier
```

---

## Smoothing Control

### Adjust Transition Speed

```cpp
FormantFilter filter;
filter.prepare(44100.0);

// Fast transitions (may click if too fast)
filter.setSmoothingTime(1.0f);   // 1ms

// Default (click-free)
filter.setSmoothingTime(5.0f);   // 5ms (default)

// Slow, obvious transitions
filter.setSmoothingTime(50.0f);  // 50ms
```

### Smoothing Guidelines

| Use Case | Recommended Time |
|----------|------------------|
| Fast LFO modulation | 1-2 ms |
| Standard parameter changes | 5 ms |
| Slow morphing effects | 10-20 ms |
| Very slow sweeps | 50-100 ms |

---

## Complete Example: Talking Wah

```cpp
#include <krate/dsp/processors/formant_filter.h>
#include <krate/dsp/processors/envelope_follower.h>  // If available

using namespace Krate::DSP;

class TalkingWah {
public:
    void prepare(double sampleRate) {
        formant_.prepare(sampleRate);
        envelope_.prepare(sampleRate);

        // Configure envelope follower
        envelope_.setAttackTime(10.0f);
        envelope_.setReleaseTime(100.0f);

        // Fast smoothing for responsive wah
        formant_.setSmoothingTime(2.0f);
    }

    float process(float input) {
        // Get envelope level (0-1)
        float level = envelope_.process(input);

        // Map envelope to vowel morph (A when quiet, I when loud)
        float morphPos = level * 2.0f;  // 0-2 range (A to I)
        formant_.setVowelMorph(morphPos);

        // Process through formant filter
        return formant_.process(input);
    }

private:
    FormantFilter formant_;
    EnvelopeFollower envelope_;
};
```

---

## Performance Notes

1. **CPU Usage**: Approximately 3 biquad filter operations per sample (~50ns typical)

2. **Memory**: No heap allocations after prepare()

3. **Thread Safety**: NOT thread-safe. Use from audio thread only.

4. **Latency**: Zero samples (no lookahead)

---

## Common Patterns

### Pattern 1: Vowel Sequence (A-E-I-O-U)

```cpp
// Cycle through vowels with envelope
float cycleVowels(float input, float phase) {
    float morphPos = 4.0f * std::fmod(phase, 1.0f);
    filter.setVowelMorph(morphPos);
    return filter.process(input);
}
```

### Pattern 2: Fixed Vowel with Gender Animation

```cpp
// Animate gender for "morphing" effect
void animateGender(float* buffer, int numSamples, float& phase) {
    filter.setVowel(Vowel::A);
    for (int i = 0; i < numSamples; ++i) {
        float gender = std::sin(phase);  // -1 to +1
        filter.setGender(gender);
        buffer[i] = filter.process(buffer[i]);
        phase += 0.0002f;
    }
}
```

### Pattern 3: MIDI-Controlled Vowel

```cpp
// Map MIDI CC to vowel morph
void handleMidiCC(int cc, int value) {
    if (cc == 1) {  // Mod wheel
        float morphPos = (value / 127.0f) * 4.0f;
        filter.setVowelMorph(morphPos);
    }
}
```

---

## Troubleshooting

### No Sound Output
- Ensure `prepare()` was called with correct sample rate
- Check that input has signal content
- Verify formant frequencies are within audible range

### Clicking/Popping
- Increase smoothing time (`setSmoothingTime()`)
- Avoid instant parameter jumps
- Call `reset()` before starting new audio

### Formants Sound Wrong
- Verify sample rate matches your audio system
- Check that shift/gender values are reasonable
- Ensure formant frequencies aren't clamped to extremes

### High CPU Usage
- Use `processBlock()` instead of per-sample `process()`
- Reduce parameter modulation rate
- Consider higher smoothing time (fewer coefficient updates)
