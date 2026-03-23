# Tasks: Bow Model Exciter

**Input**: Design documents from `/specs/130-bow-model-exciter/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

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
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Integration Tests (MANDATORY When Applicable)

If the feature **wires a sub-component into the processor** (MIDI routing, audio chain, parameter application, stateful per-block processing), integration tests are **required**. See `INTEGRATION-TESTING.md` in the testing-guide skill.

Key rules:
- **Behavioral correctness over existence checks**: Verify the output is *correct*, not just *present*
- **Test degraded host conditions**: Not just ideal conditions - also no transport, no tempo, `nullptr` process context
- **Test per-block configuration safety**: Ensure setters called every block in `applyParamsToEngine()` don't silently reset stateful components

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/unit/CMakeLists.txt`
2. Use `Approx().margin()` for floating-point comparisons, not exact equality
3. Use `std::setprecision(6)` or less in approval tests (MSVC/Clang differ at 7th-8th digits)

---

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story this task belongs to (US1-US7)
- Exact file paths are included in every description

---

## Phase 1: Setup

**Purpose**: Register the new test source file in CMake so it is compiled. No implementation yet.

- [X] T001 Add `bow_exciter_test.cpp` to `dsp/tests/unit/CMakeLists.txt` alongside existing processor test entries

**Checkpoint**: CMake knows about the test file -- build will fail to compile until the test file is created, which is intentional

---

## Phase 2: Foundational -- Unified Exciter Interface Refactor

**Purpose**: Refactor `ImpactExciter` and `ResidualSynthesizer` to accept `float feedbackVelocity` in `process()`. This is a breaking API change that MUST be completed before BowExciter can be wired into the voice engine (FR-015, FR-016). All downstream call sites must be updated before any user story work begins.

**Why foundational**: Every user story that touches the plugin voice loop depends on the unified interface. Leaving the old no-argument signatures in place would require a second disruptive refactor mid-implementation.

- [X] T002 Write failing tests that call `impactExciter.process(0.0f)` to confirm the new signature does not yet exist -- add to `dsp/tests/unit/processors/impact_exciter_test.cpp`
- [X] T003 Refactor `ImpactExciter::process()` from `float process() noexcept` to `float process(float /*feedbackVelocity*/) noexcept` in `dsp/include/krate/dsp/processors/impact_exciter.h`; update internal `processBlock()` call to `process(0.0f)`
- [X] T004 Write failing tests that call `residualSynth.process(0.0f)` -- add to `dsp/tests/unit/processors/residual_synthesizer_tests.cpp`
- [X] T005 Refactor `ResidualSynthesizer::process()` from `[[nodiscard]] float process() noexcept` to `[[nodiscard]] float process(float /*feedbackVelocity*/) noexcept` in `dsp/include/krate/dsp/processors/residual_synthesizer.h`
- [X] T006 Update all `ImpactExciter` call sites in `dsp/tests/unit/processors/impact_exciter_test.cpp` from `.process()` to `.process(0.0f)` (~13 sites per research.md R1). Note: the `processBlock()` internal call was already updated in T003; this task covers only the test call sites.
- [X] T007 Update all `ResidualSynthesizer` call sites in `dsp/tests/unit/processors/residual_synthesizer_tests.cpp` from `.process()` to `.process(0.0f)`
- [X] T008 Update plugin call sites in `plugins/innexus/src/processor/processor.cpp`: change `v.impactExciter.process()` (line ~1631) to `v.impactExciter.process(feedbackVelocity)` and `v.residualSynth.process()` (line ~1635) to `v.residualSynth.process(feedbackVelocity)`
- [X] T009 Build `dsp_tests` and `innexus_tests` targets and verify zero compiler errors and zero warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T010 Run `dsp_tests.exe 2>&1 | tail -5` and confirm all existing tests pass with the new signatures
- [X] T011 Run `innexus_tests.exe 2>&1 | tail -5` and confirm all existing plugin tests pass
- [X] T012 Verify IEEE 754 compliance: check impact_exciter_test.cpp and residual_synthesizer_tests.cpp for `std::isnan`/`std::isfinite` usage; add to `-fno-fast-math` list in `dsp/tests/unit/CMakeLists.txt` if found
- [X] T013 Commit unified interface refactor

**Checkpoint**: All tests pass with unified `process(float)` signature. Voice engine call sites updated. BowExciter integration can now begin.

---

## Phase 3: User Story 1 -- Sustained Bowed String Tone (Priority: P1) -- MVP

**Goal**: Implement the complete `BowExciter` DSP class with all friction, jitter, envelope integration, and energy control. When a MIDI note is played with the bow exciter active on a waveguide resonator, the output is a continuously sustained tone with organic micro-variation (no repeating static loop).

**Independent Test**: Instantiate `BowExciter`, call `prepare(44100.0)`, `trigger(0.8f)`, then call `process(feedbackVelocity)` in a simulated feedback loop with a stub resonator. Verify: (1) output is non-zero after trigger, (2) no two consecutive output values are identical (micro-variation from LFO+noise, SC-001), (3) output stays bounded (no runaway), (4) output is DC-free.

### 3.1 Tests for User Story 1 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T014 [US1] Write failing tests for `BowExciter` lifecycle in `dsp/tests/unit/processors/bow_exciter_test.cpp`: `prepare()` sets `isPrepared()` true; `trigger()` sets `isActive()` true; `release()` does not instantly silence; `reset()` clears active state
- [X] T015 [US1] Write failing tests for bow table friction formula in `dsp/tests/unit/processors/bow_exciter_test.cpp`: at pressure=0.5, `deltaV=0.1` produces reflection coefficient in (0.01, 0.98); at `deltaV=0` output is bounded; slope formula `clamp(5.0 - 4.0 * pressure, 1.0, 10.0)` produces 5.0 at pressure=0 and 1.0 at pressure=1.0 (FR-002, FR-003)
- [X] T016 [US1] Write failing tests for ADSR acceleration integration in `dsp/tests/unit/processors/bow_exciter_test.cpp`: with envelope value=1.0 and speed=1.0, bow velocity increases over consecutive samples and saturates at `maxVelocity * speed`; with envelope value=0.0, velocity does not increase (FR-004, FR-005)
- [X] T017 [US1] Write failing tests for excitation force output in `dsp/tests/unit/processors/bow_exciter_test.cpp`: `process(feedbackVelocity)` returns a non-zero value when bow velocity != feedbackVelocity; returned force changes when feedbackVelocity changes (FR-006)
- [X] T018 [US1] Write failing tests for position impedance scaling in `dsp/tests/unit/processors/bow_exciter_test.cpp`: at position=0.13 (default), output is scaled by `1.0 / max(0.13 * 0.87 * 4.0, 0.1)`; at position=0.0 or 1.0, singularity is prevented via `max(..., 0.1)` clamp (FR-007)
- [X] T019 [US1] Write failing tests for micro-variation (SC-001) in `dsp/tests/unit/processors/bow_exciter_test.cpp`: run 100 samples in a feedback loop at fixed parameters; collect all output values; verify no two consecutive samples are identical (proves rosin jitter is active, FR-008)
- [X] T020 [US1] Write failing tests for energy control in `dsp/tests/unit/processors/bow_exciter_test.cpp`: call `setResonatorEnergy(energy)` with energyRatio = 3.0 (i.e., energy = 3 * targetEnergy); verify output amplitude is attenuated by at least 20% (factor of 0.8 or less) relative to when energy <= targetEnergy (FR-010)
- [X] T021 [US1] Write failing tests for numerical safety in `dsp/tests/unit/processors/bow_exciter_test.cpp`: run 1000 samples at pressure=1.0, speed=1.0, position=0.01 (extreme values); verify output stays bounded to [-10.0f, 10.0f] and is not NaN or Inf (SC-008, SC-009)

### 3.2 Implementation for User Story 1

- [X] T022 [US1] Implement `BowExciter` class header in `dsp/include/krate/dsp/processors/bow_exciter.h` following the API contract at `specs/130-bow-model-exciter/contracts/bow_exciter_api.h`; include all fields from data-model.md: `pressure_`, `speed_`, `position_`, `bowVelocity_`, `maxVelocity_`, `targetEnergy_`, `resonatorEnergy_`, `envelopeValue_`, `hairLpf_` (OnePoleLP), `rosinLfo_` (LFO), `noiseState_`, `noiseHpState_`, `noiseHpCoeff_`, `sampleRate_`, `prepared_`, `active_`
- [X] T023 [US1] Implement `BowExciter::prepare(double sampleRate)` in `dsp/include/krate/dsp/processors/bow_exciter.h`: call `hairLpf_.prepare(sampleRate)` and `hairLpf_.setCutoff(kHairLpfCutoff)`; call `rosinLfo_.prepare(sampleRate)`, `rosinLfo_.setFrequency(kRosinLfoRate)`, `rosinLfo_.setWaveform(LFOWaveform::Sine)`; compute `noiseHpCoeff_` from `kRosinNoiseCutoff` and sampleRate; set `prepared_ = true`
- [X] T024 [US1] Implement `BowExciter::trigger(float velocity)` in `dsp/include/krate/dsp/processors/bow_exciter.h`: set `maxVelocity_` from velocity; set `targetEnergy_` from velocity * speed_; call `rosinLfo_.retrigger()`; set `active_ = true`; reset `bowVelocity_ = 0.0f`
- [X] T025 [US1] Implement `BowExciter::process(float feedbackVelocity)` in `dsp/include/krate/dsp/processors/bow_exciter.h` following the 11-step flow from research.md R5: (1) integrate acceleration from `envelopeValue_`; (2) clamp bowVelocity; (3) compute deltaV; (4) generate rosin jitter (LFO + LCG noise highpass); (5) evaluate bow table `clamp(1/(x*x*x*x), 0.01f, 0.98f)` where `x = std::fabs(deltaV * slope + offset) + 0.75f`; (6) compute excitation force; (7) scale by positionImpedance; (8) apply `hairLpf_.process()`; (9) compute energyGain and apply; (10) return force
- [X] T026 [US1] Implement `BowExciter::reset()`, `release()`, `setPressure()`, `setSpeed()`, `setPosition()`, `setEnvelopeValue()`, `setResonatorEnergy()`, `isActive()`, `isPrepared()` in `dsp/include/krate/dsp/processors/bow_exciter.h`
- [X] T027 [US1] Build `dsp_tests` target and fix all compiler errors and warnings
- [X] T028 [US1] Run `dsp_tests.exe "BowExciter*" 2>&1 | tail -20` and verify all User Story 1 tests pass

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T029 [US1] Verify IEEE 754 compliance: `bow_exciter_test.cpp` likely uses `std::isnan`/`std::isfinite` for numerical safety tests (T021); add `dsp/tests/unit/processors/bow_exciter_test.cpp` to `-fno-fast-math` list in `dsp/tests/unit/CMakeLists.txt`

### 3.4 Commit (MANDATORY)

- [X] T030 [US1] Commit BowExciter DSP class and tests: `BowExciter Layer 2 processor with STK friction model, rosin jitter, energy control`

**Checkpoint**: BowExciter produces sustained, bounded, non-static output. All friction formula, jitter, and energy control tests pass. Ready to add timbral control (US2).

---

## Phase 4: User Story 2 -- Expressive Timbral Control via Pressure (Priority: P1)

**Goal**: Verify that the pressure parameter produces three audibly distinct timbral regions (surface sound, Helmholtz, raucous) with smooth transitions. The Helmholtz regime must satisfy: fundamental-to-noise ratio >= 20 dB AND >= 3 harmonics above -40 dBFS (FR-014, SC-002).

**Independent Test**: Drive BowExciter in a simulated feedback loop at pressure=0.05, 0.5, and 0.9. Compute FFT of the output. At pressure=0.5, verify fundamental SNR >= 20 dB and >= 3 harmonics above -40 dBFS. At pressure=0.05, verify output energy is lower than at pressure=0.5. At pressure=0.9, verify presence of additional harmonic content vs pressure=0.5.

### 4.1 Tests for User Story 2 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T031 [US2] Write failing tests for pressure timbral regions in `dsp/tests/unit/processors/bow_exciter_test.cpp`: at pressure=0.05 (surface sound), output RMS is measurably lower than at pressure=0.5; at pressure=0.9 (raucous), output contains more high-frequency content than at pressure=0.5 (FR-014, SC-002)
- [X] T032 [US2] Write failing tests for Helmholtz regime in `dsp/tests/unit/processors/bow_exciter_test.cpp`: with a simulated resonator feedback loop at pressure=0.3 and a 220 Hz feedback frequency, output spectrum has fundamental-to-noise ratio >= 20 dB AND at least 3 harmonics above -40 dBFS after steady state (FR-014, SC-002)
- [X] T033 [US2] Write failing tests for smooth pressure transition in `dsp/tests/unit/processors/bow_exciter_test.cpp`: sweep pressure from 0.0 to 1.0 in 1000 steps during sustained output; verify no sudden amplitude discontinuities larger than 0.1 between consecutive samples (SC-002)
- [X] T034 [US2] Write failing tests for slope formula coverage in `dsp/tests/unit/processors/bow_exciter_test.cpp`: `setPressure(0.0f)` produces slope=5.0; `setPressure(1.0f)` produces slope=1.0; `setPressure(0.25f)` produces slope=4.0 (FR-003)

### 4.2 Implementation for User Story 2

Note: The pressure parameter is already handled inside `process()` via `setPressure()`. This phase verifies the formula is correct and the three timbral regions emerge naturally from the implementation completed in Phase 3. If tests T031-T034 all pass immediately, no additional implementation is needed -- the bow table formula already encodes the timbral behavior.

- [X] T035 [US2] Tune `maxAcceleration` constant in `BowExciter::process()` in `dsp/include/krate/dsp/processors/bow_exciter.h` if Helmholtz regime test (T032) fails: adjust so that at default parameters the bow reaches steady-state bowing within ~100 ms (4410 samples at 44100 Hz)
- [X] T036 [US2] Tune offset constant in bow table formula (`x = |deltaV * slope + offset| + 0.75`) in `dsp/include/krate/dsp/processors/bow_exciter.h` if surface sound region (T031) is not audibly distinct from Helmholtz; the offset controls the transition point between regimes
- [X] T037 [US2] Build `dsp_tests` target and fix all compiler errors and warnings
- [X] T038 [US2] Run `dsp_tests.exe "BowExciter*" 2>&1 | tail -20` and verify all User Story 2 tests pass

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T039 [US2] Verify no new IEEE 754 functions were added to `bow_exciter_test.cpp` that require `-fno-fast-math` beyond what was added in T029; update `dsp/tests/unit/CMakeLists.txt` if needed

### 4.4 Commit (MANDATORY)

- [X] T040 [US2] Commit pressure timbral region tests and any tuning adjustments: `BowExciter pressure timbral regions verified: surface sound / Helmholtz / raucous`

**Checkpoint**: Three pressure timbral regions verified with measurable spectral criteria. Helmholtz regime satisfies FR-014 thresholds.

---

## Phase 5: User Story 3 -- Bow Position Controls Harmonic Emphasis (Priority: P2)

**Goal**: The bow position parameter (`kBowPositionId`) affects harmonic content following the sinc-like envelope: `sin(n * pi * beta) / (n * pi * beta)`. Near-bridge (small beta) produces bright metallic tones; near-fingerboard (large beta) produces soft flute-like tones. Position also affects playability per the Schelleng diagram.

**Independent Test**: Drive BowExciter in a feedback loop at position=0.05, 0.13, and 0.5. Compute FFT of steady-state output. Verify measurable spectral differences: at position=0.5, even harmonics are suppressed relative to position=0.13. Verify that near-bridge (position=0.05) requires higher pressure to sustain stable oscillation than position=0.13.

### 5.1 Tests for User Story 3 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T041 [US3] Write failing tests for position impedance formula in `dsp/tests/unit/processors/bow_exciter_test.cpp`: at position=0.13, impedance = `1.0f / max(0.13f * 0.87f * 4.0f, 0.1f)`; at position=0.0, impedance = `1.0f / 0.1f` (singularity prevented); at position=0.5, impedance = `1.0f / (0.5f * 0.5f * 4.0f)` = 1.0 (FR-007)
- [ ] T042 [US3] Write failing tests for harmonic weighting at position=0.5 in `dsp/tests/unit/processors/bow_exciter_test.cpp`: run BowExciter in a simulated feedback loop; at position=0.5 the excitation force has measurably different spectral character vs position=0.13, consistent with 2nd harmonic suppression (FR-024, SC-005)
- [ ] T043 [US3] Write failing tests for position-dependent playability in `dsp/tests/unit/processors/bow_exciter_test.cpp`: at position=0.05 (near bridge) with pressure=0.15, oscillation does NOT sustain (output decays); at position=0.13 with the same pressure, oscillation sustains. This verifies Schelleng diagram behavior (SC-006)
- [ ] T043b [US3] Write failing test confirming Schelleng behavior is a pressure threshold, not a hard block: at position=0.05 with pressure=0.4 (above the near-bridge F_min), oscillation DOES sustain. This confirms the model correctly requires higher pressure near the bridge, rather than preventing oscillation entirely
- [ ] T044 [US3] Write failing tests for position=0.0 and position=1.0 edge cases in `dsp/tests/unit/processors/bow_exciter_test.cpp`: neither value causes NaN, Inf, or division by zero; `setPosition()` accepts and clamps these values (FR-007)

### 5.2 Implementation for User Story 3

Note: Position impedance scaling is part of `process()` implemented in Phase 3. This phase verifies the formula and tuning. The harmonic weighting via `sin(n * pi * beta)` is relevant when position modulates the excitation -- this affects the waveguide coupling in WaveguideString, which is wired in Phase 7 (US5/US6). For isolated BowExciter tests, the harmonic weighting manifests indirectly through the impedance scaling.

- [ ] T045 [US3] Verify `setPosition()` in `dsp/include/krate/dsp/processors/bow_exciter.h` clamps input to [0.0, 1.0] and the position impedance computation uses `max(beta * (1 - beta) * 4.0f, 0.1f)` to prevent the singularity at beta=0 and beta=1
- [ ] T046 [US3] Build `dsp_tests` target and fix all compiler errors and warnings
- [ ] T047 [US3] Run `dsp_tests.exe "BowExciter*" 2>&1 | tail -20` and verify all User Story 3 tests pass

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T048 [US3] Verify no new files require `-fno-fast-math` additions in `dsp/tests/unit/CMakeLists.txt`

### 5.4 Commit (MANDATORY)

- [ ] T049 [US3] Commit bow position tests and verification: `BowExciter position impedance and Schelleng playability verified`

**Checkpoint**: Position parameter controls harmonic emphasis and playability. Singularity prevention confirmed. Schelleng diagram behavior verified.

---

## Phase 6: User Story 4 -- Bow Speed Controls Dynamics and Attack Character (Priority: P2)

**Goal**: MIDI velocity maps to `maxVelocity` and ADSR attack time. Short attack = high acceleration = potentially scratchy onset (Guettler-compliant). Long attack = smooth onset. Low velocity = quiet, gentle. High velocity = loud, harder attack.

**Independent Test**: Play BowExciter at velocity=0.2 vs velocity=0.9. Verify RMS amplitude at steady state differs proportionally. Play with a simulated fast-attack envelope vs slow-attack envelope; verify the fast-attack onset has higher peak variation in the first 50 ms (scratchy vs smooth).

### 6.1 Tests for User Story 4 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T050 [US4] Write failing tests for velocity-to-amplitude mapping in `dsp/tests/unit/processors/bow_exciter_test.cpp`: trigger at velocity=0.2, run to steady state, measure RMS; trigger at velocity=0.8, run to steady state, measure RMS; verify high-velocity RMS > low-velocity RMS (SC-007)
- [ ] T051 [US4] Write failing tests for speed ceiling in `dsp/tests/unit/processors/bow_exciter_test.cpp`: with speed=0.0, `process()` returns 0.0 (stationary bow produces no excitation per spec edge cases); with speed=0.5, bow velocity saturates at `maxVelocity * 0.5` not `maxVelocity` (FR-005)
- [ ] T052 [US4] Write failing tests for Guettler-compliant attack in `dsp/tests/unit/processors/bow_exciter_test.cpp`: with fast envelope ramp (envelope goes 0->1 in 10 samples), peak-to-peak output variation in the first 50 samples is measurably higher than with a slow ramp (envelope goes 0->1 in 1000 samples). This confirms acceleration-based integration produces different onset transients (FR-004, SC-007)
- [ ] T053 [US4] Write failing tests for maxVelocity from trigger in `dsp/tests/unit/processors/bow_exciter_test.cpp`: after `trigger(0.8f)`, `maxVelocity_` corresponds to velocity=0.8; bow velocity clamps below this ceiling

### 6.2 Implementation for User Story 4

Note: The acceleration-based velocity integration is implemented in Phase 3's `process()`. This phase verifies correct behavior through targeted tests. If T050-T053 fail, the acceleration constant or velocity ceiling logic in `process()` needs adjustment.

- [ ] T054 [US4] Verify `trigger(float velocity)` in `dsp/include/krate/dsp/processors/bow_exciter.h` correctly maps velocity to `maxVelocity_` (e.g., `maxVelocity_ = velocity * kMaxBowVelocity` for a tuned constant) and sets `targetEnergy_`
- [ ] T055 [US4] Build `dsp_tests` target and fix all compiler errors and warnings
- [ ] T056 [US4] Run `dsp_tests.exe "BowExciter*" 2>&1 | tail -20` and verify all User Story 4 tests pass

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T057 [US4] Verify no new files require `-fno-fast-math` additions in `dsp/tests/unit/CMakeLists.txt`

### 6.4 Commit (MANDATORY)

- [ ] T058 [US4] Commit bow speed/dynamics tests: `BowExciter velocity-to-dynamics and Guettler-compliant attack verified`

**Checkpoint**: Speed parameter controls amplitude ceiling. ADSR acceleration integration produces distinct onset transients at different attack rates.

---

## Phase 7: User Story 5 -- Unified Exciter Interface and Plugin Integration (Priority: P2)

**Goal**: Wire BowExciter into the Innexus plugin. Add `bowExciter` field to `InnexusVoice`. Add 4 VST parameters (`kBowPressureId`, `kBowSpeedId`, `kBowPositionId`, `kBowOversamplingId`). Update processor loop to use the unified call pattern from `contracts/unified_exciter_interface.md`. Handle bow trigger on note-on in `processor_midi.cpp`.

**Independent Test**: Load Innexus plugin in pluginval. Select ExciterType::Bow. Play a MIDI note. Verify audio output is produced and the plugin does not crash. Change pressure, speed, and position parameters during a sustained note and verify the plugin remains stable.

### 7.1 Tests for User Story 5 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T059 [P] [US5] Write failing unit tests for InnexusVoice bow field in `plugins/innexus/tests/unit/processor/innexus_voice_bow_test.cpp`: verify `innexusVoice.bowExciter` is a `Krate::DSP::BowExciter` field; verify `prepare()` propagates to `bowExciter.prepare()`; verify `reset()` propagates to `bowExciter.reset()`
- [ ] T060 [P] [US5] Write failing unit tests for bow parameter IDs in `plugins/innexus/tests/unit/vst/bow_parameters_test.cpp`: verify `kBowPressureId`, `kBowSpeedId`, `kBowPositionId`, `kBowOversamplingId` are declared in `plugin_ids.h` with unique values; verify parameter registration in controller produces 4 additional parameters beyond the pre-existing set

### 7.2 Implementation for User Story 5

- [ ] T061 [US5] Add `kBowPressureId`, `kBowSpeedId`, `kBowPositionId`, `kBowOversamplingId` to `plugins/innexus/src/plugin_ids.h` with unique integer values not conflicting with existing IDs; values TBD by checking the highest existing ID in that file
- [ ] T062 [US5] Add `Krate::DSP::BowExciter bowExciter` field to the `InnexusVoice` struct in `plugins/innexus/src/processor/innexus_voice.h`; add `#include <krate/dsp/processors/bow_exciter.h>`
- [ ] T063 [US5] Register bow parameters in `plugins/innexus/src/controller/controller.cpp` `initialize()`: add 4 `RangeParameter`/`Parameter` registrations for pressure (0.0-1.0, default 0.3), speed (0.0-1.0, default 0.5), position (0.0-1.0, default 0.13), and oversampling (bool, default false)
- [ ] T064 [US5] Handle bow parameter changes in `plugins/innexus/src/processor/processor_params.cpp`: add cases for `kBowPressureId`, `kBowSpeedId`, `kBowPositionId`, `kBowOversamplingId` -- call corresponding setters on all voices (`v.bowExciter.setPressure(...)` etc.)
- [ ] T065 [US5] Handle bow trigger on note-on in `plugins/innexus/src/processor/processor_midi.cpp`: when `ExciterType::Bow` is active and a note-on event fires, call `voice.bowExciter.trigger(normalizedVelocity)` and `voice.bowExciter.setPosition(bowPosition_)` etc. with current parameter values
- [ ] T066 [US5] Update processor audio loop in `plugins/innexus/src/processor/processor.cpp`: implement the unified call pattern from `contracts/unified_exciter_interface.md` -- get `feedbackVelocity = resonator->getFeedbackVelocity()`, switch on exciter type calling `.process(feedbackVelocity)` uniformly, with bow-specific pre-processing (`setEnvelopeValue`, `setResonatorEnergy`) inside the Bow case
- [ ] T067 [US5] Add bow parameter save/load to `plugins/innexus/src/processor/processor_state.cpp`: serialize `kBowPressureId`, `kBowSpeedId`, `kBowPositionId`, `kBowOversamplingId` in `setState()`/`getState()`
- [ ] T068 [US5] Build `innexus_tests` target and fix all compiler errors and warnings: `cmake --build build/windows-x64-release --config Release --target innexus_tests`
- [ ] T069 [US5] Run `innexus_tests.exe 2>&1 | tail -5` and verify all tests pass including T059 and T060
- [ ] T070 [US5] Run pluginval to verify plugin stability with bow exciter: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"`

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T071 [US5] Verify `innexus_voice_bow_test.cpp` and `bow_parameters_test.cpp` are added to `plugins/innexus/tests/CMakeLists.txt`; check for `std::isnan`/`std::isfinite` usage and add to `-fno-fast-math` list if found

### 7.4 Commit (MANDATORY)

- [ ] T072 [US5] Commit plugin integration: `Innexus: BowExciter integrated into voice engine, bow parameters registered`

**Checkpoint**: Bow exciter functional in the plugin. All four parameters accessible. Note trigger and release work. Plugin passes pluginval.

---

## Phase 8: User Story 6 -- Modal Resonator Bowed Mode Coupling (Priority: P3)

**Goal**: Extend `ModalResonatorBank` with 8 narrow bandpass velocity taps (Q ~50) that provide feedback velocity for the bow friction computation. Excitation force feeds into all modes weighted by `sin((n+1) * pi * bowPosition)` for harmonic selectivity (FR-020, FR-024).

**Independent Test**: Set up BowExciter + ModalResonatorBank. Call `setBowModeActive(true)` and `setBowPosition(0.13f)`. Run a feedback loop. Verify `getFeedbackVelocity()` returns a non-zero value that varies with bow position. Verify self-sustained oscillation occurs (output does not decay to silence at appropriate pressure).

### 8.1 Tests for User Story 6 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T073 [US6] Write failing tests for `BowedModeBPF` struct in `dsp/tests/unit/processors/modal_resonator_bank_bow_test.cpp`: `setCoefficients(440.0f, 50.0f, 44100.0)` computes valid biquad BPF coefficients; `process(input)` returns filtered output; `reset()` clears state; verify Q~50 means -3dB bandwidth is ~8.8 Hz at 440 Hz
- [ ] T074 [US6] Write failing tests for `setBowModeActive()` in `dsp/tests/unit/processors/modal_resonator_bank_bow_test.cpp`: when false, `getFeedbackVelocity()` returns 0.0f; when true, after processing some samples with excitation input, `getFeedbackVelocity()` returns non-zero (FR-020)
- [ ] T075 [US6] Write failing tests for `setBowPosition()` harmonic weighting in `dsp/tests/unit/processors/modal_resonator_bank_bow_test.cpp`: with `bowModeActive_=true`, at position=0.5 the excitation weight for mode k=1 (2nd mode) is `sin(2 * pi * 0.5) = 0`, confirming even-mode suppression; at position=0.13 all 8 mode weights are non-zero (FR-020, FR-024)
- [ ] T076 [US6] Write failing tests for bowed-mode velocity feedback in `dsp/tests/unit/processors/modal_resonator_bank_bow_test.cpp`: drive ModalResonatorBank + BowExciter in a combined feedback loop; verify self-sustained oscillation occurs (output does not decay below -60 dBFS after 1000 samples at stable parameters) (SC-004)

### 8.2 Implementation for User Story 6

- [ ] T077 [US6] Add `BowedModeBPF` internal struct to `dsp/include/krate/dsp/processors/modal_resonator_bank.h` per `contracts/modal_bowed_modes.md`: fields `b0`, `b2`, `a1`, `a2`, `z1`, `z2` (b1 is always 0 for BPF); methods `setCoefficients(float freq, float q, double sampleRate)`, `process(float)`, `reset()`
- [ ] T078 [US6] Add bowed-mode fields to `ModalResonatorBank` in `dsp/include/krate/dsp/processors/modal_resonator_bank.h`: `std::array<BowedModeBPF, kNumBowedModes> bowedModeFilters_`; `float bowedModeSumVelocity_{0.0f}`; `float bowPosition_{0.13f}`; `bool bowModeActive_{false}`; `static constexpr int kNumBowedModes = 8`
- [ ] T079 [US6] Implement `ModalResonatorBank::setBowModeActive(bool)` and `setBowPosition(float)` in `dsp/include/krate/dsp/processors/modal_resonator_bank.h`; `setBowPosition` recomputes `inputGainTarget_` weights using `sin((k+1) * kPi * bowPosition)` for k=0..numActiveModes-1
- [ ] T080 [US6] Update `ModalResonatorBank::processSample()` in `dsp/include/krate/dsp/processors/modal_resonator_bank.h`: when `bowModeActive_`, after computing the full mode sum output, feed output through each of the 8 `bowedModeFilters_[k].process(output)` and accumulate into `bowedModeSumVelocity_`
- [ ] T081 [US6] Update `ModalResonatorBank::getFeedbackVelocity()` in `dsp/include/krate/dsp/processors/modal_resonator_bank.h`: return `bowedModeSumVelocity_` when `bowModeActive_`, else return 0.0f (current behavior)
- [ ] T082 [US6] Update `ModalResonatorBank::prepare()` and `reset()` in `dsp/include/krate/dsp/processors/modal_resonator_bank.h` to initialize all 8 `bowedModeFilters_` with `reset()` and set coefficients when `prepare()` is called with mode frequencies available
- [ ] T083 [US6] Wire `setBowModeActive()` and `setBowPosition()` into `plugins/innexus/src/processor/processor.cpp`: when ExciterType::Bow is selected and ModalResonatorBank is the active resonator, call these methods on each voice's modal resonator
- [ ] T084 [US6] Build `dsp_tests` and `innexus_tests` targets and fix all compiler errors and warnings
- [ ] T085 [US6] Run `dsp_tests.exe "ModalResonatorBank*" 2>&1 | tail -20` and verify all bowed-mode tests pass
- [ ] T086 [US6] Run `innexus_tests.exe 2>&1 | tail -5` and confirm no regressions

### 8.3 WaveguideString DC Blocker Relocation (FR-021)

The DC blocker relocation in WaveguideString (FR-021) is a prerequisite for correct waveguide-bow coupling. It is placed in this phase because it modifies existing waveguide processing behavior and existing tests may need `Approx().margin()` adjustments.

- [ ] T087 [US6] Write failing tests for DC blocker position in `dsp/tests/unit/processors/waveguide_string_dc_blocker_test.cpp`: inject a DC offset into the waveguide feedback path; verify the DC is removed by the relocated blocker; verify the fundamental at 65 Hz (cello C2) is not attenuated (blocker cutoff remains 20 Hz, not 30 Hz) (FR-021)
- [ ] T088 [US6] Relocate `dcBlocker_` usage in `WaveguideString::process()` in `dsp/include/krate/dsp/processors/waveguide_string.h`: move the `dcBlocker_.process()` call to after the bow junction output and before signal re-enters the delay lines; no new DCBlocker instance is created
- [ ] T089 [US6] Update existing waveguide tests in `dsp/tests/unit/processors/` that use exact float comparisons: replace exact equality with `Approx().margin(1e-5)` where DC blocker relocation changes output values slightly
- [ ] T090 [US6] Build `dsp_tests` and run `dsp_tests.exe "[waveguide]*" 2>&1 | tail -20`; verify all existing waveguide tests pass with updated margins

### 8.4 Cross-Platform Verification (MANDATORY)

- [ ] T091 [US6] Add `modal_resonator_bank_bow_test.cpp` and `waveguide_string_dc_blocker_test.cpp` to `dsp/tests/unit/CMakeLists.txt`; check for IEEE 754 function usage and add to `-fno-fast-math` list if found

### 8.5 Commit (MANDATORY)

- [ ] T092 [US6] Commit modal bowed-mode coupling and DC blocker relocation: `ModalResonatorBank: 8 bowed-mode BPF velocity taps for bow-modal coupling; WaveguideString: DC blocker relocated per FR-021`

**Checkpoint**: BowExciter + ModalResonatorBank produces self-sustained oscillation. Position-dependent harmonic weighting is verified. WaveguideString DC blocker correctly positioned.

---

## Phase 9: User Story 7 -- Switchable 2x Oversampling (Priority: P3)

**Goal**: Implement the switchable 2x oversampling path for the bow-resonator junction, controlled by `kBowOversamplingId` VST parameter (FR-022, FR-023). Only the friction junction and its immediate neighbors run at 2x rate; full delay lines remain at 1x with adjusted lengths.

**Independent Test**: Enable oversampling (`kBowOversamplingId = 1`). Play a high-pitched note (above 1 kHz) at pressure > 0.7. Disable oversampling. Compare spectral output: 2x mode should have reduced aliasing artifacts above Nyquist reflected content. Verify parameter state survives a preset round-trip (save then restore state).

### 9.1 Tests for User Story 7 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T093 [US7] Write failing tests for oversampling parameter in `plugins/innexus/tests/unit/vst/bow_oversampling_test.cpp`: toggle `kBowOversamplingId` parameter; verify the processor reads and applies the setting; verify default is 1x (false)
- [ ] T094 [US7] Write failing tests for oversampling preset round-trip in `plugins/innexus/tests/unit/vst/bow_oversampling_test.cpp`: save state with oversampling=true; restore state; verify `kBowOversamplingId` is true after restore (SC-012)
- [ ] T095 [US7] Write failing tests for aliasing reduction in `dsp/tests/unit/processors/bow_exciter_oversampling_test.cpp`: drive BowExciter at 2x rate (88200 Hz internal rate) with a 2000 Hz feedback frequency at pressure=0.9; verify the output at 1x has higher alias content than at 2x (SC-012)

### 9.2 Implementation for User Story 7

- [ ] T096 [US7] Add oversampling state to `BowExciter` in `dsp/include/krate/dsp/processors/bow_exciter.h`: `bool oversamplingEnabled_{false}`; `OnePoleLP downsampleLpf_`; `float upsampleState_{0.0f}`; add `setOversamplingEnabled(bool)` setter
- [ ] T097 [US7] Implement the 2x oversampling path in `BowExciter::process()` in `dsp/include/krate/dsp/processors/bow_exciter.h`: when `oversamplingEnabled_`, linearly interpolate the feedback input, run the friction junction twice at the internal 2x rate, apply `downsampleLpf_` to the two outputs, return the averaged result; when disabled, run the existing 1x path unchanged (FR-022, FR-023)
- [ ] T098 [US7] Update `BowExciter::prepare()` in `dsp/include/krate/dsp/processors/bow_exciter.h`: when oversampling is enabled, prepare `downsampleLpf_` at Nyquist/2 = sampleRate/2 as the anti-alias filter
- [ ] T099 [US7] Wire `kBowOversamplingId` parameter in `plugins/innexus/src/processor/processor_params.cpp`: on parameter change, call `voice.bowExciter.setOversamplingEnabled(value > 0.5f)` for all voices
- [ ] T100 [US7] Verify `kBowOversamplingId` is already handled in `processor_state.cpp` (added in T067); if not, add save/load for this parameter
- [ ] T101 [US7] Build `dsp_tests` and `innexus_tests` targets and fix all compiler errors and warnings
- [ ] T102 [US7] Run `dsp_tests.exe "BowExciter*" 2>&1 | tail -20` and verify all oversampling tests pass
- [ ] T103 [US7] Run `innexus_tests.exe 2>&1 | tail -5` and confirm no regressions
- [ ] T104 [US7] Run pluginval again to verify no regressions from oversampling feature: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"`

### 9.3 Cross-Platform Verification (MANDATORY)

- [ ] T105 [US7] Add `bow_oversampling_test.cpp` and `bow_exciter_oversampling_test.cpp` to their respective CMakeLists.txt; add to `-fno-fast-math` list in `dsp/tests/unit/CMakeLists.txt` and `plugins/innexus/tests/CMakeLists.txt` if IEEE 754 functions are used

### 9.4 Commit (MANDATORY)

- [ ] T106 [US7] Commit 2x oversampling: `BowExciter: switchable 2x oversampling for friction junction via kBowOversamplingId`

**Checkpoint**: Oversampling parameter is functional, saves in presets, and measurably reduces aliasing at high pitch + high pressure.

---

## Phase 10: Polish and Cross-Cutting Concerns

**Purpose**: Final integration validation, edge cases, and end-to-end verification across all user stories.

- [ ] T107a [P] Verify SC-003: BowExciter + WaveguideString produces a convincing sustained tone at default parameters. Render 2 seconds of audio at 220 Hz (A3) with pressure=0.3, speed=0.5, position=0.13. Listen and document the result in the compliance table. If an approval test is feasible, save a golden reference via `bow_waveguide_sustained_220hz.wav` in `specs/130-bow-model-exciter/golden/`
- [ ] T107b [P] Verify SC-004: BowExciter + ModalResonatorBank (8 bowed modes active) produces a self-sustained "bowed vibraphone" or "bowed bar" texture. Render 2 seconds and confirm output does not decay to silence. Document result in the compliance table
- [ ] T107 [P] Verify edge case: bow at speed=0.0 produces silence -- run `dsp_tests.exe "BowExciter*speed*zero*"` or equivalent test tag; add test if missing
- [ ] T108 [P] Verify edge case: multiple voices each with BowExciter do not share state -- instantiate 4 `BowExciter` instances, trigger them at different velocities, verify each produces independent output
- [ ] T109 [P] Verify edge case: switching from ExciterType::Impact to ExciterType::Bow mid-note does not crash -- test in `plugins/innexus/tests/unit/processor/`; verify voice state is valid after exciter type switch
- [ ] T109b Verify SC-010 pitch-range loudness consistency: run BowExciter + WaveguideString at 65 Hz (cello C2), 220 Hz (A3), and 880 Hz (A5) with identical parameters (pressure=0.3, speed=0.5, position=0.13, velocity=0.8). Measure RMS output level at each pitch after 500 ms of steady state. Verify all three RMS values are within ±6 dB of each other. Document measured values in the compliance table.
- [ ] T110 Verify all bow parameters (`kBowPressureId`, `kBowSpeedId`, `kBowPositionId`, `kBowOversamplingId`) display correctly in the plugin UI -- confirm controller.cpp registration produces correct parameter labels and ranges
- [ ] T111 Run full test suite to verify no regressions from all phases: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure 2>&1 | tail -10`
- [ ] T112 Commit polish and edge case tests

---

## Phase 11: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy across all new and modified files.

- [ ] T113 Generate `compile_commands.json` for clang-tidy via ninja preset: `"C:/Program Files/CMake/bin/cmake.exe" --preset windows-ninja` (run from VS Developer PowerShell)
- [ ] T114 Run clang-tidy on DSP targets: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`
- [ ] T115 Run clang-tidy on Innexus plugin: `./tools/run-clang-tidy.ps1 -Target innexus -BuildDir build/windows-ninja`
- [ ] T116 Fix all clang-tidy errors (blocking issues) in new files: `bow_exciter.h`, `modal_resonator_bank.h`, `waveguide_string.h`, `impact_exciter.h`, `residual_synthesizer.h`
- [ ] T117 Fix all clang-tidy errors in modified plugin files: `innexus_voice.h`, `processor.cpp`, `processor_params.cpp`, `processor_midi.cpp`, `processor_state.cpp`, `controller.cpp`
- [ ] T118 Review clang-tidy warnings; fix where appropriate; add `// NOLINT(check-name): reason` for any intentionally suppressed warnings in DSP hot paths
- [ ] T119 Commit static analysis fixes: `Fix clang-tidy warnings in bow exciter implementation`

**Checkpoint**: Static analysis clean. No suppressed warnings without documented justification.

---

## Phase 12: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation per Constitution Principle XIII.

- [ ] T120 Update `specs/_architecture_/layer-2-processors.md`: add `BowExciter` entry with purpose ("STK power-law stick-slip friction exciter for continuous physical modelling"), public API summary (prepare, trigger, release, process, setPressure, setSpeed, setPosition, setEnvelopeValue, setResonatorEnergy), file location (`dsp/include/krate/dsp/processors/bow_exciter.h`), and "when to use this" (continuous bowed string / bar excitation; requires resonator with getFeedbackVelocity())
- [ ] T121 Update `specs/_architecture_/layer-2-processors.md`: add note to `ModalResonatorBank` entry documenting the 8 bowed-mode bandpass velocity taps (Q~50) and `setBowModeActive()`/`setBowPosition()` API
- [ ] T122 Update `specs/_architecture_/layer-2-processors.md`: add note to `WaveguideString` entry documenting that DC blocker is positioned after the bow junction output (per spec 130, FR-021)
- [ ] T123 Commit architecture documentation: `docs: update layer-2-processors architecture for BowExciter and bowed-mode coupling`

**Checkpoint**: Architecture documentation reflects all new components and modifications.

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion. Constitution Principle XVI.

### 13.1 Requirements Verification

- [ ] T124 Re-read each FR-001 through FR-025 from spec.md; for each requirement, open the implementation file and find the code that satisfies it; record the file path and line number in the spec.md compliance table
- [ ] T125 Re-read each SC-001 through SC-013 from spec.md; for each criterion, run the specific test or measurement; copy the actual output; compare against the spec threshold; record actual measured values in the compliance table
- [ ] T126 Search for cheating patterns: `grep -r "TODO\|placeholder\|stub" dsp/include/krate/dsp/processors/bow_exciter.h plugins/innexus/src/` -- verify no TODOs or placeholders remain in new code
- [ ] T127 Verify no test thresholds were relaxed from spec values: re-read T032's Helmholtz test (fundamental SNR >= 20 dB, >= 3 harmonics above -40 dBFS) and confirm the actual thresholds in the test match spec.md FR-014

### 13.2 Fill Compliance Table in spec.md

- [ ] T128 Update `specs/130-bow-model-exciter/spec.md` "Implementation Verification" table: fill in Status and Evidence for every FR-xxx and SC-xxx row with specific file paths, line numbers, test names, and actual measured values; mark overall status honestly as COMPLETE / NOT COMPLETE / PARTIAL

### 13.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T129 All self-check questions answered "no" (or gaps documented honestly in spec.md)

---

## Phase 14: Final Completion

- [ ] T130 Run full test suite one final time: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure 2>&1 | tail -10`
- [ ] T131 Verify all spec work is committed to the `130-bow-model-exciter` branch: `git log --oneline main..HEAD`
- [ ] T132 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user); if any gap exists, document it in spec.md before claiming completion

**Checkpoint**: Spec implementation honestly complete.

---

## Dependencies and Execution Order

### Phase Dependencies

```
Phase 1 (Setup: CMakeLists.txt)
  --> Phase 2 (Foundational: Unified Interface Refactor) BLOCKS all user stories
      --> Phase 3 (US1: BowExciter Core DSP)
          --> Phase 4 (US2: Pressure Timbral Regions) -- depends on Phase 3 BowExciter implementation
          --> Phase 5 (US3: Position Impedance) -- depends on Phase 3
          --> Phase 6 (US4: Speed/Dynamics) -- depends on Phase 3
          --> Phase 7 (US5: Plugin Integration) -- depends on Phase 3; is prerequisite for US6 modal wiring
              --> Phase 8 (US6: Modal Bowed Modes + DC Blocker) -- depends on Phase 7 for plugin wiring
              --> Phase 9 (US7: Oversampling) -- depends on Phase 7 for parameter infrastructure
      --> Phase 10 (Polish) -- depends on all user stories
      --> Phase 11 (Clang-Tidy) -- depends on Phase 10
      --> Phase 12 (Architecture Docs) -- depends on Phase 11
      --> Phase 13 (Completion Verification) -- depends on Phase 12
      --> Phase 14 (Final Commit) -- depends on Phase 13
```

### User Story Dependencies

- **US1 (P1)**: No dependencies on other user stories. Blocked only by Phase 2 (unified interface refactor).
- **US2 (P1)**: Depends on US1 (BowExciter implementation). Tests are additive to the same test file.
- **US3 (P2)**: Depends on US1. Tests are additive. Independent of US2.
- **US4 (P2)**: Depends on US1. Tests are additive. Independent of US2 and US3.
- **US5 (P2)**: Depends on US1 (BowExciter class must exist before it can be added to InnexusVoice). Blocks US6 modal wiring and US7 parameter infrastructure.
- **US6 (P3)**: Depends on US5 (plugin integration must exist to wire modal resonator bow coupling). Also modifies WaveguideString independently.
- **US7 (P3)**: Depends on US5 (parameter infrastructure must exist for kBowOversamplingId).

### Parallel Opportunities Within Phases

Phase 3 (US1): Tests T014-T021 can all be written in parallel (same file, but different TEST_CASE blocks -- assign to one implementor).

Phase 7 (US5): T059 (InnexusVoice test) and T060 (parameter ID test) are in different files -- can be written in parallel.

Phases 4, 5, 6 (US2, US3, US4): Once US1 (Phase 3) is complete, all three stories add tests to the same `bow_exciter_test.cpp` file. They should be done sequentially by a single implementor OR by assigning each story to a different test section in the same file session.

Phase 10 (Polish): T107, T108, T109 are independent and can be worked in parallel (different files).

---

## Implementation Strategy

### MVP: User Stories 1 and 2 Only (Phase 1 + 2 + 3 + 4)

1. Complete Phase 1: CMakeLists.txt setup
2. Complete Phase 2: Unified interface refactor (CRITICAL - blocks all stories)
3. Complete Phase 3: BowExciter Core DSP (US1)
4. Complete Phase 4: Pressure Timbral Regions (US2)
5. **STOP and VALIDATE**: BowExciter produces sustained, timbral output with working pressure control
6. The DSP component is complete and testable without plugin integration

### Incremental Delivery

1. Phase 1 + 2 + 3 (US1) --> BowExciter DSP verified in isolation
2. Add Phase 4 (US2) --> Pressure timbral regions verified
3. Add Phase 5 (US3) + Phase 6 (US4) --> Position and speed verified
4. Add Phase 7 (US5) --> Plugin integration; instrument playable with bow exciter
5. Add Phase 8 (US6) --> Modal resonator bowed-mode coupling unlocked
6. Add Phase 9 (US7) --> Oversampling quality option available
7. Phases 10-14 --> Polish, static analysis, docs, honest completion

---

## Notes

- `[P]` tasks operate on different files with no dependencies on incomplete tasks
- `[US1]`-`[US7]` labels map each task to the user story from spec.md for traceability
- Each user story is independently testable before the next story begins
- Constitution Principle XII: Tests MUST be written BEFORE implementation (all Phases 3-9)
- Constitution Principle XIV ODR: `BowExciter` and `BowedModeBPF` confirmed absent from codebase before creation
- Constitution Principle VI Cross-Platform: no platform-specific APIs; all DSP is pure C++20
- All new files follow the existing monorepo header-only pattern for Layer 2 processors
- Build with full path on Windows: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target <target>`
- The post-build copy step may fail with a permission error (copying to `C:/Program Files/Common Files/VST3/`) -- this is expected and does not indicate a compilation failure
- **NEVER claim completion if ANY requirement is not met** -- document gaps honestly in spec.md instead
