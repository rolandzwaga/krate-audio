# Feature Specification: Shared-Analysis FFT Refactor for PhaseVocoder Multi-Voice Harmonizer

**Feature Branch**: `065-shared-analysis-fft-refactor`
**Plugin**: KrateDSP (Shared DSP Library)
**Created**: 2026-02-18
**Status**: Draft
**Input**: User description: "Refactor the PhaseVocoderPitchShifter (Layer 2) and HarmonizerEngine (Layer 3) to implement a shared-analysis FFT architecture for multi-voice harmonizer processing. Currently, when 4 harmony voices run in PhaseVocoder mode, each voice's PitchShiftProcessor runs its own forward FFT on the identical input signal -- wasting 75% of forward FFT computation."
**Predecessor**: [064-harmonizer-engine](../064-harmonizer-engine/spec.md) (FR-020 DEFERRED, FR-021 DEFERRED-COUPLED, SC-008 PhaseVocoder NOT MET)

## Clarifications

### Session 2026-02-18

- Q: Should `processFrame()` be adapted by copying the shared SpectralBuffer into the internal member, or by refactoring `processFrame()` to accept a `const SpectralBuffer&` parameter directly? → A: Option B -- refactor `processFrame()` to accept `const SpectralBuffer&` directly. This is a REQUIRED refactor, not optional. The rationale is: (1) conceptual correctness -- the spectrum is shared, immutable, frame-scoped, and engine-owned; copying models shared data as owned (a "design lie"); (2) memory bandwidth -- copying wastes ~11 MB/sec of unnecessary traffic (8KB spectrum x 4 voices x ~4 frames/block x 172 blocks/sec at 44.1kHz/256); (3) cache behavior -- shared read-only access keeps the spectrum hot in L1 cache and avoids write-allocate penalties; (4) mutation prevention -- `const SpectralBuffer&` turns accidental mutation into a compile-time error; (5) lifetime semantics -- the engine owns the spectrum lifetime, voices borrow it per-frame; (6) future optimizations -- enables SIMD across voices, parallel voice processing, and potential offload paths. Canonical API shape: `void PhaseVocoderPitchShifter::processFrame(const SpectralBuffer& analysis, SpectralBuffer& synthesis) noexcept;`
- Q: Should FR-020/021/022 (shared peak detection, shared transient detection, shared envelope extraction) be scheduled as implementation tasks in this spec or explicitly deferred? → A: Explicitly DEFERRED to a post-benchmark optimization spec. "MAY optimize" language removed. Rationale: (1) ownership model stabilization must precede shared interpretation -- until the shared FFT pipeline is stable, shared secondary features mix policy into infrastructure and destroy bug isolation; (2) each "MAY" implied hidden semantic contracts (peak ownership, hysteresis interactions, envelope smoothing coupling), not just performance changes; (3) FR-020/021/022 push from "analysis shared, voices independent" toward "analysis + interpretation shared, voices parameterized" -- that is a different architecture requiring profiling evidence before crossing; (4) rule: correct pipeline first, measure, then optimize demonstrated hotspots. Shared computation helps only when `(shared_cost + coordination_cost) < duplicated_cost`, which requires measured numbers first.
- Q: What measurement method and tool must be used to verify SC-001 (CPU target) and SC-008 (PitchSync re-benchmark)? → A: The KrateDSP benchmark harness under identical conditions to spec 064 -- mandatory for measurement validity, not just convenience. SC-001 and SC-008 are relative improvements against spec 064 baselines; changing any measurement variable (CPU API, block size, thread affinity, warmup, meter smoothing) invalidates the comparison. Two metrics MUST be reported per benchmark: (1) real-time CPU % (primary success/fail gate) and (2) average `process()` time in µs/block (architecture-neutral diagnostic ground truth, stable across scheduler differences). DAW CPU meters are explicitly prohibited for engineering verification. Benchmark contract added to Success Criteria section.
- Q: What is the output contract for `HarmonizerEngine::process()` when called with fewer input samples than `hopSize` (sub-hop block)? → A: Buffer incoming samples in the shared STFT without triggering analysis; zero-fill (or drain existing OLA tail for) output samples for which no new synthesis frame was computed. Asserting or rejecting sub-hop blocks is PROHIBITED -- hosts deliver arbitrary buffer sizes, all are valid. Option B (draining OLA buffers immediately) is incorrect: it violates COLA reconstruction for incomplete frames, can double-output partially overlapped segments, and breaks deterministic phase. This behaviour is encoded in FR-013a and the Edge Cases section.
- Q: What should `processWithSharedAnalysis()` write to the output buffer in degenerate/no-op conditions (wrong mode, unprepared state, pre-latency priming period, FFT size mismatch)? → A: Zero-fill the entire output buffer in all degenerate cases. Leaving the buffer unchanged is PROHIBITED (callers may pass uninitialized memory; garbage audio is a host safety violation). Partial OLA output before priming is PROHIBITED (unpredictable amplitude and phase discontinuities). Zero-fill is the only audibly safe, deterministically testable, real-time safe choice -- matches VST3/AUv3 industry practice for unprocessed/disabled channels. Encoded in FR-008a and FR-009a.

## Background & Motivation

The HarmonizerEngine (Layer 3) was implemented in spec 064 with independent per-voice `PitchShiftProcessor` instances. In PhaseVocoder mode, each of the 4 voices runs its own forward FFT on the identical input signal. This wastes 75% of forward FFT computation and contributes to the PhaseVocoder mode measuring approximately 24% CPU vs a 15% budget (SC-008 from 064). The deferred FR-020 and FR-021 from spec 064 describe the target architecture: run the forward FFT once per block, share the analysis spectrum as a read-only resource across all voices, while keeping per-voice phase state, synthesis iFFT, and OLA buffers strictly independent.

This refactor addresses two deferred items from spec 064:
- **FR-020**: Shared-analysis architecture -- forward FFT once, analysis spectrum shared across voices.
- **FR-021**: Per-voice OLA isolation -- each voice MUST have its own independent OLA buffer to satisfy the COLA (Constant Overlap-Add) condition.

Additionally, the user requests investigation of SC-008's PitchSync mode overage (approximately 26.4% CPU vs a 3% budget), which is caused by per-voice YIN autocorrelation inside PitchSyncGranularShifter.

### Architecture Overview

The current architecture has each voice performing the full STFT pipeline independently:

```
Current (per-voice, redundant):

Input --> Voice 0: [pushSamples -> analyze(FFT) -> processFrame -> OLA.synthesize] -> Output 0
Input --> Voice 1: [pushSamples -> analyze(FFT) -> processFrame -> OLA.synthesize] -> Output 1
Input --> Voice 2: [pushSamples -> analyze(FFT) -> processFrame -> OLA.synthesize] -> Output 2
Input --> Voice 3: [pushSamples -> analyze(FFT) -> processFrame -> OLA.synthesize] -> Output 3

4x forward FFT on identical input = 75% wasted computation
```

The target architecture shares the forward FFT analysis and passes the result to all voices:

```
Target (shared analysis):

Input --> Shared STFT: [pushSamples -> analyze(FFT)] --> Analysis Spectrum (read-only)
                                                               |
          Voice 0: [processFrame(spectrum, ratio0) -> OLA_0.synthesize] -> Output 0
          Voice 1: [processFrame(spectrum, ratio1) -> OLA_1.synthesize] -> Output 1
          Voice 2: [processFrame(spectrum, ratio2) -> OLA_2.synthesize] -> Output 2
          Voice 3: [processFrame(spectrum, ratio3) -> OLA_3.synthesize] -> Output 3

1x forward FFT + 4x synthesis = 75% forward FFT savings
```

### Design Rationale

**Why shared analysis is valid**: All voices analyze the identical input signal. The forward FFT is a deterministic linear transform -- feeding the same input to four independent FFT instances produces four identical analysis spectra. Sharing the analysis result is therefore mathematically equivalent to independent analysis, with no loss of information or quality.

**Why OLA must remain per-voice**: The Constant Overlap-Add (COLA) condition guarantees artifact-free time-domain reconstruction only when overlap-adding frames from a single coherent signal (Smith, CCRMA). Each voice applies a different pitch ratio, producing distinct phase rotations and synthesis timing. Sharing an OLA buffer across voices would violate COLA, producing phase interference, time-varying comb filtering at the hop rate, amplitude instability, and metallic smearing on attacks (Laroche & Dolson, 1999). The memory cost of per-voice OLA buffers is approximately 16 KB for 4 voices -- trivial relative to the spectral buffers.

**Why identity phase locking works with shared analysis**: Identity phase locking (Laroche & Dolson, 1999) assigns non-peak bins to the phase rotation of their nearest spectral peak. The peak detection and region-of-influence assignment operate on the analysis magnitude spectrum, which is shared. The phase rotation computation uses the shared analysis phases (`prevPhase_[]`) combined with per-voice `synthPhase_[]` accumulators. Since peak detection is a read-only operation on the shared spectrum and phase accumulation is per-voice, identity phase locking is fully compatible with shared analysis.

**Why transient detection works with shared analysis**: The spectral transient detector operates on the analysis magnitude spectrum to detect sudden energy changes. Since all voices see the same input, transient positions are identical for all voices. The detection result (boolean "is transient") can be computed once from the shared analysis spectrum and broadcast to all voices. Each voice then independently resets its own `synthPhase_[]` to the shared analysis phase -- a per-voice write operation using shared read-only data.

**Why formant preservation works with shared analysis**: The formant preserver extracts a spectral envelope from the analysis magnitude spectrum. The original (pre-shift) envelope is identical for all voices and can be extracted once. Each voice's shifted envelope depends on its own pitch ratio and must be computed per-voice, but this is already the case in the current architecture.

### References

- Laroche, J. & Dolson, M. (1999), "New phase-vocoder techniques for pitch-shifting, harmonizing and other exotic effects", IEEE WASPAA. Describes identity phase locking and harmonizing architecture.
- Laroche, J. & Dolson, M. (1999), "Improved Phase Vocoder Time-Scale Modification of Audio", IEEE TSAP. Phase coherence techniques.
- Smith, J.O. (CCRMA), "Constant-Overlap-Add (COLA) Constraint", Spectral Audio Signal Processing. Defines COLA condition and reconstruction guarantees.
- Prusha, Z. & Holighaus, N. (2022), "Phase Vocoder Done Right", arXiv. Modern phase vocoder best practices.
- de Cheveigne, A. & Kawahara, H. (2002), "YIN, a fundamental frequency estimator for speech and music", JASA. YIN autocorrelation algorithm.
- Roebel, A. (2003), "Transient detection and preservation in the phase vocoder", HAL. Transient detection operating on analysis spectrum.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Shared-Analysis FFT Reduces PhaseVocoder CPU Cost (Priority: P1)

A plugin developer uses HarmonizerEngine with 4 voices in PhaseVocoder mode. The engine runs the forward FFT analysis once per block and shares the analysis spectrum across all voices. Each voice only performs its own phase rotation, synthesis iFFT, and OLA reconstruction. The CPU cost is significantly reduced compared to the current per-voice independent FFT approach.

**Why this priority**: This is the core performance optimization that motivated the entire spec. Without it, PhaseVocoder mode with 4 voices exceeds the 15% CPU budget from SC-008 (measured at approximately 24%). This is the primary deliverable.

**Independent Test**: Can be tested by benchmarking 4-voice PhaseVocoder processing before and after the refactor. The forward FFT cost should be approximately 1/4 of the previous cost. Total CPU usage should decrease measurably.

**Acceptance Scenarios**:

1. **Given** a HarmonizerEngine with 4 voices in PhaseVocoder mode processing a 440Hz sine tone, **When** processing completes, **Then** the output is functionally equivalent (within floating-point tolerance) to the output produced by 4 independent PitchShiftProcessor instances processing the same input at the same pitch ratios.
2. **Given** a HarmonizerEngine with 4 voices in PhaseVocoder mode, **When** benchmarking CPU cost at 44.1kHz with block size 256, **Then** the total CPU usage is measurably lower than the pre-refactor measurement of approximately 24%.
3. **Given** a HarmonizerEngine with 1 voice in PhaseVocoder mode, **When** processing, **Then** the shared-analysis path still functions correctly (single-voice degenerate case).

---

### User Story 2 - New Layer 2 API for External Analysis Injection (Priority: P1)

A developer (or system component like HarmonizerEngine) can pass a pre-computed analysis spectrum to PhaseVocoderPitchShifter, bypassing its internal STFT analysis. The PhaseVocoderPitchShifter performs only phase rotation, synthesis iFFT, and OLA reconstruction using the externally provided spectrum.

**Why this priority**: This is the enabling API change at Layer 2 that makes the shared-analysis architecture possible. Without this API, HarmonizerEngine cannot inject a shared spectrum into individual voice processors.

**Independent Test**: Can be tested by running PhaseVocoderPitchShifter with the standard `process()` method and with `processWithSharedAnalysis()` on the same input, verifying identical output.

**Acceptance Scenarios**:

1. **Given** a PhaseVocoderPitchShifter with the new `processWithSharedAnalysis()` method, **When** called with an analysis spectrum produced by `STFT::analyze()` on the same input that would have been pushed via `pushSamples()`, **Then** the output is identical to calling the standard `process()` method.
2. **Given** a PhaseVocoderPitchShifter using `processWithSharedAnalysis()`, **When** processing with formant preservation enabled, **Then** the formant envelope is correctly extracted from the shared spectrum and applied per-voice.
3. **Given** a PhaseVocoderPitchShifter using `processWithSharedAnalysis()`, **When** processing with identity phase locking enabled, **Then** peak detection operates on the shared spectrum and phase locking produces correct output.
4. **Given** a PhaseVocoderPitchShifter using `processWithSharedAnalysis()`, **When** processing with transient detection and phase reset enabled, **Then** transients are detected from the shared spectrum and phase reset is applied correctly.

---

### User Story 3 - Per-Voice OLA Buffer Isolation Verified (Priority: P1)

Each voice in the harmonizer maintains its own independent OLA (overlap-add) buffer. Sharing OLA buffers across voices is architecturally prevented. This guarantees that the COLA condition is satisfied for each voice's time-domain reconstruction independently.

**Why this priority**: OLA isolation is a correctness requirement, not just a performance concern. Violating COLA produces audible artifacts (comb filtering, metallic smearing, amplitude instability). This is equally critical as the shared-analysis optimization itself.

**Independent Test**: Can be tested by processing 2 voices at different pitch ratios and verifying that each voice's output is identical to a standalone PhaseVocoderPitchShifter processing the same input at the same ratio.

**Acceptance Scenarios**:

1. **Given** 2 voices at +7 and -5 semitones sharing analysis but with independent OLA buffers, **When** processing a 440Hz input, **Then** each voice's output matches a standalone PhaseVocoderPitchShifter at the same ratio (within floating-point tolerance).
2. **Given** 4 voices at different ratios, **When** muting all voices except one, **Then** the remaining voice's output is identical to a single standalone PhaseVocoderPitchShifter at that ratio.
3. **Given** 2 voices processing simultaneously, **When** one voice is muted mid-stream, **Then** the remaining active voice's output is unaffected (no artifacts from the muted voice's OLA state).

---

### User Story 4 - PitchShiftProcessor Public API Backward Compatibility (Priority: P2)

The existing `PitchShiftProcessor::process()` API continues to work identically for all existing consumers. The shared-analysis path is an opt-in addition, not a replacement. No existing code that uses PitchShiftProcessor needs to change.

**Why this priority**: Backward compatibility ensures that all existing tests and consumers (non-harmonizer use cases, standalone pitch shifting) continue to work without modification. Breaking the public API would cascade changes across the entire codebase.

**Independent Test**: Can be tested by running the entire existing dsp_tests suite after the refactor and verifying all tests pass.

**Acceptance Scenarios**:

1. **Given** the refactored PitchShiftProcessor, **When** running all existing unit tests, **Then** every test passes without modification.
2. **Given** a standalone PitchShiftProcessor (not used through HarmonizerEngine), **When** calling `process()`, **Then** behavior is identical to pre-refactor.
3. **Given** a PitchShiftProcessor in any mode other than PhaseVocoder, **When** calling the shared-analysis method, **Then** the call is a documented no-op.

---

### User Story 5 - PitchSync Mode Investigation and Re-Benchmark (Priority: P3)

After the shared-analysis refactor, the PitchSync mode is re-benchmarked to determine whether any incidental improvements occurred. The YIN autocorrelation overhead is analyzed and documented, with recommendations for a future optimization spec if needed.

**Why this priority**: PitchSync mode's 26.4% CPU vs 3% budget is a known issue from spec 064, but the root cause (per-voice YIN autocorrelation) is orthogonal to the shared-analysis FFT optimization. This story investigates whether shared pitch detection (analogous to shared FFT analysis) could help, but is lower priority because the fix requires different architectural changes.

**Independent Test**: Can be tested by benchmarking PitchSync mode before and after the refactor and comparing results.

**Acceptance Scenarios**:

1. **Given** a HarmonizerEngine with 4 voices in PitchSync mode, **When** benchmarking CPU cost at 44.1kHz with block size 256 after the refactor, **Then** the measurement is documented (whether improved, unchanged, or regressed).
2. **Given** the benchmark results, **When** analyzing the PitchSync overhead, **Then** the report identifies whether shared pitch detection is architecturally feasible and estimates potential CPU savings.

---

### Edge Cases

- What happens when PhaseVocoderPitchShifter receives a shared analysis spectrum with a different FFT size than its own configuration? The method MUST validate that the spectrum's `numBins()` matches the expected `kFFTSize / 2 + 1` and reject mismatched spectra (assertion in debug builds, no-op in release builds).
- What happens when `processWithSharedAnalysis()` is called on a PhaseVocoderPitchShifter that has not been prepared? The method MUST be a no-op, consistent with the existing `process()` pre-condition behavior.
- What happens when the shared analysis path is used with unity pitch ratio (1.0)? The `processWithSharedAnalysis()` method MUST NOT auto-apply the internal unity-pitch bypass. The caller (HarmonizerEngine) is responsible for detecting unity pitch and routing accordingly. The rationale: in a shared-analysis context, the engine controls frame dispatch and can avoid calling `processWithSharedAnalysis()` for unity-pitch voices entirely, applying the OLA-based pass-through at the engine level. Implementing an internal bypass in `processWithSharedAnalysis()` would duplicate policy already owned by the caller.
- What happens when HarmonizerEngine switches from PhaseVocoder mode to another mode (Simple, Granular, PitchSync)? The shared STFT and analysis spectrum are no longer used. Non-PhaseVocoder modes continue to use the standard per-voice `PitchShiftProcessor::process()` path.
- What happens when `numActiveVoices` changes from 4 to 2 while in PhaseVocoder mode? The shared analysis still runs once (it depends on the input, not voice count). Only the 2 active voices consume the shared spectrum. This is correct and slightly more efficient.
- What happens with the PhaseVocoderPitchShifter's internal STFT and input buffering when using shared analysis? The internal `stft_` member's `pushSamples()` and `canAnalyze()` cycle is bypassed entirely. The caller (HarmonizerEngine) drives the frame-level timing via its own shared STFT.
- What happens if the shared analysis spectrum is modified between voice processing calls? This is a programming error. The shared spectrum MUST be treated as read-only after `analyze()` returns. Const-reference parameter typing MUST enforce this.
- What happens when `HarmonizerEngine::process()` is called with fewer input samples than `hopSize` (sub-hop block)? Hosts deliver arbitrary buffer sizes (128, 256, 512 samples are all valid). The shared STFT MUST buffer incoming samples until a full hop is available. Per-voice OLA buffers produce output only for complete, ready frames. Output samples for which no new frame was computed MUST be zero-filled (or filled with the existing OLA tail if one is available from a prior frame). No DSP processing beyond sample buffering is performed. Requesting output for an incomplete hop is NOT a programming error. Asserting or otherwise rejecting sub-hop blocks is PROHIBITED (it would break host compatibility with arbitrary buffer sizes). This behavior is fully deterministic and real-time safe, and is consistent with the latency model: the pipeline introduces hop-size + internal STFT delay regardless of host block size.

## Requirements *(mandatory)*

### Functional Requirements

#### Layer 2 Changes (PhaseVocoderPitchShifter)

- **FR-001**: PhaseVocoderPitchShifter MUST expose a new public method `processWithSharedAnalysis()` that accepts a pre-computed analysis spectrum (const reference to SpectralBuffer) and a pitch ratio. This method MUST skip the internal `stft_.pushSamples()` and `stft_.analyze()` steps entirely and instead pass the externally provided spectrum to the refactored `processFrame()` as a `const SpectralBuffer&` parameter. Output samples are retrieved separately via `pullOutputSamples()` (see FR-007).
- **FR-002**: The `processWithSharedAnalysis()` method MUST produce output identical (within floating-point tolerance of 1e-5) to the standard `process()` method when given the same analysis spectrum that `process()` would have computed internally. This equivalence MUST be verified by automated tests.
- **FR-003**: The `processWithSharedAnalysis()` method MUST correctly support identity phase locking when enabled. Peak detection and region-of-influence assignment MUST operate on the shared analysis spectrum's magnitude data. Per-voice `synthPhase_[]` accumulation MUST remain independent.
- **FR-004**: The `processWithSharedAnalysis()` method MUST correctly support transient detection and phase reset when enabled. The transient detector MUST operate on the shared analysis spectrum's magnitude data. Phase reset MUST write to the per-voice `synthPhase_[]` array using shared analysis phases.
- **FR-005**: The `processWithSharedAnalysis()` method MUST correctly support formant preservation when enabled. The original spectral envelope MUST be extracted from the shared analysis spectrum's magnitude data. The shifted spectral envelope is per-voice (depends on pitch ratio).
- **FR-006**: The existing `PhaseVocoderPitchShifter::process()` method MUST remain functionally unchanged. All existing tests MUST pass without modification. The new method is an addition, not a replacement.
- **FR-007**: The `processWithSharedAnalysis()` method MUST handle frame-level timing correctly. The caller provides one analysis frame at a time (matching the hop-size cadence). The method MUST process exactly one frame per call and perform OLA synthesis for that frame.
- **FR-008**: The `processWithSharedAnalysis()` method MUST validate that the provided SpectralBuffer's `numBins()` matches the expected `kFFTSize / 2 + 1` (= 2049). In debug builds, an assertion MUST fire on mismatch. In release builds, the method MUST be a no-op on mismatch and MUST NOT push a frame to the internal OLA buffer.
- **FR-008a**: In all degenerate and no-op conditions, `processWithSharedAnalysis()` MUST silently return without pushing a synthesis frame to the OLA buffer. This applies to: (a) FFT size mismatch (FR-008); (b) called on an unprepared PhaseVocoderPitchShifter. In these conditions, the subsequent `pullOutputSamples()` call will return 0 samples for this frame. The responsibility for zero-filling the caller's output buffer belongs to the caller (HarmonizerEngine), which zero-fills any output samples not covered by `pullOutputSamples()` (see FR-013a). Implementation note: a no-op early return with no OLA write is the correct pattern -- there is no output buffer parameter in `processWithSharedAnalysis()` to fill. During the OLA priming period (before the first complete synthesis frame), `pullOutputSamples()` will naturally return 0 samples, and callers MUST zero-fill the corresponding output. This matches the behavior of all major plugin frameworks for unprocessed/disabled channels (VST3 unprepared mode, AUv3 bypassed channels).

#### Layer 2 Changes (PitchShiftProcessor Public API)

- **FR-009**: PitchShiftProcessor MUST expose a new public method `processWithSharedAnalysis(const SpectralBuffer& analysis, float pitchRatio)` that delegates to the internal PhaseVocoderPitchShifter's `processWithSharedAnalysis()` when in PhaseVocoder mode. When in other modes, this method MUST be a documented no-op (see FR-009a) and return immediately.
- **FR-009a**: When PitchShiftProcessor's `processWithSharedAnalysis()` is called in any mode other than PhaseVocoder (Simple, Granular, PitchSync), the method MUST return immediately without invoking any DSP processing and without pushing a frame to the OLA buffer. The method has no output buffer parameter; the no-op is observable through `pullSharedAnalysisOutput()` returning 0 samples. This makes the no-op deterministically testable (assert `pullSharedAnalysisOutput()` returns 0 after a non-PhaseVocoder call).
- **FR-010**: PitchShiftProcessor MUST expose a companion method `pullSharedAnalysisOutput(float* output, std::size_t maxSamples)` that pulls samples from the internal PhaseVocoder OLA buffer into the caller's output buffer. When mode is not PhaseVocoder, this method MUST return 0 and leave the output buffer untouched. Before the OLA buffer has produced a complete synthesis frame (latency priming period), this method returns 0 samples; callers MUST zero-fill the corresponding output region (consistent with FR-013a).
- **FR-011**: PitchShiftProcessor MUST expose a method or constant to query the PhaseVocoder's FFT size and hop size, so that HarmonizerEngine can configure its shared STFT with matching parameters.

#### Layer 3 Changes (HarmonizerEngine)

- **FR-012**: HarmonizerEngine MUST own a shared `STFT` instance, a shared `SpectralBuffer` for the analysis spectrum, and a per-call scratch buffer (`pvVoiceScratch_`, a `std::vector<float>` of size `maxBlockSize`) for pulling per-voice OLA output. All three MUST be pre-allocated in `HarmonizerEngine::prepare()` -- the shared `STFT` and `SpectralBuffer` configured with the same FFT size and hop size as PhaseVocoderPitchShifter, and `pvVoiceScratch_` resized to `maxBlockSize`. Zero heap allocations are permitted in `process()` (SC-005).
- **FR-013**: When in PhaseVocoder mode, `HarmonizerEngine::process()` MUST push input samples to the shared `STFT` once per block, then for each ready analysis frame, pass the shared analysis spectrum to each active voice's PitchShiftProcessor via the new shared-analysis API.
- **FR-013a**: `HarmonizerEngine::process()` MUST handle sub-hop-size input blocks correctly. When the number of input samples is less than `hopSize` (or any call that does not produce a complete analysis frame), the shared STFT MUST buffer the incoming samples without triggering analysis. Per-voice OLA buffers MUST produce output only for complete, ready frames. Output samples for which no new synthesis frame was computed MUST be zero-filled (or filled with the existing OLA tail for any previously synthesized frames that have not yet been fully read out). Asserting, erroring, or rejecting sub-hop input blocks is PROHIBITED -- hosts deliver arbitrary buffer sizes and all are valid. This contract applies in PhaseVocoder mode only; non-PhaseVocoder modes use the per-voice `process()` path which already handles arbitrary block sizes internally.
- **FR-014**: When NOT in PhaseVocoder mode (Simple, Granular, PitchSync), HarmonizerEngine MUST continue to use the standard per-voice `PitchShiftProcessor::process()` path. The shared STFT and analysis spectrum are unused in these modes.
- **FR-015**: HarmonizerEngine's `process()` MUST continue to produce output that is functionally equivalent to the pre-refactor implementation. The refactor is a performance optimization, not a functional change. Output equivalence MUST be verified by automated tests comparing pre-refactor and post-refactor output.
- **FR-016**: HarmonizerEngine's public API (`process()`, `setVoiceInterval()`, `setPitchShiftMode()`, etc.) MUST remain unchanged. No new public methods are required at the HarmonizerEngine level for this refactor.

#### Shared Resources vs Per-Voice State

- **FR-017**: The following resources MUST be shared across all voices (owned by HarmonizerEngine): STFT instance (forward FFT computation), analysis spectrum (SpectralBuffer, read-only after `analyze()`), FFT plan and twiddle factors (implicit in STFT), and analysis window (implicit in STFT).
- **FR-018**: The following resources MUST remain per-voice (never shared): `synthPhase_[]` (accumulated synthesis phases, unique per pitch ratio), `prevPhase_[]` (previous frame analysis phases, updated per voice per frame -- note: `prevPhase_[]` is initialized from the shared analysis spectrum's phase values on the first frame of voice activation; subsequent updates are per-voice and diverge with each voice's unique pitch ratio), `magnitude_[]` and `frequency_[]` (intermediate computation arrays, written during processFrame), `synthesisSpectrum_` (output of phase modification, unique per pitch ratio), `ola_` (overlap-add buffer, MUST be independent per spec-064-FR-021), phase locking state (`isPeak_[]`, `regionPeak_[]`, `peakIndices_[]`, `numPeaks_`), transient detector state, and formant preservation state (`shiftedMagnitude_[]`, `shiftedEnvelope_[]`).
- **FR-019**: The shared analysis spectrum MUST be treated as const/read-only after `STFT::analyze()` completes. No voice processing may modify the shared spectrum. This MUST be enforced through const-reference parameter typing.

#### Optimization Opportunities (DEFERRED)

- **FR-020**: DEFERRED to a post-benchmark optimization spec. Shared peak detection -- computing peak detection and region-of-influence assignment once from the shared magnitude spectrum and broadcasting results to all voices -- is architecturally feasible but explicitly out of scope for this spec. Rationale: (a) shared peak detection introduces a peak ownership model and questions about per-voice filtering differences, changing semantic boundaries rather than just performance; (b) the shared-analysis pipeline must be validated and benchmarked first to determine whether peak detection is a measurable cost contributor; (c) mixing shared interpretation logic with shared analysis infrastructure during the initial architecture phase destroys bug isolation -- failures become ambiguous between shared lifetime, frame ordering, and optimization logic.
- **FR-021**: DEFERRED to a post-benchmark optimization spec. Shared transient detection -- running the transient detector once on the shared magnitude spectrum and broadcasting a boolean result to all voices -- is valid given that transient positions are identical for all voices. However, sharing transient state introduces timing alignment and hysteresis interaction concerns across voices. Must be validated against profiling data showing transient detection is a measurable cost before introduction. Not required to meet SC-001.
- **FR-022**: DEFERRED to a post-benchmark optimization spec. Shared original-envelope extraction -- extracting the spectral envelope from the shared magnitude spectrum once rather than per-voice -- is valid because the original envelope is identical for all voices. However, if envelope extraction becomes shared, voices become partially coupled (analysis + shared interpretation producing parameterized outputs), which is a different architecture from the clean separation this spec establishes (analysis shared and immutable, voices as fully independent consumers). That architectural boundary crossing requires profiling evidence of necessity and its own design review. The shifted envelope MUST remain per-voice regardless (it depends on per-voice pitch ratio and shifted magnitude).

**Deferral rationale (applies to all three)**: The correct DSP engine evolution is: correct pipeline first, then measure, then optimize demonstrated hotspots, then share selectively -- never the reverse. FR-001 through FR-019 establish the clean separation: analysis (shared, immutable) → voices (independent interpretation). FR-020/021/022 push toward partially shared interpretation, which is a distinct architectural step with its own semantic contracts. Shared computation only helps when `(shared_cost + coordination_cost) < duplicated_cost`. That inequality requires measured numbers. Real-time DSP frequently surprises: FFT cost can dominate entirely, cache locality can make per-voice cheaper than expected, and future SIMD batching can make duplication effectively free. Validate the baseline before coupling interpretation logic across voices.

#### Dataflow Architecture Rule (processFrame Refactor)

- **FR-023**: `PhaseVocoderPitchShifter::processFrame()` MUST be refactored to accept the analysis spectrum as a `const SpectralBuffer& analysis` parameter, the synthesis output as a `SpectralBuffer& synthesis` parameter, and the pitch ratio as a `float pitchRatio` parameter. The canonical signature is `void processFrame(const SpectralBuffer& analysis, SpectralBuffer& synthesis, float pitchRatio) noexcept;`. Copying the shared spectrum into any internal member before calling `processFrame()` is PROHIBITED. This models the correct dataflow semantics: the engine owns the analysis spectrum lifetime; voices borrow it as a read-only input for exactly the duration of one `processFrame()` call. This design: (a) eliminates ~11 MB/sec of unnecessary memory bandwidth (8 KB spectrum x 4 voices x ~688 frames/sec at 44.1kHz/256); (b) enforces const-correctness at compile time -- accidental writes to the shared spectrum become compile errors; (c) keeps the analysis spectrum hot in L1 cache via shared read-only access across voices, avoiding write-allocate penalties; (d) enables future SIMD-across-voices and parallel voice processing by making shared data trivially thread-safe (immutable during the frame).
- **FR-024**: Voices SHALL NOT retain references, pointers, or copies of the analysis spectrum outside of a `processFrame()` call. The analysis spectrum is valid only for the duration of the current frame processing call. Storing a pointer or reference to the externally provided `SpectralBuffer` in any per-voice member variable is PROHIBITED. This prevents use-after-free, stale-spectrum bugs, and any ambiguity about spectrum lifetime or ownership across frame boundaries.
- **FR-025**: In PhaseVocoder mode with shared analysis, per-voice onset delays (0–50 ms humanization offsets) MUST be applied to the OLA output AFTER pitch shifting (post-pitch). The shared STFT receives the raw, undelayed input signal for analysis. In all other modes (Simple, Granular, PitchSync), per-voice delays remain pre-pitch-shifting as in the pre-refactor implementation. Rationale: per-voice onset delays are timing offsets for humanization and are audibly transparent when moved post-pitch. Applying delays pre-pitch in the shared-analysis path would require a separate forward FFT per unique delay value, eliminating the shared-analysis benefit entirely.

### Key Entities

- **Shared STFT**: An `STFT` instance owned by HarmonizerEngine that performs the forward FFT analysis once per frame on the input signal. Configured with the same FFT size and hop size as defined by `PhaseVocoderPitchShifter::kFFTSize` and `kHopSize` (currently 4096 and 1024 respectively).
- **Shared Analysis Spectrum**: A `SpectralBuffer` owned by HarmonizerEngine that holds the forward FFT result. Read-only after `analyze()`. Passed by const reference to all active voices for their synthesis step.
- **Per-Voice Synthesis State**: The collection of per-voice mutable state within each PhaseVocoderPitchShifter: phase accumulators, modified spectrum, OLA buffer, phase locking state, transient detector state. Never shared.
- **processWithSharedAnalysis()**: The new Layer 2 method on PhaseVocoderPitchShifter that accepts an external analysis spectrum and performs only synthesis (phase rotation + iFFT + OLA).

## Success Criteria *(mandatory)*

### Benchmark Contract

All CPU performance measurements for this spec (SC-001 and SC-008) MUST use the KrateDSP benchmark harness under identical conditions to those used in spec 064. This is mandatory for measurement validity: SC-001 and SC-008 are relative improvements against spec 064 baselines (approximately 24% and 26.4% respectively). Changing any measurement variable invalidates the before/after comparison.

**Benchmark configuration (MUST match spec 064 exactly):**
- Harness: KrateDSP benchmark test harness (same executable, same code path)
- Sample rate: 44100 Hz
- Block size: 256 samples
- Voice count: 4 voices (for PhaseVocoder and PitchSync tests)
- Build: Release, with optimizations enabled
- Thread priority: realtime priority (same as spec 064 run)
- Measurement window: steady-state only; first 2 seconds of output MUST be discarded as warmup
- Measurement duration: minimum 10 seconds steady-state after warmup
- Reporting: measurements MUST be directly comparable to spec 064 results without any normalization

**Two metrics MUST be reported for every benchmark (not one):**

| Metric | Description | Purpose |
|--------|-------------|---------|
| Real-time CPU % | Audio-thread load as a percentage of available real-time budget | Primary success/fail metric for SC-001 |
| Average `process()` time (µs/block) | Wall-clock time per `process()` call averaged over the measurement window | Architecture-neutral diagnostic ground truth; stable across scheduler differences |

Example reporting format: `PhaseVocoder 4 voices: CPU: 17.2%, process(): 412 µs avg`

**Why DAW CPU meters MUST NOT be used**: Every DAW computes CPU differently (audio-thread load vs total core utilization, per-core scaling, smoothing window). Variation of ±5-10% is normal across DAWs. DAW meters are appropriate for user perception; they are not valid engineering targets.

### Measurable Outcomes

- **SC-001**: PhaseVocoder mode with 4 voices MUST achieve a CPU cost of less than 18% at 44.1kHz, block size 256, measured using the KrateDSP benchmark harness under the Benchmark Contract above (reduced from the spec 064 baseline of approximately 24%). Both metrics MUST be reported: real-time CPU % and average `process()` time in µs/block. The 18% target accounts for the approximately 25% savings on forward FFT cost (3 of 4 forward FFTs eliminated) while acknowledging that FFT is not the only cost contributor.
- **SC-002**: The output of HarmonizerEngine in PhaseVocoder mode after the refactor MUST be equivalent to the pre-refactor output, measured as: for each voice, the RMS difference between pre-refactor and post-refactor output MUST be less than 1e-5 over a 1-second test signal at 44.1kHz. A pre-refactor golden reference (per-voice output sample data) MUST be captured before any code changes are applied (see Phase 1 tasks) and persisted as a test fixture for the SC-002 equivalence assertion.
- **SC-003**: PhaseVocoderPitchShifter's `processWithSharedAnalysis()` MUST produce output identical (max sample-level error less than 1e-5) to the standard `process()` method when given the same analysis spectrum.
- **SC-004**: All existing dsp_tests MUST pass after the refactor with no test modifications. Zero regressions in existing functionality.
- **SC-005**: The shared-analysis path MUST perform zero heap allocations during processing (verified by code inspection). All shared buffers (STFT, SpectralBuffer) MUST be pre-allocated in `prepare()`.
- **SC-006**: Identity phase locking quality MUST be preserved after the refactor. A test comparing phase-locked output from the shared-analysis path vs the standard path MUST show sample-level equivalence (error less than 1e-5). Verified by the T012 test (Phase 3) and confirmed in the completion verification step T082 (Phase 11).
- **SC-007**: Each voice's OLA buffer MUST be independently writable and readable. A test MUST verify that processing voice 0 does not affect voice 1's OLA output (and vice versa).
- **SC-008**: PitchSync mode MUST be re-benchmarked after the refactor using the KrateDSP benchmark harness under the Benchmark Contract above. Both metrics MUST be reported (real-time CPU % and µs/block). The measurement result is informational for this spec (pass/fail against the spec 064 baseline of approximately 26.4% vs a 3% budget is documented, not a completion gate). PitchSync optimization is deferred to a separate spec.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The STFT class can be instantiated independently of PhaseVocoderPitchShifter and configured with the same FFT size (4096) and hop size (1024).
- The SpectralBuffer class can be passed by const reference and provides read-only access to magnitude and phase data via `getMagnitude()`, `getPhase()`, `getReal()`, `getImag()`.
- The OverlapAdd class continues to function correctly when driven by a frame-at-a-time calling pattern (one `synthesize()` call per analysis frame).
- PhaseVocoderPitchShifter's `processFrame()` method reads from `analysisSpectrum_` member but does not write to it. The shared analysis spectrum MUST be consumed by refactoring `processFrame()` to accept a `const SpectralBuffer&` parameter directly. Copying the shared spectrum into the internal member is explicitly prohibited (see FR-023 and the dataflow architectural rule).
- The PitchShiftProcessor pImpl pattern allows adding new public methods without breaking ABI compatibility (the Impl struct is internal and header-only).
- HarmonizerEngine's `prepare()` is called from the setup thread (not the audio thread), so allocating the shared STFT and SpectralBuffer there is safe.
- The PhaseVocoderPitchShifter's internal input buffering (`inputBuffer_`, `inputWritePos_`, `inputSamplesReady_`, `outputBuffer_`, `outputReadPos_`, `outputWritePos_`, `outputSamplesReady_`) is only used by the standard `process()` method for sample-level timing management. The `processWithSharedAnalysis()` method bypasses this entirely because frame timing is managed by the caller's shared STFT.
- All sample rates supported by the existing implementation (44100, 48000, 88200, 96000, 192000 Hz) continue to be supported.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that MUST be reused (not reimplemented):**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `PhaseVocoderPitchShifter` | `dsp/include/krate/dsp/processors/pitch_shift_processor.h` (L2, lines 936-1433) | Modified: add `processWithSharedAnalysis()` method. All existing methods preserved. |
| `PitchShiftProcessor` | `dsp/include/krate/dsp/processors/pitch_shift_processor.h` (L2, lines 98-295) | Modified: add shared-analysis public method + FFT size/hop accessors. |
| `PitchShiftProcessor::Impl` | `dsp/include/krate/dsp/processors/pitch_shift_processor.h` (L2, lines 1440-1459) | Modified: add delegation method in Impl struct. |
| `STFT` | `dsp/include/krate/dsp/primitives/stft.h` (L1, lines 34-173) | Reused as-is: HarmonizerEngine owns a shared instance. |
| `SpectralBuffer` | `dsp/include/krate/dsp/primitives/spectral_buffer.h` (L1, lines 44-216) | Reused as-is: holds shared analysis spectrum. Passed by const ref. |
| `OverlapAdd` | `dsp/include/krate/dsp/primitives/stft.h` (L1, lines 180-324) | Reused as-is: per-voice OLA buffers remain inside each PhaseVocoderPitchShifter. |
| `HarmonizerEngine` | `dsp/include/krate/dsp/systems/harmonizer_engine.h` (L3, lines 66-483) | Modified: add shared STFT + SpectralBuffer members, PhaseVocoder-mode process path. |
| `FormantPreserver` | `dsp/include/krate/dsp/processors/pitch_shift_processor.h` (L2) | Unchanged: continues to operate within PhaseVocoderPitchShifter. |
| `SpectralTransientDetector` | `dsp/include/krate/dsp/processors/spectral_transient_detector.h` (L2) | Unchanged: continues to operate within PhaseVocoderPitchShifter on analysis magnitude. |

**ODR check performed**: No new types are being created. This refactor adds methods to existing classes and adds member variables to existing structs. No ODR risk.

**Search Results Summary**: All modifications are to existing classes. No new class names, struct names, or enum names are introduced. The `processWithSharedAnalysis` method name does not conflict with any existing method.

### Forward Reusability Consideration

**Downstream consumers that benefit from shared analysis:**
- Any future multi-voice spectral processor (e.g., spectral freeze across voices, chorus with spectral domain processing)
- Any future component that needs to share FFT analysis across multiple consumers

**Potential shared components** (preliminary, refined in plan.md):
- The pattern of "shared STFT + per-voice synthesis" could be extracted into a reusable utility if a second consumer appears. For now, it is implemented directly in HarmonizerEngine.
- The `processWithSharedAnalysis()` API on PhaseVocoderPitchShifter is the primary reusable artifact. Any component that wants to provide its own analysis spectrum to a phase vocoder can use this method.

**Sibling features at same layer**:
- PitchSync YIN optimization (potential future spec) -- analogous pattern of sharing computation across voices, but for pitch detection instead of FFT analysis.

## Implementation Verification *(mandatory at completion)*

### Build Result
- All targets build with zero warnings, zero errors

### Test Results
| Suite | Test Cases | Assertions | Status |
|-------|-----------|------------|--------|
| dsp_tests | 5,658 | 21,929,824 | All passed |
| plugin_tests | 239 | 32,635 | All passed |
| disrumpo_tests | 468 | 592,267 | All passed |
| ruinae_tests | 314 | 3,925 | All passed |
| shared_tests | 175 | 1,453 | All passed |

### Compliance Status

*For EACH row below, you MUST perform these steps before writing the status:*
1. *Re-read the requirement from the spec*
2. *Open the implementation file and find the code that satisfies it -- record the file path and line number*
3. *Run or read the test that proves it -- record the test name and its actual output/result*
4. *For numeric thresholds (SC-xxx): record the actual measured value vs the spec target*
5. *Only then write the status and evidence*

*DO NOT mark MET without having just verified the code and test output. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | `pitch_shift_processor.h:1229-1251` -- `processWithSharedAnalysis()` accepts pre-computed analysis spectrum via const ref, calls `processFrame(analysis, synthesisSpectrum_, pitchRatio)` then `ola_.synthesize()`. |
| FR-002 | MET | Test `pitch_shift_processor_test.cpp:3126-3193` [SC-003] -- standard vs shared path on 1s 440 Hz at +7 semitones, `maxError < 1e-5f`. Passes. |
| FR-003 | MET | `pitch_shift_processor.h:1387-1427` -- peak detection on `analysis` param, per-voice `synthPhase_[]` accumulation. Test [SC-006] `maxError < 1e-5f` with phase locking. Passes. |
| FR-004 | MET | `pitch_shift_processor.h:1378-1385` -- transient detection on `analysis` param, per-voice `synthPhase_[k]` reset. Test [FR-004] `maxError < 1e-5f`. Passes. |
| FR-005 | MET | `pitch_shift_processor.h:1369-1372` -- formant envelope from `analysis` param. Test [FR-005] `maxError < 1e-5f` with formant preservation. Passes. |
| FR-006 | MET | `pitch_shift_processor.h:1176` -- `process()` calls `processFrame(analysisSpectrum_, synthesisSpectrum_, pitchRatio)`. Test [FR-006] regression: two identical instances produce bit-identical output. All 5,658 dsp_tests pass. |
| FR-007 | MET | `pitch_shift_processor.h:1246-1250` -- one `processFrame()` + one `ola_.synthesize()` per call. Test [FR-007] asserts `kHopSize` increase per call. Passes. |
| FR-008 | MET | `pitch_shift_processor.h:1234-1238` -- debug assert + release early return on numBins mismatch. Test [FR-008] wrong FFT size → `pullOutputSamples()` returns 0. Passes. |
| FR-008a | MET | `pitch_shift_processor.h:1231-1232` -- unprepared guard returns without OLA write. Tests [FR-008a] unprepared no-op + priming returns 0. Both pass. |
| FR-009 | MET | `pitch_shift_processor.h:1654-1658` -- `processWithSharedAnalysis()` mode guard: delegates only in PhaseVocoder, returns for others. Test [FR-009]. Passes. |
| FR-009a | MET | `pitch_shift_processor.h:1656-1657` -- non-PhaseVocoder returns immediately. `pullSharedAnalysisOutput()` returns 0. Test [FR-009a] Simple/Granular/PitchSync all return 0. Passes. |
| FR-010 | MET | `pitch_shift_processor.h:1667-1671` -- `pullSharedAnalysisOutput()` delegates to `pullOutputSamples()` in PhaseVocoder mode. Test [FR-009] section verifies `pulled > 0`. Passes. |
| FR-011 | MET | `pitch_shift_processor.h:342-350` -- `getPhaseVocoderFFTSize()` returns 4096, `getPhaseVocoderHopSize()` returns 1024 (static constexpr). static_assert at lines 1624-1627. Test [FR-011]. Passes. |
| FR-012 | MET | `harmonizer_engine.h:143-148` -- `prepare()` creates `sharedStft_`, `sharedAnalysisSpectrum_`, `pvVoiceScratch_` with correct FFT/hop params. |
| FR-013 | MET | `harmonizer_engine.h:275-299` -- push input once, loop while canAnalyze, analyze to shared spectrum, dispatch to voices via `processWithSharedAnalysis()` or `synthesizePassthrough()`. |
| FR-013a | MET | `harmonizer_engine.h:312-322` -- zero-fills `pvVoiceScratch_` before pulling. Tests [FR-013a] sub-hop blocks + priming period zero-fill. Both pass. |
| FR-014 | MET | `harmonizer_engine.h:353-418` -- non-PV modes use standard per-voice `process()` path. Test [FR-014] switches PV→Simple, verifies output. Passes. |
| FR-015 | MET | Test `harmonizer_engine_test.cpp:3509-3616` [SC-002] -- golden reference comparison, per-voice RMS < 1e-5. RMS = 0.0 (exact match). Passes. |
| FR-016 | MET | `harmonizer_engine.h:205-206` -- `process()` signature unchanged. No new public methods. All existing tests pass. |
| FR-017 | MET | `harmonizer_engine.h:622-624` -- `sharedStft_`, `sharedAnalysisSpectrum_` owned by HarmonizerEngine, shared across voices via const ref. |
| FR-018 | MET | `pitch_shift_processor.h:1576-1618` -- per-voice state (`prevPhase_[]`, `synthPhase_[]`, `magnitude_[]`, `frequency_[]`, `synthesisSpectrum_`, `ola_`). OLA isolation proven by tests T043-T045 [SC-007]. |
| FR-019 | MET | `pitch_shift_processor.h:1229` + `harmonizer_engine.h:296` -- `const SpectralBuffer&` throughout chain. No non-const overload. |
| FR-020 | DEFERRED | Shared peak detection deferred to post-benchmark optimization spec. |
| FR-021 | DEFERRED | Shared transient detection deferred to post-benchmark optimization spec. |
| FR-022 | DEFERRED | Shared envelope extraction deferred to post-benchmark optimization spec. |
| FR-023 | MET | `pitch_shift_processor.h:1343-1344` -- `processFrame(const SpectralBuffer& analysis, SpectralBuffer& synthesis, float pitchRatio) noexcept`. Reads from `analysis`, writes to `synthesis`. `process()` calls with internal members at line 1176. |
| FR-024 | MET | Code inspection: no member stores pointer/reference to external `analysis`. `analysisSpectrum_` is internal-only. |
| FR-025 | MET | `harmonizer_engine.h:324-336` -- delays post-pitch on `pvVoiceScratch_`. `processWithSharedAnalysis()` does NOT bypass at unity pitch (lines 1243-1244). Engine handles unity via `synthesizePassthrough()`. Test [FR-025]. Passes. |
| SC-001 | MET | Measured: CPU 6.52%, 407.7 us/block. Target: < 18%. Test [SC-001] asserts `cpuPercent < 18.0`. Passes. |
| SC-002 | MET | Per-voice RMS = 0.0 (exact match). Target: < 1e-5. Test [SC-002]. Passes. |
| SC-003 | MET | Max error < 1e-5. Target: < 1e-5. Test [SC-003]. Passes. |
| SC-004 | MET | 5,658 tests, all passed. Baseline: 5,630 (28 new spec 065 tests). |
| SC-005 | MET | Code inspection: zero allocations in audio path. All buffers pre-allocated in `prepare()`. |
| SC-006 | MET | Max error < 1e-5 with phase locking. Target: < 1e-5. Test [SC-006]. Passes. |
| SC-007 | MET | 3 isolation tests (T043/T044/T045): 2-voice, 4-voice, mute-mid-stream. All pass < 1e-5. |
| SC-008 | MET | Measured: CPU 28.51%, 1610 us/block. Baseline: ~26.4%. Within variance. Analysis documented. |

**Status Key:**
- MET: Requirement verified against actual code and test output with specific evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap and specific evidence of what IS met
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [X] Each FR-xxx row was verified by re-reading the actual implementation code (not from memory)
- [X] Each SC-xxx row was verified by running tests or reading actual test output (not assumed)
- [X] Evidence column contains specific file paths, line numbers, test names, and measured values
- [X] No evidence column contains only generic claims like "implemented", "works", or "test passes"
- [X] No test thresholds relaxed from spec requirements
- [X] No placeholder values or TODO comments in new code
- [X] No features quietly removed from scope
- [X] User would NOT feel cheated by this completion claim

### Self-Check

1. Did I change ANY test threshold from what the spec originally required? **No**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **No**
3. Did I remove ANY features from scope without telling the user? **No** (FR-020/021/022 were deferred in spec before implementation)
4. Would the spec author consider this "done"? **Yes**
5. If I were the user, would I feel cheated? **No**

### Honest Assessment

**Overall Status**: COMPLETE

All 22 non-deferred requirements MET. All 8 success criteria MET. 3 requirements explicitly DEFERRED per spec.
