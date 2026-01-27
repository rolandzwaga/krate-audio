# Quickstart: AllpassSaturator

**Feature**: 109-allpass-saturator-network | **Layer**: 2 (DSP Processors)

## Overview

AllpassSaturator creates resonant distortion by placing saturation inside allpass filter feedback loops. The processor can self-oscillate, creating pitched resonances that are excited by input audio.

## Basic Usage

```cpp
#include <krate/dsp/processors/allpass_saturator.h>

using namespace Krate::DSP;

// Create and prepare
AllpassSaturator processor;
processor.prepare(44100.0, 512);  // Sample rate, max block size

// Configure
processor.setTopology(NetworkTopology::SingleAllpass);
processor.setFrequency(440.0f);   // Resonant pitch
processor.setFeedback(0.85f);     // Resonance amount
processor.setDrive(2.0f);         // Saturation intensity

// Process
float output = processor.process(input);

// Or block processing
processor.processBlock(buffer, numSamples);
```

## Topologies

### SingleAllpass - Pitched Resonance

```cpp
processor.setTopology(NetworkTopology::SingleAllpass);
processor.setFrequency(440.0f);  // A4
processor.setFeedback(0.9f);     // Strong resonance
processor.setDrive(1.5f);        // Moderate saturation
```

Creates a single pitched resonance. Input audio excites the resonance at the specified frequency. Good for:
- Adding singing quality to drums
- Pitched distortion effects
- Resonant filtering with harmonics

### AllpassChain - Bell-like Tones

```cpp
processor.setTopology(NetworkTopology::AllpassChain);
processor.setFrequency(200.0f);  // Base frequency
processor.setFeedback(0.85f);    // Sustained resonance
processor.setDrive(2.0f);        // Rich harmonics
```

Four allpass filters at inharmonic frequency ratios (f, 1.5f, 2.33f, 3.67f). Creates:
- Metallic, bell-like textures
- Complex, inharmonic resonances
- Gamelan-style timbres

### KarplusStrong - Plucked Strings

```cpp
processor.setTopology(NetworkTopology::KarplusStrong);
processor.setFrequency(220.0f);  // A3 pitch
processor.setDecay(1.5f);        // 1.5 second decay
processor.setDrive(2.5f);        // Added harmonics
processor.setFeedback(0.95f);    // Long sustain
```

Classic plucked string synthesis with saturation. Great for:
- Guitar-like plucks
- Harpsichord sounds
- Physical modeling synthesis with warmth

### FeedbackMatrix - Drones

```cpp
processor.setTopology(NetworkTopology::FeedbackMatrix);
processor.setFrequency(100.0f);  // Low base frequency
processor.setFeedback(0.95f);    // Near self-oscillation
processor.setDrive(3.0f);        // Rich saturation
```

4x4 Householder matrix creates dense, evolving textures:
- Ambient drones
- Self-sustaining textures
- Complex harmonic interactions

## Parameter Guide

### Frequency (20Hz - Nyquist/2)

Controls the pitch of resonance:
- Low (50-200Hz): Subby, deep resonances
- Mid (200-1000Hz): Musical, tonal resonances
- High (1000-5000Hz): Bright, bell-like

### Feedback (0.0 - 0.999)

Controls resonance intensity:
- 0.0-0.3: Subtle coloration
- 0.3-0.7: Clear resonance
- 0.7-0.9: Strong resonance, approaching self-oscillation
- 0.9+: Self-oscillation (continuous sound with any input)

### Drive (0.1 - 10.0)

Controls saturation intensity:
- 0.1-0.5: Subtle warmth
- 0.5-2.0: Moderate saturation
- 2.0-5.0: Aggressive distortion
- 5.0+: Heavy saturation

### Saturation Curve

```cpp
processor.setSaturationCurve(WaveshapeType::Tanh);     // Warm, smooth
processor.setSaturationCurve(WaveshapeType::Tube);     // Even harmonics
processor.setSaturationCurve(WaveshapeType::Cubic);    // 3rd harmonic emphasis
processor.setSaturationCurve(WaveshapeType::HardClip); // Harsh, all harmonics
```

### Decay (KarplusStrong only)

```cpp
processor.setDecay(0.5f);   // Short, percussive
processor.setDecay(2.0f);   // Medium sustain
processor.setDecay(10.0f);  // Very long sustain
```

## Real-Time Considerations

- All methods are `noexcept` and allocation-free after `prepare()`
- Parameter changes are smoothed (10ms) to prevent clicks
- Topology changes reset state - may cause brief silence
- Process is O(1) per sample

## Thread Safety

- All setters can be called from any thread
- `process()` must be called from audio thread only
- Parameter changes are atomic (smoothed values)

## Common Patterns

### Triggered Resonance

```cpp
// Trigger with impulse
processor.setFeedback(0.95f);
output = processor.process(1.0f);  // Impulse

// Let it ring
for (int i = 0; i < 44100; ++i) {
    output = processor.process(0.0f);  // No input, resonance sustains
}
```

### Frequency Sweep

```cpp
// Smooth frequency changes
for (float freq = 100.0f; freq < 1000.0f; freq += 0.1f) {
    processor.setFrequency(freq);  // Smoothed internally
    output = processor.process(input);
}
```

### Parallel Processing (Stereo)

```cpp
// Use two instances for stereo
AllpassSaturator left, right;
left.prepare(sampleRate, blockSize);
right.prepare(sampleRate, blockSize);

// Different frequencies for width
left.setFrequency(440.0f);
right.setFrequency(443.0f);  // Slight detuning
```

## Performance

- CPU: < 0.5% per instance at 44.1kHz
- Latency: 0 samples
- Memory: ~10KB per instance (mostly delay buffer)

## Includes

```cpp
// Full header
#include <krate/dsp/processors/allpass_saturator.h>

// Enums only (for parameters)
#include <krate/dsp/primitives/waveshaper.h>  // WaveshapeType
```
