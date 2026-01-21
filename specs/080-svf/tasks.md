# Tasks: State Variable Filter (SVF)

**Input**: Design documents from `/specs/080-svf/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/svf.h, quickstart.md

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

Skills auto-load when needed (testing-guide, dsp-architecture) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             primitives/svf_test.cpp  # ADD THIS FILE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

## Format: `- [ ] [TaskID] [P?] [Story?] Description`

- **Checkbox**: ALWAYS start with `- [ ]` (markdown checkbox)
- **Task ID**: Sequential number (T001, T002, T003...) in execution order
- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Ensure CMake targets are ready for new test files

- [X] T001 Verify CMake test infrastructure is ready for new test files in dsp/tests/primitives/
- [X] T002 Verify build can find headers at dsp/include/krate/dsp/primitives/

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core dependencies that MUST exist before SVF can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T003 Verify math_constants.h (kPi, kTwoPi) is available at dsp/include/krate/dsp/core/math_constants.h
- [X] T004 Verify db_utils.h (detail::flushDenormal, detail::isNaN, detail::isInf, detail::constexprPow10) is available at dsp/include/krate/dsp/core/db_utils.h

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - DSP Developer Uses SVF for Synth-Style Filtering (Priority: P1) MVP

**Goal**: Provide SVF class with lowpass/highpass/bandpass/notch modes and audio-rate modulation stability for synth-style filtering with LFO/envelope sweeps.

**Why US1 First**: Modulation stability is the primary advantage of TPT SVF. This story establishes the core filter topology and processing engine.

**Independent Test**: Can be fully tested by configuring the filter, modulating cutoff at audio rate, and verifying no clicks or discontinuities occur in the output.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T005 [US1] Write failing test file dsp/tests/primitives/svf_test.cpp with Catch2 test cases for:
  - **SC-011**: Audio-rate cutoff modulation (sweep 100Hz to 10kHz in 100 samples) produces no clicks (max sample-to-sample change < 0.5 for unit amplitude input)
  - **Modulation stability**: High Q (Q=10) with 20Hz sinusoidal cutoff modulation over 2 octaves produces no runaway oscillation or NaN
  - **SC-001**: Lowpass at 1000Hz attenuates 10kHz sine by at least 22dB (2-pole, 12dB/oct)
  - **SC-002**: Lowpass at 1000Hz passes 100Hz sine with less than 0.5dB attenuation
  - **Acceptance Scenario 1**: Lowpass 1000Hz with Q=0.7071 passes 100Hz sine within 0.5dB
  - **Acceptance Scenario 2**: Lowpass 1000Hz attenuates 10kHz sine by at least 22dB
  - **FR-021**: process() before prepare() returns input unchanged
  - **FR-022**: NaN input returns 0.0f and resets state (ic1eq, ic2eq)
  - **FR-022**: Infinity input returns 0.0f and resets state
  - reset() clears state variables (ic1eq, ic2eq)
  - Edge cases: zero/negative sample rate, zero/negative cutoff, cutoff exceeds Nyquist, Q=0, Q>30
- [X] T006 [US1] Run failing tests to verify they FAIL (no implementation exists yet): build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[svf][US1]"

### 3.2 Implementation for User Story 1

- [X] T007 [US1] Create dsp/include/krate/dsp/primitives/svf.h with:
  - SVFMode enum class (Lowpass, Highpass, Bandpass, Notch) for US1 modes (FR-001)
  - SVFOutputs struct with low, high, band, notch members (FR-002)
  - SVF class declaration with member variables from data-model.md (FR-003)
  - prepare(double sampleRate) implementation (FR-004)
  - setMode(SVFMode mode) implementation (FR-005)
  - setCutoff(float hz) with clamping and immediate coefficient recalculation (FR-006)
  - setResonance(float q) with clamping and immediate coefficient recalculation (FR-007)
  - reset() noexcept implementation (FR-009)
  - Getter methods: getMode(), getCutoff(), getResonance(), isPrepared()
  - Internal: updateCoefficients() calculating g, k, a1, a2, a3 per FR-013, FR-014
  - Internal: updateMixCoefficients() for Lowpass, Highpass, Bandpass, Notch modes (FR-017 partial)
  - Internal: clampCutoff(), clampQ() parameter validation
  - Doxygen documentation for all public methods (FR-026)
  - Header-only implementation (FR-024)
  - Namespace Krate::DSP (FR-025)
  - Only depends on Layer 0 (math_constants.h, db_utils.h) (FR-023)
- [X] T008 [US1] Implement process(float input) noexcept in svf.h:
  - FR-010: Returns output for currently selected mode
  - FR-016: Per-sample computation (v3, v1, v2 intermediates, trapezoidal state update)
  - FR-017: Mode mixing with m0, m1, m2 coefficients for Lowpass/Highpass/Bandpass/Notch
  - FR-018: Declared noexcept
  - FR-019: Flush denormals on ic1eq and ic2eq after every process() call using detail::flushDenormal()
  - FR-020: No allocations, exceptions, or I/O
  - FR-021: Return input unchanged if not prepared
  - FR-022: Return 0.0f and reset state on NaN or Infinity input using detail::isNaN() and detail::isInf()
- [X] T009 [US1] Build DSP library and tests: "/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests
- [X] T010 [US1] Verify all User Story 1 tests pass: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[svf][US1]"
- [X] T011 [US1] Verify no compiler warnings in svf.h

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T012 [US1] Verify IEEE 754 compliance: svf_test.cpp uses detail::isNaN() and detail::isInf() - add primitives/svf_test.cpp to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 3.4 Commit (MANDATORY)

- [ ] T013 [US1] Commit completed User Story 1 work: SVF with Lowpass/Highpass/Bandpass/Notch modes and modulation stability

**Checkpoint**: User Story 1 should be fully functional with basic 4 modes, tested, and committed

---

## Phase 4: User Story 2 - DSP Developer Uses Multi-Output Processing (Priority: P1)

**Goal**: Provide processMulti() method that returns simultaneous lowpass, highpass, bandpass, and notch outputs in a single computation for efficient multi-band processing.

**Why US2 Second**: Multi-output is a key differentiator of SVF. Builds on US1's core topology by exposing all four outputs simultaneously.

**Independent Test**: Can be tested by processing a test signal and verifying all four outputs have correct frequency responses simultaneously.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T014 [US2] Add test cases to dsp/tests/primitives/svf_test.cpp for processMulti():
  - **Acceptance Scenario 1**: processMulti() at 1000Hz cutoff with 100Hz sine produces low~unity gain, high~-24dB, band attenuated, notch~unity
  - **Acceptance Scenario 2**: processMulti() at 1000Hz cutoff with 1000Hz sine produces band~unity gain, notch at minimum
  - All four outputs (low, high, band, notch) are computed in single call
  - **FR-021**: processMulti() before prepare() returns all zeros (SVFOutputs{0, 0, 0, 0})
  - **FR-022**: processMulti() with NaN input returns all zeros and resets state
  - **FR-022**: processMulti() with Infinity input returns all zeros and resets state
  - Multi-output stability: Process 1000 samples at various frequencies, verify all outputs remain valid (no NaN/Inf)
- [X] T015 [US2] Run failing tests to verify they FAIL: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[svf][US2]"

### 4.2 Implementation for User Story 2

- [X] T016 [US2] Implement processMulti(float input) noexcept in dsp/include/krate/dsp/primitives/svf.h:
  - FR-012: Returns SVFOutputs struct with all four outputs (low, high, band, notch)
  - FR-016: Same per-sample computation as process() (v3, v1, v2, state update)
  - FR-018: Declared noexcept
  - FR-019: Flush denormals on ic1eq and ic2eq after processing
  - FR-020: No allocations, exceptions, or I/O
  - FR-021: Return zeros (SVFOutputs{0, 0, 0, 0}) if not prepared
  - FR-022: Return zeros and reset state on NaN or Infinity input
  - Compute notch as low + high (equivalent formulation)
- [X] T017 [US2] Build DSP library and tests: "/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests
- [X] T018 [US2] Verify all User Story 2 tests pass: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[svf][US2]"
- [X] T019 [US2] Verify no compiler warnings in svf.h

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T020 [US2] Verify IEEE 754 compliance: svf_test.cpp already added to -fno-fast-math list in Phase 3 (T012)

### 4.4 Commit (MANDATORY)

- [ ] T021 [US2] Commit completed User Story 2 work: processMulti() with simultaneous outputs

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - DSP Developer Configures Various Filter Modes (Priority: P2)

**Goal**: Extend SVF with Allpass, Peak, LowShelf, and HighShelf modes for parametric EQ and phase manipulation applications.

**Why US3 Third**: Mode selection is the standard filter API pattern. Peak/shelf modes require gain parameter, building on core topology from US1/US2.

**Independent Test**: Can be tested by configuring each mode and verifying the expected frequency response.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T022 [US3] Add test cases to dsp/tests/primitives/svf_test.cpp for additional modes:
  - **SC-003**: Highpass at 100Hz cutoff attenuates 10Hz sine by at least 22dB (matches SC-003 exactly)
  - **SC-004**: Highpass at 100Hz passes 1000Hz sine with less than 0.5dB attenuation
  - **SC-005**: Bandpass at 1000Hz with Q=5 has peak gain within 1dB of unity at cutoff
  - **SC-006**: Notch at 1000Hz attenuates 1000Hz sine by at least 20dB
  - **SC-007**: Allpass at 1000Hz has flat magnitude response (within 0.1dB) across audio spectrum
  - **SC-008**: Peak mode at 1000Hz with +6dB gain boosts 1000Hz by 6dB (+/- 1dB)
  - **SC-009**: Low shelf at 1000Hz with +6dB gain boosts 100Hz by 6dB (+/- 1dB)
  - **SC-010**: High shelf at 1000Hz with +6dB gain boosts 10kHz by 6dB (+/- 1dB)
  - **Acceptance Scenario 1**: Highpass 1000Hz attenuates 100Hz by at least 18dB
  - **Acceptance Scenario 2**: Bandpass 1000Hz with Q=5 at 1000Hz is within 1dB of unity
  - **Acceptance Scenario 3**: Notch 1000Hz attenuates 1000Hz by at least 20dB
  - **Acceptance Scenario 4**: Allpass has flat magnitude within 0.1dB
  - **Acceptance Scenario 5**: Peak +6dB boosts 1000Hz by 6dB (+/- 1dB)
  - **Acceptance Scenario 6**: Low shelf +6dB boosts 100Hz by 6dB (+/- 1dB)
  - **Acceptance Scenario 7**: High shelf +6dB boosts 10kHz by 6dB (+/- 1dB)
  - setGain() updates A coefficient immediately (FR-008)
  - Gain clamped to [-24dB, +24dB]
  - getGain() returns correct value
- [X] T023 [US3] Run failing tests to verify they FAIL: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[svf][US3]"

### 5.2 Implementation for User Story 3

- [X] T024 [US3] Add Allpass, Peak, LowShelf, HighShelf to SVFMode enum in dsp/include/krate/dsp/primitives/svf.h (FR-001 complete)
- [X] T025 [US3] Implement setGain(float dB) noexcept in svf.h:
  - FR-008: Set gain for peak and shelf modes (ignored for other modes)
  - Clamp gain to [-24dB, +24dB]
  - Recalculate A coefficient immediately: A = detail::constexprPow10(gainDb / 40.0f)
  - Call updateMixCoefficients() to update m0, m1, m2 for shelf modes
- [X] T026 [US3] Update updateMixCoefficients() in svf.h:
  - FR-017: Complete mode mixing for all 8 modes (Allpass, Peak, LowShelf, HighShelf)
  - Allpass: m0=1, m1=-k, m2=1 (note: m1 depends on k)
  - Peak: m0=1, m1=0, m2=-1
  - LowShelf: m0=1, m1=k*(A-1), m2=A^2
  - HighShelf: m0=A^2, m1=k*(A-1), m2=1
- [X] T027 [US3] Implement getGain() const noexcept in svf.h
- [X] T028 [US3] Implement clampGainDb(float dB) const noexcept in svf.h
- [X] T029 [US3] Build DSP library and tests: "/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests
- [X] T030 [US3] Verify all User Story 3 tests pass: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[svf][US3]"
- [X] T031 [US3] Verify no compiler warnings in svf.h

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T032 [US3] Verify IEEE 754 compliance: svf_test.cpp already added to -fno-fast-math list in Phase 3 (T012)

### 5.4 Commit (MANDATORY)

- [ ] T033 [US3] Commit completed User Story 3 work: All 8 filter modes (Allpass, Peak, LowShelf, HighShelf added)

**Checkpoint**: All filter modes (8 total) should now work, tested, and committed

---

## Phase 6: User Story 4 - DSP Developer Uses Block Processing (Priority: P2)

**Goal**: Provide processBlock() method for efficient in-place processing of audio buffers with better cache performance than sample-by-sample.

**Why US4 Fourth**: Block processing is important for performance but less critical than the filter functionality itself. Final API completion task.

**Independent Test**: Can be tested by comparing block output with equivalent sample-by-sample processing.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T034 [US4] Add test cases to dsp/tests/primitives/svf_test.cpp for processBlock():
  - **SC-012**: processBlock() produces bit-identical output to equivalent process() calls
  - **Acceptance Scenario 1**: processBlock() on 1024-sample buffer is bit-identical to process() loop
  - **Acceptance Scenario 2**: processBlock() on 1024-sample buffer performs no memory allocation (noexcept verified)
  - Block processing with modulation: Update cutoff mid-block and verify smooth transition
  - Edge case: processBlock(buffer, 0) with zero samples must not crash or modify any state
  - Edge case: processBlock() before prepare() returns input unchanged (bypass behavior)
- [X] T035 [US4] Run failing tests to verify they FAIL: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[svf][US4]"

### 6.2 Implementation for User Story 4

- [X] T036 [US4] Implement processBlock(float* buffer, size_t numSamples) noexcept in dsp/include/krate/dsp/primitives/svf.h:
  - FR-011: In-place block processing
  - Simple loop calling process() on each sample (produces bit-identical output per SC-012)
  - FR-018: Declared noexcept
  - FR-020: No allocations
- [X] T037 [US4] Build DSP library and tests: "/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests
- [X] T038 [US4] Verify all User Story 4 tests pass: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[svf][US4]"
- [X] T039 [US4] Verify no compiler warnings in svf.h

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T040 [US4] Verify IEEE 754 compliance: svf_test.cpp already added to -fno-fast-math list in Phase 3 (T012)

### 6.4 Commit (MANDATORY)

- [ ] T041 [US4] Commit completed User Story 4 work: processBlock() in-place processing

**Checkpoint**: All user stories (US1, US2, US3, US4) should now be independently functional and committed

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Comprehensive testing and verification across all user stories

- [X] T042 [P] Add comprehensive stability tests to dsp/tests/primitives/svf_test.cpp:
  - **SC-013**: Process 1 million samples in [-1, 1] range without NaN or Infinity outputs from valid inputs
  - Test all 8 modes for stability over 1M samples
  - Test extreme Q values (Q=0.1, Q=30) for stability
  - Test cutoff at extremes (1Hz, sampleRate * 0.495) for stability
- [X] T043 [P] Run all DSP tests to verify entire suite passes: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe
- [X] T044 [P] Verify code quality: SVF follows naming conventions (trailing underscore for members, PascalCase for classes/enums, camelCase for methods) per FR-027
- [X] T045 [P] Verify Layer 1 file depends only on Layer 0 (FR-023): svf.h includes only math_constants.h and db_utils.h
- [X] T046 [P] Verify header-only implementation (FR-024): svf.h has no corresponding .cpp file
- [X] T047 [P] Verify namespace correctness (FR-025): All components in Krate::DSP namespace
- [X] T047a [P] Verify Doxygen documentation completeness (FR-026): All public methods, classes, and enums have Doxygen comments
- [X] T047b [P] Add static noexcept verification tests: Use STATIC_REQUIRE(noexcept(filter.process(0.0f))) for process(), processBlock(), processMulti() to verify noexcept contracts at compile time
- [ ] T048 Run quickstart.md validation: Follow implementation steps in specs/080-svf/quickstart.md to verify guide is accurate

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 8.1 Architecture Documentation Update

- [ ] T049 Update specs/_architecture_/layer-1-primitives.md with new Layer 1 component:
  - svf.h: SVF class, SVFMode enum, SVFOutputs struct
  - Include purpose: TPT State Variable Filter with modulation stability and multi-output
  - Public API summary: prepare(), setMode(), setCutoff(), setResonance(), setGain(), reset(), process(), processBlock(), processMulti()
  - File location: dsp/include/krate/dsp/primitives/svf.h
  - When to use: Synth-style filters, auto-wah, parametric EQ, any filter needing audio-rate modulation stability
  - Advantages over Biquad: Modulation-stable, multi-output, orthogonal parameters
  - Usage example with typical configuration
- [ ] T050 Verify no duplicate functionality was introduced (ODR check): grep -r "SVF\|StateVariable" dsp/include/krate/dsp/ confirms no duplicates

### 8.2 Final Commit

- [ ] T051 Commit architecture documentation updates
- [ ] T052 Verify all spec work is committed to feature branch 080-svf

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 9.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T053 Review ALL FR-xxx requirements (FR-001 through FR-027) from spec.md against implementation:
  - FR-001: SVFMode enum with 8 values
  - FR-002: SVFOutputs struct with 4 members
  - FR-003: SVF class exists
  - FR-004: prepare(double sampleRate) implemented
  - FR-005: setMode(SVFMode mode) implemented
  - FR-006: setCutoff(float hz) with clamping and immediate recalculation
  - FR-007: setResonance(float q) with clamping and immediate recalculation
  - FR-008: setGain(float dB) with clamping and immediate recalculation
  - FR-009: reset() noexcept implemented
  - FR-010: process(float input) noexcept implemented
  - FR-011: processBlock(float* buffer, size_t numSamples) noexcept implemented
  - FR-012: processMulti(float input) noexcept implemented
  - FR-013: TPT topology with g = tan(pi*fc/fs), k = 1/Q
  - FR-014: Derived coefficients a1, a2, a3 computed
  - FR-015: Two integrator state variables ic1eq, ic2eq
  - FR-016: Per-sample computation with trapezoidal integration
  - FR-017: Mode mixing with m0, m1, m2 coefficients for all 8 modes
  - FR-018: All processing methods declared noexcept
  - FR-019: Denormal flushing with detail::flushDenormal() after every process()
  - FR-020: No allocations, exceptions, or I/O in processing
  - FR-021: Pre-prepare behavior (input unchanged / zeros)
  - FR-022: NaN/Inf handling with state reset
  - FR-023: Only depends on Layer 0 (math_constants.h, db_utils.h)
  - FR-024: Header-only implementation
  - FR-025: Krate::DSP namespace
  - FR-026: Doxygen documentation for all public methods
  - FR-027: Naming conventions followed
- [ ] T054 Review ALL SC-xxx success criteria (SC-001 through SC-014) and verify measurable targets are achieved:
  - SC-001: LP 1000Hz attenuates 10kHz by >= 22dB
  - SC-002: LP 1000Hz passes 100Hz with < 0.5dB attenuation
  - SC-003: HP 100Hz attenuates 10Hz by >= 22dB
  - SC-004: HP 100Hz passes 1kHz with < 0.5dB attenuation
  - SC-005: BP 1000Hz Q=5 peak within 1dB of unity
  - SC-006: Notch 1000Hz attenuates 1kHz by >= 20dB
  - SC-007: Allpass flat magnitude within 0.1dB
  - SC-008: Peak +6dB boosts 1kHz by 6dB (+/- 1dB)
  - SC-009: Low shelf +6dB boosts 100Hz by 6dB (+/- 1dB)
  - SC-010: High shelf +6dB boosts 10kHz by 6dB (+/- 1dB)
  - SC-011: Audio-rate modulation no clicks (max change < 0.5)
  - SC-012: processBlock() bit-identical to process()
  - SC-013: 1M samples no NaN/Inf from valid inputs
  - SC-014: 100% test coverage of public methods
- [ ] T055 Search for cheating patterns in implementation:
  - No `// placeholder` or `// TODO` comments in new code
  - No test thresholds relaxed from spec requirements
  - No features quietly removed from scope
  - grep -r "TODO\|FIXME\|placeholder" dsp/include/krate/dsp/primitives/svf.h

### 9.2 Fill Compliance Table in spec.md

- [ ] T056 Update specs/080-svf/spec.md "Implementation Verification" section with compliance status for each requirement (MET/NOT MET/PARTIAL/DEFERRED) with evidence
- [ ] T057 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 9.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T058 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim

### 10.1 Final Commit

- [ ] T059 Commit all spec work to feature branch 080-svf
- [ ] T060 Verify all tests pass: "/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[svf]"

### 10.2 Completion Claim

- [ ] T061 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-6)**: All depend on Foundational phase completion
  - User Story 1 (core topology + basic modes) MUST complete first - foundation for all others
  - User Story 2 (processMulti) can start after US1 - extends same processing core
  - User Story 3 (additional modes) can start after US1 - extends mode mixing
  - User Story 4 (processBlock) can start after US1 - simple wrapper around process()
- **Polish (Phase 7)**: Depends on all user stories being complete
- **Documentation (Phase 8)**: Depends on Polish completion
- **Verification (Phase 9)**: Depends on Documentation completion
- **Completion (Phase 10)**: Depends on Verification completion

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories - Core SVF topology
- **User Story 2 (P1)**: Depends on US1 completion - Uses same processing core, adds multi-output
- **User Story 3 (P2)**: Depends on US1 completion - Extends mode mixing with shelf/peak/allpass
- **User Story 4 (P2)**: Depends on US1 completion - Wraps process() in block loop

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Test file creation before implementation file
- Build before test execution
- Verify tests pass after implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- **Phase 1**: T001 and T002 can run in parallel (different verification tasks)
- **Phase 2**: T003 and T004 can run in parallel (different header verification)
- **Phase 7**: T042, T043, T044, T045, T046, T047 can run in parallel (different verification tasks)
- **Within a user story**: Test writing (when multiple test aspects exist) can be done in any order before implementation
- **User Stories**: US2, US3, US4 could theoretically run in parallel after US1 completes, but sequential is safer due to shared file

---

## Parallel Example: Phase 7 (Polish)

```bash
# These verification tasks can run in parallel:

# Developer A: Stability tests
Task T042: Add 1M sample stability tests

# Developer B: Code quality verification
Task T044: Verify naming conventions
Task T045: Verify Layer 1 dependencies
Task T047a: Verify Doxygen documentation (FR-026)

# Developer C: Namespace and architecture
Task T046: Verify header-only
Task T047: Verify namespace
Task T047b: Add static noexcept verification tests

# All can proceed independently, no conflicts
```

---

## Implementation Strategy

### MVP First (User Stories 1 and 2 Only)

The MVP should include the core modulation-stable filter with multi-output:

1. Complete Phase 1: Setup (verify infrastructure)
2. Complete Phase 2: Foundational (verify dependencies)
3. Complete Phase 3: User Story 1 (Core SVF with LP/HP/BP/Notch - P1 priority)
4. Complete Phase 4: User Story 2 (processMulti multi-output - P1 priority)
5. **STOP and VALIDATE**: Test SVF with basic modes independently
6. Deploy/demo if ready

This gives DSP developers immediate access to modulation-stable filtering with simultaneous outputs for auto-wah and multi-band effects.

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 (core SVF with 4 basic modes) → Test independently → Core filter available
3. Add User Story 2 (processMulti) → Test independently → Multi-output available
4. Add User Story 3 (shelf/peak/allpass modes) → Test independently → Full mode set available
5. Add User Story 4 (processBlock) → Test independently → Complete API
6. Each component adds value without breaking previous components

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (core SVF - must complete first)
3. After US1 completes:
   - Developer A or B: User Story 2 (processMulti - same file as US1)
   - Developer C: User Story 3 (additional modes - same file, low conflict risk)
   - Developer D: User Story 4 (processBlock - same file, low conflict risk)
4. US2, US3, US4 can potentially overlap if carefully coordinated (all modify svf.h)

---

## Task Summary

- **Total Tasks**: 63 tasks
- **Setup Phase**: 2 tasks
- **Foundational Phase**: 2 tasks
- **User Story 1 (Core SVF + basic modes)**: 9 tasks
- **User Story 2 (processMulti)**: 8 tasks
- **User Story 3 (Additional modes)**: 12 tasks
- **User Story 4 (processBlock)**: 8 tasks
- **Polish Phase**: 9 tasks (includes T047a Doxygen verification, T047b noexcept static tests)
- **Documentation Phase**: 4 tasks
- **Verification Phase**: 6 tasks
- **Completion Phase**: 3 tasks

### Task Count by User Story

- **US1 (Core SVF with LP/HP/BP/Notch - P1)**: 9 tasks
- **US2 (processMulti multi-output - P1)**: 8 tasks
- **US3 (Allpass/Peak/Shelf modes - P2)**: 12 tasks
- **US4 (processBlock - P2)**: 8 tasks

### Parallel Opportunities Identified

- Phase 1: 2 tasks can run in parallel
- Phase 2: 2 tasks can run in parallel
- Phase 7: 8 tasks can run in parallel (T042-T048 including T047a, T047b)
- User Stories 2, 3, 4: Could potentially run in parallel after US1 (same file, requires coordination)

### Independent Test Criteria

- **US1 (Core SVF)**: Configure filter, modulate cutoff at audio rate, verify no clicks or discontinuities
- **US2 (processMulti)**: Process test signal, verify all four outputs have correct frequency responses simultaneously
- **US3 (Additional modes)**: Configure each mode, verify expected frequency response
- **US4 (processBlock)**: Compare block output with equivalent sample-by-sample processing

### Suggested MVP Scope

**MVP = User Stories 1 and 2** (SVF with core 4 modes + processMulti)

This provides immediately useful modulation-stable filtering:
- Lowpass, Highpass, Bandpass, Notch modes
- Audio-rate modulation stability (key differentiator)
- Simultaneous multi-output processing
- Real-time safe, zero allocation
- Complete with tests and documentation

User Stories 3 and 4 (shelf/peak/allpass modes and block processing) are valuable additions but less critical for the primary use case of modulation-stable synth filters.

---

## Notes

- [P] tasks = different files/verification, no dependencies
- [Story] label maps task to specific user story for traceability (US1, US2, US3, US4)
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- File locations are absolute paths for clarity: dsp/include/krate/dsp/primitives/svf.h, dsp/tests/primitives/svf_test.cpp
- Implementation order follows dependency chain: Core topology → Multi-output → Extended modes → Block processing
- Test-first workflow enforced at every step
- All tasks use proper checklist format: `- [ ] [TaskID] [P?] [Story?] Description with file path`
