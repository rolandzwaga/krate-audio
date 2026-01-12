# Research: Chebyshev Polynomial Library

**Feature**: 049-chebyshev-polynomials
**Date**: 2026-01-12
**Status**: Complete

## Summary

This document consolidates research findings for the Chebyshev polynomial library implementation. All research has been integrated into `plan.md`.

## Research Questions

### Q1: Chebyshev Polynomial Coefficients T1-T8

**Decision**: Use standard Chebyshev polynomial coefficients of the first kind.

**Polynomials**:
| Order | Polynomial | Expanded Form |
|-------|------------|---------------|
| T1 | T1(x) | x |
| T2 | T2(x) | 2x^2 - 1 |
| T3 | T3(x) | 4x^3 - 3x |
| T4 | T4(x) | 8x^4 - 8x^2 + 1 |
| T5 | T5(x) | 16x^5 - 20x^3 + 5x |
| T6 | T6(x) | 32x^6 - 48x^4 + 18x^2 - 1 |
| T7 | T7(x) | 64x^7 - 112x^5 + 56x^3 - 7x |
| T8 | T8(x) | 128x^8 - 256x^6 + 160x^4 - 32x^2 + 1 |

**Key Properties**:
- T_n(1) = 1 for all n
- T_n(-1) = (-1)^n
- T_n(cos(theta)) = cos(n * theta)
- Recurrence: T_{n+1}(x) = 2x * T_n(x) - T_{n-1}(x)

**Sources**:
- [Wolfram MathWorld - Chebyshev Polynomial of the First Kind](https://mathworld.wolfram.com/ChebyshevPolynomialoftheFirstKind.html)
- [GeeksforGeeks - Chebyshev Polynomials](https://www.geeksforgeeks.org/chebyshev-polynomials/)

### Q2: Horner's Method Implementation

**Decision**: Factor each polynomial into nested multiplication form.

**Rationale**: Horner's method reduces operations from O(n^2) to O(n) and improves numerical stability by reducing floating-point error accumulation.

**Examples of Horner Form**:
```
T3(x) = 4x^3 - 3x
      = x * (4x^2 - 3)

T4(x) = 8x^4 - 8x^2 + 1
      = x^2 * (8x^2 - 8) + 1

T5(x) = 16x^5 - 20x^3 + 5x
      = x * (x^2 * (16x^2 - 20) + 5)

T6(x) = 32x^6 - 48x^4 + 18x^2 - 1
      = x^2 * (x^2 * (32x^2 - 48) + 18) - 1

T7(x) = 64x^7 - 112x^5 + 56x^3 - 7x
      = x * (x^2 * (x^2 * (64x^2 - 112) + 56) - 7)

T8(x) = 128x^8 - 256x^6 + 160x^4 - 32x^2 + 1
      = x^2 * (x^2 * (x^2 * (128x^2 - 256) + 160) - 32) + 1
```

**Sources**:
- [Horner's method - Wikipedia](https://en.wikipedia.org/wiki/Horner's_method)
- [GeeksforGeeks - Horner's Method for Polynomial Evaluation](https://www.geeksforgeeks.org/dsa/horners-method-polynomial-evaluation/)

### Q3: Clenshaw Recurrence Algorithm

**Decision**: Use standard Clenshaw algorithm for weighted Chebyshev sum.

**Algorithm**:
The Clenshaw algorithm evaluates a sum of Chebyshev polynomials:
S = sum_{k=1}^{n} a_k * T_k(x)

Using the recurrence:
- b_{n+1} = 0
- b_n = 0
- b_k = a_k + 2*x*b_{k+1} - b_{k+2}, for k = n-1 down to 0

Final result (for sum starting at T1, not T0):
S = x * b_0 - b_1

**Complexity**: O(n) operations vs O(n^2) for naive evaluation.

**Numerical Stability**: The algorithm is mixed forward-backward stable for x in [-1, 1].

**Sources**:
- [Boost.Math - Chebyshev Polynomials](https://www.boost.org/doc/libs/1_84_0/libs/math/doc/html/math_toolkit/sf_poly/chebyshev.html)
- [Clenshaw algorithm - Wikipedia](https://en.wikipedia.org/wiki/Clenshaw_algorithm)

### Q4: Maximum Harmonic Limit

**Decision**: Cap at 32 harmonics.

**Rationale**:
- 32nd harmonic of 1kHz = 32kHz (exceeds Nyquist for 44.1/48kHz)
- Practical limit for audio applications
- Stack allocation for 32 floats is acceptable

**Alternatives Considered**:
- 64 harmonics: Overkill for audio, wastes stack space
- 16 harmonics: Too restrictive for advanced sound design

### Q5: Edge Case Handling

**Decision**: Follow IEEE 754 conventions and existing Layer 0 patterns.

| Input | Output | Rationale |
|-------|--------|-----------|
| NaN | NaN | IEEE 754 propagation |
| +Inf | Inf or saturated | Depends on polynomial |
| -Inf | +/-Inf or saturated | Depends on polynomial parity |
| n < 0 in Tn | 1.0 (T0) | Clamp to valid range |
| null weights | 0.0 | Safe default |
| numHarmonics = 0 | 0.0 | Empty sum |
| numHarmonics > 32 | Clamp to 32 | Enforce maximum |

## Alternatives Considered

### Alternative 1: Include T0 in harmonicMix

**Rejected**: T0(x) = 1 adds a DC offset. Users should control DC separately.

### Alternative 2: Use std::array for weights in harmonicMix

**Rejected**: Raw pointer + count is more flexible and matches existing API patterns. Users can pass stack arrays, heap arrays, or vectors.

### Alternative 3: SIMD implementation

**Rejected**: Layer 0 is for portable scalar code. SIMD optimization can be added at higher layers or as a separate SIMD-specific implementation.

## References

1. [Wolfram MathWorld - Chebyshev Polynomial of the First Kind](https://mathworld.wolfram.com/ChebyshevPolynomialoftheFirstKind.html)
2. [GeeksforGeeks - Chebyshev Polynomials](https://www.geeksforgeeks.org/chebyshev-polynomials/)
3. [Horner's method - Wikipedia](https://en.wikipedia.org/wiki/Horner's_method)
4. [Clenshaw algorithm - Wikipedia](https://en.wikipedia.org/wiki/Clenshaw_algorithm)
5. [Boost.Math - Chebyshev Polynomials](https://www.boost.org/doc/libs/1_84_0/libs/math/doc/html/math_toolkit/sf_poly/chebyshev.html)
6. [ALGLIB - Chebyshev Polynomials](https://www.alglib.net/specialfunctions/polynomials/chebyshev.php)
