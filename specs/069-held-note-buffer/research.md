# Research: HeldNoteBuffer & NoteSelector

**Feature**: 069-held-note-buffer | **Date**: 2026-02-20

## Research Summary

All technical questions from the spec clarification session have been resolved. This document consolidates design decisions, rationale, and alternatives considered.

---

## R-001: Buffer Data Structure for Dual-Sorted Views

**Question**: How to efficiently maintain both pitch-sorted and insertion-ordered views without heap allocation?

**Decision**: Single `std::array<HeldNote, 32>` with two index arrays for views.

**Rationale**: The buffer stores notes in a primary array. Two separate index arrays (`pitchIndices_` and `insertIndices_`) map into the primary array, providing O(1) view access via `std::span`. On noteOn, insertion sort maintains pitchIndices_ in O(N) which is acceptable for N<=32. On noteOff, both index arrays are updated with a linear scan and shift.

**Alternative 1 -- Two separate sorted arrays**: Rejected because it doubles the storage for HeldNote entries and requires keeping them synchronized on every mutation.

**Alternative 2 -- Sort on demand in byPitch()/byInsertOrder()**: Rejected because byPitch() is const and should not mutate internal state. Sorting on each access would either violate const-correctness or require mutable internal state, adding confusion. Also, multiple calls to byPitch() within the same advance() would sort repeatedly.

**Alternative 3 -- Single array, always sorted by pitch, binary search for insert order**: Rejected because insertion-order access would require scanning all entries and sorting by insertOrder, which is less clean than maintaining a separate index array.

**Final Design**: Maintain notes in a flat `entries_` array. Keep a `size_` counter. Maintain `pitchSorted_` as a secondary array that is always sorted by pitch (insertion-sort on noteOn, shift-remove on noteOff). The insertion order is implicit: `entries_` is always in insertion order because new notes are appended and removals shift-left to preserve relative order. `byPitch()` returns a span of the pitch-sorted array. `byInsertOrder()` returns a span of the entries array directly.

Actually, upon deeper analysis, the simplest approach that satisfies all requirements:
- Store notes in `entries_[0..size_-1]` in **insertion order** (append on add, shift-left on remove).
- Maintain a parallel `pitchSorted_[0..size_-1]` array of HeldNote copies sorted by pitch.
- On noteOn: append to entries_, insertion-sort into pitchSorted_.
- On noteOff: linear scan to find and shift-remove from both arrays.
- byInsertOrder() returns span over entries_.
- byPitch() returns span over pitchSorted_.

This uses 2 * 32 * sizeof(HeldNote) = 2 * 32 * 4 = 256 bytes. Trivial.

---

## R-002: UpDown/DownUp Endpoint Non-Repetition

**Question**: How to implement ping-pong traversal that does not repeat endpoint notes?

**Decision**: Use the same mathematical pattern as `SequencerCore::calculatePingPongStep` -- cycle length = 2*(N-1) for N notes, with mirror reflection at boundaries.

**Rationale**: The existing SequencerCore implementation at `sequencer_core.h:567-584` solves exactly this problem:
```
For N notes, cycle length = 2*(N-1)
Position 0..N-1: ascending (0,1,2,...,N-1)
Position N..2*(N-1)-1: descending mirror (N-2,N-3,...,1)
```
This naturally avoids repeating endpoints. For UpDown, start at position 0. For DownUp, start at position N-1 (the top) and go descending first. With 2 notes, cycle length = 2, producing [A, B, A, B, ...]. With 1 note, cycle length = 0, return the single note.

**Alternative -- Boolean direction flag with manual reversal**: Considered but rejected because the mathematical approach is more elegant, handles edge cases naturally (1 note, 2 notes), and avoids mutable direction state that could get out of sync.

**Implementation Note**: NoteSelector will track `index_` as a position within the cycle. UpDown: index increments modulo 2*(N-1). DownUp: same cycle but offset by N-1. The conversion from cycle position to array index uses the mirror formula.

---

## R-003: Converge/Diverge Index Patterns

**Question**: How to compute the alternating outside-in (Converge) and inside-out (Diverge) index sequences?

**Decision**: Pre-compute a flat index sequence of length N for the current note count, cycling through it.

**Rationale for Converge** (N notes, 0-indexed, pitch-sorted):
```
Converge order: [0, N-1, 1, N-2, 2, N-3, ...]
Step i:
  if i is even: index = i/2           (left side, moving inward)
  if i is odd:  index = N-1 - (i-1)/2 (right side, moving inward)
```

**Rationale for Diverge** (N notes, 0-indexed, pitch-sorted):
```
Diverge order: start from center, expand outward.
For even N (e.g., 4): center-left=1, center-right=2 -> [1, 2, 0, 3]
For odd N (e.g., 5): center=2 -> [2, 1, 3, 0, 4]

General formula:
  center = N/2 (integer division)
  For odd N: sequence[0] = center
  Then alternate: center-1, center+1, center-2, center+2, ...
  For even N: sequence[0] = center-1, sequence[1] = center
  Then alternate: center-2, center+1, center-3, center+2, ...
```

**Implementation**: Rather than pre-computing arrays, compute the index on-the-fly from the step position within the cycle. The NoteSelector tracks `convergeStep_` (0 to N-1), wrapping back to 0 after completing the sequence. No need for direction reversal -- pure wrap as confirmed in clarifications.

**Alternative -- Lookup table of indices**: Rejected as unnecessary allocation. The formula is trivial to compute.

---

## R-004: Walk Mode Probability and Clamping

**Question**: Confirmed -- Walk uses 50/50 for +1/-1 (no stay option). How to implement the coin flip using Xorshift32?

**Decision**: `bool stepUp = (rng_.next() & 1) != 0;` then apply +1 or -1 to current index, clamp to [0, size-1].

**Rationale**: Xorshift32's output has good bit distribution. Using the LSB as a coin flip is standard practice. The spec confirms 50/50 with no stay option, and clamping (not wrapping) at boundaries.

**Clamping behavior at boundaries**: At index 0, a -1 step clamps to 0 (stays). At index N-1, a +1 step clamps to N-1 (stays). This means boundary indices have a slight statistical bias toward staying, which creates natural "lingering" at the edges of the pitch range -- musically desirable.

---

## R-005: Octave Mode Integration with Index Advancement

**Question**: How do Sequential and Interleaved octave modes interact with the base pattern index?

**Decision**: The NoteSelector maintains two counters: `noteIndex_` (position in the base note pattern) and `octaveOffset_` (current octave shift, 0 to octaveRange-1).

**Sequential mode** advancement:
```
For each advance():
  1. Compute base note from mode logic at noteIndex_
  2. Apply octaveOffset_ * 12 to MIDI note
  3. Increment octaveOffset_
  4. If octaveOffset_ >= octaveRange: octaveOffset_ = 0, advance noteIndex_ in pattern
```
This plays each note at the current octave, cycling through all octaves before moving to the next pattern note.

Wait -- re-reading the spec more carefully:

**Sequential**: "Play the complete note pattern at octave 0, then the complete pattern at octave +1, and so on."
So noteIndex advances through the full pattern first, THEN octave increments.

```
Sequential:
  For each advance():
    1. Compute base note from mode logic at noteIndex_
    2. Apply octaveOffset_ * 12 to MIDI note
    3. Advance noteIndex_ in pattern
    4. If pattern wrapped (noteIndex_ back to 0): increment octaveOffset_
    5. If octaveOffset_ >= octaveRange: octaveOffset_ = 0
```

**Interleaved**: "Play each note at all its octave transpositions before moving to the next note."
```
Interleaved:
  For each advance():
    1. Compute base note from mode logic at noteIndex_
    2. Apply octaveOffset_ * 12 to MIDI note
    3. Increment octaveOffset_
    4. If octaveOffset_ >= octaveRange: octaveOffset_ = 0, advance noteIndex_ in pattern
```

**Chord mode exception**: Chord ignores octave range entirely (FR-020). Returns all held notes at original pitch.

**MIDI clamping**: When base note + octaveOffset * 12 > 127, clamp to 127 (FR-028).

---

## R-006: Index Clamping on Buffer Mutation

**Question**: Confirmed -- when notes are removed mid-pattern, clamp index to min(index, newSize-1). No wrap, no reset.

**Decision**: NoteSelector does NOT store a reference to HeldNoteBuffer. It receives `const HeldNoteBuffer&` on each `advance()` call. The clamping is applied at the start of advance() before any index is used:

```cpp
if (held.size() == 0) return ArpNoteResult{.count = 0};
noteIndex_ = std::min(noteIndex_, held.size() - 1);
```

This naturally handles buffer mutations between calls because the NoteSelector only uses the buffer's current state at the moment of the call.

---

## R-007: Chord Mode Design

**Question**: How does Chord mode fill ArpNoteResult?

**Decision**: Chord mode copies all held notes into the result array. It ignores octave range and octave mode. The result count equals the number of held notes.

```cpp
ArpNoteResult result{};
const auto& notes = held.byPitch();
result.count = notes.size();
for (size_t i = 0; i < result.count; ++i) {
    result.notes[i] = notes[i].note;
    result.velocities[i] = notes[i].velocity;
}
return result;
```

No octave transposition, no index advancement, no direction state mutation.

---

## R-008: Random Mode Using Xorshift32

**Question**: How to get uniform random selection from N notes using Xorshift32?

**Decision**: `size_t idx = rng_.next() % held.size();` -- simple modulo.

**Rationale**: For N <= 32, modulo bias is negligible. The maximum bias occurs when N is a large fraction of 2^32, but with N <= 32, the bias is less than 32/4294967296 = 0.0000007%. Far below the 10% tolerance specified in SC-005.

**Alternative -- Rejection sampling**: Rejected as overkill for this range.

---

## R-009: Header File Organization

**Question**: Should HeldNoteBuffer and NoteSelector be in separate headers or one combined header?

**Decision**: Single header `held_note_buffer.h` containing all types: HeldNote, HeldNoteBuffer, ArpMode, OctaveMode, ArpNoteResult, NoteSelector.

**Rationale**: These types are tightly coupled -- NoteSelector's advance() takes `const HeldNoteBuffer&` and returns ArpNoteResult. ArpMode and OctaveMode are configuration for NoteSelector. Splitting would create circular include concerns or require a third "types" header for no practical benefit. The combined header is estimated at ~400 lines, well within reasonable header size.

**Phase 2 impact**: ArpeggiatorCore will include this single header to access all arp primitive types. Clean single-include dependency.

---

## R-010: Test Strategy

**Question**: How to structure tests for deterministic verification of all 10 modes?

**Decision**: Separate TEST_CASE blocks for each component and mode:

1. **HeldNoteBuffer tests**: noteOn/noteOff basics, duplicate handling, capacity limits, ordering verification, clear, stress test (SC-004).
2. **NoteSelector directional mode tests**: Up, Down with known note sets, verify exact sequence over multiple cycles.
3. **NoteSelector UpDown/DownUp tests**: Verify non-repeating endpoints, 1-note, 2-note, 3+ note cases.
4. **NoteSelector Converge/Diverge tests**: Even and odd note counts, verify exact sequences (SC-008).
5. **NoteSelector Random tests**: Seed PRNG, run 3000+ iterations, verify statistical distribution (SC-005).
6. **NoteSelector Walk tests**: Seed PRNG, run 1000+ iterations, verify bounds (SC-006).
7. **NoteSelector AsPlayed/Chord tests**: Insertion order verification, chord returns all notes.
8. **Octave mode tests**: Sequential vs Interleaved produce distinct orderings (SC-002).
9. **Edge case tests**: Empty buffer, single note, buffer mutation mid-pattern, MIDI clamping.
10. **Reset tests**: Verify reset() returns all modes to start of pattern.

**PRNG seeding for determinism**: Tests seed the NoteSelector's Xorshift32 with a known value to ensure reproducible sequences. The NoteSelector constructor takes an optional seed parameter.
