---
description: "Task list for Membrum Phase 1 -- Plugin Scaffold + Single Voice"
---

# Tasks: Membrum Phase 1 — Plugin Scaffold + Single Voice

**Input**: Design documents from `/specs/136-membrum-phase1-scaffold/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/vst3-interface.md, research.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation. Exception: CI configuration tasks (Phase 7) have no pre-implementation test — the CI run itself is the test. Syntax validation (T060) substitutes for a failing test in that phase.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow.

### Required Steps for EVERY Task

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests — `ctest --test-dir build/windows-x64-release -C Release --output-on-failure -R membrum_tests`
4. **Run Clang-Tidy**: `./tools/run-clang-tidy.ps1 -Target membrum`
5. **Commit**: Commit the completed work

### Integration Tests (MANDATORY When Applicable)

The processor wires MIDI, audio, and parameters together — integration tests are required for the process() path. Rules:
- Verify output is **correct**, not just present ("audio exists" is not a valid test)
- Test degraded host conditions: no transport, `nullptr` process context, zero-length blocks
- Verify `setModes()` vs `updateModes()` distinction is not accidentally resetting resonator state on param changes

### Cross-Platform Compatibility (After Each User Story)

The VST3 SDK enables `-ffast-math` globally. After implementing tests that use `std::isnan`, `std::isfinite`, or `std::isinf`:
- Add the test source file to `-fno-fast-math` in `plugins/membrum/tests/CMakeLists.txt`
- Use `Approx().margin()` for floating-point comparisons, never exact equality

---

## Phase 1: Setup (Scaffolding)

**Purpose**: Create the plugin directory structure, build system, version metadata, and resource files. Establishes the CMake target so the compiler can process subsequent code.

- [X] T001 Create `plugins/membrum/` directory tree: `src/processor/`, `src/controller/`, `src/dsp/`, `tests/unit/vst/`, `tests/unit/processor/`, `resources/auv3/`, `docs/assets/`
- [X] T002 Create `plugins/membrum/version.json` with `{"major":0,"minor":1,"patch":0}` (single source of truth per FR-004)
- [X] T003 Create `plugins/membrum/src/version.h.in` following the Gradus template pattern (cmake configure_file generates `version.h` at configure time)
- [X] T004 [P] Create `plugins/membrum/src/plugin_ids.h` with Membrum namespace, processor/controller FUIDs, `kSubCategories`, `kCurrentStateVersion=1`, and `ParameterIds` enum (`kMaterialId=100` through `kLevelId=104`) per data-model.md. Before writing the file, run `grep -r "4D656D62\|72756D50\|726F6331\|72756D43\|74726C31" plugins/*/src/plugin_ids.h` to confirm no existing plugin uses these FUID hex values (FUID collision = undefined behavior).
- [X] T005 [P] Create `plugins/membrum/resources/au-info.plist` for AU v2 config: type `aumu`, subtype `Mbrm`, manufacturer `KrAt`, channel config `0 in / 2 out`, following Gradus au-info.plist pattern (FR-006)
- [X] T006 [P] Create `plugins/membrum/resources/auv3/audiounitconfig.h` with `kSupportedNumChannels` = `0022` (0 in / 2 out) per FR-006 and AU wrapper lessons
- [X] T007 [P] Create `plugins/membrum/resources/win32resource.rc.in` following the Gradus win32 resource template pattern (FR-007)
- [X] T008 Create `plugins/membrum/CMakeLists.txt`: reads `version.json` with `file(READ ...)`, runs `configure_file` for version.h, calls `smtg_add_vst3plugin(Membrum ...)`, links `KrateDSP` + `KratePluginsShared`, configures AU plist/resource, follows Gradus CMakeLists pattern (FR-001)
- [X] T009 Create `plugins/membrum/tests/CMakeLists.txt`: defines `membrum_tests` CTest target, compiles processor + controller sources + Catch2 entry, links SDK hosting helpers, follows Gradus tests/CMakeLists pattern
- [X] T010 Edit root `CMakeLists.txt` to add `add_subdirectory(plugins/membrum)` after the Gradus entry (FR-008)
- [X] T011 Create `plugins/membrum/CHANGELOG.md` with initial `[0.1.0]` entry listing Phase 1 deliverables (FR-051)
- [X] T012 [P] Create `plugins/membrum/docs/index.html`, `plugins/membrum/docs/manual-template.html`, and `plugins/membrum/docs/assets/style.css` following the Gradus doc structure (FR-050)
- [X] T013 Verify CMake configure succeeds: `"C:/Program Files/CMake/bin/cmake.exe" --preset windows-x64-release` — fix any CMakeLists errors before proceeding

**Checkpoint**: CMake configures without errors. `Membrum` target exists. Empty plugin compiles (stubs acceptable here).

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Create the stub entry point and empty processor/controller skeletons so the build system has valid translation units to compile. These stubs let the Phase 3–5 test files compile and fail (not compile-error) before implementation begins.

**CRITICAL**: No user story work can begin until this phase produces a compilable plugin.

- [X] T014 Create `plugins/membrum/src/entry.cpp` following the Gradus factory entry pattern: `BEGIN_FACTORY_DEF("Krate Audio")`, two `DEF_CLASS2` registrations (Processor + Controller), `END_FACTORY`, correct subcategory `"Instrument|Drum"` (FR-002, FR-005)
- [X] T015 Create `plugins/membrum/src/processor/processor.h` — `Membrum::Processor` class skeleton: inherits `Steinberg::Vst::AudioEffect`, declares `initialize`, `setupProcessing`, `process`, `getState`, `setState`, `setActive`; atomic parameter members; stub bodies only
- [X] T016 Create `plugins/membrum/src/processor/processor.cpp` — minimal stub implementations that compile: `initialize()` adds event input + stereo audio output buses, all other methods return `kResultOk`; no audio logic yet
- [X] T017 Create `plugins/membrum/src/controller/controller.h` — `Membrum::Controller` class skeleton: inherits `Steinberg::Vst::EditControllerEx1`, declares `initialize`, `setComponentState`, `createView`; stub bodies only
- [X] T018 Create `plugins/membrum/src/controller/controller.cpp` — minimal stub: `initialize()` returns `kResultOk` (no params yet), `createView()` returns `nullptr` (FR-021), `setComponentState()` returns `kResultOk`
- [X] T019 Create `plugins/membrum/tests/unit/test_main.cpp` — Catch2 entry point (`CATCH_CONFIG_RUNNER` / `main()`) following Gradus test_main.cpp pattern
- [X] T020 Build the stub plugin to confirm the full pipeline compiles: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Membrum` — fix all compiler errors and warnings before proceeding

**Checkpoint**: `Membrum.vst3` builds without errors or warnings. Test binary compiles. User story phases can now begin.

---

## Phase 3: User Story 1 — Plugin Loads in DAW (Priority: P1) — MVP

**Goal**: The plugin registers correctly, exposes 5 parameters with correct metadata, and round-trips state. This is the scaffold foundation — without it nothing else works.

**Independent Test**: Run `membrum_tests` and verify parameter registration tests pass. Run pluginval. State save/load round-trip produces bit-identical values (SC-004).

### 3.1 Tests for User Story 1 (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [X] T021 [US1] Create `plugins/membrum/tests/unit/vst/membrum_vst_tests.cpp` and write failing tests covering: (a) exactly 5 parameters are registered with IDs 100–104, (b) each parameter has the correct name string, range [0,1], and default value per contracts/vst3-interface.md, (c) state written then read back produces bit-identical normalized values (SC-004), (d) bus config reports 0 audio inputs and 1 stereo audio output (FR-002), (e) plugin subcategory is `"Instrument|Drum"`, (f) `createView()` returns nullptr (FR-021). Verify tests FAIL (compile but fail assertions) before moving on.

### 3.2 Implementation for User Story 1

- [X] T022 [US1] Implement `Membrum::Controller::initialize()` in `plugins/membrum/src/controller/controller.cpp`: register 5 `RangeParameter` instances using IDs/names/defaults from the Parameter Contract table in `contracts/vst3-interface.md`; Material=0.5, Size=0.5, Decay=0.3, StrikePosition=0.3, Level=0.8; Level unit string = "dB"; all others unit string = "" (FR-020)
- [X] T023 [US1] Implement `Membrum::Controller::setComponentState()` in `plugins/membrum/src/controller/controller.cpp`: read binary state stream (version int32, then 5x float64) and call `setParamNormalized()` for each; handle missing fields gracefully with defaults per the state contract (FR-016)
- [X] T024 [US1] Implement `Membrum::Processor::getState()` in `plugins/membrum/src/processor/processor.cpp`: write `kCurrentStateVersion` (int32), then 5 float64 normalized parameter values in order: Material, Size, Decay, StrikePosition, Level (FR-016)
- [X] T025 [US1] Implement `Membrum::Processor::setState()` in `plugins/membrum/src/processor/processor.cpp`: read version int32, read 5 float64 values (fewer fields use defaults, extra fields silently ignored), update the 5 `std::atomic<float>` members (FR-016)
- [X] T026 [US1] Verify all User Story 1 tests pass: build and run `membrum_tests`, confirm parameter and state tests pass, confirm no compiler warnings

### 3.3 Cross-Platform Verification

- [X] T027 [US1] Check `membrum_vst_tests.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage — if found, add to `-fno-fast-math` list in `plugins/membrum/tests/CMakeLists.txt`

### 3.4 Commit

- [ ] T028 [US1] Commit completed User Story 1 work: parameter registration, state round-trip, bus config, controller createView

**Checkpoint**: US1 tests pass. Plugin loads in DAW with correct parameter metadata. State round-trips exactly (SC-004).

---

## Phase 4: User Story 2 — MIDI Note Produces Drum Sound (Priority: P1)

**Goal**: MIDI note 36 at any velocity triggers the drum voice and produces audible, resonant, decaying audio output. Non-36 notes are silently ignored.

**Independent Test**: Inject note-on (note=36, velocity=100) into the processor, process 4096 samples, verify peak amplitude > -12 dBFS and output is not silent. Inject note-on for note=60, verify output is silent.

### 4.1 Tests for User Story 2 (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [X] T029 [US2] Create `plugins/membrum/tests/unit/processor/membrum_processor_tests.cpp` and write failing tests covering: (a) note-on (note=36, velocity=100) → after 512 samples, at least one output sample has amplitude > -12 dBFS (SC-001 soundcheck), (b) note-on (note=60) → all output samples are zero (FR-011), (c) note-on velocity=0 (MIDI note-off convention) → no voice triggered, output is silent (FR-013 / contracts/vst3-interface.md), (d) note-off after note-on → output decays naturally (non-zero for at least 100ms of samples), does NOT instantly drop to zero (FR-013), (e) rapid retrigger: two note-on events for note=36 within 50 samples → does not crash, second note-on restarts voice (FR-014), (f) process() with numSamples=0 → does not crash (edge case from spec), (g) note-on while voice is active (retrigger) → voice restarts, output contains new attack (FR-014), (h) **ADSR default values behavioral test**: trigger a note-on at t=0, send note-off at t=100ms, process until t=200ms — output must still be non-zero at 200ms (release phase active, R=300ms default per FR-038); process until t=600ms — output must be near-silent (< -60 dBFS) by 600ms (release completed). Verify tests FAIL before moving on.

### 4.2 Implementation for User Story 2

- [X] T030 [US2] Create `plugins/membrum/src/dsp/membrane_modes.h`: define `constexpr std::array<float, 16> kMembraneRatios`, `constexpr std::array<int, 16> kMembraneBesselOrder`, `constexpr std::array<float, 16> kMembraneBesselZeros` with values from data-model.md; add `float evaluateBesselJ(int m, float x)` using a polynomial approximation accurate for m=0..6, x in [0, 12.5] (FR-031, FR-035)
- [X] T031 [US2] Create `plugins/membrum/src/dsp/drum_voice.h`: define `DrumVoice` class inside `namespace Membrum`; members: `ImpactExciter exciter_`, `ModalResonatorBank modalBank_`, `ADSREnvelope ampEnvelope_`, cached params `material_`, `size_`, `decay_`, `strikePos_`, `level_`, `bool active_`; declare all methods from data-model.md
- [X] T032 [US2] Implement `DrumVoice::prepare(double sampleRate)` in `plugins/membrum/src/dsp/drum_voice.h`: call `exciter_.prepare(sampleRate, 0)`, `modalBank_.prepare(sampleRate)`, `ampEnvelope_.prepare(sampleRate)`; set ADSR defaults A=0ms, D=200ms, S=0.0, R=300ms; enable velocity scaling (FR-038)
- [X] T033 [US2] Implement `DrumVoice::noteOn(float velocity)`: (1) compute mode frequencies using current `size_` → `f0 = 500 * pow(0.1f, size_)`, then `freq[k] = f0 * kMembraneRatios[k]`; (2) compute per-mode amplitudes using `strikePos_` → `r_over_a = strikePos_ * 0.9f`, `amp[k] = abs(evaluateBesselJ(kMembraneBesselOrder[k], kMembraneBesselZeros[k] * r_over_a))`; (3) compute `brightness = material_`, `stretch = material_ * 0.3f`, `baseDecayTime = lerp(0.15f, 0.8f, material_) * (1.0f + 0.1f * size_)`, `decayTime = baseDecayTime * exp(lerp(log(0.3f), log(3.0f), decay_))`; (4) call `modalBank_.setModes(freqs, amps, 16, decayTime, brightness, stretch, 0.0f)`; (5) compute hardness/brightness for exciter from velocity; (6) call `exciter_.trigger(vel, hardness, 0.3f, brightness, 0.0f, 0.0f)`; (7) call `ampEnvelope_.setVelocity(velocity)` + `ampEnvelope_.gate(true)` (FR-030, FR-033, FR-034, FR-035, FR-037, FR-038)
- [X] T034 [US2] Implement `DrumVoice::noteOff()`: call `ampEnvelope_.gate(false)` (triggers release phase, no abrupt cut) (FR-013)
- [X] T035 [US2] Implement `DrumVoice::process() -> float`: (1) check `ampEnvelope_.isActive()` — if false, return 0.0f (early-out for silent voice) (FR-039); (2) `float exc = exciter_.process(0.0f)`; (3) `float body = modalBank_.processSample(exc)`; (4) `float env = ampEnvelope_.process()`; (5) return `body * env * level_`
- [X] T036 [US2] Implement `DrumVoice::isActive()`: return `ampEnvelope_.isActive()`
- [X] T037 [US2] Add `DrumVoice voice_` member to `Membrum::Processor`; implement `setupProcessing()` to call `voice_.prepare(processSetup.sampleRate)` and cache `sampleRate_`; implement `setActive()` to call `voice_.prepare(sampleRate_)` on activate (FR-039)
- [X] T038 [US2] Implement `Membrum::Processor::process()` in `plugins/membrum/src/processor/processor.cpp`: (a) handle `numSamples == 0` early-out; (b) read `inputParameterChanges` and update atomic params via `processParameterChanges()` helper; (c) iterate event list — on NoteOn note=36 velocity>0: trigger voice; on NoteOn note=36 velocity=0 or NoteOff note=36: release voice; all other events: ignore (FR-010, FR-011, FR-012, FR-013, FR-014, FR-015); (d) per-sample loop: `float s = voice_.process(); output[0][i] = s; output[1][i] = s;` (mono to stereo); (e) handle `data.numInputs == 0` (instrument, no audio in)
- [X] T039 [US2] Implement `Membrum::Processor::processParameterChanges()` helper: iterate parameter change queue, for each changed param ID read normalized value and store in corresponding `std::atomic<float>` member (FR-015)
- [X] T040 [US2] Verify all User Story 2 tests pass: build and run `membrum_tests`, confirm all MIDI and audio output tests pass, confirm no compiler warnings

### 4.3 Cross-Platform Verification

- [X] T041 [US2] Check `membrum_processor_tests.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage — add file to `-fno-fast-math` list in `plugins/membrum/tests/CMakeLists.txt` if found

### 4.4 Commit

- [X] T042 [US2] Commit completed User Story 2 work: DrumVoice, membrane_modes.h, processor MIDI + audio handling

**Checkpoint**: MIDI note 36 produces resonant, decaying audio. Non-36 notes produce silence. Rapid retrigger does not crash.

---

## Phase 5: User Story 3 — Velocity Affects Timbre and Volume (Priority: P1)

**Goal**: Soft hits (velocity ~30) produce a dark, lower-amplitude sound. Hard hits (velocity ~120) produce a bright, higher-amplitude sound. Both spectral centroid and amplitude differ measurably.

**Independent Test**: Process note-on at velocity=30 and velocity=127 using default parameters. Measure peak amplitude and spectral centroid. Velocity-127 must have > 6 dB higher amplitude (SC-005) and > 2x higher spectral centroid (SC-005).

### 5.1 Tests for User Story 3 (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [X] T043 [US3] Add velocity response tests to `plugins/membrum/tests/unit/processor/membrum_processor_tests.cpp`: (a) velocity=30 vs velocity=127: measure peak amplitude of first 2048 samples — velocity=127 must be > 6 dB louder (SC-005); (b) velocity=30 vs velocity=127: compute spectral centroid (weighted sum of frequency * magnitude) — velocity=127 must be > 2x higher centroid (SC-005); (c) velocity=1 (minimum): output is audible (non-zero) but very quiet (< -24 dBFS peak); (d) velocity=0: no voice triggered, output is silent (MIDI note-off per spec); (e) SC-007 check: process a note at velocity=64, default params, 4096 samples — scan for any NaN or Inf values in output (use bit manipulation for NaN check per CLAUDE.md cross-platform notes). Verify tests FAIL before moving on.

### 5.2 Implementation for User Story 3

- [X] T044 [US3] Read `plugins/membrum/src/dsp/drum_voice.h` and confirm that `DrumVoice::noteOn()` computes and passes velocity-mapped hardness and brightness to `exciter_.trigger()`: hardness = `lerp(0.3f, 0.8f, velocity)`, brightness = `lerp(0.15f, 0.4f, velocity)` per FR-037 and data-model.md Velocity Mapping section. If these mappings are absent, this is a defect — implement them now and re-run T043 tests to confirm they pass.
- [X] T045 [US3] Read `plugins/membrum/src/dsp/drum_voice.h` and confirm that `ADSREnvelope::setVelocity()` and `setVelocityScaling(true)` are called in `DrumVoice::noteOn()` so amplitude scales with velocity per FR-037. If these calls are absent, this is a defect — add them now and re-run T043 tests to confirm they pass.
- [X] T046 [US3] Verify all User Story 3 tests pass: build and run `membrum_tests`, confirm velocity amplitude and spectral centroid tests pass; if amplitude difference test fails, diagnose whether the ImpactExciter's internal `pow(velocity, 0.6)` curve is active or being overridden

### 5.3 Cross-Platform Verification

- [X] T047 [US3] SC-007 NaN/Inf check: because `membrum_processor_tests.cpp` uses bit manipulation for NaN detection (`std::isnan()` is not used due to `-ffast-math`), confirm the test file does NOT need `-fno-fast-math` — document decision in a comment in CMakeLists.txt

### 5.4 Commit

- [X] T048 [US3] Commit completed User Story 3 work: velocity-to-timbre and velocity-to-amplitude mapping verified

**Checkpoint**: SC-005 verified — velocity 30 vs 127 produces > 6 dB amplitude difference and > 2x spectral centroid difference.

---

## Phase 6: User Story 4 — Parameters Shape the Sound (Priority: P2)

**Goal**: Each of the 5 exposed parameters produces a measurable, distinct change in audio output when swept from default to extreme values.

**Independent Test**: For each parameter, process a note at default value and at the extreme value, compare output characteristics. All parameters should produce measurable differences (no silent failures).

### 6.1 Tests for User Story 4 (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [X] T049 [US4] Add parameter sweep tests to `plugins/membrum/tests/unit/processor/membrum_processor_tests.cpp`: (a) **Material**: compare spectral decay between material=0.0 and material=1.0 — at material=1.0 the RMS energy ratio of the last 512 samples vs first 512 samples must be higher than at material=0.0 (metallic rings longer); (b) **Size** at 0.0 vs 1.0: measure the frequency of the dominant peak in FFT — size=0.0 must yield a peak > 400 Hz, size=1.0 must yield a peak < 100 Hz (FR-033); (c) **Decay** at 0.0 vs 1.0: measure RMS of samples 2048–4096 — decay=1.0 must have > 3x higher RMS than decay=0.0 (longer ring); (d) **Strike Position** at 0.0 (center) vs 1.0 (edge): compare spectral content — edge must have measurably more high-frequency energy than center; (e) **Level** at 0.0 vs 1.0: level=0.0 must produce all-zero output (FR-036); (f) SC-007 at parameter extremes: process a note with all params at 0.0 simultaneously, then 1.0 simultaneously — scan 4096 samples for NaN/Inf. Verify tests FAIL before moving on.

### 6.2 Implementation for User Story 4

- [X] T050 [US4] Implement `DrumVoice::setMaterial(float v)` in `plugins/membrum/src/dsp/drum_voice.h` / `.cpp`: cache `material_ = v`; if `active_`: recompute brightness, stretch, base decay, effective decay and call `modalBank_.updateModes(freqs, amps, 16, newDecayTime, brightness, stretch, 0.0f)` — use `updateModes` (not `setModes`) to avoid clearing resonator state mid-note (quickstart note 5)
- [X] T051 [US4] Implement `DrumVoice::setSize(float v)`: cache `size_ = v`; if `active_`: recompute mode frequencies and call `modalBank_.updateModes(...)` with new frequencies (FR-033)
- [X] T052 [US4] Implement `DrumVoice::setDecay(float v)`: cache `decay_ = v`; if `active_`: recompute effective decay time and call `modalBank_.updateModes(...)` (FR-034)
- [X] T053 [US4] Implement `DrumVoice::setStrikePosition(float v)`: cache `strikePos_ = v`; if `active_`: recompute per-mode amplitudes and call `modalBank_.updateModes(...)` (FR-035)
- [X] T054 [US4] Implement `DrumVoice::setLevel(float v)`: cache `level_ = v`; used directly in `process()` already (no updateModes needed) (FR-036)
- [X] T055 [US4] Update `Membrum::Processor::processParameterChanges()` to forward each parameter update to the corresponding `DrumVoice` setter (`voice_.setMaterial()` etc.) in addition to updating the atomic members used for state save/load (FR-015)
- [X] T056 [US4] Verify all User Story 4 tests pass: build and run `membrum_tests`, confirm all parameter sweep tests pass; if Size frequency test fails, verify the `f0 = 500 * pow(0.1f, size)` formula produces the correct range
- [X] T057 [US4] SC-003 CPU check: build Release, run processor test that processes 44100 samples (1 second at 44.1 kHz) in a tight loop 10 times, measure wall-clock time — single voice should process 1 second of audio in well under 5ms (< 0.5% CPU at real-time rate); document measured value in test output

### 6.3 Cross-Platform Verification

- [X] T058 [US4] Verify FFT-based tests in parameter sweep section do not use `std::isnan`/`std::isfinite` — if they do, add to `-fno-fast-math` list in `plugins/membrum/tests/CMakeLists.txt`

### 6.4 Commit

- [X] T059 [US4] Commit completed User Story 4 work: all 5 parameter setters, processor forwarding to voice, CPU budget verified

**Checkpoint**: All 5 parameters produce measurable audio changes. Level=0.0 is silent. SC-003 CPU budget confirmed < 0.5%.

---

## Phase 7: User Story 5 — CI Builds and Validates (Priority: P2)

**Goal**: The CI pipeline builds Membrum on all three platforms, runs unit tests, uploads artifacts, and passes pluginval at strictness level 5.

**Independent Test**: Push to feature branch, observe CI workflow. All three platform jobs must turn green. Pluginval passes on Windows.

### 7.1 Tests for User Story 5 (Write FIRST — Must FAIL)

> No automated pre-implementation tests for CI configuration — the "test" is the CI run itself. Instead, verify the CI file changes are syntactically valid before pushing.

- [X] T060 [US5] Validate the CI YAML change locally: after editing `ci.yml`, run `yamllint` or equivalent syntax check to catch indentation/structure errors before pushing

### 7.2 Implementation for User Story 5

- [X] T061 [US5] Edit `.github/workflows/ci.yml` — add `membrum` to the `detect-changes` job: add `plugins/membrum/**` path filter and add `membrum` output variable following the exact Gradus pattern (FR-043)
- [X] T062 [US5] Edit `.github/workflows/ci.yml` — add Membrum to all three platform build jobs (Windows x64, macOS universal, Linux x64): add `Membrum` to the CMake build target list for each platform job (FR-040)
- [X] T063 [US5] Edit `.github/workflows/ci.yml` — add `membrum_tests` to the CTest run step in all three platform jobs (FR-041)
- [X] T064 [US5] Edit `.github/workflows/ci.yml` — add Membrum VST3 bundle upload artifact step for each platform, following the Gradus artifact upload pattern (FR-042)
- [X] T065 [US5] Edit `.github/workflows/ci.yml` — add AU validation step to the macOS job: `auval -v aumu Mbrm KrAt` after the build step (FR-044, SC-008)
- [X] T066 [US5] Edit `.github/workflows/ci.yml` — add `plugins/membrum/CMakeLists.txt` to the FetchContent dependency cache key (FR-043 / R-006)
- [X] T067 [US5] Run pluginval locally on Windows: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"` — capture output to a log file first run, fix any failures (SC-001)
- [X] T068 [US5] Verify all User Story 5 CI tasks are correct: review the diff to `ci.yml` for correct indentation and YAML structure, push to feature branch, confirm CI workflow triggers for `plugins/membrum/**` changes

### 7.3 Commit

- [ ] T069 [US5] Commit completed User Story 5 work: CI workflow updates, pluginval passing

**Checkpoint**: CI builds Membrum on all three platforms. Tests pass in CI. Pluginval passes at strictness 5 (SC-001).

---

## Phase 8: Polish and Cross-Cutting Concerns

**Purpose**: Edge cases, NaN protection, and items that span multiple user stories.

- [X] T070 [P] Harden `DrumVoice::process()` against denormals: add FTZ/DAZ enablement in `Membrum::Processor::setupProcessing()` on x86 (`_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON)`) per CLAUDE.md cross-platform notes (SC-007)
- [X] T071 [P] Add edge case tests to `membrum_processor_tests.cpp`: (a) extreme sample rates — call `voice_.prepare(22050.0)`, trigger note, verify no crash and no NaN; repeat for 96000.0 and 192000.0 (spec edge case); (b) all parameters at 0.0 simultaneously — trigger note, process 2048 samples, scan for NaN/Inf; (c) all parameters at 1.0 simultaneously — same check; (d) forward-compatible state load: write a binary blob with version=1 and only 4 float64 parameter values (omitting the 5th), call `setState()` — processor must not crash and the 5th parameter must retain its default value; (e) future-version state load: write a binary blob with version=2 and 7 float64 values, call `setState()` with a version=1 parser — processor must not crash and must correctly load the first 5 known values while silently ignoring the extra 2
- [X] T072 Verify `Membrum::Processor::process()` handles `data.numInputs == 0` — confirm the implementation does not attempt to dereference input bus data when it is absent (spec edge case, AU wrapper lesson from MEMORY.md)
- [X] T073 [P] Run `membrum_tests` with all edge case tests and confirm SC-007: no NaN/Inf in output across the full parameter range
- [X] T074 Verify SC-002: instrument startup latency — write a test that calls `voice_.prepare(44100.0)`, injects a note-on event in sample 0 of the first `process()` block, and asserts that at least one output sample in that same block has amplitude > 0.0f. This proves audio begins within the first process() call (no deferred init). Also add a `// SC-002: zero-latency by design — DrumVoice.prepare() completes all allocation before process()` comment in processor.cpp at the `setupProcessing()` call site.

---

## Phase 9: Static Analysis

**Purpose**: Run clang-tidy on all new Membrum source files before final verification.

- [X] T075 Generate `compile_commands.json` via Ninja preset if not already present: `"C:/Program Files/CMake/bin/cmake.exe" --preset windows-ninja`
- [X] T076 Run clang-tidy on Membrum target: `./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja` — capture output to a log file on first run, inspect the log afterward (per MEMORY.md feedback)
- [X] T077 Fix ALL clang-tidy errors in `plugins/membrum/src/` (blocking issues — no exceptions)
- [X] T078 Fix ALL clang-tidy warnings in `plugins/membrum/src/` (or add NOLINT comment with justification for any intentionally ignored DSP-style warning)
- [ ] T079 Commit static analysis fixes

---

## Phase 10: Final Documentation

**Purpose**: Update living architecture documentation.

- [X] T080 Update `specs/_architecture_/` (or equivalent layer docs) with Membrum's plugin-local DSP components: add `DrumVoice` entry (purpose, public API, location `plugins/membrum/src/dsp/drum_voice.h`, "when to use"); note that `membrane_modes.h` contains the Bessel constants and evaluator for the 16-mode circular membrane model
- [ ] T081 Commit architecture documentation updates

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion.

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed.

### 11.1 Requirements Verification

- [X] T082 Review ALL FR-001 through FR-051 requirements from `specs/136-membrum-phase1-scaffold/spec.md` against actual implementation files — for each FR, record the file path and line number of the satisfying code
- [X] T083 Review ALL SC-001 through SC-008 success criteria — for each SC, record the test name, actual measured value, and spec threshold (e.g., SC-003: actual CPU% vs < 0.5%)
- [X] T084 Search for cheating patterns in `plugins/membrum/src/`: no `// placeholder`, `// TODO`, or `// stub` comments; no test thresholds relaxed from spec; no features quietly removed
- [X] T085 Update `specs/136-membrum-phase1-scaffold/spec.md` "Implementation Verification" compliance table with concrete evidence for every row (file path + line number for FRs, test name + actual value for SCs)

### 11.2 Honest Self-Check

Answer these questions. If ANY answer is "yes", do NOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T086 All self-check questions answered "no" (or gaps documented honestly in spec.md)

### 11.3 Final Commit

- [ ] T087 Commit all remaining spec work to feature branch `136-membrum-phase1-scaffold`
- [X] T088 Run full test suite one final time: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure -R membrum_tests` — confirm all tests pass

**Checkpoint**: Spec implementation honestly complete. Compliance table filled with concrete evidence.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 completion — BLOCKS all user story phases
- **Phase 3 (US1 — Plugin Loads)**: Depends on Phase 2 — must complete before Phases 4–7
- **Phase 4 (US2 — MIDI Sound)**: Depends on Phase 2 — can start after Phase 2 (DrumVoice scaffolding can begin independently of US1 parameter work)
- **Phase 5 (US3 — Velocity)**: Depends on Phase 4 (needs DrumVoice noteOn with exciter)
- **Phase 6 (US4 — Parameters)**: Depends on Phase 4 (needs DrumVoice setters and updateModes)
- **Phase 7 (US5 — CI)**: Depends on Phases 3–6 all building and tests passing
- **Phase 8 (Polish)**: Depends on Phases 3–6
- **Phase 9 (Clang-Tidy)**: Depends on all source files being written (Phases 3–8)
- **Phase 10 (Docs)**: Depends on implementation complete
- **Phase 11 (Verification)**: LAST — depends on everything else

### User Story Dependencies

- **US1 (Plugin Loads)**: Independent of DSP work — controller/parameter registration can proceed in parallel with US2 DrumVoice work
- **US2 (MIDI Sound)**: Builds on foundational scaffolding — requires DrumVoice, processor MIDI handling
- **US3 (Velocity)**: Depends on US2 (needs DrumVoice noteOn with exciter trigger) — verify US2 velocity mappings are correct
- **US4 (Parameters)**: Depends on US2 (needs DrumVoice parameter setters hooked to updateModes)
- **US5 (CI)**: Depends on US1–US4 all building and passing

### Within Each User Story

1. **Tests FIRST** — write tests, verify they FAIL (Principle XII)
2. Implementation tasks
3. **Verify tests pass** — no exceptions
4. Cross-platform IEEE 754 compliance check
5. **Commit** — mandatory end of each user story

### Parallel Opportunities

- T004, T005, T006, T007 (Phase 1) — different files, can run in parallel
- T021 (US1 tests) and T029 (US2 tests) — after Phase 2, can be written in parallel since they target different test files
- T030 (membrane_modes.h + DrumVoice skeleton) can proceed in parallel with T022 (Controller::initialize) — different files
- T061–T066 (CI edits) — sequential edits to the same file, must be done in order

---

## Parallel Execution Example: Phase 3+4 (After Phase 2)

After Phase 2 foundational stubs are complete, two streams can run in parallel:

**Stream A — US1 (Plugin Loads)**:
```
T021 Write vst tests → T022 Controller::initialize() → T023 setComponentState() →
T024 Processor::getState() → T025 Processor::setState() → T026 Verify → T027 Cross-platform → T028 Commit
```

**Stream B — US2 (MIDI Sound)**:
```
T029 Write processor tests → T030 membrane_modes.h → T031 DrumVoice header →
T032 prepare() → T033 noteOn() → T034 noteOff() → T035 process() →
T036 isActive() → T037 Add to Processor → T038 Processor::process() →
T039 processParameterChanges() → T040 Verify → T041 Cross-platform → T042 Commit
```

---

## Implementation Strategy

### MVP First (User Stories 1 + 2)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational stubs — CRITICAL, blocks everything
3. Complete Phase 3: US1 (Plugin Loads) — scaffold foundation
4. Complete Phase 4: US2 (MIDI Sound) — sound comes out
5. **STOP and VALIDATE**: Run pluginval, confirm plugin loads in DAW, confirm MIDI note 36 produces audio
6. Proceed to US3, US4, US5 as incremental layers

### Incremental Delivery

1. Setup + Foundational → compilable plugin bundle
2. US1 → plugin appears in DAW, parameters visible, state round-trips (SC-004)
3. US2 → MIDI note 36 produces drum sound, note-off decays naturally
4. US3 → velocity controls timbre and volume (SC-005)
5. US4 → all 5 parameters shape the sound distinctly (SC-003, SC-007)
6. US5 → CI green on all platforms (SC-001, SC-006, SC-008)

---

## Notes

- [P] tasks = different files, no inter-task dependencies — safe to parallelize
- [US1]–[US5] labels map tasks to user stories for traceability
- Tests reference exact file paths from `quickstart.md` and `plan.md`
- `setModes()` clears resonator state — only call on note-on. Use `updateModes()` for parameter changes during sustain (critical distinction from quickstart.md note 5)
- Bessel amplitude calculation: center strike (r/a=0) excites only m=0 modes; near-edge excites all modes
- State format: version int32 + 5x float64, 44 bytes total (data-model.md)
- NaN/Inf detection in tests: use bit manipulation, not `std::isnan()`, to avoid `-ffast-math` breaking the check (CLAUDE.md cross-platform notes)
- AU wrapper: instrument has 0 audio inputs — `process()` must handle `data.numInputs == 0` gracefully (AU wrapper lesson in MEMORY.md)
- Skills auto-load when needed: `testing-guide`, `vst-guide`
- **NEVER claim completion if ANY requirement is not met** — document gaps honestly in spec.md compliance table
