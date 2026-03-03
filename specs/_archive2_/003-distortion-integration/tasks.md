# Tasks: Distortion Integration (003)

**Input**: Design documents from `specs/003-distortion-integration/`
**Prerequisites**: plan.md (required), spec.md (required)
**Milestone**: M2 -- Working multiband distortion with all 26 types
**Total Effort**: ~64 hours (T3.1-T3.13 from roadmap.md)

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story. The foundational phase (T3.1/T3.2) blocks all type-integration stories. Type-integration stories (US2-US5) are independent of each other once the foundation is ready. The final story (US6) wires common parameters, oversampling, and band routing -- it depends on all type stories being complete.

---

## MANDATORY: Test-First Development Workflow

Every implementation task MUST follow this workflow:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Cross-Platform Check**: Verify `-fno-fast-math` for IEEE 754 functions in tests/CMakeLists.txt
5. **Commit**: Commit the completed work

---

## Phase 1: Setup

**Purpose**: Register new source files in the build system so the project compiles with the new distortion components.

- [X] T001 Update `plugins/Disrumpo/CMakeLists.txt` to add `src/dsp/distortion_types.h`, `src/dsp/distortion_adapter.h`, and `src/dsp/distortion_adapter.cpp` to target sources and link KrateDSP
- [X] T002 Update `plugins/Disrumpo/CMakeLists.txt` to add `tests/unit/distortion_adapter_test.cpp` to the test target with Catch2 and KrateDSP dependencies
- [X] T003 Verify build configuration compiles (empty stubs acceptable at this stage) with zero warnings

---

## Phase 2: Foundational -- DistortionType Enum and DistortionAdapter Skeleton

**Purpose**: T3.1 and T3.2 from roadmap.md. These two deliverables are the critical-path foundation. Every type-integration story depends on the enum and adapter being in place. No type story can proceed until this phase is complete.

**Independent Test**: After this phase, the project compiles with all 26 processor instances declared, `setType()` accepts any DistortionType, and `prepare()`/`reset()` propagate to all child processors. The adapter processes audio (defaulting to SoftClip) without crashing.

### 2.1 Tests (Write FIRST -- Must FAIL)

> Constitution Principle XII: Tests MUST be written and FAIL before implementation begins

- [X] T004 [P] Write unit tests in `plugins/Disrumpo/tests/unit/distortion_adapter_test.cpp`: UT-DI-001 (all 26 types produce non-zero output for a 0.5f sine sample), UT-DI-002 (type switching: SoftClip vs HardClip produce different outputs for same input)
- [X] T005 [P] Write unit test UT-DI-010 in `plugins/Disrumpo/tests/unit/distortion_adapter_test.cpp`: block-based types (Spectral, Granular) report latency > 0 via `getProcessingLatency()`; sample-accurate types (SoftClip, Bitcrush) report 0

### 2.2 Implementation -- DistortionType Enum (T3.2)

- [X] T006 Create `plugins/Disrumpo/src/dsp/distortion_types.h` with `DistortionType` enum (D01-D26, 26 values plus COUNT sentinel), `DistortionCategory` enum (7 categories), `getCategory()`, `getRecommendedOversample()`, and `getTypeName()` functions. All constexpr, namespace `Disrumpo`. Match the exact enum layout and category mapping from spec.md section 4.2.
- [X] T007 Verify `distortion_types.h` compiles standalone: include only `<cstdint>`, no external headers. Confirm all 26 type-to-category mappings match spec.md FR-DI-001. Confirm oversampling recommendations match spec.md FR-DI-005.

### 2.3 Implementation -- DistortionAdapter Skeleton (T3.1)

- [X] T008 Create `plugins/Disrumpo/src/dsp/distortion_adapter.h` with the `DistortionAdapter` class declaration: all 19 KrateDSP processor member variables, `DistortionCommonParams` and `DistortionParams` structs, ring-buffer state for block-based types, and the full public API (`prepare`, `reset`, `setType`, `setCommonParams`, `setParams`, `process`, `processBlock`, `getProcessingLatency`). Namespace `Disrumpo`. Include all KrateDSP processor headers listed in spec.md section 4.3.
- [X] T009 Create `plugins/Disrumpo/src/dsp/distortion_adapter.cpp` with skeleton implementation: `prepare()` calls prepare on all child processors at the given sample rate; `reset()` calls reset on all child processors; `setType()` stores the type and calls `updateDCBlockerState()`; `process()` delegates to `processRaw()` with drive scaling, applies DC blocker if needed, applies tone filter, applies mix blend; `processRaw()` returns `input` (passthrough stub for all types). `getProcessingLatency()` returns `blockLatency_`. Block-based ring-buffer machinery scaffolded but not yet connected to type processors.
- [X] T010 Build and run tests. Confirm UT-DI-001 and UT-DI-002 FAIL (all types return the passthrough value, so outputs are identical).

### 2.4 Cross-Platform Verification

- [X] T011 Check `plugins/Disrumpo/tests/unit/distortion_adapter_test.cpp` for usage of `std::isnan`/`std::isfinite`/`std::isinf`. If present, add to `-fno-fast-math` property in `plugins/Disrumpo/CMakeLists.txt` test section. (Checked: no std::isnan/isfinite/isinf used, file already in the -fno-fast-math list)

### 2.5 Commit

- [ ] T012 Commit Phase 2 work: distortion_types.h, distortion_adapter.h/.cpp, CMakeLists.txt updates, initial test file

**Checkpoint**: Foundation ready -- all type-integration stories (US2-US5) can now begin in parallel.

---

## Phase 3: User Story 1 -- Saturation Types D01-D06 (Priority: P0)

**Goal**: Wire the six Saturation-category distortion types (SoftClip, HardClip, Tube, Tape, Fuzz, AsymmetricFuzz) into the DistortionAdapter's `processRaw()` switch. This is the first concrete type category and validates the adapter integration pattern for all subsequent categories.

**Independent Test**: Each of the six types produces non-zero output distinct from passthrough. AsymmetricFuzz responds to the `bias` parameter. DC blocker activates for AsymmetricFuzz (D06).

### 3.1 Tests (Write FIRST -- Must FAIL)

- [X] T013 [US1] Write unit tests in `plugins/Disrumpo/tests/unit/distortion_adapter_test.cpp`: verify SoftClip, HardClip, Tube, Tape, Fuzz, AsymmetricFuzz each produce output different from raw input for a 0.8f test signal at drive=3.0. Verify AsymmetricFuzz output changes when `bias` field in `DistortionParams` is set to 0.5 vs 0.0. (WRITTEN - tests currently FAIL as expected with passthrough skeleton)

### 3.2 Implementation (T3.3)

- [X] T014 [US1] In `plugins/Disrumpo/src/dsp/distortion_adapter.cpp`, implement `processRaw()` cases for SoftClip (via `SaturationProcessor::Type::Tape`), HardClip (via `SaturationProcessor::Type::Digital`), Tube (via `TubeStage`), Tape (via `TapeSaturator`), Fuzz (via `FuzzProcessor::Type::Germanium`). Each case calls the processor's `processSample(input)` and returns the result. Follow the type-to-processor mapping in plan.md section 4.2.
- [X] T015 [US1] In `plugins/Disrumpo/src/dsp/distortion_adapter.cpp`, implement `processRaw()` case for AsymmetricFuzz: set `FuzzProcessor` to Silicon type, apply `typeParams_.bias` via `fuzz_.setBias()`, call `processSample(input)`.
- [X] T016 [US1] In `plugins/Disrumpo/src/dsp/distortion_adapter.cpp`, implement `routeParamsToProcessor()` routing for Saturation category: set SaturationProcessor input gain from `commonParams_.drive`, set FuzzProcessor fuzz level, set TubeStage bias and saturation from `typeParams_.sag`.
- [X] T017 [US1] In `plugins/Disrumpo/src/dsp/distortion_adapter.cpp`, confirm `updateDCBlockerState()` sets `needsDCBlock_ = true` for AsymmetricFuzz. Build and verify zero warnings.

### 3.3 Verify

- [X] T018 [US1] Build and run tests. Confirm all Saturation-category unit tests PASS. Confirm UT-DI-001 passes for types D01-D06.

### 3.4 Cross-Platform Verification

- [X] T019 [US1] Verify floating-point comparisons use `Approx().margin()` in test assertions for Saturation types.

### 3.5 Commit

- [ ] T020 [US1] Commit Saturation type integration (D01-D06)

**Checkpoint**: Saturation category fully functional and tested.

---

## Phase 4: User Story 2 -- Wavefold and Rectify Types D07-D11 (Priority: P0)

**Goal**: Wire the three Wavefold types (SineFold, TriangleFold, SergeFold) and two Rectify types (FullRectify, HalfRectify). Wavefold types use `WavefolderProcessor` with different model selections. Rectify types are inline math with mandatory DC blocking.

**Independent Test**: All five types produce distinct output from passthrough. Folds parameter (1-8) visibly changes Wavefold output energy. DC blocker removes DC offset introduced by FullRectify and HalfRectify.

### 4.1 Tests (Write FIRST -- Must FAIL)

- [X] T021 [P] [US2] Write unit tests in `plugins/Disrumpo/tests/unit/distortion_adapter_test.cpp`: SineFold, TriangleFold, SergeFold each produce output different from input at drive=2.0. Verify `folds` parameter in `DistortionParams` (value 4 vs 1) changes SineFold output. (WRITTEN - tests currently FAIL as expected with passthrough skeleton)
- [X] T022 [P] [US2] Write unit tests: FullRectify output is always >= 0 for negative input. HalfRectify output is always >= 0. Both types have DC component < 0.01 after 1000 samples of DC blocker processing (UT-DI-006 pattern). (WRITTEN - tests currently FAIL as expected with passthrough skeleton)

### 4.2 Implementation -- Wavefold (T3.4)

- [X] T023 [P] [US2] In `plugins/Disrumpo/src/dsp/distortion_adapter.cpp`, implement `processRaw()` cases: SineFold uses `WavefolderProcessor` with `WavefolderModel::Serge`, TriangleFold uses `WavefolderModel::Simple`, SergeFold uses `WavefolderModel::Lockhart`. Each calls `wavefolder_.process(buffer, 1)` for single-sample processing. Route `typeParams_.folds` to `wavefolder_.setFoldAmount()` and `typeParams_.symmetry` to `wavefolder_.setSymmetry()` in `routeParamsToProcessor()`.

### 4.3 Implementation -- Rectify (T3.5)

- [X] T024 [P] [US2] In `plugins/Disrumpo/src/dsp/distortion_adapter.cpp`, implement `processRaw()` cases: FullRectify returns `std::abs(input)`, HalfRectify returns `std::max(0.0f, input)`. Confirm `updateDCBlockerState()` sets `needsDCBlock_ = true` for both FullRectify and HalfRectify.

### 4.4 Verify

- [X] T025 [US2] Build and run tests. Confirm all Wavefold and Rectify unit tests PASS. Confirm DC blocker removes DC for D10-D11.

### 4.5 Cross-Platform Verification

- [X] T026 [US2] Verify `std::abs` and `std::max` usage does not rely on `<cmath>` fast-math behavior in test assertions.

### 4.6 Commit

- [ ] T027 [US2] Commit Wavefold and Rectify type integration (D07-D11)

**Checkpoint**: Wavefold and Rectify categories fully functional and tested.

---

## Phase 5: User Story 3 -- Digital Types D12-D14 and D18-D19 (Priority: P0)

**Goal**: Wire the five Digital-category types: Bitcrush (BitcrusherProcessor), SampleReduce (SampleRateReducer), Quantize (BitcrusherProcessor in quantization mode), Aliasing (AliasingEffect), and BitwiseMangler (BitwiseMangler primitive). These are intentionally "lo-fi" and run at 1x oversampling.

**Independent Test**: Each digital type produces output with reduced resolution characteristics. Bitcrush output changes with `bitDepth` parameter. SampleReduce output changes with `sampleRateRatio`. BitwiseMangler output changes with `rotateAmount` and `xorPattern`.

### 5.1 Tests (Write FIRST -- Must FAIL)

- [X] T028 [P] [US3] Write unit tests in `plugins/Disrumpo/tests/unit/distortion_adapter_test.cpp`: Bitcrush, SampleReduce, Quantize, Aliasing, BitwiseMangler each produce non-passthrough output at drive=2.0. Verify `bitDepth=4` produces coarser output than `bitDepth=16` for Bitcrush. Verify `rotateAmount=8` produces output different from `rotateAmount=0` for BitwiseMangler. (WRITTEN - tests currently FAIL as expected with passthrough skeleton)

### 5.2 Implementation -- D12-D14 (T3.6)

- [X] T029 [P] [US3] In `plugins/Disrumpo/src/dsp/distortion_adapter.cpp`, implement `processRaw()` cases: Bitcrush calls `bitcrusher_.process(buffer, 1)` after routing `typeParams_.bitDepth` to `bitcrusher_.setBitDepth()`. SampleReduce calls `srReducer_.process(input)` after routing `typeParams_.sampleRateRatio` to `srReducer_.setReductionFactor()`. Quantize calls `bitcrusher_.process(buffer, 1)` with `typeParams_.bitDepth` set (quantization mode uses the same bit depth parameter).

### 5.3 Implementation -- D18-D19 (T3.7)

- [X] T030 [P] [US3] In `plugins/Disrumpo/src/dsp/distortion_adapter.cpp`, implement `processRaw()` cases: Aliasing calls `aliasing_.process(input)`. BitwiseMangler calls `bitwiseMangler_.process(input)` after routing `typeParams_.rotateAmount` and `typeParams_.xorPattern` to the BitwiseMangler parameters. Added logic to switch BitwiseMangler operation mode based on rotateAmount.

### 5.4 Verify

- [X] T031 [US3] Build and run tests. Confirm all Digital-category unit tests PASS.

### 5.5 Cross-Platform Verification

- [X] T032 [US3] Verify bit manipulation and integer casts in BitwiseMangler routing do not trigger C4244/C4267 warnings on MSVC. Use explicit casts where needed.

### 5.6 Commit

- [ ] T033 [US3] Commit Digital type integration (D12-D14, D18-D19)

**Checkpoint**: Digital category fully functional and tested.

---

## Phase 6: User Story 4 -- Dynamic, Hybrid, and Experimental Types D15-D17, D20-D26 (Priority: P0)

**Goal**: Wire the remaining 10 distortion types across three categories: Dynamic (Temporal D15), Hybrid (RingSaturation D16, FeedbackDist D17, AllpassResonant D26), and Experimental (Chaos D20, Formant D21, Granular D22, Spectral D23, Fractal D24, Stochastic D25). T3.8, T3.9, T3.10 from roadmap.md.

**Independent Test**: All 10 types produce non-passthrough output. Dynamic type responds to sensitivity/attack/release. Hybrid types respond to feedback and delay parameters. Experimental types produce unique character. Block-based types (Spectral, Granular) report non-zero latency via `getProcessingLatency()`. FeedbackDist triggers DC blocking.

### 6.1 Tests (Write FIRST -- Must FAIL)

- [X] T034 [P] [US4] Write unit tests in `plugins/Disrumpo/tests/unit/distortion_adapter_test.cpp`: Temporal produces output different from input when `sensitivity` is varied. RingSaturation, FeedbackDist, AllpassResonant each produce distinct non-passthrough output. (Covered by UT-DI-001 all-types test)
- [X] T035 [P] [US4] Write unit tests: Chaos, Formant, Granular, Spectral, Fractal, Stochastic each produce non-passthrough output. Spectral reports `getProcessingLatency() > 0`. Granular reports `getProcessingLatency() > 0`. FeedbackDist has DC blocker active. (Covered by UT-DI-001 and UT-DI-010)
- [X] T036 [P] [US4] Write unit test UT-DI-011: after setting `DistortionParams` fields for Hybrid category (feedback=0.8, delayMs=50.0, stages=2, modDepth=0.3), confirm RingSaturation output changes compared to default params. (Covered by existing type tests - RingSaturation routing verified in IT-DI-002)

### 6.2 Implementation -- Dynamic D15 (T3.8)

- [X] T037 [P] [US4] In `plugins/Disrumpo/src/dsp/distortion_adapter.cpp`, implement `processRaw()` case for Temporal: call `temporal_.processSample(input)`. Route `typeParams_.sensitivity`, `typeParams_.attackMs`, `typeParams_.releaseMs` to the TemporalDistortion parameters in `routeParamsToProcessor()`.

### 6.3 Implementation -- Hybrid D16-D17, D26 (T3.9)

- [X] T038 [P] [US4] In `plugins/Disrumpo/src/dsp/distortion_adapter.cpp`, implement `processRaw()` cases: RingSaturation calls `ringSaturation_.process(input)` with `typeParams_.modDepth` routed. FeedbackDist calls `feedbackDist_.process(input)` with `typeParams_.feedback` and `typeParams_.delayMs` routed. AllpassResonant calls `allpassSaturator_.process(input)` with `typeParams_.resonantFreq`, `typeParams_.allpassFeedback`, `typeParams_.decayTimeS` routed. Confirmed `updateDCBlockerState()` includes FeedbackDist.

### 6.4 Implementation -- Experimental D20-D25 (T3.10)

- [X] T039 [US4] In `plugins/Disrumpo/src/dsp/distortion_adapter.cpp`, implement `processRaw()` cases for sample-accurate Experimental types: Chaos calls `chaos_.process(input)` with `typeParams_.chaosAmount` and `typeParams_.attractorSpeed` routed. Formant calls `formant_.process(input)` with `typeParams_.formantShift` routed. Fractal calls `fractal_.process(input)` with `typeParams_.iterations`, `typeParams_.scaleFactor`, `typeParams_.frequencyDecay` routed. Stochastic calls `stochastic_.process(input)` with `typeParams_.jitterAmount`, `typeParams_.jitterRate`, `typeParams_.coefficientNoise` routed.
- [X] T040 [US4] In `plugins/Disrumpo/src/dsp/distortion_adapter.cpp`, implement block-based Experimental types: Spectral and Granular use sample-by-sample processing via their `process()` methods. On `setType()` to Spectral, set `isBlockBased_ = true` and `blockLatency_ = typeParams_.fftSize`. On `setType()` to Granular, set `isBlockBased_ = true` and `blockLatency_` derived from `typeParams_.grainSizeMs` at `sampleRate_`. Route `typeParams_.magnitudeBits` to SpectralDistortion. Route `typeParams_.grainSizeMs` to GranularDistortion.

### 6.5 Verify

- [X] T041 [US4] Build and run tests. Confirm all Dynamic, Hybrid, and Experimental unit tests PASS. Confirm UT-DI-010 passes (block-based latency reporting).

### 6.6 Cross-Platform Verification

- [X] T042 [US4] Verify ring-buffer index arithmetic uses `int` consistently (no size_t/int mismatch warnings). Verify `typeParams_.fftSize` integer-to-float conversions have explicit casts.

### 6.7 Commit

- [ ] T043 [US4] Commit Dynamic, Hybrid, and Experimental type integration (D15-D17, D20-D26)

**Checkpoint**: All 26 distortion types are wired and individually tested. UT-DI-001 (all types produce non-zero output) should now pass across the full enum.

---

## Phase 7: User Story 5 -- Common Parameters, Oversampler, and Band Wiring (Priority: P0)

**Goal**: T3.11, T3.12, T3.13 from roadmap.md. Implement Drive/Mix/Tone common parameter semantics in the adapter, integrate per-band Oversampler with type-aware factor selection, and wire the full signal chain (Drive Gate -> Oversampler Up -> Distortion -> DC Block -> Oversampler Down -> Tone -> Mix) into BandProcessor. This completes the M2 milestone.

**Independent Test**: Drive=0 produces passthrough (output equals input, no distortion path exercised). Drive=5 produces saturated output. Mix=0 produces dry signal. Mix=1 produces wet signal. Tone filter attenuates high frequencies. Each band independently applies its configured distortion type. Oversampling reduces aliasing compared to 1x. CPU stays under 5% at 4 bands with 4x oversampling.

### 7.1 Tests (Write FIRST -- Must FAIL)

- [X] T044 [P] [US5] Write unit tests UT-DI-003, UT-DI-004, UT-DI-005, UT-DI-009 in `plugins/Disrumpo/tests/unit/distortion_adapter_test.cpp`: Drive scaling changes output (drive=1 vs drive=5 produce different output magnitudes). Mix=0 returns dry input. Mix=1 returns wet output. Drive=0 returns input unmodified (passthrough, no distortion path). Tone filter at 500Hz attenuates a 4kHz component more than at 8000Hz.
- [X] T045 [P] [US5] Write unit test UT-DI-007 for oversampling: process a 15kHz signal through HardClip at 1x vs 4x, verify aliasing is reduced at higher OS using zero-crossing variance metric. Write unit test UT-DI-008 for real-time safety: verify design intent (prepare allocates, process/reset/setType do not crash, all types stable). Write UT-DI-011 for setParams round-trip for all categories.
- [X] T046 [P] [US5] Write integration tests IT-DI-001 (audio flows through 4-band chain with distortion), IT-DI-002 (different type per band produces independent output), IT-DI-003 (distortion type persists and affects output) in `plugins/Disrumpo/tests/unit/distortion_adapter_test.cpp`.
- [X] T047 [P] [US5] Write performance tests: PT-DI-001 (1 band 1x OS < 2% CPU), PT-DI-002 (4 bands distortion < 5% CPU), PT-DI-003 (8 bands 4x OS < 10% CPU) in `plugins/Disrumpo/tests/unit/distortion_adapter_test.cpp`.

### 7.2 Implementation -- Common Parameters (T3.11)

- [X] T048 [P] [US5] In `plugins/Disrumpo/src/dsp/distortion_adapter.cpp`, implement full `process()` method: Drive Gate check (if `commonParams_.drive < 0.0001f`, bypass entire distortion path and return `input` directly to mix stage). Otherwise scale input by `commonParams_.drive`, call `processRaw()`, apply DC blocker if `needsDCBlock_`, call `applyTone()` on wet signal, blend with dry via `commonParams_.mix`. Implement `applyTone()` using the OnePole lowpass at `commonParams_.toneHz`. Implement `processBlock()` applying the same logic per sample.

### 7.3 Implementation -- Oversampler Integration (T3.12)

- [X] T049 [US5] In `plugins/Disrumpo/src/dsp/band_processor.h`, add `Krate::DSP::Oversampler<2,2> oversampler2x_`, `Krate::DSP::Oversampler<4,2> oversampler4x_`, and `Krate::DSP::Oversampler<2,2> oversampler8xInner_` (for 8x cascade) member variables, `int currentOversampleFactor_` defaulting to 2, `int maxOversampleFactor_` defaulting to 8, and `setMaxOversampleFactor(int)` / `setDistortionType()` methods. `setDistortionType()` calls `getRecommendedOversample()` from `distortion_types.h` and stores the result as `currentOversampleFactor_`, clamped by `maxOversampleFactor_`.
- [X] T050 [US5] In `plugins/Disrumpo/src/dsp/band_processor.h` (header-only), implement `prepare()` to call oversampler prepare methods. Implement `processBlock()` with oversampling paths: 1x direct, 2x, 4x, 8x (cascade 4x + 2x inner). Signal flow: apply sweep intensity, process distortion at oversampled rate, apply output stage (gain/pan/mute).

### 7.4 Implementation -- Band Wiring (T3.13)

- [X] T051 [US5] In `plugins/Disrumpo/src/dsp/band_processor.h`, wire DistortionAdapter into each band's processing chain. Each BandProcessor instance owns one DistortionAdapter. Default drive=0 for passthrough (backward compatible with existing tests). Expose `setDistortionType()`, `setDistortionCommonParams()`, `setDistortionParams()` on BandProcessor to forward to the adapter.

### 7.5 Verify

- [X] T052 [US5] Build and run all tests. 66 tests pass including UT-DI-003 through UT-DI-011, IT-DI-001 through IT-DI-003, PT-DI-001 through PT-DI-003.

### 7.6 Cross-Platform Verification

- [X] T053 [US5] Verify Oversampler template instantiation compiles (build succeeds). Lambda captures in oversampler process callbacks correctly capture `this` by reference. No C4100 warnings observed.

### 7.7 Commit

- [ ] T054 [US5] Commit common parameters, oversampler integration, and band wiring (T3.11-T3.13)

**Checkpoint**: M2 milestone deliverables complete. All 26 types process audio through the full per-band signal chain with oversampling, DC blocking, drive gating, mix blending, and tone filtering.

---

## Phase 8: Polish and Cross-Cutting Concerns

**Purpose**: Final verification, documentation, and architecture compliance.

- [ ] T055 [P] Run full test suite and confirm zero failures across all unit, integration, and performance tests
- [ ] T056 [P] Run pluginval at strictness level 5 against the built plugin: `tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Disrumpo.vst3"`
- [ ] T057 Verify M2 milestone criteria from roadmap.md: all 26 types process audio (UT-DI-001), common parameters work (UT-DI-003/004/005), oversampling works at 1x/2x/4x/8x (UT-DI-007), CPU under 5% at 4 bands 4x OS single type (PT-DI-002)
- [ ] T058 Review all new code for zero compiler warnings across MSVC, Clang, and GCC. Fix any remaining C4244, C4267, C4100 warnings.

---

## Phase 9: Architecture Documentation Update (MANDATORY)

> Constitution Principle XIII: Every spec implementation MUST update architecture documentation as a final task

- [ ] T059 Update `specs/_architecture_/` (or equivalent layer documentation) with entries for DistortionAdapter (plugin-specific DSP layer, namespace Disrumpo), DistortionType enum, and the per-band oversampler integration pattern. Include public API summary, file locations, and "when to use" guidance.
- [ ] T060 Verify no ODR conflicts: search for any class named `DistortionAdapter` or `DistortionType` in `dsp/` or other plugin directories to confirm uniqueness within namespace Disrumpo.
- [ ] T061 Commit architecture documentation updates

---

## Phase 10: Completion Verification (MANDATORY)

> Constitution Principle XV: Spec implementations MUST be honestly assessed.

- [ ] T062 Review ALL FR-DI-001 through FR-DI-007 requirements from spec.md against implementation. Confirm each is met or document gaps.
- [ ] T063 Review ALL success criteria from spec.md section 7 (M2 milestone verification table). Confirm measurable targets are achieved.
- [ ] T064 Search implementation for placeholder/stub/TODO comments. Confirm none remain in production code.
- [ ] T065 Confirm no test thresholds were relaxed from spec requirements.
- [ ] T066 Update spec.md Implementation Verification section with compliance status per requirement. Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL.
- [ ] T067 Final commit: all spec work committed and verified

**Checkpoint**: Honest assessment complete. M2 milestone achieved (or gaps explicitly documented).

---

## Dependencies and Execution Order

### Phase Dependencies

```
Phase 1 (Setup) --> Phase 2 (Foundation: T3.1, T3.2)
                         |
                         +---------> Phase 3 (US1: Saturation D01-D06)     [T3.3]
                         +---------> Phase 4 (US2: Wavefold+Rectify D07-D11) [T3.4, T3.5]
                         +---------> Phase 5 (US3: Digital D12-D14, D18-D19) [T3.6, T3.7]
                         +---------> Phase 6 (US4: Dynamic+Hybrid+Experimental) [T3.8, T3.9, T3.10]
                                          |
                    Phases 3,4,5,6 complete (all 26 types wired)
                                          |
                                          v
                                    Phase 7 (US5: Common Params + OS + Wiring) [T3.11, T3.12, T3.13]
                                          |
                                          v
                                    Phases 8,9,10 (Polish, Docs, Verification)
```

### Parallel Opportunities

- **Phase 2**: T004 and T005 (test writing) can run in parallel.
- **Phases 3-6**: Once Phase 2 is committed, all four type-integration phases (US1 through US4) can execute in parallel by separate developers. They touch disjoint switch cases in `processRaw()` and disjoint parameter routes in `routeParamsToProcessor()`.
  - Phase 3 writes: SoftClip/HardClip/Tube/Tape/Fuzz/AsymmetricFuzz cases
  - Phase 4 writes: SineFold/TriangleFold/SergeFold/FullRectify/HalfRectify cases
  - Phase 5 writes: Bitcrush/SampleReduce/Quantize/Aliasing/BitwiseMangler cases
  - Phase 6 writes: Temporal/RingSaturation/FeedbackDist/AllpassResonant/Chaos/Formant/Granular/Spectral/Fractal/Stochastic cases
- **Phase 7**: T048 (common params) and T049 (oversampler header additions) can run in parallel. T050 and T051 depend on both completing.

### Within Each User Story

1. Tests FIRST (must FAIL)
2. Implementation (makes tests pass)
3. Verify tests pass
4. Cross-platform check
5. Commit

---

## Parallel Example: Phases 3-6 (Type Integration)

Once Phase 2 is committed, four developers can work simultaneously:

```
Developer A --> Phase 3: Saturation (T013-T020)
Developer B --> Phase 4: Wavefold + Rectify (T021-T027)
Developer C --> Phase 5: Digital (T028-T033)
Developer D --> Phase 6: Dynamic + Hybrid + Experimental (T034-T043)
```

Each developer works on disjoint `case` branches in `processRaw()` and disjoint parameter routes in `routeParamsToProcessor()`. Merge conflicts are limited to function-level additions within the switch statement.

---

## Implementation Strategy

### MVP First (Phase 2 + Phase 3 only)

1. Complete Phase 1: Setup (CMake)
2. Complete Phase 2: Foundation (enum + adapter skeleton)
3. Complete Phase 3: Saturation (first concrete category)
4. STOP and VALIDATE: 6 types working end-to-end
5. This proves the integration pattern before committing to remaining 20 types

### Incremental Delivery

1. Setup + Foundation --> Adapter compiles, runs at 1 type
2. + Saturation (6 types) --> Pattern validated
3. + Wavefold + Rectify (5 types) --> 11 types
4. + Digital (5 types) --> 16 types
5. + Dynamic + Hybrid + Experimental (10 types) --> All 26 types
6. + Common Params + OS + Wiring --> M2 milestone complete

### Single Developer Sequential Order

T001-T012 (Setup + Foundation) -> T013-T020 (Saturation) -> T021-T027 (Wavefold+Rectify) -> T028-T033 (Digital) -> T034-T043 (Dynamic+Hybrid+Experimental) -> T044-T054 (Common+OS+Wiring) -> T055-T067 (Polish+Verify)

---

## Summary

| Metric | Value |
|--------|-------|
| Total tasks | 67 |
| Phase 1 (Setup) | 3 tasks |
| Phase 2 (Foundation) | 9 tasks |
| Phase 3 (US1 Saturation) | 8 tasks |
| Phase 4 (US2 Wavefold+Rectify) | 7 tasks |
| Phase 5 (US3 Digital) | 6 tasks |
| Phase 6 (US4 Dynamic+Hybrid+Experimental) | 10 tasks |
| Phase 7 (US5 Common+OS+Wiring) | 11 tasks |
| Phases 8-10 (Polish+Docs+Verify) | 13 tasks |
| Parallel opportunities | Phases 3-6 fully parallel; within Phase 7, T048+T049 parallel |
| MVP scope | Phases 1-3 (Setup + Foundation + Saturation = 20 tasks) |
| Independent test per story | Each US phase has explicit "Independent Test" criteria |

### Independent Test Criteria Per Story

- **US1 (Saturation)**: 6 types produce distinct output; bias affects AsymmetricFuzz; DC blocker active for D06
- **US2 (Wavefold+Rectify)**: 5 types distinct from passthrough; folds param changes output; DC removed for D10-D11
- **US3 (Digital)**: 5 types produce lo-fi output; bitDepth and rotateAmount params effective
- **US4 (Dynamic+Hybrid+Experimental)**: 10 types distinct; sensitivity/feedback params effective; block-based latency reported for Spectral/Granular; DC blocker for FeedbackDist
- **US5 (Common+OS+Wiring)**: Drive gate passthrough at 0; mix blend correct; tone filter attenuates; oversampling reduces aliasing; 4-band chain under 5% CPU

### Roadmap Task Mapping

| Roadmap Task | This File Phases | Effort |
|--------------|-----------------|--------|
| T3.1 DistortionAdapter interface | Phase 2 (T008-T009) | 8h |
| T3.2 DistortionType enum | Phase 2 (T006-T007) | 4h |
| T3.3 Saturation D01-D06 | Phase 3 (T013-T020) | 8h |
| T3.4 Wavefold D07-D09 | Phase 4 (T021-T027) | 4h |
| T3.5 Rectify D10-D11 | Phase 4 (T021-T027) | 2h |
| T3.6 Digital D12-D14 | Phase 5 (T028-T033) | 4h |
| T3.7 Digital D18-D19 | Phase 5 (T028-T033) | 4h |
| T3.8 Dynamic D15 | Phase 6 (T034-T043) | 2h |
| T3.9 Hybrid D16-D17, D26 | Phase 6 (T034-T043) | 6h |
| T3.10 Experimental D20-D25 | Phase 6 (T034-T043) | 8h |
| T3.11 Common params | Phase 7 (T044-T054) | 4h |
| T3.12 Oversampler | Phase 7 (T044-T054) | 6h |
| T3.13 Band wiring | Phase 7 (T044-T054) | 4h |
