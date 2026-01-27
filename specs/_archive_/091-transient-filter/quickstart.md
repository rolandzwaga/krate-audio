# Quickstart: TransientAwareFilter

**Feature Branch**: `091-transient-filter`
**Date**: 2026-01-24

## Overview

The TransientAwareFilter is a Layer 2 DSP processor that detects transients (sudden level changes) and momentarily modulates filter cutoff and resonance in response. It creates dynamic, percussive tonal shaping that emphasizes or de-emphasizes attack portions of sounds.

## Key Differentiators

| Filter Type | Responds To | Use Case |
|-------------|-------------|----------|
| EnvelopeFilter | Overall amplitude | Auto-wah, touch-sensitive effects |
| SidechainFilter | External sidechain signal | Ducking, pumping effects |
| **TransientAwareFilter** | Transients only | Attack shaping, drum enhancement |

## Basic Usage

```cpp
#include <krate/dsp/processors/transient_filter.h>

using namespace Krate::DSP;

// Create and prepare
TransientAwareFilter filter;
filter.prepare(48000.0);

// Configure for drum attack enhancement
filter.setIdleCutoff(200.0f);        // Closed filter at rest
filter.setTransientCutoff(4000.0f);  // Open on transients
filter.setSensitivity(0.5f);          // Medium sensitivity (default, recommended starting point)
filter.setTransientAttack(1.0f);      // Fast attack (1ms)
filter.setTransientDecay(50.0f);      // Medium decay (50ms)
filter.setFilterType(TransientFilterMode::Lowpass);

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    buffer[i] = filter.process(buffer[i]);
}
```

## Common Configurations

### Drum Attack Enhancement (User Story 1)

Add "snap" or "click" to drums by opening filter on transients:

```cpp
filter.setIdleCutoff(200.0f);         // Dark at rest
filter.setTransientCutoff(4000.0f);   // Bright on hits
filter.setSensitivity(0.5f);
filter.setTransientAttack(1.0f);
filter.setTransientDecay(50.0f);
filter.setFilterType(TransientFilterMode::Lowpass);
```

### Synth Attack Softening (User Story 2)

Soften harsh synth attacks by closing filter on transients:

```cpp
filter.setIdleCutoff(8000.0f);        // Bright at rest
filter.setTransientCutoff(500.0f);    // Dark on attacks
filter.setSensitivity(0.5f);
filter.setTransientAttack(1.0f);
filter.setTransientDecay(100.0f);
filter.setFilterType(TransientFilterMode::Lowpass);
```

### Bass Resonance Ping (User Story 3)

Add "zing" to bass by boosting resonance on transients:

```cpp
filter.setIdleCutoff(1000.0f);
filter.setTransientCutoff(1000.0f);   // Same cutoff
filter.setIdleResonance(0.7f);        // Butterworth Q at rest
filter.setTransientQBoost(10.0f);     // High Q on attacks
filter.setSensitivity(0.6f);
filter.setTransientAttack(0.5f);
filter.setTransientDecay(80.0f);
filter.setFilterType(TransientFilterMode::Lowpass);
```

## Parameter Reference

### Transient Detection

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Sensitivity | 0.0-1.0 | 0.5 | Detection threshold (higher = more sensitive). Default 0.5 is recommended starting point. |
| Transient Attack | 0.1-50ms | 1.0ms | How fast filter responds to transients |
| Transient Decay | 1-1000ms | 50ms | How fast filter returns to idle |

### Filter Cutoff

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Idle Cutoff | 20Hz - Nyquist*0.45 | 200Hz | Cutoff when no transient |
| Transient Cutoff | 20Hz - Nyquist*0.45 | 4000Hz | Cutoff at peak transient |

### Filter Resonance

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Idle Resonance | 0.5-20.0 | 0.7071 | Q when no transient |
| Transient Q boost | 0.0-20.0 | 0.0 | Additional Q during transient |

### Filter Type

| Value | Description |
|-------|-------------|
| `TransientFilterMode::Lowpass` | 12 dB/oct lowpass |
| `TransientFilterMode::Bandpass` | Constant 0 dB peak bandpass |
| `TransientFilterMode::Highpass` | 12 dB/oct highpass |

## Monitoring

```cpp
// For UI metering/visualization
float cutoff = filter.getCurrentCutoff();       // Current cutoff Hz
float resonance = filter.getCurrentResonance(); // Current Q
float level = filter.getTransientLevel();       // Detection level [0,1]
```

## Real-Time Safety

All processing methods are:
- `noexcept` - No exceptions thrown
- Zero-allocation - No heap allocations during `process()`
- Lock-free - No mutex or blocking operations

Safe for audio thread use per Constitution Principle II.

## Performance

Target: < 0.5% CPU at 48kHz mono (Layer 2 budget)

The processor composes:
- 2x EnvelopeFollower (lightweight)
- 1x OnePoleSmoother (trivial)
- 1x SVF (efficient TPT topology)

## Edge Cases

| Situation | Behavior |
|-----------|----------|
| NaN/Inf input | Returns 0, resets state |
| Not prepared | Returns input unchanged |
| Sensitivity = 0 | No transients detected, stays at idle |
| Sensitivity = 1 | Very sensitive, may trigger on noise |
| Idle = Transient cutoff | No frequency sweep, only Q modulation |
| Q boost = 0 | No resonance modulation |
| Rapid transients | Re-triggers on each, may stay at transient position |

## Files

- **Header**: `dsp/include/krate/dsp/processors/transient_filter.h`
- **Tests**: `dsp/tests/unit/processors/transient_filter_test.cpp`
- **Spec**: `specs/091-transient-filter/spec.md`
