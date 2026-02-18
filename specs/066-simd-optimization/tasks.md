---

description: "Task list for 066-simd-optimization"
---

# Tasks: SIMD-Accelerated Math for KrateDSP Spectral Pipeline

**Input**: Design documents from `/specs/066-simd-optimization/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/spectral_simd_api.h, contracts/formant_preserver_changes.md, research.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by implementation phase (A, B, C) mapped to user stories to enable independent implementation and testing. No new files or CMake targets are created -- all changes extend existing files only.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task group follows this workflow without exception:

1. Write failing tests (tests compile and run but fail because functions do not exist yet)
2. Implement to make tests pass
3. Fix all compiler warnings (zero-warning policy)
4. Verify all tests pass (including full `dsp_tests` suite for regression check)
5. Run clang-tidy static analysis
6. Commit completed work

No pluginval step is needed -- this spec modifies only the DSP library (`KrateDSP`), not any plugin binary.

---

## Phase 1: Setup (Verify Starting Conditions)

**Purpose**: Confirm the baseline is clean before making any changes. This spec modifies 4 existing files and introduces no new files or CMake targets.

- [X] T001 Confirm existing `dsp_tests` suite passes with zero failures: run `build/windows-x64-release/bin/Release/dsp_tests.exe` and capture baseline output
- [X] T002 Confirm the 4 target files exist at their expected paths: `dsp/include/krate/dsp/core/spectral_simd.h`, `dsp/include/krate/dsp/core/spectral_simd.cpp`, `dsp/include/krate/dsp/processors/formant_preserver.h`, `dsp/tests/unit/core/spectral_simd_test.cpp`
- [X] T003 Confirm `grep -r "batchLog10\|batchPow10\|batchWrapPhase" dsp/ plugins/` returns no matches (ODR pre-check: names are unique)

**Checkpoint**: Baseline confirmed -- existing tests pass, target files exist, function names are unique

---

## Phase 2: Foundational (No Blocking Prerequisites)

**Note**: This spec has no shared foundational infrastructure to build before user stories. All 4 user stories are served by the same 4 files. The phase structure below mirrors the plan's Phase A / Phase B / Phase C dependency order:

- Phase A (US3: utility functions) must be complete before Phase B (US1: FormantPreserver integration), because `FormantPreserver` calls `batchLog10` and `batchPow10`.
- Phase B (US1) depends on Phase A.
- Phase C (US2: batch phase wrapping) is independent of Phase B -- it can be done in parallel with Phase B once Phase A tests are written.
- US4 (end-to-end performance) depends on Phase B being complete.

**Dependency summary**:
```
Phase A (US3) ---> Phase B (US1) ---> US4
Phase A (US3) ---> Phase C (US2) [can start after Phase A tests]
```

---

## Phase 3: User Story 3 / Phase A -- Vectorized Bulk Log and Exp Utility Functions (Priority: P2)

**Goal**: Add `batchLog10()`, `batchPow10()`, and `batchWrapPhase()` (both overloads) as SIMD-accelerated free functions to `spectral_simd.h/.cpp`. Add `kMinLogInput` and `kMaxPow10Output` constants. All functions follow the established Highway self-inclusion pattern (`HWY_EXPORT` / `HWY_DYNAMIC_DISPATCH`). This phase MUST be complete before Phase B (US1) because `FormantPreserver` will call these functions.

**Independent Test**: Run `dsp_tests.exe "[spectral_simd][batchLog10]" "[spectral_simd][batchPow10]"` to verify correctness of the utility functions in isolation, independent of FormantPreserver.

**Why US3 first**: Although US3 is Priority P2 in the spec, Phase A is the prerequisite for Phase B (US1, Priority P1). The utility functions must exist before FormantPreserver can call them. Tests are written first so they fail with a linker error (unresolved symbol), confirming the test-first workflow.

### 3.1 Tests for Phase A (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins. At this stage the functions do not exist, so the test file will fail to link.

- [X] T010 [P] [US3] Add `batchLog10` correctness tests to `dsp/tests/unit/core/spectral_simd_test.cpp`: known values (1.0, 10.0, 100.0, 0.001), scalar comparison loop over 2049 elements within 1e-5 absolute tolerance per bin (SC-004), and `[spectral_simd][batchLog10]` tags
- [X] T011 [P] [US3] Add `batchPow10` correctness tests to `dsp/tests/unit/core/spectral_simd_test.cpp`: known values (0.0, 1.0, -1.0, 2.0, -10.0, 6.0), scalar comparison loop over 2049 elements within 1e-5 relative error per bin (SC-005), output clamping verification (result always in `[kMinLogInput, kMaxPow10Output]`), and `[spectral_simd][batchPow10]` tags
- [X] T012 [P] [US3] Add `batchWrapPhase` correctness tests to `dsp/tests/unit/core/spectral_simd_test.cpp`: known values (0, pi, -pi, 2*pi, -2*pi, 100*pi, -100*pi), out-of-place and in-place overload verification, scalar `wrapPhase()` comparison over 2049 elements within 1e-6 tolerance (SC-006), and `[spectral_simd][batchWrapPhase]` tags
- [X] T013 [US3] Add edge case tests to `dsp/tests/unit/core/spectral_simd_test.cpp`: zero-length input (count == 0) for all three functions does not crash; non-SIMD-width tail counts (1, 3, 5, 7, 1025) produce correct results; `batchLog10` with zero/negative inputs returns finite results (no NaN, no -inf); `batchPow10` with overflow inputs (x > 38.5) returns `kMaxPow10Output` not infinity; tags `[spectral_simd][edge]`
- [X] T014 [US3] Add log10/pow10 round-trip test to `dsp/tests/unit/core/spectral_simd_test.cpp`: apply `batchLog10` then `batchPow10` on 2049 positive values, verify output matches input within relative error of 1e-4; tag `[spectral_simd][roundtrip]`
- [X] T015 [US3] Add performance benchmark test cases to `dsp/tests/unit/core/spectral_simd_test.cpp` following the `modulation_engine_perf_test.cpp` pattern: SC-001 (`batchLog10` vs scalar `std::log10` loop, 2049 elements, require >= 2x speedup), SC-002 (`batchPow10` vs scalar `std::pow(10.0f, x)` loop, 2049 elements, require >= 2x speedup), SC-003 (`batchWrapPhase` vs scalar `wrapPhase()` loop, 2049 elements, require >= 2x speedup); tags `[spectral_simd][performance]`
- [X] T016 [US3] Build `dsp_tests` target to confirm test file compiles but FAILS to link (unresolved symbols for `batchLog10`, `batchPow10`, `batchWrapPhase`): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 3.2 Implementation for Phase A

- [X] T017 [US3] Add constants `kMinLogInput = 1e-10f` and `kMaxPow10Output = 1e6f` as `inline constexpr float` in the `Krate::DSP` namespace in `dsp/include/krate/dsp/core/spectral_simd.h` (after the namespace opening, before the existing function declarations)
- [X] T018 [US3] Add four public function declarations to `dsp/include/krate/dsp/core/spectral_simd.h` in the `Krate::DSP` namespace: `void batchLog10(const float* input, float* output, std::size_t count) noexcept;`, `void batchPow10(const float* input, float* output, std::size_t count) noexcept;`, `void batchWrapPhase(const float* input, float* output, std::size_t count) noexcept;`, `void batchWrapPhase(float* data, std::size_t count) noexcept;` -- no Highway types in public API (FR-016)
- [X] T019 [US3] Add `BatchLog10Impl` SIMD kernel to `dsp/include/krate/dsp/core/spectral_simd.cpp` inside the `HWY_BEFORE_NAMESPACE()` / `HWY_AFTER_NAMESPACE()` block (after `ComputePowerSpectrumPffftImpl`): uses `hn::LoadU`, `hn::Max(v, minVal)` for branchless clamping, `hn::Log10(d, v)`, `hn::StoreU`; scalar tail uses `std::max(input[k], 1e-10f)` and `std::log10`
- [X] T020 [US3] Add `BatchPow10Impl` SIMD kernel to `dsp/include/krate/dsp/core/spectral_simd.cpp` inside the same block: uses `hn::LoadU`, `hn::Exp(d, hn::Mul(v, ln10))` where `ln10 = hn::Set(d, 2.302585093f)`, `hn::Max`/`hn::Min` for output clamping to `[kMinLogInput, kMaxPow10Output]`, `hn::StoreU`; scalar tail uses `std::pow(10.0f, input[k])` with `std::max`/`std::min` clamping
- [X] T021 [US3] Add `BatchWrapPhaseImpl` (out-of-place) SIMD kernel to `dsp/include/krate/dsp/core/spectral_simd.cpp` inside the same block: uses `hn::LoadU`, `hn::Round(hn::Mul(v, invTwoPi))`, `hn::NegMulAdd(n, twoPi, v)` (or `hn::Sub(v, hn::Mul(n, twoPi))` as fallback), `hn::StoreU`; scalar tail uses `std::round(input[k] * inv2pi)` branchless formula
- [X] T022 [US3] Add `BatchWrapPhaseInPlaceImpl` SIMD kernel to `dsp/include/krate/dsp/core/spectral_simd.cpp` inside the same block: identical logic to `BatchWrapPhaseImpl` but reads from and writes to the same `data` pointer using `hn::LoadU`/`hn::StoreU`
- [X] T023 [US3] Add `HWY_EXPORT` lines to `dsp/include/krate/dsp/core/spectral_simd.cpp` inside the `#if HWY_ONCE` block (after the existing three `HWY_EXPORT` lines): `HWY_EXPORT(BatchLog10Impl);`, `HWY_EXPORT(BatchPow10Impl);`, `HWY_EXPORT(BatchWrapPhaseImpl);`, `HWY_EXPORT(BatchWrapPhaseInPlaceImpl);`
- [X] T024 [US3] Add four `HWY_DYNAMIC_DISPATCH` wrapper functions to `dsp/include/krate/dsp/core/spectral_simd.cpp` inside the `#if HWY_ONCE` block's `Krate::DSP` namespace (after existing dispatch wrappers): `batchLog10`, `batchPow10`, out-of-place `batchWrapPhase`, in-place `batchWrapPhase` -- each delegates to the corresponding `HWY_DYNAMIC_DISPATCH(XxxImpl)` call

### 3.3 Verification for Phase A

- [X] T025 [US3] Build `dsp_tests` with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests 2>&1` -- fix any C4244, C4267, C4100 warnings before proceeding
- [X] T026 [US3] Run correctness tests and verify all pass: `build/windows-x64-release/bin/Release/dsp_tests.exe "[spectral_simd]" ~"[performance]"` -- all `[batchLog10]`, `[batchPow10]`, `[batchWrapPhase]`, `[edge]`, `[roundtrip]` cases must pass
- [X] T027 [US3] Run full `dsp_tests` suite to confirm zero regressions (SC-009): `build/windows-x64-release/bin/Release/dsp_tests.exe` -- existing `[polar]`, `[cartesian]`, `[tail]` tests must still pass
- [X] T028 [US3] Run performance benchmarks in Release build and verify SC-001, SC-002, SC-003 (>= 2x speedup each): `build/windows-x64-release/bin/Release/dsp_tests.exe "[spectral_simd][performance]"` -- record actual speedup ratio for compliance table
- [X] T029 [US3] Verify IEEE 754 compliance: confirm `spectral_simd_test.cpp` does NOT use `std::isnan`/`std::isfinite`/`std::isinf` (it should not per research R9) -- if it does, add file to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 3.4 Clang-Tidy for Phase A

- [X] T030 [US3] Run clang-tidy on modified files: `powershell -ExecutionPolicy Bypass -File tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` (requires Ninja build to be current; if not, regenerate with `"C:/Program Files/CMake/bin/cmake.exe" --preset windows-ninja` first) -- fix all errors; review and address warnings

### 3.5 Commit Phase A

- [X] T031 [US3] Commit completed Phase A work with message: "feat(dsp): add SIMD batch log10/pow10/wrapPhase utilities to spectral_simd"

**Checkpoint**: Phase A complete -- `batchLog10`, `batchPow10`, `batchWrapPhase` implemented, tested, and committed. US3 acceptance criteria verified (SC-001 through SC-006 benchmarks run and recorded). Phase B (US1) and Phase C (US2) can now proceed.

---

## Phase 4: User Story 1 / Phase B -- Vectorized Formant Preservation Log/Exp (Priority: P1)

**Goal**: Replace the scalar `std::log10` loop in `FormantPreserver::extractEnvelope()` and the scalar `std::pow` loop in `FormantPreserver::reconstructEnvelope()` with calls to `batchLog10()` and `batchPow10()` respectively. The public API of `FormantPreserver` is unchanged (FR-010). The staging copy of `complexBuf_[k].real` into `logMag_` (a cheap scalar copy, not a transcendental) enables contiguous input for `batchPow10`.

**Prerequisite**: Phase A (T031 committed) must be complete.

**Independent Test**: Run `dsp_tests.exe "[spectral_simd][formant]"` to verify `FormantPreserver` SIMD output matches the scalar reference within 1e-5 per bin (SC-007, SC-008), independent of any harmonizer engine.

### 4.1 Tests for Phase B (Write FIRST -- Must FAIL)

> Tests for FormantPreserver equivalence are added to `spectral_simd_test.cpp` (per spec clarification: no separate test file needed). They will FAIL initially because the test is written against the new SIMD behavior -- but if the scalar behavior was already passing, we write the test to compare SIMD vs inline scalar reference, which will pass as soon as Phase B integration is done.
>
> **Strategy**: Write the equivalence test now to establish the scalar reference inline. Before Phase B integration, the test verifies the CURRENT scalar behavior matches the inline scalar reference (it should pass as a sanity check). After Phase B integration, the test verifies the SIMD behavior matches the same scalar reference within tolerance. This is the correct test-first approach for a drop-in optimization.

- [X] T035 [US1] Add `FormantPreserver::extractEnvelope` equivalence test to `dsp/tests/unit/core/spectral_simd_test.cpp` (SC-007): create a `FormantPreserver` instance prepared with fftSize=4096, generate a known 2049-element magnitude array (harmonic series with exponential decay, all positive), compute inline scalar reference using `std::max(mag, 1e-10f)` + `std::log10` loop, call `extractEnvelope()` (two-argument overload), compare output `envelope` array against scalar reference within 1e-5 per bin using `Approx().margin(1e-5f)`; tags `[spectral_simd][formant]`. Note: the single-argument overload `extractEnvelope(const float* magnitudes)` delegates directly to the two-argument overload (verified at `formant_preserver.h:145-147`), so it is covered indirectly. No separate test case is required for the single-argument form.
- [X] T036 [US1] Add `FormantPreserver::reconstructEnvelope` equivalence test to `dsp/tests/unit/core/spectral_simd_test.cpp` (SC-008): extend the same test fixture as T035, after `extractEnvelope()` completes, compute inline scalar reference using `std::pow(10.0f, logEnv)` + clamp loop on the same input log-magnitudes, compare `FormantPreserver`'s internal `envelope_` (accessed via `getEnvelope()`) against scalar reference within 1e-5 per bin; tags `[spectral_simd][formant]`. Note: `reconstructEnvelope()` is a private method called internally by `extractEnvelope()`; its output is verified indirectly through the `getEnvelope()` accessor.
- [X] T037 [US1] Build and run these tests against the CURRENT (pre-integration) `FormantPreserver` to confirm they pass as a scalar baseline: `build/windows-x64-release/bin/Release/dsp_tests.exe "[spectral_simd][formant]"` -- both tests MUST pass now (scalar vs scalar inline reference); document result

### 4.2 Implementation for Phase B

- [X] T038 [US1] Add `#include <krate/dsp/core/spectral_simd.h>` to `dsp/include/krate/dsp/processors/formant_preserver.h` (after existing includes, before the class definition)
- [X] T039 [US1] Replace the scalar `std::log10` loop in `FormantPreserver::extractEnvelope()` in `dsp/include/krate/dsp/processors/formant_preserver.h`: remove the `for (std::size_t k = 0; k < numBins_; ++k) { float mag = std::max(magnitudes[k], kMinMagnitude); logMag_[k] = std::log10(mag); }` loop and replace with the single call `batchLog10(magnitudes, logMag_.data(), numBins_);` (FR-007) -- the per-element pre-clamp is removed; clamping is delegated to `batchLog10`'s internal `kMinLogInput` clamp
- [X] T040 [US1] Replace the scalar `std::pow` loop in `FormantPreserver::reconstructEnvelope()` in `dsp/include/krate/dsp/processors/formant_preserver.h`: remove the `for (std::size_t k = 0; k < numBins_; ++k) { float logEnv = complexBuf_[k].real; envelope_[k] = std::pow(10.0f, logEnv); envelope_[k] = std::max(kMinMagnitude, std::min(envelope_[k], 1e6f)); }` loop and replace with the staging copy loop plus `batchPow10` call (FR-008): `for (std::size_t k = 0; k < numBins_; ++k) { logMag_[k] = complexBuf_[k].real; }` then `batchPow10(logMag_.data(), envelope_.data(), numBins_);` -- the post-clamp is removed; clamping is delegated to `batchPow10`'s internal output clamp

### 4.3 Verification for Phase B

- [X] T041 [US1] Build `dsp_tests` with zero compiler warnings after `formant_preserver.h` changes: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests 2>&1` -- fix any warnings before proceeding
- [X] T042 [US1] Run `FormantPreserver` equivalence tests to confirm SIMD output matches scalar reference within 1e-5 per bin (SC-007, SC-008): `build/windows-x64-release/bin/Release/dsp_tests.exe "[spectral_simd][formant]"` -- both tests MUST pass
- [X] T043 [US1] Run full `dsp_tests` suite to confirm zero regressions (SC-009): `build/windows-x64-release/bin/Release/dsp_tests.exe` -- all existing tests including `[polar]`, `[cartesian]`, `[tail]`, `[batchLog10]`, `[batchPow10]`, `[batchWrapPhase]` must still pass
- [X] T044 [US1] Verify `FormantPreserver` public API is unchanged (FR-010): confirm `extractEnvelope`, `getEnvelope`, `applyFormantPreservation`, `numBins` signatures are identical to pre-integration state by reading `formant_preserver.h`

### 4.4 Clang-Tidy for Phase B

- [X] T045 [US1] Run clang-tidy on `formant_preserver.h`: `powershell -ExecutionPolicy Bypass -File tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` -- fix all errors; review and address warnings

### 4.5 Commit Phase B

- [X] T046 [US1] Commit completed Phase B work with message: "feat(dsp): integrate SIMD batchLog10/batchPow10 into FormantPreserver"

**Checkpoint**: Phase B complete -- `FormantPreserver` uses SIMD log10/pow10 internally, output is verified equivalent to scalar reference within 1e-5, zero regressions. US1 acceptance criteria verified (SC-007, SC-008 pass).

---

## Phase 5: User Story 2 / Phase C -- Batch Phase Wrapping (Priority: P2)

**Goal**: Confirm that `batchWrapPhase()` (both overloads) is fully implemented, tested, and available in the public API for any Layer 1+ component. Phase C is strictly additive -- no existing call sites (`PhaseVocoderPitchShifter` or any other processor) are modified in this spec. Adoption at call sites is deferred to future specs (FR-011, FR-012).

**Prerequisite**: Phase A (T031 committed) must be complete. Phase C is independent of Phase B and can proceed in parallel.

**Independent Test**: Run `dsp_tests.exe "[spectral_simd][batchWrapPhase]"` to verify correctness of `batchWrapPhase` in isolation.

> **Note**: The tests and implementation for `batchWrapPhase` were already written and committed in Phase A (T010-T031). Phase C is a confirmation phase that verifies the function is accessible from the public header and that scalar `wrapPhase()`/`wrapPhaseFast()` in `spectral_utils.h` remain unchanged (FR-012).

### 5.1 Verification for Phase C

- [X] T050 [US2] Confirm `batchWrapPhase` is declared in `dsp/include/krate/dsp/core/spectral_simd.h` with correct signatures: `void batchWrapPhase(const float* input, float* output, std::size_t count) noexcept;` and `void batchWrapPhase(float* data, std::size_t count) noexcept;` -- read the file and verify both overloads are present
- [X] T051 [US2] Confirm `wrapPhase()` and `wrapPhaseFast()` in `dsp/include/krate/dsp/primitives/spectral_utils.h` are unchanged (FR-012): read the file and verify signatures and implementations are identical to pre-spec state
- [X] T052 [US2] Run `batchWrapPhase` correctness and edge case tests to confirm all pass (these were written in Phase A): `build/windows-x64-release/bin/Release/dsp_tests.exe "[spectral_simd][batchWrapPhase]"` -- all tests must pass (SC-006 verified)
- [X] T053 [US2] Confirm no existing call sites were modified: `grep -r "batchWrapPhase" dsp/include/krate/dsp/primitives/ dsp/include/krate/dsp/processors/ dsp/include/krate/dsp/systems/ dsp/include/krate/dsp/effects/` -- expected result: no matches (function is additive-only, no callers yet)

### 5.2 Commit Phase C

- [X] T054 [US2] Commit Phase C confirmation note (if any verification-only changes were made); if no file changes are needed, this task is a no-op -- Phase C work was fully delivered in Phase A's commit [NO-OP CONFIRMED]

**Checkpoint**: Phase C confirmed -- `batchWrapPhase` is available in the public API, scalar functions are unchanged, no existing call sites modified. US2 acceptance criteria verified.

---

## Phase 6: User Story 4 -- End-to-End Harmonizer Performance (Priority: P3)

**Goal**: Verify that the SIMD-optimized `FormantPreserver` produces output that is bit-identical or within floating-point tolerance compared to pre-optimization output in a full `HarmonizerEngine` context with 4 voices in PhaseVocoder mode with formant preservation enabled.

**Prerequisite**: Phase B (T046 committed) must be complete.

**Independent Test**: Run the `HarmonizerEngine` benchmark test (if one exists from spec 065) or the FormantPreserver equivalence tests at the harmonizer level to confirm end-to-end correctness.

### 6.1 Verification for US4

- [X] T060 [US4] Search for existing HarmonizerEngine performance/integration tests in `dsp/tests/`: `grep -r "HarmonizerEngine\|harmonizer" dsp/tests/ --include="*.cpp" -l` -- identify which test files exercise the full harmonizer pipeline with formant preservation enabled. If no such files are found, document this explicitly and proceed directly to T063 (SC-009 final regression check), noting that US4 is verified through SC-007/SC-008 per-bin equivalence tests per spec clarification (2026-02-18 session); skip T061 in that case.
- [X] T061 [US4] Run the identified harmonizer tests with the SIMD-integrated `FormantPreserver` to confirm output is within tolerance: use the existing test target and verify zero new failures -- if no dedicated harmonizer integration test exists, verify via the FormantPreserver equivalence tests (SC-007, SC-008) which exercise the full cepstral pipeline (log10 -> IFFT -> lifter -> FFT -> pow10)
- [X] T062 [US4] Confirm end-to-end output tolerance: the RMS difference between SIMD and scalar paths must be less than 1e-5 over a representative signal -- this is already covered by SC-007/SC-008 per-bin 1e-5 tolerance; document the connection explicitly in the compliance table
- [X] T063 [US4] Run the full `dsp_tests` suite one final time to confirm the accumulated changes produce zero regressions: `build/windows-x64-release/bin/Release/dsp_tests.exe` -- this is the SC-009 final check

**Checkpoint**: US4 confirmed -- end-to-end harmonizer correctness verified, zero regressions

---

## Phase 7: Polish and Cross-Cutting Concerns

**Purpose**: Final quality checks across all phases before completion verification.

- [X] T070 [P] Verify `spectral_simd.h` public API surface contains no Highway-internal types (`hn::Vec`, `hn::ScalableTag`, etc.) -- read the file and confirm only `float*`, `const float*`, and `std::size_t` appear in function signatures (FR-016)
- [X] T071 [P] Verify all four new functions are `noexcept` in `spectral_simd.h` (FR-006) -- read declarations and confirm `noexcept` is present on all four
- [X] T072 [P] Verify `kMinLogInput` is defined only once (in `spectral_simd.h`) and that `FormantPreserver` does not define a separate equivalent constant for the log/pow paths -- `grep -n "kMinMagnitude\|kMinLogInput" dsp/include/krate/dsp/processors/formant_preserver.h` and confirm `kMinMagnitude` is only used in `applyFormantPreservation()`, not in `extractEnvelope()` or `reconstructEnvelope()`
- [X] T073 [P] Verify no heap allocations in SIMD hot loops (SC-010) -- read `BatchLog10Impl`, `BatchPow10Impl`, `BatchWrapPhaseImpl`, `BatchWrapPhaseInPlaceImpl` in `spectral_simd.cpp` and confirm no `new`, `malloc`, `std::vector`, or container construction in the per-target kernel bodies
- [X] T074 Run full performance benchmark suite and record all six metrics for the compliance table: `build/windows-x64-release/bin/Release/dsp_tests.exe "[spectral_simd][performance]"` -- capture actual speedup ratios for SC-001, SC-002, SC-003 and actual per-element error for SC-004, SC-005, SC-006

---

## Phase 8: Architecture Documentation Update (MANDATORY)

**Purpose**: Update living architecture documentation per Constitution Principle XIV (FR-018).

- [X] T080 Update `specs/_architecture_/layer-0-core.md` with the three new batch utility functions (FR-018): add entries for `batchLog10`, `batchPow10`, `batchWrapPhase` with purpose, public API summary, file location (`dsp/include/krate/dsp/core/spectral_simd.h`), and "when to use this" guidance; include the new constants `kMinLogInput` and `kMaxPow10Output`. The file exists at `specs/_architecture_/layer-0-core.md`.
- [X] T081 Commit architecture documentation update with message: "docs: update layer-0 architecture docs with SIMD batch math utilities (spec 066)"

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 9: Static Analysis -- Final Pass (MANDATORY)

**Purpose**: Final clang-tidy pass across all modified files as the pre-completion quality gate.

- [X] T085 Regenerate Ninja compile database if any of the 4 modified files changed since last generation: `"C:/Program Files/CMake/bin/cmake.exe" --preset windows-ninja` (run from VS Developer PowerShell if needed for MSVC environment)
- [X] T086 Run clang-tidy on all DSP modified files: `powershell -ExecutionPolicy Bypass -File tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` -- fix all errors and review all warnings
- [X] T087 Document any intentional clang-tidy suppressions with `// NOLINT(rule-name): reason` inline comments if any warnings are deliberately accepted (e.g., performance-related Highway idioms that clang-tidy does not recognize)

**Checkpoint**: Static analysis clean -- ready for completion verification

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify ALL requirements from spec.md before claiming completion. Do NOT fill the table from memory.

> **Constitution Principle XVI**: Every row in the compliance table MUST be verified against actual code and test output. Generic claims like "implemented" without specific evidence are NOT acceptable.

### 10.1 Functional Requirements Verification

For EACH requirement below, open the implementation file, find the code, and record the file path and line number:

- [X] T090 Verify FR-001 (`batchLog10` with `kMinLogInput` clamp): read `spectral_simd.h` and `spectral_simd.cpp`, record line numbers for declaration, kernel, and dispatch wrapper
- [X] T091 Verify FR-002 (`batchPow10` with `hn::Exp(d, hn::Mul(v, kLn10))` and output clamp): read `spectral_simd.cpp`, record the `ln10` constant value and clamp implementation line numbers
- [X] T092 Verify FR-003 (`batchWrapPhase` branchless formula with both overloads): read `spectral_simd.cpp`, record the `hn::Round` + `hn::NegMulAdd` implementation line numbers for both kernels
- [X] T093 Verify FR-004 (Highway self-inclusion pattern, `HWY_EXPORT`, `HWY_DYNAMIC_DISPATCH`): read `spectral_simd.cpp` `#if HWY_ONCE` block, record line numbers for all four `HWY_EXPORT` and four dispatch wrapper lines
- [X] T094 Verify FR-005 (count==0 returns immediately, scalar tail for non-SIMD-width counts): read kernel implementations in `spectral_simd.cpp` and confirm `for (; k + N <= count; ...)` + `for (; k < count; ...)` pattern; zero-length: both loops have zero iterations
- [X] T095 Verify FR-006 (all three functions are `noexcept`, zero allocations, branchless hot loop): read `spectral_simd.h` for `noexcept`, read kernel bodies for absence of heap allocation and use of `hn::Max`/`hn::Min`/`hn::Round`
- [X] T096 Verify FR-007 (`FormantPreserver::extractEnvelope()` uses `batchLog10`, pre-clamp removed): read `formant_preserver.h` extractEnvelope implementation, record line numbers, confirm no `std::max(magnitudes[k], kMinMagnitude)` in the log step
- [X] T097 Verify FR-008 (`FormantPreserver::reconstructEnvelope()` uses staging copy + `batchPow10`, post-clamp removed): read `formant_preserver.h` reconstructEnvelope implementation, record line numbers, confirm staging copy loop and `batchPow10` call are present, confirm no `std::min`/`std::max` clamp after the call
- [X] T098 Verify FR-009 (SIMD FormantPreserver output matches scalar within 1e-5): record actual test names and pass status from T042 run
- [X] T099 Verify FR-010 (FormantPreserver public API unchanged): record that method signatures are unchanged vs pre-spec header
- [X] T100 Verify FR-011 (`batchWrapPhase` declared in public header, no call sites modified): record declaration line in `spectral_simd.h` and grep result confirming no callers in existing processors
- [X] T101 Verify FR-012 (scalar `wrapPhase`/`wrapPhaseFast` unchanged): read `spectral_utils.h`, record that signatures are unchanged
- [X] T102 Verify FR-013 (`batchWrapPhase` matches scalar `wrapPhase` within 1e-6): record actual test name and pass status from T052 run
- [X] T103 Verify FR-014 through FR-017 (namespace, no new dependencies, no Highway types in public API, zero regressions): confirm namespace in `spectral_simd.h`, confirm no new CMakeLists.txt changes, confirm public API uses only `float*`/`std::size_t`, record `dsp_tests` final pass count

### 10.2 Success Criteria Verification

For EACH SC below, run or read the actual test output and record measured values:

- [X] T104 Verify SC-001 through SC-003 (performance benchmarks >= 2x speedup): run `dsp_tests.exe "[spectral_simd][performance]"` and record the actual speedup ratios printed to stdout for all three functions
- [X] T105 Verify SC-004 (batchLog10 error < 1e-5): record actual max error from `[batchLog10]` test run (T026 output)
- [X] T106 Verify SC-005 (batchPow10 relative error < 1e-5): record actual max relative error from `[batchPow10]` test run (T026 output)
- [X] T107 Verify SC-006 (batchWrapPhase error < 1e-6): record actual max error from `[batchWrapPhase]` test run (T052 output)
- [X] T108 Verify SC-007 and SC-008 (FormantPreserver per-bin error < 1e-5): record actual max per-bin error from `[formant]` test run (T042 output)
- [X] T109 Verify SC-009 (zero regressions): record final `dsp_tests.exe` pass/fail count from T063 run
- [X] T110 Verify SC-010 (zero heap allocations): confirm by code inspection that kernel bodies contain no allocations (T073 result)

### 10.3 Fill Compliance Table in spec.md

- [X] T111 Update the "Implementation Verification" compliance table in `specs/066-simd-optimization/spec.md` with specific evidence for EACH of FR-001 through FR-018 and SC-001 through SC-010 -- include file paths, line numbers, test names, and actual measured values; no row may be left blank or contain only "implemented"
- [X] T112 Set "Overall Status" in spec.md to COMPLETE, NOT COMPLETE, or PARTIAL based on honest evidence assessment; document any gaps explicitly

### 10.4 Honest Self-Check

Answer each question. If ANY answer is "yes", completion cannot be claimed:

- [X] T113 Did any test threshold get relaxed from the spec's original requirement? (SC-004: 1e-5, SC-005: 1e-5, SC-006: 1e-6, SC-007: 1e-5, SC-008: 1e-5 -- these are fixed)
- [X] T114 Are there any `// placeholder`, `// stub`, or `// TODO` comments in new code in `spectral_simd.h`, `spectral_simd.cpp`, or `formant_preserver.h`?
- [X] T115 Were any features quietly removed from scope? (Required deliverables: `batchLog10`, `batchPow10`, `batchWrapPhase` both overloads, FormantPreserver integration, performance benchmarks)
- [X] T116 All self-check questions answered "no" -- completion claim is honest

---

## Phase 11: Final Completion

- [X] T120 Commit compliance table and spec.md updates with message: "chore: fill spec 066 compliance table with implementation evidence"
- [X] T121 Verify feature branch `066-simd-optimization` contains all commits: T031 (Phase A), T046 (Phase B), optional Phase C, T081 (docs), T120 (compliance)
- [X] T122 Claim completion ONLY if all requirements in the compliance table are marked MET -- if any are NOT MET, document gaps in spec.md and present to user for approval before claiming done

**Checkpoint**: Spec 066 implementation honestly complete

---

## Dependencies and Execution Order

### Phase Dependencies

```
Phase 1 (Setup)
  |
  v
Phase 3 (Phase A / US3: batchLog10 + batchPow10 + batchWrapPhase tests + implementation)
  |          |
  |          v
  |    Phase 5 (Phase C / US2: batchWrapPhase confirmation -- can start after T031)
  |
  v
Phase 4 (Phase B / US1: FormantPreserver integration -- requires Phase A complete)
  |
  v
Phase 6 (US4: End-to-end harmonizer verification -- requires Phase B complete)
  |
  v
Phase 7 (Polish)
  |
  v
Phase 8 (Architecture docs)
  |
  v
Phase 9 (Clang-tidy final pass)
  |
  v
Phase 10 (Completion verification)
  |
  v
Phase 11 (Final commit + completion claim)
```

### User Story Dependencies

- **US3 (Phase A, P2)**: Starts first despite being P2 because it is the prerequisite for US1 (P1). Contains the utility function implementation that US1 calls.
- **US1 (Phase B, P1)**: Depends on US3 (Phase A) complete. Highest business priority; fastest path to value after Phase A.
- **US2 (Phase C, P2)**: Depends on Phase A being committed (batchWrapPhase already implemented). Can proceed in parallel with Phase B (US1) since it only modifies different aspects of the same test file -- or can follow sequentially. No call sites are changed.
- **US4 (P3)**: Depends on Phase B (US1) complete. Primarily a verification phase; most work is already done by SC-007/SC-008.

### Parallel Opportunities

Within Phase A (US3), T010, T011, and T012 are marked [P] because they all add separate TEST_CASE blocks to `spectral_simd_test.cpp` -- a single developer can write them sequentially, but conceptually they are independent additions to the same file.

Within Phase A implementation, T019 through T022 (kernel implementations) can conceptually be worked in parallel but must all be added to the same file section; sequential implementation is recommended to avoid merge conflicts.

Phase 5 (Phase C / US2) can start immediately after T031 (Phase A commit) without waiting for Phase 4 (Phase B / US1).

---

## Parallel Example: Phase A (US3)

```bash
# Write all three sets of correctness tests independently (same file, different TEST_CASE blocks):
Task T010: "batchLog10 correctness + scalar comparison tests in spectral_simd_test.cpp"
Task T011: "batchPow10 correctness + scalar comparison tests in spectral_simd_test.cpp"
Task T012: "batchWrapPhase correctness + scalar comparison tests in spectral_simd_test.cpp"

# After T016 confirms tests fail to link:
# Implement all four SIMD kernels in spectral_simd.cpp (same file section, sequential):
Task T019: "BatchLog10Impl kernel in spectral_simd.cpp"
Task T020: "BatchPow10Impl kernel in spectral_simd.cpp"
Task T021: "BatchWrapPhaseImpl (out-of-place) kernel in spectral_simd.cpp"
Task T022: "BatchWrapPhaseInPlaceImpl kernel in spectral_simd.cpp"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

The MVP path delivers the highest-priority SIMD optimization (FormantPreserver log/exp) as quickly as possible:

1. Complete Phase 1 (Setup -- verify baseline)
2. Complete Phase 3 (Phase A -- implement utility functions, required prerequisite)
3. Complete Phase 4 (Phase B -- FormantPreserver integration, the P1 user story)
4. STOP and VALIDATE: Run SC-007 + SC-008 equivalence tests, run SC-001 + SC-002 benchmarks

### Incremental Delivery

1. Phase 1 + Phase 3 (Phase A) complete -> `batchLog10`, `batchPow10`, `batchWrapPhase` available as reusable utilities (US3 done)
2. Phase 4 (Phase B) complete -> FormantPreserver uses SIMD internally, measurably faster (US1 done, MVP)
3. Phase 5 (Phase C) complete -> `batchWrapPhase` confirmed available for future callers (US2 done)
4. Phase 6 complete -> End-to-end harmonizer correctness verified (US4 done)
5. Phases 7-11 -> Polish, docs, compliance table, final commit

---

## Notes

- **[P]** marks tasks that can be run in parallel (different logical sections, no file dependencies on each other's results)
- **[US1], [US2], [US3], [US4]** labels map tasks to specific user stories for traceability
- No new files are created by this spec -- all changes extend `spectral_simd.h`, `spectral_simd.cpp`, `formant_preserver.h`, `spectral_simd_test.cpp`
- No CMake changes are needed -- `spectral_simd.cpp` and `spectral_simd_test.cpp` are already registered
- No pluginval step is needed -- this spec modifies only the DSP library, not any VST3 plugin binary
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify IEEE 754 compliance after writing tests (check for `std::isnan` usage)
- **MANDATORY**: Commit at end of each phase (T031 for Phase A, T046 for Phase B)
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest per-requirement evidence (Principle XVI)
- **NEVER claim completion if ANY requirement is not met** -- document gaps honestly instead
