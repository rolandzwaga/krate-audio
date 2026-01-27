# Quickstart: Spectral Gate

**Feature**: 081-spectral-gate | **Date**: 2026-01-22

## Overview

SpectralGate is a per-bin noise gate that processes audio in the frequency domain. It independently gates each frequency bin based on its magnitude relative to a configurable threshold, with attack/release envelopes, expansion ratio control, and spectral smearing for reduced artifacts.

---

## Basic Usage

### Minimal Setup

```cpp
#include <krate/dsp/processors/spectral_gate.h>

using namespace Krate::DSP;

// Create and prepare
SpectralGate gate;
gate.prepare(44100.0, 1024);  // 44.1kHz, 1024-point FFT

// Set threshold (bins below -40dB will be gated)
gate.setThreshold(-40.0f);

// Process audio (in your audio callback)
gate.processBlock(buffer, numSamples);
```

### Complete Setup with All Parameters

```cpp
#include <krate/dsp/processors/spectral_gate.h>

using namespace Krate::DSP;

SpectralGate gate;

// Prepare with custom FFT size
gate.prepare(48000.0, 2048);  // 48kHz, 2048-point FFT for better frequency resolution

// Configure gating parameters
gate.setThreshold(-35.0f);    // Gate threshold in dB
gate.setRatio(100.0f);        // 100:1 = hard gate (use lower for soft expansion)

// Configure envelope timing
gate.setAttack(5.0f);         // 5ms attack (fast for transients)
gate.setRelease(200.0f);      // 200ms release (smooth decay)

// Limit to specific frequency range (e.g., gate only high frequencies)
gate.setFrequencyRange(2000.0f, 20000.0f);  // Only gate 2kHz-20kHz

// Enable spectral smearing to reduce musical noise
gate.setSmearing(0.3f);       // Moderate smearing

// Process
gate.processBlock(buffer, numSamples);
```

---

## Common Use Cases

### 1. Basic Noise Reduction

Remove low-level broadband noise while preserving prominent spectral content.

```cpp
SpectralGate gate;
gate.prepare(44100.0, 1024);

// Conservative settings for clean noise reduction
gate.setThreshold(-50.0f);    // Low threshold to only catch noise floor
gate.setRatio(100.0f);        // Hard gate
gate.setAttack(1.0f);         // Fast attack to preserve transients
gate.setRelease(50.0f);       // Medium release
gate.setSmearing(0.2f);       // Light smearing to reduce artifacts
```

### 2. High-Frequency Hiss Removal

Gate only high frequencies to remove tape hiss or mic noise.

```cpp
SpectralGate gate;
gate.prepare(44100.0, 1024);

gate.setThreshold(-40.0f);
gate.setRatio(100.0f);
gate.setAttack(0.5f);         // Very fast attack
gate.setRelease(100.0f);

// Only affect high frequencies where hiss lives
gate.setFrequencyRange(4000.0f, 20000.0f);
gate.setSmearing(0.0f);       // No smearing needed for HF only
```

### 3. Creative Spectral "Skeletonization"

Create sparse, skeletal textures by aggressively gating quiet components.

```cpp
SpectralGate gate;
gate.prepare(44100.0, 2048);  // Larger FFT for more spectral detail

gate.setThreshold(-20.0f);    // High threshold = aggressive gating
gate.setRatio(100.0f);        // Hard gate
gate.setAttack(10.0f);
gate.setRelease(300.0f);
gate.setSmearing(0.0f);       // No smearing for maximum sparseness
```

### 4. Soft Expansion for Subtle Noise Reduction

Use expansion ratio for gentle, transparent noise reduction.

```cpp
SpectralGate gate;
gate.prepare(44100.0, 1024);

gate.setThreshold(-45.0f);
gate.setRatio(2.0f);          // Gentle 2:1 expansion instead of hard gate
gate.setAttack(10.0f);
gate.setRelease(100.0f);
gate.setSmearing(0.5f);       // More smearing for smoother result
```

### 5. Preserve Low-End, Gate Everything Else

Keep bass frequencies intact while gating mid and high frequencies.

```cpp
SpectralGate gate;
gate.prepare(44100.0, 1024);

gate.setThreshold(-40.0f);
gate.setRatio(100.0f);
gate.setAttack(5.0f);
gate.setRelease(100.0f);

// Gate 200Hz and above, pass bass through untouched
gate.setFrequencyRange(200.0f, 20000.0f);
```

---

## Single-Sample Processing

For applications requiring sample-by-sample processing:

```cpp
SpectralGate gate;
gate.prepare(44100.0, 1024);
gate.setThreshold(-40.0f);

// Process one sample at a time
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = gate.process(input[i]);
}

// Note: Latency is still FFT size samples
// First fftSize samples of output will be zeros/ramping
```

---

## Querying State

```cpp
SpectralGate gate;
gate.prepare(44100.0, 1024);

// Check preparation status
if (!gate.isPrepared()) {
    // Handle error
}

// Get latency for plugin delay compensation
size_t latency = gate.getLatencySamples();  // Returns 1024

// Get FFT configuration
size_t fftSize = gate.getFftSize();    // Returns 1024
size_t numBins = gate.getNumBins();    // Returns 513

// Get current parameter values
float threshold = gate.getThreshold();
float ratio = gate.getRatio();
float attack = gate.getAttack();
float release = gate.getRelease();
float lowHz = gate.getLowFrequency();
float highHz = gate.getHighFrequency();
float smearing = gate.getSmearing();
```

---

## Resetting State

To clear all internal state (e.g., when starting new audio):

```cpp
SpectralGate gate;
gate.prepare(44100.0, 1024);

// ... process some audio ...

// Reset clears envelopes and STFT buffers
// but preserves parameter settings
gate.reset();

// Processor is ready to use again immediately
gate.processBlock(newBuffer, numSamples);
```

---

## FFT Size Selection

| FFT Size | Frequency Resolution | Time Resolution | Latency | Use Case |
|----------|---------------------|-----------------|---------|----------|
| 256 | ~172 Hz @ 44.1kHz | ~5.8 ms | 256 samples | Fast transients, live use |
| 512 | ~86 Hz | ~11.6 ms | 512 samples | Balance for most content |
| 1024 | ~43 Hz (default) | ~23.2 ms | 1024 samples | Good general purpose |
| 2048 | ~21.5 Hz | ~46.4 ms | 2048 samples | Detailed spectral work |
| 4096 | ~10.8 Hz | ~92.9 ms | 4096 samples | Maximum resolution |

**Recommendation**: Start with 1024 (default). Use smaller for percussive material where latency matters, larger for sustained sounds where frequency resolution matters.

---

## Parameter Ranges Reference

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| threshold | [-96, 0] dB | -40 dB | Gate open above this level |
| ratio | [1, 100] | 100 | 1=bypass, 100=hard gate |
| attack | [0.1, 500] ms | 10 ms | 10%-90% rise time |
| release | [1, 5000] ms | 100 ms | 90%-10% fall time |
| lowHz | [20, 20000] Hz | 20 Hz | Lower frequency bound |
| highHz | [20, 20000] Hz | 20000 Hz | Upper frequency bound |
| smearing | [0, 1] | 0 | 0=off, 1=maximum |

---

## Thread Safety

SpectralGate is **not thread-safe**. The following guidelines apply:

- **prepare()**: Call from main thread only, never during processing
- **reset()**: Safe to call from audio thread
- **processBlock()/process()**: Audio thread only
- **Parameter setters**: Safe to call from any thread (internal smoothing prevents clicks)
- **Parameter getters**: Safe to call from any thread

For VST3 plugin use:
- Call `prepare()` in `Processor::setupProcessing()`
- Call `reset()` in `Processor::setActive(true)`
- Call `processBlock()` in `Processor::process()`
- Parameter changes from controller are safe via atomic floats + smoothing

---

## Performance Notes

- **CPU Budget**: < 0.5% single core @ 44.1kHz with 1024 FFT (SC-007)
- **Memory**: ~43 KB for 1024 FFT (pre-allocated in prepare())
- **Latency**: Exactly FFT size samples (SC-003)
- All processing is **noexcept** and allocation-free after prepare()
