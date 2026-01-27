# Quickstart: Audio-Rate Filter FM

**Feature**: 095-audio-rate-filter-fm | **Date**: 2026-01-24

## Overview

AudioRateFilterFM creates metallic, bell-like, and aggressive timbres by modulating a filter's cutoff frequency at audio rates (20Hz-20kHz) rather than traditional LFO rates.

## Include

```cpp
#include <krate/dsp/processors/audio_rate_filter_fm.h>

using namespace Krate::DSP;
```

---

## Basic Usage

### Example 1: Bell-Like Tones (Internal Oscillator)

```cpp
AudioRateFilterFM fm;

// Initialize
fm.prepare(44100.0, 512);

// Configure carrier filter
fm.setCarrierCutoff(1000.0f);   // 1kHz base frequency
fm.setCarrierQ(8.0f);            // High resonance for ring
fm.setFilterType(FMFilterType::Bandpass);

// Configure internal modulator
fm.setModulatorSource(FMModSource::Internal);
fm.setModulatorFrequency(440.0f);  // A4 modulator
fm.setModulatorWaveform(FMWaveform::Sine);
fm.setFMDepth(2.0f);  // 2 octaves modulation

// Enable oversampling for clean results
fm.setOversamplingFactor(2);

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = fm.process(input[i]);
}
```

### Example 2: External Modulation (Cross-Synthesis)

```cpp
AudioRateFilterFM fm;
fm.prepare(44100.0, 512);

// Configure carrier
fm.setCarrierCutoff(2000.0f);
fm.setCarrierQ(4.0f);
fm.setFilterType(FMFilterType::Lowpass);

// Use external modulator (e.g., second oscillator, voice)
fm.setModulatorSource(FMModSource::External);
fm.setFMDepth(1.0f);  // 1 octave depth

// Process with external modulator signal
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = fm.process(input[i], modulator[i]);
}
```

### Example 3: Self-Modulation (Chaos/Growl)

```cpp
AudioRateFilterFM fm;
fm.prepare(44100.0, 512);

// Configure for aggressive, chaotic tones
fm.setCarrierCutoff(800.0f);
fm.setCarrierQ(12.0f);  // High resonance amplifies chaos
fm.setFilterType(FMFilterType::Bandpass);

// Enable self-modulation (feedback FM)
fm.setModulatorSource(FMModSource::Self);
fm.setFMDepth(3.0f);  // High depth for aggressive effect

// 4x oversampling for stability
fm.setOversamplingFactor(4);

// Process - filter output feeds back to modulate itself
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = fm.process(input[i]);
}
```

---

## Block Processing

For efficiency, use block processing instead of sample-by-sample:

```cpp
// Internal or Self modulation
fm.processBlock(buffer, numSamples);

// External modulation
fm.processBlock(buffer, modulatorBuffer, numSamples);
```

---

## Parameter Summary

### Carrier Filter

| Method | Range | Default | Description |
|--------|-------|---------|-------------|
| setCarrierCutoff(hz) | 20-Nyquist | 1000 Hz | Base filter frequency |
| setCarrierQ(q) | 0.5-20.0 | 0.707 | Resonance/Q factor |
| setFilterType(type) | enum | Lowpass | LP/HP/BP/Notch |

### Modulator

| Method | Range | Default | Description |
|--------|-------|---------|-------------|
| setModulatorSource(src) | enum | Internal | Internal/External/Self |
| setModulatorFrequency(hz) | 0.1-20000 | 440 Hz | Internal osc frequency |
| setModulatorWaveform(wf) | enum | Sine | Sine/Tri/Saw/Square |

### FM Control

| Method | Range | Default | Description |
|--------|-------|---------|-------------|
| setFMDepth(oct) | 0.0-6.0 | 1.0 | Modulation depth in octaves |
| setOversamplingFactor(n) | 1/2/4 | 1 | Anti-aliasing factor |

---

## Typical Use Cases

### Metallic Percussion

- **Carrier**: Bandpass, 500-2000Hz, Q=10-15
- **Modulator**: Internal, 200-800Hz, Sine
- **Depth**: 2-4 octaves
- **Oversampling**: 2x

### Ring Modulation Effects

- **Carrier**: Bandpass, at target frequency, Q=5-10
- **Modulator**: External audio signal
- **Depth**: 1-2 octaves
- **Oversampling**: 2x

### Aggressive Bass

- **Carrier**: Lowpass, 200-500Hz, Q=4-8
- **Modulator**: Internal, 50-100Hz, Sawtooth or Square
- **Depth**: 1-2 octaves
- **Oversampling**: 2x

### Chaos/Noise

- **Carrier**: Bandpass, any frequency, Q=10+
- **Modulator**: Self-modulation
- **Depth**: 3-6 octaves (extreme)
- **Oversampling**: 4x

---

## Thread Safety

AudioRateFilterFM is **NOT thread-safe**. For stereo processing, create separate instances:

```cpp
AudioRateFilterFM fmLeft, fmRight;

fmLeft.prepare(sampleRate, blockSize);
fmRight.prepare(sampleRate, blockSize);

// Configure both identically or differently
fmLeft.setCarrierCutoff(1000.0f);
fmRight.setCarrierCutoff(1000.0f);

// Process separately
fmLeft.processBlock(leftBuffer, numSamples);
fmRight.processBlock(rightBuffer, numSamples);
```

---

## Latency

```cpp
// Query latency (for delay compensation)
size_t latency = fm.getLatency();

// Latency depends on oversampling:
// - Factor 1 (no oversampling): 0 samples
// - Factor 2/4 with Economy mode: 0 samples
// - Factor 2/4 with FIR mode: 15-31 samples
```

---

## Reset

Clear filter state when starting new audio region:

```cpp
fm.reset();  // Clears SVF state, oscillator phase, feedback history
```

---

## Real-Time Safety

After `prepare()` is called:
- All parameter setters are real-time safe (no allocations)
- `process()` and `processBlock()` are real-time safe
- No locks, no exceptions, no I/O

Only `prepare()` allocates memory and should be called outside the audio thread.
