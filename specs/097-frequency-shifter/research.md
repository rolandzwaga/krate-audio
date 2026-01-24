# Research: Frequency Shifter

**Feature**: 097-frequency-shifter
**Date**: 2026-01-24
**Status**: Complete

## Overview

This document captures research findings for implementing the FrequencyShifter Layer 2 processor. All key decisions have been clarified in the specification; this document consolidates the technical implementation details.

## Research Topics

### 1. Quadrature Oscillator Implementation

**Question**: How to efficiently generate the cos(wt) and sin(wt) carrier signals?

**Decision**: Direct phase increment recurrence relation with periodic renormalization every 1024 samples.

**Rationale**:
- Per spec clarification: "Direct phase increment recurrence with periodic renormalization every 1024 samples"
- More efficient than calling `std::sin`/`std::cos` every sample
- Recurrence relation: 4 multiplies + 2 adds per sample
- `std::sin`/`std::cos`: ~50-100 cycles per call on modern CPUs

**Implementation Details**:
```cpp
// Recurrence relation for complex exponential rotation:
// (cos(t+d), sin(t+d)) = (cos(t)*cos(d) - sin(t)*sin(d), sin(t)*cos(d) + cos(t)*sin(d))

// State variables
float cosTheta_ = 1.0f;   // cos(current phase)
float sinTheta_ = 0.0f;   // sin(current phase)
float cosDelta_ = 1.0f;   // cos(phase increment per sample)
float sinDelta_ = 0.0f;   // sin(phase increment per sample)
int renormCounter_ = 0;

// Initialize when shift frequency changes:
void updateOscillator(float shiftHz, double sampleRate) noexcept {
    const double delta = kTwoPi * shiftHz / sampleRate;
    cosDelta_ = static_cast<float>(std::cos(delta));
    sinDelta_ = static_cast<float>(std::sin(delta));
    // Keep current phase unless reset() is called
}

// Advance oscillator each sample:
void advanceOscillator() noexcept {
    const float cosNext = cosTheta_ * cosDelta_ - sinTheta_ * sinDelta_;
    const float sinNext = sinTheta_ * cosDelta_ + cosTheta_ * sinDelta_;
    cosTheta_ = cosNext;
    sinTheta_ = sinNext;

    // Renormalize every 1024 samples to prevent drift
    if (++renormCounter_ >= 1024) {
        renormCounter_ = 0;
        const float r = std::sqrt(cosTheta_ * cosTheta_ + sinTheta_ * sinTheta_);
        if (r > 0.0f) {
            cosTheta_ /= r;
            sinTheta_ /= r;
        }
    }
}
```

**Drift Analysis**:
- Without renormalization, amplitude can drift by ~0.01% per million samples
- At 44.1kHz, 1024 samples = 23ms between renormalizations
- Renormalization cost: 1 sqrt + 2 divides, amortized over 1024 samples = negligible

**Alternatives Considered**:
1. Direct `sin`/`cos` calls: Too expensive (~10x slower)
2. Wavetable lookup: Additional memory, interpolation complexity
3. CORDIC algorithm: More complex, similar performance

---

### 2. Single-Sideband Modulation Formulas

**Question**: What are the exact formulas for each direction mode?

**Decision**: Use standard Hilbert-based SSB formulas from spec.

**Formulas** (per spec FR-007, FR-008, FR-009):

Given analytic signal from Hilbert transform:
- `I` = in-phase component (original signal, delayed)
- `Q` = quadrature component (90-degree phase shifted)

And carrier signals:
- `cos(wt)` = cosTheta_
- `sin(wt)` = sinTheta_

The sideband outputs are:
```cpp
// Upper sideband (ShiftDirection::Up) - FR-007
float upper = I * cosTheta_ - Q * sinTheta_;

// Lower sideband (ShiftDirection::Down) - FR-008
float lower = I * cosTheta_ + Q * sinTheta_;

// Both sidebands (ShiftDirection::Both) - FR-009
// Per clarification: output = 0.5 * (up + down)
float both = 0.5f * (upper + lower);
// Simplifies to: both = I * cosTheta_ (Q terms cancel)
```

**Mathematical Proof for "Both" Mode Simplification**:
```
upper = I*cos - Q*sin
lower = I*cos + Q*sin
both = 0.5 * (upper + lower)
     = 0.5 * ((I*cos - Q*sin) + (I*cos + Q*sin))
     = 0.5 * (2*I*cos)
     = I * cos
```

This is equivalent to ring modulation (amplitude modulation without carrier).

---

### 3. Feedback Path Design

**Question**: How to implement stable feedback with spiraling effects?

**Decision**: Apply `tanh()` saturation on +/-1.0 range before feedback routing.

**Rationale** (per spec clarification):
- "Apply tanh to +/-1.0 range (standard audio saturation)"
- `tanh(x)` provides soft limiting: output approaches +/-1 asymptotically
- Prevents runaway oscillation while preserving signal character
- Standard technique used in analog emulation

**Implementation**:
```cpp
// Feedback path in process():
float processInternal(float input) noexcept {
    // Apply saturation to stored feedback sample
    const float saturatedFeedback = std::tanh(feedbackSample_);

    // Scale by smoothed feedback amount
    const float feedbackIn = saturatedFeedback * feedbackSmoother_.process();

    // Add to input
    const float inputWithFeedback = input + feedbackIn;

    // Process through Hilbert + SSB...
    // ...
    const float wet = /* SSB output */;

    // Store for next iteration
    feedbackSample_ = wet;

    return wet;
}
```

**Feedback at High Values**:
- At feedback = 0.99, gain per pass = 0.99
- After N passes: amplitude = 0.99^N
- With tanh limiting, even at 100% feedback, output is bounded

---

### 4. Stereo Processing Strategy

**Question**: How to implement opposite shifts for stereo?

**Decision**: Left = +shift, Right = -shift (per spec clarification).

**Rationale**:
- Standard stereo enhancement convention
- Creates complementary frequency content
- Simple implementation: negate shift for right channel

**Implementation Considerations**:
1. **Separate Hilbert Transforms**: Need hilbertL_ and hilbertR_ since each has internal state
2. **Shared Oscillator**: Single oscillator can serve both channels (negate sin for right)
3. **Feedback**: Each channel has independent feedback path

```cpp
void processStereo(float& left, float& right) noexcept {
    // Left channel: positive shift (standard formulas)
    const HilbertOutput hL = hilbertL_.process(left);
    float wetL = applySSB(hL.i, hL.q, +1.0f);  // +1 = positive shift

    // Right channel: negative shift (negate sinTheta)
    const HilbertOutput hR = hilbertR_.process(right);
    float wetR = applySSB(hR.i, hR.q, -1.0f);  // -1 = negative shift

    // Apply mix
    const float mixValue = mixSmoother_.process();
    left = left * (1.0f - mixValue) + wetL * mixValue;
    right = right * (1.0f - mixValue) + wetR * mixValue;

    // Advance oscillator once (shared between channels)
    advanceOscillator();
}

float applySSB(float I, float Q, float sign) noexcept {
    switch (direction_) {
        case ShiftDirection::Up:
            // Upper: I*cos - Q*sin; for negative shift, negate sin
            return I * cosTheta_ - Q * sinTheta_ * sign;
        case ShiftDirection::Down:
            // Lower: I*cos + Q*sin; for negative shift, negate sin
            return I * cosTheta_ + Q * sinTheta_ * sign;
        case ShiftDirection::Both:
            return I * cosTheta_;  // Q terms cancel
    }
    return I * cosTheta_;
}
```

---

### 5. Aliasing Behavior

**Question**: How to handle aliasing at extreme shift values?

**Decision**: Document only, no mitigation (per spec clarification).

**Rationale**:
- Keeps CPU budget within Layer 2 limits (<0.5%)
- Oversampling would double/quadruple processing cost
- SSB modulation is linear (no harmonics generated), so aliasing only occurs when shifted frequencies exceed Nyquist
- Users typically don't shift beyond +/-1000Hz for musical effects

**Documentation** (for header comments):
```cpp
/// @par Aliasing
/// Frequency shifting is linear and does not generate harmonics. However,
/// aliasing can occur when shifted frequencies exceed Nyquist (sampleRate/2).
/// For a 44.1kHz sample rate, a 20kHz component shifted by +3kHz would alias
/// back to 21.05kHz (appears at 23.05 - 22.05 = 21.05kHz).
/// For most musical applications with shifts under +/-1000Hz, aliasing is
/// negligible. No oversampling is provided at Layer 2 to maintain CPU budget.
```

---

### 6. Parameter Smoothing Strategy

**Question**: Which parameters need smoothing, and how?

**Decision**: Use OnePoleSmoother for shift, feedback, and mix.

**Parameters to Smooth**:
| Parameter | Smooth? | Rationale |
|-----------|---------|-----------|
| shiftHz | Yes | Sudden changes cause pitch artifacts |
| direction | No | Discrete enum, crossfade handled in DSP |
| modRate | No | LFO handles internally |
| modDepth | Yes | Could add, but LFO output is already smooth |
| feedback | Yes | Sudden changes cause clicks |
| mix | Yes | Standard practice for wet/dry blending |

**Smoothing Time**: Default 5ms (kDefaultSmoothingTimeMs from smoother.h)

**Implementation**:
```cpp
void prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate;

    // Configure smoothers (5ms default)
    shiftSmoother_.configure(5.0f, static_cast<float>(sampleRate));
    feedbackSmoother_.configure(5.0f, static_cast<float>(sampleRate));
    mixSmoother_.configure(5.0f, static_cast<float>(sampleRate));

    // Initialize to current values
    shiftSmoother_.snapTo(shiftHz_);
    feedbackSmoother_.snapTo(feedback_);
    mixSmoother_.snapTo(mix_);

    // ... rest of prepare
}
```

---

### 7. LFO Integration

**Question**: How to integrate the existing LFO primitive?

**Decision**: Use LFO directly, expose only rate and depth controls.

**Implementation**:
```cpp
// LFO setup in prepare():
modLFO_.prepare(sampleRate);
modLFO_.setWaveform(Waveform::Sine);  // Default sine
modLFO_.setFrequency(1.0f);            // Default 1Hz

// In process():
const float lfoValue = modLFO_.process();  // Returns [-1, +1]
const float effectiveShift = shiftHz_ + (modDepth_ * lfoValue);

// Use effectiveShift to update oscillator (after smoothing)
```

**API Methods**:
```cpp
void setModRate(float hz) noexcept {
    modLFO_.setFrequency(std::clamp(hz, 0.01f, 20.0f));
}

void setModDepth(float hz) noexcept {
    modDepth_ = std::clamp(hz, 0.0f, 500.0f);
}
```

**Note**: The LFO primitive handles waveform transitions internally with crossfading. For this component, we only expose Sine modulation (simplest, smoothest). Future versions could expose waveform selection.

---

## Dependency Analysis

### HilbertTransform (spec-094)

**API Used**:
- `prepare(double sampleRate)` - Initialize for sample rate
- `reset()` - Clear internal state
- `process(float input)` - Returns `HilbertOutput{i, q}`

**Latency**: Fixed 5 samples. Per spec assumption: "not compensated in the output."

**NaN Handling**: HilbertTransform resets and returns {0, 0} on NaN/Inf input. FrequencyShifter should also check inputs (FR-023).

### LFO (spec-003)

**API Used**:
- `prepare(double sampleRate)` - Initialize
- `reset()` - Clear state
- `process()` - Returns value in [-1, +1]
- `setFrequency(float hz)` - Set rate (clamped to [0.01, 20])
- `setWaveform(Waveform)` - Set waveform type

**Note**: LFO is non-copyable due to wavetable vectors. Use move semantics or in-place construction.

### OnePoleSmoother (spec-005)

**API Used**:
- `configure(float timeMs, float sampleRate)` - Set smoothing time
- `setTarget(float value)` - Set target value
- `process()` - Get current smoothed value
- `snapTo(float value)` - Immediately set both current and target
- `reset()` - Clear to zero

---

## Performance Considerations

**Per-Sample Cost Breakdown**:
| Operation | Cost |
|-----------|------|
| HilbertTransform.process() | ~32 multiplies (8 allpass stages) |
| Quadrature oscillator advance | 4 multiplies + 2 adds |
| SSB formula | 2-4 multiplies + 1-2 adds |
| Parameter smoothers (3x) | 9 multiplies + 6 adds |
| LFO.process() | ~10 ops (wavetable read + interpolation) |
| Feedback tanh | 1 function call (~20 cycles) |
| **Total** | ~60-80 multiplies equivalent |

**Memory**:
- HilbertTransform: ~128 bytes (filter states)
- LFO: ~16KB (wavetables)
- Smoothers: ~48 bytes
- **Total**: ~17KB per instance

**Estimated CPU**: Well under 0.5% at 44.1kHz mono (Layer 2 budget).

---

## Conclusion

All research questions have been resolved per spec clarifications:
1. Quadrature oscillator: Recurrence relation with 1024-sample renormalization
2. SSB formulas: Standard Hilbert-based, "Both" mode simplifies to I*cos
3. Feedback: tanh saturation on +/-1.0 range
4. Stereo: L = +shift, R = -shift
5. Aliasing: Document only, no mitigation
6. Smoothing: OnePoleSmoother for shift, feedback, mix
7. LFO: Use existing primitive, expose rate and depth

Ready for Phase 1 design and implementation.
