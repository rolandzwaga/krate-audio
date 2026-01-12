# Data Model: Wavefolding Math Library

**Spec**: 050-wavefolding-math | **Date**: 2026-01-12

## Overview

This specification defines pure, stateless functions for wavefolding mathematics. There are no entities, no state, and no persistence. This document describes the function signatures, their mathematical definitions, and their relationships.

## Function Signatures

### WavefoldMath Namespace

All functions reside in `Krate::DSP::WavefoldMath` namespace within `core/wavefold_math.h`.

```cpp
namespace Krate::DSP::WavefoldMath {

/// Principal branch of Lambert W function (FR-001)
/// Solves W * exp(W) = x for W
/// Valid for x >= -1/e ≈ -0.3679
[[nodiscard]] inline float lambertW(float x) noexcept;

/// Fast approximation of Lambert W function (FR-002)
/// Provides reduced accuracy for faster computation
[[nodiscard]] inline float lambertWApprox(float x) noexcept;

/// Triangle wavefolding (FR-003, FR-004, FR-005)
/// Symmetric folding with mirror-like reflection at threshold
[[nodiscard]] inline float triangleFold(float x, float threshold = 1.0f) noexcept;

/// Sine-based wavefolding (FR-006, FR-007, FR-008)
/// Serge synthesizer style folding: sin(gain * x)
[[nodiscard]] inline float sineFold(float x, float gain) noexcept;

}
```

## Parameter Constraints

### Lambert W Function

| Parameter | Type | Valid Range | Notes |
|-----------|------|-------------|-------|
| `x` | `float` | [-0.3679, ∞) | NaN for x < -1/e; NaN input propagates |
| `output` | `float` | Approximately (-∞, ∞) | Grows slowly with x |

### Triangle Fold

| Parameter | Type | Valid Range | Notes |
|-----------|------|-------------|-------|
| `x` | `float` | (-∞, ∞) | Signal to fold |
| `threshold` | `float` | (0, ∞) | Clamped to minimum 0.01f |
| `output` | `float` | [-threshold, threshold] | Always bounded by threshold |

### Sine Fold

| Parameter | Type | Valid Range | Notes |
|-----------|------|-------------|-------|
| `x` | `float` | (-∞, ∞) | Signal to fold |
| `gain` | `float` | [0, ∞) | Negative gains treated as absolute value |
| `output` | `float` | [-1, 1] | Always bounded by sine function |

## Mathematical Definitions

### lambertW(x)

**Mathematical Definition**:
The principal branch of the Lambert W function solves the equation:
```
W(x) * exp(W(x)) = x
```

**Implementation Strategy (per spec FR-001)**:
- Initial estimate: Halley approximation `w0 = x / (1 + x)`
- Newton-Raphson iteration (exactly 4 iterations):
  ```
  w = w - (w * exp(w) - x) / (exp(w) * (w + 1))
  ```
- Return NaN for x < -1/e (approximately -0.3679)

**Domain and Range**:
- Domain: [-1/e, ∞)
- Range: [-1, ∞) for principal branch
- Singularity at x = -1/e (returns -1)

**Properties**:
- W(0) = 0
- W(e) = 1
- W(-1/e) = -1 (branch point)
- Slow growth: W(∞) ∝ ln(∞)

**Use Case**: Foundation for Lockhart wavefolder design; enables precise control over harmonic mapping.

---

### lambertWApprox(x)

**Mathematical Definition**:
Fast approximation of Lambert W using single Newton-Raphson iteration (per spec FR-002).

**Implementation Strategy (per spec FR-002)**:
- Initial estimate: Halley approximation `w0 = x / (1 + x)` (same as lambertW)
- Single Newton-Raphson iteration (1 iteration total):
  ```
  w = w - (w * exp(w) - x) / (exp(w) * (w + 1))
  ```
- Return NaN for x < -1/e (consistent with lambertW)

```cpp
float lambertWApprox(float x) {
  if (x < -0.3679f) return NaN;
  float w = x / (1.0f + x);  // Halley initial estimate
  float ew = std::exp(w);
  return w - (w * ew - x) / (ew * (w + 1.0f));  // Single NR step
}
```

**Accuracy Target**:
- Error < 0.01 relative error for typical audio range [-0.36, 1.0]
- Meets SC-003 requirement

**Performance Target**:
- At least 3x faster than exact 4-iteration `lambertW()` (SC-003)
- Single exp() call vs 4 exp() calls in exact version

---

### triangleFold(x, threshold)

**Mathematical Definition**:
Multi-fold triangle wave using modular arithmetic:
```
triangleFold(x, threshold) =
  Let period = 4.0 * threshold
  Let phase = fmod(|x| + threshold, period)

  if phase < 2*threshold:
    result = phase - threshold
  else:
    result = 3*threshold - phase

  return copysign(result, x)  // preserve input sign for odd symmetry
```

**Simplified Single-Fold** (for small inputs):
```
For |x| <= 3*threshold:
  triangleFold(x, t) =
    x                          if |x| <= threshold
    sign(x) * (2t - |x|)       if threshold < |x| <= 2t
    sign(x) * (|x| - 2t)       if 2t < |x| <= 3t
```

**Symmetry**:
```
triangleFold(-x, threshold) = -triangleFold(x, threshold)
```

**Continuity**:
- Continuous everywhere (including at ±threshold)
- First derivative has discontinuity at fold points (creates spectral content)
- Produces triangle wave transfer function

**Example Transfer Function**:
```
For threshold = 1.0:
  x = 0.5  → output = 0.5    (no folding)
  x = 1.0  → output = 1.0    (at threshold)
  x = 1.5  → output = 0.5    (folded back)
  x = 2.0  → output = 0.0    (at zero crossing)
  x = 2.5  → output = -0.5   (continuing down)
  x = 3.0  → output = -1.0   (at -threshold)
  x = 3.5  → output = -0.5   (folding back up)
  x = 4.0  → output = 0.0    (cycle complete)
  x = 5.0  → output = 1.0    (next cycle)
```

**Harmonic Content**:
- Produces sawtooth-like spectrum
- Dense harmonic series with gradual rolloff
- Spectral shape depends on threshold and input amplitude

---

### sineFold(x, gain)

**Mathematical Definition**:
```
sineFold(x, gain) = sin(gain * x)

Special case: gain ≈ 0 → return x (linear passthrough)
```

**Explanation**:
This is the classic Serge wavefolder transfer function. The sine function creates smooth, musical folding by wrapping signal peaks around ±1.

**Gain Behavior**:
```
gain = 0    → output = x (linear passthrough, special case)
gain = 1    → sin(x), gentle folding for x near ±π/2
gain = π/2  → sin(π*x/2), folds at x = ±1
gain = π    → sin(π*x), folds twice in [-1, 1] range
gain = 2π   → sin(2π*x), multiple folds, rich harmonics
gain > 3π   → dense folding, significant aliasing
```

**Continuous Folding**:
- As gain increases from 0 to ∞, signal transitions smoothly from linear to heavily folded
- No discontinuities or glitches
- Output always bounded to [-1, 1]

**Harmonic Content**:
- Sparse FM-like harmonic structure (Bessel function distribution)
- Characteristic Serge synthesizer sound: musical, smooth folding
- Sideband harmonics appear at integer multiples of the input frequency
- Audible aliasing at high gains (anti-aliasing is processor responsibility)

**Example Behavior**:
```
For sine wave input: x = sin(θ)
sineFold(sin(θ), gain) = sin(gain * sin(θ))

gain = 0      → output = sin(θ) (linear)
gain = 1      → sin(sin(θ)) ≈ 0.84 * sin(θ) (nearly linear)
gain = π/2    → sin(π/2 * sin(θ)), audible folding begins
gain = π      → sin(π * sin(θ)), characteristic Serge tone
gain = 2π     → sin(2π * sin(θ)), aggressive folding
```

## Output Bounds

| Function | Output Range | Conditions |
|----------|--------------|-----------|
| `lambertW(x)` | [-1, ∞) | Monotonically increasing; no upper bound |
| `lambertWApprox(x)` | [-1, ∞) | Approximately matches exact; may vary slightly |
| `triangleFold(x, t)` | [-t, t] | Always bounded by threshold |
| `sineFold(x, g)` | [-1, 1] | Always bounded by sine function |

## Harmonic Characteristics

| Function | Primary Use | Harmonic Character | Spectral Shape |
|----------|-------------|-------------------|-----------------|
| `triangleFold()` | Simple folding, guitar effects | Dense harmonics, sawtooth-like | Gradual rolloff |
| `sineFold()` | Serge synthesis, smooth folding | Sparse, musical harmonics | Smooth, vocoder-like |
| `lambertW()` | Theoretical design, Lockhart folding | Customizable via higher layers | Flexible |

## Signal Flow Relationships

```
Input Signal (audio)
        |
        v
┌─────────────────────────┐
│   WavefoldMath::        │
│   sineFold()            │  (Serge style)
│   triangleFold()        │  (Simple geometric)
│   lambertW()            │  (Advanced design)
└─────────────────────────┘
        |
        v (output - still may contain aliasing)
        |
        | (Higher layers add oversampling/anti-aliasing)
        |
        v
  Processed Audio
```

## Numerical Stability Considerations

### NaN and Infinity Handling

**Input: NaN**
- Output: NaN (propagate through computation)

**Input: +Infinity**
- `lambertW(+inf)` → +inf
- `lambertWApprox(+inf)` → Approximation of +inf behavior
- `triangleFold(+inf, t)` → -1 to +1 range (folded)
- `sineFold(+inf, g)` → [-1, 1] (bounded by sin)

**Input: -Infinity**
- `triangleFold(-inf, t)` → -1 to +1 range
- `sineFold(-inf, g)` → [-1, 1] (bounded by sin)
- `lambertW(-inf)` → NaN (outside domain)

### Denormal Handling

All functions must handle subnormal (denormal) floats gracefully without flushing to zero or triggering performance penalties. Layer 0 functions do not require FTZ/DAZ mode.

### Cross-Platform Consistency

- IEEE 754 compliance ensures consistent behavior across platforms
- Transcendental functions (`std::sin`, `std::exp`, `std::log`) may vary slightly between platforms
- Tolerance specified in SC-002 accounts for platform differences

## State and Thread Safety

### State

None. All functions are pure:
- Output depends only on inputs
- No mutable static variables
- No side effects

### Thread Safety

All functions are thread-safe by design:
- No mutable state
- No synchronization needed
- Safe to call from multiple threads simultaneously

### Memory

No dynamic allocation. All computation uses stack variables and registers.

## Performance Characteristics

### Execution Time (estimated)

| Function | Operations | Relative Time | Notes |
|----------|-----------|--------------|-------|
| `lambertW(x)` | 4 iterations of exp + arithmetic | ~200-400 cycles | 4x Newton-Raphson iteration |
| `lambertWApprox(x)` | 1 iteration of exp + arithmetic | ~50-100 cycles | 1x Newton-Raphson, 3x+ faster |
| `triangleFold(x, t)` | fmod + comparisons + arithmetic | ~5-15 cycles | Uses modular arithmetic |
| `sineFold(x, g)` | 1 multiply, 1 sin | ~50-80 cycles | Dominated by sin() call |

### CPU Budget

Expected Layer 0 overhead per sample:
- Each function call: < 0.01% of 48kHz buffer time (48 samples = 1ms)
- Multiple calls acceptable; see Layer 1+ for aggregation

## Compatibility Notes

### With Sigmoid Library

Wavefolding functions are independent of sigmoid functions but may be composed in higher layers:
```cpp
// Possible combination (in Layer 1+):
float folded = WavefoldMath::triangleFold(input, 1.0f);
float saturated = Sigmoid::tanh(folded);
```

### With Chebyshev Library

Wavefolding may be combined with Chebyshev harmonic shaping:
```cpp
// Possible combination (in Layer 1+):
float folded = WavefoldMath::sineFold(input, gain);
float harmonic = Chebyshev::harmonicMix(folded, weights, 8);
```

### With Asymmetric Shaping

Wavefolding is inherently asymmetric via sine/triangle geometry; additional asymmetric shaping may be applied in processor layer.

## References

- **Lambert W Function**:
  - Corless, R.M., et al. (1996). "On the Lambert W function." Advances in Computational Mathematics, 5(1), 329-359.
  - [SciPy Documentation](https://docs.scipy.org/doc/scipy/reference/generated/scipy.special.lambertw.html)

- **Wavefolding**:
  - [Serge Modular Synthesizer Wavefolder Design](https://www.serge-modules.com/)
  - [Buchla System Documentation](https://www.buchla.com/)

- **Numerical Methods**:
  - [IEEE 754 Standard](https://ieeexplore.ieee.org/document/4610935/)
