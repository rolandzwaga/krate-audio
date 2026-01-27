# Research: BitwiseMangler

**Feature**: 111-bitwise-mangler
**Date**: 2026-01-27
**Status**: Complete

## Overview

This document consolidates research findings for the BitwiseMangler Layer 1 DSP primitive. All NEEDS CLARIFICATION items from the spec have been resolved through the clarification process.

## Research Topics

### 1. Float-to-Integer Conversion

**Question**: What scaling formula should be used for float-to-integer conversion?

**Decision**: Multiply by 8388608 (2^23)

**Rationale**:
- Standard audio DSP practice that matches float mantissa precision
- 24-bit signed integer range: -8388608 to +8388607
- Formula: `int24 = static_cast<int32_t>(floatSample * 8388608.0f)`
- Inverse: `floatSample = static_cast<float>(int24) / 8388608.0f`
- Slight asymmetry acceptable (standard audio conversion approach)

**Alternatives Considered**:
| Alternative | Reason Rejected |
|-------------|-----------------|
| 2^24 (16777216) | Would exceed 24-bit signed range |
| 2^23-1 (8388607) | More complex for no practical benefit |
| Symmetric scaling | Adds complexity without audible improvement |

**Reference**: Common practice in audio DSP libraries, matches IEEE float mantissa precision.

---

### 2. BitAverage Operation Type

**Question**: Should BitAverage operation be fixed to AND or parameterized with AND/OR options?

**Decision**: Fixed to AND operation

**Rationale**:
- Simplifies initial implementation
- AND creates "smoothing" by preserving only common bits
- OR operation noted as future parameter candidate
- Single-responsibility principle - one operation per mode

**Future Enhancement**:
Consider adding parameter to select AND/OR in future version if user feedback indicates need.

---

### 3. BitShuffle Permutation Generation

**Question**: How should BitShuffle generate the shuffle permutation from the seed?

**Decision**: Pre-compute permutation table on setSeed() call using Fisher-Yates shuffle

**Rationale**:
- Fisher-Yates (Knuth shuffle) produces unbiased random permutation
- Pre-computing avoids PRNG calls during process() (real-time safe)
- 24-element array maps input bit position to output position
- std::array<uint8_t, 24> is fixed-size, stack-allocated

**Algorithm**:
```cpp
void generatePermutation(uint32_t seed) noexcept {
    Xorshift32 rng(seed);
    // Initialize identity permutation
    for (uint8_t i = 0; i < 24; ++i) {
        permutation_[i] = i;
    }
    // Fisher-Yates shuffle
    for (int i = 23; i > 0; --i) {
        uint32_t r = rng.next();
        int j = static_cast<int>(r % static_cast<uint32_t>(i + 1));
        std::swap(permutation_[i], permutation_[j]);
    }
}
```

**Reference**: Knuth, "The Art of Computer Programming", Vol. 2, Algorithm P.

---

### 4. OverflowWrap Internal Gain

**Question**: Should OverflowWrap apply internal gain to drive signals into overflow?

**Decision**: No internal gain

**Rationale**:
- OverflowWrap shall only wrap values that exceed numeric range due to upstream processing
- User controls gain externally (more flexible)
- Matches user expectation of "wrap" not "clip with boost"
- Simpler implementation and mental model

**Usage Pattern**:
```cpp
// User applies external gain
for (auto& sample : buffer) {
    sample *= 2.0f;  // May exceed [-1, 1]
}
// OverflowWrap processes the hot signal
mangler.setOperation(BitwiseOperation::OverflowWrap);
mangler.processBlock(buffer, numSamples);
```

---

### 5. Permutation Storage

**Question**: How should the BitShuffle permutation table be stored to maintain real-time safety?

**Decision**: std::array<uint8_t, 24> allocated as part of object state

**Rationale**:
- Fixed-size array avoids heap allocation
- Stack-allocated as class member
- 24 bytes total (one byte per bit position)
- No runtime allocation needed

**Implementation**:
```cpp
class BitwiseMangler {
    // ...
private:
    std::array<uint8_t, 24> permutation_{};  // For BitShuffle
};
```

---

### 6. Existing Codebase Components

**Search Results**:

| Search Term | Found | Action |
|-------------|-------|--------|
| `class BitwiseMangler` | No | Create new |
| `BitwiseOperation` | No | Create new |
| `floatToInt` | No | Create as inline helper |
| `intToFloat` | No | Create as inline helper |

**Components to Reuse**:

| Component | Location | Usage |
|-----------|----------|-------|
| Xorshift32 | dsp/include/krate/dsp/core/random.h | PRNG for BitShuffle permutation |
| detail::isNaN | dsp/include/krate/dsp/core/db_utils.h | NaN input detection |
| detail::isInf | dsp/include/krate/dsp/core/db_utils.h | Inf input detection |
| detail::flushDenormal | dsp/include/krate/dsp/core/db_utils.h | Denormal flushing |

**Reference Implementation Patterns**:
- BitCrusher: Similar float-to-int concepts (different approach: quantization levels)
- Waveshaper: Similar intensity/blend pattern
- StochasticShaper: Similar seed-based determinism pattern

---

### 7. Bit Manipulation Algorithms

#### XOR Pattern
- Direct XOR with configurable 32-bit pattern on 24-bit integer
- Pattern masked to 24 bits: `pattern_ & 0x00FFFFFF`
- Creates predictable spectral changes based on pattern
- Common patterns: 0xAAAAAAAA (alternating), 0x55555555 (complement), 0xFFFFFFFF (invert)

#### XOR Previous
- XOR current sample with previous sample (both as 24-bit integers)
- Signal-dependent: high-frequency content changes more
- Creates natural transient emphasis

#### Bit Rotation
- Circular rotation on 24-bit unsigned value
- Left: `(x << N) | (x >> (24 - N))`
- Right: `(x >> N) | (x << (24 - N))`
- Modulo 24 applied (rotation by 24 = identity)
- Sign extension needed when converting back to signed

#### Bit Shuffle
- Pre-computed permutation table maps bit i to position permutation_[i]
- Applied by iterating through input bits and setting output bits
- Deterministic given same seed

#### Bit Average (AND)
- Bitwise AND with previous sample
- Preserves only bits set in both samples
- Tends toward zero for differing samples

#### Overflow Wrap
- Two's complement integer overflow behavior
- Large positive -> wraps to negative
- Large negative -> wraps to positive
- Natural behavior of int32_t cast from large float

---

## Implementation Notes

### Real-Time Safety Verification

| Operation | Allocation | Locks | I/O | Exceptions |
|-----------|------------|-------|-----|------------|
| process() | None | None | None | noexcept |
| processBlock() | None | None | None | noexcept |
| setSeed() | None | None | None | noexcept |
| setPattern() | None | None | None | noexcept |
| setOperation() | None | None | None | noexcept |

### Compile Flags

The source file using `detail::isNaN` and `detail::isInf` requires `-fno-fast-math` compile flag to ensure correct behavior. The implementation uses bit manipulation which is immune to fast-math optimizations.

---

## References

1. Knuth, D. E. "The Art of Computer Programming, Vol. 2: Seminumerical Algorithms" - Fisher-Yates shuffle
2. IEEE 754-2008 - Floating-point representation
3. Existing codebase patterns: BitCrusher, Waveshaper, StochasticShaper
4. DSP-ROADMAP.md Section 8.1 - Digital Destruction roadmap item
