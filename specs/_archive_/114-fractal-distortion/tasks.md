# Tasks: FractalDistortion

**Input**: Design documents from `/specs/114-fractal-distortion/`
**Prerequisites**: plan.md (complete), spec.md (complete)

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

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             processors/fractal_distortion_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

### PHASE-LEVEL GATE: IEEE 754 Compliance (MANDATORY)

**After completing EACH user story**, you MUST:

1. Check if any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection
2. If yes, add the file to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`:
   ```cmake
   if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
       set_source_files_properties(
           processors/fractal_distortion_test.cpp
           PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
       )
   endif()
   ```
3. This is NOT optional - skipping this will cause CI failures on macOS/Linux

**DO NOT proceed to next user story until this gate is complete.**

### Spectral Testing Guidance (for SC-002, SC-003, SC-006, SC-008, SC-009)

Several success criteria require FFT/spectral analysis. Use this approach:

1. **FFT Infrastructure**: Use existing test utilities or add kiss_fft for spectral analysis
2. **Test Methodology**:
   - Generate 1kHz sine wave at -6dB amplitude (avoid clipping)
   - Process through FractalDistortion
   - Compute FFT of output
   - Measure harmonic peaks at 2kHz, 3kHz, 4kHz, etc.
   - Verify expected harmonic ratios based on iteration count and mode

3. **Specific Tests**:
   - **SC-002 (4 harmonic layers)**: Verify 4 distinct amplitude peaks in Residual mode output
   - **SC-003 (phase coherence)**: Sum Crossover4Way bands, verify flat response ±0.5dB
   - **SC-006 (bounded output)**: Measure peak amplitude over 1 second, verify < 4x input peak
   - **SC-008 (frequency decay)**: Verify level 8 has reduced low-frequency content (highpass at 1600Hz)
   - **SC-009 (distinct Cascade)**: Verify different harmonic ratios for each waveshaper type

4. **Test Case Naming**: Use `TEST_CASE("FractalDistortion [description]", "[FR-xxx][SC-xxx]")` for traceability

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create project structure for FractalDistortion processor

- [x] T001 Create header structure at dsp/include/krate/dsp/processors/fractal_distortion.h (ALREADY EXISTS per plan.md structure)
- [x] T002 Create test structure at dsp/tests/processors/fractal_distortion_test.cpp (ALREADY EXISTS per plan.md structure)

**Note**: No actual setup needed - project structure already established. Proceeding to implementation.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [x] T003 Create FractalMode enum and FractalDistortion class skeleton in dsp/include/krate/dsp/processors/fractal_distortion.h
- [x] T004 Implement prepare(sampleRate, maxBlockSize) with sample rate validation (44100-192000Hz) and component initialization (FR-001, FR-003) in fractal_distortion.h
- [x] T004a Implement reset() to clear all internal state (filters, smoothers, feedbackBuffer_) without reallocation (FR-002) in fractal_distortion.h
- [x] T005 Implement parameter setters/getters with clamping (setIterations, setScaleFactor, setDrive, setMix) in fractal_distortion.h
- [x] T006 [P] Add smoothing infrastructure (driveSmoother_, mixSmoother_) configured in prepare() with 10ms smoothing time in fractal_distortion.h
- [x] T007 [P] Add DC blocking infrastructure (dcBlocker_) configured in prepare() with 10Hz cutoff in fractal_distortion.h
- [x] T008 [P] Initialize waveshaper array (waveshapers_[kMaxIterations]) with default Tanh type in fractal_distortion.h
- [x] T009 Implement basic process() stub that returns 0.0 in fractal_distortion.h

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Basic Fractal Saturation (Priority: P1) MVP

**Goal**: Implement Residual mode with recursive distortion and amplitude scaling, enabling complex evolving harmonic content

**Independent Test**: Process a sine wave through Residual mode and measure that each iteration adds progressively smaller harmonic content, verifiable via FFT analysis

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T010 [US1] Write lifecycle tests in dsp/tests/processors/fractal_distortion_test.cpp (prepare, reset, isPrepared)
- [x] T011 [US1] Write parameter clamping tests in fractal_distortion_test.cpp (iterations 1-8, scaleFactor 0.3-0.9, drive 1.0-20.0)
- [x] T012 [US1] Write Residual mode basic test in fractal_distortion_test.cpp (iterations=1 equivalent to single saturate operation - FR-027, AS1.2)
- [x] T013 [US1] Write Residual mode scaling test in fractal_distortion_test.cpp (scale=0.0 produces only first iteration - FR-028, AS1.3)
- [x] T014 [US1] Write Residual mode iteration test in fractal_distortion_test.cpp (iterations=4 produces 4 distinct harmonic layers - FR-029, AS1.1, SC-002)
- [x] T015 [US1] Write smoothing test in fractal_distortion_test.cpp (drive changes are click-free over 10ms - FR-018, SC-005)
- [x] T016 [US1] Write mix bypass test in fractal_distortion_test.cpp (mix=0.0 returns bit-exact dry signal - FR-021, SC-004)
- [x] T017 [US1] Write DC blocking test in fractal_distortion_test.cpp (asymmetric saturation DC is removed - FR-050)
- [x] T018 [US1] Write denormal flushing test in fractal_distortion_test.cpp (denormals flushed to prevent CPU spikes - FR-049)
- [x] T019 [US1] Write edge case tests in fractal_distortion_test.cpp (iterations<1 clamps to 1, drive=0 zero output, NaN/Inf handling returns 0.0 - Edge Cases, SC-007)

### 3.2 Implementation for User Story 1

- [x] T020 [US1] Implement processResidual() method in fractal_distortion.h (FR-027, FR-028, FR-029, FR-026)
- [x] T021 [US1] Implement process(float) single-sample method with mode dispatch in fractal_distortion.h (FR-046)
- [x] T022 [US1] Implement process(float*, size_t) block processing method in fractal_distortion.h (FR-047)
- [x] T023 [US1] Add mix parameter smoothing and dry/wet blend logic in fractal_distortion.h (FR-019 to FR-022)
- [x] T024 [US1] Add denormal flushing in process path in fractal_distortion.h (FR-049)
- [x] T025 [US1] Integrate DC blocker after processing in fractal_distortion.h (FR-050)
- [x] T026 [US1] Verify all tests pass - run dsp_tests with fractal_distortion_test filter

### 3.3 Cross-Platform Verification (MANDATORY)

- [x] T027 [US1] Verify IEEE 754 compliance - check if fractal_distortion_test.cpp uses std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt if needed

### 3.4 Commit (MANDATORY)

- [ ] T028 [US1] Commit completed User Story 1 work (use git workflow from CLAUDE.md)

**Checkpoint**: User Story 1 (Residual mode) should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Multiband Fractal Processing (Priority: P2)

**Goal**: Implement Multiband mode to split signal into octave bands and apply frequency-aware fractal processing (more iterations to higher bands)

**Independent Test**: Process full-spectrum signal and measure that high-frequency bands receive more iterations (more harmonic complexity) than low-frequency bands

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T029 [US2] Write Multiband mode parameter tests in fractal_distortion_test.cpp (setCrossoverFrequency, setBandIterationScale - FR-031, FR-032)
- [x] T030 [US2] Write Multiband band iteration distribution test in fractal_distortion_test.cpp (baseIterations=6, scale=0.5 gives [1,2,3,6] - FR-033, AS2.1)
- [x] T031 [US2] Write Multiband iteration scale test in fractal_distortion_test.cpp (bandIterationScale=1.0 gives equal iterations because 1.0^N = 1.0 for all N - AS2.2)
- [x] T032 [US2] Write Multiband phase coherence test in fractal_distortion_test.cpp (Linkwitz-Riley crossovers maintain flat sum - FR-030, AS2.3, SC-003)
- [x] T033 [US2] Write Multiband crossover configuration test in fractal_distortion_test.cpp (verify Crossover4Way frequencies set correctly)

### 4.2 Implementation for User Story 2

- [x] T034 [US2] Implement calculateBandIterations() helper method in fractal_distortion.h (FR-033 formula)
- [x] T035 [US2] Implement updateCrossoverFrequencies() helper method in fractal_distortion.h (configure Crossover4Way based on crossoverFrequency parameter)
- [x] T036 [US2] Implement processMultiband() method in fractal_distortion.h (FR-030 to FR-033)
- [x] T037 [US2] Add Multiband mode to setMode() and process() dispatch in fractal_distortion.h (FR-006)
- [x] T038 [US2] Verify all tests pass - run dsp_tests with fractal_distortion_test filter

### 4.3 Cross-Platform Verification (MANDATORY)

- [x] T039 [US2] Verify IEEE 754 compliance - check if new tests use std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list if needed

### 4.4 Commit (MANDATORY)

- [ ] T040 [US2] Commit completed User Story 2 work (use git workflow from CLAUDE.md)

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Cascade Mode with Per-Level Waveshapers (Priority: P2)

**Goal**: Implement Cascade mode to assign different waveshaper types to each iteration level, enabling specific harmonic evolution (warm to harsh progression)

**Independent Test**: Set distinct waveshaper types per level and verify each level applies its designated algorithm via spectral analysis

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T041 [US3] Write Cascade mode waveshaper assignment test in fractal_distortion_test.cpp (setLevelWaveshaper applies correct type - FR-039, AS3.1)
- [x] T042 [US3] Write Cascade mode independence test in fractal_distortion_test.cpp (changing one level affects only that level - AS3.2)
- [x] T043 [US3] Write Cascade mode invalid level test in fractal_distortion_test.cpp (level beyond iterations is safely ignored - FR-041, AS3.3)
- [x] T044 [US3] Write Cascade mode distinct harmonics test in fractal_distortion_test.cpp (8 different waveshapers produce distinct signatures - SC-009)

### 5.2 Implementation for User Story 3

- [x] T045 [US3] Implement setLevelWaveshaper() method with bounds checking in fractal_distortion.h (FR-039, FR-041)
- [x] T046 [US3] Implement getLevelWaveshaper() method in fractal_distortion.h
- [x] T047 [US3] Implement processCascade() method in fractal_distortion.h (FR-040, uses waveshapers_[N] per level)
- [x] T048 [US3] Add Cascade mode to setMode() and process() dispatch in fractal_distortion.h (FR-008)
- [x] T049 [US3] Verify all tests pass - run dsp_tests with fractal_distortion_test filter

### 5.3 Cross-Platform Verification (MANDATORY)

- [x] T050 [US3] Verify IEEE 754 compliance - check if new tests use std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list if needed

### 5.4 Commit (MANDATORY)

- [ ] T051 [US3] Commit completed User Story 3 work (use git workflow from CLAUDE.md)

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed

---

## Phase 6: User Story 4 - Harmonic Mode with Odd/Even Separation (Priority: P3)

**Goal**: Implement Harmonic mode to separate odd and even harmonics via Chebyshev polynomial extraction and apply different saturation curves to each, enabling complex intermodulation effects

**Independent Test**: Process audio and verify that odd and even harmonics receive different saturation treatments via spectral analysis

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T052 [US4] Write Harmonic mode curve assignment test in fractal_distortion_test.cpp (setOddHarmonicCurve, setEvenHarmonicCurve - FR-036)
- [x] T053 [US4] Write Harmonic mode default curves test in fractal_distortion_test.cpp (odd=Tanh, even=Tube - FR-037)
- [x] T054 [US4] Write Harmonic mode separation test in fractal_distortion_test.cpp (sine wave produces odd vs even treatments - FR-034, FR-035, AS4.1)
- [x] T055 [US4] Write Harmonic mode equivalence test in fractal_distortion_test.cpp (identical curves equals Residual mode - AS4.2)

### 6.2 Implementation for User Story 4

- [x] T056 [US4] Configure ChebyshevShapers for odd/even harmonics in prepare() in fractal_distortion.h (FR-034, FR-035)
- [x] T057 [US4] Implement setOddHarmonicCurve() and setEvenHarmonicCurve() in fractal_distortion.h (FR-036)
- [x] T058 [US4] Implement processHarmonic() method in fractal_distortion.h (FR-038, uses oddHarmonicShaper_ and evenHarmonicShaper_)
- [x] T059 [US4] Add Harmonic mode to setMode() and process() dispatch in fractal_distortion.h (FR-007)
- [x] T060 [US4] Verify all tests pass - run dsp_tests with fractal_distortion_test filter

### 6.3 Cross-Platform Verification (MANDATORY)

- [x] T061 [US4] Verify IEEE 754 compliance - check if new tests use std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list if needed

### 6.4 Commit (MANDATORY)

- [ ] T062 [US4] Commit completed User Story 4 work (use git workflow from CLAUDE.md)

**Checkpoint**: User Stories 1-4 should all work independently and be committed

---

## Phase 7: User Story 5 - Feedback Mode for Chaotic Textures (Priority: P3)

**Goal**: Implement Feedback mode to cross-feed between iteration levels with delay, creating self-oscillating and chaotic but bounded distortion effects

**Independent Test**: Enable feedback and verify that output exhibits cross-level energy transfer creating evolving textures while remaining bounded

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T063 [US5] Write Feedback mode parameter tests in fractal_distortion_test.cpp (setFeedbackAmount 0.0-0.5 clamping - FR-042)
- [x] T064 [US5] Write Feedback mode cross-feeding test in fractal_distortion_test.cpp (feedbackAmount=0.3 creates energy transfer - AS5.1)
- [x] T065 [US5] Write Feedback mode bypass test in fractal_distortion_test.cpp (feedbackAmount=0.0 equals Residual mode - AS5.2)
- [x] T066 [US5] Write Feedback mode bounded test in fractal_distortion_test.cpp (feedbackAmount=0.5 remains bounded - FR-045, AS5.3, SC-006)

### 7.2 Implementation for User Story 5

- [x] T067 [US5] Initialize feedbackBuffer_ array (std::array<float, kMaxIterations>) to store previous sample's level outputs for cross-feeding (FR-043) in fractal_distortion.h
- [x] T068 [US5] Implement setFeedbackAmount() with clamping [0.0, 0.5] in fractal_distortion.h (FR-042)
- [x] T069 [US5] Implement processFeedback() method in fractal_distortion.h (FR-044, FR-045, cross-feeds level[N-1] into level[N])
- [x] T070 [US5] Add Feedback mode to setMode() and process() dispatch in fractal_distortion.h (FR-009)
- [x] T071 [US5] Verify all tests pass - run dsp_tests with fractal_distortion_test filter

### 7.3 Cross-Platform Verification (MANDATORY)

- [x] T072 [US5] Verify IEEE 754 compliance - check if new tests use std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list if needed

### 7.4 Commit (MANDATORY)

- [ ] T073 [US5] Commit completed User Story 5 work (use git workflow from CLAUDE.md)

**Checkpoint**: User Stories 1-5 should all work independently and be committed

---

## Phase 8: User Story 6 - Frequency Decay for Brightness Control (Priority: P3)

**Goal**: Implement frequency decay modifier to apply progressive highpass filtering at deeper iteration levels, emphasizing high frequencies without affecting the fundamental

**Independent Test**: Enable frequencyDecay and verify that deeper iterations are progressively highpass-filtered via spectral analysis

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T074 [US6] Write frequency decay parameter test in fractal_distortion_test.cpp (setFrequencyDecay 0.0-1.0 - FR-023, FR-024)
- [x] T075 [US6] Write frequency decay progression test in fractal_distortion_test.cpp (level 4 higher cutoff than level 2 - FR-025, AS6.1)
- [x] T076 [US6] Write frequency decay bypass test in fractal_distortion_test.cpp (frequencyDecay=0.0 no filtering - AS6.2)
- [x] T077 [US6] Write frequency decay extreme test in fractal_distortion_test.cpp (frequencyDecay=1.0 level 8 at 1600Hz - SC-008)

### 8.2 Implementation for User Story 6

- [x] T078 [US6] Initialize decayFilters_ array (Biquad highpass per level) in fractal_distortion.h
- [x] T079 [US6] Implement updateDecayFilters() helper method in fractal_distortion.h (configure Biquads based on frequencyDecay and level)
- [x] T080 [US6] Implement setFrequencyDecay() with filter reconfiguration in fractal_distortion.h (FR-023, FR-024, FR-025)
- [x] T081 [US6] Integrate frequency decay filtering in all mode process methods in fractal_distortion.h (apply decayFilters_[N] at level N)
- [x] T082 [US6] Verify all tests pass - run dsp_tests with fractal_distortion_test filter

### 8.3 Cross-Platform Verification (MANDATORY)

- [x] T083 [US6] Verify IEEE 754 compliance - check if new tests use std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list if needed

### 8.4 Commit (MANDATORY)

- [ ] T084 [US6] Commit completed User Story 6 work (use git workflow from CLAUDE.md)

**Checkpoint**: All user stories (1-6) should now be independently functional and committed

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T085 [P] Performance optimization - verify SC-001 CPU budget (< 0.5% for 8 iterations at 44.1kHz)
- [ ] T086 [P] Code cleanup and refactoring - ensure consistent style per CLAUDE.md
- [ ] T087 [P] Add additional edge case tests if gaps found during implementation in fractal_distortion_test.cpp
- [ ] T088 [P] Verify all compiler warnings resolved (MSVC C4244, C4267, C4100)
- [ ] T089 Run quickstart.md validation - manually test all code examples

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 10.1 Architecture Documentation Update

- [ ] T090 Update specs/_architecture_/layer-2-processors.md with FractalDistortion component entry (purpose, API summary, file location, when to use this)

### 10.2 Final Commit

- [ ] T091 Commit architecture documentation updates
- [ ] T092 Verify all spec work is committed to feature branch 114-fractal-distortion

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T093 Review ALL FR-xxx requirements (FR-001 to FR-050) from spec.md against implementation
- [ ] T094 Review ALL SC-xxx success criteria (SC-001 to SC-009) and verify measurable targets are achieved
- [ ] T095 Search for cheating patterns in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in fractal_distortion.h
  - [ ] No test thresholds relaxed from spec requirements in fractal_distortion_test.cpp
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [ ] T096 Update specs/114-fractal-distortion/spec.md "Implementation Verification" section with compliance status for each requirement (MET/NOT MET/PARTIAL/DEFERRED)
- [ ] T097 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T098 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Commit

- [ ] T099 Commit all remaining spec work to feature branch 114-fractal-distortion
- [ ] T100 Verify all tests pass - run full dsp_tests suite

### 12.2 Completion Claim

- [ ] T101 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: COMPLETE - No tasks needed (structure exists)
- **Foundational (Phase 2)**: T003-T009 - BLOCKS all user stories
- **User Stories (Phase 3-8)**: All depend on Foundational phase completion
  - US1 (Residual): T010-T028 - Can start after Foundational
  - US2 (Multiband): T029-T040 - Can start after Foundational (independent of US1)
  - US3 (Cascade): T041-T051 - Can start after Foundational (independent of US1/US2)
  - US4 (Harmonic): T052-T062 - Can start after Foundational (independent of US1/US2/US3)
  - US5 (Feedback): T063-T073 - Can start after Foundational (independent of US1/US2/US3/US4)
  - US6 (Frequency Decay): T074-T084 - Can start after Foundational (integrates with all modes)
- **Polish (Phase 9)**: T085-T089 - Depends on all desired user stories being complete
- **Documentation (Phase 10)**: T090-T092 - Depends on Polish completion
- **Verification (Phase 11)**: T093-T098 - Depends on Documentation completion
- **Completion (Phase 12)**: T099-T101 - Depends on Verification completion

### User Story Dependencies

- **US1 (Residual)**: No dependencies on other stories - foundation for all modes
- **US2 (Multiband)**: Independent - uses Residual algorithm internally but can be tested standalone
- **US3 (Cascade)**: Independent - variation of Residual with different waveshapers per level
- **US4 (Harmonic)**: Independent - uses Chebyshev extraction instead of residual approach
- **US5 (Feedback)**: Independent - extends Residual with cross-level feedback
- **US6 (Frequency Decay)**: Cross-cutting modifier - applies to all modes, best implemented after core modes

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Test tasks (e.g., T010-T019 for US1) before implementation tasks (e.g., T020-T026 for US1)
- Implementation tasks can have internal dependencies (e.g., processResidual() before process() dispatch)
- **Verify tests pass**: After implementation (e.g., T026 for US1)
- **Cross-platform check**: Verify IEEE 754 functions have -fno-fast-math (e.g., T027 for US1)
- **Commit**: LAST task - commit completed work (e.g., T028 for US1)

### Parallel Opportunities

- **Foundational Phase**: T006, T007, T008 can run in parallel (different components)
- **User Stories**: After Foundational, US1-US5 can all start in parallel if team capacity allows
- **Within User Story Tests**: Multiple test tasks can run in parallel (e.g., T010-T019 for US1 are all different test cases)
- **Polish Phase**: T085-T088 can run in parallel (different concerns)

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
Task T010: "Write lifecycle tests in fractal_distortion_test.cpp"
Task T011: "Write parameter clamping tests in fractal_distortion_test.cpp"
Task T012: "Write Residual mode basic test in fractal_distortion_test.cpp"
Task T013: "Write Residual mode scaling test in fractal_distortion_test.cpp"
Task T014: "Write Residual mode iteration test in fractal_distortion_test.cpp"
Task T015: "Write smoothing test in fractal_distortion_test.cpp"
Task T016: "Write mix bypass test in fractal_distortion_test.cpp"
Task T017: "Write DC blocking test in fractal_distortion_test.cpp"
Task T018: "Write denormal flushing test in fractal_distortion_test.cpp"
Task T019: "Write edge case tests in fractal_distortion_test.cpp"

# Then implement all together:
Task T020: "Implement processResidual() in fractal_distortion.h"
Task T021: "Implement process(float) in fractal_distortion.h"
Task T022: "Implement process(float*, size_t) in fractal_distortion.h"
# ... etc
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 2: Foundational (T003-T009) - CRITICAL - blocks all stories
2. Complete Phase 3: User Story 1 (T010-T028) - Residual mode
3. **STOP and VALIDATE**: Test User Story 1 independently
4. Demo Residual mode functionality

### Incremental Delivery

1. Complete Foundational (T003-T009) - Foundation ready
2. Add User Story 1 (T010-T028) - Test independently - Basic fractal saturation works (MVP!)
3. Add User Story 2 (T029-T040) - Test independently - Multiband processing works
4. Add User Story 3 (T041-T051) - Test independently - Cascade mode works
5. Add User Story 4 (T052-T062) - Test independently - Harmonic mode works
6. Add User Story 5 (T063-T073) - Test independently - Feedback mode works
7. Add User Story 6 (T074-T084) - Test independently - Frequency decay works across all modes
8. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Foundational together (T003-T009)
2. Once Foundational is done:
   - Developer A: User Story 1 (T010-T028) - Residual mode
   - Developer B: User Story 2 (T029-T040) - Multiband mode
   - Developer C: User Story 3 (T041-T051) - Cascade mode
   - Developer D: User Story 4 (T052-T062) - Harmonic mode
   - Developer E: User Story 5 (T063-T073) - Feedback mode
3. User Story 6 (T074-T084) after any core mode is complete (integrates with all)
4. Stories complete and integrate independently

---

## Summary

**Total Tasks**: 101 tasks
- Phase 1 (Setup): 2 tasks (COMPLETE - no work needed)
- Phase 2 (Foundational): 7 tasks (T003-T009)
- Phase 3 (US1 - Residual): 19 tasks (T010-T028)
- Phase 4 (US2 - Multiband): 12 tasks (T029-T040)
- Phase 5 (US3 - Cascade): 11 tasks (T041-T051)
- Phase 6 (US4 - Harmonic): 11 tasks (T052-T062)
- Phase 7 (US5 - Feedback): 11 tasks (T063-T073)
- Phase 8 (US6 - Frequency Decay): 11 tasks (T074-T084)
- Phase 9 (Polish): 5 tasks (T085-T089)
- Phase 10 (Documentation): 3 tasks (T090-T092)
- Phase 11 (Verification): 6 tasks (T093-T098)
- Phase 12 (Completion): 3 tasks (T099-T101)

**Task Distribution by User Story**:
- US1 (P1): 19 tasks - Residual mode (MVP)
- US2 (P2): 12 tasks - Multiband mode
- US3 (P2): 11 tasks - Cascade mode
- US4 (P3): 11 tasks - Harmonic mode
- US5 (P3): 11 tasks - Feedback mode
- US6 (P3): 11 tasks - Frequency decay (cross-cutting)

**Parallel Opportunities**:
- Foundational: 3 components in parallel (T006-T008)
- User Stories: 5 stories can proceed in parallel after Foundational
- Within stories: Multiple test tasks per story can run in parallel

**Independent Test Criteria**:
- US1: Sine wave produces 4 distinct harmonic layers (FFT analysis)
- US2: High bands receive more iterations than low bands (spectral analysis)
- US3: Each level applies designated waveshaper (spectral signatures)
- US4: Odd/even harmonics receive different treatments (spectral analysis)
- US5: Cross-level energy transfer while remaining bounded (amplitude analysis)
- US6: Deeper levels progressively highpass-filtered (spectral analysis)

**Suggested MVP Scope**: User Story 1 (Residual mode) - T003-T028 (26 tasks total including foundational)

**Format Validation**: All tasks follow checklist format with Task ID, [P] marker (where applicable), [Story] label (for US phases), and exact file paths.

---

## Notes

- [P] tasks = different files or components, no dependencies, can run in parallel
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to -fno-fast-math list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update specs/_architecture_/ before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
