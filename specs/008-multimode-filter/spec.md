# Feature Specification: Multimode Filter

**Feature Branch**: `008-multimode-filter`
**Created**: 2025-12-23
**Status**: Draft
**Layer**: 2 (DSP Processors)
**Input**: User description: "Layer 2 DSP Processor - Multi-mode filter with LP/HP/BP/Notch/Allpass/Shelf modes, selectable slopes (12/24/36/48 dB/oct), coefficient smoothing for click-free modulation, and optional pre-filter drive/saturation. Composes existing Biquad and OnePoleSmoother primitives."

## Overview

The Multimode Filter is a Layer 2 DSP Processor providing a complete filter module suitable for integration into delay effects, synthesizers, or standalone filtering. It composes existing Layer 1 primitives (Biquad, BiquadCascade, SmoothedBiquad, OnePoleSmoother) into a unified, real-time safe filter processor.

As a Layer 2 component, it may only depend on Layer 0/1 utilities and must be independently testable without VST infrastructure.

## User Scenarios & Testing *(mandatory)*

**Note**: For this DSP processor, "users" are developers building higher-layer DSP systems (delay engines, synths).

### User Story 1 - Basic Filtering (Priority: P1)

A DSP developer needs to add filtering to their audio signal chain. They instantiate a MultimodeFilter, set the filter type, cutoff frequency, and resonance, then process audio buffers.

**Why this priority**: Basic filtering is the core functionality - without it, the component has no purpose.

**Independent Test**: Can be fully tested by processing white noise through each filter type and verifying frequency response matches expected characteristics.

**Acceptance Scenarios**:

1. **Given** a lowpass filter at 1kHz with Q=0.707, **When** I process white noise, **Then** frequencies above 1kHz are attenuated by at least 6dB/octave.
2. **Given** a highpass filter at 1kHz with Q=0.707, **When** I process white noise, **Then** frequencies below 1kHz are attenuated by at least 6dB/octave.
3. **Given** a bandpass filter at 1kHz with Q=4, **When** I process white noise, **Then** only frequencies near 1kHz pass with significant amplitude.
4. **Given** a notch filter at 1kHz with Q=4, **When** I process white noise, **Then** frequencies near 1kHz are significantly attenuated.

---

### User Story 2 - Filter Slope Selection (Priority: P1)

A DSP developer needs different filter steepness options for different sonic characteristics. They select from 12dB/oct (gentle), 24dB/oct (classic), 36dB/oct (steep), or 48dB/oct (brick-wall-ish) slopes for LP/HP/BP/Notch filter types.

**Why this priority**: Slope selection is fundamental to filter character - 24dB/oct sounds very different from 12dB/oct.

**Note**: Slope selection only applies to Lowpass, Highpass, Bandpass, and Notch. Allpass, Shelf, and Peak types are fixed at single-stage (12 dB/oct equivalent) as cascading these types produces different effects (more phase shift, more gain, narrower bandwidth) rather than steeper slopes.

**Independent Test**: Can be tested by measuring frequency response slopes at different settings and verifying they match the specified dB/octave rolloff.

**Acceptance Scenarios**:

1. **Given** a lowpass filter configured for 12dB/oct, **When** I measure attenuation at 2x cutoff frequency, **Then** it is approximately -12dB.
2. **Given** a lowpass filter configured for 24dB/oct, **When** I measure attenuation at 2x cutoff frequency, **Then** it is approximately -24dB.
3. **Given** a lowpass filter configured for 48dB/oct, **When** I measure attenuation at 2x cutoff frequency, **Then** it is approximately -48dB.
4. **Given** slope changes at runtime, **When** processing audio, **Then** the transition is click-free (coefficient smoothing applied).

---

### User Story 3 - Filter Type Switching (Priority: P2)

A DSP developer wants users to morph between filter types for creative sound design. They switch between LP/HP/BP/Notch/Allpass/Shelf/Peak during playback.

**Why this priority**: Type switching enables creative workflows but basic filtering must work first.

**Independent Test**: Can be tested by switching filter types during a test tone and verifying no clicks/pops occur at switch points.

**Acceptance Scenarios**:

1. **Given** audio playing through a lowpass filter, **When** I switch to highpass, **Then** the transition produces no audible clicks or pops.
2. **Given** any filter type, **When** I switch to another type, **Then** internal state is handled correctly to avoid transients.
3. **Given** shelf filter types, **When** I adjust gain parameter, **Then** boost/cut is applied correctly at the specified frequency.

---

### User Story 4 - Cutoff Modulation (Priority: P2)

A DSP developer needs to modulate cutoff frequency from an LFO or envelope for classic filter sweep effects. They update cutoff frequency every sample or per-block.

**Why this priority**: Modulation is essential for musical filters but requires basic filtering to work first.

**Independent Test**: Can be tested by sweeping cutoff from 20Hz to 20kHz over 1 second and verifying smooth frequency response changes without artifacts.

**Acceptance Scenarios**:

1. **Given** cutoff modulated by an LFO at 5Hz, **When** I process audio, **Then** the filter sweeps smoothly without zipper noise.
2. **Given** cutoff jumping from 100Hz to 10kHz instantly, **When** coefficient smoothing is enabled, **Then** the transition is smooth (no clicks).
3. **Given** resonance modulated simultaneously with cutoff, **When** I process audio, **Then** both parameters sweep smoothly.

---

### User Story 5 - Pre-Filter Drive/Saturation (Priority: P3)

A DSP developer wants to add character to the filter by driving it into soft saturation before filtering, similar to analog filter circuits.

**Why this priority**: Drive adds character but is an enhancement over basic filtering.

**Independent Test**: Can be tested by increasing drive on a sine wave and measuring harmonic content before and after.

**Acceptance Scenarios**:

1. **Given** drive set to 0 (unity), **When** I process a sine wave, **Then** output is identical to input (no harmonics added).
2. **Given** drive set to 12dB, **When** I process a sine wave, **Then** soft clipping adds even harmonics.
3. **Given** drive enabled with lowpass filter, **When** I process audio, **Then** saturation occurs before filtering (correct signal chain order).

---

### User Story 6 - Self-Oscillation at High Resonance (Priority: P3)

A DSP developer wants the filter to self-oscillate at maximum resonance for classic analog synth sounds.

**Why this priority**: Self-oscillation is a creative feature that enhances musical utility.

**Independent Test**: Can be tested by setting resonance to maximum with no input and verifying a sine wave is produced at the cutoff frequency.

**Acceptance Scenarios**:

1. **Given** resonance at maximum (Q > 50), **When** input is silence, **Then** the filter produces a sine wave at the cutoff frequency.
2. **Given** cutoff modulated during self-oscillation, **When** I sweep cutoff, **Then** the oscillation pitch follows the cutoff smoothly.
3. **Given** resonance reduced from maximum, **When** below self-oscillation threshold, **Then** oscillation decays naturally.

---

### User Story 7 - Real-Time Safety (Priority: P1)

A DSP developer integrating the filter into an audio callback needs guaranteed real-time safety. The filter must never allocate memory or block during processing.

**Why this priority**: Real-time safety is a constitution-level requirement; violations cause audio glitches.

**Independent Test**: Can be verified by code inspection and profiler analysis during process calls.

**Acceptance Scenarios**:

1. **Given** a prepared MultimodeFilter, **When** calling process(), **Then** no memory allocations occur.
2. **Given** `prepare()` called before processing, **When** processing begins, **Then** all internal buffers are pre-allocated.
3. **Given** any valid input, **When** calling process methods, **Then** no exceptions are thrown (noexcept guarantee).

---

### Edge Cases

- What happens with cutoff frequency below 20Hz? Clamp to 20Hz minimum (DC blocking).
- What happens with cutoff frequency above Nyquist/2? Clamp to safe maximum (0.49 * sampleRate).
- What happens with Q = 0? Clamp to minimum safe value (0.1) to prevent division by zero.
- What happens with Q > 100? Allow but document that instability may occur.
- What happens with NaN/infinity in input? Propagate (user responsibility to sanitize).
- What happens with drive > 24dB? Soft limit to prevent extreme distortion.
- What happens switching slopes during self-oscillation? Coefficient reset may cause transient - document limitation.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: MultimodeFilter MUST provide filter types: Lowpass, Highpass, Bandpass, Notch, Allpass.
- **FR-002**: MultimodeFilter MUST provide filter types: LowShelf, HighShelf, Peak (parametric EQ).
- **FR-003**: MultimodeFilter MUST provide selectable slopes: 12, 24, 36, 48 dB/octave for Lowpass, Highpass, Bandpass, and Notch filter types only. Allpass, LowShelf, HighShelf, and Peak types are fixed at single-stage (12 dB/oct equivalent).
- **FR-004**: MultimodeFilter MUST provide cutoff frequency range: 20Hz to Nyquist/2.
- **FR-005**: MultimodeFilter MUST provide resonance (Q) range: 0.1 to 100.
- **FR-006**: MultimodeFilter MUST provide gain parameter for Shelf and Peak types: -24dB to +24dB.
- **FR-007**: MultimodeFilter MUST provide coefficient smoothing for click-free parameter changes.
- **FR-008**: MultimodeFilter MUST provide optional pre-filter drive/saturation: 0dB to 24dB.
- **FR-009**: MultimodeFilter MUST pre-allocate all memory in `prepare()` before audio processing.
- **FR-010**: MultimodeFilter MUST NOT allocate memory, throw exceptions, or block in process methods.
- **FR-011**: All public methods MUST be marked `noexcept`.
- **FR-012**: MultimodeFilter MUST support sample-by-sample processing for modulation sources.
- **FR-013**: MultimodeFilter MUST support block processing for efficiency when modulation not needed.
- **FR-014**: MultimodeFilter MUST provide `reset()` method to clear internal state without reallocation.
- **FR-015**: MultimodeFilter MUST compose existing Layer 1 primitives (Biquad, BiquadCascade, SmoothedBiquad, OnePoleSmoother).
- **FR-016**: MultimodeFilter MUST support mono operation (single channel).
- **FR-017**: Coefficient smoothing time MUST be configurable (default: 5ms).
- **FR-018**: Drive/saturation MUST be oversampled (2x minimum) to prevent aliasing.

### Non-Functional Requirements

- **NFR-001**: Filter processing MUST complete in O(N) time where N is number of samples.
- **NFR-002**: Coefficient update MUST complete in O(S) time where S is number of stages (1-4).
- **NFR-003**: Memory footprint MUST be bounded by `4 * stages * sizeof(Biquad)` for filter state.

### Key Entities

- **MultimodeFilter**: Main processor class composing primitives.
  - Attributes: filterType, slope, cutoff, resonance, gain, drive, smoothingTime
  - Operations: prepare(), process(), processSample(), setParameters(), reset()

- **FilterSlope**: Enumeration for slope selection (LP/HP/BP/Notch only).
  - Values: Slope12dB, Slope24dB, Slope36dB, Slope48dB
  - Note: Ignored for Allpass, Shelf, and Peak types (always single-stage)

- **FilterParameters**: Parameter container for atomic updates.
  - Attributes: type, slope, cutoff, resonance, gain, drive

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Lowpass filter at 1kHz attenuates 2kHz by at least slope dB (12/24/36/48 within 1dB tolerance).
- **SC-002**: Highpass filter at 1kHz attenuates 500Hz by at least slope dB (12/24/36/48 within 1dB tolerance).
- **SC-003**: Bandpass filter at 1kHz has -3dB bandwidth matching Q relationship (BW = f0/Q).
- **SC-004**: Coefficient smoothing eliminates audible clicks when sweeping cutoff 100Hz-10kHz in 100ms.
- **SC-005**: Self-oscillation produces sine wave at cutoff frequency within 1 semitone accuracy.
- **SC-006**: Drive at 12dB produces measurable THD increase (> 1%) compared to bypass.
- **SC-007**: Zero memory allocations during process callbacks (verified by code inspection).
- **SC-008**: All unit tests pass on Windows (MSVC), macOS (Clang), and Linux (GCC).

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Single-precision float (32-bit) is the primary sample format.
- Maximum 4 stages (48 dB/oct) is sufficient for plugin applications.
- Butterworth alignment is the default; Linkwitz-Riley available for shelf filters if needed.
- Drive saturation uses tanh curve (smooth clipping characteristic).
- Stereo processing handled by instantiating two MultimodeFilters (outside this spec's scope).

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that MUST be reused:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| FilterType enum | src/dsp/primitives/biquad.h | Reuse directly for type selection |
| BiquadCoefficients | src/dsp/primitives/biquad.h | Reuse for coefficient calculation |
| Biquad | src/dsp/primitives/biquad.h | Reuse as single 12dB/oct stage |
| BiquadCascade<N> | src/dsp/primitives/biquad.h | Reuse for multi-stage slopes |
| SmoothedBiquad | src/dsp/primitives/biquad.h | Reuse for coefficient interpolation |
| butterworthQ() | src/dsp/primitives/biquad.h | Reuse for Q calculation |
| linkwitzRileyQ() | src/dsp/primitives/biquad.h | Reuse for LR alignment |
| OnePoleSmoother | src/dsp/primitives/smoother.h | Reuse for parameter smoothing |
| Oversampler | src/dsp/primitives/oversampler.h | Reuse for drive saturation |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "MultimodeFilter\|MultiModeFilter" src/
grep -r "class Filter" src/dsp/
```

**Search Results Summary**: No existing MultimodeFilter found. Will create as new Layer 2 processor composing existing Layer 1 primitives listed above.

## Dependencies

- **Layer 1**: Biquad, BiquadCascade, SmoothedBiquad (biquad.h)
- **Layer 1**: OnePoleSmoother (smoother.h)
- **Layer 1**: Oversampler (oversampler.h) - for drive saturation
- **Layer 0**: constexpr math utilities (db_utils.h)
- **Constitution**: Must comply with Principle II (Real-Time Safety), Principle IX (Layered Architecture), Principle XII (Test-First)

## Out of Scope

- Multi-channel processing (handled by multiple instances)
- Stereo-linked operation (handled by caller)
- Morph between filter types (crossfade handled by caller)
- Filter FM (frequency modulation) - use external modulation
- Ladder filter topology (separate primitive if needed later)
- Oversampled filtering (only drive is oversampled)
- SVF (State Variable Filter) topology (separate primitive if needed)

---

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion.*

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
| FR-017 | | |
| FR-018 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |

### Completion Checklist

- [ ] All FR-xxx requirements verified against implementation
- [ ] All SC-xxx success criteria measured and documented
- [ ] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: NOT STARTED

**Notes**: Specification phase - implementation not yet begun.
