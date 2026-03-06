# Feature Specification: Analysis Feedback Loop

**Feature Branch**: `123-analysis-feedback-loop`
**Plugin**: Innexus
**Created**: 2026-03-06
**Status**: Draft
**Input**: User description: "Create a self-evolving timbral system by feeding the synth's output back into its own analysis pipeline. At low feedback: subtle self-reinforcing resonances. At high feedback: harmonics crystallize into attractor states -- emergent tonal behavior."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Basic Feedback Loop (Priority: P1)

A sound designer wants the Innexus synthesizer to develop self-reinforcing timbral characteristics when processing a sidechain input. By increasing the Feedback Amount parameter, the synth's output is mixed back into the analysis input, causing harmonics that the synth produces to be re-detected and amplified, creating a resonant feedback system with emergent tonal behavior.

**Why this priority**: This is the core feature. Without the feedback path itself, nothing else in this spec is meaningful. It establishes the new signal flow where synth output feeds back into the analysis pipeline.

**Independent Test**: Can be tested by routing a sidechain signal into Innexus, enabling feedback, and verifying that the output changes character as feedback amount increases from 0 to 1.

**Acceptance Scenarios**:

1. **Given** Innexus in sidechain mode with a note held and sidechain audio present, **When** FeedbackAmount is 0.0, **Then** the signal flow is identical to the current (pre-feedback) behavior with no regression.
2. **Given** Innexus in sidechain mode with a note held and sidechain audio present, **When** FeedbackAmount is increased from 0.0 toward 1.0, **Then** the synth's timbral output begins to self-reinforce, producing more resonant and evolving harmonic content.
3. **Given** Innexus in sidechain mode with a note held, **When** FeedbackAmount is 1.0 and the sidechain input is silence, **Then** the synth output feeds fully back into analysis and converges to a stable attractor state (does not diverge or produce runaway levels) thanks to the energy limiting mechanisms.

---

### User Story 2 - Feedback Decay for Natural Sustain (Priority: P1)

A musician wants the feedback loop to have a natural decay characteristic so that timbral resonances established by the feedback path gradually fade away when the sidechain input stops, rather than sustaining indefinitely. The Feedback Decay parameter controls this entropy leak in the feedback path.

**Why this priority**: Without decay, feedback=1.0 with no input would sustain forever, which is unmusical and potentially destabilizing. Decay is essential for the feedback system to behave as a musical tool rather than an oscillation hazard.

**Independent Test**: Can be tested by feeding sidechain audio with feedback=1.0, then stopping the sidechain input and verifying the output decays to silence over time controlled by the FeedbackDecay parameter.

**Acceptance Scenarios**:

1. **Given** Innexus in sidechain mode with feedback=1.0 and decay=0.2, **When** sidechain input stops (silence), **Then** the synth output decays to silence over time rather than sustaining indefinitely.
2. **Given** Innexus in sidechain mode with feedback=1.0 and decay=0.0, **When** sidechain input stops, **Then** the feedback sustains much longer (decay is minimized, though other safety mechanisms still prevent infinite buildup).
3. **Given** Innexus in sidechain mode with feedback=1.0 and decay=1.0, **When** sidechain input stops, **Then** the feedback decays rapidly to silence.

---

### User Story 3 - Safety and Stability Guarantees (Priority: P1)

An engineer needs assurance that the feedback loop can never cause the plugin to produce dangerously loud or unstable output, regardless of parameter settings. Multiple layered safety mechanisms ensure that the output remains bounded and musically useful even under extreme feedback conditions.

**Why this priority**: Stability is non-negotiable for a production audio plugin. Unbounded feedback could damage speakers, hearing, or downstream processing. This must be guaranteed at every feedback level.

**Independent Test**: Can be tested by setting feedback=1.0 with various input conditions (silence, loud signals, impulses) and measuring that output RMS never exceeds a defined ceiling.

**Acceptance Scenarios**:

1. **Given** any combination of FeedbackAmount and FeedbackDecay values, **When** any sidechain input is provided (including silence, impulses, and loud signals), **Then** the output RMS never exceeds a defined ceiling and the output samples remain within the hard clamp range.
2. **Given** feedback=1.0 with no sidechain input, **When** the system runs for an extended period, **Then** the output converges or decays rather than diverging.
3. **Given** feedback=1.0 with a loud input signal, **When** the soft limiter engages in the feedback path, **Then** the feedback signal is bounded by the tanh soft limiter before being mixed with the sidechain input.

---

### User Story 4 - Freeze Interaction (Priority: P2)

A performer using freeze mode expects that engaging freeze locks the current harmonic frame and prevents the feedback loop from modifying it. When freeze is disengaged, the feedback buffer is cleared so stale audio does not contaminate the re-engaged analysis pipeline.

**Why this priority**: Freeze is an existing core feature. The feedback loop must interact correctly with it -- incorrect interaction could produce artifacts or undermine the freeze behavior users rely on.

**Independent Test**: Can be tested by engaging freeze while feedback is active and verifying the frozen frame is not modified, then disengaging freeze and verifying no stale feedback audio appears.

**Acceptance Scenarios**:

1. **Given** Innexus in sidechain mode with feedback active, **When** freeze is engaged, **Then** the feedback path is automatically bypassed and the frozen frame remains unmodified.
2. **Given** Innexus in sidechain mode with feedback active and freeze engaged, **When** freeze is disengaged, **Then** the feedback buffer is cleared to prevent stale audio from contaminating the re-engaged analysis.
3. **Given** Innexus in sidechain mode with freeze engaged, **When** FeedbackAmount is changed, **Then** the frozen frame is not affected (feedback changes take effect only after freeze is disengaged).

---

### User Story 5 - Sample Mode Bypass (Priority: P2)

A user working in sample mode (not sidechain mode) expects that the feedback parameters have no effect on the sound, since there is no continuous analysis pipeline to feed back into.

**Why this priority**: Feedback is architecturally tied to the sidechain/live analysis path. In sample mode, there is no continuous analysis, so feedback must be cleanly bypassed to avoid confusion or unexpected behavior.

**Independent Test**: Can be tested by switching to sample mode with any FeedbackAmount setting and verifying the output is identical to the output with FeedbackAmount=0.

**Acceptance Scenarios**:

1. **Given** Innexus in sample mode with a sample loaded, **When** FeedbackAmount is set to any value (0.0 to 1.0), **Then** the audio output is identical to when FeedbackAmount is 0.0.
2. **Given** Innexus in sample mode, **When** the user switches to sidechain mode, **Then** the feedback path becomes active according to the current FeedbackAmount setting.

---

### Edge Cases

- What happens when the sidechain input has extreme DC offset? The soft limiter (tanh) will attenuate it, and the existing analysis pipeline handles DC in its windowing/FFT.
- What happens when block size changes mid-stream? The feedback buffer is sized to max block size in setActive(), so any block size up to the maximum is handled.
- What happens when feedback amount is automated rapidly? The feedback amount is read per-process-call, so rapid automation produces smooth transitions at block rate.
- What happens when input source switches from sidechain to sample while feedback is active? Feedback is only active in sidechain mode; switching to sample mode immediately bypasses it.
- What happens when all five safety mechanisms engage simultaneously? They are layered and independent; simultaneous engagement produces a more aggressively bounded output, which is the correct behavior.
- What happens when confidence drops due to feedback-induced garbage in the analysis? The existing confidence gate auto-freezes, preventing garbage harmonics from reaching the oscillator bank. This acts as a fifth safety layer.

## Clarifications

### Session 2026-03-06

- Q: What formula should FR-013 use for per-block FeedbackDecay application? → A: Exponential time-based coefficient: `decayCoeff = exp(-decayAmount * blockSize / sampleRate); feedbackBuffer[s] *= decayCoeff`. Time-consistent and independent of block size/sample rate.
- Q: What state version should this spec write, and how should setState handle older states? → A: Version 8 (sequential from Spec A's version 7). States with version < 8 default FeedbackAmount=0.0 and FeedbackDecay=0.2.
- Q: What are the concrete measurable bounds for SC-003 ("reasonable time" was untestable)? → A: RMS below -60dBFS within 10 seconds at decay=1.0 and within 60 seconds at decay=0.5, measured at 44.1kHz with 512-sample blocks.

## Requirements *(mandatory)*

### Functional Requirements

**Feedback Path:**

- **FR-001**: System MUST provide a feedback path that mixes the synth's previous block output back into the analysis pipeline input according to the formula: `mixedInput[s] = sidechain[s] * (1.0 - feedbackAmount) + fbSample`, where `fbSample` is the soft-limited feedback signal.
- **FR-002**: System MUST capture the mono output of the synth into a feedback buffer AFTER synthesis in each process() call.
- **FR-003**: System MUST mix the feedback buffer with the sidechain mono signal BEFORE the `pushSamples()` call to the LiveAnalysisPipeline in the next process() call.
- **FR-004**: The feedback path MUST introduce exactly one block of latency (the output from block N feeds into the analysis input of block N+1). This is acceptable because the analysis pipeline already has STFT latency.

**Feedback Buffer:**

- **FR-005**: System MUST allocate a feedback buffer sized to the maximum block size during `setActive()` (no allocation on the audio thread).
- **FR-006**: The feedback buffer MUST store a mono representation of the synth output (left+right averaged if stereo, or single channel if mono output).

**Parameters:**

- **FR-007**: System MUST expose a FeedbackAmount parameter (`kAnalysisFeedbackId` = 710, range 0.0-1.0, default 0.0) that controls the mix ratio of synth output into the analysis input. At 0.0, no feedback occurs and the signal flow is identical to the current implementation.
- **FR-008**: System MUST expose a FeedbackDecay parameter (`kAnalysisFeedbackDecayId` = 711, range 0.0-1.0, default 0.2) that applies an entropy leak to the feedback buffer, preventing infinite buildup of energy in the feedback path.

**Energy Limiting (Safety):**

- **FR-009**: System MUST apply a per-sample soft limiter in the feedback path using the formula: `fbSample = tanh(feedbackBuffer[s] * feedbackAmount * 2.0) * 0.5`. This bounds the feedback signal before mixing with sidechain input.
- **FR-010**: The existing per-frame energy budget normalization in the HarmonicPhysics dynamics processor (Spec A) MUST act as a secondary safety net, preventing the total harmonic energy from exceeding the frame's global amplitude budget.
- **FR-011**: The existing hard output clamp (`HarmonicOscillatorBank::kOutputClamp = 2.0f`) MUST remain active as a tertiary safety net clamping the final synthesized output.
- **FR-012**: The existing confidence gate in the live analysis pipeline MUST continue to function: if feedback produces garbage analysis frames, confidence drops and auto-freeze engages, preventing garbage harmonics from reaching the oscillator bank.
- **FR-013**: The FeedbackDecay parameter MUST apply a per-block exponential decay to the feedback buffer contents using the formula: `decayCoeff = exp(-decayAmount * blockSize / sampleRate)`, then `feedbackBuffer[s] *= decayCoeff` for each sample (applied uniformly after capture). This formula is time-consistent: the effective decay rate is independent of block size and sample rate because the exponent normalizes by both. At decay=0.0 the coefficient approaches 1.0 (no attenuation); at decay=1.0 the coefficient produces rapid exponential decay to silence. Higher decay values produce faster energy dissipation.

**Mode Restrictions:**

- **FR-014**: Feedback MUST only be active when the input source is set to sidechain mode (`InputSource::Sidechain`). In sample mode, the feedback path MUST be completely bypassed regardless of the FeedbackAmount parameter value.

**Freeze Interaction:**

- **FR-015**: When manual freeze is engaged, the feedback path MUST be automatically bypassed (feedback signal is not mixed into analysis input).
- **FR-016**: When manual freeze is disengaged, the feedback buffer MUST be cleared (zeroed) to prevent stale audio from contaminating the re-engaged analysis pipeline.

**State Persistence:**

- **FR-017**: Both FeedbackAmount and FeedbackDecay parameter values MUST be saved and restored with plugin state (setState/getState).
- **FR-018**: The feedback buffer itself MUST NOT be persisted -- it is transient runtime state that starts empty (zeroed) on each activation.
- **FR-020**: The plugin state version MUST be incremented to 8 (from Spec A's version 7). `getState()` MUST write version 8. `setState()` MUST handle backwards compatibility: when reading a state with version < 8, FeedbackAmount MUST default to 0.0 and FeedbackDecay MUST default to 0.2 (matching the parameter defaults), preserving identical behavior for presets saved before this spec.

**No Regression:**

- **FR-019**: With FeedbackAmount=0.0, the signal flow MUST be bit-identical to the current implementation (no regression). The feedback path adds zero contribution when the amount is zero.

### Key Entities

- **Feedback Buffer**: A pre-allocated mono audio buffer (sized to max block size) that stores the synth's output from the previous process() call. Used as one input to the feedback mixer in the next call.
- **Feedback Mixer**: The per-sample mixing stage that combines the sidechain input and soft-limited feedback signal according to the FeedbackAmount parameter, producing the mixed input that feeds into the LiveAnalysisPipeline.
- **Soft Limiter**: A per-sample tanh-based limiter applied to the feedback signal before mixing, ensuring the feedback contribution is bounded regardless of the synth output level.
- **FeedbackAmount Parameter**: Controls the balance between sidechain input (external) and feedback (internal) in the analysis input. At 0.0, only sidechain passes through. At 1.0, the sidechain is replaced entirely by the soft-limited feedback signal.
- **FeedbackDecay Parameter**: Controls the rate at which energy leaks from the feedback buffer between blocks, preventing infinite sustain in the feedback loop.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: With FeedbackAmount=0.0, the plugin output MUST be identical to the output of the current implementation (no regression). This is verified by comparing output buffers sample-by-sample.
- **SC-002**: With FeedbackAmount=1.0 and silence as sidechain input, the synth output MUST converge to a stable state or decay to silence (not diverge). Output RMS measured at 1-second intervals over 10 seconds MUST NOT exceed the RMS measured at t=0 by more than 3dB at any interval.
- **SC-003**: With FeedbackAmount=1.0, FeedbackDecay > 0, and no sidechain input, the output MUST decay to silence (RMS below -60dBFS) within the following bounds, measured at 44.1kHz with 512-sample blocks: decay=1.0 within 10 seconds; decay=0.5 within 60 seconds. These bounds follow from the exponential decay formula in FR-013. Tests MUST use these specific parameter values and block configuration.
- **SC-004**: Output RMS MUST never exceed a defined ceiling (determined by the hard output clamp of 2.0) regardless of feedback amount, decay, or input signal characteristics. This criterion verifies FR-011 (`HarmonicOscillatorBank::kOutputClamp`) as the outer bound. FR-010 (energy budget normalization in `HarmonicPhysics::applyDynamics`) acts as a secondary safety net but is not tested in isolation by this spec — it operates on frame amplitudes before synthesis, not on the final output samples directly.
- **SC-005**: Engaging freeze while feedback is active MUST result in the feedback path being bypassed within the same process() call. The frozen frame MUST remain unmodified.
- **SC-006**: Disengaging freeze MUST clear the feedback buffer. Verified by confirming the feedback buffer contains all zeros after freeze disengage.
- **SC-007**: In sample mode, changing FeedbackAmount from 0.0 to 1.0 MUST produce no change in the output signal.
- **SC-008**: The feedback mixing operation MUST NOT introduce allocations, system calls, virtual dispatch, or operations with super-linear complexity relative to block size. The inner mixing loop body MUST contain only arithmetic operations (tanh, multiply, add). Verification: code review confirms loop body complexity, plus a coarse timing sanity check that feedback-active processing adds less than 1% to baseline block time when averaged over 1000 blocks.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Spec A (122-harmonic-physics) is complete and merged. The HarmonicPhysics class with warmth, coupling, and dynamics processing (including energy budget normalization in `applyDynamics`) exists and is functional.
- Parameter IDs 700-703 are in use by Spec A. IDs 710-711 are available for this spec.
- The existing LiveAnalysisPipeline accepts mono audio via `pushSamples()` and does not need modification for this feature -- only the data fed to it changes.
- The existing confidence gate auto-freeze mechanism in the processor will naturally handle degraded analysis quality caused by feedback, since confidence is computed from the analysis output regardless of what generated the input.
- The mono output capture for the feedback buffer uses a simple average of left and right output channels, matching the existing sidechain stereo-to-mono downmix approach.
- One block of latency in the feedback path is acceptable and imperceptible given the existing STFT analysis latency.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `HarmonicOscillatorBank::kOutputClamp` | `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h:86` | Hard output clamp at 2.0f -- existing safety layer, no changes needed |
| `HarmonicPhysics::applyDynamics` | `plugins/innexus/src/dsp/harmonic_physics.h:230-325` | Energy budget normalization -- existing secondary safety net, no changes needed |
| `LiveAnalysisPipeline::pushSamples` | `plugins/innexus/src/dsp/live_analysis_pipeline.cpp:158` | Entry point for feeding audio into analysis -- the mixed feedback+sidechain signal will be fed here |
| `Processor::sidechainBuffer_` | `plugins/innexus/src/processor/processor.h:504` | Existing sidechain downmix buffer -- reference for sizing/allocation pattern for new feedback buffer |
| Confidence gate auto-freeze | `plugins/innexus/src/processor/processor.cpp:861` | Existing confidence-gated freeze mechanism -- acts as safety net if feedback produces garbage analysis |
| Manual freeze handling | `plugins/innexus/src/processor/processor.cpp:596-611` | Existing freeze engage/disengage logic -- feedback bypass and buffer clear must integrate here |
| Sidechain mono downmix | `plugins/innexus/src/processor/processor.cpp:352-380` | Existing stereo-to-mono downmix -- feedback mixing must occur between this and the `pushSamples()` call |
| `InputSource` enum | `plugins/innexus/src/plugin_ids.h:127-131` | Existing mode enum (`InputSource::Sidechain = 1`) -- used to gate feedback to sidechain-only |
| `ParameterIds` enum | `plugins/innexus/src/plugin_ids.h:46-122` | Existing parameter ID registry -- new IDs 710-711 must be added here |
| Parameter atomics pattern | `plugins/innexus/src/processor/processor.h:399-402` | Existing `std::atomic<float>` pattern for warmth/coupling/stability/entropy -- same pattern for feedback params |

**Initial codebase search for key terms:**

```bash
grep -r "feedbackBuffer" plugins/innexus/   # No results -- new functionality
grep -r "kAnalysisFeedback" plugins/innexus/ # No results -- new parameter IDs
grep -r "kOutputClamp" dsp/                  # Found in harmonic_oscillator_bank.h:86 (2.0f)
grep -r "pushSamples" plugins/innexus/src/   # Found in processor.cpp:391, live_analysis_pipeline.cpp:158
grep -r "applyDynamics" plugins/innexus/src/ # Found in harmonic_physics.h:230
```

**Search Results Summary**: All referenced safety components exist and are functional. `kOutputClamp` = 2.0f in `HarmonicOscillatorBank`. Energy budget normalization exists in `HarmonicPhysics::applyDynamics`. Confidence gate auto-freeze exists at processor.cpp:861. Sidechain downmix and `pushSamples` flow exists at processor.cpp:352-394. No existing feedback buffer or feedback-related code exists -- this is entirely new functionality.

### Forward Reusability Consideration

*Note for planning phase: When this is a Layer 2+ feature, consider what new code might be reusable by sibling features at the same layer. The `/speckit.plan` phase will analyze this in detail, but early identification helps.*

**Sibling features at same layer** (if known):
- Future feedback-based effects in Innexus (e.g., timbral delay, harmonic echo) could reuse the feedback buffer and mixing infrastructure.
- The soft limiter pattern (tanh-based bounding) could be extracted to a shared utility if other feedback paths are added.

**Potential shared components** (preliminary, refined in plan.md):
- The feedback buffer allocation pattern (sized in setActive, mono capture, one-block delay) is generic enough to reuse.
- The feedback mixer (crossfade between external and internal signal with soft limiting) could become a reusable DSP primitive if more feedback paths are added later, but for now it is simple enough to inline in the processor.

## Implementation Verification *(mandatory at completion)*

<!--
  CRITICAL: This section MUST be completed when claiming spec completion.
  Constitution Principle XVI: Honest Completion requires explicit verification
  of ALL requirements before claiming "done".

  This section is EMPTY during specification phase and filled during
  implementation phase when /speckit.implement completes.
-->

### Compliance Status

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
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |

**Status Key:**
- MET: Requirement verified against actual code and test output with specific evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap and specific evidence of what IS met
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

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

**Recommendation**: [What needs to happen to achieve completion]
