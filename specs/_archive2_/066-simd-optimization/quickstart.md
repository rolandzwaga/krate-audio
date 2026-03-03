# Quickstart: SIMD-Accelerated Math for KrateDSP Spectral Pipeline

**Date**: 2026-02-18 | **Spec**: 066-simd-optimization

## Overview

This spec adds three SIMD-accelerated batch math functions to `spectral_simd.h/.cpp` and integrates two of them (`batchLog10`, `batchPow10`) into `FormantPreserver` to replace scalar transcendental loops.

## Files Modified (4 total)

| File | Changes |
|------|---------|
| `dsp/include/krate/dsp/core/spectral_simd.h` | Add constants + 4 function declarations |
| `dsp/include/krate/dsp/core/spectral_simd.cpp` | Add 4 SIMD kernels + 4 HWY_EXPORT + 4 dispatch wrappers |
| `dsp/include/krate/dsp/processors/formant_preserver.h` | Add include, replace 2 scalar loops with batch calls |
| `dsp/tests/unit/core/spectral_simd_test.cpp` | Add correctness tests + performance benchmarks |

## Files NOT Modified

- `dsp/CMakeLists.txt` -- no new source files
- `dsp/tests/CMakeLists.txt` -- test file already registered
- `dsp/include/krate/dsp/primitives/spectral_utils.h` -- `wrapPhase`/`wrapPhaseFast` untouched

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run correctness tests
build/windows-x64-release/bin/Release/dsp_tests.exe "[spectral_simd]" ~"[performance]"

# Run performance benchmarks (Release only)
build/windows-x64-release/bin/Release/dsp_tests.exe "[spectral_simd][performance]"

# Full regression check
build/windows-x64-release/bin/Release/dsp_tests.exe
```

## Implementation Pattern

All new SIMD functions follow the established Highway self-inclusion pattern in `spectral_simd.cpp`:

1. **SIMD kernel** inside `HWY_BEFORE_NAMESPACE()` / `HWY_AFTER_NAMESPACE()` block
2. **`HWY_EXPORT(KernelName)`** inside `#if HWY_ONCE` block
3. **Dispatch wrapper** calling `HWY_DYNAMIC_DISPATCH(KernelName)(...)`
4. **Public declaration** in `spectral_simd.h` with `noexcept`

## Usage Examples

```cpp
#include <krate/dsp/core/spectral_simd.h>
using namespace Krate::DSP;

// Convert magnitudes to log scale
float mags[2049], logMags[2049];
batchLog10(mags, logMags, 2049);

// Convert back to linear scale
float envelope[2049];
batchPow10(logMags, envelope, 2049);

// Wrap phases to [-pi, pi]
float phases[2049], wrapped[2049];
batchWrapPhase(phases, wrapped, 2049);  // out-of-place
batchWrapPhase(phases, 2049);            // in-place
```

## Key Decisions

1. **No new files**: All code added to existing `spectral_simd.h/.cpp`
2. **No CMake changes**: Existing build targets compile everything
3. **`kMinLogInput` replaces `kMinMagnitude` for log/pow paths**: Single source of truth in `spectral_simd.h`
4. **`logMag_` reused as staging buffer**: No new allocation in FormantPreserver
5. **`batchWrapPhase` is additive-only**: No existing call sites modified (Phase C)
