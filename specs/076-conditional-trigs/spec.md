# Feature Specification: Conditional Trig System

**Feature Branch**: `076-conditional-trigs`
**Plugin**: KrateDSP (Layer 2 processor) + Ruinae (plugin integration)
**Created**: 2026-02-22
**Status**: Complete
**Input**: User description: "Phase 8 of Arpeggiator Roadmap: Conditional Trig System. Elektron-inspired conditional triggers that create patterns evolving over multiple loops. TrigCondition enum with Always, probability (10/25/50/75/90%), A:B ratios (1:2 through 4:4), First, Fill, NotFill. Per-step condition lane, loop count tracking, fill toggle parameter, PRNG state for probability evaluation. Evaluation after Euclidean gating, before modifier evaluation."
**Depends on**: Phase 7 (075-euclidean-timing) -- COMPLETE; Phase 6 (074-ratcheting) -- COMPLETE; Phase 5 (073-per-step-mods) -- COMPLETE; Phase 4 (072-independent-lanes) -- COMPLETE

## Clarifications

### Session 2026-02-22

- Q: What is the VST3 parameter type for `kArpFillToggleId` -- momentary button or latching toggle? → A: Latching toggle (2-step discrete: 0 = Off, 1 = On), `kCanAutomate` set, serialized by host as a standard boolean parameter.
- Q: Does `loopCount_` reset on arp disable, on arp re-enable, or not at all during disable/enable transitions? → A: Reset on re-enable only (`setEnabled(true)` calls `resetLanes()`). Disable needs no condition state cleanup. Pattern starts fresh from loop 0 when arp is re-activated.
- Q: Does `loopCount_` reset when the condition lane length is changed mid-playback? → A: No. `loopCount_` continues uninterrupted across length changes. `ArpLane::setLength()` handles any out-of-bounds position clamping internally. The new cycle length takes effect on the next lane wrap.
- Q: Should `conditionRng_` use a fixed seed (7919) at construction or a time-based entropy seed? → A: Fixed seed 7919 at construction. Testability is more valuable than per-load variation; the fixed-seed sequence becomes random enough within a few steps and matches how NoteSelector's PRNG works.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Probability Triggers for Non-Repeating Rhythms (Priority: P1)

A musician wants to add controlled unpredictability to their arpeggio. They assign probability conditions (e.g., 50%, 75%) to individual steps in the condition lane. On each loop iteration, each probability step independently evaluates against a PRNG -- a 50% step fires roughly half the time, creating a pattern that never repeats exactly the same way twice. This technique is central to the Elektron sequencer workflow (Digitakt, Syntakt, Analog Rytm) where probability triggers are used to create evolving hi-hat patterns, ghost notes, and dynamic rhythmic variation. The probability values available are 10%, 25%, 50%, 75%, and 90% -- a curated set that avoids the impracticality of 100 individual percentage values (the difference between 42% and 43% is imperceptible in musical context) while covering the musically useful range from "rare accent" to "almost always."

**Why this priority**: Probability is the most frequently used conditional trig type in Elektron-style workflows. It validates the core PRNG evaluation mechanism, the condition lane infrastructure, and the integration point between condition evaluation and the existing step pipeline. Every other condition type reuses the same "condition lane value checked per step" architecture.

**Independent Test**: Can be fully tested by assigning a Prob50 condition to a step and running the arp for 1000+ loops, then verifying that the step fires approximately 50% of the time (within statistical tolerance). Also verifiable by assigning Prob10 and confirming it fires approximately 10% of the time.

**Acceptance Scenarios**:

1. **Given** a condition lane with step 0 = Always, step 1 = Prob50, step 2 = Always, step 3 = Prob75, **When** the arp plays 1000 loops of 4 steps, **Then** step 0 fires on every loop (1000 times), step 1 fires approximately 500 times (+/- 5%), step 2 fires on every loop (1000 times), step 3 fires approximately 750 times (+/- 5%).
2. **Given** a condition lane with all steps set to Always (the default), **When** the arp plays, **Then** all steps fire on every loop -- behavior is identical to Phase 7 (no conditions applied). This is the backward-compatible default.
3. **Given** a step with Prob10, **When** the arp plays 1000 loops, **Then** the step fires approximately 100 times (+/- 5%). This confirms the "rare accent" use case works correctly.
4. **Given** a step with Prob90, **When** the arp plays 1000 loops, **Then** the step fires approximately 900 times (+/- 5%). This confirms the "almost always" use case.

---

### User Story 2 - A:B Ratio Triggers for Evolving Multi-Loop Patterns (Priority: P1)

A sound designer wants to create patterns that evolve over multiple loops of the arp cycle. They assign A:B ratio conditions (e.g., 1:2, 2:4, 3:3) to specific steps. The ratio A:B means "fire on the A-th iteration of every B-iteration cycle." For example, 1:2 fires on odd-numbered loops (1st, 3rd, 5th...), while 2:2 fires on even-numbered loops (2nd, 4th, 6th...). This creates deterministic, predictable evolution -- unlike probability, A:B ratios produce the exact same evolution sequence every time, which is useful for structured build-ups and breakdowns. On Elektron hardware, these are used to create patterns where, for example, a snare hit only appears every 4th loop, or a hi-hat pattern alternates between two variations every other loop.

**Why this priority**: A:B ratios are the second most important condition type. They depend on loop count tracking, which is a new state variable in ArpeggiatorCore. The A:B evaluation formula (`loopCount % B == A - 1`) is simple but must integrate correctly with the loop counter.

**Independent Test**: Can be fully tested by assigning a Ratio_1_2 condition to a step and running the arp for exactly 8 loops, then verifying the step fires on loops 0, 2, 4, 6 (zero-indexed, i.e., the 1st, 3rd, 5th, 7th iterations).

**Acceptance Scenarios**:

1. **Given** a step with condition Ratio_1_2, **When** the arp plays 8 loops, **Then** the step fires on loops 0, 2, 4, 6 (the 1st of every 2 loops).
2. **Given** a step with condition Ratio_2_2, **When** the arp plays 8 loops, **Then** the step fires on loops 1, 3, 5, 7 (the 2nd of every 2 loops).
3. **Given** a step with condition Ratio_2_4, **When** the arp plays 8 loops, **Then** the step fires on loops 1 and 5 only (the 2nd of every 4 loops; loop indices 0-7 contain exactly two instances where `loopCount % 4 == 1`, namely loops 1 and 5, and the pattern continues as loops {1, 5, 9, 13, ...}).
4. **Given** a step with condition Ratio_1_3, **When** the arp plays 9 loops, **Then** the step fires on loops 0, 3, 6 (the 1st of every 3 loops).
5. **Given** steps with conditions Ratio_1_2 and Ratio_2_2 on different steps, **When** the arp plays, **Then** they alternate perfectly -- one fires when the other does not, creating a two-variant pattern.

---

### User Story 3 - Fill Mode for Live Performance (Priority: P1)

A performer uses the Fill toggle as a live performance control. Steps with the Fill condition only fire when Fill mode is active -- typically held down momentarily during a performance to add fills, rolls, or embellishments. Steps with the NotFill condition fire only when Fill mode is NOT active, allowing the performer to swap between two pattern variations by holding or releasing the Fill button. This is directly inspired by the Fill mode on Elektron sequencers, where Fill is a momentary toggle that activates a secondary layer of trigs during performance.

**Why this priority**: Fill mode is the primary real-time performance feature of the conditional trig system. It validates the `fillActive_` state tracking and the Fill/NotFill condition evaluation. Fill mode must be responsive enough for live use (no latency beyond the step boundary).

**Independent Test**: Can be fully tested by toggling the Fill parameter on and off while the arp plays, and verifying that Fill-conditioned steps appear/disappear and NotFill-conditioned steps disappear/appear accordingly.

**Acceptance Scenarios**:

1. **Given** a step with condition Fill and Fill mode OFF, **When** the arp reaches that step, **Then** the step is treated as a rest (no noteOn). The step fires only when Fill mode is ON.
2. **Given** a step with condition NotFill and Fill mode OFF, **When** the arp reaches that step, **Then** the step fires normally. When Fill mode is turned ON, the step becomes a rest.
3. **Given** Fill mode toggled ON mid-loop, **When** the next Fill-conditioned step is reached, **Then** it fires immediately. Fill takes effect at the next step boundary (not mid-step).
4. **Given** a pattern with steps [Always, Fill, NotFill, Always], **When** Fill is toggled, **Then** the pattern alternates between two rhythmic variants: Fill OFF plays steps 0, 2, 3 (skips step 1); Fill ON plays steps 0, 1, 3 (skips step 2).

---

### User Story 4 - First-Loop-Only Triggers (Priority: P2)

A musician wants a specific accent or note to play only on the very first time the pattern loops (e.g., a crash cymbal on the downbeat of the first bar), and then never again until the pattern is reset. They assign the First condition to that step. After the first loop completes, First-conditioned steps are permanently silent until the arp is reset (via retrigger, transport restart, or disable/enable).

**Why this priority**: First is a simpler condition type that depends on the loop counter (it checks `loopCount_ == 0`). It is less commonly used than probability or A:B but adds important expressive capability.

**Independent Test**: Can be fully tested by running the arp for 5+ loops and verifying that a First-conditioned step fires only on loop 0 and never again.

**Acceptance Scenarios**:

1. **Given** a step with condition First, **When** the arp plays 5 loops, **Then** the step fires on loop 0 only. Loops 1 through 4 treat the step as a rest.
2. **Given** a step with condition First, **When** the arp is reset (retrigger or transport restart), **Then** the loop counter resets to 0, and the First step fires again on the next loop.

---

### User Story 5 - Condition Lane Persistence and Backward Compatibility (Priority: P3)

A user programs conditional trigger patterns with various conditions across 8 steps, saves the preset, and reloads it. All condition lane data (step conditions, lane length, fill toggle state) are restored exactly. Presets saved before conditional trig support (Phase 7 or earlier) load cleanly with all conditions defaulting to Always and Fill mode off.

**Why this priority**: Without persistence, condition programming is lost on save/load. This is an integration concern that builds on the core functionality.

**Independent Test**: Can be tested by configuring specific conditions, saving plugin state, restoring it, and verifying all values match.

**Acceptance Scenarios**:

1. **Given** a condition lane of length 8 with steps [Always, Prob50, Ratio_1_2, Fill, NotFill, First, Prob75, Ratio_2_4], **When** plugin state is saved and restored, **Then** all condition lane step values and lane length are identical after restore.
2. **Given** a preset saved before conditional trig support (Phase 7 or earlier), **When** loaded into this version, **Then** the condition lane defaults to length 1 with value Always (TrigCondition::Always = 0), fill mode defaults to off. The arpeggiator behaves identically to Phase 7.

---

### Edge Cases

- What happens when the condition lane has all steps set to Always? The arp behaves identically to Phase 7 -- all steps fire unconditionally. This is the default.
- What happens when a step has both a Euclidean rest (from Phase 7) and a condition? The Euclidean rest takes precedence. The condition is never evaluated for Euclidean rest steps because Euclidean gating fires before condition evaluation. The evaluation order is: Euclidean gating -> Condition evaluation -> Modifier evaluation -> Ratcheting.
- What happens when a step has a condition that evaluates to false AND a modifier flag of Tie? The condition evaluates first: if the condition says "don't fire," the step is treated as a rest. The Tie modifier is never evaluated. This is consistent with how Euclidean rest overrides modifiers.
- What happens when a condition-skipped step is followed by a Tie step that passes its condition? The Tie step has no preceding note to sustain (the previous step was skipped by its condition). Per Phase 5 FR-013, a Tie with no preceding note falls back to rest behavior.
- What happens when a step has both a Fill condition and a Prob50 condition? This is not possible -- each step has exactly one condition (the condition lane stores a single TrigCondition enum value per step, not a bitmask). The user must choose one condition per step.
- What happens when the loop count exceeds the maximum tracking range? The loop count uses `size_t`, which can hold values up to 2^64-1 on 64-bit systems. At 120 BPM with 1/16 note rate and 32 steps per loop, one loop takes approximately 8 seconds. It would take over 4.6 billion years to overflow a 64-bit loop counter. Overflow is not a concern.
- What happens when Fill mode is toggled while a tied note is sustaining? The Fill/NotFill condition is evaluated at the step boundary when the step fires. If a currently-sounding tied note's next step has a Fill condition and Fill mode is off, the next step is treated as a rest, which breaks the tie chain and emits a noteOff for the sustained note. This is the correct behavior.
- What happens when probability conditions interact with ratcheting? If a probability step fires (condition passes), ratcheting applies normally. If the probability step does not fire (condition fails), no notes fire and no ratcheted sub-steps are emitted. The condition evaluation is a gate that precedes all note emission.
- What happens during a retrigger (Note or Beat) with conditions active? `resetLanes()` resets the condition lane position to step 0 and resets `loopCount_` to 0. The `fillActive_` flag is NOT reset (it is a performance control preserved across resets).
- What happens when the condition lane has a different length than other lanes? It cycles independently, just like all other lanes. This creates polymetric conditional patterns.
- What happens when `loadArpParams()` encounters EOF at the first condition field? This signals a clean Phase 7 preset. The function returns `true` (success) and all condition fields retain defaults (length 1, all steps Always, fill off). What happens when EOF occurs after `conditionLaneLength` is read but before all step values are read? This signals a corrupt stream -- the function returns `false`.
- What happens when the condition lane has length 1 with an A:B ratio condition? The lane wraps on every step, incrementing the loop counter on every step. The A:B ratio effectively operates per-step rather than per-loop. For example, Ratio_1_2 would fire on every other step. This is a valid degenerate case.
- What happens when the condition lane length is changed mid-playback (e.g., user shrinks from 8 to 4 steps)? `loopCount_` is NOT reset. `ArpLane::setLength()` handles any out-of-bounds position clamping internally (the current step position wraps to the new length if needed). The loop counter continues from its current value and the new cycle length takes effect on the next lane wrap. This preserves A:B and First condition continuity during live edits.

## Requirements *(mandatory)*

### Functional Requirements

**TrigCondition Enumeration (DSP Layer 2)**

- **FR-001**: The ArpeggiatorCore header MUST define a `TrigCondition` enum class with the following values, in this order:
  - `Always` (0) -- Step fires unconditionally. This is the default for all steps.
  - `Prob10` (1) -- Step fires with approximately 10% probability.
  - `Prob25` (2) -- Step fires with approximately 25% probability.
  - `Prob50` (3) -- Step fires with approximately 50% probability.
  - `Prob75` (4) -- Step fires with approximately 75% probability.
  - `Prob90` (5) -- Step fires with approximately 90% probability.
  - `Ratio_1_2` (6) -- Step fires on the 1st of every 2 loops.
  - `Ratio_2_2` (7) -- Step fires on the 2nd of every 2 loops.
  - `Ratio_1_3` (8) -- Step fires on the 1st of every 3 loops.
  - `Ratio_2_3` (9) -- Step fires on the 2nd of every 3 loops.
  - `Ratio_3_3` (10) -- Step fires on the 3rd of every 3 loops.
  - `Ratio_1_4` (11) -- Step fires on the 1st of every 4 loops.
  - `Ratio_2_4` (12) -- Step fires on the 2nd of every 4 loops.
  - `Ratio_3_4` (13) -- Step fires on the 3rd of every 4 loops.
  - `Ratio_4_4` (14) -- Step fires on the 4th of every 4 loops.
  - `First` (15) -- Step fires only on the first loop (loopCount == 0), never again until reset.
  - `Fill` (16) -- Step fires only when fill mode is active.
  - `NotFill` (17) -- Step fires only when fill mode is NOT active.
  - `kCount` (18) -- Sentinel value for bounds checking. Not a valid condition.
- **FR-002**: The enum MUST be `uint8_t`-backed (`enum class TrigCondition : uint8_t`) for compact storage in `ArpLane<uint8_t>`.

**Condition Lane in ArpeggiatorCore (DSP Layer 2)**

- **FR-003**: The ArpeggiatorCore MUST contain a condition lane (`ArpLane<uint8_t> conditionLane_`) that stores per-step TrigCondition values encoded as `uint8_t`. Each step stores a single condition (not a bitmask -- conditions are mutually exclusive per step).
- **FR-004**: The condition lane MUST default to length 1 with step 0 = `TrigCondition::Always` (value 0). With this default, the arp behaves identically to Phase 7 -- every step that passes Euclidean gating fires unconditionally.
- **FR-005**: The condition lane constructor initialization MUST set step 0 to `static_cast<uint8_t>(TrigCondition::Always)` (which is 0) in the ArpeggiatorCore constructor. Since `ArpLane<uint8_t>` zero-initializes steps to 0, and `TrigCondition::Always` is 0, the default is correct even without explicit initialization -- but an explicit set is preferred for clarity and consistency with other lane initializations.
- **FR-006**: The condition lane MUST advance once per arp step tick, simultaneously with all other lanes (velocity, gate, pitch, modifier, ratchet), and independently at its own configured length.
- **FR-007**: The ArpeggiatorCore MUST provide `conditionLane()` accessor methods (const and non-const) following the same pattern as all other lane accessors.

**Condition Evaluation State**

- **FR-008**: The ArpeggiatorCore MUST maintain a `loopCount_` (size_t) that tracks how many times the condition lane has completed a full cycle. The loop count increments when the condition lane wraps from its last step back to step 0. Default: 0.
- **FR-009**: The ArpeggiatorCore MUST maintain a `fillActive_` (bool) flag. Default: false. This is set by the `setFillActive(bool)` setter method, which is called from the parameter system.
- **FR-010**: The ArpeggiatorCore MUST maintain a PRNG state for probability evaluation. The existing `Xorshift32` PRNG from `dsp/include/krate/dsp/core/random.h` (Layer 0) MUST be reused. A dedicated `Xorshift32 conditionRng_` member MUST be added, seeded at construction time with the fixed value 7919 (a prime distinct from NoteSelector's seed of 42). The seed MUST be a fixed compile-time constant, not time-based or entropy-based -- testability is more valuable than per-load variation, and the sequence becomes statistically well-distributed within a few steps regardless of starting point. The PRNG is consumed once per step for probability conditions, providing a fresh random value on each evaluation.
- **FR-011**: The `loopCount_` MUST be incremented when the condition lane wraps from its last step back to step 0. This detection MUST occur in the condition lane advance path: after `conditionLane_.advance()` is called, if `conditionLane_.currentStep() == 0` (the lane just wrapped), increment `loopCount_`.

**Condition Evaluation Logic**

- **FR-012**: On each arp step tick (in `fireStep()`), AFTER all lane values have been advanced (including the condition lane) and AFTER Euclidean gating (if enabled), but BEFORE modifier evaluation, the condition for the current step MUST be evaluated. The evaluation order in `fireStep()` is:
  1. NoteSelector advance (`selector_.advance()`)
  2. All lane advances (velocity, gate, pitch, modifier, ratchet, **condition**)
  3. Euclidean gating check (if enabled) -- rest steps short-circuit before condition
  4. **Condition evaluation** (this feature)
  5. Modifier evaluation (Rest > Tie > Slide > Accent)
  6. Ratcheting
- **FR-013**: The condition evaluation function MUST implement the following logic for each TrigCondition value:
  - `Always`: return true (step fires).
  - `Prob10`: return `conditionRng_.nextUnipolar() < 0.10f`.
  - `Prob25`: return `conditionRng_.nextUnipolar() < 0.25f`.
  - `Prob50`: return `conditionRng_.nextUnipolar() < 0.50f`.
  - `Prob75`: return `conditionRng_.nextUnipolar() < 0.75f`.
  - `Prob90`: return `conditionRng_.nextUnipolar() < 0.90f`.
  - `Ratio_1_2`: return `loopCount_ % 2 == 0` (fires on loops 0, 2, 4, ...).
  - `Ratio_2_2`: return `loopCount_ % 2 == 1` (fires on loops 1, 3, 5, ...).
  - `Ratio_1_3`: return `loopCount_ % 3 == 0`.
  - `Ratio_2_3`: return `loopCount_ % 3 == 1`.
  - `Ratio_3_3`: return `loopCount_ % 3 == 2`.
  - `Ratio_1_4`: return `loopCount_ % 4 == 0`.
  - `Ratio_2_4`: return `loopCount_ % 4 == 1`.
  - `Ratio_3_4`: return `loopCount_ % 4 == 2`.
  - `Ratio_4_4`: return `loopCount_ % 4 == 3`.
  - `First`: return `loopCount_ == 0`.
  - `Fill`: return `fillActive_`.
  - `NotFill`: return `!fillActive_`.
  - Any value >= `kCount`: return true (treat as Always -- defensive fallback for out-of-range values).
  Note on A:B formula: The roadmap specifies `loopCount % B == A-1`. This is the formula used above. For example, Ratio_1_2 checks `loopCount_ % 2 == 0` (which is `A-1 = 1-1 = 0`). This means A=1, B=2 fires on loop 0 (the "first" loop of the cycle), matching Elektron behavior where A:B=1:2 fires on the 1st iteration of every 2.
- **FR-014**: When a condition evaluates to false, the step MUST be treated as a rest: no noteOn is emitted, a noteOff MUST be emitted for any currently sounding notes, and any active tie chain MUST be broken (set `tieActive_` to false). This behavior is identical to the Euclidean rest path and the modifier Rest path. Pending noteOffs for currently sounding notes MUST be cancelled before emitting new noteOffs (to prevent double emission).
- **FR-015**: When a condition evaluates to true, the step proceeds to modifier evaluation as normal. The condition is transparent -- it only gates whether the step enters the modifier evaluation pipeline.
- **FR-016**: Probability conditions MUST consume the PRNG exactly once per evaluation. Since Euclidean rest short-circuits BEFORE condition evaluation (FR-012 step 3), the PRNG is NOT consumed on Euclidean rest steps -- condition evaluation never executes for those steps. This means the PRNG sequence is deterministic for a given pattern of Euclidean hits, which aids testing and reproducibility.

**Loop Count Management**

- **FR-017**: The `loopCount_` MUST be initialized to 0 on construction, on `reset()`, and on `resetLanes()`. This means retrigger (Note and Beat modes), transport restart, and arp re-enable transitions all reset the loop counter to 0. Specifically: `setEnabled(true)` MUST call `resetLanes()`, which resets `loopCount_` to 0 alongside all lane positions. `setEnabled(false)` does NOT reset `loopCount_` -- no condition state cleanup is needed on disable.
- **FR-018**: The `loopCount_` MUST increment when the condition lane wraps (FR-011). This happens naturally during `conditionLane_.advance()` -- when the position wraps from `length - 1` back to 0, the caller detects this via `conditionLane_.currentStep() == 0` (after advance) and increments `loopCount_`. Special case: if the condition lane has length 1, it wraps on every step, so `loopCount_` increments on every step. This means A:B ratios with a length-1 condition lane effectively operate per-step rather than per-loop, which is a valid degenerate case. `loopCount_` MUST NOT be reset when the condition lane length is changed -- the counter continues uninterrupted across length changes and the new cycle length takes effect on the next lane wrap.
- **FR-019**: The `loopCount_` MUST NOT overflow in any practical usage scenario. It uses `size_t` (64-bit on all target platforms). At the maximum rate (50 Hz free rate, 1-step condition lane), the counter would overflow after approximately 11.7 billion years. No overflow protection is needed.

**Fill Mode**

- **FR-020**: The ArpeggiatorCore MUST provide a `setFillActive(bool)` method that sets `fillActive_`. This is a real-time safe setter with no side effects beyond storing the boolean.
- **FR-021**: The ArpeggiatorCore MUST provide a `fillActive()` const getter that returns the current fill state.
- **FR-022**: The `fillActive_` flag MUST be preserved across `reset()` and `resetLanes()` calls. Fill mode is a performance control, not a pattern state. The user expects Fill to remain active when the arp resets. It is only changed by explicit `setFillActive()` calls from the parameter system.
- **FR-023**: The `fillActive_` flag is NOT serialized in plugin state. It is a transient performance control (like a sustain pedal) that defaults to false on load. The Fill Toggle parameter (`kArpFillToggleId`) IS serialized as part of VST3 parameter state by the host, but `fillActive_` in ArpeggiatorCore is the runtime state that the audio thread reads.

**Condition and Euclidean Interaction**

- **FR-024**: The evaluation order is: Euclidean gating -> Condition evaluation -> Modifier priority chain (Rest > Tie > Slide > Accent) -> Ratcheting. A Euclidean rest short-circuits the entire step before condition evaluation. A condition failure short-circuits the step before modifier evaluation.
- **FR-025**: When a step is a Euclidean rest (hit bit not set), the condition lane still advances (all lanes advance unconditionally on every step tick, per Phase 7 FR-011), but the condition is NOT evaluated (the step is already gated as rest by Euclidean). The PRNG is NOT consumed.
- **FR-026**: When a step is a Euclidean hit, the condition is evaluated normally. If the condition passes, the step proceeds to modifier evaluation. If the condition fails, the step is treated as a rest (FR-014).

**Condition and Modifier Interaction**

- **FR-027**: When a condition evaluates to false (step is skipped), modifiers are NOT evaluated. The step is a rest regardless of what modifier flags are set. This is consistent with how Euclidean rest overrides modifiers.
- **FR-028**: When a condition evaluates to true, modifiers apply normally per Phase 5/6/7 rules. A modifier Rest flag on a condition-passing step still produces silence (the modifier Rest works as before).
- **FR-029**: A condition-skipped step breaks any active tie chain. If a tie chain was active from a preceding step and the current step's condition evaluates to false, a noteOff MUST be emitted for the sustained note(s) and `tieActive_` set to false. This is identical to the Euclidean rest tie-breaking behavior (Phase 7 FR-007).

**Condition and Ratchet Interaction**

- **FR-030**: When a step's condition evaluates to false, ratcheting does NOT apply. No ratcheted sub-steps fire. This is identical to the Euclidean rest ratchet suppression (Phase 7 FR-016).
- **FR-031**: When a step's condition evaluates to true, ratcheting applies normally per Phase 6 rules.

**Condition and Chord Mode**

- **FR-032**: In Chord mode (multiple notes per step), the condition applies uniformly to all chord notes. A condition pass fires all chord notes; a condition fail silences all.

**Condition and Swing**

- **FR-033**: Swing applies to step duration independently of conditions. Conditions determine whether a step fires; swing determines step timing. They are orthogonal.

**State Cleanup**

- **FR-034**: The `resetLanes()` method MUST be extended to reset the condition lane position to step 0, and reset `loopCount_` to 0. The `fillActive_` flag MUST NOT be reset (FR-022).
- **FR-035**: The `reset()` method MUST reset `loopCount_` to 0 (via `resetLanes()`). The condition PRNG state is NOT reset by `reset()` or `resetLanes()` -- this ensures the random sequence continues naturally across resets, avoiding audible repetition artifacts. If deterministic replay is needed (e.g., for testing), the PRNG can be re-seeded via the constructor. Note the intentional asymmetry: construction always seeds `conditionRng_` at 7919; `reset()`/`resetLanes()` do not re-seed -- the PRNG advances continuously from first construction through all resets.
- **FR-036**: When the arp is disabled (`setEnabled(false)`) while conditions are active, no condition state cleanup is needed. The condition lane position and `loopCount_` are intentionally preserved across the disable -- they will be reset by the `resetLanes()` call that `setEnabled(true)` makes on re-enable (FR-017). The `fillActive_` flag is also preserved (FR-022). The condition lane and loop counter are passive structures that only gate note emission and hold no pending events requiring teardown.

**Defensive Behavior**

- **FR-037**: In the defensive `result.count == 0` branch in `fireStep()` (where the held buffer became empty), the condition lane MUST advance along with all other lanes, keeping all lanes synchronized. The loop counter increment check (FR-011) MUST also execute in this branch.

**Plugin Integration (Ruinae)**

- **FR-038**: The plugin MUST expose conditional trig parameters through the VST3 parameter system:
  - Condition lane length: `kArpConditionLaneLengthId = 3240` (discrete: 1-32)
  - Condition lane steps 0-31: `kArpConditionLaneStep0Id = 3241` through `kArpConditionLaneStep31Id = 3272` (discrete: 0-17, mapping to TrigCondition enum values)
  - Fill toggle: `kArpFillToggleId = 3280` (latching toggle: discrete 2-step, 0 = Off, 1 = On; `kCanAutomate`; serialized by host as a standard boolean parameter)
- **FR-039**: The `kArpEndId` and `kNumParameters` sentinels MUST remain at their current values (3299 and 3300 respectively, set in Phase 6). The Condition parameter IDs (3240-3272, 3280) fall within the existing reserved range (3234-3299) and require no sentinel adjustment. The following sub-ranges within 3234-3299 are intentionally unallocated and reserved for future phases: 3234-3239 (gap before condition lane, reserved for use before Phase 9) and 3273-3279 (gap between condition step IDs and fill toggle, reserved for future condition-lane extensions). These gaps MUST be documented as inline comments in `plugin_ids.h`.
- **FR-040**: All 34 condition parameters MUST have the `kCanAutomate` flag. Step parameters (`kArpConditionLaneStep0Id` through `kArpConditionLaneStep31Id`) MUST additionally have `kIsHidden` to avoid cluttering the host's parameter list, consistent with all other lane step parameters from Phases 4-6. The length parameter (`kArpConditionLaneLengthId`) MUST NOT have `kIsHidden`. The fill toggle (`kArpFillToggleId`) MUST NOT have `kIsHidden` -- it is a user-facing performance control that benefits from host automation lane visibility (e.g., automating fill on/off per bar in a DAW).
- **FR-041**: The `ArpeggiatorParams` struct MUST be extended with atomic storage for condition lane data:
  - `std::atomic<int> conditionLaneLength{1}` (default 1)
  - `std::array<std::atomic<int>, 32> conditionLaneSteps{}` -- default 0 (Always). `int` for lock-free guarantee; cast to `uint8_t` at DSP boundary, clamped to [0, 17].
  - `std::atomic<bool> fillToggle{false}` (default false)
  The constructor MUST initialize all conditionLaneSteps to 0 (TrigCondition::Always).
- **FR-042**: The `handleArpParamChange()` dispatch MUST be extended to handle `kArpConditionLaneLengthId` (normalized [0,1] to integer 1-32), all 32 step IDs (`kArpConditionLaneStep0Id` through `kArpConditionLaneStep31Id`, normalized [0,1] to integer 0-17), and `kArpFillToggleId` (normalized 0.0/1.0 to bool).
- **FR-043**: Condition lane state (length, all step values) and fill toggle MUST be included in plugin state serialization (`saveArpParams`) and deserialization (`loadArpParams`). The condition data MUST be serialized AFTER the existing Euclidean data to maintain backward compatibility with Phase 7 presets.
- **FR-044**: Loading a preset saved before conditional trig support (Phase 7 or earlier) MUST result in the condition lane defaulting to length 1 with value 0 (Always), fill mode off. The arp output must be identical to Phase 7 behavior. Specifically: if `loadArpParams()` encounters EOF at the first condition field read (`conditionLaneLength`), it MUST return `true` (success) and all condition fields retain their defaults. If EOF occurs after `conditionLaneLength` is read but before all step values are read, it MUST return `false` (corrupt stream). Out-of-range values MUST be clamped: length to [1, 32], step values to [0, 17].
- **FR-045**: The `applyParamsToEngine()` method in the processor MUST transfer condition lane data to the ArpeggiatorCore using the expand-write-shrink pattern (identical to all other lanes): call `conditionLane().setLength(32)` first, then `conditionLane().setStep(i, value)` for each of 32 steps (with clamping to [0, 17]), then `conditionLane().setLength(actualLength)`. The final `setLength()` call MUST NOT reset `loopCount_` -- `ArpLane::setLength()` handles any out-of-bounds position clamping internally and `loopCount_` continues uninterrupted across length changes (FR-018).
- **FR-046**: The `applyParamsToEngine()` method MUST also transfer the fill toggle: call `setFillActive(fillToggle.load())`.
- **FR-047**: The `formatArpParam()` method MUST be extended to display human-readable values for condition parameters:
  - Condition lane length: "1 step" (singular when length == 1) or "N steps" (plural when length > 1). Examples: "1 step", "8 steps", "32 steps".
  - Condition lane step values: display the TrigCondition name: "Always", "10%", "25%", "50%", "75%", "90%", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "1st", "Fill", "!Fill"
  - Fill toggle: "Off" / "On"

**Controller State Sync**

- **FR-048**: When `loadArpParams()` successfully loads condition lane data, the loaded values MUST be propagated back to the VST3 controller via `setParamNormalized()` calls for all 34 condition parameters (`kArpConditionLaneLengthId`, 32 step IDs, and `kArpFillToggleId`), following the same pattern used by existing lanes.

### Key Entities

- **TrigCondition**: An enumeration (uint8_t) representing the type of conditional trigger assigned to an arp step. Each step has exactly one condition (not a bitmask). 18 values ranging from Always (unconditional) through probability percentages, A:B loop ratios, first-loop-only, and Fill mode conditions.
- **Condition Lane**: An `ArpLane<uint8_t>` instance within ArpeggiatorCore. Each step stores a TrigCondition enum value (cast to uint8_t). Advances once per arp step tick, independently of other lanes at its own configured length. Default: length 1, step value 0 (Always).
- **Loop Count**: A `size_t` counter that increments each time the condition lane wraps (completes one full cycle of its configured length). Used by A:B ratio conditions and the First condition. Reset to 0 on `reset()`, `resetLanes()`, and arp re-enable (`setEnabled(true)`). Not reset on disable (`setEnabled(false)`).
- **Fill Active**: A transient boolean flag toggled by the Fill Toggle parameter. Used by Fill and NotFill conditions. Not serialized in DSP state; preserved across resets.
- **Condition PRNG**: A `Xorshift32` instance dedicated to condition evaluation. Consumed once per step for probability conditions. Seeded at construction time. Not reset on pattern reset (ensures continuous non-repeating randomness).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Probability conditions produce statistically correct distributions. Over 10,000 evaluations, each level MUST fire within ±3 absolute percentage points of its target rate (i.e., ±300 out of 10,000 events): Prob10 fires between 700 and 1300 times, Prob25 fires between 2200 and 2800 times, Prob50 fires between 4700 and 5300 times, Prob75 fires between 7200 and 7800 times, Prob90 fires between 8700 and 9300 times. The ±3% is an absolute tolerance (percentage points), not a relative tolerance (e.g., ±3% of the expected count). Verified by at least 5 test cases (one per probability level), each running 10,000+ iterations.
- **SC-002**: A:B ratio conditions are deterministic and cycle correctly. Ratio_1_2 fires on loops {0, 2, 4, ...}. Ratio_2_2 fires on loops {1, 3, 5, ...}. Ratio_2_4 fires on loops {1, 5, 9, ...}. All 9 ratio conditions verified across at least 12 loops each.
- **SC-003**: First condition fires only on loop 0 and never again (verified across 100+ loops). After reset, First fires again on the new loop 0.
- **SC-004**: Fill/NotFill conditions correctly respond to the fill toggle. Fill-conditioned step fires only when fillActive_ is true. NotFill fires only when false. Toggling fill mid-loop takes effect at the next step boundary. Verified by at least 4 test cases.
- **SC-005**: With the condition lane at default (length 1, value Always), the arpeggiator produces identical output to Phase 7 across 1000+ steps at 120/140/180 BPM. Zero tolerance -- same notes, velocities, sample offsets, legato flags on all events.
- **SC-006**: Conditions compose correctly with Euclidean timing: Euclidean rest suppresses condition evaluation; Euclidean hit allows condition evaluation. A step that is a Euclidean hit but fails its condition is treated as rest. Verified by at least 2 test cases.
- **SC-007**: Conditions compose correctly with modifiers: a condition-skipped step breaks tie chains, suppresses ratcheting, and ignores modifier flags. A condition-passing step allows normal modifier and ratchet processing. Verified by at least 4 test cases.
- **SC-008**: Condition lane cycles independently of other lanes (polymetric). A condition lane of length 3 and a velocity lane of length 5 produce a combined cycle of 15 steps with no premature repetition.
- **SC-009**: Plugin state round-trip (save then load) preserves all condition lane step values, lane length, and fill toggle exactly. Verified by comparing every value before and after serialization.
- **SC-010**: Loading a Phase 7 preset (without condition data) succeeds with default condition values (length 1, all steps Always, fill off) and the arpeggiator output is identical to Phase 7 behavior.
- **SC-011**: All condition-related code in `fireStep()` and `processBlock()` performs zero heap allocation, verified by code inspection (no new/delete/malloc/free/vector/string/map in the modified code paths).
- **SC-012**: All condition parameters (34 total: 1 length + 32 steps + 1 fill toggle) are registered with the host, automatable, and display correct human-readable values. Verification requires: (a) an integration test confirming all 34 parameter IDs are registered with correct flags (`kCanAutomate` on all; `kIsHidden` on steps 3241-3272 only); and (b) a formatting test for each condition type verifying the display string.
- **SC-013**: Loop count resets correctly on retrigger (Note and Beat modes), transport restart, and arp re-enable (i.e., `setEnabled(true)` after a period of being disabled). Verified by tests that trigger each reset path and confirm `loopCount_` returns to 0. Specifically: a test that advances the loop counter, disables the arp, re-enables it, and confirms `loopCount_` is 0. A complementary test confirms that disabling alone (without re-enable) does NOT reset `loopCount_`.
- **SC-014**: The condition PRNG produces non-repeating sequences that are distinct from the NoteSelector PRNG (different seed). Verified by a test that generates 1000 values from each PRNG and confirms they differ.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Phase 7 (075-euclidean-timing) is fully complete and merged, providing Euclidean timing mode, pattern gating, 4 Euclidean parameters (3230-3233), and the evaluation order precedent (Euclidean before modifiers).
- The reserved parameter ID range 3234-3299 in `plugin_ids.h` has room for the 34 condition parameters (3240-3272 for condition lane, 3280 for fill toggle). No sentinel adjustment is needed (kArpEndId = 3299, kNumParameters = 3300).
- The `ArpeggiatorCore` is header-only at `dsp/include/krate/dsp/processors/arpeggiator_core.h`.
- ArpLane<uint8_t> zero-initializes steps to 0, which for the condition lane means `TrigCondition::Always` (value 0). This is the correct default, but explicit initialization in the constructor is preferred for clarity.
- The existing `Xorshift32` PRNG at `dsp/include/krate/dsp/core/random.h` (Layer 0) is the correct choice for probability evaluation. It is fast (3 XOR operations per call), real-time safe, and already used by NoteSelector for Random/Walk modes.
- The UI for editing condition steps (dropdown per step for condition selection) is deferred to Phase 11. This phase only exposes condition parameters through the VST3 parameter system.
- The condition lane stores `TrigCondition` enum values as `uint8_t` (0-17). This fits within the existing `ArpLane<uint8_t>` template with no changes to ArpLane itself.
- The "loop" concept for A:B ratios is defined as one complete cycle of the condition lane (when the condition lane wraps from its last step back to step 0). This is the simplest and most predictable definition. Alternative definitions (LCM of all lane lengths, or pattern-length-based) were considered but rejected because they would make A:B behavior dependent on unrelated lane configurations, which is counterintuitive.
- The Elektron A:B ratio range goes from 1:2 through 8:8 on hardware. This spec implements a subset (through 4:4) as specified in the roadmap. The 18 enum values provide a good balance of utility without excessive parameter range. The range can be extended in a future phase if needed.
- Elektron hardware also supports PRE (previous condition result), NEI (neighbor track condition), and inverted 1ST (!1ST) conditions. These are not included in this spec as they are either not applicable to a single-track arpeggiator (NEI) or add inter-step state coupling complexity (PRE) beyond what the roadmap specifies. They can be added as future extensions.

### Constraints & Tradeoffs

**Condition evaluation placement: after Euclidean, before modifiers**

The condition evaluation is placed after Euclidean gating and before modifier evaluation. This means:
- Euclidean rest steps skip condition evaluation entirely (the step is already gated).
- Condition-failed steps skip modifier evaluation (the step is gated before modifiers run).
- This creates three layers of gating: Euclidean (rhythmic structure) -> Conditions (evolutionary/probabilistic) -> Modifiers (per-step articulation overrides).

The alternative (evaluating conditions before Euclidean) was rejected because:
- Euclidean timing is a structural rhythmic generator; conditions are a filter on top of structure.
- Evaluating conditions on Euclidean rest steps would waste PRNG values and create confusion about what "loop count" means when some steps are structurally inactive.
- The roadmap explicitly states this dependency: "Phase 8 depends on Phase 7 because Euclidean determines which steps exist to apply conditions to."

**Loop count tied to condition lane length, not pattern length**

The loop count increments when the condition lane wraps. In a system with polymetric lanes, there is no single "pattern length." Options considered:
1. **Condition lane wrap** (chosen): Loop count is tied to the condition lane's own cycle. If the condition lane is 4 steps long, it wraps every 4 arp steps.
2. **Longest lane LCM**: Loop count increments when all lanes simultaneously wrap. This would create extremely long loop cycles (e.g., LCM(3,5,7,8) = 840 steps) making A:B ratios impractical.
3. **Fixed pattern length parameter**: A separate "pattern length" control. Adds UI complexity without clear benefit.

Option 1 is preferred because:
- It is self-contained -- the condition lane controls its own loop definition.
- It produces musically useful loop counts (short enough for A:B ratios to be audible).
- It matches the Elektron model where the loop count is tied to the pattern/track length.

**Probability PRNG behavior**

The PRNG is consumed once per step for probability conditions. It is NOT consumed for non-probability conditions (Always, A:B, First, Fill, NotFill) or for Euclidean rest steps. This means:
- The random sequence is deterministic for a given seed and pattern configuration.
- Changing the condition on one step (e.g., from Prob50 to Always) affects which random values subsequent probability steps receive -- the sequence shifts.
- This is acceptable because the user does not need to predict individual random outcomes; they only care about the statistical distribution.

The PRNG is NOT reset on pattern reset (`resetLanes()`). This ensures continuous randomness across resets. If the PRNG were reset to its initial seed on every pattern restart, the same "random" pattern would play on every restart, defeating the purpose of probability conditions.

The construction-time seed is a fixed compile-time constant (7919), not a time-based or entropy-based value. This is a deliberate choice: testability takes priority over per-load variation. A test that constructs a fresh `ArpeggiatorCore` gets a deterministic PRNG sequence it can reason about. In practice the sequence is statistically well-distributed within a few steps regardless of starting point, so the identical first-few-outcomes-per-load is imperceptible in musical use. This is consistent with how `NoteSelector`'s PRNG is seeded (fixed seed 42).

**Subset of Elektron conditions implemented**

Elektron hardware (Digitakt, Syntakt, Analog Rytm) offers additional condition types not included in this spec:
- **PRE** (previous trig condition result): Not implemented. PRE requires tracking the result of the previous step's condition evaluation, adding inter-step state coupling. Can be added in a future phase.
- **NEI** (neighbor track condition): Not applicable. The arp is a single-track system; there is no neighbor track concept.
- **!1ST** (not first): Not implemented explicitly. Can be approximated by using A:B ratios or added as a trivial extension.
- **A:B ratios beyond 4:4**: Elektron supports up to 8:8. The roadmap specifies through 4:4. Extensions can be added by expanding the enum.
- **1-99% probability granularity**: Elektron offers 21 probability steps (1%, 2%, 4%, 6%, 9%, 13%, 19%, 25%, 33%, 41%, 50%, 59%, 67%, 75%, 81%, 87%, 91%, 94%, 96%, 98%, 99%). This spec uses 5 coarser probability levels (10%, 25%, 50%, 75%, 90%) matching the roadmap. The curated set covers the musically useful range without excessive parameter granularity.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| ArpeggiatorCore | `dsp/include/krate/dsp/processors/arpeggiator_core.h` | **Primary extension target** -- condition lane added as member, `fireStep()` modified for condition gating, `resetLanes()` extended for condition state reset, new state members (loopCount_, fillActive_, conditionRng_). |
| ArpLane<T> | `dsp/include/krate/dsp/primitives/arp_lane.h` | **Reused as** `ArpLane<uint8_t>` for condition lane. No changes needed to ArpLane itself. |
| Xorshift32 | `dsp/include/krate/dsp/core/random.h` | **Reused** for probability evaluation. A dedicated instance `conditionRng_` with a distinct seed. |
| fireStep() | `arpeggiator_core.h` | Modified to insert condition evaluation after Euclidean gating check but before modifier evaluation. |
| resetLanes() | `arpeggiator_core.h` | Extended to include `conditionLane_.reset()`, `loopCount_ = 0`. |
| ArpeggiatorParams | `plugins/ruinae/src/parameters/arpeggiator_params.h` | Extended with condition lane atomic storage and fill toggle. |
| saveArpParams/loadArpParams | `plugins/ruinae/src/parameters/arpeggiator_params.h` | Extended to serialize/deserialize condition data after Euclidean data. |
| handleArpParamChange() | `plugins/ruinae/src/parameters/arpeggiator_params.h` | Extended to dispatch condition parameter IDs (3240-3272, 3280). |
| plugin_ids.h | `plugins/ruinae/src/plugin_ids.h` | New parameter IDs added at 3240-3272 and 3280. No sentinel change needed. |
| Euclidean gating pattern | `arpeggiator_core.h:1017-1047` | **Reference pattern** for condition gating: the Euclidean rest path (cancel pending noteOffs, emit noteOff, break tie chain, return early) is the same pattern the condition-fail path should follow. |

**Initial codebase search for key terms:**

- `TrigCondition`: Not found in codebase. Name is safe -- no ODR risk.
- `conditionLane`: Not found as a member variable. Safe.
- `loopCount_`: Not found in arpeggiator. Safe.
- `fillActive_`: Not found. Safe.
- `conditionRng_`: Not found. Safe.
- `evaluateCondition`: Not found. Safe.

**Search Results Summary**: No existing implementations of conditional triggers, loop counting, or fill mode exist in the arpeggiator. All proposed names are safe to introduce. The `Xorshift32` PRNG (Layer 0) is the sole existing component to reuse for randomness.

### Forward Reusability Consideration

**Sibling features at same layer** (known from roadmap):

- Phase 9 (Spice/Dice + Humanize): The Spice/Dice random overlay may interact with the condition lane. The Dice operation could randomize condition lane step values. The condition lane's `uint8_t` storage (0-17 enum range) is compatible with bounded random mutation.
- Phase 10 (Modulation Integration): The fill toggle could be a modulation destination (e.g., an LFO or envelope toggling fill mode rhythmically). The `setFillActive(bool)` setter API supports this.
- Phase 11 (Arpeggiator UI): Condition step editing requires a dropdown per step (18 condition choices). The parameter IDs and value ranges defined here provide the interface contract. The `formatArpParam()` display strings will be used by the UI.

**Potential shared components:**

- The condition evaluation pattern (gating check after Euclidean, before modifiers, with rest-like failure path) established here completes the three-layer gating chain: Euclidean -> Condition -> Modifier. No additional gating layers are planned in subsequent phases.
- The `loopCount_` tracking mechanism could be exposed as a modulation source in Phase 10 (e.g., "loop count" as a mod source for creating patterns that evolve over many loops).
- The `fillActive_` flag pattern (performance toggle, not serialized in DSP state, preserved across resets) could serve as a template for future performance controls.

## Implementation Verification *(mandatory at completion)*

<!--
  CRITICAL: This section MUST be completed when claiming spec completion.
  Constitution Principle XVI: Honest Completion requires explicit verification
  of ALL requirements before claiming "done".
-->

### Compliance Status

### Build/Test/Pluginval Results
- Build: PASS -- zero warnings, zero errors (Release build)
- dsp_tests: 22,011,843 assertions in 5,995 test cases -- all passed
- ruinae_tests: 8,669 assertions in 468 test cases -- all passed
- Pluginval: PASS at strictness level 5
- Clang-tidy: PASS -- 0 errors, 0 warnings (DSP + Ruinae targets)

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | `arpeggiator_core.h:74-94` -- TrigCondition enum class, 18+1 values |
| FR-002 | MET | `arpeggiator_core.h:74` -- `enum class TrigCondition : uint8_t` |
| FR-003 | MET | `arpeggiator_core.h:1684` -- `ArpLane<uint8_t> conditionLane_` |
| FR-004 | MET | `arpeggiator_core.h:182` -- constructor sets step 0 to Always; test `ConditionState_DefaultValues` |
| FR-005 | MET | `arpeggiator_core.h:179-182` -- explicit conditionLane_ init in constructor |
| FR-006 | MET | `arpeggiator_core.h:1123` -- conditionLane_.advance() in fireStep() lane batch; test `ConditionLane_PolymetricCycling` |
| FR-007 | MET | `arpeggiator_core.h:474-479` -- const and non-const conditionLane() accessors; test `ConditionLane_Accessors` |
| FR-008 | MET | `arpeggiator_core.h:1685` -- `size_t loopCount_{0}` |
| FR-009 | MET | `arpeggiator_core.h:1686` -- `bool fillActive_{false}`; test `FillActive_RoundTrip` |
| FR-010 | MET | `arpeggiator_core.h:1687` -- `Xorshift32 conditionRng_{7919}`; tests `ConditionRng_DeterministicSeed7919`, `ConditionRng_DistinctFromNoteSelector` |
| FR-011 | MET | `arpeggiator_core.h:1131` -- wrap detection + deferred increment at 1169-1171, 1197-1200, 1206-1208; test `LoopCount_IncrementOnConditionLaneWrap` |
| FR-012 | MET | `arpeggiator_core.h:1117-1208` -- Euclidean->Condition->Modifier order; test `EuclideanRest_ConditionNotEvaluated_PrngNotConsumed` |
| FR-013 | MET | `arpeggiator_core.h:1061-1103` -- evaluateCondition() 18-way switch with all cases |
| FR-014 | MET | `arpeggiator_core.h:1180-1201` -- condition-fail rest path; test `ConditionFail_TreatedAsRest_EmitsNoteOff` |
| FR-015 | MET | `arpeggiator_core.h:1179` -- condition pass falls through to modifier; test `ConditionPass_ModifierRest_StillSilent` |
| FR-016 | MET | `arpeggiator_core.h:1147-1173` -- Euclidean rest returns before condition eval; test `EuclideanRest_ConditionNotEvaluated_PrngNotConsumed` |
| FR-017 | MET | `arpeggiator_core.h:1623-1624` -- resetLanes() resets conditionLane_ and loopCount_; tests `LoopCount_ResetOnRetrigger`, `LoopCount_ResetOnReEnable` |
| FR-018 | MET | `arpeggiator_core.h:1131,1169-1171` -- loopCount_ increments on wrap only; test `LoopCount_IncrementEveryStep_Length1Lane` |
| FR-019 | MET | `arpeggiator_core.h:1685` -- size_t (64-bit), no overflow concern |
| FR-020 | MET | `arpeggiator_core.h:486` -- `void setFillActive(bool active) noexcept` |
| FR-021 | MET | `arpeggiator_core.h:489` -- `bool fillActive() const noexcept` |
| FR-022 | MET | `arpeggiator_core.h:1625` -- fillActive_ NOT reset in resetLanes(); test `FillActive_PreservedAcrossResetLanes` |
| FR-023 | MET | `arpeggiator_core.h:1686` -- fillActive_ is runtime bool, not serialized in DSP |
| FR-024 | MET | `arpeggiator_core.h:1137-1321` -- Euclidean->Condition->Modifier->Ratchet chain |
| FR-025 | MET | `arpeggiator_core.h:1123,1172` -- lane advances before Euclidean check; condition not evaluated on Euclidean rest |
| FR-026 | MET | `arpeggiator_core.h:1179` -- Euclidean hit proceeds to condition eval; test `EuclideanHit_ConditionFail_TreatedAsRest` |
| FR-027 | MET | `arpeggiator_core.h:1180-1201` -- condition fail returns before modifier eval |
| FR-028 | MET | `arpeggiator_core.h:1179,1210` -- condition pass proceeds to modifier; test `ConditionPass_ModifierRest_StillSilent` |
| FR-029 | MET | `arpeggiator_core.h:1191` -- `tieActive_ = false` in condition-fail path; test `ConditionFail_BreaksTieChain` |
| FR-030 | MET | `arpeggiator_core.h:1180-1201` -- condition fail returns before ratchet; test `ConditionFail_SuppressesRatchet` |
| FR-031 | MET | `arpeggiator_core.h:1321` -- ratchet after condition pass |
| FR-032 | MET | `arpeggiator_core.h:1179` -- condition evaluated once per step, not per chord note; test `ConditionChordMode_AppliesUniformly` |
| FR-033 | MET | `arpeggiator_core.h:1193-1195` -- swing counter in condition-fail path |
| FR-034 | MET | `arpeggiator_core.h:1623-1626` -- resetLanes() resets conditionLane_ and loopCount_, preserves fillActive_ and conditionRng_; test `ResetLanes_ResetsConditionStateNotFill` |
| FR-035 | MET | `arpeggiator_core.h:1626` -- conditionRng_ NOT reset; test `ConditionRng_NotResetOnResetLanes` |
| FR-036 | MET | `arpeggiator_core.h:281-285` -- setEnabled(false) does not call resetLanes(); test `LoopCount_NotResetOnDisable` |
| FR-037 | MET | `arpeggiator_core.h:1500-1504` -- defensive branch advances conditionLane_ and checks wrap; test `ConditionLane_AdvancesOnDefensiveBranch` |
| FR-038 | MET | `plugin_ids.h:1008-1044` -- 34 condition parameter IDs (3240-3272, 3280) |
| FR-039 | MET | `plugin_ids.h:1047-1050` -- kArpEndId=3299, kNumParameters=3300 unchanged |
| FR-040 | MET | `arpeggiator_params.h:528-548` -- kCanAutomate on all 34; kIsHidden on 32 step IDs only; test `ConditionParams_AllRegistered_CorrectFlags` |
| FR-041 | MET | `arpeggiator_params.h:83-85` -- 3 atomic members in ArpeggiatorParams |
| FR-042 | MET | `arpeggiator_params.h:259-309` -- handleArpParamChange dispatch for 34 IDs; tests `HandleParamChange_LaneLength`, `_StepValues`, `_FillToggle` |
| FR-043 | MET | `arpeggiator_params.h:857-863` -- saveArpParams writes condition data after Euclidean; test `ConditionState_RoundTrip_SaveLoad` |
| FR-044 | MET | `arpeggiator_params.h:993-1009` -- loadArpParams EOF-safe with clamping; tests `Phase7Backward_Compat`, `CorruptStream_*`, `OutOfRange_ValuesClamped` |
| FR-045 | MET | `processor.cpp:1344-1352` -- expand-write-shrink pattern; test `ConditionState_ApplyToEngine_ExpandWriteShrink` |
| FR-046 | MET | `processor.cpp:1354` -- setFillActive() called in applyParamsToEngine |
| FR-047 | MET | `arpeggiator_params.h:719-789` -- formatArpParam for all condition types; tests `FormatLaneLength`, `FormatStepValues`, `FormatFillToggle` |
| FR-048 | MET | `arpeggiator_params.h:1172-1185` -- loadArpParamsToController syncs 34 IDs; test `ConditionState_ControllerSync_AfterLoad` |
| SC-001 | MET | Tests Prob10/25/50/75/90 distribution at +/-3% over ~10,000 steps; all 5 pass |
| SC-002 | MET | All 9 A:B ratios verified across 12 loops; 7 tests pass |
| SC-003 | MET | First fires only on loop 0, fires again after reset; 3 tests pass |
| SC-004 | MET | Fill/NotFill toggle with [Always,Fill,NotFill,Always] pattern; 5 tests pass |
| SC-005 | MET | Default Always = Phase 7 identical (zero tolerance); test `DefaultCondition_Always_Phase7Identical` |
| SC-006 | MET | Euclidean + Condition composition verified; 2 tests pass |
| SC-007 | MET | Condition + modifier/ratchet interaction; 4 tests pass |
| SC-008 | MET | Polymetric: condition lane 3 + velocity 5 = 15-step cycle; test `ConditionLane_PolymetricCycling` |
| SC-009 | MET | State round-trip preserves all values; test `ConditionState_RoundTrip_SaveLoad` |
| SC-010 | MET | Phase 7 backward compat defaults correctly; test `ConditionState_Phase7Backward_Compat` |
| SC-011 | MET | Zero heap allocation in condition paths (code inspection); kCondNames[] uses const char* const |
| SC-012 | MET | 34 params registered with correct flags and display strings; 4 tests pass |
| SC-013 | MET | Loop count resets on retrigger/re-enable, NOT on disable; 3 tests pass |
| SC-014 | MET | PRNG seed 7919 deterministic and distinct from NoteSelector seed 42; 2 tests pass |

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

### Honest Assessment

**Overall Status**: COMPLETE

### Self-Check (T135)
1. No test thresholds changed from spec (SC-001 uses +/-3% exactly)
2. No placeholder/stub/TODO in new code
3. No features removed from scope
4. Spec author would consider this done
5. User would not feel cheated
