# Research: Sigmoid Transfer Function Library

**Feature**: 047-sigmoid-functions
**Date**: 2026-01-11

## Overview

This document consolidates research on sigmoid transfer functions for audio distortion, including algorithm selection, performance characteristics, and harmonic properties.

---

## 1. Core Sigmoid Functions

### 1.1 Hyperbolic Tangent (tanh)

**Decision**: Reuse existing `FastMath::fastTanh()` implementation

**Rationale**: 
- Already implemented and benchmarked at ~3x faster than `std::tanh`
- Uses Padé (5,4) approximant with <0.05% max error for |x| < 3.5
- Handles NaN, Inf, and saturation at ±3.5 correctly

**Alternatives Considered**:
- `std::tanh()`: Too slow for real-time (~3x slower)
- Lookup table: Memory overhead, cache misses at high sample rates
- SIMD intrinsics: Platform-specific, fastTanh already fast enough

**Mathematical Definition**:
```
tanh(x) = (e^x - e^-x) / (e^x + e^-x)
```

**Harmonic Character**: Symmetric curve produces only odd harmonics (3rd, 5th, 7th...)

---

### 1.2 Arctangent (atan)

**Decision**: Implement using `std::atan()` with normalization

**Rationale**:
- `std::atan()` is well-optimized on modern compilers
- Simple normalization: `(2/π) * atan(x)` maps output to [-1, 1]
- Different harmonic rolloff than tanh - slightly brighter character

**Implementation**:
```cpp
inline float atan(float x) noexcept {
    return (2.0f / kPi) * std::atan(x);
}
```

**Variable Drive**:
```cpp
inline float atanVariable(float x, float drive) noexcept {
    if (drive <= 0.0f) return 0.0f;
    return (2.0f / kPi) * std::atan(drive * x);
}
```

**Alternatives Considered**:
- Polynomial approximation: Not faster than std::atan on modern CPUs
- Lookup table: Unnecessary complexity

---

### 1.3 Cubic Soft Clip

**Decision**: Implement polynomial `f(x) = 1.5x - 0.5x³` with clamping

**Rationale**:
- Classic waveshaping formula from computer music literature
- f'(±1) = 0, ensuring smooth transition to clipping
- Very fast (no transcendentals)

**Implementation**:
```cpp
constexpr float softClipCubic(float x) noexcept {
    if (x <= -1.0f) return -1.0f;
    if (x >= 1.0f) return 1.0f;
    return 1.5f * x - 0.5f * x * x * x;
}
```

**Note**: This differs from `dsp_utils.h::softClip()` which uses a rational approximation `x * (27 + x²) / (27 + 9x²)`. Both are valid; cubic is simpler, rational is closer to tanh shape.

---

### 1.4 Quintic Soft Clip

**Decision**: Implement 5th-order polynomial for smoother transition

**Rationale**:
- Smoother knee than cubic
- Still very fast (polynomial evaluation)
- f'(±1) = 0 and f''(±1) = 0 for second-derivative continuity

**Implementation**:
```cpp
constexpr float softClipQuintic(float x) noexcept {
    if (x <= -1.0f) return -1.0f;
    if (x >= 1.0f) return 1.0f;
    // f(x) = (15x - 10x³ + 3x⁵) / 8
    float x2 = x * x;
    float x3 = x2 * x;
    float x5 = x3 * x2;
    return (15.0f * x - 10.0f * x3 + 3.0f * x5) * 0.125f;
}
```

---

### 1.5 Reciprocal Square Root (Fast Alternative)

**Decision**: Implement `x / sqrt(x² + 1)` as ultra-fast tanh alternative

**Rationale**:
- ~10-13x faster than tanh (vectorizes well, no transcendentals except sqrt)
- Similar shape to tanh
- Algebraic formula allows compiler optimization

**Implementation**:
```cpp
inline float recipSqrt(float x) noexcept {
    return x / std::sqrt(x * x + 1.0f);
}
// Or using fast inverse sqrt:
inline float recipSqrtFast(float x) noexcept {
    return x * rsqrtf(x * x + 1.0f);
}
```

**Alternatives Considered**:
- Quake fast inverse sqrt: Not portable, modern sqrt is fast
- SIMD rsqrt: Platform-specific, leave to compiler

---

### 1.6 Error Function (erf)

**Decision**: Provide both `std::erf()` wrapper and fast approximation

**Rationale**:
- `std::erf()` provides accurate reference
- Fast approximation for real-time when slight inaccuracy acceptable
- Unique spectral character with odd nulls - desirable for tape emulation

**Implementation (Approximation)**:
Using Abramowitz and Stegun approximation (max error 5×10⁻⁴):
```cpp
inline float erfApprox(float x) noexcept {
    // Handle sign
    float sign = (x >= 0.0f) ? 1.0f : -1.0f;
    x = std::abs(x);
    
    // Constants
    constexpr float a1 = 0.278393f;
    constexpr float a2 = 0.230389f;
    constexpr float a3 = 0.000972f;
    constexpr float a4 = 0.078108f;
    
    float t = 1.0f / (1.0f + 0.5f * x);
    float t2 = t * t;
    float t3 = t2 * t;
    float t4 = t3 * t;
    
    float result = 1.0f - t * std::exp(-x*x - a1*t - a2*t2 - a3*t3 - a4*t4);
    return sign * result;
}
```

**Note**: For constexpr version, would need custom exp() - use `detail::constexprExp()` from db_utils.h.

---

### 1.7 Hard Clip

**Decision**: Wrap existing `hardClip()` from dsp_utils.h with optional threshold

**Rationale**:
- Avoid code duplication (Constitution Principle XIV)
- Add threshold parameter for flexibility

**Implementation**:
```cpp
inline constexpr float hardClip(float x, float threshold = 1.0f) noexcept {
    return std::clamp(x, -threshold, threshold);
}
```

---

## 2. Asymmetric Shaping Functions

### 2.1 Bias-Based Asymmetry

**Decision**: Apply DC bias before symmetric sigmoid, then remove DC after

**Rationale**:
- Simplest way to create even harmonics from any symmetric function
- DC offset creates asymmetry → even harmonics appear
- Must DC-block after to remove offset

**Implementation**:
```cpp
template<typename SigmoidFunc>
inline float withBias(float x, float bias, SigmoidFunc func) noexcept {
    return func(x + bias);
    // Note: Caller must DC-block the output
}
```

---

### 2.2 Tube Polynomial (Extracted from SaturationProcessor)

**Decision**: Extract existing `saturateTube()` algorithm

**Rationale**:
- Already implemented and tested in SaturationProcessor
- Classic tube-style asymmetric transfer function
- x² term introduces even harmonics

**Implementation** (from SaturationProcessor):
```cpp
inline float tube(float x) noexcept {
    // y = x + 0.3*x² - 0.15*x³, then soft limited via tanh
    const float x2 = x * x;
    const float x3 = x2 * x;
    const float asymmetric = x + 0.3f * x2 - 0.15f * x3;
    return FastMath::fastTanh(asymmetric);
}
```

---

### 2.3 Diode Curve (Extracted from SaturationProcessor)

**Decision**: Extract existing `saturateDiode()` algorithm

**Rationale**:
- Already implemented and tested in SaturationProcessor
- Models real diode conduction: soft forward bias, harder reverse bias
- Creates even harmonics via asymmetry

**Implementation** (from SaturationProcessor):
```cpp
inline float diode(float x) noexcept {
    if (x >= 0.0f) {
        // Forward bias: soft exponential saturation
        return 1.0f - std::exp(-x * 1.5f);
    } else {
        // Reverse bias: harder, more linear with soft limit
        return x / (1.0f - 0.5f * x);
    }
}
```

---

### 2.4 Dual Curve (Different Gains per Polarity)

**Decision**: Apply different saturation gains to positive/negative half-waves

**Rationale**:
- Flexible asymmetry control
- Can create subtle to extreme asymmetry
- Common in germanium fuzz modeling

**Implementation**:
```cpp
inline float dualCurve(float x, float posGain, float negGain) noexcept {
    if (x >= 0.0f) {
        return FastMath::fastTanh(x * posGain);
    } else {
        return FastMath::fastTanh(x * negGain);
    }
}
```

---

## 3. Performance Benchmarks (Expected)

Based on research and existing fastTanh benchmarks:

| Function | Relative Speed | Notes |
|----------|---------------|-------|
| `recipSqrt` | 10-13x vs std::tanh | Fastest, vectorizes well |
| `softClipCubic` | 8-10x vs std::tanh | No transcendentals |
| `softClipQuintic` | 6-8x vs std::tanh | More ops than cubic |
| `fastTanh` | 3x vs std::tanh | Already verified |
| `atan` | 1.5-2x vs std::tanh | Modern compilers optimize well |
| `erf` | 0.8-1x vs std::tanh | Similar complexity |
| `erfApprox` | 2-3x vs std::tanh | Polynomial approximation |

---

## 4. Harmonic Characteristics Summary

| Function | Symmetry | Harmonics | Character |
|----------|----------|-----------|-----------|
| tanh | Symmetric | Odd only | Warm, smooth |
| atan | Symmetric | Odd only | Slightly brighter than tanh |
| softClipCubic | Symmetric | Odd only | Clean, polynomial |
| softClipQuintic | Symmetric | Odd only | Smoother than cubic |
| recipSqrt | Symmetric | Odd only | Similar to tanh |
| erf | Symmetric | Odd with nulls | Tape-like, unique spectrum |
| tube | Asymmetric | Odd + Even | Rich, warm, 2nd harmonic |
| diode | Asymmetric | Odd + Even | Subtle warmth |
| dualCurve | Asymmetric | Odd + Even | Variable character |
| withBias | Depends | Adds even | DC offset technique |

---

## 5. Edge Case Handling

All functions must handle:

| Input | Expected Output | Implementation |
|-------|-----------------|----------------|
| NaN | NaN (propagate) | Check with `detail::isNaN()` |
| +Inf | +1.0 | Check with `detail::isInf()` |
| -Inf | -1.0 | Check with `detail::isInf()` |
| Denormal | Process normally | No special handling (let FTZ handle) |
| drive = 0 | 0.0 | Early return |
| drive < 0 | Treat as positive | Use `std::abs(drive)` |

---

## 6. Refactoring Impact

### SaturationProcessor Changes

After implementing `sigmoid.h`, `SaturationProcessor` should be refactored to:

```cpp
// Before (inline implementations)
float saturateTape(float x) { return std::tanh(x); }
float saturateTube(float x) { /* polynomial */ }

// After (delegate to Layer 0)
float saturateTape(float x) { return Sigmoid::tanh(x); }
float saturateTube(float x) { return Asymmetric::tube(x); }
```

This maintains exact behavior while:
1. Reducing code duplication
2. Improving testability
3. Enabling reuse across all distortion processors

---

## 7. References

- DSP-DISTORTION-TECHNIQUES.md - Comprehensive distortion research
- DST-ROADMAP.md - Distortion implementation roadmap
- fast_math.h - Existing fastTanh implementation
- saturation_processor.h - Existing saturation algorithms
- [Raph Levien - A Few of My Favorite Sigmoids](https://raphlinus.github.io/audio/2018/09/05/sigmoid.html)
- [Musicdsp.org - Variable Hardness Clipping](https://www.musicdsp.org/en/latest/Effects/104-variable-hardness-clipping-function.html)
