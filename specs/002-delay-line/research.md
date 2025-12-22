# Research: Delay Line DSP Primitive

**Feature**: 002-delay-line
**Date**: 2025-12-22
**Status**: Complete (minimal research needed)

## Overview

The delay line is a well-established DSP primitive with textbook implementations. This research documents the standard algorithms and design decisions.

## Circular Buffer Implementation

### Decision: Power-of-2 Buffer Size with Bitwise Wrap

**Rationale**: Using power-of-2 buffer sizes enables O(1) index wrapping with bitwise AND instead of modulo division.

```cpp
// Modulo (slow, uses division)
writeIndex_ = (writeIndex_ + 1) % bufferSize_;

// Bitwise AND (fast, no division)
writeIndex_ = (writeIndex_ + 1) & mask_;  // mask_ = bufferSize_ - 1
```

**Alternatives Considered**:
- Standard modulo: Rejected due to division cost in tight loops
- Conditional wrap: Rejected due to branching overhead

**Trade-off**: Buffer may be up to 2x larger than strictly needed (e.g., 10s at 192kHz = 1,920,000 samples → rounds up to 2,097,152 = 2^21). Additional memory cost (~700KB) is acceptable for performance gain.

## Linear Interpolation

### Decision: Standard Two-Point Linear Interpolation

**Formula**:
```cpp
float readLinear(float delaySamples) noexcept {
    size_t index0 = static_cast<size_t>(delaySamples);
    size_t index1 = index0 + 1;
    float frac = delaySamples - static_cast<float>(index0);

    float y0 = buffer_[(writeIndex_ - index0) & mask_];
    float y1 = buffer_[(writeIndex_ - index1) & mask_];

    return y0 + frac * (y1 - y0);
}
```

**Rationale**: Simple, efficient, no phase distortion. Suitable for LFO-modulated delays per Constitution Principle X.

**Alternatives Considered**:
- No interpolation: Rejected - causes audible stepping when delay time changes
- Cubic interpolation: Not needed for Layer 1; can be added in future iteration
- Sinc interpolation: Overkill for delay line; needed for pitch shifting (Layer 2)

## Allpass Interpolation

### Decision: First-Order Allpass Filter

**Formula**:
```cpp
float readAllpass(float delaySamples) noexcept {
    size_t index0 = static_cast<size_t>(delaySamples);
    size_t index1 = index0 + 1;
    float frac = delaySamples - static_cast<float>(index0);

    // Coefficient: (1 - frac) / (1 + frac)
    float a = (1.0f - frac) / (1.0f + frac);

    float x0 = buffer_[(writeIndex_ - index0) & mask_];
    float x1 = buffer_[(writeIndex_ - index1) & mask_];

    // y[n] = x[n-D] + a * (y[n-1] - x[n-D+1])
    float output = x0 + a * (allpassState_ - x1);
    allpassState_ = output;

    return output;
}
```

**Rationale**: Preserves unity gain at all frequencies (flat magnitude response), essential for feedback loops where amplitude distortion accumulates.

**Alternatives Considered**:
- Linear interpolation in feedback: Rejected - causes high-frequency rolloff that accumulates in feedback
- Higher-order allpass: Not needed for delay line; first-order is sufficient

**Warning**: Per Constitution Principle X, allpass interpolation MUST NOT be used for modulated delays (LFO-controlled) as it causes artifacts when the delay time changes rapidly.

## Buffer Sizing

### Decision: Round Up to Next Power of 2

**Implementation**:
```cpp
static constexpr size_t nextPowerOf2(size_t n) noexcept {
    --n;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;  // For 64-bit size_t
    return ++n;
}
```

**Rationale**: Standard bit manipulation technique, constexpr for compile-time evaluation.

## Memory Allocation Strategy

### Decision: std::vector with Reserve in prepare()

**Implementation**:
```cpp
void prepare(double sampleRate, float maxDelaySeconds) noexcept {
    size_t minSize = static_cast<size_t>(sampleRate * maxDelaySeconds) + 1;
    size_t bufferSize = nextPowerOf2(minSize);

    buffer_.resize(bufferSize);
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);

    mask_ = bufferSize - 1;
    sampleRate_ = sampleRate;
}
```

**Rationale**:
- `std::vector` provides contiguous memory and RAII cleanup
- Allocation happens in `prepare()`, never during processing
- `resize()` + `fill()` ensures buffer is zeroed

**Alternatives Considered**:
- Raw array with `new[]`: Rejected - manual memory management violates Constitution Principle III
- `std::array`: Rejected - size must be compile-time constant, but max delay is runtime configurable

## Edge Case Handling

### Decision: Clamp Delay to Valid Range

| Edge Case | Handling |
|-----------|----------|
| delay < 0 | Clamp to 0 (return current sample) |
| delay > maxDelay | Clamp to maxDelay |
| NaN delay | Return 0.0f (silence) |
| Infinity delay | Clamp to maxDelay |

**Rationale**: Defensive programming prevents undefined behavior. Clamping is safer than assertions in real-time code.

## Performance Considerations

### Memory Access Pattern

- **Write**: Sequential (writeIndex increments monotonically)
- **Read**: Random access within buffer (based on delay time)
- **Cache behavior**: Buffer fits in L2/L3 cache for typical delay times (<1s)

### Branch Elimination

- Index wraparound uses bitwise AND (branchless)
- Delay clamping uses `std::clamp` (may be branchless on modern compilers)
- Interpolation formulas are branch-free

## References

1. Smith, Julius O. "Physical Audio Signal Processing" - Delay line fundamentals
2. Dattorro, Jon. "Effect Design Part 2: Delay-Line Modulation and Chorus" - Allpass interpolation
3. C++ Core Guidelines - Modern C++ patterns used in implementation
