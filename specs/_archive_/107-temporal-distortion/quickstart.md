# Quickstart: Temporal Distortion Processor

**Feature Branch**: `107-temporal-distortion`
**Date**: 2026-01-26

## Overview

TemporalDistortion is a Layer 2 DSP processor that creates dynamics-aware distortion by modulating waveshaper drive based on signal history.

---

## Basic Usage

```cpp
#include <krate/dsp/processors/temporal_distortion.h>

using namespace Krate::DSP;

// Create processor
TemporalDistortion distortion;

// Initialize (required before processing)
distortion.prepare(44100.0, 512);

// Configure for guitar-style responsive distortion
distortion.setMode(TemporalMode::EnvelopeFollow);
distortion.setBaseDrive(2.0f);           // Moderate saturation at reference level
distortion.setDriveModulation(0.5f);     // 50% modulation depth
distortion.setAttackTime(10.0f);         // 10ms attack
distortion.setReleaseTime(100.0f);       // 100ms release
distortion.setWaveshapeType(WaveshapeType::Tanh);

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = distortion.processSample(input[i]);
}

// Or block processing (identical output)
distortion.processBlock(buffer, numSamples);
```

---

## Mode-Specific Examples

### EnvelopeFollow: Guitar Amp Response

Louder playing = more distortion (like a real tube amp).

```cpp
distortion.setMode(TemporalMode::EnvelopeFollow);
distortion.setBaseDrive(2.0f);
distortion.setDriveModulation(0.7f);    // Strong dynamics response
distortion.setAttackTime(5.0f);         // Fast attack for picking response
distortion.setReleaseTime(150.0f);      // Moderate release
```

### InverseEnvelope: Expansion Distortion

Quieter signals = more distortion (brings up low-level detail).

```cpp
distortion.setMode(TemporalMode::InverseEnvelope);
distortion.setBaseDrive(1.5f);
distortion.setDriveModulation(0.6f);
distortion.setAttackTime(20.0f);
distortion.setReleaseTime(200.0f);
// Note: Drive capped at 20.0 (2x max base drive) to prevent instability on silence
```

### Derivative: Transient Enhancement

Transients = more distortion, sustain = cleaner.

```cpp
distortion.setMode(TemporalMode::Derivative);
distortion.setBaseDrive(1.5f);
distortion.setDriveModulation(0.8f);    // Strong transient emphasis
distortion.setAttackTime(5.0f);         // Fast envelope for transient tracking
distortion.setReleaseTime(50.0f);       // Quick release
distortion.setWaveshapeType(WaveshapeType::HardClip);  // Aggressive character
```

### Hysteresis: Analog Memory Effect

Processing depends on recent signal trajectory.

```cpp
distortion.setMode(TemporalMode::Hysteresis);
distortion.setBaseDrive(2.0f);
distortion.setDriveModulation(0.5f);
distortion.setHysteresisDepth(0.7f);    // Strong path-dependent behavior
distortion.setHysteresisDecay(50.0f);   // 50ms memory decay
distortion.setWaveshapeType(WaveshapeType::Tube);  // Warm character
```

---

## Parameter Reference

### Core Parameters

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Mode | 0-3 | EnvelopeFollow | Temporal modulation algorithm |
| Base Drive | 0.0-10.0 | 1.0 | Drive at reference level |
| Drive Modulation | 0.0-1.0 | 0.5 | How much envelope affects drive |
| Attack Time | 0.1-500 ms | 10 ms | Envelope attack speed |
| Release Time | 1-5000 ms | 100 ms | Envelope release speed |
| Waveshape Type | WaveshapeType | Tanh | Saturation curve |

### Hysteresis Parameters

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Hysteresis Depth | 0.0-1.0 | 0.5 | How much history affects drive |
| Hysteresis Decay | 1-500 ms | 50 ms | Memory fade time |

---

## Processing Modes Explained

### EnvelopeFollow (default)
- **Behavior**: Drive scales with input amplitude
- **Reference**: -12 dBFS RMS = base drive unchanged
- **Above reference**: Drive increases (more saturation)
- **Below reference**: Drive decreases (cleaner)
- **Use for**: Guitar amps, dynamics-responsive distortion

### InverseEnvelope
- **Behavior**: Drive scales inversely with amplitude
- **Loud signals**: Less distortion
- **Quiet signals**: More distortion (capped at 20.0, which is 2x max base drive, to prevent runaway)
- **Use for**: Expansion effects, bringing up quiet details

### Derivative
- **Behavior**: Drive scales with rate of amplitude change
- **Transients**: Maximum drive modulation
- **Sustained signals**: Minimal modulation
- **Use for**: Drums, percussive sounds, transient enhancement

### Hysteresis
- **Behavior**: Drive depends on recent signal trajectory
- **Rising signals**: Different processing than falling
- **Silent input**: Memory decays to neutral
- **Use for**: Analog-like behavior, path-dependent character

---

## Edge Cases

### Zero Drive Modulation
When `driveModulation == 0.0`, all modes behave identically as static waveshaping at `baseDrive`.

```cpp
distortion.setDriveModulation(0.0f);  // Static distortion
```

### Zero Base Drive
Returns silence regardless of input.

```cpp
distortion.setBaseDrive(0.0f);  // Output will be 0
```

### Mode Switching
Mode can be changed without audio artifacts due to internal drive smoothing.

```cpp
// Safe to call during processing
distortion.setMode(TemporalMode::Derivative);
```

### Reset for New Audio
Call `reset()` when starting a new audio stream or after discontinuity.

```cpp
distortion.reset();  // Clears envelope and hysteresis state
```

---

## Integration with Other Components

### With Oversampler (recommended for aliasing)
```cpp
#include <krate/dsp/primitives/oversampler.h>

Oversampler<2> oversampler;  // 2x oversampling
TemporalDistortion distortion;

oversampler.prepare(44100.0, maxBlockSize);
distortion.prepare(88200.0, maxBlockSize * 2);  // 2x sample rate

// Process
oversampler.processUp(input, upsampled, numSamples);
distortion.processBlock(upsampled, numSamples * 2);
oversampler.processDown(upsampled, output, numSamples);
```

### With DCBlocker (for asymmetric saturation)
```cpp
#include <krate/dsp/primitives/dc_blocker.h>

TemporalDistortion distortion;
DCBlocker dcBlocker;

distortion.prepare(44100.0, 512);
distortion.setWaveshapeType(WaveshapeType::Diode);  // Asymmetric

dcBlocker.prepare(44100.0, 10.0f);

// Process
distortion.processBlock(buffer, numSamples);
dcBlocker.processBlock(buffer, numSamples);  // Remove DC offset
```

---

## Performance Notes

- **CPU**: < 0.5% at 44.1kHz stereo (Layer 2 budget)
- **Latency**: 0 samples (no lookahead)
- **Memory**: Fixed ~200 bytes per instance
- **Real-time safe**: All processing methods are noexcept with no allocation

---

## File Locations

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/processors/temporal_distortion.h` | Header (implementation) |
| `dsp/tests/unit/processors/temporal_distortion_test.cpp` | Unit tests |
| `specs/107-temporal-distortion/spec.md` | Full specification |
