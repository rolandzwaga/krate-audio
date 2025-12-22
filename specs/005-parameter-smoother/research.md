# Research: Parameter Smoother

**Feature**: 005-parameter-smoother
**Date**: 2025-12-22
**Purpose**: Technical research for parameter smoothing algorithms

---

## Research Topics

### 1. One-Pole Smoother Algorithm

**Decision**: Use exponential moving average with coefficient derived from time constant

**Rationale**:
- Industry standard approach (used in JUCE, iPlug2, all major audio frameworks)
- Mathematically equivalent to first-order IIR lowpass filter
- Single multiply-add per sample (minimal CPU)
- Natural exponential decay matches human perception of smooth transitions

**Formula**:
```
coefficient = exp(-2.0 * PI / (smoothTimeMs * 0.001 * sampleRate))
// Simplified: coefficient = exp(-1.0 / (tau * sampleRate))
// where tau = smoothTimeMs / (2.0 * PI * 1000.0)

output = target + coefficient * (output - target)
// Equivalent to: output = output + (1 - coefficient) * (target - output)
```

**Time Constant Convention**:
- One time constant (tau) = time to reach 63.2% of target
- 5 time constants ≈ 99.3% of target (effectively "complete")
- If user specifies "10ms smoothing", this means 10ms to 99% → tau ≈ 2ms

**Alternatives Considered**:
- Second-order filter: More CPU, no perceptual benefit for parameter smoothing
- Moving average: Requires buffer storage, not suitable for real-time
- Polynomial curves: Complex, no standard definition for "smoothing time"

---

### 2. Linear Ramp Algorithm

**Decision**: Use constant increment per sample with exact target detection

**Rationale**:
- Required for delay time changes to create tape-like pitch effects
- Constant rate means predictable transition time
- No overshoot possible (unlike exponential approach)
- Users expect "10ms ramp" to complete in exactly 10ms

**Formula**:
```
increment = (target - current) / (rampTimeMs * 0.001 * sampleRate)
// Applied each sample:
if (current != target) {
    current += increment;
    // Clamp to target if we'd overshoot
    if ((increment > 0 && current > target) || (increment < 0 && current < target)) {
        current = target;
    }
}
```

**Key Considerations**:
- Must handle direction changes mid-ramp
- Must detect exact completion (avoid floating-point drift)
- Increment recalculated when target changes

**Alternatives Considered**:
- S-curve (smoothstep): More complex, no advantage for audio parameters
- Polynomial easing: Overkill for simple parameter changes

---

### 3. Slew Rate Limiter Algorithm

**Decision**: Per-sample rate clamping with separate rise/fall limits

**Rationale**:
- Prevents sudden parameter jumps (e.g., feedback 0% → 100%)
- Different rise/fall rates useful for compression-like behavior
- Simpler than full envelope follower
- Natural behavior: small changes are instant, large changes are limited

**Formula**:
```
delta = target - current;
if (delta > riseRatePerSample) {
    current += riseRatePerSample;
} else if (delta < -fallRatePerSample) {
    current -= fallRatePerSample;
} else {
    current = target;  // Within rate limit, snap to target
}
```

**Rate Specification**:
- Rate in "units per millisecond" for user configuration
- Convert to "units per sample" internally: rate_per_sample = rate_per_ms / (sampleRate * 0.001)
- Example: 0.1 units/ms at 44.1kHz = 0.1 / 44.1 = 0.00227 units/sample

**Alternatives Considered**:
- Envelope follower: More complex, attack/release semantics different
- Proportional limiter: Harder to reason about timing

---

### 4. Completion Detection

**Decision**: Use epsilon threshold with snap-to-target

**Rationale**:
- Exponential smoothers never mathematically reach target
- Practical threshold needed for "is complete?" queries
- Snapping prevents infinite asymptotic approach
- Enables CPU optimization (skip processing when stable)

**Implementation**:
```cpp
constexpr float kCompletionThreshold = 0.0001f;  // For normalized 0-1 values

bool isComplete() const noexcept {
    return std::abs(current_ - target_) < kCompletionThreshold;
}

// In process():
if (isComplete()) {
    current_ = target_;  // Snap to exact target
    return current_;
}
```

**Threshold Selection**:
- 0.0001 = -80 dB below full scale (inaudible for most parameters)
- For gain: 0.0001 difference in linear gain is ~0.001 dB
- For normalized 0-1 range: imperceptible

---

### 5. Denormal Handling

**Decision**: Flush to zero when below 1e-15

**Rationale**:
- Denormals cause massive CPU slowdowns (100x on some platforms)
- Audio smoothers decay toward target, can produce tiny values
- Constitution requires denormal handling (Principle X, Cross-Platform)

**Implementation**:
```cpp
inline float flushDenormal(float x) noexcept {
    return (std::abs(x) < 1e-15f) ? 0.0f : x;
}

// Or use FTZ/DAZ flags per constitution guidelines
```

**Alternatives Considered**:
- DC offset: Works but adds tiny offset to all values
- FTZ/DAZ only: Platform-specific, not portable for individual values

---

### 6. Sample Rate Independence

**Decision**: Store time in milliseconds, recalculate coefficient on sample rate change

**Rationale**:
- User specifies smoothing time in ms (human-meaningful)
- Coefficient depends on sample rate
- Must recalculate when sample rate changes

**Implementation**:
```cpp
class OnePoleSmoother {
    float smoothTimeMs_;   // User-specified time
    float coefficient_;    // Calculated from time + sample rate

    void setSampleRate(float sampleRate) noexcept {
        coefficient_ = calculateCoefficient(smoothTimeMs_, sampleRate);
    }
};
```

---

### 7. Constexpr Coefficient Calculation

**Decision**: Provide constexpr calculation for compile-time initialization

**Rationale**:
- Pre-computed coefficients reduce runtime overhead
- Common use case: fixed smoothing time known at compile time
- Follows pattern from biquad.h (BiquadCoefficients::calculateConstexpr)

**Challenge**: `std::exp` is not constexpr in MSVC

**Solution**: Custom Taylor series for constexpr exp (reuse from db_utils.h):
```cpp
constexpr float constexprExp(float x) noexcept {
    // Taylor series: e^x = 1 + x + x²/2! + x³/3! + ...
    // Use sufficient terms for float precision
    float sum = 1.0f;
    float term = 1.0f;
    for (int i = 1; i <= 16; ++i) {
        term *= x / static_cast<float>(i);
        sum += term;
    }
    return sum;
}
```

---

### 8. Block Processing Optimization

**Decision**: Provide optimized block processing that skips when complete

**Rationale**:
- Block processing more cache-friendly
- Can skip entire block if already at target
- Common pattern in audio processing

**Implementation**:
```cpp
void processBlock(float* output, size_t numSamples) noexcept {
    if (isComplete()) {
        // Fill with constant target value
        std::fill(output, output + numSamples, target_);
        return;
    }
    for (size_t i = 0; i < numSamples; ++i) {
        output[i] = process();
    }
}
```

---

## Existing Codebase Integration

### Related Components in ARCHITECTURE.md

| Component | Location | Relevance |
|-----------|----------|-----------|
| `OnePoleSmoother` | `dsp_utils.h` | Basic smoother exists, but limited API |
| `SmoothedBiquad` | `biquad.h` | Uses internal smoothing, can be replaced |
| `dbToGain/gainToDb` | `db_utils.h` | Used for dB smoothing (smooth in dB domain) |

### Design Decision: Standalone vs. Extending Existing

**Decision**: Create standalone `smoother.h` with full-featured smoothers

**Rationale**:
- `dsp_utils.h::OnePoleSmoother` is minimal (no completion detection, no snap)
- `SmoothedBiquad` has internal smoothing that works but isn't reusable
- New primitives follow Layer 1 pattern (separate file, comprehensive API)
- Can deprecate `dsp_utils.h::OnePoleSmoother` later

---

## Performance Validation Plan

| Test | Target | Method |
|------|--------|--------|
| Single sample processing | < 10ns | Benchmark with Catch2 BENCHMARK |
| Block processing (512 samples) | < 2μs | Benchmark with varying block sizes |
| Memory allocation | 0 allocations | Static analysis + AddressSanitizer |
| Denormal handling | No CPU spike | Feed decaying signal, measure CPU |

---

## References

- Julius O. Smith III - "Introduction to Digital Filters with Audio Applications" (one-pole smoothing theory)
- JUCE `SmoothedValue` implementation (industry standard reference)
- iPlug2 `IParam::SetSmoothed` (alternative implementation)
- Ross Bencina - "Real-time audio programming 101" (timing constraints)
