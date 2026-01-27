# Research: Hard Clip with ADAA

**Feature**: 053-hard-clip-adaa
**Date**: 2026-01-13
**Status**: Complete

## Executive Summary

This research documents the Antiderivative Anti-Aliasing (ADAA) technique for hard clipping, including the mathematical foundations, algorithm details, and implementation considerations for the KrateDSP library.

---

## 1. ADAA Algorithm Fundamentals

### 1.1 Problem Statement

Hard clipping (`std::clamp`) introduces discontinuities in the signal's first derivative at the clipping threshold. These discontinuities create infinite-bandwidth components that fold back into the audible spectrum as aliasing artifacts when processed at finite sample rates.

Traditional solutions:
- **Oversampling (4-8x)**: Effective but CPU-expensive (4-8x processing cost)
- **Bandlimited step functions**: Complex, requires pre-computed tables
- **ADAA**: Analytical technique, minimal CPU overhead (typically 2-5x naive)

### 1.2 ADAA Theory

ADAA exploits the relationship between integration and bandwidth limitation. Instead of computing the waveshaping function directly, we compute its antiderivative and use finite differences.

**Key insight**: The antiderivative of a function has one less discontinuity order than the original. Hard clip has a discontinuous first derivative; its antiderivative (F1) has a continuous first derivative.

**First-Order ADAA Formula**:
```
y[n] = (F1(x[n]) - F1(x[n-1])) / (x[n] - x[n-1])
```

This computes the average value of the waveshaping function over the interval [x[n-1], x[n]], effectively band-limiting the output.

**Second-Order ADAA**: Uses two previous samples and the second antiderivative for even smoother results:
```
y[n] = 2 * (F2(x[n]) - F2(x[n-1]) - (x[n] - x[n-1]) * D1[n-1]) / (x[n] - x[n-1])^2
```

Where D1[n] is the first-order ADAA result.

### 1.3 Literature References

- **Parker et al. (2016)**: "Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution" - DAFx-16
- **Bilbao & Parker (2019)**: "Antiderivative Antialiasing for Memoryless Nonlinearities" - IEEE Signal Processing Letters
- **Esqueda et al. (2017)**: "Aliasing Reduction in Clipping Algorithms" - Applied Sciences

---

## 2. Hard Clip Antiderivatives

### 2.1 Hard Clip Function

For threshold t:
```
f(x, t) = {
    -t,     if x < -t
    x,      if -t <= x <= t
    t,      if x > t
}
```

### 2.2 First Antiderivative F1

Integrating f(x, t) piecewise:

```
F1(x, t) = integral of f(x, t) dx = {
    -t*x - t^2/2,     if x < -t     (linear, slope -t)
    x^2/2,            if -t <= x <= t (parabola)
    t*x - t^2/2,      if x > t       (linear, slope t)
}
```

**Verification**: F1 is continuous at boundaries:
- At x = -t: left = -t*(-t) - t^2/2 = t^2 - t^2/2 = t^2/2; right = (-t)^2/2 = t^2/2 (OK)
- At x = +t: left = t^2/2; right = t*t - t^2/2 = t^2/2 (OK)

F1' = f(x, t) everywhere (derivative matches original function).

### 2.3 Second Antiderivative F2

Integrating F1(x, t) piecewise:

```
F2(x, t) = integral of F1(x, t) dx = {
    -t*x^2/2 - t^2*x/2 - t^3/6,     if x < -t
    x^3/6,                           if -t <= x <= t
    t*x^2/2 - t^2*x/2 + t^3/6,      if x > t
}
```

**Verification**: F2 is continuous at boundaries:
- At x = -t:
  - left = -t*t^2/2 - t^2*(-t)/2 - t^3/6 = -t^3/2 + t^3/2 - t^3/6 = -t^3/6
  - right = (-t)^3/6 = -t^3/6 (OK)
- At x = +t:
  - left = t^3/6
  - right = t*t^2/2 - t^2*t/2 + t^3/6 = t^3/2 - t^3/2 + t^3/6 = t^3/6 (OK)

---

## 3. Numerical Considerations

### 3.1 Division by Zero (Ill-Conditioned Case)

When x[n] == x[n-1], the ADAA formula divides by zero. Using L'Hopital's rule:

```
lim_{dx->0} (F1(x+dx) - F1(x)) / dx = F1'(x) = f(x)
```

So when samples are identical, output = f((x[n] + x[n-1])/2) = f(x[n]).

For the epsilon tolerance (1e-5 per spec), use the midpoint evaluation:
```cpp
if (|x[n] - x[n-1]| < 1e-5f) {
    return Sigmoid::hardClip((x[n] + x[n-1]) / 2.0f, threshold);
}
```

### 3.2 Epsilon Selection Rationale

**Decision**: Use epsilon = 1e-5

**Rationale**:
- Float mantissa has ~7 decimal digits of precision
- 1e-5 leaves 2 digits of headroom for the division result
- Smaller values (1e-6, 1e-7) risk numerical instability near the threshold
- Larger values (1e-4) may engage fallback too often, reducing ADAA benefit

**Alternatives considered**:
- 1e-6: Rejected - too close to float precision limit when dividing
- 1e-4: Rejected - may bypass ADAA for slowly-moving signals

### 3.3 First Sample After Reset

With no previous sample available, use naive hard clip:
```cpp
if (!hasPreviousSample_) {
    hasPreviousSample_ = true;
    x1_ = x;
    return Sigmoid::hardClip(x, threshold_);
}
```

---

## 4. Performance Analysis

### 4.1 Operation Count Comparison

**Naive hard clip** (per sample):
- 2 comparisons (min/max or clamp)
- Total: ~2 ops

**First-order ADAA** (per sample):
- 2 calls to F1(): each ~3-4 ops (comparison + arithmetic)
- 1 subtraction (F1 delta)
- 1 subtraction (x delta)
- 1 division
- 1 epsilon check
- Total: ~12-15 ops

**Performance ratio**: ~6-8x naive (well under 10x budget)

**Second-order ADAA** (per sample):
- 2 calls to F2(): each ~4-5 ops
- 2 calls to F1(): each ~3-4 ops
- Additional arithmetic for D1 update
- Total: ~25-30 ops

**Performance ratio**: ~12-15x naive (may need verification against 10x budget)

### 4.2 Performance Budget Verification

Spec SC-009 requires <= 10x naive hard clip cost.

**Decision**: First-order ADAA clearly meets budget. Second-order is borderline.

**Recommendation**: Implement both orders. Document that first-order is within budget; second-order may exceed 10x in some scenarios but provides significantly better aliasing reduction.

---

## 5. Aliasing Reduction Expectations

### 5.1 First-Order ADAA (SC-001)

Expected aliasing reduction: **12-20 dB** compared to naive hard clip.

The spec requires >= 12 dB for a 5 kHz sine at 44.1 kHz with 4x drive.

### 5.2 Second-Order ADAA (SC-002)

Expected additional reduction: **6-12 dB** beyond first-order.

The spec requires >= 6 dB additional suppression.

### 5.3 Measurement Methodology

Test signal: 5 kHz sine wave at 44.1 kHz sample rate, drive = 4.0 (input amplitude 4.0, threshold 1.0).

Measurement:
1. Process signal through naive hard clip, measure aliased energy above 10 kHz
2. Process same signal through ADAA1, measure aliased energy
3. Process same signal through ADAA2, measure aliased energy
4. Compare ratios in dB

---

## 6. Design Decisions

### 6.1 Reuse Sigmoid::hardClip()

**Decision**: Reuse `Sigmoid::hardClip(x, threshold)` from `core/sigmoid.h` for fallback.

**Rationale**:
- Already exists and tested
- Maintains consistency with existing waveshaping code
- Follows Constitution Principle XIV (ODR Prevention)

### 6.2 Standalone Class (Not Integrated with Waveshaper)

**Decision**: HardClipADAA is a standalone primitive, not integrated with the existing Waveshaper class.

**Rationale**:
- Waveshaper is stateless; ADAA requires state (previous samples)
- Different use cases: Waveshaper for general saturation, ADAA for specifically aliasing-sensitive scenarios
- Allows independent evolution of both components

### 6.3 Header-Only Implementation

**Decision**: Implement as header-only in `primitives/hard_clip_adaa.h`.

**Rationale**:
- Consistent with other Layer 1 primitives (DCBlocker, Waveshaper)
- Enables inlining for performance
- Simpler build integration

---

## 7. State Variables

### 7.1 Required State

| Variable | Type | Purpose |
|----------|------|---------|
| `x1_` | float | Previous input sample (for ADAA1 and ADAA2) |
| `D1_prev_` | float | Previous first-order ADAA result (for ADAA2 only) |
| `threshold_` | float | Clipping threshold |
| `order_` | Order | Selected ADAA order |
| `hasPreviousSample_` | bool | True after first sample processed |

### 7.2 State After reset()

All state variables reset to initial values:
- `x1_ = 0.0f`
- `D1_prev_ = 0.0f`
- `hasPreviousSample_ = false`

---

## 8. Edge Cases Summary

| Case | Handling |
|------|----------|
| First sample after reset | Return naive hard clip |
| Identical consecutive samples | Use midpoint fallback |
| Near-identical samples (< epsilon) | Use midpoint fallback |
| Threshold = 0 | Always return 0.0f |
| Negative threshold | Use absolute value |
| NaN input | Propagate NaN |
| Infinity input | Clamp to threshold |

---

## 9. Conclusion

ADAA is a well-established technique for reducing aliasing in nonlinear waveshaping with minimal CPU overhead. The mathematical foundations are solid, and implementation follows established patterns from DSP literature.

Key implementation points:
1. Antiderivative formulas verified mathematically
2. Epsilon = 1e-5 for numerical stability
3. Reuse existing `Sigmoid::hardClip()` for fallback
4. First-order ADAA meets 10x performance budget
5. Expected aliasing reduction matches spec requirements (12 dB first-order, 6 dB additional for second-order)
