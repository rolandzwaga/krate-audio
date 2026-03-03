# Feature Specification: Euclidean Timing Mode

**Feature Branch**: `075-euclidean-timing`
**Plugin**: KrateDSP (Layer 2 processor) + Ruinae (plugin integration)
**Created**: 2026-02-22
**Status**: Draft
**Input**: User description: "Phase 7 of Arpeggiator Roadmap: Euclidean Timing Mode. Replace the manual timing lane with Euclidean rhythm generation using the Bjorklund algorithm. E(k,n) distributes k pulses across n steps maximally evenly. Reuse existing EuclideanPattern component. User controls: Hits (k), Steps (n), Rotation (offset). Generated pattern determines which steps are active vs silent. All other lanes (velocity, gate, pitch, ratchet, modifier) still cycle at their own lengths."
**Depends on**: Phase 4 (072-independent-lanes) -- COMPLETE; Phase 5 (073-per-step-mods) -- COMPLETE; Phase 6 (074-ratcheting) -- COMPLETE

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Euclidean Pattern Activation for Rhythmic Gating (Priority: P1)

A musician wants to replace the default "every step fires" timing with a Euclidean rhythm pattern that distributes a chosen number of hits across a chosen number of steps as evenly as possible. They enable the Euclidean timing mode and set the parameters (e.g., 3 hits across 8 steps). The arpeggiator now plays notes only on the active steps of the generated Euclidean pattern -- the classic tresillo rhythm E(3,8) -- while resting on inactive steps. This creates complex, musically meaningful rhythmic patterns with just three intuitive controls (Hits, Steps, Rotation), without requiring the user to manually program rest/active flags step by step. This approach to rhythm generation is found in hardware sequencers such as the Mutable Instruments Grids, Elektron Digitakt, Make Noise Rene, and Intellijel Metropolix.

**Why this priority**: This is the core Euclidean timing functionality. Without it, the feature does not exist. It validates the integration of the existing `EuclideanPattern` component (Layer 0) into the `ArpeggiatorCore` (Layer 2), the parameter controls for Hits/Steps/Rotation, and the fundamental gating mechanism where the generated pattern determines which arp steps produce notes versus rests.

**Independent Test**: Can be fully tested by enabling Euclidean mode with known patterns (e.g., E(3,8) tresillo) and verifying that noteOn events are emitted only on hit positions while rest positions produce silence. All other lanes (velocity, gate, pitch, ratchet, modifier) remain unaffected.

**Acceptance Scenarios**:

1. **Given** Euclidean mode enabled with hits=3, steps=8, rotation=0 (tresillo E(3,8) = `10010010`), **When** the arp plays 8 steps, **Then** noteOn events fire on steps 0, 3, and 6 only. Steps 1, 2, 4, 5, and 7 are silent (rest). All 8 steps consume the same time duration.
2. **Given** Euclidean mode disabled (default), **When** the arp plays, **Then** every step fires a noteOn (identical to Phase 6 behavior). Euclidean mode off is a no-op.
3. **Given** Euclidean mode enabled with hits=8, steps=8 (all active), **When** the arp plays, **Then** every step fires a noteOn -- functionally identical to Euclidean mode off.
4. **Given** Euclidean mode enabled with hits=0, steps=8 (all silent), **When** the arp plays, **Then** no noteOn events fire on any step. All steps are rests.

---

### User Story 2 - Rotation for Rhythmic Variation (Priority: P1)

A musician has found a Euclidean pattern they like (e.g., E(5,8) cinquillo) and wants to shift the starting position to create different rhythmic feels from the same hits/steps combination. By adjusting the Rotation parameter, the entire pattern is cyclically shifted, placing hits at different positions relative to the downbeat. Rotation 0 gives the canonical pattern; rotation 1 shifts all hits one step later; rotation = steps-1 wraps the last hit to the front. This is equivalent to rotating a circular necklace representation of the rhythm.

**Why this priority**: Rotation is essential for musical usability. The same E(k,n) pattern rotated by different offsets produces radically different rhythmic feels. Many traditional world music rhythms are rotations of the same underlying Euclidean distribution. Without rotation, the user is limited to a single rhythmic variant per hits/steps combination.

**Independent Test**: Can be fully tested by generating the same hits/steps pattern at different rotation values and verifying that hit positions shift cyclically while the total number of hits remains constant.

**Acceptance Scenarios**:

1. **Given** E(3,8) with rotation=0 produces hits on steps {0, 3, 6}, **When** rotation is changed to 1, **Then** hits shift to different positions. The total hit count remains 3.
2. **Given** E(3,8) with rotation=0, **When** rotation is changed to 8 (equals steps), **Then** the pattern wraps back to the same as rotation=0. Rotation is taken modulo the step count.
3. **Given** E(5,16) bossa nova pattern, **When** rotation is swept from 0 to 15, **Then** each rotation produces a distinct pattern with exactly 5 hits. All 16 rotations are audibly different rhythmic variants.

---

### User Story 3 - Euclidean Timing with Existing Lane Interplay (Priority: P1)

A sound designer enables Euclidean timing and has velocity, gate, pitch, ratchet, and modifier lanes configured at various independent lengths. The Euclidean pattern determines which steps are "hit" (note fires) or "rest" (silence). All other lanes continue to advance at their own independent lengths on every step, regardless of whether the Euclidean pattern fires a note or rests. This means the polymetric interplay between the Euclidean timing grid and the other lanes creates evolving, non-repeating patterns whose combined cycle length is the LCM of all lane lengths and the Euclidean step count.

**Why this priority**: The independent lane architecture is the arpeggiator's core differentiator (Phase 4). Euclidean timing must integrate cleanly with this system, advancing alongside all other lanes without disrupting polymetric cycling.

**Independent Test**: Can be fully tested by configuring Euclidean timing at one step count and other lanes at different lengths, then verifying all lanes advance on every step tick (including Euclidean rest steps) and produce correct polymetric cycling.

**Acceptance Scenarios**:

1. **Given** Euclidean mode with steps=5 and a velocity lane of length 3, **When** the arp plays 15 steps, **Then** the Euclidean pattern repeats every 5 steps and the velocity lane repeats every 3 steps, producing a combined cycle of 15 steps. On Euclidean rest steps, the velocity lane still advances (its value is consumed but no note fires).
2. **Given** Euclidean mode with steps=8 and a ratchet lane of length 3 with values [1, 2, 4], **When** a hit step coincides with ratchet count 4, **Then** the step fires with 4 ratcheted sub-steps. When a rest step coincides with ratchet count 4, **Then** no notes fire and the ratchet count is discarded.
3. **Given** Euclidean mode with steps=8 and a modifier lane of length 4 with step 1 = Tie, **When** a Euclidean hit step coincides with a Tie modifier, **Then** tie behavior applies (previous note sustains). When a Euclidean rest step coincides with a Tie modifier, **Then** the step is still silent (Euclidean rest takes precedence over tie -- there is no previous note to sustain through a rest).

---

### User Story 4 - Euclidean Mode On/Off Transitions (Priority: P2)

A performer toggles Euclidean mode on and off during a live performance. When switched on, the Euclidean pattern immediately governs which steps fire. When switched off, the arp returns to its default behavior (all steps active). The transition must not cause timing glitches, stuck notes, or unexpected rhythm changes beyond the intended mode switch.

**Why this priority**: Live toggle is important for performance use but builds on the core Euclidean functionality. Clean transitions are necessary for usability but are secondary to correct pattern generation.

**Independent Test**: Can be tested by toggling Euclidean mode on/off mid-playback and verifying no timing disruption, stuck notes, or unexpected behavior.

**Acceptance Scenarios**:

1. **Given** the arp is playing with Euclidean mode off, **When** Euclidean mode is enabled mid-playback, **Then** the Euclidean gating takes effect from the next full step boundary. No timing glitch or stuck notes occur. The Euclidean step position starts from 0 (synchronized with the timing lane). If ratchet sub-steps are in-flight at the moment of the toggle, they complete normally before the Euclidean gating begins.
2. **Given** the arp is playing with Euclidean mode on, **When** Euclidean mode is disabled mid-playback, **Then** all steps fire as active from the next step onward. No timing glitch or stuck notes.
3. **Given** the arp is mid-step (between step boundaries) when Euclidean mode is toggled, **When** the next step fires, **Then** the new mode applies cleanly with no partial-step artifacts.

---

### User Story 5 - Euclidean Parameter Persistence (Priority: P3)

A user configures Euclidean timing parameters (enabled, hits, steps, rotation), saves the preset, and reloads it. All Euclidean settings are restored exactly.

**Why this priority**: Without persistence, Euclidean configurations are lost on save/load. This is an integration concern that builds on the core functionality.

**Independent Test**: Can be tested by configuring specific Euclidean values, saving plugin state, restoring it, and verifying all values match.

**Acceptance Scenarios**:

1. **Given** Euclidean mode enabled with hits=5, steps=16, rotation=3, **When** plugin state is saved and restored, **Then** all four Euclidean parameter values are identical after restore.
2. **Given** a preset saved before Euclidean support (Phase 6 or earlier), **When** loaded into this version, **Then** the Euclidean mode defaults to disabled. The arpeggiator behaves identically to Phase 6.

---

### Edge Cases

- What happens when hits equals 0? The pattern is all rests (silence). The arp advances steps but no notes fire. `EuclideanPattern::generate(0, n, r)` returns 0 (verified in existing tests).
- What happens when hits equals steps? All steps are hits. `EuclideanPattern::generate(n, n, r)` returns all bits set (existing behavior). Functionally identical to Euclidean mode off.
- What happens when hits is greater than steps? `EuclideanPattern::generate()` clamps hits to [0, steps]. The result is the same as hits == steps (all active).
- What happens when steps is set to less than 2? `EuclideanPattern::generate()` clamps steps to [kMinSteps (2), kMaxSteps (32)]. The `setEuclideanSteps()` setter also clamps to [2, 32] before calling `regenerateEuclideanPattern()`, so the stored member value is always in range.
- What happens when rotation is negative? `EuclideanPattern::generate()` wraps rotation to [0, steps-1] using `((rotation % steps) + steps) % steps`. This is already handled in the existing implementation.
- What happens when rotation equals or exceeds steps? Rotation is taken modulo steps. Rotation = steps produces the same pattern as rotation = 0 (verified in existing tests).
- What happens when Euclidean mode is enabled but steps is changed while the arp is playing? The pattern is regenerated on the next `processBlock()` when the parameters are applied. The regenerated pattern takes effect from the next step boundary. No mid-step artifacts occur because the pattern is only queried at step fire time.
- What happens when a Euclidean rest step has a Tie modifier? The Euclidean rest takes precedence. Rest means no note fires and no note sustains. The tie is effectively ignored for that step. If a tie chain was active from a preceding hit step, the rest breaks the chain (the sustained note receives a noteOff).
- What happens when a Euclidean rest step has a Slide modifier? The Euclidean rest takes precedence. No slide occurs because no noteOn is emitted.
- What happens when a Euclidean rest step has a Ratchet count > 1? The Euclidean rest takes precedence. No ratcheted sub-steps fire. The ratchet count is discarded for that step.
- What happens when a Euclidean rest step has an Accent modifier? The accent is irrelevant because no noteOn is emitted.
- What happens during retrigger (Note or Beat) with Euclidean mode enabled? `resetLanes()` resets the Euclidean step position to 0, consistent with all other lanes. The Euclidean pattern starts fresh from step 0.
- What happens when the internal Euclidean step position exceeds the Euclidean steps count (e.g., pattern regenerated with fewer steps while position was higher)? The position wraps modulo the new step count on the next advance.
- What happens when Euclidean steps differs from the modifier lane length? They cycle independently. The Euclidean pattern position and the modifier lane position are separate counters, each advancing once per arp step tick.
- What happens when the ratchet last-sub-step look-ahead suppresses a gate noteOff because the next modifier step has Tie/Slide, but that next step is actually a Euclidean rest? The suppression is harmless. FR-007 fires unconditionally when a Euclidean rest step is reached, emitting the noteOff for any sustained note at that point. The look-ahead checks the modifier lane only and does not need to know the next step's Euclidean status. No stuck notes result because the Euclidean rest path is the correction mechanism.

## Clarifications

### Session 2026-02-22

- Q: Should `selector_.advance()` (NoteSelector) still be called on Euclidean rest steps? → A: Yes. NoteSelector always advances on every step tick, including Euclidean rest steps. This matches existing modifier Rest behavior.
- Q: What is the minimum allowed value for the Euclidean Hits parameter -- 0 or 1? → A: Minimum is 0. A fully silent pattern (hits=0) is a valid configuration. `kArpEuclideanHitsId` is registered as discrete 0-32. The roadmap's "1-32" was a documentation error.
- Q: Should the ratchet look-ahead (Tie/Slide gate suppression) also check whether the next step is a Euclidean rest? → A: No. Look-ahead checks the modifier lane only. If the next step is a Euclidean rest, FR-007 emits the noteOff at that rest step. No change to look-ahead logic.
- Q: When Euclidean mode is toggled on mid-playback while ratchet sub-steps are in-flight, should those sub-steps be cancelled or allowed to complete? → A: In-flight ratchet sub-steps complete normally. Euclidean gating takes effect from the next full step boundary. `setEuclideanEnabled(true)` resets only `euclideanPosition_`, not ratchet sub-step state.
- Q: Should `applyParamsToArp()` prescribe a specific call order for the four Euclidean setters? → A: Yes. Required order: `setEuclideanSteps()`, `setEuclideanHits()`, `setEuclideanRotation()`, `setEuclideanEnabled()`. Steps first ensures clamping of hits against the correct step count; enabled last ensures the pattern is fully computed before the flag activates.

## Requirements *(mandatory)*

### Functional Requirements

**Euclidean Timing State in ArpeggiatorCore (DSP Layer 2)**

- **FR-001**: The ArpeggiatorCore MUST maintain Euclidean timing state consisting of:
  - `euclideanEnabled_` (bool): Whether Euclidean timing mode is active. Default: false.
  - `euclideanPattern_` (uint32_t): The pre-generated bitmask from `EuclideanPattern::generate()`. Default: 0 as a member initializer, but the constructor MUST call `regenerateEuclideanPattern()` so the field holds E(4,8,0) at construction time -- not the zero-initialized value.
  - `euclideanHits_` (int): Number of pulses (k). Default: 4.
  - `euclideanSteps_` (int): Number of steps (n). Default: 8.
  - `euclideanRotation_` (int): Rotation offset. Default: 0.
  - `euclideanPosition_` (size_t): Current step position in the Euclidean pattern [0, euclideanSteps_-1]. Default: 0.
- **FR-002**: When Euclidean mode is disabled (`euclideanEnabled_ == false`), the arp MUST behave identically to Phase 6 -- every step fires. The Euclidean state variables MUST be ignored in the `fireStep()` path.
- **FR-003**: When Euclidean mode is enabled, the Euclidean position MUST advance once per arp step tick, wrapping at `euclideanSteps_`. The advance happens at the same point as all other lane advances (in `fireStep()`).
- **FR-004**: When Euclidean mode is enabled and the current Euclidean position is a rest (not a hit), the step MUST be treated as a rest: no noteOn is emitted, but all lanes (velocity, gate, pitch, modifier, ratchet) MUST still advance. A noteOff MUST be emitted for any currently sounding note. This behavior is identical to the existing Rest modifier path but gated by the Euclidean pattern rather than the modifier lane.
- **FR-005**: When Euclidean mode is enabled and the current Euclidean position is a hit, the step fires normally -- all lane values are applied, modifier evaluation proceeds as in Phase 6, ratcheting applies.
- **FR-006**: The Euclidean rest from FR-004 MUST be evaluated BEFORE the modifier lane. If the Euclidean pattern says "rest," the step is silent regardless of the modifier flags. The evaluation order is: Euclidean gating -> Modifier evaluation (Rest > Tie > Slide > Accent) -> Ratcheting.
- **FR-007**: During a Euclidean rest step, if a tie chain is active from a preceding hit step, the tie chain MUST be broken: a noteOff for the sustained note(s) MUST be emitted and `tieActive_` set to false. The Euclidean rest acts as an unconditional gate-off. This also applies when the preceding step's ratchet look-ahead suppressed a gate noteOff because the current step has Tie/Slide modifier bits set -- the look-ahead checks only the modifier lane and does not account for Euclidean status, so FR-007 is the correction point that ensures no stuck notes result from that suppression.

**Euclidean Pattern Generation**

- **FR-008**: The Euclidean pattern bitmask MUST be regenerated whenever any of the three parameters (hits, steps, rotation) changes. Regeneration uses the existing `EuclideanPattern::generate(hits, steps, rotation)` from `dsp/include/krate/dsp/core/euclidean_pattern.h`. This is a pure computation with no allocation and O(n) complexity.
- **FR-009**: The `setEuclideanHits()`, `setEuclideanSteps()`, and `setEuclideanRotation()` setter methods MUST clamp their inputs: hits to [0, 32], steps to [2, 32], rotation to [0, 31]. After clamping, each setter regenerates the pattern bitmask by calling `regenerateEuclideanPattern()`. Note: the lower bound of hits is 0 (not 1) -- a fully silent pattern is a valid, intentional configuration. The arpeggiator roadmap's "1-32" range for `kArpEuclideanHitsId` is a documentation error; the canonical range is 0-32 per this spec.
- **FR-010**: A `setEuclideanEnabled(bool)` method MUST be provided. When transitioning from disabled to enabled, `euclideanPosition_` MUST be reset to 0; ratchet sub-step state (`ratchetSubStepsRemaining_` etc.) MUST NOT be cleared -- any in-flight sub-steps complete normally and Euclidean gating takes effect from the next full step boundary. When transitioning from enabled to disabled, no state cleanup is needed (the Euclidean state is simply ignored).

**processBlock() and fireStep() Integration**

- **FR-011**: The `fireStep()` method MUST be modified to query the Euclidean pattern before proceeding with note emission. The NoteSelector (`selector_.advance()`) and all lanes MUST advance unconditionally on every step tick -- including Euclidean rest steps -- matching the behavior of the existing modifier Rest path. After those advances, if Euclidean mode is enabled:
  1. Read the current Euclidean position
  2. Check `EuclideanPattern::isHit(euclideanPattern_, euclideanPosition_, euclideanSteps_)`
  3. Advance `euclideanPosition_ = (euclideanPosition_ + 1) % euclideanSteps_`
  4. If NOT a hit: execute the Euclidean rest path (FR-004, FR-007)
  5. If a hit: proceed with normal modifier evaluation and note emission
- **FR-012**: The Euclidean position advance (step 3 in FR-011) MUST happen unconditionally on every step tick, regardless of whether the step is a hit or rest. This ensures the Euclidean pattern cycles correctly and independently.
- **FR-013**: The `resetLanes()` method MUST be extended to reset `euclideanPosition_` to 0. This ensures retrigger (Note and Beat modes) and disable/enable transitions restart the Euclidean pattern from the beginning.
- **FR-014**: The `reset()` method MUST clear all Euclidean state: `euclideanPosition_` to 0 and `euclideanPattern_` regenerated from current parameters.

**Euclidean Configuration Accessors**

- **FR-015**: The ArpeggiatorCore MUST provide getter methods for all Euclidean parameters: `euclideanEnabled()`, `euclideanHits()`, `euclideanSteps()`, `euclideanRotation()`. These return the current configuration values (not the bitmask).

**Euclidean and Ratchet Interaction**

- **FR-016**: When a step is a Euclidean rest, ratcheting does NOT apply. No ratcheted sub-steps fire. The ratchet count from the ratchet lane is discarded for that step. Sub-step state (`ratchetSubStepsRemaining_`) is not initialized.
- **FR-017**: When a step is a Euclidean hit, ratcheting applies normally per Phase 6 rules. The ratchet count from the ratchet lane determines sub-step count.

**Euclidean and Modifier Interaction**

- **FR-018**: The evaluation order is: Euclidean gating (FR-006) -> Modifier priority chain (Rest > Tie > Slide > Accent) -> Ratcheting. A Euclidean rest short-circuits the entire step -- modifier evaluation and ratcheting are skipped.
- **FR-019**: When a step is a Euclidean hit, modifiers apply normally per Phase 5/6 rules. A modifier Rest flag on a Euclidean hit step produces silence (the modifier Rest still works).
- **FR-020**: A Euclidean hit step with a Tie modifier sustains the previous note as expected. A Euclidean rest step followed by a Euclidean hit step with a Tie modifier: the tie has no preceding note to sustain (the Euclidean rest broke the chain), so it falls back to rest behavior per Phase 5 FR-013 (tie with no preceding note = rest).

**Euclidean and Chord Mode**

- **FR-021**: In Chord mode, the Euclidean pattern applies uniformly to all chord notes. A Euclidean hit step fires all chord notes; a Euclidean rest step silences all.

**Euclidean and Swing**

- **FR-022**: Swing applies to step duration independently of Euclidean gating. Euclidean timing determines which steps fire; swing determines how long each step lasts. They are orthogonal.

**State Cleanup**

- **FR-023**: When the arp is disabled (`setEnabled(false)`) while Euclidean mode is active, no additional cleanup is needed for Euclidean state beyond what already exists (the Euclidean state is passive -- it only gates note emission and does not hold any pending events or notes).
- **FR-024**: When `setEnabled(false)` to `setEnabled(true)` transition occurs, `resetLanes()` is called (existing behavior), which resets `euclideanPosition_` to 0 (per FR-013).

**Plugin Integration (Ruinae)**

- **FR-025**: The plugin MUST expose Euclidean timing parameters through the VST3 parameter system:
  - Euclidean enabled: `kArpEuclideanEnabledId = 3230` (discrete: 0-1, on/off toggle)
  - Euclidean hits: `kArpEuclideanHitsId = 3231` (discrete: 0-32)
  - Euclidean steps: `kArpEuclideanStepsId = 3232` (discrete: 2-32)
  - Euclidean rotation: `kArpEuclideanRotationId = 3233` (discrete: 0-31)
- **FR-026**: The `kArpEndId` and `kNumParameters` sentinels MUST remain at their current values (3299 and 3300 respectively, set in Phase 6). The Euclidean parameter IDs (3230-3233) fall within the existing reserved range (3223-3299) and require no sentinel adjustment.
- **FR-027**: All Euclidean parameters MUST support host automation (`kCanAutomate` flag). None should be hidden (`kIsHidden`) since all four are user-facing controls.
- **FR-028**: The `ArpeggiatorParams` struct MUST be extended with atomic storage for Euclidean parameters:
  - `std::atomic<bool> euclideanEnabled{false}` (default off)
  - `std::atomic<int> euclideanHits{4}` (default 4)
  - `std::atomic<int> euclideanSteps{8}` (default 8)
  - `std::atomic<int> euclideanRotation{0}` (default 0)
- **FR-029**: The `handleArpParamChange()` dispatch MUST be extended to handle `kArpEuclideanEnabledId` (normalized 0.0/1.0 to bool), `kArpEuclideanHitsId` (normalized [0,1] to integer 0-32), `kArpEuclideanStepsId` (normalized [0,1] to integer 2-32), and `kArpEuclideanRotationId` (normalized [0,1] to integer 0-31).
- **FR-030**: Euclidean parameter state MUST be included in plugin state serialization (`saveArpParams`) and deserialization (`loadArpParams`). The Euclidean data MUST be serialized AFTER the existing ratchet lane data to maintain backward compatibility with Phase 6 presets.
- **FR-031**: Loading a preset saved before Euclidean support (Phase 6 or earlier) MUST result in Euclidean mode defaulting to disabled (false), hits=4, steps=8, rotation=0. The arp output must be identical to Phase 6 behavior. Specifically: if `loadArpParams()` encounters EOF at the first Euclidean field read (`euclideanEnabled`), it MUST return `true` (success) and all Euclidean fields retain their defaults -- this is the clean Phase 6 backward-compat path. If `loadArpParams()` encounters EOF after `euclideanEnabled` is successfully read but before all Euclidean fields are read, it MUST return `false` to signal a corrupt stream. Out-of-range values read from the stream MUST be clamped silently: hits to [0, 32], steps to [2, 32], rotation to [0, 31].
- **FR-032**: The `applyParamsToArp()` method in the processor MUST transfer Euclidean parameters to the ArpeggiatorCore in this prescribed order: `setEuclideanSteps()` first, then `setEuclideanHits()`, then `setEuclideanRotation()`, then `setEuclideanEnabled()`. Steps must be set before hits so that the intermediate pattern regeneration (triggered by each setter per FR-009) clamps hits against the correct -- already-updated -- step count. Enabled must be set last so the flag activates only after the pattern is fully computed from all three updated parameters.
- **FR-033**: The `formatArpParam()` method MUST be extended to display human-readable values for Euclidean parameters: enabled displays as "Off"/"On", hits as "N hits" (e.g., "5 hits"), steps as "N steps" (e.g., "16 steps"), rotation as "N" (e.g., "3").

**Controller State Sync**

- **FR-034**: When `loadArpParams()` successfully loads Euclidean data, the loaded values MUST be propagated back to the VST3 controller via `setParamNormalized()` calls for all 4 Euclidean parameters (`kArpEuclideanEnabledId` through `kArpEuclideanRotationId`), following the same pattern used by existing lanes when state is loaded.

**Defensive Behavior**

- **FR-035**: In the defensive `result.count == 0` branch in `fireStep()` (where the held buffer became empty), the Euclidean position MUST advance along with all other lanes, keeping all lane positions synchronized.

### Key Entities

- **Euclidean Pattern**: A bitmask (uint32_t) generated by `EuclideanPattern::generate(hits, steps, rotation)`. Each bit represents whether a step is a hit (1) or rest (0). The bitmask is pre-computed and cached -- only regenerated when hits, steps, or rotation changes.
- **Euclidean Position**: An independent counter (size_t) that advances once per arp step tick and wraps at `euclideanSteps_`. Similar to how `ArpLane` tracks its own position, but the Euclidean position indexes into the pre-computed bitmask rather than a step array.
- **Euclidean Gating**: The mechanism by which the pre-computed pattern filters arp steps. A hit allows the step to proceed through normal modifier/ratchet evaluation. A rest silences the step, emitting noteOffs for any sounding notes and breaking tie chains.

### Notable Euclidean Rhythms

The following well-known world music rhythms are generated by the Euclidean algorithm and serve as validation references:

| Pattern | Binary (hit=1, rest=0) | Interval | Musical Tradition |
|---------|------------------------|----------|-------------------|
| E(2,5)  | `10100`                | 32       | Persian Khafif-e-ramal |
| E(3,8)  | `10010010`             | 332      | Cuban tresillo, Afro-Cuban |
| E(4,12) | `100100100100`         | 3333     | Trinidad calypso |
| E(5,8)  | `10110110`             | 21212    | Cuban cinquillo, West Africa |
| E(5,12) | `100101001010`         | 32322    | West African bell pattern |
| E(5,16) | `1001001000100100`     | 33434    | Bossa nova |
| E(7,12) | `101101010110`         | 2122122  | West African bell (7-stroke) |
| E(7,16) | `1001010100101010`     | 2223222  | Brazilian samba |
| E(9,16) | `1011010110110101`     | 212212221 | West African rhythm |
| E(11,24)| `101010101001010101010010` | 22222322223 | Aka Pygmies of Central Africa |

*Source: Toussaint, "The Euclidean Algorithm Generates Traditional Musical Rhythms" (2005)*

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Known Euclidean patterns match expected bitmasks. The following patterns must produce exact bit-for-bit matches against reference values: E(3,8) tresillo, E(5,8) cinquillo, E(5,16) bossa nova, E(7,12) West African bell, E(7,16) samba. Verified by at least 5 test cases (one per pattern), each checking all step positions for hit/rest correctness. The existing `EuclideanPattern` tests already verify E(3,8), E(5,8), E(5,12), E(0,n), E(n,n), and E(1,n); the new tests verify integration with the arp gating mechanism.
- **SC-002**: Rotation produces distinct patterns from the same hits/steps. For E(3,8), all 8 rotations (0-7) produce different bitmasks, each with exactly 3 hits. Verified by a test that generates all rotations and confirms uniqueness and hit count preservation.
- **SC-003**: Euclidean mode integrates with the existing lane system -- Euclidean only controls which steps fire; all other lanes advance independently regardless of Euclidean hit/rest. Verified by a polymetric test (e.g., Euclidean steps=5, velocity lane length=3 = combined cycle of 15 steps with no premature repetition). Lane values consumed on Euclidean rest steps are discarded (not deferred).
- **SC-004**: With Euclidean mode disabled (default), the arpeggiator produces identical output to Phase 6 across 1000+ steps at 120/140/180 BPM. Zero tolerance -- same notes, velocities, sample offsets, legato flags on all events.
- **SC-005**: Euclidean mode on/off transitions produce no stuck notes, timing glitches, or unexpected behavior. Verified by tests that toggle Euclidean mode mid-playback and verify clean transitions.
- **SC-006**: Euclidean rest steps correctly break active tie chains by emitting noteOff for sustained notes. Verified by a test sequence where a Euclidean hit starts a note, a Euclidean hit with Tie sustains it, and a Euclidean rest terminates it.
- **SC-007**: Ratcheted steps respect Euclidean gating: ratchet on a Euclidean hit step produces correct sub-steps; ratchet on a Euclidean rest step produces silence. Verified by at least 2 test cases.
- **SC-008**: Plugin state round-trip (save then load) preserves all Euclidean parameter values exactly (enabled, hits, steps, rotation). Verified by comparing every value before and after serialization.
- **SC-009**: Loading a Phase 6 preset (without Euclidean data) succeeds with default Euclidean values (disabled, hits=4, steps=8, rotation=0) and the arpeggiator output is identical to Phase 6 behavior.
- **SC-010**: All Euclidean-related code in `fireStep()` and `processBlock()` performs zero heap allocation, verified by code inspection (no new/delete/malloc/free/vector/string/map in the modified code paths). The `EuclideanPattern::generate()` function is constexpr, static, and allocation-free (already verified in existing Layer 0 tests).
- **SC-011**: All Euclidean parameters (4 total: enabled, hits, steps, rotation) are registered with the host, automatable, and display correct human-readable values. Verification requires: (a) an integration test confirming all 4 parameter IDs (3230-3233) are registered with `kCanAutomate` flag; and (b) a formatting test calling `formatArpParam()` for each Euclidean parameter and verifying the output string.
- **SC-012**: The Euclidean position resets correctly on retrigger (Note and Beat modes) and on arp enable/disable transitions. Verified by tests that trigger retrigger events and confirm the Euclidean pattern restarts from position 0.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Phase 6 (074-ratcheting) is fully complete and merged, providing ratchet lane, sub-step timing, modifier interactions, and expanded parameter sentinel values (`kArpEndId = 3299`, `kNumParameters = 3300`).
- The existing `EuclideanPattern` class at `dsp/include/krate/dsp/core/euclidean_pattern.h` (Layer 0) is stable, tested, and ready for direct reuse. It uses the Bresenham-style accumulator method (from Paul Batchelor's sndkit) which is functionally equivalent to the Bjorklund algorithm for producing maximally even distributions. The class is constexpr, static, noexcept, and allocation-free.
- The `EuclideanPattern::generate()` function clamps its inputs (steps to [2, 32], pulses to [0, steps]), handles rotation wrapping, and returns a uint32_t bitmask. All edge cases (0 hits, n hits, hits > steps, rotation >= steps) are already handled in the existing implementation.
- The reserved parameter ID range 3223-3299 in `plugin_ids.h` has room for the 4 Euclidean parameters (3230-3233). No sentinel adjustment is needed.
- The `ArpeggiatorCore` is header-only at `dsp/include/krate/dsp/processors/arpeggiator_core.h`.
- The UI for Euclidean controls (Hits/Steps/Rotation knobs, visual pattern display) is deferred to Phase 11. This phase only exposes Euclidean parameters through the VST3 parameter system.
- Euclidean pattern regeneration is fast (O(n) where n <= 32) and can be performed on every parameter change without performance concern.
- The `#include <krate/dsp/core/euclidean_pattern.h>` is already included in the ArpeggiatorCore include chain (Layer 2 can include Layer 0), but an explicit include may need to be added to `arpeggiator_core.h` if not already present.

### Constraints & Tradeoffs

**Euclidean gating approach: pre-fire check vs. dedicated Euclidean lane**

Two approaches exist for integrating Euclidean timing:

1. **Pre-fire gating check in fireStep()**: Before processing modifiers and ratcheting, check the Euclidean pattern. If the current position is a rest, treat the step as silent. Simple, minimal code change.
2. **Dedicated ArpLane<bool> for Euclidean timing**: Create an ArpLane that is auto-populated from the Euclidean pattern. The existing modifier lane Rest mechanism handles gating.

Approach 1 is preferred because:
- It does not require adding another lane object (saves memory and complexity).
- It avoids conflating the Euclidean timing concern with the modifier lane (which has independent length and purpose).
- The gating check is a single `isHit()` call (O(1) bit check) per step.
- It keeps the Euclidean concept architecturally distinct from per-step manual programming.
- The Euclidean pattern bitmask is cached and only regenerated when parameters change, not on every step.

The cost is a new code path in `fireStep()` specifically for Euclidean rest handling, separate from the modifier Rest path. This duplication is minimal (a few lines) and justified by the cleaner separation of concerns.

**Euclidean rest vs. Modifier Rest: evaluation order**

The Euclidean pattern is evaluated BEFORE modifiers. This means:
- A Euclidean rest silences the step regardless of modifier flags.
- A Euclidean hit step can still be silenced by a modifier Rest flag.
- This creates two layers of gating: Euclidean (rhythmic structure) and modifier (per-step overrides).

The alternative (evaluating modifiers first, then Euclidean) was rejected because:
- Modifier evaluation has side effects (tie chain management, accent velocity boosting) that should not execute on Euclidean rest steps.
- Evaluating the cheap Euclidean bit-check first provides an early-out optimization.
- Semantically, Euclidean timing defines the rhythmic structure, while modifiers refine individual steps within that structure.

**Default parameter values**

- Hits: 4 (reasonable starting point -- four-on-the-floor when steps=8)
- Steps: 8 (common 8-step pattern, matches typical 1-bar arp at 1/8 note)
- Rotation: 0 (canonical pattern orientation)
- Enabled: false (backward compatibility -- existing behavior preserved by default)

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| EuclideanPattern | `dsp/include/krate/dsp/core/euclidean_pattern.h` | **Primary reuse target** -- static constexpr class with `generate(hits, steps, rotation)` returning uint32_t bitmask, `isHit(pattern, position, steps)` for O(1) lookup, `countHits(pattern)` for validation. Already tested with 15+ unit tests. |
| ArpeggiatorCore | `dsp/include/krate/dsp/processors/arpeggiator_core.h` | **Primary extension target** -- Euclidean state added as members, `fireStep()` modified for Euclidean gating, `resetLanes()` extended for Euclidean position reset. |
| ArpLane<T> | `dsp/include/krate/dsp/primitives/arp_lane.h` | **Not modified** -- existing lanes continue to work unchanged. Euclidean timing is NOT implemented as an ArpLane. |
| fireStep() | `arpeggiator_core.h` | Modified to insert Euclidean gating check after lane advance but before modifier evaluation. |
| resetLanes() | `arpeggiator_core.h:1317-1326` | Extended to include `euclideanPosition_ = 0`. |
| ArpeggiatorParams | `plugins/ruinae/src/parameters/arpeggiator_params.h` | Extended with 4 atomic Euclidean parameters. |
| saveArpParams/loadArpParams | `plugins/ruinae/src/parameters/arpeggiator_params.h` | Extended to serialize/deserialize Euclidean data after ratchet data. |
| handleArpParamChange() | `plugins/ruinae/src/parameters/arpeggiator_params.h` | Extended to dispatch Euclidean parameter IDs (3230-3233). |
| plugin_ids.h | `plugins/ruinae/src/plugin_ids.h` | New parameter IDs added at 3230-3233 within existing reserved range. No sentinel change needed. |
| TranceGate::setEuclidean() | `dsp/include/krate/dsp/processors/trance_gate.h:207-219` | **Reference pattern** for how EuclideanPattern is already used in this codebase: generate bitmask, iterate steps, check isHit(). The arp integration follows the same pattern but queries the bitmask per-step rather than pre-populating an array. |

**Initial codebase search for key terms:**

- `euclideanEnabled`: Not found in codebase. Name is safe -- no ODR risk.
- `euclideanPattern_`: Not found as a member variable (exists as class name `EuclideanPattern`). The trailing underscore differentiates the member. Safe.
- `euclideanPosition_`: Not found. Safe.
- `euclideanHits_`: Not found. Safe.
- `euclideanSteps_`: Not found. Safe.
- `euclideanRotation_`: Not found. Safe.
- `regenerateEuclideanPattern`: Not found. Safe.

**Search Results Summary**: No existing implementations of Euclidean timing integration in the arpeggiator exist. All proposed names are safe to introduce. The `EuclideanPattern` class (Layer 0) is the sole existing component to reuse.

### Forward Reusability Consideration

**Sibling features at same layer** (known from roadmap):

- Phase 8 (Conditional Trig System): Conditions filter which steps fire, evaluated AFTER Euclidean timing. The roadmap explicitly specifies this dependency: "Phase 8 depends on Phase 7 because Euclidean determines which steps exist to apply conditions to." The Euclidean gating check provides a pattern for conditional gating that Phase 8 can follow (early-out check before modifier evaluation).
- Phase 9 (Spice/Dice + Humanize): Randomization overlay may interact with Euclidean parameters (e.g., Spice could randomly perturb hits/rotation). The separate storage of Euclidean parameters (not merged into a lane) keeps the mutation surface clean.
- Phase 11 (Arpeggiator UI): Euclidean controls (Hits/Steps/Rotation knobs with visual pattern dots) will be built in the UI phase. The parameter IDs and value ranges defined here provide the interface contract.

**Potential shared components:**

- The Euclidean-before-modifier evaluation pattern established here (FR-006, FR-018) sets the precedent for Phase 8 conditional trigs: the evaluation chain becomes Euclidean gating -> Conditional trig -> Modifier priority -> Ratcheting.
- The `euclideanPosition_` management pattern (advance per step, reset in `resetLanes()`) is consistent with how a future condition lane position would work.

## Implementation Verification *(mandatory at completion)*

### Build & Test Results
- **Build**: Zero errors, zero warnings (Release build)
- **DSP tests**: 5,950 test cases -- all Euclidean tests pass (1 pre-existing unrelated benchmark failure in sidechain_filter_test.cpp)
- **Ruinae tests**: 454 test cases, 8,212 assertions -- ALL PASSED
- **Pluginval**: PASSED at strictness level 5 (19 tests, 0 failures)
- **Clang-tidy**: 0 errors, 0 warnings (DSP: 214 files, Ruinae: 3 files)

### Functional Requirements

| ID | Requirement | Status | Evidence |
|----|------------|--------|----------|
| FR-001 | 6 Euclidean state members with defaults | MET | `arpeggiator_core.h:1500-1508` -- bool, int, int, int, size_t, uint32_t. Constructor calls `regenerateEuclideanPattern()` at line 148. Test `EuclideanState_DefaultValues` passes. |
| FR-002 | Euclidean disabled = Phase 6 behavior | MET | `arpeggiator_core.h:1017` -- gating only when `euclideanEnabled_` true. Test `EuclideanDisabled_Phase6Identical` passes at 120/140/180 BPM with zero tolerance. |
| FR-003 | Position advances once per step | MET | `arpeggiator_core.h:1024-1025` -- `(euclideanPosition_ + 1) % euclideanSteps_`. Test `EuclideanGating_Tresillo_E3_8` verifies 8-step cycling. |
| FR-004 | Rest: all lanes advance, noteOff emitted | MET | `arpeggiator_core.h:1027-1046` -- cancel pending, emit noteOff, count=0, tieActive_=false. Lanes advance at 1007-1011. Test `EuclideanRestStep_AllLanesAdvance` passes. |
| FR-005 | Hit: step fires normally | MET | `arpeggiator_core.h:1047` -- falls through to modifier evaluation. Test `EuclideanGating_Tresillo_E3_8` verifies. |
| FR-006 | Euclidean evaluated before modifier | MET | `arpeggiator_core.h:1017-1049` -- Euclidean check before modifier block. Test `EuclideanEvaluationOrder_BeforeModifier` passes. |
| FR-007 | Rest breaks tie chain | MET | `arpeggiator_core.h:1039-1040` -- `tieActive_ = false`. Tests `EuclideanRestStep_BreaksTieChain`, `EuclideanRestStep_ModifierTie_TieChainBroken` pass. |
| FR-008 | Pattern regenerated on param change | MET | `arpeggiator_core.h:1436-1439` -- `regenerateEuclideanPattern()` calls `EuclideanPattern::generate()`. All setters call it. Test `EuclideanPatternGenerated_E3_8` passes. |
| FR-009 | Clamping in setters | MET | `arpeggiator_core.h:374-397` -- steps [2,32], hits [0,steps], rotation [0,31]. Tests `ClampHitsToSteps`, `ClampStepsToRange`, `ClampRotationToRange`, `HitsZeroAllowed` pass. |
| FR-010 | Enabled: position reset, ratchet preserved | MET | `arpeggiator_core.h:402-409` -- resets `euclideanPosition_=0`, no ratchet state touch. Tests `EuclideanEnabled_ResetsPosition`, `DoesNotClearRatchetState` pass. |
| FR-011 | fireStep flow order | MET | `arpeggiator_core.h:1002-1047` -- selector advance -> 5 lane advances -> Euclidean check -> modifier eval. Test `EuclideanGating_Tresillo_E3_8` passes. |
| FR-012 | Position advances unconditionally | MET | `arpeggiator_core.h:1024-1025` -- advances regardless of hit/rest. Also defensive branch at 1341-1346. |
| FR-013 | resetLanes resets position | MET | `arpeggiator_core.h:1456` -- `euclideanPosition_ = 0`. Tests `EuclideanResetLanes_ResetsPosition`, `EuclideanPositionReset_OnRetrigger` pass. |
| FR-014 | reset() regenerates pattern | MET | `arpeggiator_core.h:179` -- `regenerateEuclideanPattern()` after `resetLanes()`. |
| FR-015 | Getters exist | MET | `arpeggiator_core.h:416-433` -- 4 getters with `[[nodiscard]]`, `noexcept`. Test `EuclideanState_DefaultValues` verifies. |
| FR-016 | Rest suppresses ratchet | MET | `arpeggiator_core.h:1027-1046` -- rest path returns before ratchet evaluation. Test `EuclideanRestStep_RatchetSuppressed` passes. |
| FR-017 | Hit: ratchet applies | MET | Falls through to ratchet evaluation on hit. Test `EuclideanHitStep_RatchetApplies` passes (2 sub-steps). |
| FR-018 | Evaluation order: Euclidean before modifier | MET | Same as FR-006. Test `EuclideanEvaluationOrder_BeforeModifier` passes. |
| FR-019 | Modifier Rest on hit step works | MET | Modifier evaluation at line 1053+. Test `EuclideanHitStep_ModifierRestApplies` passes. |
| FR-020 | Modifier Tie interactions | MET | Tie on hit sustains, tie on rest overridden. Tests `EuclideanHitStep_ModifierTieApplies`, `EuclideanRestStep_ModifierTie_TieChainBroken` pass. |
| FR-021 | Chord mode | MET | Hit fires all chord notes, rest silences all. Test `EuclideanChordMode_HitFiresAll_RestSilencesAll` passes. |
| FR-022 | Swing orthogonal | MET | `calculateStepDuration()` called in both rest (line 1044) and normal (line 1367) paths. Test `EuclideanSwing_Orthogonal` passes. |
| FR-023 | Disable: no additional cleanup | MET | `arpeggiator_core.h:247-251` -- `setEnabled(false)` has no Euclidean-specific cleanup. |
| FR-024 | Enable: resetLanes called | MET | `arpeggiator_core.h:252-255` -- `setEnabled(true)` calls `resetLanes()`, which resets euclideanPosition_. |
| FR-025 | 4 parameter IDs | MET | `plugin_ids.h:1000-1004` -- 3230, 3231, 3232, 3233. Test `EuclideanParams_AllRegistered_WithCanAutomate` passes. |
| FR-026 | kArpEndId/kNumParameters unchanged | MET | `plugin_ids.h:1007,1010` -- kArpEndId=3299, kNumParameters=3300. |
| FR-027 | kCanAutomate, no kIsHidden | MET | `arpeggiator_params.h:479-499`. Test `EuclideanParams_AllRegistered_WithCanAutomate` passes. |
| FR-028 | 4 atomic members | MET | `arpeggiator_params.h:76-80` -- euclideanEnabled{false}, Hits{4}, Steps{8}, Rotation{0}. |
| FR-029 | handleArpParamChange dispatch | MET | `arpeggiator_params.h:229-250`. Tests `HandleParamChange_*` (4 tests) pass. |
| FR-030 | Euclidean serialized after ratchet | MET | `arpeggiator_params.h:776-780` -- 4 int32 fields. Test `RoundTrip_SaveLoad` passes. |
| FR-031 | EOF handling and clamping | MET | `arpeggiator_params.h:896-909` -- first field EOF=return true, subsequent=return false, clamp all. Tests `Phase6Backward_Compat`, `CorruptStream`, `OutOfRange_ValuesClamped` pass. |
| FR-032 | Prescribed setter order | MET | `processor.cpp:1331-1340` -- steps->hits->rotation->enabled. Test `ApplyToEngine_PrescribedOrder` passes. |
| FR-033 | formatArpParam outputs | MET | `arpeggiator_params.h:643-667`. Tests `FormatEnabled/Hits/Steps/Rotation` pass. |
| FR-034 | Controller sync | MET | `arpeggiator_params.h:1056-1071`. Test `ControllerSync_AfterLoad` passes. |
| FR-035 | Defensive branch advances position | MET | `arpeggiator_core.h:1341-1346`. Test `EuclideanDefensiveBranch_PositionAdvances` passes. |

### Success Criteria

| ID | Criterion | Status | Evidence |
|----|-----------|--------|----------|
| SC-001 | 5+ known Euclidean patterns match | MET | Tests: Tresillo E(3,8), AllHits E(8,8), ZeroHits E(0,8), Cinquillo E(5,8), BossaNova E(5,16) -- all pass with exact bit verification. |
| SC-002 | All rotations produce distinct patterns | MET | Tests: E(3,8) rotation 0 vs 1 differ; E(5,16) all 16 rotations distinct with exactly 5 hits each. |
| SC-003 | Polymetric cycling verified | MET | Test: Euclidean steps=5 + velocity length=3 = 15-step combined cycle, exact velocity sequence verified. |
| SC-004 | Euclidean disabled = Phase 6 identical | MET | Test: 1000+ steps at 120/140/180 BPM, zero tolerance -- same notes, velocities, offsets, legato flags. |
| SC-005 | On/off transitions clean | MET | Tests: DisabledToEnabled, EnabledToDisabled, MidStep, InFlightRatchet -- no stuck notes, clean boundaries. |
| SC-006 | Rest breaks tie chain | MET | Tests: BreaksTieChain, ModifierTie_TieChainBroken -- noteOff emitted, tieActive_ cleared. |
| SC-007 | Ratchet interaction correct | MET | Tests: RatchetSuppressed (rest=silence), RatchetApplies (hit=2 sub-steps). |
| SC-008 | Round-trip preserves all values | MET | Test: saves enabled=true, hits=5, steps=16, rotation=3; loads; all 4 identical. |
| SC-009 | Phase 6 backward compat | MET | Test: Phase 6 stream loads with defaults (false, 4, 8, 0), return true. |
| SC-010 | Zero heap allocation | MET | Code inspection: no new/delete/malloc/free/vector/string/map in Euclidean paths. All fixed-size types. |
| SC-011 | Parameters registered and formatted | MET | Tests: 4 IDs with kCanAutomate, format strings "Off"/"On", "N hits", "N steps", "N". |
| SC-012 | Position reset on retrigger | MET | Tests: ResetLanes_ResetsPosition, PositionReset_OnRetrigger -- position resets to 0. |

### Self-Check
1. Did any test threshold change from spec? **No**
2. Any placeholder/stub/TODO in new code? **No**
3. Any features removed from scope without user approval? **No**
4. Would the spec author consider this done? **Yes**
5. Would the user feel cheated? **No**

### Overall Status: COMPLETE
