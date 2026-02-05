# Quickstart: Noise Oscillator Primitive

**Feature**: 023-noise-oscillator
**Date**: 2026-02-05
**Updated**: 2026-02-05 (Added Grey noise)

## Overview

The `NoiseOscillator` is a lightweight Layer 1 primitive providing six noise colors for oscillator-level composition. Unlike the Layer 2 `NoiseGenerator` (which provides effects-oriented noise types like tape hiss and vinyl crackle), this primitive focuses on pure spectral noise colors suitable for synthesis applications.

## Installation

Include the header in your DSP code:

```cpp
#include <krate/dsp/primitives/noise_oscillator.h>
```

## Basic Usage

### Simple White Noise Generation

```cpp
#include <krate/dsp/primitives/noise_oscillator.h>

using namespace Krate::DSP;

// Create and initialize
NoiseOscillator noise;
noise.prepare(44100.0);  // Sample rate in Hz

// Generate samples
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = noise.process();
}
```

### Block Processing (More Efficient)

```cpp
NoiseOscillator noise;
noise.prepare(44100.0);

// Generate entire block at once
noise.processBlock(outputBuffer, 512);
```

### Colored Noise

```cpp
NoiseOscillator noise;
noise.prepare(44100.0);

// Set noise color before processing
noise.setColor(NoiseColor::Pink);   // -3 dB/octave
// noise.setColor(NoiseColor::Brown);  // -6 dB/octave
// noise.setColor(NoiseColor::Blue);   // +3 dB/octave
// noise.setColor(NoiseColor::Violet); // +6 dB/octave
// noise.setColor(NoiseColor::Grey);   // Perceptually flat (inverse A-weighting)

for (size_t i = 0; i < numSamples; ++i) {
    output[i] = noise.process();
}
```

## Deterministic Sequences

### Reproducible Output

```cpp
NoiseOscillator noise;
noise.prepare(44100.0);
noise.setSeed(12345);  // Fixed seed for reproducibility

// Generate 1000 samples
std::array<float, 1000> buffer1;
for (size_t i = 0; i < 1000; ++i) {
    buffer1[i] = noise.process();
}

// Reset and regenerate - identical output
noise.reset();
std::array<float, 1000> buffer2;
for (size_t i = 0; i < 1000; ++i) {
    buffer2[i] = noise.process();
}

// buffer1 == buffer2 (identical sequences)
```

### Different Seeds for Uncorrelated Sequences

```cpp
NoiseOscillator noiseL, noiseR;
noiseL.prepare(44100.0);
noiseR.prepare(44100.0);

noiseL.setSeed(1001);  // Different seeds
noiseR.setSeed(2002);  // for stereo decorrelation

for (size_t i = 0; i < numSamples; ++i) {
    leftOutput[i] = noiseL.process();
    rightOutput[i] = noiseR.process();
}
```

## Common Use Cases

### Karplus-Strong Excitation

```cpp
NoiseOscillator excitation;
excitation.prepare(sampleRate);
excitation.setColor(NoiseColor::White);

// Fill delay line with noise burst
for (size_t i = 0; i < delayLength; ++i) {
    delayLine.push(excitation.process() * envelope[i]);
}
```

### LFO Modulation Source

```cpp
NoiseOscillator noiseLFO;
noiseLFO.prepare(sampleRate);
noiseLFO.setColor(NoiseColor::Pink);  // Smoother random modulation

// Use as modulation source (typically with smoothing)
float modulation = noiseLFO.process() * modDepth;
delayTime = baseTime + modulation;
```

### Granular Synthesis Excitation

```cpp
NoiseOscillator grainNoise;
grainNoise.prepare(sampleRate);
grainNoise.setColor(NoiseColor::White);

// Generate grain excitation
void fillGrainBuffer(float* buffer, size_t grainSize) {
    grainNoise.processBlock(buffer, grainSize);
    // Apply grain envelope
    for (size_t i = 0; i < grainSize; ++i) {
        buffer[i] *= grainEnvelope[i];
    }
}
```

### Subtractive Synthesis

```cpp
NoiseOscillator noiseSource;
noiseSource.prepare(sampleRate);
noiseSource.setColor(NoiseColor::Pink);  // Start with pink

// Mix noise with oscillator
for (size_t i = 0; i < numSamples; ++i) {
    float osc = oscillator.process();
    float noise = noiseSource.process() * noiseLevel;
    output[i] = osc + noise;
}
```

## Color Selection Guide

| Color | Spectral Slope | Character | Use Case |
|-------|---------------|-----------|----------|
| White | 0 dB/octave | Bright, harsh | Excitation, dithering |
| Pink | -3 dB/octave | Balanced, natural | Ambient, modulation |
| Brown | -6 dB/octave | Rumbling, bass-heavy | Bass textures, wind |
| Blue | +3 dB/octave | Hissy, bright | Brightness, dithering |
| Violet | +6 dB/octave | Very bright | High-frequency emphasis |
| Grey | Perceptually flat | Balanced loudness | Audio testing, calibration |

### Grey Noise Details

Grey noise applies inverse A-weighting to white noise, resulting in perceptually flat loudness across all frequencies. This compensates for human hearing sensitivity:

- **Low frequencies boosted**: Compensates for reduced hearing sensitivity below 200Hz
- **Mid frequencies neutral**: Reference point around 1kHz
- **High frequencies boosted**: Compensates for reduced sensitivity above 6kHz

Grey noise is ideal for:
- Audio equipment testing
- Room acoustics calibration
- Reference noise for mixing
- Psychoacoustic research

## Real-Time Considerations

### Thread Safety

`NoiseOscillator` is **not thread-safe**. Use separate instances for different threads.

### Real-Time Safety

The following methods are **real-time safe** (no allocation, no blocking):
- `process()`
- `processBlock()`
- `setColor()`
- `setSeed()`
- `reset()`

The following method is **NOT real-time safe**:
- `prepare()` - call during initialization only

### CPU Budget

- Per-sample cost: ~10-50 nanoseconds depending on color
- Pink/Brown/Blue/Violet slightly more expensive than White
- Well within Layer 1 budget (<0.1% CPU)

## Integration with Existing Code

### With NoiseGenerator (Layer 2)

```cpp
// Use NoiseOscillator for synthesis/modulation
NoiseOscillator synthNoise;
synthNoise.prepare(sampleRate);
synthNoise.setColor(NoiseColor::Pink);

// Use NoiseGenerator for effects (tape hiss, vinyl crackle)
NoiseGenerator effectsNoise;
effectsNoise.prepare(sampleRate, blockSize);
effectsNoise.setNoiseEnabled(NoiseType::TapeHiss, true);

// They are independent - use both as needed
```

### With LFO

```cpp
// Noise oscillator as alternative to LFO S&H
NoiseOscillator randomLFO;
randomLFO.prepare(sampleRate);
randomLFO.setColor(NoiseColor::Pink);  // Smoother

// Or use actual LFO with SampleHold waveform
LFO lfo;
lfo.prepare(sampleRate);
lfo.setWaveform(Waveform::SampleHold);
```

## Troubleshooting

### Output is Always Zero
- Ensure `prepare()` was called before `process()`
- Check that output buffer is being used correctly

### Output is Clipping
- All noise colors are bounded to [-1.0, 1.0]
- If mixing multiple sources, reduce individual levels

### Sequences Not Reproducing
- Ensure `setSeed()` is called before generating samples
- Call `reset()` to restart from beginning of sequence
- Verify same seed value is used

### Clicks on Color Change
- `setColor()` resets filter state, which may cause a small discontinuity
- For click-free transitions, consider crossfading between two oscillator instances
