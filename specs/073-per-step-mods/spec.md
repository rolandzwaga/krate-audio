# Feature Specification: Per-Step Modifiers (Slide, Accent, Tie, Rest)

**Feature Branch**: `073-per-step-mods`
**Plugin**: KrateDSP (Layer 2 processor) + Ruinae (plugin integration)
**Created**: 2026-02-21
**Status**: Complete
**Input**: User description: "Phase 5 of Arpeggiator Roadmap: Per-Step Modifiers. Add TB-303-inspired per-step modifier flags (Slide, Accent, Tie, Rest) as a bitmask lane to the ArpeggiatorCore. Extend ArpEvent with a legato field. Add accent velocity and slide time parameters."
**Depends on**: Phase 4 (072-independent-lanes) -- COMPLETE

## Clarifications

### Session 2026-02-21

- Q: How should RuinaeEngine expose the legato noteOn to callers? → A: Add `bool legato = false` as a defaulted third parameter: `noteOn(uint8_t note, uint8_t velocity, bool legato = false)`. All existing call sites remain valid without modification.
- Q: When a Tie or Slide step fires while the arp is in Chord mode (multiple notes in currentArpNotes_), what sustains or glides? → A: Apply to all currently sounding notes. Tie sustains the full previous chord (suppress all pending noteOffs); Slide emits legato noteOn for every new chord note, suppressing all previous chord noteOffs.
- Q: Should the modifier lane advance in the fireStep() defensive branch where result.count == 0 (held buffer became empty)? → A: Yes. The modifier lane MUST advance in that branch, matching FR-010's rule for all other lanes. All four lanes advance exactly once per arp step tick regardless of whether a note fires.
- Q: Which step-setting pattern should applyParamsToArp() use for the modifier lane? → A: Follow the existing Phase 4 actual pattern (expand-write-shrink): call setLength(32) first to allow writing all 32 step indices safely, then write each step value by index (using ArpLane::setStep()), then call setLength(actualLength) once to set the active length. This is mandatory because ArpLane::setStep() clamps the index to length_-1; if the lane's current length is 1 (the default), all writes to step indices 1-31 would silently write to step 0, corrupting the data. The expand-first pattern is identical to what Phase 4 uses for velocity, gate, and pitch lanes.
- Q: When a slide step fires in Poly mode (MonoHandler bypassed), which component receives the slide time so portamento activates? → A: The slide time is set on both MonoHandler (for Mono mode) AND on each RuinaeVoice directly (for Poly mode). The legato flag on noteOn signals portamento activation; the slide time controls glide duration in both paths.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Rest Steps for Rhythmic Silence (Priority: P1)

A musician programming an arpeggio wants certain steps to be silent -- rhythmic gaps that create space in the pattern. They mark specific steps in the modifier lane with the Rest flag (by clearing the Active bit). When the arpeggiator reaches a rest step, no noteOn is emitted for that step, but time advances normally so the following step fires at its expected position.

**Why this priority**: Rest is the simplest modifier and the foundation of rhythmic programming. Without rest, every step produces a note, making it impossible to create rhythmic patterns with silence. Rest also validates the core modifier lane infrastructure (bitmask storage, per-step flag evaluation, lane cycling) that all other modifiers depend on.

**Independent Test**: Can be fully tested by configuring a modifier lane with some steps marked as rest and verifying that those steps produce no noteOn events while non-rest steps produce normal noteOn/noteOff pairs.

**Acceptance Scenarios**:

1. **Given** a modifier lane of length 4 with steps [Active, Active, Rest, Active], **When** the arp advances 4 steps, **Then** noteOn events are emitted for steps 0, 1, and 3 only. Step 2 produces no noteOn. All steps consume the same time duration.
2. **Given** a step marked as Rest, **When** the arp reaches that step, **Then** a noteOff is still emitted for any previously sounding note at the appropriate gate boundary, preventing stuck notes.
3. **Given** a modifier lane of length 3 with step 1 as Rest and a velocity lane of length 5, **When** the arp plays 15 steps, **Then** the modifier lane cycles independently of the velocity lane (polymetric behavior), and every 3rd step from the modifier lane's perspective is silent.

---

### User Story 2 - Tie Steps for Sustained Notes (Priority: P1)

A producer wants certain notes in the arpeggio to sustain across multiple steps without retriggering the envelope. They mark steps with the Tie flag. When the arpeggiator reaches a tied step, it suppresses the noteOff from the previous step and does not emit a new noteOn, allowing the previous note to continue sounding with its envelope uninterrupted.

**Why this priority**: Tie is essential for creating legato phrases and sustained melodic lines within an arpeggio. Without tie, every note is staccato and percussive, limiting expressiveness. Tie is also the modifier that most directly interacts with gate length (it overrides it).

**Independent Test**: Can be fully tested by configuring a tie step after a normal step and verifying that no noteOff is emitted between them and no new noteOn fires on the tied step.

**Acceptance Scenarios**:

1. **Given** a modifier lane with steps [Active, Tie, Tie, Active], **When** the arp plays 4 steps, **Then** step 0 emits a noteOn, steps 1 and 2 emit neither noteOn nor noteOff (the note from step 0 sustains), and step 3 emits noteOff for the step-0 note followed by a new noteOn.
2. **Given** a tied step, **When** the gate lane provides a specific gate value for that step, **Then** the gate value is ignored (tie overrides gate -- the note sustains regardless of gate length).
3. **Given** a sequence of [Active, Tie, Rest, Active], **When** the arp plays, **Then** step 0 emits noteOn, step 1 sustains (tie), step 2 ends the sustained note (rest terminates the tie chain and emits noteOff), and step 3 starts a fresh noteOn.

---

### User Story 3 - Slide Steps for Portamento Glide (Priority: P2)

A synthesist creating acid-style basslines wants the classic TB-303 slide effect where the pitch glides smoothly from one note to the next rather than jumping. They mark steps with the Slide flag. When the arpeggiator reaches a slide step, it emits a legato noteOn (no envelope retrigger) that signals the voice engine to apply portamento from the current pitch to the new pitch.

**Why this priority**: Slide is the signature TB-303 sound and the primary differentiator of this modifier system. However, it requires coordination between the arpeggiator (emitting legato events) and the voice engine (applying portamento), making it more complex than Rest or Tie. It builds on the legato concept introduced by Tie.

**Independent Test**: Can be tested at the DSP level by verifying that slide steps produce ArpEvent entries with the `legato` field set to true and that no noteOff is emitted before the new noteOn (legato transition).

**Acceptance Scenarios**:

1. **Given** a modifier lane with steps [Active, Slide, Active], **When** the arp plays 3 steps, **Then** step 0 emits a normal noteOn (legato=false), step 1 emits a noteOn with legato=true (no preceding noteOff for the step-0 note -- the voice should glide to the new pitch), and step 2 emits noteOff for step-1's note followed by a normal noteOn (legato=false).
2. **Given** a slide step, **When** the ArpEvent is generated, **Then** the event's `legato` field is true and the `note` field contains the new target pitch (from NoteSelector + pitch lane offset).
3. **Given** the slide time parameter set to 60ms, **When** a slide step fires, **Then** the legato noteOn event carries the information needed for the engine to apply a 60ms portamento to the target pitch.

---

### User Story 4 - Accent Steps for Velocity Boost (Priority: P2)

A musician wants certain steps to be louder or more pronounced than others, emulating the TB-303 accent circuit. They mark steps with the Accent flag. When the arpeggiator reaches an accented step, the velocity is boosted by a configurable amount (the accent velocity parameter), making the note stand out from non-accented steps.

**Why this priority**: Accent adds dynamic emphasis that creates groove and musical interest. It is simpler than Slide (no engine coordination needed -- just a velocity modification) but less fundamental than Rest and Tie.

**Independent Test**: Can be fully tested by configuring accent steps and verifying that their noteOn events carry a higher velocity than non-accented steps, with the boost amount matching the accent velocity parameter.

**Acceptance Scenarios**:

1. **Given** a modifier lane with steps [Active, Accent, Active, Accent] and accent velocity set to 30, and the input velocity is 80, **When** the arp plays 4 steps, **Then** steps 0 and 2 have velocity 80 (normal), steps 1 and 3 have velocity 110 (80 + 30 accent boost).
2. **Given** an accented step where the input velocity plus accent boost exceeds 127, **When** the event is generated, **Then** the velocity is clamped to 127.
3. **Given** a step with both Accent and velocity lane scaling, **When** the event fires, **Then** accent boost is applied AFTER velocity lane scaling. The computation is: `clamp(round(inputVelocity * velLaneScale) + accentBoost, 1, 127)`.

---

### User Story 5 - Combined Modifiers for Expressive Patterns (Priority: P2)

A sound designer wants to combine modifiers on individual steps to create complex, expressive patterns. For example, a slide step can also be accented (slide + accent), producing a gliding note that is louder. The bitmask design allows multiple flags to be set simultaneously.

**Why this priority**: Modifier combinations are what make the system musically powerful. Individual modifiers are useful, but combinations like Slide+Accent create the signature acid bassline sound. This is an emergent property of the bitmask architecture.

**Independent Test**: Can be tested by setting multiple flags on a single step and verifying that all active behaviors apply simultaneously.

**Acceptance Scenarios**:

1. **Given** a step with both Slide and Accent flags, **When** the event fires, **Then** the noteOn has both legato=true AND boosted velocity.
2. **Given** a step with both Tie and Accent flags, **When** the event fires, **Then** the previous note sustains (tie behavior) AND the accent has no audible effect on this step (no new note is triggered, so there is no velocity to boost).
3. **Given** a step with Rest flag combined with any other flag (e.g., Rest+Accent, a step value without kStepActive set but with kStepAccent set), **When** the event fires, **Then** rest takes priority -- no noteOn is emitted regardless of other flags.

---

### User Story 6 - Modifier Lane Persistence (Priority: P3)

A user programs a complex modifier pattern with specific slide, accent, tie, and rest steps, then saves the preset. When they reload the preset, all modifier flags and the accent velocity and slide time parameters are restored exactly.

**Why this priority**: Without persistence, modifier programming is lost on save/load. This is an integration concern that builds on the core functionality.

**Independent Test**: Can be tested by configuring specific modifier values, saving plugin state, restoring it, and verifying all values match.

**Acceptance Scenarios**:

1. **Given** a modifier lane of length 8 with various flags per step, accent velocity of 35, and slide time of 50ms, **When** plugin state is saved and restored, **Then** all modifier lane step values, lane length, accent velocity, and slide time are identical after restore.
2. **Given** a preset saved before modifier support (Phase 4 or earlier), **When** loaded into this version, **Then** the modifier lane defaults to length 1 with value `kStepActive` (0x01, normal active step), accent velocity defaults to 30, and slide time defaults to 60ms. The arpeggiator behaves identically to Phase 4.

---

### Edge Cases

- What happens when all steps in the modifier lane are Rest? The arp produces no noteOn events but timing continues to advance. Pending noteOffs from previous non-rest steps still fire. The arp does not get stuck.
- What happens when `loadArpParams()` encounters EOF at exactly the `modifierLaneLength` read (the first new field)? This signals a clean Phase 4 preset. The function returns `true` (success) and all modifier fields retain their default-constructed values. What happens when EOF occurs after `modifierLaneLength` is successfully read but before `modifierLaneSteps[0]` (or any subsequent modifier field)? This signals a corrupt or truncated stream -- the function returns `false`. The caller (Processor::setState) MUST treat a `false` return as a load failure and restore plugin defaults for the entire state. Partial modifier data is never silently accepted.
- What happens when all steps are Tie? The first Tie step has no previous note to sustain (no preceding Active step), so it behaves as a Rest (no noteOn, no sustained note). Subsequent Tie steps also produce silence since there is nothing to sustain. Once an Active step fires, following Tie steps sustain that note.
- What happens when a Tie step follows a Rest step? The tie has no preceding note to sustain, so it behaves as a Rest (silence). The tie chain is broken by any Rest step.
- What happens when Slide is the first step after a reset (no previous note)? Without a preceding note, there is nothing to glide from. The step fires as a normal noteOn (legato=false) since portamento requires a source pitch.
- What happens when the modifier lane length differs from other lane lengths? The modifier lane cycles independently, producing polymetric modifier patterns. A 4-step modifier lane over a 5-step velocity lane produces a combined cycle of 20 steps.
- What happens when the modifier lane length is set to 0? Clamped to minimum length 1 (same as all other lanes, per ArpLane's existing FR-008 behavior).
- What happens when a step value in the modifier lane is 0x00 (no flags set, not even kStepActive)? This is equivalent to a Rest -- the step is inactive because the Active flag is not set.
- What happens when the accent velocity parameter is 0? Accent has no effect -- accented steps have the same velocity as normal steps. This is a valid configuration.
- What happens when the slide time is 0ms? Portamento completes instantly -- the pitch jumps to the target. Functionally identical to a non-slide legato noteOn (envelope still not retriggered).
- What happens when a Slide step is in the same position as a very short gate from the gate lane? Slide suppresses the noteOff of the previous note (legato transition), so the gate lane value is effectively overridden for the previous note's tail. The new slide note gets its own gate duration from the gate lane.
- What happens when the gate of the previous step expires before the slide step fires? If the gate is less than 100%, the previous note's noteOff fires before the next step begins. When the slide step evaluates, `currentArpNoteCount_` is already 0 because the noteOff cleared it. The slide step therefore has no preceding sounding note and fires as a normal noteOn with `legato = false`, consistent with FR-016. This is the correct and expected behavior -- slide only produces portamento when there is an actively sounding note to glide from.
- What happens when a Tie or Slide step fires in Chord mode (multiple notes sounding simultaneously)? Tie sustains the entire previous chord -- all pending noteOffs for all chord notes are suppressed. Slide emits a legato noteOn (legato=true) for every note in the new chord, suppressing all noteOffs for the previous chord. In both cases the operation applies uniformly to all currently sounding notes in currentArpNotes_.
- What happens when the user releases all held keys while a tie chain is active? `currentArpNotes_` tracks arp-emitted notes, not the user's held input keys (those are in `heldNotes_`). Releasing all held keys does not immediately clear `currentArpNotes_`. The tie chain continues unaffected until a non-tie step fires. At that non-tie step, the arp calls `selector_.advance()` which reads from the now-empty `heldNotes_`. If the result is empty (`result.count == 0`), the defensive branch fires: all lanes advance, no new note is triggered, `currentArpNotes_` is cleared, and `tieActive_` is set to false. The sustained note from the tie chain receives a noteOff at its gate boundary.

## Requirements *(mandatory)*

### Functional Requirements

**ArpStepFlags Enum (DSP Layer 2)**

- **FR-001**: The system MUST define an `ArpStepFlags` enum with bitmask values: `kStepActive = 0x01`, `kStepTie = 0x02`, `kStepSlide = 0x04`, `kStepAccent = 0x08`. The underlying type MUST be `uint8_t` to fit in the existing `ArpLane<uint8_t>` container.
- **FR-002**: The default step value for the modifier lane MUST be `kStepActive` (0x01), meaning a normal active step with no modifiers. This ensures backward compatibility -- the modifier lane at default produces identical behavior to Phase 4.

**ArpEvent Extension**

- **FR-003**: The `ArpEvent` struct MUST be extended with a `bool legato{false}` field. When `legato` is true, the receiving engine SHOULD suppress envelope retrigger and apply portamento (slide behavior). When false, the event behaves as a normal noteOn/noteOff.
- **FR-004**: The addition of the `legato` field MUST be backward-compatible. All existing event construction sites that do not set `legato` explicitly MUST default to `false` via the member initializer. No existing test or behavior changes.

**Modifier Lane in ArpeggiatorCore**

- **FR-005**: The ArpeggiatorCore MUST contain a modifier lane (`ArpLane<uint8_t> modifierLane_`) that stores per-step bitmask flags.
- **FR-006**: The modifier lane MUST advance once per arp step tick, simultaneously with the velocity, gate, and pitch lanes, and independently at its own configured length.
- **FR-007**: The modifier lane MUST default to length 1 with step value `kStepActive` (0x01). With this default, the arpeggiator produces identical output to Phase 4.
- **FR-008**: The `resetLanes()` method MUST be extended to also reset the modifier lane to step 0 and clear `tieActive_` to false. Because `tieActive_` is a private member with no public accessor, tests verify reset correctness behaviorally: after calling `resetLanes()`, a subsequent Tie step (with no preceding Active step in the new playback) MUST behave as silence (no noteOn), proving `tieActive_` was cleared. An optional `[[nodiscard]] bool isTieActive() const noexcept` getter may be added to ArpeggiatorCore for test inspection if preferred by the implementer.

**Rest Behavior**

- **FR-009**: When the current modifier step does NOT have the `kStepActive` flag set (value & 0x01 == 0), the step MUST be treated as a Rest: no noteOn event is emitted, but timing advances normally. A noteOff for any previously sounding note MUST still be emitted at the appropriate gate boundary to prevent stuck notes.
- **FR-010**: During a rest step, the velocity, gate, pitch, and modifier lanes MUST all still advance (their values are consumed but discarded), keeping all lanes synchronized in their independent cycling. This applies equally to the defensive `result.count == 0` branch in `fireStep()` where the held buffer became empty mid-playback -- all four lanes advance once in that branch as well.

**Tie Behavior**

- **FR-011**: When the current modifier step has the `kStepTie` flag set (value & 0x02), and there is a currently sounding arp note (or chord), the step MUST suppress the noteOff for ALL previous notes in `currentArpNotes_` AND suppress the noteOn for this step. The previously sounding note(s) sustain through the tied step's duration. In Chord mode, the full previous chord sustains.
- **FR-012**: Tie MUST override the gate lane value for the current step. The gate length is irrelevant during a tied step because no noteOff/noteOn boundary occurs.
- **FR-013**: When a tie step has no preceding sounding note (e.g., tie is the first step after reset, or tie follows a rest), the tie MUST behave as a rest -- no noteOn, no noteOff, silence.
- **FR-014**: A chain of consecutive tie steps MUST sustain the original note(s) across all of them. In single-note mode this is one note; in Chord mode this is the entire chord. The noteOff(s) for the original note(s) fire only when a non-tie step is reached (either Active, Rest, or Slide).

**Slide Behavior**

- **FR-015**: When the current modifier step has the `kStepSlide` flag set (value & 0x04), the step MUST emit a noteOn event with `legato = true` for each new note. All previous notes' noteOffs MUST be suppressed (no gap between notes), creating a legato transition where each voice glides from the previous pitch to the new pitch. In Chord mode, a legato noteOn is emitted for every note in the new chord and all noteOffs for the previous chord are suppressed.
- **FR-016**: When a slide step has no preceding sounding note (first step after reset, or after rest), the slide MUST behave as a normal active step -- noteOn with `legato = false`, since there is no source pitch to glide from.
- **FR-017**: Slide steps MUST advance the pitch lane normally. The `legato` noteOn carries the new target pitch (NoteSelector output + pitch lane offset). The voice engine is responsible for applying the actual portamento interpolation using the configured slide time.
- **FR-018**: The ArpeggiatorCore MUST store a configurable slide time (in milliseconds) accessible via a setter method (`setSlideTime(float ms)`). This stored value (`slideTimeMs_`) is NOT read by `fireStep()` and is NOT embedded in ArpEvent -- it exists solely for API symmetry with `setAccentVelocity()` and to allow future DSP-layer use. The effective routing is: ArpeggiatorParams -> processor's `applyParamsToArp()` -> `engine_.setPortamentoTime(slideTime)`, which sets portamento on both MonoHandler (for Mono mode) and on each RuinaeVoice directly (for Poly mode). This forwarding happens unconditionally on every `applyParamsToArp()` call so that the glide duration is always current in both voice modes.

**Accent Behavior**

- **FR-019**: When the current modifier step has the `kStepAccent` flag set (value & 0x08), the velocity of the noteOn event MUST be boosted by the configurable accent velocity amount.
- **FR-020**: The accent velocity boost MUST be applied AFTER velocity lane scaling: `finalVelocity = clamp(round(inputVelocity * velLaneScale) + accentBoost, 1, 127)`.
- **FR-021**: The accent velocity amount MUST be a configurable parameter with range 0-127 and a default of 30.
- **FR-022**: When a step has both Accent and another modifier (Slide, Tie, Rest), the interaction rules are:
  - Accent + Active: velocity boosted, normal noteOn. Both apply.
  - Accent + Slide: velocity boosted, legato noteOn. Both apply.
  - Accent + Tie: accent has no effect (tie suppresses noteOn, so there is no velocity to boost).
  - Accent + Rest: accent has no effect (rest suppresses noteOn).

**Modifier Combination Priority**

- **FR-023**: When multiple modifier flags are set on a single step, the following priority rules MUST apply:
  1. Rest (kStepActive not set) takes highest priority: if the step is not active, no noteOn fires regardless of other flags.
  2. Tie (kStepTie) takes next priority: if active and tied, the previous note sustains regardless of Slide or Accent.
  3. Slide (kStepSlide) applies if active and not tied: legato noteOn is emitted.
  4. Accent (kStepAccent) applies to any step that results in a noteOn emission -- that is, any step not suppressed by Rest or Tie. This includes normal Active steps and Slide steps. Accent has no effect on Tie or Rest steps because those paths do not emit a noteOn event; there is no velocity to boost.

**Accessor Methods**

- **FR-024**: The ArpeggiatorCore MUST provide `modifierLane()` accessor methods (const and non-const) following the same pattern as `velocityLane()`, `gateLane()`, and `pitchLane()`.
- **FR-025**: The ArpeggiatorCore MUST provide `setAccentVelocity(int amount)` and `setSlideTime(float ms)` setter methods for the accent and slide parameters.

**Plugin Integration (Ruinae)**

- **FR-026**: The plugin MUST expose per-step modifier parameters through the VST3 parameter system:
  - Modifier lane length: `kArpModifierLaneLengthId = 3140` (discrete: 1-32)
  - Modifier lane steps 0-31: `kArpModifierLaneStep0Id = 3141` through `kArpModifierLaneStep31Id = 3172` (discrete: 0-255, default `kStepActive` = 1)
  - Accent velocity: `kArpAccentVelocityId = 3180` (discrete: 0-127, default 30)
  - Slide time: `kArpSlideTimeId = 3181` (continuous: 0-500ms, default 60ms)
- **FR-027**: All modifier parameters MUST support host automation (`kCanAutomate` flag). Step parameters (kArpModifierLaneStep0Id through kArpModifierLaneStep31Id) MUST additionally have `kIsHidden` to avoid cluttering the host's parameter list, consistent with the velocity, gate, and pitch lane step parameters from Phase 4. The length parameter (kArpModifierLaneLengthId) and configuration parameters (kArpAccentVelocityId, kArpSlideTimeId) MUST NOT have `kIsHidden` as they are user-facing controls.
- **FR-028**: The `ArpeggiatorParams` struct MUST be extended with atomic storage for modifier lane data: `std::atomic<int>` for modifier lane length, 32 `std::atomic<int>` for modifier lane steps (int to guarantee lock-free; cast to `uint8_t` at DSP boundary), `std::atomic<int>` for accent velocity, and `std::atomic<float>` for slide time.
- **FR-029**: Modifier lane state (length, all step values, accent velocity, slide time) MUST be included in plugin state serialization and deserialization.
- **FR-030**: Loading a preset saved before modifier support (Phase 4 or earlier) MUST result in the modifier lane defaulting to length 1 with value `kStepActive`, accent velocity 30, and slide time 60ms. No crashes or corruption. Specifically: if `loadArpParams()` encounters EOF exactly at the first modifier field read (`modifierLaneLength`), it returns `true` and all modifier fields retain their defaults -- this is the clean Phase 4 backward-compat path. If `loadArpParams()` encounters EOF at any subsequent modifier field (after `modifierLaneLength` was successfully read), it returns `false` to signal a corrupt or truncated stream. When `loadArpParams()` returns `false`, the caller (Processor::setState) MUST treat the entire load as failed and restore all plugin parameters to their default-constructed values, not apply partial modifier state.
- **FR-031**: The `applyParamsToArp()` method in the processor MUST transfer modifier lane data to the ArpeggiatorCore using the expand-write-shrink pattern, identical to the existing velocity/gate/pitch lanes: call `modifierLane().setLength(32)` first (expand), then call `modifierLane().setStep(i, value)` for each of the 32 step indices (write), then call `modifierLane().setLength(actualLength)` once (shrink). The expand step is mandatory because `ArpLane::setStep()` clamps the index to `length_-1`; if the lane's current length is 1, all writes to step indices 1-31 would silently write to step 0, corrupting the pattern data.
- **FR-032**: The arp event routing in the processor (where ArpEvents are sent to the engine) MUST pass the `legato` flag through to the engine's noteOn method so that slide steps produce portamento behavior. The call site MUST use the form `engine_.noteOn(event.note, event.velocity, event.legato)`, passing the third argument explicitly.

**Engine Integration for Legato/Slide**

- **FR-033**: The `RuinaeEngine::noteOn()` signature MUST be extended to `noteOn(uint8_t note, uint8_t velocity, bool legato = false)`. The defaulted parameter preserves backward compatibility -- all existing call sites that omit the third argument continue to work without modification. When `legato = true`, the engine MUST suppress envelope retrigger and enable portamento for the receiving voice(s). In Poly mode, the engine routes the legato noteOn to the voice currently playing the note that matches `currentArpNotes_[0]` (the arp's most recently triggered note), found by searching `voices_` for an active voice playing that MIDI note number. If no matching active voice is found (defensive case), fall back to `dispatchPolyNoteOn()`. In Chord mode, locate and glide each voice matching the previous chord notes in `currentArpNotes_`. In Mono mode the engine routes through MonoHandler which already handles portamento.
- **FR-034**: The slide time parameter MUST be communicated to both the MonoHandler (Mono mode) and directly to each RuinaeVoice (Poly mode) via their respective portamento setters, so that the glide duration matches the user's configured slide time regardless of active voice mode. This wiring is done unconditionally in `applyParamsToArp()` whenever the slide time parameter changes.

### Key Entities

- **ArpStepFlags**: A `uint8_t` bitmask enum defining per-step modifier flags: `kStepActive` (0x01), `kStepTie` (0x02), `kStepSlide` (0x04), `kStepAccent` (0x08). Multiple flags can be combined on a single step. If `kStepActive` is not set, the step is a rest.
- **Modifier Lane**: An `ArpLane<uint8_t>` instance within ArpeggiatorCore. Each step stores a bitmask of `ArpStepFlags`. Advances once per arp step tick, independently of other lanes at its own configured length.
- **ArpEvent.legato**: A boolean field added to the existing `ArpEvent` struct. When true, the receiving engine should suppress envelope retrigger and apply portamento. Set to true only on slide steps.
- **Accent Velocity**: A configurable integer parameter (0-127, default 30) that determines how much velocity is added to accented steps. Applied after velocity lane scaling.
- **Slide Time**: A configurable float parameter (0-500ms, default 60ms) that determines the portamento duration for slide steps. Communicated to the engine's portamento mechanism.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Each modifier type (Rest, Tie, Slide, Accent) produces the correct event behavior as defined in the behavior table, verified by unit tests covering every row of the table with at least 2 test cases each (minimum 8 test cases total across the 4 modifier types).
- **SC-002**: With the modifier lane at default (length 1, value `kStepActive`), the arpeggiator produces bit-identical output to Phase 4 across 1000+ steps at 120/140/180 BPM. Zero tolerance -- same notes, velocities, sample offsets, and legato=false on all events. The Phase 4 reference output is captured before any Phase 5 code is written (task T003) and stored as a structured test fixture at `dsp/tests/fixtures/arp_baseline_{bpm}bpm.dat` (committed to the repository). Each fixture records the raw ArpEvent stream (note, velocity, sampleOffset for each event) as binary data. The bit-identical test compares against these fixtures and outputs a summary of the form "N steps compared, 0 mismatches at 120 BPM, 0 mismatches at 140 BPM, 0 mismatches at 180 BPM." Any mismatch count above zero is a test failure.
- **SC-003**: Slide steps produce ArpEvents with `legato = true`, verified by unit tests that inspect the event's legato field directly.
- **SC-004**: Accent steps produce velocities exactly equal to `clamp(round(inputVelocity * velLaneScale) + accentBoost, 1, 127)`, verified by arithmetic comparison in unit tests with at least 5 velocity/accent combinations including boundary cases (overflow to 127, low velocity + accent, accent=0).
- **SC-005**: Tie steps sustain across multiple consecutive steps without any noteOff/noteOn events in the tied region, verified by checking the raw event stream for absence of events during tied steps across at least a 3-step tie chain.
- **SC-006**: Modifier lane cycles independently of velocity, gate, and pitch lanes at different lengths, verified by a polymetric test (e.g., modifier length 3, velocity length 5 = combined cycle of 15 steps with no premature repetition).
- **SC-007**: Plugin state round-trip (save then load) preserves all modifier lane step values, lane length, accent velocity, and slide time exactly, verified by comparing every value before and after serialization.
- **SC-008**: Loading a Phase 4 preset (without modifier data) succeeds with default modifier values and the arpeggiator output is identical to Phase 4 behavior.
- **SC-009**: The modifier-enhanced `fireStep()` and all modifier evaluation logic perform zero heap allocation, verified by code inspection (no new/delete/malloc/free/vector/string/map in the modified code paths).
- **SC-010**: All modifier parameters (35 total: 1 length + 32 steps + accent velocity + slide time) are registered with the host, automatable, and display correct human-readable values. Verification requires both: (a) an integration test confirming all 35 parameter IDs (3140-3181) are registered with the correct flags (kCanAutomate on all; kIsHidden on steps 3141-3172 only); and (b) a formatting test calling `formatArpParam()` for each modifier parameter type and verifying the output string: modifier lane length displays as "N steps" (e.g., "8 steps"), modifier step values display as "0x{hex}" (e.g., "0x05"), accent velocity displays as an integer string (e.g., "30"), and slide time displays as "{value} ms" (e.g., "60 ms").

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Phase 4 (072-independent-lanes) is fully complete and merged, providing ArpLane<T> template, velocity/gate/pitch lanes, polymetric cycling, and the lane parameter infrastructure. ArpLane<T> default-constructs with length=1 and all step values zero-initialized (std::array default). For `ArpLane<uint8_t>`, zero means 0x00 which is a Rest (kStepActive bit not set). Therefore the constructor call `modifierLane_.setStep(0, static_cast<uint8_t>(kStepActive))` is mandatory -- without it, the default modifier lane would produce all-rests, silencing the arpeggiator entirely instead of producing Phase 4-identical behavior.
- The ArpeggiatorCore is header-only at `dsp/include/krate/dsp/processors/arpeggiator_core.h`.
- The existing `kArpEndId = 3199` and `kNumParameters = 3200` from Phase 4 provide sufficient headroom for Phase 5's modifier lane parameters (IDs 3140-3181 all within the 3000-3199 arp range), so no sentinel update is needed for this phase. Note: Phase 6 (Ratcheting, per the roadmap) allocates IDs 3190-3222, which exceeds kArpEndId=3199. Phase 6 implementers MUST update kArpEndId and kNumParameters accordingly.
- The RuinaeEngine has a `noteOn(note, velocity)` method that currently treats all noteOn events identically. This phase extends it to `noteOn(uint8_t note, uint8_t velocity, bool legato = false)` -- the defaulted third parameter is backward-compatible with all existing call sites.
- The MonoHandler at `dsp/include/krate/dsp/processors/mono_handler.h` already supports portamento (`setPortamentoTime`, `processPortamento`). The slide time parameter can be routed through the existing portamento mechanism.
- Future phases (6-8) will add additional lane types (ratchet, euclidean, condition) using the same ArpLane<T> infrastructure established in Phase 4 and extended here.
- The UI for editing modifier steps (toggle buttons for Rest/Tie/Slide/Accent per step) is deferred to Phase 11. This phase only exposes modifiers through the VST3 parameter system.

### Constraints & Tradeoffs

**Slide implementation: MonoHandler portamento vs. dedicated arp glide**

The roadmap identified two approaches for slide:
1. Route arp through MonoHandler portamento when slide flag is set.
2. Emit a "legato noteOn" event that the engine interprets as a pitch change without envelope retrigger.

Approach 2 (dedicated legato flag in ArpEvent) is chosen because:
- It works in both poly and mono voice modes.
- It keeps the ArpeggiatorCore independent of engine internals (clean layer separation).
- The engine already has the portamento infrastructure (MonoHandler) that can be triggered by the legato flag.
- In poly mode, the engine can route the legato noteOn to the same voice that is currently sounding the previous arp note, reusing the voice's portamento.

The cross-component change is minimal: `RuinaeEngine::noteOn()` gains a defaulted `bool legato = false` third parameter. All existing call sites remain valid. The slide time is forwarded unconditionally to both MonoHandler and each RuinaeVoice so the glide duration is correct in both Poly and Mono modes.

**Modifier step parameter encoding: individual params vs. bit fields**

Each modifier lane step is stored as a single integer parameter (0-255) representing the raw bitmask. This is simpler than decomposing into 4 separate boolean parameters per step (which would require 128 parameters for 32 steps x 4 flags). The host sees an integer value per step; the upcoming Phase 11 UI will present user-friendly toggle buttons.

**Accent + Tie interaction**

When both Accent and Tie are set, accent has no effect because tie suppresses the noteOn. This is the simplest and most predictable behavior. An alternative would be to "remember" the accent and apply it when the tie chain ends, but this adds complexity for minimal musical benefit.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| ArpeggiatorCore | `dsp/include/krate/dsp/processors/arpeggiator_core.h` | Primary extension target -- modifier lane added as member, fireStep() modified to evaluate flags |
| ArpEvent | `dsp/include/krate/dsp/processors/arpeggiator_core.h:56-63` | Extended with `bool legato{false}` field |
| ArpLane<T> | `dsp/include/krate/dsp/primitives/arp_lane.h` | Reused as `ArpLane<uint8_t>` for modifier lane. No changes needed to ArpLane itself. |
| resetLanes() | `dsp/include/krate/dsp/processors/arpeggiator_core.h:873-877` | Extended to include `modifierLane_.reset()` |
| ArpeggiatorParams | `plugins/ruinae/src/parameters/arpeggiator_params.h` | Extended with modifier lane atomic storage, accent velocity, and slide time fields |
| MonoHandler | `dsp/include/krate/dsp/processors/mono_handler.h` | Engine's existing portamento mechanism. Slide time parameter routed here for portamento glide. |
| RuinaeEngine | `plugins/ruinae/src/engine/ruinae_engine.h` | Engine noteOn path modified to accept legato flag and route accordingly |
| trance_gate_params.h | `plugins/ruinae/src/parameters/trance_gate_params.h` | Reference pattern for per-step parameter registration, same pattern used for modifier steps |

**Initial codebase search for key terms:**

- `ArpStepFlags`: Not found in codebase. Name is safe -- no ODR risk.
- `legato` field in ArpEvent: Not present. Field addition is safe.
- `modifierLane`: Not found. Name is safe.
- `accentVelocity`, `slideTime`: Not found in arpeggiator context. Names are safe.

**Search Results Summary**: No existing implementations of per-step modifiers, ArpStepFlags, or legato event fields exist. All proposed names are safe to introduce.

### Forward Reusability Consideration

**Sibling features at same layer** (known from roadmap):
- Phase 6 (Ratcheting): Will add `ArpLane<uint8_t> ratchetLane_` for ratchet counts (1-4). Uses the same lane infrastructure. The modifier evaluation in `fireStep()` provides a pattern for how ratchet evaluation will be added.
- Phase 7 (Euclidean Timing): May auto-generate timing patterns that interact with the Rest modifier. The rest evaluation logic established here will be reused.
- Phase 8 (Conditional Trig): Will add condition evaluation that can suppress steps similarly to Rest. The step-suppression logic in `fireStep()` provides a pattern.
- Phase 9 (Spice/Dice): Randomization overlay may apply to modifier lane values. The bitmask format must be compatible with random mutation.

**Potential shared components:**
- The `legato` field in ArpEvent is a general-purpose mechanism that future features could also use.
- The accent velocity boost logic (additive after lane scaling, clamped to [1, 127]) establishes a pattern for any future velocity modification features.
- The slide time as a separate global parameter (not per-step) is a deliberate simplification. If per-step slide time is needed later, it would require a new lane.

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*For EACH row below, you MUST perform these steps before writing the status:*
1. *Re-read the requirement from the spec*
2. *Open the implementation file and find the code that satisfies it -- record the file path and line number*
3. *Run or read the test that proves it -- record the test name and its actual output/result*
4. *For numeric thresholds (SC-xxx): record the actual measured value vs the spec target*
5. *Only then write the status and evidence*

*DO NOT mark with a checkmark without having just verified the code and test output. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | `arpeggiator_core.h:40-45` -- `enum ArpStepFlags : uint8_t` with kStepActive=0x01, kStepTie=0x02, kStepSlide=0x04, kStepAccent=0x08. Tests: ArpStepFlags_BitValues, ArpStepFlags_Combinable, ArpStepFlags_UnderlyingType |
| FR-002 | MET | `arpeggiator_core.h:138` -- constructor sets modifierLane step 0 to kStepActive. Test: ModifierLane_DefaultIsActive |
| FR-003 | MET | `arpeggiator_core.h:78` -- `bool legato{false}` in ArpEvent. Tests: ArpEvent_LegatoDefaultsFalse, ArpEvent_LegatoField_SetAndRead |
| FR-004 | MET | `arpeggiator_core.h:78` -- defaulted member initializer. Test: ArpEvent_BackwardCompat_AggregateInit |
| FR-005 | MET | `arpeggiator_core.h:1103` -- `ArpLane<uint8_t> modifierLane_`. Test: ModifierLane_DefaultIsActive |
| FR-006 | MET | `arpeggiator_core.h:792-795` -- modifier lane advances with velocity/gate/pitch. Test: Rest_AllLanesAdvance |
| FR-007 | MET | `arpeggiator_core.h:138` -- default length=1, step[0]=kStepActive. Test: ModifierLane_DefaultIsActive |
| FR-008 | MET | `arpeggiator_core.h:1081-1086` -- resetLanes() resets modifierLane_ and tieActive_. Test: ModifierLane_ResetIncludesModifier |
| FR-009 | MET | `arpeggiator_core.h:801-817` -- Rest: no noteOn, noteOff for sounding notes. Tests: Rest_NoNoteOn, Rest_PreviousNoteOff |
| FR-010 | MET | `arpeggiator_core.h:792-795, 998` -- all lanes advance in normal and defensive paths. Tests: Rest_AllLanesAdvance, Rest_DefensiveBranch_LanesAdvance |
| FR-011 | MET | `arpeggiator_core.h:821-831` -- Tie: cancel noteOffs, set tieActive_=true. Tests: Tie_SuppressesNoteOffAndNoteOn, Tie_ChordMode_SustainsAllNotes |
| FR-012 | MET | `arpeggiator_core.h:823-824` -- cancelPendingNoteOffsForCurrentNotes() during tie. Test: Tie_OverridesGateLane |
| FR-013 | MET | `arpeggiator_core.h:832-836` -- Tie with no preceding note behaves as rest. Tests: Tie_NoPrecedingNote_BehavesAsRest, Tie_AfterRest_BehavesAsRest |
| FR-014 | MET | `arpeggiator_core.h:842-849` -- Active step ends tie chain. Test: Tie_Chain_SustainsAcross3Steps |
| FR-015 | MET | `arpeggiator_core.h:897-939` -- Slide: cancel noteOffs, emit legato=true. Tests: Slide_EmitsLegatoNoteOn, Slide_SuppressesPreviousNoteOff, Slide_ChordMode_AllNotesLegato |
| FR-016 | MET | `arpeggiator_core.h:853` -- Slide with no preceding note falls back to normal. Tests: Slide_NoPrecedingNote_FallsBackToNormal, Slide_AfterRest_FallsBackToNormal |
| FR-017 | MET | `arpeggiator_core.h:874-880` -- pitch offset applied to slide notes. Test: Slide_PitchLaneAdvances |
| FR-018 | MET | `arpeggiator_core.h:1110, 349-350` -- slideTimeMs_ stored and settable. `processor.cpp:1317` routes to engine. Test: ModifierLane_SetSlideTime |
| FR-019 | MET | `arpeggiator_core.h:866-872` -- accent boosts velocity. Test: Accent_BoostsVelocity |
| FR-020 | MET | `arpeggiator_core.h:856-872` -- velocity lane scaling first, then accent, clamped [1,127]. Tests: Accent_ClampsToMax127, Accent_AppliedAfterVelocityLaneScaling |
| FR-021 | MET | `arpeggiator_core.h:1109` -- default accentVelocity_=30, settable via setAccentVelocity(). Test: ModifierLane_SetAccentVelocity |
| FR-022 | MET | `arpeggiator_core.h:801-872` -- accent only on noteOn paths. Tests: Accent_WithTie_NoEffect, Accent_WithRest_NoEffect, Accent_WithSlide_BothApply |
| FR-023 | MET | `arpeggiator_core.h:801-872` -- priority Rest > Tie > Slide > Accent. Tests: CombinedModifiers_RestWithAnyFlag_AlwaysSilent, CombinedModifiers_RestWithAllFlags_AlwaysSilent |
| FR-024 | MET | `arpeggiator_core.h:334-338` -- mutable and const modifierLane() accessors. Test: ModifierLane_AccessorsExist |
| FR-025 | MET | `arpeggiator_core.h:343-350` -- setAccentVelocity() and setSlideTime() with clamping. Tests: ModifierLane_SetAccentVelocity, ModifierLane_SetSlideTime |
| FR-026 | MET | `plugin_ids.h:926-963` -- 35 IDs defined. `arpeggiator_params.h:372-402` -- registered. Test: ModifierParams_SC010_AllRegistered |
| FR-027 | MET | `arpeggiator_params.h:376-402` -- step params kCanAutomate|kIsHidden, config params kCanAutomate only. Test: ModifierParams_SC010_AllRegistered |
| FR-028 | MET | `arpeggiator_params.h:65-70, 186-233` -- atomic storage and handleArpParamChange dispatch. Tests: ArpModifierLaneLength_Denormalize, ArpModifierLaneStep_Denormalize, ArpAccentVelocity_Denormalize, ArpSlideTime_Denormalize |
| FR-029 | MET | `arpeggiator_params.h:620-627, 712-730` -- saveArpParams/loadArpParams. Test: ModifierLane_SaveLoad_RoundTrip |
| FR-030 | MET | `arpeggiator_params.h:712` -- EOF at first modifier returns true; subsequent EOF returns false. Tests: ModifierLane_BackwardCompat_Phase4Stream, ModifierLane_PartialStream_LengthOnly_ReturnsFalse |
| FR-031 | MET | `processor.cpp:1304-1312` -- expand-write-shrink pattern. Test: ModifierParams_FlowToCore |
| FR-032 | MET | `processor.cpp:228` -- engine_.noteOn(evt.note, evt.velocity, evt.legato). Tests: ArpIntegration_SlidePassesLegatoToEngine, ArpIntegration_NormalStepPassesLegatoFalse |
| FR-033 | MET | `ruinae_engine.h:224-236, 1439-1458` -- noteOn with legato param, dispatchPolyLegatoNoteOn(), MonoHandler legato routing |
| FR-034 | MET | `ruinae_engine.h:1286-1292` -- setPortamentoTime on MonoHandler and all voices. `ruinae_voice.h:303-305` -- per-voice portamento. `processor.cpp:1317` -- unconditional forwarding |
| SC-001 | MET | 27+ tests across 4 modifier types (Rest: 5, Tie: 7, Slide: 7, Accent: 8), exceeding 8 minimum |
| SC-002 | MET | BitIdentical_DefaultModifierLane: 3150 steps, 0 mismatches at 120/140/180 BPM |
| SC-003 | MET | Slide_SC003_LegatoFieldTrue: event.legato == true verified directly |
| SC-004 | MET | 8 accent/velocity combinations tested including overflow-to-127, low-velocity+accent, accent=0 |
| SC-005 | MET | Tie_Chain_SustainsAcross3Steps: 3-step chain, zero events in tied region |
| SC-006 | MET | Polymetric_ModifierLength3_VelocityLength5: period=15. ModifierLane_CyclesIndependently: period=420 |
| SC-007 | MET | ModifierLane_SaveLoad_RoundTrip: all 35 values round-trip exactly |
| SC-008 | MET | ModifierLane_BackwardCompat_Phase4Stream: Phase 4 stream loads with correct defaults |
| SC-009 | MET | fireStep() code inspection: zero heap allocations. All modifier evaluation uses bitwise ops on stack |
| SC-010 | MET | ModifierParams_SC010_AllRegistered: 35 params, correct flags. FormatArpParam tests: all pass |

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

**Build**: PASS -- Zero errors, zero warnings

**Tests**: PASS (6308/6308)
- dsp_tests.exe: 5,878 test cases, 21,988,608 assertions -- ALL PASSED
- ruinae_tests.exe: 430 test cases, 7,731 assertions -- ALL PASSED

**Pluginval**: PASS -- Strictness level 5, all test sections completed

All 34 FRs and 10 SCs MET. No gaps, no deferred requirements, no relaxed thresholds.
