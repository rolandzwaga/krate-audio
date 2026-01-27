# Quickstart: Spectral Morph Filter

**Date**: 2026-01-22 | **Spec**: 080-spectral-morph-filter

## Overview

SpectralMorphFilter is a Layer 2 processor that morphs between two audio signals by interpolating their magnitude spectra while preserving phase from a selectable source.

---

## Basic Usage

### Include Header

```cpp
#include <krate/dsp/processors/spectral_morph_filter.h>

using namespace Krate::DSP;
```

### Dual-Input Morphing (Cross-Synthesis)

```cpp
SpectralMorphFilter morpher;

// Initialize with sample rate and FFT size
morpher.prepare(44100.0, 2048);

// Set morph amount (0.0 = source A, 1.0 = source B)
morpher.setMorphAmount(0.5f);

// Use phase from source A (preserves transients)
morpher.setPhaseSource(PhaseSource::A);

// Process audio
morpher.processBlock(inputA, inputB, output, blockSize);
```

### Single-Input with Snapshot

```cpp
SpectralMorphFilter morpher;
morpher.prepare(44100.0, 2048);

// Capture a spectral snapshot (averages 4 frames by default)
morpher.captureSnapshot();

// Process input samples until snapshot is captured
for (int i = 0; i < numInitSamples; ++i) {
    float sample = morpher.process(initInput[i]);
}

// Now morph live input against the snapshot
morpher.setMorphAmount(0.5f);
for (int i = 0; i < numSamples; ++i) {
    output[i] = morpher.process(input[i]);
}
```

---

## Configuration Options

### FFT Size

```cpp
// Smaller FFT = lower latency, less frequency resolution
morpher.prepare(44100.0, 1024);  // 1024 samples latency

// Larger FFT = higher latency, better frequency resolution
morpher.prepare(44100.0, 4096);  // 4096 samples latency

// Default (good balance)
morpher.prepare(44100.0, 2048);  // 2048 samples latency
```

### Phase Source Selection

```cpp
// Use phase from source A (preserves A's temporal structure)
morpher.setPhaseSource(PhaseSource::A);

// Use phase from source B (preserves B's temporal structure)
morpher.setPhaseSource(PhaseSource::B);

// Blend phases via complex interpolation (smooth transitions)
morpher.setPhaseSource(PhaseSource::Blend);
```

### Spectral Shift (Formant Shifting)

```cpp
// Shift spectrum up one octave
morpher.setSpectralShift(12.0f);

// Shift spectrum down one octave
morpher.setSpectralShift(-12.0f);

// No shift (default)
morpher.setSpectralShift(0.0f);
```

### Spectral Tilt

```cpp
// Brighten: boost highs, cut lows (+6 dB/octave from 1kHz)
morpher.setSpectralTilt(6.0f);

// Darken: cut highs, boost lows (-6 dB/octave from 1kHz)
morpher.setSpectralTilt(-6.0f);

// Neutral (default)
morpher.setSpectralTilt(0.0f);
```

### Snapshot Averaging

```cpp
// More frames = smoother snapshot, slower capture
morpher.setSnapshotFrameCount(8);

// Fewer frames = noisier snapshot, faster capture
morpher.setSnapshotFrameCount(2);

// Default (good balance)
morpher.setSnapshotFrameCount(4);
```

---

## Common Patterns

### Vocal-Synth Hybrid

Morph a vocal with a synth pad, preserving vocal timing:

```cpp
SpectralMorphFilter morpher;
morpher.prepare(44100.0, 2048);
morpher.setMorphAmount(0.3f);  // 30% synth character
morpher.setPhaseSource(PhaseSource::A);  // Preserve vocal timing

// Process: vocal = inputA, synth = inputB
morpher.processBlock(vocalBuffer, synthBuffer, output, blockSize);
```

### Spectral Freeze with Morphing

Capture a "frozen" spectrum and morph live audio against it:

```cpp
SpectralMorphFilter morpher;
morpher.prepare(44100.0, 2048);

// Capture the freeze point
morpher.captureSnapshot();

// Feed initial audio to complete capture
for (size_t i = 0; i < captureLength; ++i) {
    morpher.process(captureInput[i]);
}

// Now live audio morphs with frozen spectrum
morpher.setMorphAmount(0.5f);
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = morpher.process(liveInput[i]);
}
```

### Chipmunk/Monster Effect

Use spectral shift for formant-independent pitch character:

```cpp
SpectralMorphFilter morpher;
morpher.prepare(44100.0, 2048);
morpher.setMorphAmount(0.0f);  // No morphing
morpher.setPhaseSource(PhaseSource::A);

// Chipmunk: shift formants up
morpher.setSpectralShift(12.0f);

// Monster: shift formants down
morpher.setSpectralShift(-12.0f);

morpher.processBlock(input, input, output, blockSize);
```

### Brightness Control

Use spectral tilt for tonal shaping:

```cpp
SpectralMorphFilter morpher;
morpher.prepare(44100.0, 2048);
morpher.setMorphAmount(0.0f);  // Passthrough morphing

// Make brighter
morpher.setSpectralTilt(3.0f);

// Make darker
morpher.setSpectralTilt(-3.0f);

morpher.processBlock(input, input, output, blockSize);
```

---

## State Management

### Reset

```cpp
// Clear all internal state (STFT buffers, snapshot)
morpher.reset();
```

### Query State

```cpp
// Check if prepared
if (morpher.isPrepared()) { /* ready to process */ }

// Check if snapshot captured
if (morpher.hasSnapshot()) { /* single-input mode available */ }

// Get latency for plugin delay compensation
size_t latency = morpher.getLatencySamples();  // == FFT size
```

---

## Performance Considerations

1. **Latency**: Equals FFT size (2048 samples = ~46ms at 44.1kHz)
2. **CPU**: Approximately 2.5% single core for 1 second stereo at 44.1kHz
3. **Memory**: ~130KB per instance (FFT size 2048)

### Optimize for Low Latency

```cpp
// Use smaller FFT for lower latency (trades frequency resolution)
morpher.prepare(44100.0, 512);  // ~12ms latency at 44.1kHz
```

### Optimize for Quality

```cpp
// Use larger FFT for better frequency resolution
morpher.prepare(44100.0, 4096);  // ~93ms latency at 44.1kHz
```

---

## Error Handling

```cpp
// Safe to call process before prepare (returns 0)
float sample = morpher.process(0.5f);  // Returns 0.0f

// Safe to pass nullptr (early return)
morpher.processBlock(nullptr, inputB, output, 512);  // No-op

// Parameters are clamped automatically
morpher.setMorphAmount(2.0f);  // Clamped to 1.0f
morpher.setSpectralShift(100.0f);  // Clamped to 24.0f
```

---

## Thread Safety

- **prepare()**: NOT thread-safe (allocates memory)
- **reset()**: Thread-safe
- **process() / processBlock()**: Thread-safe for audio thread
- **setMorphAmount(), etc.**: Thread-safe (atomic internally)

Typical pattern:

```cpp
// UI thread
morpher.setMorphAmount(newValue);

// Audio thread
morpher.processBlock(inputA, inputB, output, blockSize);
```
