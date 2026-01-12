# Research: Asymmetric Shaping Functions

**Spec**: 048-asymmetric-shaping | **Date**: 2026-01-12

## Executive Summary

Research confirms that the asymmetric shaping functions have already been implemented as part of spec 047 (Sigmoid Functions). The primary finding is a discrepancy between the spec requirement and current implementation of `withBias()` regarding DC neutrality.

## Research Tasks

### 1. Existing Implementation Analysis

**Question**: What is the current state of asymmetric shaping in the codebase?

**Finding**: The `Asymmetric` namespace exists in `sigmoid.h` with all four specified functions:

| Function | Status | Location |
|----------|--------|----------|
| `tube()` | Implemented | sigmoid.h:296-301 |
| `diode()` | Implemented | sigmoid.h:317-325 |
| `withBias()` | Needs fix | sigmoid.h:350-353 |
| `dualCurve()` | Implemented | sigmoid.h:370-376 |

**Rationale**: Functions were consolidated into sigmoid.h during spec 047 rather than creating a separate asymmetric.h file. This is architecturally sound as both Sigmoid and Asymmetric namespaces contain transfer functions for audio saturation.

### 2. withBias() Formula Analysis

**Question**: Does the current `withBias()` implementation match the spec?

**Finding**: NO - the spec requires DC neutrality via formula:
```
output = saturator(input + bias) - saturator(bias)
```

Current implementation only applies:
```
output = saturator(input + bias)
```

The subtraction of `saturator(bias)` is missing, which means the output will have a DC offset.

**Rationale for spec formula**:
- Adding bias before saturation creates asymmetry
- Subtracting `saturator(bias)` removes the DC component introduced by the bias
- Result: asymmetric transfer function with zero DC offset for zero input

**Example analysis**:
```
bias = 0.3
saturator = tanh

With current implementation:
  x=0 -> tanh(0.3) = 0.291 (DC offset!)

With spec formula:
  x=0 -> tanh(0.3) - tanh(0.3) = 0 (DC neutral)
```

**Action**: Fix `withBias()` to subtract `func(bias)` from output.

### 3. SaturationProcessor Integration

**Question**: How does SaturationProcessor currently use the Asymmetric functions?

**Finding**: `SaturationProcessor` already delegates to `Asymmetric::` functions:
- `saturateTube()` calls `Asymmetric::tube(x)` (line 356)
- `saturateDiode()` calls `Asymmetric::diode(x)` (line 388)

No refactoring needed - the integration is complete.

### 4. Test Coverage Analysis

**Question**: What tests exist for the Asymmetric functions?

**Finding**: `sigmoid_test.cpp` contains tests under tag `[US5]`:
- `Asymmetric::tube()` asymmetry and bounds (lines 345-363)
- `Asymmetric::diode()` behavior and bounds (lines 365-383)
- `Asymmetric::withBias()` basic behavior (lines 385-402)
- `Asymmetric::dualCurve()` polarity behavior (lines 404-421)
- Harmonic character verification (lines 672-703)

**Gap**: No test verifies DC neutrality of `withBias()` because current implementation doesn't have that property.

### 5. Numerical Stability Review

**Question**: How do the asymmetric functions handle edge cases?

**Findings**:

| Function | NaN Handling | Inf Handling | Denormal Handling |
|----------|--------------|--------------|-------------------|
| `tube()` | Propagates (via tanh) | Bounded to +/-1 | OK |
| `diode()` | Need verification | Need verification | OK |
| `withBias()` | Depends on func | Depends on func | OK |
| `dualCurve()` | Propagates (via tanh) | Bounded to +/-1 | OK |

**Action needed**: Add edge case tests for `diode()` with NaN/Inf inputs.

### 6. Performance Considerations

**Question**: Are the asymmetric functions performant enough for real-time use?

**Finding**: All functions use simple operations:
- `tube()`: 3 multiplications + 2 additions + fastTanh
- `diode()`: Branch + exp or division
- `withBias()`: 2 function calls + 1 subtraction
- `dualCurve()`: Branch + fastTanh

All are O(1) per sample with no allocations. Performance is excellent for real-time use.

## Decisions Made

| Decision | Rationale | Alternatives Considered |
|----------|-----------|-------------------------|
| Keep functions in sigmoid.h | Already consolidated there; architecturally coherent | Separate asymmetric.h (more files, no benefit) |
| Fix withBias() formula | Spec requirement for DC neutrality | Document deviation (would violate spec) |
| Add edge case tests | Verify numerical stability claims | Trust implementation (risky) |

## Outstanding Questions

None - all design decisions resolved.

## References

- [Sigmoid Functions Spec (047)](../047-sigmoid-functions/spec.md)
- [Current sigmoid.h](../../dsp/include/krate/dsp/core/sigmoid.h)
- [Sigmoid tests](../../dsp/tests/unit/core/sigmoid_test.cpp)
