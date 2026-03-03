# Implementation Plan: HeldNoteBuffer & NoteSelector

**Branch**: `069-held-note-buffer` | **Date**: 2026-02-20 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/069-held-note-buffer/spec.md`

## Summary

Two DSP Layer 1 (primitives) components for Phase 1 of the Ruinae arpeggiator:

1. **HeldNoteBuffer** -- Fixed-capacity (32), heap-free buffer tracking currently held MIDI notes. Provides pitch-sorted and insertion-ordered views. Maintains a monotonically increasing insertion counter for chronological ordering. Follows the same array-backed fixed-capacity pattern as MonoHandler's note stack but adds pitch-sorted view maintenance and insertion ordering.

2. **NoteSelector** -- Stateful traversal engine implementing all 10 arp modes (Up, Down, UpDown, DownUp, Converge, Diverge, Random, Walk, AsPlayed, Chord) with octave range (1-4) and two octave modes (Sequential, Interleaved). Receives `const HeldNoteBuffer&` per call with no stored reference. Reuses the existing `Xorshift32` PRNG from Layer 0 for Random and Walk modes.

Both components are header-only in `dsp/include/krate/dsp/primitives/held_note_buffer.h` with tests in `dsp/tests/unit/primitives/held_note_buffer_test.cpp`.

## Technical Context

**Language/Version**: C++20 (constexpr-heavy, header-only DSP primitive)
**Primary Dependencies**: `Xorshift32` (Layer 0, `dsp/include/krate/dsp/core/random.h`), `<array>`, `<cstdint>`, `<algorithm>`
**Storage**: N/A (in-memory, fixed-capacity arrays only)
**Testing**: Catch2 via `dsp_tests` target *(Constitution Principle VIII: Testing Discipline)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: Shared DSP library (`dsp/`)
**Performance Goals**: Layer 1 primitive < 0.1% CPU; zero heap allocation; called once per arp step tick (not per-sample)
**Constraints**: Zero dynamic allocation; real-time audio thread safe; single-threaded access assumed; all operations noexcept
**Scale/Scope**: 2 classes, 2 enums, 1 result struct in a single header file; 1 test file

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] Zero heap allocation in all operations (std::array, no std::vector)
- [x] No locks, mutexes, or blocking primitives
- [x] No file I/O, exceptions, or system calls
- [x] All methods marked noexcept

**Required Check - Principle III (Modern C++):**
- [x] No raw new/delete -- all fixed-capacity arrays
- [x] constexpr where possible
- [x] std::array over C-style arrays

**Required Check - Principle IV (SIMD):**
- [x] SIMD viability analysis completed (see section below -- NOT BENEFICIAL)

**Required Check - Principle IX (Layered Architecture):**
- [x] Layer 1 primitive -- only depends on Layer 0 (Xorshift32 from core/random.h)
- [x] No upward dependencies

**Required Check - Principle VIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Post-Design Re-Check:**
- [x] All design decisions validated against constitution
- [x] No violations found

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: HeldNote, HeldNoteBuffer, ArpMode, OctaveMode, ArpNoteResult, NoteSelector

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| HeldNote | `grep -r "struct HeldNote" dsp/ plugins/` | No | Create New |
| HeldNoteBuffer | `grep -r "class HeldNoteBuffer" dsp/ plugins/` | No | Create New |
| ArpMode | `grep -r "ArpMode" dsp/ plugins/` | No (only in specs/) | Create New |
| OctaveMode | `grep -r "OctaveMode" dsp/ plugins/` | No (only in specs/) | Create New |
| ArpNoteResult | `grep -r "ArpNoteResult" dsp/ plugins/` | No (only in specs/) | Create New |
| NoteSelector | `grep -r "class NoteSelector" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None -- all logic is encapsulated in class methods.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `Xorshift32` | `dsp/include/krate/dsp/core/random.h` | 0 | PRNG for Random and Walk modes in NoteSelector. Stored as member `rng_`. |

### Reference Implementations (patterns to follow, not directly reused)

| Component | Location | Layer | Pattern Reference |
|-----------|----------|-------|-------------------|
| `NoteEntry` | `dsp/include/krate/dsp/processors/mono_handler.h:72-75` | 2 | Struct with `note` and `velocity` fields. HeldNote extends this with `insertOrder`. |
| `MonoHandler::addToStack` | `dsp/include/krate/dsp/processors/mono_handler.h:362-367` | 2 | Fixed-capacity array insertion pattern with bounds check. |
| `MonoHandler::removeFromStack` | `dsp/include/krate/dsp/processors/mono_handler.h:369-376` | 2 | Linear scan + shift-left removal pattern. |
| `MonoHandler::removeAtIndex` | `dsp/include/krate/dsp/processors/mono_handler.h:378-384` | 2 | Shift-left to maintain order after removal at index. |
| `SequencerCore::calculatePingPongStep` | `dsp/include/krate/dsp/primitives/sequencer_core.h:567-584` | 1 | Ping-pong (bounce) traversal: cycle length = 2*(N-1), mirror at endpoints without repeating boundaries. Relevant to UpDown/DownUp modes. |
| `SequencerCore` Direction enum | `dsp/include/krate/dsp/primitives/sequencer_core.h:40-45` | 1 | Forward/Backward/PingPong/Random direction model. NoteSelector's ArpMode is a superset. |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no `held_note_buffer.h` exists)
- [x] `specs/_architecture_/layer-1-primitives.md` - Component inventory (no HeldNoteBuffer or NoteSelector documented)
- [x] `dsp/include/krate/dsp/processors/mono_handler.h` - NoteEntry struct in Layer 2 (different namespace path, no conflict with HeldNote in Layer 1)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (HeldNote, HeldNoteBuffer, ArpMode, OctaveMode, ArpNoteResult, NoteSelector) are unique names not found anywhere in the codebase. The existing `NoteEntry` struct in `MonoHandler` is in a different scope (nested usage within MonoHandler) and has a different name. No naming collisions possible.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Xorshift32 | Constructor | `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` | Yes |
| Xorshift32 | next() | `[[nodiscard]] constexpr uint32_t next() noexcept` | Yes |
| Xorshift32 | seed() | `constexpr void seed(uint32_t seedValue) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32 class (lines 39-90)
- [x] `dsp/include/krate/dsp/processors/mono_handler.h` - NoteEntry struct (lines 72-75), addToStack/removeFromStack/removeAtIndex patterns
- [x] `dsp/include/krate/dsp/primitives/sequencer_core.h` - Direction enum (lines 40-45), calculatePingPongStep (lines 567-584)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Xorshift32 | Seed of 0 is auto-replaced with default seed | Always use non-zero seed, or accept default behavior |
| Xorshift32 | `next()` returns range [1, 2^32-1], never 0 | Use modulo for index selection: `rng_.next() % size` |
| MonoHandler NoteEntry | Has only `note` and `velocity` (no insertOrder) | HeldNote adds `insertOrder` -- not directly reusable |

## Layer 0 Candidate Analysis

*This is a Layer 1 feature. Layer 0 extraction analysis is still relevant for any shared utilities.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Pitch-sorting logic | Simple `std::sort` on 32-element array; no reuse value |
| Converge/Diverge index calculation | Specific to arp pattern traversal |
| Walk clamping | Trivial `std::clamp` call |

**Decision**: No utilities warrant extraction to Layer 0. All logic is arp-specific and simple enough to remain as class methods. The only Layer 0 dependency is `Xorshift32`, which already exists.

## SIMD Optimization Analysis

*GATE: Must complete during planning. Constitution Principle IV.*

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | No sample-to-sample feedback. Buffer operations are discrete events. |
| **Data parallelism width** | 1 | Only one note selection happens per advance() call. No parallel streams. |
| **Branch density in inner loop** | HIGH | Mode switch (10 branches), direction reversal, bounds clamping, octave mode branching. |
| **Dominant operations** | Memory/Logic | Array indexing, comparison, sorting (32 elements max). No transcendentals. |
| **Current CPU budget vs expected usage** | <0.1% budget, <0.01% expected | Called once per arp step (e.g., every 125ms at 120BPM 1/8th notes). Negligible CPU. |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The HeldNoteBuffer and NoteSelector are event-rate components called once per arp step tick, not per audio sample. With a maximum of 32 entries and purely logic-driven branching (mode selection, direction reversal, bounds checking), there is no meaningful data parallelism to exploit. The algorithm is well under 0.01% CPU even in scalar form. SIMD optimization would add complexity with zero measurable benefit.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Maintain pitch-sorted array incrementally (insertion sort on noteOn) | Avoid re-sorting on every byPitch() call | LOW | YES |
| Early return in advance() for single-note case | Skip all mode logic | LOW | YES |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 1 - DSP Primitives

**Related features at same layer** (from arpeggiator roadmap):
- ArpLane<T> (Phase 4) -- generic fixed-capacity step lane
- EuclideanPattern already exists at Layer 0

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| HeldNoteBuffer | HIGH | ArpeggiatorCore (Phase 2), Ruinae Processor (Phase 3) | Keep in current file -- consumed by composition |
| NoteSelector | HIGH | ArpeggiatorCore (Phase 2) | Keep in current file -- composed into ArpeggiatorCore |
| ArpMode enum | HIGH | ArpeggiatorCore (Phase 2), Ruinae plugin_ids.h (Phase 3) | Keep in current file -- referenced by higher layers |
| ArpNoteResult | HIGH | ArpeggiatorCore (Phase 2) | Keep in current file -- returned through ArpeggiatorCore's API |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Single header file for all arp primitives | HeldNote, HeldNoteBuffer, ArpMode, OctaveMode, ArpNoteResult, NoteSelector are tightly coupled. Splitting would create unnecessary include management for no benefit. |
| HeldNoteBuffer as class (not extending MonoHandler) | MonoHandler is Layer 2, lives in processors/, has portamento and priority-based note selection -- too heavy. HeldNoteBuffer is a focused data structure with different semantics (sorted views, insertion ordering). |
| No shared FixedStack utility | MonoHandler's stack and HeldNoteBuffer's array are similar patterns but HeldNoteBuffer has unique requirements (dual sorting, insertOrder tracking, duplicate detection). Extracting a common base would be premature abstraction. |

### Review Trigger

After implementing **ArpeggiatorCore (Phase 2)**, review this section:
- [ ] Does ArpeggiatorCore need additional views from HeldNoteBuffer? (e.g., filtered by velocity)
- [ ] Does ArpeggiatorCore extend ArpNoteResult? (e.g., add sampleOffset)
- [ ] Any duplicated index/direction logic between NoteSelector and SequencerCore?

## Project Structure

### Documentation (this feature)

```text
specs/069-held-note-buffer/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
    +-- held_note_buffer_api.h   # C++ API contract
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/primitives/
|   +-- held_note_buffer.h       # HeldNote, HeldNoteBuffer, ArpMode, OctaveMode, ArpNoteResult, NoteSelector
+-- tests/unit/primitives/
    +-- held_note_buffer_test.cpp # All unit tests for both components
```

**Build system change**: Add `unit/primitives/held_note_buffer_test.cpp` to the `dsp_tests` target in `dsp/tests/CMakeLists.txt`.

**Structure Decision**: Standard KrateDSP Layer 1 primitive pattern -- single header in `primitives/`, single test file in `tests/unit/primitives/`, registered in the existing `dsp_tests` CMake target.

## Complexity Tracking

No constitution violations. No complexity to track.
