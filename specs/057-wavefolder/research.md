# Research: Wavefolder Primitive

**Spec**: 057-wavefolder | **Date**: 2026-01-13 | **Status**: Complete

## Research Tasks

### 1. Wavefolding Algorithm Review

**Question**: What are the mathematical properties and implementation requirements for each wavefolding algorithm?

#### Triangle Fold

**Decision**: Delegate to `WavefoldMath::triangleFold(x, threshold)` where `threshold = 1.0/foldAmount`

**Rationale**:
- Already implemented in `wavefold_math.h` with full specification compliance
- Uses modular arithmetic for multi-fold support
- Handles arbitrary input magnitudes without diverging
- Produces output bounded to `[-threshold, threshold]`
- Has odd symmetry: `f(-x) = -f(x)`

**API Contract** (verified from source):
```cpp
[[nodiscard]] inline float triangleFold(float x, float threshold = 1.0f) noexcept;
// - threshold clamped to minimum 0.01f (kMinThreshold)
// - NaN inputs propagated
// - Uses fmod for multi-fold support
```

**Infinity Handling**:
- Decision: Return +/-1.0 (saturate)
- Implementation: Check for infinity before delegating, return `sign(x) * threshold`

#### Sine Fold

**Decision**: Delegate to `WavefoldMath::sineFold(x, gain)` where `gain = foldAmount`

**Rationale**:
- Already implemented in `wavefold_math.h`
- Classic Serge wavefolder transfer function: `sin(gain * x)`
- Output always bounded to `[-1, 1]` (sine function range)
- Returns input unchanged when `gain < 0.001` (linear passthrough)

**API Contract** (verified from source):
```cpp
[[nodiscard]] inline float sineFold(float x, float gain) noexcept;
// - Negative gain treated as |gain|
// - When gain < kSineFoldGainEpsilon (0.001f), returns x
// - NaN inputs propagated
```

**Infinity Handling**:
- Decision: Return +/-1.0 (saturate)
- Implementation: Check for infinity before delegating, return `sign(x) * 1.0f`

#### Lockhart Fold

**Decision**: Implement `tanh(lambertW(exp(input * foldAmount)))` using `WavefoldMath::lambertW()`

**Rationale**:
- Classic Lockhart circuit approximation with soft limiting
- Lambert W function already implemented with 4 Newton-Raphson iterations (~200-400 cycles)
- Produces rich even and odd harmonics with characteristic spectral nulls
- Formula provides soft saturation characteristics distinct from hard clipping

**API Contract** (verified from source):
```cpp
[[nodiscard]] inline float lambertW(float x) noexcept;
// - Valid for x >= kLambertWDomainMin (-1/e ~ -0.3679)
// - Returns NaN for x < kLambertWDomainMin
// - Returns NaN for x = NaN (propagation)
// - Returns +inf for x = +inf, NaN for x = -inf
// - W(0) = 0, W(e) = 1
```

**Infinity Handling**:
- Decision: Return NaN per domain constraints
- Rationale: `exp(inf)` is inf, `lambertW(inf)` returns inf, `tanh(inf) = 1.0`
- However, spec clarification says Lockhart returns NaN for infinity inputs
- Implementation: Check for infinity before processing, return NaN

**foldAmount=0 Behavior**:
- `exp(input * 0) = exp(0) = 1` for any input
- `lambertW(1) ~ 0.567`
- `tanh(0.567) ~ 0.514`
- Decision: Follow formula mathematically, returning ~0.514 (the result of `tanh(lambertW(1))`)
- Note: `W(1) ≈ 0.567`, but after tanh the actual output is `tanh(0.567) ≈ 0.514`

### 2. foldAmount Parameter Mapping

**Question**: How should foldAmount map to each algorithm's native parameter?

| Type | Native Parameter | Mapping | Valid Range |
|------|-----------------|---------|-------------|
| Triangle | threshold | `threshold = 1.0/foldAmount` | foldAmount in [0.1, 10.0] -> threshold in [0.1, 10.0] |
| Sine | gain | `gain = foldAmount` | foldAmount in [0.0, 10.0] |
| Lockhart | foldAmount | Direct multiply: `input * foldAmount` | foldAmount in [0.0, 10.0] |

**Decision**: Clamp foldAmount to [0.0, 10.0] for all types

**Rationale**:
- Prevents numerical overflow in exp() for Lockhart (exp(10) ~ 22026, exp(20) ~ 485M)
- Provides sufficient fold intensity for all creative applications
- For Triangle: foldAmount=0 maps to threshold=inf (clamped to kMinThreshold=0.01 internally)

### 3. Edge Case Handling Summary

| Input | Triangle | Sine | Lockhart |
|-------|----------|------|----------|
| NaN | Propagate | Propagate | Propagate |
| +Infinity | +threshold (saturate) | +1.0 (saturate) | NaN |
| -Infinity | -threshold (saturate) | -1.0 (saturate) | NaN |
| foldAmount=0 | 0 (degenerate) | x (passthrough) | ~0.514 (tanh(W(1))) |
| Negative foldAmount | Use abs(foldAmount) | Use abs(foldAmount) | Use abs(foldAmount) |
| Very large input (1000.0) | Folds correctly via fmod | Wraps within [-1,1] | tanh bounds to ~1.0 |

### 4. Performance Analysis

**Per-sample CPU budget**: < 0.1% CPU for 512-sample buffer at 44.1kHz = ~50 microseconds

| Operation | Estimated Cycles | Notes |
|-----------|------------------|-------|
| Triangle fold | 5-15 | fmod + arithmetic |
| Sine fold | 50-80 | sin() call |
| Lockhart fold | 400-600 | 4x exp() + log() + arithmetic + tanh |

**Decision**: Triangle and Sine meet < 50 microseconds budget (SC-003); Lockhart has separate budget (SC-003a)
- Triangle: 512 * 15 cycles = 7680 cycles ~ 2 us @ 3 GHz ✓
- Sine: 512 * 80 cycles = 40960 cycles ~ 14 us @ 3 GHz ✓
- Lockhart: 512 * 600 cycles = 307200 cycles ~ 102 us @ 3 GHz (separate SC-003a: < 150 us)

**Mitigation for Lockhart**:
- Original SC-003 target (< 50 microseconds) exceeded by Lockhart at full accuracy
- Resolution: Added SC-003a with separate Lockhart budget (< 150 microseconds)
- Decision: Use accurate lambertW() per clarification, Lockhart has relaxed performance target

### 5. Class Design Pattern

**Question**: What design pattern should Wavefolder follow?

**Decision**: Follow Waveshaper pattern exactly

**Rationale**:
- Waveshaper (052) is a sibling primitive at Layer 1 with identical interface requirements
- Same enum + switch pattern for type selection
- Same stateless process() method signature
- Same processBlock() implementation pattern
- Trivially copyable for per-channel instances

**Pattern from Waveshaper**:
```cpp
enum class WaveshapeType : uint8_t { ... };

class Waveshaper {
    void setType(WaveshapeType type) noexcept;
    void setDrive(float drive) noexcept;
    [[nodiscard]] float process(float x) const noexcept;
    void processBlock(float* buffer, size_t n) noexcept;  // const removed for in-place
private:
    WaveshapeType type_ = WaveshapeType::Tanh;
    float drive_ = 1.0f;
};
```

### 6. Existing Components Verified

| Component | Location | Verified API |
|-----------|----------|--------------|
| `WavefoldMath::triangleFold` | core/wavefold_math.h | `float triangleFold(float x, float threshold = 1.0f)` |
| `WavefoldMath::sineFold` | core/wavefold_math.h | `float sineFold(float x, float gain)` |
| `WavefoldMath::lambertW` | core/wavefold_math.h | `float lambertW(float x)` |
| `detail::isNaN` | core/db_utils.h | `bool isNaN(float x)` |
| `detail::isInf` | core/db_utils.h | `bool isInf(float x)` |
| `FastMath::fastTanh` | core/fast_math.h | `float fastTanh(float x)` |

## Alternatives Considered

### Lambert W Implementation Choice

| Option | Cycles | Accuracy | Chosen? |
|--------|--------|----------|---------|
| `lambertW()` (4 iterations) | 200-400 | < 0.001 abs | Yes |
| `lambertWApprox()` (1 iteration) | 50-100 | < 0.01 rel | No |

**Decision**: Use accurate `lambertW()` per clarification - accuracy prioritized over lookup table approximations.

### foldAmount Upper Bound

| Option | Rationale | Chosen? |
|--------|-----------|---------|
| 10.0 | Prevents overflow, sufficient intensity | Yes |
| 20.0 | More range but exp(20*x) overflows easily | No |
| Unlimited | Numerical instability | No |

**Decision**: Clamp to 10.0 per clarification.

## Summary

All NEEDS CLARIFICATION items resolved:
1. Lockhart formula: `tanh(lambertW(exp(input * foldAmount)))` - confirmed
2. Lambert W implementation: Use accurate `lambertW()` - confirmed
3. foldAmount bounds: Clamp to [0.0, 10.0] - confirmed
4. Infinity handling: Triangle/Sine saturate to bounded output; Lockhart returns NaN - confirmed
5. foldAmount=0 for Lockhart: Follow formula (~0.514 after tanh) - confirmed

Ready for Phase 1: Design & Contracts.
