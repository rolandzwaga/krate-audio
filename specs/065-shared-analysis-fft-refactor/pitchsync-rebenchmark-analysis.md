# PitchSync Re-Benchmark Analysis (SC-008, User Story 5)

**Spec**: 065-shared-analysis-fft-refactor
**Date**: 2026-02-18
**Benchmark Contract**: KrateDSP benchmark harness, 44.1 kHz, block size 256, 4 voices, Release build, 2s warmup, 10s steady-state

## Measured Results

### PitchSync 4 Voices (Post Shared-Analysis Refactor)

| Metric | Value |
|--------|-------|
| Real-time CPU % | 27.11% |
| Average process() time | 1529.66 us/block |

### Spec 064 Baseline (Pre-Refactor)

| Metric | Value |
|--------|-------|
| Real-time CPU % | 25.93-27.86% (Phase 1 T003 range) |
| Average process() time | 1460-1588 us/block |
| Budget (spec 064) | < 3% CPU |

## Analysis

### (a) Measured PitchSync CPU vs Spec 064 Baseline

The post-refactor PitchSync measurement of **27.11% CPU / 1529.66 us/block** falls squarely within the pre-refactor baseline range of 25.93-27.86% CPU / 1460-1588 us/block. The measurement is well within normal run-to-run variance for this benchmark configuration.

**Conclusion**: PitchSync performance is unchanged by the shared-analysis refactor. The ~26-28% CPU measurement is consistent with the spec 064 baseline of ~26.4% average.

### (b) Did the Shared-Analysis Refactor Improve PitchSync?

**No.** This is expected and by design.

The shared-analysis refactor (spec 065) modified only the PhaseVocoder code path in HarmonizerEngine:
- Added shared STFT and SpectralBuffer members to HarmonizerEngine
- Added a PhaseVocoder-specific branch in `HarmonizerEngine::process()` that runs the forward FFT once and shares the analysis spectrum across voices
- Added `processWithSharedAnalysis()` and `pullOutputSamples()` to PhaseVocoderPitchShifter

**None of these changes affect PitchSync.** When `PitchMode::PitchSync` is active, HarmonizerEngine takes the standard per-voice `PitchShiftProcessor::process()` path, which delegates to `PitchSyncGranularShifter`. The shared STFT and SpectralBuffer members are not used.

The PitchSync bottleneck is fundamentally different from PhaseVocoder's: each PitchSyncGranularShifter voice runs its own YIN autocorrelation for pitch detection, which dominates the per-voice CPU cost. This is completely orthogonal to the forward FFT sharing optimization.

### (c) Is Shared Pitch Detection Architecturally Feasible?

**Yes, shared pitch detection is architecturally feasible.** The same principle that justified shared FFT analysis applies: all 4 voices analyze the identical input signal, so pitch detection results are identical across voices.

#### Architecture

A shared-pitch-detection optimization would follow this pattern:

1. Run YIN autocorrelation **once per block** on the input signal (in HarmonizerEngine)
2. Pass the detected pitch (and confidence) to all active PitchSync voices
3. Each voice uses the shared pitch result for its own grain scheduling and pitch ratio computation

This mirrors the shared-analysis pattern established in this spec:
- **Shared**: pitch detection (analogous to forward FFT)
- **Per-voice**: grain scheduling, pitch ratio application, output synthesis (analogous to phase rotation + OLA)

#### Estimated CPU Savings

Current PitchSync 4-voice cost: **~27% CPU** (1530 us/block)

The YIN autocorrelation is the dominant cost in PitchSyncGranularShifter. A rough breakdown:
- **YIN pitch detection**: ~80% of per-voice cost (autocorrelation is O(N^2) on the analysis window)
- **Grain scheduling + synthesis**: ~20% of per-voice cost

With 4 voices, eliminating 3 of 4 YIN computations would save approximately:
- 3/4 of the YIN cost = ~60% of total PitchSync cost
- Estimated post-optimization: ~27% * 0.4 = **~10.8% CPU**

This would bring PitchSync much closer to its 3% budget, though additional work (optimizing the YIN implementation itself, or using a less expensive pitch detection algorithm) may be needed to reach that target.

#### Implementation Considerations

1. **PitchSyncGranularShifter API change**: Similar to the `processWithSharedAnalysis()` pattern, a `processWithSharedPitch(float detectedPitch, float confidence)` method could be added to bypass the internal YIN computation.

2. **Pitch detection timing**: YIN requires a minimum analysis window (typically 2-3x the longest expected period). At 44.1 kHz with a minimum fundamental of ~80 Hz, this is ~1100-1650 samples. Since the block size is 256, the pitch detector must buffer input internally and update asynchronously -- which is already how each voice's internal YIN works.

3. **No correctness risk**: Since all voices see the same input, shared pitch detection produces mathematically identical results to per-voice detection. There is no quality or correctness tradeoff.

4. **Recommendation**: Implement shared pitch detection in a dedicated optimization spec after this shared-analysis refactor is merged and validated. Profile PitchSyncGranularShifter to confirm YIN is the dominant cost before proceeding. The pattern established by spec 065 (shared computation at the engine level, per-voice consumption of shared results) provides a proven template.

## Summary

| Question | Answer |
|----------|--------|
| PitchSync CPU changed? | No -- 27.11% vs ~26.4% baseline (within variance) |
| Shared-analysis refactor helped PitchSync? | No -- refactor only affects PhaseVocoder path |
| Shared pitch detection feasible? | Yes -- same principle as shared FFT, estimated ~60% savings |
| Recommended next step | Dedicated PitchSync optimization spec with shared YIN |
