# Quickstart: Formant Distortion Processor

**Spec**: 105-formant-distortion | **Layer**: 2 (Processor)

## Overview

FormantDistortion is a Layer 2 processor that creates "talking distortion" effects by combining vowel-shaped formant filtering with waveshaping saturation. It supports discrete vowel selection (A, E, I, O, U), continuous vowel morphing, and envelope-controlled formant modulation.

## Basic Usage

```cpp
#include <krate/dsp/processors/formant_distortion.h>

using namespace Krate::DSP;

// Create and prepare
FormantDistortion distortion;
distortion.prepare(44100.0, 512);  // sample rate, max block size

// Configure
distortion.setVowel(Vowel::A);           // Select vowel A
distortion.setDrive(2.0f);               // 2x drive
distortion.setDistortionType(WaveshapeType::Tanh);

// Process
float output = distortion.process(input);

// Or block processing
distortion.process(buffer, numSamples);
```

## Vowel Selection

### Discrete Vowels

```cpp
// Select specific vowels
distortion.setVowel(Vowel::A);  // "ah" sound
distortion.setVowel(Vowel::E);  // "eh" sound
distortion.setVowel(Vowel::I);  // "ee" sound
distortion.setVowel(Vowel::O);  // "oh" sound
distortion.setVowel(Vowel::U);  // "oo" sound
```

### Continuous Morphing

```cpp
// Smooth interpolation between vowels
// 0.0=A, 1.0=E, 2.0=I, 3.0=O, 4.0=U
distortion.setVowelBlend(0.0f);   // Pure A
distortion.setVowelBlend(0.5f);   // Halfway A-E
distortion.setVowelBlend(2.5f);   // Halfway I-O
distortion.setVowelBlend(4.0f);   // Pure U
```

## Formant Shifting

```cpp
// Shift formants up/down by semitones
distortion.setFormantShift(0.0f);    // No shift
distortion.setFormantShift(12.0f);   // Up one octave (chipmunk)
distortion.setFormantShift(-12.0f);  // Down one octave (monster)
```

## Distortion Types

```cpp
// Warm saturation
distortion.setDistortionType(WaveshapeType::Tanh);
distortion.setDistortionType(WaveshapeType::Tube);

// Aggressive saturation
distortion.setDistortionType(WaveshapeType::HardClip);

// Drive controls intensity
distortion.setDrive(1.0f);   // Subtle (near unity)
distortion.setDrive(5.0f);   // Moderate
distortion.setDrive(20.0f);  // Aggressive
```

## Envelope Following

```cpp
// Enable envelope-controlled formant modulation
distortion.setEnvelopeFollowAmount(1.0f);   // Full modulation
distortion.setEnvelopeModRange(12.0f);      // Up to +12 semitones

// Adjust envelope timing
distortion.setEnvelopeAttack(10.0f);   // 10ms attack
distortion.setEnvelopeRelease(100.0f); // 100ms release
```

**Effect**: Louder input = higher formants (brighter, more open vowel character).

**Formula**: `finalShift = staticShift + (envelope * modRange * amount)`

## Mix Control

```cpp
distortion.setMix(0.0f);   // Dry only
distortion.setMix(0.5f);   // 50/50 blend
distortion.setMix(1.0f);   // Wet only (default)
```

## Common Recipes

### Talking Lead Synth

```cpp
distortion.setVowelBlend(0.0f);  // Start at A
distortion.setDrive(3.0f);
distortion.setDistortionType(WaveshapeType::Tube);
distortion.setEnvelopeFollowAmount(0.5f);
// Automate vowelBlend from 0 to 4 for "aeiou" effect
```

### Dynamic Wah-Like Effect

```cpp
distortion.setVowel(Vowel::A);
distortion.setFormantShift(-6.0f);
distortion.setEnvelopeFollowAmount(1.0f);
distortion.setEnvelopeModRange(18.0f);
distortion.setEnvelopeAttack(5.0f);
distortion.setEnvelopeRelease(50.0f);
// Playing dynamics control formant sweep
```

### Alien Texture

```cpp
distortion.setVowel(Vowel::I);
distortion.setFormantShift(24.0f);  // Maximum shift up
distortion.setDrive(10.0f);
distortion.setDistortionType(WaveshapeType::HardClip);
```

### Subtle Vowel Coloring

```cpp
distortion.setVowel(Vowel::O);
distortion.setDrive(1.0f);  // Unity (minimal distortion)
distortion.setMix(0.3f);    // Blend 30% wet
```

## Signal Flow

```
Input ──> EnvelopeFollower ──> FormantFilter ──> Waveshaper ──> DCBlocker ──> Mix ──> Output
               │                    ^
               └────────────────────┘
               (modulates formant shift)
```

## Thread Safety

- NOT thread-safe
- Call setters from audio thread only
- All processing methods are noexcept and allocation-free
- Use atomic variables for UI-to-audio parameter transfer

## Stereo Usage

FormantDistortion is mono. For stereo:

```cpp
FormantDistortion leftChannel;
FormantDistortion rightChannel;

leftChannel.prepare(sampleRate, maxBlockSize);
rightChannel.prepare(sampleRate, maxBlockSize);

// Apply same parameters to both
leftChannel.setVowel(vowel);
rightChannel.setVowel(vowel);
// ... etc

// Process independently
leftChannel.process(leftBuffer, numSamples);
rightChannel.process(rightBuffer, numSamples);
```
