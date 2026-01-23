---
description: "Task breakdown for Sample & Hold Filter implementation"
---

# Tasks: Sample & Hold Filter

**Input**: Design documents from `/specs/089-sample-hold-filter/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/sample_hold_filter_api.h

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## WARNING: Test-First Development Workflow

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
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/sample_hold_filter_test.cpp  # ADD YOUR FILE HERE
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

**Purpose**: Create project structure and verify dependencies

- [X] T001 Verify existing DSP dependencies available: SVF (Layer 1), LFO (Layer 1), OnePoleSmoother (Layer 1), EnvelopeFollower (Layer 2), Xorshift32 (Layer 0)
- [X] T002 Create test file structure at dsp/tests/unit/processors/sample_hold_filter_test.cpp
- [X] T003 Verify no ODR conflicts by searching for existing SampleHoldFilter, TriggerSource, SampleSource classes

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core enumerations and internal structures that all user stories depend on

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T004 Define TriggerSource enum (Clock, Audio, Random) in dsp/include/krate/dsp/processors/sample_hold_filter.h
- [X] T005 Define SampleSource enum (LFO, Random, Envelope, External) in dsp/include/krate/dsp/processors/sample_hold_filter.h
- [X] T006 Define ParameterState struct with enabled, source, modulationRange, heldValue, smoother fields
- [X] T007 Create SampleHoldFilter class skeleton with all constants (kMinHoldTimeMs through kMaxBaseQ) per API contract
- [X] T008 Add all private member variables: filterL_, filterR_, lfo_, envelopeFollower_, rng_, three ParameterState instances, trigger state variables, configuration variables
- [X] T009 Implement default constructor initializing all member variables to safe defaults
- [X] T010 Verify header compiles with no ODR violations, correct namespace (Krate::DSP), proper include guards

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Basic Stepped Filter Effect (Priority: P1) 🎯 MVP

**Goal**: Implement core S&H functionality with clock-synchronized sampling of cutoff frequency from internal LFO, creating distinctive stepped filter patterns

**Independent Test**: Configure hold time, enable cutoff sampling from LFO source, verify cutoff changes only at hold interval boundaries (every N samples exactly)

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T011 [P] [US1] Write lifecycle tests (prepare, reset, getters) in dsp/tests/unit/processors/sample_hold_filter_test.cpp
- [X] T012 [P] [US1] Write clock trigger timing tests: verify exact hold interval accuracy at various hold times (FR-003, SC-001)
- [X] T013 [P] [US1] Write LFO sample source tests: verify LFO values are sampled at trigger points and held between triggers (FR-007)
- [X] T014 [P] [US1] Write cutoff modulation tests: verify octave-based modulation calculation and clamping (FR-011)
- [X] T015 [P] [US1] Write mono processing tests: verify process() returns filtered output with stepped cutoff (FR-021, FR-024)
- [X] T016 [P] [US1] Verify all tests FAIL (no implementation yet)

### 3.2 Implementation for User Story 1

- [X] T017 [US1] Implement prepare() in sample_hold_filter.h: initialize sample rate, prepare SVF instances, prepare LFO, calculate maxCutoff, configure smoothers (FR-025)
- [X] T018 [US1] Implement reset() in sample_hold_filter.h: reset all state to initial values, snap smoothers to 0.0 (FR-026)
- [X] T019 [US1] Implement setHoldTime() and getHoldTime(): clamp to range, calculate holdTimeSamples_, reset trigger counter
- [X] T020 [US1] Implement setLFORate() and getLFORate(): clamp to range [0.01, 20], configure LFO frequency (FR-007)
- [X] T021 [US1] Implement setCutoffSamplingEnabled(), setCutoffSource(), setCutoffOctaveRange() and corresponding getters (FR-011, FR-014)
- [X] T022 [US1] Implement setBaseCutoff() and getBaseCutoff(): clamp to range [20, maxCutoff_] (FR-019)
- [X] T023 [US1] Implement setFilterMode() to configure SVF mode (Lowpass, Highpass, Bandpass, Notch) (FR-018)
- [X] T024 [US1] Implement clockTrigger() helper: decrement samplesUntilTrigger_, return true when <= 0, reset to holdTimeSamples_ (FR-003)
- [X] T025 [US1] Implement getSampleValue() helper: switch on SampleSource enum, return LFO value (Random/Envelope/External return 0 for now)
- [X] T026 [US1] Implement onTrigger() helper: if cutoff enabled, get sample value from source, set smoother target (FR-011, FR-014)
- [X] T027 [US1] Implement calculateFinalCutoff() helper: base cutoff * pow(2, smoothedMod * octaveRange), clamp to [kMinCutoff, maxCutoff_] (FR-011)
- [X] T028 [US1] Implement process(float input): update LFO, check clock trigger, call onTrigger if triggered, calculate final cutoff, update filterL_ cutoff, process sample (FR-021)
- [X] T029 [US1] Implement processBlock(): loop calling process() for each sample (FR-023)
- [X] T030 [US1] Implement isPrepared() and sampleRate() getters

### 3.3 Verification

- [X] T031 [US1] Build DSP tests: cmake --build build/windows-x64-release --config Release --target dsp_tests
- [X] T032 [US1] Run all User Story 1 tests and verify they PASS
- [X] T033 [US1] Verify timing accuracy SC-001: hold timing within 1 sample at 192kHz
- [X] T034 [US1] Verify determinism: same seed/params produce identical output

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T035 [US1] Verify IEEE 754 compliance: if tests use std::isnan/std::isfinite/std::isinf, add dsp/tests/unit/processors/sample_hold_filter_test.cpp to -fno-fast-math list in tests/CMakeLists.txt

### 3.5 Commit (MANDATORY)

- [X] T036 [US1] Commit completed User Story 1 work with message: "feat(dsp): add SampleHoldFilter User Story 1 - Basic Stepped Filter Effect"

**Checkpoint**: User Story 1 should be fully functional, tested, and committed. Processor can create clock-synced stepped cutoff effects.

---

## Phase 4: User Story 2 - Audio-Triggered Stepped Modulation (Priority: P2)

**Goal**: Expand trigger system to respond to audio transients, enabling dynamic filter sweeps that follow rhythm of input signal

**Independent Test**: Feed signal with impulses, verify held value updates on transient detection, verify no updates below threshold

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T037 [P] [US2] Write audio trigger detection tests: verify transients detected when crossing threshold (FR-004)
- [X] T038 [P] [US2] Write audio trigger hold period tests: verify subsequent transients ignored during hold time
- [X] T039 [P] [US2] Write audio trigger threshold tests: verify no trigger when input below threshold
- [X] T040 [P] [US2] Write transient response time tests: verify detection within 1ms of transient onset (SC-002)
- [X] T041 [P] [US2] Verify all User Story 2 tests FAIL (no Audio trigger implementation yet)

### 4.2 Implementation for User Story 2

- [X] T042 [US2] Implement setTransientThreshold() and getTransientThreshold(): clamp to [0, 1] (FR-004)
- [X] T043 [US2] Implement setTriggerSource() and getTriggerSource(): update triggerSource_, set pendingSourceSwitch flag
- [X] T044 [US2] Configure EnvelopeFollower in prepare(): set DetectionMode::Peak, attack time 0.1ms, release time 50ms (FR-009)
- [X] T045 [US2] Implement audioTrigger(float input) helper: process envelope follower, detect rising edge crossing threshold, manage hold period to ignore subsequent transients (FR-004)
- [X] T046 [US2] Update process(): add envelope follower processing, add trigger source switch statement calling clockTrigger or audioTrigger based on mode (FR-001)
- [X] T047 [US2] Handle mode switching: if pendingSourceSwitch flag set, immediately trigger sample from new source on next buffer boundary

### 4.3 Verification

- [X] T048 [US2] Build and run all tests, verify User Story 2 tests PASS
- [X] T049 [US2] Verify SC-002: audio transient detection responds within 1ms of onset
- [X] T050 [US2] Verify determinism maintained with audio trigger mode

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T051 [US2] Verify IEEE 754 compliance: check if new tests use NaN/Inf detection, update CMakeLists.txt if needed

### 4.5 Commit (MANDATORY)

- [X] T052 [US2] Commit completed User Story 2 work with message: "feat(dsp): add audio-triggered mode to SampleHoldFilter (US2)"

**Checkpoint**: User Stories 1 AND 2 should both work independently. Processor supports Clock and Audio trigger modes.

---

## Phase 5: User Story 3 - Random Trigger Probability (Priority: P2)

**Goal**: Add random trigger mode with probability setting, enabling chaotic but controllable stepped filter patterns for generative sound design

**Independent Test**: Set known seed and probability, verify triggers occur at expected random intervals based on probability over 100+ trials

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T053 [P] [US3] Write random trigger probability tests: verify p=1.0 always triggers, p=0.0 never triggers (FR-005)
- [X] T054 [P] [US3] Write random trigger statistical tests: verify p=0.5 produces 45-55% triggers over 1000 intervals with fixed seed (SC-007)
- [X] T055 [P] [US3] Write random sample source tests: verify Random source generates values in [-1, 1] using Xorshift32 (FR-008)
- [X] T056 [P] [US3] Write determinism tests: verify same seed produces identical random sequence (FR-027, SC-005)
- [X] T057 [P] [US3] Verify all User Story 3 tests FAIL (no Random trigger/source implementation yet)

### 5.2 Implementation for User Story 3

- [X] T058 [US3] Implement setTriggerProbability() and getTriggerProbability(): clamp to [0, 1] (FR-005)
- [X] T059 [US3] Implement setSeed() and getSeed(): store seed value, call rng_.seed() (FR-027)
- [X] T060 [US3] Implement randomTrigger() helper: same timing as clock trigger but evaluate probability using rng_.nextUnipolar() (FR-005)
- [X] T061 [US3] Update process() trigger switch statement: add case TriggerSource::Random calling randomTrigger()
- [X] T062 [US3] Update getSampleValue() switch statement: add case SampleSource::Random returning rng_.nextFloat() (FR-008)
- [X] T063 [US3] Update reset(): call rng_.seed(seed_) to restore deterministic state

### 5.3 Verification

- [X] T064 [US3] Build and run all tests, verify User Story 3 tests PASS
- [X] T065 [US3] Verify SC-007: Random trigger with p=0.5 produces 45-55% triggers over 1000 trials
- [X] T066 [US3] Verify SC-005: Deterministic output with same seed, parameters, and input

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T067 [US3] Verify IEEE 754 compliance: check if new tests use NaN/Inf detection, update CMakeLists.txt if needed

### 5.5 Commit (MANDATORY)

- [X] T068 [US3] Commit completed User Story 3 work with message: "feat(dsp): add random trigger mode and random sample source to SampleHoldFilter (US3)"

**Checkpoint**: All three trigger modes (Clock, Audio, Random) and two sample sources (LFO, Random) now functional. Processor supports generative/chaotic effects.

---

## Phase 6: User Story 4 - Multi-Parameter Sampling with Pan (Priority: P3)

**Goal**: Extend sampling to Q and pan parameters, creating multi-dimensional modulation with stereo field movement

**Independent Test**: Enable pan sampling in stereo mode, verify pan values affect L/R cutoff balance symmetrically, update only at sample points

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T069 [P] [US4] Write Q modulation tests: verify baseQ + (heldValue * qRange * (kMaxQ - kMinQ)) calculation and clamping (FR-012, FR-020)
- [X] T070 [P] [US4] Write pan modulation tests: verify symmetric L/R cutoff offset calculation using pow(2, panOffset) (FR-013)
- [X] T071 [P] [US4] Write stereo processing tests: verify processStereo updates both channels with pan-offset cutoffs
- [X] T072 [P] [US4] Write independent parameter source tests: verify cutoff/Q/pan can each have different sample sources (FR-014)
- [X] T073 [P] [US4] Write envelope sample source tests: verify Envelope source converts [0,1] to [-1,1] correctly (FR-009)
- [X] T074 [P] [US4] Write external sample source tests: verify External source uses user-provided value [0,1] converted to [-1,1] (FR-010)
- [X] T075 [P] [US4] Verify all User Story 4 tests FAIL (no Q/pan implementation yet)

### 6.2 Implementation for User Story 4

- [X] T076 [US4] Implement setQSamplingEnabled(), setQSource(), setQRange() and corresponding getters (FR-012, FR-014)
- [X] T077 [US4] Implement setPanSamplingEnabled(), setPanSource(), setPanOctaveRange() and corresponding getters (FR-013, FR-014)
- [X] T078 [US4] Implement setBaseQ() and getBaseQ(): clamp to [0.1, 30] (FR-020)
- [X] T079 [US4] Implement setExternalValue() and getExternalValue(): clamp to [0, 1] (FR-010)
- [X] T080 [US4] Update getSampleValue() helper: add Envelope case (envelopeFollower_.getCurrentValue() * 2 - 1) and External case (externalValue_ * 2 - 1)
- [X] T081 [US4] Update onTrigger(): add Q sampling if enabled, add pan sampling if enabled, update respective smoother targets
- [X] T082 [US4] Implement calculateFinalQ() helper: baseQ_ + (smoothedMod * qRange_ * (kMaxQ - kMinQ)), clamp to [kMinQ, kMaxQ] (FR-012)
- [X] T083 [US4] Implement calculateStereoCutoffs() helper: apply pan offset symmetrically to L/R cutoffs using pow(2, ±panOffset) (FR-013)
- [X] T084 [US4] Implement processStereo(float& left, float& right): mix L+R for trigger input, update trigger state, calculate stereo cutoffs with pan, update both SVF instances, process both channels (FR-022)
- [X] T085 [US4] Implement processBlockStereo(): loop calling processStereo() for each sample pair
- [X] T086 [US4] Update process(): add Q modulation calculation, update filterL_ resonance

### 6.3 Verification

- [X] T087 [US4] Build and run all tests, verify User Story 4 tests PASS
- [X] T088 [US4] Verify pan symmetry: pan=-1 makes L lower/R higher, pan=+1 makes L higher/R lower
- [X] T089 [US4] Verify independent parameter sources: each parameter can sample from different source simultaneously

### 6.4 Cross-Platform Verification (MANDATORY)

- [X] T090 [US4] Verify IEEE 754 compliance: check if new tests use NaN/Inf detection, update CMakeLists.txt if needed

### 6.5 Commit (MANDATORY)

- [X] T091 [US4] Commit completed User Story 4 work with message: "feat(dsp): add Q/pan parameter sampling and stereo processing to SampleHoldFilter (US4)"

**Checkpoint**: All three sampleable parameters (cutoff, Q, pan) functional with independent source selection. Stereo processing with spatial modulation working.

---

## Phase 7: User Story 5 - Smooth Stepped Transitions with Slew (Priority: P3)

**Goal**: Add slew limiting to smooth transitions between held values, eliminating clicks while maintaining rhythmic stepped character

**Independent Test**: Measure transition time between held values with slew enabled vs instant transitions with slew=0, verify 99% settling within slew time +/- 10%

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T092 [P] [US5] Write slew timing tests: verify transitions reach 99% of target within configured slew time +/- 10% (SC-003)
- [X] T093 [P] [US5] Write instant transition tests: verify slew=0 produces instant changes (no smoothing)
- [X] T094 [P] [US5] Write slew scope tests: verify base parameter changes are instant, only sampled modulation values are slewed (FR-015)
- [X] T095 [P] [US5] Write slew redirect tests: verify when slew time > hold time (e.g., slew=100ms, hold=50ms), smoother redirects to new target smoothly without discontinuities; verify SC-003 compliance in this edge case
- [X] T096 [P] [US5] Write click elimination tests: verify no audible discontinuities when slew > 0 (SC-006)
- [X] T097 [P] [US5] Verify all User Story 5 tests FAIL (smoothers not yet configured)

### 7.2 Implementation for User Story 5

- [X] T098 [US5] Implement setSlewTime() and getSlewTime(): clamp to [0, 500], configure all three ParameterState smoothers (FR-015)
- [X] T099 [US5] Update prepare(): configure all three smoothers with slewTimeMs and sample rate using OnePoleSmoother::configure() (FR-016)
- [X] T100 [US5] Update reset(): snap all three smoothers to 0.0 using snapTo() (held values initialize to base, no modulation offset)
- [X] T101 [US5] Update calculateFinalCutoff(): call cutoffState_.smoother.process() to get smoothed modulation value
- [X] T102 [US5] Update calculateFinalQ(): call qState_.smoother.process() to get smoothed modulation value
- [X] T103 [US5] Update calculateStereoCutoffs(): call panState_.smoother.process() to get smoothed pan value
- [X] T104 [US5] Verify OnePoleSmoother usage: setTarget() updates target, process() advances smoother, snapTo() for instant changes

### 7.3 Verification

- [X] T105 [US5] Build and run all tests, verify User Story 5 tests PASS
- [X] T106 [US5] Verify SC-003: Slew transitions reach 99% within slew time +/- 10%
- [X] T107 [US5] Verify SC-006: No audible clicks when slew > 0 (measure sample discontinuities)
- [X] T108 [US5] Verify base parameter instant: changing baseCutoff while processing updates immediately without slew

### 7.4 Cross-Platform Verification (MANDATORY)

- [X] T109 [US5] Verify IEEE 754 compliance: check if new tests use NaN/Inf detection, update CMakeLists.txt if needed

### 7.5 Commit (MANDATORY)

- [X] T110 [US5] Commit completed User Story 5 work with message: "feat(dsp): add slew limiting for smooth transitions in SampleHoldFilter (US5)"

**Checkpoint**: All user stories complete! Processor supports all three trigger modes, four sample sources, three sampleable parameters, stereo processing, and smooth slew-limited transitions.

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Edge case handling, performance verification, and final polish

### 8.1 Edge Case Handling

- [X] T111 [P] Write tests for hold time vs buffer size boundaries: verify accurate timing when hold time < buffer size, > buffer size, and hold events that span multiple buffers maintain sample-accurate timing (FR-024)
- [X] T112 [P] Write tests for very fast hold times: verify minimum 0.1ms clamping prevents aliasing artifacts
- [X] T113 [P] Write tests for multiple transients within hold time: verify only first transient triggers, subsequent ignored
- [X] T114 [P] Write tests for sample source switching during hold: verify new source value sampled on next trigger
- [X] T115 [P] Write tests for invalid input handling: NaN/Inf input should reset filter state and return 0
- [X] T116 Implement edge case handling based on test requirements

### 8.2 Performance Verification

- [X] T117 Write CPU usage benchmark test: measure processing time per sample at 44.1kHz stereo
- [X] T118 Verify SC-004: CPU usage < 0.5% per instance at 44.1kHz stereo
- [X] T119 Profile hot paths: verify no unexpected allocations, optimal branch prediction

### 8.3 Final Code Quality

- [X] T120 [P] Code review: verify all noexcept annotations correct, no missing const qualifiers
- [X] T121 [P] Add Doxygen documentation comments for all public methods
- [X] T122 [P] Verify all FR requirements have corresponding test coverage
- [X] T123 [P] Run clang-tidy/static analysis if available
- [X] T124 Verify zero compiler warnings on Release build

### 8.4 Commit Polish Work

- [X] T125 Commit edge case handling and performance work with message: "refactor(dsp): add edge case handling and performance optimizations to SampleHoldFilter"

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [X] T126 Update specs/_architecture_/layer-2-processors.md: add SampleHoldFilter entry with purpose, public API summary, file location, usage examples
- [X] T127 Verify no duplicate functionality introduced: check for similar filter modulators, document differences
- [X] T128 Add cross-references to existing components: SVF (Layer 1), LFO (Layer 1), OnePoleSmoother (Layer 1), EnvelopeFollower (Layer 2), Xorshift32 (Layer 0)

### 9.2 Final Commit

- [X] T129 Commit architecture documentation updates with message: "docs(arch): add SampleHoldFilter to Layer 2 processors documentation"
- [X] T130 Verify all spec work committed to feature branch: git log --oneline should show all user story commits

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T131 Review ALL 27 FR-xxx requirements from spec.md against implementation: FR-001 through FR-027
- [X] T132 Review ALL 7 SC-xxx success criteria and verify measurable targets achieved: SC-001 through SC-007
- [X] T133 Search for cheating patterns in implementation:
  - [X] No `// placeholder` or `// TODO` comments in dsp/include/krate/dsp/processors/sample_hold_filter.h
  - [X] No test thresholds relaxed from spec requirements (SC-001: 1 sample accuracy, SC-003: 99% within time, SC-007: 45-55% probability)
  - [X] No features quietly removed from scope (all 3 triggers, 4 sources, 3 parameters, stereo, slew)

### 10.2 Fill Compliance Table in spec.md

- [X] T134 Update spec.md "Implementation Verification" section with compliance status (MET/NOT MET/PARTIAL/DEFERRED) for each FR and SC requirement
- [X] T135 Mark overall status honestly in spec.md: COMPLETE / NOT COMPLETE / PARTIAL with justification

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T136 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Build Verification

- [X] T137 Clean build from scratch: cmake --build build/windows-x64-release --config Release --target dsp_tests
- [X] T138 Run complete test suite: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe
- [X] T139 Verify all tests pass with no warnings

### 11.2 Completion Claim

- [X] T140 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)
- [X] T141 Final commit with message: "feat(dsp): complete SampleHoldFilter implementation (#089)"

**Checkpoint**: Spec implementation honestly complete. Ready for pull request.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Foundational phase completion
  - User Story 1 (P1): No dependencies on other stories - START HERE for MVP
  - User Story 2 (P2): Can start after Foundational - extends trigger system
  - User Story 3 (P2): Can start after Foundational - extends trigger and sample sources
  - User Story 4 (P3): Can start after Foundational - extends to Q/pan parameters
  - User Story 5 (P3): Depends on User Story 4 completion (needs Q/pan smoothers)
- **Polish (Phase 8)**: Depends on all user stories being complete
- **Documentation (Phase 9)**: Depends on all implementation complete
- **Verification (Phase 10-11)**: Final phase

### User Story Dependencies

```
Foundational (Phase 2)
         |
         +-- User Story 1 (P1) [Clock + LFO cutoff] <-- MVP START HERE
         |
         +-- User Story 2 (P2) [Audio trigger] (can start after Foundational)
         |
         +-- User Story 3 (P2) [Random trigger/source] (can start after Foundational)
         |
         +-- User Story 4 (P3) [Q/pan parameters, stereo] (can start after Foundational)
                  |
                  +-- User Story 5 (P3) [Slew limiting] (depends on US4 smoothers)
```

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation follows test structure
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

**Phase 1 (Setup)**: All 3 tasks can run in parallel

**Phase 2 (Foundational)**: Tasks T004-T010 are sequential (class skeleton depends on enums, members depend on skeleton)

**User Story 1 Tests**: All test tasks T011-T016 marked [P] can be written in parallel (different test sections)

**User Story 1 Implementation**: T017-T020 (lifecycle), T021-T023 (configuration), T024-T027 (helpers) can partially overlap if careful with dependencies

**User Story 2-5**: Once Foundational complete, User Stories 1-4 can start in parallel if multiple developers available (Story 5 must wait for Story 4)

**Polish Phase 8**: Most tasks marked [P] can run in parallel (different test cases, different concerns)

---

## Parallel Example: User Story 1

If multiple developers work on User Story 1 simultaneously:

```bash
# Developer A: Write lifecycle tests (T011)
# Developer B: Write clock trigger tests (T012)
# Developer C: Write LFO sample tests (T013)
# Developer D: Write cutoff modulation tests (T014)
# Developer E: Write processing tests (T015)

# After all tests written and verified to FAIL:

# Developer A: Lifecycle implementation (T017-T018)
# Developer B: Configuration setters/getters (T019-T023)
# Developer C: Trigger/sampling helpers (T024-T026)
# Developer D: Modulation calculation (T027)
# Developer E: Process methods (T028-T030)

# All converge for verification (T031-T034)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

**Minimum Viable Processor**: Clock-synced stepped filter with LFO modulation

1. Complete Phase 1: Setup (verify dependencies, create test structure)
2. Complete Phase 2: Foundational (enums, class skeleton) - CRITICAL
3. Complete Phase 3: User Story 1 (clock trigger + LFO cutoff sampling)
4. **STOP and VALIDATE**: Test US1 independently with various hold times and LFO rates
5. Delivers core S&H functionality immediately

**Estimated Effort**: ~60-80 tasks (T001-T036)

### Incremental Delivery

After MVP, add capabilities incrementally:

1. **MVP (US1)**: Clock-synced LFO cutoff modulation → Rhythmic stepped filter
2. **+US2**: Audio trigger → Drum-reactive filter sweeps
3. **+US3**: Random trigger/source → Generative chaotic effects
4. **+US4**: Q/pan parameters + stereo → Multi-dimensional spatial modulation
5. **+US5**: Slew limiting → Smooth transitions without clicks

Each increment adds creative possibilities without breaking previous functionality.

### Parallel Team Strategy

With 2-3 developers after Foundational phase completes:

- **Developer A**: User Story 1 (T011-T036) - MVP critical path
- **Developer B**: User Story 2 (T037-T052) - Audio trigger expansion
- **Developer C**: User Story 3 (T053-T068) - Random trigger expansion

After US1-3 complete:
- **Developer A**: User Story 4 (T069-T091) - Q/pan parameters
- **Developer B**: Prepare Polish work (T111-T116)

After US4 complete:
- **Developer A**: User Story 5 (T092-T110) - Slew limiting

All converge for final documentation and verification.

---

## Notes

- **[P] markers**: Tasks that can run in parallel (different files or independent concerns)
- **[Story] labels**: Map each task to specific user story for traceability and progress tracking
- **MVP scope**: User Story 1 only delivers core S&H functionality immediately
- **Independent stories**: Each user story (except US5) can be implemented and tested independently after Foundational phase
- **Test-first is mandatory**: Write tests that FAIL before implementing (Constitution Principle XII)
- **Cross-platform checks**: Add test files using IEEE 754 functions to `-fno-fast-math` list in CMakeLists.txt
- **Commit discipline**: Commit at end of each user story to preserve working increments
- **Header-only implementation**: All code in dsp/include/krate/dsp/processors/sample_hold_filter.h (no .cpp file)
- **Layer 2 processor**: Depends on Layers 0-1 primitives (SVF, LFO, OnePoleSmoother, Xorshift32, EnvelopeFollower)
- **Real-time safety**: All process methods noexcept, zero allocations, no locks
- **Determinism**: Seed-based reproducibility for testing and creative workflows
- **Honest verification**: Do not claim completion unless ALL FR/SC requirements are MET or explicitly deferred

---

## Total Task Count: 141 tasks

### Breakdown by Phase:
- Phase 1 (Setup): 3 tasks
- Phase 2 (Foundational): 7 tasks (BLOCKS all user stories)
- Phase 3 (User Story 1 - P1): 26 tasks 🎯 MVP
- Phase 4 (User Story 2 - P2): 16 tasks
- Phase 5 (User Story 3 - P2): 16 tasks
- Phase 6 (User Story 4 - P3): 23 tasks
- Phase 7 (User Story 5 - P3): 19 tasks
- Phase 8 (Polish): 15 tasks
- Phase 9 (Documentation): 5 tasks
- Phase 10 (Verification): 6 tasks
- Phase 11 (Completion): 5 tasks

### MVP Scope (Minimum Viable Processor):
**Tasks T001-T036 (36 tasks)** = Setup + Foundational + User Story 1

Delivers clock-synchronized stepped filter with LFO cutoff modulation - the core S&H behavior that defines this processor.

### Parallel Opportunities:
- 35 tasks marked [P] can run in parallel within their phase
- User Stories 1-4 can be developed in parallel after Foundational phase (US5 depends on US4)
- Test writing within each story is highly parallelizable

### Critical Path:
Setup → Foundational → US1 → US4 → US5 → Polish → Documentation → Verification
(US2 and US3 can be on parallel paths after Foundational)
