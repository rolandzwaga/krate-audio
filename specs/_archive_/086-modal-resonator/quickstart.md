# Quickstart: Modal Resonator

**Feature**: 086-modal-resonator
**Layer**: 2 (DSP Processor)
**Location**: `dsp/include/krate/dsp/processors/modal_resonator.h`

## Overview

The Modal Resonator models vibrating bodies as a sum of decaying sinusoidal modes. It provides physically accurate resonance for complex bodies like bells, bars, plates, and percussion instruments.

## Basic Usage

```cpp
#include <krate/dsp/processors/modal_resonator.h>

using namespace Krate::DSP;

// Create a modal resonator
ModalResonator resonator;

// Initialize for your sample rate
resonator.prepare(44100.0);

// Option 1: Use a material preset
resonator.setMaterial(Material::Metal);  // Bell-like sound

// Option 2: Configure modes manually
resonator.setModeFrequency(0, 440.0f);   // A4
resonator.setModeDecay(0, 2.0f);         // 2 second T60
resonator.setModeAmplitude(0, 1.0f);

// Process audio
float output = resonator.process(input);
```

## Material Presets

| Material | Character | Use Case |
|----------|-----------|----------|
| `Material::Wood` | Warm, quick HF decay | Marimba, xylophone |
| `Material::Metal` | Bright, sustained | Bells, vibraphone |
| `Material::Glass` | Bright, ringing | Glass bowls, wine glasses |
| `Material::Ceramic` | Warm/bright, medium | Clay pots, tiles |
| `Material::Nylon` | Dull, heavily damped | Damped strings |

```cpp
// Select a material
resonator.setMaterial(Material::Metal);

// Adjust size (shifts pitch)
resonator.setSize(2.0f);  // Larger = lower pitch

// Adjust damping (shortens decay)
resonator.setDamping(0.3f);  // 30% damping
```

## Excitation Methods

### 1. Strike (Percussion)

```cpp
// Strike like hitting with a mallet
resonator.strike(1.0f);  // Full velocity

// Softer strike
resonator.strike(0.3f);  // 30% velocity

// Multiple strikes accumulate energy
resonator.strike(0.5f);
// ... later while still ringing ...
resonator.strike(0.5f);  // Adds to existing resonance
```

### 2. Continuous Input (Filter/Effect)

```cpp
// Use as a resonant filter
for (int i = 0; i < numSamples; ++i) {
    output[i] = resonator.process(input[i]);
}
```

### 3. Block Processing

```cpp
// Process entire buffer in-place
resonator.processBlock(buffer, numSamples);
```

## Per-Mode Control

```cpp
// Configure 4 custom modes
for (int i = 0; i < 4; ++i) {
    resonator.setModeFrequency(i, 220.0f * (i + 1));  // Harmonics
    resonator.setModeDecay(i, 2.0f - i * 0.3f);       // Decreasing decay
    resonator.setModeAmplitude(i, 1.0f / (i + 1));    // 1/n amplitude
}
```

## Bulk Configuration

```cpp
// Configure from analysis data
ModalData modes[] = {
    { 440.0f, 2.0f, 1.0f },   // Mode 0: 440 Hz, 2s T60, full amplitude
    { 880.0f, 1.5f, 0.5f },   // Mode 1: 880 Hz, 1.5s T60, half amplitude
    { 1320.0f, 1.0f, 0.3f },  // Mode 2: 1320 Hz, 1s T60, 30% amplitude
};
resonator.setModes(modes, 3);
```

## Global Controls

### Size

```cpp
// Size inversely scales all frequencies
resonator.setSize(2.0f);   // All frequencies halved (larger object)
resonator.setSize(0.5f);   // All frequencies doubled (smaller object)
```

### Damping

```cpp
// Damping reduces all decay times
resonator.setDamping(0.0f);  // No extra damping (full decay)
resonator.setDamping(0.5f);  // 50% damping (half decay time)
resonator.setDamping(1.0f);  // Maximum damping (instant silence)
```

## Typical Applications

### 1. Pitched Percussion

```cpp
ModalResonator marimba;
marimba.prepare(44100.0);
marimba.setMaterial(Material::Wood);

// Play notes
marimba.setSize(1.0f / (noteFreq / 440.0f));  // Tune to note
marimba.strike(velocity);
```

### 2. Metallic Resonator (Effect)

```cpp
ModalResonator metalResonator;
metalResonator.prepare(44100.0);
metalResonator.setMaterial(Material::Metal);
metalResonator.setDamping(0.2f);  // Light damping

// Process guitar through resonator
for (int i = 0; i < numSamples; ++i) {
    wetOutput[i] = metalResonator.process(guitarInput[i]);
    output[i] = dryWetMix * wetOutput[i] + (1 - dryWetMix) * guitarInput[i];
}
```

### 3. Sympathetic Resonance

```cpp
ModalResonator sympatheticStrings[6];
float tunings[] = { 82.4f, 110.0f, 146.8f, 196.0f, 246.9f, 329.6f };

for (int i = 0; i < 6; ++i) {
    sympatheticStrings[i].prepare(44100.0);
    sympatheticStrings[i].setMaterial(Material::Metal);
    sympatheticStrings[i].setModeFrequency(0, tunings[i]);
}

// Feed main instrument output to all sympathetic strings
for (int i = 0; i < numSamples; ++i) {
    float resonance = 0.0f;
    for (int s = 0; s < 6; ++s) {
        resonance += sympatheticStrings[s].process(mainInput[i]);
    }
    output[i] = mainInput[i] + resonance * 0.1f;  // Subtle effect
}
```

## Performance Tips

1. **Disable unused modes**: Only enabled modes are processed
2. **Use block processing**: `processBlock()` is more cache-friendly
3. **Moderate smoothing time**: Default 20ms is good balance
4. **Profile first**: SIMD optimization available if needed

## Parameter Ranges

| Parameter | Range | Default |
|-----------|-------|---------|
| Frequency | [20 Hz, sampleRate * 0.45] | 440 Hz |
| T60 Decay | [0.001 s, 30.0 s] | 1.0 s |
| Amplitude | [0.0, 1.0] | 1.0 |
| Size | [0.1, 10.0] | 1.0 |
| Damping | [0.0, 1.0] | 0.0 |
| Strike Velocity | [0.0, 1.0] | 1.0 |

## Thread Safety

- All methods are `noexcept`
- No memory allocation in `process()`, `strike()`, or `reset()`
- Single-threaded use only (create separate instances for multi-threaded use)
