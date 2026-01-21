# Research: State Variable Filter (SVF)

**Feature**: 080-svf | **Date**: 2026-01-21 | **Status**: Complete

## Research Questions

This document resolves all "NEEDS CLARIFICATION" items from the Technical Context and researches best practices for TPT SVF implementation.

---

## 1. Cytomic TPT SVF Topology

### Decision: Use Cytomic's "SvfLinearTrapOptimised2" topology

### Rationale

The Cytomic TPT (Topology-Preserving Transform) SVF is considered the gold standard for modulation-stable state variable filters in audio software. Key benefits:

1. **Trapezoidal Integration**: Uses the trapezoidal rule rather than bilinear transform, providing:
   - Better frequency response matching to analog prototype
   - Excellent behavior under audio-rate modulation
   - No coefficient discontinuities when parameters change

2. **Zero-Delay Feedback (ZDF)**: The topology resolves the implicit delay in the feedback path, providing:
   - Accurate resonance at all frequencies
   - Self-oscillation possible at Q = infinity (k = 0)
   - Proper analog-like behavior

3. **Efficient Multi-Output**: Single computation produces all outputs (LP, HP, BP, Notch) with minimal overhead.

### Alternatives Considered

| Alternative | Why Rejected |
|-------------|--------------|
| Standard Biquad | Modulation unstable, single output |
| Chamberlin SVF | Has tuning error at high frequencies, modulation clicks |
| Direct Form II SVF | Less stable numerically, modulation issues |
| Zavalishin TPT | Similar to Cytomic, but Cytomic has better documentation |

### References

- [Cytomic Technical Papers](https://cytomic.com/technical-papers/)
- [SvfLinearTrapOptimised2.pdf](https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf)
- Vadim Zavalishin, "The Art of VA Filter Design" (Native Instruments)

---

## 2. Coefficient Formulas

### Decision: Use Cytomic's optimized coefficient calculation

### Core Coefficients

The TPT SVF uses these primary coefficients:

```
g = tan(pi * cutoff / sampleRate)    // "angular frequency" coefficient
k = 1/Q                               // damping coefficient (inverse Q)
```

### Derived Coefficients (for optimized computation)

Pre-compute these to reduce per-sample operations:

```
a1 = 1 / (1 + g * (g + k))
a2 = g * a1
a3 = g * a2
```

### Per-Sample Processing

```cpp
// v3 is the highpass output (before state update)
v3 = input - ic2eq;

// v1 is the bandpass output
v1 = a1 * ic1eq + a2 * v3;

// v2 is the lowpass output
v2 = ic2eq + a2 * ic1eq + a3 * v3;

// Update integrator states (trapezoidal rule)
ic1eq = 2.0f * v1 - ic1eq;
ic2eq = 2.0f * v2 - ic2eq;

// Final outputs
low = v2;
band = v1;
high = v3 - k * v1 - v2;     // Note: includes k term
notch = low + high;           // Or: v3 - k * v1 (equivalent)
```

### Rationale

This formulation:
1. Minimizes per-sample operations (only 10 multiplies, 8 adds)
2. Uses only 2 state variables (ic1eq, ic2eq)
3. Allows pre-computation of a1, a2, a3 when parameters change
4. Provides all four outputs with minimal extra cost

### Alternatives Considered

| Alternative | Why Rejected |
|-------------|--------------|
| Full matrix form | More operations per sample |
| Naive tan() per sample | Much slower, tan() is expensive |
| Table-based g lookup | Memory overhead, less accurate |

---

## 3. Mode Mixing Coefficients

### Decision: Use linear combination of outputs with mode-specific coefficients

### Mode Mixing Formula

```
output = m0 * high + m1 * band + m2 * low
```

### Mode Coefficient Table

| Mode | m0 | m1 | m2 | Notes |
|------|----|----|-----|-------|
| Lowpass | 0 | 0 | 1 | Pure LP output |
| Highpass | 1 | 0 | 0 | Pure HP output |
| Bandpass | 0 | 1 | 0 | Pure BP output |
| Notch | 1 | 0 | 1 | HP + LP = Notch |
| Allpass | 1 | -k | 1 | Note: m1 depends on k! |
| Peak | 1 | 0 | -1 | HP - LP (differential) |
| LowShelf | 1 | k*(A-1) | A^2 | A = 10^(dB/40) |
| HighShelf | A^2 | k*(A-1) | 1 | A = 10^(dB/40) |

### Shelf/Peak Gain Calculation

For shelf and peak modes, we need the amplitude parameter A:

```cpp
// A = sqrt(10^(dB/20)) = 10^(dB/40)
A = std::pow(10.0f, gainDb / 40.0f);
// Or using constexpr: A = detail::constexprPow10(gainDb / 40.0f);
```

### Rationale

1. **Allpass requires dynamic m1**: The allpass mode's m1 coefficient is `-k` (inverse of Q), which must be updated whenever Q changes. This is why SVF uses its own `SVFMode` enum instead of reusing `FilterType`.

2. **Shelf modes use A^2**: The squared gain term provides the correct shelf depth at low/high frequencies.

3. **Peak mode is differential**: HP - LP creates a band-reject character that, when mixed appropriately, provides peak boost/cut.

### Alternatives Considered

| Alternative | Why Rejected |
|-------------|--------------|
| Separate peak/shelf topology | More complex, unnecessary |
| Runtime coefficient lookup | Minimal benefit, adds indirection |

---

## 4. Gain Coefficient (A) for Shelf/Peak Modes

### Decision: Recalculate A immediately on setGain() call

### Formula

```cpp
// A = 10^(gainDb/40) = sqrt(10^(gainDb/20)) = sqrt(linear_gain)
A_ = detail::constexprPow10(gainDb / 40.0f);
```

### Why 40 instead of 20?

The division by 40 (instead of 20) is because shelf filters use `A` and `A^2`:
- `10^(dB/40)` gives the square root of the linear voltage gain
- When we use `A^2` in the mixing coefficients, we get the full shelf depth

This is equivalent to:
```cpp
float linearGain = std::pow(10.0f, gainDb / 20.0f);  // e.g., 2.0 for +6dB
float A = std::sqrt(linearGain);                      // e.g., 1.414
// A^2 = 2.0, which is the full +6dB gain
```

### Rationale

- **Immediate recalculation**: Per clarification #1 in spec.md, gain changes take effect immediately
- **No smoothing**: Matches the behavior of cutoff/resonance (clarification #2)
- **constexpr compatible**: Using `detail::constexprPow10` for potential compile-time use

---

## 5. Multi-Channel Processing Strategy

### Decision: Share coefficients, independent state per channel

### Implementation Pattern

```cpp
// Coefficients are shared (calculated once)
struct SVFCoeffs {
    float g, k, a1, a2, a3;
    float m0, m1, m2;  // Mode mixing
    float A;           // For shelf/peak
};

// State is per-channel
struct SVFState {
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;
};

// For stereo processing:
SVFCoeffs coeffs_;           // Shared
SVFState stateL_, stateR_;   // Independent
```

### Rationale

1. **Efficiency**: Coefficient calculation is expensive (tan() call); sharing coefficients avoids redundant computation.

2. **Consistent response**: Both channels have identical frequency response.

3. **Independent state**: Each channel maintains its own filter state, so transients in one channel don't affect the other.

4. **Simple API**: The SVF class manages single-channel state; users create multiple instances for multi-channel (similar to existing Biquad pattern).

### Out of Scope

Per spec.md "Out of Scope" section:
- Multi-channel/stereo variants (users create separate instances)
- Coefficient sharing between instances is application-level concern

---

## 6. Parameter Clamping Ranges

### Decision: Clamp silently to safe ranges (per clarification #4)

### Parameter Ranges

| Parameter | Min | Max | Default | Justification |
|-----------|-----|-----|---------|---------------|
| sampleRate | 1000.0 | - | 44100.0 | Minimum practical sample rate |
| cutoff (Hz) | 1.0 | sampleRate * 0.495 | 1000.0 | Below Nyquist, above DC |
| Q | 0.1 | 30.0 | 0.7071 | Butterworth default, max before instability |
| gainDb | -24.0 | +24.0 | 0.0 | Practical shelf/peak range |

### Clamping Implementation

```cpp
float clampCutoff(float hz) const noexcept {
    const float maxFreq = static_cast<float>(sampleRate_) * 0.495f;
    if (hz < 1.0f) return 1.0f;
    if (hz > maxFreq) return maxFreq;
    return hz;
}

float clampQ(float q) const noexcept {
    if (q < 0.1f) return 0.1f;
    if (q > 30.0f) return 30.0f;
    return q;
}

float clampGainDb(float db) const noexcept {
    if (db < -24.0f) return -24.0f;
    if (db > 24.0f) return 24.0f;
    return db;
}
```

### Rationale

1. **Silent clamping**: Real-time safe (no exceptions, no logging)
2. **0.495 Nyquist ratio**: Matches existing biquad.h convention
3. **Q range 0.1-30**: Wide enough for musical use, safe from instability
4. **Gain +/-24 dB**: Practical range for EQ/shelf applications

---

## 7. Denormal Handling Strategy

### Decision: Flush state after every process() call (per clarification #5)

### Implementation

```cpp
[[nodiscard]] float process(float input) noexcept {
    // ... processing ...

    // Update states with trapezoidal rule
    ic1eq_ = 2.0f * v1 - ic1eq_;
    ic2eq_ = 2.0f * v2 - ic2eq_;

    // Flush denormals after every sample
    ic1eq_ = detail::flushDenormal(ic1eq_);
    ic2eq_ = detail::flushDenormal(ic2eq_);

    return output;
}
```

### Rationale

1. **Most robust**: Prevents denormal accumulation under all circumstances
2. **Prevents CPU spikes**: Denormalized floats can cause 10-100x CPU slowdown
3. **Minimal overhead**: `flushDenormal()` is a simple comparison, well within Layer 1 budget
4. **Constitution compliance**: Principle II requires real-time safety

### Alternatives Considered

| Alternative | Why Rejected |
|-------------|--------------|
| Check only on silence | Complex logic, edge cases |
| Hardware FTZ/DAZ flags | Platform-specific, not always available |
| Flush every N samples | Denormals can accumulate in N-1 samples |

---

## 8. NaN/Infinity Input Handling

### Decision: Return 0.0f and reset internal state (per FR-022)

### Implementation

```cpp
[[nodiscard]] float process(float input) noexcept {
    // Check for invalid input
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();  // Clear ic1eq_ and ic2eq_
        return 0.0f;
    }

    // Normal processing...
}
```

### Rationale

1. **Predictable output**: 0.0f is a safe, neutral value
2. **State recovery**: Resetting prevents NaN propagation through state variables
3. **Real-time safe**: No exceptions, just a branch and return
4. **Matches existing patterns**: Same approach used in OnePoleLP/OnePoleHP

---

## 9. Pre-prepare() Behavior

### Decision: Return input unchanged for process(), zeros for processMulti()

### Implementation

```cpp
[[nodiscard]] float process(float input) noexcept {
    if (!prepared_) {
        return input;  // Bypass when not prepared
    }
    // Normal processing...
}

[[nodiscard]] SVFOutputs processMulti(float input) noexcept {
    if (!prepared_) {
        return SVFOutputs{0.0f, 0.0f, 0.0f, 0.0f};  // Zeros when not prepared
    }
    // Normal processing...
}
```

### Rationale

1. **process() bypasses**: User hears original signal, indicating filter not active
2. **processMulti() zeros**: All outputs silent when not configured
3. **Safe default**: No crashes or undefined behavior
4. **Matches OnePoleLP pattern**: Consistent with existing Layer 1 primitives

---

## 10. Coefficient Update Timing

### Decision: Immediate recalculation on setCutoff()/setResonance()/setGain()

### Rationale

Per clarification #2 in spec.md, coefficients are recalculated immediately when parameters change:

```cpp
void setCutoff(float hz) noexcept {
    cutoffHz_ = clampCutoff(hz);
    updateCoefficients();  // Recalculate g, a1, a2, a3 immediately
}

void setResonance(float q) noexcept {
    q_ = clampQ(q);
    updateCoefficients();  // Recalculate k, a1, a2, a3 immediately
}

void setGain(float dB) noexcept {
    gainDb_ = clampGainDb(dB);
    A_ = detail::constexprPow10(gainDb_ / 40.0f);  // Recalculate A immediately
    updateMixCoefficients();  // Update m0, m1, m2 for shelf/peak
}
```

### No Smoothing

The spec explicitly states no smoothing. Users who need click-free parameter changes should either:
1. Smooth the parameter values before calling setCutoff()/setResonance()
2. Use a future SmoothedSVF variant (out of scope for this spec)

---

## Summary

All research questions resolved. The implementation will follow the Cytomic TPT topology with:

1. **Coefficients**: g = tan(pi*fc/fs), k = 1/Q, derived a1/a2/a3
2. **Mode mixing**: Linear combination with mode-specific m0/m1/m2
3. **Gain**: A = 10^(dB/40) for shelf/peak modes
4. **Multi-channel**: Shared coefficients, independent state
5. **Clamping**: Silent clamping to safe ranges
6. **Denormals**: Flush after every process() call
7. **Invalid input**: Return 0, reset state
8. **Pre-prepare**: Return input unchanged / zeros
9. **Updates**: Immediate coefficient recalculation
