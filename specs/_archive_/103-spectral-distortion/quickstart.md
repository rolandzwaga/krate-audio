# Quickstart: Spectral Distortion Processor

**Feature**: 103-spectral-distortion | **Layer**: 2 (Processors)

## Overview

SpectralDistortion applies waveshaping distortion to individual frequency bins in the spectral domain, creating effects impossible with time-domain processing. Four modes provide different creative options:

- **PerBinSaturate**: Natural spectral interaction, phase may evolve
- **MagnitudeOnly**: Surgical control, exact phase preservation
- **BinSelective**: Different drive per frequency band
- **SpectralBitcrush**: Lo-fi magnitude quantization

## Basic Usage

```cpp
#include <krate/dsp/processors/spectral_distortion.h>

using namespace Krate::DSP;

// Create processor
SpectralDistortion distortion;

// Initialize (call once, not real-time safe)
distortion.prepare(44100.0, 2048);  // sampleRate, fftSize

// Configure
distortion.setMode(SpectralDistortionMode::PerBinSaturate);
distortion.setDrive(2.0f);
distortion.setSaturationCurve(WaveshapeType::Tanh);

// Process (real-time safe)
distortion.processBlock(inputBuffer, outputBuffer, numSamples);
```

## Mode Examples

### PerBinSaturate - Spectral Warmth

```cpp
distortion.setMode(SpectralDistortionMode::PerBinSaturate);
distortion.setDrive(3.0f);
distortion.setSaturationCurve(WaveshapeType::Tube);  // Even harmonics
```

### MagnitudeOnly - Surgical Saturation

```cpp
distortion.setMode(SpectralDistortionMode::MagnitudeOnly);
distortion.setDrive(2.0f);
distortion.setSaturationCurve(WaveshapeType::Tanh);
// Phase preserved exactly - no smearing
```

### BinSelective - Frequency-Targeted Distortion

```cpp
distortion.setMode(SpectralDistortionMode::BinSelective);

// Heavy saturation on bass
distortion.setLowBand(300.0f, 4.0f);    // 0-300Hz, drive=4

// Moderate on mids
distortion.setMidBand(300.0f, 3000.0f, 2.0f);  // 300-3000Hz, drive=2

// Clean highs
distortion.setHighBand(3000.0f, 1.0f);  // 3000Hz+, drive=1

// Optional: process gaps with global drive
distortion.setGapBehavior(GapBehavior::UseGlobalDrive);
distortion.setDrive(1.5f);
```

### SpectralBitcrush - Lo-Fi Spectral

```cpp
distortion.setMode(SpectralDistortionMode::SpectralBitcrush);
distortion.setMagnitudeBits(4.0f);  // 16 quantization levels
// Phase preserved - cleaner than time-domain bitcrush
```

## Advanced Options

### DC/Nyquist Processing

```cpp
// By default, DC (bin 0) and Nyquist (bin N/2) are excluded
// Enable for full-spectrum processing:
distortion.setProcessDCNyquist(true);

// WARNING: Asymmetric curves with DC processing can add DC offset
```

### Drive Bypass

```cpp
// drive=0 bypasses processing entirely (no waveshaper computation)
distortion.setDrive(0.0f);  // Efficient bypass
```

## Latency

```cpp
// Latency equals FFT size
size_t latencySamples = distortion.latency();  // e.g., 2048 samples

// Report to host for PDC (Plugin Delay Compensation)
```

## Stereo Processing

```cpp
// Create two instances for stereo
SpectralDistortion distortionL, distortionR;

distortionL.prepare(sampleRate, fftSize);
distortionR.prepare(sampleRate, fftSize);

// Configure identically (or differently for stereo effects)
auto configureBoth = [&](auto& d) {
    d.setMode(SpectralDistortionMode::PerBinSaturate);
    d.setDrive(2.0f);
    d.setSaturationCurve(WaveshapeType::Tanh);
};

configureBoth(distortionL);
configureBoth(distortionR);

// Process channels independently
distortionL.processBlock(inputL, outputL, numSamples);
distortionR.processBlock(inputR, outputR, numSamples);
```

## Available Saturation Curves

| Curve | Character |
|-------|-----------|
| Tanh | Warm, smooth |
| Atan | Slightly brighter |
| Cubic | 3rd harmonic emphasis |
| Quintic | Smoother than cubic |
| ReciprocalSqrt | Fast tanh alternative |
| Erf | Tape-like nulls |
| HardClip | Harsh, all harmonics |
| Diode | Subtle even harmonics |
| Tube | Warm even harmonics |

## Performance Notes

- CPU budget: < 0.5% at 44.1kHz (2048 FFT)
- No allocations during processBlock()
- Smaller FFT = lower latency, lower frequency resolution
- Larger FFT = higher latency, better frequency resolution

## Common Patterns

### Reset on Playback Start

```cpp
void onPlaybackStart() {
    distortion.reset();  // Clear buffers, prevent tail from previous session
}
```

### Dynamic Parameter Changes

```cpp
// All setters are real-time safe
// Parameters take effect on next spectral frame (every hopSize samples)
distortion.setDrive(newDrive);
distortion.setSaturationCurve(newCurve);
```

### FFT Size Selection

| FFT Size | Latency @ 44.1kHz | Frequency Resolution |
|----------|-------------------|---------------------|
| 256 | 5.8ms | 172 Hz/bin |
| 512 | 11.6ms | 86 Hz/bin |
| 1024 | 23.2ms | 43 Hz/bin |
| 2048 | 46.4ms | 21.5 Hz/bin |
| 4096 | 92.9ms | 10.8 Hz/bin |
| 8192 | 185.8ms | 5.4 Hz/bin |

## Files

| Artifact | Path |
|----------|------|
| Header | `dsp/include/krate/dsp/processors/spectral_distortion.h` |
| Tests | `dsp/tests/unit/processors/spectral_distortion_test.cpp` |
| Spec | `specs/103-spectral-distortion/spec.md` |
