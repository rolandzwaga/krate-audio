# Tasks: Note Event Processor (036)

**Input**: Design documents from `F:\projects\iterum\specs\036-note-event-processor\`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/note_processor_api.h, research.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Mandatory: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/note_processor_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure

- [X] T001 Create test file `dsp/tests/unit/processors/note_processor_test.cpp` (stub, no tests yet)
- [X] T002 Add test file to `dsp/tests/CMakeLists.txt` in dsp_tests target
- [X] T003 Add header placeholder `dsp/include/krate/dsp/processors/note_processor.h` to `dsp/CMakeLists.txt` in KRATE_DSP_PROCESSORS_HEADERS

**Checkpoint**: Basic project structure ready for test-first development

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Layer 0 additions that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete. All user stories depend on these Layer 0 utilities.

### 2.1 Layer 0 Velocity Utilities (Write Tests FIRST)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T004 [P] Write failing unit tests for `VelocityCurve` enum and `mapVelocity()` in `dsp/tests/unit/processors/note_processor_test.cpp` (test all four curve types: Linear, Soft, Hard, Fixed)
- [X] T005 [P] Verify tests FAIL (no implementation yet)

### 2.2 Layer 0 Velocity Implementation

- [X] T006 Add `VelocityCurve` enum to `dsp/include/krate/dsp/core/midi_utils.h` with values: Linear, Soft, Hard, Fixed
- [X] T007 Add `mapVelocity(int velocity, VelocityCurve curve)` free function to `dsp/include/krate/dsp/core/midi_utils.h` implementing all four curves
- [X] T008 Verify all Layer 0 velocity tests pass
- [ ] T009 Commit Layer 0 velocity utilities (SKIPPED per user instruction)

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Note-to-Frequency Conversion with Tunable Reference (Priority: P1) - MVP

**Goal**: Convert MIDI note numbers (0-127) to frequencies using 12-TET with configurable A4 tuning reference (400-480 Hz)

**Independent Test**: Can be fully tested by sending known MIDI note numbers and verifying the returned frequencies match the 12-TET formula within floating-point precision (SC-001, SC-005)

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T010 [US1] Write failing tests for NoteProcessor constructor and basic initialization in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T011 [US1] Write failing tests for `prepare(double sampleRate)` in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T012 [US1] Write failing tests for `setTuningReference(float hz)` and `getTuningReference()` with NaN/Inf validation (FR-002) in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T013 [US1] Write failing tests for `getFrequency(uint8_t note)` with default tuning (A4=440Hz) covering full MIDI range 0-127 (FR-001, SC-001) in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T014 [US1] Write failing tests for `getFrequency(uint8_t note)` with various A4 references: 432Hz, 442Hz, 443Hz, 444Hz (SC-005) in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T015 [US1] Write failing tests for tuning reference edge cases: out-of-range finite values (should clamp to 400-480Hz), NaN/Inf (should reset to 440Hz) in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T016 [US1] Verify all User Story 1 tests FAIL (no implementation yet)

### 3.2 Implementation for User Story 1

- [X] T017 [US1] Create `NoteProcessor` class skeleton in `dsp/include/krate/dsp/processors/note_processor.h` with member variables: `a4Reference_` (default 440.0f), `sampleRate_` (default 44100.0f)
- [X] T018 [US1] Implement default constructor in `dsp/include/krate/dsp/processors/note_processor.h`
- [X] T019 [US1] Implement `prepare(double sampleRate)` in `dsp/include/krate/dsp/processors/note_processor.h` storing sample rate for future smoother configuration
- [X] T020 [US1] Implement `setTuningReference(float hz)` in `dsp/include/krate/dsp/processors/note_processor.h` with validation: clamp finite values to [400, 480], reset NaN/Inf to 440.0 using `detail::isNaN()` and `detail::isInf()` from `db_utils.h`
- [X] T021 [US1] Implement `getTuningReference()` in `dsp/include/krate/dsp/processors/note_processor.h` returning `a4Reference_`
- [X] T022 [US1] Implement `getFrequency(uint8_t note)` in `dsp/include/krate/dsp/processors/note_processor.h` using `midiNoteToFrequency(note, a4Reference_)` from `midi_utils.h` (no pitch bend yet - returns base frequency only)
- [X] T023 [US1] Verify all User Story 1 tests pass
- [X] T024 [US1] Fix any compiler warnings

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T025 [US1] Verify IEEE 754 compliance: Add `unit/processors/note_processor_test.cpp` to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (tests use NaN/Inf detection)

### 3.4 Commit (MANDATORY)

- [ ] T026 [US1] Commit completed User Story 1 work (SKIPPED per user instruction)

**Checkpoint**: User Story 1 should be fully functional, tested, and committed. `getFrequency()` returns correct frequencies for any MIDI note and A4 reference.

---

## Phase 4: User Story 2 - Pitch Bend with Smoothing (Priority: P1)

**Goal**: Apply pitch bend (-1.0 to +1.0 bipolar) with configurable range (0-24 semitones) and exponential smoothing to prevent zipper noise

**Independent Test**: Can be tested by setting pitch bend values and verifying the resulting frequency offset matches the configured range, and that rapid changes produce smooth output without discontinuities (SC-002, SC-003)

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T027 [US2] Write failing tests for `setPitchBend(float bipolar)` in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T028 [US2] Write failing tests for `processPitchBend()` advancing smoother and returning smoothed value in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T029 [US2] Write failing tests for `setPitchBendRange(float semitones)` clamping to [0, 24] in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T030 [US2] Write failing tests for `setSmoothingTime(float ms)` in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T031 [US2] Write failing tests for `getFrequency(uint8_t note)` with pitch bend applied: bipolar +1.0 and -1.0 at 2-semitone range (SC-002) in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T032 [US2] Write failing tests for pitch bend at 12-semitone range (one octave) in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T033 [US2] Write failing tests for pitch bend smoothing: instantaneous jump from 0.0 to 1.0 should converge exponentially within 5ms to 99% (SC-003) in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T034 [US2] Write failing tests for pitch bend edge cases: NaN/Inf input should be ignored (FR-020), zero range should have no effect, neutral (0.0) should produce no offset in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T034b [US2] Write failing test verifying NaN/Inf guard ordering: after setting a valid pitch bend (e.g., 0.5), sending NaN must NOT reset the smoother state to 0.0 — the smoothed value must remain at the last valid state. This verifies that NoteProcessor validates inputs BEFORE calling `OnePoleSmoother::setTarget()` (FR-020, plan.md R10 gotcha)
- [X] T035 [US2] Write failing tests for `reset()` snapping pitch bend smoother to 0.0 (center) in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T035b [US2] Write failing tests for extreme frequency edge cases: note 0 with -24 semitone bend (maximum downward), note 127 with +24 semitone bend (maximum upward) — verify output is positive, finite, and does not overflow (spec edge cases)
- [X] T036 [US2] Write failing tests for `prepare()` called mid-transition: should preserve current/target, recalculate coefficient only (FR-003) in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T037 [US2] Verify all User Story 2 tests FAIL (no implementation yet)

### 4.2 Implementation for User Story 2

- [X] T038 [US2] Add member variables to `NoteProcessor` in `dsp/include/krate/dsp/processors/note_processor.h`: `bendSmoother_` (OnePoleSmoother), `currentBendSemitones_` (float, default 0.0), `currentBendRatio_` (float, default 1.0), `pitchBendRange_` (float, default 2.0), `smoothingTimeMs_` (float, default 5.0)
- [X] T039 [US2] Add `#include <krate/dsp/primitives/smoother.h>` to `dsp/include/krate/dsp/processors/note_processor.h`
- [X] T040 [US2] Add `#include <krate/dsp/core/pitch_utils.h>` to `dsp/include/krate/dsp/processors/note_processor.h` for `semitonesToRatio()`
- [X] T041 [US2] Update constructor to initialize smoother with `configure(5.0f, 44100.0f)` in `dsp/include/krate/dsp/processors/note_processor.h`
- [X] T042 [US2] Update `prepare(double sampleRate)` to call `bendSmoother_.setSampleRate(static_cast<float>(sampleRate))` (preserves current/target, recalculates coefficient) in `dsp/include/krate/dsp/processors/note_processor.h`
- [X] T043 [US2] Implement `setPitchBend(float bipolar)` in `dsp/include/krate/dsp/processors/note_processor.h`: validate with `detail::isNaN()` and `detail::isInf()`, ignore if invalid (FR-020), otherwise call `bendSmoother_.setTarget(bipolar)`
- [X] T044 [US2] Implement `processPitchBend()` in `dsp/include/krate/dsp/processors/note_processor.h`: advance smoother with `process()`, compute `currentBendSemitones_ = smoothedBend * pitchBendRange_`, pre-cache `currentBendRatio_ = semitonesToRatio(currentBendSemitones_)`, return smoothed bipolar value
- [X] T045 [US2] Update `getFrequency(uint8_t note)` in `dsp/include/krate/dsp/processors/note_processor.h` to multiply base frequency by `currentBendRatio_`
- [X] T046 [US2] Implement `setPitchBendRange(float semitones)` in `dsp/include/krate/dsp/processors/note_processor.h` clamping to [0.0, 24.0]
- [X] T047 [US2] Implement `setSmoothingTime(float ms)` in `dsp/include/krate/dsp/processors/note_processor.h` calling `bendSmoother_.configure(ms, sampleRate_)`
- [X] T048 [US2] Implement `reset()` in `dsp/include/krate/dsp/processors/note_processor.h` calling `bendSmoother_.snapTo(0.0f)`, resetting `currentBendSemitones_ = 0.0f`, `currentBendRatio_ = 1.0f`
- [X] T049 [US2] Verify all User Story 2 tests pass
- [X] T050 [US2] Fix any compiler warnings

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T051 [US2] Verify IEEE 754 compliance: Confirm `unit/processors/note_processor_test.cpp` is already in `-fno-fast-math` list (added in US1)

### 4.4 Commit (MANDATORY)

- [ ] T052 [US2] Commit completed User Story 2 work (SKIPPED per user instruction)

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed. `getFrequency()` now returns pitch-bent frequencies with smooth transitions.

---

## Phase 5: User Story 3 - Velocity Curve Mapping (Priority: P2)

**Goal**: Map MIDI velocity (0-127) through four curve types (Linear, Soft, Hard, Fixed) to produce normalized gain values (0.0-1.0)

**Independent Test**: Can be tested by mapping all 128 velocity values through each curve type and verifying the output ranges and curve shapes match their mathematical definitions (SC-004)

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T053 [US3] Write failing tests for `setVelocityCurve(VelocityCurve curve)` in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T054 [US3] Write failing tests for `mapVelocity(int velocity)` with Linear curve: verify velocity 127 maps to 1.0, velocity 64 maps to ~0.504 (SC-004) in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T055 [US3] Write failing tests for `mapVelocity(int velocity)` with Soft curve: verify velocity 64 maps to >0.504 (~0.710) in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T056 [US3] Write failing tests for `mapVelocity(int velocity)` with Hard curve: verify velocity 64 maps to <0.504 (~0.254) in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T057 [US3] Write failing tests for `mapVelocity(int velocity)` with Fixed curve: verify any non-zero velocity maps to 1.0 in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T058 [US3] Write failing tests for velocity edge cases: velocity 0 always maps to 0.0 regardless of curve (FR-015), out-of-range velocities clamped to [0, 127] (FR-016) in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T059 [US3] Verify all User Story 3 tests FAIL (no implementation yet)

### 5.2 Implementation for User Story 3

- [X] T060 [US3] Add member variable `velocityCurve_` (VelocityCurve, default Linear) to `NoteProcessor` in `dsp/include/krate/dsp/processors/note_processor.h`
- [X] T061 [US3] Add `#include <krate/dsp/core/midi_utils.h>` to `dsp/include/krate/dsp/processors/note_processor.h` for `VelocityCurve` and `mapVelocity()`
- [X] T062 [US3] Update constructor to initialize `velocityCurve_ = VelocityCurve::Linear` in `dsp/include/krate/dsp/processors/note_processor.h`
- [X] T063 [US3] Implement `setVelocityCurve(VelocityCurve curve)` in `dsp/include/krate/dsp/processors/note_processor.h` storing `velocityCurve_ = curve`
- [X] T064 [US3] Implement `mapVelocity(int velocity)` member function in `dsp/include/krate/dsp/processors/note_processor.h`: delegate to Layer 0 `mapVelocity(velocity, velocityCurve_)`, return simple VelocityOutput with all destinations set to the curve output (depth scaling comes in US4)
- [X] T065 [US3] Define `VelocityOutput` struct in `dsp/include/krate/dsp/processors/note_processor.h` with members: `amplitude`, `filter`, `envelopeTime` (all float, default 0.0f)
- [X] T066 [US3] Verify all User Story 3 tests pass
- [X] T067 [US3] Fix any compiler warnings

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T068 [US3] Verify IEEE 754 compliance: Confirm `unit/processors/note_processor_test.cpp` is already in `-fno-fast-math` list

### 5.4 Commit (MANDATORY)

- [ ] T069 [US3] Commit completed User Story 3 work (SKIPPED per user instruction)

**Checkpoint**: All user stories (US1, US2, US3) should now be independently functional and committed. Velocity mapping works for all four curve types.

---

## Phase 6: User Story 4 - Multi-Destination Velocity Routing (Priority: P3)

**Goal**: Provide pre-computed velocity values for three destinations (amplitude, filter, envelope time) with independent depth scaling (0.0-1.0)

**Independent Test**: Can be tested by mapping a single velocity value and verifying each destination output is independently scaled according to its configured depth (FR-017, FR-018)

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T070 [US4] Write failing tests for `setAmplitudeVelocityDepth(float depth)` clamping to [0.0, 1.0] in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T071 [US4] Write failing tests for `setFilterVelocityDepth(float depth)` clamping to [0.0, 1.0] in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T072 [US4] Write failing tests for `setEnvelopeTimeVelocityDepth(float depth)` clamping to [0.0, 1.0] in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T073 [US4] Write failing tests for `mapVelocity(int velocity)` with amplitude depth at 100%, filter depth at 50%, envelope depth at 0%: verify each destination is scaled independently in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T074 [US4] Write failing tests for multi-destination velocity edge cases: depth 0.0 produces 0.0 output, depth 1.0 produces full curve output in `dsp/tests/unit/processors/note_processor_test.cpp`
- [X] T075 [US4] Verify all User Story 4 tests FAIL (no implementation yet)

### 6.2 Implementation for User Story 4

- [X] T076 [US4] Add member variables to `NoteProcessor` in `dsp/include/krate/dsp/processors/note_processor.h`: `ampVelocityDepth_` (float, default 1.0), `filterVelocityDepth_` (float, default 0.0), `envTimeVelocityDepth_` (float, default 0.0)
- [X] T077 [US4] Update constructor to initialize velocity depths in `dsp/include/krate/dsp/processors/note_processor.h`
- [X] T078 [US4] Implement `setAmplitudeVelocityDepth(float depth)` in `dsp/include/krate/dsp/processors/note_processor.h` clamping to [0.0, 1.0]
- [X] T079 [US4] Implement `setFilterVelocityDepth(float depth)` in `dsp/include/krate/dsp/processors/note_processor.h` clamping to [0.0, 1.0]
- [X] T080 [US4] Implement `setEnvelopeTimeVelocityDepth(float depth)` in `dsp/include/krate/dsp/processors/note_processor.h` clamping to [0.0, 1.0]
- [X] T081 [US4] Update `mapVelocity(int velocity)` in `dsp/include/krate/dsp/processors/note_processor.h` to apply depth scaling: `output.amplitude = curvedVel * ampVelocityDepth_`, `output.filter = curvedVel * filterVelocityDepth_`, `output.envelopeTime = curvedVel * envTimeVelocityDepth_`
- [X] T082 [US4] Verify all User Story 4 tests pass
- [X] T083 [US4] Fix any compiler warnings

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T084 [US4] Verify IEEE 754 compliance: Confirm `unit/processors/note_processor_test.cpp` is already in `-fno-fast-math` list

### 6.4 Commit (MANDATORY)

- [ ] T085 [US4] Commit completed User Story 4 work (SKIPPED per user instruction)

**Checkpoint**: All user stories (US1-US4) should now be independently functional and committed. NoteProcessor is feature-complete per spec.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [X] T086 [P] Add namespace documentation comment to `dsp/include/krate/dsp/processors/note_processor.h`
- [X] T087 [P] Add class-level documentation comment to `NoteProcessor` in `dsp/include/krate/dsp/processors/note_processor.h` with usage pattern and thread safety notes
- [X] T088 [P] Verify all public methods have docstring comments with @param, @return, @post annotations
- [X] T089 Run quickstart.md validation: execute all code examples from `specs/036-note-event-processor/quickstart.md` to ensure they compile and run correctly
- [ ] T090 Commit documentation updates (SKIPPED per user instruction)

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 8.1 Architecture Documentation Update

- [X] T091 Update `specs/_architecture_/layer-2-processors.md` with NoteProcessor entry including: purpose (MIDI note to frequency with pitch bend and velocity mapping), public API summary (prepare, getFrequency, processPitchBend, setPitchBend, setTuningReference, velocity methods), file location (`dsp/include/krate/dsp/processors/note_processor.h`), "when to use this" (polyphonic/monophonic synthesizers needing note processing), usage pattern (prepare once, processPitchBend once per block, getFrequency per voice)
- [X] T092 Update `specs/_architecture_/layer-0-core.md` with VelocityCurve enum and mapVelocity() function entries in the midi_utils.h section

### 8.2 Final Commit

- [ ] T093 Commit architecture documentation updates with message: "Update architecture docs for NoteProcessor (spec 036)" (SKIPPED per user instruction)
- [ ] T094 Verify all spec work is committed to feature branch (SKIPPED per user instruction)

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 9: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 9.1 Run Clang-Tidy Analysis

- [X] T095 Run clang-tidy on all modified/new source files: `./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja` (Windows) or `./tools/run-clang-tidy.sh --target all` (Linux/macOS)

### 9.2 Address Findings

- [X] T096 Fix all errors reported by clang-tidy (blocking issues) -- 0 errors found
- [X] T097 Review warnings and fix where appropriate (use judgment for DSP code) -- 0 warnings found
- [X] T098 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason) -- No suppressions needed
- [ ] T099 Commit clang-tidy fixes (SKIPPED per user instruction)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T100 Review ALL FR-xxx requirements (FR-001 through FR-020) from `specs/036-note-event-processor/spec.md` against implementation in `dsp/include/krate/dsp/processors/note_processor.h`. Use `specs/036-note-event-processor/checklists/requirements.md` as the verification guide
- [X] T101 Review ALL SC-xxx success criteria (SC-001 through SC-007) and verify measurable targets are achieved (run tests, measure performance)
- [X] T101b Measure `getFrequency()` CPU usage via benchmark: call `getFrequency()` in a tight loop (e.g., 1M iterations) at 44.1 kHz equivalent, verify <0.1% CPU per spec SC-006. This measures `getFrequency()` alone (not `processPitchBend()` which is O(1) per block). Result: 13.06 ns/call = 0.058% CPU at 44.1 kHz.
- [X] T102 Search for cheating patterns in implementation:
  - [X] No `// placeholder` or `// TODO` comments in `dsp/include/krate/dsp/processors/note_processor.h`
  - [X] No test thresholds relaxed from spec requirements in `dsp/tests/unit/processors/note_processor_test.cpp`
  - [X] No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [X] T103 Update `specs/036-note-event-processor/spec.md` "Implementation Verification" section with compliance status for each requirement (FR-001 through FR-020, SC-001 through SC-007)
- [X] T104 Mark overall status honestly: COMPLETE

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **NO**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **NO**
3. Did I remove ANY features from scope without telling the user? **NO**
4. Would the spec author consider this "done"? **YES**
5. If I were the user, would I feel cheated? **NO**

- [X] T105 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Commit

- [ ] T106 Commit all spec work to feature branch `036-note-event-processor` (SKIPPED per user instruction)
- [X] T107 Verify all tests pass: 37 test cases, 255 assertions, all pass. Full DSP suite: 5178 cases, 5177 passed (1 pre-existing failure in adsr_envelope_test.cpp, unrelated to NoteProcessor).

### 11.2 Completion Claim

- [X] T108 Claim completion: All 20 FR requirements MET, all 7 SC criteria MET. Spec 036-note-event-processor is COMPLETE.

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-6)**: All depend on Foundational phase completion
  - User Story 1 (Phase 3): Can start after Foundational - No dependencies on other stories
  - User Story 2 (Phase 4): Can start after Foundational - Depends on US1 (extends `getFrequency()`)
  - User Story 3 (Phase 5): Can start after Foundational - Independent of US1/US2
  - User Story 4 (Phase 6): Depends on US3 (extends `mapVelocity()`)
- **Polish (Phase 7)**: Depends on all desired user stories being complete
- **Documentation (Phase 8)**: Depends on all implementation complete
- **Static Analysis (Phase 9)**: Depends on all code written
- **Verification (Phase 10-11)**: Depends on all previous phases

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P1)**: Depends on User Story 1 (extends `getFrequency()` to apply pitch bend)
- **User Story 3 (P2)**: Can start after Foundational (Phase 2) - Independent of US1/US2
- **User Story 4 (P3)**: Depends on User Story 3 (extends `mapVelocity()` to apply depth scaling)

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Core class skeleton before methods
- Basic methods before advanced features
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- Phase 1 (Setup): All tasks can run in parallel
- Phase 2 (Foundational): T004 and T005 can run in parallel (both writing tests)
- User Story 1 Tests (3.1): T010-T015 can run in parallel (all writing to same test file but different test cases)
- User Story 2 Tests (4.1): T027-T036 can run in parallel
- User Story 3 Tests (5.1): T053-T058 can run in parallel
- User Story 4 Tests (6.1): T070-T074 can run in parallel
- Phase 7 (Polish): T086-T088 can run in parallel (documentation tasks)
- Phase 8 (Documentation): T091 and T092 can run in parallel (different architecture files)

Note: While US1 and US3 are independent and could be parallelized by different developers, US2 depends on US1 and US4 depends on US3.

---

## Parallel Example: User Story 1

```bash
# Launch all test writing tasks for User Story 1 together:
Task T010: "Write failing tests for constructor"
Task T011: "Write failing tests for prepare()"
Task T012: "Write failing tests for setTuningReference()"
Task T013: "Write failing tests for getFrequency() default tuning"
Task T014: "Write failing tests for getFrequency() various A4"
Task T015: "Write failing tests for tuning edge cases"

# Launch parallel implementation tasks after tests fail:
Task T017: "Create class skeleton"
Task T018: "Implement constructor"
(T019-T022 follow sequentially as they build on the skeleton)
```

---

## Implementation Strategy

### MVP First (User Story 1 + User Story 2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (frequency conversion)
4. Complete Phase 4: User Story 2 (pitch bend smoothing)
5. **STOP and VALIDATE**: Test US1+US2 independently
6. Deploy/demo if ready (frequency conversion + pitch bend is usable synth voice core)

### Incremental Delivery

1. Complete Setup + Foundational (Foundation ready)
2. Add User Story 1 + User Story 2 (Test independently - Deploy/Demo - MVP: playable note processing!)
3. Add User Story 3 (Test independently - Deploy/Demo - adds velocity expressiveness)
4. Add User Story 4 (Test independently - Deploy/Demo - full multi-destination routing)
5. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 + User Story 2 (pitch processing chain)
   - Developer B: User Story 3 + User Story 4 (velocity processing chain)
3. Stories integrate when both complete

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

---

## Summary

- **Total Tasks**: 111
- **User Story 1 (P1)**: 17 tasks (T010-T026) - Note-to-frequency conversion
- **User Story 2 (P1)**: 28 tasks (T027-T052 + T034b, T035b) - Pitch bend with smoothing
- **User Story 3 (P2)**: 17 tasks (T053-T069) - Velocity curve mapping
- **User Story 4 (P3)**: 16 tasks (T070-T085) - Multi-destination velocity routing
- **Parallel Opportunities**: 25 tasks marked [P] can run in parallel within their phase
- **Independent Test Criteria**: Each user story has clear acceptance tests and can be validated independently
- **Suggested MVP Scope**: User Story 1 + User Story 2 (frequency conversion + pitch bend = playable synth voice core)
