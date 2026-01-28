# Tasks: Band Management

**Input**: Design documents from `/specs/002-band-management/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/, research.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

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
   - Add the file to the `-fno-fast-math` list in `plugins/disrumpo/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/crossover_network_test.cpp  # ADD YOUR FILE HERE
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

**Purpose**: Create DSP directory structure for band management components

- [X] T001 Create dsp/ directory structure in plugins/disrumpo/src/dsp/
- [X] T002 Create tests/unit/ directory structure in plugins/disrumpo/tests/
- [X] T003 Verify build system configured for Disrumpo plugin tests (CMakeLists.txt exists)

**Checkpoint**: Directory structure ready

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core data structures and parameter IDs that ALL user stories depend on

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T004 Create BandState structure in plugins/disrumpo/src/dsp/band_state.h (per data-model.md)
- [X] T005 Add BandParamType enum and makeBandParamId() helper to plugins/disrumpo/src/plugin_ids.h
- [X] T006 Add kBandCountId constant (0x0F03) to plugins/disrumpo/src/plugin_ids.h
- [X] T007 Add CrossoverParamType and makeCrossoverParamId() helper to plugins/disrumpo/src/plugin_ids.h

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Dynamic Band Count Configuration (Priority: P1) 🎯 MVP

**Goal**: Implement configurable 1-8 band crossover network with phase-coherent summation

**Independent Test**: Load plugin, change band count parameter, verify correct number of bands are active with proper frequency distribution

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T008 [P] [US1] Write failing unit tests for CrossoverNetwork in plugins/disrumpo/tests/unit/crossover_network_test.cpp
  - Test: 1 band passes input unchanged
  - Test: 2 bands split signal (low + high = input)
  - Test: 4 bands sum to flat response within +/-0.1 dB
  - Test: 8 bands configuration works
  - Test: Band count change preserves existing crossovers (FR-011a)
  - Test: Band count decrease preserves lowest N-1 crossovers (FR-011b)
  - Test: Logarithmic default frequency distribution

### 3.2 Implementation for User Story 1

- [X] T009 [US1] Create CrossoverNetwork class declaration in plugins/disrumpo/src/dsp/crossover_network.h (per contracts/crossover_network_api.md)
- [X] T010 [US1] Implement CrossoverNetwork::prepare() and reset() methods
- [X] T011 [US1] Implement CrossoverNetwork::process() with cascaded LR4 splitting (FR-001a, FR-012, FR-013, FR-014)
- [X] T012 [US1] Implement CrossoverNetwork::setBandCount() with frequency preservation logic (FR-011a/FR-011b)
- [X] T013 [US1] Implement CrossoverNetwork::setCrossoverFrequency() with smoothing
- [X] T014 [US1] Implement logarithmic frequency redistribution algorithm (FR-009)
- [X] T015 [US1] Verify all tests pass (SC-001: flat response within +/-0.1 dB)
- [X] T016 [US1] Add sample rate tests (44.1kHz, 48kHz, 96kHz, 192kHz) per SC-007

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T017 [US1] **Verify IEEE 754 compliance**: Check if crossover_network_test.cpp uses std::isnan/std::isfinite/std::isinf → add to -fno-fast-math list in plugins/disrumpo/tests/CMakeLists.txt

### 3.4 Commit (MANDATORY)

- [ ] T018 [US1] **Commit completed User Story 1 work** (CrossoverNetwork implementation)

**Checkpoint**: CrossoverNetwork should be fully functional, phase-coherent, and tested at all sample rates

---

## Phase 4: User Story 2 - Phase-Coherent Band Summation (Priority: P1)

**Goal**: Verify that multiband splitting and summation produces flat frequency response with no coloration

**Independent Test**: Process pink noise through crossover network with bypass processing, measure frequency response deviation from flat

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T019 [P] [US2] Write failing pink noise FFT test in plugins/disrumpo/tests/unit/crossover_network_test.cpp
  - Test: Pink noise summation flat within +/-0.1 dB (FR-031)
  - Test: Crossover frequency sweeps produce no clicks (FR-010)
  - Test: All sample rates maintain phase coherence (FR-032)

### 4.2 Implementation for User Story 2

- [ ] T020 [US2] Implement pink noise generator for FFT testing
- [ ] T021 [US2] Implement FFT analysis helper for frequency response measurement
- [ ] T022 [US2] Add crossover frequency smoothing tests
- [ ] T023 [US2] Verify all summation tests pass (SC-001)

### 4.3 Cross-Platform Verification (MANDATORY)

- [ ] T024 [US2] **Verify IEEE 754 compliance**: Check if FFT test code uses std::isnan/std::isfinite/std::isinf → add to -fno-fast-math list in plugins/disrumpo/tests/CMakeLists.txt

### 4.4 Commit (MANDATORY)

- [ ] T025 [US2] **Commit completed User Story 2 work** (Phase-coherent summation verification)

**Checkpoint**: Phase coherence verified via FFT analysis at all sample rates

---

## Phase 5: User Story 3 - Per-Band Gain and Pan Control (Priority: P2)

**Goal**: Independent control over each band's level and stereo position

**Independent Test**: Adjust gain and pan for individual bands, verify correct amplitude scaling and stereo positioning

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T026 [P] [US3] Write failing unit tests for BandProcessor in plugins/disrumpo/tests/unit/band_processing_test.cpp
  - Test: +6dB gain doubles amplitude (FR-019)
  - Test: dB to linear conversion (FR-020)
  - Test: Pan -1.0 = full left (left=1.0, right=0.0)
  - Test: Pan 0.0 = center (left=0.707, right=0.707)
  - Test: Pan +1.0 = full right (left=0.0, right=1.0)
  - Test: Equal-power pan law maintains constant power (FR-022)
  - Test: Parameter smoothing is click-free

### 5.2 Implementation for User Story 3

- [X] T027 [P] [US3] Create BandProcessor class declaration in plugins/disrumpo/src/dsp/band_processor.h (per contracts/band_processor_api.md)
- [X] T028 [US3] Implement BandProcessor::prepare() and reset() methods
- [X] T029 [US3] Implement BandProcessor::setGainDb() with dB to linear conversion (FR-020)
- [X] T030 [US3] Implement BandProcessor::setPan() with clamping to [-1, +1]
- [X] T031 [US3] Implement BandProcessor::process() with equal-power pan law (FR-022)
- [X] T032 [US3] Configure OnePoleSmoother instances with 10ms default smoothing (FR-027a)
  - Use `kDefaultSmoothingMs = 10.0f` constant in BandProcessor
  - This implements the "hidden parameter" - settable in code for testing but not exposed to UI
- [X] T033 [US3] Verify all tests pass

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T034 [US3] **Verify IEEE 754 compliance**: Check if band_processing_test.cpp uses std::isnan/std::isfinite/std::isinf → add to -fno-fast-math list in plugins/disrumpo/tests/CMakeLists.txt

### 5.4 Commit (MANDATORY)

- [ ] T035 [US3] **Commit completed User Story 3 work** (BandProcessor gain/pan implementation)

**Checkpoint**: Per-band gain and pan should work with smooth, click-free transitions

---

## Phase 6: User Story 4 - Solo/Bypass/Mute Controls (Priority: P2)

**Goal**: Isolate individual bands for debugging and creative processing

**Independent Test**: Engage solo/bypass/mute on individual bands, verify correct behavior

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T036 [P] [US4] Write failing unit tests for solo/mute logic in plugins/disrumpo/tests/unit/band_processing_test.cpp
  - Test: Solo Band 2 silences all other bands (FR-025) - Note: Solo logic is processor-level, deferred to processor integration
  - Test: Multiple solos work (all soloed bands play) (FR-026) - Deferred to processor integration
  - Test: Mute suppresses band output (FR-023) - DONE
  - Test: Mute overrides solo (FR-025a) - Deferred to processor integration
  - Test: Bypass flag is respected (FR-024) - Deferred (bypass used for distortion in future)
  - Test: Solo/mute transitions are click-free (SC-005) - DONE

### 6.2 Implementation for User Story 4

- [X] T037 [US4] Implement BandProcessor::setMute() with smooth fade (FR-027)
- [ ] T038 [US4] Add solo/mute interaction logic to processor summation (FR-025, FR-025a)
- [ ] T039 [US4] Implement shouldBandContribute() helper for solo/mute logic
- [X] T040 [US4] Verify all tests pass (SC-005: click-free transitions)

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T041 [US4] **Verify IEEE 754 compliance**: Check if solo/mute test code uses std::isnan/std::isfinite/std::isinf → add to -fno-fast-math list in plugins/disrumpo/tests/CMakeLists.txt

### 6.4 Commit (MANDATORY)

- [ ] T042 [US4] **Commit completed User Story 4 work** (Solo/bypass/mute implementation)

**Checkpoint**: Solo/mute controls should work correctly with smooth transitions

---

## Phase 7: User Story 5 - Manual Crossover Frequency Adjustment (Priority: P3)

**Goal**: Precise control over crossover frequencies rather than automatic distribution

**Independent Test**: Adjust individual crossover frequencies, verify they update correctly with minimum spacing constraint

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T043 [P] [US5] Write failing unit tests for manual crossover adjustment in plugins/disrumpo/tests/unit/crossover_network_test.cpp
  - Test: Set crossover 1 to 250Hz (FR-035)
  - Test: Minimum spacing constraint enforced (0.5 octaves)
  - Test: Manual values persist after setBandCount()
  - Test: Crossover frequencies clamp to [20Hz, sampleRate*0.45]

### 7.2 Implementation for User Story 5

- [ ] T044 [US5] Verify crossover frequency parameters are registered in Phase 9 (T066) - UI control is deferred to spec 004-vstgui-infrastructure but parameters are registered in this spec per FR-035
- [ ] T045 [US5] Verify setCrossoverFrequency() enforces minimum spacing
- [ ] T046 [US5] Verify all tests pass

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T047 [US5] **Verify IEEE 754 compliance**: Check if manual crossover tests use std::isnan/std::isfinite/std::isinf → add to -fno-fast-math list in plugins/disrumpo/tests/CMakeLists.txt

### 7.4 Commit (MANDATORY)

- [ ] T048 [US5] **Commit completed User Story 5 work** (Manual crossover frequency adjustment)

**Checkpoint**: Manual crossover adjustment should work with proper spacing constraints

---

## Phase 8: Processor Integration (Cross-Story)

**Purpose**: Integrate band management into Disrumpo processor

### 8.1 Processor Class Updates

- [ ] T049 Add band management members to plugins/disrumpo/src/processor/processor.h
  - Two CrossoverNetwork instances (L, R channels)
  - Array of 8 BandState instances
  - Array of 8 BandProcessor instances
  - std::atomic<int> bandCount_{4}

- [ ] T050 Update Processor::setupProcessing() to prepare crossover networks and band processors

- [ ] T051 Implement band splitting in Processor::process() (L and R channels independently per FR-001b)

- [ ] T052 Implement per-band processing loop in Processor::process()

- [ ] T053 Implement band summation with solo/mute logic in Processor::process()

- [ ] T054 Update Processor::processParameterChanges() to handle band parameters
  - Band count changes (kBandCountId)
  - Per-band gain (makeBandParamId(band, kBandGain))
  - Per-band pan (makeBandParamId(band, kBandPan))
  - Per-band solo/bypass/mute flags

### 8.2 State Serialization Updates

- [ ] T055 Update Processor::getState() to serialize band states (FR-037) [Depends on: T004 BandState structure]
  - Version (int32) - remains 1
  - Global parameters (existing)
  - Band count (int32)
  - For each of 8 bands: gainDb, pan, solo, bypass, mute
  - Crossover frequencies (7 floats)

- [ ] T056 Update Processor::setState() to deserialize band states (FR-038) [Depends on: T004 BandState structure]

### 8.3 Integration Tests

- [ ] T057 [P] Write integration test IT-001 in plugins/disrumpo/tests/integration/
  - Test: Full signal path through crossover → bands → summation
  - Test: Audio passes without corruption
  - Test: Flat response verification end-to-end

- [ ] T058 [P] Write integration test IT-004 in plugins/disrumpo/tests/integration/
  - Test: Dynamic band count changes during playback
  - Test: No clicks or artifacts during transition (SC-002)

- [ ] T059 Run all integration tests and verify they pass

### 8.4 Cross-Platform Verification (MANDATORY)

- [ ] T060 **Verify IEEE 754 compliance**: Check if integration tests use std::isnan/std::isfinite/std::isinf → add to -fno-fast-math list in plugins/disrumpo/tests/CMakeLists.txt

### 8.5 Commit (MANDATORY)

- [ ] T061 **Commit completed processor integration work**

**Checkpoint**: Band management fully integrated into processor with working signal path

---

## Phase 9: Controller Integration

**Purpose**: Register per-band parameters in VST3 controller

### 9.1 Parameter Registration

- [ ] T062 Register band count parameter in plugins/disrumpo/src/controller/controller.cpp (FR-034)
  - RangeParameter: "Band Count", kBandCountId, range [1, 8], default 4

- [ ] T063 Register per-band gain parameters in plugins/disrumpo/src/controller/controller.cpp
  - For bands 0-7: RangeParameter with makeBandParamId(band, kBandGain)
  - Range: [-24, +24] dB, default 0

- [ ] T064 Register per-band pan parameters in plugins/disrumpo/src/controller/controller.cpp
  - For bands 0-7: RangeParameter with makeBandParamId(band, kBandPan)
  - Range: [-1, +1], default 0

- [ ] T065 Register per-band solo/bypass/mute parameters in plugins/disrumpo/src/controller/controller.cpp
  - For bands 0-7: Parameter (boolean) for solo, bypass, mute

- [ ] T066 Register crossover frequency parameters in plugins/disrumpo/src/controller/controller.cpp (FR-035)
  - For crossovers 0-6: RangeParameter with makeCrossoverParamId(index)
  - Range: [20, 20000] Hz with logarithmic normalization
  - Note: UI control deferred to spec 004-vstgui-infrastructure

### 9.2 Commit (MANDATORY)

- [ ] T067 **Commit completed controller integration work**

**Checkpoint**: All band parameters registered and accessible from host

---

## Phase 10: Performance & Validation

**Purpose**: Verify performance targets and run validation tools

### 10.1 Performance Tests

- [ ] T068 Implement performance benchmark for crossover network in plugins/disrumpo/tests/unit/crossover_network_test.cpp
  - Test: 512-sample block processes in < 50us at 44.1kHz (SC-003)
  - Test: CPU usage < 5% of per-config budget (per plan.md)

- [ ] T069 Run performance tests and verify targets are met

### 10.2 Pluginval Validation

- [ ] T070 Run pluginval strictness level 1 on Disrumpo plugin (SC-006)
  - Command: `tools/pluginval.exe --strictness-level 1 --validate "build/windows-x64-release/VST3/Release/Disrumpo.vst3"`

- [ ] T071 Fix any pluginval issues that arise

### 10.3 State Persistence Tests

- [ ] T072 Test state save/restore for all band parameters (SC-004)
  - Test: Band count persists
  - Test: Per-band gain/pan/solo/bypass/mute persist
  - Test: Crossover frequencies persist (7 frequencies for max config)

### 10.4 Commit (MANDATORY)

- [ ] T073 **Commit performance optimizations and pluginval fixes**

**Checkpoint**: Plugin meets all performance targets and passes validation

---

## Phase 11: Polish & Cross-Cutting Concerns

**Purpose**: Final improvements and cleanup

- [ ] T074 [P] Review all new code for compiler warnings (MSVC, Clang)
- [ ] T075 [P] Verify all constants match data-model.md definitions
- [ ] T076 [P] Add assertions for debug builds (isPrepared() checks, band index bounds)
- [ ] T077 Run quickstart.md validation checklist
- [ ] T078 **Commit polish work**

**Checkpoint**: Code is clean, documented, and production-ready

---

## Phase 12: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 12.1 Architecture Documentation Update

- [ ] T079 **Update specs/_architecture_/** with new components added by this spec:
  - Add CrossoverNetwork to plugin-specific components section
  - Add BandState structure documentation
  - Add BandProcessor to plugin-specific components section
  - Include: purpose, public API summary, file locations, usage examples
  - Verify no duplicate functionality was introduced
  - Document parameter ID encoding scheme (makeBandParamId, makeCrossoverParamId)

### 12.2 Final Commit

- [ ] T080 **Commit architecture documentation updates**
- [ ] T081 Verify all spec work is committed to feature branch 002-band-management

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 13.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T082 **Review ALL FR-xxx requirements** (FR-001 through FR-039) from spec.md against implementation
- [ ] T083 **Review ALL SC-xxx success criteria** (SC-001 through SC-007) and verify measurable targets are achieved
- [ ] T084 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 13.2 Fill Compliance Table in spec.md

- [ ] T085 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T086 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 13.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T087 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 14: Final Completion

**Purpose**: Final commit and completion claim

### 14.1 Final Commit

- [ ] T088 **Commit all spec work** to feature branch 002-band-management
- [ ] T089 **Verify all tests pass** (unit, integration, performance)
- [ ] T090 **Verify pluginval passes** strictness level 1

### 14.2 Completion Claim

- [ ] T091 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Phase 1 completion - BLOCKS all user stories
- **User Stories (Phases 3-7)**: All depend on Foundational phase completion
  - User stories can proceed in parallel (if staffed)
  - Or sequentially in priority order (US1 → US2 → US3 → US4 → US5)
- **Processor Integration (Phase 8)**: Depends on US1, US2, US3, US4 completion (core functionality)
- **Controller Integration (Phase 9)**: Depends on Phase 8 completion
- **Performance & Validation (Phase 10)**: Depends on Phases 8-9 completion
- **Polish (Phase 11)**: Depends on Phase 10 completion
- **Documentation (Phase 12)**: Depends on all implementation phases
- **Verification & Completion (Phases 13-14)**: Depends on all previous phases

### User Story Dependencies

- **User Story 1 (P1)**: CrossoverNetwork with dynamic band count - FOUNDATIONAL for all other stories
- **User Story 2 (P1)**: Phase coherence verification - Depends on US1, can run in parallel with US3/US4/US5 test writing
- **User Story 3 (P2)**: Band gain/pan - Independent of US2, depends on US1
- **User Story 4 (P2)**: Solo/bypass/mute - Independent of US2/US3, depends on US1
- **User Story 5 (P3)**: Manual crossover adjustment - Depends on US1, independent of US2/US3/US4

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Tests → Implementation → Verification → Cross-platform check → Commit
- Story complete before moving to next priority

### Parallel Opportunities

- **Phase 1**: All setup tasks can run in parallel
- **Phase 2**: All foundational tasks can run in parallel
- **User Story Tests**: All test-writing tasks marked [P] within a story can run in parallel
- **User Story Implementation**: After US1 complete, US3 and US4 can proceed in parallel (both depend only on US1)
- **Integration**: US2 verification can run in parallel with US3/US4 implementation
- **Polish**: Code review, warning fixes, assertions can run in parallel

---

## Parallel Example: User Story 1

```bash
# After foundational phase complete:
# Write tests first (sequential - same file)
Task T008: Write all CrossoverNetwork unit tests

# Implementation can proceed sequentially:
Task T009: Create header
Task T010: Implement prepare/reset
Task T011: Implement process
Task T012: Implement setBandCount
Task T013: Implement setCrossoverFrequency
Task T014: Implement frequency redistribution
Task T015: Verify tests pass
Task T016: Add sample rate tests
```

---

## Parallel Example: User Stories 3 & 4

```bash
# After US1 complete, these can proceed in parallel:

# Developer A: User Story 3 (Gain/Pan)
Task T026: Write BandProcessor tests
Task T027-T033: Implement BandProcessor gain/pan
Task T034-T035: Verify and commit

# Developer B: User Story 4 (Solo/Mute) - can start simultaneously
Task T036: Write solo/mute tests
Task T037-T040: Implement solo/mute logic
Task T041-T042: Verify and commit
```

---

## Implementation Strategy

### MVP First (User Stories 1 & 2)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (CrossoverNetwork)
4. Complete Phase 4: User Story 2 (Phase coherence verification)
5. **STOP and VALIDATE**: Test band splitting and summation independently
6. This gives a working multiband splitter with verified phase coherence

### Incremental Delivery

1. Setup + Foundational → Foundation ready
2. Add US1 + US2 → Test independently → Working phase-coherent crossover (MVP!)
3. Add US3 → Test independently → Band gain/pan controls added
4. Add US4 → Test independently → Solo/mute debugging tools added
5. Add US5 → Test independently → Manual crossover adjustment added
6. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Developer A: User Story 1 + 2 (core crossover)
3. Once US1 complete:
   - Developer B: User Story 3 (gain/pan)
   - Developer C: User Story 4 (solo/mute)
   - Developer D: User Story 5 (manual crossover)
4. Stories integrate independently into processor (Phase 8)

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to -fno-fast-math list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update specs/_architecture_/ before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

---

## Build Commands Reference

```bash
# Set CMake path (Windows)
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure Release build
"$CMAKE" --preset windows-x64-release

# Build Disrumpo plugin
"$CMAKE" --build build/windows-x64-release --config Release --target disrumpo

# Build and run Disrumpo tests
"$CMAKE" --build build/windows-x64-release --config Release --target disrumpo_tests
./build/windows-x64-release/plugins/disrumpo/tests/Release/disrumpo_tests.exe

# Run all tests via CTest
ctest --test-dir build/windows-x64-release -C Release --output-on-failure

# Run pluginval
tools/pluginval.exe --strictness-level 1 --validate "build/windows-x64-release/VST3/Release/Disrumpo.vst3"
```

---

## Summary Statistics

- **Total Tasks**: 91 tasks (T001-T091)
- **User Stories**: 5 user stories (P1: US1, US2; P2: US3, US4; P3: US5)
- **Test Tasks**: 9 test-writing tasks (all must FAIL before implementation)
- **Implementation Tasks**: 61 implementation tasks
- **Integration Tasks**: 21 integration, validation, and polish tasks
- **Parallel Opportunities**: 15 tasks marked [P] can run in parallel with others
- **Mandatory Checkpoints**: 14 checkpoints for validation
- **Cross-Platform Checks**: 6 explicit IEEE 754 compliance verification tasks

### Tasks per User Story

- **US1 (CrossoverNetwork)**: 11 tasks (T008-T018)
- **US2 (Phase Coherence)**: 7 tasks (T019-T025)
- **US3 (Gain/Pan)**: 10 tasks (T026-T035)
- **US4 (Solo/Mute)**: 7 tasks (T036-T042)
- **US5 (Manual Crossover)**: 6 tasks (T043-T048)
- **Integration**: 13 tasks (T049-T061)
- **Controller**: 6 tasks (T062-T067)
- **Performance**: 6 tasks (T068-T073)
- **Polish**: 5 tasks (T074-T078)
- **Documentation**: 3 tasks (T079-T081)
- **Verification**: 11 tasks (T082-T091)

### Suggested MVP Scope

**MVP = User Stories 1 & 2 only** (Tasks T001-T025 + minimal integration)
- This delivers a working phase-coherent multiband crossover
- Can be tested and validated independently
- Provides foundation for all future band processing features
- Total MVP tasks: ~35 tasks (including setup, foundational, and basic integration)
