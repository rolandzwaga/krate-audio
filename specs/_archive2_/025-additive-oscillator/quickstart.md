# Quickstart: Additive Synthesis Oscillator

**Feature Branch**: `025-additive-oscillator`
**Date**: 2026-02-05

## Overview

The `AdditiveOscillator` class generates audio by summing up to 128 sinusoidal partials using efficient IFFT-based overlap-add resynthesis. It provides per-partial amplitude, frequency ratio, and phase control, plus macro parameters for spectral tilt and inharmonicity.

## Basic Usage

```cpp
#include <krate/dsp/processors/additive_oscillator.h>

using namespace Krate::DSP;

// Create oscillator
AdditiveOscillator osc;

// Initialize for processing (allocates buffers)
osc.prepare(44100.0, 2048);  // 44.1kHz, FFT size 2048

// Set fundamental frequency
osc.setFundamental(440.0f);

// Configure partials (1-based indexing)
osc.setNumPartials(8);
osc.setPartialAmplitude(1, 1.0f);   // Fundamental at full amplitude
osc.setPartialAmplitude(2, 0.5f);   // 2nd harmonic at -6dB
osc.setPartialAmplitude(3, 0.33f);  // 3rd harmonic
osc.setPartialAmplitude(4, 0.25f);  // 4th harmonic
// ... etc

// Generate audio
std::vector<float> output(256);
osc.processBlock(output.data(), output.size());
```

## Organ-Style Drawbars

```cpp
AdditiveOscillator organ;
organ.prepare(48000.0);
organ.setFundamental(261.63f);  // Middle C

// Set up 9 drawbar partials (Hammond organ style)
// Drawbar feet:   16'    8'    5 1/3'  4'    2 2/3'  2'    1 3/5'  1 1/3'  1'
// Partial number:  1      2      3      4      5       6      7       8      9
organ.setNumPartials(9);
organ.setPartialAmplitude(1, 0.8f);   // 16' (sub-octave)
organ.setPartialAmplitude(2, 1.0f);   // 8'  (fundamental for typical registration)
organ.setPartialAmplitude(3, 0.5f);   // 5 1/3' (quint)
organ.setPartialAmplitude(4, 0.7f);   // 4'
organ.setPartialAmplitude(5, 0.3f);   // 2 2/3'
organ.setPartialAmplitude(6, 0.5f);   // 2'
organ.setPartialAmplitude(7, 0.2f);   // 1 3/5'
organ.setPartialAmplitude(8, 0.3f);   // 1 1/3'
organ.setPartialAmplitude(9, 0.1f);   // 1'
```

## Bell/Piano Timbres with Inharmonicity

```cpp
AdditiveOscillator bell;
bell.prepare(48000.0);
bell.setFundamental(440.0f);
bell.setNumPartials(16);

// Apply inharmonicity for bell-like stretched partials
bell.setInharmonicity(0.01f);  // B = 0.01 for metallic timbre

// Set amplitude decay pattern
for (size_t n = 1; n <= 16; ++n) {
    float amp = 1.0f / static_cast<float>(n);  // 1/n decay
    bell.setPartialAmplitude(n, amp);
}

// With B=0.01, partial frequencies become:
// Partial 1: 440 * sqrt(1.01) = 442.2 Hz
// Partial 2: 880 * sqrt(1.04) = 897.4 Hz
// Partial 5: 2200 * sqrt(1.25) = 2459.7 Hz
// etc. (stretched tuning)
```

## Spectral Tilt for Brightness Control

```cpp
AdditiveOscillator osc;
osc.prepare(48000.0);
osc.setFundamental(220.0f);
osc.setNumPartials(32);

// Start with flat spectrum
for (size_t n = 1; n <= 32; ++n) {
    osc.setPartialAmplitude(n, 1.0f);
}

// Apply different tilt values:
osc.setSpectralTilt(-6.0f);   // Natural rolloff (-6 dB/octave)
// Partial 2 will be 6 dB quieter than partial 1
// Partial 4 will be 12 dB quieter than partial 1, etc.

osc.setSpectralTilt(-12.0f);  // Warmer, muted sound
osc.setSpectralTilt(0.0f);    // Flat (user amplitudes unchanged)
osc.setSpectralTilt(6.0f);    // Brighter, boosted highs
```

## Custom Frequency Ratios

```cpp
AdditiveOscillator osc;
osc.prepare(48000.0);
osc.setFundamental(440.0f);
osc.setNumPartials(5);

// Create non-integer ratios for gamelan-like timbre
osc.setPartialFrequencyRatio(1, 1.0f);     // Fundamental
osc.setPartialFrequencyRatio(2, 2.76f);    // Non-integer ratio
osc.setPartialFrequencyRatio(3, 5.404f);   // Non-integer ratio
osc.setPartialFrequencyRatio(4, 8.933f);   // Non-integer ratio
osc.setPartialFrequencyRatio(5, 12.17f);   // Non-integer ratio

// Set amplitudes
osc.setPartialAmplitude(1, 1.0f);
osc.setPartialAmplitude(2, 0.8f);
osc.setPartialAmplitude(3, 0.5f);
osc.setPartialAmplitude(4, 0.3f);
osc.setPartialAmplitude(5, 0.1f);
```

## Latency Compensation

```cpp
AdditiveOscillator osc;
osc.prepare(48000.0, 2048);

// Query latency for host compensation
size_t latencySamples = osc.latency();  // Returns 2048
float latencyMs = static_cast<float>(latencySamples) / 48000.0f * 1000.0f;
// latencyMs = 42.67 ms
```

## Phase Control at Reset

```cpp
AdditiveOscillator osc;
osc.prepare(48000.0);
osc.setFundamental(440.0f);
osc.setNumPartials(2);

// Set initial phases (applied at reset)
osc.setPartialPhase(1, 0.0f);    // Fundamental starts at phase 0
osc.setPartialPhase(2, 0.5f);    // 2nd harmonic starts at phase pi (180 degrees)

// Phases take effect when reset() is called
osc.reset();

// Now generate audio with the specified initial phases
osc.processBlock(output.data(), output.size());
```

## Safe State Query

```cpp
AdditiveOscillator osc;

// Before prepare()
assert(!osc.isPrepared());
assert(osc.latency() == 0);
assert(osc.sampleRate() == 0);
assert(osc.fftSize() == 0);

// processBlock() outputs zeros when not prepared
std::vector<float> out(64);
osc.processBlock(out.data(), out.size());
// out is all zeros

// After prepare()
osc.prepare(48000.0, 2048);
assert(osc.isPrepared());
assert(osc.latency() == 2048);
assert(osc.sampleRate() == 48000.0);
assert(osc.fftSize() == 2048);
```

## Performance Considerations

- **FFT Size Trade-off**: Larger FFT = better frequency resolution, higher latency
  - FFT 512: ~10.7 ms latency at 48kHz, coarser bins
  - FFT 2048: ~42.7 ms latency at 48kHz (recommended)
  - FFT 4096: ~85.3 ms latency at 48kHz, finest bins

- **CPU Complexity**: O(N log N) where N = FFT size, independent of partial count
  - Adding more partials does NOT increase CPU significantly
  - 128 partials costs the same as 1 partial

- **Memory**: ~80 KB per instance at FFT size 2048

- **Gain Staging**: No automatic normalization
  - 128 partials at amplitude 1.0 can produce very high peaks
  - User is responsible for overall gain control
  - Output is hard-clamped to [-2, +2] to prevent NaN/Inf

## Common Pitfalls

1. **Calling processBlock before prepare**: Outputs zeros (safe, but no sound)

2. **Partial numbering**: API is 1-based (partial 1 = fundamental)
   ```cpp
   osc.setPartialAmplitude(0, 1.0f);  // IGNORED - out of range
   osc.setPartialAmplitude(1, 1.0f);  // CORRECT - sets fundamental
   ```

3. **Phase changes during playback**: Phase is only applied at reset()
   ```cpp
   osc.setPartialPhase(1, 0.5f);  // Stored but not immediately applied
   osc.reset();                    // NOW phase takes effect
   ```

4. **Inharmonicity + high partials + low fundamental**: Partials may exceed Nyquist
   ```cpp
   osc.setFundamental(20.0f);      // Very low
   osc.setInharmonicity(0.1f);     // High stretch
   osc.setNumPartials(128);        // Many partials
   // High partials will be automatically excluded when above Nyquist
   ```
