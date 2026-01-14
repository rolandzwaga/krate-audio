# Tasks: FuzzProcessor

**Input**: Design documents from `/specs/063-fuzz-processor/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), data-model.md, contracts/fuzz_processor_api.h

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

- [X] T001 Create test file skeleton at `dsp/tests/unit/processors/fuzz_processor_test.cpp` with Catch2 includes and test tags `[fuzz_processor]`
- [X] T002 Create header file skeleton at `dsp/include/krate/dsp/processors/fuzz_processor.h` with namespace `Krate::DSP`, includes, and empty class declaration
- [X] T003 Verify build system picks up new files: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Enumeration and Constants (FR-001)

- [X] T004 [P] Write failing tests for `FuzzType` enum (Germanium=0, Silicon=1) in `dsp/tests/unit/processors/fuzz_processor_test.cpp`
- [X] T005 [P] Write failing tests for class constants: kDefaultFuzz, kDefaultVolumeDb, kDefaultBias, kDefaultTone, kMinVolumeDb, kMaxVolumeDb, kSmoothingTimeMs, kCrossfadeTimeMs, kDCBlockerCutoffHz, kToneMinHz, kToneMaxHz, kSagAttackMs, kSagReleaseMs
- [X] T006 Implement `FuzzType` enum and class constants in `dsp/include/krate/dsp/processors/fuzz_processor.h`
- [X] T007 Verify enum and constant tests pass

### 2.2 Default Constructor and Getters (FR-005, FR-011 to FR-015)

- [X] T008 Write failing tests for default constructor: type=Germanium, fuzz=0.5, volume=0dB, bias=0.7, tone=0.5, octaveUp=false
- [X] T009 Write failing tests for getters: `getFuzzType()`, `getFuzz()`, `getVolume()`, `getBias()`, `getTone()`, `getOctaveUp()`
- [X] T010 Implement FuzzProcessor class with default constructor and getter methods in `dsp/include/krate/dsp/processors/fuzz_processor.h`
- [X] T011 Verify constructor and getter tests pass

### 2.3 Parameter Setters with Clamping (FR-006, FR-007 to FR-010, FR-050)

- [X] T012 Write failing tests for `setFuzzType(FuzzType)` setter
- [X] T013 Write failing tests for `setFuzz(float)` with clamping to [0.0, 1.0]
- [X] T014 Write failing tests for `setVolume(float dB)` with clamping to [-24, +24]
- [X] T015 Write failing tests for `setBias(float)` with clamping to [0.0, 1.0]
- [X] T016 Write failing tests for `setTone(float)` with clamping to [0.0, 1.0]
- [X] T017 Write failing tests for `setOctaveUp(bool)` setter
- [X] T018 Implement all parameter setters with clamping in `dsp/include/krate/dsp/processors/fuzz_processor.h`
- [X] T019 Verify setter tests pass

### 2.4 Lifecycle Methods (FR-002, FR-003, FR-004)

- [X] T020 Write failing tests for `prepare(double sampleRate, size_t maxBlockSize)` - configures filters and smoothers
- [X] T021 Write failing tests for `reset()` - clears filter state, snaps smoothers to targets (FR-040)
- [X] T022 Write failing tests for process() before prepare() returns input unchanged (FR-004)
- [X] T023 Implement `prepare()` and `reset()` in `dsp/include/krate/dsp/processors/fuzz_processor.h`
- [X] T024 Verify lifecycle tests pass

### 2.5 Foundational Commit

- [X] T025 Verify all foundational tests pass
- [ ] T026 **Commit completed Foundational phase work**

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Germanium Fuzz (Priority: P1)

**Goal**: DSP developer applies warm, saggy Germanium fuzz with soft clipping and even harmonics

**Independent Test**: Process audio through Germanium mode and verify soft clipping characteristics with even harmonic content (SC-002)

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T027 [P] [US1] Write failing tests for Germanium soft clipping using `Asymmetric::tube()` (FR-016, FR-018)
- [X] T028 [P] [US1] Write failing tests for Germanium even harmonic content (2nd, 4th harmonics visible) (SC-002)
- [X] T029 [P] [US1] Write failing tests for Germanium sag envelope follower: 1ms attack, 100ms release (FR-017)
- [X] T030 [P] [US1] Write failing tests for sag behavior: loud signals dynamically lower clipping threshold (FR-017)
- [X] T031 [P] [US1] Write failing tests for fuzz amount: fuzz=0.0 is near-clean (THD < 1%), fuzz=1.0 is heavily saturated (SC-008)

### 3.2 Implementation for User Story 1

- [X] T032 [US1] Implement Germanium Waveshaper with WaveshapeType::Tube in `dsp/include/krate/dsp/processors/fuzz_processor.h`
- [X] T033 [US1] Implement sag envelope follower state variables (sagEnvelope_, sagAttackCoeff_, sagReleaseCoeff_) in `dsp/include/krate/dsp/processors/fuzz_processor.h`
- [X] T034 [US1] Implement sag coefficient calculation in `prepare()` based on kSagAttackMs and kSagReleaseMs
- [X] T035 [US1] Implement Germanium saturation path with dynamic threshold modulation in `process()`
- [X] T036 [US1] Verify all Germanium saturation tests pass

### 3.3 Integration Tests for User Story 1

- [X] T037 [US1] Write integration test: Germanium produces both even and odd harmonics (SC-002)
- [X] T038 [US1] Write integration test: Germanium has "saggy" character - louder input = more compression
- [X] T039 [US1] Write integration test: n=0 is handled gracefully (FR-032)
- [X] T040 [US1] Verify integration tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T041 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 3.5 Commit (MANDATORY)

- [ ] T042 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Silicon Fuzz (Priority: P1)

**Goal**: DSP developer uses bright, tight Silicon fuzz with harder clipping and odd harmonics

**Independent Test**: Process audio through Silicon mode and verify harder clipping with predominantly odd harmonics (SC-003)

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T043 [P] [US2] Write failing tests for Silicon hard clipping using `Sigmoid::tanh()` (FR-019, FR-021)
- [X] T044 [P] [US2] Write failing tests for Silicon predominantly odd harmonic content (3rd, 5th harmonics dominant) (SC-003)
- [X] T045 [P] [US2] Write failing tests for Silicon tighter, more consistent clipping threshold vs Germanium (FR-020)
- [X] T046 [P] [US2] Write failing tests for Germanium vs Silicon measurably different outputs (SC-001)

### 4.2 Implementation for User Story 2

- [X] T047 [US2] Implement Silicon Waveshaper with WaveshapeType::Tanh in `dsp/include/krate/dsp/processors/fuzz_processor.h`
- [X] T048 [US2] Implement Silicon saturation path with fixed threshold (no sag) in `process()`
- [X] T049 [US2] Verify all Silicon saturation tests pass

### 4.3 Integration Tests for User Story 2

- [X] T050 [US2] Write integration test: Silicon and Germanium produce measurably different harmonic spectra (SC-001)
- [X] T051 [US2] Write integration test: Silicon has tighter, more aggressive character with faster attack
- [X] T052 [US2] Verify integration tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T053 [US2] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 4.5 Commit (MANDATORY)

- [ ] T054 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Bias Control (Priority: P2)

**Goal**: DSP developer achieves "dying battery" sputtery gating effect by adjusting bias control

**Independent Test**: Sweep bias from 0.0 to 1.0 and measure effect on signal gating (SC-009)

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T055 [P] [US3] Write failing tests for bias=1.0 (normal) produces full sustain output (FR-024)
- [X] T056 [P] [US3] Write failing tests for bias=0.2 (low) creates gating where signals below -20dBFS are attenuated by at least 6dB (SC-009)
- [X] T057 [P] [US3] Write failing tests for bias=0.0 (extreme) creates maximum gating (FR-023)
- [X] T058 [P] [US3] Write failing tests for bias as DC offset added before waveshaping (FR-025)

### 5.2 Implementation for User Story 3

- [X] T059 [US3] Implement bias gating calculation: gateThreshold = (1.0 - bias) * 0.2 in `dsp/include/krate/dsp/processors/fuzz_processor.h`
- [X] T060 [US3] Implement gating factor: min(1.0, abs(input) / gateThreshold) applied to output
- [X] T061 [US3] Verify all bias control tests pass

### 5.3 Integration Tests for User Story 3

- [X] T062 [US3] Write integration test: bias change during processing is smoothed without clicks (SC-004)
- [X] T063 [US3] Write integration test: bias=0.2 shows measurable gating effect (SC-009)
- [X] T064 [US3] Verify integration tests pass

### 5.4 Commit (MANDATORY)

- [ ] T065 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Bias control fully functional

---

## Phase 6: User Story 4 - Fuzz Amount Control (Priority: P2)

**Goal**: DSP developer controls fuzz intensity from subtle warmth to full saturation

**Independent Test**: Sweep fuzz from 0.0 to 1.0 and measure increase in saturation and harmonic content (SC-008)

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T066 [P] [US4] Write failing tests for fuzz=0.0 produces minimal distortion (THD < 1% for moderate input) (SC-008)
- [X] T067 [P] [US4] Write failing tests for fuzz=1.0 produces heavily saturated output with rich harmonics
- [X] T068 [P] [US4] Write failing tests for fuzz increases saturation proportionally from 0.0 to 1.0

### 6.2 Implementation for User Story 4

- [X] T069 [US4] Implement fuzz amount mapping to drive gain: driveGain = 1.0 + fuzz * 9.0 (1x to 10x)
- [X] T070 [US4] Verify fuzz control tests pass

### 6.3 Commit (MANDATORY)

- [ ] T071 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Fuzz amount control fully functional

---

## Phase 7: User Story 5 - Tone Control (Priority: P2)

**Goal**: DSP developer tames harsh high frequencies with tone control low-pass filter

**Independent Test**: Measure frequency response at different tone settings (SC-010)

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T072 [P] [US5] Write failing tests for tone=0.0 sets filter cutoff to 400Hz (dark/muffled) (FR-027)
- [X] T073 [P] [US5] Write failing tests for tone=1.0 sets filter cutoff to 8000Hz (bright/open) (FR-028)
- [X] T074 [P] [US5] Write failing tests for tone sweep shows 12dB frequency response change at 4kHz (SC-010)
- [X] T075 [P] [US5] Write failing tests for tone filter uses Biquad Lowpass (FR-026, FR-029, FR-043)

### 7.2 Implementation for User Story 5

- [X] T076 [US5] Implement tone filter using Biquad with FilterType::Lowpass in `dsp/include/krate/dsp/processors/fuzz_processor.h`
- [X] T077 [US5] Implement tone cutoff mapping: cutoff = 400 + tone * 7600 Hz
- [X] T078 [US5] Implement tone filter configuration in `prepare()` and update in `process()` loop
- [X] T079 [US5] Verify tone control tests pass

### 7.3 Integration Tests for User Story 5

- [X] T080 [US5] Write integration test: tone=0.5 produces neutral cutoff around 4200Hz
- [X] T081 [US5] Write integration test: tone filter is smoothly reconfigured during processing (FR-039)
- [X] T082 [US5] Verify integration tests pass

### 7.4 Commit (MANDATORY)

- [ ] T083 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Tone control fully functional

---

## Phase 8: User Story 6 - Volume Control (Priority: P3)

**Goal**: DSP developer matches output level to input level or boosts signal

**Independent Test**: Measure output level at different volume settings

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T084 [P] [US6] Write failing tests for volume=0dB maintains saturated signal level
- [X] T085 [P] [US6] Write failing tests for volume=+6dB boosts output by 6dB
- [X] T086 [P] [US6] Write failing tests for volume=-12dB attenuates output by 12dB
- [X] T087 [P] [US6] Write failing tests for volume clamping to [-24, +24] dB (FR-008)

### 8.2 Implementation for User Story 6

- [X] T088 [US6] Implement volume gain using dbToGain() from Layer 0 in `dsp/include/krate/dsp/processors/fuzz_processor.h`
- [X] T089 [US6] Verify volume control tests pass

### 8.3 Commit (MANDATORY)

- [ ] T090 [US6] **Commit completed User Story 6 work**

**Checkpoint**: Volume control fully functional

---

## Phase 9: Octave-Up Mode (Cross-Cutting Concern)

**Goal**: Enable octave-up effect via self-modulation (input * |input|) before fuzz stage (FR-050 to FR-053)

**Independent Test**: Process sine wave with octave-up enabled and verify 2nd harmonic content (SC-011)

### 9.1 Tests for Octave-Up (Write FIRST - Must FAIL)

- [X] T091 [P] Write failing tests for octave-up self-modulation: output = input * abs(input) (FR-052)
- [X] T092 [P] Write failing tests for octave-up applied BEFORE main fuzz stage (FR-053)
- [X] T093 [P] Write failing tests for octave-up produces measurable 2nd harmonic from sine wave (SC-011)
- [X] T094 [P] Write failing tests for octaveUp=false bypasses self-modulation

### 9.2 Implementation for Octave-Up

- [X] T095 Implement octave-up self-modulation at start of sample processing loop in `dsp/include/krate/dsp/processors/fuzz_processor.h`
- [X] T096 Verify octave-up tests pass

### 9.3 Commit (MANDATORY)

- [ ] T097 **Commit completed Octave-Up work**

**Checkpoint**: Octave-up mode fully functional

---

## Phase 10: DC Blocking (Cross-Cutting Concern)

**Goal**: Remove DC offset introduced by bias and asymmetric saturation (FR-033 to FR-035)

**Independent Test**: Verify DC offset after processing is below -50dBFS (SC-006)

### 10.1 Tests for DC Blocking (Write FIRST - Must FAIL)

- [X] T098 [P] Write failing tests for DC blocker at 10Hz cutoff (FR-034)
- [X] T099 [P] Write failing tests for DC blocker uses DCBlocker from Layer 1 (FR-035, FR-042)
- [X] T100 [P] Write failing tests for DC offset below -50dBFS for any input with non-zero bias (SC-006)
- [X] T101 [P] Write failing tests for DC input signal produces output settling to zero

### 10.2 Implementation for DC Blocking

- [X] T102 Implement DCBlocker configuration in `prepare()` with kDCBlockerCutoffHz in `dsp/include/krate/dsp/processors/fuzz_processor.h`
- [X] T103 Implement DC blocking after saturation stage in `process()` loop
- [X] T104 Verify DC blocking tests pass

### 10.3 Commit (MANDATORY)

- [ ] T105 **Commit completed DC Blocking work**

**Checkpoint**: DC blocking fully functional

---

## Phase 11: Parameter Smoothing (Cross-Cutting Concern)

**Goal**: Smooth parameter changes to prevent clicks (FR-036 to FR-040)

**Independent Test**: Rapidly change parameters and verify no discontinuities in output (SC-004)

### 11.1 Tests for Parameter Smoothing (Write FIRST - Must FAIL)

- [X] T106 [P] Write failing tests for fuzz smoothing (5ms target, <10ms completion) (FR-036)
- [X] T107 [P] Write failing tests for volume smoothing (FR-037)
- [X] T108 [P] Write failing tests for bias smoothing (FR-038)
- [X] T109 [P] Write failing tests for tone smoothing (FR-039)
- [X] T110 [P] Write failing tests for reset() snaps smoothers to current target values (FR-040)

### 11.2 Implementation for Parameter Smoothing

- [X] T111 Add OnePoleSmoother instances for fuzz, volume, bias, tone in `dsp/include/krate/dsp/processors/fuzz_processor.h`
- [X] T112 Configure smoothers in `prepare()` with kSmoothingTimeMs (5ms)
- [X] T113 Implement per-sample smoothing in `process()` loop using smoother.process()
- [X] T114 Implement smoother snapToTarget() in `reset()`
- [X] T115 Verify all smoothing tests pass

### 11.3 Integration Tests for Parameter Smoothing

- [X] T116 Write integration test: rapid fuzz change produces no clicks (SC-004)
- [X] T117 Write integration test: parameter changes complete within 10ms (SC-004)
- [X] T118 Verify integration tests pass

### 11.4 Commit (MANDATORY)

- [ ] T119 **Commit completed Parameter Smoothing work**

**Checkpoint**: Parameter smoothing fully functional

---

## Phase 12: Type Crossfade (Cross-Cutting Concern)

**Goal**: Click-free type switching via 5ms equal-power crossfade (FR-006a)

**Independent Test**: Switch types during processing and verify no audible discontinuities (SC-004)

### 12.1 Tests for Type Crossfade (Write FIRST - Must FAIL)

- [ ] T120 [P] Write failing tests for crossfade activation on type change in setFuzzType()
- [ ] T121 [P] Write failing tests for 5ms crossfade duration using `crossfadeIncrement()` from Layer 0
- [ ] T122 [P] Write failing tests for equal-power crossfade using `equalPowerGains()` from Layer 0
- [ ] T123 [P] Write failing tests for crossfade completes without clicks

### 12.2 Implementation for Type Crossfade

- [ ] T124 Implement crossfade state variables: crossfadeActive_, crossfadePosition_, crossfadeIncrement_, previousType_
- [ ] T125 Implement crossfade trigger in `setFuzzType()` when type changes
- [ ] T126 Implement parallel type processing during crossfade: process both Germanium and Silicon, blend outputs
- [ ] T127 Implement crossfade completion detection and cleanup
- [ ] T128 Verify crossfade tests pass

### 12.3 Commit (MANDATORY)

- [ ] T129 **Commit completed Type Crossfade work**

**Checkpoint**: Type switching is click-free

---

## Phase 13: CPU Benchmarks (Success Criteria Verification)

**Goal**: Verify CPU budget compliance per SC-005 (< 0.5% CPU @ 44.1kHz/2.5GHz)

**Independent Test**: Measure cycles/sample at 512-sample blocks, normalize to 2.5GHz baseline

### 13.1 Benchmark Tests

- [ ] T130 Write benchmark test for FuzzProcessor: verify < 0.5% CPU (SC-005)
- [ ] T131 Write benchmark test comparing Germanium vs Silicon CPU costs
- [ ] T132 Write benchmark test with octave-up enabled vs disabled
- [ ] T133 Run benchmarks and document results in test output

**Benchmark Implementation Notes (Cross-Platform):**
- Windows: Use `__rdtsc()` for cycle counting
- macOS: Use `mach_absolute_time()` for high-resolution timing
- Linux: Use `clock_gettime(CLOCK_MONOTONIC)` for nanosecond precision
- Normalize: Convert to cycles/sample, assume 2.5GHz baseline for percentage calculation
- Test conditions: 512 samples, 44.1kHz, mono, default parameters

### 13.2 Commit (MANDATORY)

- [ ] T134 **Commit completed Benchmark work**

**Checkpoint**: CPU budget compliance verified

---

## Phase 14: Multi-Sample-Rate Tests (Success Criteria Verification)

**Goal**: Verify processor works across all supported sample rates (SC-007)

**Independent Test**: Run all unit tests at 44.1kHz, 48kHz, 88.2kHz, 96kHz, 192kHz

### 14.1 Sample Rate Tests

- [ ] T135 Write parameterized tests for sample rates: 44.1kHz, 48kHz, 88.2kHz, 96kHz, 192kHz (SC-007)
- [ ] T136 Verify tone filter cutoff scales appropriately at high sample rates (192kHz edge case)
- [ ] T137 Verify sag envelope coefficients are recalculated correctly for each sample rate
- [ ] T138 Verify crossfade duration is consistent across sample rates
- [ ] T139 Run multi-sample-rate tests and verify all pass

### 14.2 Commit (MANDATORY)

- [ ] T140 **Commit completed Multi-Sample-Rate work**

**Checkpoint**: Sample rate independence verified

---

## Phase 15: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T141 Write test verifying denormal inputs produce valid outputs without CPU spike (flushDenormal usage)
- [ ] T142 Add Doxygen documentation to FuzzProcessor class and all public methods (FR-048)
- [ ] T143 Verify naming conventions: trailing underscore for members, PascalCase for class, camelCase for methods (FR-049)
- [ ] T144 Verify all includes use `<krate/dsp/...>` pattern for Layer 0/1 dependencies (FR-047)
- [ ] T145 Verify namespace is `Krate::DSP` (FR-046)
- [ ] T146 Verify header-only implementation (FR-045)
- [ ] T147 Code cleanup: remove any unused code, ensure consistent formatting
- [ ] T148 Run full test suite: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure`

---

## Phase 16: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 16.1 Architecture Documentation Update

- [ ] T149 **Update `specs/_architecture_/layer-2-processors.md`** with FuzzProcessor entry:
  - Purpose: Fuzz Face style distortion with Germanium (warm, saggy) and Silicon (bright, tight) transistor types
  - Public API summary: prepare(), reset(), process(), setFuzzType/Fuzz/Volume/Bias/Tone/OctaveUp, getters
  - File location: `dsp/include/krate/dsp/processors/fuzz_processor.h`
  - When to use: Guitar fuzz effects, vintage distortion, octave-up effects, gating/sputtery "dying battery" sounds

### 16.2 Final Commit

- [ ] T150 **Commit architecture documentation updates**
- [ ] T151 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 17: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 17.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T152 **Review ALL FR-xxx requirements** from spec.md against implementation (FR-001 through FR-053)
- [ ] T153 **Review ALL SC-xxx success criteria** and verify measurable targets are achieved (SC-001 through SC-011)
- [ ] T154 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 17.2 Fill Compliance Table in spec.md

- [ ] T155 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T156 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 17.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T157 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 18: Final Completion

**Purpose**: Final commit and completion claim

### 18.1 Final Commit

- [ ] T158 **Commit all spec work** to feature branch
- [ ] T159 **Verify all tests pass**

### 18.2 Completion Claim

- [ ] T160 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

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
    +---> Phase 3 (US1: Germanium Fuzz) [P1]
    |         |
    |         v
    +---> Phase 4 (US2: Silicon Fuzz) [P1] -- can run parallel with US1
    |         |
    |         v
    +---> Phase 5 (US3: Bias Control) [P2] -- depends on US1 or US2
    |         |
    |         v
    +---> Phase 6 (US4: Fuzz Amount) [P2]
    |         |
    |         v
    +---> Phase 7 (US5: Tone Control) [P2]
    |         |
    |         v
    +---> Phase 8 (US6: Volume Control) [P3]
    |
    v
Phase 9-12 (Cross-Cutting: Octave-Up, DC Blocking, Smoothing, Crossfade)
    |
    v
Phase 13-14 (CPU Benchmarks, Multi-Sample-Rate Tests)
    |
    v
Phase 15-18 (Polish, Docs, Verification, Completion)
```

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P1)**: Can start after Foundational (Phase 2) - Can run parallel with US1
- **User Story 3 (P2)**: Depends on at least one fuzz type (US1 or US2) being functional
- **User Story 4 (P2)**: Can start after Foundational - fuzz amount affects both types
- **User Story 5 (P2)**: Can start after Foundational - independent tone filter logic
- **User Story 6 (P3)**: Can start after Foundational - independent volume logic

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
- User Stories 1 and 2 can run in parallel (both P1, Germanium and Silicon are independent)
- User Stories 4, 5, 6 can run in parallel after Foundational (independent parameters)
- All tests for a user story marked [P] can run in parallel
- Cross-cutting phases should run sequentially after user stories

---

## Parallel Example: User Stories 1 & 2

```bash
# Launch US1 and US2 tests in parallel (both P1 priority):

# US1 tests (Germanium fuzz):
Task: T027 "Write failing tests for Germanium soft clipping"
Task: T028 "Write failing tests for Germanium even harmonic content"
Task: T029 "Write failing tests for Germanium sag envelope follower"

# US2 tests (Silicon fuzz) - IN PARALLEL:
Task: T043 "Write failing tests for Silicon hard clipping"
Task: T044 "Write failing tests for Silicon odd harmonic content"
Task: T045 "Write failing tests for Silicon tighter clipping threshold"
```

---

## Implementation Strategy

### MVP First (User Stories 1 & 2)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (Germanium Fuzz)
4. Complete Phase 4: User Story 2 (Silicon Fuzz)
5. **STOP and VALIDATE**: Test both fuzz types independently
6. Deploy/demo if ready - core fuzz functionality works

### Incremental Delivery

1. Setup + Foundational -> Foundation ready
2. Add User Story 1 (Germanium) -> Test independently -> Warm, saggy fuzz works
3. Add User Story 2 (Silicon) -> Test independently -> Both fuzz types work
4. Add User Stories 3-6 -> Test independently -> Full parameter control
5. Add Cross-Cutting (9-12) -> Octave-up, DC blocking, smoothing, crossfade
6. Each increment adds value without breaking previous functionality

---

## Summary

| Metric | Value |
|--------|-------|
| **Total Tasks** | 160 |
| **Phase 1 (Setup)** | 3 tasks |
| **Phase 2 (Foundational)** | 23 tasks |
| **User Story 1 (Germanium)** | 16 tasks |
| **User Story 2 (Silicon)** | 12 tasks |
| **User Story 3 (Bias)** | 11 tasks |
| **User Story 4 (Fuzz Amount)** | 6 tasks |
| **User Story 5 (Tone)** | 12 tasks |
| **User Story 6 (Volume)** | 7 tasks |
| **Cross-Cutting (Octave, DC, Smoothing, Crossfade)** | 32 tasks |
| **Benchmarks + Sample Rates** | 11 tasks |
| **Polish + Docs + Verification** | 20 tasks |

### Parallel Opportunities

- Foundational tests (T004-T005) can run in parallel
- User Stories 1 and 2 can run in parallel (both P1)
- User Stories 4, 5, 6 can run in parallel (independent parameters)
- All tests within a user story marked [P] can run in parallel
- Cross-cutting phases should run sequentially after user stories

### Independent Test Criteria

| User Story | Independent Test |
|------------|------------------|
| US1 (Germanium) | Process audio, verify soft clipping with even harmonics, saggy compression |
| US2 (Silicon) | Process audio, verify hard clipping with odd harmonics, tight response |
| US3 (Bias) | Sweep bias 0.0-1.0, measure gating effect on quiet signals |
| US4 (Fuzz Amount) | Sweep fuzz 0.0-1.0, measure THD increase |
| US5 (Tone) | Sweep tone 0.0-1.0, measure 12dB frequency response change at 4kHz |
| US6 (Volume) | Set volume +6dB/-12dB, verify exact gain change |

### Suggested MVP Scope

**MVP = Phase 1 + Phase 2 + Phase 3 (US1) + Phase 4 (US2)**

This delivers:
- Germanium fuzz with soft clipping and sag (warm, vintage character)
- Silicon fuzz with hard clipping (bright, aggressive character)
- Basic parameter control (fuzz amount, volume)
- Lifecycle (prepare, reset, process)

Total MVP tasks: ~54 tasks (Phases 1-4)

---

## FR-to-Task Traceability

| Requirement | Task(s) | Description |
|-------------|---------|-------------|
| FR-001 | T004, T006 | FuzzType enum |
| FR-002 | T020, T023 | prepare() method |
| FR-003 | T021, T023 | reset() method |
| FR-004 | T022, T023 | process() before prepare() |
| FR-005 | T008, T010 | Default constructor |
| FR-006 | T012, T018 | setFuzzType() |
| FR-006a | T120-T128 | Type crossfade |
| FR-007 | T013, T018 | setFuzz() |
| FR-008 | T014, T018, T087 | setVolume() |
| FR-009 | T015, T018 | setBias() |
| FR-010 | T016, T018 | setTone() |
| FR-011-015 | T009, T010 | Getters |
| FR-016-018 | T027-T036 | Germanium implementation |
| FR-019-021 | T043-T049 | Silicon implementation |
| FR-022-025 | T055-T064 | Bias implementation |
| FR-026-029 | T072-T082 | Tone implementation |
| FR-030-032 | T034-T036, T048 | process() implementation |
| FR-033-035 | T098-T104 | DC blocking |
| FR-036-040 | T106-T118 | Parameter smoothing |
| FR-041-044 | T032, T047, T102, T111 | Component composition |
| FR-045-049 | T142-T146 | Architecture & quality |
| FR-050-053 | T091-T096 | Octave-up mode |

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
