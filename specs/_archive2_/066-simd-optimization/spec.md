# Feature Specification: SIMD-Accelerated Math for KrateDSP Spectral Pipeline

**Feature Branch**: `066-simd-optimization`
**Plugin**: KrateDSP (Shared DSP Library)
**Created**: 2026-02-18
**Status**: Draft
**Input**: User description: "SIMD-accelerated math for the KrateDSP spectral pipeline -- vectorized atan2, sincos, log, exp for cartesian-to-polar, polar-to-cartesian, phase wrapping, and formant preservation using Google Highway"
**Roadmap Reference**: [harmonizer-roadmap.md, Phase 5: SIMD Optimization](../harmonizer-roadmap.md#phase-5-simd-optimization) (lines 897-1028)

## Background & Motivation

The phase vocoder pipeline in KrateDSP performs approximately 10 distinct spectral processing stages per frame. The FFT/IFFT stages are already SIMD-accelerated via pffft (SSE/NEON radix-4 butterflies), representing approximately 30-40% of total CPU cost. The remaining non-FFT cost is dominated by three categories of scalar transcendental math:

| Operation | Current Implementation | % of Non-FFT CPU | SIMD Speedup Potential |
|-----------|----------------------|-------------------|----------------------|
| Cartesian-to-Polar (`atan2`, `sqrt`) | Already SIMD via Highway | 15-25% | Already optimized |
| Polar-to-Cartesian (`sin`, `cos`) | Already SIMD via Highway | 10-15% | Already optimized |
| `log`/`exp` (formant cepstral pipeline) | Scalar `std::log10`, `std::pow` | 5-10% | 4-8x |
| Phase wrapping | Scalar while-loop | 2-5% | 4x (branchless) |

The project already uses Google Highway (v1.2.0, Apache-2.0) for SIMD-accelerated spectral math in `spectral_simd.cpp` (Layer 0). The existing implementation provides `computePolarBulk()` and `reconstructCartesianBulk()` using Highway's `hn::Atan2`, `hn::Sin`, `hn::Cos`, `hn::Sqrt`, and `hn::MulAdd` with runtime ISA dispatch (SSE2/AVX2/AVX-512/NEON). These functions are already called by `SpectralBuffer::ensurePolarValid()` and `ensureCartesianValid()`.

However, two significant optimization opportunities remain:

1. **Formant preservation**: The `FormantPreserver::extractEnvelope()` method uses scalar `std::log10()` in a per-bin loop (converting magnitudes to log-magnitude for cepstral analysis), and `FormantPreserver::reconstructEnvelope()` uses scalar `std::pow(10.0f, logEnv)` in a per-bin loop (converting the smoothed log-envelope back to linear magnitude). For an FFT size of 4096 (2049 bins), this is 2049 scalar `log10` calls and 2049 scalar `pow` calls per frame -- totaling approximately 4098 transcendental function evaluations per frame, repeated for each voice that has formant preservation enabled.

2. **Phase wrapping**: The `wrapPhase()` function uses a while-loop (`while (phase > pi) phase -= 2*pi; while (phase < -pi) phase += 2*pi;`), which is branch-heavy and scalar. While a `wrapPhaseFast()` exists using `std::fmod`, neither function is vectorizable. A branchless SIMD batch version would eliminate branch misprediction overhead when wrapping arrays of phase values during spectral processing.

### Why Google Highway (Not Pommier sse_mathfun)

The harmonizer roadmap originally referenced Pommier's sse_mathfun (zlib license) as the SIMD math source. However, the project already depends on Google Highway (v1.2.0), which provides the same transcendental functions with superior properties:

| Property | Pommier sse_mathfun | Google Highway contrib/math |
|----------|--------------------|-----------------------------|
| License | zlib | Apache-2.0 (already in project) |
| Cross-platform | SSE-only; requires separate NEON port | Portable: SSE2, AVX2, AVX-512, NEON, WASM, RVV, SVE |
| Runtime dispatch | Manual `#ifdef` | Automatic via `HWY_DYNAMIC_DISPATCH` |
| `atan2` | Requires separate extension (to-miz/sse_mathfun_extension) | Built-in `hn::Atan2` |
| `sincos` | `sincos_ps` (SSE-only) | `hn::Sin` + `hn::Cos` (portable) |
| `log` | `log_ps` (SSE-only) | `hn::Log` (ULP <= 4), `hn::Log10` (ULP <= 2) |
| `exp` | `exp_ps` (SSE-only) | `hn::Exp` (ULP <= 1) |
| Already integrated | No | Yes -- `spectral_simd.cpp` already uses Highway |
| Build system | Separate C source files | FetchContent, already configured |

Since Highway already provides every function Pommier would, with better portability and no additional dependency, this spec uses Highway exclusively. This eliminates the cross-platform risk identified in the roadmap (SSE intrinsics not compiling on ARM) because Highway handles ISA dispatch internally.

### Highway Math Accuracy Specifications

The following accuracy guarantees are documented in Highway's `hwy/contrib/math/math-inl.h` (verified from source):

| Function | Max Error (ULP) | Valid Input Range (float32) | Equivalent Scalar |
|----------|----------------|----------------------------|-------------------|
| `hn::Sin` | 3 | [-39000, +39000] | `std::sin` |
| `hn::Cos` | 3 | [-39000, +39000] | `std::cos` |
| `hn::Atan2` | (implementation-defined) | Full float range | `std::atan2` |
| `hn::Log` | 4 | (0, +FLT_MAX] | `std::log` |
| `hn::Log10` | 2 | (0, +FLT_MAX] | `std::log10` |
| `hn::Exp` | 1 | [-FLT_MAX, +104] | `std::exp` |
| `hn::Sqrt` | (exact on most ISAs) | [0, +FLT_MAX] | `std::sqrt` |
| `hn::Round` | (exact) | Full float range | `std::round` (ties to even) |

For audio spectral processing, these accuracy bounds are more than sufficient. Single-precision IEEE 754 floats have approximately 24 bits of mantissa (approximately 7 decimal digits). A 4 ULP error on `hn::Log` corresponds to an error of approximately 4 x 2^-23 relative to the correct result -- far below audible thresholds. Phase processing typically requires approximately 20-bit accuracy; Highway's functions exceed this requirement.

### References

- Smith, J.O., "Spectral Audio Signal Processing", CCRMA Stanford -- spectral processing pipeline stages
- Laroche, J. & Dolson, M. (1999), "New phase-vocoder techniques for pitch-shifting, harmonizing and other exotic effects", IEEE WASPAA -- phase vocoder architecture
- Google Highway math-inl.h source -- ULP accuracy specifications
- Pommier, J., sse_mathfun -- original SSE math reference (superseded by Highway in this project)
- Mizrak, T., sse_mathfun_extension -- SSE atan2 reference (superseded by Highway)

## Clarifications

### Session 2026-02-18

- Q: Should `kMinLogInput` be a shared constant in `spectral_simd.h` and `batchLog10()` clamp internally, with `FormantPreserver::extractEnvelope()` removing its per-element pre-clamp and relying on `batchLog10()`'s internal clamp? → A: Yes. Define `kMinLogInput = 1e-10f` in `spectral_simd.h`; `batchLog10()` clamps non-positive inputs internally; `FormantPreserver::extractEnvelope()` removes the `std::max(magnitudes[k], kMinMagnitude)` pre-clamp and delegates clamping entirely to `batchLog10()`.
- Q: How should `FormantPreserver::reconstructEnvelope()` bridge the `complexBuf_[k].real` (Complex struct field) into the contiguous `const float*` that `batchPow10()` requires? → A: Reuse the existing `logMag_` scratch buffer as a staging area: write `complexBuf_[k].real` into `logMag_[0..numBins_-1]` in a scalar loop, then call `batchPow10(logMag_.data(), envelope_.data(), numBins_)`. No new allocation is needed.
- Q: Is Phase C (batchWrapPhase) additive only -- function implemented and tested but zero existing call sites updated in this spec -- with adoption deferred to future specs? → A: Yes. Phase C is strictly additive: implement and test `batchWrapPhase()`; no existing call sites (e.g., `PhaseVocoderPitchShifter`) are modified in this spec.
- Q: Where do the SC-001 through SC-003 performance benchmark cases live? → A: Add `[performance]`-tagged `TEST_CASE` sections to the existing `dsp/tests/unit/core/spectral_simd_test.cpp`, consistent with the project pattern (e.g., `modulation_engine_perf_test.cpp`). No new file or CMake target is needed; benchmarks run within the existing `dsp_tests` target and are skipped in normal CI unless the `[performance]` tag is explicitly requested.
- Q: What form does the SC-007/SC-008 "pre-optimization golden reference" take for FormantPreserver -- a stored binary fixture file or an in-test scalar computation? → A: In-test scalar reference computed at test time. The test runs scalar `std::log10`/`std::pow` loops inline alongside the SIMD path and compares both outputs within tolerance. No binary fixture file is stored or checked in; no pre-commit capture step is required.

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Vectorized Formant Preservation Log/Exp (Priority: P1)

A DSP developer uses the FormantPreserver with the harmonizer engine. The cepstral envelope extraction pipeline (`log10(magnitude)` -> IFFT -> lifter -> FFT -> `pow(10, logEnv)`) uses SIMD-accelerated `log10` and `exp` operations instead of scalar `std::log10` and `std::pow` loops. The formant-preserved output is audibly identical to the scalar version, with measurably lower CPU cost.

**Why this priority**: The formant preservation log/exp loop processes 2049 bins per frame per voice. With 4 voices, this is approximately 16,392 scalar transcendental calls per frame -- the single largest remaining scalar bottleneck in the spectral pipeline. This is the highest-impact optimization that is not already implemented.

**Independent Test**: Can be tested by comparing FormantPreserver output (envelope and final magnitudes) between the SIMD and scalar paths for identical input data across all supported FFT sizes. CPU savings can be benchmarked independently of the harmonizer engine.

**Acceptance Scenarios**:

1. **Given** a FormantPreserver processing a 4096-point FFT frame, **When** extracting the spectral envelope using SIMD log10, **Then** the output envelope matches the scalar `std::log10` result within 1e-5 per bin.
2. **Given** a FormantPreserver reconstructing the envelope using SIMD exp, **When** converting the smoothed log-envelope back to linear magnitude, **Then** the output matches the scalar `std::pow(10.0f, x)` result within 1e-5 per bin.
3. **Given** a FormantPreserver with SIMD-accelerated log/exp, **When** benchmarking envelope extraction + reconstruction on 2049 bins, **Then** the operation is measurably faster than the scalar baseline.
4. **Given** a HarmonizerEngine with 4 voices in PhaseVocoder mode with formant preservation enabled, **When** processing audio, **Then** the output is audibly identical to the pre-optimization output (RMS difference less than 1e-5 over 1 second at 44.1kHz).

---

### User Story 2 - Batch Phase Wrapping (Priority: P2)

A DSP developer performs spectral phase processing (phase difference computation, phase accumulation, frequency estimation) that requires wrapping arrays of phase values to the [-pi, +pi] range. A new batch phase wrapping function processes arrays of phase values using SIMD, replacing the scalar while-loop or fmod-based `wrapPhase()` / `wrapPhaseFast()` functions for bulk operations.

**Why this priority**: Phase wrapping is called for every bin during phase difference and accumulation stages. While each individual call is cheap, the cumulative cost over 2049 bins per frame is measurable, and the while-loop version (`wrapPhase()`) has branch misprediction overhead for large phase values. The SIMD version eliminates branches entirely with a branchless round-and-subtract formula.

**Independent Test**: Can be tested by wrapping arrays of known phase values (including edge cases at boundaries, large multiples of 2*pi, negative values, zero) and comparing SIMD output against scalar `wrapPhase()` output.

**Acceptance Scenarios**:

1. **Given** an array of 2049 phase values spanning [-100*pi, +100*pi], **When** batch-wrapping to [-pi, +pi] using the SIMD function, **Then** every output value matches scalar `wrapPhase()` within 1e-6.
2. **Given** an array of phase values including exact boundary values (-pi, +pi, 0, +/-2*pi), **When** batch-wrapping, **Then** outputs are correctly wrapped (values at exactly +/-pi remain at +/-pi, values at +/-2*pi wrap to 0).
3. **Given** batch wrapping of 2049 phase values, **When** benchmarking against scalar `wrapPhase()` loop, **Then** the SIMD version is measurably faster.

---

### User Story 3 - Vectorized Bulk Log and Exp Utility Functions (Priority: P2)

A DSP developer needs to apply `log10` or `exp` (or `pow(10, x)`) to an entire array of float values as part of spectral processing. New utility functions `batchLog10()` and `batchPow10()` in `spectral_simd.h` provide SIMD-accelerated bulk operations that any Layer 1+ component can use, not just FormantPreserver.

**Why this priority**: The log/exp operations are currently used only in FormantPreserver, but the batch utility functions are general-purpose and reusable. Factoring them as Layer 0 utilities enables any future spectral processor (spectral compressor, spectral noise gate, loudness analysis) to use SIMD log/exp without reimplementation.

**Independent Test**: Can be tested by applying batch log10 and pow10 to arrays of known values (powers of 10, fractional values, near-zero values, large values) and comparing against scalar results.

**Acceptance Scenarios**:

1. **Given** an array of 1024 positive float values, **When** applying `batchLog10()`, **Then** every output value matches `std::log10(input[k])` within 1e-5.
2. **Given** an array of 1024 float values in [-38, +38], **When** applying `batchPow10()`, **Then** every output value matches `std::pow(10.0f, input[k])` within relative error of 1e-5.
3. **Given** input arrays with non-multiple-of-SIMD-width lengths (e.g., 1, 3, 5, 7, 1025), **When** applying batch operations, **Then** scalar tail handling produces correct results for all remaining elements.

---

### User Story 4 - End-to-End Harmonizer Performance Improvement (Priority: P3)

A plugin developer runs the HarmonizerEngine with 4 voices in PhaseVocoder mode with formant preservation enabled. The SIMD-optimized formant preservation and phase wrapping reduce the total CPU cost, contributing to meeting or exceeding the established performance budgets.

**Why this priority**: This is the integration-level validation that the SIMD optimizations actually reduce the harmonizer's total CPU cost in a realistic processing scenario. Individual function benchmarks (Stories 1-3) measure component-level improvement; this story measures system-level impact.

**Independent Test**: Can be tested by benchmarking the full HarmonizerEngine with 4 PhaseVocoder voices with formant preservation enabled, before and after the SIMD optimizations, using the same benchmark harness and methodology as spec 065.

**Acceptance Scenarios**:

1. **Given** a HarmonizerEngine with 4 voices in PhaseVocoder mode with formant preservation enabled, **When** benchmarking at 44.1kHz, block size 256, **Then** the total CPU cost is measurably lower than the pre-optimization baseline.
2. **Given** the SIMD-optimized pipeline, **When** processing audio, **Then** the output is bit-identical or within floating-point tolerance (RMS difference less than 1e-5) compared to the pre-optimization output.

---

### Edge Cases

- What happens when `batchLog10()` receives input values that are zero or negative? The function MUST clamp inputs to the minimum positive value `kMinLogInput = 1e-10f` before computing log10, consistent with `FormantPreserver::extractEnvelope()` which already clamps to `kMinMagnitude`. Negative inputs are clamped identically. The function MUST NOT produce NaN or infinity in any output element.
- What happens when `batchPow10()` receives input values that would overflow float32 (i.e., `x > 38.5`)? The function MUST clamp outputs to the maximum value `kMaxPow10Output = 1e6f`, consistent with `FormantPreserver::reconstructEnvelope()` which already clamps to `1e6f`. The function MUST NOT produce infinity.
- What happens when batch functions receive zero-length arrays (`count == 0`)? The function MUST return immediately without reading or writing any memory.
- What happens when batch functions receive arrays whose length is not a multiple of the SIMD lane width? A scalar tail loop MUST process remaining elements, consistent with the existing pattern in `computePolarBulk()` and `reconstructCartesianBulk()`.
- What happens when input arrays are not SIMD-aligned? Highway's `ScalableTag<float>` handles unaligned loads via `hn::LoadU` where necessary. The batch functions MUST work correctly regardless of alignment, though aligned arrays may perform better.
- What happens to the existing scalar `wrapPhase()` and `wrapPhaseFast()` functions? They MUST remain unchanged for single-value use. The new batch function is an addition for array processing, not a replacement.
- What happens when `batchWrapPhase()` receives phase values that are extremely large (e.g., 1e6 radians)? The branchless formula `phase - 2*pi * round(phase / (2*pi))` handles arbitrarily large values correctly in a single operation, unlike the while-loop which would iterate many times. The function MUST produce correct results for any finite float input.

## Requirements *(mandatory)*

### Functional Requirements

#### Phase A: Bulk Log/Exp/Phase Utility Functions (Layer 0)

- **FR-001**: A new function `batchLog10()` MUST be added to `spectral_simd.h`/`.cpp` that computes `log10(x)` for an array of float values using Highway's `hn::Log10(d, v)` in a SIMD loop with scalar tail. The function signature MUST be: `void batchLog10(const float* input, float* output, std::size_t count) noexcept;`. A shared constant `kMinLogInput = 1e-10f` MUST be defined in `spectral_simd.h` (in the `Krate::DSP` namespace). Input values at or below zero MUST be clamped to `kMinLogInput` before computing log10 (using `hn::Max` in the SIMD path and a scalar equivalent in the tail), producing a finite negative result instead of NaN or -infinity. This constant is the single source of truth for minimum-magnitude clamping shared across `batchLog10()` and any caller (including `FormantPreserver`) that previously defined its own equivalent.
- **FR-002**: A new function `batchPow10()` MUST be added to `spectral_simd.h`/`.cpp` that computes `10^x` for an array of float values. This MUST be implemented as `hn::Exp(d, hn::Mul(v, hn::Set(d, kLn10)))` where `kLn10 = std::log(10.0f) = 2.302585093f` (converting `10^x` to `e^(x * ln(10))` because Highway provides `hn::Exp` but not `hn::Pow10`). The function signature MUST be: `void batchPow10(const float* input, float* output, std::size_t count) noexcept;`. Output values MUST be clamped to `[kMinLogInput, kMaxPow10Output = 1e6f]` to prevent infinity and maintain consistency with FormantPreserver's existing clamping.
- **FR-003**: A new function `batchWrapPhase()` MUST be added to `spectral_simd.h`/`.cpp` that wraps an array of phase values to the [-pi, +pi] range using the branchless formula: `output[k] = input[k] - 2*pi * round(input[k] / (2*pi))`. The Highway implementation MUST use `hn::Round(hn::Mul(v, inv_twopi))` where `hn::Round` performs round-to-nearest-even. The function signature MUST be: `void batchWrapPhase(const float* input, float* output, std::size_t count) noexcept;`. An in-place variant `void batchWrapPhase(float* data, std::size_t count) noexcept;` MUST also be provided.
- **FR-004**: All three new batch functions (FR-001, FR-002, FR-003) MUST follow the existing Highway self-inclusion pattern established in `spectral_simd.cpp`: per-target SIMD kernels compiled via `foreach_target.h`, exported via `HWY_EXPORT`, dispatched via `HWY_DYNAMIC_DISPATCH`. This ensures runtime ISA selection (SSE2/AVX2/AVX-512/NEON) with no compile-time platform checks required by callers.
- **FR-005**: All three new batch functions MUST handle `count == 0` by returning immediately. All MUST handle non-multiple-of-lane-width counts via a scalar tail loop, following the same pattern as existing `computePolarBulk()`.
- **FR-006**: All three new batch functions MUST be `noexcept`, perform zero heap allocations, and use no branching in the SIMD hot loop (branchless clamping via `hn::Max` / `hn::Min` for FR-001 and FR-002).

#### Phase B: FormantPreserver SIMD Integration (Layer 2)

- **FR-007**: `FormantPreserver::extractEnvelope()` MUST replace its scalar `std::log10(mag)` loop with a call to `batchLog10()` from `spectral_simd.h`. The per-element pre-clamp (`std::max(magnitudes[k], kMinMagnitude)`) MUST be removed; clamping is delegated entirely to `batchLog10()`'s internal clamp against `kMinLogInput` (defined in `spectral_simd.h`). Since `kMinLogInput == kMinMagnitude == 1e-10f`, the observable behavior is identical. `FormantPreserver` MUST NOT define a separate clamp constant for this path.
- **FR-008**: `FormantPreserver::reconstructEnvelope()` MUST replace its scalar `std::pow(10.0f, logEnv)` loop with a call to `batchPow10()` from `spectral_simd.h`. Because `complexBuf_` contains interleaved `Complex` structs rather than a contiguous float array, `reconstructEnvelope()` MUST first copy `complexBuf_[k].real` into `logMag_[0..numBins_-1]` using a scalar loop (a cheap integer-indexed copy, not a transcendental operation), then call `batchPow10(logMag_.data(), envelope_.data(), numBins_)`. The existing post-clamp behavior (`std::max(kMinMagnitude, std::min(envelope_[k], 1e6f))`) is satisfied by `batchPow10()`'s internal output clamping to `[kMinLogInput, kMaxPow10Output]` (FR-002); no additional post-clamp loop is needed.
- **FR-009**: After SIMD integration, `FormantPreserver::extractEnvelope()` and `reconstructEnvelope()` MUST produce output identical to the pre-SIMD scalar implementation within a per-bin tolerance of 1e-5. This equivalence MUST be verified by automated tests comparing SIMD output against a scalar reference.
- **FR-010**: The SIMD integration MUST NOT change the public API of `FormantPreserver`. The `extractEnvelope()`, `getEnvelope()`, and `applyFormantPreservation()` method signatures MUST remain unchanged. The optimization is purely internal.

#### Phase C: Batch Phase Wrapping Availability (Layer 0)

- **FR-011**: A batch phase wrapping function `batchWrapPhase()` MUST be implemented in `spectral_simd.h`/`.cpp` and covered by unit tests. Phase C is strictly additive: no existing call sites (including `PhaseVocoderPitchShifter` or any other spectral processor) are modified in this spec. Adoption at call sites is deferred to future specs. The function MUST be declared in the public header so that any Layer 1+ component can call it without additional build system changes.
- **FR-012**: The existing scalar `wrapPhase()` and `wrapPhaseFast()` functions in `spectral_utils.h` MUST remain unchanged. The batch function is an addition for array processing, not a replacement for single-value use.
- **FR-013**: The batch phase wrapping function MUST produce results that match the scalar `wrapPhase()` function within 1e-6 for all finite float inputs. For values very close to the +/-pi boundary (within 1 ULP), the SIMD and scalar versions may differ by at most 1 ULP due to different rounding modes.

#### General

- **FR-014**: All new functions MUST be placed in the `Krate::DSP` namespace, consistent with the existing `computePolarBulk()` and `reconstructCartesianBulk()` functions.
- **FR-015**: No new external dependencies MUST be introduced. All SIMD operations MUST use the existing Google Highway dependency (v1.2.0, already fetched via FetchContent in `CMakeLists.txt`).
- **FR-016**: The `spectral_simd.h` public header MUST NOT expose any Highway-internal types (`hn::Vec`, `hn::ScalableTag`, etc.). The public API MUST use only raw float pointers and `std::size_t`, consistent with the existing API surface.
- **FR-017**: All existing tests in `dsp_tests` MUST continue to pass after the changes. Zero regressions.
- **FR-018**: The architecture documentation at `specs/_architecture_/` MUST be updated to include entries for `batchLog10()`, `batchPow10()`, and `batchWrapPhase()` with purpose, public API summary, file location (`dsp/include/krate/dsp/core/spectral_simd.h`), and "when to use this" guidance. The new constants `kMinLogInput` and `kMaxPow10Output` MUST also be documented. This MUST be completed as the final task of the implementation before claiming spec completion.

### Key Entities

- **`batchLog10()`**: Computes element-wise `log10(x)` for a float array using Highway `hn::Log10`. Located in `spectral_simd.h`/`.cpp` (Layer 0). Clamps non-positive inputs to `kMinLogInput = 1e-10f` (defined in `spectral_simd.h`) to avoid NaN/infinity. This is the single shared clamp constant; callers MUST NOT define a separate equivalent.
- **`batchPow10()`**: Computes element-wise `10^x` for a float array using Highway `hn::Exp(d, hn::Mul(v, kLn10))`. Located in `spectral_simd.h`/`.cpp` (Layer 0). Clamps outputs to `[kMinLogInput, kMaxPow10Output]` to avoid infinity.
- **`batchWrapPhase()`**: Wraps an array of phase values to [-pi, +pi] using branchless `round(x / 2pi) * 2pi` subtraction via Highway `hn::Round`. Located in `spectral_simd.h`/`.cpp` (Layer 0). Two overloads: out-of-place and in-place.
- **`FormantPreserver`**: Existing Layer 2 processor. Modified internally to call `batchLog10()` and `batchPow10()` instead of scalar loops. Public API unchanged.

## Success Criteria *(mandatory)*

### Benchmark Contract

All performance measurements for this spec MUST be implemented as `[performance]`-tagged `TEST_CASE` sections added to `dsp/tests/unit/core/spectral_simd_test.cpp`, consistent with the project's existing benchmark pattern (e.g., `modulation_engine_perf_test.cpp`). They MUST run within the existing `dsp_tests` CMake target -- no new target is introduced. Benchmarks MUST be run in Release builds with optimizations enabled. Each benchmark MUST report both metrics: (1) execution time in microseconds per call, and (2) throughput in elements per second. Component-level benchmarks (SC-001 through SC-003) MUST be run on arrays of size 2049 (matching the FFT size 4096 -> 2049 bins used in the harmonizer pipeline). Performance benchmarks are excluded from normal CI runs and MUST be invoked explicitly using the `[performance]` Catch2 tag filter.

### Measurable Outcomes

- **SC-001**: `batchLog10()` processing 2049 elements MUST execute faster than an equivalent scalar `std::log10()` loop by a factor of at least 2x (measured as wall-clock time ratio, Release build). The improvement will vary by platform (higher on AVX2/AVX-512, lower on SSE2-only or NEON).
- **SC-002**: `batchPow10()` processing 2049 elements MUST execute faster than an equivalent scalar `std::pow(10.0f, x)` loop by a factor of at least 2x (measured as wall-clock time ratio, Release build).
- **SC-003**: `batchWrapPhase()` processing 2049 elements MUST execute faster than an equivalent scalar `wrapPhase()` loop by a factor of at least 2x (measured as wall-clock time ratio, Release build).
- **SC-004**: `batchLog10()` output MUST match scalar `std::log10()` within a per-element absolute error of 1e-5 for all positive float inputs in the range [1e-10, 1e6]. This tolerance accounts for the Highway `hn::Log10` accuracy of 2 ULP.
- **SC-005**: `batchPow10()` output MUST match scalar `std::pow(10.0f, x)` within a per-element relative error of 1e-5 for inputs in the range [-10, +6] (the practical range for log-magnitude spectral envelopes in audio: log10(1e-10) = -10, log10(1e6) = 6). This tolerance accounts for the Highway `hn::Exp` accuracy of 1 ULP.
- **SC-006**: `batchWrapPhase()` output MUST match scalar `wrapPhase()` within a per-element absolute error of 1e-6 for inputs in the range [-1000*pi, +1000*pi].
- **SC-007**: `FormantPreserver::extractEnvelope()` output MUST be identical (per-bin error less than 1e-5) to the scalar reference for a fixed test signal. The reference is computed inline in the test using scalar `std::log10` loops -- no stored binary fixture file is required. The test constructs a known magnitude array, runs both the scalar reference and the SIMD-integrated `extractEnvelope()`, and compares per-bin.
- **SC-008**: `FormantPreserver::reconstructEnvelope()` output MUST be identical (per-bin error less than 1e-5) to the scalar reference for the same fixed test signal. The reference is computed inline using scalar `std::pow(10.0f, x)` loops in the same test fixture as SC-007, with no stored binary file.
- **SC-009**: All existing `dsp_tests` MUST pass after the changes with zero test modifications. Zero regressions.
- **SC-010**: The SIMD hot loops MUST perform zero heap allocations (verified by code inspection only -- no automated test enforces this criterion). All batch functions MUST be `noexcept`.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Highway v1.2.0 (already in the project) provides `hn::Log`, `hn::Log10`, `hn::Exp`, and `hn::Round` with the accuracy specifications documented above. This has been verified by examining the existing `spectral_simd.cpp` which successfully uses `hn::Atan2`, `hn::Sin`, `hn::Cos`, `hn::Sqrt`, and `hn::MulAdd` from the same `hwy/contrib/math/math-inl.h` header.
- The `FormantPreserver`'s cepstral pipeline (`log10 -> IFFT -> lifter -> FFT -> pow10`) uses float32 throughout. No double-precision path exists or is needed.
- The mathematical identity `10^x = e^(x * ln(10))` is exact in infinite precision and introduces no additional error beyond the ULP bounds of `hn::Exp` when used in float32 arithmetic. The constant `ln(10) = 2.302585093...` is representable to full float32 precision.
- The branchless phase wrapping formula `x - 2*pi * round(x / (2*pi))` produces results in [-pi, +pi] for all finite float inputs. This is mathematically equivalent to the iterative while-loop version but executes in O(1) operations per element regardless of input magnitude.
- The `FormantPreserver::extractEnvelope()` method's symmetric log-magnitude mirroring loop (copying `logMag_[k]` to `logMag_[fftSize_ - k]`) operates on the already-computed log-magnitude values and does not benefit from SIMD optimization (it is a simple copy with reversed indexing, not a transcendental computation).
- `FormantPreserver::computeCepstrum()` and `applyLifter()` do not contain transcendental math -- they use FFT (already SIMD via pffft) and simple multiplication respectively. These methods are not optimization targets for this spec.
- Performance measurements will be conducted on x86-64 hardware. The SIMD speedup factors will differ on ARM/NEON platforms, but the correctness requirements (accuracy tolerances) are platform-independent.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that MUST be reused or extended (not reimplemented):**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `computePolarBulk()` | `dsp/include/krate/dsp/core/spectral_simd.h`/`.cpp` (L0) | Pattern reference: existing Highway SIMD function with self-inclusion, HWY_EXPORT, HWY_DYNAMIC_DISPATCH. New functions follow identical pattern. |
| `reconstructCartesianBulk()` | `dsp/include/krate/dsp/core/spectral_simd.h`/`.cpp` (L0) | Pattern reference: same as above. |
| `computePowerSpectrumPffft()` | `dsp/include/krate/dsp/core/spectral_simd.h`/`.cpp` (L0) | Pattern reference: same as above. |
| `FormantPreserver` | `dsp/include/krate/dsp/processors/formant_preserver.h` (L2) | Modified: `extractEnvelope()` and `reconstructEnvelope()` scalar loops replaced with batch calls. |
| `wrapPhase()` | `dsp/include/krate/dsp/primitives/spectral_utils.h` (L1) | Unchanged. Scalar function preserved for single-value use. Batch version is additive. |
| `wrapPhaseFast()` | `dsp/include/krate/dsp/primitives/spectral_utils.h` (L1) | Unchanged. Scalar function preserved for single-value use. |
| `SpectralBuffer::ensurePolarValid()` | `dsp/include/krate/dsp/primitives/spectral_buffer.h` (L1) | Already calls `computePolarBulk()`. No changes needed. |
| `SpectralBuffer::ensureCartesianValid()` | `dsp/include/krate/dsp/primitives/spectral_buffer.h` (L1) | Already calls `reconstructCartesianBulk()`. No changes needed. |
| Highway FetchContent | `CMakeLists.txt` (lines 243-257) | Already configured. No changes needed. |
| Highway link | `dsp/CMakeLists.txt` (lines 43-44) | Already links `hwy` PRIVATE to KrateDSP. No changes needed. |

**ODR check**: No new types, classes, or structs are being created. All new functions are free functions in the `Krate::DSP` namespace within the existing `spectral_simd.h`/`.cpp` files. The function names `batchLog10`, `batchPow10`, and `batchWrapPhase` are unique -- verified by searching the codebase:

```bash
grep -r "batchLog10\|batchPow10\|batchWrapPhase" dsp/ plugins/
# Expected: no matches (names are unique)
```

**Search Results Summary**: All modifications extend existing files (`spectral_simd.h`/`.cpp` and `formant_preserver.h`). No new source files are created. No new class names or struct names are introduced. No ODR risk.

### Forward Reusability Consideration

**Downstream consumers that benefit from bulk log/exp/phase utilities:**
- Any future spectral processor that computes log-magnitude spectra (spectral compressor, spectral noise gate, loudness estimation)
- Any future spectral processor that performs phase manipulation (phase vocoder variants, spectral delay, spectral freeze)
- The existing `SpectralBuffer` could potentially use `batchWrapPhase()` internally if a phase-normalization API is added in the future

**Sibling features at same layer:**
- The Tier 3 operations from the roadmap's SIMD Priority Matrix (windowing, phase diff, overlap-add accumulation, spectral multiply/divide) could be future `spectral_simd.h` additions following the same pattern. The current spec establishes the pattern; future specs extend it.
- Multi-voice parallel processing (Tier 2, Priority 4 in the roadmap) could use SoA layout across 4 voices in SIMD lanes. This is a fundamentally different optimization (data parallelism across voices vs. data parallelism within a single array) and is explicitly out of scope.

## Risk Analysis

### Medium Risk

| Risk | Impact | Mitigation |
|------|--------|------------|
| **Highway accuracy differs from scalar across platforms** | FormantPreserver output could differ between x86 and ARM builds, causing non-deterministic test failures in CI | SC-004 through SC-008 define tolerances (1e-5) that are wider than worst-case ULP differences. All tests use tolerance-based comparison (Approx with margin), never exact equality. Platform-specific golden references are NOT used -- tolerance-based comparison is the only valid approach for cross-platform SIMD. |
| **`hn::Log10` or `hn::Exp` not available in Highway v1.2.0** | Would require upgrading Highway or implementing a manual workaround | Low probability: the existing `spectral_simd.cpp` already uses `hn::Atan2`, `hn::Sin`, `hn::Cos` from the same `hwy/contrib/math/math-inl.h` header. `hn::Log`, `hn::Log10`, `hn::Exp` are documented in the same header. If unavailable, fallback: `hn::Log10` can be computed as `hn::Mul(hn::Log(d, v), Set(d, 1.0f / std::log(10.0f)))`, and `pow10` can be computed as `hn::Exp(d, hn::Mul(v, Set(d, std::log(10.0f))))`. |
| **FormantPreserver output tolerance exceeded** | Tests fail due to accumulated floating-point differences between scalar and SIMD paths through the full cepstral pipeline (log -> IFFT -> lifter -> FFT -> exp) | The tolerance of 1e-5 per bin is approximately 100x wider than worst-case single-operation ULP error. If exceeded, investigate whether the difference is in the log/exp step alone (should be within ULP bounds) or accumulated through the FFT roundtrips. The FFT steps (pffft) are unchanged by this spec, so any new differences isolate to log/exp. |

### Low Risk

| Risk | Impact | Mitigation |
|------|--------|------------|
| **Branchless phase wrapping formula edge case at +/-pi** | `wrapPhase(pi)` might return `-pi` or `+pi` depending on rounding mode, differing from scalar version | Both `-pi` and `+pi` are mathematically equivalent phase values. Audio processing is invariant to this difference. Test tolerance of 1e-6 accommodates this. |
| **SIMD speedup less than 2x on some platforms** | SC-001 through SC-003 fail on platforms with narrow SIMD width (e.g., SSE2 = 4 lanes, no FMA) | The 2x threshold is conservative -- Highway typically achieves 3-6x on AVX2 for transcendental functions. Even on SSE2 (4 lanes), the loop overhead reduction and branch elimination should achieve 2x. If a specific platform underperforms, the correctness guarantees (SC-004 through SC-009) still hold. |
| **Build time increase from additional Highway template instantiations** | `spectral_simd.cpp` compile time increases due to more functions being compiled per ISA target | Acceptable trade-off for runtime performance. The file is already compiled once per target ISA. Adding 3 more small functions should add minimal compile time. |

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*For EACH row below, you MUST perform these steps before writing the status:*
1. *Re-read the requirement from the spec*
2. *Open the implementation file and find the code that satisfies it -- record the file path and line number*
3. *Run or read the test that proves it -- record the test name and its actual output/result*
4. *For numeric thresholds (SC-xxx): record the actual measured value vs the spec target*
5. *Only then write the status and evidence*

*DO NOT mark checkmark without having just verified the code and test output. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | `batchLog10()` declared at `spectral_simd.h:66`. `kMinLogInput = 1e-10f` at line 26. SIMD kernel `BatchLog10Impl` at `spectral_simd.cpp:162-178` uses `hn::Log10(d,v)` with `hn::Max(v, minVal)` clamp. Scalar tail lines 175-178. Tests `"batchLog10 known values"` and `"batchLog10 scalar comparison"` pass. |
| FR-002 | MET | `batchPow10()` at `spectral_simd.h:74`. `kMaxPow10Output = 1e6f` at line 29. Kernel at `spectral_simd.cpp:186-208` uses `hn::Exp(d, hn::Mul(v, ln10))` where `ln10 = std::numbers::ln10_v<float>`. Output clamped `hn::Max`/`hn::Min`. Tests pass. |
| FR-003 | MET | `batchWrapPhase()` out-of-place at `spectral_simd.h:82`, in-place at line 89. Kernels at `spectral_simd.cpp:215-260` use `hn::Round(hn::Mul(v, invTwoPi))` + `hn::NegMulAdd(n, twoPi, v)`. Both overloads tested and pass. |
| FR-004 | MET | `HWY_EXPORT` at `spectral_simd.cpp:283-286` for all 4 kernels. `HWY_DYNAMIC_DISPATCH` wrappers at lines 302-316. Runtime ISA dispatch via `foreach_target.h`. |
| FR-005 | MET | All kernels use `for (; k + N <= count; ...)` + `for (; k < count; ...)` tail pattern. count==0: zero iterations. Tests: `"Batch functions handle zero-length input"` and `"non-SIMD-width tail counts"` pass. |
| FR-006 | MET | All 4 declarations `noexcept` (`spectral_simd.h:66,74,82,89`). Kernels: zero heap allocations, branchless `hn::Max`/`hn::Min`/`hn::Round`. |
| FR-007 | MET | `extractEnvelope()` at `formant_preserver.h:121` calls `batchLog10(magnitudes, logMag_.data(), numBins_)`. Old scalar loop removed. `kMinMagnitude` NOT used in extractEnvelope. |
| FR-008 | MET | `reconstructEnvelope()` at `formant_preserver.h:214-224`: staging copy lines 218-220, `batchPow10()` at line 223. Old scalar loop + post-clamp removed. |
| FR-009 | MET | Tests `"extractEnvelope SIMD matches scalar reference (SC-007)"` and `"reconstructEnvelope SIMD matches scalar reference (SC-008)"` pass within 1e-5 per bin. |
| FR-010 | MET | All public signatures unchanged: `extractEnvelope`, `getEnvelope`, `applyFormantPreservation`, `numBins` at `formant_preserver.h:114,144,149,156,175`. |
| FR-011 | MET | Both `batchWrapPhase` overloads in public header `spectral_simd.h:82,89`. No existing call sites modified (grep confirmed). |
| FR-012 | MET | `spectral_utils.h`: `wrapPhase()` at line 173 and `wrapPhaseFast()` at line 182 unchanged. |
| FR-013 | MET | Test `"batchWrapPhase matches scalar wrapPhase (SC-006)"` at `spectral_simd_test.cpp:352` within 1e-6 tolerance. All pass. |
| FR-014 | MET | All functions in `namespace Krate::DSP` (`spectral_simd.h:21-92`). |
| FR-015 | MET | No new dependencies. `dsp/CMakeLists.txt` not modified. Uses existing Highway v1.2.0. |
| FR-016 | MET | Public API uses only `float*`, `const float*`, `std::size_t`. No Highway types in `spectral_simd.h`. |
| FR-017 | MET | Full `dsp_tests`: 5677/5677 test cases, 21,951,448 assertions, 0 failures. |
| FR-018 | MET | `specs/_architecture_/layer-0-core.md` updated with SIMD Batch Math Utilities section (lines 825-917). |
| SC-001 | MET | `batchLog10` speedup: **7.96x** (SIMD: 0.64us, scalar: 5.10us). Threshold: >= 2x. |
| SC-002 | MET | `batchPow10` speedup: **15.72x** (SIMD: 0.78us, scalar: 12.28us). Threshold: >= 2x. |
| SC-003 | MET | `batchWrapPhase` speedup: **96.47x** (SIMD: 0.13us, scalar: 12.85us). Threshold: >= 2x. |
| SC-004 | MET | Test `"batchLog10 scalar comparison"` at `spectral_simd_test.cpp:200`: 2049 elements within 1e-5 absolute. All pass. |
| SC-005 | MET | Test `"batchPow10 scalar comparison"` at `spectral_simd_test.cpp:242`: 2049 elements within 1e-5 relative. All pass. |
| SC-006 | MET | Test `"batchWrapPhase matches scalar wrapPhase"` at `spectral_simd_test.cpp:352`: 2049 elements within 1e-6 absolute. All pass. |
| SC-007 | MET | Test `"extractEnvelope SIMD matches scalar reference"` at `spectral_simd_test.cpp:530`: 2049 bins within 1e-5 per bin. Inline scalar reference. Passes. |
| SC-008 | MET | Test `"reconstructEnvelope SIMD matches scalar reference"` at `spectral_simd_test.cpp:587`: 2049 bins within 1e-5 relative per bin. Inline scalar reference. Passes. |
| SC-009 | MET | Full suite: 5677/5677 passed, 0 modified existing tests. |
| SC-010 | MET | Code inspection: zero `new`/`malloc`/`std::vector` in kernels (lines 162-260). All 4 functions `noexcept`. |

**Status Key:**
- MET: Requirement verified against actual code and test output with specific evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap and specific evidence of what IS met
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [X] Each FR-xxx row was verified by re-reading the actual implementation code (not from memory)
- [X] Each SC-xxx row was verified by running tests or reading actual test output (not assumed)
- [X] Evidence column contains specific file paths, line numbers, test names, and measured values
- [X] No evidence column contains only generic claims like "implemented", "works", or "test passes"
- [X] No test thresholds relaxed from spec requirements
- [X] No placeholder values or TODO comments in new code
- [X] No features quietly removed from scope
- [X] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: COMPLETE

All 18 functional requirements (FR-001 through FR-018) and all 10 success criteria (SC-001 through SC-010) are MET with specific evidence. No gaps, no relaxed thresholds, no removed features.
