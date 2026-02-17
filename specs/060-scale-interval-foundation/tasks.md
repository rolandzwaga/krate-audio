# Tasks: Scale & Interval Foundation (ScaleHarmonizer)

**Input**: Design documents from `/specs/060-scale-interval-foundation/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/scale_harmonizer_api.h, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

**Pluginval**: NOT required. This is a DSP library component (Layer 0), not plugin code.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task Group

1. **Write Failing Tests**: Create test file, write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Fix Compiler Warnings**: All code MUST compile with ZERO warnings
4. **Verify**: Build and run tests, confirm they pass
5. **Commit**: Commit the completed work

### Build Commands (Windows)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run only ScaleHarmonizer tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[scale-harmonizer]"

# Run all DSP tests (regression check)
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe
```

---

## Phase 1: Setup

**Purpose**: Register the new test file with the build system and verify the build is clean before writing any code.

- [X] T001 Register `dsp/tests/unit/core/scale_harmonizer_test.cpp` in the `add_executable(dsp_tests ...)` block in `dsp/tests/CMakeLists.txt` (under the `# Layer 0: Core` section, after `unit/core/pitch_utils_test.cpp`)
- [X] T002 Add `unit/core/scale_harmonizer_test.cpp` to the `-fno-fast-math` `set_source_files_properties(...)` block in `dsp/tests/CMakeLists.txt` (the file uses integer arithmetic but follows the project convention of adding all Layer 0 test files to this list)
- [X] T003 Verify the build still succeeds after the CMake change (the test source file does not exist yet, but CMake configure must not error): `"$CMAKE" --preset windows-x64-release`

**Checkpoint**: CMakeLists.txt is updated, build system knows about the test file

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Create the empty test file and the empty header stub so the build can compile. This is the only prerequisite before user story work can begin.

**CRITICAL**: No user story work can begin until this phase is complete.

- [X] T004 Create the empty test file `dsp/tests/unit/core/scale_harmonizer_test.cpp` with only the Catch2 `#include` and a `[scale-harmonizer]` tag — no test cases yet (just the file skeleton so CMake can compile it)
- [X] T005 Verify the project builds cleanly after adding the empty test file: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests`

**Checkpoint**: Build is clean, test infrastructure is ready, user story implementation can begin

---

## Phase 3: User Story 1 - Compute Diatonic Interval for a Scale Degree (Priority: P1) [MVP]

**Goal**: Implement `ScaleHarmonizer::calculate()` for diatonic scales with correct semitone shifts for all 7 scale degrees across all supported keys and scale types. Covers FR-001, FR-002, FR-005, FR-006, FR-007, FR-008, FR-013, FR-014, FR-015, FR-016. Verifies SC-001, SC-002.

**Independent Test**: Run `dsp_tests.exe "[scale-harmonizer][us1]"` to verify all C Major 3rd-above intervals and multi-scale/multi-key exhaustive tests independently of other user stories.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.
> Write these tests in `dsp/tests/unit/core/scale_harmonizer_test.cpp` BEFORE creating `scale_harmonizer.h`.

- [X] T006 [US1] Write failing tests for `ScaleHarmonizer` construction and getters (`getKey()`, `getScale()`, default state = C Major) in `dsp/tests/unit/core/scale_harmonizer_test.cpp` tagged `[scale-harmonizer][us1]`
- [X] T007 [P] [US1] Write failing tests for `getScaleIntervals()` static method: verify the method compiles, is callable as a static constexpr, and returns a `std::array<int, 7>` for each of the 8 diatonic scale types (spot-check Major={0,2,4,5,7,9,11} and one other). Tagged `[scale-harmonizer][us1]` in `dsp/tests/unit/core/scale_harmonizer_test.cpp`. (Note: exhaustive truth-table verification of all 8 scale values is in T047 as an intentional cross-check; T007 establishes compilation and basic contract.)
- [X] T008 [P] [US1] Write failing tests for `calculate()` core interval: C Major 3rd above (diatonicSteps=+2) for all 7 scale degrees C through B, verifying exact semitone shifts from SC-001 table: C->E(+4), D->F(+3), E->G(+3), F->A(+4), G->B(+4), A->C(+3), B->D(+3) in `dsp/tests/unit/core/scale_harmonizer_test.cpp`
- [X] T009 [P] [US1] Write failing tests for `calculate()` multi-scale coverage: 2nd, 3rd, 5th, and octave intervals for all 8 diatonic scale types and all 12 root keys (verifying SC-002: 2688 test cases) in `dsp/tests/unit/core/scale_harmonizer_test.cpp`
- [X] T010 [P] [US1] Write failing tests for `calculate()` octave wrapping: 7th above (diatonicSteps=+6) and octave (diatonicSteps=+7) cross octave boundary correctly, octaveOffset field is correct in `dsp/tests/unit/core/scale_harmonizer_test.cpp`
- [X] T011 [US1] Build and confirm all new tests FAIL (link error or assertion error — implementation does not exist yet): `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests` (expected: compile error or linker error)

### 3.2 Implementation for User Story 1

- [X] T012 [US1] Create `dsp/include/krate/dsp/core/scale_harmonizer.h` with: `#pragma once`, includes (`<array>`, `<cstdint>`, `<algorithm>`, `<cmath>`), `Krate::DSP` namespace, `ScaleType` enum class (uint8_t, values 0-8 per contracts/scale_harmonizer_api.h), `DiatonicInterval` struct (semitones, targetNote, scaleDegree, octaveOffset), and `ScaleHarmonizer` class declaration matching the contract API exactly
- [X] T013 [US1] Implement `constexpr` scale interval tables in `dsp/include/krate/dsp/core/scale_harmonizer.h`: `kScaleIntervals` array-of-arrays for all 8 diatonic scales per FR-002 and data-model.md, plus constexpr reverse lookup tables mapping pitch class offsets (0-11) to nearest scale degree for each scale (used for O(1) non-scale note resolution per research.md R2)
- [X] T014 [US1] Implement `ScaleHarmonizer::getScaleIntervals()` static constexpr method in `dsp/include/krate/dsp/core/scale_harmonizer.h` — returns `std::array<int, 7>` for the given ScaleType
- [X] T015 [US1] Implement `ScaleHarmonizer::setKey()`, `setScale()`, `getKey()`, `getScale()` in `dsp/include/krate/dsp/core/scale_harmonizer.h` — `setKey()` wraps via `rootNote % 12` per data-model.md validation rules
- [X] T016 [US1] Implement `ScaleHarmonizer::calculate()` core algorithm in `dsp/include/krate/dsp/core/scale_harmonizer.h` using the algorithm from research.md and quickstart.md: pitch class extraction, root offset, reverse lookup for scale degree, diatonic step addition with octave-aware modular arithmetic (`/ 7` and `% 7`), semitone shift computation with octave boundary adjustment. Handle diatonicSteps = 0 (unison) and multi-octave intervals (FR-007, FR-008). All methods must be `noexcept` (FR-014).
- [X] T017 [US1] Build after implementation: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests` — fix ALL compiler warnings before proceeding (zero-warnings policy)
- [X] T018 [US1] Run US1 tests and verify they pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[scale-harmonizer][us1]"`

### 3.3 Cross-Platform Verification

- [X] T019 [US1] Verify `scale_harmonizer_test.cpp` is already in the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (done in T002). Confirm the test file uses only integer arithmetic and does not need `std::isnan()`/`std::isfinite()` — `ScaleHarmonizer` is integer-only, no floating-point IEEE 754 concerns for the core path.

### 3.4 Commit

- [X] T020 [US1] **Run all DSP tests to verify no regressions**: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [X] T021 [US1] **Commit completed User Story 1 work** (files: `dsp/include/krate/dsp/core/scale_harmonizer.h`, `dsp/tests/unit/core/scale_harmonizer_test.cpp`, `dsp/tests/CMakeLists.txt`)

**Checkpoint**: US1 is fully functional. ScaleHarmonizer correctly computes diatonic intervals for all 8 scales, all 12 keys, with correct octave wrapping. SC-001 and SC-002 pass.

---

## Phase 4: User Story 2 - Handle Non-Scale (Chromatic Passing) Notes (Priority: P2)

**Goal**: Implement correct behavior when input notes are not in the current scale. The nearest scale degree is used, with round-down on ties. Covers FR-004. Verifies SC-003.

**Independent Test**: Run `dsp_tests.exe "[scale-harmonizer][us2]"` to verify all chromatic passing tone tests independently.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.
> US2 tests may pass already if calculate() from US1 already handles this via the reverse lookup table. Write the tests first and check.

- [X] T022 [US2] Write failing tests for non-scale note handling in `dsp/tests/unit/core/scale_harmonizer_test.cpp` tagged `[scale-harmonizer][us2]`: C Major, C#4 (MIDI 61) with 3rd above must return same shift as C4 (+4 semitones); Eb4 (MIDI 63) must return nearest degree's interval; all 5 chromatic passing tones in C Major must use nearest scale degree (SC-003)
- [X] T023 [US2] Write failing test for tie-breaking rule: notes equidistant between two scale degrees (e.g., C# in C Major is equidistant from C and D) must round DOWN to the lower scale degree per FR-004 and spec Clarifications
- [X] T024 [US2] Build and run: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[scale-harmonizer][us2]"` — if tests already pass from US1 implementation (reverse lookup table handles this), document and proceed; if not, implement the fix

### 4.2 Implementation for User Story 2 (if needed after T024)

- [X] T025 [US2] If T024 reveals failures: verify the constexpr reverse lookup tables built in T013 use the correct tie-breaking rule (round down = prefer the degree with the lower index when distances are equal). Fix the table generation algorithm in `dsp/include/krate/dsp/core/scale_harmonizer.h` if needed. -- NOTE: All US2 tests passed immediately from US1 implementation. No fixes needed.
- [X] T026 [US2] Build and fix ALL compiler warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests` -- ZERO warnings
- [X] T027 [US2] Run US2 tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[scale-harmonizer][us2]"` -- 26 assertions in 2 test cases, all passed

### 4.3 Commit

- [X] T028 [US2] **Run all DSP tests**: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe` -- 21,924,086 assertions in 5482 test cases, all passed
- [X] T029 [US2] **Commit completed User Story 2 work** -- ready to commit (tests added, no implementation changes needed)

**Checkpoint**: Non-scale input notes are handled correctly for 100% of chromatic passing tone cases (SC-003).

---

## Phase 5: User Story 3 - Chromatic (Fixed Shift) Mode (Priority: P3)

**Goal**: Implement `ScaleType::Chromatic` mode where `diatonicSteps` is returned directly as the semitone shift with `scaleDegree = -1`. Covers FR-003. Verifies SC-005.

**Independent Test**: Run `dsp_tests.exe "[scale-harmonizer][us3]"` to verify chromatic mode independently.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written before implementation.

- [X] T030 [US3] Write failing tests for Chromatic mode in `dsp/tests/unit/core/scale_harmonizer_test.cpp` tagged `[scale-harmonizer][us3]`: diatonicSteps=+7 for any input note always returns +7 semitones; diatonicSteps=-5 always returns -5; key setting has no effect on result (per US3 acceptance scenarios)
- [X] T031 [US3] Write failing test: in Chromatic mode, `DiatonicInterval.scaleDegree` is always -1 per FR-003 and spec Clarifications
- [X] T032 [US3] Build and confirm tests FAIL: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests` -- NOTE: All tests PASSED immediately. Chromatic mode was already implemented in Phase 3 (calculate() lines 210-218).

### 5.2 Implementation for User Story 3

- [X] T033 [US3] Add Chromatic mode branch to `ScaleHarmonizer::calculate()` in `dsp/include/krate/dsp/core/scale_harmonizer.h`: when `scale_ == ScaleType::Chromatic`, return `DiatonicInterval{diatonicSteps, clamp(inputMidiNote + diatonicSteps, 0, 127), -1, 0}` directly (no scale logic, octaveOffset is 0 for passthrough mode per FR-003) -- NOTE: Already implemented in Phase 3 (lines 210-218 of scale_harmonizer.h). No changes needed.
- [X] T034 [US3] Build and fix ALL compiler warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests` -- ZERO warnings
- [X] T035 [US3] Run US3 tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[scale-harmonizer][us3]"` -- 259 assertions in 2 test cases, all passed

### 5.3 Commit

- [X] T036 [US3] **Run all DSP tests**: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe` -- 21,924,345 assertions in 5484 test cases, all passed (1 failed as expected = pre-existing sigmoid speedup benchmark)
- [X] T037 [US3] **Commit completed User Story 3 work** -- ready to commit (tests added, no implementation changes needed; chromatic mode was already in Phase 3)

**Checkpoint**: Chromatic passthrough mode works correctly, SC-005 passes.

---

## Phase 6: User Story 4 - Query Scale Membership and Quantization (Priority: P4)

**Goal**: Implement `getScaleDegree()` and `quantizeToScale()` utility query methods. Covers FR-010, FR-011. Verifies SC-003 (partially, via quantize semantics).

**Independent Test**: Run `dsp_tests.exe "[scale-harmonizer][us4]"` to verify scale membership and quantization independently.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written before implementation.

- [X] T038 [P] [US4] Write failing tests for `getScaleDegree()` in `dsp/tests/unit/core/scale_harmonizer_test.cpp` tagged `[scale-harmonizer][us4]`: C Major root C4 (MIDI 60) -> degree 0; D4 (MIDI 62) -> degree 1; C#4 (MIDI 61, not in C Major) -> -1; all 12 pitch classes tested for C Major membership per US4 acceptance scenarios
- [X] T039 [P] [US4] Write failing tests for `quantizeToScale()`: C#4 (MIDI 61) quantizes to C4 (60) or D4 (62), whichever nearer (C4 wins by round-down tie rule); test several chromatic notes in C Major; in Chromatic mode, returns input unchanged per API contract
- [X] T040 [US4] Build and confirm tests FAIL: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests` -- 40 failures confirmed (stubs return wrong values)

### 6.2 Implementation for User Story 4

- [X] T041 [P] [US4] Implement `ScaleHarmonizer::getScaleDegree()` in `dsp/include/krate/dsp/core/scale_harmonizer.h`: compute pitch class offset from root, check if it matches any entry in the scale intervals array exactly (return degree index 0-6), otherwise return -1. In Chromatic mode, always return -1 per FR-010.
- [X] T042 [P] [US4] Implement `ScaleHarmonizer::quantizeToScale()` in `dsp/include/krate/dsp/core/scale_harmonizer.h`: use the reverse lookup table to find nearest scale degree for the input pitch class, then compute the actual MIDI note of that degree in the same octave. In Chromatic mode, return input unchanged per API contract.
- [X] T043 [US4] Build and fix ALL compiler warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests` -- ZERO warnings
- [X] T044 [US4] Run US4 tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[scale-harmonizer][us4]"` -- 107 assertions in 2 test cases, all passed

### 6.3 Commit

- [X] T045 [US4] **Run all DSP tests**: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe` -- 21,924,452 assertions in 5486 test cases, all passed
- [X] T046 [US4] **Commit completed User Story 4 work** -- verified ready, not committed per instructions

**Checkpoint**: Scale membership queries and quantization work correctly for all 12 keys and all 8 diatonic scales.

---

## Phase 7: User Story 5 - Support All 8 Scale Types Plus Chromatic (Priority: P5)

**Goal**: Verify complete multi-scale correctness for all 8 diatonic scale types — this is largely already implemented in Phase 3 (US1) via the `kScaleIntervals` table. This phase adds exhaustive per-scale interval correctness tests as explicit verification. Covers FR-002. Verifies SC-002.

**Independent Test**: Run `dsp_tests.exe "[scale-harmonizer][us5]"` to verify all scale interval tables independently.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL if tables are wrong)

> These tests verify the scale data itself, independent of the interval calculation algorithm.

- [X] T047 [US5] Write exhaustive scale interval truth-table tests in `dsp/tests/unit/core/scale_harmonizer_test.cpp` tagged `[scale-harmonizer][us5]`: for each of the 8 diatonic scale types, call `getScaleIntervals()` and verify the returned array matches the exact values from FR-002/data-model.md (Major={0,2,4,5,7,9,11}, NaturalMinor={0,2,3,5,7,8,10}, HarmonicMinor={0,2,3,5,7,8,11}, MelodicMinor={0,2,3,5,7,9,11}, Dorian={0,2,3,5,7,9,10}, Mixolydian={0,2,4,5,7,9,10}, Phrygian={0,1,3,5,7,8,10}, Lydian={0,2,4,6,7,9,11}). Also call `getScaleIntervals(ScaleType::Chromatic)` and verify it returns `{0, 1, 2, 3, 4, 5, 6}` per FR-013. (These assertions intentionally cross-check T007's spot-check as a completeness guarantee.)
- [X] T048 [US5] Write a cross-key correctness test: pick one non-trivial scale (e.g., Dorian) and verify 3rd-above intervals for all 7 scale degrees in all 12 root keys produce musically correct results (cross-reference against music theory for key of D Dorian as a spot check)
- [X] T049 [US5] Build and run: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[scale-harmonizer][us5]"` — these should pass from US1 implementation; if any fail, fix the scale tables in `dsp/include/krate/dsp/core/scale_harmonizer.h` -- All 373 assertions in 2 test cases passed. No table fixes needed.

### 7.2 Commit

- [X] T050 [US5] **Run all DSP tests**: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe` -- 21,924,825 assertions in 5488 test cases, all passed
- [X] T051 [US5] **Commit completed User Story 5 work** (test additions only, no table fixes needed) -- verified ready to commit

**Checkpoint**: All 8 scale types verified correct against music theory reference tables (SC-002).

---

## Phase 8: User Story 6 - Negative Intervals (Harmony Below) (Priority: P6)

**Goal**: Verify and test that negative `diatonicSteps` values traverse the scale downward with correct octave boundary handling. Covers FR-001 (negative direction), FR-007, FR-008. Verifies SC-004, SC-006.

**Independent Test**: Run `dsp_tests.exe "[scale-harmonizer][us6]"` to verify negative interval calculation independently.

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL if not yet implemented)

> **Constitution Principle XIII**: Tests MUST be written before confirming implementation is correct.

- [X] T052 [P] [US6] Write failing tests for negative intervals in `dsp/tests/unit/core/scale_harmonizer_test.cpp` tagged `[scale-harmonizer][us6]`: C Major, diatonicSteps=-2 (3rd below) for E4 (MIDI 64) -> -4 semitones (target C4, MIDI 60); diatonicSteps=-2 for C4 (MIDI 60) -> -3 semitones (target A3, MIDI 57, wraps below octave) per US6 acceptance scenarios
- [X] T053 [P] [US6] Write failing tests for octave-exact negative intervals: diatonicSteps=-7 (octave below) for C5 (MIDI 72) -> -12 semitones (target C4, MIDI 60); octaveOffset=-1 in result
- [X] T054 [P] [US6] Write failing tests for multi-octave negative intervals (SC-006): diatonicSteps=-9, -14 for known input notes produce correct results with correct octaveOffset
- [X] T055 [US6] Build and confirm tests FAIL (or pass from US1 implementation — check): `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests` -- All 72 assertions in 3 test cases PASSED from US1 implementation. No fixes needed.

### 8.2 Implementation for User Story 6 (if needed after T055)

- [X] T056 [US6] If T055 reveals failures: fix the negative-steps algorithm branch in `ScaleHarmonizer::calculate()` in `dsp/include/krate/dsp/core/scale_harmonizer.h`. Key concern: C++ `%` operator truncates toward zero for negative values — use `((d + r % 7) + 7) % 7` pattern for correct positive modulo per research.md R4. Verify octave adjustment logic handles downward wrapping. -- NOTE: All US6 tests passed immediately from US1 implementation. No fixes needed.
- [X] T057 [US6] Build and fix ALL compiler warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests` -- ZERO warnings
- [X] T058 [US6] Run US6 tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[scale-harmonizer][us6]"` -- 72 assertions in 3 test cases, all passed

### 8.3 Commit

- [X] T059 [US6] **Run all DSP tests**: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe` -- 21,924,897 assertions in 5491 test cases, all passed
- [X] T060 [US6] **Commit completed User Story 6 work** -- verified ready to commit (tests added, no implementation changes needed)

**Checkpoint**: Negative intervals (harmony below) produce correct results including octave boundary wrapping (SC-004, SC-006).

---

## Phase 9: Edge Cases and Remaining Requirements

**Purpose**: Cover the remaining FRs not fully exercised by user story phases: FR-009 (MIDI clamping), FR-012 (frequency interface), and edge cases from the spec.

**Goal**: Verifies SC-007 (MIDI clamping), SC-008 (no allocations/noexcept).

### 9.1 Tests for Edge Cases (Write FIRST - Must FAIL)

- [ ] T061 [P] Write failing tests for MIDI note boundary clamping (FR-009, SC-007) in `dsp/tests/unit/core/scale_harmonizer_test.cpp` tagged `[scale-harmonizer][edge]`: input MIDI 127 + large positive interval clamps targetNote to 127; input MIDI 0 + large negative interval clamps targetNote to 0; semitones field reflects the clamped shift
- [ ] T062 [P] Write failing tests for unison (diatonicSteps=0): result always has semitones=0, octaveOffset=0, targetNote=inputMidiNote for all scales and keys; also verify scaleDegree matches `getScaleDegree(inputMidiNote)` for scale notes (e.g., C4 in C Major → scaleDegree=0) and equals -1 for non-scale input notes in diatonic mode (per FR-006: scaleDegree is the target note's scale position, which equals the input's position for unison)
- [ ] T063 [P] Write failing tests for `getSemitoneShift()` frequency convenience method (FR-012) in `dsp/tests/unit/core/scale_harmonizer_test.cpp`: 440.0f Hz (A4=MIDI69) in C Major with diatonicSteps=+2 returns the same shift as `calculate(69, +2).semitones`; verify `frequencyToMidiNote()` is called and rounded to nearest integer
- [ ] T064 Write tests verifying all methods compile as `noexcept` (can be done via `static_assert(noexcept(...))` calls in test) and confirm no heap allocations occur (SC-008 — verified by code inspection and static analysis)

### 9.2 Implementation for Edge Cases

- [ ] T065 [P] Implement MIDI note clamping in `ScaleHarmonizer::calculate()` in `dsp/include/krate/dsp/core/scale_harmonizer.h`: after computing targetNote, apply `std::clamp(targetNote, kMinMidiNote, kMaxMidiNote)` (using `kMinMidiNote`/`kMaxMidiNote` from `dsp/include/krate/dsp/core/midi_utils.h`); recompute `semitones = clampedNote - inputMidiNote` to ensure invariant holds (per research.md R5)
- [ ] T066 [P] Implement `ScaleHarmonizer::getSemitoneShift()` in `dsp/include/krate/dsp/core/scale_harmonizer.h`: call `frequencyToMidiNote(inputFrequencyHz)` from `dsp/include/krate/dsp/core/pitch_utils.h`, round via `static_cast<int>(std::round(...))`, then call `calculate()` and return `static_cast<float>(result.semitones)` — all noexcept, zero allocations (FR-012)
- [ ] T067 Verify the `#include` chain in `scale_harmonizer.h`: add `#include <krate/dsp/core/pitch_utils.h>` and `#include <krate/dsp/core/midi_utils.h>` if not already present
- [ ] T068 Build and fix ALL compiler warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T069 Run edge case tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[scale-harmonizer][edge]"`
- [ ] T070 Run complete ScaleHarmonizer test suite: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[scale-harmonizer]"` — all tests must pass

### 9.3 Commit

- [ ] T071 **Run all DSP tests**: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [ ] T072 **Commit edge cases and remaining FR implementation**

**Checkpoint**: All edge case requirements implemented. SC-007 (clamping) and SC-008 (noexcept/zero-alloc) verified. (FR-015 and FR-016 compliance verification is in Phase 10.)

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Final code quality, static analysis, and documentation.

### 10.1 Static Analysis (Clang-Tidy)

- [ ] T073 Generate compile_commands.json if not current (run from VS Developer PowerShell): `cmake --preset windows-ninja`
- [ ] T074 Run clang-tidy on the new DSP source file: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`
- [ ] T075 Fix ALL errors reported by clang-tidy in `dsp/include/krate/dsp/core/scale_harmonizer.h`
- [ ] T076 Review clang-tidy warnings and fix where appropriate; add `// NOLINT(...)` with reason for any intentionally ignored warnings (e.g., DSP-friendly magic numbers that clang-tidy flags but are correct by spec)

### 10.2 Final Regression

- [ ] T077 Run the complete DSP test suite one final time: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe` — zero failures

### 10.3 Architecture Documentation (MANDATORY per Constitution Principle XIV)

- [ ] T078 Update `specs/_architecture_/layer-0-core.md`: add a `ScaleHarmonizer` section documenting the component (purpose, public API summary, `ScaleType` enum values with their semitone arrays, `DiatonicInterval` struct fields, file location `dsp/include/krate/dsp/core/scale_harmonizer.h`, when to use, dependencies on `pitch_utils.h` and `midi_utils.h`, note that it is consumed by future HarmonizerEngine Phase 4)

### 10.4 Requirements Compliance Verification (FR-015, FR-016)

- [ ] T079 **Verify FR-015 (immutable/thread-safe)**: By code inspection, confirm that all query methods (`calculate()`, `getScaleDegree()`, `quantizeToScale()`, `getSemitoneShift()`, `getScaleIntervals()`) have no mutable state. Add a comment block in `dsp/tests/unit/core/scale_harmonizer_test.cpp` documenting why concurrent reads are safe: (1) all write operations are `setKey()`/`setScale()` which the host guarantees are not called during `process()`, (2) all query methods are `const` and modify no shared state, (3) no mutable caches or lazy-computed fields exist. Reference FR-015 in the comment.
- [ ] T080 **Verify FR-016 (Layer 0 dependency rule)**: Inspect the `#include` directives in `dsp/include/krate/dsp/core/scale_harmonizer.h` and confirm that only standard library headers and Layer 0 headers (`pitch_utils.h`, `midi_utils.h`) are included. No Layer 1+ headers (primitives/, processors/, systems/, effects/) are permitted. Document the verified include list in a code comment or in the architecture doc.

### 10.5 Commit

- [ ] T081 **Commit static analysis fixes, architecture documentation, and compliance verification notes**

**Checkpoint**: Code is clang-tidy clean, architecture documentation is updated, FR-015 and FR-016 are explicitly verified.

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion (Constitution Principle XVI).

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T082 **Review ALL FR-001 through FR-016** from `specs/060-scale-interval-foundation/spec.md` against the implementation in `dsp/include/krate/dsp/core/scale_harmonizer.h`. For each FR: open the file, find the code that satisfies it, record file path and relevant method.
- [ ] T083 **Review ALL SC-001 through SC-008** by running the tests and verifying measured results:
  - SC-001: Run `[scale-harmonizer][us1]`, verify C Major 3rd-above reference table passes
  - SC-002: Count test cases — 8 scales x 12 keys x 4 intervals x 7 degrees = 2688, all pass
  - SC-003: Run `[scale-harmonizer][us2]`, verify 100% of chromatic passing tone cases pass
  - SC-004: Run `[scale-harmonizer][us6]`, verify negative interval tests pass
  - SC-005: Run `[scale-harmonizer][us3]`, verify chromatic mode passthrough tests pass
  - SC-006: Verify multi-octave interval tests (diatonicSteps=+9, +14, -9) pass
  - SC-007: Verify MIDI boundary clamping tests pass (input 0 and 127)
  - SC-008: Verify by code inspection that all methods are noexcept and no heap allocations occur
- [ ] T084 **Search for cheating patterns** in `dsp/include/krate/dsp/core/scale_harmonizer.h`:
  - [ ] No `// placeholder` or `// TODO` comments
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [ ] T085 **Update `specs/060-scale-interval-foundation/spec.md` "Implementation Verification" section** with compliance status for each FR and SC requirement. Fill the evidence column with specific file paths, method names, test names, and actual measured values — not generic claims like "implemented" or "test passes".
- [ ] T086 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Final Commit

- [ ] T087 **Commit the filled compliance table in spec.md**
- [ ] T088 **Verify all spec work is committed to `060-scale-interval-foundation` branch**: `git status` should show clean

**Checkpoint**: Honest assessment complete. Spec is done only if ALL requirements are MET with specific evidence.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — can start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 completion — BLOCKS all user stories
- **Phase 3 (US1)**: Depends on Phase 2 — MVP, implement first
- **Phase 4 (US2)**: Can start immediately after Phase 2; likely already passes after Phase 3 (US2 uses the same reverse lookup table built in US1)
- **Phase 5 (US3)**: Can start after Phase 2, independent of US2
- **Phase 6 (US4)**: Can start after Phase 2, independent of US2/US3
- **Phase 7 (US5)**: Can start after Phase 3 (relies on getScaleIntervals from US1)
- **Phase 8 (US6)**: Can start after Phase 2, but shares the calculate() method with US1 — implement after Phase 3 to avoid conflicts
- **Phase 9 (Edge Cases)**: Depends on Phases 3, 5, and 8 (builds on calculate() from US1, Chromatic from US3, negative intervals from US6)
- **Phase 10 (Polish)**: Depends on all user story phases complete
- **Phase 11 (Verification)**: Depends on Phase 10

### User Story Dependencies

- **US1 (P1)**: Independent — foundation of everything, implement first
- **US2 (P2)**: Independent, but reuses reverse lookup tables from US1 implementation
- **US3 (P3)**: Independent of US2 (Chromatic is a separate branch in calculate())
- **US4 (P4)**: Independent — adds new methods, no conflict with US1-US3
- **US5 (P5)**: Independent — verifies scale tables from US1
- **US6 (P6)**: Shares calculate() with US1 — implement after US1 to avoid merge conflicts

### Within Each User Story

1. Write tests that FAIL (Constitution Principle XIII)
2. Implement to make tests pass
3. Fix ALL compiler warnings (zero-warning policy)
4. Build and run tests
5. Run all DSP tests (regression check)
6. Commit

### Parallel Opportunities

The following test-writing tasks are parallelizable (different test sections in the same file):

```
Parallel within Phase 3 test-writing:
  T007 - getScaleIntervals() tests
  T008 - C Major 3rd-above reference table tests
  T009 - Multi-scale exhaustive tests
  T010 - Octave wrapping tests

Parallel within Phase 4 implementation:
  T041 - getScaleDegree() implementation
  T042 - quantizeToScale() implementation

Parallel within Phase 9:
  T061 - MIDI boundary clamping tests
  T062 - Unison tests
  T063 - getSemitoneShift() tests
```

---

## Parallel Execution Example: User Story 1

```bash
# All of these can be written simultaneously (different test cases in same file):
# T007: Write getScaleIntervals() tests
# T008: Write C Major 3rd-above reference table tests
# T009: Write multi-scale exhaustive correctness tests
# T010: Write octave wrapping tests

# Then sequential:
# T011: Build to confirm FAIL
# T012-T016: Implement (sequential — all in one header file)
# T017: Build and fix warnings
# T018: Run and verify
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (CMakeLists.txt)
2. Complete Phase 2: Foundational (empty file stub)
3. Complete Phase 3: User Story 1 (ScaleHarmonizer::calculate() for diatonic scales)
4. **STOP and VALIDATE**: Run `dsp_tests.exe "[scale-harmonizer][us1]"` independently
5. All core musical intelligence for harmonizer use is delivered at this point

### Incremental Delivery

1. Setup + Foundational → Build infrastructure ready
2. Add US1 (Diatonic Intervals) → Test independently → MVP complete
3. Add US2 (Non-scale notes) → Test independently → Robustness for real audio
4. Add US3 (Chromatic mode) → Test independently → Fixed-shift passthrough
5. Add US4 (Queries) → Test independently → UI/downstream support
6. Add US5 (All scale types) → Verify table completeness
7. Add US6 (Negative intervals) → Full bidirectional harmony
8. Edge cases + polish → Production ready

### Key Files Summary

| File | Action |
|------|--------|
| `dsp/include/krate/dsp/core/scale_harmonizer.h` | CREATE: ~250-350 lines, header-only implementation |
| `dsp/tests/unit/core/scale_harmonizer_test.cpp` | CREATE: ~600-900 lines, comprehensive Catch2 test suite |
| `dsp/tests/CMakeLists.txt` | MODIFY: add test source file and -fno-fast-math entry |
| `specs/_architecture_/layer-0-core.md` | MODIFY: add ScaleHarmonizer documentation section |
| `specs/060-scale-interval-foundation/spec.md` | MODIFY: fill Implementation Verification table |

---

## Notes

- `[P]` tasks = different test cases, can be written in parallel
- `[USN]` label maps task to specific user story for traceability
- **Pluginval is NOT required** — this is a DSP library component (Layer 0), not plugin code
- Skills auto-load when needed (testing-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Constitution Principle XIII)
- **MANDATORY**: Zero compiler warnings before proceeding (project policy)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-0-core.md` before spec completion (Constitution Principle XIV)
- **MANDATORY**: Complete honesty verification and fill compliance table before claiming spec complete (Constitution Principle XVI)
- The `ScaleHarmonizer` is Layer 0 — it MUST NOT depend on anything above Layer 0. Allowed dependencies: stdlib only, plus `pitch_utils.h` (Layer 0) and `midi_utils.h` (Layer 0)
- The C++ `%` operator truncates toward zero for negative values. Use `((x % 7) + 7) % 7` pattern for correct positive modulo in downward scale traversal (research.md R4 gotcha)
- `frequencyToMidiNote()` returns `float` — MUST `std::round()` and cast to `int` before passing to `calculate()` (plan.md Dependency API Contracts table)
