# Data Model: TanhADAA Class Design

**Feature**: 056-tanh-adaa
**Date**: 2026-01-13
**Status**: Complete

## Overview

This document defines the class structure, member variables, and method signatures for the TanhADAA anti-aliased tanh saturation primitive.

---

## Entity: TanhADAA

### Description

A Layer 1 DSP primitive that applies tanh saturation with first-order Antiderivative Anti-Aliasing (ADAA). Reduces aliasing artifacts from nonlinear waveshaping without the CPU cost of oversampling.

### Location

```
dsp/include/krate/dsp/primitives/tanh_adaa.h
```

### Namespace

```cpp
namespace Krate {
namespace DSP {
```

---

## Class Definition

### Public Interface

```cpp
class TanhADAA {
public:
    // === Construction (FR-001) ===
    TanhADAA() noexcept;
    TanhADAA(const TanhADAA&) = default;
    TanhADAA& operator=(const TanhADAA&) = default;
    TanhADAA(TanhADAA&&) noexcept = default;
    TanhADAA& operator=(TanhADAA&&) noexcept = default;
    ~TanhADAA() = default;

    // === Configuration (FR-002 to FR-005) ===
    void setDrive(float drive) noexcept;
    void reset() noexcept;

    // === Getters (FR-014) ===
    [[nodiscard]] float getDrive() const noexcept;

    // === Processing (FR-009 to FR-013, FR-018 to FR-020) ===
    [[nodiscard]] float process(float x) noexcept;
    void processBlock(float* buffer, size_t n) noexcept;

    // === Static Antiderivative (FR-006 to FR-008) ===
    [[nodiscard]] static float F1(float x) noexcept;

private:
    static constexpr float kEpsilon = 1e-5f;
    static constexpr float kOverflowThreshold = 20.0f;
    static constexpr float kLn2 = 0.693147180559945f;

    float x1_;                  // Previous input sample
    float drive_;               // Saturation intensity
    bool hasPreviousSample_;    // State flag
};
```

---

## Member Variables

| Member | Type | Default | Description | Requirement |
|--------|------|---------|-------------|-------------|
| `x1_` | `float` | `0.0f` | Previous input sample for ADAA computation | FR-012 |
| `drive_` | `float` | `1.0f` | Saturation intensity (stored as absolute value) | FR-002, FR-003 |
| `hasPreviousSample_` | `bool` | `false` | True after first sample processed | FR-018 |

---

## Constants

| Constant | Value | Description | Requirement |
|----------|-------|-------------|-------------|
| `kEpsilon` | `1e-5f` | Threshold for near-identical sample detection | FR-013 |
| `kOverflowThreshold` | `20.0f` | Switch to asymptotic approximation for F1 | FR-008 |
| `kLn2` | `0.6931...f` | Natural log of 2 for asymptotic formula | FR-008 |

---

## Method Specifications

### Constructor

```cpp
TanhADAA() noexcept;
```

**Behavior**:
- Initializes `drive_` to `1.0f`
- Initializes `x1_` to `0.0f`
- Initializes `hasPreviousSample_` to `false`

**Requirements**: FR-001

---

### setDrive

```cpp
void setDrive(float drive) noexcept;
```

**Parameters**:
- `drive`: Saturation intensity (any float value)

**Behavior**:
- Stores absolute value of drive (negative treated as positive)
- Takes effect immediately on next `process()` call

**Requirements**: FR-002, FR-003

---

### reset

```cpp
void reset() noexcept;
```

**Behavior**:
- Clears `x1_` to `0.0f`
- Clears `hasPreviousSample_` to `false`
- Does NOT change `drive_`

**Post-condition**: Next `process()` call returns naive tanh

**Requirements**: FR-005

---

### getDrive

```cpp
[[nodiscard]] float getDrive() const noexcept;
```

**Returns**: Current drive value (always >= 0)

**Requirements**: FR-014

---

### process

```cpp
[[nodiscard]] float process(float x) noexcept;
```

**Parameters**:
- `x`: Input sample

**Returns**: Anti-aliased tanh-saturated output

**Behavior**:
1. If `drive_ == 0.0f`, return `0.0f` (FR-004)
2. If input is NaN, return NaN (FR-019)
3. If input is Inf, return `+/-1.0f` (FR-020)
4. If no previous sample, return `FastMath::fastTanh(x * drive_)` (FR-018)
5. If `|x - x1_| < epsilon`, return `FastMath::fastTanh((x + x1_) / 2 * drive_)` (FR-013)
6. Otherwise, compute ADAA1: `(F1(x * drive_) - F1(x1_ * drive_)) / (drive_ * (x - x1_))` (FR-012)
7. Update `x1_ = x`

**Real-Time Safety**: O(1), no allocations, noexcept

**Requirements**: FR-009, FR-012, FR-013, FR-018, FR-019, FR-020

---

### processBlock

```cpp
void processBlock(float* buffer, size_t n) noexcept;
```

**Parameters**:
- `buffer`: Audio buffer (modified in-place)
- `n`: Number of samples

**Behavior**: Equivalent to calling `process()` for each sample sequentially.

**Requirements**: FR-010, FR-011

---

### F1 (Static)

```cpp
[[nodiscard]] static float F1(float x) noexcept;
```

**Parameters**:
- `x`: Input value (already scaled by drive)

**Returns**: First antiderivative of tanh at x

**Formula**:
- For `|x| < 20.0`: `F1(x) = std::log(std::cosh(x))`
- For `|x| >= 20.0`: `F1(x) = |x| - ln(2)` (asymptotic)

**Requirements**: FR-006, FR-007, FR-008

---

## State Transitions

```
┌─────────────────┐
│  Constructed    │
│  hasPrevious=F  │
└────────┬────────┘
         │ process(x)
         v
┌─────────────────┐
│   Processing    │◄────────┐
│  hasPrevious=T  │         │
└────────┬────────┘         │
         │ reset()          │ process(x)
         v                  │
┌─────────────────┐         │
│     Reset       ├─────────┘
│  hasPrevious=F  │
└─────────────────┘
```

---

## Validation Rules

| Rule | Condition | Behavior |
|------|-----------|----------|
| Drive zero | `drive_ == 0.0f` | Output is always `0.0f` |
| Negative drive | `drive < 0.0f` | Store absolute value |
| NaN input | `isNaN(x)` | Propagate NaN |
| Inf input | `isInf(x)` | Return `+/-1.0f` |
| Near-identical samples | `|x - x1_| < 1e-5f` | Use fastTanh fallback |
| Large scaled input | `|x * drive| >= 20.0f` | Use asymptotic F1 |

---

## Dependencies

### Layer 0 (Required)

| Component | Header | Usage |
|-----------|--------|-------|
| `FastMath::fastTanh()` | `<krate/dsp/core/fast_math.h>` | Fallback computation |
| `detail::isNaN()` | `<krate/dsp/core/db_utils.h>` | NaN detection |
| `detail::isInf()` | `<krate/dsp/core/db_utils.h>` | Infinity detection |

### Standard Library (Required)

| Function | Header | Usage |
|----------|--------|-------|
| `std::log()` | `<cmath>` | F1 computation |
| `std::cosh()` | `<cmath>` | F1 computation |
| `std::abs()` | `<cmath>` | Absolute value |

---

## Memory Layout

```
TanhADAA object (12 bytes, no padding required):
┌─────────────────┬─────────────────┬──────────────────────┐
│    x1_ (4B)     │   drive_ (4B)   │ hasPreviousSample_   │
│     float       │     float       │    (1B + 3B pad)     │
└─────────────────┴─────────────────┴──────────────────────┘
```

Total size: 12 bytes (fits in cache line, trivially copyable)

---

## Comparison with HardClipADAA

| Aspect | HardClipADAA | TanhADAA |
|--------|--------------|----------|
| ADAA Orders | First + Second | First only |
| Parameter | Threshold | Drive |
| F1 formula | Piecewise polynomial | `ln(cosh(x))` |
| F2 formula | Piecewise polynomial | N/A |
| Overflow handling | None needed | `|x| >= 20` asymptotic |
| Member count | 6 | 3 |
| State flags | 2 (hasPrev, hasValidD1) | 1 (hasPrev) |
