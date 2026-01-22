# Feature Specification: Spectral Morph Filter

**Feature Branch**: `080-spectral-morph-filter`
**Created**: 2026-01-22
**Status**: Draft
**Input**: User description: "Spectral Morph Filter - morph between two audio signals by interpolating their magnitude spectra while preserving phase from one source. Phase 12.1 of filter roadmap."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Dual-Input Spectral Morphing (Priority: P1)

A sound designer wants to blend the spectral characteristics of two different audio sources (e.g., a vocal and a synthesizer) to create hybrid timbres that smoothly transition between the two sounds.

**Why this priority**: This is the core functionality - morphing between two audio signals by interpolating their magnitude spectra. Without this, the component has no distinguishing value over standard spectral processing.

**Independent Test**: Can be fully tested by instantiating a SpectralMorphFilter, feeding two distinct audio signals (e.g., sine wave and noise), setting morph amount to 0.5, and verifying the output contains spectral characteristics from both sources. Delivers the essential cross-synthesis effect that users expect.

**Acceptance Scenarios**:

1. **Given** a SpectralMorphFilter with morph amount at 0.0, **When** processing two different signals, **Then** the output spectrum matches source A's magnitude spectrum exactly.
2. **Given** a SpectralMorphFilter with morph amount at 1.0, **When** processing two different signals, **Then** the output spectrum matches source B's magnitude spectrum exactly.
3. **Given** a SpectralMorphFilter with morph amount at 0.5, **When** processing a sine wave (source A) and white noise (source B), **Then** the output contains 50% interpolated magnitudes between the two spectra.

---

### User Story 2 - Phase Source Selection (Priority: P1)

A producer wants to control which source provides the phase information during morphing to maintain the temporal characteristics (attack, transients) of the desired signal while adopting spectral content from the other.

**Why this priority**: Phase preservation is fundamental to how the morph sounds. Using phase from a percussive source maintains transients; using phase from a sustained source creates pad-like textures. This directly affects the usability of the morphing effect.

**Independent Test**: Can be tested by morphing two signals with different phase characteristics and verifying the output retains transients from the phase source while adopting spectral shape from magnitude interpolation.

**Acceptance Scenarios**:

1. **Given** phase source set to A (a percussive drum), **When** morphing with source B (a sustained pad), **Then** the output retains the transient timing of the drum while blending in the pad's spectral content.
2. **Given** phase source set to B, **When** processing, **Then** the output uses phase from source B exclusively.
3. **Given** phase source set to Blend, **When** processing, **Then** the output phase is derived from linear interpolation of complex vectors (real/imaginary components) between A and B according to morph amount.

---

### User Story 3 - Snapshot Morphing Mode (Priority: P2)

A sound designer wants to capture a spectral "snapshot" of one signal and then morph live input against this frozen reference, enabling evolving textures where one source is static.

**Why this priority**: Single-input mode with snapshot extends usability significantly by allowing morphing with a pre-captured spectrum, which is common in spectral freeze effects and evolving pad design.

**Independent Test**: Can be tested by capturing a snapshot, then processing a different signal and verifying the output morphs between the live input and the captured spectral fingerprint.

**Acceptance Scenarios**:

1. **Given** a snapshot is captured from a vocal, **When** processing a synth pad with morph at 0.5, **Then** the output blends the live synth with the captured vocal spectrum.
2. **Given** no snapshot is captured, **When** calling process(float input), **Then** the output passes through unprocessed (morph with empty/zero spectrum has no effect).
3. **Given** a new snapshot is captured, **When** morphing continues, **Then** the previous snapshot is replaced and the new spectral fingerprint is used.

---

### User Story 4 - Spectral Pitch Shifting via Bin Rotation (Priority: P3)

A sound designer wants to shift the spectral content up or down by semitones without changing the fundamental pitch of the source, creating formant-shifted or "chipmunk/monster" effects.

**Why this priority**: Spectral shift adds creative flexibility but is an enhancement to the core morphing functionality. It can be used independently or in combination with morphing.

**Independent Test**: Can be tested by processing a known harmonic signal, applying +12 semitones shift, and verifying all harmonic peaks are shifted up by approximately one octave in frequency.

**Acceptance Scenarios**:

1. **Given** spectral shift set to +12 semitones, **When** processing a 440 Hz sine wave, **Then** the output spectrum has its primary energy near 880 Hz (one octave up).
2. **Given** spectral shift set to -12 semitones, **When** processing, **Then** the spectrum is shifted down by one octave.
3. **Given** spectral shift set to 0, **When** processing, **Then** no bin rotation occurs and spectrum remains unchanged.

---

### User Story 5 - Spectral Tilt Control (Priority: P3)

A producer wants to apply a tilt to the spectral balance during morphing, emphasizing either low or high frequencies to shape the overall brightness of the morphed result.

**Why this priority**: Spectral tilt is a common mastering/mixing technique that complements morphing but is not essential to the core cross-synthesis function.

**Independent Test**: Can be tested by applying positive tilt (+6 dB) and verifying high-frequency bins have increased magnitude relative to low-frequency bins.

**Acceptance Scenarios**:

1. **Given** spectral tilt set to +6 dB, **When** processing, **Then** high-frequency bins are boosted relative to low-frequency bins by approximately 6 dB/octave.
2. **Given** spectral tilt set to -6 dB, **When** processing, **Then** high-frequency bins are attenuated relative to low-frequency bins.
3. **Given** spectral tilt set to 0 dB, **When** processing, **Then** no tilt is applied and spectral balance is unchanged.

---

## Clarifications

### Session 2026-01-22

- Q: When spectral shift exceeds Nyquist (e.g., +24 semitones on high-frequency content), how should bins that would exceed Nyquist be handled? → A: Zero bins (bins that exceed Nyquist are set to zero magnitude)
- Q: How should "Blend" phase mode interpolate phase between sources A and B (phase wraps at 2π, making direct angle interpolation problematic)? → A: Linear interpolation of complex vectors (interpolate real/imaginary components separately, then extract phase)
- Q: How should captureSnapshot() capture the spectral fingerprint (single frame vs. averaged)? → A: Average last N frames for smoother, more musical results
- Q: Where should the spectral tilt pivot point be placed (the frequency at which tilt = 0 dB gain)? → A: 1 kHz (industry standard for tilt filters)
- Q: How should spectral shift handle fractional bin indices when converting semitone shift to bin rotation (e.g., +7 semitones maps to bin 3.7)? → A: Nearest-neighbor rounding (efficient, preserves spectral clarity without interpolation artifacts)

### Edge Cases

- What happens when FFT sizes differ between prepare() calls? (All buffers and state must be reallocated via prepare())
- How does the processor handle DC bin (bin 0) and Nyquist bin? (Process them like other bins; they have zero imaginary component by symmetry)
- What happens if process() is called before prepare()? (Return immediately without processing)
- What happens if inputA or inputB is nullptr in dual-input mode? (Treat as zero-filled input)
- How does the processor handle NaN/Inf values in input? (Reset state and output silence for that frame)
- What happens when morph amount is changed rapidly? (Smooth transitions via per-frame interpolation to avoid clicks)
- What happens when spectral shift exceeds Nyquist? (Bins that would exceed Nyquist are zeroed to prevent aliasing artifacts)

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: System MUST support FFT sizes of 256, 512, 1024, 2048, and 4096 (configurable in prepare())
- **FR-002**: System MUST implement dual-input processing mode that accepts two audio streams (inputA, inputB) and produces one output stream
- **FR-003**: System MUST implement single-input processing mode with spectral snapshot capability for morphing against captured spectrum
- **FR-004**: System MUST provide setMorphAmount(float amount) where 0.0 = source A only, 1.0 = source B only, and intermediate values interpolate magnitude spectra linearly
- **FR-005**: System MUST provide setPhaseSource(PhaseSource source) with options: A (use A's phase), B (use B's phase), Blend (interpolate complex vectors via linear interpolation of real/imaginary components)
- **FR-006**: System MUST provide captureSnapshot() to freeze the input spectrum for single-input morphing mode. When called, the system begins capturing and averaging the NEXT N frames (N configurable via setSnapshotFrameCount(), default 4 frames) to produce a smooth, stable spectral fingerprint. The snapshot is finalized after N frames have been accumulated.
- **FR-007**: System MUST provide setSpectralShift(float semitones) for pitch shifting via bin rotation, range -24 to +24 semitones, with bins exceeding Nyquist zeroed to prevent aliasing and fractional bin indices rounded to nearest integer (efficient, preserves spectral clarity)
- **FR-008**: System MUST provide setSpectralTilt(float dB) for spectral balance adjustment, range -12 to +12 dB/octave, with pivot point at 1 kHz (industry standard for tilt filters)
- **FR-009**: System MUST reuse existing STFT and OverlapAdd components from Layer 1 for analysis and resynthesis
- **FR-010**: System MUST reuse existing SpectralBuffer component from Layer 1 for spectrum storage
- **FR-011**: System MUST reuse existing window functions from Layer 0 (Hann window for COLA compliance)
- **FR-012**: System MUST implement COLA-compliant overlap-add synthesis (50% overlap with Hann window)
- **FR-013**: System MUST provide reset() to clear all internal state buffers
- **FR-014**: System MUST provide prepare(double sampleRate, size_t fftSize) for initialization (see FR-001 for valid fftSize values)
- **FR-015**: System MUST handle NaN/Inf input by resetting state and outputting silence for that frame
- **FR-016**: System MUST implement per-sample block processing via processBlock(const float* inputA, const float* inputB, float* output, size_t numSamples)
- **FR-017**: System MUST implement single-sample processing via process(float input) for snapshot mode
- **FR-018**: System MUST smooth parameter changes (morph amount, tilt) to prevent audible clicks
- **FR-019**: System MUST preserve spectral phase information accurately to maintain temporal structure
- **FR-020**: System MUST report latency equal to FFT size samples

### Key Entities

- **SpectralMorphFilter**: Main processor class implementing spectral morphing, Layer 2 processor
- **STFT**: Existing Layer 1 primitive for Short-Time Fourier Transform analysis
- **OverlapAdd**: Existing Layer 1 primitive for overlap-add synthesis
- **SpectralBuffer**: Existing Layer 1 primitive for complex spectrum storage with magnitude/phase access
- **Source**: Enum for phase source selection (A, B, Blend)

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Processing two independent 1-second mono buffers (equivalent to stereo) at 44.1kHz with FFT size 2048 completes in under 50ms on a modern CPU (< 2.5% CPU for typical use)
- **SC-002**: With morph amount at 0.0, the output magnitude spectrum matches source A within 0.1 dB RMS error
- **SC-003**: With morph amount at 1.0, the output magnitude spectrum matches source B within 0.1 dB RMS error
- **SC-004**: With morph amount at 0.5, each output bin magnitude equals the arithmetic mean of corresponding A and B bin magnitudes within 1%
- **SC-005**: Spectral shift of +12 semitones shifts all harmonic peaks by a factor of 2.0x in frequency (within 5% tolerance due to bin quantization)
- **SC-006**: Spectral tilt of +6 dB produces approximately 6 dB/octave boost above 1 kHz and 6 dB/octave cut below 1 kHz, with the pivot point at 1 kHz having 0 dB gain (within 1 dB tolerance)
- **SC-007**: COLA reconstruction error is less than -60 dB (unity gain passthrough when morphAmount=0 and phaseFrom=A)
- **SC-008**: All parameter changes complete smoothly without audible clicks or discontinuities
- **SC-009**: Single-sample process() and block processBlock() produce consistent results for equivalent input sequences
- **SC-010**: Processor reports latency equal to configured FFT size

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Target sample rates are standard audio rates (44.1kHz, 48kHz, 88.2kHz, 96kHz, 176.4kHz, 192kHz)
- Default FFT size is 2048 samples (good balance between frequency resolution and latency)
- Default hop size is 50% of FFT size (COLA-compliant with Hann window)
- Hann window is used for both analysis and synthesis (COLA verified by existing infrastructure)
- Morph amount defaults to 0.0 (source A passthrough)
- Phase source defaults to A (preserve phase from first input)
- Spectral shift defaults to 0 semitones (no shift)
- Spectral tilt defaults to 0 dB (flat response with pivot at 1 kHz)
- Parameter smoothing uses 50ms time constant (consistent with SpectralDelay implementation)
- Dual-input mode processes both inputs synchronously at the same sample rate
- Snapshot averaging uses last 4 frames by default (configurable to balance smoothness vs. responsiveness)

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| STFT | dsp/include/krate/dsp/primitives/stft.h | Direct reuse - STFT analysis with windowing |
| OverlapAdd | dsp/include/krate/dsp/primitives/stft.h | Direct reuse - COLA synthesis |
| SpectralBuffer | dsp/include/krate/dsp/primitives/spectral_buffer.h | Direct reuse - magnitude/phase storage |
| FFT | dsp/include/krate/dsp/primitives/fft.h | Direct reuse - core FFT/IFFT processing |
| Complex | dsp/include/krate/dsp/primitives/fft.h | Direct reuse - complex number operations |
| Window::generate | dsp/include/krate/dsp/core/window_functions.h | Direct reuse - Hann window generation |
| WindowType | dsp/include/krate/dsp/core/window_functions.h | Direct reuse - window type enum |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | Direct reuse - parameter smoothing |
| SpectralDelay | dsp/include/krate/dsp/effects/spectral_delay.h | Reference implementation - shows STFT processing patterns |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "class SpectralMorph" dsp/ plugins/
grep -r "spectral.*morph" dsp/ plugins/
```

**Search Results Summary**: No existing SpectralMorphFilter implementation found. The component is new but builds heavily on existing spectral processing primitives (STFT, OverlapAdd, SpectralBuffer) that are already proven in SpectralDelay.

### Forward Reusability Consideration

*Note for planning phase: When this is a Layer 2+ feature, consider what new code might be reusable by sibling features at the same layer. The `/speckit.plan` phase will analyze this in detail, but early identification helps.*

**Sibling features at same layer** (if known):
- SpectralGate (Phase 12.2) - per-bin noise gate using magnitude thresholds
- SpectralTilt (Phase 12.3) - tilt filter for spectral balance

**Potential shared components** (preliminary, refined in plan.md):
- Magnitude interpolation logic could be shared with SpectralGate for gating transitions
- Spectral tilt implementation could be extracted as a standalone utility if SpectralTilt (Phase 12.3) needs the same logic
- Bin rotation/shift logic could be useful for other spectral effects requiring pitch manipulation

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | prepare() accepts fftSize 256-4096, validates in kMinFFTSize/kMaxFFTSize |
| FR-002 | MET | processBlock(inputA, inputB, output, numSamples) implemented |
| FR-003 | MET | process(float input) with captureSnapshot() implemented |
| FR-004 | MET | setMorphAmount() with 0.0-1.0 clamping, linear interpolation |
| FR-005 | MET | PhaseSource enum (A, B, Blend) with complex vector interpolation |
| FR-006 | MET | captureSnapshot() captures next N frames (configurable via setSnapshotFrameCount) |
| FR-007 | MET | setSpectralShift() -24 to +24 semitones, nearest-neighbor rounding, Nyquist zeroing |
| FR-008 | MET | setSpectralTilt() -12 to +12 dB/octave with 1kHz pivot |
| FR-009 | MET | Uses STFT and OverlapAdd from primitives/stft.h |
| FR-010 | MET | Uses SpectralBuffer from primitives/spectral_buffer.h |
| FR-011 | MET | Uses WindowType::Hann from core/window_functions.h |
| FR-012 | MET | hopSize = fftSize/2 (50% overlap) with Hann window for COLA |
| FR-013 | MET | reset() clears all state buffers |
| FR-014 | MET | prepare(sampleRate, fftSize) initializes all components |
| FR-015 | MET | NaN/Inf detection with state reset and silence output |
| FR-016 | MET | processBlock() for block processing |
| FR-017 | MET | process(float) for single-sample snapshot mode |
| FR-018 | MET | OnePoleSmoother with 50ms time constant for morph and tilt |
| FR-019 | MET | Phase preserved via applyPhaseSelection() |
| FR-020 | MET | getLatencySamples() returns fftSize |
| SC-001 | MET | Performance test passes (<50ms for 2x 1-second @ 44.1kHz) |
| SC-002 | MET | Test "morph=0.0 outputs source A" passes |
| SC-003 | MET | Test "morph=1.0 outputs source B" passes (frequency domain verification) |
| SC-004 | MET | Test "morph=0.5 blends magnitudes" passes |
| SC-005 | MET | Test "+12 semitones doubles frequency" passes |
| SC-006 | MET | Test "+6 dB/octave boosts highs" passes |
| SC-007 | MET | Test "COLA reconstruction" passes (<-60dB error) |
| SC-008 | MET | Parameter smoothing test passes (no clicks) |
| SC-009 | MET | process() and processBlock() use same STFT/OverlapAdd chain |
| SC-010 | MET | getLatencySamples() == fftSize, test passes |

**Status Key:**
- MET: Requirement fully satisfied with test evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [X] All FR-xxx requirements verified against implementation
- [X] All SC-xxx success criteria measured and documented
- [X] No test thresholds relaxed from spec requirements
- [X] No placeholder values or TODO comments in new code
- [X] No features quietly removed from scope
- [X] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: COMPLETE

**Notes:**
- All 20 functional requirements (FR-001 through FR-020) are implemented and tested
- All 10 success criteria (SC-001 through SC-010) are verified by passing tests
- 32 test cases with 578 assertions all pass
- Full DSP test suite (2973 test cases) passes with no regressions
- No TODO/PLACEHOLDER/STUB comments in implementation
- Architecture documentation updated in specs/_architecture_/layer-2-processors.md

**Recommendation**: Ready for code review and merge
