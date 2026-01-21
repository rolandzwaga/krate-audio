# Tasks: Sample Rate Converter

**Feature Branch**: `072-sample-rate-converter`
**Input**: Design documents from `/specs/072-sample-rate-converter/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/sample_rate_converter_api.h

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Summary

This spec implements a Layer 1 DSP primitive for variable-rate linear buffer playback with high-quality interpolation. The SampleRateConverter provides fractional position tracking, multiple interpolation modes (Linear, Cubic, Lagrange), and end-of-buffer detection for pitch-shifted playback of captured audio slices.

**Key Technical Decisions**:
- Reuse existing interpolation functions from `core/interpolation.h` (Layer 0)
- Edge reflection for 4-point interpolation at boundaries
- Rate clamping to [0.25, 4.0] range (±2 octaves)
- Block processing with constant rate per block
- Header-only implementation following Layer 1 primitive pattern

**Files**:
- Header: `dsp/include/krate/dsp/primitives/sample_rate_converter.h`
- Tests: `dsp/tests/unit/primitives/sample_rate_converter_test.cpp`
- CMake: `dsp/tests/CMakeLists.txt` (add test file)

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/primitives/sample_rate_converter_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

---

## Format: `- [X] [ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- All task descriptions include exact file paths

---

## Phase 0: Dependency Verification (Constitution Principle XIV)

**Purpose**: Verify all required Layer 0 dependencies exist before proceeding

- [X] T000 Verify interpolation functions exist in `dsp/include/krate/dsp/core/interpolation.h`: `linearInterpolate`, `cubicHermiteInterpolate`, `lagrangeInterpolate`. Run: `grep -E "linearInterpolate|cubicHermiteInterpolate|lagrangeInterpolate" dsp/include/krate/dsp/core/interpolation.h`

**Checkpoint**: All three interpolation functions confirmed to exist. If any are missing, STOP and implement in Layer 0 first.

---

## Phase 1: Setup (Project Structure)

**Purpose**: Create basic file structure for this Layer 1 primitive

- [X] T001 Create test file skeleton at `dsp/tests/unit/primitives/sample_rate_converter_test.cpp` with includes and namespace setup
- [X] T002 Add test file to `dsp/tests/CMakeLists.txt` in the unit test sources list
- [X] T003 Create header skeleton at `dsp/include/krate/dsp/primitives/sample_rate_converter.h` with namespace and class declaration

**Checkpoint**: Files exist, project compiles (empty test file passes)

---

## Phase 2: Foundational (Core Constants and Structure)

**Purpose**: Implement basic class structure and constants that ALL user stories depend on

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Tests for Foundation (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T004 [P] Write test for rate constants (FR-003, FR-004, FR-005) in `sample_rate_converter_test.cpp`
- [X] T005 [P] Write test for default construction (position = 0, isComplete = false) in `sample_rate_converter_test.cpp`
- [X] T006 [P] Write test for rate clamping (FR-008) in `sample_rate_converter_test.cpp`

### 2.2 Implementation for Foundation

- [X] T007 Implement `InterpolationType` enum (FR-001) in `sample_rate_converter.h`
- [X] T008 Implement class constants (kMinRate, kMaxRate, kDefaultRate) in `sample_rate_converter.h`
- [X] T009 Implement member variables (position_, rate_, interpolationType_, sampleRate_, isComplete_) in `sample_rate_converter.h`
- [X] T010 Implement `prepare(double sampleRate)` method (FR-006) in `sample_rate_converter.h`
- [X] T011 Implement `reset()` method (FR-007) in `sample_rate_converter.h`
- [X] T012 Implement `setRate(float rate)` with clamping (FR-008) in `sample_rate_converter.h`
- [X] T013 Implement `setInterpolation(InterpolationType type)` (FR-009) in `sample_rate_converter.h`
- [X] T014 Implement `setPosition(float samples)` (FR-010) in `sample_rate_converter.h`
- [X] T015 Implement `getPosition() const` (FR-011) in `sample_rate_converter.h`

### 2.3 Verify Foundation

- [X] T016 Build DSP tests: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T017 Run foundational tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[samplerate][foundational]"`

### 2.4 Commit Foundation

- [X] T018 **Commit completed Foundational phase work**

**Checkpoint**: Foundation ready - class structure exists, configuration methods work, tests pass

---

## Phase 3: User Story 1 - DSP Developer Plays Back Buffer at Variable Rate (Priority: P1) 🎯 MVP

**Goal**: Implement core variable-rate playback with linear interpolation at different rates (0.5x, 1.0x, 2.0x)

**Independent Test**: Configure converter with rate, process a buffer, verify output samples match expected interpolated values at fractional positions

**Why P1**: Variable-rate playback is the core functionality. Without it, there is no feature.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T019 [P] [US1] Write test for rate 1.0 passthrough (SC-001) - output matches input at integer positions in `sample_rate_converter_test.cpp`
- [X] T020 [P] [US1] Write test for linear interpolation at fractional positions (FR-015) in `sample_rate_converter_test.cpp`
- [X] T021 [P] [US1] Write test for position 1.5 producing exact midpoint (SC-004) in `sample_rate_converter_test.cpp`
- [X] T022 [P] [US1] Write test for rate 2.0 completing 100 samples in 50 calls (SC-002) in `sample_rate_converter_test.cpp`
- [X] T023 [P] [US1] Write test for rate 0.5 completing 100 samples in ~198 calls (SC-003) in `sample_rate_converter_test.cpp`

### 3.2 Implementation for User Story 1

- [X] T024 [US1] Implement `process(const float* buffer, size_t bufferSize)` with integer position reading for rate 1.0 (FR-012 partial) in `sample_rate_converter.h`
- [X] T025 [US1] Add fractional position handling and call `Interpolation::linearInterpolate()` for Linear mode (FR-015) in `sample_rate_converter.h`
- [X] T026 [US1] Implement position advancement by rate after each sample (FR-020) in `sample_rate_converter.h`
- [X] T027 [US1] Add `#include <krate/dsp/core/interpolation.h>` to header dependencies

### 3.3 Verify User Story 1

- [X] T028 [US1] Build DSP tests: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T029 [US1] Run US1 tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[samplerate][US1]"`
- [X] T030 [US1] Verify all SC-001, SC-002, SC-003, SC-004 success criteria pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T031 [US1] **Verify IEEE 754 compliance**: Check if test file uses `std::isfinite()` → already planned for SC-008, defer to Phase 6

### 3.5 Commit User Story 1

- [X] T032 [US1] **Commit completed User Story 1 work** with message: "feat(dsp): implement linear interpolation and variable-rate playback for SampleRateConverter (US1)"

**Checkpoint**: User Story 1 complete - rate 1.0 passthrough works, linear interpolation works, position tracking works

---

## Phase 4: User Story 2 - DSP Developer Selects Interpolation Quality (Priority: P1)

**Goal**: Add Cubic (Hermite) and Lagrange interpolation modes with edge reflection at buffer boundaries

**Independent Test**: Process known waveforms with each interpolation type and verify quality differences (cubic/Lagrange smoother than linear)

**Why P1**: Interpolation quality is essential for real-world audio use cases. MVP needs high-quality options.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T033 [P] [US2] Write test for cubic interpolation using `cubicHermiteInterpolate()` (FR-016) in `sample_rate_converter_test.cpp`
- [X] T034 [P] [US2] Write test for Lagrange interpolation using `lagrangeInterpolate()` (FR-017) in `sample_rate_converter_test.cpp`
- [X] T035 [P] [US2] Write test for edge reflection at position 0.5 (FR-018 left boundary) in `sample_rate_converter_test.cpp`
- [X] T036 [P] [US2] Write test for edge reflection at position N-1.5 (FR-018 right boundary) in `sample_rate_converter_test.cpp`
- [X] T037 [P] [US2] Write test for integer positions returning exact values (FR-019) for all interpolation types in `sample_rate_converter_test.cpp`
- [X] T038 [P] [US2] Write test for cubic vs linear quality comparison (SC-005 partial - smoother transitions) in `sample_rate_converter_test.cpp`
- [X] T039 [P] [US2] Write test for Lagrange passing through exact sample values (SC-006) in `sample_rate_converter_test.cpp`

### 4.2 Implementation for User Story 2

- [X] T040 [US2] Implement `getSampleReflected(const float* buffer, size_t bufferSize, int idx)` private helper for edge reflection (FR-018) in `sample_rate_converter.h`
- [X] T041 [US2] Add switch on `interpolationType_` in `process()` to dispatch to Linear/Cubic/Lagrange (FR-015, FR-016, FR-017) in `sample_rate_converter.h`
- [X] T042 [US2] Implement Cubic path calling `Interpolation::cubicHermiteInterpolate()` with 4 reflected samples (FR-016) in `sample_rate_converter.h`
- [X] T043 [US2] Implement Lagrange path calling `Interpolation::lagrangeInterpolate()` with 4 reflected samples (FR-017) in `sample_rate_converter.h`
- [X] T044 [US2] Add special case for integer positions (fractional part = 0) to return exact sample (FR-019) in `sample_rate_converter.h`

### 4.3 Verify User Story 2

- [X] T045 [US2] Build DSP tests: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T046 [US2] Run US2 tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[samplerate][US2]"`
- [X] T047 [US2] Verify FR-016, FR-017, FR-018, FR-019 pass and SC-005, SC-006 success criteria met

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T048 [US2] **Verify IEEE 754 compliance**: Check if test file uses `std::isfinite()` → already planned for SC-008, defer to Phase 6

### 4.5 Commit User Story 2

- [X] T049 [US2] **Commit completed User Story 2 work** with message: "feat(dsp): add cubic and Lagrange interpolation with edge reflection for SampleRateConverter (US2)"

**Checkpoint**: User Stories 1 AND 2 complete - all three interpolation modes work, edge reflection prevents crashes

---

## Phase 5: User Story 3 - DSP Developer Detects End of Buffer (Priority: P2)

**Goal**: Implement end-of-buffer detection for one-shot sample playback scenarios

**Independent Test**: Play through buffer, verify `isComplete()` transitions from false to true at buffer end, verify output is 0.0f after completion

**Why P2**: End detection is important for proper sample management but secondary to core playback functionality

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T050 [P] [US3] Write test for `isComplete()` returning false at start in `sample_rate_converter_test.cpp`
- [X] T051 [P] [US3] Write test for `isComplete()` returning true after position >= bufferSize - 1 (FR-021) in `sample_rate_converter_test.cpp`
- [X] T052 [P] [US3] Write test for `process()` returning 0.0f when complete (FR-021) in `sample_rate_converter_test.cpp`
- [X] T053 [P] [US3] Write test for `reset()` clearing complete flag (FR-022, SC-010) in `sample_rate_converter_test.cpp`
- [X] T054 [P] [US3] Write test for `setPosition(0)` restarting playback after completion (SC-009) in `sample_rate_converter_test.cpp`

### 5.2 Implementation for User Story 3

- [X] T055 [US3] Implement `isComplete()` method returning `isComplete_` flag (FR-014) in `sample_rate_converter.h`
- [X] T056 [US3] Add completion check at start of `process()`: if position >= bufferSize - 1, set `isComplete_ = true` and return 0.0f (FR-021) in `sample_rate_converter.h`
- [X] T057 [US3] Ensure `reset()` clears `isComplete_` flag and sets position to 0.0f (FR-022) in `sample_rate_converter.h`
- [X] T058 [US3] Ensure `setPosition()` clears `isComplete_` flag to allow restart (FR-010) in `sample_rate_converter.h`

### 5.3 Verify User Story 3

- [X] T059 [US3] Build DSP tests: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T060 [US3] Run US3 tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[samplerate][US3]"`
- [X] T061 [US3] Verify SC-009 and SC-010 success criteria pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T062 [US3] **Verify IEEE 754 compliance**: Check if test file uses `std::isfinite()` → already planned for SC-008, defer to Phase 6

### 5.5 Commit User Story 3

- [X] T063 [US3] **Commit completed User Story 3 work** with message: "feat(dsp): implement end-of-buffer detection for SampleRateConverter (US3)"

**Checkpoint**: User Stories 1, 2, AND 3 complete - end detection works, can restart playback

---

## Phase 6: User Story 4 - DSP Developer Uses Block Processing (Priority: P2)

**Goal**: Add efficient block processing for better cache performance when processing entire buffers at constant rate

**Independent Test**: Compare block output with equivalent sample-by-sample processing at constant rate - must be identical

**Why P2**: Block processing improves performance but is not essential for basic functionality

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T064 [P] [US4] Write test for `processBlock()` producing identical output to sequential `process()` calls (SC-007) in `sample_rate_converter_test.cpp`
- [X] T065 [P] [US4] Write test for `processBlock()` handling completion mid-block (outputs 0.0f after complete) in `sample_rate_converter_test.cpp`
- [X] T066 [P] [US4] Write test for rate captured at block start (FR-013 clarification) in `sample_rate_converter_test.cpp`

### 6.2 Implementation for User Story 4

- [X] T067 [US4] Implement `processBlock(const float* src, size_t srcSize, float* dst, size_t dstSize)` method (FR-013) in `sample_rate_converter.h`
- [X] T068 [US4] Capture rate at start of `processBlock()`, loop calling internal process logic for dstSize samples in `sample_rate_converter.h`
- [X] T069 [US4] Handle completion within block: fill remaining dst samples with 0.0f after `isComplete_` becomes true in `sample_rate_converter.h`

### 6.3 Verify User Story 4

- [X] T070 [US4] Build DSP tests: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T071 [US4] Run US4 tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[samplerate][US4]"`
- [X] T072 [US4] Verify SC-007 success criterion passes (block matches sequential)

### 6.4 Cross-Platform Verification (MANDATORY)

- [X] T073 [US4] **Verify IEEE 754 compliance**: Check if test file uses `std::isfinite()` → already planned for SC-008, defer to Phase 7

### 6.5 Commit User Story 4

- [X] T074 [US4] **Commit completed User Story 4 work** with message: "feat(dsp): implement block processing for SampleRateConverter (US4)"

**Checkpoint**: All user stories complete - block processing provides performance optimization

---

## Phase 7: Quality Verification & Edge Cases

**Purpose**: Verify THD+N quality improvement, real-time safety, and edge case handling

### 7.1 Tests for Quality & Safety (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T075 [P] Write THD+N test comparing cubic vs linear for sine wave at rate 0.75 (SC-005 - 20dB improvement) in `sample_rate_converter_test.cpp`
- [X] T076 [P] Write test for `process()` before `prepare()` returning 0.0f (FR-026) in `sample_rate_converter_test.cpp`
- [X] T077 [P] Write test for nullptr buffer returning 0.0f (FR-025) in `sample_rate_converter_test.cpp`
- [X] T078 [P] Write test for zero-size buffer returning 0.0f and setting isComplete (FR-025) in `sample_rate_converter_test.cpp`
- [X] T079 [P] Write test for rate clamping enforced during processing (SC-011) in `sample_rate_converter_test.cpp`
- [X] T080 [P] Write test for 1 million `process()` calls without NaN/Infinity (SC-008) in `sample_rate_converter_test.cpp`

### 7.2 Implementation for Quality & Safety

- [X] T081 Add nullptr/zero-size checks at start of `process()` returning 0.0f (FR-025, FR-026) in `sample_rate_converter.h`
- [X] T082 Add nullptr/zero-size checks at start of `processBlock()` (FR-025, FR-026) in `sample_rate_converter.h`
- [X] T083 Ensure all processing methods are marked `noexcept` (FR-023) in `sample_rate_converter.h`
- [X] T084 Verify no allocations in `process()` or `processBlock()` - code review (FR-024)

### 7.3 Verify Quality & Safety

- [X] T085 Build DSP tests: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T086 Run quality tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[samplerate][quality]"`
- [X] T087 Run edge case tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[samplerate][edge]"`
- [X] T088 Run stability tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[samplerate][stability]"`
- [X] T089 Verify SC-005 (THD+N 20dB improvement), SC-008 (no NaN), SC-011 (rate clamping) pass

### 7.4 Cross-Platform Verification (MANDATORY)

- [X] T090 **Verify IEEE 754 compliance**: `sample_rate_converter_test.cpp` uses `std::isfinite()` in SC-008 test → **ADD to `-fno-fast-math` list** in `dsp/tests/CMakeLists.txt`
- [X] T091 Build on macOS/Linux (if available) or verify CI passes after adding `-fno-fast-math` flag

### 7.5 Commit Quality & Safety

- [X] T092 **Commit completed Quality Verification phase work** with message: "test(dsp): add THD+N verification and edge case handling for SampleRateConverter"

**Checkpoint**: Quality verified, edge cases handled, IEEE 754 compliance ensured

---

## Phase 8: Documentation & Code Quality

**Purpose**: Add comprehensive Doxygen documentation and ensure code quality

### 8.1 Documentation Tasks

- [X] T093 [P] Add Doxygen class documentation to `SampleRateConverter` (FR-030) in `sample_rate_converter.h`
- [X] T094 [P] Add Doxygen documentation to all public methods (FR-030) in `sample_rate_converter.h`
- [X] T095 [P] Add Doxygen documentation to `InterpolationType` enum values (FR-030) in `sample_rate_converter.h`
- [X] T096 [P] Add usage examples in class documentation (FR-030) in `sample_rate_converter.h`

### 8.2 Code Quality Checks

- [X] T097 Verify all member variables follow trailing underscore convention (FR-031) in `sample_rate_converter.h`
- [X] T098 Verify class and enum names use PascalCase (FR-031) in `sample_rate_converter.h`
- [X] T099 Verify all components in `Krate::DSP` namespace (FR-029) in `sample_rate_converter.h`
- [X] T100 Verify header-only implementation (FR-028) - all methods inline or in header
- [X] T101 Verify only depends on Layer 0 (`interpolation.h`) and standard library (FR-027) - code review

### 8.3 Build and Run All Tests

- [X] T102 Clean build: `cmake --build build/windows-x64-release --config Release --target clean`
- [X] T103 Full rebuild: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T104 Run all SampleRateConverter tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[samplerate]"`
- [X] T105 Verify 100% test coverage of public methods (SC-012) - review test file

### 8.4 Commit Documentation

- [X] T106 **Commit documentation and code quality updates** with message: "docs(dsp): add comprehensive documentation for SampleRateConverter"

**Checkpoint**: Code quality verified, documentation complete

---

## Phase 9: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [X] T107 **Update `specs/_architecture_/layer-1-primitives.md`** with SampleRateConverter entry:
  - Purpose: Variable-rate linear buffer playback with high-quality interpolation
  - Location: `dsp/include/krate/dsp/primitives/sample_rate_converter.h`
  - Public API summary: setRate, setInterpolation, process, processBlock, isComplete
  - When to use: Pitch-shifted playback of captured buffers, freeze mode slice playback, granular grain playback
  - Usage example: Basic variable-rate playback with completion detection
  - Dependencies: Layer 0 `interpolation.h` only

### 9.2 Final Commit

- [X] T108 **Commit architecture documentation updates** with message: "docs(architecture): add SampleRateConverter to Layer 1 primitives"

**Checkpoint**: Architecture documentation reflects SampleRateConverter component

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T109 **Review ALL 31 FR-xxx requirements** from spec.md against implementation:
  - FR-001 through FR-005: Constants and types ✓
  - FR-006 through FR-011: Lifecycle and configuration ✓
  - FR-012 through FR-014: Processing methods ✓
  - FR-015 through FR-019: Interpolation implementation ✓
  - FR-020 through FR-022: Position management ✓
  - FR-023 through FR-026: Real-time safety ✓
  - FR-027 through FR-031: Dependencies and code quality ✓

- [X] T110 **Review ALL 12 SC-xxx success criteria** and verify measurable targets achieved:
  - SC-001: Rate 1.0 passthrough ✓
  - SC-002: Rate 2.0 completes in 50 calls ✓
  - SC-003: Rate 0.5 completes in ~198 calls ✓
  - SC-004: Linear interpolation at 1.5 produces midpoint ✓
  - SC-005: Cubic 20dB better THD+N than linear ✓
  - SC-006: Lagrange exact at integer positions ✓
  - SC-007: processBlock matches sequential ✓
  - SC-008: 1M calls without NaN ✓
  - SC-009: isComplete transitions correctly ✓
  - SC-010: reset() clears complete flag ✓
  - SC-011: Rate clamping enforced ✓
  - SC-012: 100% test coverage ✓

- [X] T111 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in `sample_rate_converter.h`
  - [X] No test thresholds relaxed in `sample_rate_converter_test.cpp` (SC-005 must be 20dB, not lowered)
  - [X] No features quietly removed from scope (all 3 interpolation modes present)

### 10.2 Fill Compliance Table in spec.md

- [X] T112 **Update spec.md "Implementation Verification" section** at line 299-343 with compliance status:
  - Set all FR-001 through FR-031 to "MET" with test evidence file path
  - Set all SC-001 through SC-012 to "MET" with test output confirmation
  - Fill "Evidence" column with test names and file locations

- [X] T113 **Mark overall status honestly** in spec.md at line 363:
  - Change `[COMPLETE / NOT COMPLETE / PARTIAL]` to `COMPLETE`

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? → NO (SC-005 is exactly 20dB)
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? → NO (all implementation complete)
3. Did I remove ANY features from scope without telling the user? → NO (all 3 interpolation modes implemented)
4. Would the spec author consider this "done"? → YES (all 31 FR + 12 SC met)
5. If I were the user, would I feel cheated? → NO (full feature set delivered)

- [X] T114 **All self-check questions answered "no"** (or gaps documented honestly)

### 10.4 Final Build Verification

- [X] T115 Clean and rebuild all tests: `cmake --build build/windows-x64-release --config Release --target clean && cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T116 Run full test suite: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [X] T117 Verify zero warnings in build output

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Commit

- [X] T118 **Commit all spec work** to feature branch `072-sample-rate-converter` with message: "feat(dsp): complete SampleRateConverter implementation (spec 072)"
- [X] T119 **Verify all tests pass**: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[samplerate]"`

### 11.2 Completion Claim

- [X] T120 **Claim completion ONLY if all 31 FR + 12 SC requirements are MET** with test evidence

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-6)**: All depend on Foundational phase completion
  - User Story 1 (P1): Can start after Foundational - MVP functionality
  - User Story 2 (P1): Depends on User Story 1 - extends interpolation modes
  - User Story 3 (P2): Can start after User Story 2 - adds completion detection
  - User Story 4 (P2): Can start after User Story 3 - adds block processing
- **Quality (Phase 7)**: Depends on all user stories being complete
- **Documentation (Phase 8)**: Can run in parallel with Phase 7
- **Architecture (Phase 9)**: Depends on implementation complete
- **Verification (Phase 10)**: Depends on all previous phases
- **Completion (Phase 11)**: Depends on honest verification passing

### User Story Dependencies

- **User Story 1 (P1)**: Depends on Foundational only - implements core linear interpolation and variable-rate playback
- **User Story 2 (P1)**: Depends on User Story 1 - extends with cubic/Lagrange interpolation (reuses process() infrastructure)
- **User Story 3 (P2)**: Depends on User Story 2 - adds completion detection to existing process()
- **User Story 4 (P2)**: Depends on User Story 3 - implements block processing using existing process() logic

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation after tests
- Verify tests pass after implementation
- Cross-platform check (IEEE 754 compliance)
- Commit LAST task - commit completed work

### Parallel Opportunities

- **Phase 1**: All 3 setup tasks can run in parallel (different files)
- **Phase 2.1**: All 3 foundation tests can run in parallel (T004, T005, T006)
- **Phase 3.1**: All 5 US1 tests can run in parallel (T019-T023)
- **Phase 4.1**: All 7 US2 tests can run in parallel (T033-T039)
- **Phase 5.1**: All 5 US3 tests can run in parallel (T050-T054)
- **Phase 6.1**: All 3 US4 tests can run in parallel (T064-T066)
- **Phase 7.1**: All 6 quality/safety tests can run in parallel (T075-T080)
- **Phase 8.1**: All 4 documentation tasks can run in parallel (T093-T096)
- **Phase 8.2**: All 5 code quality checks can run in parallel (T097-T101)

**User Stories CANNOT run in parallel** - each extends the previous functionality

---

## Parallel Example: User Story 1 Tests

```bash
# Launch all US1 tests together (they write to same file but test different aspects):
Task T019: "Write test for rate 1.0 passthrough (SC-001)"
Task T020: "Write test for linear interpolation at fractional positions (FR-015)"
Task T021: "Write test for position 1.5 producing exact midpoint (SC-004)"
Task T022: "Write test for rate 2.0 completing 100 samples in 50 calls (SC-002)"
Task T023: "Write test for rate 0.5 completing 100 samples in ~198 calls (SC-003)"

# All can be written in parallel sections of sample_rate_converter_test.cpp
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (Linear interpolation at variable rates)
4. **STOP and VALIDATE**: Test User Story 1 independently
5. You now have a working variable-rate playback primitive with linear interpolation

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Commit (MVP - linear interpolation works!)
3. Add User Story 2 → Test independently → Commit (High-quality interpolation added!)
4. Add User Story 3 → Test independently → Commit (End detection added!)
5. Add User Story 4 → Test independently → Commit (Block processing optimization added!)
6. Each story adds value without breaking previous stories

### Full Feature Strategy

1. Complete Phases 1-2 (Setup + Foundational)
2. Complete Phases 3-6 sequentially (User Stories 1-4)
3. Complete Phase 7 (Quality verification)
4. Complete Phase 8 (Documentation)
5. Complete Phases 9-11 (Architecture docs + Verification + Completion)

---

## Task Count Summary

- **Total Tasks**: 120
- **Phase 1 (Setup)**: 3 tasks
- **Phase 2 (Foundational)**: 15 tasks
- **Phase 3 (User Story 1 - P1)**: 14 tasks
- **Phase 4 (User Story 2 - P1)**: 17 tasks
- **Phase 5 (User Story 3 - P2)**: 14 tasks
- **Phase 6 (User Story 4 - P2)**: 11 tasks
- **Phase 7 (Quality)**: 18 tasks
- **Phase 8 (Documentation)**: 14 tasks
- **Phase 9 (Architecture)**: 2 tasks
- **Phase 10 (Verification)**: 9 tasks
- **Phase 11 (Completion)**: 3 tasks

**Parallel Opportunities**: 47 tasks marked [P] can run in parallel with other [P] tasks in their phase

**MVP Scope**: Phases 1-3 (32 tasks) deliver working linear interpolation at variable rates

**Independent Test Criteria**:
- **US1**: Rate 1.0 passthrough, rate 2.0/0.5 timing, linear interpolation accuracy
- **US2**: Cubic/Lagrange interpolation quality, edge reflection safety, integer position exactness
- **US3**: Completion detection, restart capability, zero output after end
- **US4**: Block processing equivalence to sequential, completion mid-block handling

---

## Notes

- All tasks include exact file paths for implementation and testing
- [P] tasks = different test sections or parallel implementation opportunities within phase
- [Story] label maps task to specific user story for traceability (US1, US2, US3, US4)
- Each user story builds on previous functionality (sequential dependencies)
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test file to `-fno-fast-math` list in Phase 7)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-1-primitives.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
