# Research: Spectral Freeze Oscillator

**Feature Branch**: `030-spectral-freeze-oscillator`
**Date**: 2026-02-06

## Research Questions & Findings

### RQ-01: How should the OverlapAdd class be used for synthesis-only mode (no analysis input)?

**Initial Decision**: ~~Use the existing `OverlapAdd` class directly for IFFT + overlap-add output.~~

**SUPERSEDED by RQ-03**: After deeper analysis, the existing `OverlapAdd` class assumes analysis-only windowing and does NOT apply a synthesis window. For synthesis-only mode (frozen spectrum resynthesis), an explicit Hann synthesis window must be applied to each IFFT output frame. **Final decision: use a custom overlap-add ring buffer following the `AdditiveOscillator` pattern.** See RQ-03 for full rationale.

**Key finding**: The existing OverlapAdd uses `colaNormalization_` factor but does NOT apply a synthesis window (only COLA normalization via the factor computed from the window sum). For synthesis-only mode, the AdditiveOscillator applies its own Hann window in `synthesizeFrame()` and uses a custom ring buffer. This is the correct pattern for SpectralFreezeOscillator.

### RQ-02: Can the existing FormantPreserver class be reused as-is for formant shifting?

**Decision**: Yes. The `FormantPreserver` class in `processors/pitch_shift_processor.h` provides the complete cepstral envelope extraction pipeline needed for FR-021 and FR-022. It can be used directly without modification.

**Rationale**: FormantPreserver provides:
- `prepare(fftSize, sampleRate)` -- allocates all buffers, computes quefrency cutoff (default 1.5ms)
- `extractEnvelope(magnitudes, outputEnvelope)` -- full cepstral analysis (log-mag, IFFT, lifter, FFT, exp)
- `applyFormantPreservation(shifted, origEnv, shiftedEnv, output, numBins)` -- envelope ratio application

These match FR-021 (cepstral envelope extraction) and FR-022 (envelope ratio application) exactly.

**Alternatives considered**:
- Extract FormantPreserver to its own header (`primitives/formant_preserver.h`): Would improve modularity but constitutes a refactor. Defer to implementation -- if extraction is straightforward with no test breakage, do it; otherwise just include `pitch_shift_processor.h`.
- Implement custom cepstral analysis: Violates Principle XIV (no duplication).

**Key finding**: FormantPreserver's quefrency default is 1.5ms, which matches the spec's FR-021 requirement. The lifter uses a Hann window. The class allocates its own FFT internally, so each FormantPreserver instance adds one FFT setup (~16KB for 2048).

**Refactoring opportunity**: The FormantPreserver is currently defined in `pitch_shift_processor.h` alongside many other classes. Extracting it to its own header would be a clean, low-risk refactor that improves reusability. The SpectralFreezeOscillator would then include `<krate/dsp/processors/formant_preserver.h>` (or even move it to `primitives/` if no Layer 2 deps). Since FormantPreserver only depends on `FFT` (Layer 1), it could live at Layer 1. However, to minimize risk of breaking existing code, we will keep it at Layer 2 in a separate header.

### RQ-03: What is the correct COLA normalization for Hann window at 75% overlap?

**Decision**: Use the existing OverlapAdd class which computes the COLA normalization automatically. For a periodic Hann window at 75% overlap (hopSize = fftSize/4), the COLA sum is approximately 1.5, so the normalization factor is approximately 1/1.5 = 0.667.

**Rationale**: The OverlapAdd::prepare() method computes: `colaNormalization_ = 1.0f / colaSum` where `colaSum` is the sum of overlapping window samples at a single position. This is correct for analysis-synthesis with analysis-only windowing (window applied in analysis, not synthesis).

**Key finding**: The OverlapAdd class does NOT apply a synthesis window -- it only applies `colaNormalization_` scaling. This means we need to be careful: since our SpectralFreezeOscillator is doing synthesis-only (no analysis windowing was applied to the frozen spectrum), we need to understand what normalization is appropriate. Looking at the AdditiveOscillator, it applies a Hann window to the IFFT output AND does its own overlap-add without OverlapAdd. That approach applies the window explicitly for smooth transitions between frames.

**Resolution**: Apply the Hann synthesis window explicitly in our implementation before adding to the overlap-add buffer, similar to AdditiveOscillator. Use a custom overlap-add ring buffer (like AdditiveOscillator) rather than the OverlapAdd class, since the OverlapAdd class assumes analysis-only windowing was already applied. The synthesis window ensures smooth frame transitions without clicks, and the COLA sum for Hann at 75% overlap is 1.5, so divide by 1.5.

**Updated decision**: Implement a custom overlap-add output buffer similar to AdditiveOscillator. This avoids confusion about whether synthesis windowing was already applied.

### RQ-04: How should phase advancement work after pitch shifting (FR-014)?

**Decision**: After pitch shifting, each destination bin k should advance its phase at the rate corresponding to that bin's center frequency: `delta_phi[k] = 2 * pi * k * hopSize / fftSize`. This is independent of the pitch shift ratio.

**Rationale**: The coherent phase advancement formula from FR-008 depends on the destination bin index, not the source bin. When we shift bin 100 to bin 200 via pitch shifting, the resynthesized sinusoid at bin 200 should advance at bin 200's center frequency. This produces a coherent output where each bin's sinusoidal component is stable.

**Alternatives considered**:
- Use source bin's frequency for phase advancement: Would produce incoherent output at wrong frequencies.
- Track instantaneous frequency from original capture: Over-complicated for frozen static spectrum.

**Key finding**: The existing `expectedPhaseIncrement(binIndex, hopSize, fftSize)` in spectral_utils.h returns exactly `kTwoPi * binIndex * hopSize / fftSize`, which is the formula needed. The phase accumulator array stores one phase per destination bin (N/2+1 entries). On each synthesis hop, we compute the working spectrum from frozen magnitudes (with pitch shift, tilt, formant shift) and accumulated phases, then advance all phases.

### RQ-05: What is the spectral tilt formula for frequency-domain application (FR-017)?

**Decision**: Use the formula from the spec: `gain_dB = tilt * log2(f_k / f_ref)` where `f_ref` is the frequency of bin 1. Apply as a multiplicative gain to magnitudes. Clamp resulting magnitudes to [0, 2.0] per FR-018.

**Rationale**: This is consistent with the spectral tilt approach used in `SpectralMorphFilter::applySpectralTilt()`, except with a different pivot point. The morph filter uses 1 kHz as pivot; the freeze oscillator uses bin 1's frequency as the reference. This matches the spec exactly.

**Key finding**: The existing `dbToGain()` function in `db_utils.h` could be used but is implemented using `constexprPow10(dB / 20.0f)` which is a Taylor series approximation. For real-time tilt computation, `std::pow(10.0f, gainDb / 20.0f)` is used in the morph filter. Since tilt is only recomputed once per synthesis frame (not per sample), the cost is negligible -- use `std::pow` directly for accuracy.

**Implementation detail**: The reference frequency `f_ref = binToFrequency(1, fftSize, sampleRate) = sampleRate / fftSize`. For bin 0 (DC), skip tilt (gain = 1.0).

### RQ-06: How should the unfreeze crossfade work (FR-005)?

**Decision**: When `unfreeze()` is called, set a flag and a sample counter for the crossfade. Over the next `hopSize` samples of output, linearly ramp the output amplitude from 1.0 to 0.0. After the crossfade completes, output silence.

**Rationale**: The spec says "crossfade to zero over one hop duration (fftSize/4 samples)." This is a simple linear gain ramp applied to the output samples during pullSamples(). No complex spectral manipulation needed.

**Implementation**:
- `unfreeze()` sets `unfreezing_ = true`, `unfadeSamplesRemaining_ = hopSize_`
- During `processBlock()`, when `unfreezing_`, multiply each output sample by `unfadeSamplesRemaining_ / hopSize_` and decrement counter
- When counter reaches 0, set `frozen_ = false`, `unfreezing_ = false`

### RQ-07: Memory budget analysis (SC-008: 200 KB for 2048 FFT at 44.1 kHz)

**Decision**: The design fits within the 200 KB budget.

**Analysis** (all values for fftSize=2048, numBins=1025):

| Buffer | Size (bytes) | Purpose |
|--------|-------------|---------|
| Frozen magnitudes | 1025 * 4 = 4,100 | FR-007: Static magnitude snapshot |
| Frozen phases (initial) | 1025 * 4 = 4,100 | FR-009: Initial phase for resynthesis |
| Phase accumulators | 1025 * 4 = 4,100 | FR-008: Running phase per bin |
| Working spectrum (SpectralBuffer) | 1025 * 8 = 8,200 | Complex buffer for IFFT input |
| IFFT output buffer | 2048 * 4 = 8,192 | Time-domain frame from IFFT |
| Hann window | 2048 * 4 = 8,192 | Synthesis window coefficients |
| Output ring buffer | 4096 * 4 = 16,384 | Overlap-add accumulator (2x fftSize) |
| FFT internals (3 aligned buffers) | 3 * 2048 * 4 = 24,576 | pffft staging + work buffers |
| FFT setup (pffft) | ~512 | pffft internal tables |
| Temporary magnitude array | 1025 * 4 = 4,100 | For tilt/shift computation |
| FormantPreserver (when used) | ~70,000 | FFT + 5 vectors of ~numBins or fftSize |
| Formant envelope arrays (2x) | 2 * 1025 * 4 = 8,200 | Original + shifted envelope |
| Input capture buffer | 2048 * 4 = 8,192 | For freeze() windowing |
| Analysis window | 2048 * 4 = 8,192 | Hann window for freeze analysis |
| **Total without formant** | **~90 KB** | Well within budget |
| **Total with formant** | **~170 KB** | Within 200 KB budget |

**Rationale**: 170 KB with all formant buffers is within the 200 KB target. The FormantPreserver is the largest single component (~70 KB) due to its internal FFT instance and work buffers. Without formant shifting, the oscillator uses only ~90 KB.

### RQ-08: Should FormantPreserver be extracted to its own header?

**Decision**: Yes, extract to `dsp/include/krate/dsp/processors/formant_preserver.h`. This is a low-risk refactor that improves modularity.

**Rationale**: FormantPreserver is currently defined in `pitch_shift_processor.h` (a 1600-line file). It depends only on `FFT` (Layer 1) and `math_constants.h` (Layer 0). Extracting it to its own file:
1. Allows SpectralFreezeOscillator to include only what it needs (not the entire pitch shift processor)
2. Makes the FormantPreserver discoverable as a standalone utility
3. Does not break any existing code (pitch_shift_processor.h would `#include` the new header)

**Implementation plan**:
1. Create `processors/formant_preserver.h` with the FormantPreserver class
2. In `pitch_shift_processor.h`, replace the class definition with `#include <krate/dsp/processors/formant_preserver.h>`
3. Run all existing tests to verify no breakage
4. Update CMakeLists.txt to list the new header for IDE visibility

### RQ-09: SIMD viability for spectral freeze oscillator

**Decision**: NOT BENEFICIAL for SIMD optimization at this time.

**Reasoning**:
1. The hot path is once-per-hop (every 512 samples for hopSize=512), not per-sample
2. The per-hop work is N/2+1 = 1025 iterations of simple math (multiply, add, cos, sin)
3. The FFT itself (via pffft) is already SIMD-optimized
4. The sin/cos calls in spectrum construction dominate CPU time, and these are transcendental functions that don't benefit from standard SIMD (would need fast_sin approximation instead)
5. SC-003 target is 0.5% CPU, which is easily achievable with scalar code given the once-per-hop processing cadence

**Alternative optimizations**:
- Pre-compute phase increment array once at parameter change (already planned)
- Skip zero-magnitude bins in spectrum construction
- Use fast sin/cos approximations if profiling shows transcendentals are the bottleneck

## Technology Decisions Summary

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Overlap-add pattern | Custom ring buffer (like AdditiveOscillator) | Need explicit synthesis window control |
| Formant extraction | Reuse FormantPreserver | Existing cepstral pipeline, Principle XIV |
| FormantPreserver location | Extract to own header | Modularity, discoverability |
| Phase advancement | Per-bin center frequency | Standard coherent phase vocoder approach |
| Spectral tilt | Multiplicative gain per bin | Frequency-domain efficient, matches spec |
| Unfreeze transition | Linear fade over hopSize samples | Simple, click-free, spec-compliant |
| SIMD | Not beneficial | Once-per-hop processing, FFT already SIMD |
| Layer | Layer 2 (processors/) | Depends on FFT, SpectralBuffer, FormantPreserver (Layer 1-2) |
