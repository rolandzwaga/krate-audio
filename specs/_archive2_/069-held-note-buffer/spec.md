# Feature Specification: HeldNoteBuffer & NoteSelector

**Feature Branch**: `069-held-note-buffer`
**Plugin**: KrateDSP (Layer 1 primitives)
**Created**: 2026-02-20
**Status**: Draft
**Input**: User description: "Phase 1 of the Ruinae arpeggiator roadmap. Two DSP Layer 1 (primitives) components: (1) HeldNoteBuffer -- fixed-capacity (32), heap-free buffer tracking currently held MIDI notes with pitch-sorted and insertion-ordered views; (2) NoteSelector -- stateful selector implementing all 10 arp modes (Up, Down, UpDown, DownUp, Converge, Diverge, Random, Walk, AsPlayed, Chord) with octave range (1-4) and two octave modes (Sequential, Interleaved)."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Track Held Notes for Arpeggiator (Priority: P1)

As an arpeggiator engine, the system needs a real-time-safe buffer that tracks which MIDI notes are currently held down. When a key is pressed, the note and velocity are stored. When a key is released, the note is removed. The buffer must provide access to the held notes sorted by pitch (ascending) for directional arp modes, and in chronological insertion order for "As Played" mode.

**Why this priority**: Without accurate held-note tracking, no arp mode can function. This is the foundational data structure upon which all note selection depends.

**Independent Test**: Can be fully tested by adding and removing notes and verifying the buffer's contents, size, ordering, and emptiness without requiring any other arp component.

**Acceptance Scenarios**:

1. **Given** an empty buffer, **When** noteOn(60, 100), noteOn(64, 90), noteOn(67, 80) are called, **Then** size() returns 3, byPitch() returns notes [60, 64, 67] in ascending order, and byInsertOrder() returns notes in the same order [60, 64, 67] reflecting chronological insertion.
2. **Given** a buffer holding notes [60, 64, 67], **When** noteOff(64) is called, **Then** size() returns 2, byPitch() returns [60, 67], and byInsertOrder() returns [60, 67] preserving the relative insertion order.
3. **Given** a buffer holding note 60 with velocity 100, **When** noteOn(60, 120) is called, **Then** size() remains 1 (no duplicate), and the stored velocity is updated to 120.
4. **Given** a buffer at maximum capacity (32 notes), **When** an additional noteOn is received, **Then** the note is silently ignored and the existing 32 notes remain uncorrupted.

---

### User Story 2 - Select Notes in Directional Arp Modes (Priority: P1)

As an arpeggiator engine, the system needs a NoteSelector that, given a set of held notes, produces the next note to play according to the selected arp mode. The directional modes (Up, Down, UpDown, DownUp) traverse the pitch-sorted note list in predictable patterns, automatically wrapping at boundaries and advancing through the configured octave range.

**Why this priority**: Directional modes (Up, Down, UpDown, DownUp) are the most fundamental and universally expected arp patterns. They validate the core selection and octave-shifting logic.

**Independent Test**: Can be fully tested by populating a HeldNoteBuffer with known notes, then calling advance() repeatedly and verifying the returned note sequence matches the expected traversal pattern for each mode.

**Acceptance Scenarios**:

1. **Given** held notes [C3, E3, G3] and mode Up with octave range 1, **When** advance() is called 6 times, **Then** the notes cycle: C3, E3, G3, C3, E3, G3 (repeating ascending pattern).
2. **Given** held notes [C3, E3, G3] and mode Down with octave range 1, **When** advance() is called 6 times, **Then** the notes cycle: G3, E3, C3, G3, E3, C3 (repeating descending pattern).
3. **Given** held notes [C3, E3, G3] and mode UpDown with octave range 1, **When** advance() is called through one full cycle, **Then** the note sequence is C3, E3, G3, E3, C3, E3, G3, ... (ascending then descending, reversing direction at endpoints without repeating the boundary notes).
4. **Given** held notes [C3, E3, G3] and mode DownUp with octave range 1, **When** advance() is called through one full cycle, **Then** the note sequence is G3, E3, C3, E3, G3, ... (descending then ascending, reversing direction at endpoints without repeating the boundary notes).
5. **Given** held notes [C3, E3, G3] and mode Up with octave range 2 and Sequential octave mode, **When** advance() is called 6 times, **Then** the notes are C3, E3, G3, C4, E4, G4 (all notes in octave 0, then all notes in octave +1).
6. **Given** held notes [C3, E3, G3] and mode Up with octave range 2 and Interleaved octave mode, **When** advance() is called 6 times, **Then** the notes are C3, C4, E3, E4, G3, G4 (each note followed by its octave transpositions before moving to the next note).

---

### User Story 3 - Select Notes in Converge/Diverge Modes (Priority: P2)

As an arpeggiator engine, the NoteSelector must support Converge and Diverge modes that traverse the held notes from the outside edges inward (Converge) or from the center outward (Diverge), creating distinctive melodic movement.

**Why this priority**: Converge/Diverge are the most musically distinctive advanced modes and are expected in professional arpeggiators (Ableton Live, hardware synths).

**Independent Test**: Can be tested independently by calling advance() on a NoteSelector with known held notes and verifying the alternating outside-in or inside-out note pattern.

**Acceptance Scenarios**:

1. **Given** held notes [C3, D3, E3, G3] (4 notes) and mode Converge, **When** advance() is called 4 times, **Then** the notes are C3, G3, D3, E3 (lowest, highest, second-lowest, second-highest -- converging inward).
2. **Given** held notes [C3, D3, E3, G3] (4 notes) and mode Diverge, **When** advance() is called 4 times, **Then** the notes are D3, E3, C3, G3 (inner notes first, then expanding outward).
3. **Given** held notes [C3, D3, E3] (3 notes, odd count) and mode Converge, **When** advance() is called 3 times, **Then** the notes are C3, E3, D3 (lowest, highest, middle).
4. **Given** held notes [C3, D3, E3] (3 notes, odd count) and mode Diverge, **When** advance() is called 3 times, **Then** the notes are D3, C3, E3 (middle, then expanding outward).

---

### User Story 4 - Select Notes in Random and Walk Modes (Priority: P2)

As an arpeggiator engine, the NoteSelector must support Random mode (pick any held note with equal probability) and Walk mode (move randomly by +/-1 step within the pitch-sorted list, clamped to boundaries).

**Why this priority**: Random and Walk modes add controlled unpredictability. Walk mode is particularly valuable for generative music as it creates coherent but non-repeating melodic movement.

**Independent Test**: Can be tested by running many iterations of advance() and verifying statistical properties: Random mode selects each note with approximately equal frequency; Walk mode never jumps more than one step and stays within the note range.

**Acceptance Scenarios**:

1. **Given** held notes [C3, E3, G3] and mode Random, **When** advance() is called 300+ times, **Then** each of the 3 notes is selected approximately 33% of the time (within reasonable statistical tolerance).
2. **Given** held notes [C3, E3, G3, B3] and mode Walk starting at index 0, **When** advance() is called, **Then** the next index is 0 (clamped from -1) or 1 (step forward), each with 50% probability; it never jumps to index 2 or 3.
3. **Given** held notes [C3, E3, G3] and mode Walk, **When** advance() is called 100+ times, **Then** the walk index never goes below 0 or above size()-1 (clamped to bounds).

---

### User Story 5 - Select Notes in AsPlayed and Chord Modes (Priority: P2)

As an arpeggiator engine, the NoteSelector must support AsPlayed mode (traverse notes in the chronological order they were pressed) and Chord mode (return all held notes simultaneously on each advance).

**Why this priority**: AsPlayed preserves the musician's intended note order, which is essential for melodic patterns. Chord mode enables rhythmic retriggering of the full chord, which is a staple of trance and EDM.

**Independent Test**: Can be tested by populating a HeldNoteBuffer in a specific insertion order and verifying that AsPlayed follows that order, and that Chord returns all notes with count matching the held note count.

**Acceptance Scenarios**:

1. **Given** notes pressed in order G3, C3, E3 (not pitch order), and mode AsPlayed, **When** advance() is called 3 times, **Then** the notes are G3, C3, E3 (insertion order, not pitch order).
2. **Given** held notes [C3, E3, G3] and mode Chord, **When** advance() is called, **Then** the result contains all 3 notes simultaneously (count == 3) with their respective velocities.
3. **Given** held notes [C3, E3, G3] and mode Chord, **When** advance() is called multiple times, **Then** each call returns the same set of all held notes.

---

### User Story 6 - Octave Range Expansion with Sequential and Interleaved Modes (Priority: P2)

As an arpeggiator engine, the NoteSelector must expand the played note pattern across multiple octaves (1-4 octave range). Two octave modes control how octave transpositions are ordered: Sequential plays the full pattern in each octave before moving to the next; Interleaved plays each note at all its octave transpositions before moving to the next note in the pattern.

**Why this priority**: Octave range expansion is a core arp feature that multiplies the melodic range. The two ordering modes give the user distinct textural options.

**Independent Test**: Can be tested by checking that advance() produces the correct pitch values (original note + 12 * octaveOffset) in the correct order for both Sequential and Interleaved modes across multiple octave ranges.

**Acceptance Scenarios**:

1. **Given** held notes [C3, E3] and mode Up with octave range 3 and Sequential mode, **When** advance() is called 6 times, **Then** the notes are C3, E3, C4, E4, C5, E5 (complete pattern at each octave before advancing).
2. **Given** held notes [C3, E3] and mode Up with octave range 3 and Interleaved mode, **When** advance() is called 6 times, **Then** the notes are C3, C4, C5, E3, E4, E5 (each note's octave transpositions before next note).
3. **Given** held notes [C3, E3, G3] and mode Down with octave range 2 and Sequential mode, **When** advance() is called 6 times, **Then** the notes are G4, E4, C4, G3, E3, C3 (descending through upper octave first, then lower).
4. **Given** octave range 1, **When** any mode's advance() is called, **Then** no octave transposition is applied -- notes play at their original pitch only.
5. **Given** a note at MIDI pitch 120 with octave range 4 and Sequential mode, **When** the octave offset would produce a note above 127, **Then** the octave offset wraps or the note is clamped to remain within valid MIDI range (0-127).

---

### User Story 7 - Pattern Reset on Retrigger (Priority: P3)

As an arpeggiator engine, the NoteSelector must support resetting to the beginning of its pattern when a retrigger event occurs (e.g., when a new note is pressed). This ensures the arp restarts predictably.

**Why this priority**: Retrigger behavior is important for musical feel but depends on the external ArpeggiatorCore (Phase 2) to decide when to call reset(). The NoteSelector just needs to support the reset() operation correctly.

**Independent Test**: Can be tested by advancing a NoteSelector partway through a pattern, calling reset(), and verifying the next advance() returns the first note in the pattern.

**Acceptance Scenarios**:

1. **Given** held notes [C3, E3, G3] and mode Up, advanced to index 2 (currently at G3), **When** reset() is called, **Then** the next advance() returns C3 (start of pattern).
2. **Given** mode UpDown at the descending phase (e.g., at E3 going down), **When** reset() is called, **Then** the direction resets to ascending and the next advance() returns C3.
3. **Given** mode Walk at any arbitrary position, **When** reset() is called, **Then** the walk position resets to index 0.

---

### Edge Cases

- What happens when advance() is called on an empty HeldNoteBuffer? The NoteSelector must return a result with count == 0 (no notes to play) without crashing.
- What happens when a note is removed from the buffer mid-pattern while the NoteSelector's current index points beyond the new size? The index is clamped to `min(index, newSize - 1)` -- it stays as close as possible to the current position without wrapping or resetting.
- What happens when a single note is held? All directional modes must produce that single note repeatedly. UpDown and DownUp must not bounce indefinitely. Converge/Diverge return the single note. Walk stays at index 0.
- What happens when the same noteOn is received twice (same pitch, different velocity)? The buffer updates the velocity of the existing entry without adding a duplicate.
- What happens when noteOff is received for a note that is not in the buffer? The operation is silently ignored; no crash, no corruption.
- What happens when 32 notes are held and a 33rd noteOn arrives? The 33rd note is silently dropped; the buffer remains at capacity with all 32 notes intact.
- How does the insertion order counter handle wrap-around? A 16-bit counter (uint16_t) wraps at 65535. Over a typical session this is unlikely to be reached. If it does wrap, the ordering becomes ambiguous for notes added before and after the wrap, which is acceptable for this use case.
- What happens with UpDown/DownUp when only 2 notes are held? With notes [C3, G3], UpDown produces C3, G3, C3, G3 (simple alternation: [A, B, A, B, ...], because with only 2 notes the no-endpoint-repeat rule produces the same alternation as ping-pong).

## Clarifications

### Session 2026-02-20

- Q: What is the probability distribution for Walk mode step direction (-1, stay, +1)? → A: Equal probability -1/+1 only (50/50, no stay option).
- Q: How does Chord mode interact with octave range > 1? → A: Chord mode ignores octave range; always returns all held notes at original pitch only.
- Q: How is HeldNoteBuffer coupled to NoteSelector -- stored reference at construction or passed per call? → A: advance() takes const HeldNoteBuffer& as a parameter; NoteSelector holds no reference to the buffer.
- Q: After Converge/Diverge completes one full cycle, does it restart from the beginning or reverse into the opposite mode? → A: Pure wrap -- cycle restarts from the beginning with no reversal.
- Q: When a note is removed mid-pattern and the current index is out of range, should the index clamp or wrap? → A: Clamp -- index = min(index, newSize - 1), staying as close as possible to the current position.

## Requirements *(mandatory)*

### Functional Requirements

**HeldNoteBuffer**:

- **FR-001**: The buffer MUST store MIDI notes with a fixed maximum capacity of 32 entries, using no heap allocation for any operation (noteOn, noteOff, clear, size queries, sorted view access).
- **FR-002**: Each stored note MUST consist of: MIDI note number (0-127), velocity (1-127), and insertion order (monotonically increasing counter for chronological ordering).
- **FR-003**: The noteOn operation MUST add a new note to the buffer if the pitch is not already present, or update the velocity of an existing note with the same pitch without creating a duplicate entry.
- **FR-004**: The noteOff operation MUST remove the note with the specified pitch from the buffer. If the pitch is not found, the operation MUST be silently ignored.
- **FR-005**: The buffer MUST provide a pitch-sorted view (ascending by MIDI note number) of all currently held notes, accessible without heap allocation.
- **FR-006**: The buffer MUST provide an insertion-ordered view (chronological order of noteOn calls) of all currently held notes, accessible without heap allocation.
- **FR-007**: The clear operation MUST remove all notes and reset the buffer to an empty state, including resetting the insertion order counter to 0.
- **FR-008**: When the buffer is at maximum capacity (32 notes), additional noteOn calls for new pitches MUST be silently ignored without corrupting existing data.
- **FR-009**: The buffer MUST correctly handle rapid sequences of interleaved noteOn/noteOff events without data corruption (no stale entries, no index-out-of-bounds).

**NoteSelector**:

- **FR-010**: The NoteSelector MUST implement 10 arp modes: Up, Down, UpDown, DownUp, Converge, Diverge, Random, Walk, AsPlayed, and Chord.
- **FR-011**: Up mode MUST traverse the pitch-sorted notes from lowest to highest, wrapping back to the lowest after the highest.
- **FR-012**: Down mode MUST traverse the pitch-sorted notes from highest to lowest, wrapping back to the highest after the lowest.
- **FR-013**: UpDown mode MUST traverse ascending then descending, reversing direction at the endpoints. The endpoint notes MUST NOT be repeated at the reversal point (e.g., [C, E, G, E, C, E, ...] not [C, E, G, G, E, C, C, E, ...]).
- **FR-014**: DownUp mode MUST traverse descending then ascending, reversing direction at the endpoints without repeating boundary notes.
- **FR-015**: Converge mode MUST alternate between selecting from the outside edges inward: lowest, highest, second-lowest, second-highest, and so on until the center is reached, then wrap back to the beginning of the same inward sequence (lowest, highest, ...) without reversing into an outward pass.
- **FR-016**: Diverge mode MUST alternate between selecting from the center outward: center note(s) first, then expanding to the outer edges, then wrap back to the beginning of the same outward sequence (center first, ...) without reversing into an inward pass.
- **FR-017**: Random mode MUST select a random note from the held notes with approximately equal probability for each note.
- **FR-018**: Walk mode MUST move the current index by exactly -1 or +1 per advance(), each with 50% probability (no stay option), clamped to the valid index range [0, size-1].
- **FR-019**: AsPlayed mode MUST traverse notes in their insertion order (the order noteOn was called), not pitch order.
- **FR-020**: Chord mode MUST return all currently held notes simultaneously on each advance() call, with the result count equal to the number of held notes. Chord mode MUST ignore the octave range and OctaveMode settings; notes are always returned at their original pitch with no transposition applied.
- **FR-021**: The NoteSelector MUST support an octave range parameter from 1 to 4, where 1 means no octave transposition and N means the pattern spans N octaves (original pitch through original pitch + 12*(N-1) semitones). Octave range applies to all modes except Chord (see FR-020).
- **FR-022**: Sequential octave mode MUST play the complete note pattern at octave 0, then the complete pattern at octave +1, and so on through the configured octave range, before cycling.
- **FR-023**: Interleaved octave mode MUST play each individual note at all its octave transpositions (octave 0, +1, +2, ...) before advancing to the next note in the pattern.
- **FR-024**: The advance() method MUST accept a `const HeldNoteBuffer&` parameter and return an ArpNoteResult containing: the MIDI note number(s) with octave offset applied, the velocity/velocities, and a count (1 for most modes, N for Chord mode). NoteSelector MUST NOT store a reference or pointer to any HeldNoteBuffer internally. The result MUST use a fixed-capacity array (no heap allocation).
- **FR-025**: The reset() method MUST return the NoteSelector to the beginning of its current pattern, resetting the internal index, direction state, and octave position.
- **FR-026**: When advance() is called with an empty HeldNoteBuffer (passed as a const reference), the NoteSelector MUST return a result with count == 0 without any crash or undefined behavior.
- **FR-027**: When notes are added or removed from the HeldNoteBuffer between advance() calls, the NoteSelector MUST clamp its current index to `min(index, newSize - 1)` to remain within the valid range of the updated note set. The index MUST NOT wrap or reset to 0; clamping keeps playback as close as possible to the current position without triggering an implicit pattern restart.
- **FR-028**: Octave-transposed notes that would exceed MIDI note number 127 MUST be clamped to 127 to remain within valid MIDI range.
- **FR-029**: All NoteSelector operations (setMode, setOctaveRange, setOctaveMode, advance, reset) MUST use zero heap allocation.

### Key Entities

- **HeldNote**: Represents a single held MIDI note. Attributes: MIDI note number (0-127), velocity (1-127), insertion order (monotonically increasing 16-bit counter). Notes are uniquely identified by their MIDI note number.
- **HeldNoteBuffer**: Fixed-capacity (32) collection of HeldNote entries. Provides two views: pitch-sorted (ascending) for directional traversal, and insertion-ordered (chronological) for AsPlayed mode. Supports noteOn (add/update), noteOff (remove), clear, size, and empty queries.
- **ArpMode**: Enumeration of 10 arp pattern modes that define how the NoteSelector traverses the held note set: Up, Down, UpDown, DownUp, Converge, Diverge, Random, Walk, AsPlayed, Chord.
- **OctaveMode**: Enumeration of 2 octave ordering strategies: Sequential (complete pattern per octave) and Interleaved (all octave transpositions per note).
- **ArpNoteResult**: Fixed-capacity result structure returned by advance(). Contains up to 32 MIDI note numbers (with octave offsets applied), up to 32 velocities, and a count of active notes in the result. Count is 1 for single-note modes, N for Chord mode.
- **NoteSelector**: Stateful traversal engine that receives a `const HeldNoteBuffer&` on each advance() call and produces the next note(s) to play according to the active ArpMode, octave range, and OctaveMode. Holds no reference to any buffer; all buffer coupling is at the call site. Tracks its current position (index, direction, octave offset) across advance() calls.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All 10 arp modes produce verified-correct note sequences for test cases with 1, 2, 3, and 4+ held notes, confirmed by deterministic unit tests.
- **SC-002**: Octave Sequential and Interleaved modes produce distinct, verified-correct orderings for the same held notes and octave range, confirmed by unit tests comparing the two outputs.
- **SC-003**: Zero heap allocation across all operations (noteOn, noteOff, clear, advance, reset, sorted view access), verified by compiling with no dynamic allocation in the component and confirming no use of new/delete/malloc/free/std::vector or any other allocating container.
- **SC-004**: The buffer handles 1000 rapid sequential noteOn/noteOff operations without corruption, verified by a stress test that checks buffer integrity after each operation.
- **SC-005**: Random mode selects each held note with approximately equal frequency (within 10% of expected proportion) over 3000+ iterations, verified by a statistical test.
- **SC-006**: Walk mode never produces an index outside [0, size-1] over 1000+ iterations, verified by a bounds-checking test.
- **SC-007**: All unit tests pass on Windows, macOS, and Linux (cross-platform CI).
- **SC-008**: Converge/Diverge modes produce correct alternating outside-in / inside-out patterns for both even and odd note counts, verified by exhaustive unit tests.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The maximum number of simultaneously held MIDI notes is 32, which exceeds what any standard MIDI keyboard can physically produce (10 fingers) and accommodates programmatic/sequencer input with generous headroom.
- MIDI note numbers are in the standard range 0-127. Velocities are in range 1-127 (velocity 0 is treated as noteOff by MIDI convention and is never stored).
- The insertion order counter (uint16_t) wrapping at 65535 is acceptable. In practice, a musician would need to press more than 65535 individual notes in a single session without ever calling clear() for this to matter.
- The HeldNoteBuffer is accessed from a single thread (the audio thread). No thread-safety mechanisms (atomics, mutexes) are required within the buffer itself.
- The NoteSelector's PRNG for Random and Walk modes should be deterministic when seeded, enabling reproducible test results. The existing Xorshift32 PRNG in the codebase can be reused.
- For UpDown/DownUp modes with exactly 2 notes, the pattern alternates between the two notes without endpoint repetition (simple ping-pong: [A, B, A, B, ...]).
- For UpDown/DownUp with a single note, the selector returns that single note repeatedly.
- Converge with an even number of notes alternates: lowest, highest, 2nd-lowest, 2nd-highest, etc. With an odd count, the center note is played last in the sequence.
- Diverge with an even number of notes starts from the two center notes, expanding outward. With an odd count, starts from the single center note.
- Walk mode uses a random step of exactly -1 or +1 per advance() call, each with 50% probability (no "stay" option). The index is clamped to [0, size-1] after applying the step. This produces continuous melodic movement with no repeated notes from staying in place.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `NoteEntry` struct | `dsp/include/krate/dsp/processors/mono_handler.h:73-76` | Similar struct with `note` and `velocity` fields. HeldNote adds `insertOrder`. Reference implementation for the data structure; cannot directly reuse because HeldNote needs the extra field and lives in a different layer (Layer 1 vs Layer 2). |
| `MonoHandler` note stack | `dsp/include/krate/dsp/processors/mono_handler.h:362-376` | Fixed-capacity array-backed note stack with addToStack/removeFromStack. Very similar pattern to HeldNoteBuffer's add/remove. Reference for the insertion/removal approach. However, MonoHandler is Layer 2 and does not maintain pitch-sorted ordering. |
| `Xorshift32` PRNG | `dsp/include/krate/dsp/core/random.h:40` | Fast, real-time-safe pseudo-random number generator. Should be reused directly for Random and Walk mode randomization in NoteSelector. |
| `SequencerCore` | `dsp/include/krate/dsp/primitives/sequencer_core.h:79-288` | Layer 1 primitive with step direction, ping-pong logic (`calculatePingPongStep`), and RNG state. Its ping-pong step calculation pattern is relevant to UpDown/DownUp modes. The SequencerCore itself will be composed with NoteSelector in Phase 2 (ArpeggiatorCore), not in this phase. |
| `EuclideanPattern` | `dsp/include/krate/dsp/core/euclidean_pattern.h:53` | Layer 0 Euclidean rhythm generator. Not needed in this phase but will be used in Phase 7. No conflict. |
| `VoiceAllocator` | `dsp/include/krate/dsp/systems/voice_allocator.h:186-1103` | Layer 3 system that manages voice assignment. The arp will feed notes through this in Phase 3. No conflict or reuse needed in Phase 1. |

**Initial codebase search for key terms:**

```bash
# Verified: no existing HeldNote, HeldNoteBuffer, NoteSelector, or ArpMode in the codebase
grep -r "HeldNote\|NoteSelector\|ArpMode\|ArpNoteResult\|OctaveMode" dsp/ plugins/
# Result: Only found in specs/arpeggiator-roadmap.md — no existing implementations
```

**Search Results Summary**: No existing implementations of HeldNoteBuffer, NoteSelector, ArpMode, or ArpNoteResult found in the codebase. The MonoHandler's note stack pattern and the Xorshift32 PRNG are the primary reuse candidates. The NoteEntry struct pattern is a reference but will not be directly reused due to the additional insertOrder field and different layer placement.

### Forward Reusability Consideration

*Note for planning phase: HeldNoteBuffer and NoteSelector are Layer 1 primitives that will be composed into the Layer 2 ArpeggiatorCore in Phase 2. Design decisions made here directly affect all subsequent arpeggiator phases.*

**Sibling features at same layer** (if known):
- `ArpLane<T>` (Phase 4) -- another Layer 1 primitive for independent step lanes. Will live alongside HeldNoteBuffer in the primitives layer but is independent.

**Potential shared components** (preliminary, refined in plan.md):
- The `Xorshift32` PRNG from Layer 0 is shared between NoteSelector (Random/Walk modes), SequencerCore (random direction), and future Phase 8 conditional trig probability evaluation.
- The array-backed fixed-capacity pattern (similar to MonoHandler's stack) is a design pattern that HeldNoteBuffer will follow. If a common "FixedStack" utility is warranted, it could be extracted to Layer 0, but the implementations are simple enough that direct implementation is preferable to avoid over-abstraction.
- NoteSelector's stateful traversal logic (index, direction, octave tracking) must be cleanly separable so ArpeggiatorCore (Phase 2) can call advance() per step tick and reset() on retrigger events.

## Implementation Verification *(mandatory at completion)*

<!--
  CRITICAL: This section MUST be completed when claiming spec completion.
  Constitution Principle XVI: Honest Completion requires explicit verification
  of ALL requirements before claiming "done".

  DO NOT fill this table from memory or assumptions. Each row requires you to
  re-read the actual implementation code and actual test output RIGHT NOW,
  then record what you found with specific file paths, line numbers, and
  measured values. Generic evidence like "implemented" or "test passes" is
  NOT acceptable — it must be verifiable by a human reader.

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
| FR-001 | MET | `held_note_buffer.h:81` — `kMaxNotes = 32`; `std::array<HeldNote, 32>` at lines 199-200; test `"HeldNoteBuffer - capacity limit 32 notes"` passes |
| FR-002 | MET | `held_note_buffer.h:32-36` — `HeldNote` struct with `note`, `velocity`, `insertOrder`; test `"noteOn adds notes"` verifies insertOrder monotonicity |
| FR-003 | MET | `held_note_buffer.h:88-131` — `noteOn()` linear scan for duplicate, updates velocity; test `"noteOn updates existing velocity"` verifies size stays 1 |
| FR-004 | MET | `held_note_buffer.h:136-168` — `noteOff()` shift-left removal, silent ignore at line 147; tests `"noteOff removes notes"` and `"noteOff unknown note ignored"` |
| FR-005 | MET | `held_note_buffer.h:188-190` — `byPitch()` returns `std::span{pitchSorted_}`, sorted by insertion-sort at lines 117-128; test `"noteOn adds notes"` verifies ascending |
| FR-006 | MET | `held_note_buffer.h:194-196` — `byInsertOrder()` returns `std::span{entries_}`, chronological; test `"noteOn adds notes"` verifies order |
| FR-007 | MET | `held_note_buffer.h:171-174` — `clear()` resets `size_=0`, `nextInsertOrder_=0`; test `"clear resets all state"` verifies |
| FR-008 | MET | `held_note_buffer.h:106-108` — `if (size_ >= kMaxNotes) return;`; test `"capacity limit 32 notes"` fills 32, adds 33rd, size stays 32 |
| FR-009 | MET | `held_note_buffer.h:88-168` — consistent dual-array maintenance; test `"stress test 1000 operations"` — 1000 interleaved ops, integrity checked each |
| FR-010 | MET | `held_note_buffer.h:250-297` — switch dispatches all 10 ArpMode values; 37 test cases across 8 tags cover all modes |
| FR-011 | MET | `held_note_buffer.h:328-350` — `advanceUp()` ascending modulo; test `"Up mode cycles ascending"` — [60,64,67,60,64,67] |
| FR-012 | MET | `held_note_buffer.h:355-380` — `advanceDown()` descending; test `"Down mode cycles descending"` — [67,64,60,67,64,60] |
| FR-013 | MET | `held_note_buffer.h:383-416` — `advanceUpDown()` ping-pong, no endpoint repeat; test `"UpDown mode no endpoint repeat"` — [60,64,67,64,60,64,67,64] |
| FR-014 | MET | `held_note_buffer.h:419-451` — `advanceDownUp()` offset ping-pong; test `"DownUp mode no endpoint repeat"` — [67,64,60,64,67,64,60,64] |
| FR-015 | MET | `held_note_buffer.h:454-481` — `advanceConverge()` outside-in, modulo wrap; tests: even [60,67,62,64], odd [60,64,62], pure wrap verified |
| FR-016 | MET | `held_note_buffer.h:484-523` — `advanceDiverge()` center-out, modulo wrap; tests: even [62,64,60,67], odd [62,60,64] |
| FR-017 | MET | `held_note_buffer.h:527-544` — `rng_.next() % size`; test `"Random mode distribution"` — 3000 iterations, each note within 10% of expected |
| FR-018 | MET | `held_note_buffer.h:547-573` — `(rng_.next() & 1)` for ±1 step, clamped; tests `"Walk mode bounds"` (1000 iters) and `"Walk mode step size always 1"` |
| FR-019 | MET | `held_note_buffer.h:576-598` — `byInsertOrder()[noteIndex_]`; test `"AsPlayed mode insertion order"` — [67,60,64] matches insertion, not pitch |
| FR-020 | MET | `held_note_buffer.h:287-296` — Chord copies all notes, no octave applied; tests `"Chord mode returns all notes"` and `"Chord mode ignores octave range"` |
| FR-021 | MET | `held_note_buffer.h:231-233` — `setOctaveRange()` clamps [1,4]; `applyOctave()` adds `octaveOffset * 12`; test `"Sequential octave mode"` verifies 3 octaves |
| FR-022 | MET | `held_note_buffer.h:320-349` — Sequential advances octave after pattern wrap; test `"Sequential octave mode"` — [60,64,72,76,84,88] |
| FR-023 | MET | `held_note_buffer.h:339-349` — Interleaved advances octave each call; test `"Interleaved octave mode"` — [60,72,84,64,76,88] |
| FR-024 | MET | `held_note_buffer.h:241` — `advance(const HeldNoteBuffer&)` returns `ArpNoteResult` with `std::array<uint8_t, 32>`; no stored reference; test `"Chord returns all notes"` verifies velocities |
| FR-025 | MET | `held_note_buffer.h:303-310` — `reset()` zeroes all state; 4 reset tests (Up, UpDown, Walk, octave) all pass |
| FR-026 | MET | `held_note_buffer.h:242-244` — `if (held.empty()) return {};`; test `"empty buffer returns count 0 all modes"` — all 10 modes verified |
| FR-027 | MET | `held_note_buffer.h:330,357,549` etc. — `std::min(noteIndex_, size-1)` clamping; test `"index clamped on buffer shrink"` passes |
| FR-028 | MET | `held_note_buffer.h:314-316` — `std::min(127, baseNote + octaveOffset * 12)`; test `"MIDI note clamped to 127"` — [120,127,127,127] |
| FR-029 | MET | Entire header uses `std::array`, `std::span`, primitives only; grep confirms no `new`/`delete`/`malloc`/`vector`/`string`/`map` |
| SC-001 | MET | 37 test cases: all 10 modes tested with 1 note (`"single note all modes"`), 2 notes (`"all modes with 2 notes"`), 3 notes (directional, converge/diverge, random/walk), 4+ notes (converge/diverge) |
| SC-002 | MET | Sequential [60,64,72,76,84,88] vs Interleaved [60,72,84,64,76,88] — distinct orderings for same inputs, both verified correct |
| SC-003 | MET | Code inspection: no dynamic allocation anywhere in 612-line header; only `std::array`, `std::span`, scalar types; SC-003 comments at lines 59, 78, 217 |
| SC-004 | MET | Test `"stress test 1000 operations"` — 1000 interleaved noteOn/noteOff, integrity verified after each: size, views match, pitch sorted |
| SC-005 | MET | Test `"Random mode distribution"` — 3000 iterations, 3 notes, each count within [900, 1100] (spec: 10% tolerance) |
| SC-006 | MET | Test `"Walk mode bounds"` — 1000 iterations, every note verified in valid set; zero out-of-bounds violations |
| SC-007 | PARTIAL | Windows: build clean, all 5,736 tests pass. macOS/Linux: CI verification pending (branch not yet pushed). Code is standard C++20, integer-only arithmetic, no platform-specific constructs. |
| SC-008 | MET | Tests: Converge even [60,67,62,64], odd [60,64,62]; Diverge even [62,64,60,67], odd [62,60,64] — all correct |

**Status Key:**
- MET: Requirement verified against actual code and test output with specific evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap and specific evidence of what IS met
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [x] Each FR-xxx row was verified by re-reading the actual implementation code (not from memory)
- [x] Each SC-xxx row was verified by running tests or reading actual test output (not assumed)
- [x] Evidence column contains specific file paths, line numbers, test names, and measured values
- [x] No evidence column contains only generic claims like "implemented", "works", or "test passes"
- [x] No test thresholds relaxed from spec requirements
- [x] No placeholder values or TODO comments in new code
- [x] No features quietly removed from scope
- [x] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: PARTIAL

**If NOT COMPLETE, document gaps:**
- Gap: SC-007 -- macOS/Linux CI verification pending. Branch has not been pushed yet. All code is standard C++20 with no platform-specific constructs, so cross-platform success is expected but unconfirmed.

**Recommendation**: Push the branch to remote and verify CI passes on all three platforms. Once confirmed, SC-007 -> MET and overall -> COMPLETE.
