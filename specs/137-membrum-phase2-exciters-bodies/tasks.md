---
description: "Task list for Membrum Phase 2 — 5 Exciter Types + 5 Body Models (Swap-In Architecture)"
---

# Tasks: Membrum Phase 2 — Exciters, Bodies, Tone Shaper, Unnatural Zone

**Input**: Design documents from `specs/137-membrum-phase2-exciters-bodies/`
**Prerequisites**: spec.md, plan.md, research.md, data-model.md, contracts/, quickstart.md
**Branch**: `137-membrum-phase2-exciters-bodies`

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation. Per the canonical todo list in CLAUDE.md: Write failing test → Implement → Fix all warnings → Verify tests pass → Run pluginval (if plugin source changed) → Run clang-tidy → Commit.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

**TONE SHAPER STAGE ORDER NOTE**: The stage order implemented in tasks is `Drive → Wavefolder → DCBlocker → SVF Filter` (the plan's order), NOT the order in FR-040's literal wording (`SVF Filter → Drive → Wavefolder`). This deviation is intentional and justified in `research.md §8` ("Drive → Wavefolder → Filter is the Serge/Buchla west-coast signal flow; the filter smooths out aliasing residues from the wavefolder harmonics"). Every task that touches tone shaper implementation honors the plan's order.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow.

### Required Steps for EVERY Implementation Task

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Fix All Compiler Warnings**: Zero warnings required (CLAUDE.md policy)
4. **Verify Tests Pass**: Run `build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5`
5. **Run Pluginval** (if plugin source changed): `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"`
6. **Run Clang-Tidy**: `./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja`
7. **Commit**

### Cross-Platform Compatibility (After Each Phase)

The VST3 SDK enables `-ffast-math` globally. After implementing tests that use `std::isnan`, `std::isfinite`, or `std::isinf`:
- Add the test source file to `-fno-fast-math` in `plugins/membrum/tests/CMakeLists.txt`
- Use bit manipulation for NaN detection (not `std::isnan`) per CLAUDE.md cross-platform notes
- Use `Approx().margin()` for floating-point comparisons, never exact equality

### NaN Detection Pattern (Required)

Use bit manipulation because `-ffast-math` breaks `std::isnan()`:

```cpp
// Phase 2 canonical NaN/Inf check (bit manipulation, -ffast-math safe)
auto isFiniteSample = [](float x) {
    uint32_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
};
```

---

## Phase 1: Setup

**Purpose**: Extend the build system, bump version, create new directory structure, and extend `plugin_ids.h` with Phase 2 parameter IDs. None of these tasks produce audio, but they are prerequisites for compilation of all Phase 2 code.

- [X] T001 Bump `plugins/membrum/version.json` from `{"major":0,"minor":1,"patch":0}` to `{"major":0,"minor":2,"patch":0}` — this is the ONLY file to edit for the version bump per CLAUDE.md policy
- [X] T002 Create new source directories: `plugins/membrum/src/dsp/exciters/`, `plugins/membrum/src/dsp/bodies/`, `plugins/membrum/src/dsp/unnatural/` — add `.gitkeep` files so they appear in git
- [X] T003 Create new test directories: `plugins/membrum/tests/unit/architecture/`, `plugins/membrum/tests/unit/exciters/`, `plugins/membrum/tests/unit/bodies/`, `plugins/membrum/tests/unit/tone_shaper/`, `plugins/membrum/tests/unit/unnatural/`, `plugins/membrum/tests/approval/`, `plugins/membrum/tests/perf/`, `plugins/membrum/tests/golden/`
- [X] T004 Extend `plugins/membrum/src/plugin_ids.h` with the full Phase 2 parameter ID enum exactly as defined in `data-model.md §9`: IDs 200–244, `kCurrentStateVersion = 2`, all naming following `k{Section}{Parameter}Id` convention; verify no ID collisions with Phase 1 IDs 100–104
- [X] T005 Update `plugins/membrum/CMakeLists.txt` to add all new Phase 2 source files in `src/dsp/exciters/`, `src/dsp/bodies/`, `src/dsp/unnatural/`, and the modified `src/dsp/drum_voice.h` — add stub `.cpp` files where needed (header-only classes do not need `.cpp` entries, but `tone_shaper.cpp` if split from header does)
- [X] T006 Update `plugins/membrum/tests/CMakeLists.txt` to include all new test files listed in `plan.md §Project Structure` — add the new test directories and stub source files so the test binary compiles empty before implementation
- [X] T007 Verify CMake configure and stub build succeed: `"C:/Program Files/CMake/bin/cmake.exe" --preset windows-x64-release && "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests` — fix all compile errors before proceeding

**Checkpoint**: All directories exist. `plugin_ids.h` has Phase 2 IDs. CMake configures without errors. `membrum_tests` binary compiles (may have zero tests at this point).

---

## Phase 2: Foundational — Architecture Refactor (sub-phase 2.A)

**Purpose**: Replace Phase 1's hardcoded `ImpactExciter + ModalResonatorBank` `DrumVoice` with `ExciterBank + BodyBank + ToneShaper + UnnaturalZone` skeletons. The only exciter active after this phase is `ImpulseExciter` (carry-over from Phase 1). The only body active is `MembraneBody`. All Phase 1 acceptance tests MUST continue to pass. This phase also adds state version 2 with backward-compat loading and the Phase 1 regression golden test.

**BLOCKING**: Nothing in Phase 3 through Phase 9 can begin until all tasks in Phase 2 are green and Phase 1 tests are still passing.

### 2.1 Foundational Tests (Write FIRST — Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [X] T008 Write failing test in `plugins/membrum/tests/unit/architecture/test_exciter_bank.cpp` covering: (a) `ExciterBank` compiles with the variant type defined in `data-model.md §2.7`; (b) `setExciterType(ExciterType::Impulse)` + `trigger(0.5f)` + 512 samples `process(0.0f)` produces non-zero, finite output; (c) `process()` returns finite values (NaN/Inf check via bit manipulation); (d) allocation-detector wraps `trigger()` + `process()` — asserts zero heap allocation; (e) `isActive()` returns false when envelope decays below threshold. Verify tests FAIL before moving on.
- [X] T009 Write failing test in `plugins/membrum/tests/unit/architecture/test_body_bank.cpp` covering: (a) `BodyBank` compiles with the variant type in `data-model.md §3.8`; (b) `setBodyModel(BodyModelType::Membrane)` + `configureForNoteOn(params, 160.0f)` + 512 samples `processSample(sharedBank, 0.1f)` returns finite values; (c) `setBodyModel()` while active sets `pendingType_` only — current body continues processing; (d) body-model switch deferred to next `configureForNoteOn()` call — asserts NO crash, NO NaN, NO allocation during the deferral; (e) allocation-detector wraps `configureForNoteOn()` + `processSample()`. Verify tests FAIL.
- [X] T010 Write failing test in `plugins/membrum/tests/approval/test_phase1_regression.cpp` covering: (a) generate (or load from `tests/golden/phase1_default.bin`) a 500 ms reference waveform from the Phase 1 default patch (Impulse + Membrane, default params, no Tone Shaper, no Unnatural Zone); (b) process the same patch through Phase 2's refactored `DrumVoice` and assert RMS difference ≤ −90 dBFS over the full 500 ms (SC-005 regression guarantee FR-095); (c) state version-1 file loaded via `setState()` produces Phase-2-default values for new parameters (FR-082). Verify tests FAIL. The golden-generation section of `plugins/membrum/tests/approval/test_phase1_regression.cpp` MUST carry the Catch2 tag `[generate_golden]` — e.g., `TEST_CASE("Generate Phase 1 golden reference", "[generate_golden]")` — so that T025's tag filter `"[generate_golden]"` selects only this test case and does not accidentally regenerate goldens during normal test runs.
- [X] T011 Write failing test in `plugins/membrum/tests/unit/vst/test_state_roundtrip_v2.cpp` covering: (a) state written with `kCurrentStateVersion = 2` and all 34 parameters round-trips bit-exactly via `getState()`/`setState()` (SC-006); (b) a Phase-1 state blob (version=1, 5 parameters) loads successfully with Phase-2 defaults for new parameters (FR-082). Verify tests FAIL.

### 2.2 Foundational Implementation

- [X] T012 Create `plugins/membrum/src/dsp/exciter_type.h`: define `enum class ExciterType : int { Impulse=0, Mallet=1, NoiseBurst=2, Friction=3, FMImpulse=4, Feedback=5, kCount=6 }` exactly per `data-model.md §1`; add `Membrum` namespace; include a `static_assert(static_cast<int>(ExciterType::kCount) == 6)` invariant
- [X] T013 Create `plugins/membrum/src/dsp/body_model_type.h`: define `enum class BodyModelType : int { Membrane=0, Plate=1, Shell=2, String=3, Bell=4, NoiseBody=5, kCount=6 }` exactly per `data-model.md §1`; add `Membrum` namespace; include static invariant
- [X] T014 Create `plugins/membrum/src/dsp/voice_common_params.h`: define `struct VoiceCommonParams` exactly per `data-model.md §3.1` with members `material, size, decay, strikePos, level, modeStretch, decaySkew`; all float; default constructor sets all to zero except `modeStretch = 1.0f`
- [X] T015 Create stub exciter headers under `plugins/membrum/src/dsp/exciters/`: `impulse_exciter.h`, `mallet_exciter.h`, `noise_burst_exciter.h`, `friction_exciter.h`, `fm_impulse_exciter.h`, `feedback_exciter.h` — each declares the struct with the structural API from `exciter_contract.md` (`prepare`, `reset`, `trigger`, `release`, `process`, `isActive`) as stub declarations; `ImpulseExciter` and `MalletExciter` include `Krate::DSP::ImpactExciter` member per `data-model.md §2`
- [X] T016 Create stub body headers under `plugins/membrum/src/dsp/bodies/`: `membrane_body.h`, `plate_body.h`, `shell_body.h`, `string_body.h` (includes `WaveguideString`), `bell_body.h`, `noise_body.h` (includes `NoiseOscillator` + `SVF` + `ADSREnvelope`) — each declares the struct with the structural API from `body_contract.md` (`prepare`, `reset`, `configureForNoteOn`, `processSample`) as stub declarations; `StringBody` has `Krate::DSP::WaveguideString string_` member; `NoiseBody` has all members from `data-model.md §3.7`
- [X] T017 [P] Create `plugins/membrum/src/dsp/exciter_bank.h`: define `class ExciterBank` per `data-model.md §2.7` — `std::variant<ImpulseExciter, MalletExciter, NoiseBurstExciter, FrictionExciter, FMImpulseExciter, FeedbackExciter> active_`, `currentType_`, `pendingType_`; implement `setExciterType()` to write `pendingType_` only; implement `trigger()` to swap variant on type mismatch then call the active backend's `trigger()`; implement `process()` via `std::visit` (with index-based `switch` fallback per `research.md §1`); implement `prepare()`, `reset()`, `release()`, `isActive()`
- [X] T018 [P] Create `plugins/membrum/src/dsp/body_bank.h`: define `class BodyBank` per `data-model.md §3.8` — `std::variant<MembraneBody, PlateBody, ShellBody, StringBody, BellBody, NoiseBody> active_`, `Krate::DSP::ModalResonatorBank sharedBank_`, `currentType_`, `pendingType_`, `lastOutput_`; implement `setBodyModel()` to write `pendingType_` only; implement `configureForNoteOn()` to apply deferred switch then call the active backend's `configureForNoteOn(sharedBank_, params, pitchHz)`; implement `processSample()` via `std::visit`; store `lastOutput_` after each sample; expose `getLastOutput()`
- [X] T019 [P] Create `plugins/membrum/src/dsp/tone_shaper.h`: declare `class ToneShaper` stub exactly per `data-model.md §6` — all member DSP objects (`SVF filter_`, `ADSREnvelope filterEnv_`, `Waveshaper drive_`, `Wavefolder wavefolder_`, `DCBlocker dcBlocker_`, `MultiStageEnvelope pitchEnv_`) and all public methods; stub `processSample()` to return input unchanged; stub `processPitchEnvelope()` to return `pitchEnvStartHz_` constant; mark `isBypassed()` return `true` — allows Phase 1 regression test to pass with tone shaper as a no-op
- [X] T020 [P] Create stubs for Unnatural Zone headers: `plugins/membrum/src/dsp/unnatural/unnatural_zone.h` (declares `class UnnaturalZone` per `data-model.md §7`), `material_morph.h`, `mode_inject.h`, `nonlinear_coupling.h` — all `process()` methods return input unchanged; all bypass/default conditions evaluate to no-op
- [X] T021 [P] Create `plugins/membrum/src/dsp/bodies/membrane_mapper.h`: extract Phase 1's inline `DrumVoice::noteOn` mapping code verbatim into `struct MembraneMapper { static MapperResult map(const VoiceCommonParams& params, float pitchHz) noexcept; }` exactly as defined in `data-model.md §4`; the implementation MUST produce bit-identical output to Phase 1's inline code for the same input (FR-031 regression guarantee)
- [X] T022 Refactor `plugins/membrum/src/dsp/drum_voice.h` to match the Phase 2 data-model design (data-model.md §8): replace `ImpactExciter exciter_` + `ModalResonatorBank modalBank_` members with `ExciterBank exciterBank_` + `BodyBank bodyBank_` + `ToneShaper toneShaper_` + `UnnaturalZone unnaturalZone_` + `VoiceCommonParams params_`; add `setExciterType()`, `setBodyModel()` passthroughs; preserve Phase 1 public API (`prepare`, `noteOn(velocity)`, `noteOff()`, `process()`, `isActive()`, 5 Phase-1 setters) with identical signatures (FR-007); update `process()` to implement the integration diagram from `unnatural_zone_contract.md` with Tone Shaper stub and Unnatural Zone stub (all no-ops at this stage)
- [X] T023 Implement state version 2 in `plugins/membrum/src/processor/processor.cpp` `getState()`/`setState()`: follow the `data-model.md §10` binary layout exactly (version int32, 5 Phase-1 float64, 2 int32 selectors, N Phase-2 float64); backward-compat loader fills Phase-2 defaults when `version == 1` (FR-082); update `processParameterChanges()` to handle new parameter IDs 200–244 and route them to `voice_.setExciterType()`, `voice_.setBodyModel()`, and `voice_.toneShaper()` / `voice_.unnaturalZone()` setters
- [X] T024 Register all 29 Phase-2 parameters in `plugins/membrum/src/controller/controller.cpp` `initialize()`: use `StringListParameter` for `kExciterTypeId` (6 choices: "Impulse,Mallet,NoiseBurst,Friction,FMImpulse,Feedback") and `kBodyModelId` (6 choices: "Membrane,Plate,Shell,String,Bell,NoiseBody"); use `RangeParameter` for all continuous parameters per `vst_parameter_contract.md`; follow ID naming from `plugin_ids.h`
- [X] T025 Generate the Phase 1 golden reference binary: run `build/windows-x64-release/bin/Release/membrum_tests.exe "[generate_golden]"` to produce `plugins/membrum/tests/golden/phase1_default.bin`, commit it; the regression test in T010 will compare against it (FR-095) (test case tagged `[generate_golden]` per T010 notes)

### 2.3 Foundational Verification

- [X] T026 Build and run all tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests && build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5` — ALL Phase 1 tests must continue to pass (FR-007); architecture tests (T008–T009) must pass; state round-trip test (T011) must pass; Phase 1 regression test (T010) must pass within −90 dBFS
- [X] T027 Check all new test files for `std::isnan`/`std::isfinite` usage — add to `-fno-fast-math` list in `plugins/membrum/tests/CMakeLists.txt` if found
- [X] T028 Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"` — must pass with zero errors; capture output to `job-logs.txt` first, inspect the log (do NOT re-run to grep)

### 2.4 Commit

- [X] T029 Commit: "membrum: Phase 2.A skeleton — swap-in architecture with ExciterBank + BodyBank + ToneShaper + UnnaturalZone stubs; state version 2; Phase 1 regression golden reference"

**Checkpoint (BLOCKING gate)**: ALL Phase 1 tests still pass. Architecture tests pass. State round-trip passes. Phase 1 golden reference committed. Pluginval passes. Nothing in Phase 3–9 may start until this checkpoint is confirmed.

---

## Phase 3: User Story 1 — Select Any of 6 Exciter Types (Priority: P1)

**Goal**: Implement the 5 new exciter backends (MalletExciter, NoiseBurstExciter, FrictionExciter, FMImpulseExciter, FeedbackExciter). Each produces audibly distinct excitation character. All 6 exciters are selectable via host parameter and round-trip through state save/load.

**Dependencies**: Phase 2 Foundational must be complete (ExciterBank stub in place).

**Independent Test**: For each of the 6 exciter types, trigger MIDI note 36 at velocity 100, process 500 ms. Verify: non-silent (peak > −60 dBFS), no NaN/Inf (bit-manipulation check), peak ≤ 0 dBFS, spectral centroid ratio ≥ 2.0 between velocity 30 and velocity 127 (SC-004), zero heap allocation.

### 3.1 Tests for User Story 1 (Write FIRST — Must FAIL)

- [X] T030 [US1] Write failing tests in `plugins/membrum/tests/unit/exciters/test_impulse_exciter.cpp` covering the exciter contract invariants for `ImpulseExciter` per `exciter_contract.md §Test coverage requirements`: (1) allocation-detector wraps `trigger()` + `process()` — zero heap; (2) velocity 0.23 vs 1.0 spectral centroid ratio ≥ 2.0 (SC-004); (3) 1 s output finite (no NaN/Inf, peak ≤ 1.0); (4) retrigger safety — two rapid triggers, no crash, no NaN; (5) reset idempotence; (6) sample-rate change (`prepare(44100)` then `prepare(96000)` then `trigger()`, reasonable output). Verify tests FAIL.
- [X] T031 [US1] [P] Write failing tests in `plugins/membrum/tests/unit/exciters/test_mallet_exciter.cpp` covering the same 6 contract invariants for `MalletExciter` plus: (7) at same velocity, first-2-ms spectral centroid of Mallet is measurably lower than Impulse (acceptance scenario US1-2 — softer, rounded hit). Verify tests FAIL.
- [X] T032 [US1] [P] Write failing tests in `plugins/membrum/tests/unit/exciters/test_noise_burst_exciter.cpp` covering the same 6 contract invariants for `NoiseBurstExciter` plus: (7) first-20-ms spectral centroid > 2× the Impulse centroid at same velocity (acceptance scenario US1-3 — broadband noise dominates). Verify tests FAIL.
- [X] T033 [US1] [P] Write failing tests in `plugins/membrum/tests/unit/exciters/test_friction_exciter.cpp` covering the same 6 contract invariants for `FrictionExciter` plus: (7) output contains a non-monotonic energy envelope over first 50 ms (stick-slip signature, acceptance scenario US1-4); (8) bow release auto-triggers within ≤ 50 ms (transient mode only). Verify tests FAIL.
- [X] T034 [US1] [P] Write failing tests in `plugins/membrum/tests/unit/exciters/test_fm_impulse_exciter.cpp` covering the same 6 contract invariants for `FMImpulseExciter` plus: (7) first-50-ms output contains inharmonic sidebands at carrier:modulator ratio 1:1.4 (acceptance scenario US1-5); (8) modulation index envelope decays faster than amplitude envelope (measure centroid vs amplitude over first 100 ms). Verify tests FAIL.
- [X] T035 [US1] [P] Write failing tests in `plugins/membrum/tests/unit/exciters/test_feedback_exciter.cpp` covering the same 6 contract invariants for `FeedbackExciter` plus: (7) with non-zero bodyFeedback, output self-sustains longer than without feedback; (8) peak ≤ 0 dBFS for any bodyFeedback in [−1, +1] at all velocities (energy limiter, SC-008, acceptance scenario US1-6); (9) allocation-detector wraps the full `process(bodyFeedback)` path. Verify tests FAIL.
- [X] T036 [US1] Write failing test in `plugins/membrum/tests/unit/exciters/test_velocity_mapping.cpp` covering FR-016 and SC-004 extended to all 6 exciter types: for each exciter, process at velocity=0.23 and velocity=1.0, assert spectral centroid ratio ≥ 2.0. Verify tests FAIL.

### 3.2 Implementation for User Story 1

- [X] T037 [US1] [P] Implement `ImpulseExciter` fully in `plugins/membrum/src/dsp/exciters/impulse_exciter.h`: `trigger(velocity)` maps to `core_.trigger(velocity, lerp(0.3f, 0.8f, velocity), 0.3f, lerp(0.15f, 0.4f, velocity), 0.0f, 0.0f)` — preserving Phase 1 parameters exactly (FR-010); `process(bodyFeedback)` calls `core_.process(0.0f)` (ignores feedback per contract); `isActive()` wraps `core_.isActive()`; `prepare()` calls `core_.prepare(sampleRate, voiceId)` and `core_.reset()`
- [X] T038 [US1] [P] Implement `MalletExciter` in `plugins/membrum/src/dsp/exciters/mallet_exciter.h`: same `ImpactExciter` backend with FR-011 parameter mapping — contact duration `lerp(8 ms, 1 ms, velocity)` (store as a sample count and apply via hardness exponent alpha `lerp(1.5f, 4.0f, velocity)`); SVF brightness rising with velocity; produces measurably lower spectral centroid than ImpulseExciter at same velocity per US1-2
- [X] T039 [US1] [P] Implement `NoiseBurstExciter` in `plugins/membrum/src/dsp/exciters/noise_burst_exciter.h`: `NoiseOscillator + SVF + linear decay envelope` per `data-model.md §2.3`; `trigger(velocity)` sets `burstSamplesRemaining_` from `lerp(15 ms, 2 ms, velocity)`, cutoff from `lerp(200 Hz, 5000 Hz, velocity)`, amplitude from velocity; `process()` early-exits when `burstSamplesRemaining_ <= 0` returning 0.0f; per FR-012, uses `NoiseOscillator` (NOT `NoiseGenerator`)
- [X] T040 [US1] [P] Implement `FrictionExciter` in `plugins/membrum/src/dsp/exciters/friction_exciter.h`: wraps `BowExciter` + `ADSREnvelope bowEnvelope_`; `trigger(velocity)` calls `core_.trigger(velocity)`, `core_.setPressure(lerp(0.1f, 0.5f, velocity))`, `core_.setSpeed(lerp(0.2f, 0.8f, velocity))`, starts bowEnvelope with A=1ms, D=40ms, S=0, R=5ms; `process()` calls `core_.setEnvelopeValue(bowEnvelope_.process())` then `core_.process(0.0f)`; auto-releases bow at envelope completion; transient-only per FR-013
- [X] T041 [US1] [P] Implement `FMImpulseExciter` in `plugins/membrum/src/dsp/exciters/fm_impulse_exciter.h`: two `FMOperator` instances (carrier + modulator) per `data-model.md §2.5`; `modulatorRatio_ = 1.4f` (Chowning bell default, FR-014); `ampEnv_` gates carrier amplitude ≤ 100 ms; `modIndexEnv_` decays modulation index faster than `ampEnv_` — use a shorter time constant for modIndexEnv (e.g., 30 ms vs 80 ms default); `trigger(velocity)` sets modulation index from velocity (`lerp(0.5f, 3.0f, velocity)`)
- [X] T042 [US1] [P] Implement `FeedbackExciter` in `plugins/membrum/src/dsp/exciters/feedback_exciter.h`: implement the energy-limiter topology from `research.md §3` and `data-model.md §2.6`; `process(bodyFeedback)` computes `energyGain = 1.0f - clamp(energyFollower_.processSample(abs(bodyFeedback)) - kEnergyThreshold, 0.0f, 1.0f)`, then `tanhADAA_.process(filter_.process(bodyFeedback * feedbackAmount_ * energyGain))`, then `dcBlocker_.process(result)`; `feedbackAmount_` set from velocity; guarantees peak ≤ 0 dBFS (SC-008, FR-015)
- [X] T043 [US1] Integrate all 6 exciter backends into `ExciterBank::trigger()` and `ExciterBank::process()` in `plugins/membrum/src/dsp/exciter_bank.h`: the variant dispatch (via `std::visit` or index-based `switch` per `research.md §1`) must be per-block (not per-sample); verify that ExciterType parameter (ID 200) correctly switches between all 6 backends via the deferred swap pattern

### 3.3 Verification for User Story 1

- [X] T044 [US1] Build and run exciter tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests && build/windows-x64-release/bin/Release/membrum_tests.exe "Exciter*" 2>&1 | tail -5` — all exciter contract tests and velocity mapping tests must pass (SC-004)
- [X] T045 [US1] Add any new test files using NaN bit-manipulation checks to `-fno-fast-math` in `plugins/membrum/tests/CMakeLists.txt` — N/A: new exciter tests use `isFiniteSample()` bit-manipulation via memcpy (no `std::isnan`/`std::isfinite`), so `-fno-fast-math` is not required.
- [X] T046 [US1] Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"` — capture output to log file first, must pass zero errors/warnings

### 3.4 Commit

- [ ] T047 [US1] Commit: "membrum: Phase 2.B exciter types — Mallet, NoiseBurst, Friction, FMImpulse, Feedback backends with velocity-spectral-centroid tests" — deferred: work is ready but no commit created (commits only on explicit user request).

**Checkpoint**: All 6 exciter types produce non-silent, non-NaN, peak-safe audio. SC-004 centroid ratio ≥ 2.0 passes for every exciter type.

---

## Phase 4: User Story 2 — Select Any of 6 Body Models (Priority: P1)

**Goal**: Implement the 5 new body backends (PlateBody, ShellBody, StringBody, BellBody, NoiseBody) with their mode-ratio tables and per-body mapping helpers. Each body produces audibly distinct resonance. The Membrane body (Phase 1) continues to pass all existing tests.

**Dependencies**: Phase 2 Foundational must be complete (BodyBank stub in place). Phase 3 is independent of Phase 4 (different files).

**Independent Test**: For each of the 6 body models, trigger MIDI note 36 with Impulse exciter at fixed velocity. Measure first N partial ratios. Verify they match the published physics within per-body tolerance (SC-002): Membrane ±2%, Plate ±3%, Shell ±3%, String ±1%, Bell ±3%, NoiseBody modal ±3%.

### 4.1 Tests for User Story 2 (Write FIRST — Must FAIL)

- [X] T048 [US2] Write failing test in `plugins/membrum/tests/unit/bodies/test_membrane_body.cpp` covering the body contract invariants for `MembraneBody` per `body_contract.md §Test coverage requirements`: (1) allocation-detector covers `configureForNoteOn()` + `processSample()` — zero heap; (2) modal ratios within ±2% of `kMembraneRatios` (SC-002 Membrane row); (3) Size sweep 0→1 produces fundamental spanning ≥ 1 octave (US4-1); (4) Decay sweep 0→1 produces RT60 change ≥ 3× (US4-2); (5) finite-output: all 6 exciters, no NaN/Inf over 1 s; (6) sample-rate sweep at {22050, 44100, 48000, 96000, 192000} Hz — correct modal frequencies relative to Nyquist (SC-007); (7) mid-note body-switch deferral — `setBodyModel()` during sounding note: no crash, no NaN, tail continues. Verify tests FAIL.
- [X] T049 [US2] [P] Write failing tests in `plugins/membrum/tests/unit/bodies/test_plate_body.cpp` covering the same 7 contract invariants for `PlateBody` plus: (8) first 8 partial ratios match `{1.000, 2.500, 4.000, 5.000, 6.500, 8.500, 9.000, 10.000}` within ±3% (acceptance scenario US2-2). Verify tests FAIL.
- [X] T050 [US2] [P] Write failing tests in `plugins/membrum/tests/unit/bodies/test_shell_body.cpp` covering the 7 contract invariants for `ShellBody` plus: (8) first 6 partial ratios match `{1.000, 2.757, 5.404, 8.933, 13.344, 18.637}` within ±3% (acceptance scenario US2-3). Verify tests FAIL.
- [X] T051 [US2] [P] Write failing tests in `plugins/membrum/tests/unit/bodies/test_string_body.cpp` covering the 7 contract invariants for `StringBody` plus: (8) partial ratios are harmonic (integer multiples within ±1%, acceptance scenario US2-4 and SC-002 String row); (9) `processSample()` ignores `sharedBank` reference and uses internal `WaveguideString` exclusively (shared-bank isolation test from contract). Verify tests FAIL.
- [X] T052 [US2] [P] Write failing tests in `plugins/membrum/tests/unit/bodies/test_bell_body.cpp` covering the 7 contract invariants for `BellBody` plus: (8) first 5 partial ratios match Chladni `{0.250, 0.500, 0.600, 0.750, 1.000}` (hum, prime, tierce, quint, nominal) within ±3% relative to nominal (acceptance scenario US2-5). Verify tests FAIL.
- [X] T053 [US2] [P] Write failing tests in `plugins/membrum/tests/unit/bodies/test_noise_body.cpp` covering the 7 contract invariants for `NoiseBody` plus: (8) output is a hybrid of modal component and noise component (measure modal-peak presence AND broadband energy, acceptance scenario US2-6); (9) modal layer uses plate ratios at `kModeCount` entries within ±3%. Verify tests FAIL.

### 4.2 Mode-Ratio Tables and Mapping Helpers

- [X] T054 [US2] [P] Create `plugins/membrum/src/dsp/bodies/plate_modes.h`: define `constexpr int kPlateModeCount = 16`, `constexpr int kPlateMaxModeCount = 40`, `constexpr float kPlateRatios[kPlateMaxModeCount]` (first 16: `{1.000f, 2.500f, 4.000f, 5.000f, 6.500f, 8.500f, 9.000f, 10.000f, 13.000f, 13.000f, 16.250f, 17.000f, 18.500f, 20.000f, 22.500f, 25.000f}`, plus 24 more computed from `sqrt(m²+n²)/sqrt(2)` sorted), `struct PlateModeIndices { int m; int n; }`, `constexpr PlateModeIndices kPlateIndices[40]`, and `float computePlateAmplitude(int modeIdx, float strikePos) noexcept` (sin(m·pi·x/a)·sin(n·pi·y/b) per research.md §4.2); all in `Membrum::Bodies` namespace; source paper citation in comment block
- [X] T055 [US2] [P] Create `plugins/membrum/src/dsp/bodies/shell_modes.h`: define `constexpr int kShellModeCount = 12`, `constexpr float kShellRatios[12] = {1.000f, 2.757f, 5.404f, 8.933f, 13.344f, 18.637f, 24.812f, 31.870f, 39.810f, 48.632f, 58.336f, 68.922f}`, `float computeShellAmplitude(int, float) noexcept` (sin(k·pi·x/L) approximation per research.md §4.3); source citation in comment
- [X] T056 [US2] [P] Create `plugins/membrum/src/dsp/bodies/bell_modes.h`: define `constexpr int kBellModeCount = 16`, `constexpr float kBellRatios[16] = {0.250f, 0.500f, 0.600f, 0.750f, 1.000f, 1.500f, 2.000f, 2.600f, 3.200f, 4.000f, 5.333f, 6.400f, 7.333f, 8.667f, 10.000f, 12.000f}`, `float computeBellAmplitude(int, float) noexcept` (Chladni radial approximation r/R=strikePos per research.md §4.5); source citation in comment
- [X] T057 [US2] [P] Create `plugins/membrum/src/dsp/bodies/plate_mapper.h`: define `struct PlateMapper { static MapperResult map(const VoiceCommonParams& params, float pitchHz) noexcept; }` per `data-model.md §4`; Size→fundamental formula `f0 = 800 * pow(0.1f, size)` (FR-032); Strike Position → `computePlateAmplitude()`; Material → metallic damping (high brightness, low b1); Decay → decayTime multiplier; modeStretch and decaySkew from `params.modeStretch` / `params.decaySkew` (scalar-bias approximation per research.md §9)
- [X] T058 [US2] [P] Create `plugins/membrum/src/dsp/bodies/shell_mapper.h`: analogous to PlateMapper; Size→fundamental `f0 = 1500 * pow(0.1f, size)` (FR-032); Strike Position → `computeShellAmplitude()`; Material → metallic damping defaults (long sustain per FR-033)
- [X] T059 [US2] [P] Create `plugins/membrum/src/dsp/bodies/bell_mapper.h`: analogous; Size→fundamental `f0 = 800 * pow(0.1f, size)` (FR-032, nominal partial); Strike Position → `computeBellAmplitude()`; Material → low-b1/very-low-b3 metallic damping (FR-024); numPartials = kBellModeCount
- [X] T060 [US2] [P] Create `plugins/membrum/src/dsp/bodies/string_mapper.h`: define `struct StringMapper { static StringMapperResult map(const VoiceCommonParams& params, float pitchHz) noexcept; }` where `StringMapperResult` has `frequencyHz, decayTime, brightness, pickPosition`; Size→frequency `f0 = 800 * pow(0.1f, size)` (FR-032); StrikePosition → pickPosition directly; Material → brightness / decay (FR-033)
- [X] T061 [US2] [P] Create `plugins/membrum/src/dsp/bodies/noise_body_mapper.h`: define `struct NoiseBodyMapper { static Result map(const VoiceCommonParams& params, float pitchHz) noexcept; }` where `Result` has both `MapperResult modal` and noise-layer parameters per `data-model.md §4`; modal layer uses `kPlateRatios[0..kModeCount)` at `f0 = 1500 * pow(0.1f, size)` (FR-032); noise filter cutoff time-varying based on envelope; default mix `modalMix=0.6f, noiseMix=0.4f`

### 4.3 Body Backend Implementation

- [X] T062 [US2] [P] Implement `MembraneBody` fully in `plugins/membrum/src/dsp/bodies/membrane_body.h`: `configureForNoteOn()` calls `MembraneMapper::map(params, pitchHz)` and feeds result to `sharedBank.setModes(...)` (FR-026); `processSample()` delegates to `sharedBank.processSample(excitation)` — must produce bit-identical output to Phase 1's inline code for same params (FR-031 via FR-020)
- [X] T063 [US2] [P] Implement `PlateBody` in `plugins/membrum/src/dsp/bodies/plate_body.h`: `configureForNoteOn()` calls `PlateMapper::map()` and `sharedBank.setModes()`; `processSample()` delegates to shared bank; 16 modes by default (FR-021)
- [X] T064 [US2] [P] Implement `ShellBody` in `plugins/membrum/src/dsp/bodies/shell_body.h`: 12 modes (FR-022); uses `ShellMapper`; otherwise structurally identical to PlateBody
- [X] T065 [US2] [P] Implement `StringBody` in `plugins/membrum/src/dsp/bodies/string_body.h`: `configureForNoteOn()` calls `StringMapper::map()` and then `string_.setFrequency()`, `string_.setDecay()`, `string_.setBrightness()` (and `string_.setPickPosition()` if `WaveguideString` exposes it); `processSample(sharedBank, excitation)` IGNORES sharedBank and calls `string_.process(excitation)` directly (FR-023); `string_` is of type `Krate::DSP::WaveguideString`
- [X] T066 [US2] [P] Implement `BellBody` in `plugins/membrum/src/dsp/bodies/bell_body.h`: 16 modes (FR-024); uses `BellMapper`; metallic default damping (low-b1/very-low-b3)
- [X] T067 [US2] [P] Implement `NoiseBody` in `plugins/membrum/src/dsp/bodies/noise_body.h`: two-layer hybrid per `data-model.md §3.7`; `configureForNoteOn()` sets up both layers — shared bank with `kPlateRatios[0..kModeCount)` for modal layer AND `noiseFilter_` + `noiseEnvelope_` reset for noise layer; `processSample()` mixes `modalMix_ * sharedBank.processSample(excitation) + noiseMix_ * noiseEnvelope_.process() * noiseFilter_.process(noise_.process())`; starts at `kModeCount = 40` — ASSUMPTION: reduce to 30/25/20 only if Phase 9 CPU benchmark exceeds 1.25% budget (FR-062)
- [X] T068 [US2] Integrate all 6 body backends into `BodyBank::configureForNoteOn()` and `BodyBank::processSample()` dispatch via `std::visit` or index-based `switch` in `plugins/membrum/src/dsp/body_bank.h`; verify that `BodyModelId` parameter (ID 201) correctly defers switches to next note-on

### 4.4 Verification for User Story 2

- [X] T069 [US2] Build and run body tests: `build/windows-x64-release/bin/Release/membrum_tests.exe "BodyBank*" 2>&1 | tail -5` and `build/windows-x64-release/bin/Release/membrum_tests.exe "*BodyModes*" 2>&1 | tail -5` — modal ratio tests (SC-002) and Size/Decay sweep tests must pass; String harmonic-ratio test must pass within ±1%
- [X] T070 [US2] Verify Phase 1 regression test still passes: `build/windows-x64-release/bin/Release/membrum_tests.exe "Phase1Regression*" 2>&1 | tail -5` — MembraneMapper refactor must be bit-identical to Phase 1 inline code (FR-031)
- [X] T071 [US2] Add NaN-check test files to `-fno-fast-math` in `plugins/membrum/tests/CMakeLists.txt` if needed
- [X] T072 [US2] Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"` — capture to log file, zero errors/warnings required

### 4.5 Commit

- [ ] T073 [US2] Commit: "membrum: Phase 2.C body models — Plate, Shell, String, Bell, NoiseBody with mode tables, mappers, and spectral-ratio tests"

**Checkpoint**: All 6 body models produce audible output. SC-002 modal ratio tests pass for all modal bodies. String produces harmonic partials within ±1%. Phase 1 regression still passes.

---

## Phase 5: User Story 3 — Exciter and Body Are Independently Swappable (Priority: P1)

**Goal**: All 36 exciter × body combinations produce audio. None crash, none produce NaN/Inf, none exceed 0 dBFS. Deferred body-model switching during a sounding note works correctly. The swap-in architecture is validated end-to-end.

**Dependencies**: Phase 3 (all 6 exciters) and Phase 4 (all 6 bodies) must be complete.

**Independent Test**: Parameterized test iterates all 36 pairs, triggers note 36 at velocity 100, processes 500 ms, asserts: non-silent, no NaN/Inf, peak ≤ 0 dBFS, zero heap allocations (SC-011, FR-090).

### 5.1 Tests for User Story 3 (Write FIRST — Must FAIL)

- [X] T074 [US3] Write failing test in `plugins/membrum/tests/unit/test_exciter_body_matrix.cpp` covering FR-090 and acceptance scenarios US3-1 through US3-5: (a) parameterized test over all 36 combinations — trigger velocity=100, process 500 ms, assert peak ∈ (−60, 0) dBFS, no NaN/Inf/denormals via bit-manipulation checks (US3-1, US3-2); (b) exciter-type change while ringing — change ExciterType during a note, trigger new note, assert new exciter applies to new note without affecting previous tail (US3-3); (c) body-model change while voice silent — assert new body applies immediately on next note-on (US3-4); (d) body-model change while voice sounding — assert deferred: tail continues, NO crash, NO allocation, NO NaN/Inf (US3-5). Verify tests FAIL.
- [X] T075 [US3] Write failing test in `plugins/membrum/tests/unit/test_allocation_matrix.cpp` covering SC-011 and FR-072: allocation-detector wraps `DrumVoice::noteOn()`, `DrumVoice::noteOff()`, `DrumVoice::process()` across all 36 exciter × body pairs — asserts zero heap allocation for every entry point. Verify tests FAIL.
- [X] T076 [US3] Write failing test in `plugins/membrum/tests/unit/test_stability_guard.cpp` covering SC-008: (a) FeedbackExciter with every body model at velocity=1.0 (max), feedbackAmount=max — process 5 s, assert peak ≤ 0 dBFS; (b) FeedbackExciter with String body (highest Larsen risk) + max feedback — assert no runaway over 10 s (US1-6 edge case "FeedbackExciter must engage energy limiter"). Verify tests FAIL.

### 5.2 Verification for User Story 3

- [X] T077 [US3] Build and run matrix and allocation tests: `build/windows-x64-release/bin/Release/membrum_tests.exe "*ExciterBodyMatrix*" 2>&1 | tail -5` and `build/windows-x64-release/bin/Release/membrum_tests.exe "*Allocation*" 2>&1 | tail -5` and `build/windows-x64-release/bin/Release/membrum_tests.exe "*StabilityGuard*" 2>&1 | tail -5` — all must pass
- [X] T078 [US3] Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"` — capture to log file, zero errors/warnings

### 5.3 Commit

- [ ] T079 [US3] Commit: "membrum: Phase 2 US3 swap-in matrix — all 36 exciter×body combos validated: non-silent, non-NaN, allocation-free, stability-guarded"

**Checkpoint**: All 36 combinations audible. SC-011 zero allocations confirmed. SC-008 peak ≤ 0 dBFS confirmed for feedback paths.

---

## Phase 6: User Story 4 — Per-Body Parameter Mappings Are Musically Meaningful (Priority: P2)

**Goal**: For each body model, the 5 existing parameters (Material, Size, Decay, Strike Position, Level) produce musically sensible, per-body-specific sweeps. Size spans ≥ 1 octave. Decay changes RT60 ≥ 3×. Strike Position changes spectral weighting measurably.

**Dependencies**: Phase 4 (all bodies and mappers) must be complete.

**Independent Test**: For each body model, sweep Size 0→1 and measure fundamental frequency span (≥ 1 octave); sweep Decay 0→1 and measure RT60 ratio (≥ 3×); sweep Strike Position and measure per-mode amplitude changes.

### 6.1 Tests for User Story 4 (Write FIRST — Must FAIL)

- [ ] T080 [US4] Write failing test in `plugins/membrum/tests/unit/bodies/test_membrane_body.cpp` (append) and each corresponding body test file, covering acceptance scenarios US4-1 through US4-5 for ALL 6 bodies: (a) Size sweep 0→1: fundamental changes monotonically by ≥ 1 octave (acceptance US4-1); (b) Decay sweep 0→1: RT60 changes by ≥ 3× (acceptance US4-2); (c) Strike Position sweep 0→1: first-5-peaks spectral weighting changes measurably for circular bodies (Bessel J_m) and rectangular bodies (sin formula) per FR-034 (acceptance US4-3); (d) Material sweep 0→1: decay tilt changes monotonically — high modes decay faster at low Material, more even at high Material (acceptance US4-4); (e) Level=0.0: output is silent (acceptance US4-5). Verify tests FAIL for any body that hasn't been fully mapped yet.

### 6.2 Verify Mapping Implementations

- [ ] T081 [US4] [P] Verify and complete `PlateMapper::map()` in `plugins/membrum/src/dsp/bodies/plate_mapper.h`: confirm the rectangular-plate Strike Position formula uses `sin(m·pi·x/a)·sin(n·pi·y/b)` with `(x, y)` derived from the single scalar via the diagonal mapping from `research.md §4.2`; confirm per-mode amplitude outputs vary audibly across strike positions
- [ ] T082 [US4] [P] Verify and complete `ShellMapper::map()` in `plugins/membrum/src/dsp/bodies/shell_mapper.h`: confirm Strike Position uses `sin(k·pi·x/L)` free-free beam mode shape approximation; confirm 12 modes span the documented frequency range at size=0..1
- [ ] T083 [US4] [P] Verify and complete `BellMapper::map()` in `plugins/membrum/src/dsp/bodies/bell_mapper.h`: confirm Chladni radial position `r/R = strikePos` maps strike position; confirm bell nominal partial (index 4 at ratio 1.0) is used as the fundamental anchor; confirm metallic defaults (low b1, very low b3)
- [ ] T084 [US4] [P] Verify and complete `StringMapper::map()` in `plugins/membrum/src/dsp/bodies/string_mapper.h`: confirm `WaveguideString::setPickPosition(strikePos)` is called; confirm `f0 = 800 * pow(0.1f, size)` produces 1-octave+ span; confirm `WaveguideString::setDecay(...)` produces ≥ 3× RT60 span across Decay sweep
- [ ] T085 [US4] [P] Verify and complete `NoiseBodyMapper::map()` in `plugins/membrum/src/dsp/bodies/noise_body_mapper.h`: confirm noise filter cutoff and noise amplitude respond to velocity / Size in a musically sensible way; confirm the noise envelope follows the body decay parameter

### 6.3 Verification for User Story 4

- [ ] T086 [US4] Build and run all body mapper tests: `build/windows-x64-release/bin/Release/membrum_tests.exe "*BodyModes*" 2>&1 | tail -5` — all size/decay/strike-position sweep tests must pass across all 6 bodies
- [ ] T087 [US4] Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"` — capture to log, zero errors

### 6.4 Commit

- [ ] T088 [US4] Commit: "membrum: Phase 2 US4 per-body parameter mappings — Size/Decay/StrikePos/Material sweeps verified for all 6 body models"

**Checkpoint**: US4 acceptance scenarios pass. Size ≥ 1 octave for every body. Decay ≥ 3× RT60 for every body. Strike Position changes spectral weighting per body physics.

---

## Phase 7: User Story 5 — Tone Shaper Post-Body Chain (Priority: P2)

**Goal**: Implement the full Tone Shaper: Drive (Waveshaper) → Wavefolder → DCBlocker → SVF Filter, all with bypass behavior. Implement the Pitch Envelope (absolute Hz, control-plane, MultiStageEnvelope). The 808-kick test must pass (SC-009). Chain order is Drive → Wavefolder → DCBlocker → SVF as specified in `research.md §8` — NOT the literal order in FR-040's text; this deviation is justified in `research.md §8` (Buchla west-coast flow).

**Dependencies**: Phase 5 (all bodies integrated in DrumVoice) must be complete for the integration diagram to work. Phase 3 and Phase 4 can be independent.

**Independent Test**: (a) Bypass identity: all Tone Shaper stages at bypass values — output RMS difference from raw body output ≤ −120 dBFS (FR-045). (b) 808 kick: Impulse + Membrane, PitchEnv Start=160 Hz, End=50 Hz, Time=20 ms — measured fundamental at t=20 ms within ±10% of 50 Hz (SC-009).

### 7.1 Tests for User Story 5 (Write FIRST — Must FAIL)

- [ ] T089 [US5] Write failing test in `plugins/membrum/tests/unit/tone_shaper/test_tone_shaper_bypass.cpp` covering `tone_shaper_contract.md §Test coverage requirements item 1`: bypass identity — feed 1 kHz sine at 0 dBFS through `ToneShaper` with all-bypass conditions (`driveAmount_=0`, `foldAmount_=0`, `filterCutoffHz_=20000`, `filterEnvAmount_=0`, `pitchEnvTimeMs_=0`); assert RMS difference from input ≤ −120 dBFS (FR-045, acceptance scenario US5-5). Verify test FAILS.
- [ ] T090 [US5] Write failing test in `plugins/membrum/tests/unit/tone_shaper/test_pitch_envelope_808.cpp` covering SC-009 and `tone_shaper_contract.md §808 kick test`: configure PitchEnv Start=160 Hz, End=50 Hz, Time=20 ms, Curve=Exp; trigger Impulse + Membrane at velocity 100; measure fundamental pitch glide via short-time FFT at 5 ms intervals; at t=20 ms fundamental MUST be within ±10% of 50 Hz (SC-009, acceptance scenario US5-1). Also test: disabled pitch envelope returns Size-derived fundamental without interpolation (contract item 6); zero-duration edge case (0.001 ms) produces no NaN, no divide-by-zero (contract item 7). Verify tests FAIL.
- [ ] T091 [US5] Write failing tests in `plugins/membrum/tests/unit/tone_shaper/test_tone_shaper.cpp` covering `tone_shaper_contract.md §Test coverage requirements items 2–5` and acceptance scenarios US5-2, US5-3, US5-4: (a) filter envelope sweep — env amount=1.0, attack=5ms, decay=100ms; process pink noise through LP; spectral centroid follows attack-then-decay curve (US5-2); (b) Drive harmonic generation — process 1 kHz sine through Drive=1.0; assert THD increases monotonically from zero drive, peak ≤ 0 dBFS (US5-3); (c) Wavefolder odd harmonics — process 1 kHz sine through Fold=1.0; assert 3rd, 5th, 7th harmonics increase (US5-4); (d) allocation-detector: all ToneShaper methods zero heap. Verify tests FAIL.

### 7.2 Implementation for User Story 5

- [ ] T092 [US5] Implement `ToneShaper::processSample()` in `plugins/membrum/src/dsp/tone_shaper.h` with the chain order from `research.md §8`: `body_output → Drive (Waveshaper) → Wavefolder → DCBlocker → SVF Filter → return`; add a comment citing `research.md §8` explaining why this order deviates from FR-040's literal wording ("Drive → Wavefolder is west-coast Buchla flow; filter smooths aliasing residues from wavefolder"); implement per-stage bypass: when `driveAmount_==0`, skip Waveshaper; when `foldAmount_==0`, skip Wavefolder; when `filterCutoffHz_>=20000 && filterEnvAmount_==0`, skip SVF
- [ ] T093 [US5] Implement `ToneShaper::processPitchEnvelope()` in `plugins/membrum/src/dsp/tone_shaper.h`: when `pitchEnvTimeMs_ == 0.0f`, return the Size-derived body fundamental (stored from last `noteOn()`); when enabled, run `pitchEnv_.process()` and interpolate from `pitchEnvStartHz_` to `pitchEnvEndHz_` using the MultiStageEnvelope output; zero-duration edge case: minimum time clamped to 1 ms to prevent divide-by-zero; all per `tone_shaper_contract.md §Pitch Envelope behavior`
- [ ] T094 [US5] Implement `ToneShaper::noteOn(velocity)` in `plugins/membrum/src/dsp/tone_shaper.h`: trigger `filterEnv_.gate(true)`, `pitchEnv_.gate(true)` if enabled; configure `pitchEnv_` stages for Start→End sweep over `pitchEnvTimeMs_` with Exp curve per `data-model.md §6`; implement `noteOff()` to call `filterEnv_.gate(false)`, `pitchEnv_.gate(false)`
- [ ] T095 [US5] Wire `ToneShaper::processPitchEnvelope()` into `DrumVoice::process()` in `plugins/membrum/src/dsp/drum_voice.h` per the integration diagram in `unnatural_zone_contract.md`: call `processPitchEnvelope()` FIRST each sample to get current pitch Hz; pass result to `bodyBank_.configureForNoteOn()` or a new `bodyBank_.updateFundamental(hz)` method that calls `ModalResonatorBank::updateModes()` (which preserves filter state, unlike `setModes()`) or `WaveguideString::setFrequency()`; THEN call `bodyBank_.processSample()`, THEN call `toneShaper_.processSample()`
- [ ] T096 [US5] Implement all ToneShaper parameter setters in `plugins/membrum/src/dsp/tone_shaper.h`: `setFilterMode()`, `setFilterCutoff()`, `setFilterResonance()`, `setFilterEnvAmount()`, `setFilterEnvAttackMs()`, `setFilterEnvDecayMs()`, `setFilterEnvSustain()`, `setFilterEnvReleaseMs()`, `setDriveAmount()`, `setFoldAmount()`, `setPitchEnvStartHz()`, `setPitchEnvEndHz()`, `setPitchEnvTimeMs()`, `setPitchEnvCurve()`; wire each to the corresponding member DSP object; smooth continuous parameters with `OnePoleSmoother` where click-free transitions matter
- [ ] T097 [US5] Wire Tone Shaper parameters from Processor → DrumVoice in `plugins/membrum/src/processor/processor.cpp`: add parameter handling in `processParameterChanges()` for IDs 210–223 (filter params, drive, fold, pitch env) per `plugin_ids.h`; denormalize per-parameter ranges (e.g., filter cutoff: `20 Hz` to `20000 Hz` log scale, pitch env start/end: `20 Hz` to `2000 Hz` log scale)

### 7.3 Verification for User Story 5

- [ ] T098 [US5] Build and run Tone Shaper tests: `build/windows-x64-release/bin/Release/membrum_tests.exe "ToneShaper*" 2>&1 | tail -5` and `build/windows-x64-release/bin/Release/membrum_tests.exe "*808Kick*" 2>&1 | tail -5` — bypass identity (−120 dBFS), 808 kick SC-009, Drive THD, Wavefolder odd harmonics all must pass
- [ ] T099 [US5] Verify Phase 1 regression still passes (ToneShaper must be transparent at defaults): `build/windows-x64-release/bin/Release/membrum_tests.exe "Phase1Regression*" 2>&1 | tail -5`
- [ ] T100 [US5] Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"` — capture to log, zero errors

### 7.4 Commit

- [ ] T101 [US5] Commit: "membrum: Phase 2.D Tone Shaper — Drive→Wavefolder→DCBlocker→SVF chain, Pitch Envelope absolute-Hz, 808-kick SC-009 passing"

**Checkpoint**: SC-009 808-kick pitch sweep test passes (±10% at t=20 ms). FR-045 bypass identity confirmed (−120 dBFS). Drive/Wavefolder/Filter individually verified.

---

## Phase 8: User Story 6 — Unnatural Zone: Push Beyond Physics (Priority: P3)

**Goal**: Implement all 5 Unnatural Zone modules: Mode Stretch (direct parameter pass-through), Decay Skew (scalar-bias approximation), Mode Inject (HarmonicOscillatorBank + XorShift32 phase randomization), Nonlinear Coupling (envelope follower + TanhADAA energy limiter), Material Morph (2-point envelope). All at default-off values must produce bit-identical output to Phase 2 "Unnatural Zone disabled" (FR-055). Fallback plan for Decay Skew documented in research.md §9.

**Dependencies**: Phase 5 (all bodies in DrumVoice) must be complete. Phase 7 (Tone Shaper wired) should be complete to avoid re-touching `drum_voice.h`.

**Independent Test**: Each module — set to non-zero value, compare output spectrogram to zero-value baseline, assert measurable deterministic difference. All-defaults-off: bit-identical to Phase 2 without Unnatural Zone.

### 8.1 Tests for Unnatural Zone (Write FIRST — Must FAIL)

- [ ] T102 [US6] Write failing test in `plugins/membrum/tests/unit/unnatural/test_mode_stretch.cpp` covering `unnatural_zone_contract.md §Mode Stretch`: (a) `modeStretch=1.5` — measure partial ratios on Membrane body; assert they are multiplied by ~1.5 relative to `modeStretch=1.0` baseline (acceptance US6-1, contract item 2); (b) `modeStretch=1.0` — output bit-identical to `UnnaturalZone disabled` path (FR-055, contract item 1); (c) allocation-detector wraps the parameter flow — zero heap. Verify tests FAIL.
- [ ] T103 [US6] [P] Write failing test in `plugins/membrum/tests/unit/unnatural/test_decay_skew.cpp` covering `unnatural_zone_contract.md §Decay Skew`: (a) `decaySkew=-1.0` on Impulse + Membrane — measure t60 of fundamental (mode 0) and mode 7 (highest); assert `t60(mode7) > t60(mode0)` (inverted from natural, acceptance US6-2, contract item 3); (b) `decaySkew=0.0` — output bit-identical to disabled (FR-055). ASSUMPTION: if the scalar-bias approximation fails this test, escalate to per-block `updateModes()` refresh per `research.md §9` fallback plan. Verify tests FAIL.
- [ ] T104 [US6] [P] Write failing test in `plugins/membrum/tests/unit/unnatural/test_mode_inject.cpp` covering `unnatural_zone_contract.md §Mode Inject`: (a) `amount=0.5` — trigger same voice twice; assert injected partial waveforms differ in phase between triggers (phase randomization via XorShift32), while body physical modes are same phase both times (acceptance US6-3, contract item 4); (b) `amount=0.0` — output bit-identical to direct body output (early-out bypass, contract item 5); (c) allocation-detector wraps `trigger()` + `process()` — zero heap (FR-056). Verify tests FAIL.
- [ ] T105 [US6] [P] Write failing test in `plugins/membrum/tests/unit/unnatural/test_nonlinear_coupling.cpp` covering `unnatural_zone_contract.md §Nonlinear Coupling`: (a) `amount=0.5`, velocity=1.0, Plate body — spectral centroid varies by > 10% over 500 ms (time-varying character, acceptance US6-4, contract item 6); (b) every body × every exciter × amount=1.0 × velocity=1.0 — peak ≤ 0 dBFS over 1 s (energy limiter, SC-008, contract item 7); (c) `amount=0.0` — `processSample(x) == x` exact bypass (contract item 8); (d) allocation-detector wraps `processSample()` — zero heap. Verify tests FAIL.
- [ ] T106 [US6] [P] Write failing test in `plugins/membrum/tests/unit/unnatural/test_material_morph.cpp` covering `unnatural_zone_contract.md §Material Morph`: (a) enabled, start=1.0, end=0.0, duration=500 ms — decay tilt envelope changes monotonically (acceptance US6-5, contract item 9); (b) enabled=false — Material is static (contract item 10); (c) duration shorter than one process block — no hang, no static timbre; (d) duration=0 — `process()` returns static material, no divide-by-zero; (e) allocation-detector wraps `process()` + `trigger()` — zero heap. Verify tests FAIL.
- [ ] T107 [US6] Write failing test `plugins/membrum/tests/unit/unnatural/test_mode_stretch.cpp` (append) or a new `test_defaults_off.cpp` covering FR-055 and acceptance scenario US6-6: all 5 Unnatural Zone parameters at their default off-values (modeStretch=1.0, decaySkew=0.0, modeInject.amount=0.0, nonlinearCoupling.amount=0.0, materialMorph.enabled=false) — assert voice output is bit-identical (within −120 dBFS RMS on deterministic inputs) to Phase 2 Unnatural Zone disabled (contract default-off guarantee). Verify test FAILS.

### 8.2 Implementation for Unnatural Zone

- [ ] T108 [US6] Implement `Mode Stretch` pass-through: `UnnaturalZone::setModeStretch(value)` stores `modeStretch_`; `DrumVoice` passes `unnaturalZone_.getModeStretch()` as `params_.modeStretch` to all mapper calls; mappers pass it as the `stretch` argument to `ModalResonatorBank::setModes()/updateModes()`; for String body, map to `WaveguideString::setInharmonicity()` or brightness parameter (ASSUMPTION: use the `Krate::DSP::WaveguideString` API closest to inharmonicity; document in `voice_common_params.h` comment); early-out: `modeStretch_ == 1.0f` produces no change vs baseline (FR-050)
- [ ] T109 [US6] [P] Implement `Decay Skew` scalar-bias approximation in `plugins/membrum/src/dsp/voice_common_params.h` or each body mapper: `decaySkew` ∈ [−1, +1] biases the `brightness` scalar and per-mode decay-time multipliers using the formula from `research.md §9`; store `decaySkew_` in `UnnaturalZone`, pass to mappers via `params_.decaySkew`; mappers apply `decay_skew_multiplier_k = exp(-decaySkew * log(f_k / f_fundamental))` to per-mode decay; `decaySkew == 0.0f` produces no change (FR-051); if the unit test (T103) fails at the required inversion, escalate to the per-block `updateModes()` approach from `research.md §9` and document the change in a comment in the mapper
- [ ] T110 [US6] [P] Implement `ModeInject` fully in `plugins/membrum/src/dsp/unnatural/mode_inject.h`: `HarmonicOscillatorBank bank_` + `XorShift32 rng_`; `trigger()` randomizes 8 starting phases by calling `rng_.next()` for each partial, applying phase offsets to the bank; `process()` returns `amount_ * bank_.process()` when `amount_ > 0.0f`, returns `0.0f` immediately when `amount_ == 0.0f` (early-out bypass, no oscillator bank update); `setFundamentalHz(hz)` calls `bank_.setTargetPitch(hz)`; `prepare(sampleRate, voiceId)` seeds `rng_` from voiceId per `research.md §6` (FR-052, FR-056)
- [ ] T111 [US6] [P] Implement `NonlinearCoupling` fully in `plugins/membrum/src/dsp/unnatural/nonlinear_coupling.h`: `EnvelopeFollower envFollower_` + `TanhADAA energyLimiter_`; implement `processSample(bodyOutput)` per the algorithm in `unnatural_zone_contract.md §Nonlinear Coupling`: early-out when `amount_==0.0f` returns input unchanged; otherwise `env = envFollower_.processSample(bodyOutput)`, `dEnv = env - previousEnv_`, `modulated = bodyOutput * (1.0 + couplingStrength * dEnv)`, return `energyLimiter_.process(modulated)`; **energy limiter is mandatory** — TanhADAA guarantees `|output| ≤ 1.0` (FR-053, SC-008)
- [ ] T112 [US6] [P] Implement `MaterialMorph` fully in `plugins/membrum/src/dsp/unnatural/material_morph.h`: 2-point envelope; `trigger()` resets `sampleCounter_=0`, computes `totalSamples_ = durationMs_ * sampleRate_ / 1000.0f`; `process()` returns static Material when `!enabled_` or `totalSamples_ == 0`; otherwise interpolates between `startMaterial_` and `endMaterial_` via linear or exponential curve; holds at `endMaterial_` after completion; `durationMs_` clamped to minimum 1 sample to prevent divide-by-zero; completes in a single block if duration < block size (FR-054)
- [ ] T113 [US6] Wire Unnatural Zone into `DrumVoice::process()` per the integration diagram in `unnatural_zone_contract.md`: after `bodyBank_.processSample(exciterOut)` → add `unnaturalZone_.modeInject.process()` to body output → pass combined output to `unnaturalZone_.nonlinearCoupling.processSample()` → pass result to `toneShaper_.processSample()` → apply amp ADSR and level; wire `unnaturalZone_.materialMorph.process()` into the Material passed to body mappers (override static Material with morph output when morph is enabled)
- [ ] T114 [US6] Wire Unnatural Zone parameters from Processor → DrumVoice in `plugins/membrum/src/processor/processor.cpp`: add handling for IDs 230–244 (modeStretch, decaySkew, modeInject amount, nonlinearCoupling amount, morph enable/start/end/duration/curve) in `processParameterChanges()`; denormalize per-parameter ranges (modeStretch: [0.5, 2.0] linear; decaySkew: [−1.0, +1.0] linear; morph duration: [10, 2000] ms)

### 8.3 Verification for User Story 6

- [ ] T115 [US6] Build and run Unnatural Zone tests: `build/windows-x64-release/bin/Release/membrum_tests.exe "UnnaturalZone*" 2>&1 | tail -5` — mode stretch ratio test, decay skew inversion test, mode inject phase randomization test, nonlinear coupling spectral evolution test, material morph envelope test, defaults-off identity test all must pass
- [ ] T116 [US6] Verify FR-055 defaults-off and Phase 1 regression: `build/windows-x64-release/bin/Release/membrum_tests.exe "Phase1Regression*" 2>&1 | tail -5` and `build/windows-x64-release/bin/Release/membrum_tests.exe "*DefaultsOff*" 2>&1 | tail -5`
- [ ] T117 [US6] Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"` — capture to log, zero errors

### 8.4 Commit (5 sub-commits, one per Unnatural Zone module)

- [ ] T118 [US6] Commit: "membrum: Phase 2.E.1 Mode Stretch — parameter pass-through to ModalResonatorBank stretch; partial-ratio-multiplication test passing"
- [ ] T119 [US6] Commit: "membrum: Phase 2.E.2 Decay Skew — scalar-bias approximation in body mappers; fundamental-decays-before-shimmer test passing"
- [ ] T120 [US6] Commit: "membrum: Phase 2.E.3 Mode Inject — HarmonicOscillatorBank + XorShift32 phase randomization; phase-differs-on-retrigger test passing"
- [ ] T121 [US6] Commit: "membrum: Phase 2.E.4 Nonlinear Coupling — envelope-follower + TanhADAA energy limiter; spectral-evolution + peak-safe tests passing"
- [ ] T122 [US6] Commit: "membrum: Phase 2.E.5 Material Morph — 2-point decay-tilt envelope; morph-envelope test passing; FR-055 defaults-off identity confirmed"

**Checkpoint**: All 5 Unnatural Zone modules produce measurable effects when enabled. FR-055 confirmed: all-defaults-off produces output within −120 dBFS of Unnatural Zone disabled. Energy limiter guarantees peak ≤ 0 dBFS.

---

## Phase 9: User Story 7 — CPU Budget Across All 144 Combinations (Priority: P2)

**Goal**: All 144 combinations (6 exciter × 6 body × tone_shaper_on/off × unnatural_on/off) meet the 1.25% single-voice CPU budget at 44.1 kHz (SC-003). The worst-case combination is identified and documented. The Noise Body mode count is confirmed or reduced per FR-062.

**Dependencies**: Phases 3–8 must all be complete (all modules implemented).

**Independent Test**: `[.perf]`-tagged benchmark iterates all 144 combinations, processes 10 s of audio each, reports CPU percentage, asserts ≤ 1.25% for every combination.

### 9.1 Tests for User Story 7 (Write FIRST — Must FAIL)

- [ ] T123 [US7] Write failing test in `plugins/membrum/tests/perf/test_benchmark_144.cpp` tagged `[.perf]` covering FR-093 and acceptance scenarios US7-1 through US7-3: (a) iterate all 144 combinations (6 exciter × 6 body × 2 tone_shaper × 2 unnatural); (b) for each combination: configure voice, process 10 s at 44100 Hz in Release mode, measure wall-clock time; (c) compute `cpu_percent = (wall_time_s / 10.0) * 100.0`; (d) hard-assert `cpu_percent ≤ 1.25` per combination (SC-003, US7-1); (e) also hard-assert `cpu_percent ≤ 2.0` as the hard ceiling for any combination (US7-3); (f) append results to `build/windows-x64-release/membrum_benchmark_results.csv` in the format from `quickstart.md §4`; (g) print worst-case combination and measured value before asserting. Verify test structure is correct (may need to be run with `[.perf]` flag, skip in regular CI).
- [ ] T124 [US7] Write failing test in `plugins/membrum/tests/perf/test_noise_body_cpu.cpp` tagged `[.perf]` covering FR-062: start with NoiseBody kModeCount=40; measure single-voice CPU for Impulse + NoiseBody + tone_shaper=on + unnatural=on; assert ≤ 1.25%; if assertion fails, the test documentation says "reduce kModeCount to 30 and re-measure" — this is the trigger for the FR-062 mode-count reduction. Verify test exists and is tagged correctly.

### 9.2 End-to-End Integration Tests (36 and 144 Combination Matrix)

- [ ] T125 [US7] Write failing test in `plugins/membrum/tests/unit/test_exciter_body_matrix.cpp` (extend T074) with the 144-combination matrix: iterate all 6 × 6 × 2 × 2 combinations; for each: trigger note, process 500 ms, assert non-silent, no NaN/Inf, peak ≤ 0 dBFS (FR-090 extended to 144); allocation-detector confirms zero heap for each combination (SC-011 extended). This is the non-perf functional gate — runs in regular CI unlike the CPU benchmark.

### 9.3 Additional Integration and State Tests

- [ ] T126 [US7] Write failing test in `plugins/membrum/tests/unit/vst/test_state_roundtrip_v2.cpp` (extend T011) covering FR-094 and SC-006: round-trip ALL 34 parameters with non-default values; assert bit-identical normalized values on load; test Phase-1 backward compat (version=1 state → Phase-2 defaults). Verify test FAILS.
- [ ] T127 [US7] Write failing test in `plugins/membrum/tests/unit/architecture/test_exciter_bank.cpp` (extend) for sample-rate sweep (SC-007): run all 36 exciter × body combinations at {22050, 44100, 48000, 96000, 192000} Hz; assert correct modal frequencies relative to Nyquist for each rate; assert no NaN/Inf at any sample rate (acceptance scenario in edge-cases section). Verify tests FAIL.

### 9.4 CPU Benchmark Execution and Noise Body Mode-Count Resolution

- [ ] T128 [US7] Run CPU benchmark: `build/windows-x64-release/bin/Release/membrum_tests.exe "[.perf]" 2>&1 | tail -20` — inspect results; if any combination exceeds 1.25%: (a) if it is the Noise Body at 40 modes, reduce `NoiseBody::kModeCount` from 40 to 30, rebuild, re-measure; (b) if still exceeds, try 25 modes; (c) document the final chosen count and measured CPU in a comment in `plugins/membrum/src/dsp/bodies/noise_body.h` citing FR-062; (d) if non-Noise-Body combinations exceed budget, switch to `ModalResonatorBankSIMD` as the emergency fallback per `plan.md §SIMD Emergency Fallback` and document the change; capture all output to `job-logs.txt`

### 9.5 Verification for User Story 7

- [ ] T129 [US7] Build and run 144-combination functional matrix: `build/windows-x64-release/bin/Release/membrum_tests.exe "*ExciterBodyMatrix*" 2>&1 | tail -5` — all 144 combinations pass non-NaN, peak-safe, allocation-free checks
- [ ] T130 [US7] Build and run state round-trip: `build/windows-x64-release/bin/Release/membrum_tests.exe "*StateRoundTrip*" 2>&1 | tail -5` — all 34 parameters round-trip bit-exactly (SC-006)
- [ ] T131 [US7] Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"` — capture to log, zero errors

### 9.6 Commit

- [ ] T132 [US7] Commit: "membrum: Phase 2.F 144-combination matrix + state round-trip — FR-090/FR-094/SC-006/SC-007 all passing; Noise Body mode count resolved"

**Checkpoint**: All 144 combinations are functionally validated (non-NaN, peak-safe, allocation-free). CPU budget confirmed ≤ 1.25% for every combination. Final Noise Body mode count documented with measured CPU. State round-trip passes for all 34 parameters.

---

## Phase 10: Polish, CI Gating, and Release Preparation

**Purpose**: Clang-tidy pass, pluginval final validation, auval (macOS), compliance table, CHANGELOG update, version confirmation. This phase is not mapped to a user story — it is the cross-cutting quality gate before Phase 2 is declared complete.

### 10.1 VST Parameter Contract Tests

- [ ] T133 Write failing test in `plugins/membrum/tests/unit/vst/test_membrum_parameters_v2.cpp` covering `vst_parameter_contract.md §Test coverage requirements`: (1) `controller.getParameterCount() == 34` (5 Phase-1 + 29 Phase-2); (2) no duplicate IDs among all 34 parameters; (3) for each parameter, 5 random `setParamNormalized()` round-trips via `getParamNormalized()` — bit-identical (SC-006 parameter layer); (4) `StringListParameter` for Exciter Type: `toPlain(0..1)` spans 6 integer values correctly; same for Body Model, Filter Type, Pitch Env Curve; (5) real-time safety: allocation-detector wraps `processParameterChanges()` and all setters — zero heap; (6) Phase-1 backward compat: load version-1 state, assert controller reflects Phase-2 defaults. Verify tests FAIL, then implement, then verify pass.

### 10.2 Full Test Suite Verification

- [ ] T134 Run full test suite: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests && build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5` — the last line MUST say "All tests passed". If any test fails, fix it before proceeding.
- [ ] T135 Run Phase 1 regression one final time: `build/windows-x64-release/bin/Release/membrum_tests.exe "Phase1Regression*" 2>&1 | tail -5` — must confirm −90 dBFS or better (SC-005)
- [ ] T136 Run allocation matrix final verification: `build/windows-x64-release/bin/Release/membrum_tests.exe "*Allocation*" 2>&1 | tail -5` — SC-011 zero allocations for all 36 combinations × all audio-thread entry points

### 10.3 Pluginval Final

- [ ] T137 Run pluginval final: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3" 2>&1 | tee job-logs.txt` — capture to log first; inspect for warnings; zero errors and zero warnings required (FR-096, SC-010). Fix any pluginval failure before proceeding. Do NOT re-run just to change grep — read the log.

### 10.4 Clang-Tidy Full Pass

- [ ] T138 Generate Ninja compile_commands.json: from VS Developer PowerShell run `"C:/Program Files/CMake/bin/cmake.exe" --preset windows-ninja` (one-time if not already done; re-run if CMakeLists changed)
- [ ] T139 Run clang-tidy for membrum target: `./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja 2>&1 | tee membrum-clang-tidy.log` — capture output to log; read log; fix ALL warnings (NOT just new code per Constitution "Own ALL failures"). After fixes rebuild and re-run tidy. When zero warnings, proceed.

### 10.5 CPU Benchmark Gate Final

- [ ] T140 Run the full 144-combination CPU benchmark and confirm results: `build/windows-x64-release/bin/Release/membrum_tests.exe "[.perf]" 2>&1 | tee job-logs.txt` — capture to log; verify every row in the CSV shows `cpu_percent ≤ 1.25`; record the worst-case combination row here (in the commit message)

### 10.6 macOS / Linux CI Verification

- [ ] T141 [P] Verify CI builds succeed on all platforms: confirm `.github/workflows/ci.yml` includes `Membrum` and `membrum_tests` targets in all platform build/test steps (was wired in Phase 1; verify Phase 2 new test files are included in `tests/CMakeLists.txt` so CI picks them up automatically)
- [ ] T142 [P] Confirm `auval -v aumu Mbrm KrAt` CI step is wired on macOS job (FR-097, SC-010) — AU config files are unchanged from Phase 1 (bus configuration still 0 in / 2 out), so this should pass without changes; flag if CI reports failure

### 10.7 Compliance Table and Documentation

- [ ] T143 Fill the compliance table in `specs/137-membrum-phase2-exciters-bodies/spec.md` per Constitution Principle XVI: for EVERY FR-xxx, open the implementation file(s), read the relevant code, confirm requirement is met, cite the file path and line number; for EVERY SC-xxx, copy the actual test output or measured value; NO generic "implemented" entries; NO ✅ without having just-now verified code/test. This task is the final gate before marking Phase 2 complete.
- [ ] T144 Update `plugins/membrum/CHANGELOG.md`: add `[0.2.0] — 2026-04-10` section listing all Phase 2 deliverables: 6 exciter types, 6 body models, swap-in architecture, Tone Shaper, Unnatural Zone, 144-combination CPU validation, state version 2 with backward compat

### 10.8 Final Commit

- [ ] T145 Final commit: "membrum: Phase 2.G CPU validation + CI gating + compliance table — v0.2.0 complete"

**Checkpoint (Phase 2 Complete)**: All test suites pass. Pluginval strictness 5 passes. Clang-tidy zero warnings. CPU benchmark ≤ 1.25% for all 144 combinations. Compliance table filled with file:line evidence for every FR and SC. CHANGELOG updated. Version 0.2.0 committed.

---

## FR Traceability Summary

Every FR from the spec maps to at least one task below. Listed in FR order:

| FR | Task(s) |
|----|---------|
| FR-001 (variant dispatch, no virtual) | T017, T018, T043, T068 |
| FR-002 (no per-sample branch in hot path) | T017, T018, T043, T068 |
| FR-003 (Exciter Type + Body Model parameters) | T024 |
| FR-004 (silent switch takes effect immediately) | T017, T018 |
| FR-005 (sounding switch deferred to note-on) | T018, T074 |
| FR-006 (pre-allocated, no audio-thread construction) | T015, T016, T075 |
| FR-007 (Phase 1 API compatibility) | T022, T026 |
| FR-010 (Impulse exciter carry-over) | T037 |
| FR-011 (Mallet exciter) | T038 |
| FR-012 (Noise Burst exciter) | T039 |
| FR-013 (Friction exciter transient mode) | T040 |
| FR-014 (FM Impulse exciter, 1:1.4 default) | T041 |
| FR-015 (Feedback exciter with energy limiter) | T042 |
| FR-016 (velocity spectral response all exciters) | T030–T035, T036 |
| FR-017 (all 6 exciters selectable, state round-trip) | T043, T126 |
| FR-020 (Membrane carry-over) | T062, T070 |
| FR-021 (Plate body, 16 modes) | T054, T063 |
| FR-022 (Shell body, 12 modes) | T055, T064 |
| FR-023 (String body, WaveguideString) | T065 |
| FR-024 (Bell body, Chladni ratios, 16 modes) | T056, T066 |
| FR-025 (Noise Body hybrid, up to 40 modes) | T067, T124 |
| FR-026 (shared ModalResonatorBank, setModes at note-on) | T018, T062–T067 |
| FR-027 (Body Model selectable, state round-trip) | T024, T126 |
| FR-028 (per-body parameter docs in modes headers) | T054–T056 |
| FR-030 (extract per-body mapper helpers) | T021, T057–T060, T061 |
| FR-031 (MembraneMapper bit-identical to Phase 1) | T021, T070 |
| FR-032 (per-body Size→fundamental formulas) | T057–T061, T080 |
| FR-033 (per-body Material→damping mapping) | T057–T061 |
| FR-034 (per-body Strike Position math) | T057–T061, T081–T085 |
| FR-035 (mapper no virtual dispatch) | T021, T057–T061 |
| FR-040 (Tone Shaper chain — plan order: Drive→Wavefolder→DCBlocker→SVF) | T092; Note: differs from FR-040 literal wording, justified in research.md §8 |
| FR-041 (SVF Filter with filter ADSR) | T092, T096 |
| FR-042 (Drive, alias-safe Waveshaper) | T092 |
| FR-043 (Wavefolder) | T092 |
| FR-044 (Pitch Envelope, absolute Hz, control-plane) | T090, T093, T094, T095, T096 |
| FR-045 (Tone Shaper bypass identity ≤ −120 dBFS) | T089 |
| FR-046 (Tone Shaper real-time safe) | T091 |
| FR-047 (Snare wire deferred) | Not tasked (deferred) |
| FR-050 (Mode Stretch) | T108, T102 |
| FR-051 (Decay Skew) | T109, T103 |
| FR-052 (Mode Inject + phase randomization) | T110, T104 |
| FR-053 (Nonlinear Coupling + energy limiter) | T111, T105 |
| FR-054 (Material Morph 2-point envelope) | T112, T106 |
| FR-055 (Unnatural Zone defaults-off identity) | T107, T113 |
| FR-056 (Mode Inject + Nonlinear Coupling real-time safe) | T104, T105 |
| FR-060 (Phase 1 velocity mapping preserved) | T037 |
| FR-061 (other 4 exciters velocity mapping) | T038–T042, T036 |
| FR-062 (Noise Body mode count measured vs budget) | T067, T124, T128 |
| FR-070 (1.25% CPU budget all 144 combinations) | T123, T128, T140 |
| FR-071 (scalar ModalResonatorBank; SIMD deferred) | T067, T128 |
| FR-072 (allocation-free, lock-free, exception-free) | T075, T125 |
| FR-073 (Tone Shaper + Unnatural Zone no audio-thread allocations) | T091, T107 |
| FR-074 (FTZ/DAZ preserved) | Phase 1 carryover; verify in T134 |
| FR-080 (29 new parameters listed) | T024 |
| FR-081 (parameter naming convention) | T004, T024 |
| FR-082 (state round-trip + Phase-1 backward compat) | T023, T126 |
| FR-083 (host-generic editor, no uidesc) | Phase 1 carryover; verify in T137 |
| FR-090 (36-combination matrix test) | T074 |
| FR-091 (per-body spectral verification) | T048–T053 |
| FR-092 (per-exciter spectral centroid test) | T030–T035, T036 |
| FR-093 (144-combination CPU benchmark) | T123, T128 |
| FR-094 (state round-trip all Phase-2 parameters) | T126 |
| FR-095 (Phase 1 regression golden test) | T010, T025, T135 |
| FR-096 (Pluginval strictness 5) | T046, T072, T100, T117, T131, T137 |
| FR-097 (auval on macOS) | T142 |
| FR-100 (ODR prevention) | T012, T013; ODR searches documented in plan.md |
| FR-101 (no dsp/ changes) | Enforced by architecture; verify in T134 |
| FR-102 (reuse existing KrateDSP components) | All implementation tasks |

## SC Traceability Summary

| SC | Task(s) |
|----|---------|
| SC-001 (all 36 combos audible, non-NaN, non-clipping, 500 ms) | T074, T125 |
| SC-002 (per-body modal ratios within tolerance) | T048–T053, T069 |
| SC-003 (1.25% CPU all 144 combinations) | T123, T128, T140 |
| SC-004 (spectral centroid ratio ≥ 2.0 all 6 exciters) | T030–T036, T044 |
| SC-005 (Phase 1 regression −90 dBFS) | T010, T025, T070, T099, T135 |
| SC-006 (state round-trip bit-exact all 34 parameters) | T011, T126, T130 |
| SC-007 (no NaN/Inf at extreme params + all sample rates) | T048–T053, T127 |
| SC-008 (peak ≤ 0 dBFS, Feedback + Nonlinear Coupling energy limiters) | T035, T076, T105 |
| SC-009 (808-kick pitch envelope ±10% at t=20 ms) | T090, T098 |
| SC-010 (pluginval + auval pass) | T137, T142 |
| SC-011 (zero allocations, all 36 combos, all audio-thread entries) | T075, T125, T136 |
