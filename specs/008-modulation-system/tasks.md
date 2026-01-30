# Tasks: Modulation System

**Feature**: 008-modulation-system
**Input**: Design documents from `/specs/008-modulation-system/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

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
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
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
             unit/core/modulation_curves_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2)
- All paths are absolute (Windows environment)

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure for modulation system

- [ ] T001 Create branch `008-modulation-system` from main
- [ ] T002 [P] Create directory structure: `F:\projects\iterum\dsp\include\krate\dsp\core\` for modulation types and curves
- [ ] T003 [P] Create directory structure: `F:\projects\iterum\dsp\include\krate\dsp\processors\` for new modulation sources
- [ ] T004 [P] Create directory structure: `F:\projects\iterum\dsp\tests\unit\core\` for Layer 0 tests
- [ ] T005 [P] Create directory structure: `F:\projects\iterum\dsp\tests\unit\processors\` for Layer 2 tests
- [ ] T006 [P] Create directory structure: `F:\projects\iterum\dsp\tests\unit\systems\` for Layer 3 tests
- [ ] T007 Verify build passes: `cmake --preset windows-x64-release && cmake --build build/windows-x64-release --config Release`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Layer 0 types and curves - MUST be complete before ANY modulation source implementation

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Layer 0 - Modulation Types (FR-001 to FR-003)

- [ ] T008 Write failing tests for modulation types in `F:\projects\iterum\dsp\tests\unit\core\modulation_types_test.cpp`
  - Verify enum value counts (ModSource has 13 values, ModCurve has 4 values)
  - Verify ModRouting default construction initializes correctly
  - Verify MacroConfig default construction
  - Verify amount clamping behavior (clamp to [-1, +1])
- [ ] T009 Create modulation types header `F:\projects\iterum\dsp\include\krate\dsp\core\modulation_types.h`
  - Define `enum class ModSource : uint8_t` with 13 values (None, LFO1, LFO2, EnvFollower, Random, Macro1-4, Chaos, SampleHold, PitchFollower, Transient)
  - Define `enum class ModCurve : uint8_t` with 4 values (Linear, Exponential, SCurve, Stepped)
  - Define `struct ModRouting` with source, destParamId, amount, curve, active fields
  - Define `struct MacroConfig` with value, minOutput, maxOutput, curve fields
  - Define `enum class EnvFollowerSourceType : uint8_t` with 5 values
  - Define `enum class SampleHoldInputType : uint8_t` with 4 values
  - Add constants: kModSourceCount=13, kModCurveCount=4, kMaxModRoutings=32, kMaxMacros=4
- [ ] T010 Verify modulation types tests pass
- [ ] T011 Run clang-tidy on `F:\projects\iterum\dsp\include\krate\dsp\core\modulation_types.h`
- [ ] T012 Commit Layer 0 types

### 2.2 Layer 0 - Modulation Curves (FR-058, FR-059, SC-003, SC-004)

- [ ] T013 Write failing tests for modulation curves in `F:\projects\iterum\dsp\tests\unit\core\modulation_curves_test.cpp`
  - Test each curve at positions 0.0, 0.25, 0.5, 0.75, 1.0 (SC-003: within 0.01 tolerance)
  - Linear: y=x (0.0→0.0, 0.5→0.5, 1.0→1.0)
  - Exponential: y=x^2 (0.5→0.25, 0.75→0.5625)
  - S-Curve: y=x^2*(3-2x) (0.0→0.0, 0.5→0.5, 1.0→1.0)
  - Stepped: y=floor(x*4)/3 (0.0→0.0, 0.3→0.333, 0.6→0.667, 1.0→1.0)
  - Test bipolar handling: verify -100% inverts +100% (SC-004: within 0.001 tolerance)
  - Test edge cases: NaN input, infinity input, out-of-range input
- [ ] T014 Create modulation curves header `F:\projects\iterum\dsp\include\krate\dsp\core\modulation_curves.h`
  - Implement `applyModCurve(ModCurve curve, float x)` with all 4 curve formulas
  - Implement `applyBipolarModulation(ModCurve curve, float sourceValue, float amount)` per FR-059
- [ ] T015 Verify modulation curves tests pass
- [ ] T016 **Verify IEEE 754 compliance**: If tests use `std::isnan` → add to `-fno-fast-math` list in `F:\projects\iterum\dsp\tests\CMakeLists.txt`
- [ ] T017 Run clang-tidy on `F:\projects\iterum\dsp\include\krate\dsp\core\modulation_curves.h`
- [ ] T018 Commit Layer 0 curves

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Modulate Parameters with LFO (Priority: P1) 🎯 MVP

**Goal**: Enable LFO modulation to automate morph position, sweep frequency, and per-band parameters for evolving distortion textures

**Independent Test**: Add routing from LFO 1 to Global Mix, play audio, verify parameter oscillates at LFO rate

**Requirements**: FR-007 to FR-014a, SC-001, SC-002, SC-018

### 3.1 Tests for LFO Integration (Write FIRST - Must FAIL)

- [ ] T019 [P] [US1] Write failing LFO unit tests in `F:\projects\iterum\dsp\tests\unit\systems\modulation_engine_test.cpp`
  - Test LFO 1 waveform output at 1Hz completes cycle within 0.1% of expected sample count (SC-001: 44100 samples at 44.1kHz, within 44 samples)
  - Test LFO tempo sync at 120 BPM quarter note produces correct period (SC-002: 22050 samples, within 0.5%)
  - Test all 6 LFO waveforms produce distinct patterns (Sine, Triangle, Saw, Square, S&H, Smooth Random) per SC-018
  - Test LFO unipolar mode converts [-1,+1] to [0,+1]
  - Test LFO retrigger resets phase to 0° on transport start (FR-014a)
  - Test LFO phase offset shifts waveform by specified degrees
- [ ] T020 [P] [US1] Write failing integration test in `F:\projects\iterum\dsp\tests\integration\modulation_integration_test.cpp`
  - Test LFO to parameter routing produces expected oscillation over 2 seconds (FR-093)

### 3.2 Implementation for LFO Integration

- [ ] T021 [US1] Create ModulationEngine class stub in `F:\projects\iterum\dsp\include\krate\dsp\systems\modulation_engine.h`
  - Declare class with LFO1, LFO2 members (reuse existing `Krate::DSP::LFO` from primitives/lfo.h)
  - Add `prepare(double sampleRate, size_t maxBlockSize)` method
  - Add `reset()` method
  - Add LFO setter methods: `setLFO1Rate()`, `setLFO1Waveform()`, `setLFO1PhaseOffset()`, `setLFO1TempoSync()`, `setLFO1NoteValue()`, `setLFO1Unipolar()`, `setLFO1Retrigger()`
  - Add corresponding LFO2 setters
  - Add `process(const BlockContext& ctx, const float* inputL, const float* inputR, size_t numSamples)` method stub
  - Add `getSourceValue(ModSource source)` query method
- [ ] T022 [US1] Implement ModulationEngine LFO processing in `F:\projects\iterum\dsp\include\krate\dsp\systems\modulation_engine.h`
  - In `prepare()`: call `lfo1_.prepare()` and `lfo2_.prepare()`
  - In `process()`: update LFO tempo from BlockContext, call `lfo1_.process()` and `lfo2_.process()`
  - Handle LFO retrigger on transport start (check `ctx.isPlaying` transitions)
  - Apply unipolar conversion if enabled: `(lfoOutput + 1.0f) * 0.5f`
- [ ] T023 [US1] Verify LFO tests pass
- [ ] T024 [US1] Run clang-tidy on `F:\projects\iterum\dsp\include\krate\dsp\systems\modulation_engine.h`
- [ ] T025 [US1] **Commit completed LFO integration work**

**Checkpoint**: User Story 1 (LFO modulation) should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Create Modulation Routing Matrix (Priority: P1)

**Goal**: Connect any modulation source to any destination parameter with amount and curve shape

**Independent Test**: Create routing entry, specify source/destination/amount/curve, verify destination is modulated

**Requirements**: FR-055 to FR-062, FR-085 to FR-088, SC-003, SC-004, SC-005

### 4.1 Tests for Routing Matrix (Write FIRST - Must FAIL)

- [ ] T026 [P] [US2] Write failing routing tests in `F:\projects\iterum\dsp\tests\unit\systems\modulation_engine_test.cpp`
  - Test single routing with LFO to destination applies correct amount and curve (FR-085)
  - Test all 4 curves produce correct output at key positions 0.0, 0.25, 0.5, 0.75, 1.0 (FR-088, SC-003)
  - Test bipolar modulation: negative amount produces inverted modulation (FR-086, SC-004: within 0.001 tolerance)
  - Test multiple routings to same destination sum correctly (FR-087, FR-060)
  - Test summation clamping: 3 routings with +40% each clamp to +1.0 (SC-005)
  - Test 32 simultaneous routings can be active (FR-004)
  - Test routing with amount=0% has no effect
  - Test modulation offset clamped to [-1, +1] (FR-061)
  - Test final value clamped to [0, 1] (FR-062)

### 4.2 Implementation for Routing Matrix

- [ ] T027 [US2] Extend ModulationEngine with routing support in `F:\projects\iterum\dsp\include\krate\dsp\systems\modulation_engine.h`
  - Add `std::array<ModRouting, kMaxModRoutings> routings_` member
  - Add `std::array<OnePoleSmoother, kMaxModRoutings> amountSmoothers_` for 20ms smoothing
  - Add `std::array<float, kMaxModDestinations> modOffsets_` for per-destination accumulation
  - Add `std::array<bool, kMaxModDestinations> destActive_` to track active destinations
  - Add `setRouting(size_t index, const ModRouting& routing)` method
  - Add `clearRouting(size_t index)` method
  - Add `getRouting(size_t index)` method
  - Add `getActiveRoutingCount()` method
  - Add `getModulationOffset(uint32_t destParamId)` method
  - Add `getModulatedValue(uint32_t destParamId, float baseNormalized)` method
- [ ] T028 [US2] Implement routing evaluation loop in `F:\projects\iterum\dsp\include\krate\dsp\systems\modulation_engine.h`
  - In `prepare()`: configure all amount smoothers with 20ms time
  - In `process()`: clear `modOffsets_` and `destActive_` arrays
  - For each active routing: get raw source value, apply `applyBipolarModulation()` with curve and amount, accumulate to destination offset
  - Clamp each destination offset to [-1, +1] per FR-061
  - In `getModulatedValue()`: apply offset to base, clamp to [0, 1] per FR-062
- [ ] T029 [US2] Verify routing tests pass
- [ ] T030 [US2] Run clang-tidy on modified files
- [ ] T031 [US2] **Commit completed routing matrix work**

**Checkpoint**: User Story 2 (routing matrix) should be fully functional, tested, and committed

---

## Phase 5: User Story 3 - Use Envelope Follower for Reactive Distortion (Priority: P1)

**Goal**: Enable distortion to respond to input dynamics for performance-responsive effects

**Independent Test**: Route envelope follower to destination, play audio at varying levels, verify destination responds proportionally

**Requirements**: FR-015 to FR-020a, FR-089, SC-006

### 5.1 Tests for Envelope Follower (Write FIRST - Must FAIL)

- [ ] T032 [P] [US3] Write failing envelope follower tests in `F:\projects\iterum\dsp\tests\unit\systems\modulation_engine_test.cpp`
  - Test envelope follower responds to step input within configured attack time (SC-006: within 10% tolerance at 90% of final value)
  - Test attack and release timing accuracy (FR-017, FR-018)
  - Test sensitivity parameter scales output (FR-019)
  - Test all 5 source modes produce correct output (Input L, Input R, Input Sum, Mid, Side) per FR-020a
  - Test output stays in [0, +1] range (FR-020)

### 5.2 Implementation for Envelope Follower

- [ ] T033 [US3] Extend ModulationEngine with envelope follower in `F:\projects\iterum\dsp\include\krate\dsp\systems\modulation_engine.h`
  - Add `EnvelopeFollower envFollower_` member (reuse existing from `processors/envelope_follower.h`)
  - Add `EnvFollowerSourceType envFollowerSourceType_` member
  - Add `float envFollowerSensitivity_` member
  - Add `setEnvFollowerAttack(float ms)`, `setEnvFollowerRelease(float ms)`, `setEnvFollowerSensitivity(float)`, `setEnvFollowerSource(EnvFollowerSourceType)` methods
- [ ] T034 [US3] Implement envelope follower processing in `F:\projects\iterum\dsp\include\krate\dsp\systems\modulation_engine.h`
  - In `prepare()`: call `envFollower_.prepare()`
  - In `process()`: for each sample, compute input based on source type (L, R, Sum, Mid, Side), call `envFollower_.processSample()`, scale by sensitivity
  - In `getRawSourceValue()`: return `envFollower_.getCurrentValue() * sensitivity` when source is EnvFollower
- [ ] T035 [US3] Verify envelope follower tests pass
- [ ] T036 [US3] Run clang-tidy on modified files
- [ ] T037 [US3] **Commit completed envelope follower work**

**Checkpoint**: User Story 3 (envelope follower) should be fully functional, tested, and committed

---

## Phase 6: User Story 4 - Use Macro Knobs for Performance Control (Priority: P1)

**Goal**: Use macro knobs to control multiple parameters simultaneously with a single gesture

**Independent Test**: Route macro to multiple destinations, adjust macro knob, verify all routed destinations respond

**Requirements**: FR-026 to FR-029a

### 6.1 Tests for Macros (Write FIRST - Must FAIL)

- [ ] T038 [P] [US4] Write failing macro tests in `F:\projects\iterum\dsp\tests\unit\systems\modulation_engine_test.cpp`
  - Test 4 macros are independently available
  - Test macro Min/Max range mapping FIRST (FR-028): `mapped = min + value * (max - min)`
  - Test macro curve applied AFTER Min/Max mapping (FR-029)
  - Test macro output range is [0, +1] unipolar (FR-029a)
  - Test all 4 curves work with macros (Linear, Exponential, S-Curve, Stepped)

### 6.2 Implementation for Macros

- [ ] T039 [US4] Extend ModulationEngine with macros in `F:\projects\iterum\dsp\include\krate\dsp\systems\modulation_engine.h`
  - Add `std::array<MacroConfig, kMaxMacros> macros_` member
  - Add `setMacroValue(size_t index, float value)`, `setMacroMin()`, `setMacroMax()`, `setMacroCurve()` methods
  - Add `getMacroOutput(size_t index)` private method
- [ ] T040 [US4] Implement macro processing in `F:\projects\iterum\dsp\include\krate\dsp\systems\modulation_engine.h`
  - In `getMacroOutput()`: apply Min/Max mapping first per FR-028, then apply curve per FR-029
  - In `getRawSourceValue()`: return `getMacroOutput(index)` when source is Macro1-4
  - Ensure output is clamped to [0, +1] per FR-029a
- [ ] T041 [US4] Verify macro tests pass
- [ ] T042 [US4] Run clang-tidy on modified files
- [ ] T043 [US4] **Commit completed macro work**

**Checkpoint**: User Story 4 (macros) should be fully functional, tested, and committed

---

## Phase 7: User Story 5 - Use Random Modulation Source (Priority: P1)

**Goal**: Introduce controlled randomness into parameter modulation for unpredictable, evolving textures

**Independent Test**: Route Random source to parameter, verify output changes unpredictably at configured rate

**Requirements**: FR-021 to FR-025, SC-016

### 7.1 Tests for Random Source (Write FIRST - Must FAIL)

- [ ] T044 [P] [US5] Write failing random source unit tests in `F:\projects\iterum\dsp\tests\unit\processors\random_source_test.cpp`
  - Test output stays in [-1, +1] (FR-025)
  - Test rate parameter controls how often value changes (0.1Hz to 50Hz)
  - Test smoothness parameter smooths transitions (0% to 100%)
  - Test tempo sync option works
  - Test statistical distribution is uniform over 10000 samples (SC-016: chi-squared test passes at p > 0.01)
- [ ] T045 [P] [US5] Write failing random source integration tests in `F:\projects\iterum\dsp\tests\unit\systems\modulation_engine_test.cpp`
  - Test random source integrates with routing matrix
  - Test 0% smoothness produces sharp transitions, 80% smoothness produces smooth transitions

### 7.2 Implementation for Random Source

- [ ] T046 [US5] Create RandomSource class in `F:\projects\iterum\dsp\include\krate\dsp\processors\random_source.h`
  - Inherit from `ModulationSource` interface
  - Add `Xorshift32 rng_` member (from `core/random.h`)
  - Add `OnePoleSmoother outputSmoother_` member
  - Add `float rate_`, `float smoothness_`, `float phase_`, `float currentValue_`, `bool tempoSync_` members
  - Implement `prepare()`, `reset()`, `process()` methods
  - Implement `getCurrentValue()` and `getSourceRange()` (returns {-1.0f, 1.0f})
  - Add `setRate()`, `setSmoothness()`, `setTempoSync()` methods
- [ ] T047 [US5] Implement random source in `F:\projects\iterum\dsp\include\krate\dsp\processors\random_source.h`
  - In `process()`: advance timer phase, on trigger generate new random value with `rng_.nextFloat()`, apply smoothing
- [ ] T048 [US5] Integrate random source into ModulationEngine in `F:\projects\iterum\dsp\include\krate\dsp\systems\modulation_engine.h`
  - Add `RandomSource random_` member
  - Add `setRandomRate()`, `setRandomSmoothness()`, `setRandomTempoSync()` methods
  - In `prepare()`: call `random_.prepare()`
  - In `process()`: call `random_.process()`
  - In `getRawSourceValue()`: return `random_.getCurrentValue()` when source is Random
- [ ] T049 [US5] Verify random source tests pass
- [ ] T050 [US5] Run clang-tidy on `F:\projects\iterum\dsp\include\krate\dsp\processors\random_source.h`
- [ ] T051 [US5] **Commit completed random source work**

**Checkpoint**: User Story 5 (random source) should be fully functional, tested, and committed

---

## Phase 8: User Story 6 - Use Chaos Attractor Modulation (Priority: P2)

**Goal**: Use chaotic attractor systems to generate organic, evolving modulation patterns

**Independent Test**: Route Chaos source to parameter, adjust speed and coupling, verify output follows chaotic trajectories

**Requirements**: FR-030 to FR-035, SC-007

### 8.1 Tests for Chaos Source (Write FIRST - Must FAIL)

- [ ] T052 [P] [US6] Write failing chaos source unit tests in `F:\projects\iterum\dsp\tests\unit\processors\chaos_mod_source_test.cpp`
  - Test output stays in [-1, +1] after 10 seconds for all 4 models (SC-007: Lorenz, Rossler, Chua, Henon)
  - Test speed parameter affects evolution rate (0.05 to 20.0)
  - Test coupling perturbs attractor from audio input (0.0 to 1.0)
  - Test model switch resets state correctly
  - Test soft-limit normalization per FR-034: `tanh(x / scale)` with per-model scales (Lorenz=20, Rossler=10, Chua=2, Henon=1.5)
  - Test real-time safety: no allocations, branches that stall
- [ ] T053 [P] [US6] Write failing chaos integration tests in `F:\projects\iterum\dsp\tests\unit\systems\modulation_engine_test.cpp`
  - Test chaos source integrates with routing matrix
  - Test different attractor models produce different modulation character

### 8.2 Implementation for Chaos Source

- [ ] T054 [US6] Create ChaosModSource class in `F:\projects\iterum\dsp\include\krate\dsp\processors\chaos_mod_source.h`
  - Inherit from `ModulationSource` interface
  - Add attractor state struct (x, y, z for 3D attractors)
  - Add `ChaosModel model_`, `float speed_`, `float coupling_`, `float normalizedOutput_`, `double sampleRate_`, `int samplesUntilUpdate_` members
  - Implement `prepare()`, `reset()`, `process()` methods (update at control rate every 32 samples)
  - Implement `getCurrentValue()` and `getSourceRange()` (returns {-1.0f, 1.0f})
  - Add `setModel()`, `setSpeed()`, `setCoupling()`, `setInputLevel()` methods
- [ ] T055 [US6] Implement chaos attractor updates in `F:\projects\iterum\dsp\include\krate\dsp\processors\chaos_mod_source.h`
  - Implement Lorenz equations: `dx/dt = sigma*(y-x)`, `dy/dt = x*(rho-z)-y`, `dz/dt = x*y - beta*z`
  - Implement Rossler equations: `dx/dt = -y-z`, `dy/dt = x+a*y`, `dz/dt = b+z*(x-c)`
  - Implement Chua equations (reference ChaosWaveshaper)
  - Implement Henon map: `x_new = 1 - a*x^2 + y`, `y_new = b*x`
  - Apply soft-limit normalization: `output = tanh(state.x / scale)` per FR-034
  - Apply coupling: perturb state based on input level
- [ ] T056 [US6] Integrate chaos source into ModulationEngine in `F:\projects\iterum\dsp\include\krate\dsp\systems\modulation_engine.h`
  - Add `ChaosModSource chaos_` member
  - Add `setChaosModel()`, `setChaosSpeed()`, `setChaosCoupling()` methods
  - In `prepare()`: call `chaos_.prepare()`
  - In `process()`: call `chaos_.process()` at control rate, feed audio envelope for coupling
  - In `getRawSourceValue()`: return `chaos_.getCurrentValue()` when source is Chaos
- [ ] T057 [US6] Verify chaos source tests pass
- [ ] T058 [US6] Run clang-tidy on `F:\projects\iterum\dsp\include\krate\dsp\processors\chaos_mod_source.h`
- [ ] T059 [US6] **Commit completed chaos source work**

**Checkpoint**: User Story 6 (chaos source) should be fully functional, tested, and committed

---

## Phase 9: User Story 7 - Use Sample and Hold Modulation (Priority: P2)

**Goal**: Create stepped modulation patterns by periodically sampling a source and holding the value

**Independent Test**: Route Sample & Hold to parameter, verify output produces distinct stepped values at configured rate

**Requirements**: FR-036 to FR-040, SC-017

### 9.1 Tests for Sample & Hold (Write FIRST - Must FAIL)

- [ ] T060 [P] [US7] Write failing S&H unit tests in `F:\projects\iterum\dsp\tests\unit\processors\sample_hold_source_test.cpp`
  - Test holds value between samples
  - Test rate controls sampling frequency (0.1Hz to 50Hz)
  - Test slew smooths transitions within 10% tolerance (SC-017: 0ms to 500ms)
  - Test all 4 input sources work (Random, LFO 1, LFO 2, External) per FR-037
  - Test output range: [-1, +1] for Random/LFO, [0, +1] for External (FR-040)
- [ ] T061 [P] [US7] Write failing S&H integration tests in `F:\projects\iterum\dsp\tests\unit\systems\modulation_engine_test.cpp`
  - Test S&H source integrates with routing matrix
  - Test S&H can sample LFO 1 or LFO 2 output

### 9.2 Implementation for Sample & Hold

- [ ] T062 [US7] Create SampleHoldSource class in `F:\projects\iterum\dsp\include\krate\dsp\processors\sample_hold_source.h`
  - Inherit from `ModulationSource` interface
  - Add `SampleHoldInputType inputType_`, `float rate_`, `float phase_`, `float heldValue_`, `OnePoleSmoother outputSmoother_`, `float slewMs_`, `Xorshift32 rng_`, `LFO* lfo1Ptr_`, `LFO* lfo2Ptr_`, `float externalLevel_` members
  - Implement `prepare()`, `reset()`, `process()` methods
  - Implement `getCurrentValue()` and `getSourceRange()` (depends on input type)
  - Add `setInputType()`, `setRate()`, `setSlewTime()`, `setExternalLevel()`, `setLFOPointers()` methods
- [ ] T063 [US7] Implement S&H processing in `F:\projects\iterum\dsp\include\krate\dsp\processors\sample_hold_source.h`
  - In `process()`: advance timer, on trigger sample based on input type (Random: `rng_.nextFloat()`, LFO: `lfo->process()`, External: `externalLevel_`), apply slew
- [ ] T064 [US7] Integrate S&H into ModulationEngine in `F:\projects\iterum\dsp\include\krate\dsp\systems\modulation_engine.h`
  - Add `SampleHoldSource sampleHold_` member
  - Add `setSampleHoldSource()`, `setSampleHoldRate()`, `setSampleHoldSlew()` methods
  - In `prepare()`: call `sampleHold_.prepare()`, call `sampleHold_.setLFOPointers(&lfo1_, &lfo2_)`
  - In `process()`: update external level from audio envelope if needed, call `sampleHold_.process()`
  - In `getRawSourceValue()`: return `sampleHold_.getCurrentValue()` when source is SampleHold
- [ ] T065 [US7] Verify S&H tests pass
- [ ] T066 [US7] Run clang-tidy on `F:\projects\iterum\dsp\include\krate\dsp\processors\sample_hold_source.h`
- [ ] T067 [US7] **Commit completed sample & hold work**

**Checkpoint**: User Story 7 (sample & hold) should be fully functional, tested, and committed

---

## Phase 10: User Story 8 - Use Pitch Follower Modulation (Priority: P2)

**Goal**: Enable detected pitch to drive modulation for musically intelligent effects

**Independent Test**: Play monophonic pitched content, route Pitch Follower to destination, verify destination changes with pitch

**Requirements**: FR-041 to FR-047, FR-091, SC-008

### 10.1 Tests for Pitch Follower (Write FIRST - Must FAIL)

- [ ] T068 [P] [US8] Write failing pitch follower unit tests in `F:\projects\iterum\dsp\tests\unit\processors\pitch_follower_source_test.cpp`
  - Test maps 440Hz to expected value within 5% tolerance given default range 80Hz-2000Hz (SC-008)
  - Test Min Hz and Max Hz range configuration (20Hz to 500Hz min, 200Hz to 5000Hz max) per FR-044
  - Test confidence threshold below which last value is held (FR-045)
  - Test tracking speed smooths output (10ms to 300ms) per FR-046
  - Test logarithmic mapping formula per spec: `midiNote = 69 + 12 * log2(freq / 440)`, `modValue = (midiNote - minMidi) / (maxMidi - minMidi)`
  - Test output stays in [0, +1] (FR-047)
- [ ] T069 [P] [US8] Write failing pitch follower integration tests in `F:\projects\iterum\dsp\tests\unit\systems\modulation_engine_test.cpp`
  - Test pitch follower maps known frequencies to expected modulation values within 5% (FR-091)
  - Test pitch follower integrates with routing matrix

### 10.2 Implementation for Pitch Follower

- [ ] T070 [US8] Create PitchFollowerSource class in `F:\projects\iterum\dsp\include\krate\dsp\processors\pitch_follower_source.h`
  - Inherit from `ModulationSource` interface
  - Add `PitchDetector detector_` member (reuse from `primitives/pitch_detector.h`)
  - Add `float minHz_`, `float maxHz_`, `float confidenceThreshold_`, `OnePoleSmoother outputSmoother_`, `float trackingSpeedMs_`, `float lastValidValue_` members
  - Implement `prepare()`, `reset()`, `pushSample()`, `process()` methods
  - Implement `getCurrentValue()` and `getSourceRange()` (returns {0.0f, 1.0f})
  - Add `setMinHz()`, `setMaxHz()`, `setConfidenceThreshold()`, `setTrackingSpeed()` methods
- [ ] T071 [US8] Implement pitch follower mapping in `F:\projects\iterum\dsp\include\krate\dsp\processors\pitch_follower_source.h`
  - In `process()`: query `detector_.getDetectedFrequency()` and `detector_.getConfidence()`
  - Apply logarithmic mapping: `midiNote = 69 + 12 * log2(freq / 440)`, compute min/max MIDI, map to [0, 1]
  - If confidence below threshold, hold `lastValidValue_`
  - Apply tracking speed smoothing with `outputSmoother_`
- [ ] T072 [US8] Integrate pitch follower into ModulationEngine in `F:\projects\iterum\dsp\include\krate\dsp\systems\modulation_engine.h`
  - Add `PitchFollowerSource pitchFollower_` member
  - Add `setPitchFollowerMinHz()`, `setPitchFollowerMaxHz()`, `setPitchFollowerConfidence()`, `setPitchFollowerTrackingSpeed()` methods
  - In `prepare()`: call `pitchFollower_.prepare()`
  - In `process()`: for each sample, call `pitchFollower_.pushSample()` with input audio, call `pitchFollower_.process()`
  - In `getRawSourceValue()`: return `pitchFollower_.getCurrentValue()` when source is PitchFollower
- [ ] T073 [US8] Verify pitch follower tests pass
- [ ] T074 [US8] Run clang-tidy on `F:\projects\iterum\dsp\include\krate\dsp\processors\pitch_follower_source.h`
- [ ] T075 [US8] **Commit completed pitch follower work**

**Checkpoint**: User Story 8 (pitch follower) should be fully functional, tested, and committed

---

## Phase 11: User Story 9 - Use Transient Detector Modulation (Priority: P2)

**Goal**: Enable transients to trigger modulation events for rhythmic, percussive modulation patterns

**Independent Test**: Play percussive audio, route Transient Detector to destination, verify destination spikes on attacks

**Requirements**: FR-048 to FR-054, FR-092, SC-009

### 11.1 Tests for Transient Detector (Write FIRST - Must FAIL)

- [ ] T076 [P] [US9] Write failing transient detector unit tests in `F:\projects\iterum\dsp\tests\unit\processors\transient_detector_test.cpp`
  - Test fires within 2ms of >12dB step input at default sensitivity (SC-009)
  - Test does NOT fire on steady-state signal (FR-092)
  - Test retrigger from current level (FR-053): restart attack from current level during attack phase, transition to attack from current level during decay
  - Test attack time controls rise time to peak (0.5ms to 10ms) per FR-051
  - Test decay time controls exponential fall time (20ms to 200ms) per FR-052
  - Test sensitivity adjusts thresholds per FR-050: `ampThresh = 0.5 * (1 - sensitivity)`, `rateThresh = 0.1 * (1 - sensitivity)`
  - Test output stays in [0, +1] (FR-054)
- [ ] T077 [P] [US9] Write failing transient detector integration tests in `F:\projects\iterum\dsp\tests\unit\systems\modulation_engine_test.cpp`
  - Test transient detector fires on known transient material (FR-092)
  - Test transient detector integrates with routing matrix

### 11.2 Implementation for Transient Detector

- [ ] T078 [US9] Create TransientDetector class in `F:\projects\iterum\dsp\include\krate\dsp\processors\transient_detector.h`
  - Inherit from `ModulationSource` interface
  - Add state enum: `Idle`, `Attack`, `Decay`
  - Add `float sensitivity_`, `float attackMs_`, `float decayMs_`, `float envelope_`, `float prevAmplitude_`, `State state_`, `float attackIncrement_`, `float decayCoeff_` members
  - Implement `prepare()`, `reset()`, `process(float sample)` methods
  - Implement `getCurrentValue()` and `getSourceRange()` (returns {0.0f, 1.0f})
  - Add `setSensitivity()`, `setAttack()`, `setDecay()` methods
- [ ] T079 [US9] Implement transient detection in `F:\projects\iterum\dsp\include\krate\dsp\processors\transient_detector.h`
  - In `process()`: compute amplitude and delta, compute thresholds from sensitivity
  - Detect transient when both amplitude > threshold AND delta > threshold
  - State machine: Idle→Attack on detection, Attack→Decay when envelope reaches 1.0, retrigger from current level
  - Attack: linear ramp with `envelope += attackIncrement`
  - Decay: exponential fall with `envelope *= decayCoeff`
- [ ] T080 [US9] Integrate transient detector into ModulationEngine in `F:\projects\iterum\dsp\include\krate\dsp\systems\modulation_engine.h`
  - Add `TransientDetector transient_` member
  - Add `setTransientSensitivity()`, `setTransientAttack()`, `setTransientDecay()` methods
  - In `prepare()`: call `transient_.prepare()`
  - In `process()`: for each sample, call `transient_.process()` with input audio
  - In `getRawSourceValue()`: return `transient_.getCurrentValue()` when source is Transient
- [ ] T081 [US9] Verify transient detector tests pass
- [ ] T082 [US9] Run clang-tidy on `F:\projects\iterum\dsp\include\krate\dsp\processors\transient_detector.h`
- [ ] T083 [US9] **Commit completed transient detector work**

**Checkpoint**: User Story 9 (transient detector) should be fully functional, tested, and committed

---

## Phase 12: User Story 10 - Configure Modulation Curves (Priority: P1)

**Goal**: Shape how modulation sources affect destinations using different response curves

**Independent Test**: Create routings with different curves, compare modulation response at known source values

**Requirements**: FR-058, FR-059, FR-088, SC-003

**Note**: This user story is already complete from Phase 2 (Foundational) and Phase 4 (Routing Matrix). No additional work needed.

**Checkpoint**: User Story 10 (curves) is verified complete

---

## Phase 13: User Story 11 - Handle Bipolar Modulation (Priority: P1)

**Goal**: Route modulation with negative amounts for inverse modulation effects

**Independent Test**: Create routing with negative amount, verify destination moves in opposite direction

**Requirements**: FR-057, FR-059, FR-086, SC-004

**Note**: This user story is already complete from Phase 4 (Routing Matrix). No additional work needed.

**Checkpoint**: User Story 11 (bipolar modulation) is verified complete

---

## Phase 14: User Story 12 - Modulation Sources UI Panel (Priority: P1)

**Goal**: See and configure all modulation sources in a dedicated panel

**Independent Test**: Open modulation panel, verify all source controls are present and functional

**Requirements**: FR-065 to FR-073

### 14.1 Plugin Integration - Parameter IDs

- [ ] T084 [US12] Add modulation parameter IDs to `F:\projects\iterum\plugins\Disrumpo\src\plugin_ids.h`
  - LFO 1: kLFO1RateId=200, kLFO1ShapeId=201, kLFO1PhaseId=202, kLFO1SyncId=203, kLFO1NoteValueId=204, kLFO1UnipolarId=205, kLFO1RetriggerId=206
  - LFO 2: kLFO2RateId=220 through kLFO2RetriggerId=226 (same layout)
  - Envelope Follower: kEnvFollowerAttackId=240, kEnvFollowerReleaseId=241, kEnvFollowerSensitivityId=242, kEnvFollowerSourceId=243
  - Random: kRandomRateId=260, kRandomSmoothnessId=261, kRandomSyncId=262
  - Chaos: kChaosModelId=280, kChaosSpeedId=281, kChaosCouplingId=282
  - Sample & Hold: kSampleHoldSourceId=285, kSampleHoldRateId=286, kSampleHoldSlewId=287
  - Pitch Follower: kPitchFollowerMinHzId=290, kPitchFollowerMaxHzId=291, kPitchFollowerConfidenceId=292, kPitchFollowerTrackingSpeedId=293
  - Transient Detector: kTransientSensitivityId=295, kTransientAttackId=296, kTransientDecayId=297
  - Macros: kMacro1Id=430, kMacro1MinId=431, kMacro1MaxId=432, kMacro1CurveId=433 (through Macro 4 at 442-445)
- [ ] T085 [US12] Register modulation source parameters in `F:\projects\iterum\plugins\Disrumpo\src\controller\controller.cpp`
  - Call `parameters.addParameter()` for all 178 modulation parameter IDs (200-445)
  - Use appropriate parameter types (RangeParameter, StringListParameter for enums)
  - Set correct ranges, defaults, units, and display names per spec parameter table
- [ ] T086 [US12] Verify parameter registration compiles and plugin loads

### 14.2 Processor Integration

- [ ] T087 [US12] Integrate ModulationEngine into Processor in `F:\projects\iterum\plugins\Disrumpo\src\processor\processor.cpp`
  - Add `ModulationEngine modulationEngine_` member to Processor class
  - In `initialize()`: call `modulationEngine_.prepare(processSetup.sampleRate, processSetup.maxSamplesPerBlock)`
  - In `terminate()`: call `modulationEngine_.reset()`
  - In `processParameterChanges()`: map all modulation parameter IDs to ModulationEngine setter calls
  - In `process()`: call `modulationEngine_.process(blockCtx, inputL, inputR, numSamples)` before band processing
  - Apply modulated values: `float modulatedValue = modulationEngine_.getModulatedValue(paramId, baseNormalized)`
- [ ] T088 [US12] Verify processor integration compiles and processes audio without crashes

### 14.3 UI - Sources Panel

- [ ] T089 [US12] Create modulation sources UI panel in `F:\projects\iterum\plugins\Disrumpo\resources\editor.uidesc`
  - Add Level 3 (Expert) disclosure section for modulation
  - Add LFO 1 controls: Rate knob (0.01-20Hz, default 1Hz), Shape dropdown (Sine/Triangle/Saw/Square/S&H/SmoothRandom, default Sine), Phase knob (0-360deg, default 0), Sync toggle (default Off), Note Value dropdown (when synced, default Quarter), Unipolar toggle (default Off), Retrigger toggle (default Off)
  - Add LFO 2 controls: same layout as LFO 1, default rate 0.5Hz, default shape Triangle
  - Add Envelope Follower controls: Attack knob (1-100ms, default 10ms), Release knob (10-500ms, default 100ms), Sensitivity knob (0-100%, default 50%), Source dropdown (Input L/R/Sum/Mid/Side, default Sum)
  - Add Random controls: Rate knob (0.1-50Hz, default 4Hz), Smoothness knob (0-100%, default 0%), Sync toggle (default Off)
  - Add Chaos controls: Model dropdown (Lorenz/Rossler/Chua/Henon, default Lorenz), Speed knob (0.05-20.0, default 1.0), Coupling knob (0-1.0, default 0.0)
  - Add Sample & Hold controls: Source dropdown (Random/LFO1/LFO2/External, default Random), Rate knob (0.1-50Hz, default 4Hz), Slew knob (0-500ms, default 0ms)
  - Add Pitch Follower controls: Min Hz knob (20-500Hz, default 80Hz), Max Hz knob (200-5000Hz, default 2000Hz), Confidence knob (0-1.0, default 0.5), Tracking Speed knob (10-300ms, default 50ms)
  - Add Transient Detector controls: Sensitivity knob (0-1.0, default 0.5), Attack knob (0.5-10ms, default 2ms), Decay knob (20-200ms, default 50ms)
  - Bind all controls to corresponding parameter IDs (200-297)
- [ ] T090 [US12] Verify sources panel displays and controls respond to user interaction
- [ ] T091 [US12] **Commit completed sources panel work**

**Checkpoint**: User Story 12 (sources panel) should be fully functional and committed

---

## Phase 15: User Story 13 - Modulation Routing Matrix UI (Priority: P1)

**Goal**: View, add, edit, and remove modulation routings in a visual routing matrix panel

**Independent Test**: Open routing matrix, add new routing, select source/destination/amount/curve, verify routing takes effect

**Requirements**: FR-074 to FR-078, FR-300 to FR-427

### 15.1 Plugin Integration - Routing Parameter IDs

- [ ] T092 [US13] Add routing parameter IDs to `F:\projects\iterum\plugins\Disrumpo\src\plugin_ids.h`
  - 32 routings × 4 params each = 128 IDs (300-427)
  - Routing 0: kRouting0SourceId=300, kRouting0DestId=301, kRouting0AmountId=302, kRouting0CurveId=303
  - Pattern continues: Routing 1 starts at 304, Routing 2 at 308, etc.
  - Routing 31: kRouting31SourceId=424, kRouting31DestId=425, kRouting31AmountId=426, kRouting31CurveId=427
- [ ] T093 [US13] Register routing parameters in `F:\projects\iterum\plugins\Disrumpo\src\controller\controller.cpp`
  - Add all 128 routing parameter IDs (300-427)
  - Source: StringListParameter with 13 options (None, LFO1, LFO2, EnvFollower, Random, Macro1-4, Chaos, SampleHold, PitchFollower, Transient)
  - Destination: StringListParameter with all modulatable parameter names (~60 options from FR-063)
  - Amount: RangeParameter [-1.0, +1.0], default 0.0, unit "%"
  - Curve: StringListParameter (Linear, Exponential, S-Curve, Stepped), default Linear
- [ ] T094 [US13] Handle routing parameters in Processor in `F:\projects\iterum\plugins\Disrumpo\src\processor\processor.cpp`
  - In `processParameterChanges()`: when routing parameter changes, call `modulationEngine_.setRouting()` with constructed `ModRouting` struct
- [ ] T095 [US13] Verify routing parameters compile and update routing state

### 15.2 UI - Routing Matrix Panel

- [ ] T096 [US13] Create routing matrix UI panel in `F:\projects\iterum\plugins\Disrumpo\resources\editor.uidesc`
  - Add routing matrix view with scrollable list of 32 routing slots
  - Each routing row displays: Source dropdown (bound to kRouting{N}SourceId), Destination dropdown (bound to kRouting{N}DestId), Amount slider/knob (bound to kRouting{N}AmountId, range -100% to +100%), Curve dropdown (bound to kRouting{N}CurveId), Delete button
  - Add "Add Routing" button (finds first inactive slot, enables it)
  - Implement routing row visibility based on active state
  - All routing parameters must be host-automatable per FR-078
- [ ] T097 [US13] Verify routing matrix displays all active routings and allows add/edit/remove
- [ ] T098 [US13] **Commit completed routing matrix UI work**

**Checkpoint**: User Story 13 (routing matrix UI) should be fully functional and committed

---

## Phase 16: User Story 14 - Macro Knobs UI (Priority: P1)

**Goal**: See four macro knobs in the modulation panel and assign them names

**Independent Test**: Locate macro knobs in UI, verify they are adjustable and produce modulation output

**Requirements**: FR-079 to FR-081

### 16.1 UI - Macros Panel

- [ ] T099 [US14] Create macros UI panel in `F:\projects\iterum\plugins\Disrumpo\resources\editor.uidesc`
  - Add macros section within modulation panel
  - For each of 4 macros: Macro knob (bound to kMacro{N}Id, range 0-1, default 0), label "Macro {N}", Min knob (bound to kMacro{N}MinId, range 0-1, default 0), Max knob (bound to kMacro{N}MaxId, range 0-1, default 1), Curve dropdown (bound to kMacro{N}CurveId, Linear/Exponential/S-Curve/Stepped, default Linear)
  - Each macro knob must display current value (0.0 to 1.0) per FR-081
  - All macro controls must be host-automatable
- [ ] T100 [US14] Verify macros panel displays 4 macros with all controls functional
- [ ] T101 [US14] **Commit completed macros UI work**

**Checkpoint**: User Story 14 (macros UI) should be fully functional and committed

---

## Phase 17: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

### 17.1 Performance Verification

- [ ] T102 [P] Profile CPU usage with 32 active routings and all 12 sources at 44.1kHz/512 samples
- [ ] T103 Verify performance is under 1% CPU overhead per SC-011
- [ ] T104 Optimize if needed (consider SIMD for routing loop, cache locality)

### 17.2 State Persistence

- [ ] T105 Write failing tests for preset save/load in `F:\projects\iterum\plugins\Disrumpo\tests\unit\processor_test.cpp`
  - Test all modulation parameters persist correctly (SC-010)
  - Test all routing configurations persist
- [ ] T106 Implement state serialization in `F:\projects\iterum\plugins\Disrumpo\src\processor\processor.cpp`
  - In `setState()`: deserialize all modulation parameters
  - In `getState()`: serialize all modulation parameters
- [ ] T107 Verify preset save/load tests pass
- [ ] T108 **Commit state persistence work**

### 17.3 Edge Case Handling

- [ ] T109 Write failing tests for edge cases in `F:\projects\iterum\dsp\tests\unit\systems\modulation_engine_test.cpp`
  - Test NaN/infinity source values are clamped to [-1, +1]
  - Test all 32 routing slots filled prevents adding more
  - Test routing with 0% amount has no effect
  - Test modulation panel closed but routings active still process
- [ ] T110 Implement edge case handling in `F:\projects\iterum\dsp\include\krate\dsp\systems\modulation_engine.h`
  - In `getRawSourceValue()`: clamp to valid range, treat NaN as 0.0
  - In `setRouting()`: validate routing index
- [ ] T111 Verify edge case tests pass
- [ ] T112 **Commit edge case handling work**

### 17.4 Documentation

- [ ] T113 [P] Update quickstart.md with final implementation notes
- [ ] T114 [P] Add usage examples to modulation_engine.h header comments
- [ ] T115 [P] Document any deviations from original spec

---

## Phase 18: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 18.1 Architecture Documentation Update

- [ ] T116 **Update `F:\projects\iterum\specs\_architecture_\`** with new components:
  - Add to `layer-0-core.md`: modulation_types.h (enums/structs), modulation_curves.h (pure functions)
  - Add to `layer-2-processors.md`: random_source.h, chaos_mod_source.h, sample_hold_source.h, pitch_follower_source.h, transient_detector.h
  - Add to `layer-3-systems.md`: modulation_engine.h
  - Include: purpose, public API summary, file location, "when to use this"
  - Add usage examples for ModulationEngine
  - Verify no duplicate functionality was introduced
- [ ] T117 **Commit architecture documentation updates**

**Checkpoint**: Architecture documentation reflects all new modulation functionality

---

## Phase 19: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations

### 19.1 Run Clang-Tidy Analysis

- [ ] T118 **Run clang-tidy** on all new/modified source files:
  ```powershell
  # Windows (PowerShell)
  ./tools/run-clang-tidy.ps1 -Target dsp
  ./tools/run-clang-tidy.ps1 -Target disrumpo
  ```

### 19.2 Address Findings

- [ ] T119 **Fix all errors** reported by clang-tidy (blocking issues)
- [ ] T120 **Review warnings** and fix where appropriate (use judgment for DSP code with tight loops)
- [ ] T121 **Document suppressions** if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 20: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 20.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T122 **Review ALL FR-xxx requirements** (FR-001 to FR-093) from spec.md against implementation
  - Verify each FR has corresponding test evidence
  - Mark status for each: MET / NOT MET / PARTIAL / DEFERRED
- [ ] T123 **Review ALL SC-xxx success criteria** (SC-001 to SC-018) and verify measurable targets are achieved
  - SC-001: LFO cycle completes within 0.1% of expected sample count
  - SC-002: LFO tempo sync produces correct period within 0.5%
  - SC-003: All 4 curves produce correct output within 0.01 tolerance
  - SC-004: Bipolar routing inverts within 0.001 tolerance
  - SC-005: Multi-source summation clamps to +1.0
  - SC-006: Envelope follower responds within 10% tolerance
  - SC-007: Chaos output stays in [-1, +1] for 10 seconds
  - SC-008: Pitch follower maps 440Hz within 5% tolerance
  - SC-009: Transient detector fires within 2ms
  - SC-010: Parameters persist across save/load
  - SC-011: CPU overhead under 1% for 32 routings
  - SC-012: User can create LFO routing within 60 seconds
  - SC-013: UI controls respond within 1 frame
  - SC-014: Sources panel displays all 12 sources
  - SC-015: Routing matrix displays all active routings
  - SC-016: Random source passes chi-squared test
  - SC-017: S&H slew within 10% tolerance
  - SC-018: All 6 LFO waveforms produce correct patterns
- [ ] T124 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope
  - [ ] No hardcoded values that should be parameters

### 20.2 Fill Compliance Table in spec.md

- [ ] T125 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
  - For each FR-xxx: provide evidence (test file, line number, commit hash)
  - For each SC-xxx: provide measured result
- [ ] T126 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL
  - If NOT COMPLETE: list gaps explicitly
  - If PARTIAL: document what is missing and why

### 20.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T127 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 21: Final Completion

**Purpose**: Final validation, commit, and completion claim

### 21.1 Final Testing

- [ ] T128 **Run all DSP unit tests**: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [ ] T129 **Run all plugin integration tests**: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure`
- [ ] T130 **Run pluginval** (REQUIRED: Phases 14-16 add Processor integration and UI parameters, which change the plugin's VST3 API surface): `tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Disrumpo.vst3"`
- [ ] T131 **Verify all tests pass**

### 21.2 Final Commit

- [ ] T132 **Commit all spec work** to feature branch `008-modulation-system`
- [ ] T133 **Push branch to remote**

### 21.3 Completion Claim

- [ ] T134 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)
- [ ] T135 **Report completion status** with summary:
  - Total FRs: 93 implemented, X not met (if any)
  - Total SCs: 18 achieved, X not met (if any)
  - Total tasks completed: ~135
  - Parallel opportunities utilized: ~40 tasks marked [P]
  - Independent test criteria met for each user story
  - MVP scope: User Story 1 (LFO modulation) is minimum viable

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-16)**: All depend on Foundational phase completion
  - US1-5 (P1 stories) can proceed in parallel after Foundational
  - US6-9 (P2 stories) can proceed in parallel after Foundational
  - US10-11 are already complete from earlier phases
  - US12-14 (UI stories) depend on US1-5 being complete (need DSP backend)
- **Polish (Phase 17)**: Depends on all desired user stories being complete
- **Documentation (Phase 18)**: Depends on Polish completion
- **Static Analysis (Phase 19)**: Depends on all code being complete
- **Verification (Phase 20)**: Depends on Static Analysis completion
- **Final Completion (Phase 21)**: Depends on Verification completion

### User Story Dependencies

- **US1 (LFO)**: Can start after Foundational - No dependencies on other stories
- **US2 (Routing)**: Can start after Foundational - Integrates with US1 but independently testable
- **US3 (EnvFollower)**: Can start after Foundational - Independent
- **US4 (Macros)**: Can start after Foundational - Independent
- **US5 (Random)**: Can start after Foundational - Independent
- **US6 (Chaos)**: Can start after Foundational - Independent
- **US7 (S&H)**: Can start after Foundational - May reference LFO but independent
- **US8 (PitchFollower)**: Can start after Foundational - Independent
- **US9 (Transient)**: Can start after Foundational - Independent
- **US10 (Curves)**: Complete from Foundational phase
- **US11 (Bipolar)**: Complete from Routing phase
- **US12 (Sources UI)**: Depends on US1-9 DSP being complete
- **US13 (Routing UI)**: Depends on US2 Routing DSP being complete
- **US14 (Macros UI)**: Depends on US4 Macros DSP being complete

### Parallel Opportunities

- **Phase 1 Setup**: All tasks marked [P] can run in parallel (T002-T006)
- **Phase 2 Foundational**: T008-T012 (types) can run parallel to T013-T018 (curves)
- **Phase 3-9 DSP**: Once Foundational completes, all DSP user stories (US1-US9) can start in parallel if team capacity allows
- **Phase 14-16 UI**: US12/US13/US14 can be worked in parallel once their DSP dependencies are met
- **Within each story**: Tasks marked [P] (tests, models) can run in parallel

---

## Implementation Strategy

### MVP First (User Stories 1-2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (LFO modulation)
4. Complete Phase 4: User Story 2 (Routing matrix)
5. **STOP and VALIDATE**: Test US1+US2 independently
6. Deploy/demo if ready (basic LFO modulation is usable)

### Incremental Delivery

1. Setup + Foundational → Foundation ready
2. Add US1 (LFO) → Test independently → Basic modulation working
3. Add US2 (Routing) → Test independently → Flexible routing available
4. Add US3 (EnvFollower) → Test independently → Audio-reactive modulation
5. Add US4 (Macros) → Test independently → Performance control
6. Add US5 (Random) → Test independently → Experimental textures
7. Add US6-9 (Advanced sources) → Test independently → Full modulation palette
8. Add US12-14 (UI) → Complete user-facing feature
9. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together (Phase 1-2)
2. Once Foundational is done:
   - Developer A: US1 (LFO) + US2 (Routing)
   - Developer B: US3 (EnvFollower) + US4 (Macros) + US5 (Random)
   - Developer C: US6 (Chaos) + US7 (S&H) + US8 (PitchFollower) + US9 (Transient)
3. When DSP complete:
   - Developer A: US12 (Sources UI)
   - Developer B: US13 (Routing UI)
   - Developer C: US14 (Macros UI)
4. Stories complete and integrate independently

---

## Summary

**Total Tasks**: 135
**Parallel Tasks**: ~40 marked [P]
**User Stories**: 14 (9 require implementation, 2 already complete, 3 UI-only)
**Test Tasks**: ~45 (test-first for every component)
**Implementation Tasks**: ~60 (DSP + plugin integration + UI)
**Verification Tasks**: ~15 (cross-platform, clang-tidy, requirements)
**Documentation Tasks**: ~5

**Critical Path**:
1. Setup (Phase 1) → 7 tasks
2. Foundational (Phase 2) → 11 tasks (BLOCKING)
3. User Stories (Phase 3-16) → ~90 tasks (can parallelize)
4. Polish (Phase 17) → ~11 tasks
5. Documentation (Phase 18) → 2 tasks
6. Static Analysis (Phase 19) → 4 tasks
7. Verification (Phase 20) → 6 tasks
8. Final Completion (Phase 21) → 8 tasks

**Estimated Implementation Time** (single developer, sequential):
- Setup + Foundational: 2-3 days
- DSP User Stories (US1-US9): 8-12 days (1-2 days per story)
- UI User Stories (US12-US14): 3-4 days
- Polish + Verification: 2-3 days
- **Total**: ~15-22 days (3-4 weeks)

**With Parallel Team** (3 developers):
- Setup + Foundational: 2-3 days (sequential)
- DSP User Stories: 4-5 days (parallel)
- UI User Stories: 2 days (parallel)
- Polish + Verification: 2-3 days (sequential)
- **Total**: ~10-13 days (2 weeks)
