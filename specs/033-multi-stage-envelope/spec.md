# Feature Specification: Multi-Stage Envelope Generator

**Feature Branch**: `033-multi-stage-envelope`
**Created**: 2026-02-07
**Status**: Draft
**Input**: User description: "Layer 2 DSP Processor: Multi-Stage Envelope Generator -- Extended envelope for complex modulation beyond ADSR. Configurable stages (4-8), loop points for LFO-like behavior, per-stage time/level/curve, sustain point selection. Inspired by Korg MS-20, Buchla 281, Yamaha DX7, and Roland Alpha Juno envelope designs. Must be real-time safe. Phase 1.2 of synth-roadmap.md."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Multi-Stage Envelope Traversal (Priority: P1)

A synthesizer voice component needs an envelope that goes beyond the fixed four-stage ADSR model. A sound designer configures an envelope with a variable number of stages (4 to 8), where each stage has an independently programmable target level and transition time. When a gate-on event occurs, the envelope begins at stage 0 and traverses each stage sequentially, transitioning from the current level to the next stage's target level over that stage's configured time. One stage is designated as the sustain point -- the envelope holds at that stage's level for as long as the gate remains on. When a gate-off event occurs, the envelope continues from its current level through any remaining post-sustain stages, and then returns to zero. This enables complex amplitude and modulation shapes such as the "spit brass" contour (initial peak followed by a dip and secondary rise to sustain), dual-decay envelopes (Korg Poly-800 DEG style), and delayed-attack effects -- none of which are possible with a standard ADSR.

**Why this priority**: Without the core multi-stage traversal and sustain point mechanism, all other features (looping, curve control, gate behavior) have no foundation to build on. This is the minimum viable product that differentiates the multi-stage envelope from the existing ADSREnvelope.

**Independent Test**: Can be fully tested by configuring an envelope with N stages (e.g., 6), designating a sustain point, sending gate-on, processing samples through all pre-sustain stages, verifying hold at sustain, sending gate-off, and confirming traversal through post-sustain stages to idle. Delivers a usable multi-stage modulation source.

**Acceptance Scenarios**:

1. **Given** an envelope with 6 stages (levels: 0.0, 1.0, 0.6, 0.8, 0.3, 0.0) with sustain point at stage 3 and times of 10ms per stage at 44100Hz, **When** gate-on is sent, **Then** the envelope traverses stages 0 through 3 sequentially, reaching each stage's target level within the configured time (within +/-1 sample).
2. **Given** the envelope has reached stage 3 (sustain point) at level 0.8, **When** the gate remains on, **Then** the envelope holds at 0.8 indefinitely.
3. **Given** the envelope is holding at the sustain point, **When** gate-off is sent, **Then** the envelope continues from stage 4 (level 0.3) through stage 5 (level 0.0), transitioning through each remaining stage's time.
4. **Given** the envelope has completed all post-sustain stages and the output has fallen below the idle threshold, **When** processing continues, **Then** the envelope transitions to Idle state, the output becomes 0.0, and `isActive()` returns false.
5. **Given** a 4-stage envelope with sustain at the last stage (stage 3), **When** gate-off is sent, **Then** the envelope transitions directly to the release phase (returning to 0.0) since there are no post-sustain stages.
6. **Given** any valid stage configuration, **When** `processBlock()` is called with N samples, **Then** the output buffer contains the same values as calling `process()` N times sequentially.

---

### User Story 2 - Per-Stage Curve Control (Priority: P2)

A sound designer wants to sculpt the transition shape between each stage independently. Percussive transients need fast exponential attacks. Pad textures need slow logarithmic rises. Some transitions benefit from perfectly linear interpolation. Each stage of the envelope should independently support exponential, linear, and logarithmic curve shapes, allowing the designer to create envelope contours that match the character of classic analog synthesizers (exponential RC-circuit curves like the Moog and Roland designs) or digital precision (linear ramps like the Yamaha DX7).

**Why this priority**: Curve control transforms each stage transition from a simple linear ramp into an expressive shaping tool. Without curve control, the envelope sounds mechanical. Exponential curves are the natural behavior of analog RC circuits and are essential for convincing amplitude and filter envelopes. This is second priority because the stage traversal mechanism must work first.

**Independent Test**: Can be tested by configuring different curve types per stage and verifying the output shape matches the expected trajectory. A linear curve produces equal level increments per sample; an exponential curve produces fast initial change with gradual approach to target; a logarithmic curve produces slow initial change with accelerating approach.

**Acceptance Scenarios**:

1. **Given** a stage with curve set to Exponential transitioning from 0.0 to 1.0, **When** the stage is processed, **Then** the output at the midpoint of the stage duration is above 0.55 (fast initial rise, gradual approach).
2. **Given** a stage with curve set to Linear transitioning from 0.0 to 1.0, **When** the stage is processed, **Then** the output at the midpoint is within 2% of 0.5 (constant rate).
3. **Given** a stage with curve set to Logarithmic transitioning from 0.0 to 1.0, **When** the stage is processed, **Then** the output at the midpoint is below 0.45 (slow initial rise, fast finish).
4. **Given** an envelope with mixed curves (exponential on stage 0, linear on stage 1, logarithmic on stage 2), **When** a full cycle is processed, **Then** each stage independently follows its configured curve shape.
5. **Given** a falling transition (e.g., from 1.0 to 0.3) with exponential curve, **When** the stage is processed, **Then** the output drops quickly at first and gradually approaches 0.3.

---

### User Story 3 - Loop Points for LFO-like Behavior (Priority: P3)

A sound designer wants to create rhythmic or evolving modulation patterns by looping a section of the envelope while the gate is held. By designating a loop start stage and a loop end stage, the envelope cycles between those stages for as long as the gate remains on, producing LFO-like behavior with complex waveshapes determined by the stage levels and curves. This capability is inspired by the Buchla 281 cycling mode, the EMS Synthi trapezoid looping, and modern MSEG implementations in synthesizers like Surge XT. When the gate is released, the envelope exits the loop and continues through the post-sustain stages (or the release phase) to return to zero.

**Why this priority**: Looping is what transforms a one-shot envelope into a versatile modulation source capable of producing complex cyclic patterns. It is a secondary feature because it requires the core stage traversal and sustain mechanisms to be working. Loop behavior is the primary differentiator between a simple multi-stage envelope and a full-featured MSEG.

**Independent Test**: Can be tested by configuring loop start and loop end stages, sending gate-on, and verifying the envelope cycles through the looped stages repeatedly, then sending gate-off and verifying it exits the loop and completes the release.

**Acceptance Scenarios**:

1. **Given** a 6-stage envelope with loop start at stage 1 and loop end at stage 3, **When** gate-on is sent and the envelope reaches the end of stage 3, **Then** the envelope jumps back to stage 1 and continues from stage 1 through stage 3 again.
2. **Given** the envelope is looping between stages 1 and 3 and has completed 5 full loop cycles, **When** gate-off is sent, **Then** the envelope exits the loop at its current position and begins the release phase (returning to 0.0 over the configured release time).
3. **Given** a 4-stage envelope with loop enabled but loop start and loop end at the same stage (stage 2), **When** the envelope reaches the end of stage 2, **Then** the envelope re-enters stage 2, creating a single-stage oscillation (ping-pong between the previous level and stage 2's target).
4. **Given** loop start at stage 0 and loop end at stage 3 (all stages), **When** gate-on is sent, **Then** the envelope loops through all stages continuously, producing a complex cyclic waveform.
5. **Given** looping is disabled, **When** the envelope reaches the sustain point, **Then** the envelope holds at the sustain level without looping (standard multi-stage behavior).

---

### User Story 4 - Sustain Point Selection (Priority: P4)

A synthesizer programmer needs to designate which stage serves as the sustain hold point. In a standard ADSR, sustain is always the third segment. In a multi-stage envelope, the sustain point can be any stage, allowing the programmer to decide how much of the envelope plays before holding and how much plays after the gate is released. Setting the sustain point to the last stage means the envelope behaves like an AD envelope that holds at its final level. Setting it to an early stage means only the attack portion plays before holding, with a complex release tail. This flexibility is inspired by the Yamaha DX7 rate/level system and the Roland Alpha Juno's multi-level envelope design.

**Why this priority**: Sustain point selection is fundamental to the multi-stage concept but depends on the core stage traversal being complete. It is implicitly required by User Story 1 but made explicit here to ensure the user can freely choose the sustain stage.

**Independent Test**: Can be tested by setting the sustain point to different stages and verifying the envelope holds at the correct stage.

**Acceptance Scenarios**:

1. **Given** a 6-stage envelope with sustain at stage 1, **When** gate-on is sent, **Then** the envelope traverses stage 0 and holds at stage 1's target level.
2. **Given** the same envelope (sustain at stage 1), **When** gate-off is sent, **Then** the envelope traverses stages 2, 3, 4, and 5 before reaching idle.
3. **Given** a 6-stage envelope with sustain at stage 5 (last stage), **When** gate-on is sent, **Then** the envelope traverses all 6 stages and holds at stage 5's level.
4. **Given** an envelope with sustain at stage 5, **When** gate-off is sent, **Then** the envelope enters the release phase directly (no post-sustain stages) and returns to 0.0 over the configured release time.
5. **Given** the sustain point is changed while the envelope is in a pre-sustain stage, **When** processing continues, **Then** the envelope uses the new sustain point for the current cycle.

---

### User Story 5 - Retrigger and Legato Modes (Priority: P5)

A keyboardist playing a synthesizer patch expects different behavior depending on playing style. When playing staccato (separate notes), each note should restart the envelope from the beginning (hard retrigger from current level). When playing legato (overlapping notes), the envelope should continue from its current position without restarting. This mirrors the retrigger behavior of the ADSREnvelope (spec 032) but extends it to the multi-stage context, where "continuing" means staying at the current stage and level rather than resetting to stage 0.

**Why this priority**: Retrigger behavior is important for musical expression but depends on all core mechanisms being in place. It is a polish feature that makes the envelope production-ready for voice integration.

**Independent Test**: Can be tested by sending overlapping gate events and verifying hard-retrigger restarts from stage 0 at the current level, while legato mode continues from the current stage and level.

**Acceptance Scenarios**:

1. **Given** the envelope is at stage 3 (sustain) with hard-retrigger mode, **When** a new gate-on is sent, **Then** the envelope restarts from stage 0 using the current output level as the starting level (not snapping to 0.0).
2. **Given** the envelope is in the release phase with hard-retrigger mode, **When** a new gate-on is sent, **Then** the envelope restarts from stage 0 using the current output level.
3. **Given** the envelope is at stage 2 with legato mode, **When** a new gate-on is sent, **Then** the envelope does NOT restart and continues from its current stage and level.
4. **Given** the envelope is in the release phase with legato mode, **When** a new gate-on is sent, **Then** the envelope returns to the sustain point stage, transitioning smoothly from the current level.
5. **Given** any retrigger scenario, **When** the envelope transitions, **Then** no audible click or discontinuity occurs (output is continuous between consecutive samples).

---

### User Story 6 - Real-Time Parameter Changes (Priority: P6)

During a live performance or automation playback, a producer changes stage times, levels, or the sustain point while a note is active. The envelope must accept parameter changes at any time without producing clicks, glitches, or requiring the envelope to restart. New parameter values take effect on the next stage entry or smoothly apply to the current stage where appropriate.

**Why this priority**: Real-time parameter safety is essential for automation and live use but is a refinement on top of working core behavior. This is a production-readiness feature.

**Independent Test**: Can be tested by changing parameters mid-stage and verifying no discontinuities.

**Acceptance Scenarios**:

1. **Given** the envelope is traversing stage 2, **When** stage 2's time is changed, **Then** the envelope recalculates its rate and continues from the current level using the new time, with no discontinuity.
2. **Given** the envelope is holding at the sustain point, **When** the sustain stage's target level is changed, **Then** the output smoothly transitions to the new level (not an instant jump).
3. **Given** the envelope is traversing stage 1, **When** stage 3's level is changed, **Then** the new level takes effect when stage 3 is entered (no impact on current stage).
4. **Given** the envelope is in the loop and currently at stage 2, **When** the loop end is changed from stage 3 to stage 2, **Then** the loop boundary updates take effect on the next loop iteration.

---

### Edge Cases

- What happens when the number of stages is set to the minimum (4)? The envelope operates with 4 stages with valid sustain and loop points clamped to the active range.
- What happens when the number of stages is set to the maximum (8)? The envelope traverses all 8 stages correctly.
- What happens when the number of stages is reduced while the envelope is active and the current stage is beyond the new count? The current stage is clamped to the new maximum, and the envelope continues from the clamped position.
- What happens when all stage times are 0ms (instant transitions)? All stages complete in 1 sample each, producing a staircase pattern. The envelope reaches the sustain point within N samples (where N is the number of pre-sustain stages).
- What happens when a stage time is set to the maximum (10,000ms)? The stage transitions correctly over ~441,000 samples at 44,100Hz without drift or precision loss.
- What happens when gate-off is sent during a pre-sustain stage? The envelope immediately enters the release phase from its current level, bypassing the sustain point and any remaining pre-sustain stages.
- What happens when gate-off is sent during the loop? The envelope exits the loop at its current position and enters the release phase.
- What happens when the sustain point is set beyond the active stage count? The sustain point is clamped to the last active stage.
- What happens when loop start equals loop end? The envelope re-enters the same stage repeatedly, creating an oscillation between the previous stage's end level and this stage's target level.
- What happens when loop start is greater than loop end? Loop start is clamped to be less than or equal to loop end.
- What happens when the sustain point is inside the loop region? The envelope loops through the stages including the sustain point. The sustain hold behavior is bypassed while looping; the sustain point only functions as a hold point when looping is disabled.
- What happens when `reset()` is called during an active envelope? The envelope returns to Idle with output at 0.0 immediately.
- What happens when `prepare()` is called with a different sample rate while active? Rate coefficients are recalculated; the current output level is preserved.
- What happens when adjacent stages have the same target level? The transition is a hold at that level for the stage's duration (regardless of curve shape, since start and end are equal).
- What happens when the release time is 0ms? The envelope snaps to 0.0 immediately on gate-off (within 1 sample).

## Requirements *(mandatory)*

### Functional Requirements

**Core Multi-Stage Behavior (P1)**

- **FR-001**: The envelope MUST support a configurable number of active stages from 4 to 8.
- **FR-002**: Each stage MUST have an independently configurable target level (0.0 to 1.0) and transition time (0.0ms to 10,000ms).
- **FR-003**: The envelope MUST traverse stages sequentially from stage 0 through the configured number of stages, transitioning from the previous stage's level to the current stage's target level over the current stage's configured time.
- **FR-004**: The envelope MUST implement a finite state machine with the following states: Idle, Running, Sustaining, Releasing. The state MUST be queryable at any time.
- **FR-005**: The envelope MUST accept a gate signal (on/off). Gate-on initiates traversal from stage 0 (or the current stage in legato mode). Gate-off initiates the release phase.
- **FR-006**: When the gate is off, the release phase MUST transition the output from its current level toward 0.0 over a configurable release time (0.0ms to 10,000ms).
- **FR-007**: The envelope MUST transition to Idle when the release output falls below the idle threshold (same `kEnvelopeIdleThreshold = 1e-4` as ADSREnvelope), setting the output to exactly 0.0.
- **FR-008**: The envelope MUST provide both per-sample processing (`process()`) and block processing (`processBlock()`). Block processing MUST produce identical output to calling per-sample processing sequentially.
- **FR-009**: The envelope MUST report whether it is active (any state except Idle) and whether it is in the release phase, via dedicated query methods.
- **FR-010**: The envelope MUST accept a `prepare(sampleRate)` call to configure for the target sample rate, and a `reset()` call to return to Idle state with output at 0.0.
- **FR-011**: Stage 0's transition MUST start from the current output level (0.0 when idle, or the current level on retrigger). This provides the equivalent of ADSR's "attack from current level" behavior.

**Sustain Point Selection (P1)**

- **FR-012**: The envelope MUST support designating any stage (0 to numStages-1) as the sustain point.
- **FR-013**: When the envelope reaches the sustain point and the gate is on (and looping is disabled), the envelope MUST hold at the sustain stage's target level indefinitely.
- **FR-014**: When gate-off is sent while holding at the sustain point, the envelope MUST continue through any post-sustain stages before entering the release phase. If the sustain point is the last stage, the envelope enters the release phase directly.
- **FR-015**: The sustain point MUST default to stage (numStages - 2), which for a 4-stage envelope is stage 2 -- equivalent to the sustain position in a standard ADSR.

**Per-Stage Curve Control (P2)**

- **FR-016**: Each stage MUST independently support three curve shapes: Exponential, Linear, and Logarithmic (reusing the `EnvCurve` enum from ADSREnvelope).
- **FR-017**: Exponential curves MUST produce fast initial change with gradual approach to target (classic analog RC-circuit behavior).
- **FR-018**: Linear curves MUST produce constant-rate change (equal output increment per sample).
- **FR-019**: Logarithmic curves MUST produce slow initial change with accelerating approach to target (inverse of exponential).
- **FR-020**: The default curve for all stages MUST be Exponential, matching the natural behavior of analog synthesizer envelope circuits.

**Loop Points (P3)**

- **FR-021**: The envelope MUST support a loop mode that is independently enabled or disabled.
- **FR-022**: When loop mode is enabled, the envelope MUST support configurable loop start and loop end stage indices, both within the range [0, numStages-1].
- **FR-023**: When loop mode is enabled and the envelope reaches the end of the loop end stage, it MUST jump back to the loop start stage and continue traversal, creating a cyclic modulation pattern.
- **FR-024**: Loop start MUST always be less than or equal to loop end. Setting loop start above loop end MUST clamp loop start down.
- **FR-025**: When loop mode is enabled, the sustain hold behavior (FR-013) MUST be bypassed. The envelope loops continuously while the gate is on, regardless of whether the sustain point falls within the loop region.
- **FR-026**: When gate-off is sent while the envelope is looping, the envelope MUST exit the loop at its current position and enter the release phase, transitioning from the current level toward 0.0 over the release time.

**Retrigger Modes (P5)**

- **FR-027**: The envelope MUST support a hard-retrigger mode (default) where gate-on during an active envelope restarts from stage 0, beginning the first stage's transition from the current output level (not snapping to 0.0).
- **FR-028**: The envelope MUST support a legato mode where gate-on during an active envelope does NOT restart. If the envelope is in the release phase, it returns to the sustain point (or the loop if looping is enabled), transitioning smoothly from the current level.

**Real-Time Parameter Changes (P6)**

- **FR-029**: All stage parameters (time, level, curve), the sustain point, loop settings, and the release time MUST be changeable at any time during processing without restarting the envelope.
- **FR-030**: When a stage's time is changed during that stage's active traversal, the envelope MUST recalculate its rate and continue from the current level with no output discontinuity.
- **FR-031**: When the sustain level is changed while the envelope is holding at the sustain point, the output MUST smoothly transition to the new level over 5ms (matching ADSREnvelope's sustain smoothing behavior).

**Real-Time Safety**

- **FR-032**: All processing methods (`process()`, `processBlock()`, `gate()`) MUST be real-time safe: no memory allocations, no locks, no exceptions, no I/O.
- **FR-033**: The envelope MUST use `noexcept` on all processing and parameter-setting methods.
- **FR-034**: The envelope MUST not produce denormalized floating-point values during normal operation.

**Layer Compliance**

- **FR-035**: The envelope MUST reside at Layer 2 (processors) and depend only on Layer 0 (core utilities), Layer 1 (primitives), and the standard library.
- **FR-036**: The envelope class MUST live in the `Krate::DSP` namespace.

### Key Entities

- **MultiStageEnvelope**: The multi-stage envelope generator instance. Holds per-stage configuration (up to 8 stages), loop settings, sustain point, current traversal state, and output level. One instance exists per voice or modulation slot. Distinct from the existing `MultiStageEnvelopeFilter` (which is a filter driven by an internal multi-stage envelope) -- this is a pure envelope generator that outputs a control signal.
- **MultiStageEnvState**: Enumeration of the four states (Idle, Running, Sustaining, Releasing). Represents the current state of the envelope's finite state machine.
- **EnvStageConfig**: Configuration data for a single stage: target level (0.0-1.0), transition time (ms), and curve shape (`EnvCurve`). Up to 8 instances stored in a fixed-size array.
- **EnvCurve**: Reused from `adsr_envelope.h` -- enumeration of curve shape options (Exponential, Linear, Logarithmic). No new definition needed if the existing enum is extracted to a shared location, or it can be used directly via include.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A full multi-stage cycle (gate-on through all pre-sustain stages, sustain hold, gate-off through post-sustain stages and release to idle) completes with all stage transitions occurring within +/-1 sample of the expected sample count based on configured times and sample rate.
- **SC-002**: Envelope output is continuous across all stage transitions -- the output difference between consecutive samples never exceeds the maximum per-sample increment for the active stage (no clicks or discontinuities).
- **SC-003**: A single multi-stage envelope instance with 8 stages consumes less than 0.05% CPU at 44,100Hz sample rate. The per-sample operation is comparable to ADSREnvelope (one multiply + one add for exponential/linear curves, two multiplies + one add for logarithmic curves, plus a stage transition check).
- **SC-004**: All three curve shapes (exponential, linear, logarithmic) produce measurably different output trajectories within each stage: at the midpoint of a stage's duration, linear output is within 2% of 0.5 (normalized to start/end), exponential output is above 0.55, and logarithmic output is below 0.45.
- **SC-005**: Loop mode produces at least 100 consecutive loop cycles without drift, accumulation error, or amplitude deviation exceeding 0.001 from the expected level at loop boundaries.
- **SC-006**: Hard retrigger from any state produces zero clicks -- the first sample after retrigger differs from the last sample before retrigger by no more than the maximum per-sample increment for stage 0.
- **SC-007**: The envelope correctly handles all standard sample rates (44,100Hz, 48,000Hz, 88,200Hz, 96,000Hz, 176,400Hz, 192,000Hz) and produces stage timing within 1% of the configured millisecond values at each rate.
- **SC-008**: After gate-off, the envelope reaches Idle state and reports `isActive() == false` within the expected release time (plus idle threshold convergence). No envelope instance remains "stuck" in an active state after release completes.
- **SC-009**: All functional requirements have corresponding passing tests.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The multi-stage envelope operates as a Layer 2 processor that outputs a control signal (0.0 to 1.0). It does not process audio directly. It has no knowledge of MIDI, notes, or voices -- gate signals are provided by calling code.
- The maximum stage count is fixed at 8, stored in a compile-time fixed-size array. No dynamic allocation is needed. Eight stages provide sufficient complexity for all classic multi-stage designs (Korg Poly-800 DEG uses 5 stages; Yamaha DX7 uses 4 rate/level pairs producing 4 segments; Roland Alpha Juno uses 4 time/3 level pairs) while remaining memory-efficient.
- The minimum stage count is 4, ensuring the envelope always has at least enough stages to replicate standard ADSR behavior (attack level, decay level, sustain level, release level).
- Per-stage curve shapes reuse the same one-pole iterative approach from ADSREnvelope (EarLevel Engineering method) for exponential and near-linear curves, and the same quadratic phase mapping for logarithmic curves. The coefficient calculation formulas are identical: `coef = exp(-log((1 + targetRatio) / targetRatio) / rate)` and `base = (target +/- targetRatio) * (1 - coef)`.
- The release phase uses a single configurable release time that applies regardless of the current level. This is a constant-rate release (matching ADSREnvelope convention): the configured release time specifies the duration for a full 1.0 to 0.0 transition; releasing from a lower level takes proportionally less time.
- When looping is enabled, the sustain hold behavior is bypassed. The loop and sustain are mutually exclusive hold mechanisms: looping creates cyclic modulation, while sustain creates a static hold. This matches the behavior of Surge XT's MSEG "Gate" loop mode and the Buchla 281's cycling mode.
- Stage 0's "from" level is the current output level (0.0 when starting from idle). There is no separate initial level parameter -- the first stage always transitions from whatever the output currently is to stage 0's target level. This naturally handles retrigger from non-zero levels.
- Block processing with mid-block gate events is NOT required. The `gate()` method is called before `processBlock()`, and the entire block uses the same gate state. Sample-accurate gate events within a block can be added as a future enhancement.
- Velocity scaling is NOT included in this spec. The caller can multiply the envelope output by a velocity scaling factor externally. This keeps the multi-stage envelope focused on shape generation and avoids duplicating ADSREnvelope's velocity logic.
- The idle threshold for release-to-idle transition reuses `kEnvelopeIdleThreshold = 1e-4` (0.0001 = -80dB) from ADSREnvelope, ensuring consistent behavior between the two envelope types.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| ADSREnvelope | `dsp/include/krate/dsp/primitives/adsr_envelope.h` | Layer 1 dependency. Reuse `EnvCurve` enum, `kEnvelopeIdleThreshold`, `kMinEnvelopeTimeMs`, `kMaxEnvelopeTimeMs` constants, and potentially the `StageCoefficients` / `calcCoefficients()` static method for per-stage coefficient computation. Consider extracting shared coefficient calculation to a utility. |
| `EnvCurve` enum | `dsp/include/krate/dsp/primitives/adsr_envelope.h:71-75` | Direct reuse. The multi-stage envelope uses the same three curve types. Either include the header or extract the enum to a shared location. |
| `kEnvelopeIdleThreshold` | `dsp/include/krate/dsp/primitives/adsr_envelope.h:51` | Direct reuse for release-to-idle transition threshold. |
| `calcCoefficients()` | `dsp/include/krate/dsp/primitives/adsr_envelope.h:341-358` | Static method computing one-pole coefficients from time, sample rate, target, and target ratio. Currently private to ADSREnvelope. Should be extracted to a shared utility (e.g., `envelope_utils.h` at Layer 1 or Layer 0) to avoid code duplication. |
| MultiStageEnvelopeFilter | `dsp/include/krate/dsp/processors/multistage_env_filter.h` | Layer 2 processor with similar multi-stage concepts but fundamentally different purpose: it is a filter driven by an internal envelope. The new MultiStageEnvelope is a standalone control signal generator. Different name, no ODR conflict. The phase-based curve application (`applyCurve()`) is a reference for curve implementation. |
| `detail::isNaN()`, `detail::isInf()` | `dsp/include/krate/dsp/core/db_utils.h` | Reuse for input validation in parameter setters (safe under `/fp:fast`). |
| `ITERUM_NOINLINE` macro | `dsp/include/krate/dsp/primitives/adsr_envelope.h:37-45` | Reuse for NaN-safe setters (guarded `#ifndef`). |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | Reference for sustain level smoothing (5ms transition). Can use inline one-pole coefficient as ADSREnvelope does, or include smoother directly since Layer 2 can depend on Layer 1. |

**Initial codebase search for key terms:**

```bash
grep -r "class MultiStage" dsp/ plugins/
grep -r "MultiStageEnvelope " dsp/ plugins/
grep -r "EnvStageConfig" dsp/ plugins/
grep -r "MultiStageEnvState" dsp/ plugins/
```

**Search Results Summary**: `MultiStageEnvelopeFilter` exists at Layer 2 (`multistage_env_filter.h`) -- this is a filter processor, not a standalone envelope. The class name `MultiStageEnvelope` is unique. No existing `EnvStageConfig` or `MultiStageEnvState` types found. The `EnvelopeState` enum exists in `multistage_env_filter.h` (with values Idle, Running, Releasing, Complete) -- our enum should use a different name (`MultiStageEnvState`) to avoid ambiguity.

### Forward Reusability Consideration

*This is a Layer 2 processor. Consider what new code might be reusable.*

**Downstream consumers (from synth-roadmap.md):**
- Phase 3.1: Basic Synth Voice -- could use MultiStageEnvelope as an alternative to ADSREnvelope for more complex modulation
- Phase 3.2: Polyphonic Synth Engine -- N voices x M envelopes using either ADSR or multi-stage
- Existing ModulationMatrix -- can route MultiStageEnvelope output as a modulation source

**Sibling features at same layer (Layer 2):**
- Mono/Legato Handler (Phase 2.2) -- uses envelope state queries for voice management
- Note Event Processor (Phase 2.3) -- no direct code sharing expected
- MultiStageEnvelopeFilter (existing) -- already has its own internal envelope; could potentially be refactored to use MultiStageEnvelope as its envelope source, but that is a separate refactoring task

**Potential shared components** (preliminary, refined in plan.md):
- **Envelope coefficient utilities**: The `calcCoefficients()` static method and related constants (`kDefaultTargetRatioA`, `kDefaultTargetRatioDR`, `kLinearTargetRatio`) should be extracted from ADSREnvelope into a shared `envelope_utils.h` at Layer 0 or Layer 1. Both ADSREnvelope and MultiStageEnvelope would then depend on this shared utility, eliminating code duplication.
- **Shared EnvCurve enum**: Could be extracted to a shared header if more envelope types are added in the future.
- **Logarithmic curve phase mapping**: The quadratic phase-based mapping used by ADSREnvelope could be shared.

## Implementation Verification *(mandatory at completion)*

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
| FR-031 | | |
| FR-032 | | |
| FR-033 | | |
| FR-034 | | |
| FR-035 | | |
| FR-036 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |
| SC-009 | | |

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
