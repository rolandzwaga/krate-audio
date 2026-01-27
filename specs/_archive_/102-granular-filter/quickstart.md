# Quickstart: GranularFilter

**Date**: 2026-01-25 | **Layer**: 3 (Systems) | **Spec**: [spec.md](spec.md)

## Overview

GranularFilter extends granular synthesis with per-grain SVF filtering. Each of up to 64 simultaneous grains has its own stereo filter with randomizable cutoff frequency, creating rich spectral textures impossible with post-process filtering.

## Basic Usage

```cpp
#include <krate/dsp/systems/granular_filter.h>

using namespace Krate::DSP;

GranularFilter filter;

// 1. Prepare with sample rate
filter.prepare(48000.0);

// 2. Configure granular parameters
filter.setGrainSize(100.0f);      // 100ms grains
filter.setDensity(20.0f);         // 20 grains/sec
filter.setPosition(500.0f);       // Read from 500ms ago

// 3. Configure filter
filter.setFilterEnabled(true);
filter.setFilterType(SVFMode::Lowpass);
filter.setFilterCutoff(1000.0f);  // 1kHz base cutoff
filter.setFilterResonance(2.0f);  // Moderate resonance
filter.setCutoffRandomization(2.0f);  // +/- 2 octaves

// 4. Process audio
float outL, outR;
filter.process(inputL, inputR, outL, outR);
```

## Filter Parameters

| Parameter | Method | Range | Description |
|-----------|--------|-------|-------------|
| Enable | `setFilterEnabled(bool)` | true/false | Master enable/bypass |
| Cutoff | `setFilterCutoff(float)` | 20Hz-Nyquist*0.495 | Base center frequency |
| Resonance | `setFilterResonance(float)` | 0.5-20.0 | Q factor (0.7071=Butterworth) |
| Type | `setFilterType(SVFMode)` | LP/HP/BP/Notch | Filter mode |
| Randomization | `setCutoffRandomization(float)` | 0-4 octaves | Per-grain cutoff spread |

## Key Behaviors

### Cutoff Randomization
Each new grain gets a unique cutoff frequency:
```
Grain cutoff = baseCutoff * 2^(random * octaves)
```
Where `random` is uniformly distributed in [-1, 1].

**Example**: Base 1kHz, 2 octaves randomization
- Grain A: 1000 * 2^(-1.5) = 354 Hz
- Grain B: 1000 * 2^(0.5) = 1414 Hz
- Grain C: 1000 * 2^(1.8) = 3482 Hz

### Global vs Per-Grain Parameters

| Parameter | Scope | When Applied |
|-----------|-------|--------------|
| Cutoff (base) | Global | At grain trigger |
| Cutoff (actual) | Per-grain | Randomized at trigger |
| Resonance (Q) | Global | Immediate to all grains |
| Type (LP/HP/BP/Notch) | Global | Immediate to all grains |

### Signal Flow

```
Read from buffer -> Pitch shift -> Envelope -> FILTER -> Pan -> Sum
```

Filter processes pitch-shifted, enveloped audio BEFORE panning is applied.

## Common Patterns

### Evolving Texture
```cpp
filter.setDensity(50.0f);           // Dense cloud
filter.setGrainSize(200.0f);        // Overlapping grains
filter.setCutoffRandomization(3.0f); // Wide spectral spread
filter.setPositionSpray(0.5f);      // Position variation
```

### Resonant Sweep
```cpp
filter.setFilterResonance(8.0f);    // High Q
filter.setCutoffRandomization(0.0f); // No randomization
// Sweep baseCutoff with LFO or envelope
filter.setFilterCutoff(cutoffFromLFO);
```

### Bandpass Emphasis
```cpp
filter.setFilterType(SVFMode::Bandpass);
filter.setFilterCutoff(800.0f);     // Mid-range focus
filter.setFilterResonance(4.0f);    // Resonant peak
filter.setCutoffRandomization(1.0f); // +/- 1 octave variation
```

### Filter Bypass (Match GranularEngine)
```cpp
filter.setFilterEnabled(false);
// Output is now identical to GranularEngine with same parameters/seed
```

## Performance Notes

- 128 SVF instances (64 grains x 2 channels) use ~1.3% CPU
- Total granular+filter processing is well under 5% CPU budget
- ~10KB memory for filter state array
- All processing is real-time safe (no allocations in process())

## Deterministic Output

For reproducible results (testing, automation):
```cpp
filter.seed(12345);
filter.reset();
// Output is now deterministic given same input sequence
```

## Comparison with GranularEngine

| Feature | GranularEngine | GranularFilter |
|---------|----------------|----------------|
| Per-grain filtering | No | Yes |
| Filter cutoff randomization | No | Yes (0-4 octaves) |
| Filter types | No | LP/HP/BP/Notch |
| Signal flow | read->pitch->env->pan | read->pitch->env->**filter**->pan |
| Bypass mode equivalence | N/A | Bit-identical with seed |
| CPU overhead | Baseline | +~1% for filtering |
