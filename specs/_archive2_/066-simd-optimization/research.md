# Research: SIMD-Accelerated Math for KrateDSP Spectral Pipeline

**Date**: 2026-02-18 | **Spec**: 066-simd-optimization

## R1: Highway `hn::Log10` Availability and Accuracy

**Decision**: Use `hn::Log10(d, v)` from `hwy/contrib/math/math-inl.h`

**Rationale**: Verified present in the project's fetched Highway source at `build/windows-x64-release/_deps/highway-src/hwy/contrib/math/math-inl.h`. The function declaration is:

```cpp
template <class D, class V>
HWY_INLINE V Log10(D d, V x);
```

The existing `spectral_simd.cpp` already includes this header and successfully uses `hn::Atan2`, `hn::Sin`, `hn::Cos`, `hn::Sqrt` from the same file. `hn::Log10` is documented with max error of 2 ULP for float32, which is well within the 1e-5 absolute tolerance requirement (SC-004).

**Alternatives considered**:
- `hn::Log(d, v)` with manual `* (1.0f / std::log(10.0f))` conversion: Unnecessary complexity since `hn::Log10` exists. Would add one extra SIMD multiply per lane.
- Scalar `std::log10` in a loop: Defeats the purpose of SIMD optimization. Baseline for comparison only.
- Pommier `log_ps`: SSE-only, not portable to ARM/NEON. Superseded by Highway.

---

## R2: Highway `hn::Exp` Availability and Accuracy

**Decision**: Use `hn::Exp(d, v)` from `hwy/contrib/math/math-inl.h`

**Rationale**: Verified present in the same header. Function declaration:

```cpp
template <class D, class V>
HWY_INLINE V Exp(D d, V x);
```

Max error 1 ULP for float32 inputs in `[-FLT_MAX, +104]`. Since Highway does not provide a direct `Pow10` function, the `10^x` computation uses the identity `10^x = e^(x * ln(10))`:

```cpp
hn::Exp(d, hn::Mul(v, hn::Set(d, 2.302585093f)))  // ln(10) = 2.302585093...
```

The constant `ln(10)` is representable to full float32 precision (7 significant digits). The combined error from the multiplication + `hn::Exp` is bounded by 2 ULP, well within the 1e-5 relative tolerance (SC-005).

**Alternatives considered**:
- `hn::Exp2(d, hn::Mul(v, Set(d, log2(10))))`: Mathematically equivalent via `10^x = 2^(x * log2(10))`. No advantage -- both `Exp` and `Exp2` have similar ULP bounds.
- Direct `std::pow(10.0f, x)` scalar loop: Baseline for comparison only.

---

## R3: Highway `hn::Round` Availability

**Decision**: Use `hn::Round(v)` from Highway core ops

**Rationale**: `hn::Round` is a core Highway operation (not in contrib/math), available on all backends: SSE2 (via `_mm_round_ps` or emulation), AVX2 (`_mm256_round_ps`), AVX-512, NEON (`vrndnq_f32`), and scalar fallback. Verified in Highway's ops headers:

```cpp
HWY_API Vec Round(Vec v);  // Round-to-nearest-even (IEEE 754)
```

Used in the branchless phase wrapping formula:
```
output = input - 2*pi * Round(input / (2*pi))
```

**Key behavior**: IEEE 754 "banker's rounding" (round-to-nearest-even). At exact half-integer values, rounds to the nearest even integer. This means at exact +/-pi boundaries, the SIMD and scalar `wrapPhase()` (which uses while-loop subtraction) may produce slightly different results (one returning +pi, the other -pi). Both are mathematically valid. The test tolerance of 1e-6 (SC-006) accommodates this.

**Alternatives considered**:
- `hn::NearestInt(v)` + conversion back to float: Extra type conversion step, no benefit.
- Manual `floor(x + 0.5f)`: Not exactly banker's rounding, and more operations.

---

## R4: `FormantPreserver::complexBuf_` Layout and Staging Strategy

**Decision**: Reuse existing `logMag_` buffer as staging area for `batchPow10`

**Rationale**: The `complexBuf_` member is of type `std::vector<Complex>` where:

```cpp
struct Complex {
    float real = 0.0f;
    float imag = 0.0f;
};
```

This is an interleaved struct layout (8 bytes per element), NOT a contiguous float array. The `batchPow10` function requires `const float*` input with contiguous float elements. Therefore, `complexBuf_[k].real` values must be copied into a contiguous buffer before calling `batchPow10`.

The `logMag_` buffer (size `fftSize_`, which is always >= `numBins_`) is already allocated in `prepare()` and is no longer needed after `extractEnvelope()` completes step 1 (it's only used for the IFFT input in `computeCepstrum()`). Reusing it as staging avoids any new allocation.

The staging copy is a simple scalar loop:
```cpp
for (std::size_t k = 0; k < numBins_; ++k) {
    logMag_[k] = complexBuf_[k].real;
}
```

This is an integer-indexed memory copy (not a transcendental computation), so it adds negligible cost compared to the saved `pow` operations.

**Alternatives considered**:
- New dedicated staging buffer: Violates "no new allocation in prepare()" principle and wastes memory.
- SIMD gather from Complex::real with stride: Would require `hn::GatherIndex` with 8-byte stride, which is complex and not faster than a simple copy + contiguous SIMD processing.
- Modifying Complex to be a float array: Would break the entire codebase's use of `Complex`.

---

## R5: Scalar Tail Pattern

**Decision**: Follow existing `computePolarBulk` pattern

**Rationale**: The established pattern in `spectral_simd.cpp` (lines 46-71) is:

```cpp
const hn::ScalableTag<float> d;
const size_t N = hn::Lanes(d);

size_t k = 0;
// SIMD loop: process N elements per iteration
for (; k + N <= count; k += N) {
    // ... SIMD operations using hn::LoadU / hn::StoreU ...
}
// Scalar tail for remaining elements
for (; k < count; ++k) {
    // ... scalar equivalent ...
}
```

Key details:
- Uses `hn::LoadU` / `hn::StoreU` for unaligned access (safe for any input alignment)
- `hn::Lanes(d)` returns the SIMD width at compile time for the current target
- The scalar tail uses standard C++ math (`std::log10`, `std::pow`, etc.)
- Zero-length check: when `count == 0`, both loops have zero iterations, so the function returns immediately

This pattern is proven, portable, and simple. No alternative is needed.

---

## R6: Highway Self-Inclusion File Structure

**Decision**: Add new kernels to existing `spectral_simd.cpp` blocks

**Rationale**: The file structure is:

```
[1] #undef HWY_TARGET_INCLUDE
    #define HWY_TARGET_INCLUDE "krate/dsp/core/spectral_simd.cpp"
    #include "hwy/foreach_target.h"
    #include "hwy/highway.h"
    #include "hwy/contrib/math/math-inl.h"

[2] HWY_BEFORE_NAMESPACE();
    namespace Krate::DSP::HWY_NAMESPACE {
        // Per-target SIMD kernels (compiled once per ISA)
        ComputePolarImpl(...)
        ReconstructCartesianImpl(...)
        ComputePowerSpectrumPffftImpl(...)
        // ** NEW KERNELS GO HERE **
        BatchLog10Impl(...)
        BatchPow10Impl(...)
        BatchWrapPhaseImpl(...)
        BatchWrapPhaseInPlaceImpl(...)
    }
    HWY_AFTER_NAMESPACE();

[3] #if HWY_ONCE
    #include "krate/dsp/core/spectral_simd.h"
    namespace Krate::DSP {
        HWY_EXPORT(ComputePolarImpl);
        HWY_EXPORT(ReconstructCartesianImpl);
        HWY_EXPORT(ComputePowerSpectrumPffftImpl);
        // ** NEW EXPORTS GO HERE **
        HWY_EXPORT(BatchLog10Impl);
        HWY_EXPORT(BatchPow10Impl);
        HWY_EXPORT(BatchWrapPhaseImpl);
        HWY_EXPORT(BatchWrapPhaseInPlaceImpl);

        // Dispatch wrappers
        void computePolarBulk(...) { HWY_DYNAMIC_DISPATCH(ComputePolarImpl)(...); }
        ...
        // ** NEW WRAPPERS GO HERE **
        void batchLog10(...) { HWY_DYNAMIC_DISPATCH(BatchLog10Impl)(...); }
        void batchPow10(...) { HWY_DYNAMIC_DISPATCH(BatchPow10Impl)(...); }
        void batchWrapPhase(const float* input, float* output, size_t count) { HWY_DYNAMIC_DISPATCH(BatchWrapPhaseImpl)(input, output, count); }
        void batchWrapPhase(float* data, size_t count) { HWY_DYNAMIC_DISPATCH(BatchWrapPhaseInPlaceImpl)(data, count); }
    }
    #endif
```

No changes to `HWY_TARGET_INCLUDE`, `foreach_target.h`, or `CMakeLists.txt` are needed.

---

## R7: Performance Benchmark Pattern

**Decision**: Add `[performance]`-tagged TEST_CASE sections to `spectral_simd_test.cpp`

**Rationale**: Reviewed `modulation_engine_perf_test.cpp` (lines 1-60). The pattern uses:

```cpp
TEST_CASE("Performance: description", "[tag][performance]") {
    // Setup
    auto start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < numIterations; ++iter) {
        // Operation under test
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    // Report and assert
}
```

For this spec, each benchmark will:
1. Prepare input data (2049 elements matching FFT size 4096 -> 2049 bins)
2. Time the SIMD batch function over N iterations (e.g., 10000)
3. Time the equivalent scalar loop over N iterations
4. Compute speedup ratio
5. Report both absolute timings and speedup ratio
6. REQUIRE speedup >= 2.0 (SC-001, SC-002, SC-003)

The `[performance]` tag allows excluding from normal CI: `dsp_tests ~"[performance]"`.

---

## R8: `hn::NegMulAdd` Availability

**Decision**: Use `hn::NegMulAdd(n, twoPi, v)` for `v - n * twoPi`

**Rationale**: `hn::NegMulAdd(a, b, c)` computes `c - a * b` which is exactly `v - n * twoPi`. This is a single fused multiply-add instruction on platforms that support FMA (AVX2, NEON), reducing the phase wrapping operation to one instruction per lane.

Verified in Highway ops headers. If unavailable on a specific target, Highway automatically decomposes it to `Sub(c, Mul(a, b))`.

**Fallback**: `hn::Sub(v, hn::Mul(n, twoPi))` achieves the same result with an explicit subtraction, at most 1 extra instruction.

---

## R9: `spectral_simd_test.cpp` and `-ffast-math`

**Decision**: The test file does NOT need to be added to the `-fno-fast-math` exception list

**Rationale**: The `-fno-fast-math` exception list in `dsp/tests/CMakeLists.txt` exists because the VST3 SDK enables `-ffast-math` globally, which breaks `std::isnan()` checks. The `spectral_simd_test.cpp` file does NOT use `std::isnan()` -- it uses `Catch::Approx` for tolerance-based comparison.

The scalar reference computations in the tests (`std::log10`, `std::pow`, `std::round`) may produce slightly different results under `-ffast-math`, but this is acceptable because:
1. The tests compare SIMD vs scalar with generous tolerances (1e-5 for log/pow, 1e-6 for phase)
2. `-ffast-math` differences are typically much smaller than these tolerances
3. The test is NOT checking NaN/Inf detection

If any issue arises on Clang/GCC, the file can be added to the exception list later.
