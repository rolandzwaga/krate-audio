# Tasks: ADSR Envelope Generator

**Feature**: 032-adsr-envelope-generator
**Input**: Design documents from `/specs/032-adsr-envelope-generator/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/adsr_envelope.h, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture) - no manual context verification required.

### Cross-Platform Compatibility Check (After Implementation)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: The ADSR envelope test file uses `std::isnan()` for NaN validation:
   - Add `dsp/tests/unit/primitives/adsr_envelope_test.cpp` to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/primitives/adsr_envelope_test.cpp  # NEW FILE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

This check prevents CI failures on macOS/Linux that pass locally on Windows.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure. All tasks here are setup for the Layer 1 primitive.

- [ ] T001 Add `adsr_envelope.h` to `KRATE_DSP_PRIMITIVES_HEADERS` in F:\projects\iterum\dsp\CMakeLists.txt
- [ ] T002 Add `#include <krate/dsp/primitives/adsr_envelope.h>` to F:\projects\iterum\dsp\lint_all_headers.cpp for compile-time verification

**Checkpoint**: CMake configuration ready to include the new header

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented.

**CRITICAL**: No user story work can begin until this phase is complete.

This is a Layer 1 primitive with no shared infrastructure dependencies beyond Layer 0. No foundational tasks required - proceed directly to User Story 1.

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic ADSR Envelope (Priority: P1) MVP

**Goal**: Implement fundamental gate-driven ADSR behavior with five states (Idle, Attack, Decay, Sustain, Release). The envelope traverses all stages correctly with sample-accurate timing. This is the absolute minimum viable product - no downstream synth component can function without this.

**Independent Test**: Can be fully tested by sending gate-on, processing samples through all four stages, sending gate-off, and verifying the envelope traverses Idle -> Attack -> Decay -> Sustain -> Release -> Idle with correct timing and output levels. Delivers a usable envelope for amplitude and filter modulation.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T003 [US1] Create test file F:\projects\iterum\dsp\tests\unit\primitives\adsr_envelope_test.cpp with failing tests for basic ADSR cycle (gate on/off, all five stages, stage transitions, timing within ±1 sample, output levels, isActive/isReleasing queries)
- [ ] T004 [US1] Add adsr_envelope_test.cpp to dsp_tests executable in F:\projects\iterum\dsp\tests\CMakeLists.txt
- [ ] T005 [US1] Verify tests FAIL (header not yet created)

### 3.2 Implementation for User Story 1

- [ ] T006 [US1] Create header F:\projects\iterum\dsp\include\krate\dsp\primitives\adsr_envelope.h with class skeleton (enums, constants, member fields from data-model.md)
- [ ] T007 [US1] Implement lifecycle methods (constructor, prepare, reset) in adsr_envelope.h
- [ ] T008 [US1] Implement coefficient calculation utility (calcCoefficients static method) using EarLevel Engineering one-pole formula in adsr_envelope.h
- [ ] T009 [US1] Implement gate control method (gate on/off, stage transitions for hard retrigger mode only) in adsr_envelope.h
- [ ] T010 [US1] Implement per-sample processing (process method with one-pole iteration, stage transition logic) in adsr_envelope.h
- [ ] T011 [US1] Implement block processing (processBlock method - calls process N times) in adsr_envelope.h
- [ ] T012 [US1] Implement parameter setters (setAttack, setDecay, setSustain, setRelease with clamping and coefficient recalculation) in adsr_envelope.h
- [ ] T013 [US1] Implement state query methods (getStage, isActive, isReleasing, getOutput) in adsr_envelope.h
- [ ] T014 [US1] Build DSP library using full CMake path: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target KrateDSP`
- [ ] T015 [US1] Build tests using full CMake path: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T016 [US1] Verify all User Story 1 tests pass (run dsp_tests with filter for ADSR tests)

### 3.3 Cross-Platform Verification (MANDATORY)

- [ ] T017 [US1] **Verify IEEE 754 compliance**: Add F:\projects\iterum\dsp\tests\unit\primitives\adsr_envelope_test.cpp to `-fno-fast-math` list in F:\projects\iterum\dsp\tests\CMakeLists.txt (uses std::isnan for NaN validation)

### 3.4 Commit (MANDATORY)

- [ ] T018 [US1] **Commit completed User Story 1 work** with message: "Implement basic ADSR envelope (US1)"

**Checkpoint**: User Story 1 should be fully functional, tested, and committed. Basic ADSR envelope works with gate control and correct stage transitions.

---

## Phase 4: User Story 2 - Curve Shape Control (Priority: P2)

**Goal**: Add exponential, linear, and logarithmic curve shapes for each time-based stage (Attack, Decay, Release). This transforms the envelope from a basic utility into an expressive tool. Each stage can independently use different curves.

**Independent Test**: Can be tested independently by configuring different curve types per stage and verifying the output shape matches the expected curve character. Linear produces equal increments, exponential produces fast initial change with slow approach, logarithmic produces slow initial change with fast finish.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T019 [US2] Add failing tests to F:\projects\iterum\dsp\tests\unit\primitives\adsr_envelope_test.cpp for curve shape control (exponential/linear/logarithmic for each stage, verify shape characteristics at midpoint, mixed curves across stages)
- [ ] T020 [US2] Verify new tests FAIL (curve setters not yet implemented)

### 4.2 Implementation for User Story 2

- [ ] T021 [US2] Implement curve shape setters (setAttackCurve, setDecayCurve, setReleaseCurve) in F:\projects\iterum\dsp\include\krate\dsp\primitives\adsr_envelope.h
- [ ] T022 [US2] Update calcCoefficients to accept targetRatio parameter and map EnvCurve enum to correct ratio values (0.3/0.0001/100.0 per research.md) in adsr_envelope.h
- [ ] T023 [US2] Update coefficient recalculation in prepare and parameter setters to use curve-specific target ratios in adsr_envelope.h
- [ ] T024 [US2] Build and run tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T025 [US2] Verify all User Story 2 tests pass

### 4.3 Cross-Platform Verification (MANDATORY)

- [ ] T026 [US2] **Verify IEEE 754 compliance**: Confirm adsr_envelope_test.cpp is already in `-fno-fast-math` list (added in T017)

### 4.4 Commit (MANDATORY)

- [ ] T027 [US2] **Commit completed User Story 2 work** with message: "Add curve shape control to ADSR envelope (US2)"

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed. Envelope now supports expressive curve shaping.

---

## Phase 5: User Story 3 - Retrigger Modes (Priority: P3)

**Goal**: Implement hard retrigger and legato modes for musical expression. Hard retrigger restarts attack from current level for punchy articulation. Legato mode continues from current stage/level for smooth connected phrases. Both must be click-free.

**Independent Test**: Can be tested by sending overlapping gate events and verifying that hard-retrigger mode restarts attack from current level (not snapping to zero), while legato mode continues from current stage without re-entering attack.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T028 [US3] Add failing tests to F:\projects\iterum\dsp\tests\unit\primitives\adsr_envelope_test.cpp for retrigger modes (hard retrigger from Sustain/Decay/Release, legato mode no-restart, legato return from Release to Sustain/Decay, click-free transitions)
- [ ] T029 [US3] Verify new tests FAIL (legato mode not yet implemented)

### 5.2 Implementation for User Story 3

- [ ] T030 [US3] Implement setRetriggerMode method in F:\projects\iterum\dsp\include\krate\dsp\primitives\adsr_envelope.h
- [ ] T031 [US3] Update gate method to handle legato mode (check current stage, return to Sustain/Decay from Release, no action during Attack/Decay/Sustain) in adsr_envelope.h
- [ ] T032 [US3] Verify hard retrigger starts attack from current output level (existing behavior from US1, ensure no regression) in adsr_envelope.h
- [ ] T033 [US3] Build and run tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T034 [US3] Verify all User Story 3 tests pass

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T035 [US3] **Verify IEEE 754 compliance**: Confirm adsr_envelope_test.cpp is already in `-fno-fast-math` list (added in T017)

### 5.4 Commit (MANDATORY)

- [ ] T036 [US3] **Commit completed User Story 3 work** with message: "Add retrigger modes to ADSR envelope (US3)"

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed. Envelope now supports expressive retrigger behavior.

---

## Phase 6: User Story 4 - Velocity Scaling (Priority: P4)

**Goal**: Add optional velocity scaling so peak level scales with note velocity. This enables dynamic, expressive playing without requiring external modulation routing for the most common use case.

**Independent Test**: Can be tested by setting different velocity values and verifying the envelope peak level scales accordingly, with velocity=1.0 producing full peak and velocity=0.0 producing zero output.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T037 [US4] Add failing tests to F:\projects\iterum\dsp\tests\unit\primitives\adsr_envelope_test.cpp for velocity scaling (enabled with various velocity values, disabled always produces peak=1.0, velocity=0.0 produces zero output)
- [ ] T038 [US4] Verify new tests FAIL (velocity scaling not yet implemented)

### 6.2 Implementation for User Story 4

- [ ] T039 [US4] Implement setVelocityScaling and setVelocity methods in F:\projects\iterum\dsp\include\krate\dsp\primitives\adsr_envelope.h
- [ ] T040 [US4] Update coefficient calculation to use peakLevel (velocity-scaled or 1.0) for attack target and sustain scaling in adsr_envelope.h
- [ ] T041 [US4] Update gate method to recalculate peakLevel based on velocity and scaling enabled state in adsr_envelope.h
- [ ] T042 [US4] Build and run tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T043 [US4] Verify all User Story 4 tests pass

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T044 [US4] **Verify IEEE 754 compliance**: Confirm adsr_envelope_test.cpp is already in `-fno-fast-math` list (added in T017)

### 6.4 Commit (MANDATORY)

- [ ] T045 [US4] **Commit completed User Story 4 work** with message: "Add velocity scaling to ADSR envelope (US4)"

**Checkpoint**: User Stories 1-4 should all work independently and be committed. Envelope now supports velocity-sensitive dynamics.

---

## Phase 7: User Story 5 - Real-Time Parameter Changes (Priority: P5)

**Goal**: Enable click-free parameter changes during live performance or automation. New values take effect on the next stage entry or smoothly apply to the current stage (5ms smoothing for sustain level changes during Sustain).

**Independent Test**: Can be tested by changing parameters mid-stage and verifying no discontinuities appear in the output and that new values affect subsequent behavior.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T046 [US5] Add failing tests to F:\projects\iterum\dsp\tests\unit\primitives\adsr_envelope_test.cpp for real-time parameter changes (change attack/decay/release mid-stage, change sustain during Sustain with 5ms smoothing, no discontinuities in output)
- [ ] T047 [US5] Verify new tests FAIL (sustain smoothing not yet implemented)

### 7.2 Implementation for User Story 5

- [ ] T048 [US5] Add sustainSmoothCoef member field and calculate in prepare method (5ms one-pole coefficient) in F:\projects\iterum\dsp\include\krate\dsp\primitives\adsr_envelope.h
- [ ] T049 [US5] Update setSustain to smoothly transition during Sustain stage (one-pole smoothing over 5ms, not instant jump) in adsr_envelope.h
- [ ] T050 [US5] Update process method to apply sustain smoothing during Sustain stage in adsr_envelope.h
- [ ] T051 [US5] Verify setAttack/setDecay/setRelease recalculate coefficients mid-stage (existing behavior from US1, ensure works correctly) in adsr_envelope.h
- [ ] T052 [US5] Build and run tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T053 [US5] Verify all User Story 5 tests pass

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T054 [US5] **Verify IEEE 754 compliance**: Confirm adsr_envelope_test.cpp is already in `-fno-fast-math` list (added in T017)

### 7.4 Commit (MANDATORY)

- [ ] T055 [US5] **Commit completed User Story 5 work** with message: "Add real-time parameter changes to ADSR envelope (US5)"

**Checkpoint**: All user stories (1-5) should now be independently functional and committed. Full ADSR envelope feature complete.

---

## Phase 8: Edge Cases & Robustness

**Purpose**: Handle edge cases and extreme parameter values to ensure robustness in all scenarios.

### 8.1 Tests for Edge Cases (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T056 Add failing tests to F:\projects\iterum\dsp\tests\unit\primitives\adsr_envelope_test.cpp for edge cases (minimum attack time 0.1ms, maximum attack time 10000ms, sustain=0.0, sustain=1.0 completes decay in 1 sample, gate-off during Attack, gate-off during Decay, gate-on followed immediately by gate-off in same block, reset during active envelope, all times at minimum, decay constant-rate behavior)
- [ ] T056b [FR-028] Add failing test to adsr_envelope_test.cpp verifying no denormalized values are produced during a full ADSR cycle (process complete gate-on through release-to-idle, check every output sample with `std::fpclassify` is FP_NORMAL or FP_ZERO)
- [ ] T056c [FR-010] Add failing test to adsr_envelope_test.cpp verifying `prepare()` with a different sample rate while the envelope is active: coefficients are recalculated, current output level is preserved (no jump), and subsequent timing matches the new sample rate
- [ ] T057 Verify edge case tests FAIL or pass as expected

### 8.2 Implementation for Edge Cases

- [ ] T058 Verify parameter clamping handles min/max edge cases (already in setters from US1, add explicit validation if needed) in F:\projects\iterum\dsp\include\krate\dsp\primitives\adsr_envelope.h
- [ ] T059 Verify coefficient calculation handles extreme values without NaN/Inf (add guards if needed) in adsr_envelope.h
- [ ] T060 Add NaN validation to parameter setters using detail::isNaN from db_utils.h (mark setters with ITERUM_NOINLINE if needed for -ffast-math safety) in adsr_envelope.h
- [ ] T061 Build and run tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T062 Verify all edge case tests pass

### 8.3 Cross-Platform Verification (MANDATORY)

- [ ] T063 **Verify IEEE 754 compliance**: Confirm adsr_envelope_test.cpp is already in `-fno-fast-math` list (added in T017)

### 8.4 Commit (MANDATORY)

- [ ] T064 **Commit edge case handling** with message: "Add edge case handling and robustness to ADSR envelope"

**Checkpoint**: Envelope handles all edge cases robustly

---

## Phase 9: Performance & Multi-Sample-Rate Verification

**Purpose**: Verify performance meets budget (SC-003: < 0.01% CPU at 44100Hz) and timing accuracy across all standard sample rates.

### 9.1 Performance Tests (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T065 Add performance benchmark test to F:\projects\iterum\dsp\tests\unit\primitives\adsr_envelope_test.cpp (measure CPU usage for single envelope at 44100Hz, verify < 0.01% target)
- [ ] T066 Add multi-sample-rate timing tests to adsr_envelope_test.cpp (verify timing accuracy within 1% at 44100, 48000, 88200, 96000, 176400, 192000 Hz)
- [ ] T067 Verify benchmark/timing tests pass (performance should already meet target with 1 mul + 1 add implementation)

### 9.2 Implementation for Performance

- [ ] T068 Review process method for optimization opportunities (early-out in Idle stage, minimize branches) in F:\projects\iterum\dsp\include\krate\dsp\primitives\adsr_envelope.h
- [ ] T069 Build and run tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T070 Verify performance benchmark passes (< 0.01% CPU at 44100Hz)
- [ ] T071 Verify multi-sample-rate tests pass (timing within 1% at all rates)

### 9.3 Cross-Platform Verification (MANDATORY)

- [ ] T072 **Verify IEEE 754 compliance**: Confirm adsr_envelope_test.cpp is already in `-fno-fast-math` list (added in T017)

### 9.4 Commit (MANDATORY)

- [ ] T073 **Commit performance verification** with message: "Verify ADSR envelope performance and multi-sample-rate accuracy"

**Checkpoint**: Performance and timing verified across all sample rates

---

## Phase 10: Polish & Documentation

**Purpose**: Final code quality improvements and documentation updates.

- [ ] T074 [P] Run quickstart.md validation (verify all code examples compile and produce expected output)
- [ ] T075 [P] Add code comments and documentation to F:\projects\iterum\dsp\include\krate\dsp\primitives\adsr_envelope.h (Doxygen-style API docs, implementation notes)
- [ ] T076 Code cleanup and formatting check in adsr_envelope.h
- [ ] T077 Final build verification: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release`
- [ ] T078 Final test run: verify all tests pass with no warnings
- [ ] T079 **Commit polish work** with message: "Polish and document ADSR envelope implementation"

**Checkpoint**: Code is clean, documented, and production-ready

---

## Phase 11: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 11.1 Architecture Documentation Update

- [ ] T080 **Update F:\projects\iterum\specs\_architecture_\layer-1-primitives.md** with ADSREnvelope section:
  - Add: Purpose (ADSR envelope generator for synth applications)
  - Add: Public API summary (lifecycle, gate control, parameter setters, curve shapes, retrigger modes, velocity scaling, processing, state queries)
  - Add: File location (dsp/include/krate/dsp/primitives/adsr_envelope.h)
  - Add: "When to use this" (per-voice amplitude/filter modulation, any time-varying envelope needed)
  - Add: Usage example (basic gate-driven envelope)
  - Verify no duplicate functionality introduced

### 11.2 Final Commit

- [ ] T081 **Commit architecture documentation updates** with message: "Update architecture docs for ADSR envelope primitive"
- [ ] T082 Verify all spec work is committed to feature branch 032-adsr-envelope-generator

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 12: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 12.1 Run Clang-Tidy Analysis

- [ ] T083 **Run clang-tidy** on DSP library:
  ```powershell
  ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
  ```
  (Note: Requires one-time setup per CLAUDE.md - LLVM, Ninja, and compile_commands.json generation)

### 12.2 Address Findings

- [ ] T084 **Fix all errors** reported by clang-tidy (blocking issues)
- [ ] T085 **Review warnings** and fix where appropriate (use judgment for DSP code - magic numbers acceptable)
- [ ] T086 **Document suppressions** if any warnings are intentionally ignored (add NOLINT comment with reason)
- [ ] T087 **Commit clang-tidy fixes** with message: "Address clang-tidy findings for ADSR envelope"

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 13.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T088 **Review ALL FR-xxx requirements** (FR-001 through FR-030) from F:\projects\iterum\specs\032-adsr-envelope-generator\spec.md against implementation in adsr_envelope.h
- [ ] T089 **Review ALL SC-xxx success criteria** (SC-001 through SC-008) and verify measurable targets are achieved (run tests, measure timing, verify CPU usage)
- [ ] T090 **Search for cheating patterns** in F:\projects\iterum\dsp\include\krate\dsp\primitives\adsr_envelope.h:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 13.2 Fill Compliance Table in spec.md

- [ ] T091 **Update F:\projects\iterum\specs\032-adsr-envelope-generator\spec.md "Implementation Verification" section** with compliance status for each requirement:
  - For each FR-xxx: Open adsr_envelope.h, find the code, cite file path and line number
  - For each SC-xxx: Run the specific test, copy actual output, compare to spec threshold
  - Fill table with concrete evidence (not generic "implemented" claims)
- [ ] T092 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 13.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T093 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 14: Final Completion

**Purpose**: Final commit and completion claim

### 14.1 Final Commit

- [ ] T094 **Commit all spec work** to feature branch 032-adsr-envelope-generator
- [ ] T095 **Verify all tests pass**: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`

### 14.2 Completion Claim

- [ ] T096 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **User Stories (Phase 3-7)**: All depend on Setup completion
  - User Story 1 (P1) is foundational - defines core ADSR behavior
  - User Story 2 (P2) extends US1 with curve shapes - can start after US1 tests pass
  - User Story 3 (P3) extends US1 with retrigger modes - can start after US1 tests pass
  - User Story 4 (P4) extends US1 with velocity scaling - can start after US1 tests pass
  - User Story 5 (P5) extends all stories with real-time parameter changes - can start after US1 tests pass
- **Edge Cases (Phase 8)**: Depends on all user stories (1-5) being complete
- **Performance (Phase 9)**: Depends on all user stories (1-5) being complete
- **Polish (Phase 10)**: Depends on all user stories and edge cases being complete
- **Architecture Docs (Phase 11)**: Depends on all implementation being complete
- **Static Analysis (Phase 12)**: Depends on all code being written
- **Completion Verification (Phase 13)**: Depends on all previous phases
- **Final Completion (Phase 14)**: Depends on honest completion verification

### User Story Dependencies

- **User Story 1 (P1)**: Setup complete - FOUNDATIONAL for all other stories
- **User Story 2 (P2)**: User Story 1 complete - extends with curve shapes
- **User Story 3 (P3)**: User Story 1 complete - extends with retrigger modes
- **User Story 4 (P4)**: User Story 1 complete - extends with velocity scaling
- **User Story 5 (P5)**: User Story 1 complete - adds real-time parameter safety

**NOTE**: US2-US5 could theoretically be implemented in parallel after US1 completes, BUT they all modify the same header file (adsr_envelope.h), so sequential implementation is safer to avoid merge conflicts.

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation to make tests pass
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- Setup tasks T001-T002 can run in parallel (different files)
- Within each user story: Tests (if multiple test files) could run in parallel, BUT we have a single test file, so no parallelism within test phase
- US2-US5 could be parallelized if using feature branches, but all modify adsr_envelope.h so sequential is safer
- Polish tasks T074-T076 can run in parallel (different concerns)

---

## Parallel Example: User Story 1

```bash
# User Story 1 is sequential because all tasks affect the same header file:
T003: Write failing tests in adsr_envelope_test.cpp
T004: Add test to CMakeLists.txt
T005: Verify tests fail
T006-T013: Implement in adsr_envelope.h (sequential - same file)
T014-T016: Build and verify
T017: Add to -fno-fast-math list
T018: Commit

# No parallel opportunities within US1 due to single header file implementation
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (T001-T002)
2. Complete Phase 3: User Story 1 (T003-T018)
3. **STOP and VALIDATE**: Test User Story 1 independently
4. Basic ADSR envelope is now usable for downstream synth components

### Incremental Delivery

1. Complete Setup (Phase 1) → CMake ready
2. Add User Story 1 (Phase 3) → Test independently → **MVP ready - basic ADSR works**
3. Add User Story 2 (Phase 4) → Test independently → Curve shaping available
4. Add User Story 3 (Phase 5) → Test independently → Retrigger modes available
5. Add User Story 4 (Phase 6) → Test independently → Velocity scaling available
6. Add User Story 5 (Phase 7) → Test independently → Real-time parameter changes safe
7. Add Edge Cases (Phase 8) → Robustness verified
8. Add Performance (Phase 9) → CPU budget verified
9. Polish (Phase 10) → Production-ready
10. Architecture Docs (Phase 11) → Documentation updated
11. Static Analysis (Phase 12) → Quality verified
12. Completion Verification (Phase 13-14) → Honest completion

Each story adds value without breaking previous stories.

### Sequential Strategy (Recommended)

With single developer and single header file:

1. Complete Setup (Phase 1)
2. Complete User Story 1 (Phase 3) → Commit
3. Complete User Story 2 (Phase 4) → Commit
4. Complete User Story 3 (Phase 5) → Commit
5. Complete User Story 4 (Phase 6) → Commit
6. Complete User Story 5 (Phase 7) → Commit
7. Complete Edge Cases (Phase 8) → Commit
8. Complete Performance (Phase 9) → Commit
9. Complete Polish (Phase 10) → Commit
10. Complete Architecture Docs (Phase 11) → Commit
11. Complete Static Analysis (Phase 12) → Commit
12. Complete Verification (Phase 13-14) → Done

---

## Notes

- All implementation is in a single header file: F:\projects\iterum\dsp\include\krate\dsp\primitives\adsr_envelope.h
- All tests are in a single test file: F:\projects\iterum\dsp\tests\unit\primitives\adsr_envelope_test.cpp
- Layer 1 primitive with no plugin code changes needed
- Dependencies: Layer 0 only (db_utils.h for isNaN/flushDenormal) + stdlib (cmath, algorithm)
- Real-time safety: no allocations, no locks, all methods noexcept
- Performance target: < 0.01% CPU at 44100Hz (easily met with 1 mul + 1 add per sample)
- CMake must use full path on Windows: `"C:/Program Files/CMake/bin/cmake.exe"`
- Test file uses std::isnan - MUST be in -fno-fast-math list (T017)
- Skills auto-load when needed (testing-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update specs/_architecture_/ before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

---

## Task Count Summary

- **Total Tasks**: 98
- **Phase 1 (Setup)**: 2 tasks
- **Phase 3 (User Story 1 - Basic ADSR)**: 16 tasks - MVP
- **Phase 4 (User Story 2 - Curve Shapes)**: 9 tasks
- **Phase 5 (User Story 3 - Retrigger Modes)**: 9 tasks
- **Phase 6 (User Story 4 - Velocity Scaling)**: 9 tasks
- **Phase 7 (User Story 5 - Real-Time Parameter Changes)**: 10 tasks
- **Phase 8 (Edge Cases)**: 11 tasks
- **Phase 9 (Performance)**: 9 tasks
- **Phase 10 (Polish)**: 6 tasks
- **Phase 11 (Architecture Docs)**: 3 tasks
- **Phase 12 (Static Analysis)**: 5 tasks
- **Phase 13 (Completion Verification)**: 6 tasks
- **Phase 14 (Final Completion)**: 3 tasks

**Parallel Opportunities**: Setup tasks (2), polish tasks (3) - limited due to single header file implementation

**Independent Test Criteria**:
- US1: Gate-driven ADSR cycle with correct timing and stage transitions
- US2: Curve shapes produce measurably different trajectories
- US3: Retrigger modes behave correctly with no clicks
- US4: Peak level scales with velocity
- US5: Parameter changes are click-free

**Suggested MVP Scope**: User Story 1 only (Phase 1 + Phase 3) = 18 tasks for basic working ADSR envelope
