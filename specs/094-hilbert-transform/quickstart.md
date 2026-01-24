# Quickstart: Hilbert Transform

**Spec**: 094-hilbert-transform | **Date**: 2026-01-24

## Overview

The HilbertTransform class creates an analytic signal by producing a 90-degree phase-shifted quadrature component alongside a delayed version of the input signal. This is essential for single-sideband modulation (frequency shifting).

## Basic Usage

### Single Sample Processing

```cpp
#include <krate/dsp/primitives/hilbert_transform.h>

using namespace Krate::DSP;

// Create and prepare the transform
HilbertTransform hilbert;
hilbert.prepare(44100.0);

// Process audio
float input = getNextSample();
HilbertOutput output = hilbert.process(input);

// Use the outputs
float inPhase = output.i;      // Original signal, delayed
float quadrature = output.q;   // 90 degrees phase-shifted
```

### Block Processing

```cpp
#include <krate/dsp/primitives/hilbert_transform.h>

using namespace Krate::DSP;

// Setup
HilbertTransform hilbert;
hilbert.prepare(44100.0);

// Prepare buffers
constexpr int kBlockSize = 512;
float input[kBlockSize];
float outputI[kBlockSize];
float outputQ[kBlockSize];

// Fill input buffer with audio...

// Process block
hilbert.processBlock(input, outputI, outputQ, kBlockSize);

// outputI contains in-phase components
// outputQ contains quadrature components
```

## Frequency Shifting Example

The primary use case is single-sideband modulation for frequency shifting:

```cpp
#include <krate/dsp/primitives/hilbert_transform.h>
#include <krate/dsp/primitives/lfo.h>
#include <cmath>

using namespace Krate::DSP;

class FrequencyShifter {
public:
    void prepare(double sampleRate) {
        hilbert_.prepare(sampleRate);
        sampleRate_ = sampleRate;
    }

    void setShiftHz(float hz) {
        shiftHz_ = hz;
    }

    float process(float input) {
        // Get analytic signal components
        HilbertOutput h = hilbert_.process(input);

        // Generate sine and cosine at shift frequency
        float omega = 2.0f * 3.14159f * shiftHz_ / static_cast<float>(sampleRate_);
        float cosVal = std::cos(phase_);
        float sinVal = std::sin(phase_);

        // Update phase
        phase_ += omega;
        if (phase_ > 2.0f * 3.14159f) {
            phase_ -= 2.0f * 3.14159f;
        }

        // Single-sideband modulation
        // Upper sideband: I * cos - Q * sin
        // Lower sideband: I * cos + Q * sin
        return h.i * cosVal - h.q * sinVal;  // Upper sideband
    }

private:
    HilbertTransform hilbert_;
    double sampleRate_ = 44100.0;
    float shiftHz_ = 0.0f;
    float phase_ = 0.0f;
};
```

## Latency Compensation

The Hilbert transform introduces a fixed 5-sample latency:

```cpp
HilbertTransform hilbert;
hilbert.prepare(44100.0);

int latency = hilbert.getLatencySamples();  // Returns 5

// In a plugin context, report this to the host
// for automatic delay compensation
```

## Reset Behavior

Call reset() when processing discontinuous audio:

```cpp
HilbertTransform hilbert;
hilbert.prepare(44100.0);

// Process some audio...

// Audio discontinuity (e.g., new file, transport jump)
hilbert.reset();

// Note: First 5 samples after reset are settling time
// Phase accuracy specification applies from sample 6 onward
```

## Sample Rate Handling

The transform accepts sample rates from 22050 Hz to 192000 Hz:

```cpp
HilbertTransform hilbert;

// Normal usage
hilbert.prepare(48000.0);

// Out-of-range sample rates are clamped silently
hilbert.prepare(10000.0);  // Clamped to 22050 Hz
hilbert.prepare(500000.0); // Clamped to 192000 Hz

// Query actual rate
double rate = hilbert.getSampleRate();
```

## Effective Bandwidth

Phase accuracy is guaranteed within the effective bandwidth:

| Sample Rate | Low Frequency | High Frequency |
|-------------|---------------|----------------|
| 44100 Hz | ~40 Hz | ~20 kHz |
| 48000 Hz | ~40 Hz | ~22 kHz |
| 96000 Hz | ~40 Hz | ~44 kHz |
| 192000 Hz | ~40 Hz | ~88 kHz |

Outside this bandwidth, phase accuracy degrades but the transform remains stable.

## Real-Time Safety

All methods are noexcept and allocation-free:

```cpp
HilbertTransform hilbert;

// Safe to call from audio thread
hilbert.prepare(44100.0);           // noexcept
hilbert.reset();                    // noexcept
HilbertOutput out = hilbert.process(0.5f);  // noexcept
hilbert.processBlock(in, outI, outQ, n);    // noexcept
```

## Integration with Existing DSP

The HilbertTransform is a Layer 1 primitive that can be composed with other DSP components:

```cpp
#include <krate/dsp/primitives/hilbert_transform.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/smoother.h>

class ModulatedFrequencyShifter {
    HilbertTransform hilbert_;
    LFO modLfo_;
    OnePoleSmoother shiftSmoother_;

    // ... compose these for modulated frequency shifting
};
```
