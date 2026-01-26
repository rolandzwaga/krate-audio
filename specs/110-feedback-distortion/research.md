# Research: Feedback Distortion Processor

**Feature**: 110-feedback-distortion
**Date**: 2026-01-26

## Research Questions

### 1. Soft Limiter Algorithm

**Question**: What attack and release times, and what compression algorithm should the soft limiter use?

**Answer** (from spec clarifications):
- Attack: 0.5ms (fast to catch spikes)
- Release: 50ms (natural breathing, no pumping artifacts)
- Algorithm: Tanh-based soft clipping

**Implementation Approach**:
```cpp
// Use EnvelopeFollower in Peak mode for fast attack
limiterEnvelope_.setMode(DetectionMode::Peak);
limiterEnvelope_.setAttackTime(0.5f);   // Fast attack
limiterEnvelope_.setReleaseTime(50.0f); // Natural release

// Soft limiting with tanh
float envelope = limiterEnvelope_.processSample(std::abs(signal));
if (envelope > thresholdLinear_) {
    float ratio = envelope / thresholdLinear_;
    float gainReduction = thresholdLinear_ / envelope * std::tanh(ratio);
    signal *= gainReduction;
}
```

**Rationale**:
- Peak mode gives instant response to transients
- 0.5ms attack catches feedback spikes before they grow
- 50ms release allows natural breathing without pumping
- Tanh provides smooth saturation character (spec requirement)

### 2. Feedback Loop Stability

**Question**: How to handle delay time modulation in a feedback loop?

**Answer**: Use linear interpolation for the delay read, not allpass.

**Rationale**:
- Allpass interpolation maintains state that creates artifacts when delay time changes
- The delay time is externally smoothed (10ms time constant), so linear interpolation is sufficient
- Linear interpolation is simpler and faster

**Implementation**:
```cpp
float delaySamples = smoothedDelayMs * sampleRate_ / 1000.0f;
float delayedSample = delayLine_.readLinear(delaySamples);
```

### 3. DC Blocking Placement

**Question**: Where should DC blocking occur in the signal chain?

**Answer**: After saturation, before the feedback is applied.

**Signal Flow**:
```
Input -> [+] -> DelayLine -> Waveshaper -> Biquad -> DCBlocker -> SoftLimiter -> Output
          ^                                                            |
          +------------------------ * feedback ------------------------+
```

**Rationale**:
- Asymmetric saturation (Tube, Diode) generates DC offset
- DC must be blocked before feedback to prevent buildup
- Placing after tone filter is fine since DC passes through lowpass anyway

### 4. Parameter Smoothing

**Question**: What smoothing time constant should be used for parameters?

**Answer**: 10ms for all parameters (consistent with existing project patterns).

**Implementation**:
```cpp
static constexpr float kSmoothingTimeMs = 10.0f;

// In prepare():
delayTimeSmoother_.configure(kSmoothingTimeMs, sampleRate_);
feedbackSmoother_.configure(kSmoothingTimeMs, sampleRate_);
driveSmoother_.configure(kSmoothingTimeMs, sampleRate_);
thresholdSmoother_.configure(kSmoothingTimeMs, sampleRate_);
toneFreqSmoother_.configure(kSmoothingTimeMs, sampleRate_);
```

**Rationale**:
- 10ms is the standard smoothing time in this project (see OnePoleSmoother default)
- Fast enough to feel responsive
- Slow enough to prevent audible clicks

### 5. Tone Filter Design

**Question**: What Q value should the tone filter use?

**Answer** (from spec clarifications): Q = 0.707 (Butterworth)

**Implementation**:
```cpp
toneFilter_.configure(
    FilterType::Lowpass,
    toneFrequencyHz_,
    kButterworthQ,  // 0.707
    0.0f,           // gainDb (unused for lowpass)
    sampleRate_
);
```

**Rationale**:
- Butterworth provides maximally flat passband
- No resonance peaks that could interact badly with high feedback
- Neutral tonal character (user can shape with frequency alone)

## Existing Component Analysis

### Components Verified for Reuse

| Component | Verification | Notes |
|-----------|--------------|-------|
| DelayLine | API verified | `prepare()`, `reset()`, `write()`, `readLinear()` |
| Waveshaper | API verified | `setType()`, `setDrive()`, `process()` |
| Biquad | API verified | `configure()`, `process()`, `reset()` |
| DCBlocker | API verified | `prepare()`, `process()`, `reset()` |
| OnePoleSmoother | API verified | `configure()`, `setTarget()`, `process()`, `snapTo()` |
| EnvelopeFollower | API verified | `prepare()`, `setMode()`, `setAttackTime()`, `setReleaseTime()`, `processSample()`, `reset()` |

### Patterns Borrowed From

| Source | Pattern Borrowed |
|--------|-----------------|
| `temporal_distortion.h` | Layer 2 processor structure, parameter smoothing pattern |
| `dynamics_processor.h` | EnvelopeFollower for level detection, threshold-based gain calculation |
| `envelope_follower.h` | Peak detection mode configuration |

## Decisions Log

| # | Decision | Rationale | Alternatives Rejected |
|---|----------|-----------|----------------------|
| 1 | Use EnvelopeFollower for limiter | Already tested, matches project patterns | Custom envelope (reinventing wheel) |
| 2 | Linear interpolation for delay | Delay time is externally smoothed | Allpass (artifacts with modulated delay) |
| 3 | Internal soft limiter logic | Tight coupling with state | Standalone SoftLimiter class (overkill) |
| 4 | Tanh for soft clipping | Spec requirement, matches Waveshaper | Other saturation curves |
| 5 | DC blocking after saturation | Removes DC before feedback accumulation | DC blocking at input (misses saturation DC) |
| 6 | 10ms parameter smoothing | Project standard | Faster/slower times |
| 7 | Butterworth Q for tone | Spec requirement, neutral response | Higher Q (resonance issues with feedback) |
