# Tasks: Particle / Swarm Oscillator

**Input**: Design documents from `/specs/028-particle-oscillator/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/particle_oscillator_api.h

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Mandatory Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase 8)
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
             unit/processors/particle_oscillator_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

This check prevents CI failures on macOS/Linux that pass locally on Windows.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Initialize test infrastructure and verify Layer 0 dependencies

- [X] T001 Create test file structure at `dsp/tests/unit/processors/particle_oscillator_test.cpp`
- [X] T002 Add test file to dsp_tests target in `dsp/tests/CMakeLists.txt` (in Layer 2: Processors section)
- [X] T003 Add test file to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (uses detail::isNaN/isInf)
- [X] T004 Verify Layer 0 dependencies available: random.h, grain_envelope.h, pitch_utils.h, math_constants.h, db_utils.h

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core class structure and header skeleton that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T005 Create header skeleton at `dsp/include/krate/dsp/processors/particle_oscillator.h` with namespace, includes, and class declaration
- [X] T006 Define SpawnMode enum (Regular, Random, Burst) in particle_oscillator.h
- [X] T007 Define internal Particle struct with fields: phase, phaseIncrement, baseFrequency, envelopePhase, envelopeIncrement, driftState, driftRange, active
- [X] T008 Define class constants: kMaxParticles (64), kEnvTableSize (256), kNumEnvelopeTypes (6), kMinFrequency (1.0), kMinLifetimeMs (1.0), kMaxLifetimeMs (10000.0), kMaxScatter (48.0), kOutputClamp (2.0)
- [X] T009 Declare all private member variables per data-model.md (particles_ array, configuration state, derived state, processing state, envelope tables)
- [X] T010 Declare all public API methods per contracts/particle_oscillator_api.h
- [X] T011 Verify header compiles without implementation (stub methods with return defaults)

**Checkpoint**: Foundation ready - user story implementation can now begin in sequence (sequential due to cumulative testing needs)

---

## Phase 3: User Story 1 - Basic Pitched Particle Cloud (Priority: P1) - MVP

**Goal**: Produce a pitched, textured tone from particle-based synthesis with basic envelope shaping and normalization

**Independent Test**: Can be fully tested by preparing the oscillator at 44100 Hz, setting frequency to 440 Hz with 8 particles, verifying the output contains energy centered around 440 Hz with bounded amplitude, and confirming the signal is non-silent.

**Requirements**: FR-001, FR-002, FR-003, FR-004, FR-010, FR-011, FR-015, FR-016, FR-017, FR-018, FR-019, FR-020, FR-021
**Success Criteria**: SC-001, SC-007

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T012 [P] [US1] Write test: ParticleOscillator default constructor compiles and can be instantiated (tag: [ParticleOscillator][lifecycle])
- [X] T013 [P] [US1] Write test: isPrepared() returns false before prepare(), true after (tag: [lifecycle])
- [X] T014 [P] [US1] Write test: processBlock() outputs silence (all zeros) before prepare() is called (tag: [lifecycle][output]) (FR-003)
- [X] T015 [P] [US1] Write test: prepare(44100.0) succeeds, reset() clears particles without changing sample rate (tag: [lifecycle])
- [X] T016 [P] [US1] Write test: single particle (density=1, scatter=0, lifetime=100ms) at 440 Hz produces sine wave with THD < 1% (tag: [frequency][output]) (SC-001)
- [X] T017 [P] [US1] Write test: output is bounded within [-1.0, +1.0] for density=8, scatter=0, lifetime=100ms (tag: [output])
- [X] T018 [P] [US1] Write test: setFrequency clamps below 1 Hz to 1 Hz, at/above Nyquist to below Nyquist (tag: [frequency]) (FR-004)
- [X] T019 [P] [US1] Write test: setFrequency with NaN/Inf is sanitized to default 440 Hz (tag: [frequency][edge-cases])
- [X] T020 [P] [US1] Write test: particle with lifetime 100 ms expires within 90-110 ms at 44100 Hz and 96000 Hz (tag: [lifecycle][population]) (SC-007)
- [X] T021 [P] [US1] Write test: output is non-silent (RMS > 0.01) for density=8, centerFreq=440, lifetime=100ms (tag: [output])
- [X] T022 [P] [US1] Write test: spectral analysis shows energy concentrated around 440 Hz for density=8, scatter=0 (tag: [frequency][output])
- [X] T023 [P] [US1] Write test: activeParticleCount() returns 0 before any spawns, increases as particles spawn (tag: [population])

### 3.2 Implementation for User Story 1

- [X] T024 [US1] Implement constructor: initialize all member variables to defaults (centerFrequency_=440, scatter_=0, density_=1, lifetimeMs_=100, spawnMode_=Regular, driftAmount_=0, currentEnvType_=0, prepared_=false, rng_ with seed 12345)
- [X] T025 [US1] Implement prepare(double sampleRate): store sampleRate_, compute nyquist_, precompute all 6 envelope tables using GrainEnvelope::generate() for each GrainEnvelopeType, clear particles_, set prepared_=true
- [X] T026 [US1] Implement reset(): clear all particles (set active=false for all), reset samplesUntilNextSpawn_=0
- [X] T027 [US1] Implement isPrepared(): return prepared_
- [X] T028 [US1] Implement setFrequency(float centerHz): sanitize NaN/Inf to 440, clamp to [kMinFrequency, nyquist_), store in centerFrequency_
- [X] T029 [US1] Implement setDensity(float particles): clamp to [1, kMaxParticles], store in density_, recompute normFactor_ = 1/sqrt(density_)
- [X] T030 [US1] Implement setLifetime(float ms): clamp to [kMinLifetimeMs, kMaxLifetimeMs], store in lifetimeMs_, recompute lifetimeSamples_ and interonsetSamples_
- [X] T031 [US1] Implement spawnParticle() private method: find inactive slot, assign baseFrequency=centerFrequency_, phase=0, envelopePhase=0, compute phaseIncrement and envelopeIncrement, set active=true
- [X] T032 [US1] Implement processParticle(Particle&) private method: if not active return 0; advance phase, advance envelopePhase, if envelopePhase >= 1.0 deactivate; lookup envelope value via GrainEnvelope::lookup(), return sin(kTwoPi * phase) * envelopeValue
- [X] T033 [US1] Implement sanitizeOutput(float x) private static method: check detail::isNaN/isInf -> return 0; clamp to [-kOutputClamp, +kOutputClamp]
- [X] T034 [US1] Implement process() single-sample method: if not prepared return 0; spawn logic (Regular mode only for US1); sum all processParticle() outputs; multiply by normFactor_; sanitize and return
- [X] T035 [US1] Implement processBlock(float* output, size_t numSamples): loop calling process() for each sample
- [X] T036 [US1] Implement activeParticleCount(): count particles where active==true
- [X] T037 [US1] Implement getters: getFrequency(), getDensity(), getLifetime(), getSpawnMode()
- [X] T038 [US1] Implement seed(uint32_t seedValue): call rng_.seed(seedValue)

### 3.3 Verification for User Story 1

- [X] T039 [US1] Build dsp_tests target: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T040 [US1] Run all User Story 1 tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[ParticleOscillator][lifecycle]"`
- [X] T041 [US1] Run frequency tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[ParticleOscillator][frequency]"`
- [X] T042 [US1] Run output tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[ParticleOscillator][output]"`
- [X] T043 [US1] Verify SC-001: THD < 1% for single particle (check test output)
- [X] T044 [US1] Verify SC-007: Timing accuracy within 10% at both 44100 Hz and 96000 Hz (check test output)
- [X] T045 [US1] Fix any compilation errors or test failures

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T046 [US1] **Verify IEEE 754 compliance**: Confirm particle_oscillator_test.cpp is in `-fno-fast-math` list in dsp/tests/CMakeLists.txt (already added in T003)

### 3.5 Commit (MANDATORY)

- [X] T047 [US1] **Commit completed User Story 1 work**: Basic pitched particle cloud with envelope shaping and normalization

**Checkpoint**: User Story 1 should be fully functional, tested, and committed. The oscillator can produce basic pitched tones with multiple particles.

---

## Phase 4: User Story 2 - Dense Granular Cloud Texture (Priority: P2)

**Goal**: Enable dense, evolving cloud textures by adding frequency scatter, wide spectral spread, and high-density support (up to 64 particles)

**Independent Test**: Can be tested by setting density to 48, scatter to 12 semitones, lifetime to 30 ms, and verifying that the output produces broadband spectral content with the expected bandwidth, bounded amplitude, and evolving texture over time.

**Requirements**: FR-005, FR-006, FR-007, FR-009, FR-022
**Success Criteria**: SC-002, SC-004

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T048 [P] [US2] Write test: setFrequencyScatter clamps to [0, 48] semitones (tag: [frequency]) (FR-005)
- [X] T049 [P] [US2] Write test: density=8, scatter=3 semitones produces spectral spread approximately 6 semitones wide (tag: [frequency][population])
- [X] T050 [P] [US2] Write test: density=48, scatter=12 semitones produces broadband spectral content spanning ~24 semitones (tag: [frequency][population]) (US2 acceptance 1)
- [X] T051 [P] [US2] Write test: density=64 (max), scatter=24 semitones, lifetime=20ms produces bounded output [-1.5, +1.5], no NaN/overflow (tag: [output][edge-cases]) (SC-002, US2 acceptance 2)
- [X] T052 [P] [US2] Write test: all 64 particle slots actively cycling at max density (tag: [population])
- [X] T053 [P] [US2] Write test: autocorrelation across non-overlapping 100 ms blocks shows variation (texture evolves) (tag: [output]) (US2 acceptance 1)
- [X] T054 [P] [US2] Write test: changing density from 48 to 4 mid-stream causes texture to thin out gradually over particle lifetime (tag: [population]) (US2 acceptance 3)
- [X] T055 [P] [US2] Write test: at density=16, lifetime=100ms, at least 90% of slots are occupied after 2x lifetime ramp-up (tag: [population]) (SC-004)
- [X] T056 [P] [US2] Write test: particles replace expired particles automatically in Regular mode to maintain target density (tag: [population]) (FR-009)
- [X] T057 [P] [US2] Write test: when all 64 slots occupied and new spawn is due, oldest particle is stolen (replaced) (tag: [population][edge-cases])

### 4.2 Implementation for User Story 2

- [X] T058 [US2] Implement setFrequencyScatter(float semitones): clamp to [0, kMaxScatter], store in scatter_
- [X] T059 [US2] Update setDensity() to recompute interonsetSamples_ when density changes
- [X] T060 [US2] Update spawnParticle() to apply scatter offset: compute random offset in [-scatter_, +scatter_] semitones using rng_.nextFloat(), convert to frequency ratio via semitonesToRatio(), multiply by centerFrequency_, clamp result to [kMinFrequency, nyquist_ - 1.0], store as baseFrequency
- [X] T061 [US2] Implement voice stealing in spawnParticle(): if all slots occupied, find oldest particle (lowest envelopePhase or first inactive from wrap-around search), replace it
- [X] T062 [US2] Update process() to implement Regular spawn scheduler: decrement samplesUntilNextSpawn_, when <= 0 call spawnParticle() and reset counter to interonsetSamples_
- [X] T063 [US2] Ensure prepare() initializes spawn counter: samplesUntilNextSpawn_ = 0 (spawn immediately on first process)

### 4.3 Verification for User Story 2

- [X] T064 [US2] Build dsp_tests target: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T065 [US2] Run frequency scatter tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[ParticleOscillator][frequency]"`
- [X] T066 [US2] Run population tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[ParticleOscillator][population]"`
- [X] T067 [US2] Run edge case tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[ParticleOscillator][edge-cases]"`
- [X] T068 [US2] Verify SC-002: Peak amplitude within [-1.5, +1.5] at max density and scatter (check test output)
- [X] T069 [US2] Verify SC-004: At least 90% occupancy after ramp-up (check test output)
- [X] T070 [US2] Fix any compilation errors or test failures

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T071 [US2] **Verify IEEE 754 compliance**: Confirm no new IEEE 754 issues introduced (already covered by T003)

### 4.5 Commit (MANDATORY)

- [X] T072 [US2] **Commit completed User Story 2 work**: Dense granular cloud texture with scatter and high-density support

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed. The oscillator now supports dense, wide-spectrum cloud textures.

---

## Phase 5: User Story 3 - Spawn Mode Variation (Priority: P3)

**Goal**: Enable control of temporal particle creation patterns via Regular, Random, and Burst spawn modes for different rhythmic and textural qualities

**Independent Test**: Can be tested independently by configuring each spawn mode with the same density and scatter settings, then analyzing the temporal distribution of particle onsets to confirm they match the expected pattern for each mode.

**Requirements**: FR-008, FR-008a
**Success Criteria**: SC-006

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T073 [P] [US3] Write test: setSpawnMode accepts all three modes (Regular, Random, Burst) without error (tag: [population])
- [X] T074 [P] [US3] Write test: Regular mode produces evenly spaced particle onsets (interonset interval ~lifetime/density, variation < 5%) (tag: [population]) (US3 acceptance 1)
- [X] T075 [P] [US3] Write test: Random mode produces stochastic onsets with coefficient of variation > 0.3 (tag: [population]) (US3 acceptance 2)
- [X] T076 [P] [US3] Write test: Burst mode does NOT auto-spawn particles; triggerBurst() spawns all density particles simultaneously (tag: [population]) (US3 acceptance 3)
- [X] T077 [P] [US3] Write test: triggerBurst() is no-op in Regular and Random modes (tag: [population]) (FR-008a)
- [X] T078 [P] [US3] Write test: switching between spawn modes produces no clicks/pops (no sample jump > 0.5) (tag: [population][output]) (SC-006)
- [X] T079 [P] [US3] Write test: switching FROM Burst TO Regular starts auto-spawning immediately (tag: [population])
- [X] T080 [P] [US3] Write test: switching TO Burst stops auto-spawning, existing particles continue (tag: [population])

### 5.2 Implementation for User Story 3

- [X] T081 [US3] Implement setSpawnMode(SpawnMode mode): store mode in spawnMode_, reset samplesUntilNextSpawn_ = 0
- [X] T082 [US3] Update process() spawn scheduler to support Random mode: when samplesUntilNextSpawn_ <= 0, compute next interval as interonsetSamples_ * (0.5 + rng_.nextUnipolar()) for exponential-like distribution
- [X] T083 [US3] Update process() spawn scheduler to support Burst mode: skip auto-spawn logic entirely when spawnMode_ == Burst
- [X] T084 [US3] Implement triggerBurst(): if spawnMode_ == Burst, loop and spawn particles up to density_ count; else no-op
- [X] T085 [US3] Verify mode transitions: setSpawnMode() lets existing particles continue naturally (no kill-all needed)

### 5.3 Verification for User Story 3

- [X] T086 [US3] Build dsp_tests target: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T087 [US3] Run spawn mode tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[ParticleOscillator][population]"`
- [X] T088 [US3] Verify SC-006: No clicks/pops on mode transitions (check test output)
- [X] T089 [US3] Fix any compilation errors or test failures

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T090 [US3] **Verify IEEE 754 compliance**: Confirm no new IEEE 754 issues introduced (already covered by T003)

### 5.5 Commit (MANDATORY)

- [X] T091 [US3] **Commit completed User Story 3 work**: Spawn mode variation (Regular, Random, Burst)

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed. The oscillator now supports all three spawn modes for temporal variation.

---

## Phase 6: User Story 4 - Frequency Drift (Priority: P4)

**Goal**: Add per-particle frequency drift (low-pass filtered random walk) so each particle's frequency gradually wanders over its lifetime, creating organic motion and evolving timbre

**Independent Test**: Can be tested by comparing spectral snapshots at the start and end of a long-lifetime particle with drift enabled versus disabled. With drift enabled, the frequency distribution should broaden over time.

**Requirements**: FR-012, FR-013, FR-014
**Success Criteria**: SC-003, SC-005

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T092 [P] [US4] Write test: setDriftAmount clamps to [0, 1] (tag: [drift])
- [X] T093 [P] [US4] Write test: drift=0 produces constant particle frequency for entire lifetime (spectral peak does not move) (tag: [drift]) (US4 acceptance 1)
- [X] T094 [P] [US4] Write test: drift=1.0 (max) produces frequency wandering within range proportional to scatter setting (tag: [drift]) (US4 acceptance 2)
- [X] T095 [P] [US4] Write test: drift=0.5 produces intermediate wandering magnitude between drift=0 and drift=1 (tag: [drift]) (US4 acceptance 3)
- [X] T096 [P] [US4] Write test: successive particles with drift enabled trace different random walks (tag: [drift])
- [X] T097 [P] [US4] Write test: drift frequency changes are smooth (no abrupt jumps, bandwidth consistent with 5-20 Hz filter) (tag: [drift])
- [X] T098 [P] [US4] Write test: setEnvelopeType switches between all 6 GrainEnvelopeType values without error (tag: [envelope])
- [X] T099 [P] [US4] Write test: different envelope types produce different amplitude shapes (e.g., Hann vs Trapezoid) (tag: [envelope])
- [X] T100 [P] [US4] Write test: output differs across successive reset() calls with different seeds (tag: [drift][lifecycle]) (SC-005)
- [X] T101 [P] [US4] Write performance test: 64 particles at 44.1 kHz consumes < 0.5% of single core (tag: [performance]) (SC-003)

### 6.2 Implementation for User Story 4

- [X] T102 [US4] Implement setDriftAmount(float amount): clamp to [0, 1], store in driftAmount_
- [X] T103 [US4] Implement setEnvelopeType(GrainEnvelopeType type): map type to index (cast to size_t), clamp to [0, kNumEnvelopeTypes - 1], store in currentEnvType_
- [X] T104 [US4] Update prepare() to compute driftFilterCoeff_: `exp(-2.0 * kPi * 10.0 / sampleRate_)` (10 Hz cutoff)
- [X] T105 [US4] Update spawnParticle() to initialize drift state: driftState = 0.0, driftRange = scatter_ * centerFrequency_ * semitonesToRatio(scatter_) (approximate max deviation in Hz)
- [X] T106 [US4] Update processParticle() to apply drift per sample: generate white noise via rng_.nextFloat(), filter through one-pole: `driftState = driftFilterCoeff_ * driftState + (1.0 - driftFilterCoeff_) * noise`, compute deviation: `deviationHz = driftState * driftAmount_ * driftRange`, update phaseIncrement = `(baseFrequency + deviationHz) / sampleRate_`, flush denormal on driftState via detail::flushDenormal()
- [X] T107 [US4] Update processParticle() to use current envelope table: access envelope via `envelopeTables_[currentEnvType_]` in GrainEnvelope::lookup()

### 6.3 Verification for User Story 4

- [X] T108 [US4] Build dsp_tests target: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T109 [US4] Run drift tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[ParticleOscillator][drift]"`
- [X] T110 [US4] Run envelope tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[ParticleOscillator][envelope]"`
- [X] T111 [US4] Run performance tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[ParticleOscillator][performance]"`
- [X] T112 [US4] Verify SC-003: Processing time < 0.5% of single core at 64 particles (check test output)
- [X] T113 [US4] Verify SC-005: Output differs across seeds (check test output)
- [X] T114 [US4] Fix any compilation errors or test failures

### 6.4 Cross-Platform Verification (MANDATORY)

- [X] T115 [US4] **Verify IEEE 754 compliance**: Confirm no new IEEE 754 issues introduced (already covered by T003)

### 6.5 Commit (MANDATORY)

- [X] T116 [US4] **Commit completed User Story 4 work**: Frequency drift and envelope type switching

**Checkpoint**: All user stories should now be independently functional and committed. The oscillator has all core features implemented.

---

## Phase 7: Edge Cases and Robustness

**Purpose**: Verify boundary conditions, error handling, and all remaining edge cases from spec.md

**Requirements**: All edge cases from spec.md
**Success Criteria**: All remaining SC-xxx not yet covered

### 7.1 Edge Case Tests (Write FIRST - Must FAIL)

- [X] T117 [P] Write test: density=0 outputs silence (no particles spawned) (tag: [edge-cases])
- [X] T118 [P] Write test: lifetime below 1 ms is clamped to 1 ms (tag: [edge-cases])
- [X] T119 [P] Write test: center frequency above Nyquist is clamped to below Nyquist (tag: [edge-cases])
- [X] T120 [P] Write test: scatter so large that some particles would have negative frequencies are clamped to 1 Hz (tag: [edge-cases])
- [X] T121 [P] Write test: NaN input to setFrequencyScatter is sanitized (tag: [edge-cases])
- [X] T122 [P] Write test: NaN input to setDensity is sanitized (tag: [edge-cases])
- [X] T123 [P] Write test: NaN input to setLifetime is sanitized (tag: [edge-cases])
- [X] T124 [P] Write test: NaN input to setDriftAmount is sanitized (tag: [edge-cases])
- [X] T125 [P] Write test: sample rate change (new prepare() call) resets all state and recalculates timing (tag: [lifecycle][edge-cases])
- [X] T126 [P] Write test: density exceeds 64 is clamped to 64 (tag: [edge-cases])

### 7.2 Edge Case Implementation

- [X] T127 Update all setters to sanitize NaN/Inf inputs before clamping: if detail::isNaN(value) || detail::isInf(value), use safe default (440 for frequency, 1 for density, 100 for lifetime, 0 for scatter/drift)
- [X] T128 Update setDensity to handle 0: clamp to minimum 1 (ensures no division by zero in normFactor_)
- [X] T129 Verify prepare() clears all state: reset particles_, spawn counter, and recomputes all derived values

### 7.3 Edge Case Verification

- [X] T130 Build dsp_tests target: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T131 Run edge case tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[ParticleOscillator][edge-cases]"`
- [X] T132 Fix any test failures

### 7.4 Commit

- [X] T133 **Commit completed edge case handling**

---

## Phase 8: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 8.1 Run Clang-Tidy Analysis

- [X] T134 **Generate compile_commands.json**: Open "Developer PowerShell for VS 2022", run `cmake --preset windows-ninja` from repo root
- [X] T135 **Run clang-tidy on DSP library**: `powershell -ExecutionPolicy Bypass -File .\tools\run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`

### 8.2 Address Findings

- [X] T136 **Fix all errors** reported by clang-tidy (blocking issues)
- [X] T137 **Review warnings** and fix where appropriate (use judgment for DSP code - may suppress magic-number warnings for filter coefficients)
- [X] T138 **Document suppressions** if any warnings are intentionally ignored (add NOLINT comment with reason)

### 8.3 Commit

- [X] T139 **Commit clang-tidy fixes**

**Checkpoint**: Static analysis clean - ready for final documentation and completion verification

---

## Phase 9: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [X] T140 **Update `specs/_architecture_/layer-2-processors.md`**: Add ParticleOscillator entry with:
  - Purpose: "Generates complex textural timbres from up to 64 lightweight sine oscillators with individual drift, lifetime, and spawn behavior"
  - File location: `dsp/include/krate/dsp/processors/particle_oscillator.h`
  - Public API summary: prepare(), reset(), setFrequency(), setFrequencyScatter(), setDensity(), setLifetime(), setSpawnMode(), triggerBurst(), setEnvelopeType(), setDriftAmount(), process(), processBlock()
  - When to use: "When you need particle/swarm synthesis, dense granular cloud textures, organic living tones, or multi-voice oscillation with individual drift"
  - Key features: 3 spawn modes (Regular/Random/Burst), 6 envelope types, per-particle drift, 1/sqrt(N) normalization
  - Performance: < 0.5% CPU at 64 particles, 44.1 kHz
  - Dependencies: Layer 0 only (random.h, grain_envelope.h, pitch_utils.h, math_constants.h, db_utils.h)
- [X] T141 Verify no duplicate functionality was introduced (confirm with layer-2-processors.md and other oscillator entries)

### 9.2 Final Commit

- [X] T142 **Commit architecture documentation updates**

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T143 **Review ALL FR-001 through FR-022 requirements** from spec.md against implementation code (open particle_oscillator.h, read each method, confirm requirement is met)
- [X] T144 **Review ALL SC-001 through SC-007 success criteria** and verify measurable targets are achieved (re-run tests, record actual measured values)
- [X] T145 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in particle_oscillator.h
  - [X] No test thresholds relaxed from spec requirements in particle_oscillator_test.cpp
  - [X] No features quietly removed from scope (all FR-xxx and US acceptance scenarios implemented)

### 10.2 Fill Compliance Table in spec.md

- [X] T146 **Open `specs/028-particle-oscillator/spec.md`** and navigate to "Implementation Verification" section
- [X] T147 **For each FR-xxx row**: Re-read the requirement, open particle_oscillator.h, find the implementing code, record file path and line number in Evidence column, mark Status as MET
- [X] T148 **For each SC-xxx row**: Re-run the specific test (e.g., `dsp_tests.exe "[ParticleOscillator][frequency]"`), copy actual measured value from test output, record in Evidence column comparing to spec threshold, mark Status as MET or NOT MET
- [X] T149 **Mark overall status honestly**: COMPLETE (if all MET) / NOT COMPLETE (if any NOT MET) / PARTIAL (if gaps explicitly approved)

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in particle_oscillator.h?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T150 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

### 10.4 Final Verification Build

- [X] T151 **Clean build from scratch**: `cmake --build build/windows-x64-release --config Release --target dsp_tests --clean-first`
- [X] T152 **Run ALL ParticleOscillator tests**: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[ParticleOscillator]"`
- [X] T153 **Verify all tests pass with no failures**

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Commit

- [X] T154 **Commit compliance table updates in spec.md** (if any changes)
- [X] T155 **Verify all spec work is committed** to feature branch `028-particle-oscillator`
- [X] T156 **Final test run**: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[ParticleOscillator]"` - confirm all pass

### 11.2 Completion Claim

- [X] T157 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phases 3-6)**: All depend on Foundational phase completion
  - Must proceed **sequentially** in priority order (P1 → P2 → P3 → P4) due to cumulative testing and feature dependencies
  - US2 builds on US1 (adds scatter to existing spawn logic)
  - US3 builds on US2 (adds spawn mode variations to existing scheduler)
  - US4 builds on US3 (adds drift to existing per-particle processing)
- **Edge Cases (Phase 7)**: Depends on all user stories being complete (tests edge cases of all features)
- **Static Analysis (Phase 8)**: Depends on all implementation complete
- **Documentation (Phase 9)**: Depends on all implementation complete
- **Completion Verification (Phase 10)**: Depends on documentation complete
- **Final Completion (Phase 11)**: Depends on completion verification passing

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- All tests for a story can be written in parallel (marked [P])
- Implementation tasks must follow dependency order (e.g., core methods before higher-level methods)
- **Verify tests pass**: After implementation
- **Cross-platform check**: After tests pass
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

- **Phase 1**: T001-T004 can run in parallel (different files)
- **Phase 2**: T005-T011 must be sequential (same file, cumulative structure)
- **Within each User Story's test section**: All test writing tasks marked [P] can run in parallel (different test cases in same file)
- **Within each User Story's implementation section**: Some tasks marked [P] can run in parallel if they touch independent parts of the class, but most must be sequential due to method dependencies
- **Phase 7**: All edge case tests (T117-T126) can be written in parallel
- **Phase 8**: Single thread (clang-tidy)
- **Phase 9**: Single thread (documentation)

---

## Parallel Example: User Story 1 Test Writing

```bash
# Launch all test-writing tasks for User Story 1 together (all marked [P]):
Task T012: "Write test: ParticleOscillator default constructor"
Task T013: "Write test: isPrepared() returns false before prepare()"
Task T014: "Write test: processBlock() outputs silence before prepare()"
Task T015: "Write test: prepare() and reset()"
Task T016: "Write test: single particle THD < 1%"
Task T017: "Write test: output bounded"
Task T018: "Write test: setFrequency clamping"
Task T019: "Write test: setFrequency NaN sanitization"
Task T020: "Write test: particle lifetime accuracy"
Task T021: "Write test: output non-silent"
Task T022: "Write test: spectral energy at 440 Hz"
Task T023: "Write test: activeParticleCount()"
# All can be written simultaneously (different TEST_CASE blocks)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (test infrastructure)
2. Complete Phase 2: Foundational (header skeleton) - BLOCKS all stories
3. Complete Phase 3: User Story 1 (basic pitched particle cloud)
4. **STOP and VALIDATE**: Test User Story 1 independently (can generate basic textured tones)
5. Demo/validate before continuing

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Basic particle synthesis works (MVP!)
3. Add User Story 2 → Test independently → Dense clouds and wide scatter work
4. Add User Story 3 → Test independently → All 3 spawn modes work
5. Add User Story 4 → Test independently → Drift and envelope types work
6. Add Edge Cases → Robustness complete
7. Complete Static Analysis → Code quality verified
8. Complete Documentation → Architecture updated
9. Complete Verification → Honest completion claim
10. Each story adds value without breaking previous stories

### Sequential Team Strategy (Recommended for Single Developer)

This feature is best implemented sequentially by a single developer due to:
- Single header file (particle_oscillator.h) - parallel edits would conflict
- Single test file (particle_oscillator_test.cpp) - parallel edits would conflict
- Cumulative feature dependencies (US2 builds on US1, US3 on US2, US4 on US3)

**Recommended order**:
1. Developer completes Setup + Foundational
2. Developer completes User Story 1 → Validates MVP
3. Developer completes User Story 2 → Validates dense clouds
4. Developer completes User Story 3 → Validates spawn modes
5. Developer completes User Story 4 → Validates drift
6. Developer completes Edge Cases, Static Analysis, Documentation, Verification

---

## Summary

- **Total Tasks**: 157 tasks
- **Task Count by User Story**:
  - Setup: 4 tasks
  - Foundational: 7 tasks
  - User Story 1 (Basic Pitched Particle Cloud): 36 tasks
  - User Story 2 (Dense Granular Cloud Texture): 25 tasks
  - User Story 3 (Spawn Mode Variation): 19 tasks
  - User Story 4 (Frequency Drift): 25 tasks
  - Edge Cases: 17 tasks
  - Static Analysis: 6 tasks
  - Architecture Documentation: 3 tasks
  - Completion Verification: 11 tasks
  - Final Completion: 4 tasks

- **Parallel Opportunities**:
  - Setup phase: 4 tasks can run in parallel
  - Each user story's test-writing phase: 10-13 tests can be written in parallel
  - Edge case tests: 10 tests can be written in parallel
  - Total parallelizable test-writing tasks: ~50+ tasks

- **Independent Test Criteria**:
  - **US1**: Single particle THD < 1%, output bounded, timing accurate, non-silent
  - **US2**: Broadband spectral content, 90% occupancy, texture evolution, bounded at max density
  - **US3**: Spawn pattern matches mode (evenly spaced / stochastic / burst), no clicks on transitions
  - **US4**: Drift=0 constant frequency, drift=1 wandering, different seeds produce different output, performance < 0.5% CPU

- **Suggested MVP Scope**: User Story 1 only (basic pitched particle cloud) - provides immediately usable particle synthesis with envelope shaping and normalization

- **Format Validation**: All tasks follow the checklist format:
  - Checkbox: `- [ ]`
  - Task ID: T001-T157 (sequential, execution order)
  - [P] marker: Present for parallelizable tasks (different test cases, independent implementation steps)
  - [Story] label: Present for US1, US2, US3, US4 tasks (Setup, Foundational, Edge Cases, Documentation phases have no story label)
  - Description: Includes file paths where applicable

---

## Notes

- [P] tasks = different files OR different test cases in same file OR independent implementation steps, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently testable (each has acceptance scenarios from spec.md)
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Run clang-tidy before claiming completion
- **MANDATORY**: Update `specs/_architecture_/layer-2-processors.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts without clear sequential order, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
