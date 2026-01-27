# Quickstart: FuzzProcessor

**Feature**: 063-fuzz-processor | **Date**: 2026-01-14

## Basic Usage

```cpp
#include <krate/dsp/processors/fuzz_processor.h>

using namespace Krate::DSP;

// Create processor
FuzzProcessor fuzz;

// Configure for sample rate (call once or when sample rate changes)
fuzz.prepare(44100.0, 512);  // 44.1kHz, up to 512 samples per block

// Set parameters
fuzz.setFuzzType(FuzzType::Germanium);  // Warm, saggy character
fuzz.setFuzz(0.7f);                      // 70% saturation
fuzz.setVolume(3.0f);                    // +3dB output boost
fuzz.setBias(0.8f);                      // Slight gating (near normal)
fuzz.setTone(0.6f);                      // Slightly bright

// Process audio (in-place)
fuzz.process(buffer, numSamples);
```

## Transistor Types

### Germanium (Warm, Vintage)

```cpp
fuzz.setFuzzType(FuzzType::Germanium);
```

Characteristics:
- Soft clipping with rounded peaks
- Even harmonics (2nd, 4th) for warmth
- "Saggy" response - loud signals compress more
- Responds to playing dynamics
- Classic Fuzz Face sound

### Silicon (Bright, Aggressive)

```cpp
fuzz.setFuzzType(FuzzType::Silicon);
```

Characteristics:
- Harder clipping for tighter sound
- Odd harmonics (3rd, 5th) for bite
- Consistent response regardless of level
- Tighter low end
- More modern fuzz sound

## Parameter Guide

### Fuzz Amount (0.0 - 1.0)

| Value | Character |
|-------|-----------|
| 0.0 | Clean, minimal distortion |
| 0.3 | Light overdrive |
| 0.5 | Classic fuzz (default) |
| 0.7 | Heavy saturation |
| 1.0 | Maximum fuzz, very compressed |

### Bias (0.0 - 1.0) - "Dying Battery" Effect

| Value | Character |
|-------|-----------|
| 0.0 | Extreme gating - sputtery, broken sound |
| 0.2 | Heavy gating - notes cut off when quiet |
| 0.5 | Moderate gating |
| 0.7 | Slight gating (default) |
| 1.0 | No gating - full sustain, normal operation |

### Tone (0.0 - 1.0)

| Value | Frequency | Character |
|-------|-----------|-----------|
| 0.0 | 400 Hz | Dark, muffled |
| 0.5 | 4200 Hz | Neutral (default) |
| 1.0 | 8000 Hz | Bright, open |

### Volume (-24 to +24 dB)

Output level adjustment for gain staging.

## Octave-Up Effect

```cpp
fuzz.setOctaveUp(true);  // Enable octave-up
fuzz.setOctaveUp(false); // Disable (default)
```

Creates a frequency-doubled "ring mod" style octave-up effect. Best used with:
- Neck pickup
- Single notes (not chords)
- Mid-range fuzz settings

## Common Presets

### Classic Germanium Fuzz

```cpp
fuzz.setFuzzType(FuzzType::Germanium);
fuzz.setFuzz(0.6f);
fuzz.setBias(0.7f);
fuzz.setTone(0.5f);
fuzz.setVolume(0.0f);
fuzz.setOctaveUp(false);
```

### Aggressive Silicon

```cpp
fuzz.setFuzzType(FuzzType::Silicon);
fuzz.setFuzz(0.8f);
fuzz.setBias(1.0f);  // Full sustain
fuzz.setTone(0.7f);  // Slightly bright
fuzz.setVolume(3.0f);
fuzz.setOctaveUp(false);
```

### Dying Battery

```cpp
fuzz.setFuzzType(FuzzType::Germanium);
fuzz.setFuzz(0.7f);
fuzz.setBias(0.2f);  // Heavy gating
fuzz.setTone(0.4f);
fuzz.setVolume(6.0f); // Compensate for gating
fuzz.setOctaveUp(false);
```

### Octavia Style

```cpp
fuzz.setFuzzType(FuzzType::Germanium);
fuzz.setFuzz(0.75f);
fuzz.setBias(0.8f);
fuzz.setTone(0.6f);
fuzz.setVolume(0.0f);
fuzz.setOctaveUp(true);  // Octave-up enabled
```

### Velvet Fuzz (Clean-ish)

```cpp
fuzz.setFuzzType(FuzzType::Germanium);
fuzz.setFuzz(0.3f);  // Low fuzz
fuzz.setBias(0.9f);
fuzz.setTone(0.5f);
fuzz.setVolume(-3.0f);
fuzz.setOctaveUp(false);
```

## Integration Example

### With Oversampling (Recommended for Anti-Aliasing)

```cpp
#include <krate/dsp/processors/fuzz_processor.h>
#include <krate/dsp/primitives/oversampler.h>

class FuzzEffect {
public:
    void prepare(double sampleRate, size_t maxBlockSize) {
        // Prepare oversampler (2x oversampling)
        oversampler_.prepare(sampleRate, maxBlockSize);

        // Prepare fuzz at oversampled rate
        fuzz_.prepare(sampleRate * 2.0, maxBlockSize * 2);
    }

    void process(float* buffer, size_t numSamples) {
        // Upsample
        float* upsampled = oversampler_.upsample(buffer, numSamples);

        // Process at 2x rate
        fuzz_.process(upsampled, numSamples * 2);

        // Downsample back
        oversampler_.downsample(upsampled, numSamples * 2, buffer);
    }

private:
    Oversampler<2> oversampler_;
    FuzzProcessor fuzz_;
};
```

### State Persistence

```cpp
struct FuzzState {
    FuzzType type;
    float fuzz;
    float volume;
    float bias;
    float tone;
    bool octaveUp;
};

void saveState(const FuzzProcessor& proc, FuzzState& state) {
    state.type = proc.getFuzzType();
    state.fuzz = proc.getFuzz();
    state.volume = proc.getVolume();
    state.bias = proc.getBias();
    state.tone = proc.getTone();
    state.octaveUp = proc.getOctaveUp();
}

void loadState(FuzzProcessor& proc, const FuzzState& state) {
    proc.setFuzzType(state.type);
    proc.setFuzz(state.fuzz);
    proc.setVolume(state.volume);
    proc.setBias(state.bias);
    proc.setTone(state.tone);
    proc.setOctaveUp(state.octaveUp);
}
```

## CPU Budget Reference

| Configuration | Estimated CPU @ 44.1kHz |
|---------------|-------------------------|
| Single type | ~0.1% |
| During type crossfade | ~0.2% |
| With 2x oversampling | ~0.3% |

## Thread Safety Notes

- All setters are thread-safe for parameter automation
- Parameter changes are smoothed (5ms) to prevent clicks
- Type changes trigger 5ms crossfade
- `process()` must only be called from audio thread
- `prepare()` and `reset()` must be called from audio thread or during initialization
