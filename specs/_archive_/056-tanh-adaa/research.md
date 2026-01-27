# Research: Tanh ADAA Mathematical Foundation

**Feature**: 056-tanh-adaa
**Date**: 2026-01-13
**Status**: Complete

## Overview

This document captures research into Antiderivative Anti-Aliasing (ADAA) applied to the hyperbolic tangent (tanh) saturation function. The research resolves all "NEEDS CLARIFICATION" items from the spec and establishes the mathematical foundation for implementation.

---

## 1. ADAA Theory for Tanh

### 1.1 First-Order ADAA Algorithm

ADAA works by computing the average value of a nonlinear function between consecutive samples using antiderivatives, rather than point-sampling:

```
y[n] = (F1(x[n]) - F1(x[n-1])) / (x[n] - x[n-1])
```

Where `F1(x)` is the first antiderivative (integral) of the nonlinear function `f(x)`.

For tanh saturation:
- `f(x) = tanh(x)`
- `F1(x) = integral of tanh(x) dx = ln(cosh(x))`

This formula effectively band-limits the waveshaping operation by integrating the discontinuities rather than sampling them directly.

### 1.2 Derivation of F1(x) = ln(cosh(x))

The antiderivative of tanh can be derived as follows:

```
integral tanh(x) dx
= integral sinh(x)/cosh(x) dx
= ln(cosh(x)) + C
```

This can be verified by differentiation:
```
d/dx ln(cosh(x)) = sinh(x)/cosh(x) = tanh(x)
```

### 1.3 Variable Drive Support

When using a drive parameter `d`, the input is scaled before tanh:

```
f(x, d) = tanh(d * x)
```

The antiderivative becomes:
```
F1(x, d) = (1/d) * ln(cosh(d * x))
```

The division by `d` ensures the ADAA formula remains correct:
```
y[n] = (F1(x[n], d) - F1(x[n-1], d)) / (x[n] - x[n-1])
      = (ln(cosh(d*x[n]))/d - ln(cosh(d*x[n-1]))/d) / (x[n] - x[n-1])
      = (ln(cosh(d*x[n])) - ln(cosh(d*x[n-1]))) / (d * (x[n] - x[n-1]))
```

---

## 2. Overflow Prevention

### 2.1 Problem: cosh(x) Overflow

The `cosh(x)` function grows exponentially:
- `cosh(10) = 11,013.2`
- `cosh(20) = 242,582,597.7`
- `cosh(30) = 5.3 * 10^12`
- `cosh(88) ≈ 10^38` (approaches float max)

For large inputs, `cosh(x)` overflows to infinity, making `ln(cosh(x))` undefined.

### 2.2 Solution: Asymptotic Approximation

For large `|x|`, we can use the asymptotic expansion:

```
cosh(x) = (e^x + e^-x) / 2
```

For `x >> 0`:
```
cosh(x) ≈ e^x / 2
ln(cosh(x)) ≈ ln(e^x / 2) = x - ln(2)
```

For `x << 0`:
```
cosh(x) ≈ e^-x / 2
ln(cosh(x)) ≈ ln(e^-x / 2) = -x - ln(2) = |x| - ln(2)
```

**Combined formula for |x| >> 1:**
```
F1(x) ≈ |x| - ln(2)
```

### 2.3 Threshold Selection

**Decision**: Use `|x| >= 20.0` as the threshold for switching to asymptotic approximation.

**Rationale**:
- At `|x| = 20`, `cosh(x) ≈ 2.4 * 10^8`, well within float range
- The relative error of the approximation at `x = 20` is negligible (< 10^-8)
- This matches the conservative approach in the spec
- Leaves significant headroom before float overflow (starts ~88)

**Verification**:
```
At x = 20:
  Exact: ln(cosh(20)) = 19.306852...
  Approx: 20 - ln(2) = 19.306852...
  Error: < 10^-8 (machine epsilon territory)
```

---

## 3. Epsilon Threshold for Near-Identical Samples

### 3.1 Problem: Division by Zero

When `x[n] ≈ x[n-1]`, the ADAA formula:
```
y[n] = (F1(x[n]) - F1(x[n-1])) / (x[n] - x[n-1])
```

Results in `0/0` indeterminate form or numerical instability.

### 3.2 Solution: L'Hopital's Rule Fallback

Using L'Hopital's rule, as `x[n] -> x[n-1]`:
```
lim (F1(x) - F1(x0)) / (x - x0) = F1'(x0) = f(x0) = tanh(x0)
```

For practical implementation, when `|x[n] - x[n-1]| < epsilon`:
```
y[n] = tanh((x[n] + x[n-1]) / 2 * drive)
```

Using the midpoint provides a good approximation and maintains symmetry.

### 3.3 Epsilon Selection

**Decision**: Use `epsilon = 1e-5f`

**Rationale**:
- Matches HardClipADAA epsilon value
- Small enough to not affect normal signal processing
- Large enough to prevent numerical instability
- Well above float epsilon (1.19 * 10^-7)

---

## 4. First Sample Handling

### 4.1 Problem: No Previous Sample

After construction or `reset()`, there is no previous sample `x[n-1]` for the ADAA formula.

### 4.2 Solution: Naive Fallback

**Decision**: Use naive `tanh(x * drive)` for the first sample.

**Rationale**:
- Single-sample discontinuity is acceptable (spec clarification)
- Matches HardClipADAA pattern
- Simple and predictable behavior
- The "click" from one sample is inaudible

**Implementation**:
```cpp
if (!hasPreviousSample_) {
    hasPreviousSample_ = true;
    x1_ = x;
    return FastMath::fastTanh(x * drive_);
}
```

---

## 5. Performance Considerations

### 5.1 Target: <= 10x Naive Tanh

The spec requires ADAA processing to be no more than 10x the cost of naive tanh.

**Benchmark Baseline**: `Sigmoid::tanh()` (wraps `std::tanh`) as specified in SC-008. This is the slower, more accurate variant. `FastMath::fastTanh()` is ~3x faster but used only for fallback cases.

**Cost Analysis per sample:**

| Operation | Naive Tanh | ADAA Tanh |
|-----------|------------|-----------|
| tanh call | 1 | 0 (normal case) |
| std::log | 0 | 2 |
| std::cosh | 0 | 2 |
| Multiplications | 0 | ~4 |
| Divisions | 0 | 1 |
| Comparisons | 0 | 2 |

**Estimated cost**: 6-8x naive (based on HardClipADAA measurements showing ~6-8x for first-order)

### 5.2 fastTanh for Fallback

**Decision**: Use `FastMath::fastTanh()` for epsilon fallback case.

**Rationale**:
- Performance priority over precision (spec clarification)
- `fastTanh` is ~3x faster than `std::tanh`
- Fallback case should be fast to maintain overall performance target
- Accuracy difference (0.05% max error) is inaudible

### 5.3 Potential Optimizations (Not in Scope)

For future consideration if performance target not met:
- Lookup table for `ln(cosh(x))` (trades memory for speed)
- Polynomial approximation of `ln(cosh(x))` in middle range
- SIMD vectorization for block processing

---

## 6. Why First-Order Only (No Second-Order)

### 6.1 Second-Order ADAA Complexity

Second-order ADAA for tanh would require the second antiderivative:
```
F2(x) = integral of ln(cosh(x)) dx
      = x * ln(cosh(x)) - x + 2 * arctan(exp(x)) - ln(2)
```

This involves:
- Multiple transcendental functions (ln, cosh, arctan, exp)
- Higher computational cost (~20-30x naive)
- More complex overflow handling

### 6.2 Diminishing Returns

From HardClipADAA learnings:
- First-order ADAA provides ~6-8 dB aliasing reduction
- Second-order only adds ~3-6 dB more for hard clipping
- For smooth functions like tanh, first-order is already very effective
- The cost/benefit ratio favors first-order only

### 6.3 Decision

**First-order only** - Provides optimal quality/performance ratio for smooth tanh function.

---

## 7. Aliasing Measurement Methodology

### 7.1 Test Configuration

From spec SC-001:
- Test frequency: 5kHz
- Sample rate: 44.1kHz
- Drive: 4x (causes significant saturation)
- FFT size: 2048
- Measurement: Power above 4th harmonic (aliased region)

### 7.2 Expected Results

Based on HardClipADAA measurements:
- Naive tanh aliasing: ~-30 to -40 dB (varies with drive)
- ADAA tanh aliasing: ~-35 to -50 dB (estimated)
- Expected reduction: >= 3 dB (spec minimum), likely 5-8 dB

### 7.3 Test Infrastructure

Reuse existing `tests/test_helpers/spectral_analysis.h`:
- `measureAliasing()` function
- `compareAliasing()` for A/B testing
- `AliasingTestConfig` for test parameters

---

## 8. Summary of Decisions

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Antiderivative F1(x) | `ln(cosh(x))` | Mathematical derivation |
| Overflow threshold | `|x| >= 20.0` | Conservative, verified accuracy |
| Asymptotic formula | `|x| - ln(2)` | Prevents cosh overflow |
| Epsilon threshold | `1e-5f` | Matches HardClipADAA pattern |
| First sample | Naive tanh | Single-sample discontinuity acceptable |
| Performance fallback | `FastMath::fastTanh()` | Performance priority |
| ADAA order | First-order only | Optimal quality/performance ratio |
| Drive handling | `F1(x, d) = ln(cosh(d*x))/d` | Correct ADAA formula |

---

## References

1. Parker et al., "Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution", DAFx-2016
2. HardClipADAA implementation: `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
3. Spec clarifications: `specs/056-tanh-adaa/spec.md` Section "Clarifications"
4. DST-ROADMAP.md: Priority 2, Item #8
