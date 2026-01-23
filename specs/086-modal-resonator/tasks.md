# Tasks: Modal Resonator

**Input**: Design documents from `/specs/086-modal-resonator/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, quickstart.md

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
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/modal_resonator_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure

- [X] T001 Create modal_resonator.h header in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T002 Create modal_resonator_test.cpp test file in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T003 Update dsp/tests/CMakeLists.txt to include modal_resonator_test.cpp in dsp_tests target

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T004 Write failing test for ModalResonator construction and default state in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T005 Implement ModalResonator class shell with constructor in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T006 Write failing test for prepare() initializing sample rate and coefficients in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T007 Implement prepare(double sampleRate) method with sample rate storage in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T008 Write failing test for reset() clearing oscillator states in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T009 Implement reset() method clearing y1_, y2_ state arrays in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T010 Write failing test for process() returning 0.0f when unprepared (FR-026) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T011 Implement isPrepared() query method in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T012 Verify foundational tests pass (construction, prepare, reset, isPrepared)
- [X] T013 **Verify IEEE 754 compliance**: Check if modal_resonator_test.cpp uses std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt if needed
- [X] T014 **Commit foundational infrastructure** (constructor, prepare, reset, isPrepared)

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Basic Modal Resonance (Priority: P1) 🎯 MVP

**Goal**: Enable configuration of modes with frequency, decay (T60), and amplitude, and process audio through them to create pitched, decaying resonant output from impulse or continuous input.

**Independent Test**: Send an impulse through a modal resonator configured with a single mode at 440Hz and verify pitched output at 440Hz (within 5 cents) with exponential decay.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T015 [P] [US1] Write failing test for single mode at 440Hz producing 440Hz output within 5 cents (SC-002, Acceptance Scenario 1) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T016 [P] [US1] Write failing test for process(0.0f) returning 0.0f with no excitation (Acceptance Scenario 2) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T017 [P] [US1] Write failing test for multiple modes decaying according to T60 (Acceptance Scenario 3) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T018 [P] [US1] Write failing test for T60 decay time accurate within 10% (SC-003) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T019 [P] [US1] Write failing test for processBlock() consistency with process() in dsp/tests/unit/processors/modal_resonator_test.cpp

### 3.2 Core Two-Pole Oscillator Implementation

- [X] T020 [US1] Implement mode state arrays (y1_, y2_, a1_, a2_, gains_, frequencies_, t60s_, enabled_) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T021 [US1] Implement t60ToPoleRadius() private helper calculating R = exp(-6.91 / (t60 * sampleRate)) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T022 [US1] Implement calculateModeCoefficients() private method computing a1 = 2*R*cos(theta), a2 = R^2 in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T023 [US1] Implement process(float input) method with two-pole difference equation y[n] = input*amp + a1*y[n-1] - a2*y[n-2] (FR-021) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T024 [US1] Implement processBlock(float* buffer, int numSamples) calling process() for each sample (FR-022) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T025 [US1] Implement denormal flushing using detail::flushDenormal() on oscillator state (FR-029) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T026 [US1] Verify all US1 tests pass (frequency accuracy SC-002, decay accuracy SC-003, silent when no excitation)

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T027 [US1] **Verify IEEE 754 compliance**: Confirm modal_resonator_test.cpp is in -fno-fast-math list in dsp/tests/CMakeLists.txt (uses denormal/NaN detection)

### 3.4 Commit (MANDATORY)

- [X] T028 [US1] **Commit completed User Story 1 work** (core two-pole oscillator, frequency/decay accuracy tests passing)

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Per-Mode Control (Priority: P1)

**Goal**: Enable fine-grained control over individual mode parameters (frequency, T60 decay, amplitude) to shape the tonal character of the resonance.

**Independent Test**: Configure individual modes with different parameters and verify each mode responds independently (frequency changes, decay times differ, amplitude scaling works).

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T029 [P] [US2] Write failing test for setModeFrequency(0, 880.0f) changing mode 0 to 880Hz (Acceptance Scenario 1) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T030 [P] [US2] Write failing test for setModeDecay(0, 2.0f) producing 2-second T60 within 10% (Acceptance Scenario 2) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T031 [P] [US2] Write failing test for setModeAmplitude(0, 0.5f) producing half amplitude compared to 1.0 (Acceptance Scenario 3) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T032 [P] [US2] Write failing test for setModes() bulk configuration from ModalData array (FR-008) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T033 [P] [US2] Write failing test for parameter clamping (frequency [20, sampleRate*0.45], t60 [0.001, 30.0], amplitude [0.0, 1.0]) (FR-033) in dsp/tests/unit/processors/modal_resonator_test.cpp

### 4.2 Per-Mode Control Implementation

- [X] T034 [P] [US2] Implement setModeFrequency(int index, float hz) with clamping to [20, sampleRate*0.45] (FR-005, FR-033) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T035 [P] [US2] Implement setModeDecay(int index, float t60Seconds) with clamping to [0.001, 30.0] (FR-006, FR-033) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T036 [P] [US2] Implement setModeAmplitude(int index, float amplitude) with clamping to [0.0, 1.0] (FR-007, FR-033) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T037 [US2] Define ModalData struct in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T038 [US2] Implement setModes(const ModalData* modes, int count) bulk configuration (FR-008) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T039 [US2] Implement getNumActiveModes() query counting enabled modes in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T040 [US2] Verify all US2 tests pass (per-mode setters, bulk config, parameter clamping)

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T041 [US2] **Verify IEEE 754 compliance**: Confirm no new IEEE 754 functions added to modal_resonator_test.cpp (already in -fno-fast-math list)

### 4.4 Commit (MANDATORY)

- [X] T042 [US2] **Commit completed User Story 2 work** (per-mode control methods, bulk configuration)

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Material Presets (Priority: P2)

**Goal**: Enable rapid prototyping with material presets (Wood, Metal, Glass, Ceramic, Nylon) that automatically configure mode frequencies, decays, and amplitudes matching physical material characteristics.

**Independent Test**: Select different material presets and compare resulting timbre characteristics (frequency ratios, decay patterns, spectral balance). Metal should have longer decay than Wood (SC-010).

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T043 [P] [US3] Write failing test for setMaterial(Material::Metal) configuring long decay and inharmonic ratios (Acceptance Scenario 1) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T044 [P] [US3] Write failing test for setMaterial(Material::Wood) having shorter decay than Metal (Acceptance Scenario 2, SC-010) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T045 [P] [US3] Write failing test for setMaterial(Material::Glass) producing bright, ringing character (Acceptance Scenario 3) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T046 [P] [US3] Write failing test for material preset remaining modifiable after selection (FR-012) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T047 [P] [US3] Write failing test for material presets producing audibly distinct timbres (SC-008) in dsp/tests/unit/processors/modal_resonator_test.cpp

### 5.2 Material Preset Implementation

- [X] T048 [US3] Define Material enum class (Wood, Metal, Glass, Ceramic, Nylon) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T049 [US3] Define MaterialCoefficients struct with b1, b3, ratios[], numModes in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T050 [US3] Define kMaterialPresets[] constexpr array with coefficients from research.md in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T051 [US3] Implement calculateMaterialT60(float frequency, float b1, float b3) computing T60 = 6.91 / (b1 + b3*f^2) (FR-011) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T052 [US3] Implement setMaterial(Material mat) applying frequency ratios and frequency-dependent decay (FR-009, FR-010, FR-011) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T053 [US3] Verify all US3 tests pass (material presets configure modes, Metal > Wood decay, presets modifiable)

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T054 [US3] **Verify IEEE 754 compliance**: Confirm no new IEEE 754 functions added to modal_resonator_test.cpp (already in -fno-fast-math list)

### 5.4 Commit (MANDATORY)

- [X] T055 [US3] **Commit completed User Story 3 work** (material presets with frequency-dependent decay)

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed

---

## Phase 6: User Story 4 - Size and Damping Control (Priority: P2)

**Goal**: Enable macro-level sound shaping via global size scaling (shifts all frequencies proportionally) and global damping (reduces all decay times) without manually editing each mode.

**Independent Test**: Apply size and damping adjustments and compare mode parameters before/after. Size 2.0 should halve frequencies (SC-009), damping should reduce decay times proportionally.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T056 [P] [US4] Write failing test for setSize(2.0f) halving all mode frequencies (Acceptance Scenario 1, SC-009) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T057 [P] [US4] Write failing test for setSize(0.5f) doubling all mode frequencies (Acceptance Scenario 2) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T058 [P] [US4] Write failing test for setDamping(0.5f) reducing all decay times by 50% (Acceptance Scenario 3) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T059 [P] [US4] Write failing test for setDamping(1.0f) producing immediate silence (Acceptance Scenario 4) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T060 [P] [US4] Write failing test for size clamping to [0.1, 10.0] range (FR-014) in dsp/tests/unit/processors/modal_resonator_test.cpp

### 6.2 Global Controls Implementation

- [X] T061 [P] [US4] Add size_ and damping_ member variables to ModalResonator class in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T062 [P] [US4] Implement setSize(float scale) with clamping to [0.1, 10.0] and inverse frequency scaling (FR-013, FR-014) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T063 [P] [US4] Implement setDamping(float amount) applying effective_T60 = base_T60 * (1.0 - damping) (FR-015, FR-016) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T064 [US4] Update calculateModeCoefficients() to incorporate size and damping factors in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T065 [US4] Verify all US4 tests pass (size scaling, damping reduction, parameter clamping)

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T066 [US4] **Verify IEEE 754 compliance**: Confirm no new IEEE 754 functions added to modal_resonator_test.cpp (already in -fno-fast-math list)

### 6.4 Commit (MANDATORY)

- [X] T067 [US4] **Commit completed User Story 4 work** (size and damping global controls)

**Checkpoint**: User Stories 1-4 should all work independently and be committed

---

## Phase 7: User Story 5 - Strike/Excitation (Priority: P3)

**Goal**: Enable standalone percussion synthesis by calling strike() to excite all modes simultaneously with an impulse, creating pitched percussion from the resonant structure alone.

**Independent Test**: Call strike() without any audio input and verify resonant output is produced at all configured mode frequencies with immediate response (within 1 sample, SC-004).

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T068 [P] [US5] Write failing test for strike(1.0f) exciting all modes (Acceptance Scenario 1) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T069 [P] [US5] Write failing test for strike(0.5f) producing half amplitude compared to strike(1.0f) (Acceptance Scenario 2, FR-018) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T070 [P] [US5] Write failing test for strike() followed by natural decay (Acceptance Scenario 3) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T071 [P] [US5] Write failing test for strike latency within 1 sample (SC-004, FR-020) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T072 [P] [US5] Write failing test for strike accumulation when modes already resonating (FR-019) in dsp/tests/unit/processors/modal_resonator_test.cpp

### 7.2 Strike Implementation

- [X] T073 [US5] Implement strike(float velocity) with velocity clamping to [0.0, 1.0] (FR-017, FR-018) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T074 [US5] Implement energy accumulation by adding velocity * gains_[k] to y1_[k] for each enabled mode (FR-019, FR-020) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T075 [US5] Verify all US5 tests pass (strike excitation, velocity scaling, latency, accumulation)

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T076 [US5] **Verify IEEE 754 compliance**: Confirm no new IEEE 754 functions added to modal_resonator_test.cpp (already in -fno-fast-math list)

### 7.4 Commit (MANDATORY)

- [X] T077 [US5] **Commit completed User Story 5 work** (strike/excitation functionality)

**Checkpoint**: All user stories (1-5) should now be independently functional and committed

---

## Phase 8: Parameter Smoothing (Cross-Cutting Enhancement)

**Purpose**: Add parameter smoothing to prevent audible clicks during frequency and amplitude changes (SC-005)

### 8.1 Tests for Parameter Smoothing (Write FIRST - Must FAIL)

- [X] T078 [P] Write failing test for no audible clicks on abrupt frequency change (SC-005) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T079 [P] Write failing test for no audible clicks on abrupt amplitude change (SC-005) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T080 [P] Write failing test for constructor smoothing time parameter (FR-031) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T081 [P] Write failing test for smoothing time configuration affecting ramp duration in dsp/tests/unit/processors/modal_resonator_test.cpp

### 8.2 Parameter Smoothing Implementation

- [X] T082 Add smoothingTimeMs_ parameter to constructor with default 20.0f (FR-031) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T083 Add OnePoleSmoother arrays frequencySmooth_[32] and amplitudeSmooth_[32] to ModalResonator class in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T084 Update prepare() to configure all smoothers with smoothingTimeMs and sample rate in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T085 Update setModeFrequency() to call frequencySmooth_[index].setTarget() instead of direct assignment (FR-030) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T086 Update setModeAmplitude() to call amplitudeSmooth_[index].setTarget() instead of direct assignment (FR-030) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T087 Update process() to call frequencySmooth_[k].process() and amplitudeSmooth_[k].process() before calculating coefficients in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T088 Update reset() to call reset() on all smoothers in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T089 Verify parameter smoothing tests pass (no clicks, configurable smoothing time)
- [X] T090 **Commit parameter smoothing implementation**

---

## Phase 9: Stability and Edge Case Handling (Cross-Cutting Enhancement)

**Purpose**: Ensure real-time safety, stability, and robust edge case handling (FR-027, FR-028, FR-032, SC-007)

### 9.1 Tests for Stability (Write FIRST - Must FAIL)

- [X] T091 [P] Write failing test for NaN input causing reset and returning 0.0f (FR-032) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T092 [P] Write failing test for Inf input causing reset and returning 0.0f (FR-032) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T093 [P] Write failing test for 32 modes at 192kHz remaining stable for 30 seconds (SC-007) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T094 [P] Write failing test for no NaN/Inf in output after 30 seconds continuous operation (SC-007) in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T095 [P] Write failing test for setModes() ignoring modes beyond 32 (FR-001) in dsp/tests/unit/processors/modal_resonator_test.cpp

### 9.2 Stability Implementation

- [X] T096 Add NaN/Inf detection using detail::isNaN() and detail::isInf() in process() method (FR-032) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T097 Add input validation resetting state and returning 0.0f on NaN/Inf (FR-032) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T098 Add noexcept specifier to all process methods (FR-027) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T099 Verify no memory allocation in process(), strike(), or reset() paths (FR-028) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T100 Add kMaxModes = 32 constant and enforce in setModes() (FR-001) in dsp/include/krate/dsp/processors/modal_resonator.h
- [X] T101 Verify stability tests pass (NaN/Inf handling, 30-second stability, 32 mode limit)
- [X] T102 **Verify IEEE 754 compliance**: Confirm modal_resonator_test.cpp remains in -fno-fast-math list (uses isNaN/isInf) in dsp/tests/CMakeLists.txt
- [X] T103 **Commit stability and edge case handling**

---

## Phase 10: Performance Validation (Cross-Cutting Enhancement)

**Purpose**: Verify performance target of 1% CPU @ 192kHz (SC-001) and optimize if needed

### 10.1 Performance Testing

- [X] T104 Write performance benchmark for 32 modes @ 192kHz with 512-sample blocks in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T105 Measure average and max microseconds per 512-sample block (target: <26.7us for 1% CPU) in performance test
- [X] T106 Add performance test tagged with [.performance] to exclude from default test runs in dsp/tests/unit/processors/modal_resonator_test.cpp
- [X] T107 Run performance benchmark and record baseline metrics in performance test
- [X] T108 If performance target not met, analyze hotspots and consider SIMD optimization (scalar implementation currently sufficient per plan.md)
- [X] T109 Verify SC-001 performance target met (1% CPU @ 192kHz)
- [X] T110 **Commit performance validation and any optimizations**

---

## Phase 11: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 11.1 Architecture Documentation Update

- [X] T111 **Update specs/_architecture_/layer-2-processors.md** with ModalResonator component entry including purpose, API summary, file location, and "when to use this"
- [X] T112 Add ModalResonator usage examples to layer-2-processors.md showing material presets and strike excitation
- [X] T113 Document differentiators from ResonatorBank (two-pole oscillators vs bandpass biquads, T60 accuracy, strike coherence) in layer-2-processors.md
- [X] T114 Verify no duplicate functionality was introduced (ModalResonator complements ResonatorBank, different use cases)

### 11.2 Update Quickstart Documentation

- [X] T115 Review and validate quickstart.md examples match final implementation API in specs/086-modal-resonator/quickstart.md
- [X] T116 Add any missing usage patterns discovered during implementation to quickstart.md

### 11.3 Final Commit

- [X] T117 **Commit architecture documentation updates** (specs/_architecture_/layer-2-processors.md, quickstart.md)
- [X] T118 Verify all spec work is committed to feature branch 086-modal-resonator

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 12.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T119 **Review ALL FR-xxx requirements** (FR-001 through FR-033) from spec.md against implementation
- [X] T120 **Review ALL SC-xxx success criteria** (SC-001 through SC-010) and verify measurable targets are achieved
- [X] T121 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in dsp/include/krate/dsp/processors/modal_resonator.h
  - [X] No test thresholds relaxed from spec requirements in dsp/tests/unit/processors/modal_resonator_test.cpp
  - [X] No features quietly removed from scope

### 12.2 Fill Compliance Table in spec.md

- [X] T122 **Update specs/086-modal-resonator/spec.md "Implementation Verification" section** with compliance status (MET/NOT MET/PARTIAL) for each FR and SC requirement
- [X] T123 **Mark overall status honestly** in spec.md: COMPLETE / NOT COMPLETE / PARTIAL

### 12.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T124 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 13: Final Completion

**Purpose**: Final commit and completion claim

### 13.1 Final Commit

- [X] T125 **Commit all spec work** to feature branch 086-modal-resonator
- [X] T126 **Verify all tests pass** using ctest --test-dir build -C Release --output-on-failure

### 13.2 Completion Claim

- [X] T127 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order (P1 US1 → P1 US2 → P2 US3 → P2 US4 → P3 US5)
- **Cross-Cutting Enhancements (Phase 8-10)**: Depend on relevant user stories
  - Parameter Smoothing (Phase 8): Can start after US2 (per-mode control exists)
  - Stability (Phase 9): Can start after US1 (process() exists)
  - Performance (Phase 10): Should run after all features complete
- **Documentation (Phase 11)**: Depends on all implementation being complete
- **Completion Verification (Phase 12-13)**: Depends on all work being complete

### User Story Dependencies

- **User Story 1 (P1) - Basic Modal Resonance**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P1) - Per-Mode Control**: Can start after US1 complete - Builds on process() from US1
- **User Story 3 (P2) - Material Presets**: Can start after US2 complete - Uses setModeFrequency/Decay/Amplitude from US2
- **User Story 4 (P2) - Size/Damping**: Can start after US2 complete - Modifies mode parameters from US2
- **User Story 5 (P3) - Strike**: Can start after US1 complete - Uses process() and mode state from US1

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Core algorithms before advanced features
- Parameter methods before global controls
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have -fno-fast-math in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

- **Setup (Phase 1)**: All 3 tasks can run in parallel (different files)
- **Foundational tests (Phase 2)**: T004, T006, T008, T010 can be written in parallel
- **US1 tests**: T015-T019 can be written in parallel (test-first phase)
- **US2 tests**: T029-T033 can be written in parallel
- **US2 implementation**: T034-T036 can run in parallel (different methods)
- **US3 tests**: T043-T047 can be written in parallel
- **US4 tests**: T056-T060 can be written in parallel
- **US4 implementation**: T061-T063 can run in parallel (size and damping independent)
- **US5 tests**: T068-T072 can be written in parallel
- **Smoothing tests (Phase 8)**: T078-T081 can be written in parallel
- **Stability tests (Phase 9)**: T091-T095 can be written in parallel

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together (test-first phase):
Task T015: "Write failing test for single mode at 440Hz producing 440Hz output within 5 cents"
Task T016: "Write failing test for process(0.0f) returning 0.0f with no excitation"
Task T017: "Write failing test for multiple modes decaying according to T60"
Task T018: "Write failing test for T60 decay time accurate within 10%"
Task T019: "Write failing test for processBlock() consistency with process()"
```

---

## Implementation Strategy

### MVP First (User Stories 1 + 2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 - Basic Modal Resonance
4. Complete Phase 4: User Story 2 - Per-Mode Control
5. **STOP and VALIDATE**: Test US1 + US2 independently
6. Add Phase 8: Parameter Smoothing for production quality
7. Deploy/demo if ready

This delivers the core modal synthesis capability with full per-mode control.

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 (Basic Resonance) → Test independently → Core functionality (MVP!)
3. Add User Story 2 (Per-Mode Control) → Test independently → Fine-tuning capability
4. Add User Story 3 (Material Presets) → Test independently → Rapid prototyping
5. Add User Story 4 (Size/Damping) → Test independently → Global shaping
6. Add User Story 5 (Strike) → Test independently → Percussion synthesis
7. Add Parameter Smoothing → Click-free operation
8. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (blocking for US2)
3. After US1 complete:
   - Developer A: User Story 2
   - Developer B: User Story 5 (only needs US1)
4. After US2 complete:
   - Developer A: User Story 3
   - Developer B: User Story 4 (parallel with US3)
5. Stories complete and integrate independently

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
- All file paths are absolute based on repository root F:\projects\iterum\
- Tests use Catch2 framework (existing test infrastructure)
- ModalResonator is Layer 2 processor in dsp/include/krate/dsp/processors/
- Depends on Layer 0 (math_constants, db_utils) and Layer 1 (OnePoleSmoother)
