# Data Model: HeldNoteBuffer & NoteSelector

**Feature**: 069-held-note-buffer | **Date**: 2026-02-20

## Entity Definitions

### HeldNote

A single held MIDI note with insertion-order tracking.

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| `note` | `uint8_t` | 0-127 | MIDI note number |
| `velocity` | `uint8_t` | 1-127 | Note velocity (0 never stored; velocity 0 = noteOff) |
| `insertOrder` | `uint16_t` | 0-65535 | Monotonically increasing counter for chronological ordering |

**Uniqueness**: Notes are uniquely identified by `note` (MIDI pitch). Duplicate noteOn for the same pitch updates `velocity` without creating a second entry.

**Size**: 4 bytes per entry (uint8_t + uint8_t + uint16_t, naturally aligned).

---

### HeldNoteBuffer

Fixed-capacity collection of HeldNote entries with dual-view access.

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `entries_` | `std::array<HeldNote, 32>` | 128 bytes | Primary storage in insertion order |
| `pitchSorted_` | `std::array<HeldNote, 32>` | 128 bytes | Secondary storage sorted by pitch (ascending) |
| `size_` | `size_t` | 8 bytes | Current number of held notes |
| `nextInsertOrder_` | `uint16_t` | 2 bytes | Next insertion order counter value |

**Constants**:
- `kMaxNotes = 32` -- Maximum simultaneous held notes

**Total memory**: ~266 bytes (no heap allocation)

**Invariants**:
- `size_ <= kMaxNotes` always
- `entries_[0..size_-1]` are in insertion order (chronological)
- `pitchSorted_[0..size_-1]` are sorted ascending by `note`
- Both arrays contain the same set of HeldNote values
- `nextInsertOrder_` increments on each new note (wraps at 65535, acceptable)

---

### ArpMode (Enumeration)

| Value | Name | Description |
|-------|------|-------------|
| 0 | Up | Ascending pitch order, wrap at top |
| 1 | Down | Descending pitch order, wrap at bottom |
| 2 | UpDown | Ascending then descending, no endpoint repeat |
| 3 | DownUp | Descending then ascending, no endpoint repeat |
| 4 | Converge | Outside edges inward: low, high, 2nd-low, 2nd-high, ... |
| 5 | Diverge | Center outward: center, then expanding |
| 6 | Random | Uniform random selection from held notes |
| 7 | Walk | Random +/-1 step, clamped to bounds |
| 8 | AsPlayed | Insertion order (chronological noteOn order) |
| 9 | Chord | All notes simultaneously |

**Underlying type**: `uint8_t`

---

### OctaveMode (Enumeration)

| Value | Name | Description |
|-------|------|-------------|
| 0 | Sequential | Complete pattern at each octave before advancing octave |
| 1 | Interleaved | Each note at all octave transpositions before next note |

**Underlying type**: `uint8_t`

---

### ArpNoteResult

Result structure returned by `NoteSelector::advance()`.

| Field | Type | Size | Description |
|-------|------|------|-------------|
| `notes` | `std::array<uint8_t, 32>` | 32 bytes | MIDI note numbers with octave offset applied |
| `velocities` | `std::array<uint8_t, 32>` | 32 bytes | Corresponding velocities |
| `count` | `size_t` | 8 bytes | Number of valid entries (1 for single-note modes, N for Chord) |

**Total memory**: 72 bytes (stack-allocated, no heap)

**Invariant**: `count <= 32` always. When count == 0, no notes should be played (empty buffer case).

---

### NoteSelector

Stateful traversal engine for arp note selection.

| Field | Type | Description |
|-------|------|-------------|
| `mode_` | `ArpMode` | Current arp mode |
| `octaveRange_` | `int` | Octave range 1-4 (1 = no transposition) |
| `octaveMode_` | `OctaveMode` | Sequential or Interleaved |
| `noteIndex_` | `size_t` | Current position in note pattern |
| `octaveOffset_` | `int` | Current octave offset (0 to octaveRange-1) |
| `direction_` | `int` | For UpDown/DownUp: +1 or -1 |
| `pingPongPos_` | `size_t` | Position within the ping-pong cycle |
| `convergeStep_` | `size_t` | Position within converge/diverge sequence |
| `walkIndex_` | `size_t` | Current walk position |
| `rng_` | `Xorshift32` | PRNG for Random and Walk modes |

**Total memory**: ~48 bytes (no heap allocation)

**State transitions**:
- `reset()`: Sets noteIndex_=0, octaveOffset_=0, direction_=+1, pingPongPos_=0, convergeStep_=0, walkIndex_=0
- `advance(held)`: Advances state according to current mode, returns next note(s)
- `setMode(mode)`: Changes mode and calls reset()
- `setOctaveRange(n)`: Changes octave range (1-4), clamps octaveOffset_ if needed
- `setOctaveMode(mode)`: Changes octave mode

---

## Relationships

```
HeldNoteBuffer "stores" 0..32 HeldNote
NoteSelector "reads" 1 HeldNoteBuffer (per advance() call, not stored)
NoteSelector "uses" 1 Xorshift32 (owned member)
NoteSelector "produces" 1 ArpNoteResult (per advance() call)
NoteSelector "configured by" 1 ArpMode + 1 OctaveMode
```

## Validation Rules

| Rule | Location | Description |
|------|----------|-------------|
| V-001 | HeldNoteBuffer::noteOn | If size_ == kMaxNotes and note is new, silently ignore |
| V-002 | HeldNoteBuffer::noteOn | If note already exists, update velocity only |
| V-003 | HeldNoteBuffer::noteOff | If note not found, silently ignore |
| V-004 | NoteSelector::setOctaveRange | Clamp input parameter to [1, 4] before storing in octaveRange_ |
| V-005 | NoteSelector::advance | If held.empty(), return {.count = 0} |
| V-006 | NoteSelector::advance | Clamp noteIndex_ to min(noteIndex_, held.size()-1) |
| V-007 | NoteSelector::advance | Clamp octave-transposed note to [0, 127] |

## State Transitions

### HeldNoteBuffer States

```
[Empty] --noteOn(n,v)--> [HasNotes]
[HasNotes] --noteOn(existing,v)--> [HasNotes] (velocity update)
[HasNotes] --noteOn(new,v)--> [HasNotes] (add, unless full)
[HasNotes] --noteOff(n)--> [HasNotes] or [Empty]
[HasNotes] --clear()--> [Empty]
[Full] --noteOn(new,v)--> [Full] (silently ignored)
[Full] --noteOff(n)--> [HasNotes]
```

### NoteSelector Mode Behavior

| Mode | Index Source | View Used | Direction State | Octave Applies |
|------|-------------|-----------|-----------------|----------------|
| Up | noteIndex_ | byPitch() | N/A (always forward) | Yes |
| Down | noteIndex_ | byPitch() | N/A (always reverse) | Yes |
| UpDown | pingPongPos_ | byPitch() | Ping-pong cycle | Yes |
| DownUp | pingPongPos_ | byPitch() | Ping-pong cycle (offset) | Yes |
| Converge | convergeStep_ | byPitch() | N/A (fixed sequence) | Yes |
| Diverge | convergeStep_ | byPitch() | N/A (fixed sequence) | Yes |
| Random | rng_ output | byPitch() | N/A (stateless) | Yes |
| Walk | walkIndex_ | byPitch() | +/-1 random step | Yes |
| AsPlayed | noteIndex_ | byInsertOrder() | N/A (always forward) | Yes |
| Chord | all indices | byPitch() | N/A (all at once) | No (FR-020) |
