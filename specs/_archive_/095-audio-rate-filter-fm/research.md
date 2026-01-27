# Research: Audio-Rate Filter FM

**Feature**: 095-audio-rate-filter-fm | **Date**: 2026-01-24

## Summary

Research findings for implementing an audio-rate filter FM processor that modulates SVF filter cutoff at audio rates (20Hz-20kHz) for metallic, bell-like, and aggressive timbres.

---

## Research Questions and Answers

### Q1: Wavetable Oscillator Implementation Strategy

**Question**: What is the best approach for implementing the internal modulator oscillator?

**Decision**: Wavetable with linear interpolation using 2048-sample tables

**Rationale**:
- Per spec clarification, use wavetable with linear interpolation
- LFO.h in this codebase already uses this pattern successfully (kTableSize = 2048)
- Linear interpolation provides adequate quality for a modulator signal (not directly audible)
- Pre-computed tables avoid runtime trigonometric function calls
- 2048 samples provides sufficient resolution for audio-rate frequencies

**Analysis**:
- At 20kHz modulator frequency and 44.1kHz sample rate: phase increment = 20000/44100 = 0.453
- With 2048 samples, step through approximately 927 samples per cycle
- Linear interpolation error < 0.1% for sine waveform
- Memory footprint: 4 tables * 2048 samples * 4 bytes = 32KB (acceptable)

**Alternatives Considered**:
1. **Direct computation (std::sin)**: Higher CPU cost per sample, no pre-computation
2. **Band-limited wavetable**: Overkill for modulator that's not directly audible
3. **Higher-order interpolation (cubic)**: Marginal quality improvement, higher CPU cost

---

### Q2: SVF Integration at Oversampled Rate

**Question**: How should the SVF be prepared when oversampling is enabled?

**Decision**: Call SVF.prepare() with oversampled rate (sampleRate * oversamplingFactor)

**Rationale** (per spec clarification):
- SVF coefficients depend on sample rate via g = tan(pi * fc / fs)
- When processing at 4x oversampled rate, fs is 4x higher
- Correct filter behavior requires coefficients computed for the oversampled rate
- Without this, cutoff frequency would be incorrect (4x too low)

**Implementation**:
```cpp
void updateSVFForOversampling() {
    const double oversampledRate = baseSampleRate_ * oversamplingFactor_;
    svf_.prepare(oversampledRate);
    // Also update cutoff limits based on new Nyquist
}
```

---

### Q3: Self-Modulation Stability

**Question**: How should self-modulation (feedback FM) be handled to prevent instability?

**Decision**: Hard-clip filter output to [-1, +1] before using as modulator

**Rationale** (per spec clarification):
- FM formula: modulatedCutoff = carrierCutoff * 2^(modulator * fmDepth)
- Without clipping, if filter output exceeds 1.0, frequency can grow exponentially
- At fmDepth=4 and modulator=2.0: frequency multiplier = 2^8 = 256x (unstable)
- Hard clipping to [-1, +1] bounds maximum frequency excursion to +/- fmDepth octaves
- Simpler than soft limiting and mathematically sufficient

**Stability Analysis**:
- Max stable excursion: +/- 6 octaves (2^6 = 64x frequency)
- At carrier 1kHz, max modulated range: 15.6Hz to 64kHz
- Clipped to Nyquist * 0.495, always stable

---

### Q4: Oversampling Factor Clamping

**Question**: How should invalid oversampling factor values be handled?

**Decision**: Clamp to nearest valid value (0/1->1, 3->2, 5+->4)

**Rationale** (per spec clarification):
- Only valid values are 1, 2, and 4 (Oversampler template only supports 2x and 4x)
- Factor 1 means "no oversampling" (bypass oversampler entirely)
- Clamping provides predictable, deterministic behavior
- No error state or exception - graceful degradation

**Mapping Table**:
| Input | Output | Rationale |
|-------|--------|-----------|
| <= 1 | 1 | No oversampling |
| 2 | 2 | Valid - 2x |
| 3 | 2 | Round down to nearest power of 2 |
| >= 4 | 4 | Maximum supported |

---

### Q5: Waveform Quality Requirements

**Question**: What THD limits apply to each waveform type?

**Decision**: Sine <0.1% THD, Triangle <1% THD, Saw/Square no limits

**Rationale** (per spec clarification):
- Sine and triangle are expected to be "clean" waveforms
- Sawtooth and square inherently contain harmonics - this is their nature, not distortion
- THD measurement is only meaningful for waveforms intended to be pure

**Verification Method**:
```cpp
// Sine: Measure harmonic content, verify < 0.1% (0.001) of fundamental
// Triangle: Measure harmonic content, verify < 1% (0.01) of fundamental
// Saw/Square: Verify bounded output, no stability tests needed
```

---

### Q6: FM Depth Calculation Formula

**Question**: How is the modulated cutoff frequency calculated?

**Decision**: Exponential mapping: modulatedCutoff = carrierCutoff * 2^(modulator * fmDepth)

**Rationale**:
- Per spec FR-013
- Octaves are logarithmic: 1 octave up = 2x frequency, 1 octave down = 0.5x frequency
- Modulator in [-1, +1] range maps to [-fmDepth, +fmDepth] octaves
- This matches musical/synthesizer conventions for FM depth

**Examples**:
| Carrier | Depth | Modulator | Multiplier | Result |
|---------|-------|-----------|------------|--------|
| 1000 Hz | 1 oct | +1.0 | 2^1 = 2 | 2000 Hz |
| 1000 Hz | 1 oct | -1.0 | 2^-1 = 0.5 | 500 Hz |
| 1000 Hz | 2 oct | +1.0 | 2^2 = 4 | 4000 Hz |
| 1000 Hz | 2 oct | -1.0 | 2^-2 = 0.25 | 250 Hz |

---

### Q7: Latency Characteristics

**Question**: What contributes to processing latency?

**Decision**: Latency = Oversampler latency only (0 for Economy/ZeroLatency mode)

**Components Analyzed**:
| Component | Latency | Notes |
|-----------|---------|-------|
| SVF | 0 samples | IIR filter, instant response |
| Internal oscillator | 0 samples | Wavetable lookup, no delay |
| Oversampler (Economy) | 0 samples | IIR filters, zero latency |
| Oversampler (FIR) | 15-31 samples | Depends on quality setting |

**Implementation**:
```cpp
[[nodiscard]] size_t getLatency() const noexcept {
    if (oversamplingFactor_ == 1) return 0;
    // Delegate to oversampler's latency reporting
    return (oversamplingFactor_ == 2)
        ? oversampler2x_.getLatency()
        : oversampler4x_.getLatency();
}
```

---

## Existing Codebase Patterns

### Pattern: Layer 2 Processor Structure (from envelope_filter.h)

```cpp
class ProcessorName {
public:
    // Lifecycle
    void prepare(double sampleRate);
    void reset() noexcept;

    // Parameters
    void setParameter(Type value);
    [[nodiscard]] Type getParameter() const noexcept;

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // Query
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    // Composed Layer 1 components
    SVF filter_;

    // Configuration
    double sampleRate_ = 44100.0;
    bool prepared_ = false;

    // Parameters with defaults
    float param_ = defaultValue;
};
```

### Pattern: Wavetable with Linear Interpolation (from lfo.h)

```cpp
static constexpr size_t kTableSize = 2048;
static constexpr size_t kTableMask = kTableSize - 1;

std::array<float, kTableSize> table_;

[[nodiscard]] float readWavetable(double phase) const noexcept {
    double scaledPhase = phase * static_cast<double>(kTableSize);
    size_t index0 = static_cast<size_t>(scaledPhase);
    size_t index1 = (index0 + 1) & kTableMask;
    float frac = static_cast<float>(scaledPhase - static_cast<double>(index0));
    return table_[index0] + frac * (table_[index1] - table_[index0]);
}
```

### Pattern: Oversampler Integration (from multimode_filter.h)

```cpp
// Declare oversamplers for different factors
Oversampler<2, 1> oversampler2x_;
Oversampler<4, 1> oversampler4x_;
std::vector<float> oversampledBuffer_;

void prepare(double sampleRate, size_t maxBlockSize) {
    oversampler2x_.prepare(sampleRate, maxBlockSize);
    oversampler4x_.prepare(sampleRate, maxBlockSize);
    oversampledBuffer_.resize(maxBlockSize * 4);  // Max factor
}
```

---

## Dependencies Verified

| Dependency | Location | API Verified |
|------------|----------|--------------|
| SVF | primitives/svf.h | prepare(), setCutoff(), setResonance(), setMode(), process() |
| Oversampler | primitives/oversampler.h | prepare(), upsample(), downsample(), getLatency() |
| kPi, kTwoPi | core/math_constants.h | inline constexpr float |
| flushDenormal | core/db_utils.h | detail::flushDenormal() |
| isNaN, isInf | core/db_utils.h | detail::isNaN(), detail::isInf() |

---

## Open Questions (Resolved)

All clarifications from spec have been incorporated:

1. [RESOLVED] Self-modulation scaling -> Hard-clip to [-1, +1]
2. [RESOLVED] Oscillator implementation -> Wavetable with linear interpolation
3. [RESOLVED] SVF sample rate -> Use oversampled rate
4. [RESOLVED] Invalid oversampling factors -> Clamp to nearest valid
5. [RESOLVED] Waveform quality criteria -> Sine <0.1%, Triangle <1%, others no limit
