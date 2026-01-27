# Data Model: Asymmetric Shaping Functions

**Spec**: 048-asymmetric-shaping | **Date**: 2026-01-12

## Overview

This specification defines pure, stateless functions. There are no entities, no state, and no persistence. This document describes the function signatures and their mathematical relationships.

## Function Signatures

### Asymmetric Namespace

All functions reside in `Krate::DSP::Asymmetric` namespace within `core/sigmoid.h`.

```cpp
namespace Krate::DSP::Asymmetric {

/// Apply DC bias to symmetric function to create asymmetry (FR-001)
/// Formula: output = saturator(input + bias) - saturator(bias)
template <typename Func>
[[nodiscard]] inline float withBias(float x, float bias, Func func) noexcept;

/// Different saturation gains for positive/negative half-waves (FR-002)
[[nodiscard]] inline float dualCurve(float x, float posGain, float negGain) noexcept;

/// Diode-style asymmetric clipping (FR-003)
[[nodiscard]] inline float diode(float x) noexcept;

/// Tube-style asymmetric saturation with even harmonics (FR-004)
[[nodiscard]] inline float tube(float x) noexcept;

}
```

## Parameter Constraints

| Parameter | Type | Valid Range | Notes |
|-----------|------|-------------|-------|
| `x` (input) | `float` | (-inf, +inf) | NaN propagates, Inf bounded |
| `bias` | `float` | (-inf, +inf) | Typical range: [-1.0, 1.0] |
| `posGain` | `float` | [0, +inf) | Zero returns zero for positive x |
| `negGain` | `float` | [0, +inf) | Zero returns zero for negative x |
| `func` | callable | float(float) | Any sigmoid function |

## Transfer Function Definitions

### tube(x)

Mathematical definition:
```
tube(x) = tanh(x + 0.3*x^2 - 0.15*x^3)
```

- Polynomial creates even-order terms (x^2)
- tanh provides soft limiting
- Even harmonics: 2nd, 4th, etc. (due to x^2)
- Odd harmonics: 3rd, 5th, etc. (due to x, x^3, and tanh)

### diode(x)

Mathematical definition:
```
diode(x) = 1 - exp(-1.5*x)        for x >= 0  (forward bias)
diode(x) = x / (1 - 0.5*x)        for x < 0   (reverse bias)
```

- Forward bias: Exponential saturation (soft)
- Reverse bias: Rational function (harder)
- Asymmetry creates even harmonics

### dualCurve(x, posGain, negGain)

Mathematical definition:
```
dualCurve(x) = tanh(x * posGain)  for x >= 0
dualCurve(x) = tanh(x * negGain)  for x < 0
```

- When posGain == negGain: Symmetric (odd harmonics only)
- When posGain != negGain: Asymmetric (even + odd harmonics)

### withBias(x, bias, func)

Mathematical definition:
```
withBias(x, bias, func) = func(x + bias) - func(bias)
```

- Shifts operating point on symmetric curve
- Subtracting func(bias) ensures DC neutrality
- Creates asymmetry from any symmetric sigmoid

## Output Bounds

| Function | Output Range | Notes |
|----------|--------------|-------|
| `tube()` | [-1, 1] | Bounded by tanh |
| `diode()` | [-inf, 1] | Negative unbounded theoretically, but input range limits this |
| `dualCurve()` | [-1, 1] | Bounded by tanh |
| `withBias()` | Depends on func | Typically [-2, 2] for common sigmoids |

## Harmonic Content

| Function | Even Harmonics | Odd Harmonics | Character |
|----------|----------------|---------------|-----------|
| `tube()` | Yes (2nd dominant) | Yes | Warm, rich |
| `diode()` | Yes (subtle) | Yes | Subtle warmth |
| `dualCurve()` | When asymmetric | Yes | Variable |
| `withBias()` | When bias != 0 | Yes | Controlled asymmetry |

## Relationships

```
                    +-----------------+
                    |   Sigmoid::     |
                    |   tanh          |
                    +-----------------+
                           |
              +------------+------------+
              |                         |
    +---------v---------+     +---------v---------+
    | Asymmetric::tube  |     | Asymmetric::      |
    |                   |     | dualCurve         |
    +-------------------+     +-------------------+

    +-----------------------+
    | Asymmetric::withBias  | <-- Takes any sigmoid function
    +-----------------------+

    +-------------------+
    | Asymmetric::diode |  (standalone, uses std::exp)
    +-------------------+
```

## State

None. All functions are pure (output depends only on inputs).

## Thread Safety

All functions are thread-safe by design:
- No mutable state
- No static variables
- All operations are atomic at the CPU level

## Memory

No dynamic allocation. All computation uses stack variables.
