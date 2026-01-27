# Tasks: Diffusion Network

**Input**: Design documents from `/specs/015-diffusion-network/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/diffusion_network.h

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Context Check**: Verify `specs/TESTING-GUIDE.md` is in context window. If not, READ IT FIRST.
2. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
3. **Implement**: Write code to make tests pass
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If test file uses `std::isnan`/`std::isfinite`/`std::isinf`:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: Project initialization and file structure

- [X] T001 Create test file `tests/unit/processors/diffusion_network_test.cpp` with Catch2 includes
- [X] T002 Add `diffusion_network_test.cpp` to `tests/CMakeLists.txt` dsp_tests sources
- [X] T003 Add `diffusion_network_test.cpp` to `-fno-fast-math` list in `tests/CMakeLists.txt`

---

## Phase 2: Foundational - AllpassStage Class

**Purpose**: Create the core building block (single allpass filter stage)

**⚠️ CRITICAL**: DiffusionNetwork depends on AllpassStage - this MUST complete first

### 2.1 Pre-Implementation (MANDATORY)

- [X] T004 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 2.2 AllpassStage Tests (Write FIRST - Must FAIL)

- [X] T005 Write unit tests for AllpassStage single sample processing in `tests/unit/processors/diffusion_network_test.cpp`
- [X] T006 Write unit tests for AllpassStage allpass property (flat frequency response) in `tests/unit/processors/diffusion_network_test.cpp`
- [X] T007 Write unit tests for AllpassStage delay time modulation in `tests/unit/processors/diffusion_network_test.cpp`

### 2.3 AllpassStage Implementation

- [X] T008 Create AllpassStage class with DelayLine in `src/dsp/processors/diffusion_network.h`
- [X] T009 Implement AllpassStage::prepare() for delay line setup in `src/dsp/processors/diffusion_network.h`
- [X] T010 Implement AllpassStage::process() with Schroeder formula in `src/dsp/processors/diffusion_network.h`
- [X] T011 Implement AllpassStage::reset() to clear state in `src/dsp/processors/diffusion_network.h`
- [X] T012 Verify AllpassStage tests pass
- [X] T013 **Commit AllpassStage implementation**

**Checkpoint**: AllpassStage class ready for composition into DiffusionNetwork

---

## Phase 3: User Story 1+2 - Basic Diffusion + Size Control (Priority: P1) 🎯 MVP

**Goal**: Create working 8-stage diffusion network with size parameter

**Independent Test**: Process impulse, verify energy spreads over time; verify size=0% bypasses

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T014 [US1] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 3.2 Tests for User Story 1+2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T015 [P] [US1] Write tests for DiffusionNetwork prepare/reset lifecycle in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T016 [P] [US1] Write tests for impulse diffusion (energy spread) in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T017 [P] [US1] Write tests for frequency spectrum preservation (±0.5dB) in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T018 [P] [US2] Write tests for size=0% bypass behavior in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T019 [P] [US2] Write tests for size=50% moderate diffusion in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T020 [P] [US2] Write tests for size=100% maximum diffusion (100-200ms spread) in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T021 [P] [US2] Write tests for size parameter smoothing (no clicks) in `tests/unit/processors/diffusion_network_test.cpp`

### 3.3 Implementation for User Story 1+2

- [ ] T022 [US1] Create DiffusionNetwork class shell matching contract in `src/dsp/processors/diffusion_network.h`
- [ ] T023 [US1] Add 8 AllpassStage instances for left channel in `src/dsp/processors/diffusion_network.h`
- [ ] T024 [US1] Add 8 AllpassStage instances for right channel in `src/dsp/processors/diffusion_network.h`
- [ ] T025 [US1] Implement DiffusionNetwork::prepare() with delay ratio initialization in `src/dsp/processors/diffusion_network.h`
- [ ] T026 [US1] Implement DiffusionNetwork::process() cascading all 8 stages in `src/dsp/processors/diffusion_network.h`
- [ ] T027 [US1] Implement DiffusionNetwork::reset() clearing all stages in `src/dsp/processors/diffusion_network.h`
- [ ] T028 [US2] Add size_ member and sizeSmoother_ in `src/dsp/processors/diffusion_network.h`
- [ ] T029 [US2] Implement setSize() with clamping and smoother target in `src/dsp/processors/diffusion_network.h`
- [ ] T030 [US2] Update process() to scale delay times by smoothed size in `src/dsp/processors/diffusion_network.h`
- [ ] T031 [US1] [US2] Verify all US1+US2 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T032 [US1] **Verify IEEE 754 compliance**: Confirm test file is in `-fno-fast-math` list in `tests/CMakeLists.txt`

### 3.5 Commit (MANDATORY)

- [ ] T033 [US1] [US2] **Commit completed User Story 1+2 work**

**Checkpoint**: Basic diffusion network with size control functional

---

## Phase 4: User Story 3 - Density Control (Priority: P2)

**Goal**: Control number of active diffusion stages (1-8)

**Independent Test**: Verify density=25% uses 2 stages, density=100% uses all 8 stages

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T034 [US3] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 4.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T035 [P] [US3] Write tests for density=25% (2 stages active) in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T036 [P] [US3] Write tests for density=50% (4 stages active) in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T037 [P] [US3] Write tests for density=100% (8 stages active) in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T038 [P] [US3] Write tests for density parameter smoothing (no clicks) in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T039 [P] [US3] Write tests for density crossfade between stage counts in `tests/unit/processors/diffusion_network_test.cpp`

### 4.3 Implementation for User Story 3

- [ ] T040 [US3] Add density_ member and densitySmoother_ in `src/dsp/processors/diffusion_network.h`
- [ ] T041 [US3] Add per-stage enableSmoother_ in AllpassStage in `src/dsp/processors/diffusion_network.h`
- [ ] T042 [US3] Implement setDensity() with stage enable calculation in `src/dsp/processors/diffusion_network.h`
- [ ] T043 [US3] Update process() to crossfade stages based on density in `src/dsp/processors/diffusion_network.h`
- [ ] T044 [US3] Verify all US3 tests pass

### 4.4 Commit (MANDATORY)

- [ ] T045 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Density control functional

---

## Phase 5: User Story 4 - Modulation (Priority: P2)

**Goal**: Add LFO modulation to delay times for movement

**Independent Test**: Verify modDepth=0% produces no pitch artifacts; modDepth>0 adds chorus-like movement

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T046 [US4] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 5.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T047 [P] [US4] Write tests for modDepth=0% (no artifacts) in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T048 [P] [US4] Write tests for modDepth=50% (subtle movement) in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T049 [P] [US4] Write tests for modRate range (0.1Hz-5Hz) in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T050 [P] [US4] Write tests for per-stage phase offsets (decorrelation) in `tests/unit/processors/diffusion_network_test.cpp`

### 5.3 Implementation for User Story 4

- [ ] T051 [US4] Add LFO instance in DiffusionNetwork in `src/dsp/processors/diffusion_network.h`
- [ ] T052 [US4] Add modDepth_, modRate_, and smoothers in `src/dsp/processors/diffusion_network.h`
- [ ] T053 [US4] Implement setModDepth() and setModRate() in `src/dsp/processors/diffusion_network.h`
- [ ] T054 [US4] Update prepare() to configure LFO in `src/dsp/processors/diffusion_network.h`
- [ ] T055 [US4] Update process() to apply modulated delay times per stage in `src/dsp/processors/diffusion_network.h`
- [ ] T056 [US4] Verify all US4 tests pass

### 5.4 Commit (MANDATORY)

- [ ] T057 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Modulation functional

---

## Phase 6: User Story 5 - Stereo Width Control (Priority: P2)

**Goal**: Control stereo decorrelation blend

**Independent Test**: Verify width=0% produces mono (L=R); width=100% produces decorrelated stereo

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T058 [US5] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 6.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T059 [P] [US5] Write tests for width=0% mono output (L=R) in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T060 [P] [US5] Write tests for width=100% decorrelation (cross-correlation < 0.5) in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T061 [P] [US5] Write tests for stereo image preservation in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T062 [P] [US5] Write tests for width parameter smoothing in `tests/unit/processors/diffusion_network_test.cpp`

### 6.3 Implementation for User Story 5

- [ ] T063 [US5] Add width_ member and widthSmoother_ in `src/dsp/processors/diffusion_network.h`
- [ ] T064 [US5] Implement setWidth() with clamping in `src/dsp/processors/diffusion_network.h`
- [ ] T065 [US5] Update process() to blend mono/stereo based on width in `src/dsp/processors/diffusion_network.h`
- [ ] T066 [US5] Verify all US5 tests pass

### 6.4 Commit (MANDATORY)

- [ ] T067 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Width control functional

---

## Phase 7: User Story 6 - Real-Time Safety (Priority: P1)

**Goal**: Verify all real-time safety requirements (noexcept, no allocations)

**Independent Test**: Static analysis confirms noexcept; runtime confirms no allocations in process()

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T068 [US6] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 7.2 Tests for User Story 6 (Write FIRST)

- [ ] T069 [P] [US6] Write tests for process() noexcept verification in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T070 [P] [US6] Write tests for block sizes 1-8192 samples in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T071 [P] [US6] Write tests for in-place processing (input==output buffers) in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T072 [P] [US6] Write tests for zero-length input handling in `tests/unit/processors/diffusion_network_test.cpp`

### 7.3 Implementation for User Story 6

- [ ] T073 [US6] Audit all methods for noexcept correctness in `src/dsp/processors/diffusion_network.h`
- [ ] T074 [US6] Verify no std::vector or allocating operations in process() in `src/dsp/processors/diffusion_network.h`
- [ ] T075 [US6] Add early return for numSamples==0 in `src/dsp/processors/diffusion_network.h`
- [ ] T076 [US6] Verify all US6 tests pass

### 7.4 Commit (MANDATORY)

- [ ] T077 [US6] **Commit completed User Story 6 work**

**Checkpoint**: Real-time safety verified

---

## Phase 8: Edge Cases & Polish

**Purpose**: Handle edge cases and cross-cutting concerns

- [ ] T078 [P] Write tests for NaN/Infinity input handling in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T079 [P] Write tests for sample rate changes (prepare called multiple times) in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T080 [P] Write tests for extreme parameter values (clamping) in `tests/unit/processors/diffusion_network_test.cpp`
- [ ] T081 Implement NaN/Infinity input protection in `src/dsp/processors/diffusion_network.h`
- [ ] T082 Verify all edge case tests pass
- [ ] T083 Run full test suite and verify all tests pass
- [ ] T084 **Commit edge case handling**

---

## Phase 9: Documentation Update (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 9.1 Architecture Documentation Update

- [ ] T085 **Update ARCHITECTURE.md** with DiffusionNetwork component:
  - Add entry to Layer 2 DSP Processors section
  - Include: purpose, public API summary, file location, "when to use this"
  - Add usage example
  - Verify no duplicate functionality was introduced

### 9.2 Final Documentation Commit

- [ ] T086 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T087 **Review ALL FR-xxx requirements** from spec.md against implementation (FR-001 through FR-030)
- [ ] T088 **Review ALL SC-xxx success criteria** and verify measurable targets are achieved (SC-001 through SC-008)
- [ ] T089 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [ ] T090 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T091 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

- [ ] T092 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Commit

- [ ] T093 **Commit all spec work** to feature branch
- [ ] T094 **Verify all tests pass**

### 11.2 Completion Claim

- [ ] T095 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup) ─────────────────────────────────────────────┐
                                                              │
Phase 2 (Foundational: AllpassStage) ←───────────────────────┘
           │
           ▼
Phase 3 (US1+US2: Basic + Size) 🎯 MVP ─────────┐
           │                                     │
           ▼                                     │
Phase 4 (US3: Density) ──────────────────────────┤
           │                                     │
           ▼                                     │
Phase 5 (US4: Modulation) ───────────────────────┤
           │                                     │
           ▼                                     │
Phase 6 (US5: Stereo Width) ─────────────────────┤
           │                                     │
           ▼                                     │
Phase 7 (US6: RT Safety) ────────────────────────┘
           │
           ▼
Phase 8 (Edge Cases)
           │
           ▼
Phase 9-11 (Documentation & Verification)
```

### User Story Dependencies

- **US1+US2 (Basic + Size)**: Requires AllpassStage - MVP deliverable
- **US3 (Density)**: Requires US1+US2 complete
- **US4 (Modulation)**: Requires US1+US2 complete (can parallel with US3)
- **US5 (Stereo Width)**: Requires US1+US2 complete (can parallel with US3, US4)
- **US6 (RT Safety)**: Can run after all features implemented

### Parallel Opportunities

Within each phase, tasks marked [P] can run in parallel:

```
Phase 3 parallel tests:
  T015, T016, T017, T018, T019, T020, T021 (all [P])

Phase 4 parallel tests:
  T035, T036, T037, T038, T039 (all [P])

Phase 5 parallel tests:
  T047, T048, T049, T050 (all [P])

Phase 6 parallel tests:
  T059, T060, T061, T062 (all [P])

Phase 7 parallel tests:
  T069, T070, T071, T072 (all [P])
```

---

## Implementation Strategy

### MVP First (User Stories 1+2)

1. Complete Phase 1: Setup
2. Complete Phase 2: AllpassStage (CRITICAL - blocks all)
3. Complete Phase 3: US1+US2 (Basic + Size)
4. **STOP and VALIDATE**: Test diffusion independently
5. Deploy/demo if ready

### Incremental Delivery

1. Setup + AllpassStage → Foundation ready
2. Add US1+US2 (Basic + Size) → MVP! Commit
3. Add US3 (Density) → Commit
4. Add US4 (Modulation) → Commit
5. Add US5 (Stereo Width) → Commit
6. Add US6 (RT Safety verification) → Commit
7. Edge cases + Documentation → Complete

### Single Developer Flow

```
T001-T003 → T004-T013 → T014-T033 → T034-T045 → T046-T057 → T058-T067 → T068-T077 → T078-T095
```

---

## Summary

| Phase | Focus | Tasks | Parallel |
|-------|-------|-------|----------|
| 1 | Setup | T001-T003 | No |
| 2 | AllpassStage | T004-T013 | Partial |
| 3 | US1+US2 (MVP) | T014-T033 | Yes (tests) |
| 4 | US3 Density | T034-T045 | Yes (tests) |
| 5 | US4 Modulation | T046-T057 | Yes (tests) |
| 6 | US5 Stereo | T058-T067 | Yes (tests) |
| 7 | US6 RT Safety | T068-T077 | Yes (tests) |
| 8 | Edge Cases | T078-T084 | Partial |
| 9 | Documentation | T085-T086 | No |
| 10 | Verification | T087-T092 | No |
| 11 | Completion | T093-T095 | No |

**Total Tasks**: 95
**MVP Tasks** (through Phase 3): 33
