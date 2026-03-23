# Tasks: Body Resonance

**Input**: Design documents from `/specs/131-body-resonance/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/body_resonance_api.h

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by implementation phase (DSP core, plugin integration, QA). The five user stories (US1-US5) map onto Phase 3 (DSP core) and Phase 4 (plugin integration), because all five stories are satisfied by a single `BodyResonance` class. Individual story acceptance is verified by dedicated test sections within Phase 3.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines — they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase 5)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) — no manual context verification required.

### Integration Tests (MANDATORY When Applicable)

The body resonance wires into the Innexus voice engine after the resonator stage. Integration tests are **required** for Phase 4. Key rules:
- **Behavioral correctness over existence checks**: Verify the output is *correct*, not just *present*.
- **Test degraded host conditions**: Not just ideal conditions — also no transport, no tempo, `nullptr` process context.
- **Test per-block configuration safety**: Ensure `setParams()` called every block does not silently reset filter state.

### Cross-Platform Compatibility Check (After Each Phase)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/unit/CMakeLists.txt`
2. Use `Approx().margin()` for floating-point comparisons, not exact equality
3. Use `std::setprecision(6)` or less in approval tests (MSVC/Clang differ at 7th-8th digits)

---

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story this task belongs to (US1-US5)
- Exact file paths are included in every description

---

## Phase 1: Setup

**Purpose**: Register new test source files in CMake so they are compiled. No implementation yet.

- [ ] T001 Add `body_resonance_tests.cpp` to `dsp/tests/unit/CMakeLists.txt` alongside existing processor test entries (e.g., next to `modal_resonator_bank_tests.cpp`)
- [ ] T002 Add `body_resonance_integration_tests.cpp` to `plugins/innexus/tests/unit/processor/CMakeLists.txt` (or equivalent innexus test registration file)

**Checkpoint**: CMake knows about both test files. Build will fail to compile until the test files are created, which is intentional.

---

## Phase 2: Foundational — Verify Existing DSP Primitives

**Purpose**: Confirm that all dependencies (`Biquad`, `OnePoleSmoother`, `CrossoverFilter`) match the API contract before writing tests against them. This prevents surprises mid-implementation.

**Why foundational**: The implementation plan locks in specific API call signatures (from `plan.md` dependency table). Discrepancies here would break tests in Phases 3 and 4.

- [ ] T003 Read `dsp/include/krate/dsp/primitives/biquad.h` and confirm these signatures exist as documented in `contracts/body_resonance_api.h`: `void setCoefficients(const BiquadCoefficients&) noexcept`, `float process(float) noexcept`, `void reset() noexcept`, `bool BiquadCoefficients::isStable() const noexcept`
- [ ] T004 Read `dsp/include/krate/dsp/primitives/smoother.h` and confirm `OnePoleSmoother::configure(float timeMs, float sampleRate) noexcept`, `setTarget(float) noexcept`, `process() noexcept`, `snapTo(float) noexcept`, `getCurrentValue() const noexcept` all exist
- [ ] T005 Read `dsp/include/krate/dsp/processors/crossover_filter.h` and confirm it is LR4 (24 dB/oct), NOT the 6 dB/oct first-order crossover required — document in code comment that `CrossoverFilter` is NOT reused for this reason
- [ ] T006 Read `dsp/include/krate/dsp/effects/fdn_reverb.h` briefly to extract the Hadamard butterfly pattern and Jot absorption formula as reference for the body FDN implementation

**Checkpoint**: All dependency signatures confirmed. First-order crossover confirmed as inline implementation (not reusing `CrossoverFilter`). Ready for test-first development.

---

## Phase 3: DSP Core — BodyResonance Processor

**Goal**: Implement the complete `BodyResonance` DSP component with all unit tests passing. This phase satisfies all five user stories at the DSP level.

**Independent Test**: Instantiate `BodyResonance`, call `prepare(44100.0)`, then call `processBlock()` with various parameter combinations. Verify: (1) mix=0 produces bit-identical bypass, (2) RMS output <= RMS input at all parameter settings, (3) size=0/0.5/1.0 produce perceptually distinct mode frequency distributions, (4) material=0 has HF T60 at least 3x shorter than LF T60, (5) no instability at any parameter combo.

---

### 3.1 Tests for All User Stories (Write FIRST — Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

**User Story 1 — Instrument Body Coloring (P1)**

- [ ] T007 [US1] Write failing test `TEST_CASE("BodyResonance - bypass produces bit-identical output")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: call `prepare(44100.0)`, `setParams(0.5f, 0.5f, 0.0f)`, process 1024 samples of white noise; assert each output sample equals the corresponding input sample exactly (SC-007, FR-018)
- [ ] T008 [US1] Write failing test `TEST_CASE("BodyResonance - audible coloring at default params")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: call `setParams(0.5f, 0.5f, 1.0f)`, process 4096 samples; assert output differs from input by more than floating-point epsilon (i.e., the resonator is doing work)
- [ ] T009 [US1] Write failing test `TEST_CASE("BodyResonance - no instability at any parameter combo")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: sweep size in {0.0, 0.25, 0.5, 0.75, 1.0} and material in {0.0, 0.5, 1.0} with mix=1.0; process 4096 samples of a unit impulse for each combo; assert all output samples are finite (SC-004, FR-016)
- [ ] T010 [US1] Write failing test `TEST_CASE("BodyResonance - energy passive")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: for all 15 size/material combos at mix=1.0, process 4096 samples of 440 Hz sine and compute input RMS and output RMS; assert `outputRms <= inputRms + 1e-6f` for each (SC-005, FR-016)
- [ ] T011 [US1] Write failing test `TEST_CASE("BodyResonance - lifecycle")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: assert `isPrepared()` is false before `prepare()`; true after; `reset()` does not change `isPrepared()`; second `prepare()` call with different sample rate succeeds without crash
- [ ] T011b [US1] Write failing test `TEST_CASE("BodyResonance - silence input produces silence output")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: call `prepare(44100.0)`, `setParams(0.5f, 0.5f, 1.0f)`, feed 8192 samples of 0.0f; assert all output samples are exactly 0.0f after the initial transient settles (first 64 samples may be skipped)
- [ ] T011c [US1] Write failing test `TEST_CASE("BodyResonance - mix transition from zero is artifact-free")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: call `prepare(44100.0)`, `setParams(0.5f, 0.5f, 0.0f)`, process 512 samples of 440 Hz sine; then call `setParams(0.5f, 0.5f, 1.0f)` and process 512 more; assert no sample during the transition exceeds 6 dB above its neighbours (no click at the mix activation boundary) (FR-017, SC-008)

**User Story 2 — Body Size Control (P1)**

- [ ] T012 [US2] Write failing test `TEST_CASE("BodyResonance - size=0 produces modes above 250 Hz")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: call `setParams(0.0f, 0.5f, 1.0f)`, process a unit impulse (8192 samples), compute FFT magnitude spectrum, assert spectral centroid is above 250 Hz and energy below 200 Hz is less than 20% of total (SC-002, FR-006)
- [ ] T013 [US2] Write failing test `TEST_CASE("BodyResonance - size=1 produces modes below 100 Hz")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: repeat with `size=1.0f`; assert spectral energy concentration below 150 Hz is greater than energy above 300 Hz (SC-002, FR-006)
- [ ] T014 [US2] Write failing test `TEST_CASE("BodyResonance - size parameter sweep has no zipper noise")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: call `setParams(0.0f, 0.5f, 1.0f)`, process 512 samples, then call `setParams(1.0f, 0.5f, 1.0f)`, process 512 more; assert no single sample is more than 6 dB above its neighbours (no clicks) during the transition (SC-008, FR-017)

**User Story 3 — Material Character Control (P1)**

- [ ] T015 [US3] Write failing test `TEST_CASE("BodyResonance - wood has strong HF damping")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: call `setParams(0.5f, 0.0f, 1.0f)`, process a unit impulse (8192 samples); compute T60 at 200 Hz band and 4000 Hz band; assert `t60_4k < t60_200hz / 3.0f` (SC-003, FR-012)
- [ ] T016 [US3] Write failing test `TEST_CASE("BodyResonance - metal has similar HF and LF decay")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: repeat with `material=1.0f`; assert `t60_4k >= t60_200hz / 2.0f` (SC-003, FR-012)
- [ ] T017 [US3] Write failing test `TEST_CASE("BodyResonance - material sweep is artifact-free")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: process 512 samples at material=0, then 512 at material=1; assert no click (same 6 dB neighbour rule as T014) (SC-008, FR-017)
- [ ] T018 [US3] Write failing test `TEST_CASE("BodyResonance - body is passive at all material values")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: at material={0.0, 0.5, 1.0} with size=0.5, mix=1.0, assert RMS output <= RMS input for 4096-sample white-noise block (FR-016)

**User Story 4 — Radiation HPF on Small Bodies (P2)**

- [ ] T019 [US4] Write failing test `TEST_CASE("BodyResonance - radiation HPF attenuates sub-bass on small body")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: `setParams(0.0f, 0.5f, 1.0f)`, feed a 50 Hz sine (8192 samples); assert output RMS is less than 30% of input RMS (i.e., at least 10 dB of attenuation below the radiation cutoff) (SC-010, FR-015)
- [ ] T020 [US4] Write failing test `TEST_CASE("BodyResonance - radiation HPF cutoff scales with size")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: process a 300 Hz sine through size=0 body and size=1 body; assert that 300 Hz is more attenuated at size=0 (small, higher HPF cutoff) than at size=1 (FR-015)

**User Story 5 — No FDN Metallic Ringing in Wood Mode (P2)**

- [ ] T021 [US5] Write failing test `TEST_CASE("BodyResonance - no FDN ringing in wood mode below crossover")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: `setParams(0.5f, 0.0f, 1.0f)`, process unit impulse (8192 samples); compute short-time spectral snapshots at 200-500 ms after impulse; assert no spectral peak in the 0-500 Hz range is more than 3 dB above its neighbors (i.e., no discrete FDN pitch peaks) (SC-009, FR-011, FR-013)

**Sample Rate Scaling (FR-022)**

- [ ] T022 [P] Write failing test `TEST_CASE("BodyResonance - sample rate scaling")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: prepare and process at 44100, 48000, 96000, 192000 Hz; at each rate, process a unit impulse and assert output is finite and bounded; assert `isPrepared()` is true after each `prepare()` call

**FDN RT60 Cap (SC-001, FR-013)**

- [ ] T023 [P] Write failing test `TEST_CASE("BodyResonance - FDN RT60 cap wood")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: `setParams(0.5f, 0.0f, 1.0f)`, feed an impulse, process up to 44100 * 0.301 samples (~13,274 at 44.1 kHz), assert the RMS of the tail after 300 ms is below -60 dB relative to the peak (FR-013)
- [ ] T024 [P] Write failing test `TEST_CASE("BodyResonance - FDN RT60 cap metal")` in `dsp/tests/unit/processors/body_resonance_tests.cpp`: `setParams(0.5f, 1.0f, 1.0f)`, feed impulse, process up to 44100 * 2.001 samples; assert tail after 2 s is below -60 dB (FR-013)

**Checkpoint**: All tests are written and failing. Build compiles the test file but every TEST_CASE fails with linker errors (no implementation yet). Proceed to 3.2.

---

### 3.2 Implementation — `body_resonance.h`

- [ ] T025 [US1] Define `BodyMode` struct and the three constexpr preset arrays (`kBodyPresets[3][8]`) in `dsp/include/krate/dsp/processors/body_resonance.h` per data-model.md: small preset (violin-scale, modes ~275-570+ Hz, bridge hill ~2-3 kHz, sub-Helmholtz gain taper), medium preset (guitar-scale, modes ~90-400+ Hz, A0/T1 anti-phase coupling at ~110 Hz), large preset (cello-scale, modes ~60-250+ Hz, sub-Helmholtz gain taper). Each mode has `{freq, gain, qWood, qMetal}` (FR-006, FR-020)
- [ ] T026 [US1] Implement `BodyResonance` class skeleton in `dsp/include/krate/dsp/processors/body_resonance.h` per `contracts/body_resonance_api.h`: default constructor, deleted copy, default move, `prepare()`, `reset()`, `isPrepared()`, `setParams()`, `process()`, `processBlock()` stubs; include `<krate/dsp/primitives/biquad.h>` and `<krate/dsp/primitives/smoother.h>` (FR-001, FR-002)
- [ ] T027 [US1] Implement private helper `computeImpulseInvariantCoeffs(float freq, float Q, float sampleRate)` in `body_resonance.h` using the formula from FR-008: `theta = 2*pi*freq/sampleRate`, `R = exp(-pi*freq/(Q*sampleRate))`, `a1 = -2*R*cos(theta)`, `a2 = R*R`, `b0 = 1-R`, `b1 = 0`, `b2 = -(1-R)`; verify with `BiquadCoefficients::isStable()`
- [ ] T028 [US2] Implement modal preset interpolation in `body_resonance.h`: `interpolateModes(float size, float material)` that log-linearly interpolates frequencies (`f = exp(lerp(log(f_small), log(f_large), t))`) and linearly interpolates gains and Q factors between wood/metal presets, per FR-007 and FR-023; normalize gains so `sum(|gain_i|) <= 1.0` (FR-016)
- [ ] T029 [US2] Implement pole/zero domain interpolation in `body_resonance.h`: maintain `currentR_[8]`, `currentTheta_[8]`, `targetR_[8]`, `targetTheta_[8]`; per-block exponential smoothing toward targets using `smoothCoeff_`; recompute `BiquadCoefficients` from smoothed (R, theta) and call `modalBiquads_[i].setCoefficients()` (FR-009, FR-017)
- [ ] T030 [US2] Implement FDN delay-line scaling in `body_resonance.h`: use base lengths `{11, 17, 23, 31}` samples at 44.1 kHz and size=0.5 (matching R4); apply scaling formula `delay_i(size) = base_i * (0.3 + 0.7 * size^0.7) * (sampleRate / 44100)`, clamp to range [8, 80] * sampleRate/44100, store as fractional floats in `fdnDelayLengths_[4]` (FR-011, FR-022)
- [ ] T031 [US3] Implement FDN absorption filter coefficient computation in `body_resonance.h`: private helper `computeAbsorptionCoeffs(float material, float sampleRate)` implementing the Jot formula — T60(DC) controlled by material (wood: T60_DC=0.15s, metal: T60_DC=1.5s per R5), T60(Nyquist) scaled by material (wood: T60_Nyquist=0.02s, metal: T60_Nyquist=1.0s); cap to enforce RT60 limits from FR-013; store per-line gain `fdnAbsorptionGain_[4]` and one-pole `fdnAbsorptionCoeff_[4]` (FR-012, FR-013)
- [ ] T032 [US1] Implement `prepare(double sampleRate)` in `body_resonance.h`: store sample rate, configure `sizeSmoother_`, `materialSmoother_`, `mixSmoother_` to 5 ms smoothing time (per R11), snap all smoothers to defaults (size=0.5, material=0.5, mix=0.0), call `interpolateModes()`, compute initial absorption coefficients, compute initial crossover alpha, compute initial radiation HPF, reset all filter states; set `prepared_ = true` (FR-003, FR-017)
- [ ] T033 [US1] Implement `reset()` in `body_resonance.h`: zero all `fdnDelayBuffers_[4][128]`, zero `fdnAbsorptionState_[4]`, call `reset()` on each `modalBiquads_[i]`, `couplingPeakEq_`, `couplingHighShelf_`, `radiationHpf_`, reset `crossoverLpState_` to 0 (FR-002)
- [ ] T034 [US4] Implement radiation HPF in `body_resonance.h`: private helper `computeRadiationHPF(float lowestModeFreq, float sampleRate)` that sets `radiationHpf_` as a 12 dB/oct (second-order Butterworth) highpass at `0.7f * lowestModeFreq` using `radiationHpf_.configure(FilterType::Highpass, cutoff, 0.707f, 0.0f, sampleRate)` (FR-015, SC-010)
- [ ] T035 [US3] Implement coupling filter update in `body_resonance.h`: private helper `updateCouplingFilter(float material, float sampleRate)` — at material=0 (wood): peak EQ ~250 Hz +3 dB (per R9), high shelf ~2 kHz -2 dB; at material=1 (metal): peak EQ ~250 Hz +0.5 dB, high shelf flat; interpolate coefficients linearly; apply via `configure()` on `couplingPeakEq_` and `couplingHighShelf_` (FR-004)
- [ ] T036 [US5] Implement first-order crossover in `body_resonance.h` as inline one-pole LP/HP: `crossoverAlpha_ = exp(-2*pi*fc/sampleRate)` where `fc` scales as ~500 Hz at size=0.5 and linearly with size; LP state update: `crossoverLpState_ = crossoverAlpha_ * crossoverLpState_ + (1 - crossoverAlpha_) * input`; HP = input - LP; these feed modal bank (LP) and FDN (HP) (FR-014)
- [ ] T037 [US5] Implement 4-line FDN `processFDN(float input)` in `body_resonance.h`: read from each delay line with linear interpolation, apply per-line one-pole absorption filter `y = fdnAbsorptionGain_[i] * (x + fdnAbsorptionCoeff_[i] * fdnAbsorptionState_[i])`, apply 4x4 Hadamard matrix `H4 = (1/2)*[[1,1,1,1],[1,-1,1,-1],[1,1,-1,-1],[1,-1,-1,1]]`, write to each delay line, sum outputs; reference `FDNReverb::applyHadamard()` pattern for the butterfly (FR-010, FR-011, FR-012)
- [ ] T038 [US1] Implement `process(float input)` in `body_resonance.h`: early-out when `mixSmoother_.getCurrentValue() == 0.0f` for bit-identical bypass; otherwise: coupling filter -> crossover LP/HP -> modal bank (LP path) + FDN (HP path) -> sum -> radiation HPF -> mix blend with input (FR-002, FR-018, FR-021)
- [ ] T039 [US1] Implement `processBlock(const float* input, float* output, size_t numSamples)` in `body_resonance.h`: per-block update — advance smoothers for size/material/mix, call `interpolateModes()` and `computeAbsorptionCoeffs()` if parameters changed, then iterate calling `process()` per sample; use a `prevSize_` / `prevMaterial_` dirty flag to skip redundant coefficient recomputation (FR-017)
- [ ] T040 [US1] Implement `setParams(float size, float material, float mix)` in `body_resonance.h`: clamp inputs to [0.0, 1.0], call `sizeSmoother_.setTarget()`, `materialSmoother_.setTarget()`, `mixSmoother_.setTarget()` (FR-003, FR-017)

---

### 3.3 Build and Verify

- [ ] T041 Build `dsp_tests` target and fix all compiler errors: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T042 Fix all compiler warnings before running tests (C4244, C4267, C4100 per CLAUDE.md)
- [ ] T043 Run `build/windows-x64-release/bin/Release/dsp_tests.exe "[body_resonance]" 2>&1 | tail -20` and confirm all body resonance tests pass
- [ ] T044 Verify IEEE 754 compliance: check `body_resonance_tests.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage; add file to `-fno-fast-math` list in `dsp/tests/unit/CMakeLists.txt` if found (cross-platform requirement)
- [ ] T045 Run full `dsp_tests` suite to confirm no regressions: `build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5`

---

### 3.4 Commit Phase 3

- [ ] T046 Commit DSP core: `git add dsp/include/krate/dsp/processors/body_resonance.h dsp/tests/unit/processors/body_resonance_tests.cpp dsp/tests/unit/CMakeLists.txt && git commit -m "feat(dsp): add BodyResonance hybrid modal-FDN body coloring processor"`

**Checkpoint**: `body_resonance.h` is complete, all 18+ unit tests pass, zero compiler warnings, full `dsp_tests` suite green.

---

## Phase 4: Plugin Integration — Innexus Voice Engine

**Goal**: Wire `BodyResonance` into the Innexus processor and register the three VST3 parameters (IDs 850, 851, 852). After this phase, a host can control body resonance via automation.

**Independent Test**: Instantiate the Innexus processor stub, activate body params, process a MIDI note; assert output when mix=1.0 differs from output when mix=0.0 (body coloring is audible), and both cases produce valid non-NaN audio.

---

### 4.1 Integration Tests (Write FIRST — Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before integration begins.

- [ ] T047 [US1] Write failing test `TEST_CASE("Innexus - body parameter IDs are registered")` in `plugins/innexus/tests/unit/processor/body_resonance_integration_tests.cpp`: instantiate `Controller`, call `initialize()`, assert `getParameterObject(850)` != nullptr, `getParameterObject(851)` != nullptr, `getParameterObject(852)` != nullptr (FR-019)
- [ ] T048 [US1] Write failing test `TEST_CASE("Innexus - body params have correct defaults")` in `plugins/innexus/tests/unit/processor/body_resonance_integration_tests.cpp`: assert body size default normalized value corresponds to 0.5, material default to 0.5, mix default to 0.0 (FR-003, FR-019)
- [ ] T049 [US1] Write failing test `TEST_CASE("Innexus - body state persists across save/load")` in `plugins/innexus/tests/unit/processor/body_resonance_integration_tests.cpp`: set body params to non-default values, call `getState()`, call `setState()` with the result, assert params are restored (FR-019)
- [ ] T050 [US1] Write failing test `TEST_CASE("Innexus - body mix=0 is bit-identical bypass in voice chain")` in `plugins/innexus/tests/unit/processor/body_resonance_integration_tests.cpp`: process a voice chain with body mix=0 and record output; process again with mix=0 and assert exact equality (FR-018, SC-007)
- [ ] T051 [US1] Write failing test `TEST_CASE("Innexus - body mix=1 adds coloring to voice output")` in `plugins/innexus/tests/unit/processor/body_resonance_integration_tests.cpp`: process the voice chain with mix=0 and mix=1; assert the two outputs are not identical (body is doing work) (US1 acceptance scenario 1)

**Checkpoint**: Integration tests written and failing. Proceed to 4.2.

---

### 4.2 Parameter Registration

- [ ] T052 Add `kBodySizeId = 850`, `kBodyMaterialId = 851`, `kBodyMixId = 852` constants to `plugins/innexus/src/plugin_ids.h` in the parameter ID section, following the `k{Mode}{Parameter}Id` naming convention from CLAUDE.md (FR-019)
- [ ] T053 Add three `std::atomic<float>` members to `plugins/innexus/src/processor/processor.h`: `bodySize_`, `bodyMaterial_`, `bodyMix_` with default values 0.5f, 0.5f, 0.0f respectively (FR-019)
- [ ] T054 Register three `RangeParameter` instances in `plugins/innexus/src/controller/controller.cpp` `Controller::initialize()`: `kBodySizeId` ("Body Size", range 0-1, default 0.5), `kBodyMaterialId` ("Material", range 0-1, default 0.5), `kBodyMixId` ("Body Mix", range 0-1, default 0.0) (FR-019)

---

### 4.3 Voice Engine Wiring

- [ ] T055 Add `Krate::DSP::BodyResonance bodyResonance;` field to `InnexusVoice` struct in `plugins/innexus/src/processor/innexus_voice.h`; add `#include <krate/dsp/processors/body_resonance.h>` (data-model.md)
- [ ] T056 In `plugins/innexus/src/processor/processor.cpp` `prepare()`: after existing voice prepare calls, iterate all voices and call `voice.bodyResonance.prepare(sampleRate_)` (plan.md Phase 2, Task 5)
- [ ] T057 In `plugins/innexus/src/processor/processor.cpp` `reset()` (or equivalent voice reset path): call `voice.bodyResonance.reset()` for each voice (FR-002)
- [ ] T058 In `plugins/innexus/src/processor/processor.cpp` per-block voice loop: call `voice.bodyResonance.setParams(bodySize_.load(), bodyMaterial_.load(), bodyMix_.load())` once per block before sample processing; after the resonator output (`physicalSample`), replace the existing direct pass-through with `physicalSample = voice.bodyResonance.process(physicalSample)` before it reaches `PhysicalModelMixer`; verify all three resonator output paths pass through `bodyResonance.process()` before reaching the mixer: (1) modal resonator path, (2) waveguide resonator path, and (3) the crossfade/blend path between them — none of the three must bypass the body resonance stage (FR-021, plan.md Phase 2, Task 5)
- [ ] T059 In `plugins/innexus/src/processor/processor_params.cpp` `processParameterChanges()`: handle `kBodySizeId`, `kBodyMaterialId`, `kBodyMixId` — denormalize from [0,1] normalized value and store to atomic with `.store()` (FR-019)
- [ ] T060 In `plugins/innexus/src/processor/processor_state.cpp`: add body size, material, and mix to state save (`getState()`) and load (`setState()`) alongside existing parameters (FR-019)

---

### 4.4 Build and Verify Integration

- [ ] T061 Build `innexus_tests` and `Innexus` plugin targets: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target innexus_tests`
- [ ] T062 Fix all compiler errors and warnings before running tests
- [ ] T063 Run `build/windows-x64-release/bin/Release/innexus_tests.exe "[body_resonance]" 2>&1 | tail -20` and confirm all integration tests pass
- [ ] T064 Verify IEEE 754 compliance in `body_resonance_integration_tests.cpp`; add to `-fno-fast-math` list in `plugins/innexus/tests/unit/processor/CMakeLists.txt` if any IEEE 754 functions are used
- [ ] T065 Run full `innexus_tests` suite to confirm no regressions: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`
- [ ] T066 Run pluginval at strictness level 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"` and fix any failures

---

### 4.5 Commit Phase 4

- [ ] T067 Commit plugin integration: `git add plugins/innexus/src/plugin_ids.h plugins/innexus/src/processor/ plugins/innexus/src/controller/controller.cpp plugins/innexus/tests/unit/processor/body_resonance_integration_tests.cpp && git commit -m "feat(innexus): wire BodyResonance into voice engine, register params 850-852"`

**Checkpoint**: Innexus plugin builds, integration tests pass, pluginval passes, full `innexus_tests` suite green.

---

## Phase 5: QA — Static Analysis, Full Test Suite, Compliance

**Goal**: Final quality assurance, architecture documentation, and honest compliance verification.

---

### 5.1 Clang-Tidy Static Analysis

- [ ] T068 Generate Ninja build for clang-tidy if not present: open VS Developer PowerShell, run `"C:/Program Files/CMake/bin/cmake.exe" --preset windows-ninja` from repo root
- [ ] T069 Run clang-tidy on new and modified files: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` then `./tools/run-clang-tidy.ps1 -Target innexus -BuildDir build/windows-ninja`
- [ ] T070 Fix ALL clang-tidy errors and warnings in `body_resonance.h`, `body_resonance_tests.cpp`, and all modified Innexus files — own all findings, do not dismiss as pre-existing (per feedback_own_all_warnings.md)
- [ ] T071 Rebuild and re-run `dsp_tests` and `innexus_tests` after clang-tidy fixes to confirm tests still pass

---

### 5.2 Full Test Suite

- [ ] T072 Run full `dsp_tests` suite: `build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5` — assert "All tests passed"
- [ ] T073 Run full `innexus_tests` suite: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5` — assert "All tests passed"
- [ ] T074 Run pluginval final check: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"`

---

### 5.3 Architecture Documentation

- [ ] T075 Update `specs/_architecture_/layer-2-processors.md`: add `BodyResonance` entry with purpose ("hybrid modal bank + FDN post-resonator body coloring for instrument body simulation"), public API summary (`prepare`, `reset`, `setParams`, `process`, `processBlock`), file location (`dsp/include/krate/dsp/processors/body_resonance.h`), "when to use this" (post-resonator coloring stage in physical modelling instruments), and a note that `CrossoverFilter` was NOT reused (wrong order — LR4 vs first-order required)

---

### 5.4 Compliance Verification

- [ ] T076 For each FR-001 through FR-023: open `body_resonance.h` and/or the relevant plugin file, find the implementing code, record the file path and approximate line number in the spec.md compliance table — do NOT mark MET from memory
- [ ] T077 For each SC-001 through SC-010: run the corresponding test (or read its output from Phase 3/4 runs), record the actual measured value vs the spec threshold in the spec.md compliance table — do NOT mark MET without actual numbers
- [ ] T077b Measure CPU cost of `BodyResonance::processBlock()`: use a high-resolution timer (e.g., `std::chrono::high_resolution_clock`) to process 1,000,000 samples with `size=0.5f`, `material=0.5f`, `mix=1.0f` at 44100 Hz; compute average microseconds per sample; assert the result corresponds to less than 0.5% single-core CPU at 44.1 kHz (~22.7 µs/sample budget); record the actual measured value in the SC-006 evidence row of the compliance table
- [ ] T078 Fill the "Implementation Verification" table in `specs/131-body-resonance/spec.md` with MET/NOT MET/PARTIAL status and concrete evidence (file paths, line numbers, test names, measured values) per the Completion Honesty rule in CLAUDE.md
- [ ] T079 Answer the self-check questions: (1) No test thresholds relaxed? (2) No TODO/placeholder comments in new code? (3) No features removed from scope? (4) Would spec author consider this done? — if any answer is "yes", fix the gap before claiming completion

---

### 5.5 Final Commit

- [ ] T080 Commit QA and docs: `git add specs/_architecture_/layer-2-processors.md specs/131-body-resonance/spec.md && git commit -m "docs(131): update architecture docs and fill compliance table for body resonance"`
- [ ] T081 Verify feature branch `131-body-resonance` contains all commits and no implementation landed on `main`

**Checkpoint**: Static analysis clean, all tests pass, pluginval passes, compliance table filled with honest evidence, architecture docs updated.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 — confirms API contracts before writing tests
- **Phase 3 (DSP Core)**: Depends on Phase 2 — all unit tests and `body_resonance.h` implementation
- **Phase 4 (Plugin Integration)**: Depends on Phase 3 — `body_resonance.h` must be complete and tested first
- **Phase 5 (QA)**: Depends on Phase 4 — all code must be final before compliance verification

### User Story Dependencies

All five user stories are implemented inside a single `body_resonance.h` class. Their test tasks in Phase 3 can be written in parallel (different TEST_CASE blocks in the same file, no inter-story code dependencies). Implementation tasks have some ordering within Phase 3:

- **US1 (Core Coloring)**: T025-T033, T038-T040 — foundational class skeleton; all other stories depend on this
- **US2 (Size Control)**: T028-T030 — depends on T025 (preset data) and T027 (II design helper)
- **US3 (Material Control)**: T031, T035 — depends on T025; can run parallel to US2 after T027
- **US4 (Radiation HPF)**: T034 — depends on T025; can run after T027
- **US5 (No FDN Ringing)**: T036-T037 — depends on T030 (FDN delay lengths)

### Within Each Phase

- **Tests FIRST**: Tests written and confirmed failing before any implementation
- **Build before test**: Always build target before running tests (CLAUDE.md Build-Before-Test Discipline)
- **Commit at end of each phase**: Phases 3, 4, and 5 each end with a mandatory commit

### Parallel Opportunities

Within Phase 3 test writing (3.1), the following test groups can be written in parallel since they are independent TEST_CASE blocks:
- T007-T011 (US1 lifecycle and bypass tests)
- T012-T014 (US2 size tests) [P]
- T015-T018 (US3 material tests) [P]
- T019-T020 (US4 radiation HPF tests) [P]
- T021 (US5 FDN ringing test) [P]
- T022-T024 (sample rate and RT60 cap tests) [P]

Within Phase 4 (4.2 parameter registration), tasks T052-T054 touch different files and can be done in parallel:
- T052 (`plugin_ids.h`) [P]
- T053 (`processor.h`) [P]
- T054 (`controller.cpp`) [P]

---

## Implementation Strategy

### MVP Scope (Phase 3 + Phase 4, US1 only)

To deliver the minimum viable body resonance:
1. Complete Phase 1 (Setup) and Phase 2 (Foundational)
2. Write only US1 tests (T007-T011) and implement T025-T033, T038-T040
3. Wire into plugin (T052-T060)
4. Build and verify bypass + energy passivity pass
5. All five user stories are actually delivered together since they share the single `BodyResonance` class — the MVP is the whole implementation

### Incremental Delivery Order

1. Phase 1 + 2: Setup and dependency confirmation
2. Phase 3 tests (all failing): 18+ test cases covering all five user stories
3. Phase 3 implementation: `body_resonance.h` (~500-700 lines), tests go green
4. Phase 4: Plugin wiring, three VST3 params, integration tests, pluginval
5. Phase 5: Clang-tidy, full suite, compliance table, arch docs

---

## Notes

- **[P]** tasks = different files, no dependencies — can run in parallel
- **[Story]** label maps task to specific user story for traceability
- `CrossoverFilter` is explicitly NOT reused — it is LR4 (24 dB/oct); spec requires first-order (6 dB/oct). Implement inline in `body_resonance.h`.
- `FDNReverb` is explicitly NOT reused — it is 8-channel room-scale. Implement a new 4-line body-scale FDN.
- `ModalResonatorBank` is NOT reused — it uses Gordon-Smith magic circle oscillators, not biquad filters. The body modal bank uses impulse-invariant biquads.
- FDN delay buffer is `std::array<std::array<float, 128>, 4>` — fixed size, no heap allocation in audio path.
- All `process()` / `processBlock()` calls are `noexcept` — no exceptions on audio thread.
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture).
- **MANDATORY**: Write tests that FAIL before implementing (Principle XIII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each phase
- **MANDATORY**: Update `specs/_architecture_/layer-2-processors.md` before spec completion
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest evidence
- **NEVER claim completion if ANY requirement is not met** — document gaps honestly instead
