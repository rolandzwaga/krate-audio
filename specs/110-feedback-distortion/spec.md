# Feature Specification: Feedback Distortion Processor

**Feature Branch**: `110-feedback-distortion`
**Created**: 2026-01-26
**Status**: Ready for Planning
**Input**: User description: "Feedback Distortion Processor - controlled feedback runaway with limiting for sustained, singing distortion. Based on DST-ROADMAP.md section 7.3."

## Clarifications

### Session 2026-01-26

- Q: What attack and release times should the soft limiter use? → A: Attack: 0.5ms, Release: 50ms
- Q: Which compression algorithm should the soft limiter use? → A: Tanh-based soft clipping (smooth saturation curve)
- Q: What Q (resonance) value should the tone filter use? → A: Q = 0.707 (Butterworth - flat, neutral response)

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Basic Feedback Distortion (Priority: P1)

A sound designer wants to create sustained, singing distortion tones from transient input material. They load the FeedbackDistortion processor, set a short delay time (5ms), moderate feedback (0.8), and moderate drive. When they feed drum hits into the processor, each hit excites a singing resonance that sustains and decays naturally, with harmonic richness from the saturation in the feedback path.

**Why this priority**: This is the core use case demonstrating the fundamental concept - feedback delay with saturation creates sustained tones that would otherwise decay. Without this working, the processor has no value.

**Independent Test**: Can be fully tested by processing transient audio through the processor and verifying that output sustains longer than input with pitched character at the feedback delay frequency.

**Acceptance Scenarios**:

1. **Given** FeedbackDistortion with 10ms delay and 0.8 feedback, **When** a short impulse is processed, **Then** the output exhibits a decaying pitched resonance at approximately 100Hz (1000ms/10ms) with harmonic overtones from saturation.
2. **Given** FeedbackDistortion with 0.5 feedback, **When** audio is processed, **Then** the resonance decays naturally without runaway.
3. **Given** FeedbackDistortion with drive at 1.0, **When** compared to drive at 4.0, **Then** higher drive produces noticeably more harmonic content in the sustained tone.

---

### User Story 2 - Controlled Runaway with Limiting (Priority: P1)

A musician wants to create self-sustaining drone textures from minimal input. They set feedback above 1.0 (e.g., 1.2) which would normally cause unbounded growth. The internal limiter catches the runaway, creating controlled chaos - the signal sustains indefinitely but remains bounded. The limiter threshold parameter allows them to control how loud the sustained signal gets.

**Why this priority**: This is the "novel aspect" from the roadmap - near-oscillation with limiter for controlled chaos. It differentiates this processor from simple feedback delays.

**Independent Test**: Can be fully tested by setting feedback > 1.0 and verifying that output sustains indefinitely but remains bounded below the limiter threshold.

**Acceptance Scenarios**:

1. **Given** FeedbackDistortion with feedback at 1.2 and limiter threshold at -6dB, **When** a brief input excites the processor, **Then** the output sustains indefinitely without decaying.
2. **Given** feedback at 1.5 (extreme runaway), **When** processing occurs for 5 seconds, **Then** the output peak never exceeds the limiter threshold (+ small overshoot margin).
3. **Given** limiter threshold set to -12dB vs -6dB, **When** both are in runaway state, **Then** the -12dB threshold produces quieter sustained output.

---

### User Story 3 - Tonal Control via Filter (Priority: P2)

A producer wants to shape the character of the sustained distortion. By adjusting the tone filter frequency in the feedback path, they can make the sustained tone brighter or darker. Higher frequencies emphasize the attack and harmonics; lower frequencies create a warmer, more muted sustain.

**Why this priority**: The tone filter adds essential creative control over the timbre of the sustained signal. Without it, the processor would have limited sonic flexibility.

**Independent Test**: Can be fully tested by processing audio with different tone frequencies and verifying audible timbral differences in the sustained output.

**Acceptance Scenarios**:

1. **Given** FeedbackDistortion with tone frequency at 1000Hz (lowpass), **When** compared to tone at 5000Hz, **Then** the 1000Hz setting produces noticeably darker, more muted sustain.
2. **Given** tone frequency at 200Hz, **When** broadband input is processed, **Then** only low frequencies sustain; high frequencies decay quickly.

---

### User Story 4 - Saturation Curve Selection (Priority: P2)

A sound designer wants different distortion characters. By selecting different saturation curves (Tanh, Tube, Diode, etc.), they get different harmonic signatures in the sustained tone - warm and smooth (Tanh), bright and aggressive (Diode), or asymmetric warmth (Tube).

**Why this priority**: Different saturation curves produce fundamentally different harmonic content, expanding the creative palette significantly.

**Independent Test**: Can be fully tested by processing the same input with different saturation curves and verifying different harmonic spectra in output.

**Acceptance Scenarios**:

1. **Given** FeedbackDistortion with Tanh saturation, **When** compared to HardClip saturation, **Then** Tanh produces smoother, rounder harmonics while HardClip produces harsher, more aggressive harmonics.
2. **Given** Tube saturation curve, **When** audio is processed, **Then** even harmonics are present (asymmetric distortion characteristic).

---

### User Story 5 - Delay Time for Pitch Control (Priority: P2)

A musician wants to tune the feedback resonance to a specific pitch. By adjusting the delay time, they control the fundamental frequency of the resonance (f = 1000 / delay_ms for ms delay times). Short delays (1-5ms) create high pitched singing; longer delays (50-100ms) create low, rumbling sustain.

**Why this priority**: Delay time directly controls the pitch of the sustained resonance, making it a fundamental creative parameter.

**Independent Test**: Can be fully tested by setting different delay times and measuring/hearing the resulting pitch of the resonance.

**Acceptance Scenarios**:

1. **Given** FeedbackDistortion with 5ms delay, **When** resonance is excited, **Then** the fundamental frequency is approximately 200Hz.
2. **Given** FeedbackDistortion with 20ms delay, **When** resonance is excited, **Then** the fundamental frequency is approximately 50Hz.
3. **Given** delay time changed from 10ms to 5ms during processing, **When** the change occurs, **Then** the pitch shifts smoothly without clicks (parameter smoothing).

---

### Edge Cases

- What happens when delay time is set below 1ms?
  - Delay time is clamped to minimum 1ms to prevent instability and ensure meaningful feedback behavior.
- What happens when delay time exceeds 100ms?
  - Delay time is clamped to maximum 100ms as specified in the roadmap API.
- What happens when feedback is set to exactly 1.0?
  - Signal sustains indefinitely with minimal growth; limiter may engage slightly for stability.
- What happens when feedback is set above 1.5?
  - Feedback is clamped to maximum 1.5 to prevent immediate saturation of the limiter.
- What happens with DC offset from asymmetric saturation?
  - Internal DC blocker removes DC offset in the feedback path.
- What happens with NaN/Inf input?
  - Processor resets internal state and returns 0.0 to prevent corruption.
- What happens when parameters change during processing?
  - All parameters are smoothed to prevent clicks (10ms smoothing time constant).
- What happens if the tone filter Q creates resonance peaks that interact with high feedback?
  - The tone filter uses Q = 0.707 (Butterworth response) which provides maximally flat passband without resonance peaks, preventing unintended feedback interactions.

## Requirements *(mandatory)*

### Functional Requirements

**Lifecycle:**
- **FR-001**: System MUST provide `prepare(double sampleRate, size_t maxBlockSize)` to initialize for processing
- **FR-002**: System MUST provide `reset()` to clear all internal state without reallocation
- **FR-003**: System MUST support sample rates from 44100Hz to 192000Hz

**Delay Time Control:**
- **FR-004**: System MUST provide `setDelayTime(float ms)` to set the feedback delay time
- **FR-005**: Delay time MUST be clamped to range [1.0, 100.0] milliseconds
- **FR-006**: Delay time changes MUST be click-free (transition energy <-60dB relative to signal) via parameter smoothing with 10ms time constant

**Feedback Control:**
- **FR-007**: System MUST provide `setFeedback(float amount)` to control feedback amount
- **FR-008**: Feedback MUST be clamped to range [0.0, 1.5] where values > 1.0 cause runaway
- **FR-009**: Feedback at or above 1.0 MUST allow sustained signal (runaway behavior) when input excites the processor (input signal >-40dB peak for >10ms)
- **FR-010**: Feedback changes MUST be click-free (transition energy <-60dB relative to signal) via parameter smoothing with 10ms time constant

**Saturation Control:**
- **FR-011**: System MUST provide `setSaturationCurve(WaveshapeType type)` to select saturation algorithm
- **FR-012**: System MUST support all WaveshapeType values from the existing Waveshaper primitive
- **FR-013**: System MUST provide `setDrive(float drive)` to control saturation intensity
- **FR-014**: Drive MUST be clamped to range [0.1, 10.0]
- **FR-015**: Drive changes MUST be click-free (transition energy <-60dB relative to signal) via parameter smoothing with 10ms time constant

**Limiter Control:**
- **FR-016**: System MUST provide `setLimiterThreshold(float dB)` to set the limiter threshold
- **FR-017**: Limiter threshold MUST be clamped to range [-24.0, 0.0] dB
- **FR-018**: The limiter MUST catch runaway feedback to prevent unbounded signal growth
- **FR-019**: The limiter MUST use soft-limiting (gradual compression approaching threshold) rather than hard clipping, per FR-019a/b/c
- **FR-019a**: The limiter MUST use 0.5ms attack time to catch runaway feedback spikes quickly
- **FR-019b**: The limiter MUST use 50ms release time to allow natural breathing without pumping artifacts
- **FR-019c**: The limiter MUST use Tanh-based soft clipping algorithm for smooth saturation character

**Tone Filter Control:**
- **FR-020**: System MUST provide `setToneFrequency(float hz)` to set the tone filter cutoff
- **FR-021**: Tone filter MUST be a lowpass filter in the feedback path
- **FR-021a**: Tone filter MUST use Q = 0.707 (Butterworth response) for neutral, flat passband response without resonance peaks
- **FR-022**: Tone frequency MUST be clamped to range [20.0, 20000.0] Hz (or sampleRate * 0.45, whichever is lower)
- **FR-023**: Tone filter changes MUST be click-free (transition energy <-60dB relative to signal) via parameter smoothing with 10ms time constant

**Processing:**
- **FR-024**: System MUST provide `process(float* buffer, size_t n) noexcept` for block processing
- **FR-025**: Processing MUST be real-time safe (no allocations, locks, exceptions, or I/O)
- **FR-026**: System MUST handle NaN/Inf inputs by resetting state and returning 0.0
- **FR-027**: System MUST flush denormals to prevent CPU spikes
- **FR-028**: System MUST include DC blocking in the feedback path to remove DC offset from asymmetric saturation

**Stability:**
- **FR-029**: System MUST remain stable (bounded output) for any valid parameter combination
- **FR-030**: Output MUST never exceed the limiter threshold by more than 3dB (soft limiter overshoot margin)

### Key Entities

- **FeedbackDistortion**: Main processor class implementing controlled feedback runaway with limiting
- **DelayLine**: Provides the feedback delay path (reuse existing primitive)
- **Waveshaper**: Provides saturation in the feedback path (reuse existing primitive)
- **Biquad**: Provides the tone filter in the feedback path (reuse existing primitive, lowpass mode with Q = 0.707 for Butterworth response)
- **DCBlocker**: Removes DC offset in the feedback path (reuse existing primitive)
- **Soft limiter logic**: Internal logic (not a separate class) that catches runaway feedback using Tanh-based gain reduction with envelope follower (0.5ms attack, 50ms release per FR-019a/b/c)

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Feedback at 0.8 produces resonance that decays to -60dB within 3-4 seconds (natural decay)
- **SC-002**: Feedback at 1.2 produces resonance that sustains above -40dB for at least 10 seconds after 100ms input burst at -6dB (1kHz sine test signal) (controlled runaway)
- **SC-003**: With feedback at 1.5 (maximum), output peak never exceeds limiter threshold + 3dB (soft limiting effectiveness)
- **SC-004**: All parameter changes (delay, feedback, drive, threshold, tone) complete smoothly within 10ms without audible clicks or pops
- **SC-005**: Processor uses less than 0.5% CPU per instance at 44100Hz sample rate
- **SC-006**: DC offset in output remains below 0.01 (40dB below full scale) under all conditions
- **SC-007**: Processing latency is zero samples (no lookahead required)
- **SC-008**: Delay time of 10ms produces resonance with fundamental frequency within +/- 10% of 100Hz

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Users understand that feedback above 1.0 creates intentional runaway behavior (this is a feature)
- Users will compose with other processors (EQ, limiter) as needed for final mixing
- The processor operates in mono; stereo processing requires two instances
- The tone filter is always active (no bypass option); set to 20kHz for minimal effect
- The limiter is always active; it is part of the core "controlled chaos" design

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that MUST be reused:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| DelayLine | dsp/include/krate/dsp/primitives/delay_line.h | Provides feedback delay - REUSE directly |
| Waveshaper | dsp/include/krate/dsp/primitives/waveshaper.h | Provides saturation curves - REUSE directly |
| WaveshapeType | dsp/include/krate/dsp/primitives/waveshaper.h | Enum for saturation selection - REUSE directly |
| Biquad | dsp/include/krate/dsp/primitives/biquad.h | Tone filter (lowpass mode) - REUSE directly |
| DCBlocker | dsp/include/krate/dsp/primitives/dc_blocker.h | DC offset removal - REUSE directly |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | Parameter smoothing - REUSE directly |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | Convert limiter threshold dB to linear gain - REUSE directly |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "class FeedbackDistortion" dsp/ plugins/
grep -r "SoftLimiter" dsp/ plugins/
```

**Search Results Summary**: No existing FeedbackDistortion or SoftLimiter found. A soft limiter implementation will need to be created as an internal component (or composed from existing primitives like Waveshaper with Tanh curve).

### Forward Reusability Consideration

*Note for planning phase: When this is a Layer 2+ feature, consider what new code might be reusable by sibling features at the same layer.*

**Sibling features at same layer** (from DST-ROADMAP.md Priority 7):
- 108-ring-saturation - Self-modulation distortion (already implemented)
- 109-allpass-saturator-network - Resonant distortion networks (already implemented)

**Potential shared components** (preliminary, refined in plan.md):
- The soft limiter implementation could potentially be extracted as a reusable primitive for other feedback-based processors
- The feedback-with-saturation pattern is similar to AllpassSaturator but with explicit delay line instead of allpass topology

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

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
| FR-019 | | |
| FR-020 | | |
| FR-021 | | |
| FR-022 | | |
| FR-023 | | |
| FR-024 | | |
| FR-025 | | |
| FR-026 | | |
| FR-027 | | |
| FR-028 | | |
| FR-029 | | |
| FR-030 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |

**Status Key:**
- MET: Requirement fully satisfied with test evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [ ] All FR-xxx requirements verified against implementation
- [ ] All SC-xxx success criteria measured and documented
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
