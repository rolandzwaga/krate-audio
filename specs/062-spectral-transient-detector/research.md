# Research: Spectral Transient Detector

**Feature**: 062-spectral-transient-detector
**Date**: 2026-02-17

## Research Tasks

### R1: Half-Wave Rectified Spectral Flux Algorithm

**Decision**: Implement the standard half-wave rectified spectral flux onset detection function as described by Duxbury et al. (2002) and Dixon (2006).

**Rationale**: Half-wave rectification (HWR) is the consensus approach in onset detection literature because it isolates energy onsets (positive magnitude changes) from energy decay (negative changes). Full spectral flux (absolute differences) produces false positives on note releases and amplitude modulation. The algorithm is a single linear pass over the magnitude array with no transcendental math, making it trivially real-time safe.

**Formula**:
```
SF(n) = sum(max(0, |X_n[k]| - |X_{n-1}[k]|)) for k = 0..numBins-1
```

**Alternatives considered**:
- Full spectral flux (sum of absolute differences): Rejected -- produces false positives on note releases
- Complex-domain spectral flux (uses phase): Rejected -- requires phase information, adds complexity, marginal benefit for phase vocoder integration where we already have magnitudes
- Spectral difference with weighting (band-weighted flux): Rejected -- adds tunable complexity without proportional benefit for the initial implementation; can be added later if needed

### R2: Adaptive Threshold Design

**Decision**: Use exponential moving average (EMA) of past flux values as the threshold baseline, with a configurable multiplier. Include a minimum floor to prevent numerical instability after silence.

**Rationale**: Fixed thresholds fail across varying loudness levels. An adaptive threshold based on recent history automatically adjusts to the signal's energy dynamics. The EMA is computationally trivial (one multiply and one add per frame) and well-established in onset detection literature.

**Formula**:
```
runningAvg(n) = alpha * runningAvg(n-1) + (1 - alpha) * SF(n)
transient = SF(n) > threshold * runningAvg(n)
```

**Parameters**:
- `alpha` (smoothing coefficient): Default 0.95, range [0.8, 0.99]. Higher values give more historical context.
- `threshold` (multiplier): Default 1.5, range [1.0, 5.0]. Higher values reduce sensitivity.
- Minimum floor on running average: 1e-10f to prevent division by zero or numerically unstable comparisons.

**Alternatives considered**:
- Median-based threshold (local median of flux history): Rejected -- requires maintaining a sorted buffer, adds memory and complexity, marginal improvement for spectral-domain detection
- Fixed threshold: Rejected -- fails for varying loudness
- Peak-picking with lookback window: Rejected -- adds latency (need to wait for post-peak samples), unnecessary for frame-rate detection

### R3: First-Frame Suppression Strategy

**Decision**: Suppress transient detection on the very first `detect()` call after `prepare()` or `reset()`, always returning `false`. Seed the running average from the first frame's flux.

**Rationale**: After `prepare()` or `reset()`, the previous magnitude buffer is all zeros. The first frame will produce artificially high flux (all magnitude increases from zero), causing a guaranteed false positive. Suppression eliminates this predictable artifact while still using the first frame's flux to seed the running average for meaningful subsequent comparisons.

**Alternatives considered**:
- No suppression (report the false positive): Rejected -- predictable false positive on every cold start is undesirable
- Initialize previous magnitudes from first frame before detection: Rejected -- would require a separate "priming" call, complicating the API
- Suppress first N frames: Rejected -- only frame 0 has the artificial inflation; frame 1 already has valid previous magnitudes

### R4: Internal Storage Design

**Decision**: Use `std::vector<float>` for the previous magnitudes buffer, allocated in `prepare()` and never reallocated in `detect()`. Use a `bool` flag to track the first-frame state.

**Rationale**: `std::vector` provides contiguous memory for cache-friendly linear iteration. Allocation only happens in `prepare()`, meeting real-time safety requirements. A simple `bool` flag is sufficient for first-frame suppression -- no complex state machine needed.

**Storage**:
- `prevMagnitudes_`: `std::vector<float>` sized to `numBins` in `prepare()`
- `runningAverage_`: single `float`, initialized to 0.0f
- `threshold_`: single `float`, default 1.5f
- `smoothingCoeff_`: single `float`, default 0.95f
- `transientDetected_`: `bool`, the most recent detection result
- `lastFlux_`: `float`, the most recent spectral flux value
- `isFirstFrame_`: `bool`, suppression flag
- `numBins_`: `size_t`, the bin count from `prepare()`

**Memory footprint**: For a 4096-point FFT (2049 bins), the previous magnitudes buffer is 2049 * 4 = ~8KB. Total detector state is under 10KB.

### R5: Phase Reset Integration Pattern

**Decision**: Add a `SpectralTransientDetector` member to `PhaseVocoderPitchShifter`, invoke `detect()` in `processFrame()` after magnitude extraction, and reset synthesis phases to analysis phases when a transient is detected. Provide an independent enable/disable toggle.

**Rationale**: The phase reset technique (Duxbury et al. 2002, Roebel 2003) is the simplest effective approach for preserving transient sharpness in phase vocoders. At transient frames, setting `synthPhase_[k] = analysisPhase[k]` breaks the phase accumulation to prevent temporal smearing. This integrates cleanly with the existing code structure:
1. `magnitude_[k]` is already computed at line 1127-1130 of `processFrame()`
2. `prevPhase_[k]` contains the analysis phases
3. `synthPhase_[k]` is the reset target

The toggle is independent of `phaseLockingEnabled_` per FR-013.

**Integration point in processFrame()**: After Step 1 (magnitude/frequency extraction, line 1146) and before Step 1c (phase locking setup, line 1154). The detector consumes `magnitude_[]` and, if transient detected, resets `synthPhase_[]` to `prevPhase_[]` before any further phase computation.

**Alternatives considered**:
- Per-bin transient detection (classify individual bins as transient): Rejected -- significantly more complex, marginal benefit for initial implementation
- Roebel's center-of-gravity method: Rejected -- more sophisticated but adds substantial complexity without proportional benefit
- Soft phase reset (blend between accumulated and analysis phases): Interesting for future work but unnecessary for initial implementation where binary reset is the established approach

### R6: Bin Count Mismatch Handling

**Decision**: Debug assertion in debug builds, silent clamping in release builds. Process only `min(passedCount, preparedCount)` bins.

**Rationale**: A bin count mismatch is a programming error (caller passed wrong buffer). In debug builds, a `KRATE_DSP_ASSERT` (or equivalent `assert()`) immediately surfaces the bug. In release builds, clamping to the smaller count prevents out-of-bounds access while still producing a valid (though potentially degraded) result. This follows the defensive coding pattern used elsewhere in the codebase.

**Alternatives considered**:
- Return false immediately (no processing): Rejected -- loses the flux computation for the frame
- Undefined behavior: Rejected -- constitution forbids it
- Throw exception: Rejected -- forbidden on audio thread

### R7: Layer Placement Verification

**Decision**: Layer 1 (Primitives). The SpectralTransientDetector depends only on `<vector>`, `<cstddef>`, `<cmath>`, `<cassert>`, and `<algorithm>` from the standard library. No Layer 0 utility headers are strictly needed (the algorithm is self-contained math). No Layer 1+ dependencies.

**Rationale**: Layer 1 primitives are self-contained building blocks that depend only on Layer 0 and the standard library. The detector is a stateful frame-level processor operating on magnitude arrays -- exactly the pattern of other Layer 1 primitives like `SpectralBuffer`, `PitchDetector`, and `DelayLine`. It does not compose other DSP components (which would make it Layer 2+).

**Alternatives considered**:
- Layer 0 (Core): Rejected -- the detector is stateful (stores previous magnitudes), which is characteristic of Layer 1 primitives, not stateless Layer 0 utilities
- Layer 2 (Processors): Rejected -- the detector has no Layer 1 dependencies beyond stdlib; Layer 2 is for components that compose Layer 1 primitives

## Summary

All NEEDS CLARIFICATION items from the Technical Context have been resolved. The algorithm is well-established (Duxbury 2002, Dixon 2006), the integration point is clearly identified in `PhaseVocoderPitchShifter::processFrame()`, and all edge cases (first frame, silence, bin mismatch) have defined handling strategies. No external library dependencies are needed -- the entire detector is approximately 80 lines of straightforward C++ math.
