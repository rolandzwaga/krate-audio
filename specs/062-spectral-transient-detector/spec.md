# Feature Specification: Spectral Transient Detector

**Feature Branch**: `062-spectral-transient-detector`
**Plugin**: KrateDSP (Shared DSP Library)
**Created**: 2026-02-17
**Status**: Draft
**Input**: User description: "Spectral flux-based transient detector for phase vocoder phase reset integration"
**Roadmap Reference**: [harmonizer-roadmap.md, Phase 2B](../harmonizer-roadmap.md)

## Clarifications

### Session 2026-02-17

- Q: When `prepare(numBins)` is called a second time with a different bin count, what should the detector do? → A: Reallocate internal buffer to new size and fully reset all state (running average, previous magnitudes). Subsequent `detect()` calls use the new bin count. This is consistent with other KrateDSP `prepare()` methods.
- Q: When `detect()` is called with a bin count mismatching the `prepare()` count, what should the detector do on the audio thread? → A: Debug assert in debug builds; in release, clamp to the prepared bin count and process only those bins, returning a valid (possibly degraded) result. No crash, no UB in release.
- Q: How should FR-009's flux value, running average, and detection state be exposed to callers? → A: Three separate `noexcept` getter methods: `getSpectralFlux()`, `getRunningAverage()`, `isTransient()`. Each returns the value from the most recent `detect()` call. Consistent with other KrateDSP Layer 1 primitives.
- Q: How should the SC-004 "5ms after onset" measurement window be aligned? → A: Onset is the first output sample of the STFT frame for which `isTransient()` returned `true`. The 5ms measurement window starts from that sample. This tests the full integrated pipeline end-to-end.
- Q: Should the first-frame detection (where previous magnitudes are all zero, producing artificially high flux) be suppressed or reported? → A: Suppress it. After `prepare()` or `reset()`, skip the threshold comparison on the first `detect()` call and always return `false`. The running average is still seeded from the first frame's flux. This prevents a predictable false positive on every cold start.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Detect Spectral Transients in Magnitude Spectra (Priority: P1)

A DSP developer uses the SpectralTransientDetector to identify transient onsets (drum hits, consonant attacks, plucked string onsets) by analyzing frame-to-frame changes in the magnitude spectrum. The detector computes half-wave rectified spectral flux -- the sum of only *positive* magnitude differences between consecutive frames -- and compares it against an adaptive threshold derived from a running average. When spectral flux exceeds the threshold, the frame is classified as a transient.

**Why this priority**: This is the core detection algorithm. Without reliable transient detection, no downstream integration (phase reset, transient preservation) can function. Half-wave rectification is critical because only positive magnitude changes (energy onsets) indicate transients; negative changes (energy decay) do not (Duxbury et al., 2002; Dixon, 2006).

**Independent Test**: Can be fully tested by feeding known magnitude spectrum sequences (impulse, sustained tone, drum pattern) and verifying detection/non-detection outcomes. Delivers a standalone, reusable onset detector for any spectral processing pipeline.

**Acceptance Scenarios**:

1. **Given** a sequence of magnitude spectra where all frames are identical (sustained sine), **When** frames are processed through the detector, **Then** no transient is detected on any frame.
2. **Given** a sequence of zero-magnitude frames followed by a frame with significant energy across multiple bins (impulse onset), **When** the onset frame is processed, **Then** a transient is detected on that frame. (The zero-magnitude frames precede the onset, so the onset is not frame 0 of the stream; first-frame suppression does not apply.)
3. **Given** a drum pattern with alternating impulse and silence frames (preceded by at least one priming frame so the onset is not frame 0), **When** all frames are processed sequentially, **Then** each impulse onset frame is detected as a transient, and silence frames are not.

---

### User Story 2 - Configure Detection Sensitivity (Priority: P2)

A DSP developer adjusts the detector's sensitivity to suit different audio material. For percussive music with clear transients, a moderate threshold multiplier (e.g., 1.5x running average) works well. For dense, polyphonic material where transients are subtler, a lower threshold (e.g., 1.2x) catches more onsets. For material with frequent low-level spectral changes (e.g., vibrato), a higher threshold (e.g., 2.0x) reduces false positives.

**Why this priority**: Different audio material requires different sensitivity. A single fixed threshold would either miss soft transients or produce false positives on sustained tonal material. This configurability is essential for production use.

**Independent Test**: Can be tested by feeding the same input sequence at different threshold settings and verifying that higher thresholds produce fewer detections and lower thresholds produce more.

**Acceptance Scenarios**:

1. **Given** a mixed signal with both strong drum hits and subtle guitar plucks, **When** threshold is set to 2.0x, **Then** only strong drum hits are detected.
2. **Given** the same mixed signal, **When** threshold is set to 1.2x, **Then** both drum hits and guitar plucks are detected.
3. **Given** a sustained vibrato signal, **When** threshold is set to 1.5x (default), **Then** no false transients are detected from the periodic spectral modulation.

---

### User Story 3 - Integrate with Phase Vocoder for Phase Reset (Priority: P3)

A DSP developer integrates the SpectralTransientDetector into the PhaseVocoderPitchShifter's frame processing pipeline. When the detector flags a transient, the phase vocoder resets its synthesis phases to match the analysis phases for that frame, preserving transient sharpness. When no transient is detected, normal phase accumulation (with optional phase locking) proceeds as usual.

**Why this priority**: Phase reset integration is the primary *motivation* for this component, but it depends on the detector working correctly first (US-1, US-2). It modifies an existing, well-tested component (PhaseVocoderPitchShifter), so it carries integration risk and is appropriately prioritized after standalone functionality is proven.

**Independent Test**: Can be tested by pitch-shifting a signal containing clear transients (e.g., drum loop) with and without phase reset enabled. The transient sharpness (peak-to-RMS ratio) of the output should be measurably higher with phase reset enabled.

**Acceptance Scenarios**:

1. **Given** a pitch-shifted drum loop with phase reset disabled, **When** the same loop is processed with phase reset enabled, **Then** the output transient sharpness (peak-to-RMS ratio in the first 5ms after onset) is at least 2 dB higher with phase reset. The 5ms window begins at the first output sample of the STFT frame for which `isTransient()` returned `true`; RMS is computed over that window and peak is the maximum absolute sample value within it.
2. **Given** a sustained tonal signal with no transients, **When** processed with phase reset enabled, **Then** the output is identical to processing without phase reset (no unnecessary resets).
3. **Given** phase reset is enabled and then disabled during processing, **When** switching between modes, **Then** no audible clicks or artifacts occur at the transition.

---

### Edge Cases

- What happens when the detector receives the very first frame (no previous magnitudes for comparison)? The threshold comparison is skipped entirely and `detect()` returns `false`, suppressing the guaranteed false positive that would otherwise occur because the previous magnitude buffer initializes to all zeros. The first frame's computed spectral flux is still used to seed the running average for subsequent frames.
- What happens when all bins have zero magnitude (silence)? Spectral flux is zero, no transient is detected, and the running average remains stable (does not decay to an arbitrarily small value that causes false positives on any subsequent energy).
- What happens when a single bin has a large magnitude increase but all others are stable? The spectral flux contribution from one bin alone should typically be below the adaptive threshold (which is based on broadband flux), preventing false detection from isolated bin noise.
- What happens when the running average is very close to zero (e.g., after prolonged silence) and then sudden input arrives? A minimum floor on the running average prevents division-by-zero or ultra-sensitive detection. The first real onset after prolonged silence should still be detected.
- What happens with very short FFT sizes (few bins) vs. large FFT sizes (many bins)? The detector must work across all supported FFT sizes (512 to 8192 bins). Spectral flux magnitude scales with bin count, but the adaptive threshold (based on running average of the same quantity) normalizes this automatically.
- What happens when `prepare()` is called again with a different bin count (e.g., host changes FFT size mid-session)? The detector reallocates its internal buffer to the new size and fully resets all state (previous magnitudes, running average, detection flag), identical to the initial `prepare()` call. This is consistent with KrateDSP `prepare()` conventions.
- What happens when `detect()` is called with a bin count that does not match the `prepare()` count (programming error)? In debug builds a debug assertion fires to surface the bug. In release builds the detector silently clamps processing to `min(passedCount, preparedCount)` bins and returns a valid result with no UB or out-of-bounds access.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The detector MUST compute half-wave rectified spectral flux per frame: `SF(n) = sum(max(0, |X_n[k]| - |X_{n-1}[k]|))` for all bins k, where only positive magnitude differences (energy increases) contribute to the flux value. This follows the established onset detection methodology (Duxbury et al., 2002; Dixon, 2006).

- **FR-002**: The detector MUST maintain an adaptive threshold based on a smoothed running average of past spectral flux values: `runningAvg(n) = alpha * runningAvg(n-1) + (1 - alpha) * SF(n)`, where `alpha` is the smoothing coefficient (default: 0.95). A transient is detected when `SF(n) > threshold * runningAvg(n)`.

- **FR-003**: The detector MUST provide a configurable threshold multiplier that controls sensitivity. Default value: 1.5x the running average. Valid range: 1.0 to 5.0. Lower values increase sensitivity (more detections); higher values decrease sensitivity (fewer detections).

- **FR-004**: The detector MUST provide a configurable smoothing coefficient for the running average. Default value: 0.95. Valid range: 0.8 to 0.99. Higher values make the running average slower-moving (more historical context); lower values make it more responsive to recent changes.

- **FR-005**: The detector MUST accept a magnitude spectrum as a contiguous array of floats and a bin count. It MUST NOT require or depend on phase information, Cartesian complex data, or time-domain audio samples.

- **FR-006**: The detector MUST store the previous frame's magnitudes internally and update them after each detection call, enabling frame-by-frame streaming operation without external state management.

- **FR-007**: The detector MUST provide a `prepare(numBins)` method that allocates all necessary internal storage (previous magnitudes buffer). If `prepare()` is called a second time with a different bin count, it MUST reallocate to the new size and fully reset all internal state (previous magnitudes, running average, detection flag) — identical behavior to the first call. All subsequent `detect()` calls MUST NOT perform any memory allocation.

- **FR-008**: The detector MUST provide a `reset()` method that clears all internal state (previous magnitudes, running average, detection flag) to initial values, enabling clean restarts without reallocation.

- **FR-009**: The detector MUST return a `bool` transient detection result from each `detect()` call and MUST expose the values computed during the most recent `detect()` call through three separate `noexcept` getter methods: `getSpectralFlux()` (returns the raw half-wave rectified flux scalar), `getRunningAverage()` (returns the current exponential moving average), and `isTransient()` (returns the boolean detection flag). This API is consistent with other KrateDSP Layer 1 primitives.

- **FR-010**: The detector MUST suppress the transient detection result on the very first `detect()` call after `prepare()` or `reset()`, always returning `false` for that frame regardless of spectral flux magnitude. The previous magnitude buffer is initialized to zeros, so the first frame's flux would otherwise be artificially high and produce a guaranteed false positive on every cold start. The running average MUST still be seeded from the first frame's computed flux value so that subsequent threshold comparisons have a meaningful baseline. From the second frame onward, normal threshold comparison resumes.

- **FR-011**: The detector MUST enforce a minimum floor on the running average (1e-10) to prevent division-by-zero or numerically unstable threshold comparisons after prolonged silence.

- **FR-012**: The PhaseVocoderPitchShifter MUST integrate the SpectralTransientDetector into its `processFrame()` method. When a transient is detected, the synthesis phase array MUST be reset to match the analysis phase: `synthPhase_[k] = analysisPhase[k]` for all bins. This follows the phase reset technique described by Duxbury et al. (2002) and Roebel (2003).

- **FR-013**: The PhaseVocoderPitchShifter MUST provide a method to enable/disable transient-aware phase reset independently of phase locking. Both features should be independently toggleable with no interference.

- **FR-014**: The detector MUST operate correctly for all FFT sizes supported by the existing spectral pipeline (FFT sizes from 512 to 8192, yielding 257 to 4097 bins).

- **FR-015**: All methods on the detector MUST be `noexcept` and safe for use on the audio thread (no allocations, no exceptions, no locks, no I/O in the `detect()` path).

- **FR-016**: When `detect()` is called with a bin count that does not match the bin count from the most recent `prepare()` call, the detector MUST: (a) fire a debug assertion in debug builds to surface the programming error, and (b) in release builds, silently clamp processing to `min(passedCount, preparedCount)` bins and return a valid (possibly degraded) detection result. No undefined behavior, crash, or memory access out of bounds is permitted in any build configuration.

### Key Entities

- **SpectralTransientDetector**: A Layer 1 primitive that operates on magnitude spectra (arrays of float). It maintains internal state (previous magnitudes, running average) and produces a per-frame transient detection result. It is a standalone component with no dependencies beyond Layer 0 core utilities.

- **Spectral Flux**: A scalar value computed per frame representing the total positive magnitude change across all frequency bins. It quantifies how much "new energy" appeared in the spectrum relative to the previous frame. Only positive differences (half-wave rectified) contribute, filtering out energy decay which does not indicate onsets.

- **Adaptive Threshold**: A time-varying threshold derived from an exponentially-weighted moving average of past spectral flux values, multiplied by a configurable sensitivity factor. It adapts to the overall energy dynamics of the signal, enabling the detector to work across varying loudness levels without manual calibration.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The detector correctly identifies transients in a synthetic impulse test (silence followed by broadband onset) with 100% detection rate and 0% false positive rate on sustained tones.

- **SC-002**: The detector produces zero false positives when processing 100 consecutive frames of a constant-magnitude sustained sinusoidal spectrum at default sensitivity (threshold = 1.5x).

- **SC-003**: The detector correctly identifies each onset in a synthetic drum pattern (alternating impulse/silence, minimum 5 onsets) with 100% detection rate at default sensitivity.

- **SC-004**: When integrated with the PhaseVocoderPitchShifter, pitch-shifted transient material (drum hit) shows a measurably higher peak-to-RMS ratio in the first 5ms after onset compared to processing without phase reset (improvement target: at least 2 dB higher peak-to-RMS with phase reset enabled). The 5ms measurement window begins at the first output sample of the STFT frame for which `isTransient()` returned `true`, testing the full integrated pipeline end-to-end. RMS is computed over that same 5ms window; peak is the maximum absolute sample value within it.

- **SC-005**: The detector's per-frame processing adds less than 0.01% CPU overhead at 44.1 kHz with a 4096-point FFT (2049 bins). The operation is a single linear pass over the magnitude array with no transcendental math.

- **SC-006**: Changing the threshold multiplier between 1.0 and 5.0 produces a monotonically non-increasing number of detections on the same test signal (higher threshold = fewer or equal detections).

- **SC-007**: The detector works identically across all supported FFT sizes (512, 1024, 2048, 4096, 8192) for equivalent spectral content scaled to the appropriate bin count.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The SpectralTransientDetector receives pre-computed magnitude spectra (floats). It does not perform FFT or magnitude extraction itself. This is consistent with the existing spectral pipeline where `SpectralBuffer::getMagnitude()` or the `magnitude_[]` array in `PhaseVocoderPitchShifter::processFrame()` already provides this data.
- The existing `PhaseVocoderPitchShifter` computes magnitudes from the analysis spectrum as its first step in `processFrame()` (line 1127-1130 of `pitch_shift_processor.h`). The SpectralTransientDetector will consume this existing magnitude array -- no redundant computation required.
- A smoothing coefficient of 0.95 and threshold multiplier of 1.5x are reasonable defaults based on the spectral flux onset detection literature (Dixon, 2006; Bock & Widmer, 2013). These values will be validated empirically during testing but represent well-established starting points.
- The half-wave rectification approach (only positive differences) is preferred over full spectral flux because it specifically detects energy onsets and ignores energy decay, reducing false positives on note releases and amplitude modulation. This is the consensus approach in onset detection literature.
- Phase reset (setting `synthPhase_[k] = analysisPhase[k]`) at transient frames is the simplest effective technique for preserving transient sharpness in a phase vocoder. More advanced techniques exist (Roebel's center-of-gravity method, per-peak transient classification) but add complexity without proportional benefit for the initial implementation.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `TransientDetector` | `dsp/include/krate/dsp/processors/transient_detector.h` | **Different purpose, no conflict**. Time-domain envelope derivative analysis for modulation source (Layer 2). Operates on audio samples, not magnitude spectra. Name differs (`TransientDetector` vs `SpectralTransientDetector`). No ODR risk. |
| `SpectralBuffer` | `dsp/include/krate/dsp/primitives/spectral_buffer.h` | **Provides input data**. Lazy dual Cartesian/polar with `getMagnitude()` per bin. The detector consumes magnitude arrays that `SpectralBuffer` can produce, but accepts raw `float*` for flexibility. |
| `PhaseVocoderPitchShifter` | `dsp/include/krate/dsp/processors/pitch_shift_processor.h` | **Integration target**. The `processFrame()` method (line 1123) already computes `magnitude_[k]` for all bins. Phase reset will be added after transient detection, before pitch shift computation. The existing `synthPhase_[]` and `prevPhase_[]` arrays are the reset targets. |
| `magnitude_[]` array | `pitch_shift_processor.h` (member of `PhaseVocoderPitchShifter`) | **Direct input source**. Already computed per frame for phase vocoder processing. The detector will consume this data directly -- zero additional magnitude computation. |

**Initial codebase search for key terms:**

Search for `SpectralTransient` and `spectral_transient`: Found only in `specs/harmonizer-roadmap.md`. No existing implementation or header with this name exists in the codebase. No ODR risk.

Search for `class TransientDetector`: Found at `dsp/include/krate/dsp/processors/transient_detector.h:34`. This is the time-domain `TransientDetector` (Layer 2, `ModulationSource` subclass). It operates on individual audio samples via `process(float sample)`, uses envelope derivative analysis, and has no spectral awareness. The new `SpectralTransientDetector` has a completely different name, different layer (Layer 1), different input (magnitude spectra), different algorithm (spectral flux), and different purpose (phase reset trigger vs. modulation source).

**Search Results Summary**: No existing spectral transient detection implementation. The time-domain `TransientDetector` is a separate component with no overlap. No ODR risk identified.

### Forward Reusability Consideration

**Sibling features at same layer** (Layer 1 primitives):
- `PitchTracker` (Phase 3, harmonizer roadmap) -- also a Layer 1 primitive, but no shared code with transient detection
- `SpectralBuffer` -- already exists, used by the detector as input source

**Potential shared components** (preliminary, refined in plan.md):
- The SpectralTransientDetector could be reused by any future spectral processing that needs onset awareness (e.g., spectral freeze that preserves attack transients, spectral gating, onset-aligned granular processing)
- The half-wave rectified spectral flux computation itself could be extracted as a utility function in `spectral_utils.h` if other spectral analysis features need it, but for now a self-contained class is simpler and avoids premature abstraction

## Implementation Verification *(mandatory at completion)*

<!--
  CRITICAL: This section MUST be completed when claiming spec completion.
  Constitution Principle XVI: Honest Completion requires explicit verification
  of ALL requirements before claiming "done".

  DO NOT fill this table from memory or assumptions. Each row requires you to
  re-read the actual implementation code and actual test output RIGHT NOW,
  then record what you found with specific file paths, line numbers, and
  measured values. Generic evidence like "implemented" or "test passes" is
  NOT acceptable — it must be verifiable by a human reader.

  This section is EMPTY during specification phase and filled during
  implementation phase when /speckit.implement completes.
-->

### Compliance Status

*For EACH row below, you MUST perform these steps before writing the status:*
1. *Re-read the requirement from the spec*
2. *Open the implementation file and find the code that satisfies it — record the file path and line number*
3. *Run or read the test that proves it — record the test name and its actual output/result*
4. *For numeric thresholds (SC-xxx): record the actual measured value vs the spec target*
5. *Only then write the status and evidence*

*DO NOT mark with a checkmark without having just verified the code and test output. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| FR-003 | | |
| FR-004 | | |
| FR-005 | | |
| FR-006 | | |
| FR-007 | | |
| FR-008 | | |
| FR-009 | | |
| FR-010 | | |
| FR-011 | | |
| FR-012 | | |
| FR-013 | | |
| FR-014 | | |
| FR-015 | | |
| FR-016 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |

**Status Key:**
- MET: Requirement verified against actual code and test output with specific evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap and specific evidence of what IS met
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [ ] Each FR-xxx row was verified by re-reading the actual implementation code (not from memory)
- [ ] Each SC-xxx row was verified by running tests or reading actual test output (not assumed)
- [ ] Evidence column contains specific file paths, line numbers, test names, and measured values
- [ ] No evidence column contains only generic claims like "implemented", "works", or "test passes"
- [ ] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: [COMPLETE / NOT COMPLETE / PARTIAL]

**If NOT COMPLETE, document gaps:**
- [Gap 1: FR-xxx not met because...]
- [Gap 2: SC-xxx achieves X instead of Y because...]

**Recommendation**: [What needs to happen to achieve completion]
