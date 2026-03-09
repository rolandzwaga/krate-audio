# Tasks: Innexus ADSR Envelope Detection

**Input**: Design documents from `/specs/124-adsr-envelope-detection/`
**Prerequisites**: plan.md, spec.md, data-model.md, quickstart.md
**Branch**: `124-adsr-envelope-detection`

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

### Integration Tests (MANDATORY)

This feature wires an ADSR envelope into the processor audio chain (note-on/off gating, amplitude multiplication in `process()`), extending the analysis pipeline, and extending memory slot capture/recall/morph. Integration tests are **required** for all of these sub-components.

Key rules:
- **Behavioral correctness over existence checks**: Verify output amplitude follows ADSR shape, not just "audio exists"
- **Test bit-exact bypass**: With Amount=0.0, output must be identical to pre-feature behavior (SC-003)
- **Test degraded host conditions**: Not just ideal playback - also test without transport/tempo context

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processor/test_envelope_detector.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Establish new files and register new parameters so all subsequent phases have a stable foundation to build on.

- [X] T001 Add 9 parameter IDs (kAdsrAttackId=720 through kAdsrReleaseCurveId=728) to `plugins/innexus/src/plugin_ids.h`
- [X] T002 Create `plugins/innexus/src/dsp/envelope_detector.h` with empty `DetectedADSR` struct and `EnvelopeDetector` class shell (no implementation yet)
- [X] T003 Create `plugins/innexus/tests/unit/processor/test_envelope_detector.cpp` as an empty Catch2 test file (compile-only check)
- [X] T004 Create `plugins/innexus/tests/integration/test_adsr_envelope.cpp` as an empty Catch2 test file (compile-only check)
- [X] T005 Register new test files in `plugins/innexus/tests/CMakeLists.txt` and verify `innexus_tests` target builds cleanly with zero errors

**Checkpoint**: `innexus_tests` target builds with new files in place. Parameter IDs 720-728 are defined.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented. Specifically: the `MemorySlot` extension and the 9 processor atomics/parameters that all stories depend on.

**CRITICAL**: No user story work can begin until this phase is complete.

- [X] T006 Extend `Krate::DSP::MemorySlot` in `dsp/include/krate/dsp/processors/harmonic_snapshot.h` with all 9 ADSR fields (adsrAttackMs=10.0f, adsrDecayMs=100.0f, adsrSustainLevel=1.0f, adsrReleaseMs=100.0f, adsrAmount=0.0f, adsrTimeScale=1.0f, adsrAttackCurve=0.0f, adsrDecayCurve=0.0f, adsrReleaseCurve=0.0f)
- [X] T007 Add 9 `std::atomic<float>` ADSR parameter fields to `Processor` in `plugins/innexus/src/processor/processor.h` (adsrAttackMs_, adsrDecayMs_, adsrSustainLevel_, adsrReleaseMs_, adsrAmount_, adsrTimeScale_, adsrAttackCurve_, adsrDecayCurve_, adsrReleaseCurve_)
- [X] T008 Add `Krate::DSP::ADSREnvelope adsr_` and `Krate::DSP::OnePoleSmoother adsrAmountSmoother_` member fields to `Processor` in `plugins/innexus/src/processor/processor.h`
- [X] T009 Handle all 9 parameter IDs (720-728) in `Processor::processParameterChanges()` in `plugins/innexus/src/processor/processor.cpp`, storing normalized-to-plain converted values into corresponding atomics
- [X] T010 Register all 9 `RangeParameter` instances in `Controller::initialize()` in `plugins/innexus/src/controller/controller.cpp` — Attack/Decay/Release use logarithmic mapping (1-5000ms), Sustain/Amount linear (0-1), TimeScale linear (0.25-4.0), Curve amounts linear (-1.0 to +1.0)
- [X] T011 Build `innexus_tests` target and verify zero compilation errors and zero warnings before continuing

**Checkpoint**: Foundation ready — processor has atomics, MemorySlot has ADSR fields, controller registers all 9 parameters. User story implementation can now begin.

---

## Phase 3: User Story 1 — Auto-Detect Envelope from Analyzed Sample (Priority: P1) — MVP

**Goal**: Load a sample, run analysis, and have ADSR parameters automatically populated from the amplitude contour. Play a MIDI note and have the output follow the detected envelope shape.

**Independent Test**: Load a synthetic percussive amplitude contour (fast attack, moderate decay, low sustain). Verify `EnvelopeDetector::detect()` returns Attack < 50ms and Sustain < 0.5. Verify the processor applies ADSR gain to audio output when Amount=1.0 and gates on note-on/off.

**Covers FRs**: FR-001, FR-002, FR-003, FR-008, FR-009, FR-011, FR-012, FR-021, FR-022, FR-023
**Covers SCs**: SC-001, SC-002, SC-003, SC-005

### 3.1 Tests for User Story 1 (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T012 [P] [US1] Write unit tests for `EnvelopeDetector` in `plugins/innexus/tests/unit/processor/test_envelope_detector.cpp`:
  - Test: synthetic percussive contour (step-up then decay) yields Attack < 50ms and Sustain < 0.5
  - Test: synthetic pad/drone contour (slow rise, flat sustain) yields Attack > 50ms and Sustain > 0.7
  - Test: constant-amplitude contour yields defaults (Attack ~5ms, Sustain ~1.0)
  - Test: very short contour (< 50ms) produces valid clamped values (all times >= 1ms)
  - Test: empty frame list produces sensible defaults without crash
  - Test: sidechain mode flag suppresses detection (FR-022)
  - Test: rolling least-squares steady-state detection identifies correct start frame (|slope| < 0.0005 AND variance < 0.002)

- [X] T013 [P] [US1] Write integration tests for ADSR processor pipeline in `plugins/innexus/tests/integration/test_adsr_envelope.cpp`:
  - Test: With Amount=0.0, audio output is bit-exact identical to bypassed output (SC-003)
  - Test: With Amount=1.0, a note-on followed by note-off produces output that rises then falls per ADSR shape
  - Test: Hard retrigger — new note-on during held note resets envelope to attack stage immediately (FR-012)
  - Test: `adsr_.prepare(sampleRate)` is called on `setActive()` / `prepare()` (no crash on first block)
  - Test: Amount parameter change during active note produces no NaN or Inf values (FR-023)

- [X] T014 [US1] Build `innexus_tests` and confirm T012 and T013 test cases FAIL (compilation succeeds, tests fail at runtime — stubs return 0/defaults)

### 3.2 Implementation for User Story 1

- [X] T015 [US1] Implement `DetectedADSR` struct and full `EnvelopeDetector::detect()` algorithm in `plugins/innexus/src/dsp/envelope_detector.h`:
  - Extract `globalAmplitude` per frame into contour vector
  - Find peak index
  - Compute Attack = peakIndex * hopTimeSec * 1000.0f ms
  - O(1) rolling least-squares with fixed window size `kWindowSize=12` (within the valid 8–20 frame range per FR-002)
  - Window grows from 0 to kWindowSize during grow-in phase (n tracks occupancy); thereafter slides by subtracting oldest and adding newest
  - Maintain: n, sum_x, sum_y, sum_xy, sum_x2, mean, M2 (Welford online variance)
  - Steady-state: |slope| < 0.0005/frame AND variance < 0.002
  - Compute Decay and Sustain per data-model.md algorithm
  - Release = (totalFrames - 1 - steady_state_end) * hopTimeSec * 1000.0f; `steady_state_end` is the **last** steady-state frame index (inclusive, 0-based); default 100ms if no steady state found
  - Clamp all outputs to valid ranges ([1, 5000] ms for times, [0, 1] for sustain)

- [X] T016 [US1] Add `DetectedADSR detectedADSR{}` field to `SampleAnalysis` struct in `plugins/innexus/src/dsp/sample_analysis.h`

- [X] T017 [US1] Call `EnvelopeDetector::detect()` at end of `SampleAnalyzer::analyzeOnThread()` in `plugins/innexus/src/dsp/sample_analyzer.cpp`, storing result in `SampleAnalysis::detectedADSR`. Skip detection when input source is sidechain (FR-022)

- [X] T018 [US1] In `Processor::checkForNewAnalysis()` in `plugins/innexus/src/processor/processor.cpp`: read `detectedADSR` from new analysis result, write Attack/Decay/Sustain/Release to corresponding atomics, send `IMessage` to Controller to update knob positions

- [X] T019 [US1] Wire `adsr_.gate(true)` in `Processor::handleNoteOn()` and `adsr_.gate(false)` in `Processor::handleNoteOff()` in `plugins/innexus/src/processor/processor.cpp` — call `adsr_.setRetriggerMode(RetriggerMode::Hard)` in `Processor::initialize()` or constructor **before** any gate calls to satisfy FR-012; verify `RetriggerMode::Hard` exists in `adsr_envelope.h`

- [X] T020 [US1] Call `adsr_.prepare(sampleRate)` and `adsrAmountSmoother_.prepare(sampleRate)` in `Processor::prepare()` and/or `setActive()` in `plugins/innexus/src/processor/processor.cpp`. Both must be initialized before the first audio block to avoid incorrect smoother output.

- [X] T021 [US1] In `Processor::process()` in `plugins/innexus/src/processor/processor.cpp`:
  - Read adsrAmount_ atomic; if Amount == 0.0 skip **all** ADSR processing including envelope tick (bit-exact bypass, SC-003)
  - Otherwise: update envelope parameters from atomics — effective times = `param_time_ms * timeScale`, clamped to [1, 5000]ms per segment (clamping applies independently; when a segment hits the clamp boundary the ratio between segments may differ slightly from the unscaled ratio — this is acceptable per FR-010)
  - Pass clamped times via setAttack, setDecay, setRelease; pass sustain directly (unscaled); pass curve amounts via setAttackCurve, setDecayCurve, setReleaseCurve
  - Smooth Amount via `adsrAmountSmoother_`
  - Multiply each output sample by `lerp(1.0f, adsr_.process(), smoothedAmount)` where `lerp(a,b,t) = a*(1-t) + b*t` — at Amount=0.0 gain is always 1.0 (bypass), at Amount=1.0 gain equals raw envelope output, at intermediate values output is never fully silent (intentional per FR-008)

- [X] T022 [US1] Verify all tests in T012 and T013 now PASS. Fix any failures before continuing.

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T023 [US1] Check `test_envelope_detector.cpp` and `test_adsr_envelope.cpp` for usage of `std::isnan`, `std::isfinite`, `std::isinf` — if present, add source files to `-fno-fast-math` compile flags in `plugins/innexus/tests/CMakeLists.txt`

### 3.4 Commit (MANDATORY)

- [X] T024 [US1] Build full `innexus_tests` target, confirm all existing tests still pass, commit all User Story 1 work with message: `feat(innexus): add ADSR envelope detector and audio-thread envelope application`

**Checkpoint**: User Story 1 is fully functional — envelope detection runs on sample load, ADSR gates on note events, Amount=0.0 is bit-exact bypass. All tests pass.

---

## Phase 4: User Story 2 — Edit ADSR Parameters After Detection (Priority: P1)

**Goal**: 9 user-editable ADSR parameters (including Time Scale and 3 curve amounts) are exposed, respond to host automation, and produce smooth transitions during active notes. Envelope Amount blends between flat and full ADSR shaping. ADSRDisplay wires to parameters for interactive curve editing.

**Independent Test**: Set known ADSR values manually via parameter changes. Play a MIDI note. Confirm output follows the manually-set envelope. Set Amount=0.0 and confirm bit-exact bypass. Set Amount=1.0 and confirm full shaping. Adjust Time Scale=2.0 and verify effective times double.

**Covers FRs**: FR-004, FR-005, FR-006, FR-007, FR-008, FR-009, FR-010, FR-018, FR-021, FR-023, FR-024, FR-025, FR-026
**Covers SCs**: SC-004, SC-005, SC-007

### 4.1 Tests for User Story 2 (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T025 [P] [US2] Extend integration tests in `plugins/innexus/tests/integration/test_adsr_envelope.cpp`:
  - Test: Setting Attack=500ms via parameter change, play note, verify attack phase lasts ~500ms of samples (within reasonable tolerance)
  - Test: Time Scale=2.0 doubles effective attack, decay, and release durations (but not sustain level)
  - Test: Time Scale at extremes (0.25x, 4.0x) produces clamped values in [1, 5000]ms range
  - Test: Attack Curve=+1.0 (exponential) produces a convex rise; =−1.0 (logarithmic) produces concave rise
  - Test: Amount transition from 0.0 to 1.0 during active note produces no NaN, Inf, or amplitude discontinuities > 0.01 per sample
  - Test: All 9 parameters respond correctly to parameter change events (SC-007)

- [X] T026 [P] [US2] Write VST parameter registration tests in `plugins/innexus/tests/unit/vst/` (or extend existing VST tests):
  - Test: All 9 parameter IDs (720-728) exist and are registered with correct normalized ranges [0.0, 1.0]
  - Test: Normalize/denormalize round-trip for logarithmic time parameters (Attack, Decay, Release)
  - Test: kAdsrAmountId defaults to normalized value corresponding to 0.0 plain (Envelope Amount default = 0.0)

- [X] T027 [US2] Build `innexus_tests` and confirm T025 and T026 test cases FAIL where implementation is not yet present

### 4.2 Implementation for User Story 2

- [X] T028 [US2] Verify Time Scale parameter is applied as a multiplier to attack, decay, and release times (NOT sustain) in `Processor::process()` — effective times = param * scale, clamped to [1, 5000]ms. This likely extends T021 implementation from Phase 3.

- [X] T029 [US2] Verify all 3 curve amounts (kAdsrAttackCurveId=726, kAdsrDecayCurveId=727, kAdsrReleaseCurveId=728) are correctly passed to `adsr_.setAttackCurve(float)`, `setDecayCurve(float)`, `setReleaseCurve(float)` (the float overloads, -1 to +1). These should already be wired from T021 — confirm and fix if missing.

- [X] T030 [US2] Wire `ADSRDisplay` in `Controller::createCustomView()` in `plugins/innexus/src/controller/controller.cpp`:
  - Match on `custom-view-name` = `"ADSRDisplay"` (this string must match exactly what is written in `editor.uidesc` — agree on it here and in T031 before coding)
  - Call `setAdsrBaseParamId(720)` — expects 4 consecutive IDs: A=720, D=721, S=722, R=723
  - Call `setCurveBaseParamId(726)` — expects 3 consecutive IDs: AC=726, DC=727, RC=728
  - Call `setParameterCallback(...)`, `setBeginEditCallback(...)`, `setEndEditCallback(...)` to wire drag edits back to VST parameter system
  - Call `setPlaybackStatePointers(...)` to wire playback dot (use processor atomics for envelope output, stage, and active flag — expose via shared atomics or IMessage)

- [X] T031 [US2] Add `ADSRDisplay` custom view to `plugins/innexus/resources/editor.uidesc`:
  - Use `custom-view-name="ADSRDisplay"` (must match the string in T030's `createCustomView()` exactly — a mismatch causes the view to silently not be created)
  - Add 9 knob/control entries for Attack, Decay, Sustain, Release, Amount, Time Scale, Attack Curve, Decay Curve, Release Curve with correct parameter IDs (720-728)
  - Position and size appropriately within existing UI layout

- [X] T032 [US2] Verify all tests in T025 and T026 now PASS. Fix any failures before continuing.

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T033 [US2] Check new/modified test files for IEEE 754 function usage (`std::isnan`, etc.) — update `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` if needed

### 4.4 Commit (MANDATORY)

- [X] T034 [US2] Build full `innexus_tests`, run pluginval (`tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"`), confirm all tests pass, commit: `feat(innexus): add ADSR parameter controls, Time Scale, curve amounts, and ADSRDisplay UI`

**Checkpoint**: All 9 parameters are automatable, ADSRDisplay wired and interactive, smooth Amount/curve transitions confirmed. Tests and pluginval both pass.

---

## Phase 5: User Story 3 — Per-Slot ADSR Storage & Recall (Priority: P2)

**Goal**: Each of the 8 memory slots stores its own set of 9 ADSR parameters. Capture, recall, and morph all correctly handle ADSR values. Morphing between slots uses geometric mean for time parameters and linear for Sustain/Amount/curves.

**Independent Test**: Capture ADSR values to two slots with contrasting envelopes. Recall each and verify ADSR knobs update. Set morph at 0.5 and verify Attack uses geometric mean (sqrt(a*b)), Sustain uses arithmetic mean.

**Covers FRs**: FR-013, FR-014, FR-015, FR-016, FR-017
**Covers SCs**: (supports SC-007 via morph parameter testing)

### 5.1 Tests for User Story 3 (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T035 [P] [US3] Write unit tests for MemorySlot ADSR capture/recall in `plugins/innexus/tests/unit/processor/` (new file `test_memory_slot_adsr.cpp` or extend existing memory tests):
  - Test: Capture stores all 9 ADSR values into slot alongside harmonic snapshot
  - Test: Recall restores all 9 ADSR values; verify atomics updated in processor
  - Test: Slot defaults have adsrAmount=0.0f (no envelope shaping if slot captured before feature)

- [X] T036 [P] [US3] Write unit tests for ADSR morph interpolation in the same file or a sibling file:
  - Test: Morph at t=0.5 between {Attack=10ms, Decay=100ms, Sustain=0.3, Release=200ms} and {Attack=500ms, Decay=50ms, Sustain=0.9, Release=1000ms} yields approximately {Attack=71ms, Decay=71ms, Sustain=0.6, Release=447ms} — geometric mean for times, linear for Sustain (margin ±5ms for times, ±0.01 for sustain)
  - Test: Morph at t=0.5 for Envelope Amount (linear: 0.0 + 1.0 = 0.5)
  - Test: Morph at t=0.5 for Curve Amounts (linear interpolation)
  - Test: Evolution engine produces smooth ADSR interpolation across occupied slot transitions

- [X] T037 [US3] Build `innexus_tests` and confirm T035 and T036 test cases FAIL where implementation is not yet present

### 5.2 Implementation for User Story 3

- [X] T038 [US3] Extend memory slot capture in `Processor` (`plugins/innexus/src/processor/processor.cpp`): when capturing to a slot, write all 9 current ADSR atomic values into the corresponding `MemorySlot` ADSR fields (FR-013, FR-014)

- [X] T039 [US3] Extend memory slot recall in `Processor` (`plugins/innexus/src/processor/processor.cpp`): when recalling a slot, read all 9 ADSR fields from the slot, write to processor atomics, and send `IMessage` to Controller to update knob positions (FR-015)

- [X] T040 [US3] Extend **morph engine** ADSR interpolation (FR-016): locate the morph calculation site (`HarmonicBlender` or wherever the host-driven slot morph is computed — distinct from the evolution engine) and add ADSR interpolation using the same rules: geometric mean for adsrAttackMs, adsrDecayMs, adsrReleaseMs (`std::exp((1-t)*std::log(a) + t*std::log(b))`); linear for adsrSustainLevel, adsrAmount, adsrTimeScale, adsrAttackCurve, adsrDecayCurve, adsrReleaseCurve (`a*(1-t) + b*t`). If the morph and evolution paths share the same interpolation function, verify T040b (below) is not needed.

- [X] T040b [US3] Extend **evolution engine** ADSR interpolation (FR-017): in `EvolutionEngine::getInterpolatedFrame()` in `plugins/innexus/src/dsp/evolution_engine.h` (or `.cpp`), apply the same geometric mean / linear interpolation rules as T040. If morph and evolution already share the same interpolation function, confirm both FR-016 and FR-017 are covered by that shared function and document accordingly.

- [X] T041 [US3] Wire interpolated ADSR values from both the morph engine (T040) and `EvolutionEngine` (T040b) back to processor atomics when morph/evolution is active — ensure processor reads interpolated values rather than the raw stored slot values during active morph or evolution

- [X] T042 [US3] Verify all tests in T035 and T036 now PASS. Fix any failures before continuing.

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T043 [US3] Check new test files for IEEE 754 function usage — update `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` if needed (geometric mean uses `std::log`/`std::exp`, verify these are safe under `-ffast-math` or guard with pragma)

### 5.4 Commit (MANDATORY)

- [X] T044 [US3] Build full `innexus_tests`, confirm all tests pass, commit: `feat(innexus): add per-slot ADSR storage, recall, and logarithmic morph interpolation`

**Checkpoint**: Memory slots capture and restore all 9 ADSR parameters. Morph uses geometric mean for time params. Evolution engine propagates ADSR alongside harmonic data.

---

## Phase 6: User Story 4 — ADSR Visualization (Priority: P2)

**Goal**: The ADSRDisplay component is fully functional — draggable control points and curve segments update parameters, and a playback dot animates during note playback. Visual feedback reflects parameter changes in real time.

**Independent Test**: Open plugin UI. Drag the peak control point horizontally and verify Attack time parameter updates. Play a note and verify playback dot moves along the envelope curve.

**Covers FRs**: FR-018
**Covers SCs**: (visual verification; automation tested in US2)

**Note**: Much of the ADSRDisplay wiring was completed in Phase 4 (T030, T031). This phase focuses on the playback dot and verifying full round-trip behavior of drag-to-edit interactions.

### 6.1 Tests for User Story 4 (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T045 [P] [US4] Write UI wiring tests (if testable programmatically) in `plugins/innexus/tests/unit/` (extend existing controller tests or create `test_adsr_display_wiring.cpp`):
  - Test: `Controller::createCustomView()` returns a non-null view for the ADSR display custom-view-name
  - Test: ADSRDisplay receives correct base param IDs (720 for ADSR, 726 for curves) after wiring

- [X] T046 [US4] Verify playback dot pointers are exposed correctly: the processor must publish current envelope output level (float atomic), current stage (int atomic), and active flag (bool atomic) for the controller to pass to `ADSRDisplay::setPlaybackStatePointers()`. Write a test verifying these atomics exist and are updated during note playback.

- [X] T047 [US4] Build `innexus_tests` and confirm T045 and T046 FAIL where not yet implemented

### 6.2 Implementation for User Story 4

- [X] T048 [US4] Add playback state atomics to `Processor` in `plugins/innexus/src/processor/processor.h`: `std::atomic<float> adsrEnvelopeOutput_{}`, `std::atomic<int> adsrStage_{0}`, `std::atomic<bool> adsrActive_{false}`. Update these in `Processor::process()` from `adsr_.getOutput()`, `adsr_.getStage()`, `adsr_.isActive()` on each block.

- [X] T049 [US4] In `Controller::createCustomView()` in `plugins/innexus/src/controller/controller.cpp`: call `setPlaybackStatePointers(&processor->adsrEnvelopeOutput_, &processor->adsrStage_, &processor->adsrActive_)` on the ADSRDisplay view (verify access pattern matches existing processor pointer sharing in the controller)

- [X] T050 [US4] Verify all tests in T045 and T046 now PASS. Fix any failures.

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T051 [US4] Check any new test files for IEEE 754 usage — update `-fno-fast-math` list if needed

### 6.4 Commit (MANDATORY)

- [X] T052 [US4] Build full `innexus_tests`, verify plugin builds and pluginval passes, commit: `feat(innexus): wire ADSRDisplay playback dot and complete visualization integration`

**Checkpoint**: ADSRDisplay shows envelope shape, responds to drag, and playback dot animates during notes.

---

## Phase 7: User Story 5 — State Persistence & Backward Compatibility (Priority: P3)

**Goal**: State version incremented to v9. All 9 global ADSR parameters and per-slot ADSR data are serialized/deserialized. v1-v8 states load without errors, defaulting Envelope Amount to 0.0 and all curve amounts to 0.0.

**Independent Test**: Save state with known ADSR values, reload, verify exact match. Load a v8 state blob and verify Envelope Amount = 0.0, no errors, no crash.

**Covers FRs**: FR-019, FR-020
**Covers SCs**: SC-006

### 7.1 Tests for User Story 5 (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T053 [P] [US5] Write state serialization tests in `plugins/innexus/tests/unit/vst/` (extend existing state tests or create `test_state_v9.cpp`):
  - Test: Save state with Amount=0.7, Attack=250ms, DecayCurve=0.5, per-slot data for slot 3 (Attack=50ms). Reload state. Verify all values match exactly.
  - Test: Load a hand-crafted v8 binary state blob. Verify Amount defaults to 0.0, all curve amounts default to 0.0, Attack/Decay/Sustain/Release default to reasonable values (10/100/1.0/100 ms), no error return code.
  - Test: Load a hand-crafted v7 or earlier state blob. Same backward compatibility guarantees.
  - Test: State version written is 9 (read back version byte and verify).
  - Test: Per-slot ADSR data for all 8 slots round-trips correctly.

- [X] T054 [US5] Build `innexus_tests` and confirm T053 test cases FAIL (version is still 8, no ADSR written/read)

### 7.2 Implementation for User Story 5

- [X] T055 [US5] In `plugins/innexus/src/processor/processor_state.cpp`: increment state version from 8 to 9 in the write path

- [X] T056 [US5] In the write path of `processor_state.cpp`: write all 9 global ADSR floats (in parameter ID order: 720-728 values), then write per-slot ADSR data for all 8 slots (9 floats per slot = 72 floats total)

- [X] T057 [US5] In the read path of `processor_state.cpp`: add `if (version >= 9)` block that reads all 9 global ADSR floats and 72 per-slot ADSR floats. In the `else` branch (v1-v8): set Envelope Amount default to 0.0, all curve amounts to 0.0, ADSR times to defaults (10/100/100 ms), Sustain to 1.0, TimeScale to 1.0.

- [X] T058 [US5] Verify all tests in T053 now PASS. Fix any failures before continuing.

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T059 [US5] Check new state test files for IEEE 754 usage — update `-fno-fast-math` list if needed

### 7.4 Commit (MANDATORY)

- [X] T060 [US5] Build full `innexus_tests`, confirm all tests pass, commit: `feat(innexus): add state v9 serialization for ADSR parameters with v1-v8 backward compatibility`

**Checkpoint**: State saves/loads all ADSR data. Old states load safely with Amount=0.0 default.

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Final quality pass covering all user stories.

- [X] T061 [P] Run `innexus_tests` full suite and confirm zero test failures across all phases
- [X] T062 [P] Run pluginval at strictness level 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"` and verify no failures
- [X] T063 Manually verify Amount=0.0 produces bit-exact bypass: compare processed audio with Amount=0.0 vs. no-ADSR reference (SC-003). Document test method in commit message.
- [X] T063b Measure SC-001 analysis overhead: time `SampleAnalyzer::analyzeOnThread()` with and without the `EnvelopeDetector::detect()` call on a representative sample (< 5 seconds, 44.1kHz). Verify that the ratio `(time_with_detection / time_without_detection) < 1.10` (i.e., <10% overhead). Record the actual measured values in the compliance table for SC-001.
- [X] T064 Manually verify smooth transitions: change Amount from 0.0 to 1.0 during active note, listen for clicks or discontinuities (SC-004). Confirm no amplitude jump > 0.01 per sample.
- [X] T064b Measure SC-005 CPU overhead: profile `Processor::process()` with ADSR active (Amount=1.0, note held) vs. bypassed (Amount=0.0). Verify the ADSR-only delta is <0.1% of a single core at 44.1kHz. Record the actual measured value in the compliance table for SC-005. (The plan projects ~0.01%, but record the actual measurement.)
- [X] T065 Verify all 9 parameters appear in host automation lane (load in a DAW or use pluginval automation test). Document as SC-007 evidence.
- [X] T066 [P] Audit all new code for: no `// placeholder`, `// TODO`, or `// FIXME` comments; no raw `new`/`delete`; no allocations in `process()` path; all atomics accessed with appropriate memory orders

---

## Phase 9: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification.

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 9.1 Run Clang-Tidy Analysis

- [X] T067 Regenerate `compile_commands.json` if new source files were added (run `cmake --preset windows-ninja` from a VS Developer PowerShell)
- [X] T068 Run clang-tidy on all modified/new source files:
  ```powershell
  ./tools/run-clang-tidy.ps1 -Target innexus -BuildDir build/windows-ninja
  ```

### 9.2 Address Findings

- [X] T069 Fix all errors reported by clang-tidy (blocking issues) — 0 errors in new ADSR code
- [X] T070 Review warnings and fix where appropriate (use judgment for DSP code) — 3 warnings all in pre-existing code (processor_messages.cpp, controller_presets.cpp), none in ADSR feature code
- [X] T071 Document suppressions with `// NOLINT(<check>): <reason>` for any intentionally ignored warnings — no suppressions needed

**Checkpoint**: Static analysis clean — ready for completion verification.

---

## Phase 10: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion.

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task.

### 10.1 Architecture Documentation Update

- [X] T072 Update `specs/_architecture_/` with new components added by this spec:
  - Document `EnvelopeDetector` (plugin-local DSP, `plugins/innexus/src/dsp/envelope_detector.h`): purpose (amplitude contour ADSR fitting using O(1) rolling least-squares), public API (`detect(frames, hopTimeSec) -> DetectedADSR`), "when to use"
  - Document `MemorySlot` ADSR extension in the appropriate layer file: 9 new ADSR fields, purpose, default values
  - Note the geometric mean interpolation pattern used for time parameters in morph/evolution

### 10.2 Architecture Documentation Commit

- [X] T073 Commit architecture documentation updates: `docs(architecture): document EnvelopeDetector and MemorySlot ADSR extension for spec 124`

**Checkpoint**: Architecture documentation reflects all new functionality.

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion.

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

- [ ] T074 **Review ALL FR-001 through FR-026** from `specs/124-adsr-envelope-detection/spec.md` against implementation — open each file, read the code, cite file+line for each FR
- [ ] T075 **Review ALL SC-001 through SC-007** — run relevant tests or measure actual values, copy test output, compare against spec thresholds with real numbers
- [ ] T076 **Search for cheating patterns**: no placeholder/TODO comments in new code, no relaxed test thresholds, no quietly removed features

### 11.2 Fill Compliance Table in spec.md

- [ ] T077 **Update `specs/124-adsr-envelope-detection/spec.md` "Implementation Verification" section** with compliance status and concrete evidence (file paths, line numbers, test names, measured values) for every FR-xxx and SC-xxx row
- [ ] T078 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

Answer these questions — if ANY is "yes", do NOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T079 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

### 11.4 Final Commit

- [ ] T080 **Commit final spec.md compliance table update**: `chore(innexus): fill spec 124 compliance table — ADSR envelope detection`
- [ ] T081 **Verify all tests pass** on final commit: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`

**Checkpoint**: Spec implementation honestly complete.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 — BLOCKS all user stories (MemorySlot extension and processor atomics must exist)
- **Phase 3 (US1)**: Depends on Phase 2 — no dependency on US2/US3/US4/US5
- **Phase 4 (US2)**: Depends on Phase 2 — no dependency on US1 but benefits from US1's processor wiring (can run in parallel with US1 if processor atomics exist from Phase 2)
- **Phase 5 (US3)**: Depends on Phase 2 AND Phase 3 (requires MemorySlot fields from foundational + processor capture/recall hooks from US1)
- **Phase 6 (US4)**: Depends on Phase 4 (ADSRDisplay wiring from US2 is prerequisite for playback dot)
- **Phase 7 (US5)**: Depends on Phase 2 (MemorySlot fields must exist) — can run in parallel with US3/US4
- **Phases 8-11**: Depend on all user stories being complete

### User Story Dependencies

```
Phase 1 (Setup)
    |
Phase 2 (Foundational: MemorySlot + Processor atomics + Controller registration)
    |
    +---> Phase 3 (US1: Detector + Processor audio chain)
    |         |
    |         +---> Phase 5 (US3: Slot capture/recall/morph — needs US1 hooks)
    |
    +---> Phase 4 (US2: Parameter editing + UI wiring)
    |         |
    |         +---> Phase 6 (US4: Playback dot — needs US2 ADSRDisplay wiring)
    |
    +---> Phase 7 (US5: State v9 — only needs foundational MemorySlot fields)
```

### Within Each User Story

- **Tests FIRST**: Write tests that FAIL before writing implementation (Principle XII)
- **Implement**: Make tests pass
- **Verify**: Confirm all tests pass, no regressions
- **Cross-platform check**: Verify IEEE 754 compliance
- **Commit**: MANDATORY last step

### Parallel Opportunities Within Phases

- T012 and T013 (US1 tests) can be written in parallel — different files
- T025 and T026 (US2 tests) can be written in parallel — different files
- T035 and T036 (US3 tests) can be written in parallel — different files
- T061, T062, T066 (Phase 8 polish checks) can run in parallel
- T067, T068 (clang-tidy setup and run) must be sequential

---

## Parallel Execution Examples

### Phase 3 (User Story 1) — Two tasks in parallel

```bash
# Simultaneously:
Task T012: Write EnvelopeDetector unit tests in test_envelope_detector.cpp
Task T013: Write ADSR processor integration tests in test_adsr_envelope.cpp

# Then sequentially:
Task T014: Build and confirm tests FAIL
Task T015: Implement EnvelopeDetector::detect() algorithm
Task T016: Extend SampleAnalysis struct
# ...
```

### Phase 5 (User Story 3) — Two tasks in parallel

```bash
# Simultaneously:
Task T035: Write capture/recall unit tests
Task T036: Write morph interpolation unit tests

# Then sequentially:
Task T037: Build and confirm tests FAIL
Task T038: Extend slot capture
Task T039: Extend slot recall
Task T040: Extend morph interpolation (geometric mean)
Task T041: Wire evolution engine
```

---

## Implementation Strategy

### MVP First (User Stories 1 and 2, P1)

1. Complete Phase 1: Setup (T001-T005)
2. Complete Phase 2: Foundational — CRITICAL, blocks all stories (T006-T011)
3. Complete Phase 3: User Story 1 — Envelope detection + audio-thread ADSR (T012-T024)
4. Complete Phase 4: User Story 2 — Parameter editing + ADSRDisplay (T025-T034)
5. **STOP and VALIDATE**: Test both P1 stories independently. The plugin is now functionally complete with envelope detection, all 9 parameters, interactive UI, and correct audio behavior.

### Full Delivery (All Stories)

6. Complete Phase 5: User Story 3 — Per-slot ADSR (T035-T044)
7. Complete Phase 6: User Story 4 — Playback dot (T045-T052)
8. Complete Phase 7: User Story 5 — State v9 persistence (T053-T060)
9. Complete Phases 8-11: Polish, clang-tidy, architecture docs, compliance verification

### Key Constraints to Remember

- **Bit-exact bypass** (SC-003): When Amount=0.0, skip ALL ADSR processing — no multiply, no envelope tick
- **No audio-thread allocations**: `ADSREnvelope` is a member of `Processor`, pre-allocated at construction
- **Hard retrigger** (FR-012): Use `ADSREnvelope::RetriggerMode::Hard` — verify this mode exists in the header
- **Monophonic**: Single `adsr_` instance in Processor — NOT per-voice
- **Parameter ID layout for ADSRDisplay**: IDs 720-723 must be consecutive (A/D/S/R), IDs 726-728 must be consecutive (AC/DC/RC) — already satisfied by data-model.md assignments
- **kReleaseTimeId=200** is a different parameter (oscillator release fade) — the new ADSR release is `kAdsrReleaseId=723`

---

## Notes

- `[P]` tasks = different files, no dependencies, can run in parallel
- `[US1]`-`[US5]` labels map tasks to user stories from spec.md for traceability
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance after each story
- **MANDATORY**: Commit at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment (Principle XV)
- **NEVER claim completion if ANY requirement is not met** — document gaps honestly instead
