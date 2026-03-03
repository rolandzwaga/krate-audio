# Quickstart: Mono/Legato Handler Implementation

**Feature Branch**: `035-mono-legato-handler`
**Date**: 2026-02-07

---

## Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/processors/mono_handler.h` | Header-only MonoHandler class |
| `dsp/tests/unit/processors/mono_handler_test.cpp` | Catch2 unit tests |

## Files to Modify

| File | Change |
|------|--------|
| `dsp/CMakeLists.txt` | Add `mono_handler.h` to `KRATE_DSP_PROCESSORS_HEADERS` |
| `dsp/tests/CMakeLists.txt` | Add test file to `dsp_tests` target + `-fno-fast-math` list |
| `specs/_architecture_/layer-2-processors.md` | Add MonoHandler documentation |

## Build & Test Commands

```bash
# Build DSP tests (Windows)
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests

# Run MonoHandler tests only
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[mono_handler]"

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe
```

## Implementation Order

### Phase 1: Core Note Handling (US1, FR-001 through FR-016)

1. Create header with MonoNoteEvent, MonoMode, PortaMode, NoteEntry, MonoHandler shell
2. Write tests for basic noteOn/noteOff with LastNote priority
3. Implement note stack + LastNote priority
4. Write tests for note stack edge cases (full stack, same-note re-press, velocity 0)
5. Implement edge case handling

### Phase 2: Priority Modes (US2, FR-008 through FR-010)

1. Write tests for LowNote and HighNote modes
2. Implement priority evaluation in findWinner()
3. Write tests for setMode() re-evaluation
4. Implement setMode() with re-evaluation

### Phase 3: Legato (US3, FR-017 through FR-020)

1. Write tests for legato retrigger suppression
2. Implement legato logic in noteOn/noteOff

### Phase 4: Portamento (US4, FR-021 through FR-025)

1. Write tests for portamento glide (linearity in pitch space, timing accuracy)
2. Implement portamento using LinearRamp in semitone space
3. Write tests for mid-glide redirection, zero portamento time

### Phase 5: Portamento Modes (US5, FR-027 through FR-028)

1. Write tests for Always vs LegatoOnly portamento
2. Implement portamento mode logic

### Phase 6: Cross-cutting (FR-029 through FR-034, SC-001 through SC-012)

1. Write tests for reset(), all FR-031/032 noexcept
2. Write performance tests (SC-009: noteOn < 500ns, SC-012: sizeof <= 512)
3. Write comprehensive accuracy tests (SC-005 through SC-008)

## Key Design Decisions

- **LinearRamp reuse**: Portamento uses LinearRamp from `smoother.h` operating in semitone space
- **Return by value**: noteOn/noteOff return MonoNoteEvent (8 bytes, trivially copyable)
- **Simple linear array**: Note stack is `std::array<NoteEntry, 16>` with insertion-order maintenance
- **Semitone space**: All portamento math in semitones, convert to Hz at output via `semitonesToRatio()`

## Dependency Include Chain

```cpp
#include <krate/dsp/core/db_utils.h>      // detail::isNaN, isInf
#include <krate/dsp/core/midi_utils.h>     // midiNoteToFrequency, kA4FrequencyHz
#include <krate/dsp/core/pitch_utils.h>    // semitonesToRatio
#include <krate/dsp/primitives/smoother.h> // LinearRamp
```

## Test Tags

Use `[mono_handler]` as the primary tag. Sub-tags:

| Tag | Coverage |
|-----|---------|
| `[mono_handler][us1]` | Basic monophonic note handling |
| `[mono_handler][us2]` | Priority modes |
| `[mono_handler][us3]` | Legato mode |
| `[mono_handler][us4]` | Portamento |
| `[mono_handler][us5]` | Portamento modes |
| `[mono_handler][edge]` | Edge cases |
| `[mono_handler][perf]` | Performance (SC-009, SC-012) |
| `[mono_handler][accuracy]` | Numerical accuracy (SC-005 through SC-008) |
