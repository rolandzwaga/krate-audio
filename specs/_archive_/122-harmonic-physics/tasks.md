# Tasks: Harmonic Physics (122)

**Feature Branch**: `122-harmonic-physics` | **Plugin**: Innexus
**Input**: Design documents from `/specs/122-harmonic-physics/`
**Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each milestone.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow.

### Required Steps for EVERY Task

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Build**: `"$CMAKE" --build build/windows-x64-release --config Release --target innexus_tests`
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

### Cross-Platform Compatibility Check (Mandatory After Each Phase)

The VST3 SDK enables `-ffast-math` globally. After writing tests, verify:

- If any test file uses `std::isnan()`, `std::isfinite()`, or `std::isinf()` - add that file to the `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt`
- Use `Approx().margin()` for floating-point comparisons, not exact equality

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Add parameter IDs, register new test files in CMake, and ensure the build scaffold is ready before any story-specific code is written.

- [X] T001 Add harmonic physics parameter IDs to `plugins/innexus/src/plugin_ids.h`: `kWarmthId = 700`, `kCouplingId = 701`, `kStabilityId = 702`, `kEntropyId = 703` (satisfies FR-023)
- [X] T002 Register new test files in `plugins/innexus/tests/CMakeLists.txt`: `unit/processor/test_harmonic_physics.cpp`, `integration/test_harmonic_physics_integration.cpp`, `unit/vst/test_state_v7.cpp`
- [X] T003 Create empty test stub `plugins/innexus/tests/unit/processor/test_harmonic_physics.cpp` with a single failing placeholder test to confirm CMake wires it correctly
- [X] T004 Create empty test stub `plugins/innexus/tests/integration/test_harmonic_physics_integration.cpp` with a single failing placeholder test
- [X] T005 Create empty test stub `plugins/innexus/tests/unit/vst/test_state_v7.cpp` with a single failing placeholder test
- [X] T006 Build `innexus_tests` target and confirm new test files compile: `"$CMAKE" --build build/windows-x64-release --config Release --target innexus_tests`

**Checkpoint**: Build succeeds. New test files are registered and stubbed. Parameter IDs 700-703 exist in plugin_ids.h.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Create the `HarmonicPhysics` class skeleton (header-only) with correct structure but stub implementations. This file must exist before any story-specific implementation begins, because all three user stories add methods to the same class.

- [X] T007 Create `plugins/innexus/src/dsp/harmonic_physics.h` with `AgentState` struct and `HarmonicPhysics` class skeleton (all public methods declared, private members declared, all method bodies are no-ops or stubs)
- [X] T008 Add `#include "dsp/harmonic_physics.h"` to `plugins/innexus/src/processor/processor.h` and declare the `HarmonicPhysics harmonicPhysics_` member and `void applyHarmonicPhysics() noexcept` method
- [X] T009 Add 4 atomic members to `plugins/innexus/src/processor/processor.h`: `std::atomic<float> warmth_{0.0f}`, `coupling_{0.0f}`, `stability_{0.0f}`, `entropy_{0.0f}`
- [X] T010 Add 4 `OnePoleSmoother` members to `plugins/innexus/src/processor/processor.h`: `warmthSmoother_`, `couplingSmoother_`, `stabilitySmoother_`, `entropySmoother_`
- [X] T011 Build `innexus_tests` and confirm zero compilation errors before any implementation work begins

**Checkpoint**: Project compiles cleanly. `HarmonicPhysics` skeleton exists. `Processor` declares all new members. All test stubs register. No user story work has started yet.

---

## Phase 3: User Story 1 - Nonlinear Energy Mapping (Warmth) (Priority: P1)

**Goal**: Deliver tanh-based soft saturation of harmonic amplitudes. Warmth = 0.0 is bit-exact bypass. Warmth = 1.0 compresses dominant partials and boosts quiet ones. Zero input produces zero output. No energy is created.

**Independent Test**: Load any analysis frame, apply warmth at values 0.0, 0.5, 1.0, verify bypass and compression behavior without running coupling or dynamics.

**Requirements satisfied**: FR-001, FR-002, FR-003, FR-004, FR-005, SC-001 (warmth portion), SC-003, SC-007 (no new warnings)

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> Tests must be written and FAIL before any warmth implementation code is written.

- [X] T012 [US1] Write warmth bypass test in `plugins/innexus/tests/unit/processor/test_harmonic_physics.cpp`: given a frame with varied amplitudes and Warmth = 0.0, output amplitudes are bit-exact equal to input amplitudes (FR-002, SC-001)
- [X] T013 [US1] Write warmth compression test: given a frame with one dominant partial at 0.9 and several quiet partials at 0.1, Warmth = 1.0 reduces dominant and relatively boosts quiet partials (FR-001, FR-003)
- [X] T014 [US1] Write peak-to-average ratio test: given a frame with one partial at 10x the average, Warmth = 1.0 reduces peak-to-average ratio by at least 50% (SC-003)
- [X] T015 [US1] Write energy non-increase test: for any warmth value, output RMS must not exceed input RMS (FR-003)
- [X] T016 [US1] Write zero-frame safety test: all-zero input with any warmth produces all-zero output, no NaN (FR-004)
- [X] T017 [US1] Build and run tests to confirm all new warmth tests FAIL: `build/windows-x64-release/bin/Release/innexus_tests.exe "HarmonicPhysics*" 2>&1 | tail -10`

### 3.2 Implementation for User Story 1

- [X] T018 [US1] Implement `HarmonicPhysics::applyWarmth()` in `plugins/innexus/src/dsp/harmonic_physics.h` using formula `amp_out[i] = tanh(drive * amp[i]) / tanh(drive)` with `drive = exp(warmth_ * ln(8.0f))`, early-out when `warmth_ == 0.0f` (FR-001, FR-002, FR-004)
- [X] T019 [US1] Implement `HarmonicPhysics::setWarmth(float)` in `plugins/innexus/src/dsp/harmonic_physics.h` (FR-005 preparation)
- [X] T020 [US1] Update `HarmonicPhysics::processFrame()` in `plugins/innexus/src/dsp/harmonic_physics.h` to call `applyWarmth()` in chain position (after coupling, before dynamics - use stubs for the others)
- [X] T021 [US1] Add warmth parameter handling in `plugins/innexus/src/processor/processor.cpp` `processParameterChanges()`: case `kWarmthId` stores to `warmth_` atomic (FR-005)
- [X] T022 [US1] Add smoother setup for warmth in `plugins/innexus/src/processor/processor.cpp` `setupProcessing()`: `warmthSmoother_.configure(kDefaultSmoothingTimeMs, sampleRate_)` and `harmonicPhysics_.prepare(sampleRate_, hopSize)` (FR-005)
- [X] T023 [US1] Add per-block smoother update for warmth in `plugins/innexus/src/processor/processor.cpp` `process()`: `warmthSmoother_.setTarget(warmth_.load(std::memory_order_relaxed))` and `warmthSmoother_.advanceSamples(numSamples)` (FR-005)
- [X] T024 [US1] Implement `Processor::applyHarmonicPhysics()` in `plugins/innexus/src/processor/processor.cpp`: sets all 4 smoother values on `harmonicPhysics_` and calls `harmonicPhysics_.processFrame(morphedFrame_)` (start with warmth only, others will follow in later phases)
- [X] T025a [US1] Audit the exact number of `oscillatorBank_.loadFrame()` call sites in `plugins/innexus/src/processor/processor.cpp` before wiring: run `grep -n "oscillatorBank_.loadFrame" plugins/innexus/src/processor/processor.cpp` and record actual line numbers. If the count differs from the 7 sites listed in plan.md (approx lines 806, 859, 1017, 1072, 1141, 1457, 1497), update plan.md and T025 to reflect the actual count before proceeding.
- [X] T025 [US1] Wire `applyHarmonicPhysics()` immediately before every `oscillatorBank_.loadFrame()` call site found in T025a in `plugins/innexus/src/processor/processor.cpp` (FR-020, FR-021)
- [X] T026 [US1] Register `kWarmthId` parameter in `plugins/innexus/src/controller/controller.cpp` `initialize()` as `RangeParameter` with range [0.0, 1.0], default 0.0, display "%" (FR-024)
- [X] T027 [US1] Increment state version from 6 to 7 in `plugins/innexus/src/processor/processor.cpp` `getState()` and add warmth float to serialization (FR-025)
- [X] T028 [US1] Add `if (version >= 7)` guard in `plugins/innexus/src/processor/processor.cpp` `setState()` to read warmth with default 0.0 if not present (FR-025)
- [X] T029 [US1] Add warmth state loading in `plugins/innexus/src/controller/controller.cpp` `setComponentState()` with `if (version >= 7)` guard (FR-025)
- [X] T030 [US1] Add smoother snap and physics reset in `Processor::setActive(true)` path in `plugins/innexus/src/processor/processor.cpp`: snap warmth smoother to the current atomic value (`warmthSmoother_.snapTo(warmth_.load(std::memory_order_relaxed))`), call `harmonicPhysics_.reset()`. Do NOT snap to hardcoded 0.0 — snapping to the current value prevents a transient jump if the plugin reactivates while a non-zero parameter is set.

### 3.3 Build and Verify

- [X] T031 [US1] Build `innexus_tests` and fix all compiler warnings (zero warnings policy, SC-007): `"$CMAKE" --build build/windows-x64-release --config Release --target innexus_tests`
- [X] T032 [US1] Run warmth unit tests and confirm all pass: `build/windows-x64-release/bin/Release/innexus_tests.exe "HarmonicPhysics*Warmth*" 2>&1 | tail -5`
- [X] T033 [US1] Run full innexus test suite and confirm no regressions: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T034 [US1] Check if `test_harmonic_physics.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` - if yes, add it to the `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt`

### 3.5 Commit

- [X] T035 [US1] Commit completed User Story 1 (Warmth) work to branch `122-harmonic-physics`

**Checkpoint**: Warmth is fully functional, tested, and committed. Coupling and Dynamics parameters are wired but pass through at 0.0.

---

## Phase 4: User Story 2 - Harmonic Coupling (Priority: P2)

**Goal**: Deliver nearest-neighbor energy sharing between harmonics. Coupling = 0.0 is bit-exact bypass. Energy is exactly conserved (sum-of-squares). Boundary partials handled safely. Only amplitudes are modified.

**Independent Test**: Create a frame with one isolated partial, apply coupling, verify neighbors receive energy while sum-of-squares is conserved.

**Requirements satisfied**: FR-006, FR-007, FR-008, FR-009, FR-010, FR-011, SC-001 (coupling portion), SC-002, SC-007

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> Tests must be written and FAIL before any coupling implementation code is written.

- [X] T036 [US2] Write coupling bypass test in `plugins/innexus/tests/unit/processor/test_harmonic_physics.cpp`: Coupling = 0.0 produces bit-exact output (FR-007, SC-001)
- [X] T037 [US2] Write neighbor spread test: single partial at index 5 with amplitude 1.0, Coupling = 0.5 - partials 4 and 6 receive energy, partial 5 amplitude is reduced (FR-006)
- [X] T038 [US2] Write energy conservation test over 100+ frames: sum-of-squares of output equals sum-of-squares of input within 0.001% tolerance for all coupling values (FR-008, SC-002)
- [X] T039 [US2] Write boundary test: partial at index 0 with coupling applied does not access out-of-bounds, energy still conserved (FR-009)
- [X] T040 [US2] Write frequency preservation test: apply coupling and verify all partial frequencies, phases, and non-amplitude fields are unchanged (FR-010)
- [X] T041 [US2] Build and run tests to confirm all new coupling tests FAIL: `build/windows-x64-release/bin/Release/innexus_tests.exe "HarmonicPhysics*Coupling*" 2>&1 | tail -10`

### 4.2 Implementation for User Story 2

- [X] T042 [US2] Implement `HarmonicPhysics::applyCoupling()` in `plugins/innexus/src/dsp/harmonic_physics.h`: read partials into a temporary buffer, apply nearest-neighbor blend using coupling weight, normalize by sum-of-squares to preserve energy, early-out when `coupling_ == 0.0f` (FR-006, FR-007, FR-008, FR-009, FR-010)
- [X] T043 [US2] Implement `HarmonicPhysics::setCoupling(float)` in `plugins/innexus/src/dsp/harmonic_physics.h`
- [X] T044 [US2] Confirm `HarmonicPhysics::processFrame()` calls `applyCoupling()` before `applyWarmth()` (order: Coupling -> Warmth -> Dynamics) (FR-020)
- [X] T045 [US2] Add coupling parameter handling in `plugins/innexus/src/processor/processor.cpp` `processParameterChanges()`: case `kCouplingId` stores to `coupling_` atomic (FR-011)
- [X] T046 [US2] Add smoother setup for coupling in `plugins/innexus/src/processor/processor.cpp` `setupProcessing()`: `couplingSmoother_.configure(kDefaultSmoothingTimeMs, sampleRate_)` (FR-011)
- [X] T047 [US2] Add per-block smoother update for coupling in `plugins/innexus/src/processor/processor.cpp` `process()`: `couplingSmoother_.setTarget` and `couplingSmoother_.advanceSamples` (FR-011)
- [X] T048 [US2] Update `Processor::applyHarmonicPhysics()` in `plugins/innexus/src/processor/processor.cpp` to pass `couplingSmoother_.getCurrentValue()` to `harmonicPhysics_.setCoupling()`
- [X] T049 [US2] Register `kCouplingId` parameter in `plugins/innexus/src/controller/controller.cpp` as `RangeParameter` with range [0.0, 1.0], default 0.0, display "%" (FR-024)
- [X] T050 [US2] Append coupling float to `getState()` serialization in `plugins/innexus/src/processor/processor.cpp` (FR-025)
- [X] T051 [US2] Read coupling float in `setState()` in `plugins/innexus/src/processor/processor.cpp` within the `if (version >= 7)` block (FR-025)
- [X] T052 [US2] Load coupling in `plugins/innexus/src/controller/controller.cpp` `setComponentState()` within `if (version >= 7)` guard (FR-025)
- [X] T053 [US2] Add coupling smoother snap to the `setActive(true)` path in `plugins/innexus/src/processor/processor.cpp`: `couplingSmoother_.snapTo(coupling_.load(std::memory_order_relaxed))`. Do NOT snap to hardcoded 0.0 — snap to the current atomic value to avoid transient jumps on reactivation.

### 4.3 Build and Verify

- [X] T054 [US2] Build `innexus_tests` and fix all compiler warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target innexus_tests`
- [X] T055 [US2] Run coupling unit tests and confirm all pass: `build/windows-x64-release/bin/Release/innexus_tests.exe "HarmonicPhysics*Coupling*" 2>&1 | tail -5`
- [X] T056 [US2] Run full innexus test suite and confirm no regressions: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T057 [US2] Verify `test_harmonic_physics.cpp` has correct `-fno-fast-math` annotation in `plugins/innexus/tests/CMakeLists.txt` if any IEEE 754 functions are used

### 4.5 Commit

- [X] T058 [US2] Commit completed User Story 2 (Coupling) work to branch `122-harmonic-physics`

**Checkpoint**: Coupling is fully functional, tested, and committed. Warmth and Coupling compose correctly (Coupling -> Warmth).

---

## Phase 5: User Story 3 - Harmonic Dynamics Agent System (Priority: P3)

**Goal**: Deliver per-partial stateful processing with inertia (Stability) and decay (Entropy). Stability = 0.0, Entropy = 0.0 is bit-exact bypass. High stability resists sudden amplitude changes. High entropy causes unreinforced harmonics to fade quickly. Persistence grows for stable partials and decays for unstable ones. Reset clears all state cleanly.

**Independent Test**: Feed sequences of harmonic frames, verify that stability produces inertia (< 5% change with Stability = 1.0 and 100% input change), entropy causes decay to < 1% in 10 frames, and reset clears all state.

**Requirements satisfied**: FR-012, FR-013, FR-014, FR-015, FR-016, FR-017, FR-018, FR-019, SC-001 (dynamics portion), SC-004, SC-005, SC-007

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> Tests must be written and FAIL before any dynamics implementation code is written.

- [X] T059 [US3] Write dynamics bypass test in `plugins/innexus/tests/unit/processor/test_harmonic_physics.cpp`: Stability = 0.0 and Entropy = 0.0, output tracks input exactly (FR-013, SC-001)
- [X] T060 [US3] Write stability inertia test: Stability = 1.0, sudden 100% amplitude change in input - output changes by less than 5% on the first frame after the change (FR-014, SC-004)
- [X] T061 [US3] Write entropy decay test: Entropy = 1.0, zero input amplitudes after an initial frame - agent amplitudes decay to below 1% of initial within 10 frames (FR-015, SC-005)
- [X] T062 [US3] Write entropy infinite sustain test: Entropy = 0.0, stable input then zero input - agent amplitudes persist indefinitely (FR-015)
- [X] T063 [US3] Write persistence growth test: given small amplitude deltas for many successive frames, persistence value grows toward 1.0 (FR-016)
- [X] T064 [US3] Write persistence decay test: given a dramatic amplitude change, persistence decays immediately, making the partial more responsive (FR-016)
- [X] T065 [US3] Write reset test: call `reset()` on dynamics processor, verify all agent amplitude, velocity, persistence, and energyShare arrays are zeroed (FR-017)
- [X] T066 [US3] Write first-frame initialization test: after reset, first `processFrame()` initializes agent amplitudes from input (no ramp-from-zero artifact) (FR-017)
- [X] T067 [US3] Write energy budget conservation test: agent sum-of-squares that exceeds `globalAmplitude^2` is normalized down; step is skipped when `globalAmplitude == 0.0` (FR-012)
- [X] T068 [US3] Build and run tests to confirm all new dynamics tests FAIL: `build/windows-x64-release/bin/Release/innexus_tests.exe "HarmonicPhysics*Dynamics*" 2>&1 | tail -10`

### 5.2 Implementation for User Story 3

- [X] T069 [US3] Implement `AgentState` struct in `plugins/innexus/src/dsp/harmonic_physics.h` with four `std::array<float, kMaxPartials>` arrays: `amplitude`, `velocity`, `persistence`, `energyShare` (FR-012)
- [X] T070 [US3] Implement `HarmonicPhysics::prepare(double sampleRate, int hopSize)` in `plugins/innexus/src/dsp/harmonic_physics.h`: derive timing constants from the `hopSize` argument so that persistence behavior scales with analysis rate (FR-018). Target: `persistenceGrowthRate_ = 1.0f / 20.0f` (persistence reaches 1.0 after ~20 stable frames) and `persistenceDecayFactor_ = 0.5f` (halves per unstable frame). These numeric targets are expressed as functions of hopSize per FR-016's requirement that constants "remain perceptually consistent across sample rates". (FR-016, FR-018)
- [X] T071 [US3] Implement `HarmonicPhysics::reset()` in `plugins/innexus/src/dsp/harmonic_physics.h`: zero all four `AgentState` arrays and set `firstFrame_ = true` (FR-017)
- [X] T072 [US3] Implement `HarmonicPhysics::applyDynamics()` in `plugins/innexus/src/dsp/harmonic_physics.h`: first-frame copy path, per-partial inertia/velocity update controlled by stability and persistence, entropy decay, persistence grow/decay, energy budget normalization via `sqrt(energyBudget / totalEnergy)`, early-out when stability == 0.0 and entropy == 0.0 (FR-012 through FR-016)
- [X] T073 [US3] Implement `HarmonicPhysics::setStability(float)` and `HarmonicPhysics::setEntropy(float)` in `plugins/innexus/src/dsp/harmonic_physics.h` (FR-019 preparation)
- [X] T074 [US3] Confirm `HarmonicPhysics::processFrame()` calls `applyDynamics()` last (after coupling and warmth) (FR-020)
- [X] T075 [US3] Add stability and entropy parameter handling in `plugins/innexus/src/processor/processor.cpp` `processParameterChanges()`: cases `kStabilityId` and `kEntropyId` store to atomics (FR-019)
- [X] T076 [US3] Add smoother setup for stability and entropy in `plugins/innexus/src/processor/processor.cpp` `setupProcessing()`: `stabilitySmoother_.configure(...)` and `entropySmoother_.configure(...)` (FR-019)
- [X] T077 [US3] Add per-block smoother updates for stability and entropy in `plugins/innexus/src/processor/processor.cpp` `process()`: setTarget and advanceSamples for both (FR-019)
- [X] T078 [US3] Update `Processor::applyHarmonicPhysics()` in `plugins/innexus/src/processor/processor.cpp` to pass stability and entropy smoother current values to `harmonicPhysics_`
- [X] T079 [US3] Register `kStabilityId` and `kEntropyId` parameters in `plugins/innexus/src/controller/controller.cpp` as `RangeParameter` with range [0.0, 1.0], default 0.0, display "%" (FR-024)
- [X] T080 [US3] Append stability and entropy floats to `getState()` serialization in `plugins/innexus/src/processor/processor.cpp` (FR-025)
- [X] T081 [US3] Read stability and entropy floats in `setState()` in `plugins/innexus/src/processor/processor.cpp` within the `if (version >= 7)` block with defaults 0.0 (FR-025)
- [X] T082 [US3] Load stability and entropy in `plugins/innexus/src/controller/controller.cpp` `setComponentState()` within `if (version >= 7)` guard (FR-025)
- [X] T083 [US3] Add stability and entropy smoother snaps to the `setActive(true)` path in `plugins/innexus/src/processor/processor.cpp`: `stabilitySmoother_.snapTo(stability_.load(std::memory_order_relaxed))` and `entropySmoother_.snapTo(entropy_.load(std::memory_order_relaxed))`. Do NOT snap to hardcoded 0.0 — snap to current atomic values to avoid transient jumps on reactivation.

### 5.3 Build and Verify

- [X] T084 [US3] Build `innexus_tests` and fix all compiler warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target innexus_tests`
- [X] T085 [US3] Run dynamics unit tests and confirm all pass: `build/windows-x64-release/bin/Release/innexus_tests.exe "HarmonicPhysics*Dynamics*" 2>&1 | tail -5`
- [X] T086 [US3] Run combined warmth+coupling+dynamics tests to verify full chain composition (FR-022): `build/windows-x64-release/bin/Release/innexus_tests.exe "HarmonicPhysics*" 2>&1 | tail -5`
- [X] T087 [US3] Run full innexus test suite and confirm no regressions: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T088 [US3] Verify `test_harmonic_physics.cpp` has correct `-fno-fast-math` annotation in `plugins/innexus/tests/CMakeLists.txt` if any IEEE 754 functions are used

### 5.5 Commit

- [X] T089 [US3] Commit completed User Story 3 (Dynamics) work to branch `122-harmonic-physics`

**Checkpoint**: All three processors are functional and tested. Full chain (Coupling -> Warmth -> Dynamics) composes correctly. All 4 parameters registered, smoothed, and saved.

---

## Phase 6: Integration & State Persistence

**Purpose**: Full-pipeline integration tests verifying the processor wires physics correctly, and state v7 backward-compatibility tests.

**Requirements satisfied**: FR-020, FR-021, FR-022, FR-023, FR-024, FR-025, SC-001 (integration check)

### 6.1 Integration Tests (Write FIRST - Must FAIL)

- [X] T090 Write integration test in `plugins/innexus/tests/integration/test_harmonic_physics_integration.cpp`: instantiate real `Processor`, send parameter changes via `processParameterChanges()`, call `process()` with a synthetic audio block containing a new analysis frame, verify warmth/coupling/dynamics effects appear in the oscillator bank's received frame
- [X] T091 Write parameter default integration test: load plugin with no state, verify all 4 parameters are 0.0 and produce bit-exact bypass (FR-024 default check, SC-001)
- [X] T092 Write state v7 save/load roundtrip test in `plugins/innexus/tests/unit/vst/test_state_v7.cpp`: set warmth=0.7, coupling=0.3, stability=0.5, entropy=0.2, call `getState()`, call `setState()` with the saved stream, verify parameter values restored correctly (FR-025)
- [X] T093 Write v6-to-v7 backward compatibility test in `plugins/innexus/tests/unit/vst/test_state_v7.cpp`: load a v6 state stream into v7 code, verify all 4 new parameters default to 0.0 (FR-025)
- [X] T094 Build and run integration test stubs to confirm they FAIL: `build/windows-x64-release/bin/Release/innexus_tests.exe "HarmonicPhysics*Integration*" 2>&1 | tail -10`

### 6.2 Implementation (Wire Remaining Integration Points)

- [X] T095 Verify all 7 `oscillatorBank_.loadFrame()` call sites in `plugins/innexus/src/processor/processor.cpp` have `applyHarmonicPhysics()` called immediately before (audit check - should be done from Phase 3, but verify here) (FR-020, FR-021)
- [X] T096 Verify the `harmonicPhysics_.reset()` call is present in the note-on handler or wherever `morphedFrame_` is reset in `plugins/innexus/src/processor/processor.cpp` (risk mitigation from plan.md)

### 6.3 Build and Verify

- [X] T097 Build `innexus_tests` with zero warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target innexus_tests`
- [X] T098 Run integration tests: `build/windows-x64-release/bin/Release/innexus_tests.exe "HarmonicPhysics*Integration*" 2>&1 | tail -5`
- [X] T099 Run state persistence tests: `build/windows-x64-release/bin/Release/innexus_tests.exe "*StateV7*" 2>&1 | tail -5` (Note: test names in `test_state_v7.cpp` MUST include "StateV7" for this filter to match. If tests are named differently, adjust the filter to match the actual test name prefix used in that file.)
- [X] T100 Run full innexus test suite and confirm no regressions: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`

### 6.4 Commit

- [X] T101 Commit integration and state persistence tests to branch `122-harmonic-physics`

**Checkpoint**: Integration tests and state persistence tests pass. Full processor pipeline wires physics correctly. State version 7 round-trips cleanly.

---

## Phase 7: Performance Benchmark

**Purpose**: Verify SC-006 - combined CPU overhead of all three processors < 0.5% of a single core at 48kHz with 48 partials.

- [X] T102 Add a `[.perf]`-tagged benchmark test in `plugins/innexus/tests/unit/processor/test_harmonic_physics.cpp`: create a `HarmonicPhysics` instance with all params at 1.0 and 48 active partials, call `processFrame()` in a tight loop for ~1 second of frames at 48kHz / 512 hop = ~94 frames/sec, measure time per frame and assert < expected budget (SC-006)
- [X] T103 Build and run the benchmark: `build/windows-x64-release/bin/Release/innexus_tests.exe "[.perf]" 2>&1 | tail -20`
- [X] T104 If benchmark exceeds budget, profile and optimize - verify early-out paths trigger for bypass cases (warmth_ == 0.0, coupling_ == 0.0, stability_ == 0.0 && entropy_ == 0.0)
- [X] T105 Commit benchmark test to branch `122-harmonic-physics`

**Checkpoint**: Performance verified. SC-006 confirmed at < 0.5% CPU per core.

---

## Phase 8: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification (Constitution workflow step 6).

- [X] T106 Generate ninja build for clang-tidy (from VS Developer PowerShell): `cmake --preset windows-ninja` (if not already current)
- [X] T107 Run clang-tidy on Innexus target: `./tools/run-clang-tidy.ps1 -Target innexus -BuildDir build/windows-ninja`
- [X] T108 Fix all errors reported by clang-tidy in `plugins/innexus/src/dsp/harmonic_physics.h` and `plugins/innexus/src/processor/processor.cpp`
- [X] T109 Fix all clang-tidy errors in new test files: `plugins/innexus/tests/unit/processor/test_harmonic_physics.cpp`, `plugins/innexus/tests/integration/test_harmonic_physics_integration.cpp`, `plugins/innexus/tests/unit/vst/test_state_v7.cpp`
- [X] T110 Document any intentional suppressions with `// NOLINT` comment and reason
- [X] T111 Rebuild `innexus_tests` and confirm zero warnings after clang-tidy fixes: `"$CMAKE" --build build/windows-x64-release --config Release --target innexus_tests`
- [X] T112 Commit clang-tidy fixes to branch `122-harmonic-physics`

**Checkpoint**: Static analysis clean. No new warnings. Ready for final compliance check.

---

## Phase 9: Pluginval Verification (MANDATORY)

**Purpose**: Verify plugin passes pluginval at strictness level 5 after integration (SC-008).

- [X] T113 Build the full Innexus plugin: `"$CMAKE" --build build/windows-x64-release --config Release`
- [X] T114 Run pluginval at strictness 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"`
- [X] T115 Fix any pluginval failures (check parameter registration order, state serialization, bus configuration)
- [X] T116 Re-run pluginval and confirm it passes (SC-008)

**Checkpoint**: Plugin validates at strictness level 5.

---

## Phase 10: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion (Constitution Principle XIII).

- [X] T117 Update `specs/_architecture_/` to document the new `HarmonicPhysics` class: purpose, public API summary, file location (`plugins/innexus/src/dsp/harmonic_physics.h`), processing chain order, when to use
- [X] T118 Commit architecture documentation updates to branch `122-harmonic-physics`

**Checkpoint**: Architecture documentation reflects all new components.

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements before claiming completion (Constitution Principle XV).

### 11.1 Requirements Verification

- [X] T119 Open `plugins/innexus/src/dsp/harmonic_physics.h` and verify each FR row with specific line numbers:
  - FR-001: `tanh(drive * amp[i]) / tanh(drive)` formula present, `drive = exp(warmth_ * ln(8.0f))`
  - FR-002: Early-out when `warmth_ == 0.0f` produces bit-exact bypass
  - FR-003: Output RMS cannot exceed input RMS (tanh property)
  - FR-004: All-zero input produces all-zero output (tanh(0) = 0)
  - FR-005: Warmth smoother configured and advances per block
  - FR-006: Nearest-neighbor blend with coupling weight
  - FR-007: Early-out when `coupling_ == 0.0f`
  - FR-008: Sum-of-squares normalization after coupling
  - FR-009: Index 0 and numPartials-1 boundary safe
  - FR-010: Only amplitude modified, frequency/phase/etc. unchanged
  - FR-011: Coupling smoother configured and advances per block
  - FR-012: Energy budget normalization in dynamics (skipped when globalAmplitude == 0)
  - FR-013: Early-out when stability == 0.0 and entropy == 0.0
  - FR-014: Stability controls inertia weighted by persistence
  - FR-015: Entropy controls decay rate
  - FR-016: Persistence grows at growthRate_, decays at decayFactor_, threshold 0.01
  - FR-017: reset() zeros all AgentState arrays and sets firstFrame_ = true
  - FR-018: prepare() computes persistence constants from hopSize
  - FR-019: Stability and entropy smoothers configured and advance per block
- [X] T120 Open `plugins/innexus/src/processor/processor.cpp` and verify integration FRs with specific line numbers:
  - FR-020: applyHarmonicPhysics() called before each oscillatorBank_.loadFrame() (all 7 sites)
  - FR-021: All three processors active in the chain
  - FR-022: Each processor independently bypassable at 0.0
  - FR-023: IDs 700-703 used in plugin_ids.h
  - FR-024: All 4 parameters registered in controller with correct range [0.0,1.0] and default 0.0
  - FR-025: State version 7 serializes and deserializes all 4 params; v6 state loads with 0.0 defaults
- [X] T121 Run each SC success criterion test and record actual measured values:
  - SC-001: Run bypass tests for all three processors, confirm bit-exact pass
  - SC-002: Run energy conservation test, record actual tolerance achieved
  - SC-003: Run peak-to-average ratio test, record actual reduction percentage
  - SC-004: Run stability inertia test, record actual output change percentage
  - SC-005: Run entropy decay test, record actual number of frames to reach < 1%
  - SC-006: Run benchmark, record actual CPU time per frame
  - SC-007: Confirm zero new warnings from build output
  - SC-008: Confirm pluginval passed (from Phase 9)
  - SC-009: Cannot easily automate - see T121a for manual verification documentation
- [X] T121a Write manual verification procedure for SC-009 (zipper noise) in `specs/122-harmonic-physics/spec.md` Implementation Verification section: document the specific steps to verify that sweeping Warmth, Coupling, Stability, and Entropy from 0.0 to 1.0 over 100ms produces no audible stepping artifacts. Steps must include: host/test setup used, parameter sweep rate, what constitutes a pass. Record result in the SC-009 compliance table row.
- [X] T121b Add an inline comment in `plugins/innexus/src/dsp/harmonic_physics.h` at the top of `applyWarmth()` confirming why oversampling is not required (Constitution Principle X): warmth operates on harmonic analysis frame amplitudes (updated ~94x/sec at 48kHz/512-sample hop), not on audio samples. Aliasing from tanh is not applicable at the analysis frame rate.
- [X] T122 Search new code for disqualifying patterns: `grep -r "placeholder\|TODO\|stub\|FIXME" plugins/innexus/src/dsp/harmonic_physics.h` (must be empty)
- [X] T123 Verify no test thresholds were relaxed from spec: SC-002 <= 0.001%, SC-003 >= 50% reduction, SC-004 < 5% change, SC-005 < 1% in 10 frames, SC-006 < 0.5% CPU

### 11.2 Fill Compliance Table in spec.md

- [X] T124 Update `specs/122-harmonic-physics/spec.md` "Implementation Verification" section with compliance status for each FR-xxx and SC-xxx requirement, citing specific file paths and line numbers for every row (no generic "implemented" claims)

### 11.3 Final Test Run and Commit

- [X] T125 Run full innexus test suite one final time: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`
- [X] T126 Commit compliance table update and any remaining fixes to branch `122-harmonic-physics`

**Checkpoint**: Spec implementation honestly assessed. All FRs and SCs verified with concrete evidence. Compliance table filled. Spec is complete.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies - start immediately
- **Phase 2 (Foundational)**: Requires Phase 1 complete - BLOCKS all user stories
- **Phase 3 (US1 Warmth)**: Requires Phase 2 complete
- **Phase 4 (US2 Coupling)**: Requires Phase 3 complete (coupling is added to the same class and same processFrame chain; the 7-site wiring from Phase 3 is reused)
- **Phase 5 (US3 Dynamics)**: Requires Phase 4 complete (dynamics is the last link in the Coupling -> Warmth -> Dynamics chain)
- **Phase 6 (Integration)**: Requires Phase 5 complete (all three processors must exist for integration tests)
- **Phase 7 (Benchmark)**: Requires Phase 6 complete
- **Phase 8 (Clang-Tidy)**: Requires Phase 7 complete
- **Phase 9 (Pluginval)**: Requires Phase 8 complete (clang-tidy fixes may change source)
- **Phase 10 (Docs)**: Requires Phase 9 complete
- **Phase 11 (Verification)**: Requires Phase 10 complete

### User Story Dependencies

- **US1 (Warmth)** does not depend on US2 or US3. Warmth operates on amplitudes independently.
- **US2 (Coupling)** does not depend on US1 for correctness (both stateless), but shares the same class file. Implement sequentially to avoid merge conflicts.
- **US3 (Dynamics)** does not depend on US1 or US2 for correctness, but the full chain (Coupling -> Warmth -> Dynamics) must compose correctly.
- The `applyHarmonicPhysics()` insertion at all 7 sites is done once in Phase 3 and reused for all subsequent phases.

### Within Each User Story

1. Tests FIRST - write and confirm FAIL
2. Implement DSP method in `harmonic_physics.h`
3. Wire into processor (atomic, smoother, processParameterChanges, setupProcessing, process, applyHarmonicPhysics)
4. Register parameter in controller
5. State save/load
6. Build with zero warnings
7. Run tests, confirm pass
8. Cross-platform check (fno-fast-math)
9. Commit

### Key Constraint: Single-File DSP Class

All three user stories write methods into `plugins/innexus/src/dsp/harmonic_physics.h`. This means the user stories CANNOT be parallelized across developers on this file. They must proceed sequentially: US1, then US2, then US3.

---

## Parallel Opportunities

Within each phase, the following tasks can run in parallel where noted:

- **Phase 1**: T001, T002, and T003/T004/T005 are independent
- **Phase 3 Tests**: T012, T013, T014, T015, T016 can be written in parallel (all in the same test file, different test cases)
- **Phase 4 Tests**: T036, T037, T038, T039, T040 can be written in parallel
- **Phase 5 Tests**: T059 through T067 can be written in parallel

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1 (Warmth)
4. STOP and VALIDATE: Warmth is independently testable with no coupling or dynamics
5. Demo the compression effect with a loaded sample

### Incremental Delivery

1. Phase 1+2 complete → HarmonicPhysics skeleton exists, build is green
2. Phase 3 complete → Warmth delivered, tested, committed (MVP)
3. Phase 4 complete → Coupling added on top, both tested, committed
4. Phase 5 complete → Dynamics added, full chain composed, all tested, committed
5. Phase 6-11 → Integration, performance, clang-tidy, pluginval, docs, compliance

---

## Task Count Summary

| Phase | Tasks | Story |
|-------|-------|-------|
| Phase 1: Setup | T001-T006 | 6 tasks |
| Phase 2: Foundational | T007-T011 | 5 tasks |
| Phase 3: US1 Warmth | T012-T035 | 25 tasks (T025a added) |
| Phase 4: US2 Coupling | T036-T058 | 23 tasks |
| Phase 5: US3 Dynamics | T059-T089 | 31 tasks |
| Phase 6: Integration | T090-T101 | 12 tasks |
| Phase 7: Benchmark | T102-T105 | 4 tasks |
| Phase 8: Clang-Tidy | T106-T112 | 7 tasks |
| Phase 9: Pluginval | T113-T116 | 4 tasks |
| Phase 10: Docs | T117-T118 | 2 tasks |
| Phase 11: Verification | T119-T126 | 11 tasks (T121a, T121b added) |
| **Total** | | **130 tasks** |

| Story | Tasks | Requirements |
|-------|-------|-------------|
| US1 Warmth | T012-T035 | FR-001 to FR-005, SC-001, SC-003 |
| US2 Coupling | T036-T058 | FR-006 to FR-011, SC-001, SC-002 |
| US3 Dynamics | T059-T089 | FR-012 to FR-019, SC-001, SC-004, SC-005 |
| Integration | T090-T101 | FR-020 to FR-025 |
| Quality | T102-T126 | SC-006 to SC-009 |

---

## Notes

- `[P]` tasks operate on different files and have no shared write dependencies - can run in parallel
- `[USn]` label maps each task to a specific user story for traceability
- The `HarmonicPhysics` class is header-only (`plugins/innexus/src/dsp/harmonic_physics.h`) - all DSP logic lives there
- State version advances from 6 to 7; use `if (version >= 7)` guards consistently
- All 4 parameters default to 0.0 - this is the bit-exact bypass guarantee
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- NEVER claim completion if ANY requirement is not met - document gaps honestly instead
- The `HarmonicPhysics` class follows the same plugin-local DSP pattern as `HarmonicModulator` and `EvolutionEngine`
