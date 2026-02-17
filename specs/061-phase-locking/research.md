# Research: Identity Phase Locking for PhaseVocoderPitchShifter

**Feature**: 061-phase-locking | **Date**: 2026-02-17

## Research Questions

### RQ-1: Where exactly does phase locking insert into the existing processFrame() code flow?

**Decision**: Phase locking inserts between the existing Step 1 (analysis: extract magnitude + compute instantaneous frequency) and Step 2 (synthesis: pitch shift loop). The peak detection and region assignment operate on the analysis-domain `magnitude_[]` array populated in Step 1. The phase propagation logic modifies the inner loop of Step 2 where `synthPhase_[k]` is accumulated and Cartesian output is computed.

**Rationale**: The existing `processFrame()` has a clean 3-step structure:
1. Step 1: Analyze (magnitude extraction, frequency computation) -- lines 1072-1096
2. Step 2: Synthesize (pitch shift resampling + phase accumulation) -- lines 1101-1133
3. Step 3: Formant preservation (optional) -- lines 1136-1155

Phase locking requires analysis-domain magnitude data (from Step 1) to detect peaks and assign regions. The locked phase propagation replaces the per-bin phase accumulation inside Step 2's synthesis loop. Step 3 (formant) is structurally unchanged but uses the phase values set during Step 2.

**Alternatives considered**:
- Inserting peak detection inside Step 2's synthesis loop: Rejected because peak detection must operate on analysis-domain magnitudes before the pitch-shift resampling loop begins.
- Creating a separate method for phase locking: Rejected per spec guidance ("keeping it inline in processFrame() is simpler and avoids premature abstraction").

### RQ-2: How should non-peak bin phases interact with the formant preservation step?

**Decision**: Store `phaseForOutput` in `synthPhase_[k]` for ALL bins (both peak and non-peak) when processing in the Step 2 loop. This ensures the formant preservation step in Step 3, which uses `synthPhase_[k]` to recompute Cartesian output with adjusted magnitudes, has the correct phase for every bin.

**Rationale**: The formant step (Step 3) iterates over all bins and computes `cos(synthPhase_[k])` / `sin(synthPhase_[k])`. If non-peak bins had their locked phase stored only in a local variable, Step 3 would use stale/wrong values from `synthPhase_[k]`. By writing the locked phase back to `synthPhase_[k]`, the formant step works correctly without modification.

This is safe because:
- Peak bins: `synthPhase_[k]` is accumulated normally (horizontal coherence preserved across frames)
- Non-peak bins: The locked phase is written to `synthPhase_[k]` each frame, but this value is NOT used for accumulation in the next frame. Instead, the next frame re-derives the locked phase from the rotation angle. So overwriting is harmless.

**Alternatives considered**:
- Adding a separate `outputPhase_[]` array: Rejected because it adds ~16KB of memory per instance (4097 floats) with no benefit over writing to `synthPhase_[k]`.
- Extracting phase from `synthesisSpectrum_` Cartesian data: Rejected because `atan2` per bin is expensive and unnecessary.
- Modifying the formant step to skip recomputation when phases haven't changed: Over-engineering for no measurable benefit.

### RQ-3: How do other implementations (Rubber Band, DAFX) structure the peak detection?

**Decision**: Use the simple 3-point local maximum comparison (`magnitude[k] > magnitude[k-1] AND magnitude[k] > magnitude[k+1]`) as described by Laroche & Dolson (1999). No parabolic interpolation, no multi-resolution peak-picking.

**Rationale**: The Laroche & Dolson paper uses this exact 3-point comparison. More sophisticated peak detection (parabolic interpolation of the peak frequency, spectral reassignment) provides marginal quality improvement for identity phase locking but adds significant complexity. The spec explicitly states: "More sophisticated peak detection methods are out of scope for this initial implementation but could be added as a future enhancement."

Rubber Band uses a more sophisticated approach with peak tracking across frames and frequency reassignment, but this is because it handles time-stretching (not just pitch-shifting) and needs to maintain peak identity across analysis frames. For our pitch-shifting-only use case, per-frame peak detection without tracking is sufficient.

**Alternatives considered**:
- Parabolic interpolation of peak frequency: Deferred to future enhancement. Would improve frequency accuracy for peaks between bins but adds complexity.
- Peak tracking across frames (Rubber Band approach): Not needed for identity phase locking. Scaled phase locking (not implemented here) would benefit from it.
- Threshold-based peak detection (require magnitude > threshold): Rejected because it introduces a tuning parameter. The 3-point comparison naturally adapts to signal level.

### RQ-4: What is the correct mapping from synthesis bin to analysis-domain peak?

**Decision**: For synthesis bin `k`, compute `srcBin = k / pitchRatio` (fractional), then `srcBinRounded = round(srcBin)`, and look up `regionPeak_[srcBinRounded]` to find the controlling analysis-domain peak.

**Rationale**: The existing code already uses `srcBin = k / pitchRatio` for magnitude interpolation. The peak detection and region assignment operate in the analysis domain on `magnitude_[]`. To find which analysis-domain peak controls a given synthesis bin, we map back through the same ratio. Rounding (not truncation) is used because the region assignment was done on integer analysis bins.

For determining whether the synthesis bin IS a peak bin, we check `isPeak_[srcBinRounded]` rather than mapping the analysis peak forward to find its synthesis position. This avoids the complexity of maintaining a reverse mapping and handles the case where multiple synthesis bins map to the same analysis bin (downshift).

**Alternatives considered**:
- Checking `isPeak_[srcBin0]` (truncated, not rounded): Less accurate mapping, would assign bins incorrectly near boundaries.
- Pre-computing a synthesis-domain peak map: More memory, more complexity, no measurable benefit for the standard case.

### RQ-5: How should the rotation angle be computed for the non-peak path?

**Decision**: Use the formula `rotationAngle = synthPhase_[synthPeakBin] - prevPhase_[analysisPeak]`, where:
- `synthPeakBin` is the synthesis bin corresponding to the analysis peak: `round(analysisPeak * pitchRatio)`
- `prevPhase_[analysisPeak]` is the analysis phase at the peak bin (from `prevPhase_[]` which was set in Step 1)

Then apply: `phaseForOutput = analysisPhaseAtSrc + rotationAngle`

Where `analysisPhaseAtSrc` is the interpolated analysis phase at the fractional source bin: `prevPhase_[srcBin0] * (1-frac) + prevPhase_[srcBin1] * frac`.

**Rationale**: This implements the identity phase locking formula from Laroche & Dolson (1999): "phi_out[k] = phi_in[srcBin] + (phi_out[peak] - phi_in[srcPeak])". The rotation angle represents the phase transformation applied at the peak bin, and identity phase locking preserves the original phase differences between each bin and its peak.

**IMPORTANT NOTE on processing order**: There is an ordering dependency. The synthesis loop iterates over synthesis bins `k` from 0 to numBins-1. For a pitch-up shift (pitchRatio > 1), synthesis bin `k` maps to analysis bin `k/pitchRatio` which is LOWER. So synthesis bin `synthPeakBin` (which corresponds to analysis peak `analysisPeak`) is at a HIGHER synthesis bin index than the non-peak synthesis bins that reference it. For these lower-indexed non-peak bins, `synthPhase_[synthPeakBin]` has NOT been updated yet in this frame when a single-pass loop reaches them — it still holds the previous frame's stale value.

This IS a problem that requires a two-pass approach. A single forward pass cannot satisfy the ordering requirement for pitch-up shifts. We need to either:
(a) Process all peak bins first in a separate pass, then process non-peak bins, OR
(b) Use the previous frame's `synthPhase_` for the peak reference, which would be incorrect.

**Resolution**: Option (a) -- split the synthesis loop into two passes:
1. **Pass 1 (peak bins only)**: Iterate synthesis bins, detect peak bins via `isPeak_[srcBinRounded]`, accumulate `synthPhase_[k]` for peak bins only, and store Cartesian output.
2. **Pass 2 (non-peak bins only)**: Iterate synthesis bins, skip peak bins, compute locked phase using `synthPhase_[synthPeakBin]` (now guaranteed to be up-to-date from Pass 1).

This two-pass approach matches the algorithmic description in the spec and avoids ordering issues.

**Alternatives considered**:
- Single-pass with forward-reference resolution: Complex, error-prone, may not handle all pitchRatio values correctly.
- Caching peak phases in a separate array: Works but adds unnecessary memory. The two-pass approach uses the existing `synthPhase_[]` array.

### RQ-6: What is the toggle-to-basic re-initialization mechanism?

**Decision**: When `wasLocked_ == true` and `phaseLockingEnabled_ == false` (transition from locked to basic), re-initialize ALL bins' `synthPhase_[k] = prevPhase_[k]` before entering the synthesis loop. This gives the basic per-bin accumulation a valid starting point.

**Rationale**: During phase locking, `synthPhase_[k]` for non-peak bins contains locked phase values that were NOT accumulated -- they were set each frame from the rotation angle. If we simply switch to basic mode, the basic accumulation would resume from these locked values, which are not continuous with what basic accumulation would have produced. By resetting to `prevPhase_[k]` (the current analysis phase), we give the basic accumulator a clean starting point. The first basic frame will have the correct phase difference (`prevPhase_[k]` for previous frame minus `prevPhase_[k]` from current = the actual analysis phase advance), producing correct results.

A brief single-frame artifact at the transition is acceptable per spec (FR-008).

**Alternatives considered**:
- Shadow-accumulate all bins during locking: Rejected per spec clarification -- adds per-frame overhead during normal locked processing.
- Gradual crossfade between locked and basic: Over-engineering for a programmatic toggle that users won't hear.

### RQ-7: Is the `<array>` include missing from pitch_shift_processor.h?

**Decision**: Yes. The file currently includes `<cstddef>`, `<cstdint>`, `<cmath>`, `<memory>`, `<vector>`, `<algorithm>` but NOT `<array>`. Adding `#include <array>` is required for the new `std::array<bool, kMaxBins>`, `std::array<uint16_t, kMaxPeaks>`, and `std::array<uint16_t, kMaxBins>` member variables.

**Rationale**: Direct verification by reading the file (lines 20-27). No `<array>` present. This is a simple, low-risk addition.

## Summary of Decisions

> **Canonical algorithm flow**: The authoritative pseudocode is in `data-model.md` (Algorithm Flow section). This research file records the rationale for each decision; `plan.md` contains the full implementation rendering. If algorithm descriptions differ between documents, `data-model.md` is the canonical source.

| # | Decision | Impact |
|---|----------|--------|
| 1 | Insert phase locking between Step 1 and Step 2 of processFrame() | Defines code structure |
| 2 | Store locked phase in `synthPhase_[k]` for all bins (not a local variable) | Ensures formant compatibility; see RQ-2 for full rationale |
| 3 | Use 3-point local maximum for peak detection | Simplicity, matches reference algorithm |
| 4 | Map synthesis bins to analysis peaks via `regionPeak_[round(k/pitchRatio)]` | Consistent with existing bin mapping; rounding is standard round-half-up, separate from equidistant-to-lower rule in region boundaries |
| 5 | Two-pass synthesis loop (peaks first, then non-peaks) | Resolves ordering dependency: pitch-up shifts cause non-peak bins to reference peak synthPhase_ values not yet computed in a single pass |
| 6 | Re-initialize `synthPhase_[k] = prevPhase_[k]` on locked-to-basic toggle | Clean transition, per spec |
| 7 | Add `#include <array>` to pitch_shift_processor.h | Required for std::array members |
| 8 | `phaseLockingEnabled_` is plain `bool`, NOT `std::atomic<bool>` | Not thread-safe across audio/control threads; callers must ensure no concurrent access |
