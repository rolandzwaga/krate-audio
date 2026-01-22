# Tasks: Phaser Effect Processor

**Input**: Design documents from `specs/079-phaser/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/phaser-api.md

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
             unit/processors/phaser_test.cpp  # ADD YOUR FILE HERE
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

## Phase 1: Setup (No Dependencies)

**Purpose**: Verify Layer 1 dependencies are ready for use

- [X] T001 [P] Verify Allpass1Pole exists at `dsp/include/krate/dsp/primitives/allpass_1pole.h`
- [X] T002 [P] Verify LFO exists at `dsp/include/krate/dsp/primitives/lfo.h`
- [X] T003 [P] Verify OnePoleSmoother exists at `dsp/include/krate/dsp/primitives/smoother.h`

**Checkpoint**: All Layer 1 dependencies confirmed available

---

## Phase 2: User Story 1 - Basic Phaser Effect (Priority: P1) - MVP

**Goal**: Create a phaser that produces audible sweeping notches through cascaded allpass stages with LFO modulation. This is the core functionality without which the component has no value.

**Independent Test**: Instantiate a Phaser, set 4 stages with 0.5 Hz LFO rate, process a signal, and verify characteristic notches appear in the frequency response.

**Acceptance Scenarios**:
1. Multiple notches audible in output that sweep through frequency spectrum (4 stages with 0.5 Hz rate)
2. Output contains both original and phase-shifted signal at 50% mix (comb-filtering effect)
3. Notches remain stationary when depth is 0.0 (no sweeping motion)

### 2.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T004 [US1] Create test file at `dsp/tests/unit/processors/phaser_test.cpp` with basic test structure (includes, test cases, no implementation)
- [X] T005 [US1] Write failing test: `TEST_CASE("Phaser - Lifecycle", "[Phaser][US1]")` - test prepare(), reset(), isPrepared()
- [X] T006 [US1] Write failing test: `TEST_CASE("Phaser - Basic Processing", "[Phaser][US1]")` - test process() returns modified output
- [X] T007 [US1] Write failing test: `TEST_CASE("Phaser - Block Processing", "[Phaser][US1]")` - test processBlock() modifies buffer
- [X] T008 [US1] Write failing test: `TEST_CASE("Phaser - Stage Configuration", "[Phaser][US1]")` - test setNumStages() with valid/invalid values
- [X] T009 [US1] Write failing test: `TEST_CASE("Phaser - LFO Rate Control", "[Phaser][US1]")` - test setRate() and getRate()
- [X] T010 [US1] Write failing test: `TEST_CASE("Phaser - Depth Control", "[Phaser][US1]")` - test setDepth() affects sweep range
- [X] T011 [US1] Write failing test: `TEST_CASE("Phaser - Center Frequency", "[Phaser][US1]")` - test setCenterFrequency() and getCenterFrequency()
- [X] T012 [US1] Write failing test: `TEST_CASE("Phaser - Mix Control", "[Phaser][US1]")` - test setMix() blends dry/wet
- [X] T013 [US1] Write failing test: `TEST_CASE("Phaser - Stationary Notches at Zero Depth", "[Phaser][US1]")` - verify FR-004 (depth = 0 stops sweep)
- [X] T014 [US1] Verify all tests FAIL (no implementation exists yet)
- [X] T015 [US1] Build tests to confirm compilation: `cmake --build build --config Release --target dsp_tests`

### 2.2 Implementation for User Story 1

- [X] T016 [US1] Create header file at `dsp/include/krate/dsp/processors/phaser.h` with class declaration
- [X] T017 [US1] Implement constants (kMaxStages, kDefaultStages, etc.) per data-model.md
- [X] T018 [US1] Implement member variables (stagesL_, stagesR_, lfoL_, lfoR_, smoothers, feedback state, config)
- [X] T019 [US1] Implement constructor with default values
- [X] T020 [US1] Implement `prepare(double sampleRate)` - initialize all components
- [X] T021 [US1] Implement `reset()` - clear filter states and feedback
- [X] T022 [US1] Implement `isPrepared() const` - return prepared_ flag
- [X] T023 [US1] Implement `setNumStages(int stages)` with clamping to even numbers [2, 12]
- [X] T024 [US1] Implement `getNumStages() const` - return numStages_
- [X] T025 [US1] Implement `setRate(float hz)` with smoother target update
- [X] T026 [US1] Implement `getRate() const` - return rate_
- [X] T027 [US1] Implement `setDepth(float amount)` with smoother target update
- [X] T028 [US1] Implement `getDepth() const` - return depth_
- [X] T029 [US1] Implement `setCenterFrequency(float hz)` with smoother target update
- [X] T030 [US1] Implement `getCenterFrequency() const` - return centerFrequency_
- [X] T031 [US1] Implement `setMix(float dryWet)` with smoother target update
- [X] T032 [US1] Implement `getMix() const` - return mix_
- [X] T033 [US1] Implement frequency sweep calculation helper (exponential mapping per FR-002)
- [X] T034 [US1] Implement `process(float input)` - single sample mono processing with LFO modulation
- [X] T035 [US1] Implement `processBlock(float* buffer, size_t numSamples)` - block processing
- [X] T036 [US1] Add NaN/Inf input handling per FR-015
- [X] T037 [US1] Add denormal flushing per FR-016
- [X] T037a [US1] Write test: `TEST_CASE("Phaser - Denormal Flushing", "[Phaser][US1]")` - verify FR-016: process 1000 samples with extremely small values (1e-40) and verify no performance degradation occurs
- [X] T038 [US1] Verify all US1 tests pass: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe "[US1]"`

### 2.3 Cross-Platform Verification (MANDATORY)

- [X] T039 [US1] **Verify IEEE 754 compliance**: Check if `phaser_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 2.4 Commit (MANDATORY)

- [X] T040 [US1] **Commit completed User Story 1 work**: "feat(dsp): implement basic phaser effect (US1)"

**Checkpoint**: User Story 1 should be fully functional, tested, and committed. Basic phaser with stage control, LFO rate, depth, center frequency, and mix.

---

## Phase 3: User Story 2 - Variable Stage Count (Priority: P2)

**Goal**: Enable users to control phaser intensity by adjusting the number of allpass stages from subtle (2 stages) to intense (12 stages).

**Independent Test**: Compare frequency responses with different stage counts (2, 6, 12) and verify the expected number of notches appears in each case.

**Acceptance Scenarios**:
1. 2 stages produces 1 notch in frequency response
2. 12 stages produces 6 notches in frequency response
3. Odd numbers (e.g., 5) are clamped to nearest even number (4 or 6)

### 3.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T041 [US2] Write failing test: `TEST_CASE("Phaser - Stage Count Validation", "[Phaser][US2]")` - test clamping to even numbers and range [2, 12]
- [X] T042 [US2] Write failing test: `TEST_CASE("Phaser - Notch Count vs Stage Count", "[Phaser][US2]")` - verify N stages produces N/2 notches (FR-001)
- [X] T043 [US2] Write failing test: `TEST_CASE("Phaser - Stage Count Changes", "[Phaser][US2]")` - test runtime stage count changes
- [X] T044 [US2] Verify tests FAIL (stage validation logic not fully implemented yet)

### 3.2 Implementation for User Story 2

- [X] T045 [US2] Enhance `setNumStages()` with comprehensive validation (odd number clamping, range clamping)
- [X] T046 [US2] Add validation in `process()` to only use active stages (numStages_ not kMaxStages)
- [X] T047 [US2] Verify all US2 tests pass: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe "[US2]"`

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T048 [US2] **Verify IEEE 754 compliance**: Check if any new test code uses IEEE 754 functions requiring `-fno-fast-math`

### 3.4 Commit (MANDATORY)

- [X] T049 [US2] **Commit completed User Story 2 work**: "feat(dsp): add phaser stage count validation and control (US2)"

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed. Stage count properly validated and affects notch count.

---

## Phase 4: User Story 3 - Feedback Resonance (Priority: P2)

**Goal**: Add resonance and emphasis at notch frequencies through feedback control, creating the intense, resonant character of classic phasers.

**Independent Test**: Process audio with 0% feedback vs 75% feedback and measure the Q/sharpness of the resulting notches.

**Acceptance Scenarios**:
1. Feedback at 0 produces normal (moderate) depth notches
2. Feedback at 0.8 produces sharper, more pronounced notches with audible resonance
3. Negative feedback (e.g., -0.5) shifts notch positions, creating different tonal character
4. Feedback at maximum (+/-1.0) remains stable without runaway oscillation

### 4.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T050 [US3] Write failing test: `TEST_CASE("Phaser - Feedback Control", "[Phaser][US3]")` - test setFeedback() and getFeedback()
- [X] T051 [US3] Write failing test: `TEST_CASE("Phaser - Feedback Range", "[Phaser][US3]")` - test bipolar range [-1.0, +1.0]
- [X] T052 [US3] Write failing test: `TEST_CASE("Phaser - Feedback Stability", "[Phaser][US3]")` - verify max feedback doesn't oscillate (FR-012, SC-008)
- [X] T053 [US3] Write failing test: `TEST_CASE("Phaser - Negative Feedback Effect", "[Phaser][US3]")` - verify negative feedback produces different output
- [X] T054 [US3] Write failing test: `TEST_CASE("Phaser - Feedback Increases Notch Depth", "[Phaser][US3]")` - verify SC-003 (12dB increase at 0.9 feedback)
- [X] T055 [US3] Verify tests FAIL (feedback not implemented yet)

### 4.2 Implementation for User Story 3

- [X] T056 [US3] Implement `setFeedback(float amount)` with smoother target update and range clamping
- [X] T057 [US3] Implement `getFeedback() const` - return feedback_
- [X] T058 [US3] Implement mix-before-feedback topology in `process()` - mix dry+wet first, then feedback to first stage
- [X] T059 [US3] Implement tanh soft-clipping for feedback signal per FR-012: `feedbackSignal = tanh(feedbackSignal * feedbackAmount)`
- [X] T060 [US3] Update `reset()` to clear feedback state (feedbackStateL_, feedbackStateR_)
- [X] T061 [US3] Verify all US3 tests pass: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe "[US3]"`

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T062 [US3] **Verify IEEE 754 compliance**: Check if any new test code uses IEEE 754 functions requiring `-fno-fast-math`

### 4.4 Commit (MANDATORY)

- [X] T063 [US3] **Commit completed User Story 3 work**: "feat(dsp): add phaser feedback resonance control (US3)"

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed. Feedback control with stability and resonance.

---

## Phase 5: User Story 4 - Stereo Processing with Spread (Priority: P3)

**Goal**: Create wide stereo phaser effect by applying different LFO phases to left and right channels.

**Independent Test**: Process stereo audio with 90-degree spread and verify left and right channels have phase-offset modulation.

**Acceptance Scenarios**:
1. Stereo spread at 180 degrees produces inverted L/R modulation
2. Stereo spread at 0 degrees produces identical L/R modulation (mono-compatible)
3. Stereo spread at 90 degrees: when L LFO is at peak, R LFO is at midpoint

### 5.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T064 [US4] Write failing test: `TEST_CASE("Phaser - Stereo Processing", "[Phaser][US4]")` - test processStereo() exists and processes both channels
- [X] T065 [US4] Write failing test: `TEST_CASE("Phaser - Stereo Spread Control", "[Phaser][US4]")` - test setStereoSpread() and getStereoSpread()
- [X] T066 [US4] Write failing test: `TEST_CASE("Phaser - Stereo Spread at 180 Degrees", "[Phaser][US4]")` - verify inverted L/R modulation
- [X] T067 [US4] Write failing test: `TEST_CASE("Phaser - Stereo Spread at 0 Degrees", "[Phaser][US4]")` - verify identical L/R (mono)
- [X] T068 [US4] Write failing test: `TEST_CASE("Phaser - Stereo Correlation", "[Phaser][US4]")` - verify SC-004 (correlation < 0.3 at 180 degrees)
- [X] T069 [US4] Verify tests FAIL (stereo processing not implemented yet)

### 5.2 Implementation for User Story 4

- [X] T070 [US4] Implement `setStereoSpread(float degrees)` with wrapping to [0, 360]
- [X] T071 [US4] Implement `getStereoSpread() const` - return stereoSpread_
- [X] T072 [US4] Update `prepare()` to configure lfoR_ phase offset based on stereoSpread_
- [X] T073 [US4] Implement `processStereo(float* left, float* right, size_t numSamples)` - independent L/R processing
- [X] T074 [US4] Update stereo processing to use separate feedback states (feedbackStateL_, feedbackStateR_)
- [X] T075 [US4] Verify all US4 tests pass: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe "[US4]"`

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T076 [US4] **Verify IEEE 754 compliance**: Check if any new test code uses IEEE 754 functions requiring `-fno-fast-math`

### 5.4 Commit (MANDATORY)

- [X] T077 [US4] **Commit completed User Story 4 work**: "feat(dsp): add stereo phaser processing with spread (US4)"

**Checkpoint**: All user stories 1-4 should work independently and be committed. Stereo width control operational.

---

## Phase 6: User Story 5 - Tempo-Synchronized Modulation (Priority: P3)

**Goal**: Enable phaser sweep to lock to song tempo for rhythmic effects.

**Independent Test**: Set tempo sync enabled with known BPM and note value, then measure actual LFO cycle duration.

**Acceptance Scenarios**:
1. Tempo sync at 120 BPM with quarter note produces exactly 2 Hz modulation rate (+/- 0.01 Hz)
2. Tempo sync with dotted eighth at 100 BPM matches dotted eighth duration
3. Tempo sync disabled: rate set to 1.5 Hz produces exactly 1.5 Hz regardless of tempo

### 6.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T078 [US5] Write failing test: `TEST_CASE("Phaser - Tempo Sync Control", "[Phaser][US5]")` - test setTempoSync() and isTempoSyncEnabled()
- [X] T079 [US5] Write failing test: `TEST_CASE("Phaser - Note Value Configuration", "[Phaser][US5]")` - test setNoteValue() with modifiers
- [X] T080 [US5] Write failing test: `TEST_CASE("Phaser - Tempo Setting", "[Phaser][US5]")` - test setTempo() updates LFO when sync enabled
- [X] T081 [US5] Write failing test: `TEST_CASE("Phaser - Tempo Sync at Quarter Note", "[Phaser][US5]")` - verify SC-005 (120 BPM quarter = 2 Hz)
- [X] T082 [US5] Write failing test: `TEST_CASE("Phaser - Tempo Sync Disabled", "[Phaser][US5]")` - verify free rate when sync off
- [X] T083 [US5] Verify tests FAIL (tempo sync not implemented yet)

### 6.2 Implementation for User Story 5

- [X] T084 [US5] Implement `setTempoSync(bool enabled)` - enable/disable tempo sync on LFOs
- [X] T085 [US5] Implement `isTempoSyncEnabled() const` - return tempoSync_
- [X] T086 [US5] Implement `setNoteValue(NoteValue value, NoteModifier modifier)` - configure LFO note values
- [X] T087 [US5] Implement `setTempo(float bpm)` - update LFO tempo
- [X] T088 [US5] Update `prepare()` to configure LFO tempo sync parameters
- [X] T089 [US5] Verify all US5 tests pass: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe "[US5]"`

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T090 [US5] **Verify IEEE 754 compliance**: Check if any new test code uses IEEE 754 functions requiring `-fno-fast-math`

### 6.4 Commit (MANDATORY)

- [X] T091 [US5] **Commit completed User Story 5 work**: "feat(dsp): add tempo-synced phaser modulation (US5)"

**Checkpoint**: All user stories should be independently functional and committed. Tempo sync operational.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Enhancements that affect multiple user stories and final verification

### 7.1 LFO Waveform Selection (FR-011)

- [X] T092 [P] Write test: `TEST_CASE("Phaser - Waveform Selection", "[Phaser][Polish]")` - test setWaveform() with Sine, Triangle, Square, Sawtooth
- [X] T093 [P] Implement `setWaveform(Waveform waveform)` - delegate to both LFOs
- [X] T094 [P] Implement `getWaveform() const` - return waveform_
- [X] T095 Verify waveform tests pass

### 7.2 Block Processing Equivalence (SC-007)

- [X] T096 Write test: `TEST_CASE("Phaser - Block vs Sample-by-Sample", "[Phaser][Polish]")` - verify bit-identical results
- [X] T097 Fix any discrepancies found in block vs sample-by-sample processing

### 7.3 Performance Verification (SC-001)

- [X] T098 Write test: `TEST_CASE("Phaser - Performance", "[Phaser][.benchmark]")` - verify < 1ms for 1 second at 44.1kHz with 12 stages
- [X] T099 Profile and optimize if needed to meet SC-001 (< 0.5% CPU)

### 7.4 Edge Cases and Robustness

- [X] T100 [P] Write test: `TEST_CASE("Phaser - Sample Rate Change", "[Phaser][Polish]")` - verify prepare() recalculates coefficients
- [X] T100a [P] Write test: `TEST_CASE("Phaser - Coefficient Recalculation", "[Phaser][Polish]")` - verify FR-014: calling prepare() with different sample rates produces different allpass coefficients (test by comparing filter behavior at 44100 vs 96000 Hz)
- [X] T101 [P] Write test: `TEST_CASE("Phaser - Parameter Smoothing", "[Phaser][Polish]")` - verify SC-006 (no clicks/zippers)
- [X] T102 [P] Write test: `TEST_CASE("Phaser - Extreme Frequencies", "[Phaser][Polish]")` - verify clamping near Nyquist
- [X] T103 Verify all edge case tests pass

### 7.5 Commit Polish Work

- [X] T104 **Commit polish work**: "polish(dsp): add waveform selection and performance validation for phaser"

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 8.1 Architecture Documentation Update

- [X] T105 **Update `specs/_architecture_/layer-2-processors.md`** with Phaser component:
  - Add entry in appropriate section with purpose, public API summary, file location
  - Include "when to use this" guidance
  - Add usage example (basic phaser setup)
  - Note relationship to Allpass1Pole, LFO, and OnePoleSmoother
  - Verify no duplicate functionality introduced

### 8.2 Final Commit

- [X] T106 **Commit architecture documentation updates**: "docs(arch): add Phaser to layer 2 processors documentation"
- [X] T107 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 9.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T108 **Review ALL FR-xxx requirements** (FR-001 through FR-020) from `spec.md` against implementation
- [X] T109 **Review ALL SC-xxx success criteria** (SC-001 through SC-008) and verify measurable targets are achieved
- [X] T110 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in `phaser.h`
  - [X] No test thresholds relaxed from spec requirements
  - [X] No features quietly removed from scope

### 9.2 Fill Compliance Table in spec.md

- [X] T111 **Update `specs/079-phaser/spec.md` "Implementation Verification" section** with compliance status for each FR/SC requirement
- [X] T112 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 9.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T113 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim

### 10.1 Final Verification

- [X] T114 **Run all tests**: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe "[Phaser]"`
- [X] T115 **Verify zero compiler warnings** in `phaser.h`
- [X] T116 **Commit final completion**: "feat(dsp): complete phaser effect processor implementation"

### 10.2 Completion Claim

- [X] T117 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **User Story 1 (Phase 2)**: Depends on Setup - MVP starts here
- **User Stories 2-5 (Phases 3-6)**: Can start after US1 completes
  - User stories can proceed in parallel (if staffed) or sequentially (P2 → P3)
  - Each story is independently testable
- **Polish (Phase 7)**: Depends on desired user stories being complete
- **Documentation (Phase 8)**: After all functionality complete
- **Verification (Phase 9)**: After documentation
- **Completion (Phase 10)**: After verification

### User Story Dependencies

- **User Story 1 (P1 - Phase 2)**: MVP - Basic phaser effect
  - No dependencies on other stories
  - Establishes core processing infrastructure
- **User Story 2 (P2 - Phase 3)**: Variable stage count
  - Enhances US1 stage validation
  - Independently testable
- **User Story 3 (P2 - Phase 4)**: Feedback resonance
  - Adds to US1 processing path
  - Independently testable
- **User Story 4 (P3 - Phase 5)**: Stereo processing
  - Builds on US1-3 mono processing
  - Independently testable
- **User Story 5 (P3 - Phase 6)**: Tempo sync
  - Enhances US1 LFO control
  - Independently testable

### Within Each User Story

1. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
2. Core processing methods before advanced features
3. **Verify tests pass**: After implementation
4. **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in `dsp/tests/CMakeLists.txt`
5. **Commit**: LAST task in user story - commit completed work

### Parallel Opportunities

- Setup tasks (T001-T003) can run in parallel
- Test creation within each user story can run in parallel (all test tasks marked with [P])
- Once US1 completes, US2-US5 can theoretically proceed in parallel (if team capacity allows)
- Polish tasks (T092-T102) can largely run in parallel

---

## Parallel Example: User Story 1

```bash
# Launch all test creation tasks together:
T004-T013: All test cases can be written in parallel

# After tests written, implementation proceeds sequentially:
T016-T037: Class structure, then methods, then processing logic

# Tests run as single batch:
T038: All US1 tests verified together
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (verify dependencies)
2. Complete Phase 2: User Story 1 (basic phaser effect)
3. **STOP and VALIDATE**: Test US1 independently
4. Demo basic phaser functionality

### Incremental Delivery

1. Complete Setup (Phase 1) → Dependencies verified
2. Add User Story 1 (Phase 2) → Test independently → MVP complete!
3. Add User Story 2 (Phase 3) → Test independently → Stage control complete
4. Add User Story 3 (Phase 4) → Test independently → Feedback complete
5. Add User Story 4 (Phase 5) → Test independently → Stereo complete
6. Add User Story 5 (Phase 6) → Test independently → Tempo sync complete
7. Polish (Phase 7) → Cross-cutting enhancements
8. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup together (Phase 1)
2. Developer A: User Story 1 (Phase 2) - MUST complete first (MVP)
3. Once US1 is done:
   - Developer A: User Story 2 (Phase 3)
   - Developer B: User Story 3 (Phase 4)
   - Developer C: User Story 4 (Phase 5)
4. Stories complete and integrate independently

---

## Notes

- **[P] tasks** = different files, no dependencies, can run in parallel
- **[Story] label** maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

---

## Summary

- **Total Tasks**: 119
- **Task Count per User Story**:
  - US1 (Basic Phaser): 38 tasks (T004-T040, T037a)
  - US2 (Variable Stages): 9 tasks (T041-T049)
  - US3 (Feedback): 14 tasks (T050-T063)
  - US4 (Stereo): 14 tasks (T064-T077)
  - US5 (Tempo Sync): 14 tasks (T078-T091)
  - Polish: 14 tasks (T092-T104, T100a)
  - Documentation: 3 tasks (T105-T107)
  - Verification: 6 tasks (T108-T113)
  - Completion: 4 tasks (T114-T117)
- **Parallel Opportunities**:
  - Setup verification (3 tasks)
  - Test creation within each story (multiple tasks per story)
  - User stories after US1 (if team capacity allows)
  - Polish tasks (largely independent)
- **Independent Test Criteria**:
  - US1: Process signal with 4 stages at 0.5 Hz, verify notches appear
  - US2: Compare frequency responses at 2/6/12 stages, verify notch count
  - US3: Process with 0% vs 75% feedback, measure notch Q/sharpness
  - US4: Process stereo at 90° spread, verify L/R phase offset
  - US5: Set tempo sync at 120 BPM quarter note, measure 2 Hz rate
- **Suggested MVP Scope**: User Story 1 only (basic phaser effect with stage control, LFO, depth, center frequency, mix)
- **Format Validation**: All tasks follow `- [ ] [ID] [P?] [Story?] Description with file path` format
