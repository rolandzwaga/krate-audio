# Quickstart: Arpeggiator Core Implementation

**Branch**: `070-arpeggiator-core` | **Date**: 2026-02-20

---

## Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/processors/arpeggiator_core.h` | Main header (header-only) |
| `dsp/tests/unit/processors/arpeggiator_core_test.cpp` | Unit tests |

## Files to Modify

| File | Change |
|------|--------|
| `dsp/CMakeLists.txt` | Add `arpeggiator_core.h` to `KRATE_DSP_PROCESSORS_HEADERS` |
| `dsp/tests/CMakeLists.txt` | Add `arpeggiator_core_test.cpp` to `dsp_tests` source list and `-fno-fast-math` list |
| `dsp/lint_all_headers.cpp` | Add `#include <krate/dsp/processors/arpeggiator_core.h>` |

## Build & Test Commands

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure (if not already done)
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run all DSP tests
build/windows-x64-release/bin/Release/dsp_tests.exe

# Run only arpeggiator tests
build/windows-x64-release/bin/Release/dsp_tests.exe "[arpeggiator_core]"
```

## Implementation Order

> **Note**: The step numbers below match tasks.md phases for cross-reference. CMake registration
> (tasks.md Phase 1, T001–T004) MUST be completed before any compilation step can succeed.
> This corresponds to Step 0 here. Steps 1–12 below align with tasks.md Phases 2–13.

### Step 0: CMake Registration (MUST DO FIRST -- tasks.md Phase 1, T001–T004)

Register the new files with the build system before writing any code:
- Add `include/krate/dsp/processors/arpeggiator_core.h` to `KRATE_DSP_PROCESSORS_HEADERS` in `dsp/CMakeLists.txt`
- Add `unit/processors/arpeggiator_core_test.cpp` to the `dsp_tests` source list in `dsp/tests/CMakeLists.txt`
- Add `unit/processors/arpeggiator_core_test.cpp` to the `-fno-fast-math` properties block in `dsp/tests/CMakeLists.txt`
- Add `#include <krate/dsp/processors/arpeggiator_core.h>` to `dsp/lint_all_headers.cpp`

The build will fail until the header is created (Step 1), but the build system must know about the files first. See tasks.md Phase 1 for exact insertion points.

### Step 1: Skeleton + Enums + ArpEvent (FR-001, FR-027, FR-028)

Create the header with:
- `LatchMode` enum
- `ArpRetriggerMode` enum
- `ArpEvent` struct
- `ArpeggiatorCore` class with empty method stubs

Write basic test: construct ArpeggiatorCore, call prepare/reset. Build and verify compilation.

### Step 2: prepare/reset/setters (FR-003, FR-004, FR-008 through FR-018)

Implement:
- `prepare()`: store sampleRate (clamped), maxBlockSize
- `reset()`: zero timing, reset selector, clear pending NoteOffs
- All setter methods with clamping
- `setMode()` must also reset `swingStepCounter_`

Write tests for setter clamping behavior. Build and verify.

### Step 3: noteOn/noteOff with latch logic (FR-005, FR-006, FR-007)

Implement:
- Latch Off: standard forward to HeldNoteBuffer
- Latch Hold: track physical keys, replace on new input when latched
- Latch Add: always add, never remove

Write tests for each latch mode. Build and verify.

### Step 4: Basic processBlock -- tempo sync timing (FR-019)

Implement the core processing loop:
- Zero blockSize guard (FR-032)
- Enabled/disabled check (FR-008)
- Transport playing check (FR-031)
- Integer sample counter with step boundary detection
- Step duration calculation from tempo + note value
- NoteOn event emission at step boundaries
- Advance NoteSelector on each step

Write timing accuracy tests (SC-001). Build and verify.

### Step 5: Gate length + pending NoteOff tracking (FR-015, FR-019g, FR-025, FR-026)

Implement:
- Gate duration calculation from step duration
- PendingNoteOff array management
- NoteOff emission at correct sample offsets
- Cross-block NoteOff handling

Write gate accuracy tests (SC-002). Build and verify.

### Step 6: Swing timing (FR-016, FR-020)

Implement:
- Swing step counter (even/odd)
- Swing formula applied to step duration
- Reset conditions for swing counter

Write swing accuracy tests (SC-006). Build and verify.

### Step 7: Gate overlap / legato (FR-021)

Implement:
- Gate > 100% allowing NoteOn before previous NoteOff
- Correct event ordering in output buffer

Write legato overlap tests (SC-007). Build and verify.

### Step 8: Chord mode (FR-022)

Implement:
- When NoteSelector returns count > 1, emit multiple NoteOn events
- Track multiple currently sounding notes
- Multiple pending NoteOffs for chord

Write chord mode tests. Build and verify.

### Step 9: Retrigger modes (FR-006, FR-018, FR-023)

Implement:
- Note retrigger: reset selector on noteOn
- Beat retrigger: bar boundary detection in processBlock

Write retrigger tests (SC-005). Build and verify.

### Step 10: Free rate mode (FR-012, FR-014)

Implement:
- Toggle between tempo sync and free rate
- Free rate step duration calculation

Write free rate tests. Build and verify.

### Step 11: Edge cases and transport (FR-024, FR-031, FR-032)

Implement/verify:
- Empty buffer returns 0 events
- Transport stop emits NoteOff, preserves latch state
- Transport restart resumes arpeggiation
- Zero blockSize returns 0 with no state change

Write edge case tests (SC-010). Build and verify.

### Step 12: Long-running drift test (SC-008)

Write test that runs 1000 steps and verifies cumulative timing error is exactly 0 samples.

### Step 13: Final build verification + lint

CMake registration was done in Step 0. This step verifies everything builds cleanly end-to-end:
- Build full project (not just `dsp_tests`): `"$CMAKE" --build build/windows-x64-release --config Release`
- Run all DSP tests: `build/windows-x64-release/bin/Release/dsp_tests.exe`
- Run clang-tidy: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`
- Update `specs/_architecture_/layer-2-processors.md` and `specs/_architecture_/README.md` (tasks.md T094, T095, and T094b)

## Key Patterns from Codebase

### Header-only DSP component pattern

All methods are `inline` in the header. See `trance_gate.h`, `sequencer_core.h` for examples.

### Test file structure

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <krate/dsp/processors/arpeggiator_core.h>

using namespace Krate::DSP;

TEST_CASE("ArpeggiatorCore: basic timing", "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    // ...
}
```

### BlockContext construction in tests

```cpp
BlockContext ctx;
ctx.sampleRate = 44100.0;
ctx.blockSize = 512;
ctx.tempoBPM = 120.0;
ctx.isPlaying = true;
ctx.transportPositionSamples = 0;
```

## Reference Numbers

At 44100 Hz, 120 BPM:
- Quarter note: 22050 samples (500ms)
- Eighth note: 11025 samples (250ms)
- Sixteenth note: 5512 samples (125ms)
- Eighth triplet: 7350 samples (166.67ms)

At 44100 Hz, free rate 4.0 Hz:
- Step duration: 11025 samples (250ms)

Swing at 50%, 1/8 note base (11025 samples):
- Even step: 16537 samples (floor(11025 * 1.5) = floor(16537.5) = 16537)
- Odd step: 5512 samples (floor(11025 * 0.5) = floor(5512.5) = 5512)
- Pair sum: 16537 + 5512 = 22049 (NOT 22050 -- integer truncation causes 1-sample discrepancy; tests must allow within-1-sample tolerance on pair sums)
