# Feature Specification: VowelSequencer with SequencerCore Refactor

**Feature Branch**: `099-vowel-sequencer`
**Created**: 2026-01-25
**Status**: Draft
**Input**: User description: "VowelSequencer - 8-step vowel pattern sequencer with SequencerCore refactor for shared sequencing logic"

## Clarifications

### Session 2026-01-25

- Q: What should be the default vowel pattern when VowelSequencer is first instantiated? → A: Steps default to palindrome A,E,I,O,U,O,I,E (matches User Story 2 scenario 2)
- Q: When a preset with fewer than 8 steps is loaded, what happens to the remaining steps and the numSteps value? → A: Remaining steps keep previous values, numSteps updates to match preset length
- Q: When the gate is off (filter bypassed), how should the dry signal be routed? → A: Filtered signal fades to zero, dry passes through continuously at full level (dry path always unity gain, filtered path fades out to avoid clicks, no gain changes, minimal CPU)
- Q: How should swing interact with different playback directions (Backward, PingPong, Random)? → A: Swing applies to step indices (even steps long, odd steps short) regardless of direction
- Q: When refactoring FilterStepSequencer to use SequencerCore, must existing tests pass byte-for-byte unchanged? → A: Test behaviors must pass (test code may be updated to accommodate new architecture while preserving all 33 tests and 181 assertions)

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Extract Reusable Sequencing Logic (Priority: P1)

A developer needs to create multiple sequencer-based effects (FilterStepSequencer, VowelSequencer, future arpeggiators) without duplicating ~160 lines of timing and direction logic. By extracting SequencerCore as a Layer 1 primitive, all sequencer implementations can share the same well-tested timing, direction, swing, and transport sync code.

**Why this priority**: This is foundational infrastructure - without SequencerCore, VowelSequencer would duplicate FilterStepSequencer's logic, violating DRY principles and creating maintenance burden.

**Independent Test**: Can be fully tested by instantiating SequencerCore alone and verifying step advancement, timing accuracy, direction modes, and transport sync work correctly without any filter or vowel processing attached.

**Acceptance Scenarios**:

1. **Given** SequencerCore at 120 BPM with 1/4 notes and 4 steps, **When** tick() is called for 2 seconds (88200 samples at 44.1kHz), **Then** step index should cycle through 0,1,2,3,0,1,2,3 exactly (each step 500ms).

2. **Given** SequencerCore in PingPong mode with 4 steps, **When** advanced through 8 steps, **Then** step sequence should be 0,1,2,3,2,1,0,1 (endpoints visited once per cycle).

3. **Given** SequencerCore in Random mode with 4 steps, **When** advanced through 40 steps, **Then** all 4 steps should have been visited and no immediate repetitions should occur.

4. **Given** FilterStepSequencer refactored to use SequencerCore, **When** all existing FilterStepSequencer behavior tests run, **Then** all 33 test behaviors (181 assertions) should pass (test code may be updated for new architecture).

---

### User Story 2 - Basic Vowel Sequencing (Priority: P1)

A producer wants to add rhythmic "talking" vowel effects to synth pads or bass lines by having the formant filter step through different vowel sounds (A, E, I, O, U) in sync with the track tempo.

**Why this priority**: This is the core VowelSequencer functionality - rhythmic vowel stepping is the fundamental feature.

**Independent Test**: Can be fully tested by setting 4 vowels in a pattern, playing audio at 120 BPM, and verifying the formant frequencies change to match each vowel at the correct timing.

**Acceptance Scenarios**:

1. **Given** VowelSequencer with pattern [A, E, I, O] at 1/4 notes and 120 BPM, **When** audio is processed for 2 seconds, **Then** each vowel should be active for exactly 500ms and the sequence should complete one full cycle.

2. **Given** VowelSequencer with default initialization, **When** prepare() is called and process() runs, **Then** formant filter should produce valid output using sensible defaults (pattern: A,E,I,O,U,O,I,E).

3. **Given** VowelSequencer with 8 steps at 1/8 note rate, **When** tempo changes from 100 BPM to 140 BPM mid-playback, **Then** step duration should adapt immediately to maintain musical timing.

---

### User Story 3 - Smooth Vowel Morphing (Priority: P2)

A sound designer wants smooth, legato transitions between vowels rather than abrupt jumps, creating fluid "talking" effects that feel organic and musical.

**Why this priority**: Morphing is essential for musical applications where abrupt formant changes would be jarring.

**Independent Test**: Can be tested by measuring the formant frequency transitions when morph time is enabled and verifying smooth interpolation between vowel formants.

**Acceptance Scenarios**:

1. **Given** morph time of 50ms and a vowel change from A to E, **When** step advances, **Then** formant frequencies should transition smoothly over 50ms via FormantFilter's setVowelMorph().

2. **Given** morph time of 0ms, **When** step advances, **Then** vowel changes should be instantaneous (within one sample).

3. **Given** a very short step duration (faster than morph time), **When** processed, **Then** morph is truncated and target vowel frequencies are reached exactly at the next step boundary.

---

### User Story 4 - Playback Direction Modes (Priority: P2)

A creative producer wants to play the vowel sequence in reverse, ping-pong, or random order to create variation and unpredictability in the talking pattern.

**Why this priority**: Direction modes add creative variation and are inherited from SequencerCore, so they come essentially "for free."

**Independent Test**: Can be tested by logging step indices during playback and verifying the sequence follows the expected pattern for each direction mode.

**Acceptance Scenarios**:

1. **Given** VowelSequencer in Forward mode with 5 vowels [A,E,I,O,U], **When** processed, **Then** steps should advance 0,1,2,3,4,0,1...

2. **Given** VowelSequencer in Backward mode, **When** processed, **Then** steps should advance 4,3,2,1,0,4,3...

3. **Given** VowelSequencer in PingPong mode, **When** processed, **Then** steps should advance 0,1,2,3,4,3,2,1,0,1...

4. **Given** VowelSequencer in Random mode, **When** processed for 50 steps with 5 vowels, **Then** all vowels should have been visited with no immediate repetitions.

---

### User Story 5 - Talking Presets (Priority: P2)

A producer wants quick access to pre-defined vowel patterns that create recognizable "talking" effects like "wow", "yeah", or "aeiou" without manually programming each step.

**Why this priority**: Presets dramatically improve usability and provide instant gratification for the effect.

**Independent Test**: Can be tested by loading a preset and verifying the pattern is correctly set to the expected vowels.

**Acceptance Scenarios**:

1. **Given** setPreset("aeiou"), **When** pattern is queried, **Then** pattern should be [A,E,I,O,U] with numSteps=5 (steps 5-7 unchanged from previous state).

2. **Given** setPreset("wow"), **When** pattern is queried, **Then** pattern should be [O,A,O] with numSteps=3 (steps 3-7 unchanged from previous state).

3. **Given** setPreset("yeah"), **When** pattern is queried, **Then** pattern should be [I,E,A] with numSteps=3 (steps 3-7 unchanged from previous state).

4. **Given** an unknown preset name, **When** setPreset() is called, **Then** pattern and numSteps should remain unchanged and function should return false.

---

### User Story 6 - Per-Step Formant Shift (Priority: P3)

A producer wants to apply different formant shifts to individual steps, creating pitch-varied talking effects where some vowels sound higher or lower.

**Why this priority**: Per-step formant shift adds another dimension of creative control but is supplementary to core vowel sequencing.

**Independent Test**: Can be tested by setting different formant shift values per step and measuring the resulting formant frequencies.

**Acceptance Scenarios**:

1. **Given** step 0 with +12 semitone shift and step 1 with -12 semitone shift, **When** processed, **Then** formant frequencies should differ by approximately one octave between steps.

2. **Given** formant shift values outside [-24, +24] semitones, **When** set, **Then** values should be clamped to valid range.

---

### User Story 7 - Gate Length Control (Priority: P3)

A user wants the vowel effect to be active for only a portion of each step (like a trance gate), creating rhythmic pumping effects where the formant alternates between active and bypassed states.

**Why this priority**: Gate length is inherited from SequencerCore and provides specialized rhythmic effects.

**Independent Test**: Can be tested by setting 50% gate length and verifying the formant filter processes only the first half of each step.

**Acceptance Scenarios**:

1. **Given** 50% gate length, **When** step is active, **Then** formant filter should process for first 50% of step duration (wet signal at unity, dry silent), then during 5ms crossfade wet fades to zero and dry remains at unity, then dry signal passes through unchanged for remaining duration.

2. **Given** 100% gate length (default), **When** processed, **Then** formant filter should be active for the entire step duration with wet at unity and dry silent.

3. **Given** gate off transitions, **When** audio passes from formant-filtered to dry, **Then** wet signal fades to zero over 5ms while dry signal remains continuously at unity gain (no level change, no gain dip).

---

### User Story 8 - DAW Transport Sync (Priority: P3)

A producer wants the vowel sequencer to stay locked to the DAW timeline so that the talking pattern always starts at the same musical position regardless of where playback begins.

**Why this priority**: Transport sync is inherited from SequencerCore and is important for professional DAW integration.

**Independent Test**: Can be tested by calling sync() with different PPQ positions and verifying the sequencer jumps to the correct step.

**Acceptance Scenarios**:

1. **Given** VowelSequencer with 5 vowels at 1/4 notes and PPQ position of 2.0 (beat 3), **When** sync() is called, **Then** sequencer should be at step 2.

2. **Given** PPQ position that doesn't align exactly with a step boundary, **When** sync() is called, **Then** sequencer should be at the step containing that position with correct phase within the step.

---

### Edge Cases

- What happens when tempo is 0 or negative? (Clamp to minimum 20 BPM - inherited from SequencerCore)
- What happens when numSteps is set to 0 or negative? (Clamp to minimum 1 step)
- How does system handle NaN/Inf input audio? (FormantFilter handles this - pass through to underlying filter)
- What happens when sample rate changes mid-playback? (Recalculate step durations on next process)
- How does morph interact with very fast step rates? (Morph is truncated; target reached exactly at step boundary)
- What happens when step duration is shorter than morph time? (Morph truncated to fit step duration)
- How does Random mode prevent getting stuck on one vowel? (Immediate repetition prevented via SequencerCore)
- What is the PingPong turnaround behavior at endpoints? (Endpoints visited once per cycle - inherited from SequencerCore)
- What is the gate on/off crossfade duration? (Fixed 5ms crossfade - inherited from SequencerCore)
- How does swing behave in Backward or PingPong modes? (Swing applies to step indices regardless of direction: even steps always longer, odd steps always shorter)
- What happens when setPreset() loads a 3-step preset after an 8-step pattern was active? (numSteps becomes 3, steps 0-2 updated to preset values, steps 3-7 unchanged from previous state)

### Gate Bypass Design Note

**VowelSequencer vs FilterStepSequencer gate behavior differs intentionally:**

| Component | Gate On | Gate Off | Formula |
|-----------|---------|----------|---------|
| **VowelSequencer** | wet at unity, dry silent | wet fades to 0, dry at unity | `output = wet * gateRamp + input` |
| **FilterStepSequencer** | wet at unity, dry at 0 | crossfade to dry | `output = wet * gateGain + input * (1 - gateGain)` |

**Rationale**: VowelSequencer uses bypass-safe design per spec clarification Q3 - dry path is always at unity gain with no crossfade math, which prevents any gain changes during gate transitions and minimizes CPU usage. FilterStepSequencer uses a traditional crossfade that redistributes energy between wet and dry.

## Requirements *(mandatory)*

### Functional Requirements

#### SequencerCore (Layer 1 Primitive)

- **FR-001**: SequencerCore MUST support up to 16 steps (kMaxSteps = 16)
- **FR-002**: SequencerCore MUST allow setting the number of active steps from 1 to 16 via `setNumSteps(size_t)` method
- **FR-003**: SequencerCore MUST accept tempo in BPM (20.0 to 300.0 range)
- **FR-004**: SequencerCore MUST support note value divisions from 1/1 to 1/32 including triplets and dotted variants via existing NoteValue and NoteModifier enums
- **FR-005**: SequencerCore MUST provide swing/shuffle control (0% to 100%)
- **FR-005a**: Swing MUST apply to step indices (even-indexed steps longer, odd-indexed steps shorter) regardless of playback direction
- **FR-006**: SequencerCore MUST support Forward, Backward, PingPong, and Random playback directions
- **FR-006a**: In PingPong mode, endpoints MUST be visited once per cycle while middle steps are visited twice
- **FR-006b**: In Random mode, immediate step repetition MUST be prevented
- **FR-007**: SequencerCore MUST provide sync(double ppqPosition) method for DAW transport lock
- **FR-008**: SequencerCore MUST provide trigger() method for manual step advance
- **FR-009**: SequencerCore MUST provide tick() method that returns true when step advances
- **FR-010**: SequencerCore MUST provide getCurrentStep() method returning current step index
- **FR-011**: SequencerCore MUST provide gate length control (0% to 100%) with isGateActive() method
- **FR-012**: SequencerCore MUST provide gate crossfade ramp (5ms fixed duration)
- **FR-012a**: When gate transitions from active to inactive, the wet signal MUST fade to zero over 5ms while the dry signal remains at unity gain continuously (no crossfade math on dry path)
- **FR-013**: All SequencerCore methods MUST be noexcept for real-time safety
- **FR-014**: SequencerCore MUST NOT allocate memory during tick() or process operations

#### VowelSequencer (Layer 3 System)

- **FR-015**: VowelSequencer MUST support up to 8 programmable vowel steps (kMaxSteps = 8)
- **FR-015a**: VowelSequencer MUST default to 8 steps with pattern A,E,I,O,U,O,I,E (palindrome) on initialization
- **FR-016**: Each step MUST store a Vowel value (A, E, I, O, U)
- **FR-017**: Each step MUST store a formant shift value in semitones (-24 to +24)
- **FR-017a**: Formant shift values MUST default to 0.0 semitones on initialization
- **FR-018**: VowelSequencer MUST compose SequencerCore for all timing/direction functionality
- **FR-019**: VowelSequencer MUST use existing FormantFilter for vowel sound generation
- **FR-020**: VowelSequencer MUST provide morph time control (0ms to 500ms) for smooth vowel transitions
- **FR-020a**: When step duration is shorter than morph time, morph MUST be truncated to reach target at step boundary
- **FR-021**: VowelSequencer MUST provide setPreset(const char* name) for built-in patterns
- **FR-021a**: When setPreset() is called, numSteps MUST be updated to match the preset's step count while remaining steps preserve their previous values
- **FR-022**: VowelSequencer MUST provide process(float input) method for single-sample processing
- **FR-023**: VowelSequencer MUST provide processBlock(float* buffer, size_t numSamples, const BlockContext* ctx) method
- **FR-024**: All VowelSequencer process methods MUST be noexcept for real-time safety
- **FR-025**: VowelSequencer MUST NOT allocate memory during process()

#### FilterStepSequencer Refactor

- **FR-026**: FilterStepSequencer MUST be refactored to use SequencerCore for timing/direction logic
- **FR-027**: FilterStepSequencer's public API MUST remain unchanged (backward compatible)
- **FR-028**: All existing FilterStepSequencer tests MUST pass after refactor (all 33 tests and 181 assertions preserved; test code may be updated to accommodate new architecture)
- **FR-029**: FilterStepSequencer MUST retain its filter-specific logic (cutoff, Q, type, gain per step)

### Key Entities

- **SequencerCore**: Layer 1 primitive handling timing, direction, transport sync, and gate logic - the reusable "engine" for any step sequencer
- **VowelStep**: Holds vowel and formant shift for one sequence position
- **VowelSequencer**: Layer 3 system composing SequencerCore + FormantFilter for rhythmic vowel effects
- **Direction**: Enum for playback direction (Forward, Backward, PingPong, Random) - shared with FilterStepSequencer

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: SequencerCore step timing at 120 BPM with 1/4 notes deviates by less than 1ms from theoretical 500ms duration
- **SC-002**: Morph transitions complete within 1% of specified morph time when step duration >= morph time; when step duration < morph time, target is reached at step boundary with zero drift
- **SC-003**: Vowel transitions produce no audible clicks when morph time is greater than 0ms
- **SC-004**: Swing at 50% produces step duration ratio between 2.9:1 and 3.1:1 (targeting 3:1) for even vs odd step indices in all playback directions
- **SC-005**: All 8 vowel steps can be programmed and recalled correctly after prepare() and reset() cycles
- **SC-006**: Random playback visits all N steps within 10*N iterations with no immediate repetitions
- **SC-007**: Combined SequencerCore + FormantFilter CPU usage for processing 1 second of audio at 48kHz remains under 1% on a single core
- **SC-008**: PPQ sync positions sequencer within 1 sample of correct step phase
- **SC-009**: Gate length transitions produce no clicks (verified via peak detection using 5ms crossfade)
- **SC-010**: Per-step formant shift accuracy within 1 semitone of specified values
- **SC-011**: All 33 existing FilterStepSequencer tests (181 assertions) pass after SequencerCore refactor

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Host provides valid tempo via BlockContext (minimum 20 BPM)
- Sample rate is set before any processing occurs
- PPQ position is provided as musical beats from start of timeline (DAW-standard)
- Random direction uses a simple PRNG; cryptographic randomness is not required
- VowelSequencer uses 8 steps maximum (simpler than FilterStepSequencer's 16) as vowel patterns are typically shorter
- Preset patterns are limited to the 5 vowels (A, E, I, O, U) available in FormantFilter

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| FormantFilter | `dsp/include/krate/dsp/processors/formant_filter.h` | Core filter - direct dependency, use as-is |
| FilterStepSequencer | `dsp/include/krate/dsp/systems/filter_step_sequencer.h` | Source of sequencing logic to extract into SequencerCore |
| LinearRamp | `dsp/include/krate/dsp/primitives/smoother.h` | Use for morph/gate crossfade smoothing |
| NoteValue, NoteModifier | `dsp/include/krate/dsp/core/note_value.h` | Tempo sync timing - use existing enums |
| getBeatsForNote() | `dsp/include/krate/dsp/core/note_value.h` | Reference for timing calculations |
| Direction enum | `dsp/include/krate/dsp/systems/filter_step_sequencer.h` | Move to SequencerCore, share with both sequencers |
| Vowel enum | `dsp/include/krate/dsp/core/filter_tables.h` | Vowel selection - use existing enum |
| BlockContext | `dsp/include/krate/dsp/core/block_context.h` | Context structure with tempo info |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "SequencerCore" dsp/ plugins/
grep -r "VowelSequencer" dsp/ plugins/
grep -r "class.*Sequencer" dsp/include/
```

**Search Results Summary**: No existing SequencerCore or VowelSequencer implementations found. FilterStepSequencer exists and contains ~160 lines of sequencing logic (timing, direction, swing, transport sync, gate handling) that should be extracted to SequencerCore. FormantFilter exists and provides vowel sound generation.

### Forward Reusability Consideration

**Sibling features at same layer** (if known):
- FilterStepSequencer (existing, will use SequencerCore)
- Future FilterArpeggiator (may use SequencerCore)
- Future Multi-Parameter Sequencer (may generalize sequencer concept)

**Potential shared components** (preliminary, refined in plan.md):
- SequencerCore is specifically designed for maximum reuse across all sequencer-based effects
- Direction enum should be defined in SequencerCore and exported for both FilterStepSequencer and VowelSequencer
- Timing calculation logic (swing, step duration) is fully encapsulated in SequencerCore

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is not met without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| FR-003 | | |
| FR-004 | | |
| FR-005 | | |
| FR-006 | | |
| FR-006a | | |
| FR-006b | | |
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
| FR-020a | | |
| FR-021 | | |
| FR-022 | | |
| FR-023 | | |
| FR-024 | | |
| FR-025 | | |
| FR-026 | | |
| FR-027 | | |
| FR-028 | | |
| FR-029 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |
| SC-009 | | |
| SC-010 | | |
| SC-011 | | |

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
