# Quickstart: DiodeClipper Processor

**Feature**: 060-diode-clipper | **Date**: 2026-01-14

## Overview

DiodeClipper is a Layer 2 DSP processor that models configurable diode clipping circuits. It provides four diode types, three topologies, and per-instance parameter control for creative distortion effects.

## Basic Usage

```cpp
#include <krate/dsp/processors/diode_clipper.h>

using namespace Krate::DSP;

// Create and prepare
DiodeClipper clipper;
clipper.prepare(44100.0, 512);  // Sample rate, max block size

// Configure
clipper.setDiodeType(DiodeType::Silicon);      // Default
clipper.setTopology(ClipperTopology::Symmetric);
clipper.setDrive(12.0f);  // +12 dB drive
clipper.setMix(1.0f);     // 100% wet

// Process audio
void processBlock(float* buffer, size_t numSamples) {
    clipper.process(buffer, numSamples);
}
```

## Diode Types

| Type | Character | When to Use |
|------|-----------|-------------|
| Silicon | Balanced overdrive | Classic pedal/amp overdrive |
| Germanium | Warm, vintage | Fuzz, vintage tones |
| LED | Aggressive, hard | Hard clipping, distortion |
| Schottky | Subtle, soft | Gentle warmth, mastering |

```cpp
// Vintage fuzz tone
clipper.setDiodeType(DiodeType::Germanium);

// Aggressive distortion
clipper.setDiodeType(DiodeType::LED);
```

## Topologies

| Topology | Harmonics | Sound |
|----------|-----------|-------|
| Symmetric | Odd only | Clean, guitar-like |
| Asymmetric | Even + Odd | Tube-like warmth |
| SoftHard | Even + Odd | Unique character |

```cpp
// Tube-like warmth with even harmonics
clipper.setTopology(ClipperTopology::Asymmetric);
```

## Custom Diode Parameters

Override type defaults for creative sound design:

```cpp
// Start with Silicon
clipper.setDiodeType(DiodeType::Silicon);

// Customize for unique character
clipper.setForwardVoltage(0.45f);  // Lower threshold = earlier clipping
clipper.setKneeSharpness(8.0f);    // Harder knee = more aggressive
```

## Parameter Ranges

| Parameter | Range | Default |
|-----------|-------|---------|
| Drive | -24 to +48 dB | 0 dB |
| Mix | 0.0 to 1.0 | 1.0 |
| Output Level | -24 to +24 dB | 0 dB |
| Forward Voltage | 0.05 to 5.0 V | Type default |
| Knee Sharpness | 0.5 to 20.0 | Type default |

## Anti-Aliasing

DiodeClipper has no internal oversampling. Wrap with Oversampler<> for anti-aliased output:

```cpp
#include <krate/dsp/primitives/oversampler.h>

Oversampler<2, 1> oversampler;  // 2x, mono
DiodeClipper clipper;

// Prepare both
oversampler.prepare(44100.0, 512, OversamplingQuality::Standard, OversamplingMode::ZeroLatency);
clipper.prepare(88200.0, 1024);  // 2x sample rate, 2x block size

// Process with oversampling
oversampler.process(buffer, nullptr, numSamples, [&](float* osBuffer, float*, size_t osNumSamples) {
    clipper.process(osBuffer, osNumSamples);
});
```

## Gain Staging

Use output level for gain compensation:

```cpp
// Heavy drive with makeup gain reduction
clipper.setDrive(24.0f);      // +24 dB drive
clipper.setOutputLevel(-12.0f); // -12 dB output compensation
```

## Bypass

Set mix to 0.0 for efficient bypass:

```cpp
clipper.setMix(0.0f);  // Full bypass, skips all processing
```

## Common Presets

### Clean Overdrive
```cpp
clipper.setDiodeType(DiodeType::Silicon);
clipper.setTopology(ClipperTopology::Symmetric);
clipper.setDrive(6.0f);
clipper.setMix(0.7f);
```

### Vintage Fuzz
```cpp
clipper.setDiodeType(DiodeType::Germanium);
clipper.setTopology(ClipperTopology::Asymmetric);
clipper.setDrive(18.0f);
clipper.setMix(1.0f);
```

### Hard Distortion
```cpp
clipper.setDiodeType(DiodeType::LED);
clipper.setTopology(ClipperTopology::Symmetric);
clipper.setDrive(24.0f);
clipper.setMix(1.0f);
```

### Subtle Warmth
```cpp
clipper.setDiodeType(DiodeType::Schottky);
clipper.setTopology(ClipperTopology::Asymmetric);
clipper.setDrive(3.0f);
clipper.setMix(0.5f);
```

## Thread Safety

- `prepare()` and `reset()` - Call from main thread only
- `setXxx()` methods - Can call from any thread (smoothed)
- `process()` - Call from audio thread only

## Files

- Header: `dsp/include/krate/dsp/processors/diode_clipper.h`
- Tests: `dsp/tests/unit/processors/diode_clipper_test.cpp`
- Spec: `specs/060-diode-clipper/spec.md`
