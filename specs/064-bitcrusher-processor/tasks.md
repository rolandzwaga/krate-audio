# Tasks: BitcrusherProcessor

**Input**: Design documents from `/specs/064-bitcrusher-processor/`
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
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and test file structure

- [ ] T001 Create test file skeleton at `dsp/tests/unit/processors/bitcrusher_processor_test.cpp` with Catch2 includes and test tags `[bitcrusher_processor]`
- [ ] T002 Create header file skeleton at `dsp/include/krate/dsp/processors/bitcrusher_processor.h` with namespace `Krate::DSP`, includes, and empty class declaration
- [ ] T003 Verify build system picks up new files: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Enumerations and Constants (FR-004a, FR-004c)

- [ ] T004 [P] Write failing tests for `ProcessingOrder` enum (BitCrushFirst=0, SampleReduceFirst=1) in `dsp/tests/unit/processors/bitcrusher_processor_test.cpp`
- [ ] T005 [P] Write failing tests for constants: kMinBitDepth=4, kMaxBitDepth=16, kMinReductionFactor=1, kMaxReductionFactor=8, kMinGainDb=-24, kMaxGainDb=+24, kDefaultSmoothingMs=5, kDCBlockerCutoffHz=10, kDitherGateThresholdDb=-60
- [ ] T006 Implement `ProcessingOrder` enum and constants in `dsp/include/krate/dsp/processors/bitcrusher_processor.h`
- [ ] T007 Verify enum and constant tests pass

### 2.2 Default Constructor and Getters (FR-001 to FR-006)

- [ ] T008 Write failing tests for default constructor: bitDepth=16, reductionFactor=1, ditherAmount=0, preGainDb=0, postGainDb=0, mix=1, processingOrder=BitCrushFirst, ditherGateEnabled=true
- [ ] T009 Write failing tests for getters: `getBitDepth()`, `getReductionFactor()`, `getDitherAmount()`, `getPreGain()`, `getPostGain()`, `getMix()`, `getProcessingOrder()`, `isDitherGateEnabled()`
- [ ] T010 Implement BitcrusherProcessor class with default constructor and getter methods in `dsp/include/krate/dsp/processors/bitcrusher_processor.h`
- [ ] T011 Verify constructor and getter tests pass

### 2.3 Parameter Setters with Clamping (FR-001 to FR-006)

- [ ] T012 Write failing tests for `setBitDepth(float)` with clamping to [4, 16]
- [ ] T013 Write failing tests for `setReductionFactor(float)` with clamping to [1, 8]
- [ ] T014 Write failing tests for `setDitherAmount(float)` with clamping to [0, 1]
- [ ] T015 Write failing tests for `setPreGain(float dB)` with clamping to [-24, +24]
- [ ] T016 Write failing tests for `setPostGain(float dB)` with clamping to [-24, +24]
- [ ] T017 Write failing tests for `setMix(float)` with clamping to [0, 1]
- [ ] T018 Write failing tests for `setProcessingOrder(ProcessingOrder)`
- [ ] T019 Write failing tests for `setDitherGateEnabled(bool)`
- [ ] T020 Implement all parameter setters with clamping in `dsp/include/krate/dsp/processors/bitcrusher_processor.h`
- [ ] T021 Verify setter tests pass

### 2.4 Lifecycle Methods (FR-014, FR-015, FR-016)

- [ ] T022 Write failing tests for `prepare(double sampleRate, size_t maxBlockSize)` - configures components
- [ ] T023 Write failing tests for `reset()` - clears filter state, snaps smoothers
- [ ] T024 Write failing tests for process() before prepare() returns input unchanged
- [ ] T025 Implement `prepare()` and `reset()` in `dsp/include/krate/dsp/processors/bitcrusher_processor.h`
- [ ] T026 Verify lifecycle tests pass

### 2.5 Foundational Commit

- [ ] T027 Verify all foundational tests pass
- [ ] T028 **Commit completed Foundational phase work**

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Lo-Fi Effect (Priority: P1)

**Goal**: Music producer adds classic lo-fi character by reducing bit depth and sample rate with mix control

**Independent Test**: Process a sine wave through the effect and measure harmonic content and aliasing artifacts

**FR-to-Task Traceability (FR-001, FR-002, FR-004, FR-012, FR-013, FR-020, FR-021):**
| Requirement | Task(s) | Description |
|-------------|---------|-------------|
| FR-001 | T029, T033 | Bit depth reduction [4-16 bits] |
| FR-001a | T030 | Immediate bit depth changes |
| FR-002 | T031, T034 | Sample rate reduction [1-8x] |
| FR-002a | T032 | Immediate factor changes |
| FR-004 | T035, T036 | Dry/wet mix blending |
| FR-012 | T037, T038 | DC blocking after processing |
| FR-013 | T037 | DC blocker at 10Hz |
| FR-020 | T039 | Mix=0 bypass optimization |
| FR-021 | T040 | Minimal processing when bitDepth=16, factor=1 |

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T029 [P] [US1] Write failing tests for bit depth reduction: 16-bit input reduced to 8-bit produces quantization artifacts in `dsp/tests/unit/processors/bitcrusher_processor_test.cpp`
- [ ] T030 [P] [US1] Write failing tests for immediate bit depth changes (no smoothing) - FR-001a
- [ ] T031 [P] [US1] Write failing tests for sample rate reduction: factor=4 produces aliasing artifacts consistent with sample-and-hold
- [ ] T032 [P] [US1] Write failing tests for immediate sample rate factor changes (no smoothing) - FR-002a
- [ ] T033 [P] [US1] Write failing tests for BitCrusher integration: verify setBitDepth() affects quantization levels
- [ ] T034 [P] [US1] Write failing tests for SampleRateReducer integration: verify setReductionFactor() affects decimation
- [ ] T035 [P] [US1] Write failing tests for mix=0% produces output identical to input (SC-007)
- [ ] T036 [P] [US1] Write failing tests for mix=50% produces equal blend of dry and wet
- [ ] T037 [P] [US1] Write failing tests for DC blocker at 10Hz removes DC offset (FR-012, FR-013)
- [ ] T038 [P] [US1] Write failing tests for DC offset below 0.001% after processing (SC-004)
- [ ] T039 [P] [US1] Write failing tests for mix=0% bypass optimization skips wet processing (FR-020)
- [ ] T040 [P] [US1] Write failing tests for bitDepth=16, factor=1 minimal processing (FR-021)

### 3.2 Implementation for User Story 1

- [ ] T041 [US1] Add member primitives: BitCrusher, SampleRateReducer, DCBlocker in `dsp/include/krate/dsp/processors/bitcrusher_processor.h`
- [ ] T042 [US1] Add dryBuffer_ (std::vector<float>) for dry signal storage
- [ ] T043 [US1] Implement `process(float*, size_t)` with signal flow: store dry -> [wet path] -> mix blend
- [ ] T044 [US1] Implement default processing order (BitCrushFirst): BitCrusher -> SampleRateReducer -> DCBlocker
- [ ] T045 [US1] Implement mix bypass optimization: skip wet processing when mix < 0.0001
- [ ] T046 [US1] Verify all US1 tests pass

### 3.3 Integration Tests for User Story 1

- [ ] T047 [US1] Write integration test: bit depth 16->8 produces measurable increase in quantization distortion (SC-001)
- [ ] T048 [US1] Write integration test: factor=4 produces aliasing visible above Nyquist/4 (SC-002)
- [ ] T049 [US1] Verify integration tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T050 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 3.5 Commit (MANDATORY)

- [ ] T051 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Gain Staging for Optimal Drive (Priority: P2)

**Goal**: Sound designer drives audio into bit crusher harder for more aggressive artifacts with makeup gain

**Independent Test**: Verify gain changes in dB translate correctly to amplitude changes and pre-gain affects saturation intensity

**FR-to-Task Traceability (FR-005, FR-006, FR-007):**
| Requirement | Task(s) | Description |
|-------------|---------|-------------|
| FR-005 | T052, T056 | Pre-gain [-24, +24] dB |
| FR-006 | T053, T057 | Post-gain [-24, +24] dB |
| FR-007 | T054, T058 | dB to linear conversion |

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T052 [P] [US2] Write failing tests for pre-gain +12dB increases signal amplitude before processing
- [ ] T053 [P] [US2] Write failing tests for post-gain -12dB decreases output amplitude after processing
- [ ] T054 [P] [US2] Write failing tests for dB to linear conversion using dbToGain() from db_utils.h (FR-007)
- [ ] T055 [P] [US2] Write failing tests for pre-gain +12dB produces more aggressive quantization artifacts

### 4.2 Implementation for User Story 2

- [ ] T056 [US2] Implement pre-gain application before BitCrusher in process() using dbToGain()
- [ ] T057 [US2] Implement post-gain application after DCBlocker in process() using dbToGain()
- [ ] T058 [US2] Verify gain staging uses Layer 0 dbToGain() from `<krate/dsp/core/db_utils.h>`
- [ ] T059 [US2] Verify all US2 tests pass

### 4.3 Integration Tests for User Story 2

- [ ] T060 [US2] Write integration test: pre-gain +12dB and post-gain -12dB produces similar output level to input
- [ ] T061 [US2] Write integration test: higher pre-gain produces more visible stepping in output waveform
- [ ] T062 [US2] Verify integration tests pass

### 4.4 Commit (MANDATORY)

- [ ] T063 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Dither for Smoother Quantization (Priority: P3)

**Goal**: Audio engineer reduces harshness of quantization by applying TPDF dither with automatic gating

**Independent Test**: Measure THD with and without dither and verify dither gating during silence

**FR-to-Task Traceability (FR-003, FR-003a, FR-003b):**
| Requirement | Task(s) | Description |
|-------------|---------|-------------|
| FR-003 | T064, T068 | TPDF dither [0-100%] |
| FR-003a | T065, T069 | Dither gate at -60dB |
| FR-003b | T066, T070 | Envelope detection for gate |

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T064 [P] [US3] Write failing tests for dither=0% produces stepped waveform with high harmonic content
- [ ] T065 [P] [US3] Write failing tests for dither=100% reduces THD compared to dither=0% (SC-008)
- [ ] T066 [P] [US3] Write failing tests for dither gate activates when signal < -60dB threshold (FR-003a)
- [ ] T067 [P] [US3] Write failing tests for dither gate produces silence (no noise floor) within 10ms (SC-009)
- [ ] T068 [P] [US3] Write failing tests for EnvelopeFollower used for signal detection (FR-003b)
- [ ] T069 [P] [US3] Write failing tests for setDitherGateEnabled(false) always applies dither

### 5.2 Implementation for User Story 3

- [ ] T070 [US3] Add EnvelopeFollower member for dither gate signal detection in `dsp/include/krate/dsp/processors/bitcrusher_processor.h`
- [ ] T071 [US3] Configure EnvelopeFollower with Amplitude mode, 1ms attack, 20ms release in prepare()
- [ ] T072 [US3] Implement dither gate logic: if envelope < dbToGain(-60dB) then dither=0, else dither=ditherAmount_
- [ ] T073 [US3] Apply conditional dither amount to BitCrusher.setDither() per-sample in process()
- [ ] T074 [US3] Verify all US3 tests pass

### 5.3 Integration Tests for User Story 3

- [ ] T075 [US3] Write integration test: dither=100% reduces THD by at least 10dB vs dither=0% for low-level signals (SC-008)
- [ ] T076 [US3] Write integration test: signal drop below -60dB produces silence within 10ms (SC-009)
- [ ] T077 [US3] Verify integration tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T078 [US3] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 5.5 Commit (MANDATORY)

- [ ] T079 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Dither and dither gating fully functional

---

## Phase 6: User Story 4 - Click-Free Parameter Automation (Priority: P4)

**Goal**: Live performer automates parameters without audible clicks or pops

**Independent Test**: Rapidly change parameters during processing and verify no discontinuities

**FR-to-Task Traceability (FR-008 to FR-011):**
| Requirement | Task(s) | Description |
|-------------|---------|-------------|
| FR-008 | T080, T084 | Pre-gain smoothing |
| FR-009 | T081, T085 | Post-gain smoothing |
| FR-010 | T082, T086 | Mix smoothing |
| FR-011 | T083, T087 | 5ms smoothing time |

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T080 [P] [US4] Write failing tests for pre-gain smoothing: change from -24dB to +24dB produces smooth transition (FR-008)
- [ ] T081 [P] [US4] Write failing tests for post-gain smoothing: change produces smooth transition (FR-009)
- [ ] T082 [P] [US4] Write failing tests for mix smoothing: change from 0% to 100% produces smooth transition (FR-010)
- [ ] T083 [P] [US4] Write failing tests for smoothing time approximately 5ms (FR-011, SC-003)
- [ ] T084 [P] [US4] Write failing tests for reset() snaps smoothers to current target values

### 6.2 Implementation for User Story 4

- [ ] T085 [US4] Add OnePoleSmoother members for preGain, postGain, mix in `dsp/include/krate/dsp/processors/bitcrusher_processor.h`
- [ ] T086 [US4] Configure smoothers in prepare() with 5ms smoothing time
- [ ] T087 [US4] Implement per-sample smoothing in process() loop: preGain, postGain, mix
- [ ] T088 [US4] Implement smoother snap in reset() using snapTo()
- [ ] T089 [US4] Verify all US4 tests pass

### 6.3 Integration Tests for User Story 4

- [ ] T090 [US4] Write integration test: rapid mix change 0%->100% produces no audible clicks (SC-006)
- [ ] T091 [US4] Write integration test: smoothing transition completes within 5ms (SC-003)
- [ ] T092 [US4] Verify integration tests pass

### 6.4 Commit (MANDATORY)

- [ ] T093 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Parameter smoothing fully functional

---

## Phase 7: Processing Order (Cross-Cutting Concern)

**Goal**: Configurable processing order (BitCrushFirst vs SampleReduceFirst) per FR-004a

**Independent Test**: Verify different processing orders produce different output characteristics

**FR-to-Task Traceability (FR-004a, FR-004b, FR-004c):**
| Requirement | Task(s) | Description |
|-------------|---------|-------------|
| FR-004a | T094, T098 | ProcessingOrder enum |
| FR-004b | T095, T099 | Immediate switch (no crossfade) |
| FR-004c | T096 | Default is BitCrushFirst |

### 7.1 Tests for Processing Order (Write FIRST - Must FAIL)

- [ ] T094 [P] Write failing tests for ProcessingOrder::BitCrushFirst signal flow: BitCrusher -> SampleRateReducer
- [ ] T095 [P] Write failing tests for ProcessingOrder::SampleReduceFirst signal flow: SampleRateReducer -> BitCrusher
- [ ] T096 [P] Write failing tests for default processing order is BitCrushFirst (FR-004c)
- [ ] T097 [P] Write failing tests for processing order switch produces different outputs (SC-010)

### 7.2 Implementation for Processing Order

- [ ] T098 Implement processBitCrushFirst() private method in `dsp/include/krate/dsp/processors/bitcrusher_processor.h`
- [ ] T099 Implement processSampleReduceFirst() private method in `dsp/include/krate/dsp/processors/bitcrusher_processor.h`
- [ ] T100 Implement processing order dispatch in process() based on processingOrder_ setting
- [ ] T101 Verify immediate switch (no crossfade) per FR-004b
- [ ] T102 Verify processing order tests pass

### 7.3 Integration Tests for Processing Order

- [ ] T103 Write integration test: switching order during processing produces no clicks (SC-010)
- [ ] T104 Write integration test: BitCrushFirst and SampleReduceFirst produce measurably different outputs
- [ ] T105 Verify integration tests pass

### 7.4 Commit (MANDATORY)

- [ ] T106 **Commit completed Processing Order work**

**Checkpoint**: Processing order fully functional

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

### 8.1 Real-Time Safety Tests (FR-017, FR-018, FR-019)

- [ ] T107a [P] Write test verifying all public methods are noexcept (FR-017) - use static_assert or compile-time checks
- [ ] T107b [P] Write test verifying no allocations in process() (FR-018) - use custom allocator or memory tracking
- [ ] T107c [P] Write test verifying O(N) time complexity in process() (FR-019) - benchmark with varying block sizes

### 8.2 Performance Tests (SC-005)

- [ ] T107d Write CPU benchmark test: process 1 second of audio at 44.1kHz, verify < 0.1% CPU overhead (SC-005)

### 8.3 Other Cross-Cutting Tests

- [ ] T107 Write test verifying denormal inputs produce valid outputs without CPU spike (flushDenormal usage)
- [ ] T108 Add Doxygen documentation to BitcrusherProcessor class and all public methods
- [ ] T109 Verify naming conventions: trailing underscore for members, PascalCase for class, camelCase for methods
- [ ] T110 Verify all includes use `<krate/dsp/...>` pattern for Layer 0/1/2 dependencies
- [ ] T111 Code cleanup: remove any unused code, ensure consistent formatting
- [ ] T112 Run full test suite: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure`
- [ ] T113 Run quickstart.md validation: verify all examples compile and work

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [ ] T114 **Update `specs/_architecture_/layer-2-processors.md`** with BitcrusherProcessor entry:
  - Purpose: Bitcrusher effect with bit depth reduction, sample rate decimation, gain staging, dither gating
  - Public API summary: prepare(), reset(), process(), setBitDepth/ReductionFactor/DitherAmount/PreGain/PostGain/Mix/ProcessingOrder
  - File location: `dsp/include/krate/dsp/processors/bitcrusher_processor.h`
  - When to use: Lo-fi effects, retro game audio, creative distortion, sample rate reduction effects

### 9.2 Final Commit

- [ ] T115 **Commit architecture documentation updates**
- [ ] T116 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T117 **Review ALL FR-xxx requirements** from spec.md against implementation (FR-001 through FR-024)
- [ ] T118 **Review ALL SC-xxx success criteria** and verify measurable targets are achieved (SC-001 through SC-010)
- [ ] T119 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [ ] T120 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T121 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T122 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Commit

- [ ] T123 **Commit all spec work** to feature branch
- [ ] T124 **Verify all tests pass**

### 11.2 Completion Claim

- [ ] T125 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

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
    +---> Phase 3 (US1: Basic Lo-Fi Effect) [P1]
    |         |
    |         v
    +---> Phase 4 (US2: Gain Staging) [P2] -- can run parallel after US1
    |         |
    |         v
    +---> Phase 5 (US3: Dither) [P3] -- can run parallel after US1
    |         |
    |         v
    +---> Phase 6 (US4: Parameter Smoothing) [P4]
    |
    v
Phase 7 (Processing Order) -- depends on US1
    |
    v
Phase 8-11 (Polish, Docs, Verification, Completion)
```

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P2)**: Depends on US1 (needs process() working to add gain staging)
- **User Story 3 (P3)**: Depends on US1 (needs BitCrusher integration for dither control)
- **User Story 4 (P4)**: Depends on US2 (needs gain parameters to add smoothing)

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation after tests
- Integration tests after core implementation
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in `dsp/tests/CMakeLists.txt`
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- All Setup tasks can run sequentially (T001-T003)
- All Foundational tests marked [P] can run in parallel
- User Stories 2, 3 can potentially run in parallel after US1 if implementation allows
- All tests for a user story marked [P] can run in parallel
- Processing Order phase should run after US1 is complete

---

## Parallel Example: User Story 1 Tests

```bash
# Launch all US1 tests in parallel:
Task: T029 "Write failing tests for bit depth reduction"
Task: T030 "Write failing tests for immediate bit depth changes"
Task: T031 "Write failing tests for sample rate reduction"
Task: T032 "Write failing tests for immediate factor changes"
Task: T033 "Write failing tests for BitCrusher integration"
Task: T034 "Write failing tests for SampleRateReducer integration"
Task: T035 "Write failing tests for mix=0% bypass"
Task: T036 "Write failing tests for mix=50% blend"
Task: T037 "Write failing tests for DC blocker"
Task: T038 "Write failing tests for DC offset threshold"
Task: T039 "Write failing tests for bypass optimization"
Task: T040 "Write failing tests for minimal processing"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (Basic Lo-Fi Effect)
4. **STOP and VALIDATE**: Test US1 independently
5. Deploy/demo if ready - core bitcrusher functional

### Incremental Delivery

1. Setup + Foundational -> Foundation ready
2. Add User Story 1 (Basic Lo-Fi) -> Test independently -> Bitcrusher works
3. Add User Story 2 (Gain Staging) -> Test independently -> Drive/makeup gain works
4. Add User Story 3 (Dither) -> Test independently -> Dither with gating works
5. Add User Story 4 (Smoothing) -> Test independently -> Click-free automation
6. Add Processing Order (Phase 7) -> Both processing orders work
7. Each increment adds value without breaking previous functionality

---

## Summary

| Metric | Value |
|--------|-------|
| **Total Tasks** | 129 |
| **Phase 1 (Setup)** | 3 tasks |
| **Phase 2 (Foundational)** | 25 tasks |
| **User Story 1 (Basic Lo-Fi)** | 23 tasks |
| **User Story 2 (Gain Staging)** | 12 tasks |
| **User Story 3 (Dither)** | 16 tasks |
| **User Story 4 (Smoothing)** | 14 tasks |
| **Processing Order (Phase 7)** | 13 tasks |
| **Polish + Docs + Verification** | 23 tasks |

### Parallel Opportunities

- Foundational tests (T004-T005) can run in parallel
- All US1 tests (T029-T040) can run in parallel
- All US2 tests (T052-T055) can run in parallel
- All US3 tests (T064-T069) can run in parallel
- All US4 tests (T080-T084) can run in parallel
- Processing order tests (T094-T097) can run in parallel

### Independent Test Criteria

| User Story | Independent Test |
|------------|------------------|
| US1 (Basic Lo-Fi) | Process sine wave, verify quantization artifacts and aliasing |
| US2 (Gain Staging) | Verify dB to amplitude conversion, pre-gain affects crushing intensity |
| US3 (Dither) | Measure THD with/without dither, verify gating at -60dB |
| US4 (Smoothing) | Rapid parameter changes produce no clicks |

### Suggested MVP Scope

**MVP = Phase 1 + Phase 2 + Phase 3 (US1)**

This delivers:
- Bit depth reduction [4-16 bits]
- Sample rate reduction [1-8x factor]
- Dry/wet mix blending
- DC blocking
- Basic lifecycle (prepare, reset, process)
- Mix=0 bypass optimization

Total MVP tasks: ~51 tasks (Phases 1-3)

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
