# Research: FM/PM Synthesis Operator

**Feature**: 021-fm-pm-synth-operator
**Date**: 2026-02-05
**Status**: Complete

## Research Tasks

This document consolidates research findings for implementing the FM/PM Synthesis Operator.

---

## 1. FM vs PM: Understanding the Yamaha DX7 Approach

### Decision: Use Phase Modulation (PM)

**Rationale**: The Yamaha DX7 and all "FM synthesis" digital synthesizers actually use Phase Modulation (PM), not true Frequency Modulation. This is well-documented and the industry standard for digital FM synthesis.

### Key Differences

| Aspect | True FM | Phase Modulation (DX7-style) |
|--------|---------|------------------------------|
| Modulation Target | Frequency (derivative of phase) | Phase directly |
| Implementation | Requires integration | Direct addition |
| Digital Implementation | Complex (accumulator issues) | Simple (add to phase) |
| Spectrum | Equivalent for sinusoidal modulators | Equivalent for sinusoidal modulators |
| Industry Name | "FM Synthesis" | Called "FM Synthesis" despite being PM |

### Implementation Formula

```
output(t) = sin(2pi * f * t + modulation_input)
```

Where `modulation_input` is in radians. For a modulated carrier:
```
carrier_output = sin(2pi * fc * t + modulator_output * modulation_index)
```

**Alternatives Considered:**
- True FM (frequency modulation): Rejected - more complex, same result for sine modulators
- Direct wavetable phase warping: Rejected - that's Phase Distortion, different concept

---

## 2. Feedback Signal Flow Order

### Decision: Scale First, Then Limit

**Formula**: `phaseOffset = fastTanh(previousOutput * feedbackAmount)`

**Rationale**: This matches analog feedback behavior and provides the most musical feedback sweep:
- At low feedback (0.0-0.3): Nearly linear, smooth transition from sine
- At mid feedback (0.3-0.7): Gradual saturation, increasing harmonics
- At high feedback (0.7-1.0): Strong saturation, sawtooth-like character

**Alternatives Considered:**
- Limit first, then scale: `fastTanh(previousOutput) * feedbackAmount`
  - Rejected: Feedback amount wouldn't control saturation character
  - Would always apply full nonlinearity, then attenuate
- No limiting: Rejected - unbounded feedback causes "hunting" phenomenon (see below)
- DX7-style averaging filter: `y(t) = 0.5*(x(t) + x(t-1))`
  - Valid alternative used in original DX7 hardware
  - Acts as low-pass filter to prevent oscillatory instability
  - Tanh chosen for smoother saturation and more controllable timbral sweep

**The "Hunting" Phenomenon:**
Without feedback limiting, when the modulation index exceeds ~1.0, the phase increment can reverse direction. This causes:
- Oscillatory noise bursts at each period boundary
- Phase oscillates between small and large values every clock cycle
- Produces harsh, unpleasant digital artifacts

The DX7 solved this with a two-sample averaging filter. Modern digital implementations often use soft limiting (tanh) instead, which provides:
- Smoother transition into saturation
- More predictable spectral characteristics
- Musical feedback sweep from sine → sawtooth-like

### Feedback Path Implementation

```cpp
// In process():
float feedbackContribution = FastMath::fastTanh(previousRawOutput_ * feedbackAmount_);
float totalPM = externalPM + feedbackContribution;
osc_.setPhaseModulation(totalPM);
```

---

## 3. Phase Modulation Depth and Units

### Decision: No Additional Scaling (1:1 Radians)

**Formula**: A modulator output of +/-1.0 represents +/-1.0 radians of phase modulation.

**Rationale**:
- Keeps the system mathematically honest - the spec declares signals are in radians
- Modulation index = 1.0 at modulator level 1.0 is clean and intuitive
- Maps directly to FM synthesis theory
- Users who want stronger modulation can raise modulator amplitude or stack operators

**Modulation Index Relationship**:
- In FM theory, modulation index (I) = peak phase deviation in radians
- With our convention: I = modulator_amplitude * modulator_level
- At level 1.0 with full-scale modulator output: I = 1.0

**Alternatives Considered:**
- 2pi scaling (full cycle per unit): Rejected - would make I=6.28 at level 1.0
- Arbitrary scaling factor: Rejected - adds hidden constants, complicates understanding

---

## 4. Frequency Ratio Range

### Decision: Maximum Ratio = 16.0

**Rationale**:
- Above ratio ~16, FM sidebands are densely packed and aliasing-prone
- Matches SC-004 test specification (0.5 to 16.0)
- Provides sufficient range for bell-like inharmonic tones (ratios 1.41, 2.76, etc.)
- Effective frequency still clamped to Nyquist for safety

**Common FM Ratios**:
| Character | Ratio | Result |
|-----------|-------|--------|
| Unison | 1:1 | Same frequency |
| Octave | 2:1 | One octave higher |
| Fifth | 3:2 | Musical fifth |
| Bell-like | 1.41:1 | Metallic/bell |
| Organ | 1, 2, 3, 4 | Harmonic series |

**Alternatives Considered:**
- No maximum: Rejected - Nyquist edge cases become unpredictable
- Lower cap (8.0): Rejected - unnecessarily restrictive for high ratios

---

## 5. Combined Modulation Bounds

### Decision: No Additional Combined Bound

**Rationale**:
- Per-component limits are sufficient:
  - Feedback: bounded by tanh to approximately [-1, 1] radians
  - External PM: caller's responsibility
- FR-013 provides final output sanitization to [-2.0, 2.0]
- Large combined phase offsets produce "musically degraded but numerically stable" output
- Adding another nonlinearity would change the sound and mask which subsystem causes issues

**Worst Case Analysis**:
- Max feedback contribution: ~1.0 radians (from tanh)
- Max external PM: unbounded (caller control)
- Combined large PM: wraps through multiple cycles, produces complex spectrum
- Output: sine of wrapped phase, always [-1, 1] before level scaling

**Alternatives Considered:**
- Clamp combined PM to +/-pi: Rejected - changes sound, unnecessary
- Soft limit combined PM: Rejected - adds unwanted nonlinearity

---

## 6. Default Constructor Initialization

### Decision: Safe Silence Defaults

**Default State**:
```cpp
FMOperator::FMOperator() noexcept
    : frequency_(0.0f)      // Silence
    , ratio_(1.0f)          // Unison
    , feedbackAmount_(0.0f) // No feedback
    , level_(0.0f)          // Silence
    , sampleRate_(0.0)      // Unprepared
    , prepared_(false)
{}
```

**Rationale**:
- Satisfies FR-016 by construction: produces silence before prepare()
- Guarantees defined behavior under any call order
- No NaNs, no DC offset, no surprises
- Container-friendly (vector<FMOperator> works correctly)

**Alternatives Considered:**
- No default constructor: Rejected - hostile to containers, test setup
- "Ready to use" defaults (level=1.0): Rejected - violates FR-016
- Uninitialized members: Rejected - undefined behavior

---

## 7. WavetableOscillator Integration Pattern

### Decision: Composition with Internal Table Ownership

**Pattern**:
```cpp
class FMOperator {
private:
    WavetableOscillator osc_;    // Composed oscillator
    WavetableData sineTable_;    // Owned sine wavetable (~90 KB)
    float previousRawOutput_;     // For feedback
    // ... parameters
};
```

**Rationale**:
- Simplifies API - no external table management required
- Each FMOperator is self-contained
- Memory cost (~90 KB per operator) acceptable for typical use (4-6 operators)
- Future FM Voice can optimize with shared table if needed

**prepare() Implementation**:
```cpp
void prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate;

    // Generate sine wavetable (single harmonic at amplitude 1.0)
    float harmonics[] = {1.0f};
    generateMipmappedFromHarmonics(sineTable_, harmonics, 1);

    // Configure oscillator
    osc_.prepare(sampleRate);
    osc_.setWavetable(&sineTable_);

    prepared_ = true;
}
```

**Alternatives Considered:**
- External table injection: Rejected - complicates API for typical use
- Static shared table: Deferred to FM Voice optimization if needed
- Direct sine calculation: Rejected - WavetableOscillator provides mipmapping, interpolation

---

## 8. Real-Time Safety Analysis

### Verified Real-Time Safe Operations

| Operation | Real-Time Safe | Notes |
|-----------|----------------|-------|
| process() | Yes | noexcept, no allocations |
| setFrequency() | Yes | Single assignment |
| setRatio() | Yes | Single assignment + clamp |
| setFeedback() | Yes | Single assignment + clamp |
| setLevel() | Yes | Single assignment + clamp |
| reset() | Yes | Resets phase, clears feedback history |
| lastRawOutput() | Yes | Returns cached value |

### Non-Real-Time Safe Operations

| Operation | Why Not RT-Safe | When Called |
|-----------|-----------------|-------------|
| prepare() | Generates wavetable (FFT/IFFT) | Init time, sample rate change |
| Constructor | Initializes members | Object creation |

---

## 9. Output Sanitization Pattern

### Decision: Follow WavetableOscillator Pattern

**Implementation** (from wavetable_oscillator.h line 271-278):
```cpp
[[nodiscard]] static float sanitize(float x) noexcept {
    const auto bits = std::bit_cast<uint32_t>(x);
    const bool isNan = ((bits & 0x7F800000u) == 0x7F800000u) &&
                       ((bits & 0x007FFFFFu) != 0);
    x = isNan ? 0.0f : x;
    x = (x < -2.0f) ? -2.0f : x;
    x = (x > 2.0f) ? 2.0f : x;
    return x;
}
```

**Rationale**:
- Branchless NaN detection using bit manipulation
- Works correctly with -ffast-math disabled for this file
- Bounds output to [-2.0, 2.0] matching FR-013 requirement
- Follows established codebase pattern

---

## 10. Performance Considerations

### Expected Performance Profile

| Operation | Cost | Notes |
|-----------|------|-------|
| process() | ~20-30 cycles | Wavetable lookup + interpolation + tanh |
| fastTanh | ~5-10 cycles | Pade approximant (3x faster than std::tanh) |
| Wavetable read | ~10-15 cycles | Cubic Hermite interpolation |

### Memory Footprint

| Component | Size | Notes |
|-----------|------|-------|
| WavetableData | ~90 KB | 11 levels x 2052 floats x 4 bytes |
| WavetableOscillator | ~48 bytes | Phase accumulator + state |
| FMOperator members | ~32 bytes | Parameters + previousOutput |
| **Total per operator** | **~90 KB** | Dominated by wavetable |

### FM Voice Projection (Phase 9)

For a 6-operator FM voice:
- Memory: 6 x 90 KB = ~540 KB (could optimize with shared table to ~90 KB)
- CPU: Well under 0.5% for all 6 operators at 44.1 kHz

---

## Summary of Research Decisions

| Topic | Decision | Key Rationale |
|-------|----------|---------------|
| FM vs PM | Use Phase Modulation | Industry standard, simpler implementation |
| Feedback order | Scale first, then tanh | Musical sweep, matches analog behavior |
| PM depth | 1:1 radians (no scaling) | Mathematically honest, intuitive |
| Max ratio | 16.0 | Sufficient range, avoids aliasing issues |
| Combined bounds | No additional limit | Per-component limits sufficient |
| Default state | Safe silence | FR-016 compliance, container-friendly |
| Table ownership | Internal to FMOperator | Simpler API, acceptable memory cost |
| Sanitization | WavetableOscillator pattern | Branchless, proven, matches codebase |

---

## References

Technical sources consulted during research:

- [CCRMA FM Introduction](https://ccrma.stanford.edu/software/snd/snd/fm.html) - Comprehensive FM synthesis theory, feedback behavior, Bessel functions
- [Ken Shirriff's DX7 Reverse Engineering](http://www.righto.com/2021/11/reverse-engineering-yamaha-dx7.html) - PM vs FM distinction, chip-level implementation
- [DX7 Algorithm Implementation](http://www.righto.com/2021/12/yamaha-dx7-chip-reverse-engineering.html) - Feedback averaging filter, anti-hunting mechanism
- [Yamaha DX7 Technical Analysis](https://ajxs.me/blog/Yamaha_DX7_Technical_Analysis.html) - Detailed technical analysis
- [CMU FM Synthesis Lecture](https://www.cs.cmu.edu/~music/icm-online/readings/fm-synthesis/fm_synthesis.pdf) - Academic FM synthesis reference
