# Research: First-Order Allpass Filter (Allpass1Pole)

**Date**: 2026-01-21 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Research Summary

All clarifications were resolved during the specification phase. This research documents the technical decisions and their rationales.

---

## Resolved Clarifications

### 1. NaN/Infinity Detection Strategy

**Decision**: Per-block detection with early exit

**Rationale**:
- `processBlock()`: Check first sample only; if invalid, fill entire block with zeros and reset state
- `process()`: Check every input for safety (single sample API)
- Per-sample checking in block processing adds unnecessary overhead (~3% CPU cost per spec analysis)
- Early exit prevents processing garbage data through the filter

**Alternatives Considered**:
- Per-sample detection in all cases: Rejected due to CPU overhead
- No detection: Rejected due to potential for filter explosion/instability

### 2. Floating-Point Precision

**Decision**: float-only arithmetic

**Rationale**:
- Sample rate stored as `double` for accuracy at high rates (192kHz)
- Sample rate cast to `float` during coefficient calculation
- All intermediate calculations use `float`
- Cumulative error for 12-stage cascade: approximately -118 dB (inaudible)
- Memory footprint minimized (no double state variables)

**Alternatives Considered**:
- Double precision throughout: Rejected - unnecessary for audio quality, larger memory footprint
- Mixed precision: Current choice - optimal balance

### 3. Denormal Flushing Strategy

**Decision**: Per-block flushing

**Rationale**:
- `processBlock()`: Flush state variables (z1_, y1_) once at block end
- `process()`: Flush after each call (necessary for single-sample API safety)
- Prevents denormal accumulation during sustained silence
- Uses existing `detail::flushDenormal()` from db_utils.h

**Alternatives Considered**:
- Per-sample in all cases: Rejected - excessive for block processing
- No flushing: Rejected - causes 100x CPU slowdown on some processors

### 4. Coefficient Boundary Clamping

**Decision**: Clamp to +/-0.9999f

**Rationale**:
- Coefficient `a` must be in range (-1, +1) exclusive for stability
- `setCoefficient(1.0f)` -> 0.9999f
- `setCoefficient(-1.0f)` -> -0.9999f
- Provides numerical headroom without audible artifacts
- Matches common DSP practice for IIR filter stability margins

**Alternatives Considered**:
- +/-0.999f: Rejected - too restrictive for high-frequency operation
- +/-0.99999f: Rejected - too close to instability boundary

### 5. Frequency Clamping

**Decision**: Fixed 1 Hz minimum

**Rationale**:
- Minimum: 1 Hz (simple, predictable, frequency-independent)
- Maximum: Nyquist * 0.99 (prevents tan() from approaching infinity)
- Consistent with `kMinFilterFrequency` in biquad.h
- Avoids complex sample-rate-dependent minimum calculations

**Alternatives Considered**:
- Sample-rate-dependent minimum: Rejected - adds complexity without benefit
- 0.1 Hz minimum: Rejected - coefficient approaches 1.0, causes instability

---

## Technical Research

### First-Order Allpass Filter Theory

**Transfer Function**:
```
H(z) = (a + z^-1) / (1 + a*z^-1)
```

**Difference Equation**:
```
y[n] = a*x[n] + x[n-1] - a*y[n-1]
```

**Coefficient Calculation**:
```
a = (1 - tan(pi * freq / sampleRate)) / (1 + tan(pi * freq / sampleRate))
```

Where `freq` is the break frequency (frequency at which phase shift = -90 degrees).

**Phase Response**:
- 0 degrees at DC (f = 0 Hz)
- -90 degrees at break frequency
- -180 degrees at Nyquist

**Magnitude Response**:
- Unity (1.0) at all frequencies
- This is the defining characteristic of allpass filters

### Coefficient-to-Frequency Relationship

| Coefficient (a) | Break Frequency |
|-----------------|-----------------|
| a -> +1.0 | f -> 0 Hz |
| a = 0 | f = fs/4 (quarter sample rate) |
| a -> -1.0 | f -> fs/2 (Nyquist) |

Inverse formula for `frequencyFromCoeff`:
```
freq = sampleRate * atan((1 - a) / (1 + a)) / pi
```

### Phaser Application

Phasers cascade multiple first-order allpass stages (typically 2-12) and mix with dry signal:
- Phase shift varies with frequency
- Mixing creates comb filter effect (notches)
- LFO modulation of break frequency creates "swooshing" effect

---

## Implementation Patterns from Codebase

### Pattern: Layer 1 Primitive (from biquad.h)

```cpp
class Biquad {
public:
    // Configuration
    void configure(...) noexcept;

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // State
    void reset() noexcept;

private:
    // State variables
    float z1_ = 0.0f;
    float z2_ = 0.0f;
};
```

### Pattern: NaN Handling (from biquad.h)

```cpp
[[nodiscard]] float process(float input) noexcept {
    if (!detail::isFiniteBits(input)) {
        reset();
        return 0.0f;
    }
    // ... processing
}
```

### Pattern: Denormal Flushing (from biquad.h)

```cpp
z1_ = detail::flushDenormal(z1_);
z2_ = detail::flushDenormal(z2_);
```

### Pattern: Static Utility Functions (from biquad.h)

```cpp
[[nodiscard]] static BiquadCoefficients calculate(
    FilterType type,
    float frequency,
    float Q,
    float gainDb,
    float sampleRate
) noexcept;
```

---

## Dependencies Verified

| Dependency | Header | Verified API |
|------------|--------|--------------|
| kPi | math_constants.h | `inline constexpr float kPi = 3.14159265358979323846f;` |
| flushDenormal | db_utils.h | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` |
| isNaN | db_utils.h | `constexpr bool isNaN(float x) noexcept` |
| isInf | db_utils.h | `[[nodiscard]] constexpr bool isInf(float x) noexcept` |

---

## Test Strategy

Based on existing patterns from `biquad_test.cpp`:

### Unit Tests

1. **Construction/Default State**: Default coefficient, zero state
2. **prepare()**: Sample rate storage
3. **setFrequency()**: Coefficient calculation, clamping
4. **setCoefficient()**: Direct setting, clamping
5. **process()**: Difference equation, NaN handling
6. **processBlock()**: Block processing, NaN early exit, denormal flushing
7. **reset()**: State clearing

### Property Tests

1. **Unity Magnitude**: Output amplitude equals input amplitude at multiple frequencies
2. **Phase at Break Frequency**: -90 degrees +/- 0.1 degree (SC-002)
3. **Coefficient Round-Trip**: coeffFromFrequency -> frequencyFromCoeff = original
4. **Block vs Sample Equivalence**: processBlock == N x process() (SC-007)

### Edge Case Tests

1. **NaN Input**: Reset and return 0
2. **Infinity Input**: Reset and return 0
3. **Denormal State**: Flushed to zero
4. **Frequency at 0 Hz**: Clamped to 1 Hz
5. **Frequency above Nyquist**: Clamped to Nyquist * 0.99
6. **Coefficient at +/-1**: Clamped to +/-0.9999f

### Performance Tests

1. **Processing Speed**: < 10 ns/sample (SC-003)
2. **Memory Footprint**: < 32 bytes (SC-004)

---

## Risk Assessment

| Risk | Mitigation |
|------|------------|
| Numerical instability at boundary frequencies | Coefficient clamping to +/-0.9999f |
| Denormal CPU spikes | Per-block denormal flushing |
| NaN propagation | Input validation with state reset |
| Cross-platform precision differences | Float-only arithmetic, 1e-6 test tolerance |

---

## Conclusion

All clarifications resolved. Ready to proceed to Phase 1 (Design & Contracts).
