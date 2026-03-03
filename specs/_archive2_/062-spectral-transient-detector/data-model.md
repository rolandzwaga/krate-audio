# Data Model: Spectral Transient Detector

**Feature**: 062-spectral-transient-detector
**Date**: 2026-02-17

## Entities

### SpectralTransientDetector

**Layer**: 1 (Primitives)
**Location**: `dsp/include/krate/dsp/primitives/spectral_transient_detector.h`
**Namespace**: `Krate::DSP`

A stateful frame-level onset detector that operates on magnitude spectra. Computes half-wave rectified spectral flux per frame and compares against an adaptive threshold derived from an exponential moving average of past flux values.

#### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `prevMagnitudes_` | `std::vector<float>` | (empty) | Previous frame's magnitude values. Sized to `numBins` in `prepare()`. |
| `runningAverage_` | `float` | `0.0f` | Exponential moving average of spectral flux. Updated each `detect()` call. |
| `threshold_` | `float` | `1.5f` | Multiplier applied to running average for detection threshold. Range [1.0, 5.0]. |
| `smoothingCoeff_` | `float` | `0.95f` | EMA smoothing coefficient (alpha). Range [0.8, 0.99]. |
| `transientDetected_` | `bool` | `false` | Most recent detection result. |
| `lastFlux_` | `float` | `0.0f` | Most recent raw spectral flux value. |
| `isFirstFrame_` | `bool` | `true` | Suppresses detection on first frame after `prepare()`/`reset()`. |
| `numBins_` | `std::size_t` | `0` | Bin count from most recent `prepare()` call. |

#### Validation Rules

- `threshold_` is clamped to [1.0, 5.0] in `setThreshold()`
- `smoothingCoeff_` is clamped to [0.8, 0.99] in `setSmoothingCoeff()`
- `numBins_` must be > 0 after `prepare()` for `detect()` to execute
- Bin count passed to `detect()` must match `numBins_`; debug assert on mismatch, release clamp

#### State Transitions

```
[Unprepared] --prepare(numBins)--> [Ready, FirstFrame]
[Ready, FirstFrame] --detect()--> [Ready, Active]  (suppressed, seeds EMA)
[Ready, Active] --detect()--> [Ready, Active]       (normal detection)
[Ready, *] --reset()--> [Ready, FirstFrame]          (clears state, keeps allocation)
[Ready, *] --prepare(newBins)--> [Ready, FirstFrame] (reallocates if size changed)
```

### Modifications to PhaseVocoderPitchShifter

**Location**: `dsp/include/krate/dsp/processors/pitch_shift_processor.h`

New member fields added to `PhaseVocoderPitchShifter`:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `transientDetector_` | `SpectralTransientDetector` | (default-constructed) | Spectral flux onset detector instance. |
| `phaseResetEnabled_` | `bool` | `false` | Independent toggle for transient-aware phase reset. |

## Relationships

```
SpectralTransientDetector (Layer 1)
    ^
    | consumed by (member of)
    |
PhaseVocoderPitchShifter (Layer 2)
    ^
    | contained by (pImpl)
    |
PitchShiftProcessor (Layer 2, public API)
```

- `SpectralTransientDetector` is a standalone Layer 1 primitive with no upward dependencies.
- `PhaseVocoderPitchShifter` creates and owns a `SpectralTransientDetector` instance.
- `PitchShiftProcessor` (the public API wrapper) accesses the phase vocoder through its pImpl.
- The detector consumes the `magnitude_[]` array already computed in `processFrame()`.
- Phase reset modifies `synthPhase_[]` before it is used in pitch shift computation.

## Memory Layout

| Component | Size (4096-point FFT) | Allocation Timing |
|-----------|----------------------|-------------------|
| `prevMagnitudes_` (2049 floats) | 8,196 bytes | `prepare()` |
| `std::vector` object header (pointer + size + capacity) | ~24 bytes (64-bit) | Construction |
| 7 scalar fields: `runningAverage_`, `threshold_`, `smoothingCoeff_`, `lastFlux_` (4 floats = 16 bytes), `transientDetected_`, `isFirstFrame_` (2 bools = 2 bytes), `numBins_` (size_t = 8 bytes) | ~26 bytes | Construction |
| **Total per detector** | **~8.2 KB** | |

All heap memory is allocated in `prepare()`. The `detect()` path performs zero allocations.
