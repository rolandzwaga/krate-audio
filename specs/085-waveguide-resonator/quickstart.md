# Quickstart: WaveguideResonator

**Feature Branch**: `085-waveguide-resonator`
**Layer**: 2 (Processors)
**Header**: `<krate/dsp/processors/waveguide_resonator.h>`

---

## Overview

WaveguideResonator implements a bidirectional digital waveguide for creating flute-like and pipe-like resonances. It models acoustic tube behavior with configurable end reflections, frequency-dependent loss, and dispersion for inharmonicity.

---

## Basic Usage

```cpp
#include <krate/dsp/processors/waveguide_resonator.h>

Krate::DSP::WaveguideResonator waveguide;

// Initialize for processing
waveguide.prepare(44100.0);  // Sample rate in Hz

// Configure resonance
waveguide.setFrequency(440.0f);     // A4 pitch
waveguide.setEndReflection(-1.0f, -1.0f);  // Both ends open (flute-like)
waveguide.setLoss(0.1f);            // Light damping
waveguide.setDispersion(0.0f);      // Pure harmonics
waveguide.setExcitationPoint(0.5f); // Excite at center

// Process audio (excite with noise, impulse, or audio signal)
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = waveguide.process(input[i]);
}
```

---

## Pipe Configurations

### Open-Open (Flute-like)

```cpp
waveguide.setEndReflection(-1.0f, -1.0f);
waveguide.setFrequency(440.0f);  // Fundamental at 440 Hz
// Produces all harmonics (1f, 2f, 3f, 4f, ...)
```

### Closed-Closed (Organ Pipe)

```cpp
waveguide.setEndReflection(+1.0f, +1.0f);
waveguide.setFrequency(440.0f);  // Fundamental at 440 Hz
// Produces all harmonics (1f, 2f, 3f, 4f, ...)
```

### Open-Closed (Clarinet-like)

```cpp
waveguide.setEndReflection(-1.0f, +1.0f);
waveguide.setFrequency(440.0f);  // Fundamental at 220 Hz (half!)
// Produces ODD harmonics only (1f, 3f, 5f, 7f, ...)
// Note: Set frequency to 2x desired fundamental for open-closed
```

### Partial Reflections (Damped)

```cpp
waveguide.setEndReflection(-0.7f, -0.7f);
// Faster decay, less resonance
```

---

## Loss and Brightness Control

```cpp
// Bright, long decay
waveguide.setLoss(0.05f);

// Dark, shorter decay
waveguide.setLoss(0.5f);

// Very short decay
waveguide.setLoss(0.9f);
```

Loss affects high frequencies more than low frequencies, simulating air absorption in real pipes.

---

## Adding Inharmonicity (Bell-like)

```cpp
// Harmonic (pipe-like)
waveguide.setDispersion(0.0f);

// Slightly inharmonic
waveguide.setDispersion(0.3f);

// Very inharmonic (bell-like)
waveguide.setDispersion(0.7f);
```

Dispersion shifts upper partials away from exact integer harmonics.

---

## Excitation Point Effects

```cpp
// Center excitation - attenuates even harmonics
waveguide.setExcitationPoint(0.5f);

// Near left end - emphasizes all harmonics
waveguide.setExcitationPoint(0.1f);

// Near right end - emphasizes all harmonics
waveguide.setExcitationPoint(0.9f);
```

Excitation point simulates where you "blow" or "strike" the pipe.

---

## Block Processing

```cpp
// In-place processing
waveguide.processBlock(buffer, numSamples);

// Separate input/output
waveguide.processBlock(inputBuffer, outputBuffer, numSamples);
```

---

## Impulse Excitation Example

```cpp
// Generate an impulse response
waveguide.prepare(44100.0);
waveguide.setFrequency(220.0f);  // A3
waveguide.setEndReflection(-1.0f, -1.0f);
waveguide.setLoss(0.1f);

// Send single impulse
float impulse[1024] = {0};
impulse[0] = 1.0f;

float output[1024];
waveguide.processBlock(impulse, output, 1024);
// output now contains the resonant response
```

---

## Continuous Noise Excitation

```cpp
// Create sustained resonance by feeding noise
Krate::DSP::Xorshift32 rng{12345};

for (size_t i = 0; i < numSamples; ++i) {
    float noise = rng.nextFloat() * 0.1f;  // Low-level noise
    output[i] = waveguide.process(noise);
}
```

---

## Parameter Automation

Parameters can be changed in real-time without clicks:

```cpp
// Frequency, loss, and dispersion are smoothed automatically
waveguide.setFrequency(newFrequency);  // Glides smoothly
waveguide.setLoss(newLoss);            // Fades smoothly
waveguide.setDispersion(newDispersion); // Morphs smoothly

// End reflections and excitation point change instantly (no smoothing needed)
waveguide.setEndReflection(newLeft, newRight);
waveguide.setExcitationPoint(newPosition);
```

---

## Resetting State

```cpp
// Clear all resonance to silence
waveguide.reset();
```

---

## API Reference

| Method | Description |
|--------|-------------|
| `prepare(sampleRate)` | Initialize for processing |
| `reset()` | Clear state to silence |
| `setFrequency(hz)` | Set resonant frequency (20 Hz - Nyquist/2) |
| `setEndReflection(left, right)` | Set reflection coefficients (-1 to +1) |
| `setLoss(amount)` | Set frequency-dependent loss (0 to ~1) |
| `setDispersion(amount)` | Set inharmonicity (0 to 1) |
| `setExcitationPoint(position)` | Set input/output position (0 to 1) |
| `process(input)` | Process one sample |
| `processBlock(buffer, numSamples)` | Process in-place |
| `processBlock(in, out, numSamples)` | Process with separate buffers |
