# Research: Comb Filters

**Feature**: 074-comb-filter | **Date**: 2026-01-21

## Research Questions Resolved

### Q1: Damping Coefficient Range & Interpretation

**Decision**: Range [0.0, 1.0] where 0.0 = no damping (bright/pass-through), 1.0 = maximum damping (dark/heavy lowpass)

**Rationale**: This follows the Freeverb convention where the damping parameter directly controls the one-pole lowpass coefficient. The formula `LP(x) = (1-d)*x + d*LP_prev` means:
- When d=0: Output = input (no filtering, all frequencies pass)
- When d=1: Output = previous output (maximum filtering, DC only)

**Alternatives Considered**:
- Cutoff frequency in Hz: Rejected because it requires per-sample coefficient calculation from frequency, adding overhead
- Inverted range (0=dark, 1=bright): Rejected to match Freeverb convention

**Source**: [DSPRelated - Lowpass Feedback Comb Filter](https://www.dsprelated.com/freebooks/pasp/Lowpass_Feedback_Comb_Filter.html)

---

### Q2: Denormal Flushing Locations

**Decision**: Flush denormals only in feedback path state variables:
- FeedbackComb: `dampingState_` (one-pole LP state)
- SchroederAllpass: `feedbackState_` (y[n-D] state)

**Do NOT flush**:
- DelayLine buffer (DelayLine handles its own denormal flushing)
- FeedforwardComb (no feedback path, no state accumulation)

**Rationale**: Denormals accumulate in feedback loops due to repeated multiplication by values < 1. The DelayLine already manages its own buffer. Flushing state variables per-sample is sufficient and avoids unnecessary overhead.

**Source**: Constitution Principle II (Real-Time Safety), spec clarification session

---

### Q3: Block Processing Bit-Identical Requirements

**Decision**: `processBlock()` MUST be bit-identical to sequential `process()` calls for testing/verification purposes.

**Rationale**: This ensures tests can verify correctness by comparing block and sample-by-sample outputs. SIMD optimizations that differ at the LSB are acceptable ONLY if they provide measurable performance improvement and are clearly documented.

**Implementation**: Initial implementation will use a simple loop calling `process()` for each sample. SIMD optimization can be added later if performance requires it.

**Source**: Spec clarification session

---

### Q4: Schroeder Allpass Formulation

**Decision**: Use standard two-state formulation: `y[n] = -g*x[n] + x[n-D] + g*y[n-D]`

**Rationale**:
- Cleaner separation of concerns (input, delayed input, delayed output)
- Better for modulation (linear interpolation on both delay taps)
- More intuitive API (coefficient g directly controls diffusion)

**Alternatives Considered**:
- Single-delay-line formulation (used by AllpassStage in diffusion_network.h):
  ```cpp
  v[n] = x[n] + g * v[n-D]
  y[n] = -g * v[n] + v[n-D]
  ```
  This requires allpass interpolation to preserve energy and is optimized for fixed delays in diffusion networks. Not suitable for modulated delays.

**Source**: [Stanford CCRMA - Schroeder Allpass Sections](https://ccrma.stanford.edu/~jos/pasp/Schroeder_Allpass_Sections.html)

---

### Q5: Interpolation Method

**Decision**: Use `DelayLine::readLinear()` for all fractional delay reads.

**Rationale**:
- Linear interpolation supports smooth modulation without artifacts
- Allpass interpolation (readAllpass) causes artifacts when delay time is modulated
- Per Constitution Principle X: "Linear/Cubic for modulated delays"

**Alternatives Considered**:
- Allpass interpolation: Better frequency response for fixed delays but causes "zipper" artifacts when modulated
- Cubic interpolation: Higher quality but more CPU, overkill for comb filters

**Source**: CLAUDE.md, dsp-architecture skill

---

### Q6: Feedback Stability Limits

**Decision**: Clamp feedback coefficient to [-0.9999f, 0.9999f]

**Rationale**:
- |g| < 1 is required for DC stability (from comb filter theory)
- 0.9999f provides very long decay (T60 ~ D * 14000 samples) while maintaining stability
- Negative feedback is useful for certain effects (phase-inverted resonances)

**Alternatives Considered**:
- 0.999f: Too aggressive limiting, audible difference in decay time
- 1.0f: Unstable at DC, would require DC blocking in feedback path
- [0, 0.9999f] (positive only): Limits creative possibilities

**Source**: [Number Analytics - Comb Filters Theory](https://www.numberanalytics.com/blog/comb-filters-theory-and-practice)

---

## Codebase Compatibility Analysis

### Existing AllpassStage vs New SchroederAllpass

| Aspect | AllpassStage (Layer 2) | SchroederAllpass (Layer 1) |
|--------|------------------------|---------------------------|
| Location | diffusion_network.h | comb_filter.h |
| Layer | 2 (Processor) | 1 (Primitive) |
| Formulation | Single-delay-line | Standard two-state |
| Interpolation | Allpass (readAllpass) | Linear (readLinear) |
| Use case | Fixed delays in diffusion | General purpose, modulation |
| Coefficient | Fixed at 0.618 (golden ratio) | Configurable [-0.9999, 0.9999] |

**No ODR conflict**: Different names, different layers, different use cases.

### DelayLine API Compatibility

The existing DelayLine API fully supports the comb filter requirements:
- `prepare(sampleRate, maxDelaySeconds)` - Initialize buffer
- `reset()` - Clear state
- `write(sample)` - Write input
- `readLinear(delaySamples)` - Read with linear interpolation

No modifications to DelayLine are required.

---

## Performance Considerations

### Memory Layout

Each filter instance overhead (excluding DelayLine buffer):
- FeedforwardComb: gain_, delaySamples_, sampleRate_ = ~16 bytes
- FeedbackComb: feedback_, damping_, dampingState_, delaySamples_, sampleRate_ = ~24 bytes
- SchroederAllpass: coefficient_, feedbackState_, delaySamples_, sampleRate_ = ~20 bytes

All well under the 64-byte overhead limit (SC-005).

### Processing Cost

Per-sample operations:
- FeedforwardComb: 1 readLinear + 1 write + 1 multiply + 1 add = ~4 ops
- FeedbackComb: 1 readLinear + 1 write + 2 multiply + 2 add + 1 flushDenormal = ~7 ops
- SchroederAllpass: 1 readLinear + 1 write + 3 multiply + 2 add + 1 flushDenormal = ~8 ops

All should easily meet the <20ns/sample target (SC-004).

---

## References

1. [Stanford CCRMA - Schroeder Allpass Sections](https://ccrma.stanford.edu/~jos/pasp/Schroeder_Allpass_Sections.html)
2. [Valhalla DSP - Reverb Diffusion](https://valhalladsp.com/2011/01/21/reverbs-diffusion-allpass-delays-and-metallic-artifacts/)
3. [DSPRelated - Lowpass Feedback Comb Filter](https://www.dsprelated.com/freebooks/pasp/Lowpass_Feedback_Comb_Filter.html)
4. [Number Analytics - Comb Filters Theory](https://www.numberanalytics.com/blog/comb-filters-theory-and-practice)
5. [Wikipedia - Comb Filter](https://en.wikipedia.org/wiki/Comb_filter)
