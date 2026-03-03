# Data Model: Spectral Freeze Oscillator

**Feature Branch**: `030-spectral-freeze-oscillator`
**Date**: 2026-02-06

## Entities

### SpectralFreezeOscillator

**Layer**: 2 (processors/)
**Location**: `dsp/include/krate/dsp/processors/spectral_freeze_oscillator.h`
**Namespace**: `Krate::DSP`

The primary class implementing the frozen spectral drone oscillator.

#### Configuration State (set at prepare-time, NOT real-time safe to change)

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `sampleRate_` | `double` | 0.0 | Sample rate in Hz |
| `fftSize_` | `size_t` | 0 | FFT size (power of 2, 256-8192) |
| `hopSize_` | `size_t` | 0 | Hop size = fftSize/4 |
| `numBins_` | `size_t` | 0 | Number of frequency bins = fftSize/2+1 |
| `prepared_` | `bool` | false | Whether prepare() was called successfully |

#### Frozen State (captured at freeze-time, read during processing)

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `frozenMagnitudes_` | `std::vector<float>` | numBins | Frozen magnitude spectrum (FR-007: constant) |
| `initialPhases_` | `std::vector<float>` | numBins | Captured phase at freeze time (FR-009) |
| `frozen_` | `bool` | 1 | Whether oscillator is in frozen state |

#### Phase Accumulation State (updated every synthesis hop)

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `phaseAccumulators_` | `std::vector<float>` | numBins | Running phase per bin (FR-008) |
| `phaseIncrements_` | `std::vector<float>` | numBins | Pre-computed delta_phi per bin |

#### Parameter State (set at runtime, applied on next frame boundary)

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| `pitchShiftSemitones_` | `float` | [-24, +24] | 0.0 | Pitch shift (FR-012) |
| `spectralTiltDbPerOctave_` | `float` | [-24, +24] | 0.0 | Spectral tilt (FR-016) |
| `formantShiftSemitones_` | `float` | [-24, +24] | 0.0 | Formant shift (FR-019) |

#### Processing Resources (allocated in prepare, used in process)

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `fft_` | `FFT` | - | SIMD-accelerated FFT for IFFT resynthesis |
| `workingSpectrum_` | `SpectralBuffer` | numBins | Complex spectrum for IFFT input |
| `ifftBuffer_` | `std::vector<float>` | fftSize | Time-domain IFFT output |
| `synthesisWindow_` | `std::vector<float>` | fftSize | Hann window for synthesis windowing |
| `outputBuffer_` | `std::vector<float>` | fftSize*2 | Circular overlap-add accumulator |
| `outputWriteIndex_` | `size_t` | - | Write position in output ring buffer |
| `outputReadIndex_` | `size_t` | - | Read position in output ring buffer |
| `samplesInBuffer_` | `size_t` | - | Samples available in output buffer |
| `workingMagnitudes_` | `std::vector<float>` | numBins | Scratch for pitch-shifted/tilted magnitudes |
| `captureBuffer_` | `std::vector<float>` | fftSize | Windowed input for freeze analysis |
| `analysisWindow_` | `std::vector<float>` | fftSize | Hann window for freeze analysis |

#### Formant Processing Resources (allocated if needed)

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `formantPreserver_` | `FormantPreserver` | - | Cepstral envelope extractor |
| `originalEnvelope_` | `std::vector<float>` | numBins | Envelope of frozen spectrum |
| `shiftedEnvelope_` | `std::vector<float>` | numBins | Envelope after formant shift |

#### Unfreeze Transition State

| Field | Type | Description |
|-------|------|-------------|
| `unfreezing_` | `bool` | Whether crossfade-to-silence is in progress |
| `unfadeSamplesRemaining_` | `size_t` | Samples left in unfreeze crossfade |

### FormantPreserver (existing, to be extracted)

**Current Location**: `dsp/include/krate/dsp/processors/pitch_shift_processor.h`
**New Location**: `dsp/include/krate/dsp/processors/formant_preserver.h`

No changes to the class itself. Only extracting to its own header file.

## State Transitions

```
                    prepare()
    UNPREPARED  ─────────────>  PREPARED (not frozen)
                                    │
                                    │ freeze(input, size)
                                    v
                               FROZEN (producing output)
                                    │
                                    │ unfreeze()
                                    v
                               UNFREEZING (crossfading to silence)
                                    │
                                    │ crossfade complete
                                    v
                               PREPARED (not frozen, silence)
                                    │
                                    │ freeze(input, size)  [re-freeze]
                                    v
                               FROZEN (new capture)
```

### State: UNPREPARED
- `isPrepared() == false`
- `processBlock()` outputs zeros (FR-028)
- All setters are no-ops

### State: PREPARED (not frozen)
- `isPrepared() == true`, `isFrozen() == false`
- `processBlock()` outputs zeros (FR-027)
- `freeze()` transitions to FROZEN

### State: FROZEN
- `isFrozen() == true`
- `processBlock()` produces continuous output from frozen spectrum
- Parameters (pitch, tilt, formant) applied on next frame boundary
- `freeze()` overwrites with new capture (edge case 9)
- `unfreeze()` transitions to UNFREEZING

### State: UNFREEZING
- `isFrozen() == true` (still frozen, but fading out)
- `processBlock()` produces fading output
- After `hopSize_` samples of fade, transitions to PREPARED (not frozen)

### Re-prepare
- `prepare()` called again clears all frozen state (edge case 8)
- Returns to PREPARED (not frozen)

## Validation Rules

| Parameter | Validation | Action |
|-----------|-----------|--------|
| fftSize | Not power of 2 | Clamp to nearest power of 2 in [256, 8192] |
| fftSize | Outside [256, 8192] | Clamp to range |
| pitchShiftSemitones | Outside [-24, +24] | Clamp to range |
| spectralTiltDbPerOctave | Outside [-24, +24] | Clamp to range |
| formantShiftSemitones | Outside [-24, +24] | Clamp to range |
| blockSize in freeze() | < fftSize | Zero-pad to fftSize |
| processBlock before prepare | - | Output zeros |
| processBlock when not frozen | - | Output zeros |

## Relationships

```
SpectralFreezeOscillator
    uses ─> FFT (Layer 1)           [IFFT resynthesis]
    uses ─> SpectralBuffer (Layer 1) [working spectrum storage]
    uses ─> FormantPreserver (Layer 2) [cepstral envelope extraction]
    uses ─> Window::generateHann (Layer 0) [analysis + synthesis windows]
    uses ─> expectedPhaseIncrement (Layer 1) [phase advance computation]
    uses ─> interpolateMagnitudeLinear (Layer 1) [pitch shift bin interpolation]
    uses ─> binToFrequency (Layer 1) [spectral tilt computation]
    uses ─> wrapPhaseFast (Layer 1)  [phase accumulator wrapping]
    uses ─> kPi, kTwoPi (Layer 0)   [math constants]
    uses ─> semitonesToRatio (Layer 0) [pitch/formant ratio computation]
```
