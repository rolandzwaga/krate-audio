# Quickstart: Resonator Bank

**Feature**: 083-resonator-bank | **Layer**: 2 (Processor)

## Overview

ResonatorBank is a bank of 16 tuned resonant bandpass filters for physical modeling applications. It can model marimba bars, bells, strings, or create experimental tunings.

## Include

```cpp
#include <krate/dsp/processors/resonator_bank.h>
```

## Basic Usage

### Setup and Harmonic Tuning

```cpp
using namespace Krate::DSP;

ResonatorBank bank;
bank.prepare(44100.0);

// Configure harmonic series (strings, flutes)
bank.setHarmonicSeries(440.0f, 8);  // A4 with 8 partials
```

### Processing Audio

```cpp
// Sample-by-sample
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = bank.process(input[i]);
}

// Block processing
bank.processBlock(buffer, numSamples);
```

### Percussive Trigger

```cpp
// Strike the resonators (creates pitched sound from nothing)
bank.trigger(0.8f);  // 80% velocity

// Then process to hear the decay
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = bank.process(0.0f);  // No input needed
}
```

## Tuning Modes

### Harmonic Series (Strings, Flutes)
```cpp
// f, 2f, 3f, 4f, 5f, 6f, 7f, 8f
bank.setHarmonicSeries(220.0f, 8);  // A3 with 8 partials
```

### Inharmonic Series (Bells, Bars)
```cpp
// Stretched partials: f_n = f * n * sqrt(1 + B*n^2)
bank.setInharmonicSeries(440.0f, 0.01f);  // B=0.01 for mild stretch
// Higher B values (0.05-0.1) for more bell-like character
```

### Custom Frequencies
```cpp
// Arbitrary tuning
float freqs[] = {100.0f, 220.0f, 350.0f, 480.0f, 623.0f};
bank.setCustomFrequencies(freqs, 5);
```

## Per-Resonator Control

```cpp
// Set individual resonator parameters
bank.setFrequency(0, 440.0f);   // Hz
bank.setDecay(0, 2.0f);         // RT60 in seconds
bank.setGain(0, -6.0f);         // dB
bank.setQ(0, 15.0f);            // Q factor
bank.setEnabled(0, true);       // Enable/disable

// Query parameters
float freq = bank.getFrequency(0);
bool active = bank.isEnabled(0);
```

## Global Controls

### Damping (Decay Scaling)
```cpp
bank.setDamping(0.0f);  // Full decay (no damping)
bank.setDamping(0.5f);  // 50% shorter decays
bank.setDamping(1.0f);  // Instant silence
```

### Exciter Mix (Dry/Wet)
```cpp
bank.setExciterMix(0.0f);  // Wet only (resonators only)
bank.setExciterMix(0.5f);  // 50/50 blend
bank.setExciterMix(1.0f);  // Dry only (bypass)
```

### Spectral Tilt (Brightness)
```cpp
bank.setSpectralTilt(0.0f);   // Flat response
bank.setSpectralTilt(-6.0f);  // -6dB/octave (darker)
bank.setSpectralTilt(+6.0f);  // +6dB/octave (brighter)
```

## Example: Marimba Sound

```cpp
ResonatorBank marimba;
marimba.prepare(44100.0);

// Marimba-like inharmonic tuning
marimba.setInharmonicSeries(262.0f, 0.005f);  // Middle C

// Configure for marimba character
marimba.setDamping(0.1f);         // Slight damping
marimba.setSpectralTilt(-3.0f);   // Softer highs

// Per-resonator adjustments
marimba.setDecay(0, 1.5f);   // Fundamental decays slower
marimba.setGain(0, 3.0f);    // Boost fundamental

// Process struck mallet sound
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = marimba.process(malletImpact[i]);
}
```

## Example: Triggered Percussion

```cpp
ResonatorBank bells;
bells.prepare(44100.0);

// Bell-like stretched tuning
bells.setInharmonicSeries(880.0f, 0.02f);  // High A
bells.setSpectralTilt(-2.0f);               // Natural HF rolloff

// Trigger and let ring
bells.trigger(1.0f);  // Full velocity strike

// Render the decay
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = bells.process(0.0f);
}
```

## Reset Behavior

```cpp
// Reset clears ALL state and parameters
bank.reset();

// After reset, must reconfigure tuning
bank.setHarmonicSeries(440.0f, 8);  // Required after reset
```

## Constants Reference

| Constant | Value | Description |
|----------|-------|-------------|
| `kMaxResonators` | 16 | Maximum resonators |
| `kMinResonatorFrequency` | 20 Hz | Minimum frequency |
| `kMaxResonatorFrequencyRatio` | 0.45 | Max freq / sample rate |
| `kMinResonatorQ` | 0.1 | Minimum Q |
| `kMaxResonatorQ` | 100 | Maximum Q |
| `kMinDecayTime` | 0.001s | Minimum RT60 |
| `kMaxDecayTime` | 30s | Maximum RT60 |
| `kResonatorSmoothingTimeMs` | 20ms | Parameter smoothing |

## Performance Notes

- 16 resonators at 192kHz: <1% CPU on typical workstation
- All processing is real-time safe (no allocations)
- Parameter changes are smoothed (20ms) to prevent clicks
- Trigger produces output within 1 sample
