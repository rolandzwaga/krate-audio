# Data Model: Spectral Distortion Processor

**Feature**: 103-spectral-distortion | **Date**: 2026-01-25

## Entities

### SpectralDistortionMode

Enumeration defining the four distortion algorithms.

| Value | Name | Description |
|-------|------|-------------|
| 0 | PerBinSaturate | Per-bin waveshaping with natural phase evolution |
| 1 | MagnitudeOnly | Per-bin waveshaping with exact phase preservation |
| 2 | BinSelective | Per-band drive control with frequency crossovers |
| 3 | SpectralBitcrush | Magnitude quantization with exact phase preservation |

**Constraints**:
- Underlying type: `uint8_t`
- Valid range: 0-3

### GapBehavior

Enumeration defining how unassigned bins are processed in BinSelective mode.

| Value | Name | Description |
|-------|------|-------------|
| 0 | Passthrough | Unassigned bins pass through unmodified |
| 1 | UseGlobalDrive | Unassigned bins use global drive parameter |

**Constraints**:
- Underlying type: `uint8_t`
- Only relevant when mode = BinSelective

### BandConfig (Internal)

Structure holding frequency band configuration for BinSelective mode.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| lowHz | float | 0.0f | Lower frequency bound in Hz |
| highHz | float | 0.0f | Upper frequency bound in Hz |
| drive | float | 1.0f | Drive amount for this band |
| lowBin | size_t | 0 | Cached: Lower bin index (computed) |
| highBin | size_t | 0 | Cached: Upper bin index (computed) |

**Constraints**:
- lowHz < highHz (swapped if necessary)
- drive >= 0.0 (clamped)
- lowBin/highBin recomputed on prepare() or frequency change

### SpectralDistortion

Main processor class that composes STFT, OverlapAdd, SpectralBuffer, and Waveshaper.

#### State

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| stft_ | STFT | - | Forward FFT analyzer |
| overlapAdd_ | OverlapAdd | - | Inverse FFT synthesizer |
| inputSpectrum_ | SpectralBuffer | - | Input frequency domain data |
| outputSpectrum_ | SpectralBuffer | - | Output frequency domain data |
| waveshaper_ | Waveshaper | - | Per-bin saturation processor |
| mode_ | SpectralDistortionMode | PerBinSaturate | Current processing mode |
| drive_ | float | 1.0f | Global drive parameter |
| magnitudeBits_ | float | 16.0f | Bit depth for SpectralBitcrush |
| processDCNyquist_ | bool | false | Include DC/Nyquist bins |
| gapBehavior_ | GapBehavior | Passthrough | Gap handling for BinSelective |
| lowBand_ | BandConfig | - | Low frequency band config |
| midBand_ | BandConfig | - | Mid frequency band config |
| highBand_ | BandConfig | - | High frequency band config |
| sampleRate_ | double | 44100.0 | Cached sample rate |
| fftSize_ | size_t | 2048 | Configured FFT size |
| hopSize_ | size_t | 1024 | Hop size (fftSize/2) |
| numBins_ | size_t | 1025 | Number of bins (fftSize/2+1) |
| prepared_ | bool | false | Initialization state |
| storedPhases_ | vector<float> | - | Phase storage for MagnitudeOnly mode |

#### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| kMinFFTSize | 256 | Minimum supported FFT size |
| kMaxFFTSize | 8192 | Maximum supported FFT size |
| kDefaultFFTSize | 2048 | Default FFT size |
| kMinDrive | 0.0f | Minimum drive (bypass) |
| kMaxDrive | 10.0f | Maximum drive |
| kMinBits | 1.0f | Minimum bit depth (binary) |
| kMaxBits | 16.0f | Maximum bit depth (transparent) |

## Relationships

```
SpectralDistortion
    |-- STFT (composition, 1:1)
    |-- OverlapAdd (composition, 1:1)
    |-- SpectralBuffer inputSpectrum_ (composition, 1:1)
    |-- SpectralBuffer outputSpectrum_ (composition, 1:1)
    |-- Waveshaper (composition, 1:1)
    |-- BandConfig lowBand_ (composition, 1:1)
    |-- BandConfig midBand_ (composition, 1:1)
    |-- BandConfig highBand_ (composition, 1:1)
```

## Validation Rules

### FFT Size
- Must be power of 2
- Clamped to [kMinFFTSize, kMaxFFTSize]
- Non-power-of-2 values rounded to nearest power of 2

### Drive
- Clamped to [kMinDrive, kMaxDrive]
- Drive = 0 bypasses processing (no waveshaper computation)

### Magnitude Bits
- Clamped to [kMinBits, kMaxBits]
- Fractional values allowed (continuous bit depth)

### Band Frequencies
- Low/high swapped if lowHz > highHz
- Clamped to [0, sampleRate/2]
- Converted to nearest bin index

### Band Overlap Resolution
- When multiple bands claim a bin, highest drive wins

## State Transitions

### Initialization Flow

```
[Uninitialized] --prepare()--> [Prepared]
    |                              |
    v                              v
prepared_=false              prepared_=true
numBins_=0                   numBins_=fftSize/2+1
```

### Processing Flow (per call to processBlock)

```
[Input Samples]
    |
    v
pushSamples() to STFT
    |
    v
[Check canAnalyze()]
    |
    +--yes--> analyze() --> processSpectralFrame() --> synthesize()
    |
    +--no--> continue accumulating
    |
    v
pullSamples() from OverlapAdd
    |
    v
[Output Samples]
```

### Spectral Frame Processing Flow

```
[inputSpectrum_]
    |
    +-- mode_ == PerBinSaturate --> applyPerBinSaturate()
    |
    +-- mode_ == MagnitudeOnly --> applyMagnitudeOnly()
    |
    +-- mode_ == BinSelective --> applyBinSelective()
    |
    +-- mode_ == SpectralBitcrush --> applySpectralBitcrush()
    |
    v
[outputSpectrum_]
```

## Bin Processing Logic

### DC/Nyquist Handling

```
For bin in [0, numBins):
    if (bin == 0 || bin == numBins-1) && !processDCNyquist_:
        outputSpectrum_[bin] = inputSpectrum_[bin]  // pass through
    else:
        process bin according to mode
```

### Drive=0 Bypass

```
For each bin:
    if effectiveDrive == 0:
        outputSpectrum_[bin] = inputSpectrum_[bin]  // pass through
    else:
        apply waveshaping
```

### Waveshaping Formula (FR-020)

```
newMag = waveshaper.process(mag * drive) / drive

Where:
- mag = inputSpectrum_.getMagnitude(bin)
- drive = effectiveDrive for this bin
- Division by drive maintains approximate unity gain
```

### Magnitude Quantization Formula (FR-024)

```
levels = 2^bits - 1
quantized = round(mag * levels) / levels

Where:
- bits = magnitudeBits_ (1.0 to 16.0)
- round() is nearest integer rounding
```

### Band Assignment (FR-022)

```
binFreq = bin * sampleRate / fftSize

if lowBand_.lowBin <= bin <= lowBand_.highBin:
    assignedBands.add(lowBand_)
if midBand_.lowBin <= bin <= midBand_.highBin:
    assignedBands.add(midBand_)
if highBand_.lowBin <= bin <= highBand_.highBin:
    assignedBands.add(highBand_)

if assignedBands.empty():
    if gapBehavior_ == Passthrough:
        effectiveDrive = 0  // bypass
    else:
        effectiveDrive = drive_  // global
else:
    effectiveDrive = max(band.drive for band in assignedBands)  // FR-023
```
