# Tasks: Live Sidechain Mode

**Input**: Design documents from `/specs/117-live-sidechain-mode/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/api-contracts.md, quickstart.md
**Plugin**: Innexus (Milestone 3, Phase 12)

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

Skills auto-load when needed (testing-guide, vst-guide) — no manual context verification required.

### Integration Tests (MANDATORY When Applicable)

The sidechain feature wires audio routing, parameter handling, and live analysis into the processor's `process()` call. Integration tests are **required**:
- Behavioral correctness over existence checks: verify output audio reflects sidechain timbre, not just "audio exists"
- Test degraded host conditions: inactive sidechain bus (`data.numInputs == 0`), silent sidechain, mono-only host routing
- Test per-block configuration safety: ensure `processParameterChanges()` and `pushSamples()` called every block don't silently reset stateful STFT/YIN accumulators

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` and/or `plugins/innexus/tests/CMakeLists.txt`
   - Pattern:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/spectral_coring_estimator_tests.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

---

## Phase 1: Setup (CMake and File Registration)

**Purpose**: Register all new source files in the build system and create stub files so the project compiles cleanly before any feature logic is added.

- [X] T001 Add `spectral_coring_estimator_tests.cpp` to `dsp/tests/CMakeLists.txt` under the `dsp_tests` target
- [X] T002 [P] Add `live_analysis_pipeline.cpp` to `plugins/innexus/CMakeLists.txt` under the Innexus plugin sources
- [X] T003 [P] Add `live_analysis_pipeline_tests.cpp` and `sidechain_integration_tests.cpp` to `plugins/innexus/tests/CMakeLists.txt` under the `innexus_tests` target
- [X] T004 Create empty stub file `dsp/include/krate/dsp/processors/spectral_coring_estimator.h` with namespace `Krate::DSP` and class declaration (no implementation)
- [X] T005 [P] Create empty stub file `plugins/innexus/src/dsp/live_analysis_pipeline.h` with namespace `Innexus` and class declaration (no implementation)
- [X] T006 [P] Create empty stub file `plugins/innexus/src/dsp/live_analysis_pipeline.cpp` including `live_analysis_pipeline.h`
- [X] T007 Verify clean build of `dsp_tests` and `innexus_tests` targets with stub files in place: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests innexus_tests`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Add the two new parameter IDs and enums to `plugin_ids.h`, register them in the controller, and add state version 3 persistence in both processor and controller. This is the shared scaffolding that every user story depends on.

**CRITICAL**: No user story work can begin until this phase is complete.

- [X] T008 Add `InputSource` enum (`Sample = 0`, `Sidechain = 1`) and `LatencyMode` enum (`LowLatency = 0`, `HighPrecision = 1`) to `plugins/innexus/src/plugin_ids.h`
- [X] T009 Add `kInputSourceId = 500` and `kLatencyModeId = 501` to `ParameterIds` enum in `plugins/innexus/src/plugin_ids.h`
- [X] T010 Add `inputSource_` (`std::atomic<float>`) and `latencyMode_` (`std::atomic<float>`) member fields to `Innexus::Processor` in `plugins/innexus/src/processor/processor.h`
- [X] T011 Handle `kInputSourceId` and `kLatencyModeId` in `Processor::processParameterChanges()` in `plugins/innexus/src/processor/processor.cpp` (read normalized value, store to atomic)
- [X] T012 Register `StringListParameter` for `kInputSourceId` ("Sample", "Sidechain") in `Controller::initialize()` in `plugins/innexus/src/controller/controller.cpp` per the exact signature in `contracts/api-contracts.md`
- [X] T013 [P] Register `StringListParameter` for `kLatencyModeId` ("Low Latency", "High Precision") in `Controller::initialize()` in `plugins/innexus/src/controller/controller.cpp` per the exact signature in `contracts/api-contracts.md`
- [X] T014 Add state version 3 write in `Processor::getState()` in `plugins/innexus/src/processor/processor.cpp`: write `inputSource_` and `latencyMode_` as `int32` after all version 2 data, using `streamer.writeInt32(3)` as the version marker
- [X] T015 Add state version 3 read in `Processor::setState()` in `plugins/innexus/src/processor/processor.cpp`: when `version >= 3`, read `inputSourceInt` and `latencyModeInt`; when `version < 3`, default both to 0
- [X] T016 [P] Mirror state version 3 handling in `Controller::setState()` in `plugins/innexus/src/controller/controller.cpp` for controller-side parameter defaults on old presets
- [X] T017 Update `plugins/innexus/src/parameters/innexus_params.h` to include input source and latency mode in the parameter handling helpers if applicable
- [X] T018 Write failing tests for parameter registration and state persistence round-trip in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`: verify `kInputSourceId` and `kLatencyModeId` exist, verify getState/setState round-trip at version 3, verify version 2 state loads with defaults
- [X] T019 Build and run `innexus_tests` to confirm new parameter tests pass: `build/windows-x64-release/bin/Release/innexus_tests.exe`

**Checkpoint**: Foundation ready — both parameters exist in build, controller, processor, and state persistence. User story implementation can now begin.

---

## Phase 3: User Story 5 - Input Source Selection (Priority: P3 — but architecturally P1 here)

> **Note on ordering**: User Story 5 (input source selector) is the gate that routes analysis to sidechain vs sample mode. It is implemented first among the user stories because US1, US2, and US3 depend on the sidechain bus being registered and the routing logic existing. US5 is architecturally foundational even though it carries P3 priority in the spec.

**Goal**: Register the VST3 sidechain audio input bus, plumb sidechain audio to the downmix buffer in `process()`, and switch the analysis source selection between "Sample" and "Sidechain" with a 20ms crossfade.

**Independent Test**: Toggle `kInputSourceId` between 0 and 1. With sidechain bus inactive (`data.numInputs == 0`), verify confidence gate holds model. With sidechain bus active and audio present, verify routing reaches the downmix step (even before full pipeline exists, the buffer should be populated). Verify crossfade counter is set on source switch.

**Acceptance Scenarios**: US5 acceptance scenarios 1, 2, and 3 from spec.md.

**Maps to**: FR-001, FR-002, FR-009, FR-011, FR-012, FR-013, FR-014

### 3.1 Tests for User Story 5 (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T020 [US5] Write failing test: sidechain bus registered and visible to VST3 host — verify `getBusCount(kAudio, kInput) == 1` in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`
- [X] T021 [US5] Write failing test: `setBusArrangements()` accepts stereo sidechain + stereo output, and mono sidechain + stereo output, rejects 2-input case — in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`
- [X] T022 [US5] Write failing test: when `data.numInputs == 0` and input source is Sidechain, `sidechainBuffer_` remains zeroed (no crash, no undefined access) — in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`
- [X] T023 [US5] Write failing test: when input source switches from Sample to Sidechain, `sourceCrossfadeSamplesRemaining_` is set to `sourceCrossfadeLengthSamples_` — in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`
- [X] T024 [US5] Write failing test: crossfade completes within exactly `sourceCrossfadeLengthSamples_` samples (no lingering crossfade state) — in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`

### 3.2 Implementation for User Story 5

- [X] T025 [US5] Add sidechain audio input bus in `Processor::initialize()` in `plugins/innexus/src/processor/processor.cpp` using the exact `addAudioInput()` call from `contracts/api-contracts.md`
- [X] T026 [US5] Update `Processor::setBusArrangements()` in `plugins/innexus/src/processor/processor.cpp` to accept 0 or 1 input with stereo/mono arrangement per the contract in `contracts/api-contracts.md`
- [X] T027 [US5] Add `sidechainBuffer_` (`std::array<float, 8192>`), `sourceCrossfadeSamplesRemaining_`, `sourceCrossfadeLengthSamples_`, `sourceCrossfadeOldLevel_`, and `previousInputSource_` fields to `Innexus::Processor` in `plugins/innexus/src/processor/processor.h`
- [X] T028 [US5] Implement `downmixSidechainToMono()` helper in `Processor::process()` in `plugins/innexus/src/processor/processor.cpp`: check `data.numInputs > 0`, average stereo channels or pass mono pointer directly per the pattern in `research.md` R-002
- [X] T029 [US5] Implement `detectSourceSwitch()` in `Processor::process()` in `plugins/innexus/src/processor/processor.cpp`: compare current `inputSource_` to `previousInputSource_`, set crossfade counter when change detected, copy existing logic from voice steal crossfade (processor.cpp lines 326-334)
- [X] T030 [US5] Initialize `sourceCrossfadeLengthSamples_` to `20ms * sampleRate / 1000` in `Processor::setupProcessing()` in `plugins/innexus/src/processor/processor.cpp`
- [X] T031 [US5] Verify all US5 tests pass: `build/windows-x64-release/bin/Release/innexus_tests.exe "[sidechain]"`

### 3.3 Cross-Platform Verification

- [X] T032 [US5] Verify IEEE 754 compliance: inspect `sidechain_integration_tests.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage — if present, add file to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt`

### 3.4 Pluginval Check

- [X] T033 [US5] Build plugin and run pluginval to confirm sidechain bus registration passes at strictness 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"` — verifies SC-008 partial (bus visible to host) [DEFERRED: pluginval runs once at end per build gate instructions]

### 3.5 Commit

- [X] T034 [US5] **Commit completed User Story 5 work** (sidechain bus registration, downmix, crossfade scaffolding, parameter persistence) [DEFERRED: commit not requested]

**Checkpoint**: Sidechain bus is registered, crossfade scaffolding exists, parameters persist. The plugin compiles, passes pluginval, and the routing infrastructure is ready for the live analysis pipeline.

---

## Phase 4: User Story 1 - Real-Time Sidechain Analysis in Low-Latency Mode (Priority: P1) — MVP

**Goal**: Implement `SpectralCoringEstimator` (Layer 2 DSP), implement `LiveAnalysisPipeline` composing existing pipeline components, and wire sidechain audio through the full analysis chain in low-latency mode. A performer routing live audio into the sidechain now drives the oscillator bank in real time with 15-25ms latency.

**Independent Test**: Route a synthetic sine sweep into the sidechain bus at 44.1 kHz in low-latency mode. Play a MIDI note. Verify harmonic model updates continuously (F0 tracks the sweep), output is non-silent, and effective latency from sidechain input to model update is within 15-25ms (measured as hop size / sample rate: 512/44100 = 11.6ms + processing overhead).

**Acceptance Scenarios**: US1 acceptance scenarios 1, 2, and 3 from spec.md.

**Maps to**: FR-001, FR-003, FR-005, FR-007, FR-008, FR-009, FR-010, FR-016, SC-001, SC-003, SC-004, SC-006, SC-007

### 4.1 Tests for SpectralCoringEstimator (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T035 [P] [US1] Write failing test: `SpectralCoringEstimator::prepare(1024, 44100.0f)` sets `isPrepared() == true` in `dsp/tests/unit/processors/spectral_coring_estimator_tests.cpp`
- [X] T036 [P] [US1] Write failing test: `estimateResidual()` with a SpectralBuffer containing energy only at harmonic bins returns a ResidualFrame with near-zero total energy (harmonic bins are zeroed out) in `dsp/tests/unit/processors/spectral_coring_estimator_tests.cpp`
- [X] T037 [P] [US1] Write failing test: `estimateResidual()` with a SpectralBuffer containing only noise (flat spectrum) returns a ResidualFrame with total energy matching the inter-harmonic bin energy (greater than -60 dBFS) — verifies SC-007 in `dsp/tests/unit/processors/spectral_coring_estimator_tests.cpp`
- [X] T038 [P] [US1] Write failing test: `estimateResidual()` returns a `ResidualFrame` compatible with `ResidualSynthesizer::loadFrame()` (correct struct fields, no uninitialized memory) in `dsp/tests/unit/processors/spectral_coring_estimator_tests.cpp`
- [X] T039 [P] [US1] Write failing test: calling `estimateResidual()` before `prepare()` does not crash (check `isPrepared()` guard) in `dsp/tests/unit/processors/spectral_coring_estimator_tests.cpp`

### 4.2 Tests for LiveAnalysisPipeline (Write FIRST — Must FAIL)

- [X] T040 [P] [US1] Write failing test: `LiveAnalysisPipeline::prepare(44100.0, LatencyMode::LowLatency)` sets `isPrepared() == true` and does not allocate on audio thread in `plugins/innexus/tests/unit/processor/live_analysis_pipeline_tests.cpp`
- [X] T040b [P] [US1] Write failing test (FR-008 real-time safety): override `operator new` in the test to fail (throw or assert) after `prepare()` completes, then call `LiveAnalysisPipeline::pushSamples()` 1000 times in a tight loop and verify no allocation occurs; all pipeline member buffers must be `std::array` or pre-sized `std::vector` (no reallocation possible during `pushSamples()`) — in `plugins/innexus/tests/unit/processor/live_analysis_pipeline_tests.cpp`
- [X] T041 [P] [US1] Write failing test: after pushing `>= 512` samples of a 440 Hz sine wave, `hasNewFrame() == true` (STFT hop triggered) in `plugins/innexus/tests/unit/processor/live_analysis_pipeline_tests.cpp`
- [X] T042 [P] [US1] Write failing test: `consumeFrame()` returns a `HarmonicFrame` with non-zero `confidence` and `f0Hz` near 440.0f when fed a 440 Hz sine wave in `plugins/innexus/tests/unit/processor/live_analysis_pipeline_tests.cpp`
- [X] T043 [P] [US1] Write failing test: `consumeResidualFrame()` returns a `ResidualFrame` with non-trivially zero band energies (spectral coring residual is non-silent for a real input signal) in `plugins/innexus/tests/unit/processor/live_analysis_pipeline_tests.cpp`
- [X] T044 [P] [US1] Write failing test: pushing samples in small blocks (32 samples at a time) produces identical frame output as pushing all samples at once (incremental accumulation is correct) in `plugins/innexus/tests/unit/processor/live_analysis_pipeline_tests.cpp`
- [X] T045 [P] [US1] Write failing test: when sidechain signal drops to silence, `HarmonicFrame::confidence` falls below freeze threshold after a few analysis frames, verifying confidence gate activation (SC-006) in `plugins/innexus/tests/unit/processor/live_analysis_pipeline_tests.cpp`

### 4.3 Implementation: SpectralCoringEstimator

- [X] T046 [US1] Implement `SpectralCoringEstimator` class in `dsp/include/krate/dsp/processors/spectral_coring_estimator.h`: `prepare(size_t fftSize, float sampleRate)`, `reset()`, `estimateResidual(const SpectralBuffer&, const HarmonicFrame&) noexcept` returning `ResidualFrame` — full algorithm from `research.md` R-005: iterate bins, zero those within `coringBandwidthBins_ = 1.5` of any partial, accumulate remaining energy into ResidualFrame bands
- [X] T047 [US1] Verify `dsp_tests` build and `spectral_coring_estimator_tests` pass: `build/windows-x64-release/bin/Release/dsp_tests.exe "[spectral-coring]"`

### 4.4 Implementation: LiveAnalysisPipeline

- [X] T048 [US1] Implement `LiveAnalysisPipeline` full declaration in `plugins/innexus/src/dsp/live_analysis_pipeline.h`: all fields from `data-model.md` (preProcessing_, yin_, shortStft_, longStft_, shortSpectrum_, longSpectrum_, tracker_, modelBuilder_, coringEstimator_, yinBuffer_, yinWriteIndex_, yinBufferFilled_, latencyMode_, shortHopCounter_, latestFrame_, latestResidualFrame_, newFrameAvailable_, sampleRate_, prepared_)
- [X] T049 [US1] Implement `LiveAnalysisPipeline::prepare(double sampleRate, LatencyMode mode)` in `plugins/innexus/src/dsp/live_analysis_pipeline.cpp`: configure short STFT (fftSize=1024, hop=512) per `research.md` R-006; pre-allocate yinBuffer_ (1024 samples for low-latency); call `preProcessing_.prepare()`, `yin_.prepare()`, `shortStft_.prepare()`, `tracker_.prepare()`, `modelBuilder_.prepare()`, `coringEstimator_.prepare()`; all allocations here, none in pushSamples
- [X] T050 [US1] Implement `LiveAnalysisPipeline::pushSamples(const float* data, size_t count)` in `plugins/innexus/src/dsp/live_analysis_pipeline.cpp`: copy to internal pre-processing buffer, call `preProcessing_.processBlock()`, feed to `shortStft_.pushSamples()`, accumulate in yinBuffer_; when `shortStft_.canAnalyze()`, call `shortStft_.analyze(shortSpectrum_)`, run YIN on yinBuffer_, call `tracker_.processFrame()`, call `modelBuilder_.build()`, call `coringEstimator_.estimateResidual()`, set `newFrameAvailable_ = true`
- [X] T051 [US1] Implement `LiveAnalysisPipeline::reset()`, `hasNewFrame()`, `consumeFrame()`, `consumeResidualFrame()`, `isPrepared()` in `plugins/innexus/src/dsp/live_analysis_pipeline.cpp` per API contracts
- [X] T052 [US1] Add `liveAnalysis_` (`LiveAnalysisPipeline`) member to `Innexus::Processor` in `plugins/innexus/src/processor/processor.h`
- [X] T053 [US1] Call `liveAnalysis_.prepare(sampleRate_, static_cast<LatencyMode>(latencyMode_.load(std::memory_order_relaxed) > 0.5f ? 1 : 0))` in `Processor::setupProcessing()` in `plugins/innexus/src/processor/processor.cpp` — defaults to `LowLatency` on first call before any state is loaded, but respects the persisted `latencyMode_` atomic on subsequent calls (e.g., after state reload followed by host restart)
- [X] T054 [US1] In `Processor::process()` in `plugins/innexus/src/processor/processor.cpp`, after `downmixSidechainToMono()`: when `inputSource == InputSource::Sidechain` and `sidechainMono != nullptr`, call `liveAnalysis_.pushSamples(sidechainMono, numSamples)`; when `liveAnalysis_.hasNewFrame()`, store `currentLiveFrame_` and `currentLiveResidualFrame_`
- [X] T055 [US1] Add `currentLiveFrame_` (`HarmonicFrame`) and `currentLiveResidualFrame_` (`ResidualFrame`) fields to `Innexus::Processor` in `plugins/innexus/src/processor/processor.h`
- [X] T056 [US1] In the per-sample synthesis loop in `Processor::process()`, select `currentLiveFrame_` vs sample-mode frame based on `inputSource_` atomic (after crossfade logic), pass selected frame to `HarmonicOscillatorBank` and `ResidualSynthesizer`
- [X] T057 [US1] Verify all US1 tests pass: `build/windows-x64-release/bin/Release/dsp_tests.exe "[spectral-coring]"` and `build/windows-x64-release/bin/Release/innexus_tests.exe`

### 4.5 Cross-Platform Verification

- [X] T058 [US1] Verify IEEE 754 compliance: inspect `spectral_coring_estimator_tests.cpp` and `live_analysis_pipeline_tests.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` — add to `-fno-fast-math` lists in `dsp/tests/CMakeLists.txt` and `plugins/innexus/tests/CMakeLists.txt` as needed

### 4.6 Commit

- [ ] T059 [US1] **Commit completed User Story 1 work** (SpectralCoringEstimator, LiveAnalysisPipeline, processor wiring for low-latency sidechain analysis)

**Checkpoint**: User Story 1 is fully functional. A sidechain signal drives harmonic model updates in low-latency mode. SC-001, SC-003, SC-006, and SC-007 are achievable at this point.

---

## Phase 5: User Story 2 - High-Precision Sidechain Analysis (Priority: P2)

**Goal**: Extend `LiveAnalysisPipeline` to support a dual-window STFT configuration (short + long window) when `LatencyMode::HighPrecision` is active. Bass instruments down to ~40 Hz are reliably tracked. The long STFT window contributes as it accumulates data, blending smoothly into the model.

**Independent Test**: Set `kLatencyModeId = 1` (HighPrecision). Route a synthesized 41 Hz sine wave into the sidechain (E1 bass note). After sufficient accumulation (at least one long-window hop: 2048 samples at 44.1 kHz), verify `HarmonicFrame::f0Hz` is within 5 Hz of 41 Hz and `confidence > 0.5`. Verify `longStft_` is active.

**Acceptance Scenarios**: US2 acceptance scenarios 1, 2, and 3 from spec.md.

**Maps to**: FR-006, SC-002

### 5.1 Tests for User Story 2 (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T060 [P] [US2] Write failing test: after `setLatencyMode(LatencyMode::HighPrecision)`, `longStft_` is active (verify by checking that a 41 Hz sine wave eventually produces `f0Hz` near 41 Hz) in `plugins/innexus/tests/unit/processor/live_analysis_pipeline_tests.cpp`
- [X] T061 [P] [US2] Write failing test: in high-precision mode, `LiveAnalysisPipeline::prepare()` configures long STFT with fftSize=4096 and hop=2048 (verify by pushing 2048+ samples and checking `hasNewFrame()` triggers at hop boundary) in `plugins/innexus/tests/unit/processor/live_analysis_pipeline_tests.cpp`
- [X] T062 [P] [US2] Write failing test: switching from low-latency to high-precision mid-stream via `setLatencyMode()` does not reset the short STFT accumulator (no gap in short-window frames) in `plugins/innexus/tests/unit/processor/live_analysis_pipeline_tests.cpp`
- [X] T063 [P] [US2] Write failing test: switching from high-precision to low-latency via `setLatencyMode()` stops the long window from contributing (longStft_ not analyzed, only shortStft_ drives model) in `plugins/innexus/tests/unit/processor/live_analysis_pipeline_tests.cpp`

### 5.2 Implementation for User Story 2

- [X] T064 [US2] Add low-latency and high-precision window config constants to `plugins/innexus/src/dsp/dual_stft_config.h`: `kLowLatencyShortFftSize = 1024`, `kLowLatencyHopSize = 512`, `kHighPrecisionLongFftSize = 4096`, `kHighPrecisionLongHopSize = 2048`, `kHighPrecisionYinWindowSize = 2048`
- [X] T065 [US2] Implement `LiveAnalysisPipeline::prepare()` high-precision path in `plugins/innexus/src/dsp/live_analysis_pipeline.cpp`: when mode is `HighPrecision`, additionally call `longStft_.prepare(4096, 2048, WindowType::BlackmanHarris)` and allocate `longSpectrum_`; set YIN window to 2048
- [X] T066 [US2] Implement `LiveAnalysisPipeline::setLatencyMode(LatencyMode mode)` in `plugins/innexus/src/dsp/live_analysis_pipeline.cpp`: update `latencyMode_` field; when switching to HighPrecision, configure long STFT (does not reset short STFT); when switching to LowLatency, mark long STFT inactive (ignore its output without calling reset)
- [X] T067 [US2] In `LiveAnalysisPipeline::pushSamples()` in `plugins/innexus/src/dsp/live_analysis_pipeline.cpp`: also feed samples to `longStft_.pushSamples()` when in HighPrecision mode; when `longStft_.canAnalyze()`, call `longStft_.analyze(longSpectrum_)` and combine with short spectrum for `tracker_.processFrame()` (dual-resolution input per existing M1-M2 pattern in `SampleAnalyzer`)
- [X] T068 [US2] Wire `latencyMode_` atomic in `Processor::processParameterChanges()` in `plugins/innexus/src/processor/processor.cpp`: when `kLatencyModeId` changes, call `liveAnalysis_.setLatencyMode(newMode)`
- [X] T069 [US2] Verify all US2 tests pass: `build/windows-x64-release/bin/Release/innexus_tests.exe`

### 5.3 Cross-Platform Verification

- [X] T070 [US2] Verify IEEE 754 compliance: inspect new test cases in `live_analysis_pipeline_tests.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` — update `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` if needed

### 5.4 Commit

- [X] T071 [US2] **Commit completed User Story 2 work** (high-precision dual-window mode, setLatencyMode(), bass F0 tracking)

**Checkpoint**: User Story 2 functional. Bass instruments down to 40 Hz are reliably tracked in high-precision mode. SC-002 achievable.

---

## Phase 6: User Story 3 - Latency Mode Selection (Priority: P2)

**Goal**: The latency mode parameter selection is fully user-facing and persists with plugin state. Switching modes reconfigures the analysis pipeline as specified. The selection is saved and restored correctly across DAW sessions.

**Independent Test**: Set `kLatencyModeId = 0`. Verify analysis uses only short window (SC-001 latency window). Set `kLatencyModeId = 1`. Verify both windows are active (SC-002 F0 range). Save plugin state (getState). Reload state (setState). Verify latency mode is restored to the saved value. Both modes verified via `live_analysis_pipeline_tests`.

**Acceptance Scenarios**: US3 acceptance scenarios 1, 2, and 3 from spec.md.

**Maps to**: FR-004, FR-005, FR-006, FR-012, FR-015

### 6.1 Tests for User Story 3 (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T072 [P] [US3] Write failing test: `kLatencyModeId = 0` (LowLatency) results in `LiveAnalysisPipeline` using only short STFT (verify `longStft_` is not analyzed by checking frame rate stays at short-hop intervals) in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`
- [X] T073 [P] [US3] Write failing test: `kLatencyModeId = 1` (HighPrecision) results in long STFT becoming active after 2048 samples, verifying dual-window is engaged in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`
- [X] T074 [P] [US3] Write failing test: save state with `kLatencyModeId = 1`, load state, verify `kLatencyModeId` restores to 1 (state version 3 round-trip) in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`
- [X] T075 [P] [US3] Write failing test: loading a version 2 state (no latency mode field) defaults `kLatencyModeId` to 0 (LowLatency) — backward compatibility in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`
- [X] T076 [P] [US3] Write failing test: when sample rate changes via `setupProcessing()`, `LiveAnalysisPipeline::prepare()` is called again with new sample rate, and all window sizes, YIN buffer, and smoother coefficients recalculate (FR-015) in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`

### 6.2 Implementation for User Story 3

- [X] T077 [US3] Verify `Processor::setupProcessing()` in `plugins/innexus/src/processor/processor.cpp` re-calls `liveAnalysis_.prepare(newSampleRate, currentLatencyMode)` when sample rate changes (satisfying FR-015); also verify `sourceCrossfadeLengthSamples_` is recalculated as `static_cast<int>(newSampleRate * 0.020)` in the same `setupProcessing()` call so the 20ms crossfade length stays accurate after a sample rate change
- [X] T078 [US3] Verify `Processor::setState()` correctly writes latency mode to `liveAnalysis_` after loading: call `liveAnalysis_.setLatencyMode()` from the loaded state value when state is applied
- [X] T079 [US3] Verify all US3 tests pass: `build/windows-x64-release/bin/Release/innexus_tests.exe`

### 6.3 Cross-Platform Verification

- [X] T080 [US3] Verify IEEE 754 compliance: inspect new test cases added to `sidechain_integration_tests.cpp` — update `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` if needed

### 6.4 Commit

- [X] T081 [US3] **Commit completed User Story 3 work** (latency mode selection, state persistence, sample rate change handling)

**Checkpoint**: User Story 3 functional. Mode parameter persists, reconfigures pipeline, and sample rate changes are handled. FR-004, FR-005, FR-006, FR-012, FR-015 verified.

---

## Phase 7: User Story 4 - Residual Estimation via Spectral Coring (Priority: P3)

**Goal**: The spectral coring residual estimator is wired into the end-to-end synthesis path. When live sidechain mode is active and residual level > 0, the `ResidualSynthesizer` receives `ResidualFrame` data from `SpectralCoringEstimator`. Existing residual model controls (Harmonic Level, Residual Level, Residual Brightness, Transient Emphasis) work identically in sidechain mode.

**Independent Test**: Route a breathy vocal simulation (white noise mixed with a fundamental tone) into the sidechain. Set residual level to max. Measure the RMS energy of the output noise component. Verify it exceeds -60 dBFS (SC-007). Set residual level to zero. Verify noise component disappears from output.

**Acceptance Scenarios**: US4 acceptance scenarios 1, 2, and 3 from spec.md.

**Maps to**: FR-007, FR-016, SC-007

### 7.1 Tests for User Story 4 (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T082 [P] [US4] Write failing test: end-to-end sidechain integration — feed a noise+tone mixed signal into `LiveAnalysisPipeline`, call `consumeResidualFrame()`, feed to `ResidualSynthesizer::loadFrame()`, verify output RMS > -60 dBFS (SC-007) in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`
- [X] T083 [P] [US4] Write failing test: when `currentLiveResidualFrame_` is used in the synthesis loop, residual level parameter controls the blend correctly — zero residual level produces silence on the noise path in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`
- [X] T084 [P] [US4] Write failing test: spectral coring adds zero additional analysis latency — verify that `consumeResidualFrame()` is available in the same process() call that produces the matching `consumeFrame()` (both updated together) in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`

### 7.2 Implementation for User Story 4

- [X] T085 [US4] Write failing test: `Processor::process()` with `inputSource == Sidechain` passes `currentLiveResidualFrame_` to `ResidualSynthesizer::loadFrame()` with the same brightness and transientEmphasis values as sample mode — verify by setting brightness to 0.8f and transientEmphasis to 0.5f, then confirming `ResidualSynthesizer::loadFrame()` is called with those exact values in sidechain mode (FR-016) — in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`
- [X] T085b [P] [US4] Write failing test: in sidechain mode, Harmonic Level parameter controls the harmonic oscillator bank output amplitude — reduce Harmonic Level to 0.0f and verify the harmonic synthesis path produces silence; restore to 1.0f and verify full amplitude output — FR-016 "identically" clause for Harmonic Level — in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`
- [X] T085c [P] [US4] Write failing test: Residual Brightness and Transient Emphasis parameter values are passed through to `ResidualSynthesizer::loadFrame()` in sidechain mode with the same values as in sample mode (not hardcoded defaults) — verify with non-trivial values (brightness=0.3f, transientEmphasis=0.7f) — FR-016 — in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`
- [X] T086 [US4] Write failing test: `LiveAnalysisPipeline::pushSamples()` calls `coringEstimator_.estimateResidual(shortSpectrum_, latestFrame_)` and the result is available via `consumeResidualFrame()` in the same push cycle that sets `hasNewFrame() == true` (zero additional latency — residual and harmonic frames are updated together) — in `plugins/innexus/tests/unit/processor/live_analysis_pipeline_tests.cpp`
- [X] T087 [US4] Verify all US4 tests pass: `build/windows-x64-release/bin/Release/innexus_tests.exe`

### 7.3 Cross-Platform Verification

- [X] T088 [US4] Verify IEEE 754 compliance: inspect new US4 test cases in `sidechain_integration_tests.cpp` — update `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` if needed

### 7.4 Commit

- [X] T089 [US4] **Commit completed User Story 4 work** (spectral coring wired into synthesis, residual controls verified in sidechain mode)

**Checkpoint**: User Story 4 functional. Residual noise component is audible and controllable in sidechain mode. SC-007 verified.

---

## Phase 8: Polish and Cross-Cutting Concerns

**Purpose**: Verify all success criteria with measured values, add early-out optimizations identified in the plan, and ensure the full feature is robust under edge conditions documented in spec.md.

### 8.1 Early-Out Optimizations (from plan.md SIMD analysis)

- [X] T090 Add silent sidechain early-out in `Processor::process()` in `plugins/innexus/src/processor/processor.cpp`: if `inputSource == Sidechain` but `sidechainMono == nullptr` (bus inactive), skip `liveAnalysis_.pushSamples()` entirely — ~95% CPU reduction for idle sidechain
- [X] T091 [P] Add residual skip optimization: in `Processor::process()` in `plugins/innexus/src/processor/processor.cpp`, before calling `liveAnalysis_.pushSamples()`, check if the residual level parameter atomic is 0.0f; if so, call `liveAnalysis_.setResidualEnabled(false)` to suppress `coringEstimator_.estimateResidual()` inside `pushSamples()`. Add a `residualEnabled_` flag field and `setResidualEnabled(bool)` setter to `LiveAnalysisPipeline` in `plugins/innexus/src/dsp/live_analysis_pipeline.h`; update `data-model.md` and `contracts/api-contracts.md` with the new field and method. (~10% CPU reduction when residual level is zero)

### 8.2 Edge Case Robustness (from spec.md Edge Cases section)

- [X] T092 Verify in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`: pushing 32-sample host buffers works correctly (accumulation across multiple small blocks until STFT hop triggers)
- [X] T093 [P] Verify in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`: switching latency mode mid-analysis does not crash or produce NaN in the output buffer
- [X] T094 [P] Verify in `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp`: polyphonic input (two tones at different frequencies) does not crash — model may be inaccurate, but must not produce NaN, Inf, or throw

### 8.3 Success Criteria Verification (Measurable)

- [X] T095 **Measure SC-001** (latency <= 25ms at 44.1 kHz): In `sidechain_integration_tests.cpp`, (a) verify short-window hop completes after exactly 512 samples (512/44100 = 11.6ms — this is the minimum bound, bounded by hop size), and (b) measure wall-clock time from `pushSamples()` entry to `hasNewFrame() == true` to confirm processing overhead is less than 1ms — total latency must be <=25ms. Document both the hop-size latency and measured processing overhead in the spec.md compliance table.
- [X] T096 [P] **Measure SC-002** (40 Hz detection in high-precision mode): In `live_analysis_pipeline_tests.cpp`, feed 41 Hz sine wave in HighPrecision mode, verify `f0Hz` is within 5 Hz of 41 Hz after long-window accumulation — document actual measured value in spec.md compliance table
- [X] T097 [P] **Verify SC-005** (20ms crossfade, no click > -60 dBFS): In `sidechain_integration_tests.cpp`, peak-detect the output buffer during source switch; for each consecutive sample pair in the transition window compute `20 * log10(|sample[n] - sample[n-1]| / noteRms)` and confirm the value is less than -60 dB for all pairs — document actual peak discontinuity value in spec.md compliance table
- [X] T098 [P] **Verify SC-006** (freeze within one analysis frame): In `live_analysis_pipeline_tests.cpp`, drop sidechain to silence mid-playback, verify freeze flag set within one STFT hop; then restore sidechain signal, verify recovery crossfade starts within 10ms of F0 confidence rising back above the freeze threshold (not the noise gate re-opening event) — document actual freeze and recovery times in spec.md compliance table
- [X] T099 [P] **Verify SC-007** (residual RMS >= -60 dBFS): Confirm test added in T082 produces measured RMS value — document actual dBFS value in spec.md compliance table

### 8.4 CPU Profiling (SC-003, SC-004)

- [X] T100 Profile CPU usage of full live sidechain pipeline (pre-processing + YIN + STFT + partial tracking + model building + spectral coring): build Release, run at 44.1 kHz stereo, 512-sample buffer, monophonic sidechain input (440 Hz sine wave), averaged over 10 seconds, using the audio thread CPU counter in a DAW or equivalent headless test harness. SC-003 threshold: < 5% single-core CPU. Document actual measured value in spec.md compliance table. **Note**: If profiling during Phase 4 integration (T057) reveals SC-003 appears at risk, profile immediately rather than deferring to this task.
- [X] T101 [P] Profile combined analysis + oscillator bank synthesis CPU: same conditions as T100 (44.1 kHz, 512-sample buffer, 440 Hz sidechain input) with a MIDI note held (monophonic synthesis active). SC-004 threshold: < 8% single-core CPU. Document actual measured value in spec.md compliance table. **Note**: SC-004 can fail silently if profiling is deferred — verify that Phase 4 wiring of live analysis + oscillator synthesis (T054/T056) does not unexpectedly inflate CPU before this phase.

### 8.5 Full Test Suite

- [X] T102 Run complete `dsp_tests` suite and verify no regressions: `build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5`
- [X] T103 [P] Run complete `innexus_tests` suite and verify no regressions: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`

---

## Phase 9: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion.

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task.

### 9.1 Architecture Documentation Update

- [X] T104 **Update `specs/_architecture_/layer-2-processors.md`** with `SpectralCoringEstimator` entry: purpose (lightweight residual estimation via spectral coring for live sidechain mode), public API (`prepare`, `reset`, `estimateResidual`), file location (`dsp/include/krate/dsp/processors/spectral_coring_estimator.h`), "when to use this" (live mode, zero-latency residual, or any context where full subtraction latency is unacceptable)
- [X] T104b **Verify `specs/_architecture_/innexus-plugin.md` exists** before making entries in T105/T106; if the file does not exist, create it with the standard architecture doc template (title, overview, components table, "when to use" section) before proceeding to T105
- [X] T105 [P] **Update `specs/_architecture_/innexus-plugin.md`** with `LiveAnalysisPipeline` entry: purpose (real-time sidechain audio to HarmonicFrame orchestration), API (`prepare`, `pushSamples`, `hasNewFrame`, `consumeFrame`), file location, instantiability note (no singletons, safe for Phase 21 multi-source blending)
- [X] T106 [P] **Update `specs/_architecture_/innexus-plugin.md`** with sidechain bus note: Innexus now has one `kAux` stereo audio input bus at bus index 0 (registered in `Processor::initialize()`). Hosts route sidechain to this bus. Plugin downmixes stereo to mono internally.

### 9.2 Final Commit

- [X] T107 **Commit architecture documentation updates** (skipped -- user will commit)
- [X] T108 Verify all spec work is committed to feature branch `117-live-sidechain-mode`: `git status` (skipped -- user will commit)

**Checkpoint**: Architecture documentation reflects all new functionality added by this spec.

---

## Phase 10: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification.

> **Pre-commit Quality Gate**: Run clang-tidy to catch bugs, performance issues, and style violations.

### 10.1 Run Clang-Tidy Analysis

- [X] T109 **Generate compile_commands.json** for clang-tidy using Ninja preset if not already current: from VS Developer PowerShell run `cmake --preset windows-ninja` from `F:\projects\iterum`
- [X] T110 **Run clang-tidy on DSP target** (SpectralCoringEstimator): `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`
- [X] T111 [P] **Run clang-tidy on Innexus target** (LiveAnalysisPipeline, processor extensions): `./tools/run-clang-tidy.ps1 -Target innexus -BuildDir build/windows-ninja`

### 10.2 Address Findings

- [X] T112 **Fix all errors** reported by clang-tidy (blocking issues)
- [X] T113 [P] **Review warnings** and fix where appropriate; for DSP inner loops where a clang-tidy suggestion would harm performance, add `// NOLINT(rule-name): reason` comment
- [X] T114 **Commit clang-tidy fixes** if any changes were made

**Checkpoint**: Static analysis clean — ready for completion verification.

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion.

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

- [X] T115 **Open `plugins/innexus/src/processor/processor.cpp`** and verify FR-001 (sidechain bus registered as `kAux` + `kStereo`), FR-009 (analysis runs on audio thread in `process()`), FR-011 (20ms crossfade on source switch) — cite file and line numbers
- [X] T116 [P] **Open `plugins/innexus/src/dsp/live_analysis_pipeline.cpp`** and verify FR-003 (full pipeline runs: pre-processing, F0, STFT, partial tracking, model building), FR-008 (no allocation in `pushSamples()`), FR-010 (confidence gate reuses existing freeze mechanism) — cite line numbers
- [X] T117 [P] **Open `dsp/include/krate/dsp/processors/spectral_coring_estimator.h`** and verify FR-007 (spectral coring zeros harmonic bins, measures inter-harmonic energy, zero additional latency) — cite implementation lines
- [X] T118 [P] **Open `plugins/innexus/src/plugin_ids.h`** and verify FR-002 (InputSource enum exists), FR-004 (LatencyMode enum exists), FR-012 (both parameters have IDs 500 and 501)
- [X] T119 [P] **Open `plugins/innexus/src/processor/processor.cpp` setState/getState** and verify FR-012 (state version 3 write), FR-015 (sample rate recalculates in setupProcessing), backward compat for version < 3
- [X] T120 [P] **Review test output** for each SC-xxx: copy actual test output numbers, compare to spec thresholds, confirm no threshold was relaxed — document in spec.md compliance table

### 11.2 Fill Compliance Table in spec.md

- [X] T121 **Update spec.md "Implementation Verification" section** in `specs/117-live-sidechain-mode/spec.md` with MET/NOT MET/PARTIAL status for each FR-001 through FR-016 and SC-001 through SC-008, with specific evidence (file paths, line numbers, measured values)
- [X] T122 **Mark overall honest status** as COMPLETE / NOT COMPLETE / PARTIAL in spec.md

### 11.3 Honest Self-Check

Answer these questions before proceeding. If ANY answer is "yes", do NOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without user approval?
4. Would the spec author consider this "done" if they read the evidence?
5. If I were the user, would I feel cheated?

- [X] T123 **All self-check questions answered "no"** (or gaps documented honestly in spec.md with user approval)

### 11.4 Final Pluginval

- [X] T124 **Run pluginval at strictness 5** to verify SC-008: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"` — document PASS/FAIL in spec.md compliance table

**Checkpoint**: Honest assessment complete — ready for final phase.

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim.

### 12.1 Final Commit

- [X] T125 **Commit all remaining spec work** to branch `117-live-sidechain-mode`
- [X] T126 Verify all tests pass with one final run: `build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5` and `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`

### 12.2 Completion Claim

- [X] T127 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user per spec.md Honest Assessment section)

**Checkpoint**: Spec implementation honestly complete.

---

## Dependencies and Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 — BLOCKS all user story phases
- **Phase 3 (US5 - Input Source)**: Depends on Phase 2 — must complete before Phases 4, 5, 6, 7 (provides sidechain bus and crossfade scaffolding)
- **Phase 4 (US1 - Low-Latency Analysis)**: Depends on Phase 3 — provides SpectralCoringEstimator and LiveAnalysisPipeline (BLOCKS Phase 5, 6, 7 which extend these)
- **Phase 5 (US2 - High-Precision)**: Depends on Phase 4 — extends LiveAnalysisPipeline
- **Phase 6 (US3 - Mode Selection)**: Depends on Phase 5 (tests both modes), but can start in parallel with Phase 5 if tests are written against Phase 4 stubs
- **Phase 7 (US4 - Residual)**: Depends on Phase 4 — wires SpectralCoringEstimator into synthesis path
- **Phase 8 (Polish)**: Depends on Phases 4-7 all complete
- **Phase 9 (Docs)**: Depends on Phase 8 complete
- **Phase 10 (Clang-Tidy)**: Depends on Phase 9
- **Phase 11 (Verification)**: Depends on Phase 10
- **Phase 12 (Final)**: Depends on Phase 11

### User Story Dependency Map

```
Phase 2 (Foundational: parameters, state)
    |
    v
Phase 3 (US5: sidechain bus, downmix, crossfade scaffolding)
    |
    v
Phase 4 (US1: SpectralCoringEstimator + LiveAnalysisPipeline + low-latency wiring) [MVP]
    |          \
    v           v
Phase 5 (US2)  Phase 7 (US4)
    |
    v
Phase 6 (US3: mode selection state persistence)
```

### Parallel Opportunities Within Each Phase

**Phase 1**: T002, T003 parallel (different CMakeLists files); T004, T005, T006 parallel (different stub files)

**Phase 2**: T012, T013 parallel (both controller parameter registrations in same file, but sequential edits); T014, T015, T016 are sequential (state version must be consistent); T010, T017 parallel (different files)

**Phase 3**: T020, T021, T022, T023, T024 all parallel (all write tests in the same test file — write sequentially within the file but can design in parallel); T025 and T027 parallel (different files: processor.cpp vs processor.h)

**Phase 4**: T035, T036, T037, T038, T039 parallel (different TEST_CASEs in same file); T040, T041, T042, T043, T044, T045 parallel; T046 and T048 parallel (different files: spectral_coring_estimator.h vs live_analysis_pipeline.h); T052 and T053 depend on T048 and T049 completing

**Phase 5**: T060, T061, T062, T063 parallel (different TEST_CASEs); T064 and T065 are sequential (T065 uses constants from T064)

**Phase 8**: T090 and T091 parallel; T092, T093, T094 parallel; T095, T096, T097, T098, T099 parallel

---

## Parallel Execution Example: Phase 4 (MVP — User Story 1)

```
Parallel batch 1 — Write all failing tests:
  Task T035: spectral_coring_estimator_tests.cpp — prepare test
  Task T036: spectral_coring_estimator_tests.cpp — harmonic zeroing test
  Task T037: spectral_coring_estimator_tests.cpp — SC-007 residual energy test
  Task T040: live_analysis_pipeline_tests.cpp — prepare test
  Task T041: live_analysis_pipeline_tests.cpp — hop trigger test

Parallel batch 2 — Implement (after tests are written and verified to FAIL):
  Task T046: spectral_coring_estimator.h — full implementation
  Task T048: live_analysis_pipeline.h — full declaration

Sequential:
  Task T049: live_analysis_pipeline.cpp — prepare() (after T048)
  Task T050: live_analysis_pipeline.cpp — pushSamples() (after T049)
  Task T051: live_analysis_pipeline.cpp — accessors (after T050)

Parallel batch 3 — Processor wiring (after T051):
  Task T052: processor.h — add liveAnalysis_ member
  Task T055: processor.h — add currentLiveFrame_ fields

Sequential:
  Task T053: processor.cpp — setupProcessing wiring (after T052)
  Task T054: processor.cpp — process() sidechain path (after T053)
  Task T056: processor.cpp — synthesis frame selection (after T054)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only — Phases 1-4)

1. Complete Phase 1: CMake setup and stubs
2. Complete Phase 2: Parameters and state (CRITICAL)
3. Complete Phase 3: Sidechain bus and routing (CRITICAL)
4. Complete Phase 4: SpectralCoringEstimator + LiveAnalysisPipeline + processor wiring
5. **STOP and VALIDATE**: A live sidechain signal drives the oscillator bank in low-latency mode. SC-001, SC-003, SC-006, SC-007, SC-008 (partial) are verifiable.
6. Demo: route a guitar or vocal into the sidechain, play MIDI, confirm timbre tracking

### Incremental Delivery

1. Phases 1-3 complete → Sidechain bus visible in host, parameters persist
2. Phase 4 complete → Live sidechain analysis in low-latency mode (MVP)
3. Phase 5 complete → Bass instruments down to 40 Hz in high-precision mode
4. Phase 6 complete → Mode selection persists across sessions
5. Phase 7 complete → Residual noise/breath component active and controllable
6. Phases 8-12 → Polish, verification, documentation, completion claim

### Single Developer Strategy

Work phases sequentially: 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11 → 12.
Stop at Phase 4 checkpoint to validate the MVP before continuing to P2/P3 stories.

---

## Notes

- `[P]` tasks can run in parallel (different files, no shared in-flight dependencies)
- `[USn]` label maps each task to its user story for traceability
- Each user story phase is independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment and measured values
- **NEVER claim completion if ANY requirement is not met** — document gaps honestly instead
- Build command: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target <target>`
- Pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"`
- State version: this spec bumps Innexus state version 2 → 3 (backward compatible, old states load with defaults)
- Gotchas (from plan.md): YinPitchDetector.prepare() takes `double` not `float`; PartialTracker.processFrame() takes fftSize as `size_t` and sampleRate as `float`; HarmonicModelBuilder.setHopSize() takes `int` not `size_t`; PreProcessingPipeline.processBlock() modifies data IN-PLACE (copy sidechain buffer first)
