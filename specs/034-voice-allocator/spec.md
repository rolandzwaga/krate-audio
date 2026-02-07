# Feature Specification: Voice Allocator

**Feature Branch**: `034-voice-allocator`
**Created**: 2026-02-07
**Status**: Draft
**Input**: User description: "Layer 3 System DSP Component: Voice Allocator -- Core polyphonic voice management without owning actual voice DSP. Configurable voice count (1-32), allocation modes (round-robin, oldest, lowest-velocity, highest-note), voice stealing with release tail option, note tracking, and unison mode support. Phase 2.1 of synth-roadmap.md."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Basic Polyphonic Voice Allocation (Priority: P1)

A synthesizer engine needs to manage a pool of voices so that incoming MIDI note events are correctly routed to available voices. When a note-on event arrives, the voice allocator assigns an idle voice from the pool and returns a VoiceEvent describing which voice index should begin playing, along with the note number, velocity, and pre-computed frequency. When a note-off event arrives, the allocator identifies which voice is playing that note and returns a VoiceEvent instructing that voice to enter its release phase. The allocator tracks which voice is playing which note at all times. When a voice's envelope completes its release and the voice becomes idle again, the owning code calls `voiceFinished()` to return the voice to the available pool. This is the fundamental building block that enables polyphonic playback -- without it, a synthesizer can only play one note at a time.

**Why this priority**: Without basic note-on/note-off allocation and tracking, no polyphonic synthesizer can function. This is the absolute minimum viable product. Every other feature (stealing, allocation modes, unison) builds on top of this core note-to-voice mapping.

**Independent Test**: Can be fully tested by creating a VoiceAllocator with N voices, sending note-on events and verifying that unique voice indices are assigned, sending note-off events and verifying the correct voices enter release, calling voiceFinished() and verifying voices return to the idle pool. Delivers a usable polyphonic allocator.

**Acceptance Scenarios**:

1. **Given** a VoiceAllocator with 8 voices and all voices idle, **When** a note-on for note 60 velocity 100 is sent, **Then** a single VoiceEvent of type NoteOn is returned with a valid voice index (0-7), note=60, velocity=100, and the correct frequency for MIDI note 60 (~261.63 Hz at A4=440).
2. **Given** a VoiceAllocator with 8 voices where voice 3 is playing note 60, **When** a note-off for note 60 is sent, **Then** a single VoiceEvent of type NoteOff is returned with voiceIndex=3 and note=60.
3. **Given** a VoiceAllocator with 8 voices where voices 0-4 are active, **When** a note-on for a new note arrives, **Then** an idle voice (index 5, 6, or 7) is assigned.
4. **Given** a VoiceAllocator with 8 voices where voice 2 has received a note-off and is in the releasing state, **When** `voiceFinished(2)` is called, **Then** voice 2 returns to the idle pool and can be assigned to a new note.
5. **Given** a VoiceAllocator with 8 voices, **When** `getActiveVoiceCount()` is called, **Then** the correct number of voices that are either playing or releasing is returned.
6. **Given** a VoiceAllocator where all 8 voices are idle, **When** 8 different notes are sent in sequence, **Then** each note is assigned to a different voice (no voice is used twice).
7. **Given** a VoiceAllocator, **When** a note-off for a note that is not currently playing is sent, **Then** no VoiceEvent is returned (empty span).

---

### User Story 2 - Allocation Mode Selection (Priority: P2)

A synthesizer designer needs control over how voices are chosen from the available pool, because different allocation strategies produce different musical results. In **round-robin** mode, voices cycle sequentially through the pool, distributing wear evenly across voice circuits (historically important for analog synths where each voice sounded slightly different -- the Sequential Prophet-5 and Oberheim OB-X used this approach). In **oldest** mode, when stealing is required, the voice that has been active the longest is selected, which is the most common strategy in modern synthesizers and minimizes audible disruption because the oldest note's amplitude envelope has progressed the furthest. In **lowest-velocity** mode, the voice playing with the lowest velocity is selected for stealing, preserving the perceptually louder notes. In **highest-note** mode, the voice playing the highest pitched note is selected for stealing, preserving lower notes that form the harmonic foundation (common in bass-heavy playing styles where the lowest note must not drop out, as documented in voice allocation literature from the Electronic Music Wiki and Sweetwater). The allocation mode can be changed at any time without disrupting currently active voices.

**Why this priority**: Allocation modes define the musical character of the polyphonic instrument. While the allocator works with a single default mode (P1), having selectable modes is essential for matching the allocator's behavior to different musical contexts. P2 because a single mode is sufficient for basic operation, but mode selection is the first thing a synth designer will customize.

**Independent Test**: Can be tested by configuring different allocation modes, filling all voices, sending additional notes, and verifying which voice is selected for each mode according to its documented strategy.

**Acceptance Scenarios**:

1. **Given** a VoiceAllocator with 4 voices in RoundRobin mode with all voices idle, **When** 6 notes are sent in sequence, **Then** voices are assigned in order 0, 1, 2, 3, and then the 5th and 6th notes steal voices 0 and 1 (cycling through the pool).
2. **Given** a VoiceAllocator with 4 voices in Oldest mode where voice 0 was triggered first, voice 1 second, voice 2 third, voice 3 fourth, **When** a 5th note arrives, **Then** voice 0 (the oldest) is stolen.
3. **Given** a VoiceAllocator with 4 voices in LowestVelocity mode where voices have velocities 100, 40, 80, 60, **When** a 5th note arrives, **Then** voice 1 (velocity 40, the lowest) is stolen.
4. **Given** a VoiceAllocator with 4 voices in HighestNote mode where voices are playing notes 48, 72, 60, 55, **When** a 5th note arrives, **Then** voice 1 (note 72, the highest) is stolen.
5. **Given** the allocation mode is changed from RoundRobin to Oldest while voices are active, **When** the next note triggers voice stealing, **Then** the Oldest strategy is used. Currently active voices are not disrupted by the mode change.

---

### User Story 3 - Voice Stealing with Release Tail Option (Priority: P3)

When all voices are occupied and a new note arrives, the allocator must steal an existing voice. Voice stealing is what makes a synthesizer feel playable even when exceeding its voice count -- without it, new notes would simply be dropped, creating dead keys and a frustrating playing experience. The allocator provides two stealing behaviors: **hard steal** (the stolen voice is immediately silenced and reassigned to the new note) and **soft steal** (the stolen voice is sent a NoteOff event to begin its release tail, while a separate idle or releasing voice is reassigned to the new note). Soft stealing produces smoother transitions because the stolen note fades out naturally rather than cutting off abruptly, at the cost of one voice temporarily being used for the release tail. The voice stealing strategy (which voice to pick for stealing) is determined by the current allocation mode. Additionally, when looking for a voice to steal, voices that are already in the releasing state are preferred over voices that are still held, because stealing a releasing voice is less audible (its amplitude is already fading). This preference-for-releasing-voices heuristic is standard practice in professional synthesizer implementations and was documented in early Oberheim and Sequential Circuits designs.

**Why this priority**: Voice stealing is essential for a production-quality polyphonic synth. Without it, exceeding the voice count causes dropped notes. The soft-steal option with release tails is what separates a professional-sounding implementation from a toy. P3 because basic allocation (P1) and mode selection (P2) must work first, but stealing is the next critical behavior.

**Independent Test**: Can be tested by filling all voices, sending one more note, and verifying the correct voice is stolen according to the selected mode. For soft steal, verify that the stolen voice receives a NoteOff event before the new voice receives a NoteOn.

**Acceptance Scenarios**:

1. **Given** a VoiceAllocator with 4 voices, all active, and hard-steal mode, **When** a 5th note arrives, **Then** a VoiceEvent of type Steal is returned for the selected voice, followed by a NoteOn event assigning the stolen voice index to the new note.
2. **Given** a VoiceAllocator with 4 voices, all active, and soft-steal mode, **When** a 5th note arrives, **Then** the events returned include: (a) a NoteOff for the selected victim voice (to begin its release tail), and (b) a NoteOn for the new note assigned to that same voice index. The caller is responsible for allowing the voice to complete its release before reassigning.
3. **Given** a VoiceAllocator with 4 voices where 3 are actively held and 1 is in the releasing state, **When** a 5th note arrives, **Then** the releasing voice is preferred for stealing over the held voices, regardless of allocation mode.
4. **Given** a VoiceAllocator with 4 voices where 2 are releasing, **When** a 5th note arrives, **Then** the allocation mode's strategy (oldest, lowest-velocity, etc.) is applied among the releasing voices to select the victim.
5. **Given** a VoiceAllocator where the oldest voice is releasing and a newer voice is also releasing, in Oldest mode, **When** stealing occurs, **Then** the oldest releasing voice is stolen.

---

### User Story 4 - Same-Note Retrigger Behavior (Priority: P4)

A keyboardist plays the same note twice rapidly (either by releasing and re-pressing, or via MIDI from a sequencer sending overlapping same-note events). The allocator must handle this gracefully. When a note-on arrives for a note that is already playing on an existing voice, the allocator re-uses the same voice rather than allocating a new one. This is standard behavior in virtually all polyphonic synthesizers -- playing C4 twice should not consume two voices, because there is no musical reason to have two instances of the same pitch sounding simultaneously (unlike a piano where each press of the same key produces a distinct acoustic event from a separate hammer strike). The retrigger causes the voice's envelope to restart from its current level (as specified in the ADSREnvelope spec, FR-018/FR-020), producing click-free re-articulation. If the same note is already in the releasing state, the allocator reclaims that releasing voice for the new note-on, allowing the new instance to start from the releasing voice's current level.

**Why this priority**: Same-note retrigger is a quality-of-life behavior that prevents wasted voices and matches musician expectations. P4 because basic allocation, mode selection, and stealing must work first, but same-note handling is important for natural playability and voice conservation.

**Independent Test**: Can be tested by sending two note-on events for the same MIDI note and verifying that the second note-on reuses the same voice index, then verifying the voice count has not increased.

**Acceptance Scenarios**:

1. **Given** a VoiceAllocator where voice 2 is playing note 60, **When** a note-on for note 60 arrives, **Then** voice 2 is retriggered (a Steal event followed by a NoteOn event is returned for voice 2), and no additional voice is consumed.
2. **Given** a VoiceAllocator where voice 5 was playing note 60 and is now in the releasing state, **When** a note-on for note 60 arrives, **Then** voice 5 is reclaimed and receives a NoteOn event (starts from current level).
3. **Given** a VoiceAllocator with 4 voices where all 4 play different notes, **When** a note-on for one of the already-playing notes arrives, **Then** the voice playing that note is retriggered and the active voice count remains 4.

---

### User Story 5 - Unison Mode (Priority: P5)

A sound designer wants to create thick, detuned sounds by assigning multiple voices to a single note. When unison mode is enabled with a unison count of N, each note-on event triggers N voices simultaneously rather than one. The VoiceAllocator returns N VoiceEvent entries for a single note-on, each with a different voice index but the same note and velocity. The frequency field in each VoiceEvent reflects a small detune offset to spread the voices across the frequency spectrum (matching the behavior of classic analog polysynths like the Roland Jupiter-8 and Sequential Prophet-5 in unison mode). For a unison count of 3, the center voice plays at the exact note frequency, while the other two are detuned symmetrically above and below. The total polyphony is divided by the unison count: with 8 voices and unison=2, 4 notes can sound simultaneously (each consuming 2 voices). Note-off events release all voices belonging to that note simultaneously.

**Why this priority**: Unison mode is a powerful creative feature that adds richness to the sound, but it is an enhancement on top of the core allocation, mode selection, stealing, and retrigger behaviors. P5 because all core polyphonic mechanisms must work correctly before introducing multi-voice-per-note allocation.

**Independent Test**: Can be tested by setting unison count to N, sending a note-on, and verifying that N VoiceEvents are returned with distinct voice indices but the same note. Verify that note-off releases all N voices.

**Acceptance Scenarios**:

1. **Given** a VoiceAllocator with 8 voices and unison count 2, **When** a note-on for note 60 velocity 100 is sent, **Then** 2 VoiceEvents of type NoteOn are returned, each with a different voice index, note=60, velocity=100, and frequencies symmetrically offset from the base frequency of note 60.
2. **Given** a VoiceAllocator with 8 voices and unison count 2 where note 60 is playing on voices 0 and 1, **When** a note-off for note 60 is sent, **Then** 2 VoiceEvents of type NoteOff are returned (one for voice 0, one for voice 1).
3. **Given** a VoiceAllocator with 8 voices and unison count 4, **When** 3 different notes are sent, **Then** only 2 notes are assigned (consuming 8 voices total), and the 3rd note triggers voice stealing because all voices are occupied (effective polyphony = 8 / 4 = 2).
4. **Given** a VoiceAllocator with unison count 1 (default), **When** a note-on is sent, **Then** exactly 1 VoiceEvent is returned (standard polyphonic behavior).
5. **Given** a VoiceAllocator with 8 voices and unison count 3 (odd), **When** a note-on is sent for note 60, **Then** 3 VoiceEvents are returned: one at the exact frequency of note 60, one detuned slightly above, and one detuned slightly below.

---

### User Story 6 - Configurable Voice Count (Priority: P6)

A synthesizer designer needs to configure the total number of available voices based on the target platform's CPU budget and the desired polyphony. The voice allocator supports a configurable voice count from 1 to 32. At 1 voice, the allocator behaves as a monophonic allocator (every new note steals the single voice). At 32 voices, the allocator provides rich polyphony for complex arrangements. The voice count can be changed at runtime: reducing the voice count while voices are active causes excess voices to be released (sent NoteOff events), while increasing the voice count makes new idle voices available immediately. All internal data structures are pre-allocated for the maximum voice count (32) to avoid runtime allocation.

**Why this priority**: Voice count configuration is important for resource management but is straightforward once all other allocation behaviors are implemented. P6 because the default voice count (8) is sufficient for basic operation, and changing voice count at runtime is a refinement.

**Independent Test**: Can be tested by setting different voice counts and verifying the allocator respects the configured limit, including reducing voice count while voices are active.

**Acceptance Scenarios**:

1. **Given** a VoiceAllocator with voice count set to 4, **When** 5 notes are sent, **Then** the 5th note triggers voice stealing (only 4 voices available).
2. **Given** a VoiceAllocator with 8 active voices and voice count reduced to 4, **When** the voice count change is applied, **Then** 4 excess voices are released (NoteOff events returned for voices 4-7), and subsequent allocation only uses voices 0-3.
3. **Given** a VoiceAllocator with voice count 1, **When** two notes are sent, **Then** the second note steals the first (monophonic behavior).
4. **Given** a VoiceAllocator with voice count increased from 4 to 8, **When** a note-on arrives, **Then** the newly available voices (4-7) can be assigned.

---

### Edge Cases

- What happens when `noteOn()` is called for MIDI note 0 (the lowest valid note)? The allocator processes it normally, computing the correct frequency (~8.18 Hz at A4=440).
- What happens when `noteOn()` is called for MIDI note 127 (the highest valid note)? The allocator processes it normally, computing the correct frequency (~12,543.85 Hz at A4=440).
- What happens when `noteOn()` is called with velocity 0? By MIDI convention, velocity 0 is treated as a note-off. The allocator treats it identically to a `noteOff()` call for that note.
- What happens when `noteOff()` is called for a note that has already received a note-off (double note-off)? No events are returned (empty span). The voice is already releasing or idle.
- What happens when `voiceFinished()` is called for a voice that is not in the releasing state? The call is ignored. Only releasing voices can transition to idle via this method.
- What happens when `voiceFinished()` is called with an out-of-range voice index? The call is ignored.
- What happens when all voices are active and no voice is releasing, and a new note arrives? The allocator steals an active voice according to the current allocation mode.
- What happens when all voices are in the releasing state and a new note arrives? The allocator selects the best releasing voice to steal (according to allocation mode), assigns the new note to it.
- What happens when `setVoiceCount()` is called with a value less than 1? The value is clamped to 1.
- What happens when `setVoiceCount()` is called with a value greater than 32? The value is clamped to 32.
- What happens when unison count exceeds the voice count? The unison count is clamped to the voice count. With 4 voices and unison requested at 8, the effective unison count is 4 (one note consumes all voices).
- What happens when unison count is set to 0? The value is clamped to 1.
- What happens when the same note-on is sent many times rapidly (MIDI machine gun)? Each subsequent note-on for the same note retriggers the same voice(s) without consuming additional voices.
- What happens when pitch bend is applied? The allocator provides a `setPitchBend()` method that updates the frequency field for all active voices. Pitch bend is applied globally (per-note pitch bend / MPE is not in scope).
- What happens when `reset()` is called while voices are active? All voices are immediately returned to idle. No events are generated. Internal state is cleared.

## Requirements *(mandatory)*

### Functional Requirements

**VoiceEvent Structure**

- **FR-001**: The library MUST provide a `VoiceEvent` struct in the `Krate::DSP` namespace with the following fields: a `Type` enumeration (NoteOn, NoteOff, Steal), a voice index (0 to maxVoices-1), a MIDI note number (0-127), a velocity value (0-127), and a pre-computed frequency in Hz. The struct MUST be a simple aggregate with no user-declared constructors.

**VoiceAllocator Class (Layer 3 -- `systems/voice_allocator.h`)**

- **FR-002**: The library MUST provide a `VoiceAllocator` class at `dsp/include/krate/dsp/systems/voice_allocator.h` in the `Krate::DSP` namespace. The class MUST pre-allocate all internal data structures for the maximum voice count (32) at construction. No heap allocation occurs after construction.

**VoiceAllocator -- Constants**

- **FR-003**: The class MUST declare `static constexpr size_t kMaxVoices = 32` as the maximum number of simultaneous voices.
- **FR-004**: The class MUST declare `static constexpr size_t kMaxUnisonCount = 8` as the maximum number of unison voices per note.
- **FR-005**: The class MUST declare `static constexpr size_t kMaxEvents = kMaxVoices * 2` as the maximum number of VoiceEvents that can be returned from a single noteOn or noteOff call (worst case: unison count voices stolen + unison count voices assigned).

**AllocationMode Enumeration**

- **FR-006**: The library MUST provide an `AllocationMode` enumeration in the `Krate::DSP` namespace with the following values: `RoundRobin`, `Oldest`, `LowestVelocity`, `HighestNote`.

**StealMode Enumeration**

- **FR-007**: The library MUST provide a `StealMode` enumeration in the `Krate::DSP` namespace with the following values: `Hard` (stolen voice is immediately reassigned; a Steal event is generated), `Soft` (stolen voice receives a NoteOff to begin release; the voice is then reassigned to the new note with a NoteOn).

**Voice State Tracking**

- **FR-008**: The allocator MUST track each voice's state as one of: `Idle` (available for assignment), `Active` (playing a held note), or `Releasing` (note-off received, envelope completing release tail). The state is updated internally based on noteOn, noteOff, and voiceFinished calls.
- **FR-009**: The allocator MUST track for each voice: the MIDI note number, the velocity, the frequency, and the timestamp (monotonically increasing counter incremented on each noteOn call) of when the voice was last triggered.

**Core Note-On Allocation (P1)**

- **FR-010**: When `noteOn(note, velocity)` is called and at least one idle voice is available, the allocator MUST assign an idle voice to the note and return a span containing a single VoiceEvent of type NoteOn (or multiple events if unison is enabled).
- **FR-011**: The frequency field of each VoiceEvent MUST be computed using 12-TET tuning (standard equal temperament) from the MIDI note number, incorporating the current pitch bend offset. The formula is: `frequency = midiNoteToFrequency(note) * semitonesToRatio(pitchBendSemitones)`.
- **FR-012**: When `noteOn()` is called for a note that is already playing on an existing voice (same-note retrigger), the allocator MUST reuse the existing voice rather than allocating a new one. The returned events indicate a Steal for the existing voice followed by a NoteOn on the same voice index with the new velocity and frequency.

**Core Note-Off (P1)**

- **FR-013**: When `noteOff(note)` is called, the allocator MUST find the voice playing that note, transition it from Active to Releasing state, and return a span containing a VoiceEvent of type NoteOff for that voice. If multiple voices play the same note (unison), all are transitioned.
- **FR-014**: When `noteOff()` is called for a note that is not currently active, the allocator MUST return an empty span (no events).
- **FR-015**: When `noteOn()` is called with velocity 0, it MUST be treated identically to `noteOff()` for that note, following standard MIDI convention.

**Voice Lifecycle (P1)**

- **FR-016**: The `voiceFinished(voiceIndex)` method MUST transition a voice from Releasing to Idle state, making it available for new note assignment. Calls for voices not in the Releasing state or with out-of-range indices MUST be ignored.
- **FR-017**: The `getActiveVoiceCount()` method MUST return the number of voices in either Active or Releasing state.
- **FR-018**: The `isVoiceActive(voiceIndex)` method MUST return true if the voice is in Active or Releasing state.

**Allocation Mode Strategies (P2)**

- **FR-019**: In **RoundRobin** mode, idle voices MUST be selected by cycling through voice indices sequentially (0, 1, 2, ..., N-1, 0, 1, ...). When stealing is required, the next voice in the cycle is stolen regardless of its age, velocity, or note. The round-robin counter advances with each allocation and wraps at the voice count.
- **FR-020**: In **Oldest** mode, when multiple idle voices are available, the one that has been idle the longest MUST be selected (FIFO from idle pool). When stealing is required, the voice with the lowest timestamp (triggered earliest) MUST be stolen.
- **FR-021**: In **LowestVelocity** mode, when multiple idle voices are available, any idle voice may be selected. When stealing is required, the voice with the lowest velocity value MUST be stolen. Ties are broken by age (oldest first).
- **FR-022**: In **HighestNote** mode, when multiple idle voices are available, any idle voice may be selected. When stealing is required, the voice playing the highest MIDI note number MUST be stolen. Ties are broken by age (oldest first).
- **FR-023**: The allocation mode MUST be changeable at any time via `setAllocationMode()` without disrupting currently active voices.

**Voice Stealing (P3)**

- **FR-024**: When all voices are occupied and a new note arrives, the allocator MUST select a voice to steal according to the current allocation mode strategy (FR-019 through FR-022).
- **FR-025**: When selecting a voice to steal, the allocator MUST prefer voices in the Releasing state over voices in the Active state. Among releasing voices, the allocation mode's strategy applies. Only if no releasing voices exist does the allocator steal an Active voice.
- **FR-026**: In Hard steal mode (default), the stolen voice MUST receive a Steal event type in the returned VoiceEvents, immediately followed by a NoteOn event for the same voice index with the new note data.
- **FR-027**: In Soft steal mode, the stolen voice MUST receive a NoteOff event type (to begin its release tail), followed by a NoteOn event for the same voice index with the new note data. The caller is responsible for managing the crossfade between the releasing old note and the new note on that voice.
- **FR-028**: The steal mode MUST be configurable via `setStealMode()` and changeable at any time.

**Unison Mode (P5)**

- **FR-029**: The allocator MUST support a configurable unison count (1 to kMaxUnisonCount). When unison count is N, each `noteOn()` call allocates N voices simultaneously and returns N VoiceEvent entries, each with a different voice index but the same note and velocity.
- **FR-030**: In unison mode, the frequency of each voice MUST be offset symmetrically around the base note frequency. For unison count N: with odd N, one voice plays at the exact frequency and (N-1)/2 pairs are detuned above and below; with even N, N/2 pairs are detuned symmetrically (no center voice). The detune spread is determined by a configurable unison detune parameter (0.0 to 1.0) mapped to a maximum spread of 50 cents (half a semitone in each direction).
- **FR-031**: `noteOff()` for a note in unison mode MUST release all voices belonging to that note group, returning N NoteOff events.
- **FR-032**: The effective polyphony in unison mode MUST be `voiceCount / unisonCount` (integer division). For example, 8 voices with unison=2 allows 4 simultaneous notes.
- **FR-033**: When unison count is changed while voices are active, voices currently playing are not affected. The new unison count applies to subsequent note-on events.
- **FR-034**: The `setUnisonDetune(float amount)` method MUST accept values from 0.0 (no detune) to 1.0 (maximum spread of +/-50 cents). NaN and infinity values MUST be ignored.

**Configurable Voice Count (P6)**

- **FR-035**: The `setVoiceCount(size_t count)` method MUST accept values from 1 to kMaxVoices (32), clamping out-of-range values. Reducing voice count while voices are active MUST release excess voices by returning NoteOff events for any active voice whose index is at or above the new count.
- **FR-036**: Increasing voice count MUST make newly available voices immediately idle and assignable.

**Pitch Bend Support**

- **FR-037**: The `setPitchBend(float semitones)` method MUST update the pitch bend offset applied to all frequency calculations. The pitch bend range is determined by the caller (typically +/-2 semitones for standard MIDI pitch bend). The method MUST recalculate and update the frequency for all currently active and releasing voices.

**State Query Methods**

- **FR-038**: The `getVoiceNote(size_t voiceIndex)` method MUST return the MIDI note number currently assigned to the specified voice, or -1 if the voice is idle.
- **FR-039**: The `getVoiceState(size_t voiceIndex)` method MUST return the current state (Idle, Active, Releasing) of the specified voice.

**Reset and Lifecycle**

- **FR-040**: The `reset()` method MUST return all voices to Idle state, clear all note tracking, reset the round-robin counter, and reset the timestamp counter. No events are generated.
- **FR-041**: The `setTuningReference(float a4Hz)` method MUST set the reference frequency for A4 (default 440 Hz) used in frequency calculations. Changing the tuning reference MUST recalculate frequencies for all active voices.

**Real-Time Safety**

- **FR-042**: All methods (`noteOn()`, `noteOff()`, `voiceFinished()`, all setters, all queries) MUST be real-time safe: no memory allocations, no locks, no exceptions, no I/O. The pre-allocated event buffer and fixed-size voice arrays ensure zero-allocation operation.
- **FR-043**: All methods MUST be marked `noexcept`.

**Layer Compliance**

- **FR-044**: The voice allocator MUST reside at Layer 3 (systems) and depend only on Layer 0 (core utilities) and the standard library. It does NOT depend on Layer 1 or Layer 2 components (no envelopes, no oscillators, no filters).
- **FR-045**: The voice allocator class MUST live in the `Krate::DSP` namespace.

### Key Entities

- **VoiceEvent**: A lightweight event descriptor returned by the allocator to instruct the caller which voice to start, stop, or steal. Contains the event type, voice index, MIDI note, velocity, and pre-computed frequency. The allocator does not own voices -- it only produces instructions for the caller to act on.
- **VoiceAllocator**: The central voice management system. Maintains a pool of voice slots with their state (Idle, Active, Releasing), note assignments, velocities, timestamps, and frequencies. Implements multiple allocation strategies and voice stealing policies. Does NOT contain any DSP processing code -- it is purely a note-to-voice routing engine.
- **VoiceState**: Enumeration of the three states a voice slot can be in: Idle (available for assignment), Active (playing a held note, gate is on), Releasing (note-off received, envelope completing release, gate is off). Transitions: Idle -> Active (on noteOn), Active -> Releasing (on noteOff or steal), Releasing -> Idle (on voiceFinished).
- **AllocationMode**: Enumeration selecting the strategy for voice assignment and stealing. Determines which voice is chosen from the available pool (for idle voices) and which voice is stolen when the pool is full.
- **StealMode**: Enumeration selecting the stealing behavior: Hard (immediate reassignment with Steal event) or Soft (graceful release with NoteOff before reassignment with NoteOn).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All note-on events for distinct notes produce unique voice assignments (no two notes share a voice) until the voice pool is exhausted, verified over sequences of 1 to 32 notes with 32 voices available.
- **SC-002**: All note-off events correctly identify and release the voice playing the specified note. After note-off followed by voiceFinished, the voice returns to the idle pool and can be reassigned.
- **SC-003**: Voice stealing in each allocation mode selects the correct victim: RoundRobin selects the next voice in cycle, Oldest selects the voice with the earliest timestamp, LowestVelocity selects the voice with the lowest velocity, HighestNote selects the voice with the highest MIDI note number.
- **SC-004**: The preference-for-releasing-voices heuristic is measurable: when both Active and Releasing voices exist, the allocator always steals a Releasing voice first, verified across all four allocation modes.
- **SC-005**: Same-note retrigger reuses the existing voice 100% of the time, verified by sending the same note twice and confirming the voice index is identical and active voice count does not increase.
- **SC-006**: Unison mode allocates exactly N voices per note (where N is the unison count), and noteOff releases all N voices, verified for unison counts 1 through 8.
- **SC-007**: Frequency computation accuracy: for all 128 MIDI notes, the computed frequency matches the 12-TET formula `440 * 2^((note-69)/12)` to within 0.01 Hz at A4=440Hz tuning reference.
- **SC-008**: A single `noteOn()` call (including voice selection, stealing logic, and event generation) completes in under 1 microsecond on average for 32 voices (measured in Release build). This ensures negligible overhead compared to audio processing.
- **SC-009**: Memory footprint of a VoiceAllocator instance MUST NOT exceed 4096 bytes (4 KB), including all pre-allocated event buffers, voice state arrays, and internal bookkeeping for 32 voices.
- **SC-010**: All 45 functional requirements have corresponding passing tests.
- **SC-011**: Velocity-0 note-on events are treated as note-off, verified by sending note-on with velocity 0 and confirming the voice enters Releasing state.
- **SC-012**: Pitch bend updates propagate to all active voice frequencies immediately, verified by checking that after setPitchBend(), getVoiceNote() returns the same note but the voice's frequency has been updated by the correct semitone offset.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The VoiceAllocator is a pure routing/management component at Layer 3. It does NOT own, create, or process any DSP objects (oscillators, envelopes, filters). It produces VoiceEvent instructions that a higher-level system (e.g., future PolyphonicSynthEngine from Phase 3.2) acts upon by starting, stopping, or stealing actual voice DSP instances.
- The allocator operates synchronously on the audio thread. Each `noteOn()` / `noteOff()` call returns immediately with events. There is no background processing or deferred event delivery.
- The maximum voice count of 32 is fixed at compile time. All internal arrays are sized to kMaxVoices=32. The `setVoiceCount()` method only adjusts the effective working range, not the allocated capacity.
- The maximum unison count of 8 is sufficient for all practical unison scenarios. Most commercial synthesizers cap unison at 7-8 voices (e.g., Serum uses up to 16, Massive up to 8, the Roland JP-8000 uses 7 for its supersaw).
- Frequency computation uses 12-TET (twelve-tone equal temperament) tuning exclusively. Microtuning / Scala file support is a future Phase 4 enhancement (see synth-roadmap.md section 4.4) and is NOT in scope for this spec.
- Pitch bend is global (applied to all voices equally). Per-note pitch bend (MPE) is a future Phase 4 enhancement (see synth-roadmap.md section 4.1).
- The unison detune in VoiceAllocator provides simple symmetric linear spacing of voice frequencies. This is distinct from the non-linear detune curve in UnisonEngine (spec 020), which uses a power curve for supersaw-specific timbral shaping. The allocator's unison is simpler because it is a voice management feature (which voices play which frequencies), not an oscillator-level DSP feature.
- The event buffer returned by `noteOn()` and `noteOff()` is internal to the allocator. The returned `std::span` is valid until the next call to `noteOn()`, `noteOff()`, or `setVoiceCount()`. The caller must process events before the next call.
- Block-level processing is NOT required. The allocator processes discrete note events, not continuous audio samples. It has no `prepare(sampleRate)` or `processBlock()` method.
- The voice timestamp is a monotonically increasing counter (not wall-clock time). It increments on each noteOn call and is used only for relative age comparisons (Oldest mode). Overflow is not a practical concern at 64-bit precision.
- Voice stealing tie-breaking (when two voices have equal priority under the current mode) uses the older voice (lower timestamp) as the tiebreaker in all modes.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `midiNoteToFrequency()` | `dsp/include/krate/dsp/core/midi_utils.h` | MUST reuse. Converts MIDI note number to frequency using 12-TET. Used for computing VoiceEvent frequency field. |
| `semitonesToRatio()` | `dsp/include/krate/dsp/core/pitch_utils.h` | MUST reuse. Converts semitone offset to frequency ratio. Used for pitch bend and unison detune calculations. |
| `kA4FrequencyHz` | `dsp/include/krate/dsp/core/midi_utils.h` | MUST reuse. Default A4 reference frequency (440 Hz). |
| `velocityToGain()` | `dsp/include/krate/dsp/core/midi_utils.h` | Reference only. The allocator passes raw MIDI velocity (0-127) through to VoiceEvents; conversion to gain is the caller's responsibility. |
| `detail::isNaN()` | `dsp/include/krate/dsp/core/db_utils.h` | MUST reuse. Bit-manipulation NaN detection for parameter setter guards (pitch bend, detune, tuning reference). |
| ADSREnvelope | `dsp/include/krate/dsp/primitives/adsr_envelope.h` | NOT a dependency. The allocator does not own envelopes. However, the allocator's voice state model (Active -> Releasing -> Idle) is designed to mirror the ADSREnvelope's lifecycle (gate-on -> gate-off/release -> idle). The `voiceFinished()` callback corresponds to `ADSREnvelope::isActive() == false`. |
| UnisonEngine | `dsp/include/krate/dsp/systems/unison_engine.h` | NOT a dependency. The UnisonEngine handles oscillator-level unison detuning for a single voice. The VoiceAllocator's unison mode operates at the voice management level (assigning multiple voice slots to one note). They serve complementary purposes and can be used together. |
| FMVoice | `dsp/include/krate/dsp/systems/fm_voice.h` | Reference for Layer 3 system patterns. No code sharing. |
| StereoOutput | `dsp/include/krate/dsp/core/stereo_output.h` | NOT used. The allocator does not produce audio. |

**Initial codebase search for key terms:**

```bash
grep -r "class VoiceAllocator" dsp/ plugins/
grep -r "VoiceEvent" dsp/ plugins/
grep -r "VoiceState" dsp/ plugins/
grep -r "AllocationMode" dsp/ plugins/
grep -r "StealMode" dsp/ plugins/
```

**Search Results Summary**: No existing VoiceAllocator, VoiceEvent, VoiceState, AllocationMode, or StealMode types found anywhere in the codebase. All names are unique and safe from ODR conflicts.

### Forward Reusability Consideration

*This is a Layer 3 system. Consider what new code might be reusable by sibling features.*

**Downstream consumers (from synth-roadmap.md):**
- Phase 2.2: Mono/Legato Handler -- operates alongside VoiceAllocator (mono mode vs. poly mode), but is a separate component with different responsibilities (note priority stack, portamento). The Mono/Legato Handler handles single-voice note selection; VoiceAllocator handles multi-voice pool management.
- Phase 3.1: Basic Synth Voice -- consumes VoiceEvents produced by VoiceAllocator to start/stop voice DSP instances.
- Phase 3.2: Polyphonic Synth Engine -- composes VoiceAllocator + pool of SynthVoice instances. This is the primary consumer.

**Sibling features at same layer (Layer 3):**
- UnisonEngine (spec 020) -- complementary voice-level detuning (oscillator DSP), not competing with VoiceAllocator's note-level unison (voice management)
- FMVoice (spec 022) -- a single voice system that would be managed by VoiceAllocator in a polyphonic context
- VectorMixer (spec 031) -- unrelated (audio signal mixing)

**Potential shared components** (preliminary, refined in plan.md):
- The `VoiceEvent` struct could be reused by the future Mono/Legato Handler (Phase 2.2) for consistent event signaling, potentially extracted to a shared header if needed.
- The `VoiceState` enum (Idle, Active, Releasing) is a generic concept that could be shared with any voice-consuming system. Consider extraction to a shared `voice_types.h` during planning.

## Implementation Verification *(mandatory at completion)*

<!--
  CRITICAL: This section MUST be completed when claiming spec completion.
  Constitution Principle XVI: Honest Completion requires explicit verification
  of ALL requirements before claiming "done".

  DO NOT fill this table from memory or assumptions. Each row requires you to
  re-read the actual implementation code and actual test output RIGHT NOW,
  then record what you found with specific file paths, line numbers, and
  measured values. Generic evidence like "implemented" or "test passes" is
  NOT acceptable -- it must be verifiable by a human reader.

  This section is EMPTY during specification phase and filled during
  implementation phase when /speckit.implement completes.
-->

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
| FR-037 | | |
| FR-038 | | |
| FR-039 | | |
| FR-040 | | |
| FR-041 | | |
| FR-042 | | |
| FR-043 | | |
| FR-044 | | |
| FR-045 | | |
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
| SC-012 | | |

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
