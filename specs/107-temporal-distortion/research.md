# Research: Temporal Distortion Processor

**Feature Branch**: `107-temporal-distortion`
**Research Date**: 2026-01-26

## Overview

This document captures research findings for the Temporal Distortion Processor - a Layer 2 DSP processor that modulates waveshaper drive based on signal history (envelope, derivative, inverse envelope, or hysteresis).

---

## 1. Existing Components Analysis

### 1.1 EnvelopeFollower (REUSE)

**Location**: `dsp/include/krate/dsp/processors/envelope_follower.h`
**Layer**: 2

The existing EnvelopeFollower provides exactly what we need for envelope tracking:

```cpp
enum class DetectionMode : uint8_t { Amplitude, RMS, Peak };

class EnvelopeFollower {
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    float processSample(float input) noexcept;
    void setMode(DetectionMode mode) noexcept;
    void setAttackTime(float ms) noexcept;   // 0.1-500ms
    void setReleaseTime(float ms) noexcept;  // 1-5000ms
    float getCurrentValue() const noexcept;
};
```

**Decision**: REUSE directly. Set to RMS mode as specified in clarifications.

**Key API Details Verified**:
- `processSample()` returns the current envelope value [0.0, 1.0+]
- Attack/release time ranges match spec requirements
- RMS mode uses asymmetric smoothing for dynamics-appropriate response
- Denormal flushing included

### 1.2 Waveshaper (REUSE)

**Location**: `dsp/include/krate/dsp/primitives/waveshaper.h`
**Layer**: 1

The Waveshaper primitive provides all needed saturation curves:

```cpp
enum class WaveshapeType : uint8_t {
    Tanh, Atan, Cubic, Quintic, ReciprocalSqrt, Erf, HardClip, Diode, Tube
};

class Waveshaper {
    void setType(WaveshapeType type) noexcept;
    void setDrive(float drive) noexcept;  // >= 0.0
    void setAsymmetry(float bias) noexcept;  // [-1, 1]
    float process(float x) const noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
};
```

**Decision**: REUSE. The drive can be modulated per-sample by calling `setDrive()` then `process()`.

**Key API Details Verified**:
- `setDrive()` takes absolute value (negative treated as positive)
- When drive is 0.0, process() returns 0.0
- Stateless processing - can change drive between samples

### 1.3 OnePoleHP (REUSE for Derivative)

**Location**: `dsp/include/krate/dsp/primitives/one_pole.h`
**Layer**: 1

Per clarifications, Derivative mode uses a highpass filter on the envelope signal:

```cpp
class OnePoleHP {
    void prepare(double sampleRate) noexcept;
    void setCutoff(float hz) noexcept;
    float process(float input) noexcept;
    void reset() noexcept;
};
```

**Decision**: REUSE. A lowpass filtered derivative = highpass on envelope gives rate-of-change with smoothing.

**Rationale**:
- True derivative (x[n] - x[n-1]) is noisy and sample-rate dependent
- Highpass filter on envelope provides:
  - Smoothed rate of change
  - Sample-rate independence (via cutoff frequency)
  - Natural decay toward zero for sustained signals

### 1.4 OnePoleSmoother (REUSE)

**Location**: `dsp/include/krate/dsp/primitives/smoother.h`
**Layer**: 1

For parameter smoothing to prevent zipper noise:

```cpp
class OnePoleSmoother {
    void configure(float smoothTimeMs, float sampleRate) noexcept;
    void setTarget(float target) noexcept;
    float process() noexcept;
    void snapTo(float value) noexcept;
    bool isComplete() const noexcept;
};
```

**Decision**: REUSE for drive smoothing and hysteresis decay.

### 1.5 DCBlocker (COMPOSE if needed)

**Location**: `dsp/include/krate/dsp/primitives/dc_blocker.h`
**Layer**: 1

Available for post-processing if asymmetric waveshaping generates DC offset.

**Decision**: Document as optional composition - per spec assumption, DC blocking handled externally.

---

## 2. Drive Modulation Calculation

### 2.1 Reference Level Definition

**Clarification**: Reference level is **-12 dBFS RMS**

This converts to linear amplitude:
```
referenceLevel = 10^(-12/20) = 0.251189 (approximately 0.25)
```

### 2.2 EnvelopeFollow Mode

Formula: `effectiveDrive = baseDrive + driveModulation * baseDrive * (envelope / referenceLevel - 1)`

When envelope = referenceLevel: effectiveDrive = baseDrive (FR-011)
When envelope > referenceLevel: effectiveDrive > baseDrive (more distortion)
When envelope < referenceLevel: effectiveDrive < baseDrive (less distortion)

**Implementation**:
```cpp
float envelopeRatio = envelope / kReferenceLevel;
float modAmount = driveModulation_ * baseDrive_ * (envelopeRatio - 1.0f);
float effectiveDrive = std::max(0.0f, baseDrive_ + modAmount);
```

### 2.3 InverseEnvelope Mode

Formula: `effectiveDrive = baseDrive * (1.0 + driveModulation * (referenceLevel / max(envelope, floor) - 1))`

**Clarification**: Safe maximum drive = 20.0 (FR-013)

**Implementation**:
```cpp
constexpr float kFloor = 0.001f;  // Prevent division by zero
float safeEnvelope = std::max(envelope, kFloor);
float inverseRatio = kReferenceLevel / safeEnvelope;
float rawDrive = baseDrive_ * (1.0f + driveModulation_ * (inverseRatio - 1.0f));
float effectiveDrive = std::min(rawDrive, kMaxSafeDrive);  // 20.0
```

### 2.4 Derivative Mode

Formula: `effectiveDrive = baseDrive + driveModulation * baseDrive * |derivative| * sensitivity`

The derivative is computed via highpass filter on envelope (per clarification):
```cpp
float derivative = derivativeFilter_.process(envelope);  // OnePoleHP
float absDerivative = std::abs(derivative);
float modAmount = driveModulation_ * baseDrive_ * absDerivative * kDerivativeSensitivity;
float effectiveDrive = std::max(0.0f, baseDrive_ + modAmount);
```

**Cutoff frequency selection**: 10 Hz chosen from the ~5-20 Hz range as the optimal balance:
- Below 5 Hz: Too slow, misses fast transients
- Above 20 Hz: Too noisy, picks up high-frequency envelope ripple
- 10 Hz: Sweet spot for typical transient attack times (10-100ms), provides both good transient detection and noise rejection

### 2.5 Hysteresis Mode

**Clarification**: Simple state memory with exponential decay (not physical Jiles-Atherton)

**Concept**: The processing depends on whether the signal has been rising or falling recently.

**Implementation Approach**:
```cpp
// Memory state tracks recent signal trajectory
float signalChange = envelope - prevEnvelope_;
hysteresisState_ = hysteresisState_ * decayCoeff_ + signalChange;

// Drive modulation based on state (positive = rising, negative = falling)
float stateInfluence = hysteresisState_ * hysteresisDepth_;
float effectiveDrive = baseDrive_ + stateInfluence * baseDrive_ * driveModulation_;
```

Where `decayCoeff_` is calculated from `hysteresisDecay` parameter:
```cpp
decayCoeff_ = exp(-1000.0 / (hysteresisDecayMs_ * sampleRate_));  // ~5x for settling
```

---

## 3. Mode Switching Without Artifacts

**Requirement**: FR-002 - No zipper noise on mode switch

**Approach**:
1. Smooth the effectiveDrive value regardless of mode
2. Use OnePoleSmoother with 5ms smoothing time
3. When mode changes, drive calculation changes but output is smoothed

This is simpler than crossfading between modes since all modes ultimately produce a drive value that is then smoothed.

---

## 4. Performance Considerations

### 4.1 CPU Budget

Layer 2 target: < 0.5% CPU at 44.1kHz stereo (SC-010)

**Component costs**:
- EnvelopeFollower: ~0.1% (per existing benchmarks)
- Waveshaper: ~0.05% (stateless, simple math)
- OnePoleHP (derivative): ~0.02%
- OnePoleSmoother: ~0.02%
- Mode-specific math: ~0.05%

**Total estimated**: ~0.24% - well within budget

### 4.2 Memory

Fixed allocations only:
- EnvelopeFollower state
- Waveshaper (trivially copyable)
- OnePoleHP state
- OnePoleSmoother state
- Hysteresis state (single float)

No dynamic allocation in processing path.

---

## 5. Edge Case Handling

### 5.1 Silence/Near-Zero Input

**EnvelopeFollow/InverseEnvelope**:
- Envelope decays toward 0
- InverseEnvelope uses floor value to prevent divide-by-zero
- Drive settles toward baseDrive (envelope at floor produces bounded behavior)

**Hysteresis**:
- Memory state decays toward 0 (neutral)
- Drive settles toward baseDrive

### 5.2 NaN/Inf Input (FR-027)

Standard approach per constitution:
```cpp
if (detail::isNaN(input) || detail::isInf(input)) {
    reset();  // Clear all state
    return 0.0f;
}
```

### 5.3 Zero Base Drive (FR-029)

```cpp
if (baseDrive_ <= 0.0f) {
    return 0.0f;  // Output silence
}
```

### 5.4 Zero Drive Modulation (FR-028)

When `driveModulation_ == 0.0f`, all modes produce constant drive equal to baseDrive - effectively static waveshaping.

---

## 6. API Design Decisions

### 6.1 Derivative Sensitivity

The derivative filter output scale depends on sample rate and envelope dynamics. A sensitivity multiplier normalizes this:

**Decision**: Internal fixed sensitivity of 10.0 (`kDerivativeSensitivity`) tuned for typical signals. No user parameter needed since driveModulation already controls intensity. This value normalizes the highpass filter output to produce musically useful drive changes for typical envelope dynamics.

### 6.2 Derivative Filter Cutoff

**Decision**: Fixed at 10 Hz (`kDerivativeFilterHz`) for optimal transient detection. Chosen from 5-20 Hz range as the sweet spot balancing transient response with noise rejection. Could be exposed as advanced parameter in future if needed.

### 6.3 Hysteresis Parameters

Per spec:
- `hysteresisDepth`: [0.0, 1.0] - how much history affects processing
- `hysteresisDecay`: [1, 500] ms - how fast memory fades

These are separate from the main driveModulation parameter.

---

## 7. Testing Strategy

### 7.1 Unit Tests

1. **EnvelopeFollow mode**: Process signal with varying amplitude, verify drive correlation
2. **InverseEnvelope mode**: Verify inverse relationship, verify max drive cap
3. **Derivative mode**: Process transients vs sustained, verify difference
4. **Hysteresis mode**: Process rising vs falling signals, verify path dependence
5. **Mode switching**: Verify no clicks with constant tone input

### 7.2 Metrics for Success Criteria

- **SC-001/SC-002**: Measure THD at different envelope levels, verify 6dB difference
- **SC-003**: Analyze drum hit harmonic content at attack vs sustain
- **SC-004**: Compare output of identical amplitudes reached differently
- **SC-007**: Spectral analysis for discontinuities during mode switch

---

## 8. Alternatives Considered

### 8.1 True Derivative vs Highpass

**Alternative**: Use `x[n] - x[n-1]` for derivative
**Rejected Because**: Sample-rate dependent, noisy, requires additional smoothing

**Chosen**: Highpass filter on envelope - provides smoothed derivative that is sample-rate independent via cutoff frequency.

### 8.2 Physical Hysteresis Model

**Alternative**: Use Jiles-Atherton magnetic hysteresis (like TapeSaturator)
**Rejected Because**: Spec clarification specified "simple state memory with exponential decay". J-A is complex, CPU intensive, and overkill for this creative effect.

**Chosen**: Simple exponential memory state - captures path-dependence concept with minimal complexity.

### 8.3 Separate Mode Processors

**Alternative**: Create separate classes for each mode
**Rejected Because**: All modes share: envelope tracking, waveshaping, parameter smoothing. Only drive calculation differs.

**Chosen**: Single class with mode enum and internal dispatch.

---

## 9. Summary

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Envelope detection | Reuse EnvelopeFollower (RMS mode) | Full-featured, tested, spec-compliant |
| Waveshaping | Reuse Waveshaper | All needed curve types, per-sample drive |
| Derivative calc | OnePoleHP on envelope | Smooth, sample-rate independent |
| Hysteresis model | Simple exponential decay | Per clarification, sufficient for effect |
| Reference level | -12 dBFS RMS (0.251189 linear) | Per clarification |
| Max inverse drive | 20.0 | Per clarification |
| Mode switching | Smooth effectiveDrive | Simple, effective |
