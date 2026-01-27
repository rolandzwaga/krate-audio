# Data Model: Spectral Morph Filter

**Date**: 2026-01-22 | **Spec**: 080-spectral-morph-filter | **Layer**: 2 (Processors)

## Class Hierarchy

```
Layer 0 (Core)
├── math_constants.h (kPi, kTwoPi)
└── window_functions.h (Window::generate, WindowType)

Layer 1 (Primitives)
├── fft.h (FFT, Complex)
├── stft.h (STFT, OverlapAdd)
├── spectral_buffer.h (SpectralBuffer)
└── smoother.h (OnePoleSmoother)

Layer 2 (Processors)
└── spectral_morph_filter.h (SpectralMorphFilter, PhaseSource)  <- NEW
```

---

## Type Definitions

### PhaseSource Enum

```cpp
/// Phase source selection for spectral morphing
enum class PhaseSource : uint8_t {
    A,      ///< Use phase from source A exclusively
    B,      ///< Use phase from source B exclusively
    Blend   ///< Interpolate via complex vector lerp
};
```

---

## SpectralMorphFilter Class

### Public Constants

| Constant | Type | Value | Description |
|----------|------|-------|-------------|
| kMinFFTSize | size_t | 256 | Minimum supported FFT size |
| kMaxFFTSize | size_t | 4096 | Maximum supported FFT size |
| kDefaultFFTSize | size_t | 2048 | Default FFT size |
| kMinMorphAmount | float | 0.0f | Minimum morph (source A only) |
| kMaxMorphAmount | float | 1.0f | Maximum morph (source B only) |
| kMinSpectralShift | float | -24.0f | Minimum shift in semitones |
| kMaxSpectralShift | float | +24.0f | Maximum shift in semitones |
| kMinSpectralTilt | float | -12.0f | Minimum tilt in dB/octave |
| kMaxSpectralTilt | float | +12.0f | Maximum tilt in dB/octave |
| kTiltPivotHz | float | 1000.0f | Tilt pivot frequency |
| kDefaultSnapshotFrames | size_t | 4 | Default frames for snapshot averaging |

### Public Methods

#### Lifecycle

| Method | Signature | Description |
|--------|-----------|-------------|
| prepare | `void prepare(double sampleRate, size_t fftSize = kDefaultFFTSize) noexcept` | Initialize processor, allocate buffers |
| reset | `void reset() noexcept` | Clear all internal state |

#### Processing

| Method | Signature | Description |
|--------|-----------|-------------|
| processBlock | `void processBlock(const float* inputA, const float* inputB, float* output, size_t numSamples) noexcept` | Dual-input block processing |
| process | `float process(float input) noexcept` | Single-input sample processing (snapshot mode) |

#### Snapshot

| Method | Signature | Description |
|--------|-----------|-------------|
| captureSnapshot | `void captureSnapshot() noexcept` | Start snapshot capture from current input |
| setSnapshotFrameCount | `void setSnapshotFrameCount(size_t frames) noexcept` | Set number of frames to average |

#### Parameters

| Method | Signature | Description |
|--------|-----------|-------------|
| setMorphAmount | `void setMorphAmount(float amount) noexcept` | Set morph blend 0.0-1.0 |
| setPhaseSource | `void setPhaseSource(PhaseSource source) noexcept` | Set phase source mode |
| setSpectralShift | `void setSpectralShift(float semitones) noexcept` | Set pitch shift in semitones |
| setSpectralTilt | `void setSpectralTilt(float dBPerOctave) noexcept` | Set spectral tilt |

#### Query

| Method | Signature | Description |
|--------|-----------|-------------|
| getLatencySamples | `[[nodiscard]] size_t getLatencySamples() const noexcept` | Returns FFT size |
| getFftSize | `[[nodiscard]] size_t getFftSize() const noexcept` | Current FFT size |
| getMorphAmount | `[[nodiscard]] float getMorphAmount() const noexcept` | Current morph amount |
| getPhaseSource | `[[nodiscard]] PhaseSource getPhaseSource() const noexcept` | Current phase source |
| getSpectralShift | `[[nodiscard]] float getSpectralShift() const noexcept` | Current shift |
| getSpectralTilt | `[[nodiscard]] float getSpectralTilt() const noexcept` | Current tilt |
| hasSnapshot | `[[nodiscard]] bool hasSnapshot() const noexcept` | True if snapshot captured |
| isPrepared | `[[nodiscard]] bool isPrepared() const noexcept` | True if prepare() called |

### Private Members

#### Configuration State

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| sampleRate_ | double | 44100.0 | Current sample rate |
| fftSize_ | size_t | 2048 | Current FFT size |
| hopSize_ | size_t | 1024 | Hop size (50% overlap) |
| prepared_ | bool | false | Preparation state flag |

#### STFT Components

| Member | Type | Description |
|--------|------|-------------|
| stftA_ | STFT | Analysis for input A |
| stftB_ | STFT | Analysis for input B |
| overlapAdd_ | OverlapAdd | Synthesis with COLA |

#### Spectral Buffers

| Member | Type | Description |
|--------|------|-------------|
| spectrumA_ | SpectralBuffer | Spectrum of input A |
| spectrumB_ | SpectralBuffer | Spectrum of input B |
| outputSpectrum_ | SpectralBuffer | Processed output spectrum |

#### Snapshot State

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| snapshotSpectrum_ | SpectralBuffer | Final averaged snapshot |
| snapshotAccumulator_ | SpectralBuffer | Accumulation buffer |
| snapshotFrameCount_ | size_t | 4 | Frames to average |
| snapshotFramesAccumulated_ | size_t | 0 | Current accumulation count |
| hasSnapshot_ | bool | false | Snapshot captured flag |
| captureRequested_ | bool | false | Capture in progress |

#### Parameters

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| morphAmount_ | float | 0.0f | Morph blend (0=A, 1=B) |
| spectralShift_ | float | 0.0f | Pitch shift in semitones |
| spectralTilt_ | float | 0.0f | Tilt in dB/octave |
| phaseSource_ | PhaseSource | A | Phase source selection |

#### Smoothers

| Member | Type | Description |
|--------|------|-------------|
| morphSmoother_ | OnePoleSmoother | Smooths morph amount |
| tiltSmoother_ | OnePoleSmoother | Smooths spectral tilt |

#### Temporary Buffers

| Member | Type | Description |
|--------|------|-------------|
| shiftedMagnitudes_ | std::vector<float> | Temp buffer for shift |
| shiftedPhases_ | std::vector<float> | Temp buffer for shift |

---

## Memory Layout

### Allocation in prepare()

```
prepare(sampleRate, fftSize):
    numBins = fftSize / 2 + 1
    hopSize = fftSize / 2

    // STFT components
    stftA_.prepare(fftSize, hopSize, Hann)      // Internal buffers
    stftB_.prepare(fftSize, hopSize, Hann)      // Internal buffers
    overlapAdd_.prepare(fftSize, hopSize, Hann) // Internal buffers

    // Spectral buffers (numBins Complex each)
    spectrumA_.prepare(fftSize)           // numBins * sizeof(Complex)
    spectrumB_.prepare(fftSize)           // numBins * sizeof(Complex)
    outputSpectrum_.prepare(fftSize)      // numBins * sizeof(Complex)
    snapshotSpectrum_.prepare(fftSize)    // numBins * sizeof(Complex)
    snapshotAccumulator_.prepare(fftSize) // numBins * sizeof(Complex)

    // Temp buffers
    shiftedMagnitudes_.resize(numBins)    // numBins * sizeof(float)
    shiftedPhases_.resize(numBins)        // numBins * sizeof(float)

    // Smoothers
    morphSmoother_.configure(50.0f, sampleRate)
    tiltSmoother_.configure(50.0f, sampleRate)
```

### Memory Estimate (FFT size 2048)

| Component | Size |
|-----------|------|
| numBins | 1025 |
| SpectralBuffer x5 | 5 * 1025 * 8 = 41,000 bytes |
| Temp buffers x2 | 2 * 1025 * 4 = 8,200 bytes |
| STFT internal (x2) | ~32KB each = 64KB |
| OverlapAdd internal | ~16KB |
| **Total** | ~130 KB |

---

## Processing Flow

### Dual-Input Mode (processBlock)

```
processBlock(inputA, inputB, output, numSamples):
    // 1. Push samples to both STFT analyzers
    stftA_.pushSamples(inputA, numSamples)
    stftB_.pushSamples(inputB, numSamples)

    // 2. Process available spectral frames
    while (stftA_.canAnalyze() && stftB_.canAnalyze()):
        stftA_.analyze(spectrumA_)
        stftB_.analyze(spectrumB_)
        processSpectralFrame()
        overlapAdd_.synthesize(outputSpectrum_)

    // 3. Pull output samples
    available = overlapAdd_.samplesAvailable()
    toPull = min(numSamples, available)
    overlapAdd_.pullSamples(output, toPull)
```

### Spectral Frame Processing

```
processSpectralFrame():
    // Get smoothed parameters
    morph = morphSmoother_.process()
    tilt = tiltSmoother_.process()

    // 1. Magnitude interpolation
    for bin in 0..numBins:
        magA = spectrumA_.getMagnitude(bin)
        magB = spectrumB_.getMagnitude(bin)
        outputSpectrum_.setMagnitude(bin, magA*(1-morph) + magB*morph)

    // 2. Phase selection
    switch (phaseSource_):
        case A: copyPhaseFrom(spectrumA_)
        case B: copyPhaseFrom(spectrumB_)
        case Blend: blendPhaseComplex(spectrumA_, spectrumB_, morph)

    // 3. Apply spectral shift
    if (spectralShift_ != 0.0f):
        applySpectralShift(outputSpectrum_, spectralShift_)

    // 4. Apply spectral tilt
    if (tilt != 0.0f):
        applySpectralTilt(outputSpectrum_, tilt)
```

### Snapshot Mode (process)

```
process(input):
    // 1. Push sample to STFT A (reused for snapshot mode)
    stftA_.pushSamples(&input, 1)

    // 2. Check for snapshot capture
    if (captureRequested_ && stftA_.canAnalyze()):
        stftA_.analyze(spectrumA_)
        accumulateSnapshotFrame(spectrumA_)

    // 3. Process with snapshot if available
    if (hasSnapshot_ && stftA_.canAnalyze()):
        stftA_.analyze(spectrumA_)
        // Morph between spectrumA_ and snapshotSpectrum_
        processSpectralFrameWithSnapshot()
        overlapAdd_.synthesize(outputSpectrum_)

    // 4. Pull one sample
    if (overlapAdd_.samplesAvailable() > 0):
        overlapAdd_.pullSamples(&result, 1)
        return result
    return 0.0f
```

---

## State Transitions

### Snapshot Capture State Machine

```
States: IDLE, CAPTURING, COMPLETE

IDLE:
    captureSnapshot() -> CAPTURING
    snapshotFramesAccumulated_ = 0
    captureRequested_ = true

CAPTURING:
    Each frame: accumulateSnapshotFrame()
    snapshotFramesAccumulated_++
    if (snapshotFramesAccumulated_ >= snapshotFrameCount_):
        finalizeSnapshot()
        -> COMPLETE

COMPLETE:
    hasSnapshot_ = true
    captureRequested_ = false
    New captureSnapshot() -> CAPTURING (replaces old)
```

---

## Dependencies

```cpp
// Layer 0 (Core)
#include <krate/dsp/core/math_constants.h>    // kPi, kTwoPi
#include <krate/dsp/core/window_functions.h>  // WindowType

// Layer 1 (Primitives)
#include <krate/dsp/primitives/fft.h>             // Complex
#include <krate/dsp/primitives/stft.h>            // STFT, OverlapAdd
#include <krate/dsp/primitives/spectral_buffer.h> // SpectralBuffer
#include <krate/dsp/primitives/smoother.h>        // OnePoleSmoother

// Standard library
#include <algorithm>  // std::clamp, std::copy
#include <cmath>      // std::pow, std::log2, std::atan2
#include <cstddef>    // size_t
#include <cstdint>    // uint8_t
#include <vector>     // std::vector
```
