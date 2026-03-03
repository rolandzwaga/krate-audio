# Implementation Plan: SIMD-Accelerated Math for KrateDSP Spectral Pipeline

**Branch**: `066-simd-optimization` | **Date**: 2026-02-18 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/066-simd-optimization/spec.md`

## Summary

Add three SIMD-accelerated batch math functions (`batchLog10`, `batchPow10`, `batchWrapPhase`) to `spectral_simd.h/.cpp` using the existing Google Highway integration, then integrate `batchLog10`/`batchPow10` into `FormantPreserver` to replace scalar `std::log10`/`std::pow` loops. The batch phase wrapping function is additive-only (no call site changes). All functions follow the established Highway self-inclusion pattern with `HWY_EXPORT`/`HWY_DYNAMIC_DISPATCH` for runtime ISA selection.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Google Highway v1.2.0 (already integrated via FetchContent), pffft (FFT backend)
**Storage**: N/A (in-memory DSP processing)
**Testing**: Catch2 (within existing `dsp_tests` target)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- x86-64 (SSE2/AVX2/AVX-512) + ARM (NEON)
**Project Type**: Shared DSP library (KrateDSP) in monorepo
**Performance Goals**: Each batch function must achieve at least 2x speedup over scalar equivalent for 2049 elements (SC-001 through SC-003)
**Constraints**: Zero heap allocations in SIMD hot loops, `noexcept`, no Highway types in public API
**Scale/Scope**: 3 new free functions in existing `.h/.cpp`, 1 existing class modified internally

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):** N/A -- this is a Layer 0/2 DSP library change, no VST3 processor/controller involvement.

**Principle II (Real-Time Safety):**
- [x] All new functions are `noexcept`
- [x] Zero heap allocations in SIMD hot loops
- [x] No locks, mutexes, exceptions, or I/O

**Principle III (Modern C++):**
- [x] C++20 compilation
- [x] `constexpr` constants, `inline constexpr` for shared values
- [x] No raw `new`/`delete`

**Principle IV (SIMD & DSP Optimization):**
- [x] SIMD viability is BENEFICIAL (see analysis below)
- [x] Scalar-First Workflow: Scalar implementations already exist (`std::log10`, `std::pow`, `wrapPhase`). This spec adds SIMD Phase 2 behind the same API.
- [x] Branchless inner loops (using `hn::Max`/`hn::Min` for clamping, `hn::Round` for phase wrapping)
- [x] Profiling will verify improvement (SC-001 through SC-003 benchmarks)

**Principle VI (Cross-Platform):**
- [x] Highway handles ISA dispatch internally -- no platform-specific code
- [x] Tests use tolerance-based comparison, never exact equality

**Principle VIII (Testing Discipline):**
- [x] Tests written BEFORE implementation code
- [x] All existing `dsp_tests` must pass (SC-009)
- [x] WARNING: `spectral_simd_test.cpp` is NOT in the `-fno-fast-math` exception list -- this is correct because it uses Highway functions, not `std::isnan`

**Principle IX (Layered Architecture):**
- [x] New batch functions in Layer 0 (`core/spectral_simd.h/.cpp`)
- [x] `FormantPreserver` in Layer 2 calls Layer 0 -- valid dependency direction

**Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) -- no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XVI (Pre-Implementation Research):**
- [x] All function names verified unique in codebase
- [x] No new types/classes/structs created

**Principle XVII (Honest Completion):**
- [x] Compliance table in spec requires specific evidence per requirement

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: None. All new code is free functions and `inline constexpr` constants.

**Utility Functions to be created**:

| Planned Function | Search Result | Existing? | Location | Action |
|------------------|--------------|-----------|----------|--------|
| `batchLog10` | No matches outside spec.md | No | -- | Create New in `spectral_simd.h/.cpp` |
| `batchPow10` | No matches outside spec.md | No | -- | Create New in `spectral_simd.h/.cpp` |
| `batchWrapPhase` | No matches outside spec.md | No | -- | Create New in `spectral_simd.h/.cpp` |

**Constants to be created**:

| Planned Constant | Search Result | Existing? | Location | Action |
|-----------------|--------------|-----------|----------|--------|
| `kMinLogInput` | No matches | No | -- | Create New in `spectral_simd.h` |
| `kMaxPow10Output` | No matches | No | -- | Create New in `spectral_simd.h` |
| `kLn10` | No matches | No | -- | Internal to `spectral_simd.cpp` only |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `computePolarBulk()` | `dsp/include/krate/dsp/core/spectral_simd.h/.cpp` | 0 | Pattern reference for Highway self-inclusion, HWY_EXPORT, HWY_DYNAMIC_DISPATCH |
| `reconstructCartesianBulk()` | `dsp/include/krate/dsp/core/spectral_simd.h/.cpp` | 0 | Pattern reference (same file) |
| `computePowerSpectrumPffft()` | `dsp/include/krate/dsp/core/spectral_simd.h/.cpp` | 0 | Pattern reference (same file) |
| `FormantPreserver` | `dsp/include/krate/dsp/processors/formant_preserver.h` | 2 | Modified: scalar loops replaced with batch calls |
| `wrapPhase()` | `dsp/include/krate/dsp/primitives/spectral_utils.h` | 1 | Unchanged. Scalar reference for batch version correctness tests |
| `wrapPhaseFast()` | `dsp/include/krate/dsp/primitives/spectral_utils.h` | 1 | Unchanged |
| `kPi`, `kTwoPi` | `dsp/include/krate/dsp/core/math_constants.h` | 0 | Used in `batchWrapPhase` scalar tail |
| `Complex` struct | `dsp/include/krate/dsp/primitives/fft.h` | 1 | `FormantPreserver::complexBuf_` uses this type |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (verified `spectral_simd.h` API surface)
- [x] `dsp/include/krate/dsp/primitives/spectral_utils.h` - Existing `wrapPhase`/`wrapPhaseFast` (will NOT be modified)
- [x] `dsp/include/krate/dsp/processors/formant_preserver.h` - Target for Phase B modifications
- [x] `specs/_architecture_/` - No conflicting component names

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No new types are being created. All new code is free functions in the `Krate::DSP` namespace within existing files. Function names (`batchLog10`, `batchPow10`, `batchWrapPhase`) are confirmed unique. Constants (`kMinLogInput`, `kMaxPow10Output`) are `inline constexpr` in a single header.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `FormantPreserver` | `kMinMagnitude` | `static constexpr float kMinMagnitude = 1e-10f;` | Yes |
| `FormantPreserver` | `extractEnvelope` | `void extractEnvelope(const float* magnitudes, float* outputEnvelope) noexcept` | Yes |
| `FormantPreserver` | `reconstructEnvelope` (private) | `void reconstructEnvelope() noexcept` | Yes |
| `FormantPreserver` | `logMag_` (private) | `std::vector<float> logMag_;` | Yes |
| `FormantPreserver` | `envelope_` (private) | `std::vector<float> envelope_;` | Yes |
| `FormantPreserver` | `complexBuf_` (private) | `std::vector<Complex> complexBuf_;` | Yes |
| `FormantPreserver` | `numBins_` (private) | `std::size_t numBins_ = 0;` | Yes |
| `FormantPreserver` | `fftSize_` (private) | `std::size_t fftSize_ = 0;` | Yes |
| `Complex` struct | `real` field | `float real = 0.0f;` | Yes |
| `wrapPhase` | function | `[[nodiscard]] inline float wrapPhase(float phase) noexcept` | Yes |
| `hn::Log10` | Highway math | `template<class D, class V> HWY_INLINE V Log10(D d, V x)` | Yes (verified in `math-inl.h`) |
| `hn::Exp` | Highway math | `template<class D, class V> HWY_INLINE V Exp(D d, V x)` | Yes (verified in `math-inl.h`) |
| `hn::Round` | Highway core | `HWY_API Vec Round(Vec v)` | Yes (verified in ops headers) |

### Files Read

**Headers:**
- [x] `dsp/include/krate/dsp/core/spectral_simd.h` - Current public API (3 functions)
- [x] `dsp/include/krate/dsp/processors/formant_preserver.h` - Full class implementation (header-only)
- [x] `dsp/include/krate/dsp/primitives/spectral_utils.h` - `wrapPhase()` and `wrapPhaseFast()` signatures
- [x] `dsp/include/krate/dsp/core/math_constants.h` - `kPi`, `kTwoPi` constants
- [x] `dsp/include/krate/dsp/primitives/fft.h` - `Complex` struct definition
- [x] `build/windows-x64-release/_deps/highway-src/hwy/contrib/math/math-inl.h` - Highway math function signatures

**Source files:**
- [x] `dsp/include/krate/dsp/core/spectral_simd.cpp` - Highway self-inclusion pattern, SIMD kernel structure. Note: this `.cpp` lives under the `include/` tree by design -- Highway's self-inclusion pattern requires `HWY_TARGET_INCLUDE` to resolve the file via the same include path used by `foreach_target.h`.

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `Complex` struct | Fields are `real` and `imag`, NOT `re`/`im` | `complexBuf_[k].real` |
| `FormantPreserver::kMinMagnitude` | Value is `1e-10f`, same as planned `kMinLogInput` | After integration, FormantPreserver delegates clamping to `batchLog10` |
| `FormantPreserver::reconstructEnvelope` | `complexBuf_` contains interleaved `Complex` structs (8 bytes each), NOT contiguous floats | Must copy `.real` fields into contiguous `logMag_` buffer before calling `batchPow10` |
| Highway `hn::Round` | Round-to-nearest-even (IEEE 754 "banker's rounding") | Phase wrapping result at exact +/-pi may differ from scalar `wrapPhase` by up to 1 ULP |
| Highway self-inclusion | `spectral_simd.cpp` is re-included multiple times via `foreach_target.h` | New SIMD kernels go inside `HWY_BEFORE_NAMESPACE()`/`HWY_AFTER_NAMESPACE()` block, new dispatch wrappers in `#if HWY_ONCE` block |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| `batchLog10()` | General-purpose SIMD log10 for arrays | `spectral_simd.h/.cpp` | FormantPreserver, future spectral compressor, spectral noise gate, loudness analysis |
| `batchPow10()` | General-purpose SIMD pow10 for arrays | `spectral_simd.h/.cpp` | FormantPreserver, future spectral processors |
| `batchWrapPhase()` | General-purpose SIMD phase wrapping for arrays | `spectral_simd.h/.cpp` | Future PhaseVocoderPitchShifter, spectral delay, spectral freeze |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| -- | All new functions are Layer 0 free functions, not member functions |

**Decision**: All three batch functions are Layer 0 utilities in `spectral_simd.h/.cpp`, consistent with existing `computePolarBulk` and `reconstructCartesianBulk`. This maximizes reusability for any Layer 1+ consumer.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | `log10`, `pow10`, and phase wrapping are element-wise operations with no inter-element dependencies |
| **Data parallelism width** | 2049 elements | FFT size 4096 -> 2049 bins per frame. Excellent parallelism. |
| **Branch density in inner loop** | LOW | Clamping uses branchless `hn::Max`/`hn::Min`; phase wrapping uses branchless `hn::Round` formula |
| **Dominant operations** | Transcendental | `log10`, `exp`, `round` -- precisely what SIMD math libraries optimize |
| **Current CPU budget vs expected usage** | Formant path is 5-10% of non-FFT CPU per voice, 4 voices = 20-40% of non-FFT CPU | Significant headroom gain from SIMD |

### SIMD Viability Verdict

**Verdict**: BENEFICIAL

**Reasoning**: All three target operations are element-wise with no feedback dependencies, operating on arrays of 2049 floats -- well above the threshold where SIMD overhead is amortized. The operations are dominated by transcendental math (`log10`, `exp`) which Highway specifically optimizes (4-8x speedup typical on AVX2). Phase wrapping replaces a branch-heavy while-loop with branchless SIMD math. The project already has a proven Highway integration pattern in `spectral_simd.cpp`.

### Implementation Workflow

**This spec IS the SIMD Phase 2.** The scalar Phase 1 already exists:
- `std::log10()` in `FormantPreserver::extractEnvelope()` (lines 119-122)
- `std::pow(10.0f, x)` in `FormantPreserver::reconstructEnvelope()` (lines 218-223)
- `wrapPhase()` in `spectral_utils.h` (lines 173-177)

The scalar implementations serve as correctness oracles for the SIMD versions (SC-004 through SC-008).

| Phase | What | When | Deliverables |
|-------|------|------|-------------|
| **Phase 1 (Existing)** | Scalar implementations | Already complete | `std::log10`, `std::pow`, `wrapPhase` |
| **Phase 2 (This spec)** | SIMD-optimized batch functions behind same-signature API | This implementation | `batchLog10`, `batchPow10`, `batchWrapPhase` + FormantPreserver integration |

### Alternative Optimizations

Not applicable -- SIMD is the correct tool for this use case.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 0 (core utilities) + Layer 2 (FormantPreserver modification)

**Related features at same layer** (from harmonizer roadmap):
- Future spectral compressor (would use `batchLog10`/`batchPow10`)
- Future spectral noise gate (would use `batchLog10`)
- Future phase vocoder improvements (would use `batchWrapPhase`)
- Tier 3 SIMD operations from roadmap (windowing, phase diff, overlap-add)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `batchLog10()` | HIGH | Spectral compressor, spectral noise gate, loudness analysis | Extract now (Layer 0) |
| `batchPow10()` | HIGH | Any processor converting log-magnitude back to linear | Extract now (Layer 0) |
| `batchWrapPhase()` | HIGH | PhaseVocoderPitchShifter, spectral delay | Extract now (Layer 0) |
| `kMinLogInput` constant | HIGH | Any code needing minimum magnitude clamp | Extract now (Layer 0, `spectral_simd.h`) |

**Recommendation**: All three functions are already designed as Layer 0 free functions in `spectral_simd.h`, making them immediately reusable by any Layer 1+ component without additional work.

## Project Structure

### Documentation (this feature)

```text
specs/066-simd-optimization/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output (API contracts)
+-- quickstart.md        # Phase 1 output (implementation guide)
+-- contracts/           # Phase 1 output (function signatures)
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
|   +-- core/
|   |   +-- spectral_simd.h          # MODIFIED: Add batchLog10, batchPow10, batchWrapPhase declarations + constants
|   |   +-- spectral_simd.cpp        # MODIFIED: Add SIMD kernel implementations + HWY_EXPORT + dispatch wrappers
|   +-- processors/
|       +-- formant_preserver.h       # MODIFIED: Replace scalar log10/pow loops with batch calls
+-- tests/
    +-- unit/core/
        +-- spectral_simd_test.cpp    # MODIFIED: Add tests for all three batch functions + performance benchmarks
```

**Structure Decision**: No new files. All changes extend existing files per the spec's explicit requirement. The test file `spectral_simd_test.cpp` already exists and is registered in `dsp/tests/CMakeLists.txt`. No CMake changes needed.

## Complexity Tracking

No constitution violations identified. All design decisions align with existing patterns and principles.

---

# Phase 0: Research

## Research Tasks

### R1: Highway `hn::Log10` Availability and Accuracy

**Decision**: Use `hn::Log10(d, v)` from `hwy/contrib/math/math-inl.h`

**Rationale**: Verified present in `build/windows-x64-release/_deps/highway-src/hwy/contrib/math/math-inl.h` at the function declaration level. The existing `spectral_simd.cpp` already includes `hwy/contrib/math/math-inl.h` and uses `hn::Atan2`, `hn::Sin`, `hn::Cos`, `hn::Sqrt` from the same header. `hn::Log10` is documented with max error of 2 ULP for float32, which is well within the 1e-5 tolerance requirement.

**Alternatives considered**:
- `hn::Log(d, v)` with manual `* (1/ln(10))` conversion -- unnecessary since `hn::Log10` exists directly
- Scalar `std::log10` -- defeats the purpose of SIMD optimization

### R2: Highway `hn::Exp` Availability and Accuracy

**Decision**: Use `hn::Exp(d, v)` from `hwy/contrib/math/math-inl.h`

**Rationale**: Verified present in the same header. Max error 1 ULP for float32. Since Highway does not provide `hn::Pow10`, the `10^x` computation is implemented as `hn::Exp(d, hn::Mul(v, Set(d, ln(10))))` which is the mathematically exact identity `10^x = e^(x * ln(10))`.

**Alternatives considered**:
- Direct `hn::Pow10` -- does not exist in Highway v1.2.0
- `hn::Exp2(d, hn::Mul(v, Set(d, log2(10))))` -- slightly less accurate, no advantage

### R3: Highway `hn::Round` Availability

**Decision**: Use `hn::Round(v)` from Highway core ops (not contrib/math)

**Rationale**: `hn::Round` is a core Highway operation available on all backends (SSE2, AVX2, AVX-512, NEON, scalar). It performs IEEE 754 round-to-nearest-even ("banker's rounding"). This is used in the branchless phase wrapping formula: `output = input - 2*pi * Round(input / (2*pi))`.

**Alternatives considered**:
- `hn::NearestInt` + convert back -- extra conversion step, no benefit
- Manual floor/ceil combination -- more complex, no benefit

### R4: `FormantPreserver::complexBuf_` Layout and Staging Strategy

**Decision**: Reuse existing `logMag_` buffer as staging area for `batchPow10`

**Rationale**: `complexBuf_` is `std::vector<Complex>` where `Complex` has `{float real, float imag}` -- interleaved 8-byte structs, NOT contiguous floats. `batchPow10` requires a contiguous `const float*` input. The spec clarifies (Clarification session 2026-02-18) that `reconstructEnvelope()` should copy `complexBuf_[k].real` into `logMag_[0..numBins_-1]` using a scalar loop, then call `batchPow10(logMag_.data(), envelope_.data(), numBins_)`. The `logMag_` buffer is already allocated (size `fftSize_`, which is >= `numBins_`) and is no longer needed after `extractEnvelope()` completes, so reusing it incurs no additional allocation.

**Alternatives considered**:
- New dedicated staging buffer -- violates "no new allocation" and wastes memory
- Reinterpret `complexBuf_` with stride -- would require custom SIMD gather/scatter, far more complex

### R5: Scalar Tail Pattern

**Decision**: Follow existing `computePolarBulk` pattern: SIMD loop for `k + N <= count`, then scalar loop for remaining elements

**Rationale**: The existing pattern in `spectral_simd.cpp` (lines 46-71) uses `hn::Lanes(d)` to determine SIMD width, processes full lanes in the main loop, then uses standard scalar C++ for the tail. This is proven, portable, and simple.

### R6: Highway Self-Inclusion File Structure

**Decision**: Add new SIMD kernel functions (`BatchLog10Impl`, `BatchPow10Impl`, `BatchWrapPhaseImpl`, `BatchWrapPhaseInPlaceImpl`) inside the existing `HWY_BEFORE_NAMESPACE()`/`HWY_AFTER_NAMESPACE()` block in `spectral_simd.cpp`, and add new `HWY_EXPORT`/dispatch wrappers in the `#if HWY_ONCE` block.

**Rationale**: This is the exact pattern used by the three existing functions. No changes to `foreach_target.h` include or `HWY_TARGET_INCLUDE` macro are needed -- they already point to `spectral_simd.cpp`.

### R7: Performance Benchmark Pattern

**Decision**: Add `[performance]`-tagged TEST_CASE sections to `spectral_simd_test.cpp`, matching the pattern in `modulation_engine_perf_test.cpp`.

**Rationale**: The spec (Clarification session) explicitly requires benchmarks in `spectral_simd_test.cpp` with `[performance]` tag. The modulation engine test shows the pattern: use `std::chrono::high_resolution_clock`, run multiple iterations, report timing.

---

# Phase 1: Design & Contracts

## Data Model

### New Constants (`spectral_simd.h`)

```cpp
namespace Krate::DSP {

/// Minimum input value for log operations. Clamps zero/negative to avoid NaN/inf.
/// Shared constant: FormantPreserver and batchLog10 use this instead of separate equivalents.
inline constexpr float kMinLogInput = 1e-10f;

/// Maximum output value for pow10 operations. Prevents overflow to infinity.
inline constexpr float kMaxPow10Output = 1e6f;

} // namespace Krate::DSP
```

### New Function Signatures (`spectral_simd.h`)

```cpp
namespace Krate {
namespace DSP {

/// @brief Batch compute log10(x) for an array of floats using SIMD
/// @param input Input array of float values
/// @param output Output array (must hold count floats)
/// @param count Number of elements
/// @note Non-positive inputs are clamped to kMinLogInput before log10
/// @note SIMD-accelerated with runtime ISA dispatch
void batchLog10(const float* input, float* output, std::size_t count) noexcept;

/// @brief Batch compute 10^x for an array of floats using SIMD
/// @param input Input array of float values (exponents)
/// @param output Output array (must hold count floats)
/// @param count Number of elements
/// @note Output clamped to [kMinLogInput, kMaxPow10Output]
/// @note SIMD-accelerated with runtime ISA dispatch
void batchPow10(const float* input, float* output, std::size_t count) noexcept;

/// @brief Batch wrap phase values to [-pi, +pi] range using SIMD (out-of-place)
/// @param input Input array of phase values in radians
/// @param output Output array (must hold count floats)
/// @param count Number of elements
/// @note Uses branchless round-and-subtract formula
/// @note SIMD-accelerated with runtime ISA dispatch
void batchWrapPhase(const float* input, float* output, std::size_t count) noexcept;

/// @brief Batch wrap phase values to [-pi, +pi] range using SIMD (in-place)
/// @param data Array of phase values in radians (modified in-place)
/// @param count Number of elements
/// @note Uses branchless round-and-subtract formula
/// @note SIMD-accelerated with runtime ISA dispatch
void batchWrapPhase(float* data, std::size_t count) noexcept;

} // namespace DSP
} // namespace Krate
```

### SIMD Kernel Implementations (`spectral_simd.cpp`)

Inside the `HWY_BEFORE_NAMESPACE()` / `HWY_AFTER_NAMESPACE()` block:

#### `BatchLog10Impl`

```cpp
void BatchLog10Impl(const float* HWY_RESTRICT input,
                    float* HWY_RESTRICT output, size_t count) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto minVal = hn::Set(d, 1e-10f);  // kMinLogInput

    size_t k = 0;
    for (; k + N <= count; k += N) {
        auto v = hn::LoadU(d, input + k);
        v = hn::Max(v, minVal);  // Branchless clamp
        hn::StoreU(hn::Log10(d, v), d, output + k);
    }
    // Scalar tail
    for (; k < count; ++k) {
        float val = std::max(input[k], 1e-10f);
        output[k] = std::log10(val);
    }
}
```

#### `BatchPow10Impl`

```cpp
void BatchPow10Impl(const float* HWY_RESTRICT input,
                    float* HWY_RESTRICT output, size_t count) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto ln10 = hn::Set(d, 2.302585093f);  // std::log(10.0f)
    const auto minVal = hn::Set(d, 1e-10f);       // kMinLogInput
    const auto maxVal = hn::Set(d, 1e6f);          // kMaxPow10Output

    size_t k = 0;
    for (; k + N <= count; k += N) {
        const auto v = hn::LoadU(d, input + k);
        auto result = hn::Exp(d, hn::Mul(v, ln10));  // e^(x * ln(10)) = 10^x
        result = hn::Max(result, minVal);  // Clamp min
        result = hn::Min(result, maxVal);  // Clamp max
        hn::StoreU(result, d, output + k);
    }
    // Scalar tail
    for (; k < count; ++k) {
        float val = std::pow(10.0f, input[k]);
        val = std::max(1e-10f, std::min(val, 1e6f));
        output[k] = val;
    }
}
```

#### `BatchWrapPhaseImpl` (out-of-place)

```cpp
void BatchWrapPhaseImpl(const float* HWY_RESTRICT input,
                        float* HWY_RESTRICT output, size_t count) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto twoPi = hn::Set(d, 6.283185307f);   // 2 * pi
    const auto invTwoPi = hn::Set(d, 0.159154943f); // 1 / (2 * pi)

    size_t k = 0;
    for (; k + N <= count; k += N) {
        const auto v = hn::LoadU(d, input + k);
        const auto n = hn::Round(hn::Mul(v, invTwoPi));
        hn::StoreU(hn::NegMulAdd(n, twoPi, v), d, output + k);  // v - n * twoPi
    }
    // Scalar tail (using same branchless formula for consistency)
    for (; k < count; ++k) {
        float n = std::round(input[k] * (1.0f / (2.0f * 3.14159265358979323846f)));
        output[k] = input[k] - n * (2.0f * 3.14159265358979323846f);
    }
}
```

#### `BatchWrapPhaseInPlaceImpl`

```cpp
void BatchWrapPhaseInPlaceImpl(float* HWY_RESTRICT data, size_t count) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto twoPi = hn::Set(d, 6.283185307f);
    const auto invTwoPi = hn::Set(d, 0.159154943f);

    size_t k = 0;
    for (; k + N <= count; k += N) {
        const auto v = hn::LoadU(d, data + k);
        const auto n = hn::Round(hn::Mul(v, invTwoPi));
        hn::StoreU(hn::NegMulAdd(n, twoPi, v), d, data + k);
    }
    for (; k < count; ++k) {
        float n = std::round(data[k] * (1.0f / (2.0f * 3.14159265358979323846f)));
        data[k] = data[k] - n * (2.0f * 3.14159265358979323846f);
    }
}
```

### FormantPreserver Modifications (`formant_preserver.h`)

#### New Include

Add to top of file:
```cpp
#include <krate/dsp/core/spectral_simd.h>
```

#### `extractEnvelope()` Modification (FR-007)

**Before** (lines 119-122):
```cpp
for (std::size_t k = 0; k < numBins_; ++k) {
    float mag = std::max(magnitudes[k], kMinMagnitude);
    logMag_[k] = std::log10(mag);
}
```

**After**:
```cpp
batchLog10(magnitudes, logMag_.data(), numBins_);
```

The per-element `std::max(magnitudes[k], kMinMagnitude)` pre-clamp is removed because `batchLog10()` clamps internally to `kMinLogInput` (which equals `1e-10f == kMinMagnitude`).

#### `reconstructEnvelope()` Modification (FR-008)

**Before** (lines 218-223):
```cpp
for (std::size_t k = 0; k < numBins_; ++k) {
    float logEnv = complexBuf_[k].real;
    envelope_[k] = std::pow(10.0f, logEnv);
    envelope_[k] = std::max(kMinMagnitude, std::min(envelope_[k], 1e6f));
}
```

**After**:
```cpp
// Stage: copy Complex::real fields into contiguous buffer for batchPow10
for (std::size_t k = 0; k < numBins_; ++k) {
    logMag_[k] = complexBuf_[k].real;
}
batchPow10(logMag_.data(), envelope_.data(), numBins_);
```

The post-clamp `std::max(kMinMagnitude, std::min(envelope_[k], 1e6f))` is removed because `batchPow10()` clamps output to `[kMinLogInput, kMaxPow10Output]` = `[1e-10f, 1e6f]` internally.

---

## File-by-File Modification Plan

### File 1: `dsp/include/krate/dsp/core/spectral_simd.h`

**Current state**: 54 lines, 3 function declarations, no constants.

**Changes**:

1. **Add constants** after `namespace DSP {` opening (after line 23):
   - `inline constexpr float kMinLogInput = 1e-10f;`
   - `inline constexpr float kMaxPow10Output = 1e6f;`

2. **Add 4 function declarations** after `computePowerSpectrumPffft` declaration (after line 51):
   - `void batchLog10(const float* input, float* output, std::size_t count) noexcept;`
   - `void batchPow10(const float* input, float* output, std::size_t count) noexcept;`
   - `void batchWrapPhase(const float* input, float* output, std::size_t count) noexcept;`
   - `void batchWrapPhase(float* data, std::size_t count) noexcept;`

### File 2: `dsp/include/krate/dsp/core/spectral_simd.cpp`

**Note**: This `.cpp` file lives under the `include/` tree by design. Highway's self-inclusion pattern requires `HWY_TARGET_INCLUDE` to define the path that `foreach_target.h` uses to re-include the file for each ISA target. Because `dsp/include/krate/dsp/core/` is on the compiler include path, this path resolves correctly. This is not a naming error -- it is an intentional consequence of the Highway build pattern.

**Current state**: 196 lines. Highway self-inclusion pattern with 3 SIMD kernels + 3 dispatch wrappers.

**Changes**:

1. **Add SIMD kernels** inside the `HWY_BEFORE_NAMESPACE()` / `HWY_AFTER_NAMESPACE()` block, after `ComputePowerSpectrumPffftImpl` (after line 154, before the namespace closing braces):
   - `BatchLog10Impl`
   - `BatchPow10Impl`
   - `BatchWrapPhaseImpl`
   - `BatchWrapPhaseInPlaceImpl`

2. **Add HWY_EXPORT lines** inside `#if HWY_ONCE` block (after line 176):
   - `HWY_EXPORT(BatchLog10Impl);`
   - `HWY_EXPORT(BatchPow10Impl);`
   - `HWY_EXPORT(BatchWrapPhaseImpl);`
   - `HWY_EXPORT(BatchWrapPhaseInPlaceImpl);`

3. **Add dispatch wrapper functions** (after existing wrappers, before closing namespace braces at line 192):
   - `void batchLog10(...)` calling `HWY_DYNAMIC_DISPATCH(BatchLog10Impl)(...)`
   - `void batchPow10(...)` calling `HWY_DYNAMIC_DISPATCH(BatchPow10Impl)(...)`
   - `void batchWrapPhase(const float*, float*, size_t)` calling `HWY_DYNAMIC_DISPATCH(BatchWrapPhaseImpl)(...)`
   - `void batchWrapPhase(float*, size_t)` calling `HWY_DYNAMIC_DISPATCH(BatchWrapPhaseInPlaceImpl)(...)`

### File 3: `dsp/include/krate/dsp/processors/formant_preserver.h`

**Current state**: 241 lines. Header-only class.

**Changes**:

1. **Add include** (after line 30, with existing includes):
   ```cpp
   #include <krate/dsp/core/spectral_simd.h>
   ```

2. **Modify `extractEnvelope()`** (lines 119-122): Replace scalar `std::log10` loop with `batchLog10()` call. Remove `std::max(magnitudes[k], kMinMagnitude)` pre-clamp.

3. **Modify `reconstructEnvelope()`** (lines 218-223): Replace scalar `std::pow(10.0f, logEnv)` loop with staging copy + `batchPow10()` call. Remove post-clamp.

4. **Note on `kMinMagnitude`**: The constant `kMinMagnitude = 1e-10f` remains for other uses in the class (e.g., `applyFormantPreservation()` line 165: `std::max(shiftedEnvelope[k], kMinMagnitude)`). It is NOT removed.

### File 4: `dsp/tests/unit/core/spectral_simd_test.cpp`

**Current state**: 180 lines, 5 test cases.

**Changes**: Add the following new test cases:

1. **`batchLog10` correctness tests** (`[spectral_simd][batchLog10]`):
   - Known values (powers of 10, fractional values)
   - Comparison against scalar `std::log10` for 2049 elements within 1e-5 tolerance
   - Edge cases: zero input, negative input, very small input (1e-10), large input (1e6)
   - Scalar tail: non-multiples of SIMD width (1, 3, 5, 7, 1025)
   - Zero-length input

2. **`batchPow10` correctness tests** (`[spectral_simd][batchPow10]`):
   - Known values (integer exponents, fractional exponents)
   - Comparison against scalar `std::pow(10.0f, x)` for 2049 elements within 1e-5 relative error
   - Edge cases: very negative input (-10), large input (+6), overflow input (+39)
   - Output clamping verification (result in `[kMinLogInput, kMaxPow10Output]`)
   - Scalar tail: non-multiples of SIMD width
   - Zero-length input

3. **`batchWrapPhase` correctness tests** (`[spectral_simd][batchWrapPhase]`):
   - Known values: 0, pi, -pi, 2*pi, -2*pi, 100*pi, -100*pi
   - Comparison against scalar `wrapPhase()` for 2049 elements within 1e-6 tolerance
   - Boundary values: exact +/-pi, +/-2*pi
   - In-place variant verification
   - Scalar tail: non-multiples of SIMD width
   - Zero-length input

4. **`batchLog10`/`batchPow10` round-trip test** (`[spectral_simd][roundtrip]`):
   - Apply batchLog10 then batchPow10, verify output matches input within tolerance

5. **Performance benchmarks** (`[spectral_simd][performance]`):
   - SC-001: `batchLog10` vs scalar `std::log10` loop (2049 elements, Release build)
   - SC-002: `batchPow10` vs scalar `std::pow(10.0f, x)` loop (2049 elements)
   - SC-003: `batchWrapPhase` vs scalar `wrapPhase()` loop (2049 elements)

### File 5: `dsp/tests/CMakeLists.txt`

**No changes needed.** `spectral_simd_test.cpp` is already registered (line 47). Since we are only adding test cases to an existing file, no CMake modification is required.

### File 6: `dsp/CMakeLists.txt`

**No changes needed.** `spectral_simd.cpp` is already a source file for KrateDSP (line 29). Highway is already linked (line 45). No new source files are created.

---

## Test Strategy

### Test Categories

| Category | Test Tags | When to Run |
|----------|-----------|-------------|
| Correctness (batch functions) | `[spectral_simd][batchLog10]`, `[batchPow10]`, `[batchWrapPhase]` | Every build |
| Edge cases | `[spectral_simd][edge]` | Every build |
| Scalar tail | `[spectral_simd][tail]` | Every build |
| Round-trip | `[spectral_simd][roundtrip]` | Every build |
| FormantPreserver equivalence | `[spectral_simd][formant]` | Every build |
| Performance benchmarks | `[spectral_simd][performance]` | Manual, Release builds only |

### Test Execution

```bash
# All correctness tests (excludes performance)
dsp_tests.exe "[spectral_simd]" ~"[performance]"

# Performance benchmarks only (Release build)
dsp_tests.exe "[spectral_simd][performance]"

# Full test suite (regression check)
dsp_tests.exe
```

### FormantPreserver Equivalence Tests (SC-007, SC-008)

These tests must:
1. Create a FormantPreserver instance, prepare with fftSize=4096
2. Generate a known magnitude spectrum (e.g., harmonic series with exponential decay)
3. Compute scalar reference: run `std::log10` loop on same magnitudes, compare with `extractEnvelope()` output
4. Compute scalar reference for reconstructEnvelope: run `std::pow(10.0f, x)` loop, compare with internal `envelope_` after `extractEnvelope()` completes
5. Verify per-bin error < 1e-5

Since `extractEnvelope()` and `reconstructEnvelope()` are now using `batchLog10`/`batchPow10` internally, and these batch functions are tested independently against scalar references, the FormantPreserver equivalence test provides end-to-end validation through the full cepstral pipeline (log10 -> IFFT -> lifter -> FFT -> pow10).

---

## Risk Mitigations

| Risk | Mitigation |
|------|------------|
| Highway `hn::Log10` or `hn::Exp` not compiling on some platform | Verified both functions exist in Highway's `math-inl.h`. Fallback: compute `log10` as `Log(x) * (1/ln(10))`, `pow10` as `Exp(x * ln(10))` |
| FormantPreserver output tolerance exceeded (>1e-5) | The 1e-5 tolerance is approximately 100x wider than worst-case ULP error. If exceeded, isolate to log/exp step vs FFT roundtrip. The FFT steps are unchanged. |
| `hn::NegMulAdd` not available in Highway v1.2.0 | Alternative: `hn::Sub(v, hn::Mul(n, twoPi))` achieves the same result |
| Branchless phase wrapping gives different result at exact +/-pi boundary | Both -pi and +pi are valid wrapped values. Test tolerance of 1e-6 accommodates this. |
| `spectral_simd_test.cpp` compiled with `-ffast-math` breaks scalar reference in tests | The test file is NOT in the `-fno-fast-math` exception list, but the scalar references (`std::log10`, `std::pow`) are used only for comparison, not for NaN detection. If needed, can add the test file to the exception list. |
| Performance benchmark shows <2x speedup on SSE2-only | The 2x threshold is conservative. Even SSE2 (4 lanes) should achieve 2x due to branch elimination and optimized transcendental implementations. If not met on a specific CI runner, document the platform. |

---

## Implementation Order (Phases A, B, C from Spec)

### Phase A: Batch Utility Functions (Layer 0)

**Task Group A1: Write Tests**
1. Add `batchLog10` correctness + edge case tests to `spectral_simd_test.cpp`
2. Add `batchPow10` correctness + edge case tests
3. Add `batchWrapPhase` correctness + edge case tests (both overloads)
4. Add round-trip test (log10 -> pow10)
5. Add performance benchmark test cases (tagged `[performance]`)
6. Build and verify tests compile (they will fail since functions do not exist yet)

**Task Group A2: Implement Batch Functions**
1. Add constants (`kMinLogInput`, `kMaxPow10Output`) to `spectral_simd.h`
2. Add function declarations to `spectral_simd.h`
3. Add SIMD kernel implementations to `spectral_simd.cpp` (inside `HWY_BEFORE_NAMESPACE` block)
4. Add `HWY_EXPORT` and dispatch wrappers to `spectral_simd.cpp` (inside `#if HWY_ONCE` block)
5. Build and verify zero warnings
6. Run all tests and verify pass
7. Run performance benchmarks (Release build) and verify SC-001, SC-002, SC-003

### Phase B: FormantPreserver Integration (Layer 2)

**Task Group B1: Write Equivalence Tests**
1. Add FormantPreserver equivalence tests for extractEnvelope (SC-007)
2. Add FormantPreserver equivalence tests for reconstructEnvelope (SC-008)
3. Build and verify tests pass with current scalar implementation (establishes baseline)

**Task Group B2: Integrate SIMD**
1. Add `#include <krate/dsp/core/spectral_simd.h>` to `formant_preserver.h`
2. Replace `extractEnvelope()` scalar loop with `batchLog10()` call
3. Replace `reconstructEnvelope()` scalar loop with staging copy + `batchPow10()` call
4. Build and verify zero warnings
5. Run all tests (including FormantPreserver equivalence tests) and verify pass
6. Run full `dsp_tests` suite to verify zero regressions (SC-009)

### Phase C: Batch Phase Wrapping (Already Complete in Phase A)

Phase C is completed as part of Phase A -- `batchWrapPhase()` is implemented and tested but no existing call sites are modified. This phase exists only to confirm the function is available for future specs.

---

## Quickstart Guide

### Developer Getting Started

1. **Build**: No CMake changes needed. Standard build command:
   ```bash
   "$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
   ```

2. **Run correctness tests**:
   ```bash
   build/windows-x64-release/bin/Release/dsp_tests.exe "[spectral_simd]" ~"[performance]"
   ```

3. **Run performance benchmarks** (Release build only):
   ```bash
   build/windows-x64-release/bin/Release/dsp_tests.exe "[spectral_simd][performance]"
   ```

4. **Key pattern to follow**: Look at existing `ComputePolarImpl` in `spectral_simd.cpp` for the Highway self-inclusion pattern. New kernels follow the identical structure: per-target function inside `HWY_NAMESPACE`, exported via `HWY_EXPORT`, dispatched via `HWY_DYNAMIC_DISPATCH`.

### API Usage Examples

```cpp
#include <krate/dsp/core/spectral_simd.h>

// Batch log10 (e.g., for converting magnitudes to dB-like scale)
std::vector<float> mags(2049);
std::vector<float> logMags(2049);
Krate::DSP::batchLog10(mags.data(), logMags.data(), 2049);

// Batch pow10 (inverse of log10)
std::vector<float> envelope(2049);
Krate::DSP::batchPow10(logMags.data(), envelope.data(), 2049);

// Batch phase wrapping (out-of-place)
std::vector<float> phases(2049);
std::vector<float> wrapped(2049);
Krate::DSP::batchWrapPhase(phases.data(), wrapped.data(), 2049);

// Batch phase wrapping (in-place)
Krate::DSP::batchWrapPhase(phases.data(), 2049);
```
