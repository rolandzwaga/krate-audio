# Quickstart: WavefolderProcessor

**Feature**: 061-wavefolder-processor
**Date**: 2026-01-14

## Basic Usage

```cpp
#include <krate/dsp/processors/wavefolder_processor.h>

using namespace Krate::DSP;

// Create processor
WavefolderProcessor folder;

// Configure for sample rate (must call before processing)
folder.prepare(44100.0, 512);

// Set parameters
folder.setModel(WavefolderModel::Simple);
folder.setFoldAmount(2.0f);   // Moderate folding
folder.setSymmetry(0.0f);     // Symmetric (odd harmonics)
folder.setMix(1.0f);          // 100% wet

// Process audio in-place
folder.process(buffer, numSamples);
```

## Model Selection

### Simple (Triangle Fold)

Dense odd harmonics with smooth rolloff. Good for guitar effects.

```cpp
folder.setModel(WavefolderModel::Simple);
folder.setFoldAmount(3.0f);  // Higher = more folds per cycle
```

### Serge (Sine Fold)

Characteristic FM-like sparse spectrum. Classic synthesizer wavefolder.

```cpp
folder.setModel(WavefolderModel::Serge);
folder.setFoldAmount(3.14159f);  // Pi for classic Serge tone
```

### Buchla259 (5-Stage Parallel)

Complex timbre from parallel folding stages. Two sub-modes available.

```cpp
// Classic mode (authentic fixed values)
folder.setModel(WavefolderModel::Buchla259);
// buchlaMode defaults to Classic

// Custom mode (user-defined thresholds/gains)
folder.setBuchlaMode(BuchlaMode::Custom);
folder.setBuchlaThresholds({0.15f, 0.35f, 0.55f, 0.75f, 0.95f});
folder.setBuchlaGains({1.0f, 0.9f, 0.7f, 0.5f, 0.3f});
```

### Lockhart (Lambert-W)

Rich even and odd harmonics with characteristic spectral nulls.

```cpp
folder.setModel(WavefolderModel::Lockhart);
folder.setFoldAmount(2.0f);  // Soft saturation at lower values
```

## Adding Even Harmonics (Symmetry)

Symmetry creates asymmetric folding for even harmonic content (tube-like warmth).

```cpp
// Symmetric folding (odd harmonics only)
folder.setSymmetry(0.0f);

// Add even harmonics (2nd, 4th, etc.)
folder.setSymmetry(0.5f);    // Moderate asymmetry
folder.setSymmetry(1.0f);    // Maximum asymmetry

// Negative asymmetry (opposite direction)
folder.setSymmetry(-0.5f);
```

## Parallel Processing (Mix Control)

Use mix for parallel processing (blend dry with folded).

```cpp
// 100% wet (full effect)
folder.setMix(1.0f);

// 50% wet (parallel blend)
folder.setMix(0.5f);

// Full bypass (output = input)
folder.setMix(0.0f);
```

## Real-Time Parameter Changes

Parameters are smoothed internally (5ms) to prevent clicks.

```cpp
// Safe to change during processing
folder.setFoldAmount(5.0f);  // Will smoothly ramp

// Model changes are immediate (use mix for smooth transitions)
folder.setMix(0.0f);                         // Fade out
folder.setModel(WavefolderModel::Serge);     // Switch model
folder.setMix(1.0f);                         // Fade in
```

## Lifecycle Management

```cpp
// Initial setup
folder.prepare(44100.0, 512);

// Clear state between audio segments
folder.reset();

// Change sample rate (re-prepare)
folder.prepare(48000.0, 512);
```

## Edge Cases

```cpp
// Before prepare() - safe, returns input unchanged
WavefolderProcessor folder;
folder.process(buffer, 512);  // Output = input

// Zero samples - handled gracefully
folder.process(buffer, 0);    // No-op

// NaN/Infinity input - propagates through
// (real-time safe, no branching)
```

## Performance Notes

- Layer 2 processor: < 0.5% CPU per mono instance
- Buchla259 mode: ~2x CPU of other modes (5 parallel folds)
- No internal oversampling - wrap with Oversampler if needed for anti-aliasing

```cpp
// With external oversampling
#include <krate/dsp/primitives/oversampler.h>

Oversampler2x oversampler;
WavefolderProcessor folder;

oversampler.prepare(44100.0, 512, OversamplingQuality::Standard, OversamplingMode::ZeroLatency);
folder.prepare(88200.0, 1024);  // 2x sample rate

oversampler.process(left, right, numSamples, [&](float* L, float* R, size_t n) {
    folder.process(L, n);  // Process at 2x rate
    folder.process(R, n);
});
```

## Complete Example

```cpp
#include <krate/dsp/processors/wavefolder_processor.h>
#include <vector>

using namespace Krate::DSP;

void processAudio(float* input, float* output, size_t numSamples) {
    static WavefolderProcessor folder;
    static bool initialized = false;

    if (!initialized) {
        folder.prepare(44100.0, 512);
        folder.setModel(WavefolderModel::Serge);
        folder.setFoldAmount(3.0f);
        folder.setSymmetry(0.2f);   // Slight asymmetry
        folder.setMix(0.7f);        // 70% wet
        initialized = true;
    }

    // Copy input to output buffer
    std::copy(input, input + numSamples, output);

    // Process in-place
    folder.process(output, numSamples);
}
```
