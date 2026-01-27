---
description: "Implementation task breakdown for TimeVaryingCombBank"
---

# Tasks: TimeVaryingCombBank

**Input**: Design documents from `F:\projects\iterum\specs\101-timevar-comb-bank\`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/timevar_comb_bank.h

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, follow this workflow:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Commit**: Commit the completed work

**Note**: Skills (testing-guide, vst-guide, dsp-architecture) auto-load when their file patterns are accessed. No manual skill invocation is required - this is informational only.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             systems/timevar_comb_bank_tests.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Verify project structure and dependencies are in place

- [ ] T001 Verify DSP library structure at `F:\projects\iterum\dsp\` exists with include/krate/dsp/systems/ directory
- [ ] T002 Verify dependency headers exist: FeedbackComb, LFO, OnePoleSmoother, Xorshift32, db_utils
- [ ] T003 Verify Catch2 test infrastructure in `F:\projects\iterum\dsp\tests\systems\` directory

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [ ] T004 Create empty header file at `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h` with namespace and class skeleton
- [ ] T005 Create empty test file at `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp` with Catch2 includes
- [ ] T006 Add timevar_comb_bank_tests.cpp to `F:\projects\iterum\dsp\tests\CMakeLists.txt` test sources
- [ ] T007 Verify clean build: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Create Evolving Metallic Textures (Priority: P1) 🎯 MVP

**Goal**: Implement core time-varying comb bank functionality with modulated delay times for evolving metallic textures

**Independent Test**: Process a test signal through a bank of 4 combs with sine wave modulation and verify the output contains time-varying spectral resonances at the expected fundamental frequencies

**Requirements**: FR-001, FR-002, FR-009, FR-013, FR-015, FR-016, FR-017, FR-018, FR-019, FR-020

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T008 [US1] Write failing test for prepare() and reset() lifecycle in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T009 [US1] Write failing test for mono process() with 4 combs at harmonic intervals in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T010 [US1] Write failing test for modulation at 1 Hz rate and 10% depth producing smooth delay variations in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T011 [US1] Write failing test for modulation depth at 0% producing static output (no time variation) in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T012 [US1] Write failing test for NaN/Inf handling per FR-020 (per-comb reset and substitute 0) in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`

### 3.2 Implementation for User Story 1

- [ ] T013 [US1] Implement CombChannel struct with FeedbackComb, LFO, Xorshift32, and 4 OnePoleSmoother members in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T014 [US1] Implement TimeVaryingCombBank class with kMaxCombs=8 array of CombChannel in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T015 [US1] Implement prepare(sampleRate, maxDelayMs) with buffer allocation and smoother configuration (20ms delay, 10ms feedback, 5ms gain) in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T016 [US1] Implement reset() to clear all delay lines, LFOs, and random generators in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T017 [US1] Implement setNumCombs(count) with range clamping [1, kMaxCombs] in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T018 [US1] Implement setCombDelay(index, ms) with range clamping and smoother update in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T019 [US1] Implement setModRate(hz) and setModDepth(percent) with range validation [0.01, 20] Hz and [0, 100]% in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T020 [US1] Implement mono process(input) with per-comb modulation calculation (LFO + smoother) and NaN/Inf checking in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T021 [US1] Verify all User Story 1 tests pass: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[timevar-comb-bank]"`

### 3.3 Cross-Platform Verification (MANDATORY)

- [ ] T022 [US1] **Verify IEEE 754 compliance**: Test file uses NaN/Inf detection from detail::isNaN/isInf → add `systems/timevar_comb_bank_tests.cpp` to `-fno-fast-math` list in `F:\projects\iterum\dsp\tests\CMakeLists.txt`

### 3.4 Commit (MANDATORY)

- [ ] T023 [US1] **Commit completed User Story 1 work**: `git add dsp/include/krate/dsp/systems/timevar_comb_bank.h dsp/tests/systems/timevar_comb_bank_tests.cpp dsp/tests/CMakeLists.txt && git commit -m "feat(dsp): add TimeVaryingCombBank core functionality - US1"`

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Harmonic Series Tuning (Priority: P1)

**Goal**: Implement automatic harmonic tuning mode to create harmonic overtones based on a fundamental frequency

**Independent Test**: Set a fundamental frequency and verify the delay times are automatically calculated to produce a harmonic series of resonances

**Requirements**: FR-006, FR-007

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T024 [US2] Write failing test for harmonic tuning at 100 Hz producing delays [10ms, 5ms, 3.33ms, 2.5ms] within 1 cent (SC-001) in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T025 [US2] Write failing test for fundamental change from 100 Hz to 200 Hz updating all comb delays proportionally without discontinuities in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T026 [US2] Write failing test for switching between Harmonic, Inharmonic, and Custom modes in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`

### 4.2 Implementation for User Story 2

- [ ] T027 [US2] Implement Tuning enum (Harmonic, Inharmonic, Custom) in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T028 [US2] Implement setTuningMode(mode) with recalculateTunedDelays() callback in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T029 [US2] Implement setFundamental(hz) with range clamping [20, 1000] Hz and recalculation in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T030 [US2] Implement computeHarmonicDelay(index) using f[n] = fundamental * (n+1) formula in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T031 [US2] Implement recalculateTunedDelays() to update all comb baseDelayMs based on tuning mode in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T032 [US2] Verify all User Story 2 tests pass: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[timevar-comb-bank]"`

### 4.3 Commit (MANDATORY)

- [ ] T033 [US2] **Commit completed User Story 2 work**: `git add dsp/include/krate/dsp/systems/timevar_comb_bank.h dsp/tests/systems/timevar_comb_bank_tests.cpp && git commit -m "feat(dsp): add harmonic tuning mode to TimeVaryingCombBank - US2"`

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Inharmonic Bell-Like Tones (Priority: P2)

**Goal**: Implement inharmonic tuning mode for bell-like, metallic sounds with non-integer overtone relationships

**Independent Test**: Set inharmonic tuning and verify the delay ratios follow a non-integer pattern (e.g., 1:1.4:1.9:2.3)

**Requirements**: FR-007, FR-008

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T034 [US3] Write failing test for inharmonic tuning at 100 Hz with spread=1.0 producing frequencies [100, 141, 173, 200 Hz] in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T035 [US3] Write failing test for spread parameter changing from 0.0 (harmonic) to 1.0 (inharmonic) in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`

### 5.2 Implementation for User Story 3

- [ ] T036 [US3] Implement setSpread(amount) with range clamping [0, 1] and recalculation in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T037 [US3] Implement computeInharmonicDelay(index) using f[n] = fundamental * sqrt(1 + n * spread) formula in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T038 [US3] Update recalculateTunedDelays() to handle Inharmonic mode with spread parameter in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T039 [US3] Verify all User Story 3 tests pass: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[timevar-comb-bank]"`

### 5.3 Commit (MANDATORY)

- [ ] T040 [US3] **Commit completed User Story 3 work**: `git add dsp/include/krate/dsp/systems/timevar_comb_bank.h dsp/tests/systems/timevar_comb_bank_tests.cpp && git commit -m "feat(dsp): add inharmonic tuning mode to TimeVaryingCombBank - US3"`

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed

---

## Phase 6: User Story 4 - Stereo Movement Effects (Priority: P2)

**Goal**: Implement stereo output distribution and modulation phase offsets for wide stereo effects

**Independent Test**: Process a mono signal with stereo spread enabled and verify the left and right outputs have different spectral content due to pan distribution

**Requirements**: FR-010, FR-012, FR-014

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T041 [US4] Write failing test for stereo spread at 1.0 distributing 4 combs across L-R field in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T042 [US4] Write failing test for modulation phase spread at 90 degrees creating quarter-cycle offsets between adjacent combs in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T043 [US4] Write failing test for stereo spread at 0.0 producing mono-compatible centered output in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T044 [US4] Write failing test for stereo decorrelation (SC-006: correlation coefficient < 0.7 with modulation) in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T044a [US4] Write failing test for phase spread + stereo spread interaction (verify effects are independent and compound correctly, not interfering) in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`

### 6.2 Implementation for User Story 4

- [ ] T045 [US4] Implement setStereoSpread(amount) with range clamping [0, 1] and recalculatePanPositions() callback in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T046 [US4] Implement setModPhaseSpread(degrees) with range wrapping [0, 360) and recalculateLfoPhases() callback in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T047 [US4] Implement recalculatePanPositions() using linear L-R distribution and equal-power pan law in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T048 [US4] Implement recalculateLfoPhases() to set per-comb LFO phase offsets in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T049 [US4] Implement processStereo(left, right) with pan distribution and equal-power mixing in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T050 [US4] Verify all User Story 4 tests pass: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[timevar-comb-bank]"`

### 6.3 Commit (MANDATORY)

- [ ] T051 [US4] **Commit completed User Story 4 work**: `git add dsp/include/krate/dsp/systems/timevar_comb_bank.h dsp/tests/systems/timevar_comb_bank_tests.cpp && git commit -m "feat(dsp): add stereo movement effects to TimeVaryingCombBank - US4"`

**Checkpoint**: User Stories 1-4 should all work independently and be committed

---

## Phase 7: User Story 5 - Random Drift Modulation (Priority: P3)

**Goal**: Implement random drift modulation for organic, unpredictable variations on top of LFO modulation

**Independent Test**: Enable random modulation, process identical input twice with the same seed, and verify outputs are identical (deterministic randomness)

**Requirements**: FR-011

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T052 [US5] Write failing test for random modulation amount at 0.5 adding slow drift to delay times in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T053 [US5] Write failing test for deterministic random sequence with fixed seed (SC-004) in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T054 [US5] Write failing test for random modulation at 0.0 producing only deterministic LFO modulation in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`

### 7.2 Implementation for User Story 5

- [ ] T055 [US5] Implement setRandomModulation(amount) with range clamping [0, 1] in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T056 [US5] Update process() and processStereo() to add random drift from Xorshift32 to modulated delay times in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T057 [US5] Update reset() to reseed all Xorshift32 instances with deterministic seeds (per-comb unique seeds) in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T058 [US5] Verify all User Story 5 tests pass: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[timevar-comb-bank]"`

### 7.3 Commit (MANDATORY)

- [ ] T059 [US5] **Commit completed User Story 5 work**: `git add dsp/include/krate/dsp/systems/timevar_comb_bank.h dsp/tests/systems/timevar_comb_bank_tests.cpp && git commit -m "feat(dsp): add random drift modulation to TimeVaryingCombBank - US5"`

**Checkpoint**: User Stories 1-5 should all work independently and be committed

---

## Phase 8: User Story 6 - Per-Comb Parameter Control (Priority: P3)

**Goal**: Implement fine control over individual comb filters for complex sound design

**Independent Test**: Set different feedback values for each comb and verify the decay rates differ accordingly

**Requirements**: FR-003, FR-004, FR-005

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T060 [US6] Write failing test for per-comb feedback (comb 0 at 0.9, comb 1 at 0.5) producing different decay rates in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T061 [US6] Write failing test for per-comb damping (comb 2 at 0.8 dark) producing more HF rolloff in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T062 [US6] Write failing test for per-comb gain (comb 3 at -6 dB) contributing half the level in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`

### 8.2 Implementation for User Story 6

- [ ] T063 [US6] Implement setCombFeedback(index, amount) with range clamping [-0.9999, 0.9999] and smoother update in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T064 [US6] Implement setCombDamping(index, amount) with range clamping [0, 1] and smoother update in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T065 [US6] Implement setCombGain(index, dB) with dbToGain() conversion and smoother update in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T066 [US6] Update process() and processStereo() to apply smoothed feedback, damping, and gain per comb in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T067 [US6] Verify all User Story 6 tests pass: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[timevar-comb-bank]"`

### 8.3 Commit (MANDATORY)

- [ ] T068 [US6] **Commit completed User Story 6 work**: `git add dsp/include/krate/dsp/systems/timevar_comb_bank.h dsp/tests/systems/timevar_comb_bank_tests.cpp && git commit -m "feat(dsp): add per-comb parameter control to TimeVaryingCombBank - US6"`

**Checkpoint**: All user stories should now be independently functional and committed

---

## Phase 9: Success Criteria Verification

**Purpose**: Verify all measurable success criteria (SC-001 through SC-006) are met

### 9.1 Success Criteria Tests (Write if Not Already Covered)

- [ ] T069 Write test for SC-001 (harmonic tuning within 1 cent of target frequencies) if not already covered by US2 tests in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T070 Write test for SC-002 (modulation produces +/-10% delay variation without clicks) if not already covered by US1 tests in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T071 Write benchmark test for SC-003 (1 second at 44.1kHz with 8 combs in <10ms, Release -O2, averaged over 10 runs) in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T072 Write test for SC-004 (deterministic random with seed) if not already covered by US5 tests in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T073 Write test for SC-005 (smooth parameter transitions without zipper noise) in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T074 Write test for SC-006 (stereo decorrelation coefficient < 0.7) if not already covered by US4 tests in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T074a Write test for FR-018 (verify FeedbackComb uses linear interpolation for delay modulation, not allpass) by checking that modulated delay changes don't introduce allpass-characteristic phase artifacts in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
- [ ] T075 Verify all success criteria tests pass: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[timevar-comb-bank]"`
- [ ] T076 **Commit success criteria verification**: `git add dsp/tests/systems/timevar_comb_bank_tests.cpp && git commit -m "test(dsp): add success criteria verification for TimeVaryingCombBank"`

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T077 Code review: Verify all noexcept annotations are present on process methods (FR-017)
- [ ] T078 Code review: Verify no allocations in process() or processStereo() (FR-017)
- [ ] T079 Code review: Verify all parameter ranges are validated and clamped
- [ ] T080 Code review: Verify all smoothers use correct time constants (20ms delay, 10ms feedback/damping, 5ms gain per FR-019)
- [ ] T081 Run full test suite with ASan to detect memory issues: `cmake -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON && cmake --build build-asan --config Debug --target dsp_tests && build-asan/dsp/tests/Debug/dsp_tests.exe`
- [ ] T082 Run quickstart.md validation: Copy examples from `F:\projects\iterum\specs\101-timevar-comb-bank\quickstart.md` and verify they compile
- [ ] T083 **Commit polish work**: `git add -A && git commit -m "refactor(dsp): polish TimeVaryingCombBank implementation"`

---

## Phase 11: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 11.1 Architecture Documentation Update

- [ ] T084 **Update `F:\projects\iterum\specs\_architecture_\layer-3-systems.md`** with new TimeVaryingCombBank component:
  - Add entry: TimeVaryingCombBank - Bank of comb filters with independently modulated delay times
  - Include: Purpose (evolving metallic textures), public API summary (prepare, reset, setNumCombs, setCombDelay, setTuningMode, setFundamental, setSpread, setModRate, setModDepth, setModPhaseSpread, setRandomModulation, setStereoSpread, process, processStereo)
  - Location: `dsp/include/krate/dsp/systems/timevar_comb_bank.h`
  - When to use: Creating time-varying resonances, metallic textures, bell-like tones, modulated delays
  - Usage example: Basic harmonic tuning with modulation
  - Dependencies: FeedbackComb (L1), LFO (L1), OnePoleSmoother (L1), Xorshift32 (L0)

### 11.2 Final Commit

- [ ] T085 **Commit architecture documentation updates**: `git add specs/_architecture_/layer-3-systems.md && git commit -m "docs: add TimeVaryingCombBank to architecture documentation"`
- [ ] T086 Verify all spec work is committed to feature branch: `git status` should show clean working tree

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 12.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T087 **Review ALL FR-xxx requirements** (FR-001 through FR-020) from `F:\projects\iterum\specs\101-timevar-comb-bank\spec.md` against implementation in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
- [ ] T088 **Review ALL SC-xxx success criteria** (SC-001 through SC-006) and verify measurable targets are achieved in test results
- [ ] T089 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in `F:\projects\iterum\dsp\include\krate\dsp\systems\timevar_comb_bank.h`
  - [ ] No test thresholds relaxed from spec requirements in `F:\projects\iterum\dsp\tests\systems\timevar_comb_bank_tests.cpp`
  - [ ] No features quietly removed from scope

### 12.2 Fill Compliance Table in spec.md

- [ ] T090 **Update `F:\projects\iterum\specs\101-timevar-comb-bank\spec.md` "Implementation Verification" section** with compliance status (MET/NOT MET/PARTIAL) and evidence for each FR-xxx and SC-xxx requirement
- [ ] T091 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL in spec.md

### 12.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T092 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 13: Final Completion

**Purpose**: Final commit and completion claim

### 13.1 Final Commit

- [ ] T093 **Commit all spec work** to feature branch: `git add specs/101-timevar-comb-bank/spec.md && git commit -m "docs: mark spec 101-timevar-comb-bank as complete"`
- [ ] T094 **Verify all tests pass**: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`

### 13.2 Completion Claim

- [ ] T095 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-8)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order (P1 → P2 → P3)
- **Success Criteria (Phase 9)**: Depends on all user stories being complete
- **Polish (Phase 10)**: Depends on all user stories being complete
- **Documentation (Phase 11)**: Depends on all implementation being complete
- **Verification (Phase 12)**: Depends on all previous phases
- **Completion (Phase 13)**: Depends on successful verification

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P1)**: Can start after Foundational (Phase 2) - Extends US1 tuning functionality
- **User Story 3 (P2)**: Can start after US2 - Extends tuning with inharmonic mode
- **User Story 4 (P2)**: Can start after US1 - Extends processing with stereo mode
- **User Story 5 (P3)**: Can start after US1 - Extends modulation with random drift
- **User Story 6 (P3)**: Can start after US1 - Extends per-comb parameter control

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Core implementation before integration
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in dsp/tests/CMakeLists.txt (done once for all tests in US1)
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- All Setup tasks (T001-T003) can run in parallel
- All Foundational tasks (T004-T007) can run in parallel within Phase 2
- Once Foundational phase completes:
  - US1, US4, US5, US6 can start in parallel (independent features)
  - US2 can start in parallel with others
  - US3 requires US2 to complete first
- All test-writing tasks within a user story can run in parallel (T008-T012 for US1, etc.)
- Success criteria tests (T069-T074) can run in parallel if not already covered

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
Task T008: "Write failing test for prepare() and reset() lifecycle"
Task T009: "Write failing test for mono process() with 4 combs"
Task T010: "Write failing test for modulation at 1 Hz rate and 10% depth"
Task T011: "Write failing test for modulation depth at 0%"
Task T012: "Write failing test for NaN/Inf handling"

# All can be written in parallel before any implementation begins
```

---

## Implementation Strategy

### MVP First (User Stories 1 + 2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (Core functionality)
4. Complete Phase 4: User Story 2 (Harmonic tuning)
5. **STOP and VALIDATE**: Test US1 + US2 independently
6. This gives a functional time-varying comb bank with harmonic tuning

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Core functionality MVP
3. Add User Story 2 → Test independently → Harmonic tuning added
4. Add User Story 3 → Test independently → Inharmonic tuning added
5. Add User Story 4 → Test independently → Stereo effects added
6. Add User Story 5 → Test independently → Random drift added
7. Add User Story 6 → Test independently → Per-comb control added
8. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (core)
   - Developer B: User Story 2 (harmonic tuning)
   - Developer C: User Story 4 (stereo)
3. After US1 completes:
   - Developer D: User Story 5 (random drift)
   - Developer E: User Story 6 (per-comb control)
4. After US2 completes:
   - Developer F: User Story 3 (inharmonic tuning)

---

## Total Task Count: 97 tasks

### Task Count per User Story

- Setup (Phase 1): 3 tasks
- Foundational (Phase 2): 4 tasks
- User Story 1 (Phase 3): 16 tasks
- User Story 2 (Phase 4): 10 tasks
- User Story 3 (Phase 5): 7 tasks
- User Story 4 (Phase 6): 12 tasks (+1: T044a phase+stereo interaction)
- User Story 5 (Phase 7): 8 tasks
- User Story 6 (Phase 8): 9 tasks
- Success Criteria (Phase 9): 9 tasks (+1: T074a FR-018 linear interpolation)
- Polish (Phase 10): 7 tasks
- Documentation (Phase 11): 3 tasks
- Verification (Phase 12): 6 tasks
- Completion (Phase 13): 3 tasks

### Parallel Opportunities Identified

- 3 tasks in Setup can run in parallel
- 4 tasks in Foundational can run in parallel
- 5 test-writing tasks in US1 can run in parallel (T008-T012)
- 3 test-writing tasks in US2 can run in parallel (T024-T026)
- 2 test-writing tasks in US3 can run in parallel (T034-T035)
- 5 test-writing tasks in US4 can run in parallel (T041-T044, T044a)
- 3 test-writing tasks in US5 can run in parallel (T052-T054)
- 3 test-writing tasks in US6 can run in parallel (T060-T062)
- 7 success criteria tests can run in parallel (T069-T074, T074a)
- After Foundational: US1, US2, US4, US5, US6 can run in parallel (US3 depends on US2)

### Independent Test Criteria per Story

- **US1**: Process signal through 4 combs with sine modulation → verify time-varying spectral resonances
- **US2**: Set fundamental frequency → verify harmonic series delay times (within 1 cent)
- **US3**: Set inharmonic tuning → verify non-integer frequency ratios
- **US4**: Enable stereo spread → verify L/R decorrelation from pan distribution
- **US5**: Enable random modulation with seed → verify deterministic output on reset
- **US6**: Set different feedback per comb → verify different decay rates

### Suggested MVP Scope

**Minimum viable product**: User Story 1 + User Story 2
- Core time-varying comb bank functionality (US1)
- Harmonic tuning from fundamental frequency (US2)
- This provides the essential value: evolving metallic textures with musical tuning

**Full featured product**: All user stories (US1-US6)
- Adds inharmonic tuning, stereo effects, random drift, and per-comb control

---

## Format Validation

All tasks follow the checklist format:
- **Checkbox**: All tasks start with `- [ ]`
- **Task ID**: Sequential T001-T095
- **[P] marker**: Applied to parallelizable tasks (different files, no dependencies)
- **[Story] label**: Applied to user story phase tasks (US1-US6)
- **Description**: Clear action with exact file path

---

## Notes

- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list) - done once in US1
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
