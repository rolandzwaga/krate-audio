# Feature Specification: Allpass-Saturator Network

**Feature Branch**: `109-allpass-saturator-network`
**Created**: 2026-01-26
**Status**: Draft
**Input**: User description: "Allpass-Saturator Network processor - places saturation inside allpass filter feedback loops for resonant distortion that can self-oscillate. Input excites the resonance."

## Clarifications

### Session 2026-01-26

- Q: Which routing matrix should FeedbackMatrix topology use? → A: Householder feedback matrix - unitary, energy-preserving, dense diffusion
- Q: What smoothing time constant should be used for parameter changes (frequency, feedback, drive)? → A: 10 milliseconds
- Q: How should the 4 allpass filter frequencies be distributed in AllpassChain topology? → A: Prime number ratios (f, 1.5f, 2.33f, 3.67f) for inharmonic, bell-like timbre with smooth diffusion and avoidance of pitched resonance
- Q: What lowpass filter should be used in the KarplusStrong topology feedback path? → A: 1-pole lowpass (6 dB/oct) with cutoff controlled by decay parameter
- Q: How should self-oscillating feedback be bounded to prevent unbounded growth? → A: Soft clipping at ±2.0 in feedback path (gradual compression approaching limit)

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Single Allpass Resonant Distortion (Priority: P1)

A sound designer wants to add pitched, resonant distortion to a drum loop. They load the AllpassSaturator processor, select the SingleAllpass topology, set the frequency to 200Hz, and adjust feedback to 0.85. The drum hits now have a singing, resonant quality at the specified pitch, with harmonic richness from the saturation.

**Why this priority**: This is the core use case demonstrating the fundamental concept of the processor. Without this working, the other topologies cannot be built.

**Independent Test**: Can be fully tested by processing audio through a single allpass-saturator stage and verifying pitched resonance emerges at the specified frequency with harmonic content from saturation.

**Acceptance Scenarios**:

1. **Given** an AllpassSaturator configured with SingleAllpass topology at 440Hz, **When** a short impulse is processed, **Then** the output exhibits a decaying pitched resonance at approximately 440Hz with harmonic overtones.
2. **Given** SingleAllpass topology with feedback at 0.5, **When** audio is processed, **Then** the resonance decays naturally without self-oscillation.
3. **Given** SingleAllpass topology with feedback at 0.95, **When** audio is processed, **Then** the resonance sustains much longer, approaching self-oscillation.

---

### User Story 2 - Karplus-Strong String Synthesis Enhancement (Priority: P2)

A musician wants to create plucked string sounds with more harmonic complexity than a standard Karplus-Strong synthesizer. They select the KarplusStrong topology, set a pitch via frequency, adjust decay time, and increase drive to add saturation warmth. The result is a physically-modeled string sound with tube-like harmonic richness.

**Why this priority**: Karplus-Strong is a well-known topology that demonstrates the saturator-in-feedback concept clearly. This provides a familiar reference point for users.

**Independent Test**: Can be fully tested by triggering the processor with an impulse/noise burst and verifying plucked string behavior with saturation harmonics.

**Acceptance Scenarios**:

1. **Given** KarplusStrong topology at 220Hz with decay of 1.0 second, **When** the processor receives a transient input, **Then** a plucked string sound decays over approximately 1 second with the characteristic Karplus-Strong timbre plus saturation harmonics.
2. **Given** KarplusStrong topology with drive set to 3.0, **When** compared to drive at 1.0, **Then** the output exhibits noticeably more harmonic content while maintaining pitch stability.

---

### User Story 3 - Allpass Chain for Metallic Resonance (Priority: P2)

A producer wants to create metallic, bell-like tones from a synth pad. They select AllpassChain topology, set a base frequency, and adjust feedback for sustained resonance. The cascaded allpass filters with saturation at inharmonic frequency ratios (f, 1.5f, 2.33f, 3.67f) create complex, inharmonic overtones reminiscent of struck metal with smooth diffusion and avoidance of pitched resonance.

**Why this priority**: The chain topology showcases how multiple resonant stages interact, creating more complex timbres than a single stage.

**Independent Test**: Can be fully tested by processing sustained audio and verifying multiple resonant peaks with inharmonic frequency relationships and more complex harmonic structure than SingleAllpass.

**Acceptance Scenarios**:

1. **Given** AllpassChain topology with 4 stages at prime number frequency ratios, **When** a broadband signal is processed, **Then** multiple inharmonic resonant peaks are audible, creating a more complex, bell-like timbre than SingleAllpass.
2. **Given** AllpassChain with high feedback (0.9), **When** input excites the chain, **Then** the resonance exhibits smooth, inharmonic, metallic characteristics without strong pitched resonance.

---

### User Story 4 - Feedback Matrix for Drone Generation (Priority: P3)

An ambient artist wants to create evolving drone textures. They select FeedbackMatrix topology, set different frequencies for each of the 4 channels, and adjust cross-feed amounts. The matrix of cross-fed saturators creates complex, evolving textures that can self-sustain indefinitely.

**Why this priority**: This is the most complex topology and builds upon all previous concepts. It enables creative sound design beyond traditional distortion.

**Independent Test**: Can be fully tested by processing minimal input with high feedback and verifying sustained, evolving output with multiple interacting resonances.

**Acceptance Scenarios**:

1. **Given** FeedbackMatrix topology with high feedback, **When** a brief input excites the matrix, **Then** the output continues to produce sound indefinitely (self-oscillation).
2. **Given** FeedbackMatrix with 4 different resonant frequencies, **When** audio is processed, **Then** the output exhibits complex beating patterns and harmonic interactions between the frequencies.

---

### Edge Cases

- What happens when frequency is set below 20Hz or above Nyquist/2?
  - Frequency is clamped to valid range [20Hz, sampleRate * 0.45]
- What happens when feedback exceeds 1.0?
  - Feedback is clamped to [0.0, 0.999] to prevent unbounded growth
- What happens with NaN/Inf input samples?
  - Processor resets internal state and returns 0.0 to prevent corruption
- What happens when topology is changed during processing?
  - State is reset to prevent artifacts from mismatched internal buffers
- What happens at extreme drive values?
  - Drive is clamped to reasonable range; DC blocker removes offset from asymmetric saturation

## Requirements *(mandatory)*

### Functional Requirements

**Lifecycle:**
- **FR-001**: System MUST provide `prepare(double sampleRate, size_t maxBlockSize)` to initialize for processing
- **FR-002**: System MUST provide `reset()` to clear all internal state without reallocation
- **FR-003**: System MUST support sample rates from 44100Hz to 192000Hz

**Topology Selection:**
- **FR-004**: System MUST provide `setTopology(NetworkTopology topology)` to select network configuration
- **FR-005**: System MUST support SingleAllpass topology (one allpass with saturation in feedback)
- **FR-006**: System MUST support AllpassChain topology (series of 4 allpass filters with saturation at prime number frequency ratios: f, 1.5f, 2.33f, 3.67f for inharmonic diffusion)
- **FR-007**: System MUST support KarplusStrong topology (delay + saturator + 1-pole lowpass filter (6 dB/oct) with cutoff controlled by decay parameter + feedback)
- **FR-008**: System MUST support FeedbackMatrix topology (4x4 Householder feedback matrix of cross-fed saturators with unitary, energy-preserving properties)
- **FR-009**: System MUST reset internal state when topology changes to prevent artifacts

**Frequency Control:**
- **FR-010**: System MUST provide `setFrequency(float hz)` to set the resonant frequency / pitch
- **FR-011**: System MUST clamp frequency to valid range [20Hz, sampleRate * 0.45]
- **FR-012**: Frequency changes MUST be click-free via parameter smoothing with 10ms time constant

**Feedback Control:**
- **FR-013**: System MUST provide `setFeedback(float feedback)` to control resonance amount (0.0 to 1.0)
- **FR-014**: Feedback above 0.9 MUST allow self-oscillation when input excites resonance
- **FR-015**: Feedback MUST be internally limited via soft clipping at ±2.0 in feedback path to prevent unbounded signal growth while allowing natural dynamics
- **FR-016**: Feedback changes MUST be click-free via parameter smoothing with 10ms time constant

**Saturation Control:**
- **FR-017**: System MUST provide `setSaturationCurve(WaveshapeType type)` to select saturation algorithm
- **FR-018**: System MUST support all WaveshapeType values from the existing Waveshaper primitive
- **FR-019**: System MUST provide `setDrive(float drive)` to control saturation intensity (0.1 to 10.0)
- **FR-020**: Drive changes MUST be click-free via parameter smoothing with 10ms time constant

**Karplus-Strong Specific:**
- **FR-021**: System MUST provide `setDecay(float seconds)` for KarplusStrong topology decay time
- **FR-022**: Decay parameter MUST only affect KarplusStrong topology (ignored for others)
- **FR-023**: Decay MUST be converted to appropriate feedback coefficient and 1-pole lowpass cutoff for pitch-independent decay with characteristic string timbre evolution

**Processing:**
- **FR-024**: System MUST provide `process(float* buffer, size_t n) noexcept` for block processing
- **FR-025**: Processing MUST be real-time safe (no allocations, locks, exceptions, or I/O)
- **FR-026**: System MUST handle NaN/Inf inputs by resetting state and returning 0.0
- **FR-027**: System MUST flush denormals to prevent CPU spikes
- **FR-028**: System MUST include DC blocking after saturation to remove DC offset

**Stability:**
- **FR-029**: System MUST remain stable (bounded output) for any valid parameter combination
- **FR-030**: Self-oscillation MUST be bounded via soft clipping at ±2.0 in feedback path to prevent output exceeding reasonable levels while preserving natural resonance behavior

### Key Entities

- **NetworkTopology**: Enumeration of available network configurations (SingleAllpass, AllpassChain, KarplusStrong, FeedbackMatrix)
- **AllpassSaturator**: Main processor class composing allpass filters, delay lines, waveshaper, DC blocker, and soft clipping limiter
- **AllpassStage**: Internal component representing a single allpass filter with saturation in feedback loop and soft clipping at ±2.0
- **HouseholderMatrix**: 4x4 unitary feedback matrix for FeedbackMatrix topology, providing energy-preserving diffusion
- **OnePoleLowpass**: Simple 6 dB/oct lowpass filter used in KarplusStrong topology for characteristic string timbre decay

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: SingleAllpass topology produces measurable resonance within +/- 5% of target frequency
- **SC-002**: KarplusStrong topology produces pitched decay with RT60 within +/- 20% of specified decay time
- **SC-003**: Self-oscillation occurs when feedback is set to 0.95 or higher (output sustains > 5 seconds after 100ms input burst)
- **SC-004**: All parameter changes (frequency, feedback, drive) complete smoothly within 10ms without audible clicks or pops
- **SC-005**: Processor CPU usage remains below 0.5% per instance at 44100Hz sample rate
- **SC-006**: Output remains bounded (peak < 2.0) under all valid parameter combinations including self-oscillation via soft clipping in feedback path
- **SC-007**: DC offset in output remains below 0.01 (40dB below full scale) under all conditions
- **SC-008**: Processing latency is zero samples (no lookahead required)

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Users understand that high feedback values can cause self-oscillation (this is a feature, not a bug)
- Users will compose with other processors (limiter, EQ) as needed for final mixing
- The processor operates in mono; stereo processing requires two instances
- FeedbackMatrix uses a fixed Householder feedback matrix (not user-configurable) for energy-preserving, diffuse cross-channel interaction

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| Biquad | dsp/include/krate/dsp/primitives/biquad.h | Allpass filter mode - REUSE directly |
| DelayLine | dsp/include/krate/dsp/primitives/delay_line.h | For KarplusStrong delay - REUSE directly |
| Waveshaper | dsp/include/krate/dsp/primitives/waveshaper.h | Saturation curves - REUSE directly |
| DCBlocker | dsp/include/krate/dsp/primitives/dc_blocker.h | DC offset removal - REUSE directly |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | Parameter smoothing - REUSE directly |
| KarplusStrong | dsp/include/krate/dsp/processors/karplus_strong.h | Architecture reference for delay+feedback topology |
| DiffusionNetwork | dsp/include/krate/dsp/processors/diffusion_network.h | Architecture reference for allpass chains |
| AllpassStage | dsp/include/krate/dsp/processors/diffusion_network.h | Schroeder allpass implementation - could REFACTOR to extract |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "class AllpassSaturator" dsp/ plugins/
grep -r "AllpassSaturator" dsp/ plugins/
grep -r "NetworkTopology" dsp/ plugins/
```

**Search Results Summary**: No existing AllpassSaturator or NetworkTopology found. The AllpassStage class in DiffusionNetwork is a good reference but uses a different formulation (single delay line Schroeder) that could potentially be extracted and reused.

### Forward Reusability Consideration

*Note for planning phase: When this is a Layer 2+ feature, consider what new code might be reusable by sibling features at the same layer.*

**Sibling features at same layer** (if known):
- Other Priority 7 (Hybrid & Network) items from DSP-ROADMAP.md
- Future feedback network processors

**Potential shared components** (preliminary, refined in plan.md):
- The AllpassStage with saturation could be extracted as a reusable primitive for other feedback processors
- Householder feedback matrix could be generalized for other matrix-based effects (reverbs, diffusion networks)

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

**Overall Status**: NOT STARTED

**If NOT COMPLETE, document gaps:**
- Implementation not yet begun

**Recommendation**: Proceed to planning phase with `/speckit.plan`
