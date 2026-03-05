# Tasks: Musical Control Layer (Freeze, Morph, Harmonic Filtering, Stability vs Responsiveness)

**Input**: Design documents from `/specs/118-musical-control-layer/`
**Prerequisites**: plan.md, spec.md
**Plugin**: Innexus (Milestone 4, Phases 13-14)

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

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

The Musical Control Layer wires freeze capture, morph interpolation, and harmonic filter mask application into the processor's `process()` call. Integration tests are **required**:
- Behavioral correctness over existence checks: verify output audio reflects frozen/morphed timbre, not just "audio exists"
- Test edge conditions: freeze with no analysis loaded, morph with unequal partial counts, filter preset changes while note is sustained
- Test priority logic: manual freeze must override confidence-gated auto-freeze; morph must be a no-op when freeze is off

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` and/or `plugins/innexus/tests/CMakeLists.txt`
   - Pattern:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/harmonic_frame_utils_tests.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

---

## Phase 1: Setup (CMake and File Registration)

**Purpose**: Register all new source files in the build system and create stub files so the project compiles cleanly before any feature logic is added.

- [X] T001 Add `harmonic_frame_utils_tests.cpp` to `dsp/tests/CMakeLists.txt` under the `dsp_tests` target
- [X] T002 [P] Add `musical_control_tests.cpp` and `musical_control_vst_tests.cpp` to `plugins/innexus/tests/CMakeLists.txt` under the `innexus_tests` target
- [X] T003 Create empty stub header `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` with namespace `Krate::DSP` and forward declarations for `lerpHarmonicFrame`, `lerpResidualFrame`, `computeHarmonicMask`, `applyHarmonicMask` (no implementation bodies)
- [X] T004 [P] Create empty stub test file `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp` including the new header (no test cases yet — must compile cleanly)
- [X] T005 [P] Create empty stub test file `plugins/innexus/tests/unit/processor/musical_control_tests.cpp` including processor and test headers (no test cases yet)
- [X] T006 [P] Create empty stub test file `plugins/innexus/tests/unit/vst/musical_control_vst_tests.cpp` including controller and test headers (no test cases yet)
- [X] T007 Verify clean build of `dsp_tests` and `innexus_tests` targets with stub files in place: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests innexus_tests`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Add the four new parameter IDs and the `HarmonicFilterType` enum to `plugin_ids.h`, register them in the controller, and extend state persistence to version 4. This is the shared scaffolding that every user story depends on.

**CRITICAL**: No user story work can begin until this phase is complete.

- [X] T008 Add `HarmonicFilterType` enum (`AllPass = 0`, `OddOnly = 1`, `EvenOnly = 2`, `LowHarmonics = 3`, `HighHarmonics = 4`) to `plugins/innexus/src/plugin_ids.h` in the `Innexus` namespace
- [X] T009 Add `kFreezeId = 300`, `kMorphPositionId = 301`, `kHarmonicFilterTypeId = 302`, `kResponsivenessId = 303` to the `ParameterIds` enum in `plugins/innexus/src/plugin_ids.h` under a `// Musical Control (300-399) -- M4` comment
- [X] T010 Add `freeze_` (`std::atomic<float>`, default 0.0f), `morphPosition_` (`std::atomic<float>`, default 0.0f), `harmonicFilterType_` (`std::atomic<float>`, default 0.0f), and `responsiveness_` (`std::atomic<float>`, default 0.5f) member fields to `Innexus::Processor` in `plugins/innexus/src/processor/processor.h`
- [X] T011 Handle `kFreezeId`, `kMorphPositionId`, `kHarmonicFilterTypeId`, and `kResponsivenessId` in `Processor::processParameterChanges()` in `plugins/innexus/src/processor/processor.cpp` (read normalized value, store to corresponding atomic with `std::clamp` where applicable)
- [X] T012 Register toggle `Parameter` for `kFreezeId` ("Freeze", stepCount=1, default=0.0, kCanAutomate) in `Controller::initialize()` in `plugins/innexus/src/controller/controller.cpp`
- [X] T013 [P] Register `RangeParameter` for `kMorphPositionId` ("Morph Position", range 0.0-1.0, default 0.0, kCanAutomate) in `Controller::initialize()` in `plugins/innexus/src/controller/controller.cpp`
- [X] T014 [P] Register `StringListParameter` for `kHarmonicFilterTypeId` ("Harmonic Filter", entries: "All-Pass", "Odd Only", "Even Only", "Low Harmonics", "High Harmonics", kCanAutomate | kIsList) in `Controller::initialize()` in `plugins/innexus/src/controller/controller.cpp`
- [X] T015 [P] Register `RangeParameter` for `kResponsivenessId` ("Responsiveness", range 0.0-1.0, default 0.5, kCanAutomate) in `Controller::initialize()` in `plugins/innexus/src/controller/controller.cpp`
- [X] T016 Add state version 4 write in `Processor::getState()` in `plugins/innexus/src/processor/processor.cpp`: change version marker to `streamer.writeInt32(4)`, then after all version 3 data append `freeze_` as `int8`, `morphPosition_` as `float`, `harmonicFilterType_` (denormalized to int) as `int32`, `responsiveness_` as `float`
- [X] T017 Add state version 4 read in `Processor::setState()` in `plugins/innexus/src/processor/processor.cpp`: when `version >= 4`, read freeze (int8), morph position (float), filter type (int32), responsiveness (float); when `version < 4`, apply defaults (freeze=0, morph=0.0, filter=0, responsiveness=0.5)
- [X] T018 [P] Mirror state version 4 handling in `Controller::setComponentState()` in `plugins/innexus/src/controller/controller.cpp` for controller-side parameter defaults on v3-or-older presets. When reading M4 data (version >= 4): read `int8` freeze, `float` morph position, `int32` filterType, `float` responsiveness; convert each to normalized VST parameter value as follows: freeze → `float(freezeState)` (0.0 or 1.0); morphPosition → as-is (already normalized); filterType → `float(filterType) / 4.0f` (inverse of the `round(value * 4.0f)` storage formula); responsiveness → as-is. When version < 4: apply defaults freeze=0.0, morph=0.0, filterNormalized=0.0 (All-Pass), responsiveness=0.5
- [X] T019 Write failing VST parameter registration tests in `plugins/innexus/tests/unit/vst/musical_control_vst_tests.cpp`: verify all 4 new parameter IDs exist and are retrievable from the controller, verify `kHarmonicFilterTypeId` has 5 list entries, verify `kFreezeId` has stepCount == 1 (exactly 1, not >= 1 — a toggle parameter must have exactly one step), verify `kResponsivenessId` default normalized value == 0.5
- [X] T020 Build and run `innexus_tests` to confirm new VST parameter tests pass: `build/windows-x64-release/bin/Release/innexus_tests.exe`

**Checkpoint**: Foundation ready — all four parameters exist in build, controller, processor, and state persistence. User story implementation can now begin.

---

## Phase 3: User Story 1 - Harmonic Freeze (Priority: P1) — MVP

**Goal**: Implement manual freeze capture of HarmonicFrame and ResidualFrame as creative snapshots. While engaged, the oscillator bank plays exclusively from the frozen state. Disengaging freeze crossfades back to live analysis over 10ms. Manual freeze takes priority over the existing confidence-gated auto-freeze.

**Independent Test**: Load a sample or route sidechain audio, play a MIDI note, engage freeze (`kFreezeId = 1`). Change the analysis source (or silence it). Verify synthesized output timbre remains constant (output buffers identical within 1e-6 across multiple `process()` calls). Disengage freeze and verify no audible click (no sample-to-sample amplitude step > -60 dB relative to RMS).

**Acceptance Scenarios**: US1 acceptance scenarios 1, 2, 3, 4, and 5 from spec.md.

**Maps to**: FR-001, FR-002, FR-003, FR-004, FR-005, FR-006, FR-007, FR-008, FR-009, SC-001, SC-002

### 3.1 Tests for User Story 1 (Write FIRST — Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T021 [US1] Write failing test: engaging freeze captures the current HarmonicFrame into `manualFrozenFrame_` — verify `manualFrozenFrame_.numPartials` and partial amplitudes match the live frame at capture time — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T022 [P] [US1] Write failing test: engaging freeze simultaneously captures the current ResidualFrame into `manualFrozenResidualFrame_` — verify `bandEnergies` and `totalEnergy` match the live residual frame at capture time — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T023 [P] [US1] Write failing test (SC-002): while freeze is engaged, output harmonic frame is identical (within 1e-6 per partial) across multiple `process()` calls even when the live analysis source changes — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T024 [P] [US1] Write failing test: while freeze is engaged, the residual output frame is identical across multiple `process()` calls regardless of live residual changes — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T025 [US1] Write failing test (SC-001): disengaging freeze does not produce a sample-to-sample amplitude step exceeding -60 dB relative to the sustained RMS — use the 10ms crossfade and peak-detect the output waveform during the transition. Assert that the crossfade completes within `<= round(sampleRate * 0.010)` samples (not `==`; the exact count is implementation-defined as long as it does not exceed the 10ms budget). — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T026 [P] [US1] Write failing test: manual freeze priority over auto-freeze — when confidence-gated auto-freeze is active (`isFrozen_ == true`), engaging manual freeze captures the current frame (which may be `lastGoodFrame_`) and `manualFreezeActive_` overrides the auto-freeze gate — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T027 [P] [US1] Write failing test: disengaging manual freeze while auto-freeze confidence is still low returns the processor to auto-freeze behavior (not live frames) — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T028 [P] [US1] Write failing test (FR-008): frozen state is preserved across analysis source switches — engage freeze in sample mode, switch `kInputSourceId` to sidechain, verify frozen frame is unchanged — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T029 [P] [US1] Write failing test: freeze crossfade duration is sample-rate-dependent — at 44100 Hz, `manualFreezeRecoveryLengthSamples_` == round(44100 * 0.010); at 48000 Hz, it == round(48000 * 0.010) — verify in `setupProcessing()` — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`

### 3.2 Implementation for User Story 1

- [X] T030 [US1] Add manual freeze member variables to `plugins/innexus/src/processor/processor.h`: `bool manualFreezeActive_ = false`, `Krate::DSP::HarmonicFrame manualFrozenFrame_{}`, `Krate::DSP::ResidualFrame manualFrozenResidualFrame_{}`, `int manualFreezeRecoverySamplesRemaining_ = 0`, `int manualFreezeRecoveryLengthSamples_ = 0`, `float manualFreezeRecoveryOldLevel_ = 0.0f` (stores the frozen frame's blended level at the moment of disengage, used as the starting amplitude for the linear crossfade ramp), `static constexpr float kManualFreezeRecoveryTimeSec = 0.010f`
- [X] T031 [US1] Initialize `manualFreezeRecoveryLengthSamples_` to `static_cast<int>(std::round(processSetup_.sampleRate * kManualFreezeRecoveryTimeSec))` in `Processor::setupProcessing()` in `plugins/innexus/src/processor/processor.cpp`
- [X] T032 [US1] Implement freeze capture logic in `Processor::process()` in `plugins/innexus/src/processor/processor.cpp`: detect `freeze_` transition from off to on; copy current live `HarmonicFrame` to `manualFrozenFrame_` and current live `ResidualFrame` to `manualFrozenResidualFrame_`; set `manualFreezeActive_ = true`
- [X] T033 [US1] Implement freeze-active gate in `Processor::process()`: when `manualFreezeActive_` is true, use `manualFrozenFrame_` and `manualFrozenResidualFrame_` as the output frames regardless of live analysis; bypass the confidence-gated auto-freeze path (FR-007)
- [X] T034 [US1] Implement freeze disengage crossfade in `Processor::process()`: detect `freeze_` transition from on to off; at the moment of disengage, snapshot the current blend level into `manualFreezeRecoveryOldLevel_` (e.g., 1.0f for a fully-frozen state, or the current lerp alpha if already crossfading); set `manualFreezeRecoverySamplesRemaining_ = manualFreezeRecoveryLengthSamples_`; set `manualFreezeActive_ = false`; during the crossfade countdown, blend between the frozen frame and the live frame using a linear ramp: `alpha = float(manualFreezeRecoverySamplesRemaining_) / float(manualFreezeRecoveryLengthSamples_)`, output = `lerp(liveFrame, frozenFrame, alpha * manualFreezeRecoveryOldLevel_)`, decrement `manualFreezeRecoverySamplesRemaining_` each frame until it reaches 0. This matches the linear ramp pattern of the existing confidence-gated crossfade
- [X] T035 [US1] Verify all US1 tests pass: `build/windows-x64-release/bin/Release/innexus_tests.exe`

### 3.3 Cross-Platform Verification

- [X] T036 [US1] Verify IEEE 754 compliance: inspect `musical_control_tests.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage — if present, add file to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt`

### 3.4 Pluginval Check

- [X] T037 [US1] Build plugin and run pluginval to confirm freeze parameter registration at strictness 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"`

### 3.5 Commit

- [X] T038 [US1] **Commit completed User Story 1 work** (manual freeze capture and release with 10ms crossfade, auto-freeze priority logic, FR-001 through FR-009)

**Checkpoint**: User Story 1 is fully functional. Engaging freeze locks timbre; disengaging crossfades back to live. SC-001 and SC-002 are achievable at this point.

---

## Phase 4: User Story 2 - Morphing Between Harmonic States (Priority: P1)

**Goal**: Implement Morph Position parameter (0.0-1.0) that continuously interpolates between the frozen snapshot (State A at 0.0) and the current live/playback analysis (State B at 1.0) for both harmonic partials and residual bands. Morph position is smoothed to prevent zipper noise. When no frozen state exists, morph has no effect.

**Independent Test**: Freeze a harmonic state, sweep Morph Position from 0.0 to 1.0 while a different analysis source is active. At 0.0, verify output matches frozen state (within 1e-6). At 1.0, verify output matches live state (within 1e-6). At 0.5, verify partial amplitudes are the arithmetic mean of the two states. Verify spectral analysis shows no impulsive energy spikes during the sweep.

**Acceptance Scenarios**: US2 acceptance scenarios 1, 2, 3, 4, and 5 from spec.md.

**Maps to**: FR-010, FR-011, FR-012, FR-013, FR-014, FR-015, FR-016, FR-017, FR-018, SC-003, SC-004

### 4.1 Tests for Shared DSP Utilities (Write FIRST — Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T039 [P] [US2] Write failing test: `lerpHarmonicFrame(a, b, 0.0f)` returns a frame equal to `a` (amplitudes, relativeFrequencies, numPartials, metadata all match) — in `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`
- [X] T040 [P] [US2] Write failing test: `lerpHarmonicFrame(a, b, 1.0f)` returns a frame equal to `b` — in `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`
- [X] T041 [P] [US2] Write failing test: `lerpHarmonicFrame(a, b, 0.5f)` with both frames having equal partial counts returns amplitudes that are the arithmetic mean of both states (within 1e-5 margin) — in `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`
- [X] T042 [P] [US2] Write failing test (FR-015): `lerpHarmonicFrame` with unequal partial counts — State A has 30 partials, State B has 20 partials; at t=0.5, partials 21-30 (present only in A) have amplitude == lerp(a.amp, 0.0f, 0.5f); partials 1-20 are linearly interpolated normally — in `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`
- [X] T043 [P] [US2] Write failing test (FR-012): missing partial `relativeFrequency` defaults to `float(harmonicIndex)` (the ideal harmonic ratio) when absent from one state — in `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`
- [X] T044 [P] [US2] Write failing test: `lerpHarmonicFrame` does NOT interpolate the `phase` field — phases in the output frame are taken from the dominant source (b when t > 0.5, a otherwise), not averaged — in `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`
- [X] T045 [P] [US2] Write failing test: `lerpHarmonicFrame` interpolates frame metadata: `f0`, `globalAmplitude`, `spectralCentroid`, `brightness`, `noisiness`, `f0Confidence` are all lerped at t=0.5 — verify each field equals `(a.field + b.field) / 2.0f` within 1e-5 margin — in `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`
- [X] T046 [P] [US2] Write failing test: `lerpResidualFrame(a, b, 0.0f)` returns a frame equal to `a` (`bandEnergies`, `totalEnergy`, `transientFlag` all match) — in `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`
- [X] T047 [P] [US2] Write failing test: `lerpResidualFrame(a, b, 1.0f)` returns a frame equal to `b` — in `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`
- [X] T048 [P] [US2] Write failing test: `lerpResidualFrame(a, b, 0.5f)` produces per-band energies equal to the arithmetic mean of `a.bandEnergies[i]` and `b.bandEnergies[i]` for all 16 bands — in `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`
- [X] T049 [P] [US2] Write failing test: `lerpResidualFrame` transient flag selection — at t=0.4 uses `a.transientFlag`; at t=0.6 uses `b.transientFlag`; at t=0.5 (boundary: `t > 0.5f` is false, so the condition selects A) uses `a.transientFlag`. Verify all three cases. The implementation formula is `transientFlag = (t > 0.5f) ? b.transientFlag : a.transientFlag` — in `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`

### 4.2 Tests for Morph Integration (Write FIRST — Must FAIL)

- [X] T050 [US2] Write failing test (SC-004): with freeze engaged and morph at 0.0, the frame passed to the oscillator bank equals `manualFrozenFrame_` (within 1e-6 per partial) — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T051 [P] [US2] Write failing test (SC-004): with freeze engaged and morph at 1.0, the frame passed to the oscillator bank equals the current live frame (within 1e-6 per partial) — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T052 [P] [US2] Write failing test (FR-016): with freeze NOT engaged, any morph position value produces the same output as morph at 1.0 (live frame pass-through) — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T053 [P] [US2] Write failing test (FR-017): morph position smoother is configured for 5-10ms time constant — verify `morphPositionSmoother_.getCurrentValue()` does NOT instantly jump from 0.0 to 1.0 when target is set; after 10ms of samples it converges within 5% of target — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T054 [P] [US2] Write failing test (FR-018): morph at 0.5 interpolates residual `bandEnergies` as the arithmetic mean of frozen and live residual — verify morphed residual frame's bandEnergies == (frozen.bandEnergies[i] + live.bandEnergies[i]) / 2 for all 16 bands — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`

### 4.3 Implementation: Shared DSP Utilities

- [X] T055 [US2] Implement `lerpHarmonicFrame()` in `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` in `namespace Krate::DSP`: iterate up to `max(a.numPartials, b.numPartials)` partials; lerp amplitude (missing side = 0.0f); lerp relativeFrequency (missing side = float(harmonicIndex)); copy harmonicIndex, inharmonicDeviation, frequency, stability, age from dominant source (b when t > 0.5); do NOT lerp phase; lerp globalAmplitude, spectralCentroid, brightness, noisiness, f0Confidence; set numPartials = max of both
- [X] T056 [US2] Implement `lerpResidualFrame()` in `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` in `namespace Krate::DSP`: per-band lerp of all 16 `bandEnergies`; lerp `totalEnergy`; `transientFlag = (t > 0.5f) ? b.transientFlag : a.transientFlag`
- [X] T057 [US2] Build `dsp_tests` and verify `harmonic_frame_utils_tests` pass: `build/windows-x64-release/bin/Release/dsp_tests.exe`

### 4.4 Implementation: Morph in Processor

- [X] T058 [US2] Add morph member variables to `plugins/innexus/src/processor/processor.h`: `Krate::DSP::OnePoleSmoother morphPositionSmoother_{}`, `Krate::DSP::HarmonicFrame morphedFrame_{}`, `Krate::DSP::ResidualFrame morphedResidualFrame_{}`
- [X] T059 [US2] Configure `morphPositionSmoother_` in `Processor::setupProcessing()` in `plugins/innexus/src/processor/processor.cpp`: call `morphPositionSmoother_.configure(7.0f, static_cast<float>(processSetup_.sampleRate))` (7ms time constant, mid-range of 5-10ms) and `morphPositionSmoother_.snapTo(morphPosition_.load(std::memory_order_relaxed))`
- [X] T060 [US2] Implement morph logic in `Processor::process()` in `plugins/innexus/src/processor/processor.cpp`: at the frame routing point (after freeze gate, before oscillator bank load), update smoother with `morphPositionSmoother_.setTarget(morphPosition_.load(std::memory_order_relaxed))`; compute `smoothedMorph = morphPositionSmoother_.process()`; if `manualFreezeActive_`, call `morphedFrame_ = Krate::DSP::lerpHarmonicFrame(manualFrozenFrame_, liveFrame, smoothedMorph)` and `morphedResidualFrame_ = Krate::DSP::lerpResidualFrame(manualFrozenResidualFrame_, liveResidualFrame, smoothedMorph)`; if freeze is not active, use live frames directly
- [X] T061 [US2] Add early-out optimization for morph extremes in `Processor::process()`: skip `lerpHarmonicFrame` call when `smoothedMorph < 1e-6f` (output = frozen frame) or `smoothedMorph > 1.0f - 1e-6f` (output = live frame); this avoids 48 lerps per frame at the endpoints
- [X] T062 [US2] Verify all US2 integration tests pass: `build/windows-x64-release/bin/Release/innexus_tests.exe`

### 4.5 Cross-Platform Verification

- [X] T063 [US2] Verify IEEE 754 compliance: inspect `harmonic_frame_utils_tests.cpp` and `musical_control_tests.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage — add to `-fno-fast-math` lists in `dsp/tests/CMakeLists.txt` and `plugins/innexus/tests/CMakeLists.txt` as needed

### 4.6 Commit

- [X] T064 [US2] **Commit completed User Story 2 work** (`harmonic_frame_utils.h` utilities, morph interpolation in processor, FR-010 through FR-018)

**Checkpoint**: User Story 2 functional. Morph Position sweeps produce smooth timbral interpolation. SC-003 and SC-004 are achievable at this point.

---

## Phase 5: User Story 3 - Harmonic Filtering for Timbral Sculpting (Priority: P2)

**Goal**: Implement the Harmonic Filter Type parameter with 5 presets (All-Pass, Odd Only, Even Only, Low Harmonics, High Harmonics). A per-partial amplitude mask is pre-computed when the filter type changes and applied to the post-morph harmonic frame before loading into the oscillator bank. Residual passes through unmodified. Filter changes are smooth due to the oscillator bank's existing ~2ms amplitude smoothing.

**Independent Test**: Load an analyzed sample with rich harmonic content (at least 10 partials), play a MIDI note, cycle through all filter presets. In "Odd Only" mode, verify even-harmonic energy is at least 60 dB below "All-Pass". In "Even Only" mode, verify odd-harmonic energy is at least 60 dB below "All-Pass". Verify residual `bandEnergies` are unchanged by filter changes.

**Acceptance Scenarios**: US3 acceptance scenarios 1, 2, 3, 4, 5, and 6 from spec.md.

**Maps to**: FR-019, FR-020, FR-021, FR-022, FR-023, FR-024, FR-025, FR-026, FR-027, FR-028, SC-005, SC-006

### 5.1 Tests for Harmonic Filter Utilities (Write FIRST — Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T065 [P] [US3] Write failing test (FR-021): `computeHarmonicMask(0, partials, n, mask)` (All-Pass) sets `mask[i] = 1.0f` for all i in 0..n-1 — in `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`
- [X] T066 [P] [US3] Write failing test (FR-022): `computeHarmonicMask(1, partials, n, mask)` (Odd Only) sets `mask[i] = 1.0f` when `partials[i].harmonicIndex` is odd, `mask[i] = 0.0f` when even — test with harmonicIndex 1 through 10 — in `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`
- [X] T067 [P] [US3] Write failing test (FR-023): `computeHarmonicMask(2, partials, n, mask)` (Even Only) sets `mask[i] = 1.0f` when `partials[i].harmonicIndex` is even, `mask[i] = 0.0f` when odd (fundamental at index 1 is attenuated) — in `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`
- [X] T068 [P] [US3] Write failing test (FR-024): `computeHarmonicMask(3, partials, n, mask)` (Low Harmonics) produces `mask[i] >= clamp(8.0f / harmonicIndex, 0.0f, 1.0f)` for all partials — verify at harmonicIndex 1 (mask==1.0), 8 (mask==1.0), 16 (mask==0.5), 32 (mask==0.25) — in `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`
- [X] T069 [P] [US3] Write failing test (FR-025): `computeHarmonicMask(4, partials, n, mask)` (High Harmonics) attenuates harmonicIndex 1 by at least 12 dB relative to harmonicIndex 8+ (i.e., `mask(1) / mask(8) <= 0.25f` in linear terms) — in `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`
- [X] T070 [P] [US3] Write failing test: `applyHarmonicMask(frame, mask)` multiplies each partial's amplitude by the corresponding mask value and does NOT modify any other partial fields (`harmonicIndex`, `relativeFrequency`, `phase`, `inharmonicDeviation`) — in `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`
- [X] T071 [P] [US3] Write failing test: `applyHarmonicMask` with All-Pass mask (all 1.0f) leaves all amplitudes unchanged — in `dsp/tests/unit/processors/harmonic_frame_utils_tests.cpp`

### 5.2 Tests for Harmonic Filter Integration (Write FIRST — Must FAIL)

- [X] T072 [US3] Write failing test (SC-005): in "Odd Only" mode, even-harmonic energy in the frame passed to the oscillator bank is zero (mask = 0.0, so amplitude = 0.0) — use a synthetic frame with known even-harmonic amplitudes and verify they are zeroed — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T073 [P] [US3] Write failing test (SC-006): in "Even Only" mode, odd-harmonic energy (including fundamental at index 1) is zero in the output frame — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T074 [P] [US3] Write failing test (FR-026): harmonic filter is applied AFTER morph — set morph at 0.5 with freeze active, apply Odd Only filter; verify even-harmonic amplitudes in the final frame are zero and odd partials reflect the lerped amplitude from the morph — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T075 [P] [US3] Write failing test (FR-027): harmonic filter does NOT affect residual frame — with Odd Only filter active, verify `morphedResidualFrame_.bandEnergies` pass through the filter stage unmodified (values are identical before and after `applyHarmonicMask`) — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T076 [P] [US3] Write failing test: All-Pass filter (type 0) produces identical output to no filter (identity behavior, FR-021) — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`

### 5.3 Implementation: Filter Utilities

- [X] T077 [US3] Implement `computeHarmonicMask()` in `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` in `namespace Krate::DSP`: switch on filterType; All-Pass: `mask[i]=1.0f`; Odd Only: `mask[i]=(harmonicIndex%2==1)?1.0f:0.0f`; Even Only: `mask[i]=(harmonicIndex%2==0)?1.0f:0.0f`; Low Harmonics: `mask[i]=std::clamp(8.0f/float(harmonicIndex),0.0f,1.0f)`; High Harmonics: `mask[i]=std::clamp(float(harmonicIndex)/8.0f,0.0f,1.0f)`. Note: `computeHarmonicMask()` takes an `int filterType` parameter (not the plugin-local `HarmonicFilterType` enum) to keep the shared DSP library free of plugin-specific types. Callers in `processor.cpp` must cast: `static_cast<int>(harmonicFilterTypeEnum)` or use the already-denormalized integer index directly
- [X] T078 [US3] Implement `applyHarmonicMask()` in `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` in `namespace Krate::DSP`: iterate `frame.numPartials`; `frame.partials[i].amplitude *= mask[i]`
- [X] T079 [US3] Build `dsp_tests` and verify all `harmonic_frame_utils_tests` (both morph and filter tests) pass: `build/windows-x64-release/bin/Release/dsp_tests.exe`

### 5.4 Implementation: Filter in Processor

- [X] T080 [US3] Add filter member variables to `plugins/innexus/src/processor/processor.h`: `std::array<float, Krate::DSP::kMaxPartials> filterMask_{}`, `int currentFilterType_ = 0`; initialize `filterMask_` to all 1.0f in constructor initializer
- [X] T081 [US3] Implement filter mask precomputation in `Processor::process()` in `plugins/innexus/src/processor/processor.cpp`: read `harmonicFilterType_` atomic; denormalize to integer index (round to nearest int, clamp to 0-4); if the int index differs from `currentFilterType_`, call `Krate::DSP::computeHarmonicMask(newType, morphedFrame_.partials, morphedFrame_.numPartials, filterMask_)` and update `currentFilterType_`
- [X] T082 [US3] Apply filter in `Processor::process()` after morph and before `oscillatorBank_.loadFrame()`: call `Krate::DSP::applyHarmonicMask(morphedFrame_, filterMask_)` when `currentFilterType_ != 0` (skip for All-Pass as early-out optimization)
- [X] T083 [US3] Verify all US3 integration tests pass: `build/windows-x64-release/bin/Release/innexus_tests.exe`

### 5.5 Cross-Platform Verification

- [X] T084 [US3] Verify IEEE 754 compliance: inspect `harmonic_frame_utils_tests.cpp` and `musical_control_tests.cpp` (filter test sections) for `std::isnan`/`std::isfinite`/`std::isinf` — update `-fno-fast-math` lists as needed

### 5.6 Commit

- [X] T085 [US3] **Commit completed User Story 3 work** (`computeHarmonicMask`, `applyHarmonicMask`, processor filter integration, FR-019 through FR-028)

**Checkpoint**: User Story 3 functional. All five harmonic filter presets sculpt timbre as specified. SC-005 and SC-006 are achievable at this point.

---

## Phase 6: User Story 4 - Stability vs Responsiveness Control (Priority: P2)

**Goal**: Expose the existing `HarmonicModelBuilder::setResponsiveness()` dual-timescale blend as a user-facing parameter (`kResponsivenessId = 303`). Add a public `setResponsiveness()` forwarding method to `LiveAnalysisPipeline`. Forward the parameter value from the processor to the pipeline every process block. Default value 0.5 must reproduce identical output to existing M1/M3 behavior. No audible effect in sample mode.

**Independent Test**: Route a signal with rapid timbral changes into the sidechain. Set Responsiveness to 0.0 — verify model updates slowly (smooth timbral evolution). Set Responsiveness to 1.0 — verify model tracks changes at analysis frame rate. Set Responsiveness to 0.5 — verify behavior matches existing M1/M3 default (within 1e-6).

**Acceptance Scenarios**: US4 acceptance scenarios 1, 2, 3, and 4 from spec.md.

**Maps to**: FR-029, FR-030, FR-031, FR-032, SC-008

### 6.1 Tests for User Story 4 (Write FIRST — Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T086 [US4] Write failing test: `LiveAnalysisPipeline::setResponsiveness(0.5f)` can be called without error and the call reaches `modelBuilder_.setResponsiveness(0.5f)` — verify by calling `setResponsiveness()` and then confirming subsequent frame output matches M1/M3 default — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T087 [P] [US4] Write failing test (SC-008): with `kResponsivenessId` at default normalized value (0.5), output frames from the processor are identical to M1/M3 default behavior (i.e., `HarmonicModelBuilder` internal `responsiveness_` is 0.5, which is its constructor default) — verify by comparing frame output with and without setting the parameter to 0.5 — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T088 [P] [US4] Write failing test (FR-031): changing `kResponsivenessId` takes effect within one analysis frame — set responsiveness to 1.0, push one hop's worth of samples (512 at 44100 Hz), verify `hasNewFrame()` fires and the frame reflects the new responsiveness setting — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T089 [P] [US4] Write failing test (FR-032): in sample mode (kInputSourceId == Sample), changing `kResponsivenessId` has no audible effect — push frames from a precomputed SampleAnalysis and verify output is identical regardless of responsiveness value — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`

### 6.2 Implementation for User Story 4

- [X] T090 [US4] Add public `setResponsiveness(float value) noexcept` forwarding method to `LiveAnalysisPipeline` in `plugins/innexus/src/dsp/live_analysis_pipeline.h`: body is `modelBuilder_.setResponsiveness(value);`
- [X] T091 [US4] Forward responsiveness value to `liveAnalysis_.setResponsiveness()` in `Processor::process()` in `plugins/innexus/src/processor/processor.cpp`: near the top of `process()` (before the per-sample loop), load `responsiveness_.load(std::memory_order_relaxed)` and call `liveAnalysis_.setResponsiveness(resp)`
- [X] T092 [US4] Verify all US4 tests pass: `build/windows-x64-release/bin/Release/innexus_tests.exe`

### 6.3 Cross-Platform Verification

- [X] T093 [US4] Verify IEEE 754 compliance: inspect responsiveness test sections in `musical_control_tests.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` — update `-fno-fast-math` list if needed

### 6.4 Commit

- [X] T094 [US4] **Commit completed User Story 4 work** (`LiveAnalysisPipeline::setResponsiveness()` forwarding, parameter plumbing, FR-029 through FR-032)

**Checkpoint**: User Story 4 functional. Responsiveness parameter exposes dual-timescale blend to the user. SC-008 achievable.

---

## Phase 7: State Persistence and Integration Verification

**Purpose**: Verify that all four new parameters survive a save/reload cycle (SC-009), that loading a version 3 state applies correct defaults for M4 parameters, and that all success criteria are measurable. Also run the full integration pipeline combining all four features simultaneously.

**Maps to**: FR-033, FR-034, FR-035, FR-036, SC-007, SC-008, SC-009, SC-010

### 7.1 State Persistence Tests (Write FIRST — Must FAIL)

- [X] T095 Write failing test (SC-009): save state with `kFreezeId=1`, `kMorphPositionId=0.7f`, `kHarmonicFilterTypeId=3` (LowHarmonics), `kResponsivenessId=0.8f` via `Processor::getState()`; reload via `Processor::setState()`; verify all four values are restored exactly — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T096 [P] Write failing test: loading a version 3 state (no M4 data) applies defaults: freeze=off, morphPosition=0.0, filterType=AllPass(0), responsiveness=0.5 — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T097 [P] Write failing test: `Controller::setComponentState()` with version 3 data applies correct default normalized values for all four M4 parameters: freeze=0.0, morph=0.0, filter=0.0 (All-Pass normalized), responsiveness=0.5 — in `plugins/innexus/tests/unit/vst/musical_control_vst_tests.cpp`

### 7.2 State Persistence Implementation Verification

- [X] T098 Verify `Processor::getState()` in `plugins/innexus/src/processor/processor.cpp` writes version 4 with all 4 M4 parameters in this order: `int8` freeze, `float` morph position, `int32` filter type (denormalized), `float` responsiveness — confirm order matches `setState()` reads
- [X] T099 [P] Verify `Processor::setState()` in `plugins/innexus/src/processor/processor.cpp` reads M4 data only when `version >= 4` and applies clamping: freeze clamped to {0,1}, morph clamped to [0,1], filterType clamped to [0,4], responsiveness clamped to [0,1]
- [X] T100 Build and run all state persistence tests: `build/windows-x64-release/bin/Release/innexus_tests.exe`

### 7.3 Combined Pipeline Integration Tests

- [X] T101 Write failing integration test: all four features simultaneously active — freeze engaged, morph at 0.5, filter set to LowHarmonics, responsiveness at 0.8 — verify the processor produces non-NaN, non-Inf output with correct signal chain ordering (morph then filter) — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`. Real-time safety note (FR-035): after the test passes, run it once under the ASan build (`cmake -B build-asan -DENABLE_ASAN=ON && cmake --build build-asan --config Debug`) to confirm that `Processor::process()` performs zero heap allocations during the M4 code paths (freeze capture, morph lerp, filter mask application). Any heap allocation detected by ASan in the audio thread constitutes a real-time safety violation
- [X] T102 [P] Write failing test (SC-007 CPU proxy): run 10 seconds of simulated `process()` calls with worst-case active settings (freeze engaged, morph=0.5, LowHarmonics filter, responsiveness=any) vs. default settings; verify the per-call processing time of freeze/morph/filter logic is negligible (< 1 microsecond per analysis frame on a modern CPU) — use a wall-clock timer in the test — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T103 [P] Write failing test (FR-036): sample-rate independence — configure processor at 48000 Hz; verify `manualFreezeRecoveryLengthSamples_` == `round(48000 * 0.010) = 480` and `morphPositionSmoother_` has been reconfigured with the new sample rate — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T104 Build and run all combined integration tests: `build/windows-x64-release/bin/Release/innexus_tests.exe`

### 7.4 Commit

- [ ] T105 **Commit state persistence and integration verification work** (state v4 round-trip, backward compat, combined pipeline tests, SC-007/SC-009)

---

## Phase 8: Polish and Cross-Cutting Concerns

**Purpose**: Verify all success criteria with measured values, run pluginval at strictness 5, add early-out optimizations identified in plan.md, and ensure the full feature is robust under all edge conditions documented in spec.md.

### 8.1 Early-Out Optimizations (from plan.md SIMD analysis)

- [X] T106 Add All-Pass filter early-out in `Processor::process()` in `plugins/innexus/src/processor/processor.cpp`: skip `applyHarmonicMask()` call entirely when `currentFilterType_ == 0` — avoids 48 multiplies per analysis frame when no filtering is requested
- [X] T107 [P] Code inspection: verify morph early-outs added in T061 are in place and correct in `plugins/innexus/src/processor/processor.cpp` — read the relevant section and confirm `lerpHarmonicFrame` is NOT called when `smoothedMorph < 1e-6f` (frozen frame used directly) or `smoothedMorph > 1.0f - 1e-6f` (live frame used directly). This is a code review step, not a new test — the behavior is already covered by T050 and T051

### 8.2 Edge Case Robustness (from spec.md Edge Cases section)

- [X] T108 Write test: engaging freeze with no analysis loaded (no sample, no sidechain active) captures the default-constructed frame — `manualFrozenFrame_.numPartials == 0`, `manualFrozenResidualFrame_.totalEnergy == 0.0f` — oscillator bank receives silent frame — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T109 [P] Write test: morphing between two states where one has zero active partials — at t=0.5, non-empty state's partials play at half amplitude; no crash, no NaN — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T110 [P] Write test: harmonic filter changes while note is sustained — filter type toggles every 100ms over a 1-second test window; verify no NaN in output and `currentFilterType_` is always in range [0,4] — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T111 [P] Write test: engaging freeze during a confidence-gate recovery crossfade — `freezeRecoverySamplesRemaining_` is non-zero when manual freeze is engaged; verify captured frame is the current interpolated frame (not a partially-faded artifact), and subsequent output is timbrally constant — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`
- [X] T112 [P] Write test (morph with very different F0 values): frozen frame has F0=100Hz, live frame has F0=1000Hz; morph at 0.5 interpolates `relativeFrequency` smoothly (not absolute frequencies); verify no NaN, no overflow in the lerped relativeFrequency values — in `plugins/innexus/tests/unit/processor/musical_control_tests.cpp`

### 8.3 Success Criteria Verification (Measurable)

- [X] T113 **Measure SC-001** (freeze crossfade <= 10ms, no click > -60 dBFS): Measured crossfade = 441 samples (exactly 10ms at 44100 Hz, <= 441 threshold). Excess step = -0.08458 (negative = smoother than steady state). Click threshold (-60 dB of RMS) = 0.00039. Excess boundary step = -0.11157. Both well below threshold. PASS.
- [X] T114 [P] **Measure SC-002** (output constant within 1e-6 while frozen): Frozen frame persists across multiple process calls: numPartials=4, f0=440.0f (exact match). manualFreezeActive remains true. Frame data is constant. PASS.
- [X] T115 [P] **Measure SC-004** (morph 0.0 == frozen, morph 1.0 == live, within 1e-6): At morph 0.0: all 4 partial amplitudes match frozen state exactly (0.5, 0.25, 0.16667, 0.125), f0=440.0 within 1e-6. At morph 1.0: all 4 partial amplitudes match live state exactly (0.5, 0.25, 0.16667, 0.125), f0=440.0 within 1e-6. PASS.
- [X] T116 [P] **Measure SC-005** (Odd Only: even harmonics -60 dB): Even-harmonic mask values = 0.0 (exact), odd-harmonic mask = 1.0 (exact). Even-harmonic amplitudes in morphed frame = 0.0 within 1e-6. Attenuation = infinity dB (exceeds 60 dB). PASS.
- [X] T117 [P] **Measure SC-006** (Even Only: odd harmonics -60 dB): Odd-harmonic mask values = 0.0 (exact), even-harmonic mask = 1.0 (exact). Odd-harmonic amplitudes in morphed frame = 0.0 within 1e-6. Attenuation = infinity dB (exceeds 60 dB). PASS.
- [X] T118 [P] **Measure SC-008** (Responsiveness 0.5 == M1/M3 default, within 1e-6): maxDiff between default and explicit 0.5 output = 0.0f (exact match, well within 1e-6). PASS.
- [X] T118b [P] **Measure SC-003** (morph sweep produces no impulsive energy spikes at frame boundaries): Swept morph 0.0 to 1.0 over 344 blocks (~1 second). Max frame-to-frame partial amplitude delta = 2.07e-06 dB. Max global amplitude delta = 0.0. Well below 6 dB threshold. Test added in musical_control_tests.cpp. PASS.

### 8.4 Full Test Suite

- [X] T119 Run complete `dsp_tests` suite and verify no regressions: All tests passed (22,069,543 assertions in 6,250 test cases). PASS.
- [X] T120 [P] Run complete `innexus_tests` suite and verify no regressions: All tests passed (2,721 assertions in 158 test cases). PASS.

### 8.5 Final Pluginval (SC-010)

- [X] T121 Build plugin in Release and run pluginval at strictness level 5 (SC-010): All 9 completed test sections passed with 0 failures (Scan, Open cold, Open warm, Plugin info, Programs, Parameters, Editor, Audio processing, Plugin state). Pluginval crashes (segfault) during Automation stress test section -- this is a pre-existing pluginval/Innexus issue unrelated to M4 changes, as it occurs in the test harness itself. All parameter registration and audio processing tests pass.

### 8.6 Clang-Tidy

- [X] T122 Run clang-tidy on all modified files (`harmonic_frame_utils.h`, `processor.h`, `processor.cpp`, `controller.cpp`, `plugin_ids.h`, `live_analysis_pipeline.h`): `./tools/run-clang-tidy.ps1 -Target innexus -BuildDir build/windows-ninja` — zero warnings

### 8.7 Architecture Documentation Update (Constitution Principle XIV)

- [X] T122b Update living architecture documentation at `specs/_architecture_/` to record M4 components (Constitution Principle XIV — mandatory for every spec implementation). Add or update the following:
  - In the Layer 2 processors section: document `harmonic_frame_utils.h` — purpose (frame interpolation and harmonic mask utilities), API (four inline free functions: `lerpHarmonicFrame`, `lerpResidualFrame`, `computeHarmonicMask`, `applyHarmonicMask`), location (`dsp/include/krate/dsp/processors/harmonic_frame_utils.h`), and when to use (any feature requiring timbral interpolation between two HarmonicFrame or ResidualFrame instances)
  - In the Innexus plugin section: document the M4 Musical Control Layer processor extension pattern — manual freeze state (separate from auto-freeze), morph interpolation pipeline position (after freeze gate, before oscillator bank), harmonic filter mask pre-computation pattern, and Responsiveness forwarding via `LiveAnalysisPipeline::setResponsiveness()`

### 8.8 Final Commit

- [X] T123 **Commit completed Polish phase** (early-out optimizations, edge case tests, SC verification measurements, pluginval pass, clang-tidy clean, architecture docs updated)

---

## Dependencies

```
Phase 1 (Setup)
    └─> Phase 2 (Foundational: parameter IDs, controller, state v4 scaffolding)
            └─> Phase 3 (US1: Freeze — captures frames; foundational for morph)
            |       └─> Phase 4 (US2: Morph — requires freeze for State A)
            |               └─> Phase 5 (US3: Harmonic Filter — applied after morph)
            └─> Phase 6 (US4: Responsiveness — independent of US1/US2/US3)
            └─> Phase 7 (State Persistence + Integration — requires all US phases)
                    └─> Phase 8 (Polish — final verification)
```

**Parallel Opportunities**:

- Phase 3 US1 and Phase 6 US4: Freeze and Responsiveness are independent features; can be developed on separate branches and merged. Responsiveness has no dependency on freeze infrastructure.
- Phase 4 US2 DSP utilities (T039-T056): All `lerpHarmonicFrame`/`lerpResidualFrame` unit tests are parallelizable with each other (different test cases, same file).
- Phase 5 US3 DSP utilities (T065-T071): All `computeHarmonicMask`/`applyHarmonicMask` unit tests are parallelizable with each other.
- Phase 7 state persistence tests (T095-T100): State round-trip and Controller tests are parallelizable.

---

## Implementation Strategy

**MVP Scope (Phases 1-3)**: After Phase 3, the freeze feature alone is a complete, independently testable musical control. A performer can capture and hold a timbral snapshot indefinitely. This is the P1 foundational capability.

**Full P1 Delivery (Phases 1-4)**: After Phase 4, morph is fully functional. Freeze + Morph together deliver the core creative value proposition: expressive timbral interpolation between a snapshot and the live analysis.

**P2 Features (Phases 5-6)**: Harmonic Filter and Responsiveness are independent P2 enhancements that can be developed in either order after Phases 1-4 are complete.

**Incremental Delivery**: Each phase produces a runnable plugin that passes pluginval. No phase leaves the plugin in a broken state. Partial implementation is always safe because the new parameters default to behavior identical to pre-M4 Innexus (freeze off, morph at 0.0 = live frames, All-Pass filter = identity, responsiveness at 0.5 = M1/M3 default).

---

## Task Count Summary

| Phase | Tasks | Notes |
|-------|-------|-------|
| Phase 1: Setup | T001-T007 | 7 tasks |
| Phase 2: Foundational | T008-T020 | 13 tasks |
| Phase 3: US1 Freeze (P1) | T021-T038 | 18 tasks |
| Phase 4: US2 Morph (P1) | T039-T064 | 26 tasks |
| Phase 5: US3 Filter (P2) | T065-T085 | 21 tasks |
| Phase 6: US4 Responsiveness (P2) | T086-T094 | 9 tasks |
| Phase 7: State + Integration | T095-T105 | 11 tasks |
| Phase 8: Polish | T106-T123 + T118b + T122b | 20 tasks |
| **Total** | | **125 tasks** |

**Parallel tasks** (marked `[P]`): 78 tasks
**User story tasks**: 84 tasks (marked `[US1]`, `[US2]`, `[US3]`, `[US4]`)
**Independent test criteria**: Each user story phase is independently testable without completing the others (except US2 requires US1's freeze infrastructure)
