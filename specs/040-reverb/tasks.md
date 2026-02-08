# Tasks: Dattorro Plate Reverb

**Input**: Design documents from `F:\projects\iterum\specs\040-reverb\`
**Prerequisites**: plan.md (complete), spec.md (complete), research.md (complete), data-model.md (complete), contracts/reverb-api.h (complete)

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
4. **Cross-Platform Check**: Verify `-fno-fast-math` for IEEE 754 functions
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/effects/reverb_test.cpp  # ADD YOUR FILE HERE
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

**Purpose**: Project initialization and basic structure for the reverb feature

- [X] T001 Update `dsp/CMakeLists.txt` to add `include/krate/dsp/effects/reverb.h` to `KRATE_DSP_EFFECTS_HEADERS` list
- [X] T002 Update `dsp/tests/CMakeLists.txt` to add `unit/effects/reverb_test.cpp` to the `dsp_tests` source list in "Layer 4: Effects" section
- [X] T003 Create empty test file `dsp/tests/unit/effects/reverb_test.cpp` with includes for Catch2 and reverb header

**Checkpoint**: Build system ready to accept reverb implementation and tests

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

This reverb feature has NO foundational blocking tasks - the reverb reuses existing Layer 0-1 primitives (DelayLine, OnePoleLP, DCBlocker, SchroederAllpass, OnePoleSmoother). All dependencies already exist and are tested.

**Checkpoint**: Foundation ready - user story implementation can now begin immediately

---

## Phase 3: User Story 1 - Basic Reverb Processing (Priority: P1) MVP

**Goal**: A synthesizer developer integrates the Reverb effect into a signal chain to add spatial depth to dry synthesizer output. The reverb processes stereo audio in real time, producing a lush, diffuse tail.

**Independent Test**: Feed a stereo impulse into the reverb and verify that the output contains a decaying, diffuse tail with correct stereo separation. Delivers immediate value as a usable effect.

**Acceptance Scenarios**:
1. Stereo impulse produces a decaying reverb tail lasting at least 1 second with energy in both channels
2. Continuous stereo audio produces smooth blend of dry input and wet reverb signal according to mix parameter
3. mix=0.0 produces output identical to dry input (no wet signal)
4. mix=1.0 produces output containing only wet reverb signal (no dry signal)

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

**Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T004 [P] [US1] Write test for `Reverb()` default construction in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T005 [P] [US1] Write test for `prepare()` lifecycle in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T006 [P] [US1] Write test for `reset()` clearing state in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T007 [P] [US1] Write test for impulse producing decaying tail (FR-001 validation) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T008 [P] [US1] Write test for mix=0.0 producing dry-only output in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T009 [P] [US1] Write test for mix=1.0 producing wet-only output in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T010 [P] [US1] Write test for continuous audio producing blended dry/wet output in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T011 [US1] Verify all tests FAIL (no implementation exists yet)

### 3.2 Implementation for User Story 1

- [X] T012 [US1] Create `dsp/include/krate/dsp/effects/reverb.h` header file with namespace and include guards
- [X] T013 [US1] Implement `ReverbParams` struct with all 9 parameters (roomSize, damping, width, mix, preDelayMs, diffusion, freeze, modRate, modDepth) and defaults per FR-011 in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T014 [US1] Implement `Reverb` class skeleton with private members in `dsp/include/krate/dsp/effects/reverb.h`: bandwidth filter (OnePoleLP), pre-delay (DelayLine), 4 input diffusion allpasses (SchroederAllpass), 4 tank allpass delay lines (standalone DelayLine — NOT SchroederAllpass, because FR-009 output taps must read inside them), 2 tank pre-damp delays (DelayLine), 2 tank damping LPs (OnePoleLP), 2 tank post-damp delays (DelayLine), 2 tank DC blockers (DCBlocker), 8 parameter smoothers (OnePoleSmoother), tank state variables (tankAOut_, tankBOut_, lfoPhase_)
- [X] T015 [US1] Implement `Reverb::prepare(double sampleRate)` method with delay length scaling per FR-010, component initialization, and parameter smoother configuration in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T016 [US1] Implement `Reverb::reset()` method to clear all delay lines, filter states, LFO phase, and tank feedback variables in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T017 [US1] Implement `Reverb::setParams(const ReverbParams&)` method to update all 8 smoothers with parameter values in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T018 [US1] Implement input processing in `Reverb::process()` in `dsp/include/krate/dsp/effects/reverb.h`: (1) NaN/Inf input validation using `detail::isNaN()` and `detail::isInf()` per FR-027 and research.md R11 — replace invalid inputs with 0.0f BEFORE any other processing, (2) sum L+R to mono, (3) bandwidth filter per FR-012, (4) pre-delay per FR-011
- [X] T019 [US1] Implement 4-stage input diffusion (allpass cascade per FR-002, FR-003, scaled coefficients via diffusion parameter) in `Reverb::process()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T020 [US1] Implement Tank A processing (modulated DD1 allpass, pre-damp delay, damping LP, decay gain, DD2 allpass, post-damp delay, DC blocker) per FR-004, FR-005, FR-006, FR-007 in `Reverb::process()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T021 [US1] Implement Tank B processing (modulated DD1 allpass, pre-damp delay, damping LP, decay gain, DD2 allpass, post-damp delay, DC blocker) per FR-004, FR-005, FR-006, FR-007 in `Reverb::process()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T022 [US1] Implement output tap computation (7 taps for left, 7 taps for right per FR-008, FR-009, Table 2) in `Reverb::process()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T023 [US1] Implement stereo width processing (mid-side encoding per FR-011a) in `Reverb::process()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T024 [US1] Implement dry/wet mix blending per FR-011 in `Reverb::process()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T025 [US1] Implement `Reverb::processBlock()` as a loop over `process()` calls in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T026 [US1] Implement `Reverb::isPrepared()` query method in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T027 [US1] Build the reverb: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T028 [US1] Fix all compiler warnings and errors
- [X] T029 [US1] Run tests and verify all User Story 1 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[reverb]"`

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T030 [US1] Verify IEEE 754 compliance: Since reverb uses NaN/Inf detection (detail::isNaN, detail::isInf), add `unit/effects/reverb_test.cpp` to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (Clang/GCC only)

### 3.4 Commit (MANDATORY)

- [X] T031 [US1] Commit completed User Story 1 work: "Implement Dattorro plate reverb basic processing (US1)"

**Checkpoint**: User Story 1 should be fully functional - reverb processes stereo audio with impulse response, mix control, and basic tank topology

---

## Phase 4: User Story 2 - Parameter Control (Priority: P1)

**Goal**: A sound designer adjusts reverb parameters (room size, damping, diffusion, width, pre-delay) to shape the reverb character from tight, bright spaces to large, dark halls. Each parameter change takes effect smoothly without clicks or artifacts.

**Independent Test**: Sweep each parameter across its full range during audio processing and verify no clicks, pops, or discontinuities occur.

**Acceptance Scenarios**:
1. Room size changes from min to max changes reverb tail length proportionally without audible artifacts
2. Increasing damping causes high frequencies in reverb tail to decay faster than low frequencies
3. width=0.0 produces mono output (L == R), width=1.0 produces maximally decorrelated stereo
4. Pre-delay=50ms delays onset of reverb tail by approximately 50ms
5. Reducing diffusion toward 0.0 makes reverb tail less smooth with audible discrete echoes

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

**Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T032 [P] [US2] Write test for roomSize parameter mapping to decay coefficient (0.5 to 0.95 range per FR-011) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T033 [P] [US2] Write test for damping parameter mapping to cutoff frequency (200 Hz to 20000 Hz per FR-011, FR-013) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T034 [P] [US2] Write test for width=0.0 producing mono output (left == right after width processing) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T035 [P] [US2] Write test for width=1.0 producing full stereo (cross-correlation < 0.5, SC-007) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T036 [P] [US2] Write test for pre-delay creating temporal offset in wet signal in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T037 [P] [US2] Write test for diffusion=0.0 reducing smearing (less diffuse output than diffusion=1.0) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T038 [P] [US2] Write test for parameter changes producing no clicks (swept sine test per SC-004) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T039 [US2] Verify all tests FAIL (parameter logic not yet implemented)

### 4.2 Implementation for User Story 2

- [X] T040 [US2] Implement roomSize to decay coefficient mapping (`decay = 0.5 + roomSize * roomSize * 0.45`) in `Reverb::setParams()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T041 [US2] Implement damping to cutoff Hz mapping (`cutoffHz = 200.0 * pow(100.0, 1.0 - damping)`) and damping LP coefficient calculation per FR-013 in `Reverb::setParams()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T042 [US2] Implement bandwidth filter cutoff calculation from 0.9995 coefficient (solve: `cutoffHz = -ln(0.9995) * sampleRate / (2*pi)`) in `Reverb::prepare()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T043 [US2] Implement pre-delay time conversion from milliseconds to samples and smoother updates in `Reverb::setParams()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T044 [US2] Implement diffusion parameter scaling (diffusion1 = diffusion * 0.75, diffusion2 = diffusion * 0.625) with smoother updates in `Reverb::setParams()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T045 [US2] Implement input diffusion coefficient application from smoothers (call `setCoefficient()` on 4 input allpasses every sample or when smoothers change) in `Reverb::process()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T046 [US2] Implement parameter smoothing integration (advance all 8 smoothers per sample, apply smoothed values to components) in `Reverb::process()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T047 [US2] Build the reverb: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T048 [US2] Fix all compiler warnings and errors
- [X] T049 [US2] Run tests and verify all User Story 2 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[reverb]"`

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T050 [US2] Verify IEEE 754 compliance: Confirm `unit/effects/reverb_test.cpp` is in `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (already added in US1)

### 4.4 Commit (MANDATORY)

- [X] T051 [US2] Commit completed User Story 2 work: "Implement reverb parameter control with smoothing (US2)"

**Checkpoint**: User Stories 1 AND 2 should both work - reverb has full parameter control (roomSize, damping, width, preDelay, diffusion, mix) with smooth transitions

---

## Phase 5: User Story 3 - Freeze Mode (Priority: P2)

**Goal**: A performer activates freeze mode to capture and sustain the current reverb tail indefinitely, creating an evolving pad-like texture. When freeze is deactivated, the reverb resumes normal decay behavior.

**Independent Test**: Process audio, activate freeze, stop input, and verify the reverb tail sustains indefinitely without decay or growth.

**Acceptance Scenarios**:
1. When freeze mode is activated, reverb tail sustains indefinitely without decaying
2. When frozen, new input audio does not enter the tank (frozen texture is preserved)
3. When freeze is deactivated, normal decay resumes and tail fades out according to current room size
4. Frozen reverb signal level remains stable (within +/- 0.5 dB) for at least 60 seconds (SC-003)

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

**Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T052 [P] [US3] Write test for freeze mode sustaining tail indefinitely (signal level stable over 60s per SC-003) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T053 [P] [US3] Write test for freeze blocking new input (no new signal enters tank) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T054 [P] [US3] Write test for unfreeze resuming normal decay in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T055 [P] [US3] Write test for freeze transition being click-free in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T056 [US3] Verify all tests FAIL (freeze logic not yet implemented)

### 5.2 Implementation for User Story 3

- [X] T057 [US3] Implement freeze parameter handling in `Reverb::setParams()`: when freeze=true, target inputGain smoother to 0.0, decay smoother to 1.0, and damping cutoff to Nyquist; when freeze=false, restore normal targets per FR-015, FR-016 in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T058 [US3] Implement input gain application (multiply diffused input by smoothed inputGain before tank injection) in `Reverb::process()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T059 [US3] Build the reverb: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T060 [US3] Fix all compiler warnings and errors
- [X] T061 [US3] Run tests and verify all User Story 3 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[reverb]"`

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T062 [US3] Verify IEEE 754 compliance: Confirm `unit/effects/reverb_test.cpp` is in `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (already added in US1)

### 5.4 Commit (MANDATORY)

- [X] T063 [US3] Commit completed User Story 3 work: "Implement reverb freeze mode for infinite sustain (US3)"

**Checkpoint**: User Stories 1, 2, AND 3 should all work - reverb now has freeze mode for creative sound design

---

## Phase 6: User Story 4 - Tank Modulation (Priority: P2)

**Goal**: A sound designer enables subtle modulation within the reverb tank to prevent metallic ringing artifacts on long decay settings, adding a chorusing quality that makes the reverb sound more natural and lively.

**Independent Test**: Compare spectral characteristics of reverb tail with modulation off (potential metallic ringing on long decays) versus modulation on (smoother, more diffuse tail).

**Acceptance Scenarios**:
1. With long decay and zero modulation, output spectrum shows distinct resonant peaks
2. With modulation enabled (moderate rate and depth), resonant peaks are smeared and reduced in amplitude
3. With modulation at maximum depth, subtle chorusing effect is audible but no obvious pitch wobble

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

**Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T064 [P] [US4] Write test for modDepth=0.0 having no effect on output (matches unmodulated reverb) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T065 [P] [US4] Write test for modDepth>0.0 smearing spectral peaks (compare FFT of tail with/without modulation) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T066 [P] [US4] Write test for quadrature LFO phase (Tank A uses sin, Tank B uses cos, 90-degree offset per FR-018) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T067 [P] [US4] Write test for LFO excursion scaling (8 samples peak at 29761 Hz, scaled to actual sample rate per FR-017) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T068 [US4] Verify all tests FAIL (modulation logic not yet implemented)

### 6.2 Implementation for User Story 4

- [X] T069 [US4] Implement LFO phase accumulator (`lfoPhase_` variable, `lfoPhaseIncrement_` calculated from modRate per sample in `Reverb::prepare()`) in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T070 [US4] Implement maxExcursion scaling (8 samples at 29761 Hz, scaled to actual sample rate) in `Reverb::prepare()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T071 [US4] Implement per-sample LFO computation (`lfoA = sin(lfoPhase_) * modDepth * maxExcursion_`, `lfoB = cos(lfoPhase_) * modDepth * maxExcursion_` for quadrature phase per FR-018) in `Reverb::process()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T072 [US4] Implement modulated delay application to Tank A DD1 allpass (`setDelaySamples(tankADD1Center_ + lfoA)`) and Tank B DD1 allpass (`setDelaySamples(tankBDD1Center_ + lfoB)`) per FR-017 in `Reverb::process()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T073 [US4] Implement LFO phase advancement per sample (`lfoPhase_ += lfoPhaseIncrement_`, wrap to [0, 2*pi)) in `Reverb::process()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T074 [US4] Build the reverb: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T075 [US4] Fix all compiler warnings and errors
- [X] T076 [US4] Run tests and verify all User Story 4 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[reverb]"`

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T077 [US4] Verify IEEE 754 compliance: Confirm `unit/effects/reverb_test.cpp` is in `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (already added in US1)

### 6.4 Commit (MANDATORY)

- [X] T078 [US4] Commit completed User Story 4 work: "Implement reverb tank modulation with quadrature LFO (US4)"

**Checkpoint**: User Stories 1-4 all work - reverb now has LFO modulation for reduced metallic ringing

---

## Phase 7: User Story 5 - Multiple Instance Performance (Priority: P2)

**Goal**: A synth engine runs multiple reverb instances (one per effects chain or as a shared bus effect) without exceeding the CPU budget, maintaining real-time performance at standard sample rates.

**Independent Test**: Instantiate multiple reverb instances, process audio through all of them simultaneously, and measure total CPU usage.

**Acceptance Scenarios**:
1. Single reverb instance at 44.1 kHz consumes less than 1% CPU (SC-001)
2. 4 simultaneous reverb instances at 44.1 kHz consume less than 4% CPU total
3. Reverb instance at 96 kHz scales proportionally (~2x the 44.1 kHz usage) and remains within acceptable limits

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

**Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T079 [P] [US5] Write performance benchmark test for single reverb instance at 44.1 kHz (target < 1% CPU per SC-001) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T080 [P] [US5] Write performance benchmark test for 4 simultaneous reverb instances at 44.1 kHz (target < 4% total CPU) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T081 [P] [US5] Write performance benchmark test for reverb instance at 96 kHz (verify proportional scaling) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T082 [P] [US5] Write test for `processBlock()` being bit-identical to N calls to `process()` in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T083 [US5] Verify all tests FAIL (performance not yet optimized)

### 7.2 Implementation for User Story 5

- [X] T084 [US5] Implement optimization: skip modulation LFO when modDepth=0.0 (per plan.md Alternative Optimizations) in `Reverb::process()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T085 [US5] Implement optimization: skip input diffusion when diffusion=0.0 (per plan.md Alternative Optimizations) in `Reverb::process()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T086 [US5] Implement denormal flushing on tank feedback state variables (`tankAOut_`, `tankBOut_`) using `detail::flushDenormal()` per FR-028, research.md R9 in `Reverb::process()` in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T087 [US5] Build the reverb: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T088 [US5] Fix all compiler warnings and errors
- [X] T089 [US5] Run performance benchmarks and verify SC-001 target (<1% CPU at 44.1 kHz): `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[reverb][performance]"`
- [X] T090 [US5] If performance target not met, profile and optimize critical path (delay reads, smoothing overhead)

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T091 [US5] Verify IEEE 754 compliance: Confirm `unit/effects/reverb_test.cpp` is in `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (already added in US1)

### 7.4 Commit (MANDATORY)

- [X] T092 [US5] Commit completed User Story 5 work: "Optimize reverb performance for multiple instances (US5)"

**Checkpoint**: All 5 user stories complete - reverb is fully functional with performance validated

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Final validation, edge cases, sample rate support, and stability tests

### 8.1 Edge Case Tests (Write FIRST - Must FAIL)

- [X] T093 [P] Write test for NaN input producing valid output (FR-027: NaN replaced with 0.0) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T094 [P] Write test for infinity input producing valid output (FR-027: Inf replaced with 0.0) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T095 [P] Write test for max roomSize + min damping stability (SC-008: no unbounded growth over 10s) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T096 [P] Write test for white noise input staying bounded (output < +6 dBFS per SC-008) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T097 [P] Write test for all parameters changed simultaneously producing no clicks in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T098 [P] Write test for `reset()` during active processing immediately stopping tail in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T099 [P] Write test for `prepare()` called with different sample rate re-initializing delay lines in `dsp/tests/unit/effects/reverb_test.cpp`

### 8.2 Sample Rate Support Tests (Write FIRST - Must FAIL)

- [X] T100 [P] Write test for 8000 Hz sample rate support (FR-030: edge case low rate) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T101 [P] Write test for 44100 Hz sample rate support (FR-030, SC-005) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T102 [P] Write test for 48000 Hz sample rate support (FR-030, SC-005) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T103 [P] Write test for 88200 Hz sample rate support (FR-030, SC-005) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T104 [P] Write test for 96000 Hz sample rate support (FR-030, SC-005) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T105 [P] Write test for 192000 Hz sample rate support (FR-030, SC-005) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T106 [P] Write test for reverb character consistency across sample rates (SC-005: perceptually similar decay) in `dsp/tests/unit/effects/reverb_test.cpp`

### 8.3 Success Criteria Validation Tests (Write FIRST - Must FAIL)

- [X] T107 [P] Write test for RT60 exponential decay measurement (SC-002: smooth exponential decay) in `dsp/tests/unit/effects/reverb_test.cpp`
- [X] T108 [P] Write test for impulse response echo density increasing over time (SC-006: sparse reflections becoming dense tail) in `dsp/tests/unit/effects/reverb_test.cpp`

### 8.4 Edge Case Implementation

- [X] T109 Verify NaN/Inf input handling: Open `dsp/include/krate/dsp/effects/reverb.h`, find `Reverb::process()`, and confirm that `detail::isNaN()` and `detail::isInf()` checks exist BEFORE the bandwidth filter (implemented in T018). If missing, add them now per FR-027: `if (detail::isNaN(left) || detail::isInf(left)) left = 0.0f;` and same for right channel
- [X] T110 Build the reverb: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T111 Run all edge case tests and verify they pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[reverb][edge]"`

### 8.5 Sample Rate and Success Criteria Validation

- [X] T112 Run all sample rate tests and verify they pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[reverb][samplerate]"`
- [X] T113 Run all success criteria validation tests and verify they pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[reverb][success]"`

### 8.6 Final Commit

- [X] T114 Commit polish work: "Add reverb edge case handling and sample rate validation (Phase 8)"

**Checkpoint**: All edge cases, sample rates, and success criteria validated

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

**Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [X] T115 Update `specs/_architecture_/layer-4-features.md` with new Reverb component entry: purpose (Dattorro plate reverb with freeze, modulation), public API summary (ReverbParams, Reverb class with prepare/reset/setParams/process/processBlock/isPrepared), file location (`dsp/include/krate/dsp/effects/reverb.h`), when to use (spatial processing for synth/delay output, post-delay pre-output effects chain), usage example (from quickstart.md)
- [X] T116 Verify no duplicate reverb functionality was introduced (search for existing reverb implementations in `specs/_architecture_/`)

### 9.2 Final Commit

- [ ] T117 Commit architecture documentation updates: "Update architecture docs with Reverb component"
- [ ] T118 Verify all spec work is committed to feature branch `040-reverb`

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

**Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 10.1 Run Clang-Tidy Analysis

- [ ] T119 Ensure compile_commands.json exists (run `cmake --preset windows-ninja` from VS Developer PowerShell if not present)
- [ ] T120 Run clang-tidy on reverb header: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`

### 10.2 Address Findings

- [ ] T121 Fix all errors reported by clang-tidy (blocking issues)
- [ ] T122 Review warnings and fix where appropriate (use judgment for DSP code - magic numbers in delay lengths are acceptable)
- [ ] T123 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)
- [ ] T124 Rebuild after fixes: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T125 Verify all tests still pass after clang-tidy fixes: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[reverb]"`

### 10.3 Final Commit

- [ ] T126 Commit clang-tidy fixes: "Address clang-tidy findings for reverb"

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

**Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T127 Review ALL FR-xxx requirements (FR-001 through FR-030) from `F:\projects\iterum\specs\040-reverb\spec.md` against implementation in `dsp/include/krate/dsp/effects/reverb.h`
- [X] T128 Review ALL SC-xxx success criteria (SC-001 through SC-008) from `F:\projects\iterum\specs\040-reverb\spec.md` and verify measurable targets are achieved (run tests, record actual values)
- [X] T129 Search for cheating patterns in `dsp/include/krate/dsp/effects/reverb.h`:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [X] T130 Open `F:\projects\iterum\specs\040-reverb\spec.md` and locate "Implementation Verification" section
- [X] T131 For EACH FR-xxx requirement, fill the Evidence column with: file path, line number(s), test name that validates it, and actual test result
- [X] T132 For EACH SC-xxx success criterion, fill the Evidence column with: test name, actual measured value, and comparison to spec threshold
- [X] T133 Mark each requirement Status as: MET (with evidence), NOT MET (document gap), PARTIAL (document what IS met), or DEFERRED (with user approval)
- [X] T134 Fill "Overall Status" section: COMPLETE / NOT COMPLETE / PARTIAL
- [X] T135 If NOT COMPLETE, document gaps in the "If NOT COMPLETE" section with specific requirement IDs and reasons

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T136 All self-check questions answered "no" (or gaps documented honestly in spec.md)

### 11.4 Final Commit

- [ ] T137 Commit honest compliance assessment: "Complete spec verification for Dattorro reverb (040-reverb)"

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Verification

- [ ] T138 Run ALL reverb tests one final time: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[reverb]"`
- [ ] T139 Verify all tests pass with no failures

### 12.2 Completion Claim

- [ ] T140 Claim completion ONLY if all requirements in `F:\projects\iterum\specs\040-reverb\spec.md` are marked MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: No blocking tasks (all dependencies exist in Layer 0-1)
- **User Stories (Phase 3-7)**: Can start immediately after Setup
  - User Story 1 (P1): Basic reverb processing - MUST complete first (MVP foundation)
  - User Story 2 (P1): Parameter control - Depends on US1 (extends parameter handling)
  - User Story 3 (P2): Freeze mode - Depends on US1 (uses tank state), independent of US2/US4/US5
  - User Story 4 (P2): Tank modulation - Depends on US1 (modulates tank allpasses), independent of US2/US3/US5
  - User Story 5 (P2): Performance - Depends on US1-4 (optimizes complete implementation)
- **Polish (Phase 8)**: Depends on all desired user stories (US1-5) being complete
- **Documentation (Phase 9)**: Depends on all implementation complete
- **Static Analysis (Phase 10)**: Depends on all implementation complete
- **Completion Verification (Phase 11)**: Depends on all previous phases
- **Final Completion (Phase 12)**: Depends on Phase 11 completion

### User Story Dependencies

- **User Story 1 (P1)**: Foundation - No dependencies on other stories
- **User Story 2 (P1)**: Can start after US1 implementation complete (extends setParams/process logic)
- **User Story 3 (P2)**: Can start after US1 implementation complete (independent of US2/US4/US5)
- **User Story 4 (P2)**: Can start after US1 implementation complete (independent of US2/US3/US5)
- **User Story 5 (P2)**: MUST wait for US1-4 implementation complete (optimizes full feature set)

### Within Each User Story

1. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
2. Implementation to make tests pass
3. Build and verify compilation
4. Run tests and verify they pass
5. **Cross-platform check**: Verify `-fno-fast-math` for IEEE 754 functions (done once in US1, confirmed in later stories)
6. **Commit**: LAST task - commit completed work

### Parallel Opportunities

**Setup (Phase 1)**: All 3 tasks can run in parallel (different files)

**User Story 1 Tests (Phase 3.1)**: Tasks T004-T010 can run in parallel (all write to same file, but different test cases)

**User Story 1 Implementation**: Sequential (each task builds on previous state in reverb.h)

**User Story 2 Tests (Phase 4.1)**: Tasks T032-T038 can run in parallel (all write to same file, different test cases)

**User Story 3 Tests (Phase 5.1)**: Tasks T052-T055 can run in parallel

**User Story 4 Tests (Phase 6.1)**: Tasks T064-T067 can run in parallel

**User Story 5 Tests (Phase 7.1)**: Tasks T079-T082 can run in parallel

**Phase 8 Tests**: Tasks T093-T108 can ALL run in parallel (different test cases in same file)

**After US1 complete**: US3 and US4 can proceed in parallel (independent features)

---

## Parallel Example: User Story 1

```bash
# Step 1: Write all tests in parallel (different test cases in reverb_test.cpp)
Task T004 [P]: Write test for Reverb() default construction
Task T005 [P]: Write test for prepare() lifecycle
Task T006 [P]: Write test for reset() clearing state
Task T007 [P]: Write test for impulse producing decaying tail
Task T008 [P]: Write test for mix=0.0 producing dry-only output
Task T009 [P]: Write test for mix=1.0 producing wet-only output
Task T010 [P]: Write test for continuous audio blended output

# Step 2: Implement sequentially (same file, dependent steps)
Task T012: Create reverb.h header
Task T013: Implement ReverbParams struct
Task T014: Implement Reverb class skeleton
Task T015: Implement prepare()
Task T016: Implement reset()
... (continue sequentially)

# Step 3: Build, test, commit
Task T027: Build
Task T029: Run tests
Task T031: Commit
```

---

## Implementation Strategy

### MVP First (User Stories 1 & 2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 3: User Story 1 (Basic Processing)
3. Complete Phase 4: User Story 2 (Parameter Control)
4. **STOP and VALIDATE**: Test US1+US2 independently
5. Reverb is now fully usable for basic spatial processing

### Incremental Delivery

1. Setup → Foundation ready
2. Add User Story 1 → Test independently → Basic reverb working (impulse response, mix)
3. Add User Story 2 → Test independently → Full parameter control (roomSize, damping, width, preDelay, diffusion)
4. Add User Story 3 → Test independently → Freeze mode for creative sound design
5. Add User Story 4 → Test independently → Tank modulation for reduced metallic ringing
6. Add User Story 5 → Test independently → Performance optimization validated
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers (after US1 complete):

1. Team completes Setup + User Story 1 together (foundation)
2. Once US1 is done:
   - Developer A: User Story 2 (parameter control)
   - Developer B: User Story 3 (freeze mode)
   - Developer C: User Story 4 (tank modulation)
3. User Story 5 (performance) waits for US1-4 complete
4. Stories integrate independently

---

## Notes

- [P] tasks = different test cases or independent files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test file to `-fno-fast-math` list in US1, verify in later stories)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-4-features.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification in Phase 11 before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- The reverb is header-only (reverb.h) - no .cpp file needed
- All existing Layer 0-1 primitives are already tested and working
- Research decisions documented in research.md (R1-R13) guide implementation choices
