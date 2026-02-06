# Quickstart: Spectral Freeze Oscillator

**Feature Branch**: `030-spectral-freeze-oscillator`
**Date**: 2026-02-06

## What This Feature Does

The Spectral Freeze Oscillator captures a single FFT frame from any audio input and continuously resynthesizes it as a stable drone. It transforms transient audio events into infinite sustain while preserving the timbral character of the captured moment.

## Key Files

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/processors/spectral_freeze_oscillator.h` | Main implementation |
| `dsp/include/krate/dsp/processors/formant_preserver.h` | Extracted formant analysis (refactor) |
| `dsp/tests/unit/processors/spectral_freeze_oscillator_test.cpp` | Unit tests |

## Dependencies (All Existing)

| Component | Header | Layer | Usage |
|-----------|--------|-------|-------|
| FFT | `primitives/fft.h` | 1 | Forward FFT for freeze, inverse for resynthesis |
| SpectralBuffer | `primitives/spectral_buffer.h` | 1 | Working spectrum storage |
| FormantPreserver | `processors/formant_preserver.h` | 2 | Cepstral envelope extraction for formant shift |
| Window::generateHann | `core/window_functions.h` | 0 | Analysis + synthesis Hann windows |
| expectedPhaseIncrement | `primitives/spectral_utils.h` | 1 | Per-bin phase advance |
| interpolateMagnitudeLinear | `primitives/spectral_utils.h` | 1 | Pitch shift bin interpolation |
| binToFrequency | `primitives/spectral_utils.h` | 1 | Tilt frequency reference |
| wrapPhaseFast | `primitives/spectral_utils.h` | 1 | Phase wrapping |
| semitonesToRatio | `core/pitch_utils.h` | 0 | Pitch/formant ratio |
| kPi, kTwoPi | `core/math_constants.h` | 0 | Math constants |

## Build and Test

```bash
# Build
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests

# Run tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe --tag "[SpectralFreezeOscillator]"
```

## Usage Example

```cpp
#include <krate/dsp/processors/spectral_freeze_oscillator.h>

using namespace Krate::DSP;

SpectralFreezeOscillator osc;

// 1. Prepare (allocates buffers)
osc.prepare(44100.0, 2048);

// 2. Feed audio and freeze at desired moment
float audioBlock[512] = { /* ... audio data ... */ };
osc.freeze(audioBlock, 512);  // Zero-pads to fftSize internally

// 3. Generate frozen output
float output[512];
osc.processBlock(output, 512);  // Continuous drone output

// 4. Modify the frozen drone
osc.setPitchShift(12.0f);       // Octave up
osc.setSpectralTilt(-6.0f);     // 6 dB/octave darker
osc.setFormantShift(-12.0f);    // Lower formants by an octave

// 5. Release (crossfades to silence over one hop)
osc.unfreeze();
```

## Implementation Notes

> For full details on all requirements, see [spec.md](spec.md). For research rationale, see [research.md](research.md).

### Phase Coherence
The oscillator advances phase coherently per bin using `delta_phi[k] = 2*pi*k*hopSize/fftSize`. This produces stable, non-cancelling sinusoidal output rather than "smeared" random-phase artifacts. (See spec.md FR-008, FR-009.)

### Parameter Application
All parameter changes (pitch, tilt, formant) take effect on the next synthesis frame boundary (every hopSize samples). This ensures STFT-aligned transitions without zippering artifacts. (See spec.md FR-012, FR-016, FR-019.)

### Memory Budget
Total memory at 2048 FFT / 44.1 kHz: approximately 170 KB (including FormantPreserver). Target is under 200 KB (SC-008). See [research.md RQ-07](research.md#rq-07-memory-budget-analysis-sc-008-200-kb-for-2048-fft-at-441-khz) for full breakdown.

### Overlap-Add Pattern
Uses 75% overlap (hop = fftSize/4) with explicit Hann synthesis window, following the AdditiveOscillator pattern. Custom overlap-add ring buffer for output accumulation (not the existing `OverlapAdd` class). (See spec.md FR-010, research.md RQ-03.)

### Formant Preservation
FormantPreserver uses cepstral analysis: log-magnitude -> IFFT -> low-pass lifter (Hann, ~1.5ms quefrency) -> FFT -> exponentiate. Lifter cutoff computed as `lifterBins = static_cast<size_t>(0.0015 * sampleRate)`. (See spec.md FR-021, FR-022.)

## Requirement Coverage Map

| Requirement Group | FRs | Implementation Location |
|------------------|------|------------------------|
| Lifecycle | FR-001 to FR-003 | `prepare()`, `reset()`, `isPrepared()` |
| Freeze/Unfreeze | FR-004 to FR-007 | `freeze()`, `unfreeze()`, `isFrozen()` |
| Phase/Resynthesis | FR-008 to FR-011 | `processBlock()` inner loop |
| Pitch Shift | FR-012 to FR-015 | `applyPitchShift()` private method |
| Spectral Tilt | FR-016 to FR-018 | `applySpectralTilt()` private method |
| Formant Shift | FR-019 to FR-022 | `applyFormantShift()` private method |
| Real-Time Safety | FR-023 to FR-025 | All methods noexcept, pre-allocated buffers |
| Query | FR-026 to FR-028 | `getLatencySamples()`, silence output guards |
