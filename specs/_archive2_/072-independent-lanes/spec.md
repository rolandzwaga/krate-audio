# Feature Specification: Independent Lane Architecture

**Feature Branch**: `072-independent-lanes`
**Plugin**: KrateDSP (Layer 1 primitive) + Ruinae (plugin integration)
**Created**: 2026-02-21
**Status**: Complete
**Input**: User description: "Phase 4 of Arpeggiator Roadmap: Independent Lane Architecture. Add ArpLane<T> template containers and velocity/gate/pitch lanes to the ArpeggiatorCore for polymetric arpeggiator patterns."
**Depends on**: Phase 3 (071-arp-engine-integration) -- COMPLETE

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Velocity Lane Shaping (Priority: P1)

A synthesist wants to program a repeating velocity accent pattern for their arpeggio. They set a velocity lane with 4 steps (e.g., loud-soft-soft-medium) that cycles independently from the note pattern. Each time the arpeggiator advances a step, the velocity lane provides the velocity value for that step's note. If the arpeggiator plays 8 notes, the 4-step velocity pattern repeats twice.

**Why this priority**: Velocity is the most immediately audible per-step variation. Without per-step velocity control, the arpeggiator plays every note at the same loudness, which sounds mechanical and lifeless. This is the minimum viable lane feature.

**Independent Test**: Can be fully tested by setting a velocity lane with known step values and verifying that arp events carry the correct velocities in the expected cycling order.

**Acceptance Scenarios**:

1. **Given** the arpeggiator is enabled with a velocity lane of length 4 set to [1.0, 0.3, 0.3, 0.7], **When** the arp advances 8 steps, **Then** the generated note events carry velocities corresponding to [1.0, 0.3, 0.3, 0.7, 1.0, 0.3, 0.3, 0.7] (the lane cycles).
2. **Given** the velocity lane is set to length 1 with value 0.5, **When** the arp advances any number of steps, **Then** every note event has velocity scaled to 0.5 of the input velocity (constant behavior, equivalent to a global velocity setting).
3. **Given** the velocity lane length is changed from 4 to 3 mid-playback, **When** the arp continues playing, **Then** the lane immediately begins cycling at the new length without audio glitches.

---

### User Story 2 - Gate Length Lane for Rhythmic Variation (Priority: P1)

A producer wants different note durations within a single arpeggio pattern. They set up a gate lane with step values like [short, long, short, short, long] to create rhythmic interest. The gate lane values multiply with the global gate length parameter to determine each note's actual duration.

**Why this priority**: Gate length variation is essential for rhythmic expressiveness. Combined with velocity, it transforms a monotonous arpeggio into a rhythmically engaging pattern. Gate and velocity together form the core of the lane system.

**Independent Test**: Can be fully tested by configuring a gate lane with known values, running the arpeggiator, and measuring that noteOff events fire at the expected sample offsets relative to their corresponding noteOn events.

**Acceptance Scenarios**:

1. **Given** a gate lane of length 3 set to [0.5, 1.0, 1.5] and a global gate length of 80%, **When** the arp fires 3 steps, **Then** the effective gate per step is [40%, 80%, 120%] of the step duration (gate lane value multiplied by global gate length).
2. **Given** a gate lane with values that produce effective gates above 100%, **When** notes overlap, **Then** the arpeggiator correctly handles the overlap (legato behavior: noteOff fires after the next noteOn).
3. **Given** a gate lane of length 5 and a velocity lane of length 3, **When** the arp plays 15 steps, **Then** both lanes have completed their full polymetric cycle (LCM of 3 and 5 = 15) and the combined pattern repeats.

---

### User Story 3 - Pitch Offset Lane for Melodic Patterns (Priority: P2)

A musician wants to add melodic variation to their arpeggio by transposing individual steps up or down by semitones. They program a pitch lane with offsets like [0, +7, +12, -5] which are added to the note selected by the arpeggiator's note selector on each step.

**Why this priority**: Pitch offsets transform the arpeggiator from a simple note cycler into a melodic sequencer. However, it depends on the lane infrastructure already working for velocity and gate, so it is a natural second step.

**Independent Test**: Can be fully tested by setting known pitch offsets in the lane and verifying that the MIDI note numbers in generated events are offset by the expected semitone amounts.

**Acceptance Scenarios**:

1. **Given** a pitch lane of length 4 set to [0, +7, +12, -5] semitones and the note selector outputs note 60 (C4), **When** the arp fires 4 steps, **Then** the generated noteOn events carry notes [60, 67, 72, 55].
2. **Given** a pitch offset that would push a note above 127 or below 0, **When** that step fires, **Then** the note is clamped to the valid MIDI range [0, 127] without crashing or wrapping.
3. **Given** a pitch lane of length 7, a velocity lane of length 3, and a gate lane of length 5, **When** the arp plays 105 steps, **Then** the full polymetric cycle completes (LCM of 3, 5, 7 = 105) and the combined pattern repeats exactly.

---

### User Story 4 - Polymetric Pattern Discovery (Priority: P2)

A sound designer sets up lanes of deliberately different lengths to create evolving, non-repeating patterns. For example, a 3-step velocity pattern over a 5-step gate pattern over a 7-step pitch pattern produces a combined cycle of 105 steps before the exact combination repeats. The musician uses this to generate complex, evolving sequences from simple individual lane patterns.

**Why this priority**: Polymetric interaction is the architectural differentiator of this arpeggiator. It elevates the instrument from a basic feature to a creative tool. However, it is an emergent property of the independent lane lengths rather than a separate feature to implement.

**Independent Test**: Can be tested by configuring lanes of coprime lengths, running the arpeggiator for the expected LCM number of steps, and verifying that the exact step combination does not repeat until the LCM boundary.

**Acceptance Scenarios**:

1. **Given** a velocity lane of length 3 and a gate lane of length 5, **When** the arp plays 15 steps, **Then** no two consecutive groups of 15 steps have different combined velocity+gate patterns (i.e., the pattern repeats exactly at step 15, not before).
2. **Given** all lanes set to length 1, **When** the arp plays any number of steps, **Then** the behavior is identical to the Phase 3 arpeggiator (constant values, no per-step variation).
3. **Given** all lanes set to the same length N, **When** the arp plays, **Then** all lanes advance in lockstep and the combined pattern repeats every N steps (no polymetric effect).

---

### User Story 5 - Lane State Persistence (Priority: P3)

A user programs a complex polymetric pattern with specific velocity, gate, and pitch lane configurations, then saves the preset. When they reload the preset later or in a different session, all lane lengths and step values are restored exactly as configured.

**Why this priority**: Without persistence, all lane programming is lost on preset save/load, rendering the feature impractical for real-world use. However, this is an integration concern that builds on the core lane functionality.

**Independent Test**: Can be tested by configuring specific lane values, saving plugin state, restoring the state into a fresh instance, and verifying all lane configurations match the original.

**Acceptance Scenarios**:

1. **Given** a fully configured set of lanes (velocity length 5, gate length 3, pitch length 7 with specific step values), **When** plugin state is saved and restored, **Then** all lane lengths and step values are identical after restore.
2. **Given** a preset saved by a version without lane support (Phase 3), **When** loaded into this version, **Then** all lanes default to length 1 with default values (velocity 1.0, gate 1.0, pitch 0) and the arpeggiator behaves identically to Phase 3.

---

### Edge Cases

- What happens when a lane length is set to 0? The system MUST clamp to minimum length 1.
- What happens when the held note buffer becomes empty while lanes are mid-cycle? Lanes pause at their current step position and resume from that same step when new notes are held again. This is a pause, not a reset — the lane counters are not zeroed. Only the explicit reset triggers listed in FR-022 return lanes to step 0.
- What happens when the arpeggiator is disabled and re-enabled? Lane positions MUST reset to step 0.
- What happens when retrigger mode resets the arpeggiator? All lane positions MUST reset to step 0 simultaneously.
- What happens when a step value is set outside its valid range (e.g., pitch offset of +50)? Values MUST be clamped to their defined ranges.
- What happens when the arpeggiator is in Chord mode? Lane values apply to all notes in the chord equally (same velocity scale, same gate multiplier, same pitch offset for all chord notes on that step).
- What happens when transport stops and then restarts? Transport stop triggers `reset()` on the ArpeggiatorCore, which calls `resetLanes()` — all lane positions return to step 0. On transport restart the arp begins from step 0, so the first note after restart always uses step 0 from all lanes. This is consistent with the retrigger and disable/enable behavior documented in FR-022.

## Clarifications

### Session 2026-02-21

- Q: How should lane step values be stored in `ArpeggiatorParams` for thread-safe audio-thread reads? → A: `std::atomic<T>` per step, same pattern as `trance_gate_params.h` — 32 `std::atomic<float>` for velocity, 32 `std::atomic<float>` for gate, 32 `std::atomic<int>` for pitch (int to avoid `std::atomic<int8_t>` lock-free uncertainty), plus 3 `std::atomic<int>` for lane lengths.
- Q: What does "bit-identical" mean in SC-002 — strict binary equality or tolerance-bounded? → A: Strict bit-identical. Same exact integer note/velocity values AND noteOff fires at the exact same sample offset. Zero tolerance. Test fails if even 1 sample differs.
- Q: When the held note buffer empties in Latch Off mode (key release), do lanes pause at their current position or reset to step 0? → A: Lanes pause at current position. They resume from the same step when new keys are pressed. Only explicit reset triggers (retrigger mode, transport stop/start, disable/enable) return lanes to step 0.
- Q: Should the spec document why individual VST3 parameters per step were chosen over bulk/blob encoding for lane data? → A: Yes — individual parameters chosen because: (1) host automation works per-step, (2) matches the existing trance_gate_params.h pattern, (3) VST3 handles 99 additional parameters without issue at this scale. Blob encoding was rejected as it breaks per-step automation and deviates from the established pattern without benefit.
- Q: Can a velocity lane step value of 0.0 produce velocity 0 (silence), or is 1 the enforced minimum floor? → A: Enforce minimum of 1. The velocity lane always produces an audible note; velocity 0 (MIDI NoteOff semantic) is never emitted via the velocity lane. Rests and silence are handled by the modifier lane (Phase 5), not by setting velocity to 0.

## Requirements *(mandatory)*

### Functional Requirements

**ArpLane Container (DSP Layer 1 Primitive)**

- **FR-001**: The system MUST provide a generic fixed-capacity step lane container (`ArpLane`) that stores up to 32 steps of a configurable value type.
- **FR-002**: The lane container MUST support configurable length from 1 to 32 steps, inclusive.
- **FR-003**: The lane container MUST provide an `advance()` operation that returns the current step value and moves the internal position forward by one step, wrapping to step 0 when the end is reached.
- **FR-004**: The lane container MUST provide a `reset()` operation that returns the internal position to step 0.
- **FR-005**: The lane container MUST provide `setStep(index, value)` and `getStep(index)` operations for random access to individual step values.
- **FR-006**: The lane container MUST provide a `currentStep()` query that returns the current position index (for UI playhead display in future Phase 11).
- **FR-007**: The lane container MUST perform zero heap allocation in all operations. All storage is internal (array-backed).
- **FR-008**: The lane container MUST clamp the length to [1, 32] when set. Out-of-range values are silently clamped.
- **FR-009**: The lane container MUST clamp step index parameters to [0, length-1] in `setStep()` and `getStep()`. Out-of-range reads return a raw `T{}` default value (e.g., `0.0f` for float, `0` for int8_t). Callers are responsible for any post-read clamping required by their own semantics (for example, the velocity lane's floor of 1 per FR-011 is enforced by `fireStep()`, not by `getStep()`).

**Velocity Lane**

- **FR-010**: The ArpeggiatorCore MUST contain a velocity lane that stores normalized velocity values (0.0 to 1.0) per step.
- **FR-011**: When the velocity lane is active, the velocity of each generated noteOn event MUST be the product of the input note's velocity and the current velocity lane step value, clamped to [1, 127] after scaling. The minimum is 1 (never 0) because velocity 0 carries NoteOff semantics in MIDI — silencing a step is the responsibility of the modifier lane (Phase 5), not the velocity lane. A lane value of 0.0 therefore produces the quietest audible note (velocity 1), not silence.
- **FR-012**: The velocity lane MUST default to length 1 with a step value of 1.0 (full input velocity passthrough, identical to Phase 3 behavior).

**Gate Lane**

- **FR-013**: The ArpeggiatorCore MUST contain a gate lane that stores gate multiplier values (0.01 to 2.0) per step.
- **FR-014**: The effective gate duration for each step MUST be calculated as: `stepDuration * globalGateLengthPercent/100 * gateLaneValue`. This means the global gate length parameter (kArpGateLengthId from Phase 3) acts as a multiplier on the per-step gate lane value. If this computation rounds to zero samples (e.g., very low gate percent combined with a small gate lane multiplier at a fast arp rate), the result MUST be clamped to a minimum of 1 sample so that a NoteOff is always emitted and no stuck-note condition can occur.
- **FR-015**: The gate lane MUST default to length 1 with a step value of 1.0 (pure global gate length, identical to Phase 3 behavior).

**Pitch Lane**

- **FR-016**: The ArpeggiatorCore MUST contain a pitch lane that stores signed semitone offsets (-24 to +24) per step.
- **FR-017**: The pitch offset from the current pitch lane step MUST be added to the note number selected by the NoteSelector before emitting the noteOn event.
- **FR-018**: The resulting MIDI note number MUST be clamped to [0, 127] after applying the pitch offset. Notes clamped to the boundary still fire (they are not silenced).
- **FR-019**: The pitch lane MUST default to length 1 with a step value of 0 (no transposition, identical to Phase 3 behavior).

**Lane Integration with ArpeggiatorCore**

- **FR-020**: All three lanes (velocity, gate, pitch) MUST advance by one step on each arp step tick, simultaneously and independently.
- **FR-021**: Because each lane has its own configurable length, lanes of different lengths MUST cycle at different rates, producing polymetric patterns. A velocity lane of length 3 and a gate lane of length 5 MUST produce a combined pattern that repeats every 15 steps (LCM).
- **FR-022**: When the arpeggiator is explicitly reset (via retrigger mode firing, transport stop/start, or disable/enable transition), all lane positions MUST reset to step 0 simultaneously. Held-note-buffer becoming empty (Latch Off key release) is NOT an explicit reset — lanes pause at their current position and resume from that same step when new keys are pressed. Only the three triggers above zero the lane counters.
- **FR-023**: Lane advancement MUST occur within the existing `processBlock()` method with zero additional heap allocation. Lane step value updates from `applyParamsToArp()` take effect at the start of the current audio block, before any `fireStep()` calls within that block, because `applyParamsToArp()` is called at the top of the audio processing callback before the main event loop.
- **FR-024**: The lane system MUST be backward-compatible: with all lanes at length 1 and default values, the arpeggiator's output MUST be identical to the Phase 3 implementation.

**Plugin Integration (Ruinae)**

- **FR-025**: The plugin MUST expose per-step parameters for each lane through the VST3 parameter system. The parameter ID ranges are:
  - Velocity lane: length at 3020, steps 0-31 at 3021-3052
  - Gate lane: length at 3060, steps 0-31 at 3061-3092
  - Pitch lane: length at 3100, steps 0-31 at 3101-3132
  - Note: ID 3100 (`kArpPitchLaneLengthId`) was previously the `kNumParameters` sentinel. The sentinel is simultaneously updated to 3200, so there is no collision. Any code that previously array-sized by `kNumParameters = 3100` must be updated to use the new sentinel `kNumParameters = 3200`.
- **FR-026**: The arpeggiator parameter ID range MUST be expanded from 3000-3099 to 3000-3199 to accommodate the lane parameters.
- **FR-027**: Each lane length parameter MUST be registered as a discrete parameter (integer, 1-32).
- **FR-028**: Each velocity lane step parameter MUST be registered as a continuous parameter (0.0-1.0, default 1.0).
- **FR-029**: Each gate lane step parameter MUST be registered as a continuous parameter (0.01-2.0, default 1.0).
- **FR-030**: Each pitch lane step parameter MUST be registered as a discrete parameter (-24 to +24 semitones, default 0).
- **FR-031**: All lane parameters MUST support host automation (kCanAutomate flag).
- **FR-032**: Lane state (lengths and all step values) MUST be included in plugin state serialization and deserialization.
- **FR-033**: Loading a preset saved before lane support was added (Phase 3 presets) MUST result in all lanes defaulting to length 1 with default values, with no crashes or corruption.
- **FR-034**: The atomic parameter storage struct (`ArpeggiatorParams`) MUST be extended with lane data for thread-safe communication between the host/UI thread and the audio thread. Storage uses `std::atomic<T>` per step, following the `trance_gate_params.h` pattern: 32 `std::atomic<float>` for velocity steps, 32 `std::atomic<float>` for gate steps, 32 `std::atomic<int>` for pitch steps (int preferred over int8_t to avoid lock-free uncertainty), and 3 `std::atomic<int>` for the three lane lengths.

### Key Entities

- **ArpLane<T>**: A generic, fixed-capacity, array-backed step container parameterized by value type T and maximum step count (default 32). Maintains an internal step position that advances and wraps independently. Key attributes: steps array, length, current position. Located at DSP Layer 1 (primitives).
- **Velocity Lane**: An `ArpLane<float>` instance within ArpeggiatorCore. Values range 0.0-1.0. Multiplies the input note velocity to determine the output velocity for each arp step.
- **Gate Lane**: An `ArpLane<float>` instance within ArpeggiatorCore. Values range 0.01-2.0. Multiplies the global gate length to determine the effective gate duration for each arp step.
- **Pitch Lane**: An `ArpLane<int8_t>` instance within ArpeggiatorCore. Values range -24 to +24 semitones. Added to the note number from NoteSelector to produce the final MIDI note for each arp step. Note the dual-type design: the DSP-side `ArpLane` uses `int8_t` natively because pitch offsets fit in one byte; the plugin parameter storage (`ArpeggiatorParams`) uses `std::atomic<int>` per step to guarantee lock-free operation on all platforms (`std::atomic<int8_t>` lock-free status is not universally guaranteed). The conversion from `int` to `int8_t` (with [-24, 24] clamping) occurs at the DSP boundary in `processor.cpp`.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Lanes of coprime lengths (3, 5, 7) produce a combined pattern that does not repeat before LCM steps (105), verified by comparing step-by-step output over a full cycle.
- **SC-002**: With all lanes at length 1 and default values, the arpeggiator produces strictly bit-identical output to the Phase 3 implementation across 1000+ steps at multiple tempos (verify at a minimum: 120, 140, and 180 BPM). Bit-identical means: (a) same exact integer MIDI note number, (b) same exact integer velocity byte, and (c) noteOff at the exact same sample offset — zero tolerance, test fails if any value differs by even 1. Because `x * 1.0f` is guaranteed equal to `x` in IEEE 754, the new gate formula `stepDuration * globalGateLengthPercent/100 * 1.0f` produces the same integer sample count as the Phase 3 formula when lane defaults are in effect. For velocity: `static_cast<int>(std::round(v * 1.0f)) == v` holds exactly for all integers v in [1, 127] by IEEE 754, satisfying the bit-identical constraint without special-casing. The Phase 3 baseline for this comparison is the unmodified Phase 3 `fireStep()` output captured before any lane code is introduced.
- **SC-003**: The `advance()` operation of ArpLane and the lane-enhanced `processBlock()` perform zero heap allocation, verified by code inspection (no new/delete/malloc/free/vector/string/map in the code paths).
- **SC-004**: Plugin state round-trip (save then load) preserves all lane lengths and step values exactly, verified by comparing every step value before and after serialization across all three lanes.
- **SC-005**: Preset backward compatibility is maintained: a Phase 3 preset (without lane data) loads successfully with lanes at default values, and the arpeggiator produces the same output as before lanes were added.
- **SC-006**: All 99 lane parameters (3 lengths + 3x32 steps) are registered with the host, automatable, and display correct human-readable values.
- **SC-007**: Lane position resets correctly on retrigger (Note mode), transport restart, and arp disable/enable, verified by checking that the first note after reset always uses step 0 from all lanes.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Phase 3 (071-arp-engine-integration) is fully complete and merged, providing a working ArpeggiatorCore with timing, event generation, latch, retrigger, and all 11 base parameters.
- The ArpeggiatorCore is header-only and located at `dsp/include/krate/dsp/processors/arpeggiator_core.h`.
- The existing parameter ID allocation ends at 3099 (kArpEndId). This spec expands the range to 3199.
- The kNumParameters sentinel value (currently 3100) will need to be updated to accommodate new parameters.
- Future phases (5-9) will add additional lane types (modifier bitmask, ratchet, condition, euclidean timing) using the same ArpLane<T> template, so the container design must be generic enough to support uint8_t bitmasks, uint8_t counts, and enum-like condition values.
- The UI for editing lane steps is deferred to Phase 11. This phase only exposes lanes through the VST3 parameter system (host automation, generic editors).

### Constraints & Tradeoffs

**Lane parameter storage: individual VST3 parameters vs. bulk/blob encoding**

The arpeggiator roadmap identified "lane parameter explosion" as a high-impact risk (6 lanes × 32 steps = 192+ parameters across all phases) and proposed bulk/blob encoding as a mitigation. For Phase 4 specifically, individual VST3 parameters were chosen for the following reasons:

1. **Host automation per step**: Individual parameters allow DAWs to automate any single lane step value, which is a first-class feature expectation. A blob would make per-step automation impossible.
2. **Pattern consistency**: The `trance_gate_params.h` precedent already registers 32 contiguous per-step parameters (`kTranceGateStepLevel0Id-31Id`) using this exact approach. Deviating would introduce two incompatible patterns in the same codebase.
3. **VST3 parameter count**: 99 new parameters (3 lengths + 96 step values) is well within VST3's practical limits. This decision should be revisited only if the total across all phases (potentially 192+) causes measurable host performance problems.

Blob encoding remains an option for Phases 5-8 if parameter count becomes a genuine problem. If adopted then, it should be applied uniformly (not mixed with individual params).

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| ArpeggiatorCore | `dsp/include/krate/dsp/processors/arpeggiator_core.h` | Primary extension target -- lanes are added as member variables, `fireStep()` is modified to read lane values |
| HeldNoteBuffer | `dsp/include/krate/dsp/primitives/held_note_buffer.h` | Reference for fixed-capacity, array-backed, zero-allocation container pattern. ArpLane follows the same design philosophy. |
| SequencerCore | `dsp/include/krate/dsp/primitives/sequencer_core.h` | Has a step counter and length concept; however, SequencerCore tracks per-sample gate state while ArpLane is a simpler value-per-step container. Conceptual reference only, not composed. |
| ArpeggiatorParams | `plugins/ruinae/src/parameters/arpeggiator_params.h` | Must be extended with atomic storage for lane data. Follows the same pattern for handleParamChange, register, format, save, load functions. |
| TranceGate step levels | `plugins/ruinae/src/plugin_ids.h` (kTranceGateStepLevel0Id-31Id) | Reference pattern for registering 32 contiguous per-step parameters. The lane parameters follow this exact ID allocation pattern. |
| trance_gate_params.h | `plugins/ruinae/src/parameters/trance_gate_params.h` | Reference for how per-step parameter changes are handled, registered, formatted, saved, and loaded. The lane parameter functions follow this established pattern. |
| StepPatternEditor | `plugins/shared/src/ui/step_pattern_editor.h` | Will be reused in Phase 11 for lane editing UI. Not needed in this phase, but the per-step parameter structure should be compatible with StepPatternEditor's expectations. |
| EuclideanPattern | `dsp/include/krate/dsp/core/euclidean_pattern.h` | Will be used in Phase 7 to auto-generate timing lane patterns. Not needed in this phase. |

**Search Results Summary**: No existing class named "ArpLane" or any lane-related template exists in the codebase. The name is safe to use without ODR risk. The fixed-capacity array pattern is well-established via HeldNoteBuffer and internal arrays in SequencerCore.

### Forward Reusability Consideration

*The ArpLane<T> template is designed to serve all future lane-based arpeggiator phases:*

**Sibling features at same layer** (known from roadmap):
- Phase 5: Per-Step Modifiers -- will use `ArpLane<uint8_t>` for bitmask modifier lane (Rest/Tie/Slide/Accent flags)
- Phase 6: Ratcheting -- will use `ArpLane<uint8_t>` for ratchet count lane (1-4)
- Phase 7: Euclidean Timing -- may use ArpLane internally or as output target for generated patterns
- Phase 8: Conditional Trig -- will use `ArpLane<uint8_t>` for TrigCondition enum values

**Potential shared components** (preliminary, refined in plan.md):
- `ArpLane<T>` is the primary shared component. It must support at minimum: `float` (velocity, gate), `int8_t` (pitch), `uint8_t` (modifiers, ratchet, conditions). The template parameter and MaxSteps default of 32 should accommodate all future lane types.
- Lane reset logic (resetting all lanes simultaneously on retrigger/transport) should be encapsulated so future lanes added in Phases 5-8 are automatically included.

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*For EACH row below, you MUST perform these steps before writing the status:*
1. *Re-read the requirement from the spec*
2. *Open the implementation file and find the code that satisfies it -- record the file path and line number*
3. *Run or read the test that proves it -- record the test name and its actual output/result*
4. *For numeric thresholds (SC-xxx): record the actual measured value vs the spec target*
5. *Only then write the status and evidence*

*DO NOT mark with a checkmark without having just verified the code and test output. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

### Build & Test Results
- Build: PASS (zero errors, zero warnings)
- DSP tests: 5825 test cases, 21,988,400 assertions -- ALL PASSED
- Ruinae tests: 409 test cases, 7,190 assertions -- ALL PASSED
- Pluginval L5: PASS (19 test sections, 0 failures)

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | `arp_lane.h:51-117` -- ArpLane<T, MaxSteps=32> template. Test: `Default construction` passes |
| FR-002 | MET | `arp_lane.h:66-72` -- setLength() clamps [1, MaxSteps]. Tests: clamp 0->1, 33->32 pass |
| FR-003 | MET | `arp_lane.h:100-105` -- advance() returns + wraps. Test: `advance cycles and wraps` passes |
| FR-004 | MET | `arp_lane.h:108` -- reset() sets position_=0. Test: `reset sets position back to 0` passes |
| FR-005 | MET | `arp_lane.h:78-92` -- setStep()/getStep() random access. Test: `round-trip 32 steps` passes |
| FR-006 | MET | `arp_lane.h:111` -- currentStep() returns position_. Test: `currentStep correct` passes |
| FR-007 | MET | `arp_lane.h:114-116` -- std::array backing, no heap. Test: sizeof verification passes |
| FR-008 | MET | `arp_lane.h:68` -- clamp to [1, MaxSteps]. Tests: 0->1, 33->32 pass |
| FR-009 | MET | `arp_lane.h:80-89` -- setStep clamps, getStep returns T{} for OOB. Tests pass |
| FR-010 | MET | `arpeggiator_core.h:891` -- ArpLane<float> velocityLane_. Test: DefaultIsPassthrough passes |
| FR-011 | MET | `arpeggiator_core.h:742-747` -- std::clamp(scaledVel, 1, 127). Tests: ScalesVelocity, ClampsToMinimum1, ClampsToMax127 pass |
| FR-012 | MET | `arpeggiator_core.h:114-120` -- Constructor sets step[0]=1.0f. Test: DefaultIsPassthrough passes |
| FR-013 | MET | `arpeggiator_core.h:892` -- ArpLane<float> gateLane_. Test: GateLane_DefaultIsPassthrough passes |
| FR-014 | MET | `arpeggiator_core.h:650-655` -- std::max(size_t{1}, ...) with double cast chain. Tests: MultipliesGlobalGate, MinimumOneSample pass |
| FR-015 | MET | `arpeggiator_core.h:119` -- gateLane_.setStep(0, 1.0f). Test: DefaultIsPassthrough passes |
| FR-016 | MET | `arpeggiator_core.h:893` -- ArpLane<int8_t> pitchLane_. Test: PitchLane_DefaultIsPassthrough passes |
| FR-017 | MET | `arpeggiator_core.h:750-755` -- int offsetNote + std::clamp(0,127). Test: PitchLane_AddsOffset [60,67,72,55] passes |
| FR-018 | MET | `arpeggiator_core.h:754` -- Clamped notes still fire. Tests: ClampsHigh, ClampsLow, NoteStillFires_WhenClamped pass |
| FR-019 | MET | `arpeggiator_core.h:893` -- pitchLane_ value-init to 0. Test: DefaultIsPassthrough passes |
| FR-020 | MET | `arpeggiator_core.h:737-739` -- All 3 lanes advance() in fireStep(). Tests: polymetric LCM tests pass |
| FR-021 | MET | Independent lengths. Tests: VelGate LCM=15, VelGatePitch LCM=105 pass |
| FR-022 | MET | resetLanes() called from: reset() (line 149), noteOn retrigger (175), setEnabled (221), bar boundary (509). Lane pause on empty heldNotes_. Tests: retrigger, disable, transport-stop, pause all pass |
| FR-023 | MET | Lane advance in fireStep(), no heap. applyParamsToArp in processor.cpp:1270-1301 |
| FR-024 | MET | Default values = identity operations. Tests: BitIdentical_VelocityDefault, BitIdentical_GateDefault at 3 tempos pass |
| FR-025 | MET | plugin_ids.h:817-922 -- 99 IDs (3020-3132). Test: SC006_AllLaneParamsRegistered passes |
| FR-026 | MET | plugin_ids.h:925,928 -- kArpEndId=3199, kNumParameters=3200 |
| FR-027 | MET | arpeggiator_params.h:271-274 -- RangeParameter [1,32] default 1 stepCount 31. Tests pass |
| FR-028 | MET | arpeggiator_params.h:282-287 -- Velocity [0.0,1.0] default 1.0. Test passes |
| FR-029 | MET | arpeggiator_params.h:303-308 -- Gate [0.01,2.0] default 1.0. Test passes |
| FR-030 | MET | arpeggiator_params.h:324-329 -- Pitch [-24,+24] default 0 stepCount 48. Test passes |
| FR-031 | MET | Step params: kCanAutomate|kIsHidden. Length params: kCanAutomate only. Test: SC006 passes |
| FR-032 | MET | saveArpParams/loadArpParams in arpeggiator_params.h:479-598. Tests: all SaveLoad_RoundTrip + FullRoundTrip pass |
| FR-033 | MET | arpeggiator_params.h:566 -- EOF-safe return true. Tests: BackwardCompat + Phase3Compat pass |
| FR-034 | MET | std::atomic<int> for pitch steps (lock-free). Test: is_lock_free() assertion passes |
| SC-001 | MET | Tests: LCM105 no early repeat (steps 1-104 all unique), repeat at LCM (step 105 = step 0) |
| SC-002 | MET | Tests: BitIdentical_VelocityDefault 1000+ steps at 120/140/180 BPM, 0 mismatches. BitIdentical_GateDefault same. Zero tolerance. |
| SC-003 | MET | Code inspection: only std::array + arithmetic in all ArpLane methods. No new/delete/malloc/vector/string |
| SC-004 | MET | Test: LanePersistence_FullRoundTrip -- 99 values + beyond-length steps preserved exactly |
| SC-005 | MET | Test: LanePersistence_Phase3Compat_NoLaneData -- 11-param stream loads with all lane defaults |
| SC-006 | MET | Test: SC006_AllLaneParamsRegistered -- 99 params found, correct flags verified |
| SC-007 | MET | Tests: retrigger, disable/enable, transport-stop all reset lanes to step 0. First note uses step 0 values. |

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

All 34 FRs and 7 SCs are MET. No gaps, no deferred items.
