# Research: Additive Synthesis Oscillator

**Feature Branch**: `025-additive-oscillator`
**Date**: 2026-02-05
**Status**: Complete

## Executive Summary

This document captures research findings for implementing an IFFT-based additive synthesis oscillator. All technical unknowns have been resolved, and existing codebase components have been identified for reuse.

---

## Research Task 1: IFFT-Based Additive Synthesis Algorithm

### Question
How to efficiently synthesize partials using IFFT and overlap-add?

### Findings

**Decision**: Use the existing `FFT::inverse()` and `OverlapAdd` infrastructure from `primitives/stft.h` with a custom spectrum construction phase.

**Algorithm Overview**:
1. **Spectrum Construction** (per hop): For each active partial, calculate its frequency (with inharmonicity), amplitude (with spectral tilt), and accumulated phase. Place complex values at the appropriate FFT bin.
2. **IFFT**: Transform spectrum to time domain using `FFT::inverse()`
3. **Windowing**: Apply Hann window to IFFT output for COLA reconstruction
4. **Overlap-Add**: Use `OverlapAdd` pattern to accumulate windowed frames

**Key Insight**: Unlike STFT analysis which applies window before FFT, IFFT synthesis applies window AFTER IFFT. The existing `OverlapAdd` class applies COLA normalization but NOT a synthesis window, which is correct for analysis-synthesis roundtrip. For pure synthesis (no analysis), we need to apply the synthesis window ourselves.

**Rationale**:
- Reuses battle-tested FFT implementation
- O(N log N) complexity independent of partial count
- Existing window functions and COLA infrastructure available

**Alternatives Considered**:
- Direct oscillator bank: O(M * P) where M=samples, P=partials. More CPU at high partial counts.
- PFFFT library: Would require new dependency. Our FFT is sufficient for synthesis.

### Sources
- Existing `FFT::inverse()` in `dsp/include/krate/dsp/primitives/fft.h`
- Existing `OverlapAdd` in `dsp/include/krate/dsp/primitives/stft.h`
- CCRMA Stanford: Inverse FFT Synthesis

---

## Research Task 2: Phase Accumulation for IFFT Synthesis

### Question
How to maintain phase continuity across IFFT frames?

### Findings

**Decision**: Use double-precision phase accumulators per partial, advancing by `frequency * hopTime` each frame.

**Algorithm**:
```cpp
// Per frame, for each partial i:
double freq = partialFrequency_[i];  // Already includes inharmonicity
double phaseIncrement = freq * hopSize_ / sampleRate_;
accumulatedPhase_[i] = wrapPhase(accumulatedPhase_[i] + phaseIncrement);
```

**Key Points**:
- Phase is accumulated in normalized units [0, 1) where 1.0 = 2*pi radians
- `wrapPhase()` from `phase_utils.h` handles wrapping
- Initial phases from `setPartialPhase()` are applied at `reset()` only
- Double precision prevents drift over long playback (matches existing oscillator patterns)

**Rationale**:
- Matches pattern in `PhaseAccumulator` struct from `phase_utils.h`
- Double precision already used in codebase oscillators
- Per-partial tracking is necessary since each partial has a different frequency

**Alternatives Considered**:
- Store phase in radians: More memory, more conversion overhead
- Single precision: Could drift over very long playback

---

## Research Task 3: Spectral Tilt Implementation

### Question
How to apply dB/octave spectral tilt to partials?

### Findings

**Decision**: Apply tilt as an amplitude modifier per partial using the formula from FR-015.

**Formula** (spec-defined):
```cpp
float tiltFactor = std::pow(10.0f, tiltDb_ * std::log2(static_cast<float>(n)) / 20.0f);
float effectiveAmplitude = partialAmplitude_[i] * tiltFactor;
```

Where `n` is the 1-based partial number (n=1 for fundamental).

**Implementation Notes**:
- For partial 1 (fundamental): log2(1) = 0, so tiltFactor = 1.0 (no change)
- For partial 2: tiltFactor = 10^(tiltDb * 1 / 20) = 10^(tiltDb/20)
- At -6 dB/octave: partial 2 is 6 dB quieter than partial 1

**Rationale**:
- Formula matches spec FR-015 exactly
- dB/octave is the standard unit for brightness control
- Preserves user-set amplitudes; tilt is applied as a modifier

**Alternatives Considered**:
- Pre-compute tilt factors: Could cache for performance, but at max 128 partials the runtime cost is minimal
- Use existing SpectralTilt processor: That's time-domain filtering, not applicable here

---

## Research Task 4: Inharmonicity Formula

### Question
How to calculate stretched partial frequencies for bell/piano timbres?

### Findings

**Decision**: Use the piano string inharmonicity formula from FR-017.

**Formula** (spec-defined):
```cpp
float stretchedFreq = n * fundamental_ * std::sqrt(1.0f + B_ * n * n);
```

Where:
- `n` is the 1-based partial number
- `B` is the inharmonicity coefficient [0, 0.1]
- For B=0: stretchedFreq = n * fundamental (pure harmonics)

**Example Calculations** (for verification):
- f1 at 440 Hz, B=0.001, partial 10: 10 * 440 * sqrt(1 + 0.001 * 100) = 4400 * sqrt(1.1) = 4614.5 Hz
- f1 at 100 Hz, B=0.01, partial 5: 5 * 100 * sqrt(1 + 0.01 * 25) = 500 * sqrt(1.25) = 559.0 Hz

**Rationale**:
- Standard formula from acoustic physics research
- B range [0, 0.1] covers typical piano to bell sounds
- Formula in spec is well-documented with references

**Alternatives Considered**:
- Custom frequency ratio per partial: Already supported via `setPartialFrequencyRatio()`
- Fixed stretched tuning tables: Less flexible than parametric formula

---

## Research Task 5: FFT Bin Placement for Partials

### Question
How to place partial amplitudes/phases into FFT bins?

### Findings

**Decision**: Use nearest-bin placement with rounding.

**Algorithm**:
```cpp
float partialFreq = calculatePartialFrequency(i);  // With inharmonicity
if (partialFreq >= nyquist_) continue;  // Skip above Nyquist

size_t bin = static_cast<size_t>(std::round(partialFreq * fftSize_ / sampleRate_));
if (bin >= numBins_) continue;  // Safety check

// Set bin from polar form
float phase = static_cast<float>(accumulatedPhase_[i]) * kTwoPi;
spectrum_[bin].real = amplitude * std::cos(phase);
spectrum_[bin].imag = amplitude * std::sin(phase);
```

**Key Points**:
- Partials above Nyquist are excluded (FR-021)
- Multiple partials could map to same bin (rare, handled by summing)
- Bin 0 is DC (not used for partials)
- Bin N/2 is Nyquist (excluded)

**Rationale**:
- Nearest-bin is simple and efficient
- At FFT size 2048, bin resolution is ~21.5 Hz at 44.1kHz (adequate for synthesis)
- More sophisticated interpolation (sinc, etc.) adds complexity with minimal benefit for synthesis

**Alternatives Considered**:
- Frequency-domain interpolation (sinc): More accurate but computationally expensive
- Phase vocoder style: Overkill for synthesis-only use case

---

## Research Task 6: Overlap-Add Parameters for Synthesis

### Question
What hop size and window should be used for synthesis?

### Findings

**Decision**: Use 75% overlap (hop = FFT_size / 4) with Hann window per FR-019 and FR-020.

**Configuration**:
- FFT sizes: 512, 1024, 2048 (default), 4096 (FR-002)
- Hop size: FFT_size / 4 (e.g., 512 for FFT_size=2048)
- Window: Hann (periodic variant, already in `window_functions.h`)

**COLA Verification**:
The Hann window at 75% overlap (hop = N/4) satisfies COLA because:
- Sum of 4 overlapping Hann windows = constant
- This is stricter than required (50% overlap would also work)
- 75% overlap provides smoother output and better transient handling

**Rationale**:
- Spec requirement FR-020 mandates 75% overlap
- Hann window already implemented and COLA-verified
- More overlap = smoother output, acceptable CPU trade-off

**Alternatives Considered**:
- 50% overlap: Fewer FFTs per second but more potential for artifacts
- Different windows (Blackman, Kaiser): No benefit for synthesis; Hann is optimal for COLA

---

## Research Task 7: Latency and Buffering Strategy

### Question
How to manage latency and sample buffering?

### Findings

**Decision**: Latency equals FFT size (FR-004), use circular output buffer for sample management.

**Buffer Strategy**:
```
Internal State:
- spectrum_[numBins_]: Complex spectrum buffer (N/2+1 bins)
- ifftBuffer_[fftSize_]: Time-domain IFFT output
- outputBuffer_[fftSize_ * 2]: Circular output accumulator
- outputReadIndex_: Position in output buffer
- samplesInBuffer_: Available samples to output
```

**Processing Flow**:
1. When processBlock() is called, check if more samples needed
2. If outputBuffer has enough samples, copy to output and return
3. If not, synthesize new frames until buffer has enough:
   - Construct spectrum from current partial state
   - IFFT to ifftBuffer
   - Window ifftBuffer (Hann)
   - Overlap-add to outputBuffer
   - Advance phase accumulators
4. Copy requested samples to output

**Rationale**:
- Latency disclosure via `latency()` method allows host compensation
- Circular buffer avoids allocations during processing
- Pattern matches existing OverlapAdd class design

**Alternatives Considered**:
- External buffering by caller: Violates encapsulation, error-prone
- Zero-latency mode: Not possible with block-based IFFT synthesis

---

## Research Task 8: Output Amplitude and Clamping

### Question
How to handle output amplitude when many partials are active?

### Findings

**Decision**: No automatic normalization. Direct sum of partials with hard clamp to [-2, +2] per FR-022.

**Implementation**:
```cpp
// After IFFT and windowing, before output:
for (size_t i = 0; i < fftSize_; ++i) {
    ifftBuffer_[i] = std::clamp(ifftBuffer_[i], -2.0f, 2.0f);
}
```

**Rationale** (from spec clarifications):
- Auto-normalization destroys intentional density/energy
- Makes amplitude context-dependent (breaks presets/automation)
- Direct sum is honest DSP
- Energy growth with partial count is expected behavior
- Clamping prevents NaN/Inf without hiding sonic consequences

**User Responsibility**:
- Users must manage gain staging externally
- With 128 partials at amplitude 1.0, output could peak at ~128x (before windowing)
- Typical usage: partial amplitudes sum to ~1.0 total

---

## Codebase Components to Reuse

| Component | Location | How Used |
|-----------|----------|----------|
| FFT | `primitives/fft.h` | IFFT via `inverse()` method |
| Complex | `primitives/fft.h` | Spectrum bin storage |
| SpectralBuffer | `primitives/spectral_buffer.h` | Reference for spectrum management |
| Window::generate() | `core/window_functions.h` | Hann window generation |
| Window::generateHann() | `core/window_functions.h` | In-place window application |
| wrapPhase() | `core/phase_utils.h` | Phase accumulator wrapping |
| calculatePhaseIncrement() | `core/phase_utils.h` | Not directly used (we compute per-frame) |
| detail::isNaN() | `core/db_utils.h` | Input sanitization |
| detail::isInf() | `core/db_utils.h` | Input sanitization |
| kTwoPi, kPi | `core/math_constants.h` | Phase calculations |

---

## New Components Required

| Component | Location | Purpose |
|-----------|----------|---------|
| AdditiveOscillator | `processors/additive_oscillator.h` | Main class per spec |
| AdditiveOscillator tests | `tests/unit/processors/additive_oscillator_test.cpp` | Unit tests |

---

## Open Questions Resolved

All technical unknowns from the spec have been resolved. No NEEDS CLARIFICATION items remain.

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Spectral leakage at non-integer bin frequencies | Medium | Low | Accept nearest-bin; 21.5 Hz resolution is adequate for musical purposes |
| Phase drift over very long playback | Low | Low | Double precision accumulation prevents meaningful drift |
| CPU spikes with many partials + small blocks | Low | Medium | O(N log N) scales well; document performance characteristics |
| Memory usage with large FFT sizes | Low | Low | Max ~64KB per oscillator instance (acceptable) |
