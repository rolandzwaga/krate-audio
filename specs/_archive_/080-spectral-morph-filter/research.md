# Research: Spectral Morph Filter

**Date**: 2026-01-22 | **Spec**: 080-spectral-morph-filter

## Summary

All clarification questions from the specification have been resolved. This document captures the research findings and decisions.

---

## Resolved Questions

### Q1: Bins Beyond Nyquist Handling

**Question**: When spectral shift exceeds Nyquist (e.g., +24 semitones on high-frequency content), how should bins that would exceed Nyquist be handled?

**Decision**: Zero bins (bins that exceed Nyquist are set to zero magnitude)

**Rationale**:
- Prevents aliasing artifacts that would occur from frequency folding
- Consistent with industry standard pitch shifters
- Simple and efficient to implement
- User expectation: shifted content should disappear, not wrap around

**Alternatives Considered**:
- Wrap around (frequency folding): Causes aliasing, unmusical results
- Clamp to Nyquist: Creates energy pile-up at highest bin, unnatural

---

### Q2: Phase Blend Mode

**Question**: How should "Blend" phase mode interpolate phase between sources A and B (phase wraps at 2pi, making direct angle interpolation problematic)?

**Decision**: Linear interpolation of complex vectors (interpolate real/imaginary components separately, then extract phase)

**Rationale**:
- Avoids phase wrapping discontinuities
- Produces smooth transitions between sources
- Mathematically correct for circular quantities
- Already proven pattern in SpectralDelay (complex delay line interpolation)

**Algorithm**:
```
blended_real = realA * (1 - morph) + realB * morph
blended_imag = imagA * (1 - morph) + imagB * morph
output_phase = atan2(blended_imag, blended_real)
```

**Alternatives Considered**:
- Direct phase interpolation with unwrapping: Complex, error-prone at wrap boundaries
- Shortest arc interpolation: Still has discontinuity issues at antipodal phases
- Use phase from source with higher magnitude: Loses smooth morphing characteristic

---

### Q3: Snapshot Capture Method

**Question**: How should captureSnapshot() capture the spectral fingerprint (single frame vs. averaged)?

**Decision**: Average last N frames (default 4 frames, configurable)

**Rationale**:
- Single frames are noisy and capture transient content
- Averaging produces smoother, more musical spectral fingerprints
- N=4 provides good balance between smoothness and responsiveness
- Configurable to allow user trade-off

**Implementation**:
- Accumulate magnitude over N consecutive frames
- Store phase from first frame (for stable temporal characteristics)
- Allow N to be configured via setSnapshotFrameCount()

**Alternatives Considered**:
- Single frame capture: Too noisy, captures transients
- Long averaging (16+ frames): Too slow, loses responsiveness
- Exponential averaging: More complex, harder to understand "when capture is complete"

---

### Q4: Spectral Tilt Pivot Point

**Question**: Where should the spectral tilt pivot point be placed (the frequency at which tilt = 0 dB gain)?

**Decision**: 1 kHz (industry standard)

**Rationale**:
- 1 kHz is the industry standard pivot for tilt EQs
- Perceptually centered in the frequency spectrum
- Consistent with mastering/mixing tools (Pultec, API, etc.)
- Good separation between bass and treble regions

**Implementation**:
```
freq = bin * (sampleRate / fftSize)
gain_dB = tilt * log2(freq / 1000.0)
```

**Alternatives Considered**:
- 500 Hz: Too low, bass heavy pivot
- 2 kHz: Common but less standard
- Configurable: Adds complexity, not needed for this use case

---

### Q5: Spectral Shift Fractional Bins

**Question**: How should spectral shift handle fractional bin indices when converting semitone shift to bin rotation (e.g., +7 semitones maps to bin 3.7)?

**Decision**: Nearest-neighbor rounding

**Rationale**:
- Efficient computation (single round operation)
- Preserves spectral clarity without interpolation blurring
- Appropriate for discrete frequency domain processing
- Matches phase vocoder convention

**Implementation**:
```
src_bin = static_cast<int>(output_bin / ratio + 0.5f)
```

**Alternatives Considered**:
- Linear interpolation: Blurs spectrum, adds computation
- No rounding (truncate): Systematic bias toward lower bins
- Interpolation with anti-aliasing: Overkill for morphing application

---

## Additional Research Findings

### STFT Processing Pattern

From SpectralDelay reference implementation:

1. Push samples into STFT analyzers via `pushSamples()`
2. Check `canAnalyze()` for frame availability
3. Call `analyze()` to perform windowed FFT
4. Process spectral data (morphing, shift, tilt)
5. Call `synthesize()` on OverlapAdd
6. Pull output via `pullSamples()`

### Complex Storage for Phase Preservation

SpectralDelay stores real+imaginary components separately in delay lines to avoid phase wrapping issues during linear interpolation. This pattern applies to phase blending:

- When interpolating between two phases directly, wrapping at +/-pi causes discontinuities
- Complex vector interpolation naturally handles this

### Parameter Smoothing Recommendations

From SpectralDelay: 50ms smoothing time constant works well for spectral processing parameters.

- Morph amount: Smooth at 50ms
- Spectral tilt: Smooth at 50ms
- Spectral shift: No smoothing (discrete bin operation)

### Existing Infrastructure Quality

The STFT, OverlapAdd, and SpectralBuffer components are well-tested:
- COLA compliance verified via Window::verifyCOLA()
- Phase preservation tested in SpectralDelay
- Real-time safety validated

---

## References

- SpectralDelay implementation: `dsp/include/krate/dsp/effects/spectral_delay.h`
- STFT/OverlapAdd: `dsp/include/krate/dsp/primitives/stft.h`
- SpectralBuffer: `dsp/include/krate/dsp/primitives/spectral_buffer.h`
- Window functions: `dsp/include/krate/dsp/core/window_functions.h`
