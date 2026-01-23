# Quickstart: Stochastic Filter

**Feature**: 087-stochastic-filter
**Date**: 2026-01-23

---

## Installation

Include the header in your DSP code:

```cpp
#include <krate/dsp/processors/stochastic_filter.h>
```

---

## Basic Usage

### Minimal Example: Evolving Lowpass Filter

```cpp
#include <krate/dsp/processors/stochastic_filter.h>

using namespace Krate::DSP;

// Create and prepare
StochasticFilter filter;
filter.prepare(44100.0, 512);

// Configure for slow evolution (Walk mode)
filter.setMode(RandomMode::Walk);
filter.setBaseCutoff(1000.0f);       // Center at 1kHz
filter.setCutoffOctaveRange(2.0f);   // +/- 2 octaves (250Hz to 4kHz)
filter.setChangeRate(0.5f);          // Slow drift (0.5 Hz)
filter.setSmoothingTime(100.0f);     // Smooth transitions

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    buffer[i] = filter.process(buffer[i]);
}
```

### Glitchy Random Jumps

```cpp
StochasticFilter filter;
filter.prepare(44100.0, 512);

// Configure for rhythmic chaos
filter.setMode(RandomMode::Jump);
filter.setBaseCutoff(2000.0f);
filter.setCutoffOctaveRange(3.0f);   // Wide range
filter.setChangeRate(4.0f);          // 4 jumps per second
filter.setSmoothingTime(50.0f);      // Quick but click-free

// Enable Q randomization too
filter.setResonanceRandomEnabled(true);
filter.setBaseResonance(2.0f);
filter.setResonanceRange(0.8f);      // Wide Q variation
```

### Chaotic Modulation (Lorenz)

```cpp
StochasticFilter filter;
filter.prepare(44100.0, 512);

// Configure for chaotic behavior
filter.setMode(RandomMode::Lorenz);
filter.setBaseCutoff(1500.0f);
filter.setCutoffOctaveRange(1.5f);
filter.setChangeRate(2.0f);          // Speed of chaos

// Set seed for reproducibility
filter.setSeed(12345);
```

### Smooth Coherent Noise (Perlin)

```cpp
StochasticFilter filter;
filter.prepare(44100.0, 512);

// Configure for organic modulation
filter.setMode(RandomMode::Perlin);
filter.setBaseCutoff(800.0f);
filter.setCutoffOctaveRange(2.0f);
filter.setChangeRate(1.0f);          // Fundamental rate
filter.setSmoothingTime(30.0f);

// Perlin produces band-limited noise with 3 octaves
```

---

## Random Filter Type Switching

```cpp
StochasticFilter filter;
filter.prepare(44100.0, 512);

// Enable type randomization
filter.setTypeRandomEnabled(true);

// Select which types can be chosen
filter.setEnabledFilterTypes(
    FilterTypeMask::Lowpass |
    FilterTypeMask::Highpass |
    FilterTypeMask::Bandpass
);

// Set transition parameters
filter.setSmoothingTime(100.0f);  // Crossfade duration
filter.setChangeRate(1.0f);       // Type change rate

// Type transitions use parallel crossfade (click-free)
```

---

## Stereo Processing

StochasticFilter uses linked modulation for stereo (same random values for L/R).
Create one filter and process both channels with same instance:

```cpp
StochasticFilter filter;
filter.prepare(44100.0, 512);
filter.setMode(RandomMode::Walk);

// Process stereo buffer (interleaved)
for (size_t i = 0; i < numSamples; ++i) {
    // Same filter for both channels (linked modulation)
    bufferL[i] = filter.process(bufferL[i]);
    bufferR[i] = filter.process(bufferR[i]);
}
```

For independent stereo modulation, create two filter instances with different seeds.

---

## Reproducible Behavior

Use seeds for deterministic random sequences:

```cpp
StochasticFilter filter;
filter.prepare(44100.0, 512);
filter.setSeed(42);  // Any non-zero seed

// Process some audio...
for (size_t i = 0; i < 1000; ++i) {
    output1[i] = filter.process(input[i]);
}

// Reset to same seed
filter.reset();  // Restores saved seed

// Process again - identical output
for (size_t i = 0; i < 1000; ++i) {
    output2[i] = filter.process(input[i]);
}

// output1 == output2 (bit-identical)
```

---

## Block Processing

For efficiency, use block processing:

```cpp
StochasticFilter filter;
filter.prepare(44100.0, 512);

// Block processing (more efficient)
filter.processBlock(buffer, numSamples);
```

---

## Parameter Summary

| Method | Range | Default | Description |
|--------|-------|---------|-------------|
| `setMode(mode)` | Walk/Jump/Lorenz/Perlin | Walk | Random algorithm |
| `setBaseCutoff(hz)` | 1-22050 | 1000 | Center frequency |
| `setBaseResonance(q)` | 0.1-30 | 0.707 | Center Q |
| `setBaseFilterType(type)` | SVFMode enum | Lowpass | Default type |
| `setCutoffOctaveRange(oct)` | 0-8 | 2 | Modulation range |
| `setResonanceRange(range)` | 0-1 | 0.5 | Q modulation range |
| `setChangeRate(hz)` | 0.01-100 | 1 | Modulation speed |
| `setSmoothingTime(ms)` | 0-1000 | 50 | Transition time |
| `setSeed(seed)` | 1-UINT32_MAX | 1 | Random seed |
| `setCutoffRandomEnabled(b)` | bool | true | Enable cutoff mod |
| `setResonanceRandomEnabled(b)` | bool | false | Enable Q mod |
| `setTypeRandomEnabled(b)` | bool | false | Enable type mod |
| `setEnabledFilterTypes(mask)` | bitmask | 0x07 | Types for random |

---

## Edge Cases

### Zero Change Rate
```cpp
filter.setChangeRate(0.0f);  // Clamped to 0.01 Hz
// Filter parameters remain nearly static
```

### Zero Octave Range
```cpp
filter.setCutoffOctaveRange(0.0f);
// Cutoff stays at base frequency (no modulation)
```

### Zero Smoothing in Jump Mode
```cpp
filter.setMode(RandomMode::Jump);
filter.setSmoothingTime(0.0f);  // May cause clicks!
// Consider using minimum 10ms for click-free operation
```

---

## Performance Notes

- Control-rate updates every 32 samples (~1400 Hz at 44.1kHz)
- Linked stereo modulation (single calculation for both channels)
- Type transitions use parallel filters (2x filter CPU during transition)
- Target: < 0.5% CPU per instance at 44.1kHz stereo
