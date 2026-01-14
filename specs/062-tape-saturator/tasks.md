# Tasks: TapeSaturator Processor

**Input**: Design documents from `/specs/062-tape-saturator/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

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
4. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and test file structure

- [X] T001 Create test file skeleton at `dsp/tests/unit/processors/tape_saturator_test.cpp` with Catch2 includes and test tags `[tape_saturator]`
- [X] T002 Create header file skeleton at `dsp/include/krate/dsp/processors/tape_saturator.h` with namespace `Krate::DSP`, includes, and empty class declaration
- [X] T003 Verify build system picks up new files: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Enumerations and Constants (FR-001, FR-002)

- [X] T004 [P] Write failing tests for `TapeModel` enum (Simple=0, Hysteresis=1) in `dsp/tests/unit/processors/tape_saturator_test.cpp`
- [X] T005 [P] Write failing tests for `HysteresisSolver` enum (RK2=0, RK4=1, NR4=2, NR8=3) in `dsp/tests/unit/processors/tape_saturator_test.cpp`
- [X] T006 Implement `TapeModel` and `HysteresisSolver` enums in `dsp/include/krate/dsp/processors/tape_saturator.h`
- [X] T007 Verify enum tests pass

### 2.2 Default Constructor and Getters (FR-006, FR-013 to FR-018)

- [X] T008 Write failing tests for default constructor: model=Simple, solver=RK4, drive=0dB, saturation=0.5, bias=0.0, mix=1.0
- [X] T009 Write failing tests for getters: `getModel()`, `getSolver()`, `getDrive()`, `getSaturation()`, `getBias()`, `getMix()`
- [X] T010 Implement TapeSaturator class with default constructor and getter methods in `dsp/include/krate/dsp/processors/tape_saturator.h`
- [X] T011 Verify constructor and getter tests pass

### 2.3 Parameter Setters with Clamping (FR-007 to FR-012)

- [X] T012 Write failing tests for `setModel(TapeModel)` and `setSolver(HysteresisSolver)`
- [X] T013 Write failing tests for `setDrive(float dB)` with clamping to [-24, +24]
- [X] T014 Write failing tests for `setSaturation(float)` with clamping to [0, 1]
- [X] T015 Write failing tests for `setBias(float)` with clamping to [-1, +1]
- [X] T016 Write failing tests for `setMix(float)` with clamping to [0, 1]
- [X] T017 Implement all parameter setters with clamping in `dsp/include/krate/dsp/processors/tape_saturator.h`
- [X] T018 Verify setter tests pass

### 2.4 Lifecycle Methods (FR-003, FR-004, FR-005)

- [X] T019 Write failing tests for `prepare(double sampleRate, size_t maxBlockSize)`
- [X] T020 Write failing tests for `reset()` - clears filter state, snaps smoothers
- [X] T021 Write failing tests for process() before prepare() returns input unchanged (FR-005)
- [X] T022 Implement `prepare()` and `reset()` in `dsp/include/krate/dsp/processors/tape_saturator.h`
- [X] T023 Verify lifecycle tests pass

### 2.5 Foundational Commit

- [X] T024 Verify all foundational tests pass
- [ ] T025 **Commit completed Foundational phase work**

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Simple Tape Saturation (Priority: P1)

**Goal**: DSP developer applies simple tape saturation with tanh + pre/de-emphasis filtering

**Independent Test**: Process audio through Simple model and verify frequency-dependent saturation (HF saturates earlier due to pre-emphasis)

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T026 [P] [US1] Write failing tests for pre-emphasis filter configuration (HighShelf +9dB @ 3kHz) in `dsp/tests/unit/processors/tape_saturator_test.cpp`
- [X] T027 [P] [US1] Write failing tests for de-emphasis filter configuration (HighShelf -9dB @ 3kHz) in `dsp/tests/unit/processors/tape_saturator_test.cpp`
- [X] T028 [P] [US1] Write failing tests for Simple model signal flow: input -> drive -> pre-emphasis -> tanh -> de-emphasis -> DC block -> mix
- [X] T029 [P] [US1] Write failing tests for saturation parameter blend (saturation=0 is linear, saturation=1 is full tanh) per FR-022
- [X] T030 [P] [US1] Write failing tests for DC blocker at 10Hz (FR-035, FR-036, FR-037)

### 3.2 Implementation for User Story 1

- [X] T031 [US1] Implement pre-emphasis and de-emphasis Biquad filters in `dsp/include/krate/dsp/processors/tape_saturator.h`
- [X] T032 [US1] Implement Simple model saturation using `Sigmoid::tanh()` with saturation blend in `dsp/include/krate/dsp/processors/tape_saturator.h`
- [X] T033 [US1] Implement DC blocker using `DCBlocker` from Layer 1 in `dsp/include/krate/dsp/processors/tape_saturator.h`
- [X] T034 [US1] Implement `process(float*, size_t)` for Simple model with drive gain, filters, saturation, and mix blend
- [X] T035 [US1] Verify all Simple model tests pass

### 3.3 Integration Tests for User Story 1

- [X] T036 [US1] Write integration test: HF content saturates more than LF at equal drive (SC-002)
- [X] T037 [US1] Write integration test: mix=0.0 produces output identical to input (SC-009)
- [X] T038 [US1] Write integration test: n=0 is handled gracefully (FR-033)
- [X] T039 [US1] Verify integration tests pass
- [X] T039a [US1] Write test verifying pre/de-emphasis inverse relationship: deEmphasis.gain == -preEmphasis.gain (FR-019/FR-021)

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T040 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 3.5 Commit (MANDATORY)

- [ ] T041 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Hysteresis Model (Priority: P1)

**Goal**: DSP developer uses Jiles-Atherton magnetic hysteresis model for authentic tape saturation with memory effects

**Independent Test**: Process audio through Hysteresis mode and verify output differs from Simple mode (hysteresis loop characteristics)

**FR-to-Task Traceability (FR-023 to FR-030d):**
| Requirement | Task(s) | Description |
|-------------|---------|-------------|
| FR-023 (J-A model) | T047, T048, T049, T050 | Langevin, dM/dH, RK4 solver, process path |
| FR-024 (M state) | T043 | Magnetization persistence test |
| FR-025 (RK2) | T058, T062 | Phase 5 - solver implementation |
| FR-026 (RK4) | T049 | Default solver implementation |
| FR-027 (NR4) | T059, T063 | Phase 5 - solver implementation |
| FR-028 (NR8) | T060, T063 | Phase 5 - solver implementation |
| FR-029 (saturation→Ms) | T075 | Phase 6 - parameter effect |
| FR-030 (bias→DC) | T046 | Bias DC offset test |
| FR-030a (J-A defaults) | T042 | Default parameter values |
| FR-030b (setJAParams) | T109, T113 | Phase 10 - expert mode |
| FR-030c (J-A getters) | T110, T114 | Phase 10 - expert mode |
| FR-030d (T-scaling) | T118-T124 | Phase 11 - sample rate independence |

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T042 [P] [US2] Write failing tests for J-A default parameters: a=22, alpha=1.6e-11, c=1.7, k=27, Ms=350000 (FR-030a)
- [X] T043 [P] [US2] Write failing tests for magnetization state (M) persistence between samples (FR-024)
- [X] T044 [P] [US2] Write failing tests for Langevin function implementation (coth approximation, Taylor series for small x)
- [X] T045 [P] [US2] Write failing tests for hysteresis loop characteristics: output differs on rising vs falling edges (SC-003)
- [X] T046 [P] [US2] Write failing tests for bias parameter introducing DC offset before hysteresis (FR-030)

### 4.2 Implementation for User Story 2

- [X] T047 [US2] Implement Langevin function as private method in `dsp/include/krate/dsp/processors/tape_saturator.h`
- [X] T048 [US2] Implement J-A dM/dH differential equation in `dsp/include/krate/dsp/processors/tape_saturator.h`
- [X] T049 [US2] Implement RK4 solver (default) for Hysteresis model (FR-026) in `dsp/include/krate/dsp/processors/tape_saturator.h`
- [X] T050 [US2] Implement `process()` path for Hysteresis model with bias, J-A computation, DC blocking
- [X] T051 [US2] Verify all Hysteresis model tests pass

### 4.3 Integration Tests for User Story 2

- [X] T052 [US2] Write integration test: Simple and Hysteresis produce measurably different outputs (SC-001)
- [X] T053 [US2] Write integration test: triangle wave shows asymmetric saturation from memory effects
- [X] T054 [US2] Write integration test: DC offset after processing is below -50dBFS with non-zero bias (SC-007)
- [X] T055 [US2] Verify integration tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T056 [US2] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 4.5 Commit (MANDATORY)

- [ ] T057 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Numerical Solver Selection (Priority: P2)

**Goal**: DSP developer selects from RK2, RK4, NR4, NR8 solvers to balance CPU vs accuracy

**Independent Test**: Measure CPU time and output differences between solver types

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T058 [P] [US3] Write failing tests for RK2 solver implementation (FR-025) - 2 function evaluations per sample
- [X] T059 [P] [US3] Write failing tests for NR4 solver implementation (FR-027) - 4 Newton-Raphson iterations
- [X] T060 [P] [US3] Write failing tests for NR8 solver implementation (FR-028) - 8 Newton-Raphson iterations
- [X] T061 [P] [US3] Write failing tests for solver switch during processing - no clicks (FR-043)

### 5.2 Implementation for User Story 3

- [X] T062 [US3] Implement RK2 solver (Heun's method) in `dsp/include/krate/dsp/processors/tape_saturator.h`
- [X] T063 [US3] Implement Newton-Raphson solver with configurable iterations (NR4, NR8) in `dsp/include/krate/dsp/processors/tape_saturator.h`
- [X] T064 [US3] Implement solver dispatch in `process()` based on `solver_` setting
- [X] T065 [US3] Verify all solver tests pass

### 5.3 Integration Tests for User Story 3

- [X] T066 [US3] Write integration test: all solvers produce output within 10% RMS of each other (SC-010)
- [X] T067 [US3] Write integration test: solver change during processing is immediate without clicks
- [X] T068 [US3] Verify integration tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T069 [US3] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 5.5 Commit (MANDATORY)

- [ ] T070 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Solver selection fully functional

---

## Phase 6: User Story 4 - Saturation Parameter Control (Priority: P2)

**Goal**: DSP developer controls saturation intensity through drive, saturation, and bias controls

**Independent Test**: Sweep each parameter and measure effect on output waveform and harmonic content

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T071 [P] [US4] Write failing tests for drive increase from 0dB to +12dB increases saturation intensity
- [ ] T072 [P] [US4] Write failing tests for saturation=0.0 produces linear operation (EQ-only mode)
- [ ] T073 [P] [US4] Write failing tests for saturation=1.0 produces maximum nonlinear distortion
- [ ] T074 [P] [US4] Write failing tests for bias=0.5 introduces even harmonics (asymmetric saturation)

### 6.2 Implementation for User Story 4

- [ ] T075 [US4] Implement saturation parameter effect on Ms (saturation magnetization) in Hysteresis model
- [ ] T076 [US4] Verify parameter control affects output as specified
- [ ] T077 [US4] Verify all parameter control tests pass

### 6.3 Commit (MANDATORY)

- [ ] T078 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Parameter controls fully functional

---

## Phase 7: User Story 5 - Dry/Wet Mix (Priority: P3)

**Goal**: Mix engineer blends clean signal with saturated signal for parallel processing

**Independent Test**: Verify mix=0.0 bypasses, mix=1.0 is fully wet, mix=0.5 is 50/50 blend

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T079 [P] [US5] Write failing tests for mix=0.0 skips all processing (FR-034) - full bypass
- [ ] T080 [P] [US5] Write failing tests for mix=1.0 produces 100% saturated signal
- [ ] T081 [P] [US5] Write failing tests for mix=0.5 produces 50/50 blend of dry and wet

### 7.2 Implementation for User Story 5

- [ ] T082 [US5] Implement mix bypass optimization: when mix=0.0, skip all processing entirely
- [ ] T083 [US5] Implement linear mix blend between dry input and wet output
- [ ] T084 [US5] Verify mix tests pass

### 7.3 Commit (MANDATORY)

- [ ] T085 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Mix control fully functional

---

## Phase 8: User Story 6 - Parameter Smoothing (Priority: P3)

**Goal**: DSP developer automating parameters gets smooth transitions without clicks

**Independent Test**: Rapidly change parameters and verify no discontinuities in output

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T086 [P] [US6] Write failing tests for drive smoothing (5ms target, <10ms completion) per FR-038
- [ ] T087 [P] [US6] Write failing tests for saturation smoothing per FR-039
- [ ] T088 [P] [US6] Write failing tests for bias smoothing per FR-040
- [ ] T089 [P] [US6] Write failing tests for mix smoothing per FR-041
- [ ] T090 [P] [US6] Write failing tests for reset() snaps smoothers to current values (FR-044)

### 8.2 Implementation for User Story 6

- [ ] T091 [US6] Add OnePoleSmoother instances for drive, saturation, bias, mix in `dsp/include/krate/dsp/processors/tape_saturator.h`
- [ ] T092 [US6] Configure smoothers in `prepare()` with 5ms smoothing time
- [ ] T093 [US6] Implement per-sample smoothing in `process()` loop
- [ ] T094 [US6] Implement smoother snap in `reset()`
- [ ] T095 [US6] Verify all smoothing tests pass

### 8.3 Integration Tests for User Story 6

- [ ] T096 [US6] Write integration test: rapid drive change (0dB to +24dB) produces no clicks (SC-004)
- [ ] T097 [US6] Verify integration tests pass

### 8.4 Commit (MANDATORY)

- [ ] T098 [US6] **Commit completed User Story 6 work**

**Checkpoint**: Parameter smoothing fully functional

---

## Phase 9: Model Crossfade (Cross-Cutting Concern)

**Goal**: Click-free model switching via 10ms internal crossfade (FR-042)

**Independent Test**: Switch models during processing and verify no audible discontinuities

### 9.1 Tests for Model Crossfade (Write FIRST - Must FAIL)

- [ ] T099 [P] Write failing tests for model crossfade activation on model change
- [ ] T100 [P] Write failing tests for 10ms crossfade duration using `crossfadeIncrement()`
- [ ] T101 [P] Write failing tests for equal-power crossfade using `equalPowerGains()`
- [ ] T102 [P] Write failing tests for crossfade completes within 15ms (SC-011)

### 9.2 Implementation for Model Crossfade

- [ ] T103 Implement crossfade state variables: `crossfadeActive_`, `crossfadePosition_`, `crossfadeIncrement_`, `previousModel_`
- [ ] T104 Implement crossfade trigger in `setModel()` when model changes
- [ ] T105 Implement parallel model processing during crossfade in `process()`
- [ ] T106 Implement crossfade completion detection and cleanup
- [ ] T107 Verify crossfade tests pass

### 9.3 Commit (MANDATORY)

- [ ] T108 **Commit completed Model Crossfade work**

**Checkpoint**: Model switching is click-free

---

## Phase 10: Expert Mode - J-A Parameters (Cross-Cutting Concern)

**Goal**: Advanced users can configure Jiles-Atherton parameters directly (FR-030b, FR-030c)

**Independent Test**: Set custom J-A parameters and verify they affect hysteresis output

### 10.1 Tests for Expert Mode (Write FIRST - Must FAIL)

- [ ] T109 [P] Write failing tests for `setJAParams(float a, float alpha, float c, float k, float Ms)`
- [ ] T110 [P] Write failing tests for `getJA_a()`, `getJA_alpha()`, `getJA_c()`, `getJA_k()`, `getJA_Ms()` getters
- [ ] T111 [P] Write failing tests for custom J-A parameters affecting hysteresis output

### 10.2 Implementation for Expert Mode

- [ ] T112 Add J-A parameter member variables: `ja_a_`, `ja_alpha_`, `ja_c_`, `ja_k_`, `ja_Ms_`
- [ ] T113 Implement `setJAParams()` setter in `dsp/include/krate/dsp/processors/tape_saturator.h`
- [ ] T114 Implement J-A parameter getters in `dsp/include/krate/dsp/processors/tape_saturator.h`
- [ ] T115 Update hysteresis computation to use member J-A parameters instead of constants
- [ ] T116 Verify expert mode tests pass

### 10.3 Commit (MANDATORY)

- [ ] T117 **Commit completed Expert Mode work**

**Checkpoint**: Expert J-A parameters accessible

---

## Phase 11: T-Scaling for Sample Rate Independence (Cross-Cutting Concern)

**Goal**: Consistent hysteresis behavior across sample rates via T-scaling (FR-030d)

**Independent Test**: Compare outputs at 44.1kHz, 48kHz, 96kHz, 192kHz - RMS should be within 5%

### 11.1 Tests for T-Scaling (Write FIRST - Must FAIL)

- [ ] T118 [P] Write failing tests for T-scale calculation: `TScale_ = 44100.0 / sampleRate`
- [ ] T119 [P] Write failing tests for sample rate independence: output RMS within 5% across 44.1k/48k/88.2k/96k/192k (SC-008)
- [ ] T120 [P] Write failing tests for dH scaling: `dH = (H_current - H_prev) * TScale_`

### 11.2 Implementation for T-Scaling

- [ ] T121 Add `TScale_` member variable
- [ ] T122 Calculate `TScale_` in `prepare()` based on sample rate
- [ ] T123 Apply T-scaling in hysteresis dH computation
- [ ] T124 Verify T-scaling tests pass

### 11.3 Commit (MANDATORY)

- [ ] T125 **Commit completed T-Scaling work**

**Checkpoint**: Sample rate independence achieved

---

## Phase 12: CPU Benchmarks (Success Criteria Verification)

**Goal**: Verify CPU budget compliance per SC-005 and SC-006

**Independent Test**: Measure cycles/sample at 512-sample blocks, normalize to 2.5GHz baseline

### 12.1 Benchmark Tests

- [X] T126 Write benchmark test for Simple model: verify < 0.3% CPU (SC-005)
- [X] T127 Write benchmark test for Hysteresis/RK4: verify < 1.5% CPU (SC-006)
- [X] T128 Write benchmark test comparing all solver CPU costs: RK2 < RK4 < NR4 < NR8
- [X] T129 Run benchmarks and document results in test output

**Benchmark Implementation Notes (Cross-Platform):**
- Windows: Use `__rdtsc()` for cycle counting
- macOS: Use `mach_absolute_time()` for high-resolution timing
- Linux: Use `clock_gettime(CLOCK_MONOTONIC)` for nanosecond precision
- Normalize: Convert to cycles/sample, assume 2.5GHz baseline for percentage calculation
- Test conditions: 512 samples, 44.1kHz, mono, drive=0dB, saturation=0.5

### 12.2 Commit (MANDATORY)

- [ ] T130 **Commit completed Benchmark work**

**Checkpoint**: CPU budget compliance verified

---

## Phase 13: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [X] T130a Write test verifying denormal inputs produce valid outputs without CPU spike (flushDenormal usage)
- [X] T131 Add Doxygen documentation to TapeSaturator class and all public methods (FR-051)
- [X] T132 Verify naming conventions: trailing underscore for members, PascalCase for class, camelCase for methods (FR-052)
- [X] T133 Verify all includes use `<krate/dsp/...>` pattern for Layer 0/1 dependencies
- [X] T134 Code cleanup: remove any unused code, ensure consistent formatting
- [X] T135 Run full test suite: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure`
- [X] T136 Run quickstart.md validation: verify all examples compile and work

---

## Phase 14: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 14.1 Architecture Documentation Update

- [X] T137 **Update `specs/_architecture_/layer-2-processors.md`** with TapeSaturator entry:
  - Purpose: Tape saturation with Simple (tanh+emphasis) and Hysteresis (Jiles-Atherton) models
  - Public API summary: prepare(), reset(), process(), setModel/Solver/Drive/Saturation/Bias/Mix, setJAParams
  - File location: `dsp/include/krate/dsp/processors/tape_saturator.h`
  - When to use: Tape delay age/warmth, lo-fi effects, creative saturation with memory effects

### 14.2 Final Commit

- [ ] T138 **Commit architecture documentation updates**
- [ ] T139 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 15: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 15.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T140 **Review ALL FR-xxx requirements** from spec.md against implementation (FR-001 through FR-052)
- [X] T141 **Review ALL SC-xxx success criteria** and verify measurable targets are achieved (SC-001 through SC-011)
- [X] T142 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in new code
  - [X] No test thresholds relaxed from spec requirements
  - [X] No features quietly removed from scope

### 15.2 Fill Compliance Table in spec.md

- [X] T143 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [X] T144 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 15.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **NO**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **NO**
3. Did I remove ANY features from scope without telling the user? **NO**
4. Would the spec author consider this "done"? **YES**
5. If I were the user, would I feel cheated? **NO**

- [X] T145 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 16: Final Completion

**Purpose**: Final commit and completion claim

### 16.1 Final Commit

- [X] T146 **Commit all spec work** to feature branch
- [X] T147 **Verify all tests pass**

### 16.2 Completion Claim

- [X] T148 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup)
    |
    v
Phase 2 (Foundational) -----> BLOCKS all user stories
    |
    +---> Phase 3 (US1: Simple Model) [P1]
    |         |
    |         v
    +---> Phase 4 (US2: Hysteresis Model) [P1]
    |         |
    |         v
    +---> Phase 5 (US3: Solver Selection) [P2] -- depends on US2
    |         |
    |         v
    +---> Phase 6 (US4: Parameter Control) [P2] -- depends on US1, US2
    |         |
    |         v
    +---> Phase 7 (US5: Mix Control) [P3]
    |         |
    |         v
    +---> Phase 8 (US6: Parameter Smoothing) [P3]
    |
    v
Phase 9 (Model Crossfade) -- depends on US1, US2
    |
    v
Phase 10 (Expert J-A) -- depends on US2
    |
    v
Phase 11 (T-Scaling) -- depends on US2
    |
    v
Phase 12 (CPU Benchmarks) -- depends on all models/solvers
    |
    v
Phase 13-16 (Polish, Docs, Verification, Completion)
```

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P1)**: Can start after Foundational (Phase 2) - No dependencies on US1
- **User Story 3 (P2)**: Depends on US2 (Hysteresis model must exist for solver selection)
- **User Story 4 (P2)**: Depends on US1 and US2 (parameter effects apply to both models)
- **User Story 5 (P3)**: Can start after Foundational - independent mix logic
- **User Story 6 (P3)**: Can start after Foundational - independent smoothing logic

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation after tests
- Integration tests after core implementation
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in `dsp/tests/CMakeLists.txt`
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- All Setup tasks can run in parallel
- All Foundational tests marked [P] can run in parallel
- User Stories 1 and 2 can run in parallel (both P1, no interdependency)
- User Stories 5 and 6 can run in parallel (both P3, independent logic)
- All tests for a user story marked [P] can run in parallel
- Cross-cutting phases (9, 10, 11) should run sequentially after user stories

---

## Parallel Example: User Stories 1 & 2

```bash
# Launch US1 and US2 tests in parallel (both P1 priority):

# US1 tests (Simple model):
Task: T026 "Write failing tests for pre-emphasis filter configuration"
Task: T027 "Write failing tests for de-emphasis filter configuration"
Task: T028 "Write failing tests for Simple model signal flow"

# US2 tests (Hysteresis model) - IN PARALLEL:
Task: T042 "Write failing tests for J-A default parameters"
Task: T043 "Write failing tests for magnetization state persistence"
Task: T044 "Write failing tests for Langevin function implementation"
```

---

## Implementation Strategy

### MVP First (User Stories 1 & 2)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (Simple Model)
4. Complete Phase 4: User Story 2 (Hysteresis Model)
5. **STOP and VALIDATE**: Test both models independently
6. Deploy/demo if ready - core tape saturation functional

### Incremental Delivery

1. Setup + Foundational -> Foundation ready
2. Add User Story 1 (Simple) -> Test independently -> Basic tape saturation works
3. Add User Story 2 (Hysteresis) -> Test independently -> Full tape modeling works
4. Add User Story 3 (Solvers) -> Test independently -> CPU/accuracy tradeoffs available
5. Add User Stories 4-6 -> Test independently -> Full parameter control
6. Add Cross-Cutting (9-11) -> Model switching, expert mode, sample rate independence
7. Each increment adds value without breaking previous functionality

---

## Summary

| Metric | Value |
|--------|-------|
| **Total Tasks** | 148 |
| **Phase 1 (Setup)** | 3 tasks |
| **Phase 2 (Foundational)** | 22 tasks |
| **User Story 1 (Simple Model)** | 16 tasks |
| **User Story 2 (Hysteresis)** | 16 tasks |
| **User Story 3 (Solvers)** | 13 tasks |
| **User Story 4 (Parameters)** | 8 tasks |
| **User Story 5 (Mix)** | 7 tasks |
| **User Story 6 (Smoothing)** | 13 tasks |
| **Cross-Cutting (Crossfade, Expert, T-Scale)** | 27 tasks |
| **Benchmarks** | 5 tasks |
| **Polish + Docs + Verification** | 18 tasks |

### Parallel Opportunities

- Foundational tests (T004-T005) can run in parallel
- User Stories 1 and 2 can run in parallel (both P1)
- User Stories 5 and 6 can run in parallel (both P3)
- All tests within a user story marked [P] can run in parallel
- Cross-cutting phases should run sequentially after user stories

### Independent Test Criteria

| User Story | Independent Test |
|------------|------------------|
| US1 (Simple) | Process audio, verify HF saturates earlier than LF |
| US2 (Hysteresis) | Process audio, verify waveform asymmetry on rising/falling edges |
| US3 (Solvers) | Measure CPU time and compare outputs between RK2/RK4/NR4/NR8 |
| US4 (Parameters) | Sweep drive/saturation/bias, measure harmonic changes |
| US5 (Mix) | Verify mix=0 bypasses, mix=1 is wet, mix=0.5 is blend |
| US6 (Smoothing) | Rapid parameter changes produce no clicks |

### Suggested MVP Scope

**MVP = Phase 1 + Phase 2 + Phase 3 (US1) + Phase 4 (US2)**

This delivers:
- Simple model with pre/de-emphasis (low CPU, immediate tape character)
- Hysteresis model with RK4 solver (authentic magnetic saturation)
- Full parameter control (drive, saturation, bias, mix)
- DC blocking
- Basic lifecycle (prepare, reset, process)

Total MVP tasks: ~57 tasks (Phases 1-4)

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion
- **MANDATORY**: Complete honesty verification before claiming spec complete
- **MANDATORY**: Fill Implementation Verification table in spec.md
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
