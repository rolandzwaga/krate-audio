# Quickstart: HeldNoteBuffer & NoteSelector

**Feature**: 069-held-note-buffer | **Date**: 2026-02-20

## Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/primitives/held_note_buffer.h` | Header-only implementation of HeldNote, HeldNoteBuffer, ArpMode, OctaveMode, ArpNoteResult, NoteSelector |
| `dsp/tests/unit/primitives/held_note_buffer_test.cpp` | Catch2 unit tests for all components |

## Files to Modify

| File | Change |
|------|--------|
| `dsp/tests/CMakeLists.txt` | Add `unit/primitives/held_note_buffer_test.cpp` to the `dsp_tests` target |
| `specs/_architecture_/layer-1-primitives.md` | Add documentation for HeldNoteBuffer and NoteSelector |

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run all DSP tests
build/windows-x64-release/bin/Release/dsp_tests.exe

# Run only HeldNoteBuffer tests
build/windows-x64-release/bin/Release/dsp_tests.exe "[held_note_buffer]"

# Run only NoteSelector tests
build/windows-x64-release/bin/Release/dsp_tests.exe "[note_selector]"
```

**Cross-platform note (SC-007)**: The commands above are for Windows. On macOS use `cmake --preset macos-release` and on Linux use `cmake --preset linux-release`. The test binary path and filter syntax are identical across platforms. SC-007 cross-platform verification is confirmed by CI after the branch is pushed -- the local Windows run covers correctness, while CI covers macOS and Linux.

## Implementation Order

Follow test-first development (Constitution Principle XIII):

### Task Group 1: HeldNoteBuffer (FR-001 through FR-009)

1. Write test: noteOn adds notes, verify size and byPitch/byInsertOrder ordering
2. Write test: noteOff removes notes, verify ordering preserved
3. Write test: duplicate noteOn updates velocity
4. Write test: capacity limit (32 notes, 33rd silently dropped)
5. Write test: clear() resets everything
6. Write test: stress test -- 1000 rapid noteOn/noteOff (SC-004)
7. Implement HeldNote struct and HeldNoteBuffer class
8. Build, fix warnings, verify all tests pass

### Task Group 2: NoteSelector -- Directional Modes (FR-010 through FR-014)

1. Write test: Up mode with 3 notes, verify ascending cycle
2. Write test: Down mode with 3 notes, verify descending cycle
3. Write test: UpDown mode, verify no endpoint repeat
4. Write test: DownUp mode, verify no endpoint repeat
5. Write test: UpDown/DownUp with 1 note, 2 notes
6. Implement NoteSelector with Up/Down/UpDown/DownUp
7. Build, fix warnings, verify all tests pass

### Task Group 3: NoteSelector -- Converge/Diverge (FR-015, FR-016)

1. Write test: Converge with 4 notes (even count)
2. Write test: Converge with 3 notes (odd count)
3. Write test: Diverge with 4 notes (even count)
4. Write test: Diverge with 3 notes (odd count)
5. Implement Converge and Diverge modes
6. Build, fix warnings, verify all tests pass

### Task Group 4: NoteSelector -- Random/Walk (FR-017, FR-018)

1. Write test: Random mode distribution over 3000+ iterations (SC-005)
2. Write test: Walk mode bounds over 1000+ iterations (SC-006)
3. Write test: Walk mode step size is always exactly 1
4. Implement Random and Walk modes
5. Build, fix warnings, verify all tests pass

### Task Group 5: NoteSelector -- AsPlayed/Chord (FR-019, FR-020)

1. Write test: AsPlayed follows insertion order
2. Write test: Chord returns all notes
3. Write test: Chord ignores octave range
4. Implement AsPlayed and Chord modes
5. Build, fix warnings, verify all tests pass

### Task Group 6: Octave Modes (FR-021 through FR-023, FR-028)

1. Write test: Sequential mode -- full pattern per octave (SC-002)
2. Write test: Interleaved mode -- all octaves per note (SC-002)
3. Write test: MIDI clamping at 127 (FR-028)
4. Write test: Octave range 1 = no transposition
5. Implement octave logic in advance()
6. Build, fix warnings, verify all tests pass

### Task Group 7: Pattern Reset on Retrigger (FR-025) -- Phase 9 in tasks.md

1. Write test: reset() returns to start of pattern for all modes (Up, UpDown direction, Walk index, octave offset)
2. Verify reset() resets all state fields (noteIndex_, pingPongPos_, octaveOffset_, walkIndex_, convergeStep_)
3. Build, fix warnings, verify all tests pass

### Task Group 7b: Edge Cases and Integration (FR-024 through FR-027, FR-029) -- Phase 10 in tasks.md

> Note: FR-028 (MIDI clamping to 127) was covered in Task Group 6 above.

1. Write test: advance() with empty buffer returns count=0 for all 10 modes
2. Write test: Index clamping when notes removed mid-pattern
3. Write test: All 10 modes with single note held
4. Verify zero heap allocation (SC-003) by code inspection -- no new/delete/vector in header
5. Build, fix warnings, verify all tests pass

### Task Group 8: Finalization

1. Run full dsp_tests suite -- all tests pass (SC-007)
2. Run clang-tidy
3. Update `specs/_architecture_/layer-1-primitives.md`
4. Fill compliance table in spec.md
5. Commit

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| Single header file | All 6 types are tightly coupled; NoteSelector::advance takes HeldNoteBuffer& and returns ArpNoteResult |
| Two parallel arrays in HeldNoteBuffer | entries_ in insertion order, pitchSorted_ in pitch order; avoids sorting on every access |
| Xorshift32 for PRNG | Already exists in Layer 0, real-time safe, deterministic when seeded |
| Ping-pong cycle math for UpDown/DownUp | Same pattern as SequencerCore::calculatePingPongStep; cycle=2*(N-1), mirror at endpoints |
| Index clamping (not wrapping) on buffer mutation | Spec clarification: keep playback position as close as possible to current |
| Chord ignores octave range | Spec FR-020: returns all held notes at original pitch only |
| NoteSelector takes buffer by const ref (no stored pointer) | Spec clarification: all coupling is at the call site |

## Dependencies

```
held_note_buffer.h
    +-- <krate/dsp/core/random.h>    (Xorshift32 PRNG)
    +-- <array>                       (std::array)
    +-- <cstdint>                     (uint8_t, uint16_t)
    +-- <cstddef>                     (size_t)
    +-- <span>                        (std::span -- C++20)
    +-- <algorithm>                   (std::min, std::sort)
```
