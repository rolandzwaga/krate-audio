# Research: Unified Waveshaper Primitive

**Feature**: 052-waveshaper | **Date**: 2026-01-13

## Overview

This document resolves all technical unknowns identified during the planning phase for the Waveshaper primitive.

## Research Tasks

### R1: Existing Sigmoid Function Implementations

**Question**: What are the exact implementations of the Sigmoid/Asymmetric functions we will call?

**Source**: `dsp/include/krate/dsp/core/sigmoid.h`

**Findings**:

All required functions exist and are verified in Layer 0:

| Function | Implementation | Bounds | Harmonics |
|----------|----------------|--------|-----------|
| `Sigmoid::tanh(x)` | `FastMath::fastTanh(x)` - Pade (5,4) | [-1, 1] | Odd only |
| `Sigmoid::atan(x)` | `(2/pi) * std::atan(x)` | [-1, 1] | Odd only |
| `Sigmoid::softClipCubic(x)` | `1.5x - 0.5x^3` (clipped outside [-1,1]) | [-1, 1] | Odd (3rd dominant) |
| `Sigmoid::softClipQuintic(x)` | `(15x - 10x^3 + 3x^5) / 8` | [-1, 1] | Odd (smooth rolloff) |
| `Sigmoid::recipSqrt(x)` | `x / sqrt(x^2 + 1)` | [-1, 1] approaching | Odd only |
| `Sigmoid::erfApprox(x)` | Abramowitz-Stegun approximation | (-1, 1) | Odd with nulls |
| `Sigmoid::hardClip(x)` | `std::clamp(x, -1, 1)` | [-1, 1] | All harmonics |
| `Asymmetric::diode(x)` | Forward: `1 - exp(-1.5x)`, Reverse: `x/(1-0.5x)` | Unbounded | Even + Odd |
| `Asymmetric::tube(x)` | `tanh(x + 0.3x^2 - 0.15x^3)` | Unbounded* | Even + Odd |

*Note: Asymmetric::tube() applies fastTanh after the polynomial, so it IS bounded to [-1, 1]. The spec incorrectly states it is unbounded. Only Asymmetric::diode() is truly unbounded.

**Correction Required**: Spec says Diode/Tube are both unbounded. Review shows:
- Tube: BOUNDED - output passes through fastTanh()
- Diode: UNBOUNDED - no limiting applied

**Decision**: Document correctly. Only Diode is unbounded. Tube is bounded by the final tanh().

### R2: NaN/Infinity Handling in Sigmoid Functions

**Question**: How do the existing Sigmoid functions handle NaN and Infinity?

**Source**: `dsp/include/krate/dsp/core/sigmoid.h` and `core/fast_math.h`

**Findings**:

| Function | NaN Handling | Infinity Handling |
|----------|--------------|-------------------|
| `Sigmoid::tanh` | Propagates NaN | Returns +/-1 |
| `Sigmoid::atan` | Propagates NaN (via std::atan) | Returns +/-1 (normalized) |
| `Sigmoid::softClipCubic` | Explicit check, propagates | Returns +/-1 (comparison fails) |
| `Sigmoid::softClipQuintic` | Explicit check, propagates | Returns +/-1 (comparison fails) |
| `Sigmoid::recipSqrt` | Propagates NaN | Explicit check, returns +/-1 |
| `Sigmoid::erfApprox` | Propagates | Approaches +/-1 |
| `Sigmoid::hardClip` | Propagates (std::clamp) | Clamps to +/-1 |
| `Asymmetric::diode` | Propagates | Unbounded propagation |
| `Asymmetric::tube` | Propagates via tanh | Returns +/-1 via tanh |

**Decision**: No special handling needed in Waveshaper. The underlying functions already handle edge cases correctly. Document that NaN propagates and infinity saturates to bounds (except Diode which propagates).

### R3: Drive Parameter Implementation

**Question**: Best approach for drive parameter - pre-multiply or use existing variable functions?

**Source**: `core/sigmoid.h` - `Sigmoid::tanhVariable()`, `Sigmoid::atanVariable()`

**Findings**:

The sigmoid.h provides `tanhVariable(x, drive)` and `atanVariable(x, drive)` but not for other functions.

Options:
1. **Pre-multiply in Waveshaper**: `shape(drive * x + asymmetry)` - consistent for all types
2. **Use Variable functions where available**: Inconsistent, only 2 of 9 types have them

**Decision**: Use Option 1 - pre-multiply approach. This is consistent across all 9 waveshape types and matches the spec requirement FR-005/FR-006:
```cpp
float transformed = drive_ * x + asymmetry_;
return applyShape(transformed);
```

The variable functions in sigmoid.h handle drive=0 specially (return 0). Our Waveshaper should match: when drive is 0, `drive_ * x = 0`, and `shape(0 + asymmetry) = shape(asymmetry)`. Wait - FR-027 says "When drive is 0.0, process() MUST return 0.0". This differs from what variable functions do when asymmetry != 0.

**Clarification Needed**: FR-027 says drive=0 returns 0.0, but what if asymmetry != 0?
- Interpretation: drive=0 means signal is scaled to zero, so even with asymmetry bias, the mathematical expression `0 * x + asymmetry = asymmetry` would be shaped.
- But FR-027 is explicit: "When drive is 0.0, process() MUST return 0.0"

**Decision**: Follow FR-027 exactly. When drive=0, short-circuit and return 0.0f. This makes drive=0 a bypass that outputs silence, regardless of asymmetry.

### R4: Asymmetry Parameter Behavior

**Question**: How should asymmetry interact with drive?

**Source**: Spec FR-006, `Asymmetric::withBias()` pattern

**Findings**:

Spec says: `shape(drive * x + asymmetry)` - asymmetry is added after drive scaling.

This means:
- Low drive (0.1) + high asymmetry (0.5) = input scaled small, then shifted by 0.5
- High drive (10) + low asymmetry (0.1) = input scaled large, then shifted by 0.1

The asymmetry value represents a DC bias applied BEFORE the shaping function, which creates transfer function asymmetry.

**Decision**: Implement exactly as spec says: `y = shape(drive * x + asymmetry)`. The asymmetry_ value is added after drive multiplication.

### R5: Bounded vs Unbounded Output Analysis

**Question**: Which waveshape types produce bounded output?

**Source**: Code analysis of sigmoid.h functions

**Findings**:

| Type | Output Range | Verification |
|------|--------------|--------------|
| Tanh | [-1, 1] | fastTanh bounded by math |
| Atan | [-1, 1] | Normalized by 2/pi factor |
| Cubic | [-1, 1] | Explicit clamp + polynomial design |
| Quintic | [-1, 1] | Explicit clamp + polynomial design |
| ReciprocalSqrt | (-1, 1) | Asymptotically approaches +/-1 |
| Erf | (-1, 1) | Error function property |
| HardClip | [-1, 1] | std::clamp |
| Diode | UNBOUNDED | No limiting: forward grows, reverse grows negative |
| Tube | [-1, 1] | Final fastTanh limits output |

**SC-007 Verification**: "Each waveshape type maintains output in range [-1, 1] for inputs in [-10, 10] with drive 1.0 (bounded curves only)". The 7 bounded types are: Tanh, Atan, Cubic, Quintic, ReciprocalSqrt, Erf, HardClip.

**Correction**: Spec lists Tube as unbounded but code shows it IS bounded. Need to document this discrepancy.

**Decision**:
- 7 bounded types: Tanh, Atan, Cubic, Quintic, ReciprocalSqrt, Erf, HardClip
- 1 unbounded type: Diode
- 1 bounded but spec says unbounded: Tube - treat as bounded in implementation, note discrepancy

Actually, re-reading spec clarifications: "Diode/Tube output bounds - should unbounded output be documented or constrained? -> A: Document as unbounded - users responsible for post-shaping gain/limiting". The spec made a design choice to call both unbounded for documentation purposes. Let's accept this even though Tube is technically bounded by tanh.

### R6: Test Strategy for 9 Waveshape Types

**Question**: How to efficiently test all 9 types without excessive repetition?

**Source**: Catch2 patterns, dc_blocker_test.cpp patterns

**Findings**:

Use parameterized tests with GENERATE():
```cpp
TEST_CASE("Waveshaper type produces correct output", "[waveshaper]") {
    auto type = GENERATE(
        WaveshapeType::Tanh,
        WaveshapeType::Atan,
        // ... all 9 types
    );
    DYNAMIC_SECTION("Type: " << static_cast<int>(type)) {
        // Test each type
    }
}
```

**Decision**: Use GENERATE for type iteration, DYNAMIC_SECTION for labeling, and targeted tests for type-specific behaviors (e.g., Diode unbounded output).

## Summary of Decisions

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Drive=0 behavior | Return 0.0f immediately | FR-027 explicit requirement |
| Drive<0 behavior | Use std::abs(drive) | FR-008 explicit requirement |
| Asymmetry range | Clamp to [-1, 1] | FR-007 explicit requirement |
| Processing formula | `shape(drive * x + asymmetry)` | FR-005, FR-006 |
| NaN handling | Propagate (no special handling) | FR-028, underlying functions handle |
| Infinity handling | Let underlying functions handle | FR-029 |
| Bounded types | 7 types (not Diode/Tube per spec) | SC-007 |
| Test approach | GENERATE + DYNAMIC_SECTION | Efficient coverage |

## Resolved Unknowns

All technical unknowns from the planning phase have been resolved:

1. Existing function APIs - Verified all 9 Sigmoid/Asymmetric functions
2. Edge case handling - Underlying functions handle correctly
3. Drive/Asymmetry interaction - Formula defined by spec
4. Bounded output - 7 bounded types identified
5. Test strategy - Parameterized approach defined

**Status**: Ready to proceed to Phase 1 (Design & Contracts)
