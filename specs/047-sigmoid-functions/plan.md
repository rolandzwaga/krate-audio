# Implementation Plan: Sigmoid Transfer Function Library

**Branch**: `047-sigmoid-functions` | **Date**: 2026-01-11 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `/specs/047-sigmoid-functions/spec.md`

## Summary

Create a unified sigmoid transfer function library (`core/sigmoid.h`) consolidating all soft-clipping and saturation functions into Layer 0 of KrateDSP. The library provides 10 symmetric sigmoid functions and 4 asymmetric shaping utilities, reusing existing `FastMath::fastTanh()` and referencing `hardClip()`/`softClip()` from `dsp_utils.h` to avoid duplication. All functions are header-only, `constexpr`/`inline`, `noexcept`, and real-time safe.

## Technical Context

**Language/Version**: C++20 (per Constitution Principle III)
**Primary Dependencies**: 
- `<cmath>` for std::sqrt, std::atan, std::erf
- `core/fast_math.h` for FastMath::fastTanh()
- `core/db_utils.h` for detail::isNaN(), detail::isInf()
- `core/dsp_utils.h` for hardClip() reference
**Storage**: N/A (stateless functions)
**Testing**: Catch2 via CTest (per existing test infrastructure)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: DSP library - Layer 0 core utilities
**Performance Goals**: 
- fastTanh: 2x faster than std::tanh (already verified in existing implementation)
- recipSqrt: 10x faster than std::tanh
**Constraints**: 
- All functions must be branchless or have predictable branching
- No allocations, no exceptions, no I/O
- Must handle NaN, Inf, denormals correctly
**Scale/Scope**: ~10 sigmoid functions + ~4 asymmetric utilities, header-only

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle IX (Layered DSP Architecture):**
- [x] This is Layer 0 (Core) - NO dependencies on higher layers
- [x] Only depends on standard library and other Layer 0 components
- [x] Will be independently testable

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created (verified via grep)

**Required Check - Principle X (DSP Processing Constraints):**
- [x] Functions are pure/stateless - no oversampling needed at this layer
- [x] Oversampling is handled by Layer 1 Oversampler when composing these functions

**Required Check - Principle II (Real-Time Safety):**
- [x] No allocations in any function
- [x] All functions marked noexcept
- [x] No locks, mutexes, or blocking primitives

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Namespaces to be created**: `Krate::DSP::Sigmoid`, `Krate::DSP::Asymmetric`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `namespace Sigmoid` | `grep -r "namespace Sigmoid" dsp/` | No | Create New |
| `namespace Asymmetric` | `grep -r "namespace Asymmetric" dsp/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `tanh` (fast) | `grep -r "fastTanh" dsp/` | Yes | `core/fast_math.h` | **Reuse** - wrap in Sigmoid namespace |
| `softClip` | `grep -r "softClip" dsp/` | Yes | `core/dsp_utils.h` | Reference - different algorithm (rational approx) |
| `hardClip` | `grep -r "hardClip" dsp/` | Yes | `core/dsp_utils.h` | **Wrap** - delegate to existing |
| `atan` | `grep -r "atan\(" dsp/` | No | - | Create New |
| `erf` | `grep -r "erf\(" dsp/` | No | - | Create New |
| `recipSqrt` | `grep -r "recipSqrt" dsp/` | No | - | Create New |
| `tube` | `grep -r "saturateTube" dsp/` | Yes | `processors/saturation_processor.h` | **Extract** to Asymmetric |
| `diode` | `grep -r "saturateDiode" dsp/` | Yes | `processors/saturation_processor.h` | **Extract** to Asymmetric |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `FastMath::fastTanh()` | `core/fast_math.h` | 0 | Wrap as `Sigmoid::tanh()` |
| `detail::isNaN()` | `core/db_utils.h` | 0 | NaN handling in edge cases |
| `detail::isInf()` | `core/db_utils.h` | 0 | Infinity handling in edge cases |
| `hardClip()` | `core/dsp_utils.h` | 0 | Delegate for `Sigmoid::hardClip()` |
| `softClip()` | `core/dsp_utils.h` | 0 | Reference (different algorithm than cubic) |
| `saturateTube()` | `processors/saturation_processor.h` | 2 | Extract algorithm to `Asymmetric::tube()` |
| `saturateDiode()` | `processors/saturation_processor.h` | 2 | Extract algorithm to `Asymmetric::diode()` |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/saturation_processor.h` - Existing saturation algorithms

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned namespaces (`Sigmoid`, `Asymmetric`) and function names are unique. Existing functions (`fastTanh`, `hardClip`, `softClip`) will be reused/wrapped, not duplicated. The tube and diode algorithms will be extracted from `SaturationProcessor` (Layer 2) down to Layer 0, then `SaturationProcessor` will be refactored to call the new Layer 0 functions - this is a proper layering improvement, not duplication.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `FastMath` | `fastTanh` | `[[nodiscard]] constexpr float fastTanh(float x) noexcept` | ✓ |
| `detail` | `isNaN` | `constexpr bool isNaN(float x) noexcept` | ✓ |
| `detail` | `isInf` | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | ✓ |
| `DSP` | `hardClip` | `[[nodiscard]] inline constexpr float hardClip(float sample) noexcept` | ✓ |
| `DSP` | `softClip` | `[[nodiscard]] inline float softClip(float sample) noexcept` | ✓ |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/fast_math.h` - FastMath::fastTanh()
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN(), detail::isInf()
- [x] `dsp/include/krate/dsp/core/dsp_utils.h` - hardClip(), softClip()
- [x] `dsp/include/krate/dsp/processors/saturation_processor.h` - saturateTube(), saturateDiode()

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `FastMath::fastTanh` | Saturates at ±3.5, not ±∞ | Already handles Inf correctly (returns ±1) |
| `detail::isNaN` | Requires `-fno-fast-math` on source file | sigmoid.h tests must compile without fast-math |
| `hardClip` | Only clips to ±1, no threshold param | Wrap with threshold: `hardClip(x / threshold) * threshold` |
| `softClip` | Uses rational approximation, not cubic | Different from `softClipCubic` - both are valid |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

This IS a Layer 0 feature. The following are being ADDED to Layer 0:

### Utilities Being Added to Layer 0

| New Function | Why Add? | Proposed Location | Future Consumers |
|--------------|----------|-------------------|------------------|
| `Sigmoid::tanh()` | Unified API, reuses fastTanh | `core/sigmoid.h` | All saturation processors |
| `Sigmoid::atan()` | Different harmonic character | `core/sigmoid.h` | Distortion effects |
| `Sigmoid::erf()` | Tape-like saturation | `core/sigmoid.h` | Tape emulation |
| `Sigmoid::recipSqrt()` | 10x faster alternative | `core/sigmoid.h` | CPU-critical paths |
| `Asymmetric::tube()` | Even harmonics, extracted from L2 | `core/sigmoid.h` | TubeStage, SaturationProcessor |
| `Asymmetric::diode()` | Asymmetric clipping, extracted from L2 | `core/sigmoid.h` | DiodeClipper, SaturationProcessor |

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 0 - Core Utilities

**Related features at same layer** (from DST-ROADMAP.md):
- `core/asymmetric.h` - **MERGED INTO THIS FEATURE** (Asymmetric namespace included)
- `core/chebyshev.h` - Independent, will use different math
- `core/wavefold_math.h` - Independent, may use Sigmoid for limiting

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `Sigmoid::*` functions | HIGH | All distortion processors (L1-L4) | Primary API |
| `Asymmetric::*` functions | HIGH | TubeStage, FuzzProcessor, AmpChannel | Primary API |
| `SigmoidFunc` type alias | MEDIUM | Composition utilities | Define as `float(*)(float)` |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Merge Asymmetric into sigmoid.h | Single header simplifies includes; asymmetric functions are small |
| Keep Chebyshev separate | Different mathematical domain (polynomials vs sigmoids) |
| Use template for `withBias()` | Allows inlining of sigmoid function for performance; callable with lambdas or function pointers |
| Keep `SigmoidFunc` type alias | Useful for documentation and explicit function pointer storage |

## Project Structure

### Documentation (this feature)

```text
specs/047-sigmoid-functions/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── spec.md              # Feature specification
└── checklists/
    └── requirements.md  # Quality checklist
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── core/
│       ├── sigmoid.h        # NEW: Sigmoid + Asymmetric namespaces
│       ├── fast_math.h      # Existing: FastMath::fastTanh (reused)
│       ├── db_utils.h       # Existing: isNaN, isInf (reused)
│       └── dsp_utils.h      # Existing: hardClip, softClip (referenced)
└── tests/
    └── core/
        └── sigmoid_test.cpp # NEW: Unit tests for all sigmoid functions
```

**Structure Decision**: Single new header file `sigmoid.h` in `core/` containing both `Sigmoid` and `Asymmetric` namespaces. Tests in `tests/core/sigmoid_test.cpp`.

## Complexity Tracking

No constitution violations requiring justification. This is a straightforward Layer 0 utility library.
