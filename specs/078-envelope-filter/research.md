# Research: Envelope Filter / Auto-Wah

**Feature**: 078-envelope-filter | **Date**: 2026-01-22

## Research Summary

This document consolidates research findings for implementing the EnvelopeFilter (Auto-Wah) processor.

---

## Decision 1: EnvelopeFollower Integration

**Decision**: Compose EnvelopeFollower from `envelope_follower.h` as member variable

**Rationale**:
- EnvelopeFollower already provides all needed functionality (attack/release, amplitude detection)
- Layer 2 composing Layer 2 is established pattern (BitcrusherProcessor, DuckingProcessor)
- No need to duplicate envelope tracking logic

**Alternatives Considered**:
- Implement custom envelope follower: Rejected - would duplicate tested code
- Use RMS mode: Rejected - spec says "Amplitude mode (default) for typical auto-wah response"

**API Usage**:
```cpp
EnvelopeFollower envFollower_;

// In prepare():
envFollower_.prepare(sampleRate, 512);  // maxBlockSize not used but required

// In process():
float envelope = envFollower_.processSample(gainedInput);
```

---

## Decision 2: SVF Integration

**Decision**: Compose SVF from `svf.h` as member variable

**Rationale**:
- SVF has "excellent modulation stability" (from spec) - critical for envelope-controlled cutoff
- Provides all three needed modes: Lowpass, Bandpass, Highpass
- Per-sample setCutoff() is safe (no clicks during modulation)

**Alternatives Considered**:
- Biquad filter: Rejected - less stable during fast modulation
- MultimodeFilter: Rejected - SVF specifically chosen in spec for stability

**API Usage**:
```cpp
SVF filter_;

// In prepare():
filter_.prepare(sampleRate);

// In process():
filter_.setCutoff(cutoffHz);  // Safe per-sample
float filtered = filter_.process(input);
```

---

## Decision 3: FilterType to SVFMode Mapping

**Decision**: Direct mapping with explicit enum qualification

**Rationale**:
- Spec clarification: "3 modes only (Lowpass, Bandpass, Highpass) with explicit enum qualification"
- Simple switch statement, no complex mapping logic needed

**Implementation**:
```cpp
SVFMode mapFilterType(FilterType type) noexcept {
    switch (type) {
        case FilterType::Lowpass:  return SVFMode::Lowpass;
        case FilterType::Bandpass: return SVFMode::Bandpass;
        case FilterType::Highpass: return SVFMode::Highpass;
    }
    return SVFMode::Lowpass;  // Default fallback
}
```

---

## Decision 4: Exponential Frequency Mapping

**Decision**: Use exponential (logarithmic) mapping for perceptually linear sweeps

**Rationale**:
- Musical intervals are logarithmic (octave = 2x frequency)
- Linear mapping would sound unnatural (most sweep at low end)
- Formula from spec: `cutoff = minFreq * pow(maxFreq/minFreq, envelope * depth)`

**Mathematical Verification**:
```
Given: minFreq = 200Hz, maxFreq = 2000Hz, envelope = 0.5, depth = 1.0
Expected: sqrt(200 * 2000) = 632Hz (geometric mean)

Calculation:
  ratio = 2000/200 = 10
  cutoff = 200 * pow(10, 0.5 * 1.0)
         = 200 * sqrt(10)
         = 200 * 3.162
         = 632.4Hz

Verified: Matches spec SC-008
```

---

## Decision 5: Sensitivity Application Point

**Decision**: Apply sensitivity gain only to envelope detection input, not to filter input

**Rationale**:
- Spec clarification: "Apply sensitivity gain only for envelope detection, pass original input to filter"
- Separates envelope tracking control from audio signal path
- Hot signals can trigger envelope without boosting audio level through filter

**Implementation**:
```cpp
float process(float input) noexcept {
    // Sensitivity affects envelope detection only
    const float gainedInput = input * sensitivityGain_;
    const float envelope = envFollower_.processSample(gainedInput);

    // Original input goes to filter
    const float filtered = filter_.process(input);

    // Mix original and filtered
    return input * (1.0f - mix_) + filtered * mix_;
}
```

---

## Decision 6: Envelope Clamping Strategy

**Decision**: Always clamp envelope to [0.0, 1.0] before frequency mapping

**Rationale**:
- Spec clarification: "maxFrequency is hard ceiling"
- Hot signals can push envelope > 1.0 without sensitivity clamping
- Prevents cutoff from exceeding configured maxFrequency

**Implementation**:
```cpp
const float clampedEnvelope = std::clamp(envelope, 0.0f, 1.0f);
const float modAmount = clampedEnvelope * depth_;
```

---

## Decision 7: Frequency Range Clamping

**Decision**: Setter-based clamping to maintain min < max invariant

**Rationale**:
- Spec clarification: "setMinFrequency() clamps to maxFreq-1Hz, setMaxFrequency() clamps to minFreq+1Hz"
- Always valid state, no need for runtime checks in process()

**Implementation**:
```cpp
void setMinFrequency(float hz) noexcept {
    const float maxValid = static_cast<float>(sampleRate_) * 0.4f;
    hz = std::clamp(hz, kMinFrequency, maxValid);

    // Ensure min < max
    if (hz >= maxFrequency_) {
        hz = maxFrequency_ - 1.0f;
    }
    minFrequency_ = hz;
}

void setMaxFrequency(float hz) noexcept {
    const float maxValid = static_cast<float>(sampleRate_) * 0.45f;
    hz = std::clamp(hz, kMinFrequency, maxValid);

    // Ensure max > min
    if (hz <= minFrequency_) {
        hz = minFrequency_ + 1.0f;
    }
    maxFrequency_ = hz;
}
```

---

## Decision 8: depth = 0 Behavior

**Decision**: Cutoff stays at starting position (minFreq for Up, maxFreq for Down)

**Rationale**:
- Spec clarification: "depth = 0 means cutoff stays at minFrequency (Up) or maxFrequency (Down)"
- Effectively disables modulation, creates static filter
- Useful for A/B testing effect vs no effect

**Mathematical Verification**:
```
Up direction, depth = 0:
  modAmount = envelope * 0 = 0
  cutoff = minFreq * pow(ratio, 0) = minFreq * 1 = minFreq

Down direction, depth = 0:
  modAmount = envelope * 0 = 0
  cutoff = maxFreq * pow(1/ratio, 0) = maxFreq * 1 = maxFreq

Verified: Matches expected behavior
```

---

## Decision 9: Q/Resonance Range

**Decision**: Use SVF's native Q range [0.5, 20.0] but spec says [0.5, 20.0]

**Rationale**:
- SVF supports Q up to 30.0 (kMaxQ = 30.0)
- Spec FR-016 limits to [0.5, 20.0] for safety
- Q = 20 is already very resonant (borderline self-oscillation)

**Implementation**:
```cpp
static constexpr float kMinResonance = 0.5f;
static constexpr float kMaxResonance = 20.0f;

void setResonance(float q) noexcept {
    q = std::clamp(q, kMinResonance, kMaxResonance);
    resonance_ = q;
    filter_.setResonance(q);
}
```

---

## Decision 10: Mix Implementation

**Decision**: Simple linear crossfade between dry and wet

**Rationale**:
- Spec uses simple 0.0-1.0 range
- No need for equal-power crossfade for this effect type
- Matches expected behavior: mix=0 is fully dry, mix=1 is fully wet

**Implementation**:
```cpp
void setMix(float dryWet) noexcept {
    mix_ = std::clamp(dryWet, 0.0f, 1.0f);
}

// In process():
return input * (1.0f - mix_) + filtered * mix_;
```

---

## Performance Analysis

**Expected CPU per sample**: < 100ns

| Component | Estimated Cost |
|-----------|---------------|
| Sensitivity gain multiply | ~1ns |
| EnvelopeFollower.processSample | ~10ns |
| Envelope clamp + depth multiply | ~2ns |
| Frequency mapping (pow) | ~15ns |
| SVF.setCutoff | ~5ns |
| SVF.process | ~10ns |
| Mix calculation | ~2ns |
| **Total** | ~45ns |

Meets SC-015 target (< 100ns per sample) with margin.

---

## Test Strategy Summary

1. **Envelope Tracking Tests** (SC-001, SC-002): Step input, measure settling time
2. **Filter Response Tests** (SC-004, SC-005, SC-006): Fixed cutoff, measure frequency response
3. **Frequency Mapping Tests** (SC-007, SC-008): Verify exponential at envelope=0.5
4. **Depth Tests** (SC-003): Compare depth=0.5 vs depth=1.0 sweep range
5. **Direction Tests** (SC-014): Up vs Down, verify inverse behavior
6. **Mix Tests** (SC-012, SC-013): Verify dry/wet blend
7. **Stability Tests** (SC-009, SC-010): High Q, long processing, no NaN
8. **Multi-Sample-Rate Tests** (SC-011): 44.1k, 48k, 96k, 192k
9. **Edge Case Tests**: Silent input, depth=0, freq clamping
10. **Performance Tests** (SC-015): Measure processing time
