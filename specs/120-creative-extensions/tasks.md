# Tasks: Innexus M6 -- Creative Extensions

**Input**: Design documents from `specs/120-creative-extensions/`
**Feature Branch**: `120-creative-extensions`
**Plugin**: Innexus (`plugins/innexus/`) and KrateDSP (`dsp/`)
**Created**: 2026-03-05

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story. The implementation order follows the quickstart.md phase ordering: Stereo Output (US2) → Cross-Synthesis (US1) → Evolution Engine (US3) → Harmonic Modulators (US4) → Multi-Source Blending (US5).

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow.

### Required Steps for EVERY Task

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase 8: Static Analysis)
5. **Commit**: Commit the completed work

### Integration Tests (MANDATORY When Applicable)

Any task that wires a sub-component into the processor (parameter application, stateful per-block processing, audio chain changes) requires integration tests verifying behavioral correctness, not just existence.

### Cross-Platform Compatibility Check (After Each User Story)

After implementing test files, verify: if any test uses `std::isnan()`, `std::isfinite()`, or `std::isinf()`, add that file to the `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt`.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story this task belongs to (US1=Cross-Synthesis, US2=Stereo Spread, US3=Evolution, US4=Modulators, US5=Multi-Source Blend)

---

## Phase 1: Setup -- Parameter Registration (All Stories)

**Purpose**: Register all 31 M6 parameters at once. This is a prerequisite for all user stories since parameters must be registered in the controller before any feature can be automated or saved. Grouping all parameter registrations prevents fragmented commits across stories.

**Why first**: The `plugin_ids.h`, `processParameterChanges()`, and `controller.cpp` registration are shared files touched by every user story. Doing all parameter registration up front eliminates merge conflicts and lets each story focus purely on DSP implementation.

- [X] T001 Add all M6 parameter IDs (600-649) to `plugins/innexus/src/plugin_ids.h`: kTimbralBlendId=600, kStereoSpreadId=601, kEvolutionEnableId=602, kEvolutionSpeedId=603, kEvolutionDepthId=604, kEvolutionModeId=605, kMod1EnableId=610, kMod1WaveformId=611, kMod1RateId=612, kMod1DepthId=613, kMod1RangeStartId=614, kMod1RangeEndId=615, kMod1TargetId=616, kMod2EnableId=620, kMod2WaveformId=621, kMod2RateId=622, kMod2DepthId=623, kMod2RangeStartId=624, kMod2RangeEndId=625, kMod2TargetId=626, kDetuneSpreadId=630, kBlendEnableId=640, kBlendSlotWeight1Id=641..kBlendSlotWeight8Id=648, kBlendLiveWeightId=649. Also add EvolutionMode, ModulatorWaveform, ModulatorTarget enums.

- [X] T002 Register all 31 M6 parameters in `plugins/innexus/src/controller/controller.cpp`: RangeParameters for continuous values, StringListParameters for kEvolutionModeId (Cycle/PingPong/Random Walk), kMod1WaveformId/kMod2WaveformId (Sine/Triangle/Square/Saw/Random S&H), kMod1TargetId/kMod2TargetId (Amplitude/Frequency/Pan). Use ranges per spec parameter table.

- [X] T003 Add all 31 M6 parameter atomics and 19 smoothers to `plugins/innexus/src/processor/processor.h`: atomic<float> for each param, OnePoleSmoother for timbralBlend (5ms), stereoSpread (10ms), evolutionSpeed (5ms), evolutionDepth (5ms), mod1Rate/Depth/mod2Rate/Depth/detuneSpread (5ms each), blendWeightSmoother_[9] (5ms each).

- [X] T004 Handle all 31 M6 parameter IDs in `processParameterChanges()` in `plugins/innexus/src/processor/processor.cpp`: follow existing pattern of `paramQueue->getPoint(numPoints - 1, ...)` and store into corresponding atomic.

**Checkpoint**: All parameters registered and handled. Build must succeed with zero warnings before proceeding to user stories.

---

## Phase 2: User Story 2 -- Stereo Partial Spread (Priority: P1)

**Goal**: Extend `HarmonicOscillatorBank` with stereo output and detune spread. This is implemented first (Phase 1 in quickstart) because it is a Layer 2 (KrateDSP) change that all other features depend on for output.

**Independent Test**: Synthesize a known harmonic model at Spread=0% and verify left == right. At Spread=100%, verify odd partials contribute to left channel and even partials to right channel. Measure inter-channel spectral difference.

**Requirements covered**: FR-006 through FR-013, FR-030 through FR-032, FR-046, FR-050, SC-002, SC-005, SC-007, SC-010

### 2.1 Tests for Stereo Spread (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T005 [P] [US2] Write failing tests for `processStereo()` and `setStereoSpread()` in `dsp/tests/unit/processors/test_harmonic_oscillator_bank_stereo.cpp`: test spread=0 produces identical L/R (SC-010), test spread=1.0 sends odd partials left / even partials right (SC-002), test constant-power law (panLeft^2 + panRight^2 ~= 1.0), test fundamental (partial 1) reduced spread (FR-009), test pan smoothing transition.

- [X] T006 [P] [US2] Write failing tests for `setDetuneSpread()` in `dsp/tests/unit/processors/test_harmonic_oscillator_bank_stereo.cpp`: test detune=0 produces no frequency offset, test detune=0.5 produces proportional offsets scaling with harmonic number, test odd partials detune positive / even negative, test fundamental frequency deviation < 1 cent at any spread (SC-005).

### 2.2 Implementation for User Story 2

- [X] T007 [US2] Add stereo SoA arrays and state to `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h`: `alignas(32) std::array<float, kMaxPartials> panPosition_{}`, `panLeft_{}`, `panRight_{}`, `detuneMultiplier_{}`. Add `float stereoSpread_ = 0.0f`, `detuneSpread_ = 0.0f`. Add constants `kDetuneMaxCents = 15.0f`, `kFundamentalSpreadScale = 0.25f`.

- [X] T008 [US2] Implement `setStereoSpread(float spread)` in `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h`: compute per-partial pan positions (odd left, even right, partial 1 at 25% reduced spread per FR-009), apply constant-power pan law `angle = pi/4 + panPosition * pi/4`, `panLeft[n] = cos(angle)`, `panRight[n] = sin(angle)`. Depends on T007.

- [X] T009 [US2] Implement `setDetuneSpread(float spread)` in `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h`: `detuneOffset_n = spread * n * kDetuneMaxCents * direction`, `multiplier_n = pow(2.0f, detuneOffset_n / 1200.0f)`, direction = +1 odd / -1 even (FR-030, FR-031, FR-032). Depends on T007.

- [X] T010 [US2] Implement `processStereo(float& left, float& right) noexcept` in `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h`: same MCF loop as existing `process()` but apply `panLeft_[i]` and `panRight_[i]` gains and `detuneMultiplier_[i]` to each partial's frequency. When spread=0, left==right (SC-010). Also implement `processStereoBlock(float*, float*, size_t)`. Depends on T008, T009.

- [X] T011 [US2] Wire stereo spread into processor in `plugins/innexus/src/processor/processor.cpp`: (a) call `oscillatorBank_.setStereoSpread(stereoSpreadSmoother_.process())` and `oscillatorBank_.setDetuneSpread(detuneSpreadSmoother_.process())` each frame, (b) replace all `oscillatorBank_.process()` call sites with `oscillatorBank_.processStereo(left, right)`, (c) update output assignment from mono-copy `out[0][s] = out[1][s] = sample` to `out[0][s] = left; out[1][s] = right`. Handle mono output bus: when `numOutputs == 1`, sum both channels (FR-013). Also update `plugins/innexus/src/processor/processor.h` includes. Depends on T003, T010.

- [X] T012 [US2] Write processor integration test verifying stereo output pipeline in `plugins/innexus/tests/unit/processor/test_stereo_spread_integration.cpp`: inject known HarmonicFrame, set spread=0 verify L==R, set spread=1.0 verify L≠R, verify residual is center-panned in both channels (FR-012). Also verify FR-013 mono bus behavior: configure the test processor with a single output channel (numOutputs=1), set spread=1.0, process 128 samples, verify the single output channel receives the summed mono signal (i.e., equivalent to spread=0 output energy within tolerance). Depends on T011.

### 2.3 Cross-Platform Verification

- [X] T013 [US2] Check `dsp/tests/unit/processors/test_harmonic_oscillator_bank_stereo.cpp` and `plugins/innexus/tests/unit/processor/test_stereo_spread_integration.cpp` for use of `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list in respective CMakeLists.txt if found. Also verify floating-point comparisons use `Approx().margin()`.

- [X] T014 [US2] Build `dsp_tests` and `innexus_tests` targets, verify zero warnings, run tests and confirm all pass. Verify SC-010 (bit-identical mono at spread=0), SC-002 (decorrelation > 0.8 at spread=1.0).

### 2.4 Commit

- [ ] T015 [US2] **Commit completed User Story 2 work**: stereo spread, detune spread, `processStereo()` in oscillator bank, processor wiring, and tests.

**Checkpoint**: US2 complete. `HarmonicOscillatorBank` now produces true stereo output. All stereo/detune tests pass.

---

## Phase 3: User Story 1 -- Cross-Synthesis: Timbral Blend (Priority: P1)

**Goal**: Add a Timbral Blend parameter that interpolates between a pure harmonic series and the analyzed source model, enabling carrier-modulator performance with real-time source switching.

**Independent Test**: Load a sample, play MIDI notes at blend=1.0 (verify source timbre), blend=0.0 (verify pure 1/n series), blend=0.5 (verify interpolated content). Verify source switching crossfades click-free.

**Requirements covered**: FR-001 through FR-005, FR-044, FR-047, SC-001, SC-007, SC-009

### 3.1 Tests for Cross-Synthesis (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T016 [P] [US1] Write failing tests for cross-synthesis timbral blend in `plugins/innexus/tests/unit/processor/test_cross_synthesis.cpp`: test blend=1.0 output matches source model (SC-001, correlation > 0.95), test blend=0.0 output matches pure 1/n harmonic series, test blend=0.5 produces lerped values for both relativeFreq and normalizedAmp, test inharmonic deviation scales with blend (FR-002), test parameter smoother prevents clicks (SC-007). Also test FR-003 source switching: inject two distinct HarmonicFrames representing Slot A and Slot B, simulate a slot recall while a note is active, verify the processor triggers the existing crossfade mechanism (crossfadeRemaining_ > 0 after recall) and that output amplitude changes continuously over the crossfade window with no sample-level discontinuity > -80 dBFS (SC-007).

- [X] T017 [P] [US1] Write failing test for pure harmonic reference construction in `plugins/innexus/tests/unit/processor/test_cross_synthesis.cpp`: verify L2-norm of normalizedAmps == 1.0 (+/- 1e-6), verify relativeFreqs[n] = n (1-indexed), verify inharmonicDeviation[n] = 0 for all n (FR-004, R-004).

### 3.2 Implementation for User Story 1

- [X] T018 [US1] Build pure harmonic reference in `plugins/innexus/src/processor/processor.cpp` at `setupProcessing()` time: construct `pureHarmonicFrame_` with `relativeFreq_n = n`, `rawAmp_n = 1.0f / n`, L2-normalize amps, `inharmonicDeviation_n = 0`. Store as member `HarmonicFrame pureHarmonicFrame_` in `plugins/innexus/src/processor/processor.h` (FR-004, R-004). Depends on T003.

- [X] T019 [US1] Implement Timbral Blend pipeline step in `plugins/innexus/src/processor/processor.cpp`: after source frame selection and before harmonic filter, apply `if (timbralBlend < 1.0f - epsilon) { currentFrame = lerpHarmonicFrame(pureHarmonicFrame_, currentFrame, timbralBlend); }`. Use `timbralBlendSmoother_.process()` each frame for smoothed value (FR-001, FR-002, FR-005, FR-047). Depends on T003, T018.

### 3.3 Cross-Platform Verification

- [X] T020 [US1] Check `test_cross_synthesis.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage and add to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` if found. Verify `Approx().margin()` used for floating-point comparisons.

- [X] T021 [US1] Build `innexus_tests` target, verify zero warnings, run tests and confirm all pass. Verify SC-001 (correlation > 0.95 at blend=1.0).

### 3.4 Commit

- [ ] T022 [US1] **Commit completed User Story 1 work**: pure harmonic reference, timbral blend pipeline step, smoothing, and tests.

**Checkpoint**: US1 complete. Timbral Blend sweeps smoothly between pure harmonics and source timbre. Tests verify SC-001.

---

## Phase 4: User Story 3 -- Evolution Engine: Autonomous Timbral Drift (Priority: P2)

**Goal**: Add an autonomous Evolution Engine that drifts the morph position through populated memory slots as waypoints, supporting Cycle, PingPong, and Random Walk modes.

**Independent Test**: Populate 2+ Memory Slots with distinct timbral snapshots, enable Evolution with known speed, play a sustained MIDI note, verify spectral centroid changes over time with standard deviation > 100 Hz across 10 seconds (SC-003).

**Requirements covered**: FR-014 through FR-023, FR-044, FR-046, SC-003, SC-007, SC-008, SC-009

### 4.1 Tests for Evolution Engine (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T023 [P] [US3] Write failing unit tests for `EvolutionEngine` class in `plugins/innexus/tests/unit/processor/test_evolution_engine.cpp`: test `prepare()` sets inverseSampleRate, test `updateWaypoints()` collects only occupied slots, test Cycle mode phase wraps at 1.0, test PingPong mode bounces direction at endpoints, test RandomWalk mode stays within [0, depth] range, test `getInterpolatedFrame()` returns false with <2 waypoints (edge case from spec), test phase is global and does not reset between notes (FR-020), test manual offset coexistence clamped to [0,1] (FR-021).

- [X] T024 [P] [US3] Write failing integration tests for Evolution Engine in processor in `plugins/innexus/tests/unit/processor/test_evolution_engine.cpp`: inject 2 memory slots with distinct spectra, enable evolution, advance processor N samples, verify output frame changes (non-zero spectral drift), verify Evolution + ManualMorph interaction (FR-021), verify blendEnabled=true skips evolution (FR-022, FR-052).

### 4.2 Implementation for User Story 3

- [X] T025 [US3] Implement `EvolutionEngine` class in `plugins/innexus/src/dsp/evolution_engine.h` matching the contract in `specs/120-creative-extensions/contracts/evolution_engine.h`: `prepare()`, `reset()`, `updateWaypoints()`, `setMode()`, `setSpeed()`, `setDepth()`, `setManualOffset()`, `advance()`, `getInterpolatedFrame()`, `getPosition()`, `getNumWaypoints()`. Use `lerpHarmonicFrame()` and `lerpResidualFrame()` for interpolation. Use `Xorshift32` for Random Walk. All methods `noexcept`, no heap allocations.

- [X] T026 [US3] Integrate `EvolutionEngine` into processor in `plugins/innexus/src/processor/processor.h` and `processor.cpp`: add `EvolutionEngine evolutionEngine_` member, call `evolutionEngine_.prepare(sampleRate)` in `setupProcessing()`, call `evolutionEngine_.updateWaypoints(memorySlots_)` when slots change (after capture/recall/state load). In frame selection pipeline: if `evolutionEnabled && !blendEnabled`, call `evolutionEngine_.advance()` each sample and `getInterpolatedFrame()` each frame to produce `currentFrame` (FR-022). Apply `clamp(evolutionPosition + manualOffset, 0.0, 1.0)` (FR-021). Apply smoothed speed and depth each frame (FR-023). Add `#include "dsp/evolution_engine.h"`. Depends on T003, T025.

### 4.3 Cross-Platform Verification

- [X] T027 [US3] Check `test_evolution_engine.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage and add to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` if found.

- [X] T028 [US3] Build `innexus_tests` target, verify zero warnings, run tests and confirm all pass. Verify evolution produces measurable spectral centroid variation (SC-003 criterion: std deviation > 100 Hz across 10s of samples).

### 4.4 Commit

- [X] T029 [US3] **Commit completed User Story 3 work**: `evolution_engine.h`, processor integration, tests.

**Checkpoint**: US3 complete. Evolution Engine autonomously drifts timbre through waypoints. Tests verify SC-003.

---

## Phase 5: User Story 4 -- Harmonic Modulators + Detune (Priority: P2)

**Goal**: Add two independent harmonic modulators with LFO-driven per-partial amplitude/frequency/pan animation, plus a Detune Spread parameter for chorus-like richness.

**Independent Test**: Load a static snapshot, enable Modulator 1 with Triangle at 2 Hz, depth 50% on partials 8-16. Verify partial amplitudes in that range oscillate at 2 Hz. Verify partials outside range are unaffected (SC-004). Enable Detune Spread and verify fundamental pitch deviation < 1 cent (SC-005).

**Requirements covered**: FR-024 through FR-033, FR-044, FR-046, FR-051, SC-004, SC-005, SC-007, SC-008, SC-009

### 5.1 Tests for Harmonic Modulators (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T030 [P] [US4] Write failing unit tests for `HarmonicModulator` class in `plugins/innexus/tests/unit/processor/test_harmonic_modulator.cpp`: test `prepare()` resets phase to 0.0 (FR-051), test `advance()` increments phase by rate*inverseSampleRate, test phase wraps at 1.0 and updates S&H value, test amplitude modulation formula at depth=0 (no change), depth=1.0 (full sweep), depth=0.5 (proportional - SC-004), test frequency modulation produces multiplier = pow(2, depth*lfo*50/1200), test pan offset formula, test range start > end edge case handled gracefully.

- [X] T031 [P] [US4] Write failing tests for all 5 LFO waveforms in `plugins/innexus/tests/unit/processor/test_harmonic_modulator.cpp`: Sine at phase=0 is 0, at phase=0.25 is 1, Triangle uses formula `4*|phase-0.5|-1` so at phase=0 is 1, at phase=0.5 is -1, at phase=1.0 is 1, Square at phase<0.5 is 1 at phase>=0.5 is -1, Saw at phase=0 is -1 at phase=1 is 1, RandomSH holds constant between wraps and changes on wrap.

- [X] T032 [P] [US4] Write failing tests for two-modulator overlap behavior in `plugins/innexus/tests/unit/processor/test_harmonic_modulator.cpp`: test overlapping amplitude ranges multiply effects (FR-028), test overlapping frequency ranges add effects, test partial outside range gets multiplier 1.0 / offset 0.0.

- [X] T033 [P] [US4] Write failing tests verifying LFO phase does not reset on note events in `plugins/innexus/tests/unit/processor/test_harmonic_modulator.cpp`: advance 1000 samples, record phase, simulate note-off/note-on, verify phase continues from recorded value (FR-029, FR-051).

### 5.2 Implementation for User Story 4

- [X] T034 [US4] Implement `HarmonicModulator` class in `plugins/innexus/src/dsp/harmonic_modulator.h` matching the contract in `specs/120-creative-extensions/contracts/harmonic_modulator.h`: `ModulatorWaveform` enum (Sine/Triangle/Square/Saw/RandomSH), `ModulatorTarget` enum (Amplitude/Frequency/Pan), all setter/getter methods, `advance()`, `applyAmplitudeModulation()`, `getFrequencyMultipliers()`, `getPanOffsets()`, `computeWaveform()`. Formula-based LFO (no wavetable, no heap) per R-002. S&H using `Xorshift32`. Phase initialized to 0.0 in `prepare()`. All `noexcept`.

- [X] T035 [US4] Integrate two `HarmonicModulator` instances into processor in `plugins/innexus/src/processor/processor.h` and `processor.cpp`: add `HarmonicModulator mod1_`, `mod2_` members, call `prepare()` in `setupProcessing()`, call `mod1_.advance()` / `mod2_.advance()` per sample. Per frame: if modulator enabled, apply amplitude/frequency/pan modulation after harmonic filter step in pipeline (FR-049 step 4). Two modulators on overlapping amplitude ranges multiply; overlapping frequency/pan ranges add (FR-028). Apply smoothed rate and depth. Add `#include "dsp/harmonic_modulator.h"`. Detune spread is already wired in T011 via `oscillatorBank_.setDetuneSpread()`, so verify the detune smoother is applied here. Depends on T003, T011, T034.

### 5.3 Cross-Platform Verification

- [X] T036 [US4] Check all modulator test files for `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` if found. Verify `Approx().margin()` used for waveform value comparisons.

- [X] T037 [US4] Build `innexus_tests` target, verify zero warnings, run all tests. Verify SC-004 (amplitude modulation depth within +/-5% of configured depth at 2 Hz, measured over 500ms), verify SC-005 (Detune Spread at 1.0 preserves fundamental pitch < 1 cent deviation).

### 5.4 Commit

- [X] T038 [US4] **Commit completed User Story 4 work**: `harmonic_modulator.h`, processor integration (two modulators), detune spread wired, tests.

**Checkpoint**: US4 complete. Two independent LFO modulators animate per-partial amplitude/frequency/pan. Detune spread adds chorus richness. Tests verify SC-004, SC-005.

---

## Phase 6: User Story 5 -- Multi-Source Blending (Priority: P3)

**Goal**: Add a HarmonicBlender that produces a normalized weighted sum across up to 8 memory slots plus one live source, overriding the normal recall/freeze path when enabled.

**Independent Test**: Populate Slots 1 and 3 with spectrally distinct timbres at equal weights. Verify the output spectral centroid is within +/-10% of the arithmetic mean of the two source centroids (SC-006). Verify single-source blend at weight=1.0 produces output identical to direct Memory Recall (SC-011).

**Requirements covered**: FR-034 through FR-042, FR-044, FR-048, FR-052, SC-006, SC-007, SC-008, SC-009, SC-011

### 6.1 Tests for Multi-Source Blending (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T039 [P] [US5] Write failing unit tests for `HarmonicBlender` class in `plugins/innexus/tests/unit/processor/test_harmonic_blender.cpp`: test equal-weight blend of 2 sources produces amplitude = 0.5*A + 0.5*B for each partial (FR-037), test weight normalization: three sources at [0.2, 0.3, 0.5] produce normalized sum (FR-035), test zero-weight source contributes nothing, test all-zero weights produces silence (FR-039), test partial count mismatch: source A with 24 partials contributes 0 for partials 25-48 (FR-038), test single-source at weight=1.0 produces output identical to source direct read (SC-011).

- [X] T040 [P] [US5] Write failing integration test for blend priority over evolution in `plugins/innexus/tests/unit/processor/test_harmonic_blender.cpp`: enable both blendEnabled and evolutionEnabled, verify output comes from blended model not evolution (FR-052, FR-049 pipeline order).

- [X] T041 [P] [US5] Write failing integration test for live source blending in `plugins/innexus/tests/unit/processor/test_harmonic_blender.cpp`: inject live analysis frame, set live weight=0.5 and slot weight=0.5, verify blended output is mean of live and slot data (FR-036, FR-037).

### 6.2 Implementation for User Story 5

- [X] T042 [US5] Implement `HarmonicBlender` class in `plugins/innexus/src/dsp/harmonic_blender.h` matching the contract in `specs/120-creative-extensions/contracts/harmonic_blender.h`: `setSlotWeight(int, float)`, `setLiveWeight(float)`, `blend(slots, liveFrame, liveResidual, hasLiveSource, frame, residual)`, `getEffectiveSlotWeight(int)`, `getEffectiveLiveWeight()`. Weight normalization per R-006: `effectiveWeight_i = weight_i / totalWeight`. Empty slots (`!slot.occupied`) contribute zero regardless of weight. All `noexcept`, no heap allocations.

- [X] T043 [US5] Integrate `HarmonicBlender` into processor in `plugins/innexus/src/processor/processor.h` and `processor.cpp`: add `HarmonicBlender harmonicBlender_` member. In the frame selection pipeline (FR-049): if `blendEnabled`, call `harmonicBlender_.blend(...)` to produce `currentFrame` and skip evolution. Apply smoothed slot weights and live weight each frame before calling `blend()`. Update blend weights in blender from smoothers each frame. Call `evolutionEngine_.updateWaypoints()` when blend disabled state changes (evolution needs current waypoints). Add `#include "dsp/harmonic_blender.h"`. Depends on T003, T025, T042.

### 6.3 Cross-Platform Verification

- [X] T044 [US5] Check all blender test files for `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` if found.

- [X] T045 [US5] Build `innexus_tests` target, verify zero warnings, run all tests. Verify SC-006 (blended centroid within +/-10% of mean of two source centroids), verify SC-011 (single-source blend == direct recall).

### 6.4 Commit

- [X] T046 [US5] **Commit completed User Story 5 work**: `harmonic_blender.h`, processor integration, blend priority logic, tests.

**Checkpoint**: US5 complete. Multi-source blending weights up to 8 stored snapshots + 1 live source. Tests verify SC-006, SC-011.

---

## Phase 7: State Persistence -- v6 State (All Stories)

**Purpose**: Extend `getState()`/`setState()` to version 6, persisting all 31 new parameters. Maintain backward compatibility with v5 state files.

**Requirements covered**: FR-043, FR-044, SC-009

### 7.1 Tests for State Persistence (Write FIRST -- Must FAIL)

- [X] T047 Write failing tests for v6 state round-trip in `plugins/innexus/tests/unit/vst/test_state_v6.cpp`: call `getState()`, then `setState()` on a new processor, verify all 31 M6 parameters match originals within 1e-6 tolerance (SC-009). Write test for loading v5 state: construct a minimal v5 binary blob (version=5 plus all M1-M5 fields), call `setState()`, verify all M6 parameters initialize to their spec defaults (timbralBlend=1.0, stereoSpread=0.0, all others=0.0), verify no crash or error (SC-009).

### 7.2 Implementation for State Persistence

- [X] T048 Extend `getState()` in `plugins/innexus/src/processor/processor.cpp`: after existing M5 fields, write version=6 marker and all 31 M6 normalized parameter values in the order defined in `data-model.md` state layout. Update version constant from 5 to 6.

- [X] T049 Extend `setState()` in `plugins/innexus/src/processor/processor.cpp`: detect version number; if version < 6, skip reading M6 block and initialize all M6 atomics to defaults (`timbralBlend_=1.0`, all others=0.0); if version >= 6, read all 31 M6 fields and store into atomics. After loading, call `evolutionEngine_.updateWaypoints(memorySlots_)` and update blender weights. Depends on T003, T026, T043.

### 7.3 Verify

- [X] T050 Build `innexus_tests` target, verify zero warnings, run `test_state_v6.cpp` tests. Verify SC-009 (round-trip tolerance 1e-6, v5 compatibility).

### 7.4 Commit

- [X] T051 **Commit state persistence work**: v6 state read/write, backward compatibility with v5, tests.

**Checkpoint**: State persistence complete. All 31 parameters survive save/load cycles. Old presets load cleanly.

---

## Phase 8: Static Analysis (MANDATORY)

**Purpose**: Clang-tidy quality gate on all modified and new source files before final verification.

- [X] T052 Regenerate `compile_commands.json` if not already current:
  ```bash
  cmake --preset windows-ninja
  ```
  from VS Developer PowerShell (needed if new source files were added).

- [X] T053 Run clang-tidy on all modified targets:
  ```powershell
  ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
  ./tools/run-clang-tidy.ps1 -Target innexus -BuildDir build/windows-ninja
  ```

- [X] T054 Fix all clang-tidy errors (blocking). Review warnings and fix where appropriate. Add `// NOLINT(...)` with reason for any intentionally suppressed DSP-specific warnings.

**Checkpoint**: Static analysis clean -- no errors, documented suppressions for any warnings.

---

## Phase 9: Integration Verification (All Stories)

**Purpose**: End-to-end pipeline integration test covering all features active simultaneously and full pluginval validation.

- [X] T055 Write full pipeline integration test in `plugins/innexus/tests/unit/processor/test_m6_pipeline_integration.cpp`: (a) enable all M6 features simultaneously (timbralBlend=0.5, stereoSpread=0.5, detuneSpread=0.5, evolutionEnabled=true, mod1Enabled=true, mod2Enabled=true), (b) advance 44100 samples and verify output is non-silent and non-NaN, (c) verify pipeline order: blend/evolution -> filter -> modulators -> oscillator (FR-049), (d) verify blendEnabled overrides evolutionEnabled (FR-052), (e) verify click-free parameter sweeps (SC-007): sweep each parameter from 0 to 1 at maximum rate and verify no sample-level discontinuities above -80 dBFS.

- [ ] T056 Run full pluginval validation (skipped -- will be run in Phase 11):
  ```bash
  tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"
  ```
  Verify all 5 strictness levels pass with all 31 new parameters registered.

- [X] T057 Run CPU benchmark to verify SC-008 using the following methodology: (1) Build Release target. (2) In `test_m6_pipeline_integration.cpp`, add a `[.perf]`-tagged benchmark that processes 44100 * 10 samples (10 seconds of audio) at 44.1 kHz with a 512-sample buffer, single voice, all M6 features active (stereo spread=0.5, detune=0.5, 2 modulators enabled, evolution enabled). (3) Measure wall-clock time of the processing loop using `std::chrono::high_resolution_clock`. Run 100 iterations, take the median. (4) Compute CPU% as `(median_time_per_10s / 10.0) * 100`. (5) Establish M5 baseline by running same benchmark with all M6 features disabled (spread=0, detune=0, mods disabled, evolution disabled). (6) Verify M6_active - M5_baseline < 1.0% and M6_active < 2.0% total. Record actual measured values in the compliance table.

- [X] T058 Commit integration verification test and any fixes.

---

## Phase 10: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation per Constitution Principle XIII.

- [X] T059 Update `specs/_architecture_/layer-2-processors.md` (or equivalent): add entries for `HarmonicOscillatorBank` stereo/detune extension (`processStereo()`, `setStereoSpread()`, `setDetuneSpread()`), include: purpose, new public API summary, file location `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h`, when to use.

- [X] T060 Update `specs/_architecture_/innexus-plugin.md` (or equivalent): add entries for three new plugin-local DSP classes: `EvolutionEngine` (`plugins/innexus/src/dsp/evolution_engine.h`), `HarmonicModulator` (`plugins/innexus/src/dsp/harmonic_modulator.h`), `HarmonicBlender` (`plugins/innexus/src/dsp/harmonic_blender.h`). Include purpose, public API summary, pipeline position.

- [X] T061 **Commit architecture documentation updates**.

**Checkpoint**: Architecture docs reflect all M6 functionality. Docs accurate for future milestone developers.

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honest assessment of all requirements before claiming completion (Constitution Principle XV).

### 11.1 Requirements Verification

- [ ] T062 **Review ALL FR-001 through FR-052** from `specs/120-creative-extensions/spec.md` against implementation. For each FR: open the implementation file, find the relevant code, confirm the requirement is met. Cite file and line number.

- [ ] T063 **Review ALL SC-001 through SC-011** by running specific tests or measurements. Copy actual output values and compare against spec thresholds:
  - SC-001: correlation > 0.95 at blend=1.0
  - SC-002: decorrelation > 0.8 at spread=1.0; < 0.01 at spread=0.0
  - SC-003: centroid std deviation > 100 Hz over 10s
  - SC-004: modulation depth within +/-5% of configured depth
  - SC-005: fundamental deviation < 1 cent at detune=1.0
  - SC-006: blended centroid within +/-10% of mean
  - SC-007: no discontinuities > -80 dBFS during parameter sweeps
  - SC-008: < 1.0% additional CPU for all M6 features combined
  - SC-009: round-trip tolerance 1e-6; v5 state loads with M6 defaults
  - SC-010: left == right at spread=0.0
  - SC-011: single-source blend == direct recall

- [ ] T064 **Search for cheating patterns** in all new code:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [ ] T065 **Update `specs/120-creative-extensions/spec.md` "Implementation Verification" section** with compliance status (file path, line number, test name, actual measured value) for every FR-xxx and SC-xxx row. Mark overall status COMPLETE / NOT COMPLETE / PARTIAL.

### 11.3 Final Honest Self-Check

- [ ] T066 Answer these questions. If ANY is "yes", do NOT claim completion:
  1. Did I change ANY test threshold from what the spec originally required?
  2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
  3. Did I remove ANY features from scope without telling the user?
  4. Would the spec author consider this "done"?
  5. If I were the user, would I feel cheated?

**Checkpoint**: Honest assessment complete. Evidence-based compliance table filled.

---

## Phase 12: Final Completion

### 12.1 Final Commit

- [ ] T067 **Verify all tests pass** (`dsp_tests` and `innexus_tests` targets).
- [ ] T068 **Verify the feature branch is up to date** and all work committed: `git status` shows clean.

### 12.2 Completion Claim

- [ ] T069 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user). Link to evidence in spec.md compliance table.

**Checkpoint**: Spec implementation honestly complete.

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup/Params) -- No dependencies. BLOCKS all user stories.
  |
  +-- Phase 2 (US2: Stereo) -- Depends on Phase 1. BLOCKS processor output changes.
  |     |
  |     +-- Phase 3 (US1: Cross-Synthesis) -- Depends on Phase 1, Phase 2.
  |     |
  |     +-- Phase 4 (US3: Evolution) -- Depends on Phase 1, Phase 2.
  |     |
  |     +-- Phase 5 (US4: Modulators) -- Depends on Phase 1, Phase 2.
  |     |
  |     +-- Phase 6 (US5: Blending) -- Depends on Phase 1, Phase 2, Phase 4 (evolution priority logic).
  |
  +-- Phase 7 (State Persistence) -- Depends on Phases 1-6 (all parameters must exist).
        |
        +-- Phase 8 (Static Analysis) -- Depends on Phases 1-7.
              |
              +-- Phase 9 (Integration Verification) -- Depends on Phases 1-8.
                    |
                    +-- Phase 10 (Architecture Docs) -- Depends on Phase 9.
                          |
                          +-- Phase 11 (Completion Verification) -- Depends on Phase 10.
                                |
                                +-- Phase 12 (Final Completion) -- Depends on Phase 11.
```

### User Story Dependencies

- **US2 (Stereo Spread) -- Phase 2**: Must be first user story. Changing `oscillatorBank_.process()` to `processStereo()` affects all other features' output path.
- **US1 (Cross-Synthesis) -- Phase 3**: Depends on Phase 1 (timbralBlend atomic + smoother) and Phase 2 (stereo output path in place). Independent of US3/US4/US5.
- **US3 (Evolution) -- Phase 4**: Depends on Phase 1 (evolution atomics) and Phase 2 (stereo output). Independent of US1/US4/US5.
- **US4 (Modulators) -- Phase 5**: Depends on Phase 1 (modulator atomics) and Phase 2 (stereo output, since pan modulation interacts with spread). Independent of US1/US3/US5.
- **US5 (Multi-Source Blending) -- Phase 6**: Depends on Phase 1 (blend atomics) and Phase 4 (evolution must exist for FR-052 priority logic). Can start after Phase 4 is committed.

### Parallel Opportunities Within Each Story

Within Phase 2 (US2): T005 and T006 (test files) can run in parallel.
Within Phase 3 (US1): T016 and T017 (tests) can run in parallel.
Within Phase 4 (US3): T023 and T024 (tests) can run in parallel.
Within Phase 5 (US4): T030, T031, T032, T033 (test cases) can run in parallel.
Within Phase 6 (US5): T039, T040, T041 (test cases) can run in parallel.

---

## Implementation Strategy

### MVP First (User Stories 1 + 2 Only)

1. Complete Phase 1: Parameter Registration
2. Complete Phase 2: US2 (Stereo Spread) -- foundational output change
3. Complete Phase 3: US1 (Cross-Synthesis) -- flagship use case
4. **STOP and VALIDATE**: Both US1 and US2 independently functional
5. Add Phase 7 (state persistence for the 2 params only) if needed for demo

### Incremental Delivery

1. Phase 1 + Phase 2 (US2) → Stereo output working
2. Phase 3 (US1) → Cross-synthesis blend working
3. Phase 4 (US3) → Evolution Engine working
4. Phase 5 (US4) → Harmonic Modulators + Detune working
5. Phase 6 (US5) → Multi-Source Blending working
6. Phase 7 → State persistence for all parameters
7. Phases 8-12 → Quality gate, docs, completion

Each phase produces a committable, buildable, testable increment with zero regressions.

---

## Traceability Matrix

| Requirement | Task(s) |
|-------------|---------|
| FR-001 (Timbral Blend parameter) | T001, T016, T019 |
| FR-002 (Blend computation: lerp relFreq, amp, deviation) | T016, T019 |
| FR-003 (Source switching with crossfade) | T016, T019 (reuses existing crossfade mechanism) |
| FR-004 (Pure harmonic reference: 1/n, L2-norm) | T017, T018 |
| FR-005 (Timbral Blend OnePoleSmoother) | T003, T019 |
| FR-006 (Stereo Spread parameter) | T001, T005, T008 |
| FR-007 (Stereo output: processStereo) | T005, T010 |
| FR-008 (Odd/even pan positions, constant power) | T005, T008 |
| FR-009 (Fundamental partial reduced spread 25%) | T005, T008 |
| FR-010 (Pan recalculated per frame, not per sample) | T008, T011 |
| FR-011 (Stereo Spread OnePoleSmoother, ~10ms) | T003, T011 |
| FR-012 (Residual mono-center in stereo out) | T012, T011 |
| FR-013 (Mono output bus: sum to mono) | T011 |
| FR-014 (Evolution Enable toggle) | T001, T026 |
| FR-015 (Evolution Speed 0.01-10.0 Hz) | T001, T025, T026 |
| FR-016 (Evolution Depth 0.0-1.0) | T001, T025, T026 |
| FR-017 (Evolution Mode: Cycle/PingPong/RandomWalk) | T001, T002, T023, T025 |
| FR-018 (Waypoints from occupied memory slots) | T023, T025 |
| FR-019 (lerpHarmonicFrame / lerpResidualFrame for evolution) | T023, T025 |
| FR-020 (Evolution phase is global, not per-note) | T023, T025 |
| FR-021 (evolutionPosition + manualMorphOffset, clamped) | T023, T026 |
| FR-022 (Evolution overrides freeze/morph path) | T024, T026 |
| FR-023 (Speed and Depth parameter smoothed) | T003, T026 |
| FR-024 (2 independent modulators with all params) | T001, T002, T030, T034 |
| FR-025 (Amplitude modulation: multiplicative unipolar) | T030, T034 |
| FR-026 (Frequency modulation: additive cents bipolar) | T030, T034 |
| FR-027 (Pan modulation: offset bipolar, clamped) | T030, T034 |
| FR-028 (Two modulators overlap: multiply amp, add freq/pan) | T032, T035 |
| FR-029 (LFO free-running, not note-synced) | T033, T034 |
| FR-030 (Detune Spread parameter) | T001, T006, T009 |
| FR-031 (Detune deterministic, per frame not per sample) | T006, T009 |
| FR-032 (Detune additive with inharmonic deviation) | T006, T009 |
| FR-033 (Modulator parameter changes smoothed) | T003, T035 |
| FR-034 (Multi-Source Blend Enable toggle) | T001, T043 |
| FR-035 (Per-slot weights 0-1, normalized internally) | T001, T039, T042 |
| FR-036 (Live Source Weight parameter) | T001, T041, T042 |
| FR-037 (Weighted sum: amp, relFreq, residualBands) | T039, T042 |
| FR-038 (Missing partials contribute zero amplitude) | T039, T042 |
| FR-039 (All-zero weights -> silence) | T039, T042 |
| FR-040 (Blend overrides normal recall/freeze path) | T040, T043 |
| FR-041 (Weight changes parameter-smoothed) | T003, T043 |
| FR-042 (Max 1 live analysis source) | T042 (blender accepts single liveFrame input) |
| FR-043 (Parameter naming convention) | T001 (k{Feature}{Parameter}Id pattern) |
| FR-044 (State save/load with v5 backward compat) | T047, T048, T049 |
| FR-045 (Real-time safety: no alloc, no locks, no I/O) | T010, T025, T034, T042 (all noexcept, fixed arrays) |
| FR-046 (Stereo/detune in KrateDSP Layer 2; Evolution/Mod/Blender plugin-local) | T007-T010, T025, T034, T042 |
| FR-047 (Timbral Blend in processor pipeline) | T019 |
| FR-048 (HarmonicBlender class in plugins/innexus/src/dsp/) | T042 |
| FR-049 (Pipeline order: blend/crosssynth -> evolution -> filter -> modulators -> oscillator) | T055 |
| FR-050 (processStereo API alongside existing process()) | T010 |
| FR-051 (Modulator LFO phase init to 0 in prepare(), no MIDI reset) | T030, T033, T034 |
| FR-052 (Blend active -> evolution ignored) | T024, T040, T043 |
| SC-001 (Blend=1.0 correlation > 0.95) | T016, T021 |
| SC-002 (Spread=1.0 decorrelation > 0.8; Spread=0 < 0.01) | T005, T014 |
| SC-003 (Evolution centroid std dev > 100 Hz over 10s) | T024, T028 |
| SC-004 (Modulator depth within +/-5% of configured) | T030, T037 |
| SC-005 (Detune fundamental < 1 cent deviation) | T006, T037 |
| SC-006 (Blend centroid within +/-10% of mean) | T039, T045 |
| SC-007 (Click-free parameter sweeps, < -80 dBFS) | T016, T055 |
| SC-008 (< 1.0% additional CPU for all M6 features) | T057 |
| SC-009 (State round-trip 1e-6 tolerance; v5 loads with defaults) | T047, T050 |
| SC-010 (Stereo bit-identical to mono at spread=0) | T005, T014 |
| SC-011 (Single-source blend == direct recall) | T039, T045 |

---

## Notes

- [P] tasks use different files with no dependencies on incomplete tasks -- can be parallelized
- Story labels map to: US1=Cross-Synthesis, US2=Stereo Spread, US3=Evolution, US4=Modulators, US5=Multi-Source Blend
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance after each user story (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest, evidence-based assessment
- Implementation order follows quickstart.md: Stereo (US2) first because it changes the output path that all other features depend on. Cross-synthesis (US1) second as the flagship use case. Evolution (US3) third as it requires memory slot infrastructure. Modulators (US4) fourth. Blending (US5) last as it is most complex and references evolution's priority logic.
- All new DSP classes use fixed-size `std::array` (no `std::vector`), `noexcept` on all audio-thread methods, and no heap allocations after `prepare()`. This satisfies FR-045 and Constitution Principle II.
- The `detuneSpread` parameter (ID 630) is wired in Phase 2 (US2/T011) since it lives in `HarmonicOscillatorBank`. Phase 5 (US4) verifies the modulator-detune interaction only.
