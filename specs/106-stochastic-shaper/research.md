# Research: Stochastic Shaper

**Feature**: 106-stochastic-shaper | **Date**: 2026-01-26

## Summary

This document captures research findings for the StochasticShaper Layer 1 primitive implementation.

---

## R1: Jitter Rate to Smoother Configuration

### Problem

The spec defines `jitterRate` in Hz (FR-012: 0.01 to sampleRate/2), but OnePoleSmoother expects `smoothTimeMs` (time to reach 99% of target). How do we convert?

### Analysis

From `smoother.h`, the coefficient calculation is:
```cpp
// Time to 99% = 5 * tau, so tau = smoothTimeMs / 5000 (seconds)
// coefficient = exp(-1.0 / (tau * sampleRate))
// Simplified: coeff = exp(-5000.0 / (smoothTimeMs * sampleRate))
```

For rate-based smoothing (frequency in Hz):
- Time constant `tau = 1 / (2 * pi * frequency)`
- Time to 99% = `5 * tau = 5 / (2 * pi * frequency)` seconds
- In milliseconds: `smoothTimeMs = 5000 / (2 * pi * jitterRate)`
- Simplified: `smoothTimeMs = 795.77 / jitterRate`

### Verification

| jitterRate (Hz) | smoothTimeMs | Expected Behavior |
|-----------------|--------------|-------------------|
| 0.1 | ~7958 ms (clamped to 1000) | Very slow drift |
| 1.0 | ~796 ms | Slow variation |
| 10.0 (default) | ~80 ms | Moderate variation |
| 100.0 | ~8 ms | Fast variation |
| 1000.0 | ~0.8 ms (clamped to 0.1) | Rapid, near audio-rate |

### Decision

Use the formula: `smoothTimeMs = std::clamp(800.0f / jitterRate, 0.1f, 1000.0f)`

The smoother's built-in clamping handles edge cases automatically.

---

## R2: Smoothed Random Distribution

### Problem

What is the statistical distribution of smoothed Xorshift32 output? Does it remain bounded?

### Analysis

1. **Xorshift32 raw output**: `nextFloat()` returns uniform distribution over [-1.0, 1.0]

2. **OnePoleSmoother behavior**:
   - Output approaches target exponentially
   - With random targets varying in [-1, 1], output stays bounded to [-1, 1]
   - Distribution becomes approximately Gaussian due to central limit theorem effect from smoothing
   - Mean approaches 0 (symmetric input distribution)

3. **Bounding guarantee**:
   - Smoother cannot exceed input bounds
   - If target stays in [-1, 1], output stays in [-1, 1]
   - This satisfies FR-031: "Smoothed random values MUST remain bounded to [-1.0, 1.0]"

### Decision

No additional clamping needed on smoother output. The mathematical properties of exponential smoothing with bounded input guarantee bounded output.

---

## R3: Per-Sample Drive Modulation Performance

### Problem

The Waveshaper doesn't have a `process(x, drive)` overload. Is calling `setDrive()` + `process()` per-sample acceptable?

### Analysis

Looking at Waveshaper::setDrive():
```cpp
void setDrive(float drive) noexcept {
    drive_ = std::abs(drive);
}
```

This is:
- 1 std::abs call (typically 1 instruction)
- 1 assignment

Total overhead: ~2-3 CPU cycles per sample.

At 44100 Hz:
- 2 cycles * 44100 samples = 88200 cycles/second
- On a 3 GHz CPU: 0.003% overhead
- Well within Layer 1 budget of 0.1%

### Decision

Use `setDrive()` + `process()` pattern. Overhead is negligible and avoids modifying Waveshaper API.

---

## R4: Denormal Prevention Pattern

### Problem

Where should flushDenormal be applied in StochasticShaper?

### Analysis

From existing code review:

1. **OnePoleSmoother** (smoother.h:208):
   ```cpp
   float process() noexcept {
       // ...
       current_ = detail::flushDenormal(current_);
       return current_;
   }
   ```
   - Already handles denormals internally

2. **ChaosWaveshaper** (chaos_waveshaper.h):
   - Applies flushDenormal to attractor state variables after update
   - Does NOT apply to smoother outputs (redundant)

3. **Waveshaper**:
   - Pure function, no state accumulation
   - No denormal risk in processing

### Decision

Do NOT add explicit flushDenormal calls to StochasticShaper. The composed OnePoleSmoother primitives handle denormal prevention internally. The only state variables are the smoothers, which are self-maintaining.

---

## R5: Two Independent Smoothers Design

### Problem

FR-018 requires "independent smoothed random streams from the same RNG". How to implement?

### Analysis

Options considered:

1. **Single smoother, two samples per call**:
   - Call RNG twice, smooth each independently
   - Problem: RNG calls alternate, creating correlation

2. **Two smoothers, sequential RNG calls**:
   - Each smoother gets its own RNG call per sample
   - Result: Uncorrelated values at same rate
   - Matches FR-018: "same rate, uncorrelated values"

3. **Two separate RNGs**:
   - More memory, harder to reproduce
   - Overkill for this use case

### Decision

Use Option 2: Two OnePoleSmoother instances, each getting a fresh RNG value per process() call.

```cpp
// Per-sample processing
jitterSmoother_.setTarget(rng_.nextFloat());  // First call
float jitter = jitterSmoother_.process();

driveSmoother_.setTarget(rng_.nextFloat());   // Second call (uncorrelated)
float driveMod = driveSmoother_.process();
```

This ensures:
- Same RNG instance (reproducible with seed)
- Independent random sequences (two separate calls)
- Same smoothing rate (configured identically)
- Uncorrelated values (different random samples)

---

## R6: Bypass Behavior (FR-010, FR-016, FR-024)

### Problem

What happens when stochastic parameters are zero?

### Analysis

| Condition | Expected Output | Implementation |
|-----------|-----------------|----------------|
| jitterAmount=0, coeffNoise=0 | Identical to standard Waveshaper | `jitterOffset=0`, `effectiveDrive=baseDrive_` |
| jitterAmount=0, coeffNoise>0 | Drive modulation only | `jitterOffset=0`, drive varies |
| jitterAmount>0, coeffNoise=0 | Signal jitter only | `effectiveDrive=baseDrive_` |

### Decision

The processing formula naturally handles all cases:

```cpp
float jitterOffset = jitterAmount_ * smoothedJitter * 0.5f;  // 0 when jitterAmount_=0
float effectiveDrive = baseDrive_ * (1.0f + coefficientNoise_ * smoothedDriveMod * 0.5f);  // baseDrive_ when coeffNoise=0
```

No special-case bypass code needed. FR-024 is satisfied mathematically.

---

## Patterns for Reuse

### SmoothedRandom Pattern

This pattern composes Xorshift32 + OnePoleSmoother for rate-controlled random modulation:

```cpp
// Setup
Xorshift32 rng_{seed};
OnePoleSmoother smoother_;

void prepare(double sampleRate) {
    float smoothTimeMs = 800.0f / rateHz;
    smoother_.configure(smoothTimeMs, sampleRate);
    smoother_.snapTo(rng_.nextFloat());  // Initialize without transient
}

float process() {
    smoother_.setTarget(rng_.nextFloat());
    return smoother_.process();  // Returns smoothed random in [-1, 1]
}
```

**When to extract**: If `ring_saturation` or `bitwise_mangler` need the same pattern, extract to `SmoothedRandom` utility class.

---

## References

- `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother implementation
- `dsp/include/krate/dsp/core/random.h` - Xorshift32 implementation
- `dsp/include/krate/dsp/primitives/waveshaper.h` - Waveshaper composition target
- `dsp/include/krate/dsp/primitives/chaos_waveshaper.h` - Reference for similar composition patterns
- `dsp/include/krate/dsp/processors/stochastic_filter.h` - Reference for smoothed random pattern (Layer 2)
