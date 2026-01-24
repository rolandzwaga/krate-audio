# Research: Hilbert Transform Implementation

**Spec**: 094-hilbert-transform | **Date**: 2026-01-24

## Summary

Research completed for implementing a Hilbert transform using allpass filter cascade approximation. All NEEDS CLARIFICATION items from the specification have been resolved.

---

## Decision 1: Allpass Coefficients

**Decision**: Use Olli Niemitalo's optimized coefficients

**Rationale**:
- These coefficients are specifically optimized for wideband Hilbert transform approximation
- Achieve +/- 0.7 degree phase accuracy over 0.002 to 0.998 of Nyquist frequency
- Well-documented and widely used in audio DSP applications
- Sample-rate independent (work for normalized frequency range)

**Alternatives Considered**:
- Polyphase filter bank: More complex, higher latency, better accuracy at band edges
- FIR Hilbert transform: Linear phase but requires hundreds of taps for comparable bandwidth
- Different allpass coefficient sets: Niemitalo's are optimal for 4-stage cascades

**Coefficients**:
```cpp
// Path 1 (In-phase): 4 Allpass1Pole instances + 1-sample delay
constexpr float kHilbertPath1Coeffs[4] = {
    0.6923878f,
    0.9360654322959f,
    0.9882295226860f,
    0.9987488452737f
};

// Path 2 (Quadrature): 4 Allpass1Pole instances
constexpr float kHilbertPath2Coeffs[4] = {
    0.4021921162426f,
    0.8561710882420f,
    0.9722909545651f,
    0.9952884791278f
};
```

**Source**: [Olli Niemitalo - Hilbert Transform](https://yehar.com/blog/?p=368)

---

## Decision 2: Latency Value

**Decision**: 5 samples fixed latency

**Rationale**:
- Path 1 has 4 Allpass1Pole instances + 1 explicit sample delay = ~5 samples total group delay
- Path 2 has 4 Allpass1Pole instances = ~4 samples group delay
- The 1-sample explicit delay in Path 1 aligns the two paths for correct phase relationship
- This latency is sample-rate independent (fixed at 5 samples regardless of sample rate)

**Alternatives Considered**:
- Dynamic latency calculation: Unnecessary, group delay is effectively fixed for these coefficients
- Higher order filters (more stages): Would increase latency for marginal accuracy improvement

---

## Decision 3: Reusing Allpass1Pole

**Decision**: Reuse existing Allpass1Pole class with setCoefficient()

**Rationale**:
- Allpass1Pole already implements the correct difference equation: y[n] = a*x[n] + x[n-1] - a*y[n-1]
- Has proper NaN/Inf handling built in
- Has denormal flushing built in
- Well-tested (allpass_1pole_test.cpp has 1050 lines of tests)
- Avoids code duplication and ODR issues

**Usage Pattern**:
```cpp
// In prepare()
for (int j = 0; j < 4; ++j) {
    ap1_[j].prepare(sampleRate);
    ap1_[j].setCoefficient(kHilbertPath1Coeffs[j]);
    ap2_[j].prepare(sampleRate);
    ap2_[j].setCoefficient(kHilbertPath2Coeffs[j]);
}
```

**Alternatives Considered**:
- Inline allpass implementation: Would duplicate code, lose built-in safety features
- New specialized HilbertAllpass class: Unnecessary complexity, no benefit

---

## Decision 4: Settling Time

**Decision**: 5 samples settling time after reset()

**Rationale**:
- Matches the group delay/latency
- After reset, all 8 Allpass1Pole instances have zero state
- The first few samples propagate through the cascade
- After 5 samples, steady-state phase relationship is established

**Verification Method**: Test by processing impulse after reset, verify phase accuracy from sample 6 onward

---

## Decision 5: Sample Rate Validation

**Decision**: Clamp to valid range [22050, 192000] Hz silently

**Rationale**:
- Maintains noexcept guarantee (no exceptions for invalid input)
- Real-time safe (no error reporting mechanisms needed)
- Consistent with other primitives in the codebase (e.g., Allpass1Pole defaults invalid to 44100)
- Edge values chosen to support:
  - 22050 Hz: Half of CD quality, lowest practical professional rate
  - 192000 Hz: Highest professional sample rate

**Clamping Logic**:
```cpp
void prepare(double sampleRate) noexcept {
    constexpr double kMinSampleRate = 22050.0;
    constexpr double kMaxSampleRate = 192000.0;
    sampleRate_ = std::clamp(sampleRate, kMinSampleRate, kMaxSampleRate);
    // ... configure allpass filters
}
```

---

## Decision 6: NaN/Inf Handling

**Decision**: Reset state and output zeros on NaN/Inf input

**Rationale**:
- Consistent with Allpass1Pole behavior (already built-in)
- Prevents corruption from propagating through feedback paths
- Real-time safe (no exceptions, deterministic behavior)
- Processing can continue normally after invalid sample

**Implementation**: Allpass1Pole already handles this - no additional code needed in HilbertTransform

---

## Phase Accuracy Analysis

### Theoretical Accuracy

The Niemitalo coefficients provide:
- +/- 0.7 degree accuracy over 0.002 to 0.998 of Nyquist

### Practical Accuracy at Common Sample Rates

| Sample Rate | Effective Low Freq | Effective High Freq | Accuracy Band |
|-------------|-------------------|---------------------|---------------|
| 44100 Hz | ~44 Hz | ~21.6 kHz | 40 Hz - 20 kHz |
| 48000 Hz | ~48 Hz | ~23.5 kHz | 40 Hz - 22 kHz |
| 96000 Hz | ~96 Hz | ~47 kHz | 40 Hz - 44 kHz |
| 192000 Hz | ~192 Hz | ~94 kHz | 40 Hz - 88 kHz |

Note: Low frequency cutoff in the spec (40 Hz) is conservative; actual accuracy extends lower.

---

## Memory Layout Analysis

```cpp
class HilbertTransform {
    Allpass1Pole ap1_[4];  // 4 x ~24 bytes = ~96 bytes
    float delay1_ = 0.0f;  // 4 bytes
    Allpass1Pole ap2_[4];  // 4 x ~24 bytes = ~96 bytes
    double sampleRate_;    // 8 bytes
};
// Total: ~204 bytes (acceptable for Layer 1 primitive)
```

---

## References

1. [Olli Niemitalo - Hilbert Transform](https://yehar.com/blog/?p=368) - Original coefficient derivation
2. [DSPRelated - Hilbert Transform Design](https://www.dsprelated.com/freebooks/sasp/Hilbert_Transform_Design_Example.html) - Theory background
3. [CCRMA - Hilbert Transform for SSB](https://ccrma.stanford.edu/~jos/st/Hilbert_Transform.html) - Stanford reference
4. Existing codebase: `dsp/include/krate/dsp/primitives/allpass_1pole.h` - Implementation pattern
