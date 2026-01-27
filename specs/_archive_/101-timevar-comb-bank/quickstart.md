# Quickstart: TimeVaryingCombBank

**Branch**: `101-timevar-comb-bank` | **Date**: 2026-01-25

---

## Overview

TimeVaryingCombBank is a Layer 3 DSP system component that creates evolving metallic and resonant textures using multiple comb filters with modulated delay times. It supports up to 8 comb filters with independent modulation, automatic harmonic/inharmonic tuning, and stereo panning distribution.

---

## Include Path

```cpp
#include <krate/dsp/systems/timevar_comb_bank.h>
```

---

## Basic Usage

### 1. Minimal Example - Metallic Resonance

```cpp
#include <krate/dsp/systems/timevar_comb_bank.h>

using namespace Krate::DSP;

// Create and prepare
TimeVaryingCombBank combBank;
combBank.prepare(44100.0, 50.0f);  // 44.1kHz, 50ms max delay

// Configure for harmonic resonance at 100 Hz
combBank.setNumCombs(4);
combBank.setTuningMode(Tuning::Harmonic);
combBank.setFundamental(100.0f);

// Add slow modulation for movement
combBank.setModRate(0.5f);     // 0.5 Hz
combBank.setModDepth(10.0f);   // 10% depth

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = combBank.process(input[i]);
}
```

### 2. Stereo Processing with Pan Distribution

```cpp
TimeVaryingCombBank combBank;
combBank.prepare(48000.0, 50.0f);

// 6 combs spread across stereo field
combBank.setNumCombs(6);
combBank.setFundamental(150.0f);
combBank.setStereoSpread(1.0f);  // Full L-R distribution

// Process stereo
for (size_t i = 0; i < numSamples; ++i) {
    float left = inputL[i];
    float right = inputR[i];
    combBank.processStereo(left, right);
    outputL[i] = left;
    outputR[i] = right;
}
```

### 3. Bell-Like Inharmonic Texture

```cpp
TimeVaryingCombBank combBank;
combBank.prepare(44100.0);

// Inharmonic tuning for metallic, bell-like quality
combBank.setNumCombs(8);
combBank.setTuningMode(Tuning::Inharmonic);
combBank.setFundamental(200.0f);
combBank.setSpread(0.8f);  // High inharmonicity

// Per-comb feedback variation for decay differences
combBank.setCombFeedback(0, 0.95f);  // Long decay
combBank.setCombFeedback(1, 0.90f);
combBank.setCombFeedback(2, 0.85f);
combBank.setCombFeedback(3, 0.80f);
combBank.setCombFeedback(4, 0.75f);
combBank.setCombFeedback(5, 0.70f);
combBank.setCombFeedback(6, 0.65f);
combBank.setCombFeedback(7, 0.60f);  // Short decay
```

### 4. Evolving Texture with Random Drift

```cpp
TimeVaryingCombBank combBank;
combBank.prepare(44100.0);

combBank.setNumCombs(4);
combBank.setFundamental(80.0f);

// Slow LFO with random drift for organic movement
combBank.setModRate(0.2f);         // Very slow
combBank.setModDepth(15.0f);       // 15% depth
combBank.setRandomModulation(0.3f); // 30% random drift
combBank.setModPhaseSpread(45.0f); // 45 degrees between combs
```

---

## Common Configurations

### Static Comb Bank (No Modulation)

```cpp
combBank.setModDepth(0.0f);        // Disable modulation
combBank.setRandomModulation(0.0f);
// Output identical to static comb filter bank
```

### Custom Per-Comb Delays

```cpp
combBank.setTuningMode(Tuning::Custom);  // Or just call setCombDelay()
combBank.setCombDelay(0, 5.0f);   // 5ms
combBank.setCombDelay(1, 7.5f);   // 7.5ms
combBank.setCombDelay(2, 11.0f);  // 11ms (prime ratio)
combBank.setCombDelay(3, 13.5f);  // 13.5ms
```

### Dark Resonance (High Damping)

```cpp
for (size_t i = 0; i < 8; ++i) {
    combBank.setCombDamping(i, 0.7f);  // High HF rolloff
}
```

### Bright Resonance (No Damping)

```cpp
for (size_t i = 0; i < 8; ++i) {
    combBank.setCombDamping(i, 0.0f);  // No HF rolloff
}
```

---

## Parameter Reference

| Parameter | Method | Range | Default | Description |
|-----------|--------|-------|---------|-------------|
| Comb count | `setNumCombs()` | 1-8 | 4 | Active combs |
| Tuning mode | `setTuningMode()` | Harmonic/Inharmonic/Custom | Harmonic | Auto-tuning |
| Fundamental | `setFundamental()` | 20-1000 Hz | 100 Hz | Base frequency |
| Spread | `setSpread()` | 0-1 | 0 | Inharmonic factor |
| Mod rate | `setModRate()` | 0.01-20 Hz | 1 Hz | LFO frequency |
| Mod depth | `setModDepth()` | 0-100% | 0% | Modulation amount |
| Phase spread | `setModPhaseSpread()` | 0-360 deg | 0 | LFO phase offset |
| Random mod | `setRandomModulation()` | 0-1 | 0 | Random drift |
| Stereo spread | `setStereoSpread()` | 0-1 | 0 | Pan distribution |
| Per-comb delay | `setCombDelay()` | 1-50 ms | varies | Manual delay |
| Per-comb feedback | `setCombFeedback()` | -0.9999-0.9999 | 0.5 | Resonance |
| Per-comb damping | `setCombDamping()` | 0-1 | 0 | HF rolloff |
| Per-comb gain | `setCombGain()` | any dB | 0 dB | Output level |

---

## Thread Safety

- `prepare()`: Call from main thread before audio processing
- `reset()`: Call from main thread between audio sessions
- `process()`/`processStereo()`: Real-time safe, call from audio thread
- Parameter setters: Safe to call from any thread (smoothed transitions)

---

## Performance Notes

- Processing cost scales with active comb count
- 8 combs at 44.1kHz: <1% CPU single core (per SC-003)
- Modulation and random drift add minimal overhead
- Stereo processing is ~2x mono cost

---

## Common Mistakes

1. **Calling process() before prepare()**: Returns 0, no audio output
2. **Forgetting to reset() on transport stop**: May have stale resonances
3. **Setting modulation depth too high**: >50% can cause pitch instability
4. **Expecting immediate parameter changes**: All parameters are smoothed

---

## Related Components

- `FeedbackComb` (Layer 1): Individual comb filter used internally
- `FilterFeedbackMatrix` (Layer 3): Multi-filter system with cross-feedback
- `FeedbackNetwork` (Layer 3): Delay-based feedback network
