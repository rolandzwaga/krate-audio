# Tasks: Impact Exciter (Spec 128)

**Input**: Design documents from `/specs/128-impact-exciter/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/ (all present)
**Plugin**: Innexus
**Branch**: `128-impact-exciter`

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

### Integration Tests (MANDATORY for Voice Integration Phases)

This feature wires ImpactExciter into the Innexus processor audio chain (MIDI routing, audio processing, parameter application). Integration tests are **required** for Phases 6 and 7 (Voice Integration and Retrigger Safety). Key rules:

- **Behavioral correctness over existence checks**: Verify output amplitude/spectrum is correct, not just that "audio exists."
- **Test degraded host conditions**: Retrigger safety must be tested under rapid-fire MIDI conditions.
- **Test per-block configuration safety**: Ensure `applyParamsToEngine()` callers don't silently reset pulse state mid-burst.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection, add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` or `plugins/innexus/tests/CMakeLists.txt`.
2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality.
3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits).

---

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1-US5)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Register new parameters and extend data model. These foundational changes are required before any DSP or voice integration work begins.

- [X] T001 Add `ExciterType` enum and parameter IDs 805-809 (`kExciterTypeId`, `kImpactHardnessId`, `kImpactMassId`, `kImpactBrightnessId`, `kImpactPositionId`) to `plugins/innexus/src/plugin_ids.h` per contract `specs/128-impact-exciter/contracts/parameter_ids.h`
- [X] T002 Add five `std::atomic<float>` members (`exciterType_`, `impactHardness_`, `impactMass_`, `impactBrightness_`, `impactPosition_`) to `plugins/innexus/src/processor/processor.h`
- [X] T003 Register the five new parameters in `plugins/innexus/src/controller/controller.cpp` following the existing StringListParameter (kExciterTypeId) and RangeParameter (kImpactHardnessId/MassId/BrightnessId/PositionId) patterns
- [X] T004 Add state save/load for the five new parameter atomics in `plugins/innexus/src/processor/processor_state.cpp` following the existing IBStream pattern

**Checkpoint**: Parameters are registered and persisted. Plugin compiles. No DSP or voice logic yet.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core DSP infrastructure that MUST be complete before any user story can be tested. XorShift32 (Layer 0) and ModalResonatorBank decayScale extension are consumed by nearly every subsequent phase.

**CRITICAL**: No user story work can begin until this phase is complete.

- [X] T005 Write failing unit tests for `XorShift32` in `dsp/tests/unit/core/xorshift32_test.cpp` covering: non-zero state invariant after seed, unique sequences across voiceIds 0-7, `nextFloat()` range [0.0, 1.0), `nextFloatSigned()` range [-1.0, 1.0), determinism (same seed = same sequence), and the absorbing-state guard
- [X] T006 Implement `XorShift32` struct at `dsp/include/krate/dsp/core/xorshift32.h` per contract `specs/128-impact-exciter/contracts/xorshift32_api.h` (3 XOR/shift ops, `seed()` multiplicative hash `0x9E3779B9u ^ (voiceId * 0x85EBCA6Bu)`, absorbing-state guard)
- [X] T007 Verify `XorShift32` tests pass: build `dsp_tests`, run `dsp_tests.exe "XorShift32*"`
- [X] T008 Add `xorshift32_test.cpp` to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if it uses IEEE 754 functions
- [X] T009 Write failing unit tests for the `ModalResonatorBank` `decayScale` overload in `dsp/tests/unit/processors/test_modal_resonator_bank.cpp` (new test cases only): verify `processSample(excitation, 1.0f)` == `processSample(excitation)`, verify `decayScale > 1.0f` accelerates decay measurably, verify relative damping ratios preserved across modes at `decayScale = 2.0f`
- [X] T010 Add `float processSample(float excitation, float decayScale) noexcept` overload to `dsp/include/krate/dsp/processors/modal_resonator_bank.h` per contract `specs/128-impact-exciter/contracts/modal_resonator_bank_extension.h`, applying `R_eff = powf(R, decayScale)` per mode only when `decayScale != 1.0f`; refactor existing `processSample(float)` to delegate with `decayScale = 1.0f`
- [X] T011 Verify `ModalResonatorBank` decayScale tests pass: build `dsp_tests`, run `dsp_tests.exe "ModalResonatorBank*"`
- [X] T012 Commit Phase 2: XorShift32 + ModalResonatorBank decayScale extension

**Checkpoint**: Layer 0 XorShift32 and ModalResonatorBank decayScale are implemented, tested, and committed. All phases that depend on these can now proceed.

---

## Phase 3: User Story 1 - Struck-Object Sound Design (Priority: P1) - MVP

**Goal**: ImpactExciter core DSP component exists, produces a physically-motivated impact burst (asymmetric pulse + shaped noise + SVF lowpass + strike position comb), and feeds the ModalResonatorBank to create audible struck-object sounds.

**Covers FRs**: FR-001, FR-002, FR-003, FR-004, FR-005, FR-006, FR-009, FR-010, FR-011, FR-013, FR-015, FR-016, FR-017, FR-022, FR-023, FR-024, FR-033

**Success Criteria**: SC-001, SC-002, SC-003, SC-007, SC-008

**Independent Test**: Instantiate `ImpactExciter`, call `prepare()`, call `trigger(0.7f, 0.8f, 0.3f, 0.0f, 0.13f, 440.0f)`, run `processBlock()` for 512 samples, verify output is non-zero during pulse duration, then becomes zero (silent) after. Sweep hardness 0.0 to 1.0 and confirm spectral centroid increases monotonically.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T013 [P] [US1] Write failing unit tests for the `ImpactExciter` lifecycle and pulse shape in `dsp/tests/unit/processors/impact_exciter_test.cpp`:
  - `prepare()` transitions to `isPrepared() == true`
  - `isActive()` is false before trigger, true during pulse, false after pulse completes
  - `processBlock()` produces non-zero output after trigger, zero output after pulse duration
  - Pulse is zero after `T_max + bounce_delay + bounce_duration` samples at all mass settings
  - `reset()` clears active state (all subsequent `process()` calls return 0.0f)

- [ ] T014 [P] [US1] Write failing unit tests for pulse shape mathematics in `dsp/tests/unit/processors/impact_exciter_test.cpp`:
  - Pulse peak occurs earlier in time at high hardness (skewed), later at low hardness (FR-003)
  - Pulse duration increases with mass: `trigger(vel, h, 0.0)` produces shorter burst than `trigger(vel, h, 1.0)` (FR-005)
  - Pulse amplitude scales nonlinearly with velocity: amplitude at vel=1.0 is NOT 2x amplitude at vel=0.25 (FR-006: `pow(v, 0.6)`)
  - Rise-time measurement: defined as time from pulse start to 90% of peak amplitude. At hardness=0.8, rise-time MUST be less than 50% of rise-time at hardness=0.2 (SC-003: verifies asymmetry is audibly different at hard vs soft settings)

- [ ] T015 [P] [US1] Write failing unit tests for the SVF lowpass and brightness trim in `dsp/tests/unit/processors/impact_exciter_test.cpp`:
  - Output spectral centroid at hardness=1.0 is measurably higher than at hardness=0.0 (FR-015)
  - Brightness trim at default 0.0 produces same spectral centroid as without trim (FR-017)
  - Brightness trim at +1.0 increases spectral centroid vs 0.0 (FR-016)
  - Brightness trim at -1.0 decreases spectral centroid vs 0.0 (FR-016)

- [ ] T016 [P] [US1] Write failing unit tests for strike position comb filter in `dsp/tests/unit/processors/impact_exciter_test.cpp`:
  - Position 0.0 and position 0.5 produce different output spectra (FR-024)
  - Position 0.5 measurably attenuates even harmonics vs position 0.0 (SC-008)
  - Position 0.13 (default) produces output with all harmonics present (FR-024 "sweet spot")
  - 70/30 blend: output at non-zero position is not identical to either dry or fully wet (FR-023)

### 3.2 Implementation for User Story 1

- [ ] T017 [US1] Implement `ImpactExciter` class in `dsp/include/krate/dsp/processors/impact_exciter.h` per contract `specs/128-impact-exciter/contracts/impact_exciter_api.h`:
  - All fields from data-model.md (pulse state, bounce state, noise state, SVF, DelayLine, energy capping, attack ramp)
  - `prepare(double sampleRate, uint32_t voiceId)`: allocates SVF (Lowpass mode, `snapToTarget()`), DelayLine (55ms at given sampleRate), seeds XorShift32, computes `energyDecay_`
  - `reset()`: clears SVF, DelayLine, sets `pulseActive_ = false`, `bounceActive_ = false`, `energy_ = 0.0f`, `pinkState_ = 0.0f`

- [ ] T018 [US1] Implement `trigger()` in `dsp/include/krate/dsp/processors/impact_exciter.h`:
  - Compute `effectiveHardness = clamp(hardness + velocity * 0.1f, 0.0f, 1.0f)` (FR-018)
  - Compute `gamma_ = 1.0f + 3.0f * effectiveHardness` (FR-004)
  - Compute `skew_ = 0.3f * effectiveHardness` (FR-003)
  - Compute `amplitude_ = powf(velocity, 0.6f)` (FR-006)
  - Compute `T` from mass: `T_min + (T_max - T_min) * powf(mass, 0.4f)` where T_min=0.5ms, T_max=15ms (FR-005)
  - Apply velocity duration shortening: `T *= powf(1.0f - velocity, 0.2f)` (FR-020)
  - Apply micro-variation to gamma and T: `gamma_ *= (1.0f + rng_.nextFloatSigned() * 0.02f)`, `T *= (1.0f + rng_.nextFloatSigned() * 0.05f)` (FR-014)
  - Compute SVF cutoff from effectiveHardness (500 Hz at 0.0, 12 kHz at 1.0, exponential mapping), apply brightness offset `baseCutoff * exp2f(brightness)`, apply velocity modulation `cutoff *= exp2f(velocity * 1.5f)`, clamp to valid range; call `svf_.setCutoff(cutoff)` then `svf_.snapToTarget()` (FR-015, FR-016, FR-019)
  - Set `combDelaySamples_` from position and f0: `floorf(position * sampleRate_ / f0)`, clamp to DelayLine max; set `combWet_ = 0.7f` (FR-022, FR-023)
  - Compute `noiseLevel_` = `lerp(0.25f, 0.08f, effectiveHardness)` (FR-011)
  - Set `pulseSamplesTotal_`, `pulseSampleCounter_ = 0`, `attackRampCounter_ = 0`, `pulseActive_ = true` (FR-033)

- [ ] T019 [US1] Implement `process()` in `dsp/include/krate/dsp/processors/impact_exciter.h`:
  - Early-out if not `pulseActive_` and not `bounceActive_`: return 0.0f (performance optimization from plan.md)
  - Compute pulse sample: `t = pulseSampleCounter_ / pulseSamplesTotal_`, `skewedX = powf(t, 1.0f - skew_)`, `pulseSample = amplitude_ * powf(sinf(kPi * skewedX), gamma_)` (FR-003, FR-004)
  - Generate noise: `white = rng_.nextFloatSigned()`, apply one-pole pinking `pink = white - 0.9f * pinkState_; pinkState_ = pink` (FR-010 fixed `b=0.9f`), blend based on hardness, envelope with `pulseSample` envelope factor (FR-009), scale by `noiseLevel_` (FR-011)
  - Mix pulse + noise: `combined = pulseSample + noiseComponent` (FR-002)
  - Apply attack ramp: multiply by `min(1.0f, attackRampCounter_++ / attackRampSamples_)` (FR-033)
  - Apply SVF: `filtered = svf_.process(combined)` (FR-013, FR-015)
  - Apply comb filter using DelayLine: `combDelay_.write(filtered)`, `combOut = filtered - combDelay_.read(combDelaySamples_)`, blend: `output = lerp(filtered, combOut, combWet_)` (FR-022, FR-023)
  - Apply energy capping: update `energy_`, apply `gain = threshold_ / energy_` if `energy_ > threshold_` (FR-034)
  - Advance `pulseSampleCounter_`, set `pulseActive_ = false` when counter reaches total (FR-001)

- [ ] T020 [US1] Implement `processBlock()` in `dsp/include/krate/dsp/processors/impact_exciter.h`: loop over `process()` filling `output[i]` (FR-001 convenience wrapper)

- [ ] T021 [US1] Verify all User Story 1 tests pass: build `dsp_tests`, run `dsp_tests.exe "ImpactExciter*"`

### 3.3 Cross-Platform Verification

- [ ] T022 [US1] Verify IEEE 754 compliance in `dsp/tests/unit/processors/impact_exciter_test.cpp`: if any test uses `std::isnan`/`std::isfinite`/`std::isinf`, add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 3.4 Commit

- [ ] T023 [US1] Commit completed User Story 1 work: XorShift32 + ModalResonatorBank decayScale (Phase 2) + ImpactExciter core DSP

**Checkpoint**: `ImpactExciter` is a standalone tested DSP component. It can be instantiated in isolation and produces physically-motivated excitation bursts. All Phase 3 tests pass.

---

## Phase 4: User Story 2 - Velocity-Expressive Performance (Priority: P1)

**Goal**: MIDI velocity produces multi-dimensional timbral response (loudness + brightness + effective hardness simultaneously) through the exponential coupling already baked into `trigger()`. This phase adds tests that verify the coupling measurably and wires velocity from the voice's note-on path into `ImpactExciter::trigger()`.

**Covers FRs**: FR-018, FR-019, FR-020, FR-021 (all in trigger() from Phase 3), plus the voice integration path

**Success Criteria**: SC-004, SC-005

**Independent Test**: In `impact_exciter_test.cpp`, trigger at velocity=30/127 (normalized 0.235/1.0) with same hardness=0.5, compare output buffers measuring: (1) amplitude at peak is higher for ff, (2) spectral centroid is higher for ff, (3) pulse duration is shorter for ff.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T024 [P] [US2] Write failing unit tests for multi-dimensional velocity response in `dsp/tests/unit/processors/impact_exciter_test.cpp`:
  - At same hardness=0.5, velocity 1.0 vs velocity 0.2: output peak amplitude is higher at vel=1.0 (amplitude = `pow(v, 0.6)`)
  - At same hardness=0.5, velocity 1.0 vs velocity 0.2: output spectral centroid is measurably higher at vel=1.0 (exponential cutoff modulation, SC-004)
  - At same hardness=0.5, velocity 1.0 vs velocity 0.2: pulse duration is shorter at vel=1.0 (`T *= pow(1 - v, 0.2)`, SC-004)
  - Velocity coupling is NOT linear: spectral centroid at vel=0.5 is NOT exactly midway between vel=0.0 and vel=1.0 (SC-004, FR-021)
  - At same hardness=0.5, pulse durations at vel=0.0, vel=0.5, vel=1.0 form a non-linear curve: T(0.5) is NOT midway between T(0.0) and T(1.0) (verifies FR-021 for duration mapping)

- [ ] T025 [P] [US2] Write failing integration tests for velocity routing in `plugins/innexus/tests/unit/processor/test_physical_model.cpp`:
  - On note-on with Impact exciter selected, `impactExciter.trigger()` receives normalized velocity from `InnexusVoice::velocityGain` (not 0 or 1 default)
  - Two notes at same pitch, same hardness, different velocities produce different peak amplitudes from resonator

### 4.2 Implementation for User Story 2

- [ ] T026 [US2] Extend `InnexusVoice` in `plugins/innexus/src/processor/innexus_voice.h`: add `ImpactExciter impactExciter` member and choke state fields (`chokeDecayScale_`, `chokeEnvelope_`, `chokeEnvelopeCoeff_`, `chokeMaxScale_`) per data-model.md InnexusVoice Extensions section

- [ ] T027 [US2] Extend `initVoiceForNoteOn()` in `plugins/innexus/src/processor/processor_midi.cpp`: when `exciterType_ == ExciterType::Impact`, call `voice.impactExciter.trigger(velocityGain, hardness, mass, brightness, position, f0)` reading normalized params from processor atomics (hardness, mass, brightness, position) and voice's current f0

- [ ] T028 [US2] Extend the voice processing loop in `plugins/innexus/src/processor/processor.cpp`: when `exciterType_ == ExciterType::Impact`, compute `excitation = voice.impactExciter.process()` instead of `residualSynth.process()`, pass excitation to `voice.modalResonator.processSample(excitation, voice.chokeDecayScale_)` (FR-030, FR-031)

- [ ] T029 [US2] Call `impactExciter.prepare(sampleRate, voiceIndex)` and `impactExciter.reset()` at the appropriate lifecycle points in `plugins/innexus/src/processor/processor.cpp` (alongside existing `residualSynth` prepare/reset calls)

- [ ] T030 [US2] Verify all User Story 2 tests pass: build `dsp_tests` and `innexus_tests`, run `dsp_tests.exe "ImpactExciter*"` and `innexus_tests.exe`

### 4.3 Cross-Platform Verification

- [ ] T031 [US2] Verify IEEE 754 compliance in `plugins/innexus/tests/unit/processor/test_physical_model.cpp`: add to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` if needed

### 4.4 Commit

- [ ] T032 [US2] Commit completed User Story 2 work: ImpactExciter velocity coupling tests + voice integration (exciter trigger on note-on, voice loop switch)

**Checkpoint**: Playing MIDI notes with Impact exciter selected produces audible struck-object output with velocity-proportional loudness and brightness. All Phase 4 tests pass.

---

## Phase 5: User Story 3 - Per-Trigger Naturalness (Priority: P2)

**Goal**: Each trigger of the same note produces a subtly different output, preventing "machine gun" repetitions. Driven entirely by per-trigger micro-variation (FR-014) and per-bounce randomization (FR-007, FR-008) already seeded in `trigger()`. This phase adds verification tests and implements the micro-bounce subsystem.

**Covers FRs**: FR-007, FR-008, FR-012, FR-014

**Success Criteria**: SC-006

**Independent Test**: Call `trigger()` ten times with identical parameters on the same `ImpactExciter` instance. Collect ten 512-sample output buffers. Assert no two are bit-identical (SC-006).

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T033 [P] [US3] Write failing unit tests for per-trigger micro-variation in `dsp/tests/unit/processors/impact_exciter_test.cpp`:
  - Trigger 10 times with identical parameters: all 10 output buffers are pairwise non-identical (SC-006)
  - Variation is subtle: root-mean-square difference between any two buffers is less than 10% of the signal RMS (SC-006 "subtle but audible")
  - Per-voice isolation: two `ImpactExciter` instances with different voiceIds produce different noise sequences from the start even with identical trigger parameters (FR-012)
  - Polyphonic RNG isolation: in a polyphonic context (two `ImpactExciter` instances representing two chord voices), the noise components do not produce identical phase patterns -- verified by confirming the two output buffers differ from sample 0 onward (FR-012, US3 acceptance scenario 3)

- [ ] T034 [P] [US3] Write failing unit tests for micro-bounce in `dsp/tests/unit/processors/impact_exciter_test.cpp`:
  - At hardness=0.8: output has two distinct peaks (primary + secondary bounce) (FR-007)
  - At hardness=0.4: output has only one peak (no bounce below threshold 0.6) (FR-007)
  - Bounce delay and amplitude vary across triggers (randomized per FR-008): compare bounce peak timing across 5 triggers

### 5.2 Implementation for User Story 3

- [ ] T035 [US3] Implement micro-bounce in `trigger()` in `dsp/include/krate/dsp/processors/impact_exciter.h`: when `effectiveHardness > 0.6f`, compute `bounceDelay_` in samples (0.5-2ms, shorter for harder strikes: `delay_s = lerp(0.002f, 0.0005f, (effectiveHardness - 0.6f) / 0.4f)`), randomize `bounceDelay_ *= (1.0f + rng_.nextFloatSigned() * 0.15f)`, compute `bounceAmplitude_` (10-20% of primary, less for harder: `lerp(0.2f, 0.1f, (effectiveHardness - 0.6f) / 0.4f)`), randomize amplitude similarly (FR-007, FR-008), set `bounceActive_ = false`, `bounceDelayCounter_ = 0`, `bounceSamplesTotal_` = pulse duration samples, `bounceGamma_ = gamma_`

- [ ] T036 [US3] Extend `process()` to handle micro-bounce in `dsp/include/krate/dsp/processors/impact_exciter.h`: track `bounceDelayCounter_` counting down to bounce start, when active compute bounce pulse sample using same skewed half-sine formula with `bounceGamma_` and `bounceAmplitude_`, add to main signal before SVF (FR-007)

- [ ] T037 [US3] Verify all User Story 3 tests pass: build `dsp_tests`, run `dsp_tests.exe "ImpactExciter*"`

### 5.3 Cross-Platform Verification

- [ ] T038 [US3] Verify IEEE 754 compliance: no new test files requiring `-fno-fast-math` annotation for this phase (micro-variation tests use deterministic integer ops)

### 5.4 Commit

- [ ] T039 [US3] Commit completed User Story 3 work: micro-bounce subsystem + per-trigger variation tests

**Checkpoint**: Identical trigger parameters produce measurably different output each time. Micro-bounce appears for hardness > 0.6. All Phase 5 tests pass.

---

## Phase 6: User Story 4 - Creative Sound Shaping (Priority: P2)

**Goal**: Brightness trim and strike position provide independent creative axes beyond the physical model defaults. This phase focuses on verifying the existing SVF brightness offset and comb filter strike-position behavior with dedicated tests and parameter routing for the new plugin parameters.

**Covers FRs**: FR-016, FR-017, FR-022, FR-023, FR-024, FR-025 (parameter routing)

**Success Criteria**: SC-007, SC-008

**Independent Test**: At hardness=0.8 (hard pulse), set brightness=-1.0: confirm spectral centroid decreases vs brightness=0.0. At position=0.5 vs position=0.0: confirm even harmonics are relatively attenuated at 0.5 (measure amplitudes at 2nd, 4th harmonic).

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T040 [P] [US4] Write failing unit tests for brightness trim in `dsp/tests/unit/processors/impact_exciter_test.cpp`:
  - High hardness + brightness=-1.0: spectral centroid lower than hardness alone at brightness=0.0 (SC-007 "sharp transient but dark tone")
  - Brightness=0.0: spectral centroid equals baseline hardness mapping exactly (FR-017, SC-007)
  - Brightness=+1.0: spectral centroid higher than brightness=0.0 (SC-007)
  - cutoff shift at +/-1.0 is approximately +/-12 semitones from the brightness=0.0 cutoff (SC-007: verify `cutoff_+1 / cutoff_0 ≈ 2.0`, within 5% tolerance)

- [ ] T041 [P] [US4] Write failing unit tests for strike position in `dsp/tests/unit/processors/impact_exciter_test.cpp`:
  - Position=0.0 (edge): second harmonic is NOT attenuated relative to fundamental (FR-024, SC-008)
  - Position=0.5 (center): second harmonic amplitude measurably lower than at position=0.0 (SC-008)
  - Position=0.13 (default): output is between 0.0 and 0.5 in harmonic content (FR-024 "sweet spot")
  - Blend factor: output at position=0.5 is NOT equal to pure comb filter output (confirms 70/30 blend, FR-023)

- [ ] T042 [P] [US4] Write failing VST parameter tests for new Innexus parameters in `plugins/innexus/tests/unit/vst/innexus_vst_tests.cpp`:
  - `kExciterTypeId` (805) range: 0 to 2, default 0
  - `kImpactHardnessId` (806) range: 0.0-1.0, default 0.5
  - `kImpactMassId` (807) range: 0.0-1.0, default 0.3
  - `kImpactBrightnessId` (808) normalized range: 0.0-1.0 maps to -1.0-+1.0 plain, default norm=0.5
  - `kImpactPositionId` (809) range: 0.0-1.0, default 0.13
  - Verify parameters are persisted correctly via save/load round-trip (processor_state)

### 6.2 Implementation for User Story 4

- [ ] T043 [US4] Verify parameter routing: confirm `processor.cpp` reads `impactBrightness_` and `impactPosition_` atomics and passes denormalized values to `impactExciter.trigger()` (brightness: `param * 2.0f - 1.0f` for -1..+1 from normalized 0..1). Fix any routing gaps found.

- [ ] T044 [US4] Add edge-case guard in `process()` for position=0.0 in `dsp/include/krate/dsp/processors/impact_exciter.h`: when `combDelaySamples_ < 1`, skip comb filter computation entirely and return filtered signal directly (performance optimization from plan.md, also ensures FR-024 boundary behavior)

- [ ] T045 [US4] Verify all User Story 4 tests pass: build `dsp_tests` and `innexus_tests`, run `dsp_tests.exe "ImpactExciter*"` and `innexus_tests.exe`

### 6.3 Cross-Platform Verification

- [ ] T046 [US4] Verify IEEE 754 compliance in `plugins/innexus/tests/unit/vst/innexus_vst_tests.cpp`: add to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` if needed

### 6.4 Commit

- [ ] T047 [US4] Commit completed User Story 4 work: brightness trim + strike position tests + parameter routing verified

**Checkpoint**: Brightness trim and strike position produce verifiable spectral changes. VST parameter registration is confirmed correct for all 5 new parameters. All Phase 6 tests pass.

---

## Phase 7: User Story 5 - Click-Free Retrigger and Energy Safety (Priority: P1)

**Goal**: Rapid retriggering cannot cause energy explosion, produces no clicks, and retrigger on a ringing note delivers natural "cut-then-ring" damping proportional to retrigger velocity.

**Covers FRs**: FR-032, FR-033, FR-034, FR-035

**Success Criteria**: SC-009, SC-010, SC-011

**Independent Test**: Send 100 rapid note-on events per second on the same pitch for 1 second. Measure that peak output amplitude never exceeds 4x the single-strike peak (SC-010). Check output for sample-level discontinuities at each retrigger point (SC-009).

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T048 [P] [US5] Write failing unit tests for energy capping in `dsp/tests/unit/processors/impact_exciter_test.cpp`:
  - Single trigger: peak output is at expected amplitude level (baseline)
  - 100 rapid triggers (1ms apart at 44100 Hz = every 44 samples): peak output never exceeds 4x single-strike peak (SC-010)
  - After rapid retrigger storm, output eventually decays to near-zero when no new triggers arrive (energy dissipates naturally, no stuck gain)
  - Energy capping acts gradually (soft gain reduction), not as hard clipping: no sudden amplitude step at threshold

- [ ] T049 [P] [US5] Write failing unit tests for click-free retrigger in `dsp/tests/unit/processors/impact_exciter_test.cpp`:
  - Retrigger after 10ms: no sample-level discontinuity at retrigger point (difference between sample N and N+1 is less than 0.01 at trigger boundary) (SC-009)
  - Attack ramp makes the onset slope gradual: first 13 samples (at 44100 Hz, ~0.3ms) ramp from 0 to full amplitude (FR-033)

- [ ] T050 [P] [US5] Write failing integration tests for mallet choke in `plugins/innexus/tests/unit/processor/test_physical_model.cpp`:
  - Retrigger on ringing note: modal resonator output immediately after retrigger is measurably lower than before retrigger (choke active, SC-011)
  - Hard retrigger (velocity=1.0) produces more attenuation than gentle retrigger (velocity=0.2) (SC-011)
  - After ~10ms, choke envelope recovers to 1.0 and resonator behaves normally again (FR-035)
  - Choke does NOT reset resonator state (FR-032): some residual vibration remains even after maximum choke

### 7.2 Implementation for User Story 5

- [ ] T051 [US5] Implement mallet choke envelope logic in `plugins/innexus/src/processor/processor_midi.cpp`: on note-on retrigger (note already playing), set `voice.chokeMaxScale_` proportional to velocity (`kMaxChoke = 1.0f + velocity * 2.0f` or similar empirical value), set `voice.chokeEnvelope_ = 0.0f` (full choke), compute `voice.chokeEnvelopeCoeff_ = expf(-1.0f / (0.01f * sampleRate))` (~10ms decay) (FR-035)

- [ ] T052 [US5] Update the voice processing loop in `plugins/innexus/src/processor/processor.cpp`: advance choke envelope each sample (`chokeEnvelope_ = 1.0f - (1.0f - chokeEnvelope_) * chokeEnvelopeCoeff_`), compute `chokeDecayScale_ = lerp(chokeMaxScale_, 1.0f, chokeEnvelope_)`, pass `chokeDecayScale_` to `modalResonator.processSample(excitation, chokeDecayScale_)` (FR-035)

- [ ] T053 [US5] Verify energy capping is correctly implemented in `ImpactExciter::process()`: confirm `energyThreshold_` is initialized to approximately 4x single-strike RMS in `prepare()` (compute single-strike energy analytically or empirically from pulse formula), fix threshold if tests fail (FR-034)

- [ ] T054 [US5] Verify all User Story 5 tests pass: build `dsp_tests` and `innexus_tests`, run `dsp_tests.exe "ImpactExciter*"` and `innexus_tests.exe`

### 7.3 Cross-Platform Verification

- [ ] T055 [US5] Verify IEEE 754 compliance in `plugins/innexus/tests/unit/processor/test_physical_model.cpp`: add to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` if it uses `std::isfinite` or similar for amplitude bound checks

### 7.4 Commit

- [ ] T056 [US5] Commit completed User Story 5 work: energy capping tests + mallet choke integration + retrigger click-free verification

**Checkpoint**: Rapid retrigger is energy-safe. Retrigger produces natural choke. No clicks at onset. All Phase 7 tests pass.

---

## Phase 8: Polish and Cross-Cutting Concerns

**Purpose**: Performance validation, build integration, pluginval, and final cleanup.

- [ ] T057 [P] Build full Innexus plugin and run pluginval: `cmake --build build/windows-x64-release --config Release`, then `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"` - fix any failures
- [ ] T058 [P] Verify CPU budget: add a performance test to `dsp/tests/unit/processors/impact_exciter_test.cpp` that measures CPU time for `processBlock(output, 512)` on a triggered exciter at 44100 Hz, assert single-voice cost is < 0.1% CPU (SC-012) using wall-clock measurement. Note: if wall-clock measurement produces non-deterministic CI failures, replace with instruction count measurement (using RDTSC or equivalent) or document that SC-012 is verified by a manual profiling session rather than automated test.
- [ ] T059 [P] Verify all dsp_tests and innexus_tests pass together with a clean release build: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure | tail -10`
- [ ] T060 Add `impact_exciter_test.cpp` to `dsp/tests/CMakeLists.txt` source list (if not already added during T013, ensure the file is explicitly listed as a test source)
- [ ] T061 Add `xorshift32_test.cpp` to `dsp/tests/CMakeLists.txt` source list (if not already added during T005)
- [ ] T062 Verify the early-out optimization in `ImpactExciter::process()` is in place: when `!pulseActive_ && !bounceActive_`, the function returns immediately without calling SVF or DelayLine (plan.md performance optimization)
- [ ] T082 Verify FR-036 broadband compliance: measure the power spectral density of the `ImpactExciter` output at default parameters (hardness=0.5, mass=0.3, brightness=0.0, position=0.13, velocity=0.5) and confirm energy is present across 0-8 kHz. Document that the architecture (exciter outputs a scalar; `ModalResonatorBank` accepts it uniformly) does not preclude per-mode weighting. Record findings in the FR-036 compliance row of spec.md.

---

## Phase 9: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation per Constitution Principle XIV (Living Architecture Documentation).

- [ ] T063 [P] Update `specs/_architecture_/layer-0-core.md`: add `XorShift32` entry with purpose (deterministic per-voice PRNG), public API summary (`seed()`, `next()`, `nextFloat()`, `nextFloatSigned()`), file location (`dsp/include/krate/dsp/core/xorshift32.h`), and when to use (any processor needing deterministic per-voice noise without stdlib dependency)
- [ ] T064 [P] Update `specs/_architecture_/layer-2-processors.md` (or equivalent processors section): add `ImpactExciter` entry with purpose (hybrid pulse+noise impact excitation burst), sub-systems (pulse shaping, micro-bounce, noise texture, SVF filter, comb filter, energy capping), file location (`dsp/include/krate/dsp/processors/impact_exciter.h`), and key design notes (Layer 1 deps: SVF + DelayLine; per-voice state; non-copyable)
- [ ] T065 Update `specs/_architecture_/layer-2-processors.md` ModalResonatorBank entry: document the new `decayScale` overload and its mallet choke use case
- [ ] T066 Commit architecture documentation updates

**Checkpoint**: Architecture docs reflect all new components. Feature branch is clean.

---

## Phase N-1.0: Static Analysis (MANDATORY)

**Purpose**: Clang-tidy quality gate before final completion verification.

- [ ] T067 Generate `compile_commands.json` via ninja preset if not current: `cmake --preset windows-ninja` (or use existing build if up to date)
- [ ] T068 Run clang-tidy on new and modified DSP files: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`
- [ ] T069 Run clang-tidy on new and modified Innexus plugin files: `./tools/run-clang-tidy.ps1 -Target innexus -BuildDir build/windows-ninja`
- [ ] T070 Fix all clang-tidy errors (blocking). Review warnings and fix where appropriate for production audio code.
- [ ] T071 Document any intentional suppressions with `// NOLINT(<check-name>): <reason>` comments if any warnings are intentionally ignored in DSP-specific code
- [ ] T072 Commit clang-tidy fixes

**Checkpoint**: Static analysis clean - ready for completion verification.

---

## Phase N-1: Completion Verification (MANDATORY)

**Purpose**: Honest verification of all requirements before claiming completion.

- [ ] T073 **Review ALL FR-001 through FR-036** from `specs/128-impact-exciter/spec.md` against the actual implementation: open each relevant file, read the code, confirm the requirement is met, note file path and line number
- [ ] T074 **Review ALL SC-001 through SC-012** from `specs/128-impact-exciter/spec.md`: run or read specific test output for each, confirm measurable targets are achieved with actual numbers
- [ ] T075 **Search for cheating patterns**: `grep -r "TODO\|placeholder\|FIXME\|stub" dsp/include/krate/dsp/processors/impact_exciter.h dsp/include/krate/dsp/core/xorshift32.h plugins/innexus/src/` - must return empty
- [ ] T076 **Fill the compliance table in `specs/128-impact-exciter/spec.md`** "Implementation Verification" section with honest status, file paths, line numbers, and measured values for every FR-xxx and SC-xxx row
- [ ] T077 **Honest self-check**: Answer all 5 questions in the template. If ANY answer is "yes", fix the gap before proceeding.
- [ ] T078 Commit compliance table update to `specs/128-impact-exciter/spec.md`
- [ ] T083 Conduct manual listening test with at least 3 analyzed sources (e.g., voice, glass bell, metallic percussive) using Impact exciter at default settings. Document perceptual result (does each source produce a convincing struck-object sound?) in the SC-001 row of the spec.md compliance table.

**Checkpoint**: Honest compliance table is complete. No gaps hidden.

---

## Phase N: Final Completion

- [ ] T079 Verify all tests pass on a clean build: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure | tail -5`
- [ ] T080 Verify feature branch `128-impact-exciter` is current with all work committed: `git status` must show clean working tree
- [ ] T081 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user). If any FR or SC is unmet, document it honestly rather than claiming completion.

---

## Dependencies and Execution Order

### Phase Dependencies

```
Phase 1 (Setup)
  └── T001-T004: Parameter IDs, atomics, registration, state I/O
      └── Blocks all voice integration phases (4, 7)

Phase 2 (Foundational)
  └── T005-T011: XorShift32 + ModalResonatorBank decayScale
      └── Blocks Phase 3 (ImpactExciter uses XorShift32)
      └── Blocks Phase 7 (choke uses decayScale overload)

Phase 3 (US1 - Core DSP)
  └── T013-T023: ImpactExciter class + tests
      └── Blocks Phase 4 (voice wiring needs the class)
      └── Blocks Phase 5 (bounce extends process())
      └── Blocks Phase 6 (comb/SVF are in process())

Phase 4 (US2 - Velocity + Voice Integration)
  └── T024-T032: velocity tests + InnexusVoice wiring
      └── Blocks Phase 7 (choke wiring builds on voice loop changes)

Phase 5 (US3 - Micro-Variation)       [can run after Phase 3, parallel with Phase 6]
  └── T033-T039: bounce + variation tests

Phase 6 (US4 - Sound Shaping)         [can run after Phase 3, parallel with Phase 5]
  └── T040-T047: brightness/position tests + VST param tests

Phase 7 (US5 - Retrigger Safety)
  └── T048-T056: energy capping + choke tests + integration
      └── Requires Phase 2 (decayScale), Phase 4 (voice loop structure)

Phase 8 (Polish)
  └── T057-T062: pluginval + CPU + build cleanup
      └── Requires all user story phases complete

Phase 9 (Architecture Docs)           [can run parallel with Phase 8]
  └── T063-T066

Phase N-1.0 (Clang-Tidy)
  └── T067-T072: requires all implementation complete

Phase N-1 (Completion Verification)
  └── T073-T078: requires clang-tidy clean

Phase N (Final)
  └── T079-T081
```

### User Story Dependencies

- **US1 (P1)**: Depends on Phase 2. No dependency on other stories. Start immediately after Phase 2.
- **US2 (P1)**: Depends on Phase 3 (ImpactExciter class must exist for voice wiring). Depends on Phase 1 (atomics must exist).
- **US3 (P2)**: Depends on Phase 3 (extends `process()` and `trigger()`). Independent of US2.
- **US4 (P2)**: Depends on Phase 3 (SVF and comb already in `process()`). Independent of US2 and US3.
- **US5 (P1)**: Depends on Phase 2 (decayScale overload) and Phase 4 (voice loop structure for choke wiring). Test phase (T048/T049) can be written after Phase 3.

### Parallel Opportunities Within Phases

**Phase 2**: T005-T007 (XorShift32) and T009-T011 (ModalResonatorBank) can run in parallel - different files.

**Phase 3 tests** (T013, T014, T015, T016): All four test groups can be written in parallel - they all target `impact_exciter_test.cpp` but cover independent behaviors. Writing order matters; implementation begins only after all tests exist and fail.

**Phase 5 + Phase 6**: Can run concurrently after Phase 3, as they touch different aspects of `ImpactExciter` (bounce in process() vs. parameter routing verification).

**Phase 8 + Phase 9**: T057-T062 (polish) and T063-T066 (architecture docs) are independent and can run in parallel.

---

## Parallel Execution Example: Phase 3 Tests

```
# All four test groups for User Story 1 can be written in parallel:
Task T013: ImpactExciter lifecycle + pulse shape tests  → impact_exciter_test.cpp (section A)
Task T014: Pulse shape mathematics tests                → impact_exciter_test.cpp (section B)
Task T015: SVF + brightness trim tests                  → impact_exciter_test.cpp (section C)
Task T016: Strike position comb filter tests            → impact_exciter_test.cpp (section D)

# Then implementation follows in dependency order:
Task T017: ImpactExciter class skeleton + prepare/reset
Task T018: trigger() implementation (depends on T017)
Task T019: process() implementation (depends on T017, T018)
Task T020: processBlock() wrapper (depends on T019)
```

---

## Implementation Strategy

### MVP First (User Stories 1 + 2 Only)

1. Complete Phase 1: Setup (parameters, atomics, registration, state I/O)
2. Complete Phase 2: Foundational (XorShift32 + ModalResonatorBank decayScale)
3. Complete Phase 3: User Story 1 (ImpactExciter core DSP)
4. Complete Phase 4: User Story 2 (velocity coupling + voice wiring)
5. **STOP and VALIDATE**: Play notes with Impact exciter, verify struck-object sounds with velocity expression
6. Pluginval pass before any further development

### Incremental Delivery

- After Phase 4: Impact exciter is playable with velocity expression (US1 + US2 complete)
- After Phase 5: Rapid repetitions sound natural, no machine-gun (US3 complete)
- After Phase 6: Full sound design palette (US4 complete)
- After Phase 7: Production-safe for retrigger-heavy playing (US5 complete)
- Each phase adds value without breaking previous phases

### Risk Flags

- **Energy threshold calibration (FR-034)**: The `energyThreshold_` value (~4x single-strike energy) must be calibrated against the actual pulse formula output. T053 flags this explicitly - if T048 tests fail at this step, recalculate the threshold empirically.
- **Choke scale empirical tuning (FR-035)**: `kMaxChoke` is described as "velocity-dependent" but the exact mapping is not specified in the spec. Start with `1.0f + velocity * 2.0f` and tune against SC-011 test results.
- **Comb filter delay size at 192 kHz**: Confirmed at 55ms per research.md R-009. Verify `DelayLine::prepare()` max size at this sample rate doesn't exceed reasonable memory.

---

## Notes

- [P] tasks = different files, no dependencies on incomplete tasks in current phase
- [Story] label maps task to specific user story for traceability
- `ImpactExciter` is non-copyable (contains SVF and DelayLine with allocated buffers) - ensure InnexusVoice uses it by value or std::unique_ptr, never by copy
- SVF must have `snapToTarget()` called after `setCutoff()` at trigger time to avoid smoothing lag (plan.md gotcha)
- DelayLine uses `read(int)` for integer delay (not `readLinear(float)`) - comb filter uses `floorf()` (FR-022)
- FeedforwardComb clamps gain to [0, 1]; use DelayLine directly for negative-gain comb (research.md R-001)
- One-pole pinking filter: use `b = 0.9f` fixed constant (NOT the existing PinkNoiseFilter class) (research.md R-003)
- Skills auto-load when needed (testing-guide, vst-guide)
- NEVER claim completion if ANY requirement is not met - document gaps honestly instead
