# Tasks: Dual Reverb System

**Input**: Design documents from `/specs/125-dual-reverb/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

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

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in the appropriate `tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/effects/fdn_reverb_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

---

## Phase 1: Setup (Branch & Build Configuration)

**Purpose**: Checkout the feature branch and configure the build system to include the new SIMD source file.

- [X] T001 Checkout or create the `125-dual-reverb` feature branch (verify `git branch` before any changes)
- [X] T002 Add `fdn_reverb_simd.cpp` to the KrateDSP target in `dsp/CMakeLists.txt` (source file does not yet exist -- just add the entry so the build system is ready)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Establish the shared infrastructure that all three user stories depend on. The `ReverbParams` struct already exists and is unchanged. This phase validates that the existing build is green before any modifications begin.

**CRITICAL**: No user story work can begin until this phase is complete.

- [X] T003 Build `dsp_tests` target and verify all existing DSP tests pass before making any changes: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5`
- [X] T004 Build `ruinae_tests` target and verify all existing Ruinae tests pass before any changes: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe 2>&1 | tail -5`

**Checkpoint**: Green baseline confirmed -- user story implementation can now begin.

---

## Phase 3: User Story 1 - Optimized Plate Reverb Performance (Priority: P1) - MVP

**Goal**: Optimize the existing Dattorro plate reverb (Gordon-Smith LFO, block-rate parameter smoothing, contiguous delay buffer, denormal removal) to deliver at least 15% CPU reduction at 44.1kHz with modulation enabled, while preserving the existing `ReverbParams` interface and API.

**Independent Test**: Build and run `dsp_tests`. Benchmark CPU before/after with modulation enabled (modRate=1.0, modDepth=0.5) at 44.1kHz. Run A/B test that both old and new versions produce output within acceptable tolerance. This story is completely independent of the FDN reverb and the Ruinae integration.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.
> Read the existing `dsp/tests/unit/effects/reverb_test.cpp` before adding tests to it.

- [X] T005 [US1] Write failing tests for Gordon-Smith LFO equivalence in `dsp/tests/unit/effects/reverb_test.cpp`: a test that calls `processBlock` with `modRate=1.0, modDepth=0.5` and verifies output is within `Approx().margin(0.01f)` of a reference (computed with the old implementation or golden snapshot) -- this will fail until the new LFO is implemented
- [X] T006 [US1] Write failing test for block-rate parameter smoothing in `dsp/tests/unit/effects/reverb_test.cpp`: verify that a parameter change mid-block (calling `setParams` between two `processBlock` calls on a 32-sample buffer) is latched at the next 16-sample sub-block boundary rather than applied mid-block -- currently the code applies it per-sample so this structural test will fail after restructuring
- [X] T007 [US1] Write failing test for contiguous buffer size in `dsp/tests/unit/effects/reverb_test.cpp`: after calling `prepare(44100.0)`, verify `reverb.totalBufferSize()` returns a value greater than 0 (this will fail until the `totalBufferSize_` member and accessor are added)
- [X] T008 [US1] Write a CPU benchmark test (tagged `[.perf]`) in `dsp/tests/unit/effects/reverb_test.cpp` that processes 10 seconds of audio at 44.1kHz with modulation enabled and records elapsed time -- this establishes the baseline before optimization and will be re-run after to confirm SC-001

### 3.2 Implementation for User Story 1

- [X] T009 [US1] Replace `std::sin`/`std::cos` LFO in `dsp/include/krate/dsp/effects/reverb.h` with Gordon-Smith phasor (FR-001): replace `lfoPhase_` and `lfoPhaseIncrement_` members with `sinState_`, `cosState_`, `lfoEpsilon_`; update `prepare()` to init states and `process()` to use the magic circle formula (`sinState_ += lfoEpsilon_ * cosState_; cosState_ -= lfoEpsilon_ * sinState_`) matching the proven pattern in `dsp/include/krate/dsp/processors/particle_oscillator.h`
- [X] T010 [US1] Refactor `processBlock` in `dsp/include/krate/dsp/effects/reverb.h` to use 16-sample sub-blocks (FR-002, FR-003): restructure the loop to call all 9 `OnePoleSmoother::process()` calls plus `setCutoff()` and `setCoefficient()` exactly once per 16-sample sub-block using `advanceSamples(blockLen - 1)` for the remaining samples; hold smoothed values constant within each sub-block inner loop; ensure parameter changes arriving between `processBlock` calls are applied at the next sub-block boundary
- [X] T011 [US1] Allocate a single contiguous `std::vector<float> contiguousBuffer_` in `prepare()` in `dsp/include/krate/dsp/effects/reverb.h` (FR-004): compute the total size dynamically from the sample rate by scaling the 13 canonical Dattorro delay lengths from the 29.76kHz reference values; partition the buffer into 13 sections using power-of-2 per-section sizes for masking efficiency; store total size in `totalBufferSize_` and expose it via a `totalBufferSize()` const accessor; wire existing delay-read/write logic to use the contiguous buffer offsets
- [X] T012 [US1] Remove redundant `flushDenormal()` calls from the Dattorro tank output path in `dsp/include/krate/dsp/effects/reverb.h` (FR-005): remove calls on `tankAOut_` and `tankBOut_`; add a comment citing FTZ/DAZ assumption and referencing FR-005
- [X] T013 [US1] Build `dsp_tests` and verify all tests pass (zero warnings): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5`; note that any attempt to call the 5 public Reverb API methods (`prepare`, `reset`, `setParams`, `process`, `processBlock`) with their pre-existing signatures will fail to compile if signatures changed -- successful compilation of existing callers is the FR-006 API-preservation check
- [X] T014 [US1] Run the CPU benchmark test with `[.perf]` tag before and after changes to confirm SC-001 (at least 15% CPU reduction at 44.1kHz with modRate=1.0, modDepth=0.5): `build/windows-x64-release/bin/Release/dsp_tests.exe "[.perf]"`; record actual measured values in `spec.md` SC-001 evidence column

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T015 [US1] Verify IEEE 754 compliance: check `dsp/tests/unit/effects/reverb_test.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage; if present, add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 3.4 Commit (MANDATORY)

- [X] T016 [US1] Commit all User Story 1 work with message: `feat(dsp): optimize Dattorro reverb - Gordon-Smith LFO, block-rate smoothing, contiguous buffer (FR-001 to FR-006)`

**Checkpoint**: User Story 1 should be fully functional, tested, benchmarked, and committed. The existing Reverb API is unchanged, no other plugin is affected.

---

## Phase 4: User Story 2 - New FDN Hall Reverb (Priority: P2)

**Goal**: Create the new `FDNReverb` class at Layer 4 with 8-channel FDN architecture, Hadamard diffuser, Householder feedback, Gordon-Smith quadrature LFO modulation on 4 channels, SIMD acceleration via Google Highway, and validate it meets SC-002 (<2% CPU for 512 samples at 44.1kHz), SC-004 (stability under all parameter combos), SC-005 (NED >= 0.8 within 50ms), and SC-007 (decay correlates with roomSize).

**Independent Test**: Build and run `dsp_tests`. FDNReverb can be instantiated, prepared, and tested entirely from `dsp/tests/unit/effects/fdn_reverb_test.cpp` with no dependency on Ruinae or Phase 3.

### 4.1 Tests for User Story 2 - Phase A: Scalar FDN (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.
> Write all tests in `dsp/tests/unit/effects/fdn_reverb_test.cpp` (new file).

- [X] T017 [P] [US2] Write failing test: `FDNReverb` default-constructs, calling `prepare(48000.0)` does not crash and `isPrepared()` returns true; calling `reset()` does not crash; test lives in `dsp/tests/unit/effects/fdn_reverb_test.cpp`
- [X] T018 [P] [US2] Write failing test: after `prepare(48000.0)` and `setParams` with default `ReverbParams`, processing a stereo impulse via `processBlock` produces non-zero output (reverb builds up) and remains finite (no NaN/Inf) for 10 seconds of white noise -- uses `std::isfinite` so this file needs `-fno-fast-math` in `dsp/tests/CMakeLists.txt`
- [X] T019 [P] [US2] Write failing test: `processBlock` with `params.freeze = true` produces sustained tail energy without growth; measure RMS of output window 0.5s after input stops and verify it is within 5% of the window immediately before input stopped (SC-004 freeze stability)
- [X] T020 [P] [US2] Write failing test: SC-007 decay correlation -- process white noise for 1 second, then measure decay time at `roomSize=0.3` vs `roomSize=0.8`; assert decay at 0.8 is at least 50% longer than at 0.3
- [X] T021 [P] [US2] Write failing test: SC-005 echo density -- process an impulse, collect the impulse response for 50ms, compute NED using 1ms sliding window (NED = stddev(windowed_IR) / expected_stddev_Gaussian), assert NED >= 0.8 within 50ms at default params at 48kHz
- [X] T022 [P] [US2] Write two failing tests for FR-009 delay length validation in `dsp/tests/unit/effects/fdn_reverb_test.cpp`:
  - **Histogram check (FR-009 rule 5)**: A constexpr or static (compile-time or early-init) test that simulates arrival-time bins (1ms windows, summing delay-length combinations at 48kHz reference lengths [149, 189, 240, 305, 387, 492, 625, 794]) and asserts >= 50 unique occupied bins within 50ms. This must NOT use runtime audio processing -- it is a design-time validation of the delay length set. Implement as a `static_assert` or a `STATIC_REQUIRE` (Catch2 compile-time assertion) where possible; if constexpr computation is too complex, implement as a `TEST_CASE` that runs only the static computation, with a comment marking it as a design-time check.
  - **Anti-ringing rule check (FR-009 rule 4)**: For all pairs and triples of the 8 reference delay lengths at 48kHz ([149, 189, 240, 305, 387, 492, 625, 794]), compute the total cycle length for 2-hop and 3-hop and 4-hop short feedback cycles (sum of the N selected delay lengths), and assert each cycle length exceeds `2 * delayLengths_[7]` = `2 * 794` = 1588 samples. This verifies no short resonant cycle can build up. Implement as a static test case (no audio processing needed).
- [X] T023 [P] [US2] Write failing test: SC-004 NaN/Inf input safety (FR-019) -- feed NaN and Inf values into `process()` and verify output remains finite; uses `std::isnan`/`std::isinf` so needs `-fno-fast-math`
- [X] T024 [P] [US2] Write failing test: FR-020 multi-sample-rate -- prepare at 8000, 44100, 96000, 192000 Hz; verify `isPrepared()` true and that a short impulse produces finite output at all rates
- [X] T025 [US2] Write a CPU benchmark test (tagged `[.perf]`) in `dsp/tests/unit/effects/fdn_reverb_test.cpp` that processes 512-sample blocks at 44.1kHz for 5 seconds and records elapsed wall-clock time to verify SC-002. The threshold is: average `processBlock` call duration must be under 0.23ms (= 2% of the 11.6ms real-time budget for 512 samples at 44.1kHz). Run at least 3 times and average the result.

### 4.2 Implementation for User Story 2 - Phase A: Scalar FDN

- [X] T026 [US2] Create `dsp/include/krate/dsp/effects/fdn_reverb.h` matching the contract in `specs/125-dual-reverb/contracts/fdn_reverb_api.h`: declare `FDNReverb` in `Krate::DSP` namespace with constants `kNumChannels=8`, `kNumModulatedChannels=4`, `kNumDiffuserSteps=4`, `kSubBlockSize=16`; declare all public methods (`prepare`, `reset`, `setParams`, `process`, `processBlock`, `isPrepared`); declare SoA state arrays with `alignas(32)`: `delayOutputs_[8]`, `filterStates_[8]`, `filterCoeffs_[8]`, `dcBlockX_[8]`, `dcBlockY_[8]`, `feedbackGains_[8]`; declare contiguous buffer members, LFO state (`lfoSinState_[4]`, `lfoCosState_[4]`, `lfoEpsilon_`, `lfoModChannels_[4]`, `lfoMaxExcursion_`), `preDelay_` (type `DelayLine`), and `sampleRate_`
- [X] T027 [US2] Implement `FDNReverb::prepare(double sampleRate)` in `dsp/include/krate/dsp/effects/fdn_reverb.h`: scale the 8 reference delay lengths from 48kHz using `round(ref_i * sampleRate / 48000.0)`, enforce 3ms min / 20ms max per FR-009; allocate `delayBuffers_` as a single contiguous `std::vector<float>` with power-of-2 per-section sizes; prepare `preDelay_` using `DelayLine::prepare(sampleRate, 0.1f)`; init Gordon-Smith phasors (`lfoSinState_[j] = sin(j * kPi/2)`, `lfoCosState_[j] = cos(j * kPi/2)` for independent phase offsets); set `lfoModChannels_` to indices `[4, 5, 6, 7]` (the 4 longest delays after sorting); call `reset()`
- [X] T028 [US2] Implement `FDNReverb::reset()` in `dsp/include/krate/dsp/effects/fdn_reverb.h`: zero-fill `delayBuffers_`, `diffuserBuffers_`, all SoA arrays, and re-init LFO phasor states to their initial phase offsets
- [X] T029 [US2] Implement `FDNReverb::setParams(const ReverbParams&)` in `dsp/include/krate/dsp/effects/fdn_reverb.h`: map `roomSize` to `feedbackGains_[i]` using `0.75f + params.roomSize * 0.2495f` (range 0.75-0.9995); map `damping` to `filterCoeffs_[i]` using the same Hz-based one-pole coefficient formula as Dattorro (damping=0 -> 20kHz, damping=1 -> 200Hz); store `freeze`, `mix`, `width`, `preDelayMs`, `diffusion`, `modRate`, `modDepth` for use in `process()`; update `lfoEpsilon_ = 2.0f * std::sin(kPi * params.modRate / static_cast<float>(sampleRate_))`; update `lfoMaxExcursion_` as `params.modDepth * (delayLengths_[7] * 0.05f)` (5% of longest delay)
- [X] T030 [US2] Implement the Hadamard FWHT diffuser (FR-008) as a private inline method `applyHadamard(float x[8])` in `dsp/include/krate/dsp/effects/fdn_reverb.h`: implement the 3-stage butterfly (stage 1: stride=4, stage 2: stride=2, stage 3: stride=1, each doing N/2 add/subtract pairs) followed by normalization by `1/sqrt(8)`; also implement `applyDiffuserStep(float x[8], size_t stepIndex)` that reads from and writes to a diffuser delay section then applies Hadamard
- [X] T031 [US2] Implement the Householder feedback matrix (FR-010) as a private inline method `applyHouseholder(float x[8])` in `dsp/include/krate/dsp/effects/fdn_reverb.h`: the method modifies `x[8]` in-place. Compute `sum = x[0]+...+x[7]` (8 additions), then `scaled = sum * 0.25f` (1 multiply, since 2/N = 2/8 = 0.25), then `x[i] -= scaled` for each channel (8 subtractions). Total: 17 arithmetic operations for N=8 — O(N), not 2N-1.
- [X] T032 [US2] Implement `FDNReverb::process(float& left, float& right)` in `dsp/include/krate/dsp/effects/fdn_reverb.h` with the full scalar signal path (FR-007 through FR-022): (1) NaN/Inf guard replacing with 0.0f; (2) mono sum + pre-delay via `preDelay_`; (3) 4-step Hadamard diffuser; (4) feedback: read 8 delay outputs, apply one-pole damping filters (`filterStates_[i] = filterCoeffs_[i] * delayOutputs_[i] + (1-filterCoeffs_[i]) * filterStates_[i]`), apply DC blockers (`dcBlockY_[i] = filterStates_[i] - dcBlockX_[i] + 0.9999f * dcBlockY_[i]; dcBlockX_[i] = filterStates_[i]`), apply Householder, apply feedbackGains, write back to delay lines with LFO modulation on channels `lfoModChannels_[0..3]`; (5) stereo output mix using `width` parameter and dry/wet blend
- [X] T033 [US2] Implement `FDNReverb::processBlock(float* left, float* right, size_t numSamples)` in `dsp/include/krate/dsp/effects/fdn_reverb.h` as a simple loop calling `process(left[i], right[i])` for each sample. This scalar implementation intentionally does NOT use 16-sample sub-blocks -- FR-016 applies only to the SIMD `processBlock` path introduced in Phase B (T036). The scalar Phase A implementation is correct without sub-blocks.
- [X] T034 [US2] Build and run failing tests to verify the scalar implementation makes them pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "FDN*" 2>&1 | tail -10`

### 4.3 Implementation for User Story 2 - Phase B: SIMD Acceleration

> **Constitution Principle IV**: SIMD follows scalar-first workflow. Phase B only begins after all Phase A tests pass.

- [X] T035 [US2] Create `dsp/include/krate/dsp/effects/fdn_reverb_simd.cpp` (new file) following the Highway self-inclusion pattern used in `spectral_simd.cpp`: add `#include "hwy/foreach_target.h"`, `#include "hwy/highway.h"`, `HWY_BEFORE_NAMESPACE()` macro, implement SIMD kernels inside `HWY_NAMESPACE` -- see `dsp/include/krate/dsp/core/spectral_simd.cpp` for the exact pattern; implement three SIMD kernels: (a) `ApplyFilterBankSIMD` for the 8-channel one-pole filter bank using `hn::Load`, `hn::MulAdd`, `hn::Store`, (b) `ApplyHadamardSIMD` for the FWHT butterfly stages using load/add/subtract pairs, (c) `ApplyHouseholderSIMD` for `hn::ReduceSum` + `hn::Set` + `hn::Sub`; export all three via `HWY_EXPORT` / `HWY_DYNAMIC_DISPATCH`
- [X] T036 [US2] Wire the SIMD kernels into `FDNReverb::processBlock` in `dsp/include/krate/dsp/effects/fdn_reverb.h` (FR-015, FR-016): this is the task that satisfies FR-016. Replace the per-sample scalar loop from T033 with a 16-sample sub-block loop that dispatches the SIMD filter bank kernel (`ApplyFilterBankSIMD`), Hadamard kernel (`ApplyHadamardSIMD`), and Householder kernel (`ApplyHouseholderSIMD`) once per 16-sample sub-block. Keep the scalar `process()` method unchanged for single-sample use. Guard the SIMD path with `isPrepared()` and fall back to scalar if not prepared.
- [X] T037 [US2] Build `dsp_tests` and re-run all FDN tests to confirm SIMD implementation passes the same tests as scalar: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "FDN*" 2>&1 | tail -10`
- [X] T038 [US2] Run the FDN CPU benchmark (`[.perf]` tag) to measure SC-002 (<2% CPU for 512 samples at 44.1kHz): `build/windows-x64-release/bin/Release/dsp_tests.exe "[.perf]"` (filter to FDN bench); record actual measured value in `spec.md` SC-002 evidence column; if threshold is not met, profile and optimize before moving to Phase 5

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T039 [US2] Verify IEEE 754 compliance for `dsp/tests/unit/effects/fdn_reverb_test.cpp`: this file uses `std::isnan`/`std::isinf`/`std::isfinite` for NaN/Inf stability tests (T018, T023); add it to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` using the pattern from the template above; rebuild and confirm tests still pass

### 4.5 Commit (MANDATORY)

- [ ] T040 [US2] Commit all User Story 2 work with message: `feat(dsp): add FDNReverb - 8-channel FDN with Hadamard diffuser, Householder feedback, Highway SIMD (FR-007 to FR-022)`

**Checkpoint**: `FDNReverb` is fully functional, all tests pass, SC-002 and SC-005 verified. This delivers the new hall reverb as a standalone DSP component independent of Ruinae.

---

## Phase 5: User Story 3 - Reverb Type Selection in Ruinae (Priority: P3)

**Goal**: Integrate both reverb types into Ruinae with a `kReverbTypeId` parameter (0=Plate, 1=Hall), smooth 30ms equal-power crossfade switching via `equalPowerGains()`, correct state serialization (version 5 with backward compatibility to version 4), and all shared reverb parameters routing to the active reverb type.

**Independent Test**: Build and run `ruinae_tests`. Switch reverb types during audio processing in tests and verify no clicks (smooth crossfade), correct state save/load (SC-006), and parameter routing (FR-027). Depends on User Story 1 (optimized Dattorro in the DSP library) and User Story 2 (FDNReverb in the DSP library) being committed first.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.
> Write tests in `plugins/ruinae/tests/unit/processor/reverb_type_test.cpp` (new file). Read the existing processor test files first to match their Catch2 patterns and fixture setup.

- [ ] T041 [P] [US3] Write failing test: `kReverbTypeId` parameter exists and is registered in `plugins/ruinae/tests/unit/processor/reverb_type_test.cpp`: create a `RuinaeController` instance, call `initialize`, and verify `getParameterObject(kReverbTypeId) != nullptr` -- this will fail until the parameter is registered in `controller.cpp`
- [ ] T042 [P] [US3] Write failing test: reverb type switching triggers crossfade -- in `plugins/ruinae/tests/unit/processor/reverb_type_test.cpp`, call `setReverbType(1)` on an effects chain initialized with `prepare(44100.0, 512)`, then process 2048 samples; assert that the output is non-zero and finite during the crossfade window (first ~1323 samples at 44.1kHz for 30ms); uses `std::isfinite` so check CMakeLists.txt
- [ ] T043 [P] [US3] Write failing test: SC-003 click-free switching -- process a 440Hz sine wave through Ruinae processor with reverb type switch mid-block; compute the maximum instantaneous amplitude change between consecutive samples across the crossfade window and assert it is below 0.01 (no step discontinuity)
- [ ] T044 [P] [US3] Write failing test: state save/load with reverb type (FR-026, SC-006) -- in `plugins/ruinae/tests/unit/processor/reverb_type_test.cpp`, set reverbType=1 (Hall), call `getState`, reset the processor, call `setState` with the saved stream, and verify reverbType is restored to 1
- [ ] T045 [P] [US3] Write failing test: backward compatibility load (FR-028) -- construct a version-4 state binary stream (without reverbType field), call `setState` on the processor, and verify `reverbType` defaults to 0 (Plate) without crash
- [ ] T046 [P] [US3] Write failing test: FR-027 parameter routing -- set all reverb parameters (size, damping, width, mix, preDelayMs, diffusion, freeze, modRate, modDepth) on the processor; switch to Hall; process audio; verify output energy changes appropriately (non-zero output from FDN reverb, correlated with `mix` value)
- [ ] T046B [P] [US3] Write failing test: FR-029 freeze+switch -- enable freeze on the effects chain (set params with `freeze=true`, process briefly to build up a frozen tail); call `setReverbType(1)` to switch to Hall; process the crossfade window (~30ms at 44.1kHz = ~1323 samples); assert: (a) both reverbs produce non-zero output during the crossfade (outgoing frozen plate still audible), (b) the incoming FDN reverb is also in freeze mode from the start of the crossfade (verify by checking it still sustains after input stops), (c) no click at the switch point (amplitude delta < 0.01 per sample)

### 5.2 Implementation for User Story 3

- [ ] T047 [US3] Add `kReverbTypeId = 1709` to `plugins/ruinae/src/plugin_ids.h` and bump `kCurrentStateVersion` from 4 to 5 (FR-023, FR-028): add the ID constant in the Reverb ID range comment block (1700-1799), update the version constant
- [ ] T048 [US3] Add `std::atomic<int32_t> reverbType{0}` field to `RuinaeReverbParams` in `plugins/ruinae/src/parameters/reverb_params.h` (data-model entity 4): add the new field, add a case `kReverbTypeId` in `handleReverbParamChange` that stores `static_cast<int32_t>(std::round(value))` with `memory_order_relaxed`
- [ ] T049 [US3] Add `FDNReverb fdnReverb_` instance and crossfade state members to `RuinaeEffectsChain` in `plugins/ruinae/src/engine/ruinae_effects_chain.h` (data-model entity 5): add the members listed in the contract (`fdnReverb_`, `activeReverbType_`, `incomingReverbType_`, `reverbCrossfading_`, `reverbCrossfadeAlpha_`, `reverbCrossfadeIncrement_`); add `#include <krate/dsp/effects/fdn_reverb.h>` at the top
- [ ] T050 [US3] Implement `RuinaeEffectsChain::setReverbType(int type)` in `plugins/ruinae/src/engine/ruinae_effects_chain.h` (FR-025): if `type == activeReverbType_` and not crossfading, return early (no-op); otherwise, if freeze is currently active (`params.freeze == true`), call `setParams` on the incoming reverb FIRST so it enters freeze mode before the crossfade begins (FR-029); then set `incomingReverbType_ = type`, `reverbCrossfading_ = true`, `reverbCrossfadeAlpha_ = 0.0f`, compute `reverbCrossfadeIncrement_` using `crossfadeIncrement(30.0f, sampleRate_)` (30ms fixed duration per FR-025, research R9). Also implement `setReverbTypeDirect(int type)` (see data-model.md Entity 5): set `activeReverbType_ = type`, `reverbCrossfading_ = false`, `reverbCrossfadeAlpha_ = 0.0f` -- no crossfade, for state load only.
- [ ] T051 [US3] Implement the `processReverbSlot` helper method in `plugins/ruinae/src/engine/ruinae_effects_chain.h` (FR-024, FR-025): when not crossfading, process only `activeReverbType_` (0=`reverb_`, 1=`fdnReverb_`); when crossfading, copy input to temp buffers, process outgoing reverb into temp buffers and incoming reverb in-place, blend per sample using `equalPowerGains(reverbCrossfadeAlpha_)` (clamp alpha to [0, 1] before calling), advance `reverbCrossfadeAlpha_` by `reverbCrossfadeIncrement_` each sample; on `reverbCrossfadeAlpha_ >= 1.0f`, call `reset()` on the outgoing reverb, set `activeReverbType_ = incomingReverbType_`, set `reverbCrossfading_ = false`; replace the existing reverb call in `processInternal()` with `processReverbSlot()`
- [ ] T052 [US3] Update `RuinaeEffectsChain::prepare()` in `plugins/ruinae/src/engine/ruinae_effects_chain.h` to call `fdnReverb_.prepare(sampleRate)` alongside the existing `reverb_.prepare(sampleRate)` call
- [ ] T053 [US3] Update `RuinaeEffectsChain::applyReverbParams()` in `plugins/ruinae/src/engine/ruinae_effects_chain.h` (FR-027): call `fdnReverb_.setParams(params)` alongside `reverb_.setParams(params)` so both reverb instances always receive current parameters regardless of which is active
- [ ] T054 [US3] Add `reverbType` to state serialization in `plugins/ruinae/src/processor/processor.cpp` (FR-026, FR-028): in the save path, append `reverbParams.reverbType.load()` after the existing reverb params; in the load path, check `stateVersion < 5` -- if true, set `reverbType = 0` (backward compat, SC-006); if version 5, load the stored value; call `effectsChain_.setReverbTypeDirect(reverbType)` (NOT `setReverbType`) to restore the saved type without triggering a crossfade. Using `setReverbTypeDirect` (documented in data-model.md Entity 5 and contracts/reverb_type_integration.h) ensures state restoration is instant and click-free on load.
- [ ] T055 [US3] Add `reverbType` atomic member to `plugins/ruinae/src/processor/processor.h` and handle `kReverbTypeId` in `processParameterChanges()` in `plugins/ruinae/src/processor/processor.cpp`: store the normalized value (which is 0.0 or 1.0 for a 2-step StringListParameter) rounded to int and passed to `effectsChain_.setReverbType()`
- [ ] T056 [US3] Register `kReverbTypeId` as a `StringListParameter` in `plugins/ruinae/src/controller/controller.cpp` (FR-023): following the contract in `specs/125-dual-reverb/contracts/reverb_type_integration.h`, create a `StringListParameter` with title `STR16("Reverb Type")`, append `STR16("Plate")` and `STR16("Hall")`, add to `parameters`; place it after the existing reverb parameter registrations
- [ ] T057 [US3] Build `ruinae_tests` and verify all new reverb type tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe 2>&1 | tail -5`
- [ ] T058 [US3] Run pluginval to verify VST3 compliance after plugin source changes: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"` -- confirm all tests pass at strictness level 5

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T059 [US3] Verify IEEE 754 compliance for `plugins/ruinae/tests/unit/processor/reverb_type_test.cpp`: if any test uses `std::isfinite`/`std::isnan`, add the file to the `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt`

### 5.4 Commit (MANDATORY)

- [ ] T060 [US3] Commit all User Story 3 work with message: `feat(ruinae): add reverb type selector (Plate/Hall) with equal-power crossfade and state v5 (FR-023 to FR-029)`

**Checkpoint**: Dual reverb system is fully integrated into Ruinae. Users can switch between Plate and Hall reverb types with smooth transitions. State saves and loads correctly with backward compatibility.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Run the full test suite across all targets, verify all success criteria, and confirm no regressions.

- [ ] T061 [P] Build and run all DSP tests to confirm no regressions from the Dattorro optimization: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5`
- [ ] T062 [P] Build and run all Ruinae tests to confirm no regressions from plugin integration: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe 2>&1 | tail -5`
- [ ] T063 SC-004 stability sweep: run the full parameter combination stability test (roomSize 0.0/0.5/1.0, damping 0.0/0.5/1.0, freeze on/off) at sample rates 8000, 44100, 96000, 192000 Hz for 10 seconds each for both reverb types; confirm no NaN/Inf/unbounded growth; record results in `spec.md`
- [ ] T064 SC-007 verification: for both Dattorro and FDN reverbs, measure T60 decay time at roomSize=0.2, 0.5, 0.8; confirm each larger roomSize produces a longer decay time; record measured T60 values in `spec.md`

---

## Phase 7: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion (Constitution Principle XIII).

- [ ] T065 [P] Update `specs/_architecture_/layer-4-effects.md` (or equivalent Layer 4 section): add an entry for `FDNReverb` with purpose ("8-channel FDN hall reverb with SIMD acceleration"), public API summary, file location (`dsp/include/krate/dsp/effects/fdn_reverb.h`), when to use it (vs Dattorro plate), and note that it accepts the same `ReverbParams` interface
- [ ] T066 [P] Update `specs/_architecture_/layer-4-effects.md` (or equivalent): update the `Reverb` (Dattorro) entry to note the optimizations added (Gordon-Smith LFO, block-rate smoothing, contiguous buffer), referencing spec 125
- [ ] T067 Commit architecture documentation: `docs(architecture): add FDNReverb entry, update Dattorro optimization notes (spec-125)`

---

## Phase 8: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification.

- [ ] T068 Generate `compile_commands.json` with Ninja preset if not current: `"C:/Program Files/CMake/bin/cmake.exe" --preset windows-ninja` (run from VS Developer PowerShell if needed)
- [ ] T069 Run clang-tidy on DSP targets (new and modified files): `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`
- [ ] T070 Run clang-tidy on Ruinae plugin target: `./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja`
- [ ] T071 Fix all clang-tidy errors (blocking); review warnings and add `// NOLINT(rule): reason` comments for intentional suppressions in SIMD/DSP code where appropriate
- [ ] T072 Rebuild and re-run all tests after any clang-tidy fixes: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests ruinae_tests && build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -3 && build/windows-x64-release/bin/Release/ruinae_tests.exe 2>&1 | tail -3`

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion (Constitution Principle XV).

### 9.1 Requirements Verification

- [ ] T073 Review ALL FR-001 through FR-006 (Dattorro optimizations): open `dsp/include/krate/dsp/effects/reverb.h`, confirm each requirement is satisfied, record file path and line numbers in `spec.md` compliance table
- [ ] T074 Review ALL FR-007 through FR-022 (FDN reverb): open `dsp/include/krate/dsp/effects/fdn_reverb.h` and `fdn_reverb_simd.cpp`, confirm each requirement is satisfied with file path and line numbers in `spec.md`
- [ ] T075 Review ALL FR-023 through FR-029 (Ruinae integration): open `plugin_ids.h`, `reverb_params.h`, `ruinae_effects_chain.h`, `processor.cpp`, `controller.cpp`, confirm each requirement is satisfied with evidence in `spec.md`; for FR-029 (freeze+switch), also verify the T046B test passes and confirm freeze is applied to the incoming reverb before crossfade starts
- [ ] T076 Verify SC-001 (15%+ CPU reduction for Dattorro): run perf benchmark, record actual before/after numbers in `spec.md`; if not met, profile and optimize before proceeding
- [ ] T077 Verify SC-002 (FDN <2% CPU for 512 samples at 44.1kHz): run perf benchmark, record actual measured value in `spec.md`; if not met, profile SIMD kernels before proceeding
- [ ] T078 Verify SC-003 (no audible clicks on type switch): run the click-detection test from T043; record pass/fail and measured max amplitude delta in `spec.md`
- [ ] T079 Verify SC-004 (stability for all params/rates/10s): run T063 results; record pass/fail in `spec.md`
- [ ] T080 Verify SC-005 (NED >= 0.8 within 50ms): run the NED test from T021; record actual NED value at 50ms in `spec.md`
- [ ] T081 Verify SC-006 (backward-compatible state load): run T045 test; confirm version-4 state loads correctly with Plate default; record evidence in `spec.md`
- [ ] T082 Verify SC-007 (decay correlates with roomSize): run T064 results; record T60 values at each roomSize in `spec.md`

### 9.2 Fill Compliance Table

- [ ] T083 Update the "Implementation Verification" section in `specs/125-dual-reverb/spec.md` with MET/NOT MET/PARTIAL/DEFERRED status and specific evidence (file paths, line numbers, test names, actual measured values) for every FR-xxx and SC-xxx row -- NO generic claims, every row must cite specific evidence

### 9.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T084 Confirm all self-check questions answered "no" (or gaps documented honestly in `spec.md`)

---

## Phase 10: Final Completion

- [ ] T085 Run full test suite one final time and confirm all tests pass: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure 2>&1 | tail -10`
- [ ] T086 Run pluginval final verification: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"`
- [ ] T087 Commit any remaining cleanup with message: `chore(125-dual-reverb): final cleanup, compliance table complete`
- [ ] T088 Claim completion ONLY if all requirements in the compliance table are MET (or gaps explicitly approved by user)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies -- start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 -- verifies green baseline; BLOCKS all user story work
- **Phase 3 (US1 - Dattorro optimization)**: Depends on Phase 2 completion -- modifies `dsp/include/krate/dsp/effects/reverb.h`; does NOT depend on US2 or US3
- **Phase 4 (US2 - FDN reverb)**: Depends on Phase 2 completion -- creates new files; does NOT depend on US1 or US3; MUST complete Phase A (scalar) before Phase B (SIMD)
- **Phase 5 (US3 - Ruinae integration)**: Depends on Phase 3 AND Phase 4 being committed (uses both reverb types from the DSP library)
- **Phases 6-10 (Polish, Docs, Analysis, Verification)**: Depend on all three user stories being complete

### User Story Dependencies

```
Phase 2 (Baseline) --> Phase 3 (US1: Dattorro) --> Phase 5 (US3: Ruinae integration)
                   --> Phase 4 (US2: FDN)      /
                        Phase 4A (scalar) --> Phase 4B (SIMD)
```

- **US1 and US2 can proceed in parallel** after Phase 2 (they modify different files: `reverb.h` vs new `fdn_reverb.h`)
- **US3 requires both US1 and US2** to be committed before beginning

### Within Each User Story

1. Tests FIRST (write and verify they FAIL)
2. Implementation (scalar before SIMD for US2)
3. Verify tests pass
4. Cross-platform IEEE 754 check
5. Commit

---

## Parallel Opportunities

### Phase 3 & 4 (US1 and US2) Can Run in Parallel

```
# After Phase 2 completes, both can start simultaneously (different files):
Developer A: Phase 3 (optimize reverb.h - Dattorro)
Developer B: Phase 4 (create fdn_reverb.h - FDN reverb)
```

### Within Phase 4: Tests T017-T025 Can Run in Parallel

All FDN reverb tests (T017-T025) are marked `[P]` -- they all write to the same new file `fdn_reverb_test.cpp` but test independent behaviors and can be written in parallel by multiple contributors (or batched in one sitting).

### Phase 6-7 Tasks T061/T062 and T065/T066 Can Run in Parallel

DSP test run and Ruinae test run are independent (`[P]`). Architecture doc updates for FDN entry and Dattorro update are independent (`[P]`).

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (T001-T002)
2. Complete Phase 2: Foundational (T003-T004)
3. Complete Phase 3: User Story 1 (T005-T016)
4. **STOP and VALIDATE**: Optimized Dattorro works, benchmark confirms 15%+ CPU reduction
5. All existing Ruinae users benefit immediately with no API or compatibility changes

### Incremental Delivery

1. Setup + Foundational (T001-T004) → Green baseline confirmed
2. User Story 1 (T005-T016) → Dattorro optimized, committed, benchmarked
3. User Story 2 Phase A (T017-T034) → Scalar FDN working, all tests pass
4. User Story 2 Phase B (T035-T040) → SIMD acceleration added, SC-002 verified
5. User Story 3 (T041-T060) → Ruinae integration complete, dual reverb usable
6. Polish + Docs + Analysis + Verification (T061-T088) → Spec complete

### Single-Developer Sequential Order

```
T001 → T002 → T003 → T004 → T005 → T006 → T007 → T008 → T009 → T010 →
T011 → T012 → T013 → T014 → T015 → T016 → T017 → T018 → T019 → T020 →
T021 → T022 → T023 → T024 → T025 → T026 → T027 → T028 → T029 → T030 →
T031 → T032 → T033 → T034 → T035 → T036 → T037 → T038 → T039 → T040 →
T041 → T042 → T043 → T044 → T045 → T046 → T046B → T047 → T048 → T049 → T050 →
T051 → T052 → T053 → T054 → T055 → T056 → T057 → T058 → T059 → T060 →
T061 → T062 → T063 → T064 → T065 → T066 → T067 → T068 → T069 → T070 →
T071 → T072 → T073 → T074 → T075 → T076 → T077 → T078 → T079 → T080 →
T081 → T082 → T083 → T084 → T085 → T086 → T087 → T088
```

---

## Notes

- `[P]` tasks = different files or independent concerns, no shared dependencies -- can run in parallel
- `[US1]`/`[US2]`/`[US3]` labels map tasks to user stories for traceability
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Constitution Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance -- add NaN/Inf test files to `-fno-fast-math` list
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Constitution Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Constitution Principle XV)
- **MANDATORY**: Fill Implementation Verification table in `spec.md` with honest, evidence-backed assessment
- **NEVER** skip the CMake full-path requirement on Windows: use `"C:/Program Files/CMake/bin/cmake.exe"`
- **NEVER** claim completion if ANY requirement is not met -- document gaps honestly instead
- The scalar-first workflow for FDN SIMD (Phase A then Phase B) is Constitution Principle IV compliance -- do not skip Phase A to jump straight to SIMD
