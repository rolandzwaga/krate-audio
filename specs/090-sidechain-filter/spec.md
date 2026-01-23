# Feature Specification: Sidechain Filter Processor

**Feature Branch**: `090-sidechain-filter`
**Created**: 2026-01-23
**Status**: Draft
**Input**: User description: "A sidechain filter processor where filter cutoff is controlled by a sidechain signal's envelope - enabling ducking and pumping effects"

## Overview

The SidechainFilter is a Layer 2 DSP processor that dynamically controls a filter's cutoff frequency based on the amplitude envelope of a sidechain signal. Unlike the existing `EnvelopeFilter` which uses self-analysis, this processor enables external signal control for classic sidechain ducking and pumping effects commonly used in electronic music production.

**Key differentiator from EnvelopeFilter**: External sidechain input allows the filter to respond to a completely different signal (e.g., kick drum) than the signal being filtered (e.g., bass synth), enabling rhythmic "pumping" and "ducking" effects that are impossible with self-sidechain.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - External Sidechain Ducking Filter (Priority: P1)

A producer wants to create a pumping bass effect where the bass synth's high frequencies are ducked whenever the kick drum hits, creating rhythmic movement and separation between kick and bass.

**Why this priority**: This is the primary use case for sidechain filtering - the ability to control one signal's filter based on another signal's dynamics is the core value proposition.

**Independent Test**: Can be fully tested by feeding a kick drum into the sidechain input while processing a sustained bass tone, verifying that the filter cutoff responds to kick transients.

**Acceptance Scenarios**:

1. **Given** a sustained bass tone as main input and kick drum as sidechain, **When** the kick hits with sidechain above threshold, **Then** the filter cutoff moves toward minCutoff (Direction::Down) or maxCutoff (Direction::Up) based on the configured direction.
2. **Given** sidechain signal returns below threshold after kick transient, **When** hold time expires, **Then** the filter cutoff smoothly returns to its resting position over the release time.
3. **Given** multiple kick hits in rapid succession, **When** sidechain re-triggers during hold phase, **Then** the hold timer resets and filter remains in ducked position.

---

### User Story 2 - Self-Sidechain Mode (Priority: P2)

A producer wants an auto-wah effect where the filter responds to the dynamics of the input signal itself, similar to EnvelopeFilter but with additional hold and lookahead features.

**Why this priority**: Provides backward compatibility with simpler envelope-following behavior while still benefiting from enhanced features like lookahead and hold.

**Independent Test**: Can be tested by processing a dynamic guitar signal in self-sidechain mode, verifying filter responds to playing dynamics.

**Acceptance Scenarios**:

1. **Given** self-sidechain mode is enabled, **When** a dynamic signal is input, **Then** the same signal is used for both envelope detection and audio processing.
2. **Given** self-sidechain mode with lookahead enabled, **When** a transient occurs, **Then** the filter begins responding before the transient reaches the main output (by lookahead amount). The sidechain path analyzes the undelayed signal while the audio path is delayed.

---

### User Story 3 - Transient Anticipation with Lookahead (Priority: P3)

An engineer wants the filter to respond to transients before they occur in the main signal, creating a more musical ducking effect that doesn't clip the beginning of transients.

**Why this priority**: Lookahead improves the musicality of the effect but requires delay line infrastructure and adds latency.

**Independent Test**: Can be tested by comparing output with and without lookahead, measuring timing of filter response relative to transients.

**Acceptance Scenarios**:

1. **Given** lookahead is set to 5ms, **When** a transient occurs in the sidechain, **Then** the filter response begins 5ms before the corresponding sample appears in the main output.
2. **Given** lookahead of N ms, **When** getLatency() is called, **Then** it returns the lookahead time converted to samples.

---

### Edge Cases

- What happens when sidechain input is silent? Filter remains at resting position: Direction::Up rests at minCutoff (filter closed), Direction::Down rests at maxCutoff (filter open).
- How does system handle sidechain signal with DC offset? Sidechain highpass filter removes DC before envelope detection.
- What happens when threshold is set very low (-60dB)? Filter responds to very quiet signals, essentially always active.
- What happens when minCutoff equals maxCutoff? No frequency sweep occurs - filter acts as static filter.
- What happens with NaN/Inf inputs? Main returns 0, sidechain is treated as silent, filter state resets.

## Clarifications

### Session 2026-01-23

- Q: What exponential mapping function should be used for envelope-to-cutoff conversion (FR-012)? → A: Option B - Simple linear-to-exponential mapping. Interpolate linearly in log space, which is the standard synthesizer approach.
- Q: What happens to envelope tracking during the hold phase (FR-016)? → A: Option A - Continue tracking envelope during hold. Filter cutoff follows envelope changes during hold, but release phase is blocked until hold expires.
- Q: How is the threshold compared to the envelope (FR-005)? → A: Option A - Convert envelope to dB for comparison. Use `envelopeDB = 20*log10(envelope)`, then compare `envelopeDB > threshold`. This is the standard dynamics processor approach.
- Q: In self-sidechain mode with lookahead enabled, should the sidechain path also be delayed? → A: Option A - No delay on sidechain path in self-sidechain mode. Sidechain analyzes the undelayed signal, audio is delayed by lookahead amount, so the filter responds ahead of the delayed audio transient.
- Q: What are the correct resting positions when sidechain is silent (envelope = 0)? → A: Option A - Correct the resting positions. Direction::Up rests at minCutoff when silent (filter closed), Direction::Down rests at maxCutoff when silent (filter open). This matches semantic expectations.

## Requirements *(mandatory)*

### Functional Requirements

**Sidechain Detection**

- **FR-001**: System MUST accept an external sidechain signal separate from the main audio input.
- **FR-002**: System MUST support self-sidechain mode where the main input is used for envelope detection.
- **FR-003**: System MUST provide configurable attack time [0.1-500ms] for envelope following.
- **FR-004**: System MUST provide configurable release time [1-5000ms] for envelope following.
- **FR-005**: System MUST provide configurable threshold [-60dB to 0dB] for triggering filter response. The linear envelope value is converted to dB (`envelopeDB = 20*log10(envelope)`) and compared: filter is triggered when `envelopeDB > threshold`.
- **FR-006**: System MUST provide sensitivity control [-24dB to +24dB] to adjust sidechain input gain before envelope detection.

**Filter Response**

- **FR-007**: System MUST provide direction control (Up or Down) determining whether envelope opens or closes the filter.
- **FR-008**: System MUST provide configurable minimum cutoff frequency [20Hz to maxCutoff-1].
- **FR-009**: System MUST provide configurable maximum cutoff frequency [minCutoff+1 to sampleRate*0.45].
- **FR-010**: System MUST provide configurable resonance/Q [0.5 to 20.0].
- **FR-011**: System MUST support multiple filter types: Lowpass, Bandpass, Highpass.
- **FR-012**: System MUST use exponential frequency mapping (linear interpolation in log-frequency space) for perceptually linear sweeps. Cutoff is computed as: `exp(lerp(log(minCutoff), log(maxCutoff), envelope))`.

**Timing Controls**

- **FR-013**: System MUST provide configurable lookahead time [0-50ms] to anticipate transients. Lookahead delays the main audio path but NOT the sidechain analysis path, allowing filter response before delayed transients reach the output.
- **FR-014**: System MUST provide configurable hold time [0-1000ms] to prevent chattering on decaying transients.
- **FR-015**: Hold time MUST delay release phase without affecting attack response.
- **FR-016**: Re-triggering during hold phase MUST reset the hold timer. During hold, envelope tracking continues (filter cutoff follows envelope changes), but the release phase is blocked until hold expires.

**Sidechain Filtering**

- **FR-017**: System MUST provide optional sidechain highpass filter [20-500Hz] for removing low frequencies before detection.
- **FR-018**: Sidechain filter cutoff MUST be independently configurable from the main filter.

**Processing**

- **FR-019**: System MUST provide sample-by-sample processing with separate main and sidechain inputs.
- **FR-020**: System MUST provide block processing with separate main and sidechain buffers.
- **FR-021**: System MUST provide in-place processing variant.
- **FR-022**: System MUST handle NaN/Inf inputs gracefully (return 0, reset state).
- **FR-023**: All processing methods MUST be noexcept for real-time safety.

**Lifecycle**

- **FR-024**: System MUST implement prepare(sampleRate, maxBlockSize) for initialization.
- **FR-025**: System MUST implement reset() to clear state without reallocation.
- **FR-026**: System MUST report processing latency via getLatency() (equals lookahead in samples).

**Monitoring**

- **FR-027**: System MUST provide getCurrentCutoff() to report current filter frequency for UI metering.
- **FR-028**: System MUST provide getCurrentEnvelope() to report current envelope value for UI metering.

### Key Entities

- **SidechainFilter**: The main processor class composing EnvelopeFollower, SVF, and DelayLine.
- **Direction**: Enum controlling envelope-to-cutoff mapping (Up = louder opens filter, rests at minCutoff when silent; Down = louder closes filter, rests at maxCutoff when silent).
- **FilterType**: Enum for filter response type (Lowpass, Bandpass, Highpass).
- **SidechainFilterState**: Internal state machine for hold time behavior (Idle, Active, Holding).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Filter cutoff responds to sidechain transients within attack time tolerance (+/-5% of configured attack).
- **SC-002**: Filter cutoff returns to resting position within release time tolerance (+/-5% of configured release).
- **SC-003**: Hold time maintains filter position for configured duration (+/-1ms accuracy).
- **SC-004**: Lookahead anticipates transients by configured amount (verified by comparing main output timing to sidechain).
- **SC-005**: Frequency sweep covers full range from minCutoff to maxCutoff when envelope goes 0 to 1.
- **SC-006**: Exponential mapping produces perceptually linear sweep (equal perceived change per envelope unit).
- **SC-007**: Processing introduces no audible artifacts during parameter changes (click-free).
- **SC-008**: Latency equals lookahead time (0 latency when lookahead is disabled).
- **SC-009**: All processing methods complete within real-time constraints (< 0.5% CPU at 48kHz stereo).
- **SC-010**: No memory allocation occurs during process() calls.
- **SC-011**: State survives prepare() with new sample rate (coefficients recalculated, state preserved or reset cleanly).
- **SC-012**: Self-sidechain mode produces identical results to external sidechain with same signal.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Sidechain signal is provided at the same sample rate as main audio.
- Users understand that lookahead adds latency to the signal path.
- Attack/release times follow the same conventions as EnvelopeFollower (JUCE-style ~99% settling).
- Threshold is compared against envelope in dB domain (envelope converted from linear to dB: `20*log10(envelope)`).

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| EnvelopeFollower | dsp/include/krate/dsp/processors/envelope_follower.h | REUSE - Core envelope detection with attack/release/sidechain HP |
| SVF | dsp/include/krate/dsp/primitives/svf.h | REUSE - TPT filter with excellent modulation stability |
| DelayLine | dsp/include/krate/dsp/primitives/delay_line.h | REUSE - For lookahead buffer |
| EnvelopeFilter | dsp/include/krate/dsp/processors/envelope_filter.h | REFERENCE - Similar pattern for self-envelope filtering |
| DuckingProcessor | dsp/include/krate/dsp/processors/ducking_processor.h | REFERENCE - Sidechain pattern with threshold/hold |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | REUSE - For cutoff smoothing if needed |
| Direction enum | dsp/include/krate/dsp/processors/envelope_filter.h | REUSE - Already defined in EnvelopeFilter |
| dbToGain/gainToDb | dsp/include/krate/dsp/core/db_utils.h | REUSE - Conversion utilities |

**Initial codebase search for key terms:**

```bash
# Searches performed:
grep -r "class.*Sidechain" dsp/  # No conflicts found
grep -r "SidechainFilter" dsp/   # No conflicts found
grep -r "Direction" dsp/include/krate/dsp/  # Found in envelope_filter.h, waveguide_resonator.h
```

**Search Results Summary**: No existing `SidechainFilter` class. The `Direction` enum exists in `EnvelopeFilter` and can be reused or re-declared locally (enum class allows same name in different class scope).

### Forward Reusability Consideration

**Sibling features at same layer** (if known):
- 091-reactive-filter (Phase 15.2) - May share threshold/dynamics detection
- 092-spectrum-morph-filter (Phase 16) - Different approach but may share SVF composition pattern

**Potential shared components** (preliminary, refined in plan.md):
- The hold-time state machine pattern from DuckingProcessor can be adapted
- EnvelopeFollower composition is a proven pattern from EnvelopeFilter

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | `processSample(mainInput, sidechainInput)` accepts separate sidechain signal |
| FR-002 | MET | `processSample(input)` uses same signal for both envelope and audio |
| FR-003 | MET | `setAttackTime()` with [0.1, 500ms] range, tested in parameter setter tests |
| FR-004 | MET | `setReleaseTime()` with [1, 5000ms] range, tested in parameter setter tests |
| FR-005 | MET | Threshold comparison in dB domain using `gainToDb()`, tested in "Threshold comparison uses dB domain" |
| FR-006 | MET | `setSensitivity()` with [-24, +24dB] range, tested in "Sensitivity gain affects threshold effectively" |
| FR-007 | MET | `setDirection()` with Up/Down modes, tested in direction-specific tests |
| FR-008 | MET | `setMinCutoff()` with [20, maxCutoff-1] range, tested in parameter setter tests |
| FR-009 | MET | `setMaxCutoff()` with [minCutoff+1, sampleRate*0.45] range, tested in parameter setter tests |
| FR-010 | MET | `setResonance()` with [0.5, 20.0] range, tested in parameter setter tests |
| FR-011 | MET | `setFilterType()` with Lowpass/Bandpass/Highpass, tested in parameter setter tests |
| FR-012 | MET | Log-space mapping in `mapEnvelopeToCutoff()`, tested in "Log-space mapping produces perceptually linear sweep" |
| FR-013 | MET | `setLookahead()` with [0, 50ms] range, lookahead delay implemented, tested in "Lookahead anticipates transients" |
| FR-014 | MET | `setHoldTime()` with [0, 1000ms] range, tested in "Hold time accuracy" |
| FR-015 | MET | Hold delays release in state machine, tested in "Hold phase delays release" |
| FR-016 | MET | Re-trigger resets hold timer, tested in "Re-trigger during hold resets hold timer" |
| FR-017 | MET | `setSidechainFilterEnabled()` controls optional HP filter on sidechain path |
| FR-018 | MET | `setSidechainFilterCutoff()` with [20, 500Hz] range, tested in parameter setter tests |
| FR-019 | MET | `processSample()` methods for sample-by-sample processing |
| FR-020 | MET | `process()` block methods with separate main/sidechain buffers |
| FR-021 | MET | `process(mainInOut, sidechain, numSamples)` in-place variant implemented |
| FR-022 | MET | NaN/Inf handling returns 0 and resets filter, tested in edge case tests |
| FR-023 | MET | All processing methods marked `noexcept` |
| FR-024 | MET | `prepare(sampleRate, maxBlockSize)` initializes all components |
| FR-025 | MET | `reset()` clears state without reallocation, tested in prepare/reset tests |
| FR-026 | MET | `getLatency()` returns lookahead samples, tested in "getLatency returns lookahead samples" |
| FR-027 | MET | `getCurrentCutoff()` returns current filter frequency |
| FR-028 | MET | `getCurrentEnvelope()` returns current envelope value |
| SC-001 | MET | Attack time tested in "Attack time controls envelope rise rate", reaches target within expected time |
| SC-002 | MET | Release time tested in "Release time within 5% tolerance", completes within tolerance |
| SC-003 | MET | Hold time tested in "Hold time accuracy", holds position for configured duration |
| SC-004 | MET | Lookahead tested in "Lookahead anticipates transients", audio delayed by lookahead amount |
| SC-005 | MET | Frequency sweep tested in "Frequency sweep covers full range", covers minCutoff to maxCutoff |
| SC-006 | MET | Exponential mapping produces perceptually linear sweep via log-space interpolation |
| SC-007 | MET | Click-free operation tested in "Click-free operation during parameter changes", max diff < 0.5 |
| SC-008 | MET | Latency equals lookahead tested in "Latency equals lookahead samples" |
| SC-009 | MET | CPU usage < 0.5% tested in "CPU usage < 0.5% single core @ 48kHz stereo" |
| SC-010 | MET | No allocations in process() - design verified in "No memory allocation during process" |
| SC-011 | MET | State survives prepare() tested in "State survives prepare() with new sample rate" |
| SC-012 | MET | Self-sidechain matches external tested in "Self-sidechain produces same results as external" |

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

**Implementation Summary:**
- 40 tests covering all user stories and edge cases
- External sidechain, self-sidechain, and lookahead modes fully implemented
- State machine (Idle/Active/Holding) correctly handles threshold crossing and hold behavior
- Log-space cutoff mapping for perceptually linear sweeps
- All parameters properly clamped with getters/setters
- NaN/Inf handling for robustness
- Click-free parameter changes via OnePoleSmoother

**Files Implemented:**
- `dsp/include/krate/dsp/processors/sidechain_filter.h` - Header-only implementation
- `dsp/tests/unit/processors/sidechain_filter_test.cpp` - Comprehensive test suite

**Architecture Documentation Updated:**
- `specs/_architecture_/layer-2-processors.md` - SidechainFilter entry added
